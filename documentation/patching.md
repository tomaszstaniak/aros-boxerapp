# Pinned sources and patch workflow

`upstreams.json` pins `boxer` to the original alinebee/Boxer repository
(`0062fc18e154cde6cef3bf3329be4b0658597e31`, tip of master on 2026-10-01,
2017-03-18, Boxer 2.0.0-alpha line; not the 1.4.0 maintenance branch). It has
no submodules: DOSBox 0.74 (modified) and all frameworks are vendored.
The pin is a starting reference, not a release-selection or portability finding.

- `upstream/boxer/`: clean, read-only checkout of the pin. Never edited.
- `work/boxer/`: editable copy, Git branch `aros-port`, remote `pinned-upstream`.
- `patches/boxer/series`: application order, one `.patch` per line.
- `work/.boxer.baseline.json`: the materialized baseline identity (repository,
  full pin, ordered series with SHA-256 of each patch, baseline commit and tree).
  It records expected inputs only; the tools verify the actual tree separately.

## Commands

All tools are Python 3 standard library plus `git`; they need no ABI, SDK or
`scripts/env.sh`, and work from any directory and with spaces in paths.
Exit codes: 0 ok, 1 check failed or action refused, 2 configuration error
(missing manifest, repository, series file, patch file, or a non-40-hex pin).

```sh
scripts/bootstrap.sh            # clone upstream if absent; create work/ if absent;
                                # otherwise report and change nothing
scripts/bootstrap.sh --verify   # read-only: upstream clean at pin, baseline inputs
                                # current, saved tree == isolated reconstruction,
                                # local files and Git history inventory
scripts/bootstrap.sh --adopt    # record a baseline for an existing work copy that
                                # verify proved equivalent, clean, with no local history
scripts/bootstrap.sh --recreate [--backup-dir DIR]
scripts/save-patch --title T --author A --problem P --solution S \
    --platform-scope X --upstream-status local-only --verification V \
    --removal-condition R  "Other Sources/file.m" ...   # or --staged
scripts/save-patch --header header.txt PATHS...          # Key: value file or .json
scripts/reproduce [--out DIR] [--strict]
scripts/status [--reconstruct]
```

### Creating and checking `work/`

`bootstrap.sh` builds a new copy in a temporary sibling directory (clone of
`upstream/boxer`, checkout of the pin, `git apply --index` of each patch, one
commit per patch) and moves it into place only when every patch applied. A
conflict stops the run; no work copy or baseline is recorded.

When the recorded baseline differs from the current pin, series order or a
patch's digest, the copy is reported as STALE; it is never reported as current.

`--recreate` rebuilds `work/` only when both of these hold:

1. **Source equivalence**: the work copy's committed tree (`HEAD`) equals an
   isolated reconstruction of pin + current series (contents, modes, symlinks),
   and there are no staged, unstaged, untracked or ignored files.
2. **History preservation**: no stash entries, no other branch/tag/ref holding
   commits absent from upstream and the baseline, `HEAD` is the recorded baseline
   commit (an amend fails this even when the tree matches), no reflog-only prior
   states, no unreachable local commits (dropped stashes), no linked worktrees or
   alternates.

Otherwise it refuses, unless `--backup-dir DIR` (outside `work/`) is given: the
whole directory, including `.git` with refs, reflogs and stashes and all ignored
files, is copied with `cp -a`, compared file by file, every inventoried commit and
stash is resolved in the copy, refs and the stash list are compared, and the copy
passes `git fsck`. Only then is the old copy replaced. The copy is never deleted
by the tools.

### Saving a patch

`save-patch` takes the selected paths (relative to `work/boxer`) from the working
tree, or the staged index with `--staged`. The selection is assembled in a
private index, so the user's index and files are unchanged until the save is
complete. Before anything is written, the patch is applied to `HEAD` in a second
private index and must reproduce the selected tree exactly.

The file is the next free number above every existing `NNNN-*.patch` in the
directory and in the series (gaps are kept; an existing file is never
overwritten, the file is created with exclusive create). Order of writes: patch
file, series, commit on `aros-port`, baseline record. Any failure rolls back the
completed steps; working-tree edits are never touched. Each patch is the delta
against the previous baseline only.

Required header fields: title, author, problem, solution, platform scope,
upstream status (`candidate`, `local-only`, `submitted`, `accepted`;
the last two need `--upstream-link`), verification, removal condition.
Repository, date, base commit and preceding patches are filled in. Saving a
patch is not verification: write "not run" when nothing was run.
`save-patch` refuses when the baseline is stale or `HEAD` is not the baseline
commit (local commits or amend): it saves working-tree changes only.

### Reproducing

`reproduce` clones `upstream/boxer` into a temporary directory and applies the
series, then compares with `work/boxer` read-only: the committed tree by
`git ls-tree` (blob hash and mode per path) and the files on disk by SHA-256,
executable bit and symlink target. Unsaved deltas, extra local files and staged
entries are listed separately and never discarded. `--out DIR` keeps the
reconstruction; `--strict` also fails on unsaved deltas.

## Binary files and symlinks

Supported and round-tripped byte for byte: binary files (added, modified,
deleted) as Git binary patches (`git diff --binary --full-index`), symlinks
(mode 120000, target as content, e.g. inside `.framework` bundles), and the
executable bit. A diff containing a bare "Binary files differ" notice is refused.

Rejected before any mutation, with the files left as they are: submodule or
nested-repository changes (gitlinks). Not represented at all (Git limits):
empty directories, permission bits other than executable, ownership, extended
attributes and resource forks. Patches for large binaries are large text files;
review them before committing.

## Limitations

- `work/` Git commits only track the local diff; they are not project history.
  A saved patch is a working file until committed with its metadata to the
  project repository.
- Clean filters, LFS and `.gitattributes`-driven conversions are not handled.
- Path case changes on a case-insensitive file system are untested.
- The tool test suite (`tests/patch-tooling/run.sh`) exercises fixture
  repositories, not the full Boxer tree.

Retest affected scenarios with `tests/patch-tooling/run.sh` after changing
`scripts/patchtool.py`.
