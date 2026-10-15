#!/usr/bin/env python3
"""Fail-closed audit of opt-in four-CPU timerfd A/B raw evidence.

Evidence completeness is not a real-time SLA or production migration approval.
The independent benchmark does not run actual application callbacks.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import sys

from compare_timer_backends import check_cpu_quota_capacity
from timer_baseline import DEFAULT_SCENARIOS

SCENARIOS = list(DEFAULT_SCENARIOS)
BACKENDS = ("legacy", "timerfd")
TRIALS = 20
DURATION_MS = 1500


def fail(message):
    raise ValueError(message)


def nonnegative(row, field, context):
    try:
        value = float(row[field])
    except (KeyError, ValueError, TypeError) as exc:
        raise ValueError(f"{context}: missing/non-numeric {field}") from exc
    if not math.isfinite(value) or value < 0:
        fail(f"{context}: invalid nonnegative {field}: {value}")
    return value


def same_number(actual, expected, context):
    if not math.isclose(float(actual), float(expected), rel_tol=1e-9,
                        abs_tol=1e-6):
        fail(f"{context}: summary={actual} raw={expected}")


def audit_condition(folder, expected_sha, duty_percent):
    folder = Path(folder)
    with (folder / "summary.json").open(encoding="utf-8") as f:
        report = json.load(f)
    meta = report["metadata"]
    expected_metadata = {
        "git_sha": expected_sha,
        "benchmark_affinity_mode": "inherited_full_allowed",
        "unrestricted_cpu_count_required": 4,
        "cpu_quota_capacity_required": True,
        "duration_ms": DURATION_MS,
        "trials_per_case_per_backend": TRIALS,
    }
    for field, expected in expected_metadata.items():
        if meta.get(field) != expected:
            fail(f"{folder}: {field} is {meta.get(field)!r}, expected {expected!r}")
    if meta.get("scenarios") != SCENARIOS:
        fail(f"{folder}: scenario list/order does not match required baseline")
    cpus = meta.get("cpu_affinity_allowed")
    if not isinstance(cpus, list) or len(cpus) != 4 or len(set(cpus)) != 4:
        fail(f"{folder}: exactly four distinct allowed CPU IDs required")
    if not all(type(cpu) is int and cpu >= 0 for cpu in cpus):
        fail(f"{folder}: invalid CPU IDs")
    if meta.get("benchmark_affinity_cpus") != cpus:
        fail(f"{folder}: child benchmark affinity differs from allowed CPU list")
    quota = check_cpu_quota_capacity(meta.get("cpu_cgroup_max"), 4)
    recorded = meta.get("cpu_quota_capacity_cores")
    if quota is None:
        if recorded is not None:
            fail(f"{folder}: unlimited cgroup quota metadata mismatch")
    elif recorded is None:
        fail(f"{folder}: finite cgroup capacity missing from metadata")
    else:
        same_number(recorded, quota, f"{folder}: quota capacity")
    load = meta.get("background_load")
    if not isinstance(load, dict) or load.get("duty_percent_of_one_core") != duty_percent:
        fail(f"{folder}: load duty must be {duty_percent}")
    if duty_percent and load.get("cpu") != cpus[0]:
        fail(f"{folder}: background load CPU must match first allowed CPU")
    if set(report["summaries"]) != set(SCENARIOS):
        fail(f"{folder}: incomplete or unexpected summary scenarios")

    with (folder / "paired_samples.csv").open(newline="", encoding="utf-8") as f:
        source_rows = list(csv.DictReader(f))
    if len(source_rows) != len(SCENARIOS) * TRIALS * len(BACKENDS):
        fail(f"{folder}: wrong raw row count {len(source_rows)} (expected 200)")
    rows = {}
    for row in source_rows:
        key = row.get("scenario")
        if key not in SCENARIOS:
            fail(f"{folder}: unknown scenario {key}")
        context = f"{folder} {key}"
        try:
            trial = int(row["trial"])
            position = int(row["position"])
        except (KeyError, TypeError, ValueError) as exc:
            raise ValueError(f"{context}: invalid trial/position") from exc
        backend = row.get("backend")
        if not 1 <= trial <= TRIALS or backend not in BACKENDS:
            fail(f"{context}: invalid trial/backend {trial}/{backend}")
        expected_position = (1 if backend == "legacy" else 2) if trial % 2 else (
            2 if backend == "legacy" else 1)
        if position != expected_position:
            fail(f"{context} trial {trial}: A/B alternation invalid")
        mode, timers, period = key.split(":")
        if (row.get("mode") != mode or row.get("timers") != timers or
                row.get("period") != period or
                row.get("duration_ms") != str(DURATION_MS)):
            fail(f"{context} trial {trial}: row scenario/duration mismatch")
        values = {
            "p99_us": nonnegative(row, "p99_us", context)
        }
        if mode == "ms":
            values["deadline_lag_p99_us"] = nonnegative(
                row, "deadline_lag_p99_us", context)
        identity = (key, trial, backend)
        if identity in rows:
            fail(f"{context}: duplicate trial/backend {trial}/{backend}")
        rows[identity] = values

    condition_metrics = {}
    for key in SCENARIOS:
        mode = key.split(":")[0]
        summary = report["summaries"][key]
        current = {}
        for backend in BACKENDS:
            metrics = {}
            names = ("p99_us", "deadline_lag_p99_us") if mode == "ms" else ("p99_us",)
            for name in names:
                values = [rows[(key, trial, backend)][name]
                          for trial in range(1, TRIALS + 1)]
                expected_summary = summary[backend][name]
                same_number(expected_summary["median"], statistics.median(values),
                            f"{key}/{backend}/{name}/median")
                same_number(expected_summary["max"], max(values),
                            f"{key}/{backend}/{name}/max")
                metrics[name] = {"median": statistics.median(values), "worst": max(values)}
            current[backend] = metrics
        for field, metric in (("paired_p99", "p99_us"),
                              ("paired_deadline_lag_p99", "deadline_lag_p99_us")):
            if metric not in current["legacy"]:
                if field in summary:
                    fail(f"{key}: microsecond deadline lag cannot be paired")
                continue
            winner_count = sum(
                rows[(key, trial, "timerfd")][metric] <
                rows[(key, trial, "legacy")][metric]
                for trial in range(1, TRIALS + 1))
            paired = summary[field]
            if (paired.get("total_pairs") != TRIALS or
                    paired.get("timerfd_better_runs") != winner_count):
                fail(f"{key}: paired {metric} wins/total disagree with raw data")
            current[field] = {"timerfd_better_runs": winner_count,
                              "total_pairs": TRIALS}
        condition_metrics[key] = current

    return {
        "git_sha": expected_sha,
        "allowed_cpus": cpus,
        "cpu_cgroup_max": meta["cpu_cgroup_max"],
        "load_duty_percent": duty_percent,
        "independent_runs": len(source_rows),
        "scenarios": condition_metrics,
    }


def audit_pair(idle_dir, loaded_dir, expected_sha):
    idle = audit_condition(idle_dir, expected_sha, 0)
    loaded = audit_condition(loaded_dir, expected_sha, 25)
    if idle["allowed_cpus"] != loaded["allowed_cpus"]:
        fail("idle/loaded effective affinity mismatch")
    if idle["cpu_cgroup_max"] != loaded["cpu_cgroup_max"]:
        fail("idle/loaded cgroup quota changed; cannot treat as same environment")
    return {
        "evidence_integrity": "PASS",
        "experiment": "timerfd vs legacy, callback-free prototype, four allowed CPUs",
        "production_migration": "NO-GO",
        "caveat": "No callback execution parity, realtime SLA or lifecycle equivalence",
        "total_independent_runs": 400,
        "idle": idle,
        "loaded25": loaded,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idle-dir", type=Path, required=True)
    parser.add_argument("--loaded-dir", type=Path, required=True)
    parser.add_argument("--expected-sha", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = audit_pair(args.idle_dir, args.loaded_dir, args.expected_sha)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        parser.exit(1, f"timerfd four-CPU evidence INVALID: {exc}\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n",
                           encoding="utf-8")
    print("400-run idle/loaded evidence integrity PASS; production migration NO-GO")


if __name__ == "__main__":
    main()
