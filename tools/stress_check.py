#!/usr/bin/env python3
"""Run the edit-edgt suites N times beside one CPU burner per available CPU.

Each round runs editor parts 4/5 and editor_scroll_test concurrently, on
separate private displays. There are no retries: counts are raw suite exits.
Build the sanitizer binaries first with make build/san/tests/editor_test
build/san/tests/editor_scroll_test. Logs and power/time stamps are retained.
Use --focused for the four affected cases, or --focused chrome for one case.
"""

import argparse
from datetime import datetime
from pathlib import Path
import signal
import subprocess
import sys
import time

import check_parallel as check


def power():
    path = Path('/sys/class/power_supply/BAT0/status')
    return path.read_text().strip() if path.exists() else 'no battery'


class StressRunner(check.Runner):
    def launch(self, task):
        launched = super().launch(task)
        self.processes[task.name] = next((process for process, entry in self.active.items()
                                          if entry[0] == task), None)
        return launched


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rounds', type=check.positive_int)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--focused', nargs='*', choices=('blink', 'chrome', 'replay', 'scroll'),
                        help='run selected affected cases (default: all four)')
    args = parser.parse_args()
    output = args.output or Path('build/edit-edgt-evidence') / datetime.now().strftime('stress-%Y%m%d-%H%M%S')
    output.mkdir(parents=True, exist_ok=False)
    cpus = int(subprocess.check_output(['nproc'], text=True))
    tasks = [check.Task('build/san/tests/editor_test', '--part-4', True),
             check.Task('build/san/tests/editor_test', '--part-5', True),
             check.Task('build/san/tests/editor_scroll_test', display=True)]
    if args.focused is not None:
        cases = args.focused or ('blink', 'chrome', 'replay', 'scroll')
        tasks = [check.Task('build/san/tests/editor_scroll_test', display=True) if case == 'scroll'
                 else check.Task('build/san/tests/editor_test', '--edgt-' + case, True)
                 for case in dict.fromkeys(cases)]
    counts = {task.name: [0, 0] for task in tasks}
    check.LOGS = output
    runner = StressRunner(len(tasks), len(tasks))
    burners = []
    handlers = {sig: signal.signal(sig, check.interrupt)
                for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)}
    stamp = power()
    started = time.monotonic()
    print(f'stress: rounds={args.rounds} burners={cpus} power={stamp} date={datetime.now().astimezone().isoformat()}', flush=True)
    try:
        # (G) User's requested load: nproc independent busy loops, one per CPU.
        for _ in range(cpus):
            burners.append(subprocess.Popen([sys.executable, '-c', 'while True: pass'],
                                           start_new_session=True))
        runner.start_displays()
        for number in range(1, args.rounds + 1):
            check.LOGS = output / f'round-{number}'
            check.LOGS.mkdir()
            stamp = power()
            tick = time.monotonic()
            durations = {}
            runner.processes = {}
            runner.run(tasks, durations)
            for task in tasks:
                process = runner.processes[task.name]
                rc_failed = process is None or process.returncode != 0
                counts[task.name][bool(rc_failed)] += 1
                print(f'stress: round={number} {task.name} {"FAIL" if rc_failed else "PASS"}'
                      f' elapsed={durations.get(task.name, 0):.3f}s'
                      + (f' first={check.first_failure(task.log)}' if rc_failed else ''), flush=True)
            print(f'stress: round={number} wall={time.monotonic() - tick:.3f}s power={stamp}', flush=True)
    finally:
        runner.close()
        for process in burners:
            check.signal_group(process, signal.SIGTERM)
        for process in burners:
            process.wait()
        for sig, handler in handlers.items():
            signal.signal(sig, handler)
    elapsed = time.monotonic() - started
    for name, (passed, failed) in counts.items():
        print(f'stress: {name} pass={passed} fail={failed}', flush=True)
    print(f'stress: total wall={elapsed:.3f}s logs={output}', flush=True)
    return int(any(failed for passed, failed in counts.values()))


if __name__ == '__main__':
    try:
        sys.exit(main())
    except check.Interrupted as error:
        sys.exit(128 + error.signum)
