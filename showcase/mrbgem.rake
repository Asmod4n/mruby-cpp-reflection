MRuby::Gem::Specification.new('mruby-cpp-reflection-showcase') do |spec|
  spec.license = 'MPL-2'
  spec.authors = 'Hendrik Beskow'
  spec.summary = 'A small C++ library that mruby-cpp-reflection makes a Ruby library'
  spec.add_dependency 'mruby-cpp-reflection'
  spec.cxx.include_paths << "#{spec.dir}/include"
  spec.reflect 'showcase', headers: ['showcase.hpp']
end
