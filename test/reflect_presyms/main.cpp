/*
 * Build-time only: prints the presym table for the types the gem's own
 * tests reflect. reflect_presyms(spec, this file) in mrbgem.rake builds
 * and runs it before the presym scan; nothing it prints enters the tree.
 */
#if defined(__cpp_impl_reflection)
#include <mruby/reflect_presyms.hpp>
#include <cstdio>
struct Reflected {
    mrb_int total = 0;
    std::vector<mrb_int> seen;
    bool same(std::string_view a, mrb_int n);
    mrb_int same(mrb_int n);
    mrb_value rest(mrb_value first, std::span<const mrb_value> more);
    void add(mrb_int n);
    mrb_int sum() const;
    mrb_int scaled_by(mrb_int n, mrb_int factor = 2) const;
    std::string_view name() const;
    const std::vector<mrb_int> &history() const;
    mrb_int count(const std::vector<mrb_int> &v) const;
};
constexpr auto classes = mrb_cpp_reflector::reflect<^^Reflected>();
int main() { std::fputs(mrb_cpp_reflector::reflect_presyms_header<classes>().c_str(), stdout); }
#else
int main() { return 0; }
#endif
