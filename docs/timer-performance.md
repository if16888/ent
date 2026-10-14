# Linux timer scaling measurements

`perf_timer_scale` records diagnostics for the current POSIX timer implementation.
It does **not** implement timerfd/eventfd or claim a hard real-time SLA.

### Metrics and limitations

Each timer records callback timestamps in its own single-writer buffer. Reported
P50/P95/P99 and maximum are **absolute callback inter-arrival errors**, not
scheduled-deadline-to-callback latencies. With fixed-delay `ms` timers, callback
execution time contributes to the observed interval. The CPU figure includes
create, runtime, deletion and close, uses `CLOCK_PROCESS_CPUTIME_ID`, and can exceed
100% of a single core. Thread counts use snapshots from
`/proc/self/status`; they are **not** peak thread counts. `ru_maxrss` is a
process high-water mark in KiB, not instantaneous RSS or a per-timer allocation.

For each timer, `delete_p50_us`, `delete_p95_us`, `delete_p99_us` and
`delete_max_us` describe the elapsed wall-clock time inside
`UTL_TimerDelete` for that timer. `timer_close_us` measures
`UTL_TimerClose` **after individual timers have already been deleted**;
it does not represent the time to drain all active timers.
Normal POSIX timers use one worker thread each; the Linux microsecond RT
implementation uses a timer scheduler plus callback worker per timer.

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

### Reproducible 20-run evidence

Build the release binary once and use the standard-library Python collector:

```sh
python3 scripts/timer_baseline.py \
  --binary build-perf/bin/perf_timer_scale \
  --output-dir timer-results/arm64-before \
  --trials 20 --duration-ms 1500 --pin-first-allowed
```

The default scenarios are `ms:1:20`, `ms:16:20`, `ms:64:20`,
`ms:128:20` and `us:16:1000`. The output is `samples.csv` (all individual
runs) and `summary.json` (machine, git commit, CPU affinity, cgroup CPU quota,
and min/median/P95/max **across trial-level metrics**). These across-trial
statistics are **not** percentiles of pooled callback samples. Use a new,
versioned output folder for each hardware/commit/configuration combination;
do not compare results from unlike runners as if they were paired samples.

The ARM64 workflow checks 1-run, up-to-128-timer startup and delete smoke
automatically. Its **Actions → ARM64 Self-hosted Validation → Run workflow**
manual dispatch also runs the complete 20-trial pinned baseline and uploads
the `timer-baseline-arm64` artifact. This longer measurement does **not** run
on every push or PR. `./scripts/run-ci-linux.sh perf` retains the four
650ms functional smoke cases. The other Linux CI workflow currently uses a
**GitHub-hosted fallback**, not the Oracle x86 machine.

The timer ceiling of 128 is an intentional guardrail for the current
thread-per-timer architecture; this baseline alone does not establish safe
operation for 1000 timers. Record hardware load, scheduler policy and sample
interval truncation before asserting performance improvements.

### timerfd/eventfd architecture decision gate

Before changing the runtime implementation, create an **independent** Linux
timerfd/eventfd prototype and run the same counts/periods at least 20 times
on pinned hardware. Record inter-arrival error, true deadline-to-callback
latency where measurable, per-process CPU, thread counts, RSS and shutdown
latency. Cover callback overrun and self-delete cases. Compare P50/P95/P99
*and* resource budgets; only migrate if lifecycle guarantees and performance
both improve. Current output is baseline evidence only.
