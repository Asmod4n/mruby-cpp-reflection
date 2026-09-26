# mruby-cpp-reflection

A C++ class is a Ruby class. C++26 reflection reads the declaration and
defines the class, its methods, its attributes and its overloads.

## Use

```cpp
#include <mruby/reflection.hpp>

struct Counter {
    mrb_int total = 0;
    std::string label;
    void add(mrb_int n) { total += n; }
    mrb_int scaled_by(mrb_int n, mrb_int factor = 2) const { return n * factor; }
    const std::vector<mrb_int> &history() const { return seen; }
private:
    std::vector<mrb_int> seen;
};

constexpr auto classes = mrb_cpp_reflector::reflect<^^Counter>();

extern "C" void mrb_my_gem_gem_init(mrb_state *mrb)
{
    mrb_cpp_reflector::reflect_define<classes>(mrb);
}
```

```ruby
c = Counter.new
c.add(4)
c.total            # => 4
c.total = 9
c.scaled_by(5)     # => 10
c.scaled_by(5, 3)  # => 15
c.history          # => a Std::Vector, frozen
c.history.to_a     # => [4], a copy
c.label            # => a Std::String
c.label.to_s       # => "", a copy
c.label.replace("x")
c.seen.replace([1, 2])
```

Two calls. `reflect<^^A, ^^B>()` names the classes, once, at namespace
scope. `reflect_define<classes>(mrb)` defines them, in `gem_init`.
`reflect_define<classes>(mrb, outer)` defines them under `outer`, a module
or a class.

## What Ruby sees

- A class under the C++ namespace path, in CamelCase: `Counter` is
  `Counter`, `ns::Thing` is `Ns::Thing`, `std::vector` is `Std::Vector`.
  A name that is already defined at that place raises `NameError`.
- A type that is not listed in `reflect<>` and appears as a member, a
  parameter or a result is defined at first use, under its own namespace.
  One Ruby class per C++ type in the process.
- One method per public member function, under the C++ name. Overloads are
  one Ruby method; the call picks the overload by argument count and type.
  A parameter with a default argument is optional.
- One attribute per public data member: `total` and `total=`.
- `initialize` where the class has a default constructor.
- A `std::string`, `std::vector`, `std::array`, `std::map`, `std::set` or
  `std::pair` member or result is a `Std::` object over the C++ value.
  Its methods are the C++ methods. It is frozen where the C++ side is
  `const`. `to_s`, `to_a` and `to_h` give a Ruby copy.
- A parameter of such a type takes the `Std::` object as itself, and a Ruby
  String, Array or Hash as a copy.
- `replace` is `operator=`, on every class that has one: `x.replace(y)`
  copies `y` into `x`, from the same class or from a Ruby value, and
  returns `x`. That is the way a Ruby copy goes back into a C++ value.
- `std::string_view` and `std::span<const mrb_value>` parameters read the
  Ruby value in place. `mrb_value` passes through.
- A `const` object raises `FrozenError` on a method that is not `const`.
- A member function with no Ruby form is not defined: iterators, allocators,
  a non-const reference to a type without a reflected class, rvalue-qualified
  members, member templates.

## Build

A compiler with `__cpp_impl_reflection`, today g++ 16 with `-freflection`,
and [mruby-c-ext-helpers](https://github.com/Asmod4n/mruby-c-ext-helpers)
for the value conversions.

Every reflected name is a presym. `mrbgem.rake` builds and runs a small
program before the presym scan and writes the names to
`build/<name>/include/mruby/presym/reflect.h`. Nothing of it is in the tree.
A gem calls `reflect_presyms(spec, "#{spec.dir}/tools/reflect_presyms/main.cpp")`
with a program that prints `reflect_presyms_header<^^A, ^^B>()`.
