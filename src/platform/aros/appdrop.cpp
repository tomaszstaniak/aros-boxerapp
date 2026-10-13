// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#include "appdrop.h"

#include <dos/dos.h>
#include <dos/dostags.h>
#include <exec/tasks.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>

// A base of its own: main.cpp opens workbench.library only around single
// calls (openInWanderer) and puts its global back afterwards.
static struct Library *g_dropWB;
static int g_dropWBUsers;
#define __WORKBENCH_LIBBASE g_dropWB
#include <proto/workbench.h>

namespace boxer {

AppDropTarget::~AppDropTarget() { close(); }

void AppDropTarget::close() {
    detach();
    if (port_) {
        // Detached: no sender can reach the port any more; anything left
        // is answered before it goes.
        struct Message *m;
        while ((m = GetMsg(port_))) ReplyMsg(m);
        DeleteMsgPort(port_);
        port_ = nullptr;
        // The targets share one opening; the last one closes it.
        if (--g_dropWBUsers == 0) {
            CloseLibrary(g_dropWB);
            g_dropWB = nullptr;
        }
    }
}

bool AppDropTarget::create() {
    if (port_) return true;
    if (!g_dropWB && !(g_dropWB = OpenLibrary((CONST_STRPTR) "workbench.library", 0))) return false;
    if (!(port_ = CreateMsgPort())) {
        if (g_dropWBUsers == 0) {
            CloseLibrary(g_dropWB);
            g_dropWB = nullptr;
        }
        return false;
    }
    ++g_dropWBUsers;
    return true;
}

ULONG AppDropTarget::signalMask() const { return port_ ? 1UL << port_->mp_SigBit : 0; }

bool AppDropTarget::startResponder() {
    if (!port_) return false;
    if (appWindow_ || responder_) detach();
    stop_ = 0;
    answered_ = 0;
    struct Process *p = nullptr;
    for (int tries = 0; !p && tries < 3; ++tries) {
        // No home or current directory: a helper must not keep the
        // program's drawer in use.
        p = CreateNewProcTags(NP_Entry, (IPTR)&AppDropTarget::answer, NP_UserData, (IPTR)this,
                              NP_Name, (IPTR) "BoxerUI drop answerer", NP_StackSize, 16384,
                              NP_NotifyOnDeath, TRUE, NP_Input, 0, NP_Output, 0, NP_Error, 0,
                              NP_WindowPtr, (IPTR)-1, NP_CurrentDir, 0, NP_HomeDir, 0, TAG_DONE);
        if (!p) Delay(5);
    }
    if (!p) return false;
    responder_ = GetETaskID(&p->pr_Task);
    responderTask_ = &p->pr_Task;
    return true;
}

bool AppDropTarget::addAppWindow(struct Window *window) {
    if (!port_ || !window || !responder_ || appWindow_) return false;
    appWindow_ = AddAppWindowA(0, 0, window, port_, nullptr);
    window_ = appWindow_ ? window : nullptr;
    return appWindow_ != nullptr;
}

// The responder leaves its wait (it has not touched the port), finds stop
// set, drains once and exits; its exit, not a flag, is waited for.
void AppDropTarget::endResponder() {
    if (!responder_) return;
    stop_ = 1;
    Signal(responderTask_, SIGBREAKF_CTRL_F);
    while (ChildStatus(responder_) != CHILD_EXITED) Delay(1);
    ChildFree(responder_);
    responder_ = 0;
    responderTask_ = nullptr;
}

// The object's full path: the lock names the drawer it is in, or, for a
// drawer or a volume, the object itself (with an empty name).
static AppDrop::Arg argPath(const struct WBArg &a) {
    AppDrop::Arg out;
    char buf[1024];
    out.named = a.wa_Name && *a.wa_Name;
    if (!a.wa_Lock || !NameFromLock(a.wa_Lock, (STRPTR)buf, sizeof buf)) return out;
    if (out.named && !AddPart((STRPTR)buf, (CONST_STRPTR)a.wa_Name, sizeof buf)) return out;
    out.path = buf;
    return out;
}

bool AppDropTarget::take(AppDrop &out) {
    struct AppMessage *m;
    if (!port_ || !(m = (struct AppMessage *)GetMsg(port_))) return false;
    out = AppDrop();
    // Everything is copied before the reply: the sender unlocks the locks
    // and frees the names once it has its answer.
    try {
        if (m->am_Type == AMTYPE_APPWINDOW && m->am_NumArgs > 0 && m->am_ArgList) {
            out.count = (size_t)m->am_NumArgs;
            out.mouseX = m->am_MouseX;
            out.mouseY = m->am_MouseY;
            for (size_t i = 0; i < out.count && i < AppDrop::kMaxArgs; ++i) out.args.push_back(argPath(m->am_ArgList[i]));
        }
    } catch (...) {
        // No memory for the copy: the drop is answered unread.
        out = AppDrop();
    }
    ReplyMsg((struct Message *)m);
    return true;
}

// The answering process: parked from the registration on, then replies to
// every drop while the task that owns the port waits in RemoveAppWindow.
// GetMsg and ReplyMsg from another task are exec's ordinary contract.
// stop_ is read before each drain, so a drop that came before it was set
// is still answered.
void AppDropTarget::answer() {
    AppDropTarget *t = (AppDropTarget *)FindTask(nullptr)->tc_UserData;
    struct Message *m;
    int last;
    Wait(SIGBREAKF_CTRL_F);   // parked until detach (or endResponder) wakes it
    do {
        last = t->stop_;
        while ((m = GetMsg(t->port_))) {
            t->answered_ = t->answered_ + 1;
            ReplyMsg(m);
        }
        if (!last) Delay(1);
    } while (!last);
}

// The order is what makes a close safe with a drop in flight:
// 1. Messages already queued on the port are answered (unread): their
//    senders wait holding the Workbench semaphore shared.
// 2. The parked responder (started at registration, so it exists) is woken;
//    from now on a drop that arrives is answered by it, not by this task.
// 3. RemoveAppWindow: it takes the semaphore exclusively, so it returns
//    only after every sender has its reply and let go; after it no sender
//    can find the window, and Workbench keeps no pointer to it.
// 4. The responder is told to stop, drains once more and exits; its exit
//    (ChildStatus) is waited for, so it never touches a port that is gone.
// 5. Anything still on the port is answered. Only then may the caller
//    close the Intuition window. Nothing taken from a message is a pointer
//    into it: take() copies the paths before replying.
unsigned AppDropTarget::detach() {
    if (!appWindow_) {
        endResponder();   // parked for a registration that did not happen
        return 0;
    }
    struct Message *m;
    while ((m = GetMsg(port_))) {
        answered_ = answered_ + 1;
        ReplyMsg(m);
    }
    Signal(responderTask_, SIGBREAKF_CTRL_F);
    RemoveAppWindow((struct AppWindow *)appWindow_);
    appWindow_ = nullptr;
    window_ = nullptr;
    stop_ = 1;
    while (ChildStatus(responder_) != CHILD_EXITED) Delay(1);
    ChildFree(responder_);
    responder_ = 0;
    responderTask_ = nullptr;
    while ((m = GetMsg(port_))) {
        answered_ = answered_ + 1;
        ReplyMsg(m);
    }
    const unsigned n = answered_;
    answered_ = 0;
    return n;
}

}  // namespace boxer
