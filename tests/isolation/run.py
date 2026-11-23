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
'''

# F9 and F10 in docs/reviews/wave0-scaffold-muse.md, plus controls.
SOURCES = {
    'P20a hex escape': r'const char *a = "sublim\x69te";',
    'P20a2 escaped slash': r'const char *a2 = "\x2fsublimite";',
    'P20b relative directory': 'const char *b = "sublimite/config.toml";',
    'P20c abstract socket': r'const char *c = "\0sublimite";',
    'P20d underscore suffix': 'const char *d = "sublimite_sock";',
    'P20d2 dot prefix': 'const char *d2 = "org.sublimite";',
    'P20h spliced adjacency': 'const char *h = "sublim" \\\n"ite";',
    'P20e stringification': '#define STR_(x) #x\n#define STR(x) STR_(x)\n'
                             'const char *e = STR(sublimite);',
    'P20f character array': r"const char f[] = {'s','u','b','l','i','m','i','t','e','\0'};",
    'P20g formatted pieces': '''#include <stdio.h>
void assemble(char *buf, size_t n) { snprintf(buf, n, "%s%s", "sub", "limite"); }
''',
    'bare leaf': 'const char *path = "sublimite";',
    'absolute path': 'const char *path = "/home/x/.config/sublimite/config.toml";',
    'adjacent strings': 'const char *path = "sub" /* next piece */ "limite";',
    'hardcoded v2 leaf': 'const char *path = "sublimite-v2";',
    'octal escape': r'const char *path = "sublim\151te";',
    'spliced literal': 'const char *path = "sublim\\\nite";',
    'prefixed strings': 'const char *path = u8"sub" u8"limite";',
    'local undefinition': '#undef /* identity is build-defined */ EDIT_APP_ID',
    'local definition': '%:define EDIT_APP_ID "other-editor"',
}


def expect(name, command, code, diagnostic, **kwargs):
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, check=False, **kwargs)
    if result.returncode == code and diagnostic in result.stdout:
        print(f'{name}: ok')
        return True
    print(f'{name}: FAIL expected exit {code} and {diagnostic!r}, got {result.returncode}')
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
        for name, snippet in SOURCES.items():
            source.write_text(snippet + '\n', encoding='utf-8')
            verdicts.append(expect(name, lint, 1, 'src/probe.c:'))
        source.write_text(CLEAN, encoding='utf-8')
        verdicts.append(expect('clean', lint, 0, 'check_isolation: ok'))
        makefile.write_text('APP_ID := sublimite\n', encoding='utf-8')
        verdicts.append(expect('wrong Makefile APP_ID', lint, 1, 'Makefile:'))
        makefile.write_text('# APP_ID must be defined below\n', encoding='utf-8')
        verdicts.append(expect('missing Makefile APP_ID', lint, 1, 'Makefile:'))

    env = os.environ.copy()
    for var in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'MAKEFILES'):
        env.pop(var, None)
    verdicts.append(expect('P28 command-line APP_ID', ['make', '-n', 'APP_ID=sublimite', 'all'],
                           2, 'APP_ID must be sublimite-v2', env=env))
    print(f'isolation fixtures: {sum(verdicts)}/{len(verdicts)} passed')
    return int(not all(verdicts))


if __name__ == '__main__':
    sys.exit(main())
