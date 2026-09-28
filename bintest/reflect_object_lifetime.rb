# Bintest of the reflect_object_lifetime API. It stubs
# MRuby::Gem::Specification instead of loading mruby's Rakefile,
# because the API under test touches no other part of that class.
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

def declare(name = 'Tree', &block)
  @spec.reflect_object_lifetime(name, &block)
end

def header
  File.read(@spec.reflect_object_lifetime_header)
end

# The header is all the C++ side reads: one specialization of
# mruby::cpp_reflection::object_lifetime per class, named as C++ names
# it, with one word of <mruby/cpp_reflection_lifetime.hpp> per entry, as a source
# of C++ writes it by hand.
assert('ReflectObjectLifetimeTest: the header specializes the words of the class') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    declare('ns::Tree') { takes_ownership :set_parent, by: 0 }
    assert_true(header.include?("template <>\ninline constexpr auto mruby::cpp_reflection::object_lifetime<^^::ns::Tree> = std::array{\n"))
    assert_true(header.include?("    mruby::cpp_reflection::takes_ownership(^^::ns::Tree::set_parent, {.by = 0}),\n};\n"))
    assert_true(header.include?('#include <mruby/cpp_reflection_lifetime.hpp>'))
    assert_equal(["#{@dir}/include"], @spec.cxx.include_paths)
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# Every word reaches the header with its arguments, so that nothing a
# declaration says is lost on the way to C++.
assert('ReflectObjectLifetimeTest: every word reaches the header') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    declare('lib::handle') do
      takes_ownership :initialize, by: :parent
      ends_lifetime :destroy
      retains :set_layout, 1
      errors :open, error: :negative, sets_errno: true
      errors :find, error: nil
      errors :status, success: 0
      stack_reserve :depth, 1024
      threadsafe :start, :no
      allocator :open_handle, output_parameter: :made
      deallocator :close_handle
      shared_ownership increment: :ref, decrement: :unref
    end
    text = header
    [
      'takes_ownership(^^::lib::handle, {.by = "parent"})',
      'ends_lifetime(^^::lib::handle::destroy)',
      'retains(^^::lib::handle::set_layout, 1)',
      'errors(^^::lib::handle::open, {.error = mruby::cpp_reflection::negative, .sets_errno = true})',
      'errors(^^::lib::handle::find, {.error = nullptr, .sets_errno = false})',
      'errors(^^::lib::handle::status, {.success = 0, .sets_errno = false})',
      'stack_reserve(^^::lib::handle::depth, 1024)',
      'threadsafe(^^::lib::handle::start, false)',
      'allocator(^^::lib::handle::open_handle, {.output_parameter = "made"})',
      'deallocator(^^::lib::handle::close_handle)',
      'shared_ownership({.increment = ^^::lib::handle::ref, .decrement = ^^::lib::handle::unref})'
    ].each { |word| assert_true(text.include?("    mruby::cpp_reflection::#{word},\n"), word) }
    assert_equal(11, text.scan(/^    mruby::cpp_reflection::/).size)
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# results_of with several allocators gives one entry per allocator,
# so each entry names one allocator.
assert('ReflectObjectLifetimeTest: results of gives one entry per allocator') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    declare('h') do
      allocator :open
      allocator :make
      deallocator :close, results_of: %i[open make]
    end
    assert_true(header.include?('mruby::cpp_reflection::deallocator(^^::h::close, {.results_of = ^^::h::open})'))
    assert_true(header.include?('mruby::cpp_reflection::deallocator(^^::h::close, {.results_of = ^^::h::make})'))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# An unchanged declaration leaves the header alone, so the build that
# follows compiles nothing again.
assert('ReflectObjectLifetimeTest: an unchanged declaration does not touch the header') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    declare { ends_lifetime :destroy, 0 }
    File.utime(Time.at(0), Time.at(0), @spec.reflect_object_lifetime_header)
    $stdout = StringIO.new
    begin
      declare { ends_lifetime :destroy, 0 }
    ensure
      $stdout = STDOUT
    end
    assert_equal(Time.at(0), File.mtime(@spec.reflect_object_lifetime_header))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# A gem's mrbgem.rake runs before the user's conf.gem block
# (mruby lib/mruby/gem.rb 246-256), so the build config's call is
# the second one and wins, and the replacement is visible.
assert('ReflectObjectLifetimeTest: a second declaration replaces the first and prints one line') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    declare { ends_lifetime :destroy, 0 }
    out = StringIO.new
    $stdout = out
    begin
      declare { retains :keep, 0 }
    ensure
      $stdout = STDOUT
    end
    out = out.string
    assert_equal(1, out.lines.count)
    assert_true(/Tree/.match?(out))
    assert_equal([:retains], @spec.reflect_object_lifetime_declarations['Tree'].map { |w| w[:word] })
    assert_false(/destroy/.match?(header))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: no line is printed for the first declaration') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    out = StringIO.new
    $stdout = out
    begin
      declare { ends_lifetime :destroy, 0 }
    ensure
      $stdout = STDOUT
    end
    out = out.string
    assert_equal('', out)
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# The block runs on an object that knows only the words, so a word
# that does not exist is an error and not a method of Kernel.
assert('ReflectObjectLifetimeTest: an unknown word is refused') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(NoMethodError) { declare { release :set_parent } }
    assert_raise(NoMethodError) { declare { puts :set_parent } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: a declaration takes a block') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { @spec.reflect_object_lifetime('Tree') }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: takes ownership names of or by') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { declare { takes_ownership :set_parent } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# A position counts the parameters from 0, as in every other word, and
# the position left out is the receiver.
assert('ReflectObjectLifetimeTest: takes ownership leaves out the receiver') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    declare { takes_ownership :adopt, of: 1 }
    assert_true(header.include?('mruby::cpp_reflection::takes_ownership(^^::Tree::adopt, {.of = 1})'))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# The words of the earlier runtime declaration are no words now.
assert('ReflectObjectLifetimeTest: owns is no word') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(NoMethodError) { declare { owns :set_parent, owner: 0 } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: a parameter is a number or an identifier') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(TypeError) { declare { retains :keep, 'first' } }
    assert_raise(TypeError) { declare { retains :keep, -1 } }
    assert_raise(TypeError) { declare { takes_ownership :set_parent, by: 1.5 } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# C++ names a function of the class through the class, and a function
# beside the class through its own scope, which rake cannot find, so the
# second one is named by its C++ name.
assert('ReflectObjectLifetimeTest: a function is named by symbol or by its C++ name') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(TypeError) { declare { ends_lifetime 1 } }
    declare('lib::handle') do
      allocator 'lib::handle_make'
      deallocator 'lib::handle_close', results_of: 'lib::handle_make'
    end
    assert_true(header.include?('mruby::cpp_reflection::allocator(^^::lib::handle_make)'))
    assert_true(header.include?('mruby::cpp_reflection::deallocator(^^::lib::handle_close, {.results_of = ^^::lib::handle_make})'))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: errors names either success or error') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { declare { errors :open } }
    assert_raise(ArgumentError) { declare { errors :open, success: 0, error: -1 } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: an answer is an integer or nil') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(TypeError) { declare { errors :open, success: 'ok' } }
    assert_raise(TypeError) { declare { errors :open, error: :positive } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: sets errno is true or false') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(TypeError) { declare { errors :open, error: :negative, sets_errno: 1 } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: a stack reserve is a positive number of bytes') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { declare { stack_reserve :depth, 0 } }
    assert_raise(ArgumentError) { declare { stack_reserve :depth, '1k' } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: threadsafe takes only no') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { declare { threadsafe :start, :yes } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: results of names allocators as functions') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(TypeError) { declare { allocator(:make); deallocator :close, results_of: 1 } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: shared ownership names functions') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(TypeError) { declare { shared_ownership increment: 1, decrement: :unref } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# One function carries one word of each kind; a second one would leave
# open which of the two holds.
assert('ReflectObjectLifetimeTest: a word is declared once for a function') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { declare { takes_ownership(:set_parent, by: 0); takes_ownership :set_parent, of: 0 } }
    assert_raise(ArgumentError) { declare { ends_lifetime(:destroy, 0); ends_lifetime :destroy } }
    assert_raise(ArgumentError) { declare { errors(:open, success: 0); errors :open, error: nil } }
    assert_raise(ArgumentError) { declare { stack_reserve(:depth, 1); stack_reserve :depth, 2 } }
    assert_raise(ArgumentError) { declare { threadsafe(:start, :no); threadsafe :start, :no } }
    assert_raise(ArgumentError) { declare { retains(:keep, 0); retains :keep, 0 } }
    assert_raise(ArgumentError) { declare { shared_ownership(increment: :ref, decrement: :unref); shared_ownership increment: :ref, decrement: :unref } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: retains takes several parameters of one function') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_nothing_raised { declare { retains(:keep, 0); retains :keep, 1 } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# A handle that an allocator made and no deallocator frees would leak,
# and a deallocator for a handle it did not make would free the wrong
# memory; both are refused before any code is compiled.
assert('ReflectObjectLifetimeTest: an allocator needs a deallocator') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { declare { allocator :make } }
    assert_raise(ArgumentError) { declare { allocator(:make); allocator(:open); deallocator :close, results_of: :make } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: results of names only allocators of the class') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { declare { allocator(:make); deallocator :close, results_of: %i[make open] } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectObjectLifetimeTest: a general deallocator frees what every allocator made') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_nothing_raised { declare { allocator(:make); allocator(:open); deallocator :close } }
  ensure
    FileUtils.remove_entry(@dir)
  end
end
