# Review: session-3 typing-path commits (Sonnet bug hunt)

Scope: f363227 (P0.6 trace/replay), 3173f0e (P2.2 x11 input), 74e13a6 (P2.3b font fallback + arena raster).
Method: read src/x11/{input,xi2,clip,x11}.c, src/trace/*, src/font/{font,fallback}.c plus the touched tests; ran
build/tests/font_test and compared against `xinput list` on the live X server. No BLOCKERs found.

## Summary

| # | Sev | Location | Description |
|---|-----|----------|-------------|
| 1 | MAJOR | src/x11/x11.c:372-411 (+ clip.c:92) | Events already buffered inside xcb are invisible to poll(); any reply wait made outside the dispatch loop (plat_clip_set from an on_event callback) can strand queued key events until the next fd activity |
| 2 | MAJOR | src/font/font.c:167 + vendor/stb_truetype.h:3307 | Arena exhaustion with glyph width > 64 px: STBTT_malloc returns NULL and stb dereferences it (SEGV) instead of FONT_ERR_NOMEM |
| 3 | MINOR | src/x11/x11.c:326-328, xi2.c:303-307 | XI_DeviceChanged feeds time_ms = 0 and mods = 0 into the clock mapper and xkb state, resetting the X-time to T0 mapping |
| 4 | MINOR | src/x11/xi2.c:246-253, x11.c:111 | Scroll classes are keyed by sourceid only and QueryDevice(0) returns master and slave infos, so entries can overwrite each other when valuator numbers differ |
| 5 | MINOR | Makefile:91-98, tests/font_test.c:104-127 | The font Law-2 (zero malloc) assertion never runs under `make check` (ASan: skipped) or `make bench` (font_bench has no guard) |
| 6 | MINOR | src/x11/clip.c:192-198 | Stale SelectionClear (older than our latest ownership) frees the newly set clipboard data |
| 7 | MINOR | src/x11/clip.c:200-221 | No UTF8_STRING to STRING fallback; INCR reply deletes the property and leaves the owner blocked; unsolicited SelectionNotify accepted |
| 8 | MINOR | src/font/fallback.c:98-104, 148-162 | Fallback ignores FC_INDEX (CJK .ttc loads face 0 = JP although "zh" was asked); whole file fread into arena; failed read leaves arena bump |
| 9 | MINOR | src/font/font.c:84-125 | Font vertical metrics and glyph boxes unvalidated: negative ascent/descent cast to uint32_t, a==d gives inf scale then UB in round_f; stb has no bounds checks on malformed files |
| 10 | MINOR | src/font/fallback.c:112-135 | font_fallback_discover never clears `done` first; reusing an fb struct races the reader against the rewrite of cjk[]/emoji[] |
| 11 | MINOR | src/x11/x11.c:274-281 | FocusIn/Out with grab modes (NotifyGrab/WhileGrabbed) clear held-key state although keys are still down |
| 12 | MINOR | src/x11/input.c:122-127 | Key swallowed by compose (modifier or dead key) still produces a release event: stray KEY_UP without a KEY_DOWN |
| 13 | MINOR | tools/replay.c:62-68, src/trace/trace.h:90 | `--speed=inf` accepted by the tool but rejected by trace_replay with no message (exit 2); trace.h says wheel dx/dy are 16.16 but plat uses 1/256 notch |
| 14 | MINOR | src/x11/input.c:31-38 | x11_input_init failure leaks ctab/cstate (ownership transferred before the fallible step) |

## 1. MAJOR: events buffered in xcb are not seen by poll() (x11.c:372-411, clip.c:92)

`plat_run_for` polls the xcb fd, then drains `xcb_poll_for_event`. Any `xcb_*_reply` / `xcb_wait_for_reply`
reads the socket and parks unrelated events in xcb's internal queue; the fd is then no longer readable, so the
next `poll()` sleeps although events are pending. Inside `dispatch` this is harmless (the `while (xcb_poll_for_event)`
loop keeps going), but `plat_clip_set` is called by the application from `cb->on_event` (invoked by `drain()`, after
that loop has finished) and does two blocking round trips (`xcb_get_selection_owner_reply`, up to four with the retry).
Key presses arriving in that window are queued inside xcb and not delivered until some other fd (blink timer, work fd,
next X event) wakes the loop. On a typing path this is a stuck or late keystroke after Ctrl+C / Ctrl+X.
The same applies to events read while `plat_init` waits for replies before the loop starts.

Fix: at the top of each loop iteration (before `poll`) and after any callback that may call plat_* functions, run
`while ((e = xcb_poll_for_event(c))) dispatch...`; or use `poll` timeout 0 whenever `xcb_poll_for_event` returned
non-NULL last time. Add a regression test using a mock that queues an event during a reply wait.

## 2. MAJOR: NULL deref in stb on arena exhaustion (font.c:158-171, stb_truetype.h:3307)

`font_stb_alloc` returns NULL when the arena is full and sets `ctx->failed`, relying on stb to cope. Most stb
call sites check, but `stbtt__rasterize_sorted_edges` does not:
```
if (result->w > 64)
   scanline = (float *) STBTT_malloc((result->w*2+1) * sizeof(float), userdata);   /* may be NULL */
scanline2 = scanline + result->w;
...
STBTT_memset(scanline , 0, result->w*sizeof(scanline[0]));                      /* NULL write */
```
Reachable whenever the glyph bitmap is wider than 64 px (px up to 4096 is accepted by font_set_px, or wide CJK/emoji
at large sizes) and the arena has room for the bitmap but not for the edge/scanline scratch. The error path is
documented as returning FONT_ERR_NOMEM (`if (ctx.failed) return FONT_ERR_NOMEM`) but it crashes first.
Fix: before calling stb, require `arena_free >= out->w*out->h + K*(w+h+verts)` (conservative bound, e.g. 64 KiB +
16*w*h), or patch the vendored stb to check `scanline == NULL` and bail, or hand stb a fixed scratch block of
known worst-case size. Add a test with a tiny arena and a w>64 glyph.

## 3. MINOR: DeviceChanged corrupts the clock mapping (x11.c:326-329, xi2.c:303-307)

`xi2_decode` returns early for type 1 with `r->time_ms == 0` and `r->mods == 0`. `dispatch` then unconditionally calls
`x11_clock_map(&clock, 0, now)` and `x11_mods_from_state(.., 0)`. With ref_ms != 0 the signed distance is huge, the "in
the future / stale" branch fires and the reference becomes (0, now). The next real event (x_ms ~ 1e8) is again far in
the future and re-syncs, so that event's T0 is `now` (ingest time, latency hidden) rather than the physical time; the
xkb state is also zeroed until the next event syncs it. Fix: in dispatch, skip clock/mods work when `r.device_changed`
(or only run it when `r.time_ms != 0`); better, parse the DeviceChanged time (offset 12) in xi2_decode.

## 4. MINOR: scroll class table keyed by source id only (xi2.c:246-253, x11.c:111)

QueryDevice(0 = XIAllDevices) returns master and slave devices. Each slave's ScrollClass has sourceid = itself with
the slave's valuator numbers; the master carries a copy for the most recently used slave with the master's numbers.
`dev_for(x, src)` merges them into one entry, last writer wins. On this machine (`xinput list`: touchpad id 9, mouse 10,
trackpoint 14) the numbers coincide (2/3 everywhere) so it works, but a device whose master numbering differs (tablet,
or any device attached after another one with scroll axes) gets wrong `num[]`, so scrolling is silently dropped
(`d->num[a] != bit`). Events come from XIAllMasterDevices, so only the master numbering is valid. Fix: query deviceid 1
(XIAllMasterDevices) only, or ignore classes whose DeviceInfo.deviceid is a slave; rescan on DeviceChanged (already done).
The unit test only exercises one slave device, so this is untested.

## 5. MINOR: font Law-2 check not enforced by the gates (Makefile:91-98, font_test.c:107-127)

`make check` builds only ASan binaries, where the malloc guard is compiled out (font_test prints "skipped"). `make bench`
runs font_bench, which has no guard. The "0 mallocs per glyph" claim therefore holds only if someone runs
build/tests/font_test (release) by hand. I ran it: `font_test: mallocs during 600 rasterisations: 0`, so the property holds
today, just not under any binding gate. Fix: add the guard count to font_bench (exit non-zero as x11_bench does), or add
the release test binaries to `make bench`.

## 6. MINOR: stale SelectionClear clobbers fresh ownership (clip.c:192-198)

`SELECTION_CLEAR` frees `own[w]` without comparing `x->time` to `own_time[w]`. Sequence: another client takes the
selection (Clear queued), the user copies again before the Clear is dispatched (`plat_clip_set` succeeds, new data),
then the old Clear is processed and frees the new data while the server still lists us as owner; later requests are refused.
Fix: ignore the event when `x->time != 0 && own_time[w] != 0 && (int32_t)(x->time - own_time[w]) < 0`.
Related ICCCM nit: SelectionRequest with `rq->time` older than `own_time` should be refused (serve()).

## 7. MINOR: clipboard receive gaps (clip.c:200-221)

- Only UTF8_STRING is requested; owners that lack it (xterm, older Java) yield property None and a failed paste, with no
  retry as STRING (the converter code for STRING exists but is never requested).
- INCR (type == a_incr): the get_property uses delete=1, which starts the transfer, and we never listen for
  PropertyNotify (not in the event mask), so the owner blocks mid-transfer. Documented as a gap, but an "INCR then fail"
  paste also leaves the owner wedged; at least do not delete on INCR, or finish by deleting chunks.
- SELECTION_NOTIFY is accepted without checking `requestor == p->win`/`target`, and an unsolicited notify overwrites
  `got` and raises a CLIPBOARD event.
- `plat_clip_set` failure path frees the data but we may still be the real owner (set succeeded, owner query raced),
  after which requests are refused with no SelectionClear coming.

## 8. MINOR: fallback face selection (fallback.c:98-104, 148-162)

Verified: `fc-match ':lang=zh:charset=4e2d'` returns `NotoSansCJK-Regular.ttc index=0 family=Noto Sans CJK JP`.
The code only copies the "file" property; FC_INDEX is dropped and font_init always uses face 0 (JP), so Han-unified glyphs
render with Japanese forms even when the user asked for zh. Fix: read "index" (FcPatternGetInteger), store it in
font_fallback, pass it to stbtt_GetFontOffsetForIndex.
`font_load_file` freads the whole (~20-60 MB) file into the arena; a short read returns NULL but leaves the bump
allocation; TOCTOU between ftell and fread is harmless (checked). Consider mmap.

## 9. MINOR: unvalidated font data (font.c:84-125)

stb_truetype performs no bounds checks against the font length (`font_init` only checks len >= 12 and then drops
`len`). Fonts come from fontconfig paths, including user-writable ~/.local/share/fonts. A crafted table offset gives
OOB reads. In addition `font_set_px` casts `round_f(a*scale)` to uint32_t (negative ascent/descent wraps, `cell_h`
overflows) and `ScaleForPixelHeight` divides by (ascent - descent) which is 0 for a degenerate font, giving inf and UB in
`round_f`. Fix: reject fonts with `a - d <= 0`, `asc/dsc` outside [0, 4*px], cell_w/h beyond atlas page dim; run the existing
font loader under a libFuzzer harness (there is no fuzz/font_fuzz.c).

## 10. MINOR: `done` not cleared (fallback.c:112-135)

`fb->cjk/emoji` are written before `atomic_store(done, 1, release)` which is correct for the first run only. A second
discover on the same struct (retry/refresh) leaves `done == 1` while the worker rewrites the strings, and a reader that
saw done == 1 races on them. Fix: `atomic_store_explicit(&fb->done, 0, relaxed)` before dlopen and document single-run use.

## 11. MINOR: focus events with grab modes (x11.c:274-281)

FocusIn/FocusOut are handled regardless of `mode`. Window-manager key grabs (Alt-Tab, compositor shortcuts) generate
NotifyGrab/NotifyUngrab focus events while a key is still physically down. Clearing `down[]` then lets the server's
detectable-autorepeat press for the still-held key through as a brand-new press (duplicate character), and cancels our
repeat. Fix: ignore `mode == NotifyGrab/NotifyUngrab/NotifyWhileGrabbed` for state reset, and ignore detail NotifyPointer.

## 12. MINOR: stray key-up after swallowed press (input.c:110-127)

`x11_input_key` marks the key down, then returns false for COMPOSING/CANCELLED, but the matching release is delivered
(`return true` at line 119). Consumers see KEY_UP with no KEY_DOWN (dead_acute, and any modifier pressed mid-sequence:
libxkbcommon ignores modifier keysyms, so Shift pressed while COMPOSING is swallowed but its release is not). Fix: remember
swallowed keycodes in a second bitmap and swallow their release too, and do not swallow pure modifiers.

## 13. MINOR: replay tool and doc mismatches

tools/replay.c accepts `--speed=inf` (strtod parses "inf", `!(inf > 0.0)` is false) but trace_replay rejects non-finite speed;
the tool exits 2 with no message. Also trace.h:90 says wheel dx/dy are "16.16 fixed point", but plat.h defines
PLAT_WHEEL_UNIT = 256 (24.8); whoever wires the recorder will pick the wrong scale. Fix the comment (or the unit) now.
Not a bug: trace_load validation is complete (every length checked before use, all failure paths free via
trace_fmt_dump_free, no overflow possible with the bounded counts); trace_replay uses absolute deadlines correctly.
Note that trace_input_* has no caller yet (P0.6 says wiring is a later bead) so single-producer is by contract only;
nothing prevents a worker thread from calling it (no registration check, unlike trace_record).

## 14. MINOR: init ownership (input.c:31-38, x11.c:55-58)

`x11_input_init` stores `ctab` and creates `cstate` before `x11_input_set_keymap`, which can fail; on failure the callers
(`setup_keyboard`) free `in` and unref `km`/`ctx` but never `ct`/`cstate`. One-time leak on a startup failure path only.

## Checked and found sound

- xi2 wire offsets (DeviceEvent 40/44/48/50/52/72/79/80, xcb +4 shift after byte 32, QueryDevice reply, SelectEvents length 5,
  FP3232 decoding) match the XInput2 spec; every read is bounds-checked against len, and the truncation sweep in
  x11_xi2_test covers all prefix lengths.
- xi_send with ext = NULL and the minor opcode in byte 1 is equivalent to a core-style request and works (length overwritten by xcb).
- x11_clock_map wrap handling ((int32_t)(x_ms - ref_ms)), queue index arithmetic, repeat catch-up (no burst), key bitmaps, set_text
  length checks, and the wheel de-dup test (unsigned difference trick at x11.c:304) are correct.
- Typing path (x11_input_key, x11_translate, repeat_poll, q_push/pop, trace_record_at, font_raster_glyph) has no malloc; x11_bench
  and font_test confirm 0 allocations.
- Font fallback thread use is fine: the worker only writes the caller's fb, publishes via work_publish, and never touches font_t;
  font_raster_glyph mutates `info->userdata` so one font_t must not be rastered from two threads at once (document it).
