#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Build the binary package of BoxerUI for one ABI from an existing build.
#
#   [BOXER_HOST=sdl2|sdl3] [BOXER_UI_OUT=dir] scripts/make-package.sh <abiv11|mainline-v1>
#
# Packages the BoxerUI of the ABI's default host (env.sh: sdl3 on ABIv11,
# sdl2 on mainline) from build/<abi>/ui<suffix>, or from BOXER_UI_OUT (a
# side build). Output in build/<abi>/package<suffix>/
# (package-sdl3 for SDL3; BOXER_PKG_OUT overrides):
#   Boxer/ + Boxer.info               the AROS drawer (what users install) and its icon
#   boxer-<ver>.<target>.lha          the drawer packed as arospkg expects
#   MANIFEST-<abi>.sha256             every file in the drawer + the archive
# Contents and decisions: packaging/README.md. The package is refused when
# the binary does not match its BUILDINFO, has undefined symbols, or when a
# source file listed in BUILDINFO has changed since the build (the shipped
# source archive would then not correspond to the binary).
set -euo pipefail
die() { echo "make-package: $*" >&2; exit 2; }
[ $# -eq 1 ] || die "usage: make-package.sh <abiv11|mainline-v1>"
here=$(cd "$(dirname "$0")" && pwd)
set +e; . "$here/env.sh" "$1"; rc=$?; set -e
[ $rc -eq 0 ] || die "environment setup failed"
P="$PROJECT_ROOT"
ver=$(tr -d ' \n' < "$P/packaging/VERSION")
case "$BOXER_ABI" in
  abiv11) target=x86_64-aros-v11; abiname="ABIv11 (AROS One)"; sdkrepo=${AROS_V11_SRC:-} ;;
  mainline-v1) target=x86_64-aros; abiname="mainline AROS (ABI v1)"; sdkrepo=${AROS_V1_SRC:-} ;;
esac
ui="${BOXER_UI_OUT:-$BUILD_DIR/ui$HOST_SUFFIX}"; info="$ui/BUILDINFO.txt"; bin="$ui/BoxerUI"
[ -f "$bin" ] && [ -f "$info" ] || die "no build: run scripts/build-ui.sh $BOXER_ABI"

# 1. The binary is the one BUILDINFO describes, was built for this host
# (an SDL2 binary must not ship as the SDL3 package), and links completely.
[ "$(sed -n 's/^host: //p' "$info")" = "$BOXER_HOST" ] || die "$info is not a $BOXER_HOST build"
want=$(sed -n 's/^sha256: //p' "$info")
got=$(shasum -a 256 "$bin" | cut -d' ' -f1)
[ "$want" = "$got" ] || die "BoxerUI sha256 $got differs from BUILDINFO ($want)"
"$here/check-undefined.sh" "$AROS_TOOLCHAIN/x86_64-aros-nm" "$bin" "$here/ui-undefined-allowlist.txt" \
  || die "undefined-symbol check failed"
# 2. The tree still holds the sources that binary was built from.
awk '/^sources:/{f=1;next} /^core library/{f=0} f{print $1"  "$2}' "$info" > "$BUILD_DIR/.pkg-src.sha256"
# BOXER_PKG_ALLOW_STALE=1 packages anyway for a dry run of the recipe; the
# result is marked NOT CORRESPONDING and must never be distributed.
#
# BOXER_PKG_ACCEPTED_BINARY=<sha256> + BOXER_PKG_SOURCE_NOTE=<text>: package
# an already-accepted binary whose sources were changed afterwards in ways
# that do not change the program (e.g. comments only). The sha256 must equal
# the binary's; the changed files and the note are written to BUILDINFO.txt.
# The script cannot prove the change is behaviour-neutral: whoever sets the
# note must have checked it (for example by a rebuild that differs only in
# the embedded build time).
stale=0; srcnote=""
# shasum exits 1 on a mismatch and grep 1 on none; only the text counts.
changed=$( (cd "$P" && shasum -a 256 -c "$BUILD_DIR/.pkg-src.sha256" 2>/dev/null) | grep -v ': OK$' || true)
if [ -n "$changed" ]; then
  if [ -n "${BOXER_PKG_ACCEPTED_BINARY:-}" ]; then
    [ "$BOXER_PKG_ACCEPTED_BINARY" = "$got" ] \
      || die "BOXER_PKG_ACCEPTED_BINARY ($BOXER_PKG_ACCEPTED_BINARY) is not this BoxerUI ($got)"
    [ -n "${BOXER_PKG_SOURCE_NOTE:-}" ] || die "BOXER_PKG_ACCEPTED_BINARY needs BOXER_PKG_SOURCE_NOTE"
    srcnote="$changed"
    echo "make-package: packaging accepted binary $got; sources changed since the build:" >&2
    echo "$changed" | sed 's/^/  /' >&2
  elif [ "${BOXER_PKG_ALLOW_STALE:-0}" = 1 ]; then
    stale=1; ver="$ver-STALE"
    echo "make-package: WARNING sources changed since the build; dry run, not distributable" >&2
  else
    die "sources changed since the build; rebuild (build-core.sh, build-ui.sh) before packaging"
  fi
fi
rm -f "$BUILD_DIR/.pkg-src.sha256"
lib="$BUILD_DIR/core$HOST_SUFFIX/libboxer-dosbox.a"
[ "$(sed -n 's/^core library: \([0-9a-f]*\).*/\1/p' "$info")" = "$(shasum -a 256 "$lib" | cut -d' ' -f1)" ] \
  || die "core library differs from the one BoxerUI was linked with"

out="${BOXER_PKG_OUT:-$BUILD_DIR/package$HOST_SUFFIX}"; pkg="$out/Boxer"
[ -d "$out" ] && chmod -R u+w "$out"; rm -rf "$out"; mkdir -p "$pkg/conf" "$pkg/LICENSES"

# 3. Program, fonts, runtime files. Boxer's artwork is embedded in BoxerUI
# (tools/png2inc.py), so no image or sound file is shipped separately.
cp "$bin" "$pkg/BoxerUI"
cp -R "$ui/stage/Fonts" "$pkg/Fonts"
# Only these two are read: sessionConfigFiles() passes no game profiles yet.
for c in Preflight Launch; do
  cmp -s "$WORK_DIR/Resources/Configurations/$c.conf" "$UPSTREAM_DIR/Resources/Configurations/$c.conf" \
    || die "$c.conf in work/ differs from the upstream pin"
  cp "$UPSTREAM_DIR/Resources/Configurations/$c.conf" "$pkg/conf/"
done
strings="$UPSTREAM_DIR/Resources/Base.lproj/DOSBox.strings"
chmod 644 "$pkg"/conf/*.conf
python3 "$P/tools/strings2table.py" "$strings" "$pkg/dosbox.msg" >/dev/null
# Icons with Boxer's own art (D2, 2026-10-04). The old make-icon.py tool
# icon was a plain rectangle, and every game icon BoxerUI writes copies
# BoxerUI.info's image. Boxer.info makes the drawer visible in Wanderer's
# default view; conf/GamesFolder.info is the template BoxerUI copies when it
# creates the games folder (art: gamefolder.icns, assets manifest).
art="$P/assets/runtime/boxer"
python3 "$P/tools/mkicon.py" "$pkg/BoxerUI.info" tool --image "$art/boxer-128.png" --size 48 --stack 65536 >/dev/null
python3 "$P/tools/mkicon.py" "$out/Boxer.info" drawer --image "$art/boxer-128.png" --size 48 >/dev/null
python3 "$P/tools/mkicon.py" "$pkg/conf/GamesFolder.info" drawer --image "$art/gamefolder-128.png" --size 48 >/dev/null

# 4. Notices (documentation/licenses.md, "Distribution checklist").
cp "$P/COPYING" "$pkg/"
cp "$P"/LICENSES/*.txt "$pkg/LICENSES/"
cp "$P/assets/NOTICE-boxer.txt" "$pkg/"
cp "$P/assets/NOTICE-replacements.txt" "$pkg/"
cp "$P/documentation/licenses.md" "$pkg/licenses.md"
cp "$P/packaging/requirements-$BOXER_ABI.txt" "$pkg/REQUIREMENTS.txt"
cp "$P/packaging/Install-Assign" "$pkg/Install-Assign"
# Install-Assign is a script: IconX runs it from its icon in a window that
# stays open (WAIT), changed into the icon's drawer, so DIR is not needed.
python3 "$P/tools/mkicon.py" "$pkg/Install-Assign.info" project --default-tool C:IconX \
  --tooltype "WINDOW=CON:40/40/640/220/Boxer Install-Assign/AUTO/WAIT/CLOSE" >/dev/null

# 5. Corresponding source (GPL-2.0 s.3(a)): the project files that build
# uses, plus the used part of the pinned Boxer tree. Never var/,
# work/, build/ or local.env (private or machine-specific).
srcname="boxer-$ver-source"; sx="$out/.src/$srcname"
mkdir -p "$sx/aros-boxerapp" "$sx/boxer-upstream/Resources/Base.lproj"
srcpaths=(.gitignore COPYING README.md upstreams.json upstreams.example.json local.env.example LICENSES documentation src scripts
  tools patches packaging assets tests third_party)
# In a Git checkout only tracked files go in, so local material in those
# directories (ignored or untracked) is never published by accident.
if git -C "$P" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  (cd "$P" && git ls-files -z -- "${srcpaths[@]}" | tar cf - --null -T -) | (cd "$sx/aros-boxerapp" && tar xf -)
else
  (cd "$P" && tar cf - --exclude __pycache__ --exclude .DS_Store "${srcpaths[@]}") | (cd "$sx/aros-boxerapp" && tar xf -)
fi
pin=$(git -C "$UPSTREAM_DIR" rev-parse HEAD)
[ "$pin" = "$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["repositories"]["boxer"]["commit"])' "$P/upstreams.json")" ] \
  || die "upstream/boxer is not at the pin in upstreams.json"
(cd "$UPSTREAM_DIR" && tar cf - --exclude .DS_Store Boxer DOSBox Readme.md Resources/Configurations) \
  | (cd "$sx/boxer-upstream" && tar xf -)
cp "$strings" "$sx/boxer-upstream/Resources/Base.lproj/"
printf 'repository: https://github.com/alinebee/Boxer.git\ncommit: %s\n' "$pin" > "$sx/UPSTREAM-PIN.txt"
cp "$P/packaging/SOURCE-README.txt" "$sx/"
# Published copies of BUILDINFO name the configured directories by their
# variable (see local.env.example), not by this machine's paths.
pubinfo() {
  sed -e "s|$AROS_TOOLCHAIN|\$AROS_TOOLCHAIN|g" -e "s|$AROS_SDK|\$AROS_SDK|g" \
      -e "s|${AROS_ISO_FONTS:-/nonexistent}|\$AROS_ISO_FONTS|g" -e "s|$P/|<project>/|g" \
      -e "s|$HOME|~|g" "$1"
}
pubinfo "$info" > "$sx/BUILDINFO-$BOXER_ABI.txt"
srcarc="$srcname.tar.gz"
(cd "$out/.src" && COPYFILE_DISABLE=1 tar --uid 0 --gid 0 --uname root --gname wheel -czf "$pkg/$srcarc" "$srcname")
chmod -R u+w "$out/.src"; rm -rf "$out/.src"

# 6. BUILDINFO, user ReadMe, checksums.
# AROS_V11_SRC / AROS_V1_SRC: the AROS source checkout the SDK was built
# from (optional); its revision is recorded in BUILDINFO.txt.
sdkrev=$(git -C "${sdkrepo:-/nonexistent}" rev-parse --short=10 HEAD 2>/dev/null || echo "unknown (${sdkrepo:-AROS source not set})")
sdkbr=$(git -C "${sdkrepo:-/nonexistent}" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)
{
  pubinfo "$info"
  echo
  echo "package: boxer $ver $target"
  [ $stale = 1 ] && echo "WARNING: NOT CORRESPONDING SOURCE - src/ changed after this binary was built; dry run only, do not distribute"
  if [ -n "$srcnote" ]; then
    echo "source changed after the build (accepted binary $got):"
    echo "$srcnote" | sed 's/^/  /'
    echo "  note: $BOXER_PKG_SOURCE_NOTE"
  fi
  echo "package date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "AROS link libraries from source: $sdkrepo at $sdkrev (branch $sdkbr)"
  echo "Boxer upstream: https://github.com/alinebee/Boxer.git $pin + patches/boxer/series"
  echo "conf: Preflight.conf Launch.conf, unmodified from the pin"
  echo "dosbox.msg: tools/strings2table.py from Resources/Base.lproj/DOSBox.strings"
  echo "  $(shasum -a 256 "$strings" | cut -d' ' -f1)  (Boxer, GPL-2.0, Alun Bestor and contributors)"
  echo "BoxerUI.info, ../Boxer.info: tools/mkicon.py, assets/runtime/boxer/boxer-128.png at 48 px (stack 65536 for BoxerUI)"
  echo "conf/GamesFolder.info: tools/mkicon.py drawer, assets/runtime/boxer/gamefolder-128.png at 48 px"
  echo "ReadMe.txt.info: tools/mkicon.py project, built-in page image, default tool SYS:Utilities/MultiView"
  echo "Install-Assign.info: tools/mkicon.py project, default tool C:IconX, image assets/runtime/boxer/boxer-32.png"
  echo "source archive: $(shasum -a 256 "$pkg/$srcarc" | cut -d' ' -f1)  $srcarc"
} > "$pkg/BUILDINFO.txt"
sed -e "s|@VERSION@|$ver|g" -e "s|@ABI_NAME@|$abiname|g" -e "s|@SOURCE_ARCHIVE@|$srcarc|g" \
  "$P/packaging/ReadMe.txt.in" > "$pkg/ReadMe.txt"
# Visible in Wanderer's default view, opens in MultiView (both ABIs ship it there).
python3 "$P/tools/mkicon.py" "$pkg/ReadMe.txt.info" project --image builtin:page \
  --default-tool SYS:Utilities/MultiView >/dev/null
(cd "$pkg" && find . -type f ! -name SHA256SUMS | sed 's|^\./||' | LC_ALL=C sort | while read -r f; do
  shasum -a 256 "$f"; done) > "$pkg/SHA256SUMS"

# 7. Never shipped: game data, Dune, third-party or unclear artwork.
bad=$(find "$pkg" \( -iname '*.png' -o -iname '*.wav' -o -iname '*.ttf' -o -iname '*.exe' \
  -o -iname '*.boxer' -o -ipath '*dune*' \) | head -5)
[ -z "$bad" ] || die "forbidden files in package: $bad"

# 8. Archive (arospkg guide: LHA, drawer at the top level).
# Homebrew's lha is Lhasa, which only reads; LHA_WRITER names a classic
# lha that can create (as aros-micropolis/scripts/package-release.sh).
# Without one, a ZIP is made, which arospkg also accepts.
if [ -n "${LHA_WRITER:-}" ] && "$LHA_WRITER" --help 2>&1 | grep -q 'a   Add'; then
  arc="boxer-$ver.$target.lha"
  (cd "$out" && "$LHA_WRITER" aq2o51 "$arc" Boxer Boxer.info)
  lha t "$out/$arc" >/dev/null 2>&1 || "$LHA_WRITER" t "$out/$arc" >/dev/null || die "archive test failed: $arc"
else
  echo "make-package: LHA_WRITER unset or not a writing lha; making a ZIP" >&2
  arc="boxer-$ver.$target.zip"
  (cd "$out" && zip -qrX "$arc" Boxer Boxer.info)
  unzip -tq "$out/$arc" >/dev/null || die "archive test failed: $arc"
fi
(cd "$out" && { { (cd Boxer && find . -type f | sed 's|^\./|Boxer/|'); echo Boxer.info; } | LC_ALL=C sort | xargs -I{} shasum -a 256 "{}"; \
  shasum -a 256 "$arc"; }) > "$out/MANIFEST-$BOXER_ABI.sha256"
"$here/check-package-notices.sh" "$BOXER_ABI" "$pkg"
echo "make-package: ok -> $out/$arc ($(wc -l < "$out/MANIFEST-$BOXER_ABI.sha256" | tr -d ' ') entries in MANIFEST-$BOXER_ABI.sha256)"
