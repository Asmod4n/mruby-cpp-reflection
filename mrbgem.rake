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

# A reflected class may name a function that its headers declare and no
# linked library defines: an extern template, a C++23 member a shared
# library built earlier lacks, a marker only ever named in sizeof. The
# first link names what is missing; the second points each at
# reflect_undefined, which raises NotImplementedError when Ruby calls it.
module ReflectUndefinedLink
  private

  def _run(options, params = {}, chdir: nil)
    return super unless options.equal?(link_options)
    require 'open3'
    cmd = command_line(options, params)
    out, status = Open3.capture2e({ 'LC_ALL' => 'C' }, "#{cmd} -Wl,--no-demangle", chdir: chdir || Dir.pwd)
    return if status.success?
    missing = out.scan(/undefined reference to [`']([^']+)'/).flatten.uniq
    if missing.empty?
      $stderr.print out
      fail "Command failed with status (#{status.exitstatus}): [#{cmd}]"
    end
    _pp 'LD', "again, #{missing.size} undefined"
    sh "#{cmd} #{missing.map { |symbol| "-Wl,--defsym=#{symbol}=reflect_undefined" }.join(' ')}", chdir: chdir || Dir.pwd
  end
end

MRuby::Gem::Specification.new('mruby-cpp-reflection') do |spec|
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
  reflect_presyms(spec, "#{spec.dir}/test/reflect_presyms/main.cpp")
  spec.build.linker.extend(ReflectUndefinedLink) unless spec.build.linker.singleton_class.include?(ReflectUndefinedLink)
end
