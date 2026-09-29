#pragma once
#include <functional>
#include <mruby/cpp_reflection_lifetime.hpp>

/* The declarations of a C library, as its header shows them: the types
 * are incomplete, so only pointers to them cross the interface, and the
 * header does not say which function frees what another made. */
namespace c_library {
struct handle;
struct counted;
int handle_open(const char *name, handle **made);
handle *handle_make(int n);
handle *handle_popen(int n);
int handle_close(handle *h);
int handle_pclose(handle *h);
int handle_value(const handle *h);
void handle_watch(handle *h, std::function<void()> watcher);
int handles_alive();
counted *counted_find(int n);
void counted_ref(counted *c);
void counted_unref(counted *c);
int counted_count(const counted *c);
}

/* A library can declare its functions in a namespace and the class of
 * its handle at global scope, as Dear ImGui does with ImGuiContext and
 * ImGui::CreateContext. */
struct c_library_context;
namespace c_library {
c_library_context *create_context();
void destroy_context(c_library_context *c);
int contexts_alive();
c_library_context *current_context();
c_library_context *static_context();
}

/* As ImGui::GetIO answers the settings of the current context, a library
 * can answer a reference to a part of an object that it keeps. */
namespace c_library {
struct settings {
    int width = 0;
};
settings &current_settings();
int current_width();
}

/* Functions that make handles of types that no declaration names. The
 * build refuses to reflect them, so lifetime_allocator.cpp only checks
 * them with the function that the compile runs, and nothing defines
 * them. */
namespace c_library_undeclared {
struct loose;
struct stray;
struct unknown;
loose *loose_make();
void loose_free(loose *l);
int stray_open(stray **made);
void stray_close(stray *s);
unknown *unknown_make();
}

/* The header of the library does not say which function frees what
 * another made, so this declaration says it. */
#if defined(__cpp_impl_reflection)
template <>
inline constexpr auto mruby::cpp_reflection::object_lifetime<^^c_library::handle> = std::array{
    allocator(^^c_library::handle_open, {.output_parameter = "made"}),
    allocator(^^c_library::handle_make),
    allocator(^^c_library::handle_popen),
    deallocator(^^c_library::handle_close, {.results_of = ^^c_library::handle_open}),
    deallocator(^^c_library::handle_close, {.results_of = ^^c_library::handle_make}),
    deallocator(^^c_library::handle_pclose, {.results_of = ^^c_library::handle_popen}),
    errors(^^c_library::handle_open, {.success = 0}),
};
template <>
inline constexpr auto mruby::cpp_reflection::object_lifetime<^^c_library_context> = std::array{
    allocator(^^c_library::create_context),
    deallocator(^^c_library::destroy_context, {.results_of = ^^c_library::create_context}),
    borrowed(^^c_library::current_context),
    borrowed(^^c_library::static_context),
};
template <>
inline constexpr auto mruby::cpp_reflection::object_lifetime<^^c_library::settings> = std::array{
    borrowed(^^c_library::current_settings, {.owner = ^^c_library::current_context}),
};
template <>
inline constexpr auto mruby::cpp_reflection::object_lifetime<^^c_library::counted> = std::array{
    shared_ownership({.increment = ^^c_library::counted_ref, .decrement = ^^c_library::counted_unref}),
};
#endif
