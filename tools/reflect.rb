# spec.reflect writes the C++ of a gem that binds a library, so the gem
# carries no C++ of its own. It takes the headers of the library and an
# allowlist of scopes: namespaces and classes, named as C++ names them.
# The generated source includes the headers, then the varargs header of
# the gem if spec.reflect_varargs wrote one, then the gem, and defines
# gem_init and gem_final. reflect_with_signature_types adds every class
# and enum that a function of a listed scope takes or answers, so a
# handle type is a Ruby class before any function has made one.
#
# The source goes to build_dir/reflect/reflect.cpp, and is written again
# only when its text changes.
require 'fileutils'

module MRuby
  module Gem
    class Specification
      def reflect(headers:, scopes:)
        unless headers.is_a?(Array) && !headers.empty? && headers.all? { |h| h.is_a?(String) }
          raise ArgumentError, "reflect: headers is a non-empty Array of header names"
        end
        unless scopes.is_a?(Array) && !scopes.empty? && scopes.all? { |s| s.is_a?(String) }
          raise ArgumentError, "reflect: scopes is a non-empty Array of C++ names"
        end
        source = reflect_source
        text = reflect_source_text(headers, scopes)
        FileUtils.mkdir_p(File.dirname(source))
        File.write(source, text) unless File.exist?(source) && File.read(source) == text
        object = objfile(source.pathmap("%X"))
        objs << object unless objs.include?(object)
      end

      def reflect_source
        "#{build_dir}/reflect/reflect.cpp"
      end

      def reflect_source_text(headers, scopes)
        includes = headers.map { |h| "#include <#{h}>\n" }.join
        listed = scopes.map { |s| "^^#{s.start_with?('::') ? s : "::#{s}"}" }.join(', ')
        <<~CPP
          #{includes.chomp}
          #include <mruby.h>
          #if __has_include(<mruby/reflect_varargs.h>)
          #include <mruby/reflect_varargs.h>
          #endif
          #include <mruby/reflection.hpp>

          #if defined(__cpp_impl_reflection)
          constexpr auto mrb_#{funcname}_reflected = mrb_cpp_reflector::reflect_with_signature_types<mrb_cpp_reflector::reflect<#{listed}>()>();

          extern "C" void mrb_#{funcname}_gem_init(mrb_state *mrb)
          {
              mrb_cpp_reflector::reflect_define<mrb_#{funcname}_reflected>(mrb);
          }
          #else
          extern "C" void mrb_#{funcname}_gem_init(mrb_state *) {}
          #endif

          extern "C" void mrb_#{funcname}_gem_final(mrb_state *) {}
        CPP
      end
    end
  end
end
