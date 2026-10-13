// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// A window as a Workbench AppWindow: icons dropped on it arrive as
// AppMessages on a port of this program.
//
// Wanderer sends a drop with SendAppWindowMessage, which holds the
// Workbench semaphore shared and waits for the reply, so every message is
// answered as soon as its paths are copied (take), and nothing from it
// (names, locks) is kept past the reply. RemoveAppWindow takes the same
// semaphore exclusively: a drop that arrives after this task's last reply
// and before RemoveAppWindow would leave both waiting for each other.
// So another process answers the drops while this task waits in
// RemoveAppWindow. That process is started before the window is
// registered and waits, parked, until detach() needs it: a window whose
// answerer cannot be started is not registered (ui/drop_logic.h
// registerDropTarget), and detach() never has to start a process.
#pragma once

#include <exec/types.h>

#include <string>
#include <vector>

struct Window;
struct MsgPort;

namespace boxer {

struct AppDrop {
    struct Arg {
        std::string path;   // full path; empty when it could not be named
        bool named = false; // the WBArg had a name (a file or a lone icon)
    };
    std::vector<Arg> args;   // at most kMaxArgs, in the message's order
    size_t count = 0;        // objects in the message (may exceed args.size())
    int mouseX = 0, mouseY = 0;   // relative to the window
    static const size_t kMaxArgs = 8;
};

class AppDropTarget {
public:
    AppDropTarget() = default;
    ~AppDropTarget();   // close()
    AppDropTarget(const AppDropTarget &) = delete;
    AppDropTarget &operator=(const AppDropTarget &) = delete;

    // The port (and workbench.library) for the program's life, so its
    // signal can be waited on before any window is open. False: no drops.
    bool create();
    ULONG signalMask() const;
    // detach(), then the port goes (anything left on it answered first).
    void close();

    // The three steps of a registration, in registerDropTarget's order.
    // startResponder: the parked answering process (false: none could be
    // created). addAppWindow: registers the open Intuition window; only
    // with a parked responder. endResponder: ends a parked responder that
    // is not needed (AddAppWindow failed). A target already attached is
    // detached by startResponder first.
    bool startResponder();
    bool addAppWindow(struct Window *window);
    void endResponder();
    // Unregisters (see appdrop.cpp for the order). Drops that were waiting
    // and not taken are answered unread; their count is returned. Safe
    // when not attached.
    unsigned detach();
    struct Window *window() const { return window_; }

    // One waiting message: its paths copied into `out`, then replied.
    // True when a message was taken (also one that is not a drop, which
    // gives an AppDrop with count 0).
    bool take(AppDrop &out);

private:
    struct MsgPort *port_ = nullptr;
    void *appWindow_ = nullptr;
    struct Window *window_ = nullptr;
    ULONG responder_ = 0;            // the parked answerer's task id, while attached
    struct Task *responderTask_ = nullptr;
    // Read by the answering process.
    volatile int stop_ = 0;
    volatile ULONG answered_ = 0;
    static void answer();
};

}  // namespace boxer
