#pragma once
#if defined(__cpp_impl_reflection)

#include <cstddef>
#include <cstdint>
#include <mruby/common.h>
struct mrb_state;
typedef struct mrb_state mrb_state;
#include <mruby/value.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <meta>
#include <optional>
#include <source_location>
#include <span>
#include <string_view>
#include <vector>

namespace mruby::cpp_reflection {

enum class reflect_answer_test : unsigned char { none, success, error, negative };

enum class reflect_word : unsigned char { takes_ownership, ends_lifetime, retains, errors, stack_reserve, threadsafe, allocator, deallocator, shared_ownership, borrowed };

struct reflect_parameter {
    consteval reflect_parameter() {}
    consteval reflect_parameter(const int given) : number(given) {}
    consteval reflect_parameter(const char *const given) : identifier(std::define_static_string(std::string_view(given))) {}
    int number = -1;
    const char *identifier = std::define_static_string("");
};

struct reflect_answer {
    consteval reflect_answer() {}
    consteval reflect_answer(const std::integral auto given) : test(reflect_answer_test::error), expected(given) {}
    consteval reflect_answer(std::nullptr_t) : test(reflect_answer_test::error), expects_nil(true) {}
    consteval reflect_answer(const reflect_answer_test given) : test(given) {}
    reflect_answer_test test = reflect_answer_test::none;
    bool expects_nil = false;
    mrb_int expected = 0;
};

struct reflect_ownership {
    reflect_parameter of{};
    reflect_parameter by{};
};

struct reflect_error_answers {
    reflect_answer success{};
    reflect_answer error{};
    bool sets_errno = false;
};

struct reflect_allocation {
    reflect_parameter output_parameter{};
};

struct reflect_deallocation {
    std::meta::info results_of{};
};

struct reflect_borrowing {
    std::meta::info owner{};
};

struct reflect_shared_ownership {
    std::meta::info increment{};
    std::meta::info decrement{};
};

struct reflect_object_lifetime_word {
    reflect_word word;
    std::meta::info function{};
    std::meta::info owner{};
    reflect_parameter of{};
    reflect_parameter by{};
    reflect_parameter position{};
    reflect_parameter output_parameter{};
    const char *results_of = std::define_static_string("");
    const char *increment = std::define_static_string("");
    const char *decrement = std::define_static_string("");
    reflect_answer_test test = reflect_answer_test::none;
    bool expects_nil = false;
    mrb_int expected = 0;
    bool sets_errno = false;
    std::size_t stack_reserve = 0;
};

consteval const char *reflect_lifetime_function_name(const std::meta::info function)
{
    if (std::meta::is_type(function) && std::meta::is_class_type(function)) return std::define_static_string("initialize");
    if (std::meta::is_nonstatic_data_member(function) && std::meta::has_identifier(function)) return std::define_static_string(std::meta::identifier_of(function));
    if (!std::meta::is_function(function) || !std::meta::has_identifier(function)) throw "a lifetime word names a function, a data member or a class by its reflection";
    return std::define_static_string(std::meta::identifier_of(function));
}

}

namespace mruby::cpp_reflection {

inline constexpr mruby::cpp_reflection::reflect_answer_test negative = mruby::cpp_reflection::reflect_answer_test::negative;

template <std::meta::info Class>
inline constexpr std::array<mruby::cpp_reflection::reflect_object_lifetime_word, 0> object_lifetime{};

template <std::meta::info Function>
inline constexpr std::array<std::meta::info, 0> varargs{};

namespace macros {
}

struct parameter_extent {
    unsigned line;
    unsigned column;
    unsigned position;
    std::size_t extent;
};

struct format_attribute {
    unsigned line;
    unsigned column;
    const char *archetype;
    unsigned string_index;
    unsigned first_to_check;
};

struct callback_destination {
    unsigned line;
    unsigned column;
    unsigned position;
    bool appends;
    int holder;
    const char *field;
};

template <const char *File>
inline constexpr std::span<const mruby::cpp_reflection::callback_destination> callback_destinations{};

template <const char *File>
inline constexpr std::span<const mruby::cpp_reflection::parameter_extent> parameter_extents{};

template <const char *File>
inline constexpr std::span<const mruby::cpp_reflection::format_attribute> format_attributes{};

template <class Row>
consteval std::span<const Row> reflect_facts_of_file(const std::meta::info facts, const std::source_location where)
{
    return std::meta::extract<const std::span<const Row> &>(
        std::meta::substitute(facts, {std::meta::reflect_constant(std::define_static_string(std::string_view(where.file_name())))}));
}

consteval std::size_t reflect_parameter_extent(const std::meta::info function, const unsigned position)
{
    const std::source_location where = std::meta::source_location_of(function);
    for (const mruby::cpp_reflection::parameter_extent &row : mruby::cpp_reflection::reflect_facts_of_file<mruby::cpp_reflection::parameter_extent>(^^mruby::cpp_reflection::parameter_extents, where))
        if (row.line == where.line() && row.column == where.column() && row.position == position) return row.extent;
    return 0;
}

consteval std::size_t reflect_parameter_extent(const std::meta::info parameter)
{
    const std::meta::info function = std::meta::parent_of(parameter);
    const std::vector<std::meta::info> parameters = std::meta::parameters_of(function);
    return mruby::cpp_reflection::reflect_parameter_extent(function, static_cast<unsigned>(std::ranges::distance(parameters.begin(), std::ranges::find(parameters, parameter))));
}

consteval std::optional<mruby::cpp_reflection::callback_destination> reflect_callback_destination(const std::meta::info parameter)
{
    const std::meta::info function = std::meta::parent_of(parameter);
    const std::vector<std::meta::info> parameters = std::meta::parameters_of(function);
    const auto position = static_cast<unsigned>(std::ranges::distance(parameters.begin(), std::ranges::find(parameters, parameter)));
    const std::source_location where = std::meta::source_location_of(function);
    for (const mruby::cpp_reflection::callback_destination &row : mruby::cpp_reflection::reflect_facts_of_file<mruby::cpp_reflection::callback_destination>(^^mruby::cpp_reflection::callback_destinations, where))
        if (row.line == where.line() && row.column == where.column() && row.position == position) return row;
    return std::nullopt;
}

consteval std::optional<mruby::cpp_reflection::format_attribute> reflect_format_attribute(const std::meta::info function)
{
    const std::source_location where = std::meta::source_location_of(function);
    for (const mruby::cpp_reflection::format_attribute &row : mruby::cpp_reflection::reflect_facts_of_file<mruby::cpp_reflection::format_attribute>(^^mruby::cpp_reflection::format_attributes, where))
        if (row.line == where.line() && row.column == where.column()) return row;
    return std::nullopt;
}

consteval mruby::cpp_reflection::reflect_object_lifetime_word takes_ownership(const std::meta::info function, const mruby::cpp_reflection::reflect_ownership given)
{
    if (given.of.number < 0 && *given.of.identifier == '\0' && given.by.number < 0 && *given.by.identifier == '\0') throw "takes_ownership names the of: or the by: parameter";
    return {.word = mruby::cpp_reflection::reflect_word::takes_ownership, .function = function, .of = given.of, .by = given.by};
}

consteval mruby::cpp_reflection::reflect_object_lifetime_word ends_lifetime(const std::meta::info function, const mruby::cpp_reflection::reflect_parameter position = {})
{
    return {.word = mruby::cpp_reflection::reflect_word::ends_lifetime, .function = function, .position = position};
}

consteval mruby::cpp_reflection::reflect_object_lifetime_word retains(const std::meta::info function, const mruby::cpp_reflection::reflect_parameter position)
{
    return {.word = mruby::cpp_reflection::reflect_word::retains, .function = function, .position = position};
}

consteval mruby::cpp_reflection::reflect_object_lifetime_word errors(const std::meta::info function, const mruby::cpp_reflection::reflect_error_answers given)
{
    using mruby::cpp_reflection::reflect_answer_test;
    if ((given.success.test == reflect_answer_test::none) == (given.error.test == reflect_answer_test::none)) throw "errors names either success: or error:";
    if (given.success.test == reflect_answer_test::negative) throw "errors takes negative as error:";
    const mruby::cpp_reflection::reflect_answer answer = given.success.test != reflect_answer_test::none ? given.success : given.error;
    const reflect_answer_test test = given.success.test != reflect_answer_test::none ? reflect_answer_test::success : answer.test;
    return {.word = mruby::cpp_reflection::reflect_word::errors,
            .function = function,
            .test = test,
            .expects_nil = answer.expects_nil,
            .expected = answer.expected,
            .sets_errno = given.sets_errno};
}

consteval mruby::cpp_reflection::reflect_object_lifetime_word stack_reserve(const std::meta::info function, const std::size_t bytes)
{
    if (bytes == 0) throw "a stack reserve is a positive number of bytes";
    return {.word = mruby::cpp_reflection::reflect_word::stack_reserve, .function = function, .stack_reserve = bytes};
}

consteval mruby::cpp_reflection::reflect_object_lifetime_word threadsafe(const std::meta::info function, const bool value)
{
    if (value) throw "threadsafe takes false";
    return {.word = mruby::cpp_reflection::reflect_word::threadsafe, .function = function};
}

consteval mruby::cpp_reflection::reflect_object_lifetime_word allocator(const std::meta::info function, const mruby::cpp_reflection::reflect_allocation given = {})
{
    return {.word = mruby::cpp_reflection::reflect_word::allocator, .function = function, .output_parameter = given.output_parameter};
}

consteval mruby::cpp_reflection::reflect_object_lifetime_word deallocator(const std::meta::info function, const mruby::cpp_reflection::reflect_deallocation given = {})
{
    return {.word = mruby::cpp_reflection::reflect_word::deallocator,
            .function = function,
            .results_of = given.results_of == std::meta::info{} ? std::define_static_string("") : mruby::cpp_reflection::reflect_lifetime_function_name(given.results_of)};
}

consteval mruby::cpp_reflection::reflect_object_lifetime_word borrowed(const std::meta::info function, const mruby::cpp_reflection::reflect_borrowing given = {})
{
    return {.word = mruby::cpp_reflection::reflect_word::borrowed, .function = function, .owner = given.owner};
}

consteval mruby::cpp_reflection::reflect_object_lifetime_word shared_ownership(const mruby::cpp_reflection::reflect_shared_ownership given)
{
    return {.word = mruby::cpp_reflection::reflect_word::shared_ownership,
            .increment = mruby::cpp_reflection::reflect_lifetime_function_name(given.increment),
            .decrement = mruby::cpp_reflection::reflect_lifetime_function_name(given.decrement)};
}

}

#endif
