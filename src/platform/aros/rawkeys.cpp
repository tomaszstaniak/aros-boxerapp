// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// AROS raw key codes (IECODE values without IECODE_UP_PREFIX) to DOSBox
// keys. Codes above 0x67 and the PC-only keys (Insert, Home, Page Up/Down,
// End, F11/F12, Print Screen, Pause, Num/Scroll Lock) follow the AROS PC
// keyboard driver's assignments as remembered, not yet checked: the smoke
// program logs every raw code it receives so each ABI can be verified.
#include "rawkeys.h"

namespace boxer {

KBD_KEYS dosKeyForRawKey(unsigned code)
{
	static const KBD_KEYS main[0x68] = {
		/* 00 */ KBD_grave, KBD_1, KBD_2, KBD_3, KBD_4, KBD_5, KBD_6, KBD_7,
		/* 08 */ KBD_8, KBD_9, KBD_0, KBD_minus, KBD_equals, KBD_backslash, KBD_NONE, KBD_kp0,
		/* 10 */ KBD_q, KBD_w, KBD_e, KBD_r, KBD_t, KBD_y, KBD_u, KBD_i,
		/* 18 */ KBD_o, KBD_p, KBD_leftbracket, KBD_rightbracket, KBD_NONE, KBD_kp1, KBD_kp2, KBD_kp3,
		/* 20 */ KBD_a, KBD_s, KBD_d, KBD_f, KBD_g, KBD_h, KBD_j, KBD_k,
		/* 28 */ KBD_l, KBD_semicolon, KBD_quote, KBD_backslash, KBD_NONE, KBD_kp4, KBD_kp5, KBD_kp6,
		/* 30 */ KBD_extra_lt_gt, KBD_z, KBD_x, KBD_c, KBD_v, KBD_b, KBD_n, KBD_m,
		/* 38 */ KBD_comma, KBD_period, KBD_slash, KBD_NONE, KBD_kpperiod, KBD_kp7, KBD_kp8, KBD_kp9,
		/* 40 */ KBD_space, KBD_backspace, KBD_tab, KBD_kpenter, KBD_enter, KBD_esc, KBD_delete, KBD_insert,
		/* 48 */ KBD_pageup, KBD_pagedown, KBD_kpminus, KBD_f11, KBD_up, KBD_down, KBD_right, KBD_left,
		/* 50 */ KBD_f1, KBD_f2, KBD_f3, KBD_f4, KBD_f5, KBD_f6, KBD_f7, KBD_f8,
		/* 58 */ KBD_f9, KBD_f10, KBD_numlock, KBD_scrolllock, KBD_kpdivide, KBD_kpmultiply, KBD_kpplus, KBD_NONE,
		/* 60 */ KBD_leftshift, KBD_rightshift, KBD_capslock, KBD_leftctrl, KBD_leftalt, KBD_rightalt, KBD_NONE, KBD_NONE,
	};
	if (code < 0x68)
		return main[code];
	switch (code) {
	case 0x6D: return KBD_printscreen;
	case 0x6E: return KBD_pause;
	case 0x6F: return KBD_f12;
	case 0x70: return KBD_home;
	case 0x71: return KBD_end;
	default: return KBD_NONE;
	}
}

} // namespace boxer
