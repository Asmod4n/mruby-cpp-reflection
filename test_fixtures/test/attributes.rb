if Object.const_defined?(:Attributes)
  assert('a parameter that nonnull names refuses nil') do
    assert_equal 1, Attributes.read(Attributes::Box.new)
    assert_raise(TypeError) { Attributes.read(nil) }
  end

  assert('a parameter that no nonnull names takes nil') do
    assert_equal(-1, Attributes.read_or_none(nil))
  end
end
