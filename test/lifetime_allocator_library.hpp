#pragma once
#include <functional>

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
