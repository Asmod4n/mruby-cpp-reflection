#pragma once
#if defined(__cpp_impl_reflection)

#include <cstddef>
#include <cstdint>
#include <mruby/common.h>
struct mrb_state;
typedef struct mrb_state mrb_state;
#include <mruby/value.h>

#include <algorithm>
#include <functional>
#include <meta>
#include <span>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct RClass;

namespace mrb_cpp_reflector
{

consteval std::string_view reflect_operator_method(const std::meta::info function)
{
    using enum std::meta::operators;
    const bool unary = std::meta::parameters_of(function).empty();
    switch (std::meta::operator_of(function)) {
    case op_plus: return unary ? "+@" : "+";
    case op_minus: return unary ? "-@" : "-";
    case op_star: return unary ? "" : "*";
    case op_ampersand: return unary ? "" : "&";
    case op_slash: return "/";
    case op_percent: return "%";
    case op_caret: return "^";
    case op_pipe: return "|";
    case op_tilde: return "~";
    case op_exclamation: return "!";
    case op_less_less: return "<<";
    case op_greater_greater: return ">>";
    case op_equals_equals: return "==";
    case op_exclamation_equals: return "!=";
    case op_less: return "<";
    case op_greater: return ">";
    case op_less_equals: return "<=";
    case op_greater_equals: return ">=";
    case op_spaceship: return "<=>";
    case op_square_brackets: return "[]";
    case op_parentheses: return "call";
    default: return "";
    }
}

consteval std::string_view reflect_identifier(const std::meta::info named)
{
    if (std::meta::is_operator_function(named)) return reflect_operator_method(named);
    if (std::meta::has_identifier(named)) return std::meta::identifier_of(named);
    const std::meta::info bare = std::meta::is_type(named) ? std::meta::dealias(std::meta::remove_cvref(named)) : named;
    if (std::meta::has_identifier(bare)) return std::meta::identifier_of(bare);
    if (std::meta::has_template_arguments(bare)) return std::meta::identifier_of(std::meta::template_of(bare));
    return std::define_static_string(std::meta::display_string_of(bare));
}

consteval std::string reflect_class_name(const std::meta::info named)
{
    std::string name;
    bool upper = true;
    for (const char c : reflect_identifier(named)) {
        if (c == '_') { upper = true; continue; }
        name += upper && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        upper = false;
    }
    return name;
}

consteval std::vector<std::meta::info> reflect_namespaces(const std::meta::info type)
{
    std::vector<std::meta::info> chain;
    for (std::meta::info scope = std::meta::parent_of(std::meta::dealias(type)); std::meta::has_parent(scope); scope = std::meta::parent_of(scope))
        if (std::meta::is_namespace(scope) && std::meta::has_identifier(scope) && !std::meta::identifier_of(scope).starts_with("__"))
            chain.insert(chain.begin(), scope);
    return chain;
}

consteval std::meta::info reflect_bare(const std::meta::info type)
{
    return std::meta::dealias(std::meta::remove_cvref(type));
}

consteval bool reflect_is_view(const std::meta::info bare)
{
    if (bare == std::meta::dealias(^^std::string_view)) return true;
    return std::meta::has_template_arguments(bare) && std::meta::template_of(bare) == ^^std::span;
}

consteval bool reflect_is_object(const std::meta::info type)
{
    const std::meta::info bare = reflect_bare(type);
    if (bare == std::meta::dealias(^^mrb_value) || bare == std::meta::dealias(^^std::string_view) || reflect_is_view(bare)) return false;
    return std::meta::is_class_type(bare);
}

consteval char reflect_get_args_letter(const std::meta::info type)
{
    const std::meta::info t = reflect_bare(type);
    if (std::meta::is_pointer_type(t)) {
        const std::meta::info to = std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(t)));
        if (to == ^^char) return 'z';
        return std::meta::is_class_type(to) ? 'o' : '\0';
    }
    if (t == std::meta::dealias(^^mrb_value)) return 'o';
    if (t == ^^bool || t == std::meta::dealias(^^mrb_bool)) return 'b';
    if (t == std::meta::dealias(^^mrb_sym)) return 'n';
    if (t == ^^RClass *) return 'c';
    if (t == std::meta::dealias(^^std::string_view)) return 's';
    if (t == std::meta::dealias(^^std::span<const mrb_value>)) return '*';
    if (std::meta::is_integral_type(t)) return 'i';
    if (std::meta::is_floating_point_type(t)) return 'f';
    if (std::meta::is_class_type(t)) return 'o';
    return '\0';
}

consteval bool reflect_mutates(const std::meta::info type)
{
    if (std::meta::is_lvalue_reference_type(type)) return !std::meta::is_const_type(std::meta::remove_reference(type));
    if (std::meta::is_pointer_type(std::meta::dealias(type))) return !std::meta::is_const_type(std::meta::remove_pointer(std::meta::dealias(type)));
    return false;
}

consteval bool reflect_is_function(const std::meta::info type)
{
    const std::meta::info t = std::meta::dealias(std::meta::remove_cvref(type));
    return std::meta::has_template_arguments(t) && std::meta::template_of(t) == ^^std::function;
}

consteval bool reflect_takes_block(const std::meta::info function)
{
    const auto parameters = std::meta::parameters_of(function);
    return !parameters.empty() && reflect_is_function(std::meta::type_of(parameters.back()));
}

consteval std::size_t reflect_required(const std::meta::info function)
{
    std::size_t required = 0;
    for (const std::meta::info p : std::meta::parameters_of(function)) {
        if (std::meta::has_default_argument(p)) break;
        required++;
    }
    return required;
}

consteval bool reflect_rest(const std::meta::info function)
{
    const std::vector<std::meta::info> p = std::meta::parameters_of(function);
    return !p.empty() && reflect_bare(std::meta::type_of(p.back())) == std::meta::dealias(^^std::span<const mrb_value>);
}

consteval bool reflect_is_iterator(const std::meta::info bare)
{
    if (!std::meta::is_class_type(bare)) return false;
    if (std::meta::has_template_arguments(bare) && std::meta::template_of(bare) == ^^std::allocator) return true;
    for (const std::meta::info m : std::meta::members_of(bare, std::meta::access_context::unchecked()))
        if (std::meta::is_type_alias(m) && std::meta::has_identifier(m) && std::meta::identifier_of(m) == "iterator_category") return true;
    return false;
}

consteval bool reflect_parameter_supported(const std::meta::info type)
{
    if (reflect_get_args_letter(type) == '\0') return false;
    if (reflect_mutates(type) && !reflect_is_object(type)) return false;
    if (std::meta::is_pointer_type(reflect_bare(type))) {
        const std::meta::info to = std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(reflect_bare(type))));
        if (std::meta::is_class_type(to) && !std::meta::is_complete_type(to)) return false;
    }
    if (reflect_is_object(type)) {
        const std::meta::info bare = reflect_bare(type);
        if (!std::meta::is_complete_type(bare) || reflect_is_iterator(bare) || std::meta::is_abstract_type(bare)) return false;
        if (!std::meta::is_lvalue_reference_type(type) && !std::meta::is_pointer_type(std::meta::dealias(type)) && !std::meta::is_copy_constructible_type(bare)) return false;
    }
    return true;
}

consteval bool reflect_result_supported(const std::meta::info type)
{
    const std::meta::info bare = reflect_bare(type);
    if (bare == ^^void || bare == std::meta::dealias(^^mrb_value) || bare == ^^bool || std::meta::is_arithmetic_type(bare)) return true;
    if (std::meta::is_pointer_type(bare)) {
        const std::meta::info to = std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(bare)));
        return to == ^^char || (std::meta::is_class_type(to) && std::meta::is_complete_type(to));
    }
    if (!std::meta::is_class_type(bare) || !std::meta::is_complete_type(bare) || std::meta::is_abstract_type(bare) || reflect_is_iterator(bare)) return false;
    if (std::meta::has_template_arguments(bare) && std::meta::template_of(bare) == ^^std::pair) return true;
    return std::meta::is_reference_type(type) || std::meta::is_move_constructible_type(bare);
}

consteval bool reflect_call_supported(const std::meta::info function)
{
    if (std::meta::is_template(function) || std::meta::is_function_template(function)) return false;
    if (std::meta::is_vararg_function(function)) return false;
    if (std::meta::is_rvalue_reference_qualified(function) || std::meta::is_volatile(function)) return false;
    for (const std::meta::info p : std::meta::parameters_of(function))
        if (!reflect_parameter_supported(std::meta::type_of(p))) return false;
    return std::meta::is_constructor(function) || reflect_result_supported(std::meta::return_type_of(function));
}

consteval std::vector<std::meta::info> reflect_direct_bases(const std::meta::info type)
{
    std::vector<std::meta::info> bases;
    if (!std::meta::is_class_type(std::meta::dealias(type)) || !std::meta::is_complete_type(std::meta::dealias(type))) return bases;
    for (const std::meta::info b : std::meta::bases_of(std::meta::dealias(type), std::meta::access_context::current()))
        if (std::meta::is_public(b) && !std::meta::is_virtual(b)) bases.push_back(std::meta::dealias(std::meta::type_of(b)));
    return bases;
}

consteval std::vector<std::meta::info> reflect_bases(const std::meta::info type)
{
    std::vector<std::meta::info> bases;
    for (const std::meta::info b : reflect_direct_bases(type)) {
        bases.push_back(b);
        for (const std::meta::info a : reflect_bases(b)) bases.push_back(a);
    }
    return bases;
}

template <std::meta::info Type>
consteval auto reflect_members_computed()
{
    std::vector<std::meta::info> methods;
    for (const std::meta::info m : std::meta::members_of(std::meta::dealias(Type), std::meta::access_context::current()))
        if (std::meta::is_function(m) && !std::meta::is_static_member(m) &&
            !std::meta::is_special_member_function(m) &&
            (std::meta::is_operator_function(m) ? !reflect_operator_method(m).empty() : std::meta::has_identifier(m)) &&
            reflect_call_supported(m))
            methods.push_back(m);
    return std::define_static_array(methods);
}

template <std::meta::info Type>
consteval auto reflect_constructors_computed()
{
    std::vector<std::meta::info> constructors;
    for (const std::meta::info m : std::meta::members_of(std::meta::dealias(Type), std::meta::access_context::current()))
        if (std::meta::is_constructor(m) && !std::meta::is_deleted(m) && !std::meta::is_move_constructor(m) && reflect_call_supported(m))
            constructors.push_back(m);
    return std::define_static_array(constructors);
}

template <std::meta::info Type>
inline constexpr auto reflect_constructors_cached = reflect_constructors_computed<Type>();

template <std::meta::info Type>
consteval auto reflect_constructors()
{
    return reflect_constructors_cached<Type>;
}

template <std::meta::info Type>
consteval auto reflect_converting_constructors_computed()
{
    std::vector<std::meta::info> constructors;
    for (const std::meta::info c : reflect_constructors<Type>())
        if (!std::meta::is_explicit(c) && !std::meta::is_copy_constructor(c) && !std::meta::parameters_of(c).empty() && reflect_required(c) <= 1)
            constructors.push_back(c);
    return std::define_static_array(constructors);
}

template <std::meta::info Type>
inline constexpr auto reflect_converting_constructors_cached = reflect_converting_constructors_computed<Type>();

template <std::meta::info Type>
consteval auto reflect_converting_constructors()
{
    return reflect_converting_constructors_cached<Type>;
}

template <std::meta::info Type>
consteval auto reflect_static_functions_computed()
{
    std::vector<std::meta::info> functions;
    for (const std::meta::info m : std::meta::members_of(std::meta::dealias(Type), std::meta::access_context::current()))
        if (std::meta::is_function(m) && std::meta::is_static_member(m) &&
            (std::meta::is_operator_function(m) ? !reflect_operator_method(m).empty() : std::meta::has_identifier(m)) &&
            reflect_call_supported(m))
            functions.push_back(m);
    return std::define_static_array(functions);
}

template <std::meta::info Type>
inline constexpr auto reflect_static_functions_cached = reflect_static_functions_computed<Type>();

template <std::meta::info Type>
consteval auto reflect_static_functions()
{
    return reflect_static_functions_cached<Type>;
}

template <std::meta::info Type>
consteval auto reflect_static_data_members_computed()
{
    std::vector<std::meta::info> members;
    for (const std::meta::info m : std::meta::members_of(std::meta::dealias(Type), std::meta::access_context::current()))
        if (std::meta::is_variable(m) && std::meta::is_static_member(m) && std::meta::has_identifier(m) && reflect_result_supported(std::meta::type_of(m)))
            members.push_back(m);
    return std::define_static_array(members);
}

template <std::meta::info Type>
inline constexpr auto reflect_static_data_members_cached = reflect_static_data_members_computed<Type>();

template <std::meta::info Type>
consteval auto reflect_static_data_members()
{
    return reflect_static_data_members_cached<Type>;
}

template <std::meta::info Type>
inline constexpr auto reflect_members_cached = reflect_members_computed<Type>();

template <std::meta::info Type>
consteval auto reflect_members()
{
    return reflect_members_cached<Type>;
}

template <std::meta::info Type>
consteval auto reflect_fields_computed()
{
    std::vector<std::meta::info> fields;
    for (const std::meta::info m : std::meta::nonstatic_data_members_of(std::meta::dealias(Type), std::meta::access_context::current()))
        if (std::meta::has_identifier(m) && !std::meta::is_bit_field(m) && reflect_result_supported(std::meta::type_of(m)))
            fields.push_back(m);
    return std::define_static_array(fields);
}

template <std::meta::info Type>
inline constexpr auto reflect_fields_cached = reflect_fields_computed<Type>();

template <std::meta::info Type>
consteval auto reflect_fields()
{
    return reflect_fields_cached<Type>;
}

template <std::meta::info... Types>
consteval std::array<std::meta::info, sizeof...(Types)> reflect()
{
    return {Types...};
}

template <std::meta::info Type, std::meta::info Member>
consteval auto reflect_overloads()
{
    std::vector<std::meta::info> same;
    for (const std::meta::info m : std::meta::is_static_member(Member) ? reflect_static_functions<Type>() : reflect_members<Type>())
        if (reflect_identifier(m) == reflect_identifier(Member)) same.push_back(m);
    return std::define_static_array(same);
}

template <std::meta::info Type>
void reflect_names_into(std::vector<std::string_view> &names)
{
    template for (constexpr std::meta::info scope : std::define_static_array(reflect_namespaces(Type)))
        names.push_back(std::define_static_string(reflect_class_name(scope)));
    names.push_back(std::define_static_string(reflect_class_name(Type)));
    template for (constexpr std::meta::info member : reflect_members<Type>())
        names.push_back(reflect_identifier(member));
    template for (constexpr std::meta::info function : reflect_static_functions<Type>())
        names.push_back(reflect_identifier(function));
    template for (constexpr std::meta::info member : reflect_static_data_members<Type>())
        names.push_back(std::meta::identifier_of(member));
    template for (constexpr std::meta::info field : reflect_fields<Type>())
        names.push_back(std::meta::identifier_of(field));
}

template <std::meta::info Type>
void reflect_setter_names_into(std::vector<std::string_view> &names)
{
    template for (constexpr std::meta::info field : reflect_fields<Type>())
        if constexpr (!std::meta::is_const_type(std::meta::type_of(field))) names.push_back(std::meta::identifier_of(field));
    template for (constexpr std::meta::info member : reflect_static_data_members<Type>())
        if constexpr (!std::meta::is_const_type(std::meta::type_of(member))) names.push_back(std::meta::identifier_of(member));
}

template <auto Classes>
std::string reflect_presyms_header()
{
    std::string out = "#pragma once\n#include <array>\n#include <string_view>\n#include <utility>\n#include <mruby.h>\n"
                      "inline constexpr std::array<std::pair<std::string_view, mrb_sym>, ";
    std::vector<std::string_view> names, setters;
    template for (constexpr std::meta::info type : Classes) {
        reflect_names_into<type>(names);
        reflect_setter_names_into<type>(setters);
    }
    for (const std::string_view fixed : {"to_s", "to_str", "to_a", "to_ary", "to_h", "to_hash", "each", "initialize", "owner", "Enumerable"})
        names.push_back(fixed);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    std::sort(setters.begin(), setters.end());
    setters.erase(std::unique(setters.begin(), setters.end()), setters.end());
    out += std::to_string(names.size() + setters.size()) + "> reflect_presyms{{\n";
    for (const std::string_view name : names)
        out += "    {\"" + std::string(name) + "\", MRB_SYM(" + std::string(name) + ")},\n";
    for (const std::string_view name : setters)
        out += "    {\"" + std::string(name) + "=\", MRB_SYM_E(" + std::string(name) + ")},\n";
    out += "}};\n";
    return out;
}

}

#endif
