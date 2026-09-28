if Object.const_defined?(:FunctionPointers)
  assert('a function pointer that C++ answers can be passed back to C++') do
    f = FunctionPointers.pick(false)
    assert_kind_of FunctionPointer, f
    assert_equal 6, FunctionPointers.apply(f, 3)
    assert_equal(-3, FunctionPointers.apply(FunctionPointers.pick(true), 3))
  end

  assert('a parameter takes only a pointer to a function of its own type') do
    # A call through a pointer of another function type is undefined, so
    # a pointer to long(long) never reaches a parameter of int(*)(int).
    assert_raise(TypeError) { FunctionPointers.apply(FunctionPointers.wide, 3) }
    assert_raise(TypeError) { FunctionPointers.apply(Object.new, 3) }
  end

  assert('a null function pointer is nil, and a parameter refuses nil') do
    # C++ would call through the null pointer.
    assert_nil FunctionPointers.none
    assert_raise(TypeError) { FunctionPointers.apply(nil, 3) }
  end

  assert('FunctionPointer has no constructor') do
    assert_raise(NoMethodError) { FunctionPointer.new }
  end
end
