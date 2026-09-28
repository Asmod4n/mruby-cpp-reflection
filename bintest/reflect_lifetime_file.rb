# Bintest of the search_package record. It stubs
# MRuby::Gem::Specification as the other bintests do, and runs the
# pkg-config of the host against bintest/pkgconfig.
require 'tmpdir'
require 'ostruct'
require 'fileutils'
require 'rake'

module MRuby
  module Gem
    class Specification
      attr_reader :build_dir, :objs, :funcname, :cxx
      attr_accessor :search_answer

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

      # mruby's search_package, with an answer that the test chooses.
      def search_package(name, version_query = nil)
        search_answer
      end
    end
  end
end

load File.expand_path('../mrbgem.rake', __dir__) unless MRuby::Gem::Specification.const_defined?(:ReflectSearchPackage, false)

ENV['PKG_CONFIG_PATH'] = File.expand_path('pkgconfig', __dir__)

def lifetime_spec(answer = true)
  @dir = Dir.mktmpdir
  spec = MRuby::Gem::Specification.new(@dir)
  spec.search_answer = answer
  spec
end

# The lifetime lookup reads this record, so it holds what pkg-config
# found for the query that mruby's search_package used.
assert('ReflectLifetimeFileTest: search_package records the name, the query and the version') do
  spec = lifetime_spec
  begin
    assert_true(spec.search_package('reflectlib', '>= 1'))
    assert_equal([{ name: 'reflectlib', query: '>= 1', version: '1.2.3' }], spec.reflect_packages)
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# pkgconf 1.8.1 prints one line per condition. Equal lines are one
# version.
assert('ReflectLifetimeFileTest: equal lines of several conditions are one version') do
  spec = lifetime_spec
  begin
    spec.search_package('reflectlib', '>= 1 reflectlib < 2')
    assert_equal('1.2.3', spec.reflect_packages.first[:version])
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# The override changes nothing that mruby's search_package answers.
assert('ReflectLifetimeFileTest: search_package answers what mruby answers') do
  spec = lifetime_spec(:mruby_answer)
  begin
    assert_equal(:mruby_answer, spec.search_package('reflectlib'))
    assert_equal([], spec.reflect_packages)
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# A package that pkg-config does not find, or a query that it refuses,
# leaves no record.
assert('ReflectLifetimeFileTest: a failed query records nothing') do
  spec = lifetime_spec
  begin
    assert_true(spec.search_package('reflectlib', '>= 2'))
    assert_true(spec.search_package('no-such-package-of-the-bintest'))
    assert_equal([], spec.reflect_packages)
    spec.search_answer = false
    assert_false(spec.search_package('reflectlib'))
    assert_equal([], spec.reflect_packages)
  ensure
    FileUtils.remove_entry(@dir)
  end
end
