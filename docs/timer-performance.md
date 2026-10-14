# Linux timer scaling measurements

`perf_timer_scale` records diagnostics for the current POSIX timer implementation.
It does **not** implement timerfd/eventfd or claim a hard real-time SLA.

### Metrics and limitations

Each timer records callback timestamps in its own single-writer buffer. Reported
P50/P95/P99 and maximum are **absolute callback inter-arrival errors**, not
scheduled-deadline-to-callback latencies. With fixed-delay `ms` timers, callback
execution time contributes to the observed interval. The CPU figure includes
create, runtime and deletion, uses `CLOCK_PROCESS_CPUTIME_ID`, and can exceed
100% of a single core. Thread counts use snapshots from
`/proc/self/status`; they are **not** peak thread counts. `ru_maxrss` is a
process high-water mark in KiB, not instantaneous RSS or a per-timer allocation.

Jitter can vary with host CPU contention, thermal throttling, scheduler policy,
and callback overrun. The smoke checks functional completion and basic samples,
but intentionally does **not** hard-fail on a percentile threshold.

### Reproduce on Linux

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-perf --target perf_timer_scale --parallel 2
./build-perf/bin/perf_timer_scale ms 1 20 1500
./build-perf/bin/perf_timer_scale ms 16 20 1500
./build-perf/bin/perf_timer_scale ms 64 20 1500
./build-perf/bin/perf_timer_scale us 16 1000 1500
```

Arguments: `ms|us`, timer count (1–128), period in mode units (1–10000),
duration in milliseconds (250–5000). The per-timer timestamp buffer is
capped at 8192; use suitable duration and period to prevent truncation.

`./scripts/run-ci-linux.sh perf` includes these four cases at 650ms
as a diagnostic smoke. Save multiple trials on the same machine with fixed
affinity, scheduler policy, CPU load and build settings before comparing designs.

### timerfd/eventfd architecture decision gate

Before changing the runtime implementation, create an **independent** Linux
timerfd/eventfd prototype and run the same counts/periods at least 20 times
on pinned hardware. Record inter-arrival error, true deadline-to-callback
latency where measurable, per-process CPU, thread counts, RSS and shutdown
latency. Cover callback overrun and self-delete cases. Compare P50/P95/P99
*and* resource budgets; only migrate if lifecycle guarantees and performance
both improve. Current output is baseline evidence only.
