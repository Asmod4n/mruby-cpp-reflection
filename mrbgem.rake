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

# spec.reflect writes the C++ of a gem that binds a library, so the gem
# carries no C++ of its own. It takes the headers of the library and an
# allowlist of scopes: namespaces and classes, named as C++ names them.
# The generated source includes the headers, then the varargs header of
# the gem if spec.reflect_varargs wrote one, the lifetime header if
# spec.reflect_object_lifetime wrote one, then the gem, and defines
# gem_init and gem_final. reflect_with_signature_types adds every class
# and enum that a function of a listed scope takes or answers, so a
# handle type is a Ruby class before any function has made one.
#
# The source goes to build_dir/reflect/reflect.cpp, and is written again
# only when its text changes.

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
          #if __has_include(<mruby/reflect_object_lifetimes.h>)
          #include <mruby/reflect_object_lifetimes.h>
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

# Presyms for the names that C++26 reflection produces. A gem calls
#   reflect_presyms(spec, "#{spec.dir}/tools/reflect_presyms/reflect_presyms.cpp")
# with a program that prints mrb_cpp_reflector::reflect_presyms_header<^^T...>()
# for its reflected types. The program is built and run before the presym
# scan; its table goes to build/<name>/include/mruby/presym/reflect.h, and
# a generated source of the gem includes it, so the scanner reads the
# MRB_SYM tokens like any other source's. Nothing of it enters the tree.
def reflect_presyms(spec, runner_src)
  return unless spec.build.cxx.flags.flatten.any? { |f| f.to_s == '-freflection' }
  runner_bin = "#{spec.build_dir}/reflect_presyms/runner"
  header = "#{spec.build.build_dir}/include/mruby/presym/reflect.h"
  source = "#{spec.build_dir}/reflect_presyms/reflect_presyms.cpp"
  reflection = spec.build.gems.detect { |g| g.name == 'mruby-cpp-reflection' }
  helpers = spec.build.gems.detect { |g| g.name == 'mruby-c-ext-helpers' }
  file runner_bin => [runner_src, "#{reflection.dir}/include/mruby/reflect_presyms.hpp"] do |t|
    FileUtils.mkdir_p(File.dirname(t.name))
    incs = (spec.build.cxx.include_paths + ["#{reflection.dir}/include", "#{helpers.dir}/include", "#{spec.build.build_dir}/include"]).map { |i| "-I#{i}" }.join(' ')
    sh "#{spec.build.cxx.command} #{spec.build.cxx.flags.flatten.join(' ')} #{incs} #{runner_src} -o #{t.name}"
  end
  file header => runner_bin do |t|
    FileUtils.mkdir_p(File.dirname(t.name))
    sh "#{runner_bin} > #{t.name}"
  end
  file source => header do |t|
    File.write(t.name, "#include <mruby.h>\n#include <mruby/presym/reflect.h>\n")
  end
  obj = spec.objfile(source.pathmap("#{spec.build_dir}/reflect_presyms/%n"))
  file obj => source
  spec.objs << obj
end

# Overriders for the virtual functions of the classes a source file lists
# with reflect_options virtual_overriders. A first compile of the source
# file, with MRB_CPP_REFLECTOR_GENERATE, writes the text of the overriders
# into its object file between two marker lines. That text goes to
# spec.build_dir, and include_virtual_overriders.cpp compiles the source
# file with it, in place of the source file's own object.
def reflect_virtual_overriders(spec, source)
  return unless spec.build.cxx.flags.flatten.any? { |f| f.to_s == '-freflection' }
  reflection = spec.build.gems.detect { |g| g.name == 'mruby-cpp-reflection' }
  source = File.expand_path(source, spec.dir)
  dir = "#{spec.build_dir}/virtual_overriders/#{File.basename(source, '.*')}"
  msvc = spec.build.toolchains.include?('visualcpp')
  quoted = ->(path) { msvc ? %(\\"#{path}\\") : %('"#{path}"') }
  printed = "#{dir}/print_virtual_overriders#{spec.build.exts.object}"
  text = "#{dir}/virtual_overriders.inc"
  object = "#{dir}/include_virtual_overriders#{spec.build.exts.object}"
  replaced = spec.objfile(source.relative_path_from(spec.dir).pathmap("#{spec.build_dir}/%X"))
  file printed => [source, "#{reflection.dir}/tools/print_virtual_overriders/print_virtual_overriders.cpp", "#{reflection.dir}/include/mruby/reflection.hpp"] do |t|
    spec.cxx.run t.name, "#{reflection.dir}/tools/print_virtual_overriders/print_virtual_overriders.cpp", ["MRB_CPP_REFLECTOR_SOURCE=#{quoted.(source)}"], [], [msvc ? '/GL-' : '-fno-lto']
  end
  file text => printed do |t|
    blocks = File.binread(printed).scan(/BEGIN_MRB_CPP_REFLECTOR_VIRTUAL_OVERRIDERS\n(.*?)END_MRB_CPP_REFLECTOR_VIRTUAL_OVERRIDERS\n/m).flatten
    File.write(t.name, blocks.flat_map { |b| b.split(/^(?=template <> struct)/) }.uniq.join)
  end
  file object => [text, "#{reflection.dir}/tools/include_virtual_overriders/include_virtual_overriders.cpp"] do |t|
    spec.cxx.run t.name, "#{reflection.dir}/tools/include_virtual_overriders/include_virtual_overriders.cpp",
                 ["MRB_CPP_REFLECTOR_SOURCE=#{quoted.(source)}", "MRB_CPP_REFLECTOR_VIRTUAL_OVERRIDERS=#{quoted.(text)}"]
  end
  if spec.test_objs.include?(replaced)
    spec.test_objs = spec.test_objs.map { |o| o == replaced ? object : o }
  else
    spec.objs = spec.objs.map { |o| o == replaced ? object : o }
  end
end

MRuby::Gem::Specification.new('mruby-cpp-reflection') do |spec|
  spec.export_include_paths << "#{spec.dir}/include" if spec.respond_to?(:export_include_paths)
  spec.license = 'MPL-2'
  spec.authors = 'Hendrik Beskow'
  spec.version = '0.1.0'
  spec.summary = 'A C++ class is a Ruby class: C++26 reflection defines it, methods, attributes, overloads and all'
  spec.add_dependency 'mruby-proc-ext', core: 'mruby-proc-ext'
  spec.add_dependency 'mruby-c-ext-helpers', github: 'Asmod4n/mruby-c-ext-helpers', branch: 'mrb-value-to'
  spec.add_test_dependency 'mruby-string-ext', core: 'mruby-string-ext'
  spec.add_test_dependency 'mruby-errno', core: 'mruby-errno'
  spec.add_test_dependency 'mruby-metaprog', core: 'mruby-metaprog'
  spec.add_test_dependency 'mruby-class-ext', core: 'mruby-class-ext'
  spec.add_test_dependency 'mruby-method', core: 'mruby-method'
  spec.add_test_dependency 'mruby-enumerator', core: 'mruby-enumerator'
  spec.build.enable_cxx_exception
  relink = "#{spec.dir}/bin/relink"
  spec.build.linker.command = %("#{RbConfig.ruby}" "#{relink}" "#{spec.build.linker.command}") unless spec.build.linker.command.include?(relink)
end
