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
# Each failure must come from the relevant rule, with the other lint passing.
CASES = (
    ('angle', (1, '[inter-layer]'), (0, '')),
    ('macro', (1, '[forbidden]'), (0, '')),
    ('continuation', (1, '[forbidden]'), (0, '')),
    ('comment', (1, '[forbidden]'), (0, '')),
    ('transitive', (1, '[inter-layer]'), (0, '')),
    ('app-transitive', (1, '[forbidden]'), (0, '')),
    ('intra-layer', (1, '[intra-layer]'), (0, '')),
    ('c-shim', (0, ''), (1, 'reaches a .c file')),
    ('fuzz-shim', (0, ''), (1, 'reaches a .c file')),
    ('missing-dep', (1, 'missing dependency file'), (0, '')),
    ('stale-dep', (0, ''), (1, 'stale dependency file')),
    ('duplicate-layer', (2, 'module piece listed in two layers'), (0, '')),
    ('clean', (0, ''), (0, '')),
)


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

        passed = True
        for lint, (code, diagnostic) in zip(LINTS, verdicts):
            result = subprocess.run([sys.executable, ROOT / 'tools' / lint, '--root', root],
                                    env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, check=False)
            if result.returncode != code or diagnostic not in result.stdout:
                print(f'{name}: FAIL {lint} expected exit {code} {diagnostic!r}, '
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
