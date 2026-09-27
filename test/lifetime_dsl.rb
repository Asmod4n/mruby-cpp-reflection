if Object.const_defined?(:TreeObject)
  # An object that Ruby made before the declaration was made with the
  # allocator of mruby, so C++ may not delete it later.
  $made_before_declaration = TreeObject.new

  TreeObject.lifetime do
    owns :set_parent, owner: 0
    owns :initialize, owner: :owner
    ends_lifetime :destroy, 0
  end
  Resource.lifetime { ends_lifetime :destroy, 0 }
  Window.lifetime { retains :set_layout, 0 }
  Device.lifetime do
    errors :open, error: :negative, sets_errno: true
    errors :status, success: 0
    errors :find, error: nil
    threadsafe :start, :no
  end
  Deep.lifetime do
    stack_reserve :depth, 1 << 60
    stack_reserve :shallow, 1024
  end

  def tree_child_of_dropped_owner
    owner = TreeObject.new
    child = TreeObject.new
    child.set_parent(owner)
    child
  end

  def tree_dropped_pair
    owner = TreeObject.new
    TreeObject.new.set_parent(owner)
    nil
  end

  assert('without a lifetime declaration, an owner and Ruby delete the same child') do
    # The model checker found this trace; the child process ends with a
    # double free, which shows what owns prevents.
    assert_true undeclared_tree_fails?
  end

  assert('owns keeps the owner while Ruby holds the child') do
    full_gc
    before = TreeObject.alive
    child = tree_child_of_dropped_owner
    full_gc
    assert_equal before + 2, TreeObject.alive
    assert_equal 1, child.value
    child = nil
    full_gc
    full_gc
    assert_equal before, TreeObject.alive
  end

  assert('owns lets C++ delete a child once, in the order the GC frees') do
    full_gc
    before = TreeObject.alive
    20.times { tree_dropped_pair }
    full_gc
    full_gc
    assert_equal before, TreeObject.alive
  end

  assert('owns follows the constructor that takes an owner') do
    full_gc
    before = TreeObject.alive
    owner = TreeObject.new
    TreeObject.new(owner)
    assert_equal 1, owner.child_count
    owner = nil
    full_gc
    full_gc
    assert_equal before, TreeObject.alive
  end

  assert('a nil owner gives the object back to Ruby') do
    full_gc
    before = TreeObject.alive
    owner = TreeObject.new
    child = TreeObject.new
    child.set_parent(owner)
    child.set_parent(nil)
    assert_equal 0, owner.child_count
    owner = nil
    child = nil
    full_gc
    full_gc
    assert_equal before, TreeObject.alive
  end

  assert('a second owner takes the object from the first') do
    full_gc
    before = TreeObject.alive
    first = TreeObject.new
    second = TreeObject.new
    child = TreeObject.new
    child.set_parent(first)
    child.set_parent(second)
    assert_equal [0, 1], [first.child_count, second.child_count]
    first = second = child = nil
    full_gc
    full_gc
    assert_equal before, TreeObject.alive
  end

  assert('owns refuses a link that makes an object own itself') do
    owner = TreeObject.new
    child = TreeObject.new
    child.set_parent(owner)
    assert_raise(ArgumentError) { owner.set_parent(child) }
    assert_raise(ArgumentError) { owner.set_parent(owner) }
    assert_equal 0, child.child_count
  end

  assert('owns refuses an object made before the declaration') do
    assert_raise(TypeError) { $made_before_declaration.set_parent(TreeObject.new) }
  end

  assert('the end of an owner ends the lifetime of what it owns') do
    full_gc
    before = TreeObject.alive
    owner = TreeObject.new
    child = TreeObject.new
    grandchild = TreeObject.new
    child.set_parent(owner)
    grandchild.set_parent(child)
    TreeObject.destroy(owner)
    assert_raise(TypeError) { owner.value }
    assert_raise(TypeError) { child.value }
    assert_raise(TypeError) { grandchild.value }
    owner = child = grandchild = nil
    full_gc
    assert_equal before, TreeObject.alive
  end

  assert('mrb_close deletes owned objects once, children first') do
    assert_true tree_freed_at_close_with_owns?
  end

  assert('ends_lifetime: after the call nothing reaches the object') do
    full_gc
    before = Resource.alive
    r = Resource.new
    Resource.destroy(r)
    assert_equal before, Resource.alive
    assert_raise(TypeError) { r.value }
    assert_raise(TypeError) { Resource.destroy(r) }
    r = nil
    full_gc
    assert_equal before, Resource.alive
  end

  assert('retains keeps an argument while the receiver lives') do
    w = Window.new
    w.set_layout(Layout.new)
    full_gc
    full_gc
    assert_equal 3, w.layout_value
  end

  assert('retains holds one object once, however often it is passed') do
    w = Window.new
    layout = Layout.new
    100.times { w.set_layout(layout) }
    assert_equal 1, retained_count(w)
    w.set_layout(Layout.new)
    assert_equal 2, retained_count(w)
  end

  assert('errors: a negative answer raises the errno the function set') do
    d = Device.new
    assert_equal 3, d.open(0)
    assert_raise(SystemCallError) { d.open(-1) }
  end

  assert('errors: an answer other than success raises RuntimeError') do
    d = Device.new
    assert_equal 0, d.status(0)
    assert_raise(RuntimeError) { d.status(1) }
  end

  assert('errors: a null pointer raises RuntimeError') do
    d = Device.new
    assert_kind_of Device, d.find(true)
    assert_raise(RuntimeError) { d.find(false) }
  end

  assert('threadsafe :no takes a function whose arguments are copies') do
    assert_equal 4, Device.new.start(4)
  end

  assert('threadsafe :no refuses a function that takes a callback') do
    assert_raise(ArgumentError) { Worker.lifetime { threadsafe :watch, :no } }
  end

  assert('stack_reserve raises when the thread stack has less left') do
    assert_raise(SystemStackError) { Deep.new.depth }
    assert_equal 2, Deep.new.shallow
  end

  assert('a class declares its lifetime once') do
    assert_raise(FrozenError) { TreeObject.lifetime { } }
    subclass = Class.new(TreeObject)
    assert_raise(FrozenError) { subclass.lifetime { } }
  end

  assert('the declaration is closed after its block') do
    kept = nil
    Blank.lifetime { kept = self }
    assert_raise(FrozenError) { kept.retains(:n, 0) }
  end

  assert('a declaration names a reflected method of the class') do
    assert_raise(NameError) { Empty.lifetime { owns :nowhere, owner: 0 } }
  end
end
