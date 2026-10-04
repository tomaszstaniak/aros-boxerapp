// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// Software canvas, Boxer's original artwork and its clear-provenance replacements.
//
// Boxer's own images are GPL-2.0 (assets/README.md, assets/NOTICE-boxer.txt)
// and are embedded from assets/runtime/boxer where the asset audit cleared
// them (category (a) in assets/boxer-assets-manifest.json).
// Unclear and Apple images are replaced by Tango
// (public domain) and Feather (MIT) art: assets/replacement-assets.json.
#ifndef BOXER_UI_GFX_H
#define BOXER_UI_GFX_H

#include <cstdint>
#include <utility>
#include <vector>

namespace boxer_ui {

struct Canvas {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;   // packed RGB, row-major, what RECTFMT_RGB takes
    Canvas() = default;
    Canvas(int w_, int h_) : w(w_), h(h_), rgb((size_t)w_ * h_ * 3, 0) {}
    void resize(int w_, int h_) { w = w_; h = h_; rgb.assign((size_t)w * h * 3, 0); }
    uint8_t *at(int x, int y) { return &rgb[((size_t)y * w + x) * 3]; }
    void set(int x, int y, unsigned c);
    // Source-over with coverage a (0..1).
    void blend(int x, int y, unsigned c, double a);
    // AppKit's NSCompositePlusLighter: dst + src * a, clamped.
    void addLight(int x, int y, unsigned c, double a);
};

// Anti-aliased coverage masks built from primitives (4x4 supersampling).
struct Prim {
    enum Kind { Rect, RRect, Circle, Ring, Poly } kind;
    double a = 0, b = 0, c = 0, d = 0, e = 0;   // geometry, per kind
    std::vector<std::pair<double, double>> pts;
    int half = 0;      // Ring only: 0 full, 1 x>=a, 2 y<=b, 3 x<=a, 4 y>=b
    bool subtract = false;
};
Prim rect(double x, double y, double w, double h, bool sub = false);
Prim rrect(double x, double y, double w, double h, double r, bool sub = false);
Prim circle(double cx, double cy, double r, bool sub = false);
Prim ring(double cx, double cy, double r0, double r1, int half = 0, bool sub = false);
Prim poly(std::vector<std::pair<double, double>> pts, bool sub = false);

struct Mask {
    int w = 0, h = 0;
    std::vector<float> a;
    float at(int x, int y) const { return a[(size_t)y * w + x]; }
};
Mask rasterize(int w, int h, const std::vector<Prim> &prims);

// A filled layer with a vertical colour gradient over [y0, y1].
struct Layer {
    std::vector<Prim> prims;
    unsigned top, bottom;
    double alpha = 1.0;
    double y0 = 0, y1 = 0;   // gradient span in local coordinates; 0,0 = flat
};
void paintLayers(Canvas &cv, int ox, int oy, int w, int h,
                 const std::vector<Layer> &layers);
void paintMask(Canvas &cv, int ox, int oy, const Mask &m, unsigned c, double alpha);

// RGBA image (premultiplied-free): embedded art and rendered layers.
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
};
Image renderImage(int w, int h, const std::vector<Layer> &layers);
void drawImage(Canvas &cv, int ox, int oy, const Image &img, double alpha = 1.0);

// Originals drawn with PlusLighter by the welcome buttons (160x160).
enum class OriginalArt { WelcomeSpotlight, WelcomeFocusRing };
const Image &originalImage(OriginalArt which);

// Originals and replacements. Names match the original resource names they replace.
enum class WelcomeArt { GameFolder, Import, Prompt };
const Image &welcomeImage(WelcomeArt which);              // 128x128
enum class TabIcon { Game, CPU, Mouse, Joystick, Drives };
const Image &tabIcon(TabIcon which);                     // 32x32
enum class Glyph {
    LauncherList, Documentation, Pause, Play, FastForward, Volume0,
    Volume100, Reveal, FullScreen, LockUnlocked, LockLocked
};
const Mask &glyphMask(Glyph g);   // template-style alpha mask at 1x size

// Launch panel art (Boxer originals, category a): heading templates as
// 24x24 masks (the 32 pt PDFs drawn proportionally down into the 24x24
// heading image view) and the 560x4 divider.
enum class LauncherArt { Favorites, Recent, AllPrograms, HardDisk, CDROM, Floppy };
const Mask &launcherIcon(LauncherArt which);
const Image &launchPanelDivider();

// The 320x200 test pattern standing in for the DOS frame (emulator not wired).
const Canvas &testPattern();

}  // namespace boxer_ui
#endif
