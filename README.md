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

## Virtual functions overridden in Ruby

`reflect_define<classes, {.virtual_overriders = true}>(mrb)` together with
`reflect_virtual_overriders(spec, 'src/file.cpp')` in `mrbgem.rake` lets a
Ruby subclass override the virtual functions of a listed class:

```ruby
class RubySquare < Shapes::Square
  def area(k) = super * 10
end
```

- C++ calls the Ruby method where it calls the virtual function. `super`
  calls the C++ function it overrides. Without a Ruby method, the C++
  function runs.
- A pure virtual function without a Ruby method raises
  `NotImplementedError`, so an abstract class is made through a Ruby
  subclass.
- An argument that C++ passes by reference or pointer is lent for the call
  and detached when the call returns.
- The build step compiles the named file once more to write the overriders
  into `spec.build_dir`, and compiles the file with them in place of its
  own object.
- Left out: `private` and `final` virtual functions, classes without a
  virtual destructor, and objects that C++ creates itself. While the Ruby
  method of a function runs, a call of the same function on the same
  object from C++ runs the C++ function.

## Build

A compiler with `__cpp_impl_reflection`, today g++ 16 with `-freflection`,
and [mruby-c-ext-helpers](https://github.com/Asmod4n/mruby-c-ext-helpers)
for the value conversions.

Every reflected name is a presym. `mrbgem.rake` builds and runs a small
program before the presym scan and writes the names to
`build/<name>/include/mruby/presym/reflect.h`. Nothing of it is in the tree.
A gem calls `reflect_presyms(spec, "#{spec.dir}/tools/reflect_presyms/main.cpp")`
with a program that prints `reflect_presyms_header<^^A, ^^B>()`.

## Ideas not taken

libclang reads a header without reflection. A generator on top of it
could write plain C++ bindings that any compiler builds, MSVC included,
and it sees what reflection does not show: friend operators declared in a
class, inherited constructors and the values of default arguments. The
gem stays with reflection. Where reflection cannot answer a question,
the gem could ask clang's API for that part alone. This is a note for
later.
