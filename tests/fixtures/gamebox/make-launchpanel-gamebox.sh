#!/bin/bash
# Assemble "LaunchTest.boxer" for the launch panel test: two launchers and
# no default one (so 2.0-alpha starts on the launch panel), and a C drive
# with several programs: BOXTEST.COM, GAME/PLAY.BAT, TOOLS/SETUP.EXE (a
# 64-byte DOS MZ program that only exits; at least the 64-byte header,
# since BXFileTypes treats a shorter .EXE as truncated, not DOS), and what the executable scan
# must leave out: TOOLS/WINAPP.EXE (MZ stub + PE header), .HIDDEN.COM (dot
# file), README.TXT. Names contain spaces as real gameboxes do.
# Usage: make-launchpanel-gamebox.sh <BOXTEST.COM> <output-dir>
set -euo pipefail
[ $# -eq 2 ] || { echo "usage: $0 <BOXTEST.COM> <output-dir>" >&2; exit 2; }
[ -f "$1" ] || { echo "missing $1" >&2; exit 2; }
box="$2/LaunchTest.boxer"
c="$box/C LaunchTest.harddisk"
rm -rf "$box"
mkdir -p "$c/GAME" "$c/TOOLS"
cp "$1" "$c/BOXTEST.COM"
printf '@echo off\r\nC:\\BOXTEST.COM save\r\n' > "$c/GAME/PLAY.BAT"
printf 'not a program\r\n' > "$c/README.TXT"
printf '\xb8\x00\x4c\xcd\x21' > "$c/.HIDDEN.COM"
python3 - "$c/TOOLS" <<'PY'
import struct, sys
d = sys.argv[1]
# DOS MZ: 2-paragraph header, code "mov ax,4C00h; int 21h".
code = b"\xb8\x00\x4c\xcd\x21"
size = 64
hdr = struct.pack("<2sHHHHHHHHHHHHH", b"MZ", size % 512, 1, 0, 2, 0x10, 0xFFFF,
                  0, 0x100, 0, 0, 0, 0x1C, 0)
open(d + "/SETUP.EXE", "wb").write((hdr.ljust(32, b"\0") + code).ljust(size, b"\0"))
# Windows PE: extended header (relocations at 0x40), new header at 0x80.
pe = bytearray(512)
pe[0:2] = b"MZ"
struct.pack_into("<H", pe, 24, 0x40)
struct.pack_into("<I", pe, 60, 0x80)
pe[0x80:0x84] = b"PE\0\0"
open(d + "/WINAPP.EXE", "wb").write(bytes(pe))
PY
cat > "$box/Game Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>BXGameIdentifier</key>
	<string>4C2A61E0-7D35-4B8E-9E0B-2B6C1D0F3A52</string>
	<key>BXGameIdentifierType</key>
	<integer>1</integer>
	<key>BXLaunchers</key>
	<array>
		<dict>
			<key>BXLauncherTitle</key>
			<string>BoxTest save</string>
			<key>BXLauncherPath</key>
			<string>C LaunchTest.harddisk/BOXTEST.COM</string>
			<key>BXLauncherArguments</key>
			<string>save</string>
		</dict>
		<dict>
			<key>BXLauncherTitle</key>
			<string>BoxTest time</string>
			<key>BXLauncherPath</key>
			<string>C LaunchTest.harddisk/BOXTEST.COM</string>
			<key>BXLauncherArguments</key>
			<string>time</string>
		</dict>
	</array>
</dict>
</plist>
PLIST
echo "$box"
