// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#include "coverassets.h"

#include <cstring>

#include "cover_assets.inc"

namespace boxer_ui {

static boxer::RGBAImage make(const unsigned char *px, int w, int h)
{
    boxer::RGBAImage img(w, h);
    std::memcpy(img.px.data(), px, (size_t)w * h * 4);
    return img;
}

const boxer::BootlegArt &bootlegArt(boxer::ReleaseMedium medium)
{
    static const boxer::RGBAImage cdCase = make(kAsset_CDCase, kAsset_CDCase_w, kAsset_CDCase_h);
    static const boxer::RGBAImage cdCover = make(kAsset_CDCover, kAsset_CDCover_w, kAsset_CDCover_h);
    static const boxer::RGBAImage d35 = make(kAsset_35Diskette, kAsset_35Diskette_w, kAsset_35Diskette_h);
    static const boxer::RGBAImage d35Shine =
        make(kAsset_35DisketteShine, kAsset_35DisketteShine_w, kAsset_35DisketteShine_h);
    static const boxer::RGBAImage d525 = make(kAsset_525Diskette, kAsset_525Diskette_w, kAsset_525Diskette_h);
    // +baseLayerForSize: / +topLayerForSize: (BXBootlegCoverArt.m): the
    // cover glass and the 3.5" shine at 128 px and up; 5.25" has none.
    static const boxer::BootlegArt jewel{&cdCase, &cdCover}, disk35{&d35, &d35Shine}, disk525{&d525, nullptr};
    switch (medium) {
    case boxer::ReleaseMedium::CDROM: return jewel;
    case boxer::ReleaseMedium::Diskette525: return disk525;
    default: return disk35;
    }
}

const boxer::RGBAImage &boxArtShine()
{
    static const boxer::RGBAImage shine = make(kAsset_BoxArtShine, kAsset_BoxArtShine_w, kAsset_BoxArtShine_h);
    return shine;
}

}  // namespace boxer_ui
