# AGENTS.md

charts: DOS-style chart decks for the Linux console. C89 app (`app/`) on the
C89 library libcharts (`lib/`, `include/`). `src/` is the old C++ build, kept
only as a test oracle. The agent skill in `skill/charts/` is generated from
`charts -h`.

## Version

- Every change is released: a version bump, a tag `vX.Y.Z` and a GitHub
  Release. The patch number (0.1.0 -> 0.1.1) for fixes and small features,
  the minor number (0.1.x -> 0.2.0) for a new chart type, deck key or CLI
  flag.
- Stay below 1.0. Never set 1.0 or higher: the owner alone certifies when
  charts is 1.0. `tools/release.sh` refuses anything but `0.y.z`.
- The version lives in three places, and `tools/release.sh` changes all of
  them in one commit: `include/charts.h` (`LC_VERSION`), `app/cli.c`
  (`app_print_version`) and the header of `docs/charts.1`. Do not bump by
  hand in the change itself.

## Git workflow

1. **Start clean.** `git status` shows nothing to commit. On `main`:
   `git pull --ff-only origin main`. If the pull is not a fast-forward, stop
   and ask; never merge or rebase someone else's history to make it fit.
2. **Branch.** One branch per change, named `<area>/<topic>`
   (`pie/explode-label`, `cli/better-check`). For parallel work use a
   worktree: `git worktree add ../charts-<topic> -b <area>/<topic> main`.
   Never commit a change straight to `main`; the only commit made on `main`
   is the version commit from `tools/release.sh`.
3. **Change.** Keep the change to one thing.
4. **Build and test** before every commit:
   - `make && make test` (library, CLI smoke, fuzz, platform, presenter).
   - `make test-asan` when C code that allocates, parses or indexes changed.
   - `make test-stress` when chart sizing or allocation changed.
   - `make test-oracle` when rendering changed: PNG parity with the C++ oracle.
     A difference must be explained in the commit message, not ignored.
   - `make skill` when `charts -h`, `docs/DECK.md`, `examples/demo` or the
     gallery changed: it regenerates `skill/charts/SKILL.md`, its references
     and the gallery PNGs. Commit the regenerated files with the change.
   - `make dist TARGET=<target>` when platform code or the build changed
     (`make dist-targets` lists them; needs `zig`). CI builds them all on
     every push.
   - Real check: run the change on a deck (`charts deck.json --check`, then
     `--png-dir` and look at the PNGs). A green test is not proof it works.
5. **Commit.** Message style of this repo: `area: what changed, in plain
   words` (lowercase area, no period, under ~72 characters), e.g.
   `pie: probe the slice only next to an edge`. Body, if needed: why, and
   numbers with what they were measured on. One logical change per commit.
   Never commit build output (`build/`, `dist/`, `charts`, `*.o`, `*.a`,
   `.rag_index/`; see `.gitignore`).
6. **Merge.** Rebase the branch on the latest `main`
   (`git fetch origin && git rebase origin/main`), rerun `make test`, then
   `git switch main && git merge --ff-only <area>/<topic>`. Delete the branch
   (`git branch -d`, and `git worktree remove` for a worktree).
7. **Release and push.** Finishing a change means releasing it:
   `tools/release.sh <x.y.z> ["summary"]` on the clean `main`. It checks the
   version is below 1.0 and above the last, bumps it, runs `make test`,
   commits `version x.y.z: <summary>`, tags `vx.y.z` and pushes `main` and the
   tag together. The tag starts `.github/workflows/release.yml`, which builds
   every target, tests the archives and publishes the GitHub Release with
   them and `SHA256SUMS`. Watch it to green: `gh run list --limit 3`, then
   `gh run watch <id>`. A red release is fixed by a new change and a new
   patch version; never move or delete a pushed tag.
8. **Never force-push `main`**, never rewrite pushed history, never push
   other branches unless asked.

## Install on this box

`make && sudo make install` installs to `/usr/local` (what is on PATH here).
Do it after every release, so `/usr/local/bin/charts` is the released version.
`make install-skill` copies the skill to the user's skill directory.
