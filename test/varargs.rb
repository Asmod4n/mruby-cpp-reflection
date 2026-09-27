if Object.const_defined?(:Varargs)
  assert('each declared list of trailing types is one call') do
    assert_equal 0, Varargs.sum_ints(0, 0)
    assert_equal 3, Varargs.sum_ints(2, 1, 2)
    assert_equal 6, Varargs.sum_ints(3, 1, 2, 3)
  end

  assert('a count of arguments that no list declares is refused') do
    # va_arg would read an argument that was never passed.
    assert_raise(ArgumentError) { Varargs.sum_ints(4, 1, 2, 3, 4) }
    assert_raise(ArgumentError) { Varargs.sum_ints(0) }
  end

  assert('the trailing types convert as the parameters of a function do') do
    assert_equal 1 + 2 + 0.5 + 3, Varargs.describe(1, 2, 0.5, "abc")
    assert_raise(TypeError) { Varargs.describe(1, 2, 0.5, 3) }
  end

  assert('a function whose trailing types are not declared raises NotImplementedError') do
    assert_raise(NotImplementedError) { Varargs.undeclared(1) }
  end
end
