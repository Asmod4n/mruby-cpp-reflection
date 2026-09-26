#include <mruby.h>
#include <mruby/error.h>
#if defined(__cpp_impl_reflection)
#include <mruby/reflection.hpp>
#endif

#if defined(__cpp_impl_reflection)
extern "C" [[noreturn]] void reflect_undefined()
{
    throw mrb_cpp_reflector::reflect_undefined_call();
}
#endif

extern "C" void mrb_mruby_cpp_reflection_gem_init(mrb_state *) {}

extern "C" void mrb_mruby_cpp_reflection_gem_final(mrb_state *) {}
