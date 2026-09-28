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

`reflect_define<classes, {.virtual_overriders = true}>(mrb)` lets a
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
- The build compiles the file once more to write the overriders into
  `spec.build_dir`, and compiles the file with them in place of its own
  object.
- `objcopy` copies the overriders out of a section of that object file.
  `conf.objcopy = 'path'` in the build config names it; the default is the
  objcopy beside the C++ compiler.
- Left out: `private` and `final` virtual functions, classes without a
  virtual destructor, and objects that C++ creates itself. While the Ruby
  method of a function runs, a call of the same function on the same
  object from C++ runs the C++ function.

## Lifetimes that the types do not state

`<mruby/cpp_reflection.hpp>` declares what the C++ types of a class do
not say, after the declaration of the class:

```cpp
template <>
inline constexpr auto mruby::cpp_reflection::object_lifetime<^^TreeObject> = std::array{
    takes_ownership(^^TreeObject::set_parent, {.by = 0}),
    takes_ownership(^^TreeObject, {.by = "parent"}),
    ends_lifetime(^^TreeObject::destroy, 0),
};
```

A word names a function by its reflection, and the constructors by the
reflection of the class. A word applies to every function of that name
of the class, or of the scope around the class where the function takes
or makes the class. C++ has no reflection of an overloaded name, so a
word names an overloaded function by the reflection of one overload,
from `std::meta::members_of`. A position is a parameter number from 0 or
a parameter identifier, and the one left out is the receiver. The C++
side reads the declaration when it compiles and refuses a declaration
that the types contradict. Ruby has no way to declare or change a
lifetime at runtime. The words:

- `takes_ownership(^^C::set_parent, {.by = 0})` - after the call,
  argument 0 owns the receiver and deletes it; `{.of = 0}` says the
  receiver takes ownership of argument 0.
- `ends_lifetime(^^C::destroy, 0)` - the call ends the lifetime of
  argument 0 (of the receiver without a position).
- `retains(^^C::set_layout, 0)` - the receiver keeps argument 0.
- `errors(^^C::open, {.error = negative, .sets_errno = true})`,
  `{.success = 0}`, `{.error = nullptr}` - the answer that raises.
- `stack_reserve(^^C::parse, 65536)` - the call raises `SystemStackError`
  when the thread stack has less left.
- `threadsafe(^^C::start, false)` - the function takes only arguments
  that are copies.
- `allocator(^^lib::open, {.output_parameter = "made"})`,
  `deallocator(^^lib::close, {.results_of = ^^lib::open})`,
  `shared_ownership({.increment = ^^lib::ref, .decrement = ^^lib::unref})`
  - for a pointer to an incomplete type. The build stops at a reflected
  function that makes such a handle when its class declares none of these,
  and the message names the declaration that is missing.

`spec.reflect_object_lifetime 'lib::Klass' do ... end` in `mrbgem.rake` or
in the `conf.gem` block of the build config writes the same C++ into
`<mruby/reflect_object_lifetimes.h>`, which a source includes after the
headers of every class that it names. The words take the same arguments:
a function of the class is a Symbol, the constructors are `:initialize`,
a function beside the class is its C++ name as a String, and a position
is a number or a Symbol (`takes_ownership :set_parent, by: 0`,
`errors :open, error: :negative, sets_errno: true`,
`threadsafe :start, :no`, `deallocator 'lib::close', results_of: 'lib::open'`).
The build config wins over the gem, and rake prints one line for each
declaration it replaces.

## Generated bindings

`spec.reflect headers: ['lib.h'], scopes: ['lib']` in `mrbgem.rake`
writes the C++ of a gem: the listed namespaces and classes, and every
class and enum their functions take or answer.
The lists of trailing types of a function with `...` are declared in
C++:

```cpp
template <>
inline constexpr auto mruby::cpp_reflection::varargs<^^lib::f> = std::array{^^std::tuple<int>, ^^std::tuple<int, const char *>};
```

`spec.reflect_varargs 'lib::f', [%w[int], ['int', 'const char *']]`
writes the same C++ into `<mruby/reflect_varargs.h>`, which a source
includes after the header of the library.

## Build

A compiler with `__cpp_impl_reflection`, today g++ 16 with `-freflection`,
and [mruby-c-ext-helpers](https://github.com/Asmod4n/mruby-c-ext-helpers)
for the value conversions.

Each call of `reflect_define` has one list of the names that its classes
need, which the compiler builds, with each name once. When the call runs
for a state, it interns each name of the list once and keeps the symbols
in the `mrb_symbol_bridge` of that call, in the record of the gem for
that state. A class that is defined later, when a value of its type
reaches Ruby, has a list and a bridge of its own. The names that no class
owns have one list, which the gem interns in its `gem_init`.

The build finds each C++ source that contains `reflect_define<` and
`virtual_overriders`, in `src/` and `test/` of every gem of the build and
in the source that `spec.reflect` writes. It compiles such a source once
more, and the object file carries the virtual overriders in a section.
Nothing of it is in the tree.

## Tests

The C++ classes that the tests reflect are in `test/`, and a test
build of this gem builds them.

`bintest/` tests the Rake API of `mrbgem.rake` in CRuby.

## Ideas not taken

libclang reads a header without reflection. A generator on top of it
could write plain C++ bindings that any compiler builds, MSVC included,
and it sees what reflection does not show: friend operators declared in a
class, inherited constructors and the values of default arguments. The
gem stays with reflection. Where reflection cannot answer a question,
the gem could ask clang's API for that part alone. This is a note for
later.
