#include <mruby.h>
#if defined(__cpp_impl_reflection)
#include <mruby/reflection.hpp>
#endif

extern "C" void mrb_mruby_cpp_reflection_gem_init(mrb_state *) {}

extern "C" void mrb_mruby_cpp_reflection_gem_final(mrb_state *) {}
