#!/usr/bin/env python3
"""Minimal C lexer shared by the structural linters (stdlib only).

Splits C source into code / comment / string / char / header tokens so the
linters never trip over `#include` text inside comments or string contents.
Not a preprocessor: it does not expand macros or follow includes.
"""
import bisect
import re

_INCLUDE_BEFORE = re.compile(r'^[ \t]*#[ \t]*(?:include|include_next|import)[ \t]*$')


def lex(text):
    """Return [(kind, start, end)] covering `text`; kinds: code, comment,
    string, char, header (the <...> of an #include)."""
    toks = []
    n = len(text)
    i = 0
    start = 0

    def flush(upto):
        if start < upto:
            toks.append(('code', start, upto))

    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ''
        if c == '/' and nxt == '/':
            flush(i)
            j = i + 2
            while j < n:
                if text[j] == '\n':
                    k = j - 1
                    if k > i + 1 and text[k] == '\r':
                        k -= 1
                    if k > i + 1 and text[k] == '\\':   # spliced line continues the comment
                        j += 1
                        continue
                    break
                j += 1
            toks.append(('comment', i, j))
            i = start = j
        elif c == '/' and nxt == '*':
            flush(i)
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            toks.append(('comment', i, j))
            i = start = j
        elif c == '"' or c == "'":
            flush(i)
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            if j < n and text[j] == c:
                j += 1
            else:
                j = min(j, n)
            toks.append(('string' if c == '"' else 'char', i, j))
            i = start = j
        elif c == '<' and _INCLUDE_BEFORE.match(text[text.rfind('\n', 0, i) + 1:i]):
            flush(i)
            e = text.find('>', i)
            nl = text.find('\n', i)
            j = i + 1 if e < 0 or (0 <= nl < e) else e + 1
            toks.append(('header', i, j))
            i = start = j
        else:
            i += 1
    flush(n)
    return toks


def blank_comments(text):
    """`text` with every comment replaced by spaces (newlines kept), so line
    numbers and columns are unchanged."""
    out = []
    for kind, a, b in lex(text):
        seg = text[a:b]
        out.append(re.sub(r'[^\n]', ' ', seg) if kind == 'comment' else seg)
    return ''.join(out)


def line_mapper(text):
    """Return f(offset) -> 1-based line number."""
    nls = [m.start() for m in re.finditer('\n', text)]
    return lambda off: bisect.bisect_left(nls, off) + 1


def read_text(path):
    with open(path, encoding='utf-8', errors='replace') as fh:
        return fh.read()
