#!/usr/bin/env python3
"""Exercise lane and registry checks in disposable checkouts under build/."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = Path('tests/lanes.mk')


def write(root, path, text):
    dest = root / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(text, encoding='utf-8')


def append(root, path, text):
    dest = root / path
    previous = dest.read_text(encoding='utf-8') if dest.exists() else ''
    write(root, path, previous + text)


def prepare(root):
    shutil.copy2(ROOT / 'Makefile', root)
    for directory in ('mk', 'src', 'tests', 'fuzz', 'tools'):
        shutil.copytree(ROOT / directory, root / directory,
                        ignore=shutil.ignore_patterns('__pycache__', 'lanes'))


def run(root, *args):
    env = os.environ.copy()
    for name in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES'):
        env.pop(name, None)
    env['TMPDIR'] = str(root)
    return subprocess.run(['make', '--no-print-directory', '-s', '-j1', 'B=build/case', *args],
                          cwd=root, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, check=False)


def reject(result, diagnostic):
    return result.returncode != 0 and diagnostic in result.stdout


def stub_law2_guard(root, active):
    guard = f"""#include "base/base.h"
bool base_malloc_guard_active(void) {{ return {active}; }}
void base_malloc_guard_begin(void) {{}}
size_t base_malloc_guard_end(void) {{ return 0; }}
void base_malloc_guard_exempt_begin(void) {{}}
void base_malloc_guard_exempt_end(void) {{}}
"""
    write(root, 'src/base/malloc_guard.c', guard)


def leave_stale_suite_binary(root):
    append(root, MANIFEST, '\nSAN_SUITES += tests/absent_test.c\n')
    binary = 'build/case/san/tests/absent_test'
    write(root, binary, '#!/bin/sh\nexit 0\n')
    (root / binary).chmod(0o755)


def cases(baseline):
    registry = 'check-lints' if baseline else 'check-layout'
    yield ('empty Law-2 lane without a declaration',
           lambda repo: append(repo, MANIFEST, '\nLAW2_SUITES :=\n'),
           ('check-law2',), 'check-law2 has no suites and no first-wave declaration')
    yield ('listed suite missing on disk',
           leave_stale_suite_binary, ('check-san',),
           'listed suite is missing: tests/absent_test.c')
    yield ('unlisted nested suite',
           lambda repo: write(repo, 'tests/contract/unlisted_test.c', 'int main(void) { return 0; }\n'),
           ('check-san',), 'unlisted suite: tests/contract/unlisted_test.c')
    for active in ('false', 'true'):
        yield (f'Law-2 guard without interposition (active={active})',
               lambda repo, value=active: stub_law2_guard(repo, value),
               ('check-law2',), f'guard positive control: active={int(active == "true")} count=0')
    yield ('unregistered nested tool file',
           lambda repo: write(repo, 'tools/helpers/unregistered.txt', ''),
           (registry,), 'unregistered file: tools/helpers/unregistered.txt')
    yield ('registered but missing tool file',
           lambda repo: append(repo, 'tools/README.md',
                               '\n| `absent.txt` | Fixture | Fixture | `check-lints` |\n'),
           (registry,), 'registered file is missing: tools/absent.txt')
    yield ('variants directory',
           lambda repo: (repo / 'docs' / 'variants').mkdir(parents=True),
           (registry,), 'forbidden directory: docs/variants/')


def check_lint_objects(repo, baseline):
    write(repo, 'src/base/nested/real.c', 'int base_real(void) { return 0; }\n')
    write(repo, 'tests/contract/real.c', 'int real(void) { return 0; }\n')
    write(repo, 'fuzz/nested/real.c', 'int real(void) { return 0; }\n')
    write(repo, 'tests/symbols/lint-fixture/probe.c', '#error fixture entered real lint set\n')
    write(repo, 'fuzz/corpus/probe/seed.c', '#error seed entered real lint set\n')
    target = 'print-fixture-lint-objs: ; @printf "%s\\n" $(LINT_OBJS)'
    result = run(repo, '--eval', target, 'print-fixture-lint-objs')
    objects = result.stdout.splitlines()
    required = {'build/case/rel/src/base/nested/real.o', 'build/case/rel/tests/contract/real.o',
                'build/case/fuzz/fuzz/nested/real.o'}
    if (result.returncode != 0 or not required.issubset(objects)
            or any('/tests/symbols/' in obj or '/fuzz/corpus/' in obj for obj in objects)):
        print(f'lint objects: FAIL got exit {result.returncode}')
        print(result.stdout, end='')
        return False
    if not baseline:
        result = run(repo, 'check-includes')
        if result.returncode != 0:
            print(f'lint objects: FAIL check-includes got exit {result.returncode}')
            print(result.stdout, end='')
            return False
        write(repo, 'tests/contract/real.c', '#include "../../src/base/nested/real.c"\n')
        result = run(repo, 'check-includes')
        if not reject(result, 'tests/contract/real.c: reaches a .c file'):
            print(f'lint objects: FAIL expected forbidden C include, got exit {result.returncode}')
            print(result.stdout, end='')
            return False
    print('lint objects: checked real nested sources and excluded fixtures and fuzz seeds')
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--baseline', action='store_true')
    args = parser.parse_args()
    build = (ROOT / args.build_dir).resolve()
    build.mkdir(parents=True, exist_ok=True)
    passed = total = 0
    with tempfile.TemporaryDirectory(prefix='lane-fixtures-', dir=build) as directory:
        root = Path(directory)
        for total, (name, mutate, targets, diagnostic) in enumerate(cases(args.baseline), 1):
            repo = root / f'case-{total}'
            repo.mkdir()
            prepare(repo)
            mutate(repo)
            result = run(repo, *targets)
            if reject(result, diagnostic):
                print(f'{name}: rejected as expected')
                passed += 1
            else:
                print(f'{name}: FAIL expected {diagnostic!r}, got exit {result.returncode}')
                print(result.stdout, end='')

        total += 1
        repo = root / 'lint-objects'
        repo.mkdir()
        prepare(repo)
        passed += check_lint_objects(repo, args.baseline)
    print(f'lane fixtures: {passed}/{total} passed')
    return int(passed != total)


if __name__ == '__main__':
    raise SystemExit(main())
