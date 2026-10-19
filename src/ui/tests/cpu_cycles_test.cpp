// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer and DOSBox 0.74 (src/cpu/cpu.cpp, src/dos/dos_execute.cpp)
// as vendored in the Boxer tree, https://github.com/alunbestor/Boxer at
// commit 0062fc18; GPL-2.0. Modified by Tomasz Staniak, 2026.

// Host unit test for src/ui/cpu_cycles.{h,cpp}. DOSBox's own transitions
// are modelled here from its source, so that a speed chosen by the user
// can be followed through a protected-mode program and its end:
//   cyclesAuto     cpu.cpp Change_Config, type "auto" (3000, flag set);
//   cyclesMax      cpu.cpp Change_Config, type "max";
//   enterPmode     cpu.cpp CPU_SET_CRX, CR0_PROTECTION set;
//   programExit    dos_execute.cpp DOS_Terminate, back in real mode.
#include "../cpu_cycles.h"

#include <cstdio>

using namespace boxer_ui;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static CycleState cyclesAuto() {
    CycleState s;
    s.autoDetermine = kDetermineCycles | kDetermineCore;   // cycles=auto, core=auto
    s.cycleMax = s.oldCycleMax = 3000;
    s.autoAdjust = false;
    return s;
}
static CycleState cyclesFixed(int n) {
    CycleState s;
    s.cycleMax = n;
    s.autoAdjust = false;
    return s;
}
static CycleState cyclesMax() {
    CycleState s;
    s.cycleMax = 0;
    s.autoAdjust = true;
    return s;
}

static bool pmode;
static void enterPmode(CycleState &s) {
    pmode = true;
    if (!(s.autoDetermine & (kDetermineCore | kDetermineCycles))) return;
    if (s.autoDetermine & kDetermineCycles) {
        s.autoAdjust = true;
        s.oldCycleMax = s.cycleMax;
    }
    s.autoDetermine <<= kDetermineShift;
}
static void programExit(CycleState &s) {
    pmode = false;   // the program left protected mode before it ended
    if (!(s.autoDetermine >> kDetermineShift) || pmode) return;
    s.autoDetermine >>= kDetermineShift;
    if (s.autoDetermine & kDetermineCycles) {
        s.autoAdjust = false;
        s.cycleMax = s.oldCycleMax;
    }
}

// The configuration is told apart from the momentary state: cycles=auto
// looks like fixed 3000 right now, but is not.
static void testConfig() {
    CHECK(cycleConfigOf(cyclesAuto()) == CycleConfig::AutoDetermine);
    CHECK(cyclesAuto().cycleMax == 3000 && !cyclesAuto().autoAdjust);   // what the old reading saw
    CHECK(cycleConfigOf(cyclesFixed(3000)) == CycleConfig::Fixed);
    CHECK(cycleConfigOf(cyclesMax()) == CycleConfig::Max);
    // The core flag alone (core=auto, cycles=fixed) is not a cycles setting.
    CycleState c = cyclesFixed(3000);
    c.autoDetermine = kDetermineCore;
    CHECK(cycleConfigOf(c) == CycleConfig::Fixed);
}

// Without a choice DOSBox does its own thing: auto in protected mode,
// back to 3000 when the program ends.
static void testNoChoice() {
    CycleState s = cyclesAuto();
    enterPmode(s);
    CHECK(s.autoAdjust);
    programExit(s);
    CHECK(!s.autoAdjust && s.cycleMax == 3000);
    CHECK(s.autoDetermine == (kDetermineCycles | kDetermineCore));   // ready for the next program
}

int main() {
    testConfig();
    testNoChoice();
    if (failures) {
        std::printf("cpu_cycles_test: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    std::printf("cpu_cycles_test: all %d checks passed\n", checks);
    return 0;
}
