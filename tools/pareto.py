#!/usr/bin/env python3 -I
"""pareto.py name1=log1 name2=log2 ...  [--tol=PCT]

Reads one BENCH log per variant (lines produced by bench/piece_bench.c and
tools/bench_variant.sh), prints a markdown comparison table and the Pareto
front over the p99 of every non-SKIP cell (including build/text_size).
All cells are lower-is-better except unit=GB/s. TIMEOUT counts as +inf.
--tol=PCT: values within PCT percent count as equal (default 0).
Stdlib only; run as `python3 -I tools/pareto.py ...`.
"""
import sys


def parse(path):
    cells = {}
    with open(path, errors="replace") as f:
        for line in f:
            if not line.startswith("BENCH "):
                continue
            kv = {}
            for tok in line.split()[1:]:
                if "=" in tok:
                    k, v = tok.split("=", 1)
                    kv[k] = v
            if "row" not in kv or "col" not in kv:
                continue
            try:
                p50 = float(kv.get("p50", "0"))
                p99 = float(kv.get("p99", "0"))
            except ValueError:
                continue
            cells[(kv["row"], kv["col"])] = {
                "p50": p50, "p99": p99, "unit": kv.get("unit", ""),
                "status": kv.get("status", "TRACK"), "n": kv.get("n", "0"),
            }
    return cells


def score(c):
    """Comparable number (lower is better) or None when no data."""
    if c is None or c["status"] == "SKIP":
        return None
    if c["status"] == "TIMEOUT":
        return float("inf")
    return -c["p99"] if c["unit"] == "GB/s" else c["p99"]


def fmt(c):
    if c is None:
        return "-"
    if c["status"] == "SKIP":
        return "SKIP"
    u = c["unit"]
    s = "%g / %g %s" % (c["p50"], c["p99"], u)
    if c["status"] == "MISS":
        s += " **MISS**"
    elif c["status"] == "TIMEOUT":
        s += " **TIMEOUT**"
    return s


def no_worse(a, b, tol):
    # a <= b within tolerance (a, b finite or +inf)
    if a == b:
        return True
    if a == float("inf"):
        return False
    if b == float("inf"):
        return True
    return a <= b + abs(b) * tol / 100.0


def strictly_better(a, b, tol):
    if a == b:
        return False
    if b == float("inf"):
        return a != float("inf")
    if a == float("inf"):
        return False
    return a < b - abs(b) * tol / 100.0


def main(argv):
    tol = 0.0
    args = []
    for a in argv:
        if a.startswith("--tol="):
            tol = float(a[6:])
        else:
            args.append(a)
    names, logs = [], {}
    for a in args:
        if "=" not in a:
            print("usage: pareto.py name=log ... [--tol=PCT]", file=sys.stderr)
            return 2
        n, p = a.split("=", 1)
        names.append(n)
        logs[n] = parse(p)
    if not names:
        print("usage: pareto.py name=log ... [--tol=PCT]", file=sys.stderr)
        return 2
    keys = []
    for n in names:
        for k in logs[n]:
            if k not in keys:
                keys.append(k)

    print("| row | col | " + " | ".join(names) + " |")
    print("|---|---|" + "|".join("---" for _ in names) + "|")
    for k in keys:
        print("| %s | %s | " % k + " | ".join(fmt(logs[n].get(k)) for n in names) + " |")
    print()

    sc = {n: {k: score(logs[n].get(k)) for k in keys} for n in names}
    dominated = {}
    for b in names:
        for a in names:
            if a == b:
                continue
            common = [k for k in keys if sc[a][k] is not None and sc[b][k] is not None]
            if not common:
                continue
            if all(no_worse(sc[a][k], sc[b][k], tol) for k in common) and \
               any(strictly_better(sc[a][k], sc[b][k], tol) for k in common):
                dominated[b] = a
                break
    front = [n for n in names if n not in dominated]
    print("Non-dominated: " + ", ".join(front))
    if dominated:
        print("Dominated: " + ", ".join("%s (by %s)" % (b, a) for b, a in dominated.items()))
    else:
        print("Dominated: none")
    print()
    for n in names:
        wins = []
        for k in keys:
            vals = [(sc[m][k], m) for m in names if sc[m][k] is not None]
            if len(vals) < 2:
                continue
            best = min(v for v, _ in vals)
            if best == float("inf"):
                continue
            w = [m for v, m in vals if not strictly_better(best, v, tol) and
                 not strictly_better(v, best, tol)]
            if w == [n]:
                wins.append("%s/%s" % k)
        miss = sum(1 for k in keys if logs[n].get(k, {}).get("status") in ("MISS", "TIMEOUT"))
        print("%s: wins %d [%s]; MISS/TIMEOUT cells: %d" % (n, len(wins), ", ".join(wins), miss))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
