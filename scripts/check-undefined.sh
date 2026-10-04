#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Reject an AROS executable that still has undefined symbols.
#
#   scripts/check-undefined.sh <nm> <binary> [allowlist-file]
#
# collect-aros can exit 0 and leave unresolved symbols when its own nm call
# fails (collect-aros runs nm on the output path unquoted), so
# the link exit code is not enough. Exit codes:
#   0  nm ran and every undefined symbol is on the allowlist
#   3  nm itself failed (tool missing, unreadable or unknown file format)
#   4  undefined symbols outside the allowlist (listed on stderr)
# The allowlist holds one symbol name per line ('#' comments). For BoxerUI it
# is empty: a correctly linked AROS executable has no undefined symbols
# (checked on both ABIs, 2026-10-01).
set -uo pipefail
[ $# -ge 2 ] && [ $# -le 3 ] || { echo "usage: check-undefined.sh <nm> <binary> [allowlist]" >&2; exit 2; }
nm=$1 bin=$2 allow=${3:-}
out=$("$nm" -u "$bin" 2>&1)
rc=$?
if [ $rc -ne 0 ]; then
  echo "check-undefined: nm failed (exit $rc) on $bin:" >&2
  echo "$out" >&2
  exit 3
fi
allowed=()
if [ -n "$allow" ]; then
  [ -r "$allow" ] || { echo "check-undefined: allowlist $allow unreadable" >&2; exit 3; }
  while read -r s _; do
    [ -z "$s" ] || [ "${s#\#}" != "$s" ] || allowed+=("$s")
  done < "$allow"
fi
bad=()
while read -r kind sym _; do
  [ -n "${kind:-}" ] || continue
  [ -n "${sym:-}" ] || { sym=$kind; }
  ok=0
  for a in "${allowed[@]+"${allowed[@]}"}"; do [ "$a" = "$sym" ] && ok=1; done
  [ $ok -eq 1 ] || bad+=("$sym")
done <<< "$out"
if [ ${#bad[@]} -gt 0 ]; then
  echo "check-undefined: ${#bad[@]} unexpected undefined symbol(s) in $bin:" >&2
  printf '  %s\n' "${bad[@]}" >&2
  exit 4
fi
echo "check-undefined: ok, no undefined symbols outside the allowlist ($bin)"
