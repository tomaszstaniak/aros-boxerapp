// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (expected values from Boxer UI formulas),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Host unit test for src/ui/ui_logic.cpp. Expected values are worked out by
// hand from the original formulas cited in ui_logic.h, not taken from the
// implementation.
#include "../ui_logic.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace boxer_ui;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)
static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

int main() {
    // Band thresholds land on the even ticks 0, 0.2, ... 1.0.
    CHECK(near(speedToSlider(50), 0.0));
    CHECK(near(speedToSlider(1000), 0.2));
    CHECK(near(speedToSlider(2500), 0.4));
    CHECK(near(speedToSlider(10000), 0.6));
    CHECK(near(speedToSlider(25000), 0.8));
    CHECK(near(speedToSlider(62500), 1.0));
    CHECK(near(speedToSlider(3000), 0.4 + 0.2 * 500.0 / 7500.0));
    // Odd ticks are band midpoints.
    CHECK(near(sliderToSpeed(0.5), 6250));
    CHECK(near(sliderToSpeed(0.1), 525));

    // Increments use speed+1 > threshold.
    CHECK(speedIncrement(1000, true) == 100);
    CHECK(speedIncrement(999, true) == 50);
    CHECK(speedIncrement(25000, true) == 2500);
    CHECK(snappedSpeed(3120) == 3000);
    CHECK(snappedSpeed(3260) == 3500);
    CHECK(snappedSpeed(1049) == 1000);

    // Max switches to auto; auto shows at the right end.
    SpeedChange c = applySpeedSlider(1.0, true);
    CHECK(c.sessionSpeed == kAutoSpeed && near(c.knobRatio, 1.0));
    c = applySpeedSlider(0.4 + 0.2 * 500.0 / 7500.0, true);
    CHECK(c.sessionSpeed == 3000);
    CHECK(speedDescription(3000, true) == "386 speed (3000 cycles)");
    CHECK(speedDescription(kAutoSpeed, true) == "Maximum speed");
    CHECK(speedDescription(999, true) == "XT speed (999 cycles)");
    CHECK(speedDescription(25000, true) == "Pentium speed (25000 cycles)");
    CHECK(speedDescription(3000, false).empty());
    CHECK(frameskipDescription(0, true) == "Playing every frame");
    CHECK(frameskipDescription(3, true) == "Playing 1 in 4 frames");

    // Window snapping: base 320x240 (320x200 aspect-corrected), 4:3.
    Size s = snapRenderSize({600, 450}, {320, 240}, 4.0 / 3.0);
    CHECK(s.w == 640 && s.h == 480);          // 40 below 640: snaps up
    s = snapRenderSize({500, 300}, {320, 240}, 4.0 / 3.0);
    CHECK(s.w == 500 && s.h == 375);          // 140 below 640: no snap
    s = snapRenderSize({640, 400}, {320, 240}, 4.0 / 3.0);
    CHECK(s.w == 640 && s.h == 480);          // exact multiple, aspect fixed
    s = snapRenderSize({700, 525}, {320, 240}, 4.0 / 3.0);
    CHECK(s.w == 700 && s.h == 525);          // 260 below 960: no snap

    // Aspect correction (BXVideoFrame): 320x200 -> 320x240, text 720x400 ->
    // 720x540, 640x480 unchanged, 640x350 -> 640x480; off: unchanged.
    Size a = aspectCorrectedSize({320, 200}, true);
    CHECK(a.w == 320 && a.h == 240);
    a = aspectCorrectedSize({720, 400}, true);
    CHECK(a.w == 720 && a.h == 540);
    a = aspectCorrectedSize({640, 480}, true);
    CHECK(a.w == 640 && a.h == 480);
    a = aspectCorrectedSize({640, 350}, true);
    CHECK(a.w == 640 && a.h == 480);
    a = aspectCorrectedSize({640, 400}, false);
    CHECK(a.w == 640 && a.h == 400);
    a = aspectCorrectedSize({320, 400}, true);   // narrower than 4:3: keep height
    CHECK(a.w == 533 && a.h == 400);

    // Viewport: fit and centre.
    Rect v = viewportFor({720, 540}, {640, 480});
    CHECK(v.x == 0 && v.y == 0 && v.w == 640 && v.h == 480);
    v = viewportFor({640, 480}, {720, 360});
    CHECK(v.x == 120 && v.y == 0 && v.w == 480 && v.h == 360);
    v = viewportFor({640, 480}, {640, 600});
    CHECK(v.x == 0 && v.y == 60 && v.w == 640 && v.h == 480);

    // Mouse fraction through the viewport, clamped.
    Fraction fr = canvasFraction(120, 0, v = viewportFor({640, 480}, {720, 360}));
    CHECK(fr.x == 0.0 && fr.y == 0.0);
    fr = canvasFraction(360, 180, v);
    CHECK(fr.x == 0.5 && fr.y == 0.5);
    fr = canvasFraction(10, 400, v);
    CHECK(fr.x == 0.0 && fr.y == 1.0);

    StatusState st = statusFor(false, true, false, true);
    CHECK(st.icon == LockIcon::Hidden && std::strlen(st.message) == 0);
    st = statusFor(true, true, true, true);
    CHECK(st.icon == LockIcon::Locked);
    st = statusFor(true, true, false, false);
    CHECK(std::strncmp(st.message, "Click inside", 12) == 0);

    // Welcome gradient: grey at the bottom centre, black at the top.
    unsigned bottom = welcomeGradient(260, 267, 520, 268);
    unsigned top = welcomeGradient(260, 0, 520, 268);
    CHECK(bottom == ((38u << 16) | (43u << 8) | 51u));
    CHECK(top == 0);
    // ~174 pt up from the bottom is black on the centre line (reference).
    CHECK(welcomeGradient(260, 268 - 175, 520, 268) == 0);
    CHECK(welcomeGradient(260, 268 - 120, 520, 268) != 0);

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("ui_logic_test: all checks passed\n");
    return 0;
}
