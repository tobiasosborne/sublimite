#!/usr/bin/env python3
"""Check the release archive symbol DAG, per-module system imports and LDLIBS entries."""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

from check_dag import INTRA_RULES


ALLOWLIST = Path(__file__).resolve().with_name('system-symbols.txt')
ANY_MODULE = '*'
UNDEFINED = frozenset('Uwv')
L5_FORBIDDEN = {'x11', 'wayland', 'gl', 'raster'}
# The vendored tree-sitter runtime and grammars form one pseudo-module: no
# src/ directory, linked into hl's archive, ordered below hl and above
# nothing (docs/decisions/v2-he7.24.md). It is implicit, like main.
VENDOR_TS = 'vendor_ts'
TS_RUNTIME = Path('vendor/tree-sitter/lib/src/lib.o')
TS_RUNTIME_EXPORTS = {'_ts_dup', 'range_intersects', 'range_within'}
# These definitions must use the libc ABI names to interpose at link time.
BASE_ALLOCATOR_HOOK = Path('src/base/malloc_guard.o')
BASE_ALLOCATOR_EXPORTS = {'malloc', 'calloc', 'realloc'}
SYSTEM_LIBRARY = re.compile(r'-l[A-Za-z0-9][A-Za-z0-9_.+-]*')
SYMBOL_NAME = re.compile(r'[A-Za-z0-9_.$@]+')


def read_module_map(entries):
    layers = {}
    for entry in entries:
        layer, _, module = entry.partition(':')
        if not re.fullmatch(r'L[0-9]+', layer) or not module or module in layers:
            raise ValueError(f'invalid or duplicate module map entry {entry!r}')
        layers[module] = int(layer[1:])
    if 'app' not in layers or 'hl' not in layers or {'main', VENDOR_TS} & layers.keys():
        raise ValueError(f'module map must include app and hl and leave main and {VENDOR_TS} implicit')
    layers['main'] = layers['app']
    layers[VENDOR_TS] = layers['hl']
    return layers


def intra_order(layers):
    order = set()
    for rule in INTRA_RULES:
        groups = [[module.strip() for module in group.split(',')] for group in rule.split('<')]
        for lower, upper in zip(groups, groups[1:]):
            for a in lower:
                for b in upper:
                    if a not in layers or b not in layers or layers[a] != layers[b]:
                        raise ValueError(f'invalid intra-layer rule {rule!r}')
                    order.add((a, b))
    order.add((VENDOR_TS, 'hl'))
    while True:
        closure = order | {(a, d) for a, b in order for c, d in order if b == c}
        if closure == order:
            break
        order = closure
    if any(a == b for a, b in order):
        raise ValueError('cycle in INTRA_RULES')
    return order


def vendor_symbol(symbol):
    return symbol.startswith(('ts_', 'tree_sitter_')) or symbol in TS_RUNTIME_EXPORTS


def symbol_module(symbol, layers):
    if symbol == 'main':
        return 'main'
    if vendor_symbol(symbol):
        return VENDOR_TS
    prefix, _, _ = symbol.rpartition('_')
    while prefix:
        if prefix in layers and len(symbol) > len(prefix) + 1:
            return prefix
        prefix, _, _ = prefix.rpartition('_')
    return None


def read_allowlist(path, layers):
    """Map each system symbol to its scopes: {'*'} or the modules granted it."""
    scopes = {}
    for number, line in enumerate(path.read_text(encoding='ascii').splitlines(), 1):
        entry, _, reason = line.partition('#')
        fields = entry.split()
        if not fields:
            continue
        where = f'{path}:{number}'
        if len(fields) != 2 or not reason.strip():
            raise ValueError(f'{where}: expected SYMBOL SCOPE # reason')
        name, scope = fields
        if not SYMBOL_NAME.fullmatch(name) or symbol_module(name, layers):
            raise ValueError(f'{where}: invalid or project symbol {name!r}')
        if scope != ANY_MODULE and scope not in layers:
            raise ValueError(f'{where}: {name} is granted to unknown module {scope!r}')
        held = scopes.setdefault(name, set())
        # A '*' entry is its symbol's only entry; each module grant appears once.
        if held and (ANY_MODULE in held | {scope} or scope in held):
            raise ValueError(f'{where}: {name} {scope} duplicates or contradicts an earlier entry')
        held.add(scope)
    return scopes


def ldlibs_errors(directory, layer_libs):
    reserved = set()
    for library in layer_libs:
        match = re.fullmatch(r'lib(.+)\.a', library)
        if not match:
            raise ValueError(f'invalid layer archive name {library!r}')
        reserved.add('-l' + match[1])
    errors = []
    for module_dir in sorted(directory.iterdir()):
        if not module_dir.is_dir():
            continue
        path = module_dir / 'LDLIBS'
        try:
            path.lstat()
        except FileNotFoundError:
            continue
        try:
            if path.is_symlink() or not path.is_file():
                raise ValueError('expected a regular file')
            contents = path.read_bytes().decode('ascii')
        except (OSError, ValueError) as error:
            errors.append(f'{path}: error: [ldlibs] {error}')
            continue
        for token in re.split(r'[ \t\n]+', contents.strip(' \t\n')):
            if token and (not SYSTEM_LIBRARY.fullmatch(token) or token in reserved):
                errors.append(f'{path}: error: [ldlibs] {token!r}: expected -lNAME for a system library')
    return errors


def nm_lines(nm, path):
    result = subprocess.run([*nm, '-g', '-P', '--', str(path)], capture_output=True, text=True,
                            env={**os.environ, 'LC_ALL': 'C'})
    # nm can return success after skipping a bad archive member.
    if result.returncode or result.stderr:
        raise ValueError(f'{path}: nm failed: {result.stderr.strip() or result.returncode}')
    return result.stdout.splitlines()


def archive_members(nm, archive, members):
    rows = []
    for line in nm_lines(nm, archive):
        if line.endswith(']:'):
            index = len(rows)
            if index == len(members):
                raise ValueError(f'{archive}: unexpected archive member {line!r}')
            expected = f'{archive}[{members[index].name}]:'
            if line != expected:
                raise ValueError(f'{archive}: expected member {expected!r}, got {line!r}')
            rows.append([])
        elif rows:
            rows[-1].append(line)
        else:
            raise ValueError(f'{archive}: nm output precedes the first archive member')
    if len(rows) != len(members):
        raise ValueError(f'{archive}: expected {len(members)} members, nm reported {len(rows)}')
    return zip(members, rows)


def object_module(path):
    if path.suffix == '.o' and '..' not in path.parts:
        if len(path.parts) >= 3 and path.parts[0] == 'src' and path.parts[1] != VENDOR_TS:
            return path.parts[1]
        if path == TS_RUNTIME or (len(path.parts) >= 4 and path.parts[0] == 'vendor'
                                  and path.parts[1].startswith('tree-sitter-')):
            return VENDOR_TS
    raise ValueError(f'unrecognized object path {path}')


def owns_symbol(member, module, owner, symbol):
    if member == BASE_ALLOCATOR_HOOK and symbol in BASE_ALLOCATOR_EXPORTS:
        return True
    if member == TS_RUNTIME:
        return symbol.startswith('ts_') or symbol in TS_RUNTIME_EXPORTS
    if member.parts[0] == 'vendor':
        return symbol.startswith('tree_sitter_')
    return owner == module


def audit_object(rows, where, member, module, layers, order, allowlist):
    layer = layers[module]
    errors = []
    for row in rows:
        fields = row.split()
        if len(fields) < 2 or not SYMBOL_NAME.fullmatch(fields[0]) or not re.fullmatch('[A-Za-z]', fields[1]):
            raise ValueError(f'{where}: malformed nm row {row!r}')
        symbol, kind = fields[:2]
        if (kind in UNDEFINED and len(fields) != 2) or (kind not in UNDEFINED and len(fields) not in (3, 4)):
            raise ValueError(f'{where}: malformed nm row {row!r}')
        owner = symbol_module(symbol, layers)
        violation = None
        if kind in {'w', 'v'}:
            violation = f'[weak] weak undefined symbol {symbol} ({kind})'
        elif kind != 'U':
            if not owns_symbol(member, module, owner, symbol):
                violation = f'[prefix] {module} defines {symbol} without its own prefix'
        elif symbol in allowlist:
            if not allowlist[symbol] & {ANY_MODULE, module}:
                granted = ', '.join(sorted(allowlist[symbol]))
                violation = f'[restricted] {module} imports {symbol}, granted only to {granted}'
        elif owner is None or owner not in layers:
            violation = f'[unknown] undefined symbol {symbol} has no known module prefix or system entry'
        else:
            edge = f'{module} (L{layer}) references {symbol} from {owner} (L{layers[owner]})'
            if layer == layers['app'] and owner in L5_FORBIDDEN:
                violation = f'[forbidden] {edge}'
            elif layers[owner] > layer:
                violation = f'[inter-layer] {edge}: upward dependency'
            elif layers[owner] == layer and owner != module and (owner, module) not in order:
                violation = f'[intra-layer] {edge}: dependency is outside INTRA_RULES'
        if violation:
            errors.append(f'{where}: error: {violation}')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', action='append', nargs='+', default=[],
                        metavar='ARG', help='LAYER ARCHIVE [ORDERED_OBJECT ...]')
    parser.add_argument('--main', type=Path, help='loose release entry-point object')
    parser.add_argument('--module-map', nargs='+', default=[], metavar='LAYER:MODULE')
    parser.add_argument('--layer-libs', nargs='+', required=True)
    parser.add_argument('--nm', default='nm')
    parser.add_argument('--ldlibs-dir', type=Path, default=Path('src'))
    parser.add_argument('--ldlibs-only', action='store_true')
    parser.add_argument('--system-symbols', type=Path, default=ALLOWLIST)
    args = parser.parse_args()
    errors = []
    try:
        errors.extend(ldlibs_errors(args.ldlibs_dir, args.layer_libs))
        if not args.ldlibs_only:
            nm = shlex.split(args.nm)
            if not nm or not args.archive:
                raise ValueError('expected an nm command and at least one archive')
            layers = read_module_map(args.module_map)
            order = intra_order(layers)
            allowlist = read_allowlist(args.system_symbols, layers)
            for specification in args.archive:
                layer_id, path, *objects = specification
                layer, archive = int(layer_id), Path(path)
                try:
                    for member, rows in archive_members(nm, archive, list(map(Path, objects))):
                        where = f'{archive}({member})'
                        module = object_module(member)
                        if module == 'main' or layers.get(module) != layer:
                            raise ValueError(f'{where}: {module} is not an archive member of L{layer}')
                        errors.extend(audit_object(rows, where, member, module, layers, order, allowlist))
                except (OSError, ValueError) as error:
                    errors.append(f'error: [input] {error}')
            if args.main:
                member = Path('src/main.o')
                errors.extend(audit_object(nm_lines(nm, args.main), args.main, member,
                                           'main', layers, order, allowlist))
    except (OSError, ValueError) as error:
        errors.append(f'error: [input] {error}')
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    if not args.ldlibs_only:
        print(f'check_symbols: ok ({len(args.archive)} archives plus LDLIBS)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
