#!/bin/bash
# Sequential queue runner for the suite: one run at a time, never concurrent
# with another solver process. Each queue line is
#   <version> <scenario> <rep> <exe-dir>
# Completed runs (manifest status COMPLETE) are skipped, so the queue file can
# be edited and the runner restarted at any time. Usage:
#   bash tools/preflop_suite/run_queue.sh <queue-file> [<wait-for-file-containing-BATCH_DONE>]
# Environment:
#   SUITE_WINDOW=HH:MM-HH:MM  measured runs start only inside this local-time window
#                             (default 00:00-20:00: the machine is available 00:00-21:00 and
#                             the last hour is reserved to builds, tests and probes); a run
#                             also has to be expected to finish inside the window.
#   SUITE_WINDOW=off          disable the window (diagnostic runs, dedicated machines).
#   SUITE_PAUSE=HH:MM-HH:MM   optional daily pause inside the window (default off): a run
#                             starts only if it is expected to finish before the pause or
#                             after the pause has ended.
#   SUITE_NOT_BEFORE="YYYY-MM-DD HH:MM"  nothing starts before this local date and time
#                             (a rule that takes effect on a later day).
set -u
ROOT="/c/Users/GoryNickel/Documents/GitHub/GTO-Solver"
cd "$ROOT"
QUEUE="$1"
WAIT_FOR="${2:-}"
WINDOW="${SUITE_WINDOW:-00:00-20:00}"
PAUSE="${SUITE_PAUSE:-off}"
NOT_BEFORE="${SUITE_NOT_BEFORE:-}"
LOG="out/suite/queue_runner.log"
log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG"; }

# Expected duration of one run (training plus exact certification, baseline worst case)
# in minutes, used to refuse a start that would end outside the window or inside the pause.
expected_minutes() {
  case "$1" in
    HU10) echo 40;;
    *) echo 100;;
  esac
}

minutes_of() {  # HH:MM -> minutes since midnight
  local h=${1%%:*}; local m=${1#*:}
  echo $((10#$h * 60 + 10#$m))
}

# Blocks until the run may start: after SUITE_NOT_BEFORE, inside the window with room
# for the expected duration, and not overlapping the pause.
wait_for_window() {
  local scenario="$1"
  local need; need=$(expected_minutes "$scenario")
  local start=0 end=1440 pause_start=-1 pause_end=-1 not_before=0
  if [ "$WINDOW" != "off" ]; then
    start=$(minutes_of "${WINDOW%-*}"); end=$(minutes_of "${WINDOW#*-}")
  fi
  if [ "$PAUSE" != "off" ]; then
    pause_start=$(minutes_of "${PAUSE%-*}"); pause_end=$(minutes_of "${PAUSE#*-}")
  fi
  [ -n "$NOT_BEFORE" ] && not_before=$(date -d "$NOT_BEFORE" +%s)
  local announced=0
  while true; do
    local now=$((10#$(date +%H) * 60 + 10#$(date +%M)))
    local ok=1
    [ "$(date +%s)" -lt "$not_before" ] && ok=0
    if [ "$now" -lt "$start" ] || [ $((now + need)) -gt "$end" ]; then ok=0; fi
    if [ "$pause_start" -ge 0 ] && [ $((now + need)) -gt "$pause_start" ] && [ "$now" -lt "$pause_end" ]; then
      ok=0
    fi
    [ "$ok" -eq 1 ] && return 0
    if [ "$announced" -eq 0 ]; then
      log "waiting for a start slot: window $WINDOW, pause $PAUSE, not before '${NOT_BEFORE:-now}', $scenario needs $need min"
      announced=1
    fi
    sleep 300
  done
}

log "queue runner started with $QUEUE (pid $$, window $WINDOW, pause $PAUSE, not before '${NOT_BEFORE:-now}')"
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
  wait_for_window "$scenario"
  # A version whose executables are not archived yet is waited for, not skipped:
  # the queue is edited while candidates are being built.
  announced=0
  while [ ! -d "$exedir" ]; do
    if [ "$announced" -eq 0 ]; then log "exe dir $exedir for $version missing: waiting"; announced=1; fi
    sleep 300
  done
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
