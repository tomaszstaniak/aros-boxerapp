#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Print one indented list of a BUILDINFO.txt ("<header>:" followed by
# lines indented by two spaces), without the indentation.
#
#   buildinfo-section.sh <BUILDINFO.txt> <header>
#
# Exit 0 with the lines; exit 2 when the file or the section is missing or
# the section has no lines.
set -uo pipefail
[ $# -eq 2 ] || { echo "buildinfo-section: usage: buildinfo-section.sh <BUILDINFO.txt> <header>" >&2; exit 2; }
info=$1; head=$2
[ -f "$info" ] || { echo "buildinfo-section: no $info" >&2; exit 2; }
out=$(awk -v h="$head:" 'index($0, h) == 1 { f = 1; n++; next }
  f && /^  / { sub(/^  /, ""); print; next } { f = 0 }
  END { if (n != 1) exit 3 }' "$info")
rc=$?
[ $rc -ne 3 ] || { echo "buildinfo-section: $info has no single \"$head:\" section" >&2; exit 2; }
[ $rc -eq 0 ] && [ -n "$out" ] || { echo "buildinfo-section: \"$head:\" in $info lists nothing" >&2; exit 2; }
printf '%s\n' "$out"
