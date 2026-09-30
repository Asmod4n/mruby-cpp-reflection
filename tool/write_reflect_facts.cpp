#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Attr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/MacroInfo.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Options/OptionUtils.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/FileSystem.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

struct ParameterExtent {
    unsigned line;
    unsigned column;
    unsigned position;
    std::uint64_t extent;
    auto operator<=>(const ParameterExtent &) const = default;
};

struct FormatAttribute {
    unsigned line;
    unsigned column;
    std::string archetype;
    unsigned string_index;
    unsigned first_to_check;
    auto operator<=>(const FormatAttribute &) const = default;
};

struct CallbackDestination {
    unsigned line;
    unsigned column;
    unsigned position;
    bool appends;
    int holder;
    std::string field;
    auto operator<=>(const CallbackDestination &) const = default;
};

struct Facts {
    std::map<std::string, std::set<CallbackDestination>> callback_destinations;
    std::map<std::string, std::set<ParameterExtent>> parameter_extents;
    std::map<std::string, std::set<FormatAttribute>> format_attributes;
    std::vector<std::string> macro_candidates;
    std::set<std::string> macros;
};

std::string string_literal(const std::string_view text)
{
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

bool is_callback_type(const clang::QualType type)
{
    const clang::QualType bare = type.getNonReferenceType().getCanonicalType();
    if (bare->isFunctionPointerType())
        return true;
    const auto *specialization = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(bare->getAsRecordDecl());
    if (!specialization || !specialization->isInStdNamespace())
        return false;
    const llvm::StringRef name = specialization->getName();
    return name == "function" || name == "move_only_function" || name == "copyable_function";
}

bool names_parameter(const clang::Expr *expression, const clang::ParmVarDecl *parameter)
{
    for (;;) {
        expression = expression->IgnoreParenImpCasts()->IgnoreImplicit();
        if (const auto *construct = llvm::dyn_cast<clang::CXXConstructExpr>(expression); construct && construct->getNumArgs() == 1) {
            expression = construct->getArg(0);
            continue;
        }
        if (const auto *call = llvm::dyn_cast<clang::CallExpr>(expression); call && call->getNumArgs() == 1) {
            const clang::FunctionDecl *callee = call->getDirectCallee();
            if (callee && callee->isInStdNamespace() && (callee->getName() == "move" || callee->getName() == "forward")) {
                expression = call->getArg(0);
                continue;
            }
        }
        break;
    }
    const auto *reference = llvm::dyn_cast<clang::DeclRefExpr>(expression);
    return reference && reference->getDecl() == parameter;
}

std::optional<std::pair<int, std::string>> holder_of(const clang::Expr *target, const clang::FunctionDecl *function)
{
    const auto *member = llvm::dyn_cast<clang::MemberExpr>(target->IgnoreParenImpCasts());
    if (!member)
        return std::nullopt;
    const clang::Expr *base = member->getBase()->IgnoreParenImpCasts();
    if (llvm::isa<clang::CXXThisExpr>(base))
        return std::pair{-1, member->getMemberDecl()->getNameAsString()};
    if (const auto *reference = llvm::dyn_cast<clang::DeclRefExpr>(base))
        if (const auto *parameter = llvm::dyn_cast<clang::ParmVarDecl>(reference->getDecl()); parameter && parameter->getDeclContext() == function)
            return std::pair{static_cast<int>(parameter->getFunctionScopeIndex()), member->getMemberDecl()->getNameAsString()};
    return std::nullopt;
}

struct DestinationVisitor : clang::RecursiveASTVisitor<DestinationVisitor> {
    const clang::FunctionDecl *function;
    const clang::ParmVarDecl *parameter;
    std::optional<std::tuple<bool, int, std::string>> found;

    void take(const bool appends, const clang::Expr *target)
    {
        if (const std::optional<std::pair<int, std::string>> holder = holder_of(target, function))
            found = std::tuple{appends, holder->first, holder->second};
    }

    bool VisitCXXOperatorCallExpr(clang::CXXOperatorCallExpr *call)
    {
        if (call->getOperator() == clang::OO_Equal && call->getNumArgs() == 2 && names_parameter(call->getArg(1), parameter))
            take(false, call->getArg(0));
        return true;
    }

    bool VisitBinaryOperator(clang::BinaryOperator *assignment)
    {
        if (assignment->getOpcode() == clang::BO_Assign && names_parameter(assignment->getRHS(), parameter))
            take(false, assignment->getLHS());
        return true;
    }

    bool VisitCXXMemberCallExpr(clang::CXXMemberCallExpr *call)
    {
        const clang::CXXMethodDecl *method = call->getMethodDecl();
        if (!method)
            return true;
        const llvm::StringRef name = method->getName();
        if (name != "push_back" && name != "emplace_back" && name != "push_front" && name != "emplace_front" && name != "insert" &&
            name != "emplace")
            return true;
        if (std::ranges::any_of(call->arguments(), [this](const clang::Expr *argument) { return names_parameter(argument, parameter); }))
            take(true, call->getImplicitObjectArgument());
        return true;
    }
};

struct FunctionVisitor : clang::RecursiveASTVisitor<FunctionVisitor> {
    clang::ASTContext &context;
    Facts &facts;

    bool VisitFunctionDecl(clang::FunctionDecl *function)
    {
        const clang::SourceManager &sources = context.getSourceManager();
        const clang::SourceLocation spelled = sources.getSpellingLoc(function->getLocation());
        if (spelled.isInvalid() || sources.isInSystemHeader(spelled))
            return true;
        const clang::PresumedLoc where = sources.getPresumedLoc(spelled);
        if (where.isInvalid())
            return true;
        const clang::QualType va_list = context.getCanonicalType(context.getBuiltinVaListType());
        for (const clang::ParmVarDecl *parameter : function->parameters())
            if (const clang::ConstantArrayType *array = context.getAsConstantArrayType(parameter->getOriginalType());
                array && context.getCanonicalType(parameter->getOriginalType()) != va_list)
                facts.parameter_extents[where.getFilename()].insert(
                    {where.getLine(), where.getColumn(), parameter->getFunctionScopeIndex(), array->getSize().getZExtValue()});
        if (const clang::FunctionDecl *definition = function->getDefinition(); definition && definition->getBody())
            for (const clang::ParmVarDecl *parameter : definition->parameters()) {
                if (!is_callback_type(parameter->getType()))
                    continue;
                DestinationVisitor destinations{.function = definition, .parameter = parameter, .found = std::nullopt};
                destinations.TraverseStmt(definition->getBody());
                if (destinations.found)
                    facts.callback_destinations[where.getFilename()].insert({where.getLine(), where.getColumn(), parameter->getFunctionScopeIndex(),
                                                                             std::get<0>(*destinations.found), std::get<1>(*destinations.found),
                                                                             std::get<2>(*destinations.found)});
            }
        for (const clang::FormatAttr *format : function->specific_attrs<clang::FormatAttr>())
            facts.format_attributes[where.getFilename()].insert({where.getLine(), where.getColumn(), format->getType()->getName().str(),
                                                                 static_cast<unsigned>(format->getFormatIdx()),
                                                                 static_cast<unsigned>(format->getFirstArg())});
        return true;
    }
};

struct DeclarationConsumer : clang::ASTConsumer {
    clang::Preprocessor &preprocessor;
    Facts &facts;
    DeclarationConsumer(clang::Preprocessor &given_preprocessor, Facts &given_facts) : preprocessor(given_preprocessor), facts(given_facts) {}

    void HandleTranslationUnit(clang::ASTContext &context) override
    {
        FunctionVisitor visitor{.context = context, .facts = facts};
        visitor.TraverseDecl(context.getTranslationUnitDecl());
        const clang::SourceManager &sources = context.getSourceManager();
        for (const auto &[identifier, state] : preprocessor.macros()) {
            const clang::MacroInfo *macro = preprocessor.getMacroInfo(identifier);
            if (!macro || !macro->isObjectLike() || macro->isBuiltinMacro() || macro->getNumTokens() == 0)
                continue;
            const clang::SourceLocation defined = macro->getDefinitionLoc();
            if (defined.isInvalid() || sources.isInSystemHeader(defined) || sources.isWrittenInBuiltinFile(defined) ||
                sources.isWrittenInCommandLineFile(defined))
                continue;
            if (identifier->getName().starts_with("_"))
                continue;
            facts.macro_candidates.push_back(identifier->getName().str());
        }
        std::ranges::sort(facts.macro_candidates);
    }
};

struct DeclarationAction : clang::ASTFrontendAction {
    Facts &facts;
    explicit DeclarationAction(Facts &given) : facts(given) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef) override
    {
        return std::make_unique<DeclarationConsumer>(compiler.getPreprocessor(), facts);
    }
};

struct MacroVisitor : clang::RecursiveASTVisitor<MacroVisitor> {
    Facts &facts;

    bool VisitVarDecl(clang::VarDecl *variable)
    {
        const auto *scope = llvm::dyn_cast<clang::NamespaceDecl>(variable->getDeclContext());
        if (!scope || scope->getName() != "mrb_reflect_facts_trial" || variable->isInvalidDecl() || !variable->getInit())
            return true;
        if (!variable->getInit()->isCXX11ConstantExpr(variable->getASTContext()))
            return true;
        const std::string_view name = variable->getName();
        std::size_t index = 0;
        std::istringstream(std::string(name.substr(std::string_view("value_").size()))) >> index;
        facts.macros.insert(facts.macro_candidates.at(index));
        return true;
    }
};

struct MacroConsumer : clang::ASTConsumer {
    Facts &facts;
    explicit MacroConsumer(Facts &given) : facts(given) {}

    void HandleTranslationUnit(clang::ASTContext &context) override
    {
        MacroVisitor visitor{.facts = facts};
        visitor.TraverseDecl(context.getTranslationUnitDecl());
    }
};

struct MacroAction : clang::ASTFrontendAction {
    Facts &facts;
    explicit MacroAction(Facts &given) : facts(given) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &, llvm::StringRef) override
    {
        return std::make_unique<MacroConsumer>(facts);
    }
};

template <class Action>
struct ActionFactory : clang::tooling::FrontendActionFactory {
    Facts &facts;
    explicit ActionFactory(Facts &given) : facts(given) {}

    std::unique_ptr<clang::FrontendAction> create() override
    {
        return std::make_unique<Action>(facts);
    }
};

std::vector<std::string> without_dependency_output(const std::vector<std::string> &arguments)
{
    std::vector<std::string> kept;
    for (auto at = arguments.begin(); at != arguments.end(); ++at) {
        if (*at == "-MF" || *at == "-MT" || *at == "-MQ") {
            if (std::next(at) != arguments.end())
                ++at;
            continue;
        }
        if (*at == "-MD" || *at == "-MMD")
            continue;
        kept.push_back(*at);
    }
    return kept;
}

std::string header_text(const Facts &facts)
{
    std::string out = "#pragma once\n#include <mruby.h>\n#include <mruby/cpp_reflection_lifetime.hpp>\n#if defined(__cpp_impl_reflection)\n";
    for (const auto &[file, rows] : facts.parameter_extents) {
        out += std::format("template <>\ninline constexpr std::span<const mruby::cpp_reflection::parameter_extent> "
                           "mruby::cpp_reflection::parameter_extents<std::define_static_string({})> = std::define_static_array(std::array{{\n",
                           string_literal(file));
        for (const ParameterExtent &row : rows)
            out += std::format("    mruby::cpp_reflection::parameter_extent{{{}, {}, {}, {}}},\n", row.line, row.column, row.position, row.extent);
        out += "});\n";
    }
    for (const auto &[file, rows] : facts.format_attributes) {
        out += std::format("template <>\ninline constexpr std::span<const mruby::cpp_reflection::format_attribute> "
                           "mruby::cpp_reflection::format_attributes<std::define_static_string({})> = std::define_static_array(std::array{{\n",
                           string_literal(file));
        for (const FormatAttribute &row : rows)
            out += std::format("    mruby::cpp_reflection::format_attribute{{{}, {}, std::define_static_string({}), {}, {}}},\n", row.line, row.column, string_literal(row.archetype),
                               row.string_index, row.first_to_check);
        out += "});\n";
    }
    for (const auto &[file, rows] : facts.callback_destinations) {
        out += std::format("template <>\ninline constexpr std::span<const mruby::cpp_reflection::callback_destination> "
                           "mruby::cpp_reflection::callback_destinations<std::define_static_string({})> = std::define_static_array(std::array{{\n",
                           string_literal(file));
        for (const CallbackDestination &row : rows)
            out += std::format("    mruby::cpp_reflection::callback_destination{{{}, {}, {}, {}, {}, std::define_static_string({})}},\n", row.line, row.column,
                               row.position, row.appends, row.holder, string_literal(row.field));
        out += "});\n";
    }
    out += "namespace mruby::cpp_reflection::macros {\n";
    for (const std::string &name : facts.macros)
        out += std::format("#pragma push_macro({0})\n#undef {1}\ninline constexpr auto {1} =\n#pragma pop_macro({0})\n    ({1});\n", string_literal(name), name);
    return out + "}\n#endif\n";
}

}

int main(const int argc, const char **argv)
{
    const std::vector<std::string_view> given(argv, std::next(argv, argc));
    const auto separator = std::ranges::find(given, "--");
    if (std::distance(given.begin(), separator) != 2) {
        std::cerr << "usage: write_reflect_facts <source> -- <clang arguments>\n";
        return 2;
    }
    const std::string source(given.at(1));
    std::vector<std::string> arguments(std::next(separator), given.end());
    llvm::SmallString<256> clang_binary;
    if (llvm::sys::fs::real_path(WRITE_REFLECT_FACTS_LLVM_BINDIR "/clang", clang_binary)) {
        std::cerr << "write_reflect_facts: " WRITE_REFLECT_FACTS_LLVM_BINDIR "/clang does not exist\n";
        return 1;
    }
    arguments.push_back("-resource-dir=" + clang::GetResourcesPath(clang_binary));

    Facts facts;
    clang::tooling::FixedCompilationDatabase declarations_database(".", arguments);
    clang::tooling::ClangTool declarations(declarations_database, {source});
    declarations.clearArgumentsAdjusters();
    declarations.appendArgumentsAdjuster(clang::tooling::getClangSyntaxOnlyAdjuster());
    ActionFactory<DeclarationAction> declaration_factory(facts);
    if (declarations.run(&declaration_factory) != 0)
        return 1;

    std::ifstream file(source);
    std::string trial((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    trial += "\nnamespace mrb_reflect_facts_trial {\n";
    for (std::size_t index = 0; index < facts.macro_candidates.size(); ++index)
        trial += std::format("inline constexpr auto value_{} = ({});\n", index, facts.macro_candidates.at(index));
    trial += "}\n";
    const std::string trial_source = source + ".macros.cpp";
    clang::tooling::FixedCompilationDatabase macros_database(".", without_dependency_output(arguments));
    clang::tooling::ClangTool macros(macros_database, {trial_source});
    macros.mapVirtualFile(trial_source, trial);
    clang::IgnoringDiagConsumer ignoring;
    macros.setDiagnosticConsumer(&ignoring);
    ActionFactory<MacroAction> macro_factory(facts);
    macros.run(&macro_factory);

    std::cout << header_text(facts);
    return 0;
}
