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
