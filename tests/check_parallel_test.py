#!/usr/bin/env python3
"""tools/check_parallel.py smoke test (edit-nafu): timing-cache parsing, a display waiter never
holds the job a headless suite needs, a failing suite reports its log tail while others finish,
Ctrl-C exits 130 with suites and private Xvfbs gone. Run from the repo root:
python3 tests/check_parallel_test.py"""
import importlib.util
import contextlib
from datetime import datetime
import io
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
from unittest.mock import patch

root = Path.cwd()
import tempfile
work = Path(tempfile.mkdtemp(prefix='check-parallel-test-'))
work.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location('check_parallel', root/'tools/check_parallel.py')
m = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = m
spec.loader.exec_module(m)
m.LOGS = work/'logs'
m.LOGS.mkdir(exist_ok=True)
m.TIMES = work/'times.txt'
m.TIMES.write_text('broken\nnegative -1\nnotfinite nan\nknown 2.5\n')
assert m.read_times() == {'known': 2.5}

marker = work/'plain-ready'
marker.unlink(missing_ok=True)

def helper(name, body):
    path = work/name
    path.write_text('#!/usr/bin/python3\nimport os, time\nfrom pathlib import Path\n' + body)
    path.chmod(0o755)
    return str(path)

first = helper('display_a', f'''assert os.environ['EDIT_REFUSE_REAL_DISPLAY'] == '1'
assert int(os.environ['DISPLAY'][1:]) >= 100
stop = time.monotonic() + {m.STARTUP_SECONDS}
while not Path({str(marker)!r}).exists():
    assert time.monotonic() < stop, 'plain suite starved behind display waiters'
    time.sleep(0.01)
''')
second = helper('display_b', "print('intentional exit-7 tail sentinel', flush=True)\nraise SystemExit(7)\n")
plain = helper('plain', f'''assert os.environ['EDIT_REFUSE_REAL_DISPLAY'] == '1'
assert os.environ['DISPLAY'] == ':98'
Path({str(marker)!r}).touch()
''')
runner = m.Runner(2, 1)
runner.environment['DISPLAY'] = ':98'
times = {}
try:
    runner.start_displays()
    socket = Path('/tmp/.X11-unix/X'+runner.available[0][0][1:])
    failed = runner.run([m.Task(first, display=True), m.Task(second, display=True), m.Task(plain)], times)
    assert failed == {second}
    assert set(times) == {'display_a', 'display_b', 'plain'}
    assert 'intentional exit-7' in m.tail(m.LOGS/'display_b.log')
    m.save_times(times)
    assert set(m.read_times()) == set(times)
finally:
    runner.close()
assert not socket.exists()
assert all(p.poll() is not None for p, _ in runner.servers)
print('PASS: display waiters leave CPU slots free; failure aggregation/tail; cache parse/save; Xvfb cleanup')

ready = work/'cancel-ready'
ready.unlink(missing_ok=True)
cancel = helper('cancel', f'''Path({str(ready)!r}).write_text(str(os.getpid())+' '+os.environ['DISPLAY'])
while True:
    time.sleep(1)
''')
proc = subprocess.Popen([sys.executable, 'tools/check_parallel.py', '--display', cancel, '--', cancel],
                        env=dict(os.environ, CHECK_JOBS='1', CHECK_DISPLAYS='1'),
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
stop = time.monotonic()+m.STARTUP_SECONDS
try:
    while not ready.exists():
        assert proc.poll() is None
        assert time.monotonic() < stop
        time.sleep(0.01)
    child, display = ready.read_text().split()
    proc.send_signal(signal.SIGINT)
    output = proc.communicate(timeout=m.STARTUP_SECONDS)[0]
    assert proc.returncode == 130, output
    assert not Path('/tmp/.X11-unix/X'+display[1:]).exists()
    try:
        os.kill(int(child), 0)
    except ProcessLookupError:
        pass
    else:
        raise AssertionError('suite survived cancellation')
    assert 'interrupted by SIGINT' in output
finally:
    if proc.poll() is None:
        proc.kill(); proc.wait()
print('PASS: Ctrl-C exits 130, kills suite process group, removes private Xvfb socket')

class OrderedRunner(m.Runner):
    def launch(self, task):
        order.append(task.name)
        return super().launch(task)

order = []
fast = helper('known_fast', 'pass\n')
slow = helper('known_slow', 'pass\n')
unknown = helper('unknown', 'pass\n')
runner = OrderedRunner(1, 0)
try:
    assert not runner.run([m.Task(fast), m.Task(slow), m.Task(unknown)], {'known_fast': 1, 'known_slow': 2})
finally:
    runner.close()
assert order == ['unknown', 'known_slow', 'known_fast'], order
print('PASS: unknown suites first, then longest recorded duration first')

# edit-edgt: a failed task is retried exactly once, after its peer finishes.
# Cover exit status at main(), original/rerun logs, and the persistent audit.
for permanent in (False, True):
    case = work / ('permanent' if permanent else 'flaky')
    case.mkdir()
    attempts = case / 'attempts'
    initial_done = case / 'initial-done'
    peer_done = case / 'peer-done'
    suspect = helper(case.name + '_suspect', f'''attempts = Path({str(attempts)!r})
count = int(attempts.read_text()) if attempts.exists() else 0
attempts.write_text(str(count + 1))
if count == 0:
    print('setup noise before the assertion', flush=True)
    print('test: FAIL first failure sentinel', flush=True)
    Path({str(initial_done)!r}).touch()
    raise SystemExit(7)
assert Path({str(peer_done)!r}).exists(), 'rerun overlapped a parallel peer'
print('rerun failure sentinel' if {permanent!r} else 'isolated pass sentinel', flush=True)
raise SystemExit(7 if {permanent!r} else 0)
''')
    peer = helper(case.name + '_peer', f'''stop = time.monotonic() + {m.STARTUP_SECONDS}
while not Path({str(initial_done)!r}).exists():
    assert time.monotonic() < stop
    time.sleep(0.01)
Path({str(peer_done)!r}).touch()
''')
    m.LOGS = case / 'logs'
    m.TIMES = case / 'times.txt'
    output = io.StringIO()
    with patch.object(sys, 'argv', ['check_parallel.py', suspect, peer]), \
            patch.dict(os.environ, CHECK_JOBS='2', CHECK_DISPLAYS='1'), \
            contextlib.redirect_stdout(output):
        rc = m.main()
    assert attempts.read_text() == '2', output.getvalue()
    assert rc == int(permanent), output.getvalue()
    name = Path(suspect).name
    assert 'test: FAIL first failure sentinel' in (m.LOGS / (name + '.log')).read_text()
    rerun = (m.LOGS / (name + '.rerun.log')).read_text()
    assert ('rerun failure sentinel' if permanent else 'isolated pass sentinel') in rerun
    flaky_log = m.LOGS.parent / 'check-flaky.log'
    if permanent:
        assert 'check: FLAKY' not in output.getvalue()
        assert not flaky_log.exists()
    else:
        assert f'check: FLAKY {name} (failed in parallel, passed alone; see {m.LOGS / (name + ".log")} and {flaky_log})' in output.getvalue()
        audit = flaky_log.read_text()
        assert name in audit and 'test: FAIL first failure sentinel' in audit
        assert datetime.now().astimezone().date().isoformat() in audit
        assert 'setup noise' not in audit
print('PASS: isolated rerun recovers flakes, preserves both logs/audit; repeated failure stays red')

# A flaky part must not clear a sibling's permanent failure in the same binary.
case = work / 'split'
case.mkdir()
split = helper('split_suite', f'''import sys
selector = sys.argv[1]
attempts = Path({str(case)!r}) / selector
count = int(attempts.read_text()) if attempts.exists() else 0
attempts.write_text(str(count + 1))
print('FAIL split sentinel' if count == 0 or selector == '--hard' else 'PASS split rerun')
raise SystemExit(7 if count == 0 or selector == '--hard' else 0)
''')
m.LOGS = case / 'logs'
m.TIMES = case / 'times.txt'
output = io.StringIO()
with patch.object(sys, 'argv', ['check_parallel.py', split]), \
        patch.dict(os.environ, CHECK_JOBS='2', CHECK_DISPLAYS='1'), \
        patch.dict(m.PARTS, split_suite=('--hard', '--flaky')), \
        contextlib.redirect_stdout(output):
    assert m.main() == 1
assert (case / '--flaky').read_text() == (case / '--hard').read_text() == '2'
assert 'check: FLAKY split_suite.flaky ' in output.getvalue()
assert 'check: FLAKY split_suite.hard ' not in output.getvalue()
for part in ('flaky', 'hard'):
    assert (m.LOGS / f'split_suite.{part}.log').exists()
    assert (m.LOGS / f'split_suite.{part}.rerun.log').exists()
assert '== split_suite.flaky' in (m.LOGS / 'split_suite.log').read_text()
print('PASS: split tasks retry individually and preserve a sibling permanent failure')
