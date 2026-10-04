#!/bin/bash
# Gameboxes for the failure paths F1-F4 (stage 2 acceptance), next to the
# valid "BoxTest Game.boxer" from make-boxtest-gamebox.sh.
#   Corrupt Info.boxer    - Game Info.plist is not a property list (F2)
#   Missing Drive.boxer   - default launcher on a drive folder that is absent (F3)
#   ReadOnly NoId.boxer   - no identifier; made read-only on the guest (F4a)
#   ReadOnly Game.boxer   - identifier present; made read-only on the guest (F4b)
#   Locked NoId/Game.boxer - copies of the two above for the C:Lock cases (F4c/F4d)
# F1 (missing gamebox) needs no fixture.
# Usage: make-error-gameboxes.sh <BOXTEST.COM> <output-dir>
set -euo pipefail
[ $# -eq 2 ] || { echo "usage: $0 <BOXTEST.COM> <output-dir>" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
out=$2
"$here/make-boxtest-gamebox.sh" "$1" "$out" >/dev/null
src="$out/BoxTest Game.boxer"

rm -rf "$out/Corrupt Info.boxer" "$out/Missing Drive.boxer" "$out/ReadOnly NoId.boxer" "$out/ReadOnly Game.boxer"
cp -R "$src" "$out/Corrupt Info.boxer"
printf 'this is not a plist\n' > "$out/Corrupt Info.boxer/Game Info.plist"

cp -R "$src" "$out/Missing Drive.boxer"
sed -i '' 's#C BoxTest.harddisk/BOXTEST.COM#D Missing.harddisk/BOXTEST.COM#' "$out/Missing Drive.boxer/Game Info.plist"

cp -R "$src" "$out/ReadOnly NoId.boxer"
python3 - "$out/ReadOnly NoId.boxer/Game Info.plist" <<'PY'
import re, sys
p = sys.argv[1]; t = open(p).read()
t = re.sub(r'\s*<key>BXGameIdentifier</key>\s*<string>[^<]*</string>', '', t)
t = re.sub(r'\s*<key>BXGameIdentifierType</key>\s*<integer>[^<]*</integer>', '', t)
open(p, 'w').write(t)
PY
cp -R "$src" "$out/ReadOnly Game.boxer"
# Same pair for the volume write-protect cases (C:Lock), kept separate so the
# protection-bit cases cannot alter them first.
rm -rf "$out/Locked NoId.boxer" "$out/Locked Game.boxer"
cp -R "$out/ReadOnly NoId.boxer" "$out/Locked NoId.boxer"
cp -R "$src" "$out/Locked Game.boxer"
