#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reduced static SDL3 from contrib a3cb9c4700 (the controller.hidd backend):
no video/renderer/GL/camera; joystick, gamepad, virtual joystick, haptic and
sensor stay on. Outside the AROS build system, one ABI per output dir.

Headers the backend needs that the SDKs do not carry (see README.md):
  inc/hidd/controller.h      upstream AROS 72a773f2de
  gen/interface/*.h          genmodule writeincludes from that controller.conf
  inc-v11/                   (abiv11 only) hidd/input.h & co copied from the
                             mainline Developer include, ABIv11 SDK has no hidd/
usage: build-sdl3-ctl.py <abiv11|mainline-v1> <outdir>
(normally run through scripts/build-sdl3.sh)
"""
import hashlib, os, re, subprocess, sys, tarfile, datetime
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

here = Path(__file__).resolve().parent
abi, out = sys.argv[1], Path(sys.argv[2]).resolve()
if abi not in ('abiv11', 'mainline-v1'):
    sys.exit('usage: build-sdl3-ctl.py <abiv11|mainline-v1> <outdir>')
# Set by scripts/env.sh for the selected ABI; SDL3_TARBALL by scripts/build-sdl3.sh.
TC, SDK = os.environ['AROS_TOOLCHAIN'], os.environ['AROS_SDK']
TARBALL = Path(os.environ['SDL3_TARBALL'])
TARBALL_SHA = 'f07b958a9ac5020fb7a44cadb957f658b2149c3c8abb4f63145fac9303249db7'
CONTRIB = here / 'contrib-a3cb9c4700'
PATCH = CONTRIB / 'SDL3-3.4.12-aros.diff'
LANGINFO = here / 'sdl3-3.4.12-langinfo-guard.diff'

sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(TARBALL) == TARBALL_SHA, 'tarball checksum'

src = out / 'src/SDL3-3.4.12'
if not src.exists():
    (out / 'src').mkdir(parents=True, exist_ok=True)
    with tarfile.open(TARBALL) as t:
        t.extractall(out / 'src')
    for p in (PATCH, LANGINFO):
        subprocess.run(['patch', '-s', '-p1', '-d', str(src), '-i', str(p)], check=True)

cfg = src / 'include/build_config/SDL_build_config_aros.h'
text = cfg.read_text()
for d in ('SDL_VIDEO_DRIVER_AROS', 'SDL_VIDEO_DRIVER_DUMMY', 'SDL_VIDEO_OPENGL_AGL',
          'SDL_VIDEO_OPENGL', 'SDL_VIDEO_RENDER_OGL'):
    text = re.sub(rf'^#define {d} 1$', f'/* {d} removed */', text, flags=re.M)
if '/* build-sdl3-ctl.py */' not in text:
    i = text.rstrip().rfind('#endif')
    text = (text[:i] + '/* build-sdl3-ctl.py */\n#define SDL_VIDEO_DISABLED 1\n'
            '#define SDL_RENDER_DISABLED 1\n#define SDL_CAMERA_DISABLED 1\n' + text[i:])
cfg.write_text(text)
assert '#define SDL_JOYSTICK_AROS 1' in text and '#define SDL_JOYSTICK_VIRTUAL 1' in text

mm = (CONTRIB / 'mmakefile.src').read_text(errors='replace').splitlines()
block = []
for i, ln in enumerate(mm):
    if re.match(r'FILES\s*[:+]?=', ln):
        for cont in mm[i:]:
            block.append(cont)
            if not cont.rstrip().endswith('\\'):
                break
files = re.findall(r'\$\(ARCHSRCDIR\)/(\S+)', '\n'.join(block))
files = [f for f in files if not f.startswith(('src/video/aros/', 'src/render/opengl/'))]

objdir = out / 'obj'; objdir.mkdir(parents=True, exist_ok=True)
cc, ar = Path(TC) / 'x86_64-aros-gcc', Path(TC) / 'x86_64-aros-ar'
inc = [f'-I{src}/include', f'-I{src}/include/build_config', f'-I{src}', f'-I{src}/src',
       f'-I{here}/inc', f'-I{here}/gen']
if abi == 'abiv11':
    inc.append(f'-I{here}/inc-v11')
inc.append(f'-I{SDK}/include')
if Path(SDK, 'SDK/Extras/include').is_dir():
    inc.append(f'-I{SDK}/SDK/Extras/include')
cflags = ['-std=gnu99', '-O2', '-DSDL3_AROS_STATIC',
          f'-DADATE="{datetime.date.today():%d.%m.%Y}"', '-w']
units = [(src / (f + '.c'), objdir / (f.replace('/', '_') + '.o')) for f in files]
units.append((CONTRIB / 'SDL3_static.c', objdir / 'SDL3_static.o'))


def build(u):
    s, o = u
    r = subprocess.run([str(cc), f'--sysroot={SDK}'] + cflags + inc + ['-c', str(s), '-o', str(o)],
                       capture_output=True, text=True)
    return s, r.returncode, r.stdout + r.stderr


fail = 0
with ThreadPoolExecutor(max_workers=8) as ex:
    for s, rc, msg in ex.map(build, units):
        if rc:
            fail += 1
            print(f'FAIL {s}\n{msg[:1500]}', file=sys.stderr)
print(f'{abi}: compiled {len(units) - fail}/{len(units)} objects')
if fail:
    sys.exit(1)
lib = out / 'lib/libSDL3_ctl.a'
lib.parent.mkdir(exist_ok=True); lib.unlink(missing_ok=True)
objs = [str(o) for _, o in units]
for i in range(0, len(objs), 100):
    subprocess.run([str(ar), 'rcs', str(lib)] + objs[i:i + 100], check=True)
print(f'{lib} {lib.stat().st_size} bytes')
