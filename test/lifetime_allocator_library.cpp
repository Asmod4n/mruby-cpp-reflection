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
struct loose {
    int n = 0;
};
struct stray {
    int n = 0;
};
struct unknown {
    int n = 0;
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
loose *loose_make() { return new loose; }
void loose_free(loose *const l) { delete l; }
int stray_open(stray **const out)
{
    *out = new stray;
    return 0;
}
void stray_close(stray *const s) { delete s; }
unknown *unknown_make() { return new unknown; }

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
}
