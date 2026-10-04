#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tomasz Staniak
"""Classify Boxer's image/sound assets, copy the category-(a) originals and
generate AROS runtime versions.

Why a script: the classification, the copy and the conversion must be
reproducible from the pinned upstream checkout alone, and the manifest must
record exactly which tool produced each runtime file.

Usage: scripts/boxer-assets.py   (macOS host: needs sips, iconutil and afconvert)
       scripts/boxer-assets.py --restore-originals
         only (re)does the four restored originals (RESTORED below) and their
         manifest entries, leaving every other output untouched.
Inputs: upstream/boxer (read-only, pinned). Outputs: assets/source/boxer/,
assets/runtime/boxer/, assets/boxer-assets-manifest.json.
"""
import hashlib, json, os, platform, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UP = os.path.join(ROOT, "upstream", "boxer")
SRC = os.path.join(ROOT, "assets", "source", "boxer")
RUN = os.path.join(ROOT, "assets", "runtime", "boxer")
EXT = re.compile(r"\.(png|jpe?g|gif|tiff?|icns|pdf|aiff?|wav|caf|mp3|ttf|otf|ico|svg|psd)$", re.I)
GPL = "GPL-2.0 (legalese.html: 'Boxer is released under the GNU General Public License 2.0')"

# Concrete reasons only. Anything not matched here is category (a).
RULES = [
    # (c) third party, with their own terms
    (r"^Frameworks/BGHUDAppKit", "c", "BGHUDAppKit (BSD-3, Tim Davis); IB plugin images, not used at runtime"),
    (r"^Frameworks/Sparkle", "c", "Sparkle (MIT, Andy Matuschak); updater not ported (D3)"),
    (r"^Resources/Brand(Watermark)?\.png$", "c", "DOSBox cross logo by Robert Hagenstr\u00f6m, 'used with permission' (acknowledgements.html); permission was given to Boxer, not to this port"),
    (r"^Resources/Sample Game Icons/|^Resources/Sample Games/.*\.boxer/", "c", "shareware game data/cover art (iD/Apogee, Epic, Origin, MicroProse); Boxer did not relicense it"),
    (r"joypad-logo\.png$", "c", "Joypad app logo (third-party product brand); Joypad SDK not ported (D3)"),
    # (d) unclear, each with a concrete reason
    (r"^Resources/(gamefolder|prompt)\.icns$", "d", "artwork appears built on Apple system icons (Finder folder body / Terminal bezel); Boxer's GPL grant cannot cover Apple's part"),
    (r"^Resources/(executable|package)\.icns$", "d", "page base resembles Apple's generic document icon (page curl); lower confidence than gamefolder/prompt"),
    (r"^Resources/(import|Game)(@2x)?\.png$", "d", "rainbow disc element resembles the Mac OS X optical-disc icon; box/C:\\ is Boxer's"),
    (r"shared/images/(gamebox-128|importer-(128|32)|games-folder-128-trimmed|display-32|icon|boxer-(16|128))\.png$", "d", "help-page renderings of icons; some derive from the (d) icons above; help is not ported"),
    (r"^Resources/(MT32|CM32L)(@2x)?\.png$|shared/images/mt32\.png$", "d", "depicts Roland hardware with Roland name/logo (trademark)"),
    (r"^Resources/(CHFlightstickPro|ThrustmasterFCS)(@2x)?\.png$|shared/images/(thrustmaster-128|flightstick-pro-128|360-controller)\.png$", "d", "depicts a named third-party product (CH, Thrustmaster, Microsoft); trademark/logo check needed"),
    (r"^Resources/Gallery(HQx|MAME|Original|TVScanlines)\.png$|shared/images/(gallery-hqx|crt|game-shelf)\.(png|jpg)$", "d", "contains DOS game pixel art of unidentified origin"),
    (r"Boxer\.help/.*English\.lproj/images/|Help\.help/.*lproj/images/|shared/images/(application|email|site)-popout\.png$|shared/images/(info|reveal|topic-arrow|keyboard|fast-forward-key|play-pause-key|add-drive-button|bundle-drive-button|eject-drive-button|insert-drive-button|coverart-128|joystick-disabled-128|racing-wheel-128|standard-joystick-128)\.png$",
     "d", "help-book images: screenshots with macOS UI/game content or renderings of other assets; help book not ported, not needed"),
]

# Apple system images referenced by name; no file exists in the repo.
SYSTEM = ["NSActionTemplate", "NSAddTemplate", "NSApplicationIcon", "NSFolder",
          "NSFollowLinkFreestandingTemplate", "NSLockLockedTemplate", "NSLockUnlockedTemplate",
          "NSPreferencesGeneral", "NSRefreshTemplate", "NSRemoveTemplate",
          "NSRevealFreestandingTemplate", "NSRightFacingTriangleTemplate",
          "NSStopProgressFreestandingTemplate"]

# Category (d) originals restored on 2026-10-02 (assets/README.md,
# "Restored originals"): Boxer's own artwork is preferred, and
# the Apple resemblance is an open question, not a confirmed bar. Each source
# already has a representation at the exact size the UI draws, so nothing is
# resampled: icns -> the 128x128 member extracted by iconutil, PNGs copied.
RESTORED = [
    ("Resources/gamefolder.icns", "gamefolder-128.png", 128,
     "Welcome window, 'Browse your games' button (src/ui/gfx.cpp welcomeImage, WelcomeArt::GameFolder)"),
    ("Resources/prompt.icns", "prompt-128.png", 128,
     "Welcome window, 'Open a DOS prompt' button (WelcomeArt::Prompt)"),
    ("Resources/import.png", "import.png", 128,
     "Welcome window, 'Import a new game' button (WelcomeArt::Import)"),
    ("Resources/import@2x.png", None, None,
     "source only: 256 px Retina variant; the UI draws 128 px, so import.png (exact size) is used"),
    ("Resources/Game.png", "Game.png", 32,
     "Inspector 'Gamebox' tab icon (Inspector.xib toolbarItem image=\"Game\"; src/ui/gfx.cpp tabIcon, TabIcon::Game)"),
    ("Resources/Game@2x.png", None, None,
     "source only: 64 px Retina variant; the UI draws 32 px, so Game.png (exact size) is used"),
]

def restore(rel, out_name, px):
    """Copy one restored original; return its runtime list."""
    src = os.path.join(UP, rel)
    dst_src = os.path.join(SRC, rel); os.makedirs(os.path.dirname(dst_src), exist_ok=True)
    shutil.copyfile(src, dst_src)
    if out_name is None:
        return []
    dst = os.path.join(RUN, out_name)
    if rel.endswith(".icns"):
        tmp = os.path.join(RUN, ".iconset-tmp.iconset")
        shutil.rmtree(tmp, ignore_errors=True)
        subprocess.run(["iconutil", "-c", "iconset", "-o", tmp, src], check=True, capture_output=True)
        shutil.copyfile(os.path.join(tmp, f"icon_{px}x{px}.png"), dst)
        shutil.rmtree(tmp)
        cmd = f"iconutil -c iconset {rel}; copy icon_{px}x{px}.png unchanged (exact {px} px representation, no resampling)"
    else:
        shutil.copyfile(src, dst)
        cmd = f"copy (already PNG at the exact {px} px drawn size, no resampling)"
    w = sh("sips", "-g", "pixelWidth", "-g", "pixelHeight", "-g", "hasAlpha", dst)
    assert f"pixelWidth: {px}" in w and f"pixelHeight: {px}" in w and "hasAlpha: yes" in w, w
    return [{"file": os.path.relpath(dst, ROOT), "sha256": sha(dst), "command": cmd}]

def restored_entry(e, head):
    for rel, out_name, px, used in RESTORED:
        if e["path"] == rel:
            e.update(restored="2026-10-02", upstream_revision=head, licence=GPL,
                     notice="Boxer is copyright (c) 2013 Alun Bestor and contributors.",
                     copied_to=os.path.relpath(os.path.join(SRC, rel), ROOT),
                     runtime=restore(rel, out_name, px), used_in=used,
                     open_question="category (d) note kept as history: visual resemblance only, not a confirmed prohibition"
                     + ("; the rainbow disc resembles Apple's optical-disc icon" if "import" in rel or "Game" in rel else ""))
    return e

def sh(*a):
    return subprocess.run(a, check=True, capture_output=True, text=True).stdout

def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()

def classify(rel):
    for pat, cat, why in RULES:
        if re.search(pat, rel):
            return cat, why
    return "a", "Boxer's own; covered by the project GPL-2.0 declaration, no exception listed"

def runtime(rel, src):
    """Return list of (output, command) for one category-(a) file."""
    base, ext = os.path.splitext(os.path.basename(rel)); ext = ext.lower()
    out = []
    def run(cmd, dst):
        subprocess.run(cmd, check=True, capture_output=True)
        # Paths relative to the project root, so the manifest names no host directory.
        rel_cmd = [os.path.relpath(c, ROOT) if os.path.isabs(c) else c for c in cmd]
        out.append((os.path.relpath(dst, ROOT), " ".join(f"'{c}'" if " " in c else c for c in rel_cmd)))
    if ext == ".png":
        dst = os.path.join(RUN, base + ".png"); shutil.copyfile(src, dst)
        out.append((os.path.relpath(dst, ROOT), "copy (already PNG)"))
    elif ext in (".jpg", ".jpeg"):
        dst = os.path.join(RUN, base + ".png")
        run(["sips", "-s", "format", "png", src, "--out", dst], dst)
    elif ext == ".icns":
        for px in (32, 128, 512):
            dst = os.path.join(RUN, f"{base}-{px}.png")
            run(["sips", "-s", "format", "png", "-Z", str(px), src, "--out", dst], dst)
    elif ext == ".pdf":
        w = float(re.search(r"pixelWidth: ([\d.]+)", sh("sips", "-g", "pixelWidth", "-g", "pixelHeight", src)).group(1))
        h = float(re.search(r"pixelHeight: ([\d.]+)", sh("sips", "-g", "pixelHeight", src)).group(1))
        for scale, suffix in ((1, ""), (2, "@2x")):
            dst = os.path.join(RUN, f"{base}{suffix}.png")
            run(["sips", "-s", "format", "png", "-Z", str(round(max(w, h) * scale)), src, "--out", dst], dst)
    elif ext in (".aiff", ".aif"):
        dst = os.path.join(RUN, base + ".wav")
        run(["afconvert", "-f", "WAVE", "-d", "LEI16", src, dst], dst)
    return out

def main():
    head = sh("git", "-C", UP, "rev-parse", "HEAD").strip()
    files = [f for f in sh("git", "-C", UP, "ls-files").splitlines() if EXT.search(f)]
    for d in (SRC, RUN):
        shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    # Licence evidence travels with the copies.
    for doc in ("legalese.html", "acknowledgements.html"):
        rel = "Resources/Boxer.help/Contents/Resources/English.lproj/pages/" + doc
        dst = os.path.join(SRC, rel); os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.join(UP, rel), dst)
    entries = []
    for rel in sorted(files):
        src = os.path.join(UP, rel)
        cat, why = classify(rel)
        e = {"path": rel, "sha256": sha(src), "category": cat, "reason": why}
        if cat == "a":
            dst = os.path.join(SRC, rel); os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
            e.update(licence=GPL, notice="Boxer is copyright (c) 2013 Alun Bestor and contributors.",
                     copied_to=os.path.relpath(dst, ROOT),
                     runtime=[{"file": f, "sha256": sha(os.path.join(ROOT, f)), "command": c}
                              for f, c in runtime(rel, src)])
        entries.append(restored_entry(e, head))
    tools = {"host": f"macOS {platform.mac_ver()[0]} {platform.machine()}",
             "sips": sh("sips", "--version").strip(),
             "afconvert": "afconvert from the same macOS (no version flag)",
             "iconutil": "iconutil from the same macOS (no version flag)",
             "python": sys.version.split()[0]}
    counts = {c: sum(1 for e in entries if e["category"] == c) for c in "acd"}
    json.dump({"upstream": "alunbestor/Boxer", "commit": head, "generated_by": "scripts/boxer-assets.py",
               "tools": tools, "counts": counts, "apple_system_images_by_name": SYSTEM,
               "assets": entries}, open(os.path.join(ROOT, "assets", "boxer-assets-manifest.json"), "w"),
              indent=1, ensure_ascii=False)
    print(counts, "system names:", len(SYSTEM))

def restore_only():
    head = sh("git", "-C", UP, "rev-parse", "HEAD").strip()
    p = os.path.join(ROOT, "assets", "boxer-assets-manifest.json")
    m = json.load(open(p))
    assert m["commit"] == head, "upstream moved; rerun the full script"
    m["tools"]["iconutil"] = "iconutil from the same macOS (no version flag)"
    m["entries_restored_by"] = "scripts/boxer-assets.py --restore-originals (2026-10-02)"
    m["assets"] = [restored_entry(e, head) for e in m["assets"]]
    json.dump(m, open(p, "w"), indent=1, ensure_ascii=False)
    print("restored:", [r[0] for r in RESTORED])

if __name__ == "__main__":
    restore_only() if sys.argv[1:] == ["--restore-originals"] else main()
