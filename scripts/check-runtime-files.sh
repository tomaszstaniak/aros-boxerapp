#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Runtime-file check of make-package.sh: the files build-ui.sh staged next
# to BoxerUI (Fonts/), recorded in BUILDINFO.txt under "runtime files:", are
# exactly the ones in a directory: build-ui's stage/ before copying, the
# package drawer after.
#
#   check-runtime-files.sh <ui BUILDINFO.txt> <dir>
#
# Every top-level directory named in the record must hold exactly the
# recorded files with the recorded content.
# Exit 0: all match; nothing is printed.
# Exit 1: one line per difference: "<path>: FAILED" (content), "<path>:
#         FAILED open or read" (missing), "<path>: not in the staging record".
# Exit 2: the check could not be made (no record, empty or malformed).
set -uo pipefail
die() { echo "check-runtime-files: $*" >&2; exit 2; }
[ $# -eq 2 ] || die "usage: check-runtime-files.sh <ui BUILDINFO.txt> <dir>"
info=$1; dir=$2
here=$(cd "$(dirname "$0")" && pwd)
[ -d "$dir" ] || die "no directory $dir"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/runtime-files.XXXXXX") || die "mktemp failed"
trap 'rm -rf "$tmp"' EXIT
"$here/buildinfo-section.sh" "$info" "runtime files" > "$tmp/m" \
  || die "no runtime file list in $info; rebuild (build-ui.sh)"
changed=$("$here/check-sources.sh" "$dir" "$tmp/m"); rc=$?
[ $rc -le 1 ] || exit 2
sed -E 's/^[0-9a-f]{64}  //' "$tmp/m" | LC_ALL=C sort > "$tmp/listed"
tops=$(cut -d/ -f1 "$tmp/listed" | sort -u)
extra=$(cd "$dir" && while IFS= read -r t; do [ ! -d "$t" ] || find "$t" -type f; done <<< "$tops" \
  | LC_ALL=C sort | LC_ALL=C comm -23 - "$tmp/listed" | sed 's/$/: not in the staging record/')
[ -z "$changed" ] && [ -z "$extra" ] && exit 0
[ -z "$changed" ] || printf '%s\n' "$changed"
[ -z "$extra" ] || printf '%s\n' "$extra"
exit 1
