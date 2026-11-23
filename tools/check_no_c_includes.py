#!/usr/bin/env python3
"""Check that tests and fuzzers never include C implementation files.

Tests link the layer archives and use public headers; including a .c file
couples a test to private internals and hides upward dependencies. The caller
supplies the translation units (make check-includes passes the real-source
inventory; fixture runners pass their fixture's sources).
"""
import argparse
from pathlib import Path
import sys

from check_dag import ROOT, read_dependencies


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--root', type=Path, default=ROOT)
    ap.add_argument('--build-dir', type=Path, default=Path('build'))
    ap.add_argument('--tests', nargs='*', required=True, help='test sources (release objects)')
    ap.add_argument('--fuzz', nargs='*', required=True, help='fuzzer sources (fuzz objects)')
    args = ap.parse_args()
    root = args.root.resolve()
    errors = []
    for cfg, sources in (('rel', args.tests), ('fuzz', args.fuzz)):
        for source in sorted(map(Path, sources)):
            obj = args.build_dir / cfg / source.with_suffix('.o')
            try:
                dependencies = read_dependencies(root, obj, source)
            except (OSError, ValueError) as err:
                errors.append(f'{source}: error: {err}')
                continue
            resolved_source = (root / source).resolve()
            for path, resolved in dependencies:
                if resolved != resolved_source and '.c' in (path.suffix.lower(), resolved.suffix.lower()):
                    errors.append(f'{source}: error: reaches a .c file "{path}" '
                                  '(link the layer archive and include the public header instead)')
    for error in errors:
        print(error)
    if errors:
        print('check_no_c_includes: FAIL: %d violation(s)' % len(errors))
        return 1
    print(f'check_no_c_includes: ok ({len(args.tests) + len(args.fuzz)} test and fuzz translation units)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
