// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#pragma once

#include "dosbox.h"
#include "keyboard.h"

namespace boxer {
KBD_KEYS dosKeyForRawKey(unsigned code);
}
