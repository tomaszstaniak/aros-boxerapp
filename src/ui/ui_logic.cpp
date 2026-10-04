// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer 2.0-alpha UI rules cited per line),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "ui_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace boxer_ui {

namespace {
const double kBands[6] = {kMinSpeed, k286Speed, k386Speed,
                          k486Speed, kPentiumSpeed, kMaxSpeed};
const int kNumBands = 6;

unsigned rgb(double r, double g, double b) {
    auto c = [](double v) {
        long i = std::lround(v);
        return (unsigned)std::clamp(i, 0L, 255L);
    };
    return (c(r) << 16) | (c(g) << 8) | c(b);
}

// Parameter t of a two-circle radial gradient (CoreGraphics semantics): the
// largest t whose circle passes through the point with a non-negative radius.
// Both Boxer gradients extend before and after, so t is clamped afterwards.
double radialT(double px, double py, double x0, double y0, double r0,
               double x1, double y1, double r1) {
    double cdx = x1 - x0, cdy = y1 - y0, dr = r1 - r0;
    double pdx = px - x0, pdy = py - y0;
    double a = cdx * cdx + cdy * cdy - dr * dr;
    double b = pdx * cdx + pdy * cdy + r0 * dr;
    double c = pdx * pdx + pdy * pdy - r0 * r0;
    if (std::fabs(a) < 1e-9) {            // concentric-like degenerate case
        return b != 0 ? c / (2 * b) : 0.0;
    }
    double disc = b * b - a * c;
    if (disc < 0) return 0.0;
    double s = std::sqrt(disc);
    double t1 = (b + s) / a, t2 = (b - s) / a;
    double t = std::max(t1, t2);
    if (r0 + t * dr < 0) t = std::min(t1, t2);
    return t;
}
}  // namespace

double speedToSlider(double v) {
    if (v <= kBands[0]) return 0.0;
    if (v >= kBands[kNumBands - 1]) return 1.0;
    int band = 1;
    for (; band < kNumBands; ++band)
        if (v < kBands[band]) break;
    double lo = kBands[band - 1], hi = kBands[band];
    double within = (hi - lo) != 0 ? (v - lo) / (hi - lo) : 0.0;
    return (band - 1 + within) / (kNumBands - 1);
}

double sliderToSpeed(double r) {
    if (r >= 1.0) return kBands[kNumBands - 1];
    if (r <= 0.0) return kBands[0];
    double spread = 1.0 / (kNumBands - 1);
    int band = (int)(r / spread);
    double within = (r - band * spread) / spread;
    double lo = kBands[band], hi = kBands[band + 1];
    return lo + within * (hi - lo);
}

int speedIncrement(int speed, bool goingUp) {
    speed += goingUp ? 1 : 0;
    if (speed > kPentiumSpeed) return 2500;
    if (speed > k486Speed) return 1000;
    if (speed > k386Speed) return 500;
    if (speed > k286Speed) return 100;
    return 50;
}

int snappedSpeed(int raw) {
    int inc = speedIncrement(raw, true);
    return (int)(std::round((double)raw / inc) * inc);
}

int sliderValueToSessionSpeed(int s) { return s >= kMaxSpeed ? kAutoSpeed : s; }
int sessionSpeedToSliderValue(int s) { return s == kAutoSpeed ? kMaxSpeed : s; }

std::string speedDescription(int s, bool emulating) {
    if (!emulating) return "";
    if (s == kAutoSpeed) return "Maximum speed";
    const char *cls = s >= kPentiumSpeed ? "Pentium"
                    : s >= k486Speed     ? "486"
                    : s >= k386Speed     ? "386"
                    : s >= k286Speed     ? "AT"
                                         : "XT";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s speed (%u cycles)", cls, (unsigned)s);
    return buf;
}

std::string frameskipDescription(int fs, bool emulating) {
    if (!emulating) return "";
    if (fs == 0) return "Playing every frame";
    char buf[48];
    std::snprintf(buf, sizeof buf, "Playing 1 in %u frames", (unsigned)(fs + 1));
    return buf;
}

SpeedChange applySpeedSlider(double ratio, bool snap) {
    // The binding validates the banded value (validateSliderSpeed:) before
    // setSliderSpeed:, so snapping happens on cycles, not on the ratio.
    int raw = (int)std::lround(sliderToSpeed(ratio));
    int value = snap ? snappedSpeed(raw) : raw;
    int session = sliderValueToSessionSpeed(value);
    return {session, speedToSlider(sessionSpeedToSliderValue(session))};
}

Size snapRenderSize(Size p, Size base, double aspect) {
    Size out = p;
    if (base.w > 0) {
        double snapped = std::ceil((double)p.w / base.w) * base.w;
        double diff = std::fabs(snapped - p.w);
        if (diff > 0 && diff <= kWindowSnapThreshold) out.w = (int)snapped;
    }
    // The window's contentAspectRatio keeps the proportion during any resize
    // (BXDOSWindow.m:55-100); the snap above only picks the width.
    if (aspect > 0) out.h = (int)std::lround(out.w / aspect);
    return out;
}

Size aspectCorrectedSize(Size f, bool correct) {
    if (!correct || f.w <= 0 || f.h <= 0) return f;
    const double target = 320.0 / 240.0, ratio = (double)f.w / f.h;
    if (std::fabs(target - ratio) <= 0.025) return f;
    // sizeToMatchRatio (ADBGeometry.m:59-66): keep the height when the frame
    // is narrower than 4:3, otherwise keep the width.
    bool preserveHeight = ratio < target;
    double iw = preserveHeight ? f.h * target : f.w, ih = preserveHeight ? f.h : f.w / target;
    // scale = intended / size, scaledSize = roundf(size * scale)
    double sx = iw / f.w, sy = ih / f.h;
    return {(int)std::lround((float)(f.w * sx)), (int)std::lround((float)(f.h * sy))};
}

Rect viewportFor(Size s, Size c) {
    if (s.w <= 0 || s.h <= 0 || c.w <= 0 || c.h <= 0) return {0, 0, c.w, c.h};
    // sizeToFitSize: scale by the smaller of the two ratios.
    double k = std::min((double)c.w / s.w, (double)c.h / s.h);
    int w = (int)std::lround(s.w * k), h = (int)std::lround(s.h * k);
    w = std::min(w, c.w); h = std::min(h, c.h);
    return {(int)std::lround((c.w - w) * 0.5), (int)std::lround((c.h - h) * 0.5), w, h};
}

Fraction canvasFraction(int x, int y, Rect r) {
    double fx = r.w > 0 ? (double)(x - r.x) / r.w : 0, fy = r.h > 0 ? (double)(y - r.y) / r.h : 0;
    return {std::clamp(fx, 0.0, 1.0), std::clamp(fy, 0.0, 1.0)};
}

StatusState statusFor(bool mouseActive, bool dosViewShown, bool locked,
                      bool track) {
    // "Cmd" is the macOS modifier. The click accepts either Amiga key, but
    // AROS Intuition takes Left Amiga + left button for screen dragging
    // (IControl MetaDrag, default IEQUALIFIER_LCOMMAND: rom/intuition/
    // inputhandler.c), so by default only Right Amiga reaches the window;
    // the text names that one (guest test 2026-10-01, comparison.md).
    if (!(mouseActive && dosViewShown)) return {LockIcon::Hidden, ""};
    if (locked) return {LockIcon::Locked, "Right Amiga+click to release the mouse pointer."};
    if (track)
        return {LockIcon::Unlocked,
                "Right Amiga+click inside the window to lock the mouse pointer."};
    return {LockIcon::Unlocked, "Click inside the window to lock the mouse pointer."};
}

unsigned welcomeGradient(double x, double y, double W, double H) {
    // Convert to y-up as in the original drawing code.
    double yu = H - y;
    double cx = W / 2, cy = 0.15 * H - 1.5 * W;
    double r0 = 1.5 * W, r1 = 1.5 * W + 0.5 * H;
    double t = radialT(x, yu, cx, cy, r0, cx, cy, r1);
    t = std::clamp(t, 0.0, 1.0);
    double k = 1.0 - t;   // grey(0.15,0.17,0.20) -> black
    return rgb(0.15 * 255 * k, 0.17 * 255 * k, 0.20 * 255 * k);
}

unsigned dosBackground(double x, double y, double W, double H) {
    double r = 97, g = 98, b = 103;
    // Vignette: white a=0.2 at the top centre (r = 0.1W) to black a=0.2 at
    // the middle (r = 0.75W). Computed in y-up coordinates like AppKit.
    double yu = H - y;
    double t = radialT(x, yu, W / 2, H, 0.1 * W, W / 2, H / 2, 0.75 * W);
    t = std::clamp(t, 0.0, 1.0);
    double c = 255.0 * (1.0 - t), a = 0.2;
    r = r * (1 - a) + c * a; g = g * (1 - a) + c * a; b = b * (1 - a) + c * a;
    // Top inner shadow, 6 pt: black a=0.3 @0, 0.05 @0.5, clear @1.
    if (y < 6) {
        double u = y / 6.0;
        double sa = u < 0.5 ? 0.3 + (0.05 - 0.3) * (u / 0.5)
                            : 0.05 * (1 - (u - 0.5) / 0.5);
        r *= 1 - sa; g *= 1 - sa; b *= 1 - sa;
    }
    return rgb(r, g, b);
}

}  // namespace boxer_ui
