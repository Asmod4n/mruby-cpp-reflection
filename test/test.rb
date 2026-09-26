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
    copy = r.history
    assert_false(copy.frozen?)
    copy.push_back(1)
    assert_equal([4], r.history.to_a)
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
    assert_equal([], r.history.replace([]).to_a)
    assert_equal([7], r.history.to_a)
    assert_raise(TypeError) { r.seen.replace('no') }
    other = Reflected.new
    other.replace(r)
    assert_equal('y', other.label.to_s)
    assert_equal(r.total, other.total)
    assert_equal('Std::Vector[:long]', r.seen.class.to_s)
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

# A void pointer has no type that Ruby can check, so Ruby gets an object
# that holds the address and nothing that reads the memory behind it. A
# const void pointer is a class of its own, so a C++ function that takes
# a void pointer cannot be given one that C++ made const.
# A const on the pointer itself makes a field read only and changes
# nothing else.
assert('a void pointer crosses as VoidPointer or ConstVoidPointer') do
  c = Callback.new
  seen = []
  assert_true(c.compare(->(left, right) { seen << left << right; true }))
  left, right = seen
  assert_equal(ConstVoidPointer, left.class)
  assert_equal(VoidPointer, right.class)
  assert_equal(left.address, right.address)
  assert_equal(left.address, left.to_i)
  assert_true(left == right)
  assert_true(right.dup == right)
  assert_true(c.same(left, right))
  assert_true(c.same(right, c.address.call(right)))
  assert_nil(c.address.call(nil))
  assert_raise(TypeError) { c.address.call(left) }
  assert_raise(TypeError) { c.same(1, left) }
  assert_equal(ConstVoidPointer, c.fixed.class)
  assert_true(c.same(c.fixed, c.where))
  assert_nil(c.place)
  c.place = right
  assert_true(c.same(c.place, right))
  assert_raise(TypeError) { c.place = left }
  assert_false(c.respond_to?(:where=))
  Callback.anywhere = right
  assert_true(c.same(Callback.anywhere, right))
  Callback.anywhere = nil
  assert_nil(Callback.anywhere)
  assert_raise(NoMethodError) { VoidPointer.new }
  assert_raise(NoMethodError) { ConstVoidPointer.new }
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

def node_tree_without_a_reference
  root = Node.new
  Node.new(root)
  Node.new(root)
  nil
end

assert('an object with a parent lives as long as its parent') do
  full_gc
  base = Node.alive
  root = Node.new
  child = Node.new
  child.set_parent(root)
  child.instance_variable_set(:@tag, 5)
  child = nil
  full_gc
  assert_equal(base + 2, Node.alive)
  assert_equal(5, root.child_at(0).instance_variable_get(:@tag))
  assert_same(root.child_at(0), root.child_at(0))
  Node.new(root)
  full_gc
  assert_equal(2, root.child_count)
  assert_equal(base + 3, Node.alive)
end

assert('an object without a parent is freed with its children') do
  full_gc
  base = Node.alive
  node_tree_without_a_reference
  full_gc
  assert_equal(base, Node.alive)
end

def node_child_of_a_dropped_root
  Node.new(Node.new)
end

assert('a Ruby object whose C++ object C++ deleted raises when used') do
  root = Node.new
  child = Node.new(root)
  root.delete_child(0)
  assert_equal(0, root.child_count)
  assert_raise(TypeError) { child.child_count }
  orphan = node_child_of_a_dropped_root
  full_gc
  assert_raise(TypeError) { orphan.child_count }
end

def share_made_and_dropped(sharer)
  sharer.make(1)
  nil
end

assert('an object in a std::shared_ptr lives as long as Ruby holds it') do
  full_gc
  base = Share.alive rescue 0
  sharer = Sharer.new
  kept = sharer.keep(7)
  assert_true(sharer.same(kept))
  sharer.drop
  full_gc
  assert_equal(7, kept.v)
  assert_equal(base + 1, Share.alive)
  assert_equal(2, sharer.count(kept))
  kept = nil
  share_made_and_dropped(sharer)
  full_gc
  assert_equal(base, Share.alive)
end

assert('a pointer to an object that shares from itself keeps a share') do
  full_gc
  base = Share.alive
  sharer = SelfSharer.new
  raw = sharer.raw
  ref = sharer.ref
  sharer.drop
  full_gc
  assert_equal(9, raw.v)
  assert_equal(9, ref.v)
  assert_equal(base + 1, Share.alive)
  raw = nil
  ref = nil
  sharer = nil
end

assert('an object C++ deleted raises through its guard') do
  holder = WatchedHolder.new
  watched = holder.get
  assert_equal(3, watched.v)
  holder.reset
  assert_raise(TypeError) { watched.v }
end

assert('a reference from a method is a copy, and a field is lent') do
  lender = Lender.new
  view = lender.view
  lender.add(3)
  assert_equal([1, 2], view.to_a)
  assert_false(view.frozen?)
  assert_equal([1, 2, 3], lender.items.to_a)
  assert_raise(TypeError) { lender.alone }
end

assert('a field goes with the object it belongs to') do
  root = Node.new
  child = Node.new(root)
  tag = child.tag
  assert_equal(1, tag.n)
  root.delete_child(0)
  assert_raise(TypeError) { tag.n }
end

assert('dup and clone copy the C++ object with its copy constructor') do
  s = S.new(7)
  d = s.dup
  d.v = 1
  assert_equal([7, 1], [s.v, d.v])
  s.freeze
  assert_true(s.clone.frozen?)
  assert_false(s.dup.frozen?)
  assert_equal(7, s.clone.v)
  assert_raise(TypeError) { Node.new.dup }
end

# std::vector is a template, not a type, so Std::Vector is a module;
# each specialization is a class of its own that includes it, reached
# with its template arguments: a class type as its class, any other
# type as the symbol C++ spells it with. An argument with a default may
# be left out, as in C++, and std::string is the alias it is in C++.
assert('a template is a module and its specializations are classes') do
  r = Reflected.new
  longs = r.seen
  assert_false(Std::Vector.is_a?(Class))
  assert_true(longs.is_a?(Std::Vector))
  assert_same(Std::Vector[:long], longs.class)
  assert_same(Std::Vector[:long], Std::Vector[:long, Std::Allocator[:long]])
  assert_same(Std::String, r.label.class)
  assert_same(Std::String, Std::BasicString[:char])
  assert_true(Std::String <= Std::BasicString)
  assert_true(r.label.is_a?(Std::BasicString[:char]))
  words = r.words.class
  assert_same(Std::Vector[Std::String], words)
  assert_equal('Std::Vector[Std::String]', words.name)
  pointers = r.pointers.class
  assert_same(Std::Vector[Under::Plain::Ptr], pointers)
  const_pointers = r.const_pointers.class
  assert_same(Std::Vector[Under::Plain::ConstPtr], const_pointers)
  raw = r.raw_longs.class
  assert_same(Std::Vector[:long__ptr], raw)
  assert_raise(ArgumentError) { Std::Vector[:double] }
end


# libstdc++ declares std::allocator<char> an extern template, so its
# members come from the shared library, which was built before C++23
# added allocate_at_least. The second link points that one at
# reflect_undefined, and a call to it raises instead of failing to link.
assert('a function no linked library defines raises NotImplementedError') do
  Reflected.new.label
  assert_raise(NotImplementedError) { Std::Allocator[:char].new.allocate_at_least(1) }
end

assert('a function no library defines raises in the state that called it') do
  assert_equal(:raised_here, undefined_after_other_state)
end

assert('a namespace is a module with its free functions') do
  assert_false(FreeFunctions.is_a?(Class))
  assert_equal(6, FreeFunctions.twice(3))
  assert_equal(12, FreeFunctions.twice(2, 3))
  assert_equal(6, FreeFunctions.scaled(2))
  assert_equal(8, FreeFunctions.scaled(2, 4))
  assert_equal(5, FreeFunctions.made(5).n)
end

assert('what a GUI library brings is left out or converted as C++ allows it') do
  odd = Odd.new
  assert_equal(7, odd.constant.n)
  assert_false(Odd.method_defined?(:gone))
  assert_false(Odd.method_defined?(:==) && Odd.instance_method(:==).owner != Kernel && Odd.instance_method(:==).owner != BasicObject)
  assert_false(Odd.method_defined?(:opaque))
  assert_equal({'a' => 1, 'b' => 2}, odd.pairs.to_h.to_a.map { |k, v| [k.to_s, v] }.to_h)
  box = odd.box
  assert_equal(2, box.get.n)
  assert_false(box.class.method_defined?(:contains))
end


# Ruby names an implicit conversion to_int or to_str, and an explicit
# one to_i or to_s. A C++ conversion function that is explicit answers
# only the explicit names.
assert('a conversion function answers the Ruby conversion of its kind') do
  c = Converts.new
  assert_equal(7, c.to_int)
  assert_equal(2.5, c.to_f)
  assert_equal("seven", c.to_str)
  assert_equal("seven", c.to_s)
  assert_equal(7, c.to_i)
  e = ConvertsExplicitly.new
  assert_equal("eight", e.to_s)
  assert_equal(8, e.to_i)
  assert_false(e.respond_to?(:to_str))
  assert_false(e.respond_to?(:to_int))
end

# A class declared inside a class is a constant of the outer class, as
# its C++ name is qualified by the outer class.
assert('a nested class is a constant of its enclosing class') do
  i = Outer.new.inner
  assert_equal(Outer::Inner, i.class)
  assert_equal("Outer::Inner", i.class.name)
  assert_equal(3, i.n)
  assert_false(Object.const_defined?(:Inner))
end

# With virtual bases a diamond holds one Top, so every path from Diamond
# to Top reaches the same object and the conversion is not ambiguous.
assert('a class with a virtual base reaches it through every path') do
  d = Diamond.new
  assert_equal(1, d.top)
  assert_equal(2, d.left)
  assert_equal(3, d.right)
  assert_kind_of(Top::InstanceMethods, d)
  assert_kind_of(RightOfDiamond::InstanceMethods, d)
  assert_equal(1, d.reach(d))
  assert_equal(3, d.reach_right(d))
  d.n = 5
  assert_equal(5, d.reach(d))
end
