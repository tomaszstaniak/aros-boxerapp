// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (BXCoverArt.m, BXBootlegCoverArt.m, BXGameProfile.m,
// BXSession.m), https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "coverart.h"
#include "coverfont.h"
#include "fsutil.h"
#include "gamebox.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>

namespace boxer {

// ------------------------------------------------------------- canvas ---

namespace {

// Premultiplied float RGBA, the working surface of every composition.
struct Canvas {
	int w, h;
	std::vector<float> p;
	Canvas(int w_, int h_) : w(w_), h(h_), p((size_t)w_ * h_ * 4, 0.0f) {}
	float *at(int x, int y) { return &p[((size_t)y * w + x) * 4]; }
	// Source-over of a straight colour (0..1) with coverage a.
	void over(int x, int y, float r, float g, float b, float a)
	{
		if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
		float *d = at(x, y);
		const float k = 1 - a;
		d[0] = r * a + d[0] * k; d[1] = g * a + d[1] * k; d[2] = b * a + d[2] * k; d[3] = a + d[3] * k;
	}
	void drawImage(const RGBAImage &img, int ox, int oy, float alpha = 1.0f)
	{
		for (int y = 0; y < img.h; ++y)
			for (int x = 0; x < img.w; ++x) {
				const uint8_t *s = img.at(x, y);
				over(ox + x, oy + y, s[0] / 255.0f, s[1] / 255.0f, s[2] / 255.0f, s[3] / 255.0f * alpha);
			}
	}
	RGBAImage image() const
	{
		RGBAImage out(w, h);
		for (size_t i = 0; i < (size_t)w * h; ++i) {
			const float a = p[i * 4 + 3];
			uint8_t *o = &out.px[i * 4];
			for (int c = 0; c < 3; ++c) {
				const float v = a > 0 ? p[i * 4 + c] / a : 0.0f;
				o[c] = (uint8_t)std::lround(std::min(1.0f, std::max(0.0f, v)) * 255);
			}
			o[3] = (uint8_t)std::lround(std::min(1.0f, std::max(0.0f, a)) * 255);
		}
		return out;
	}
};

// Separable Gaussian blur of a coverage mask. NSShadow's blur radius is
// taken as two standard deviations, which is how Core Graphics draws it.
std::vector<float> blur(const std::vector<float> &m, int w, int h, double radius)
{
	if (radius <= 0) return m;
	const double sigma = radius / 2.0;
	const int r = (int)std::ceil(sigma * 3);
	std::vector<double> k(2 * r + 1);
	double sum = 0;
	for (int i = -r; i <= r; ++i) sum += k[i + r] = std::exp(-(i * i) / (2 * sigma * sigma));
	for (auto &v : k) v /= sum;
	std::vector<float> tmp(m.size(), 0.0f), out(m.size(), 0.0f);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			double a = 0;
			for (int i = -r; i <= r; ++i) {
				const int sx = x + i;
				if (sx >= 0 && sx < w) a += m[(size_t)y * w + sx] * k[i + r];
			}
			tmp[(size_t)y * w + x] = (float)a;
		}
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			double a = 0;
			for (int i = -r; i <= r; ++i) {
				const int sy = y + i;
				if (sy >= 0 && sy < h) a += tmp[(size_t)sy * w + x] * k[i + r];
			}
			out[(size_t)y * w + x] = (float)a;
		}
	return out;
}

} // namespace

RGBAImage scaleImage(const RGBAImage &src, int w, int h)
{
	RGBAImage out(w, h);
	if (src.empty() || w <= 0 || h <= 0) return out;
	const double fx = (double)src.w / w, fy = (double)src.h / h;
	for (int oy = 0; oy < h; ++oy) {
		const double y0 = oy * fy, y1 = (oy + 1) * fy;
		for (int ox = 0; ox < w; ++ox) {
			const double x0 = ox * fx, x1 = (ox + 1) * fx;
			double acc[4] = {0, 0, 0, 0}, total = 0;
			for (int sy = (int)y0; sy < std::min(src.h, (int)std::ceil(y1)); ++sy) {
				const double cy = std::min(y1, sy + 1.0) - std::max(y0, (double)sy);
				if (cy <= 0) continue;
				for (int sx = (int)x0; sx < std::min(src.w, (int)std::ceil(x1)); ++sx) {
					const double f = (std::min(x1, sx + 1.0) - std::max(x0, (double)sx)) * cy;
					if (f <= 0) continue;
					const uint8_t *s = src.at(sx, sy);
					const double a = s[3] / 255.0;
					acc[0] += s[0] * a * f; acc[1] += s[1] * a * f; acc[2] += s[2] * a * f;
					acc[3] += a * f;
					total += f;
				}
			}
			uint8_t *o = out.at(ox, oy);
			if (acc[3] > 0)
				for (int c = 0; c < 3; ++c) o[c] = (uint8_t)std::min(255L, std::lround(acc[c] / acc[3]));
			o[3] = total > 0 ? (uint8_t)std::min(255L, std::lround(acc[3] / total * 255)) : 0;
		}
	}
	return out;
}

// ------------------------------------------------------------- medium ---

const char *mediumName(ReleaseMedium m)
{
	switch (m) {
	case ReleaseMedium::Diskette525: return "525";
	case ReleaseMedium::Diskette35: return "35";
	case ReleaseMedium::CDROM: return "cdrom";
	default: return "";
	}
}

ReleaseMedium mediumFromName(const std::string &n)
{
	if (n == "525") return ReleaseMedium::Diskette525;
	if (n == "35") return ReleaseMedium::Diskette35;
	if (n == "cdrom") return ReleaseMedium::CDROM;
	return ReleaseMedium::Unknown;
}

namespace {
// BXGameProfile.m:17-26.
const unsigned long long kDisketteGameSizeThreshold = 30ULL * 1024 * 1024;
const time_t k35DisketteGameDate = 757382400;    // 1994-01-01 00:00:00 +0000
const time_t k525DisketteGameDate = 567993600;   // 1988-01-01 00:00:00 +0000
const time_t kInvalidGameDate = 347155200;       // 1981-01-01 00:00:00 +0000

// Returns Unknown to go on walking.
ReleaseMedium walk(const std::string &dir, unsigned long long &size, int depth)
{
	if (depth > 32) return ReleaseMedium::Unknown;   // bounded recursion (small Shell stack)
	std::vector<std::string> names;
	if (!fsutil::list(dir, names)) return ReleaseMedium::Unknown;
	std::sort(names.begin(), names.end());
	for (const auto &n : names) {
		if (n.empty() || n[0] == '.') continue;    // NSDirectoryEnumerationSkipsHiddenFiles
		const std::string p = fsutil::join(dir, n);
		struct stat st;
		if (::stat(p.c_str(), &st) != 0) continue;
		const time_t t = st.st_mtime;
		if (t > kInvalidGameDate) {
			if (t < k525DisketteGameDate) return ReleaseMedium::Diskette525;
			if (t < k35DisketteGameDate) return ReleaseMedium::Diskette35;
		}
		if (S_ISDIR(st.st_mode)) {
			const ReleaseMedium m = walk(p, size, depth + 1);
			if (m != ReleaseMedium::Unknown) return m;
		} else {
			size += (unsigned long long)st.st_size;
			if (size > kDisketteGameSizeThreshold) return ReleaseMedium::CDROM;
		}
	}
	return ReleaseMedium::Unknown;
}
} // namespace

ReleaseMedium mediumOfGameAt(const std::string &path)
{
	unsigned long long size = 0;
	const ReleaseMedium m = walk(path, size, 0);
	return m == ReleaseMedium::Unknown ? ReleaseMedium::Diskette35 : m;
}

// ------------------------------------------------------------ bootleg ---

BootlegStyle bootlegStyle(ReleaseMedium medium)
{
	// Regions are the originals' NSMakeRect(x, y, w, h) at 128 px with y
	// counted from the bottom; here y is the box's top edge: 128 - y - h.
	switch (medium) {
	case ReleaseMedium::CDROM:          // BXJewelCase :39-58
		return {medium, 22, 128 - 32 - 60, 92, 60, 14, 20};
	case ReleaseMedium::Diskette525:    // BX525Diskette :207-219
		return {medium, 16, 128 - 90 - 32, 96, 32, 12, 16};
	default:                            // BX35Diskette :184-198 (font size of BXJewelCase)
		return {ReleaseMedium::Diskette35, 24, 128 - 56 - 56, 80, 56, 14, 18};
	}
}

RGBAImage renderBootleg(ReleaseMedium medium, const std::string &title, const BootlegArt &art,
                        const CoverFont &font)
{
	const int size = 128;
	const BootlegStyle st = bootlegStyle(medium);
	Canvas cv(size, size);
	if (art.base) cv.drawImage(*art.base, 0, 0);
	if (font.valid()) {
		auto lines = wrapTitle(font, title, st.fontSize, st.regionW);
		// Lines that do not fit the box entirely are not drawn, as a text
		// container of the box's height lays them out.
		const size_t maxLines = (size_t)std::floor(st.regionH / st.lineHeight + 1e-9);
		if (lines.size() > maxLines) lines.resize(maxLines);
		std::vector<float> mask((size_t)size * size, 0.0f);
		const double descent = font.descent(st.fontSize);
		for (size_t i = 0; i < lines.size(); ++i) {
			// A fixed line height puts the baseline one descent above the
			// line's bottom (extra height goes above the glyphs).
			const double baseline = st.regionY + (i + 1) * st.lineHeight - descent;
			double x = st.regionX + (st.regionW - font.width(lines[i], st.fontSize)) / 2;
			const std::string &l = lines[i];
			for (size_t c = 0; c < l.size(); ++c) {
				const unsigned char ch = (unsigned char)l[c];
				font.drawGlyph(ch, st.fontSize, x, baseline, mask, size, size);
				x += font.advance(ch, c + 1 < l.size() ? (unsigned char)l[c + 1] : 0, st.fontSize);
			}
		}
		// +[BXJewelCase textColor]: (0.0, 0.1, 0.2) at alpha 0.9.
		for (int y = 0; y < size; ++y)
			for (int x = 0; x < size; ++x)
				cv.over(x, y, 0.0f, 0.1f, 0.2f, 0.9f * mask[(size_t)y * size + x]);
	}
	if (art.top) cv.drawImage(*art.top, 0, 0);
	return cv.image();
}

// ---------------------------------------------------------- cover art ---

bool imageHasTransparency(const RGBAImage &img)
{
	if (img.empty()) return false;
	const int pts[5][2] = {{0, 0}, {img.w - 1, 0}, {0, img.h - 1}, {img.w - 1, img.h - 1},
	                       {img.w / 2, img.h / 2}};
	for (const auto &p : pts)
		if (img.at(p[0], p[1])[3] < 0.9 * 255) return true;
	return false;
}

RGBAImage renderCoverArt(const RGBAImage &picture, const RGBAImage &shine, int size)
{
	if (picture.empty()) return RGBAImage();
	// sizeToFitSize (ADBGeometry.m:73): scale proportionally, up or down.
	auto fit = [](double iw, double ih, double ow, double oh, double &w, double &h) {
		const double rw = ow / iw, rh = oh / ih;
		if (rw < rh) { w = ow; h = ih * rw; } else { w = iw * rh; h = oh; }
	};
	if (imageHasTransparency(picture)) {
		double w, h;
		fit(picture.w, picture.h, size, size, w, h);
		const int iw = std::max(1, (int)std::lround(w)), ih = std::max(1, (int)std::lround(h));
		Canvas cv(size, size);
		cv.drawImage(scaleImage(picture, iw, ih), (size - iw) / 2, (size - ih) / 2);
		return cv.image();
	}
	// +dropShadowForSize: (BXCoverArt.m:19-29), +innerGlowForSize: (:32-40).
	const bool hasShadow = size >= 32, hasGlow = size >= 64;
	const double shadowBlur = hasShadow ? std::max(1.0, size / 32.0) : 0;
	const double shadowOffset = hasShadow ? std::max(1.0, size / 128.0) : 0;
	const double glowBlur = std::max(1.0, size / 64.0);

	double fw, fh;
	fit(picture.w, picture.h, size - shadowBlur * 2, size - shadowBlur * 2, fw, fh);
	// Centred horizontally, standing on the shadow's room at the bottom
	// (:93-98); NSIntegralRect then grows the frame to whole pixels.
	const double fx = (size - fw) / 2, fyBottom = shadowBlur + shadowOffset;
	const int left = (int)std::floor(fx), right = (int)std::ceil(fx + fw);
	const int bottomUp = (int)std::floor(fyBottom), topUp = (int)std::ceil(fyBottom + fh);
	const int top = size - topUp, bottom = size - bottomUp;   // rows [top, bottom)
	const int aw = right - left, ah = bottom - top;
	const RGBAImage art = scaleImage(picture, aw, ah);

	Canvas cv(size, size);
	std::vector<float> rectMask((size_t)size * size, 0.0f);
	for (int y = top; y < bottom; ++y)
		for (int x = left; x < right; ++x)
			if (x >= 0 && y >= 0 && x < size && y < size) rectMask[(size_t)y * size + x] = art.at(x - left, y - top)[3] / 255.0f;

	if (hasShadow) {
		// The shadow falls shadowOffset px down the screen (NSShadow offset
		// (0, -offset) in the original's bottom-up coordinates).
		const auto soft = blur(rectMask, size, size, shadowBlur);
		const int dy = (int)std::lround(shadowOffset);
		for (int y = 0; y < size; ++y)
			for (int x = 0; x < size; ++x) {
				const int sy = y - dy;
				const float a = sy >= 0 && sy < size ? soft[(size_t)sy * size + x] : 0.0f;
				cv.over(x, y, 0, 0, 0, 0.85f * a);
			}
	}
	cv.drawImage(art, left, top);

	if (hasGlow) {
		// fillWithInnerShadow: the blurred outside of the frame, inside it.
		std::vector<float> outside((size_t)size * size, 1.0f);
		for (int y = std::max(0, top); y < std::min(size, bottom); ++y)
			for (int x = std::max(0, left); x < std::min(size, right); ++x) outside[(size_t)y * size + x] = 0.0f;
		const auto glow = blur(outside, size, size, glowBlur);
		for (int y = std::max(0, top); y < std::min(size, bottom); ++y)
			for (int x = std::max(0, left); x < std::min(size, right); ++x)
				cv.over(x, y, 1, 1, 1, 0.33f * glow[(size_t)y * size + x]);
	}

	// BoxArtShine sized to the icon, the frame's part of it at 25% (:115-118).
	if (!shine.empty()) {
		const RGBAImage s = shine.w == size && shine.h == size ? shine : scaleImage(shine, size, size);
		for (int y = std::max(0, top); y < std::min(size, bottom); ++y)
			for (int x = std::max(0, left); x < std::min(size, right); ++x) {
				const uint8_t *p = s.at(x, y);
				cv.over(x, y, p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f * 0.25f);
			}
	}

	// A 1 px line on NSInsetRect(frame, -0.5, -0.5): the pixels just
	// outside the frame, black at 33% (:121-123).
	for (int x = left - 1; x <= right; ++x) {
		cv.over(x, top - 1, 0, 0, 0, 0.33f);
		cv.over(x, bottom, 0, 0, 0, 0.33f);
	}
	for (int y = top; y < bottom; ++y) {
		cv.over(left - 1, y, 0, 0, 0, 0.33f);
		cv.over(right, y, 0, 0, 0, 0.33f);
	}
	return cv.image();
}

// ----------------------------------------------------------------- PNG ---

namespace {
uint32_t crc32Of(const std::string &data, uint32_t crc = 0)
{
	static uint32_t table[256];
	static bool made = false;
	if (!made) {
		for (uint32_t n = 0; n < 256; ++n) {
			uint32_t c = n;
			for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[n] = c;
		}
		made = true;
	}
	crc ^= 0xFFFFFFFFu;
	for (unsigned char ch : data) crc = table[(crc ^ ch) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}
void be32(std::string &o, uint32_t v)
{
	o += (char)(v >> 24); o += (char)(v >> 16); o += (char)(v >> 8); o += (char)v;
}
void chunk(std::string &png, const char *type, const std::string &body)
{
	be32(png, (uint32_t)body.size());
	std::string t = std::string(type, 4) + body;
	png += t;
	be32(png, crc32Of(t));
}
} // namespace

std::string encodePNG(const RGBAImage &img)
{
	std::string png("\x89PNG\r\n\x1a\n", 8);
	std::string ihdr;
	be32(ihdr, (uint32_t)img.w); be32(ihdr, (uint32_t)img.h);
	ihdr += (char)8; ihdr += (char)6; ihdr += (char)0; ihdr += (char)0; ihdr += (char)0;
	chunk(png, "IHDR", ihdr);
	std::string raw;
	raw.reserve((size_t)img.h * (img.w * 4 + 1));
	for (int y = 0; y < img.h; ++y) {
		raw += (char)0;   // filter: none
		raw.append((const char *)img.at(0, y), (size_t)img.w * 4);
	}
	// zlib stream of stored deflate blocks.
	std::string z("\x78\x01", 2);
	size_t pos = 0;
	do {
		const size_t n = std::min<size_t>(65535, raw.size() - pos);
		const bool last = pos + n == raw.size();
		z += (char)(last ? 1 : 0);
		z += (char)(n & 0xFF); z += (char)(n >> 8);
		z += (char)(~n & 0xFF); z += (char)((~n >> 8) & 0xFF);
		z.append(raw, pos, n);
		pos += n;
	} while (pos < raw.size());
	uint32_t a = 1, b = 0;
	for (unsigned char ch : raw) { a = (a + ch) % 65521; b = (b + a) % 65521; }
	be32(z, (b << 16) | a);
	chunk(png, "IDAT", z);
	chunk(png, "IEND", "");
	return png;
}

// -------------------------------------------------------- gamebox data ---

namespace {
const char *kCoverKey = "AROSCoverArt";
}

ReleaseMedium CoverChoice::bootlegMedium() const
{
	switch (style) {
	case CoverStyle::JewelCase: return ReleaseMedium::CDROM;
	case CoverStyle::Diskette35: return ReleaseMedium::Diskette35;
	case CoverStyle::Diskette525: return ReleaseMedium::Diskette525;
	default: return detected == ReleaseMedium::Unknown ? ReleaseMedium::Diskette35 : detected;
	}
}

const char *coverStyleName(CoverStyle s)
{
	switch (s) {
	case CoverStyle::JewelCase: return "cdrom";
	case CoverStyle::Diskette35: return "35";
	case CoverStyle::Diskette525: return "525";
	case CoverStyle::Picture: return "picture";
	default: return "automatic";
	}
}

CoverChoice readCoverChoice(const Gamebox &box)
{
	CoverChoice c;
	const PlistValue *d = box.gameInfo().get(kCoverKey);
	if (!d || d->type() != PlistValue::Type::Dict) return c;
	auto str = [&](const char *k) {
		const PlistValue *v = d->get(k);
		return v && v->type() == PlistValue::Type::String ? v->str() : std::string();
	};
	const std::string style = str("Style");
	if (style == "cdrom") c.style = CoverStyle::JewelCase;
	else if (style == "35") c.style = CoverStyle::Diskette35;
	else if (style == "525") c.style = CoverStyle::Diskette525;
	else if (style == "picture") c.style = CoverStyle::Picture;
	c.detected = mediumFromName(str("Medium"));
	c.picture = str("Picture");
	// A picture record without a usable file name falls back to the bootleg.
	if (c.style == CoverStyle::Picture &&
	    (c.picture.empty() || c.picture.find('/') != std::string::npos || c.picture.find(':') != std::string::npos))
		c.style = CoverStyle::Automatic;
	return c;
}

void setCoverChoice(Gamebox &box, const CoverChoice &c)
{
	PlistValue d = PlistValue::dict();
	d.set("Style", PlistValue::string(coverStyleName(c.style)));
	if (c.detected != ReleaseMedium::Unknown) d.set("Medium", PlistValue::string(mediumName(c.detected)));
	if (c.style == CoverStyle::Picture && !c.picture.empty()) d.set("Picture", PlistValue::string(c.picture));
	box.gameInfo().set(kCoverKey, d);
}

std::string coverPictureName(const std::string &sourcePath)
{
	const std::string ext = fsutil::extension(fsutil::baseName(sourcePath));
	return ext.empty() ? std::string("Cover Art") : "Cover Art." + ext;
}

bool storeCoverPicture(Gamebox &box, const std::string &sourcePath, std::string *error)
{
	auto fail = [&](const std::string &m) { if (error) *error = m; return false; };
	std::string bytes;
	if (!fsutil::readFile(sourcePath, bytes)) return fail("could not read " + sourcePath);
	if (bytes.empty()) return fail(sourcePath + " is empty");
	if (bytes.size() > 16u * 1024 * 1024) return fail(sourcePath + " is larger than 16 MB");
	CoverChoice c = readCoverChoice(box);
	const std::string old = c.style == CoverStyle::Picture ? c.picture : std::string();
	// Never write over the picture the record still names: until the record
	// is saved, that one is the gamebox's cover.
	std::string name = coverPictureName(sourcePath);
	if (fsutil::toLower(name) == fsutil::toLower(old)) {
		const std::string ext = fsutil::extension(name);
		name = ext.empty() ? std::string("Cover Art (2)") : "Cover Art (2)." + ext;
	}
	const std::string dest = fsutil::join(box.path(), name);
	std::string err;
	if (!fsutil::replaceFile(dest, bytes, &err)) return fail("could not write " + dest + ": " + err);
	const PlistValue before = box.gameInfo();
	c.style = CoverStyle::Picture;
	c.picture = name;
	setCoverChoice(box, c);
	if (!box.saveGameInfo(&err)) {
		box.gameInfo() = before;
		std::remove(dest.c_str());
		return fail(err);
	}
	if (!old.empty()) std::remove(fsutil::join(box.path(), old).c_str());
	return true;
}

} // namespace boxer
