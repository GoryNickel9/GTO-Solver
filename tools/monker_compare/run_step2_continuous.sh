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
#              arguments, for example "--seed 2"), POLICY_SNAPSHOTS (1: every snapshot
#              also holds the average policy of its iteration, charts/it_<N>/policy.bin,
#              read by tools/monker_compare/convergence_curve.py; one policy file per
#              snapshot, about half the state size each), POLICY_SNAPSHOT_EVERY (with
#              POLICY_SNAPSHOTS=1: keep a policy only every N iterations, a multiple of
#              STEP), POLICY_SNAPSHOT_RESERVE_GB (free space kept on the disk beyond the
#              next checkpoint and the final policy, default 1; a policy that does not
#              fit is skipped and logged, the training goes on).
#              STOP_METRIC (overall: the mean change of every chart; non_all_in: of the
#              charts that do not face an all-in; all_in: of those that do; a chart faces
#              an all-in when its actions are exactly Call and Fold, compare_charts.py),
#              MIN_ITERATIONS (no stop before this iteration, default 0), PAUSE (1: pass
#              --pause-file <output dir>/PAUSE, see "Daily stop"; default 0 heads-up, so a
#              trainer binary older than the option still runs, 1 for 3 players).
#
# Three players (phase 3 spec, sections 4.3, 5.3, 5.4; the player count is read
# from the configuration): the defaults become STOP_METRIC=non_all_in,
# THRESHOLD=0.008, MIN_ITERATIONS=16000, MAX=48000 (the cap), CHECKPOINT_EVERY=0 (no
# periodic checkpoint: a 15x4 3WAY50 checkpoint is 13.6 GB), POLICY_SNAPSHOTS=1,
# POLICY_SNAPSHOT_EVERY=16000 and POLICY_SNAPSHOT_RESERVE_GB=40; an explicit
# environment value still wins. Every snapshot logs the change and the distance
# from MonkerSolver over all charts, the charts facing an all-in and the others.
#
# Daily stop (3 players, or PAUSE=1): create <output dir>/PAUSE (for example at
# 19:40 from the queue script). The trainer then writes its checkpoint only (no
# policy.bin) and exits PAUSED; rerunning the same command resumes from the
# checkpoint. policy.bin is written only at the real end of the run: the stop
# rule (this script creates <output dir>/STOP) or the MAX cap.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
CFG="$1"
BUCKETS="$2"
MONKER="$3"
OUT="$4"
PLAYERS=$(python -c "import json,sys; print(json.load(open(sys.argv[1])).get('player_count', 2))" "$CFG")
if [ "$PLAYERS" = "3" ]; then
  STOP_METRIC="${STOP_METRIC:-non_all_in}"
  THRESHOLD="${THRESHOLD:-0.008}"
  MIN_ITERATIONS="${MIN_ITERATIONS:-16000}"
  MAX="${MAX:-48000}"
  CHECKPOINT_EVERY="${CHECKPOINT_EVERY:-0}"
  POLICY_SNAPSHOTS="${POLICY_SNAPSHOTS:-1}"
  POLICY_SNAPSHOT_EVERY="${POLICY_SNAPSHOT_EVERY-16000}"
  POLICY_SNAPSHOT_RESERVE_GB="${POLICY_SNAPSHOT_RESERVE_GB-40}"
  PAUSE="${PAUSE:-1}"
fi
PAUSE="${PAUSE:-0}"
STEP="${STEP:-4000}"
MAX="${MAX:-160000}"
THRESHOLD="${THRESHOLD:-0.01}"
STOP_METRIC="${STOP_METRIC:-overall}"
MIN_ITERATIONS="${MIN_ITERATIONS:-0}"
CHECKPOINT_EVERY="${CHECKPOINT_EVERY:-20000}"
THREADS="${THREADS:-8}"
STORAGE="${STORAGE:-double}"
BIN="${BIN:-out/build/windows-release-suite/benchmarks}"
RES="${RES:-out/preflop_blueprint_resources}"
TRAIN_ARGS="${TRAIN_ARGS:-}"
POLICY_SNAPSHOTS="${POLICY_SNAPSHOTS:-0}"
POLICY_SNAPSHOT_EVERY="${POLICY_SNAPSHOT_EVERY:-}"
POLICY_SNAPSHOT_RESERVE_GB="${POLICY_SNAPSHOT_RESERVE_GB:-}"
case "$STOP_METRIC" in
  overall) metric_field=overall_mean_distance ;;
  non_all_in) metric_field=non_all_in_mean_distance ;;
  all_in) metric_field=all_in_mean_distance ;;
  *) echo "unknown STOP_METRIC $STOP_METRIC (overall, non_all_in or all_in)" >&2; exit 2 ;;
esac
pause_args=""
[ "$PAUSE" = "1" ] && pause_args="--pause-file $OUT/PAUSE"
# Heads-up with the default metric keeps the former start and snapshot log lines.
detailed_log=0
{ [ "$PLAYERS" != "2" ] || [ "$STOP_METRIC" != "overall" ]; } && detailed_log=1
snapshot_args=""
if [ "$POLICY_SNAPSHOTS" = "1" ]; then
  snapshot_args="--policy-snapshots"
  [ -n "$POLICY_SNAPSHOT_EVERY" ] && snapshot_args="$snapshot_args --policy-snapshot-every $POLICY_SNAPSHOT_EVERY"
  [ -n "$POLICY_SNAPSHOT_RESERVE_GB" ] && snapshot_args="$snapshot_args --policy-snapshot-reserve-gb $POLICY_SNAPSHOT_RESERVE_GB"
fi
mkdir -p "$OUT/charts"
rm -f "$OUT/STOP" "$OUT/PAUSE"
log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" | tee -a "$OUT/run.log"; }
field() { python -c "import json,sys; v=json.load(open(sys.argv[1])).get(sys.argv[2]); print('na' if v is None else v)" "$1" "$2"; }
# The three means of a compare_charts.py report: all charts, facing all-in, not facing all-in.
means() { echo "$(field "$1" overall_mean_distance) (all-in $(field "$1" all_in_mean_distance), non-all-in $(field "$1" non_all_in_mean_distance))"; }

resume=""
[ -f "$OUT/state.ckpt" ] && resume="--resume"
if [ "$detailed_log" = "1" ]; then
  log "step 2 continuous start: config $CFG buckets $BUCKETS step $STEP max $MAX threshold $THRESHOLD metric $STOP_METRIC min $MIN_ITERATIONS checkpoint_every $CHECKPOINT_EVERY players $PLAYERS $TRAIN_ARGS $snapshot_args $pause_args $resume"
else
  log "step 2 continuous start: config $CFG buckets $BUCKETS step $STEP max $MAX threshold $THRESHOLD $TRAIN_ARGS $snapshot_args $resume"
fi
"$BIN/gtosd_preflop_blueprint_train.exe" --config "$CFG" --resources-dir "$RES" --buckets-dir "$BUCKETS" \
  --board-class-rows --threads "$THREADS" --table-storage "$STORAGE" --eval-every 0 \
  --batch 32 --partition-target 64 --scheme dcfr --update alternating --batch-policy-refresh \
  --lazy-discount --progress-every 500 --iterations "$MAX" \
  --checkpoint "$OUT/state.ckpt" --checkpoint-every "$CHECKPOINT_EVERY" --policy-out "$OUT/policy.bin" \
  --chart-every "$STEP" --chart-dir "$OUT/charts" --stop-file "$OUT/STOP" $pause_args \
  $snapshot_args $TRAIN_ARGS $resume \
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
    changes="na"
    if [ -n "$previous" ]; then
      if python tools/monker_compare/compare_charts.py "$charts" "$OUT/charts/it_$previous" --json "$charts/vs_previous.json" > "$charts/vs_previous.txt" 2>&1; then
        change=$(field "$charts/vs_previous.json" "$metric_field" 2>/dev/null)
        changes=$(means "$charts/vs_previous.json" 2>/dev/null)
      fi
      [ -z "$change" ] && change="na"
      [ "$change" = "na" ] && log "comparison with it_$previous failed at $iteration"
    fi
    if [ "$detailed_log" = "1" ]; then
      log "iteration $iteration: elapsed $(( $(date +%s) - started )) s, change vs previous $changes, stop metric $STOP_METRIC $change, vs monker distance $(means "$charts/vs_monker.json") range difference $(field "$charts/vs_monker.json" overall_range_difference)"
    else
      log "iteration $iteration: elapsed $(( $(date +%s) - started )) s, change vs previous $change, vs monker distance $(field "$charts/vs_monker.json" overall_mean_distance) range difference $(field "$charts/vs_monker.json" overall_range_difference)"
    fi
    previous=$iteration
    if [ "$change" != "na" ] && [ ! -f "$OUT/STOP" ] && [ "$iteration" -ge "$MIN_ITERATIONS" ] && python -c "import sys; sys.exit(0 if float(sys.argv[1]) < float(sys.argv[2]) else 1)" "$change" "$THRESHOLD"; then
      log "STABLE at $iteration ($STOP_METRIC change $change < $THRESHOLD): stopping the trainer"
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
status=$(grep -o "PREFLOP_BLUEPRINT_TRAIN=[A-Z_]*" "$OUT/train.jsonl" | tail -n 1)
log "trainer finished: $status, last snapshot $previous, stop event $(grep -o "\"event\":\"stop_file\",\"iteration\":[0-9]*" "$OUT/train.jsonl" | tail -n 1)"
if [ "$status" = "PREFLOP_BLUEPRINT_TRAIN=PAUSED" ]; then
  log "paused (daily stop) at $(grep -o "\"event\":\"pause_file\",\"iteration\":[0-9]*" "$OUT/train.jsonl" | tail -n 1 | sed 's/.*://'): checkpoint only, no policy.bin; rerun the same command to resume"
fi
if [ "$POLICY_SNAPSHOTS" = "1" ]; then
  skipped=$(grep -o "\"event\":\"policy_snapshot_skipped\",\"iteration\":[0-9]*" "$OUT/train.jsonl" | sed 's/.*://' | tr '\n' ' ' | sed 's/ $//')
  log "policy snapshots: skipped for disk space at iterations ${skipped:-none}, failed $(grep -c "\"event\":\"policy_snapshot_failed\"" "$OUT/train.jsonl")"
fi
exit 0
