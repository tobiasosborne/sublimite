#!/usr/bin/env python3
"""Exercise the isolation lint with one independent verdict per spelling."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
IDENTITY = 'APP_ID := sublimite-v2\n'
CLEAN = '''/* "sublimite" is the v1 leaf. */
const char *ok = EDIT_APP_ID "/config.toml";
const char *socket = "\\0" EDIT_APP_ID "-%lu";
const char *words[] = {"sub", "limite"};
// A spliced line comment continues \\
   "sublimite" on its next physical line, as a CRLF splice does \\\r
   "sublimite" too (finding 23: clex reads despliced, newline-normalized text).
'''

LITERAL = 'identity in string literal {!r}'
REDEFINED = 'EDIT_APP_ID is defined by the build'
# F9 and F10 in docs/reviews/wave0-scaffold-muse.md, plus controls. Each
# spelling must print exactly its own diagnostic (line and reason), and its
# near neighbour (the first NEAR substitution: one character or name away from
# the identity) must pass.
SOURCES = {
    'P20a hex escape': (r'const char *a = "sublim\x69te";', 1, LITERAL.format('sublimite')),
    'P20a2 escaped slash': (r'const char *a2 = "\x2fsublimite";', 1, LITERAL.format('/sublimite')),
    'P20b relative directory': ('const char *b = "sublimite/config.toml";', 1,
                                LITERAL.format('sublimite/config.toml')),
    'P20c abstract socket': (r'const char *c = "\0sublimite";', 1, LITERAL.format('\0sublimite')),
    'P20d underscore suffix': ('const char *d = "sublimite_sock";', 1, LITERAL.format('sublimite_sock')),
    'P20d2 dot prefix': ('const char *d2 = "org.sublimite";', 1, LITERAL.format('org.sublimite')),
    'P20h spliced adjacency': ('const char *h = "sublim" \\\n"ite";', 1, LITERAL.format('sublimite')),
    'P20e stringification': ('#define STR_(x) #x\n#define STR(x) STR_(x)\nconst char *e = STR(sublimite);',
                             3, "identity identifier 'sublimite' can be stringified"),
    'P20f character array': (r"const char f[] = {'s','u','b','l','i','m','i','t','e','\0'};", 1,
                             'identity assembled from character literals'),
    'P20g formatted pieces': ('#include <stdio.h>\nvoid assemble(char *buf, size_t n) '
                              '{ snprintf(buf, n, "%s%s", "sub", "limite"); }',
                              2, 'identity assembled from formatted string arguments'),
    'bare leaf': ('const char *path = "sublimite";', 1, LITERAL.format('sublimite')),
    'literal after a line comment': ('// ends here\nconst char *path = "sublimite";', 2,
                                     LITERAL.format('sublimite')),
    'absolute path': ('const char *path = "/home/x/.config/sublimite/config.toml";', 1,
                      LITERAL.format('/home/x/.config/sublimite/config.toml')),
    'adjacent strings': ('const char *path = "sub" /* next piece */ "limite";', 1, LITERAL.format('sublimite')),
    'hardcoded v2 leaf': ('const char *path = "sublimite-v2";', 1, LITERAL.format('sublimite-v2')),
    'octal escape': (r'const char *path = "sublim\151te";', 1, LITERAL.format('sublimite')),
    'spliced literal': ('const char *path = "sublim\\\nite";', 1, LITERAL.format('sublimite')),
    'prefixed strings': ('const char *path = u8"sub" u8"limite";', 1, LITERAL.format('sublimite')),
    'local undefinition': ('#undef /* identity is build-defined */ EDIT_APP_ID', 1, REDEFINED),
    'local definition': ('%:define EDIT_APP_ID "other-editor"', 1, REDEFINED),
}
NEAR = (('limite', 'limate'), (r'\x69te', r'\x61te'), (r'\151te', r'\141te'), ('\\\nite', '\\\nate'),
        ('"ite"', '"ate"'), ("'i','t','e'", "'a','t','e'"), ('EDIT_APP_ID', 'EDIT_APP_NAME'))


def near_neighbour(snippet):
    old, new = next(pair for pair in NEAR if pair[0] in snippet)
    return snippet.replace(old, new)


MAKEFILE_RULE = 'Makefile:1: error: expected exactly one literal APP_ID := sublimite-v2 assignment'
CLEAN_RUN = 'check_isolation: ok (1 files under src/)'


def expect(name, command, code, lines, **kwargs):
    """Require the exit code and exactly these output lines."""
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, check=False, **kwargs)
    if result.returncode == code and result.stdout.splitlines() == lines:
        print(f'{name}: ok')
        return True
    print(f'{name}: FAIL expected exit {code} and {lines!r}, got {result.returncode}')
    print(result.stdout, end='')
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    args = parser.parse_args()
    build = ROOT / args.build_dir
    build.mkdir(parents=True, exist_ok=True)

    verdicts = []
    with tempfile.TemporaryDirectory(prefix='isolation-fixtures-', dir=build) as tmp:
        root = Path(tmp)
        source = root / 'src' / 'probe.c'
        source.parent.mkdir()
        makefile = root / 'Makefile'
        makefile.write_text(IDENTITY, encoding='utf-8')
        lint = [sys.executable, '-B', ROOT / 'tools/check_isolation.py', '--root', root]
        for name, (snippet, line, reason) in SOURCES.items():
            source.write_text(snippet + '\n', encoding='utf-8')
            verdicts.append(expect(name, lint, 1, [
                f'src/probe.c:{line}: error: {reason}; build app-identity paths from EDIT_APP_ID',
                'check_isolation: FAIL: 1 violation(s)']))
            source.write_text(near_neighbour(snippet) + '\n', encoding='utf-8')
            verdicts.append(expect(f'{name} (near neighbour)', lint, 0, [CLEAN_RUN]))
        source.write_text(CLEAN, encoding='utf-8')
        verdicts.append(expect('clean', lint, 0, [CLEAN_RUN]))
        makefile.write_text('APP_ID := sublimite\n', encoding='utf-8')
        verdicts.append(expect('wrong Makefile APP_ID', lint, 1, [MAKEFILE_RULE]))
        makefile.write_text('# APP_ID must be defined below\n', encoding='utf-8')
        verdicts.append(expect('missing Makefile APP_ID', lint, 1, [MAKEFILE_RULE]))

    env = os.environ.copy()
    for var in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'MAKEFILES'):
        env.pop(var, None)
    guard = (ROOT / 'Makefile').read_text(encoding='utf-8').splitlines().index(
        '$(error APP_ID must be sublimite-v2)') + 1
    make = ['make', '--no-print-directory', '-n', 'APP_ID=sublimite', 'all']
    verdicts.append(expect('P28 command-line APP_ID', make, 2,
                           [f'Makefile:{guard}: *** APP_ID must be sublimite-v2.  Stop.'], env=env))
    print(f'isolation fixtures: {sum(verdicts)}/{len(verdicts)} passed')
    return int(not all(verdicts))


if __name__ == '__main__':
    sys.exit(main())
