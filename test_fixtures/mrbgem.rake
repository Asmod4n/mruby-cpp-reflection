MRuby::Gem::Specification.new('mruby-cpp-reflection-test_fixtures') do |spec|
  spec.license = 'MPL-2'
  spec.authors = 'Hendrik Beskow'
  spec.version = '0.1.0'
  spec.summary = 'The C++ classes and declarations that the tests of mruby-cpp-reflection reflect'
  spec.add_dependency 'mruby-cpp-reflection'
  spec.reflect_varargs 'varargs::sum_ints', [%w[int], %w[int int], %w[int int int]]
  spec.reflect_varargs 'varargs::describe', [['int', 'double', 'const char *']]
  spec.reflect_object_lifetime 'TreeObject' do
    takes_ownership :set_parent, by: 0
    takes_ownership :initialize, by: :parent
    ends_lifetime :destroy, 0
  end
  spec.reflect_object_lifetime('Resource') { ends_lifetime :destroy, 0 }
  spec.reflect_object_lifetime('Window') { retains :set_layout, 0 }
  spec.reflect_object_lifetime 'Device' do
    errors :open, error: :negative, sets_errno: true
    errors :status, success: 0
    errors :find, error: nil
    threadsafe :start, :no
  end
  spec.reflect_object_lifetime 'Deep' do
    stack_reserve :depth, 1 << 60
    stack_reserve :shallow, 1024
  end
  spec.reflect_object_lifetime('Adopter') { takes_ownership :adopt, of: 0 }
  spec.reflect_object_lifetime 'c_library::handle' do
    allocator :handle_open, output_parameter: :made
    allocator :handle_make
    allocator :handle_popen
    deallocator :handle_close, results_of: %i[handle_open handle_make]
    deallocator :handle_pclose, results_of: :handle_popen
    errors :handle_open, success: 0
  end
  spec.reflect_object_lifetime('c_library::counted') { shared_ownership increment: :counted_ref, decrement: :counted_unref }
  reflect_presyms(spec, "#{spec.dir}/tools/reflect_presyms/reflect_presyms.cpp")
  reflect_virtual_overriders(spec, 'src/reflection_tests.cpp')
end
