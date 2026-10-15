// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXSession.m, BXSession+BXUIControls.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "emulation_settings.h"

#include "../model/plist.h"

#include <cctype>
#include <cstdlib>

namespace boxer_ui {

static bool numberAt(const boxer::PlistValue &dict, const char *key, long long &out) {
    const boxer::PlistValue *v = dict.get(key);
    if (!v || v->type() != boxer::PlistValue::Type::Integer) return false;
    out = v->integer();
    return true;
}

EmulationChoice readEmulationChoice(const boxer::PlistValue &dict) {
    EmulationChoice c;
    long long n = 0;
    if (numberAt(dict, kCPUSpeedKey, n)) {
        if (n == kBXAutoSpeed) c.speed = CPUSpeed::autoSpeed();
        else if (n > 0) c.speed = CPUSpeed::fixed(int(n));
    }
    if (numberAt(dict, kFrameskipKey, n) && n >= 0) c.frameskip = int(n);
    return c;
}

static bool plainNumber(const std::string &s, long &out) {
    if (s.empty()) return false;
    for (char ch : s)
        if (!std::isdigit((unsigned char)ch)) return false;
    out = std::strtol(s.c_str(), nullptr, 10);
    return true;
}

EmulationChoice parseEmulationOverride(const std::string &cycles, const std::string &frameskip) {
    EmulationChoice c;
    long n = 0;
    std::string lc;
    for (char ch : cycles) lc += char(std::tolower((unsigned char)ch));
    if (lc == "auto") c.speed = CPUSpeed::autoSpeed();
    else if (plainNumber(cycles, n) && n > 0) c.speed = CPUSpeed::fixed(int(n));
    if (plainNumber(frameskip, n)) c.frameskip = int(n);
    return c;
}

void EmulationSettings::begin(const EmulationChoice &saved, const EmulationChoice &override, CPUSpeed baseSpeed,
                              int baseFrameskip) {
    begun_ = true;
    saved_ = saved;
    chosen_ = EmulationChoice();
    baseSpeed_ = baseSpeed;
    baseFrameskip_ = baseFrameskip;
    speed_ = override.speed ? *override.speed : saved.speed ? *saved.speed : baseSpeed;
    frameskip_ = override.frameskip ? *override.frameskip : saved.frameskip ? *saved.frameskip : baseFrameskip;
}

void EmulationSettings::userChoseSpeed(CPUSpeed s) {
    chosen_.speed = s;
    speed_ = s;
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
    if (!c.speed || *c.speed == baseSpeed_) dict.remove(kCPUSpeedKey);
    else dict.set(kCPUSpeedKey, boxer::PlistValue::integer(c.speed->automatic ? kBXAutoSpeed : c.speed->cycles));
    if (!c.frameskip || *c.frameskip == baseFrameskip_) dict.remove(kFrameskipKey);
    else dict.set(kFrameskipKey, boxer::PlistValue::integer(*c.frameskip));
}

}  // namespace boxer_ui
