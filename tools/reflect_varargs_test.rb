# Host-side test of the reflect_format/reflect_varargs API. It stubs
# MRuby::Gem::Specification instead of loading mruby's Rakefile,
# because the API under test touches no other part of that class.
# Run with: ruby tools/reflect_varargs_test.rb
require 'test/unit'

module MRuby
  module Gem
    class Specification
      def initialize
      end
    end
  end
end

require_relative 'reflect_varargs'

class ReflectVarargsTest < Test::Unit::TestCase
  def setup
    @spec = MRuby::Gem::Specification.new
  end

  def test_reflect_varargs_stores_one_declaration_per_function_name
    @spec.reflect_varargs('execl', [%w[char* char*]])
    assert_equal([%w[char* char*]], @spec.reflect_varargs_declarations['execl'])
  end

  # RULES.md: "rake writes a header ... A gem's mrbgem.rake runs before
  # the user's conf.gem block (gem.rb 246-256), so a build config's
  # call replaces the gem's own and wins."
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

  def test_reflect_format_stores_the_format_parameter_and_kind
    @spec.reflect_format('printf', format_param: 0, kind: :printf)
    assert_equal({ format_param: 0, kind: :printf }, @spec.reflect_format_declarations['printf'])
  end

  def test_reflect_format_accepts_scanf
    @spec.reflect_format('scanf', format_param: 0, kind: :scanf)
    assert_equal(:scanf, @spec.reflect_format_declarations['scanf'][:kind])
  end

  def test_reflect_format_refuses_an_unknown_kind
    assert_raise(ArgumentError) { @spec.reflect_format('scanf', format_param: 0, kind: :n) }
  end

  def test_a_second_reflect_format_declaration_replaces_the_first_and_prints_one_line
    @spec.reflect_format('fprintf', format_param: 0, kind: :printf)
    out, = capture_output { @spec.reflect_format('fprintf', format_param: 1, kind: :printf) }
    assert_equal(1, out.lines.count)
    assert_equal({ format_param: 1, kind: :printf }, @spec.reflect_format_declarations['fprintf'])
  end
end
