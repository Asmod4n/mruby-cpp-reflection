# mruby-cpp-reflection

A C++ class is a Ruby class. C++26 reflection reads the declaration and
defines the class, its methods, its attributes and its overloads.

## Use

```cpp
#include <mruby/cpp_reflection.hpp>

struct Counter {
    mrb_int total = 0;
    std::string label;
    void add(mrb_int n) { total += n; }
    mrb_int scaled_by(mrb_int n, mrb_int factor = 2) const { return n * factor; }
    const std::vector<mrb_int> &history() const { return seen; }
private:
    std::vector<mrb_int> seen;
};

constexpr auto classes = mruby::cpp_reflection::reflect<^^Counter>();

extern "C" void mrb_my_gem_gem_init(mrb_state *mrb)
{
    mruby::cpp_reflection::reflect_define<classes>(mrb);
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

`<mruby/cpp_reflection_lifetime.hpp>` declares what the C++ types of a class do
not say, after the declaration of the class:

```cpp
template <>
inline constexpr auto mruby::cpp_reflection::object_lifetime<^^TreeObject> = std::array{
    takes_ownership(^^TreeObject::set_parent, {.by = 0, .moves = true}),
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
  receiver takes ownership of argument 0. The call raises `ArgumentError`
  for an object that Ruby does not own, because a second C++ owner would
  delete it a second time. `{.moves = true}` says that the call takes the
  object away from its old owner, as `QObject::setParent` does, so an
  object that another C++ object owns can be handed over.
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
- `borrowed(^^lib::current)` - the function returns a pointer that the
  caller does not free. The call returns the Ruby object that already
  owns the C++ object, and raises `TypeError` when no Ruby object owns
  it. A null pointer is `nil`.
- `borrowed(^^lib::settings_of, {.owner = ^^lib::current})` - the
  function returns a reference or a pointer into the object that `owner`
  returns. The answer is a part of that object: not frozen, the same Ruby
  object on each call, and it keeps the Ruby object of the owner.
- `borrowed(^^lib::Io::fonts, {.owner = ^^lib::Io})` - the pointer field
  `fonts` of `Io` points to an object that lives as long as the `Io`.
  The field is that object, not a frozen copy: it is read again at each
  access, a null field is `nil`, the object of an old target ends when
  the field points elsewhere, and it keeps the Ruby object of the `Io`.

`spec.reflect_object_lifetime 'lib::Klass' do ... end` in `mrbgem.rake` or
in the `conf.gem` block of the build config writes the same C++ into
`<mruby/reflect_object_lifetimes.h>`, which a source includes after the
headers of every class that it names. The words take the same arguments:
a function of the class is a Symbol, the constructors are `:initialize`,
a function beside the class is its C++ name as a String, and a position
is a number or a Symbol (`takes_ownership :set_parent, by: 0, moves: true`,
`borrowed :fonts, owner: 'lib::Io'`,
`errors :open, error: :negative, sets_errno: true`,
`threadsafe :start, :no`, `deallocator 'lib::close', results_of: 'lib::open'`).
The build config wins over the gem, and rake prints one line for each
declaration it replaces.

`spec.reflect_packages` records the name, the query and the version that
`search_package` found. A `.lifetime` file of a found package is planned
as a TOML file that a future native C++ library reads, not this gem.

## Generated bindings

`spec.reflect 'lib', 'lib::Klass', headers: ['lib.h']` in `mrbgem.rake`
writes the C++ of a gem: the listed namespaces and classes, and every
class and enum their functions take or answer, with `gem_init` and
`gem_final`, so the gem needs no `src/`. The arguments are the scopes, in
the order of `reflect<^^lib, ^^lib::Klass>()`.
`c: <<~C` and `cxx: <<~CXX` take C and C++ text. The build writes the C
text into `reflect/reflect_c.c` of the build directory, which `spec.cc`
compiles. It writes the C++ text into `reflect/reflect_cxx.cxx`, which
the generated source includes after the headers and before the
reflection, so the reflection sees its declarations; `spec.cxx` compiles
it once, as a part of that source. The C++ text holds lifetime
declarations and the template instances the gem uses.
A class template instance is reflected only when a declaration names
it: a type alias or a derived class in the scope of the template, in the
`cxx:` text or in the C++ source of the gem. The build stops at any
other instance, and the message names it. An instance of a template of
namespace `std` is not affected.
The lists of trailing types of a function with `...` are declared in
C++:

```cpp
template <>
inline constexpr auto mruby::cpp_reflection::varargs<^^lib::f> = std::array{^^std::tuple<int>, ^^std::tuple<int, const char *>};
```

`spec.reflect_varargs 'lib::f', [%w[int], ['int', 'const char *']]`
writes the same C++ into `<mruby/reflect_varargs.h>`, which a source
includes after the header of the library.

## Facts that reflection cannot read

Three things of a header are gone before reflection sees them. g++
adjusts `float col[3]` to `float *col`. Reflection has no GNU attributes,
so it does not see `format(printf, 1, 2)`. The preprocessor removes every
macro. In a build with `-freflection`, `spec.reflect` has libclang read
the same headers, with the include paths and defines of the gem, and
write what it finds into `<mruby/reflect_facts.h>`. The generated source
includes it after the headers and before the `cxx:` text:

```cpp
mruby::cpp_reflection::reflect_parameter_extent(^^ImGui::ColorEdit3, 1); // 3
mruby::cpp_reflection::reflect_format_attribute(^^ImGui::Text);         // printf, 1, 2
std::meta::members_of(^^mruby::cpp_reflection::macros, ...);             // IMGUI_VERSION, ...
```

A row names the file, the line and the column of the name of a
function, which is what `std::meta::source_location_of` answers for it.
Each header file has its own table, so two gems that read one header
define the same table. A `va_list` is an array of one on x86-64, and it
is not a parameter of known extent. A macro is in
`mruby::cpp_reflection::macros` only when it is object like, is in a
header that is not a system header, and clang reads its expansion as a
C++ constant expression. The variable has the name of the macro, so a
source that names it in text gets the macro; reflection finds it by
`identifier_of`. `reflect_define` makes each one whose name starts with
a capital letter a constant of `outer`, and without `outer` a global
constant: `IMGUI_VERSION`.

`tool/write_reflect_facts.cpp` is the program that reads the headers.
The build compiles it once with the C++ compiler of the build and links
it against `libclang-cpp`, with the flags of `llvm-config`.
`conf.llvm_config = 'path'` in the build config names another one. It
writes a dependency file, and a change of any header that it read runs
it again.

## Build

A compiler with `__cpp_impl_reflection`, today g++ 16 with `-freflection`,
[mruby-c-ext-helpers](https://github.com/Asmod4n/mruby-c-ext-helpers)
for the value conversions, and for `spec.reflect` the development files
of libclang and LLVM (`clang-devel` and `llvm-devel` on Fedora).

Each call of `reflect_define` has one list of the names that its classes
need, which the compiler builds, with each name once. When the call runs
for a state, it interns each name of the list once and keeps the symbols
in the `mrb_symbol_bridge` of that call, in the record of the gem for
that state. A class that is defined later, when a value of its type
reaches Ruby, has a list and a bridge of its own. The names that the gem
itself defines, such as `to_s`, `each` and the operators, are presyms:
`MRB_SYM` and `MRB_OPSYM` at the place that defines them.

The build compiles each C++ source in `src/` and `test/` of every gem
that depends on this gem, and the source that `spec.reflect` writes,
once more. The object file of that compile carries the virtual
overriders in a section, and a source without them carries none.
Nothing of it is in the tree.

## Tests

The C++ classes that the tests reflect are in `src/` of the gem
`mruby-cpp-reflection-test_fixtures` in `test_fixtures/`. That gem
depends on this gem as any other gem does, and its `test/` holds the
Ruby tests. `test_fixtures/build_config.rb` builds and tests it:

    MRUBY_CONFIG=path/to/test_fixtures/build_config.rb rake test

`bintest/` tests the Rake API of `mrbgem.rake` in CRuby.

## Ideas not taken

libclang reads a header without reflection. A generator on top of it
could write plain C++ bindings that any compiler builds, MSVC included,
and it sees what reflection does not show: friend operators declared in a
class, inherited constructors and the values of default arguments. The
gem stays with reflection. Where reflection cannot answer a question,
the gem could ask clang's API for that part alone. This is a note for
later.
