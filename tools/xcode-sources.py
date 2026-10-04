#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
"""Print the DOSBox C/C++ sources that an Xcode target of the original Boxer compiles.

The original build recipe is Boxer.xcodeproj; a probe must use its source set,
not whatever `find` returns. Files are matched by basename against DOSBox/src;
an unmatched or ambiguous basename is an error, not a silent skip.
Usage: xcode-sources.py <boxer-checkout> [target-name]   (default target: Boxer)
"""
import collections, os, re, sys

if len(sys.argv) not in (2, 3):
    sys.exit(__doc__)
root, target = sys.argv[1], (sys.argv[2] if len(sys.argv) == 3 else "Boxer")
pbx = os.path.join(root, "Boxer.xcodeproj", "project.pbxproj")
text = open(pbx, encoding="utf-8").read()

refs = dict(re.findall(r'(\w{24}) /\* [^*]+ \*/ = \{isa = PBXFileReference;[^}]*?path = "?([^";]+)"?;', text))
build_files = dict(re.findall(r'(\w{24}) /\* [^*]+ \*/ = \{isa = PBXBuildFile; fileRef = (\w{24})', text))
tgt = re.search(r'\w{24} /\* ' + re.escape(target) + r' \*/ = \{\s*isa = PBXNativeTarget;.*?buildPhases = \((.*?)\);', text, re.S)
if not tgt:
    sys.exit(f"target not found: {target}")
phase_ids = re.findall(r'(\w{24}) /\* Sources \*/', tgt.group(1))
if len(phase_ids) != 1:
    sys.exit(f"expected one Sources phase in {target}, found {len(phase_ids)}")
phase = re.search(phase_ids[0] + r' /\* Sources \*/ = \{\s*isa = PBXSourcesBuildPhase;.*?files = \((.*?)\);', text, re.S)
names = {os.path.basename(refs.get(build_files.get(i), "")) for i in re.findall(r'(\w{24}) /\*', phase.group(1))}

tree = collections.defaultdict(list)
for d, _, files in os.walk(os.path.join(root, "DOSBox", "src")):
    for f in files:
        tree[f].append(os.path.relpath(os.path.join(d, f), root))

errors = 0
# Sources outside DOSBox/src (Boxer's own .m/.mm) are not part of the core.
for name in sorted(n for n in names if n.endswith((".cpp", ".c")) and n in tree):
    hits = tree[name]
    if len(hits) != 1:
        print(f"ambiguous: {name}: {hits}", file=sys.stderr); errors += 1
    else:
        print(hits[0])
sys.exit(1 if errors else 0)
