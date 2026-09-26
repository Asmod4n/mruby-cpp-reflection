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
c.history          # => a std::vector<long int>, frozen
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
`reflect_define<classes, {.nested_types = true}>(mrb)` also defines every
public nested class and enum of the listed classes. Without it, a nested
type is defined with its class only where a member uses it.

## What Ruby sees

- A class under the C++ namespace path, in CamelCase: `Counter` is
  `Counter`, `ns::Thing` is `Ns::Thing`. A class declared in a class is a
  constant of that class. A name that is already defined at that place
  raises `NameError`.
- A template has no name at runtime, so Ruby sees none. A specialization
  is a class. An alias that C++ gives it is its constant: `std::string`
  is `Std::String`. A specialization without an alias has no constant,
  and its name is the one C++ spells, such as `std::vector<int>`.
- A type that is not listed in `reflect<>` and appears as a member, a
  parameter or a result is defined at first use, under its own namespace.
  One Ruby class per C++ type in the process.
- One method per public member function, under the C++ name. Overloads are
  one Ruby method; the call picks the overload by argument count and type.
  A parameter with a default argument is optional.
- One attribute per public data member: `total` and `total=`.
- `initialize` where the class has a default constructor.
- A `std::string`, `std::vector`, `std::array`, `std::map`, `std::set` or
  `std::pair` member or result is an object over the C++ value.
  Its methods are the C++ methods. It is frozen where the C++ side is
  `const`. `to_s`, `to_a` and `to_h` give a Ruby copy.
- A parameter of such a type takes that object as itself, and a Ruby
  String, Array or Hash as a copy.
- `replace` is `operator=`, on every class that has one: `x.replace(y)`
  copies `y` into `x`, from the same class or from a Ruby value, and
  returns `x`. That is the way a Ruby copy goes back into a C++ value.
- `std::string_view` and `std::span<const mrb_value>` parameters read the
  Ruby value in place. `mrb_value` passes through.
- An enum is a class. Each enumerator is a frozen instance and a constant:
  `enum class Color { red }` gives `Color::Red`. A result is that
  constant, and a value that is no enumerator is a frozen instance of its
  own. A parameter takes only an instance. `to_i` gives the value, and a
  plain `enum` also answers `to_int`. Instances compare by value.
- A conversion function to an integer, a floating point type or text
  answers `to_i`, `to_f` or `to_s`, and `to_int` or `to_str` where it is
  not `explicit`.
- `void *` is a `VoidPointer` and `const void *` a `ConstVoidPointer`,
  which hold the address and nothing that reads the memory behind it.
  `nil` is a null pointer. `mrb_void_pointer_type` and
  `mrb_const_void_pointer_type` are their data types, for the `d` format of
  `mrb_get_args`.
- A `const` object raises `FrozenError` on a method that is not `const`.
- A member function with no Ruby form is not defined: iterators, allocators,
  a non-const reference to a type without a reflected class, rvalue-qualified
  members.
- A function template has no instance at runtime unless the program
  compiles one. An instance named in `reflect<>`, such as
  `^^ns::Scale::measure<ns::Apple>`, is an overload under the name of its
  template. `reflect_define<classes, {.templates = true}>(mrb)` also
  instantiates each function template of a listed class or namespace for
  the classes and enums of its namespace that its declaration accepts. A
  template whose body does not compile for such a type stops the build.

## Build

A compiler with `__cpp_impl_reflection`, today g++ 16 with `-freflection`,
and [mruby-c-ext-helpers](https://github.com/Asmod4n/mruby-c-ext-helpers)
for the value conversions.

Every reflected name is a presym. `mrbgem.rake` builds and runs a small
program before the presym scan and writes the names to
`build/<name>/include/mruby/presym/reflect.h`. Nothing of it is in the tree.
A gem calls `reflect_presyms(spec, "#{spec.dir}/tools/reflect_presyms/main.cpp")`
with a program that prints `reflect_presyms_header<^^A, ^^B>()`.
