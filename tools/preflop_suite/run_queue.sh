#!/bin/bash
# Sequential queue runner for the suite: one run at a time, never concurrent
# with another solver process. Each queue line is
#   <version> <scenario> <rep> <exe-dir>
# Completed runs (manifest status COMPLETE) are skipped, so the queue file can
# be edited and the runner restarted at any time. Usage:
#   bash tools/preflop_suite/run_queue.sh <queue-file> [<wait-for-file-containing-BATCH_DONE>]
set -u
ROOT="/c/Users/GoryNickel/Documents/GitHub/GTO-Solver"
cd "$ROOT"
QUEUE="$1"
WAIT_FOR="${2:-}"
LOG="out/suite/queue_runner.log"
log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG"; }
log "queue runner started with $QUEUE (pid $$)"
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
