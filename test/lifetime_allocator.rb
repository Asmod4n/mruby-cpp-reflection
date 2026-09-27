if Object.const_defined?(:CLibrary)
  assert('a function that makes a handle of an undeclared lifetime raises NotImplementedError') do
    # No declaration says who frees an Unknown, so the gem does not call
    # the function at all.
    assert_raise(NotImplementedError) { CLibrary.unknown_make }
  end

  CLibrary::Handle.lifetime do
    allocator :handle_open, output_parameter: :made
    allocator :handle_make
    allocator :handle_popen
    deallocator :handle_close, results_of: [:handle_open, :handle_make]
    deallocator :handle_pclose, results_of: :handle_popen
    errors :handle_open, success: 0
  end

  def dropped_handles
    CLibrary.handle_open("abc", nil)
    CLibrary.handle_make(4)
    CLibrary.handle_popen(5)
    nil
  end

  assert('an allocator answers a handle that the deallocator frees once') do
    full_gc
    before = CLibrary.handles_alive
    h = CLibrary.handle_make(7)
    assert_kind_of CLibrary::Handle, h
    assert_equal 7, CLibrary.handle_value(h)
    assert_equal before + 1, CLibrary.handles_alive
    h = nil
    full_gc
    assert_equal before, CLibrary.handles_alive
  end

  assert('an output parameter gives the handle, and Ruby passes nil for it') do
    h = CLibrary.handle_open("abc", nil)
    assert_equal 3, CLibrary.handle_value(h)
    assert_raise(TypeError) { CLibrary.handle_open("abc", 1) }
  end

  assert('errors raise, and a failed allocator leaves no handle') do
    full_gc
    before = CLibrary.handles_alive
    assert_raise(RuntimeError) { CLibrary.handle_open("", nil) }
    full_gc
    assert_equal before, CLibrary.handles_alive
  end

  assert('an allocator that answers a null pointer answers nil') do
    assert_nil CLibrary.handle_make(-1)
  end

  assert('results_of pairs each allocator with its deallocator') do
    # handle_pclose ends the process for a handle that handle_popen did
    # not make, and handle_close for one that it did.
    full_gc
    before = CLibrary.handles_alive
    10.times { dropped_handles }
    full_gc
    full_gc
    assert_equal before, CLibrary.handles_alive
  end

  assert('a deallocator that Ruby calls ends the lifetime of the handle') do
    full_gc
    before = CLibrary.handles_alive
    h = CLibrary.handle_make(2)
    assert_equal 0, CLibrary.handle_close(h)
    assert_equal before, CLibrary.handles_alive
    assert_raise(TypeError) { CLibrary.handle_value(h) }
    assert_raise(TypeError) { CLibrary.handle_close(h) }
    h = nil
    full_gc
    assert_equal before, CLibrary.handles_alive
  end

  assert('a handle of another class is refused') do
    assert_raise(TypeError) { CLibrary.handle_value(Object.new) }
    assert_raise(NoMethodError) { CLibrary::Handle.new }
  end

  assert('an allocator needs a deallocator') do
    assert_raise(ArgumentError) { CLibrary::Loose.lifetime { allocator :loose_make } }
    assert_raise(NotImplementedError) { CLibrary.loose_make }
  end

  assert('an allocator that gives the handle through a parameter names it') do
    assert_raise(ArgumentError) do
      CLibrary::Stray.lifetime do
        allocator :stray_open
        deallocator :stray_close
      end
    end
  end

  CLibrary::Counted.lifetime { shared_ownership increment: :counted_ref, decrement: :counted_unref }

  def dropped_counted
    CLibrary.counted_find(1)
    nil
  end

  assert('shared_ownership takes a share of a handle that no allocator made') do
    # The registry keeps one share; each Ruby object holds one more, and
    # gives it back once. counted_unref ends the process below one share.
    # Every count is read through c, which the test holds, so no object
    # that the collector may free in between is counted.
    full_gc
    c = CLibrary.counted_find(1)
    held = CLibrary.counted_count(c)
    d = CLibrary.counted_find(1)
    assert_equal held + 1, CLibrary.counted_count(c)
    d = nil
    5.times { dropped_counted }
    full_gc
    full_gc
    assert_equal held, CLibrary.counted_count(c)
  end

  assert('a decrement that Ruby calls gives back the share of that object') do
    c = CLibrary.counted_find(1)
    CLibrary.counted_unref(c)
    assert_raise(TypeError) { CLibrary.counted_count(c) }
    c = nil
    full_gc
    full_gc
    assert_true CLibrary.counted_count(CLibrary.counted_find(1)) >= 1
  end

  assert('a callback that a deallocator calls in the collector ends the process') do
    assert_true callback_in_deallocator_aborts?
  end

  assert('a callback that a deallocator calls at mrb_close reaches no Ruby') do
    assert_true watched_handle_freed_at_close?
  end
end
