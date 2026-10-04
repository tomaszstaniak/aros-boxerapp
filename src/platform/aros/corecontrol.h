// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXEmulator.mm),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Session controls applied to the running core, without DOSBox headers in
// the caller (they need gnu++14; the UI is C++17). Each function names the
// original it reproduces. Call only while Emulator::run() is active.
#pragma once

#include <string>

namespace boxer {

// BXEmulator pause/resume (BXEmulator.mm:563-600): the run loop stops
// (the host keeps servicing the UI) and audio output is suspended
// (_suspendAudio / _resumeAudio).
void corePauseAudio(bool paused);

// Port addition: the core mixer's host-audio counters as one log line
// (device size, host vs emulated ms, callbacks, underruns, overflows).
// Valid during the session and, frozen at the mixer's shutdown, after it.
std::string coreAudioStats();

// BXEmulator setFixedSpeed: / setAutoSpeed: (BXEmulator.mm:396-457).
// cycles > 0: fixed speed; cycles < 0: automatic throttling ("Max").
void coreSetSpeed(int cycles);
// Current CPU_CycleMax and whether automatic throttling is on.
int coreCycles(bool *autoSpeed);

// BXEmulator turboSpeed (BXEmulator.mm:460-497): fast forward. Turning it
// on suppresses automatic throttling (cycles / 3, at least 1000) and lets
// emulated time run ahead of the host; turning it off restores automatic
// throttling if it was on. Fixed cycles and frameskip are left as they are.
void coreSetTurbo(bool on);
bool coreTurbo();

// BXVideoHandler setFrameskip: (BXVideoHandler.mm:126-134).
void coreSetFrameskip(int frameskip);
int coreFrameskip();

// BXEmulator setMasterVolume: (BXEmulator+BXAudio.mm:401-424), 0..1.
// Applied through each mixer channel's volume; see corecontrol.cpp.
void coreSetMasterVolume(float volume);

// Host milliseconds as the core counts them (SDL_GetTicks).
unsigned coreHostMillis();

} // namespace boxer
