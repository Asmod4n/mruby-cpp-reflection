# The gem rake_only has no source of its own: spec.reflect in its
# mrbgem.rake writes the reflection, gem_init and gem_final, the C text
# and the C++ text. These tests call into each of them.
assert('a gem reflects a library from its mrbgem.rake alone') do
  assert_equal 42, RakeOnly.c_answer
end

assert('the cxx: text declares the template instance that the gem uses') do
  # corners answers a rake_only::Pair<Vec2>, and the alias Corners in
  # the cxx: text is the declaration that the build asks for.
  corners = RakeOnly.corners
  assert_equal 'RakeOnly::Corners', corners.class.to_s
  assert_equal 2.0, corners.second.y
end

# g++ adjusts a parameter written as an array of known extent to a
# pointer before reflection sees it, reflection has no GNU attributes,
# and the preprocessor removes every macro before the compiler starts.
# libclang reads the headers of spec.reflect and writes these facts
# into reflect_facts.h. A va_list on x86-64 is an array of one, and it
# is no parameter of known extent.
assert('libclang gives the extent of an array parameter') do
  assert_true RakeOnly.array_parameter_has_its_extent
  assert_true RakeOnly.va_list_parameter_has_no_extent
end

assert('libclang gives the format attribute of a function') do
  assert_true RakeOnly.format_attribute_is_read
  assert_true RakeOnly.function_without_format_has_none
end

# A macro enters mruby::cpp_reflection::macros only when clang reads its
# expansion as a C++ constant expression. A macro that calls a function
# is not one.
assert('libclang gives the object like macros that are constants') do
  assert_true RakeOnly.macro_number_is_read
  assert_true RakeOnly.macro_string_is_read
  assert_true RakeOnly.macro_call_is_left_out
end

# A macro of the headers of spec.reflect is a constant of the module that
# reflect_define defines the scopes under. The generated source names no
# module, so the constant is global.
assert('a macro that is a constant is a global Ruby constant') do
  assert_equal 42, RAKE_ONLY_ANSWER
  assert_equal 'rake_only', RAKE_ONLY_NAME
  assert_false Object.const_defined?(:RAKE_ONLY_NOT_A_CONSTANT)
end

# g++ adjusts double values[3] to double *values, and libclang gives the
# 3 back. An Array of exactly 3 numbers is the argument. C++ writes into
# a copy, and after a call that ends without an exception the gem writes
# the copy back into the same Array. After an exception there is no
# answer, so the Array stays as it was.
assert('an array parameter of known extent takes an Array of that length') do
  assert_equal 6.5, RakeOnly.sum_of_three([1, 2, 3.5])
  assert_equal 6.0, RakeOnly.sum_of_three([1, 2, 3].freeze)
  assert_raise(ArgumentError) { RakeOnly.sum_of_three([1, 2]) }
  assert_raise(TypeError) { RakeOnly.sum_of_three([1, 2, 'three']) }
end

assert('an array parameter writes its values back into the Array') do
  values = [1.0, 2.0, 3.0]
  assert_nil RakeOnly.scale_three(values, 2)
  assert_equal [2.0, 4.0, 6.0], values
  counts = [1, 254]
  RakeOnly.count_two(counts)
  assert_equal [2, 255], counts
  flags = [true, false]
  RakeOnly.flip_two(flags)
  assert_equal [false, true], flags
end

assert('an array parameter refuses a frozen Array and a value that does not fit') do
  assert_raise(FrozenError) { RakeOnly.scale_three([1.0, 2.0, 3.0].freeze, 2) }
  assert_raise(RangeError) { RakeOnly.count_two([1, 256]) }
  assert_raise(TypeError) { RakeOnly.flip_two([true, 1]) }
end

assert('an array parameter stays as it was after an exception') do
  values = [1.0, 2.0, 3.0]
  assert_raise(RuntimeError) { RakeOnly.scale_three_then_throw(values) }
  assert_equal [1.0, 2.0, 3.0], values
end

# A Ruby block that C++ keeps after the call must be kept from the
# collector, as close as possible to the object that holds it. Reflection
# sees no function body, so libclang reads the body where the header has
# it and says where the callback lands: assigned to one field, or pushed
# into a container, and which object holds that field. A callback that
# is only called lands nowhere.
assert('libclang gives where a callback parameter lands') do
  assert_true RakeOnly.assigned_callback_has_one_place
  assert_true RakeOnly.pushed_callback_has_many_places
  assert_true RakeOnly.called_callback_is_not_kept
  assert_true RakeOnly.member_keeps_callback_in_this
end

# C and C++ expect that memory passed to a function stays as it is while
# the function runs, and a pointer that a function keeps expects the same
# after it. Ruby can run inside the call and the collector with it. So a
# String reaches C++ as a byte slice of itself: when Ruby changes the
# String, the String gets a buffer of its own and the slice keeps the old
# one. Where libclang shows that the function keeps the pointer, the gem
# keeps the slice on the object that holds the pointer, after the call
# returned, so the collector does not free it.
assert('a String that C++ keeps stays what it was') do
  assert_true RakeOnly.kept_string_lands_in_this
  named = RakeOnly.make_named
  name = 'n' * 64
  label = 'l' * 64
  named.set_name(name)
  named.set_label(label)
  name.replace('x' * 64)
  label.replace('y' * 64)
  name = label = nil
  3.times do
    GC.start
    Array.new(1000) { |i| 'garbage' * (i % 16) }
  end
  assert_true named.name_is('n' * 64)
  assert_true named.label_is('l' * 64)
end

assert('a String with a NUL byte is refused where C++ reads a C string') do
  assert_raise(ArgumentError) { RakeOnly.make_named.set_name("a\0b") }
  # A kept name that raises keeps the name before it: the gem reads the
  # String before it replaces the copy that the C++ field points into.
  kept = RakeOnly.make_named
  kept.set_name('ok')
  assert_raise(ArgumentError) { kept.set_name("a\0b") }
  GC.start
  assert_true kept.name_is('ok')
end

# A String that C++ pushes into a container needs one kept value per
# element, and the gem keeps one value per field. So the call raises
# before C++ runs, rather than free the String of an earlier call that
# the container still points to.
assert('a String that C++ keeps in a container raises NotImplementedError') do
  named = RakeOnly.make_named
  assert_raise(NotImplementedError) { named.add_alias('first') }
end
