// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// The title font of the bootleg covers: a TrueType file read whole and
// rasterised with stb_truetype (third_party/stb, MIT), so the covers come
// out the same on the build host (tests, font samples) and on both AROS ABIs.
//
// The original draws with Marker Felt Thin (BXBootlegCoverArt.m:15), an Apple
// font that cannot be shipped. Until a free replacement is chosen,
// BoxerUI uses the system's Vera Sans (BoxerSans), read from
// Fonts:TrueType; no font file is shipped.
//
// Text is Latin-1, as AROS file names are: each byte is one code point.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace boxer {

class CoverFont {
public:
	CoverFont();
	~CoverFont();
	CoverFont(const CoverFont &) = delete;
	CoverFont &operator=(const CoverFont &) = delete;

	// Takes a copy of the file's bytes. False when it is not a font
	// stb_truetype can read.
	bool load(const std::string &ttf);
	bool loadFile(const std::string &path);
	bool valid() const;

	// For a size in pixels per em (an AppKit point size at 1x).
	double ascent(double px) const;
	double descent(double px) const;     // positive, below the baseline
	// Advance of c, plus the kerning towards next (0 = none).
	double advance(unsigned char c, unsigned char next, double px) const;
	double width(const std::string &text, double px) const;

	// Adds the glyph's coverage (0..1, clamped) to mask (mw x mh, row-major)
	// with its origin at (x, baseline y); sub-pixel x is kept.
	void drawGlyph(unsigned char c, double px, double x, double y,
	               std::vector<float> &mask, int mw, int mh) const;

private:
	struct Impl;
	std::unique_ptr<Impl> d_;
};

// NSLineBreakByWordWrapping as NSString drawInRect: does it: words are kept
// together while they fit, a word wider than the box is broken between
// characters, runs of spaces between words do not start a line.
std::vector<std::string> wrapTitle(const CoverFont &font, const std::string &text, double px, double width);

} // namespace boxer
