# tools

## mkcorpus

`mkcorpus.c` writes the perf corpus from `perf/01-perf-target.md` section 4 into a directory. Standard C only, no project headers.

```
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g tools/mkcorpus.c -o mkcorpus
./mkcorpus [--small] [--huge] [DIR]      # DIR defaults to /tmp/edit-corpus
```

- Output is deterministic (seeded xorshift64 per file); the same bytes every run.
- Files that already exist with the expected size are skipped.
- `--small` scales sizes by 1/64 (except `needle.txt`) for quick tests.
- `--huge` also writes `oneline_10g.txt` (10 GiB, one line). Without it only `oneline_1g.txt` is written.
- Streams through a 1 MiB buffer, so memory stays flat. Exits non-zero on any write error.

Files: `ascii_code.c` (1 MiB C source), `unicode.txt` (1 MiB, precomposed and combining marks, CJK, ZWJ emoji, Arabic/Hebrew, bidi and zero-width controls), `malformed.txt` (1 MiB, injected truncated, overlong, lone-continuation, surrogate and 0xFE/0xFF bytes), `dense_short.txt` (64 MiB, lines of 0-3 chars), `crlf.txt` (1 MiB, CRLF), `log_1g.txt` (1 GiB, 120-byte timestamped lines), `oneline_1g.txt` (1 GiB, no newline), `oneline_10g.txt` (`--huge` only), `periodic.txt` (256 MiB, `a` with a `b` every 32 bytes at offset 37 mod 32), `needle.txt` (32 bytes: a x31 then b).
