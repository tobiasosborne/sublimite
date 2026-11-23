#!/usr/bin/env python3
"""Validate the v2 build identity and reject hardcoded identities in src/."""

import argparse
from bisect import bisect_right
import os
from pathlib import Path
import re
import sys
from typing import NamedTuple

import clex

LEGACY_ID = 'sublimite'
APP_ID = 'sublimite-v2'
EXTS = ('.c', '.h', '.inc')
MAKE_DEF = re.compile(r' *(?:(?:override|export|private)[ \t]+)*'
                      r'(?:APP_ID[ \t]*(?::+|[+?!])?=|define[ \t]+APP_ID\b)')
CODE = re.compile(r'\w+|%:|[^\s]')
ESCAPE = re.compile(r'\\(x[0-9a-fA-F]+|[0-7]{1,3}|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|.)')
SIMPLE_ESCAPES = {
    '\\': '\\', '"': '"', "'": "'", '?': '?',
    'a': '\a', 'b': '\b', 'f': '\f', 'n': '\n', 'r': '\r', 't': '\t', 'v': '\v',
}
STRING_FORMAT = re.compile(r'(?:[^%]|%s|%%)*')


class Token(NamedTuple):
    kind: str
    value: str
    offset: int


def build_id_ok(makefile):
    definitions = [(line, text) for line, text in enumerate(clex.read_text(makefile).splitlines(), 1)
                   if MAKE_DEF.match(text)]
    if len(definitions) == 1:
        _, text = definitions[0]
        if re.fullmatch(r' *APP_ID[ \t]*:=[ \t]*' + re.escape(APP_ID), text):
            return True
    line = definitions[0][0] if definitions else 1
    print(f'Makefile:{line}: error: expected exactly one literal APP_ID := {APP_ID} assignment')
    return False


def desplice(text):
    """Remove C line splices and keep physical line numbers for diagnostics."""
    parts, offsets = [], []
    start, size = 0, 0
    for match in re.finditer(r'\\\n', text):
        parts.append(text[start:match.start()])
        size += len(parts[-1])
        offsets.append(size)
        start = match.end()
    parts.append(text[start:])
    joined = ''.join(parts)
    line_of = clex.line_mapper(joined)
    return joined, lambda offset: line_of(offset) + bisect_right(offsets, offset)


def decode_escape(match):
    escape = match[1]
    if escape[0] in 'xuU' and len(escape) > 1:
        value = int(escape[1:], 16)
    elif escape[0] in '01234567':
        value = int(escape, 8)
    else:
        return SIMPLE_ESCAPES.get(escape, match[0])
    return chr(value) if value <= sys.maxunicode else match[0]


def source_tokens(text):
    tokens = []
    for kind, start, end in clex.lex(text):
        if kind == 'code':
            tokens.extend(Token(kind, match[0], match.start())
                          for match in CODE.finditer(text, start, end))
        elif kind in ('string', 'char'):
            quote = '"' if kind == 'string' else "'"
            body_start = text.index(quote, start, end) + 1
            body_end = end - 1 if text[end - 1] == quote else end
            value = ESCAPE.sub(decode_escape, text[body_start:body_end])
            if kind == 'string' and tokens and tokens[-1].kind == 'string':
                previous = tokens.pop()
                tokens.append(Token(kind, previous.value + value, previous.offset))
            else:
                tokens.append(Token(kind, value, start))
        elif kind == 'header':
            tokens.append(Token(kind, '', start))
    return tokens


def initializer_identity(tokens, start):
    chars = []
    for i in range(start, len(tokens) - 1, 2):
        char, separator = tokens[i:i + 2]
        if char.kind != 'char' or len(char.value) != 1:
            break
        if separator[:2] not in (('code', ','), ('code', '}')):
            break
        chars.append(char.value)
        if separator.value == '}':
            break
    return LEGACY_ID in ''.join(chars)


def formatted_identity(tokens, i, fmt):
    if '%s' not in fmt or not STRING_FORMAT.fullmatch(fmt):
        return False
    parts = re.split(r'(%s|%%)', fmt)
    count = parts[1::2].count('%s')
    if not count:
        return False
    end = i + 2 * count + 1
    if end >= len(tokens) or tokens[end][:2] not in (('code', ','), ('code', ')')):
        return False
    if any(token[:2] != ('code', ',') for token in tokens[i + 1:end:2]):
        return False
    args = tokens[i + 2:end:2]
    if any(token.kind != 'string' for token in args):
        return False
    values = iter(token.value.split('\0', 1)[0] for token in args)
    for j in range(1, len(parts), 2):
        parts[j] = next(values) if parts[j] == '%s' else '%'
    return LEGACY_ID in ''.join(parts)


def violations(tokens):
    for i, (kind, value, offset) in enumerate(tokens):
        if kind == 'string':
            if LEGACY_ID in value:
                yield offset, f'identity in string literal {value!r}'
            elif formatted_identity(tokens, i, value):
                yield offset, 'identity assembled from formatted string arguments'
        elif kind == 'code':
            if value == LEGACY_ID:
                yield offset, f'identity identifier {value!r} can be stringified'
            elif value == '{' and initializer_identity(tokens, i + 1):
                yield offset, 'identity assembled from character literals'
            elif value in ('#', '%:') and i + 2 < len(tokens):
                directive, name = tokens[i + 1:i + 3]
                if (directive[:2] in (('code', 'define'), ('code', 'undef'))
                        and name[:2] == ('code', 'EDIT_APP_ID')):
                    yield offset, 'EDIT_APP_ID is defined by the build'


def scan_sources(root):
    bad = files = 0
    for dirpath, dirnames, filenames in os.walk(root / 'src', onerror=raise_walk_error):
        dirnames.sort()
        for fn in sorted(filenames):
            if not fn.endswith(EXTS):
                continue
            path = Path(dirpath) / fn
            files += 1
            text, line_of = desplice(clex.read_text(path))
            for offset, reason in violations(source_tokens(text)):
                print(f'{path.relative_to(root)}:{line_of(offset)}: error: {reason}; '
                      'build app-identity paths from EDIT_APP_ID')
                bad += 1
    return bad, files


def raise_walk_error(exc):
    raise exc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    root = parser.parse_args().root
    try:
        if not build_id_ok(root / 'Makefile'):
            return 1
        bad, files = scan_sources(root)
    except OSError as exc:
        print(f'check_isolation: FAIL: {exc}')
        return 1
    if not files:
        print('check_isolation: FAIL: no C sources under src/')
        return 1
    if bad:
        print(f'check_isolation: FAIL: {bad} violation(s)')
        return 1
    print(f'check_isolation: ok ({files} files under src/)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
