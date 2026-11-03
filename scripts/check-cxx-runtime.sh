#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Check the Raspberry Pi target's private C++ runtime against
# third_party/llvm-runtimes-aros/PINS (tools/cxxrt_pins.py):
#   scripts/check-cxx-runtime.sh <cxxrt dir> [<BoxerUI BUILDINFO.txt>]
# - the AROS patches in third_party/llvm-runtimes-aros/ are the pinned ones;
# - <cxxrt dir>/lib holds the pinned archives; <cxxrt dir>/HEADERS.sha256 is
#   the pinned header manifest and <cxxrt dir>/include holds exactly the
#   headers it lists (none changed, added or removed); <cxxrt dir>/BUILDINFO.txt
#   (written by scripts/build-cxx-runtime.sh) records exactly the pins;
# - with a BoxerUI BUILDINFO.txt: its "c++ runtime:" lines record exactly
#   the pins, so the binary was linked with that runtime;
# - when AROS_RASPI_LLVM_TARBALL is set: it is the pinned tarball.
# Exit 0 when all match, 1 on a mismatch, 2 on a usage error.
set -u
[ $# -ge 1 ] && [ $# -le 2 ] || { echo "usage: $0 <cxxrt dir> [<BoxerUI BUILDINFO.txt>]" >&2; exit 2; }
root=$(cd "$(dirname "$0")/.." && pwd)
pins="$root/third_party/llvm-runtimes-aros/PINS"
c() { python3 "$root/tools/cxxrt_pins.py" "$@"; }
bad=0
c diffs "$pins" "$root/third_party/llvm-runtimes-aros" || bad=1
c archives "$pins" "$1/lib" || bad=1
c headers "$pins" "$1" || bad=1
c record "$pins" "$1/BUILDINFO.txt" "" || bad=1
[ $# -lt 2 ] || c record "$pins" "$2" "c++ runtime: " || bad=1
[ -z "${AROS_RASPI_LLVM_TARBALL:-}" ] || c tarball "$pins" "$AROS_RASPI_LLVM_TARBALL" || bad=1
[ $bad = 0 ] || echo "check-cxx-runtime: the C++ runtime does not match $pins" >&2
exit $bad
