// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXEmulator.mm),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "cpu_cycles.h"

namespace boxer_ui {

CycleConfig cycleConfigOf(const CycleState &s) {
    if (s.autoDetermine & kDetermineCycles) return CycleConfig::AutoDetermine;
    return s.autoAdjust ? CycleConfig::Max : CycleConfig::Fixed;
}

}  // namespace boxer_ui
