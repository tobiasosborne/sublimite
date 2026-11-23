#!/usr/bin/env python3
"""Check the suite inventory, tools registry and real test/fuzzer includes."""

import argparse
from collections import Counter
import os
from pathlib import Path
import re

from check_dag import ROOT, read_dependencies


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


def check_suites(args):
    tests = set(args.tests_on_disk)
    fuzzers = set(args.fuzz_on_disk)
    lanes = (
        ('check-layers', args.layers, None, tests),
        ('check-san', args.san, None, tests),
        ('check-law2', args.law2, args.law2_empty_wave, tests),
        ('check-tsan', args.tsan, args.tsan_empty_wave, tests),
        ('fuzz-smoke', args.fuzz, args.fuzz_empty_wave, fuzzers),
    )
    errors = []
    if not (ROOT / 'tests/lanes.mk').is_file():
        errors.append('missing manifest: tests/lanes.mk')
    for lane, suites, wave, on_disk in lanes:
        if not suites and wave is None:
            errors.append(f'{lane} requires at least one suite')
        elif not suites and not re.fullmatch(r'[1-9][0-9]*', wave):
            errors.append(f'{lane} has no suites and no first-wave declaration (positive wave number required)')
        elif suites and wave:
            errors.append(f'{lane} declares suites and a first-wave marker; remove the marker')
        for suite, count in Counter(suites).items():
            if count > 1:
                errors.append(f'{lane} lists a suite twice: {suite}')
            if not (ROOT / suite).is_file():
                errors.append(f'{lane} listed suite is missing: {suite}')
            elif suite not in on_disk:
                errors.append(f'{lane} lists a file outside its suite inventory: {suite}')

    if set(args.layers) != set(args.required_layers):
        errors.append('check-layers must register exactly the L0–L4 isolated link suites')
    if not (ROOT / 'src/main.c').is_file():
        errors.append('check-layers listed suite is missing: src/main.c (L5 link entry point)')

    registered = {suite for _, suites, _, _ in lanes for suite in suites}
    for suite in sorted((tests | fuzzers) - registered):
        errors.append(f'unlisted suite: {suite}')
    for suite in sorted(tests):
        path = Path(suite)
        if (path.name.startswith(('law2_', 'g1_')) or {'law2', 'g1'}.intersection(path.parts[1:-1])):
            if suite not in args.law2:
                errors.append(f'Law-2/G1 suite must be registered in check-law2: {suite}')
        if path.name in TSAN_NAMES and suite not in args.tsan:
            errors.append(f'threaded suite must be registered in check-tsan: {suite}')
    return finish('check-suites', errors)


def raise_walk_error(error):
    raise error


def files_in_tools(tools):
    for directory, subdirs, files in os.walk(tools, onerror=raise_walk_error):
        subdirs[:] = [name for name in subdirs if name != '__pycache__']
        for name in files:
            path = Path(directory, name).relative_to(tools).as_posix()
            if path != 'README.md':
                yield path


def variants_dirs():
    for directory, subdirs, _ in os.walk(ROOT, onerror=raise_walk_error):
        if Path(directory) == ROOT:
            subdirs[:] = [name for name in subdirs if name not in {'.git', 'build', 'vendor'}]
        if 'variants' in subdirs:
            yield (Path(directory) / 'variants').relative_to(ROOT)
            subdirs.remove('variants')


def check_registry():
    tools = ROOT / 'tools'
    registry = (tools / 'README.md').read_text(encoding='utf-8')
    entries = Counter(re.findall(r'^\s*\|\s*`([^`\n]+)`\s*\|', registry, re.MULTILINE))
    files = set(files_in_tools(tools))
    errors = []
    for name in sorted(files - entries.keys()):
        errors.append(f'unregistered file: tools/{name}')
    for name, count in sorted(entries.items()):
        # A row naming a path with a slash registers a repo-relative tool outside
        # tools/ (e.g. tests/lint/run.py); a bare name is a file under tools/.
        if '/' in name:
            if not (ROOT / name).is_file():
                errors.append(f'registered file is missing: {name}')
        elif name not in files or not (tools / name).is_file():
            errors.append(f'registered file is missing: tools/{name}')
        if count > 1:
            errors.append(f'duplicate registry entry: {name}')
    for path in sorted(variants_dirs()):
        errors.append(f'forbidden directory: {path}/')
    return finish('check-registry', errors)


def check_includes(args):
    errors = []
    for cfg, sources in (('rel', args.tests), ('fuzz', args.fuzz)):
        for source in sorted(map(Path, sources)):
            obj = args.build_dir / cfg / source.with_suffix('.o')
            try:
                dependencies = read_dependencies(ROOT, obj, source)
            except (OSError, ValueError) as error:
                errors.append(f'{source}: {error}')
                continue
            actual_source = (ROOT / source).resolve()
            for path, resolved in dependencies:
                if resolved != actual_source and '.c' in (path.suffix.lower(), resolved.suffix.lower()):
                    errors.append(f'{source}: reaches a .c file "{path}" '
                                  '(link the layer archive and include the public header instead)')
    return finish('check-no-c-includes', errors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    suites = commands.add_parser('suites')
    for name in ('required-layers', 'layers', 'san', 'law2', 'tsan', 'fuzz',
                 'tests-on-disk', 'fuzz-on-disk'):
        suites.add_argument(f'--{name}', nargs='*', required=True)
    for name in ('law2', 'tsan', 'fuzz'):
        suites.add_argument(f'--{name}-empty-wave', required=True)
    commands.add_parser('registry')
    includes = commands.add_parser('includes')
    includes.add_argument('--build-dir', type=Path, required=True)
    includes.add_argument('--tests', nargs='*', required=True)
    includes.add_argument('--fuzz', nargs='*', required=True)
    args = parser.parse_args()

    try:
        if args.command == 'suites':
            return check_suites(args)
        if args.command == 'registry':
            return check_registry()
        return check_includes(args)
    except (OSError, UnicodeError) as error:
        return finish('check-layout', [error])


if __name__ == '__main__':
    raise SystemExit(main())
