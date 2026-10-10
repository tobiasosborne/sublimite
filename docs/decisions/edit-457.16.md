# edit-457.16 — session 8 finisher decisions

The integration design and contract mapping live in [P4.I](P4.I.md).
The final session evidence lives in [the worker report](../worker-reports/edit-457.16-s8.md).

The supplied WIP already contains the asynchronous missing-file completion fix
and the unique-directory regression fixture. Keep both. File lookup runs on a
worker, so only its installed `file_errno` can establish ENOENT; UI-thread errno
is not evidence of that lookup's outcome. Only FILE_ERR_IO/ENOENT creates a new
named buffer from supplied initial bytes. Other errors abort preparation before
tab publication. This is a code integration correction, not a raster workaround.

To verify regression sensitivity, temporarily remove that completion fallback,
run the existing fixture to failure, restore it byte for byte, and rebuild.
The final source has no additional change from the supplied WIP. Do not invent a
second implementation or widen a finisher because its initial suite is green.

Measure the existing null/raster loop rows back to back once, in TRACK mode on
the loaded AC box, on Xvfb :99. Keep every sample; do not turn noisy thresholds
or the TRACK process exit code into a timing acceptance verdict. G3 must include
minimap composition and end at T5 after the backend fence. The second invocation
row includes exec, parsing, loop acceptance, ACK, exit and reap, and uses a null
incumbent. It does not establish raster handoff latency or optical latency.

Honor the explicit no-new-scope boundary. Newer contracts on main add launcher
wait and disconnected-token sweeping to IPC, and worker-prepared cached fill
and approximate byte navigation to minimap. Those are inherited integration
gaps, documented for coordination, not silently claimed as implemented or
added to this narrowly authorized finisher. Renderer/legacy benchmark tuning
and leak-enabled validation likewise remain separate work.
