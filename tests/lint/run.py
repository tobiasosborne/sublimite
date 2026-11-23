#!/usr/bin/env python3
"""Compile fixture translation units and check both dependency lints' verdicts."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / 'fixtures'
LINTS = ('check_dag.py', 'check_no_c_includes.py')
UPWARD = '{0}: error: [inter-layer] {1} (L{2}) reaches "{3}" ({4}, L{5}): upward include'
L5_X11 = ('src/app/probe.c: error: [forbidden] app (L5) reaches "src/x11/x11.h" (x11, L3): '
          'L5 may not reach backend headers')
C_FILE = ('{0}: error: reaches a .c file "{1}" '
          '(link the layer archive and include the public header instead)')
# Each failure is the complete diagnostic of its rule, naming the source and the
# dependency it reaches, with the other lint passing; clean passes both.
CASES = (
    ('angle', (1, UPWARD.format('src/os/probe.c', 'os', 0, 'src/piece/piece.h', 'piece', 1)), (0,)),
    ('macro', (1, L5_X11), (0,)),
    ('continuation', (1, L5_X11), (0,)),
    ('comment', (1, L5_X11), (0,)),
    ('transitive', (1, UPWARD.format('src/os/probe.c', 'os', 0, 'src/app/app.h', 'app', 5)), (0,)),
    ('app-transitive', (1, L5_X11), (0,)),
    ('intra-layer', (1, 'src/x11/probe.c: error: [intra-layer] x11 (L3) reaches "src/wayland/wayland.h" '
                        '(wayland, L3): wayland is not below x11 in INTRA_RULES'), (0,)),
    ('c-shim', (0,), (1, C_FILE.format('tests/probe_test.c', 'tests/../bench/../src/os/probe.c'))),
    ('fuzz-shim', (0,), (1, C_FILE.format('fuzz/probe_fuzz.c', 'fuzz/../src/os/probe.c'))),
    ('missing-dep', (1, 'src/os/probe.c: error: missing dependency file build/rel/src/os/probe.d'), (0,)),
    ('stale-dep', (0,), (1, 'tests/probe_test.c: error: stale dependency file build/rel/tests/probe_test.d '
                            '(older than tests/probe_test.c)')),
    ('duplicate-layer', (2, 'check_dag: config error: module piece listed in two layers'), (0,)),
    ('clean', (0,), (0,)),
)


def diagnostics(output):
    return tuple(line for line in output.splitlines() if ' error: ' in line)


def run_case(name, verdicts):
    with tempfile.TemporaryDirectory(prefix='lint-fixture-') as tmp:
        root = Path(tmp)
        (root / 'src').mkdir()
        shutil.copy2(ROOT / 'Makefile', root)
        shutil.copytree(ROOT / 'mk', root / 'mk')
        for template in sorted((FIXTURES / name).rglob('*.in')):
            dest = root / template.relative_to(FIXTURES / name).with_suffix('')
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(template, dest)

        if name == 'duplicate-layer':
            layers = root / 'mk/layers.mk'
            data = layers.read_text()
            before, line, after = data.partition('\nL1_MODS')
            assert line
            layers.write_text(before + '\nL0_MODS += piece' + line + after)

        env = os.environ.copy()
        for var in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES'):
            env.pop(var, None)
        objects = []
        for directory, cfg in (('src', 'rel'), ('tests', 'rel'), ('fuzz', 'fuzz')):
            objects.extend(Path('build', cfg, source.relative_to(root)).with_suffix('.o')
                           for source in sorted((root / directory).rglob('*.c')))
        if objects and name != 'missing-dep':
            subprocess.run(['make', '-s', '-j1', *objects], cwd=root, env=env, check=True)
        if name == 'stale-dep':
            dep = root / objects[0].with_suffix('.d')
            older = (root / 'tests/probe_test.c').stat().st_mtime_ns - 1
            os.utime(dep, ns=(older, older))

        inventory = {directory: [str(path.relative_to(root)) for path in sorted((root / directory).rglob('*.c'))]
                     for directory in ('tests', 'fuzz')}
        arguments = {'check_dag.py': [],
                     'check_no_c_includes.py': ['--tests', *inventory['tests'], '--fuzz', *inventory['fuzz']]}
        passed = True
        for lint, (code, *expected) in zip(LINTS, verdicts):
            result = subprocess.run([sys.executable, ROOT / 'tools' / lint, '--root', root, *arguments[lint]],
                                    env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, check=False)
            if result.returncode != code or diagnostics(result.stdout) != tuple(expected):
                print(f'{name}: FAIL {lint} expected exit {code} {expected!r}, '
                      f'got exit {result.returncode}')
                print(result.stdout, end='')
                passed = False
        if passed:
            print(f'{name}: ok')
        return passed


def main():
    passed = sum(run_case(name, (dag, no_c)) for name, dag, no_c in CASES)
    print(f'lint fixtures: {passed}/{len(CASES)} passed')
    return int(passed != len(CASES))


if __name__ == '__main__':
    sys.exit(main())
