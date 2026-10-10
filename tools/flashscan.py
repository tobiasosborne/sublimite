#!/usr/bin/env python3
"""flashscan: find frames in a screen recording where a colour appears or disappears abruptly.

usage: flashscan.py VIDEO [--rgb bf853d] [--tol 40] [--fps 60] [--min-px 200] [--region X,Y,W,H]

Decodes VIDEO with ffmpeg to raw RGB at FPS, counts pixels within TOL (max channel
distance) of RGB per frame, and prints every frame where that count jumps by more
than MIN_PX against the previous frame (appear) or drops by more than MIN_PX
(vanish), with timestamps. A flash = appear followed by vanish within a few
frames. Exit 0 always; this is a measurement, not a gate.
"""
import argparse, subprocess, sys
import numpy as np

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video"); ap.add_argument("--rgb", default="bf853d")
    ap.add_argument("--tol", type=int, default=40); ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--min-px", type=int, default=200); ap.add_argument("--region", default=None)
    a = ap.parse_args()
    probe = subprocess.run(["ffprobe","-v","error","-select_streams","v:0","-show_entries",
        "stream=width,height","-of","csv=p=0", a.video], capture_output=True, text=True, check=True)
    w, h = map(int, probe.stdout.strip().split(","))
    vf = f"fps={a.fps}"
    if a.region:
        x, y, rw, rh = map(int, a.region.split(",")); vf += f",crop={rw}:{rh}:{x}:{y}"; w, h = rw, rh
    target = np.array([int(a.rgb[i:i+2], 16) for i in (0, 2, 4)], dtype=np.int16)
    p = subprocess.Popen(["ffmpeg","-v","error","-i",a.video,"-vf",vf,"-f","rawvideo","-pix_fmt","rgb24","-"],
                         stdout=subprocess.PIPE)
    n = w*h*3; prev = None; i = 0; events = []
    while True:
        buf = p.stdout.read(n)
        if len(buf) < n: break
        fr = np.frombuffer(buf, dtype=np.uint8).reshape(h, w, 3).astype(np.int16)
        cnt = int((np.abs(fr - target).max(axis=2) <= a.tol).sum())
        if prev is not None:
            d = cnt - prev
            if d > a.min_px: events.append((i, "appear", prev, cnt))
            elif -d > a.min_px: events.append((i, "vanish", prev, cnt))
        prev = cnt; i += 1
    print(f"frames={i} fps={a.fps} colour=#{a.rgb} tol={a.tol} min_px={a.min_px}")
    for f, kind, before, after in events:
        print(f"t={f/a.fps:8.3f}s frame={f:6d} {kind:6s} {before:7d} -> {after:7d}")
    flashes = sum(1 for k in range(len(events)-1)
                  if events[k][1]=="appear" and events[k+1][1]=="vanish" and events[k+1][0]-events[k][0] <= a.fps//2)
    print(f"flashes (appear then vanish within {a.fps//2} frames): {flashes}")
if __name__ == "__main__": main()
