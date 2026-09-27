/*
 * A function whose last parameter is ... takes the lists of trailing
 * types that mrbgem.rake declares with spec.reflect_varargs, and nothing
 * else: each list is one call instance, and Ruby chooses among them as
 * among overloads. A function without a declaration raises
 * NotImplementedError, since only the declaration says what va_arg
 * reads ([cstdarg.syn], ISO C 7.16.1.1). varargs.rb drives the
 * functions below.
 */
#include <mruby.h>
#if defined(__cpp_impl_reflection)
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

#include <mruby/reflect_varargs.h>

constexpr auto varargs_classes = mrb_cpp_reflector::reflect<^^varargs>();

void varargs_gem_test(mrb_state *const mrb)
{
    mrb_cpp_reflector::reflect_define<varargs_classes>(mrb);
}
#else
void varargs_gem_test(mrb_state *) {}
#endif
