// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXEmulator.mm setFixedSpeed:/setAutoSpeed:),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// DOSBox's CPU speed state, as the speed setters change it, without the
// core's headers, so it is unit-tested on the host
// (src/ui/tests/cpu_cycles_test.cpp); platform/aros/corecontrol copies the
// core's variables in and out.
//
// The configuration and the momentary state differ. "cycles=auto" starts
// at fixed 3000 cycles but sets CPU_AUTODETERMINE_CYCLES: entering
// protected mode switches to automatic and shifts the flags left
// (cpu.cpp CPU_SET_CRX), and a program that ends back in real mode
// shifts them right and restores the fixed cycles (dos_execute.cpp
// DOS_Terminate). So "3000, fixed" right now may be "auto-determine", and
// a choice made by the user must clear both flag positions, or the next
// program's end undoes it.
#ifndef BOXER_UI_CPU_CYCLES_H
#define BOXER_UI_CPU_CYCLES_H

namespace boxer_ui {

// DOSBox include/cpu.h values (checked against them in corecontrol.cpp).
constexpr unsigned kDetermineCore = 0x01, kDetermineCycles = 0x02, kDetermineShift = 2;

struct CycleState {
    bool autoAdjust = false;     // CPU_CycleAutoAdjust: automatic speed now
    int cycleMax = 3000;         // CPU_CycleMax
    int oldCycleMax = 3000;      // CPU_OldCycleMax: what a program's end restores
    int percUsed = 100;          // CPU_CyclePercUsed
    unsigned autoDetermine = 0;  // CPU_AutoDetermineMode
};

// What the configuration asked for. Read at the first shell, before any
// program ran (the flags are not shifted then).
enum class CycleConfig {
    Fixed,          // cycles=fixed N (or a number)
    Max,            // cycles=max: automatic all the time
    AutoDetermine,  // cycles=auto: fixed in real mode, automatic in protected mode
};
CycleConfig cycleConfigOf(const CycleState &s);

// BXEmulator setFixedSpeed: and setAutoSpeed:YES, plus what the original
// did not do: both clear the cycles auto-determine flag in its current and
// its shifted position, so the choice stays through protected mode and
// the end of a program. The core flag is left alone.
void setFixedCycles(CycleState &s, int cycles);
void setAutomaticCycles(CycleState &s);

}  // namespace boxer_ui
#endif
