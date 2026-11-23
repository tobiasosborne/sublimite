#!/usr/bin/env python3
"""Build archives from the Makefile's fixture objects and check each verdict."""

import argparse
from collections import namedtuple
from pathlib import Path
import shlex
import subprocess
import sys


FIXTURES = Path('tests/symbols')
CHECKER = Path('tools/check_symbols.py')
# Each layer's module tuple also gives the member order passed to ar; an
# entry with a '/' is a directory of vendored sources (the vendor_ts
# pseudo-module, docs/decisions/v2-he7.24.md), any other is src/MODULE. An
# accepted case names the imports its objects must still make, so a compiler
# that folds the call away cannot turn the case into a vacuous pass. A case
# with system_symbols audits against that file in its source directory.
Case = namedtuple('Case', 'name layers diagnostics imports source system_symbols',
                  defaults=((), None, None))
CASES = (
    Case('intra', {3: ('x11', 'wayland')}, ('[intra-layer]', 'wayland_smoke')),
    Case('forbidden', {3: ('x11',), 5: ('app',)}, ('[forbidden]', 'x11_smoke')),
    Case('weak', {0: ('os',), 5: ('app',)}, ('[weak]', 'app_smoke (w)', 'base_optional (v)')),
    Case('ldlibs', {0: ('os',), 5: ('app',)},
         ('[ldlibs]', '-Wl,--start-group', 'build/rel/libapp.a', '[inter-layer]', 'app_smoke')),
    Case('ldlibs_search', {0: ('os',)}, ('[ldlibs]', '-Lbuild/rel', '-lapp', '-l:libapp.a')),
    Case('unknown', {0: ('os',)}, ('[unknown]', 'mystery_import')),
    Case('global', {0: ('os',)}, ('[prefix]', 'stray_export', 'base_wrong_owner')),
    Case('test_export', {0: ('os',)}, ('[test]', 'os_test_probe')),
    Case('clean', {0: ('os', 'base', 'trace'), 1: ('piece',)}, (), ('strlen',)),
    Case('restricted', {1: ('piece',), 5: ('app',)},
         ('[restricted] piece imports read, granted only to os',
          '[restricted] app imports malloc, granted only to vendor_ts')),
    Case('granted', {0: ('os',), 4: ('vendor/tree-sitter/lib/src',)}, (), ('read', 'fdopen')),
    Case('hl_heap', {4: ('hl',)}, ('[restricted] hl imports malloc, granted only to vendor_ts',)),
    Case('vendor', {4: ('hl', 'vendor/tree-sitter/lib/src', 'vendor/tree-sitter-probe/src')}, (),
         ('malloc', 'free', 'calloc', 'ts_probe_parser_new', 'tree_sitter_probe')),
    Case('vendor_owner', {4: ('hl', 'vendor/tree-sitter/lib/src', 'vendor/tree-sitter-probe/src')},
         ('lib/src/lib.o): error: [restricted] vendor_ts imports read, granted only to os',
          'scanner.o): error: [intra-layer] vendor_ts (L4) references hl_probe_callback from hl (L4)')),
    Case('pure', {1: ('piece',), 5: ('app',)}, (), ('memcpy', 'strcmp')),
    Case('config_unknown_module', {0: ('os',)}, ('[input]', ":2: read is granted to unknown module 'nosuchmod'"),
         source='config', system_symbols='unknown_module.txt'),
    Case('config_any_conflict', {0: ('os',)}, ('[input]', ':3: read * duplicates or contradicts'),
         source='config', system_symbols='any_conflict.txt'),
    Case('config_duplicate', {0: ('os',)}, ('[input]', ':3: read os duplicates or contradicts'),
         source='config', system_symbols='duplicate.txt'),
    Case('config_no_reason', {0: ('os',)}, ('[input]', ':2: expected SYMBOL SCOPE # reason'),
         source='config', system_symbols='no_reason.txt'),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--ar', required=True)
    parser.add_argument('--nm', default='nm')
    parser.add_argument('--layer-libs', nargs='+', required=True)
    parser.add_argument('--module-map', nargs='+', required=True)
    args = parser.parse_args()

    failures = 0
    for case in CASES:
        source = FIXTURES / (case.source or case.name)
        build = args.build / 'rel' / source
        command = [sys.executable, str(CHECKER), '--nm', args.nm,
                   '--ldlibs-dir', str(source / 'src'), '--layer-libs', *args.layer_libs,
                   '--module-map', *args.module_map]
        if case.system_symbols:
            command.extend(['--system-symbols', str(source / case.system_symbols)])
        undefined = set()
        for layer, modules in case.layers.items():
            members = [path.relative_to(source).with_suffix('.o')
                       for module in modules
                       for path in sorted((source / ('' if '/' in module else 'src') / module).glob('*.c'))]
            if not members:
                raise ValueError(f'{case.name}: no fixture objects for L{layer}')
            archive = build / f'layer{layer}.a'
            archive.unlink(missing_ok=True)
            subprocess.run([*shlex.split(args.ar), 'rcs', archive,
                            *(build / member for member in members)], check=True)
            command.extend(['--archive', str(layer), str(archive), *map(str, members)])
            symbols = subprocess.run([*shlex.split(args.nm), '-g', '-P', '--', archive],
                                     check=True, capture_output=True, text=True).stdout
            undefined.update(row.split()[0] for row in symbols.splitlines()
                             if row.split()[1:2] == ['U'])

        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        expected = int(bool(case.diagnostics))
        missing = sorted(set(case.imports) - undefined)
        if (result.returncode == expected and not missing
                and all(word in result.stdout for word in case.diagnostics)):
            print(f'{case.name}: {"rejected" if case.diagnostics else "accepted"} as expected')
        else:
            print(f'{case.name}: expected exit {expected} and {case.diagnostics}, '
                  f'got {result.returncode}; fixture imports missing: {missing}')
            print(result.stdout, end='')
            failures += 1
    return int(bool(failures))


if __name__ == '__main__':
    sys.exit(main())
