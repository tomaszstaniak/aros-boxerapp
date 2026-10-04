#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
#
# Checks that a staged BoxerUI package directory carries the licence notices
# that documentation/licenses.md ("Distribution checklist") requires.
# Usage: scripts/check-package-notices.sh <abiv11|mainline-v1> <package dir>
# Exit 0 when nothing is missing, 1 otherwise. Content of the notices is
# compared with this repository's copies so that a stale copy is caught too.
set -u
abi=${1:-}; pkg=${2:-}
case "$abi" in abiv11|mainline-v1) ;; *) echo "usage: $0 <abiv11|mainline-v1> <package dir>" >&2; exit 2;; esac
[ -d "$pkg" ] || { echo "not a directory: $pkg" >&2; exit 2; }
root=$(cd "$(dirname "$0")/.." && pwd)
missing=0
need() {   # need <path in package> <reference file or ->
    if [ ! -f "$pkg/$1" ]; then echo "MISSING  $1"; missing=1
    elif [ "$2" != - ] && ! cmp -s "$pkg/$1" "$2"; then echo "DIFFERS  $1 (from $2)"; missing=1
    else echo "ok       $1"; fi
}
need COPYING "$root/COPYING"
for l in AROS-APL-1.1 GCC-exception-3.1 LGPL-2.1 Bitstream-Vera BSD-2-Clause-ADBToolkit; do
    need "LICENSES/$l.txt" "$root/LICENSES/$l.txt"
done
# The SDL notice follows the host the package's BoxerUI was built with
# (BUILDINFO "host:"). SDL3 is linked statically. For SDL2, mainline links
# only the sdl2.library stub; the notice is kept because the stub's headers
# come from SDL. ABIv11 links SDL2 itself.
host=$(sed -n 's/^host: //p' "$pkg/BUILDINFO.txt" 2>/dev/null | head -1)
case "$host" in
    sdl3) need LICENSES/SDL3-zlib.txt "$root/LICENSES/SDL3-zlib.txt" ;;
    sdl2|"") need LICENSES/SDL2-zlib.txt "$root/LICENSES/SDL2-zlib.txt" ;;
    *) echo "UNKNOWN  host '$host' in BUILDINFO.txt"; missing=1 ;;
esac
need NOTICE-boxer.txt "$root/assets/NOTICE-boxer.txt"
need NOTICE-replacements.txt "$root/assets/NOTICE-replacements.txt"
need licenses.md "$root/documentation/licenses.md"
# GPL-2.0 s.3: the source archive itself (3a) or a written offer (3b).
if ls "$pkg"/*-source.tar* >/dev/null 2>&1 || [ -f "$pkg/SOURCE-OFFER.txt" ]; then
    echo "ok       source archive or SOURCE-OFFER.txt"
else echo "MISSING  source archive (*-source.tar*) or SOURCE-OFFER.txt"; missing=1; fi
if [ -d "$pkg/Fonts" ]; then need Fonts/Bitstream-Vera.txt "$root/LICENSES/Bitstream-Vera.txt"
    if grep -l -a -e Bitstream -e 'Vera Sans' "$pkg"/Fonts/*.otag 2>/dev/null; then
        echo "BADNAME  Fonts/*.otag still names Bitstream/Vera Sans"; missing=1; fi
fi
# Never shipped (documentation/licenses.md checklist item 7).
for bad in Brand.png BrandWatermark.png '*.ttf'; do
    found=$(find "$pkg" -name "$bad" | head -3)
    [ -z "$found" ] || { echo "FORBIDDEN $found"; missing=1; }
done
[ -f "$pkg/BUILDINFO.txt" ] || echo "note     no BUILDINFO.txt (toolchain/SDK identification for the source offer)"
echo "abi=$abi result=$([ $missing = 0 ] && echo complete || echo incomplete)"
exit $missing
