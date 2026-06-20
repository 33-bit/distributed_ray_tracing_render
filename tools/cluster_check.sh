#!/usr/bin/env bash
# Pre-flight check for the MPI cluster: SSH reachability, matching Open MPI
# versions, and a launch test — run this before a real distributed render.
#
#   tools/cluster_check.sh [hostfile]
#
# See docs/CLUSTER_SETUP.md for setup steps.
set -uo pipefail

HF="${1:-hostfile}"
[ -f "$HF" ] || { echo "hostfile '$HF' not found — copy hostfile.example to hostfile"; exit 1; }

hosts() { grep -v '^[[:space:]]*#' "$HF" | awk 'NF{print $1}'; }
NP=$(grep -v '^[[:space:]]*#' "$HF" | awk -F'slots=' 'NF>1{s+=$2} END{print s+0}')
[ "$NP" -gt 0 ] || { echo "no slots found in $HF"; exit 1; }

echo "== hosts in $HF (total slots: $NP) =="
hosts | sed 's/^/  /'

echo "== passwordless SSH =="
for h in $(hosts); do
    if ssh -o BatchMode=yes -o ConnectTimeout=5 "$h" true 2>/dev/null; then
        echo "  OK    $h"
    else
        echo "  FAIL  $h   (set up passwordless SSH — docs/CLUSTER_SETUP.md §4)"
    fi
done

echo "== Open MPI version per host (these MUST match) =="
for h in $(hosts); do
    v=$(ssh -o BatchMode=yes -o ConnectTimeout=5 "$h" 'mpirun --version 2>/dev/null | head -1' 2>/dev/null)
    echo "  $h : ${v:-<unreachable or mpi not installed>}"
done

echo "== launch test: hostname across all $NP slots =="
if mpirun --hostfile "$HF" -np "$NP" hostname 2>/dev/null | sort | uniq -c; then
    echo "OK — every host replied. If versions match above, you're ready (CLUSTER_SETUP.md §8)."
else
    echo "launch FAILED — see docs/CLUSTER_SETUP.md §9 (troubleshooting)."
    exit 1
fi
