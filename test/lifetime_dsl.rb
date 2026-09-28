if Object.const_defined?(:TreeObject)
  # lifetime_dsl.hpp declares the lifetimes of these classes; the tests
  # below only use the classes.

  def tree_child_of_dropped_parent
    parent = TreeObject.new
    child = TreeObject.new
    child.set_parent(parent)
    child
  end

  def tree_dropped_pair
    parent = TreeObject.new
    TreeObject.new.set_parent(parent)
    nil
  end

  assert('Ruby has no method that declares or changes a lifetime') do
    # A lifetime is declared when the gem is built. Hostile Ruby code
    # that could declare one for a class the host did not declare could
    # pair a wrong deallocator with an allocator, which is a memory fault.
    [TreeObject, Resource, Window, Device, Deep, Adopter, Blank, Empty].each do |klass|
      assert_false klass.respond_to?(:lifetime)
      assert_false klass.singleton_methods.include?(:lifetime)
    end
    %i[lifetime takes_ownership owns ends_lifetime retains errors stack_reserve threadsafe allocator deallocator shared_ownership reflect_lifetime reflect_object_lifetime].each do |word|
      assert_false Object.new.respond_to?(word, true)
      assert_false TreeObject.respond_to?(word, true)
    end
  end

  assert('without a lifetime declaration, a parent and Ruby delete the same child') do
    # The model checker found this trace; the child process ends with a
    # double free, which shows what takes_ownership prevents.
    assert_true undeclared_tree_fails?
  end

  assert('takes_ownership keeps the parent while Ruby holds the child') do
    full_gc
    before = TreeObject.alive
    child = tree_child_of_dropped_parent
    full_gc
    assert_equal before + 2, TreeObject.alive
    assert_equal 1, child.value
    child = nil
    full_gc
    full_gc
    assert_equal before, TreeObject.alive
  end

  assert('takes_ownership lets C++ delete a child once, in the order the GC frees') do
    full_gc
    before = TreeObject.alive
    20.times { tree_dropped_pair }
    full_gc
    full_gc
    assert_equal before, TreeObject.alive
  end

  assert('takes_ownership follows the constructor that takes a parent') do
    full_gc
    before = TreeObject.alive
    parent = TreeObject.new
    TreeObject.new(parent)
    assert_equal 1, parent.child_count
    parent = nil
    full_gc
    full_gc
    assert_equal before, TreeObject.alive
  end

  assert('a nil parent gives the object back to Ruby') do
    full_gc
    before = TreeObject.alive
    parent = TreeObject.new
    child = TreeObject.new
    child.set_parent(parent)
    child.set_parent(nil)
    assert_equal 0, parent.child_count
    parent = nil
    child = nil
    full_gc
    full_gc
    assert_equal before, TreeObject.alive
  end

  assert('a second parent takes ownership from the first') do
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

  assert('takes_ownership refuses a link that makes an object own itself') do
    parent = TreeObject.new
    child = TreeObject.new
    child.set_parent(parent)
    assert_raise(ArgumentError) { parent.set_parent(child) }
    assert_raise(ArgumentError) { parent.set_parent(parent) }
    assert_equal 0, child.child_count
  end

  assert('takes_ownership takes an object that Ruby made') do
    # Ruby makes an AdoptedLeaf with new, so ~Adopter may delete it. A second
    # delete from the GC shows as a double free under ASan.
    adopter = Adopter.new
    leaf = AdoptedLeaf.new
    adopter.adopt(leaf)
    assert_equal 5, leaf.n
    adopter = leaf = nil
    full_gc
    full_gc
  end

  assert('the end of an object ends the lifetime of what it took ownership of') do
    full_gc
    before = TreeObject.alive
    parent = TreeObject.new
    child = TreeObject.new
    grandchild = TreeObject.new
    child.set_parent(parent)
    grandchild.set_parent(child)
    TreeObject.destroy(parent)
    assert_raise(TypeError) { parent.value }
    assert_raise(TypeError) { child.value }
    assert_raise(TypeError) { grandchild.value }
    parent = child = grandchild = nil
    full_gc
    assert_equal before, TreeObject.alive
  end

  assert('mrb_close deletes each object once, children first') do
    assert_true tree_freed_at_close_with_takes_ownership?
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

  assert('stack_reserve raises when the thread stack has less left') do
    assert_raise(SystemStackError) { Deep.new.depth }
    assert_equal 2, Deep.new.shallow
  end

  assert('the compile refuses a declaration that the types contradict') do
    # Each entry is the text the compiler prints for one wrong
    # declaration; nil is a declaration that compiles.
    errors = object_lifetime_declaration_errors
    assert_true errors['threadsafe_callback'].include?('watch takes an argument that points into the VM')
    assert_true errors['no_function'].include?('status is no function of the class')
    assert_true errors['owns_itself'].include?('set_parent cannot make an object take ownership of itself')
    assert_true errors['no_class_there'].include?('status has no pointer or reference to a class')
    assert_true errors['no_class_named'].include?('set_layout has no pointer or reference to a class')
    assert_true errors['errors_without_number'].include?('set_layout answers no integer and no pointer')
    assert_nil errors['right']
  end

  def dropped_watcher
    Watcher.new.watch(->(x) { x })
    nil
  end

  assert('a free function releases a callback without a call into mruby') do
    # The free function of a Watcher deletes the callback it keeps. It
    # only marks the GC root of the callback, so the collection leaves
    # the number of roots alone, and the next callback that Ruby passes
    # to C++ unregisters the marked roots. The collector is off while
    # the watchers are made, so that none of them is freed before.
    kept = Watcher.new
    kept.watch(->(x) { x + 1 })
    GC.disable
    10.times { dropped_watcher }
    GC.enable
    made = callback_roots
    full_gc
    full_gc
    assert_equal made, callback_roots
    kept.watch(->(x) { x + 2 })
    assert_true callback_roots < made
  end
end
