"""Encode vendored query bytes without runtime file I/O (run from worktree root)."""
from pathlib import Path
import json

for language in ("c", "rust", "python", "javascript", "json", "toml", "yaml",
                 "julia", "typescript", "markdown", "markdown_inline", "latex", "bash"):
    directory = f"vendor/tree-sitter-{language}"
    if language in ("markdown", "markdown_inline"):
        directory = "vendor/tree-sitter-markdown/tree-sitter-" + language.replace("_", "-")
    source = Path(f"{directory}/queries/highlights.scm").read_text()
    output = Path(f"src/hl/query_{language}.inc")
    output.write_text(
        "/* Verbatim query bytes encoded as C strings; regenerate with tools/hl_embed.py. */\n"
        f"static const char highlights_{language}[] =\n"
        + "".join(json.dumps(line).replace("?", r"\?") + "\n" for line in source.splitlines(keepends=True))
        + ";\n"
    )
