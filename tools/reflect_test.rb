# Host-side test of spec.reflect. It stubs MRuby::Gem::Specification
# instead of loading mruby's Rakefile, because the API under test touches
# only the build directory, the object list and the function name.
# Run with: ruby tools/reflect_test.rb
require 'test/unit'
require 'tmpdir'
require 'rake'

module MRuby
  module Gem
    class Specification
      attr_reader :build_dir, :objs, :funcname

      def initialize(build_dir)
        @build_dir = build_dir
        @objs = []
        @funcname = 'mruby_sqlite'
      end

      def objfile(name)
        "#{name}.o"
      end
    end
  end
end

require_relative 'reflect'

class ReflectTest < Test::Unit::TestCase
  def setup
    @dir = Dir.mktmpdir
    @spec = MRuby::Gem::Specification.new(@dir)
  end

  def teardown
    FileUtils.remove_entry(@dir)
  end

  # The headers come first, so the gem, the varargs header and the
  # lifetime header see the declarations of the library.
  def test_the_source_includes_the_headers_before_the_gem
    @spec.reflect(headers: ['sqlite3.h'], scopes: ['sqlite'])
    text = File.read(@spec.reflect_source)
    assert_operator(text.index('#include <sqlite3.h>'), :<, text.index('#include <mruby/reflect_varargs.h>'))
    assert_operator(text.index('#include <sqlite3.h>'), :<, text.index('#include <mruby/reflect_object_lifetimes.h>'))
    assert_operator(text.index('#include <mruby/reflect_varargs.h>'), :<, text.index('#include <mruby/reflection.hpp>'))
    assert_operator(text.index('#include <mruby/reflect_object_lifetimes.h>'), :<, text.index('#include <mruby/reflection.hpp>'))
  end

  def test_the_scopes_are_listed_as_cpp_names_them
    @spec.reflect(headers: ['a.h'], scopes: ['ns', '::other::Klass'])
    assert_match(/reflect<\^\^::ns, \^\^::other::Klass>\(\)/, File.read(@spec.reflect_source))
  end

  def test_gem_init_and_gem_final_carry_the_function_name_of_the_gem
    @spec.reflect(headers: ['a.h'], scopes: ['ns'])
    text = File.read(@spec.reflect_source)
    assert_match(/void mrb_mruby_sqlite_gem_init\(mrb_state \*mrb\)/, text)
    assert_match(/void mrb_mruby_sqlite_gem_final\(mrb_state \*\)/, text)
  end

  def test_the_source_is_one_object_of_the_gem
    @spec.reflect(headers: ['a.h'], scopes: ['ns'])
    @spec.reflect(headers: ['a.h'], scopes: ['ns'])
    assert_equal(["#{@dir}/reflect/reflect.o"], @spec.objs)
  end

  # An unchanged call leaves the source alone, so nothing compiles again.
  def test_an_unchanged_call_does_not_touch_the_source
    @spec.reflect(headers: ['a.h'], scopes: ['ns'])
    File.utime(Time.at(0), Time.at(0), @spec.reflect_source)
    @spec.reflect(headers: ['a.h'], scopes: ['ns'])
    assert_equal(Time.at(0), File.mtime(@spec.reflect_source))
  end

  def test_empty_or_wrong_arguments_are_refused
    assert_raise(ArgumentError) { @spec.reflect(headers: [], scopes: ['ns']) }
    assert_raise(ArgumentError) { @spec.reflect(headers: ['a.h'], scopes: []) }
    assert_raise(ArgumentError) { @spec.reflect(headers: ['a.h'], scopes: [:ns]) }
  end
end
