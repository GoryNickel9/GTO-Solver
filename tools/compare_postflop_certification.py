"""Diagnostic A/B of existing exact certification paths; never promotes a backend.

Run only after building Release and finishing tests. Original fixtures are read
unchanged. Every sample uses a fresh process and an isolated child environment.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def preflight() -> dict:
    if os.name != "nt":
        raise RuntimeError("This diagnostic uses Windows process memory and CPU accounting")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)

    def cpu_times() -> tuple[int, int]:
        idle, system, user = ctypes.c_uint64(), ctypes.c_uint64(), ctypes.c_uint64()
        if not kernel.GetSystemTimes(ctypes.byref(idle), ctypes.byref(system), ctypes.byref(user)):
            raise ctypes.WinError(ctypes.get_last_error())
        return idle.value, system.value + user.value

    class MemoryStatus(ctypes.Structure):
        _fields_ = [("length", ctypes.c_uint32), ("load", ctypes.c_uint32)] + [
            (name, ctypes.c_uint64)
            for name in ("total", "available", "page_total", "page_available",
                         "virtual_total", "virtual_available", "extended")
        ]

    samples = []
    for _ in range(5):
        idle_before, total_before = cpu_times()
        time.sleep(1)
        idle_after, total_after = cpu_times()
        if total_after <= total_before:
            raise RuntimeError("Invalid CPU accounting interval")
        samples.append(100 * (1 - (idle_after - idle_before) / (total_after - total_before)))
    memory = MemoryStatus()
    memory.length = ctypes.sizeof(memory)
    if not kernel.GlobalMemoryStatusEx(ctypes.byref(memory)):
        raise ctypes.WinError(ctypes.get_last_error())
    return {"cpu_samples_percent": samples, "mean_cpu_percent": statistics.mean(samples),
            "free_ram_bytes": memory.available,
            "passed": statistics.mean(samples) <= 15 and memory.available >= 4 * 1024**3}


def numerical_curve(report: dict) -> list[dict]:
    fields = ("iteration", "profile_value_co_antes", "profile_value_btn_antes",
              "best_response_value_co_antes", "best_response_value_btn_antes",
              "normalized_nash_conv", "gto_plus_dev_percent")
    return [{key: sample[key] for key in fields} for sample in report["convergence"]]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="out/build/windows-release-current")
    parser.add_argument("--output", required=True)
    parser.add_argument("--repetitions", type=int, default=1)
    parser.add_argument("--cases", nargs="+", choices=("ahkhqh", "th7d6s", "tstc9d"),
                        default=["ahkhqh", "th7d6s", "tstc9d"])
    args = parser.parse_args()
    if args.repetitions < 1:
        parser.error("repetitions must be positive")
    repo = Path(__file__).resolve().parents[1]
    executable = (repo / args.build_dir / "apps/gto_cli/gto_cli.exe").resolve()
    executable_hash = sha256(executable)
    sources = [repo / path for path in ("libs/postflop/src/postflop_solver.cpp",
               "libs/postflop/src/solver_memory_ledger.cpp", "apps/gto_cli/main.cpp")]
    source_hashes = {str(path.relative_to(repo)): sha256(path) for path in sources}
    ambient = [key for key in os.environ if key.startswith("GTOSD_")]
    if ambient:
        raise RuntimeError(f"Clean GTOSD environment required; found {ambient}")
    output = (repo / args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest = {"schema": "gtosd.certification_diagnostic_ab.v1",
                "scope": "diagnostic_not_production_qualification",
                "executable_sha256": executable_hash, "source_sha256": source_hashes,
                "maximum_active_solver_threads_declared": 8,
                "samples": [], "comparisons": []}

    def save() -> None:
        (output / "summary.json").write_text(json.dumps(manifest, indent=2, allow_nan=False),
                                             encoding="utf-8")

    save()
    for case in args.cases:
        fixture = repo / f"benchmarks/fixtures/gto_plus_{case}_101.json"
        fixture_hash = sha256(fixture)
        for repeat in range(1, args.repetitions + 1):
            reports = {}
            modes = ["current", "legacy_parallel"]
            if repeat % 2 == 0:
                modes.reverse()
            for mode in modes:
                prefix = f"{case}-{repeat:02d}-{mode}"
                check = preflight()
                if not check["passed"]:
                    manifest["blocked_preflight"] = {"sample": prefix, **check}
                    save()
                    raise RuntimeError(f"Controlled-load preflight failed: {check}")
                environment = os.environ.copy()
                if mode == "legacy_parallel":
                    environment["GTOSD_DIAGNOSTIC_LEGACY_CERTIFICATION"] = "1"
                report_path = output / f"{prefix}.json"
                print(f"RUN {prefix}", flush=True)
                with (output / f"{prefix}.log").open("wb") as log:
                    process = subprocess.run([str(executable), "postflop", "benchmark-gto-plus",
                                              str(fixture), str(report_path)], cwd=repo,
                                             env=environment, stdout=log, stderr=subprocess.STDOUT,
                                             timeout=900, check=False)
                if process.returncode not in (0, 4) or not report_path.is_file():
                    raise RuntimeError(f"{prefix}: process failed with code {process.returncode}")
                report = read_json(report_path)
                last = report["convergence"][-1]
                sample = {"case": case, "repeat": repeat, "mode": mode,
                          "report": report_path.name, "exit_code": process.returncode,
                          "fixture_sha256": fixture_hash, "preflight": check,
                          "elapsed_seconds": report["elapsed_seconds"],
                          "traversal_seconds": last["traversal_elapsed_seconds"],
                          "certification_seconds": last["certification_elapsed_seconds"],
                          "cpu_seconds": last["process_cpu_seconds"],
                          "process_memory": report.get("process_memory"),
                          "completed_iterations": report["completed_iterations"],
                          "dev_percent": report["final_gto_plus_dev_percent"],
                          "solver_state_bytes": report["solver_state_bytes"],
                          "converged": report["converged"],
                          "layout_matches_fixture": report["layout_matches_fixture"],
                          "exact_outcomes": report["exact_outcomes"]}
                manifest["samples"].append(sample)
                reports[mode] = report
                save()
                if (not all(sample[key] for key in
                            ("converged", "layout_matches_fixture", "exact_outcomes"))
                        or report["algorithm"] != "exact_production_dcfr"
                        or report["bucketing"]):
                    raise RuntimeError(f"{prefix}: invalid exact production baseline")
                print(f"DONE {prefix}: solver={sample['elapsed_seconds']:.6f}s "
                      f"certification={sample['certification_seconds']:.6f}s "
                      f"dEV={sample['dev_percent']:.9f}%", flush=True)
            first, second = reports["current"], reports["legacy_parallel"]
            identity_fields = ("completed_iterations", "solver_state_bytes", "game_fingerprint")
            identity = all(first[key] == second[key] for key in identity_fields)
            curve_equal = numerical_curve(first) == numerical_curve(second)
            manifest["comparisons"].append({"case": case, "repeat": repeat,
                "identity_equal": identity, "numerical_curve_equal": curve_equal,
                "solver_speedup": first["elapsed_seconds"] / second["elapsed_seconds"],
                "certification_speedup": first["convergence"][-1]["certification_elapsed_seconds"] /
                                         second["convergence"][-1]["certification_elapsed_seconds"]})
            save()
            if not identity or not curve_equal:
                raise RuntimeError(f"{case}: exact certification differential failed")
            if sha256(fixture) != fixture_hash:
                raise RuntimeError("Fixture changed during diagnostic")
    manifest["source_unchanged"] = all(sha256(path) == source_hashes[str(path.relative_to(repo))]
                                       for path in sources)
    manifest["executable_unchanged"] = sha256(executable) == executable_hash
    save()
    return 0 if manifest["source_unchanged"] and manifest["executable_unchanged"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
