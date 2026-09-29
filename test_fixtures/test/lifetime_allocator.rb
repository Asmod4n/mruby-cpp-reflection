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
    # owner: names a function that answers the object that holds the
    # part, so it takes no argument and returns a pointer to a class.
    assert_true errors['owner_takes_argument'].include?('current_settings names as owner: no function that takes no argument and returns a pointer to a class')
    assert_nil errors['right_owner']
    # A borrowed data member names the class that holds it as its owner,
    # and holds a pointer to a class.
    assert_true errors['field_owner_elsewhere'].include?('fonts is a data member, and owner: names no class that holds it and declares it')
    assert_true errors['field_holds_no_pointer'].include?('frame is a data member that holds no pointer to a class')
    assert_nil errors['right_field']
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

  assert('a lifetime names functions of a namespace for a class outside it') do
    # The allocator and the deallocator of c_library_context are in
    # namespace c_library, and the class is at global scope.
    full_gc
    before = CLibrary.contexts_alive
    c = CLibrary.create_context
    assert_equal before + 1, CLibrary.contexts_alive
    c = nil
    full_gc
    assert_equal before, CLibrary.contexts_alive
  end
  assert('a borrowed result is the Ruby object that owns the C++ object') do
    # current_context returns the context that create_context made, and
    # the caller does not free it. The gem answers the object it already
    # tracks, so no second Ruby object points at the context.
    c = CLibrary.create_context
    assert_same c, CLibrary.current_context
    assert_same c, CLibrary.current_context
    CLibrary.destroy_context(c)
  end

  assert('a borrowed result after the deallocator is what the C++ returns') do
    # destroy_context ends the record of the context, and the library
    # forgets its current context, so current_context returns NULL.
    c = CLibrary.create_context
    CLibrary.destroy_context(c)
    assert_nil CLibrary.current_context
  end

  assert('a borrowed reference with an owner is a mutable part of the object of the owner') do
    # current_settings answers a reference into the current context, which
    # C++ does not copy. The Ruby object changes the settings in place, is
    # the same object on each call, and ends with the context.
    c = CLibrary.create_context
    s = CLibrary.current_settings
    assert_false s.frozen?
    s.width = 800
    assert_equal 800, CLibrary.current_width
    assert_same s, CLibrary.current_settings
    CLibrary.destroy_context(c)
    assert_raise(TypeError) { s.width }
  end

  assert('a borrowed reference keeps the Ruby object of its owner') do
    # The settings are memory of the context, so the context stays while
    # a Ruby object of its settings stays.
    full_gc
    before = CLibrary.contexts_alive
    c = CLibrary.create_context
    s = CLibrary.current_settings
    c = nil
    full_gc
    assert_equal before + 1, CLibrary.contexts_alive
    s.width = 3
    assert_equal 3, CLibrary.current_width
    s = nil
    full_gc
    assert_equal before, CLibrary.contexts_alive
  end

  assert('a borrowed reference raises when no Ruby object owns its owner') do
    # With no current context, current_settings reads through a null
    # pointer, so the gem raises before it calls the function.
    CLibrary.destroy_context(CLibrary.create_context)
    assert_nil CLibrary.current_context
    e = assert_raise(TypeError) { CLibrary.current_settings }
    assert_include e.message, 'current_context'
  end

  assert('a borrowed pointer field is the object it points to, changed in place') do
    # io.fonts points to an atlas that C++ keeps, as ImGuiIO::Fonts does.
    # Without the declaration the field would be a frozen copy, and a
    # change would never reach the atlas.
    io = CLibrary::Io.new
    assert_nil io.fonts
    CLibrary.io_use_atlas(io, 1)
    fonts = io.fonts
    assert_false fonts.frozen?
    fonts.width = 640
    assert_equal 640, CLibrary.atlas_width(1)
    assert_same fonts, io.fonts
  end

  assert('a borrowed pointer field is read again at each access') do
    # The field can point to another atlas or to none between two reads.
    # The object of the old atlas ends, so it cannot reach the old memory.
    io = CLibrary::Io.new
    CLibrary.io_use_atlas(io, 1)
    first = io.fonts
    CLibrary.io_use_atlas(io, 2)
    second = io.fonts
    assert_not_same first, second
    second.width = 7
    assert_equal 7, CLibrary.atlas_width(2)
    assert_raise(TypeError) { first.width }
    CLibrary.io_use_atlas(io, 0)
    assert_nil io.fonts
  end

  assert('a borrowed pointer field keeps the Ruby object that holds it') do
    # The object of the atlas keeps the io that points to it, so the io
    # stays while Ruby holds the atlas.
    io = CLibrary::Io.new
    CLibrary.io_use_atlas(io, 1)
    fonts = io.fonts
    io = nil
    full_gc
    fonts.width = 3
    assert_equal 3, CLibrary.atlas_width(1)
  end

  assert('a borrowed pointer to an object that the gem does not track raises') do
    # A Ruby object for static_context would own nothing and could
    # outlive the C++ object, so the call raises.
    e = assert_raise(TypeError) { CLibrary.static_context }
    assert_include e.message, 'static_context'
  end
end
