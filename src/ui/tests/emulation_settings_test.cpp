// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXSession.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Host unit test for src/ui/emulation_settings.{h,cpp}: which CPU speed and
// frameskip a session runs with, and what the game's settings file keeps,
// across sessions, with the file on disk (round trip, rename).
// Usage: emulation_settings_test <scratch dir>
#include "../emulation_settings.h"
#include "../ui_logic.h"

#include "../../model/datalocations.h"
#include "../../model/fsutil.h"
#include "../../model/gamebox.h"
#include "../../model/gameboxrename.h"
#include "../../model/importsource.h"
#include "../../model/plist.h"
#include "../../model/programs.h"

#include <cstdio>
#include <string>

using namespace boxer_ui;
namespace fu = boxer::fsutil;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const CPUSpeed kAuto = CPUSpeed::autoSpeed();
static CPUSpeed fixedSpeed(int c) { return CPUSpeed::fixed(c); }

// One session on a settings dictionary, as main.cpp runs it: read the
// saved choice, start with the core's base, let the user act, store.
struct Session {
    EmulationSettings s;
    Session(const boxer::PlistValue &dict, CPUSpeed base, int baseSkip, const EmulationChoice &ovr = {}) {
        s.begin(readEmulationChoice(dict), ovr, base, baseSkip);
    }
};

static bool hasKey(const boxer::PlistValue &d, const char *k) { return d.get(k) != nullptr; }
static long long num(const boxer::PlistValue &d, const char *k) { return d.get(k) ? d.get(k)->integer() : -999; }

// The layers: override > saved > gamebox configuration > defaults (base).
static void testLayers() {
    boxer::PlistValue d = boxer::PlistValue::dict();
    // No saved choice: the base (the gamebox's cycles, else the default).
    Session a(d, fixedSpeed(10000), 2);
    CHECK(a.s.speed() == fixedSpeed(10000) && a.s.frameskip() == 2);
    Session a2(d, kAuto, 0);
    CHECK(a2.s.speed() == kAuto && a2.s.frameskip() == 0);
    // A saved choice beats the gamebox's configuration.
    d.set(kCPUSpeedKey, boxer::PlistValue::integer(20000));
    d.set(kFrameskipKey, boxer::PlistValue::integer(3));
    Session b(d, fixedSpeed(10000), 1);
    CHECK(b.s.speed() == fixedSpeed(20000) && b.s.frameskip() == 3);
    // A one-run override beats both, one setting at a time.
    Session c(d, fixedSpeed(10000), 1, parseEmulationOverride("auto", ""));
    CHECK(c.s.speed() == kAuto && c.s.frameskip() == 3);
    Session c2(d, fixedSpeed(10000), 1, parseEmulationOverride("", "0"));
    CHECK(c2.s.speed() == fixedSpeed(20000) && c2.s.frameskip() == 0);
    Session c3(d, fixedSpeed(10000), 1, parseEmulationOverride("4500", "5"));
    CHECK(c3.s.speed() == fixedSpeed(4500) && c3.s.frameskip() == 5);
    // Values that are not choices: ignored, the next layer applies.
    CHECK(!parseEmulationOverride("fast", "x").speed && !parseEmulationOverride("-3", "-1").frameskip);
    CHECK(!parseEmulationOverride("0", "").speed);
    CHECK(parseEmulationOverride("AUTO", "").speed == kAuto);
    boxer::PlistValue junk = boxer::PlistValue::dict();
    junk.set(kCPUSpeedKey, boxer::PlistValue::string("fast"));
    junk.set(kFrameskipKey, boxer::PlistValue::integer(-4));
    const EmulationChoice j = readEmulationChoice(junk);
    CHECK(!j.speed && !j.frameskip);
}

// Automatic chosen over a gamebox that sets fixed cycles: stored as
// Automatic, and restored as Automatic, not as the gamebox's cycles.
static void testAutomaticOverFixed() {
    boxer::PlistValue d = boxer::PlistValue::dict();
    Session s1(d, fixedSpeed(10000), 0);
    CHECK(s1.s.speed() == fixedSpeed(10000));
    s1.s.userChoseSpeed(kAuto);
    s1.s.store(d);
    CHECK(hasKey(d, kCPUSpeedKey) && num(d, kCPUSpeedKey) == kBXAutoSpeed);
    Session s2(d, fixedSpeed(10000), 0);
    CHECK(s2.s.speed() == kAuto);
    // On a gamebox whose base is Automatic anyway the key is not needed.
    boxer::PlistValue e = boxer::PlistValue::dict();
    Session s3(e, kAuto, 0);
    s3.s.userChoseSpeed(kAuto);
    s3.s.store(e);
    CHECK(!hasKey(e, kCPUSpeedKey));
    // And a fixed choice equal to the gamebox's cycles is not stored either.
    boxer::PlistValue f = boxer::PlistValue::dict();
    Session s4(f, fixedSpeed(10000), 0);
    s4.s.userChoseSpeed(fixedSpeed(10000));
    s4.s.store(f);
    CHECK(!hasKey(f, kCPUSpeedKey));
}

// Frameskip 0 chosen over a gamebox that sets frameskip 2: stored as 0.
static void testFrameskipZeroOverConfig() {
    boxer::PlistValue d = boxer::PlistValue::dict();
    Session s1(d, kAuto, 2);
    CHECK(s1.s.frameskip() == 2);
    s1.s.userChoseFrameskip(0);
    s1.s.store(d);
    CHECK(hasKey(d, kFrameskipKey) && num(d, kFrameskipKey) == 0);
    Session s2(d, kAuto, 2);
    CHECK(s2.s.frameskip() == 0);
    // Without such a configuration, 0 is what the defaults give: not stored.
    boxer::PlistValue e = boxer::PlistValue::dict();
    Session s3(e, kAuto, 0);
    s3.s.userChoseFrameskip(0);
    s3.s.store(e);
    CHECK(!hasKey(e, kFrameskipKey));
}

// No saved choice and no change: nothing is written; a dictionary holding
// other keys keeps them. Nothing is written before the session began.
static void testNoChoice() {
    boxer::PlistValue d = boxer::PlistValue::dict();
    d.set("BXGameRecentPrograms", boxer::PlistValue::array());
    Session s(d, fixedSpeed(8000), 1);
    s.s.store(d);
    CHECK(!hasKey(d, kCPUSpeedKey) && !hasKey(d, kFrameskipKey) && hasKey(d, "BXGameRecentPrograms"));
    EmulationSettings never;
    boxer::PlistValue e = boxer::PlistValue::dict();
    e.set(kCPUSpeedKey, boxer::PlistValue::integer(kBXAutoSpeed));
    never.store(e);
    CHECK(num(e, kCPUSpeedKey) == kBXAutoSpeed);
    CHECK(!never.begun());
}

// A one-run override is used, never saved, and does not remove a saved
// choice; a choice made in the Inspector during that run is saved.
static void testOverrideNotPersisted() {
    boxer::PlistValue d = boxer::PlistValue::dict();
    d.set(kCPUSpeedKey, boxer::PlistValue::integer(20000));
    Session s(d, fixedSpeed(10000), 0, parseEmulationOverride("3000", "4"));
    CHECK(s.s.speed() == fixedSpeed(3000) && s.s.frameskip() == 4);
    s.s.store(d);
    CHECK(num(d, kCPUSpeedKey) == 20000 && !hasKey(d, kFrameskipKey));
    Session t(d, fixedSpeed(10000), 0, parseEmulationOverride("auto", ""));
    t.s.userChoseFrameskip(1);
    t.s.store(d);
    CHECK(num(d, kCPUSpeedKey) == 20000 && num(d, kFrameskipKey) == 1);
}

// The core's own cycle count in automatic mode, fast forward and pause are
// not choices: the stored value is the Automatic marker, never a count.
static void testAutoCountNeverStored() {
    boxer::PlistValue d = boxer::PlistValue::dict();
    Session s(d, fixedSpeed(10000), 0);
    s.s.userChoseSpeed(kAuto);
    // The core then reports, say, 31337 cycles in auto mode; the Inspector
    // only follows the core (it calls no userChose*), so nothing changes.
    s.s.store(d);
    CHECK(num(d, kCPUSpeedKey) == kBXAutoSpeed);
    CHECK((CPUSpeed{true, 31337} == kAuto));   // the count is not part of the value
}

// A second program in the same session: nothing resets the choice.
static void testSecondProgram() {
    boxer::PlistValue d = boxer::PlistValue::dict();
    Session s(d, fixedSpeed(10000), 0);
    s.s.userChoseSpeed(fixedSpeed(15000));
    s.s.userChoseFrameskip(2);
    // Program A ends, program B starts: the session's settings are used
    // as they are (main.cpp applies them once per session, at the first
    // shell, and not again for later programs).
    CHECK(s.s.speed() == fixedSpeed(15000) && s.s.frameskip() == 2);
    s.s.store(d);
    CHECK(num(d, kCPUSpeedKey) == 15000 && num(d, kFrameskipKey) == 2);
}

// The settings file on disk: saved, read back by a new session, and kept
// by the gamebox after a rename (same identifier, same file).
static void testFileRoundTripAndRename(const std::string &scratch) {
    const std::string games = fu::join(scratch, "Games");
    fu::makeDirs(games);
    const std::string src = fu::join(scratch, "Commander Keen");
    fu::makeDirs(src);
    boxer::ImportSession imp;
    imp.sourcePath = src;
    std::string err;
    CHECK(imp.createGamebox(games, &err));
    boxer::Gamebox box;
    CHECK(box.open(imp.createdGamebox, &err));
    const std::string id = box.identifier();
    CHECK(!id.empty());
    boxer::DataLocations where;
    where.dataDir = fu::join(scratch, "Boxer Data");
    fu::makeDirs(where.dataDir);

    boxer::GameSettings gs;
    CHECK(gs.load(boxer::GameSettings::pathFor(where, id), &err));
    Session s1(gs.dict(), fixedSpeed(10000), 0);
    s1.s.userChoseSpeed(fixedSpeed(25000));
    s1.s.userChoseFrameskip(3);
    s1.s.store(gs.dict());
    CHECK(gs.save(&err));

    boxer::GameSettings again;
    CHECK(again.load(boxer::GameSettings::pathFor(where, id), &err));
    Session s2(again.dict(), fixedSpeed(10000), 0);
    CHECK(s2.s.speed() == fixedSpeed(25000) && s2.s.frameskip() == 3);

    const boxer::RenameOutcome r = boxer::renameGameboxPair(imp.createdGamebox, "Keen Renamed", nullptr);
    CHECK(r.kind == boxer::RenameOutcome::Renamed);
    boxer::Gamebox renamed;
    CHECK(renamed.open(r.gameboxPath, &err) && renamed.identifier() == id);
    boxer::GameSettings after;
    CHECK(after.load(boxer::GameSettings::pathFor(where, renamed.identifier()), &err));
    Session s3(after.dict(), fixedSpeed(10000), 0);
    CHECK(s3.s.speed() == fixedSpeed(25000) && s3.s.frameskip() == 3);
}

// Ranges and overflow: the limits themselves are taken, one past them and
// huge, negative or malformed values are refused with a sentence, and a
// refused value is never a choice (the lower layers apply).
static void testValidation() {
    std::vector<std::string> p;
    CHECK(parseEmulationOverride(std::to_string(kMinSpeed), "0", &p).speed == fixedSpeed(kMinSpeed));
    CHECK(parseEmulationOverride(std::to_string(kMaxSpeed), "", &p).speed == fixedSpeed(kMaxSpeed));
    CHECK(*parseEmulationOverride("", std::to_string(kMaxFrameskip), &p).frameskip == kMaxFrameskip);
    CHECK(*parseEmulationOverride("", "0", &p).frameskip == 0);
    CHECK(p.empty());
    const char *badCycles[] = {"49", "62501", "0", "-1", "-62500", "2147483648", "4294967346",
                               "99999999999999999999999999", "1e4", " 3000", "3000 ", "+3000", "0x100", "autox"};
    for (const char *b : badCycles) {
        p.clear();
        const EmulationChoice c = parseEmulationOverride(b, "", &p);
        CHECK(!c.speed && p.size() == 1 && p[0].find("CYCLES") != std::string::npos);
    }
    const char *badSkip[] = {"10", "-1", "4294967296", "18446744073709551617", "99999999999999999999", "1.5", "two"};
    for (const char *b : badSkip) {
        p.clear();
        const EmulationChoice c = parseEmulationOverride("", b, &p);
        CHECK(!c.frameskip && p.size() == 1 && p[0].find("FRAMESKIP") != std::string::npos);
    }
    // A refused override leaves the saved choice and the base in force.
    boxer::PlistValue d = boxer::PlistValue::dict();
    d.set(kCPUSpeedKey, boxer::PlistValue::integer(20000));
    Session s(d, fixedSpeed(10000), 2, parseEmulationOverride("99999999999", "-5"));
    CHECK(s.s.speed() == fixedSpeed(20000) && s.s.frameskip() == 2);

    // Stored values: the limits are taken, anything else is ignored and
    // reported, and the gamebox's configuration applies.
    const struct { long long speed, skip; bool speedOk, skipOk; } stored[] = {
        {kMinSpeed, 0, true, true}, {kMaxSpeed, kMaxFrameskip, true, true}, {-1, 0, true, true},
        {kMinSpeed - 1, kMaxFrameskip + 1, false, false}, {kMaxSpeed + 1, -1, false, false},
        {0, 0, false, true}, {-2, 1, false, true},
        {2147483648LL, 4294967296LL, false, false}, {-9223372036854775807LL, 9223372036854775807LL, false, false},
    };
    for (const auto &t : stored) {
        boxer::PlistValue e = boxer::PlistValue::dict();
        e.set(kCPUSpeedKey, boxer::PlistValue::integer(t.speed));
        e.set(kFrameskipKey, boxer::PlistValue::integer(t.skip));
        p.clear();
        const EmulationChoice c = readEmulationChoice(e, &p);
        CHECK(bool(c.speed) == t.speedOk && bool(c.frameskip) == t.skipOk);
        CHECK(p.size() == size_t(!t.speedOk) + size_t(!t.skipOk));
        Session u(e, fixedSpeed(10000), 3);
        if (!t.speedOk) CHECK(u.s.speed() == fixedSpeed(10000));
        if (!t.skipOk) CHECK(u.s.frameskip() == 3);
    }
    // Wrong types: a string, a real, a boolean.
    boxer::PlistValue w = boxer::PlistValue::dict();
    w.set(kCPUSpeedKey, boxer::PlistValue::real(3000.0));
    w.set(kFrameskipKey, boxer::PlistValue::boolean(true));
    p.clear();
    const EmulationChoice wc = readEmulationChoice(w, &p);
    CHECK(!wc.speed && !wc.frameskip && p.size() == 2);
    // A bad stored value is no choice, so storing removes it (the lower
    // layers applied this session and will again).
    boxer::PlistValue x = boxer::PlistValue::dict();
    x.set(kCPUSpeedKey, boxer::PlistValue::integer(5000000000LL));
    Session v(x, fixedSpeed(10000), 0);
    v.s.store(x);
    CHECK(!hasKey(x, kCPUSpeedKey));
    x.set(kCPUSpeedKey, boxer::PlistValue::integer(5000000000LL));
    // Once the user chooses, the bad value is replaced.
    Session y(x, fixedSpeed(10000), 0);
    y.s.userChoseSpeed(fixedSpeed(25000));
    y.s.store(x);
    CHECK(num(x, kCPUSpeedKey) == 25000);
}

int main(int argc, char **argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: emulation_settings_test <scratch dir>\n"); return 2; }
    const std::string scratch = argv[1];
    fu::removeTree(scratch);
    if (!fu::makeDirs(scratch)) { std::fprintf(stderr, "cannot create %s\n", scratch.c_str()); return 2; }
    testLayers();
    testAutomaticOverFixed();
    testFrameskipZeroOverConfig();
    testNoChoice();
    testOverrideNotPersisted();
    testAutoCountNeverStored();
    testSecondProgram();
    testValidation();
    testFileRoundTripAndRename(scratch);
    if (failures) {
        std::printf("emulation_settings_test: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    std::printf("emulation_settings_test: all %d checks passed\n", checks);
    fu::removeTree(scratch);
    return 0;
}
