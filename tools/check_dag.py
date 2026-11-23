#!/usr/bin/env python3
"""Check layer and module include orders using compiler dependency files.

Every translation unit under src/ may reach files in its own module, in a
lower layer, or in a module ordered below it in the same layer. L5 may never
reach x11/, wayland/, gl/, or raster/. Every src/ subdirectory needs a layer.

Exit 0 clean, 1 on violations, 2 on a bad layer or module table.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

# Edit here (and in docs/PLAN_V2.md §2) when an order legitimately changes.
INTRA_RULES = [
    # L0 core
    "os < base < trace < work",
    "base < ipc",  # amended: docs/decisions/wave0-plan-amendments.md
    # L1 text
    "scan,utf8 < piece < undo,lineidx,indent,find",
    # L2 persist
    "file < journal < savectl",
    "journal < edit",
    # L3 gfx_plat: x11 and wayland are unrelated; neither may see the other.
    "plat < x11,wayland",
    "render < raster,gl,font",
    "plat < raster,gl",
    # L4 ui (findui is a single module: its internal file order is not a module order)
    "cmd < config",
    "viewport < layout < tabs",
    # L5
    "app < main",
]
MAIN_LAYER = 5
# The modules L5 (app and main) may never reach; check_symbols.py shares it.
L5_BACKENDS = ('x11', 'wayland', 'gl', 'raster')
EXTS = ('.c', '.h', '.inc')
MAKE_WORD = re.compile(r'(?:\\[ \t#]|[^\s])+')
MAKE_ESCAPE = re.compile(r'\\([ \t#])|\$\$')


def fail_config(msg):
    print('check_dag: config error: %s' % msg)
    sys.exit(2)


def read_layers(root, assignments):
    if assignments is None:
        try:
            result = subprocess.run(['make', '--no-print-directory', '-s', 'print-layer-map'],
                                    cwd=root, capture_output=True, text=True, check=True)
        except subprocess.CalledProcessError as err:
            fail_config('make print-layer-map failed: %s' % err.stderr.strip())
        except OSError as err:
            fail_config(str(err))
        assignments = result.stdout.split()
    try:
        return parse_layers(assignments)
    except ValueError as err:
        fail_config(err)


def parse_layers(assignments):
    """{module: layer} from L<n>:<module> words, plus the implicit main in L5.

    The one module map for check_dag.py and check_symbols.py."""
    layer_of = {}
    for assignment in assignments:
        m = re.fullmatch(r'L([0-5]):([^:\s]+)', assignment)
        if not m:
            raise ValueError('invalid layer assignment %r' % assignment)
        n, mod = m.groups()
        if mod in layer_of:
            raise ValueError('module %s listed in two layers' % mod)
        layer_of[mod] = int(n)
    if not layer_of:
        raise ValueError('no module assignments from mk/layers.mk')
    if 'main' in layer_of:
        raise ValueError('"main" is implicit (src/main.c); do not list it in layers.mk')
    layer_of['main'] = MAIN_LAYER
    return layer_of


def build_order(layer_of, rules=INTRA_RULES):
    """Transitive closure of the intra-layer rules: set of (lower, upper)."""
    lt = set()
    for rule in rules:
        groups = [[x.strip() for x in g.split(',')] for g in rule.split('<')]
        for lo, hi in zip(groups, groups[1:]):
            for a in lo:
                for b in hi:
                    for x in (a, b):
                        if x not in layer_of:
                            raise ValueError('rule "%s": unknown module %s' % (rule, x))
                    if layer_of[a] != layer_of[b]:
                        raise ValueError('rule "%s": %s (L%d) and %s (L%d) are in different layers'
                                         % (rule, a, layer_of[a], b, layer_of[b]))
                    lt.add((a, b))
    while True:
        closure = lt | {(a, d) for a, b in lt for c, d in lt if b == c}
        if closure == lt:
            break
        lt = closure
    for a, b in lt:
        if a == b:
            raise ValueError('cycle through %s in the intra-layer rules' % a)
    return lt


def translation_units(root, directory):
    return sorted(path.relative_to(root) for path in (root / directory).rglob('*.c')
                  if path.is_file())


def make_paths(words):
    return [Path(MAKE_ESCAPE.sub(lambda m: m[1] or '$', word))
            for word in MAKE_WORD.findall(words)]


def read_dependencies(root, obj, source):
    """Return prerequisites as (compiler path, resolved path) pairs."""
    dep = obj.with_suffix('.d')
    try:
        stamp = (root / dep).stat().st_mtime_ns
        # -MP adds empty rules after the first logical line.
        data = (root / dep).read_text(encoding='utf-8', errors='surrogateescape')
    except FileNotFoundError:
        raise ValueError(f'missing dependency file {dep}') from None

    rule = data.replace('\\\n', ' ').partition('\n')[0]
    targets, _, prerequisites = rule.partition(':')
    paths = make_paths(prerequisites)
    full_source = root / source
    if [root / target for target in make_paths(targets)] != [root / obj] or not any(
            root / path == full_source for path in paths):
        raise ValueError(f'invalid dependency file {dep} for {source}')

    dependencies = []
    for path in paths:
        full_path = root / path
        try:
            if full_path.stat().st_mtime_ns > stamp:
                raise ValueError(f'stale dependency file {dep} (older than {path})')
        except FileNotFoundError:
            raise ValueError(f'dependency {path} from {dep} is missing') from None
        if full_path != full_source:
            dependencies.append((path, full_path.resolve()))
    return dependencies


def module_of(path):
    if len(path.parts) > 1:
        return path.parts[0]
    return 'main' if path.name in ('main.c', 'main.h') else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--root', type=Path, default=ROOT)
    ap.add_argument('--build-dir', type=Path, default=Path('build'))
    ap.add_argument('--layers', nargs='*', metavar='LAYER:MODULE')
    args = ap.parse_args()
    root = args.root.resolve()
    src = root / 'src'
    layer_of = read_layers(root, args.layers)
    try:
        lt = build_order(layer_of)
    except ValueError as err:
        fail_config(err)

    errors = []
    nfiles = nedges = 0
    if not src.is_dir():
        fail_config('%s not found' % src)
    for entry in sorted(src.iterdir()):
        if entry.is_dir() and entry.name not in layer_of:
            errors.append(f'src/{entry.name}/: error: directory is not assigned to a layer in mk/layers.mk')
        elif entry.is_file() and entry.suffix in EXTS and not module_of(Path(entry.name)):
            errors.append(f'src/{entry.name}: error: stray file in src/ (only main belongs outside a module)')

    src = src.resolve()
    for source in translation_units(root, 'src'):
        me = module_of(source.relative_to('src'))
        if me not in layer_of:
            continue  # already reported as unassigned
        nfiles += 1
        where = f'{source}: error:'
        obj = args.build_dir / 'rel' / source.with_suffix('.o')
        try:
            dependencies = read_dependencies(root, obj, source)
        except (OSError, ValueError) as err:
            errors.append(f'{where} {err}')
            continue

        for inc in sorted({resolved.relative_to(src) for _, resolved in dependencies
                           if resolved.is_relative_to(src)}):
            other = module_of(inc)
            if other == me:
                continue
            if other not in layer_of:
                errors.append(f'{where} reaches src/{inc}, which has no assigned layer')
                continue
            nedges += 1
            who = f'{me} (L{layer_of[me]}) reaches "src/{inc}" ({other}, L{layer_of[other]})'
            if layer_of[me] == MAIN_LAYER and other in L5_BACKENDS:
                errors.append(f'{where} [forbidden] {who}: L5 may not reach backend headers')
            elif layer_of[other] > layer_of[me]:
                errors.append(f'{where} [inter-layer] {who}: upward include')
            elif layer_of[other] == layer_of[me] and (other, me) not in lt:
                errors.append(f'{where} [intra-layer] {who}: {other} is not below {me} in INTRA_RULES')
    for e in errors:
        print(e)
    if errors:
        print('check_dag: FAIL: %d violation(s)' % len(errors))
        return 1
    print('check_dag: ok (%d translation units, %d cross-module dependencies, %d modules in %d layers)'
          % (nfiles, nedges, len(layer_of), len(set(layer_of.values()))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
