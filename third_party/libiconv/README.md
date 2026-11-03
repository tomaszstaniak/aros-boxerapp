# GNU libiconv in the x86_64 programs

The x86_64 BoxerUI programs (ABIv11 and mainline) link GNU libiconv
statically: SDL3's character-set conversion (`SDL_iconv`) uses it. The
library comes from each target's AROS SDK; this project does not modify it.
The Raspberry Pi program contains no iconv (its SDK has none, so SDL3 uses
its own converter).

GNU libiconv is under the GNU Lesser General Public License (version 2.1
or later for the library; `LICENSES/LGPL-2.1.txt`). For each x86_64 release
a separate asset, `Boxer-<version>-<target>-lgpl-kit.zip`, carries the
library source exactly as linked, its build recipe and a relink kit, made
by `scripts/make-lgpl-kit.sh` (see `documentation/licenses.md`).

| Target | Library | Upstream source | AROS changes | Build recipe |
| --- | --- | --- | --- | --- |
| x86_64 ABIv11 | `lib/libiconv.a` of the AROS One 1.3 SDK | `libiconv-1.14.tar.gz` | none (the recipe builds it unpatched) | `abiv11/mmakefile.src` (deadwood2/contrib `development/libs/libiconv`, commit `ddbc21eb8b4dd324f82caa69304ef4f827371178`), `abiv11/BUILD.txt` |
| x86_64 mainline v1 | `SDK/Extras/lib/libiconv.a` of the mainline SDK (AROS 87266168, contrib 45327d2c) | `libiconv-1.19.tar.gz` | `mainline-v1/libiconv-1.19-aros.diff` (gnulib `srclib/` only: `fseterr`, `getprogname`; no file of `libiconv.a` is changed) | `mainline-v1/mmakefile.src` (aros-development-team/contrib `development/libs/libiconv` at 45327d2c), `mainline-v1/BUILD.txt` |

The upstream tarballs are not stored here; they are the GNU releases, checked
by the sha256 in `PINS` (both verified against their GNU signatures):
<https://ftp.gnu.org/gnu/libiconv/libiconv-1.14.tar.gz> and
<https://ftp.gnu.org/gnu/libiconv/libiconv-1.19.tar.gz>.

The version is also read from the linked object: `_libiconv_version` is
`0x010E` (1.14) in the ABIv11 library and `0x0113` (1.19) in the mainline
one. Built as `BUILD.txt` describes, the sources above give object files
identical byte for byte to the members of each SDK's `libiconv.a`
(`iconv.o`, `localcharset.o` and `relocatable.o` on ABIv11; `iconv.o`,
`localcharset.o` and `compat.o` on mainline).

`PINS` lines: `tarball <sha256> <file>` for the upstream source,
`archive <sha256> <target> <version>` for the SDK's `libiconv.a`.
