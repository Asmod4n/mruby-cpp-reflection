# The two release builds of mruby that a benchmark of this gem links:
# g++ at -O2 and at -Os, both for x86-64-v4 and with the alignment flags,
# so a relink does not move a hot loop. clang has no reflection, so there
# is no clang build.
%w[O2 Os].each do |level|
  MRuby::Build.new("bench_#{level.downcase}") do |conf|
    conf.toolchain :gcc
    conf.enable_cxx_exception
    flags = ["-#{level}", '-march=x86-64-v4', '-falign-functions=64', '-falign-loops=64', '-falign-jumps=64']
    conf.cc.flags << flags
    conf.cxx.flags << flags << '-std=c++26' << '-freflection'
    conf.gem core: 'mruby-compiler'
    conf.gem File.expand_path('..', __dir__)
  end
end
