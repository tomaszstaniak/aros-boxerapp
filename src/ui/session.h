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

}  // namespace boxer_ui
#endif
