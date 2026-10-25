// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// AROS side of the recent games file's guard (model/recentgames.h
// RecentGuard).
//
// The guard is a public exec SignalSemaphore named "Boxer recent games",
// found or added under Forbid() by the first BoxerUI that needs it. It is
// held across the whole read-modify-replace of the file, recovery
// included, so two BoxerUI processes never interleave their changes. Why a
// semaphore and not a lock file: it needs no file whose name could be
// mistaken for ownership (a stale lock file after a crash would block, or
// be stolen), it works on any volume ENVARC: is on, and exec ties it to the
// holding task. It is never removed: another process may be about to obtain
// it, and nothing tells when none is; it costs a few dozen bytes once per
// boot. A task that dies while holding it leaves it held until a reboot; the
// others then skip their history changes (logged) instead of waiting.
#pragma once

#include <string>

namespace boxer {

// Tries for at most `ticks` (1/50 s); false when another process holds it.
bool recentLockAttempt(int ticks);
void recentLockRelease();
} // namespace boxer
