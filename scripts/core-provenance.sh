#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Print what the core is built from: the Boxer upstream pin, the patch
# series in order and each patch's sha256. build-core.sh records it in the
# core's BUILDINFO.txt; make-package.sh compares it with the tree, so a
# package never pairs a core with a patch series it was not built from.
#
#   core-provenance.sh <project-root>
set -euo pipefail
P=${1:?usage: core-provenance.sh <project-root>}
pin=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["repositories"]["boxer"]["commit"])' \
  "$P/upstreams.json")
echo "upstream pin: $pin"
echo "series:"
while IFS= read -r line; do
  name=${line%%#*}; name=$(echo "$name" | tr -d '[:space:]')
  [ -n "$name" ] || continue
  [ -f "$P/patches/boxer/$name" ] || { echo "core-provenance: series lists missing $name" >&2; exit 2; }
  echo "  $(shasum -a 256 "$P/patches/boxer/$name" | cut -d' ' -f1)  $name"
done < "$P/patches/boxer/series"
