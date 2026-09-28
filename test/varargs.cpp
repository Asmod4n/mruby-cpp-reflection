/*
 * A function whose last parameter is ... takes the lists of trailing
 * types that varargs.hpp declares with mruby::cpp_reflection::varargs, and nothing
 * else: each list is one call instance, and Ruby chooses among them as
 * among overloads. A function without a declaration raises
 * NotImplementedError, since only the declaration says what va_arg
 * reads ([cstdarg.syn], ISO C 7.16.1.1). varargs.rb drives the
 * functions below.
 */
#include <mruby.h>
#if defined(__cpp_impl_reflection)
#include "varargs.hpp"
#include <mruby/reflection.hpp>

constexpr auto varargs_classes = mrb_cpp_reflector::reflect<^^varargs>();

void varargs_gem_test(mrb_state *const mrb)
{
    mrb_cpp_reflector::reflect_define<varargs_classes>(mrb);
}
#else
void varargs_gem_test(mrb_state *) {}
#endif
