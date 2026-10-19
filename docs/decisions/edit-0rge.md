# edit-0rge: lineidx_test LeakSanitizer failure (14768 B from file_open_begin)

**Owner: the test, not production.** `test_combined_ownership` (tests/lineidx_test.c,
since edit-4w1.57, 2026-10-10) opens `/tmp/edit-corpus/sparse_10g.bin`. In the
session-19 run the corpus directory was absent (every `test_corpus` call also
printed `skip`). `file_open_begin` is asynchronous: it allocates the handle
(file.c:864 calloc), returns FILE_OK and reports the missing file later as
FILE_MSG_OPEN_FAILED, so `file_open_ready` never becomes true. The
`REQUIRE(file_open_ready(f) && ...)` then `return`ed past the test's
`file_close(f)`, abandoning the handle. The suite was already failing on that
FATAL ("1 failure(s)"); the leak was its consequence.

Production check: with only the test change that calls `file_close(f)` before
the REQUIRE returns, the failed-open handle is released completely (LSan
clean, suite still 1 FATAL). The cancel/close path of file.c needed no change.

Decisions:
- On the not-mapped / wrong-size paths the test closes `f` before REQUIRE
  returns (the size REQUIRE two lines below abandoned the same handle).
- An absent fixture is a `skip`, matching `test_corpus` in the same file; the
  G10f combined census only runs when `tools/mkcorpus` has populated
  `/tmp/edit-corpus`. A present-but-unopenable fixture still fails.
