/*
 * A C library hands out pointers to types it never defines in its
 * header. The gem cannot know who frees such a handle, so the build
 * refuses a function that makes one unless a declaration names the
 * allocator and the deallocator of the handle, as
 * lifetime_allocator_library.hpp does. lifetime_allocator.rb drives c_library
 * from Ruby.
 */
#include <mruby.h>
#if defined(__cpp_impl_reflection)
#include "lifetime_allocator_library.hpp"
#include "lifetime_dsl.hpp"
#include <mruby/reflection.hpp>
#include <mruby/compile.h>
#include <mruby/hash.h>

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
        mrb_load_string(other, "def watcher\n"
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
    mrb_load_string(other, "$kept = CLibrary.handle_make(1)\n"
                           "CLibrary.handle_watch($kept, -> { :called })\n");
    const bool raised = other->exc != nullptr;
    const bool made = c_library::handles_alive() == before + 1;
    mrb_close(other);
    return mrb_bool_value(!raised && made && c_library::handles_alive() == before);
}

namespace allocated_wrong {
using namespace mruby::cpp_reflection;
/* Each declaration below is one that the C++ side refuses when it
 * compiles the class of the handle, checked with the function that the
 * compile runs. */
constexpr auto unnamed_output = std::array{allocator(^^c_library_undeclared::stray_open), deallocator(^^c_library_undeclared::stray_close)};
constexpr auto wrong_output = std::array{allocator(^^c_library::handle_open, {.output_parameter = 0}), deallocator(^^c_library::handle_close)};
constexpr auto makes_nothing = std::array{allocator(^^c_library::handle_value), deallocator(^^c_library::handle_close)};
constexpr auto frees_nothing = std::array{allocator(^^c_library::handle_make), deallocator(^^c_library::handle_make)};
constexpr auto no_deallocator = std::array{allocator(^^c_library_undeclared::loose_make)};
constexpr auto no_decrement = std::array{shared_ownership({.increment = ^^c_library::counted_ref, .decrement = ^^c_library::counted_find})};
constexpr auto right = std::array{allocator(^^c_library::handle_open, {.output_parameter = "made"}), allocator(^^c_library::handle_popen),
                                  deallocator(^^c_library::handle_close, {.results_of = ^^c_library::handle_open}),
                                  deallocator(^^c_library::handle_pclose, {.results_of = ^^c_library::handle_popen})};
}

static mrb_value allocator_declaration_errors_m(mrb_state *const mrb, mrb_value)
{
    using mrb_cpp_reflector::reflect_object_lifetime_error;
    const mrb_value errors = mrb_hash_new(mrb);
    const auto set = [&](const char *const key, const std::string_view error) {
        mrb_hash_set(mrb, errors, mrb_str_new_cstr(mrb, key), error.empty() ? mrb_nil_value() : mrb_str_new(mrb, error.data(), static_cast<mrb_int>(error.size())));
    };
    set("unnamed_output", reflect_object_lifetime_error(^^c_library_undeclared::stray, allocated_wrong::unnamed_output));
    set("wrong_output", reflect_object_lifetime_error(^^c_library::handle, allocated_wrong::wrong_output));
    set("makes_nothing", reflect_object_lifetime_error(^^c_library::handle, allocated_wrong::makes_nothing));
    set("frees_nothing", reflect_object_lifetime_error(^^c_library::handle, allocated_wrong::frees_nothing));
    set("no_deallocator", reflect_object_lifetime_error(^^c_library_undeclared::loose, allocated_wrong::no_deallocator));
    set("no_decrement", reflect_object_lifetime_error(^^c_library::counted, allocated_wrong::no_decrement));
    set("right", reflect_object_lifetime_error(^^c_library::handle, allocated_wrong::right));
    return errors;
}

/* A function that makes a handle whose lifetime no declaration states
 * stops the build when it is reflected. Each entry is the text that the
 * compiler prints for one such function, checked with the function that
 * the compile runs; nil is a function that the build takes. */
static mrb_value missing_lifetime_errors_m(mrb_state *const mrb, mrb_value)
{
    using mrb_cpp_reflector::reflect_missing_object_lifetime;
    const mrb_value errors = mrb_hash_new(mrb);
    const auto set = [&](const char *const key, const std::string_view error) {
        mrb_hash_set(mrb, errors, mrb_str_new_cstr(mrb, key), error.empty() ? mrb_nil_value() : mrb_str_new(mrb, error.data(), static_cast<mrb_int>(error.size())));
    };
    set("unknown_make", reflect_missing_object_lifetime(^^c_library_undeclared, ^^c_library_undeclared::unknown_make));
    set("loose_make", reflect_missing_object_lifetime(^^c_library_undeclared, ^^c_library_undeclared::loose_make));
    set("stray_open", reflect_missing_object_lifetime(^^c_library_undeclared, ^^c_library_undeclared::stray_open));
    set("handle_make", reflect_missing_object_lifetime(^^c_library, ^^c_library::handle_make));
    set("handle_open", reflect_missing_object_lifetime(^^c_library, ^^c_library::handle_open));
    set("counted_find", reflect_missing_object_lifetime(^^c_library, ^^c_library::counted_find));
    set("handle_value", reflect_missing_object_lifetime(^^c_library, ^^c_library::handle_value));
    return errors;
}

void lifetime_allocator_gem_init(mrb_state *const mrb)
{
    mrb_define_module_function(mrb, mrb->kernel_module, "allocator_declaration_errors", allocator_declaration_errors_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "missing_lifetime_errors", missing_lifetime_errors_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "callback_in_deallocator_aborts?", callback_in_deallocator_aborts_q, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "watched_handle_freed_at_close?", watched_handle_freed_at_close_q, MRB_ARGS_NONE());
    mrb_cpp_reflector::reflect_define<allocator_classes>(mrb);
}
#else
void lifetime_allocator_gem_init(mrb_state *) {}
#endif
