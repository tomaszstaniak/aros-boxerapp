# Boxer for AROS

An AROS port of [Boxer](https://github.com/alinebee/Boxer), the DOS game
emulator front end by Alun Bestor, with Boxer's own DOSBox 0.74 core.

It brings Boxer's gamebox workflow and interface to AROS using Zune.
**This is a first experimental preview.** It provides
the foundation for the port; it is not yet the complete Boxer experience.

## What this preview can do

Import a game folder, run its installer or copy a ready-to-play game,
choose its startup program, and launch it from its icon. You can play, save
using the game's own save system, close the session, and reopen the game to
continue. Game saves and other changes made during play are stored
separately in **Boxer Data**, so the imported gamebox stays unchanged.

Music, sound effects, volume control and the basic import-to-play workflow
have been tested. No games are included; bring your own DOS games.

## Supported system

- **Tested:** AROS x86_64 ABIv11 (AROS One), under QEMU. Other ABIv11
  distributions and real hardware have not been tested.
- **Builds, not tested:** mainline AROS x86_64 (ABI v1), an intended target.

ABIv11 and mainline binaries are different files and do not run on each
other.

## Download

Get the archive from the
[releases page](https://github.com/tomaszstaniak/aros-boxerapp/releases)
(currently [0.1.0 preview 1](https://github.com/tomaszstaniak/aros-boxerapp/releases/tag/v0.1.0-preview1),
x86_64 ABIv11). Checksums are listed with each release.

## Before testing

**Use a separate test installation or a copy of your AROS system.** This is
an early preview: sessions can freeze, including when leaving a game
installer, and closing a hung session can crash AROS. These problems are
still under investigation. Game compatibility is not yet broadly tested.
Not yet tested: paths containing spaces, importing from LhA archives, and
keeping icons a game folder already has.

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

Report problems through the
[issue tracker](https://github.com/tomaszstaniak/aros-boxerapp/issues).
Hardware testing and feedback are welcome.

BoxerUI logs to `RAM:boxerui.log` by default, which is lost when AROS
restarts or crashes. To keep it, start BoxerUI with a log in a writable
drawer, for example the ToolType `LOG=Work:boxerui.log` or, from a Shell,
`BoxerUI LOG Work:boxerui.log` (the Boxer drawer itself may be read-only).

Please include:

- which game you tried, what worked, and the steps that led to the problem;

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
