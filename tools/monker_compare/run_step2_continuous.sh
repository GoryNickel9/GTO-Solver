#!/bin/bash
# MonkerSolver step 2 on one benchmark with a single training process: the
# trainer writes the preflop charts every STEP iterations without stopping
# (--chart-every), this script compares every snapshot with the previous one
# and with the MonkerSolver charts, and creates the stop file when the mean
# distance between two consecutive snapshots is below THRESHOLD; the trainer
# then saves its state and policy once and exits. A full checkpoint is also
# written every CHECKPOINT_EVERY iterations so a crash loses little work; a
# rerun with the same output directory resumes from it.
#
# Usage: tools/monker_compare/run_step2_continuous.sh <config.json> <bucket dir> <monker chart dir> <output dir>
# (relative paths are relative to the repository root)
# Environment: STEP (4000), MAX (160000), THRESHOLD (0.01), CHECKPOINT_EVERY (20000),
#              THREADS (8), STORAGE (double), BIN, RES, TRAIN_ARGS (extra trainer
#              arguments, for example "--seed 2").
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
CHECKPOINT_EVERY="${CHECKPOINT_EVERY:-20000}"
THREADS="${THREADS:-8}"
STORAGE="${STORAGE:-double}"
BIN="${BIN:-out/build/windows-release-suite/benchmarks}"
RES="${RES:-out/preflop_blueprint_resources}"
TRAIN_ARGS="${TRAIN_ARGS:-}"
mkdir -p "$OUT/charts"
rm -f "$OUT/STOP"
log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" | tee -a "$OUT/run.log"; }
field() { python -c "import json,sys; v=json.load(open(sys.argv[1]))[sys.argv[2]]; print('na' if v is None else v)" "$1" "$2"; }

resume=""
[ -f "$OUT/state.ckpt" ] && resume="--resume"
log "step 2 continuous start: config $CFG buckets $BUCKETS step $STEP max $MAX threshold $THRESHOLD $TRAIN_ARGS $resume"
"$BIN/gtosd_preflop_blueprint_train.exe" --config "$CFG" --resources-dir "$RES" --buckets-dir "$BUCKETS" \
  --board-class-rows --threads "$THREADS" --table-storage "$STORAGE" --eval-every 0 \
  --batch 32 --partition-target 64 --scheme dcfr --update alternating --batch-policy-refresh \
  --lazy-discount --progress-every 500 --iterations "$MAX" \
  --checkpoint "$OUT/state.ckpt" --checkpoint-every "$CHECKPOINT_EVERY" --policy-out "$OUT/policy.bin" \
  --chart-every "$STEP" --chart-dir "$OUT/charts" --stop-file "$OUT/STOP" $TRAIN_ARGS $resume \
  >> "$OUT/train.jsonl" 2>> "$OUT/train.stderr.log" &
trainer=$!

previous=""
# Snapshots already present (a resumed run) count as processed.
for existing in $(ls "$OUT/charts" 2>/dev/null | sed -n 's/^it_\([0-9]*\)$/\1/p' | sort -n); do
  previous=$existing
done
started=$(date +%s)
while true; do
  alive=1
  kill -0 "$trainer" 2>/dev/null || alive=0
  for iteration in $(ls "$OUT/charts" 2>/dev/null | sed -n 's/^it_\([0-9]*\)$/\1/p' | sort -n); do
    if [ -n "$previous" ] && [ "$iteration" -le "$previous" ]; then
      continue
    fi
    charts="$OUT/charts/it_$iteration"
    python tools/monker_compare/compare_charts.py "$charts" "$MONKER" --json "$charts/vs_monker.json" > "$charts/vs_monker.txt"
    change="na"
    if [ -n "$previous" ]; then
      if python tools/monker_compare/compare_charts.py "$charts" "$OUT/charts/it_$previous" --json "$charts/vs_previous.json" > "$charts/vs_previous.txt" 2>&1; then
        change=$(field "$charts/vs_previous.json" overall_mean_distance 2>/dev/null)
      fi
      [ -z "$change" ] && change="na"
      [ "$change" = "na" ] && log "comparison with it_$previous failed at $iteration"
    fi
    log "iteration $iteration: elapsed $(( $(date +%s) - started )) s, change vs previous $change, vs monker distance $(field "$charts/vs_monker.json" overall_mean_distance) range difference $(field "$charts/vs_monker.json" overall_range_difference)"
    previous=$iteration
    if [ "$change" != "na" ] && [ ! -f "$OUT/STOP" ] && python -c "import sys; sys.exit(0 if float(sys.argv[1]) < float(sys.argv[2]) else 1)" "$change" "$THRESHOLD"; then
      log "STABLE at $iteration (change $change < $THRESHOLD): stopping the trainer"
      touch "$OUT/STOP"
    fi
  done
  [ $alive -eq 0 ] && break
  sleep 15
done
wait "$trainer"
rc=$?
if [ $rc -ne 0 ]; then
  log "trainer failed (exit $rc)"
  tail -n 5 "$OUT/train.stderr.log" | tee -a "$OUT/run.log"
  exit 1
fi
log "trainer finished: $(grep -o "PREFLOP_BLUEPRINT_TRAIN=[A-Z_]*" "$OUT/train.jsonl" | tail -n 1), last snapshot $previous, stop event $(grep -o "\"event\":\"stop_file\",\"iteration\":[0-9]*" "$OUT/train.jsonl" | tail -n 1)"
exit 0
