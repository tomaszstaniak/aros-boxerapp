// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXEmulatedKeyboard, BXEmulatedMouse),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "coreinput.h"
#include "rawkeys.h"

#include "dosbox.h"
#include "keyboard.h"
#include "mouse.h"

#include <devices/inputevent.h>

namespace boxer {

namespace {
bool held[KBD_LAST];
bool capsOn = false, capsKnown = false;
bool buttons[3];
} // namespace

int coreKey(unsigned code, bool down, unsigned qualifier)
{
	// Caps Lock: AROS reports the lock as a qualifier and its key events
	// follow the lock state, not the key (an Amiga keyboard sends "down"
	// when the lock engages and "up" when it releases). DOS expects a
	// press and release per toggle, so the qualifier is the source of truth
	// and the core gets one tap whenever it disagrees with it.
	const bool caps = qualifier & IEQUALIFIER_CAPSLOCK;
	if (!capsKnown) {
		capsKnown = true;
		capsOn = false;
	}
	if (caps != capsOn) {
		KEYBOARD_AddKey(KBD_capslock, true);
		KEYBOARD_AddKey(KBD_capslock, false);
		capsOn = caps;
	}
	const KBD_KEYS key = dosKeyForRawKey(code);
	if (key == KBD_capslock || key == KBD_NONE)
		return key;
	// Auto-repeat arrives as further downs (IEQUALIFIER_REPEAT); DOS sees
	// typematic repeats as repeated make codes too, so they pass.
	if (!down && !held[key])
		return key;   // a release whose press went elsewhere (window was inactive)
	held[key] = down;
	KEYBOARD_AddKey(key, down);
	return key;
}

int coreReleaseAllKeys()
{
	int n = 0;
	for (int k = 0; k < KBD_LAST; ++k)
		if (held[k]) {
			held[k] = false;
			KEYBOARD_AddKey(KBD_KEYS(k), false);
			++n;
		}
	return n;
}

void coreMouseMoved(float fx, float fy, float dx, float dy, bool locked)
{
	Mouse_CursorMoved(dx, dy, fx, fy, locked);
}

void coreMouseButton(int button, bool down)
{
	if (button < 0 || button > 2 || buttons[button] == down)
		return;
	buttons[button] = down;
	if (down)
		Mouse_ButtonPressed(button);
	else
		Mouse_ButtonReleased(button);
}

int coreReleaseAllButtons()
{
	int n = 0;
	for (int b = 0; b < 3; ++b)
		if (buttons[b]) {
			coreMouseButton(b, false);
			++n;
		}
	return n;
}

} // namespace boxer
