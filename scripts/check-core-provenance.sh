#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Compare the core library's BUILDINFO.txt (written by build-core.sh) with
# the tree, as make-package.sh does before packaging.
#
#   check-core-provenance.sh <project-root> <core BUILDINFO.txt>
#
# Exit 0: the core was built from work/boxer checked against the pin and
#         the patch series, and both are unchanged.
# Exit 1: pin or series changed since the build; the difference is printed
#         ("<" built, ">" now).
# Exit 3: the core was built from a source tree that was not checked
#         (BOXER_CORE_UNCHECKED_SOURCE=1); its "source check:" line is printed.
#         The pin and series in the record do not describe that tree.
# Exit 2: the record is missing, has no "source check:" line (a build
#         before the line existed) or is otherwise unusable.
set -uo pipefail
die() { echo "check-core-provenance: $*" >&2; exit 2; }
[ $# -eq 2 ] || die "usage: check-core-provenance.sh <project-root> <core BUILDINFO.txt>"
P=$1; info=$2
here=$(cd "$(dirname "$0")" && pwd)
[ -f "$info" ] || die "no $info"
grep -q '^upstream pin: [0-9a-f]\{40\}$' "$info" || die "$info has no upstream pin"
check=$(grep '^source check: ' "$info")
[ "$(grep -c '' <<< "$check")" -eq 1 ] && [ -n "$check" ] \
  || die "$info has no single \"source check:\" line; rebuild the core (build-core.sh)"
case "$check" in
  "source check: work/boxer equals pin + series"*) ;;
  "source check: UNCHECKED"*) printf '%s\n' "$check"; exit 3 ;;
  *) die "$info: unknown source check \"$check\"" ;;
esac
now=$("$here/core-provenance.sh" "$P") || die "core-provenance.sh failed"
delta=$(diff <(sed '/^library: /d; /^source check: /d' "$info") <(printf '%s\n' "$now"))
if [ -n "$delta" ]; then
  printf '%s\n' "$delta"
  exit 1
fi
exit 0
