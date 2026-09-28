/*
 * reflect_with_signature_types adds the classes that the signatures of
 * the listed scopes name. A class has a destructor, and a destructor has
 * no return type ([class.dtor]), so the list of a class skips it.
 * signature_types.rb checks that a class with an implicit destructor is
 * reflected.
 */
#include <mruby.h>
#if defined(__cpp_impl_reflection)
#include <mruby/cpp_reflection.hpp>

namespace signature_types {
struct point {
    int x;
};
}

constexpr auto signature_type_classes = mruby::cpp_reflection::reflect_with_signature_types<mruby::cpp_reflection::reflect<^^signature_types::point>()>();

void signature_types_gem_init(mrb_state *const mrb)
{
    mruby::cpp_reflection::reflect_define<signature_type_classes>(mrb);
}
#else
void signature_types_gem_init(mrb_state *) {}
#endif
