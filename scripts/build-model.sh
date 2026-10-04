#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Cross-compile check of src/model for one ABI: objects only, no link.
#
#   scripts/build-model.sh <abiv11|mainline-v1>
#
# Output: build/<abi>/model/*.o and build.log. The model has no AROS headers;
# this proves it compiles with the target C++ library (GCC 10.5 for
# x86_64-aros), not that it runs there.
set -euo pipefail
die() { echo "build-model: $*" >&2; exit 2; }
[ $# -eq 1 ] || die "usage: build-model.sh <abiv11|mainline-v1>"
here=$(cd "$(dirname "$0")" && pwd)
set +e; . "$here/env.sh" "$1"; rc=$?; set -e
[ $rc -eq 0 ] || die "environment setup failed"

out="$BUILD_DIR/model"
mkdir -p "$out"
log="$out/build.log"
: > "$log"
"$AROS_CXX" --version | head -1 >> "$log"
for src in "$PROJECT_ROOT"/src/model/*.cpp "$PROJECT_ROOT/src/emulator/filesystem.cpp"; do
  obj="$out/$(basename "${src%.cpp}").o"
  echo "CXX $src" >> "$log"
  "$AROS_CXX" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -I"$AROS_SDK/include" \
    -c "$src" -o "$obj" >> "$log" 2>&1 || { cat "$log"; die "compile failed: $src"; }
done
cat "$log"
echo "build-model: $BOXER_ABI objects in $out"
