# Wave 0: v2 bead database and v1 oracle/harvest pin (2026-11-22)

## 1. Beads: own database, prefix `v2`

**Problem.** bd 1.3.2 resolves `.beads` through the git common dir, so inside the
v2 worktree `bd where` answered `/home/tobias/Projects/sublimite/.beads` (v1, prefix `edit`).
A plain `bd init -p v2` in the worktree refuses (it sees v1's database and asks for `--reinit-local`).

**Choice.** Create `.beads/` in the worktree and initialise with an explicit `BEADS_DIR`:

```
mkdir .beads
BEADS_DIR=/home/tobias/Projects/sublimite-v2/.beads bd init -p v2 --skip-hooks --skip-agents --non-interactive
bd dolt remote remove origin      # init copied v1's GitHub URL as the dolt remote
# and deleted the `sync.remote` line it wrote into .beads/config.yaml
```

After that, plain `bd` inside the v2 worktree resolves to the v2 database (no env var needed);
`bd` in the v1 checkout still resolves to v1 (prefix `edit`).

**Evidence.** Rehearsed first in a scratch repo (main + two orphan worktrees): without `BEADS_DIR`
init aborts; with it, `bd where` resolves locally both with and without the env var; an issue created
in the worktree is not visible from main; init's auto-commit (`bd init: initialize beads issue tracking`)
contains only `.beads/{.gitignore,README.md,config.yaml,metadata.json}` and `.gitignore`, and leaves
unrelated modified/untracked files alone. Real run: commit `aae1067` on `v2`, same five files.
v1 afterwards: `git status` clean, HEAD `c95b6eb` on `main`, `bd where` → v1 `.beads`, prefix `edit`.

**Hazards recorded, not fixed.**
- The remote was removed because `bd dolt push` from v2 would have targeted v1's GitHub repo.
  Do not add one without Tobias.
- The shared `.git/config` of v1 sets `core.hookspath=/home/tobias/Projects/sublimite/.beads/hooks`,
  so commits in the v2 worktree run v1's beads hook scripts. They call `bd hooks run <hook>`, which now
  resolves to the v2 database from the v2 cwd. Changing this would mean editing v1's git config
  (`extensions.worktreeConfig`), which is off limits; left as is.

## 2. Oracle and harvest pin: `6fec2e8`

v1 local `main` is `c95b6eb`; `origin/main` is `6fec2e8` (sessions 28–30: THP on os_vm, NVIDIA EGL,
XWayland DPI, largest-output grid, Wayland clipboard publish, fractional scale, wl_shm raster, Windows work).
Harvest and oracle pin to **`6fec2e8`**, as a detached worktree outside both checkouts:

```
git -C /home/tobias/Projects/sublimite worktree add --detach /home/tobias/Projects/sublimite-v1-oracle 6fec2e8
```

v1's checkout and `main` branch are unchanged by this. Consequences:
- HANDOFF step 1 becomes `git checkout 6fec2e8 -- vendor/`.
- PLAN_V2 line references were written against `c95b6eb`; each is re-resolved to a symbol at `6fec2e8`
  before it enters a bead (the M2–M4 dossiers do this).
- The wl_shm raster at `6fec2e8` stays out of the MVP (PLAN_V2 §1) pending Tobias's call.
