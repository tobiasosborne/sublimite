#!/usr/bin/env python3
"""Exercise lane and registry checks in disposable checkouts under build/.

The unmutated copy must pass the inventory first. Each mutated copy must then
fail with exactly its expected error lines: a red that also prints any other
error is not evidence for the boundary it names.
"""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = Path('tests/lanes.mk')
REGISTRY = Path('tools/README.md')
ERROR = re.compile(r'^\S+: error: ')


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
                        ignore=shutil.ignore_patterns('__pycache__'))


def run(root, *args):
    env = os.environ.copy()
    for name in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES'):
        env.pop(name, None)
    env['TMPDIR'] = str(root)
    return subprocess.run(['make', '--no-print-directory', '-s', '-j1', 'B=build/case', *args],
                          cwd=root, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, check=False)


def errors(result):
    return [line for line in result.stdout.splitlines() if ERROR.match(line)]


def suites(*messages):
    return [f'check-suites: error: {message}' for message in messages]


def registry(*messages):
    return [f'check-registry: error: {message}' for message in messages]


def line_of(repo, path, prefix):
    """Line number of the first line of path (after mutation) starting with prefix."""
    lines = (repo / path).read_text(encoding='utf-8').splitlines()
    return next((number for number, line in enumerate(lines, 1) if line.startswith(prefix)), None)


def last_line(repo, path):
    return len((repo / path).read_text(encoding='utf-8').splitlines())


def add_row(path):
    def mutate(repo):
        append(repo, REGISTRY, f'\n| `{path}` | Fixture | Fixture | `check-lints` |\n')
    return mutate


def row_problem(path, problem):
    return lambda repo: registry(f'{REGISTRY}:{line_of(repo, REGISTRY, f"| `{path}` |")}: {problem}: {path}')


def stub_law2_guard(root, active):
    guard = f"""#include "base/base.h"
bool base_malloc_guard_active(void) {{ return {active}; }}
void base_malloc_guard_begin(void) {{}}
size_t base_malloc_guard_end(void) {{ return 0; }}
void base_malloc_guard_exempt_begin(void) {{}}
void base_malloc_guard_exempt_end(void) {{}}
"""
    write(root, 'src/base/malloc_guard.c', guard)


def law2_guard_rejected(active):
    """The release base suite, not trace or work, must fail on the inert guard."""
    def check(result):
        lines = result.stdout.splitlines()
        ran = [line for line in lines if line.startswith('== ')]
        return (result.returncode != 0 and ran[-1:] == ['== build/case/rel/tests/law2_base_test']
                and f'guard positive control: active={active} count=0' in lines)
    return lambda repo: check


def leave_stale_suite_binary(root):
    edit(root, MANIFEST, r'^(?=tests/base_test\.c :=)', 'tests/absent_test.c := san\n')  # sorted
    binary = 'build/case/san/tests/absent_test'
    write(root, binary, '#!/bin/sh\nexit 0\n')
    (root / binary).chmod(0o755)


def empty_corpus(root):
    for seed in (root / 'fuzz/corpus/os').iterdir():
        seed.unlink()


def unreadable_corpus(root):
    for seed in (root / 'fuzz/corpus/os').iterdir():
        seed.chmod(0)


def symlink_escape(root):
    write(root.parent, 'escape-target.txt', 'outside the checkout\n')
    (root / 'tools/escape.txt').symlink_to(root.parent / 'escape-target.txt')
    add_row('tools/escape.txt')(root)


def parent_escape(root):
    write(root.parent, 'escape.txt', 'outside the checkout\n')  # exists: containment comes first
    add_row('tools/../../escape.txt')(root)


COMPOUND = '| `tools/check_dag.py` and `tools/clex.py` | Fixture | Fixture | `check-lints` |'


def law2_lane_empty(repo):
    law2 = re.findall(r'^(tests/law2_\w+_test\.c) := san$', (repo / MANIFEST).read_text(), re.MULTILINE)
    return suites('check-law2 requires at least one suite',
                  *(f'Law-2/G1 suite must be registered in check-law2: {suite}' for suite in law2))


def cases():
    """(name, mutation, make targets, expected): the expected error lines, or a function of
    the mutated copy returning them or a verdict on the make result."""
    yield ('no line names the Law-2 lane',
           lambda repo: edit(repo, MANIFEST, r'[ \t]law2$', ' san'), ('check-law2',), law2_lane_empty)
    # v2-he7.26: the manifest is a table, one line per suite naming its lanes.
    yield ('suite line deleted',
           lambda repo: edit(repo, MANIFEST, suite_line('tests/trace_test.c'), ''),
           ('check-san',), suites(f'unlisted suite: tests/trace_test.c (add its line to {MANIFEST})'))
    yield ('duplicate suite line',
           lambda repo: edit(repo, MANIFEST, suite_line('tests/trace_test.c'),
                             'tests/trace_test.c := san tsan\ntests/trace_test.c := san\n'),
           ('check-tsan',), lambda repo: suites(
               f'{MANIFEST}:{line_of(repo, MANIFEST, "tests/trace_test.c := san") + 1}: '
               'duplicate line for tests/trace_test.c'))
    yield ('unknown lane',
           lambda repo: edit(repo, MANIFEST, suite_line('tests/trace_test.c'),
                             'tests/trace_test.c := san tsna\n'),
           ('check-san',), lambda repo: suites(
               f'{MANIFEST}:{line_of(repo, MANIFEST, "tests/trace_test.c := san tsna")}: '
               'tests/trace_test.c: unknown lane tsna'))
    yield ('unsorted lines',
           lambda repo: edit(repo, MANIFEST, suite_line('tests/base_test.c'), '')
           or append(repo, MANIFEST, 'tests/base_test.c := san tsan\n'),
           ('check-san',), lambda repo: suites(
               f'{MANIFEST}:{last_line(repo, MANIFEST)}: not sorted: tests/base_test.c after tests/work_test.c'))
    # The ipc merge shape (v2-0fv.6): a lane re-assigned after the += blocks.
    yield ('lane re-assigned after += (exact ipc merge shape)',
           lambda repo: append(repo, MANIFEST,
                               'TSAN_SUITES := tests/os_test.c tests/base_test.c tests/ipc_test.c\n'),
           ('check-tsan',),
           lambda repo: suites(f'{MANIFEST}:{last_line(repo, MANIFEST)}: not a suite line: '
                               'TSAN_SUITES := tests/os_test.c tests/base_test.c tests/ipc_test.c'))
    yield ('misspelled lane variable',
           lambda repo: append(repo, MANIFEST, 'TSAN_SUITE += tests/os_endpoint_test.c\n'),
           ('check-tsan',), lambda repo: suites(
               f'{MANIFEST}:{last_line(repo, MANIFEST)}: not a suite line: TSAN_SUITE += tests/os_endpoint_test.c'))
    yield ('Makefile derivation disagrees with the table',
           lambda repo: edit(repo, 'Makefile', r'^(TSAN_SUITES *:= *)(.*)$',
                             r'\1$(filter-out tests/trace_test.c,\2)'),
           ('check-tsan',), suites(f'check-tsan: make derived a list that differs from {MANIFEST}'))
    yield ('listed suite missing on disk',
           leave_stale_suite_binary, ('check-san',),
           suites('check-san listed suite is missing: tests/absent_test.c'))
    yield ('unlisted nested suite',
           lambda repo: write(repo, 'tests/contract/unlisted_test.c', 'int main(void) { return 0; }\n'),
           ('check-san',),
           suites(f'unlisted suite: tests/contract/unlisted_test.c (add its line to {MANIFEST})'))
    # W0.7 finding 11: required lane overlap and readable fuzz seeds.
    yield ('layer suite dropped from SAN',
           lambda repo: edit(repo, MANIFEST, r'^(tests/layer_L2_test\.c := layers) san$', r'\1'),
           ('check-san',), suites('check-san must also run check-layers suite tests/layer_L2_test.c'))
    for name, mutate in (('empty', empty_corpus), ('unreadable', unreadable_corpus)):
        yield (f'{name} fuzz corpus', mutate, ('fuzz-smoke',),
               suites('fuzz-smoke corpus has no readable seed: fuzz/corpus/os/ (commit minimal seeds)'))
    for active in ('false', 'true'):
        yield (f'Law-2 guard without interposition (active={active})',
               lambda repo, value=active: stub_law2_guard(repo, value),
               ('check-law2',), law2_guard_rejected(int(active == 'true')))
    yield ('unregistered nested tool file',
           lambda repo: write(repo, 'tools/helpers/unregistered.txt', ''),
           ('check-layout',), registry('unregistered file: tools/helpers/unregistered.txt'))
    # W0.7 finding 12: one repo-relative path per row, contained before it exists.
    yield ('registered nested tool', lambda repo: write(repo, 'tools/helpers/t.py', '')
           or add_row('tools/helpers/t.py')(repo), ('check-layout',), [])
    yield ('registered but missing tool file', add_row('tools/absent.txt'), ('check-layout',),
           row_problem('tools/absent.txt', 'registered file is missing'))
    yield ('registered but missing repo-relative tool', add_row('tests/absent/tool.py'),
           ('check-layout',), row_problem('tests/absent/tool.py', 'registered file is missing'))
    yield ('removed external helper', lambda repo: (repo / 'tests/contract/xdriver.py').unlink(),
           ('check-layout',), row_problem('tests/contract/xdriver.py', 'registered file is missing'))
    yield ('absolute registry path', add_row(str(ROOT / 'tools/clex.py')), ('check-layout',),
           row_problem(str(ROOT / 'tools/clex.py'), 'not a canonical repo-relative path'))
    yield ('parent escape', parent_escape, ('check-layout',),
           row_problem('tools/../../escape.txt', 'not a canonical repo-relative path'))
    yield ('symlink escape', symlink_escape, ('check-layout',),
           row_problem('tools/escape.txt', 'resolves through a symlink'))
    yield ('duplicate registry row', add_row('tools/clex.py'), ('check-layout',),
           lambda repo: registry(f'{REGISTRY}:{last_line(repo, REGISTRY)}: duplicate registry entry: tools/clex.py'))
    yield ('compound first cell', lambda repo: append(repo, REGISTRY, f'\n{COMPOUND}\n'), ('check-layout',),
           lambda repo: registry(f'{REGISTRY}:{last_line(repo, REGISTRY)}: '
                                 f'malformed row (first cell must be one `path`): {COMPOUND}'))
    yield ('variants directory',
           lambda repo: (repo / 'docs' / 'variants').mkdir(parents=True),
           ('check-layout',), registry('forbidden directory: docs/variants/'))


def check_case(repo, name, mutate, targets, expected):
    try:
        mutate(repo)
    except MutationMissed as error:
        print(f'{name}: FAIL fixture did not apply: {error}')
        return False
    want = expected(repo) if callable(expected) else expected
    result = run(repo, *targets)
    if (want(result) if callable(want) else
            (result.returncode != 0) == bool(want) and errors(result) == want):
        print(f'{name}: {"rejected" if want else "accepted"} as expected')
        return True
    print(f'{name}: FAIL expected {want!r}, got exit {result.returncode}')
    print(result.stdout, end='')
    return False


def check_lint_objects(repo):
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
    result = run(repo, 'check-includes')
    if result.returncode != 0 or errors(result):
        print(f'lint objects: FAIL check-includes got exit {result.returncode}')
        print(result.stdout, end='')
        return False
    # A real nested test reaching a real nested source through a header shim.
    write(repo, 'tests/contract/shim.h', '#include "../../src/base/nested/real.c"\n')
    write(repo, 'tests/contract/real.c', '#include "shim.h"\n')
    result = run(repo, 'check-includes')
    expected = ['tests/contract/real.c: error: reaches a .c file "tests/contract/../../src/base/nested/real.c" '
                '(link the layer archive and include the public header instead)']
    if result.returncode == 0 or errors(result) != expected:
        print(f'lint objects: FAIL expected {expected!r}, got exit {result.returncode}')
        print(result.stdout, end='')
        return False
    print('lint objects: checked real nested sources and excluded fixtures and fuzz seeds')
    return True


def check_lane_runs_every_binary(repo):
    """W0.7 finding 26: with the count assertions gone, the first failing binary still fails
    the lane and later binaries do not run; an all-passing list reports its count."""
    for name, body in (('pass1', 'exit 0'), ('fail', 'echo probe failed; exit 1'), ('pass2', 'exit 0')):
        write(repo, f'build/case/{name}', f'#!/bin/sh\n{body}\n')
        (repo / f'build/case/{name}').chmod(0o755)
    failing = run(repo, 'check-san', 'TEST_SAN=build/case/pass1 build/case/fail build/case/pass2')
    passing = run(repo, 'check-san', 'TEST_SAN=build/case/pass1 build/case/pass2')
    ran = [line for line in failing.stdout.splitlines() if line.startswith('== ')]
    if (failing.returncode != 0 and ran == ['== build/case/pass1', '== build/case/fail']
            and 'passed' not in failing.stdout and passing.returncode == 0
            and passing.stdout.splitlines()[-1] == 'check-san: 2 test binaries passed'):
        print('lane binaries: a failing binary stops the lane; a passing list reports its count')
        return True
    print(f'lane binaries: FAIL got exits {failing.returncode} and {passing.returncode}')
    print(failing.stdout + passing.stdout, end='')
    return False


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
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    build = (ROOT / args.build_dir).resolve()
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='lane-fixtures-', dir=build) as directory:
        def fresh(name):
            repo = Path(directory) / name
            repo.mkdir()
            prepare(repo)
            return repo

        clean = run(fresh('clean'), 'check-layout')
        verdicts = [clean.returncode == 0 and not errors(clean)]
        if not verdicts[0]:
            print(f'clean checkout: FAIL check-layout got exit {clean.returncode}')
            print(clean.stdout, end='')
        for number, (name, mutate, targets, expected) in enumerate(cases(), 1):
            verdicts.append(check_case(fresh(f'case-{number}'), name, mutate, targets, expected))
        verdicts.append(check_lint_objects(fresh('lint-objects')))
        verdicts.append(check_lane_runs_every_binary(fresh('lane-binaries')))
        verdicts.append(check_ubsan_fatal(fresh('ubsan-fatal')))
    print(f'lane fixtures: {sum(verdicts)}/{len(verdicts)} passed')
    return int(not all(verdicts))


if __name__ == '__main__':
    raise SystemExit(main())
