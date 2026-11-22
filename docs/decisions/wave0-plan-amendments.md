# Wave 0: amendments to PLAN_V2 (2026-11-22)

Rule (advisor): the plan text is kept true; this file says why each line changed.

## A1. §2 L0 internal order: `base < ipc` (was `os < ipc`)

v1 `src/ipc/ipc.h` includes `base/base.h`: `ipc_args.arena` and `ipc_server.arena` are `edit_arena`, used by
`ipc_parse_args` / `ipc_args_read_stdin` (`src/ipc/args.c`), `ipc_client_send_display` and `setup_runtime`
(`src/ipc/ipc_posix.c`) at `6fec2e8` (docs/decisions/wave1-L0-harvest.md §0.1). An arena is what `base` is for;
rewriting ipc to avoid it would be churn. New order: `os < base < trace < work; base < ipc` (ipc may use os and
base, never trace or work). Changed in PLAN_V2 §2 and `tools/check_dag.py` `INTRA_RULES`. Approved by the advisor.

## A2. §3, §2 L1, §6: the 17-leaf piece root collapse is struck

The deciding test (docs/decisions/wave0-piece-root-probe.md, bead v2-he7.7) ran ten root grow/collapse shapes
against v1 at `6fec2e8` under ASan+UBSan with a reference model: 16→17 grow, left/right/boundary shrinks,
shrink-by-1, repeated grow/shrink cycles, height-2 collapse (257/300 pieces) with a live snapshot, checkpoint,
`insert_ref`, chunk build, and a random walk. All pass; a deliberate one-byte model flip is reported (harness
detects divergence). Outcome B: no bug, so no "failing test first" and no "fix root promotion" in Wave 1. The test
itself becomes an ordinary v2 `piece` regression test. Nobody had pointed at the bug in code, test, bead or review.

## A3. §6 expected-failure list (from M3, docs/decisions/wave0-known-bugs.md)

Two XFAIL specs remain: post-rename save conflict, `LINEIDX_EDIT_MAX_CHUNKS` delete+undo above 32 MiB
(512 × 64 KiB = 33,554,432 B (P)). Wayland clipboard copy is fixed in v1 by `73beeb4` and moves to green-on-v1,
mock-verified only; the real-compositor check stays a Wave 2 gate item. See also
docs/decisions/wave0-beads-and-oracle-pin.md §7.
