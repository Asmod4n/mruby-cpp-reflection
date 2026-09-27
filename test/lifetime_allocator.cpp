/*
 * A C library hands out pointers to types it never defines in its
 * header. The gem cannot know who frees such a handle, so a function
 * that makes one raises NotImplementedError until the class of the
 * handle declares its allocator and its deallocator. lifetime_allocator.rb
 * drives c_library from Ruby.
 */
#include <mruby.h>
#if defined(__cpp_impl_reflection)
#include <mruby/reflection.hpp>
#include <mruby/compile.h>
#include "lifetime_allocator_library.hpp"

/* Only the namespace is listed: its classes are the types its functions
 * take and answer, as spec.reflect generates the list. */
constexpr auto allocator_classes = mrb_cpp_reflector::reflect_with_signature_types<mrb_cpp_reflector::reflect<^^c_library>()>();

bool aborts_in_child(void (*run)());

/* A deallocator runs in the free function of the handle, while the
 * garbage collector frees objects. A callback into Ruby from there would
 * run Ruby inside the collector, so the process ends instead. The lambda
 * comes from a method of its own: a lambda made next to the handle would
 * hold the local variable of the handle, and the callback, which C++
 * keeps, would keep the handle alive. Twenty handles are made, since a
 * register of the caller can still hold the last one when the collector
 * runs. */
static mrb_value callback_in_deallocator_aborts_q(mrb_state *, mrb_value)
{
    return mrb_bool_value(aborts_in_child([] {
        mrb_state *const other = mrb_open();
        mrb_cpp_reflector::reflect_define<allocator_classes>(other);
        mrb_load_string(other, "CLibrary::Handle.lifetime do\n"
                               "  allocator :handle_make\n"
                               "  deallocator :handle_close\n"
                               "end\n"
                               "def watcher\n"
                               "  -> { :called }\n"
                               "end\n"
                               "def watched\n"
                               "  h = CLibrary.handle_make(1)\n"
                               "  CLibrary.handle_watch(h, watcher)\n"
                               "  nil\n"
                               "end\n"
                               "20.times { watched }\n"
                               "GC.start\n");
        mrb_close(other);
    }));
}

/* At mrb_close the callbacks are closed, so a deallocator that calls one
 * reaches no Ruby, and the handle is freed once. */
static mrb_value watched_handle_freed_at_close_q(mrb_state *, mrb_value)
{
    const int before = c_library::handles_alive();
    mrb_state *const other = mrb_open();
    mrb_cpp_reflector::reflect_define<allocator_classes>(other);
    mrb_load_string(other, "CLibrary::Handle.lifetime do\n"
                           "  allocator :handle_make\n"
                           "  deallocator :handle_close\n"
                           "end\n"
                           "$kept = CLibrary.handle_make(1)\n"
                           "CLibrary.handle_watch($kept, -> { :called })\n");
    const bool raised = other->exc != nullptr;
    const bool made = c_library::handles_alive() == before + 1;
    mrb_close(other);
    return mrb_bool_value(!raised && made && c_library::handles_alive() == before);
}

void lifetime_allocator_gem_test(mrb_state *const mrb)
{
    mrb_define_module_function(mrb, mrb->kernel_module, "callback_in_deallocator_aborts?", callback_in_deallocator_aborts_q, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "watched_handle_freed_at_close?", watched_handle_freed_at_close_q, MRB_ARGS_NONE());
    mrb_cpp_reflector::reflect_define<allocator_classes>(mrb);
}
#else
void lifetime_allocator_gem_test(mrb_state *) {}
#endif
