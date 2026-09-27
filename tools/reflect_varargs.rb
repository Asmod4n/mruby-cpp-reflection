# The API of a gem's mrbgem.rake, and of a build config's conf.gem
# block, for a C or C++ function whose last parameter is `...`.
#
# reflect_format names the parameter that carries a printf or scanf
# format string; reflect_varargs names a fixed list of trailing types
# for a function whose varargs are not a format string. Either call
# stores one declaration under the function's name. A second call for
# the same function name replaces the first, and MRuby::Gem::Specification
# 246-256 in mruby's lib/mruby/gem.rb (a gem's own mrbgem.rake runs
# before the user's conf.gem block) makes the build config's call the
# second one, so it wins. rake prints one line for every replacement.
#
# The maximum of 16 arguments after the fixed parameters is the limit
# that the generated call instances (a separate unit) provide.
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
        trailing_types.each do |types|
          if types.length > MRB_CPP_REFLECTOR_MAX_VARARGS
            raise ArgumentError, "reflect_varargs: #{function_name} names #{types.length} types after its fixed parameters, more than the #{MRB_CPP_REFLECTOR_MAX_VARARGS} the generated call instances cover"
          end
        end
        replace_reflect_declaration(reflect_varargs_declarations, function_name, trailing_types, 'reflect_varargs')
      end

      # kind is :printf for a function whose format string reads its
      # varargs by value, :scanf for one that reads them through a
      # pointer. format_param is the 0-based index, in the function's
      # full parameter list, of the parameter that carries the format
      # string.
      def reflect_format(function_name, format_param:, kind:)
        unless %i[printf scanf].include?(kind)
          raise ArgumentError, "reflect_format: kind is :printf or :scanf, not #{kind.inspect}"
        end
        function_name = function_name.to_s
        declaration = { format_param: format_param, kind: kind }
        replace_reflect_declaration(reflect_format_declarations, function_name, declaration, 'reflect_format')
      end

      private

      def replace_reflect_declaration(declarations, function_name, declaration, caller_name)
        if declarations.key?(function_name)
          puts "#{caller_name}: replaced the declaration of #{function_name} (#{declarations[function_name].inspect}) with #{declaration.inspect}"
        end
        declarations[function_name] = declaration
      end
    end
  end
end
