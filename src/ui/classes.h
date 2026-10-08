// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tomasz Staniak

// Zune classes of UI slice A. Each class uses the lowest tier that was
// enough (standard Zune class, then a subclass, then a new class); a new
// class exists only where the original's behaviour needed one.
#ifndef BOXER_UI_CLASSES_H
#define BOXER_UI_CLASSES_H

#include <exec/types.h>
#include <exec/tasks.h>
#include <intuition/classusr.h>
#include <libraries/mui.h>
#include <graphics/text.h>
#include <proto/intuition.h>

#include <cstdint>

#include "ui_logic.h"
#include "launchpanel_logic.h"

#include <vector>

// mui.h defines get/set/nnset only for C (#ifndef __cplusplus); these are
// the same calls as functions.
inline ULONG get(Object *o, ULONG attr, void *store) { return GetAttr(attr, o, (IPTR *)store); }
inline ULONG set(Object *o, ULONG attr, IPTR v) { return SetAttrs(o, attr, v, TAG_DONE); }
inline ULONG nnset(Object *o, ULONG attr, IPTR v) {
    return SetAttrs(o, MUIA_NoNotify, TRUE, attr, v, TAG_DONE);
}

namespace boxer_ui {

// Fonts chosen by the mapping proposal (comparison.md, "Font mapping").
// bold16: LauncherFavorite.xib's title (systemBold 16).
constexpr int kFontSlots = 6;
struct Fonts {
    struct TextFont *system13, *bold13, *small11, *mini9, *title32, *bold16;
    char names[kFontSlots][240];   // what actually opened, for the run log
};
extern Fonts g_fonts;
typedef void (*LogFn)(const char *fmt, ...);
// fontCheck: also measure every glyph against freetype2's engine (logged,
// development only; see openFonts in classes.cpp for why it is off by default).
bool openFonts(LogFn log, bool fontCheck = false);
void closeFonts();

bool createClasses();
void deleteClasses();

#define BX_TAG(n) (TAG_USER | 0x0B0E0000 | (n))

// PaintGroup: Group subclass whose background is drawn procedurally.
enum PaintMode { PM_Welcome, PM_BottomBar, PM_Toolbar, PM_Panel, PM_Box, PM_VolumeBox, PM_LaunchBar };
#define PGA_Mode   BX_TAG(1)
#define PGA_Layout BX_TAG(2)        // const AbsLayout *: AppKit-style layout hook
#define PGM_Tick   BX_TAG(3)        // method: calls g_tick (target of a timer input handler)
extern void (*g_tick)();

// Autoresizing bits with AppKit's meaning, expressed for a top-left origin.
enum { FlexMinX = 1, FlexW = 2, FlexMaxX = 4, FlexTop = 8, FlexH = 16, FlexBottom = 32 };
struct AbsItem { int x, y, w, h; unsigned mask; };
struct AbsLayout {
    int baseW, baseH;          // the xib size the items are expressed in
    int minW, minH, maxW, maxH;
    const AbsItem *items;
    int count;
};

// Label: text with RGB colour, optional 1px shadow, alignment, multi-line.
#define LA_Text     BX_TAG(10)

// GlyphButton: textured/segmented/recessed/inline/help bezels.
enum GlyphStyle { GS_RoundTextured, GS_SegLeft, GS_SegMid, GS_SegRight,
                  GS_Recessed, GS_Inline, GS_Help };
#define GBA_Hidden  BX_TAG(25)

// WelcomeButton
#define WBM_SyncHover BX_TAG(33)  // method: re-evaluate hover (window (in)active)

extern struct MUI_CustomClass *mccPaintGroup;
extern void (*g_renderClick)(bool commandHeld);
extern volatile bool g_renderResized;
extern struct Task *g_mainTask;

Object *newLabel(const char *text, struct TextFont *font, unsigned rgb, int align,
                 int w, int h, unsigned shadow = ~0u, int shadowDY = 0, bool vcenter = false);
Object *newGlyphButton(int glyph, int style, int w, int h, ULONG inputMode, int altGlyph = -1);
Object *newTextButton(const char *text, int style, int w, int h, bool pulldown);
Object *newWelcomeButton(int art, const char *title, Object *view);
Object *newToolTab(int icon, const char *label);
// NSSlider small, linear: Numeric subclass (see classes.cpp, BxSlider).
// tickPos 0 none (round knob), 1 below, 2 above (pointer knob).
Object *newSlider(LONG min, LONG max, LONG value, int w, int h, int ticks, int tickPos,
                  bool ticksOnly, bool focusRing);
constexpr int kSliderRing = 3;   // focus-ring margin around a slider with a ring
bool sliderState(Object *slider, bool *hover, bool *pressed, bool *focused);
void setAuditLog(LogFn log);
void auditTextBox(const char *kind, struct TextFont *font, const char *s, int len,
                  int x, int baseline, int boxL, int boxT, int boxW, int boxH);
Object *newRenderView(bool fullscreen);

// A core frame for the render view: 0xFFRRGGBB pixels (BGRA bytes), the
// output size and the aspect-corrected size it is shown at (4:3 rule).
struct RenderFrame {
    const uint32_t *pixels;
    int w, h, pitch;          // pitch in bytes
    int scaledW, scaledH;
    bool smooth;              // Smoothed (bilinear) instead of Normal (nearest)
};
// Shows f (null: back to the test pattern) and redraws. The pixels must stay
// valid until the next call; the view keeps the pointer for exposures.
void renderSetFrame(Object *renderView, const RenderFrame *f);
// The viewport inside the view, relative to its top-left (_mleft/_mtop).
Rect renderViewport(Object *renderView);

// Session input from a render view. Coordinates are view-relative. Return
// true to consume the event (MUI_EventHandlerRC_Eat).
struct RenderEvents {
    bool (*rawKey)(Object *view, unsigned code, bool up, unsigned qualifier);
    void (*mouseMove)(Object *view, int x, int y, unsigned qualifier);
    bool (*button)(Object *view, int button, bool down, unsigned qualifier, int x, int y, bool inside);
    // Amiga-qualified key presses (the original's Cmd key equivalents that
    // need the key's release, such as fast forward while held).
    bool (*commandKey)(Object *view, unsigned code, bool up, unsigned qualifier);
};
extern RenderEvents g_renderEvents;

void setLabelText(Object *label, const char *text);

// Cover well (BXCoverArtWell / BXImportIconDropzone): shows an RGBA cover,
// centred; a click sends MUIA_Pressed. The pixels are copied.
Object *newCoverWell();
void coverWellSetImage(Object *well, const uint8_t *rgba, int w, int h);

// ---- Launch panel (LaunchPanel.xib, LauncherHeading/Favorite/Item.xib) ----
// The panel: a 40 pt search bar (PaintGroup) over a Scrollgroup whose
// contents are a LauncherList (Virtgroup subclass) of LauncherItems. The
// background is the DOS window's (BXDOSWindowBackgroundView: flat
// 97/98/103 with the top-centre lighting and the 6 pt top shadow).
Object *newLaunchPanel(Object **searchField, Object **list);
// Replaces the rows (InitChange/ExitChange). launchable: the session can
// open programs now (BXSession canOpenURLs); rows keep their index into
// rows for g_launcherAction.
void launchPanelSetRows(Object *list, const std::vector<LPRow> &rows, bool launchable);
void launchPanelSetLaunchable(Object *list, bool launchable);
// BXLauncherItem actions (openItemInDOS:, removeItem:), for the row at
// index. Called from inside Zune's input handling: defer real work.
enum { LA_Open = 1, LA_Remove = 2 };
extern void (*g_launcherAction)(int row, int action);
// Rows currently shown with their on-screen rectangle (window-relative),
// for the run log and for aimed clicks in tests. visible: inside the list.
struct LPRowGeom { int index; int x, y, w, h; bool visible; };
std::vector<LPRowGeom> launchPanelGeometry(Object *list);
bool windowActive(Object *obj);

}  // namespace boxer_ui
#endif
