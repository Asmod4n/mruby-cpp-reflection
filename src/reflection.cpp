#include <mruby.h>
#include <mruby/error.h>
#if defined(__cpp_impl_reflection)
#include <mruby/reflection.hpp>
#endif

extern "C" thread_local mrb_state *reflect_calling = nullptr;

extern "C" [[noreturn]] void reflect_undefined()
{
    mrb_state *const mrb = reflect_calling;
    mrb_raise(mrb, E_NOTIMP_ERROR, "no linked library defines this function");
}

extern "C" void mrb_mruby_cpp_reflection_gem_init(mrb_state *) {}

extern "C" void mrb_mruby_cpp_reflection_gem_final(mrb_state *) {}
