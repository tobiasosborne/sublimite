Fixed the journal/file contract integration. Full `make check` passes with LeakSanitizer enabled.

The fixture assumed `file_open_begin` returned a ready file. Its failed assertion exited without cleanup, leaking the file and prefix result. It also relied on `journal_flush` to finish an independent file save.

Changed files:

- [tests/journal_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.42/tests/journal_test.c): explicit mailbox waits, phase assertions, failure cleanup, and copy/mmap/empty/error/unread-close regressions.
- [P1.7d.md](/home/tobias/Projects/editor/.wt/edit-4w1.42/docs/decisions/P1.7d.md): contract, ownership audit, and verification evidence.
- [file/STATUS.md](/home/tobias/Projects/editor/.wt/edit-4w1.42/src/file/STATUS.md) and [journal/STATUS.md](/home/tobias/Projects/editor/.wt/edit-4w1.42/src/journal/STATUS.md): updated integration status.

No production source or header changes were needed.

Local red runs, (M)[AC, Not charging], launch loads respectively `5.99` and `6.93`:

```text
journal_test:316 FAIL file_open_begin(&pool,target,NULL,&f)==0 && file_open_ready(f)
journal_test:326 FAIL file_open_begin(&pool,target,NULL,&f)==0 && file_open_ready(f)
```

Coordinator aggregate red, power/load not supplied:

```text
journal_test:950 FAIL retry_test()==0 && barriers_test()==0 && save_test()==0 && worker_test()==0 && cadence_test()==0 && save_names_test()==0 && save_shared_base_test()==0 && retained_external_test(false)==0 && retained_external_test(true)==0 && retained_copy_test()==0
```

Green results; all commands used `DISPLAY=:99 EDIT_DISPLAY=:99`. Stamps are (M)[AC, Not charging]:

```text
make all exit=0                         launch load1=20.50
journal_test: save ok (file_save_begin, retained generation, post-save edits, two buffers, marker/rename/dir failures)
journal_test: file mailbox ok (copy, mmap, empty, open failure, unread close, independent save wait)
check: 39 test binaries passed
test_replay_cli: all passed
make check exit=0; detect_leaks=1        launch load1=19.98
fuzz: 21 fuzzers built
make fuzz exit=0                        launch load1=20.50
Done 11486605 runs in 121 second(s)
file_fuzz exit=0                        launch load1=18.14
```

LSan aborted inside the sandbox; the full green run used outside-sandbox execution. A forced paused-save assertion failure also cleaned up without ASan/UBSan/LSan findings, (M)[AC, Not charging; load1=13.87].

Benchmarks were not rerun for this fixture/docs fix. Open integration issue: the supplied snapshot lacks the advertised P1.9g section/synchronous append and P1.7f bench adaptation. Existing file proposals and undo’s group-atomicity review skip remain unchanged.