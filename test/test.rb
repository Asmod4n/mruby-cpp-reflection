if Object.const_defined?(:Reflected)
  assert("a reflected C++ class answers from Ruby") do
    r = Reflected.new
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
    # A std::string parameter takes a Ruby String as a copy and a
    # Std::String as itself. assign is the way back from a Ruby String.
    assert_equal(3, r.length_of('abc'))
    assert_equal(1, r.length_of(r.label))
    assert_raise(TypeError) { r.length_of(1) }
    assert_equal('q', r.echo('q').to_s)
    assert_equal('l', r.label.to_s)
    copy = r.label.to_s
    copy += 'x'
    assert_equal('l', r.label.to_s)
    r.label.assign(copy)
    assert_equal('lx', r.label.to_s)
    assert_equal(2, r.length_of(r.label))
    # replace is operator=: the argument is the same class as itself, or a
    # Ruby value as a copy. A const object raises, a wrong type raises.
    assert_equal([1, 2], r.seen.replace([1, 2]).to_a)
    source = Reflected.new
    source.add(7)
    assert_equal([7], r.seen.replace(source.history).to_a)
    assert_equal('y', r.label.replace('y').to_s)
    assert_raise(FrozenError) { r.history.replace([]) }
    assert_raise(TypeError) { r.seen.replace('no') }
    other = Reflected.new
    other.replace(r)
    assert_equal('y', other.label.to_s)
    assert_equal(r.total, other.total)
    assert_equal('Std::Vector', r.seen.class.to_s)
    assert_equal(1, Under::Plain.new.n)
    assert_true(reflect_presym_ok?)
  end
end

# C++ builds D from its bases in the order they are declared, then D
# itself. mruby looks methods up the other way round. Read from the
# class back to Object, the classes behind D::InstanceMethods and the
# modules it includes must name the same order C++ constructed in.
assert('the ancestors of a reflected class are the order C++ constructs it in') do
  constructed
  D.new
  built = constructed
  looked_up = D.ancestors.map { |m| m.to_s.delete_suffix('::InstanceMethods') }.select { |n| built.include?(n) }.uniq
  assert_equal(%w[A B C D], built)
  assert_equal(built.reverse, looked_up)
end

assert('a reflected class answers is_a? for every C++ base') do
  d = D.new
  assert_equal(A, D.superclass)
  %w[A B C].each { |n| assert_true(d.is_a?(Object.const_get(n)::InstanceMethods)) }
  assert_true(d.is_a?(A))
end

assert('a method belongs to the class C++ declares it in') do
  d = D.new
  assert_equal(4, d.f)
  assert_equal(1, d.a)
  assert_equal(2, d.b)
  assert_equal(3, d.c)
  assert_equal(2, d.b_of(d))
  d.b = 5
  assert_equal(5, d.b_of(d))
  assert_raise(TypeError) { d.b_of(A.new) }
end

assert('initialize takes every public constructor') do
  assert_equal([0, 'S()'], [S.new.v, S.new.from.to_s])
  assert_equal([7, 'S(mrb_int)'], [S.new(7).v, S.new(7).from.to_s])
  assert_equal([16, 'S(mrb_int, mrb_int, mrb_int)'], [S.new(2, 3).v, S.new(2, 3).from.to_s])
  assert_equal(7, S.new(2, 3, 1).v)
  assert_equal([3, 'S(std::string_view)'], [S.new('abc').v, S.new('abc').from.to_s])
  assert_equal(5, X.new(5).v)
  assert_raise(ArgumentError) { X.new }
  assert_raise(ArgumentError) { S.new(1, 2, 3, 4) }
end

