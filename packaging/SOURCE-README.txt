Corresponding source for the Boxer for AROS binary package
==========================================================

This archive is the complete corresponding source (GNU GPL version 2,
section 3(a)) of the BoxerUI binary it was shipped with. BUILDINFO.txt in
the binary package lists the SHA-256 of every compiled project file; the
same files are in this archive.

Contents
  aros-boxerapp/          the port: src/, scripts/, tools/, patches/,
                          assets/ (Boxer artwork originals and their
                          conversions), tests/, third_party/sdl3-aros/
                          (SDL3 build inputs), third_party/llvm-runtimes-aros/
                          (AROS patches for the Raspberry Pi C++ runtime,
                          with their pins), third_party/libiconv/ (origin,
                          hashes and build recipe of the GNU libiconv the
                          x86_64 programs link; its source and a relink kit
                          are the release asset
                          Boxer-<version>-<target>-lgpl-kit.zip), packaging/,
                          documentation/, COPYING, LICENSES/
  SOURCE-COMMIT.txt       the aros-boxerapp commit this archive was made from
  boxer-upstream/         the parts of Boxer (https://github.com/alinebee/Boxer,
                          commit in UPSTREAM-PIN.txt) the build uses:
                          Boxer/ and DOSBox/ (source code),
                          Resources/Configurations/*.conf and
                          Resources/Base.lproj/DOSBox.strings. Unmodified.
                          Boxer's other files (Xcode project, Mac
                          frameworks, Apple and third-party images, sample
                          games) are not used by this port and not included.

Rebuilding
  1. Copy boxer-upstream/ to aros-boxerapp/work/boxer and apply the patches
     in aros-boxerapp/patches/boxer/series in order:
       cd aros-boxerapp/work/boxer
       for p in $(cat ../../patches/boxer/series); do patch -p1 < ../../patches/boxer/$p; done
     (scripts/reproduce does the same from a Git checkout of the pin.)
  2. Copy local.env.example to local.env and point it at the toolchain and
     SDK named in BUILDINFO.txt.
  3. scripts/build-core.sh <abi>, then scripts/build-ui.sh <abi>
     (abi: abiv11, mainline-v1 or aros-aarch64-abiv1-raspi). The Raspberry
     Pi target uses Clang 20.1, the SDK of the nightly it runs on, and
     needs scripts/build-cxx-runtime.sh first: libc++, libc++abi and
     libunwind 20.1 from llvm-project-20.1.0.src.tar.xz with the AROS
     patches in third_party/llvm-runtimes-aros/; the tarball, the patches
     and the resulting archives must match the sha256 in its PINS. On mainline, build-ui.sh first runs
     scripts/build-unwind-fix.sh, which compiles GCC 10.5's unwind-dw2.c
     from a configured mainline AROS GCC build tree (AROS_V1_GCC_BUILD).
     For the SDL3 host (default on ABIv11) run scripts/build-sdl3.sh <abi>
     before build-core.sh; it fetches SDL3-3.4.12.tar.gz and checks its
     sha256.
  4. scripts/make-package.sh <abi> rebuilds the binary package.

The compilers (GCC 10.5 for x86_64, Clang 20.1 with the AROS patches of
tools/crosstools/llvm for the Raspberry Pi) and the AROS SDK link libraries
are components of the AROS development system; their source is public at
https://github.com/aros-development-team/AROS (mainline) and
https://github.com/deadwood2/AROS (ABIv11), at the revisions in BUILDINFO.txt.
