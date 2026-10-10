#!/usr/bin/env python3
"""Validate the raw run and produce nearest-rank TRACK rows, never gate verdicts."""
import argparse
import math
import re
from collections import defaultdict
from pathlib import Path


def percentile(values, q):
    values = sorted(values)
    return values[max(0, math.ceil(q * len(values)) - 1)]


def read_log(path):
    rows = defaultdict(list)
    sizes = {}
    for line in Path(path).read_text().splitlines():
        if ' READY ' in line:
            v = re.search(r'variant=(\w)', line)[1]
            sizes[v] = int(re.search(r'binary_bytes=(\d+)', line)[1])
        if ' SAMPLE ' not in line:
            continue
        fields = dict(re.findall(r'(\w+)=([^ ]+)', line))
        v, c = fields['variant'], fields['condition']
        sample = {k: int(fields[k]) for k in ('trial', 'idle_ns', 'frame_ns', 'key_ns',
                    'hint_wall_ns', 'hint_cpu_ns', 'rss_kib', 'minflt', 'majflt', 'allocations')}
        sample['load'] = float(fields['load1'])
        sample['power'] = fields['power']
        rows[v, c].append(sample)
    return rows, sizes


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('log', nargs='?', default='build/prewake/trials.log')
    parser.add_argument('--partial', action='store_true')
    args = parser.parse_args()
    rows, sizes = read_log(args.log)
    if not args.partial:
        text = Path(args.log).read_text()
        assert 'DONE all samples collected' in text, 'run incomplete'
        sequence = re.findall(r'variant=(\w) trial=\d+ SAMPLE condition=(\w)', text)
        assert sequence == [(v, c) for _ in range(200) for c in 'NH' for v in 'abc'], 'order/count mismatch'
        for v in 'abc':
            for c in 'NH':
                samples = rows[v, c]
                assert len(samples) == 200, (v, c, len(samples))
                assert [s['trial'] for s in samples] == list(range(1, 201))
                assert all(s['idle_ns'] >= 15_000_000_000 for s in samples), 'idle too short'
                assert all(s['allocations'] == 0 for s in samples), 'submit allocation failure'
                assert all(s['power'] in ('(M)[AC]', '(M)[bat]') for s in samples), 'missing power'
    print('| Variant / condition | n | frame p50 / p99 ms | key p50 / p99 ms | hint wall p50 / p99 ms | hint process CPU p50 / p99 ms | binary bytes | max RSS KiB | max major faults (cumulative) | load1 range | power |')
    print('|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|')
    for v in 'abc':
        for c in 'NH':
            samples = rows[v, c]
            if not samples:
                continue
            pairs = []
            for field in ('frame_ns', 'key_ns', 'hint_wall_ns', 'hint_cpu_ns'):
                values = [s[field] for s in samples]
                pairs.append(f'{percentile(values, .5)/1e6:.6f} / {percentile(values, .99)/1e6:.6f}')
            powers = ','.join(sorted(set(s['power'] for s in samples)))
            print(f'| {v.upper()} / {"no hint" if c == "N" else "hint"} | {len(samples)} | ' + ' | '.join(pairs) + f' | {sizes[v]} | {max(s["rss_kib"] for s in samples)} | {max(s["majflt"] for s in samples)} | {min(s["load"] for s in samples):.2f}–{max(s["load"] for s in samples):.2f} | {powers} |')
    if not args.partial:
        print('\nBENCH validation: 1200 samples, 200 per variant/condition, all idle >=15s, zero submit allocations, all page-fault samples retained, A/B/C/A/B/C order PASS; TRACK')
        print('Idle_ns min/max:', min(s['idle_ns'] for values in rows.values() for s in values), max(s['idle_ns'] for values in rows.values() for s in values))
        print('First/last sample timestamps:', next(line.split()[0] for line in text.splitlines() if ' SAMPLE ' in line), [line.split()[0] for line in text.splitlines() if ' SAMPLE ' in line][-1])
        for v in 'abc':
            n, h = rows[v, 'N'], rows[v, 'H']
            delta = [y['frame_ns']-x['frame_ns'] for x, y in zip(n, h)]
            print(f'PAIR variant={v} frame_H_minus_N_p50_ms={percentile(delta,.5)/1e6:.6f} p99_ms={percentile(delta,.99)/1e6:.6f} (M) power={sorted(set(s["power"] for s in n+h))} load1={min(s["load"] for s in n+h):.2f}–{max(s["load"] for s in n+h):.2f}')
        front = []
        points = {}
        for v in 'abc':
            samples = rows[v, 'H']
            points[v] = tuple(percentile([s[field] for s in samples], q)
                              for field in ('key_ns', 'hint_cpu_ns') for q in (.5, .99)) + (sizes[v], max(s['rss_kib'] for s in samples))
        for v, point in points.items():
            if not any(all(a <= b for a, b in zip(other, point)) and any(a < b for a, b in zip(other, point))
                       for w, other in points.items() if w != v):
                front.append(v)
        print('PARETO raw hinted-key p50/p99, hint CPU p50/p99, binary size, max RSS:', ','.join(front))
        decision_front = []
        for v, point in points.items():
            point = point[:-1]
            if not any(all(a <= b for a, b in zip(other[:-1], point)) and any(a < b for a, b in zip(other[:-1], point))
                       for w, other in points.items() if w != v):
                decision_front.append(v)
        print('PARETO decision hinted-key p50/p99, hint CPU p50/p99, binary size (equal reserved render capacity):', ','.join(decision_front))


if __name__ == '__main__':
    main()
