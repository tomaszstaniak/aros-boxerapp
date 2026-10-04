#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Build the static, reduced SDL3 that BoxerUI links when BOXER_HOST=sdl3,
# for one ABI, into build/<abi>/sdl3/ (lib/libSDL3_ctl.a, include via src/).
#
# SDL 3.4.12 release tarball + contrib
# a3cb9c4700 AROS diff (controller.hidd joystick backend, lowlevel.library
# fallback) + OpenLoco's langinfo guard; video, renderer, OpenGL and camera
# compiled out; audio (AHI), timer, threads, joystick, gamepad and virtual
# joystick on. Nothing links gl.library: the result needs only -liconv.
# The script that compiles it is third_party/sdl3-aros/build-sdl3-ctl.py;
# this wrapper fetches and checks the tarball, adds the pins and a BUILDINFO.
# SDL3_TARBALL: a local copy of SDL3-3.4.12.tar.gz; default
# deps/src/SDL3-3.4.12.tar.gz, downloaded from SDL3_URL when missing.
#
# Usage: build-sdl3.sh <abiv11|mainline-v1>
set -euo pipefail
die() { echo "build-sdl3: $*" >&2; exit 2; }
[ $# -eq 1 ] || die "usage: build-sdl3.sh <abiv11|mainline-v1>"
here=$(cd "$(dirname "$0")" && pwd)
set +e; . "$here/env.sh" "$1"; rc=$?; set -e
[ $rc -eq 0 ] || die "environment setup failed"
trial="$PROJECT_ROOT/third_party/sdl3-aros"
sdl3_sha=f07b958a9ac5020fb7a44cadb957f658b2149c3c8abb4f63145fac9303249db7
SDL3_URL="${SDL3_URL:-https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-3.4.12.tar.gz}"
SDL3_TARBALL="${SDL3_TARBALL:-$PROJECT_ROOT/deps/src/SDL3-3.4.12.tar.gz}"
if [ ! -f "$SDL3_TARBALL" ]; then
  mkdir -p "$(dirname "$SDL3_TARBALL")"
  curl -fL -o "$SDL3_TARBALL.part" "$SDL3_URL" || die "download failed: $SDL3_URL"
  mv "$SDL3_TARBALL.part" "$SDL3_TARBALL"
fi
[ "$(shasum -a 256 "$SDL3_TARBALL" | cut -d' ' -f1)" = "$sdl3_sha" ] \
  || die "$SDL3_TARBALL does not have sha256 $sdl3_sha"
export SDL3_TARBALL
out="$BUILD_DIR/sdl3"
# A fresh tree every time: the build script edits the config header in place.
rm -rf "$out"
python3 "$trial/build-sdl3-ctl.py" "$1" "$out"
lib="$out/lib/libSDL3_ctl.a"
cfg="$out/src/SDL3-3.4.12/include/build_config/SDL_build_config_aros.h"
for d in SDL_AUDIO_DRIVER_AHI SDL_THREAD_AROS SDL_TIMER_AROS SDL_JOYSTICK_AROS SDL_JOYSTICK_VIRTUAL; do
  grep -q "^#define $d 1" "$cfg" || die "$d is not enabled in $cfg"
done
for d in SDL_VIDEO_DISABLED SDL_RENDER_DISABLED SDL_CAMERA_DISABLED; do
  grep -q "^#define $d 1" "$cfg" || die "$d is not set in $cfg"
done
if grep -qE '^#define (SDL_VIDEO_OPENGL|SDL_VIDEO_DRIVER_AROS|SDL_VIDEO_RENDER_OGL) 1' "$cfg"; then
  die "video/GL still enabled in $cfg"
fi
undef=$("$AROS_TOOLCHAIN/x86_64-aros-nm" -u "$lib" 2>/dev/null | grep -E ' glA|GLBase' || true)
[ -z "$undef" ] || die "the SDL3 library references GL: $undef"
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "abi: $BOXER_ABI"
  echo "compiler: $AROS_TOOLCHAIN/x86_64-aros-gcc"
  "$AROS_TOOLCHAIN/x86_64-aros-gcc" --version | head -1
  echo "sdk: $AROS_SDK"
  echo "upstream: SDL3-3.4.12.tar.gz sha256 f07b958a9ac5020fb7a44cadb957f658b2149c3c8abb4f63145fac9303249db7"
  echo "aros port: aros-development-team/contrib a3cb9c4700a81c9d14ebdc8741c932825c4429f0 (SDL3/main)"
  (cd "$trial/contrib-a3cb9c4700" && shasum -a 256 SDL3-3.4.12-aros.diff SDL3_static.c mmakefile.src) | sed 's/^/  /'
  echo "local patch: $(shasum -a 256 "$trial/sdl3-3.4.12-langinfo-guard.diff" | cut -d' ' -f1)  third_party/sdl3-aros/sdl3-3.4.12-langinfo-guard.diff"
  echo "borrowed headers (controller.hidd backend): inc/hidd/controller.h from AROS 72a773f2de8af8295f221dc02ee6fd8d48ae46e0; gen/ genmodule output; inc-v11/ (abiv11 only)"
  echo "  tree: $( (cd "$trial" && find inc gen $( [ "$BOXER_ABI" = abiv11 ] && echo inc-v11 ) -type f -print0 | sort -z | xargs -0 shasum -a 256) | shasum -a 256 | cut -d' ' -f1)"
  echo "config header: $(shasum -a 256 "$cfg" | cut -d' ' -f1)"
  echo "enabled: audio(ahi,dummy) timer threads joystick(aros: controller.hidd + lowlevel fallback) gamepad virtual-joystick (rumble via joystick API only)"
  echo "disabled: video renderer opengl camera gpu hidapi haptic sensor"
  echo "library: $(shasum -a 256 "$lib" | cut -d' ' -f1)  $(stat -f %z "$lib") bytes"
  echo "GL references: none (nm -u checked)"
} > "$out/BUILDINFO.txt"
cat "$out/BUILDINFO.txt"
