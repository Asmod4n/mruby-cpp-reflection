MRuby::Gem::Specification.new('mruby-cpp-reflection-rake_only') do |spec|
  spec.license = 'MPL-2'
  spec.authors = 'Hendrik Beskow'
  spec.summary = 'A gem whose mrbgem.rake is its only file: spec.reflect writes all of its sources'
  spec.add_dependency 'mruby-cpp-reflection'
  spec.cxx.include_paths << File.expand_path('../src', spec.dir)
  spec.reflect 'rake_only', headers: ['rake_only_library.hpp'], c: <<~C, cxx: <<~CXX
    int rake_only_c_answer(void) { return 42; }
  C
    namespace rake_only {
    using Corners = Pair<Vec2>;
    }

    #if defined(__cpp_impl_reflection)
    #include <string_view>
    namespace rake_only_facts {
    consteval std::meta::info macro_named(const std::string_view name)
    {
        for (const std::meta::info m : std::meta::members_of(^^mruby::cpp_reflection::macros, std::meta::access_context::current()))
            if (std::meta::identifier_of(m) == name) return m;
        return {};
    }
    consteval bool format_is(const std::meta::info function, const std::string_view archetype, const unsigned string_index, const unsigned first_to_check)
    {
        const auto format = mruby::cpp_reflection::reflect_format_attribute(function);
        return format && format->archetype == archetype && format->string_index == string_index && format->first_to_check == first_to_check;
    }
    }
    namespace rake_only {
    constexpr bool array_parameter_has_its_extent = mruby::cpp_reflection::reflect_parameter_extent(^^::rake_only_sum_of_three, 0) == 3;
    constexpr bool va_list_parameter_has_no_extent = mruby::cpp_reflection::reflect_parameter_extent(^^::rake_only_vformat, 1) == 0;
    constexpr bool format_attribute_is_read = rake_only_facts::format_is(^^::rake_only_format, "printf", 1, 2);
    constexpr bool function_without_format_has_none = !mruby::cpp_reflection::reflect_format_attribute(^^::rake_only_vformat);
    constexpr bool macro_number_is_read = std::meta::extract<const int &>(rake_only_facts::macro_named("RAKE_ONLY_ANSWER")) == 42;
    constexpr bool macro_string_is_read = std::string_view(std::meta::extract<const char *const &>(rake_only_facts::macro_named("RAKE_ONLY_NAME"))) == "rake_only";
    constexpr bool macro_call_is_left_out = rake_only_facts::macro_named("RAKE_ONLY_NOT_A_CONSTANT") == std::meta::info{};
    }
    #endif
  CXX
end
