# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Source with bash: `. scripts/env.sh <abiv11|mainline-v1>`.
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
  *) _env_fail "ABI must be abiv11 or mainline-v1 (got '${BOXER_ABI}')"; return 1 ;;
esac
AROS_TOOLCHAIN="${!_tc:-}"; AROS_SDK="${!_sdk:-}"
[ -n "$AROS_TOOLCHAIN" ] || { _env_fail "$_tc is not set (see local.env.example)"; return 1; }
[ -n "$AROS_SDK" ] || { _env_fail "$_sdk is not set (see local.env.example)"; return 1; }
[ -d "$AROS_SDK/include" ] || { _env_fail "SDK include dir missing: $AROS_SDK/include (volume not mounted?)"; return 1; }
AROS_CXX="$AROS_TOOLCHAIN/x86_64-aros-g++"
[ -x "$AROS_CXX" ] || { _env_fail "compiler not executable: $AROS_CXX"; return 1; }

UPSTREAM_DIR="$PROJECT_ROOT/upstream/boxer"
WORK_DIR="$PROJECT_ROOT/work/boxer"
BUILD_DIR="$PROJECT_ROOT/build/$BOXER_ABI"
# Host audio/timer layer of the core: BOXER_HOST=sdl3 (static reduced SDL3
# from scripts/build-sdl3.sh, no GL) or sdl2 (the SDK's SDL2, the comparison
# build). Each host has its own core/UI/smoke output directories, so both
# builds exist side by side from one source tree (patch 0004 selects the
# code with -DBOXER_HOST_SDL3).
# Default sdl3 on ABIv11, because
# the SDL2 build hung or corrupted memory at flush, SDL2 pulls in gl.library.
# Mainline keeps sdl2 until its SDL3 build has been run, not just linked.
case "$BOXER_ABI" in
  abiv11) BOXER_HOST="${BOXER_HOST:-sdl3}" ;;
  *)      BOXER_HOST="${BOXER_HOST:-sdl2}" ;;
esac
case "$BOXER_HOST" in
  sdl2) HOST_SUFFIX=""
        HOST_CFLAGS=()
        HOST_LIBS=(-L"$AROS_SDK/lib" -lSDL2 -lGL) ;;
  sdl3) HOST_SUFFIX="-sdl3"
        _sdl3="$BUILD_DIR/sdl3"
        HOST_CFLAGS=(-DBOXER_HOST_SDL3 -I"$_sdl3/src/SDL3-3.4.12/include")
        HOST_LIBS=("$_sdl3/lib/libSDL3_ctl.a" -L"$AROS_SDK/lib")
        [ -d "$AROS_SDK/SDK/Extras/lib" ] && HOST_LIBS+=(-L"$AROS_SDK/SDK/Extras/lib")
        HOST_LIBS+=(-liconv)
        unset _sdl3 ;;
  *) _env_fail "BOXER_HOST must be sdl2 or sdl3 (got '$BOXER_HOST')"; return 1 ;;
esac
export PROJECT_ROOT BOXER_ABI AROS_TOOLCHAIN AROS_SDK AROS_CXX UPSTREAM_DIR WORK_DIR BUILD_DIR BOXER_HOST HOST_SUFFIX
unset _tc _sdk
