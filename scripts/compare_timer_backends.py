#!/usr/bin/env python3
"""Alternate independent ent timer and Linux timerfd benchmarks on one host.

Experimental only. Inter-arrival P99 is comparable; absolute deadline lag is
comparable for instrumented Linux millisecond timers only. Neither metric
establishes a production timer real-time SLA.
"""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time

from timer_baseline import (
    DEFAULT_SCENARIOS, METRICS, OPTIONAL, REQUIRED, machine_metadata, parse_case, parse_result, rank
)

def check_cpu_quota_capacity(cpu_max, required_cores):
    """Fail closed if cgroup v2 cannot demonstrate the requested CPU quota.

    An unlimited cpu.max passes this quota *preflight*, but does not guarantee
    four dedicated physical CPUs, a host without noisy neighbors, or a real-time
    scheduling policy.
    """
    if cpu_max is None:
        raise ValueError("cannot verify CPU quota: /sys/fs/cgroup/cpu.max unavailable")
    fields = cpu_max.split()
    if len(fields) != 2:
        raise ValueError("cannot verify CPU quota: invalid cpu.max format")
    quota_text, period_text = fields
    try:
        period = int(period_text)
        quota = None if quota_text == "max" else int(quota_text)
    except ValueError as exc:
        raise ValueError("cannot verify CPU quota: invalid cpu.max numbers") from exc
    if period <= 0 or (quota is not None and quota <= 0):
        raise ValueError("cannot verify CPU quota: non-positive cpu.max values")
    if quota is not None and quota < required_cores * period:
        raise ValueError(
            f"CPU quota permits only {quota / period:.3f} cores, "
            f"below required {required_cores}; adjust runner slice/host limit"
        )
    return None if quota is None else quota / period


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--legacy-bin", type=Path, required=True)
    parser.add_argument("--prototype-bin", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--trials", type=int, default=20)
    parser.add_argument("--duration-ms", type=int, default=1500)
    parser.add_argument("--scenario", action="append")
    parser.add_argument("--pin-first-allowed", action="store_true")
    parser.add_argument("--cpu-width", type=int,
                        help="pin both backends to the first N allowed CPUs (1..4)")
    parser.add_argument("--load-duty-percent", type=int, default=0,
                        help="bounded background load on first selected CPU (0..50%%)")
    parser.add_argument("--require-unrestricted-cpus", type=int, choices=range(1, 5),
                        metavar="N", help="do not pin backends; require exactly N allowed CPUs (1..4)")
    parser.add_argument("--require-cpu-quota-capacity", action="store_true",
                        help="fail closed unless cgroup v2 cpu.max allows N full CPUs; requires --require-unrestricted-cpus N")
    args = parser.parse_args()
    if not 1 <= args.trials <= 100 or not 250 <= args.duration_ms <= 5000:
        parser.error("trials=1..100 and duration-ms=250..5000 required")
    try:
        cases = [parse_case(x) for x in args.scenario or DEFAULT_SCENARIOS]
        binaries = {
            "legacy": args.legacy_bin.resolve(strict=True),
            "timerfd": args.prototype_bin.resolve(strict=True),
        }
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    meta = machine_metadata()
    affinity = meta["cpu_affinity_allowed"]
    if args.pin_first_allowed and args.cpu_width is not None:
        parser.error("--pin-first-allowed and --cpu-width are mutually exclusive")
    if not 0 <= args.load_duty_percent <= 50:
        parser.error("--load-duty-percent must be 0..50")
    width = args.cpu_width if args.cpu_width is not None else (
        1 if args.pin_first_allowed else 0
    )
    if width < 0 or width > 4 or (args.cpu_width is not None and width == 0):
        parser.error("--cpu-width must be 1..4 (or omit for unrestricted affinity)")
    if args.require_cpu_quota_capacity and args.require_unrestricted_cpus is None:
        parser.error("--require-cpu-quota-capacity requires --require-unrestricted-cpus N")
    if args.require_unrestricted_cpus is not None:
        if width:
            parser.error("--require-unrestricted-cpus cannot be combined with CPU pinning")
        if len(affinity) != args.require_unrestricted_cpus:
            parser.error(f"expected exactly {args.require_unrestricted_cpus} allowed CPUs, got {len(affinity)}")
    cpu_quota_capacity_cores = None
    if args.require_cpu_quota_capacity:
        try:
            cpu_quota_capacity_cores = check_cpu_quota_capacity(
                meta.get("cpu_cgroup_max"), args.require_unrestricted_cpus
            )
        except ValueError as exc:
            parser.error(str(exc))
    if width and (len(affinity) < width or shutil.which("taskset") is None):
        parser.error("insufficient allowed CPUs or taskset unavailable")
    cpus = affinity[:width] if width else []
    if args.load_duty_percent and not (cpus or args.require_unrestricted_cpus):
        parser.error("controlled load requires --cpu-width, --pin-first-allowed or --require-unrestricted-cpus")
    if args.load_duty_percent and shutil.which("taskset") is None:
        parser.error("controlled background load requires taskset")
    load_cpu = (cpus[0] if cpus else affinity[0]) if args.load_duty_percent else None
    cpu_list = ",".join(str(cpu) for cpu in cpus)

    meta.update({
        "experiment": "timerfd single epoll dispatcher vs per-timer ent runtime",
        "binary_paths": {name: str(path) for name, path in binaries.items()},
        "trials_per_case_per_backend": args.trials,
        "duration_ms": args.duration_ms,
        "scenarios": [":".join(map(str, case)) for case in cases],
        "pinned_cpu": cpus[0] if len(cpus) == 1 else None,
        "benchmark_affinity_cpus": cpus if cpus else affinity,
        "benchmark_affinity_mode": "subset_pinned" if cpus else "inherited_full_allowed",
        "unrestricted_cpu_count_required": args.require_unrestricted_cpus,
        "cpu_quota_capacity_required": bool(args.require_cpu_quota_capacity),
        "cpu_quota_capacity_cores": cpu_quota_capacity_cores,
        "background_load": {
            "duty_percent_of_one_core": args.load_duty_percent,
            "cpu": load_cpu,
            "cycle_ms": 100,
            "description": "bounded busy/sleep load; not a guarantee of host utilization",
        },
        "ordering": "alternating legacy-timerfd then timerfd-legacy",
        "metric_scope": "sampled interarrival and resource measurements; no SLA",
        "caveat": "delete and close contracts differ; deadline lag is comparable only for instrumented ms timers",
    })
    args.output_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    loader = None
    try:
        if args.load_duty_percent:
            helper = Path(__file__).resolve().with_name("timer_benchmark_load.py")
            loader = subprocess.Popen(
                ["taskset", "-c", str(load_cpu), sys.executable,
                 str(helper), "--duty-percent", str(args.load_duty_percent)],
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
            )
            time.sleep(0.2)
            if loader.poll() is not None:
                raise RuntimeError(
                    "background load exited before benchmark: " +
                    (loader.stderr.read() if loader.stderr else "")
                )
        for mode, count, period in cases:
            key = f"{mode}:{count}:{period}"
            for trial in range(1, args.trials + 1):
                order = ("legacy", "timerfd") if trial % 2 else ("timerfd", "legacy")
                for position, backend in enumerate(order, 1):
                    if loader is not None and loader.poll() is not None:
                        raise RuntimeError("background load exited unexpectedly")
                    command = (["taskset", "-c", cpu_list] if cpus else [])
                    command.extend((str(binaries[backend]), mode, str(count), str(period),
                                    str(args.duration_ms)))
                    result = subprocess.run(command, capture_output=True, text=True,
                                            check=False,
                                            timeout=max(60, args.duration_ms / 1000 + 30))
                    if result.returncode:
                        raise RuntimeError(
                            f"{key}, trial {trial}, backend {backend}: rc={result.returncode}\n"
                            f"{result.stdout[-2000:]}\n{result.stderr[-2000:]}"
                        )
                    metric = parse_result(result.stdout)
                    # Reject mixed old/new binaries when comparing absolute deadlines.
                    # The Linux RT microsecond implementation has a separate
                    # scheduler and is not yet instrumented for that metric.
                    if mode == "ms":
                        for field in OPTIONAL:
                            try:
                                value = float(metric[field])
                            except (KeyError, ValueError) as exc:
                                raise ValueError(f"missing {field} for {key} {backend}") from exc
                            if not math.isfinite(value) or value < 0:
                                raise ValueError(f"invalid {field} for {key} {backend}")
                    if (metric["mode"], int(metric["timers"]), int(metric["period"])) != (
                        mode, count, period
                    ):
                        raise ValueError(f"scenario mismatch {key} backend {backend}")
                    if metric["intervals"] < 2:
                        raise ValueError(f"too few intervals {key} backend {backend}")
                    row = {
                        "scenario": key, "trial": trial, "backend": backend,
                        "position": position,
                        **{field: metric[field] for field in REQUIRED},
                        **{field: metric.get(field, "") for field in OPTIONAL},
                    }
                    rows.append(row)
                    print(f"{key} trial={trial} backend={backend} "
                          f"p99_us={metric['p99_us']:.3f} "
                          f"cpu_pct={metric['cpu_pct_single_core']:.2f}", flush=True)
    except (ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
        (args.output_dir / "error.txt").write_text(str(exc) + "\n", encoding="utf-8")
        raise
    finally:
        if loader is not None:
            if loader.poll() is None:
                loader.terminate()
            try:
                loader.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                loader.kill()
                loader.communicate()

    with (args.output_dir / "paired_samples.csv").open(
        "w", newline="", encoding="utf-8"
    ) as out:
        fields = ["scenario", "trial", "backend", "position", *REQUIRED, *OPTIONAL]
        writer = csv.DictWriter(out, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    summaries = {}
    for mode, count, period in cases:
        key = f"{mode}:{count}:{period}"
        summaries[key] = {}
        for backend in binaries:
            subset = [row for row in rows if row["scenario"] == key and
                      row["backend"] == backend]
            summaries[key][backend] = {}
            # Scheduled deadline lag is now the same measurement for
            # both backends for *millisecond* timers only.
            for field in (*METRICS, *(OPTIONAL if mode == "ms" else ())):
                values = [float(row[field]) for row in subset]
                summaries[key][backend][field] = {
                    "median": statistics.median(values),
                    "p95_trials": rank(values, 0.95),
                    "min": min(values),
                    "max": max(values),
                }
        legacy = {int(x["trial"]): x for x in rows if x["scenario"] == key
                  and x["backend"] == "legacy"}
        prototype = {int(x["trial"]): x for x in rows if x["scenario"] == key
                     and x["backend"] == "timerfd"}
        if len(legacy) != args.trials or len(prototype) != args.trials:
            raise ValueError("incomplete pair count for " + key)
        summaries[key]["paired_p99"] = {
            "timerfd_better_runs": sum(
                float(prototype[t]["p99_us"]) < float(legacy[t]["p99_us"])
                for t in legacy
            ),
            "total_pairs": args.trials,
            "definition": "prototype per-run interarrival P99 strictly lower",
        }
        if mode == "ms":
            summaries[key]["paired_deadline_lag_p99"] = {
                "timerfd_better_runs": sum(
                    float(prototype[t]["deadline_lag_p99_us"]) <
                    float(legacy[t]["deadline_lag_p99_us"])
                    for t in legacy
                ),
                "total_pairs": args.trials,
                "definition": "timerfd scheduled-deadline-to-callback P99 strictly lower (Linux ms only)",
            }
    (args.output_dir / "summary.json").write_text(
        json.dumps({"metadata": meta, "summaries": summaries},
                   indent=2, ensure_ascii=False, allow_nan=False) + "\n",
        encoding="utf-8",
    )
    print(f"captured {len(rows)} independent runs in {args.output_dir}", flush=True)


if __name__ == "__main__":
    main()
