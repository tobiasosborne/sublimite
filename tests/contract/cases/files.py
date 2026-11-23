"""Run file contracts R3 and R5 once against the pinned, inspected v1 oracle.

Every wait is on an event: a snapshot reply on the inspector socket (SNAP, or
NEXT after a named submit serial), an inotify event armed before the keys that
cause it, or the editor's pidfd. The only timers are hang detectors; an expired
one is ERROR, never FAIL. Every editor launch gets a private X server no earlier
case or launch used (tests/contract/xserver.py).

Records and exit statuses (PASS 0, FAIL 3, ERROR 4) follow tests/contract/records.py.
"""

import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_oracle import EDITOR
from files_fixtures import BINARY_SEED, SEARCH_FILES, binary_bytes, search_results, search_tree
import records
from sandbox import CONTRACT_DISPLAY, Refused, RunTarget, Sandbox
import snapwire as wire
from test_oracle_inspector import (IN_MOVED_TO, Inspector, capacity, directory_watch, law2_count,
                                   wait_for_file_event)
from xserver import TeardownError, fresh_display


ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "build"
# Reuse the X driver conformance fixture's operation and wrapper watchdogs.
WATCHDOG = 15
WAIT_MS = WATCHDOG * 1000
SCREEN = (1280, 1024)  # Xvfb(1)'s default screen geometry.
GEOMETRY = f"{SCREEN[0]}x{SCREEN[1]}x24"
# The mapping bound at this screen; each frame's window must fit the screen, or ERROR.
CAPACITY = capacity(*SCREEN)
ASSERTIONS = {
    "R3": ("results-between-a-and-b", "save-b-bytes", "save-a-bytes",
           "results-text-still-visible", "other-files-unchanged", "law2-count"),
    "R5": ("open-alive", "header-painted", "adjacent-text-and-sibling-save", "open-exit",
           "reopen-alive", "settled-binary-tab-revisit", "reopen-exit", "bytes-unchanged",
           "law2-count"),
}
EVIDENCE = {"results-between-a-and-b": "B", "results-text-still-visible": "B",
            "header-painted": "B", "settled-binary-tab-revisit": "B",
            "adjacent-text-and-sibling-save": "A+B", "law2-count": "B"}
NEGATIVES = tuple(f"{case}.{assertion}" for case, assertions in ASSERTIONS.items()
                  for assertion in assertions) + ("R3.save-signal", "R3.snapshot-reply",
                                                   "R3.search-results", "R5.nonzero-exit")
RESULTS_TITLE = "Find Results"
TAB_TRAILER = 2  # v1's last two tab cells hold the modified dot and spacing.
UNRESOLVED = "▯"  # Stands for an unresolved cell; never part of an expected ASCII text.
SEARCH_END = re.compile(r"\d+ match(es)? (across \d+ files|in 1 file)$|Search (failed|truncated|cancelled)")
SIBLING = b"sibling text\n"
IN_ACCESS = 0x00000001


class Mismatch(Exception):
    """An observed value contradicted the assertion."""


def describe(value):
    if isinstance(value, bytes):
        # Full binary evidence stays in the retained fixture.
        if len(value) <= 1024:
            return {"hex": value.hex(), "size": len(value)}
        return {"sha256": hashlib.sha256(value).hexdigest(), "size": len(value)}
    raise TypeError(f"cannot encode {type(value).__name__}")


def exited(process, timeout):
    """Wait on the process's pidfd; True once it can be reaped without blocking."""
    if process.poll() is not None:
        return True
    pidfd = os.pidfd_open(process.pid)
    try:
        with selectors.DefaultSelector() as ready:
            ready.register(pidfd, selectors.EVENT_READ)
            return bool(ready.select(timeout))
    finally:
        os.close(pidfd)


def row_text(frame, row, start, stop):
    """Displayed text of a cell range; unresolved cells read as UNRESOLVED (T3).

    Expected texts are ASCII, so a required match crossing an unresolved cell fails."""
    offset = row * frame.columns
    return "".join(UNRESOLVED if cell.kind == wire.CellKind.UNRESOLVED else
                   " " if cell.kind == wire.CellKind.BLANK else cell.text
                   for cell in frame.cells[offset + start:offset + stop])


def document_rows(frame):
    d = frame.document
    return [row_text(frame, y, d.x, d.x + d.width).rstrip(" ") for y in range(d.y, d.y + d.height)]


def tab_strip(frame):
    """Visible tab titles in strip order and the active tab's index."""
    titles = [row_text(frame, tab.rect.y, tab.rect.x, tab.rect.x + tab.rect.width - TAB_TRAILER).strip()
              for tab in frame.tabs]
    active = [i for i, tab in enumerate(frame.tabs) if tab.active]
    return titles, active[0] if len(active) == 1 else None


def active_title(frame):
    titles, active = tab_strip(frame)
    return None if active is None else titles[active]


def hard_wrapped(text, width, height):
    """v1's Find Results rows for this fixture: a line wider than the document breaks
    every `width` cells. The fixture's only wide lines are absolute paths; a space in
    one would put the line outside this model, which is then a harness ERROR."""
    rows = []
    for line in text.split("\n"):
        if len(line) > width and " " in line:
            raise Refused(f"Find Results line outside the hard-wrap model: {line!r}")
        rows += [line[i:i + width] for i in range(0, len(line), width)] or [""]
    if len(rows) > height:
        raise Refused(f"Find Results needs {len(rows)} rows; the document shows {height}")
    return [row.rstrip(" ") for row in rows] + [""] * (height - len(rows))


class Case:
    def __init__(self, case_id, artifacts, negative, display):
        self.id = case_id
        self.display = display
        self.artifacts = artifacts / case_id
        self.artifacts.mkdir(mode=0o700)
        self.records = artifacts / "results.jsonl"
        self.transcript = self.artifacts / "snapshots.jsonl"
        self.run_id = artifacts.name
        self.seed = BINARY_SEED if case_id == "R5" else None
        self.negative = negative
        self.children = []
        self.logs = []
        self.asserted = []
        self.results = []
        self.inspector = self.child = self.driver = None
        self.ticket = self.serial = None
        self.step("setup", "private sandbox and authenticated X driver")

    def note(self, message):
        with (self.artifacts / "steps.log").open("a", encoding="utf-8") as file:
            print(message, file=file)

    def step(self, assertion, expected, *, part=None):
        """Enter a step. A prerequisite `part` is scored under its listed `assertion`."""
        self.assertion = assertion
        self.part = part
        self.expected = expected
        self.observed = self.ordinary = None
        self.note(assertion if part is None else f"{assertion}/{part}")

    def emit(self, status, *, reason=None):
        record = {
            "run_id": self.run_id, "case_id": self.id, "assertion_id": self.assertion,
            "evidence": EVIDENCE.get(self.assertion, "A"),
            "first_required_wave": 3 if self.id == "R3" else 2,
            "status": status, "predicate": reason or self.assertion,
            "step": self.part or self.assertion,
            "observed": self.observed, "expected": self.expected, "ordinary": self.ordinary,
            "ticket": self.ticket, "submit_serial": self.serial,
            "seed": self.seed, "negative": self.negative,
            "child_status": [{"pid": child.pid, "exit": child.poll()} for child in self.children],
            "artifacts": str(self.artifacts),
        }
        with self.records.open("a", encoding="utf-8") as file:
            print(json.dumps(record, default=describe), file=file)
        records.emit(record, default=describe)
        if status != "PASS":
            print(f"{self.id}.{self.assertion}: {status} {record['predicate']}", file=sys.stderr)
        self.results.append(status)

    @property
    def status(self):
        for status in ("ERROR", "FAIL"):
            if status in self.results:
                return status
        return "PASS" if self.results else "ERROR"

    def check(self, assertion, observe, expected, wrong):
        """Compare once. `observe` is a value or a function of the active expectation.

        Under this assertion's red control the observation must first meet the ordinary
        expectation (kept as `ordinary`); only then is the wrong one expected."""
        self.step(assertion, expected)
        if assertion in self.asserted or assertion not in ASSERTIONS[self.id]:
            self.step("assertion-inventory", "each known assertion executes once")
            raise Refused(f"duplicate or unknown assertion {assertion}")
        self.asserted.append(assertion)
        self.observed = observe(expected) if callable(observe) else observe
        if self.negative == f"{self.id}.{assertion}":
            if self.observed != expected:
                raise Refused(f"{assertion}: the control's observation fails the ordinary "
                              "expectation, so its red would not be the deliberate one")
            self.ordinary = {"expected": expected, "observed": self.observed}
            self.expected = wrong
            self.observed = observe(wrong) if callable(observe) else self.observed
        if self.observed != self.expected:
            raise Mismatch(f"{assertion}: observed value differs from expectation")
        self.emit("PASS")
        # Work after a PASS belongs to no assertion until the next step names one.
        self.step(f"after-{assertion}", None)

    def failure(self, exc):
        # The first failure in a case is final; its later steps do not run.
        if "FAIL" in self.results or "ERROR" in self.results:
            return
        reason = f"{type(exc).__name__}: {exc}"
        if not isinstance(exc, Mismatch):
            self.emit("ERROR", reason=reason)
        elif self.assertion in ASSERTIONS[self.id]:
            self.emit("FAIL", reason=reason)
        else:
            # A FAIL is scored only under a listed ID; a mismatch elsewhere is a harness bug.
            self.emit("ERROR", reason=f"mismatch outside a listed assertion: {reason}")

    def teardown(self, exc):
        """A server that outlived its stop is ERROR even after this case's first failure,
        which is recorded first when the server's block was raising it."""
        if exc.pending is not None:
            self.failure(exc.pending)
        self.step("teardown", "owned X server stopped within its deadline")
        self.emit("ERROR", reason=f"TeardownError: {exc}")

    def server(self, sandbox, label):
        """A fresh private server and driver for one launch; the case's Xauthority gets its
        cookie. Yields (server, driver)."""
        return fresh_display(self.display, self.artifacts, f"Xvfb-{label}", sandbox.env,
                             timeout=WATCHDOG * 2, geometry=GEOMETRY)

    def launch(self, stack, start, paths, driver):
        """Start one inspected editor, owned by `stack` for its lifetime."""
        self.inspector = Inspector(stack, CAPACITY)
        self.driver = driver
        log_path = self.artifacts / f"editor-{len(self.children) + 1}.log"
        with log_path.open("wb") as log:
            self.child = self.inspector.attach(start(
                [*self.inspector.args, *map(str, paths)], stdout=log, stderr=log,
                pass_fds=self.inspector.fds))
        self.children.append(self.child)
        self.logs.append(log_path)
        return self.child

    def bind(self, sandbox):
        window = self.driver.find(self.child.pid, WAIT_MS)
        self.driver.focus()
        sandbox.record_server()
        self.note(f"focused PID {self.child.pid} window {window:#x}")
        return window

    def frame(self, action, *, after=None, deadline=None):
        """One settled frame: SNAP, or NEXT after the submit serial `after`."""
        inspector = self.inspector
        inspector.deadline = deadline or time.monotonic() + WATCHDOG
        operation = wire.Operation.SNAP if after is None else wire.Operation.NEXT
        previous = 0 if after is None else after
        self.ticket, self.serial = inspector.ticket + 1, None
        if self.negative == f"{self.id}.snapshot-reply":
            # Withhold the marker. The live editor keeps its endpoint open, so the
            # receive path must reach its watchdog, independently of EOF.
            self.note(f"negative control withheld ticket {self.ticket}")
            inspector.receive(wire.Request(operation, self.ticket, previous))
            raise wire.ProtocolError("received a reply to the withheld marker")
        snapshot = inspector.receive(inspector.send(self.driver, operation, previous))
        if len(snapshot.frames) != 1:
            raise wire.ProtocolError("the pinned oracle must provide exactly one settled frame")
        frame = snapshot.settled
        if frame.window_width > SCREEN[0] or frame.window_height > SCREEN[1]:
            raise Refused(f"window {frame.window_width}x{frame.window_height} exceeds the mapping bound")
        self.serial = frame.submit_serial
        titles, active = tab_strip(frame)
        with self.transcript.open("a", encoding="utf-8") as log:
            print(json.dumps({"action": action, "request": operation.name, "ticket": self.ticket,
                              "after_serial": previous, "submit_serial": self.serial,
                              "tabs": titles, "active": active, "document": vars(frame.document),
                              "rows": document_rows(frame),
                              "unresolved": sum(cell.kind == wire.CellKind.UNRESOLVED
                                                for cell in frame.cells)}), file=log)
        return frame

    def quit(self):
        """Ctrl+Q, reaped on the editor's pidfd; the caller scores the exit status."""
        if self.child.poll() is not None:
            raise Refused(f"editor exited before deliberate quit: {self.child.returncode}")
        if self.negative == "R5.nonzero-exit":
            # A deliberately nonzero reaped exit in place of Ctrl+Q's.
            self.note("negative control sent SIGKILL instead of Ctrl+Q")
            signal.pidfd_send_signal(self.inspector.pidfd, signal.SIGKILL)
        else:
            self.driver.chord("Control_L", "q")
        if not exited(self.child, WATCHDOG):
            raise TimeoutError("editor did not exit before watchdog after Ctrl+Q")
        return self.child.wait()

    def allocation_count(self, part):
        """The passive Law-2 count of the last editor's clean exit (one validated report)."""
        self.step("law2-count", "an active guard's report with measured scopes", part=part)
        count = law2_count(self.logs[-1].read_bytes())
        self.note(f"law2 {count}")
        return count.allocations


def search_frame(case, expected, wrong):
    """NEXT until a frame draws the search's end line, then compare that frame once; the
    R3.search-results red expects `wrong` only after the ordinary `expected` held."""
    case.step("results-between-a-and-b", "the completed Find Results frame draws the fixture results",
              part="search-readiness")
    deadline = time.monotonic() + WATCHDOG
    frame = case.frame("search submitted", deadline=deadline)
    while not (active_title(frame) == RESULTS_TITLE
               and any(SEARCH_END.match(row) for row in document_rows(frame))):
        frame = case.frame("search progress", after=frame.submit_serial, deadline=deadline)
    d = frame.document
    case.expected = hard_wrapped(expected, d.width, d.height)
    case.observed = document_rows(frame)
    if case.negative == "R3.search-results":
        if case.observed != case.expected:
            raise Refused("search-results: the control's observation fails the ordinary "
                          "expectation, so its red would not be the deliberate one")
        case.ordinary = {"expected": case.expected, "observed": case.observed}
        case.expected = hard_wrapped(wrong, d.width, d.height)
    if case.observed != case.expected:
        raise Mismatch("completed Find Results differ from the fixture results")
    return frame


def save(case, assertion, path, original):
    case.step(assertion, f"IN_MOVED_TO for {path.name}", part=f"save-{path.stem}-signal")
    with directory_watch(path.parent, IN_MOVED_TO) as watch:
        case.observed = path.read_bytes()
        if case.observed != original:
            raise Mismatch(f"{path.name} changed before its save")
        if case.negative != "R3.save-signal":
            case.driver.chord("Control_L", "s")
        else:
            case.note("negative control withheld Ctrl+S")
        wait_for_file_event(watch, path, IN_MOVED_TO, case.child)
    case.note(f"IN_MOVED_TO {path.name}")
    return path.read_bytes()


def typed(case, action, title, row):
    """A settled frame after an edit: the input prefix is disposed and the row drawn.

    The adapter answers only once no input is queued and the buffer is not opening,
    so Ctrl+S sent after this reply cannot overtake the edit (A9)."""
    frame = case.frame(action)
    case.expected = (title, 1)
    case.observed = (active_title(frame), document_rows(frame).count(row))
    if case.observed != case.expected:
        raise Mismatch(f"{action}: the edited row is not drawn once in {title}")
    return frame


def unrelated_files(root, edited):
    files = {}
    for path in sorted(root.rglob("*")):
        name = path.relative_to(root).as_posix()
        info = path.lstat()
        if name not in edited and not stat.S_ISDIR(info.st_mode):
            data = path.read_bytes() if stat.S_ISREG(info.st_mode) else None
            files[name] = [data, info.st_ino, info.st_mode, info.st_mtime_ns]
    return files


def r3(case, sandbox):
    root = sandbox.fixtures
    search_tree(root)
    a, b = "a.txt", "nested/b.c"
    others = unrelated_files(root, {a, b})
    results = search_results(root).decode("ascii")
    expected_b = b"cat\nsbcatter\ncat!\n"
    expected_a = b"a" + SEARCH_FILES[a]
    strip = ["a.txt", RESULTS_TITLE, "b.c"]
    with ExitStack() as stack:
        _, driver = stack.enter_context(case.server(sandbox, "R3"))
        case.launch(stack, sandbox.start, [root / a], driver)
        case.bind(sandbox)
        case.step("search-start", "Ctrl+Shift+F, cat, Tab, private fixture root, Return")
        driver.chord("Control_L", "Shift_L", "f")
        for key in "cat":
            driver.key(key)
        driver.key("Tab")
        for byte in os.fsencode(root):
            driver.key(byte)
        driver.key("Return")
        # The negative expects a completed search with one match fewer.
        search_frame(case, results, results.replace("9 matches", "8 matches"))

        case.step("open-b-match", "Go to Line 8, Return on the scatter result")
        driver.chord("Control_L", "g")
        driver.key("8")  # One-based form of the reference's zero-based scatter row 7.
        driver.key("Return")
        driver.key("Return")
        frame = case.frame("open b from the match")
        case.check("results-between-a-and-b", tab_strip(frame), (strip, 2),
                   (["a.txt", "b.c", RESULTS_TITLE], 1))

        case.step("save-b-bytes", "b drawn before Ctrl+S (A9)", part="edit-b")
        driver.key("b")
        typed(case, "type b", "b.c", "sbcatter")
        saved_b = save(case, "save-b-bytes", root / b, SEARCH_FILES[b])
        case.check("save-b-bytes", saved_b, expected_b, expected_a)
        after_b = unrelated_files(root, {a, b})

        case.step("save-a-bytes", "Alt+1, Ctrl+Home, type a; a drawn before Ctrl+S", part="edit-a")
        driver.chord("Alt_L", "1")
        driver.chord("Control_L", "Home")
        driver.key("a")
        typed(case, "type a", "a.txt", "aAlpha cat")
        saved_a = save(case, "save-a-bytes", root / a, SEARCH_FILES[a])
        case.check("save-a-bytes", (saved_a, (root / b).read_bytes()),
                   (expected_a, expected_b), (expected_b, expected_b))

        case.step("show-results", "Alt+2 selects the surviving results tab")
        driver.chord("Alt_L", "2")
        frame = case.frame("results after both saves")
        d = frame.document
        model = hard_wrapped(results, d.width, d.height)
        wrong = hard_wrapped(results.replace("9 matches", "8 matches"), d.width, d.height)
        case.check("results-text-still-visible", (*tab_strip(frame), document_rows(frame)),
                   (strip, 1, model), (strip, 1, wrong))
        case.step("quit", "Ctrl+Q reaped")
        status = case.quit()
    case.check("other-files-unchanged", (after_b, unrelated_files(root, {a, b})),
               (others, others), ({}, {}))
    # R3 lists no exit assertion; its count is the clean exit's report, so it scores the exit.
    case.step("law2-count", 0, part="quit-exit")
    case.observed = status
    if status != 0:
        raise Mismatch(f"editor exited {status} after Ctrl+Q")
    count = case.allocation_count("report")
    # v1 measured 0 for this window (docs/decisions/v2-he7.13.md), so the count rule asserts 0.
    case.check("law2-count", [count], [0], [1])


def tab_view(frame, rows):
    """The active tab title and its first `rows` document rows."""
    return active_title(frame), document_rows(frame)[:rows]


def crossing(frame, title):
    """Red control for T3: the drawn rows through the first row holding an unresolved
    cell, with that cell expected as v1's '?' replacement glyph. Every other cell
    equals the frame, so the FAIL shows a required match across an unresolved cell fails."""
    rows = document_rows(frame)
    first = next((i for i, row in enumerate(rows) if UNRESOLVED in row), None)
    if first is None:
        raise Refused("the binary frame has no unresolved cell to cross")
    return title, [row.replace(UNRESOLVED, "?") for row in rows[:first + 1]]


def r5(case, sandbox):
    path = sandbox.fixtures / "fixture.pdf"
    sibling = sandbox.fixtures / "sibling.txt"
    expected = binary_bytes()
    path.write_bytes(expected)
    sibling.write_bytes(SIBLING)
    header = (path.name, [expected[:expected.index(b"\n")].decode("ascii")])
    on_disk, counts, revisits = [], [], []
    for phase, start, paths in (("open", sandbox.start, [path, sibling]),
                                ("reopen", sandbox.restart, [path])):
        with ExitStack() as stack:
            _, driver = stack.enter_context(case.server(sandbox, f"R5-{phase}"))
            case.step(f"{phase}-alive", "file opened, mapped owned window, live editor")
            with directory_watch(path.parent, IN_ACCESS) as watch:
                child = case.launch(stack, start, paths, driver)
                wait_for_file_event(watch, path, IN_ACCESS, child)
            case.note(f"IN_ACCESS {path.name}, PID {child.pid}")
            case.bind(sandbox)
            case.check(f"{phase}-alive", child.poll(), None, 0)
            if phase == "open":
                case.step("activate-binary", "Alt+1 activates the binary tab")
                driver.chord("Alt_L", "1")
                frame = case.frame("activate binary tab")
                case.check("header-painted", tab_view(frame, 1), header,
                           (path.name, ["%PDF-1.4"]))

                case.step("adjacent-text-and-sibling-save", "Alt+2, type s; s drawn before Ctrl+S",
                          part="edit-sibling")
                driver.chord("Alt_L", "2")
                driver.key("s")
                frame = typed(case, "type into sibling", sibling.name, "s" + SIBLING.decode().rstrip())
                drawn = tab_view(frame, 1)
                saved = save(case, "adjacent-text-and-sibling-save", sibling, SIBLING)
                case.check("adjacent-text-and-sibling-save", (*drawn, saved),
                           (sibling.name, ["s" + SIBLING.decode().rstrip()], b"s" + SIBLING),
                           (sibling.name, ["s" + SIBLING.decode().rstrip()], SIBLING))

                case.step("revisit-binary", "Alt+1 returns to the binary tab")
                driver.chord("Alt_L", "1")
                revisits.append(case.frame("revisit binary tab"))
            else:
                # Alt+1 gives the reopened editor an input-to-submit window to count.
                case.step("reopened-binary", "Alt+1 in the reopened editor shows the binary tab")
                driver.chord("Alt_L", "1")
                revisits.append(case.frame("reopened binary tab"))
                wrong = None
                if case.negative == "R5.settled-binary-tab-revisit":
                    wrong = [crossing(revisits[0], path.name), header]
                case.check("settled-binary-tab-revisit",
                           lambda want: [tab_view(frame, len(rows))
                                         for frame, (_, rows) in zip(revisits, want)],
                           [header, header], wrong)
            case.step(f"{phase}-exit", "Ctrl+Q exits 0")
            status = case.quit()
            case.check(f"{phase}-exit", status, 0, 1)
            on_disk.append(path.read_bytes())
            counts.append(case.allocation_count(f"{phase}-report"))
    case.check("bytes-unchanged", on_disk, [expected, expected], [expected, b"!" + expected[1:]])
    # v1 measured 0 for both windows (docs/decisions/v2-he7.13.md); the count rule asserts 0.
    case.check("law2-count", counts, [0, 0], [0, 1])


def run_case(case, scenario, target):
    try:
        with Sandbox(case.artifacts, target, display=case.display) as sandbox:
            try:
                scenario(case, sandbox)
                case.step("assertion-inventory", ASSERTIONS[case.id])
                case.observed = case.asserted
                if tuple(case.asserted) != ASSERTIONS[case.id]:
                    raise Refused("assertion inventory incomplete")
                case.step("teardown", "driver shutdown and fixture retention")
            except TeardownError as exc:
                case.teardown(exc)
            except (OSError, RuntimeError, ValueError, Mismatch, subprocess.SubprocessError) as exc:
                case.failure(exc)
            finally:
                shutil.copytree(sandbox.fixtures, case.artifacts / "fixtures", symlinks=True)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        case.failure(exc)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path,
                        help="absolute inspected v1 build/contract-oracle directory")
    parser.add_argument("--display", required=True)
    parser.add_argument("--negative", choices=NEGATIVES)
    records.digests_argument(parser)
    args = parser.parse_args()
    if not CONTRACT_DISPLAY.fullmatch(args.display):
        parser.error("--display must be a contract display, :120 through :199")
    if not args.build_dir.is_absolute():
        parser.error("--build-dir must be absolute")

    BUILD.mkdir(exist_ok=True)
    artifacts = Path(tempfile.mkdtemp(prefix="contract-files-", dir=BUILD))
    scenarios = {"R3": r3, "R5": r5}
    if args.negative:
        selected = args.negative.split(".", 1)[0]
        scenarios = {selected: scenarios[selected]}
    try:
        battery = Path("/sys/class/power_supply/BAT0/status").read_text(encoding="ascii").strip()
    except OSError:
        battery = None
    manifest = {
        "run_id": artifacts.name, "display": args.display, "geometry": GEOMETRY,
        "capacity": CAPACITY, "cases": list(scenarios), "battery_status": battery,
        "load_average": os.getloadavg(), "negative": args.negative,
        "oracle_build": str(args.build_dir),
    }
    print(f"R3/R5 run artifacts in {artifacts}", file=sys.stderr)
    cases = [Case(case_id, artifacts, args.negative, args.display) for case_id in scenarios]
    try:
        target = RunTarget("v1", args.build_dir / EDITOR, args.build_dir,
                           args.digests[str(args.build_dir / EDITOR)])
        manifest.update(role=target.role, oracle=str(target.executable), digests=args.digests)
        # Evaluate both independent cases once even after an earlier failure.
        for case in cases:
            run_case(case, scenarios[case.id], target)
    except KeyError as exc:
        for case in cases:
            case.step("binding", "the runner's digest for the inspected oracle")
            case.failure(exc)
    finally:
        (artifacts / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                                 encoding="utf-8")
    return records.exit_status(case.status for case in cases)


if __name__ == "__main__":
    sys.exit(main())
