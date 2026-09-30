#!/bin/bash
# Correctness runs of the HU50 step-2 path (design of 30 September 2026): train
# a small game of this directory with the literal HU50 command line (board
# class rows, a texture map, DCFR alternating, 32 boards, lazy discount,
# policy snapshots) in resumed segments ending at the given iteration targets,
# and evaluate every snapshot with the exact physical best response
# (tools/monker_compare/convergence_curve.py -> gtosd_preflop_blueprint_monker_values
# --all-flops). The NashConv curve is <run>/convergence.txt (and .json).
#
# Usage (from anywhere; paths relative to the repository root):
#   run_correctness.sh <run dir> <config> <bucket dir> <texture map> <threads> <target>...
# e.g. run_correctness.sh out/monker/correctness/V0 benchmarks/monker/correctness/HU6_V0_flop.json \
#        out/monker/buckets_flopexact_15x4 benchmarks/monker/textures/identity_texture_map.txt \
#        2 250 500 1000 2000 4000 8000 16000 32000 64000
# Environment: BIN (executables, default out/monker/bin_correct/c123: run a
# frozen copy, never the build tree), RES (out/preflop_blueprint_resources),
# EVAL_THREADS (evaluation threads, default <threads>), PARTITION (4: with the
# HU50 value 64 these small trees are one work unit and skip the top/unit
# reduction path of HU50), KEEP_POLICIES=1 keeps every snapshot's policy.bin
# (default: deleted once its evaluation is cached in values.json).
# LOCK_CHARTS=<chart dir> LOCK_NODES=<a,b> pass --lock-charts/--lock-nodes to the trainer
# (e.g. benchmarks/monker/correctness/lock_limp_check with
# CO/CO_strategy.txt,BTN/CO_Call_BTN_strategy.txt: every hand limps and is checked behind,
# so every deal reaches the flop; gate those runs on the gain_lower columns).
# Every segment's start event must carry the tree fingerprint of the config listed in
# README.md (EXPECT_TREE overrides it; a config not in the table is not checked): an
# executable folder without postflop_betting_streets (bin_correct/base) silently builds
# the tree that bets on every street.
# A file <run dir>/CANCEL stops the run before the next segment. Run a frozen
# copy of this script (out/frozen/run_correctness.sh) for long runs.
set -u
# The repository root, also from a frozen copy under out/frozen.
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)" || exit 1
if [ $# -lt 6 ]; then
  echo "usage: $0 <run dir> <config> <bucket dir> <texture map> <threads> <target>..." >&2
  exit 2
fi
RUN="$1"
CFG="$2"
BUCKETS="$3"
MAP="$4"
THREADS="$5"
shift 5
BIN="${BIN:-out/monker/bin_correct/c123}"
RES="${RES:-out/preflop_blueprint_resources}"
EVAL_THREADS="${EVAL_THREADS:-$THREADS}"
PARTITION="${PARTITION:-4}"
LOCK_ARGS=()
if [ -n "${LOCK_CHARTS:-}" ] || [ -n "${LOCK_NODES:-}" ]; then
  LOCK_ARGS=(--lock-charts "${LOCK_CHARTS:-}" --lock-nodes "${LOCK_NODES:-}")
fi
case "$(basename "$CFG")" in
  HU6_all.json) TREE_DEFAULT="fnv1a64:fb76ddcd880fec5f" ;;
  HU6_all_rake25cap2.json) TREE_DEFAULT="fnv1a64:f226b87d43f28215" ;;
  HU6_V0_flop.json) TREE_DEFAULT="fnv1a64:cd66796bdbdac5c1" ;;
  HU6_V1_flopturn.json) TREE_DEFAULT="fnv1a64:da5c6942354ad5ad" ;;
  HU6_V2_river.json) TREE_DEFAULT="fnv1a64:2f109f6f1891d9f2" ;;
  *) TREE_DEFAULT="" ;;
esac
EXPECT_TREE="${EXPECT_TREE:-$TREE_DEFAULT}"
mkdir -p "$RUN/charts"
echo "$(date '+%Y-%m-%d %H:%M:%S') correctness start: config $CFG buckets $BUCKETS map $MAP" \
  "threads $THREADS eval $EVAL_THREADS partition $PARTITION bin $BIN lock ${LOCK_CHARTS:-none}" \
  "${LOCK_NODES:-} tree ${EXPECT_TREE:-unchecked} targets $*" >> "$RUN/run.log"
for N in "$@"; do
  if [ -f "$RUN/CANCEL" ]; then
    echo "$(date '+%H:%M:%S') cancelled before $N" >> "$RUN/run.log"
    exit 3
  fi
  resume=""
  [ -f "$RUN/state.ckpt" ] && resume="--resume"
  started=$(date +%s)
  "$BIN/gtosd_preflop_blueprint_train.exe" --config "$CFG" --resources-dir "$RES" \
    --buckets-dir "$BUCKETS" --board-class-rows --board-texture-map "$MAP" --threads "$THREADS" \
    --table-storage double --eval-every 0 --batch 32 --partition-target "$PARTITION" \
    --scheme dcfr --update alternating --batch-policy-refresh --lazy-discount \
    --progress-every 1000 --iterations "$N" --checkpoint "$RUN/state.ckpt" --chart-every "$N" \
    --chart-dir "$RUN/charts" --policy-snapshots "${LOCK_ARGS[@]}" $resume \
    >> "$RUN/train.jsonl" 2>> "$RUN/train.stderr.log"
  rc=$?
  echo "$(date '+%H:%M:%S') segment $N rc $rc ($(( $(date +%s) - started )) s)" >> "$RUN/run.log"
  [ $rc -ne 0 ] && exit 1
  tree=$(grep '"event": "start"' "$RUN/train.jsonl" | tail -1 \
    | grep -o '"tree_fingerprint": "[^"]*"' | cut -d'"' -f4)
  if [ -n "$EXPECT_TREE" ] && [ "$tree" != "$EXPECT_TREE" ]; then
    echo "$(date '+%H:%M:%S') tree $tree is not the expected $EXPECT_TREE: stop" >> "$RUN/run.log"
    exit 1
  fi
  started=$(date +%s)
  BIN="$BIN" RES="$RES" python tools/monker_compare/convergence_curve.py "$RUN" --config "$CFG" \
    --buckets "$BUCKETS" --texture-map "$MAP" --threads "$EVAL_THREADS" \
    >> "$RUN/curve.log" 2>&1
  rc=$?
  echo "$(date '+%H:%M:%S') evaluation $N rc $rc ($(( $(date +%s) - started )) s)" >> "$RUN/run.log"
  [ $rc -ne 0 ] && exit 1
  snap="$RUN/charts/it_$N"
  if [ "${KEEP_POLICIES:-0}" != "1" ] && [ -f "$snap/values.json" ]; then
    rm -f "$snap/policy.bin"
  fi
done
echo "$(date '+%H:%M:%S') done" >> "$RUN/run.log"
