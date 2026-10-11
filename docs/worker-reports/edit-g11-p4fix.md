# edit-g11 p4 fix

Cause: no production bug. Splitting the assert showed minimap_stale clears after the index completes (the first half passes); only frames.coherent failed. The map_submitted hook in tests/editor_p4_test.c required the last tab-strip cell to be '!' exactly when the minimap is stale. g11 deliberately removed that '!' (compose_chrome in src/editor/paint.c), so the raster pass, which has one stale frame, flagged it as incoherent. The null pass sees the stale frame too (stale frames=1) but it passed. Not investigated why.

Fix: tests only. The hook now asserts the last cell is never '!', which is g11's guarantee. The stale-state half of the check is unchanged. The expectation was obsolete, so this is a test change, not a weakening of the stale-clears check. No src changes.

Red (before): editor_p4_test:329 FAIL !editor_get_stats(e).minimap_stale && frames.coherent ; :547 FAIL index_recovery(raster) == 0
Split run before the fix: only "FAIL frames.coherent".
Green: editor_p4_test null+raster index recovery pass (stale frames=1); editor_test all passed; --minimap-flash orange_frames=0 chrome_markers=0 mallocs=0; minimap_test ok; make all (gcc -Werror) ok.
