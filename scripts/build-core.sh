#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Build Boxer's modified DOSBox 0.74 core as a static library for one ABI.
#
# The source set is the Boxer target's Sources phase in Boxer.xcodeproj
# (tools/xcode-sources.py), so the library contains what the original app
# linked, nothing found by globbing.
#
# Divergences from Boxer.xcodeproj, deliberate:
# - x86_64-aros target instead of i386-apple-darwin;
# - -O2 instead of Xcode's Release default -Os: the emulator runs under TCG
#   in tests and speed matters more than size; -mstackrealign dropped (an
#   i386 SSE alignment workaround, irrelevant on x86_64);
# - Xcode resolved quoted includes through a header map covering every
#   project header; here each DOSBox directory holding headers is put on the
#   quote-include path (-iquote, so core_dyn_x86/string.h cannot shadow
#   <string.h>) to the same effect (e.g. BXCoalfaceDrives.h -> "drives.h");
# - feature switches for AROS are set in DOSBox/config.h by patches, not here.
#
# Usage: [BOXER_HOST=sdl2|sdl3] build-core.sh <abiv11|mainline-v1> [source-root]
#   BOXER_HOST (scripts/env.sh): sdl2 -> build/<abi>/core, sdl3 -> core-sdl3.
#   source-root defaults to work/boxer. Compile errors stop the build (exit 1);
#   configuration faults exit 2.
set -euo pipefail
die() { echo "build-core: $*" >&2; exit 2; }
[ $# -ge 1 ] && [ $# -le 2 ] || die "usage: build-core.sh <abiv11|mainline-v1> [source-root]"
here=$(cd "$(dirname "$0")" && pwd)
set +e; . "$here/env.sh" "$1"; rc=$?; set -e
[ $rc -eq 0 ] || die "environment setup failed"
src=${2:-$WORK_DIR}
[ -f "$src/DOSBox/include/dosbox.h" ] || die "not a Boxer source tree: $src"
AROS_AR="$AROS_TOOLCHAIN/x86_64-aros-ar"
[ -x "$AROS_AR" ] || die "archiver not executable: $AROS_AR"

out="$BUILD_DIR/core$HOST_SUFFIX"
if [ "$BOXER_HOST" = sdl3 ]; then
  [ -f "$BUILD_DIR/sdl3/lib/libSDL3_ctl.a" ] || die "SDL3 missing: run scripts/build-sdl3.sh $1"
fi
mkdir -p "$out"
"$PROJECT_ROOT/tools/xcode-sources.py" "$src" Boxer > "$out/sources.txt" \
  || die "could not derive the source set"

cxxflags=(-O2 -std=gnu++0x -fno-strict-aliasing -Wno-deprecated
  -I"$src/DOSBox" -I"$src/DOSBox/include" -I"$src/Boxer"
  "${HOST_CFLAGS[@]+"${HOST_CFLAGS[@]}"}" -I"$AROS_SDK/include")
# DOSBox/include first: several basenames (support.h, cache.h, ...) exist
# both there and in CPU-core directories, and the public one must win.
cxxflags+=(-iquote "$src/DOSBox/include")
while IFS= read -r d; do cxxflags+=(-iquote "$d"); done < <(
  find "$src/DOSBox/src" -name '*.h' -exec dirname {} \; | sort -u)
jobs=${JOBS:-$(sysctl -n hw.ncpu)}

# Build identity: objects are reused only when they were made from the same
# source root, by the same compiler binary and version, against the same
# SDK, with the same flags. Any difference discards them. (Dependency files
# name absolute paths of one source root, so another root's objects would
# otherwise look current while compiled from other files.)
identity=$(
  printf 'root %s\n' "$(cd "$src" && pwd -P)"
  printf 'cxx %s\n' "$AROS_CXX"
  "$AROS_CXX" --version | head -1
  shasum -a 256 "$(command -v "$AROS_CXX")" | cut -d' ' -f1
  printf 'sdk %s\n' "$AROS_SDK"
  printf 'flag %s\n' "${cxxflags[@]}"
)
if [ -f "$out/build-identity" ] && [ "$(cat "$out/build-identity")" != "$identity" ]; then
  echo "build-core: build identity changed, discarding objects" >&2
  rm -rf "$out/obj"
fi
mkdir -p "$out/obj"
printf '%s\n' "$identity" > "$out/build-identity"

objs=(); pids=(); fail=0
compile() {  # one TU; log next to the object, kept on failure
  "$AROS_CXX" "${cxxflags[@]}" -MMD -MF "$2.d" -c "$1" -o "$2" > "$2.log" 2>&1 \
    || { rm -f "$2"; return 1; }
}
# An object is current only if every file its last compile read (its .d)
# still exists and is older; tools/depcheck.py parses make syntax properly.
up_to_date() { python3 "$PROJECT_ROOT/tools/depcheck.py" "$1"; }
while IFS= read -r f; do
  o="$out/obj/${f//\//_}.o"; objs+=("$o")
  up_to_date "$o" && continue
  compile "$src/$f" "$o" & pids+=($!)
  if [ ${#pids[@]} -ge "$jobs" ]; then
    wait "${pids[0]}" || fail=$((fail+1)); pids=("${pids[@]:1}")
  fi
done < "$out/sources.txt"
for p in "${pids[@]+"${pids[@]}"}"; do wait "$p" || fail=$((fail+1)); done
[ $fail -eq 0 ] || { echo "build-core: $fail TU(s) failed; logs: $out/obj/*.log" >&2; exit 1; }

rm -f "$out/libboxer-dosbox.a"
"$AROS_AR" rcs "$out/libboxer-dosbox.a" "${objs[@]}"
echo "built $out/libboxer-dosbox.a (host $BOXER_HOST, ${#objs[@]} objects, $("$AROS_CXX" --version | head -1))"
