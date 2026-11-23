"""Run R1 and R6 once against the pinned v1 snapshot oracle.

Each case runs on its own private X server (tests/contract/xserver.py), so no case observes
another's server history. Records and exit statuses (PASS 0, FAIL 3, ERROR 4) follow
tests/contract/records.py.
"""

import argparse
from contextlib import ExitStack, contextmanager
import hashlib
import json
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_oracle import EDITOR
from cases.files import GEOMETRY, IN_MOVED_TO, SCREEN, directory_watch, wait_for_file_event
import records
from navigation_fixtures import (
    LOG_BYTES, LOG_SEED, RECORD_BYTES, SEARCH_TOKEN, SHARD_BYTES, WINDOW_BYTES, WRAP_LINES,
    Row, log_fixture, read_log_rows, search_shards, visual_rows, wrapped_fixture,
)
from sandbox import CONTRACT_DISPLAY, Refused, RunTarget, Sandbox
import snapwire as wire
from test_oracle_inspector import Inspector, capacity, law2_count
from test_xdriver import WATCHDOG
from xserver import TeardownError, fresh_display, stop


ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "build"
WAIT_MS = WATCHDOG * 1000
IN_CLOSE_NOWRITE = 0x00000010  # Linux inotify: the search worker finished reading a shard.
ASSERTIONS = {
    "R1": ("bottom-setup", "wrapped-return", "return-caret", "saved-bytes", "law2-count"),
    "R6": ("first-viewport", "tail-navigation", "marker", "occupied-navigation", "law2-count"),
}
# The count's predicate is the manifest's, per target (expectations.json "count"): no red here.
NEGATIVES = ("R1.snapshot-reply", *(f"{case}.{name}" for case, names in ASSERTIONS.items()
                                    for name in names if name != "law2-count"))


class Mismatch(Exception):
    """The snapshot or saved bytes contradicted the independent model."""


def document_text(frame):
    d = frame.document
    return [frame.cell_text(y, d.x, d.x + d.width).rstrip(" ")
            for y in range(d.y, d.y + d.height)]


def active_title(frame):
    tab = frame.active_tab
    if tab is None:
        return ""
    rect = tab.rect
    # v1's last two tab cells are reserved for the modified dot and spacing.
    return frame.cell_text(rect.y, rect.x, rect.x + rect.width - 2).strip()


def describe(value):
    if isinstance(value, bytes):
        return {"bytes": len(value), "sha256": hashlib.sha256(value).hexdigest()}
    raise TypeError(f"cannot encode {type(value).__name__}")


class Case:
    def __init__(self, case_id, root, negative):
        self.id = case_id
        self.root = root
        self.path = root / case_id
        self.path.mkdir(mode=0o700)
        self.negative = negative
        self.child = None
        self.law2 = None
        self.ticket = self.serial = None
        self.transcript = self.path / "snapshots.jsonl"
        self.results = []
        self.stage("setup")

    def stage(self, assertion):
        self.assertion = assertion
        self.predicate = assertion
        self.observed = self.expected = self.ordinary = None

    def note(self, **fields):
        with self.transcript.open("a", encoding="utf-8") as log:
            print(json.dumps(fields), file=log)

    def compare(self, predicate, observed, expected, *, red=None):
        """Compare once. Under this assertion's red control the observation must first meet
        the ordinary expectation (kept as `ordinary`); only then is `red` expected."""
        self.predicate, self.observed, self.expected = predicate, observed, expected
        if red is not None and self.negative == f"{self.id}.{self.assertion}":
            if observed != expected:
                raise Refused(f"{predicate}: the control's observation fails the ordinary "
                              "expectation, so its red would not be the deliberate one")
            self.ordinary, self.expected = {"expected": expected, "observed": observed}, red
        if observed != self.expected:
            raise Mismatch(predicate)

    def record(self, status, reason=None):
        self.results.append({
            "run_id": self.root.name, "case_id": self.id, "assertion_id": self.assertion,
            "evidence": "A" if self.assertion == "saved-bytes" else "B",
            "first_required_wave": 2, "status": status, "predicate": self.predicate,
            "observed": self.observed, "expected": self.expected, "ordinary": self.ordinary,
            "reason": reason,
            "seed": LOG_SEED if self.id == "R6" else None, "negative": self.negative,
            "ticket": self.ticket, "submit_serial": self.serial, "artifacts": str(self.path),
        })

    def publish(self):
        with (self.root / "results.jsonl").open("a", encoding="utf-8") as log:
            for record in self.results:
                record["child_status"] = ({"pid": self.child.pid, "exit": self.child.poll()}
                                          if self.child else None)
                record["law2"] = self.law2
                print(json.dumps(record, default=describe), file=log)
                records.emit(record, default=describe)
                if record["status"] != "PASS":
                    print(f"{self.id}.{record['assertion_id']}: {record['status']} {record['reason']}",
                          file=sys.stderr)

    def law2_count(self):
        """The passive Law-2 report of the editor's clean exit, under its own ID. The case
        requires a healthy instrument; the count's predicate per target is the manifest's
        (R1/R6 "count": recorded on v1-pin, zero on v2), checked by the runner."""
        with self.assertion_group("law2-count"):
            self.predicate = "one active guard report over at least one measured window"
            self.law2 = law2_count((self.path / "editor.log").read_bytes())._asdict()
            self.note(law2=self.law2)

    @contextmanager
    def assertion_group(self, assertion):
        self.stage(assertion)
        yield
        self.record("PASS")

    def frame(self, action, *, next_frame=False, deadline=None):
        self.predicate = f"{action} snapshot"
        self.observed = None
        self.expected = "one settled snapshot"
        self.inspector.deadline = deadline or (time.monotonic() + WATCHDOG)
        previous = self.inspector.serial if next_frame else 0
        operation = wire.Operation.NEXT if next_frame else wire.Operation.SNAP
        self.ticket = self.inspector.ticket + 1
        self.serial = None
        self.note(action=action, request=operation.name, ticket=self.ticket, after_serial=previous)
        if self.negative == "R1.snapshot-reply":
            self.inspect_missing(operation, previous)
        request = self.inspector.send(self.driver, operation, previous)
        snapshot = self.inspector.receive(request)
        if len(snapshot.frames) != 1:
            raise wire.ProtocolError("the pinned oracle must provide exactly one settled frame")
        frame = snapshot.settled
        self.serial = frame.submit_serial
        doc = frame.document
        if doc.empty or frame.scroll_q8:
            raise Refused(f"expected stationary document geometry: {doc}, {frame.scroll_q8}")
        self.note(action=action, ticket=self.ticket, submit_serial=self.serial,
                  document=vars(doc), active_tab=active_title(frame), rows=document_text(frame),
                  caret=frame.drawn_caret_cells(), scroll_q8=frame.scroll_q8)
        return frame

    def inspect_missing(self, operation, previous):
        # Withhold the ClientMessage. The live editor keeps its socket endpoint
        # open, so the real receive path must time out, independently of EOF.
        self.note(control="withheld-marker", ticket=self.ticket)
        self.inspector.receive(wire.Request(operation, self.ticket, previous))
        raise wire.ProtocolError("received a reply to the withheld marker")

    def arrival(self, title):
        deadline = time.monotonic() + WATCHDOG
        frame = self.frame("first viewport", deadline=deadline)
        while active_title(frame) != title:
            frame = self.frame("document arrival", next_frame=True, deadline=deadline)
        return frame

    @contextmanager
    def launch(self, box, driver, path):
        self.driver = driver
        with ExitStack() as stack, (self.path / "editor.log").open("wb") as log:
            self.inspector = Inspector(stack, capacity(*SCREEN))
            self.child = self.inspector.attach(box.start(
                [*self.inspector.args, str(path)], pass_fds=self.inspector.fds, stdout=log, stderr=log))
            try:
                driver.find(self.child.pid, WAIT_MS)
                driver.focus()
                yield
            except BaseException:
                try:
                    stop(self.child, WATCHDOG)
                except (OSError, TeardownError) as exc:
                    self.note(cleanup_error=str(exc))
                raise
            else:
                self.stage("editor-exit")
                self.expected = 0
                self.observed = stop(self.child, WATCHDOG)
                if self.observed:
                    raise Refused(f"editor exited {self.observed}")


def caret(frame):
    d = frame.document
    return [[row - d.y, col - d.x] for row, col in frame.drawn_caret_cells()
            if d.y <= row < d.y + d.height and d.x <= col < d.x + d.width]


def r1(case, box, driver):
    path = box.fixtures / "wrap.txt"
    text = wrapped_fixture(path)
    if case.negative == "R1.snapshot-reply":
        case.stage("snapshot-reply")
    try:
        with case.launch(box, driver, path):
            case.arrival(path.name)
            line = WRAP_LINES // 2 + 1
            driver.chord("Control_L", "g")
            for digit in str(line):
                driver.key(digit)
            driver.key("Return")
            frame = case.frame("go to middle")
            d = frame.document
            rows = visual_rows(text, d.width)
            visible = document_text(frame)
            top = next((i for i, row in enumerate(rows) if row.text == visible[0]), -1)
            bottom = top + d.height - 1
            # Split three ASCII cells into the first six-cell word fragment.
            col = 3
            with case.assertion_group("bottom-setup"):
                case.compare("viewport is inside the numbered mid-file rows",
                             0 < top < bottom < len(rows) - 2, True)
                case.compare("setup viewport matches independently wrapped rows",
                             visible, [r.text for r in rows[top:bottom + 1]])
                x = (d.x + col) * frame.cell_width + frame.cell_width // 4
                y = (d.y + d.height - 1) * frame.cell_height + frame.cell_height // 2
                driver.motion(x, y)
                driver.button(1)
                frame = case.frame("click bottom document row")
                case.compare("bottom-row caret before Return", caret(frame), [[d.height - 1, col]],
                             red=[[d.height - 2, col]])
                case.compare("click preserves setup viewport", document_text(frame), visible)

            offset = rows[bottom].start + col
            edited = text[:offset] + "\n" + text[offset:]
            after_rows = visual_rows(edited, d.width)
            after_caret = next(i for i, row in enumerate(after_rows) if row.start == offset + 1)
            after_top = max(top, after_caret - d.height + 1)
            case.note(width=d.width, rows=d.height, byte_offset=offset, before_top=top,
                      after_top=after_top, after_caret=after_caret)
            with case.assertion_group("wrapped-return"):
                driver.key("Return")
                frame = case.frame("Return")
                expected = [r.text for r in after_rows[after_top:after_top + d.height]]
                wrong = [r.text for r in after_rows[after_top + 1:after_top + d.height + 1]]
                case.compare("minimal reveal and neighbouring word fragments",
                             document_text(frame), expected, red=wrong)
            with case.assertion_group("return-caret"):
                row = after_caret - after_top
                case.compare("settled caret after Return", caret(frame), [[row, 0]], red=[[row, 1]])
            with case.assertion_group("saved-bytes"):
                case.predicate = "atomic save notification"
                with directory_watch(path.parent, IN_MOVED_TO) as watch:
                    driver.chord("Control_L", "s")
                    wait_for_file_event(watch, path, IN_MOVED_TO, case.child)
                case.compare("saved buffer equals inserting one LF into the fixture",
                             path.read_bytes(), edited.encode("ascii"), red=text.encode("ascii"))
    finally:
        path.replace(case.path / path.name)


def check_target(case, frame, rows, index, col, action, *, wrong_row=None):
    d = frame.document
    cells = caret(frame)
    case.compare(f"{action} drew one document caret", len(cells), 1)
    row, actual_col = cells[0]
    top = index - row
    case.compare(f"{action} viewport starts in the oracle range", top >= 0, True)
    expected = [r.text.rstrip(" ") for r in rows[top:top + d.height]]
    expected += [""] * (d.height - len(expected))
    case.compare(f"{action} viewport contains a numbered fixture row", any(expected), True)
    wrong = expected[:row] + [wrong_row] + expected[row + 1:] if wrong_row is not None else None
    case.note(action=action, target_byte=rows[index].start + col, target_row=rows[index].text,
              target_column=col, first_byte=rows[top].start)
    case.compare(f"{action} viewport and target label", document_text(frame), expected, red=wrong)
    case.compare(f"{action} caret column", actual_col, col)


def occupied(case, box, driver, path, tail, page):
    # Restore the original source offsets before the search companion.
    driver.chord("Control_L", "z")
    directory = box.fixtures / "corpus"
    last_shard = search_shards(path, directory)
    with directory_watch(last_shard, IN_CLOSE_NOWRITE) as watch, \
         selectors.DefaultSelector() as progress:
        progress.register(watch, selectors.EVENT_READ)
        driver.chord("Control_L", "Shift_L", "f")
        for key in SEARCH_TOKEN:
            driver.key(key)
        driver.chord("Alt_L", "c")  # Case-sensitive literal. All fixture text is ASCII.
        driver.key("Tab")
        # The sandbox workdir and corpus share a parent. Supply the explicit
        # absolute corpus path via the Where field, as in the files case.
        driver.chord("Control_L", "a")
        for byte in os.fsencode(directory):
            driver.key(byte)
        driver.key("Return")
        driver.chord("Alt_L", "1")
        driver.chord("Control_L", "End")
        frame = case.frame("search companion Ctrl+End")
        eof_before_last_read = not progress.select(0)
        check_target(case, frame, tail, len(tail) - 1, 0, "search companion Ctrl+End")
        driver.key("Page_Up")
        frame = case.frame("search companion PageUp")
        page_before_last_read = not progress.select(0)
        check_target(case, frame, tail, len(tail) - 1 - page, 0, "search companion PageUp")
        case.note(search_submitted=True, ctrl_end_before_final_shard_read=eof_before_last_read,
                  page_up_before_final_shard_read=page_before_last_read)
        driver.chord("Alt_L", "2")
        deadline = time.monotonic() + WATCHDOG
        frame = case.frame("find results", deadline=deadline)
        case.compare("Find Results became active", active_title(frame), "Find Results")
        hits = ["L00000000", tail[-2].text.split()[0]]
        while True:
            texts = document_text(frame)
            footer = next((line for line in texts if " match" in line and
                           (" file" in line or " across " in line)), None)
            error = next((line for line in texts if line.startswith(("Search failed", "Search truncated",
                                                                     "Search cancelled"))), None)
            if footer or error:
                matches = [label for line in texts if SEARCH_TOKEN in line
                           for label in hits if label in line]
                case.compare("full-corpus search completed with both endpoint hits",
                             [footer, error, matches], ["2 matches across 2 files", None, hits],
                             red=["1 match in 1 file", None, hits[:1]])
                break
            frame = case.frame("find results progress", next_frame=True, deadline=deadline)


def r6(case, box, driver):
    path = box.fixtures / "tail.log"
    # A navigation corpus and its search shards are the maximum live output.
    if shutil.disk_usage(case.path).free < 2 * LOG_BYTES:
        raise Refused("insufficient disk for the navigation corpus and its search shards")
    log_fixture(path)
    tail = read_log_rows(path, LOG_BYTES - 2 * WINDOW_BYTES, 2 * WINDOW_BYTES)
    head = read_log_rows(path, 0, WINDOW_BYTES)
    window_start = LOG_BYTES - WINDOW_BYTES
    window_lines = sum(row.start >= window_start for row in tail)
    case.note(seed=LOG_SEED, size=path.stat().st_size, scan_budget=WINDOW_BYTES,
              final_window=window_start, tail=[vars(row) for row in tail],
              shard_size=SHARD_BYTES)
    try:
        with case.launch(box, driver, path):
            with case.assertion_group("first-viewport"):
                first = case.arrival(path.name)
                page = first.document.height
                if first.document.width <= RECORD_BYTES or not page:
                    raise Refused(f"fixture requires fixed-width log records to fit: {first.document}")
                check_target(case, first, head, 0, 0, "first viewport",
                             wrong_row=head[1].text.rstrip(" "))
            pages = window_lines // page + 1
            eof = len(tail) - 1
            destination = eof - pages * page
            if destination < page or tail[destination].start >= window_start:
                raise Refused("the computed traversal does not cross into the preceding window")
            case.note(page_rows=page, window_lines=window_lines, pages_up=pages,
                      crossed_to=tail[destination].start, eof_byte=tail[eof].start)
            with case.assertion_group("tail-navigation"):
                driver.chord("Control_L", "End")
                check_target(case, case.frame("Ctrl+End"), tail, eof, 0, "Ctrl+End",
                             wrong_row=tail[eof - 1].text.rstrip(" "))
                index = eof
                for step in range(1, pages + 1):
                    index -= page
                    action = f"PageUp {step}/{pages}"
                    driver.key("Page_Up")
                    check_target(case, case.frame(action), tail, index, 0, action)
                driver.key("Page_Down")
                index += page
                check_target(case, case.frame("PageDown"), tail, index, 0, "PageDown")
                driver.key("Right")
                check_target(case, case.frame("Right before Home"), tail, index, 1, "Right before Home")
                driver.key("Home")
                check_target(case, case.frame("Home"), tail, index, 0, "Home")
                end = len(tail[index].text)
                driver.key("End")
                check_target(case, case.frame("End"), tail, index, end, "End")
            with case.assertion_group("marker"):
                driver.key("z")
                frame = case.frame("type marker")
                edited = tail.copy()
                edited[index] = Row(tail[index].start, tail[index].text + "z")
                check_target(case, frame, edited, index, end + 1, "type marker",
                             wrong_row=tail[index].text.rstrip(" "))
            with case.assertion_group("occupied-navigation"):
                occupied(case, box, driver, path, tail, page)
    except BaseException:
        # A green input regenerates from LOG_SEED; keep it only as failure evidence.
        path.replace(case.path / path.name)
        raise


def run_case(case, scenario, target, display):
    """One case on a server no earlier case used; a teardown ERROR is recorded beside any
    FAIL or ERROR the case already recorded."""
    try:
        with Sandbox(case.path, target, display=display) as box, fresh_display(
                display, case.path, "Xvfb", box.env, timeout=2 * WATCHDOG,
                geometry=GEOMETRY) as (_, driver):
            try:
                scenario(case, box, driver)
                case.law2_count()
                case.stage("assertion-inventory")
                passed = tuple(result["assertion_id"] for result in case.results)
                if passed != ASSERTIONS[case.id]:
                    raise Refused(f"expected {ASSERTIONS[case.id]}, got {passed}")
            except Mismatch as exc:
                case.record("FAIL", str(exc))
            except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
                case.record("ERROR", f"{type(exc).__name__}: {exc}")
    except TeardownError as exc:
        case.stage("teardown")
        case.record("ERROR", f"TeardownError: {exc}")
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        if all(result["status"] == "PASS" for result in case.results):
            case.stage("sandbox")
            case.record("ERROR", f"{type(exc).__name__}: {exc}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--display", required=True)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--case", choices=ASSERTIONS)
    group.add_argument("--negative", choices=NEGATIVES)
    records.digests_argument(parser)
    args = parser.parse_args()
    if not CONTRACT_DISPLAY.fullmatch(args.display):
        parser.error("--display must be a contract display, :120 through :199")
    if not args.build_dir.is_absolute():
        parser.error("--build-dir must be absolute")

    root = Path(tempfile.mkdtemp(prefix="contract-navigation-", dir=BUILD))
    scenarios = {"R1": r1, "R6": r6}
    selected = args.case or (args.negative.split(".")[0] if args.negative else None)
    cases = [Case(case_id, root, args.negative) for case_id in ([selected] if selected else scenarios)]
    try:
        battery = Path("/sys/class/power_supply/BAT0/status").read_text(encoding="ascii").strip()
    except OSError:
        battery = "unavailable"
    run = {"display": args.display, "geometry": GEOMETRY, "battery_status": battery,
           "load_average": os.getloadavg(), "negative": args.negative, "cases": [c.id for c in cases],
           "oracle_build": str(args.build_dir)}
    print(f"R1/R6 run artifacts in {root}", file=sys.stderr)
    try:
        target = RunTarget("v1", args.build_dir / EDITOR, args.build_dir,
                           args.digests[str(args.build_dir / EDITOR)])
        run.update(digests=args.digests)
        for case in cases:
            run_case(case, scenarios[case.id], target, args.display)
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        for case in cases:
            if all(result["status"] == "PASS" for result in case.results):
                case.stage("run")
                case.record("ERROR", f"{type(exc).__name__}: {exc}")
    finally:
        (root / "manifest.json").write_text(json.dumps(run, indent=2) + "\n", encoding="utf-8")
    for case in cases:
        case.publish()
    return records.exit_status(result["status"] for case in cases for result in case.results)


if __name__ == "__main__":
    sys.exit(main())
