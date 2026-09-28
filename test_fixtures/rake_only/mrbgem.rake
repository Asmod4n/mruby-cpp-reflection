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
  CXX
end
