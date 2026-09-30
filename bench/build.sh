#!/bin/sh
# Builds the mruby release builds of build_config.rb and links each
# benchmark source of this directory against each of them.
set -e
bench=$(cd "$(dirname "$0")" && pwd)
mruby=${MRUBY_DIR:-/home/user/mruby}
(cd "$mruby" && MRUBY_CONFIG="$bench/build_config.rb" rake -j4 all "$mruby/build/bench_o2/lib/libmruby.flags.mak" "$mruby/build/bench_os/lib/libmruby.flags.mak")
mkdir -p "$bench/out/include/mruby"
g++ -std=c++23 -O1 $(llvm-config --cxxflags | sed 's/-std=c++17//') -DWRITE_REFLECT_FACTS_LLVM_BINDIR="\"$(llvm-config --bindir)\"" \
  "$bench/../tool/write_reflect_facts.cpp" -o "$bench/out/write_reflect_facts" $(llvm-config --ldflags) -lclang-cpp $(llvm-config --libs --link-shared)
printf '#include "named.hpp"\n' > "$bench/out/facts_input.cpp"
"$bench/out/write_reflect_facts" "$bench/out/facts_input.cpp" -- -std=c++26 -I"$bench" > "$bench/out/include/mruby/reflect_facts.h"
for build in bench_o2 bench_os; do
  lib="$mruby/build/$build/lib"
  flags() { sed -n "s/^$1 = //p" "$lib/libmruby.flags.mak" | sed "s|\$(MRUBY_PACKAGE_DIR)|$mruby/build/$build|g"; }
  cxxflags=$(flags MRUBY_CXXFLAGS)
  ldflags=$(flags MRUBY_LDFLAGS)
  libs=$(flags MRUBY_LIBS)
  for source in "$bench"/*.cpp; do
    name=$(basename "$source" .cpp)
    eval g++ $cxxflags -I"$bench/out/include" -I"$bench" -I"$mruby/build/repos/$build/mruby-c-ext-helpers/include" \
      "$source" -o "$bench/out/$name-$build" $ldflags $libs -lbenchmark -lpthread
  done
done
