#!/usr/bin/env python3
"""Require a cross-module release reference or a filed reservation for each archive export.

Vendored code (vendor_ts) is exempt from the export-usage gate because the
project preserves the upstream library's public surface. Its references count.
The Make target leaves piece, undo and journal unenforced during their rewrites.
"""

import argparse
from collections import Counter, defaultdict
from pathlib import Path
import re
import shlex
import sys

from check_symbols import (SYMBOL_NAME, UNDEFINED, VENDOR_TS, archive_members, nm_lines,
                           object_module, object_symbols, read_module_map)


ROOT = Path(__file__).resolve().parents[1]
RESERVATIONS = ROOT / 'tools' / 'export-reservations.txt'
FILED_ID = re.compile(r'[A-Za-z0-9][A-Za-z0-9_.-]*')


def read_reservations(path):
    reservations = {}
    documents = {}
    for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        entry, _, reason = line.partition('#')
        fields = entry.split()
        if not fields:
            continue
        where = f'{path}:{number}'
        if len(fields) != 2 or not reason.strip():
            raise ValueError(f'{where}: expected SYMBOL BEAD-OR-BRIEF # reason')
        symbol, filed = fields
        if not SYMBOL_NAME.fullmatch(symbol) or not FILED_ID.fullmatch(filed):
            raise ValueError(f'{where}: invalid symbol or filed identifier')
        if filed not in documents:
            paths = [ROOT / 'docs' / directory / f'{filed}.md'
                     for directory in ('briefs', 'decisions')]
            documents[filed] = [doc.read_text(encoding='utf-8') for doc in paths if doc.is_file()]
        if not documents[filed]:
            raise ValueError(f'{where}: no brief or decision named {filed}.md')
        name = rf'(?<![A-Za-z0-9_]){re.escape(symbol)}(?![A-Za-z0-9_])'
        if not any(re.search(name, document) for document in documents[filed]):
            raise ValueError(f'{where}: {filed}.md does not name {symbol}')
        if symbol in reservations:
            raise ValueError(f'{where}: duplicate reservation for {symbol}')
        reservations[symbol] = where
    return reservations


def release_symbols(nm, archives, main, layers):
    exports = {}
    references = defaultdict(set)
    for layer_id, path, *objects in archives:
        layer, archive = int(layer_id), Path(path)
        for member, rows in archive_members(nm, archive, list(map(Path, objects))):
            where = f'{archive}({member})'
            module = object_module(member)
            if layers.get(module) != layer:
                raise ValueError(f'{where}: {module} is not an archive member of L{layer}')
            for symbol, kind in object_symbols(rows, where):
                if kind in UNDEFINED:
                    references[symbol].add(module)
                else:
                    exports.setdefault((module, symbol), where)
    for symbol, kind in object_symbols(nm_lines(nm, main), main):
        if kind in UNDEFINED:
            references[symbol].add('main')
    return exports, references


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', action='append', nargs='+', required=True,
                        metavar='ARG', help='LAYER ARCHIVE [ORDERED_OBJECT ...]')
    parser.add_argument('--main', type=Path, required=True, help='loose release entry-point object')
    parser.add_argument('--module-map', nargs='+', required=True, metavar='LAYER:MODULE')
    parser.add_argument('--module', action='append', default=[],
                        help='enforce this module (repeatable) and count other violations')
    parser.add_argument('--nm', default='nm')
    parser.add_argument('--reservations', type=Path, default=RESERVATIONS)
    args = parser.parse_args()

    try:
        nm = shlex.split(args.nm)
        if not nm:
            raise ValueError('expected an nm command')
        layers = read_module_map(args.module_map)
        for module in args.module:
            if module == 'main' or module not in layers:
                raise ValueError(f'unknown archive module {module!r}')
        reservations = read_reservations(args.reservations)
        exports, references = release_symbols(nm, args.archive, args.main, layers)
    except (OSError, ValueError) as error:
        print(f'check_exports: error: [input] {error}', file=sys.stderr)
        return 1

    errors = []
    exported_names = {symbol for _, symbol in exports}
    for symbol, where in reservations.items():
        if symbol not in exported_names:
            errors.append(f'{where}: error: [reservation] {symbol} is not exported')

    other = Counter()
    for (module, symbol), where in exports.items():
        if module == VENDOR_TS or symbol in reservations or references[symbol] - {module}:
            continue
        if args.module and module not in args.module:
            other[module] += 1
        else:
            errors.append(f'{where}: error: [export] {symbol} has no cross-module reference or reservation')

    for error in errors:
        print(error, file=sys.stderr)
    if other:
        counts = ', '.join(f'{module}={count}' for module, count in other.items())
        print(f'check_exports: out-of-scope violations: {counts}')
    if errors:
        print(f'check_exports: FAIL ({len(errors)} violations)', file=sys.stderr)
        return 1
    scope = ', '.join(args.module) or 'all project modules'
    print(f'check_exports: ok ({scope}, {len(args.archive)} archives)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
