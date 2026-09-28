MRuby::Build.new do |conf|
  conf.toolchain :gcc
  conf.enable_debug
  conf.enable_test
  conf.enable_bintest
  conf.cxx.flags << '-std=c++26' << '-freflection'
  conf.gem File.expand_path('..', __dir__)
  conf.gem File.expand_path(__dir__)
end
