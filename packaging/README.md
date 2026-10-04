# Packaging

`scripts/make-package.sh <abiv11|mainline-v1>` turns an existing build
(`scripts/build-core.sh`, `scripts/build-ui.sh`) into a binary package. Test
stagings (`build/stage-*`) hold test fixtures and scripts; they are not
packages and must not be distributed.

## Output

`build/<abi>/package/`:

| Path | What |
| --- | --- |
| `Boxer/` | the AROS drawer users install (layout below) |
| `boxer-<version>.x86_64-aros-v11.lha` / `boxer-<version>.x86_64-aros.lha` | the drawer packed for arospkg (LHA, drawer at top level, `lha aq2o51`); `.zip` when no LHA writer is available |
| `MANIFEST-<abi>.sha256` | sha256 of every file in the drawer and of the archive |

`Boxer/`:

| File | Source |
| --- | --- |
| `BoxerUI` | `build/<abi>/ui/BoxerUI`, unstripped (a full strip breaks AROS x86_64 relocations) |
| `BoxerUI.info` | tool icon, stack 65536, `tools/mkicon.py` with Boxer's `boxer-128.png` |
| `Fonts/` | `build/<abi>/ui/stage/Fonts`: BoxerSans `.font`/`.otag` + `Bitstream-Vera.txt` |
| `conf/Preflight.conf`, `conf/Launch.conf` | Boxer pin, unmodified. Only these two are read: `sessionConfigFiles()` gets no game profiles yet. Add profile confs here when profile detection lands |
| `dosbox.msg` | `tools/strings2table.py` from the pin's `Resources/Base.lproj/DOSBox.strings` (Boxer, GPL-2.0); input hash in `BUILDINFO.txt` |
| `COPYING`, `LICENSES/*`, `NOTICE-boxer.txt`, `licenses.md` | `documentation/licenses.md` distribution checklist |
| `boxer-<version>-source.tar.gz` | corresponding source (GPL-2.0 s.3(a)), see below |
| `BUILDINFO.txt` | build's BUILDINFO + package data: AROS SDK source revision, Boxer pin, dosbox.msg provenance, source-archive hash |
| `REQUIREMENTS.txt` | `packaging/requirements-<abi>.txt` |
| `ReadMe.txt` | `packaging/ReadMe.txt.in` |
| `SHA256SUMS` | every other file in the drawer |

Not shipped, by design: Boxer artwork files (all used artwork is embedded in
`BoxerUI` by `tools/png2inc.py`, category (a) only); Apple, third-party and
unclear assets; `tools/mkgameboxicon.py` (a host tool, not runnable on AROS);
`var/` (Dune) or any game data; test fixtures. The script fails if a PNG,
WAV, TTF, EXE, `.boxer` or Dune file ends up in the drawer.

## Checks the script makes

1. `BoxerUI` sha256 equals `BUILDINFO.txt`; `scripts/check-undefined.sh`
   passes with the empty allowlist.
2. Every source file hash in `BUILDINFO.txt` matches the tree, and the core
   library matches. Otherwise the source archive would not correspond to the
   binary and the script stops. `BOXER_PKG_ALLOW_STALE=1` builds a dry-run
   package anyway, versioned `-STALE` and marked NOT CORRESPONDING in
   `BUILDINFO.txt`; never distribute one.
3. `work/boxer` conf files equal the upstream pin; `upstream/boxer` is at the
   commit in `upstreams.json`.
4. `scripts/check-package-notices.sh` on the drawer must pass.

## Source archive

`aros-boxerapp/`: project files the build uses (`src scripts tools patches
assets tests third_party packaging documentation LICENSES COPYING README.md
upstreams.json local.env.example`); in a Git checkout only tracked files.
Never `var/`, `work/`, `build/`, `local.env`.
`boxer-upstream/`: the used part of the pinned Boxer tree (`Boxer/`, `DOSBox/`
code, `Resources/Configurations`, `DOSBox.strings`), unmodified; patched
source = this + `patches/boxer/series`. Rebuild steps: `SOURCE-README.txt`.

## Running

```
LHA_WRITER=/path/to/classic/lha scripts/make-package.sh abiv11
LHA_WRITER=/path/to/classic/lha scripts/make-package.sh mainline-v1
```

Homebrew's `lha` is Lhasa and cannot create archives; LHa for UNIX 1.14i
can. Without `LHA_WRITER` the script makes a ZIP. The version
comes from `packaging/VERSION` (`0.1.0-test`: not a release).

## Open points

- The ABIv11 build links AROS libraries from a local SDK build; the AROS
  source revision is recorded in `BUILDINFO.txt` when `AROS_V11_SRC` is set.
- The runtime library lists in `requirements-*.txt` come from the link trace
  and the binary's library-name strings; presence on the target images was not
  checked.
- Icons: `Boxer.info` beside the drawer (in the archive),
  `BoxerUI.info` and `Install-Assign.info` with Boxer's art, and
  `conf/GamesFolder.info`, the template BoxerUI copies when it creates the
  games folder (the `Boxer Data` drawer it creates gets the system's
  default drawer icon). All from `tools/mkicon.py`; existing `.info` files
  are never replaced.

## The Boxer: assign (`Install-Assign`)

Gamebox icons name `Boxer:BoxerUI` as default tool (Workbench accepts an
assign or an absolute path, not a path relative to the icon). `Install-Assign`
is copied into the drawer with a project icon (default tool `C:IconX`, a
window that stays open). The user double-clicks it, or runs
`Execute Install-Assign [DIR=<drawer>] [REPLACE|KEEP|REMOVE]`; DIR defaults
to the current directory, which IconX sets to the icon's drawer. Rewritten
on 2026-10-04:

- `Boxer:` is assigned now, and `S:Boxer-Assign` is (re)written with the
  drawer's absolute path;
- `S:User-Startup` gets one `;BEGIN Boxer`/`;END Boxer` block that executes
  `S:Boxer-Assign` if it exists. The file is created when missing, existing
  lines are kept, and a re-run finds the block and adds nothing. Because the
  block is indirect, a moved drawer needs only a re-run, never an edit;
- `Boxer:` already pointing elsewhere is never overwritten silently: `KEEP`
  leaves it (exit 5), `REPLACE` changes it, and without either a
  Replace/Keep requester asks (the window says so first);
- `REMOVE` deletes `S:Boxer-Assign` and the assign; the block becomes inert;
- no `.def` (AROS did not apply it to a `/K` keyword, which left the old
  script waiting at `If EXISTS ""`), and no `Search` on an empty file;
- BoxerUI keeps working from the Shell without the assign. When it is
  missing, Workbench refuses the icon before BoxerUI runs (it cannot show
  its own error then); a Shell start logs `assign Boxer: missing`.

User data does not live in this drawer: the data folder is chosen at the
first gamebox start and kept in `ENVARC:Boxer/Boxer.prefs`.
