// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXEmulator/BXSession launch sequence),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// boxer-core-smoke: runs the embedded DOSBox core in a plain Intuition
// window, for the core slice's first runs. It is test scaffolding, not the
// product window: the Zune session window replaces it once both slices meet.
//
// Usage: boxer-core-smoke LOG <file> [MSG <table>] CONF <file> [CONF|OPTCONF <file>...]
//                         [PRE <dos command>...] [CMD <dos command>...]
// PRE commands run when the autoexec reaches boxer_preflight (Preflight.conf),
// CMD commands when it reaches boxer_launch (Launch.conf), as in Boxer.
// CONF files are required (missing = error), OPTCONF files optional.
//
// Gamebox mode: GAMEBOX <path> DATA <dir> CONFDIR <dir> [OPTCONF <file>...]
// [CMD ...] [THEN ...]. The gamebox model supplies the configuration layering, the
// preflight MOUNT/IMGMOUNT lines and (without CMD) the default launcher;
// all DOS writes go through the shadow filesystem into
// <DATA>/Gamebox States/<id>/Current.boxerstate, never into the gamebox.
// Everything the run does is written to the LOG file, because a Shell can
// hold another program's output.

#include "../../emulator/emulator.h"
#include "../../emulator/filesystem.h"
#include "../../model/datalocations.h"
#include "../../model/gamebox.h"
#include "../../model/shadowfs.h"
#include "corecontrol.h"
#include "rawkeys.h"
#include "session_setup.h"

#include "dosbox.h"
#include "keyboard.h"
#include "mouse.h"

#include <proto/cybergraphics.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <cybergraphx/cybergraphics.h>
#include <intuition/intuition.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>
#include <string>
#include <vector>

namespace {

class SmokeHost : public boxer::EmulatorDelegate {
public:
	explicit SmokeHost(FILE *log) : log_(log) {}
	~SmokeHost() override { closeWindow(); }

	void setEmulator(boxer::Emulator *e) { emulator_ = e; }

	void frameSizeChanged(const boxer::FrameInfo &info) override
	{
		note("frame size %ux%u scale %.3f,%.3f", info.width, info.height, info.scaleX, info.scaleY);
		closeWindow();
		window_ = OpenWindowTags(NULL,
			WA_Title, (IPTR)"Boxer core smoke",
			WA_InnerWidth, info.width, WA_InnerHeight, info.height,
			WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
			WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_ReportMouse, TRUE,
			WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_RAWKEY | IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS,
			TAG_DONE);
		if (!window_)
			note("OpenWindowTags failed");
	}

	void frameFinished(const uint32_t *pixels, unsigned pitch, const boxer::FrameInfo &info) override
	{
		++frames_;
		if (!window_)
			return;
		WritePixelArray((APTR)pixels, 0, 0, pitch, window_->RPort,
			window_->BorderLeft, window_->BorderTop, info.width, info.height, RECTFMT_BGRA32);
	}

	// Diagnostic flag AUDIOSTATS <ms> (2026-10-03 audio2): log the core's
	// audio counters at that interval of host time, so generation and pull
	// can be followed during a run and not only summed at its end.
	unsigned statsEveryMs = 0;
	void maybeAudioStats(bool force)
	{
		if (!statsEveryMs)
			return;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		const unsigned long long now = tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
		if (!force && now < nextStats_)
			return;
		nextStats_ = now + statsEveryMs;
		note("audio wall_ms=%llu %s", now, boxer::coreAudioStats().c_str());
	}

	void processEvents() override
	{
		maybeAudioStats(false);
		if (!window_)
			return;
		while (struct IntuiMessage *msg = (struct IntuiMessage *)GetMsg(window_->UserPort)) {
			const ULONG cls = msg->Class;
			const UWORD code = msg->Code;
			const WORD mx = msg->MouseX - window_->BorderLeft;
			const WORD my = msg->MouseY - window_->BorderTop;
			ReplyMsg((struct Message *)msg);
			switch (cls) {
			case IDCMP_CLOSEWINDOW:
				note("close requested");
				emulator_->requestQuit();
				break;
			case IDCMP_RAWKEY: {
				const bool up = code & IECODE_UP_PREFIX;
				const KBD_KEYS key = boxer::dosKeyForRawKey(code & ~IECODE_UP_PREFIX);
				note("rawkey %02x %s -> %d", code & ~IECODE_UP_PREFIX, up ? "up" : "down", key);
				if (key != KBD_NONE)
					KEYBOARD_AddKey(key, !up);
				break;
			}
			case IDCMP_MOUSEMOVE:
				moveMouse(mx, my);
				break;
			case IDCMP_MOUSEBUTTONS:
				moveMouse(mx, my);
				switch (code) {
				case SELECTDOWN: Mouse_ButtonPressed(0); break;
				case SELECTUP: Mouse_ButtonReleased(0); break;
				case MENUDOWN: Mouse_ButtonPressed(1); break;
				case MENUUP: Mouse_ButtonReleased(1); break;
				}
				note("button %04x at %d,%d", code, mx, my);
				break;
			}
		}
	}

	void emulationStateChanged(int cycles, int frameskip, bool paused) override
	{
		note("state cycles=%d frameskip=%d paused=%d", cycles, frameskip, paused);
	}
	void shellWillStart() override { note("shell will start"); }
	void runPreflightCommands() override
	{
		note("preflight: queueing %u commands", unsigned(preflight.size()));
		for (const auto &c : preflight) emulator_->queueCommand(c);
	}
	void runLaunchCommands() override
	{
		note("launch: queueing %u commands", unsigned(launch.size()));
		for (const auto &c : launch) emulator_->queueCommand(c);
	}

	std::vector<std::string> preflight, launch;
	void returnedToShell() override { note("returned to shell"); }
	void programWillStart(const std::string &path, const std::string &args) override
	{
		note("program start %s %s", path.c_str(), args.c_str());
	}
	void programDidFinish(const std::string &path) override { note("program end %s", path.c_str()); }
	void driveMounted(int index) override { note("drive mounted %c:", 'A' + index); }
	void driveUnmounted(int index) override { note("drive unmounted %c:", 'A' + index); }
	void log(const std::string &line) override { note("dosbox: %s", line.c_str()); }

	unsigned long frames() const { return frames_; }

	void note(const char *format, ...)
	{
		va_list args;
		va_start(args, format);
		fprintf(log_, "%lu ", (unsigned long)clock());
		vfprintf(log_, format, args);
		fputc('\n', log_);
		fflush(log_);
		va_end(args);
	}

private:
	void moveMouse(WORD x, WORD y)
	{
		if (!window_ || !emulator_->frame.width)
			return;
		const float fx = float(x) / float(emulator_->frame.width);
		const float fy = float(y) / float(emulator_->frame.height);
		Mouse_CursorMoved(float(x - lastX_), float(y - lastY_), fx, fy, false);
		lastX_ = x;
		lastY_ = y;
	}

	void closeWindow()
	{
		if (window_) {
			CloseWindow(window_);
			window_ = nullptr;
		}
	}

	FILE *log_;
	boxer::Emulator *emulator_ = nullptr;
	struct Window *window_ = nullptr;
	unsigned long frames_ = 0;
	unsigned long long nextStats_ = 0;
	WORD lastX_ = 0, lastY_ = 0;
};

} // namespace

int main(int argc, char **argv)
{
	std::string logPath, messages, gameboxPath, dataDir, confDir;
	unsigned statsMs = 0;
	int taskPri = 0;
	bool setPri = false; // TASKPRI <n>: diagnostic, sets this (emulator) task's priority
	int runs = 1; // RUNS 2 checks that a second start in one process is refused
	std::vector<boxer::ConfigFile> confs;
	std::vector<std::string> preflight, commands, then;
	for (int i = 1; i + 1 < argc; i += 2) {
		const std::string key = argv[i], value = argv[i + 1];
		if (key == "LOG") logPath = value;
		else if (key == "MSG") messages = value;
		else if (key == "CONF") confs.push_back({value, true});
		else if (key == "OPTCONF") confs.push_back({value, false});
		else if (key == "PRE") preflight.push_back(value);
		else if (key == "THEN") then.push_back(value);
		else if (key == "RUNS") runs = atoi(value.c_str());
		else if (key == "GAMEBOX") gameboxPath = value;
		else if (key == "DATA") dataDir = value;
		else if (key == "CONFDIR") confDir = value;
		else if (key == "CMD") commands.push_back(value);
		else if (key == "AUDIOSTATS") statsMs = unsigned(atoi(value.c_str()));
		else if (key == "TASKPRI") { taskPri = atoi(value.c_str()); setPri = true; }
		else {
			fprintf(stderr, "unknown argument %s\n", key.c_str());
			return 20;
		}
	}
	if (logPath.empty() || (argc - 1) % 2 || (confs.empty() && gameboxPath.empty())) {
		fprintf(stderr, "usage: %s LOG <file> [MSG <table>] CONF <file>... [PRE <command>...] [CMD <command>...]\n", argv[0]);
		return 20;
	}
	FILE *log = fopen(logPath.c_str(), "w");
	if (!log) {
		fprintf(stderr, "cannot open %s\n", logPath.c_str());
		return 20;
	}

	SmokeHost host(log);
	host.statsEveryMs = statsMs;
	if (setPri) {
		const BYTE old = SetTaskPri(FindTask(NULL), taskPri);
		host.note("taskpri %d -> %d", int(old), taskPri);
	}
	boxer::FileSystem plainFiles;
	boxer::ShadowFileSystem shadowFiles;
	boxer::FileSystem *files = &plainFiles;
	if (!gameboxPath.empty()) {
		std::string error;
		auto setupLog = [&host](const std::string &l) { host.note("%s", l.c_str()); };
		if (!boxer::openGamebox(setupLog, gameboxPath, dataDir, confDir, confs, preflight, commands,
		                        shadowFiles, error)) {
			host.note("gamebox: %s", error.c_str());
			fclose(log);
			return 10;
		}
		files = &shadowFiles;
	}
	// THEN commands follow the launch commands (e.g. "exit" after the
	// default launcher has run).
	commands.insert(commands.end(), then.begin(), then.end());
	boxer::Emulator emulator(host, *files);
	host.setEmulator(&emulator);
	host.note("start, %u config files, %u preflight and %u launch commands",
		unsigned(confs.size()), unsigned(preflight.size()), unsigned(commands.size()));
	try {
		if (!messages.empty())
			emulator.loadMessages(messages);
	} catch (const std::exception &e) {
		host.note("%s", e.what());
		fclose(log);
		return 10;
	}
	host.preflight = preflight;
	host.launch = commands;
	const int result = emulator.run(confs);
	host.maybeAudioStats(true);
	host.note("end result=%d frames=%lu error=%s", result, host.frames(), emulator.lastError().c_str());
	for (int i = 1; i < runs; i++) {
		boxer::Emulator again(host, *files);
		host.setEmulator(&again);
		const int r = again.run(confs);
		host.note("repeat run %d result=%d error=%s", i + 1, r, again.lastError().c_str());
	}
	fclose(log);
	return result ? 10 : 0;
}
