#!/usr/bin/env python3
"""Source-only guard: hidden Windows tests must not enter pointer/input loops.

Scan all test C sources, headers and include fragments (a conservative superset
of Windows inputs), including unreferenced headers waiting to be integrated.
Hazards belong in a function whose FIRST statement is LIVE_ONLY_NATIVE_LOOP.
The macro queries native isolation and returns with a counted SKIP. Comments
and literals are ignored; this is a review guard, not a full C preprocessor.
Optional paths let coordinators check incoming sources without executing them.
"""
import argparse
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
GATE = "LIVE_ONLY_NATIVE_LOOP"
GATE_DEFINITION = '''#define LIVE_ONLY_NATIVE_LOOP(name, ...) do { \\
    if (test_isolation_query() != TEST_ISOLATION_LIVE) { \\
        test_skip(name, "needs live desktop: native move/size loop moves the real pointer"); \\
        return __VA_ARGS__; \\
    } \\
} while (0)'''
HAZARDS = re.compile(r"\b(?:SC_MOVE|SC_SIZE|SC_KEYMENU|SetCursorPos|SetPhysicalCursorPos|"
                     r"ClipCursor|mouse_event|SendInput|WM_NCLBUTTONDOWN|DragDetect|"
                     r"TrackPopupMenu(?:Ex)?|BlockInput|SetCapture|SetForegroundWindow|"
                     r"SwitchDesktop)\b")
LEXEMES = re.compile(r'/\*.*?\*/|//[^\n]*|L?"(?:\\.|[^"\\])*"|L?\'(?:\\.|[^\'\\])*\'', re.S)


def mask(source):
    return LEXEMES.sub(lambda m: re.sub(r"[^\n]", " ", m[0]), source)


def gate_is_valid(header):
    # Ignore commented-out definitions, preserve the actual reason literal,
    # and reject duplicate definitions/undefs even in the trusted header.
    header = LEXEMES.sub(lambda m: " " * len(m[0]) if m[0].startswith(("/*", "//")) else m[0], header)
    directives = re.findall(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", header, re.M)
    definitions = [d for d in directives if re.match(r"#\s*(?:define|undef)\s+" + GATE + r"\b", d.lstrip())]
    return len(definitions) == 1 and re.sub(r"\s+", "", definitions[0]) == re.sub(r"\s+", "", GATE_DEFINITION)


def gated_regions(code):
    """Only accept a gate at the start of a top-level function body."""
    original = code
    # Directives can contain braces; they are never function definitions.
    code = re.sub(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", lambda m: " " * len(m[0]), code,
                  flags=re.M)
    stack, regions = [], []
    for match in re.finditer(r"[{}]", code):
        pos = match.start()
        if match[0] == "{":
            is_function = not stack and code[:pos].rstrip().endswith(")")
            # Literals were masked: require a literal name and an empty/zero
            # return value, never a return expression with input side effects.
            gated = is_function and re.match(r"\s*" + GATE + r"\s*\(\s*(?:,\s*0\s*)?\)\s*;",
                                             original[pos+1:])
            stack.append((pos, bool(gated)))
        elif stack:
            start, gated = stack.pop()
            if gated:
                regions.append((start, pos))
    return regions


def violations(source):
    code = mask(source)
    regions = gated_regions(code)
    hits = [(m.start(), m[0]) for m in HAZARDS.finditer(code)]
    # Alt+Space reaches the same native system menu without spelling SC_KEYMENU.
    hits += [(m.start(), "Alt+Space native menu") for m in
             re.finditer(r"\bWM_SYS(?:KEYDOWN|CHAR)\b[^;{}]*\bVK_SPACE\b", code)]
    hits += [(m.start(), "Alt release native menu") for m in
             re.finditer(r"\bWM_SYSKEYUP\b[^;{}]*\bVK_MENU\b", code)]
    return [(source.count("\n", 0, pos)+1, name) for pos, name in sorted(hits)
            if not any(start < pos < end for start, end in regions)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path)
    args = parser.parse_args()
    header = (ROOT / "tests/win/test_isolation.h").read_text()
    failed = not gate_is_valid(header)
    if failed:
        print("tests/win/test_isolation.h: missing/changed native isolation gate")
    paths = args.paths or sorted(p for p in (ROOT / "tests").rglob("*")
                                if p.suffix in (".c", ".h", ".inc"))
    for path in paths:
        source = path.read_text()
        label = path.relative_to(ROOT) if path.is_relative_to(ROOT) else path
        for line, name in violations(source):
            print(f"{label}:{line}: {name} requires {GATE} as the function's first statement")
            failed = True
        # There must be only one definition of the trusted gate.
        if path.resolve() != (ROOT / "tests/win/test_isolation.h") and re.search(
                r"^\s*#\s*(?:define|undef)\s+" + GATE + r"\b", mask(source), re.M):
            print(f"{label}: must not redefine/undefine {GATE}")
            failed = True
    if not failed:
        print("Windows pointer guard: PASS")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
