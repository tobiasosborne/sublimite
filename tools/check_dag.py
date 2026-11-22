#!/usr/bin/env python3
"""Include-DAG checker: inter-layer AND intra-layer module partial orders.

Layers come from mk/layers.mk (L<n>_MODS); L5 additionally owns src/main.c
(module "main"). Rules, all on quoted #include edges inside src/:

  1. Inter-layer: a module may include only modules of its own or a LOWER
     layer (L0 < L1 < ... < L5).
  2. Intra-layer: INTRA_RULES below declare "a < b" (b may include a, never
     the reverse). The relation is transitively closed; two modules of one
     layer that are not related must not include each other at all (this is
     what bans x11 <-> wayland, the v1 x11/clip.c <-> wayland/dispatch.c
     private.h cycle).
  3. FORBIDDEN: L5 (src/app, src/main.c) never includes x11/, wayland/, gl/
     or raster/ headers; it reaches L3 through plat/render public headers.
  4. Every src/<dir> must be assigned to a layer.

Exit 0 clean, 1 on violations (file:line diagnostics), 2 on a bad rule table.
Usage: check_dag.py [--root DIR]
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import clex  # noqa: E402

# Edit here (and in docs/PLAN_V2.md §2) when an order legitimately changes.
INTRA_RULES = [
    # L0 core
    "os < base < trace < work",
    "os < ipc",
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
FORBIDDEN = {            # includer module -> modules it must never include
    'app': ('x11', 'wayland', 'gl', 'raster'),
    'main': ('x11', 'wayland', 'gl', 'raster'),
}
EXTS = ('.c', '.h', '.inc')
QUOTED = re.compile(r'^[ \t]*#[ \t]*(?:include|include_next|import)[ \t]*"([^"\n]+)"', re.M)


def fail_config(msg):
    print('check_dag: config error: %s' % msg)
    sys.exit(2)


def read_layers(root):
    path = os.path.join(root, 'mk', 'layers.mk')
    if not os.path.isfile(path):
        fail_config('%s not found' % path)
    layer_of = {}
    for line in clex.read_text(path).splitlines():
        m = re.match(r'^L([0-9])_MODS\s*:=\s*([^#]*)', line)
        if not m:
            continue
        n = int(m.group(1))
        for mod in m.group(2).split():
            if mod in layer_of:
                fail_config('module %s listed in two layers' % mod)
            layer_of[mod] = n
    if not layer_of:
        fail_config('no L<n>_MODS lines found in %s' % path)
    if 'main' in layer_of:
        fail_config('"main" is implicit (src/main.c); do not list it in layers.mk')
    layer_of['main'] = MAIN_LAYER
    return layer_of


def build_order(layer_of):
    """Transitive closure of INTRA_RULES: set of (lower, upper)."""
    lt = set()
    for rule in INTRA_RULES:
        groups = [[x.strip() for x in g.split(',')] for g in rule.split('<')]
        for lo, hi in zip(groups, groups[1:]):
            for a in lo:
                for b in hi:
                    for x in (a, b):
                        if x not in layer_of:
                            fail_config('rule "%s": unknown module %s' % (rule, x))
                    if layer_of[a] != layer_of[b]:
                        fail_config('rule "%s": %s (L%d) and %s (L%d) are in different layers'
                                    % (rule, a, layer_of[a], b, layer_of[b]))
                    lt.add((a, b))
    changed = True
    while changed:
        changed = False
        for a, b in list(lt):
            for c, d in list(lt):
                if b == c and (a, d) not in lt:
                    lt.add((a, d))
                    changed = True
    for a, b in lt:
        if a == b:
            fail_config('cycle through %s in INTRA_RULES' % a)
    return lt


def module_of(src, path):
    rel = os.path.relpath(path, src).replace(os.sep, '/')
    if '/' not in rel:
        return 'main' if rel in ('main.c', 'main.h') else None
    return rel.split('/', 1)[0]


def resolve(src, includer, inc):
    """Module an include refers to, or None (system/vendor/unknown)."""
    for base in (os.path.dirname(includer), src):
        cand = os.path.normpath(os.path.join(base, inc))
        if os.path.isfile(cand):
            if os.path.commonpath([os.path.abspath(cand), os.path.abspath(src)]) == os.path.abspath(src):
                return module_of(src, cand)
            return None
    return inc.split('/', 1)[0] if '/' in inc else None   # header not written yet


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    src = os.path.join(root, 'src')
    layer_of = read_layers(root)
    lt = build_order(layer_of)

    errors = []
    nfiles = nedges = 0
    if not os.path.isdir(src):
        fail_config('%s not found' % src)
    for entry in sorted(os.listdir(src)):
        p = os.path.join(src, entry)
        if os.path.isdir(p) and entry not in layer_of:
            errors.append('src/%s/: error: directory is not assigned to a layer in mk/layers.mk' % entry)
        elif os.path.isfile(p) and entry.endswith(EXTS) and entry not in ('main.c', 'main.h'):
            errors.append('src/%s: error: stray file in src/ (only main.c lives outside a module)' % entry)

    for dirpath, dirnames, filenames in os.walk(src):
        dirnames.sort()
        for fn in sorted(filenames):
            if not fn.endswith(EXTS):
                continue
            path = os.path.join(dirpath, fn)
            me = module_of(src, path)
            if me not in layer_of:
                continue                       # already reported as unassigned
            nfiles += 1
            text = clex.blank_comments(clex.read_text(path))
            line_of = clex.line_mapper(text)
            rel = os.path.relpath(path, root)
            for m in QUOTED.finditer(text):
                inc = m.group(1)
                other = resolve(src, path, inc)
                if other is None or other == me or other not in layer_of:
                    continue
                nedges += 1
                where = '%s:%d: error:' % (rel, line_of(m.start()))
                who = '%s (L%d) includes "%s" (%s, L%d)' % (me, layer_of[me], inc, other, layer_of[other])
                if other in FORBIDDEN.get(me, ()):
                    errors.append('%s [forbidden] %s: L5 must reach L3 only through plat/render public headers'
                                  % (where, who))
                elif layer_of[other] > layer_of[me]:
                    errors.append('%s [inter-layer] %s: upward include (L%d may only include L0..L%d)'
                                  % (where, who, layer_of[me], layer_of[me]))
                elif layer_of[other] == layer_of[me] and (other, me) not in lt:
                    why = ('%s is declared above %s in this layer: downward-only' % (other, me)
                           if (me, other) in lt else
                           '%s and %s are unrelated: modules of one layer must not couple unless ordered' % (me, other))
                    errors.append('%s [intra-layer] %s: %s (see INTRA_RULES in tools/check_dag.py)'
                                  % (where, who, why))
    for e in errors:
        print(e)
    if errors:
        print('check_dag: FAIL: %d violation(s)' % len(errors))
        return 1
    print('check_dag: ok (%d files, %d cross-module includes, %d modules in %d layers)'
          % (nfiles, nedges, len(layer_of), len(set(layer_of.values()))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
