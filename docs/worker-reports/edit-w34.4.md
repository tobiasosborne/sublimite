# edit-w34.4 — Astra comparison drive

Completed the comparison drive and wrote the single review at [docs/reviews/astra-vs-sublime-1.md](../reviews/astra-vs-sublime-1.md).

All UI interaction and capture used `DISPLAY=:99`. No `bd` or `git` commands were run. No files under `src/`, `tests/`, `bench/`, `fuzz/` or `tools/` were modified. Read-only source inspection was used to understand graceful shutdown, display routing, and the minimap's stale-color overlay. Driver/analysis helpers and fixtures live only under `build/astra/`; the requested two Markdown files are under `docs/`.

## What ran

- Copied the 1 MiB C fixture, 64 MiB dense-short fixture, PRD and editor source under `build/astra/files/`; wrote UTF-8 and small deterministic navigation/edit/clipboard fixtures there. Opened `/tmp/edit-corpus/log_1g.txt` in place. Final filesystem check still shows 1,073,741,824 bytes and the original date prefix, with no injected initial `Z` on disk.
- Drove both editors with `xdotool`: launch, typing at the supplied delay and burst flags, explicit Return, genuinely paced 100 ms character deadlines, navigation/selections, mouse and minimap, deletion/undo/redo, clipboard and cross-editor transfer, find/replace UI, save/disk reads, external append, CLI tabs, large-file navigation/typing, resize, SIGKILL/relaunch, minimap editing, and a logged minute of idle time in each editor.
- Used `--new-instance` for isolated sublimité tests. The CLI handoff test deliberately started its base instance without that flag, then opened five additional fixtures and attempted 100 more unique-file handoffs. All command exit codes are recorded.
- Captured 27 sublimité and 29 Sublime per-task MKVs, counted from the final video manifest. One additional MKV is a lossless derived moving-minimap strip for resize analysis: 57 task/derived videos total, excluding the owner's continuing whole-session recording.
- Ran `tools/flashscan.py` against every sublimité capture at the requested colors/tolerances and at the measured darker rendered color. The fixed minimap ROI is documented. A separate tracked crop follows the minimap through the resize sequence and is scanned as well.
- Extracted thirty-frame sequences around small-file isolated/slow/burst glyphs and completed large-file glyphs. Measurements are explicitly 60 Hz resampled video intervals, with command envelopes and capture limitations, not claimed hardware-latency precision.
- Enabled `EDIT_TRACE_DUMP` on sublimité launches, sent WM_DELETE_WINDOW for graceful completion, and ran `build/tools/tracedump` on resulting rings. SIGKILL intentionally cannot emit a normal exit trace; the relaunch trace is not mislabeled as the killed edit's trace.
- Counted idle image changes outside the caret neighborhood and inspected text results. Read stills only for text/layout; flashing and latency conclusions come from video sequences/scans.

## Where the evidence is

- Videos: `build/astra/video/`.
- Authoritative inventory: `build/astra/logs/video-manifest.json` (ffprobe dimensions/rates/duration/size for every task/derived video).
- Input and observation logs: `build/astra/logs/<video-stem>.json`; video-relative summaries: matching `.timeline.txt`.
- Flash outputs: `build/astra/logs/<stem>.flash-{bf853d,ff8800,6b5030}.txt`; index: `flash-index.json`. Resize: `11-resize-sublimite.tracked-flash-*.txt`, `resize-tracking.json`, and `video/11-resize-minimap-track.mkv`.
- Quantitative onset results: `startup-fresh-metrics.json`, `startup-metrics.json`, `latency.json`, `more-latency.json`, `large-metrics.json`, `large-typing-latency.json` under `build/astra/logs/`.
- Raw trace rings and tracedump output: `build/astra/traces/`.
- Readable snapshots and thirty-frame sequences: `build/astra/frames/`; some cropped/contact-sheet inspections are also in `build/astra/`.
- Driver and analysis source: `build/astra/drive.py`, `tasks.py`, `tasks_extra.py`, `focus_guard.py`, and named measurement/scan scripts. These are evidence helpers, not changes to the product or its test suite.

The owner's whole-session file is `build/astra/video/session-085506.mkv`, recorder PID 1169353. It was not stopped or replaced.

## Failures, repeats and limits

- Ordinary sandbox commands could not connect to the X display. Elevated commands connected to the requested `:99`; no automatic approval rejection occurred. No alternative display was used.
- The initial exact-title match did not discover the sublimité window. Switching to a title prefix fixed discovery. The earliest `01-open-sublimite.mkv` is incomplete and full decoding warns “File ended prematurely”; it remains in the inventory and flash scans as a failed attempt, not the primary launch result. `01-open-r2` was too short; later warm and fresh-path repeats establish the reported startup observation.
- A one-shot tool command did not preserve launched child processes beyond its lifetime. The drive moved to a persistent Python/PTY session. ffmpeg then received `-nostdin` so it would not consume subsequent driver commands from that PTY.
- Python Xlib's filesystem socket initially refused the connection although the X client tools worked. The test-only close helper connected to the X server's abstract Unix socket, and used a synchronous WM_DELETE_WINDOW send. Initial graceful-close evidence was therefore incomplete; later traces were obtained normally.
- Literal newlines inside `xdotool type` did not create the intended lines in either editor. The typing repeat uses explicit Return keys. The requested `--delay 100` command was also faster than ten characters per second in this environment; a separate deadline-paced run measured approximately ten character intervals per second in both editors.
- Sublime's initial 1 GiB video remained blank during its recorded observation. Large-file loading subsequently delayed new windows; the first UTF-8 comparison launch exceeded the harness's window-discovery limit. Its per-task recorder was stopped with SIGINT; the loaded window was later tested in `utf8-r2-sublime.mkv`. Large-file editing was resumed after loading in `10-large-resume-sublime.mkv`. No full initial Sublime load time is invented.
- A longer-dwell Sublime crash-recovery repeat also timed out during window discovery while restoration of the large session was busy. Its recorder was stopped with SIGINT. The short-delay SIGKILL comparison ran in both editors; the longer-dwell sublimité repeat ran. The report identifies the extended Sublime attempt as incomplete. The owner's pre-existing Sublime PID 1168575 was explicitly protected from the crash test.
- The capture stack drops samples and has PTS gaps; encoder drop counters alone are incomplete. Numerical timing claims disclose this and use video timestamps/60 Hz resampling. Concurrent whole-session recording and raster fallback are material test conditions.
- The requested orange RGB values yielded false negatives. The report retains those results, plus corrected-color scans grounded in measured on-screen pixels. Fixed-ROI resize counts are not presented as full coverage; the tracked minimap scan supplies that supplemental check.
- A suspected selection-anchor problem from a scaled preview was **rejected** after a minimal repeat and exact selection-background pixel checks. It is not listed as a product defect.
- Successful sublimité near-end typing latency on the 1 GiB file could not be established: no confirmed glyph onset in the guarded repeat. R/K external-change choices, exhaustive wrap behavior, memory benchmarks and other rendering backends were not comprehensively tested. The report states these limits.

The substantive verdict is **no**: saving, clipboard, text mouse/wheel, find/replace and Unicode are immediate daily-use blockers, with substantial minimap flashing, large-file interaction and handoff defects also demonstrated.

Final cleanup gracefully closed the leftover test-only sublimité window and restored the owner’s original scratch window on `:99`. The whole-session ffmpeg PID was checked alive after cleanup; no per-task capture process remained. Cleanup actions are in `build/astra/logs/cleanup.json`. Wall-clock completion timing is in `build/astra/logs/run-summary.json`; the drive finished inside the requested 80-minute budget.
