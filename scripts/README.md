# Scripts

- `env.sh`: per-ABI toolchain/SDK environment (`. scripts/env.sh abiv11|mainline-v1`),
  read from the environment or `local.env`.
- `bootstrap.sh`, `save-patch`, `reproduce`, `status`: patch tooling, wrappers
  around `patchtool.py`; see [patching](../documentation/patching.md).
- `build-sdl3.sh`, `build-core.sh`, `build-ui.sh`, `build-smoke.sh`,
  `build-model.sh`, `build-unwind-fix.sh`: builds, see
  [development](../documentation/development.md).
- `check-undefined.sh` (with `ui-undefined-allowlist.txt`),
  `check-package-notices.sh`: checks used by the build and packaging.
- `make-package.sh`: binary package, see [packaging](../packaging/README.md).
- `boxer-assets.py`: regenerates `assets/source/boxer` and `assets/runtime/boxer`
  from the Boxer pin (macOS).
