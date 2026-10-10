#!/usr/bin/env python3
"""Serial idle trials: A/B/C/A/B/C, fresh >=15s bench inactivity per frame.
Default Xvfb; explicit coordinator opt-in for hardware-display proxy runs.
No quiet-box waiting or root/cold-cache operation.
"""
import argparse
import datetime
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--trials', type=int, default=200)
    parser.add_argument('--idle-seconds', type=float, default=15.0)
    parser.add_argument('--san', action='store_true', help='use build-san binaries; validation only')
    parser.add_argument('--real-display', action='store_true', help='coordinator only: requires explicit matching display and EDIT_ALLOW_REAL_DISPLAY=1')
    parser.add_argument('--check-idle', action='store_true', help='measure an eleven-second no-hint UI-loop/CPU row per variant before trials')
    parser.add_argument('--out', default='build/prewake/trials.log')
    args = parser.parse_args()
    if not 1 <= args.trials <= 200 or args.idle_seconds < 0:
        parser.error('trials must be 1..200 and idle nonnegative')
    if args.real_display:
        env = dict(os.environ)
        if env.get('EDIT_ALLOW_REAL_DISPLAY') != '1' or not env.get('DISPLAY') or env.get('DISPLAY') != env.get('EDIT_DISPLAY'):
            parser.error('real-display mode requires EDIT_ALLOW_REAL_DISPLAY=1 and matching DISPLAY/EDIT_DISPLAY')
    else:
        env = dict(os.environ, DISPLAY=':99', EDIT_DISPLAY=':99')
        env.pop('EDIT_ALLOW_REAL_DISPLAY', None)
    processes = {}
    with open(args.out, 'x', buffering=1) as log:
        def emit(line):
            stamp = datetime.datetime.now(datetime.timezone.utc).isoformat()
            line = f'{stamp} {line}'
            print(line, flush=True)
            log.write(line + '\n')
        emit(f'CONFIG trials={args.trials} idle_seconds={args.idle_seconds} hint_lead_ms=50 order=A/B/C/A/B/C status={"TRACK" if args.idle_seconds >= 15 else "SMOKE"}')
        try:
            for variant in 'abc':
                exe = Path(f'build/prewake/prewake-{variant}{"-san" if args.san else ""}')
                command = [str(exe), '--protocol'] + (['--real-display'] if args.real_display else [])
                proc = subprocess.Popen(command, env=env,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=log, text=True, bufsize=1)
                processes[variant] = proc
                ready = proc.stdout.readline().rstrip()
                if not ready.startswith('READY '):
                    raise RuntimeError(f'{variant}: initialization failed ({ready!r})')
                emit(f'variant={variant} binary_bytes={exe.stat().st_size} {ready}')
            if args.check_idle:
                for variant, proc in processes.items():
                    proc.stdin.write('I\n')
                    proc.stdin.flush()
                    row = proc.stdout.readline().rstrip()
                    if not row.startswith('G11 ') or 'wakeups=0 hint_events=0 warmups=0 ' not in row:
                        raise RuntimeError(f'{variant}: no-hint idle failed ({row!r})')
                    emit(f'variant={variant} {row}')
            for trial in range(1, args.trials + 1):
                for condition in 'NH':
                    for variant in 'abc':
                        # This sleeps after the previous sample; no other bench
                        # renderer runs here. Shared-box unrelated load remains.
                        time.sleep(args.idle_seconds)
                        status = Path('/sys/class/power_supply/BAT0/status').read_text().strip()
                        load = Path('/proc/loadavg').read_text().split()[0]
                        emit(f'BEFORE variant={variant} trial={trial} condition={condition} status={status} load1={load}')
                        proc = processes[variant]
                        proc.stdin.write(condition + '\n')
                        proc.stdin.flush()
                        sample = proc.stdout.readline().rstrip()
                        if not sample.startswith('SAMPLE '):
                            raise RuntimeError(f'{variant}: trial failed ({sample!r})')
                        emit(f'variant={variant} trial={trial} {sample}')
            for variant, proc in processes.items():
                proc.stdin.close()
                if proc.wait() != 0:
                    raise RuntimeError(f'{variant}: nonzero exit')
            emit('DONE all samples collected; proxy only, no gate verdict or variant choice')
        finally:
            for proc in processes.values():
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait()


if __name__ == '__main__':
    main()
