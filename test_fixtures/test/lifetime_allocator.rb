if Object.const_defined?(:CLibrary)
  assert('the build refuses a function that makes a handle of an undeclared lifetime') do
    # No declaration says who frees an unknown, a loose or a stray handle,
    # so the build stops where such a function is reflected, and the
    # message names the function, where the handle comes from, and the
    # declaration that is missing.
    errors = missing_lifetime_errors
    assert_true errors['unknown_make'].include?('unknown_make')
    assert_true errors['unknown_make'].include?('the result gives a pointer to c_library_undeclared::unknown')
    assert_true errors['unknown_make'].include?("spec.reflect_object_lifetime 'c_library_undeclared::unknown' needs allocator :unknown_make and a deallocator, or shared_ownership")
    # The message ends with the missing declaration in the form of a
    # lifetime file, so that a user can paste it, check the marked word
    # against the library, and send it to this repository.
    assert_true errors['unknown_make'].include?("send the block as lifetimes/<package>/<version>.lifetime:\n")
    assert_true errors['unknown_make'].end_with?("reflect_object_lifetime 'c_library_undeclared::unknown' do\n  allocator :unknown_make\n  deallocator :CHECK\nend\n")
    assert_true errors['stray_open'].include?("do\n  allocator :stray_open, output_parameter: :made\n  deallocator :CHECK\nend\n")
    assert_true errors['loose_make'].include?('allocator :loose_make')
    assert_true errors['stray_open'].include?('parameter 0 (made) gives a pointer to c_library_undeclared::stray')
    assert_true errors['stray_open'].include?('allocator :stray_open, output_parameter: :made')
    assert_nil errors['handle_make']
    assert_nil errors['handle_open']
    assert_nil errors['counted_find']
    assert_nil errors['handle_value']
  end

  # lifetime_allocator_library.hpp declares the allocators, the
  # deallocators and the shared ownership of these handles.

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

  assert('the compile refuses an allocator or deallocator that the types contradict') do
    # Each entry is the text the compiler prints for one wrong
    # declaration; nil is a declaration that compiles.
    errors = allocator_declaration_errors
    assert_true errors['unnamed_output'].include?('stray_open gives the object through a parameter, which output_parameter: names')
    assert_true errors['wrong_output'].include?('handle_open has no pointer to a pointer to the class where output_parameter: points')
    assert_true errors['makes_nothing'].include?('handle_value makes no object of the class')
    assert_true errors['frees_nothing'].include?('handle_make takes no pointer to the class as its only parameter')
    assert_true errors['no_deallocator'].include?('loose_make is an allocator without a deallocator')
    assert_true errors['no_decrement'].include?('counted_find takes no pointer to the class as its only parameter')
    assert_nil errors['right']
  end

  assert('Ruby cannot declare the lifetime of a handle') do
    [CLibrary::Handle, CLibrary::Counted].each do |klass|
      assert_false klass.respond_to?(:lifetime)
    end
  end

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
