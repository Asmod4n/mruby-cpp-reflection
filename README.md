# mruby-cpp-reflection

A C++ class becomes a Ruby class through C++26 reflection. The user calls two
things: `mrb_cpp_reflector::reflect<^^A, ^^B>()` once, outside `gem_init`, to
name the types, and `mrb_cpp_reflector::reflect_define<classes>(mrb)` in
`gem_init` to define them. Everything else follows from the declarations.

Needs a compiler with `__cpp_impl_reflection` (g++ 16 with `-freflection`) and
[mruby-c-ext-helpers](https://github.com/Asmod4n/mruby-c-ext-helpers) for the
value conversions in both directions.

## What it does

`include/mruby/reflection.hpp` (namespace `mrb_cpp_reflector`, active only where the
compiler defines `__cpp_impl_reflection`, today g++ 16 with `-freflection`):

```cpp
struct Counter {
  mrb_int total = 0;
  void add(mrb_int n) { total += n; }
  mrb_int sum() { return total; }
  bool same(std::string_view a, mrb_int n);   // overloads dispatch on
  mrb_int same(mrb_int n);                     // argument count and type
};
MRB_CPP_DEFINE_TYPE(Counter, counter)

RClass *klass = mrb_cpp_reflector::reflect_define_class<^^Counter>(mrb, mrb->object_class);
mrb_cpp_reflector::reflect_define_method(mrb, klass, mrb_intern_lit(mrb, "scaled"),
    [factor](Counter &c, mrb_int n) { return c.total * factor + n; });
```

- `reflect_define_class<^^T>` defines the Ruby class under the C++ name with one
  method per public non-static member function; `initialize` uses `mrb_cpp_new<T>`.
- `reflect_define_method<^^T::f...>` defines one method from an overload set.
- `reflect_define_method(mrb, klass, sym, lambda)` defines a method from a lambda;
  its first parameter is `self`, as `T &` or `mrb_value`; the closure lives in the
  proc's env.
- A parameter with a default argument is optional in Ruby; the call uses as many
  arguments as Ruby gave. A public member function whose parameter or return type
  has no Ruby form (iterators, allocators, mutable references, `&&`-qualified
  members, member templates) is left out, so `std::string` and `std::vector<T>`
  reflect as far as Ruby can reach them: `BasicString.new.append('ab').find('b')`.
  A class name goes to CamelCase; a method name stays as it is.
- `reflect_get_args<^^T::f>(mrb)` returns the arguments as a tuple; the format for
  the format string for `mrb_get_args` comes from the parameter types (`mrb_value` o, `mrb_int` i,
  `mrb_float` f, `mrb_bool` b, `mrb_sym` n, `RClass *` c, `std::string_view` s,
  `std::span<const mrb_value>` *). A type without a letter does not compile.
- Presyms: `reflect_presyms_header<^^T...>()` returns a header that lists every
  reflected name as `MRB_SYM(name)` beside a table. Written to
  `include/mruby/reflect_presyms.h` and included from a gem source, the presym
  scanner picks the names up and `reflect_presym(name)` yields the symbol at
  compile time; without the header, names are interned once with
  `mrb_intern_static` when the class is defined.

## The one rule for Ruby code

An object of a `CPP::` class stays C++. Ruby reads and changes it through its
methods and attributes. Where Ruby code needs a Ruby String, Array or Hash,
call `to_s`, `to_a` or `to_h` once; the result is a snapshot, and changes to it
do not reach the C++ value. `to_str`, `to_ary`, `to_hash` and `to_int` are not
defined, because they would claim the object is a String, Array, Hash or
Integer at heart, and it is not.
