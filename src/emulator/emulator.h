// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXEmulator.mm, Boxer/BXCoalface.h),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Embedding of Boxer's DOSBox 0.74 core.
//
// Plays the role of the original BXEmulator (Boxer/BXEmulator.mm): owns the
// DOSBox configuration and run loop and answers the core's boxer_* hooks
// (Boxer/BXCoalface.h). Platform code supplies an EmulatorDelegate for video,
// events and session notifications. Nothing here includes AROS headers, so
// the same layer serves every target architecture.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace boxer {

class FileSystem;

struct FrameInfo {
	unsigned width = 0, height = 0;   // DOSBox output size in pixels
	double scaleX = 1.0, scaleY = 1.0; // DOSBox's requested aspect scaling
};

class EmulatorDelegate {
public:
	virtual ~EmulatorDelegate() = default;
	// Video: pixels are 32-bit, one uint32_t per pixel holding 0xFFRRGGBB,
	// i.e. B,G,R,A bytes in memory on little-endian hosts.
	virtual void frameSizeChanged(const FrameInfo &info) = 0;
	virtual void frameFinished(const uint32_t *pixels, unsigned pitchBytes,
	                           const FrameInfo &info) = 0;
	// Called whenever the core yields; the host feeds input from here.
	virtual void processEvents() = 0;

	virtual void emulationStateChanged(int cycles, int frameskip, bool paused) {}
	virtual void mouseLockRequested(bool active) {}
	virtual void shellWillStart() {}
	// Reached through the boxer_preflight and boxer_launch lines of the
	// original Preflight.conf and Launch.conf autoexecs
	// (BXSession runPreflightCommandsForEmulator / runLaunchCommandsForEmulator):
	// mount drives in the first, queue the program in the second.
	virtual void runPreflightCommands() {}
	virtual void runLaunchCommands() {}
	virtual void returnedToShell() {}
	virtual void programWillStart(const std::string &dosPath, const std::string &args) {}
	virtual void programDidFinish(const std::string &dosPath) {}
	virtual void driveMounted(int index) {}
	virtual void driveUnmounted(int index) {}
	virtual void log(const std::string &line) {}
};

// Configuration layering as in BXSession configurationURLsForEmulator:
// Preflight.conf and Launch.conf must exist; a gamebox's own conf may not.
struct ConfigFile {
	std::string path;
	bool required;
};

class Emulator {
public:
	explicit Emulator(EmulatorDelegate &delegate, FileSystem &files);
	~Emulator() = default;

	// The running Emulator, or null outside run().
	static Emulator *current();

	// Blocks until the DOS session ends. Configuration files are parsed in
	// order, later files overriding earlier ones. A missing or unparsable
	// required file is an error. The core keeps global state, so only the
	// first run() per process may start it (BXEmulator.mm:115); later calls
	// fail without touching the core.
	// Returns 0 on a normal end, nonzero with lastError() set otherwise.
	int run(const std::vector<ConfigFile> &configFiles);
	const std::string &lastError() const { return error_; }

	void requestQuit() { cancelled_ = true; }

	// Boxer's master volume, 0..1, applied on top of every mixer channel
	// (boxer_masterVolume in BXCoalfaceAudio.mm). Takes effect at once.
	void setMasterVolume(float volume);
	float masterVolume() const { return masterVolume_; }
	bool cancelled() const { return cancelled_; }

	// Commands typed into the DOS shell on Boxer's behalf, run in order
	// before the shell reads the keyboard (BXEmulator+BXShell).
	void queueCommand(const std::string &line) { pending_.push_back(line); }

	// Translation table for DOSBox messages (DOSBox.strings in the
	// original); an absent key yields an empty string as in Boxer.
	void loadMessages(const std::string &tablePath);

	// --- used by the hook implementations ---
	EmulatorDelegate &delegate() { return delegate_; }
	FileSystem &files() { return files_; }
	std::deque<std::string> &pendingCommands() { return pending_; }
	const char *message(const char *key) const;
	void setError(const std::string &e) { error_ = e; }

	FrameInfo frame;
	std::vector<uint32_t> frameBuffer;
	void *renderCallback = nullptr; // GFX_CallBack_t, kept opaque here

private:
	EmulatorDelegate &delegate_;
	FileSystem &files_;
	bool cancelled_ = false;
	float masterVolume_ = 1.0f;
	std::deque<std::string> pending_;
	std::vector<std::pair<std::string, std::string>> messages_;
	std::string error_;
};

} // namespace boxer
