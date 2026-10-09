# edit-4w1.46c finishing run (Sonnet) -- P1.6b lineidx review fixes

Result: all findings in scope were already fixed on main by 741ba09 and tested, except
review §13 (deferred, needs src/work). No source change was needed. Docs only.

Changed files: docs/decisions/P1.6b.md (per-finding table appended),
src/lineidx/STATUS.md (status). src/lineidx/*.c, tests, fuzz restored byte-identical
after temporary reverts (cmp checked).

## Red (hunk reverted) / green, ASan+UBSan, leaks on, DISPLAY=:99
- §1 without `pieces > cap - kept` preflight: ASan heap-buffer-overflow (--review=1). Green: ok.
- §2 without checked total: 4 FAIL (tests/lineidx_test.c:144,145,146,160). Green: ok.
- §3 `epoch > h.epoch+1`: `:182 FAIL running`; `epoch >= h.epoch+1`: FAIL !released + heap-use-after-free. Green: ok.
- §4 test leasing the shared buffer: heap-use-after-free READ 16 in scan_count, freed in model_edit. Green: ok.
- §5 `aligned = lo`: FAIL :221, :229. Green: ok. Fuzz approximate-boundary traps present.
- §12 `x->n` for `x->cap`: FAIL :264 owned==observed, :295. Green: ok.
- §14 fuzz drain: red NOT reproducible. Current work_publish drops on a full mailbox and
  does not refuse submits, so a drain-disabled fuzzer on a 3000-build input no longer
  traps. Drains and the successful-submit assertion are present; green only.
- §13: `--review=13` still red (2 FAIL, opt-in). Not fixable in lineidx files: the slot
  epoch cannot distinguish an externally cancelled lease from a completed lease whose
  slot was reused (both h.epoch+1). Needs a src/work selective-receive API plus lineidx
  adoption; recorded as a proposal in P1.6b.md. Coordinator should keep/open a bead.

## Verification
- make all: rc 0
- ASAN_OPTIONS=detect_leaks=1 make check: rc 0, lineidx_test ok
- make fuzz: 20 fuzzers built
- build/fuzz/lineidx_fuzz -max_total_time=120: 17778 runs in 121 s, rc 0, no crash
- No bench run (bench/lineidx_bench.c untouched).

## Open problems
- §13 above. §6-§11 untouched by design (later bead). work-scan-1 §9 (bounded polling)
  and §14 lineidx parts: §14 covered; §9 is the same later bead as lineidx-1 §10.
