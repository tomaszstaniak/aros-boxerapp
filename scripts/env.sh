# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Source with bash: `. scripts/env.sh <abiv11|mainline-v1|aros-aarch64-abiv1-raspi>`.
# Resolves the project root and per-ABI toolchain/SDK paths.
# Precedence: existing environment, then ignored local.env, then nothing:
# workstation paths are never defaulted here, a missing value is an error.
# Relative values in local.env resolve against the directory containing it.

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

_env_fail() { echo "scripts/env.sh: $*" >&2; }

if [ -f "$PROJECT_ROOT/local.env" ]; then
  eval "$(python3 - "$PROJECT_ROOT/local.env" <<'PY'
import os, shlex, sys
from pathlib import Path
path = Path(sys.argv[1]); base = path.parent
for raw in path.read_text().splitlines():
    line = raw.strip()
    if not line or line.startswith("#") or "=" not in line:
        continue
    key, val = (s.strip() for s in line.split("=", 1))
    if len(val) >= 2 and val[0] == val[-1] and val[0] in "\"'":
        val = val[1:-1]
    if not key or key in os.environ:
        continue
    if val and not Path(val).is_absolute():
        val = str((base / val).resolve())
    print(f"export {key}={shlex.quote(val)}")
PY
)" || { _env_fail "cannot parse local.env"; return 1; }
fi

BOXER_ABI="${1:-${BOXER_ABI:-}}"
case "$BOXER_ABI" in
  abiv11)      _tc=AROS_V11_TOOLCHAIN; _sdk=AROS_V11_SDK ;;
  mainline-v1) _tc=AROS_V1_TOOLCHAIN;  _sdk=AROS_V1_SDK ;;
  aros-aarch64-abiv1-raspi) _tc=AROS_RASPI_TOOLCHAIN; _sdk=AROS_RASPI_SDK ;;
  *) _env_fail "ABI must be abiv11, mainline-v1 or aros-aarch64-abiv1-raspi (got '${BOXER_ABI}')"; return 1 ;;
esac
AROS_TOOLCHAIN="${!_tc:-}"; AROS_SDK="${!_sdk:-}"
[ -n "$AROS_TOOLCHAIN" ] || { _env_fail "$_tc is not set (see local.env.example)"; return 1; }
[ -n "$AROS_SDK" ] || { _env_fail "$_sdk is not set (see local.env.example)"; return 1; }
[ -d "$AROS_SDK/include" ] || { _env_fail "SDK include dir missing: $AROS_SDK/include (volume not mounted?)"; return 1; }
# Tool names per target: both cross toolchains use <cpu>-aros-<tool>.
case "$BOXER_ABI" in
  aros-aarch64-abiv1-raspi) AROS_TOOL_PREFIX=aarch64-aros- ;;
  *) AROS_TOOL_PREFIX=x86_64-aros- ;;
esac
AROS_CXX="$AROS_TOOLCHAIN/${AROS_TOOL_PREFIX}g++"
AROS_CC="$AROS_TOOLCHAIN/${AROS_TOOL_PREFIX}gcc"
AROS_AR="$AROS_TOOLCHAIN/${AROS_TOOL_PREFIX}ar"
AROS_NM="$AROS_TOOLCHAIN/${AROS_TOOL_PREFIX}nm"
AROS_SYSROOT="$AROS_SDK"
[ -x "$AROS_CXX" ] || { _env_fail "compiler not executable: $AROS_CXX"; return 1; }
# The mainline driver's built-in sysroot is the tree the toolchain was built
# in, and it is searched first, so -I and -L alone would still mix that tree's
# headers and libraries in: every mainline compile and link names the SDK as
# the sysroot (scripts/aros-v1-cxx).
[ "$BOXER_ABI" = mainline-v1 ] && AROS_CXX="$PROJECT_ROOT/scripts/aros-v1-cxx"
# The Raspberry Pi toolchain is Clang with no C++ standard library of its own:
# scripts/aros-raspi-cc adds the SDK and the libc++ of
# scripts/build-cxx-runtime.sh to every compile and link.
if [ "$BOXER_ABI" = aros-aarch64-abiv1-raspi ]; then
  AROS_CXX="$PROJECT_ROOT/scripts/aros-raspi-cxx"
  AROS_CC="$PROJECT_ROOT/scripts/aros-raspi-cc"
  AROS_RASPI_CXXRT="${AROS_RASPI_CXXRT:-$PROJECT_ROOT/deps/$BOXER_ABI/cxxrt}"
  export AROS_RASPI_CXXRT
fi

UPSTREAM_DIR="$PROJECT_ROOT/upstream/boxer"
WORK_DIR="$PROJECT_ROOT/work/boxer"
BUILD_DIR="$PROJECT_ROOT/build/$BOXER_ABI"
# Host audio/timer layer of the core: BOXER_HOST=sdl3 (static reduced SDL3
# from scripts/build-sdl3.sh, no GL) or sdl2 (the SDK's SDL2, the comparison
# build). Each host has its own core/UI/smoke output directories, so both
# builds exist side by side from one source tree (patch 0004 selects the
# code with -DBOXER_HOST_SDL3).
# Default sdl3: the SDL2 build hung or corrupted memory at flush on ABIv11, and
# SDL2 pulls in gl.library. Mainline allows nothing else, so a wrong setting
# stops here and never falls back to the GL path.
BOXER_HOST="${BOXER_HOST:-sdl3}"
if [ "$BOXER_ABI" != abiv11 ] && [ "$BOXER_HOST" != sdl3 ]; then
  _env_fail "$BOXER_ABI builds use the reduced SDL3 only (BOXER_HOST=$BOXER_HOST)"; return 1
fi
case "$BOXER_HOST" in
  sdl2) HOST_SUFFIX=""
        HOST_CFLAGS=()
        HOST_LIBS=(-L"$AROS_SDK/lib" -lSDL2 -lGL) ;;
  sdl3) HOST_SUFFIX="-sdl3"
        _sdl3="$BUILD_DIR/sdl3"
        HOST_CFLAGS=(-DBOXER_HOST_SDL3 -I"$_sdl3/src/SDL3-3.4.12/include")
        HOST_LIBS=("$_sdl3/lib/libSDL3_ctl.a" -L"$AROS_SDK/lib")
        [ -d "$AROS_SDK/SDK/Extras/lib" ] && HOST_LIBS+=(-L"$AROS_SDK/SDK/Extras/lib")
        # The Raspberry Pi SDK has no iconv; its SDL3 uses SDL's own converter
        # (third_party/sdl3-aros/build-sdl3-ctl.py).
        [ "$BOXER_ABI" = aros-aarch64-abiv1-raspi ] || HOST_LIBS+=(-liconv)
        unset _sdl3 ;;
  *) _env_fail "BOXER_HOST must be sdl2 or sdl3 (got '$BOXER_HOST')"; return 1 ;;
esac
export PROJECT_ROOT BOXER_ABI AROS_TOOLCHAIN AROS_SDK AROS_CXX AROS_CC AROS_AR AROS_NM AROS_SYSROOT UPSTREAM_DIR WORK_DIR BUILD_DIR BOXER_HOST HOST_SUFFIX
unset _tc _sdk
