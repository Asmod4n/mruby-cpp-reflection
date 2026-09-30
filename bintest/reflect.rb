# Bintest of spec.reflect. It stubs MRuby::Gem::Specification
# instead of loading mruby's Rakefile, because the API under test touches
# only the build directory, the object list and the function name.
require 'tmpdir'
require 'ostruct'
require 'fileutils'
require 'stringio'
require 'rake'

module MRuby
  module Gem
    class Specification
      include Rake::DSL
      attr_reader :build_dir, :objs, :funcname, :cxx
      attr_accessor :build

      # mrbgem.rake ends with the Specification of the gem itself, and
      # that block needs a build, so it does not run here.
      def self.new(*arguments, &block)
        super(*arguments) unless block
      end

      def initialize(build_dir)
        @build_dir = build_dir
        @objs = []
        @funcname = 'mruby_sqlite'
        @cxx = OpenStruct.new(include_paths: [], flags: [], defines: [])
        @build = OpenStruct.new(cxx: OpenStruct.new(flags: []), build_dir: build_dir,
                                exts: OpenStruct.new(executable: '', presym_preprocessed: '.pi'))
      end

      def name
        'mruby-sqlite'
      end

      def objfile(name)
        "#{name}.o"
      end
    end
  end
end

load File.expand_path('../mrbgem.rake', __dir__) unless MRuby::Gem::Specification.method_defined?(:reflect)

# The headers come first, so the gem, the varargs header and the
# lifetime header see the declarations of the library.
assert('ReflectTest: the source includes the headers before the gem') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect('sqlite', headers: ['sqlite3.h'])
    text = File.read(@spec.reflect_source)
    assert_operator(text.index('#include <sqlite3.h>'), :<, text.index('#include <mruby/reflect_varargs.h>'))
    assert_operator(text.index('#include <sqlite3.h>'), :<, text.index('#include <mruby/reflect_object_lifetimes.h>'))
    assert_operator(text.index('#include <mruby/reflect_varargs.h>'), :<, text.index('#include <mruby/cpp_reflection.hpp>'))
    assert_operator(text.index('#include <mruby/reflect_object_lifetimes.h>'), :<, text.index('#include <mruby/cpp_reflection.hpp>'))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# The facts header names rows by the file of a header and holds macros
# of the headers, so it comes after the headers. The cxx: text and the gem
# read it, so it comes before both.
assert('ReflectTest: the source includes the facts after the headers and before the cxx: text') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect('sqlite', headers: ['sqlite3.h'], cxx: "int a;\n")
    text = File.read(@spec.reflect_source)
    assert_operator(text.index('#include <sqlite3.h>'), :<, text.index('#include <mruby/reflect_facts.h>'))
    assert_operator(text.index('#include <mruby/reflect_facts.h>'), :<, text.index("#include \"#{@spec.reflect_cxx_source}\""))
    assert_operator(text.index('#include <mruby/reflect_facts.h>'), :<, text.index('#include <mruby/cpp_reflection.hpp>'))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# libclang runs only where reflection reads its answer. A build with
# -freflection gets the source that libclang reads, the include path of
# the header it writes, and the task that writes it before the source of
# spec.reflect and its presym pass compile.
assert('ReflectTest: a build with -freflection reads the facts of the headers') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect('sqlite', headers: ['sqlite3.h'])
    assert_false(File.exist?(@spec.reflect_facts_source))
    @spec.build.cxx.flags << '-freflection'
    @spec.reflect('sqlite', headers: ['sqlite3.h', 'sqlite3ext.h'])
    assert_equal("#include <sqlite3.h>\n#include <sqlite3ext.h>\n", File.read(@spec.reflect_facts_source))
    assert_include(@spec.cxx.include_paths, "#{@dir}/include")
    object = @spec.objfile(@spec.reflect_source.pathmap('%X'))
    assert_include(Rake::Task[object].prerequisites, @spec.reflect_facts_written)
    assert_include(Rake::Task[object.ext('.pi')].prerequisites, @spec.reflect_facts_written)
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectTest: the scopes are listed as cpp names them') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect('ns', '::other::Klass', headers: ['a.h'])
    assert_true(/reflect<\^\^::ns, \^\^::other::Klass>\(\)/.match?(File.read(@spec.reflect_source)))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectTest: gem init and gem final carry the function name of the gem') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect('ns', headers: ['a.h'])
    text = File.read(@spec.reflect_source)
    assert_true(/void mrb_mruby_sqlite_gem_init\(mrb_state \*mrb\)/.match?(text))
    assert_true(/void mrb_mruby_sqlite_gem_final\(mrb_state \*\)/.match?(text))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectTest: the source is one object of the gem') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect('ns', headers: ['a.h'])
    @spec.reflect('ns', headers: ['a.h'])
    assert_equal(["#{@dir}/reflect/reflect.o"], @spec.objs)
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# An unchanged call leaves the source alone, so nothing compiles again.
assert('ReflectTest: an unchanged call does not touch the source') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect('ns', headers: ['a.h'])
    File.utime(Time.at(0), Time.at(0), @spec.reflect_source)
    @spec.reflect('ns', headers: ['a.h'])
    assert_equal(Time.at(0), File.mtime(@spec.reflect_source))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectTest: empty or wrong arguments are refused') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { @spec.reflect('ns', headers: []) }
    assert_raise(ArgumentError) { @spec.reflect(headers: ['a.h']) }
    assert_raise(ArgumentError) { @spec.reflect(:ns, headers: ['a.h']) }
    assert_raise(ArgumentError) { @spec.reflect('ns', headers: ['a.h'], cxx: :text) }
    assert_raise(ArgumentError) { @spec.reflect('ns', headers: ['a.h'], c: 1) }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# The c: text is a source of its own for the C compiler, and the cxx:
# text is read by the reflection, so the build writes each verbatim.
assert('ReflectTest: the c and cxx texts are written verbatim') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    c = "int lib_c(void) { return 1; }\n"
    cxx = "using Positions = lib::Vector<lib::Vec2>;\n"
    @spec.reflect('lib', headers: ['lib.h'], c: c, cxx: cxx)
    assert_equal(c, File.read(@spec.reflect_c_source))
    assert_equal(cxx, File.read(@spec.reflect_cxx_source))
    assert_equal("#{@dir}/reflect/reflect_c.c", @spec.reflect_c_source)
    assert_equal("#{@dir}/reflect/reflect_cxx.cxx", @spec.reflect_cxx_source)
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# The C text is one more object; the C++ text is part of reflect.cpp,
# so a function it defines is defined once.
assert('ReflectTest: the c text is an object and the cxx text is not') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect('lib', headers: ['lib.h'], c: "\n", cxx: "\n")
    assert_equal(["#{@dir}/reflect/reflect_c.o", "#{@dir}/reflect/reflect.o"], @spec.objs)
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# The reflection sees what the cxx: text declares, and the cxx: text
# sees the headers of the library.
assert('ReflectTest: the source includes the cxx text after the headers and before the reflection') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect('lib', headers: ['lib.h'], cxx: "\n")
    text = File.read(@spec.reflect_source)
    included = %(#include "#{@spec.reflect_cxx_source}")
    assert_operator(text.index('#include <lib.h>'), :<, text.index(included))
    assert_operator(text.index(included), :<, text.index('#include <mruby/reflect_object_lifetimes.h>'))
    assert_operator(text.index(included), :<, text.index('#include <mruby/cpp_reflection.hpp>'))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# Without the keys the build writes neither file.
assert('ReflectTest: no c or cxx text writes no file') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect('lib', headers: ['lib.h'])
    assert_false(File.exist?(@spec.reflect_c_source))
    assert_false(File.exist?(@spec.reflect_cxx_source))
    assert_false(File.read(@spec.reflect_source).include?('reflect_cxx.cxx'))
  ensure
    FileUtils.remove_entry(@dir)
  end
end
