// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer 2.0-alpha UI controllers and nibs),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

// BoxerUI: the Zune front end (Welcome window, DOS session window, Inspector
// CPU panel) with the embedded DOSBox core running on the main task.
//
// Usage (Shell, KEY=value or KEY value; the same keys as icon ToolTypes):
//   BoxerUI [SCREEN=welcome|dos|inspector|all] [LOG=<file>] [STYLE=normal|smoothed]
//           [GAMEBOX=<path> DATA=<dir> CONFDIR=<dir>] | [SESSION=dos CONFDIR=<dir>]
//           [MSG=<table>] [OPTCONF=<file>]... [PRE=<cmd>]... [CMD=<cmd>]... [THEN=<cmd>]...
//           [FONTCHECK=1] [HARNESS=1]
// DATADIR=<dir> sets the one user data directory (stored in
// ENVARC:Boxer/Boxer.prefs) without a requester; DATA=<dir> overrides it
// for one session. PREFSONLY=1 stores DATADIR and exits; PREFS=<file> and
// PREFSFAULT=<step>:fail|crash (with HARNESS=1) are for the data-directory tests.
// GAMEBOX opens a gamebox session (writes shadowed into DATA), SESSION=dos a
// plain DOS prompt; without either the windows show the slice-A test pattern
// and "Open a DOS prompt" starts a session when CONFDIR is known. The core
// keeps global state, so one session per process (a new
// process per session).
//
// Product shortcuts:
// RAmiga+Q quit, RAmiga+F full screen, RAmiga+L and RAmiga+click mouse lock,
// RAmiga+P pause, RAmiga+G launch panel (toggleLaunchPanel:),
// RAmiga+Shift+A aspect correction, RAmiga+I Inspector, RAmiga+Alt+F fast
// forward while held. Volume, CPU speed and disc changes have no keys.
//
// Harness keys (not part of the original UI) exist only with HARNESS=1
// (Shell argument or ToolType); without it F1-F5 and every plain key go to
// DOS. F1 mouse active on/off, F2 track-while-unlocked on/off, F3 dump
// geometry, slider states and the text audit to the log, F5 the same dump
// 2.5 s later, F4 in the Inspector toggles "emulating". In a session:
// RAmiga+D dump, RAmiga+S Normal/Smoothed, RAmiga+H/J/K render area
// 640x480 / 720x360 without aspect snapping (letterbox) / 480x360. They
// never use a product shortcut's key.
#include "classes.h"
#include "gfx.h"
#include "ui_logic.h"
#include "launchpanel_logic.h"
#include "coverassets.h"
#include "../emulator/emulator.h"
#include "../emulator/filesystem.h"
#include "../model/coverart.h"
#include "../model/coverfont.h"
#include "../model/datalocations.h"
#include "../model/gameboxrename.h"
#include "../model/importsource.h"
#include "../model/installerscan.h"
#include "../model/sourcecopy.h"
#include "../model/fsutil.h"
#include "../model/gamebox.h"
#include "../model/programs.h"
#include "../model/shadowfs.h"
#include "../platform/aros/coreinput.h"
#include "../platform/aros/coverio.h"
#include "../platform/aros/corecontrol.h"
#include "../platform/aros/session_setup.h"
#include "../platform/aros/wbopen.h"

#include <proto/exec.h>
#include <exec/execbase.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <libraries/mui.h>
#include <proto/icon.h>
#include <proto/workbench.h>
#include <proto/asl.h>
#include <libraries/asl.h>
#include <proto/input.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <devices/input.h>
#include <devices/inputevent.h>

#include <cctype>
#include <cmath>
#include <string>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <cstring>

using namespace boxer_ui;

static FILE *g_log;
// Every line starts with the guest time since the first line, in seconds
// (DateStamp, 1/50 s), so UI actions can be placed inside a recording.
static long g_logT0 = -1;
static void logf(const char *fmt, ...) {
    if (!g_log) return;
    struct DateStamp ds;
    DateStamp(&ds);
    long t = (ds.ds_Days * 1440L + ds.ds_Minute) * 60L * TICKS_PER_SECOND + ds.ds_Tick;
    if (g_logT0 < 0) g_logT0 = t;
    std::fprintf(g_log, "[%7.2f] ", (t - g_logT0) / (double)TICKS_PER_SECOND);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_log, fmt, ap);
    va_end(ap);
    std::fputc('\n', g_log);
    std::fflush(g_log);   // a crash must not take the evidence with it
}

// Which GL/SDL/audio/controller libraries are in memory and how often they
// are open: the SDL3 build must not bring gl.library or Mesa in at all
// (SDL3 migration requirement). Read under Forbid from
// exec's lists, so the check itself opens nothing.
static void logLibraries(const char *when) {
    static const char *const watch[] = {"gl.library", "mesa3dgl.library", "mesa3dgl26-0.library",
        "SDL2.library", "sdl3.library", "lowlevel.library", "controller.hidd", "ahi.device", nullptr};
    char line[400];
    int len = std::snprintf(line, sizeof line, "libs[%s]:", when);
    Forbid();
    for (int i = 0; watch[i]; i++) {
        struct Node *n = FindName(&SysBase->LibList, watch[i]);
        if (!n) n = FindName(&SysBase->DeviceList, watch[i]);
        if (n && len < (int)sizeof line)
            len += std::snprintf(line + len, sizeof line - len, " %s=%u", watch[i],
                                 (unsigned)((struct Library *)n)->lib_OpenCnt);
    }
    Permit();
    logf("%s", line);
}

#ifdef BOXER_MEMTRACE
// Memory checkpoints (build with -DBOXER_MEMTRACE, off by default): each
// checkpoint appends "<n> <tag> <AvailMem(MEMF_ANY)>" to the file named by
// the variable BOXER_MEMLOG; BOXER_MEMSTOP=<n> ends main cleanly right
// after checkpoint n, so a per-start loss can be bisected by start-up stage
// from outside. exec/dos only: the probe
// must not allocate through the C runtime it is measuring around.
static BPTR g_memFh;
static LONG g_memStop = -1, g_memN = 0;
static void memInit() {
    char b[256];
    if (GetVar((STRPTR)"BOXER_MEMLOG", (STRPTR)b, sizeof b, 0) > 0) {
        g_memFh = Open((STRPTR)b, MODE_READWRITE);
        if (g_memFh) Seek(g_memFh, 0, OFFSET_END);
    }
    if (GetVar((STRPTR)"BOXER_MEMSTOP", (STRPTR)b, sizeof b, 0) > 0) StrToLong((STRPTR)b, &g_memStop);
}
static bool memcp(const char *tag) {
    ULONG a = AvailMem(MEMF_ANY);
    if (g_memFh) FPrintf(g_memFh, (STRPTR)"%ld %s %lu\n", (IPTR)g_memN, (IPTR)tag, (IPTR)a);
    return g_memN++ == g_memStop;
}
static void memDone() { if (g_memFh) { Close(g_memFh); g_memFh = 0; } }
#else
static inline void memInit() {}
static inline bool memcp(const char *) { return false; }
static inline void memDone() {}
#endif

enum {
    ID_QUIT = 1, ID_WELCOME_CLOSE, ID_BROWSE, ID_IMPORT, ID_PROMPT, ID_RECENT_PICK,
    ID_PROGRAMS, ID_MANUALS, ID_SEG0, ID_SEG1, ID_SEG2, ID_VOL_MIN, ID_VOL_MAX, ID_VOLUME,
    ID_INSPECTOR, ID_INSPECTOR_CLOSE, ID_FULLSCREEN, ID_LOCK_BUTTON, ID_LOCK_KEY,
    ID_MOUSE_ACTIVE, ID_TRACK, ID_DUMP, ID_TAB0, ID_TAB4 = ID_TAB0 + 4, ID_SPEED, ID_FRAMES,
    ID_DYNAMIC, ID_HELP, ID_DOS_CLOSE, ID_ACTIVATION, ID_FS_CLOSE, ID_EMULATING,
    ID_DUMP_LATER, ID_STYLE, ID_SIZE1, ID_SIZE2, ID_SIZE3, ID_ASPECT,
    ID_LP_FILTER, ID_LP_ENTER, ID_LP_ACTION, ID_LP_TOGGLE,
    ID_IMPORT_CHOOSE, ID_IMPORT_BACK, ID_IMPORT_CLOSE, ID_IMPORT_SKIP, ID_IMPORT_BACK2,
    ID_IMPORT_LAUNCH, ID_IMPORT_USE, ID_IMPORT_DONE_CLOSE, ID_IMPORT_CREATE, ID_IMPORT_STOP, ID_IMPORT_LAUNCH_GAME,
    ID_IMPORT_NAME, ID_IMPORT_COVER, ID_IMPORT_WELL, ID_INSP_NAME, ID_INSP_COVER, ID_INSP_WELL
};

// ------------------------------------------------------------- layouts ---
// Geometry measured from the original's xibs (Boxer 0062fc18),
// converted to top-left origin. Autoresizing masks follow the xibs.
static const AbsItem kWelcomeViewItems[] = {
    {17, 15, 486, 39, FlexW | FlexBottom},
    {20, 62, 160, 186, FlexMaxX | FlexBottom},
    {180, 62, 160, 186, FlexMinX | FlexMaxX | FlexBottom},
    {340, 62, 160, 186, FlexMinX | FlexBottom},
};
static const AbsLayout kWelcomeView = {520, 268, 520, 268, 520, 268, kWelcomeViewItems, 4};
static const AbsItem kWelcomeBarItems[] = {
    {13, 8, 115, 25, FlexMaxX},
    {435, 8, 70, 25, FlexMinX},
};
static const AbsLayout kWelcomeBar = {520, 40, 520, 40, 520, 40, kWelcomeBarItems, 2};

static const AbsItem kStatusItems[] = {
    {7, 5, 24, 17, FlexMaxX},
    {33, 6, 321, 14, FlexMinX | FlexMaxX},
};
static const AbsLayout kStatusBar = {640, 26, 160, 26, MUI_MAXMAX, 26, kStatusItems, 2};

// The volume box's subviews sit at y=5/7 from the bottom of a 22 pt box.
static const AbsItem kVolumeItems[] = {
    {6, 0, 22, 17, 0},
    {29, 0, 72, 15, 0},
    {102, 0, 27, 17, 0},
};
static const AbsLayout kVolumeBox = {130, 22, 130, 22, 130, 22, kVolumeItems, 3};

static const AbsItem kCPUItems[] = {
    {20, 37, 87, 17, 0},      // "CPU speed:"
    {109, 40, 167, 14, 0},    // speed description
    // Sliders: the xib frame grown by the focus-ring margin on every side
    // (kSliderRing; the control itself keeps the xib size, see BxSlider).
    {20 - kSliderRing, 63 - kSliderRing, 256 + 2 * kSliderRing, 17 + 2 * kSliderRing, 0},  // speed, 11 ticks below
    {20, 83, 36, 11, 0},      // XT
    {53, 83, 44, 11, 0},      // AT
    {102, 83, 44, 11, 0},     // 386
    {150, 83, 44, 11, 0},     // 486
    {199, 83, 44, 11, 0},     // Pentium
    {244, 83, 36, 11, 0},     // Max
    {0, 112, 296, 86, 0},     // options box (xib -1,…,298: side edges off-panel)
    {20, 214, 126, 17, 0},    // "Frame rate:"
    {150, 217, 126, 14, 0},   // frameskip description
    {20 - kSliderRing, 240 - kSliderRing, 256 + 2 * kSliderRing, 18 + 2 * kSliderRing, 0},  // frame rate, 10 ticks above
    {20, 265, 256, 28, 0},    // frame-rate help
    {254, 391, 25, 25, 0},    // help button
};
static const AbsLayout kCPUPage = {296, 432, 296, 432, 296, 432, kCPUItems, 15};
static const AbsItem kBoxItems[] = {
    {20, 18, 256, 18, 0},     // Optimize checkbox
    {20, 42, 256, 28, 0},     // its help text
};
static const AbsLayout kOptionsBox = {296, 86, 296, 86, 296, 86, kBoxItems, 2};
// Gamebox panel (Inspector.xib "Gamebox Panel", 296x432): the cover well
// at x=84 (128 pt plus a 6 pt highlight margin), its help text, and the
// launch box at y=259. AROS adds the name field and the cover choice
// between them (the original renames a gamebox in the Finder), so the
// well sits higher than its xib y=56.
static const AbsItem kGameboxItems[] = {
    {78, 12, 140, 140, 0},    // cover well
    {20, 156, 256, 29, 0},    // help text
    {20, 190, 256, 22, 0},    // name (AROS)
    {20, 216, 256, 22, 0},    // cover choice (AROS)
    {20, 241, 256, 14, 0},    // pending-rename note (AROS)
    {0, 259, 296, 134, 0},    // launch box (xib -1,…,298: side edges off-panel)
};
static const AbsLayout kGameboxPage = {296, 432, 296, 432, 296, 432, kGameboxItems, 6};
static const AbsItem kLaunchBoxItems[] = {
    {20, 10, 256, 17, 0},     // "When starting up, launch:"
    {20, 33, 256, 26, 0},     // program popup
    {21, 64, 230, 18, 0},     // "Close window after exiting"
};
static const AbsLayout kLaunchBox = {296, 134, 296, 134, 296, 134, kLaunchBoxItems, 3};
static const AbsItem kPlaceholderItems[] = {{20, 200, 256, 32, 0}};
static const AbsLayout kPlaceholder = {296, 432, 296, 432, 296, 432, kPlaceholderItems, 1};

// ---------------------------------------------------------------- state ---
static struct {
    Object *app, *welcome, *dos, *fs, *insp;
    Object *welcomeView, *wb[3], *recentPop, *recentList, *closeBtn, *recentBtn;
    Object *toolbar, *status, *render, *fsRender, *lockBtn, *statusText;
    Object *programs, *manuals, *seg[3], *volSlider, *volMin, *volMax, *inspBtn, *fsBtn;
    Object *tabbar, *tabs[5], *pages, *speedSlider, *speedDesc, *frameSlider, *frameDesc;
    Object *dynamic, *helpBtn, *volBox;
    Object *dosPages, *lpPanel, *lpSearch, *lpList;   // launch panel (BXLaunchPanelController)
    // Import window (ImportWindow.xib): a standard Window with a
    // PageMode group, one page per import stage.
    Object *imp, *impPages, *impChoose, *impBack, *impSource, *impLaunch, *impSkip;
    Object *impList, *impReady, *impBack2, *impCreate;
    Object *impProgText, *impProgList, *impUse, *impDoneText, *impLaunchGame, *impDoneClose;
    Object *impCopyText, *impGauge, *impStop;
    // Name and cover on the finished panel (ImportFinishedPanel) and in
    // the Inspector's Gamebox tab (Inspector.xib "Gamebox Panel").
    Object *impWell, *impName, *impCover;
    Object *inspWell, *inspName, *inspCover, *inspNote, *inspLaunch, *inspCloseOnExit;
} ui;

static struct {
    bool mouseActive = true, locked = false, track = true, paused = false;
    bool fullscreen = false, launchPanel = false, emulating = true;
    int speed = 3000, frameskip = 0, tab = 1, playback = 1;
} st;

static std::string g_gamesFolder;   // from the prefs; "" until chosen
static boxer::ImportSession g_import;
// The installer runs in this process's one core session; the main loop
// starts it, and these say how the user ended it.
static struct { bool pending = false, finish = false, stop = false; } g_inst;
// The source copy runs in steps from the main loop so the Gauge moves and
// Stop is heard between them (cancelSourceFileImport, IS:1070).
static boxer::SourceCopy *g_copy;

static const char *kTabLabels[5] = {"Gamebox", "CPU", "Mouse", "Joystick", "Drives"};
static const char *kTabTitles[5] = {"Gamebox Inspector", "CPU Inspector", "Mouse Inspector",
                                    "Joystick Inspector", "Drives Inspector"};
static char g_dosTitle[64];
static char g_speedText[64], g_frameText[64];

static std::string g_sessionTitle = "BOXTEST";   // the slice-A stand-in until a session names it
static void updateTitle() {
    std::snprintf(g_dosTitle, sizeof g_dosTitle, st.paused ? "%s (Paused)" : "%s", g_sessionTitle.c_str());
    set(ui.dos, MUIA_Window_Title, (IPTR)g_dosTitle);
}

static void updateStatus() {
    StatusState s = statusFor(st.mouseActive, true, st.locked, st.track);
    set(ui.lockBtn, GBA_Hidden, s.icon == LockIcon::Hidden);
    nnset(ui.lockBtn, MUIA_Selected, st.locked);
    MUI_Redraw(ui.lockBtn, MADF_DRAWOBJECT);
    setLabelText(ui.statusText, s.message);
    logf("status: mouseActive=%d locked=%d track=%d icon=%d text=\"%s\"", st.mouseActive,
         st.locked, st.track, (int)s.icon, s.message);
}

static void renderClicked(bool cmd) {
    if (!st.mouseActive) return;
    // BXInputController: a locked pointer is released with Cmd+click; an
    // unlocked one locks on Cmd+click while tracked, on any click otherwise.
    if (st.locked) { if (cmd) st.locked = false; }
    else if (!st.track || cmd) st.locked = true;
    logf("render click: amiga=%d -> locked=%d", cmd, st.locked);
    updateStatus();
}

static void updateSpeedText() {
    std::snprintf(g_speedText, sizeof g_speedText, "%s", speedDescription(st.speed, st.emulating).c_str());
    setLabelText(ui.speedDesc, g_speedText);
    std::snprintf(g_frameText, sizeof g_frameText, "%s",
                  frameskipDescription(st.frameskip, st.emulating).c_str());
    setLabelText(ui.frameDesc, g_frameText);
}

// The Inspector's static texts and descriptions are Labels (classes.cpp),
// not Zune Text objects: Zune centres a font's whole cell in the frame,
// which for BoxerSans (taller cell than ink, see openFonts) pushes ink out of
// the xib frames; Labels place the ink itself and audit it when drawn.
static Object *textObj(const char *s, struct TextFont *f, int align, int w, int h) {
    return newLabel(s, f, 0x000000, align, w, h);
}

static int textWidthOf(struct TextFont *f, const char *s) {
    struct RastPort rp;
    InitRastPort(&rp);
    SetFont(&rp, f);
    return TextLength(&rp, (STRPTR)s, (int)std::strlen(s));
}

static void dumpSlider(const char *name, Object *o, Object *win) {
    // _window() reads render info that a closed window no longer has
    // (crashed here on mainline v1 when the Inspector was closed at exit).
    IPTR open = 0;
    get(win, MUIA_Window_Open, &open);
    if (!open) return;
    bool hv = false, pr = false, fo = false;
    sliderState(o, &hv, &pr, &fo);
    IPTR v = 0, dis = 0;
    get(o, MUIA_Numeric_Value, &v);
    get(o, MUIA_Disabled, &dis);
    logf("slider %s: value=%d hover=%d pressed=%d focused=%d disabled=%d", name, (int)(LONG)v,
         hv, pr, fo, (int)dis);
}

static void selectTab(int i) {
    st.tab = i;
    for (int k = 0; k < 5; ++k) nnset(ui.tabs[k], MUIA_Selected, k == i);
    for (int k = 0; k < 5; ++k) MUI_Redraw(ui.tabs[k], MADF_DRAWOBJECT);
    set(ui.pages, MUIA_Group_ActivePage, i);
    set(ui.insp, MUIA_Window_Title, (IPTR)kTabTitles[i]);
    logf("inspector tab: %d (%s)", i, kTabLabels[i]);
}

static void dumpObj(const char *name, Object *o) {
    IPTR win = 0;
    if (!o) return;
    struct Window *w = _window(o);
    get(o, MUIA_WindowObject, &win);
    if (!w) { logf("geom %s: not shown", name); return; }
    logf("geom %s: x=%d y=%d w=%d h=%d min %dx%d (window inner origin %d,%d)", name,
         _left(o) - w->BorderLeft, _top(o) - w->BorderTop, _width(o), _height(o),
         _minwidth(o), _minheight(o), w->BorderLeft, w->BorderTop);
}

static void dumpWindow(const char *name, Object *win) {
    IPTR open = 0;
    get(win, MUIA_Window_Open, &open);
    struct Window *w = nullptr;
    get(win, MUIA_Window_Window, &w);
    if (!open || !w) { logf("window %s: closed", name); return; }
    logf("window %s: pos %d,%d outer %dx%d inner %dx%d borders l%d t%d r%d b%d active=%d", name,
         w->LeftEdge, w->TopEdge, w->Width, w->Height,
         w->Width - w->BorderLeft - w->BorderRight, w->Height - w->BorderTop - w->BorderBottom,
         w->BorderLeft, w->BorderTop, w->BorderRight, w->BorderBottom,
         (w->Flags & WFLG_WINDOWACTIVE) ? 1 : 0);
}

static int g_dumpSerial;
static void logButtons();
static void dumpGeometry(const char *why = "F3") {
    // Numbered, with the trigger and the live mouse-button qualifier, so a
    // dump that never happened (e.g. a key lost while a button is held) is
    // visible as a gap, and a dump's button state can be checked.
    logf("--- geometry dump #%d (%s) ---", ++g_dumpSerial, why);
    logButtons();
    dumpWindow("welcome", ui.welcome);
    dumpWindow("dos", ui.dos);
    dumpWindow("inspector", ui.insp);
    dumpWindow("fullscreen", ui.fs);
    IPTR o = 0;
    get(ui.welcome, MUIA_Window_Open, &o);
    if (o) {
        dumpObj("welcome.view", ui.welcomeView);
        for (int i = 0; i < 3; ++i) { char n[32]; std::snprintf(n, 32, "welcome.button%d", i); dumpObj(n, ui.wb[i]); }
        dumpObj("welcome.openRecent", ui.recentPop);
        dumpObj("welcome.close", ui.closeBtn);
    }
    get(ui.dos, MUIA_Window_Open, &o);
    if (o) {
        dumpObj("dos.toolbar", ui.toolbar);
        dumpObj("dos.programs", ui.programs);
        dumpObj("dos.manuals", ui.manuals);
        for (int i = 0; i < 3; ++i) { char n[32]; std::snprintf(n, 32, "dos.segment%d", i); dumpObj(n, ui.seg[i]); }
        dumpObj("dos.volumeBox", ui.volBox);
        dumpObj("dos.volumeSlider", ui.volSlider);
        dumpObj("dos.inspector", ui.inspBtn);
        dumpObj("dos.fullscreen", ui.fsBtn);
        dumpObj("dos.render", ui.render);
        dumpObj("dos.statusbar", ui.status);
        dumpObj("dos.lock", ui.lockBtn);
        dumpObj("dos.statusText", ui.statusText);
    }
    get(ui.insp, MUIA_Window_Open, &o);
    if (o) {
        dumpObj("insp.tabbar", ui.tabbar);
        for (int i = 0; i < 5; ++i) { char n[32]; std::snprintf(n, 32, "insp.tab%d", i); dumpObj(n, ui.tabs[i]); }
        dumpObj("insp.pages", ui.pages);
        dumpObj("insp.speedSlider", ui.speedSlider);
        dumpObj("insp.speedDesc", ui.speedDesc);
        dumpObj("insp.frameSlider", ui.frameSlider);
        dumpObj("insp.dynamic", ui.dynamic);
        dumpObj("insp.help", ui.helpBtn);
    }
    dumpSlider("volume", ui.volSlider, ui.dos);
    dumpSlider("speed", ui.speedSlider, ui.insp);
    dumpSlider("frame", ui.frameSlider, ui.insp);
    IPTR v = 0;
    get(ui.speedSlider, MUIA_Numeric_Value, &v);
    logf("state: emulating=%d speed=%d slider=%d frameskip=%d tab=%d paused=%d playback=%d fullscreen=%d",
         st.emulating, st.speed, (int)v, st.frameskip, st.tab, st.paused, st.playback, st.fullscreen);
}

static bool g_noSnap;   // harness: letterbox test (RAmiga+2)
static void correctAspect() {
    if (g_noSnap) { logf("resize: aspect snapping off (harness)"); return; }
    // windowWillResize:toSize: equivalent. AROS has no aspect constraint for
    // a live drag, so the window is corrected once Zune has laid it out.
    struct Window *w = _window(ui.render);
    if (!w) return;
    Size cur = {_mwidth(ui.render), _mheight(ui.render)};
    Size want = snapRenderSize(cur, {320, 240}, 4.0 / 3.0);
    if (want.w == cur.w && want.h == cur.h) { logf("resize: render %dx%d ok", cur.w, cur.h); return; }
    int nw = w->Width + (want.w - cur.w), nh = w->Height + (want.h - cur.h);
    struct Screen *s = w->WScreen;
    if (w->LeftEdge + nw > s->Width || w->TopEdge + nh > s->Height) {
        // Too large for the screen: shrink to the largest 4:3 that fits.
        int maxW = s->Width - w->LeftEdge - (w->Width - cur.w);
        int maxH = s->Height - w->TopEdge - (w->Height - cur.h);
        Size fit = {maxW, (int)std::lround(maxW * 3.0 / 4.0)};
        if (fit.h > maxH) fit = {(int)std::lround(maxH * 4.0 / 3.0), maxH};
        want = fit;
        nw = w->Width + (want.w - cur.w); nh = w->Height + (want.h - cur.h);
    }
    logf("resize: render %dx%d -> %dx%d (window %dx%d -> %dx%d)", cur.w, cur.h, want.w, want.h,
         w->Width, w->Height, nw, nh);
    ChangeWindowBox(w, w->LeftEdge, w->TopEdge, nw, nh);
}

static void (*g_onFullscreenChanged)();
static void setFullscreen(bool on) {
    if (on == st.fullscreen) return;
    st.fullscreen = on;
    // Fullscreen hides the status bar and the toolbar (AutoHideToolbar); a
    // borderless screen-sized window carries only the render view. The new
    // window opens before the old one closes, so a session window is
    // active throughout and the switch is not a deactivation.
    if (on) { set(ui.fs, MUIA_Window_Open, TRUE); set(ui.dos, MUIA_Window_Open, FALSE); }
    else { set(ui.dos, MUIA_Window_Open, TRUE); set(ui.fs, MUIA_Window_Open, FALSE); }
    logf("fullscreen: %d", on);
    if (g_onFullscreenChanged) g_onFullscreenChanged();
}

static Object *buildWelcome() {
    ui.welcomeView = (Object *)NewObject(mccPaintGroup->mcc_Class, NULL,
        PGA_Mode, PM_Welcome, PGA_Layout, (IPTR)&kWelcomeView, TAG_DONE);
    // Children need the view pointer for gradient offsets, so they are added after.
    Object *title = newLabel("Welcome to Boxer.", g_fonts.title32, 0xFFFFFF, 1, 486, 39, ~0u, 0, true);
    ui.wb[0] = newWelcomeButton((int)WelcomeArt::GameFolder, "Browse your games", ui.welcomeView);
    ui.wb[1] = newWelcomeButton((int)WelcomeArt::Import, "Import a new game", ui.welcomeView);
    ui.wb[2] = newWelcomeButton((int)WelcomeArt::Prompt, "Open a DOS prompt", ui.welcomeView);
    DoMethod(ui.welcomeView, OM_ADDMEMBER, (IPTR)title);
    for (Object *b : ui.wb) DoMethod(ui.welcomeView, OM_ADDMEMBER, (IPTR)b);

    ui.recentBtn = newTextButton("Open recent", GS_RoundTextured, 115, 25, true);
    ui.recentList = ListObject, MUIA_Frame, MUIV_Frame_InputList,
        MUIA_List_SourceArray, (IPTR)(new const char *[5]{
            "BOXTEST (stand-in entry)", "Dune (stand-in entry)", "-----", "Clear Menu", nullptr}),
        End;
    ui.recentPop = PopobjectObject,
        MUIA_Popstring_Button, (IPTR)ui.recentBtn,
        MUIA_Popobject_Object, (IPTR)(ListviewObject, MUIA_Listview_List, (IPTR)ui.recentList, End),
        End;
    ui.closeBtn = newTextButton("Close", GS_RoundTextured, 70, 25, false);
    Object *bar = (Object *)NewObject(mccPaintGroup->mcc_Class, NULL,
        PGA_Mode, PM_BottomBar, PGA_Layout, (IPTR)&kWelcomeBar,
        Child, (IPTR)ui.recentPop, Child, (IPTR)ui.closeBtn, TAG_DONE);

    return WindowObject,
        MUIA_Window_Title, (IPTR)"Welcome to Boxer",
        MUIA_Window_LeftEdge, MUIV_Window_LeftEdge_Centered,
        MUIA_Window_TopEdge, MUIV_Window_TopEdge_Centered,
        MUIA_Window_SizeGadget, FALSE,
        MUIA_Window_RootObject, (IPTR)(VGroup, MUIA_Group_Spacing, 0,
            MUIA_InnerLeft, 0, MUIA_InnerRight, 0, MUIA_InnerTop, 0, MUIA_InnerBottom, 0,
            Child, (IPTR)ui.welcomeView, Child, (IPTR)bar, End),
        End;
}

// The cover choice next to the well (an AROS addition: the original picks
// the bootleg template itself, BXSession.m:186-201, and takes a picture by
// drag and drop, which this increment does not have). Index order is
// kCoverStyles; the last entry asks for a picture.
static const char *kCoverEntries[] = {"Automatic", "CD-ROM case", "3.5\" diskette", "5.25\" diskette",
                                      "Own picture...", nullptr};
static const boxer::CoverStyle kCoverStyles[] = {boxer::CoverStyle::Automatic, boxer::CoverStyle::JewelCase,
                                                 boxer::CoverStyle::Diskette35, boxer::CoverStyle::Diskette525,
                                                 boxer::CoverStyle::Picture};

static Object *newNameField() {
    // NSTextField: the gamebox name, committed with Return (IS:457).
    return StringObject, StringFrame, MUIA_String_MaxLen, 101, MUIA_CycleChain, 1,
        MUIA_FixWidth, 256, End;
}

static Object *newCoverCycle() {
    return CycleObject, MUIA_Cycle_Entries, (IPTR)kCoverEntries, MUIA_CycleChain, 1, End;
}

// Pages follow BXImportWindowController.m:75-104: dropzone while waiting
// for a source, installer panel once the source is loaded. AROS: no drop
// target yet (deferred), so the dropzone page offers only the chooser.
static Object *buildImport() {
    ui.impChoose = SimpleButton("Choose game folder...");
    ui.impBack = SimpleButton("Back");
    ui.impLaunch = SimpleButton("Launch installer");
    ui.impSkip = SimpleButton("Skip installer");
    ui.impBack2 = SimpleButton("Back");
    ui.impCreate = SimpleButton("Import game");
    ui.impSource = TextObject, MUIA_Text_Contents, (IPTR)"", MUIA_Text_SetMin, FALSE, End;
    // Two lines from the start: a Text object takes its height from the
    // contents it has when the window is laid out, and the ready text set
    // later has two lines.
    ui.impReady = TextObject, MUIA_Text_PreParse, (IPTR)"\33c", MUIA_Text_Contents, (IPTR)" \n ", MUIA_Text_SetMin, FALSE, End;
    // Installer choice: a standard single-select List rather than a
    // Cycle: a list shows every candidate with its folder at once.
    ui.impList = ListObject, MUIA_Frame, MUIV_Frame_InputList,
        MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
        MUIA_List_DestructHook, MUIV_List_DestructHook_String, End;
    ui.impUse = SimpleButton("Use this program");
    ui.impLaunchGame = SimpleButton("Launch game");
    ui.impDoneClose = SimpleButton("Close");
    ui.impProgText = TextObject, MUIA_Text_Contents, (IPTR)"", MUIA_Text_SetMin, FALSE, End;
    ui.impDoneText = TextObject, MUIA_Text_PreParse, (IPTR)"\33c", MUIA_Text_Contents, (IPTR)"", MUIA_Text_SetMin, FALSE, End;
    ui.impProgList = ListObject, MUIA_Frame, MUIV_Frame_InputList,
        MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
        MUIA_List_DestructHook, MUIV_List_DestructHook_String, End;
    ui.impStop = SimpleButton("Stop importing");
    ui.impWell = newCoverWell();
    ui.impName = newNameField();
    ui.impCover = newCoverCycle();
    ui.impCopyText = TextObject, MUIA_Text_PreParse, (IPTR)"\33c", MUIA_Text_Contents, (IPTR)"", MUIA_Text_SetMin, FALSE, End;
    ui.impGauge = GaugeObject, GaugeFrame, MUIA_Gauge_Horiz, TRUE, MUIA_Gauge_Max, 1000,
        MUIA_FixHeightTxt, (IPTR)"M", End;
    ui.impPages = VGroup, MUIA_Group_PageMode, TRUE,
        Child, (IPTR)(VGroup,
            Child, (IPTR)VSpace(0),
            Child, (IPTR)(TextObject, MUIA_Text_PreParse, (IPTR)"\33c",
                MUIA_Text_Contents, (IPTR)"Choose the folder of the DOS game you want to import.", End),
            Child, (IPTR)(HGroup, Child, (IPTR)HSpace(0), Child, (IPTR)ui.impChoose, Child, (IPTR)HSpace(0), End),
            Child, (IPTR)VSpace(0), End),
        Child, (IPTR)(VGroup,
            Child, (IPTR)ui.impSource,
            Child, (IPTR)(TextObject, MUIA_Text_Contents, (IPTR)"Choose installer:", End),
            Child, (IPTR)(ListviewObject, MUIA_Listview_List, (IPTR)ui.impList, End),
            Child, (IPTR)(HGroup, Child, (IPTR)ui.impBack, Child, (IPTR)HSpace(0),
                Child, (IPTR)ui.impSkip, Child, (IPTR)ui.impLaunch, End),
            End),
        Child, (IPTR)(VGroup,
            Child, (IPTR)VSpace(0),
            Child, (IPTR)ui.impReady,
            Child, (IPTR)VSpace(0),
            Child, (IPTR)(HGroup, Child, (IPTR)ui.impBack2, Child, (IPTR)HSpace(0), Child, (IPTR)ui.impCreate, End),
            End),
        // AROS loop: the startup program is chosen here; the original takes
        // it from an installer-made launcher or the launch panel later.
        Child, (IPTR)(VGroup,
            Child, (IPTR)ui.impProgText,
            Child, (IPTR)(ListviewObject, MUIA_Listview_List, (IPTR)ui.impProgList, End),
            Child, (IPTR)(HGroup, Child, (IPTR)HSpace(0), Child, (IPTR)ui.impUse, End),
            End),
        // ImportFinishedPanel: title, the cover well, the name field; AROS
        // adds the cover choice and a line of text in place of the
        // ImportCoverArtTip / ImportRenameTip arrows.
        Child, (IPTR)(VGroup,
            Child, (IPTR)VSpace(0),
            Child, (IPTR)(TextObject, MUIA_Text_PreParse, (IPTR)"\33c",
                MUIA_Text_Contents, (IPTR)"Congratulations! Your new gamebox is ready to play.", End),
            Child, (IPTR)(HGroup, Child, (IPTR)HSpace(0), Child, (IPTR)ui.impWell, Child, (IPTR)HSpace(0), End),
            Child, (IPTR)(HGroup, Child, (IPTR)HSpace(0), Child, (IPTR)ui.impName, Child, (IPTR)HSpace(0), End),
            Child, (IPTR)(HGroup, Child, (IPTR)HSpace(0),
                Child, (IPTR)(TextObject, MUIA_Text_Contents, (IPTR)"Cover:", MUIA_Text_SetMax, TRUE, End),
                Child, (IPTR)ui.impCover, Child, (IPTR)HSpace(0), End),
            Child, (IPTR)(TextObject, MUIA_Text_PreParse, (IPTR)"\33c",
                MUIA_Text_Contents, (IPTR)"Type a new name and press Return to rename the game.\n"
                                          "Click the icon to use your own picture as its cover.", End),
            Child, (IPTR)ui.impDoneText,
            Child, (IPTR)VSpace(0),
            Child, (IPTR)(HGroup, Child, (IPTR)ui.impDoneClose, Child, (IPTR)HSpace(0), Child, (IPTR)ui.impLaunchGame, End),
            End),
        // Finalizing panel: progress and Stop (Gauge + button).
        Child, (IPTR)(VGroup,
            Child, (IPTR)VSpace(0),
            Child, (IPTR)ui.impCopyText,
            Child, (IPTR)ui.impGauge,
            Child, (IPTR)VSpace(0),
            Child, (IPTR)(HGroup, Child, (IPTR)HSpace(0), Child, (IPTR)ui.impStop, End),
            End),
        End;
    return WindowObject,
        MUIA_Window_Title, (IPTR)"Import a Game",
        MUIA_Window_LeftEdge, MUIV_Window_LeftEdge_Centered,
        MUIA_Window_TopEdge, MUIV_Window_TopEdge_Centered,
        MUIA_Window_Width, 480,
        MUIA_Window_RootObject, (IPTR)ui.impPages,
        End;
}

static Object *buildDOS() {
    ui.programs = newGlyphButton((int)Glyph::LauncherList, GS_RoundTextured, 40, 25, MUIV_InputMode_Toggle);
    ui.manuals = newGlyphButton((int)Glyph::Documentation, GS_RoundTextured, 40, 25, MUIV_InputMode_RelVerify);
    ui.seg[0] = newGlyphButton((int)Glyph::Pause, GS_SegLeft, 35, 25, MUIV_InputMode_Immediate);
    ui.seg[1] = newGlyphButton((int)Glyph::Play, GS_SegMid, 34, 25, MUIV_InputMode_Immediate);
    ui.seg[2] = newGlyphButton((int)Glyph::FastForward, GS_SegRight, 35, 25, MUIV_InputMode_Immediate);
    ui.volMin = newGlyphButton((int)Glyph::Volume0, GS_Inline, 22, 17, MUIV_InputMode_RelVerify);
    ui.volMax = newGlyphButton((int)Glyph::Volume100, GS_Inline, 27, 17, MUIV_InputMode_RelVerify);
    // xib: small slider 72x15, no ticks, focusRingType none.
    ui.volSlider = newSlider(0, 100, 75, 72, 15, 0, 0, false, false);
    ui.volBox = (Object *)NewObject(mccPaintGroup->mcc_Class, NULL,
        PGA_Mode, PM_VolumeBox, PGA_Layout, (IPTR)&kVolumeBox,
        Child, (IPTR)ui.volMin, Child, (IPTR)ui.volSlider, Child, (IPTR)ui.volMax, TAG_DONE);
    ui.inspBtn = newGlyphButton((int)Glyph::Reveal, GS_RoundTextured, 40, 25, MUIV_InputMode_RelVerify);
    ui.fsBtn = newGlyphButton((int)Glyph::FullScreen, GS_RoundTextured, 40, 25, MUIV_InputMode_RelVerify);

    ui.toolbar = (Object *)NewObject(mccPaintGroup->mcc_Class, NULL,
        PGA_Mode, PM_Toolbar, MUIA_Group_Horiz, TRUE, MUIA_Group_Spacing, 8,
        MUIA_InnerLeft, 7, MUIA_InnerRight, 7, MUIA_InnerTop, 6, MUIA_InnerBottom, 7,
        Child, (IPTR)ui.programs, Child, (IPTR)ui.manuals,
        Child, (IPTR)(RectangleObject, MUIA_FixWidth, 16, End),   // NSToolbarSpaceItem 32 pt
        Child, (IPTR)(HGroup, MUIA_Group_Spacing, 0, MUIA_InnerLeft, 0, MUIA_InnerRight, 0,
            MUIA_InnerTop, 0, MUIA_InnerBottom, 0,
            Child, (IPTR)ui.seg[0], Child, (IPTR)ui.seg[1], Child, (IPTR)ui.seg[2], End),
        Child, (IPTR)(RectangleObject, End),                       // flexible space
        Child, (IPTR)ui.volBox, Child, (IPTR)ui.inspBtn, Child, (IPTR)ui.fsBtn,
        TAG_DONE);

    ui.render = newRenderView(false);
    // BXDOSWindowController's panel wrapper: the DOS view and the launch
    // panel share the area between toolbar and status bar; one is shown.
    ui.lpPanel = newLaunchPanel(&ui.lpSearch, &ui.lpList);
    ui.dosPages = VGroup, MUIA_Group_PageMode, TRUE, MUIA_InnerLeft, 0, MUIA_InnerRight, 0,
        MUIA_InnerTop, 0, MUIA_InnerBottom, 0,
        Child, (IPTR)ui.render, Child, (IPTR)ui.lpPanel, End;
    ui.lockBtn = newGlyphButton((int)Glyph::LockUnlocked, GS_Recessed, 24, 17,
                                MUIV_InputMode_Toggle, (int)Glyph::LockLocked);
    ui.statusText = newLabel("", g_fonts.small11, 0x000000, 0, 321, 14, 0xF7F7F7, 1);
    ui.status = (Object *)NewObject(mccPaintGroup->mcc_Class, NULL,
        PGA_Mode, PM_BottomBar, PGA_Layout, (IPTR)&kStatusBar,
        Child, (IPTR)ui.lockBtn, Child, (IPTR)ui.statusText, TAG_DONE);

    return WindowObject,
        MUIA_Window_Title, (IPTR)"BOXTEST",
        MUIA_Window_LeftEdge, 8, MUIA_Window_TopEdge, 24,
        // RMBTRAP: the right button belongs to DOS (BXInputController
        // rightMouseDown:). Without it Intuition took the button for menu
        // mode and the session stopped taking input (ABIv11, 2026-10-01).
        MUIA_Window_NoMenus, TRUE,
        MUIA_Window_RootObject, (IPTR)(VGroup, MUIA_Group_Spacing, 0,
            MUIA_InnerLeft, 0, MUIA_InnerRight, 0, MUIA_InnerTop, 0, MUIA_InnerBottom, 0,
            Child, (IPTR)ui.toolbar, Child, (IPTR)ui.dosPages, Child, (IPTR)ui.status, End),
        End;
}

static Object *buildFullscreen() {
    ui.fsRender = newRenderView(true);
    return WindowObject,
        MUIA_Window_Borderless, TRUE, MUIA_Window_Backdrop, FALSE,
        MUIA_Window_NoMenus, TRUE,
        MUIA_Window_CloseGadget, FALSE, MUIA_Window_DepthGadget, FALSE,
        MUIA_Window_SizeGadget, FALSE, MUIA_Window_DragBar, FALSE,
        MUIA_Window_LeftEdge, 0, MUIA_Window_TopEdge, 0,
        MUIA_Window_Width, MUIV_Window_Width_Screen(100),
        MUIA_Window_Height, MUIV_Window_Height_Screen(100),
        MUIA_Window_RootObject, (IPTR)(VGroup, MUIA_Group_Spacing, 0,
            MUIA_InnerLeft, 0, MUIA_InnerRight, 0, MUIA_InnerTop, 0, MUIA_InnerBottom, 0,
            Child, (IPTR)ui.fsRender, End),
        End;
}

static LONG rightEdgeFor(int width) {
    struct Screen *s = LockPubScreen(NULL);
    LONG x = s ? s->Width - width : 0;
    if (s) UnlockPubScreen(NULL, s);
    return x > 0 ? x : 0;
}

static Object *buildInspector() {
    const TabIcon icons[5] = {TabIcon::Game, TabIcon::CPU, TabIcon::Mouse, TabIcon::Joystick, TabIcon::Drives};
    for (int i = 0; i < 5; ++i) ui.tabs[i] = newToolTab((int)icons[i], kTabLabels[i]);
    ui.tabbar = (Object *)NewObject(mccPaintGroup->mcc_Class, NULL,
        PGA_Mode, PM_Toolbar, MUIA_Group_Horiz, TRUE, MUIA_Group_Spacing, 2,
        MUIA_InnerLeft, 0, MUIA_InnerRight, 0, MUIA_InnerTop, 3, MUIA_InnerBottom, 4,
        Child, (IPTR)(RectangleObject, End),
        Child, (IPTR)ui.tabs[0], Child, (IPTR)ui.tabs[1], Child, (IPTR)ui.tabs[2],
        Child, (IPTR)ui.tabs[3], Child, (IPTR)ui.tabs[4],
        Child, (IPTR)(RectangleObject, End), TAG_DONE);

    // xib: small sliders 256x17 (11 ticks below, continuous) and 256x18
    // (10 ticks above, tick values only); default focus ring.
    ui.speedSlider = newSlider(0, 1000, std::lround(speedToSlider(st.speed) * 1000), 256, 17,
                               11, 1, false, true);
    ui.frameSlider = newSlider(-kMaxFrameskip, 0, 0, 256, 18, 10, 2, true, true);
    ui.speedDesc = textObj("", g_fonts.small11, 2, 167, 14);
    ui.frameDesc = textObj("", g_fonts.small11, 2, 126, 14);
    ui.dynamic = MUI_MakeObject(MUIO_Checkmark, NULL);
    set(ui.dynamic, MUIA_Selected, TRUE);
    set(ui.dynamic, MUIA_CycleChain, 1);
    ui.helpBtn = newGlyphButton(-1, GS_Help, 25, 25, MUIV_InputMode_RelVerify);

    Object *box = (Object *)NewObject(mccPaintGroup->mcc_Class, NULL,
        PGA_Mode, PM_Box, PGA_Layout, (IPTR)&kOptionsBox,
        Child, (IPTR)(HGroup, MUIA_Group_Spacing, 4, MUIA_InnerLeft, 0, MUIA_InnerRight, 0,
            MUIA_InnerTop, 0, MUIA_InnerBottom, 0,
            Child, (IPTR)ui.dynamic,
            // The checkbox is ghosted (not passed to the core); AppKit dims a disabled
            // checkbox's title with it, and here the title is a separate label.
            Child, (IPTR)newLabel("Optimize for newer games", g_fonts.system13, 0xA0A0A0, 0,
                                  textWidthOf(g_fonts.system13, "Optimize for newer games") + 2, 18),
            Child, (IPTR)(RectangleObject, End), End),
        Child, (IPTR)newLabel("Turn off optimized emulation if the game\ncrashes or behaves unreliably.",
                              g_fonts.small11, 0x555555, 0, 256, 28, 0xFFFFFF, 1),
        TAG_DONE);

    Object *cpu = (Object *)NewObject(mccPaintGroup->mcc_Class, NULL,
        PGA_Mode, PM_Panel, PGA_Layout, (IPTR)&kCPUPage,
        Child, (IPTR)textObj("CPU speed:", g_fonts.bold13, 0, 87, 17),
        Child, (IPTR)ui.speedDesc,
        Child, (IPTR)ui.speedSlider,
        Child, (IPTR)textObj("XT", g_fonts.mini9, 0, 36, 11),
        Child, (IPTR)textObj("AT", g_fonts.mini9, 1, 44, 11),
        Child, (IPTR)textObj("386", g_fonts.mini9, 1, 44, 11),
        Child, (IPTR)textObj("486", g_fonts.mini9, 1, 44, 11),
        Child, (IPTR)textObj("Pentium", g_fonts.mini9, 1, 44, 11),
        Child, (IPTR)textObj("Max", g_fonts.mini9, 2, 36, 11),
        Child, (IPTR)box,
        Child, (IPTR)textObj("Frame rate:", g_fonts.bold13, 0, 126, 17),
        Child, (IPTR)ui.frameDesc,
        Child, (IPTR)ui.frameSlider,
        // Latin-1 fonts have no U+2019; the straight apostrophe stands in.
        Child, (IPTR)newLabel("Reduce the frame rate if the game's\nsound or animation are stuttering.",
                              g_fonts.small11, 0x555555, 0, 256, 28, 0xFFFFFF, 1),
        Child, (IPTR)ui.helpBtn,
        TAG_DONE);

    // Gamebox panel: name and cover work; the launch box (startup
    // program, close after exiting) is shown ghosted because those options
    // are not ported yet.
    ui.inspWell = newCoverWell();
    ui.inspName = newNameField();
    ui.inspCover = newCoverCycle();
    ui.inspNote = newLabel("", g_fonts.small11, 0x555555, 1, 256, 14, 0xFFFFFF, 1);
    ui.inspLaunch = TextObject, ButtonFrame, MUIA_Background, MUII_ButtonBack,
        MUIA_Text_Contents, (IPTR)"Nothing (show a DOS prompt)", MUIA_Disabled, TRUE, End;
    ui.inspCloseOnExit = MUI_MakeObject(MUIO_Checkmark, NULL);
    set(ui.inspCloseOnExit, MUIA_Disabled, TRUE);
    Object *launchBox = (Object *)NewObject(mccPaintGroup->mcc_Class, NULL,
        PGA_Mode, PM_Box, PGA_Layout, (IPTR)&kLaunchBox,
        Child, (IPTR)textObj("When starting up, launch:", g_fonts.bold13, 0, 256, 17),
        Child, (IPTR)ui.inspLaunch,
        Child, (IPTR)(HGroup, MUIA_Group_Spacing, 4, MUIA_InnerLeft, 0, MUIA_InnerRight, 0,
            MUIA_InnerTop, 0, MUIA_InnerBottom, 0,
            Child, (IPTR)ui.inspCloseOnExit,
            Child, (IPTR)newLabel("Close window after exiting", g_fonts.system13, 0xA0A0A0, 0,
                                  textWidthOf(g_fonts.system13, "Close window after exiting") + 2, 18),
            Child, (IPTR)(RectangleObject, End), End),
        TAG_DONE);
    Object *gameboxPage = (Object *)NewObject(mccPaintGroup->mcc_Class, NULL,
        PGA_Mode, PM_Panel, PGA_Layout, (IPTR)&kGameboxPage,
        Child, (IPTR)ui.inspWell,
        // AROS: a click on the well, not a drop (no drag and drop yet).
        Child, (IPTR)newLabel("Click the icon to choose your own\ncover art for this game.",
                              g_fonts.small11, 0x000000, 1, 256, 29, 0xFFFFFF, 1),
        Child, (IPTR)ui.inspName,
        Child, (IPTR)ui.inspCover,
        Child, (IPTR)ui.inspNote,
        Child, (IPTR)launchBox,
        TAG_DONE);

    Object *pages[5];
    for (int i = 0; i < 5; ++i) {
        if (i == 1) { pages[i] = cpu; continue; }
        if (i == 0) { pages[i] = gameboxPage; continue; }
        static char msg[5][80];
        std::snprintf(msg[i], 80, "%s panel:\nnot part of UI slice A", kTabLabels[i]);
        pages[i] = (Object *)NewObject(mccPaintGroup->mcc_Class, NULL,
            PGA_Mode, PM_Panel, PGA_Layout, (IPTR)&kPlaceholder,
            // Grey of a disabled control: the page is a placeholder, not content.
            Child, (IPTR)newLabel(msg[i], g_fonts.small11, 0xA0A0A0, 1, 256, 32), TAG_DONE);
    }
    ui.pages = (Object *)NewObject(mccPaintGroup->mcc_Class, NULL,
        PGA_Mode, PM_Panel, MUIA_Group_PageMode, TRUE,
        MUIA_InnerLeft, 0, MUIA_InnerRight, 0, MUIA_InnerTop, 0, MUIA_InnerBottom, 0,
        Child, (IPTR)pages[0], Child, (IPTR)pages[1], Child, (IPTR)pages[2],
        Child, (IPTR)pages[3], Child, (IPTR)pages[4], TAG_DONE);

    // The original is a utility NSPanel (small title bar); here it gets
    // a standard AROS window frame instead.
    return WindowObject,
        MUIA_Window_Title, (IPTR)kTabTitles[1],
        MUIA_Window_LeftEdge, rightEdgeFor(296 + 12), MUIA_Window_TopEdge, 24,
        MUIA_Window_SizeGadget, FALSE,
        MUIA_Window_RootObject, (IPTR)(VGroup, MUIA_Group_Spacing, 0,
            MUIA_InnerLeft, 0, MUIA_InnerRight, 0, MUIA_InnerTop, 0, MUIA_InnerBottom, 0,
            Child, (IPTR)ui.tabbar, Child, (IPTR)ui.pages, End),
        End;
}


// =============================================================== session ===
// The embedded core runs on this (the main) task: Emulator::run blocks, and
// the core calls back into SessionHost::processEvents, which services Zune
// without ever waiting. The original's equivalents are named per function.

static struct MsgPort *g_inputPort;
static struct IOStdReq *g_inputReq;
struct Device *InputBase;

static bool openInput() {
    if (g_inputReq) return true;
    g_inputPort = CreateMsgPort();
    if (!g_inputPort) return false;
    g_inputReq = (struct IOStdReq *)CreateIORequest(g_inputPort, sizeof(struct IOStdReq));
    if (!g_inputReq || OpenDevice((STRPTR)"input.device", 0, (struct IORequest *)g_inputReq, 0)) {
        if (g_inputReq) DeleteIORequest((struct IORequest *)g_inputReq);
        DeleteMsgPort(g_inputPort);
        g_inputReq = nullptr; g_inputPort = nullptr;
        return false;
    }
    InputBase = g_inputReq->io_Device;
    return true;
}

static void closeInput() {
    if (!g_inputReq) return;
    CloseDevice((struct IORequest *)g_inputReq);
    DeleteIORequest((struct IORequest *)g_inputReq);
    DeleteMsgPort(g_inputPort);
    g_inputReq = nullptr; g_inputPort = nullptr; InputBase = nullptr;
}

// The live button qualifiers from input.device, for the dumps.
static void logButtons() {
    if (!openInput()) { logf("buttons: input.device not available"); return; }
    UWORD q = PeekQualifier();
    logf("buttons: qualifier %04x left=%d right=%d", q, (q & IEQUALIFIER_LEFTBUTTON) ? 1 : 0,
         (q & IEQUALIFIER_RBUTTON) ? 1 : 0);
}

// Pointer warp for a locked mouse (the original warps the cursor back
// with CGWarpMouseCursorPosition): an absolute IECLASS_POINTERPOS event,
// which Intuition treats as IESUBCLASS_COMPATIBLE screen coordinates
// (rom/intuition/inputhandler.c). The SDKs have no IEPointerPixel.
static void warpPointer(struct Window *w, int x, int y) {
    if (!w || !openInput()) return;
    struct InputEvent ie = {};
    ie.ie_Class = IECLASS_POINTERPOS;
    ie.ie_Code = IECODE_NOBUTTON;
    ie.ie_X = w->WScreen->LeftEdge + w->LeftEdge + x;
    ie.ie_Y = w->WScreen->TopEdge + w->TopEdge + y;
    g_inputReq->io_Command = IND_WRITEEVENT;
    g_inputReq->io_Data = (APTR)&ie;
    g_inputReq->io_Length = sizeof ie;
    DoIO((struct IORequest *)g_inputReq);
}

struct SessionArgs {
    std::string gamebox, data, confDir, msg, style = "normal", kind;
    std::vector<boxer::ConfigFile> confs;
    std::vector<std::string> pre, cmd, then;
    int volume = -1;   // VOLUME: the toolbar volume (0..100) at start
    bool harness = false;   // HARNESS=1: test keys (header comment)
    bool fontCheck = false; // FONTCHECK=1: log the per-glyph font check (openFonts)
    std::string missingGamebox;   // sidecar icon whose gamebox is not there
    // That icon's GAMEBOX and BOXERID ToolTypes: they find the
    // gamebox when the icon and the gamebox no longer have the same name.
    std::string sidecarGamebox, sidecarId;
    // The user data directory. data (DATA) overrides it for one session only;
    // dataDirChosen (DATADIR, or the BOXER_DATADIR variable) sets the
    // configured one without a requester (setup and test harness).
    std::string dataDirChosen;
    std::string prefsPath;        // PREFS: test only, replaces ENVARC:Boxer/Boxer.prefs, no ENV: copy
    bool prefsOnly = false;       // PREFSONLY=1: resolve and store the data directory, then exit
    std::string prefsFault;       // PREFSFAULT=<step>:fail|crash, HARNESS only (data dir runtime test)
};
static SessionArgs g_args;

static struct {
    bool running = false, ran = false, quitApp = false;
    boxer::Emulator *emu = nullptr;
    boxer::FrameInfo frame;
    Size scaled = {0, 0};
    bool smooth = false;
    unsigned long frames = 0;
    bool haveLast = false;
    int lastX = 0, lastY = 0;     // view coordinates of the last pointer event
    double lastFx = 0, lastFy = 0; // clamped viewport fraction of the last event
    bool haveFracLast = false;
    bool aspect = true;           // aspectCorrected (UserDefaults.plist default YES)
    unsigned statT0 = 0;          // host ms at the start of the stats window
    unsigned long statF0 = 0;     // frames delivered at that time
    bool pointerHidden = false;
    RenderFrame last = {};        // the frame on screen, re-shown on a style change
    bool haveFrame = false;
    bool buttonsToDOS[3] = {};
    bool ffKeyHeld = false;   // fast forward held with RAmiga+Alt+F
    std::string title = "DOS Prompt";
} g_ss;

static ULONG g_sigMask;
static bool handleId(ULONG id);
static void updateStatus();

static Object *activeRender() { return st.fullscreen ? ui.fsRender : ui.render; }

static bool windowOpen(Object *win) {
    IPTR o = 0;
    get(win, MUIA_Window_Open, &o);
    return o;
}

// ---- launch panel (BXLaunchPanelController + the BXSession parts) --------
// Shown in the DOS window in place of the DOS view, only for
// a gamebox session (allowsLauncherPanel; no standalone bundles exist here).
static void releaseInput(const char *why);
static void setLocked(bool locked, const char *why);
static void setPaused(bool paused, const char *why);

static struct {
    bool active = false;                 // gamebox session: allowsLauncherPanel
    boxer::Gamebox box;
    std::vector<boxer::BundledDrive> drives;
    std::vector<boxer::Launcher> launchers;
    boxer::GameSettings settings;
    bool settingsOk = false;
    std::vector<boxer::RecentProgram> recents;
    bool mounted[26] = {};
    boxer::FileSystem *fs = nullptr;
    std::vector<LPRow> rows;             // displayedRows
    std::string filter;
    // BXDOSWindowController currentPanel == Loading: until the first
    // program starts or the shell first waits for input.
    bool loading = true;
    Completion completion = Completion::DoNothing;  // _programCompletionBehavior
    int depth = 0;                       // running programs and batch files
    unsigned startMs = 0;
    bool lastInternal = false, processEnded = false;
    double lastRun = 0;
    int pendingRow = -1, pendingAction = 0;
} g_lp;

static const boxer::BundledDrive *lpDrive(char letter) {
    for (const auto &d : g_lp.drives)
        if (d.letter == letter && !d.queued) return &d;
    return nullptr;
}

// DOSPathForLogicalURL: on a mounted folder drive of the gamebox. Names are
// shown upper-cased as DOS reports them; a host name that is not 8.3 would
// get a DOSBox short name (NAME~1) that this mapping does not compute.
static bool lpHostToDos(const std::string &host, std::string &dos, char *letter = nullptr) {
    for (const auto &d : g_lp.drives) {
        if (d.queued || d.isImage || !g_lp.mounted[d.letter - 'A']) continue;
        if (!boxer::fsutil::isWithin(host, d.sourcePath)) continue;
        std::string rel = boxer::fsutil::relativeTo(host, d.sourcePath);
        for (auto &c : rel) c = c == '/' ? '\\' : (char)std::toupper((unsigned char)c);
        dos = std::string(1, d.letter) + ":\\" + rel;
        if (letter) *letter = d.letter;
        return true;
    }
    return false;
}

static bool lpDosToHost(const std::string &dos, std::string &host) {
    if (dos.size() < 2 || dos[1] != ':') return false;
    const boxer::BundledDrive *d = lpDrive((char)std::toupper((unsigned char)dos[0]));
    if (!d || d->isImage) return false;
    std::string rel = dos.substr(2);
    while (!rel.empty() && (rel[0] == '\\' || rel[0] == '/')) rel.erase(0, 1);
    for (auto &c : rel) if (c == '\\') c = '/';
    host = rel.empty() ? d->sourcePath : boxer::fsutil::join(d->sourcePath, rel);
    return true;
}

static std::string lpRecentHost(const boxer::RecentProgram &r) {
    bool absolute = r.path.find(':') != std::string::npos || (!r.path.empty() && r.path[0] == '/');
    return absolute ? r.path : boxer::fsutil::join(g_lp.box.path(), r.path);
}

// canOpenURLs = !isRunningActiveProcess.
static bool lpCanOpen() { return g_ss.running && g_lp.depth == 0; }
// canToggleLaunchPanel: allowsLauncherPanel and not on the loading panel.
// Not in the fullscreen window: the panel lives in the DOS window only (gap).
static bool lpCanToggle() { return g_ss.running && g_lp.active && !g_lp.loading && !st.fullscreen; }

static void lpSyncToolbar() {
    // Toolbar "Programs": enabled = canToggleLaunchPanel, value = launchPanelShown.
    set(ui.programs, MUIA_Disabled, !lpCanToggle());
    nnset(ui.programs, MUIA_Selected, st.launchPanel);
    MUI_Redraw(ui.programs, MADF_DRAWOBJECT);
}

static void lpLogGeometry() {
    struct Window *w = _window(ui.lpList);
    if (!w || !st.launchPanel) return;
    for (const auto &g : launchPanelGeometry(ui.lpList)) {
        const LPRow &r = g_lp.rows[g.index];
        logf("launch panel: row %d at %d,%d %dx%d%s kind=%d \"%s\"", g.index, g.x, g.y, g.w, g.h,
             g.visible ? "" : " (not fully visible)", (int)r.kind, r.title.c_str());
    }
    dumpObj("lp.search", ui.lpSearch);
}

// _syncAllProgramRows / _syncRecentProgramRows / _syncFavoriteProgramRows /
// _syncDisplayedRows. Executables are rescanned each time the panel is shown
// or filtered, so files created or deleted in DOS (state files included,
// through the shadowing filesystem) are current.
static std::vector<LPRow> g_lpAll;
static void lpRebuild(const char *why, bool rescan) {
    if (!g_lp.active) return;
    std::vector<LPRow> favs;
    std::vector<LPLauncherRef> refs;
    for (size_t i = 0; i < g_lp.launchers.size(); ++i) {
        const auto &l = g_lp.launchers[i];
        std::string host = boxer::fsutil::join(g_lp.box.path(), l.path), dos;
        char letter = 0;
        lpHostToDos(host, dos, &letter);
        favs.push_back(favoriteRow(l.title, host, dos, letter, l.arguments, (int)i));
        refs.push_back({host, l.arguments});
    }
    std::vector<LPRecent> rec;
    for (const auto &r : g_lp.recents) {
        LPRecent x;
        x.hostPath = lpRecentHost(r);
        x.arguments = r.arguments;
        x.accessible = lpHostToDos(x.hostPath, x.dosPath, &x.drive) && g_lp.fs &&
                       g_lp.fs->fileExists(x.hostPath.c_str(), nullptr);
        rec.push_back(x);
    }
    if (rescan || g_lpAll.empty()) {
        g_lpAll.clear();
        for (char letter = 'A'; letter <= 'Z'; ++letter) {
            const boxer::BundledDrive *d = lpDrive(letter);
            // FIXME in the original: queued drives are not listed until mounted.
            if (!d || d->isImage || !g_lp.mounted[letter - 'A'] || !g_lp.fs) continue;
            auto exes = boxer::scanExecutables(*g_lp.fs, d->sourcePath);
            if (exes.empty()) continue;
            std::string title = d->label.empty() ? boxer::fsutil::baseName(d->sourcePath) : d->label;
            int type = d->type == boxer::DriveType::CDROM ? 1 : d->type == boxer::DriveType::Floppy ? 2 : 0;
            g_lpAll.push_back(driveRow(letter, title, type, d->sourcePath));
            for (const auto &e : exes) {
                std::string dos;
                lpHostToDos(e, dos);
                g_lpAll.push_back(programRow(e, dos, letter, ""));
            }
        }
    }
    g_lp.rows = displayedRows(favs, recentRows(rec, refs), g_lpAll, filterKeywords(g_lp.filter));
    launchPanelSetRows(ui.lpList, g_lp.rows, lpCanOpen());
    logf("launch panel: %u rows (%s), filter \"%s\", %u favorites, %u recents stored, %u in all programs",
         unsigned(g_lp.rows.size()), why, g_lp.filter.c_str(), unsigned(favs.size()),
         unsigned(g_lp.recents.size()), unsigned(g_lpAll.size()));
    for (size_t i = 0; i < g_lp.rows.size(); ++i)
        logf("launch panel:   [%u] %s \"%s\" %s", unsigned(i),
             g_lp.rows[i].kind == LPRow::SectionHeading ? "heading" :
             g_lp.rows[i].kind == LPRow::DriveHeading ? "drive" :
             g_lp.rows[i].kind == LPRow::Favorite ? "favorite" : "program",
             g_lp.rows[i].title.c_str(), g_lp.rows[i].subtitle().c_str());
    lpLogGeometry();
}

// switchToPanel: (no animation, as in 2.0-alpha where it is disabled).
static void lpShow(bool show, const char *why) {
    if (show && !(g_ss.running && g_lp.active)) return;
    if (show == st.launchPanel) { lpSyncToolbar(); return; }
    if (show) {
        // Leaving the DOS view: the input view resigns (keys and buttons
        // released, pointer unlocked).
        setLocked(false, "launch panel shown");
        releaseInput("launch panel shown");
    }
    st.launchPanel = show;
    set(ui.dosPages, MUIA_Group_ActivePage, show ? 1 : 0);
    logf("launch panel: %s (%s)", show ? "shown" : "hidden, DOS view shown", why);
    if (show) lpRebuild("willShowPanel", true);
    else set(ui.dos, MUIA_Window_ActiveObject, MUIV_Window_ActiveObject_None);
    lpSyncToolbar();
}

static void lpToggle(const char *why) {
    if (!lpCanToggle()) {
        logf("launch panel: toggle refused (%s): gamebox=%d loading=%d running=%d", why, g_lp.active,
             g_lp.loading, g_ss.running);
        lpSyncToolbar();
        return;
    }
    lpShow(!st.launchPanel, why);
}

// openItemInDOS: -> BXSession openURLInDOS:withArguments:clearScreen:onCompletion:
// as commands queued in the running DOS session (BXEmulator
// executeProgramAtDOSPath:changingDirectory: = drive, cd, program).
static void lpOpen(int index) {
    if (index < 0 || index >= (int)g_lp.rows.size()) return;
    const LPRow &row = g_lp.rows[index];
    if (!lpCanOpen() || !g_ss.emu) { logf("launch panel: open refused, session busy (row %d)", index); return; }
    if (row.dosPath.empty() && row.kind != LPRow::DriveHeading) {
        logf("launch panel: open refused, %s is not reachable in DOS", row.hostPath.c_str());
        return;
    }
    if (st.paused) setPaused(false, "program opened");
    if (row.kind == LPRow::DriveHeading) {
        // A drive: switch to it and stay at the DOS prompt.
        g_lp.completion = Completion::ShowPrompt;
        g_ss.emu->queueCommand(std::string(1, row.drive) + ":");
        g_ss.emu->queueCommand("cd \\");
        logf("launch panel: switch to drive %c:", row.drive);
        lpShow(false, "drive opened");
        return;
    }
    g_lp.completion = Completion::ShowLauncher;
    const std::string &dp = row.dosPath;
    size_t slash = dp.rfind('\\');
    std::string dir = slash > 2 ? dp.substr(2, slash - 2) : "\\";
    std::string prog = dp.substr(slash + 1) + (row.arguments.empty() ? "" : " " + row.arguments);
    g_ss.emu->queueCommand(dp.substr(0, 2));
    g_ss.emu->queueCommand("cd " + dir);
    g_ss.emu->queueCommand(prog);
    logf("launch panel: open row %d \"%s\" -> %s, cd %s, %s", index, row.title.c_str(), dp.substr(0, 2).c_str(),
         dir.c_str(), prog.c_str());
    lpShow(false, "program opened");
}

// removeItem: a launcher leaves the gamebox (BXGamebox removeLauncher:,
// persisted in Game Info.plist), a recent program leaves the session's list.
static void lpRemove(int index) {
    if (index < 0 || index >= (int)g_lp.rows.size()) return;
    const LPRow row = g_lp.rows[index];
    if (row.launcher >= 0 && row.launcher < (int)g_lp.launchers.size()) {
        boxer::PlistValue *list = g_lp.box.gameInfo().get("BXLaunchers");
        if (list && list->type() == boxer::PlistValue::Type::Array && row.launcher < (int)list->items().size())
            list->items().erase(list->items().begin() + row.launcher);
        else
            g_lp.box.gameInfo().set("BXLaunchers", boxer::PlistValue::array());
        std::string err;
        bool ok = g_lp.box.saveGameInfo(&err);
        logf("launch panel: removed launcher %d \"%s\" (Game Info.plist %s%s)", row.launcher, row.title.c_str(),
             ok ? "saved" : "NOT saved: ", ok ? "" : err.c_str());
        g_lp.launchers = g_lp.box.launchers();
    } else if (row.recent >= 0 && row.recent < (int)g_lp.recents.size()) {
        boxer::RecentProgram r = g_lp.recents[row.recent];
        boxer::removeRecentProgram(g_lp.recents, r);
        logf("launch panel: removed recent program %s %s", r.path.c_str(), r.arguments.c_str());
    } else {
        return;
    }
    lpRebuild("item removed", false);
}

static void lpAction(int row, int action) {
    // Called inside Zune's event handling of the row: run it from the loop.
    g_lp.pendingRow = row;
    g_lp.pendingAction = action;
    DoMethod(ui.app, MUIM_Application_ReturnID, ID_LP_ACTION);
}

// noteRecentProgram: for the first program of a run, not internal
// (DOSBox's own Z: programs) and on the gamebox's drives.
static void lpProgramStarted(const std::string &dos, const std::string &rawArgs) {
    if (g_lp.depth++ != 0) return;
    // _willExecuteFileAtDOSPath: trims the argument string (DOSBox passes
    // it with the separating blank in front).
    size_t a0 = rawArgs.find_first_not_of(" \t"), a1 = rawArgs.find_last_not_of(" \t");
    const std::string args = a0 == std::string::npos ? std::string() : rawArgs.substr(a0, a1 - a0 + 1);
    g_lp.startMs = boxer::coreHostMillis();
    std::string host;
    bool internal = dos.size() >= 2 && std::toupper((unsigned char)dos[0]) == 'Z' && dos[1] == ':';
    g_lp.lastInternal = internal;
    if (g_lp.active) launchPanelSetLaunchable(ui.lpList, false);
    if (internal) return;
    // _showDOSViewAfterProgramStart: a real program leaves the loading panel.
    if (g_lp.loading) { g_lp.loading = false; lpSyncToolbar(); }
    if (!g_lp.active || !lpDosToHost(dos, host)) return;
    boxer::RecentProgram r;
    r.path = boxer::fsutil::isWithin(host, g_lp.box.path()) ? boxer::fsutil::relativeTo(host, g_lp.box.path()) : host;
    r.arguments = args;
    boxer::noteRecentProgram(g_lp.recents, r);
    logf("launch panel: recent program noted: %s%s%s", r.path.c_str(), args.empty() ? "" : " ", args.c_str());
}

static void lpProgramFinished() {
    if (g_lp.depth <= 0) return;
    if (--g_lp.depth != 0) return;
    g_lp.lastRun = (boxer::coreHostMillis() - g_lp.startMs) / 1000.0;
    g_lp.processEnded = true;
    if (g_lp.active) launchPanelSetLaunchable(ui.lpList, true);
}

// emulatorDidReturnToShell: with _behaviorAfterReturningToShellFromProcess:.
static void lpReturnedToShell() {
    if (!g_lp.active) return;
    if (!g_lp.processEnded && !g_lp.loading) return;
    Completion c = afterReturnToShell(g_lp.completion, true, g_lp.lastInternal, g_lp.lastRun,
                                      g_lp.loading);
    logf("launch panel: returned to shell after %.2f s (internal=%d, requested=%d, loading=%d) -> %s",
         g_lp.lastRun, g_lp.lastInternal, (int)g_lp.completion, g_lp.loading,
         c == Completion::ShowLauncher ? "launch panel" : c == Completion::ShowPrompt ? "DOS prompt" : "no change");
    g_lp.completion = Completion::DoNothing;
    g_lp.processEnded = false;
    g_lp.loading = false;
    if (c == Completion::ShowLauncher) lpShow(true, "returned to shell");
    else if (c == Completion::ShowPrompt) lpShow(false, "returned to shell");
    else lpSyncToolbar();
}

// Hidden pointer while locked (the original hides the cursor with
// CGDisplayHideCursor and keeps it in the view).
static UWORD *g_blankPointer;
static void setPointerHidden(bool hidden) {
    struct Window *w = nullptr;
    get(st.fullscreen ? ui.fs : ui.dos, MUIA_Window_Window, &w);
    if (!w) return;
    if (hidden) {
        if (!g_blankPointer) g_blankPointer = (UWORD *)AllocVec(6 * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);
        if (g_blankPointer) SetPointer(w, g_blankPointer, 1, 16, 0, 0);
    } else {
        ClearPointer(w);
    }
    g_ss.pointerHidden = hidden;
}

// BXInputController didResignKey (BXInputController.m:403-416) clears the
// emulated keyboard and mouse; this port also unlocks the mouse.
static void releaseInput(const char *why) {
    if (!g_ss.running) return;
    int k = boxer::coreReleaseAllKeys(), b = boxer::coreReleaseAllButtons();
    for (bool &d : g_ss.buttonsToDOS) d = false;
    logf("input: released %d keys, %d buttons (%s)", k, b, why);
}

static void setLocked(bool locked, const char *why) {
    if (locked && !st.mouseActive) locked = false;
    if (locked == st.locked) return;
    st.locked = locked;
    logf("mouse lock -> %d (%s)", locked, why);
    if (g_ss.running) {
        setPointerHidden(locked);
        if (!locked) releaseInput("mouse unlocked");
        if (locked) {
            // BXInputController: locking centres the cursor in the canvas.
            Object *rv = activeRender();
            Rect vp = renderViewport(rv);
            struct Window *w = _window(rv);
            if (w) {
                int cx = vp.x + vp.w / 2, cy = vp.y + vp.h / 2;
                warpPointer(w, _mleft(rv) + cx, _mtop(rv) + cy);
                g_ss.lastX = cx; g_ss.lastY = cy; g_ss.haveLast = true;
            }
        }
    }
    updateStatus();
}

static void syncInspectorFromCore();
static void applyVolume();

class SessionHost : public boxer::EmulatorDelegate {
public:
    void frameSizeChanged(const boxer::FrameInfo &info) override {
        g_ss.frame = info;
        Size before = g_ss.scaled;
        g_ss.scaled = aspectCorrectedSize({(int)info.width, (int)info.height}, g_ss.aspect);
        logf("video: frame %ux%u dosbox scale %.3f,%.3f -> shown at %dx%d (4:3 rule %s)", info.width,
             info.height, info.scaleX, info.scaleY, g_ss.scaled.w, g_ss.scaled.h, g_ss.aspect ? "on" : "off");
        // The core has just reallocated frameBuffer (coalface.cpp
        // boxer_prepareForFrameSize): the views must not keep the old
        // pointer, since the window change below can redraw before the
        // next frame. The new buffer is valid (black) at the new size.
        showFrame(g_ss.emu->frameBuffer.data(), info.width * 4);
        if (before.w != g_ss.scaled.w || before.h != g_ss.scaled.h) accommodate();
    }

    void showFrame(const uint32_t *pixels, unsigned pitch) {
        RenderFrame f = {pixels, (int)g_ss.frame.width, (int)g_ss.frame.height, (int)pitch,
                         g_ss.scaled.w, g_ss.scaled.h, g_ss.smooth};
        g_ss.last = f;
        g_ss.haveFrame = true;
        // Only the shown view holds the pointer; the other one is cleared.
        Object *other = st.fullscreen ? ui.render : ui.fsRender;
        renderSetFrame(other, nullptr);
        IPTR open = 0;
        get(st.fullscreen ? ui.fs : ui.dos, MUIA_Window_Open, &open);
        if (open) renderSetFrame(activeRender(), &f);
    }

    void frameFinished(const uint32_t *pixels, unsigned pitch, const boxer::FrameInfo &info) override {
        ++g_ss.frames;
        showFrame(pixels, pitch);
    }

    void processEvents() override;

    // BXEmulator _didChangeEmulationState: the Inspector follows the core.
    void emulationStateChanged(int cycles, int frameskip, bool paused) override {
        bool autoSpeed = false;
        boxer::coreCycles(&autoSpeed);
        logf("core: cycles=%d auto=%d frameskip=%d paused=%d", cycles, autoSpeed, frameskip, paused);
        syncInspectorFromCore();
    }
    // boxer_setMouseActive: the DOS program installed (or removed) a mouse
    // driver; BXEmulatedMouse.active drives the status bar and locking.
    void mouseLockRequested(bool active) override {
        logf("core: mouse active=%d", active);
        st.mouseActive = active;
        if (!active) setLocked(false, "mouse inactive");
        updateStatus();
    }
    void shellWillStart() override {
        logf("core: shell will start");
        // The mixer exists now; apply the toolbar volume and the Inspector
        // values the core started with.
        applyVolume();
        syncInspectorFromCore();
    }
    void runPreflightCommands() override {
        logf("core: preflight, queueing %u commands", unsigned(preflight.size()));
        for (const auto &c : preflight) g_ss.emu->queueCommand(c);
    }
    void runLaunchCommands() override {
        logf("core: launch, queueing %u commands", unsigned(launch.size()));
        for (const auto &c : launch) g_ss.emu->queueCommand(c);
    }
    void returnedToShell() override { logf("core: returned to shell"); lpReturnedToShell(); }
    void programWillStart(const std::string &p, const std::string &a) override {
        logf("core: program start %s %s", p.c_str(), a.c_str());
        lpProgramStarted(p, a);
    }
    void programDidFinish(const std::string &p) override {
        logf("core: program end %s", p.c_str());
        lpProgramFinished();
    }
    void driveMounted(int i) override {
        logf("core: drive mounted %c:", 'A' + i);
        if (i >= 0 && i < 26) g_lp.mounted[i] = true;
    }
    void driveUnmounted(int i) override {
        logf("core: drive unmounted %c:", 'A' + i);
        if (i >= 0 && i < 26) g_lp.mounted[i] = false;
    }
    void log(const std::string &l) override { logf("dosbox: %s", l.c_str()); }

    std::vector<std::string> preflight, launch;

private:
    // BXDOSWindowController _resizeToAccommodateFrame: when the shown size
    // changes, the render area becomes at least the scaled resolution
    // (the frame's own aspect-corrected size), within the screen.
    void accommodate() {
        struct Window *w = nullptr;
        get(ui.dos, MUIA_Window_Window, &w);
        if (!w || st.fullscreen) return;
        Size cur = {_mwidth(ui.render), _mheight(ui.render)};
        Size want = cur;
        double a = (double)g_ss.scaled.w / g_ss.scaled.h, ca = (double)cur.w / cur.h;
        if (std::fabs(a - ca) < 0.025) {
            if (cur.w < g_ss.scaled.w || cur.h < g_ss.scaled.h) want = g_ss.scaled;
        } else {
            want = {cur.w, (int)std::lround(cur.w / a)};
            if (want.w < g_ss.scaled.w) want = g_ss.scaled;
        }
        int chromeW = w->Width - cur.w, chromeH = w->Height - cur.h;
        struct Screen *scr = w->WScreen;
        int maxW = scr->Width - chromeW, maxH = scr->Height - w->TopEdge - chromeH;
        if (want.w > maxW || want.h > maxH) {
            double k = std::min((double)maxW / want.w, (double)maxH / want.h);
            want = {(int)(want.w * k), (int)(want.h * k)};
        }
        if (want.w == cur.w && want.h == cur.h) return;
        int left = std::min<int>(w->LeftEdge, scr->Width - (want.w + chromeW));
        logf("video: render area %dx%d -> %dx%d to accommodate %dx%d", cur.w, cur.h, want.w, want.h,
             g_ss.scaled.w, g_ss.scaled.h);
        ChangeWindowBox(w, std::max(0, left), w->TopEdge, want.w + chromeW, want.h + chromeH);
    }
};
static SessionHost *g_host;

static void syncInspectorFromCore() {
    if (!g_ss.running) return;
    bool autoSpeed = false;
    int cycles = boxer::coreCycles(&autoSpeed);
    st.speed = autoSpeed ? kAutoSpeed : cycles;
    st.frameskip = boxer::coreFrameskip();
    nnset(ui.speedSlider, MUIA_Numeric_Value, std::lround(speedToSlider(sessionSpeedToSliderValue(st.speed)) * 1000));
    nnset(ui.frameSlider, MUIA_Numeric_Value, -st.frameskip);
    updateSpeedText();
}

static void applyVolume() {
    if (!g_ss.running) return;
    IPTR v = 0;
    get(ui.volSlider, MUIA_Numeric_Value, &v);
    boxer::coreSetMasterVolume((LONG)v / 100.0f);
    logf("core: master volume %d%%", (int)(LONG)v);
}

// Frames delivered per host second, with the core's speed settings, every
// 2 s of host time (measurement for the Inspector controls).
static void logStats(bool force = false) {
    unsigned now = boxer::coreHostMillis();
    unsigned dt = now - g_ss.statT0;
    if (!force && dt < 2000) return;
    bool autoSpeed = false;
    int cycles = boxer::coreCycles(&autoSpeed);
    unsigned long df = g_ss.frames - g_ss.statF0;
    logf("stats: ms=%u frames=%lu fps=%.2f cycles=%d auto=%d frameskip=%d paused=%d", dt, df,
         dt ? df * 1000.0 / dt : 0.0, cycles, autoSpeed, boxer::coreFrameskip(), st.paused);
    g_ss.statT0 = now;
    g_ss.statF0 = g_ss.frames;
}

// BXEmulator pause/resume. Pausing also counts as resigning key status
// (BXInputController.m:250-260: paused -> didResignKey).
static void setTurbo(bool on, const char *why);
static void setPaused(bool paused, const char *why) {
    // BXSession pause:/resume: both turn fast forward off.
    if (g_ss.running && boxer::coreTurbo()) setTurbo(false, why);
    if (paused == st.paused) return;
    st.paused = paused;
    st.playback = paused ? 0 : 1;
    for (int i = 0; i < 3; ++i) {
        nnset(ui.seg[i], MUIA_Selected, i == st.playback);
        MUI_Redraw(ui.seg[i], MADF_DRAWOBJECT);
    }
    updateTitle();
    logf("pause -> %d (%s)", paused, why);
    if (!g_ss.running) return;
    logStats(true);
    boxer::corePauseAudio(paused);
    if (paused) { setLocked(false, "paused"); releaseInput("paused"); }
}

// BXSession fastForward:/releaseFastForward: -> BXEmulator setTurboSpeed:.
// Fast forward resumes a paused session; pause, resume and a CPU speed
// change end it. The toolbar segment makes it stick until toggled again;
// Amiga+Alt+Cursor Right (or the provisional F) holds it while down.
static void setTurbo(bool on, const char *why) {
    if (!g_ss.running) return;
    if (on && st.paused) setPaused(false, why);
    if (on == boxer::coreTurbo()) return;
    logStats(true);
    boxer::coreSetTurbo(on);
    if (!on) g_ss.ffKeyHeld = false;
    bool autoSpeed = false;
    int cycles = boxer::coreCycles(&autoSpeed);
    logf("turbo -> %d (%s): cycles=%d auto=%d frameskip=%d", on, why, cycles, autoSpeed,
         boxer::coreFrameskip());
    st.playback = on ? 2 : st.paused ? 0 : 1;
    for (int i = 0; i < 3; ++i) {
        nnset(ui.seg[i], MUIA_Selected, i == st.playback);
        MUI_Redraw(ui.seg[i], MADF_DRAWOBJECT);
    }
    syncInspectorFromCore();
}

// Services Zune from inside the core's run loop. Never waits: only the
// signals that already arrived are handled (SetSignal), and every ReturnID
// they produced is dispatched before returning to the core.
static void serviceSignals(ULONG got);
void SessionHost::processEvents() {
    serviceSignals(SetSignal(0, g_sigMask | SIGBREAKF_CTRL_C) & (g_sigMask | SIGBREAKF_CTRL_C));
    // Paused: the core stays here, the UI keeps running (the original's
    // concurrent run loop waits with distantFuture while paused,
    // BXEmulator.mm:872-883). Close and Ctrl-C end the wait.
    while (st.paused && !g_ss.emu->cancelled())
        serviceSignals(Wait(g_sigMask | SIGBREAKF_CTRL_C) & (g_sigMask | SIGBREAKF_CTRL_C));
    logStats();
}

static void serviceSignals(ULONG got) {
    if (got & SIGBREAKF_CTRL_C) {
        logf("session: Ctrl-C, ending the session");
        g_ss.emu->requestQuit();
        g_ss.quitApp = true;
    }
    got &= g_sigMask;
    if (!got) return;
    for (int i = 0; i < 64; ++i) {
        ULONG sigs = got;
        ULONG id = DoMethod(ui.app, MUIM_Application_NewInput, (IPTR)&sigs);
        if (sigs) g_sigMask |= sigs;
        if (!id) break;
        if (!handleId(id)) {
            g_ss.quitApp = true;
            g_ss.emu->requestQuit();
        }
    }
    if (g_renderResized) {
        g_renderResized = false;
        if (windowOpen(ui.dos)) correctAspect();
    }
}

// ---- input from the render views -------------------------------------------
// Fast forward is held with RAmiga+Alt+F. The
// original's Cmd+Option+Cursor Right cannot be used: Amiga+cursor presses
// never reach a window on either ABI (only the release arrives).
static const unsigned kRawF = 0x23, kRawP = 0x19, kRawG = 0x24, kRawA = 0x20;
static bool sessionKey(Object *, unsigned code, bool up, unsigned qual) {
    if (!g_ss.running) return false;
    if (st.launchPanel) return false;   // keys belong to the launch panel (search field)
    // The hold key released after RAmiga+Alt+F: the hold ends, whichever
    // qualifier was let go first.
    if (up && code == kRawF && g_ss.ffKeyHeld) {
        setTurbo(false, "key released");
        return true;
    }
    int key = boxer::coreKey(code, !up, qual);
    logf("key: raw %02x %s qual %04x -> dos %d", code, up ? "up" : "down", qual, key);
    return true;
}

// Amiga-qualified keys in the render view, before the window's
// MUIA_Window_InputEvent notifications (RAmiga+Q/F/L/I and the harness keys
// stay there). Handled here: the keys whose qualifiers must be exact, which
// ParseIX strings do not express reliably (Shift, Alt), and the hold key.
static bool sessionCommandKey(Object *, unsigned code, bool up, unsigned qual) {
    const bool ramiga = qual & IEQUALIFIER_RCOMMAND;
    const bool shift = qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT);
    const bool alt = qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT);
    if (code == kRawF && up && g_ss.ffKeyHeld) {
        setTurbo(false, "key released");
        return true;
    }
    if (up || !ramiga) return false;
    if (code == kRawF && alt) {
        // RAmiga+Alt+F: fast forward while held (BXSession+BXUIControls.m:306-353).
        if (!g_ss.running) return true;
        if (qual & IEQUALIFIER_REPEAT) return true;
        logf("key: raw %02x down qual %04x -> fast forward held", code, qual);
        setTurbo(true, "key held");
        g_ss.ffKeyHeld = boxer::coreTurbo();
        return true;
    }
    if (qual & IEQUALIFIER_REPEAT) return code == kRawP || code == kRawG || (code == kRawA && shift);
    if (code == kRawP && !shift && !alt) {
        logf("shortcut: RAmiga+P");
        if (g_ss.running) setPaused(!st.paused, "RAmiga+P");
        return true;
    }
    if (code == kRawG && !shift && !alt) {
        // View > Show Launch Panel (Cmd+G, toggleLaunchPanel:), enabled
        // only while canToggleLaunchPanel. Deferred to the loop: the panel
        // switch replaces the view that is handling this key.
        logf("shortcut: RAmiga+G");
        DoMethod(ui.app, MUIM_Application_ReturnID, ID_LP_TOGGLE);
        return true;
    }
    if (code == kRawA && shift && !alt) {
        logf("shortcut: RAmiga+Shift+A");
        handleId(ID_ASPECT);
        return true;
    }
    return false;
}

// BXInputController mouseMoved: with BXEmulatedMouse movedTo:by:onCanvas:
// whileLocked:. Moves reach DOS while locked, or while the pointer is over
// the view with "track mouse while unlocked" on (_controlsCursorWhileMouseInside).
static void sessionMouseMove(Object *view, int x, int y, unsigned) {
    if (!g_ss.running || !st.mouseActive || view != activeRender() || st.launchPanel) return;
    Rect vp = renderViewport(view);
    bool insideView = x >= 0 && y >= 0 && x < _mwidth(view) && y < _mheight(view);
    if (!st.locked && !(st.track && insideView)) { g_ss.haveLast = false; g_ss.haveFracLast = false; return; }
    if (st.paused) return;
    int dx = g_ss.haveLast ? x - g_ss.lastX : 0, dy = g_ss.haveLast ? y - g_ss.lastY : 0;
    g_ss.lastX = x; g_ss.lastY = y; g_ss.haveLast = true;
    if (st.locked) {
        // Locked: relative motion only (emulate); the pointer is warped back
        // to the viewport centre so it never stops at a screen edge.
        if (dx || dy) boxer::coreMouseMoved(0, 0, dx, dy, true);
        int cx = vp.x + vp.w / 2, cy = vp.y + vp.h / 2;
        if (x != cx || y != cy) {
            warpPointer(_window(view), _mleft(view) + cx, _mtop(view) + cy);
            g_ss.lastX = cx; g_ss.lastY = cy;
        }
        logf("mouse: locked delta %+d,%+d", dx, dy);
        return;
    }
    // Outside the image (letterbox bars): the position is clamped to the
    // image edge, as the original clamps to its canvas
    // (BXInputController.m:851-852, clampPointToRect to _canvasBounds), and
    // the delta is the motion of that clamped point, so DOS sees neither a
    // position nor mickeys beyond the edge.
    Fraction f = canvasFraction(x, y, vp);
    bool outside = x < vp.x || y < vp.y || x >= vp.x + vp.w || y >= vp.y + vp.h;
    double cdx = (dx || dy) && g_ss.haveFracLast ? (f.x - g_ss.lastFx) * vp.w : 0;
    double cdy = (dx || dy) && g_ss.haveFracLast ? (f.y - g_ss.lastFy) * vp.h : 0;
    g_ss.lastFx = f.x; g_ss.lastFy = f.y; g_ss.haveFracLast = true;
    if (!cdx && !cdy && outside && (dx || dy)) {
        logf("mouse: view %d,%d outside image %d,%d %dx%d, clamped fraction %.5f,%.5f unchanged", x, y,
             vp.x, vp.y, vp.w, vp.h, f.x, f.y);
        return;
    }
    // delta * canvas size (BXEmulatedMouse.mm:83-90): with the canvas being
    // the viewport, that is the motion in view pixels.
    boxer::coreMouseMoved((float)f.x, (float)f.y, (float)cdx, (float)cdy, false);
    logf("mouse: view %d,%d viewport %d,%d %dx%d -> fraction %.5f,%.5f delta %+.0f,%+.0f%s", x, y, vp.x,
         vp.y, vp.w, vp.h, f.x, f.y, cdx, cdy, outside ? " (outside image, clamped)" : "");
}

// BXInputController mouseDown:/mouseUp: — Cmd+click (here either Amiga key)
// toggles the lock; an unlocked click locks when "track while unlocked" is
// off; otherwise the button goes to DOS.
static bool sessionButton(Object *view, int button, bool down, unsigned qual, int x, int y, bool inside) {
    if (!g_ss.running) return false;
    if (st.launchPanel && !st.fullscreen) return false;
    logf("input: button %d %s qual %04x inside=%d", button, down ? "down" : "up", qual, inside);
    if (!down) {
        if (g_ss.buttonsToDOS[button]) {
            g_ss.buttonsToDOS[button] = false;
            boxer::coreMouseButton(button, false);
            logf("mouse: button %d up -> dos", button);
            return true;
        }
        return false;
    }
    if (!inside) return false;
    // BXInputController mouseDown: "Unpause whenever the view is clicked on".
    if (st.paused) setPaused(false, "click in the view");
    if (!st.mouseActive) { logf("mouse: button %d ignored (no DOS mouse driver)", button); return true; }
    bool amiga = qual & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND);
    if (button == 0 && amiga) {
        setLocked(!st.locked, (qual & IEQUALIFIER_LCOMMAND) ? "LAmiga+click" : "RAmiga+click");
        return true;
    }
    if (!st.locked && !st.track) {
        setLocked(true, "click");
        return true;
    }
    sessionMouseMove(view, x, y, qual);   // the position travels with the click
    g_ss.buttonsToDOS[button] = true;
    boxer::coreMouseButton(button, true);
    logf("mouse: button %d down -> dos at view %d,%d", button, x, y);
    return true;
}

// After a fullscreen switch: the new view gets the frame on screen (it
// may be paused, so no new frame would come), and a locked pointer stays
// hidden in the new window.
static void fullscreenChanged() {
    if (!g_ss.running) return;
    if (st.fullscreen && st.launchPanel) lpShow(false, "fullscreen window has no launch panel");
    lpSyncToolbar();
    if (g_ss.haveFrame && g_host) g_host->showFrame(g_ss.last.pixels, g_ss.last.pitch);
    if (st.locked) setPointerHidden(true);
    g_ss.haveLast = false;
    g_ss.haveFracLast = false;
}

struct CoreCall {
    boxer::Emulator *emu;
    const std::vector<boxer::ConfigFile> *confs;
    int result;
};

// The core needs far more stack than a default Shell or Workbench start
// gives (40960 bytes on ABIv11 AROS One); NewStackSwap runs it on its own.
// NewStackSwap calls the entry as a plain C function with Args[0..7].
static IPTR coreEntry(CoreCall *call) {
    call->result = call->emu->run(*call->confs);
    return 0;
}

static bool runSession() {
    if (g_ss.ran) {
        logf("session: the core already ran in this process and keeps global state, so it cannot run again; start a new BoxerUI");
        return false;
    }
    g_ss.ran = true;
    std::vector<boxer::ConfigFile> confs;
    boxer::FileSystem plainFiles;
    boxer::ShadowFileSystem shadowFiles;
    boxer::FileSystem *files = &plainFiles;
    SessionHost host;
    g_host = &host;
    std::vector<std::string> preflight = g_args.pre, launch = g_args.cmd;
    std::vector<boxer::ConfigFile> extra;
    for (const auto &c : g_args.confs) (c.required ? confs : extra).push_back(c);
    g_lp = {};
    g_lpAll.clear();
    const bool installing = g_import.stage == boxer::ImportStage::RunningInstaller;
    if (installing) {
        std::string error, name;
        auto setupLog = [](const std::string &l) { logf("setup: %s", l.c_str()); };
        std::vector<boxer::ConfigFile> layered = extra;
        if (!boxer::installerSessionCommands(g_import.sourcePath, g_import.installerPath, preflight, launch)) {
            logf("session: source path cannot be mounted: %s", g_import.sourcePath.c_str());
            return false;
        }
        if (!boxer::openGamebox(setupLog, g_import.createdGamebox, g_args.data, g_args.confDir, layered,
                                preflight, launch, shadowFiles, error, &name, false)) {
            logf("session: new gamebox not opened: %s", error.c_str());
            return false;
        }
        confs = layered;
        files = &plainFiles;
        g_ss.title = name;
    } else if (!g_args.gamebox.empty()) {
        std::string error, name;
        auto setupLog = [](const std::string &l) { logf("setup: %s", l.c_str()); };
        std::vector<boxer::ConfigFile> layered = extra;
        // Per-game settings first: the launch-panel flags decide whether the
        // default launcher runs at start-up (BXSession readFromURL:).
        boxer::DataLocations where;
        where.dataDir = g_args.data;
        {
            boxer::Gamebox probe;
            std::string id;
            if (probe.open(g_args.gamebox) && !(id = probe.identifier()).empty()) {
                std::string err;
                g_lp.settingsOk = g_lp.settings.load(boxer::GameSettings::pathFor(where, id), &err);
                if (!g_lp.settingsOk) logf("launch panel: game settings not read: %s", err.c_str());
            }
        }
        const bool always = g_lp.settings.flag(boxer::kAlwaysShowLaunchPanelKey);
        const bool once = g_lp.settings.flag(boxer::kShowLaunchPanelKey);
        const size_t launchBefore = launch.size();
        if (!boxer::openGamebox(setupLog, g_args.gamebox, g_args.data, g_args.confDir, layered,
                                preflight, launch, shadowFiles, error, &name)) {
            logf("session: gamebox not opened: %s", error.c_str());
            return false;
        }
        confs = layered;
        files = &shadowFiles;
        g_ss.title = name;
        // The gamebox again, now with its identifier persisted by openGamebox.
        g_lp.active = g_lp.box.open(g_args.gamebox);
        if (g_lp.active) {
            const std::string id = g_lp.box.identifier();
            if (g_lp.settings.path().empty()) {
                std::string err;
                g_lp.settingsOk = g_lp.settings.load(boxer::GameSettings::pathFor(where, id), &err);
                if (!g_lp.settingsOk) logf("launch panel: game settings not read: %s", err.c_str());
            }
            g_lp.drives = g_lp.box.bundledDrives();
            g_lp.launchers = g_lp.box.launchers();
            g_lp.recents = g_lp.settings.recentPrograms();
            g_lp.fs = &shadowFiles;
            bool startPanel = startWithLaunchPanel(true, always, once);
            if (startPanel && launch.size() > launchBefore) launch.resize(launchBefore);
            // "Once we've finished, clear any flags that override the startup
            // program for this game."
            g_lp.settings.dict().remove(boxer::kShowLaunchPanelKey);
            // openURLInDOS with BXSessionProgramCompletionBehaviorAuto while
            // the loading panel shows -> back to the launcher afterwards.
            if (launch.size() > launchBefore || !g_args.cmd.empty()) g_lp.completion = Completion::ShowLauncher;
            logf("launch panel: allowed; settings %s (%s), alwaysShow=%d showOnce=%d -> start %s; "
                 "%u launchers, %u recent programs; BXLastProgramPath resume not ported",
                 g_lp.settings.path().c_str(), g_lp.settingsOk ? "ok" : "unreadable", always, once,
                 startPanel ? "on the launch panel (startup program skipped)" : "with the startup program, if any",
                 unsigned(g_lp.launchers.size()), unsigned(g_lp.recents.size()));
        }
    } else if (!g_args.confDir.empty()) {
        confs = boxer::sessionConfigFiles(g_args.confDir, nullptr);
        if (confs.empty()) { logf("session: no configuration files in %s", g_args.confDir.c_str()); return false; }
        confs.insert(confs.end() - 1, extra.begin(), extra.end());
    } else {
        confs.insert(confs.end(), extra.begin(), extra.end());
    }
    if (confs.empty()) { logf("session: no configuration"); return false; }
    launch.insert(launch.end(), g_args.then.begin(), g_args.then.end());
    host.preflight = preflight;
    host.launch = launch;

    boxer::Emulator emu(host, *files);
    if (!g_args.msg.empty()) {
        try { emu.loadMessages(g_args.msg); }
        catch (const std::exception &e) { logf("session: %s", e.what()); return false; }
    }
    // The master volume is set before the core starts, so the mixer
    // channels created during start-up (MIXER_AddChannel -> SetVolume ->
    // UpdateVolume) already carry it and the first sample is at that level.
    {
        IPTR v = 0;
        get(ui.volSlider, MUIA_Numeric_Value, &v);
        emu.setMasterVolume((LONG)v / 100.0f);
        logf("session: master volume %d%% before start", (int)(LONG)v);
    }
    g_ss.emu = &emu;
    g_ss.smooth = g_args.style == "smoothed";
    g_ss.running = true;
    st.mouseActive = false;   // until the DOS program installs a driver
    st.locked = false;
    st.emulating = true;
    g_ss.statT0 = 0;
    g_sessionTitle = g_ss.title;
    updateTitle();
    // Zune turns Esc, Tab and the cursor keys into window actions (close
    // request, cycle chain) before any event handler sees them
    // (window.c HandleRawkey); while DOS owns the keyboard they are off.
    set(ui.dos, MUIA_Window_DisableKeys, 0xFFFFFFFF);
    set(ui.fs, MUIA_Window_DisableKeys, 0xFFFFFFFF);
    if (!windowOpen(ui.dos)) set(ui.dos, MUIA_Window_Open, TRUE);
    set(ui.dos, MUIA_Window_Activate, TRUE);
    updateStatus();
    lpSyncToolbar();
    logf("session: start \"%s\", %u config files, %u preflight, %u launch commands, style %s",
         g_ss.title.c_str(), unsigned(confs.size()), unsigned(preflight.size()),
         unsigned(launch.size()), g_ss.smooth ? "smoothed" : "normal");
    for (const auto &c : confs) logf("session: conf %s%s", c.path.c_str(), c.required ? "" : " (optional)");

    CoreCall call = {&emu, &confs, -1};
    const ULONG stackSize = 2 * 1024 * 1024;
    APTR stack = AllocVec(stackSize, MEMF_ANY);
    if (stack) {
        struct StackSwapStruct sss;
        sss.stk_Lower = stack;
        sss.stk_Upper = (decltype(sss.stk_Upper))((IPTR)stack + stackSize);
        sss.stk_Pointer = (APTR)sss.stk_Upper;
        struct StackSwapArgs ssa = {};
        ssa.Args[0] = (IPTR)&call;
        NewStackSwap(&sss, (APTR)coreEntry, &ssa);
        FreeVec(stack);
    } else {
        logf("session: no memory for the core's stack");
    }
    releaseInput("session end");
    if (st.paused) setPaused(false, "session end");
    if (st.launchPanel) lpShow(false, "session end");
    if (g_lp.active) {
        // _saveGameSettings at session close: recent programs (gamebox-
        // relative where possible) and the cleared one-shot flag.
        g_lp.settings.setRecentPrograms(g_lp.recents);
        std::string err;
        bool ok = g_lp.settingsOk && g_lp.settings.save(&err);
        logf("launch panel: game settings %s: %u recent programs%s%s", ok ? "saved" : "NOT saved",
             unsigned(g_lp.recents.size()), ok ? "" : ", ", ok ? "" : (g_lp.settingsOk ? err.c_str() : "unreadable at start, not overwritten"));
        for (const auto &r : g_lp.recents) logf("launch panel:   recent %s %s", r.path.c_str(), r.arguments.c_str());
    }
    g_lp.active = false;
    g_lp.fs = nullptr;
    g_ss.running = false;
    g_ss.haveFrame = false;
    renderSetFrame(ui.render, nullptr);
    renderSetFrame(ui.fsRender, nullptr);
    if (g_ss.pointerHidden) setPointerHidden(false);
    st.locked = false;
    st.mouseActive = true;
    logf("session: end result=%d frames=%lu error=%s", call.result, g_ss.frames, emu.lastError().c_str());
    logf("session: audio %s", boxer::coreAudioStats().c_str());
    logLibraries("after session");
    g_ss.emu = nullptr;
    g_host = nullptr;
    // The session window closes with its emulator (BXSession emulatorDidFinish).
    if (st.fullscreen) setFullscreen(false);
    set(ui.dos, MUIA_Window_DisableKeys, 0);
    set(ui.fs, MUIA_Window_DisableKeys, 0);
    set(ui.dos, MUIA_Window_Open, FALSE);
    return true;
}

// ---- arguments ------------------------------------------------------------
static std::string g_screen = "welcome", g_logPath = "RAM:boxerui.log";
static std::vector<std::string> g_wbNotes;

static std::string upper(std::string s);
static bool takeArg(const std::string &key, const std::string &value) {
    if (key == "SCREEN") g_screen = value;
    else if (key == "LOG") g_logPath = value;
    else if (key == "STYLE") g_args.style = value;
    else if (key == "GAMEBOX") g_args.gamebox = value;
    else if (key == "DATA") g_args.data = value;
    else if (key == "DATADIR") g_args.dataDirChosen = value;
    else if (key == "PREFS") g_args.prefsPath = value;
    else if (key == "PREFSONLY") g_args.prefsOnly = value == "1" || upper(value) == "YES" || upper(value) == "ON";
    else if (key == "PREFSFAULT") g_args.prefsFault = value;
    else if (key == "CONFDIR") g_args.confDir = value;
    else if (key == "MSG") g_args.msg = value;
    else if (key == "SESSION") g_args.kind = value;
    else if (key == "CONF") g_args.confs.push_back({value, true});
    else if (key == "OPTCONF") g_args.confs.push_back({value, false});
    else if (key == "PRE") g_args.pre.push_back(value);
    else if (key == "CMD") g_args.cmd.push_back(value);
    else if (key == "THEN") g_args.then.push_back(value);
    else if (key == "FONTCHECK") g_args.fontCheck = value == "1" || upper(value) == "YES" || upper(value) == "ON";
    else if (key == "HARNESS") g_args.harness = value == "1" || upper(value) == "YES" || upper(value) == "ON";
    else if (key == "VOLUME") g_args.volume = std::max(0, std::min(100, std::atoi(value.c_str())));
    else return false;
    return true;
}

static std::string upper(std::string s) {
    for (auto &c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

// KEY=value or KEY value, in the Shell; ToolTypes give KEY=value.
static bool parseArgs(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i], key, value;
        auto eq = a.find('=');
        if (eq != std::string::npos) { key = upper(a.substr(0, eq)); value = a.substr(eq + 1); }
        else if (i + 1 < argc) { key = upper(a); value = argv[++i]; }
        else { std::fprintf(stderr, "BoxerUI: missing value for %s\n", a.c_str()); return false; }
        if (!takeArg(key, value)) { std::fprintf(stderr, "BoxerUI: unknown argument %s\n", key.c_str()); return false; }
    }
    return true;
}

struct Library *IconBase;
struct Library *WorkbenchBase;

static std::string pathOfLock(BPTR lock, const char *name) {
    char buf[1024];
    if (!lock || !NameFromLock(lock, (STRPTR)buf, sizeof buf)) return "";
    if (name && *name && !AddPart((STRPTR)buf, (CONST_STRPTR)name, sizeof buf)) return "";
    return buf;
}

static bool g_fromWorkbench;
static std::string g_progDir;   // PROGDIR: expanded (a path with spaces stays one string)

static void readToolTypes(struct WBArg *arg, const char *what) {
    if (!arg->wa_Lock) return;
    BPTR old = CurrentDir(arg->wa_Lock);
    // A drawer argument (a gamebox selected with Shift) has an empty name;
    // its icon is "<drawer>.info" in the parent, which GetDiskObject("")
    // does not find, so only named arguments carry ToolTypes here.
    const char *name = (const char *)arg->wa_Name;
    struct DiskObject *dob = name && *name ? GetDiskObject((CONST_STRPTR)name) : nullptr;
    int n = 0;
    if (dob) {
        for (STRPTR *tt = dob->do_ToolTypes; tt && *tt; ++tt) {
            std::string t = (const char *)*tt;
            auto eq = t.find('=');
            // "(KEY=value)" is a disabled ToolType, as Workbench writes it.
            if (eq == std::string::npos || t[0] == '(') continue;
            const std::string key = upper(t.substr(0, eq));
            // A sidecar's GAMEBOX/BOXERID only help find a gamebox
            // that moved; taken as arguments, a relative GAMEBOX would
            // replace the gamebox the icon itself resolves to. They are kept
            // for the lookup when that gamebox is missing.
            if (!std::strcmp(what, "project") && (key == "GAMEBOX" || key == "BOXERID")) {
                if (key == "GAMEBOX") g_args.sidecarGamebox = t.substr(eq + 1);
                else g_args.sidecarId = t.substr(eq + 1);
                continue;
            }
            if (takeArg(key, t.substr(eq + 1))) ++n;
        }
        FreeDiskObject(dob);
    }
    CurrentDir(old);
    g_wbNotes.push_back(std::string(what) + " \"" + pathOfLock(arg->wa_Lock, name) + "\": " +
                        (dob ? std::to_string(n) + " ToolTypes" : std::string("no icon")));
}

// Workbench start: the program icon's ToolTypes first, then each project
// argument (a gamebox's own icon with BoxerUI as default tool,
// or a gamebox drawer selected with Shift) adds its own and names the
// gamebox when it is a *.boxer drawer.
static void parseToolTypes(struct WBStartup *wb) {
    if (!wb || wb->sm_NumArgs < 1) return;
    IconBase = OpenLibrary((STRPTR)"icon.library", 0);
    if (!IconBase) return;
    readToolTypes(&wb->sm_ArgList[0], "program");
    for (LONG i = 1; i < wb->sm_NumArgs; ++i) {
        struct WBArg *arg = &wb->sm_ArgList[i];
        readToolTypes(arg, "project");
        // The gamebox drawer itself, a lone sidecar icon "Name" beside
        // "Name.boxer", or a lone icon inside it (tools/mkgameboxicon.py).
        std::string gb;
        boxer::GameboxArg kind = boxer::resolveGameboxArg(arg->wa_Lock, (const char *)arg->wa_Name, gb);
        if (kind == boxer::GameboxArg::Missing) {
            g_wbNotes.push_back("workbench: sidecar icon without its gamebox, expected " + gb);
            if (g_args.missingGamebox.empty()) g_args.missingGamebox = gb;
        } else if (kind != boxer::GameboxArg::None) {
            g_wbNotes.push_back(std::string("workbench: gamebox argument (") + boxer::gameboxArgName(kind) + ") " + gb);
            if (g_args.gamebox.empty()) g_args.gamebox = gb;
        }
    }
    CloseLibrary(IconBase);
    IconBase = nullptr;
}

// Defaults that keep an icon or a Shell start from any current directory
// working: resources are looked up in PROGDIR:. The data directory is NOT
// derived here from the gamebox's location (a moved gamebox
// would then start fresh); resolveDataDir() takes it from the
// user prefs.
static bool exists(const std::string &p) {
    BPTR l = Lock((CONST_STRPTR)p.c_str(), SHARED_LOCK);
    if (l) UnLock(l);
    return l != 0;
}

static void applyDefaults() {
    g_progDir = pathOfLock(GetProgramDir(), nullptr);
    auto inProg = [](const char *name) {
        char buf[1024];
        std::snprintf(buf, sizeof buf, "%s", g_progDir.c_str());
        AddPart((STRPTR)buf, (CONST_STRPTR)name, sizeof buf);
        return std::string(buf);
    };
    if (g_args.confDir.empty() && exists(inProg("conf"))) g_args.confDir = inProg("conf");
    if (g_args.msg.empty() && exists(inProg("dosbox.msg"))) g_args.msg = inProg("dosbox.msg");
}

// ---- the one user data directory ------------------------------------------
struct Library *AslBase;

static LONG ask(const char *text, const char *gadgets, const char *arg1 = "", const char *arg2 = "") {
    struct EasyStruct es = {sizeof(struct EasyStruct), 0, (STRPTR)"Boxer", (STRPTR)text, (STRPTR)gadgets};
    return EasyRequest(NULL, &es, NULL, (IPTR)arg1, (IPTR)arg2);
}

// ASL drawer requester; "" when cancelled or asl.library is missing.
static std::string pickDrawer(const std::string &initial,
                              const char *title = "Choose the folder for Boxer's saved games",
                              bool saveMode = true) {
    std::string out;
    AslBase = OpenLibrary((STRPTR)"asl.library", 37);
    if (!AslBase) { logf("datadir: asl.library not available"); return out; }
    std::string start = initial;
    while (!start.empty() && !exists(start)) {
        std::string up = boxer::fsutil::parent(start);
        if (up == start) break;
        start = up;
    }
    struct FileRequester *fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (IPTR)title,
        ASLFR_DrawersOnly, TRUE, ASLFR_DoSaveMode, (IPTR)saveMode,
        ASLFR_InitialDrawer, (IPTR)start.c_str(), TAG_DONE);
    if (fr) {
        if (AslRequest(fr, NULL) && fr->fr_Drawer) out = (const char *)fr->fr_Drawer;
        FreeAslRequest(fr);
    }
    CloseLibrary(AslBase);
    AslBase = nullptr;
    return out;
}

static boxer::fsutil::ReplaceStep stepNamed(const std::string &n, bool &ok) {
    using S = boxer::fsutil::ReplaceStep;
    ok = true;
    if (n == "write") return S::WriteTemp;
    if (n == "backup") return S::BackupOld;
    if (n == "rename") return S::RenameTemp;
    if (n == "remove") return S::RemoveBackup;
    ok = false;
    return S::WriteTemp;
}

// Looked up in the DOS list: Lock() on a missing volume opens an "insert
// volume" requester (seen with Boxer:, k112d2b).
static bool dosNameExists(const char *name) {
    bool found = false;
    if (struct DosList *dl = LockDosList(LDF_ASSIGNS | LDF_VOLUMES | LDF_DEVICES | LDF_READ)) {
        found = FindDosEntry(dl, (CONST_STRPTR)name, LDF_ASSIGNS | LDF_VOLUMES | LDF_DEVICES) != NULL;
        UnLockDosList(LDF_ASSIGNS | LDF_VOLUMES | LDF_DEVICES | LDF_READ);
    }
    return found;
}

// A typed ASL drawer string is stored only in the form DOS itself gives the
// directory: "SYS:A SYS:A/B" style input resolves to one real directory, and
// that, not the text, is what must survive in the prefs.
static std::string canonicalDir(const std::string &path) {
    BPTR l = Lock((CONST_STRPTR)path.c_str(), SHARED_LOCK);
    if (!l) return path;
    char buf[1024];
    std::string out = NameFromLock(l, (STRPTR)buf, sizeof buf) ? std::string(buf) : path;
    UnLock(l);
    return out;
}

static const char *kGamesFolderTitle = "Select a folder in which to keep your DOS games:";

// A drawer BoxerUI creates gets an icon: without one,
// Wanderer's default view (icons only) hides it, and a new user cannot find
// the games folder or the game icons in it. The image comes from
// PROGDIR:<templ>.info when given, else the system's default drawer icon.
// An existing .info is never replaced; a volume root is left alone (its
// icon is the volume's disk.info). Failure is logged, never fatal.
static void addDrawerIcon(const std::string &dir, const char *templ) {
    const std::string d = boxer::fsutil::trimTrailingSlash(dir);
    if (d.empty() || d.back() == ':') return;
    const std::string info = d + ".info";
    if (BPTR l = Lock((CONST_STRPTR)info.c_str(), SHARED_LOCK)) { UnLock(l); return; }
    struct Library *base = OpenLibrary((STRPTR)"icon.library", 0);
    if (!base) { logf("drawericon: icon.library not available"); return; }
    struct Library *saved = IconBase;
    IconBase = base;
    struct DiskObject *dob = templ ? GetDiskObject((CONST_STRPTR)templ) : nullptr;
    const bool fromTempl = dob != nullptr;
    if (!dob) dob = GetDefDiskObject(WBDRAWER);
    bool ok = false;
    if (dob) {
        const LONG x = dob->do_CurrentX, y = dob->do_CurrentY;
        dob->do_CurrentX = NO_ICON_POSITION;
        dob->do_CurrentY = NO_ICON_POSITION;
        ok = PutDiskObject((CONST_STRPTR)d.c_str(), dob);
        dob->do_CurrentX = x; dob->do_CurrentY = y;
        FreeDiskObject(dob);
    }
    logf("drawericon: %s (%s) -> %s", info.c_str(), fromTempl ? templ : "default drawer icon", ok ? "written" : "FAILED");
    IconBase = saved;
    CloseLibrary(base);
}

// Creates (when allowed) and probes the games folder; a folder that cannot
// hold games is reported and the user picks another one, or cancels (false).
// On success folder is in DOS's own form, ready to be stored.
static bool settleGamesFolder(std::string &folder, bool mayCreate, const boxer::DataLocations &loc) {
    for (;;) {
        std::string msg;
        bool ok = boxer::acceptableGamesFolder(folder, loc, &msg);
        if (ok) {
            const bool existed = boxer::fsutil::isDirectory(folder);
            auto st = boxer::prepareDataDir(folder, &msg, mayCreate);
            ok = st == boxer::DataDirStatus::Ready;
            if (ok && !existed) addDrawerIcon(folder, "PROGDIR:conf/GamesFolder");
        }
        if (ok) folder = canonicalDir(folder);
        logf("gamesfolder: \"%s\" -> %s", folder.c_str(), ok ? "ready" : msg.c_str());
        if (ok) return true;
        LONG r = ask("This folder cannot hold your games:\n%s\n\nChoose another folder?", "Choose...|Cancel", msg.c_str());
        logf("gamesfolder: not usable requester -> %d", (int)r);
        if (r != 1) return false;
        std::string d = pickDrawer(folder, kGamesFolderTitle);
        if (d.empty()) return false;
        folder = d;
        mayCreate = true;
    }
}

// First run from the Welcome window (BXGamesFolderPanelController, GF:72-104):
// the games folder is proposed, confirmed or replaced, created
// and probed for write access. A configured folder that vanished is reported
// and the user chooses again; nothing is recreated silently. False ends the
// program. Sets *changed when prefs.gamesFolder must be saved.
static bool resolveGamesFolder(boxer::UserPrefs &prefs, bool *changed) {
    boxer::DataLocations loc;
    loc.dataDir = prefs.dataDir;
    std::string folder = prefs.gamesFolder;
    bool mayCreate = false;
    if (folder.empty()) {
        const bool haveWork = dosNameExists("Work");
        folder = boxer::defaultGamesFolder(haveWork);
        logf("gamesfolder: first run, Work: %s, proposal \"%s\"", haveWork ? "present" : "absent", folder.c_str());
        LONG r = ask("Boxer keeps your DOS games in one folder.\nProposed:\n\n%s\n",
                     "Use this folder|Choose another...|Cancel", folder.c_str());
        logf("gamesfolder: first-run requester -> %d", (int)r);
        if (r == 0) return false;
        if (r == 2) {
            std::string d = pickDrawer(folder, kGamesFolderTitle);
            if (d.empty()) return false;
            folder = d;
        }
        mayCreate = true;
    }
    if (!settleGamesFolder(folder, mayCreate, loc)) return false;
    *changed = folder != prefs.gamesFolder;
    prefs.gamesFolder = folder;
    return true;
}

// Where the prefs live: ENVARC:/ENV: Boxer/Boxer.prefs, or the PREFS file of a test start.
static boxer::DataLocations prefsLocations() {
    boxer::DataLocations where;
    if (!g_args.prefsPath.empty()) { where.envarcPrefsPath = g_args.prefsPath; where.envPrefsPath.clear(); }
    return where;
}

// While a stored path is checked, DOS must not put up "Please insert
// volume": the missing-folder prompt itself tells the user to connect the
// disk, and a system requester in front of it would ask the same thing twice.
template <typename F> static auto withoutVolumeRequesters(F f) -> decltype(f()) {
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR saved = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;
    auto r = f();
    me->pr_WindowPtr = saved;
    return r;
}

enum class MovedDataDirAnswer { Use, ChooseOther, Quit, NotOffered };

// A configured data folder that is gone has most often moved along with the
// games folder it was in. It is offered only when its contents show that it
// holds this games folder's saved games (boxer::findMovedDataDir); when that
// cannot be told, the user is asked to point to it, starting where it would
// be. A folder is never made the data folder for its name alone, and no new
// one is created here. atStart: BoxerUI cannot go on without a data folder,
// so another folder can be chosen and Cancel ends the program; after Locate
// the question can wait ("Not now") until the next start.
static MovedDataDirAnswer offerMovedDataDir(const std::string &missing, const std::string &oldGames,
                                            const std::vector<std::string> &folders, bool atStart,
                                            std::string &out) {
    using M = boxer::MovedDataDir;
    const auto r = withoutVolumeRequesters([&] { return boxer::findMovedDataDir(missing, oldGames, folders); });
    logf("datadir: \"%s\" missing, moved-folder search -> %s \"%s\" (%u matching, %u games)", missing.c_str(),
         r.result == M::Found ? "found" : r.result == M::Ambiguous ? "ambiguous"
         : r.result == M::Uncertain ? "uncertain" : "not found",
         r.path.c_str(), (unsigned)r.matching.size(), (unsigned)r.matchedGames);
    if (r.result == M::NotFound) return MovedDataDirAnswer::NotOffered;
    std::string body = "Boxer's data folder is no longer at\n" + missing + "\n\n";
    std::string gadgets;
    if (r.result == M::Found) {
        body += "Your games folder has a data folder with the saved games of\n" +
                (r.matchedGames == 1 ? std::string("one") : std::to_string(r.matchedGames)) +
                " of your games:\n" + r.path + "\n\nUse it?";
        gadgets = atStart ? "Use this folder|Choose another...|Cancel" : "Use this folder|Not now";
    } else {
        if (r.result == M::Ambiguous) {
            body += "Your games folder has more than one data folder with saved\ngames of your games:\n";
            for (const auto &m : r.matching) body += m + "\n";
            body += "\nChoose the one Boxer should use.";
        } else {
            body += "Your games folder contains\n" + r.path +
                    "\nbut Boxer cannot tell whether your saved games are there.\n"
                    "If you moved the data folder, choose where it is now.";
        }
        gadgets = atStart ? "Locate folder...|Choose another...|Cancel" : "Locate folder...|Not now";
    }
    // The body goes in as an argument: a '%' in a path is not a format.
    const LONG b = ask("%s", gadgets.c_str(), body.c_str());
    logf("datadir: moved-folder requester -> %d", (int)b);
    if (b == 1) {
        if (r.result == M::Found) { out = r.path; return MovedDataDirAnswer::Use; }
        const std::string start = r.result == M::Ambiguous ? boxer::fsutil::parent(r.matching[0]) : r.path;
        const std::string d = pickDrawer(start, "Select the folder that holds Boxer's saved games", false);
        logf("datadir: moved-folder locate -> \"%s\"", d.c_str());
        if (d.empty()) return MovedDataDirAnswer::NotOffered;
        out = d;
        return MovedDataDirAnswer::Use;
    }
    if (atStart && b == 2) return MovedDataDirAnswer::ChooseOther;
    return atStart ? MovedDataDirAnswer::Quit : MovedDataDirAnswer::NotOffered;
}

// Fills g_args.data. Order (boxer::chooseDataDir): DATA (this session
// only), DATADIR / BOXER_DATADIR (stored), the prefs' DataDir, and on the
// very first run the proposal "<drawer holding the gamebox>/Boxer Data",
// which the user confirms or replaces. A directory that cannot be used is
// never swapped for another one silently: the user chooses, or the start
// ends with a readable error. Returns false to end the program.
static bool resolveDataDir() {
    boxer::DataLocations where = prefsLocations();
    if (g_args.dataDirChosen.empty()) {
        char var[512];
        if (GetVar((CONST_STRPTR)"BOXER_DATADIR", (STRPTR)var, sizeof var, GVF_GLOBAL_ONLY) > 0)
            g_args.dataDirChosen = var;
    }
    boxer::UserPrefs prefs;
    std::string err;
    auto src = boxer::loadUserPrefs(where, prefs, &err);
    logf("datadir: prefs %s (%s), DataDir \"%s\"",
         src == boxer::PrefsSource::Env ? where.envPrefsPath.c_str() : where.envarcPrefsPath.c_str(),
         src == boxer::PrefsSource::Env ? "ENV" : src == boxer::PrefsSource::Envarc ? "ENVARC"
         : src == boxer::PrefsSource::Unreadable ? ("unreadable: " + err).c_str() : "none",
         prefs.dataDir.c_str());
    // The Welcome start is where a new user begins (scenario step 2), so it
    // settles the games folder first; the data directory proposal follows it.
    bool gamesChanged = false;
    const std::string oldGames = prefs.gamesFolder;
    const bool welcomeStart = g_args.gamebox.empty() && g_args.kind.empty() && !g_args.prefsOnly;
    if (welcomeStart && !resolveGamesFolder(prefs, &gamesChanged)) return false;
    g_gamesFolder = prefs.gamesFolder;
    const std::string games = !g_args.gamebox.empty()
        ? boxer::fsutil::parent(boxer::fsutil::trimTrailingSlash(g_args.gamebox)) : prefs.gamesFolder;
    auto choice = boxer::chooseDataDir(g_args.data, g_args.dataDirChosen, prefs, games);
    if (gamesChanged && choice.origin != boxer::DataDirOrigin::Override) choice.save = true;
    logf("datadir: %s \"%s\"", boxer::dataDirOriginName(choice.origin), choice.dataDir.c_str());
    if (choice.origin == boxer::DataDirOrigin::None) {
        if (g_args.gamebox.empty() && !g_args.prefsOnly) return true;   // nothing needs it
        logf("error: no data directory known");
        return false;
    }
    // First run from a Workbench or Shell start without DATADIR: confirm.
    if (choice.origin == boxer::DataDirOrigin::Proposal) {
        LONG r = ask("Boxer keeps the saved games and changes of all\nyour games in one folder. Proposed:\n\n%s\n",
                     "Use this folder|Choose another...|Cancel", choice.dataDir.c_str());
        logf("datadir: first-run requester -> %d", (int)r);
        if (r == 0) return false;
        if (r == 2) {
            std::string d = pickDrawer(choice.dataDir);
            if (d.empty()) return false;
            choice.dataDir = d;
        }
    }
    bool searched = false;
    for (;;) {
        std::string msg;
        const bool existed = boxer::fsutil::isDirectory(choice.dataDir);
        auto st = boxer::prepareDataDir(choice.dataDir, &msg, choice.mayCreate);
        if (st == boxer::DataDirStatus::Ready && !existed) addDrawerIcon(choice.dataDir, nullptr);
        logf("datadir: prepare \"%s\" -> %s", choice.dataDir.c_str(),
             st == boxer::DataDirStatus::Ready ? "ready" : msg.c_str());
        if (st == boxer::DataDirStatus::Ready) {
            if (choice.save) choice.dataDir = canonicalDir(choice.dataDir);
            break;
        }
        if (st == boxer::DataDirStatus::Missing && choice.origin == boxer::DataDirOrigin::Configured &&
            !searched && !g_args.prefsOnly) {
            searched = true;
            std::vector<std::string> folders{prefs.gamesFolder};
            if (!g_args.gamebox.empty())
                folders.push_back(boxer::fsutil::parent(boxer::fsutil::trimTrailingSlash(g_args.gamebox)));
            std::string found;
            const auto a = offerMovedDataDir(choice.dataDir, oldGames, folders, true, found);
            if (a == MovedDataDirAnswer::Quit) return false;
            if (a == MovedDataDirAnswer::Use) {
                // An existing folder: if it is gone again it is reported, not created.
                choice.dataDir = found;
                choice.mayCreate = false;
                choice.save = true;
                continue;
            }
            if (a == MovedDataDirAnswer::ChooseOther) {
                std::string d = pickDrawer(choice.dataDir);
                if (d.empty()) return false;
                choice.dataDir = d;
                choice.mayCreate = true;
                choice.save = d != prefs.dataDir;
                continue;
            }
        }
        // Shell/test starts with an explicit directory get an error, not a requester.
        if (choice.origin == boxer::DataDirOrigin::Override || g_args.prefsOnly) {
            std::fprintf(stderr, "BoxerUI: data folder not usable: %s\n", msg.c_str());
            return false;
        }
        LONG r = ask("The folder for saved games cannot be used:\n%s\n\nChoose another folder?",
                     "Choose...|Cancel", msg.c_str());
        logf("datadir: not usable requester -> %d", (int)r);
        if (r != 1) return false;
        std::string d = pickDrawer(choice.dataDir);
        if (d.empty()) return false;
        choice.dataDir = d;
        choice.mayCreate = true;
        choice.save = d != prefs.dataDir;
    }
    if (choice.save) {
        prefs.dataDir = choice.dataDir;
        if (g_args.harness && !g_args.prefsFault.empty()) {
            // <step>:fail|crash on the target filesystem (data-directory runtime test).
            auto colon = g_args.prefsFault.find(':');
            bool ok = false;
            auto step = stepNamed(g_args.prefsFault.substr(0, colon), ok);
            auto act = colon != std::string::npos && g_args.prefsFault.substr(colon + 1) == "crash"
                           ? boxer::fsutil::FaultAction::Crash : boxer::fsutil::FaultAction::Fail;
            if (ok) boxer::fsutil::setReplaceFaultHook([=](boxer::fsutil::ReplaceStep s) {
                return s == step ? act : boxer::fsutil::FaultAction::Proceed; });
            logf("datadir: harness fault %s", g_args.prefsFault.c_str());
        }
        std::string envErr;
        bool saved = boxer::saveUserPrefs(where, prefs, &err, &envErr);
        boxer::fsutil::setReplaceFaultHook(nullptr);
        if (!saved) {
            logf("datadir: prefs NOT saved: %s", err.c_str());
            if (g_args.prefsOnly) { std::fprintf(stderr, "BoxerUI: settings not saved: %s\n", err.c_str()); return false; }
            ask("Boxer could not save its settings:\n%s\n\nThe folder is used for this session only.", "OK", err.c_str());
        } else {
            logf("datadir: prefs saved to %s%s%s", where.envarcPrefsPath.c_str(),
                 envErr.empty() ? "" : "; ENV: copy failed: ", envErr.c_str());
        }
    }
    g_args.data = choice.dataDir;
    return true;
}

// Opens a drawer or an icon the way a double-click in Wanderer does.
static bool openInWanderer(const std::string &path) {
    struct Library *wb = OpenLibrary((STRPTR)"workbench.library", 44);
    if (!wb) return false;
    struct Library *saved = WorkbenchBase;
    WorkbenchBase = wb;
    BOOL ok = OpenWorkbenchObject((STRPTR)path.c_str(), TAG_DONE);
    WorkbenchBase = saved;
    CloseLibrary(wb);
    return ok;
}

// BXGamesFolderPanelController after the missing-folder prompt: an existing
// folder is chosen and becomes the games folder. As in the original it is
// stored, not opened; the next Browse opens it.
static void locateGamesFolder() {
    std::string start = withoutVolumeRequesters([] {
        std::string p = g_gamesFolder;
        while (!p.empty() && !exists(p)) {
            std::string up = boxer::fsutil::parent(p);
            if (up == p) { p.clear(); break; }
            p = up;
        }
        return p.empty() ? std::string("SYS:") : p;
    });
    std::string folder = pickDrawer(start, kGamesFolderTitle, false);
    if (folder.empty()) { logf("gamesfolder: locate cancelled"); return; }
    boxer::DataLocations loc;
    loc.dataDir = g_args.data;
    if (!settleGamesFolder(folder, false, loc)) return;
    boxer::DataLocations where = prefsLocations();
    boxer::UserPrefs prefs;
    std::string err, envErr;
    boxer::loadUserPrefs(where, prefs, &err);
    // The data folder inside the old games folder went with it; settled
    // now, the next start does not ask for it again.
    const bool dataGone = !g_args.data.empty() &&
        withoutVolumeRequesters([] { return !boxer::fsutil::isDirectory(g_args.data); });
    if (dataGone) {
        std::string found, msg;
        if (offerMovedDataDir(g_args.data, g_gamesFolder, {folder}, false, found) == MovedDataDirAnswer::Use) {
            if (boxer::prepareDataDir(found, &msg, false) == boxer::DataDirStatus::Ready) {
                g_args.data = prefs.dataDir = canonicalDir(found);
                logf("datadir: now \"%s\"", g_args.data.c_str());
            } else {
                logf("datadir: \"%s\" not usable: %s", found.c_str(), msg.c_str());
                ask("The folder for saved games cannot be used:\n%s\n\nBoxer will ask for it at its next start.", "OK", msg.c_str());
            }
        }
    }
    prefs.gamesFolder = folder;
    g_gamesFolder = folder;
    if (!boxer::saveUserPrefs(where, prefs, &err, &envErr)) {
        logf("gamesfolder: prefs NOT saved: %s", err.c_str());
        ask("Boxer could not save its settings:\n%s\n\nThe folder is used for this session only.", "OK", err.c_str());
        return;
    }
    logf("gamesfolder: relocated to \"%s\", prefs saved to %s", folder.c_str(), where.envarcPrefsPath.c_str());
}

// GF:778 revealGamesFolder: open the games folder in Wanderer; a folder
// that cannot be found (deleted, renamed, disk not mounted) brings up the
// prompt of GF:732 with a way to locate it.
static void browseGames() {
    auto st = withoutVolumeRequesters([] { return boxer::browseGamesFolder(g_gamesFolder); });
    if (st == boxer::GamesFolderBrowse::NotSet) return;   // the button is ghosted then
    if (st == boxer::GamesFolderBrowse::Open) {
        bool ok = openInWanderer(g_gamesFolder);
        logf("browse: open \"%s\" -> %s", g_gamesFolder.c_str(), ok ? "ok" : "failed");
        if (!ok)
            MUI_Request(ui.app, ui.welcome, 0, (char *)"Boxer", (char *)"OK",
                        (char *)"Wanderer could not open your games folder:\n%s", (IPTR)g_gamesFolder.c_str());
        return;
    }
    // The path is added to the original's text: nothing else tells the
    // user which folder Boxer was looking for.
    LONG r = MUI_Request(ui.app, ui.welcome, 0, (char *)"Boxer", (char *)"Locate folder...|Cancel",
        (char *)"Boxer can no longer find your games folder.\n\n"
                "Make sure the disk containing your games folder is connected.\n\n%s",
        (IPTR)g_gamesFolder.c_str());
    logf("browse: \"%s\" missing, prompt -> %d", g_gamesFolder.c_str(), (int)r);
    if (r == 1) locateGamesFolder();
}

// ----------------------------------------------------------------- main ---
static void notifyId(Object *o, ULONG attr, IPTR value, ULONG id) {
    DoMethod(o, MUIM_Notify, attr, value, (IPTR)ui.app, 2, MUIM_Application_ReturnID, id);
}

static Object *g_wins[4];
static struct MUI_InputHandlerNode g_laterIhn;
static bool g_laterArmed;

static void dumpLaterTick() {
    if (g_laterArmed) {
        DoMethod(ui.app, MUIM_Application_RemInputHandler, (IPTR)&g_laterIhn);
        g_laterArmed = false;
    }
    dumpGeometry("F5 timer");
}

static void resizeRenderTo(int w, int h, bool snap) {
    struct Window *win = nullptr;
    get(ui.dos, MUIA_Window_Window, &win);
    if (!win) return;
    g_noSnap = !snap;
    int cw = _mwidth(ui.render), ch = _mheight(ui.render);
    logf("harness: render area %dx%d -> %dx%d%s", cw, ch, w, h, snap ? "" : " (no aspect snapping)");
    ChangeWindowBox(win, win->LeftEdge, win->TopEdge, win->Width + (w - cw), win->Height + (h - ch));
}

// ======================================================= covers and names ===
// BXCoverArt / BXBootlegCoverArt for the gamebox in hand (the finished
// import, or the session's gamebox in the Inspector), the sidecar icon that
// shows the cover, and renaming the gamebox together with its icon.

static boxer::CoverFont g_coverFont;
static bool g_coverFontTried;
// Inspector rename while the game runs: the session's drives were mounted
// from the gamebox's path, so the pair is renamed when the session ends.
static std::string g_pendingRename;
static std::string g_pictureDrawer;   // where the last picture was chosen

// The bootleg title font: BoxerSans, which is the system's Vera Sans
// file, until a free replacement for Marker Felt is chosen.
static const boxer::CoverFont &coverFont() {
    if (!g_coverFontTried) {
        g_coverFontTried = true;
        const char *path = "Fonts:TrueType/VeraSans.ttf";
        bool ok = g_coverFont.loadFile(path);
        logf("cover: title font %s %s", path, ok ? "loaded" : "NOT loaded (covers get no title)");
    }
    return g_coverFont;
}

// -[BXSession representedIcon]: the recorded cover, a picture made into box
// art, else the bootleg cover of the recorded or detected medium.
static boxer::RGBAImage renderGameboxCover(const boxer::Gamebox &box) {
    boxer::CoverChoice c = boxer::readCoverChoice(box);
    // A gamebox without a record (made elsewhere): the medium is detected
    // from the gamebox each time; nothing is written into it just for
    // showing it.
    if (!box.gameInfo().get("AROSCoverArt")) c.detected = boxer::mediumOfGameAt(box.path());
    if (c.style == boxer::CoverStyle::Picture) {
        boxer::RGBAImage pic;
        std::string err;
        if (boxer::loadPicture(boxer::fsutil::join(box.path(), c.picture), pic, &err))
            return boxer::renderCoverArt(pic, boxer_ui::boxArtShine());
        logf("cover: picture of %s not readable (%s); bootleg cover shown", box.path().c_str(), err.c_str());
    }
    const boxer::ReleaseMedium m = c.bootlegMedium();
    return boxer::renderBootleg(m, box.gameName(), boxer_ui::bootlegArt(m), coverFont());
}

// A gamebox without a cover record gets the medium the original would
// have detected (IS:581-609: the source folder while it exists, else the
// gamebox), recorded so later renders do not depend on the source.
static void ensureCoverRecord(boxer::Gamebox &box, const std::string &source) {
    if (box.gameInfo().get("AROSCoverArt")) return;
    boxer::CoverChoice c;
    const std::string where = !source.empty() && boxer::fsutil::isDirectory(source) ? source : box.path();
    c.detected = boxer::mediumOfGameAt(where);
    boxer::setCoverChoice(box, c);
    std::string err;
    bool ok = box.saveGameInfo(&err);
    logf("cover: %s detected as medium \"%s\" from %s -> %s%s", box.path().c_str(), boxer::mediumName(c.detected),
         where.c_str(), ok ? "recorded" : "NOT recorded: ", err.c_str());
}

// Renders the cover and writes it as the sidecar icon "<iconStem>.info".
// replaceOwn: an existing icon is replaced only when it is this gamebox's.
static bool writeCoverIcon(const std::string &gameboxPath, const std::string &iconStem, bool replaceOwn,
                           std::string &err, boxer::RGBAImage *rendered = nullptr) {
    boxer::Gamebox box;
    if (!box.open(gameboxPath, &err)) return false;
    bool persisted = true;
    const std::string id = box.ensureIdentifier(&persisted, &err);
    if (id.empty() || !persisted) {
        if (err.empty()) err = "the gamebox has no identifier";
        return false;
    }
    boxer::RGBAImage icon = renderGameboxCover(box);
    if (rendered) *rendered = icon;
    bool ok = boxer::writeSidecarIcon(iconStem, icon, id, boxer::fsutil::baseName(gameboxPath), replaceOwn, &err);
    logf("cover: icon %s.info %s%s", iconStem.c_str(), ok ? "written" : "NOT written: ", err.c_str());
    for (const auto &p : boxer::fsutil::pendingCleanup())
        logf("cover: old version \"%s\" in use, removed later", p.c_str());
    return ok;
}

static void showCover(Object *well, Object *cycle, Object *name, const std::string &gameboxPath) {
    boxer::Gamebox box;
    if (!box.open(gameboxPath)) {
        coverWellSetImage(well, nullptr, 0, 0);
        return;
    }
    const boxer::RGBAImage icon = renderGameboxCover(box);
    coverWellSetImage(well, icon.px.data(), icon.w, icon.h);
    const boxer::CoverChoice c = boxer::readCoverChoice(box);
    for (int i = 0; i < 5; ++i)
        if (kCoverStyles[i] == c.style) nnset(cycle, MUIA_Cycle_Active, i);
    if (name) nnset(name, MUIA_String_Contents, (IPTR)box.gameName().c_str());
}

// ASL file requester for a picture (BXCoverArtWell takes any image).
static std::string pickPicture() {
    std::string out;
    AslBase = OpenLibrary((STRPTR)"asl.library", 37);
    if (!AslBase) { logf("cover: asl.library not available"); return out; }
    const std::string start = g_pictureDrawer.empty() ? std::string("SYS:") : g_pictureDrawer;
    struct FileRequester *fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (IPTR)"Choose a picture for the game's cover",
        ASLFR_InitialDrawer, (IPTR)start.c_str(),
        ASLFR_DoPatterns, TRUE,
        ASLFR_InitialPattern, (IPTR)"#?.(png|jpg|jpeg|iff|ilbm|gif|bmp)", TAG_DONE);
    if (fr) {
        if (AslRequest(fr, NULL) && fr->fr_File && fr->fr_File[0]) {
            g_pictureDrawer = fr->fr_Drawer ? (const char *)fr->fr_Drawer : "";
            out = boxer::fsutil::join(g_pictureDrawer, (const char *)fr->fr_File);
        }
        FreeAslRequest(fr);
    }
    CloseLibrary(AslBase);
    AslBase = nullptr;
    return out;
}

static std::string iconStemOf(const std::string &gameboxPath) {
    return boxer::fsutil::stripExtension(boxer::fsutil::trimTrailingSlash(gameboxPath));
}

// Cover choice from the cycle (index into kCoverStyles) or the well (-1:
// a picture). Records it, redraws the well and rewrites the icon.
static void changeCover(const std::string &gameboxPath, int index, Object *win, Object *well, Object *cycle,
                        Object *name) {
    boxer::Gamebox box;
    std::string err;
    if (!box.open(gameboxPath, &err)) {
        MUI_Request(ui.app, win, 0, (char *)"Boxer", (char *)"OK", (char *)"%s", (IPTR)err.c_str());
        return;
    }
    const boxer::CoverStyle style = index < 0 ? boxer::CoverStyle::Picture : kCoverStyles[index];
    bool ok = true;
    if (style == boxer::CoverStyle::Picture) {
        const std::string pic = pickPicture();
        if (pic.empty()) { logf("cover: picture chooser cancelled"); showCover(well, cycle, nullptr, gameboxPath); return; }
        // Only a picture the datatypes can read is taken into the gamebox.
        boxer::RGBAImage probe;
        if (!box.gameInfo().get("AROSCoverArt")) {
            // Kept in the record, for a later return to the bootleg cover.
            boxer::CoverChoice c;
            c.detected = boxer::mediumOfGameAt(box.path());
            boxer::setCoverChoice(box, c);
        }
        ok = boxer::loadPicture(pic, probe, &err) && boxer::storeCoverPicture(box, pic, &err);
        logf("cover: picture \"%s\" (%dx%d) -> %s%s", pic.c_str(), probe.w, probe.h, ok ? "stored" : "NOT stored: ", err.c_str());
        if (!ok) {
            std::string text = "This picture cannot be used as the cover:\n" + err;
            MUI_Request(ui.app, win, 0, (char *)"Boxer", (char *)"OK", (char *)"%s", (IPTR)text.c_str());
        }
    } else {
        boxer::CoverChoice c = boxer::readCoverChoice(box);
        if (!box.gameInfo().get("AROSCoverArt")) c.detected = boxer::mediumOfGameAt(box.path());
        const std::string oldPicture = c.style == boxer::CoverStyle::Picture ? c.picture : std::string();
        c.style = style;
        boxer::setCoverChoice(box, c);
        ok = box.saveGameInfo(&err);
        // BXImportFinishedPanelController addCoverArt: with no image the
        // bootleg icon comes back; the picture file is not needed then.
        if (ok && !oldPicture.empty()) std::remove(boxer::fsutil::join(box.path(), oldPicture).c_str());
        logf("cover: style %s -> %s%s", boxer::coverStyleName(style), ok ? "recorded" : "NOT recorded: ", err.c_str());
        if (!ok) MUI_Request(ui.app, win, 0, (char *)"Boxer", (char *)"OK", (char *)"The cover could not be changed:\n%s", (IPTR)err.c_str());
    }
    if (ok && !writeCoverIcon(gameboxPath, iconStemOf(gameboxPath), true, err)) {
        std::string text = "The cover was changed, but the game's icon could not be updated:\n" + err;
        MUI_Request(ui.app, win, 0, (char *)"Boxer", (char *)"OK", (char *)"%s", (IPTR)text.c_str());
    }
    showCover(well, cycle, name, gameboxPath);
}

// setGameboxName: for the gamebox and its icon. Returns the gamebox's path afterwards;
// every outcome but a clean rename is reported.
static std::string renameGamebox(const std::string &gameboxPath, const std::string &newName, Object *win) {
    boxer::RenameOutcome o = boxer::renameGameboxPair(gameboxPath, newName,
        [](const std::string &stem, std::string *err) {
            // The bootleg title follows the name (IS:501-502); the icon's
            // GAMEBOX ToolType names the renamed gamebox.
            std::string e;
            bool ok = writeCoverIcon(stem + ".boxer", stem, true, e);
            if (err) *err = e;
            return ok;
        });
    logf("rename: \"%s\" -> \"%s\": outcome %d, now \"%s\"%s%s", gameboxPath.c_str(), newName.c_str(), (int)o.kind,
         o.gameboxPath.c_str(), o.message.empty() ? "" : ": ", o.message.c_str());
    if (!o.message.empty())
        MUI_Request(ui.app, win, 0, (char *)"Boxer", (char *)"OK", (char *)"%s", (IPTR)o.message.c_str());
    return o.gameboxPath;
}

// The finished panel's name field: committed on Return and before the
// window is left (BXImportFinishedPanelController launchGamebox:).
static bool commitImportName() {
    STRPTR text = nullptr;
    get(ui.impName, MUIA_String_Contents, &text);
    const std::string want = text ? (const char *)text : "";
    if (want == g_import.gameName()) return true;
    const std::string now = renameGamebox(g_import.createdGamebox, want, ui.imp);
    g_import.createdGamebox = now;
    showCover(ui.impWell, ui.impCover, ui.impName, now);   // the field shows the name it has now
    std::string line = "\"" + g_import.gameName() + "\" is in " + g_import.gamesFolder + ".";
    set(ui.impDoneText, MUIA_Text_Contents, (IPTR)line.c_str());
    return g_import.gameName() == boxer::validGameboxName(want);
}

// Inspector Gamebox tab for the session's gamebox; ghosted without one.
static void inspectorGameboxSync() {
    boxer::Gamebox box;
    const bool have = !g_args.gamebox.empty() && box.open(g_args.gamebox);
    set(ui.tabs[0], MUIA_Disabled, !have);
    if (!have) return;
    showCover(ui.inspWell, ui.inspCover, ui.inspName, g_args.gamebox);
    std::string launch = "Nothing (show a DOS prompt)";
    for (const auto &l : box.launchers())
        if (l.isDefault) launch = l.title.empty() ? l.path : l.title;
    set(ui.inspLaunch, MUIA_Text_Contents, (IPTR)launch.c_str());
    nnset(ui.inspCloseOnExit, MUIA_Selected, box.closeAfterDefaultProgram());
    if (!g_pendingRename.empty()) nnset(ui.inspName, MUIA_String_Contents, (IPTR)g_pendingRename.c_str());
}

// Inspector name field: checked now, applied when the session ends.
static void inspectorRename() {
    STRPTR text = nullptr;
    get(ui.inspName, MUIA_String_Contents, &text);
    const std::string want = text ? (const char *)text : "";
    std::string name, msg;
    const boxer::NameCheck c = boxer::checkGameboxRename(g_args.gamebox, want, name, &msg);
    logf("rename (inspector): \"%s\" -> check %d \"%s\"", want.c_str(), (int)c, name.c_str());
    if (c == boxer::NameCheck::Unchanged) {
        g_pendingRename.clear();
        setLabelText(ui.inspNote, "");
        return;
    }
    if (c != boxer::NameCheck::Ok) {
        MUI_Request(ui.app, ui.insp, 0, (char *)"Boxer", (char *)"OK", (char *)"%s", (IPTR)msg.c_str());
        nnset(ui.inspName, MUIA_String_Contents,
              (IPTR)(g_pendingRename.empty() ? boxer::fsutil::baseName(iconStemOf(g_args.gamebox)) : g_pendingRename).c_str());
        return;
    }
    g_pendingRename = name;
    nnset(ui.inspName, MUIA_String_Contents, (IPTR)name.c_str());
    // The drives stay mounted from the old path until the session ends, so
    // the note must say the rename has not happened yet: the field already
    // shows the new name, and a past-tense note read as if it were done.
    setLabelText(ui.inspNote, "Rename pending: applies when the game is closed.");
}

// Returns false to end the application.
// BXImportWindowController panel choice for the session's stage.
static void showImportStage() {
    using S = boxer::ImportStage;
    const std::string name = boxer::fsutil::baseName(g_import.sourcePath);
    switch (g_import.stage) {
    case S::WaitingForSource:
    case S::LoadingSource:
        set(ui.impPages, MUIA_Group_ActivePage, 0);
        break;
    case S::WaitingForInstaller: {
        std::string line = "Source: " + g_import.sourcePath;
        set(ui.impSource, MUIA_Text_Contents, (IPTR)line.c_str());
        DoMethod(ui.impList, MUIM_List_Clear);
        for (const auto &p : g_import.scan.installers)
            DoMethod(ui.impList, MUIM_List_InsertSingle, (IPTR)p.c_str(), MUIV_List_Insert_Bottom);
        set(ui.impList, MUIA_List_Active, 0);   // preferred installer first (IP:246)
        // One core run per process; a second installer needs a new
        // BoxerUI (handing over to one is not implemented).
        set(ui.impLaunch, MUIA_Disabled, g_ss.ran);
        set(ui.impPages, MUIA_Group_ActivePage, 1);
        break;
    }
    case S::RunningInstaller:
        break;   // the DOS window is in front; the Import window stays as it is
    case S::ChoosingStartupProgram: {
        boxer::FileSystem fs;
        std::string line = "Choose the program that starts \"" + g_import.gameName() + "\":";
        set(ui.impProgText, MUIA_Text_Contents, (IPTR)line.c_str());
        DoMethod(ui.impProgList, MUIM_List_Clear);
        auto progs = boxer::scanExecutables(fs, g_import.rootDrivePath());
        std::vector<std::string> rel;
        for (const auto &p : progs) rel.push_back(boxer::fsutil::relativeTo(p, g_import.rootDrivePath()));
        boxer::sortByPathDepth(rel);
        for (const auto &r : rel)
            DoMethod(ui.impProgList, MUIM_List_InsertSingle, (IPTR)r.c_str(), MUIV_List_Insert_Bottom);
        const int pick = boxer::preferredStartupProgram(rel, g_import.gameName());
        set(ui.impProgList, MUIA_List_Active, pick < 0 ? 0 : pick);
        set(ui.impUse, MUIA_Disabled, rel.empty());
        logf("import: %u programs in the gamebox, preselected %d", (unsigned)rel.size(), pick);
        set(ui.impPages, MUIA_Group_ActivePage, 3);
        break;
    }
    case S::Finished: {
        std::string line = "\"" + g_import.gameName() + "\" is in " + g_import.gamesFolder + ".";
        set(ui.impDoneText, MUIA_Text_Contents, (IPTR)line.c_str());
        showCover(ui.impWell, ui.impCover, ui.impName, g_import.createdGamebox);
        set(ui.impPages, MUIA_Group_ActivePage, 4);
        break;
    }
    case S::ImportingSourceFiles: {
        std::string line = "Importing " + g_import.gameName() + "...";
        set(ui.impCopyText, MUIA_Text_Contents, (IPTR)line.c_str());
        set(ui.impGauge, MUIA_Gauge_Current, 0);
        set(ui.impPages, MUIA_Group_ActivePage, 5);
        break;
    }
    case S::ReadyToFinalize: {
        // Once a gamebox exists (an installer ran) there is no source
        // choice to go back to; Stop is the window's close question.
        set(ui.impBack2, MUIA_Disabled, !g_import.createdGamebox.empty());
        // A long name is shortened on the panel; the bubble help gives it
        // in full (Zune keeps the pointer, so the string must outlive this).
        static std::string fullName;
        fullName = name;
        const std::string line = boxer::readyToImportText(name);
        set(ui.impReady, MUIA_Text_Contents, (IPTR)line.c_str());
        set(ui.impReady, MUIA_ShortHelp, (IPTR)fullName.c_str());
        set(ui.impPages, MUIA_Group_ActivePage, 2);
        break;
    }
    }
    logf("import: stage %d", (int)g_import.stage);
}

// IS:715 finishInstaller / IS:_cleanup, after the installer's session ended.
static void importAfterInstaller() {
    g_inst.pending = false;
    if (g_inst.stop) {
        std::string err;
        bool ok = g_import.discardGamebox(&err);
        logf("import: stopped; unfinished gamebox %s%s", ok ? "deleted" : "NOT deleted: ", err.c_str());
        g_import.cancelSourceSelection();
        if (!ok) MUI_Request(ui.app, ui.imp, 0, (char *)"Import a Game", (char *)"OK", (char *)"%s", (IPTR)err.c_str());
    } else if (g_import.gameDidInstall()) {
        g_import.stage = boxer::ImportStage::ChoosingStartupProgram;
        logf("import: installer finished, files in the gamebox");
    } else {
        // Nothing was installed: the source files are imported instead
        // (IS:745).
        g_import.stage = boxer::ImportStage::ReadyToFinalize;
        logf("import: installer finished without installing files");
    }
    g_inst.finish = g_inst.stop = false;
    set(ui.imp, MUIA_Window_Open, TRUE);
    showImportStage();
}

// Stop or failure while copying: only the new gamebox goes; the source stays.
static void importAbandon(const std::string &why) {
    if (g_copy) { g_copy->close(); delete g_copy; g_copy = nullptr; }
    const std::string name = g_import.gameName(), source = g_import.sourcePath;
    std::string err;
    bool ok = g_import.discardGamebox(&err);
    logf("import: abandoned (%s); unfinished gamebox %s%s", why.c_str(), ok ? "deleted" : "NOT deleted: ", err.c_str());
    g_import.cancelSourceSelection();
    showImportStage();
    if (why != "stopped" || !ok) {
        std::string text = "Boxer could not import " + name + ":\n" + why + "\n\n" +
            (ok ? "The unfinished gamebox was removed. " : "The unfinished gamebox could not be removed: " + err + ". ") +
            "The game folder " + source + " was not changed.";
        MUI_Request(ui.app, ui.imp, 0, (char *)"Import a Game", (char *)"OK", (char *)"%s", (IPTR)text.c_str());
    }
}

// BXCloseAlert closeAlertWhileRunningInstaller. Returns false for Cancel.
static bool askEndInstaller() {
    const std::string name = g_import.gameName();
    LONG r = MUI_Request(ui.app, ui.dos, 0, (char *)"Boxer",
        (char *)"Finish Importing|Stop Importing|Cancel",
        (char *)"Do you want to finish importing %s first?\n\n"
                "If you stop importing, any already-imported game files will be discarded.",
        (IPTR)name.c_str());
    logf("import: close while installing -> %d", (int)r);
    if (r == 0) return false;
    g_inst.finish = r == 1;
    g_inst.stop = r == 2;
    g_ss.emu->requestQuit();
    return true;
}

static bool handleId(ULONG id) {
    switch (id) {
    case MUIV_Application_ReturnID_Quit:
    case ID_QUIT:
        if (g_ss.running && g_import.stage == boxer::ImportStage::RunningInstaller) {
            askEndInstaller();
            return true;
        }
        if (g_ss.running) {
            // Quit / window close end the session first (BXSession close ->
            // cancel the emulator, wait for it to finish), then the app.
            logf("quit: ending the session");
            g_ss.emu->requestQuit();
            g_ss.quitApp = true;
            return true;
        }
        return false;
    case ID_WELCOME_CLOSE:
        logf("welcome: close");
        set(ui.welcome, MUIA_Window_Open, FALSE);
        break;
    case ID_BROWSE:
        browseGames();
        break;
    case ID_IMPORT:
        // BXAppController orderFrontImportGamePanel: a fresh session at the dropzone.
        g_import = boxer::ImportSession();
        set(ui.impPages, MUIA_Group_ActivePage, 0);
        set(ui.imp, MUIA_Window_Open, TRUE);
        logf("import: window opened (waiting for source)");
        break;
    case ID_IMPORT_CHOOSE: {
        std::string d = pickDrawer(g_import.sourcePath.empty() ? std::string("SYS:") : g_import.sourcePath,
                                   "Choose a DOS game folder to import:", false);
        if (d.empty()) { logf("import: chooser cancelled, still waiting for source"); break; }
        // SYS: and its volume name are the same place; the reserved-folder
        // rules compare paths, so both sides must be in DOS's own form.
        d = canonicalDir(d);
        boxer::DataLocations loc;
        loc.dataDir = g_args.data;
        std::string msg;
        auto c = g_import.chooseSource(d, loc, g_gamesFolder, &msg);
        logf("import: source \"%s\" -> %s", d.c_str(), c == boxer::SourceCheck::Ok ? "accepted" : msg.c_str());
        if (c != boxer::SourceCheck::Ok) {
            MUI_Request(ui.app, ui.imp, 0, (char *)"Import a Game", (char *)"OK", (char *)"%s", (IPTR)msg.c_str());
            break;
        }
        // BXInstallerScan, run in the UI task: folders are small next to
        // the copy that follows, and the busy pointer covers the wait.
        set(ui.app, MUIA_Application_Sleep, TRUE);
        boxer::FileSystem fs;
        auto scan = boxer::scanForInstallers(fs, d);
        set(ui.app, MUIA_Application_Sleep, FALSE);
        logf("import: scan of \"%s\": %u DOS programs, %u Windows, %u confs, installed=%d, %u installers%s%s",
             d.c_str(), (unsigned)scan.dosExecutables.size(), (unsigned)scan.windowsExecutables.size(),
             (unsigned)scan.dosboxConfigurations.size(), (int)scan.alreadyInstalled,
             (unsigned)scan.installers.size(), scan.installers.empty() ? "" : ", first ",
             scan.installers.empty() ? "" : scan.installers.front().c_str());
        auto err = g_import.applyScan(scan);
        if (err != boxer::InstallerScanError::None) {
            std::string text = boxer::installerScanErrorText(err, boxer::fsutil::baseName(d));
            logf("import: scan refused the source: %s", text.c_str());
            MUI_Request(ui.app, ui.imp, 0, (char *)"Import a Game", (char *)"OK", (char *)"%s", (IPTR)text.c_str());
            break;
        }
        showImportStage();
        break;
    }
    case ID_IMPORT_SKIP:
        g_import.skipInstaller();
        logf("import: installer skipped");
        showImportStage();
        break;
    case ID_IMPORT_BACK2:
        // Nothing is created before the gamebox step, so going back from
        // "ready" is as harmless as going back from the installer choice.
        [[fallthrough]];
    case ID_IMPORT_BACK:
        g_import.cancelSourceSelection();
        set(ui.impPages, MUIA_Group_ActivePage, 0);
        logf("import: back to source selection");
        break;
    case ID_IMPORT_LAUNCH: {
        IPTR active = 0;
        get(ui.impList, MUIA_List_Active, &active);
        if ((LONG)active < 0 || active >= g_import.scan.installers.size()) break;
        std::string err;
        if (!g_import.createGamebox(g_gamesFolder, &err)) {
            logf("import: gamebox not created: %s", err.c_str());
            MUI_Request(ui.app, ui.imp, 0, (char *)"Import a Game", (char *)"OK",
                        (char *)"The gamebox could not be created:\n%s", (IPTR)err.c_str());
            break;
        }
        g_import.installerPath = g_import.scan.installers[active];
        g_import.stage = boxer::ImportStage::RunningInstaller;
        g_inst = {};
        g_inst.pending = true;
        logf("import: created \"%s\" (id %s), running installer %s", g_import.createdGamebox.c_str(),
             g_import.createdIdentifier.c_str(), g_import.installerPath.c_str());
        break;
    }
    case ID_IMPORT_USE: {
        IPTR active = 0;
        STRPTR entry = nullptr;
        get(ui.impProgList, MUIA_List_Active, &active);
        DoMethod(ui.impProgList, MUIM_List_GetEntry, active, (IPTR)&entry);
        if (!entry) break;
        boxer::Gamebox box;
        std::string err;
        const std::string rel = std::string("C.harddisk/") + (const char *)entry;
        bool ok = box.open(g_import.createdGamebox, &err) && boxer::setDefaultLauncher(box, rel, &err);
        logf("import: startup program %s -> %s%s", rel.c_str(), ok ? "saved" : "NOT saved: ", err.c_str());
        if (!ok) {
            MUI_Request(ui.app, ui.imp, 0, (char *)"Import a Game", (char *)"OK",
                        (char *)"The startup program could not be saved:\n%s", (IPTR)err.c_str());
            break;
        }
        // Sidecar launch icon "<name>.info" beside "<name>.boxer",
        // showing the cover. A failure is reported but does not undo
        // the import: the gamebox is complete without it.
        ensureCoverRecord(box, g_import.sourcePath);
        std::string iconErr;
        bool icon = writeCoverIcon(g_import.createdGamebox, iconStemOf(g_import.createdGamebox), false, iconErr);
        logf("import: sidecar icon %s%s", icon ? "written" : "NOT written: ", iconErr.c_str());
        if (!icon)
            MUI_Request(ui.app, ui.imp, 0, (char *)"Import a Game", (char *)"OK",
                        (char *)"The game was imported, but its icon could not be written:\n%s", (IPTR)iconErr.c_str());
        g_import.stage = boxer::ImportStage::Finished;
        showImportStage();
        break;
    }
    case ID_IMPORT_CREATE: {
        // importSourceFiles for a game that installed nothing (IS:745-1037).
        std::string err, dest;
        if (g_import.createdGamebox.empty() && !g_import.createGamebox(g_gamesFolder, &err)) {
            logf("import: gamebox not created: %s", err.c_str());
            MUI_Request(ui.app, ui.imp, 0, (char *)"Import a Game", (char *)"OK",
                        (char *)"The gamebox could not be created:\n%s", (IPTR)err.c_str());
            break;
        }
        g_copy = new boxer::SourceCopy;
        if (!g_import.prepareCopyDestination(dest, &err) || !g_copy->prepare(g_import.sourcePath, dest, &err)) {
            importAbandon(err);
            break;
        }
        g_import.stage = boxer::ImportStage::ImportingSourceFiles;
        logf("import: copying %u files, %llu bytes from \"%s\" to \"%s\"", (unsigned)g_copy->fileCount(),
             (unsigned long long)g_copy->totalBytes(), g_import.sourcePath.c_str(), dest.c_str());
        showImportStage();
        break;
    }
    case ID_IMPORT_STOP: {
        if (!g_copy) break;
        LONG r = MUI_Request(ui.app, ui.imp, 0, (char *)"Boxer", (char *)"Stop Importing|Cancel",
            (char *)"Boxer has not finished importing %s.\n\n"
                    "If you stop importing, any already-imported game files will be discarded.",
            (IPTR)g_import.gameName().c_str());
        logf("import: stop while copying -> %d", (int)r);
        if (r == 1) importAbandon("stopped");
        break;
    }
    case ID_IMPORT_LAUNCH_GAME: {
        // The game runs in a new BoxerUI process (this one may already
        // have used its one core run). Opened as Wanderer would open the
        // sidecar icon: a SystemTags start left BoxerUI's drawer locked after
        // both processes ended (SystemTags drops NP_HomeDir), the
        // icon start did not. This process ends only after the open succeeded.
        // A name being typed is committed first; a refused one keeps the
        // panel (launchGamebox: makeFirstResponder:nil fails on validation).
        if (!commitImportName()) break;
        const std::string icon = boxer::fsutil::join(g_import.gamesFolder, g_import.gameName());
        const bool ok = openInWanderer(icon);
        logf("import: launch game: open \"%s\" -> %s", icon.c_str(), ok ? "ok" : "failed");
        if (!ok) {
            MUI_Request(ui.app, ui.imp, 0, (char *)"Import a Game", (char *)"OK",
                        (char *)"The game could not be started.\nOpen it from its icon in %s.",
                        (IPTR)g_import.gamesFolder.c_str());
            break;
        }
        return false;   // quit this BoxerUI
    }
    case ID_IMPORT_DONE_CLOSE:
        if (!commitImportName()) break;
        set(ui.imp, MUIA_Window_Open, FALSE);
        logf("import: finished window closed");
        break;
    case ID_IMPORT_NAME:
        if (g_import.stage == boxer::ImportStage::Finished) commitImportName();
        break;
    case ID_IMPORT_COVER:
    case ID_IMPORT_WELL: {
        if (g_import.stage != boxer::ImportStage::Finished) break;
        IPTR idx = 0;
        get(ui.impCover, MUIA_Cycle_Active, &idx);
        changeCover(g_import.createdGamebox, id == ID_IMPORT_WELL ? -1 : (int)idx, ui.imp, ui.impWell, ui.impCover,
                    ui.impName);
        break;
    }
    case ID_INSP_NAME:
        if (!g_args.gamebox.empty()) inspectorRename();
        break;
    case ID_INSP_COVER:
    case ID_INSP_WELL: {
        if (g_args.gamebox.empty()) break;
        IPTR idx = 0;
        get(ui.inspCover, MUIA_Cycle_Active, &idx);
        changeCover(g_args.gamebox, id == ID_INSP_WELL ? -1 : (int)idx, ui.insp, ui.inspWell, ui.inspCover, nullptr);
        break;
    }
    case ID_IMPORT_CLOSE:
        if (g_import.stage == boxer::ImportStage::RunningInstaller) {
            if (g_ss.running) askEndInstaller();
            break;
        }
        if (g_import.stage == boxer::ImportStage::Finished) {
            if (!commitImportName()) break;
            set(ui.imp, MUIA_Window_Open, FALSE);
            break;
        }
        if (g_copy) {
            handleId(ID_IMPORT_STOP);
            if (g_copy) break;      // Cancel: keep copying
            set(ui.imp, MUIA_Window_Open, FALSE);
            break;
        }
        if (!g_import.createdGamebox.empty()) {
            // BXCloseAlert closeAlertWhileImportingGame.
            const std::string name = g_import.gameName();
            LONG r = MUI_Request(ui.app, ui.imp, 0, (char *)"Boxer", (char *)"Stop Importing|Cancel",
                (char *)"Boxer has not finished importing %s.\n\n"
                        "If you stop importing, any already-imported game files will be discarded.",
                (IPTR)name.c_str());
            if (r != 1) break;
            std::string err;
            bool ok = g_import.discardGamebox(&err);
            logf("import: stopped; unfinished gamebox %s%s", ok ? "deleted" : "NOT deleted: ", err.c_str());
        }
        // Nothing has been created at these stages, so closing only forgets
        // the source (IS:311 asks first only once a gamebox exists).
        g_import.cancelSourceSelection();
        set(ui.imp, MUIA_Window_Open, FALSE);
        logf("import: window closed, nothing created");
        break;
    case ID_PROMPT:
        logf("welcome: Open a DOS prompt -> DOS window");
        set(ui.dos, MUIA_Window_Open, TRUE);
        if (!g_args.confDir.empty() && !g_ss.running && !g_ss.ran) g_args.kind = "dos-pending";
        break;
    case ID_PROGRAMS: {
        IPTR v = 0; get(ui.programs, MUIA_Selected, &v);
        logf("toolbar: Programs -> launchPanelShown=%d", (int)v);
        if ((bool)v != st.launchPanel) lpToggle("toolbar Programs");
        break;
    }
    case ID_LP_TOGGLE: lpToggle("RAmiga+G"); break;
    case ID_LP_FILTER: {
        // enterSearchText: (sendsSearchStringImmediately): filter as typed.
        STRPTR text = nullptr;
        get(ui.lpSearch, MUIA_String_Contents, &text);
        g_lp.filter = text ? (const char *)text : "";
        logf("launch panel: search \"%s\"", g_lp.filter.c_str());
        lpRebuild("filter", false);
        break;
    }
    case ID_LP_ENTER:
        // control:textView:doCommandBySelector: insertNewline: is a TODO in
        // 2.0-alpha ("launch the first search result"): nothing is launched.
        logf("launch panel: Return in the search field: no launch (2.0-alpha behaviour)");
        break;
    case ID_LP_ACTION: {
        int row = g_lp.pendingRow, action = g_lp.pendingAction;
        g_lp.pendingRow = -1; g_lp.pendingAction = 0;
        if (action == LA_Open) lpOpen(row);
        else if (action == LA_Remove) lpRemove(row);
        break;
    }
    case ID_SEG0: case ID_SEG1: case ID_SEG2: {
        st.playback = id - ID_SEG0;
        for (int i = 0; i < 3; ++i) {
            nnset(ui.seg[i], MUIA_Selected, i == st.playback);
            MUI_Redraw(ui.seg[i], MADF_DRAWOBJECT);
        }
        logf("toolbar: playback mode %d", (int)(id - ID_SEG0));
        if (id == ID_SEG2) setTurbo(true, "toolbar");
        else setPaused(id == ID_SEG0, "toolbar");
        if (!g_ss.running) st.playback = id - ID_SEG0;
        break;
    }
    case ID_VOL_MIN: set(ui.volSlider, MUIA_Numeric_Value, 0); break;
    case ID_VOL_MAX: set(ui.volSlider, MUIA_Numeric_Value, 100); break;
    case ID_VOLUME: {
        IPTR v = 0; get(ui.volSlider, MUIA_Numeric_Value, &v);
        logf("toolbar: volume %d", (int)v);
        applyVolume();
        break;
    }
    case ID_INSPECTOR: {
        IPTR o = 0; get(ui.insp, MUIA_Window_Open, &o);
        set(ui.insp, MUIA_Window_Open, !o);
        logf("toolbar: inspector %s", o ? "hidden" : "shown");
        break;
    }
    case ID_INSPECTOR_CLOSE: set(ui.insp, MUIA_Window_Open, FALSE); break;
    case ID_FULLSCREEN: setFullscreen(!st.fullscreen); break;
    case ID_LOCK_BUTTON: {
        IPTR v = 0; get(ui.lockBtn, MUIA_Selected, &v);
        if ((bool)v != st.locked) {
            if (g_ss.running) setLocked(v, "lock button");
            else { st.locked = v; logf("lock button -> %d", st.locked); updateStatus(); }
        }
        break;
    }
    case ID_LOCK_KEY:
        if (st.mouseActive) {
            if (g_ss.running) setLocked(!st.locked, "RAmiga+L");
            else { st.locked = !st.locked; logf("Amiga+L -> locked=%d", st.locked); updateStatus(); }
        }
        break;
    case ID_MOUSE_ACTIVE:
        if (g_ss.running) break;   // the DOS program decides while a session runs
        st.mouseActive = !st.mouseActive; if (!st.mouseActive) st.locked = false; updateStatus(); break;
    case ID_TRACK: st.track = !st.track; updateStatus(); break;
    case ID_DUMP: dumpGeometry(g_ss.running ? "RAmiga+D" : "F3"); break;
    case ID_DUMP_LATER:
        if (!g_laterArmed) {
            g_laterIhn.ihn_Object = ui.toolbar;
            g_laterIhn.ihn_Flags = MUIIHNF_TIMER;
            g_laterIhn.ihn_Millis = 2500;
            g_laterIhn.ihn_Method = PGM_Tick;
            DoMethod(ui.app, MUIM_Application_AddInputHandler, (IPTR)&g_laterIhn);
            g_laterArmed = true;
            logf("harness: dump armed for 2.5 s (dump #%d will be next)", g_dumpSerial + 1);
        }
        break;
    case ID_STYLE:
        g_ss.smooth = !g_ss.smooth;
        g_args.style = g_ss.smooth ? "smoothed" : "normal";
        logf("harness: rendering style %s", g_args.style.c_str());
        if (g_ss.running && g_ss.haveFrame) {
            g_ss.last.smooth = g_ss.smooth;
            renderSetFrame(activeRender(), &g_ss.last);
        }
        break;
    case ID_ASPECT:
        g_ss.aspect = !g_ss.aspect;
        logf("aspect correction %s (RAmiga+Shift+A)", g_ss.aspect ? "on" : "off");
        if (g_ss.running && g_host && g_ss.frame.width) g_host->frameSizeChanged(g_ss.frame);
        break;
    case ID_SIZE1: resizeRenderTo(640, 480, true); break;
    case ID_SIZE2: resizeRenderTo(720, 360, false); break;
    case ID_SIZE3: resizeRenderTo(480, 360, true); break;
    case ID_SPEED: {
        IPTR v = 0; get(ui.speedSlider, MUIA_Numeric_Value, &v);
        SpeedChange c = applySpeedSlider((LONG)v / 1000.0, true);
        st.speed = c.sessionSpeed;
        LONG knob = std::lround(c.knobRatio * 1000);
        if (knob != (LONG)v) nnset(ui.speedSlider, MUIA_Numeric_Value, knob);
        updateSpeedText();
        logf("cpu: slider %d -> speed %d, knob %d, \"%s\"", (int)(LONG)v, st.speed, (int)knob, g_speedText);
        if (g_ss.running) {
            // BXSession setCPUSpeed:/setAutoSpeed: turn fast forward off.
            setTurbo(false, "speed change");
            logStats(true);
            boxer::coreSetSpeed(st.speed == kAutoSpeed ? -1 : st.speed);
            bool a = false;
            int now = boxer::coreCycles(&a);   // before reading a: argument order is unspecified
            logf("core: speed set, cycles now %d auto=%d", now, a);
        }
        break;
    }
    case ID_FRAMES: {
        IPTR v = 0; get(ui.frameSlider, MUIA_Numeric_Value, &v);
        st.frameskip = -(int)(LONG)v;
        updateSpeedText();
        logf("cpu: frameskip %d \"%s\"", st.frameskip, g_frameText);
        if (g_ss.running) {
            logStats(true);
            boxer::coreSetFrameskip(st.frameskip);
            logf("core: frameskip now %d", boxer::coreFrameskip());
        }
        break;
    }
    case ID_EMULATING:
        // Harness stand-in for document.emulating (xib: enabled bindings
        // of both sliders and the checkbox; descriptions empty when NO).
        st.emulating = !st.emulating;
        set(ui.speedSlider, MUIA_Disabled, !st.emulating);
        set(ui.frameSlider, MUIA_Disabled, !st.emulating);
        updateSpeedText();
        logf("harness: emulating=%d", st.emulating);
        break;
    case ID_ACTIVATION:
        for (Object *w : g_wins) {
            IPTR o = 0, root = 0;
            get(w, MUIA_Window_Open, &o);
            if (!o) continue;
            get(w, MUIA_Window_RootObject, &root);
            MUI_Redraw((Object *)root, MADF_DRAWOBJECT);
        }
        for (Object *b : ui.wb) {
            IPTR o = 0; get(ui.welcome, MUIA_Window_Open, &o);
            if (o) DoMethod(b, WBM_SyncHover);
        }
        {
            IPTR a1 = 0, a2 = 0, a3 = 0;
            get(ui.welcome, MUIA_Window_Activate, &a1);
            get(ui.dos, MUIA_Window_Activate, &a2);
            get(ui.insp, MUIA_Window_Activate, &a3);
            IPTR a4 = 0;
            get(ui.fs, MUIA_Window_Activate, &a4);
            logf("activation: welcome=%d dos=%d inspector=%d fullscreen=%d", (int)a1, (int)a2, (int)a3, (int)a4);
            // windowDidResignKey -> didResignKey: unlock, release keys and
            // buttons (BXDOSWindowController.m:1569, BXInputController.m:403).
            if (g_ss.running && !a2 && !a4) {
                setLocked(false, "window inactive");
                releaseInput("window inactive");
            }
        }
        break;
    default: break;
    }
    if (id >= ID_TAB0 && id <= ID_TAB4) selectTab(id - ID_TAB0);
    return true;
}

// A sidecar icon whose "<name>.boxer" is not beside it (renamed or moved
// apart, or a rename that could not take the icon along): its GAMEBOX
// and BOXERID ToolTypes find the gamebox. One gamebox with the icon's
// identifier is offered with a repair of the icon's name; anything else
// (several, or a GAMEBOX whose identifier differs) is the user's choice.
// Sets g_args.gamebox when the user picks one. Runs before the windows.
static void findSidecarGamebox() {
    const std::string stem = iconStemOf(g_args.missingGamebox);
    const std::string folder = boxer::fsutil::parent(stem);
    const auto found = boxer::locateSidecarGamebox(folder, g_args.sidecarGamebox, g_args.sidecarId);
    logf("start: sidecar \"%s\" (GAMEBOX \"%s\", BOXERID \"%s\"): %u candidate(s)", stem.c_str(),
         g_args.sidecarGamebox.c_str(), g_args.sidecarId.c_str(), (unsigned)found.size());
    if (found.empty()) return;
    const std::string iconName = boxer::fsutil::baseName(stem);
    std::string chosen;
    if (boxer::sidecarLookupIsCertain(found)) {
        const std::string game = boxer::fsutil::baseName(iconStemOf(found[0].path));
        LONG r = ask("The icon \"%s\" belongs to the game that is now called \"%s\".\n\n"
                     "Should Boxer give the icon the game's name?",
                     "Rename the icon|Open without renaming|Cancel", iconName.c_str(), game.c_str());
        logf("start: sidecar lookup -> answer %d", (int)r);
        if (r == 0) return;
        chosen = found[0].path;
        if (r == 1) {
            std::string err;
            bool ok = boxer::repairSidecarName(stem, chosen, [](const std::string &s, std::string *e) {
                std::string m;
                bool w = writeCoverIcon(s + ".boxer", s, true, m);
                if (e) *e = m;
                return w;
            }, &err);
            logf("start: icon \"%s\" renamed for \"%s\" -> %s%s", stem.c_str(), chosen.c_str(),
                 ok ? "ok" : "FAILED: ", err.c_str());
            if (!ok) ask("The icon could not be renamed:\n%s\n\nThe game opens anyway.", "OK", err.c_str());
        }
    } else {
        // Ask, never guess: list what was found (at most three choices).
        std::string text = "The icon \"" + iconName + "\" no longer has a game of the same name beside it.\n"
                           "These games match it:\n";
        std::string gadgets;
        const size_t n = std::min<size_t>(found.size(), 3);
        for (size_t i = 0; i < found.size(); ++i) {
            const std::string g = boxer::fsutil::baseName(iconStemOf(found[i].path));
            text += "\n  " + g + (found[i].identifierMatches ? " (same identifier)" : " (named by the icon, other identifier)");
            if (i < n) gadgets += g + "|";
        }
        text += "\n\nWhich game should open?";
        gadgets += "Cancel";
        // EasyRequest formats its text: a % in a name must not be taken as one.
        std::string safe;
        for (char c : text) { safe += c; if (c == '%') safe += '%'; }
        LONG r = ask(safe.c_str(), gadgets.c_str());
        logf("start: sidecar lookup, %u matches -> answer %d", (unsigned)found.size(), (int)r);
        if (r <= 0 || (size_t)r > n) return;
        chosen = found[r - 1].path;
    }
    g_args.gamebox = chosen;
    g_args.missingGamebox.clear();
    g_screen = "dos";
}

int main(int argc, char **argv) {
    memInit();
    if (memcp("main-entry")) { memDone(); return 0; }
    g_mainTask = FindTask(NULL);
    g_fromWorkbench = argc == 0;
    bool argsOk = argc > 0 ? parseArgs(argc, argv) : (parseToolTypes((struct WBStartup *)argv), true);
    if (!argsOk) return 20;
    applyDefaults();
    if (!g_args.gamebox.empty() || g_args.kind == "dos") g_screen = "dos";
    g_log = std::fopen(g_logPath.c_str(), "w");
    const char *screen = g_screen.c_str();   // refreshed if a sidecar lookup changes g_screen
    logf("BoxerUI, built %s %s, abi %s, screen=%s", __DATE__, __TIME__,
#ifdef BOXER_ABI
         BOXER_ABI,
#else
         "unknown",
#endif
         screen);
    logLibraries("main");
    {
        char cwd[1024] = "";
        NameFromLock(((struct Process *)FindTask(NULL))->pr_CurrentDir, (STRPTR)cwd, sizeof cwd);
        logf("start: from %s, PROGDIR \"%s\", current dir \"%s\"", g_fromWorkbench ? "Workbench" : "Shell",
             g_progDir.c_str(), cwd);
        for (const auto &n : g_wbNotes) logf("start: %s", n.c_str());
        // Gamebox icons name "Boxer:BoxerUI" as default tool (installer's
        // assign, packaging/Install-Assign). Workbench fails before BoxerUI
        // runs when it is missing, so only the Shell start can say so.
        // Looked up in the DOS list: a plain Lock("Boxer:") on a missing
        // name opens an "insert volume Boxer" requester (seen k112d2b).
        bool haveAssign = false;
        if (struct DosList *dl = LockDosList(LDF_ASSIGNS | LDF_VOLUMES | LDF_DEVICES | LDF_READ)) {
            haveAssign = FindDosEntry(dl, (CONST_STRPTR)"Boxer", LDF_ASSIGNS | LDF_VOLUMES | LDF_DEVICES) != NULL;
            UnLockDosList(LDF_ASSIGNS | LDF_VOLUMES | LDF_DEVICES | LDF_READ);
        }
        BPTR bl = haveAssign ? Lock((CONST_STRPTR)"Boxer:", SHARED_LOCK) : 0;
        if (!bl) logf("start: assign Boxer: missing - gamebox icons cannot start BoxerUI (run Install-Assign)");
        else {
            logf("start: assign Boxer: %s", SameLock(bl, GetProgramDir()) == LOCK_SAME ? "is PROGDIR" : "points elsewhere");
            UnLock(bl);
        }
        logf("start: GAMEBOX \"%s\" DATA \"%s\" CONFDIR \"%s\" MSG \"%s\" SESSION \"%s\"",
             g_args.gamebox.c_str(), g_args.data.c_str(), g_args.confDir.c_str(), g_args.msg.c_str(),
             g_args.kind.c_str());
    }

    if (!g_args.missingGamebox.empty() && g_args.gamebox.empty()) {
        findSidecarGamebox();
        screen = g_screen.c_str();
    }
    if (!g_args.missingGamebox.empty() && g_args.gamebox.empty()) {
        logf("error: gamebox not found: \"%s\" (the icon was moved or renamed without it)",
             g_args.missingGamebox.c_str());
        struct EasyStruct es = {sizeof(struct EasyStruct), 0, (STRPTR)"Boxer",
                                (STRPTR)"The game could not be found:\n%s\n\nKeep the icon and the gamebox drawer\n"
                                        "together, with the same name.",
                                (STRPTR)"OK"};
        EasyRequest(NULL, &es, NULL, (IPTR)g_args.missingGamebox.c_str());
        logf("exit: rc 10 (missing gamebox)");
        if (g_log) std::fclose(g_log);
        return 10;
    }
    if (!resolveDataDir()) {
        logf("exit: rc 10 (no usable games folder or data directory)");
        if (g_log) std::fclose(g_log);
        return 10;
    }
    if (g_args.prefsOnly) {
        logf("exit: rc 0 (PREFSONLY, data \"%s\")", g_args.data.c_str());
        if (g_log) std::fclose(g_log);
        return 0;
    }
    if (memcp("before-fonts")) { memDone(); return 0; }
    openFonts(logf, g_args.fontCheck);   // never fails: falls back down to topaz.font/8
    if (memcp("fonts")) { closeFonts(); memcp("fonts-closed"); memDone(); return 0; }
    setAuditLog(logf);
    if (!createClasses()) { logf("FAIL: classes"); return 20; }
    if (memcp("classes")) { deleteClasses(); memcp("classes-deleted"); closeFonts(); memcp("fonts-closed"); memDone(); return 0; }

    ui.welcome = buildWelcome();
    if (memcp("welcome-built")) {
        MUI_DisposeObject(ui.welcome); memcp("welcome-disposed");
        deleteClasses(); closeFonts(); memcp("end"); memDone(); return 0;
    }
    ui.dos = buildDOS();
    if (memcp("dos-built")) {
        MUI_DisposeObject(ui.dos); memcp("dos-disposed"); MUI_DisposeObject(ui.welcome);
        deleteClasses(); closeFonts(); memcp("end"); memDone(); return 0;
    }
    ui.fs = buildFullscreen();
    if (memcp("fs-built")) {
        MUI_DisposeObject(ui.fs); memcp("fs-disposed"); MUI_DisposeObject(ui.dos); MUI_DisposeObject(ui.welcome);
        deleteClasses(); closeFonts(); memcp("end"); memDone(); return 0;
    }
    ui.insp = buildInspector();
    if (memcp("insp-built")) {
        MUI_DisposeObject(ui.insp); memcp("insp-disposed"); MUI_DisposeObject(ui.fs); MUI_DisposeObject(ui.dos);
        MUI_DisposeObject(ui.welcome); deleteClasses(); closeFonts(); memcp("end"); memDone(); return 0;
    }
    ui.imp = buildImport();
    ui.app = ApplicationObject,
        MUIA_Application_Title, (IPTR)"BoxerUI",
        MUIA_Application_Base, (IPTR)"BOXERUI",
        MUIA_Application_Version, (IPTR)"$VER: BoxerUI 0.2 (1.10.2026) UI and core slice",
        MUIA_Application_Description, (IPTR)"Boxer for AROS (prototype)",
        SubWindow, (IPTR)ui.welcome, SubWindow, (IPTR)ui.dos,
        SubWindow, (IPTR)ui.fs, SubWindow, (IPTR)ui.insp, SubWindow, (IPTR)ui.imp,
        End;
    if (!ui.app || !ui.welcome || !ui.dos || !ui.fs || !ui.insp || !ui.imp) {
        logf("FAIL: object creation app=%p welcome=%p dos=%p fs=%p insp=%p", ui.app,
             ui.welcome, ui.dos, ui.fs, ui.insp);
        return 20;
    }
    if (memcp("app")) {
        MUI_DisposeObject(ui.app); memcp("app-disposed");
        deleteClasses(); closeFonts(); memcp("end"); memDone(); return 0;
    }
    g_renderClick = renderClicked;
    g_renderEvents = {sessionKey, sessionMouseMove, sessionButton, sessionCommandKey};
    g_onFullscreenChanged = fullscreenChanged;
    g_tick = dumpLaterTick;

    // Welcome: Esc = Close (key equivalent 0x1B); key-view loop
    // Close -> Browse -> Import -> Prompt -> Open recent -> Close; the
    // initial first responder is Open recent.
    notifyId(ui.welcome, MUIA_Window_CloseRequest, TRUE, ID_WELCOME_CLOSE);
    DoMethod(ui.welcome, MUIM_Notify, MUIA_Window_InputEvent, (IPTR)"esc", (IPTR)ui.app, 2,
             MUIM_Application_ReturnID, ID_WELCOME_CLOSE);
    notifyId(ui.closeBtn, MUIA_Pressed, FALSE, ID_WELCOME_CLOSE);
    notifyId(ui.wb[2], MUIA_Pressed, FALSE, ID_PROMPT);
    // A control is either working or ghosted, never a stub that only logs.
    // Open recent has no history behind it yet.
    set(ui.recentBtn, MUIA_Disabled, TRUE);
    // Browse and Import work from the Welcome window once a games folder is known.
    if (g_gamesFolder.empty()) {
        set(ui.wb[0], MUIA_Disabled, TRUE);
        set(ui.wb[1], MUIA_Disabled, TRUE);
    } else {
        notifyId(ui.wb[0], MUIA_Pressed, FALSE, ID_BROWSE);
        notifyId(ui.wb[1], MUIA_Pressed, FALSE, ID_IMPORT);
    }
    notifyId(ui.impChoose, MUIA_Pressed, FALSE, ID_IMPORT_CHOOSE);
    notifyId(ui.impBack, MUIA_Pressed, FALSE, ID_IMPORT_BACK);
    notifyId(ui.imp, MUIA_Window_CloseRequest, TRUE, ID_IMPORT_CLOSE);
    notifyId(ui.impSkip, MUIA_Pressed, FALSE, ID_IMPORT_SKIP);
    notifyId(ui.impLaunch, MUIA_Pressed, FALSE, ID_IMPORT_LAUNCH);
    notifyId(ui.impUse, MUIA_Pressed, FALSE, ID_IMPORT_USE);
    notifyId(ui.impDoneClose, MUIA_Pressed, FALSE, ID_IMPORT_DONE_CLOSE);
    notifyId(ui.impLaunchGame, MUIA_Pressed, FALSE, ID_IMPORT_LAUNCH_GAME);
    notifyId(ui.impBack2, MUIA_Pressed, FALSE, ID_IMPORT_BACK2);
    notifyId(ui.impCreate, MUIA_Pressed, FALSE, ID_IMPORT_CREATE);
    notifyId(ui.impStop, MUIA_Pressed, FALSE, ID_IMPORT_STOP);
    notifyId(ui.impName, MUIA_String_Acknowledge, MUIV_EveryTime, ID_IMPORT_NAME);
    notifyId(ui.impCover, MUIA_Cycle_Active, MUIV_EveryTime, ID_IMPORT_COVER);
    notifyId(ui.impWell, MUIA_Pressed, FALSE, ID_IMPORT_WELL);
    notifyId(ui.inspName, MUIA_String_Acknowledge, MUIV_EveryTime, ID_INSP_NAME);
    notifyId(ui.inspCover, MUIA_Cycle_Active, MUIV_EveryTime, ID_INSP_COVER);
    notifyId(ui.inspWell, MUIA_Pressed, FALSE, ID_INSP_WELL);

    notifyId(ui.dos, MUIA_Window_CloseRequest, TRUE, ID_QUIT);
    notifyId(ui.programs, MUIA_Selected, MUIV_EveryTime, ID_PROGRAMS);
    notifyId(ui.lpSearch, MUIA_String_Contents, MUIV_EveryTime, ID_LP_FILTER);
    notifyId(ui.lpSearch, MUIA_String_Acknowledge, MUIV_EveryTime, ID_LP_ENTER);
    g_launcherAction = lpAction;
    set(ui.programs, MUIA_Disabled, TRUE);   // until a gamebox session allows the panel
    set(ui.manuals, MUIA_Disabled, TRUE);   // no documentation panel yet
    for (int i = 0; i < 3; ++i) notifyId(ui.seg[i], MUIA_Selected, TRUE, ID_SEG0 + i);
    notifyId(ui.volMin, MUIA_Pressed, FALSE, ID_VOL_MIN);
    notifyId(ui.volMax, MUIA_Pressed, FALSE, ID_VOL_MAX);
    notifyId(ui.volSlider, MUIA_Numeric_Value, MUIV_EveryTime, ID_VOLUME);
    notifyId(ui.inspBtn, MUIA_Pressed, FALSE, ID_INSPECTOR);
    notifyId(ui.fsBtn, MUIA_Pressed, FALSE, ID_FULLSCREEN);
    notifyId(ui.lockBtn, MUIA_Selected, MUIV_EveryTime, ID_LOCK_BUTTON);
    // Product keys that ParseIX matches reliably; the others (P, G, I,
    // Shift+A, Alt+F) are taken in the render view's RAWKEY handler
    // (sessionCommandKey), which sees the exact qualifiers.
    const struct { Object *w; const char *key; ULONG id; } keys[] = {
        {ui.dos, "ramiga l", ID_LOCK_KEY}, {ui.dos, "ramiga f", ID_FULLSCREEN},
        {ui.dos, "ramiga q", ID_QUIT}, {ui.fs, "ramiga f", ID_FULLSCREEN},
        {ui.fs, "ramiga l", ID_LOCK_KEY}, {ui.fs, "ramiga q", ID_QUIT},
        {ui.welcome, "ramiga q", ID_QUIT}, {ui.insp, "ramiga q", ID_QUIT}, {ui.imp, "ramiga q", ID_QUIT},
        {ui.dos, "ramiga i", ID_INSPECTOR}, {ui.welcome, "ramiga i", ID_INSPECTOR},
        {ui.insp, "ramiga i", ID_INSPECTOR},
    };
    for (auto &k : keys)
        DoMethod(k.w, MUIM_Notify, MUIA_Window_InputEvent, (IPTR)k.key, (IPTR)ui.app, 2,
                 MUIM_Application_ReturnID, k.id);
    const struct { Object *w; const char *key; ULONG id; } harnessKeys[] = {
        {ui.dos, "f1", ID_MOUSE_ACTIVE}, {ui.dos, "f2", ID_TRACK}, {ui.dos, "f3", ID_DUMP},
        {ui.dos, "f5", ID_DUMP_LATER}, {ui.insp, "f5", ID_DUMP_LATER},
        {ui.dos, "ramiga d", ID_DUMP}, {ui.fs, "ramiga d", ID_DUMP}, {ui.dos, "ramiga s", ID_STYLE},
        // Letters: "ramiga 1".."3" never matched on ABIv11 (no notification fired).
        {ui.dos, "ramiga h", ID_SIZE1}, {ui.dos, "ramiga j", ID_SIZE2}, {ui.dos, "ramiga k", ID_SIZE3},
        {ui.fs, "f3", ID_DUMP}, {ui.welcome, "f3", ID_DUMP}, {ui.insp, "f3", ID_DUMP},
        {ui.insp, "f4", ID_EMULATING},
    };
    if (g_args.harness)
        for (auto &k : harnessKeys)
            DoMethod(k.w, MUIM_Notify, MUIA_Window_InputEvent, (IPTR)k.key, (IPTR)ui.app, 2,
                     MUIM_Application_ReturnID, k.id);
    logf("keys: harness %s", g_args.harness ? "ON (HARNESS=1)" : "off");

    notifyId(ui.insp, MUIA_Window_CloseRequest, TRUE, ID_INSPECTOR_CLOSE);
    for (int i = 0; i < 5; ++i) notifyId(ui.tabs[i], MUIA_Selected, TRUE, ID_TAB0 + i);
    notifyId(ui.speedSlider, MUIA_Numeric_Value, MUIV_EveryTime, ID_SPEED);
    notifyId(ui.frameSlider, MUIA_Numeric_Value, MUIV_EveryTime, ID_FRAMES);
    // Not passed to the core and no help panel: ghosted.
    // Mouse, Joystick and Drives have only placeholder pages.
    set(ui.dynamic, MUIA_Disabled, TRUE);
    set(ui.helpBtn, MUIA_Disabled, TRUE);
    for (int i = 2; i < 5; ++i) set(ui.tabs[i], MUIA_Disabled, TRUE);
    // Gamebox: name and cover of the session's gamebox; ghosted without one.
    inspectorGameboxSync();

    g_wins[0] = ui.welcome; g_wins[1] = ui.dos; g_wins[2] = ui.insp; g_wins[3] = ui.fs;
    for (Object *w : g_wins) notifyId(w, MUIA_Window_Activate, MUIV_EveryTime, ID_ACTIVATION);

    // Initial states as in the reference diagrams.
    nnset(ui.seg[1], MUIA_Selected, TRUE);
    nnset(ui.tabs[1], MUIA_Selected, TRUE);
    set(ui.pages, MUIA_Group_ActivePage, 1);
    if (g_args.volume >= 0) nnset(ui.volSlider, MUIA_Numeric_Value, g_args.volume);
    updateSpeedText();
    updateTitle();

    bool openWelcome = !std::strcmp(screen, "welcome") || !std::strcmp(screen, "all");
    bool openDos = !std::strcmp(screen, "dos") || !std::strcmp(screen, "inspector") || !std::strcmp(screen, "all");
    bool openInsp = !std::strcmp(screen, "inspector") || !std::strcmp(screen, "all");
    if (openDos) set(ui.dos, MUIA_Window_Open, TRUE);
    if (openInsp) set(ui.insp, MUIA_Window_Open, TRUE);
    if (openWelcome) {
        set(ui.welcome, MUIA_Window_Open, TRUE);
        // The original's first responder is Open recent; while it is
        // ghosted AppKit would fall through to the next key view, Close.
        set(ui.welcome, MUIA_Window_ActiveObject, (IPTR)ui.closeBtn);
    }
    updateStatus();
    dumpGeometry("start");

    ULONG sigs = 0;
    bool running = !memcp("windows-open");
    bool sessionWanted = !g_args.gamebox.empty() || g_args.kind == "dos";
    while (running) {
        ULONG id = DoMethod(ui.app, MUIM_Application_NewInput, (IPTR)&sigs);
        if (sigs) g_sigMask |= sigs;
        running = handleId(id);
        if (!running) break;
        if (g_args.kind == "dos-pending") { g_args.kind = "dos"; sessionWanted = true; }
        if (sessionWanted && g_sigMask && !g_ss.ran) {
            sessionWanted = false;
            runSession();
            if (g_ss.quitApp) break;
            sigs = 0;
        }
        if (g_inst.pending && g_sigMask && !g_ss.ran) {
            set(ui.imp, MUIA_Window_Open, FALSE);
            if (!runSession()) {
                logf("import: installer session did not start");
                g_inst.stop = true;
            }
            importAfterInstaller();
            if (g_ss.quitApp) break;
            sigs = 0;
        }
        if (g_copy && g_import.stage == boxer::ImportStage::ImportingSourceFiles) {
            std::string err;
            if (!g_copy->step(256 * 1024, &err)) {
                importAbandon(err);
            } else {
                const uint64_t total = g_copy->totalBytes();
                set(ui.impGauge, MUIA_Gauge_Current, total ? (LONG)(g_copy->copiedBytes() * 1000 / total) : 1000);
                if (g_copy->finished()) {
                    logf("import: copied %llu bytes", (unsigned long long)g_copy->copiedBytes());
                    delete g_copy;
                    g_copy = nullptr;
                    g_import.stage = boxer::ImportStage::ChoosingStartupProgram;
                    showImportStage();
                }
            }
            if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) break;
        } else if (sigs) {
            sigs = Wait(sigs | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F);
            if (sigs & SIGBREAKF_CTRL_C) break;
        }
        if (g_renderResized) {
            g_renderResized = false;
            if (windowOpen(ui.dos)) correctAspect();
        }
        IPTR w1 = 0, w2 = 0, w3 = 0, w4 = 0;
        get(ui.welcome, MUIA_Window_Open, &w1); get(ui.dos, MUIA_Window_Open, &w2);
        get(ui.insp, MUIA_Window_Open, &w3); get(ui.fs, MUIA_Window_Open, &w4);
        IPTR w5 = 0; get(ui.imp, MUIA_Window_Open, &w5);
        if (!w1 && !w2 && !w3 && !w4 && !w5 && !g_inst.pending) running = false;
    }
    // An Inspector rename waits for the session's end (its drives were
    // mounted from the old path); the gamebox and its icon are renamed now.
    if (!g_pendingRename.empty() && !g_args.gamebox.empty()) {
        const std::string now = renameGamebox(g_args.gamebox, g_pendingRename, nullptr);
        logf("rename (inspector, at session end): now \"%s\"", now.c_str());
        g_pendingRename.clear();
    }
    // IS:1579 _cleanup: an import that did not finish takes its gamebox
    // with it, and only that directory.
    if (g_copy) { g_copy->close(); delete g_copy; g_copy = nullptr; }
    if (!g_import.createdGamebox.empty() && g_import.stage != boxer::ImportStage::Finished) {
        std::string err;
        bool ok = g_import.discardGamebox(&err);
        logf("import: unfinished at exit; gamebox %s%s", ok ? "deleted" : "NOT deleted: ", err.c_str());
    }
    // An old icon or settings file that a reader (Wanderer) still held when
    // it was replaced goes now; one still held is named, so it can be found.
    for (const auto &p : boxer::fsutil::retryPendingCleanup())
        logf("cleanup: old version \"%s\" still in use, NOT deleted", p.c_str());
    logf("exit");
    dumpGeometry("exit");
    if (g_laterArmed) DoMethod(ui.app, MUIM_Application_RemInputHandler, (IPTR)&g_laterIhn);
    memcp("loop-left");
    MUI_DisposeObject(ui.app);
    memcp("app-disposed");
    deleteClasses();
    memcp("classes-deleted");
    closeFonts();
    memcp("fonts-closed");
    closeInput();
    if (g_blankPointer) FreeVec(g_blankPointer);
    if (g_log) std::fclose(g_log);
    memcp("end");
    memDone();
    return 0;
}
