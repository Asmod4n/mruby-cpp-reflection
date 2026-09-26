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
  assert_equal(5, Z.new(5).v)
  assert_raise(ArgumentError) { Z.new }
  assert_raise(ArgumentError) { S.new(1, 2, 3, 4) }
end

assert('a parameter takes what a converting constructor takes') do
  f = F.new
  assert_equal(7, f.f(7))
  assert_equal(6, f.f('Jessie'))
  assert_equal(7, f.g(7))
  assert_equal(6, f.g('Jessie'))
  assert_equal(3, f.g(X.new(3)))
  assert_equal(9, f.g(X.new('abc', 6)))
  assert_raise(TypeError) { f.h(5) }
  assert_equal(5, f.h(Z.new(5)))
  assert_raise(TypeError) { f.y(5) }
  assert_equal(5, f.y(Y.new(X.new(5))))
  assert_equal(5, f.y(Y.new(5)))
end

assert('a member operator is the Ruby method of the same sign') do
  two = Operand.new(2)
  three = Operand.new(3)
  assert_equal(5, (two + three).v)
  assert_equal(5, (two + 3).v)
  assert_equal(-1, (two - three).v)
  assert_equal(-2, (-two).v)
  assert_equal(6, (two * three).v)
  assert_equal(8, (two << 2).v)
  assert_true(two == Operand.new(2))
  assert_false(two != Operand.new(2))
  assert_true(two < three)
  assert_equal(-1, two <=> three)
  assert_equal(0, two <=> Operand.new(2))
  assert_equal(1, three <=> two)
  assert_true(!Operand.new(0))
  assert_false(!two)
  assert_equal(2, two[0])
  two[0] = 9
  assert_equal(9, two.v)
  assert_equal(27, two.(3))
  assert_equal(27, two.call(3))
  assert_false(Operand.method_defined?(:'+='))
end

assert('a static member is a class method') do
  assert_equal(6, Static.twice(3))
  assert_equal(12, Static.twice(2, 3))
  assert_equal(4, Static.make(4).v)
  assert_equal(10, Static.limit)
  assert_false(Static.respond_to?(:limit=))
  assert_false(Static.new.respond_to?(:twice))
  assert_equal(1, Static.count)
  Static.count = 5
  assert_equal(5, Static.count)
  assert_equal(5, Static.read_count)
end

assert('a C++ exception is the Ruby exception for it') do
  t = Thrower.new
  assert_equal('bad argument', assert_raise(ArgumentError) { t.invalid }.message)
  assert_equal(2, t.at(1))
  assert_raise(IndexError) { t.at(5) }
  assert_equal('too big', assert_raise(RangeError) { t.overflow }.message)
  assert_equal('broken', assert_raise(RuntimeError) { t.runtime }.message)
  assert_equal('negative', assert_raise(ArgumentError) { Thrower.new(-1) }.message)
  assert_equal(2, Thrower.new(1).at(1))
  assert_raise(RuntimeError) { t.number }
  assert_raise(NoMemoryError) { t.memory }
  assert_equal('domain', assert_raise(FloatDomainError) { t.domain }.message)
  assert_equal('length', assert_raise(IndexError) { t.length }.message)
  assert_raise(Errno::ENOENT) { t.system }
  assert_raise(RegexpError) { t.regex }
  assert_raise(Object.const_defined?(:IOError) ? Object.const_get(:IOError) : Errno::ENOENT) { t.filesystem }
end

assert('a second mrb_state gets its own classes') do
  assert_equal("[4, 2, 7, #{D.ancestors.size}, 6]", second_state)
  assert_equal(4, D.new.f)
end

assert('a thread cancelled inside a reflected call ends as cancelled') do
  assert_true(cancelled_through_a_call?)
end

class CallableForTest
  def call(n)
    n - 1
  end
end

assert('a std::function parameter takes anything that answers call') do
  c = Callback.new
  assert_equal(6, c.apply(->(n) { n * 2 }, 3))
  assert_equal(6, c.apply(proc { |n| n * 2 }, 3))
  assert_equal(2, c.apply(CallableForTest.new, 3))
  assert_equal(12, c.apply(Operand.new(4), 3))
  assert_equal(7, c.each_twice(3) { |n| n + 2 })
  assert_raise(TypeError) { c.apply(5, 3) }
end

assert('a callback C++ keeps survives a full collection') do
  c = Callback.new
  c.keep(->(n) { n * 10 })
  full_gc
  assert_equal(30, c.call_kept(3))
end

assert('a std::function or a lambda from C++ answers call and to_proc') do
  c = Callback.new
  times = c.times(3)
  assert_equal(12, times.call(4))
  assert_equal(12, times.(4))
  assert_equal([3, 6], [1, 2].map(&times))
  assert_equal(1, times.to_proc.arity)
  assert_true(times.to_proc.lambda?)
  plus = c.plus(1)
  assert_equal(5, plus.call(4))
  assert_equal([2, 3], [1, 2].map(&plus))
  assert_equal(15, c.apply(times, 5))
end

assert('a std::function made from Ruby goes back as the object it was made from') do
  c = Callback.new
  l = ->(n) { n }
  c.keep(l)
  assert_same(l, c.given)
end

