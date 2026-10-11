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

using namespace boxer_ui;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
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

int main() {
    testEnds();
    testInstallerImport();
    testScope();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("session_test: all checks passed\n");
    return 0;
}
