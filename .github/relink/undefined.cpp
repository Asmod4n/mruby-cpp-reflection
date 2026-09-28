#include <cstdlib>
namespace mruby::cpp_reflection {
[[noreturn]] void reflect_undefined() { std::exit(42); }
}
