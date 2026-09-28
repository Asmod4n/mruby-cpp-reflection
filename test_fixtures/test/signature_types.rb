if Object.const_defined?(:SignatureTypes)
  assert('a class listed with its signature types is reflected with its destructor skipped') do
    # The list failed to compile before: it asked the destructor for a return type.
    p = SignatureTypes::Point.new
    p.x = 3
    assert_equal 3, p.x
  end
end
