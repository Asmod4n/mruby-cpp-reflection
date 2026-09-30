#pragma once

/* The header of a library that the gem rake_only reflects from its
 * mrbgem.rake alone. rake_only_c_answer is defined in the c: text of
 * that spec.reflect, and Pair<Vec2> is declared in its cxx: text. */
extern "C" int rake_only_c_answer(void);

/* The facts that reflection cannot read and libclang reads for it: an
 * array parameter of known extent, a format attribute, a va_list that
 * is an array of one on x86-64 and is no array parameter, and three
 * macros, of which the last is no constant expression. */
#include <cstdarg>
#include <stdexcept>
extern "C" double rake_only_sum_of_three(const double values[3]);
extern "C" int rake_only_format(const char *format, ...) __attribute__((format(printf, 1, 2)));
extern "C" int rake_only_vformat(const char *format, va_list arguments);
#define RAKE_ONLY_ANSWER 42
#define RAKE_ONLY_NAME "rake_only"
#define RAKE_ONLY_NOT_A_CONSTANT rake_only_c_answer()

namespace rake_only {
struct Vec2 {
    double x = 0;
    double y = 0;
};
template <class T>
struct Pair {
    T first{};
    T second{};
};
inline Pair<Vec2> corners() { return {{0, 0}, {1, 2}}; }
inline double sum_of_three(const double values[3]) { return values[0] + values[1] + values[2]; }
inline void scale_three(double values[3], const double factor)
{
    for (int i = 0; i < 3; i++) values[i] *= factor;
}
inline void scale_three_then_throw(double values[3])
{
    values[0] = 99;
    throw std::runtime_error("scale_three_then_throw");
}
inline void count_two(unsigned char counts[2])
{
    counts[0]++;
    counts[1]++;
}
inline void flip_two(bool flags[2])
{
    flags[0] = !flags[0];
    flags[1] = !flags[1];
}
inline int c_answer() { return rake_only_c_answer(); }
}
