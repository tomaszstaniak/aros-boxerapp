#!/bin/bash
# Assemble "BoxTest Game.boxer", a gamebox in the original's layout around
# BOXTEST.COM: Game Info.plist with a fixed identifier and one default
# launcher, and a lettered, labelled hard-disk folder. Names contain spaces
# on purpose (as real gameboxes do).
# Usage: make-boxtest-gamebox.sh <BOXTEST.COM> <output-dir>
set -euo pipefail
[ $# -eq 2 ] || { echo "usage: $0 <BOXTEST.COM> <output-dir>" >&2; exit 2; }
[ -f "$1" ] || { echo "missing $1" >&2; exit 2; }
box="$2/BoxTest Game.boxer"
rm -rf "$box"
mkdir -p "$box/C BoxTest.harddisk"
cp "$1" "$box/C BoxTest.harddisk/BOXTEST.COM"
cat > "$box/Game Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>BXGameIdentifier</key>
	<string>8E7B1C9A-0B6E-4F4A-9C35-0B0C5E1D2A11</string>
	<key>BXGameIdentifierType</key>
	<integer>1</integer>
	<key>BXLaunchers</key>
	<array>
		<dict>
			<key>BXLauncherTitle</key>
			<string>BoxTest save</string>
			<key>BXLauncherPath</key>
			<string>C BoxTest.harddisk/BOXTEST.COM</string>
			<key>BXLauncherArguments</key>
			<string>save</string>
			<key>BXLauncherIsDefault</key>
			<true/>
		</dict>
	</array>
</dict>
</plist>
PLIST
echo "$box"
