#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Build boxer-core-smoke for one ABI: the emulator layer (src/emulator) and
# the AROS smoke host (src/platform/aros) linked against the core library
# from build-core.sh. Run build-core.sh first with the same source root.
#
# Usage: build-smoke.sh <abiv11|mainline-v1> [source-root]
set -euo pipefail
die() { echo "build-smoke: $*" >&2; exit 2; }
[ $# -ge 1 ] && [ $# -le 2 ] || die "usage: build-smoke.sh <abiv11|mainline-v1> [source-root]"
here=$(cd "$(dirname "$0")" && pwd)
set +e; . "$here/env.sh" "$1"; rc=$?; set -e
[ $rc -eq 0 ] || die "environment setup failed"
src=${2:-$WORK_DIR}
lib="$BUILD_DIR/core$HOST_SUFFIX/libboxer-dosbox.a"
[ -f "$lib" ] || die "core library missing: $lib (run build-core.sh $1)"
# Some AROS cross toolchains have their build tree's linker path compiled
# into collect-aros; AROS_TOOLCHAIN_MOUNT (optional) names a directory that
# must be present before linking, e.g. a mounted build volume.
[ -z "${AROS_TOOLCHAIN_MOUNT:-}" ] || [ -d "$AROS_TOOLCHAIN_MOUNT" ] \
  || die "AROS_TOOLCHAIN_MOUNT=$AROS_TOOLCHAIN_MOUNT is not available (volume not mounted?)"

out="$BUILD_DIR/smoke$HOST_SUFFIX"
mkdir -p "$out/obj"
# Files that include the core's headers compile as gnu++14: DOSBox 0.74's
# setup.h uses dynamic exception specifications, which C++17 removed.
# Project code that does not touch the core (the model) stays C++17.
cxxflags=(-O2 -Wall -Wno-deprecated -Wno-unknown-pragmas -fno-strict-aliasing
  -iquote "$src/DOSBox/include" -I"$src/DOSBox" -I"$src/DOSBox/include"
  -iquote "$src/DOSBox/src/dos" -I"$src/Boxer" "${HOST_CFLAGS[@]+"${HOST_CFLAGS[@]}"}" -I"$AROS_SDK/include")
objs=()
for f in src/emulator/emulator.cpp src/emulator/coalface.cpp src/emulator/filesystem.cpp \
         src/model/fsutil.cpp src/model/plist.cpp src/model/gamebox.cpp \
         src/model/datalocations.cpp src/model/shadowfs.cpp \
         src/platform/aros/session_setup.cpp \
         src/platform/aros/rawkeys.cpp src/platform/aros/core_smoke.cpp; do
  o="$out/obj/$(basename "$f").o"; objs+=("$o")
  # The model does not include the core's headers and is C++17.
  # session_setup.cpp uses only the model and emulator.h, no core headers.
  case $f in src/model/*|*/session_setup.cpp) std=-std=gnu++17 ;; *) std=-std=gnu++14 ;; esac
  "$AROS_CXX" "${cxxflags[@]}" "$std" -c "$PROJECT_ROOT/$f" -o "$o"
done
# Mainline's libgcc never fills dwarf_reg_size_table, so every C++ throw
# aborts (scripts/build-unwind-fix.sh). Link a rebuilt unwind-dw2.o first;
# it then provides all _Unwind_* symbols. Remove once the toolchain is fixed.
unwind=()
if [ "$BOXER_ABI" = mainline-v1 ]; then
  fix="$BUILD_DIR/unwind-fix/unwind-dw2.o"
  [ -f "$fix" ] || "$here/build-unwind-fix.sh" >&2 \
    || die "could not build the unwind fix"
  unwind=("$fix")
fi
"$AROS_CXX" -o "$out/boxer-core-smoke" "${objs[@]}" "${unwind[@]+"${unwind[@]}"}" "$lib" "${HOST_LIBS[@]}"
if [ "$BOXER_ABI" = mainline-v1 ]; then
  # Read all of nm's output first: grep -q would close the pipe early and
  # pipefail would report nm's SIGPIPE as a failure.
  syms=$("$AROS_TOOLCHAIN/x86_64-aros-nm" "$out/boxer-core-smoke")
  grep ' dwarf_reg_size_table$' <<< "$syms" > /dev/null \
    || die "unwind fix not linked: no dwarf_reg_size_table in the binary"
fi
echo "built $out/boxer-core-smoke"
