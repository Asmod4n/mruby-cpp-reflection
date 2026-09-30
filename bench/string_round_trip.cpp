#include <mruby.h>
#include <mruby/string.h>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <string>

#include "heap_counter.hpp"

[[gnu::noinline]] void change_first_byte(std::string &text)
{
    text.front() = static_cast<char>(text.front() ^ 1);
}

struct State {
    mrb_state *mrb = mrb_open();
    State(const State &) = delete;
    State &operator=(const State &) = delete;
    State() = default;
    ~State() { mrb_close(mrb); }
};

static State *state_of_run = nullptr;

static void round_trip(benchmark::State &state)
{
    mrb_state *const mrb = state_of_run->mrb;
    const auto size = static_cast<mrb_int>(state.range(0));
    const mrb_value ruby = mrb_str_new(mrb, std::string(static_cast<std::size_t>(size), 'a').data(), size);
    mrb_gc_register(mrb, ruby);
    for (auto _ : state) {
        std::string copy(RSTRING_PTR(ruby), static_cast<std::size_t>(RSTRING_LEN(ruby)));
        change_first_byte(copy);
        mrb_str_resize(mrb, ruby, static_cast<mrb_int>(copy.size()));
        std::ranges::copy(copy, RSTRING_PTR(ruby));
        benchmark::DoNotOptimize(RSTRING_PTR(ruby));
        benchmark::ClobberMemory();
    }
    mrb_gc_unregister(mrb, ruby);
    state.SetBytesProcessed(state.iterations() * size);
}

static void in_place(benchmark::State &state)
{
    std::string text(static_cast<std::size_t>(state.range(0)), 'a');
    for (auto _ : state) {
        change_first_byte(text);
        benchmark::DoNotOptimize(text.data());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}

static void sizes(benchmark::Benchmark *const b)
{
    for (const long bytes : {20L, 10L * 1024, 20L * 1024, 30L * 1024, 40L * 1024, 50L * 1024, 2L * 1024 * 1024, 4L * 1024 * 1024}) b->Arg(bytes);
}

BENCHMARK(round_trip)->Apply(sizes);
BENCHMARK(in_place)->Apply(sizes);

int main(int argc, char **argv)
{
    HeapCounter counter;
    benchmark::RegisterMemoryManager(&counter);
    add_bench_context();
    benchmark::Initialize(&argc, argv);
    State state;
    state_of_run = &state;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
