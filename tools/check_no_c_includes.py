#!/usr/bin/env python3
"""Check that tests and fuzzers never include C implementation files.

Tests link the layer archives and use public headers; including a .c file
couples a test to private internals and hides upward dependencies.
"""
import argparse
from pathlib import Path
import sys

from check_dag import ROOT, read_dependencies, translation_units


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--root', type=Path, default=ROOT)
    ap.add_argument('--build-dir', type=Path, default=Path('build'))
    args = ap.parse_args()
    root = args.root.resolve()
    bad = files = 0
    for directory, cfg in (('tests', 'rel'), ('fuzz', 'fuzz')):
        for source in translation_units(root, directory):
            files += 1
            obj = args.build_dir / cfg / source.with_suffix('.o')
            try:
                dependencies = read_dependencies(root, obj, source)
            except (OSError, ValueError) as err:
                print(f'{source}: error: {err}')
                bad += 1
                continue

            resolved_source = (root / source).resolve()
            for path, resolved in dependencies:
                if resolved != resolved_source and '.c' in (path.suffix.lower(), resolved.suffix.lower()):
                    print(f'{source}: error: reaches a .c file "{path}" '
                          '(link the layer archive and include the public header instead)')
                    bad += 1
    if bad:
        print('check_no_c_includes: FAIL: %d violation(s)' % bad)
        return 1
    print(f'check_no_c_includes: ok ({files} translation units under tests/fuzz)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
