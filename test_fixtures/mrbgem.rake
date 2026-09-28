MRuby::Gem::Specification.new('mruby-cpp-reflection-test_fixtures') do |spec|
  spec.license = 'MPL-2'
  spec.authors = 'Hendrik Beskow'
  spec.summary = 'The C++ classes that the tests of mruby-cpp-reflection reflect, and the tests'
  spec.add_dependency 'mruby-cpp-reflection'
  spec.add_dependency 'mruby-compiler', core: 'mruby-compiler'
  spec.add_test_dependency 'mruby-cpp-reflection-rake_only', gemdir: File.expand_path('rake_only', spec.dir)
  spec.add_test_dependency 'mruby-string-ext', core: 'mruby-string-ext'
  spec.add_test_dependency 'mruby-errno', core: 'mruby-errno'
  spec.add_test_dependency 'mruby-metaprog', core: 'mruby-metaprog'
  spec.add_test_dependency 'mruby-class-ext', core: 'mruby-class-ext'
  spec.add_test_dependency 'mruby-method', core: 'mruby-method'
  spec.add_test_dependency 'mruby-enumerator', core: 'mruby-enumerator'
end
