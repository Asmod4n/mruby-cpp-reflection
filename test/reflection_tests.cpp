/*
 * A C++ class handed to reflect_define becomes a Ruby class under CPP::
 * with one method per public member function, one attribute per public
 * data member, argument formats derived from the signature (mruby.h, the
 * mrb_get_args table), overloads chosen by argument count and type, and
 * default arguments honoured. test.rb drives it from Ruby, so what is
 * asserted is what a Ruby caller sees. Only a compiler with C++26
 * reflection builds this; elsewhere the class is absent and test.rb
 * skips the assertions.
 */
#include <mruby.h>
#if defined(__cpp_impl_reflection)
#include <mruby/reflection.hpp>

struct Reflected {
    mrb_int total = 0;
    std::vector<mrb_int> seen;
    bool same(std::string_view a, mrb_int n) { total += n; return static_cast<mrb_int>(a.size()) == n; }
    mrb_int same(mrb_int n) { return n * 2; }
    mrb_value rest(mrb_value first, std::span<const mrb_value> more) { return more.empty() ? first : more.back(); }
    void add(mrb_int n) { total += n; seen.push_back(n); }
    mrb_int sum() const { return total; }
    mrb_int scaled_by(mrb_int n, mrb_int factor = 2) const { return n * factor; }
    std::string_view name() const { return "reflected"; }
    const std::vector<mrb_int> &history() const { return seen; }
    mrb_int count(const std::vector<mrb_int> &v) const { return static_cast<mrb_int>(v.size()); }
    std::string label{"l"};
    mrb_int length_of(const std::string &s) const { return static_cast<mrb_int>(s.size()); }
    std::string echo(std::string s) const { return s; }
};

struct Plain {
    mrb_int n = 1;
};

constexpr auto classes = mrb_cpp_reflector::reflect<^^Reflected>();
constexpr auto under = mrb_cpp_reflector::reflect<^^Plain>();

static_assert(std::string_view(mrb_cpp_reflector::reflect_get_args_format<std::meta::members_of(^^Reflected, std::meta::access_context::current())[2]>().data()) == "si");
static_assert(mrb_cpp_reflector::reflect_presym("same") != 0);
static_assert(mrb_cpp_reflector::reflect_presym("nowhere") == 0);

static mrb_value presym_ok_q(mrb_state *mrb, mrb_value)
{
    return mrb_bool_value(mrb_cpp_reflector::reflect_presym("same") == mrb_intern_lit(mrb, "same") &&
                          mrb_cpp_reflector::reflect_presym("Reflected") == mrb_intern_lit(mrb, "Reflected"));
}

extern "C" void mrb_mruby_cpp_reflection_gem_test(mrb_state *mrb)
{
    mrb_define_module_function(mrb, mrb->kernel_module, "reflect_presym_ok?", presym_ok_q, MRB_ARGS_NONE());
    mrb_cpp_reflector::reflect_define<classes>(mrb);
    mrb_cpp_reflector::reflect_define<under>(mrb, mrb_define_module(mrb, "Under"));
}
#else
extern "C" void mrb_mruby_cpp_reflection_gem_test(mrb_state *) {}
#endif
