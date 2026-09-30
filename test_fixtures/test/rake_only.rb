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
