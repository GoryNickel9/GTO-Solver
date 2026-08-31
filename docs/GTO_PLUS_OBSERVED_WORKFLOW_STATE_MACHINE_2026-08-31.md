# GTO+ observed workflow state machine — 2026-08-31

## Scope

This document records only externally observable behavior from the authorized
GTO+ 1.6.9 installation. It does not infer an internal algorithm, storage
layout, iteration count, precision, or best-response cadence.

The observed session cannot support an authoritative autonomous runner profile:
the accessibility tree exposed only the top-level window, the restored project
had no discoverable file identity, and a completed/consultable signal was not
available to the observer.

## Observed path

```text
PROCESS_NOT_RUNNING
  -> STARTING                         direct executable launch
  -> MAIN_WINDOW_READY                title and responsive top-level window
  -> RESTORED_SESSION_AMBIGUOUS       title "GTOSD3 - GTO", stale dEV present
  -> SOLVING                          manual Run Solver action plus new trace/activity
  -> TARGET_NOT_REACHED               native progress values >= 1%
  -> TARGET_FIRST_OBSERVED_BELOW_1    0.145654 ante / 16 ante = 0.9103375%
  -> SOLVE_COMPLETING_EXTERNAL_ONLY   CPU and memory later fall
  -> COMPLETION_UNVERIFIED            consultable solution not proved
```

`RESTORED_SESSION_AMBIGUOUS` intentionally replaces a claim of either
`READY_TO_SOLVE` or `SOLUTION_CONSULTABLE`: the stale displayed dEV and missing
project hash made those two states indistinguishable before the manual action.

## State evidence

| State | Observable | Confidence | Limitation |
|---|---|---:|---|
| `PROCESS_NOT_RUNNING` | no matching process before launch | high | none material |
| `STARTING` | process created from verified executable | high | no project argument |
| `MAIN_WINDOW_READY` | responsive top-level window | high | child controls absent from accessibility tree |
| `NO_PROJECT` | initial title `Untitled - GTO` | medium | state changed through native restore |
| `PROJECT_LOADING` | not independently exposed | none | transition not observable |
| `PROJECT_LOADED` | title changed to `GTOSD3 - GTO`; board visually `Ts Tc 9d` | medium | no file path or SHA-256 |
| `TREE_PREPARING` | not observed | none | no separate native signal |
| `READY_TO_SOLVE` | Run Solver panel visible | low | stale 0.91% dEV prevented proof of fresh-ready state |
| `SOLVING` | new native progress-file lifecycle, CPU and memory activity | high after manual action | exact click timestamp absent |
| `TARGET_NOT_REACHED` | native trace from 44.55% down to 1.1290375% | high | percent derived from 16-ante initial pot |
| `TARGET_FIRST_OBSERVED_BELOW_1` | first native value 0.145654 ante = 0.9103375% | high for crossing, invalid for timing | source belongs to current trace after file reset; Run timestamp missing |
| `SOLVE_COMPLETING` | memory release and CPU idle 11.5973211 s after crossing | diagnostic only | external idle is not completion authority |
| `SOLUTION_CONSULTABLE` | not verified | none | no UI state or read-only solution query |
| `ERROR` | not observed | high | process remained responsive |
| `UNSAVED_CHANGES` | not observable | none | close was deliberately not attempted |
| `CLOSING` | not exercised | none | avoids possible loss or prompt interaction |

## Transition guards required for a future valid run

| Transition | Required guard |
|---|---|
| load project | exact read-only project copy, original/copy SHA-256 equality |
| loaded -> ready | board, pot, stack, ranges, sizings, rake, target, threads and displayed memory all independently readable |
| ready -> solving | stable native selector with `InvokePattern`; monotonic timestamp immediately before invocation |
| solving -> crossing | current-run activity proven; raw dEV retained; strict parsed value `< 1.0%` |
| crossing -> consultable | at least one native completion control plus a read-only solution query |
| consultable -> closing | copy hash checked; controlled close; process exit verified |

CPU idle, process responsiveness, a stable dEV, and memory release are never
accepted by themselves as `SOLUTION_CONSULTABLE`.
