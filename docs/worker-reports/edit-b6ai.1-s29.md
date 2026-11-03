# edit-b6ai.1 worker report (session 29)

Box: monolith, Arch/Omarchy, gcc 16 / clang 23, libxkbcommon 1.13.2, THP=always, no battery [AC, desktop monolith].

## Changed files
- src/os/os_posix.c: os_vm_reserve applies madvise(MADV_NOHUGEPAGE) once at map time (guarded by #ifdef MADV_NOHUGEPAGE). Linux/posix only.
- tests/savectl_test.c: reload_memory_bound disables THP (prctl PR_SET_THP_DISABLE) around its measurement, sanitizer builds only; bound unchanged.
- tests/keys_test.c: version-tolerant oracle helpers host_fold / host_name (exact pair list of known skews).
- docs/decisions/edit-b6ai.1.md: causes, choices, alternatives.

## Red runs (before, same box)
- undo_test (san): `review 3: cleared resident pool=1093632 allowance=4096`, CHECK n<=4096 fails (undo_test.c:281).
  undo_test (release, gcc, stash of the fix): `review 3: cleared resident pool=761856`, same failure: a product footprint issue, not a sanitizer artefact.
- savectl_test (san): `reload anonymous ownership delta=5435392 .. 7540736 bytes (M)[AC], bound=2097152 bytes (G)`, fails at savectl_test.c:938.
  The release build of the same test on the same box: `delta=20480 bytes`, passes (the excess is the ASan allocator's 2 MB faults).
- keys_test (san): `tests/keys_test.c:303: assertion failed: keys_keysym_name(value,actual,sizeof actual)==n`.
  Enumerating the table against the host found 29 names that differ, plus two format differences (see the decision note).

## Green runs (after)
- undo_test review 3: `cleared resident pool=0 allowance=4096` (san and release).
- savectl_test: `reload anonymous ownership delta=1286144 bytes (M)[AC], bound=2097152 bytes (G)`.
- keys_test: `2572 generated names and 2442 canonical values checked both directions against libxkbcommon`, all passed.
- `make all`: ok. `make TARGET=win all`: ok (rc=0, llvm-mingw).
- `make check`: `check: 117 test binaries passed in 90.1 s (parallel, 36 jobs, 6 displays)`; replay CLI test passed; make check exit status 0.
  Note: in the tail, tools/test_display_lock.sh contract printed display-lock timeouts ("held by 35391") because another worker held :99 at the same time; the contract still ended `PASS` and make exited 0. Not a failure of this bead.

## Measurements
Undo pool resident after clear + set_cap(1), mincore: 1,093,632 B (san) / 761,856 B (rel) -> 0 B. Reload anonymous delta (san): 5.4 to 7.5 MB -> 1.29 MB with THP off (the 2 MB figure is the bound, FILE_PREFIX_MAX = 1 MiB doubled).
Typing path: no change in src/ outside one madvise in os_vm_reserve (pool/arena creation); G1 and mallocs=0 tests in `make check` pass.

## Found (not fixed)
- The pinned keysym header (vendor/xkbcommon, 1.6.0 naming) lacks keysyms added since (dead_apostrophe, dead_lowline, dead_*verticalline, XF86Accessibility, XF86Fn_*, SSHARP, left/rightsingleanglequotemark, ...) and still has osfReselect that 1.13.2 dropped. Config keymaps cannot name the newer ones. Bumping the pin also changes Windows tables; its own decision.
- NOHUGEPAGE on every os_vm reservation removes 2 MB pages from arenas/pools on THP=always kernels (TLB reach). Not benchmarked (no lag observed; law 1).
- Sanitizer measurements of memory under THP=always are dominated by the ASan allocator: other tests with anonymous-memory bounds may show the same on this box.
