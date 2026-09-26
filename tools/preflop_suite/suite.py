"""Common benchmark suite of the preflop blueprint solver.

Subcommands:
  resolve  generate the canonical fixtures from (stack, preflop_sizes, postflop_sizes)
           and, with --game-exe, record tree fingerprints and state layouts
  check    fail when two scenarios of one version differ in anything but the three
           scenario parameters (fixtures, executables, shared artifacts, run logs)
  run      run one scenario with one solver version under the common protocol,
           sampling private commit / working set peaks of every process
  report   aggregate the manifests of out/suite/<version>/<scenario>/rep<N>

Every path is relative to the repository root unless absolute.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wintypes
import datetime as dt
import hashlib
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

try:
    import psutil
except ImportError:  # pragma: no cover
    psutil = None

ROOT = Path(__file__).resolve().parents[2]
SUITE_PATH = ROOT / "benchmarks" / "suite" / "preflop_blueprint_suite.json"
FIXTURE_DIR = ROOT / "benchmarks" / "suite" / "fixtures"
RESOLVED_PATH = ROOT / "benchmarks" / "suite" / "resolved.json"
RUNS_ROOT = ROOT / "out" / "suite"

# Fields of a fixture that are derived from the three scenario parameters (and the
# scenario id). Everything else must be identical across the scenarios of a version.
SCENARIO_FIELDS = {
    "id",
    "effective_stack_units",
    "open_target_units",
    "response_target_units",
    "limp_response_target_units",
    "postflop_sizes_basis_points",
}

# Keys of the trainer "start" event that legitimately depend on the tree.
START_TREE_KEYS = {
    "config_id",
    "tree_fingerprint",
    "trainer_identity",
    "nodes",
    "decisions",
    "state_bytes",
    "units",
    "top_nodes",
    "largest_unit",
    "preparation_seconds",
    "history_map_resident_bytes",
    "memory_breakdown",
}
CERT_TREE_KEYS = {"config_id", "tree_fingerprint", "policy_fingerprint", "policy_source",
                  "preparation_seconds", "process_after_load"}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 22), b""):
            digest.update(block)
    return digest.hexdigest()


def load_suite() -> dict:
    return json.loads(SUITE_PATH.read_text(encoding="utf-8"))


def resolve_fixture(common: dict, scenario: dict) -> dict:
    """Pure function (common profile, three parameters, id) -> fixture document."""
    game = common["game"]
    catalog = common["size_catalog"]
    units = catalog["units_per_ante"]
    stack = scenario["stack_antes"] * units
    n_pre = scenario["preflop_sizes"]
    n_post = scenario["postflop_sizes"]
    targets = catalog["preflop_targets_units"]
    if not 1 <= n_pre <= len(targets):
        raise ValueError(f"{scenario['name']}: preflop_sizes must be in 1..{len(targets)}")
    selected = [target for target in targets[:n_pre] if target < stack]
    if not selected:
        raise ValueError(f"{scenario['name']}: the open target reaches the stack")
    open_targets = [selected[0]]
    response_targets = selected[1:2]
    post_table = catalog["postflop_sizes_by_count"]
    if str(n_post) not in post_table:
        raise ValueError(f"{scenario['name']}: postflop_sizes={n_post} is not in the catalog")
    fixture = {
        "schema": game["schema"],
        "id": scenario["id"],
        "monetary_contract_revision": game["monetary_contract_revision"],
        "ante_accounting": game["ante_accounting"],
        "preflop_target_basis": game["preflop_target_basis"],
        "player_count": game["player_count"],
        "positions": list(game["positions"]),
        "effective_stack_units": stack,
        "ante_units": game["ante_units"],
        "button_blind_units": game["button_blind_units"],
        "open_target_units": open_targets,
        "response_target_units": response_targets,
    }
    if response_targets:
        fixture["limp_response_target_units"] = []
    fixture.update({
        "allow_configured_incomplete_raise": game["allow_configured_incomplete_raise"],
        "postflop_sizes_basis_points": list(post_table[str(n_post)]),
        "postflop_minimum_bet_units": game["postflop_minimum_bet_units"],
        "include_all_in": game["include_all_in"],
        "raise_termination": game["raise_termination"],
        "rake_mode": game["rake_mode"],
    })
    return fixture


def run_game_report(game_exe: Path, fixture_path: Path) -> dict:
    """Tree statistics and per-row action columns (capacities 1/1/1)."""
    result = subprocess.run(
        [str(game_exe), "--config", str(fixture_path), "--flop", "1", "--turn", "1",
         "--river", "1", "--alt-flop", "200", "--alt-turn", "500", "--alt-river", "1000"],
        capture_output=True, text=True, cwd=ROOT)
    if result.returncode != 0:
        return {"error": (result.stderr or result.stdout).strip()}
    text = result.stdout.split("PREFLOP_BLUEPRINT_GAME=")[0]
    return json.loads(text)


def layout_estimate(report: dict, capacities: tuple[int, int, int]) -> dict:
    unit = report["layout_baseline"]
    columns = {
        "preflop": unit["entries_preflop"],
        "flop": unit["entries_flop"],
        "turn": unit["entries_turn"],
        "river": unit["entries_river"],
    }
    entries = {
        "preflop": columns["preflop"],
        "flop": columns["flop"] * capacities[0],
        "turn": columns["turn"] * capacities[1],
        "river": columns["river"] * capacities[2],
    }
    total = sum(entries.values())
    rows = {
        "preflop": report["preflop"]["decisions"] * 81,
        "flop": report["postflop"]["decisions_flop"] * capacities[0],
        "turn": report["postflop"]["decisions_turn"] * capacities[1],
        "river": report["postflop"]["decisions_river"] * capacities[2],
    }
    return {
        "action_columns_per_row": columns,
        "entries_by_street": entries,
        "entries": total,
        "rows_by_street": rows,
        "rows": sum(rows.values()),
        "double_table_bytes": total * 8,
        "two_double_tables_bytes": total * 16,
        "three_double_tables_bytes": total * 24,
        "lazy_timestamp_bytes_uint32": sum(rows.values()) * 4,
    }


def cmd_resolve(arguments: argparse.Namespace) -> int:
    suite = load_suite()
    common = suite["common"]
    FIXTURE_DIR.mkdir(parents=True, exist_ok=True)
    capacities = (7585, 222865, 1539270)
    ram = psutil.virtual_memory().total if psutil else 0
    budget = common["measurement"]["budget_bytes"]
    resolved = {"schema": "gtosd.preflop_blueprint_benchmark_suite_resolved.v1",
                "generated": dt.datetime.now().isoformat(timespec="seconds"),
                "suite_sha256": sha256_file(SUITE_PATH), "scenarios": []}
    for scenario in suite["scenarios"]:
        fixture = resolve_fixture(common, scenario)
        path = FIXTURE_DIR / f"{scenario['name']}.json"
        path.write_text(json.dumps(fixture, indent=2) + "\n", encoding="utf-8")
        entry = {"name": scenario["name"], "id": scenario["id"],
                 "stack_antes": scenario["stack_antes"],
                 "preflop_sizes": scenario["preflop_sizes"],
                 "postflop_sizes": scenario["postflop_sizes"],
                 "fixture": str(path.relative_to(ROOT)).replace("\\", "/"),
                 "fixture_sha256": sha256_file(path),
                 "open_target_units": fixture["open_target_units"],
                 "response_target_units": fixture["response_target_units"],
                 "postflop_sizes_basis_points": fixture["postflop_sizes_basis_points"]}
        if arguments.game_exe:
            report = run_game_report(Path(arguments.game_exe), path)
            if "error" in report:
                entry["game_report_error"] = report["error"]
            else:
                entry["tree_fingerprint"] = report["tree_fingerprint"]
                entry["config_fingerprint"] = report["config_fingerprint"]
                entry["nodes"] = report["node_count"]
                entry["decision_nodes"] = report["decision_nodes"]
                entry["preflop"] = report["preflop"]
                entry["postflop"] = report["postflop"]
                entry["maximum_depth"] = report["maximum_depth"]
                layout = layout_estimate(report, capacities)
                entry["history7_layout"] = layout
                # Baseline trainer keeps three double tables plus uint32 timestamps.
                baseline_state = layout["three_double_tables_bytes"] + \
                    layout["lazy_timestamp_bytes_uint32"]
                entry["baseline_trainer_state_bytes"] = baseline_state
                entry["fits_ram"] = bool(ram and baseline_state < 0.9 * ram)
                entry["fits_budget_8gib"] = baseline_state < budget
        resolved["scenarios"].append(entry)
        print(f"{scenario['name']:10s} stack={scenario['stack_antes']:3d}a "
              f"pre={scenario['preflop_sizes']} post={scenario['postflop_sizes']} "
              f"open={fixture['open_target_units']} response={fixture['response_target_units']} "
              f"postflop={fixture['postflop_sizes_basis_points']}"
              + (f" tree={entry.get('tree_fingerprint', '?')} nodes={entry.get('nodes', '?')}"
                 f" state={entry.get('baseline_trainer_state_bytes', 0) / 2**30:.2f} GiB"
                 if arguments.game_exe else ""))
    RESOLVED_PATH.write_text(json.dumps(resolved, indent=2) + "\n", encoding="utf-8")
    return 0


def read_events(path: Path) -> list[dict]:
    events = []
    if not path.exists():
        return events
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if line.startswith("{"):
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return events


def find_event(events: list[dict], name: str) -> dict | None:
    for event in events:
        if event.get("event") == name:
            return event
    return None


def cmd_check(arguments: argparse.Namespace) -> int:
    suite = load_suite()
    common = suite["common"]
    failures: list[str] = []
    names = [s["name"] for s in suite["scenarios"]]
    if arguments.scenarios:
        names = [n for n in names if n in arguments.scenarios]
    fixtures = {}
    for scenario in suite["scenarios"]:
        if scenario["name"] not in names:
            continue
        expected = resolve_fixture(common, scenario)
        path = FIXTURE_DIR / f"{scenario['name']}.json"
        if not path.exists():
            failures.append(f"{scenario['name']}: fixture missing, run resolve")
            continue
        actual = json.loads(path.read_text(encoding="utf-8"))
        if actual != expected:
            failures.append(f"{scenario['name']}: fixture differs from the catalog derivation")
        fixtures[scenario["name"]] = actual
    # Pairwise comparison of everything but the scenario fields.
    reference_name = next(iter(fixtures), None)
    if reference_name:
        reference = {k: v for k, v in fixtures[reference_name].items() if k not in SCENARIO_FIELDS}
        for name, fixture in fixtures.items():
            other = {k: v for k, v in fixture.items() if k not in SCENARIO_FIELDS}
            for key in sorted(set(reference) | set(other)):
                if reference.get(key) != other.get(key):
                    failures.append(f"{name}: common field '{key}' = {other.get(key)!r} differs "
                                    f"from {reference_name} = {reference.get(key)!r}")
    # Shared artifacts.
    for relative, expected_hash in common["abstraction"]["sha256"].items():
        path = ROOT / relative
        if not path.exists():
            failures.append(f"shared artifact missing: {relative}")
        elif sha256_file(path) != expected_hash:
            failures.append(f"shared artifact changed: {relative}")
    # Runs of one version, when requested.
    if arguments.runs_root:
        runs_root = Path(arguments.runs_root)
        manifests = sorted(runs_root.glob("*/rep*/manifest.json"))
        by_key: dict[str, dict] = {}
        exe_hashes: dict[str, set] = {}
        incomplete: list[str] = []
        for manifest_path in manifests:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            scenario = manifest["scenario"]
            if scenario not in names:
                continue
            if manifest.get("status") != "COMPLETE":
                # A run still in progress or failed has no start events to compare: it is
                # reported, never silently counted as uniform.
                incomplete.append(f"{manifest_path.relative_to(runs_root)} ({manifest.get('status')})")
                continue
            for phase in ("train", "certify"):
                info = manifest["executables"].get(phase)
                if info:
                    exe_hashes.setdefault(phase, set()).add(info["sha256"])
            start = manifest.get("train_start") or {}
            filtered = {k: v for k, v in start.items() if k not in START_TREE_KEYS}
            cert = manifest.get("certify_start") or {}
            cert_filtered = {k: v for k, v in cert.items() if k not in CERT_TREE_KEYS}
            by_key[str(manifest_path.relative_to(runs_root))] = {
                "train": filtered, "certify": cert_filtered,
                "train_args": manifest["commands"]["train_protocol_arguments"],
                "certify_args": manifest["commands"]["certify_protocol_arguments"],
                "resources": manifest["shared_artifacts"],
            }
        for phase, hashes in exe_hashes.items():
            if len(hashes) > 1:
                failures.append(f"{phase}: {len(hashes)} different executables in {runs_root}")
        if by_key:
            first_key = next(iter(by_key))
            first = by_key[first_key]
            for key, entry in by_key.items():
                for section in ("train", "certify", "train_args", "certify_args", "resources"):
                    if entry[section] != first[section]:
                        diff = {k: (first[section].get(k), entry[section].get(k))
                                for k in set(first[section]) | set(entry[section])
                                if first[section].get(k) != entry[section].get(k)} \
                            if isinstance(entry[section], dict) else (first[section], entry[section])
                        failures.append(f"{key}: {section} differs from {first_key}: {diff}")
        print(f"runs compared: {len(by_key)} manifests under {runs_root}")
        for entry in incomplete:
            print(f"  not compared (incomplete run): {entry}")
    if failures:
        print("UNIFORMITY_CHECK=FAIL")
        for failure in failures:
            print("  - " + failure)
        return 1
    print(f"UNIFORMITY_CHECK=PASS scenarios={len(fixtures)}")
    return 0


# --- process monitor -------------------------------------------------------------------

class PROCESS_MEMORY_COUNTERS_EX(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t), ("PrivateUsage", ctypes.c_size_t)]


def query_handle_counters(handle: int) -> dict | None:
    if os.name != "nt":
        return None
    counters = PROCESS_MEMORY_COUNTERS_EX()
    counters.cb = ctypes.sizeof(counters)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    ok = psapi.GetProcessMemoryInfo(wintypes.HANDLE(handle), ctypes.byref(counters), counters.cb)
    if not ok:
        return None
    return {"page_fault_count": counters.PageFaultCount,
            "peak_working_set_bytes": counters.PeakWorkingSetSize,
            "working_set_bytes": counters.WorkingSetSize,
            "private_commit_bytes": counters.PagefileUsage,
            "peak_private_commit_bytes": counters.PeakPagefileUsage,
            "private_usage_bytes": counters.PrivateUsage}


def query_handle_times(handle: int) -> dict | None:
    if os.name != "nt":
        return None
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    creation, exit_, kernel, user = (wintypes.FILETIME() for _ in range(4))
    ok = kernel32.GetProcessTimes(wintypes.HANDLE(handle), ctypes.byref(creation),
                                  ctypes.byref(exit_), ctypes.byref(kernel), ctypes.byref(user))
    if not ok:
        return None

    def seconds(filetime: wintypes.FILETIME) -> float:
        return ((filetime.dwHighDateTime << 32) | filetime.dwLowDateTime) / 1e7

    return {"user_seconds": seconds(user), "kernel_seconds": seconds(kernel),
            "cpu_seconds": seconds(user) + seconds(kernel)}


def monitored_run(command: list[str], stdout_path: Path, stderr_path: Path,
                  samples_path: Path, interval: float) -> dict:
    """Run a command, sampling its memory counters and the system memory."""
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    started_wall = time.time()
    started = time.perf_counter()
    with open(stdout_path, "wb") as out, open(stderr_path, "wb") as err:
        process = subprocess.Popen(command, cwd=ROOT, stdout=out, stderr=err)
        proc = psutil.Process(process.pid) if psutil else None
        samples = []
        peak_ws = peak_commit = 0
        min_available = None
        max_swap = 0
        last = None
        while True:
            exited = process.poll() is not None
            if proc is not None and not exited:
                try:
                    info = proc.memory_info()
                    vm = psutil.virtual_memory()
                    swap = psutil.swap_memory()
                    cpu = proc.cpu_times()
                    system_cpu = psutil.cpu_percent(interval=None)
                    last = {"t": time.perf_counter() - started, "wset": info.wset,
                            "peak_wset": info.peak_wset, "pagefile": info.pagefile,
                            "peak_pagefile": info.peak_pagefile, "private": info.private,
                            "faults": info.num_page_faults,
                            "cpu_user": cpu.user, "cpu_system": cpu.system,
                            "sys_available": vm.available, "swap_used": swap.used,
                            "system_cpu": system_cpu}
                    samples.append(last)
                    peak_ws = max(peak_ws, info.peak_wset)
                    peak_commit = max(peak_commit, info.peak_pagefile)
                    min_available = vm.available if min_available is None \
                        else min(min_available, vm.available)
                    max_swap = max(max_swap, swap.used)
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass
            if exited:
                break
            time.sleep(interval)
        exit_code = process.wait()
        elapsed = time.perf_counter() - started
        handle = getattr(process, "_handle", None)
        final_counters = query_handle_counters(handle) if handle is not None else None
        times = query_handle_times(handle) if handle is not None else None
    with open(samples_path, "w", encoding="utf-8") as handle_out:
        handle_out.write("t_seconds,working_set,peak_working_set,private_commit,"
                         "peak_private_commit,private_usage,page_faults,cpu_user,cpu_system,"
                         "system_available,swap_used,system_cpu_percent\n")
        for s in samples:
            handle_out.write(f"{s['t']:.3f},{s['wset']},{s['peak_wset']},{s['pagefile']},"
                             f"{s['peak_pagefile']},{s['private']},{s['faults']},"
                             f"{s['cpu_user']:.3f},{s['cpu_system']:.3f},"
                             f"{s['sys_available']},{s['swap_used']},{s.get('system_cpu', 0):.1f}\n")
    result = {
        "command": command,
        "started_utc": dt.datetime.fromtimestamp(started_wall, dt.timezone.utc).isoformat(timespec="seconds"),
        "exit_code": exit_code,
        "wall_seconds": elapsed,
        "samples": len(samples),
        "sample_interval_seconds": interval,
        "peak_working_set_bytes_sampled": peak_ws,
        "peak_private_commit_bytes_sampled": peak_commit,
        "final_counters_after_exit": final_counters,
        "process_times": times,
        "last_sample": last,
        "system_min_available_bytes": min_available,
        "system_max_swap_used_bytes": max_swap,
        # Mean system-wide CPU load over the samples (excluding the first, unprimed one):
        # values well above what the solver alone uses reveal interference from other work.
        "system_cpu_percent_mean": (sum(s.get("system_cpu", 0) for s in samples[1:]) /
                                    max(1, len(samples) - 1)) if len(samples) > 1 else None,
    }
    # Prefer the exact counters of the process object when the query succeeds.
    if final_counters:
        result["peak_working_set_bytes"] = max(peak_ws, final_counters["peak_working_set_bytes"])
        result["peak_private_commit_bytes"] = max(peak_commit,
                                                  final_counters["peak_private_commit_bytes"])
        result["page_faults"] = final_counters["page_fault_count"]
    else:
        result["peak_working_set_bytes"] = peak_ws
        result["peak_private_commit_bytes"] = peak_commit
        result["page_faults"] = last["faults"] if last else None
    if result.get("process_times") and elapsed > 0:
        # Average cores the process actually got: 8 threads on 4c/8t -> ~6.5 when alone.
        result["cores_used_mean"] = result["process_times"]["cpu_seconds"] / elapsed
    if times is None and last is not None:
        result["process_times"] = {"user_seconds": last["cpu_user"],
                                   "kernel_seconds": last["cpu_system"],
                                   "cpu_seconds": last["cpu_user"] + last["cpu_system"],
                                   "source": "last sample"}
    return result


def other_solver_processes() -> list[dict]:
    if psutil is None:
        return []
    found = []
    for proc in psutil.process_iter(["pid", "name", "create_time"]):
        name = (proc.info["name"] or "").lower()
        if name.startswith("gtosd_") or name.startswith("gto_"):
            try:
                mem = proc.memory_info()
                found.append({"pid": proc.pid, "name": proc.info["name"],
                              "private_commit_bytes": mem.pagefile,
                              "working_set_bytes": mem.wset})
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
    return found


def git_state() -> dict:
    def run(args):
        try:
            return subprocess.run(["git"] + args, cwd=ROOT, capture_output=True, text=True,
                                  check=True).stdout.strip()
        except Exception:  # pragma: no cover
            return ""
    return {"head": run(["rev-parse", "HEAD"]), "branch": run(["rev-parse", "--abbrev-ref", "HEAD"]),
            "dirty": run(["status", "--porcelain"]) != ""}


def cmd_run(arguments: argparse.Namespace) -> int:
    suite = load_suite()
    common = suite["common"]
    scenario = next((s for s in suite["scenarios"] if s["name"] == arguments.scenario), None)
    if scenario is None:
        print(f"unknown scenario {arguments.scenario}")
        return 2
    fixture_path = FIXTURE_DIR / f"{scenario['name']}.json"
    if not fixture_path.exists() or json.loads(fixture_path.read_text(encoding="utf-8")) != \
            resolve_fixture(common, scenario):
        print("fixture missing or stale: run `suite.py resolve` first")
        return 2
    exe_dir = Path(arguments.exe_dir)
    train_exe = exe_dir / common["trainer"]["executable"]
    certify_exe = exe_dir / common["certifier"]["executable"]
    for exe in (train_exe, certify_exe):
        if not exe.exists():
            print(f"missing executable {exe}")
            return 2
    blocking = [p for p in other_solver_processes()
                if p["name"].lower() in ("gtosd_preflop_blueprint_train.exe",
                                         "gtosd_preflop_blueprint_certify.exe",
                                         "gtosd_preflop_blueprint_abstract_br.exe")]
    if blocking and not arguments.allow_concurrent:
        print(f"refusing to start: solver processes already running: {blocking}")
        return 3
    out_dir = RUNS_ROOT / arguments.version / scenario["name"] / f"rep{arguments.rep}"
    if out_dir.exists() and any(out_dir.iterdir()) and not arguments.overwrite:
        print(f"output directory exists: {out_dir} (use --overwrite)")
        return 2
    out_dir.mkdir(parents=True, exist_ok=True)
    version_spec = suite.get("versions", {}).get(arguments.version, {})
    # A version may replace parts of the shared abstraction (research versions of phase 3):
    # the override is recorded in the manifest and marks the run as deviating from the
    # common protocol, so it never enters a fixed-work comparison.
    abstraction_override = version_spec.get("abstraction", {})
    abstraction = {**common["abstraction"], **abstraction_override}
    iterations = arguments.iterations or common["trainer"]["iterations"]
    train_args = list(common["trainer"]["arguments"])
    version_args = list(version_spec.get("train_arguments", []))
    train_args += version_args
    if arguments.iterations:
        index = train_args.index("--iterations")
        train_args[index + 1] = str(arguments.iterations)
    extra = list(arguments.train_extra or [])
    policy_path = out_dir / "policy.bin"
    checkpoint_path = out_dir / "checkpoint.bin"
    train_command = [str(train_exe), "--config", str(fixture_path),
                     "--resources-dir", abstraction["resources_dir"],
                     "--buckets-dir", abstraction["buckets_dir"],
                     "--history-rows", abstraction["history_rows"],
                     "--policy-out", str(policy_path),
                     "--checkpoint", str(checkpoint_path)] + train_args + extra
    interval = common["measurement"]["sampling_seconds"]
    manifest = {
        "status": "RUNNING",
        "schema": "gtosd.preflop_blueprint_benchmark_run.v1",
        "version": arguments.version,
        "scenario": scenario["name"],
        "scenario_parameters": {k: scenario[k] for k in ("stack_antes", "preflop_sizes",
                                                          "postflop_sizes")},
        "repetition": arguments.rep,
        "protocol_iterations": iterations,
        "protocol_deviation": bool(arguments.iterations or extra or abstraction_override),
        "version_train_arguments": version_args,
        "version_description": version_spec.get("description", ""),
        "abstraction_override": abstraction_override,
        "fixture": {"path": str(fixture_path.relative_to(ROOT)).replace("\\", "/"),
                    "sha256": sha256_file(fixture_path)},
        "executables": {"train": {"path": str(train_exe), "sha256": sha256_file(train_exe),
                                  "bytes": train_exe.stat().st_size},
                        "certify": {"path": str(certify_exe), "sha256": sha256_file(certify_exe),
                                    "bytes": certify_exe.stat().st_size}},
        "shared_artifacts": {rel: sha256_file(ROOT / rel) for rel in abstraction["sha256"]},
        "git": git_state(),
        "machine": {"node": platform.node(), "platform": platform.platform(),
                    "cpu_count": os.cpu_count(),
                    "ram_bytes": psutil.virtual_memory().total if psutil else None,
                    "available_before_bytes": psutil.virtual_memory().available if psutil else None,
                    "other_solver_processes_before": other_solver_processes()},
        "commands": {"train": train_command, "train_protocol_arguments": train_args,
                     "certify_protocol_arguments": list(common["certifier"]["arguments"])},
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"[{scenario['name']}] training: {' '.join(train_command)}")
    train_result = monitored_run(train_command, out_dir / "train.jsonl",
                                 out_dir / "train.stderr.log", out_dir / "samples_train.csv",
                                 interval)
    manifest["train_run"] = train_result
    events = read_events(out_dir / "train.jsonl")
    manifest["train_start"] = find_event(events, "start")
    manifest["train_end"] = find_event(events, "end")
    manifest["train_progress"] = [e for e in events if e.get("event") == "training_progress"]
    # The instrumented trainer prints one breakdown after initialization and one after the
    # first iteration (workspaces and compact policy allocated): keep all, report the last.
    breakdown_events = [e for e in events if e.get("event") == "memory_breakdown"]
    manifest["train_memory_breakdowns"] = breakdown_events
    manifest["train_memory_breakdown"] = breakdown_events[-1] if breakdown_events else None
    manifest["train_policy_cells"] = find_event(events, "policy_cells")
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    if train_result["exit_code"] != 0 or not policy_path.exists():
        manifest["status"] = "TRAIN_FAILED"
        (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        print(f"[{scenario['name']}] training failed (exit {train_result['exit_code']})")
        return 1
    certificate_path = out_dir / "certificate.json"
    certify_command = [str(certify_exe), "--config", str(fixture_path),
                       "--resources-dir", abstraction["resources_dir"],
                       "--buckets-dir", abstraction["buckets_dir"],
                       "--history-rows", abstraction["history_rows"],
                       "--policy", str(policy_path), "--output", str(certificate_path),
                       "--state", str(out_dir / "cert_state.bin")] + \
        list(common["certifier"]["arguments"])
    manifest["commands"]["certify"] = certify_command
    print(f"[{scenario['name']}] certifying: {' '.join(certify_command)}")
    certify_result = monitored_run(certify_command, out_dir / "certify.jsonl",
                                   out_dir / "certify.stderr.log",
                                   out_dir / "samples_certify.csv", interval)
    manifest["certify_run"] = certify_result
    cert_events = read_events(out_dir / "certify.jsonl")
    manifest["certify_start"] = find_event(cert_events, "start")
    certificate = None
    if certificate_path.exists():
        certificate = json.loads(certificate_path.read_text(encoding="utf-8"))
        manifest["certificate"] = {k: certificate.get(k) for k in (
            "exact", "partial", "sampled", "flops", "boards", "max_gain", "nashconv", "ev",
            "gain", "gain_lower", "best_response", "passes_target", "target_antes",
            "policy_fingerprint", "tree_fingerprint", "history_map_fingerprint", "seconds",
            "evaluation_seconds", "aggregation_seconds", "process_bytes", "capacities",
            "process_peaks", "policy_table_bytes", "history_map_resident_bytes",
            "preparation_seconds") if k in certificate}
    # Artifact hashes, then optional cleanup of the multi-gigabyte files.
    artifacts = {}
    for name, path in (("policy", policy_path), ("checkpoint", checkpoint_path),
                       ("cert_state", out_dir / "cert_state.bin")):
        if path.exists():
            artifacts[name] = {"bytes": path.stat().st_size, "sha256": sha256_file(path)}
    manifest["artifacts"] = artifacts
    if not arguments.keep_checkpoint and checkpoint_path.exists():
        checkpoint_path.unlink()
        artifacts["checkpoint"]["deleted"] = True
    if not arguments.keep_policy and policy_path.exists():
        policy_path.unlink()
        artifacts["policy"]["deleted"] = True
    manifest["machine"]["available_after_bytes"] = psutil.virtual_memory().available if psutil else None
    end = manifest.get("train_end") or {}
    internal_train_total = end.get("total_seconds")
    cert_start = manifest.get("certify_start") or {}
    internal_cert = (cert_start.get("preparation_seconds") or 0) + \
        ((certificate or {}).get("seconds") or 0)
    manifest["summary"] = {
        "train_wall_seconds": train_result["wall_seconds"],
        "certify_wall_seconds": certify_result["wall_seconds"],
        "end_to_end_wall_seconds": train_result["wall_seconds"] + certify_result["wall_seconds"],
        "train_internal_total_seconds": internal_train_total,
        "certify_internal_total_seconds": internal_cert,
        "end_to_end_internal_seconds": (internal_train_total or 0) + internal_cert,
        "training_seconds": end.get("training_seconds"),
        "train_peak_private_commit_bytes": train_result["peak_private_commit_bytes"],
        "train_peak_working_set_bytes": train_result["peak_working_set_bytes"],
        "certify_peak_private_commit_bytes": certify_result["peak_private_commit_bytes"],
        "certify_peak_working_set_bytes": certify_result["peak_working_set_bytes"],
        "max_gain": (certificate or {}).get("max_gain"),
        "passes_target": (certificate or {}).get("passes_target"),
        "policy_fingerprint": end.get("policy_fingerprint"),
        "state_fingerprint": end.get("state_fingerprint"),
    }
    manifest["status"] = "COMPLETE" if certify_result["exit_code"] == 0 and certificate else \
        "CERTIFY_FAILED"
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    s = manifest["summary"]
    print(f"[{scenario['name']}] {manifest['status']}: train {s['train_wall_seconds']:.1f} s, "
          f"certify {s['certify_wall_seconds']:.1f} s, peak commit "
          f"{s['train_peak_private_commit_bytes'] / 2**30:.3f} GiB, max_gain {s['max_gain']}")
    return 0 if manifest["status"] == "COMPLETE" else 1


def gib(value) -> str:
    return "" if value is None else f"{value / 2**30:.3f}"


def median_of(values):
    values = [v for v in values if v is not None]
    return statistics.median(values) if values else None


def cores_used(manifest: dict, phase: str = "train_run") -> float | None:
    """Average cores the process got (CPU seconds / wall): the contention indicator."""
    run = manifest.get(phase) or {}
    times = run.get("process_times") or {}
    wall = run.get("wall_seconds") or 0
    return times["cpu_seconds"] / wall if times.get("cpu_seconds") is not None and wall > 0 else None


CLEAN_CORES_RATIO = 0.9  # a repetition below 90 % of the best cores of its version/scenario is contaminated


def fmt(value, digits=1) -> str:
    return "" if value is None else f"{value:.{digits}f}"


def analytic_baseline_breakdown(resolved_entry: dict) -> dict | None:
    """Component bytes of the HEAD ba93c75 trainer, from the layout arithmetic
    (three dense double tables, one uint32 timestamp per row, per-board
    465 x 465 all-in copies, dense 630 x 630 tables, workspaces)."""
    layout = resolved_entry.get("history7_layout")
    if not layout:
        return None
    entries = layout["entries"]
    rows = layout["rows"]
    depth = resolved_entry.get("maximum_depth", 14) + 2
    live = 465
    return {
        "regret_bytes": entries * 8,
        "strategy_sum_bytes": entries * 8,
        "policy_bytes": entries * 8,
        "discount_timestamp_bytes": rows * 4,
        "all_in_dense_bytes": 2 * 630 * 630 * 8,
        "board_batch_bytes": 32 * (2 * live * live * 8 + 6 * live * 8 + 16 * 1024),
        "workspace_bytes": 8 * depth * (2 * 8 * live * 8 + 3 * live * 8 + live * 8),
        "history_map_resident_bytes": 52829432,
        "cells_by_street": list(layout["entries_by_street"].values()),
        "rows_by_street": list(layout["rows_by_street"].values()),
        "source": "analytic (HEAD has no instrumentation)",
    }


def cmd_report(arguments: argparse.Namespace) -> int:
    manifests = []
    for path in sorted(RUNS_ROOT.glob("*/*/rep*/manifest.json")):
        manifest = json.loads(path.read_text(encoding="utf-8"))
        if manifest.get("status") not in ("COMPLETE", "TRAIN_FAILED", "CERTIFY_FAILED"):
            continue
        if arguments.versions and manifest["version"] not in arguments.versions:
            continue
        manifests.append(manifest)
    resolved = {}
    if RESOLVED_PATH.exists():
        resolved = {e["name"]: e for e in
                    json.loads(RESOLVED_PATH.read_text(encoding="utf-8"))["scenarios"]}
    rows = {}
    for manifest in manifests:
        rows.setdefault((manifest["version"], manifest["scenario"]), []).append(manifest)
    ordered = sorted(rows.items(), key=lambda kv: (kv[0][1], kv[0][0]))
    out = []
    out.append("## Tabella principale (mediana dei tempi sulle ripetizioni pulite, picchi massimi su tutte)\n")
    out.append("| Benchmark | Versione | Rip. (pulite) | Picco private commit (GiB) | Picco working set (GiB) | "
               "Training (s) | BR esatta (s) | Totale interno (s) | Totale processi (s) | "
               "max gain (a) | Esito |")
    out.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|")
    table = {}
    for (version, scenario), items in ordered:
        complete = [m for m in items if m.get("status") == "COMPLETE"]
        summaries = [m["summary"] for m in complete]
        if not summaries:
            out.append(f"| {scenario} | {version} | {len(items)} | | | | | | | | "
                       f"{items[0].get('status')} |")
            continue
        peak_commit = max(max(s["train_peak_private_commit_bytes"],
                              s["certify_peak_private_commit_bytes"]) for s in summaries)
        peak_ws = max(max(s["train_peak_working_set_bytes"], s["certify_peak_working_set_bytes"])
                      for s in summaries)
        gains = sorted(set(round(s["max_gain"], 12) for s in summaries))
        passes = all(s["passes_target"] for s in summaries)
        # Contaminated repetitions (CPU taken by other work, or paging) are excluded from the
        # timing medians only: memory peaks and quality use every complete repetition.
        cores = [cores_used(m) for m in complete]
        best_cores = max((c for c in cores if c is not None), default=None)
        clean = [m for m, c in zip(complete, cores)
                 if best_cores is None or c is None or c >= CLEAN_CORES_RATIO * best_cores]
        clean_summaries = [m["summary"] for m in clean]
        row = {"peak_commit": peak_commit, "peak_ws": peak_ws,
               "train_peak_commit": max(s["train_peak_private_commit_bytes"] for s in summaries),
               "certify_peak_commit": max(s["certify_peak_private_commit_bytes"] for s in summaries),
               "training": median_of(s["training_seconds"] for s in clean_summaries),
               "training_min": min(s["training_seconds"] for s in summaries),
               "training_max": max(s["training_seconds"] for s in summaries),
               "certify": median_of(s["certify_internal_total_seconds"] for s in clean_summaries),
               "e2e_internal": median_of(s["end_to_end_internal_seconds"] for s in clean_summaries),
               "e2e_min": min(s["end_to_end_internal_seconds"] for s in summaries),
               "e2e_max": max(s["end_to_end_internal_seconds"] for s in summaries),
               "e2e_wall": median_of(s["end_to_end_wall_seconds"] for s in clean_summaries),
               "gains": gains, "passes": passes, "reps": len(summaries),
               "reps_clean": len(clean_summaries), "cores": cores,
               "protocol_deviation": any(m.get("protocol_deviation") for m in complete),
               "fingerprints": sorted(set(s.get("policy_fingerprint") for s in summaries)),
               "manifests": complete, "clean_manifests": clean}
        table[(version, scenario)] = row
        out.append(f"| {scenario} | {version} | {len(summaries)} ({len(clean_summaries)}) | {gib(peak_commit)} | "
                   f"{gib(peak_ws)} | {fmt(row['training'])} | {fmt(row['certify'])} | "
                   f"{fmt(row['e2e_internal'])} | {fmt(row['e2e_wall'])} | "
                   f"{'/'.join(f'{g:.9f}' for g in gains)} | "
                   f"{'PASS' if passes else 'FAIL'}"
                   f"{' (protocollo ridotto)' if row['protocol_deviation'] else ''} |")
    # Scenarios of the suite without a complete run for a version are listed, never omitted:
    # a benchmark that does not fit the machine stays RESOURCE_LIMIT, a derived one NON ESEGUITO.
    listed_versions = sorted({m["version"] for m in manifests}) or list(arguments.versions or [])
    ram_bytes = 32 * 2**30
    for version in listed_versions:
        for name, entry in resolved.items():
            if (version, name) in rows:
                continue
            state = entry.get("baseline_trainer_state_bytes") or 0
            if state > ram_bytes:
                reason = (f"RESOURCE_LIMIT (stato baseline {state / 2**30:.1f} GiB > 32 GiB di RAM "
                          f"con history7; nessun run)")
            elif name == "HU20-2":
                reason = ("NON ESEGUITO (scenario derivato: 20a con la 3-bet a 17a, che l'utente ha "
                          "escluso dalla fixture storica)")
            else:
                reason = "NON ESEGUITO"
            out.append(f"| {name} | {version} | 0 | | | | | | | | {reason} |")
    baseline = arguments.baseline
    if baseline:
        out.append("\n## Confronto con la baseline\n")
        out.append("| Benchmark | Candidata | GiB risparmiati (picco commit) | Riduzione memoria | "
                   "Secondi risparmiati (e2e interno) | Speedup training | Speedup e2e | "
                   "delta max gain (a) | delta EV CO (a) | delta NashConv (a) | Stessa policy | "
                   "Criteri preregistrati (7.1 identita' / 7.2 esito) |")
        out.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|")
        for (version, scenario), row in sorted(table.items(), key=lambda kv: (kv[0][1], kv[0][0])):
            base = table.get((baseline, scenario))
            if version == baseline or base is None:
                continue
            saved = base["peak_commit"] - row["peak_commit"]
            cert_c = row["manifests"][-1].get("certificate") or {}
            cert_b = base["manifests"][-1].get("certificate") or {}
            ev_c = (cert_c.get("ev") or [None])[0]
            ev_b = (cert_b.get("ev") or [None])[0]
            ev_delta = ev_c - ev_b if ev_c is not None and ev_b is not None else None
            nc_c, nc_b = cert_c.get("nashconv"), cert_b.get("nashconv")
            nc_delta = nc_c - nc_b if nc_c is not None and nc_b is not None else None
            gain_delta = row["gains"][0] - base["gains"][0]
            identical = row["fingerprints"] == base["fingerprints"]
            passes_c = all(m["summary"]["passes_target"] for m in row["manifests"])
            passes_b = all(m["summary"]["passes_target"] for m in base["manifests"])
            # Pre-registered acceptance (protocol 7.1 for bit-identical candidates, 7.2 otherwise):
            # |delta EV| <= 1e-4 a, max gain <= baseline + 3e-4 a with the same verdict, NashConv
            # within 3e-4 a, training not slower than 3 %, peak commit not higher.
            reasons = []
            if not identical:
                if ev_delta is None or abs(ev_delta) > 1e-4:
                    reasons.append("EV")
                if gain_delta > 3e-4 or passes_c != passes_b:
                    reasons.append("max gain")
                if nc_delta is None or abs(nc_delta) > 3e-4:
                    reasons.append("NashConv")
            if row["training"] > 1.03 * base["training"]:
                reasons.append("training +3 %")
            if row["peak_commit"] > base["peak_commit"]:
                reasons.append("picco commit")
            if identical:
                verdict = "7.1 identica" + (" (PASS)" if not reasons else " ma " + ", ".join(reasons))
            else:
                verdict = "7.2 PASS" if not reasons else "7.2 FAIL: " + ", ".join(reasons)
            fmt_delta = lambda v: "n/d" if v is None else f"{v:+.9f}"
            out.append(f"| {scenario} | {version} | {saved / 2**30:.3f} | "
                       f"{100 * saved / base['peak_commit']:.1f} % | "
                       f"{base['e2e_internal'] - row['e2e_internal']:.1f} | "
                       f"{base['training'] / row['training']:.3f} | "
                       f"{base['e2e_internal'] / row['e2e_internal']:.3f} | "
                       f"{gain_delta:+.9f} | {fmt_delta(ev_delta)} | {fmt_delta(nc_delta)} | "
                       f"{'si' if identical else 'no'} | {verdict} |")
    out.append("\n## Scomposizione dei tempi (mediana, secondi)\n")
    out.append("| Benchmark | Versione | Init | Discount | Refresh policy | Board + all-in | "
               "Traversata | Training | Scrittura | Prep. cert. | BR esatta | E2E interno | "
               "s/iter | ms/board | CPU s (train) | CPU s (cert) | Core medi train / cert | "
               "CPU di sistema media (%) train / cert |")
    out.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for (version, scenario), row in sorted(table.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        ends = [m["train_end"] for m in row["manifests"] if m.get("train_end")]
        starts = [m["train_start"] for m in row["manifests"] if m.get("train_start")]
        certs = [m.get("certificate") or {} for m in row["manifests"]]
        cert_starts = [m.get("certify_start") or {} for m in row["manifests"]]
        init = median_of(s.get("preparation_seconds") for s in starts)
        training = median_of(e.get("training_seconds") for e in ends)
        write = median_of(
            e.get("write_seconds") if e.get("write_seconds") is not None else
            (e["total_seconds"] - (e.get("preparation_seconds") or s.get("preparation_seconds") or 0)
             - e["training_seconds"] - e.get("evaluation_seconds", 0) -
             e.get("certification_seconds", 0))
            for e, s in zip(ends, starts))
        iterations = median_of(e.get("iteration") for e in ends) or 1
        boards = median_of(e.get("boards_processed") for e in ends) or 1
        cpu_train = median_of(m["train_run"]["process_times"]["cpu_seconds"]
                              for m in row["manifests"] if m["train_run"].get("process_times"))
        cpu_cert = median_of(m["certify_run"]["process_times"]["cpu_seconds"]
                             for m in row["manifests"] if m["certify_run"].get("process_times"))
        cores_train = median_of(m["train_run"]["process_times"]["cpu_seconds"] /
                                m["train_run"]["wall_seconds"] for m in row["manifests"]
                                if m.get("train_run", {}).get("process_times"))
        cores_cert = median_of(m["certify_run"]["process_times"]["cpu_seconds"] /
                               m["certify_run"]["wall_seconds"] for m in row["manifests"]
                               if m.get("certify_run", {}).get("process_times"))
        sys_train = median_of(m["train_run"].get("system_cpu_percent_mean")
                              for m in row["manifests"])
        sys_cert = median_of(m["certify_run"].get("system_cpu_percent_mean")
                             for m in row["manifests"])
        out.append(f"| {scenario} | {version} | {fmt(init)} | "
                   f"{fmt(median_of(e.get('discount_seconds') for e in ends), 3)} | "
                   f"{fmt(median_of(e.get('policy_refresh_seconds') for e in ends))} | "
                   f"{fmt(median_of(e.get('board_prepare_seconds') for e in ends))} | "
                   f"{fmt(median_of(e.get('traversal_seconds') for e in ends))} | "
                   f"{fmt(training)} | {fmt(write)} | "
                   f"{fmt(median_of(c.get('preparation_seconds') for c in cert_starts))} | "
                   f"{fmt(median_of(c.get('seconds') for c in certs))} | "
                   f"{fmt(row['e2e_internal'])} | "
                   f"{fmt((training or 0) / iterations, 4)} | "
                   f"{fmt(1000 * (training or 0) / boards, 3)} | {fmt(cpu_train)} | {fmt(cpu_cert)} | "
                   f"{fmt(cores_train, 2)} / {fmt(cores_cert, 2)} | "
                   f"{fmt(sys_train) or 'n/d'} / {fmt(sys_cert) or 'n/d'} |")
    out.append("\n## Scomposizione della memoria del trainer (byte riservati per componente)\n")
    out.append("| Benchmark | Versione | Regret | Somme strategia | Policy densa / compatta | "
               "Timestamp discount | All-in (dense + per board) | Batch board | Workspace | "
               "Mappa history | Albero + layout | Totale conteggiato | Picco commit | "
               "Non attribuito | Fonte |")
    out.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|")
    for (version, scenario), row in sorted(table.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        breakdowns = [m.get("train_memory_breakdown") for m in row["manifests"]
                      if m.get("train_memory_breakdown")]
        source = "misurata"
        if breakdowns:
            b = breakdowns[-1]
            policy = b.get("compact_policy_capacity_bytes", 0)
            all_in = b.get("all_in_dense_bytes", 0)
            tree = b.get("tree_bytes", 0) + b.get("layout_offset_bytes", 0) + \
                b.get("partition_bytes", 0) + b.get("compact_policy_offsets_bytes", 0) + \
                b.get("discount_offset_bytes", 0)
            total = b.get("accounted_total_bytes", 0)
        else:
            b = analytic_baseline_breakdown(resolved.get(scenario, {}))
            if not b:
                continue
            policy = b["policy_bytes"]
            all_in = b["all_in_dense_bytes"]
            tree = 0
            total = sum(v for k, v in b.items() if k.endswith("_bytes"))
            source = b["source"]
        peak = row["train_peak_commit"]
        out.append(f"| {scenario} | {version} | {gib(b['regret_bytes'])} | "
                   f"{gib(b['strategy_sum_bytes'])} | {gib(policy)} | "
                   f"{gib(b['discount_timestamp_bytes'])} | {gib(all_in + (0 if breakdowns else 0))} | "
                   f"{gib(b['board_batch_bytes'])} | {gib(b['workspace_bytes'])} | "
                   f"{gib(b['history_map_resident_bytes'])} | {gib(tree)} | {gib(total)} | "
                   f"{gib(peak)} | {gib(peak - total)} | {source} |")
    out.append("\n## Celle per street (tabelle CFR) e policy per batch\n")
    out.append("| Benchmark | Versione | Celle preflop/flop/turn/river | Righe preflop/flop/turn/river | "
               "Celle policy allocate | Righe materializzate per pass (media) | Celle materializzate "
               "per pass (media) | Lookup mano-riga per pass | Riusi per pass | Policy compatta "
               "(byte, picco) | Board distinte / rebuild identici | Byte regret+somme per street (GiB) |")
    out.append("|---|---|---|---|---:|---|---|---|---|---:|---|---|")
    for (version, scenario), row in sorted(table.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        ends = [m["train_end"] for m in row["manifests"] if m.get("train_end")]
        cells = None
        breakdowns = [m.get("train_memory_breakdown") for m in row["manifests"]
                      if m.get("train_memory_breakdown")]
        if breakdowns:
            cells = breakdowns[-1].get("cells_by_street")
            street_rows = breakdowns[-1].get("rows_by_street")
        elif scenario in resolved and resolved[scenario].get("history7_layout"):
            cells = list(resolved[scenario]["history7_layout"]["entries_by_street"].values())
            street_rows = list(resolved[scenario]["history7_layout"]["rows_by_street"].values())
        else:
            street_rows = None
        pc = ends[-1].get("policy_cells") if ends else None
        bytes_per_cell = (breakdowns[-1].get("regret_bytes_per_cell", 8) +
                          breakdowns[-1].get("strategy_sum_bytes_per_cell", 8)) if breakdowns else 16
        street_bytes = "/".join(f"{c * bytes_per_cell / 2**30:.3f}" for c in cells or [])
        if pc:
            passes = 2 * (ends[-1].get("iteration") or 1)
            rows_m = [r / passes for r in pc["rows_materialized"]]
            cells_m = [c / passes for c in pc["cells_materialized"]]
            lookups = [l / passes for l in pc["hand_lookups"]]
            reuses = [l - r for l, r in zip(lookups, rows_m)]
            allocated = pc.get("compact_policy_bytes_peak", 0) // 8
            out.append(f"| {scenario} | {version} | {'/'.join(str(c) for c in cells or [])} | "
                       f"{'/'.join(str(r) for r in street_rows or [])} | {allocated} (compatta) | "
                       f"{'/'.join(f'{r:.0f}' for r in rows_m)} | {'/'.join(f'{c:.0f}' for c in cells_m)} | "
                       f"{'/'.join(f'{l:.0f}' for l in lookups)} | {'/'.join(f'{r:.0f}' for r in reuses)} | "
                       f"{pc.get('compact_policy_bytes_peak', 0)} | "
                       f"{ends[-1].get('boards_distinct', 'n/d')} / {ends[-1].get('boards_repeated', 'n/d')} | "
                       f"{street_bytes} ({bytes_per_cell} B/cella) |")
        else:
            out.append(f"| {scenario} | {version} | {'/'.join(str(c) for c in cells or [])} | "
                       f"{'/'.join(str(r) for r in street_rows or [])} | "
                       f"{sum(cells) if cells else ''} (densa) | non strumentato | non strumentato | "
                       f"non strumentato | non strumentato | | non strumentato | {street_bytes} (16 B/cella) |")
    out.append("\n## Qualita' ottenuta (certificato esatto)\n")
    out.append("| Benchmark | Versione | EV CO (a) | EV BTN (a) | Guadagno CO | Guadagno BTN | "
               "NashConv (a) | max gain (a) | Soglia (a) | Esito | Policy | Albero |")
    out.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---|---|---|")
    for (version, scenario), row in sorted(table.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        cert = (row["manifests"][-1].get("certificate") or {})
        ev = cert.get("ev") or [None, None]
        gain = cert.get("gain") or [None, None]
        out.append(f"| {scenario} | {version} | {fmt(ev[0], 9)} | {fmt(ev[1], 9)} | "
                   f"{fmt(gain[0], 9)} | {fmt(gain[1], 9)} | {fmt(cert.get('nashconv'), 9)} | "
                   f"{fmt(cert.get('max_gain'), 9)} | {fmt(cert.get('target_antes'), 3)} | "
                   f"{'PASS' if cert.get('passes_target') else 'FAIL'} | "
                   f"{cert.get('policy_fingerprint', '')} | {cert.get('tree_fingerprint', '')} |")
    out.append("\n## Ripetizioni e condizioni di memoria\n")
    out.append("| Benchmark | Versione | Rip. (pulite) | Training per rip. (s) | E2E interno per rip. (s) | "
               "Core medi train per rip. (* = contaminata) | CPU sistema media (%) per rip. | "
               "Picco commit per rip. (GiB) | Min memoria disponibile (GiB) | "
               "Max paging usato (GiB) | Page fault (train) | Altri processi gtosd |")
    out.append("|---|---|---:|---|---|---|---|---|---:|---:|---:|---|")
    for (version, scenario), row in sorted(table.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        peaks = [m["summary"]["train_peak_private_commit_bytes"] for m in row["manifests"]]
        avail = [m["train_run"].get("system_min_available_bytes") for m in row["manifests"]]
        swap = [m["train_run"].get("system_max_swap_used_bytes") for m in row["manifests"]]
        faults = [m["train_run"].get("page_faults") for m in row["manifests"]]
        others = sorted(set(p["name"] for m in row["manifests"]
                            for p in m["machine"].get("other_solver_processes_before", [])))
        best = max((c for c in row["cores"] if c is not None), default=None)
        cores_text = " / ".join(
            ("n/d" if c is None else f"{c:.2f}" +
             ("*" if best is not None and c < CLEAN_CORES_RATIO * best else ""))
            for c in row["cores"])
        sys_text = " / ".join(
            "n/d" if m["train_run"].get("system_cpu_percent_mean") is None
            else f"{m['train_run']['system_cpu_percent_mean']:.0f}" for m in row["manifests"])
        out.append(f"| {scenario} | {version} | {row['reps']} ({row['reps_clean']}) | "
                   f"{' / '.join(fmt(m['summary']['training_seconds']) for m in row['manifests'])} | "
                   f"{' / '.join(fmt(m['summary']['end_to_end_internal_seconds']) for m in row['manifests'])} | "
                   f"{cores_text} | {sys_text} | "
                   f"{' / '.join(gib(p) for p in peaks)} | "
                   f"{gib(min(a for a in avail if a is not None)) if any(a is not None for a in avail) else ''} | "
                   f"{gib(max(s for s in swap if s is not None)) if any(s is not None for s in swap) else ''} | "
                   f"{max(f for f in faults if f is not None) if any(f is not None for f in faults) else ''} | "
                   f"{', '.join(others) or 'nessuno'} |")
    text = "\n".join(out)
    print(text)
    if arguments.markdown_out:
        Path(arguments.markdown_out).write_text(text + "\n", encoding="utf-8")
    if arguments.json_out:
        Path(arguments.json_out).write_text(
            json.dumps({f"{v}/{s}": {k: r[k] for k in r if k != "manifests"}
                        for (v, s), r in table.items()}, indent=2), encoding="utf-8")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("resolve")
    p.add_argument("--game-exe", help="gtosd_preflop_blueprint_game.exe for tree reports")
    p.set_defaults(func=cmd_resolve)
    p = sub.add_parser("check")
    p.add_argument("--runs-root", help="out/suite/<version> to compare the run logs")
    p.add_argument("--scenarios", nargs="*")
    p.set_defaults(func=cmd_check)
    p = sub.add_parser("run")
    p.add_argument("--version", required=True)
    p.add_argument("--exe-dir", required=True)
    p.add_argument("--scenario", required=True)
    p.add_argument("--rep", type=int, default=1)
    p.add_argument("--iterations", type=int, help="research override (marks the run as deviating)")
    # REMAINDER: trainer flags start with "--" and would otherwise be taken for options of
    # this tool; --train-extra must therefore be the last option on the command line.
    p.add_argument("--train-extra", nargs=argparse.REMAINDER,
                   help="extra trainer flags, last option (marks the run as deviating)")
    p.add_argument("--keep-policy", action="store_true")
    p.add_argument("--keep-checkpoint", action="store_true")
    p.add_argument("--overwrite", action="store_true")
    p.add_argument("--allow-concurrent", action="store_true")
    p.set_defaults(func=cmd_run)
    p = sub.add_parser("report")
    p.add_argument("--baseline", help="version label used as the reference")
    p.add_argument("--versions", nargs="*", help="restrict to these version labels")
    p.add_argument("--json-out")
    p.add_argument("--markdown-out")
    p.set_defaults(func=cmd_report)
    arguments = parser.parse_args()
    return arguments.func(arguments)


if __name__ == "__main__":
    sys.exit(main())
