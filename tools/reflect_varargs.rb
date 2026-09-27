# The API of a gem's mrbgem.rake, and of a build config's conf.gem
# block, for a C or C++ function whose last parameter is `...`.
#
# reflect_format declares a printf or scanf format string, with the
# arguments of GCC's format attribute; reflect_varargs names a fixed list of trailing types
# for a function whose varargs are not a format string. Either call
# stores one declaration under the function's name. A second call for
# the same function name replaces the first, and MRuby::Gem::Specification
# 246-256 in mruby's lib/mruby/gem.rb (a gem's own mrbgem.rake runs
# before the user's conf.gem block) makes the build config's call the
# second one, so it wins. rake prints one line for every replacement.
#
# The maximum of 16 arguments after the fixed parameters is the limit
# that the generated call instances provide.
#
# Each reflect_varargs call writes build_dir/include/mruby/reflect_varargs.h
# again, and only when its text changes, so an unchanged declaration
# rebuilds nothing. The header specializes
# mrb_cpp_reflector::reflect_varargs_lists for each declared function
# with one reflect_types<...> per list of trailing types; the gem reads
# it by reflection and makes one call instance per list. A source
# includes the header after the header that declares the functions, and
# names a function as C++ names it, with its namespace.
require 'fileutils'

module MRuby
  module Gem
    class Specification
      MRB_CPP_REFLECTOR_MAX_VARARGS = 16

      def reflect_varargs_declarations
        @reflect_varargs_declarations ||= {}
      end

      def reflect_format_declarations
        @reflect_format_declarations ||= {}
      end

      # trailing_types is an Array of Arrays: each inner Array is one
      # fixed list of C types for the arguments after the function's
      # named parameters, and becomes one generated call instance.
      def reflect_varargs(function_name, trailing_types)
        function_name = function_name.to_s
        unless trailing_types.is_a?(Array) && trailing_types.all? { |types| types.is_a?(Array) && types.all? { |type| type.is_a?(String) } }
          raise ArgumentError, "reflect_varargs: #{function_name} takes an Array of Arrays of C++ type names"
        end
        trailing_types.each do |types|
          if types.length > MRB_CPP_REFLECTOR_MAX_VARARGS
            raise ArgumentError, "reflect_varargs: #{function_name} names #{types.length} types after its fixed parameters, more than the #{MRB_CPP_REFLECTOR_MAX_VARARGS} the generated call instances cover"
          end
        end
        replace_reflect_declaration(reflect_varargs_declarations, function_name, trailing_types, 'reflect_varargs')
        write_reflect_varargs_header
      end

      def reflect_varargs_header
        "#{build_dir}/include/mruby/reflect_varargs.h"
      end

      # The keywords are the arguments of GCC's
      # format(archetype, string-index, first-to-check) attribute.
      # Both indexes count the parameters from 1. first_to_check 0
      # names a function that takes a va_list, and the gem cannot
      # build a va_list.
      def reflect_format(function_name, archetype:, string_index:, first_to_check:)
        unless %i[printf scanf].include?(archetype)
          raise ArgumentError, "reflect_format: archetype is :printf or :scanf, not #{archetype.inspect}"
        end
        unless string_index.is_a?(Integer) && string_index >= 1
          raise ArgumentError, "reflect_format: string_index counts from 1, not #{string_index.inspect}"
        end
        function_name = function_name.to_s
        if first_to_check == 0
          raise NotImplementedError, "reflect_format: #{function_name} takes a va_list (first_to_check 0), and a va_list cannot be built"
        end
        unless first_to_check.is_a?(Integer) && first_to_check > string_index
          raise ArgumentError, "reflect_format: first_to_check is 0 or greater than string_index, not #{first_to_check.inspect}"
        end
        declaration = { archetype: archetype, string_index: string_index, first_to_check: first_to_check }
        replace_reflect_declaration(reflect_format_declarations, function_name, declaration, 'reflect_format')
      end

      private

      def write_reflect_varargs_header
        text = +"#pragma once\n#include <mruby/reflection.hpp>\n#if defined(__cpp_impl_reflection)\nnamespace mrb_cpp_reflector {\n"
        reflect_varargs_declarations.sort.each do |name, lists|
          qualified = name.start_with?('::') ? name : "::#{name}"
          types = lists.map { |list| "^^reflect_types<#{list.join(', ')}>" }.join(', ')
          text << "template <>\ninline constexpr std::span<const std::meta::info> reflect_varargs_lists<^^#{qualified}> =\n"
          text << "    std::define_static_array(std::array<std::meta::info, #{lists.size}>{#{types}});\n"
        end
        text << "}\n#endif\n"
        header = reflect_varargs_header
        FileUtils.mkdir_p(File.dirname(header))
        File.write(header, text) unless File.exist?(header) && File.read(header) == text
        include_dir = File.dirname(File.dirname(header))
        cxx.include_paths << include_dir unless cxx.include_paths.include?(include_dir)
      end

      def replace_reflect_declaration(declarations, function_name, declaration, caller_name)
        if declarations.key?(function_name)
          puts "#{caller_name}: replaced the declaration of #{function_name} (#{declarations[function_name].inspect}) with #{declaration.inspect}"
        end
        declarations[function_name] = declaration
      end
    end
  end
end
