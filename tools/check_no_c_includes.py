#!/usr/bin/env python3
"""Fail on any `#include` that targets a .c file inside tests/ or fuzz/.

Tests link the layer archives and use public headers; including a .c file
couples a test to private internals and hides upward dependencies.
Usage: check_no_c_includes.py [--root DIR]
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import clex  # noqa: E402

INC = re.compile(r'^[ \t]*#[ \t]*(?:include|include_next|import)[ \t]*[<"]([^>"\n]+)[>"]', re.M)
EXTS = ('.c', '.h', '.inc')
DIRS = ('tests', 'fuzz')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    args = ap.parse_args()
    bad = files = 0
    for d in DIRS:
        for dirpath, dirnames, filenames in os.walk(os.path.join(args.root, d)):
            dirnames.sort()
            for fn in sorted(filenames):
                if not fn.endswith(EXTS):
                    continue
                path = os.path.join(dirpath, fn)
                files += 1
                text = clex.blank_comments(clex.read_text(path))
                line_of = clex.line_mapper(text)
                for m in INC.finditer(text):
                    if m.group(1).lower().endswith('.c'):
                        rel = os.path.relpath(path, args.root)
                        print('%s:%d: error: #include of a .c file: "%s" '
                              '(link the layer archive and include the public header instead)'
                              % (rel, line_of(m.start()), m.group(1)))
                        bad += 1
    if bad:
        print('check_no_c_includes: FAIL: %d violation(s)' % bad)
        return 1
    print('check_no_c_includes: ok (%d files under %s)' % (files, '/'.join(DIRS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
