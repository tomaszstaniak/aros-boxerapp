# Boxer for AROS

An AROS port of [Boxer](https://github.com/alinebee/Boxer), the DOS game
emulator front end by Alun Bestor, with Boxer's own DOSBox 0.74 core.

**This is a first experimental preview, not a full Boxer port.** It
follows the original's look and gamebox format, but many of Boxer's
features are not there yet, and there are known bugs (below). Use it on a
test installation of AROS, not on the system you depend on.

## Targets

- **Confirmed:** AROS x86_64 ABIv11 (AROS One and other ABIv11
  distributions). Tested under QEMU.
- **Builds, not tested:** mainline AROS x86_64 (ABI v1).

ABIv11 and mainline binaries are different files and do not run on each
other. No DOS games or game data are included.

## What works in this preview

On ABIv11, this sequence was checked end to end: import a DOS game (run its
installer, or copy it in) into a gamebox, launch it, save in the game, quit,
reopen the gamebox and load the save.

## Known issues

- Loading a save from Tyrian's title screen froze the game twice. Loading
  it through the in-game Options menu worked; that is the path that was
  checked, not a workaround known to avoid every freeze.
- After such a freeze, quitting caused a DOSBox assertion (`CPU_IRET`,
  `cpu.cpp:901`) and, separately, a crash in the SDL audio thread; neither
  is explained yet. **Closing a hung session can crash the whole AROS
  system.**
- A session froze twice when a game installer exited, on a machine with
  live audio output; eight later attempts did not freeze.
- Tyrian's first title menu may ignore keys and clicks until its demo has
  run once.
- Not tested in this preview's acceptance runs: paths that contain spaces,
  importing from LhA archives, and keeping icons that a game folder already
  has.

## Install

1. Unpack the archive, for example to `SYS:` or `Work:`
   (`UnZip boxer-<version>.x86_64-aros-v11.zip -d Work:`). This makes the
   drawer `Boxer`.
2. Open the drawer and double-click `Install-Assign`. It assigns `Boxer:`
   to the drawer and adds a marked block to `S:User-Startup` so the assign
   is set at every boot. Game icons need `Boxer:`.
3. Double-click `BoxerUI`. On the first start it proposes a folder for your
   games and one for their saved games. From a Shell, use `Stack 65536`
   first.

`ReadMe.txt` in the drawer has the details.

## Uninstall

In a Shell, `CD` to the Boxer drawer and run `Execute Install-Assign
REMOVE`, then delete the lines from `;BEGIN Boxer` to `;END Boxer` in
`S:User-Startup` if you want them gone, and delete the drawer. Your games
folder, its `Boxer Data` (saved games) and `ENVARC:Boxer` are separate;
delete them only if you no longer want them.

## Reporting a problem

BoxerUI logs to `RAM:boxerui.log` by default, which is lost when AROS
restarts or crashes. To keep it, start BoxerUI with a log in a writable
drawer, for example the ToolType `LOG=Work:boxerui.log` or, from a Shell,
`BoxerUI LOG Work:boxerui.log` (the Boxer drawer itself may be read-only).

Please open an issue with:

- the log file;
- what you did just before the problem (screen, button or key);
- the AROS version and distribution, and the ABI (ABIv11 or mainline);
- the amount of RAM;
- real hardware (which) or an emulator (which, and its settings);
- the BoxerUI sha256 from `SHA256SUMS` in the drawer.

## Building from source

The build runs on a macOS host with an AROS cross toolchain (GCC 10.5) and
SDK for each ABI. In short:

```sh
cp local.env.example local.env     # fill in toolchain and SDK paths
scripts/bootstrap.sh               # fetch Boxer at the pinned commit, apply patches/boxer
scripts/build-sdl3.sh abiv11
scripts/build-core.sh abiv11
scripts/build-ui.sh abiv11
scripts/make-package.sh abiv11
```

Source pins, requirements, variables and tests:
[documentation/development.md](documentation/development.md).
Patch workflow: [documentation/patching.md](documentation/patching.md).

## Licence and credits

Distributed under the GNU General Public License version 2 (`COPYING`).
Code written for this port is GPL-2.0-or-later; files derived from Boxer are
GPL-2.0-only; files derived from ADBToolkit are BSD-2-Clause. Each file
carries an SPDX header. All components, authors and licence texts are listed
in [documentation/licenses.md](documentation/licenses.md) and `LICENSES/`.

- **Boxer** is copyright (c) 2013 Alun Bestor and contributors, released
  under the GNU General Public License 2.0. This port follows Boxer's
  behaviour, file formats and DOSBox integration, and uses Boxer's artwork
  (`assets/NOTICE-boxer.txt`).
- **DOSBox** 0.74 is copyright (C) 2002-2010 The DOSBox Team, GPL version 2
  or later.
- **SDL** is by Sam Lantinga and SDL contributors (zlib); the AROS SDL3 port
  is by the AROS contributors.
- Replacement icons: Feather by Cole Bemis (MIT,
  `assets/NOTICE-replacements.txt`). Fonts: Bitstream Vera descriptions
  (`LICENSES/Bitstream-Vera.txt`).

AROS port: Tomasz Staniak.
