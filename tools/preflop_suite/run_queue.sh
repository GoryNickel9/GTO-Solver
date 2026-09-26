#!/bin/bash
# Sequential queue runner for the suite: one run at a time, never concurrent
# with another solver process. Queue lines:
#   <version> <scenario> <rep> <exe-dir> [key=value ...]
#       keys: minutes=N (expected duration, default 40 for HU10 and 100 otherwise),
#             iterations=N (research override: --iterations N, the run is marked deviating),
#             extra=a,b,c (extra trainer flags passed through --train-extra, comma-separated)
#   job <label> <minutes> <command ...>
#       a preparation job (data tables, resources) run through bash -c when no solver
#       process is alive; done when out/suite/jobs/<label>.done exists (written on exit 0).
# Completed runs (manifest status COMPLETE) and done jobs are skipped, so the queue file
# can be edited and the runner restarted at any time; a run whose manifest says RUNNING
# (a driver started by a previous runner instance) is waited for, not repeated. Usage:
#   bash tools/preflop_suite/run_queue.sh <queue-file> [<wait-for-file-containing-BATCH_DONE>]
# Environment:
#   SUITE_WINDOW=HH:MM-HH:MM  runs start only inside this local-time window (default
#                             00:00-20:00: the machine is available 00:00-21:00 and the last
#                             hour is reserved to builds, tests and probes); a run also has
#                             to be expected to finish inside the window.
#   SUITE_WINDOW=off          disable the window (diagnostic runs, dedicated machines).
#   SUITE_PAUSE=HH:MM-HH:MM   optional daily pause inside the window (default off): a run
#                             starts only if it is expected to finish before the pause or
#                             after the pause has ended.
#   SUITE_NOT_BEFORE="YYYY-MM-DD HH:MM"  nothing starts before this local date and time.
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

# Blocks until a run of `need` minutes may start: after SUITE_NOT_BEFORE, inside the
# window with room for the expected duration, and not overlapping the pause.
wait_for_slot() {
  local what="$1" need="$2"
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
      log "waiting for a start slot: window $WINDOW, pause $PAUSE, not before '${NOT_BEFORE:-now}', $what needs $need min"
      announced=1
    fi
    sleep 300
  done
}

# tasklist truncates image names to 25 characters: every solver tool starts with the
# prefix gtosd_preflop_blueprint_ (train, certify, history rows, bucket tables, ...).
solver_alive() { tasklist 2>/dev/null | grep -qi "gtosd_preflop_blueprint"; }
wait_for_no_solver() { while solver_alive; do sleep 60; done; }

manifest_status() {  # prints COMPLETE, RUNNING, other status, or MISSING
  local manifest="$1"
  if [ ! -f "$manifest" ]; then echo MISSING; return; fi
  local status
  status=$(grep -o '"status": "[A-Z_]*"' "$manifest" | head -1 | cut -d'"' -f4)
  echo "${status:-UNKNOWN}"
}

# A run started by a previous runner instance (manifest RUNNING) is waited for. A RUNNING
# manifest with no solver process for ten minutes is stale (crashed driver) and is repeated.
wait_if_running() {
  local manifest="$1" idle=0
  while [ "$(manifest_status "$manifest")" = "RUNNING" ]; do
    if solver_alive; then idle=0; else idle=$((idle + 1)); fi
    [ "$idle" -ge 10 ] && { log "stale RUNNING manifest $manifest: repeating the run"; return; }
    sleep 60
  done
}

log "queue runner started with $QUEUE (pid $$, window $WINDOW, pause $PAUSE, not before '${NOT_BEFORE:-now}')"
if [ -n "$WAIT_FOR" ]; then
  log "waiting for BATCH_DONE in $WAIT_FOR"
  while ! grep -q BATCH_DONE "$WAIT_FOR" 2>/dev/null; do sleep 60; done
  log "wait over"
fi
mkdir -p out/suite/jobs
while true; do
  next=""
  # tr strips carriage returns so a CRLF queue file does not corrupt the fields.
  while read -r line; do
    set -- $line
    [ $# -eq 0 ] && continue
    case "$1" in \#*) continue;; esac
    if [ "$1" = "job" ]; then
      [ -f "out/suite/jobs/$2.done" ] && continue
      next="$line"; break
    fi
    [ $# -lt 4 ] && continue
    manifest="out/suite/$1/$2/rep$3/manifest.json"
    [ "$(manifest_status "$manifest")" = "COMPLETE" ] && continue
    next="$line"; break
  done < <(tr -d '\r' < "$QUEUE")
  if [ -z "$next" ]; then log "queue empty"; break; fi
  set -- $next
  if [ "$1" = "job" ]; then
    label=$2; need=$3; shift 3; command="$*"
    wait_for_slot "job $label" "$need"
    wait_for_no_solver
    log "running job $label: $command"
    bash -c "$command" >> "out/suite/jobs/$label.log" 2>&1
    rc=$?
    log "job $label exit=$rc"
    if [ $rc -eq 0 ]; then date '+%Y-%m-%d %H:%M:%S' > "out/suite/jobs/$label.done"; else log "job $label failed; stopping the queue"; break; fi
    continue
  fi
  version=$1; scenario=$2; rep=$3; exedir=$4; shift 4
  need=$(expected_minutes "$scenario"); iterations=""; extra=""
  for field in "$@"; do
    case "$field" in
      minutes=*) need=${field#minutes=};;
      iterations=*) iterations=${field#iterations=};;
      extra=*) extra=${field#extra=};;
    esac
  done
  manifest="out/suite/$version/$scenario/rep$rep/manifest.json"
  wait_if_running "$manifest"
  [ "$(manifest_status "$manifest")" = "COMPLETE" ] && continue
  wait_for_slot "$version $scenario" "$need"
  # A version whose executables are not archived yet is waited for, not skipped:
  # the queue is edited while candidates are being built.
  announced=0
  while [ ! -d "$exedir" ]; do
    if [ "$announced" -eq 0 ]; then log "exe dir $exedir for $version missing: waiting"; announced=1; fi
    sleep 300
  done
  wait_for_no_solver
  # The manifest may have been completed by another driver while waiting.
  [ "$(manifest_status "$manifest")" = "COMPLETE" ] && continue
  args=()
  [ -n "$iterations" ] && args+=(--iterations "$iterations")
  if [ -n "$extra" ]; then args+=(--train-extra); IFS=',' read -r -a extra_args <<< "$extra"; args+=("${extra_args[@]}"); fi
  log "running $version $scenario rep$rep${iterations:+ (iterations $iterations)}${extra:+ (extra $extra)}"
  mkdir -p "out/suite/$version"
  python tools/preflop_suite/suite.py run --version "$version" --exe-dir "$exedir" --scenario "$scenario" --rep "$rep" --overwrite "${args[@]}" >> "out/suite/$version/driver_queue.log" 2>&1
  rc=$?
  log "$version $scenario rep$rep exit=$rc"
  if [ $rc -ne 0 ] && ! grep -q '"status"' "$manifest" 2>/dev/null; then
    log "run could not start ($rc); stopping the queue"
    break
  fi
done
log "queue runner finished"
