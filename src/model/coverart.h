// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXCoverArt.m, BXBootlegCoverArt.m, BXGameProfile.m,
// BXSession.m), https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Gamebox cover art: the three bootleg covers (BXBootlegCoverArt.m), a
// user's picture made into box art (BXCoverArt.m), the release-medium guess
// that picks a bootleg cover (BXGameProfile.m:77), and where a gamebox keeps
// its choice.
//
// AROS: composed in software on RGBA buffers instead of Core Graphics; the
// icon is 128 px and only that size is rendered. The cover's
// source is kept in the gamebox so it can be rendered again at another size
// later: the user's picture as a file, a bootleg cover as its template
// name in Game Info.plist (the title is the gamebox name).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace boxer {

class Gamebox;
class CoverFont;

// Straight (not premultiplied) 8-bit RGBA, rows top to bottom.
struct RGBAImage {
	int w = 0, h = 0;
	std::vector<uint8_t> px;
	RGBAImage() = default;
	RGBAImage(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0) {}
	bool empty() const { return w <= 0 || h <= 0; }
	uint8_t *at(int x, int y) { return &px[((size_t)y * w + x) * 4]; }
	const uint8_t *at(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
};

// Area-average resample (alpha-weighted, so transparent pixels do not darken
// edges); what NSImageInterpolationHigh gives for a downscale.
RGBAImage scaleImage(const RGBAImage &src, int w, int h);

// BXReleaseMedium.
enum class ReleaseMedium { Unknown, Diskette525, Diskette35, CDROM };
const char *mediumName(ReleaseMedium m);        // "525", "35", "cdrom", ""
ReleaseMedium mediumFromName(const std::string &name);

// +[BXGameProfile mediumOfGameAtURL:] (BXGameProfile.m:77-125): walks the
// tree (hidden entries skipped); the first entry dated after 1981 and before
// 1988 makes it a 5.25" game, before 1994 a 3.5" one; once the files add up
// to more than 30 MiB it is a CD game; otherwise 3.5".
// AROS: file systems keep a modification date, not a creation date (the
// original reads NSURLCreationDateKey), and entries are visited sorted by
// name instead of in directory order.
ReleaseMedium mediumOfGameAt(const std::string &path);

// One bootleg template (BXBootlegCoverArt.m): the base image, the optional
// cover glass drawn over the title, and the title's box and type at 128 px.
// Region coordinates are top-left based (the original's are bottom-left).
struct BootlegStyle {
	ReleaseMedium medium;
	double regionX, regionY, regionW, regionH;
	double fontSize, lineHeight;
};
// BXJewelCase (:12-80) for CDROM, BX525Diskette (:203-221) for 5.25",
// BX35Diskette (:169-201) for everything else, as +bootlegCoverArtForGamebox:
// (BXSession.m:193-198) chooses.
BootlegStyle bootlegStyle(ReleaseMedium medium);

struct BootlegArt {
	const RGBAImage *base = nullptr;   // CDCase / 35Diskette / 525Diskette at 128
	const RGBAImage *top = nullptr;    // CDCover / 35DisketteShine; none for 5.25"
};

// -[BXJewelCase drawInRect:] at 128x128: base, the title centred and
// word-wrapped in the template's box in the text colour (0, 0.1, 0.2, 0.9),
// lines that do not fit the box left out, then the top layer.
RGBAImage renderBootleg(ReleaseMedium medium, const std::string &title, const BootlegArt &art,
                        const CoverFont &font);

// +[BXCoverArt imageHasTransparency:] (BXCoverArt.m:170-205): any of the four
// corners or the centre less than 90% opaque.
bool imageHasTransparency(const RGBAImage &image);

// -[BXCoverArt drawInRect:] (BXCoverArt.m:58-126) at size x size: the picture
// fitted into the box left after the drop shadow (centred, standing on the
// shadow's baseline), the drop shadow (blur size/32, 1 px down, black 85%),
// an inner white glow (blur size/64, 33%), BoxArtShine at 25% and a 1 px
// black 33% outline. A picture with transparency is used as it is
// (BXCoverArt.m:151), only fitted into the square.
RGBAImage renderCoverArt(const RGBAImage &picture, const RGBAImage &shine, int size = 128);

// PNG file (8-bit RGBA, stored deflate blocks: no compressor needed).
std::string encodePNG(const RGBAImage &image);

// What the gamebox's cover is, kept in Game Info.plist under
// "AROSCoverArt" (an addition; the original kept only the finished icon,
// as the bundle's Finder icon, BXGamebox.m:378-390):
//   Style   "automatic" | "cdrom" | "35" | "525" | "picture"
//   Medium  the medium "automatic" detected, so a later re-render does not
//           depend on the source folder still being there
//   Picture file name of the user's picture inside the gamebox
enum class CoverStyle { Automatic, JewelCase, Diskette35, Diskette525, Picture };
struct CoverChoice {
	CoverStyle style = CoverStyle::Automatic;
	ReleaseMedium detected = ReleaseMedium::Unknown;
	std::string picture;
	// The medium a bootleg render uses: the explicit template, else the
	// detected one, else 3.5" (BX35Diskette is the original's default).
	ReleaseMedium bootlegMedium() const;
	bool isBootleg() const { return style != CoverStyle::Picture; }
};
const char *coverStyleName(CoverStyle s);
CoverChoice readCoverChoice(const Gamebox &box);
void setCoverChoice(Gamebox &box, const CoverChoice &choice);   // caller saves

// The user's picture is copied into the gamebox as "Cover Art.<ext>" (the
// source's extension, lower case; none when it has none).
std::string coverPictureName(const std::string &sourcePath);

// Copies the picture into the gamebox and records it. An earlier picture
// of another name is removed only after the new one and the record are
// written. On failure the gamebox keeps its previous cover record.
bool storeCoverPicture(Gamebox &box, const std::string &sourcePath, std::string *error = nullptr);

} // namespace boxer
