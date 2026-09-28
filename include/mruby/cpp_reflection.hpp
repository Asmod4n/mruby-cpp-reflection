#pragma once
#if defined(__cpp_impl_reflection)

#include <cstddef>
#include <cstdint>
#include <mruby/common.h>
struct mrb_state;
typedef struct mrb_state mrb_state;
#include <mruby/value.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <meta>
#include <string_view>

namespace mrb_cpp_reflector {

enum class reflect_answer_test : unsigned char { none, success, error, negative };

enum class reflect_word : unsigned char { takes_ownership, ends_lifetime, retains, errors, stack_reserve, threadsafe, allocator, deallocator, shared_ownership };

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

struct reflect_shared_ownership {
    std::meta::info increment{};
    std::meta::info decrement{};
};

struct reflect_object_lifetime_word {
    reflect_word word;
    const char *function = std::define_static_string("");
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
    if (!std::meta::is_function(function) || !std::meta::has_identifier(function)) throw "a lifetime word names a function or a class by its reflection";
    return std::define_static_string(std::meta::identifier_of(function));
}

}

namespace mruby::cpp_reflection {

inline constexpr mrb_cpp_reflector::reflect_answer_test negative = mrb_cpp_reflector::reflect_answer_test::negative;

template <std::meta::info Class>
inline constexpr std::array<mrb_cpp_reflector::reflect_object_lifetime_word, 0> object_lifetime{};

template <std::meta::info Function>
inline constexpr std::array<std::meta::info, 0> varargs{};

consteval mrb_cpp_reflector::reflect_object_lifetime_word takes_ownership(const std::meta::info function, const mrb_cpp_reflector::reflect_ownership given)
{
    if (given.of.number < 0 && *given.of.identifier == '\0' && given.by.number < 0 && *given.by.identifier == '\0') throw "takes_ownership names the of: or the by: parameter";
    return {.word = mrb_cpp_reflector::reflect_word::takes_ownership, .function = mrb_cpp_reflector::reflect_lifetime_function_name(function), .of = given.of, .by = given.by};
}

consteval mrb_cpp_reflector::reflect_object_lifetime_word ends_lifetime(const std::meta::info function, const mrb_cpp_reflector::reflect_parameter position = {})
{
    return {.word = mrb_cpp_reflector::reflect_word::ends_lifetime, .function = mrb_cpp_reflector::reflect_lifetime_function_name(function), .position = position};
}

consteval mrb_cpp_reflector::reflect_object_lifetime_word retains(const std::meta::info function, const mrb_cpp_reflector::reflect_parameter position)
{
    return {.word = mrb_cpp_reflector::reflect_word::retains, .function = mrb_cpp_reflector::reflect_lifetime_function_name(function), .position = position};
}

consteval mrb_cpp_reflector::reflect_object_lifetime_word errors(const std::meta::info function, const mrb_cpp_reflector::reflect_error_answers given)
{
    using mrb_cpp_reflector::reflect_answer_test;
    if ((given.success.test == reflect_answer_test::none) == (given.error.test == reflect_answer_test::none)) throw "errors names either success: or error:";
    if (given.success.test == reflect_answer_test::negative) throw "errors takes negative as error:";
    const mrb_cpp_reflector::reflect_answer answer = given.success.test != reflect_answer_test::none ? given.success : given.error;
    const reflect_answer_test test = given.success.test != reflect_answer_test::none ? reflect_answer_test::success : answer.test;
    return {.word = mrb_cpp_reflector::reflect_word::errors,
            .function = mrb_cpp_reflector::reflect_lifetime_function_name(function),
            .test = test,
            .expects_nil = answer.expects_nil,
            .expected = answer.expected,
            .sets_errno = given.sets_errno};
}

consteval mrb_cpp_reflector::reflect_object_lifetime_word stack_reserve(const std::meta::info function, const std::size_t bytes)
{
    if (bytes == 0) throw "a stack reserve is a positive number of bytes";
    return {.word = mrb_cpp_reflector::reflect_word::stack_reserve, .function = mrb_cpp_reflector::reflect_lifetime_function_name(function), .stack_reserve = bytes};
}

consteval mrb_cpp_reflector::reflect_object_lifetime_word threadsafe(const std::meta::info function, const bool value)
{
    if (value) throw "threadsafe takes false";
    return {.word = mrb_cpp_reflector::reflect_word::threadsafe, .function = mrb_cpp_reflector::reflect_lifetime_function_name(function)};
}

consteval mrb_cpp_reflector::reflect_object_lifetime_word allocator(const std::meta::info function, const mrb_cpp_reflector::reflect_allocation given = {})
{
    return {.word = mrb_cpp_reflector::reflect_word::allocator, .function = mrb_cpp_reflector::reflect_lifetime_function_name(function), .output_parameter = given.output_parameter};
}

consteval mrb_cpp_reflector::reflect_object_lifetime_word deallocator(const std::meta::info function, const mrb_cpp_reflector::reflect_deallocation given = {})
{
    return {.word = mrb_cpp_reflector::reflect_word::deallocator,
            .function = mrb_cpp_reflector::reflect_lifetime_function_name(function),
            .results_of = given.results_of == std::meta::info{} ? std::define_static_string("") : mrb_cpp_reflector::reflect_lifetime_function_name(given.results_of)};
}

consteval mrb_cpp_reflector::reflect_object_lifetime_word shared_ownership(const mrb_cpp_reflector::reflect_shared_ownership given)
{
    return {.word = mrb_cpp_reflector::reflect_word::shared_ownership,
            .increment = mrb_cpp_reflector::reflect_lifetime_function_name(given.increment),
            .decrement = mrb_cpp_reflector::reflect_lifetime_function_name(given.decrement)};
}

}

#endif
