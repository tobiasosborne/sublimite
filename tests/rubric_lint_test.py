#!/usr/bin/env python3
"""edit-cxho.22: lint docs/design/rubric.md.

Checks: criteria C1..C10 each have a heading and anchors 1, 3 and 5; every
number in an anchor carries an evidence tag (P)/(M)/(E)/(G) or a pointer to a
derivation (CLAUDE.md law 4); no other editor is named as a reference (Tobias
2026-10-27: other editors are not the bar). Run: python3 tests/rubric_lint_test.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUBRIC = ROOT / "docs/design/rubric.md"
EDITORS = ("sublime text", "zed", "vs code", "vscode", "visual studio code",
           "jetbrains ide", "nova", "textmate", "bbedit", "emacs", "vim")
TAG = re.compile(r"\((?:P|M|E|G)[):]|derivation|§")
# A number that needs evidence: digits not part of an identifier (C1, 2x),
# hex colour or criterion label.
NUM = re.compile(r"(?<![A-Za-z#\w])\d+(?:\.\d+)?(?:\s*(?:px|mm|pt|%|:1|ms))")


def lint(text):
    errs = []
    for i in range(1, 11):
        m = re.search(rf"^### C{i} .+$", text, re.M)
        if not m:
            errs.append(f"C{i}: heading '### C{i} <name>' missing")
            continue
        end = re.search(r"^### C\d+ |^## ", text[m.end():], re.M)
        body = text[m.end(): m.end() + end.start()] if end else text[m.end():]
        for a in (1, 3, 5):
            line = re.search(rf"^- \*\*{a}\*\*[:.]? (.+)$", body, re.M)
            if not line:
                errs.append(f"C{i}: anchor {a} missing ('- **{a}** ...')")
                continue
            if NUM.search(line.group(1)) and not TAG.search(line.group(1)):
                errs.append(f"C{i} anchor {a}: number without (P)/(M)/(E)/(G) "
                            f"or derivation pointer")
    low = text.lower()
    for e in EDITORS:
        if re.search(rf"\b{re.escape(e)}\b", low):
            errs.append(f"names another editor as a reference: {e!r}")
    return errs


def selftest():
    good = "\n".join(
        f"### C{i} Name\n- **1** bad\n- **3** ok, 4.5:1 (P)\n- **5** best\n"
        for i in range(1, 11))
    assert lint(good) == [], lint(good)
    assert any("C3" in e for e in lint(good.replace("### C3 ", "### X3 ")))
    assert any("number without" in e
               for e in lint(good.replace("4.5:1 (P)", "4.5:1", 1)))
    assert any("editor" in e for e in lint(good + "\nlike Sublime Text\n"))


def main():
    selftest()
    if not RUBRIC.exists():
        print(f"FAIL: {RUBRIC.relative_to(ROOT)} missing")
        return 1
    errs = lint(RUBRIC.read_text(encoding="utf-8"))
    for e in errs:
        print("FAIL:", e)
    if errs:
        return 1
    print("rubric_lint_test: PASS (10 criteria, anchors 1/3/5, numbers tagged)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
