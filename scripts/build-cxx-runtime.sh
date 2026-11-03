#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Build libc++, libc++abi and libunwind for the Raspberry Pi target into
# $AROS_RASPI_CXXRT (default deps/aros-aarch64-abiv1-raspi/cxxrt). The Pi
# toolchain and SDK carry no C++ standard library, so the program links this
# one statically.
#
# Sources: the LLVM 20.1.0 release tarball (the compiler's version) and the
# AROS patches of tools/crosstools/llvm, copied in third_party/llvm-runtimes-aros/;
# options after that AROS directory's mmakefile.src, through the runtimes/ build.
#   AROS_RASPI_LLVM_TARBALL  llvm-project-20.1.0.src.tar.xz
# Every input, the resulting archives and the installed headers (their
# manifest HEADERS.sha256) must match third_party/llvm-runtimes-aros/PINS
# (tools/cxxrt_pins.py).
#
# The output directory and its work directory (<output>-work) are deleted and
# rebuilt. They must resolve to a directory below the project's ignored deps/
# drawer, and an existing one must hold the marker file this script writes;
# anything else is refused before anything is deleted.
set -euo pipefail
die() { echo "build-cxx-runtime: $*" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
set +e; . "$here/env.sh" aros-aarch64-abiv1-raspi; rc=$?; set -e
[ $rc -eq 0 ] || die "environment setup failed"
pins="$PROJECT_ROOT/third_party/llvm-runtimes-aros/PINS"
diffs="$PROJECT_ROOT/third_party/llvm-runtimes-aros"
check() { python3 "$PROJECT_ROOT/tools/cxxrt_pins.py" "$@"; }

# 1. Where the output may go (nothing is deleted yet).
marker=.build-cxx-runtime-owned
marker_text="directory owned by scripts/build-cxx-runtime.sh; deleted and rebuilt by it"
resolve() { python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$1"; }
root=$(resolve "$PROJECT_ROOT")
drawer=$(resolve "$PROJECT_ROOT/deps")
case "$drawer/" in "$root"/?*/) ;; *) die "the deps/ drawer resolves outside the project: $drawer";; esac
if git -C "$PROJECT_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git -C "$PROJECT_ROOT" check-ignore -q deps/ || die "deps/ is not ignored by Git; refusing to build into it"
fi
home=$(resolve "${HOME:-/}")
owned_dir() {  # <configured path> <what>: prints the resolved path, or dies
  local p=$1 what=$2 r
  [ -n "$p" ] || die "refusing an empty $what path"
  r=$(resolve "$p")
  [ -n "$r" ] && [ "$r" != / ] || die "refusing $what '$p': it resolves to /"
  [ "$r" != "$home" ] || die "refusing $what '$p': it resolves to \$HOME"
  case "$r/" in "$drawer"/?*/) ;; *) die "refusing $what '$p': $r is not inside $drawer";; esac
  if [ -e "$r" ] || [ -L "$p" ]; then
    [ -d "$r" ] || die "refusing $what '$p': $r exists and is not a directory"
    if [ -n "$(ls -A "$r")" ]; then
      [ -f "$r/$marker" ] && [ "$(cat "$r/$marker")" = "$marker_text" ] \
        || die "refusing $what '$p': $r is not empty and has no $marker written by this script"
    fi
  fi
  printf '%s\n' "$r"
}
out=$(owned_dir "${AROS_RASPI_CXXRT:-}" "output directory")
work=$(owned_dir "$out-work" "work directory")

# 2. Inputs against the pins.
tar=${AROS_RASPI_LLVM_TARBALL:?set AROS_RASPI_LLVM_TARBALL (see local.env.example)}
check tarball "$pins" "$tar" || die "$tar is not the pinned LLVM tarball ($pins)"
check diffs "$pins" "$diffs" || die "the AROS patches in $diffs differ from $pins"

# 3. Build.
rm -rf "$out" "$work"
mkdir -p "$out" "$work/src" "$work/build"
printf '%s\n' "$marker_text" > "$out/$marker"
printf '%s\n' "$marker_text" > "$work/$marker"
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
# __FILE__ in libc++abi and libunwind messages would name this machine's work
# directory; map it to the tarball's own directory name.
pmap="-ffile-prefix-map=$work/src/=$p/"
flags="-D_GNU_SOURCE -fno-PIC -fno-PIE -O2 $pmap"
cmake -G Ninja -S "$work/src/runtimes" -B "$work/build" \
  -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$AROS_TOOLCHAIN/aarch64-aros-clang" -DCMAKE_CXX_COMPILER="$AROS_TOOLCHAIN/aarch64-aros-clang++" \
  -DCMAKE_ASM_COMPILER="$AROS_TOOLCHAIN/aarch64-aros-clang" -DCMAKE_C_COMPILER_TARGET=aarch64-unknown-aros \
  -DCMAKE_CXX_COMPILER_TARGET=aarch64-unknown-aros -DCMAKE_ASM_COMPILER_TARGET=aarch64-unknown-aros \
  -DCMAKE_AR="$AROS_AR" -DCMAKE_RANLIB="$AROS_TOOLCHAIN/aarch64-aros-ranlib" -DCMAKE_NM="$AROS_NM" \
  -DCMAKE_SYSROOT="$AROS_SDK" -DCMAKE_INSTALL_PREFIX="$out" -DCMAKE_POSITION_INDEPENDENT_CODE=OFF \
  -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" -DCMAKE_ASM_FLAGS="-fno-PIC -fno-PIE $pmap" \
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

# 4. The archives and the installed headers against the pins. A different
# result is never left usable. The compiles of the core and of BoxerUI read
# these headers (-isystem, scripts/aros-raspi-cxx); HEADERS.sha256 lists every
# file below include/ with its sha256, and its own sha256 is the headers pin.
if ! check archives "$pins" "$out/lib"; then
  rm -rf "$out/lib"
  die "the built archives differ from $pins (removed $out/lib); if the change is intended, update the archive pins"
fi
check manifest "$out" > "$out/HEADERS.sha256" || { rm -rf "$out/lib"; die "could not list the installed headers"; }
if ! check headers "$pins" "$out"; then
  rm -rf "$out/lib"
  die "the installed headers differ from $pins (removed $out/lib); if the change is intended, update the headers pin"
fi
{
  echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ) by scripts/build-cxx-runtime.sh"
  echo "pins: $(shasum -a 256 "$pins" | cut -d' ' -f1)  third_party/llvm-runtimes-aros/PINS"
  echo "compiler: $("$AROS_TOOLCHAIN/aarch64-aros-clang" --version | head -1)"
  echo "sdk: $AROS_SDK"
  check lines "$pins"
} > "$out/BUILDINFO.txt"
cat "$out/BUILDINFO.txt"
