"""R4: restore under real live-tab pressure keeps every unsaved tab and drops excess clean ones.

One run per invocation against the pinned, inspected v1 oracle
(docs/decisions/v2-he7.17.md). Three phases on one display number; each phase, and each
editor launch scored in phase 3, gets a private X server no earlier one used:

1. Capacity. A sacrificial fresh profile admits individually named clean files through
   public handoffs (a second launch forwarding its paths) until a one-path handoff is
   refused for capacity. The number of live tabs at that refusal is C.
2. Producers. In the fixture profile the filler editor F (the IPC primary) starts first,
   then the older editor O, a --new-instance peer sharing F's DATA. With both alive, O
   holds c0 c1 m c2 plus an untitled marker, with m and the untitled tab dirty (U = 2),
   and hot-exits. F then admits exactly C - U - 1 clean files and hot-exits last. The case
   then stamps F's journal newer than O's and verifies both stamps as setup.
3. Restore. An argument-free relaunch restores F whole and O under pressure. The case
   reads the notice on the full-width status row, walks every live tab with Ctrl+PageDown,
   copies the dirty tabs, names the surviving and dropped clean tabs by path, SIGKILLs that
   editor, relaunches and walks again.

Which clean tab survives is v1's stated rule, not "some": plan_partial in v1
src/editor/recovery.c keeps unmodified tabs "in recorded tab order" (the last TABS record),
skipping only the PAINT-named buffer and vanished targets, until the live room left after
every unsaved tab is spent. Here that room is one tab, so c0 survives and c1, c2 are dropped.

Every wait is on an event: a snapshot reply on the inspector socket, or a pidfd. Timers
are hang detectors only; an expired one is ERROR. Records and exit statuses (PASS 0,
FAIL 3, ERROR 4) follow tests/contract/records.py.
"""

import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_oracle import EDITOR
from cases.files import (CAPACITY, GEOMETRY, SCREEN, WATCHDOG, describe, document_rows, exited,
                         tab_strip)
from cases.recovery import copied_text
import records
from sandbox import CONTRACT_DISPLAY, Refused, RunTarget, Sandbox
from test_oracle_inspector import Inspector, law2_count
from xserver import TeardownError, fresh_display, stop


ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "build"
WAIT_MS = WATCHDOG * 1000
CASE_ID = "R4"
FIRST_WAVE = 3
ASSERTIONS = ("capacity-measured", "producers-admitted", "partial-restore-triggered",
              "notice-visible", "dirty-restored", "clean-survivor-tab-order",
              "omitted-state-recoverable", "second-recovery", "disk-intact", "law2-count")
EVIDENCE = {"capacity-measured": "A", "producers-admitted": "A+B",
            "partial-restore-triggered": "A+B", "notice-visible": "B", "dirty-restored": "A+B",
            "clean-survivor-tab-order": "A+B", "omitted-state-recoverable": "A",
            "second-recovery": "A+B", "disk-intact": "A", "law2-count": "B"}
# Red controls: a swapped survivor (c2 expected to survive instead of c0), or a fixture
# without pressure.
NEGATIVES = ("clean-survivor-tab-order", "no-pressure")

# Paths per handoff. (P) v1 src/ipc/ipc.h IPC_MAX_PATHS = 128 is the ceiling, but v1's
# per-open cost grows with the live tab count and a forwarding launch gives up after
# 2000 ms (P, src/main.c ipc_client_send_display). (M) [AC, load 0.7] 128-path handoffs took
# 0.3, 0.6, 0.9, 1.2, 1.5, about 1.8 s at 1, 129, 257, 385, 513, 641 tabs and timed out at 769
# (run contract-capacity-u9qozozn, log mtimes). (E) a batch of 32 stays near 0.6 s at 1000.
# A handoff timeout leaves the request's admission unknown and is an ERROR.
BATCH = 32
# (P) v1 src/main.c: editor_main configures .tab_capacity = 1000u. The probe never relies on
# it; it only bounds the search: past twice that, no capacity refusal is discoverable (ERROR).
PROBE_BOUND = 2 * 1000
# (P) v1 src/main.c: a forwarding launch exits 0 once the primary accepted the request and 2
# with this line when the primary answered IPC_LIMIT (src/ipc/result.c). EDITOR_ERR_CAPACITY
# is the only editor-side source of IPC_LIMIT (src/editor/buffers.c:open_request); a one-path
# request cannot reach the client's wire bound (IPC_MAX_WIRE 1 MiB, IPC_PATH_CAP 4096).
REFUSED = b"sublimite: handoff failed: request or editor capacity limit exceeded\n"
# (P) v1 src/journal/journal.h JOURNAL_STARTUP_SCAN_BYTES: sources above it take the unrelated
# large-session deferral instead of a restore; both producers must stay below it.
STARTUP_SCAN_BYTES = 105_000_000
JOURNAL_LINE = re.compile(rb"sublimite: unsaved session journal: (\S+)\n")

# The older session: clean files before and after a dirty named file, then a dirty untitled
# tab. Contents carry no newline so each tab's identity is its first document row.
OLDER = (("c0.txt", b"c0-clean"), ("c1.txt", b"c1-clean"), ("m.txt", b"m-original"),
         ("c2.txt", b"c2-clean"))
DIRTY_FILE = "m.txt"
OLDER_CLEAN = ("c0.txt", "c1.txt", "c2.txt")
UNSAVED = 2  # U: m.txt and the untitled marker
UNTITLED = "<untitled>"
# (P) v1 src/editor/recovery.c restore_source: the notice of a partial restore that dropped
# clean tabs and set its source aside, with U unsaved and D dropped tabs (D > 1 here).
NOTICE = ("restored {u} unsaved tabs; {d} unmodified tabs from the last session were not "
          "reopened; the old session journal was set aside")


class Mismatch(Exception):
    """An observation contradicted the assertion."""


def filler_name(index):
    return f"f{index:04d}.txt"


def probe_name(index):
    return f"p{index:04d}.txt"


def content(name):
    return name.removesuffix(".txt").encode("ascii")


def write_fixture(path, data):
    with path.open("xb") as file:
        file.write(data)


def sessions(data_dir):
    """Journal sources as v1 discovery sees them: session-* without a dot."""
    if not data_dir.exists():
        return set()
    return {p.name for p in data_dir.iterdir()
            if p.name.startswith("session-") and "." not in p.name}


class Case:
    def __init__(self, artifacts, negative, display):
        self.display = display
        self.artifacts = artifacts
        self.run_id = artifacts.name
        self.negative = negative
        self.asserted = []
        self.results = []
        self.children = []
        self.facts = {}
        self.stage = "setup"
        self.observed = self.expected = self.ordinary = None

    def note(self, message):
        with (self.artifacts / "steps.log").open("a", encoding="utf-8") as file:
            print(message, file=file)

    def step(self, stage):
        self.stage = stage
        self.note(stage)

    def emit(self, assertion, status, predicate, **extra):
        record = {
            "run_id": self.run_id, "case_id": CASE_ID, "assertion_id": assertion,
            "evidence": EVIDENCE.get(assertion, "A+B"), "first_required_wave": FIRST_WAVE,
            "status": status, "predicate": predicate, "observed": self.observed,
            "expected": self.expected, "ordinary": self.ordinary, "negative": self.negative,
            "stage": self.stage,
            "facts": self.facts, "artifacts": str(self.artifacts),
            "child_status": [{"role": role, "pid": child.pid, "exit": child.poll()}
                             for role, child in self.children],
            **extra,
        }
        with (self.artifacts / "results.jsonl").open("a", encoding="utf-8") as file:
            print(json.dumps(record, default=describe, sort_keys=True), file=file)
        records.emit(record, default=describe)
        if status != "PASS":
            print(f"{CASE_ID}.{assertion}: {status} {predicate}", file=sys.stderr, flush=True)
        self.results.append(status)

    def check(self, assertion, observed, expected, wrong=None, **extra):
        """Compare once, in inventory order; the first failure is final.

        A red control is credited only from a healthy run: its observation must first meet
        the ordinary expectation, and only then is it compared with the wrong one."""
        if assertion in self.asserted or ASSERTIONS[len(self.asserted)] != assertion:
            raise Refused(f"assertion {assertion} out of inventory order")
        self.observed, self.expected, self.ordinary = observed, expected, None
        if self.negative == assertion:
            if observed != expected:
                raise Refused(f"{assertion}: the control's observation fails the ordinary "
                              "expectation, so its red would not be the deliberate one")
            self.ordinary, self.expected = {"expected": expected, "observed": observed}, wrong
        self.asserted.append(assertion)
        if self.observed != self.expected:
            raise Mismatch(f"{assertion}: observed value differs from expectation")
        self.emit(assertion, "PASS", assertion, **extra)

    def failure(self, exc):
        status = "FAIL" if isinstance(exc, Mismatch) else "ERROR"
        assertion = (self.asserted[-1] if isinstance(exc, Mismatch) else
                     ASSERTIONS[len(self.asserted)] if len(self.asserted) < len(ASSERTIONS)
                     else "assertion-inventory")
        self.emit(assertion, status, f"{self.stage}: {type(exc).__name__}: {exc}")

    def server(self, sandbox, label):
        """A fresh private server and driver; the sandbox's Xauthority gets its cookie."""
        return fresh_display(self.display, self.artifacts, f"Xvfb-{label}", sandbox.env,
                             timeout=WATCHDOG * 2, geometry=GEOMETRY)

    @property
    def status(self):
        for status in ("ERROR", "FAIL"):
            if status in self.results:
                return status
        return "PASS" if self.results else "ERROR"


class Editor:
    """One inspected editor: its process, inspector, log and bound window."""

    def __init__(self, case, stack, role):
        self.case = case
        self.role = role
        self.inspector = Inspector(stack, CAPACITY)
        self.stack = stack
        self.log = case.artifacts / f"{role}.log"
        self.child = self.window = None

    def spawn(self, launch, argv, **kwargs):
        with self.log.open("wb") as log:
            self.child = self.inspector.attach(launch(
                [*self.inspector.args, *argv], stdout=log, stderr=log,
                pass_fds=self.inspector.fds, **kwargs))
        self.case.children.append((self.role, self.child))
        return self

    def bind(self, driver):
        self.driver = driver
        self.window = driver.find(self.child.pid, WAIT_MS)
        driver.focus()
        self.case.note(f"{self.role}: PID {self.child.pid} window {self.window:#x}")
        return self.window

    def frame(self):
        """One settled frame; the adapter answers once queued input is disposed (A9)."""
        self.inspector.deadline = time.monotonic() + WATCHDOG
        snapshot = self.inspector.receive(self.inspector.send(self.driver))
        if len(snapshot.frames) != 1:
            raise Refused("the pinned oracle must provide exactly one settled frame")
        frame = snapshot.settled
        if frame.window_width > SCREEN[0] or frame.window_height > SCREEN[1]:
            raise Refused(f"window {frame.window_width}x{frame.window_height} exceeds the mapping")
        return frame

    def tab(self, frame):
        """(title, first document row) of the active tab."""
        titles, active = tab_strip(frame)
        if active is None:
            raise Refused(f"{self.role}: no single active tab in the strip")
        rows = document_rows(frame)
        return titles[active], rows[0] if rows else ""

    def ring(self, bound):
        """Walk every live tab with Ctrl+PageDown until the walk returns to its start.

        Tab identities are unique by fixture construction (title and first row), so the
        first repeat closes the ring. Off-strip tabs are visited, not inferred."""
        first = self.tab(self.frame())
        walked, seen = [first], {first}
        for _ in range(bound):
            self.driver.chord("Control_L", "Next")
            current = self.tab(self.frame())
            if current == first:
                return walked
            if current in seen:
                raise Refused(f"{self.role}: tab ring repeats {current!r} before its start")
            walked.append(current)
            seen.add(current)
        raise Refused(f"{self.role}: tab ring did not close within {bound} steps")

    def steps(self, walked, start, index):
        """Move from ring entry `start` to `index` the shorter way and verify the arrival."""
        forward = (index - start) % len(walked)
        key, count = ("Next", forward) if forward <= len(walked) - forward else \
            ("Prior", len(walked) - forward)
        for _ in range(count):
            self.driver.chord("Control_L", key)
        observed = self.tab(self.frame())
        if observed != walked[index]:
            raise Refused(f"{self.role}: expected tab {walked[index]!r}, found {observed!r}")

    def copy_at(self, walked, index):
        """Copy ring entry `index`, then return to the ring's start."""
        self.steps(walked, 0, index)
        data = self.copy()
        self.steps(walked, index, 0)
        return data

    def copy(self):
        """One Select All and Copy, then CLIPBOARD read from this editor's window.

        A snapshot reply does not order the editor's SetSelectionOwner against the driver's
        query at the X server, so this waits on the ownership transition (recovery's bounded
        wait: a third owner or watchdog expiry is ERROR)."""
        return copied_text(self.driver, self.window)

    def type_text(self, text):
        for char in text:
            self.driver.key(char)

    def hot_exit(self):
        """Ctrl+Q with unsaved tabs: v1 exits 0 and names the journal it keeps."""
        if self.child.poll() is not None:
            raise Refused(f"{self.role} exited before Ctrl+Q: {self.child.returncode}")
        self.driver.chord("Control_L", "q")
        if not exited(self.child, WATCHDOG):
            raise TimeoutError(f"{self.role} did not exit before watchdog after Ctrl+Q")
        status = self.child.wait()
        if status != 0:
            raise Refused(f"{self.role} exited {status} after Ctrl+Q")
        found = JOURNAL_LINE.findall(self.log.read_bytes())
        if len(found) != 1:
            raise Refused(f"{self.role} named {len(found)} kept journals")
        return Path(os.fsdecode(found[0]))

    def law2(self):
        return law2_count(self.log.read_bytes())._asdict()


def handoff(case, sandbox, paths, label):
    """Forward `paths` to the primary through a second launch; True admitted, False refused."""
    log_path = case.artifacts / "handoffs" / f"{label}.log"
    with log_path.open("wb") as log:
        child = sandbox.relay([str(path) for path in paths], stdout=log, stderr=log)
    if not exited(child, WATCHDOG):
        child.kill()
        child.wait()
        raise TimeoutError(f"handoff {label} did not exit before watchdog")
    status = child.wait()
    output = log_path.read_bytes()
    if status == 0 and output == b"":
        return True
    if status == 2 and output == REFUSED:
        return False
    raise Refused(f"handoff {label}: neither admitted nor a capacity refusal: "
                  f"exit {status}, {output!r}")


def measure_capacity(case, target):
    """Phase 1: C in a sacrificial fresh profile, by public opens until a capacity refusal."""
    case.step("capacity: sacrificial profile")
    with Sandbox(case.artifacts, target, display=case.display) as sandbox, ExitStack() as stack:
        _, driver = stack.enter_context(case.server(sandbox, "capacity"))

        def probes(start, count):
            paths = [sandbox.fixtures / probe_name(start + i) for i in range(count)]
            for path in paths:
                write_fixture(path, content(path.name))
            return paths

        probe = Editor(case, stack, "sacrificial").spawn(sandbox.start, [str(*probes(0, 1))])
        window = probe.bind(driver)
        sandbox.record_server()
        admitted, batch, attempts = 1, BATCH, []
        # A refused request admits nothing (buffers.c: the whole request is refused), so
        # halving the batch after each refusal ends at a refused one-path request exactly
        # when no new tab fits: the live tab count is then C.
        while True:
            if admitted + batch > PROBE_BOUND:
                raise Refused(f"no capacity refusal within {PROBE_BOUND} admitted tabs")
            paths = probes(admitted, batch)
            ok = handoff(case, sandbox, paths, f"probe-{len(attempts):03d}")
            attempts.append([admitted, batch, ok])
            if ok:
                admitted += batch
            elif batch == 1:
                break
            else:
                for path in paths:
                    path.unlink()
                batch //= 2
        refused_name = probe_name(admitted)
        # Control: the editor is alive and an already-open path is still accepted, so the
        # refusal was about a new slot, not a broken primary.
        reopen = handoff(case, sandbox, [sandbox.fixtures / probe_name(0)], "probe-reopen")
        alive = probe.child.poll() is None
        last = probe.tab(probe.frame())
        stop(probe.child, WATCHDOG)
        case.facts["capacity"] = {
            "C": admitted, "window": window, "pid": probe.child.pid,
            "first": probe_name(0), "last_admitted": probe_name(admitted - 1),
            "refused": refused_name, "receipt": REFUSED.decode().strip(),
            "attempts": attempts, "reopen_admitted": reopen, "alive_after_refusal": alive,
            "active_after_reopen": list(last),
        }
        return admitted, {"refused_one_path": True, "reopen_admitted": reopen,
                          "alive": alive, "active_after_reopen": list(last)}


def producers(case, sandbox, driver, stack, capacity, fill):
    """Phase 2: the older source O, then the newer filler source F, both hot-exited."""
    root = sandbox.fixtures
    disk = {}
    for name, data in OLDER:
        write_fixture(root / name, data)
        disk[name] = data
    for index in range(fill):
        name = filler_name(index)
        write_fixture(root / name, content(name))
        disk[name] = content(name)
    token = os.urandom(4).hex()
    dirty_marker = f"x{token}"
    untitled_marker = f"u{token}"
    expected_dirty = {DIRTY_FILE: OLDER[2][1] + dirty_marker.encode(),
                      UNTITLED: untitled_marker.encode()}
    data_dir = sandbox.data_dir

    case.step("producers: filler starts as the IPC primary")
    filler = Editor(case, stack, "filler").spawn(sandbox.start, [str(root / filler_name(0))])
    filler.bind(driver)
    sandbox.record_server()
    started = sessions(data_dir)
    if len(started) != 1:
        raise Refused(f"filler start left journals {sorted(started)}")
    filler_source = data_dir / started.pop()

    case.step("producers: older starts as a --new-instance peer while the filler lives")
    older = Editor(case, stack, "older").spawn(
        sandbox.relay, ["--new-instance", *(str(root / name) for name, _ in OLDER)])
    older.bind(driver)
    both = sessions(data_dir) - {filler_source.name}
    if len(both) != 1:
        raise Refused(f"older start left journals {sorted(both)}")
    older_source = data_dir / both.pop()

    case.step("producers: dirty the named file and add the untitled marker")
    for _ in range(len(OLDER)):
        if older.tab(older.frame())[0] == DIRTY_FILE:
            break
        older.driver.chord("Control_L", "Next")
    else:
        raise Refused(f"older never activated {DIRTY_FILE}")
    older.driver.chord("Control_L", "End")
    older.type_text(dirty_marker)
    older.driver.chord("Control_L", "Next")
    if older.tab(older.frame())[0] != "c2.txt":
        raise Refused("older: c2.txt does not follow m.txt")
    older.driver.chord("Control_L", "n")
    older.type_text(untitled_marker)
    older_ring = older.ring(len(OLDER) + UNSAVED)
    # All five tabs fit O's strip, so its titles are the tab order a hot exit records (TABS).
    older_strip = tab_strip(older.frame())[0]
    older_journal = older.hot_exit()
    # Count rule: the producer's typing window is recorded, not asserted; v1 counts its known
    # typing-path allocation there (xcb read_packet, docs/decisions/v2-he7.23.md).
    case.facts["law2_older_typing"] = older.law2()
    older_bytes = older_source.read_bytes()
    (case.artifacts / "older-source.journal").write_bytes(older_bytes)

    case.step("producers: the filler admits its clean files and exits last")
    filler.bind(driver)
    pending = [root / filler_name(i) for i in range(1, fill)]
    admitted = 1
    for start in range(0, len(pending), BATCH):
        if not handoff(case, sandbox, pending[start:start + BATCH], f"fill-{start // BATCH:03d}"):
            break
        admitted += len(pending[start:start + BATCH])
    filler_last = filler.tab(filler.frame())
    filler_journal = filler.hot_exit()
    filler_bytes = filler_source.read_bytes()
    (case.artifacts / "filler-source.journal").write_bytes(filler_bytes)
    # Setup, not editor behavior: discovery restores the newest source first by st_mtim in ns,
    # ties broken by path (P, v1 src/editor/recovery.c newer_source, src/os/os_posix.c ts_ns).
    # F's last write follows O's exit causally, but a tied stamp or a clock step can hide that,
    # so with both producers exited the case stamps the order: whole seconds one apart, which
    # any Linux filesystem with at most 1 s granularity stores exactly. A stamp read back
    # differently is ERROR.
    stamped = max(older_source.stat().st_mtime_ns, filler_source.stat().st_mtime_ns)
    older_mtime = stamped - stamped % 1_000_000_000
    filler_mtime = older_mtime + 1_000_000_000
    os.utime(older_source, ns=(older_mtime, older_mtime))
    os.utime(filler_source, ns=(filler_mtime, filler_mtime))
    if (older_source.stat().st_mtime_ns, filler_source.stat().st_mtime_ns) != \
            (older_mtime, filler_mtime):
        raise Refused("producer journal stamps did not read back as set")

    # The older ring as a cycle starting at c0; the untitled tab's title is v1's own.
    names = [UNTITLED if title not in dict(OLDER) else title for title, _ in older_ring]
    if "c0.txt" not in names:
        raise Refused(f"older ring lacks c0.txt: {older_ring}")
    turn = names.index("c0.txt")
    rows = {name: row for name, (_, row) in zip(names, older_ring)}
    observed = {
        "older_strip": [UNTITLED if title not in dict(OLDER) else title for title in older_strip],
        "older_ring": names[turn:] + names[:turn],
        "older_rows": rows,
        "older_journal": older_journal == older_source,
        "filler_admitted": admitted,
        "filler_last": list(filler_last),
        "filler_journal": filler_journal == filler_source,
        "below_startup_scan": max(len(older_bytes), len(filler_bytes)) < STARTUP_SCAN_BYTES,
    }
    expected = {
        "older_strip": [name for name, _ in OLDER] + [UNTITLED],
        "older_ring": [name for name, _ in OLDER] + [UNTITLED],
        "older_rows": {**{name: data.decode() for name, data in OLDER},
                       DIRTY_FILE: expected_dirty[DIRTY_FILE].decode(),
                       UNTITLED: untitled_marker},
        "older_journal": True, "filler_admitted": fill,
        "filler_last": [filler_name(fill - 1), content(filler_name(fill - 1)).decode()],
        "filler_journal": True, "below_startup_scan": True,
    }
    case.facts["producers"] = {
        "older_source": older_source.name, "older_bytes": len(older_bytes),
        "older_mtime_ns": older_mtime, "filler_source": filler_source.name,
        "filler_bytes": len(filler_bytes), "filler_mtime_ns": filler_mtime,
        "fill": fill, "U": UNSAVED, "C": capacity, "token": token,
        "older_ring_raw": older_ring,
    }
    case.check("producers-admitted", observed, expected)
    return disk, expected_dirty, older_source, older_bytes, filler_source, older_ring


def classify(walked):
    """Split a restored ring into fillers, older clean files and the dirty tabs."""
    olders = dict(OLDER)
    fillers, older_clean, dirty, other = [], [], {}, []
    for index, (title, row) in enumerate(walked):
        if re.fullmatch(r"f\d{4}\.txt", title) and row.encode() == content(title):
            fillers.append(title)
        elif title in OLDER_CLEAN and row.encode() == olders[title]:
            older_clean.append(title)
        elif title == DIRTY_FILE:
            dirty[DIRTY_FILE] = index
        elif title not in olders and not re.fullmatch(r"[fp]\d{4}\.txt", title):
            dirty.setdefault(UNTITLED, index)
            if dirty[UNTITLED] != index:
                other.append(title)
        else:
            other.append(title)
    return fillers, older_clean, dirty, other


def status_row(frame, dropped):
    """The notice as the pin's status row shows it, with geometry established independently.

    v1 sizes the grid as window / cell (P, src/editor/editor.c, below its max_cols clamp) and
    gives the status the grid's last row (P, src/editor/paint.c editor_status_overlay), which
    the inspector reports as (0, rows - 1, columns, 1). The text is read from that row and the
    notice clipped to that width, so an empty, short or misplaced rectangle cannot match."""
    columns = frame.window_width // frame.cell_width
    rect = frame.bottom_row
    observed = {"columns": frame.columns, "bottom_row": [rect.x, rect.y, rect.width, rect.height],
                "text": frame.cell_text(frame.rows - 1).rstrip(" ")}
    expected = {"columns": columns, "bottom_row": [0, frame.rows - 1, columns, 1],
                "text": NOTICE.format(u=UNSAVED, d=dropped)[:columns].rstrip(" ")}
    return observed, expected


def restore(case, sandbox, capacity, fill, disk, expected_dirty,
            older_source, older_bytes, filler_source, older_ring):
    """Phase 3: the pressured relaunch, then a SIGKILL and a second relaunch, each on its own
    server."""
    room_clean = capacity - fill - UNSAVED  # live room left once F and every unsaved tab fit
    with case.server(sandbox, "restored") as (_, driver), ExitStack() as stack:
        case.step("restore: argument-free relaunch under pressure")
        first = Editor(case, stack, "restored").spawn(sandbox.restart, [])
        first.bind(driver)
        sandbox.record_server()
        frame = first.frame()
        data_after = sorted(p.name for p in sandbox.data_dir.iterdir())
        walked = first.ring(capacity + 1)
        (case.artifacts / "restored-ring.json").write_text(json.dumps(walked) + "\n")
        fillers, older_clean, dirty, other = classify(walked)
        dropped = len(OLDER_CLEAN) - len(older_clean)
        notice, notice_expected = status_row(frame, dropped)
        case.facts["restored"] = {"live": len(walked), "fillers": len(fillers),
                                  "older_clean": older_clean, "dirty_positions": dirty,
                                  "unclassified": other, "notice": notice, "data_dir": data_after}
        admitted_fillers = [filler_name(i) for i in range(fill)]
        if case.negative == "no-pressure":
            # The control is credited only from the exact unpressured restore under ample C: both
            # fillers and all five older tabs with their identities and dirty rows. Anything else
            # (a dropped dirty tab, a wrong filler) is not the deliberate red.
            whole = sorted([(name, content(name).decode()) for name in admitted_fillers] + older_ring)
            if capacity <= len(whole) or sorted(walked) != whole:
                raise Refused(f"no-pressure control: restored {sorted(walked)!r}, not the "
                              f"unpressured {whole!r} under C = {capacity}")
        case.check("partial-restore-triggered",
                   {"live": len(walked), "fillers": sorted(fillers),
                    "older_restored_whole": len(older_clean) == len(OLDER_CLEAN) and len(dirty) == 2,
                    "unclassified": other},
                   {"live": capacity, "fillers": admitted_fillers, "older_restored_whole": False,
                    "unclassified": []})
        case.check("notice-visible", notice, notice_expected)
        copies = {key: first.copy_at(walked, dirty[key]) if key in dirty else None
                  for key in (DIRTY_FILE, UNTITLED)}
        case.check("dirty-restored", copies, expected_dirty)
        # v1's rule (P, src/editor/recovery.c:plan_partial, "Unmodified tabs in recorded tab
        # order"): after every unsaved buffer is placed, the clean tabs of the last TABS record are
        # kept in that order, skipping the PAINT-named buffer and vanished targets, while
        # spare = room - live unsaved (- 1 with closed ones) remains. Fixture inputs: TABS is
        # c0 c1 m c2 untitled (producers-admitted, older_strip); every target exists; PAINT is 0
        # because each older tab presented during the producer walk (editor_journal_painted
        # appends PAINT 0); spare = room_clean = C - fill - U = 1. So c0 survives, c1 and c2 drop.
        # By path, publicly: at C live tabs a forwarded open of a live path is admitted (it
        # activates that tab) and of an absent path is refused for capacity (no room exists:
        # partial-restore-triggered asserted live == C).
        paths = {name: str(sandbox.fixtures / name) for name in OLDER_CLEAN}
        admitted = {paths[name]: handoff(case, sandbox, [sandbox.fixtures / name], f"restored-{name}")
                    for name in OLDER_CLEAN}
        active = list(first.tab(first.frame()))

        def clean_choice(kept):
            return {"kept": [paths[name] for name in OLDER_CLEAN if name in kept],
                    "dropped": [paths[name] for name in OLDER_CLEAN if name not in kept],
                    "handoff_admitted": {paths[name]: name in kept for name in OLDER_CLEAN},
                    "active_after_handoffs": [kept[-1], dict(OLDER)[kept[-1]].decode()]}

        observed = clean_choice(older_clean) if older_clean else {"kept": [], "dropped": list(paths.values())}
        observed.update(handoff_admitted=admitted, active_after_handoffs=active)
        case.check("clean-survivor-tab-order", observed, clean_choice(OLDER_CLEAN[:room_clean]),
                   wrong=clean_choice(OLDER_CLEAN[-room_clean:]))
        aside = [name for name in data_after
                 if name.startswith(older_source.name + ".set-aside-")]
        if filler_source.name in data_after:
            raise Refused("the filler source is still present: no durable restore checkpoint")
        case.check("omitted-state-recoverable",
                   {"set_aside": len(aside),
                    "bytes": [hashlib.sha256((sandbox.data_dir / name).read_bytes()).hexdigest()
                              for name in aside]},
                   {"set_aside": 1, "bytes": [hashlib.sha256(older_bytes).hexdigest()]})

        case.step("restore: SIGKILL before any save, then relaunch")
        with os.fdopen(os.pidfd_open(first.child.pid), "rb") as pidfd:
            signal.pidfd_send_signal(pidfd.fileno(), signal.SIGKILL)
        if not exited(first.child, WATCHDOG):
            raise TimeoutError("restored editor survived SIGKILL past the watchdog")
        if first.child.wait() != -signal.SIGKILL:
            raise Refused(f"restored editor exit {first.child.returncode}, expected SIGKILL")
    with case.server(sandbox, "relaunched") as (_, driver), ExitStack() as stack:
        second = Editor(case, stack, "relaunched").spawn(sandbox.restart, [])
        second.bind(driver)
        sandbox.record_server()
        second_notice = second.frame().bottom_row_text().rstrip(" ")
        again = second.ring(capacity + 1)
        (case.artifacts / "relaunched-ring.json").write_text(json.dumps(again) + "\n")
        _, _, dirty_again, _ = classify(again)
        copies_again = {key: second.copy_at(again, dirty_again[key]) if key in dirty_again else None
                        for key in (DIRTY_FILE, UNTITLED)}
        case.facts["relaunched"] = {"live": len(again), "notice": second_notice}
        case.check("second-recovery",
                   {"tabs": sorted(again), "dirty": copies_again},
                   {"tabs": sorted(walked), "dirty": expected_dirty})

        case.step("restore: clean close and disk check")
        status = stop(second.child, WATCHDOG)
        if status != 0:
            raise Refused(f"relaunched editor clean close returned {status}")
        case.check("disk-intact",
                   {name: (sandbox.fixtures / name).read_bytes() for name in sorted(disk)},
                   {name: disk[name] for name in sorted(disk)})
    return second


def scenario(case, target):
    capacity, receipt = measure_capacity(case, target)
    case.check("capacity-measured", receipt,
               {"refused_one_path": True, "reopen_admitted": True, "alive": True,
                "active_after_reopen": [probe_name(0), content(probe_name(0)).decode()]})
    # The filler must admit at least one file after the older editor's hot exit, or nothing
    # makes its journal the newer source (no-pressure red, run contract-capacity-jk_oelwv:
    # filler_newer was False with fill = 1). The no-pressure control keeps that write: fill = 2.
    fill = 2 if case.negative == "no-pressure" else capacity - UNSAVED - 1
    if fill < 2:
        raise Refused(f"capacity {capacity} leaves no room for a filler")
    with Sandbox(case.artifacts, target, display=case.display) as sandbox:
        try:
            with case.server(sandbox, "producers") as (_, driver), ExitStack() as stack:
                produced = producers(case, sandbox, driver, stack, capacity, fill)
            last = restore(case, sandbox, capacity, fill, *produced)
            counts = {"relaunched": last.law2()}
            case.facts["law2"] = counts
            # Count rule (wave0-contract-design.md): on a server of its own the relaunched editor
            # counts 4 on v1 (v2-he7.33; the 0 of run contract-capacity-v8wtgv9p was a shared
            # server's history), so the case requires a healthy instrument and the manifest's
            # per-target predicate decides the count: recorded on v1-pin, zero on v2.
            case.check("law2-count", counts["relaunched"]["scopes"] >= 1, True,
                       law2=counts["relaunched"])
        finally:
            if sandbox.data_dir.exists():
                shutil.copytree(sandbox.data_dir, case.artifacts / "data")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", required=True, type=Path,
                        help="absolute inspected v1 build/contract-oracle directory")
    parser.add_argument("--display", required=True)
    parser.add_argument("--negative", choices=NEGATIVES,
                        help="red control: a wrong expectation or a fixture without pressure")
    records.digests_argument(parser)
    args = parser.parse_args()
    if not CONTRACT_DISPLAY.fullmatch(args.display):
        parser.error("--display must be a contract display, :120 through :199")
    if not args.build_dir.is_absolute():
        parser.error("--build-dir must be absolute")

    BUILD.mkdir(exist_ok=True)
    artifacts = Path(tempfile.mkdtemp(prefix="contract-capacity-", dir=BUILD))
    (artifacts / "handoffs").mkdir()
    case = Case(artifacts, args.negative, args.display)
    try:
        battery = Path("/sys/class/power_supply/BAT0/status").read_text(encoding="ascii").strip()
    except OSError:
        battery = None
    manifest = {"run_id": artifacts.name, "display": args.display, "geometry": GEOMETRY,
                "battery_status": battery, "load_average": os.getloadavg(),
                "negative": args.negative, "oracle_build": str(args.build_dir),
                "started": time.time()}
    print(f"R4 run artifacts in {artifacts}", file=sys.stderr, flush=True)
    try:
        target = RunTarget("v1", args.build_dir / EDITOR, args.build_dir,
                           args.digests[str(args.build_dir / EDITOR)])
        manifest.update(role=target.role, oracle=str(target.executable), digests=args.digests)
        scenario(case, target)
        if tuple(case.asserted) != ASSERTIONS:
            case.stage = "assertion-inventory"
            raise Refused(f"assertion inventory incomplete: {case.asserted}")
    except TeardownError as exc:
        # A server that outlived its stop is ERROR beside the failure it interrupted, if any.
        if exc.pending is not None:
            case.failure(exc.pending)
        case.emit("teardown", "ERROR", f"{case.stage}: TeardownError: {exc}")
    except Mismatch as exc:
        case.failure(exc)
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        case.failure(exc)
    finally:
        manifest["finished"] = time.time()
        manifest["status"] = case.status
        (artifacts / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                                 encoding="utf-8")
    return records.EXIT[case.status]


if __name__ == "__main__":
    sys.exit(main())
