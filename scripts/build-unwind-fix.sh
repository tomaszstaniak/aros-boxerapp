#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
# Mainline v1 only. Mainline's GCC 10.5 AROS patch wraps
# __builtin_init_dwarf_reg_size_table in #ifdef MD_FALLBACK_FRAME_STATE_FOR,
# which AROS never defines, so dwarf_reg_size_table stays empty and every
# C++ throw aborts. Rebuild libgcc's unwind-dw2.o without that guard into
# build/mainline-v1/unwind-fix/, then check it. Link the object before
# libgcc: it then supplies every _Unwind_* symbol and libgcc's copy is
# never pulled. Needs the configured mainline GCC build tree (read only):
# AROS_V1_GCC_BUILD is the host build directory of a mainline AROS build,
# e.g. <aros-build>/bin/darwin-aarch64 or <aros-build>/bin/linux-x86_64.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
set +e; . "$here/env.sh" mainline-v1; rc=$?; set -e
[ $rc -eq 0 ] || exit 2
B=${AROS_V1_GCC_BUILD:?set AROS_V1_GCC_BUILD (see local.env.example)}
G=$B/gen/host/tools/crosstools/gnu/gcc
P=$B/Ports/host/gcc/gcc-10.5.0/libgcc
out=$BUILD_DIR/unwind-fix; mkdir -p "$out"
perl -0pe 's/#ifdef MD_FALLBACK_FRAME_STATE_FOR\n(  __builtin_init_dwarf_reg_size_table \(dwarf_reg_size_table\);\n)#endif\n/$1/' \
  "$P/unwind-dw2.c" > "$out/unwind-dw2.c"
! grep -B1 '__builtin_init_dwarf_reg_size_table (dwarf' "$out/unwind-dw2.c" | grep -q MD_FALLBACK \
  || { echo "guard not removed" >&2; exit 1; }
cd "$G/x86_64-aros/libgcc"
"$G/gcc/xgcc" -B"$G/gcc/" -B"$AROS_TOOLCHAIN/x86_64-aros/bin/" \
  -B"$AROS_TOOLCHAIN/x86_64-aros/lib/" \
  -isystem "$AROS_TOOLCHAIN/x86_64-aros/include" -isystem "$AROS_TOOLCHAIN/x86_64-aros/sys-include" \
  -mcmodel=large -O2 -DIN_GCC -DCROSS_DIRECTORY_STRUCTURE -isystem ./include -g \
  -DIN_LIBGCC2 -fbuilding-libgcc -fno-stack-protector -I. -I../.././gcc \
  -I"$P" -I"$P/../gcc" -I"$P/../include" -fexceptions -fvisibility=hidden -DHIDE_EXPORTS \
  -c "$out/unwind-dw2.c" -o "$out/unwind-dw2.o"
# Regression check: the stock object has no dwarf_reg_size_table at all
# (its init is a bare ret); the fixed one must define it.
"$AROS_TOOLCHAIN/x86_64-aros-nm" "$out/unwind-dw2.o" | grep -q ' dwarf_reg_size_table$' \
  || { echo "dwarf_reg_size_table missing: fix not effective" >&2; exit 1; }
echo "built $out/unwind-dw2.o"
