// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXEmulatedKeyboard, BXEmulatedMouse),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Input from the AROS session window to the embedded core, without DOSBox
// headers in the caller (they need gnu++14; the UI is C++17).
//
// The roles follow the original's BXEmulatedKeyboard / BXEmulatedMouse: keys
// arrive as AROS raw key codes and are translated by rawkeys.cpp; the mouse
// gets BXEmulatedMouse movedTo:by:onCanvas:whileLocked: semantics.
#pragma once

namespace boxer {

// code without IECODE_UP_PREFIX; qualifier is the IntuiMessage's.
// Returns the DOSBox key number (0 = not mapped) for logging.
int coreKey(unsigned code, bool down, unsigned qualifier);
// Releases every key the core currently sees as held (window deactivated,
// session ending), as the original does when the window resigns key status.
// Returns how many were held.
int coreReleaseAllKeys();

// BXEmulatedMouse movedTo:by:onCanvas:whileLocked: — point is the absolute
// position as a fraction of the canvas (0..1), delta the movement in canvas
// pixels; locked becomes DOSBox's "emulate" (relative motion only).
void coreMouseMoved(float fx, float fy, float dx, float dy, bool locked);
// button 0 left, 1 right, 2 middle.
void coreMouseButton(int button, bool down);
int coreReleaseAllButtons();   // returns how many were held

} // namespace boxer
