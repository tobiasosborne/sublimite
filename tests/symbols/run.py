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
# pseudo-module, docs/decisions/v2-he7.24.md), any other is src/MODULE. A
# rejected case lists every complete error line the checker must print (no
# more, no fewer; {a} is the case's archive directory). An accepted case names,
# per object, the imports it must still make, so a compiler that folds a call
# away cannot turn the case into a vacuous pass. A case with system_symbols
# audits against that file in its source directory.
Case = namedtuple('Case', 'name layers diagnostics imports source system_symbols',
                  defaults=((), {}, None, None))
CASES = (
    Case('intra', {3: ('x11', 'wayland')},
         ('{a}/layer3.a(src/x11/probe.o): error: [intra-layer] x11 (L3) references wayland_smoke '
          'from wayland (L3): dependency is outside INTRA_RULES',)),
    Case('forbidden', {3: ('x11',), 5: ('app',)},
         ('{a}/layer5.a(src/app/app.o): error: [forbidden] app (L5) references x11_smoke from x11 (L3)',)),
    Case('weak', {0: ('os',), 5: ('app',)},
         ('{a}/layer0.a(src/os/os.o): error: [weak] weak undefined symbol app_smoke (w)',
          '{a}/layer0.a(src/os/os.o): error: [weak] weak undefined symbol base_optional (v)')),
    Case('ldlibs', {0: ('os',), 5: ('app',)},
         ("{s}/src/os/LDLIBS: error: [ldlibs] '-Wl,--start-group': expected -lNAME for a system library",
          *(f"{{s}}/src/os/LDLIBS: error: [ldlibs] 'build/rel/{lib}.a': expected -lNAME for a system library"
            for lib in ('libapp', 'libui', 'libgfx_plat', 'libpersist', 'libtext')),
          "{s}/src/os/LDLIBS: error: [ldlibs] '-Wl,--end-group': expected -lNAME for a system library",
          '{a}/layer0.a(src/os/os.o): error: [inter-layer] os (L0) references app_smoke from app (L5): '
          'upward dependency')),
    Case('ldlibs_search', {0: ('os',)},
         tuple(f"{{s}}/src/os/LDLIBS: error: [ldlibs] '{token}': expected -lNAME for a system library"
               for token in ('-Lbuild/rel', '-lapp', '-l:libapp.a'))),
    Case('unknown', {0: ('os',)},
         ('{a}/layer0.a(src/os/os.o): error: [unknown] undefined symbol mystery_import has no known '
          'module prefix or system entry',)),
    Case('global', {0: ('os',)},
         ('{a}/layer0.a(src/os/os.o): error: [prefix] os defines stray_export without its own prefix',
          '{a}/layer0.a(src/os/os.o): error: [prefix] os defines base_wrong_owner without its own prefix')),
    Case('test_export', {0: ('os',)},
         ('{a}/layer0.a(src/os/os.o): error: [test] os ships test symbol os_test_probe',)),
    Case('clean', {0: ('os', 'base', 'trace'), 1: ('piece',)}, (), {'src/piece/piece.o': ('strlen',)}),
    Case('restricted', {1: ('piece',), 5: ('app',)},
         ('{a}/layer1.a(src/piece/piece.o): error: [restricted] piece imports read, granted only to os',
          '{a}/layer5.a(src/app/app.o): error: [restricted] app imports malloc, granted only to vendor_ts')),
    Case('granted', {0: ('os',), 4: ('vendor/tree-sitter/lib/src',)}, (),
         {'src/os/os.o': ('read', 'abort'), 'vendor/tree-sitter/lib/src/lib.o': ('abort',)}),
    Case('hl_heap', {4: ('hl',)},
         ('{a}/layer4.a(src/hl/hl.o): error: [restricted] hl imports malloc, granted only to vendor_ts',)),
    Case('vendor', {4: ('hl', 'vendor/tree-sitter/lib/src', 'vendor/tree-sitter-probe/src')}, (),
         {'vendor/tree-sitter/lib/src/lib.o': ('malloc', 'free'),
          'vendor/tree-sitter-probe/src/scanner.o': ('calloc',),
          'src/hl/hl.o': ('ts_probe_parser_new', 'tree_sitter_probe')}),
    Case('vendor_owner', {4: ('hl', 'vendor/tree-sitter/lib/src', 'vendor/tree-sitter-probe/src')},
         ('{a}/layer4.a(vendor/tree-sitter/lib/src/lib.o): error: [restricted] vendor_ts imports read, '
          'granted only to os',
          '{a}/layer4.a(vendor/tree-sitter-probe/src/scanner.o): error: [intra-layer] vendor_ts (L4) '
          'references hl_probe_callback from hl (L4): dependency is outside INTRA_RULES')),
    Case('pure', {1: ('piece',), 5: ('app',)}, (), {'src/piece/piece.o': ('memcpy',), 'src/app/app.o': ('strcmp',)}),
    Case('config_unknown_module', {0: ('os',)},
         ("error: [input] {s}/unknown_module.txt:2: read is granted to unknown module 'nosuchmod'",),
         source='config', system_symbols='unknown_module.txt'),
    Case('config_any_conflict', {0: ('os',)},
         ('error: [input] {s}/any_conflict.txt:3: read * duplicates or contradicts an earlier entry',),
         source='config', system_symbols='any_conflict.txt'),
    Case('config_duplicate', {0: ('os',)},
         ('error: [input] {s}/duplicate.txt:3: read os duplicates or contradicts an earlier entry',),
         source='config', system_symbols='duplicate.txt'),
    Case('config_no_reason', {0: ('os',)},
         ('error: [input] {s}/no_reason.txt:2: expected SYMBOL SCOPE # reason',),
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
        undefined = {}
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
            order = iter(members)  # nm lists members in archive order, one "archive[x.o]:" row each
            for row in symbols.splitlines():
                if row.endswith(']:'):
                    imports = undefined.setdefault(next(order).as_posix(), set())
                elif row.split()[1:2] == ['U']:
                    imports.add(row.split()[0])

        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        expected = sorted(line.format(a=build, s=source) for line in case.diagnostics)
        errors = sorted(line for line in result.stdout.splitlines() if 'error: ' in line)
        missing = sorted(f'{member}:{symbol}' for member, symbols in case.imports.items()
                         for symbol in set(symbols) - undefined.get(member, set()))
        if result.returncode == int(bool(expected)) and errors == expected and not missing:
            print(f'{case.name}: {"rejected" if expected else "accepted"} as expected')
        else:
            print(f'{case.name}: expected exit {int(bool(expected))} and exactly {expected}, '
                  f'got {result.returncode}; fixture imports missing: {missing}')
            print(result.stdout, end='')
            failures += 1
    return int(bool(failures))


if __name__ == '__main__':
    sys.exit(main())
