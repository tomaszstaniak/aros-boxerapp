#!/bin/sh
# Reproducible build of BOXTEST.COM with nasm only.
# Output: <project>/build/boxtest/BOXTEST.COM (generated, not versioned).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
NASM=${NASM:-nasm}
out=${BOXTEST_OUT:-$root/build/boxtest}

if ! command -v "$NASM" >/dev/null 2>&1; then
    echo "build.sh: nasm not found (set NASM=/path/to/nasm or install nasm)" >&2
    exit 1
fi
mkdir -p "$out"
"$NASM" -v
"$NASM" -f bin -o "$out/BOXTEST.COM" "$here/boxtest.asm"
size=$(wc -c < "$out/BOXTEST.COM" | tr -d ' ')
echo "BOXTEST.COM $size bytes"
if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$out/BOXTEST.COM"
else
    sha256sum "$out/BOXTEST.COM"
fi
