// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXCoalface.mm),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// The core's boxer_* hooks (Boxer/BXCoalface.h), answered on behalf of the
// running Emulator. Each function names the original implementation in
// Boxer/BXCoalface.mm whose behaviour it reproduces; where this port does
// less, the comment says what is missing.

#include "emulator.h"
#include "filesystem.h"

#include "dosbox.h"
#include "BXCoalfaceAudio.h"
#include "BXCoalfaceDrives.h"
#include "cross.h"
#include "mapper.h"
#include "setup.h"
#include "shell.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using boxer::Emulator;

namespace {

Emulator &emu() { return *Emulator::current(); }

bool waitingForCommandInput = false;

std::string vformat(const char *format, va_list args)
{
	char buffer[1024];
	vsnprintf(buffer, sizeof buffer, format, args);
	return buffer;
}

} // namespace

// --- Run loop (BXCoalface.mm "Runloop state functions") -----------------

void boxer_processEvents() { emu().delegate().processEvents(); }
void boxer_runLoopWillStartWithContextInfo(void **) {}
void boxer_runLoopDidFinishWithContextInfo(void *) {}
bool boxer_runLoopShouldContinue() { return !emu().cancelled(); }

void boxer_handleDOSBoxTitleChange(Bit32s cycles, Bits frameskip, bool paused)
{
	emu().delegate().emulationStateChanged(cycles, frameskip, paused);
}

// --- Rendering ------------------------------------------------------------

void boxer_applyRenderingStrategy() {}

Bitu boxer_prepareForFrameSize(Bitu width, Bitu height, Bitu, double scalex,
                               double scaley, GFX_CallBack_t callback)
{
	Emulator &e = emu();
	e.frame.width = width;
	e.frame.height = height;
	e.frame.scaleX = scalex;
	e.frame.scaleY = scaley;
	e.frameBuffer.assign(size_t(width) * height, 0xFF000000u);
	e.renderCallback = reinterpret_cast<void *>(callback);
	e.delegate().frameSizeChanged(e.frame);
	// As in Boxer: always 32 bpp, scaling done by the frontend.
	return GFX_CAN_32 | GFX_SCALING;
}

Bitu boxer_idealOutputMode(Bitu) { return GFX_CAN_32 | GFX_SCALING; }

bool boxer_startFrame(Bit8u **frameBuffer, Bitu *pitch)
{
	Emulator &e = emu();
	if (e.frameBuffer.empty())
		return false;
	*frameBuffer = reinterpret_cast<Bit8u *>(e.frameBuffer.data());
	*pitch = e.frame.width * 4;
	return true;
}

void boxer_finishFrame(const uint16_t *)
{
	// Dirty-block hints are ignored: the frontend uploads whole frames, as
	// Boxer's OpenGL renderer did.
	Emulator &e = emu();
	if (!e.frameBuffer.empty())
		e.delegate().frameFinished(e.frameBuffer.data(), e.frame.width * 4, e.frame);
}

Bitu boxer_getRGBPaletteEntry(Bit8u red, Bit8u green, Bit8u blue)
{
	return 0xFF000000u | (Bitu(red) << 16) | (Bitu(green) << 8) | blue;
}

void boxer_setPalette(Bitu, Bitu, GFX_PalEntry *)
{
	// Boxer asserts here: only 8-bit surface output calls it, and the core
	// is always given 32-bit output.
	E_Exit("boxer_setPalette called on 32-bit output");
}

// --- Shell (BXCoalface.mm "Shell-related functions", BXEmulator+BXShell) --

void boxer_shellWillStart(DOS_Shell *) { emu().delegate().shellWillStart(); }
void boxer_shellDidFinish(DOS_Shell *) {}
void boxer_shellWillStartAutoexec(DOS_Shell *) {}
void boxer_didReturnToShell(DOS_Shell *) { emu().delegate().returnedToShell(); }
bool boxer_shellShouldContinue(DOS_Shell *) { return !emu().cancelled(); }

// Returns true to let DOSBox run the command itself, false when Boxer
// handled it (BXEmulator+BXShell.mm _handleCommand). Of Boxer's command
// table only boxer_preflight and boxer_launch exist so far; the remaining
// commands and aliases are later parity work.
bool boxer_shellShouldRunCommand(DOS_Shell *, char *cmd, char *)
{
	std::string command(cmd);
	for (auto &c : command)
		c = char(tolower((unsigned char)c));
	if (command == "boxer_preflight") {
		emu().delegate().runPreflightCommands();
		return false;
	}
	if (command == "boxer_launch") {
		emu().delegate().runLaunchCommands();
		return false;
	}
	return true;
}

// The original's _handleCommandInput (BXEmulator+BXShell.mm:553-566): with
// commands pending, abandon the typed line and return to the shell loop at
// once, so queued commands (launch panel) run while DOS waits at the prompt.
bool boxer_handleShellCommandInput(DOS_Shell *shell, char *, Bitu *, bool *execute)
{
	if (emu().pendingCommands().empty()) return false;
	*execute = true;
	shell->WriteOut_NoParsing("\n");
	return true;
}

void boxer_shellWillReadCommandInputFromHandle(DOS_Shell *, Bit16u handle)
{
	if (handle == STDIN) waitingForCommandInput = true;
}

void boxer_shellDidReadCommandInputFromHandle(DOS_Shell *, Bit16u handle)
{
	if (handle == STDIN) waitingForCommandInput = false;
}

bool boxer_hasPendingCommandsForShell(DOS_Shell *)
{
	return !emu().pendingCommands().empty();
}

// BXEmulator+BXShell.mm _executeNextPendingCommand: echo like a batch line,
// then parse; the command is dequeued first because it may run nested ones.
bool boxer_executeNextPendingCommandForShell(DOS_Shell *shell)
{
	auto &queue = emu().pendingCommands();
	if (queue.empty())
		return false;
	std::string command = queue.front();
	queue.pop_front();
	const auto first = command.find_first_not_of(" \t\r\n");
	const auto last = command.find_last_not_of(" \t\r\n");
	if (first == std::string::npos)
		return true;
	command = command.substr(first, last - first + 1);
	if (command.size() >= CMD_MAXLINE)
		E_Exit("Pending command exceeds CMD_MAXLINE: %s", command.c_str());

	const bool echo = shell->echo;
	if (echo && (shell->bf == NULL || command[0] != '@')) {
		shell->ShowPrompt();
		shell->WriteOut_NoParsing(command.c_str());
		shell->WriteOut_NoParsing("\n");
	}
	char line[CMD_MAXLINE];
	strncpy(line, command.c_str(), sizeof line);
	line[sizeof line - 1] = 0;
	shell->ParseLine(line);
	if (echo)
		shell->WriteOut_NoParsing("\n");
	return true;
}

bool boxer_shellShouldDisplayStartupMessages(DOS_Shell *) { return false; }

void boxer_shellWillExecuteFileAtDOSPath(DOS_Shell *, const char *path, const char *args)
{
	emu().delegate().programWillStart(path, args ? args : "");
}
void boxer_shellDidExecuteFileAtDOSPath(DOS_Shell *, const char *path)
{
	emu().delegate().programDidFinish(path);
}
void boxer_shellWillBeginBatchFile(DOS_Shell *, const char *path, const char *args)
{
	emu().delegate().programWillStart(path, args ? args : "");
}
void boxer_shellDidEndBatchFile(DOS_Shell *, const char *path)
{
	emu().delegate().programDidFinish(path);
}

// --- Drives and files -----------------------------------------------------

FILE *boxer_openCaptureFile(const char *, const char *) { return NULL; }

bool boxer_shouldMountPath(const char *path) { return emu().files().mayMount(path); }
bool boxer_shouldShowFileWithName(const char *name) { return emu().files().showFile(name); }
bool boxer_shouldAllowWriteAccessToPath(const char *path, DOS_Drive *drive)
{
	return emu().files().mayWrite(path, drive);
}
void boxer_driveDidMount(Bit8u index) { emu().delegate().driveMounted(index); }
void boxer_driveDidUnmount(Bit8u index) { emu().delegate().driveUnmounted(index); }
void boxer_didCreateLocalFile(const char *, DOS_Drive *) {}
void boxer_didRemoveLocalFile(const char *, DOS_Drive *) {}

FILE *boxer_openLocalFile(const char *path, DOS_Drive *drive, const char *mode)
{
	return emu().files().open(path, mode, drive);
}
bool boxer_removeLocalFile(const char *path, DOS_Drive *drive) { return emu().files().remove(path, drive); }
bool boxer_moveLocalFile(const char *from, const char *to, DOS_Drive *drive)
{
	return emu().files().move(from, to, drive);
}
bool boxer_createLocalDir(const char *path, DOS_Drive *drive) { return emu().files().makeDir(path, drive); }
bool boxer_removeLocalDir(const char *path, DOS_Drive *drive) { return emu().files().removeDir(path, drive); }
bool boxer_getLocalPathStats(const char *path, DOS_Drive *drive, struct stat *out)
{
	return emu().files().stat(path, drive, out);
}
bool boxer_localDirectoryExists(const char *path, DOS_Drive *drive) { return emu().files().dirExists(path, drive); }
bool boxer_localFileExists(const char *path, DOS_Drive *drive) { return emu().files().fileExists(path, drive); }

// Boxer's enumerators omit "." and "..", which DOSBox expects, so it
// prepends them (BXCoalface.mm boxer_openLocalDirectory). Same here.
namespace {
struct DirectoryHandle {
	void *inner;
	int fakeEntries;
};
}

void *boxer_openLocalDirectory(const char *path, DOS_Drive *drive)
{
	void *inner = emu().files().openDir(path, drive);
	if (!inner)
		return NULL;
	return new DirectoryHandle{inner, 2};
}

void boxer_closeLocalDirectory(void *handle)
{
	auto *h = static_cast<DirectoryHandle *>(handle);
	emu().files().closeDir(h->inner);
	delete h;
}

bool boxer_getNextDirectoryEntry(void *handle, char *outName, bool &isDirectory)
{
	auto *h = static_cast<DirectoryHandle *>(handle);
	if (h->fakeEntries) {
		strcpy(outName, h->fakeEntries == 2 ? "." : "..");
		h->fakeEntries--;
		isDirectory = true;
		return true;
	}
	std::string name;
	if (!emu().files().nextEntry(h->inner, name, isDirectory))
		return false;
	strncpy(outName, name.c_str(), CROSS_LEN);
	outName[CROSS_LEN - 1] = 0;
	return true;
}

// FAT structures: AROS targets are little-endian, so these are identities.
// Boxer needed them for PowerPC.
bootstrap boxer_FATBootstrapLittleToHost(bootstrap b) { return b; }
bootstrap boxer_FATBootstrapHostToLittle(bootstrap b) { return b; }
direntry boxer_FATDirEntryLittleToHost(direntry e) { return e; }
direntry boxer_FATDirEntryHostToLittle(direntry e) { return e; }
partTable boxer_FATPartitionTableLittleToHost(partTable t) { return t; }
partTable boxer_FATPartitionTableHostToLittle(partTable t) { return t; }

// --- Input ----------------------------------------------------------------

void boxer_setJoystickActive(bool) {}
void boxer_setMouseActive(bool active) { emu().delegate().mouseLockRequested(active); }
void boxer_mouseMovedToPoint(float, float) {}
void boxer_setCapsLockActive(bool) {}
void boxer_setNumLockActive(bool) {}
void boxer_setScrollLockActive(bool) {}

// Null lets DOSBox keep its own choice; the original maps the macOS input
// source to a DOS layout code.
const char *boxer_preferredKeyboardLayout() { return NULL; }

bool boxer_continueListeningForKeyEvents()
{
	Emulator &e = emu();
	return !(e.cancelled() || (waitingForCommandInput && !e.pendingCommands().empty()));
}

Bitu boxer_numKeyCodesInPasteBuffer() { return 0; }
bool boxer_getNextKeyCodeInPasteBuffer(Bit16u *, bool) { return false; }

// --- Printer: absent in this slice (BXEmulatedPrinter in the original) ----

Bitu boxer_PRINTER_readdata(Bitu, Bitu) { return 0; }
void boxer_PRINTER_writedata(Bitu, Bitu, Bitu) {}
Bitu boxer_PRINTER_readstatus(Bitu, Bitu) { return 0; }
void boxer_PRINTER_writecontrol(Bitu, Bitu, Bitu) {}
Bitu boxer_PRINTER_readcontrol(Bitu, Bitu) { return 0; }
bool boxer_PRINTER_isInited(Bitu) { return false; }

// --- Audio and MIDI (BXCoalfaceAudio.mm) ------------------------------------

void boxer_suggestMIDIHandler(const char *, const char *) {}
bool boxer_MIDIAvailable() { return false; }
void boxer_sendMIDIMessage(Bit8u *) {}
void boxer_sendMIDISysex(Bit8u *, Bitu) {}
float boxer_masterVolume(BXAudioChannel) { return emu().masterVolume(); }

// --- Messages, logging, errors --------------------------------------------

const char *boxer_localizedStringForKey(char const *key) { return emu().message(key); }

void boxer_log(char const *format, ...)
{
	va_list args;
	va_start(args, format);
	std::string line = vformat(format, args);
	va_end(args);
	emu().delegate().log(line);
}

void boxer_die(const char *function, const char *file, int line, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	std::string reason = vformat(format, args);
	va_end(args);
	char where[512];
	snprintf(where, sizeof where, " (%s, %s:%d)", function, file, line);
	throw std::runtime_error(reason + where);
}

// --- Former sdl_mapper.cpp entry points, not compiled into Boxer ---------

void MAPPER_AddHandler(MAPPER_Handler *, MapKeys, Bitu, char const *const, char const *const) {}
void MAPPER_Init(void) {}
void MAPPER_StartUp(Section *) {}
void MAPPER_Run(bool) {}
void MAPPER_RunInternal() {}
void MAPPER_LosingFocus(void) {}
