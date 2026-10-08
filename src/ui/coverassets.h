// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// Boxer's own cover art templates (GPL-2.0, category (a) in
// assets/boxer-assets-manifest.json), embedded at build time from
// assets/runtime/boxer by tools/png2inc.py into cover_assets.inc:
// CDCase/CDCover, 35Diskette/35DisketteShine, 525Diskette at 128 px, and
// BoxArtShine scaled from 512 to the 128 px icon size.
// No AROS headers: the font-sample tool on the build host uses it too.
#ifndef BOXER_UI_COVERASSETS_H
#define BOXER_UI_COVERASSETS_H

#include "../model/coverart.h"

namespace boxer_ui {

const boxer::BootlegArt &bootlegArt(boxer::ReleaseMedium medium);
const boxer::RGBAImage &boxArtShine();

}  // namespace boxer_ui
#endif
