// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

#include "gfx.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

// Original Boxer artwork (GPL-2.0, assets/NOTICE-boxer.txt), decoded at build
// time from assets/runtime/boxer by tools/png2inc.py (scripts/build-ui.sh).
#include "boxer_assets.inc"

namespace boxer_ui {

static Image assetImage(const unsigned char *rgba, int w, int h) {
    Image img;
    img.w = w; img.h = h;
    img.rgba.assign(rgba, rgba + (size_t)w * h * 4);
    return img;
}
#define BX_ASSET(n) assetImage(kAsset_##n, kAsset_##n##_w, kAsset_##n##_h)

const Image &originalImage(OriginalArt which) {
    static Image cache[2];
    static bool done[2];
    int i = (int)which;
    if (!done[i]) {
        cache[i] = which == OriginalArt::WelcomeSpotlight ? BX_ASSET(WelcomeSpotlight)
                                                          : BX_ASSET(WelcomeFocusRing);
        done[i] = true;
    }
    return cache[i];
}

// AppKit template images: only the alpha channel counts; the caller tints.
static Mask templateMask(const unsigned char *rgba, int w, int h) {
    Mask m;
    m.w = w; m.h = h;
    m.a.resize((size_t)w * h);
    for (size_t i = 0; i < m.a.size(); ++i) m.a[i] = rgba[i * 4 + 3] / 255.0f;
    return m;
}
#define BX_TEMPLATE(n) templateMask(kAsset_##n, kAsset_##n##_w, kAsset_##n##_h)

static inline uint8_t ch(unsigned c, int shift) { return (c >> shift) & 0xff; }

void Canvas::set(int x, int y, unsigned c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint8_t *p = at(x, y);
    p[0] = ch(c, 16); p[1] = ch(c, 8); p[2] = ch(c, 0);
}

void Canvas::blend(int x, int y, unsigned c, double a) {
    if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
    if (a > 1) a = 1;
    uint8_t *p = at(x, y);
    for (int i = 0; i < 3; ++i) {
        double s = ch(c, 16 - 8 * i);
        p[i] = (uint8_t)std::lround(p[i] * (1 - a) + s * a);
    }
}

void Canvas::addLight(int x, int y, unsigned c, double a) {
    if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
    uint8_t *p = at(x, y);
    for (int i = 0; i < 3; ++i) {
        double v = p[i] + ch(c, 16 - 8 * i) * a;
        p[i] = (uint8_t)std::min(255.0, std::round(v));
    }
}

Prim rect(double x, double y, double w, double h, bool sub) {
    Prim p{Prim::Rect}; p.a = x; p.b = y; p.c = w; p.d = h; p.subtract = sub; return p;
}
Prim rrect(double x, double y, double w, double h, double r, bool sub) {
    Prim p{Prim::RRect}; p.a = x; p.b = y; p.c = w; p.d = h; p.e = r; p.subtract = sub; return p;
}
Prim circle(double cx, double cy, double r, bool sub) {
    Prim p{Prim::Circle}; p.a = cx; p.b = cy; p.c = r; p.subtract = sub; return p;
}
Prim ring(double cx, double cy, double r0, double r1, int half, bool sub) {
    Prim p{Prim::Ring}; p.a = cx; p.b = cy; p.c = r0; p.d = r1; p.half = half;
    p.subtract = sub; return p;
}
Prim poly(std::vector<std::pair<double, double>> pts, bool sub) {
    Prim p{Prim::Poly}; p.pts = std::move(pts); p.subtract = sub; return p;
}

static bool inside(const Prim &p, double x, double y) {
    switch (p.kind) {
    case Prim::Rect:
        return x >= p.a && x < p.a + p.c && y >= p.b && y < p.b + p.d;
    case Prim::RRect: {
        if (!(x >= p.a && x < p.a + p.c && y >= p.b && y < p.b + p.d)) return false;
        double r = std::min(p.e, std::min(p.c, p.d) / 2);
        double cx = std::clamp(x, p.a + r, p.a + p.c - r);
        double cy = std::clamp(y, p.b + r, p.b + p.d - r);
        double dx = x - cx, dy = y - cy;
        return dx * dx + dy * dy <= r * r;
    }
    case Prim::Circle: {
        double dx = x - p.a, dy = y - p.b;
        return dx * dx + dy * dy <= p.c * p.c;
    }
    case Prim::Ring: {
        double dx = x - p.a, dy = y - p.b, d2 = dx * dx + dy * dy;
        if (d2 < p.c * p.c || d2 > p.d * p.d) return false;
        switch (p.half) {
        case 1: return x >= p.a;
        case 2: return y <= p.b;
        case 3: return x <= p.a;
        case 4: return y >= p.b;
        default: return true;
        }
    }
    case Prim::Poly: {
        bool in = false;
        size_t n = p.pts.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            double xi = p.pts[i].first, yi = p.pts[i].second;
            double xj = p.pts[j].first, yj = p.pts[j].second;
            if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi))
                in = !in;
        }
        return in;
    }
    }
    return false;
}

Mask rasterize(int w, int h, const std::vector<Prim> &prims) {
    Mask m; m.w = w; m.h = h; m.a.assign((size_t)w * h, 0.f);
    const int S = 4;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int hits = 0;
            for (int sy = 0; sy < S; ++sy)
                for (int sx = 0; sx < S; ++sx) {
                    double px = x + (sx + 0.5) / S, py = y + (sy + 0.5) / S;
                    bool in = false;
                    for (const Prim &p : prims)
                        if (inside(p, px, py)) in = !p.subtract;
                    hits += in;
                }
            m.a[(size_t)y * w + x] = (float)hits / (S * S);
        }
    return m;
}

static unsigned lerpColor(unsigned a, unsigned b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    unsigned out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        double v = ch(a, s) * (1 - t) + ch(b, s) * t;
        out |= (unsigned)std::lround(v) << s;
    }
    return out;
}

void paintLayers(Canvas &cv, int ox, int oy, int w, int h,
                 const std::vector<Layer> &layers) {
    for (const Layer &l : layers) {
        Mask m = rasterize(w, h, l.prims);
        for (int y = 0; y < h; ++y) {
            double t = l.y1 > l.y0 ? (y + 0.5 - l.y0) / (l.y1 - l.y0) : 0.0;
            unsigned c = lerpColor(l.top, l.bottom, t);
            for (int x = 0; x < w; ++x)
                if (m.at(x, y) > 0) cv.blend(ox + x, oy + y, c, m.at(x, y) * l.alpha);
        }
    }
}

void paintMask(Canvas &cv, int ox, int oy, const Mask &m, unsigned c, double alpha) {
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x)
            if (m.at(x, y) > 0) cv.blend(ox + x, oy + y, c, m.at(x, y) * alpha);
}

Image renderImage(int w, int h, const std::vector<Layer> &layers) {
    // Render over a 0-alpha canvas by painting onto black and white and
    // recovering alpha from the difference; avoids a second compositor.
    Canvas b(w, h), wcv(w, h);
    std::fill(wcv.rgb.begin(), wcv.rgb.end(), 255);
    paintLayers(b, 0, 0, w, h, layers);
    paintLayers(wcv, 0, 0, w, h, layers);
    Image img; img.w = w; img.h = h; img.rgba.resize((size_t)w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        double a = 1.0 - (wcv.rgb[i * 3] - b.rgb[i * 3]) / 255.0;
        a = std::clamp(a, 0.0, 1.0);
        for (int k = 0; k < 3; ++k)
            img.rgba[i * 4 + k] = a > 0 ? (uint8_t)std::clamp(std::lround(b.rgb[i * 3 + k] / a), 0L, 255L) : 0;
        img.rgba[i * 4 + 3] = (uint8_t)std::lround(a * 255);
    }
    return img;
}

void drawImage(Canvas &cv, int ox, int oy, const Image &img, double alpha) {
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const uint8_t *p = &img.rgba[((size_t)y * img.w + x) * 4];
            if (!p[3]) continue;
            unsigned c = (p[0] << 16) | (p[1] << 8) | p[2];
            cv.blend(ox + x, oy + y, c, p[3] / 255.0 * alpha);
        }
}

// --- 5x7 block font for the test pattern ---------------------------------
static const std::map<char, const char *> &miniFont() {
    static const std::map<char, const char *> f = {
        {'C', ".###.#...##....#....#....#...#.###."},
        {':', ".......##..##.......##..##........."},
        {'\\', "#.....#.....#.....#.....#.....#...."},
        {'>', ".#.....#.....#.....#...#...#...#..."},
        {'_', "..............................#####"},
        {'T', "#####..#....#....#....#....#....#.."},
        {'E', "######....#....####.#....#....#####"},
        {'S', ".#####....#.....###......#....####."},
        {'P', "####.#...##...#####.#....#....#...."},
        {'A', ".###.#...##...#######...##...##...#"},
        {'R', "####.#...##...#####.#.#..#..#.#...#"},
        {'N', "#...###..##.#.##..###...##...##...#"},
        {'3', "####.....#....#.###.....#....#####."},
        {'2', ".###.#...#....#...#...#...#...#####"},
        {'0', ".###.#..###.#.###..##...##...#.###."},
        {'X', "#...##...#.#.#...#...#.#.#...##...#"},
        {' ', "..................................."},
    };
    return f;
}

static void blockText(Canvas &cv, int x, int y, const char *s, int scale, unsigned c) {
    for (; *s; ++s, x += 6 * scale) {
        auto it = miniFont().find(*s);
        if (it == miniFont().end()) continue;
        for (int r = 0; r < 7; ++r)
            for (int k = 0; k < 5; ++k)
                if (it->second[r * 5 + k] == '#')
                    for (int dy = 0; dy < scale; ++dy)
                        for (int dx = 0; dx < scale; ++dx)
                            cv.set(x + k * scale + dx, y + r * scale + dy, c);
    }
}

// --- Welcome images (128x128) --------------------------------------------
// Boxer's original gamefolder/import/prompt art (restored 2026-10-02; the
// Apple resemblance is an open question, assets/README.md).
const Image &welcomeImage(WelcomeArt which) {
    static Image cache[3];
    static bool done[3];
    int i = (int)which;
    if (!done[i]) {
        cache[i] = which == WelcomeArt::GameFolder ? BX_ASSET(GameFolder)
                 : which == WelcomeArt::Import     ? BX_ASSET(Import)
                                                   : BX_ASSET(Prompt);
        done[i] = true;
    }
    return cache[i];
}

// --- Inspector tab icons (32x32) ------------------------------------------
const Image &tabIcon(TabIcon which) {
    static Image cache[5];
    static bool done[5];
    int i = (int)which;
    if (done[i]) return cache[i];
    done[i] = true;
    // Boxer's own tab icons, Game.png included (restored 2026-10-02).
    switch (which) {
    case TabIcon::Game: return cache[i] = BX_ASSET(Gamebox);
    case TabIcon::CPU: return cache[i] = BX_ASSET(CPU);
    case TabIcon::Mouse: return cache[i] = BX_ASSET(Mouse);
    case TabIcon::Joystick: return cache[i] = BX_ASSET(Joystick);
    case TabIcon::Drives: default: return cache[i] = BX_ASSET(Drives);
    }
}

// --- Template-style glyphs (alpha masks) ----------------------------------
const Mask &glyphMask(Glyph g) {
    static std::map<int, Mask> cache;
    auto it = cache.find((int)g);
    if (it != cache.end()) return it->second;
    // Boxer's own templates; Reveal, FullScreen and the locks are Apple system
    // images (category b), replaced by Feather glyphs (MIT).
    switch (g) {
    case Glyph::LauncherList: return cache[(int)g] = BX_TEMPLATE(LauncherListTemplate);
    case Glyph::Documentation: return cache[(int)g] = BX_TEMPLATE(DocumentationTemplate);
    case Glyph::Pause: return cache[(int)g] = BX_TEMPLATE(PauseTemplate);
    case Glyph::Play: return cache[(int)g] = BX_TEMPLATE(PlayTemplate);
    case Glyph::FastForward: return cache[(int)g] = BX_TEMPLATE(FastForwardTemplate);
    case Glyph::Volume0: return cache[(int)g] = BX_TEMPLATE(Volume0PercentCroppedTemplate);
    case Glyph::Volume100: return cache[(int)g] = BX_TEMPLATE(Volume100PercentTemplate);
    case Glyph::Reveal: return cache[(int)g] = BX_TEMPLATE(RevealTemplate);
    case Glyph::FullScreen: return cache[(int)g] = BX_TEMPLATE(FullScreenTemplate);
    case Glyph::LockLocked: return cache[(int)g] = BX_TEMPLATE(LockLockedTemplate);
    case Glyph::LockUnlocked: default: return cache[(int)g] = BX_TEMPLATE(LockUnlockedTemplate);
    }
}

// --- Launch panel art -------------------------------------------------------
// Box filter from the 32x32 1x template to the 24x24 heading image view.
static Mask scaledMask(const Mask &src, int w, int h) {
    Mask m;
    m.w = w; m.h = h;
    m.a.assign((size_t)w * h, 0.0f);
    const double sx = (double)src.w / w, sy = (double)src.h / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double x0 = x * sx, x1 = x0 + sx, y0 = y * sy, y1 = y0 + sy, sum = 0;
            for (int v = (int)y0; v < (int)std::ceil(y1) && v < src.h; ++v)
                for (int u = (int)x0; u < (int)std::ceil(x1) && u < src.w; ++u) {
                    double cw = std::min<double>(u + 1, x1) - std::max<double>(u, x0);
                    double chh = std::min<double>(v + 1, y1) - std::max<double>(v, y0);
                    if (cw > 0 && chh > 0) sum += src.at(u, v) * cw * chh;
                }
            m.a[(size_t)y * w + x] = (float)(sum / (sx * sy));
        }
    return m;
}

const Mask &launcherIcon(LauncherArt which) {
    static std::map<int, Mask> cache;
    auto it = cache.find((int)which);
    if (it != cache.end()) return it->second;
    Mask full;
    switch (which) {
    case LauncherArt::Favorites: full = BX_TEMPLATE(FavoriteOutlineTemplate); break;
    case LauncherArt::Recent: full = BX_TEMPLATE(RecentItemsTemplate); break;
    case LauncherArt::AllPrograms: full = BX_TEMPLATE(LauncherListTemplate); break;
    case LauncherArt::CDROM: full = BX_TEMPLATE(CDROMTemplate); break;
    case LauncherArt::Floppy: full = BX_TEMPLATE(DisketteTemplate); break;
    case LauncherArt::HardDisk: default: full = BX_TEMPLATE(HardDiskTemplate); break;
    }
    // proportionallyDown: only larger images shrink, aspect kept, centred.
    int w = full.w, h = full.h;
    if (w > 24 || h > 24) {
        double k = std::min(24.0 / w, 24.0 / h);
        w = std::max(1, (int)std::lround(w * k));
        h = std::max(1, (int)std::lround(h * k));
    }
    Mask scaled = (w == full.w && h == full.h) ? full : scaledMask(full, w, h);
    Mask out;
    out.w = 24; out.h = 24;
    out.a.assign(24 * 24, 0.0f);
    int ox = (24 - w) / 2, oy = (24 - h) / 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (ox + x >= 0 && oy + y >= 0 && ox + x < 24 && oy + y < 24)
                out.a[(size_t)(oy + y) * 24 + ox + x] = scaled.at(x, y);
    return cache[(int)which] = out;
}

const Image &launchPanelDivider() {
    static Image img = BX_ASSET(LaunchPanelDivider);
    return img;
}

// --- Test pattern ----------------------------------------------------------
const Canvas &testPattern() {
    static Canvas cv;
    if (cv.w) return cv;
    cv.resize(320, 200);
    static const unsigned ega[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF};
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            unsigned c = 0x000000;
            if (y < 40) c = ega[8 + x / 40];              // bright bars
            else if (y < 60) c = ega[x / 40];             // dark bars
            else if (y >= 180) c = (unsigned)((x * 255 / 319) * 0x010101);  // grey ramp
            else if (x % 20 == 0 || y % 20 == 0) c = 0x303030;               // grid
            cv.set(x, y, c);
        }
    // On a 4:3 display each 320x200 pixel is 1.2 times taller than wide, so
    // an ellipse with ry = rx / 1.2 appears round when the aspect is right.
    for (int k = 0; k < 2000; ++k) {
        double t = k * 2 * M_PI / 2000;
        int x = (int)std::lround(160 + 60 * std::cos(t));
        int y = (int)std::lround(120 + 50 * std::sin(t));
        cv.set(x, y, 0xFFFFFF);
        cv.set(x + 1, y, 0xFFFFFF);
    }
    // Corner markers show any crop at the edges.
    for (int k = 0; k < 8; ++k) {
        cv.set(k, 62, 0xFF5555); cv.set(319 - k, 62, 0xFF5555);
        cv.set(k, 177, 0xFF5555); cv.set(319 - k, 177, 0xFF5555);
    }
    blockText(cv, 124, 72, "TEST", 2, 0xFFFF55);
    blockText(cv, 94, 152, "320X200", 2, 0xFFFFFF);
    return cv;
}

}  // namespace boxer_ui
