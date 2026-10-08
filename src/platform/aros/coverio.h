// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// The AROS side of gamebox covers: reading the user's picture through
// picture.datatype, and writing the sidecar icon with the cover as its
// image through the running system's icon.library.
//
// Icon format: a PNG icon. icon.library reads a PNG .info with its ARGB
// image (diskobjPNGio.c) on both ABIs, and writes a PNG icon it has read back
// as PNG with its own "icOn" chunk of ToolTypes and default tool
// (WriteIconPNG). The OS 3.5 ARGB chunk is not used: its header is read
// differently by the two ABIs' icon.library (diskobj35io.c ReadARGB35), and
// in both source trees WriteIcon35 passes the address of the image pointer,
// not the pixels, to compress() (diskobj35io.c, WriteARGB35 calls), so an
// ARGB image set with IconControl would be written as garbage.
#pragma once

#include "../../model/coverart.h"

#include <string>

namespace boxer {

// Decodes any picture the installed datatypes read (PNG, JPEG, ILBM...) to
// RGBA. Alpha is kept only for pictures that have it (mskHasAlpha); others
// are opaque. Refuses pictures over 4096 px on a side.
bool loadPicture(const std::string &path, RGBAImage &out, std::string *error = nullptr);

// Writes "<iconStem>.info": a project icon with default tool Boxer:BoxerUI,
// ToolTypes BOXERID=<identifier> and GAMEBOX=<gameboxFileName>, no
// fixed position, and image as its picture. The bytes are produced by
// icon.library in T: and then put in place:
// - replaceOwn false: only when no "<iconStem>.info" exists (never written
//   over, rules of 2d96121);
// - replaceOwn true: an existing icon is replaced only when it is this
//   gamebox's own sidecar (its BOXERID is identifier), through
//   fsutil::replaceFile so a failed write keeps the old icon.
// Wanderer is told about the new or changed icon (UpdateWorkbench).
bool writeSidecarIcon(const std::string &iconStem, const RGBAImage &image, const std::string &identifier,
                      const std::string &gameboxFileName, bool replaceOwn, std::string *error = nullptr);

// BOXERID and GAMEBOX of an icon "<iconStem>.info"; false without an icon.
bool readSidecarToolTypes(const std::string &iconStem, std::string &boxerId, std::string &gameboxName);

} // namespace boxer
