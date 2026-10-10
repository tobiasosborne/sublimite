# edit-2vs: raster fence job outlived the backend state

Cause. The CPU raster backend (src/raster/raster.c) submits one fence job per
presented frame; it reads `st->fence` and the xcb connection. Frame completion
can reach the render adapter without that job: the platform loop delivers
Present PRESENT_COMPLETE to the adapter directly (bench/file_bench.c pump_view
-> present_done -> render_backend_event), and DEVICE_DONE is only the fence
job's first message. The adapter then reports the frame inactive while the fence
job is still queued or polling for the idle event. `cpu_submit` of the next frame
did `st->nhandles = 0`, forgetting that handle (and overwriting `st->fence`
under it); `cpu_shutdown` only joined the current frame's handles, so
`free(v->state)` raced a running or queued fence job (ASan: read of freed memory
in fence_job_fn, seen in file_kill_test).

Fix. `cpu_join_jobs()`: cancel every retained handle, wait until
`work_handle_finished` for each, then clear the list. Called at the top of
`cpu_submit` (after argument checks, before the snapshot is rewritten) and in
`cpu_shutdown`. A cancelled fence job returns at its next `work_should_stop`
poll (at most about 1 ms); a queued one is removed immediately. Shutdown now
also waits on `finished_epoch` instead of the busy/epoch pair.

Consequence. The previous frame's fence job is cancelled once the next frame
is submitted, so an idle notify that has not been consumed by then is ignored
(the new job matches the new serial). Behaviour of the single reused pixmap is
unchanged otherwise.

Test. tests/raster_test.c fence_outlives_test holds the fence job QUEUED by
occupying all four raster workers, completes the frame through
render_backend_signal (the platform path), submits frame 2, shuts down and
frees the state, then releases the workers. No timing dependence.
