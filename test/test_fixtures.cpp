#include <mruby.h>

/* The fixtures are the gem mruby-cpp-reflection-test_fixtures, so that
 * nothing in src/ exists only for the tests. The state of these tests
 * holds only the dependencies of mruby-cpp-reflection, so the fixtures
 * are defined here. The build config loads that gem. */
extern "C" void GENERATED_TMP_mrb_mruby_cpp_reflection_test_fixtures_gem_init(mrb_state *mrb);

extern "C" void mrb_mruby_cpp_reflection_gem_test(mrb_state *mrb)
{
    GENERATED_TMP_mrb_mruby_cpp_reflection_test_fixtures_gem_init(mrb);
}
