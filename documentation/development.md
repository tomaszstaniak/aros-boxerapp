# Development

How to build Boxer for AROS from a clone of this repository.

## Targets

| Target | Status |
| --- | --- |
| AROS x86_64 ABIv11 | first target; built, packaged and tested on AROS One under QEMU (other ABIv11 distributions and real hardware not tested) |
| AROS x86_64 mainline (ABI v1) | builds and links; the package is not tested |
| AROS AArch64 | planned; ABI, toolchain and SDK not selected |

ABIv11 and mainline binaries are different files and do not run on each
other. Every build output is kept per ABI under `build/<abi>/`.

## Source pins

| Component | Pin | Where |
| --- | --- | --- |
| Boxer | <https://github.com/alinebee/Boxer> commit `0062fc18e154cde6cef3bf3329be4b0658597e31` (master, 2017-03-18, Boxer 2.0.0-alpha line) | `upstreams.json`; checked out by `scripts/bootstrap.sh` |
| DOSBox | DOSBox 0.74 as modified and vendored in the Boxer tree (`DOSBox/`), plus `patches/boxer/series` | inside the Boxer pin |
| SDL3 (default host layer on ABIv11) | SDL 3.4.12 release tarball, sha256 `f07b958a9ac5020fb7a44cadb957f658b2149c3c8abb4f63145fac9303249db7`, with the AROS port from aros-development-team/contrib `a3cb9c4700a81c9d14ebdc8741c932825c4429f0` and one local patch | `third_party/sdl3-aros/` ([README](../third_party/sdl3-aros/README.md)); tarball fetched by `scripts/build-sdl3.sh` |
| SDL2 (comparison host layer; default on mainline) | the SDK's SDL 2.32.10 | AROS SDK |

The Boxer pin has no submodules: DOSBox and all frameworks are vendored.
This port does not use Boxer's Objective-C code; it reimplements Boxer's
model and UI in C++17 with Zune (`src/`) and builds Boxer's DOSBox core.

## Requirements

Build host: macOS (the scripts use BSD `stat -f` and `shasum`; asset
regeneration uses `sips`, `afconvert` and `iconutil`). Other hosts are not
tested. Also needed: `bash`, `git`, `python3`, `curl`, `patch`, a host C++17
compiler for the unit tests, and `zip` (or a writing LHa, see
`packaging/README.md`).

Per ABI, a matching AROS cross toolchain and SDK:

- **ABIv11**: `x86_64-aros-gcc`/`g++` 10.5.0 built for ABIv11 and the
  ABIv11 SDK (`include/`, `lib/`) from the same AROS ABIv11 build
  (deadwood2/AROS, ABIv11 branch). Tested with the 2026.09 ABIv11 SDK.
- **mainline v1**: `x86_64-aros-gcc`/`g++` 10.5.0 from a mainline AROS
  build (aros-development-team/AROS) and that build's `Developer/`
  directory as the SDK. Mainline's GCC 10.5 libgcc never fills
  `dwarf_reg_size_table`, so every C++ `throw` aborts;
  `scripts/build-unwind-fix.sh` rebuilds `unwind-dw2.o` without that guard
  from the build's configured GCC tree (`AROS_V1_GCC_BUILD`), and
  `build-ui.sh`/`build-smoke.sh` link it automatically.
- `AROS_ISO_FONTS`: the `Fonts/` directory of an AROS ISO (source of the
  Vera Sans `.otag` descriptions that `tools/make-fonts.py` adapts).

## Configuration

Copy `local.env.example` to `local.env` (ignored by Git) and fill in the
paths. Values already in the environment take precedence; relative values
resolve from the project root. `scripts/env.sh <abiv11|mainline-v1>` loads
them for every script.

| Variable | Meaning |
| --- | --- |
| `AROS_V11_TOOLCHAIN`, `AROS_V11_SDK` | ABIv11 toolchain directory (contains `x86_64-aros-g++`) and SDK |
| `AROS_V1_TOOLCHAIN`, `AROS_V1_SDK` | the same for mainline |
| `AROS_ISO_FONTS` | an AROS ISO's `Fonts/` directory |
| `AROS_TOOLCHAIN_MOUNT` | optional: a directory that must exist before linking (some toolchains have their build tree's path built into `collect-aros`) |
| `AROS_V1_GCC_BUILD` | mainline only: `<aros-build>/bin/<host>` of the mainline build |
| `AROS_V11_SRC`, `AROS_V1_SRC` | optional: AROS source checkouts, recorded in a package's `BUILDINFO.txt` |
| `SDL3_TARBALL` | optional: local `SDL3-3.4.12.tar.gz` (default `deps/src/`, downloaded when missing) |
| `BOXER_HOST` | `sdl3` (default on ABIv11) or `sdl2` (default on mainline) |

## Build

```sh
scripts/bootstrap.sh               # clone Boxer at the pin into upstream/boxer,
                                   # create work/boxer with patches/boxer/series applied
scripts/build-sdl3.sh abiv11       # static reduced SDL3 -> build/abiv11/sdl3/
scripts/build-core.sh abiv11       # DOSBox core -> build/abiv11/core-sdl3/libboxer-dosbox.a
scripts/build-ui.sh abiv11         # host unit tests, then BoxerUI -> build/abiv11/ui-sdl3/
scripts/make-package.sh abiv11     # drawer, archive and checksums -> build/abiv11/package-sdl3/
```

For mainline replace `abiv11` with `mainline-v1` (SDL2 by default; the
SDL3 build links but has not been run there). Each step writes a
`BUILDINFO.txt` with compiler, SDK and input hashes. BoxerUI records its
build date and time, so two builds of the same source differ in those
bytes.

- `build-core.sh` compiles the source set of the Boxer target's Sources
  phase in `Boxer.xcodeproj` (`tools/xcode-sources.py`). It first runs
  `scripts/reproduce --source-only` and refuses to build when `work/boxer`
  differs from the upstream pin plus the patch series, including unsaved or
  staged changes.
- `build-ui.sh` runs the host unit tests of `src/ui/` first, embeds Boxer's
  artwork from `assets/runtime/` (`tools/png2inc.py`), builds the private
  font descriptions, links, and rejects the binary if `nm` reports any
  undefined symbol (`scripts/check-undefined.sh`; `collect-aros` can exit 0
  with unresolved symbols). The binary is not stripped: a full strip breaks
  AROS x86_64 relocations.
- `build-smoke.sh` builds `boxer-core-smoke`, a Shell program that runs the
  core without the UI.
- `make-package.sh` refuses to package when the binary does not match its
  `BUILDINFO.txt`, or when a source file has changed since the build. It
  includes the complete corresponding source as an archive. Details:
  [packaging/README.md](../packaging/README.md).

## Tests on the build host

```sh
tests/model/run.sh                         # gamebox, plist, data locations, shadow filesystem
tests/patch-tooling/run.sh                 # bootstrap/save-patch/reproduce scenarios
python3 tests/build-tooling/test_depcheck.py
```

`build-ui.sh` also runs `src/ui/tests/ui_logic_test.cpp` and
`src/ui/tests/launchpanel_test.cpp`. `tests/fixtures/boxtest/` is a small
DOS test program (NASM source and expected logs) for runs on AROS;
`tests/fixtures/gamebox/` makes synthetic test gameboxes around it. No game
data is part of this repository.

## Running on AROS

Install the package as its `ReadMe.txt` describes. From a Shell (stack
65536):

```
BoxerUI                                          ; the welcome window
BoxerUI GAMEBOX <path> DATA <dir> CONFDIR <dir>  ; open one gamebox
BoxerUI SESSION dos CONFDIR <dir>                ; a plain DOS prompt
BoxerUI LOG <file>                               ; log somewhere other than RAM:boxerui.log
```

The same keys work as ToolTypes (`KEY=value`).

## Where data lives on AROS

In place of the original's `~/Library` locations:

- settings: `ENVARC:Boxer/Boxer.prefs` (and the live copy in `ENV:`);
- game data: a data folder chosen once by the user, by default
  `<games folder>/Boxer Data`, with `Gamebox States/<identifier>/` per game.
  A gamebox's state is found by its identifier, so moving or renaming the
  gamebox keeps it. DOS writes go through a shadow filesystem into the data
  folder, not into the gamebox.

## Patches to Boxer

Changes to the Boxer tree are kept as a patch series in `patches/boxer/`,
applied to the pin by `scripts/bootstrap.sh`. See [patching](patching.md).
