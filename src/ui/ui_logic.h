// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer 2.0-alpha UI rules cited per line),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// Pure UI logic for the Zune slice: no AROS headers, so it is unit-tested on
// the host (src/ui/tests/ui_logic_test.cpp) before any guest run.
// Every rule here is transcribed from Boxer 2.0-alpha at 0062fc18; the source
// line is cited next to it so the transcription can be checked.
#ifndef BOXER_UI_LOGIC_H
#define BOXER_UI_LOGIC_H

#include <string>

namespace boxer_ui {

// BXSession+BXUIControls.h:20-34
constexpr int kMinSpeed = 50, k286Speed = 1000, k386Speed = 2500,
              k486Speed = 10000, kPentiumSpeed = 25000, kMaxSpeed = 62500;
constexpr int kAutoSpeed = -1;
constexpr int kMaxFrameskip = 9;

// BXBandedValueTransformer (BXValueTransformers.m:170-220) with the six
// thresholds registered in BXSession+BXUIControls.m:44-52.
double speedToSlider(double cycles);
double sliderToSpeed(double ratio);

// +incrementAmountForSpeed:goingUp:YES and +snappedSpeed: (:66-81). Note the
// strict ">" after adding 1, which puts e.g. 1000 itself into the 100 band.
int speedIncrement(int speed, bool goingUp);
int snappedSpeed(int rawSpeed);

// -setSliderSpeed: (:386-393): at or above the maximum the session switches
// to automatic throttling. -sliderSpeed reports kMaxSpeed in that mode.
int sliderValueToSessionSpeed(int sliderSpeed);
int sessionSpeedToSliderValue(int sessionSpeed);

// +descriptionForSpeed: / -frameskipDescription (:84-103, :729-739)
std::string speedDescription(int sessionSpeed, bool emulating);
std::string frameskipDescription(int frameskip, bool emulating);

// The whole slider change path as the panel performs it: knob ratio in,
// (snapped session speed, ratio the knob should be put back to) out.
struct SpeedChange { int sessionSpeed; double knobRatio; };
SpeedChange applySpeedSlider(double ratio, bool snap);

// BXDOSWindowController.m:1341-1360 windowWillResize: snap the render width
// UP to a multiple of the scaled base resolution when within the threshold,
// then keep the aspect ratio of the view.
constexpr int kWindowSnapThreshold = 96;   // BXWindowSnapThreshold
struct Size { int w, h; };
Size snapRenderSize(Size proposed, Size scaledResolution, double aspect);

// BXVideoFrame.m:29-49 scalingFactorForSize:toAspectRatio: with
// BX4by3AspectRatio (320/240) and BXIdenticalAspectRatioDelta 0.025, then
// -scaledSize (:94-98, roundf). Applied to every frame because both
// aspectCorrected and aspectCorrectedText default to YES
// (Resources/UserDefaults.plist; BXDOSWindowController.m:1229-1268).
// DOSBox's own scalex/scaley are not used: Boxer sets render.aspect = NO
// and applies this correction itself (BXVideoHandler.mm:368).
Size aspectCorrectedSize(Size frame, bool correct);

// BXGLRenderingView.m:198-221 viewportForFrame: fitInRect (sizeToFitSize,
// then centred with anchor 0.5,0.5; ADBGeometry.m:73-133), with no maximum
// viewport size. Coordinates are relative to the view, top-left origin.
struct Rect { int x, y, w, h; };
Rect viewportFor(Size scaled, Size canvas);

// BXInputController.m:820-872 mouseMoved: the position as a fraction of
// the canvas, clamped to 0..1 (_canvasBounds). The canvas here is the
// viewport rectangle (see comparison.md, "Mouse mapping").
struct Fraction { double x, y; };
Fraction canvasFraction(int x, int y, Rect canvas);

// BXStatusBarController.m:112-149
enum class LockIcon { Hidden, Unlocked, Locked };
struct StatusState { LockIcon icon; const char *message; };
StatusState statusFor(bool mouseActive, bool dosViewShown, bool locked,
                      bool trackWhileUnlocked);

// BXWelcomeView.m:20-39: radial NSGradient grey(0.15,0.17,0.20) -> black,
// both circles centred at (W/2, 0.15H - 1.5W) in y-up coordinates, radii
// 1.5W and 1.5W + 0.5H, extending before and after. x, y are top-left
// based view coordinates; returns 0xRRGGBB.
unsigned welcomeGradient(double x, double y, double W, double H);

// BXDOSWindowBackgroundView.m:28-131 (solid fill, vignette, top shadow).
unsigned dosBackground(double x, double y, double W, double H);

}  // namespace boxer_ui
#endif
