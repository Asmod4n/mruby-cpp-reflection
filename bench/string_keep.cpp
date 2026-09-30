#include <mruby.h>
#include <mruby/compile.h>

#include "named.hpp"
#include <mruby/reflect_facts.h>
#include <mruby/cpp_reflection.hpp>

#include <benchmark/benchmark.h>

#include <pwd.h>
#include <sys/resource.h>
#include <unistd.h>

#include <string>

#include "heap_counter.hpp"

constexpr auto classes = mruby::cpp_reflection::reflect<^^named::Named>();

struct State {
    mrb_state *mrb = mrb_open();
    mrb_value named;
    mrb_value text;
    State()
    {
        mruby::cpp_reflection::reflect_define<classes>(mrb);
        named = mrb_load_string(mrb, "$named = Named::Named.new");
        text = mrb_load_string(mrb, "$text = 'n' * 64");
    }
    State(const State &) = delete;
    State &operator=(const State &) = delete;
    ~State() { mrb_close(mrb); }
};

static State *state_of_run = nullptr;

static void pass(benchmark::State &state, const char *const method)
{
    State &s = *state_of_run;
    const mrb_sym name = mrb_intern_cstr(s.mrb, method);
    for (auto _ : state) {
        const int arena = mrb_gc_arena_save(s.mrb);
        benchmark::DoNotOptimize(mrb_funcall_argv(s.mrb, s.named, name, 1, &s.text));
        mrb_gc_arena_restore(s.mrb, arena);
    }
    if (s.mrb->exc != nullptr) state.SkipWithError("Ruby raised");
}

BENCHMARK_CAPTURE(pass, c_string_not_kept, "length_of_name");
BENCHMARK_CAPTURE(pass, c_string_kept, "set_name");
BENCHMARK_CAPTURE(pass, string_view_not_kept, "length_of_label");
BENCHMARK_CAPTURE(pass, string_view_kept, "set_label");

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
