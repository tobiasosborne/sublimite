#!/usr/bin/env python3
"""Build archives from the Makefile's fixture objects and check each verdict."""

import argparse
from pathlib import Path
import shlex
import subprocess
import sys


FIXTURES = Path('tests/symbols')
CHECKER = Path('tools/check_symbols.py')
# Each layer's module tuple also gives the member order passed to ar.
CASES = (
    ('intra', {3: ('x11', 'wayland')}, ('[intra-layer]', 'wayland_smoke')),
    ('forbidden', {3: ('x11',), 5: ('app',)}, ('[forbidden]', 'x11_smoke')),
    ('weak', {0: ('os',), 5: ('app',)}, ('[weak]', 'app_smoke (w)', 'base_optional (v)')),
    ('ldlibs', {0: ('os',), 5: ('app',)},
     ('[ldlibs]', '-Wl,--start-group', 'build/rel/libapp.a', '[inter-layer]', 'app_smoke')),
    ('ldlibs_search', {0: ('os',)}, ('[ldlibs]', '-Lbuild/rel', '-lapp', '-l:libapp.a')),
    ('unknown', {0: ('os',)}, ('[unknown]', 'mystery_import')),
    ('global', {0: ('os',)}, ('[prefix]', 'stray_export', 'base_wrong_owner')),
    ('clean', {0: ('os', 'base', 'trace'), 1: ('piece',)}, ()),
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
    for name, layers, diagnostics in CASES:
        source = FIXTURES / name
        build = args.build / 'rel' / source
        command = [sys.executable, str(CHECKER), '--nm', args.nm,
                   '--ldlibs-dir', str(source / 'src'), '--layer-libs', *args.layer_libs,
                   '--module-map', *args.module_map]
        for layer, modules in layers.items():
            members = [path.relative_to(source).with_suffix('.o')
                       for module in modules
                       for path in sorted((source / 'src' / module).glob('*.c'))]
            if not members:
                raise ValueError(f'{name}: no fixture objects for L{layer}')
            archive = build / f'layer{layer}.a'
            archive.unlink(missing_ok=True)
            subprocess.run([*shlex.split(args.ar), 'rcs', archive,
                            *(build / member for member in members)], check=True)
            command.extend(['--archive', str(layer), str(archive), *map(str, members)])

        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        expected = int(bool(diagnostics))
        if result.returncode == expected and all(word in result.stdout for word in diagnostics):
            print(f'{name}: {"rejected" if diagnostics else "accepted"} as expected')
        else:
            print(f'{name}: expected exit {expected} and {diagnostics}, got {result.returncode}')
            print(result.stdout, end='')
            failures += 1
    return int(bool(failures))


if __name__ == '__main__':
    sys.exit(main())
