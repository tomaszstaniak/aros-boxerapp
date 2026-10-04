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
