#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Build libc++, libc++abi and libunwind for the Raspberry Pi target into
# $AROS_RASPI_CXXRT (default deps/aros-aarch64-abiv1-raspi/cxxrt). The Pi
# toolchain and SDK carry no C++ standard library, so the program links this
# one statically. Nothing outside the output and build directories changes.
#
# Sources: the LLVM 20.1.0 release tarball (the compiler's version) and the
# AROS patches of mainline tools/crosstools/llvm; options after that
# directory's mmakefile.src, through the runtimes/ build.
#   AROS_RASPI_LLVM_TARBALL  llvm-project-20.1.0.src.tar.xz
#   AROS_RASPI_LLVM_DIFFS    directory holding {libcxx,libcxxabi,libunwind}-20.1.0.src-aros.diff
set -euo pipefail
die() { echo "build-cxx-runtime: $*" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
set +e; . "$here/env.sh" aros-aarch64-abiv1-raspi; rc=$?; set -e
[ $rc -eq 0 ] || die "environment setup failed"
tar=${AROS_RASPI_LLVM_TARBALL:?set AROS_RASPI_LLVM_TARBALL (see local.env.example)}
diffs=${AROS_RASPI_LLVM_DIFFS:?set AROS_RASPI_LLVM_DIFFS (see local.env.example)}
llvm_sha=4579051e3c255fb4bb795d54324f5a7f3ef79bd9181e44293d7ee9a7f62aad9a
[ "$(shasum -a 256 "$tar" | cut -d' ' -f1)" = "$llvm_sha" ] || die "$tar does not have sha256 $llvm_sha"
out=$AROS_RASPI_CXXRT
work=${out}-work
rm -rf "$out" "$work"; mkdir -p "$out" "$work/src" "$work/build"
p=llvm-project-20.1.0.src
tar xJf "$tar" -C "$work/src" --strip-components=1 "$p/libcxx" "$p/libcxxabi" "$p/libunwind" \
  "$p/runtimes" "$p/cmake" "$p/llvm/cmake" "$p/llvm/utils/llvm-lit"
for c in libcxx libcxxabi libunwind; do
  patch -s -p1 -d "$work/src/$c" -i "$diffs/$c-20.1.0.src-aros.diff"
done
# The wrappers name the SDK; libc++ itself is what is being built, so the
# compiler is the toolchain's own with RPI_SDK set.
export RPI_SDK="$AROS_SDK"
# CMake's Generic platform has no shared libraries and turns SHARED targets
# into static ones of the same name, hence the *_SHARED_OUTPUT_NAME values.
flags="-D_GNU_SOURCE -fno-PIC -fno-PIE -O2"
cmake -G Ninja -S "$work/src/runtimes" -B "$work/build" \
  -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$AROS_TOOLCHAIN/aarch64-aros-clang" -DCMAKE_CXX_COMPILER="$AROS_TOOLCHAIN/aarch64-aros-clang++" \
  -DCMAKE_ASM_COMPILER="$AROS_TOOLCHAIN/aarch64-aros-clang" -DCMAKE_C_COMPILER_TARGET=aarch64-unknown-aros \
  -DCMAKE_CXX_COMPILER_TARGET=aarch64-unknown-aros -DCMAKE_ASM_COMPILER_TARGET=aarch64-unknown-aros \
  -DCMAKE_AR="$AROS_AR" -DCMAKE_RANLIB="$AROS_TOOLCHAIN/aarch64-aros-ranlib" -DCMAKE_NM="$AROS_NM" \
  -DCMAKE_SYSROOT="$AROS_SDK" -DCMAKE_INSTALL_PREFIX="$out" -DCMAKE_POSITION_INDEPENDENT_CODE=OFF \
  -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" -DCMAKE_ASM_FLAGS="-fno-PIC -fno-PIE" \
  -DLLVM_ENABLE_RUNTIMES="libunwind;libcxxabi;libcxx" -DLLVM_INCLUDE_TESTS=OFF \
  -DLIBCXX_INCLUDE_BENCHMARKS=OFF -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF -DLIBUNWIND_INCLUDE_TESTS=OFF \
  -DLIBUNWIND_ENABLE_SHARED=OFF -DLIBUNWIND_ENABLE_STATIC=ON -DLIBUNWIND_USE_COMPILER_RT=ON -DLIBUNWIND_IS_BAREMETAL=ON \
  -DLIBUNWIND_SHARED_OUTPUT_NAME=unwind_sh -DLIBCXXABI_SHARED_OUTPUT_NAME=c++abi_sh -DLIBCXX_SHARED_OUTPUT_NAME=c++_sh \
  -DLIBCXXABI_ENABLE_SHARED=OFF -DLIBCXXABI_ENABLE_STATIC=ON -DLIBCXXABI_USE_LLVM_UNWINDER=ON \
  -DLIBCXXABI_ENABLE_THREADS=ON -DLIBCXXABI_HAS_PTHREAD_API=ON -DLIBCXXABI_USE_COMPILER_RT=ON \
  -DLIBCXX_CXX_ABI=libcxxabi -DLIBCXX_USE_COMPILER_RT=ON -DLIBCXX_ENABLE_STATIC=ON -DLIBCXX_ENABLE_SHARED=OFF \
  -DLIBCXX_ENABLE_THREADS=ON -DLIBCXX_HAS_PTHREAD_API=ON -DLIBCXX_ENABLE_LOCALIZATION=ON \
  -DLIBCXX_ENABLE_WIDE_CHARACTERS=ON -DLIBCXX_ENABLE_FILESYSTEM=ON \
  -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON -DLIBCXX_STATICALLY_LINK_ABI_IN_STATIC_LIBRARY=ON \
  > "$work/configure.log" 2>&1 || { tail -20 "$work/configure.log" >&2; die "configure failed"; }
ninja -C "$work/build" install-unwind install-cxxabi install-cxx > "$work/build.log" 2>&1 \
  || { tail -20 "$work/build.log" >&2; die "build failed"; }
{
  echo "llvm 20.1.0 tarball sha256 $llvm_sha"
  (cd "$diffs" && shasum -a 256 libcxx-20.1.0.src-aros.diff libcxxabi-20.1.0.src-aros.diff libunwind-20.1.0.src-aros.diff) | sed 's/^/aros patch: /'
  (cd "$out/lib" && shasum -a 256 libc++.a libunwind.a) | sed 's/^/library: /'
} > "$out/BUILDINFO.txt"
cat "$out/BUILDINFO.txt"
