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
      attr_reader :build_dir, :objs, :funcname, :cxx

      # mrbgem.rake ends with the Specification of the gem itself, and
      # that block needs a build, so it does not run here.
      def self.new(*arguments, &block)
        super(*arguments) unless block
      end

      def initialize(build_dir)
        @build_dir = build_dir
        @objs = []
        @funcname = 'mruby_sqlite'
        @cxx = OpenStruct.new(include_paths: [])
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
    @spec.reflect(headers: ['sqlite3.h'], scopes: ['sqlite'])
    text = File.read(@spec.reflect_source)
    assert_operator(text.index('#include <sqlite3.h>'), :<, text.index('#include <mruby/reflect_varargs.h>'))
    assert_operator(text.index('#include <sqlite3.h>'), :<, text.index('#include <mruby/reflect_object_lifetimes.h>'))
    assert_operator(text.index('#include <mruby/reflect_varargs.h>'), :<, text.index('#include <mruby/cpp_reflection.hpp>'))
    assert_operator(text.index('#include <mruby/reflect_object_lifetimes.h>'), :<, text.index('#include <mruby/cpp_reflection.hpp>'))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectTest: the scopes are listed as cpp names them') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect(headers: ['a.h'], scopes: ['ns', '::other::Klass'])
    assert_true(/reflect<\^\^::ns, \^\^::other::Klass>\(\)/.match?(File.read(@spec.reflect_source)))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectTest: gem init and gem final carry the function name of the gem') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect(headers: ['a.h'], scopes: ['ns'])
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
    @spec.reflect(headers: ['a.h'], scopes: ['ns'])
    @spec.reflect(headers: ['a.h'], scopes: ['ns'])
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
    @spec.reflect(headers: ['a.h'], scopes: ['ns'])
    File.utime(Time.at(0), Time.at(0), @spec.reflect_source)
    @spec.reflect(headers: ['a.h'], scopes: ['ns'])
    assert_equal(Time.at(0), File.mtime(@spec.reflect_source))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectTest: empty or wrong arguments are refused') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { @spec.reflect(headers: [], scopes: ['ns']) }
    assert_raise(ArgumentError) { @spec.reflect(headers: ['a.h'], scopes: []) }
    assert_raise(ArgumentError) { @spec.reflect(headers: ['a.h'], scopes: [:ns]) }
  ensure
    FileUtils.remove_entry(@dir)
  end
end
