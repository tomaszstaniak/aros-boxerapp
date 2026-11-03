# AROS patches for libc++, libc++abi and libunwind 20.1.0

Used only for the Raspberry Pi target (`aros-aarch64-abiv1-raspi`), whose
toolchain and SDK carry no C++ standard library. `scripts/build-cxx-runtime.sh`
builds the three runtimes from the LLVM 20.1.0 release tarball with these
patches, and BoxerUI links them statically.

| File | Origin |
| --- | --- |
| `libcxx-20.1.0.src-aros.diff` | AROS `tools/crosstools/llvm/` |
| `libcxxabi-20.1.0.src-aros.diff` | AROS `tools/crosstools/llvm/` |
| `libunwind-20.1.0.src-aros.diff` | AROS `tools/crosstools/llvm/` |

Copied unmodified from <https://github.com/aros-development-team/AROS>. The
three files were last changed in commit
`7654818bf1acd25b11cd840a713dc97692bfff55` ("libc++abi 20: keep thread_local
exception storage off AROS", 2026-08-15) and are byte-identical at
`1fef6c415e12523a2c868e4dd1597f5d7b4ba710`, the revision they were copied
from.

The patched files belong to LLVM (Apache-2.0 WITH LLVM-exception,
`LICENSES/LLVM-Apache-2.0-with-exception.txt`); the AROS repository is under
the AROS Public License 1.1 (`LICENSES/AROS-APL-1.1.txt`).

`PINS` holds the sha256 of the LLVM tarball, of each patch here and of the
static archives the build must produce. `tools/cxxrt_pins.py` checks them:
`build-cxx-runtime.sh` before it unpacks and after it builds, `build-ui.sh`
before it links, and `make-package.sh` before it packages. A changed patch,
tarball or archive stops the build; after a deliberate change, update `PINS`
in the same commit.
