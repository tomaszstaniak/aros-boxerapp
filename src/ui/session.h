// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXSession.m, BXImportSession.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// How a DOS session ends, without Zune or AROS headers, so it is unit-tested
// on the host (src/ui/tests/session_test.cpp).
#ifndef BOXER_UI_SESSION_H
#define BOXER_UI_SESSION_H

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace boxer_ui {

// Normal: the core stopped on its own (the DOS program or shell ended it).
// Cancelled: the core stopped because the user asked it to (closing the
//   session window, Quit, ending an installer).
// PreparationFailed: the core's global state was never touched (gamebox not
//   opened, no configuration, no memory for its stack, or the core refused
//   before starting, e.g. a missing required .conf); another session can
//   still run in this process.
// CoreFailed: the core started and ended with an error.
enum class SessionEnd { Normal, Cancelled, PreparationFailed, CoreFailed };

struct SessionResult {
    SessionEnd end;
    std::string error;   // empty unless PreparationFailed or CoreFailed
};

// An error wins over a cancel: a core that failed while it was being
// stopped still failed.
SessionEnd classifySessionEnd(bool prepared, bool coreEntered, bool coreStarted, int coreResult,
                              bool userCancelled);

// Only the two failures are shown to the user (BXSession's error alerts).
bool sessionEndIsError(SessionEnd end);
const char *sessionEndName(SessionEnd end);

// BXImportSession emulatorDidFinish: (IS:1095-1130): the import goes on after
// the installer's session ended, by itself or because the user ended it with
// "Finish Importing", unless the user stopped the import; a session that
// failed stops it, so nothing half-installed is imported as a finished game.
bool importContinuesAfterInstaller(SessionEnd end, bool userStopped);

// BoxerUI's return code (AmigaDOS: 0 OK, 10 ERROR), so that a script or a
// launcher can tell from RC that a session could not be prepared or that
// the core failed. A normal end and the user's own cancel are not errors.
constexpr int kExitOk = 0, kExitError = 10;
int sessionExitCode(SessionEnd end);
// One BoxerUI may run more than one session (a DOS session, an installer):
// the worst outcome wins, and a later good session does not hide an earlier
// failure.
class ExitStatus {
public:
    void note(SessionEnd end);
    void noteError() { code_ = kExitError; }   // an error outside a session (setup)
    int code() const { return code_; }
private:
    int code_ = kExitOk;
};

// Owns the state of one session for exactly as long as the session runs.
// Constructing it publishes a fresh State through `slot` (the pointer the
// event handlers read); on every way out of the owning scope (the normal
// return, an early return, an exception) the clean-ups registered with
// onExit() run in reverse order and the slot gets back the value it had
// before, so nothing keeps pointing at state that no longer exists.
template <class State>
class SessionScope {
public:
    explicit SessionScope(State *&slot) : slot_(slot), previous_(slot) { slot_ = &state_; }
    ~SessionScope() {
        for (auto it = cleanups_.rbegin(); it != cleanups_.rend(); ++it) (*it)();
        slot_ = previous_;
    }
    SessionScope(const SessionScope &) = delete;
    SessionScope &operator=(const SessionScope &) = delete;

    State &state() { return state_; }
    void onExit(std::function<void()> cleanup) { cleanups_.push_back(std::move(cleanup)); }

private:
    State state_;
    State *&slot_;
    State *previous_;
    std::vector<std::function<void()>> cleanups_;
};

// Everything a started session changes outside its own state, undone in
// this order (the enum order is the run order, wherever a step was
// registered). Why this order:
// - FreeStack first: the core has returned from its stack, nothing points
//   into it, and 2 MB go back before the window work below.
// - ReleaseInput, Unpause and HideLaunchPanel need the session still marked
//   running (they release keys and buttons in DOS, resume its audio).
// - StopSession marks it stopped, so no handler run by the window changes
//   below reaches the core any more.
// - DetachViews: the views hold pointers into frame buffers the emulator
//   owns; they let go before the emulator is destroyed.
// - RestorePointer while the window that has the blank pointer is still
//   open (the full-screen one in full screen).
// - UnpublishCore clears the emulator and host pointers before they end.
// - LeaveFullscreen, EnableWindowKeys, CloseWindow last: the window state
//   the session started with (window keys back to Zune, window closed).
enum class TeardownStep {
    FreeStack, ReleaseInput, Unpause, HideLaunchPanel, StopSession, DetachViews, RestorePointer,
    ResetMouse, UnpublishCore, LeaveFullscreen, EnableWindowKeys, CloseWindow
};
constexpr int kTeardownSteps = 12;
const char *teardownStepName(TeardownStep step);

// Runs the registered steps once, in TeardownStep order, at run() or at the
// latest when it is destroyed (an early return or an exception). Never
// throws: a step that throws is reported through `report` and the steps
// after it still run.
class SessionTeardown {
public:
    using Report = std::function<void(const std::string &)>;
    explicit SessionTeardown(Report report) : report_(std::move(report)) {}
    ~SessionTeardown() { run(); }
    SessionTeardown(const SessionTeardown &) = delete;
    SessionTeardown &operator=(const SessionTeardown &) = delete;

    void set(TeardownStep step, std::function<void()> undo) { steps_[int(step)] = std::move(undo); }
    bool has(TeardownStep step) const { return bool(steps_[int(step)]); }
    void run() noexcept;

private:
    void reportNoThrow(const std::string &text) noexcept;
    Report report_;
    std::function<void()> steps_[kTeardownSteps];
};

// What the core run returned (CoreCall in main.cpp).
struct CoreRun {
    int result = -1;
    bool started = false;     // Emulator::coreHasStarted() afterwards
    bool cancelled = false;   // the user asked the core to stop
    std::string error;
};

// The front end's part of a session from the start of the core on; main.cpp
// fills it with the Zune and exec calls, the host test with recorders.
// Every undo action must be harmless when its change was not made (begin()
// may fail half way).
struct SessionSteps {
    std::function<void()> begin;   // publish the core, take window keys, open the window
    std::function<void *()> allocStack;   // null: no memory
    std::function<void(void *)> freeStack;
    std::function<CoreRun(void *stack)> runCore;
    std::function<void(const SessionResult &)> closing;   // before the teardown (game settings)
    std::function<void(const SessionResult &)> ended;     // after it (end log)
    std::function<void()> releaseInput, unpause, hideLaunchPanel, stopSession, detachViews,
        restorePointer, resetMouse, unpublishCore, leaveFullscreen, enableWindowKeys, closeWindow;
    std::function<void(const std::string &)> report;   // log line
};

// Registers every undo action of `s` (all but FreeStack) with `t`. When one
// is empty it registers none, names the missing ones through s.report and
// returns false.
bool registerSessionTeardown(SessionTeardown &t, const SessionSteps &s);

// The started session: registers the teardown before begin() changes
// anything, runs the core on its own stack and undoes everything on every
// way out. An exception is reported as an error, never swallowed: before
// the core was entered PreparationFailed, from then on CoreFailed.
SessionResult runStartedSession(const SessionSteps &s);

}  // namespace boxer_ui
#endif
