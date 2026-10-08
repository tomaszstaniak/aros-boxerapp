#!/bin/bash
# Host unit tests for src/model (gamebox, plist, data locations, shadowing
# filesystem, file replacement). No AROS toolchain or VM needed.
#
#   tests/model/run.sh
#
# Builds with the host C++ compiler (CXX, default clang++) into
# build/host/model/ and runs against a scratch tree whose names contain
# spaces; the scratch tree is removed after a passing run.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out="$root/build/host/model"
mkdir -p "$out"
cxx=${CXX:-clang++}
command -v "$cxx" >/dev/null || { echo "run.sh: $cxx not found (set CXX)" >&2; exit 2; }

"$cxx" -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -g -o "$out/model_test" \
  "$here/model_test.cpp" "$root"/src/model/*.cpp "$root/src/emulator/filesystem.cpp"

scratch="$out/scratch dir with spaces"
"$out/model_test" "$scratch"
rm -rf "$scratch"

# Cover art and renaming. The title checks need a TrueType font: the
# system Vera Sans of an AROS ISO (AROS_ISO_FONTS, see local.env.example),
# or COVER_TEST_FONT; without one they are skipped and the run says so.
"$cxx" -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -g -o "$out/cover_test" \
  "$here/cover_test.cpp" "$root"/src/model/*.cpp "$root/src/emulator/filesystem.cpp"
font=${COVER_TEST_FONT:-}
if [ -z "$font" ] && [ -f "$root/local.env" ]; then
  iso=$(sed -n 's/^AROS_ISO_FONTS="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' "$root/local.env" | head -1)
  [ -n "${AROS_ISO_FONTS:-}" ] && iso=$AROS_ISO_FONTS
  [ -n "$iso" ] && [ -f "$iso/TrueType/VeraSans.ttf" ] && font="$iso/TrueType/VeraSans.ttf"
fi
scratch="$out/cover scratch with spaces"
"$out/cover_test" "$scratch" ${font:+"$font"}
rm -rf "$scratch"
