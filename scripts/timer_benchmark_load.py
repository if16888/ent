#!/usr/bin/env python3
"""Bounded and disposable CPU load for *experimental* timer A/B measurements.

Run under taskset; the parent benchmark terminates this subprocess on exit.
Uses a 100 ms cycle with <=50% busy time, never occupies a full CPU core.
This is a synthetic controlled process, not a stable CPU-utilization guarantee.
"""
import argparse
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--duty-percent", type=int, required=True)
    p.add_argument("--max-seconds", type=int, default=1200)
    args = p.parse_args()
    if not 1 <= args.duty_percent <= 50 or not 1 <= args.max_seconds <= 1200:
        p.error("duty-percent 1..50 and max-seconds 1..1200 required")
    cycle = 0.1
    busy = cycle * args.duty_percent / 100.0
    stop = time.monotonic() + args.max_seconds
    while time.monotonic() < stop:
        deadline = time.perf_counter() + busy
        while time.perf_counter() < deadline:
            pass
        time.sleep(cycle - busy)


if __name__ == "__main__":
    main()
