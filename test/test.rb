if Object.const_defined?(:CPP) && CPP.const_defined?(:Reflected)
  assert("a reflected C++ class answers from Ruby") do
    r = CPP::Reflected.new
    assert_true(r.same('abc', 3))
    assert_false(r.same('ab', 3))
    assert_equal(10, r.same(5))
    assert_raise(ArgumentError) { r.same }
    assert_equal(3, r.rest(1, 2, 3))
    assert_equal(1, r.rest(1))
    assert_nil(r.add(4))
    assert_equal(10, r.sum)
    assert_equal(10, r.scaled_by(5))
    assert_equal(15, r.scaled_by(5, 3))
    assert_raise(ArgumentError) { r.scaled_by }
    assert_equal('reflected', r.name.to_s)
    assert_equal(10, r.total)
    r.total = 3
    assert_equal(3, r.total)
    assert_equal([4], r.history.to_a)
    assert_true(r.history.frozen?)
    assert_raise(FrozenError) { r.history.push_back(1) }
    assert_equal(2, r.count([1, 2]))
    assert_equal(1, r.count(r.history))
    r.seen.push_back(9)
    assert_equal([4, 9], r.seen.to_a)
    assert_true(reflect_presym_ok?)
  end
end
