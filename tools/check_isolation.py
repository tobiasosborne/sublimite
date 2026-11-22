#!/usr/bin/env python3
"""v2 namespace isolation: no hardcoded v1 app identity in src/.

The app identity is injected as EDIT_APP_ID ("sublimite-v2", set by the
Makefile). Any string literal in src/ that spells the identity itself
("/sublimite...", "sublimite.", "sublimite-", or the bare leaf "sublimite")
could point v2 at v1's XDG config/data/runtime paths or IPC socket, so it is
rejected. Comments are ignored; adjacent literals are concatenated first so
"sub" "limite-x" style splits do not hide a path.
Usage: check_isolation.py [--root DIR]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import clex  # noqa: E402

NEEDLES = ('/sublimite', 'sublimite.', 'sublimite-')
EXTS = ('.c', '.h', '.inc')


def literals(text):
    """Yield (start_offset, content) of string literals, merging adjacent ones."""
    toks = [t for t in clex.lex(text) if t[0] != 'comment']
    cur = None
    for kind, a, b in toks:
        if kind == 'string':
            body = text[a + 1:b - 1] if text[b - 1:b] == '"' and b - a >= 2 else text[a + 1:b]
            cur = (cur[0], cur[1] + body) if cur else (a, body)
        elif kind == 'code' and not text[a:b].strip():
            continue                      # whitespace between literals keeps them adjacent
        else:
            if cur:
                yield cur
            cur = None
    if cur:
        yield cur


def bad_reason(lit):
    for n in NEEDLES:
        if n in lit:
            return 'contains "%s"' % n
    if lit == 'sublimite':
        return 'is the bare v1 leaf "sublimite"'
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    args = ap.parse_args()
    bad = files = 0
    for dirpath, dirnames, filenames in os.walk(os.path.join(args.root, 'src')):
        dirnames.sort()
        for fn in sorted(filenames):
            if not fn.endswith(EXTS):
                continue
            path = os.path.join(dirpath, fn)
            files += 1
            text = clex.read_text(path)
            line_of = clex.line_mapper(text)
            for off, lit in literals(text):
                why = bad_reason(lit)
                if why:
                    print('%s:%d: error: string literal "%s" %s; build app-identity paths from EDIT_APP_ID '
                          '(v2 must never hit v1 XDG/config/IPC paths)'
                          % (os.path.relpath(path, args.root), line_of(off), lit, why))
                    bad += 1
    if bad:
        print('check_isolation: FAIL: %d violation(s)' % bad)
        return 1
    print('check_isolation: ok (%d files under src/)' % files)
    return 0


if __name__ == '__main__':
    sys.exit(main())
