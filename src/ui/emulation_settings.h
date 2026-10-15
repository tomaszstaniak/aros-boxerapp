// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXSession.m, BXSession+BXUIControls.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// A game's CPU speed and frameskip across sessions, without Zune or AROS
// headers, so it is unit-tested on the host
// (src/ui/tests/emulation_settings_test.cpp).
//
// What applies, highest first:
//   1. a one-run override (CYCLES / FRAMESKIP argument or ToolType), never saved;
//   2. the user's saved choice (the game's settings file);
//   3. the gamebox's own configuration;
//   4. the defaults.
// 3 and 4 are what the core starts with ("base"), read before 1 and 2 are
// applied. The original saved the speed into the gamebox's configuration
// (BXSession synchronizeSettings); gameboxes are left unchanged here, so the
// choice lives in the per-game settings file (<data>/Game Settings/
// <identifier>.plist), with the original's keys: "CPUSpeed" (BXAutoSpeed,
// -1, or fixed cycles) and "frameskip".
//
// "No saved choice" (no key) and "the user chose Automatic" (CPUSpeed -1)
// are different: with a gamebox that sets fixed cycles, Automatic must be
// stored, or the next session would get the gamebox's cycles back. A key is
// left out only when the layers below give the same value.
#ifndef BOXER_UI_EMULATION_SETTINGS_H
#define BOXER_UI_EMULATION_SETTINGS_H

#include <optional>
#include <string>

namespace boxer {
class PlistValue;
}

namespace boxer_ui {

constexpr const char *kCPUSpeedKey = "CPUSpeed";
constexpr const char *kFrameskipKey = "frameskip";
constexpr int kBXAutoSpeed = -1;   // BXSession+BXUIControls.h

// Automatic, or a fixed number of cycles. In automatic mode the core
// computes its own cycle count; that number is never part of this value.
struct CPUSpeed {
    bool automatic = true;
    int cycles = 0;   // fixed only
    static CPUSpeed autoSpeed() { return {}; }
    static CPUSpeed fixed(int c) { return {false, c}; }
    bool operator==(const CPUSpeed &o) const { return automatic == o.automatic && (automatic || cycles == o.cycles); }
    bool operator!=(const CPUSpeed &o) const { return !(*this == o); }
};

struct EmulationChoice {
    std::optional<CPUSpeed> speed;
    std::optional<int> frameskip;
};

// The saved choice in a game settings dictionary; a key that is missing or
// not a number is no choice.
EmulationChoice readEmulationChoice(const boxer::PlistValue &dict);
// The CYCLES / FRAMESKIP argument values ("" = not given): "auto",
// or a positive number; anything else is no override.
EmulationChoice parseEmulationOverride(const std::string &cycles, const std::string &frameskip);

// One session of one game.
class EmulationSettings {
public:
    // At the start of the session, before the first program: saved is the
    // file's choice, override the one-run values, base what the core
    // started with from the gamebox's configuration and the defaults.
    void begin(const EmulationChoice &saved, const EmulationChoice &override, CPUSpeed baseSpeed, int baseFrameskip);
    bool begun() const { return begun_; }
    // What the core is to run with now.
    CPUSpeed speed() const { return speed_; }
    int frameskip() const { return frameskip_; }
    // A change made in the Inspector: the user's choice, saved at the end.
    // Nothing else (the core's own auto cycle count, fast forward, pause, a
    // new program in the session) changes what is saved.
    void userChoseSpeed(CPUSpeed s);
    void userChoseFrameskip(int f);
    // What the file should hold after this session.
    EmulationChoice toSave() const;
    // Writes toSave() into the settings dictionary: a key the layers below
    // would restore anyway is removed, every other is set. Nothing is
    // written before begin() (the base is unknown then).
    void store(boxer::PlistValue &dict) const;

private:
    bool begun_ = false;
    EmulationChoice saved_, chosen_;
    CPUSpeed baseSpeed_;
    int baseFrameskip_ = 0;
    CPUSpeed speed_;
    int frameskip_ = 0;
};

}  // namespace boxer_ui
#endif
