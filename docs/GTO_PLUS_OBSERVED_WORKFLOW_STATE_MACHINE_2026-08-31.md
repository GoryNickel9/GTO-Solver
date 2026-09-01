# GTO+ observed workflow state machine — final 2026-09-01

## Scope

This state machine records externally observable behavior from the authorized
GTO+ 1.6.9 installation. It does not infer an internal algorithm, storage
layout, iteration count, precision, or best-response cadence.

The application remains `UIA_NONE`: accessibility exposes the top-level window
but not semantic child controls. A manual start marker is therefore required.

## Valid observed path

```text
PROCESS_NOT_RUNNING
  -> STARTING                         verified executable plus exact read-only project copy
  -> MAIN_WINDOW_READY                responsive title "GTOSD3 - GTO"
  -> PROJECT_LOADED                   exact original/copy SHA-256 identity
  -> READY_TO_SOLVE                   TST state visible; target 1%; 8 threads
  -> OBSERVER_ARMED                   immutable run directory and stale baseline captured
  -> [manual Run Solver]
  -> SOLVING                          new native progress generation plus CPU/memory activity
  -> TARGET_NOT_REACHED               native progress values >= 1%
  -> TARGET_FIRST_OBSERVED_BELOW_1    0.145654 ante / 16 ante = 0.9103375%
  -> SOLVE_COMPLETING                 target reached; native UI still finalizing
  -> SOLUTION_CONSULTABLE             native dEV/time, strategy view and Run Solver available
  -> COPY_INTEGRITY_VERIFIED          original and read-only copy unchanged
  -> [manual close without save]
  -> PROCESS_NOT_RUNNING
```

The project contains a previously solved strategy and stale `0.146 / 0.91%`.
`READY_TO_SOLVE` is not inferred from that value. A run becomes current only
after the native progress file begins a new generation with a lower line count.

## State evidence

| State | Observable | Confidence | Limitation |
|---|---|---:|---|
| `PROCESS_NOT_RUNNING` | no GTO process | high | none |
| `STARTING` | process from verified executable and project argument | high | external application |
| `MAIN_WINDOW_READY` | responsive exact-title window | high | no semantic children |
| `PROJECT_LOADED` | exact file/copy hashes; board and actions visible | high | remaining serialized fields treated as project identity |
| `READY_TO_SOLVE` | target `1%`, 8 threads, TST state visible | high visually | no UIA selector |
| `OBSERVER_ARMED` | manifest and immutable output directory | high | exact click timestamp absent |
| `SOLVING` | native progress reset/new generation and resource activity | high | manual action required |
| `TARGET_NOT_REACHED` | current native trace down to `1.1290375%` | high | percentage derived from 16-ante pot |
| `TARGET_FIRST_OBSERVED_BELOW_1` | current value `0.9103375%` | high | generation-to-cross timing is diagnostic |
| `SOLVE_COMPLETING` | process remains active after crossing | medium | CPU/WS alone are not completion |
| `SOLUTION_CONSULTABLE` | native `Time`, dEV, strategy view and start control visible | high | time read visually |
| `COPY_INTEGRITY_VERIFIED` | original/copy SHA-256 unchanged | high | none |
| `CLOSING` | manual close without save | high | agent cannot close reliably |
| `ERROR` | not observed in valid runs | high | invalid runs retained separately |

## Transition guards

| Transition | Guard |
|---|---|
| start -> loaded | exact read-only copy and original/copy SHA-256 equality |
| loaded -> ready | board, action state, target and thread count visible |
| ready -> solving | observer armed before manual action; new progress generation required |
| solving -> crossing | raw trace retained; strict parsed dEV `<1.0%` |
| crossing -> consultable | native completion panel plus visible strategy state |
| consultable -> closing | hash equality checked; no save |

CPU idle, process responsiveness, stable dEV and memory release remain
diagnostic. They are not accepted by themselves as `SOLUTION_CONSULTABLE`.

## Invalid-path adjudication

`tst-controlled-manual-20260901-07` entered `SOLVING` before
`OBSERVER_ARMED`. It remained immutable, was marked invalid and received one
replacement (`-08`). Its native time was diagnostic only and did not enter the
five-run statistics.
