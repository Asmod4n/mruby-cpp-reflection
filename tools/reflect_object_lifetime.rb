# The API of a gem's mrbgem.rake, and of a build config's conf.gem
# block, that declares the lifetime of the objects of a C++ class where
# its types do not say it:
#
#   spec.reflect_object_lifetime 'c_library::handle' do
#     allocator :handle_make
#     deallocator :handle_close
#   end
#
# The block names the functions of the class, or the functions of the
# scope around the class that take or make the class, with these words:
# takes_ownership, ends_lifetime, retains, errors, stack_reserve, threadsafe,
# allocator, deallocator and shared_ownership. Rake checks what the
# words say without the C++ types; the C++ side checks the rest when it
# compiles the class.
#
# A second call for the same class replaces the first. MRuby::Gem::Specification
# 246-256 in mruby's lib/mruby/gem.rb runs a gem's own mrbgem.rake before
# the user's conf.gem block, so the build config's call is the second one
# and wins. rake prints one line for every replacement.
#
# Each call writes build_dir/include/mruby/reflect_object_lifetimes.h again, and
# only when its text changes. The header specializes
# mrb_cpp_reflector::reflect_object_lifetime_words for each declared class. A
# source includes it after the headers that declare every class it names.
# Ruby code has no way to declare or change a lifetime at runtime.
require 'fileutils'

module MRuby
  module Gem
    class ReflectObjectLifetimeDeclaration < BasicObject
      def initialize(class_name)
        @class_name = class_name
        @words = []
      end

      # of: names the object whose ownership the function takes, by: the
      # object that takes it, as a parameter number from 0 or a parameter
      # identifier; the one left out is the receiver.
      def takes_ownership(function, of: nil, by: nil)
        if of.nil? && by.nil?
          ::Kernel.raise ::ArgumentError, "reflect_object_lifetime #{@class_name}: takes_ownership #{function.inspect} names the of: or the by: parameter"
        end
        add(word: :takes_ownership, function: function_name(function), of: parameter(of), by: parameter(by))
      end

      def ends_lifetime(function, position = nil)
        add(word: :ends_lifetime, function: function_name(function), position: parameter(position))
      end

      def retains(function, position)
        add(word: :retains, function: function_name(function), position: parameter(position))
      end

      def errors(function, success: :none, error: :none, sets_errno: false)
        if (success == :none) == (error == :none)
          ::Kernel.raise ::ArgumentError, "reflect_object_lifetime #{@class_name}: errors #{function.inspect} names either success: or error:"
        end
        unless sets_errno == true || sets_errno == false
          ::Kernel.raise ::TypeError, "reflect_object_lifetime #{@class_name}: sets_errno is true or false"
        end
        test, expected = if success != :none then [:success, answer(success)]
                         elsif error == :negative then [:negative, nil]
                         else [:error, answer(error)]
                         end
        add(word: :errors, function: function_name(function), test: test, expected: expected, sets_errno: sets_errno)
      end

      def stack_reserve(function, bytes)
        unless bytes.is_a?(::Integer) && bytes > 0
          ::Kernel.raise ::ArgumentError, "reflect_object_lifetime #{@class_name}: a stack reserve is a positive number of bytes"
        end
        add(word: :stack_reserve, function: function_name(function), stack_reserve: bytes)
      end

      def threadsafe(function, value)
        ::Kernel.raise ::ArgumentError, "reflect_object_lifetime #{@class_name}: threadsafe takes :no" unless value == :no
        add(word: :threadsafe, function: function_name(function))
      end

      def allocator(function, output_parameter: nil)
        add(word: :allocator, function: function_name(function), output_parameter: parameter(output_parameter))
      end

      def deallocator(function, results_of: [])
        allocators = results_of.is_a?(::Array) ? results_of : [results_of]
        unless allocators.all? { |a| a.is_a?(::Symbol) }
          ::Kernel.raise ::TypeError, "reflect_object_lifetime #{@class_name}: results_of names allocators by Symbol"
        end
        add(word: :deallocator, function: function_name(function), results_of: allocators.map(&:to_s))
      end

      def shared_ownership(increment:, decrement:)
        unless increment.is_a?(::Symbol) && decrement.is_a?(::Symbol)
          ::Kernel.raise ::TypeError, "reflect_object_lifetime #{@class_name}: increment: and decrement: name functions by Symbol"
        end
        add(word: :shared_ownership, increment: increment.to_s, decrement: decrement.to_s)
      end

      def words_of(block)
        instance_eval(&block)
        pair_allocators
        @words
      end

      private

      def function_name(function)
        ::Kernel.raise ::TypeError, "reflect_object_lifetime #{@class_name}: a word names a function by Symbol, not #{function.inspect}" unless function.is_a?(::Symbol)
        function.to_s
      end

      def parameter(position)
        return nil if position.nil?
        return position if position.is_a?(::Integer) && position >= 0
        return position.to_s if position.is_a?(::Symbol)
        ::Kernel.raise ::TypeError, "reflect_object_lifetime #{@class_name}: a parameter is named by its number or its identifier, not #{position.inspect}"
      end

      def answer(given)
        return given if given.nil? || given.is_a?(::Integer)
        ::Kernel.raise ::TypeError, "reflect_object_lifetime #{@class_name}: an answer is an Integer or nil, not #{given.inspect}"
      end

      def add(word)
        same = @words.find do |w|
          w[:word] == word[:word] && w[:function] == word[:function] && (word[:word] != :retains || w[:position] == word[:position])
        end
        ::Kernel.raise ::ArgumentError, "reflect_object_lifetime #{@class_name}: #{word[:word]} is already declared#{word[:function] && " for #{word[:function]}"}" if same
        @words << word
        nil
      end

      def pair_allocators
        deallocators = @words.select { |w| w[:word] == :deallocator }
        allocators = @words.select { |w| w[:word] == :allocator }.map { |w| w[:function] }
        allocators.each do |a|
          next if deallocators.any? { |d| d[:results_of].empty? || d[:results_of].include?(a) }
          ::Kernel.raise ::ArgumentError, "reflect_object_lifetime #{@class_name}: the allocator #{a} has no deallocator"
        end
        deallocators.each do |d|
          stray = d[:results_of].find { |a| !allocators.include?(a) }
          ::Kernel.raise ::ArgumentError, "reflect_object_lifetime #{@class_name}: the deallocator #{d[:function]} names #{stray}, which is no allocator of this class" if stray
        end
      end
    end

    class Specification
      def reflect_object_lifetime_declarations
        @reflect_object_lifetime_declarations ||= {}
      end

      def reflect_object_lifetime(class_name, &block)
        class_name = class_name.to_s
        raise ArgumentError, "reflect_object_lifetime: #{class_name} takes a block with the words of its lifetime" unless block
        words = ReflectObjectLifetimeDeclaration.new(class_name).words_of(block)
        replace_reflect_declaration(reflect_object_lifetime_declarations, class_name, words, 'reflect_object_lifetime')
        write_reflect_object_lifetime_header
      end

      def reflect_object_lifetime_header
        "#{build_dir}/include/mruby/reflect_object_lifetimes.h"
      end

      private

      def reflect_object_lifetime_parameter_text(position)
        return '{}' if position.nil?
        return "{.number = #{position}}" if position.is_a?(Integer)
        "{.identifier = std::define_static_string(\"#{position}\")}"
      end

      def reflect_object_lifetime_word_texts(word)
        results_of = word[:word] == :deallocator && !word[:results_of].empty? ? word[:results_of] : [nil]
        results_of.map do |allocator|
          fields = [".word = reflect_word::#{word[:word]}"]
          fields << ".function = std::define_static_string(\"#{word[:function]}\")" if word[:function]
          fields << ".of = #{reflect_object_lifetime_parameter_text(word[:of])}" if word[:of]
          fields << ".by = #{reflect_object_lifetime_parameter_text(word[:by])}" if word[:by]
          fields << ".position = #{reflect_object_lifetime_parameter_text(word[:position])}" if word[:position]
          fields << ".output_parameter = #{reflect_object_lifetime_parameter_text(word[:output_parameter])}" if word[:output_parameter]
          fields << ".results_of = std::define_static_string(\"#{allocator}\")" if allocator
          fields << ".increment = std::define_static_string(\"#{word[:increment]}\")" if word[:increment]
          fields << ".decrement = std::define_static_string(\"#{word[:decrement]}\")" if word[:decrement]
          if word[:test]
            fields << ".test = reflect_answer_test::#{word[:test]}"
            fields << (word[:expected].nil? ? '.expects_nil = true' : ".expected = #{word[:expected]}") unless word[:test] == :negative
            fields << ".sets_errno = #{word[:sets_errno]}"
          end
          fields << ".stack_reserve = #{word[:stack_reserve]}" if word[:stack_reserve]
          "{#{fields.join(', ')}}"
        end
      end

      def write_reflect_object_lifetime_header
        text = +"#pragma once\n#include <mruby/reflection.hpp>\n#if defined(__cpp_impl_reflection)\nnamespace mrb_cpp_reflector {\n"
        reflect_object_lifetime_declarations.sort.each do |name, words|
          qualified = name.start_with?('::') ? name : "::#{name}"
          texts = words.flat_map { |w| reflect_object_lifetime_word_texts(w) }
          text << "template <>\ninline constexpr std::span<const reflect_object_lifetime_word> reflect_object_lifetime_words<^^#{qualified}> =\n"
          text << "    std::define_static_array(std::array<reflect_object_lifetime_word, #{texts.size}>{\n"
          text << texts.map { |t| "        reflect_object_lifetime_word#{t},\n" }.join
          text << "    });\n"
        end
        text << "}\n#endif\n"
        header = reflect_object_lifetime_header
        FileUtils.mkdir_p(File.dirname(header))
        File.write(header, text) unless File.exist?(header) && File.read(header) == text
        include_dir = File.dirname(File.dirname(header))
        cxx.include_paths << include_dir unless cxx.include_paths.include?(include_dir)
      end
    end
  end
end
