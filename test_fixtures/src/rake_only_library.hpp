#pragma once

/* The header of a library that the gem rake_only reflects from its
 * mrbgem.rake alone. rake_only_c_answer is defined in the c: text of
 * that spec.reflect, and Pair<Vec2> is declared in its cxx: text. */
extern "C" int rake_only_c_answer(void);

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
inline int c_answer() { return rake_only_c_answer(); }
}
