MRuby::Build.new do |conf|
  conf.toolchain :gcc
  conf.gem core: 'mruby-bin-mrbc'
  conf.enable_debug
  conf.enable_test
  conf.cxx.flags << '-std=c++26' << '-freflection'
  conf.gem File.expand_path('..', __dir__)
end
