#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Source/asset check of make-package.sh: compare the files BoxerUI was made
# from, as build-ui.sh recorded them in BUILDINFO.txt, with what is there now.
#
#   check-build-inputs.sh <project-root> <ui BUILDINFO.txt>
#
# Two lists are compared: "sources:" (every project file the compiles read,
# tools/depmanifest.py) and "build inputs:" (what the generators read:
# embedded PNG art and cover templates, the AROS ISO's font descriptions,
# the generating scripts, and the headers and generated files the compiles
# read from build/, such as the SDL3 headers).
#
# Exit 0: every listed file is unchanged; nothing is printed.
# Exit 1: files changed or are missing; one "<path>: FAILED..." line each.
# Exit 2: the check could not be made (no record, an empty or malformed
#         list, e.g. a BUILDINFO from before "build inputs:" existed).
set -uo pipefail
die() { echo "check-build-inputs: $*" >&2; exit 2; }
[ $# -eq 2 ] || die "usage: check-build-inputs.sh <project-root> <ui BUILDINFO.txt>"
root=$1; info=$2
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/build-inputs.XXXXXX") || die "mktemp failed"
trap 'rm -rf "$tmp"' EXIT
"$here/buildinfo-section.sh" "$info" sources > "$tmp/m" \
  || die "no source list in $info; rebuild (build-ui.sh)"
"$here/buildinfo-section.sh" "$info" "build inputs" >> "$tmp/m" \
  || die "no build input list in $info; rebuild (build-ui.sh)"
"$here/check-sources.sh" "$root" "$tmp/m"
