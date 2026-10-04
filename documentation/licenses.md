# Licences and authors

This page lists every component that goes into the BoxerUI program and its
files, who wrote it, and under which licence. Licence texts are in this
repository: `COPYING` (GNU GPL version 2) and the `LICENSES/` directory.
Every package must contain them (see "Distribution checklist").

The project's own licence (2026-10-02): new code written for this port is
GPL-2.0-or-later; files derived from Boxer stay GPL-2.0-only and files
derived from ADBToolkit stay BSD-2-Clause.

## Summary

| Component | Authors | Licence | In the binary | Text |
| --- | --- | --- | --- | --- |
| This port's own code | Tomasz Staniak | GPL-2.0-or-later; Boxer-derived files GPL-2.0-only (per-file SPDX headers) | both ABIs | `COPYING` |
| `src/model/shadowfs.*` (derived from ADBToolkit's ADBShadowedFilesystem) | Alun Bestor; modified by Tomasz Staniak | BSD-2-Clause | both ABIs | `LICENSES/BSD-2-Clause-ADBToolkit.txt` |
| pthread link library (`libpthread.a`) | Szilard Biro, Harry Sintonen, Stefan Franke | zlib | ABIv11 only, statically linked | (no notice required in binaries) |
| Boxer (alunbestor/Boxer at `0062fc18`): DOSBox integration hooks, behaviour and file formats that the port follows, artwork | Alun Bestor and contributors | GPL-2.0 | both ABIs (core, artwork) | `COPYING`, `assets/NOTICE-boxer.txt` |
| DOSBox 0.74 as included in Boxer | The DOSBox Team (below) | GPL-2.0-or-later; `opl.cpp` LGPL-2.1-or-later | both ABIs | `COPYING`, `LICENSES/LGPL-2.1.txt` |
| SDL 2.32.10 | Sam Lantinga and SDL contributors | zlib | Only in an SDL2 build (`BOXER_HOST=sdl2`; on ABIv11 the comparison build). ABIv11: statically linked. Mainline: only a link stub; `sdl2.library` is part of the system and not shipped | `LICENSES/SDL2-zlib.txt` |
| SDL 3.4.12, reduced (audio, timer, threads, joystick; no video or OpenGL), with the AROS port from aros-development-team/contrib a3cb9c4700 | Sam Lantinga and SDL contributors; AROS port by the AROS contributors | zlib | default host layer on ABIv11, statically linked (`scripts/build-sdl3.sh`); not shipped for mainline v1 | `LICENSES/SDL3-zlib.txt` |
| GCC runtime (libstdc++, libgcc) | Free Software Foundation | GPL-3.0 with GCC Runtime Library Exception 3.1 | both ABIs, statically linked | `LICENSES/GCC-exception-3.1.txt` |
| AROS startup code and link libraries | The AROS Development Team | AROS Public License 1.1 | both ABIs (see list below) | `LICENSES/AROS-APL-1.1.txt` |
| Replacement icons (`assets/runtime/replacements/`) | Cole Bemis (Feather: search, maximize-2, lock, unlock) | MIT | both ABIs, embedded in BoxerUI | `LICENSES/Tango-Public-Domain.txt`, `LICENSES/MIT-Feather.txt`, `assets/NOTICE-replacements.txt` |
| BoxerSans font descriptions | Bitstream, Inc. (Vera); description files from AROS; metric value and family name changed by this project | Bitstream Vera Fonts licence | shipped as `Fonts/BoxerSans*.otag`, `.font`; the font outlines are **not** shipped | `LICENSES/Bitstream-Vera.txt` |
| BOXTEST (`tests/fixtures/boxtest`) | Tomasz Staniak | as the project's own code | not in BoxerUI; test fixture only | `COPYING` |

## This port's own code

`src/`, `scripts/`, `tools/`, `tests/`, `patches/`,
`third_party/sdl3-aros/build-sdl3-ctl.py` and the documentation are written
for this project. Some files reproduce the
behaviour of Boxer classes and follow their structure, for example the
write-shadowing filesystem (after Boxer's `ADBShadowedFilesystem`) and the
emulator run sequence (after `BXEmulator`). Such files are derived from
Boxer and can only be distributed under GPL-2.0, Boxer's licence.

Since 2026-10-02: original files under GPL-2.0-or-later; files
derived from Boxer source under GPL-2.0-only, with Boxer's copyright line
added to their header. The program as a whole is then distributed under
GPL-2.0, as Boxer is.

## Boxer

"Boxer is copyright (c) 2013 Alun Bestor and contributors. Boxer is released
under the GNU General Public License 2.0" (Boxer help, `legalese.html`, at
commit `0062fc18e154cde6cef3bf3329be4b0658597e31` of
<https://github.com/alunbestor/Boxer>). Boxer's source file headers say the
same, without an "or later" clause. Boxer ships no `COPYING` file. Its
headers point to the GPL 2.0 text, which is `COPYING` here.

Boxer's own artwork and sounds are used under the same licence. The files,
their conversions and the required notice are listed in `assets/README.md`
and `assets/NOTICE-boxer.txt`. These are not covered and not shipped: the
DOSBox logo (used by Boxer with permission), Apple system images, Sparkle,
BGHUDAppKit and Joypad images, sample games, and the files that the asset
manifest marks as unclear, except four restored on 2026-10-02
(`gamefolder.icns`, `prompt.icns`, `import.png`, `Game.png`): these are used
under Boxer's GPL-2.0 like its other artwork, with their resemblance to Apple
artwork recorded as an open question. For the Apple system images BoxerUI
uses Feather (MIT) icons, listed in `assets/replacement-assets.json`.

## DOSBox 0.74

The emulator core is the DOSBox 0.74 tree that Boxer carries, with Boxer's
changes and this project's patches (`patches/boxer/`). Source headers:
"Copyright (C) 2002-2010 The DOSBox Team", GPL version 2 "or (at your
option) any later version".

The DOSBox Team, per the 0.74 release's `AUTHORS` file: Sjoerd v.d. Berg
(harekiet), Peter Veenstra (qbix79), Ulf Wohlers (finsterr), Tommy Frössman
(fanskapet), Dean Beeler (canadacow), Sebastian Strohhäcker (c2woody),
Ralf Grillenberger (h-a-l-9000).

DOSBox's `THANKS` file credits, among others: Vlad R. (vdmsound) for Sound
Blaster information; Tatsuyuki Satoh (MAME team) for the FM emulator; Jarek
Burczynski for the OPL3 emulator; Ken Silverman for his OPL2 emulator;
the Bochs and DOSemu projects; FreeDOS; crazyc, gulikoza and M-HT for the
dynrec core; Ido Beeri for the icon; and the GOG team for the splash screen.

Files under another licence in the compiled set:
- `src/hardware/opl.cpp`: LGPL-2.1-or-later, "Originally based on
  ADLIBEMU.C … by Ken Silverman, Copyright (C) 1998-2001 Ken Silverman".
  LGPL-2.1 section 3 allows it to be distributed under the GPL as part of
  this program.
- `src/debug/debug_disasm.cpp`: based on "2asm" as adapted for MAME,
  under the GNU GPL.
- `src/hardware/parport/` (printer emulation added by Boxer, based on
  gulikoza's DOSBox megabuild): GPL-2.0-or-later headers.

## SDL 2

Version 2.32.10 (`SDL_version.h` in both SDKs). Copyright (C) 1997-2025
Sam Lantinga. zlib licence, text in `LICENSES/SDL2-zlib.txt`.
- ABIv11: the SDK's `libSDL2.a` is linked statically into BoxerUI.
- Mainline v1: the SDK's `libSDL2.a` is a link stub that opens the system's
  `sdl2.library` at start. No SDL code is in the binary, and the library is
  not shipped with BoxerUI.

## Libraries linked into the binary

Determined from the link (`-Wl,-t,-t`) of the current `scripts/build-ui.sh`
on 2026-10-02. This list must be redone when the link line changes.

| Archive | ABIv11 | Mainline v1 | What it is | Licence |
| --- | --- | --- | --- | --- |
| `libSDL2.a` | 94 objects (the library) | 14 objects (stub for `sdl2.library`) | SDL 2.32.10 | zlib |
| `libGL.a` | 7 objects (stub for `gl.library`) | not linked in | link stub only, no Mesa code | AROS APL 1.1 |
| `libstdc++.a`, `libgcc.a` | yes | yes | GCC 10.5 runtime | GPL-3.0 + GCC RLE 3.1 |
| `startup.o`, `libautoinit.a`, `libamiga.a`, `libmui.a`, per-library stubs (exec, graphics, intuition, diskfont, utility, …) | yes | yes | AROS start-up code and library stubs | AROS APL 1.1 |
| `libcrt.a`, `libstdlib.a`, `libm.a` | stubs for `crt.library` (+ ctype table) | no | ABIv11 C runtime interface; the runtime itself is a shared system library | AROS APL 1.1 |
| `libcrtprog.a` | yes (`_exit`, abort, atexit, startup) | no | ABIv11 program glue | AROS APL 1.1 |
| `libpthread.a` | yes (pthreads implementation) | no | ABIv11 pthreads | zlib (Szilard Biro et al.) |
| `libstdc.a`, `libstdcio.a`, `libposixc.a`, `liblibinit.a` | no | yes | mainline stubs for the system C libraries | AROS APL 1.1 |

The GCC Runtime Library Exception lets a program compiled with GCC be
distributed under its own terms, together with the runtime parts linked into
it. It places no obligations on this program's licence.

The AROS libraries named above are system components: start-up code and
stubs that open shared libraries at run time, as every AROS program
contains. The system libraries they open (for example `sdl2.library`,
`gl.library`, `muimaster.library`) are part of the operating system and are
not shipped with BoxerUI.

## Fonts

BoxerUI draws its interface with Bitstream Vera Sans. It ships two font
**descriptions**, `Fonts/BoxerSans.otag` and `Fonts/BoxerSansBold.otag`,
made by `tools/make-fonts.py` from AROS's `Vera Sans.otag` and `Vera Sans
Bold.otag`. They are byte copies with one value changed: the vertical metric
source becomes the font's bounding box. The font outlines
(`Fonts:TrueType/VeraSans.ttf`, `VeraSansBold.ttf`) are not shipped. The
descriptions use the copies that AROS installs. As the Bitstream Vera
licence requires for modified Font Software, the family name inside the
descriptions is "BoxerSans", without "Bitstream" or "Vera"; the only
remaining "Vera" is the path of the unmodified system font they refer to.

Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved. Bitstream Vera is
a trademark of Bitstream, Inc. The licence text is in
`LICENSES/Bitstream-Vera.txt` and must accompany the description files.

## Distribution checklist

For every binary package, per ABI:

1. Include `COPYING`, the whole `LICENSES/` directory (including
   `BSD-2-Clause-ADBToolkit.txt`, which the BSD licence requires in the
   documentation of binary distributions), `assets/NOTICE-boxer.txt` as
   `NOTICE-boxer.txt`, this file as `licenses.md`, and
   `Fonts/Bitstream-Vera.txt`. `scripts/check-package-notices.sh <abi>
   <package dir>` checks a staged package for these files.
2. Provide the complete corresponding source of that exact build (GPL-2.0
   section 3), preferably as an archive next to the binary (section 3(a)).
   A link to the upstream repository alone is not enough. The archive holds:
   - this repository at the release tag (`src/`, `scripts/`, `tools/`,
     `patches/boxer/` with its `series`, `assets/source/`, `tests/fixtures/`);
   - the Boxer source at the pinned commit (`upstreams.json`), including its
     DOSBox tree, because the build compiles from that tree after applying
     the patches;
   - the scripts that apply the patches and build (`scripts/`), and the
     generators for embedded or generated files (`tools/png2inc.py`,
     `tools/make-fonts.py`, `scripts/boxer-assets.py`);
   - the toolchain and SDK identification recorded in the build's
     `BUILDINFO.txt`.
   If the archive is not shipped with the binary, a written offer valid for
   three years is needed instead (section 3(b)), as `SOURCE-OFFER.txt`.
3. ABIv11 (SDL linked statically): keep the zlib notice of the SDL the
   binary links, `LICENSES/SDL3-zlib.txt` for the default SDL3 build or
   `LICENSES/SDL2-zlib.txt` for an SDL2 build (`check-package-notices.sh`
   reads the host from the package's `BUILDINFO.txt`). Do not present SDL as
   our work. The SDL3 build carries the AROS port and a local patch
   (`third_party/sdl3-aros/`, recorded in `build/<abi>/sdl3/BUILDINFO.txt`);
   if that altered source is ever distributed,
   mark it as altered. The zlib licence does not
   require shipping SDL's source.
4. LGPL code (`opl.cpp`) is conveyed under the GPL as part of the program.
   Its source is in the source archive. No other LGPL library is linked
   (SDL 1.2, SDL_net, SDL_sound and Munt from Boxer's build are not used).
5. AROS link code: unmodified. Name the AROS release or SDK in the package
   notes. The AROS sources are public.
6. Fonts: ship `LICENSES/Bitstream-Vera.txt` with `Fonts/`. Never ship the
   Vera `.ttf` files under a changed name.
7. Never include: Dune or other game data, the DOSBox logo, Apple images, or
   assets marked (c) or (d) in `assets/boxer-assets-manifest.json`.
8. List the package's files against this page before release, and update
   this page when a component is added or the link line changes.
