#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/cpp_reflection.hpp>

#include <benchmark/benchmark.h>

#include <malloc.h>
#include <pwd.h>
#include <sys/resource.h>
#include <unistd.h>

#include <cstdlib>
#include <functional>
#include <new>
#include <string>
#include <vector>

using number = mrb_int;

namespace sink {
class Sink {
    std::function<number(number)> kept;
    std::vector<std::function<number(number)>> many;

public:
    number take(const number n) { return n; }
    number call_now(const std::function<number(number)> &f) { return f ? 1 : 0; }
    void keep(std::function<number(number)> f) { kept = std::move(f); }
    void add(std::function<number(number)> f) { many.push_back(std::move(f)); }
    void clear()
    {
        kept = nullptr;
        many.clear();
        many.shrink_to_fit();
    }
};
}

constexpr auto classes = mruby::cpp_reflection::reflect<^^sink::Sink>();

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

static Heap heap;

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
    void Start() override
    {
        heap = Heap{.counting = true};
    }
    void Stop(Result &result) override
    {
        result.num_allocs = heap.allocations;
        result.max_bytes_used = heap.peak;
        result.total_allocated_bytes = heap.allocated;
        result.net_heap_growth = heap.live;
        heap.counting = false;
    }
};

struct State {
    mrb_state *mrb = mrb_open();
    mrb_value sink;
    mrb_value blocks;
    State()
    {
        mruby::cpp_reflection::reflect_define<classes>(mrb);
        sink = mrb_load_string(mrb, "$sink = Sink::Sink.new");
        blocks = mrb_load_string(mrb, "$blocks = Array.new(256) { |i| ->(n) { n + i } }");
    }
    State(const State &) = delete;
    State &operator=(const State &) = delete;
    ~State() { mrb_close(mrb); }
    mrb_value block(const std::int64_t i) const { return mrb_ary_entry(blocks, static_cast<mrb_int>(i % 256)); }
};

static State *state_of_run = nullptr;

template <class Call>
void run(benchmark::State &state, const Call &call)
{
    State &s = *state_of_run;
    std::int64_t i = 0;
    for (auto _ : state) {
        const int arena = mrb_gc_arena_save(s.mrb);
        benchmark::DoNotOptimize(call(s, i++));
        mrb_gc_arena_restore(s.mrb, arena);
    }
    if (s.mrb->exc != nullptr) state.SkipWithError("Ruby raised");
}

static void clear_sink(const benchmark::State &)
{
    State &s = *state_of_run;
    const bool counting = std::exchange(heap.counting, false);
    mrb_funcall_argv(s.mrb, s.sink, mrb_intern_lit(s.mrb, "clear"), 0, nullptr);
    const mrb_value block = s.block(0);
    mrb_funcall_argv(s.mrb, s.sink, mrb_intern_lit(s.mrb, "call_now"), 1, &block);
    mrb_funcall_argv(s.mrb, s.sink, mrb_intern_lit(s.mrb, "call_now"), 1, &block);
    mrb_full_gc(s.mrb);
    heap.counting = counting;
}

static void take_integer(benchmark::State &state)
{
    run(state, [](State &s, std::int64_t) {
        const mrb_value one = mrb_fixnum_value(1);
        return mrb_funcall_argv(s.mrb, s.sink, mrb_intern_lit(s.mrb, "take"), 1, &one);
    });
}

static void pass_block_not_kept(benchmark::State &state)
{
    run(state, [](State &s, const std::int64_t i) {
        const mrb_value block = s.block(i);
        return mrb_funcall_argv(s.mrb, s.sink, mrb_intern_lit(s.mrb, "call_now"), 1, &block);
    });
}

static void pass_block_kept_one(benchmark::State &state)
{
    run(state, [](State &s, const std::int64_t i) {
        const mrb_value block = s.block(i);
        return mrb_funcall_argv(s.mrb, s.sink, mrb_intern_lit(s.mrb, "keep"), 1, &block);
    });
}

static void pass_block_kept_many(benchmark::State &state)
{
    run(state, [](State &s, const std::int64_t i) {
        const mrb_value block = s.block(i);
        return mrb_funcall_argv(s.mrb, s.sink, mrb_intern_lit(s.mrb, "add"), 1, &block);
    });
}

BENCHMARK(take_integer)->Setup(clear_sink);
BENCHMARK(pass_block_not_kept)->Setup(clear_sink);
BENCHMARK(pass_block_kept_one)->Setup(clear_sink);
BENCHMARK(pass_block_kept_many)->Setup(clear_sink);
BENCHMARK(pass_block_kept_many)->Name("held_bytes_of_16_kept_blocks")->Iterations(16)->Setup(clear_sink);

int main(int argc, char **argv)
{
    HeapCounter counter;
    benchmark::RegisterMemoryManager(&counter);
    const passwd *const user = getpwuid(getuid());
    benchmark::AddCustomContext("benchmark_lib", "google-benchmark-devel of Fedora 44 (" + std::string(benchmark::GetBenchmarkVersion()) + ")");
    benchmark::AddCustomContext("ran_as", user != nullptr ? user->pw_name : std::to_string(getuid()));
    benchmark::AddCustomContext("bench_nice", std::to_string(getpriority(PRIO_PROCESS, 0)));
    benchmark::AddCustomContext("bench_threads_max", std::to_string(sysconf(_SC_NPROCESSORS_ONLN) - 1));
    benchmark::Initialize(&argc, argv);
    State state;
    state_of_run = &state;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
