#include <mruby.h>
#include <mruby/string.h>

#include <benchmark/benchmark.h>

#include <string>
#include <string_view>

#include "heap_counter.hpp"

[[gnu::noinline]] char last_byte_of(const std::string_view text)
{
    return text.back();
}

struct State {
    mrb_state *mrb = mrb_open();
    State(const State &) = delete;
    State &operator=(const State &) = delete;
    State() = default;
    ~State() { mrb_close(mrb); }
};

static State *state_of_run = nullptr;

static mrb_value made_string(benchmark::State &state)
{
    mrb_state *const mrb = state_of_run->mrb;
    const auto size = static_cast<mrb_int>(state.range(0));
    const mrb_value ruby = mrb_str_new(mrb, std::string(static_cast<std::size_t>(size), 'a').data(), size);
    mrb_gc_register(mrb, ruby);
    return ruby;
}

static void view(benchmark::State &state)
{
    const mrb_value ruby = made_string(state);
    for (auto _ : state) benchmark::DoNotOptimize(last_byte_of(std::string_view(RSTRING_PTR(ruby), static_cast<std::size_t>(RSTRING_LEN(ruby)))));
    mrb_gc_unregister(state_of_run->mrb, ruby);
    state.SetBytesProcessed(state.iterations() * state.range(0));
}

static void frozen_view(benchmark::State &state)
{
    const mrb_value ruby = made_string(state);
    RString *const string = mrb_str_ptr(ruby);
    for (auto _ : state) {
        const bool was_frozen = mrb_frozen_p(string);
        string->frozen = 1;
        benchmark::DoNotOptimize(last_byte_of(std::string_view(RSTRING_PTR(ruby), static_cast<std::size_t>(RSTRING_LEN(ruby)))));
        if (!was_frozen) string->frozen = 0;
        benchmark::ClobberMemory();
    }
    mrb_gc_unregister(state_of_run->mrb, ruby);
    state.SetBytesProcessed(state.iterations() * state.range(0));
}

static void copy(benchmark::State &state)
{
    const mrb_value ruby = made_string(state);
    for (auto _ : state) {
        const std::string copied(RSTRING_PTR(ruby), static_cast<std::size_t>(RSTRING_LEN(ruby)));
        benchmark::DoNotOptimize(last_byte_of(copied));
    }
    mrb_gc_unregister(state_of_run->mrb, ruby);
    state.SetBytesProcessed(state.iterations() * state.range(0));
}

static void sizes(benchmark::Benchmark *const b)
{
    for (const long bytes : {20L, 10L * 1024, 20L * 1024, 30L * 1024, 40L * 1024, 50L * 1024, 2L * 1024 * 1024, 4L * 1024 * 1024}) b->Arg(bytes);
}

BENCHMARK(view)->Apply(sizes);
BENCHMARK(frozen_view)->Apply(sizes);
BENCHMARK(copy)->Apply(sizes);

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
