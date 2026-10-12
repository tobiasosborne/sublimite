#!/usr/bin/env python3
"""Exercise the existing editor STAMP preambles with injected BAT0 files.

The bench function is compiled verbatim. The editor cost function's preamble
is compiled through its STAMP print, before any editor/trace workload starts.
No production path override or display connection is needed.
"""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "bench/editor_bench.c"
EDITOR = ROOT / "tests/editor_test.c"


def preamble(path, start, end):
    source = path.read_text()
    return source[source.index(start):source.index(end, source.index(start))]


DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *fixture_status;
static FILE *fixture_open(const char *path, const char *mode) {
    (void)mode;
    const char *contents;
    if (!strcmp(path, "/sys/class/power_supply/BAT0/status")) {
        if (!strcmp(fixture_status, "missing")) return NULL;
        contents = fixture_status;
    } else if (!strcmp(path, "/proc/loadavg")) contents = "unknown";
    else return NULL;
    FILE *file = tmpfile();
    if (!file) return NULL;
    if (fputs(contents, file) == EOF) { fclose(file); return NULL; }
    rewind(file);
    return file;
}
#define fopen fixture_open
'''


def main():
    snippets = {
        "editor_bench": (
            preamble(BENCH, "typedef struct stamp", '#include "editor_accounting.h"'),
            "(void)power_stamp();",
            "STAMP",
        ),
        "editor_test": (
            preamble(EDITOR, "static int minimap_compose_cost(void)", "    trace_reset();")
            + "    return 0; /* (P) conventional successful test exit. */\n}\n",
            "(void)minimap_compose_cost();",
            "edit-d5o STAMP",
        ),
    }
    # (P) Fixtures enumerate kernel power_supply status spellings plus empty,
    # unavailable and unrecognised input. Missing evidence stays [unknown].
    fixtures = {
        "Discharging\n": ("[bat]", "Discharging"),
        "Charging\n": ("[AC]", "Charging"),
        "Full\n": ("[AC]", "Full"),
        "Not charging\n": ("[AC]", "Not charging"),
        "Unknown\n": ("[unknown]", "Unknown"),
        "unexpected\n": ("[unknown]", "unexpected"),
        "": ("[unknown]", "unknown"),
        "missing": ("[unknown]", "unknown"),
    }
    with tempfile.TemporaryDirectory(prefix="edit-cuh-stamp-") as directory:
        for name, (snippet, call, prefix) in snippets.items():
            source = Path(directory) / f"{name}.c"
            binary = source.with_suffix("")
            source.write_text(DRIVER + snippet + "\nint main(int argc, char **argv) {\n"
                              "    /* (P) executable + fixture argument = argc 2. */\n"
                              "    if (argc != 2) return EXIT_FAILURE;\n"
                              "    fixture_status = argv[1];\n" + call
                              + "\n    return EXIT_SUCCESS;\n}\n")
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wshadow", "-Wconversion", "-O2", str(source),
                            "-o", str(binary)], check=True)
            for status, (tag, label) in fixtures.items():
                output = subprocess.check_output([str(binary), status], text=True)
                expected = f"{prefix} (M){tag} BAT0={label} load1=unknown shared box\n"
                if output != expected:
                    raise AssertionError(f"{name}: {status!r}: {output!r} != {expected!r}")
            print(f"{name}: BAT0 status -> STAMP battery/AC/unknown fixtures PASS")


if __name__ == "__main__":
    main()
