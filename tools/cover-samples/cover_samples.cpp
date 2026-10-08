// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// Renders the three bootleg covers with a candidate title font, with the
// same code BoxerUI uses (src/model/coverart.cpp, coverfont.cpp) and the
// original templates, for choosing a free replacement font.
//
// Usage: cover_samples <out dir> <label> <font.ttf> <caption font.ttf>
// Writes <label>-<template>-<n>.png (each 128 px cover, with alpha) and
// <label>-sheet.png / .ppm: every cover on a light and a dark background,
// shown at 1x and at 2x (nearest neighbour), captioned with the label.
#include "../../src/model/coverart.h"
#include "../../src/model/coverfont.h"
#include "../../src/model/fsutil.h"
#include "../../src/ui/coverassets.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace boxer;

static const char *kTitles[] = {"Tyrian", "Commander Keen 4", "The Secret of Monkey Island",
                                "Prince of Persia 2: The Shadow and the Flame"};
static const struct { ReleaseMedium m; const char *name; } kTemplates[] = {
    {ReleaseMedium::CDROM, "jewelcase"},
    {ReleaseMedium::Diskette35, "35diskette"},
    {ReleaseMedium::Diskette525, "525diskette"},
};

static void put(RGBAImage &dst, const RGBAImage &src, int ox, int oy, int zoom)
{
    for (int y = 0; y < src.h * zoom; ++y)
        for (int x = 0; x < src.w * zoom; ++x) {
            const int dx = ox + x, dy = oy + y;
            if (dx < 0 || dy < 0 || dx >= dst.w || dy >= dst.h) continue;
            const uint8_t *s = src.at(x / zoom, y / zoom);
            uint8_t *d = dst.at(dx, dy);
            const double a = s[3] / 255.0;
            for (int c = 0; c < 3; ++c) d[c] = (uint8_t)(s[c] * a + d[c] * (1 - a) + 0.5);
        }
}

static void fill(RGBAImage &img, int x0, int y0, int w, int h, uint8_t v)
{
    for (int y = y0; y < y0 + h && y < img.h; ++y)
        for (int x = x0; x < x0 + w && x < img.w; ++x) {
            uint8_t *p = img.at(x, y);
            p[0] = p[1] = p[2] = v; p[3] = 255;
        }
}

static void caption(RGBAImage &img, const CoverFont &font, const std::string &text, int x, int baseline)
{
    std::vector<float> mask((size_t)img.w * img.h, 0.0f);
    double pen = x;
    for (size_t i = 0; i < text.size(); ++i) {
        font.drawGlyph((unsigned char)text[i], 14, pen, baseline, mask, img.w, img.h);
        pen += font.advance((unsigned char)text[i], i + 1 < text.size() ? (unsigned char)text[i + 1] : 0, 14);
    }
    for (int y = 0; y < img.h; ++y)
        for (int xx = 0; xx < img.w; ++xx) {
            const float a = mask[(size_t)y * img.w + xx];
            uint8_t *p = img.at(xx, y);
            for (int c = 0; c < 3; ++c) p[c] = (uint8_t)(p[c] * (1 - a) + 0.5);
        }
}

int main(int argc, char **argv)
{
    if (argc != 5) { fprintf(stderr, "usage: cover_samples <out dir> <label> <font.ttf> <caption font.ttf>\n"); return 2; }
    const std::string out = argv[1], label = argv[2];
    CoverFont font, cap;
    if (!font.loadFile(argv[3])) { fprintf(stderr, "cannot read font %s\n", argv[3]); return 1; }
    if (!cap.loadFile(argv[4])) { fprintf(stderr, "cannot read font %s\n", argv[4]); return 1; }
    fsutil::makeDirs(out);
    const int n = sizeof kTitles / sizeof *kTitles, gap = 12, head = 34;
    // Per template row: n covers at 1x on light, n at 1x on dark, then n at 2x on light.
    const int rowH = 128 * 2 + gap * 2, width = gap + n * (256 + gap);
    RGBAImage sheet(width, head + 3 * (rowH + 128 + gap) + gap);
    fill(sheet, 0, 0, sheet.w, sheet.h, 235);
    caption(sheet, cap, label + "  (bootleg covers, 1x on light and dark, then 2x)", gap, 22);
    int y = head;
    for (const auto &t : kTemplates) {
        for (int i = 0; i < n; ++i) {
            RGBAImage cover = renderBootleg(t.m, kTitles[i], boxer_ui::bootlegArt(t.m), font);
            fsutil::writeFile(fsutil::join(out, label + "-" + t.name + "-" + std::to_string(i + 1) + ".png"),
                              encodePNG(cover));
            const int x = gap + i * (256 + gap);
            put(sheet, cover, x, y, 1);
            fill(sheet, x + 128, y, 128, 128, 60);
            put(sheet, cover, x + 128, y, 1);
            put(sheet, cover, x, y + 128 + gap, 2);
        }
        y += 128 + gap + 256 + gap;
    }
    sheet.h = y;
    sheet.px.resize((size_t)sheet.w * sheet.h * 4);
    fsutil::writeFile(fsutil::join(out, label + "-sheet.png"), encodePNG(sheet));
    std::string ppm = "P6\n" + std::to_string(sheet.w) + " " + std::to_string(sheet.h) + "\n255\n";
    for (int yy = 0; yy < sheet.h; ++yy)
        for (int x = 0; x < sheet.w; ++x) ppm.append((const char *)sheet.at(x, yy), 3);
    fsutil::writeFile(fsutil::join(out, label + "-sheet.ppm"), ppm);
    printf("%s: %s-sheet.png\n", label.c_str(), fsutil::join(out, label).c_str());
    return 0;
}
