// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXEmulator.mm),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Emulator run sequence: BXEmulator.mm _startDOSBox, with SDL2 or SDL3
// (BOXER_HOST_SDL3) standing in for the SDL 1.2 frameworks (see
// DOSBox/include/boxer_host_sdl.h).

#include "emulator.h"
#include "filesystem.h"

#include "dosbox.h"
#include "control.h"
#include "setup.h"
#include "BXCoalfaceAudio.h"

#include "boxer_host_sdl.h"

#include <fstream>
#include <stdexcept>

void DOSBOX_Init(void);

namespace boxer {

namespace {
// The object currently answering the core's hooks.
Emulator *running = nullptr;
// Set once the core's global state has been touched (DOSBOX_Init onwards)
// and never cleared: DOSBox 0.74 keeps module statics that are not reset,
// so a second start in the same process is refused even after the first
// Emulator is destroyed (one session per process, as BXEmulator.mm:115).
bool coreStarted = false;
}

Emulator::Emulator(EmulatorDelegate &delegate, FileSystem &files)
	: delegate_(delegate), files_(files) {}

Emulator *Emulator::current() { return running; }

int Emulator::run(const std::vector<ConfigFile> &configFiles)
{
	if (coreStarted) {
		error_ = "the emulator core already ran in this process";
		return 1;
	}
	// Required files are checked before anything global changes, so a
	// missing Preflight.conf leaves the process able to report and exit.
	for (const auto &file : configFiles) {
		std::ifstream probe(file.path);
		if (file.required && !probe) {
			error_ = "required configuration file missing: " + file.path;
			return 1;
		}
	}
#ifdef BOXER_HOST_SDL3
	// SDL3's SDL_Init returns bool (true on success), the inverse of SDL2's
	// int; the timer needs no subsystem. Joystick/gamepad are compiled into
	// the SDL3 library but not initialised until controller support uses them.
	if (!SDL_Init(SDL_INIT_AUDIO)) {
#else
	if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
#endif
		// Nothing of the core was touched yet, so this is not counted as a
		// start; SDL_Init cleans up after its own failure.
		error_ = std::string("SDL_Init failed: ") + SDL_GetError();
		return 1;
	}
	coreStarted = true;
	running = this;

	int result = 0;
	try {
		char const *argv[1] = {"boxer"};
		CommandLine commandLine(0, argv);
		Config configuration(&commandLine);
		control = &configuration;
		DOSBOX_Init();
		for (const auto &file : configFiles) {
			if (control->ParseConfigFile(file.path.c_str()))
				continue;
			if (file.required)
				throw std::runtime_error("required configuration file not parsed: " + file.path);
			delegate_.log("optional configuration file not loaded: " + file.path);
		}
		control->Init();
		control->StartUp();
	} catch (char *message) {
		error_ = message;
		result = 1;
	} catch (const char *message) {
		error_ = message;
		result = 1;
	} catch (const std::exception &e) {
		error_ = e.what();
		result = 1;
	} catch (int) {
		// DOSBox's kill switch: a normal shutdown, as in the original.
	}
	control = NULL;
	SDL_Quit();
	running = nullptr;
	return result;
}

void Emulator::setMasterVolume(float volume)
{
	masterVolume_ = volume < 0 ? 0 : volume > 1 ? 1 : volume;
	if (running == this)
		boxer_updateVolumes();
}

// The table is the original's DOSBox.strings converted by
// tools/strings2table.py: pairs of NUL-terminated key and value.
void Emulator::loadMessages(const std::string &tablePath)
{
	std::ifstream in(tablePath, std::ios::binary);
	if (!in)
		throw std::runtime_error("cannot read message table " + tablePath);
	std::string key, value;
	while (std::getline(in, key, '\0') && std::getline(in, value, '\0'))
		messages_.emplace_back(key, value);
}

const char *Emulator::message(const char *key) const
{
	for (const auto &m : messages_)
		if (m.first == key)
			return m.second.c_str();
	return "";
}

} // namespace boxer
