#!/bin/bash

set -euo pipefail

KLEE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$KLEE_DIR/build"

build() {
  [ -d "$BUILD_DIR" ] || mkdir -p "$BUILD_DIR"

  cd "$BUILD_DIR"

  [ -f "Makefile" ] || CXXFLAGS="-D_GLIBCXX_USE_CXX11_ABI=0" \
                     CMAKE_PREFIX_PATH="$KLEE_DIR/../z3/build" \
                     CMAKE_INCLUDE_PATH="$KLEE_DIR/../z3/build/include/" \
                     cmake \
                         -DENABLE_UNIT_TESTS=OFF \
                         -DBUILD_SHARED_LIBS=OFF \
                         -DLLVM_CONFIG_BINARY="$KLEE_DIR/../llvm/Release/bin/llvm-config" \
                         -DLLVMCC="$KLEE_DIR/../llvm/Release/bin/clang" \
                         -DLLVMCXX="$KLEE_DIR/../llvm/Release/bin/clang++" \
                         -DENABLE_SOLVER_Z3=ON \
                         -DENABLE_KLEE_UCLIBC=ON \
                         -DKLEE_UCLIBC_PATH="$KLEE_DIR/../klee-uclibc" \
                         -DENABLE_POSIX_RUNTIME=ON \
                         -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                         -DENABLE_KLEE_ASSERTS=ON \
                         -DENABLE_DOXYGEN=OFF \
                         $KLEE_DIR

  make -kj $(nproc) || exit 1
}

build
