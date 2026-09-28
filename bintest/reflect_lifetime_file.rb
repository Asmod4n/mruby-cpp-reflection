# Bintest of the search_package record and of the lifetime files. It
# stubs MRuby::Gem::Specification as the other bintests do, and runs
# the pkg-config of the host against bintest/pkgconfig.
require 'tmpdir'
require 'ostruct'
require 'fileutils'
require 'stringio'
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
  files = "#{@dir}/lifetimes"
  spec.define_singleton_method(:reflect_lifetime_files_dir) { files }
  spec
end

def lifetime_file(name, text)
  path = "#{@dir}/lifetimes/reflectlib/#{name}.lifetime"
  FileUtils.mkdir_p(File.dirname(path))
  File.write(path, text)
end

def printed
  $stdout = StringIO.new
  yield
  $stdout.string
ensure
  $stdout = STDOUT
end

def lifetime_header(spec)
  File.exist?(spec.reflect_object_lifetime_header) ? File.read(spec.reflect_object_lifetime_header) : ''
end

CONFIRMED = "confirmed_by 'the authors of reflectlib, in their issue 1'\n"
DECLARED = "reflect_object_lifetime 'rl::handle' do\n  allocator :make\n  deallocator :close\nend\n"

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

# A file for the exact version is bound to that version, so it wins
# over the file of the major version.
assert('ReflectLifetimeFileTest: the exact version file wins over the major file') do
  spec = lifetime_spec
  begin
    lifetime_file('1.2.3', "version '1.2.3'\n#{CONFIRMED}#{DECLARED}")
    lifetime_file('1', "version '1'\n#{CONFIRMED}follows_semver confirmed_by: 'x'\nreflect_object_lifetime 'rl::other' do\n  ends_lifetime :close\nend\n")
    spec.search_package('reflectlib')
    assert_true(lifetime_header(spec).include?('mruby::cpp_reflection::allocator(^^::rl::handle::make)'))
    assert_false(lifetime_header(spec).include?('rl::other'))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# A major file is used only when it states that the library follows
# SemVer, because only then does the major version bind the lifetimes.
assert('ReflectLifetimeFileTest: a major file is used only with the SemVer statement') do
  spec = lifetime_spec
  begin
    lifetime_file('1', "version '1'\n#{CONFIRMED}#{DECLARED}")
    assert_true(printed { spec.search_package('reflectlib') }.include?('does not state that the library follows SemVer'))
    assert_equal('', lifetime_header(spec))
    lifetime_file('1', "version '1'\n#{CONFIRMED}follows_semver confirmed_by: 'the authors of reflectlib, in their README'\n#{DECLARED}")
    spec.search_package('reflectlib')
    assert_true(lifetime_header(spec).include?('mruby::cpp_reflection::deallocator(^^::rl::handle::close)'))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# A file that names nobody who confirmed it is not used.
assert('ReflectLifetimeFileTest: a file without confirmation is not used') do
  spec = lifetime_spec
  begin
    lifetime_file('1.2.3', "version '1.2.3'\n#{DECLARED}")
    assert_true(printed { spec.search_package('reflectlib') }.include?('names nobody who confirmed it'))
    assert_equal('', lifetime_header(spec))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# The build config wins over the gem's mrbgem.rake, and both win over
# the file of the repository. rake prints each replacement.
assert('ReflectLifetimeFileTest: mrbgem.rake and the build config replace the file') do
  spec = lifetime_spec
  begin
    lifetime_file('1.2.3', "version '1.2.3'\n#{CONFIRMED}#{DECLARED}reflect_object_lifetime 'rl::tree' do\n  ends_lifetime :free\nend\n")
    out = printed do
      spec.reflect_object_lifetime('rl::handle') { ends_lifetime :destroy }
      spec.search_package('reflectlib')
      spec.reflect_object_lifetime('rl::tree') { ends_lifetime :drop }
      spec.reflect_object_lifetime('rl::tree') { ends_lifetime :release }
    end
    lines = out.lines
    assert_equal(3, lines.size)
    assert_true(lines[0].start_with?('reflect_object_lifetime: replaced the declaration of rl::handle from '))
    assert_true(lines[1].start_with?('reflect_object_lifetime: replaced the declaration of rl::tree'))
    assert_true(lines[2].start_with?('reflect_object_lifetime: replaced the declaration of rl::tree'))
    text = lifetime_header(spec)
    assert_true(text.include?('ends_lifetime(^^::rl::handle::destroy)'))
    assert_true(text.include?('ends_lifetime(^^::rl::tree::release)'))
    assert_false(text.include?('allocator(^^::rl::handle::make)'))
  ensure
    FileUtils.remove_entry(@dir)
  end
end
