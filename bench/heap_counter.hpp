#pragma once
#include <benchmark/benchmark.h>

#include <malloc.h>
#include <pwd.h>
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <string>

struct Heap {
    bool counting = false;
    std::int64_t allocations = 0;
    std::int64_t allocated = 0;
    std::int64_t live = 0;
    std::int64_t peak = 0;

    void add(void *const p)
    {
        if (!counting || p == nullptr) return;
        const auto bytes = static_cast<std::int64_t>(malloc_usable_size(p));
        allocations++;
        allocated += bytes;
        live += bytes;
        peak = std::max(peak, live);
    }
    void remove(void *const p)
    {
        if (!counting || p == nullptr) return;
        live -= static_cast<std::int64_t>(malloc_usable_size(p));
    }
};

inline Heap heap;

void *operator new(const std::size_t size)
{
    void *const p = std::malloc(size == 0 ? 1 : size);
    if (p == nullptr) throw std::bad_alloc();
    heap.add(p);
    return p;
}

void operator delete(void *const p) noexcept
{
    heap.remove(p);
    std::free(p);
}

void operator delete(void *const p, std::size_t) noexcept
{
    heap.remove(p);
    std::free(p);
}

extern "C" void *mrb_basic_alloc_func(void *const p, const size_t size)
{
    heap.remove(p);
    if (size == 0) {
        std::free(p);
        return nullptr;
    }
    void *const moved = std::realloc(p, size);
    heap.add(moved);
    return moved;
}

struct HeapCounter : benchmark::MemoryManager {
    void Start() override { heap = Heap{.counting = true}; }
    void Stop(Result &result) override
    {
        result.num_allocs = heap.allocations;
        result.max_bytes_used = heap.peak;
        result.total_allocated_bytes = heap.allocated;
        result.net_heap_growth = heap.live;
        heap.counting = false;
    }
};

inline void add_bench_context()
{
    const passwd *const user = getpwuid(getuid());
    benchmark::AddCustomContext("benchmark_lib", "google-benchmark-devel of Fedora 44 (" + std::string(benchmark::GetBenchmarkVersion()) + ")");
    benchmark::AddCustomContext("ran_as", user != nullptr ? user->pw_name : std::to_string(getuid()));
    benchmark::AddCustomContext("bench_nice", std::to_string(getpriority(PRIO_PROCESS, 0)));
    benchmark::AddCustomContext("bench_threads_max", std::to_string(sysconf(_SC_NPROCESSORS_ONLN) - 1));
}
