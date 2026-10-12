"""Encode vendored query bytes without runtime file I/O (run from worktree root)."""
from pathlib import Path
import json

for language in ("c", "rust"):
    source = Path(f"vendor/tree-sitter-{language}/queries/highlights.scm").read_text()
    output = Path(f"src/hl/query_{language}.inc")
    output.write_text(
        "/* Verbatim query bytes encoded as C strings; regenerate with tools/hl_embed.py. */\n"
        f"static const char highlights_{language}[] =\n"
        + "".join(json.dumps(line) + "\n" for line in source.splitlines(keepends=True))
        + ";\n"
    )
