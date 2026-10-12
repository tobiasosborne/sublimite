# scroll: STATUS (edit-91c, 2026-10-13)

Done: resident windows choose WORK_FOREGROUND when the pool has the lane and
every page of the window is in core (mincore probe, <= 16 spans), else
WORK_BULK; `tests/scroll_test.c resident_lane_choice` covers both halves.
Remaining (outside this module): page motions during an index build wait on
view's exact cursor-line query; see docs/decisions/edit-91c.md "Remains".
