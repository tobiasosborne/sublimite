P1.3c frozen amendment is ready for stacking edit-4w1.37. Only the six allowed files changed:

- [piece.h](/home/tobias/Projects/editor/.wt/edit-4w1.39/src/piece/piece.h): appended contract.
- [checkpoint_stub.c](/home/tobias/Projects/editor/.wt/edit-4w1.39/src/piece/checkpoint_stub.c): intentional NOMEM stubs.
- [piece_model.h](/home/tobias/Projects/editor/.wt/edit-4w1.39/tests/piece_model.h): copy-based checkpoint oracle.
- [piece_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.39/tests/piece_test.c): new checkpoint rows.
- [piece_checkpoint_fuzz.c](/home/tobias/Projects/editor/.wt/edit-4w1.39/fuzz/piece_checkpoint_fuzz.c): transaction/NOMEM fuzz target.
- [P1.3c.md](/home/tobias/Projects/editor/.wt/edit-4w1.39/docs/decisions/P1.3c.md): rationale, status, and evidence.

Existing declarations and row bodies are byte-for-byte unchanged; `piece.c` hash is unchanged.

Final GCC and ASan/UBSan red run **(M)[AC], load 4.23**:

```text
piece_test: pre-P1.3c rows all passed
piece_test: checkpoint model passed
FAIL [checkpoint begin/commit] tests/piece_test.c:392: piece_checkpoint_begin(t, &c) == PIECE_OK && c
FAIL [checkpoint abort bytes/counts] tests/piece_test.c:437: piece_checkpoint_begin(t, &c) == PIECE_OK && c
FAIL [checkpoint nested/NULL RANGE] tests/piece_test.c:468: piece_checkpoint_begin(NULL, &out) == PIECE_ERR_RANGE && !out
FAIL [checkpoint begin NOMEM] tests/piece_test.c:497: piece_checkpoint_begin(t, &c) == PIECE_OK && c
FAIL [checkpoint abort failing allocator] tests/piece_test.c:532: piece_checkpoint_begin(t, &c) == PIECE_OK && c
FAIL [checkpoint snapshots survive abort] tests/piece_test.c:567: piece_checkpoint_begin(t, &c) == PIECE_OK && c
FAIL [checkpoint repeated abort memory] tests/piece_test.c:605: piece_checkpoint_begin(t, &c) == PIECE_OK && c
FAIL [checkpoint refs after abort] tests/piece_test.c:630: piece_checkpoint_begin(t, &c) == PIECE_OK && c
piece_test: FAILED
```

Green regression evidence: original frozen suite **(M)[AC], load 4.23**, and remaining sanitizer/CLI checks **(M)[AC], load 3.87**:

```text
piece_test: all passed
remaining sanitizer binaries: result=0
test_replay_cli: all passed
```

Build results:

- `make all`: PASS, exit 0 **(M)[AC], load 4.37**.
- `make check`: intentional RED, exit 2; only new checkpoint rows fail **(M)[AC], load 5.50**.
- `make fuzz`: PASS — `fuzz: 16 fuzzers built` **(M)[AC], load 4.37**.

Fuzz evidence:

- Existing piece fuzzer: 1000 completed smoke executions, 0.513 s wall time **(M)[AC], load 5.50**.
- New checkpoint fuzzer: intentional healthy-begin assertion on empty input, exit 77, 0.213 s wall time **(M)[AC], load 4.23**. No completed checkpoint campaign claimed.

Benchmarks were not run; this amendment changes no kernel hot path.

Open work: edit-4w1.37 must implement checkpoints and delete the stub. LSan was disabled as instructed; live X11 rows skipped because the sandbox could not connect/start Xvfb. Coordinator reruns remain necessary. Amendment status is recorded in the decision; `STATUS.md` was outside the allowed file list.