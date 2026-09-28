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
# rebuilds nothing. The header specializes mruby::cpp_reflection::varargs
# for each declared function with one std::tuple<...> per list of
# trailing types, as a source of C++ does it by hand; the gem reads it by
# reflection and makes one call instance per list. A source includes the
# header after the header that declares the functions, and names a
# function as C++ names it, with its namespace.

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
        text = +"#pragma once\n#include <mruby/cpp_reflection_lifetime.hpp>\n#include <tuple>\n#if defined(__cpp_impl_reflection)\n"
        reflect_varargs_declarations.sort.each do |name, lists|
          qualified = name.start_with?('::') ? name : "::#{name}"
          types = lists.map { |list| "^^std::tuple<#{list.join(', ')}>" }.join(', ')
          text << "template <>\ninline constexpr auto mruby::cpp_reflection::varargs<^^#{qualified}> = std::array{#{types}};\n"
        end
        text << "#endif\n"
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
# mruby::cpp_reflection::object_lifetime for each declared class with the
# words of <mruby/cpp_reflection_lifetime.hpp>, as a source of C++ does it by hand. A
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
        add(word: :deallocator, function: function_name(function), results_of: allocators.map { |a| function_name(a) })
      end

      def shared_ownership(increment:, decrement:)
        add(word: :shared_ownership, increment: function_name(increment), decrement: function_name(decrement))
      end

      def words_of(block)
        instance_eval(&block)
        pair_allocators
        @words
      end

      private

      def function_name(function)
        return function if function.is_a?(::Symbol) || function.is_a?(::String)
        ::Kernel.raise ::TypeError, "reflect_object_lifetime #{@class_name}: a word names a function of the class by Symbol and a function beside it by its C++ name, not #{function.inspect}"
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
        position.is_a?(Integer) ? position.to_s : "\"#{position}\""
      end

      def reflect_object_lifetime_function_text(class_name, function)
        return "^^#{function.start_with?('::') ? function : "::#{function}"}" if function.is_a?(String)
        return "^^#{class_name}" if function == :initialize
        "^^#{class_name}::#{function}"
      end

      def reflect_object_lifetime_answer_text(answer)
        answer.nil? ? 'nullptr' : answer.to_s
      end

      def reflect_object_lifetime_word_texts(class_name, word)
        function = word[:function] && reflect_object_lifetime_function_text(class_name, word[:function])
        call = "mruby::cpp_reflection::#{word[:word]}"
        case word[:word]
        when :takes_ownership
          given = { of: word[:of], by: word[:by] }.compact.map { |k, v| ".#{k} = #{reflect_object_lifetime_parameter_text(v)}" }
          ["#{call}(#{function}, {#{given.join(', ')}})"]
        when :ends_lifetime, :retains
          [word[:position].nil? ? "#{call}(#{function})" : "#{call}(#{function}, #{reflect_object_lifetime_parameter_text(word[:position])})"]
        when :errors
          given = case word[:test]
                  when :success then ".success = #{reflect_object_lifetime_answer_text(word[:expected])}"
                  when :negative then '.error = mruby::cpp_reflection::negative'
                  else ".error = #{reflect_object_lifetime_answer_text(word[:expected])}"
                  end
          ["#{call}(#{function}, {#{given}, .sets_errno = #{word[:sets_errno]}})"]
        when :stack_reserve then ["#{call}(#{function}, #{word[:stack_reserve]})"]
        when :threadsafe then ["#{call}(#{function}, false)"]
        when :allocator
          [word[:output_parameter].nil? ? "#{call}(#{function})" : "#{call}(#{function}, {.output_parameter = #{reflect_object_lifetime_parameter_text(word[:output_parameter])}})"]
        when :deallocator
          return ["#{call}(#{function})"] if word[:results_of].empty?
          word[:results_of].map { |a| "#{call}(#{function}, {.results_of = #{reflect_object_lifetime_function_text(class_name, a)}})" }
        when :shared_ownership
          ["#{call}({.increment = #{reflect_object_lifetime_function_text(class_name, word[:increment])}, .decrement = #{reflect_object_lifetime_function_text(class_name, word[:decrement])}})"]
        end
      end

      def write_reflect_object_lifetime_header
        text = +"#pragma once\n#include <mruby/cpp_reflection_lifetime.hpp>\n#if defined(__cpp_impl_reflection)\n"
        reflect_object_lifetime_declarations.sort.each do |name, words|
          qualified = name.start_with?('::') ? name : "::#{name}"
          texts = words.flat_map { |w| reflect_object_lifetime_word_texts(qualified, w) }
          text << "template <>\ninline constexpr auto mruby::cpp_reflection::object_lifetime<^^#{qualified}> = "
          text << (texts.empty? ? "std::array<mruby::cpp_reflection::reflect_object_lifetime_word, 0>{};\n" : "std::array{\n#{texts.map { |t| "    #{t},\n" }.join}};\n")
        end
        text << "#endif\n"
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
          #include <mruby/cpp_reflection.hpp>

          #if defined(__cpp_impl_reflection)
          constexpr auto mrb_#{funcname}_reflected = mruby::cpp_reflection::reflect_with_signature_types<mruby::cpp_reflection::reflect<#{listed}>()>();

          extern "C" void mrb_#{funcname}_gem_init(mrb_state *mrb)
          {
              mruby::cpp_reflection::reflect_define<mrb_#{funcname}_reflected>(mrb);
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

# conf.objcopy = 'path' names the objcopy of a build. The default is the
# objcopy beside the C++ compiler of the toolchain.
module MRuby
  class Build
    attr_writer :objcopy

    def objcopy
      @objcopy ||= if toolchains.include?('clang') then `#{cxx.command} -print-prog-name=llvm-objcopy`.chomp
                   else cxx.command.sub(/(?:g\+\+|gcc|c\+\+)(?:-[\d.]+)?\z/, 'objcopy')
                   end
    end
  end
end

# The generator of the virtual overriders. The build compiles each C++
# source of src/, of test/ in a build with tests, and the source that
# spec.reflect writes, of every gem that depends on this gem, twice.
#
# A first compile of the source, with MRB_CPP_REFLECTOR_GENERATE, writes
# the overriders of the classes it lists with reflect_options
# virtual_overriders into the section
# .mrb_cpp_reflector_virtual_overriders of its object file. A source
# without such a class writes no section. objcopy copies the section
# out. The text of the overriders runs the compile from its own action,
# so the compile is no prerequisite of a product.
# include_virtual_overriders.cpp compiles the source with its overriders,
# in place of the source's own object, in the object list that
# lib/mruby/gem.rb already gave to libmruby. Nothing of it enters the
# tree.

module MRuby
  module Gem
    class Specification
      def reflect_generate
        return unless build.cxx.flags.flatten.any? { |f| f.to_s == '-freflection' }
        reflection = build.gems.detect { |g| g.name == 'mruby-cpp-reflection' }
        return unless reflection && dependencies.any? { |d| d[:gem] == reflection.name }
        sources = Dir.glob("#{dir}/src/*.{cpp,cxx,cc}")
        sources += Dir.glob("#{dir}/test/*.{cpp,cxx,cc}") if build.test_enabled?
        sources << reflect_source if File.exist?(reflect_source)
        sources.each { |source| reflect_virtual_overriders(reflection, source) }
      end

      private

      def reflect_virtual_overriders(reflection, source)
        out = "#{build_dir}/reflect_generate/#{File.basename(File.dirname(source))}/#{File.basename(source, '.*')}"
        msvc = build.toolchains.include?('visualcpp')
        quoted = ->(path) { msvc ? %(\\"#{path}\\") : %('"#{path}"') }
        printed = "#{out}/print_virtual_overriders#{build.exts.object}"
        section = "#{out}/virtual_overriders.section"
        text = "#{out}/virtual_overriders.inc"
        object = "#{out}/include_virtual_overriders#{build.exts.object}"
        replaced = source.start_with?("#{self.dir}/") ? objfile(source.relative_path_from(self.dir).pathmap("#{build_dir}/%X")) : objfile(source.pathmap('%X'))
        headers = %w[cpp_reflection.hpp cpp_reflection_lifetime.hpp reflect_members.hpp].map { |h| "#{reflection.dir}/include/mruby/#{h}" }
        file printed => [source, "#{reflection.dir}/src/print_virtual_overriders.cpp", *headers] do |t|
          cxx.run t.name, "#{reflection.dir}/src/print_virtual_overriders.cpp",
                  ["MRB_CPP_REFLECTOR_SOURCE=#{quoted.(source)}", 'MRB_NO_PRESYM'], [], [msvc ? '/GL-' : '-fno-lto']
        end
        file section => printed do |t|
          sh "#{build.objcopy} -O binary --only-section=.mrb_cpp_reflector_virtual_overriders #{printed} #{t.name}"
        end
        file text do |t|
          Rake::Task[section].invoke
          overriders = File.read(section).delete("\0").split(/^(?=template <> struct)/).uniq.join
          File.write(t.name, overriders) unless File.exist?(t.name) && File.read(t.name) == overriders
        end
        Rake::Task[text].define_singleton_method(:needed?) { true }
        file object => [text, source, "#{reflection.dir}/src/include_virtual_overriders.cpp", *headers] do |t|
          cxx.run t.name, "#{reflection.dir}/src/include_virtual_overriders.cpp",
                  ["MRB_CPP_REFLECTOR_SOURCE=#{quoted.(source)}", "MRB_CPP_REFLECTOR_VIRTUAL_OVERRIDERS=#{quoted.(text)}"]
        end
        if test_objs.include?(replaced)
          test_objs.map! { |o| o == replaced ? object : o }
        else
          objs.map! { |o| o == replaced ? object : o }
        end
      end
    end

    Specification.prepend(Specification.const_set(:ReflectGenerate, Module.new do
      def setup_compilers
        super
        reflect_generate
      end
    end)) unless Specification.const_defined?(:ReflectGenerate, false)
  end
end

# search_package runs `pkg-config --modversion` with the query that
# mruby's search_package (lib/mruby/gem.rb) builds, then calls mruby's
# search_package with the same arguments and answers its answer. When
# both find the package, the spec records the name, the query and the
# version. pkgconf prints one line per condition of the query; the
# version is recorded only when all lines are equal.

module MRuby
  module Gem
    class Specification
      def reflect_packages
        @reflect_packages ||= []
      end
    end

    Specification.prepend(Specification.const_set(:ReflectSearchPackage, Module.new do
      def search_package(name, version_query = nil, *rest, **options, &block)
        query = version_query ? "#{name} #{version_query}" : name.to_s
        output = begin
          IO.popen(['pkg-config', '--modversion', query], &:read)
        rescue SystemCallError
          nil
        end
        found = output && $?.success?
        answer = super
        if answer == true && found
          lines = output.lines.map(&:strip).uniq
          version = lines.size == 1 ? lines.first : nil
          reflect_packages << { name: name.to_s, query: version_query, version: version }
        end
        answer
      end
    end)) unless Specification.const_defined?(:ReflectSearchPackage, false)
  end
end

MRuby::Gem::Specification.new('mruby-cpp-reflection') do |spec|
  spec.export_include_paths << "#{spec.dir}/include" if spec.respond_to?(:export_include_paths)
  spec.license = 'MPL-2'
  spec.authors = 'Hendrik Beskow'
  spec.version = '0.1.0'
  spec.summary = 'A C++ class is a Ruby class: C++26 reflection defines it, methods, attributes, overloads and all'
  spec.add_dependency 'mruby-proc-ext', core: 'mruby-proc-ext'
  spec.add_dependency 'mruby-c-ext-helpers', github: 'Asmod4n/mruby-c-ext-helpers'
  spec.build.enable_cxx_exception
  relink = "#{spec.dir}/bin/relink"
  spec.build.linker.command = %("#{RbConfig.ruby}" "#{relink}" "#{spec.build.linker.command}") unless spec.build.linker.command.include?(relink)
end
