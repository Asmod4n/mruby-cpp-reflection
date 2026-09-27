require 'shellwords'
require "#{File.dirname(__FILE__)}/tools/reflect_varargs"
require "#{File.dirname(__FILE__)}/tools/reflect_object_lifetime"
require "#{File.dirname(__FILE__)}/tools/reflect"

# Presyms for the names that C++26 reflection produces. A gem calls
#   reflect_presyms(spec, "#{spec.dir}/tools/reflect_presyms/main.cpp")
# with a program that prints mrb_cpp_reflector::reflect_presyms_header<^^T...>()
# for its reflected types. The program is built and run before the presym
# scan; its table goes to build/<name>/include/mruby/presym/reflect.h, and
# a generated source of the gem includes it, so the scanner reads the
# MRB_SYM tokens like any other source's. Nothing of it enters the tree.
def reflect_presyms(spec, runner_src)
  return unless spec.build.cxx.flags.flatten.any? { |f| f.to_s == '-freflection' }
  runner_bin = "#{spec.build_dir}/reflect_presyms/runner"
  header = "#{spec.build.build_dir}/include/mruby/presym/reflect.h"
  source = "#{spec.build_dir}/reflect_presyms/reflect_presyms.cpp"
  helpers = spec.build.gems.detect { |g| g.name == 'mruby-c-ext-helpers' }
  file runner_bin => [runner_src, "#{spec.dir}/include/mruby/reflect_presyms.hpp"] do |t|
    FileUtils.mkdir_p(File.dirname(t.name))
    incs = (spec.build.cxx.include_paths + ["#{spec.dir}/include", "#{helpers.dir}/include", "#{spec.build.build_dir}/include"]).map { |i| "-I#{i}" }.join(' ')
    sh "#{spec.build.cxx.command} #{spec.build.cxx.flags.flatten.join(' ')} #{incs} #{runner_src} -o #{t.name}"
  end
  file header => runner_bin do |t|
    FileUtils.mkdir_p(File.dirname(t.name))
    sh "#{runner_bin} > #{t.name}"
  end
  file source => header do |t|
    File.write(t.name, "#include <mruby.h>\n#include <mruby/presym/reflect.h>\n")
  end
  obj = spec.objfile(source.pathmap("#{spec.build_dir}/reflect_presyms/%n"))
  file obj => source
  spec.objs << obj
end

# Overriders for the virtual functions of the classes a source file lists
# with reflect_options virtual_overriders. A first compile of the source
# file, with MRB_CPP_REFLECTOR_GENERATE, writes the text of the overriders
# into its object file between two marker lines. That text goes to
# spec.build_dir, and include_virtual_overriders.cpp compiles the source
# file with it, in place of the source file's own object.
def reflect_virtual_overriders(spec, source)
  return unless spec.build.cxx.flags.flatten.any? { |f| f.to_s == '-freflection' }
  reflection = spec.build.gems.detect { |g| g.name == 'mruby-cpp-reflection' }
  source = File.expand_path(source, spec.dir)
  dir = "#{spec.build_dir}/virtual_overriders/#{File.basename(source, '.*')}"
  msvc = spec.build.toolchains.include?('visualcpp')
  quoted = ->(path) { msvc ? %(\\"#{path}\\") : %('"#{path}"') }
  printed = "#{dir}/print_virtual_overriders#{spec.build.exts.object}"
  text = "#{dir}/virtual_overriders.inc"
  object = "#{dir}/include_virtual_overriders#{spec.build.exts.object}"
  replaced = spec.objfile(source.relative_path_from(spec.dir).pathmap("#{spec.build_dir}/%X"))
  file printed => [source, "#{reflection.dir}/src/print_virtual_overriders.cpp", "#{reflection.dir}/include/mruby/reflection.hpp"] do |t|
    spec.cxx.run t.name, "#{reflection.dir}/src/print_virtual_overriders.cpp", ["MRB_CPP_REFLECTOR_SOURCE=#{quoted.(source)}"], [], [msvc ? '/GL-' : '-fno-lto']
  end
  file text => printed do |t|
    blocks = File.binread(printed).scan(/BEGIN_MRB_CPP_REFLECTOR_VIRTUAL_OVERRIDERS\n(.*?)END_MRB_CPP_REFLECTOR_VIRTUAL_OVERRIDERS\n/m).flatten
    File.write(t.name, blocks.flat_map { |b| b.split(/^(?=template <> struct)/) }.uniq.join)
  end
  file object => [text, "#{reflection.dir}/src/include_virtual_overriders.cpp"] do |t|
    spec.cxx.run t.name, "#{reflection.dir}/src/include_virtual_overriders.cpp",
                 ["MRB_CPP_REFLECTOR_SOURCE=#{quoted.(source)}", "MRB_CPP_REFLECTOR_VIRTUAL_OVERRIDERS=#{quoted.(text)}"]
  end
  if spec.test_objs.include?(replaced)
    spec.test_objs = spec.test_objs.map { |o| o == replaced ? object : o }
  else
    spec.objs = spec.objs.map { |o| o == replaced ? object : o }
  end
end

MRuby::Gem::Specification.new('mruby-cpp-reflection') do |spec|
  spec.export_include_paths << "#{spec.dir}/include" if spec.respond_to?(:export_include_paths)
  spec.license = 'MPL-2'
  spec.authors = 'Hendrik Beskow'
  spec.version = '0.1.0'
  spec.summary = 'A C++ class is a Ruby class: C++26 reflection defines it, methods, attributes, overloads and all'
  spec.add_dependency 'mruby-proc-ext', core: 'mruby-proc-ext'
  spec.add_dependency 'mruby-c-ext-helpers', github: 'Asmod4n/mruby-c-ext-helpers', branch: 'mrb-value-to'
  spec.add_test_dependency 'mruby-string-ext', core: 'mruby-string-ext'
  spec.add_test_dependency 'mruby-errno', core: 'mruby-errno'
  spec.add_test_dependency 'mruby-metaprog', core: 'mruby-metaprog'
  spec.add_test_dependency 'mruby-class-ext', core: 'mruby-class-ext'
  spec.add_test_dependency 'mruby-method', core: 'mruby-method'
  spec.add_test_dependency 'mruby-enumerator', core: 'mruby-enumerator'
  if spec.build.test_enabled?
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
  end
  reflect_presyms(spec, "#{spec.dir}/test/reflect_presyms/main.cpp")
  reflect_virtual_overriders(spec, 'test/reflection_tests.cpp')
  spec.build.enable_cxx_exception
  relink = "#{spec.dir}/tools/relink.rb"
  spec.build.linker.command = %("#{RbConfig.ruby}" "#{relink}" "#{spec.build.linker.command}") unless spec.build.linker.command.include?(relink)
end
