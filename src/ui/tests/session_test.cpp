// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXSession.m, BXImportSession.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Host unit test for src/ui/session.{h,cpp}.
#include "../session.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace boxer_ui;
static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void testEnds() {
    CHECK(classifySessionEnd(false, false, false, 0, false) == SessionEnd::PreparationFailed);
    CHECK(classifySessionEnd(true, false, false, 0, false) == SessionEnd::PreparationFailed);  // no stack
    CHECK(classifySessionEnd(true, true, false, 1, false) == SessionEnd::PreparationFailed);   // missing .conf
    CHECK(classifySessionEnd(true, true, true, 1, false) == SessionEnd::CoreFailed);
    CHECK(classifySessionEnd(true, true, true, 1, true) == SessionEnd::CoreFailed);           // error wins
    CHECK(classifySessionEnd(true, true, true, 0, false) == SessionEnd::Normal);
    CHECK(classifySessionEnd(true, true, true, 0, true) == SessionEnd::Cancelled);
    CHECK(!sessionEndIsError(SessionEnd::Normal) && !sessionEndIsError(SessionEnd::Cancelled));
    CHECK(sessionEndIsError(SessionEnd::PreparationFailed) && sessionEndIsError(SessionEnd::CoreFailed));
}

static void testInstallerImport() {
    CHECK(importContinuesAfterInstaller(SessionEnd::Normal, false));
    CHECK(importContinuesAfterInstaller(SessionEnd::Cancelled, false));   // "Finish Importing"
    CHECK(!importContinuesAfterInstaller(SessionEnd::Cancelled, true));   // "Stop Importing"
    CHECK(!importContinuesAfterInstaller(SessionEnd::Normal, true));
    CHECK(!importContinuesAfterInstaller(SessionEnd::PreparationFailed, false));
    CHECK(!importContinuesAfterInstaller(SessionEnd::CoreFailed, false));
}

struct State {
    bool running = false;
    int frames = 0;
};
static State *published = nullptr;
static int *borrowed = nullptr;   // another pointer a session publishes

// Returns early, as runSession() does when preparation fails.
static bool runAndReturnEarly(std::string &log) {
    SessionScope<State> scope(published);
    int local = 7;
    borrowed = &local;
    scope.onExit([&] { log += "a"; borrowed = nullptr; });
    scope.onExit([&] { log += "b"; });
    if (published != &scope.state()) return false;
    return true;   // early: nothing below runs
}

static void testScope() {
    CHECK(published == nullptr);
    {
        SessionScope<State> scope(published);
        CHECK(published == &scope.state());
        CHECK(!published->running && published->frames == 0);   // fresh state
        published->running = true;
        published->frames = 42;
    }
    CHECK(published == nullptr);
    {
        SessionScope<State> scope(published);
        CHECK(!published->running && published->frames == 0);   // nothing left from the last one
    }
    std::string log;
    CHECK(runAndReturnEarly(log));
    CHECK(published == nullptr && borrowed == nullptr);
    CHECK(log == "ba");                                          // reverse order, once
    try {
        SessionScope<State> scope(published);
        scope.onExit([&] { log += "x"; });
        throw std::runtime_error("core");
    } catch (const std::exception &) {
    }
    CHECK(published == nullptr && log == "bax");
    State outer;
    published = &outer;
    {
        SessionScope<State> scope(published);
        CHECK(published != &outer);
    }
    CHECK(published == &outer);
    published = nullptr;
}

// --- SessionTeardown ------------------------------------------------------

static_assert(noexcept(std::declval<SessionTeardown &>().run()), "the teardown must not throw");
static_assert(std::is_nothrow_destructible<SessionTeardown>::value, "the teardown must not throw");

static void testTeardownOrder() {
    std::vector<int> ran;
    std::vector<std::string> reports;
    {
        SessionTeardown t([&](const std::string &r) { reports.push_back(r); });
        // Registered out of order, as runStartedSession() does with FreeStack.
        for (int i : {11, 3, 0, 7, 5, 1, 9, 2, 10, 4, 8, 6})
            t.set(TeardownStep(i), [&ran, i] { ran.push_back(i); });
        t.run();
        CHECK(ran.size() == kTeardownSteps);
        for (int i = 0; i < (int)ran.size(); ++i) CHECK(ran[i] == i);
        t.run();   // once only, also at destruction
    }
    CHECK(ran.size() == kTeardownSteps && reports.empty());

    // A failing step is reported and the rest still run, on destruction too.
    ran.clear();
    {
        SessionTeardown t([&](const std::string &r) { reports.push_back(r); });
        t.set(TeardownStep::CloseWindow, [&] { ran.push_back(11); });
        t.set(TeardownStep::DetachViews, [&] { ran.push_back(5); throw std::runtime_error("views"); });
        t.set(TeardownStep::Unpause, [&] { ran.push_back(2); throw 42; });
    }
    CHECK((ran == std::vector<int>{2, 5, 11}));
    CHECK(reports.size() == 2);
    CHECK(reports.size() == 2 && reports[0].find("\"unpause\"") != std::string::npos &&
          reports[0].find("unknown exception") != std::string::npos);
    CHECK(reports.size() == 2 && reports[1].find("\"detach views\" failed: views") != std::string::npos);

    // A throwing reporter does not escape either.
    bool after = false;
    {
        SessionTeardown t([](const std::string &) { throw std::runtime_error("log full"); });
        t.set(TeardownStep::FreeStack, [] { throw std::runtime_error("x"); });
        t.set(TeardownStep::CloseWindow, [&] { after = true; });
    }
    CHECK(after);
}

// --- runStartedSession: the integration main.cpp uses ------------------------
// A model of runSession() after preparation: an emulator that owns the frame
// buffer the views point into, window and input state that begin() changes,
// and a core that may fail at a chosen point. Every call is recorded.

enum class Fail { None, InBegin, NoStack, CoreThrows, CoreError, Closing, Ended };

struct Desk {
    std::vector<std::string> log;
    // State begin() and the core change; the teardown must put it back.
    bool published = false, windowKeysOff = false, windowOpen = false, running = false;
    bool pointerHidden = false, fullscreen = false, paused = false, panel = false;
    int heldKeys = 0;
    const unsigned *view = nullptr;   // the render view's frame pointer
    void *stack = nullptr;
    int stacksAllocated = 0, stacksFreed = 0;
    bool viewDetachedBeforeOwnerGone = true;
};

// The frame buffer's owner (boxer::Emulator in main.cpp).
struct Owner {
    Desk &d;
    unsigned frame[4] = {};
    explicit Owner(Desk &desk) : d(desk) {}
    ~Owner() {
        if (d.view == frame) d.viewDetachedBeforeOwnerGone = false;
        d.log.push_back("owner gone");
    }
};

static SessionSteps stepsFor(Desk &d, Owner &o, Fail fail, bool cancelled = false) {
    SessionSteps s;
    auto rec = [&d](const char *what) { d.log.push_back(what); };
    s.begin = [&d, fail, rec] {
        rec("begin");
        d.published = true;
        d.running = true;
        d.windowKeysOff = true;
        if (fail == Fail::InBegin) throw std::runtime_error("window would not open");
        d.windowOpen = true;
    };
    s.allocStack = [&d, fail, rec]() -> void * {
        rec("alloc stack");
        if (fail == Fail::NoStack) return nullptr;
        ++d.stacksAllocated;
        d.stack = &d.stacksAllocated;
        return d.stack;
    };
    s.freeStack = [&d, rec](void *p) {
        rec("free stack");
        if (p == d.stack) ++d.stacksFreed;
    };
    s.runCore = [&d, &o, fail, cancelled, rec](void *stack) {
        rec("core");
        CHECK(stack == d.stack);   // runs on the stack it was given
        // Everything is set up: frame shown, pointer locked, keys held,
        // full screen, paused, launch panel shown.
        d.view = o.frame;
        d.pointerHidden = true;
        d.heldKeys = 3;
        d.fullscreen = true;
        d.paused = true;
        d.panel = true;
        if (fail == Fail::CoreThrows) throw std::runtime_error("injected after set-up");
        CoreRun c;
        c.started = true;
        c.cancelled = cancelled;
        c.result = fail == Fail::CoreError ? 1 : 0;
        c.error = fail == Fail::CoreError ? "E_Exit" : "";
        return c;
    };
    s.closing = [fail, rec](const SessionResult &) {
        rec("closing");
        if (fail == Fail::Closing) throw std::runtime_error("settings");
    };
    s.ended = [fail, rec](const SessionResult &) {
        rec("ended");
        if (fail == Fail::Ended) throw std::runtime_error("log");
    };
    // The undo actions, each harmless when its change was not made.
    s.releaseInput = [&d, rec] { rec("release input"); if (d.running) d.heldKeys = 0; };
    s.unpause = [&d, rec] { rec("unpause"); d.paused = false; };
    s.hideLaunchPanel = [&d, rec] { rec("hide launch panel"); d.panel = false; };
    s.stopSession = [&d, rec] { rec("stop session"); d.running = false; };
    s.detachViews = [&d, rec] { rec("detach views"); d.view = nullptr; };
    s.restorePointer = [&d, rec] {
        rec("restore pointer");
        // On the window that has it: still full screen here.
        if (d.pointerHidden && !d.fullscreen) d.log.push_back("pointer restored on the wrong window");
        d.pointerHidden = false;
    };
    s.resetMouse = [rec] { rec("reset mouse"); };
    s.unpublishCore = [&d, rec] { rec("unpublish core"); d.published = false; };
    s.leaveFullscreen = [&d, rec] { rec("leave full screen"); d.fullscreen = false; };
    s.enableWindowKeys = [&d, rec] { rec("enable window keys"); d.windowKeysOff = false; };
    s.closeWindow = [&d, rec] { rec("close window"); d.windowOpen = false; };
    s.report = [&d](const std::string &r) { d.log.push_back("report: " + r); };
    return s;
}

// runSession() in miniature: the owner lives in the caller, the session runs
// inside its lifetime.
static SessionResult runModel(Desk &d, Fail fail, bool cancelled = false) {
    Owner owner(d);
    SessionSteps s = stepsFor(d, owner, fail, cancelled);
    return runStartedSession(s);
}

static const std::vector<std::string> kTeardown = {
    "free stack", "release input", "unpause", "hide launch panel", "stop session", "detach views",
    "restore pointer", "reset mouse", "unpublish core", "leave full screen", "enable window keys",
    "close window"};

static std::vector<std::string> seq(std::initializer_list<std::vector<std::string>> parts) {
    std::vector<std::string> v;
    for (const auto &p : parts) v.insert(v.end(), p.begin(), p.end());
    return v;
}

static void checkRestored(const Desk &d) {
    CHECK(!d.published && !d.windowKeysOff && !d.windowOpen && !d.running);
    CHECK(!d.pointerHidden && !d.fullscreen && !d.paused && !d.panel && d.heldKeys == 0);
    CHECK(d.view == nullptr && d.viewDetachedBeforeOwnerGone);
    CHECK(d.stacksFreed == d.stacksAllocated);
}

static void testStartedSession() {
    {   // A failure injected after the stack, views, pointer and input are set up.
        Desk d;
        SessionResult r = runModel(d, Fail::CoreThrows);
        CHECK(r.end == SessionEnd::CoreFailed && r.error == "injected after set-up");
        CHECK(d.log == seq({{"begin", "alloc stack", "core"}, kTeardown,
                            {"report: session: exception: injected after set-up", "owner gone"}}));
        checkRestored(d);
        CHECK(d.stacksAllocated == 1);
    }
    {   // The core's own error.
        Desk d;
        SessionResult r = runModel(d, Fail::CoreError);
        CHECK(r.end == SessionEnd::CoreFailed && r.error == "E_Exit");
        CHECK(d.log == seq({{"begin", "alloc stack", "core", "closing"}, kTeardown, {"ended", "owner gone"}}));
        checkRestored(d);
    }
    {   // Normal end and cancel: the same teardown, between closing and ended.
        Desk d;
        SessionResult r = runModel(d, Fail::None);
        CHECK(r.end == SessionEnd::Normal && r.error.empty());
        CHECK(d.log == seq({{"begin", "alloc stack", "core", "closing"}, kTeardown, {"ended", "owner gone"}}));
        checkRestored(d);
        Desk c;
        CHECK(runModel(c, Fail::None, true).end == SessionEnd::Cancelled);
        checkRestored(c);
    }
    {   // Exceptions after the core: still an error, teardown complete.
        Desk d;
        SessionResult r = runModel(d, Fail::Closing);
        CHECK(r.end == SessionEnd::CoreFailed && r.error == "settings");
        CHECK(d.log == seq({{"begin", "alloc stack", "core", "closing"}, kTeardown,
                            {"report: session: exception: settings", "owner gone"}}));
        checkRestored(d);
        Desk e;
        r = runModel(e, Fail::Ended);
        CHECK(r.end == SessionEnd::CoreFailed && r.error == "log");
        checkRestored(e);
    }
    {   // begin() fails half way: preparation failed, its changes undone.
        Desk d;
        SessionResult r = runModel(d, Fail::InBegin);
        CHECK(r.end == SessionEnd::PreparationFailed && r.error == "window would not open");
        std::vector<std::string> undo(kTeardown.begin() + 1, kTeardown.end());   // no stack to free
        CHECK(d.log == seq({{"begin"}, undo, {"report: session: exception: window would not open", "owner gone"}}));
        checkRestored(d);
        CHECK(d.stacksAllocated == 0);
    }
    {   // No memory for the stack.
        Desk d;
        SessionResult r = runModel(d, Fail::NoStack);
        CHECK(r.end == SessionEnd::PreparationFailed && r.error == "no memory for the core's stack");
        std::vector<std::string> undo(kTeardown.begin() + 1, kTeardown.end());
        CHECK(d.log == seq({{"begin", "alloc stack", "closing"}, undo, {"ended", "owner gone"}}));
        checkRestored(d);
    }
    {   // A missing undo action: nothing starts, and the report names it.
        Desk d;
        Owner o(d);
        SessionSteps s = stepsFor(d, o, Fail::None);
        s.restorePointer = nullptr;
        SessionResult r = runStartedSession(s);
        CHECK(r.end == SessionEnd::PreparationFailed);
        CHECK((d.log == std::vector<std::string>{"report: session teardown: no action for \"restore pointer\""}));
        CHECK(!d.published && d.stacksAllocated == 0);
    }
}

// The return code: errors of preparation and of the core are not 0, a
// normal end and a cancel are; across several sessions the worst stays.
static void testExitCode() {
    CHECK(sessionExitCode(SessionEnd::Normal) == 0);
    CHECK(sessionExitCode(SessionEnd::Cancelled) == 0);
    CHECK(sessionExitCode(SessionEnd::PreparationFailed) == kExitError);
    CHECK(sessionExitCode(SessionEnd::CoreFailed) == kExitError);
    CHECK(kExitError == 10);   // RETURN_ERROR: "If ERROR" and "If WARN" both see it
    ExitStatus x;
    CHECK(x.code() == 0);
    x.note(SessionEnd::Normal);
    x.note(SessionEnd::Cancelled);
    CHECK(x.code() == 0);
    x.note(SessionEnd::PreparationFailed);
    CHECK(x.code() == kExitError);
    x.note(SessionEnd::Normal);   // a later good session does not hide it
    CHECK(x.code() == kExitError);
    ExitStatus y;
    y.note(SessionEnd::CoreFailed);
    y.note(SessionEnd::Cancelled);
    CHECK(y.code() == kExitError);
    ExitStatus z;
    z.noteError();
    CHECK(z.code() == kExitError);
    // An installer session ends the same way: its failure is an error too.
    for (SessionEnd e : {SessionEnd::Normal, SessionEnd::Cancelled, SessionEnd::PreparationFailed, SessionEnd::CoreFailed})
        CHECK((sessionExitCode(e) != 0) == sessionEndIsError(e));
}

int main() {
    testEnds();
    testInstallerImport();
    testScope();
    testTeardownOrder();
    testStartedSession();
    testExitCode();
    if (failures) { std::printf("%d of %d checks failed\n", failures, checks); return 1; }
    std::printf("session_test: all %d checks passed\n", checks);
    return 0;
}
