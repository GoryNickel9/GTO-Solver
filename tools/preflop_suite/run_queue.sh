#!/bin/bash
# Sequential queue runner for the suite: one run at a time, never concurrent
# with another solver process. Each queue line is
#   <version> <scenario> <rep> <exe-dir>
# Completed runs (manifest status COMPLETE) are skipped, so the queue file can
# be edited and the runner restarted at any time. Usage:
#   bash tools/preflop_suite/run_queue.sh <queue-file> [<wait-for-file-containing-BATCH_DONE>]
# Environment:
#   SUITE_WINDOW=HH:MM-HH:MM  measured runs start only inside this local-time window
#                             (default 01:00-09:00, the hours when the machine is idle);
#                             a run also has to be expected to finish inside the window.
#   SUITE_WINDOW=off          disable the window (diagnostic runs, dedicated machines).
set -u
ROOT="/c/Users/GoryNickel/Documents/GitHub/GTO-Solver"
cd "$ROOT"
QUEUE="$1"
WAIT_FOR="${2:-}"
WINDOW="${SUITE_WINDOW:-01:00-09:00}"
LOG="out/suite/queue_runner.log"
log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG"; }

# Expected duration of one run (training plus exact certification, baseline worst case)
# in minutes, used to refuse a start that would end outside the window.
expected_minutes() {
  case "$1" in
    HU10) echo 40;;
    *) echo 100;;
  esac
}

# Blocks until the current local time is inside the window with room for the run.
wait_for_window() {
  local scenario="$1"
  [ "$WINDOW" = "off" ] && return 0
  local start_h=${WINDOW%%:*}; local rest=${WINDOW#*:}
  local start_m=${rest%%-*}; rest=${rest#*-}
  local end_h=${rest%%:*}; local end_m=${rest#*:}
  local start=$((10#$start_h * 60 + 10#$start_m))
  local end=$((10#$end_h * 60 + 10#$end_m))
  local need; need=$(expected_minutes "$scenario")
  local announced=0
  while true; do
    local now=$((10#$(date +%H) * 60 + 10#$(date +%M)))
    if [ "$now" -ge "$start" ] && [ $((now + need)) -le "$end" ]; then
      return 0
    fi
    if [ "$announced" -eq 0 ]; then
      log "outside the measurement window $WINDOW (or not enough time left for $scenario): waiting"
      announced=1
    fi
    sleep 300
  done
}

log "queue runner started with $QUEUE (pid $$, window $WINDOW)"
if [ -n "$WAIT_FOR" ]; then
  log "waiting for BATCH_DONE in $WAIT_FOR"
  while ! grep -q BATCH_DONE "$WAIT_FOR" 2>/dev/null; do sleep 60; done
  log "wait over"
fi
while true; do
  next=""
  # tr strips carriage returns so a CRLF queue file does not corrupt the fields.
  while read -r version scenario rep exedir; do
    [ -z "$version" ] && continue
    case "$version" in \#*) continue;; esac
    manifest="out/suite/$version/$scenario/rep$rep/manifest.json"
    if [ -f "$manifest" ] && grep -q '"status": "COMPLETE"' "$manifest"; then continue; fi
    next="$version $scenario $rep $exedir"
    break
  done < <(tr -d '\r' < "$QUEUE")
  if [ -z "$next" ]; then log "queue empty"; break; fi
  set -- $next
  version=$1; scenario=$2; rep=$3; exedir=$4
  if [ ! -d "$exedir" ]; then log "missing exe dir $exedir for $version; stopping"; break; fi
  wait_for_window "$scenario"
  # tasklist truncates image names to 25 characters: match the truncated prefixes
  # of gtosd_preflop_blueprint_train.exe and gtosd_preflop_blueprint_certify.exe.
  while tasklist 2>/dev/null | grep -qi "gtosd_preflop_blueprint_t\|gtosd_preflop_blueprint_c"; do
    sleep 60
  done
  log "running $version $scenario rep$rep"
  mkdir -p "out/suite/$version"
  python tools/preflop_suite/suite.py run --version "$version" --exe-dir "$exedir" --scenario "$scenario" --rep "$rep" --overwrite >> "out/suite/$version/driver_queue.log" 2>&1
  rc=$?
  log "$version $scenario rep$rep exit=$rc"
  if [ $rc -ne 0 ] && ! grep -q '"status"' "out/suite/$version/$scenario/rep$rep/manifest.json" 2>/dev/null; then
    log "run could not start ($rc); stopping the queue"
    break
  fi
done
log "queue runner finished"
