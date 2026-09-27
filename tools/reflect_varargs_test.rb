# Host-side test of the reflect_format/reflect_varargs API. It stubs
# MRuby::Gem::Specification instead of loading mruby's Rakefile,
# because the API under test touches no other part of that class.
# Run with: ruby tools/reflect_varargs_test.rb
require 'test/unit'

require 'tmpdir'
require 'ostruct'

module MRuby
  module Gem
    class Specification
      attr_reader :build_dir, :cxx

      def initialize(build_dir)
        @build_dir = build_dir
        @cxx = OpenStruct.new(include_paths: [])
      end
    end
  end
end

require_relative 'reflect_varargs'

class ReflectVarargsTest < Test::Unit::TestCase
  def setup
    @dir = Dir.mktmpdir
    @spec = MRuby::Gem::Specification.new(@dir)
  end

  def teardown
    FileUtils.remove_entry(@dir)
  end

  # The header is what the C++ side reads: one specialization per
  # function, one reflect_types per list, the name as C++ names it.
  def test_reflect_varargs_writes_the_header_the_gem_reads
    @spec.reflect_varargs('ns::sum', [%w[int], ['int', 'const char *']])
    text = File.read(@spec.reflect_varargs_header)
    assert_match(/reflect_varargs_lists<\^\^::ns::sum>/, text)
    assert_match(/\^\^reflect_types<int>, \^\^reflect_types<int, const char \*>/, text)
    assert_equal(["#{@dir}/include"], @spec.cxx.include_paths)
  end

  # An unchanged declaration leaves the header alone, so the build that
  # follows compiles nothing again.
  def test_an_unchanged_declaration_does_not_touch_the_header
    @spec.reflect_varargs('sum', [%w[int]])
    File.utime(Time.at(0), Time.at(0), @spec.reflect_varargs_header)
    capture_output { @spec.reflect_varargs('sum', [%w[int]]) }
    assert_equal(Time.at(0), File.mtime(@spec.reflect_varargs_header))
  end

  def test_a_list_of_something_other_than_type_names_is_refused
    assert_raise(ArgumentError) { @spec.reflect_varargs('sum', %w[int]) }
    assert_raise(ArgumentError) { @spec.reflect_varargs('sum', [[:int]]) }
  end

  def test_reflect_varargs_stores_one_declaration_per_function_name
    @spec.reflect_varargs('execl', [%w[char* char*]])
    assert_equal([%w[char* char*]], @spec.reflect_varargs_declarations['execl'])
  end

  # A gem's mrbgem.rake runs before the user's conf.gem block
  # (mruby lib/mruby/gem.rb 246-256), so the build config's call is
  # the second one and wins.
  def test_a_second_declaration_for_the_same_function_replaces_the_first
    @spec.reflect_varargs('execl', [%w[char*]])
    @spec.reflect_varargs('execl', [%w[char* char*]])
    assert_equal([%w[char* char*]], @spec.reflect_varargs_declarations['execl'])
  end

  def test_a_replacement_prints_one_line
    @spec.reflect_varargs('execl', [%w[char*]])
    out, = capture_output { @spec.reflect_varargs('execl', [%w[char* char*]]) }
    assert_equal(1, out.lines.count)
    assert_match(/execl/, out)
  end

  def test_no_line_is_printed_for_the_first_declaration
    out, = capture_output { @spec.reflect_varargs('execl', [%w[char*]]) }
    assert_equal('', out)
  end

  def test_a_type_list_longer_than_16_is_refused
    assert_raise(ArgumentError) { @spec.reflect_varargs('too_many', [Array.new(17, 'int')]) }
  end

  def test_a_type_list_of_exactly_16_is_accepted
    assert_nothing_raised { @spec.reflect_varargs('sixteen', [Array.new(16, 'int')]) }
  end

  # The keywords follow GCC's format attribute, so that a
  # declaration reads like the attribute on the C prototype.
  def test_reflect_format_stores_the_arguments_of_the_format_attribute
    @spec.reflect_format('fprintf', archetype: :printf, string_index: 2, first_to_check: 3)
    assert_equal({ archetype: :printf, string_index: 2, first_to_check: 3 }, @spec.reflect_format_declarations['fprintf'])
  end

  def test_reflect_format_accepts_scanf
    @spec.reflect_format('scanf', archetype: :scanf, string_index: 1, first_to_check: 2)
    assert_equal(:scanf, @spec.reflect_format_declarations['scanf'][:archetype])
  end

  def test_reflect_format_refuses_an_unknown_archetype
    assert_raise(ArgumentError) { @spec.reflect_format('strftime', archetype: :strftime, string_index: 3, first_to_check: 0) }
  end

  # first_to_check 0 is how GCC marks vprintf and its kin. A va_list
  # cannot be built by the gem, so the declaration is refused and the
  # function is not silently left out.
  def test_a_va_list_function_is_refused
    assert_raise(NotImplementedError) { @spec.reflect_format('vprintf', archetype: :printf, string_index: 1, first_to_check: 0) }
  end

  def test_string_index_counts_from_one
    assert_raise(ArgumentError) { @spec.reflect_format('printf', archetype: :printf, string_index: 0, first_to_check: 1) }
  end

  def test_first_to_check_lies_after_string_index
    assert_raise(ArgumentError) { @spec.reflect_format('printf', archetype: :printf, string_index: 1, first_to_check: 1) }
  end

  def test_a_second_reflect_format_declaration_replaces_the_first_and_prints_one_line
    @spec.reflect_format('fprintf', archetype: :printf, string_index: 1, first_to_check: 2)
    out, = capture_output { @spec.reflect_format('fprintf', archetype: :printf, string_index: 2, first_to_check: 3) }
    assert_equal(1, out.lines.count)
    assert_equal({ archetype: :printf, string_index: 2, first_to_check: 3 }, @spec.reflect_format_declarations['fprintf'])
  end
end
