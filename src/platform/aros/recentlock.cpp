// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#include "recentlock.h"

#include <exec/memory.h>
#include <exec/semaphores.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <cstring>

namespace boxer {

static const char kSemaphoreName[] = "Boxer recent games";
static struct SignalSemaphore *g_sem;
static bool g_held;

static struct SignalSemaphore *semaphore() {
    if (g_sem) return g_sem;
    Forbid();
    struct SignalSemaphore *s = FindSemaphore((STRPTR)kSemaphoreName);
    if (!s) {
        // One block for the semaphore and its name: it is never freed.
        s = (struct SignalSemaphore *)AllocVec(sizeof(*s) + sizeof kSemaphoreName, MEMF_PUBLIC | MEMF_CLEAR);
        if (s) {
            char *name = (char *)(s + 1);
            std::memcpy(name, kSemaphoreName, sizeof kSemaphoreName);
            s->ss_Link.ln_Name = name;
            s->ss_Link.ln_Pri = 0;
            AddSemaphore(s);
        }
    }
    Permit();
    g_sem = s;
    return s;
}

bool recentLockAttempt(int ticks) {
    struct SignalSemaphore *s = semaphore();
    if (!s) return false;
    for (int waited = 0;; waited += 2) {
        if (AttemptSemaphore(s)) {
            g_held = true;
            return true;
        }
        if (waited >= ticks) return false;
        Delay(2);
    }
}

void recentLockRelease() {
    if (g_held && g_sem) {
        g_held = false;
        ReleaseSemaphore(g_sem);
    }
}

} // namespace boxer
