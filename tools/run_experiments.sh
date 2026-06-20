#!/usr/bin/env bash
# Member D — run the three experiments from the proposal and dump per-rank
# timings to CSVs under output/ for make_charts.py.
#
#   tools/run_experiments.sh
#
# Override the workload (the "N") and core count via env vars, e.g.
#   W=640 H=480 SPP=48 FRAMES=12 MAXP=8 tools/run_experiments.sh
set -euo pipefail
cd "$(dirname "$0")/.."

BIN=./raytracer_mpi
[ -x "$BIN" ] || { echo "build first: make mpi"; exit 1; }

OUTDIR="${OUTDIR:-output}"
FRAMESDIR="${FRAMESDIR:-/tmp/rt_exp_frames}"
mkdir -p "$OUTDIR" "$FRAMESDIR"

# Workload N (tuned so a 1-core run is a few seconds; scale up for the report).
W=${W:-480}; H=${H:-360}; SPP=${SPP:-32}; DEPTH=${DEPTH:-6}; SH=${SH:-1}; FRAMES=${FRAMES:-8}
MAXP=${MAXP:-8}
TRIALS=${TRIALS:-3}    # best-of-N: each config is run TRIALS times; charts keep the fastest
COMMON="--width $W --height $H --spp $SPP --depth $DEPTH --shadow-samples $SH --frames $FRAMES"
echo "workload: $COMMON   (max procs $MAXP, $TRIALS trials each)"

# --- Experiment 2: granularity (fixed N, fixed P, vary tile size) ---
GCSV="$OUTDIR/granularity.csv"; rm -f "$GCSV"
echo "## granularity sweep (P=$MAXP)"
for ts in 16 32 64 128; do
    for trial in $(seq 1 "$TRIALS"); do
        mpirun -np "$MAXP" $BIN $COMMON --tile "$ts" --schedule dynamic \
            --out "$FRAMESDIR" --bench "$GCSV" | sed "s/^/   t$trial /"
    done
done

# --- Experiment 3: speedup (fixed N, vary P) ---
SCSV="$OUTDIR/speedup.csv"; rm -f "$SCSV"
echo "## speedup sweep (tile 32)"
for p in 1 2 4 8; do
    [ "$p" -le "$MAXP" ] || continue
    for trial in $(seq 1 "$TRIALS"); do
        mpirun -np "$p" $BIN $COMMON --tile 32 --schedule dynamic \
            --out "$FRAMESDIR" --bench "$SCSV" | sed "s/^/   t$trial /"
    done
done

# --- Bonus: static vs dynamic at a coarse tile (load-balance contrast) ---
DCSV="$OUTDIR/sched.csv"; rm -f "$DCSV"
echo "## static vs dynamic (P=$MAXP, tile 64)"
for sch in dynamic static; do
    for trial in $(seq 1 "$TRIALS"); do
        mpirun -np "$MAXP" $BIN $COMMON --tile 64 --schedule "$sch" \
            --out "$FRAMESDIR" --bench "$DCSV" | sed "s/^/   t$trial /"
    done
done

echo "wrote: $GCSV  $SCSV  $DCSV"
echo "now run: python3 tools/make_charts.py $OUTDIR"
