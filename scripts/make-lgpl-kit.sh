#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Make the LGPL kit of an x86_64 BoxerUI build: the GNU libiconv source as
# linked (LGPL-2.1 section 6) and a relink kit, so a user can change
# libiconv and link BoxerUI again with it.
#
#   scripts/make-lgpl-kit.sh <abiv11|mainline-v1>
#
# Reads the build of scripts/build-ui.sh (build/<abi>/ui-sdl3, or
# BOXER_UI_OUT): BoxerUI, BUILDINFO.txt and link-inputs.txt. Writes
# build/<abi>/lgpl-kit/ (BOXER_KIT_OUT overrides):
#   Boxer-<version>-<target>-lgpl-kit/      the kit
#   Boxer-<version>-<target>-lgpl-kit.zip   the same, packed (the release asset)
# The GNU tarballs are read from LIBICONV_TARBALLS (a directory; default
# deps/src) and checked against third_party/libiconv/PINS.
# BOXER_KIT_VERSION: the version in the kit's name (default: BUILDINFO's
# "version:", else packaging/VERSION). For a kit made later for an earlier
# release: BOXER_KIT_NAME (the asset's name without .zip) and
# BOXER_KIT_SOURCE_COMMIT (the commit that build was made from).
#
# The link is repeated with -Wl,-t from link-inputs.txt; it must give the
# same BoxerUI, and every file it opens is taken into the kit, except the
# compiler's own libstdc++.a and libgcc.a (named with their sha256). The kit's
# link.sh is then run on a copy of the kit, which must give the same BoxerUI
# again, opening nothing outside the kit but those two archives.
set -euo pipefail
die() { echo "make-lgpl-kit: $*" >&2; exit 2; }
[ $# -eq 1 ] || die "usage: make-lgpl-kit.sh <abiv11|mainline-v1>"
case "$1" in abiv11|mainline-v1) ;; *) die "only the x86_64 targets link libiconv (got $1)";; esac
here=$(cd "$(dirname "$0")" && pwd)
set +e; . "$here/env.sh" "$1"; rc=$?; set -e
[ $rc -eq 0 ] || die "environment setup failed"
[ -z "${AROS_TOOLCHAIN_MOUNT:-}" ] || [ -d "$AROS_TOOLCHAIN_MOUNT" ] \
  || die "AROS_TOOLCHAIN_MOUNT=$AROS_TOOLCHAIN_MOUNT is not available (volume not mounted?)"
P=$PROJECT_ROOT
ui="${BOXER_UI_OUT:-$BUILD_DIR/ui$HOST_SUFFIX}"
bin=$ui/BoxerUI; info=$ui/BUILDINFO.txt; inputs=$ui/link-inputs.txt
[ -f "$bin" ] && [ -f "$info" ] && [ -f "$inputs" ] || die "no build with link-inputs.txt in $ui (run scripts/build-ui.sh $1)"
want=$(shasum -a 256 "$bin" | cut -d' ' -f1)
[ "$(sed -n 's/^sha256: //p' "$info")" = "$want" ] || die "BoxerUI does not match $info"
ver=${BOXER_KIT_VERSION:-$(sed -n 's/^version: //p' "$info")}
[ -n "$ver" ] || ver=$(tr -d ' \n' < "$P/packaging/VERSION")
case "$BOXER_ABI" in abiv11) target=aros-x86_64-abiv11-pc;; mainline-v1) target=aros-x86_64-abiv1-pc;; esac
name=${BOXER_KIT_NAME:-Boxer-$ver-$target-lgpl-kit}
out="${BOXER_KIT_OUT:-$BUILD_DIR/lgpl-kit}"
pins="$P/third_party/libiconv/PINS"
tarballs="${LIBICONV_TARBALLS:-$P/deps/src}"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/lgpl-kit.XXXXXX"); tmp=$(cd "$tmp" && pwd)
trap 'chmod -R u+w "$tmp" 2>/dev/null; rm -rf "$tmp"' EXIT
gxx="$AROS_TOOLCHAIN/x86_64-aros-g++"
# The compiler driver the build used: on mainline it names the SDK as sysroot.
sysopt=(); [ "$BOXER_ABI" = mainline-v1 ] && sysopt=(--sysroot="$AROS_SDK")

# 1. Repeat the link and list what it opens.
link_in=(); while IFS= read -r l; do link_in+=("$l"); done < "$inputs"
"$gxx" "${sysopt[@]+"${sysopt[@]}"}" -Wl,-t -o "$tmp/BoxerUI" "${link_in[@]}" > "$tmp/trace.txt" 2>&1 \
  || { cat "$tmp/trace.txt" >&2; die "the link could not be repeated"; }
[ "$(shasum -a 256 "$tmp/BoxerUI" | cut -d' ' -f1)" = "$want" ] \
  || die "the repeated link does not give the BoxerUI of $info"
opened=$(grep -E '^/' "$tmp/trace.txt" | sed -E 's/\(.*\)$//' | awk '!seen[$0]++')

kit="$out/$name"; [ -d "$out" ] && chmod -R u+w "$out"; rm -rf "$out"
mkdir -p "$kit/relink/obj" "$kit/relink/lib" "$kit/relink/sysroot/lib" "$kit/libiconv" "$kit/LICENSES"
: > "$kit/relink/objects.txt"; toolchain_libs=""; iconv_sha=""
for f in $opened; do
  [ -f "$f" ] || continue
  b=$(basename "$f")
  # Named on the link line (link-inputs.txt): the build's own files.
  own=0; for l in "${link_in[@]}"; do [ "$l" = "$f" ] && own=1; done
  if [ $own = 1 ]; then
    case "$b" in
      *.o) [ ! -e "$kit/relink/obj/$b" ] || die "two objects named $b"
           cp "$f" "$kit/relink/obj/$b"; echo "obj/$b" >> "$kit/relink/objects.txt" ;;
      libboxer-dosbox.a|libSDL3_ctl.a) cp "$f" "$kit/relink/lib/$b" ;;
      *) die "unexpected file on the link line: $f" ;;
    esac
    continue
  fi
  case "$f" in
    */libiconv.a)
      cp "$f" "$kit/relink/lib/$b"; iconv_sha=$(shasum -a 256 "$f" | cut -d' ' -f1) ;;
    "$AROS_TOOLCHAIN"/*/libstdc++.a|"$AROS_TOOLCHAIN"/*/libgcc.a)
      toolchain_libs="$toolchain_libs$(shasum -a 256 "$f" | cut -d' ' -f1)  ${f#$AROS_TOOLCHAIN/}
" ;;
    *.a|*.o)
      # AROS start-up code and link libraries (AROS Public License).
      if [ -e "$kit/relink/sysroot/lib/$b" ]; then
        cmp -s "$f" "$kit/relink/sysroot/lib/$b" || die "two different files named $b: $f"
      else cp "$f" "$kit/relink/sysroot/lib/$b"; fi ;;
  esac
done
# The objects in link order (the trace lists them in that order).
[ -s "$kit/relink/objects.txt" ] || die "no object files found in the trace"
[ -f "$kit/relink/lib/libboxer-dosbox.a" ] && [ -f "$kit/relink/lib/libSDL3_ctl.a" ] || die "core or SDL3 library missing from the trace"
[ -n "$iconv_sha" ] || die "libiconv.a not in the link"
[ -n "$toolchain_libs" ] || die "the compiler's libstdc++.a and libgcc.a not found in the trace"
chmod 644 "$kit"/relink/obj/* "$kit"/relink/lib/* "$kit"/relink/sysroot/lib/*

# 2. The libiconv source as linked.
line=$(awk -v s="$iconv_sha" -v a="$BOXER_ABI" '$1=="archive" && $2==s && $3==a {print $4}' "$pins")
[ -n "$line" ] || die "libiconv.a ($iconv_sha) is not the library pinned for $BOXER_ABI in $pins"
iv=$line; tball="libiconv-$iv.tar.gz"
tsha=$(awk -v n="$tball" '$1=="tarball" && $3==n {print $2}' "$pins")
[ -f "$tarballs/$tball" ] || die "$tball not in $tarballs (get https://ftp.gnu.org/gnu/libiconv/$tball)"
[ "$(shasum -a 256 "$tarballs/$tball" | cut -d' ' -f1)" = "$tsha" ] || die "$tarballs/$tball is not the pinned tarball"
cp "$tarballs/$tball" "$kit/libiconv/"
cp "$P"/third_party/libiconv/"$BOXER_ABI"/* "$kit/libiconv/"
cp "$P/third_party/libiconv/README.md" "$kit/libiconv/PROVENANCE.md"
cp "$P/LICENSES/LGPL-2.1.txt" "$kit/LICENSES/"
cp "$P/COPYING" "$kit/LICENSES/GPL-2.0.txt"
cp "$P/LICENSES/AROS-APL-1.1.txt" "$kit/LICENSES/"

# 3. The link script and the notes.
gccv=$("$gxx" -dumpversion)
case "$BOXER_ABI" in
  abiv11) tcname="an AROS ABIv11 cross toolchain (deadwood2/AROS): x86_64-aros-g++ $gccv"; sysname="the AROS ABIv11 build the toolchain belongs to" ;;
  mainline-v1) tcname="a mainline AROS cross toolchain (aros-development-team/AROS): x86_64-aros-g++ $gccv"; sysname="the mainline SDK named in BUILDINFO.txt" ;;
esac
cat > "$kit/relink/link.sh" <<'EOF'
#!/bin/sh
# Link BoxerUI again from the files of this kit.
#   AROS_TOOLCHAIN=<directory with x86_64-aros-g++> ./link.sh [<libiconv.a>]
# With no argument the libiconv.a of this kit is used. Output: ./BoxerUI.
set -e
kit=$(cd "$(dirname "$0")" && pwd)
: "${AROS_TOOLCHAIN:?set AROS_TOOLCHAIN to the directory holding x86_64-aros-g++}"
iconv=${1:-$kit/lib/libiconv.a}
[ -f "$iconv" ] || { echo "no such file: $iconv" >&2; exit 2; }
ilib=$(mktemp -d "${TMPDIR:-/tmp}/boxer-relink.XXXXXX")
trap 'rm -rf "$ilib"' EXIT
cp "$iconv" "$ilib/libiconv.a"
objs=$(sed "s|^|$kit/|" "$kit/objects.txt")
# shellcheck disable=SC2086
"$AROS_TOOLCHAIN/x86_64-aros-g++" --sysroot="$kit/sysroot" ${BOXER_RELINK_FLAGS:-} -o "$kit/BoxerUI" $objs \
  "$kit/lib/libboxer-dosbox.a" "$kit/lib/libSDL3_ctl.a" -L"$ilib" -L"$kit/sysroot/lib" -liconv
echo "linked $kit/BoxerUI"
EOF
chmod 755 "$kit/relink/link.sh"
printf '%s  BoxerUI\n' "$want" > "$kit/relink/BoxerUI.sha256"
printf '%s' "$toolchain_libs" > "$kit/relink/TOOLCHAIN-LIBS.sha256"
srccommit=${BOXER_KIT_SOURCE_COMMIT:-$(sed -n 's/^source commit: //p' "$info" | head -1)}
[ -n "$srccommit" ] || srccommit="see BUILDINFO.txt of the package"
title="GNU libiconv source and relink kit for Boxer for AROS $ver ($target)"
rule=$(printf '%s' "$title" | sed 's/./=/g')
aros_diff=""; [ -f "$kit/libiconv/libiconv-$iv-aros.diff" ] \
  && aros_diff="  libiconv-$iv-aros.diff   the AROS patch the SDK's build applies"
cat > "$kit/README.txt" <<EOF
$title
$rule

The BoxerUI program of the package Boxer-$ver-$target links GNU libiconv
$iv statically (SDL3's character-set conversion uses it). GNU libiconv is
free software under the GNU Lesser General Public License, version 2.1 or
later (LICENSES/LGPL-2.1.txt). This kit lets you change libiconv and link
BoxerUI again with your version (LGPL-2.1 section 6). BoxerUI itself is
under the GNU GPL version 2 (LICENSES/GPL-2.0.txt); its complete source is
in the package (boxer-*-source.tar.gz).

libiconv/
  $tball   the GNU release, unmodified (sha256 $tsha)
$aros_diff
  mmakefile.src           the AROS build recipe of the SDK's libiconv
  BUILD.txt               how the SDK's libiconv.a was built; following it
                          gives the same object files, byte for byte
  PROVENANCE.md           where the library comes from, per target

relink/
  obj/                    BoxerUI's own object files, in link order (objects.txt)
  lib/libboxer-dosbox.a   the DOSBox core of this build
  lib/libSDL3_ctl.a       the reduced SDL3 of this build
  lib/libiconv.a          the SDK's GNU libiconv, as linked
                          (sha256 $iconv_sha)
  sysroot/lib/            the AROS start-up code and link libraries the link
                          opened, from $sysname: AROS Public License
                          (LICENSES/AROS-APL-1.1.txt); libpthread.a, where
                          present, is under the zlib licence (notice in its
                          AROS source, compiler/pthread)
  link.sh                 the link command
  BoxerUI.sha256          the sha256 of the BoxerUI in the package
  TOOLCHAIN-LIBS.sha256   the compiler's libstdc++.a and libgcc.a used; they
                          come with the compiler and are not in this kit

To link BoxerUI with a changed libiconv
  You need $tcname.
  1. Unpack $tball, make your change and build libiconv.a as BUILD.txt
     describes.
  2. AROS_TOOLCHAIN=<directory with x86_64-aros-g++> relink/link.sh <your libiconv.a>
  3. Copy relink/BoxerUI over BoxerUI in the Boxer drawer.
  Without an argument link.sh uses the kit's own libiconv.a. With the same
  compiler (the same libstdc++.a and libgcc.a as TOOLCHAIN-LIBS.sha256) that
  gives a BoxerUI identical to the packaged one (BoxerUI.sha256).
  Some AROS toolchains have the directory they were built in compiled into
  their linker driver; the link then needs that directory to exist.

Package source commit: $srccommit
EOF
(cd "$kit" && find . -type f ! -name SHA256SUMS | sed 's|^\./||' | LC_ALL=C sort | while read -r f; do
  shasum -a 256 "$f"; done) > "$kit/SHA256SUMS"

# 4. The kit links on its own: a copy, linked with link.sh, gives the same
# BoxerUI and opens nothing outside the kit but the compiler's libraries.
cp -R "$kit" "$tmp/test"
TMPDIR="$tmp" AROS_TOOLCHAIN="$AROS_TOOLCHAIN" BOXER_RELINK_FLAGS=-Wl,-t "$tmp/test/relink/link.sh" > "$tmp/test.txt" 2>&1 \
  || { cat "$tmp/test.txt" >&2; die "the kit's link.sh failed"; }
got=$(shasum -a 256 "$tmp/test/relink/BoxerUI" | cut -d' ' -f1)
[ "$got" = "$want" ] || die "the kit's link.sh gives $got, not the packaged BoxerUI $want"
outside_kit() {
  local f
  while read -r f; do
    case "$f" in
      ("$tmp"/*) ;;
      (*/libstdc++.a|*/libgcc.a) ;;
      (*) echo "$f" ;;
    esac
  done
}
outside=$(grep -E '^/' "$tmp/test.txt" | sed -E 's/\(.*\)$//' | sort -u | outside_kit)
[ -z "$outside" ] || { echo "$outside" >&2; die "the kit's link opened files outside the kit"; }
# No path of this machine in the kit's text files.
leak=$(grep -r -l -I -F -e "$P" -e "$HOME" -e "$AROS_TOOLCHAIN" -e "$AROS_SDK" "$kit" || true)
[ -z "$leak" ] || die "host paths in: $leak"

(cd "$out" && rm -f "$name.zip" && zip -qrX "$name.zip" "$name")
unzip -tq "$out/$name.zip" >/dev/null || die "zip test failed"
echo "make-lgpl-kit: ok -> $out/$name.zip ($(shasum -a 256 "$out/$name.zip" | cut -d' ' -f1)); relink test: BoxerUI $got"
