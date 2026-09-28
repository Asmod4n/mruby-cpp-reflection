#pragma once
#if defined(__cpp_impl_reflection)

#include <cstddef>
#include <cstdint>
#include <variant>
#include <mruby/common.h>
struct mrb_state;
typedef struct mrb_state mrb_state;
#include <mruby/value.h>

#include <algorithm>
#include <functional>
#include <iterator>
#include <meta>
#include <span>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct RClass;

namespace mrb_cpp_reflector
{

consteval std::size_t reflect_skip(const std::meta::info function)
{
    return !std::meta::is_class_member(function) && std::meta::is_operator_function(function) ? 1 : 0;
}

consteval std::string_view reflect_operator_method(const std::meta::info function)
{
    using enum std::meta::operators;
    const bool unary = std::meta::parameters_of(function).size() == reflect_skip(function);
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

consteval bool reflect_is_coroutine(std::meta::info type);

consteval bool reflect_is_view(const std::meta::info bare)
{
    if (std::meta::has_template_arguments(bare) && (std::meta::template_of(bare) == ^^std::span || std::meta::template_of(bare) == ^^std::basic_string_view)) return true;
    if (!std::meta::is_class_type(bare) || !std::meta::is_complete_type(bare) || reflect_is_coroutine(bare)) return false;
    return std::meta::extract<bool>(std::meta::substitute(^^std::ranges::view, {bare}));
}

consteval bool reflect_is_object(const std::meta::info type)
{
    const std::meta::info bare = reflect_bare(type);
    if (bare == std::meta::dealias(^^mrb_value) || bare == std::meta::dealias(^^std::string_view) || reflect_is_view(bare)) return false;
    return std::meta::is_class_type(bare) || std::meta::is_enum_type(bare);
}

consteval bool reflect_is_opaque_pointer(const std::meta::info type)
{
    const std::meta::info t = std::meta::dealias(type);
    if (!std::meta::is_pointer_type(t)) return false;
    const std::meta::info to = std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(t)));
    return std::meta::is_class_type(to) && !std::meta::is_complete_type(to);
}

consteval bool reflect_is_output_parameter(const std::meta::info type)
{
    const std::meta::info t = std::meta::dealias(type);
    if (!std::meta::is_pointer_type(t)) return false;
    const std::meta::info to = std::meta::remove_pointer(t);
    return !std::meta::is_const_type(to) && !std::meta::is_volatile_type(to) && reflect_is_opaque_pointer(to);
}

consteval bool reflect_is_function_pointer(const std::meta::info type)
{
    const std::meta::info t = std::meta::dealias(type);
    return std::meta::is_pointer_type(t) && std::meta::is_function_type(std::meta::dealias(std::meta::remove_pointer(t)));
}

consteval char reflect_get_args_letter(const std::meta::info type)
{
    const std::meta::info t = reflect_bare(type);
    if (std::meta::is_pointer_type(t)) {
        const std::meta::info to = std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(t)));
        if (std::meta::is_pointer_type(to)) return reflect_is_output_parameter(type) ? 'o' : '\0';
        if (std::meta::is_function_type(to)) return 'o';
        if (to == ^^char) return 'z';
        if (to == ^^void) return 'o';
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
    if (std::meta::is_class_type(t) || std::meta::is_enum_type(t)) return 'o';
    return '\0';
}

consteval bool reflect_is_void_pointer(const std::meta::info type)
{
    const std::meta::info t = reflect_bare(type);
    return std::meta::is_pointer_type(t) && std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(t))) == ^^void;
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

consteval bool reflect_is_shared_ptr(const std::meta::info type)
{
    const std::meta::info t = std::meta::dealias(std::meta::remove_cvref(type));
    return std::meta::has_template_arguments(t) && std::meta::template_of(t) == ^^std::shared_ptr;
}

consteval bool reflect_takes_block(const std::meta::info function)
{
    const auto parameters = std::meta::parameters_of(function);
    return !parameters.empty() && reflect_is_function(std::meta::type_of(parameters.back()));
}

consteval std::size_t reflect_required(const std::meta::info function)
{
    std::size_t required = 0;
    std::size_t at = 0;
    for (const std::meta::info p : std::meta::parameters_of(function)) {
        if (at++ < reflect_skip(function)) continue;
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

consteval bool reflect_complete(const std::meta::info type)
{
    if (!std::meta::is_type(type)) return true;
    const std::meta::info t = std::meta::dealias(std::meta::remove_cvref(type));
    if (std::meta::is_pointer_type(t)) return reflect_complete(std::meta::remove_pointer(t));
    if (!std::meta::is_class_type(t)) return true;
    if (!std::meta::is_complete_type(t)) return false;
    if (std::meta::has_template_arguments(t))
        for (const std::meta::info a : std::meta::template_arguments_of(t))
            if (std::meta::is_type(a) && !reflect_complete(a)) return false;
    return true;
}

consteval bool reflect_is_variant(const std::meta::info type)
{
    const std::meta::info t = reflect_bare(type);
    return std::meta::is_class_type(t) && std::meta::has_template_arguments(t) && std::meta::template_of(t) == ^^std::variant;
}

consteval bool reflect_parameter_supported(const std::meta::info type)
{
    if (reflect_is_opaque_pointer(type) || reflect_is_output_parameter(type) || reflect_is_function_pointer(type)) return true;
    if (reflect_is_variant(type)) {
        if (reflect_mutates(type)) return false;
        for (const std::meta::info a : std::meta::template_arguments_of(reflect_bare(type)))
            if (!reflect_parameter_supported(a)) return false;
        return true;
    }
    if (!reflect_complete(type)) return false;
    if (reflect_is_function(type)) {
        const std::meta::info r = std::meta::return_type_of(std::meta::template_arguments_of(reflect_bare(type))[0]);
        if (std::meta::is_reference_type(r) || std::meta::is_pointer_type(std::meta::dealias(r)) || reflect_is_view(reflect_bare(r))) return false;
    }
    if (reflect_is_view(reflect_bare(type)) && reflect_bare(type) != std::meta::dealias(^^std::string_view) &&
        reflect_bare(type) != std::meta::dealias(^^std::span<const mrb_value>))
        return false;
    if (reflect_get_args_letter(type) == '\0') return false;
    const bool class_pointer = std::meta::is_pointer_type(reflect_bare(type)) &&
                               std::meta::is_class_type(std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(reflect_bare(type)))));
    if (reflect_mutates(type) && !reflect_is_object(type) && !class_pointer && !reflect_is_void_pointer(type)) return false;
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
    if (reflect_is_function_pointer(type) && !std::meta::is_reference_type(type)) return true;
    if (!reflect_complete(type)) return false;
    const std::meta::info bare = reflect_bare(type);
    if (reflect_is_variant(bare)) {
        for (const std::meta::info a : std::meta::template_arguments_of(bare))
            if (!reflect_result_supported(a)) return false;
        return true;
    }
    if (std::meta::is_array_type(bare))
        return std::meta::extent(bare) > 0 && !std::meta::is_array_type(std::meta::remove_extent(bare)) && reflect_result_supported(std::meta::remove_extent(bare));
    if (bare == ^^void || bare == std::meta::dealias(^^mrb_value) || bare == ^^bool || std::meta::is_arithmetic_type(bare) || std::meta::is_enum_type(bare)) return true;
    if (std::meta::is_pointer_type(bare)) {
        const std::meta::info to = std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(bare)));
        return to == ^^char || to == ^^void || (std::meta::is_class_type(to) && std::meta::is_complete_type(to));
    }
    if (!std::meta::is_class_type(bare) || !std::meta::is_complete_type(bare) || std::meta::is_abstract_type(bare) || reflect_is_iterator(bare)) return false;
    if (reflect_is_view(bare)) {
        const std::meta::info element = std::meta::dealias(std::meta::substitute(^^std::ranges::range_value_t, {bare}));
        return std::meta::extract<bool>(std::meta::substitute(^^std::ranges::forward_range, {bare})) && std::meta::is_copy_constructible_type(element) &&
               reflect_result_supported(element);
    }
    if (std::meta::has_template_arguments(bare) && std::meta::template_of(bare) == ^^std::pair) return true;
    return std::meta::is_reference_type(type) || std::meta::is_move_constructible_type(bare);
}

consteval bool reflect_element_compared(const std::meta::info function)
{
    const std::meta::info owner = std::meta::parent_of(function);
    if (!std::meta::is_type(owner) || !std::meta::has_template_arguments(owner)) return true;
    for (const std::meta::info p : std::meta::parameters_of(function))
        for (const std::meta::info a : std::meta::template_arguments_of(owner))
            if (std::meta::is_type(a) && std::meta::is_class_type(std::meta::dealias(a)) &&
                reflect_bare(std::meta::type_of(p)) == std::meta::dealias(a) &&
                !std::meta::extract<bool>(std::meta::substitute(^^std::equality_comparable, {a})))
                return false;
    return true;
}

consteval bool reflect_is_coroutine(const std::meta::info type)
{
    const std::meta::info t = reflect_bare(type);
    if (!std::meta::is_class_type(t) || !std::meta::is_complete_type(t)) return false;
    for (const std::meta::info m : std::meta::members_of(t, std::meta::access_context::unchecked()))
        if (std::meta::is_type(m) && std::meta::has_identifier(m) && std::meta::identifier_of(m) == "promise_type") return true;
    return false;
}

consteval bool reflect_coroutine_parameters_supported(const std::meta::info function)
{
    for (const std::meta::info p : std::meta::parameters_of(function)) {
        const std::meta::info t = std::meta::type_of(p);
        const std::meta::info bare = reflect_bare(t);
        if (std::meta::is_reference_type(t) || std::meta::is_pointer_type(std::meta::dealias(t)) || reflect_is_view(bare) ||
            bare == std::meta::dealias(^^std::string_view) || bare == std::meta::dealias(^^std::span<const mrb_value>))
            return false;
    }
    return true;
}

consteval bool reflect_call_supported(const std::meta::info function)
{
    if (std::meta::is_deleted(function) || !reflect_element_compared(function)) return false;
    if (std::meta::is_template(function) || std::meta::is_function_template(function)) return false;
    if (std::meta::is_vararg_function(function)) return false;
    if (std::meta::is_rvalue_reference_qualified(function) || std::meta::is_volatile(function)) return false;
    for (const std::meta::info p : std::meta::parameters_of(function))
        if (!reflect_parameter_supported(std::meta::type_of(p))) return false;
    if (std::ranges::count_if(std::meta::parameters_of(function), [](const std::meta::info p) { return reflect_is_output_parameter(std::meta::type_of(p)); }) > 1)
        return false;
    if (std::meta::is_constructor(function)) return true;
    if (reflect_is_coroutine(std::meta::return_type_of(function)) && !reflect_coroutine_parameters_supported(function)) return false;
    return reflect_is_opaque_pointer(std::meta::return_type_of(function)) || reflect_result_supported(std::meta::return_type_of(function));
}

consteval bool reflect_reserved(const std::meta::info type)
{
    const std::meta::info t = std::meta::dealias(type);
    const std::string_view name = std::meta::has_identifier(t)          ? std::meta::identifier_of(t)
                                  : std::meta::has_template_arguments(t) ? std::meta::identifier_of(std::meta::template_of(t))
                                                                         : std::string_view{};
    return name.contains("__") || (name.size() > 1 && name[0] == '_' && name[1] >= 'A' && name[1] <= 'Z');
}

consteval std::vector<std::meta::info> reflect_direct_bases(const std::meta::info type)
{
    std::vector<std::meta::info> bases;
    if (!std::meta::is_class_type(std::meta::dealias(type)) || !std::meta::is_complete_type(std::meta::dealias(type))) return bases;
    for (const std::meta::info b : std::meta::bases_of(std::meta::dealias(type), std::meta::access_context::current()))
        if (std::meta::is_public(b) && !reflect_reserved(std::meta::type_of(b))) bases.push_back(std::meta::dealias(std::meta::type_of(b)));
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
        if (std::meta::is_function(m) && !std::meta::is_static_member(m) && reflect_skip(m) == 0 &&
            !std::meta::is_special_member_function(m) &&
            (std::meta::is_operator_function(m) ? !reflect_operator_method(m).empty() : std::meta::has_identifier(m)) &&
            (!std::meta::has_identifier(m) || std::meta::identifier_of(m) != "swap" || std::meta::extract<bool>(std::meta::substitute(^^std::swappable, {std::meta::dealias(Type)}))) &&
            reflect_call_supported(m))
            methods.push_back(m);
    return std::define_static_array(methods);
}

template <std::meta::info Type>
consteval auto reflect_constructors_computed()
{
    std::vector<std::meta::info> constructors;
    if (!std::meta::is_class_type(std::meta::dealias(Type))) return std::define_static_array(constructors);
    for (const std::meta::info m : std::meta::members_of(std::meta::dealias(Type), std::meta::access_context::current()))
        if (std::meta::is_constructor(m) && !std::meta::is_deleted(m) && !std::meta::is_move_constructor(m) && reflect_call_supported(m))
            constructors.push_back(m);
    for (const std::meta::info base : reflect_direct_bases(Type))
        for (const std::meta::info c : std::meta::members_of(base, std::meta::access_context::current())) {
            if (!std::meta::is_constructor(c) || std::meta::is_deleted(c) || std::meta::is_copy_constructor(c) || std::meta::is_move_constructor(c) ||
                std::meta::is_template(c) || !reflect_call_supported(c))
                continue;
            std::vector<std::meta::info> types;
            for (const std::meta::info p : std::meta::parameters_of(c)) types.push_back(std::meta::type_of(p));
            const bool own = std::ranges::any_of(constructors, [&](const std::meta::info o) {
                const std::vector<std::meta::info> op = std::meta::parameters_of(o);
                return op.size() == types.size() && std::ranges::equal(op, types, {}, [](const std::meta::info p) { return std::meta::type_of(p); });
            });
            if (!own && std::meta::is_constructible_type(std::meta::dealias(Type), types)) constructors.push_back(c);
        }
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

consteval bool reflect_conversion_supported(const std::meta::info function)
{
    const std::meta::info target = reflect_bare(std::meta::return_type_of(function));
    const bool text = target == std::meta::dealias(^^std::string) || target == std::meta::dealias(^^std::string_view) || target == ^^const char *;
    return (std::meta::is_integral_type(target) && target != ^^bool) || std::meta::is_floating_point_type(target) || text;
}

template <std::meta::info Type>
consteval auto reflect_conversion_functions_computed()
{
    std::vector<std::meta::info> functions;
    for (const std::meta::info m : std::meta::members_of(std::meta::dealias(Type), std::meta::access_context::current()))
        if (std::meta::is_conversion_function(m) && !std::meta::is_template(m) && !std::meta::is_deleted(m) && !std::meta::is_volatile(m) &&
            !std::meta::is_rvalue_reference_qualified(m) && reflect_conversion_supported(m))
            functions.push_back(m);
    return std::define_static_array(functions);
}

template <std::meta::info Type>
inline constexpr auto reflect_conversion_functions_cached = reflect_conversion_functions_computed<Type>();

template <std::meta::info Type>
consteval auto reflect_conversion_functions()
{
    return reflect_conversion_functions_cached<Type>;
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
    if (!std::meta::is_class_type(std::meta::dealias(Type))) return std::define_static_array(fields);
    for (const std::meta::info m : std::meta::nonstatic_data_members_of(std::meta::dealias(Type), std::meta::access_context::current()))
        if (std::meta::has_identifier(m) && reflect_result_supported(std::meta::type_of(m)))
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

consteval bool reflect_in_namespace_std(const std::meta::info type)
{
    for (std::meta::info scope = std::meta::parent_of(type); std::meta::has_parent(scope); scope = std::meta::parent_of(scope))
        if (scope == ^^std) return true;
    return false;
}

consteval std::vector<std::meta::info> reflect_signature_types(const std::span<const std::meta::info> scopes)
{
    std::vector<std::meta::info> types(scopes.begin(), scopes.end());
    const auto note = [&](const std::meta::info type) {
        std::meta::info t = reflect_bare(type);
        while (std::meta::is_pointer_type(t)) t = reflect_bare(std::meta::remove_pointer(t));
        if (!(std::meta::is_class_type(t) || std::meta::is_enum_type(t)) || !std::meta::has_identifier(t) || reflect_reserved(t) || reflect_in_namespace_std(t) ||
            t == std::meta::dealias(^^mrb_value) || !std::meta::is_namespace(std::meta::parent_of(t)))
            return;
        if (std::ranges::find(types, t) == types.end()) types.push_back(t);
    };
    for (const std::meta::info scope : scopes) {
        if (!std::meta::is_namespace(scope) && !(std::meta::is_class_type(scope) && std::meta::is_complete_type(scope))) continue;
        for (const std::meta::info m : std::meta::members_of(scope, std::meta::access_context::current())) {
            if (!std::meta::is_function(m) || std::meta::is_template(m) || !reflect_call_supported(m)) continue;
            if (!std::meta::is_constructor(m)) note(std::meta::return_type_of(m));
            for (const std::meta::info p : std::meta::parameters_of(m)) note(std::meta::type_of(p));
        }
    }
    return types;
}

template <auto Scopes>
consteval auto reflect_with_signature_types()
{
    constexpr auto list = std::define_static_array(reflect_signature_types(Scopes));
    std::array<std::meta::info, list.size()> out{};
    std::ranges::copy(list, out.begin());
    return out;
}

consteval bool reflect_variable_supported(const std::meta::info variable)
{
    const std::meta::info type = reflect_bare(std::meta::type_of(variable));
    if (std::meta::is_class_type(type)) return std::meta::is_complete_type(type) && !reflect_is_iterator(type);
    return reflect_result_supported(std::meta::type_of(variable));
}

consteval std::vector<std::meta::info> reflect_variables(const std::meta::info scope)
{
    std::vector<std::meta::info> variables;
    for (const std::meta::info m : std::meta::members_of(scope, std::meta::access_context::current()))
        if (std::meta::is_variable(m) && std::meta::has_identifier(m) && !std::meta::identifier_of(m).starts_with("_") && reflect_variable_supported(m))
            variables.push_back(m);
    return variables;
}

consteval std::vector<std::meta::info> reflect_given_parameters(const std::meta::info function)
{
    const std::vector<std::meta::info> parameters = std::meta::parameters_of(function);
    return {parameters.begin() + static_cast<std::ptrdiff_t>(reflect_skip(function)), parameters.end()};
}

consteval std::meta::info reflect_operand_class(const std::meta::info function)
{
    return reflect_bare(std::meta::type_of(std::meta::parameters_of(function)[0]));
}

consteval bool reflect_operand_is_lvalue(const std::meta::info function)
{
    return !std::meta::is_rvalue_reference_type(std::meta::type_of(std::meta::parameters_of(function)[0]));
}

consteval std::vector<std::meta::info> reflect_free_operators(const std::meta::info scope, const std::span<const std::meta::info> classes)
{
    std::vector<std::meta::info> operators;
    for (const std::meta::info m : std::meta::members_of(scope, std::meta::access_context::current()))
        if (std::meta::is_function(m) && reflect_skip(m) == 1 && !reflect_operator_method(m).empty() &&
            std::meta::is_class_type(reflect_operand_class(m)) && reflect_operand_is_lvalue(m) && reflect_call_supported(m))
            operators.push_back(m);
    for (const std::meta::info c : classes)
        if (std::meta::is_function(c) && std::meta::parent_of(c) == scope && reflect_skip(c) == 1 && !reflect_operator_method(c).empty() &&
            std::meta::is_class_type(reflect_operand_class(c)) && reflect_operand_is_lvalue(c) && reflect_call_supported(c) && std::ranges::find(operators, c) == operators.end())
            operators.push_back(c);
    return operators;
}


consteval std::vector<std::meta::info> reflect_operand_classes(const std::span<const std::meta::info> operators)
{
    std::vector<std::meta::info> classes;
    for (const std::meta::info f : operators)
        if (std::ranges::find(classes, reflect_operand_class(f)) == classes.end()) classes.push_back(reflect_operand_class(f));
    return classes;
}

consteval std::meta::info reflect_enclosing_namespace(const std::meta::info scope)
{
    std::meta::info n = scope;
    while (!std::meta::is_namespace(n)) n = std::meta::parent_of(n);
    return n;
}

consteval std::vector<std::meta::info> reflect_template_candidates(const std::meta::info scope)
{
    std::vector<std::meta::info> candidates;
    for (const std::meta::info m : std::meta::members_of(reflect_enclosing_namespace(scope), std::meta::access_context::current()))
        if (std::meta::is_type(m) && !std::meta::is_type_alias(m) && (std::meta::is_class_type(m) || std::meta::is_enum_type(m)) &&
            std::meta::has_identifier(m) && std::meta::is_complete_type(m) && !reflect_reserved(m))
            candidates.push_back(m);
    return candidates;
}

consteval std::vector<std::meta::info> reflect_instances_computed(const std::meta::info scope, const std::span<const std::meta::info> classes, const bool templates)
{
    std::vector<std::meta::info> instances;
    for (const std::meta::info c : classes)
        if (std::meta::parent_of(c) == scope && ((std::meta::is_function(c) && reflect_call_supported(c)) || (std::meta::is_variable(c) && reflect_variable_supported(c))))
            instances.push_back(c);
    if (!templates || !(std::meta::is_namespace(scope) || std::meta::is_class_type(scope))) return instances;
    const std::vector<std::meta::info> candidates = reflect_template_candidates(scope);
    for (const std::meta::info m : std::meta::members_of(scope, std::meta::access_context::current())) {
        if (!std::meta::is_function_template(m) || !std::meta::has_identifier(m) || std::meta::identifier_of(m).starts_with("_")) continue;
        for (const std::meta::info t : candidates) {
            if (!std::meta::can_substitute(m, {t})) continue;
            const std::meta::info instance = std::meta::substitute(m, {t});
            if (reflect_call_supported(instance) && std::ranges::find(instances, instance) == instances.end()) instances.push_back(instance);
        }
    }
    return instances;
}

template <std::meta::info Scope>
inline constexpr auto reflect_free_operators_cached = std::define_static_array(reflect_free_operators(Scope, {}));

template <std::meta::info Type>
consteval std::vector<std::meta::info> reflect_operators_for()
{
    std::vector<std::meta::info> found;
    for (const std::meta::info f : reflect_free_operators_cached<reflect_enclosing_namespace(std::meta::parent_of(std::meta::dealias(Type)))>)
        if (reflect_operand_class(f) == std::meta::dealias(Type)) found.push_back(f);
    return found;
}

template <std::meta::info Scope, auto Classes, bool Templates>
consteval auto reflect_instances()
{
    constexpr auto list = std::define_static_array(reflect_instances_computed(Scope, Classes, Templates));
    std::array<std::meta::info, list.size()> out{};
    std::ranges::copy(list, out.begin());
    return out;
}

consteval std::vector<std::meta::info> reflect_scopes(const std::span<const std::meta::info> classes)
{
    std::vector<std::meta::info> scopes;
    for (const std::meta::info c : classes) {
        const std::meta::info scope = std::meta::is_function(c) || std::meta::is_variable(c) ? std::meta::parent_of(c) : c;
        if (std::ranges::find(scopes, scope) == scopes.end()) scopes.push_back(scope);
    }
    return scopes;
}

consteval std::vector<std::meta::info> reflect_merged(const std::span<const std::meta::info> functions, const std::span<const std::meta::info> instances, const bool statics)
{
    std::vector<std::meta::info> merged(functions.begin(), functions.end());
    for (const std::meta::info i : instances)
        if (std::meta::is_function(i) && std::meta::is_static_member(i) == statics) merged.push_back(i);
    return merged;
}

consteval std::vector<std::meta::info> reflect_overloads_in(const std::span<const std::meta::info> functions, const std::meta::info member)
{
    std::vector<std::meta::info> same;
    for (const std::meta::info m : functions)
        if (reflect_identifier(m) == reflect_identifier(member)) same.push_back(m);
    return same;
}

consteval bool reflect_first_of_its_name(const std::span<const std::meta::info> functions, const std::meta::info member)
{
    for (const std::meta::info m : functions) {
        if (m == member) return true;
        if (reflect_identifier(m) == reflect_identifier(member)) return false;
    }
    return true;
}

consteval std::vector<std::meta::info> reflect_nested_types(const std::meta::info type)
{
    std::vector<std::meta::info> nested;
    for (const std::meta::info m : std::meta::members_of(std::meta::dealias(type), std::meta::access_context::current()))
        if (std::meta::is_type(m) && !std::meta::is_type_alias(m) && (std::meta::is_class_type(m) || std::meta::is_enum_type(m)) &&
            std::meta::has_identifier(m) && std::meta::is_complete_type(m) && !reflect_reserved(m))
            nested.push_back(m);
    return nested;
}

template <std::meta::info Type>
consteval std::vector<std::meta::info> reflect_nested_types_used()
{
    std::vector<std::meta::info> used;
    const std::vector<std::meta::info> nested = reflect_nested_types(Type);
    const auto note = [&](const std::meta::info type) {
        std::meta::info t = reflect_bare(type);
        while (std::meta::is_pointer_type(t)) t = std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(t)));
        if (std::ranges::find(used, t) == used.end() && std::ranges::find(nested, t) != nested.end()) used.push_back(t);
    };
    const auto signature = [&](const std::meta::info function) {
        if (!std::meta::is_constructor(function)) note(std::meta::return_type_of(function));
        for (const std::meta::info p : std::meta::parameters_of(function)) note(std::meta::type_of(p));
    };
    for (const std::meta::info f : reflect_members<Type>()) signature(f);
    for (const std::meta::info f : reflect_static_functions<Type>()) signature(f);
    for (const std::meta::info f : reflect_constructors<Type>()) signature(f);
    for (const std::meta::info f : reflect_conversion_functions<Type>()) signature(f);
    for (const std::meta::info f : reflect_fields<Type>()) note(std::meta::type_of(f));
    for (const std::meta::info f : reflect_static_data_members<Type>()) note(std::meta::type_of(f));
    return used;
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
consteval void reflect_names_into(std::vector<std::string_view> &names)
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
consteval void reflect_setter_names_into(std::vector<std::string_view> &names)
{
    template for (constexpr std::meta::info field : reflect_fields<Type>())
        if constexpr (!std::meta::is_const_type(std::meta::type_of(field))) names.push_back(std::meta::identifier_of(field));
    template for (constexpr std::meta::info member : reflect_static_data_members<Type>())
        if constexpr (!std::meta::is_const_type(std::meta::type_of(member))) names.push_back(std::meta::identifier_of(member));
}

template <auto Classes>
consteval std::string reflect_presym_entries()
{
    std::vector<std::string_view> names, setters;
    template for (constexpr std::meta::info type : Classes) {
        if constexpr (std::meta::is_type(type) && std::meta::is_class_type(std::meta::dealias(type)) && std::meta::is_complete_type(std::meta::dealias(type))) {
            reflect_names_into<type>(names);
            reflect_setter_names_into<type>(setters);
        }
    }
    for (const std::string_view fixed : {"to_s", "to_str", "to_a", "to_ary", "to_h", "to_hash", "each", "initialize", "owner", "Enumerable"})
        names.push_back(fixed);
    const auto identifier = [](const std::string_view name) {
        const auto word = [](const char c) { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
        return !name.empty() && !(name[0] >= '0' && name[0] <= '9') && std::ranges::all_of(name, word);
    };
    std::erase_if(names, [&](const std::string_view name) { return !identifier(name); });
    std::erase_if(setters, [&](const std::string_view name) { return !identifier(name); });
    std::string out;
    const auto entry = [&](const std::string_view name, const std::string_view suffix, const std::string_view macro) {
        for (const std::string_view part : {std::string_view("    {\""), name, suffix, std::string_view("\", "), macro, std::string_view("("), name, std::string_view(")},\n")})
            std::ranges::copy(part, std::back_inserter(out));
    };
    for (const std::string_view name : names) entry(name, "", "MRB_SYM");
    for (const std::string_view name : setters) entry(name, "=", "MRB_SYM_E");
    return out;
}

}

#endif
