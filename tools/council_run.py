#!/usr/bin/env python3
"""Design council runner (edit-cxho.23). Scores are advisory (CLAUDE.md law 1).

  prepare --mode absolute --a SHOTS --out RUN [--seed N]
  prepare --mode ab --a SHOTS --b SHOTS --out RUN [--seed N]
      Writes RUN/prompt.md, RUN/schema.json, RUN/images.json and, in ab mode,
      RUN/key.json (which side is which; never sent to a member). Images are
      scenes from tools/design_shots.sh scaled to half size plus two 1:1 crops
      of the code scene for typography.
  run RUN [--member codex|muse|opus ...] [--dry-run]
      One batched call per member (all scenes, both themes). Writes
      RUN/out-<member>.json; --dry-run prints the calls without invoking.
  aggregate RUN
      Validates every out-*.json and writes RUN/report.md.

Members: Codex gpt-6.1-sol xhigh, Muse muse-wonder-full-1.0, Claude Opus.
"""
import argparse
import json
import random
import re
import shlex
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUBRIC = ROOT / "docs/design/rubric.md"
CRITERIA = [f"C{i}" for i in range(1, 11)]
# Scenes sent to the council: six of design_shots.sh's eight. 04-replace and
# 05-goto repeat 03-find's minibar.
SCENES = ["01-empty", "02-tabs", "03-find", "06-palette", "07-code", "08-large"]
# The control pair shows one build on both sides. It must not be the crop
# scene: round 2 (2026-10-27) used 07-code for both, so a member saw the
# change in the crops next to an unchanged control and reported it as noise.
CONTROL = "03-find"
CROP_SCENE = "07-code"
MEMBERS = ("codex", "muse", "opus")
# Words that would tell a member which side of an A/B pair is the change.
LEAKS = ("baseline", "candidate", " new ", " old ", "before", "after",
         "previous", "current build", "improv")
# (P) design_shots.sh renders 3296x2060; half size is 1648x1030, within the
# image sizes the members accept without their own rescale.
HALF = "50%"
CROP = "1400x880+0+0"  # 1:1 top-left of CROP_SCENE: tabs, gutter, ~20 code rows


def schema(mode):
    s = {"type": "integer", "minimum": 1, "maximum": 5}
    item = {"id": {"type": "string", "enum": CRITERIA},
            "evidence": {"type": "string"}, "change": {"type": "string"},
            "cost": {"type": "string"}}
    if mode == "absolute":
        item["score"] = s
    else:
        item["score_a"] = s
        item["score_b"] = s
    top = {"criteria": {"type": "array", "minItems": 10, "maxItems": 10,
                        "items": {"type": "object", "additionalProperties": False,
                                  "properties": item, "required": list(item)}},
           "verdict": {"type": "string"}}
    if mode == "absolute":
        top["top_gaps"] = {"type": "array", "minItems": 3, "maxItems": 3,
                           "items": {"type": "string"}}
    else:
        top["scene_prefs"] = {
            "type": "array",
            "items": {"type": "object", "additionalProperties": False,
                      "properties": {"scene": {"type": "string"},
                                     "prefer": {"type": "string",
                                                "enum": ["A", "B", "same"]}},
                      "required": ["scene", "prefer"]}}
    return {"type": "object", "additionalProperties": False,
            "properties": top, "required": list(top)}


def validate(r, mode):
    errs = []
    cr = r.get("criteria")
    if not isinstance(cr, list):
        return ["criteria missing"]
    ids = [x.get("id") for x in cr]
    for cid in CRITERIA:
        if ids.count(cid) != 1:
            errs.append(f"{cid} appears {ids.count(cid)} times")
    keys = ("score",) if mode == "absolute" else ("score_a", "score_b")
    for x in cr:
        for k in keys:
            v = x.get(k)
            if not isinstance(v, int) or isinstance(v, bool) or not 1 <= v <= 5:
                errs.append(f"{x.get('id')}.{k} = {v!r} outside 1-5")
    if mode == "ab":
        for p in r.get("scene_prefs", []):
            if p.get("prefer") not in ("A", "B", "same"):
                errs.append(f"scene_prefs {p!r}")
    return errs


def by_id(r):
    return {x["id"]: x for x in r["criteria"]}


def aggregate_absolute(outs):
    agg = {}
    for cid in CRITERIA:
        v = [by_id(r)[cid]["score"] for r in outs.values()]
        agg[cid] = {"scores": v, "mean": round(statistics.mean(v), 2),
                    "median": statistics.median(v),
                    "disagree": max(v) - min(v) >= 2}
    return agg


def aggregate_ab(outs, key):
    cand = "a" if key["A"] == "cand" else "b"
    base = "b" if cand == "a" else "a"
    per, regressions = {}, []
    for cid in CRITERIA:
        d = [by_id(r)[cid]["score_" + cand] - by_id(r)[cid]["score_" + base]
             for r in outs.values()]
        per[cid] = {"deltas": d, "delta": round(statistics.mean(d), 2),
                    "base": [by_id(r)[cid]["score_" + base] for r in outs.values()],
                    "cand": [by_id(r)[cid]["score_" + cand] for r in outs.values()]}
        # A regression: any member scores the change lower on this criterion.
        if min(d) < 0:
            regressions.append(cid)
    noisy, prefer = 0, {}
    side = cand.upper()
    for r in outs.values():
        for p in r.get("scene_prefs", []):
            if p["scene"] == key["control_scene"]:
                noisy += p["prefer"] != "same"
            elif p["prefer"] == side:
                prefer[p["scene"]] = prefer.get(p["scene"], 0) + 1
    return {"per": per, "regressions": regressions,
            "control_noise": noisy / len(outs), "prefer_cand": prefer}


def rubric_text():
    t = RUBRIC.read_text(encoding="utf-8")
    t = t[t.index("## The standard"):]
    # Keep the blind prompt free of words that name a side.
    t = re.sub(r"- \*\*Improvement earns nothing\.\*\*[^\n]*\n", "", t)
    return t.replace("new primitive", "additional primitive")


def magick(args):
    exe = shutil.which("magick") or shutil.which("convert")
    subprocess.run([exe, *args], check=True)


def prepare(a):
    out = Path(a.out)
    (out / "img").mkdir(parents=True, exist_ok=True)
    rng = random.Random(a.seed)
    images = []  # (label, path)
    builds = {"A": Path(a.a)} if a.mode == "absolute" else None
    key = None
    if a.mode == "ab":
        order = ["base", "cand"]
        rng.shuffle(order)
        key = {"A": order[0], "B": order[1], "control_scene": CONTROL,
               "seed": a.seed}
        src = {"base": Path(a.a), "cand": Path(a.b)}
        builds = {"A": src[order[0]], "B": src[order[1]]}
    for theme in ("dark", "light"):
        for s in SCENES:
            for side, d in builds.items():
                # Control: the same image on both sides of one pair.
                srcdir = builds["A"] if a.mode == "ab" and s == CONTROL else d
                name = f"{s}-{theme}" + ("" if a.mode == "absolute" else f"-{side}")
                dst = out / "img" / f"{name}.png"
                if not a.no_images:
                    magick([str(srcdir / f"{s}-{theme}.png"), "-resize", HALF,
                            str(dst)])
                images.append((f"scene {s}, {theme} theme"
                               + ("" if a.mode == "absolute" else f", side {side}")
                               + ", half size", dst))
        for side, d in builds.items():
            name = f"crop-{CROP_SCENE}-{theme}" + ("" if a.mode == "absolute" else f"-{side}")
            dst = out / "img" / f"{name}.png"
            if not a.no_images:
                magick([str(d / f"{CROP_SCENE}-{theme}.png"), "-crop", CROP,
                        "+repage", str(dst)])
            images.append((f"typography detail of {CROP_SCENE}, {theme} theme, "
                           "1:1 pixels at 192 DPI"
                           + ("" if a.mode == "absolute" else f", side {side}"),
                           dst))
    listing = "\n".join(f"{i + 1}. `{p.resolve()}`: {lab}"
                        for i, (lab, p) in enumerate(images))
    if a.mode == "absolute":
        task = (
            "Grade the screens below of sublimité, a text editor, against the "
            "standard and the ten criteria. Score each criterion 1-5 on the "
            "rubric's absolute scale, from the evidence in the pixels across all "
            "scenes and both themes. Do not grade on a curve and do not compare "
            "with other software. For each criterion give one sentence of "
            "pixel evidence, the single change that would most raise it, and "
            "its perf cost (0 B / bytes, derived / needs an additional "
            "primitive). Then name the three gaps, in order, that most keep this "
            "screen out of the pages of the periodicals described, and a "
            "one-paragraph verdict.")
    else:
        task = (
            "Two variants of sublimité, a text editor, are shown as side A and "
            "side B, scene by scene in both themes. Score each criterion 1-5 for "
            "side A and for side B on the rubric's absolute scale. In "
            "scene_prefs, for every scene listed, say which side is better "
            "designed or 'same' if you see no difference. For each criterion "
            "give one sentence of pixel evidence comparing the sides, the "
            "single change that would most raise the better side, and its perf "
            "cost. Finish with a one-paragraph verdict.")
    prompt = (f"# Design council\n\nYou are the design editor described in the "
              f"standard below. Be exacting; the scale is absolute.\n\n{task}\n\n"
              f"Images (attached in this order; also on disk at the paths given):\n"
              f"{listing}\n\nAnswer only with JSON matching the schema.\n\n"
              f"{rubric_text()}")
    if a.mode == "ab":
        low = prompt.lower()
        hit = [w for w in LEAKS if w in low]
        if hit:
            sys.exit(f"blind prompt leaks {hit}")
        (out / "key.json").write_text(json.dumps(key, indent=1))
    (out / "prompt.md").write_text(prompt)
    (out / "schema.json").write_text(json.dumps(schema(a.mode), indent=1))
    (out / "images.json").write_text(json.dumps(
        {"mode": a.mode, "images": [str(p.resolve()) for _, p in images]},
        indent=1))
    print(f"prepared {a.mode}: {len(images)} images, prompt {len(prompt)} chars")


def calls(run, members):
    meta = json.loads((run / "images.json").read_text())
    imgs = meta["images"]
    prompt = (run / "prompt.md").read_text()
    sch = run / "schema.json"
    out = {}
    for m in members:
        o = run / f"out-{m}.json"
        if m == "codex":
            cmd = ["codex", "exec", "-m", "gpt-6.1-sol", "-c",
                   "model_reasoning_effort=xhigh", "-s", "read-only",
                   "--skip-git-repo-check", "--output-schema", str(sch),
                   "-o", str(o)]
            for p in imgs:
                cmd += ["-i", p]
            cmd += ["--", prompt]
        elif m == "muse":
            cmd = ["muse", "exec", "--model", "muse-wonder-full-1.0",
                   "--reasoning-effort", "high", "--output-schema", str(sch),
                   "--prompt-file", str(run / "prompt.md")]
            for p in imgs:
                cmd += ["--image", p]
        else:
            cmd = ["claude", "-p", "--model", "opus", "--allowedTools", "Read",
                   "--output-format", "json", "--json-schema", sch.read_text(),
                   "Read every image path listed below with the Read tool "
                   "before answering.\n\n" + prompt]
        out[m] = (cmd, o)
    return out


def extract_json(text):
    """The last top-level JSON object in a member's output."""
    dec, last, i = json.JSONDecoder(), None, 0
    while True:
        j = text.find("{", i)
        if j < 0:
            return last
        try:
            obj, end = dec.raw_decode(text, j)
            last, i = obj, end
        except ValueError:
            i = j + 1


def run_cmd(a):
    run = Path(a.run)
    members = a.member or list(MEMBERS)
    for m, (cmd, o) in calls(run, members).items():
        shown = " ".join(shlex.quote(c) if len(c) < 200 else f"<{len(c)} chars>"
                         for c in cmd)
        print(f"CALL {m}: {shown}")
        if a.dry_run:
            continue
        p = subprocess.run(cmd, capture_output=True, text=True,
                           stdin=subprocess.DEVNULL)
        (run / f"raw-{m}.txt").write_text(p.stdout + "\n--stderr--\n" + p.stderr)
        if m != "codex" or not o.exists():
            obj = extract_json(p.stdout)
            if m == "opus" and obj is not None:
                obj = obj.get("structured_output") or extract_json(
                    obj.get("result", "")) or obj
            if obj is not None:
                o.write_text(json.dumps(obj, indent=1))
        print(f"  exit {p.returncode}; {'wrote ' + o.name if o.exists() else 'NO OUTPUT'}")


def aggregate_cmd(a):
    run = Path(a.run)
    mode = json.loads((run / "images.json").read_text())["mode"]
    outs = {}
    for f in sorted(run.glob("out-*.json")):
        r = json.loads(f.read_text())
        errs = validate(r, mode)
        m = f.stem[4:]
        if errs:
            print(f"{m}: rejected: {errs}")
            continue
        outs[m] = r
    if not outs:
        sys.exit("no valid member output")
    names = list(outs)
    lines = [f"# Design council — {run.name} ({mode})", "",
             "Advisory scores (CLAUDE.md law 1); scale and standard in "
             "docs/design/rubric.md. Members: " + ", ".join(names) + ".", ""]
    if mode == "absolute":
        agg = aggregate_absolute(outs)
        lines += ["| | " + " | ".join(names) + " | mean | spread ≥ 2 |",
                  "|---|" + "---|" * (len(names) + 2)]
        for cid in CRITERIA:
            g = agg[cid]
            lines.append(f"| {cid} | " + " | ".join(map(str, g["scores"]))
                         + f" | {g['mean']} | {'yes' if g['disagree'] else ''} |")
        lines += ["", f"Overall mean: "
                  f"{round(statistics.mean(g['mean'] for g in agg.values()), 2)}"]
        for m, r in outs.items():
            lines += ["", f"## {m}", "", "Top gaps:"]
            lines += [f"{i + 1}. {g}" for i, g in enumerate(r["top_gaps"])]
            lines += ["", r["verdict"], ""]
            lines += [f"- **{x['id']} {x['score']}**: {x['evidence']} "
                      f"*Change:* {x['change']} *Cost:* {x['cost']}"
                      for x in r["criteria"]]
    else:
        key = json.loads((run / "key.json").read_text())
        ab = aggregate_ab(outs, key)
        lines += [f"Key: A = {key['A']}, B = {key['B']} (seed {key['seed']}).",
                  "", "| | base | cand | mean delta |", "|---|---|---|---|"]
        for cid in CRITERIA:
            g = ab["per"][cid]
            lines.append(f"| {cid} | {g['base']} | {g['cand']} | {g['delta']:+} |")
        lines += ["", f"Regressed (any member lower): {ab['regressions'] or 'none'}",
                  f"Control noise (members seeing a difference in identical "
                  f"images): {ab['control_noise']}",
                  f"Scenes preferring the change: {ab['prefer_cand']}"]
        for m, r in outs.items():
            lines += ["", f"## {m}", "", r["verdict"], ""]
            lines += [f"- **{x['id']} A{x['score_a']} B{x['score_b']}**: "
                      f"{x['evidence']} *Change:* {x['change']}"
                      for x in r["criteria"]]
    (run / "report.md").write_text("\n".join(lines) + "\n")
    print(f"wrote {run / 'report.md'}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("prepare")
    p.add_argument("--mode", choices=("absolute", "ab"), required=True)
    p.add_argument("--a", required=True)
    p.add_argument("--b")
    p.add_argument("--out", required=True)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--no-images", action="store_true")
    r = sub.add_parser("run")
    r.add_argument("run")
    r.add_argument("--member", action="append", choices=MEMBERS)
    r.add_argument("--dry-run", action="store_true")
    g = sub.add_parser("aggregate")
    g.add_argument("run")
    a = ap.parse_args()
    if a.cmd == "prepare":
        if a.mode == "ab" and not a.b:
            ap.error("--mode ab needs --b")
        prepare(a)
    elif a.cmd == "run":
        run_cmd(a)
    else:
        aggregate_cmd(a)


if __name__ == "__main__":
    main()
