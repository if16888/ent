#!/usr/bin/env python3
"""Repeat timer-scale experiments with machine metadata and auditable raw results.

No third-party dependencies. Percentiles summarize PER-TRIAL metrics, not pooled
callback samples. Do not interpret inter-arrival error as deadline latency.
"""
import argparse
import csv
import datetime
import json
import math
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys

DEFAULT_SCENARIOS = ("ms:1:20", "ms:16:20", "ms:64:20", "ms:128:20", "us:16:1000")
REQUIRED = (
    "mode", "timers", "period", "duration_ms", "hits", "intervals",
    "abs_interarrival_error_p50_us", "p95_us", "p99_us", "max_us",
    "cpu_process_ms", "wall_ms", "cpu_pct_single_core",
    "threads_before", "threads_active", "threads_after", "ru_maxrss_kib",
    "delete_p50_us", "delete_p95_us", "delete_p99_us", "delete_max_us",
    "timer_close_us",
)
OPTIONAL = ("deadline_lag_p50_us", "deadline_lag_p95_us", "deadline_lag_p99_us")
METRICS = (
    "abs_interarrival_error_p50_us", "p95_us", "p99_us", "max_us",
    "cpu_pct_single_core", "threads_active", "ru_maxrss_kib",
    "delete_p50_us", "delete_p95_us", "delete_p99_us", "delete_max_us",
    "timer_close_us",
)


def parse_case(spec):
    parts = spec.split(":")
    if len(parts) != 3 or parts[0] not in ("ms", "us"):
        raise ValueError("scenario must be ms|us:timers:period")
    try:
        count, period = map(int, parts[1:])
    except ValueError as exc:
        raise ValueError("scenario timers and period must be integers") from exc
    if not 1 <= count <= 128 or not 1 <= period <= 10000:
        raise ValueError("scenario count must be 1..128, period 1..10000")
    return parts[0], count, period


def parse_result(stdout):
    lines = [line for line in stdout.splitlines() if line.startswith("timer_scale ")]
    if len(lines) != 1:
        raise ValueError("expected exactly one timer_scale result line")
    result = {}
    for part in lines[0].split()[1:]:
        key, sep, value = part.partition("=")
        if not sep or not key or key in result:
            raise ValueError("invalid or duplicate metric: " + part)
        result[key] = value
    missing = set(REQUIRED) - set(result)
    if missing:
        raise ValueError("missing timer metrics: " + ", ".join(sorted(missing)))
    for key in REQUIRED:
        if key == "mode":
            if result[key] not in ("ms", "us"):
                raise ValueError("invalid mode")
        else:
            value = float(result[key])
            if not math.isfinite(value):
                raise ValueError("nonfinite metric: " + key)
            result[key] = value
    unknown = set(result) - set(REQUIRED) - set(OPTIONAL)
    if unknown:
        raise ValueError("unknown timer metrics: " + ", ".join(sorted(unknown)))
    for key in OPTIONAL:
        if key in result:
            value = float(result[key])
            if not math.isfinite(value) or value < 0:
                raise ValueError("invalid optional metric: " + key)
            result[key] = value
    return result


def rank(values, fraction):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def machine_metadata():
    affinity = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else []
    meta = {
        "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "uname": platform.uname()._asdict(),
        "python": platform.python_version(),
        "cpu_affinity_allowed": affinity,
        "git_sha": None,
        "cpu_cgroup_max": None,
    }
    git = subprocess.run(["git", "rev-parse", "--verify", "HEAD"], text=True,
                         capture_output=True, check=False)
    if git.returncode == 0:
        meta["git_sha"] = git.stdout.strip()
    cpu_max = Path("/sys/fs/cgroup/cpu.max")
    if cpu_max.is_file():
        meta["cpu_cgroup_max"] = cpu_max.read_text(encoding="utf-8").strip()
    return meta


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True,
                        help="path to built bin/perf_timer_scale")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--trials", type=int, default=20)
    parser.add_argument("--duration-ms", type=int, default=1500)
    parser.add_argument("--scenario", action="append",
                        help="repeatable ms|us:timers:period; defaults to five cases")
    parser.add_argument("--pin-first-allowed", action="store_true",
                        help="pin each process to first available CPU with taskset")
    args = parser.parse_args()

    if not 1 <= args.trials <= 100 or not 250 <= args.duration_ms <= 5000:
        parser.error("--trials must be 1..100, --duration-ms must be 250..5000")
    try:
        scenarios = [parse_case(x) for x in (args.scenario or DEFAULT_SCENARIOS)]
    except ValueError as exc:
        parser.error(str(exc))
    binary = args.binary.resolve(strict=True)
    if not binary.is_file():
        parser.error("--binary must be a file")
    meta = machine_metadata()
    cpu = None
    if args.pin_first_allowed:
        if not meta["cpu_affinity_allowed"] or not shutil.which("taskset"):
            parser.error("taskset and CPU affinity support required for pinning")
        cpu = meta["cpu_affinity_allowed"][0]
    meta.update({
        "binary": str(binary), "trials_per_case": args.trials,
        "duration_ms": args.duration_ms,
        "scenarios": [":".join(map(str, case)) for case in scenarios],
        "pinned_cpu": cpu,
        "percentile_scope": "per_trial (not pooled callbacks)",
        "jitter_definition": "absolute callback inter-arrival error (not deadline latency)",
    })
    args.output_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    try:
        for mode, count, period in scenarios:
            scenario = f"{mode}:{count}:{period}"
            for trial in range(1, args.trials + 1):
                command = ([ "taskset", "-c", str(cpu)] if cpu is not None else [])
                command += [str(binary), mode, str(count), str(period), str(args.duration_ms)]
                completed = subprocess.run(
                    command, capture_output=True, text=True,
                    timeout=max(60, args.duration_ms / 1000 + 30), check=False,
                )
                if completed.returncode:
                    raise RuntimeError(
                        f"{scenario} trial {trial} failed rc={completed.returncode}\n"
                        f"stdout={completed.stdout[-3000:]}\nstderr={completed.stderr[-3000:]}"
                    )
                metric = parse_result(completed.stdout)
                if (metric["mode"], int(metric["timers"]), int(metric["period"])) != (mode, count, period):
                    raise ValueError("binary returned mismatched scenario")
                if metric["intervals"] < 2:
                    raise ValueError("too few interval samples")
                rows.append({"scenario": scenario, "trial": trial, **metric})
                print(f"{scenario} trial {trial}/{args.trials} "
                      f"p99_interarrival_us={metric['p99_us']:.3f} "
                      f"cpu_pct={metric['cpu_pct_single_core']:.2f}", flush=True)
    except (RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
        (args.output_dir / "error.txt").write_text(str(exc) + "\n", encoding="utf-8")
        raise

    with (args.output_dir / "samples.csv").open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=["scenario", "trial", *REQUIRED, *OPTIONAL])
        writer.writeheader()
        writer.writerows(rows)

    summaries = {}
    for mode, count, period in scenarios:
        key = f"{mode}:{count}:{period}"
        subset = [row for row in rows if row["scenario"] == key]
        summaries[key] = {}
        for field in (*METRICS, *OPTIONAL):
            # The microsecond RT path intentionally has no common deadline
            # instrumentation; preserve a blank CSV column, omit its summary.
            if not all(field in row for row in subset):
                continue
            values = [float(row[field]) for row in subset]
            summaries[key][field] = {
                "min": min(values),
                "median": statistics.median(values),
                "p95_trials": rank(values, 0.95),
                "max": max(values),
            }
    with (args.output_dir / "summary.json").open("w", encoding="utf-8") as out:
        json.dump({"metadata": meta, "summaries": summaries},
                  out, indent=2, ensure_ascii=False, allow_nan=False)
        out.write("\n")
    print(f"saved {len(rows)} trials to {args.output_dir}", flush=True)


if __name__ == "__main__":
    main()
