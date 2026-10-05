#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Build BoxerUI (src/ui, the Zune front end with the embedded core) for one
# ABI. Needs the core library from build-core.sh for the same ABI.
#
#   [BOXER_HOST=sdl2|sdl3] scripts/build-ui.sh <abiv11|mainline-v1>
#   (sdl3: build/<abi>/ui-sdl3, core from core-sdl3, see scripts/env.sh)
#
# Output: build/<abi>/ui-sdl3/BoxerUI (ui/ for sdl2; the default host per ABI
# is in env.sh), a build log and BUILDINFO.txt (compiler, SDK, hashes), plus
# stage/ there holding the files a test installation needs (BoxerUI, Fonts/).
# The host unit test of src/ui/ui_logic.cpp runs first; a failing test stops
# the build, because the guest run would then test known-wrong logic.
# No strip: a full strip breaks AROS x86_64 relocations (the program then
# fails at its first OpenLibrary).
set -euo pipefail
die() { echo "build-ui: $*" >&2; exit 2; }
[ $# -eq 1 ] || die "usage: build-ui.sh <abiv11|mainline-v1>"
here=$(cd "$(dirname "$0")" && pwd)
set +e; . "$here/env.sh" "$1"; rc=$?; set -e
[ $rc -eq 0 ] || die "environment setup failed"
# Some AROS cross toolchains have their build tree's linker path compiled
# into collect-aros; AROS_TOOLCHAIN_MOUNT (optional) names a directory that
# must be present before linking, e.g. a mounted build volume.
[ -z "${AROS_TOOLCHAIN_MOUNT:-}" ] || [ -d "$AROS_TOOLCHAIN_MOUNT" ] \
  || die "AROS_TOOLCHAIN_MOUNT=$AROS_TOOLCHAIN_MOUNT is not available (volume not mounted?)"

src="$PROJECT_ROOT/src/ui"
# BOXER_UI_OUT: a separate output directory (e.g. build/<abi>/ui-assetsrestore)
# so a side build never overwrites the default or a frozen tree.
out="${BOXER_UI_OUT:-$BUILD_DIR/ui$HOST_SUFFIX}"
core_src="$WORK_DIR"
lib="$BUILD_DIR/core$HOST_SUFFIX/libboxer-dosbox.a"
[ -f "$lib" ] || die "core library missing: $lib (run build-core.sh $1)"
mkdir -p "$out/host" "$out/stage" "$out/gen" "$out/obj"
# A stale staged binary from an earlier build must never survive a failed one.
rm -rf "$out/stage/BoxerUI" "$out/BoxerUI" "$out/stage/Fonts"

# Original Boxer artwork (GPL-2.0) embedded as RGBA arrays; see
# assets/README.md.
art="$PROJECT_ROOT/assets/runtime/boxer"
# Feather (MIT) replacements for the Apple system images Boxer names
# (assets/replacement-assets.json). The four Welcome/Gamebox icons are
# Boxer's originals (assets/README.md).
repl="$PROJECT_ROOT/assets/runtime/replacements"
python3 "$PROJECT_ROOT/tools/png2inc.py" "$out/gen/boxer_assets.inc" \
  WelcomeSpotlight="$art/WelcomeSpotlight.png" WelcomeFocusRing="$art/WelcomeFocusRing.png" \
  CPU="$art/CPU.png" Mouse="$art/Mouse.png" Joystick="$art/Joystick.png" Drives="$art/Drives.png" \
  LauncherListTemplate="$art/LauncherListTemplate.png" DocumentationTemplate="$art/DocumentationTemplate.png" \
  PauseTemplate="$art/PauseTemplate.png" PlayTemplate="$art/PlayTemplate.png" \
  FastForwardTemplate="$art/FastForwardTemplate.png" \
  Volume0PercentCroppedTemplate="$art/Volume0PercentCroppedTemplate.png" \
  Volume100PercentTemplate="$art/Volume100PercentTemplate.png" \
  FavoriteOutlineTemplate="$art/FavoriteOutlineTemplate.png" RecentItemsTemplate="$art/RecentItemsTemplate.png" \
  HardDiskTemplate="$art/HardDiskTemplate.png" CDROMTemplate="$art/CDROMTemplate.png" \
  DisketteTemplate="$art/DisketteTemplate.png" LaunchPanelDivider="$art/LaunchPanelDivider.png" \
  GameFolder="$art/gamefolder-128.png" Import="$art/import.png" Prompt="$art/prompt-128.png" \
  Gamebox="$art/Game.png" RevealTemplate="$repl/RevealTemplate.png" \
  FullScreenTemplate="$repl/FullScreenTemplate.png" \
  LockLockedTemplate="$repl/LockLockedTemplate.png" LockUnlockedTemplate="$repl/LockUnlockedTemplate.png"
# Private outline font descriptions (glyph clipping on mainline v1; see
# tools/make-fonts.py). AROS_ISO_FONTS: an AROS ISO's Fonts/ directory.
[ -n "${AROS_ISO_FONTS:-}" ] || die "AROS_ISO_FONTS is not set (an AROS ISO's Fonts/ directory)"
python3 "$PROJECT_ROOT/tools/make-fonts.py" "$AROS_ISO_FONTS" "$out/stage/Fonts"
# Bitstream Vera terms: the notice travels with the modified descriptions.
cp "$PROJECT_ROOT/LICENSES/Bitstream-Vera.txt" "$out/stage/Fonts/"

c++ -std=c++17 -Wall -O1 -o "$out/host/ui_logic_test" "$src/tests/ui_logic_test.cpp" "$src/ui_logic.cpp"
"$out/host/ui_logic_test"
# Launch panel rows, filter and session rules, with the model's executable
# scan and game settings (scratch tree removed after a passing run).
c++ -std=c++17 -Wall -O1 -o "$out/host/launchpanel_test" "$src/tests/launchpanel_test.cpp" \
  "$src/launchpanel_logic.cpp" "$PROJECT_ROOT"/src/model/*.cpp "$PROJECT_ROOT/src/emulator/filesystem.cpp"
"$out/host/launchpanel_test" "$out/host/launchpanel scratch"

sources=("$src/main.cpp" "$src/classes.cpp" "$src/gfx.cpp" "$src/ui_logic.cpp" "$src/launchpanel_logic.cpp")
# The emulator layer and the model as in build-smoke.sh: files that include
# the core's headers compile as gnu++14 (DOSBox 0.74's dynamic exception
# specifications), the model and the UI as C++17.
core_flags=(-iquote "$core_src/DOSBox/include" -I"$core_src/DOSBox" -I"$core_src/DOSBox/include"
  -iquote "$core_src/DOSBox/src/dos" -I"$core_src/Boxer" "${HOST_CFLAGS[@]+"${HOST_CFLAGS[@]}"}")
core14=(src/emulator/emulator.cpp src/emulator/coalface.cpp src/emulator/filesystem.cpp
  src/platform/aros/rawkeys.cpp src/platform/aros/coreinput.cpp src/platform/aros/corecontrol.cpp)
model17=(src/model/fsutil.cpp src/model/plist.cpp src/model/gamebox.cpp
  src/model/datalocations.cpp src/model/shadowfs.cpp src/model/programs.cpp src/model/importsource.cpp src/model/installerscan.cpp src/model/sourcecopy.cpp src/platform/aros/session_setup.cpp
  src/platform/aros/wbopen.cpp)
# __FILE__ in assertion messages names the build host's directories; map
# them to project-relative names (GCC tries the last matching map first,
# so the core tree, which may lie inside the project, comes last).
pmap=(-ffile-prefix-map="$PROJECT_ROOT/"= -ffile-prefix-map="$core_src/"=boxer/)
log="$out/build.log"
: > "$log"
objs=()
set +e
rc=0
for f in "${core14[@]}"; do
  o="$out/obj/$(basename "$f").o"; objs+=("$o")
  "$AROS_CXX" "${pmap[@]}" -std=gnu++14 -O2 -Wall -Wno-deprecated -Wno-unknown-pragmas -fno-strict-aliasing \
    "${core_flags[@]}" -I"$AROS_SDK/include" -c "$PROJECT_ROOT/$f" -o "$o" >> "$log" 2>&1 || rc=1
done
for f in "${model17[@]}"; do
  o="$out/obj/$(basename "$f").o"; objs+=("$o")
  "$AROS_CXX" "${pmap[@]}" -std=gnu++17 -O2 -Wall -I"$AROS_SDK/include" -c "$PROJECT_ROOT/$f" -o "$o" >> "$log" 2>&1 || rc=1
done
for f in "${sources[@]}"; do
  o="$out/obj/ui-$(basename "$f").o"; objs+=("$o")
  "$AROS_CXX" "${pmap[@]}" -std=c++17 -O2 -Wall -Wno-narrowing -I"$AROS_SDK/include" -I"$out/gen" \
    -DBOXER_ABI="\"$BOXER_ABI\"" ${BOXER_UI_DEFS:-} -c "$f" -o "$o" >> "$log" 2>&1 || rc=1
done
# Mainline's libgcc never fills dwarf_reg_size_table, so every C++ throw
# aborts (scripts/build-unwind-fix.sh). Link a rebuilt unwind-dw2.o first,
# as scripts/build-smoke.sh does.
unwind=()
if [ "$BOXER_ABI" = mainline-v1 ]; then
  fix="$BUILD_DIR/unwind-fix/unwind-dw2.o"
  [ -f "$fix" ] || "$here/build-unwind-fix.sh" >&2 \
    || die "could not build the unwind fix"
  unwind=("$fix")
fi
if [ $rc -eq 0 ]; then
  "$AROS_CXX" -o "$out/BoxerUI" "${objs[@]}" "${unwind[@]+"${unwind[@]}"}" "$lib" "${HOST_LIBS[@]}" >> "$log" 2>&1
  rc=$?
fi
if [ $rc -eq 0 ] && [ "$BOXER_ABI" = mainline-v1 ]; then
  # Read nm into a variable first: grep -q under pipefail trips on SIGPIPE.
  syms=$("$AROS_TOOLCHAIN/x86_64-aros-nm" "$out/BoxerUI")
  if ! grep ' dwarf_reg_size_table$' <<< "$syms" > /dev/null; then
    rm -f "$out/BoxerUI"
    die "unwind fix not linked: no dwarf_reg_size_table in BoxerUI"
  fi
fi
set -e
cat "$log"
[ $rc -eq 0 ] || { echo "build-ui: compile/link failed (exit $rc), log $log" >&2; exit 1; }

# collect-aros can exit 0 with unresolved symbols when its nm call fails
# (it runs nm on an unquoted output path); check explicitly. A failing nm
# and any undefined symbol outside the (empty) allowlist both reject the
# artifact: it is deleted and nothing is staged.
# BOXER_UI_DEFS: extra -D flags for the UI sources only, e.g.
# -DBOXER_MEMTRACE (memory checkpoints, main.cpp); off by default.
# BOXER_UI_NM is a test hook only (simulating nm failure or extra symbols).
nm_tool="${BOXER_UI_NM:-$AROS_TOOLCHAIN/x86_64-aros-nm}"
set +e
"$here/check-undefined.sh" "$nm_tool" "$out/BoxerUI" \
  "$here/ui-undefined-allowlist.txt"
rc=$?
set -e
if [ $rc -ne 0 ]; then
  rm -f "$out/BoxerUI"
  echo "build-ui: artifact rejected by the undefined-symbol check (exit $rc); nothing staged" >&2
  exit 1
fi
cp "$out/BoxerUI" "$out/stage/BoxerUI"
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "abi: $BOXER_ABI"
  echo "host: $BOXER_HOST"
  [ "$BOXER_HOST" = sdl3 ] && echo "sdl3 library: $(shasum -a 256 "$BUILD_DIR/sdl3/lib/libSDL3_ctl.a" | cut -d' ' -f1) (build/$BOXER_ABI/sdl3/BUILDINFO.txt)"
  echo "libsets: $("$AROS_TOOLCHAIN/x86_64-aros-nm" "$out/BoxerUI" | grep -oE '__aros_libset_[A-Za-z0-9_]+' | sort -u | sed 's/__aros_libset_//' | tr '\n' ' ')"
  echo "compiler: $AROS_CXX"
  "$AROS_CXX" --version | head -1
  echo "sysroot: $("$AROS_CXX" -print-sysroot)"
  echo "sdk include: $AROS_SDK/include"
  echo "file: $(file -b "$out/BoxerUI")"
  echo "sha256: $(shasum -a 256 "$out/BoxerUI" | cut -d' ' -f1)"
  echo "size: $(stat -f %z "$out/BoxerUI")"
  echo "sources:"
  for f in "${sources[@]}" "$src/classes.h" "$src/gfx.h" "$src/ui_logic.h" "$src/launchpanel_logic.h"; do
    echo "  $(shasum -a 256 "$f" | cut -d' ' -f1)  ${f#$PROJECT_ROOT/}"
  done
  for f in "${core14[@]}" "${model17[@]}" src/platform/aros/coreinput.h src/platform/aros/corecontrol.h src/platform/aros/session_setup.h src/platform/aros/wbopen.h; do
    echo "  $(shasum -a 256 "$PROJECT_ROOT/$f" | cut -d' ' -f1)  $f"
  done
  echo "core library: $(shasum -a 256 "$lib" | cut -d' ' -f1)  ${lib#$PROJECT_ROOT/}"
  echo "embedded art: $(shasum -a 256 "$out/gen/boxer_assets.inc" | cut -d' ' -f1)  (tools/png2inc.py, assets/runtime/boxer)"
  echo "fonts dir: $AROS_ISO_FONTS"
  (cd "$out/stage/Fonts" && shasum -a 256 *) | sed 's/^/  Fonts\//' 
  [ "$BOXER_ABI" = mainline-v1 ] && echo "unwind fix: $(shasum -a 256 "$BUILD_DIR/unwind-fix/unwind-dw2.o" | cut -d' ' -f1)  unwind-dw2.o linked before libgcc (dwarf_reg_size_table present)"
  echo "undefined-symbol check: passed (nm -u empty; allowlist scripts/ui-undefined-allowlist.txt is empty)"
} > "$out/BUILDINFO.txt"
cat "$out/BUILDINFO.txt"
echo "build-ui: ok ($BOXER_ABI) -> $out/BoxerUI"
