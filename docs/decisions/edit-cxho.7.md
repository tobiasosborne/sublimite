# Parked input and busy tab commands

The failing tests reproduced the queue-full return and a fatal busy-tab return
through the input loop. New-file commands already deferred safely; preserve that
behavior rather than changing it into a rejection.

While input waits for an open, replacement, activation, journal, command or
layout, merge the trailing pointer run: keep the latest MOTION and sum WHEEL
deltas. Keys, buttons and other control events remain ordering barriers. Preserve
owner/generation, modifier, button-state and smooth-wheel distinctions. Move a
merged event to the end of its pointer run; clamp wheel sums to the event type's
signed limits (P: representable range, computed with a wider intermediate).

Keep bounded type-ahead: retain accepted keys in FIFO order, discard incoming
events once capacity is exhausted, increment the existing rejected-command
counter and show `input queue full: excess input ignored`. This is the chosen
Sublime-like behavior: resume accepted typing when the operation finishes and
keep the editor usable under overload. It is a product choice, not a measurement
of Sublime's overflow policy. No queue growth or typing-path allocation.

A pending notice follows retained mutations until the queue drains, then uses
the existing status lifetime: visible when panel chrome permits, retired by the
next mutation. The private pending flag prevents accepted type-ahead from
silently invalidating the notice before its first visible frame.

Busy select, cycle, close and reopen commands return success without changing
tabs and increment `rejected_commands`. Close also refuses before opening an
unsaved-text prompt. Internal activation and buffer-creation errors retain their
existing contracts.

Test quantities: the burst is 1100 events (E: specified by the brief); capacity
is 1024 (P: existing `EDITOR_INPUT_CAP`), giving 1100 - 1024 = 76 rejected keys
(P). Pointer sequences derive their expected lengths and sums from their actual
events; signed-limit tests derive from `INT32_MIN`/`INT32_MAX`. The open fixture
is `FILE_PREFIX_MAX` plus the literal remainder (P), sparse and removed after
use. Geometry reuses the existing pending-open control fixture (E); watchdogs
reuse existing test helpers rather than adding a duration.
