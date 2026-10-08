// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#include "coverfont.h"
#include "fsutil.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace boxer {

struct CoverFont::Impl {
	std::string data;
	stbtt_fontinfo info;
	bool ok = false;
};

CoverFont::CoverFont() : d_(new Impl) {}
CoverFont::~CoverFont() = default;

bool CoverFont::load(const std::string &ttf)
{
	d_->data = ttf;
	d_->ok = false;
	const unsigned char *p = (const unsigned char *)d_->data.data();
	if (d_->data.size() < 12) return false;
	const int offset = stbtt_GetFontOffsetForIndex(p, 0);
	if (offset < 0) return false;
	d_->ok = stbtt_InitFont(&d_->info, p, offset) != 0;
	return d_->ok;
}

bool CoverFont::loadFile(const std::string &path)
{
	std::string bytes;
	return fsutil::readFile(path, bytes) && load(bytes);
}

bool CoverFont::valid() const { return d_->ok; }

double CoverFont::ascent(double px) const
{
	if (!d_->ok) return 0;
	int a, de, g;
	stbtt_GetFontVMetrics(&d_->info, &a, &de, &g);
	return a * stbtt_ScaleForMappingEmToPixels(&d_->info, (float)px);
}

double CoverFont::descent(double px) const
{
	if (!d_->ok) return 0;
	int a, de, g;
	stbtt_GetFontVMetrics(&d_->info, &a, &de, &g);
	return -de * stbtt_ScaleForMappingEmToPixels(&d_->info, (float)px);
}

double CoverFont::advance(unsigned char c, unsigned char next, double px) const
{
	if (!d_->ok) return 0;
	const float scale = stbtt_ScaleForMappingEmToPixels(&d_->info, (float)px);
	int adv, lsb;
	stbtt_GetCodepointHMetrics(&d_->info, c, &adv, &lsb);
	double w = adv * scale;
	if (next) w += stbtt_GetCodepointKernAdvance(&d_->info, c, next) * scale;
	return w;
}

double CoverFont::width(const std::string &text, double px) const
{
	double w = 0;
	for (size_t i = 0; i < text.size(); ++i)
		w += advance((unsigned char)text[i], i + 1 < text.size() ? (unsigned char)text[i + 1] : 0, px);
	return w;
}

void CoverFont::drawGlyph(unsigned char c, double px, double x, double y,
                          std::vector<float> &mask, int mw, int mh) const
{
	if (!d_->ok || c == ' ') return;
	const float scale = stbtt_ScaleForMappingEmToPixels(&d_->info, (float)px);
	const int ix = (int)std::floor(x);
	const float shift = (float)(x - ix);
	const int by = (int)std::lround(y);
	int x0, y0, x1, y1;
	stbtt_GetCodepointBitmapBoxSubpixel(&d_->info, c, scale, scale, shift, 0, &x0, &y0, &x1, &y1);
	const int gw = x1 - x0, gh = y1 - y0;
	if (gw <= 0 || gh <= 0) return;
	std::vector<unsigned char> buf((size_t)gw * gh, 0);
	stbtt_MakeCodepointBitmapSubpixel(&d_->info, buf.data(), gw, gh, gw, scale, scale, shift, 0, c);
	for (int j = 0; j < gh; ++j) {
		const int ty = by + y0 + j;
		if (ty < 0 || ty >= mh) continue;
		for (int i = 0; i < gw; ++i) {
			const int tx = ix + x0 + i;
			if (tx < 0 || tx >= mw) continue;
			float &m = mask[(size_t)ty * mw + tx];
			m = std::min(1.0f, m + buf[(size_t)j * gw + i] / 255.0f);
		}
	}
}

std::vector<std::string> wrapTitle(const CoverFont &font, const std::string &text, double px, double width)
{
	// Words and the spaces before each.
	std::vector<std::string> words;
	{
		std::string w;
		for (char ch : text) {
			if (ch == ' ' || ch == '\t' || ch == '\n') {
				if (!w.empty()) { words.push_back(w); w.clear(); }
			} else {
				w += ch;
			}
		}
		if (!w.empty()) words.push_back(w);
	}
	std::vector<std::string> lines;
	std::string line;
	auto flush = [&]() { if (!line.empty()) { lines.push_back(line); line.clear(); } };
	for (const auto &word : words) {
		const std::string candidate = line.empty() ? word : line + " " + word;
		if (font.width(candidate, px) <= width) { line = candidate; continue; }
		flush();
		if (font.width(word, px) <= width) { line = word; continue; }
		// A word wider than the box: as many characters per line as fit,
		// at least one so the loop always advances.
		std::string piece;
		for (char ch : word) {
			if (!piece.empty() && font.width(piece + ch, px) > width) {
				lines.push_back(piece);
				piece.clear();
			}
			piece += ch;
		}
		line = piece;
	}
	flush();
	return lines;
}

} // namespace boxer
