/*
 * The definitions of the C library of lifetime_allocator.cpp. They live
 * in a translation unit of their own, so the types stay incomplete where
 * the gem reflects them. A function that frees a handle it did not make,
 * or frees one twice, ends the process, so a wrong deallocator shows in
 * every build and not only under AddressSanitizer.
 */
#include "lifetime_allocator_library.hpp"
#include <cstdlib>
#include <cstring>
#include <set>

namespace c_library {
struct handle {
    int n;
    bool piped;
    std::function<void()> watcher;
};
struct counted {
    int count = 1;
};

static std::set<const handle *> &live()
{
    static std::set<const handle *> handles;
    return handles;
}

static handle *made(const int n, const bool piped)
{
    handle *const h = new handle{n, piped, {}};
    live().insert(h);
    return h;
}

static int freed(handle *const h, const bool piped)
{
    if (live().erase(h) != 1 || h->piped != piped) std::abort();
    if (h->watcher) h->watcher();
    delete h;
    return 0;
}

int handle_open(const char *const name, handle **const out)
{
    if (std::strlen(name) == 0) {
        *out = nullptr;
        return -1;
    }
    *out = made(static_cast<int>(std::strlen(name)), false);
    return 0;
}
handle *handle_make(const int n) { return n < 0 ? nullptr : made(n, false); }
handle *handle_popen(const int n) { return made(n, true); }
int handle_close(handle *const h) { return freed(h, false); }
int handle_pclose(handle *const h) { return freed(h, true); }
int handle_value(const handle *const h)
{
    if (!live().contains(h)) std::abort();
    return h->n;
}
void handle_watch(handle *const h, std::function<void()> watcher) { h->watcher = std::move(watcher); }
int handles_alive() { return static_cast<int>(live().size()); }

/* The library keeps one share of each counted object in a registry and
 * hands out pointers to it, as a reference-counted C library does. */
static counted &registered()
{
    static counted kept;
    return kept;
}
counted *counted_find(const int) { return &registered(); }
void counted_ref(counted *const c) { ++c->count; }
void counted_unref(counted *const c)
{
    if (c->count <= 1) std::abort();
    --c->count;
}
int counted_count(const counted *const c) { return c->count; }

static int &contexts()
{
    static int alive = 0;
    return alive;
}
int contexts_alive() { return contexts(); }
}

struct c_library_context {
    int n;
    c_library::settings settings;
};

/* As in Dear ImGui, the library keeps the last context it made as the
 * current one, and forgets it when that context is destroyed. */
static c_library_context *&current()
{
    static c_library_context *kept = nullptr;
    return kept;
}

c_library_context *c_library::create_context()
{
    ++c_library::contexts();
    current() = new c_library_context{0};
    return current();
}
void c_library::destroy_context(c_library_context *const c)
{
    --c_library::contexts();
    if (current() == c) current() = nullptr;
    delete c;
}
c_library_context *c_library::current_context() { return current(); }
c_library::settings &c_library::current_settings() { return current()->settings; }
int c_library::current_width() { return current() == nullptr ? -1 : current()->settings.width; }

/* A context that no allocator made: the gem tracks no Ruby object for it. */
c_library_context *c_library::static_context()
{
    static c_library_context kept{0};
    return &kept;
}

namespace c_library {
static atlas &atlas_at(const int which)
{
    static atlas first;
    static atlas second;
    return which == 1 ? first : second;
}
io io_make() { return {}; }
void io_use_atlas(io &target, const int which) { target.fonts = which == 0 ? nullptr : &atlas_at(which); }
int atlas_width(const int which) { return atlas_at(which).width; }
}
