# v2-he7.19 — Check lane and tools inventories

`tests/lanes.mk` is the reviewed suite manifest. `LAYER_SUITES`,
`SAN_SUITES`, `LAW2_SUITES`, `TSAN_SUITES` and `FUZZ_SUITES` are
whitespace-separated checkout-relative C paths. Register each `*_test.c` or
`*_fuzz.c` in every lane that must run it. Nested paths keep their directory
structure in the binary and, for fuzzers, in `fuzz/corpus/`. The isolated L0–L4
checks must cover every layer. L5 uses `src/main.c` as its link entry point.

An empty Law-2, TSan or fuzz lane requires its corresponding `*_EMPTY_WAVE`
assignment with the first wave due. Remove that assignment when registering
the first suite. Wave 1 brings the guard and threaded modules from
`docs/PLAN_V2.md` §5–6, and the parsing and mutation modules that require
fuzzers under Law 1. The inventory check runs before lane executables, even
when binaries are cached. It verifies that listed sources still exist and
that every discovered suite is registered. It also requires Law-2/G1 and
plan-listed threaded suites to appear in their respective lanes. Each
runnable lane compares its completed run count to its manifest.

Wave 0 has no allocation guard implementation in `src/base`, so there is no
runtime state to read. `check-law2` rejects a nonempty Law-2/G1 manifest until
the guard and its release test integration land. That integration must read
the `edit_malloc_guard` result from the same `gcc -O2` process that exercises
the typing path and require `active && count == 0`. A successful suite exit
alone provides insufficient evidence that the guard observed allocations.

The dependency lints cover C translation units recursively under `src/`,
`tests/` and `fuzz/`, including unlinked helpers. The Make source lists prune
`tests/lint/`, `tests/symbols/`, `tests/lanes/` and `fuzz/corpus/`. The first
three belong to their fixture runners and the last contains fuzzer inputs.
The real test and fuzz include audit receives the same source lists as the
compiler and reuses the compiler dependency reader from `check_dag.py`.

`make check-lints` checks exact paths from the first column of the
`tools/README.md` registry against files under `tools/`. The registry and
generated `__pycache__/` directories are exempt. It also rejects any directory
named `variants` outside the top-level `.git/`, `build/` and `vendor/` trees.
The remaining no-fossils rules about module generations and environment
feature matrices require review of the code's behavior.

## W0.7-B7 (2026-11-23): overlap, seeds, registry paths, no count assertions

Findings 11, 12, 18 and 26 of `docs/reviews/wave0-code-astra.md`.

- **Required overlap.** Membership in the union of lanes is not enough: every
  `layers` suite must also be in `san` (`REQUIRED_OVERLAP` in
  `tools/check_layout.py`), because an isolated link proves the layer DAG, not
  the absence of memory errors. Only this overlap is declared; TSan-only suites
  stay legal.
- **Seeds.** Each fuzz suite's corpus `fuzz/corpus/<name>/` needs at least one
  regular, non-symlink file that opens for reading. libFuzzer replays an empty
  directory as one empty input and exits 0, so `-d` was no evidence.
  `check-suites` checks this, so the miss fails at the inventory before any
  fuzzer runs.
- **Registry rows.** Every data row of the one table in `tools/README.md` is
  parsed. Its first cell is exactly one backticked repo-relative path
  (`tools/…` or `tests/…`): not absolute, no `.`/`..`, no doubled or trailing
  slash, and no symlink anywhere on the path. Containment is decided on the text
  and the resolved path before existence is tested. A malformed first cell, such
  as the former compound "`a` and `b`" rows, is an error. Before this change those
  rows were skipped, and `tests/contract/xdriver.py` could disappear unnoticed. The
  convention is "repo-relative" because it is the only one that holds for files
  inside and outside `tools/`.
- **Exact diagnostics.** `tests/lanes/run.py` first requires `check-layout` to pass
  on the unmutated copy, which now includes `tests/lanes/`. Each red must then
  print exactly its expected `…: error:` lines. The Law-2 guard reds must fail in
  `law2_base_test` with its own guard line. Without this check, the stale-binary
  fixture passed while it also broke sorting.
- **No count assertions.** The lane recipes run binaries under `set -e`, so the
  first failing binary fails the lane. The run counter and expected count came
  from the same Make expansion, so the comparison could never fail. It is
  deleted. The recipes still print the count. `check-suites` remains the
  independent inventory, including `--required-layers`.
  `tests/lanes/run.py` "lane binaries" proves that a failing binary in the
  middle stops the lane and that later binaries do not run.
