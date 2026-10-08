// SPDX-License-Identifier: GPL-2.0-only
// Derived from Boxer (Boxer 2.0-alpha UI views),
// https://github.com/alunbestor/Boxer at commit 0062fc18:
//   Copyright (c) 2013 Alun Bestor and contributors. All rights reserved.
//   This source file is released under the GNU General Public License 2.0.
// Modified by Tomasz Staniak, 2026: rewritten in C++ for AROS.
// Distributed under GPL-2.0 only, as the original; see COPYING.

#include "classes.h"
#include "gfx.h"
#include "ui_logic.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/diskfont.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <cybergraphx/cybergraphics.h>
#include <devices/inputevent.h>
#include <graphics/rpattr.h>
#include <intuition/intuition.h>
#include <diskfont/diskfonttag.h>
#include <diskfont/glyph.h>
#include <diskfont/oterrors.h>
#include <proto/bullet.h>
#include <proto/dos.h>
#include <aros/macros.h>
#include <graphics/gfxbase.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <new>
#include <vector>

namespace boxer_ui {

Fonts g_fonts;
void (*g_renderClick)(bool) = nullptr;
volatile bool g_renderResized = false;
void (*g_tick)() = nullptr;
struct Task *g_mainTask = nullptr;

// ---------------------------------------------------------------- fonts ---
// Proposal: Bitstream Vera Sans stands in for Lucida Grande / Helvetica Neue
// (a humanist sans with similar x-height, shipped as an outline font so any
// point size opens). arial.font is the fallback because every AROS install
// has it as a bitmap font.
//
// Fonts. diskfont
// makes an outline font's bitmap exactly tf_YSize rows high and cuts every
// glyph to it (diskfont/bullet.c OTAG_MakeCharData); the baseline goes at
// the tallest glyph of 0..255, so tops are never cut, only rows below the
// baseline. The system Vera Sans description scales the OS/2 typo range
// (metric 3) on mainline, which Vera's ink exceeds; installed ABIv11
// systems may carry metric 0 instead, so the same request gives different
// glyph sizes per installation. BoxerUI therefore uses its own descriptions
// of the same TrueType files (PROGDIR:Fonts/BoxerSans*.font, metric source
// = global bounding box, tools/make-fonts.py) as the primary family on every
// ABI, at YSize (pt*2384+1024)/2048, the bounding-box height of a pt-pixel
// em: one family for all slots, decided once before any window opens.
// Fallbacks, each logged with its reason: system Vera Sans -> arial.font ->
// topaz.font/8. A font failure never stops the program.
// The check measures what is drawn: per glyph, the engine's glyph map
// against the font's anti-aliased data (plain data when not anti-aliased),
// over 0x21..0x7E and 0xA1..0xFF.
static const char *probeSet(int *len) {
    static char s[96 + 95];
    static int n;
    if (!n) {
        for (int c = 0x21; c <= 0x7E; ++c) s[n++] = (char)c;
        for (int c = 0xA1; c <= 0xFF; ++c) s[n++] = (char)c;
    }
    *len = n;
    return s;
}

// Rows of ink of s drawn with f, relative to the cell top (0..tf_YSize-1).
// Returns false when nothing was drawn. Uses a 1-plane bitmap: the plain
// (non-anti-aliased) glyph data is clipped exactly like the AA data.
bool inkRows(struct TextFont *f, const char *s, int len, int *top, int *bottom, int *left, int *right) {
    struct RastPort rp;
    InitRastPort(&rp);
    SetFont(&rp, f);
    struct TextExtent te;
    TextExtent(&rp, (STRPTR)s, len, &te);
    int pad = 8;
    int w = (te.te_Extent.MaxX - te.te_Extent.MinX + 1) + 2 * pad, h = f->tf_YSize;
    if (w <= 0 || h <= 0) return false;
    struct BitMap *bm = AllocBitMap(w, h, 1, BMF_CLEAR, NULL);
    if (!bm) return false;
    rp.BitMap = bm;
    SetAPen(&rp, 1);
    SetDrMd(&rp, JAM1);
    int x0 = pad - te.te_Extent.MinX;
    Move(&rp, x0, f->tf_Baseline);
    Text(&rp, (STRPTR)s, len);
    int t = h, bt = -1, l = w, r = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (ReadPixel(&rp, x, y) > 0) {
                t = std::min(t, y); bt = std::max(bt, y);
                l = std::min(l, x - x0); r = std::max(r, x - x0);
            }
    FreeBitMap(bm);
    if (bt < 0) return false;
    *top = t; *bottom = bt; *left = l; *right = r;
    return true;
}

struct Library *BulletBase;
#define BX_OT_Spec4_Metric (OT_Level1 | 0x104)   // freetype2 engine private tag

// An .otag as diskfont reads it (bullet.c OTAG_GetFile): big-endian ULONG
// pairs, OT_Indirect data as offsets into the file.
struct OTag { std::vector<ULONG> raw; std::vector<struct TagItem> tags; };
static bool loadOTag(const char *path, OTag &o) {
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) return false;
    Seek(fh, 0, OFFSET_END);
    LONG len = Seek(fh, 0, OFFSET_BEGINNING);
    bool ok = len > 8;
    if (ok) {
        o.raw.assign(len / 4 + 2, 0);
        ok = Read(fh, o.raw.data(), len) == len && AROS_BE2LONG(o.raw[0]) == OT_FileIdent;
    }
    Close(fh);
    if (!ok) return false;
    for (size_t i = 0; i + 1 < o.raw.size(); i += 2) {
        struct TagItem t;
        t.ti_Tag = AROS_BE2LONG(o.raw[i]);
        t.ti_Data = t.ti_Tag == TAG_DONE ? 0 : AROS_BE2LONG(o.raw[i + 1]);
        if (t.ti_Tag != TAG_DONE && (t.ti_Tag & OT_Indirect)) t.ti_Data = (IPTR)o.raw.data() + t.ti_Data;
        o.tags.push_back(t);
        if (t.ti_Tag == TAG_DONE) return true;
    }
    return false;
}

static void glyphRows(struct TextFont *tf, int idx, bool aa, int *top, int *bot) {
    ULONG loc = ((ULONG *)tf->tf_CharLoc)[idx];
    int x0 = loc >> 16, w = loc & 0xFFFF;
    *top = -1; *bot = -1;
    for (int y = 0; y < tf->tf_YSize; ++y) {
        bool ink = false;
        if (aa) {
            const UBYTE *row = (const UBYTE *)((struct ColorTextFont *)tf)->ctf_CharData[0] + y * tf->tf_Modulo * 8;
            for (int x = x0; x < x0 + w && !ink; ++x) ink = row[x];
        } else {
            const UBYTE *row = (const UBYTE *)tf->tf_CharData + y * tf->tf_Modulo;
            for (int x = x0; x < x0 + w && !ink; ++x) ink = row[x >> 3] & (0x80 >> (x & 7));
        }
        if (ink) { if (*top < 0) *top = y; *bot = y; }
    }
}

// Per-glyph check of an opened outline font against its engine. Returns
// the number of cut glyphs (-1 when it could not be measured).
static int checkGlyphs(struct TextFont *tf, const char *otagPath, int ysize, const char *what, LogFn log) {
    OTag o;
    if (!loadOTag(otagPath, o)) { if (log) log("font check %s: %s not readable, not measured", what, otagPath); return -1; }
    // The engine library is named by the description (OT_Engine, e.g.
    // "freetype2" -> freetype2.library), as diskfont opens it.
    const char *engine = "freetype2";
    for (const auto &t : o.tags) if (t.ti_Tag == OT_Engine) engine = (const char *)t.ti_Data;
    std::string lib = std::string(engine) + ".library";
    BulletBase = OpenLibrary((CONST_STRPTR)lib.c_str(), 0);
    if (!BulletBase) { if (log) log("font check %s: %s not opened, not measured", what, lib.c_str()); return -1; }
    int cut = 0, moved = 0, total = 0;
    std::string cutList;
    struct GlyphEngine *ge = OpenEngine();
    struct TagItem main[] = {{OT_OTagList, (IPTR)o.tags.data()}, {OT_OTagPath, (IPTR)"BoxerUI"}, {TAG_DONE, 0}};
    struct TagItem size[] = {{OT_PointHeight, (IPTR)ysize << 16}, {OT_DeviceDPI, (72 << 16) | 72},
                             {OT_DotSize, (100 << 16) | 100}, {TAG_DONE, 0}};
    if (!ge || SetInfoA(ge, main) != OTERR_Success || SetInfoA(ge, size) != OTERR_Success) {
        if (log) log("font check %s: engine setup failed, not measured", what);
        if (ge) CloseEngine(ge);
        CloseLibrary(BulletBase); BulletBase = nullptr;
        return -1;
    }
    bool aa = (tf->tf_Style & FSF_COLORFONT) && (((struct ColorTextFont *)tf)->ctf_Flags & CT_ANTIALIAS);
    int n;
    const char *set = probeSet(&n);
    for (int i = 0; i < n; ++i) {
        int c = (unsigned char)set[i];
        if (c < tf->tf_LoChar || c > tf->tf_HiChar) continue;
        struct TagItem st[] = {{OT_GlyphCode, (IPTR)c}, {TAG_DONE, 0}};
        struct GlyphMap *gm = nullptr;
        struct TagItem ot[] = {{OT_GlyphMap, (IPTR)&gm}, {TAG_DONE, 0}};
        SetInfoA(ge, st);
        if (ObtainInfoA(ge, ot) != OTERR_Success || !gm) continue;
        int h = gm->glm_BlackHeight, y0 = gm->glm_Y0;
        struct TagItem rt[] = {{OT_GlyphMap, (IPTR)gm}, {TAG_DONE, 0}};
        ReleaseInfoA(ge, rt);
        if (h == 0) continue;
        ++total;
        int et = tf->tf_Baseline + 1 - y0, eb = et + h - 1, t, b;
        glyphRows(tf, c - tf->tf_LoChar, aa, &t, &b);
        if (t < 0 || b - t < eb - et) {
            ++cut;
            char code[8];
            std::snprintf(code, sizeof code, " %02X", c);
            cutList += code;
        } else if (t != et || b != eb) {
            ++moved;
        }
    }
    CloseEngine(ge);
    CloseLibrary(BulletBase);
    BulletBase = nullptr;
    if (log)
        log("font check %s: %d glyphs (%s data), %d cut, %d moved%s%s", what, total, aa ? "anti-aliased" : "plain",
            cut, moved, cut ? ", cut codes:" : "", cutList.c_str());
    return cut;
}

// The system Vera Sans description's metric source (0 = bounding box).
static int otagMetric(const char *path) {
    OTag o;
    if (!loadOTag(path, o)) return -1;
    for (const auto &t : o.tags) if (t.ti_Tag == BX_OT_Spec4_Metric) return (int)t.ti_Data;
    return -1;
}

static const int kSlotPt[kFontSlots] = {13, 13, 11, 9, 32, 16};
static struct TextFont **slotFont(int i) {
    struct TextFont **all[] = {&g_fonts.system13, &g_fonts.bold13, &g_fonts.small11, &g_fonts.mini9, &g_fonts.title32,
                               &g_fonts.bold16};
    return all[i];
}
static void closeSlots() { for (int i = 0; i < kFontSlots; ++i) if (*slotFont(i)) { CloseFont(*slotFont(i)); *slotFont(i) = nullptr; } }

// One family for all five slots: every slot opens or the family is dropped.
static bool g_fontCheck;
static bool openFamily(const char *regular, const char *bold, bool bboxScale, int style, const char *otagR,
                       const char *otagB, LogFn log) {
    for (int i = 0; i < kFontSlots; ++i) {
        int pt = kSlotPt[i], ysize = bboxScale ? (pt * 2384 + 1024) / 2048 : pt;
        const bool isBold = i == 1 || i == 5;
        const char *name = isBold ? bold : regular;
        struct TextAttr ta = {(STRPTR)name, (UWORD)ysize, (UBYTE)(isBold ? style : 0), 0};
        struct TextFont *f = OpenDiskFont(&ta);
        if (!f) {
            if (log) log("font family %s: slot %d %s/%d did not open (missing file or its TrueType outline)", regular, i, name, ysize);
            closeSlots();
            return false;
        }
        *slotFont(i) = f;
        std::snprintf(g_fonts.names[i], sizeof g_fonts.names[i], "%s/%d (for %d pt)", name, ysize, pt);
        if (log) log("font slot %d: %s -> ysize %d baseline %d", i, g_fonts.names[i], (int)f->tf_YSize, (int)f->tf_Baseline);
        const char *otag = isBold ? otagB : otagR;
        if (otag && g_fontCheck) checkGlyphs(f, otag, ysize, g_fonts.names[i], log);
    }
    return true;
}

// The glyph check is opt-in (FONTCHECK=1): freetype2.library's CloseEngine
// ends with FT_Done_Library, not FT_Done_FreeType, so the memory pool behind
// every engine stays allocated. One engine per slot lost 61,440 B per start
// on both ABIs, never returned by a flush (measured 2026-10-02).
bool openFonts(LogFn log, bool fontCheck) {
    g_fontCheck = fontCheck;
    // Own descriptions first, on both ABIs.
    if (openFamily("PROGDIR:Fonts/BoxerSans.font", "PROGDIR:Fonts/BoxerSansBold.font", true, 0,
                   "PROGDIR:Fonts/BoxerSans.otag", "PROGDIR:Fonts/BoxerSansBold.otag", log)) {
        if (log) log("fonts: BoxerSans (own descriptions, bounding-box metric)");
        return true;
    }
    // Fallback: the system Vera Sans; at the bounding-box size when its metric is
    // 0 (same em as BoxerSans), else at the requested size (cut glyphs logged).
    int metric = otagMetric("FONTS:Vera Sans.otag");
    if (openFamily("Vera Sans.font", "Vera Sans Bold.font", metric == 0, 0, "FONTS:Vera Sans.otag",
                   "FONTS:Vera Sans Bold.otag", log)) {
        if (log) log("fonts: system Vera Sans (metric %d)", metric);
        return true;
    }
    if (openFamily("arial.font", "arial.font", false, FSF_BOLD, nullptr, nullptr, log)) {
        if (log) log("fonts: arial.font (bitmap fallback)");
        return true;
    }
    struct TextAttr topaz = {(STRPTR)"topaz.font", 8, 0, 0};
    for (int i = 0; i < kFontSlots; ++i) {
        *slotFont(i) = OpenFont(&topaz);
        std::snprintf(g_fonts.names[i], sizeof g_fonts.names[i], "topaz.font/8 (last resort)");
    }
    if (log) log("fonts: topaz.font/8 (last resort)");
    return true;
}

void closeFonts() { closeSlots(); }

// ------------------------------------------------------------- helpers ---
bool windowActive(Object *obj) {
    struct Window *w = _window(obj);
    return w && (w->Flags & WFLG_WINDOWACTIVE);
}

// Text audit: logs, once per distinct result, where a string's ink lies
// relative to the object that draws it, so truncation and clipping can be
// read per ABI from the run log (the same font files have different
// metrics on ABIv11 and mainline).
static LogFn g_auditLog;
void setAuditLog(LogFn log) { g_auditLog = log; }

void auditTextBox(const char *kind, struct TextFont *font, const char *s, int len,
                  int x, int baseline, int boxL, int boxT, int boxW, int boxH) {
    if (!g_auditLog || len <= 0) return;
    // Horizontal: TextExtent includes bearings and over-extent. Vertical:
    // TextExtent reports only the line box, so the ink rows are measured by
    // drawing the string off-screen (inkRows). Ink on the first or last row
    // of the font cell means the font itself may have cut glyphs there.
    struct RastPort rp;
    InitRastPort(&rp);
    SetFont(&rp, font);
    struct TextExtent te;
    TextExtent(&rp, (STRPTR)s, len, &te);
    // Drawing a string off-screen costs a bitmap and a ReadPixel per pixel,
    // far too much for every redraw of an animated button, so the result is
    // kept per font and string.
    struct InkCache { struct TextFont *f; std::string s; bool any; int t, b, l, r; };
    static std::vector<InkCache> inkCache;
    int it = 0, ib = -1, il = 0, ir = -1;
    bool any = false, found = false;
    for (const InkCache &c : inkCache)
        if (c.f == font && c.s.size() == (size_t)len && !c.s.compare(0, len, s, len)) {
            any = c.any; it = c.t; ib = c.b; il = c.l; ir = c.r; found = true;
            break;
        }
    if (!found) {
        any = inkRows(font, s, len, &it, &ib, &il, &ir);
        inkCache.push_back({font, std::string(s, len), any, it, ib, il, ir});
    }
    int cellTop = baseline - font->tf_Baseline;
    int inkL = x + std::min<int>(te.te_Extent.MinX, any ? il : 0);
    int inkR = x + std::max<int>(te.te_Extent.MaxX, any ? ir : 0);
    int inkT = any ? cellTop + it : baseline, inkB = any ? cellTop + ib : baseline;
    bool cutL = inkL < boxL, cutR = inkR > boxL + boxW - 1;
    bool cutT = inkT < boxT, cutB = inkB > boxT + boxH - 1;
    bool cellEdge = any && (it == 0 || ib == font->tf_YSize - 1);
    // Built in pieces: one snprintf with 22 variadic arguments printed
    // garbage from the 21st on (ABIv11, 2026-10-01).
    std::string verdict;
    if (cutL) verdict += "CLIP-LEFT ";
    if (cutR) verdict += "TRUNCATED-RIGHT ";
    if (cutT) verdict += "CLIP-TOP ";
    if (cutB) verdict += "CLIP-BOTTOM ";
    if (cellEdge) verdict += "CELL-EDGE";
    if (verdict.empty()) verdict = "fits";
    char a[160], b[200], line[400];
    std::snprintf(a, sizeof a, "text: %s \"%.*s\"", kind, len > 60 ? 60 : len, s);
    std::snprintf(b, sizeof b, " font %d/%d box %dx%d width %d ink x%+d..%+d y%+d..%+d",
                  (int)font->tf_YSize, (int)font->tf_Baseline, boxW, boxH, (int)te.te_Width,
                  inkL - boxL, inkR - boxL, inkT - boxT, inkB - boxT);
    std::snprintf(line, sizeof line, "%s%s (cell rows %d..%d of %d) baseline %+d -> %s", a, b, it, ib,
                  (int)font->tf_YSize, baseline - boxT, verdict.c_str());
    static std::vector<std::string> seen;
    for (const std::string &l : seen) if (l == line) return;
    seen.push_back(line);
    g_auditLog("%s", line);
}

// Text in an exact RGB colour. RPTAG_PenMode FALSE switches the RastPort to
// direct colours; it must be switched back, or MUI's later SetAPen calls
// keep drawing in our colour.
static void textRGB(Object *obj, struct TextFont *font, int x, int baseline,
                    const char *s, int len, unsigned rgb, const char *kind = "label") {
    struct RastPort *rp = _rp(obj);
    auditTextBox(kind, font, s, len, x, baseline, _left(obj), _top(obj), _width(obj), _height(obj));
    struct TextFont *old = rp->Font;
    SetFont(rp, font);
    SetDrMd(rp, JAM1);
    SetRPAttrs(rp, RPTAG_PenMode, FALSE, RPTAG_FgColor, 0xFF000000 | rgb, TAG_DONE);
    Move(rp, x, baseline);
    Text(rp, (STRPTR)s, len);
    SetRPAttrs(rp, RPTAG_PenMode, TRUE, TAG_DONE);
    SetFont(rp, old);
}

// Vertical placement rule for every text drawn by these classes: the
// font's line box (tf_YSize) is centred in the xib frame and the baseline
// is tf_Baseline below the line box top. No per-ABI offsets.
// Ink placement: the font's ink (rows above and below the baseline,
// measured once per font by drawing 0x21..0x7E and 0xA1..0xFF) is
// centred in the frame, as AppKit centres ascender..descender. The cell
// (tf_YSize) is not used: it may be taller than the ink (BoxerSans) or end
// right at it.
struct InkMetrics { int asc, desc; };
static InkMetrics fontInk(struct TextFont *f) {
    static struct TextFont *keys[8];
    static InkMetrics vals[8];
    for (int i = 0; i < 8; ++i) if (keys[i] == f) return vals[i];
    InkMetrics m = {f->tf_Baseline, f->tf_YSize - 1 - f->tf_Baseline};
    int t, b, l, r;
    int n;
    const char *set = probeSet(&n);
    if (inkRows(f, set, n, &t, &b, &l, &r)) m = {f->tf_Baseline - t, b - f->tf_Baseline};
    for (int i = 0; i < 8; ++i) if (!keys[i]) { keys[i] = f; vals[i] = m; break; }
    return m;
}
int textInkHeight(struct TextFont *f) { InkMetrics m = fontInk(f); return m.asc + m.desc + 1; }
static int baselineIn(int boxTop, int boxH, struct TextFont *f) {
    InkMetrics m = fontInk(f);
    return boxTop + (boxH - (m.asc + m.desc + 1)) / 2 + m.asc;
}

static int textWidth(struct TextFont *font, const char *s, int len) {
    struct RastPort rp;
    InitRastPort(&rp);
    SetFont(&rp, font);
    return TextLength(&rp, (STRPTR)s, len);
}

static unsigned mix(unsigned a, unsigned b, double t) {   // a*(1-t) + b*t
    unsigned out = 0;
    for (int s = 16; s >= 0; s -= 8) {
        double v = ((a >> s) & 0xff) * (1 - t) + ((b >> s) & 0xff) * t;
        out |= (unsigned)std::lround(v) << s;
    }
    return out;
}

// Paint the parent's background under the object, then read it back so the
// object can composite anti-aliased art over it exactly like AppKit does.
static void grabBackground(Object *obj, Canvas &cv) {
    int l = _left(obj), t = _top(obj), w = _width(obj), h = _height(obj);
    cv.resize(w, h);
    DoMethod(obj, MUIM_DrawParentBackground, l, t, w, h, l, t, 0);
    ReadPixelArray(cv.rgb.data(), 0, 0, w * 3, _rp(obj), l, t, w, h, RECTFMT_RGB);
}

static void blit(Object *obj, Canvas &cv, int x, int y) {
    WritePixelArray(cv.rgb.data(), 0, 0, cv.w * 3, _rp(obj), x, y, cv.w, cv.h, RECTFMT_RGB);
}

static bool inside(Object *obj, int x, int y) {
    return x >= _left(obj) && x <= _right(obj) && y >= _top(obj) && y <= _bottom(obj);
}

// A disabled AppKit control is drawn dimmed by the control itself.
// MUI_Redraw paints a 67% grey wash over the whole rectangle of a disabled
// object unless MUIA_NestedDisabled is set (mui_redraw.c), and that wash also
// greys the art behind the control. Classes that draw their own disabled
// look therefore turn MUIA_Disabled into Area's standard MUIA_NestedDisabled;
// MUIA_Disabled still reads TRUE, so Zune keeps refusing input and events.
// Returns true when this OM_SET disables the object.
static bool ownDisabledLook(Object *obj, struct opSet *msg) {
    struct TagItem *tag = FindTagItem(MUIA_Disabled, msg->ops_AttrList);
    if (!tag) return false;
    IPTR cur = 0;
    get(obj, MUIA_Disabled, &cur);
    tag->ti_Tag = ((tag->ti_Data != 0) != (cur != 0)) ? MUIA_NestedDisabled : TAG_IGNORE;
    return tag->ti_Tag != TAG_IGNORE && tag->ti_Data;
}

// ========================================================= PaintGroup ====
struct PGData {
    int mode;
    const AbsLayout *layout;
    struct Hook *hook;
};
struct MUI_CustomClass *mccPaintGroup;

static void autoresize(int base, int now, int pos, int size, bool fMin, bool fSize,
                       bool fMax, int &outPos, int &outSize) {
    int delta = now - base;
    double parts[3] = {(double)pos, (double)size, (double)(base - pos - size)};
    bool flex[3] = {fMin, fSize, fMax};
    double sum = 0; int n = 0;
    for (int i = 0; i < 3; ++i) if (flex[i]) { sum += parts[i]; ++n; }
    double d[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i)
        if (flex[i]) d[i] = sum > 0 ? delta * parts[i] / sum : (double)delta / n;
    outPos = pos + (int)std::lround(d[0]);
    outSize = size + (int)std::lround(d[1]);
}

AROS_UFH3S(ULONG, absLayoutFunc,
           AROS_UFHA(struct Hook *, hook, A0),
           AROS_UFHA(Object *, obj, A2),
           AROS_UFHA(struct MUI_LayoutMsg *, lm, A1))
{
    AROS_USERFUNC_INIT
    (void)obj;
    const AbsLayout *L = (const AbsLayout *)hook->h_Data;
    if (lm->lm_Type == MUILM_MINMAX) {
        lm->lm_MinMax.MinWidth = L->minW;  lm->lm_MinMax.MinHeight = L->minH;
        lm->lm_MinMax.DefWidth = L->baseW; lm->lm_MinMax.DefHeight = L->baseH;
        lm->lm_MinMax.MaxWidth = L->maxW;  lm->lm_MinMax.MaxHeight = L->maxH;
        return 0;
    }
    if (lm->lm_Type == MUILM_LAYOUT) {
        Object *cstate = (Object *)lm->lm_Children->mlh_Head, *child;
        int i = 0;
        while ((child = (Object *)NextObject(&cstate)) && i < L->count) {
            const AbsItem &it = L->items[i++];
            int x, w, y, h;
            autoresize(L->baseW, lm->lm_Layout.Width, it.x, it.w, it.mask & FlexMinX,
                       it.mask & FlexW, it.mask & FlexMaxX, x, w);
            autoresize(L->baseH, lm->lm_Layout.Height, it.y, it.h, it.mask & FlexTop,
                       it.mask & FlexH, it.mask & FlexBottom, y, h);
            if (!MUI_Layout(child, x, y, w, h, 0)) return FALSE;
        }
        return TRUE;
    }
    return MUILM_UNKNOWN;
    AROS_USERFUNC_EXIT
}

static unsigned paintColor(int mode, bool active, int x, int y, int W, int H) {
    switch (mode) {
    case PM_Welcome:
        return welcomeGradient(x + 0.5, y + 0.5, W, H);
    case PM_BottomBar: {
        // Stand-in for the system-drawn textured band (contentBorderThickness),
        // which the reference cannot give values for.
        if (y == 0) return active ? 0xAAAAAA : 0xC8C8C8;
        double t = H > 1 ? (double)y / (H - 1) : 0;
        return active ? mix(0xE6E6E6, 0xCFCFCF, t) : mix(0xF2F2F2, 0xE8E8E8, t);
    }
    case PM_Toolbar: {
        if (y == H - 1) return active ? 0xAAAAAA : 0xCDCDCD;
        double t = H > 1 ? (double)y / (H - 1) : 0;
        return active ? mix(0xE8E8E8, 0xD3D3D3, t) : mix(0xF4F4F4, 0xEBEBEB, t);
    }
    case PM_Box:
        // NSBox: fill black a=0.05 and border black a=0.10 over the panel
        // grey. The box frame starts at x=-1 and is 298 wide in a 296 panel,
        // so only its top and bottom edges are visible.
        if (y == 0 || y == H - 1) return mix(0xECECEC, 0x000000, 0.10);
        return mix(0xECECEC, 0x000000, 0.05);
    case PM_Panel:
    default:
        return 0xECECEC;   // Aqua window background (white 0.925), stand-in
    }
}

static IPTR PG_New(struct IClass *cl, Object *obj, struct opSet *msg) {
    const AbsLayout *layout = (const AbsLayout *)GetTagData(PGA_Layout, 0, msg->ops_AttrList);
    struct Hook *hook = nullptr;
    if (layout) {
        hook = (struct Hook *)AllocVec(sizeof(struct Hook), MEMF_CLEAR);
        hook->h_Entry = (APTR)absLayoutFunc;
        hook->h_Data = (APTR)layout;
    }
    obj = (Object *)DoSuperNewTags(cl, obj, NULL,
                                   hook ? MUIA_Group_LayoutHook : TAG_IGNORE, (IPTR)hook,
                                   TAG_MORE, (IPTR)msg->ops_AttrList);
    if (!obj) { FreeVec(hook); return 0; }
    PGData *d = (PGData *)INST_DATA(cl, obj);
    d->mode = (int)GetTagData(PGA_Mode, PM_Panel, msg->ops_AttrList);
    d->layout = layout;
    d->hook = hook;
    return (IPTR)obj;
}

static unsigned lpBg(int x, int y, int W, int H);
static int lpPanelHeight();
static int lpPanelWidth(Object *fallback);

static IPTR PG_DrawBackground(struct IClass *cl, Object *obj, struct MUIP_DrawBackground *msg) {
    PGData *d = (PGData *)INST_DATA(cl, obj);
    if (msg->width <= 0 || msg->height <= 0) return 0;
    if (d->mode == PM_LaunchBar) {
        // The search bar sits at the top of the launch panel: panel
        // coordinates are its own.
        Canvas cv(msg->width, msg->height);
        int W = lpPanelWidth(obj), H = lpPanelHeight();
        for (int y = 0; y < msg->height; ++y)
            for (int x = 0; x < msg->width; ++x)
                cv.set(x, y, lpBg(msg->left - _left(obj) + x, msg->top - _top(obj) + y, W, H));
        WritePixelArray(cv.rgb.data(), 0, 0, cv.w * 3, _rp(obj), msg->left, msg->top,
                        cv.w, cv.h, RECTFMT_RGB);
        return TRUE;
    }
    if (d->mode == PM_VolumeBox) {
        // No fill: the toolbar shows through; only the border is ours.
        DoSuperMethodA(cl, obj, (Msg)msg);
        struct RastPort *rp = _rp(obj);
        unsigned c = windowActive(obj) ? 0x7B7B7B : 0xB0B0B0;  // black a=0.42 over toolbar
        int l = _left(obj), t = _top(obj), r = _right(obj), b = _bottom(obj);
        FillPixelArray(rp, l, t, r - l + 1, 1, c);
        FillPixelArray(rp, l, b, r - l + 1, 1, c);
        FillPixelArray(rp, l, t, 1, b - t + 1, c);
        FillPixelArray(rp, r, t, 1, b - t + 1, c);
        return TRUE;
    }
    bool active = windowActive(obj);
    Canvas cv(msg->width, msg->height);
    int W = _width(obj), H = _height(obj);
    for (int y = 0; y < msg->height; ++y)
        for (int x = 0; x < msg->width; ++x)
            cv.set(x, y, paintColor(d->mode, active, msg->left - _left(obj) + x,
                                    msg->top - _top(obj) + y, W, H));
    WritePixelArray(cv.rgb.data(), 0, 0, cv.w * 3, _rp(obj), msg->left, msg->top,
                    cv.w, cv.h, RECTFMT_RGB);
    return TRUE;
}

BOOPSI_DISPATCHER(IPTR, PG_Dispatcher, cl, obj, msg)
{
    switch (msg->MethodID) {
    case OM_NEW: return PG_New(cl, obj, (struct opSet *)msg);
    case OM_DISPOSE: {
        PGData *d = (PGData *)INST_DATA(cl, obj);
        struct Hook *h = d->hook;
        IPTR r = DoSuperMethodA(cl, obj, msg);
        FreeVec(h);
        return r;
    }
    case MUIM_DrawBackground:
        return PG_DrawBackground(cl, obj, (struct MUIP_DrawBackground *)msg);
    case PGM_Tick:
        if (g_tick) g_tick();
        return 0;
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

// ============================================================== Label ====
struct LabelData {
    char text[256];
    struct TextFont *font;
    unsigned rgb, shadow;
    int shadowDY, align, w, h;
    bool vcenter;
};
static struct MUI_CustomClass *mccLabel;

BOOPSI_DISPATCHER(IPTR, Label_Dispatcher, cl, obj, msg)
{
    switch (msg->MethodID) {
    case OM_SET: {
        struct TagItem *tag = FindTagItem(LA_Text, ((struct opSet *)msg)->ops_AttrList);
        if (tag) {
            LabelData *d = (LabelData *)INST_DATA(cl, obj);
            const char *s = tag->ti_Data ? (const char *)tag->ti_Data : "";
            if (std::strcmp(d->text, s) != 0) {
                std::snprintf(d->text, sizeof d->text, "%s", s);
                MUI_Redraw(obj, MADF_DRAWOBJECT);
            }
        }
        break;
    }
    case MUIM_AskMinMax: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        LabelData *d = (LabelData *)INST_DATA(cl, obj);
        struct MUI_MinMax *mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        mm->MinWidth += d->w; mm->DefWidth += d->w; mm->MaxWidth += d->w;
        mm->MinHeight += d->h; mm->DefHeight += d->h; mm->MaxHeight += d->h;
        return r;
    }
    case MUIM_Draw: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return r;
        LabelData *d = (LabelData *)INST_DATA(cl, obj);
        if (((struct MUIP_Draw *)msg)->flags & MADF_DRAWUPDATE)
            DoMethod(obj, MUIM_DrawParentBackground, _left(obj), _top(obj), _width(obj),
                     _height(obj), _left(obj), _top(obj), 0);
        int lines = 1;
        for (const char *p = d->text; *p; ++p) lines += (*p == '\n');
        // Lines are spread over the xib frame (AppKit sized it for exactly
        // these lines); a font taller than that pitch keeps its own height.
        int ys = textInkHeight(d->font);
        int lh = lines > 1 ? std::max(ys, _mheight(obj) / lines) : ys;
        int b0 = baselineIn(_mtop(obj), _mheight(obj) - lh * (lines - 1), d->font);
        const char *s = d->text;
        for (int i = 0; i < lines; ++i) {
            const char *e = std::strchr(s, '\n');
            int len = e ? (int)(e - s) : (int)std::strlen(s);
            int tw = textWidth(d->font, s, len);
            int x = _mleft(obj);
            if (d->align == 1) x += (_mwidth(obj) - tw) / 2;
            else if (d->align == 2) x += _mwidth(obj) - tw;
            int base = b0 + i * lh;
            if (d->shadow != ~0u)
                textRGB(obj, d->font, x, base + d->shadowDY, s, len, d->shadow, "label-shadow");
            textRGB(obj, d->font, x, base, s, len, d->rgb);
            if (!e) break;
            s = e + 1;
        }
        return r;
    }
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

Object *newLabel(const char *text, struct TextFont *font, unsigned rgb, int align,
                 int w, int h, unsigned shadow, int shadowDY, bool vcenter) {
    Object *o = (Object *)NewObject(mccLabel->mcc_Class, NULL, MUIA_Font, (IPTR)font, TAG_DONE);
    if (!o) return nullptr;
    LabelData *d = (LabelData *)INST_DATA(mccLabel->mcc_Class, o);
    std::snprintf(d->text, sizeof d->text, "%s", text);
    d->font = font; d->rgb = rgb; d->align = align; d->w = w; d->h = h;
    d->shadow = shadow; d->shadowDY = shadowDY; d->vcenter = vcenter;
    return o;
}

void setLabelText(Object *label, const char *text) { set(label, LA_Text, (IPTR)text); }

// ======================================================== GlyphButton ====
struct GBData {
    int glyph, alt, style, w, h;
    char text[48];
    bool pulldown;
    bool hidden, hover;
    struct MUI_EventHandlerNode ehn;
    bool ehnAdded;
};
static struct MUI_CustomClass *mccGlyph;

static void drawBezel(Canvas &cv, int style, bool pressed, bool active, bool hover) {
    int w = cv.w, h = cv.h;
    unsigned border = active ? 0x9A9A9A : 0xC4C4C4;
    unsigned top = pressed ? 0xC6C6C6 : (active ? 0xFDFDFD : 0xF7F7F7);
    unsigned bot = pressed ? 0xBABABA : (active ? 0xEBEBEB : 0xF2F2F2);
    double r = 4.5;
    double x0 = 0, x1 = w;           // the rounded shape, possibly beyond the cell
    if (style == GS_SegLeft) x1 = w + 6;
    if (style == GS_SegMid) { x0 = -6; x1 = w + 6; }
    if (style == GS_SegRight) x0 = -6;
    switch (style) {
    case GS_RoundTextured: case GS_SegLeft: case GS_SegMid: case GS_SegRight:
        paintLayers(cv, 0, 0, w, h, {
            {{rrect(x0, 0.5, x1 - x0, h - 1, r)}, border, border, 1.0},
            {{rrect(x0 + 1, 1.5, x1 - x0 - 2, h - 3, r - 1)}, top, bot, 1.0, 1.5, (double)h - 1.5},
        });
        if (style == GS_SegLeft || style == GS_SegMid)
            for (int y = 1; y < h - 1; ++y) cv.set(w - 1, y, border);
        break;
    case GS_Recessed:
        // Recessed buttons show their bezel only while the mouse is inside.
        if (hover || pressed)
            paintLayers(cv, 0, 0, w, h, {
                {{rrect(0, 0, w, h, 3.5)}, 0x000000, 0x000000, pressed ? 0.28 : 0.18},
                {{rrect(1, 1, w - 2, h - 2, 2.5)}, 0xFFFFFF, 0xFFFFFF, pressed ? 0.05 : 0.25},
            });
        break;
    case GS_Help:
        paintLayers(cv, 0, 0, w, h, {
            {{circle(w / 2.0, h / 2.0, std::min(w, h) / 2.0 - 0.5)}, border, border, 1.0},
            {{circle(w / 2.0, h / 2.0, std::min(w, h) / 2.0 - 1.5)}, top, bot, 1.0, 1.0, (double)h - 1},
        });
        break;
    default:
        break;
    }
}

static IPTR GB_Draw(struct IClass *cl, Object *obj, struct MUIP_Draw *msg) {
    IPTR r = DoSuperMethodA(cl, obj, (Msg)msg);
    if (!(msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return r;
    GBData *d = (GBData *)INST_DATA(cl, obj);
    Canvas cv;
    grabBackground(obj, cv);
    if (!d->hidden) {
        IPTR sel = 0, dis = 0;
        get(obj, MUIA_Selected, &sel);
        get(obj, MUIA_Disabled, &dis);
        bool active = windowActive(obj);
        drawBezel(cv, d->style, sel, active, d->hover);
        // Template images: dark grey normally, lighter when disabled or in an
        // inactive window, darker when the cell is pushed in.
        unsigned tint = dis ? 0xB4B4B4 : !active ? 0x9C9C9C : sel ? 0x262626 : 0x4D4D4D;
        int g = (sel && d->alt >= 0) ? d->alt : d->glyph;
        if (g >= 0) {
            const Mask &m = glyphMask((Glyph)g);
            paintMask(cv, (cv.w - m.w) / 2, (cv.h - m.h) / 2, m, tint, 1.0);
        }
        if (d->pulldown) {   // the pull-down arrow of a textured pop-up button
            Mask m = rasterize(7, 4, {poly({{0, 0}, {7, 0}, {3.5, 4}})});
            paintMask(cv, cv.w - 14, (cv.h - 4) / 2 + 1, m, tint, 1.0);
        }
        blit(obj, cv, _left(obj), _top(obj));
        if (d->text[0]) {
            struct TextFont *f = g_fonts.system13;
            int len = (int)std::strlen(d->text), tw = textWidth(f, d->text, len);
            int tx = d->pulldown ? 10 : (cv.w - tw) / 2;
            unsigned tc = dis ? 0xA0A0A0 : !active ? 0x8C8C8C : 0x1A1A1A;
            textRGB(obj, f, _left(obj) + tx, baselineIn(_top(obj), cv.h, f),
                    d->text, len, tc, "button");
        }
        if (d->style == GS_Help) {
            int tw = textWidth(g_fonts.bold13, "?", 1);
            textRGB(obj, g_fonts.bold13, _left(obj) + (cv.w - tw) / 2,
                    baselineIn(_top(obj), cv.h, g_fonts.bold13),
                    "?", 1, tint, "help-button");
        }
    } else {
        blit(obj, cv, _left(obj), _top(obj));
    }
    return r;
}

BOOPSI_DISPATCHER(IPTR, GB_Dispatcher, cl, obj, msg)
{
    GBData *d;
    switch (msg->MethodID) {
    case OM_SET: {
        ownDisabledLook(obj, (struct opSet *)msg);
        struct TagItem *tag = FindTagItem(GBA_Hidden, ((struct opSet *)msg)->ops_AttrList);
        if (tag) {
            d = (GBData *)INST_DATA(cl, obj);
            bool h = tag->ti_Data != 0;
            if (h != d->hidden) { d->hidden = h; MUI_Redraw(obj, MADF_DRAWOBJECT); }
        }
        break;
    }
    case MUIM_AskMinMax: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        d = (GBData *)INST_DATA(cl, obj);
        struct MUI_MinMax *mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        mm->MinWidth += d->w; mm->DefWidth += d->w; mm->MaxWidth += d->w;
        mm->MinHeight += d->h; mm->DefHeight += d->h; mm->MaxHeight += d->h;
        return r;
    }
    case MUIM_Setup:
        if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
        d = (GBData *)INST_DATA(cl, obj);
        if (d->style == GS_Recessed) {
            d->ehn.ehn_Events = IDCMP_MOUSEMOVE;
            d->ehn.ehn_Priority = 1;
            d->ehn.ehn_Object = obj;
            d->ehn.ehn_Class = cl;
            DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&d->ehn);
            d->ehnAdded = true;
        }
        return TRUE;
    case MUIM_Cleanup:
        d = (GBData *)INST_DATA(cl, obj);
        if (d->ehnAdded) {
            DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&d->ehn);
            d->ehnAdded = false;
        }
        break;
    case MUIM_HandleEvent: {
        struct MUIP_HandleEvent *he = (struct MUIP_HandleEvent *)msg;
        d = (GBData *)INST_DATA(cl, obj);
        if (he->imsg && he->imsg->Class == IDCMP_MOUSEMOVE) {
            bool h = inside(obj, he->imsg->MouseX, he->imsg->MouseY);
            if (h != d->hover) { d->hover = h; MUI_Redraw(obj, MADF_DRAWOBJECT); }
            return 0;   // never eat: other objects track the pointer too
        }
        break;
    }
    case MUIM_Draw:
        return GB_Draw(cl, obj, (struct MUIP_Draw *)msg);
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

Object *newTextButton(const char *text, int style, int w, int h, bool pulldown) {
    Object *o = newGlyphButton(-1, style, w, h, MUIV_InputMode_RelVerify);
    if (!o) return nullptr;
    GBData *d = (GBData *)INST_DATA(mccGlyph->mcc_Class, o);
    std::snprintf(d->text, sizeof d->text, "%s", text);
    d->pulldown = pulldown;
    return o;
}

Object *newGlyphButton(int glyph, int style, int w, int h, ULONG inputMode, int altGlyph) {
    Object *o = (Object *)NewObject(mccGlyph->mcc_Class, NULL,
                                    MUIA_InputMode, inputMode,
                                    MUIA_CycleChain, 1,
                                    MUIA_ShowSelState, TRUE,
                                    TAG_DONE);
    if (!o) return nullptr;
    GBData *d = (GBData *)INST_DATA(mccGlyph->mcc_Class, o);
    d->glyph = glyph; d->alt = altGlyph; d->style = style; d->w = w; d->h = h;
    return o;
}

// ====================================================== WelcomeButton ====
struct WBData {
    int art;
    char title[64];
    Object *view;
    bool hover, focused;
    double illum, target;
    struct MUI_EventHandlerNode ehn;
    struct MUI_InputHandlerNode ihn;
    bool ihnAdded;
};
static struct MUI_CustomClass *mccWelcome;
#define WBM_Tick BX_TAG(34)

static void wbAnimate(Object *obj, WBData *d) {
    if (d->illum == d->target) return;
    if (!d->ihnAdded) {
        d->ihn.ihn_Object = obj;
        d->ihn.ihn_Flags = MUIIHNF_TIMER;
        d->ihn.ihn_Millis = 20;
        d->ihn.ihn_Method = WBM_Tick;
        DoMethod(_app(obj), MUIM_Application_AddInputHandler, (IPTR)&d->ihn);
        d->ihnAdded = true;
    }
}

static void wbStop(Object *obj, WBData *d) {
    if (d->ihnAdded) {
        DoMethod(_app(obj), MUIM_Application_RemInputHandler, (IPTR)&d->ihn);
        d->ihnAdded = false;
    }
}

static void wbSetHover(Object *obj, WBData *d, bool h) {
    if (h == d->hover) return;
    d->hover = h;
    d->target = h ? 1.0 : 0.0;
    wbAnimate(obj, d);
}

static IPTR WB_Draw(struct IClass *cl, Object *obj, struct MUIP_Draw *msg) {
    IPTR r = DoSuperMethodA(cl, obj, (Msg)msg);
    if (!(msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return r;
    WBData *d = (WBData *)INST_DATA(cl, obj);
    int W = _width(obj), H = _height(obj);
    int ox = _left(obj) - _left(d->view), oy = _top(obj) - _top(d->view);
    int VW = _width(d->view), VH = _height(d->view);
    Canvas cv(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            cv.set(x, y, welcomeGradient(ox + x + 0.5, oy + y + 0.5, VW, VH));
    IPTR sel = 0, dis = 0;
    get(obj, MUIA_Selected, &sel);
    get(obj, MUIA_Disabled, &dis);
    double il = dis ? 0.0 : d->illum;

    // WelcomeFocusRing (fraction 1) and WelcomeSpotlight (fraction =
    // illumination), both 160x160 at the cell origin with PlusLighter, the
    // ring before the cell as in BXWelcomeView.m:137-169. Original artwork.
    auto plusLighter = [&](const Image &img, double fraction) {
        for (int y = 0; y < img.h && y < H; ++y)
            for (int x = 0; x < img.w && x < W; ++x) {
                const uint8_t *p = &img.rgba[((size_t)y * img.w + x) * 4];
                if (p[3]) cv.addLight(x, y, (p[0] << 16) | (p[1] << 8) | p[2], p[3] / 255.0 * fraction);
            }
    };
    if (d->focused) plusLighter(originalImage(OriginalArt::WelcomeFocusRing), 1.0);
    if (il > 0) plusLighter(originalImage(OriginalArt::WelcomeSpotlight), il);
    // Image at 16,20 (BXWelcomeView.m:128-131) with its state overlay:
    // black (1-illum)*0.33 normally, white 0.15 while pressed.
    const Image &img = welcomeImage((WelcomeArt)d->art);
    // Disabled: AppKit draws a disabled button's image at half opacity
    // (NSButtonCell imageDimsWhenDisabled); the normal resting overlay
    // stays so the button still reads as the same, unlit cell.
    drawImage(cv, 16, 20, img, dis ? 0.5 : 1.0);
    double ov = (sel ? 0.15 : (1 - il) * 0.33) * (dis ? 0.5 : 1.0);
    unsigned ovc = sel ? 0xFFFFFF : 0x000000;
    if (sel || il < 0.9)
        for (int y = 0; y < img.h; ++y)
            for (int x = 0; x < img.w; ++x) {
                double a = img.rgba[((size_t)y * img.w + x) * 4 + 3] / 255.0;
                if (a > 0) cv.blend(16 + x, 20 + y, ovc, a * ov);
            }
    blit(obj, cv, _left(obj), _top(obj));

    // Title: bold 13, white at 0.75 + 0.25*illum, black shadow 1pt below.
    struct TextFont *f = g_fonts.bold13;
    int len = (int)std::strlen(d->title);
    int tw = textWidth(f, d->title, len);
    int tx = (W - tw) / 2;
    int centre = 165;   // derived: label sits about 157-173 pt from the top
    int tb = baselineIn(157, 16, f);
    unsigned bg = welcomeGradient(ox + W / 2.0, oy + centre, VW, VH);
    // Disabled title: Boxer's dark-theme disabledTextColor, white a=0.5
    // (BXThemes.m:204), instead of the resting 0.75.
    unsigned fg = mix(bg, 0xFFFFFF, dis ? 0.5 : 0.75 + 0.25 * il);
    unsigned sh = mix(bg, 0x000000, 0.6);   // blur 2 is not reproduced
    textRGB(obj, f, _left(obj) + tx, _top(obj) + tb + 1, d->title, len, sh, "welcome-shadow");
    textRGB(obj, f, _left(obj) + tx, _top(obj) + tb, d->title, len, fg, "welcome-title");
    return r;
}

BOOPSI_DISPATCHER(IPTR, WB_Dispatcher, cl, obj, msg)
{
    WBData *d;
    switch (msg->MethodID) {
    case OM_SET:
        if (ownDisabledLook(obj, (struct opSet *)msg)) {
            d = (WBData *)INST_DATA(cl, obj);
            d->hover = false;
            d->illum = d->target = 0;
            wbStop(obj, d);
        }
        break;
    case MUIM_AskMinMax: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        struct MUI_MinMax *mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        mm->MinWidth += 160; mm->DefWidth += 160; mm->MaxWidth += 160;
        mm->MinHeight += 186; mm->DefHeight += 186; mm->MaxHeight += 186;
        return r;
    }
    case MUIM_Setup:
        if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
        d = (WBData *)INST_DATA(cl, obj);
        d->ehn.ehn_Events = IDCMP_MOUSEMOVE;
        d->ehn.ehn_Priority = 1;
        d->ehn.ehn_Object = obj;
        d->ehn.ehn_Class = cl;
        DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&d->ehn);
        return TRUE;
    case MUIM_Cleanup:
        d = (WBData *)INST_DATA(cl, obj);
        wbStop(obj, d);
        DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&d->ehn);
        break;
    case MUIM_HandleEvent: {
        struct MUIP_HandleEvent *he = (struct MUIP_HandleEvent *)msg;
        d = (WBData *)INST_DATA(cl, obj);
        if (he->imsg && he->imsg->Class == IDCMP_MOUSEMOVE && windowActive(obj))
            wbSetHover(obj, d, inside(obj, he->imsg->MouseX, he->imsg->MouseY));
        return 0;
    }
    case WBM_SyncHover: {
        // windowDidBecomeKey lights the button under the pointer;
        // windowDidResignKey clears every hover (BXWelcomeWindowController.m:69-90).
        d = (WBData *)INST_DATA(cl, obj);
        struct Window *w = _window(obj);
        IPTR dis = 0;
        get(obj, MUIA_Disabled, &dis);
        bool h = !dis && w && windowActive(obj) && inside(obj, w->MouseX, w->MouseY);
        wbSetHover(obj, d, h);
        return 0;
    }
    case WBM_Tick: {
        d = (WBData *)INST_DATA(cl, obj);
        double step = 20.0 / 250.0;   // CABasicAnimation default 0.25 s
        if (d->illum < d->target) d->illum = std::min(d->target, d->illum + step);
        else d->illum = std::max(d->target, d->illum - step);
        if (d->illum == d->target) wbStop(obj, d);
        MUI_Redraw(obj, MADF_DRAWOBJECT);
        return 0;
    }
    case MUIM_GoActive:
        // Not passed to Area: Zune would add its dashed focus frame, while the
        // original draws WelcomeFocusRing instead.
        d = (WBData *)INST_DATA(cl, obj);
        d->focused = true;
        MUI_Redraw(obj, MADF_DRAWOBJECT);
        return TRUE;
    case MUIM_GoInactive:
        d = (WBData *)INST_DATA(cl, obj);
        d->focused = false;
        MUI_Redraw(obj, MADF_DRAWOBJECT);
        return TRUE;
    case MUIM_Draw:
        return WB_Draw(cl, obj, (struct MUIP_Draw *)msg);
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

Object *newWelcomeButton(int art, const char *title, Object *view) {
    Object *o = (Object *)NewObject(mccWelcome->mcc_Class, NULL,
                                    // The button paints its whole cell; Area's
                                    // fill before every animation frame only
                                    // made it flicker to the background.
                                    MUIA_FillArea, FALSE,
                                    MUIA_InputMode, MUIV_InputMode_RelVerify,
                                    MUIA_CycleChain, 1,
                                    TAG_DONE);
    if (!o) return nullptr;
    WBData *d = (WBData *)INST_DATA(mccWelcome->mcc_Class, o);
    d->art = art; d->view = view;
    std::snprintf(d->title, sizeof d->title, "%s", title);
    return o;
}

// ============================================================ ToolTab ====
struct TTData { int icon; char label[32]; int w; };
static struct MUI_CustomClass *mccTab;

BOOPSI_DISPATCHER(IPTR, TT_Dispatcher, cl, obj, msg)
{
    TTData *d;
    switch (msg->MethodID) {
    case OM_SET:
        ownDisabledLook(obj, (struct opSet *)msg);
        break;
    case MUIM_AskMinMax: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        d = (TTData *)INST_DATA(cl, obj);
        struct MUI_MinMax *mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        mm->MinWidth += d->w; mm->DefWidth += d->w; mm->MaxWidth += d->w;
        mm->MinHeight += 54; mm->DefHeight += 54; mm->MaxHeight += 54;
        return r;
    }
    case MUIM_Draw: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return r;
        d = (TTData *)INST_DATA(cl, obj);
        Canvas cv;
        grabBackground(obj, cv);
        IPTR sel = 0, dis = 0;
        get(obj, MUIA_Selected, &sel);
        get(obj, MUIA_Disabled, &dis);
        bool active = windowActive(obj);
        if (sel)   // stand-in for the system selected-item well
            paintLayers(cv, 0, 0, cv.w, cv.h,
                        {{{rrect(1, 1, cv.w - 2.0, cv.h - 2.0, 5)}, 0x000000, 0x000000, active ? 0.13 : 0.07}});
        // A disabled toolbar item dims icon and label (NSToolbarItem with
        // its action not validated); stronger than the inactive-window dim
        // so the two stay distinguishable.
        drawImage(cv, (cv.w - 32) / 2, 4, tabIcon((TabIcon)d->icon), dis ? 0.35 : active ? 1.0 : 0.6);
        blit(obj, cv, _left(obj), _top(obj));
        struct TextFont *f = g_fonts.small11;
        int len = (int)std::strlen(d->label);
        int tw = textWidth(f, d->label, len);
        // Label band 38..54 under the 32 pt icon.
        textRGB(obj, f, _left(obj) + (cv.w - tw) / 2, baselineIn(_top(obj) + 38, 16, f),
                d->label, len, dis ? 0xB4B4B4 : active ? 0x262626 : 0x8C8C8C, "tab");
        return r;
    }
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

Object *newToolTab(int icon, const char *label) {
    Object *o = (Object *)NewObject(mccTab->mcc_Class, NULL,
                                    MUIA_InputMode, MUIV_InputMode_Immediate,
                                    MUIA_ShowSelState, TRUE,
                                    TAG_DONE);
    if (!o) return nullptr;
    TTData *d = (TTData *)INST_DATA(mccTab->mcc_Class, o);
    d->icon = icon;
    std::snprintf(d->label, sizeof d->label, "%s", label);
    d->w = std::max(32, textWidth(g_fonts.small11, label, (int)std::strlen(label))) + 14;
    return o;
}

// ========================================================== CoverWell ====
// The gamebox's cover as the user sees it: BXImportIconDropzone on the
// finished panel and BXCoverArtWell in the Inspector (128 x 128 at x=84 in
// Inspector.xib). An Area subclass drawing the
// composed RGBA cover over the parent background. AROS: no drag and drop in
// this increment, so a click (RelVerify) asks for a picture instead of a drop.
struct CWData { uint8_t *rgba; int w, h; };
static struct MUI_CustomClass *mccCover;

BOOPSI_DISPATCHER(IPTR, CW_Dispatcher, cl, obj, msg)
{
    CWData *d;
    switch (msg->MethodID) {
    case OM_DISPOSE:
        d = (CWData *)INST_DATA(cl, obj);
        delete[] d->rgba;
        d->rgba = nullptr;
        break;
    case OM_SET:
        ownDisabledLook(obj, (struct opSet *)msg);
        break;
    case MUIM_AskMinMax: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        struct MUI_MinMax *mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        // 128 pt plus the original's 6 pt highlight margin on every side.
        mm->MinWidth += 140; mm->DefWidth += 140; mm->MaxWidth += 140;
        mm->MinHeight += 140; mm->DefHeight += 140; mm->MaxHeight += 140;
        return r;
    }
    case MUIM_Draw: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return r;
        d = (CWData *)INST_DATA(cl, obj);
        Canvas cv;
        grabBackground(obj, cv);
        IPTR sel = 0, dis = 0;
        get(obj, MUIA_Selected, &sel);
        get(obj, MUIA_Disabled, &dis);
        if (sel)   // stand-in for the keyboard-focus glow while pressed
            paintLayers(cv, 0, 0, cv.w, cv.h,
                        {{{rrect(1, 1, cv.w - 2.0, cv.h - 2.0, 6)}, 0x3874D8, 0x3874D8, 0.25}});
        if (d->rgba) {
            Image img;
            img.w = d->w; img.h = d->h;
            img.rgba.assign(d->rgba, d->rgba + (size_t)d->w * d->h * 4);
            drawImage(cv, (cv.w - d->w) / 2, (cv.h - d->h) / 2, img, dis ? 0.4 : 1.0);
        }
        blit(obj, cv, _left(obj), _top(obj));
        return r;
    }
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

Object *newCoverWell() {
    Object *o = (Object *)NewObject(mccCover->mcc_Class, NULL,
                                    MUIA_FillArea, FALSE,
                                    MUIA_InputMode, MUIV_InputMode_RelVerify,
                                    MUIA_ShowSelState, FALSE,
                                    MUIA_CycleChain, 1,
                                    TAG_DONE);
    return o;
}

void coverWellSetImage(Object *well, const uint8_t *rgba, int w, int h) {
    if (!well) return;
    CWData *d = (CWData *)INST_DATA(mccCover->mcc_Class, well);
    delete[] d->rgba;
    d->rgba = nullptr;
    d->w = d->h = 0;
    if (rgba && w > 0 && h > 0) {
        d->rgba = new uint8_t[(size_t)w * h * 4];
        std::memcpy(d->rgba, rgba, (size_t)w * h * 4);
        d->w = w; d->h = h;
    }
    MUI_Redraw(well, MADF_DRAWOBJECT);
}

// ========================================================== BxSlider ====
// NSSlider (controlSize small, linear) as a subclass of the standard Numeric
// class, the superclass of Zune's Slider. Why not a subclass of Slider:
// Slider keeps its knob rectangle, scale length and drag offset in private
// instance data that only its own MUIM_Draw computes (slider.c,
// Slider__MUIM_Draw / Slider__MUIM_HandleEvent). Replacing its drawing would
// leave its hit test and drag maths on a knob of a different size and place,
// and its knob size is derived from the font and the prefs knob frame, so no
// attribute gives the 11x13 pointer or 13x13 round knob of the xibs. Numeric
// keeps everything that is standard: value, min/max, notifications, cursor
// keys and wheel (Numeric__MUIM_HandleEvent). This class adds drawing, size
// and mouse tracking only. Values are proposals, see comparison.md "Sliders".
struct SLData {
    int w, h, ticks, tickPos;      // tickPos: 0 none, 1 below, 2 above
    bool ticksOnly, focusRing;
    bool hover, dragging, focused;
    int grab;                      // pointer x minus knob left while dragging
    // Focus-ring margin on every side. AppKit draws a focus ring outside
    // the control's frame; Zune clips drawing to the object, so the object
    // is this much larger than the xib frame and the control (track, knob,
    // ticks, hit area) keeps exactly the xib size inside it.
    int ring;
    struct MUI_EventHandlerNode ehn;
    bool ehnAdded;
};
static struct MUI_CustomClass *mccSlider;

namespace {
struct SliderGeom { int kw, kh, ky, trackY, tickY0, tickY1; bool round, up; };

SliderGeom sliderGeom(const SLData *d, int h) {
    SliderGeom g{};
    if (d->tickPos == 0) {          // round knob, centred; 15 pt frame
        g.round = true; g.kw = 13; g.kh = 13; g.ky = (h - 13) / 2;
        g.trackY = g.ky + 6;
    } else if (d->tickPos == 1) {   // pointer down, ticks below; 17 pt frame
        g.kw = 11; g.kh = 13; g.ky = 0; g.trackY = 4;
        g.tickY0 = h - 3; g.tickY1 = h;
    } else {                        // pointer up, ticks above; 18 pt frame
        g.up = true; g.kw = 11; g.kh = 13; g.ky = h - 13; g.trackY = h - 13 + 8;
        g.tickY0 = 0; g.tickY1 = 3;
    }
    return g;
}

// Knob outline, inset by i (0 = outer border, 1 = fill).
std::vector<Prim> knobPrims(const SliderGeom &g, double x, double y, double i) {
    if (g.round) return {circle(x + g.kw / 2.0, y + g.kh / 2.0, g.kw / 2.0 - i)};
    double w = g.kw, body = 8;
    if (!g.up)
        return {rrect(x + i, y + i, w - 2 * i, body + 1 - i, 2.5 - i),
                poly({{x + i, y + body}, {x + w - i, y + body}, {x + w / 2, y + g.kh - 0.2 - 1.4 * i}})};
    return {rrect(x + i, y + g.kh - body - 1, w - 2 * i, body + 1 - i, 2.5 - i),
            poly({{x + i, y + g.kh - body}, {x + w - i, y + g.kh - body}, {x + w / 2, y + 0.2 + 1.4 * i}})};
}

double sliderFrac(Object *obj) {
    IPTR v = 0, mn = 0, mx = 0;
    get(obj, MUIA_Numeric_Value, &v); get(obj, MUIA_Numeric_Min, &mn); get(obj, MUIA_Numeric_Max, &mx);
    LONG lo = (LONG)mn, hi = (LONG)mx;
    return hi > lo ? (double)((LONG)v - lo) / (hi - lo) : 0.0;
}
}  // namespace

// The control rectangle: the object minus the focus-ring margin.
static int ctlL(Object *o, const SLData *d) { return _left(o) + d->ring; }
static int ctlT(Object *o, const SLData *d) { return _top(o) + d->ring; }
static int ctlW(Object *o, const SLData *d) { return _width(o) - 2 * d->ring; }
static int ctlH(Object *o, const SLData *d) { return _height(o) - 2 * d->ring; }

static void SL_Paint(Object *obj, SLData *d) {
    Canvas full;
    grabBackground(obj, full);
    // Everything below draws in control coordinates on a control-sized
    // canvas; the ring is drawn on the full canvas with the margin offset.
    const int m = d->ring;
    Canvas cv(full.w - 2 * m, full.h - 2 * m);
    for (int y = 0; y < cv.h; ++y)
        std::memcpy(cv.at(0, y), full.at(m, y + m), (size_t)cv.w * 3);
    int W = cv.w, H = cv.h;
    SliderGeom g = sliderGeom(d, H);
    IPTR dis = 0;
    get(obj, MUIA_Disabled, &dis);
    // AppKit dims a disabled control as a whole (about half opacity).
    double a = dis ? 0.5 : 1.0;
    double span = W - g.kw;
    if (d->ticks > 1)
        for (int i = 0; i < d->ticks; ++i) {
            int x = (int)std::floor(g.kw / 2.0 + span * i / (d->ticks - 1));
            for (int y = g.tickY0; y < g.tickY1; ++y) cv.blend(x, y, 0x7A7A7A, a);
        }
    // Track: 4 pt groove, darker at the top edge (recessed).
    double ty = g.trackY - 2;
    paintLayers(cv, 0, 0, W, H, {
        {{rrect(1, ty, W - 2, 4, 2)}, 0x8C8C8C, 0xB9B9B9, a, ty, ty + 4},
        {{rrect(2, ty + 1, W - 4, 2.5, 1.25)}, 0xC8C8C8, 0xE4E4E4, a, ty + 1, ty + 3.5},
    });
    double kx = std::round(span * sliderFrac(obj));
    double ky = g.ky;
    bool ringShown = d->focused && d->focusRing && !dis;   // a disabled control shows no focus
    bool pressed = d->dragging;
    unsigned top = pressed ? 0xD9D9D9 : 0xFFFFFF, bot = pressed ? 0xC4C4C4 : 0xEDEDED;
    paintLayers(cv, 0, 0, W, H, {
        {knobPrims(g, kx, ky + 1, 0), 0x000000, 0x000000, 0.12 * a},          // drop shadow
        {knobPrims(g, kx, ky, 0), 0x7E7E7E, 0x8E8E8E, a, ky, ky + g.kh},     // border
        {knobPrims(g, kx, ky, 1), top, bot, a, ky + 1, ky + g.kh - 1},       // face
    });
    for (int y = 0; y < cv.h; ++y)
        std::memcpy(full.at(m, y + m), cv.at(0, y), (size_t)cv.w * 3);
    if (ringShown) {
        // Focus ring around the knob (keyboard focus colour, outer 3 pt),
        // in object coordinates so it may extend into the margin.
        std::vector<Prim> ring = knobPrims(g, kx + m, ky + m, -3);
        for (Prim &p : knobPrims(g, kx + m, ky + m, 0)) { p.subtract = true; ring.push_back(p); }
        paintLayers(full, 0, 0, full.w, full.h, {{ring, 0x3D8EF0, 0x3D8EF0, 0.55}});
    }
    blit(obj, full, _left(obj), _top(obj));
}

static bool insideCtl(Object *o, const SLData *d, int x, int y) {
    return x >= ctlL(o, d) && x < ctlL(o, d) + ctlW(o, d) && y >= ctlT(o, d) && y < ctlT(o, d) + ctlH(o, d);
}

// Logged at the event itself, after the redraw that shows the state, so a
// screenshot taken after this line and the line describe the same state.
static void sliderTrace(Object *o, SLData *d, const char *what, int mx, int my) {
    if (g_auditLog)
        g_auditLog("slider event: %s at %d,%d -> pressed=%d (redrawn) control %d,%d %dx%d",
                   what, mx, my, d->dragging, ctlL(o, d), ctlT(o, d), ctlW(o, d), ctlH(o, d));
}

static void SL_SetFromX(Object *obj, SLData *d, int mouseX) {
    SliderGeom g = sliderGeom(d, ctlH(obj, d));
    double span = ctlW(obj, d) - g.kw;
    double f = span > 0 ? (mouseX - ctlL(obj, d) - d->grab) / span : 0;
    f = std::clamp(f, 0.0, 1.0);
    IPTR mn = 0, mx = 0;
    get(obj, MUIA_Numeric_Min, &mn); get(obj, MUIA_Numeric_Max, &mx);
    LONG lo = (LONG)mn, hi = (LONG)mx;
    double v = lo + f * (hi - lo);
    if (d->ticksOnly && d->ticks > 1) {
        double step = (double)(hi - lo) / (d->ticks - 1);
        v = lo + std::round((v - lo) / step) * step;
    }
    set(obj, MUIA_Numeric_Value, (IPTR)(LONG)std::lround(v));
}

BOOPSI_DISPATCHER(IPTR, SL_Dispatcher, cl, obj, msg)
{
    SLData *d;
    switch (msg->MethodID) {
    case OM_SET: {
        if (ownDisabledLook(obj, (struct opSet *)msg)) {
            d = (SLData *)INST_DATA(cl, obj);
            d->dragging = false;
        }
        break;
    }
    case MUIM_AskMinMax: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        d = (SLData *)INST_DATA(cl, obj);
        struct MUI_MinMax *mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        mm->MinWidth += 40 + 2 * d->ring; mm->DefWidth += d->w + 2 * d->ring; mm->MaxWidth = MUI_MAXMAX;
        mm->MinHeight += d->h + 2 * d->ring; mm->DefHeight += d->h + 2 * d->ring;
        mm->MaxHeight += d->h + 2 * d->ring;
        return r;
    }
    case MUIM_Setup:
        if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
        d = (SLData *)INST_DATA(cl, obj);
        d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
        d->ehn.ehn_Priority = 1;
        d->ehn.ehn_Flags = MUI_EHF_GUIMODE;
        d->ehn.ehn_Object = obj;
        d->ehn.ehn_Class = cl;
        DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&d->ehn);
        d->ehnAdded = true;
        return TRUE;
    case MUIM_Cleanup:
        d = (SLData *)INST_DATA(cl, obj);
        if (d->ehnAdded) {
            DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&d->ehn);
            d->ehnAdded = false;
        }
        d->dragging = false;
        break;
    case MUIM_GoActive:
        // Not passed to Area: its dashed frame would replace AppKit's ring.
        d = (SLData *)INST_DATA(cl, obj);
        d->focused = true;
        MUI_Redraw(obj, MADF_DRAWOBJECT);
        return TRUE;
    case MUIM_GoInactive:
        d = (SLData *)INST_DATA(cl, obj);
        d->focused = false;
        MUI_Redraw(obj, MADF_DRAWOBJECT);
        return TRUE;
    case MUIM_HandleEvent: {
        // Only this class's own handler arrives here: Numeric registers its
        // key handler with its own class, so keys never pass through twice.
        struct MUIP_HandleEvent *he = (struct MUIP_HandleEvent *)msg;
        d = (SLData *)INST_DATA(cl, obj);
        if (!he->imsg) return 0;
        IPTR dis = 0;
        get(obj, MUIA_Disabled, &dis);
        int mx = he->imsg->MouseX, my = he->imsg->MouseY;
        if (he->imsg->Class == IDCMP_MOUSEMOVE) {
            if (d->dragging) { SL_SetFromX(obj, d, mx); return MUI_EventHandlerRC_Eat; }
            // NSSlider has no hover appearance; the state is only tracked
            // so the harness can log it.
            d->hover = insideCtl(obj, d, mx, my);
            return 0;
        }
        if (he->imsg->Class == IDCMP_MOUSEBUTTONS) {
            if (he->imsg->Code == SELECTDOWN && !dis && insideCtl(obj, d, mx, my)) {
                SliderGeom g = sliderGeom(d, ctlH(obj, d));
                int kx = ctlL(obj, d) + (int)std::round((ctlW(obj, d) - g.kw) * sliderFrac(obj));
                // On the knob: grab where it was hit. On the track: AppKit
                // centres the knob under the pointer and keeps tracking.
                d->grab = (mx >= kx && mx < kx + g.kw) ? mx - kx : g.kw / 2;
                d->dragging = true;
                // No focus change on click: AppKit gives a slider keyboard
                // focus only through Tab with Full Keyboard Access.
                SL_SetFromX(obj, d, mx);
                MUI_Redraw(obj, MADF_DRAWOBJECT);
                sliderTrace(obj, d, "press", mx, my);
                return MUI_EventHandlerRC_Eat;
            }
            if (he->imsg->Code == SELECTUP && d->dragging) {
                d->dragging = false;
                MUI_Redraw(obj, MADF_DRAWOBJECT);
                sliderTrace(obj, d, "release", mx, my);
                return MUI_EventHandlerRC_Eat;
            }
        }
        return 0;
    }
    case MUIM_Draw: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return r;
        SL_Paint(obj, (SLData *)INST_DATA(cl, obj));
        return r;
    }
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

Object *newSlider(LONG min, LONG max, LONG value, int w, int h, int ticks, int tickPos,
                  bool ticksOnly, bool focusRing) {
    Object *o = (Object *)NewObject(mccSlider->mcc_Class, NULL,
                                    MUIA_Numeric_Min, (IPTR)min, MUIA_Numeric_Max, (IPTR)max,
                                    MUIA_Numeric_Value, (IPTR)value,
                                    MUIA_CycleChain, 1, TAG_DONE);
    if (!o) return nullptr;
    SLData *d = (SLData *)INST_DATA(mccSlider->mcc_Class, o);
    d->w = w; d->h = h; d->ticks = ticks; d->tickPos = tickPos;
    d->ticksOnly = ticksOnly; d->focusRing = focusRing;
    d->ring = focusRing ? kSliderRing : 0;
    return o;
}

bool sliderState(Object *o, bool *hover, bool *pressed, bool *focused) {
    SLData *d = (SLData *)INST_DATA(mccSlider->mcc_Class, o);
    *hover = d->hover; *pressed = d->dragging; *focused = d->focused;
    return true;
}

// ========================================================= RenderView ====
// BXFrameRenderingView's role: shows the core's frames in the viewport
// (aspect-corrected size fitted and centred, BXGLRenderingView
// viewportForFrame:) over the DOS window background, and reports keys and
// pointer events to the session (g_renderEvents). Without a frame it shows
// the slice-A test pattern at 4:3.
//
// Scaling is done here, not with cybergraphics ScalePixelArray: Smoothed
// needs bilinear filtering, which ScalePixelArray does not offer, and one
// scaler for both styles keeps Normal and Smoothed sampling the same pixel
// centres. Output goes to the screen with one WritePixelArray per frame.
struct RVData {
    bool fullscreen;
    int lastW, lastH;
    RenderFrame frame;          // pixels owned by the emulator; null = none
    bool hasFrame;
    bool shown;                 // between MUIM_Show and MUIM_Hide
    std::vector<uint32_t> scaled;
    std::vector<int> xmap;
    int mapW;                   // source width xmap was built for (smoothed)
    struct MUI_EventHandlerNode ehn;
};
static struct MUI_CustomClass *mccRender;
RenderEvents g_renderEvents;

static Rect rvViewport(Object *obj, RVData *d) {
    Size scaled = d->hasFrame ? Size{d->frame.scaledW, d->frame.scaledH} : Size{320, 240};
    return viewportFor(scaled, {_mwidth(obj), _mheight(obj)});
}

Rect renderViewport(Object *obj) {
    return rvViewport(obj, (RVData *)INST_DATA(mccRender->mcc_Class, obj));
}

static inline uint32_t lerp4(uint32_t a, uint32_t b, unsigned t) {   // t 0..256
    uint32_t rb = (((a & 0xFF00FF) * (256 - t) + (b & 0xFF00FF) * t) >> 8) & 0xFF00FF;
    uint32_t g = (((a & 0x00FF00) * (256 - t) + (b & 0x00FF00) * t) >> 8) & 0x00FF00;
    return 0xFF000000u | rb | g;
}

static void rvScale(RVData *d, int vw, int vh) {
    const RenderFrame &f = d->frame;
    d->scaled.resize((size_t)vw * vh);
    const int pitchPx = f.pitch / 4;
    if (!f.smooth) {
        // Nearest: source pixel under each destination pixel centre.
        d->mapW = -1;
        d->xmap.resize(vw);
        for (int x = 0; x < vw; ++x)
            d->xmap[x] = std::min(f.w - 1, (int)(((long long)(2 * x + 1) * f.w) / (2LL * vw)));
        for (int y = 0; y < vh; ++y) {
            int sy = std::min(f.h - 1, (int)(((long long)(2 * y + 1) * f.h) / (2LL * vh)));
            const uint32_t *src = f.pixels + (size_t)sy * pitchPx;
            uint32_t *dst = &d->scaled[(size_t)y * vw];
            for (int x = 0; x < vw; ++x) dst[x] = src[d->xmap[x]];
        }
        return;
    }
    // Smoothed: bilinear between the four nearest source pixel centres,
    // with the per-column source positions and weights computed once per
    // size (fixed point, 8-bit weights) so a frame costs integer work only.
    if ((int)d->xmap.size() != vw * 3 || d->mapW != f.w) {
        d->xmap.resize(vw * 3);
        d->mapW = f.w;
        for (int x = 0; x < vw; ++x) {
            double sx = std::clamp((x + 0.5) * f.w / vw - 0.5, 0.0, (double)f.w - 1);
            int x0 = (int)sx;
            d->xmap[x * 3] = x0;
            d->xmap[x * 3 + 1] = std::min(x0 + 1, f.w - 1);
            d->xmap[x * 3 + 2] = (int)((sx - x0) * 256);
        }
    }
    for (int y = 0; y < vh; ++y) {
        double sy = std::clamp((y + 0.5) * f.h / vh - 0.5, 0.0, (double)f.h - 1);
        int y0 = (int)sy, y1 = std::min(y0 + 1, f.h - 1);
        unsigned ty = (unsigned)((sy - y0) * 256);
        const uint32_t *r0 = f.pixels + (size_t)y0 * pitchPx, *r1 = f.pixels + (size_t)y1 * pitchPx;
        uint32_t *dst = &d->scaled[(size_t)y * vw];
        const int *m = d->xmap.data();
        for (int x = 0; x < vw; ++x, m += 3) {
            unsigned tx = (unsigned)m[2];
            dst[x] = lerp4(lerp4(r0[m[0]], r0[m[1]], tx), lerp4(r1[m[0]], r1[m[1]], tx), ty);
        }
    }
}

static IPTR RV_Draw(struct IClass *cl, Object *obj, struct MUIP_Draw *msg) {
    IPTR r = DoSuperMethodA(cl, obj, (Msg)msg);
    if (!(msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return r;
    RVData *d = (RVData *)INST_DATA(cl, obj);
    int W = _mwidth(obj), H = _mheight(obj);
    if (W != d->lastW || H != d->lastH) {
        d->lastW = W; d->lastH = H;
        if (!d->fullscreen && g_mainTask) {
            g_renderResized = true;
            Signal(g_mainTask, SIGBREAKF_CTRL_F);
        }
    }
    Rect vp = rvViewport(obj, d);
    bool full = (msg->flags & MADF_DRAWOBJECT) || !d->hasFrame;
    if (full) {
        // Background (BXDOSWindowBackgroundView) outside the viewport.
        Canvas cv(W, H);
        const Canvas *pattern = d->hasFrame ? nullptr : &testPattern();
        for (int y = 0; y < H; ++y) {
            bool rowIn = y >= vp.y && y < vp.y + vp.h;
            for (int x = 0; x < W; ++x) {
                if (rowIn && x >= vp.x && x < vp.x + vp.w) {
                    if (!pattern) continue;   // the frame is drawn below
                    int sy = std::min(pattern->h - 1, (y - vp.y) * pattern->h / vp.h);
                    int sx = std::min(pattern->w - 1, (x - vp.x) * pattern->w / vp.w);
                    const uint8_t *p = &pattern->rgb[((size_t)sy * pattern->w + sx) * 3];
                    uint8_t *q = cv.at(x, y);
                    q[0] = p[0]; q[1] = p[1]; q[2] = p[2];
                } else {
                    cv.set(x, y, dosBackground(x + 0.5, y + 0.5, W, H));
                }
            }
        }
        if (d->hasFrame) {
            // Only the band outside the viewport: the frame covers the rest.
            if (vp.y > 0) WritePixelArray(cv.rgb.data(), 0, 0, W * 3, _rp(obj), _mleft(obj), _mtop(obj), W, vp.y, RECTFMT_RGB);
            int below = H - vp.y - vp.h;
            if (below > 0) WritePixelArray(cv.rgb.data(), 0, vp.y + vp.h, W * 3, _rp(obj), _mleft(obj), _mtop(obj) + vp.y + vp.h, W, below, RECTFMT_RGB);
            if (vp.x > 0) WritePixelArray(cv.rgb.data(), 0, vp.y, W * 3, _rp(obj), _mleft(obj), _mtop(obj) + vp.y, vp.x, vp.h, RECTFMT_RGB);
            int right = W - vp.x - vp.w;
            if (right > 0) WritePixelArray(cv.rgb.data(), vp.x + vp.w, vp.y, W * 3, _rp(obj), _mleft(obj) + vp.x + vp.w, _mtop(obj) + vp.y, right, vp.h, RECTFMT_RGB);
        } else {
            blit(obj, cv, _mleft(obj), _mtop(obj));
        }
    }
    if (d->hasFrame && vp.w > 0 && vp.h > 0) {
        rvScale(d, vp.w, vp.h);
        // 0xFFRRGGBB words are B,G,R,A bytes in memory (little-endian).
        WritePixelArray(d->scaled.data(), 0, 0, vp.w * 4, _rp(obj), _mleft(obj) + vp.x,
                        _mtop(obj) + vp.y, vp.w, vp.h, RECTFMT_BGRA32);
    }
    return r;
}

void renderSetFrame(Object *obj, const RenderFrame *f) {
    RVData *d = (RVData *)INST_DATA(mccRender->mcc_Class, obj);
    bool had = d->hasFrame;
    Size before = {d->frame.scaledW, d->frame.scaledH};
    if (f) { d->frame = *f; d->hasFrame = true; }
    else d->hasFrame = false;
    // _window() reads render info that an object never set up does not
    // have, so the view tracks MUIM_Show/MUIM_Hide itself.
    if (!d->shown) return;   // drawn on the next MUIM_Draw
    bool layoutChanged = had != d->hasFrame || before.w != d->frame.scaledW || before.h != d->frame.scaledH;
    MUI_Redraw(obj, layoutChanged ? MADF_DRAWOBJECT : MADF_DRAWUPDATE);
}

BOOPSI_DISPATCHER(IPTR, RV_Dispatcher, cl, obj, msg)
{
    RVData *d;
    switch (msg->MethodID) {
    case MUIM_AskMinMax: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        struct MUI_MinMax *mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        // Minimum: the scaled base resolution, 320x200 shown at 4:3
        // (BXDOSWindowController.m:1604-1617); default 640x480 (:191-203).
        mm->MinWidth += 320; mm->MinHeight += 240;
        mm->DefWidth += 640; mm->DefHeight += 480;
        mm->MaxWidth = MUI_MAXMAX; mm->MaxHeight = MUI_MAXMAX;
        return r;
    }
    case MUIM_Setup:
        if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
        d = (RVData *)INST_DATA(cl, obj);
        d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
        // Above the window's own key handling, so DOS gets Tab, Return and
        // the cursor keys while a session runs.
        d->ehn.ehn_Priority = 10;
        d->ehn.ehn_Object = obj;
        d->ehn.ehn_Class = cl;
        DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&d->ehn);
        return TRUE;
    case MUIM_Cleanup:
        d = (RVData *)INST_DATA(cl, obj);
        DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&d->ehn);
        break;
    case MUIM_HandleEvent: {
        struct MUIP_HandleEvent *he = (struct MUIP_HandleEvent *)msg;
        struct IntuiMessage *im = he->imsg;
        if (!im) return 0;
        const UWORD qual = im->Qualifier;
        const bool amiga = qual & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND);
        const int x = im->MouseX - _mleft(obj), y = im->MouseY - _mtop(obj);
        d = (RVData *)INST_DATA(cl, obj);
        // A view on a hidden page (the launch panel shown in its place) keeps
        // its old layout rectangle: pointer and plain keys belong to the
        // panel then. Amiga keys still reach commandKey (RAmiga+G).
        if (!d->shown && (im->Class != IDCMP_RAWKEY || !amiga)) return 0;
        switch (im->Class) {
        case IDCMP_RAWKEY:
            // Amiga-qualified keys stay with the window (menu shortcuts and
            // RAmiga+L/F/Q), as Cmd keys stay with the menus in the original.
            // Releases still go to rawKey: a key pressed before the Amiga
            // key (Alt in Amiga+Alt+cursor) must not stay held in DOS, and
            // the core forwards only releases of keys it saw pressed.
            if (amiga && !(im->Code & IECODE_UP_PREFIX)) {
                if (g_renderEvents.commandKey &&
                    g_renderEvents.commandKey(obj, im->Code, false, qual))
                    return MUI_EventHandlerRC_Eat;
                return 0;
            }
            if (amiga && g_renderEvents.commandKey &&
                g_renderEvents.commandKey(obj, im->Code & ~IECODE_UP_PREFIX, true, qual))
                return MUI_EventHandlerRC_Eat;
            if (!g_renderEvents.rawKey) return 0;
            if (amiga) {
                // Forwarded only if the core holds it; never eaten, so the
                // window's own Amiga-key handling is unchanged.
                g_renderEvents.rawKey(obj, im->Code & ~IECODE_UP_PREFIX, true, qual);
                return 0;
            }
            return g_renderEvents.rawKey(obj, im->Code & ~IECODE_UP_PREFIX,
                                         im->Code & IECODE_UP_PREFIX, qual)
                       ? MUI_EventHandlerRC_Eat : 0;
        case IDCMP_MOUSEMOVE:
            if (g_renderEvents.mouseMove) g_renderEvents.mouseMove(obj, x, y, qual);
            return 0;   // other objects track the pointer too
        case IDCMP_MOUSEBUTTONS: {
            int button = -1;
            bool down = false;
            switch (im->Code) {
            case SELECTDOWN: button = 0; down = true; break;
            case SELECTUP: button = 0; break;
            case MENUDOWN: button = 1; down = true; break;
            case MENUUP: button = 1; break;
            case MIDDLEDOWN: button = 2; down = true; break;
            case MIDDLEUP: button = 2; break;
            }
            if (button < 0) return 0;
            bool in = inside(obj, im->MouseX, im->MouseY);
            if (g_renderEvents.button && g_renderEvents.button(obj, button, down, qual, x, y, in))
                return MUI_EventHandlerRC_Eat;
            // Without a session: the slice-A lock behaviour on a plain click.
            if (button == 0 && down && in) {
                if (g_renderClick) g_renderClick(amiga);
                return MUI_EventHandlerRC_Eat;
            }
            return 0;
        }
        }
        return 0;
    }
    case MUIM_Show:
        ((RVData *)INST_DATA(cl, obj))->shown = true;
        break;
    case MUIM_Hide:
        ((RVData *)INST_DATA(cl, obj))->shown = false;
        break;
    case OM_DISPOSE:
        d = (RVData *)INST_DATA(cl, obj);
        d->scaled.~vector();
        d->xmap.~vector();
        break;
    case MUIM_Draw:
        return RV_Draw(cl, obj, (struct MUIP_Draw *)msg);
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

Object *newRenderView(bool fullscreen) {
    Object *o = (Object *)NewObject(mccRender->mcc_Class, NULL, MUIA_FillArea, FALSE, TAG_DONE);
    if (!o) return nullptr;
    RVData *d = (RVData *)INST_DATA(mccRender->mcc_Class, o);
    // Instance data is cleared but not constructed: build the vectors in place.
    new (&d->scaled) std::vector<uint32_t>();
    new (&d->xmap) std::vector<int>();
    d->fullscreen = fullscreen;
    return o;
}

// ======================================================= Launch panel ====
// BXDOSWindowBackgroundView _drawBackgroundInRect: + _drawLightingInRect:
// (the grilles and the brand are commented out there in 2.0-alpha). x, y in
// panel coordinates (top-left), W x H the panel. The lighting is NSGradient
// drawFromCenter:(midX, top) radius 0.1W toCenter:(midX, midY) radius 0.75W,
// white a=0.2 -> black a=0.2, extended before and after; then a 6 pt shadow
// at the top edge (black 0.3 -> 0.05 at half -> clear).
static unsigned lpBackground(double x, double y, double W, double H) {
    const double bg[3] = {97, 98, 103};
    double px = x + 0.5 - W / 2, py = y + 0.5;
    double r0 = 0.1 * W, dr = 0.75 * W - r0, dy = H / 2;
    double a = dy * dy - dr * dr, b = py * dy + r0 * dr, c = px * px + py * py - r0 * r0;
    double t;
    if (std::fabs(a) < 1e-9) t = b != 0 ? c / (2 * b) : 0;
    else {
        double disc = b * b - a * c;
        if (disc < 0) disc = 0;
        double s = std::sqrt(disc), t1 = (b + s) / a, t2 = (b - s) / a;
        t = std::max(t1, t2);
        if (r0 + t * dr < 0) t = std::min(t1, t2);
    }
    t = std::clamp(t, 0.0, 1.0);
    double light = 255.0 * (1 - t);   // premultiplied white->black at a=0.2
    double shadow = 0;
    if (y < 3) shadow = 0.3 + (0.05 - 0.3) * (y + 0.5) / 3;
    else if (y < 6) shadow = 0.05 * (1 - (y + 0.5 - 3) / 3);
    unsigned out = 0;
    for (int k = 0; k < 3; ++k) {
        double v = bg[k] * 0.8 + 0.2 * light;
        v *= 1 - shadow;
        out = (out << 8) | (unsigned)std::clamp(std::lround(v), 0L, 255L);
    }
    return out;
}

// The lighting costs a square root per pixel; under QEMU TCG a filter
// keystroke repainted the list in 0.9 s. Rows of the current panel size are
// computed once and reused.
static int g_bgW = -1, g_bgH = -1;
static std::vector<std::vector<unsigned>> g_bgRows;
static unsigned lpBg(int x, int y, int W, int H) {
    if (W != g_bgW || H != g_bgH) { g_bgW = W; g_bgH = H; g_bgRows.clear(); }
    if (x < 0 || x >= W || y < 0 || y > 4096) return lpBackground(x, y, W, H);
    if ((int)g_bgRows.size() <= y) g_bgRows.resize(y + 1);
    std::vector<unsigned> &row = g_bgRows[y];
    if (row.empty()) {
        row.resize(W);
        for (int i = 0; i < W; ++i) row[i] = lpBackground(i, y, W, H);
    }
    return row[x];
}

static Object *g_lpRoot, *g_lpList;
static const int kLPBarH = 40;     // LaunchPanel.xib: search bar view 640x40
static const int kLPRowH = 44;     // the item prototype's size (LauncherItem.xib)
static int lpPanelHeight() { return g_lpRoot ? _height(g_lpRoot) : 480; }
// Panel coordinates: the scroll bar takes width from the list, not from the
// window background under it.
static int lpPanelWidth(Object *fallback) { return g_lpRoot ? _width(g_lpRoot) : _width(fallback); }
static int lpPanelLeft() { return g_lpRoot ? _left(g_lpRoot) : 0; }

void (*g_launcherAction)(int, int) = nullptr;

// LauncherList: Virtgroup subclass. Only MUIM_DrawBackground is overridden:
// Zune scrolls a virtual group with ScrollWindowRaster, so a background
// fixed to the window (as in the original, where the clip view does not
// draw) would tear; it is painted in content coordinates instead, which is
// identical while the list fits and moves with the rows when it scrolls.
struct LLData { int dummy; };
static struct MUI_CustomClass *mccLList;

static int lpVirtTop(Object *list) {
    IPTR top = 0;
    get(list, MUIA_Virtgroup_Top, &top);
    return (int)top;
}

static IPTR LL_DrawBackground(struct IClass *cl, Object *obj, struct MUIP_DrawBackground *msg) {
    if (msg->width <= 0 || msg->height <= 0) return 0;
    Canvas cv(msg->width, msg->height);
    int W = lpPanelWidth(obj), H = lpPanelHeight(), vt = lpVirtTop(obj);
    for (int y = 0; y < msg->height; ++y)
        for (int x = 0; x < msg->width; ++x)
            cv.set(x, y, lpBg(msg->left - lpPanelLeft() + x,
                              msg->top - _mtop(obj) + vt + kLPBarH + y, W, H));
    WritePixelArray(cv.rgb.data(), 0, 0, cv.w * 3, _rp(obj), msg->left, msg->top, cv.w, cv.h, RECTFMT_RGB);
    return TRUE;
}

BOOPSI_DISPATCHER(IPTR, LL_Dispatcher, cl, obj, msg)
{
    if (msg->MethodID == MUIM_DrawBackground)
        return LL_DrawBackground(cl, obj, (struct MUIP_DrawBackground *)msg);
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

// LauncherItem: one collection-view item (heading, favorite or program).
// Area-derived (new class): Zune's List draws prefs-styled single-line
// entries, and Button/Text cannot draw the hover and pressed states of
// BXLauncherRegularItemView, the themed text with drop shadows, the heading
// template icon or the divider. Click handling follows BXLauncherRegularItemView
// mouseDown:/mouseUp: (launch on release inside), the context menu is
// Area's standard MUIA_ContextMenu (BXLauncherItem menuForView:).
struct LIData {
    int index;
    LPRow *row;
    bool hover, active, launchable;
    Object *menu;
    struct MUI_EventHandlerNode ehn;
    bool ehnAdded;
};
static struct MUI_CustomClass *mccLItem;
static std::vector<LPRow> g_lpRows;

static unsigned blendRGB(unsigned d, unsigned s, double a) { return mix(d, s, a); }

// Clip a row to the list's visible area (rows scrolled out stay laid out).
static bool lpVisibleRect(Object *obj, int &l, int &t, int &r, int &b) {
    l = _left(obj); t = _top(obj); r = _right(obj); b = _bottom(obj);
    if (g_lpList) {
        l = std::max(l, (int)_mleft(g_lpList)); t = std::max(t, (int)_mtop(g_lpList));
        r = std::min(r, (int)_mright(g_lpList)); b = std::min(b, (int)_mbottom(g_lpList));
    }
    return l <= r && t <= b;
}

// Text clipped to a width with a trailing "..." (NSLineBreakByTruncatingTail
// is the label default in these xibs).
static std::string fitText(struct TextFont *f, const std::string &s, int maxW) {
    if (textWidth(f, s.c_str(), (int)s.size()) <= maxW) return s;
    std::string t = s;
    while (!t.empty() && textWidth(f, (t + "...").c_str(), (int)t.size() + 3) > maxW) t.pop_back();
    return t + "...";
}

static void lpText(Object *obj, struct TextFont *f, int x, int boxTop, int boxH, int maxW,
                   const std::string &s, unsigned rgb, unsigned shadowRGB, int shadowDY) {
    std::string t = fitText(f, s, maxW);
    int base = baselineIn(boxTop, boxH, f);
    if (shadowDY) textRGB(obj, f, x, base + shadowDY, t.c_str(), (int)t.size(), shadowRGB, "launcher-shadow");
    textRGB(obj, f, x, base, t.c_str(), (int)t.size(), rgb, "launcher");
}

static IPTR LI_Draw(struct IClass *cl, Object *obj, struct MUIP_Draw *msg) {
    IPTR rr = DoSuperMethodA(cl, obj, (Msg)msg);
    if (!(msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return rr;
    LIData *d = (LIData *)INST_DATA(cl, obj);
    if (!d->row || !g_lpList) return rr;
    const LPRow &row = *d->row;
    int vl, vt, vr, vb;
    if (!lpVisibleRect(obj, vl, vt, vr, vb)) return rr;
    const int W = _width(obj), H = _height(obj);
    const int listW = lpPanelWidth(obj), panelH = lpPanelHeight();
    // Row origin in panel coordinates (content scrolls with the list).
    const int ox = _left(obj) - lpPanelLeft();
    const int oy = _top(obj) - _mtop(g_lpList) + lpVirtTop(g_lpList) + kLPBarH;
    Canvas cv(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) cv.set(x, y, lpBg(ox + x, oy + y, listW, panelH));
    const bool regular = row.kind == LPRow::Favorite || row.kind == LPRow::Program;
    if (regular && d->launchable) {
        if (d->active) {
            // NSCompositePlusDarker of alternateSelectedControlColor a=0.25
            // over the row inset by 1 pt, white a=0.1 lines top and bottom.
            const int sel[3] = {5, 84, 212};
            for (int y = 1; y < H - 1; ++y)
                for (int x = 0; x < W; ++x) {
                    uint8_t *p = cv.at(x, y);
                    for (int k = 0; k < 3; ++k)
                        p[k] = (uint8_t)std::max(0.0, p[k] - (255 - sel[k]) * 0.25);
                }
            // Inner shadow (blur 4, offset 1 down, black 0.15): a soft band under the top edge.
            for (int y = 1; y < std::min(H - 1, 6); ++y) {
                double a = 0.15 * (1 - (y - 1) / 5.0);
                for (int x = 0; x < W; ++x) {
                    uint8_t *p = cv.at(x, y);
                    cv.set(x, y, blendRGB((p[0] << 16) | (p[1] << 8) | p[2], 0x000000, a));
                }
            }
            for (int x = 0; x < W; ++x) {
                uint8_t *p = cv.at(x, 0);
                cv.set(x, 0, blendRGB((p[0] << 16) | (p[1] << 8) | p[2], 0xFFFFFF, 0.1));
                p = cv.at(x, H - 1);
                cv.set(x, H - 1, blendRGB((p[0] << 16) | (p[1] << 8) | p[2], 0xFFFFFF, 0.1));
            }
        } else if (d->hover) {
            // Radial white a=0.1 at radius 0.25W fading to clear at 0.5W.
            double cx = W / 2.0, cy = H / 2.0, r0 = W * 0.25, r1 = W * 0.5;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    double dist = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
                    double a = dist <= r0 ? 0.1 : dist >= r1 ? 0 : 0.1 * (r1 - dist) / (r1 - r0);
                    if (a <= 0) continue;
                    uint8_t *p = cv.at(x, y);
                    cv.set(x, y, blendRGB((p[0] << 16) | (p[1] << 8) | p[2], 0xFFFFFF, a));
                }
        }
    }
    if (row.isHeading()) {
        // LauncherHeading.xib: icon 24x24 at (99, 6) filled with the heading
        // theme's gradient (black 0.25 -> 0.33) and a white a=0.25 shadow
        // 1 pt below; divider 600x4 at (20, 38), the 560 pt image centred.
        LauncherArt art = row.icon == LPIcon::Favorites ? LauncherArt::Favorites
                        : row.icon == LPIcon::Recent ? LauncherArt::Recent
                        : row.icon == LPIcon::AllPrograms ? LauncherArt::AllPrograms
                        : row.icon == LPIcon::CDROM ? LauncherArt::CDROM
                        : row.icon == LPIcon::Floppy ? LauncherArt::Floppy : LauncherArt::HardDisk;
        const Mask &m = launcherIcon(art);
        paintMask(cv, 99, 7, m, 0xFFFFFF, 0.25);
        for (int y = 0; y < m.h; ++y) {
            double a = 0.25 + (0.33 - 0.25) * y / std::max(1, m.h - 1);
            for (int x = 0; x < m.w; ++x)
                if (m.at(x, y) > 0) cv.blend(99 + x, 6 + y, 0x000000, m.at(x, y) * a);
        }
        const Image &div = launchPanelDivider();
        drawImage(cv, 20 + (600 - div.w) / 2, 38, div);
    }
    // Only the visible part goes to the window.
    WritePixelArray(cv.rgb.data(), vl - _left(obj), vt - _top(obj), W * 3, _rp(obj), vl, vt,
                    vr - vl + 1, vb - vt + 1, RECTFMT_RGB);
    // Themed text (BXThemes.m): BXLauncherTheme white with a black a=0.5
    // shadow 1 pt down; BXLauncherHelpTextTheme white a=0.75; the heading
    // theme black a=0.5 with a white a=0.25 shadow 1 pt down. Over the
    // panel grey these are the colours below (blur not reproduced).
    const unsigned bgc = lpBg(ox + 128, oy + 10, listW, panelH);
    // While a program runs, rows are not launchable. The original's
    // BXLauncherItemView setEnabled: only drops the hover/pressed art (its
    // labels have no enabled binding in LauncherItem.xib), so a row looks
    // clickable but ignores clicks. AROS: the text is dimmed as well, with
    // Boxer's own dark-theme disabledTextColor, white a=0.5 (BXThemes.m:204),
    // so a running program visibly locks the list; the shadow stays.
    const bool dim = regular && !d->launchable;
    const unsigned dark = blendRGB(bgc, 0x000000, 0.5);
    const unsigned title = dim ? blendRGB(bgc, 0xFFFFFF, 0.5) : 0xFFFFFF;
    const unsigned help = blendRGB(bgc, 0xFFFFFF, dim ? 0.5 : 0.75);
    const int L = _left(obj), T = _top(obj);
    switch (row.kind) {
    case LPRow::SectionHeading:
    case LPRow::DriveHeading: {
        std::string up = row.title;
        for (auto &ch : up) ch = (char)std::toupper((unsigned char)ch);
        lpText(obj, g_fonts.bold13, L + 128, T + 11, 19, 455, up, dark, blendRGB(bgc, 0xFFFFFF, 0.25), 1);
        break;
    }
    case LPRow::Favorite:
        // LauncherFavorite.xib is 40 pt high inside a 44 pt item: the
        // alignment wrapper keeps its 2 pt offset as in LauncherItem.xib.
        lpText(obj, g_fonts.bold16, L + 128, T + 6, 17, 494, row.title, title, dark, 1);
        lpText(obj, g_fonts.small11, L + 128, T + 25, 17, 494, row.subtitle(), help, dark, 1);
        break;
    case LPRow::Program:
        lpText(obj, g_fonts.bold13, L + 128, T + 4, 17, 494, row.title, title, dark, 1);
        lpText(obj, g_fonts.small11, L + 128, T + 23, 17, 494, row.subtitle(), help, dark, 1);
        break;
    }
    return rr;
}

static Object *lpMenu(const LPRow &row, bool launchable) {
    // BXLauncherItem menuForView: no menu without a URL (section headings).
    // Items: Launch / Show in Finder / Remove From List (hidden unless
    // removable); a drive heading has Switch to Drive / Show in Finder.
    // "Show in Finder" is not ported (no AROS reveal action yet).
    if (row.kind == LPRow::SectionHeading) return nullptr;
    auto item = [](const char *title, IPTR action, bool enabled) {
        return (IPTR)MUI_NewObject(MUIC_Menuitem, MUIA_Menuitem_Title, (IPTR)title,
                                   MUIA_Menuitem_Enabled, enabled, MUIA_UserData, action, TAG_DONE);
    };
    Object *menu;
    if (row.kind == LPRow::DriveHeading) {
        menu = MUI_NewObject(MUIC_Menu, MUIA_Menu_Title, (IPTR)"Drive",
                             MUIA_Family_Child, item("Switch to Drive", LA_Open, launchable), TAG_DONE);
    } else if (row.launcher >= 0 || row.recent >= 0) {
        menu = MUI_NewObject(MUIC_Menu, MUIA_Menu_Title, (IPTR)"Program",
                             MUIA_Family_Child, item("Launch", LA_Open, launchable),
                             MUIA_Family_Child, item("Remove From List", LA_Remove, true), TAG_DONE);
    } else {
        menu = MUI_NewObject(MUIC_Menu, MUIA_Menu_Title, (IPTR)"Program",
                             MUIA_Family_Child, item("Launch", LA_Open, launchable), TAG_DONE);
    }
    return menu ? MUI_NewObject(MUIC_Menustrip, MUIA_Family_Child, (IPTR)menu, TAG_DONE) : nullptr;
}

BOOPSI_DISPATCHER(IPTR, LI_Dispatcher, cl, obj, msg)
{
    LIData *d;
    switch (msg->MethodID) {
    case OM_DISPOSE: {
        d = (LIData *)INST_DATA(cl, obj);
        Object *menu = d->menu;
        delete d->row;
        IPTR r = DoSuperMethodA(cl, obj, msg);
        if (menu) MUI_DisposeObject(menu);
        return r;
    }
    case MUIM_AskMinMax: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        struct MUI_MinMax *mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        mm->MinWidth += 160; mm->DefWidth += 640; mm->MaxWidth = MUI_MAXMAX;
        mm->MinHeight += kLPRowH; mm->DefHeight += kLPRowH; mm->MaxHeight += kLPRowH;
        return r;
    }
    case MUIM_Setup:
        if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
        d = (LIData *)INST_DATA(cl, obj);
        if (d->row && !d->row->isHeading()) {
            d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
            d->ehn.ehn_Priority = 1;
            d->ehn.ehn_Object = obj;
            d->ehn.ehn_Class = cl;
            DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&d->ehn);
            d->ehnAdded = true;
        }
        return TRUE;
    case MUIM_Cleanup:
        d = (LIData *)INST_DATA(cl, obj);
        if (d->ehnAdded) {
            DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&d->ehn);
            d->ehnAdded = false;
        }
        break;
    case MUIM_Hide:
        d = (LIData *)INST_DATA(cl, obj);
        d->hover = d->active = false;
        break;
    case MUIM_HandleEvent: {
        struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
        d = (LIData *)INST_DATA(cl, obj);
        if (!im || !d->row) return 0;
        int vl, vt, vr, vb;
        bool vis = lpVisibleRect(obj, vl, vt, vr, vb);
        bool in = vis && im->MouseX >= vl && im->MouseX <= vr && im->MouseY >= vt && im->MouseY <= vb;
        if (im->Class == IDCMP_MOUSEMOVE) {
            if (in != d->hover) { d->hover = in; MUI_Redraw(obj, MADF_DRAWOBJECT); }
            return 0;
        }
        if (im->Class == IDCMP_MOUSEBUTTONS) {
            if (im->Code == SELECTDOWN && in && d->launchable) {
                d->active = true;
                MUI_Redraw(obj, MADF_DRAWOBJECT);
                return MUI_EventHandlerRC_Eat;
            }
            if (im->Code == SELECTUP && d->active) {
                d->active = false;
                MUI_Redraw(obj, MADF_DRAWOBJECT);
                if (in && g_launcherAction) g_launcherAction(d->index, LA_Open);
                return MUI_EventHandlerRC_Eat;
            }
        }
        return 0;
    }
    case MUIM_ContextMenuChoice: {
        d = (LIData *)INST_DATA(cl, obj);
        Object *item = ((struct MUIP_ContextMenuChoice *)msg)->item;
        IPTR action = 0;
        if (item) get(item, MUIA_UserData, &action);
        if (action && g_launcherAction) g_launcherAction(d->index, (int)action);
        return 0;
    }
    case MUIM_Draw:
        return LI_Draw(cl, obj, (struct MUIP_Draw *)msg);
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

static Object *newLauncherItem(int index, const LPRow &row, bool launchable) {
    Object *menu = lpMenu(row, launchable);
    Object *o = (Object *)NewObject(mccLItem->mcc_Class, NULL, MUIA_FillArea, FALSE,
                                    menu ? MUIA_ContextMenu : TAG_IGNORE, (IPTR)menu, TAG_DONE);
    if (!o) { if (menu) MUI_DisposeObject(menu); return nullptr; }
    LIData *d = (LIData *)INST_DATA(mccLItem->mcc_Class, o);
    d->index = index;
    d->row = new LPRow(row);
    d->launchable = launchable;
    d->menu = menu;
    return o;
}

// SearchField: String subclass that draws the xib's placeholder ("Search
// programs") while empty and inactive; Zune's String has no placeholder.
// The NSSearchField magnifier and cancel button are not reproduced.
struct SFData { int dummy; };
static struct MUI_CustomClass *mccSearch;

BOOPSI_DISPATCHER(IPTR, SF_Dispatcher, cl, obj, msg)
{
    if (msg->MethodID == MUIM_Draw) {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) return r;
        STRPTR text = nullptr;
        get(obj, MUIA_String_Contents, &text);
        IPTR activeObj = 0;
        if (_win(obj)) get(_win(obj), MUIA_Window_ActiveObject, &activeObj);
        if ((!text || !*text) && (Object *)activeObj != obj) {
            const char *ph = "Search programs";
            textRGB(obj, g_fonts.system13, _mleft(obj) + 4, baselineIn(_mtop(obj), _mheight(obj), g_fonts.system13),
                    ph, (int)std::strlen(ph), 0x9A9A9A, "placeholder");
        }
        return r;
    }
    if (msg->MethodID == MUIM_GoActive || msg->MethodID == MUIM_GoInactive) {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        MUI_Redraw(obj, MADF_DRAWOBJECT);
        return r;
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

static const AbsItem kLaunchBarItems[] = {{128, 9, 384, 22, FlexMinX | FlexMaxX | FlexTop | FlexBottom}};
static const AbsLayout kLaunchBar = {640, kLPBarH, 160, kLPBarH, MUI_MAXMAX, kLPBarH, kLaunchBarItems, 1};

Object *newLaunchPanel(Object **searchField, Object **list) {
    // StayActive: Return keeps the field focused (NSSearchField stays the
    // first responder); Return never launches anything, as in 2.0-alpha.
    Object *search = (Object *)NewObject(mccSearch->mcc_Class, NULL, MUIA_Frame, MUIV_Frame_String,
                                         MUIA_Font, (IPTR)g_fonts.system13, MUIA_String_MaxLen, 128,
                                         MUIA_String_StayActive, TRUE, MUIA_CycleChain, 1, TAG_DONE);
    Object *bar = (Object *)NewObject(mccPaintGroup->mcc_Class, NULL, PGA_Mode, PM_LaunchBar,
                                      PGA_Layout, (IPTR)&kLaunchBar, Child, (IPTR)search, TAG_DONE);
    Object *lst = (Object *)NewObject(mccLList->mcc_Class, NULL, MUIA_Frame, MUIV_Frame_None,
                                      MUIA_Group_Spacing, 0, MUIA_InnerLeft, 0, MUIA_InnerRight, 0,
                                      MUIA_InnerTop, 0, MUIA_InnerBottom, 0,
                                      Child, (IPTR)(RectangleObject, End), TAG_DONE);
    Object *scroll = ScrollgroupObject, MUIA_Scrollgroup_FreeHoriz, FALSE,
        MUIA_Scrollgroup_Contents, (IPTR)lst, End;
    Object *root = VGroup, MUIA_Group_Spacing, 0, MUIA_InnerLeft, 0, MUIA_InnerRight, 0,
        MUIA_InnerTop, 0, MUIA_InnerBottom, 0,
        Child, (IPTR)bar, Child, (IPTR)scroll, End;
    g_lpRoot = root;
    g_lpList = lst;
    if (searchField) *searchField = search;
    if (list) *list = lst;
    return root;
}

void launchPanelSetRows(Object *list, const std::vector<LPRow> &rows, bool launchable) {
    g_lpRows = rows;
    // The change is bracketed on the panel root too: ExitChange of the
    // Virtgroup alone relayouts only itself (its virtual minimum always
    // fits), so the Scrollgroup would not re-decide its scroll bar.
    if (g_lpRoot) DoMethod(g_lpRoot, MUIM_Group_InitChange);
    DoMethod(list, MUIM_Group_InitChange);
    std::vector<Object *> old;
    Object *cstate = nullptr, *child;
    struct List *children = nullptr;
    get(list, MUIA_Group_ChildList, &children);
    if (children) {
        cstate = (Object *)children->lh_Head;
        while ((child = (Object *)NextObject(&cstate))) old.push_back(child);
    }
    for (Object *o : old) {
        DoMethod(list, OM_REMMEMBER, (IPTR)o);
        MUI_DisposeObject(o);
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        Object *o = newLauncherItem((int)i, rows[i], launchable);
        if (o) DoMethod(list, OM_ADDMEMBER, (IPTR)o);
    }
    // The rows keep their 44 pt height; the space below is panel background.
    DoMethod(list, OM_ADDMEMBER, (IPTR)(RectangleObject, End));
    DoMethod(list, MUIM_Group_ExitChange);
    // A new list starts at its top (the collection view gets a new content array).
    set(list, MUIA_Virtgroup_Top, 0);
    if (g_lpRoot) DoMethod(g_lpRoot, MUIM_Group_ExitChange);
}

void launchPanelSetLaunchable(Object *list, bool launchable) {
    struct List *children = nullptr;
    get(list, MUIA_Group_ChildList, &children);
    if (!children) return;
    Object *cstate = (Object *)children->lh_Head, *child;
    while ((child = (Object *)NextObject(&cstate))) {
        if (OCLASS(child) != mccLItem->mcc_Class) continue;
        LIData *d = (LIData *)INST_DATA(mccLItem->mcc_Class, child);
        if (d->launchable == launchable) continue;
        d->launchable = launchable;
        d->active = false;
        if (d->menu) {
            // "Launch" / "Switch to Drive" follow canOpenItemInDOS:
            // (BXLauncherItem validateMenuItem:).
            Object *item = (Object *)DoMethod(d->menu, MUIM_FindUData, (IPTR)LA_Open);
            if (item) set(item, MUIA_Menuitem_Enabled, launchable);
        }
        MUI_Redraw(child, MADF_DRAWOBJECT);
    }
}

std::vector<LPRowGeom> launchPanelGeometry(Object *list) {
    std::vector<LPRowGeom> out;
    struct List *children = nullptr;
    get(list, MUIA_Group_ChildList, &children);
    struct Window *w = _window(list);
    if (!children || !w) return out;
    Object *cstate = (Object *)children->lh_Head, *child;
    while ((child = (Object *)NextObject(&cstate))) {
        if (OCLASS(child) != mccLItem->mcc_Class) continue;
        LIData *d = (LIData *)INST_DATA(mccLItem->mcc_Class, child);
        int vl, vt, vr, vb;
        bool vis = lpVisibleRect(child, vl, vt, vr, vb);
        out.push_back({d->index, _left(child) - w->BorderLeft, _top(child) - w->BorderTop,
                       _width(child), _height(child), vis && (vb - vt + 1) == _height(child)});
    }
    return out;
}

// ============================================================ classes ====
bool createClasses() {
    mccPaintGroup = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Group, NULL, sizeof(PGData), (APTR)PG_Dispatcher);
    mccLabel   = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL, sizeof(LabelData), (APTR)Label_Dispatcher);
    mccGlyph   = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL, sizeof(GBData), (APTR)GB_Dispatcher);
    mccWelcome = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL, sizeof(WBData), (APTR)WB_Dispatcher);
    mccTab     = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL, sizeof(TTData), (APTR)TT_Dispatcher);
    mccSlider  = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Numeric, NULL, sizeof(SLData), (APTR)SL_Dispatcher);
    mccRender  = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL, sizeof(RVData), (APTR)RV_Dispatcher);
    mccLList   = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Virtgroup, NULL, sizeof(LLData), (APTR)LL_Dispatcher);
    mccLItem   = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL, sizeof(LIData), (APTR)LI_Dispatcher);
    mccSearch  = MUI_CreateCustomClass(NULL, (ClassID)MUIC_String, NULL, sizeof(SFData), (APTR)SF_Dispatcher);
    mccCover   = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL, sizeof(CWData), (APTR)CW_Dispatcher);
    return mccPaintGroup && mccLabel && mccGlyph && mccWelcome && mccTab && mccSlider && mccRender &&
           mccLList && mccLItem && mccSearch && mccCover;
}

void deleteClasses() {
    struct MUI_CustomClass **all[] = {&mccCover, &mccSearch, &mccLItem, &mccLList, &mccRender, &mccSlider, &mccTab,
                                      &mccWelcome, &mccGlyph, &mccLabel, &mccPaintGroup};
    for (auto c : all) if (*c) { MUI_DeleteCustomClass(*c); *c = nullptr; }
}

}  // namespace boxer_ui
