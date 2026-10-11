// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXSession.m, BXImportSession.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "session.h"

#include <exception>
#include <utility>

namespace boxer_ui {

SessionEnd classifySessionEnd(bool prepared, bool coreEntered, bool coreStarted, int coreResult,
                              bool userCancelled) {
    if (!prepared || !coreEntered) return SessionEnd::PreparationFailed;
    if (coreResult != 0) return coreStarted ? SessionEnd::CoreFailed : SessionEnd::PreparationFailed;
    return userCancelled ? SessionEnd::Cancelled : SessionEnd::Normal;
}

bool sessionEndIsError(SessionEnd end) {
    return end == SessionEnd::PreparationFailed || end == SessionEnd::CoreFailed;
}

const char *sessionEndName(SessionEnd end) {
    switch (end) {
    case SessionEnd::Normal: return "normal end";
    case SessionEnd::Cancelled: return "cancelled by the user";
    case SessionEnd::PreparationFailed: return "preparation failed";
    case SessionEnd::CoreFailed: return "core error";
    }
    return "?";
}

bool importContinuesAfterInstaller(SessionEnd end, bool userStopped) {
    return !sessionEndIsError(end) && !userStopped;
}

const char *teardownStepName(TeardownStep step) {
    static const char *const names[kTeardownSteps] = {
        "free stack", "release input", "unpause", "hide launch panel", "stop session", "detach views",
        "restore pointer", "reset mouse", "unpublish core", "leave full screen", "enable window keys",
        "close window"};
    const int i = int(step);
    return i >= 0 && i < kTeardownSteps ? names[i] : "?";
}

void SessionTeardown::reportNoThrow(const std::string &text) noexcept {
    try {
        if (report_) report_(text);
    } catch (...) {
        // Nowhere left to say it; the teardown must go on.
    }
}

void SessionTeardown::run() noexcept {
    for (int i = 0; i < kTeardownSteps; ++i) {
        // Moved out first, so a step runs once even if run() is called again.
        std::function<void()> undo;
        undo.swap(steps_[i]);
        if (!undo) continue;
        try {
            undo();
        } catch (const std::exception &e) {
            reportNoThrow(std::string("session teardown: step \"") + teardownStepName(TeardownStep(i)) +
                          "\" failed: " + e.what());
        } catch (...) {
            reportNoThrow(std::string("session teardown: step \"") + teardownStepName(TeardownStep(i)) +
                          "\" failed with an unknown exception");
        }
    }
}

bool registerSessionTeardown(SessionTeardown &t, const SessionSteps &s) {
    const std::pair<TeardownStep, const std::function<void()> *> undo[] = {
        {TeardownStep::ReleaseInput, &s.releaseInput},       {TeardownStep::Unpause, &s.unpause},
        {TeardownStep::HideLaunchPanel, &s.hideLaunchPanel}, {TeardownStep::StopSession, &s.stopSession},
        {TeardownStep::DetachViews, &s.detachViews},         {TeardownStep::RestorePointer, &s.restorePointer},
        {TeardownStep::ResetMouse, &s.resetMouse},           {TeardownStep::UnpublishCore, &s.unpublishCore},
        {TeardownStep::LeaveFullscreen, &s.leaveFullscreen}, {TeardownStep::EnableWindowKeys, &s.enableWindowKeys},
        {TeardownStep::CloseWindow, &s.closeWindow}};
    bool complete = true;
    for (const auto &u : undo) {
        if (*u.second) continue;
        complete = false;
        if (s.report) s.report(std::string("session teardown: no action for \"") + teardownStepName(u.first) + "\"");
    }
    if (!complete) return false;   // nothing registered: the session does not start
    for (const auto &u : undo) t.set(u.first, *u.second);
    return true;
}

SessionResult runStartedSession(const SessionSteps &s) {
    bool coreEntered = false;
    // Declared before the teardown, which frees it from its first step.
    void *stack = nullptr;
    try {
        SessionTeardown t(s.report);
        // Registered before anything is allocated, so no allocation in
        // between can lose the stack.
        t.set(TeardownStep::FreeStack, [&s, &stack] {
            if (stack) s.freeStack(stack);
            stack = nullptr;
        });
        if (!registerSessionTeardown(t, s))
            return {SessionEnd::PreparationFailed, "the session teardown is incomplete"};
        s.begin();
        stack = s.allocStack();
        if (!stack) {
            const SessionResult r = {SessionEnd::PreparationFailed, "no memory for the core's stack"};
            s.closing(r);
            t.run();
            s.ended(r);
            return r;
        }
        coreEntered = true;
        const CoreRun c = s.runCore(stack);
        const SessionEnd end = classifySessionEnd(true, true, c.started, c.result, c.cancelled);
        const SessionResult r = {end, sessionEndIsError(end) ? c.error : std::string()};
        s.closing(r);
        t.run();
        s.ended(r);
        return r;
    } catch (const std::exception &e) {
        // The teardown has run (t is gone); the error goes to the caller.
        const SessionEnd end = coreEntered ? SessionEnd::CoreFailed : SessionEnd::PreparationFailed;
        if (s.report) s.report(std::string("session: exception: ") + e.what());
        return {end, e.what()};
    } catch (...) {
        const SessionEnd end = coreEntered ? SessionEnd::CoreFailed : SessionEnd::PreparationFailed;
        if (s.report) s.report("session: unknown exception");
        return {end, "unknown exception"};
    }
}

}  // namespace boxer_ui
