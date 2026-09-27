# Host-side test of the reflect_object_lifetime API. It stubs
# MRuby::Gem::Specification instead of loading mruby's Rakefile,
# because the API under test touches no other part of that class.
# Run with: ruby tools/reflect_object_lifetime_test.rb
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
require_relative 'reflect_object_lifetime'

class ReflectObjectLifetimeTest < Test::Unit::TestCase
  def setup
    @dir = Dir.mktmpdir
    @spec = MRuby::Gem::Specification.new(@dir)
  end

  def teardown
    FileUtils.remove_entry(@dir)
  end

  def declare(name = 'Tree', &block)
    @spec.reflect_object_lifetime(name, &block)
  end

  def header
    File.read(@spec.reflect_object_lifetime_header)
  end

  # The header is all the C++ side reads: one specialization per class,
  # named as C++ names it, with one word per entry.
  def test_the_header_specializes_the_words_of_the_class
    declare('ns::Tree') { takes_ownership :set_parent, by: 0 }
    assert_match(/reflect_object_lifetime_words<\^\^::ns::Tree>/, header)
    assert_match(/\.word = reflect_word::takes_ownership, \.function = std::define_static_string\("set_parent"\), \.by = \{\.number = 0\}/, header)
    assert_equal(["#{@dir}/include"], @spec.cxx.include_paths)
  end

  # Every word reaches the header with its arguments, so that nothing a
  # declaration says is lost on the way to C++.
  def test_every_word_reaches_the_header
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
    assert_match(/reflect_word::takes_ownership, .function = std::define_static_string\("initialize"\), .by = \{.identifier = std::define_static_string\("parent"\)\}/, text)
    assert_match(/reflect_word::ends_lifetime, .function = std::define_static_string\("destroy"\)\}/, text)
    assert_match(/reflect_word::retains, .function = std::define_static_string\("set_layout"\), .position = \{.number = 1\}/, text)
    assert_match(/"open"\), .test = reflect_answer_test::negative, .sets_errno = true/, text)
    assert_match(/"find"\), .test = reflect_answer_test::error, .expects_nil = true, .sets_errno = false/, text)
    assert_match(/"status"\), .test = reflect_answer_test::success, .expected = 0, .sets_errno = false/, text)
    assert_match(/"depth"\), .stack_reserve = 1024/, text)
    assert_match(/reflect_word::threadsafe, .function = std::define_static_string\("start"\)\}/, text)
    assert_match(/"open_handle"\), .output_parameter = \{.identifier = std::define_static_string\("made"\)\}/, text)
    assert_match(/reflect_word::deallocator, .function = std::define_static_string\("close_handle"\)\}/, text)
    assert_match(/.increment = std::define_static_string\("ref"\), .decrement = std::define_static_string\("unref"\)/, text)
    assert_match(/std::array<reflect_object_lifetime_word, 11>/, text)
  end

  # results_of with several allocators gives one entry per allocator,
  # so each entry names one allocator.
  def test_results_of_gives_one_entry_per_allocator
    declare('h') do
      allocator :open
      allocator :make
      deallocator :close, results_of: %i[open make]
    end
    assert_match(/"close"\), .results_of = std::define_static_string\("open"\)/, header)
    assert_match(/"close"\), .results_of = std::define_static_string\("make"\)/, header)
  end

  # An unchanged declaration leaves the header alone, so the build that
  # follows compiles nothing again.
  def test_an_unchanged_declaration_does_not_touch_the_header
    declare { ends_lifetime :destroy, 0 }
    File.utime(Time.at(0), Time.at(0), @spec.reflect_object_lifetime_header)
    capture_output { declare { ends_lifetime :destroy, 0 } }
    assert_equal(Time.at(0), File.mtime(@spec.reflect_object_lifetime_header))
  end

  # A gem's mrbgem.rake runs before the user's conf.gem block
  # (mruby lib/mruby/gem.rb 246-256), so the build config's call is
  # the second one and wins, and the replacement is visible.
  def test_a_second_declaration_replaces_the_first_and_prints_one_line
    declare { ends_lifetime :destroy, 0 }
    out, = capture_output { declare { retains :keep, 0 } }
    assert_equal(1, out.lines.count)
    assert_match(/Tree/, out)
    assert_equal([:retains], @spec.reflect_object_lifetime_declarations['Tree'].map { |w| w[:word] })
    assert_no_match(/destroy/, header)
  end

  def test_no_line_is_printed_for_the_first_declaration
    out, = capture_output { declare { ends_lifetime :destroy, 0 } }
    assert_equal('', out)
  end

  # The block runs on an object that knows only the words, so a word
  # that does not exist is an error and not a method of Kernel.
  def test_an_unknown_word_is_refused
    assert_raise(NoMethodError) { declare { release :set_parent } }
    assert_raise(NoMethodError) { declare { puts :set_parent } }
  end

  def test_a_declaration_takes_a_block
    assert_raise(ArgumentError) { @spec.reflect_object_lifetime('Tree') }
  end

  def test_takes_ownership_names_of_or_by
    assert_raise(ArgumentError) { declare { takes_ownership :set_parent } }
  end

  # A position counts the parameters from 0, as in every other word, and
  # the position left out is the receiver.
  def test_takes_ownership_leaves_out_the_receiver
    declare { takes_ownership :adopt, of: 1 }
    assert_match(/"adopt"\), .of = \{.number = 1\}\}/, header)
  end

  # The words of the earlier runtime declaration are no words now.
  def test_owns_is_no_word
    assert_raise(NoMethodError) { declare { owns :set_parent, owner: 0 } }
  end

  def test_a_parameter_is_a_number_or_an_identifier
    assert_raise(TypeError) { declare { retains :keep, 'first' } }
    assert_raise(TypeError) { declare { retains :keep, -1 } }
    assert_raise(TypeError) { declare { takes_ownership :set_parent, by: 1.5 } }
  end

  def test_a_function_is_named_by_symbol
    assert_raise(TypeError) { declare { ends_lifetime 'destroy' } }
  end

  def test_errors_names_either_success_or_error
    assert_raise(ArgumentError) { declare { errors :open } }
    assert_raise(ArgumentError) { declare { errors :open, success: 0, error: -1 } }
  end

  def test_an_answer_is_an_integer_or_nil
    assert_raise(TypeError) { declare { errors :open, success: 'ok' } }
    assert_raise(TypeError) { declare { errors :open, error: :positive } }
  end

  def test_sets_errno_is_true_or_false
    assert_raise(TypeError) { declare { errors :open, error: :negative, sets_errno: 1 } }
  end

  def test_a_stack_reserve_is_a_positive_number_of_bytes
    assert_raise(ArgumentError) { declare { stack_reserve :depth, 0 } }
    assert_raise(ArgumentError) { declare { stack_reserve :depth, '1k' } }
  end

  def test_threadsafe_takes_only_no
    assert_raise(ArgumentError) { declare { threadsafe :start, :yes } }
  end

  def test_results_of_names_allocators_by_symbol
    assert_raise(TypeError) { declare { allocator(:make); deallocator :close, results_of: 'make' } }
  end

  def test_shared_ownership_names_functions_by_symbol
    assert_raise(TypeError) { declare { shared_ownership increment: 'ref', decrement: :unref } }
  end

  # One function carries one word of each kind; a second one would leave
  # open which of the two holds.
  def test_a_word_is_declared_once_for_a_function
    assert_raise(ArgumentError) { declare { takes_ownership(:set_parent, by: 0); takes_ownership :set_parent, of: 0 } }
    assert_raise(ArgumentError) { declare { ends_lifetime(:destroy, 0); ends_lifetime :destroy } }
    assert_raise(ArgumentError) { declare { errors(:open, success: 0); errors :open, error: nil } }
    assert_raise(ArgumentError) { declare { stack_reserve(:depth, 1); stack_reserve :depth, 2 } }
    assert_raise(ArgumentError) { declare { threadsafe(:start, :no); threadsafe :start, :no } }
    assert_raise(ArgumentError) { declare { retains(:keep, 0); retains :keep, 0 } }
    assert_raise(ArgumentError) { declare { shared_ownership(increment: :ref, decrement: :unref); shared_ownership increment: :ref, decrement: :unref } }
  end

  def test_retains_takes_several_parameters_of_one_function
    assert_nothing_raised { declare { retains(:keep, 0); retains :keep, 1 } }
  end

  # A handle that an allocator made and no deallocator frees would leak,
  # and a deallocator for a handle it did not make would free the wrong
  # memory; both are refused before any code is compiled.
  def test_an_allocator_needs_a_deallocator
    assert_raise(ArgumentError) { declare { allocator :make } }
    assert_raise(ArgumentError) { declare { allocator(:make); allocator(:open); deallocator :close, results_of: :make } }
  end

  def test_results_of_names_only_allocators_of_the_class
    assert_raise(ArgumentError) { declare { allocator(:make); deallocator :close, results_of: %i[make open] } }
  end

  def test_a_general_deallocator_frees_what_every_allocator_made
    assert_nothing_raised { declare { allocator(:make); allocator(:open); deallocator :close } }
  end
end
