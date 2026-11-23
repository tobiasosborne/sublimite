#!/usr/bin/env python3
"""Exercise lane and registry checks in disposable checkouts under build/."""

import argparse
import os
from pathlib import Path
import re
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


class MutationMissed(Exception):
    pass


def edit(root, path, pattern, replacement):
    text, count = re.subn(pattern, replacement, (root / path).read_text(encoding='utf-8'),
                          flags=re.MULTILINE)
    if not count:
        raise MutationMissed(f'{path} has no match for {pattern!r}')
    write(root, path, text)


def suite_line(suite):
    return rf'^{re.escape(suite)}[ \t]*:=.*\n'


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
    append(root, MANIFEST, 'tests/absent_test.c := san\n')
    binary = 'build/case/san/tests/absent_test'
    write(root, binary, '#!/bin/sh\nexit 0\n')
    (root / binary).chmod(0o755)


def cases(baseline):
    registry = 'check-lints' if baseline else 'check-layout'
    yield ('no line names the Law-2 lane',
           lambda repo: edit(repo, MANIFEST, r'[ \t]law2\b', ''),
           ('check-law2',), 'check-law2 requires at least one suite')
    # v2-he7.26: the manifest is a table, one line per suite naming its lanes.
    yield ('suite line deleted',
           lambda repo: edit(repo, MANIFEST, suite_line('tests/trace_test.c'), ''),
           ('check-san',), 'unlisted suite: tests/trace_test.c')
    yield ('duplicate suite line',
           lambda repo: edit(repo, MANIFEST, suite_line('tests/trace_test.c'),
                             'tests/trace_test.c := san tsan\ntests/trace_test.c := san\n'),
           ('check-tsan',), 'duplicate line for tests/trace_test.c')
    yield ('unknown lane',
           lambda repo: edit(repo, MANIFEST, suite_line('tests/trace_test.c'),
                             'tests/trace_test.c := san tsna\n'),
           ('check-san',), 'tests/trace_test.c: unknown lane tsna')
    yield ('unsorted lines',
           lambda repo: edit(repo, MANIFEST, suite_line('tests/base_test.c'), '')
           or append(repo, MANIFEST, 'tests/base_test.c := san tsan\n'),
           ('check-san',), 'not sorted: tests/base_test.c after tests/work_test.c')
    # The ipc merge shape (v2-0fv.6): a lane re-assigned after the += blocks.
    # The exact shape also drops work, which the threaded-name rule happened to
    # catch; the second case drops only trace, which nothing else would catch.
    yield ('lane re-assigned after += (exact ipc merge shape)',
           lambda repo: append(repo, MANIFEST,
                               'TSAN_SUITES := tests/os_test.c tests/base_test.c tests/ipc_test.c\n'),
           ('check-tsan',),
           'not a suite line: TSAN_SUITES := tests/os_test.c tests/base_test.c tests/ipc_test.c')
    yield ('lane re-assigned after += (drops only trace)',
           lambda repo: append(repo, MANIFEST, 'TSAN_SUITES := tests/os_test.c tests/base_test.c '
                               'tests/ipc_test.c tests/work_test.c\n'),
           ('check-tsan',), 'not a suite line: TSAN_SUITES := tests/os_test.c')
    yield ('misspelled lane variable',
           lambda repo: append(repo, MANIFEST, 'TSAN_SUITE += tests/os_endpoint_test.c\n'),
           ('check-tsan',), 'not a suite line: TSAN_SUITE += tests/os_endpoint_test.c')
    yield ('Makefile derivation disagrees with the table',
           lambda repo: edit(repo, 'Makefile', r'^(TSAN_SUITES *:= *)(.*)$',
                             r'\1$(filter-out tests/trace_test.c,\2)'),
           ('check-tsan',), 'check-tsan: make derived a list that differs from tests/lanes.mk')
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
    # A row naming a path with a slash registers a repo-relative tool outside tools/.
    yield ('registered but missing repo-relative tool',
           lambda repo: append(repo, 'tools/README.md',
                               '\n| `tests/absent/tool.py` | Fixture | Fixture | `check-lints` |\n'),
           (registry,), 'registered file is missing: tests/absent/tool.py')
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


UB_SOURCE = """#include <limits.h>
#include <stdint.h>
#include <stddef.h>
static volatile int sink;
static int add(int a, int b) { return a + b; }
"""


def check_ubsan_fatal(repo):
    """W0.7 finding 10: a runtime signed overflow must fail the sanitizer and fuzz lanes'
    binaries (built with the lanes' own flags), not log and exit 0."""
    write(repo, 'tests/ubsan_probe_test.c', UB_SOURCE +
          'int main(void) { volatile int big = INT_MAX; sink = add(big, 1); return 0; }\n')
    write(repo, 'fuzz/ubsan_probe_fuzz.c', UB_SOURCE +
          'int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n);\n'
          'int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {\n'
          '    (void)d; volatile int big = INT_MAX; if (n) sink = add(big, 1); return 0; }\n')
    ok = True
    for target, argv in (('build/case/san/tests/ubsan_probe_test', []),
                         ('build/case/fuzz/ubsan_probe_fuzz', ['-runs=1'])):
        built = run(repo, target)
        if built.returncode != 0:
            print(f'ubsan fatal: FAIL could not build {target}')
            print(built.stdout, end='')
            ok = False
            continue
        seed = repo / 'build' / 'ubsan-seed'
        seed.write_bytes(b'x')
        result = subprocess.run([str(repo / target), *argv, *([str(seed)] if argv else [])],
                                cwd=repo, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, check=False)
        if result.returncode != 0 and 'runtime error: signed integer overflow' in result.stdout:
            print(f'ubsan fatal ({target.rsplit("/", 1)[1]}): rejected as expected')
        else:
            print(f'ubsan fatal ({target.rsplit("/", 1)[1]}): FAIL exit {result.returncode}')
            print(result.stdout, end='')
            ok = False
    return int(ok)


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
            try:
                mutate(repo)
            except MutationMissed as error:
                print(f'{name}: FAIL fixture did not apply: {error}')
                continue
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

        total += 1
        repo = root / 'ubsan-fatal'
        repo.mkdir()
        prepare(repo)
        passed += check_ubsan_fatal(repo)
    print(f'lane fixtures: {passed}/{total} passed')
    return int(passed != total)


if __name__ == '__main__':
    raise SystemExit(main())
