// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXSession.m, BXSession+BXUIControls.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "emulation_settings.h"

#include "ui_logic.h"

#include "../model/plist.h"

#include <cctype>

namespace boxer_ui {

bool validCycles(long long cycles) { return cycles >= kMinSpeed && cycles <= kMaxSpeed; }
bool validFrameskip(long long frameskip) { return frameskip >= 0 && frameskip <= kMaxFrameskip; }

static void note(std::vector<std::string> *problems, const std::string &text) {
    if (problems) problems->push_back(text);
}

EmulationChoice readEmulationChoice(const boxer::PlistValue &dict, std::vector<std::string> *problems) {
    EmulationChoice c;
    if (const boxer::PlistValue *v = dict.get(kCPUSpeedKey)) {
        const bool number = v->type() == boxer::PlistValue::Type::Integer;
        const long long n = number ? (long long)v->integer() : 0;
        if (number && n == kBXAutoSpeed) c.speed = CPUSpeed::autoSpeed();
        else if (number && validCycles(n)) c.speed = CPUSpeed::fixed(int(n));
        else note(problems, std::string("stored ") + kCPUSpeedKey + " is not -1 (automatic) or " +
                                std::to_string(kMinSpeed) + ".." + std::to_string(kMaxSpeed) + " cycles; ignored");
    }
    if (const boxer::PlistValue *v = dict.get(kFrameskipKey)) {
        const bool number = v->type() == boxer::PlistValue::Type::Integer;
        if (number && validFrameskip(v->integer())) c.frameskip = int(v->integer());
        else note(problems, std::string("stored ") + kFrameskipKey + " is not 0.." + std::to_string(kMaxFrameskip) +
                                "; ignored");
    }
    return c;
}

// A whole number without sign, read digit by digit so that no conversion
// can overflow: anything past `limit` is out of range, however long.
static bool wholeNumber(const std::string &s, long long limit, long long &out) {
    if (s.empty()) return false;
    long long n = 0;
    for (char ch : s) {
        if (!std::isdigit((unsigned char)ch)) return false;
        n = n * 10 + (ch - '0');
        if (n > limit) return false;
    }
    out = n;
    return true;
}

EmulationChoice parseEmulationOverride(const std::string &cycles, const std::string &frameskip,
                                       std::vector<std::string> *problems) {
    EmulationChoice c;
    long long n = 0;
    std::string lc;
    for (char ch : cycles) lc += char(std::tolower((unsigned char)ch));
    if (lc == "auto") c.speed = CPUSpeed::autoSpeed();
    else if (wholeNumber(cycles, kMaxSpeed, n) && validCycles(n)) c.speed = CPUSpeed::fixed(int(n));
    else if (!cycles.empty())
        note(problems, "CYCLES \"" + cycles + "\" is not AUTO or a number of cycles from " + std::to_string(kMinSpeed) +
                           " to " + std::to_string(kMaxSpeed) + "; it is not used.");
    if (wholeNumber(frameskip, kMaxFrameskip, n)) c.frameskip = int(n);
    else if (!frameskip.empty())
        note(problems, "FRAMESKIP \"" + frameskip + "\" is not a number from 0 to " + std::to_string(kMaxFrameskip) +
                           "; it is not used.");
    return c;
}

void EmulationSettings::begin(const EmulationChoice &saved, const EmulationChoice &override, CPUBase baseSpeed,
                              int baseFrameskip) {
    begun_ = true;
    saved_ = saved;
    chosen_ = EmulationChoice();
    baseSpeed_ = baseSpeed;
    baseFrameskip_ = baseFrameskip;
    const std::optional<CPUSpeed> choice = override.speed ? override.speed : saved.speed;
    speedChosen_ = bool(choice);
    speed_ = choice ? *choice
                    : baseSpeed.config == CycleConfig::Max ? CPUSpeed::autoSpeed() : CPUSpeed::fixed(baseSpeed.cycles);
    frameskip_ = override.frameskip ? *override.frameskip : saved.frameskip ? *saved.frameskip : baseFrameskip;
}

std::optional<CPUSpeed> EmulationSettings::speedToApply() const {
    if (!speedChosen_ || baseSpeed_.sameAs(speed_)) return std::nullopt;
    return speed_;
}

void EmulationSettings::userChoseSpeed(CPUSpeed s) {
    chosen_.speed = s;
    speed_ = s;
    speedChosen_ = true;
}

void EmulationSettings::userChoseFrameskip(int f) {
    chosen_.frameskip = f;
    frameskip_ = f;
}

EmulationChoice EmulationSettings::toSave() const {
    EmulationChoice c = saved_;
    if (chosen_.speed) c.speed = chosen_.speed;
    if (chosen_.frameskip) c.frameskip = chosen_.frameskip;
    return c;
}

void EmulationSettings::store(boxer::PlistValue &dict) const {
    if (!begun_) return;
    const EmulationChoice c = toSave();
    if (!c.speed || baseSpeed_.sameAs(*c.speed)) dict.remove(kCPUSpeedKey);
    else dict.set(kCPUSpeedKey, boxer::PlistValue::integer(c.speed->automatic ? kBXAutoSpeed : c.speed->cycles));
    if (!c.frameskip || *c.frameskip == baseFrameskip_) dict.remove(kFrameskipKey);
    else dict.set(kFrameskipKey, boxer::PlistValue::integer(*c.frameskip));
}

}  // namespace boxer_ui
