#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Compare the tree with a source manifest ("<sha256>  <path>" lines, paths
# relative to the project root), as make-package.sh does before packaging.
#
#   check-sources.sh <project-root> <manifest>
#
# Exit 0: every listed file is unchanged; nothing is printed.
# Exit 1: content differs; the changed or missing files are printed, one
#         "<path>: FAILED..." line each (shasum's wording).
# Exit 2: the check itself could not be made: missing, empty or malformed
#         manifest, or shasum output that does not account for every entry.
#         Never read as "nothing changed".
set -uo pipefail
die() { echo "check-sources: $*" >&2; exit 2; }
[ $# -eq 2 ] || die "usage: check-sources.sh <project-root> <manifest>"
root=$1; manifest=$2
[ -d "$root" ] || die "no project root $root"
[ -f "$manifest" ] || die "no manifest $manifest"
entries=$(grep -c '' "$manifest")
[ "$entries" -gt 0 ] || die "manifest $manifest is empty"
bad=$(grep -nvE '^[0-9a-f]{64}  [^ ].*$' "$manifest" | head -3)
[ -z "$bad" ] || die "malformed lines in $manifest: $bad"
abs=$(cd "$(dirname "$manifest")" && pwd)/$(basename "$manifest")
out=$(cd "$root" && shasum -a 256 -c "$abs" 2>/dev/null)
status=$?
ok=$(grep -c ': OK$' <<< "$out")
changed=$(grep -v ': OK$' <<< "$out" | grep -v '^$')
nchanged=$(grep -c '' <<< "$changed")
[ -n "$changed" ] || nchanged=0
# Every entry must be accounted for by exactly one result line.
[ $((ok + nchanged)) -eq "$entries" ] \
  || die "shasum reported $ok OK and $nchanged changed for $entries entries in $manifest"
if [ "$nchanged" -gt 0 ]; then
  [ $status -ne 0 ] || die "shasum reported changes but exited 0"
  printf '%s\n' "$changed"
  exit 1
fi
[ $status -eq 0 ] || die "shasum failed (exit $status) with every entry OK"
exit 0
