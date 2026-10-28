#!/usr/bin/env python3
"""Run sanitizer suites with private, locked Xvfbs and bounded concurrency."""

import argparse
from collections import deque
from dataclasses import dataclass
import fcntl
import itertools
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


# These consecutive slices concatenate to editor_test's original main body.
PARTS = {"editor_test": ("--part-1", "--part-2", "--part-3", "--part-4", "--part-5")}
LOGS = Path("build/check-logs")
TIMES = Path("build/check-times.txt")
# (E) Same startup grace as tools/design_shots.sh: 100 polls x 0.1 s.
POLL_SECONDS = 0.1
STARTUP_SECONDS = 100 * POLL_SECONDS
requested_signal = None


@dataclass(frozen=True)
class Task:
    binary: str
    selector: str = ""
    display: bool = False

    @property
    def name(self):
        return Path(self.binary).name + self.selector.replace("--", ".", 1)

    @property
    def log(self):
        return LOGS / (self.name + ".log")


class Interrupted(Exception):
    def __init__(self, signum):
        self.signum = signum


def interrupt(signum, _frame):
    # Defer interruption to scheduler boundaries so Popen always gets recorded
    # before cleanup, even if the signal arrives during process creation.
    global requested_signal
    requested_signal = signum


def raise_if_interrupted():
    if requested_signal is not None:
        raise Interrupted(requested_signal)


def positive_int(value):
    number = int(value)
    if number < 1:
        raise ValueError("must be a positive integer")
    return number


def read_times():
    durations = {}
    if TIMES.exists():
        for line in TIMES.read_text().splitlines():
            fields = line.split()
            try:
                name, seconds = fields
                seconds = float(seconds)
                if math.isfinite(seconds) and seconds >= 0:
                    durations[name] = seconds
            except ValueError:
                continue
    return durations


def save_times(durations):
    temporary = TIMES.with_suffix(".tmp")
    temporary.write_text("".join(f"{name} {seconds:.6f}\n"
                                 for name, seconds in sorted(durations.items())))
    temporary.replace(TIMES)


def tail(path):
    # (E) Diagnostic bound: the last terminal screen (24 lines), at most
    # 64 KiB so a failing binary cannot flood the check driver's output.
    with path.open("rb") as output:
        output.seek(0, os.SEEK_END)
        output.seek(max(0, output.tell() - 64 * 1024))
        return "\n".join(output.read().decode(errors="replace").splitlines()[-24:])


def signal_group(process, signum):
    try:
        os.killpg(process.pid, signum)
    except ProcessLookupError:
        pass


class Runner:
    def __init__(self, jobs, display_count):
        self.jobs = jobs
        self.display_count = display_count
        self.servers = []
        self.locks = []
        self.available = deque()
        self.active = {}
        self.environment = dict(os.environ, EDIT_REFUSE_REAL_DISPLAY="1")
        # Match SAFE_DISPLAY for direct invocations as well as make callers.
        display = self.environment.get("DISPLAY", "")
        if not display or display.split(".")[0] in (":0", "unix:0", "localhost:0"):
            display = self.environment.get("EDIT_DISPLAY", ":99")
        self.environment["DISPLAY"] = display
        self.environment.pop("EDIT_DISPLAY_LOCK_FD", None)

    def start_displays(self):
        # (P) Start above :99, the shared test display. The socket, X lock,
        # and the common flock together exclude existing servers and runners.
        for number in itertools.count(100):
            raise_if_interrupted()
            if len(self.available) == self.display_count:
                return
            socket = Path(f"/tmp/.X11-unix/X{number}")
            xlock = Path(f"/tmp/.X{number}-lock")
            if socket.exists() or xlock.exists():
                continue
            fd = os.open(f"/tmp/edit-xvfb-{number}.lock", os.O_CREAT | os.O_RDWR, 0o600)
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                os.close(fd)
                continue
            if socket.exists() or xlock.exists():
                os.close(fd)
                continue
            self.locks.append(fd)
            display = f":{number}"
            log = LOGS / f"Xvfb-{number}.log"
            with log.open("wb") as output:
                process = subprocess.Popen(
                    ["Xvfb", display, "-screen", "0", "2880x1800x24", "-dpi", "100",
                     "-nolisten", "tcp", "-noreset"],
                    stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
            self.servers.append((process, log))
            deadline = time.monotonic() + STARTUP_SECONDS
            while not socket.exists():
                raise_if_interrupted()
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError(f"Xvfb {display} did not become ready:\n{tail(log)}")
                time.sleep(POLL_SECONDS)
            if process.poll() is not None:
                raise RuntimeError(f"Xvfb {display} exited:\n{tail(log)}")
            self.available.append((display, fd))

    def launch(self, task):
        lease = self.available.popleft() if task.display else None
        env = self.environment.copy()
        command = [task.binary] + ([task.selector] if task.selector else [])
        descriptors = ()
        if lease:
            env["DISPLAY"], fd = lease
            # Keep the reservation across exec, using the wrapper's existing
            # inherited-flock contract. Only this suite can use this lease.
            env["EDIT_DISPLAY_LOCK_FD"] = str(fd)
            descriptors = (fd,)
            command = ["sh", "tools/with_display_lock.sh"] + command
        started = time.monotonic()
        with task.log.open("wb") as output:
            try:
                process = subprocess.Popen(command, env=env, stdout=output,
                                           stderr=subprocess.STDOUT, pass_fds=descriptors,
                                           start_new_session=True)
            except OSError as error:
                output.write((str(error) + "\n").encode())
                if lease:
                    self.available.append(lease)
                return False
        self.active[process] = (task, started, lease)
        return True

    def run(self, tasks, durations):
        # Unknown suites precede every known suite, then longest first.
        pending = sorted(tasks, key=lambda t: (t.name in durations,
                                               -durations.get(t.name, 0)))
        failed = set()

        def failure(task, rc):
            failed.add(task.binary)
            print(f"FAIL {task.name}: exit {rc} ({task.log})\n{tail(task.log)}", flush=True)

        while pending or self.active:
            raise_if_interrupted()
            for server, log in self.servers:
                if server.poll() is not None:
                    raise RuntimeError(f"private Xvfb exited ({server.returncode}):\n{tail(log)}")
            while len(self.active) < self.jobs:
                raise_if_interrupted()
                # Acquire a display BEFORE occupying a job slot. A queued
                # display suite never blocks eligible non-display work.
                eligible = next((i for i, t in enumerate(pending)
                                 if not t.display or self.available), None)
                if eligible is None:
                    break
                task = pending.pop(eligible)
                if not self.launch(task):
                    failure(task, 127)  # POSIX command could not be executed.
            for process, (task, started, lease) in list(self.active.items()):
                rc = process.poll()
                if rc is None:
                    continue
                durations[task.name] = time.monotonic() - started
                # Reap any helper left in the suite's process group before
                # returning the display to the pool.
                signal_group(process, signal.SIGKILL)
                del self.active[process]
                if lease:
                    self.available.append(lease)
                if rc:
                    failure(task, rc)
            if self.active:
                time.sleep(POLL_SECONDS)
        return failed

    def close(self):
        processes = list(self.active) + [p for p, _ in self.servers]
        # Tear down suite process groups before Xvfb; retain all display locks
        # until every child is gone. This also covers startup and Ctrl-C.
        for process in processes:
            signal_group(process, signal.SIGTERM)
        # (E) Reuse the Xvfb startup grace for orderly child shutdown.
        deadline = time.monotonic() + STARTUP_SECONDS
        for process in processes:
            try:
                process.wait(timeout=max(0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                pass
        for process in processes:
            signal_group(process, signal.SIGKILL)
            process.wait()
        for fd in self.locks:
            os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--display", nargs="*", default=[], metavar="SUITE")
    parser.add_argument("suites", nargs="+")
    args = parser.parse_args()
    try:
        jobs = positive_int(os.environ["CHECK_JOBS"] if "CHECK_JOBS" in os.environ
                            else subprocess.check_output(["nproc"], text=True).strip())
        # (E) Bead input: 488 s display work / 6 displays = 81 s packed;
        # splitting editor and shortening the inert-guard loop reduces it.
        displays = positive_int(os.environ.get("CHECK_DISPLAYS", "6"))
    except ValueError as error:
        parser.error(f"CHECK_JOBS and CHECK_DISPLAYS: {error}")
    if len(set(args.suites)) != len(args.suites):
        parser.error("duplicate suite paths")
    if not set(args.display) <= set(args.suites):
        parser.error("--display must contain only suites from the full list")
    tasks = [Task(binary, selector, binary in args.display)
             for binary in args.suites
             for selector in PARTS.get(Path(binary).name, ("",))]
    LOGS.mkdir(parents=True, exist_ok=True)
    durations = read_times()
    runner = Runner(jobs, min(displays, jobs, sum(t.display for t in tasks)))
    started = time.monotonic()
    handlers = {sig: signal.signal(sig, interrupt)
                for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)}
    try:
        runner.start_displays()
        print(f"check: running {len(args.suites)} test binaries "
              f"({len(tasks)} tasks, {jobs} jobs, {runner.display_count} displays)", flush=True)
        failed = runner.run(tasks, durations)
        raise_if_interrupted()
        # Preserve a per-binary log as well as the individual part logs.
        for binary in args.suites:
            if Path(binary).name in PARTS:
                with (LOGS / (Path(binary).name + ".log")).open("wb") as output:
                    for task in tasks:
                        if task.binary == binary:
                            output.write(f"== {task.name}\n".encode())
                            output.write(task.log.read_bytes())
        save_times(durations)
    finally:
        for sig in handlers:
            signal.signal(sig, signal.SIG_IGN)
        runner.close()
        for sig, handler in handlers.items():
            signal.signal(sig, handler)
    elapsed = time.monotonic() - started
    if failed:
        print(f"check: {len(failed)} of {len(args.suites)} test binaries failed in {elapsed:.1f} s")
        return 1
    print(f"check: {len(args.suites)} test binaries passed in {elapsed:.1f} s "
          f"(parallel, {jobs} jobs, {runner.display_count} displays)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Interrupted as error:
        print(f"check: interrupted by {signal.Signals(error.signum).name}", file=sys.stderr)
        sys.exit(128 + error.signum)
    except (OSError, RuntimeError) as error:
        print(f"check: {error}", file=sys.stderr)
        sys.exit(1)
