# edit-e6x.27 gl bench honesty (review gl-1 BLOCKER 7, MAJOR 9-11, 15)

Design choices:

- Gate logic lives in `bench/gl_gate.h` (pure, header-only, no display) so
  `tests/gl_gate_test.c` runs it under ASan/UBSan in `make check`.
- Evidence tags are evidence, not switches. `gl_gate_judge` always compares the
  limits; verdicts are PASS, PASS_PARTIAL, MISS, UNKNOWN (power unknown, limits
  met) and REFUSED (fewer samples than required). Only MISS fails the process;
  non-qualifying statuses are printed as such and never as PASS.
- p99 is compared as `p99 * 180 <= 1e9` (exact rational, equals floor 5555555 ns).
- Minimap is not rendered by this bench. The 180 000 ns allowance (E) is deducted
  from the G3 p50 and p99 budget, and rows are PASS_PARTIAL with `g_claim=no`:
  partial-operation evidence, no G3 compliance claim.
- Ingress is the first statement of the timed snapshot; the scroll shift (or typing
  cell edit) is inside it in every mode.
- Required samples: 10 000 per interaction scenario; `--quick` therefore REFUSES.
  G3i takes 5 samples (isolated, 15 s apart) and requires 5.
- Typing: T4 (submit + present return) is judged against G1 1/2 ms as a separate
  partial row; T5 typing stays a tracked row.
- Bulk: the work pool admits a single bulk worker, so "one active, rest queued"
  means one looping job (index/find/save kernels in 256 KiB chunks, polling
  `work_should_stop`) and 0 or 2 jobs queued behind it. Overlap is verified (chunk
  counter advances during the window, no job finished, queued ones never ran);
  otherwise the row is REFUSED. `--bulk-self-check` verifies this without GL.
- No GL context under Xvfb here (EGL has no DRI3): the bench prints SKIP and exits 2.
  No numbers were produced on this box for the new GL rows.
