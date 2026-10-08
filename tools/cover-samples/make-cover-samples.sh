#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Title font samples for the bootleg covers: each candidate font
# rendered on the three original templates by BoxerUI's own cover code.
#
#   tools/cover-samples/make-cover-samples.sh <out dir> <caption font.ttf> <label>=<font.ttf>...
#
# Host only (c++ and python3); nothing here is shipped.
set -euo pipefail
die() { echo "make-cover-samples: $*" >&2; exit 2; }
[ $# -ge 3 ] || die "usage: make-cover-samples.sh <out dir> <caption font.ttf> <label>=<font.ttf>..."
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=$1; capfont=$2; shift 2
build="$root/build/host/cover-samples"
mkdir -p "$build/gen" "$out"
art="$root/assets/runtime/boxer"
python3 "$root/tools/png2inc.py" "$build/gen/cover_assets.inc" \
  CDCase="$art/CDCase-128.png" CDCover="$art/CDCover-128.png" \
  35Diskette="$art/35Diskette-128.png" 35DisketteShine="$art/35DisketteShine-128.png" \
  525Diskette="$art/525Diskette-128.png" BoxArtShine="$art/BoxArtShine.png@128"
"${CXX:-c++}" -std=c++17 -Wall -O2 -I"$build/gen" -o "$build/cover_samples" \
  "$here/cover_samples.cpp" "$root/src/ui/coverassets.cpp" \
  "$root/src/model/coverart.cpp" "$root/src/model/coverfont.cpp" "$root/src/model/fsutil.cpp" \
  "$root/src/model/gamebox.cpp" "$root/src/model/plist.cpp" "$root/src/model/datalocations.cpp" \
  "$root/src/emulator/filesystem.cpp" "$root/src/model/programs.cpp"
for spec in "$@"; do
  label=${spec%%=*}; font=${spec#*=}
  [ -f "$font" ] || die "no font file $font"
  "$build/cover_samples" "$out" "$label" "$font" "$capfont"
done
