/*
 * A pointer to a function reaches Ruby as a FunctionPointer: an
 * identifier with no methods, which Ruby can only hand back to C++. Each
 * signature has a data type of its own, so a parameter takes only a
 * pointer to a function of exactly its type, and C++ never calls a
 * function through a pointer of another type ([expr.call]).
 * function_pointers.rb drives the functions below.
 */
#include <mruby.h>
#if defined(__cpp_impl_reflection)
#include <mruby/cpp_reflection.hpp>

namespace function_pointers {
inline int twice(const int n) { return n * 2; }
inline int negated(const int n) { return -n; }
inline long widened(const long n) { return n; }
inline int (*pick(const bool negate))(int) { return negate ? negated : twice; }
inline long (*wide())(long) { return widened; }
inline int (*none())(int) { return nullptr; }
inline int apply(int (*const f)(int), const int n) { return f(n); }
}

constexpr auto function_pointer_classes = mruby::cpp_reflection::reflect<^^function_pointers>();

void function_pointers_gem_init(mrb_state *const mrb)
{
    mruby::cpp_reflection::reflect_define<function_pointer_classes>(mrb);
}
#else
void function_pointers_gem_init(mrb_state *) {}
#endif
