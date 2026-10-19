// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXEmulator.mm setFixedSpeed:/setAutoSpeed:),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "cpu_cycles.h"

namespace boxer_ui {

static constexpr unsigned kCyclesBothPositions = kDetermineCycles | (kDetermineCycles << kDetermineShift);

CycleConfig cycleConfigOf(const CycleState &s) {
    if (s.autoDetermine & kDetermineCycles) return CycleConfig::AutoDetermine;
    return s.autoAdjust ? CycleConfig::Max : CycleConfig::Fixed;
}

void setFixedCycles(CycleState &s, int cycles) {
    s.autoAdjust = false;
    s.oldCycleMax = s.cycleMax = cycles;
    s.autoDetermine &= ~kCyclesBothPositions;
}

void setAutomaticCycles(CycleState &s) {
    if (!s.autoAdjust) {
        // Records the fixed value and forces 100% usage, as the original.
        s.oldCycleMax = s.cycleMax;
        s.percUsed = 100;
        s.autoAdjust = true;
    }
    s.autoDetermine &= ~kCyclesBothPositions;
}

}  // namespace boxer_ui
