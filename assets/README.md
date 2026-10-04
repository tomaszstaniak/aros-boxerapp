# Assets

## Boxer artwork (2026-10-01)

Boxer as a whole, artwork included, is released under GPL-2.0: "Boxer is
copyright (c) 2013 Alun Bestor and contributors. Boxer is released under the
GNU General Public License 2.0" (`legalese.html` in the pinned upstream
`0062fc18`). No exception for Boxer's own images or sounds is listed there.

Boxer's own artwork is therefore used under GPL-2.0, like its code
(decided 2026-10-01).

| Path | Content |
| --- | --- |
| `source/boxer/` | Unmodified originals of category (a), at their upstream repo-relative paths, plus `legalese.html` and `acknowledgements.html` as licence evidence |
| `runtime/boxer/` | AROS runtime versions (PNG; WAV for sounds), flat names |
| `boxer-assets-manifest.json` | Every upstream image/sound: category, reason, sha256; for (a) the licence, notice, copy path and the exact conversion command per runtime file |
| `NOTICE-boxer.txt` | Attribution and GPL-2.0 obligations for distribution |

Regenerate everything with `scripts/boxer-assets.py` (macOS host; `sips`,
`afconvert`). The script wipes and rebuilds `source/boxer/` and
`runtime/boxer/`; do not hand-edit them.

Naming in `runtime/boxer/`: PNG originals keep their names (`X.png`,
`X@2x.png`); JPEGs become `X.png`; PDF templates become `X.png` at their
point size and `X@2x.png` at double; ICNS become `X-32.png`, `X-128.png`,
`X-512.png`; AIFF become 16-bit little-endian `X.wav`.

Categories: (a) Boxer's own, GPL-2.0, copied; (b) Apple system images,
referenced by name only, never shipped; (c) third party with own terms (DOSBox
logo, sample games, Sparkle, BGHUDAppKit, Joypad logo), not copied; (d)
unclear for a stated reason, not copied. Reasons per file are in the
manifest.

Restored originals (2026-10-02): `gamefolder.icns`, `prompt.icns`,
`import.png` (+`@2x`) and `Game.png` (+`@2x`) keep their category (d) note as
history, but are copied and used again (manifest fields `restored`,
`runtime`, `used_in`, `open_question`). The resemblance to Apple artwork is an
open question, not a confirmed bar. `scripts/boxer-assets.py
--restore-originals` redoes only these: `runtime/boxer/gamefolder-128.png`
and `prompt-128.png` are the icns 128x128 members extracted with `iconutil`,
`import.png` and `Game.png` are byte copies; every output is at the exact size
the UI draws, so nothing is resampled. The `@2x` files are kept as sources only.

## Replacement icons (2026-10-02)

The Apple system images the UI needs (Reveal, lock open/closed, full screen)
are replaced by Feather (MIT), never by imitations of the originals. Until
the same day Tango images also stood in for the four (d) icons above; those
are gone from `runtime/replacements/`, and the Tango sources stay only as
history (`replacement-assets.json` `history`).

| Path | Content |
| --- | --- |
| `source/replacements/` | Unmodified source files (Feather 4.29.2, MIT; Tango 0.8.90, public domain, unused since 2026-10-02) with their licence files |
| `runtime/replacements/` | Converted PNGs embedded by `scripts/build-ui.sh` |
| `replacement-assets.json` | Per output: sha256, sources and their hashes, archive URL and sha256, version, licence, author, conversion, UI use; rejected candidates |
| `NOTICE-replacements.txt` | Attribution; licence texts in `LICENSES/Tango-Public-Domain.txt`, `LICENSES/MIT-Feather.txt` |

Regenerate with `tools/make-replacement-assets.py` (macOS host, `sips`). It
does not touch `source/boxer/` or `runtime/boxer/`, and `boxer-assets.py`
does not touch these directories.
