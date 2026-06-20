#!/usr/bin/env python3
"""Member D — turn the experiment CSVs into the report charts.

    python3 tools/make_charts.py [output_dir]

Reads speedup.csv, granularity.csv, sched.csv (whichever exist) from the dir
and writes chart_*.png next to them. Pure csv + matplotlib (no pandas).

Timing model (see benchmark.hpp): per run, the master row's total_s is the
makespan (wall, incl. communication); the worst worker's comp_s approximates the
compute-only critical path (what runtime would be if communication were free).
"""
import csv
import os
import sys
import collections
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

INT = ('nprocs', 'width', 'height', 'spp', 'depth', 'shadow_samples',
       'frames', 'tile', 'rank', 'tiles')
FLT = ('comp_s', 'comm_s', 'idle_s', 'total_s')


def load(path):
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            for k in INT:
                r[k] = int(r[k])
            for k in FLT:
                r[k] = float(r[k])
            rows.append(r)
    return rows


def best_trial(rows):
    """Given all rows for one config (possibly several best-of-N trials), chunk
    them into nprocs-sized blocks (one per trial) and return the (master,
    workers) of the fastest trial — the standard way to reject transient
    contention noise on a shared machine."""
    nprocs = rows[0]['nprocs']
    trials = [rows[i:i + nprocs] for i in range(0, len(rows), nprocs)]

    def makespan(block):
        ms = [r for r in block if r['role'] == 'master']
        return ms[0]['total_s'] if ms else min(r['total_s'] for r in block)

    best = min(trials, key=makespan)
    m = [r for r in best if r['role'] == 'master'][0]
    w = [r for r in best if r['role'] == 'worker']
    return m, w


def speedup_chart(path, out):
    by_p = collections.defaultdict(list)
    for r in load(path):
        by_p[r['nprocs']].append(r)
    Ps = sorted(by_p)
    t_total, t_comp = {}, {}
    for p in Ps:
        m, w = best_trial(by_p[p])
        t_total[p] = m['total_s']
        t_comp[p] = max((x['comp_s'] for x in w), default=m['comp_s'])
    base, base_c = t_total[Ps[0]], t_comp[Ps[0]]
    sp_incl = [base / t_total[p] for p in Ps]
    sp_excl = [base_c / t_comp[p] for p in Ps]
    eff = [s / p * 100 for s, p in zip(sp_incl, Ps)]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.5))
    ax1.plot(Ps, Ps, 'k--', label='ideal (P)')
    ax1.plot(Ps, [max(1, p - 1) for p in Ps], 'k:', label='P-1 (dedicated master)')
    ax1.plot(Ps, sp_incl, 'o-', label='measured (incl. comm)')
    ax1.plot(Ps, sp_excl, 's-', label='measured (excl. comm)')
    ax1.set(xlabel='processes P', ylabel='speedup  T1 / TP', title='Speedup')
    ax1.legend(); ax1.grid(alpha=.3)

    ax2.plot(Ps, eff, 'o-', color='tab:green')
    ax2.axhline(100, ls='--', c='k')
    ax2.set(xlabel='processes P', ylabel='efficiency  (%)', title='Parallel efficiency')
    ax2.grid(alpha=.3)
    fig.tight_layout(); fig.savefig(out, dpi=120); plt.close(fig)
    print("wrote", out)


def granularity_chart(path, out):
    by_t = collections.defaultdict(list)
    for r in load(path):
        by_t[r['tile']].append(r)
    tiles = sorted(by_t)
    comp, comm, idle, makespan = [], [], [], []
    for ts in tiles:
        m, w = best_trial(by_t[ts])
        n = max(1, len(w))
        comp.append(sum(x['comp_s'] for x in w) / n)
        comm.append(sum(x['comm_s'] for x in w) / n)
        idle.append(sum(x['idle_s'] for x in w) / n)
        makespan.append(m['total_s'])
    x = np.arange(len(tiles))

    fig, ax = plt.subplots(figsize=(8, 5))
    ax.bar(x, comp, label='compute')
    ax.bar(x, comm, bottom=comp, label='communication')
    ax.bar(x, idle, bottom=[c + m for c, m in zip(comp, comm)], label='idle')
    ax.plot(x, makespan, 'ko-', label='makespan (wall)')
    ax.set_xticks(x); ax.set_xticklabels([f"{t}×{t}" for t in tiles])
    ax.set(xlabel='tile size', ylabel='seconds (avg per worker)',
           title='Granularity: time breakdown vs tile size')
    ax.legend(); ax.grid(alpha=.3, axis='y')
    fig.tight_layout(); fig.savefig(out, dpi=120); plt.close(fig)
    print("wrote", out)


def sched_chart(path, out):
    by_s = collections.defaultdict(list)
    for r in load(path):
        by_s[r['schedule']].append(r)
    scheds = sorted(by_s)
    fig, ax = plt.subplots(figsize=(8, 5))
    width = 0.8 / max(1, len(scheds))
    for i, s in enumerate(scheds):
        _, w = best_trial(by_s[s])
        w = sorted(w, key=lambda r: r['rank'])
        comp = [x['comp_s'] for x in w]
        xs = np.arange(len(comp)) + i * width
        ax.bar(xs, comp, width, label=f"{s} (imbalance "
               f"{100*(max(comp)-min(comp))/max(comp):.1f}%)")
    ax.set(xlabel='worker rank', ylabel='compute seconds',
           title='Load balance: per-worker compute, static vs dynamic')
    ax.legend(); ax.grid(alpha=.3, axis='y')
    fig.tight_layout(); fig.savefig(out, dpi=120); plt.close(fig)
    print("wrote", out)


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else 'output'
    jobs = [('speedup.csv', speedup_chart, 'chart_speedup.png'),
            ('granularity.csv', granularity_chart, 'chart_granularity.png'),
            ('sched.csv', sched_chart, 'chart_schedule.png')]
    made = 0
    for name, fn, out in jobs:
        p = os.path.join(d, name)
        if os.path.exists(p):
            fn(p, os.path.join(d, out)); made += 1
        else:
            print("skip (missing):", p)
    if not made:
        print("no CSVs found in", d, "- run tools/run_experiments.sh first")
        sys.exit(1)


if __name__ == '__main__':
    main()
