# Bintest of the reflect_format/reflect_varargs API. It stubs
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

# The header is what the C++ side reads: one specialization of
# mruby::cpp_reflection::varargs per function, one std::tuple per list,
# the name as C++ names it, as a source of C++ writes it by hand.
assert('ReflectVarargsTest: reflect varargs writes the header the gem reads') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect_varargs('ns::sum', [%w[int], ['int', 'const char *']])
    text = File.read(@spec.reflect_varargs_header)
    assert_true(text.include?("template <>\ninline constexpr auto mruby::cpp_reflection::varargs<^^::ns::sum> = std::array{^^std::tuple<int>, ^^std::tuple<int, const char *>};\n"))
    assert_equal(["#{@dir}/include"], @spec.cxx.include_paths)
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# An unchanged declaration leaves the header alone, so the build that
# follows compiles nothing again.
assert('ReflectVarargsTest: an unchanged declaration does not touch the header') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect_varargs('sum', [%w[int]])
    File.utime(Time.at(0), Time.at(0), @spec.reflect_varargs_header)
    $stdout = StringIO.new
    begin
      @spec.reflect_varargs('sum', [%w[int]])
    ensure
      $stdout = STDOUT
    end
    assert_equal(Time.at(0), File.mtime(@spec.reflect_varargs_header))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectVarargsTest: a list of something other than type names is refused') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { @spec.reflect_varargs('sum', %w[int]) }
    assert_raise(ArgumentError) { @spec.reflect_varargs('sum', [[:int]]) }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectVarargsTest: reflect varargs stores one declaration per function name') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect_varargs('execl', [%w[char* char*]])
    assert_equal([%w[char* char*]], @spec.reflect_varargs_declarations['execl'])
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# A gem's mrbgem.rake runs before the user's conf.gem block
# (mruby lib/mruby/gem.rb 246-256), so the build config's call is
# the second one and wins.
assert('ReflectVarargsTest: a second declaration for the same function replaces the first') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect_varargs('execl', [%w[char*]])
    @spec.reflect_varargs('execl', [%w[char* char*]])
    assert_equal([%w[char* char*]], @spec.reflect_varargs_declarations['execl'])
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectVarargsTest: a replacement prints one line') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect_varargs('execl', [%w[char*]])
    out = StringIO.new
    $stdout = out
    begin
      @spec.reflect_varargs('execl', [%w[char* char*]])
    ensure
      $stdout = STDOUT
    end
    out = out.string
    assert_equal(1, out.lines.count)
    assert_true(/execl/.match?(out))
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectVarargsTest: no line is printed for the first declaration') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    out = StringIO.new
    $stdout = out
    begin
      @spec.reflect_varargs('execl', [%w[char*]])
    ensure
      $stdout = STDOUT
    end
    out = out.string
    assert_equal('', out)
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectVarargsTest: a type list longer than 16 is refused') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { @spec.reflect_varargs('too_many', [Array.new(17, 'int')]) }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectVarargsTest: a type list of exactly 16 is accepted') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_nothing_raised { @spec.reflect_varargs('sixteen', [Array.new(16, 'int')]) }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# The keywords follow GCC's format attribute, so that a
# declaration reads like the attribute on the C prototype.
assert('ReflectVarargsTest: reflect format stores the arguments of the format attribute') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect_format('fprintf', archetype: :printf, string_index: 2, first_to_check: 3)
    assert_equal({ archetype: :printf, string_index: 2, first_to_check: 3 }, @spec.reflect_format_declarations['fprintf'])
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectVarargsTest: reflect format accepts scanf') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect_format('scanf', archetype: :scanf, string_index: 1, first_to_check: 2)
    assert_equal(:scanf, @spec.reflect_format_declarations['scanf'][:archetype])
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectVarargsTest: reflect format refuses an unknown archetype') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { @spec.reflect_format('strftime', archetype: :strftime, string_index: 3, first_to_check: 0) }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

# first_to_check 0 is how GCC marks vprintf and its kin. A va_list
# cannot be built by the gem, so the declaration is refused and the
# function is not silently left out.
assert('ReflectVarargsTest: a va list function is refused') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(NotImplementedError) { @spec.reflect_format('vprintf', archetype: :printf, string_index: 1, first_to_check: 0) }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectVarargsTest: string index counts from one') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { @spec.reflect_format('printf', archetype: :printf, string_index: 0, first_to_check: 1) }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectVarargsTest: first to check lies after string index') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    assert_raise(ArgumentError) { @spec.reflect_format('printf', archetype: :printf, string_index: 1, first_to_check: 1) }
  ensure
    FileUtils.remove_entry(@dir)
  end
end

assert('ReflectVarargsTest: a second reflect format declaration replaces the first and prints one line') do
  @dir = Dir.mktmpdir
  @spec = MRuby::Gem::Specification.new(@dir)
  begin
    @spec.reflect_format('fprintf', archetype: :printf, string_index: 1, first_to_check: 2)
    out = StringIO.new
    $stdout = out
    begin
      @spec.reflect_format('fprintf', archetype: :printf, string_index: 2, first_to_check: 3)
    ensure
      $stdout = STDOUT
    end
    out = out.string
    assert_equal(1, out.lines.count)
    assert_equal({ archetype: :printf, string_index: 2, first_to_check: 3 }, @spec.reflect_format_declarations['fprintf'])
  ensure
    FileUtils.remove_entry(@dir)
  end
end
