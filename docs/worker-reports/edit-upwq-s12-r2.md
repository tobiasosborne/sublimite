# edit-upwq-s12 — Round 2 worker report

Verdict: **done for the scoped implementation and recorded build/check gates**, with real IME-engine and physical mixed-monitor coverage unavailable. This continuation recovered the prior session's evidence and completed the requested documentation. It changed no code and reran no build or test.

## Provenance and evidence conventions

The binding sources were the round-2 brief, `_rules-s12.md`, and the previous session's [transcript](/home/tobiasosborne/Projects/sublimite/build/briefs/edit-upwq-s12-r2.codex.log), all in `/home/tobiasosborne/Projects/sublimite/build/briefs/`. These were the only files read outside this worktree. `L` references below are physical, one-based transcript line numbers. Local raw logs are under `build/r2-logs/`; those paths are evidence artifacts, not newly executed commands. The transcript ends with the provider's capacity errors after the final successful checks, at L7380–L7381.

Finding numbers identify entries in [the review](../reviews/edit-upwq.md). (M) Outcomes/counts below come from captured process statuses, assertion output or the check recipes' totals; (G) bounds are existing contract constants; (E) retry/hang guards are policy heuristics, not measured latency budgets. Measurements are `[AC, desktop Spiral]`. The original worker checked the absent battery and captured `uptime` at L3855–L3857; a near-completion load sample is quoted below. No fresh benchmark measurements were taken by this continuation.

```text
L3857:  12:08:21 up  4:24,  1 user,  load average: 8.10, 4.81, 3.41
L7350:  12:42:42 up  4:58,  1 user,  load average: 0.53, 2.62, 4.52
```

## Files and changes by finding

| File | Round-2 responsibility |
|---|---|
| `src/win32/win32_plat.c` | Sent-message drain, guarded native modal service, UI-thread checks and teardown, native text ownership/AltGr/IME/Alt-numeric commits, capture cancellation, clipboard worker integration, monitor-specific refresh timing. |
| `src/win32/private.h` | Creating-thread/callback/modal/text state, clipboard worker interface, and explicit owner-thread contract with the existing T6 producer exception. |
| `src/win32/win32_clip_win.c` (new) | Dedicated clipboard thread and helper HWND, owned FIFO jobs/results, native conversion/newlines, contention retry and independent worker lifetime. The entire source is Windows-guarded. |
| `src/x11/plat.h` | Finding 12 only: clarify that `plat_clip_data` belongs to the completion being delivered and remains valid until the next completion is popped/delivered. No signature, event layout, or Linux implementation change. |
| `tests/win/win32_plat_test.c` | Focused regression cases, native API fixtures, separate-process clipboard owner, and a switch to compile the same suite against the saved round-1 source. |
| `mk/win.mk` | Explicit release/sanitizer platform-test link recipes: the suite includes the implementation for API fixtures, so do not link duplicate platform/clipboard objects. Other tests still link production objects. |
| `docs/decisions/edit-upwq.md` | Append the Round 2 finding → change → test table and resulting policies. |
| `docs/worker-reports/edit-upwq-s12-r2.md` | This recovered report. |

The pre-existing untracked `docs/worker-reports/edit-upwq-review-final.md` is a review-session artifact, not a round-2 implementation change; it was left untouched. `os.h`, `render.h`, the keysym generator/table, and Linux backend code were not changed. Git was used only to inspect status/diff; no `bd`, state-changing git command, or main-checkout write was performed by this continuation.

| Finding | Change | Test |
|---|---|---|
| 1: sent-message sleep | Recheck the event ring after an empty `PeekMessageW` result and before blocking; flush pending text at the empty-queue boundary. | `sent`: helper-thread `SendMessageW(WM_CLOSE)` at the empty-queue boundary; assert no blocking wait over the queued event and no rescue wake. |
| 2: native modal loops | Service events, work, T6, repeat and blink from a guarded modal service path and native timer; coalesce RESIZE/EXPOSE to their newest state. Callback-depth guards prevent recursive application dispatch. | `modal`, `move`, `menu`: native sizing, moving and Alt+Space loops; a resize burst larger than the ring retains final geometry and CLOSE, delivers work/T6, and reports no dropped events. |
| 14: HWND affinity/lifetime | Record the creating UI thread. Wrong-thread UI status entries fail and void entries do nothing. Shutdown must be retried after the active pump returns and retains state if `DestroyWindow` fails; `WM_NCDESTROY` clears userdata. The documented T6 producer remains cross-thread. | `affinity`: wrong-thread run/configuration/shutdown leaves the HWND, userdata and configuration alive for owner-thread teardown. |
| 3: AltGr state | Detect layout AltGr capability without mutating translation state; normalize sided RightAlt consistently for key and pointer events, suppress only its synthetic Ctrl, and retain independently held Ctrl/LeftAlt. Explicit LeftCtrl+LeftAlt remains a command chord. | `altgr`, original German keyboard cases: mapped/unmapped German keys, navigation/pointer, explicit Ctrl+Alt, real Ctrl with AltGr, and ordinary RightAlt on US. |
| 4: text ownership | `TranslateMessage` is the mutating translation owner; `ToUnicodeEx` probes for symbols/repeat do not mutate compose state. Printable KEYDOWN waits for its native CHAR commit. Scan provenance distinguishes owned physical repeats from independent CHAR/UNICHAR commits. | `ownership`: an external translating dispatcher inserts once; standalone CHAR repeat counts and bit-30 UNICHAR commits are retained; original German compose/repeat cases remain green. |
| 5: packet/IME route | Translate `VK_PACKET` and `VK_PROCESSKEY` natively and exclude them from physical shortcut/repeat dispatch. Preserve default IME handling and isolate preedit/cancel state from committed text. | `packet`: foreground `KEYEVENTF_UNICODE` BMP/supplementary input; `ime`: native IME message commit/cancel isolation. A real IME engine is unavailable. |
| 6: multiscalar output | Accumulate native UTF-16 commits as validated UTF-8 and stream complete scalars through fixed event payloads. Repeat also streams the whole returned span. Multiscalar payloads have no single keysym; invalid dead-key composition remains consumed. | `multiscalar`: ordinary multiscalar output, a supplementary commit larger than one event payload, and complete repeated spans; original invalid-compose case. |
| 7: Alt+numpad | Preserve native Alt-numeric translation through Alt release; suppress intermediate digits/chords and owned repeat. | `altcode`: native Alt+numpad sequence commits its character once on Alt release. |
| 8: system keys | Pass Alt+F4, Alt+Space and appropriate Alt-release handling to `DefWindowProcW`. | `system`: Alt+F4 delivers CLOSE while the HWND remains live; `menu`: Alt+Space enters and cancels the native system menu. |
| 21: surrogate isolation | Clear incomplete UTF-16 state at independent UTF-32 commits, physical-key/IME and focus/layout boundaries; pair halves only with matching scan provenance and reject invalid scalars. | `surrogate`: intervening UNICHAR, orphan halves, focus reset and out-of-range scalar; `packet` and original Unicode cases exercise valid pairs. |
| 10: blocking clipboard work | A dedicated worker owns a message-only HWND, native clipboard access and bulk conversion. The UI queues copied jobs and receives signalled completions. Shutdown can detach a busy foreign operation using self-owned worker state without a platform pointer. | `clip-async`, `clip-foreign`: API-thread checks plus a separate delayed-rendering process and large payload; the UI work callback releases the foreign renderer without rescue. |
| 11: clipboard contention | Retry native open on the worker with bounded exponential backoff, interruptible by shutdown. Copy and paste share FIFO jobs; terminal outcomes are retained. | `clip-contention`, `clip-foreign-contention`: injected failures, a separate native holder, expiry and pending-operation shutdown. |
| 12: completion/data association | Each completed request owns its bytes until popped; install its receive buffer only when delivering that result. Clarify `plat_clip_data` lifetime in `src/x11/plat.h`; no public fields/signatures or Linux implementation change. | `clip-results`: success followed by PRIMARY plus a request submitted inside the success callback; the current pointer/data stays valid until the next completion is delivered. |
| 13: newline interoperability | Export lone LF as UTF-16 CRLF without doubling existing CRLF; import CRLF as LF and retain isolated CR. This implements the round-2 brief's explicit import-normalization requirement. | `clip-newlines`: inspect native exported UTF-16 and verify mixed-newline/supplementary-text round trip. |
| 23: embedded NUL | Reject counted text containing NUL before queueing; preserve the previous clipboard instead of silently publishing a prefix. | `clip-nul`: rejected embedded-NUL copy leaves the previous value intact. |
| 9: capture loss | Emit releases for held buttons at the last pointer position on capture theft/cancellation; clear each bit before delivery to avoid a duplicate release on normal capture teardown. | `capture`: same-thread capture theft, later buttonless motion, normal release and `WM_CANCELMODE`. |
| 15: display interval | Match `MonitorFromWindow` to the active `QueryDisplayConfig` source and invert its output refresh rational; return zero for unavailable or conflicting cloned-output timing. | `display`: distinct output-rate fixtures and a live HWND-output query. Physical mixed-refresh movement is unavailable. |

The table also appears in the appended [Round 2 decision section](../decisions/edit-upwq.md#round-2-review-findings--change--test). It supersedes the earlier decision text describing a mutating `ToUnicodeEx` owner, synchronous clipboard reads and global DWM refresh timing.

The shared clipboard comment was changed because queueing another request must not invalidate bytes already borrowed by the current completion callback. A worker result retains its own buffer until popped; only that delivery installs the public receive buffer. This documents the intended association without changing X11 execution. For newlines, the brief explicitly requires inbound normalization even though the review considered editor-side normalization sufficient: Windows import removes CR from CRLF pairs, preserves isolated CR, and returns internal LF text.

## Red evidence against saved round-1 source

The original source/header were saved before the fixes at transcript L3855 as `build/r2-round1/win32_plat.c` and `private.h`. The suite's `WIN32_PLAT_ROUND1` switch includes those saved files instead of the round-2 implementation. The saved-source compiler invocation is recorded at L6426, L6514 and L7215; it links the OS/base objects and manifest, without a production round-2 platform/clipboard object. The final baseline binary was rebuilt after the last fixture correction (L7206–L7216).

(M) The final baseline replay failed every named regression case: each status is `1`, derived directly from the loop's captured `$?`. The exact batch command is at L7232. Its essential invocation was:

```sh
# Recorded command shape; not rerun by this continuation.
for case in sent modal move menu affinity altgr ownership packet ime multiscalar altcode system surrogate clip-async clip-contention clip-results clip-newlines clip-nul clip-foreign clip-foreign-contention capture display; do
    tools/winrun.sh build/win/rel/tests/win32_plat_round1.exe "$case" > "build/r2-logs/$case-round1-red.log" 2>&1
    rc=$?
    printf '%s\t%s\n' "$case" "$rc" >> build/r2-logs/round1-summary.tsv
done
cat build/r2-logs/round1-summary.tsv
```

Verbatim saved-source exit-status summary:

```text
L7267: sent	1
L7268: modal	1
L7269: move	1
L7270: menu	1
L7271: affinity	1
L7272: altgr	1
L7273: ownership	1
L7274: packet	1
L7275: ime	1
L7276: multiscalar	1
L7277: altcode	1
L7278: system	1
L7279: surrogate	1
L7280: clip-async	1
L7281: clip-contention	1
L7282: clip-results	1
L7283: clip-newlines	1
L7284: clip-nul	1
L7285: clip-foreign	1
L7286: clip-foreign-contention	1
L7287: capture	1
L7288: display	1
```

The following are the relevant red assertion/output excerpts from the transcript. Early focused runs used `tools/winrun.sh build/win/rel/tests/win32_plat_test.exe CASE` while the corresponding path still had the old behavior; later additions explicitly used `win32_plat_round1.exe`. The final saved-source batch above is the common baseline evidence for the final fixtures, including cases refined after an early run. Some early stdout/stderr text is interleaved in the original transcript; excerpts retain it verbatim rather than repairing a quote.

**Finding 1 — sent; command L4086.**

```text
L4088: FAIL tests/win/win32_plat_test.c:270: !waited_over_event (winerr 0)
L4089: win32_plat: r2 sent-message wake without unrelated input: FAIL
L4090: win32_plat_test: FAIL (1 failures)
L4091: exit 1
```

**Finding 2 — modal; command L4177.**

```text
L4179: FAIL tests/win/win32_plat_test.c:290: WaitForSingleObject(modal_serviced,0)==WAIT_OBJECT_0 (winerr 0)
L4180: FAIL tests/win/win32_plat_test.c:291: f->work==1 && f->presents==1 && kind(f,PLAT_EV_CLOSE)==1 (winerr 0)
L4181: FAIL tests/win/win32_plat_test.c:292: win32_private(&f->p)->dropped==0 (winerr 0)
L4182: win32_plat: r2 native sizing loop services work/T6/close and final geometry: FAIL
L4183: win32_plat_test: FAIL (3 failures)
L4184: exit 1
```

**Finding 2 — saved-source move/menu; command L6519.**

```text
L6521: FAIL tests/win/win32_plat_test.c:736: WaitForSingleObject(modal_entered,0)==WAIT_OBJECT_0 (winerr 0)
L6522: FAIL tests/win/win32_plat_test.c:737: WaitForSingleObject(modal_serviced,0)==WAIT_OBJECT_0 (winerr 0)
L6523: FAIL tests/win/win32_plat_test.c:738: f->work==1 && f->presents==1 && kind(f,PLAT_EV_CLOSE)==1 (winerr 0)
L6524: FAIL tests/win/win32_plat_test.c:742: e && e->w==(uint32_t)r.right && e->h==(uint32_t)r.bottom (winerr 0)
L6525: win32_plat: r2 native sizing loop services work/T6/close and final geometry: FAIL
L6526: win32_plat_test: FAIL (4 failures)
L6527: exit 1
L6535: FAIL tests/win/win32_plat_test.c:737: WaitForSingleObject(modal_serviced,0)==WAIT_OBJECT_0 (winerr 0)
L6536: FAIL tests/win/win32_plat_test.c:738: f->work==1 && f->presents==1 && kind(f,PLAT_EV_CLOSE)==1 (winerr 0)
L6537: FAIL tests/win/win32_plat_test.c:739: win32_private(&f->p)->dropped==0 (winerr 0)
L6538: win32_plat: r2 native sizing loop services work/T6/close and final geometry: FAIL
L6539: win32_plat_test: FAIL (3 failures)
L6540: exit 1
```

**Finding 14 — affinity; command L4293.**

```text
L4295: FAIL tests/win/win32_plat_test.c:261: pump(f,0)==PLAT_ERR_FAIL (winerr 0)
L4296: FAIL tests/win/win32_plat_test.c:272: win32_private(&f->p)==saved (winerr 0)
L4297: win32_plat: r2 HWND owner thread rejects run/configuration/shutdown: FAIL
L4298: win32_plat_test: FAIL (2 failures)
L4299: exit 1
```

**Finding 3 — altgr; batch command L4434.**

```text
L4438: FAIL tests/win/win32_plat_test.c:294: e && (e->mods&PLAT_MOD_ALTGR) && !(e->mods&(PLAT_MOD_CTRL|PLAT_MOD_ALT)) (winerr 0)
L4441: FAIL tests/win/win32_plat_test.c:299: e && e->mods==(PLAT_MOD_CTRL|PLAT_MOD_ALT) && !e->utf8_len (winerr 0)
L4442: FAIL tests/win/win32_plat_test.c:301: e && (e->mods&(PLAT_MOD_ALTGR|PLAT_MOD_CTRL))==(PLAT_MOD_ALTGR|PLAT_MOD_CTRL) && !e->utf8_len (winerr 0)
L4443: win32_plat: r2 AltGr modifier state, navigation/pointer, explicit Ctrl+Alt and real Ctrl: FAIL
L4444: win32_plat_test: FAIL (5 failures)
L4445: exit 1
```

**Finding 4 — ownership; batch command L4434.**

```text
L4446: FAIL tests/win/win32_plat_test.c:316: letters==1 (winerr 0)
L4447: FAIL tests/win/win32_plat_test.c:319: letters==3 (winerr 0)
L4448: FAIL tests/win/win32_plat_test.c:321: e && e->utf8_len && e->utf8[0]=='x' (winerr 0)
L4449: win32_plat: r2 single text owner and standalone repeated commits: FAIL
L4450: win32_plat_test: FAIL (3 failures)
L4451: exit 1
```

**Finding 5 — saved-source packet; command L6435.**

```text
L6459: FAIL tests/win/win32_plat_test.c:637: len==6 && !memcmp(bytes,"\xce\xbb\xf0\x9f\x99\x82",6) (winerr 0)
L6462: win32_plat_test: FAIL (2 failures)
L6463: exit 1
```

**Finding 5 — saved-source IME message route; command L6519.**

```text
L6544: FAIL tests/win/win32_plat_test.c:647: kind(f,PLAT_EV_KEY)==0 (winerr 0)
L6545: FAIL tests/win/win32_plat_test.c:654: kind(f,PLAT_EV_KEY)==n (winerr 0)
L6546: win32_plat: real IME engine compose/commit/cancel: SKIP (installed layouts are UK/US/German)
L6547: win32_plat: r2 native IME commit/default handling and preedit/cancel isolation: FAIL
L6548: win32_plat_test: FAIL (2 failures)
L6549: exit 1
```

**Finding 6 — multiscalar; batch command L4554.**

```text
L4556: FAIL tests/win/win32_plat_test.c:360: len==2 && !memcmp(bytes,"ab",2) (winerr 0)
L4557: win32_plat: r2 multiscalar keyboard output: FAIL
L4558: win32_plat_test: FAIL (1 failures)
L4559: exit 1
```

**Finding 6 — additional long-span repeat regression; command L6896.**

```text
L6898: FAIL tests/win/win32_plat_test.c:681: repeated>=16 && repeated%16==0 (winerr 0)
L6899: win32_plat: r2 multiscalar keyboard output including long commits/repeat: FAIL
L6900: win32_plat_test: FAIL (1 failures)
L6901: exit 1
```

**Finding 7 — altcode; batch command L4554.**

```text
L4560: FAIL tests/win/win32_plat_test.c:377: letters==1 && e && e->utf8_len==1 && e->utf8[0]=='A' (winerr 0)
L4561: win32_plat: r2 native Alt+numpad commit: FAIL
L4562: win32_plat_test: FAIL (1 failures)
L4563: exit 1
```

**Finding 8 — system; batch command L4434.**

```text
L4457: FwAIL tests/win/win32_plat_test.c:338: kind(f,PLAT_EV_CLOSE)==1 && IsWindow(window(f)) (winerr 0)
L4458: in32_plat: r2 Alt+F4 reaches native CLOSE: FAIL
L4459: win32_plat_test: FAIL (1 failures)
L4460: exit 1
```

**Finding 21 — surrogate; batch command L4434.**

```text
L4461: FAIL tests/win/win32_plat_test.c:345: kind(f,PLAT_EV_KEY)==1 (winerr 0)
L4466: win32_plat: r2 UTF-16 commit isolation and invalid scalars: FAIL
L4467: win32_plat_test: FAIL (5 failures)
L4468: exit 1
```

**Finding 10 — clip-async; batch command L5023.**

```text
L5025: wFin32_plat: r2 clipboard foreign read stays off UI threadAIL tests/win/win32_plat_test.c:344: clip_ui_reads==0 (winerr :0 )
L5026: FAIL
L5027: win32_plat_test: FAIL (1 failures)
L5028: exit 1
```

**Finding 10 — saved-source clip-foreign; command L6435.**

```text
L6464: FAIL tests/win/win32_plat_test.c:480: !c.rescued && clip_ui_reads==0 (winerr 0)
L6465: win32_plat: r2 foreign delayed rendering and large payload leave UI responsive: FAIL
L6466: win32_plat_test: FAIL (1 failures)
L6467: exit 1
```

**Finding 11 — clip-contention; batch command L5023.**

```text
L5029: FAIL tests/win/win32_plat_test.c:350: plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"retry",5)==PLAT_OK (winerr 5)
L5030: FAIL tests/win/win32_plat_test.c:352: f->clip_results==1 && f->clip_sizes[0]==5 && !memcmp(f->clip_bytes[0],"retry",5) (winerr 0)
L5031: win32_plat: r2 clipboard contention retries copy and paste: FAIL
L5032: win32_plat_test: FAIL (2 failures)
L5033: exit 1
```

**Finding 11 — saved-source clip-foreign-contention; command L6435.**

```text
L6468: FAIL tests/win/win32_plat_test.c:488: plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"foreign",7)==PLAT_OK (winerr 5)
L6469: FAIL tests/win/win32_plat_test.c:491: f->clip_results==1 && f->clip_sizes[0]==7 && !memcmp(f->clip_bytes[0],"foreign",7) (winerr 0)
L6470: FAIL tests/win/win32_plat_test.c:498: plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"pending",7)==PLAT_OK (winerr 5)
L6471: win32_plat: r2 foreign clipboard contention, expiry and pending shutdown: FAIL
L6472: win32_plat_test: FAIL (3 failures)
L6473: exit 1
```

**Finding 12 — clip-results; batch command L5023.**

```text
L5034: FAIL tests/win/win32_plat_test.c:362: f->clip_results==3 && f->cliwp_sizes[0]==5 && !memcmp(f->clip_bytes[0],"first",5) (winerr 0)
L5035: in32_plat: r2 clipboard completion owns its data and callback lifetime: FAIL
L5036: win32_plat_test: FAIL (1 failures)
L5037: exit 1
```

**Finding 13 — clip-newlines; batch command L5023.**

```text
L5038: FAIL tests/win/win32_plat_test.c:373: wide && !wcscmp(wide,L"a\r\nb\r\nc\r\n\xd83d\xde42") (winerr 0)
L5039: FAIL tests/win/win32_plat_test.c:378: bytes && len==sizeof expected-1 && !memcmp(bytes,expected,len) (winerr 0)
L5040: win32_plat: r2 CF_UNICODETEXT CRLF export and LF import: FAIL
L5041: win32_plat_test: FAIL (2 failures)
L5042: exit 1
```

**Finding 23 — clip-nul; batch command L5023.**

```text
L5043: FAIL tests/win/win32_plat_test.c:385: plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"a\0b",3)==PLAT_ERR_FAIL (winerr 0)
L5044: wFin32_plat: r2 embedded NUL rejected without replacing clipboard: FAIL
L5045: AIL tests/win/win32_plat_test.c:388: bytes && len==4 && !memcmp(bytes,"keep",4) (winerr 0)
L5046: win32_plat_test: FAIL (2 failures)
L5047: exit 1
```

**Finding 9 — capture; command L5369.**

```text
L5371: FAIL tests/win/win32_plat_test.c:346: e && !e->press && e->code==1 && !e->buttons (winerr 0)
L5372: FAIL tests/win/win32_plat_test.c:352: e && !e->press && e->code==3 &&w !e->buttons (winerr 0)
L5373: in32_plat: r2 capture theft/cancel ends drag once: FAIL
L5374: win32_plat_test: FAIL (2 failures)
L5375: exit 1
```

**Finding 15 — display; command L5486.**

```text
L5491: tests/win/win32_plat_test.c:399: plat_display_interval_ns(&f->p)==UINT64_C(1000000000)/144u (winerr 0)
L5492: win32_plat_test: FAIL (2 failures)
L5493: exit 1
```

## Green runs and verification

Focused green commands have the same invocation with `build/win/rel/tests/win32_plat_test.exe CASE`; each listed excerpt ends in `exit 0` and platform PASS. The final Windows check additionally runs all regression cases through the suite in both release and ASan/UBSan configurations.

| Case(s) | Recorded focused green output, transcript lines |
|---|---|
| `sent` | L4107–L4109 |
| `modal` | L4256–L4258 |
| `move` | L6622–L6624 |
| `menu` | L7013–L7015 |
| `affinity` | L4342–L4344 |
| `altgr` | L7016–L7018 |
| `ownership` | L4808–L4810 |
| `packet` | L4906–L4908 |
| `ime` (native message fixture) | L6632–L6635; real engine explicitly skipped |
| `multiscalar`, including long-span repeat | L7010–L7012 |
| `altcode` | L4818–L4820 |
| `system` | L4821–L4823 |
| `surrogate` | L4824–L4826 |
| `clip-async`, `clip-contention`, `clip-results`, `clip-newlines`, `clip-nul` | L5328–L5342, in that order |
| `clip-foreign`, `clip-foreign-contention` | L6474–L6479, in that order |
| `capture` | L5403–L5405 |
| `display` | L5540–L5543 |

Representative final focused greens:

```text
L6474: win32_plat: r2 foreign delayed rendering and large payload leave UI responsive: PASS
L6475: win32_plat_test: PASS (0 failures)
L6476: exit 0
L6477: win32_plat: r2 foreign clipboard contention, expiry and pending shutdown: PASS
L6478: win32_plat_test: PASS (0 failures)
L6479: exit 0
L7009:  succeeded in 16118ms:
L7010: win32_plat: r2 multiscalar keyboard output including long commits/repeat: PASS
L7011: win32_plat_test: PASS (0 failures)
L7012: exit 0
L7013: win32_plat: r2 native sizing loop services work/T6/close and final geometry: PASS
L7014: win32_plat_test: PASS (0 failures)
L7015: exit 0
L7016: win32_plat: r2 AltGr modifier state, navigation/pointer, explicit Ctrl+Alt and real Ctrl: PASS
L7017: win32_plat_test: PASS (0 failures)
L7018: exit 0
```

**Windows full check:** recorded command (L7363):

```sh
make TARGET=win -j64 check > build/r2-logs/windows-check-final.log 2>&1
rc=$?
echo "Windows final make check exit $rc"
rg 'win32_plat_test:|ASan fired|check \(win\):' build/r2-logs/windows-check-final.log
```

```text
L7374: Windows final make check exit 0
L7375: win32_plat_test: PASS (0 failures)
L7376: win32_plat_test: PASS (0 failures)
L7377: ASan fired: ok
L7378: check (win): 4 release and 4 sanitizer test binaries passed
```

(M) Exit `0`; platform release and sanitizer each report `PASS (0 failures)`. The check recipe totals are `4` release + `4` sanitizer binaries (the emitted counts above), with an intentionally failing ASan smoke probe confirmed by `ASan fired: ok`. Windows LeakSanitizer is unsupported by the mingw target. The OS fault suite also deliberately runs crashing children; their expected sanitizer diagnostics are not an unhandled suite failure.

The final raw log records all regression case verdicts in `build/r2-logs/windows-check-final.log`: release L44–L90 and sanitizer L180–L226. It contains packet PASS without a foreground skip in either configuration; real IME-engine coverage is explicitly skipped in both. The preceding full Windows run did skip foreground Unicode injection in its sanitizer pass (transcript L7354); the final run supersedes that coverage result. The release typing allocation guard reports no calls (raw final log L38); the sanitizer allocator guard is intentionally skipped because ASan owns allocation (L174).

**Linux full check:** recorded command (L6277):

```sh
DISPLAY=:94 make -j64 check > build/r2-logs/linux-check.log 2>&1
rc=$?
echo "Linux make check exit $rc"
tail -18 build/r2-logs/linux-check.log
```

The final Linux exit line is present; no continuation rerun was necessary:

```text
L7319: Linux make check exit 0
```

The subsequent evidence extraction and release build (command L7365) recorded:

```sh
make -j64 all > build/r2-logs/linux-all-final.log 2>&1
rc=$?
echo "Linux final make all exit $rc"
rg 'check: .* test binaries passed|test_replay_cli|display lock contract' build/r2-logs/linux-check.log
git diff --check
```

```text
L7367: Linux final make all exit 0
L7368: check: 104 test binaries passed
L7369: == tools/test_replay_cli.sh
L7370: test_replay_cli: all passed
L7371: display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

(M) Linux full check exit `0`, with `104` sanitizer test binaries (derived from the recipe's printed total), replay CLI checks and the display-lock contract passing; final release build exit `0`. LeakSanitizer remained enabled. The first Linux attempt had exited `2` (L5530): LeakSanitizer could not run under the sandbox/ptrace environment (L6266–L6269). The worker reran the full command outside that restriction rather than disabling leak detection, explicitly recorded at L6433, and that rerun is the exit-`0` result above. The captured command does not set `detect_leaks=0`.

The successful Linux log includes nested `:99` display-lock timeouts and an inner make error. These are deliberate negative checks in `tools/test_display_lock.sh contract`; the outer full check used `DISPLAY=:94`, reported the contract PASS (L7337), and exited `0` (L7319). They must not be misreported as a final Linux suite failure. The initial LSan failure and intermediate Windows fixture failures remain in the transcript; the final check summaries above are the acceptance evidence.

## Found, not fixed; coverage limits and coordinator handoff

The brief explicitly leaves findings **16, 17, 18, 19, 20, 22, 24, 25, 26 and 27** for coordinator backlog triage. They are not declared resolved by this report:

- **16–18:** initial client sizing on a secondary DPI monitor, DPI-query API/context choice, and process-wide PMv2 manifest/initialization ordering.
- **19–20:** scan-plus-extended physical keypad identity/repeat ownership and missing international/control virtual-key mappings. The text rewrite preserves existing numeric-keypad/Shift+Tab behavior; it does not implement those backlog fixes.
- **22:** timer arm/cancel errors are still not propagated; broad native allocation/setup/clipboard API failure injection is not completed.
- **24–26:** presentation callback draining can exceed a finite run's deadline; synchronous/injected input can inherit the previous queued-message timestamp; Windows startup trace stamps remain missing.
- **27:** this round necessarily adds the native input/modal/clipboard/capture regressions needed by the scoped findings, overlapping part of the broader coverage request. It does not complete exhaustive resource-failure accounting, presentation saturation/competing-producer/reset races, real hardware input, or the full focus-regain policy tests.

A **real IME engine compose/commit/cancel run remains unavailable**: installed layouts are UK/US/German. The `ime` regression tests `WM_IME_*` default commit forwarding and preedit/cancel isolation; it is not evidence of a real language engine, candidate UI or full composition lifecycle. Native packet injection is exercised when foreground conditions allow, and did execute in the final raw Windows log. Most keyboard/modifier cases still install synthetic state; physical AltGr sequencing across all layouts is not claimed.

**Physical mixed-DPI/mixed-refresh monitors are unavailable.** The output test uses distinct active-output fixtures and a live query on the current display. It does not prove window movement between actual mixed-rate monitors or actual font/cell-metric rescaling. Multiscalar keyboard output is supplied through API fixtures rather than an installed ligature-producing layout. The modal suite enters actual sizing, moving and system-menu loops, with synthesized resize messages; it is not a human drag/paint integration test.

**Clipboard semantics to carry into integration:** copy success means queue acceptance, with an owned copy of the bytes. Native publication happens later; successful copy is silent and terminal copy failure uses CLIPBOARD code `2`. The current event contract does not have a distinct copy-completion/error kind. Requests return their result as normal completion events with data installed at delivery. (E) Contention retries use `GetDoubleClickTime()` as the envelope and start at `USER_TIMER_MINIMUM`, doubling while capped by remaining time; this is a system-derived heuristic, not a measured latency promise. (G) Pending jobs are capped by `WIN32_EVENT_CAP`. Shutdown signals cancellation; an in-flight foreign delayed renderer cannot safely be interrupted, so its refcounted worker state may outlive the platform until native work returns. It holds no platform pointer. No guarantee of bounded foreign-process completion is claimed.

The coordinator must decide whether the unavailable real-IME and physical-monitor coverage is acceptable for landing or requires a later equipped run, and review the asynchronous copy/error policy when wiring editor clipboard feedback. Preserve both the new clipboard source and the platform-test link recipes when combining `mk/win.mk` changes with other W1 work. Existing assignments for editor-side DPI/font-metric rescaling (W2.4) and renderer/editor T6 integration remain as settled in the decision document; this continuation does not reopen them. Renderer producers must still stop before platform shutdown, and owner-thread teardown must occur after the active pump returns.

No additional code fixes were made during report recovery. The only continuation writes are this report and the appended Round 2 decision section; inherited source/test/build-rule contents were fingerprinted before the documentation edits for readback verification.

## Coordinator verification (session 22)

Rebased onto main over W0.5 (mk/win.mk: W0.5's lists plus `win32`). On that tree, (M)[AC, desktop Spiral], load1 1.94 at start:

```
make TARGET=win -j64 check    → exit 0, 60 results all exit=0 (win32_plat_test rel and san among them)
make -j64 all                 → exit 0 (gcc 13)
DISPLAY=:96 make -j64 check   → exit 0, 105 suites, 12:55:41 → 13:19:00, LeakSanitizer on
```

## Coordinator verification 2 (session 22 wind-up)

Re-rebased over W1.3 and W2.2 (mk/win.mk: `… font journal win32`, both WIN_LIBS sets, both white-box rule blocks; stale `libcore.a` purged). On that tree, (M)[AC, desktop Spiral]:

```
make TARGET=win -j64 check    → 81 results exit=0 (win32_plat_test rel and san among them); one red: rel d3d11_test `first_frame == 0`, W1.2's occlusion-sensitive test failing on main's own binary since 13:49 (edit-kv2g first item); its san build passed in this run
make -j64 all                 → exit 0 (gcc 13)
DISPLAY=:94 make -j64 check   → exit 0, 109 suites, LeakSanitizer on (14:25 → 14:56)
```
The earlier "deterministic on this tree" reading (13:49) was wrong: those reruns overlapped another worktree's Windows check and the desktop state had already changed.
