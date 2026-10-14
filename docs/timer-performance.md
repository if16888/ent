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

### Independent timerfd/epoll/eventfd feasibility prototype

Linux now also builds `perf_timerfd_scale`, an **isolated benchmark only**,
not a new timer implementation linked into ent. It uses timerfd one-shot
absolute deadlines, epoll with a single dispatcher, and eventfd for shutdown.
After each callback sample, the timer is re-armed for the next fixed-delay
period. The common output columns support a controlled comparison to
`perf_timer_scale`.

Linux `perf_timer_scale` is also built with a **benchmark-only** internal
deadline hook for normal millisecond timers; its callback records the
original monotonic deadline computed by the POSIX worker before waiting.
Both benchmark binaries therefore report `deadline_lag_p50_us`,
`deadline_lag_p95_us` and `deadline_lag_p99_us` for `ms` tests,
measured from planned MONOTONIC deadline to the timestamp at the event/
callback sampling point. They use the same clock and deadline definition,
but their internal sampling points differ slightly (the prototype samples
after reading timerfd; the existing timer samples inside its callback).
These are observational latencies, not a hard real-time SLA. Microsecond
legacy timers use a separate RT worker and **do not** report these fields,
so the `us` scenario has no comparable deadline-lag pair. Do not compare
prototype-only lag to legacy inter-arrival error. The instrumentation
macro `ENT_TIMER_BENCH_DEADLINE_HOOK` is set only for the benchmark target,
never the published ent library.

Run a short functional sanity check:

```sh
./build-perf/bin/perf_timerfd_scale ms 16 20 650
python3 scripts/compare_timer_backends.py \
  --legacy-bin build-perf/bin/perf_timer_scale \
  --prototype-bin build-perf/bin/perf_timerfd_scale \
  --output-dir timer-results/arm64-ab \
  --trials 20 --duration-ms 1500 --pin-first-allowed
```

The paired comparison alternates execution order (AB then BA). Outputs:
`paired_samples.csv`, `summary.json`, and `error.txt` on any failed
measurement. The GitHub ARM64 workflow runs short, non-threshold A/B smoke
on each PR; **manual workflow dispatch** runs the full 20-trial A/B experiment
and uploads `timerfd-comparison-arm64`.

**Known non-equivalence:** the epoll prototype has one dispatcher thread,
whereas ent currently has a worker per ordinary timer and two per microsecond
timer. The prototype only timestamps callbacks; it does not emulate arbitrary
slow callbacks, callback self-deletion, backpressure, thread affinity for
individual callbacks, or multi-owner external API races. Its timer-delete
latency is epoll removal plus FD close, while ent includes worker termination.
The prototype's `timer_close_us` also includes joining the dispatcher.
Consequently these values are resource/feasibility evidence, **not sufficient
for a production architecture migration**.

### ARM64 20-trial A/B evidence (2026-10-14)

See [measured comparison and decision boundaries](timerfd-ab-evidence-20261014.md).
The 20-trial dataset is attached to
[Actions run #38523577481](https://github.com/if16888/ent/actions/runs/38523577481)
as `timerfd-comparison-arm64`. A separate follow-up benchmark correction
stabilizes the post-shutdown thread-count snapshot; it **does not** revise
or silently replace the archived original measurements.

### Bounded multicore and controlled-load experiments

For ARM64 and Linux x64 comparisons, `compare_timer_backends.py` can now
restrict both backends to the **same first N allowed CPUs**. For a synthetic
load probe, a separate subprocess runs on the first selected CPU with a
100 ms cycle and a bounded busy duty (maximum 50% of one CPU). The loader
is terminated and reaped when the collector exits, and exits on its own
after at most 1200 seconds. These controls do **not** guarantee CPU
utilization on a shared VM, nor isolate noisy neighbors, CPU frequency,
or load outside the benchmark's affinity. Benchmark process CPU percent
does not include the separate load subprocess.

```sh
python3 scripts/compare_timer_backends.py \
  --legacy-bin build-perf/bin/perf_timer_scale \
  --prototype-bin build-perf/bin/perf_timerfd_scale \
  --output-dir timer-results/arm64-multicore \
  --trials 20 --duration-ms 1500 --cpu-width 2

python3 scripts/compare_timer_backends.py \
  --legacy-bin build-perf/bin/perf_timer_scale \
  --prototype-bin build-perf/bin/perf_timerfd_scale \
  --output-dir timer-results/arm64-multicore-load25 \
  --trials 20 --duration-ms 1500 --cpu-width 2 \
  --load-duty-percent 25
```

Each output carries the exact allowed affinity, selected CPU IDs, requested
load duty and a **paired per-run P99 win count**, in addition to raw results
and min/median/P95/worst across run-level metrics. The smoke tests validate
the collector and the load-process lifecycle but **do not** enforce jitter
thresholds. Use a dedicated benchmark host/time window and record ambient
load and cgroup CPU quotas. Do not compare these two-core data directly to
the earlier one-core pinned baseline as though they were paired measurements.

### timerfd/eventfd architecture decision gate

Before changing the runtime implementation, create an **independent** Linux
timerfd/eventfd prototype and run the same counts/periods at least 20 times
on pinned hardware. Record inter-arrival error, true deadline-to-callback
latency where measurable, per-process CPU, thread counts, RSS and shutdown
latency. Cover callback overrun and self-delete cases. Compare P50/P95/P99
*and* resource budgets; only migrate if lifecycle guarantees and performance
both improve. Current output is baseline evidence only.
