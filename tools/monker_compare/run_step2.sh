#!/bin/bash
# MonkerSolver step 2 on one benchmark: train in blocks of STEP iterations
# (resuming from the checkpoint), export the preflop charts after every block,
# compare them with the previous block and with the MonkerSolver charts, and
# stop when the mean distance between two consecutive blocks is below
# THRESHOLD (or at MAX iterations).
#
# Usage: tools/monker_compare/run_step2.sh <config.json> <bucket dir> <monker chart dir> <output dir>
# Environment: STEP (4000), MAX (160000), THRESHOLD (0.01), THREADS (8), STORAGE (double).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CFG="$1"
BUCKETS="$2"
MONKER="$3"
OUT="$4"
STEP="${STEP:-4000}"
MAX="${MAX:-160000}"
THRESHOLD="${THRESHOLD:-0.01}"
THREADS="${THREADS:-8}"
STORAGE="${STORAGE:-double}"
BIN="${BIN:-out/build/windows-release-suite/benchmarks}"
RES="${RES:-out/preflop_blueprint_resources}"
mkdir -p "$OUT/charts"
log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" | tee -a "$OUT/run.log"; }
overall() { python -c "import json,sys; v=json.load(open(sys.argv[1]))['overall_mean_distance']; print('na' if v is None else v)" "$1"; }
same() { python -c "import json,sys; v=json.load(open(sys.argv[1]))['overall_same_main_action_share']; print('na' if v is None else v)" "$1"; }

log "step 2 start: config $CFG buckets $BUCKETS step $STEP max $MAX threshold $THRESHOLD"
iteration=$STEP
previous=""
while [ "$iteration" -le "$MAX" ]; do
  resume=""
  [ -f "$OUT/state.ckpt" ] && resume="--resume"
  started=$(date +%s)
  "$BIN/gtosd_preflop_blueprint_train.exe" --config "$CFG" --resources-dir "$RES" --buckets-dir "$BUCKETS" \
    --board-class-rows --threads "$THREADS" --table-storage "$STORAGE" --eval-every 0 \
    --batch 32 --partition-target 64 --scheme dcfr --update alternating --batch-policy-refresh \
    --lazy-discount --progress-every 500 \
    --iterations "$iteration" --checkpoint "$OUT/state.ckpt" --policy-out "$OUT/policy.bin" $resume \
    > "$OUT/train_$iteration.jsonl" 2> "$OUT/train_$iteration.stderr.log"
  rc=$?
  trained=$(date +%s)
  if [ $rc -ne 0 ]; then
    log "training failed at $iteration (exit $rc)"
    tail -n 5 "$OUT/train_$iteration.stderr.log" | tee -a "$OUT/run.log"
    exit 1
  fi
  charts="$OUT/charts/it_$iteration"
  "$BIN/gtosd_preflop_blueprint_monker_charts.exe" --config "$CFG" --policy "$OUT/policy.bin" \
    --output-dir "$charts" > "$charts.log" 2>&1 || { log "chart export failed at $iteration"; exit 1; }
  rm -f "$OUT/policy.bin"
  python tools/monker_compare/compare_charts.py "$charts" "$MONKER" --json "$charts/vs_monker.json" > "$charts/vs_monker.txt"
  change="na"
  if [ -n "$previous" ]; then
    python tools/monker_compare/compare_charts.py "$charts" "$OUT/charts/it_$previous" --json "$charts/vs_previous.json" > "$charts/vs_previous.txt"
    change=$(overall "$charts/vs_previous.json")
  fi
  log "iteration $iteration: train $((trained - started)) s, total $(( $(date +%s) - started )) s, change vs previous $change, vs monker distance $(overall "$charts/vs_monker.json") same main action $(same "$charts/vs_monker.json")"
  if [ "$change" != "na" ] && python -c "import sys; sys.exit(0 if float(sys.argv[1]) < float(sys.argv[2]) else 1)" "$change" "$THRESHOLD"; then
    log "STABLE at $iteration (change $change < $THRESHOLD)"
    exit 0
  fi
  previous=$iteration
  iteration=$((iteration + STEP))
done
log "reached MAX $MAX without stability"
exit 0
