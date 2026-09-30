MRuby::Build.new do |conf|
  conf.toolchain :gcc
  conf.cxx.flags << '-std=c++26' << '-freflection'
  conf.gembox 'default'
  conf.gem File.expand_path('..', __dir__)
  conf.gem File.expand_path(__dir__)
end
