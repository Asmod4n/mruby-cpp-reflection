#pragma once
/*
 * Functions whose last parameter is ..., and the lists of trailing types
 * that the tests declare for two of them. undeclared has no list.
 */
#include <mruby/cpp_reflection.hpp>
#include <cstdarg>
#include <cstring>

namespace varargs {
inline int sum_ints(const int count, ...)
{
    va_list arguments;
    va_start(arguments, count);
    int sum = 0;
    for (int i = 0; i < count; i++) sum += va_arg(arguments, int);
    va_end(arguments);
    return sum;
}
inline double describe(const int n, ...)
{
    va_list arguments;
    va_start(arguments, n);
    const int whole = va_arg(arguments, int);
    const double part = va_arg(arguments, double);
    const char *const text = va_arg(arguments, const char *);
    va_end(arguments);
    return n + whole + part + static_cast<double>(std::strlen(text));
}
inline int undeclared(const int n, ...) { return n; }
}

#if defined(__cpp_impl_reflection)
#include <tuple>
template <>
inline constexpr auto mruby::cpp_reflection::varargs<^^varargs::sum_ints> = std::array{^^std::tuple<int>, ^^std::tuple<int, int>, ^^std::tuple<int, int, int>};
template <>
inline constexpr auto mruby::cpp_reflection::varargs<^^varargs::describe> = std::array{^^std::tuple<int, double, const char *>};
#endif
