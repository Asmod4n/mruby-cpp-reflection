/*
 * No declaration says who frees the handle that unknown_make answers, so
 * this file must not compile. mrbgem.rake compiles it in the test build
 * and checks the exit status and the message.
 */
#include <mruby.h>
#include "../lifetime_allocator_library.hpp"
#include <mruby/reflection.hpp>

void missing_lifetime_defines(mrb_state *const mrb)
{
    mrb_cpp_reflector::reflect_define<mrb_cpp_reflector::reflect<^^c_library_undeclared>()>(mrb);
}
