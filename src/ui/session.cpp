// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer/BXSession.m, BXImportSession.m),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "session.h"

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

}  // namespace boxer_ui
