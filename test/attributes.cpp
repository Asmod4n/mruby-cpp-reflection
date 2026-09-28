/*
 * The attributes of a library's functions reach the gem as
 * specializations of reflect_attributes_of. A parameter that the nonnull
 * attribute names refuses nil, since the function may read through it
 * (GCC manual, Common Function Attributes, nonnull). The specialization
 * below is the form for __attribute__((nonnull(1))); the indexes count
 * from 1, as GCC writes them. attributes.rb drives the functions.
 */
#include <mruby.h>
#if defined(__cpp_impl_reflection)
#include <mruby/reflection.hpp>

namespace attributes {
struct Box {
    int n = 1;
};
inline int read(const Box *const box) { return box == nullptr ? -1 : box->n; }
inline int read_or_none(const Box *const box) { return box == nullptr ? -1 : box->n; }
}

namespace mrb_cpp_reflector {
template <>
inline constexpr reflect_attributes reflect_attributes_of<^^::attributes::read> = {.nonnull = std::define_static_array(std::array<int, 1>{1})};
}

constexpr auto attribute_classes = mrb_cpp_reflector::reflect<^^attributes, ^^attributes::Box>();

void attributes_gem_test(mrb_state *const mrb)
{
    mrb_cpp_reflector::reflect_define<attribute_classes>(mrb);
}
#else
void attributes_gem_test(mrb_state *) {}
#endif
