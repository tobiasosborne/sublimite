#!/usr/bin/env python3
"""Quota and pace for every provider T3 Code knows, read from T3 Code's own caches.

T3 Code writes each provider's limits to $T3CODE_HOME/caches/<provider>.json
under "usageLimits" (windows with usedPercent, resetsAt, windowDurationMins).
Pace is uniform use of a window: u* = elapsed / duration; delta = used - u*.
"room" is how many points may still be spent now without passing pace.

Providers without usageLimits (Muse, Pi) print "no data": ask the vendor CLI
(`muse usage`) or Tobias.

Usage: python3 tools/t3quota.py [--json]
"""
import datetime
import glob
import json
import os
import sys


def main() -> int:
    home = os.environ.get("T3CODE_HOME", os.path.expanduser("~/.t3"))
    now = datetime.datetime.now(datetime.timezone.utc)
    rows = []
    for path in sorted(glob.glob(os.path.join(home, "caches", "*.json"))):
        name = os.path.basename(path)[:-5]
        try:
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, ValueError) as e:
            rows.append({"provider": name, "error": str(e)})
            continue
        if not data.get("enabled", True):
            continue
        limits = data.get("usageLimits")
        if not limits:
            rows.append({"provider": name, "window": None})
            continue
        checked = limits.get("checkedAt")
        age_s = None
        if checked:
            t = datetime.datetime.fromisoformat(checked.replace("Z", "+00:00"))
            age_s = int((now - t).total_seconds())
        credits = (limits.get("resetCredits") or {}).get("availableCount")
        for w in limits.get("windows", []):
            used = float(w.get("usedPercent", 0.0))
            resets, dur = w.get("resetsAt"), w.get("windowDurationMins")
            delta = left_min = None
            if resets and dur:
                rt = datetime.datetime.fromisoformat(resets.replace("Z", "+00:00"))
                left_min = (rt - now).total_seconds() / 60.0
                if left_min < 0:
                    # The window has reset since T3 Code last read it: "used" is
                    # from the old window. Report the reset, claim no room.
                    stale_min = -left_min
                    rows.append({
                        "provider": name, "window": w.get("label") or w.get("id"),
                        "used": None, "delta": None, "room": None,
                        "resets_in_min": None, "reset_ago_min": int(stale_min),
                        # Pace of the new window if nothing was used since the
                        # reset: an upper bound on room, not a reading.
                        "pace_if_unused": round(100.0 * (stale_min % dur) / dur, 1),
                        "age_s": age_s, "reset_credits": credits,
                    })
                    continue
                delta = used - 100.0 * (1.0 - left_min / dur)
            rows.append({
                "provider": name, "window": w.get("label") or w.get("id"),
                "used": round(used, 1),
                "delta": None if delta is None else round(delta, 1),
                "room": None if delta is None else round(max(0.0, -delta), 1),
                "resets_in_min": None if left_min is None else int(left_min),
                "age_s": age_s, "reset_credits": credits,
            })
    if "--json" in sys.argv[1:]:
        json.dump(rows, sys.stdout, indent=1)
        print()
        return 0
    print(f"{'PROVIDER':<12} {'WINDOW':<34} {'USED':>6} {'PACE':>14} {'ROOM':>6} {'RESETS IN':>10} {'AGE':>6}")
    for r in rows:
        if r.get("error"):
            print(f"{r['provider']:<12} error: {r['error']}")
        elif r["window"] is None:
            print(f"{r['provider']:<12} no data in T3 Code's cache (ask the vendor CLI or Tobias)")
        elif r.get("reset_ago_min") is not None:
            m = r["reset_ago_min"]
            age = "-" if r["age_s"] is None else f"{r['age_s']}s"
            print(f"{r['provider']:<12} {r['window'][:34]:<34} stale: window reset {m // 60}h {m % 60}m ago; "
                  f"room at most {r['pace_if_unused']:.1f} if unused since (reading {age} old)")
        else:
            d = r["delta"]
            pace = "-" if d is None else (f"{d:+.1f} ahead" if d > 0 else f"{-d:.1f} behind")
            room = "-" if r["room"] is None else f"{r['room']:.1f}"
            m = r["resets_in_min"]
            left = "-" if m is None else f"{m // 1440}d {m % 1440 // 60}h {m % 60}m"
            age = "-" if r["age_s"] is None else f"{r['age_s']}s"
            print(f"{r['provider']:<12} {r['window'][:34]:<34} {r['used']:>5.1f}% {pace:>14} {room:>6} {left:>10} {age:>6}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
