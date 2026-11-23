#!/usr/bin/env python3
"""Check the suite inventory and the tools registry."""

import argparse
from collections import Counter
import os
from pathlib import Path, PurePosixPath
import re

from check_dag import ROOT


# Recognize the threaded suites named in docs/PLAN_V2.md §5 even when nested.
THREADED = ('work', 'piece', 'journal', 'savectl', 'font_async', 'hl',
            'find_session', 'app_session')
TSAN_NAMES = {f'{name}_test.c' for name in THREADED}


def finish(check, errors):
    for error in errors:
        print(f'{check}: error: {error}')
    if not errors:
        print(f'{check}: ok')
    return int(bool(errors))


MANIFEST = 'tests/lanes.mk'
# Lane name in the table -> make target that runs it. Each line of the table is
# also a make assignment (docs/decisions/v2-he7.26.md), so the grammar admits
# exactly the lines whose make meaning equals their table meaning: no '$', no
# '+=', no continuation, no other variable.
LANES = {'layers': 'check-layers', 'san': 'check-san', 'law2': 'check-law2',
         'tsan': 'check-tsan', 'fuzz': 'fuzz-smoke'}
# Lane -> the lane that must also run each of its suites (Makefile, check-san):
# an isolated layer link is no substitute for the sanitizers.
REQUIRED_OVERLAP = {'layers': 'san'}
LINE = re.compile(r'((?:tests/[\w./-]+_test|fuzz/[\w./-]+_fuzz)\.c)[ \t]+:=((?:[ \t]+\w+)+)[ \t]*')


def read_table(text, errors):
    """Return {suite: lanes} from the table, reporting every malformed line."""
    table = {}
    previous = None
    for number, line in enumerate(text.splitlines(), 1):
        where = f'{MANIFEST}:{number}'
        if not line.strip() or line.startswith('#'):
            continue
        match = LINE.fullmatch(line)
        if not match:
            errors.append(f'{where}: not a suite line: {line}')
            continue
        suite, lanes = match[1], match[2].split()
        if suite in table:
            errors.append(f'{where}: duplicate line for {suite}')
        elif previous is not None and suite < previous:
            errors.append(f'{where}: not sorted: {suite} after {previous}')
        previous = suite if previous is None else max(previous, suite)
        for lane, count in Counter(lanes).items():
            if lane not in LANES:
                errors.append(f'{where}: {suite}: unknown lane {lane}')
            if count > 1:
                errors.append(f'{where}: {suite}: names lane {lane} twice')
        table[suite] = set(lanes)  # make keeps the last assignment too
    return table


def check_suites(args):
    tests = set(args.tests_on_disk)
    fuzzers = set(args.fuzz_on_disk)
    derived = {'layers': args.layers, 'san': args.san, 'law2': args.law2,
               'tsan': args.tsan, 'fuzz': args.fuzz}
    errors = []
    try:
        text = (ROOT / MANIFEST).read_text(encoding='utf-8')
    except FileNotFoundError:
        return finish('check-suites', [f'missing manifest: {MANIFEST}'])
    table = read_table(text, errors)

    for lane, target in LANES.items():
        suites = sorted(suite for suite, lanes in table.items() if lane in lanes)
        on_disk = fuzzers if lane == 'fuzz' else tests
        if sorted(derived[lane]) != suites:
            errors.append(f'{target}: make derived a list that differs from {MANIFEST}')
        if not suites:
            errors.append(f'{target} requires at least one suite')
        for suite in suites:
            if not (ROOT / suite).is_file():
                errors.append(f'{target} listed suite is missing: {suite}')
            elif suite not in on_disk:
                errors.append(f'{target} lists a file outside its suite inventory: {suite}')

    for lane, also in REQUIRED_OVERLAP.items():
        for suite in sorted(s for s, lanes in table.items() if lane in lanes and also not in lanes):
            errors.append(f'{LANES[also]} must also run {LANES[lane]} suite {suite}')
    for suite in sorted(s for s, lanes in table.items() if 'fuzz' in lanes):
        corpus = Path('fuzz/corpus', suite.removeprefix('fuzz/').removesuffix('_fuzz.c'))
        if not readable_seed(ROOT / corpus):
            errors.append(f'fuzz-smoke corpus has no readable seed: {corpus}/ (commit minimal seeds)')

    if {s for s, lanes in table.items() if 'layers' in lanes} != set(args.required_layers):
        errors.append('check-layers must register exactly the L0–L4 isolated link suites')
    if not (ROOT / 'src/main.c').is_file():
        errors.append('check-layers listed suite is missing: src/main.c (L5 link entry point)')

    for suite in sorted((tests | fuzzers) - table.keys()):
        errors.append(f'unlisted suite: {suite} (add its line to {MANIFEST})')
    for suite in sorted(tests):
        lanes = table.get(suite, set())
        path = Path(suite)
        if (path.name.startswith(('law2_', 'g1_')) or {'law2', 'g1'}.intersection(path.parts[1:-1])):
            if 'law2' not in lanes:
                errors.append(f'Law-2/G1 suite must be registered in check-law2: {suite}')
        if path.name in TSAN_NAMES and 'tsan' not in lanes:
            errors.append(f'threaded suite must be registered in check-tsan: {suite}')
    return finish('check-suites', errors)


def readable_seed(corpus):
    for directory, _, files in os.walk(corpus):
        for name in files:
            path = Path(directory, name)
            try:
                if not path.is_symlink() and path.is_file():
                    with path.open('rb'):
                        return True
            except OSError:
                pass
    return False


def raise_walk_error(error):
    raise error


def files_in_tools(tools):
    for directory, subdirs, files in os.walk(tools, onerror=raise_walk_error):
        subdirs[:] = [name for name in subdirs if name != '__pycache__']
        for name in files:
            path = Path(directory, name).relative_to(ROOT).as_posix()
            if path != REGISTRY:
                yield path


def variants_dirs():
    for directory, subdirs, _ in os.walk(ROOT, onerror=raise_walk_error):
        if Path(directory) == ROOT:
            subdirs[:] = [name for name in subdirs if name not in {'.git', 'build', 'vendor'}]
        if 'variants' in subdirs:
            yield (Path(directory) / 'variants').relative_to(ROOT)
            subdirs.remove('variants')


REGISTRY = 'tools/README.md'
ROW = re.compile(r'\|\s*`([^`|]+)`\s*\|.*\|')
SEPARATOR = re.compile(r'\|(?:\s*:?-+:?\s*\|)+')


def registry_rows(text, errors):
    """Yield (line, path) for each data row; every row's first cell is one `path`."""
    table = [(number, line.strip()) for number, line in enumerate(text.splitlines(), 1)
             if line.lstrip().startswith('|')]
    if len(table) < 2 or not SEPARATOR.fullmatch(table[1][1]):
        errors.append(f'{REGISTRY}: expected one table with a header and separator row')
        return
    for number, line in table[2:]:
        match = ROW.fullmatch(line)
        if match:
            yield number, match[1]
        else:
            errors.append(f'{REGISTRY}:{number}: malformed row (first cell must be one `path`): {line}')


def registry_error(path):
    """Why a registered repo-relative path is not a canonical file in the checkout, or None."""
    pure = PurePosixPath(path)
    if pure.is_absolute() or str(pure) != path or '..' in pure.parts:
        return 'not a canonical repo-relative path'
    full = ROOT / path
    if full.resolve() != full:  # containment before existence: no symlink, no escape
        return 'resolves through a symlink'
    if not full.is_file():
        return 'registered file is missing'
    return None


def check_registry():
    errors = []
    rows = Counter()
    for number, path in registry_rows((ROOT / REGISTRY).read_text(encoding='utf-8'), errors):
        rows[path] += 1
        problem = registry_error(path)
        if problem:
            errors.append(f'{REGISTRY}:{number}: {problem}: {path}')
        elif rows[path] > 1:
            errors.append(f'{REGISTRY}:{number}: duplicate registry entry: {path}')
    for path in sorted(set(files_in_tools(ROOT / 'tools')) - rows.keys()):
        errors.append(f'unregistered file: {path}')
    for path in sorted(variants_dirs()):
        errors.append(f'forbidden directory: {path}/')
    return finish('check-registry', errors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    suites = commands.add_parser('suites')
    for name in ('required-layers', 'layers', 'san', 'law2', 'tsan', 'fuzz',
                 'tests-on-disk', 'fuzz-on-disk'):
        suites.add_argument(f'--{name}', nargs='*', required=True)
    commands.add_parser('registry')
    args = parser.parse_args()

    try:
        if args.command == 'suites':
            return check_suites(args)
        return check_registry()
    except (OSError, UnicodeError) as error:
        return finish('check-layout', [error])


if __name__ == '__main__':
    raise SystemExit(main())
