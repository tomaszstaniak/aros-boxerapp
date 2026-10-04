// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXEmulator.mm),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "../../emulator/emulator.h"
#include "corecontrol.h"

#include "dosbox.h"
#include "cpu.h"
#include "mixer.h"
#include "render.h"
#include "timer.h"

#include <cstdio>
#include <cstring>

// DOSBox's fast-forward switch (dosbox.cpp:129, 160-166): while set, the
// core runs 5 ms of emulated time per loop without waiting for the host.
extern bool ticksLocked;

namespace boxer {

void corePauseAudio(bool paused)
{
	// The core owns the host device (SDL2 legacy device or SDL3 stream,
	// mixer.cpp); this layer does not see which.
	MIXER_SetHostPaused(paused);
}

std::string coreAudioStats()
{
	MixerHostStats st;
	MIXER_GetHostStats(&st);
	// One line per stage: core generation (emulated time), hand-off to the
	// host API, device pull cadence, device format, API errors.
	// Two calls: on the guest, values after about the 20th argument of one
	// snprintf call came out as stack garbage (2026-10-02 apA1/rsa1 logs).
	char b[384], c[384];
	std::snprintf(b, sizeof b,
		"nosound=%d freq=%u blocksize=%u host_ms=%u emu_ms=%u callbacks=%u frames=%llu "
		"underrun_full=%u underrun_stretch=%u underrun_low=%u overflow=%u mixed=%llu delivered=%llu",
		st.nosound ? 1 : 0, (unsigned)st.freq, (unsigned)st.blocksize,
		(unsigned)(st.now_ms - st.open_ms), (unsigned)st.mix_ticks, (unsigned)st.callbacks,
		(unsigned long long)st.frames_requested, (unsigned)st.underruns_full,
		(unsigned)st.underruns_stretched, (unsigned)st.underruns_low, (unsigned)st.overflows,
		(unsigned long long)st.frames_mixed, (unsigned long long)st.frames_delivered);
	std::snprintf(c, sizeof c,
		" cb_gap_max_ms=%u put=%u put_fail=%u put_bytes=%llu queued_max=%u pause=%u pause_fail=%u"
		" dev_freq=%d dev_frames=%d dev_ch=%d dev_fmt=0x%x error=\"%s\"",
		(unsigned)st.cb_gap_max_ms, (unsigned)st.put_calls, (unsigned)st.put_failures,
		(unsigned long long)st.put_bytes, (unsigned)st.queued_max, (unsigned)st.pause_calls,
		(unsigned)st.pause_failures, (int)st.dev_freq, (int)st.dev_frames, (int)st.dev_channels,
		(unsigned)st.dev_format, st.error);
	return std::string(b) + c;
}

void coreSetSpeed(int cycles)
{
	if (cycles > 0) {
		// setFixedSpeed: turn automatic throttling off, set both maxima,
		// keep DOSBox from resetting cycles when a program exits, and
		// drop the queued cycles as DOSBox's own CPU code does.
		CPU_CycleAutoAdjust = false;
		CPU_OldCycleMax = CPU_CycleMax = (Bit32s)cycles;
		CPU_AutoDetermineMode &= ~CPU_AUTODETERMINE_CYCLES;
		CPU_CycleLeft = 0;
		CPU_Cycles = 0;
	} else if (!CPU_CycleAutoAdjust) {
		// setAutoSpeed:YES records the fixed value and forces 100% usage.
		CPU_OldCycleMax = CPU_CycleMax;
		CPU_CyclePercUsed = 100;
		CPU_CycleAutoAdjust = true;
	}
}

static bool wasAutoSpeed;

int coreCycles(bool *autoSpeed)
{
	// BXEmulator isAutoSpeed: while in turbo, report the setting turbo
	// will return to.
	if (autoSpeed) *autoSpeed = ticksLocked ? wasAutoSpeed : CPU_CycleAutoAdjust;
	return (int)CPU_CycleMax;
}

// BXEmulator setTurboSpeed: (BXEmulator.mm:465-497), the same steps as
// DOSBox's own Alt+F12 handler (dosbox.cpp:265-283). Frameskip and audio
// are not touched; while ticksLocked the mixer itself puts IRQ timing
// before smooth output (Mixer_irq_important, mixer.cpp:358-362).
void coreSetTurbo(bool on)
{
	if (on == ticksLocked) return;
	if (on) {
		ticksLocked = true;
		wasAutoSpeed = CPU_CycleAutoAdjust;
		if (wasAutoSpeed) {
			CPU_CycleAutoAdjust = false;
			CPU_CycleMax /= 3;
			if (CPU_CycleMax < 1000) CPU_CycleMax = 1000;
		}
	} else {
		ticksLocked = false;
		if (wasAutoSpeed) {
			wasAutoSpeed = false;
			CPU_CycleAutoAdjust = true;
		}
	}
}

bool coreTurbo() { return ticksLocked; }

void coreSetFrameskip(int frameskip)
{
	render.frameskip.max = (Bitu)(frameskip < 0 ? 0 : frameskip);
}

int coreFrameskip() { return (int)render.frameskip.max; }

// Master volume as in the original: boxer_masterVolume() feeds
// MixerChannel::UpdateVolume (mixer.cpp:118-125) and boxer_updateVolumes()
// (mixer.cpp:685) re-applies it to every channel, so channel volumes set by
// DOS programs or the MIXER command stay their own.
void coreSetMasterVolume(float v)
{
	if (Emulator *e = Emulator::current())
		e->setMasterVolume(v);
}

unsigned coreHostMillis() { return (unsigned)GetTicks(); }

} // namespace boxer
