#!/usr/bin/env python3
"""Correctness check: compare two directories of PPM frames pixel-by-pixel.

Used to validate the MPI renderer against the sequential baseline. Reports, per
frame and overall, the Mean Squared Error, the maximum absolute channel
difference, and the mean absolute difference.

    tools/compare_frames.py frames_seq frames_mpi [--threshold 1.0]

Exit code 0 if within threshold (0 with max-diff 0 means byte-identical),
1 if the difference exceeds the threshold, 2 on a structural problem.
"""
import sys
import os
import glob
import argparse

try:
    import numpy as np
    HAVE_NUMPY = True
except ImportError:
    HAVE_NUMPY = False


def read_ppm(path):
    """Return (width, height, raw_rgb_bytes) for a binary P6 PPM."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:2] != b'P6':
        raise ValueError(f"{path}: not a P6 PPM")
    idx = 2
    vals = []
    while len(vals) < 3:                       # width, height, maxval
        while data[idx:idx + 1].isspace():
            idx += 1
        if data[idx:idx + 1] == b'#':          # skip comment line
            while data[idx:idx + 1] != b'\n':
                idx += 1
            continue
        start = idx
        while not data[idx:idx + 1].isspace():
            idx += 1
        vals.append(int(data[start:idx]))
    idx += 1                                    # single whitespace after maxval
    w, h, _ = vals
    return w, h, data[idx:idx + w * h * 3]


def frame_stats(a, b):
    """(mse, max_abs, mean_abs) over two equal-length RGB byte buffers."""
    n = min(len(a), len(b))
    if HAVE_NUMPY:
        x = np.frombuffer(a[:n], dtype=np.uint8).astype(np.int32)
        y = np.frombuffer(b[:n], dtype=np.uint8).astype(np.int32)
        d = np.abs(x - y)
        return float((d * d).mean()), int(d.max(initial=0)), float(d.mean())
    sq = mx = ab = 0
    for i in range(n):
        d = abs(a[i] - b[i]); sq += d * d; ab += d
        if d > mx:
            mx = d
    return (sq / n if n else 0.0), mx, (ab / n if n else 0.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dir_a')
    ap.add_argument('dir_b')
    ap.add_argument('--threshold', type=float, default=1.0,
                    help="max acceptable overall MSE (default 1.0)")
    args = ap.parse_args()

    names = sorted(os.path.basename(p)
                   for p in glob.glob(os.path.join(args.dir_a, 'frame_*.ppm')))
    if not names:
        print(f"no frames in {args.dir_a}")
        sys.exit(2)

    tot_sq = tot_n = tot_abs = 0
    gmax = 0
    print(f"{'frame':<20}{'MSE':>12}{'maxAbs':>9}{'meanAbs':>10}")
    print("-" * 51)
    for name in names:
        pa, pb = os.path.join(args.dir_a, name), os.path.join(args.dir_b, name)
        if not os.path.exists(pb):
            print(f"{name:<20}  MISSING in {args.dir_b}")
            sys.exit(2)
        wa, ha, da = read_ppm(pa)
        wb, hb, db = read_ppm(pb)
        if (wa, ha) != (wb, hb):
            print(f"{name}: dimension mismatch {wa}x{ha} vs {wb}x{hb}")
            sys.exit(2)
        mse, mx, mean = frame_stats(da, db)
        n = min(len(da), len(db))
        tot_sq += mse * n
        tot_n += n
        tot_abs += mean * n
        gmax = max(gmax, mx)
        print(f"{name:<20}{mse:>12.5f}{mx:>9}{mean:>10.5f}")

    omse = tot_sq / tot_n if tot_n else 0.0
    print("-" * 51)
    print(f"{'OVERALL':<20}{omse:>12.5f}{gmax:>9}{(tot_abs / tot_n if tot_n else 0):>10.5f}")

    if gmax == 0:
        print(f"\nPASS: frames are byte-identical (MSE=0).")
        sys.exit(0)
    if omse <= args.threshold:
        print(f"\nPASS: within threshold (MSE={omse:.5f} <= {args.threshold}, max diff {gmax}).")
        sys.exit(0)
    print(f"\nFAIL: MSE={omse:.5f} > threshold {args.threshold}.")
    sys.exit(1)


if __name__ == '__main__':
    main()
