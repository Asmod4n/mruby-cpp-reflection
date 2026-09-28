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
