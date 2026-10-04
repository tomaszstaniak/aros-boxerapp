# Reduced static SDL3 for BoxerUI

`scripts/build-sdl3.sh <abiv11|mainline-v1>` builds a static SDL3 with audio
(AHI), timer, threads, joystick, gamepad and virtual joystick, and without
video, renderer, OpenGL or camera, into `build/<abi>/sdl3/`. BoxerUI links it
when `BOXER_HOST=sdl3` (the default on ABIv11). The result needs only
`-liconv` and never opens `gl.library`.

Inputs (all recorded in `build/<abi>/sdl3/BUILDINFO.txt`):

| Input | Origin | Licence |
| --- | --- | --- |
| `SDL3-3.4.12.tar.gz` (not in this repository) | <https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-3.4.12.tar.gz>, sha256 `f07b958a9ac5020fb7a44cadb957f658b2149c3c8abb4f63145fac9303249db7`; downloaded to `deps/src/` by `build-sdl3.sh`, or set `SDL3_TARBALL` | zlib (`LICENSES/SDL3-zlib.txt`) |
| `contrib-a3cb9c4700/` | The AROS SDL3 port, unmodified copies of `SDL3-3.4.12-aros.diff`, `SDL3_static.c`, `SDL3_intern.h` and `mmakefile.src` from aros-development-team/contrib at `a3cb9c4700a81c9d14ebdc8741c932825c4429f0` (`SDL3/`) | zlib (SDL files), AROS Public License 1.1 (`mmakefile.src`) |
| `sdl3-3.4.12-langinfo-guard.diff` | Local patch: includes `<langinfo.h>` only when `HAVE_NL_LANGINFO` is defined | zlib, as the file it changes |
| `inc/hidd/controller.h` | AROS `72a773f2de8af8295f221dc02ee6fd8d48ae46e0` | AROS Public License 1.1 |
| `gen/` | genmodule output for `controller.conf` at that revision | AROS Public License 1.1 |
| `inc-v11/` | `hidd/`, `oop/` and interface headers copied from a mainline AROS `Developer/include` (the ABIv11 SDK has no `hidd/`); used for ABIv11 only | AROS Public License 1.1 |

`build-sdl3-ctl.py` extracts the tarball, applies the contrib diff and the
local patch, disables video, renderer, OpenGL and camera in
`SDL_build_config_aros.h`, and compiles the file list of the contrib
`mmakefile.src` without the video and OpenGL sources. The source in
`build/<abi>/sdl3/src/` is therefore an altered SDL; it is not distributed
by this project.
