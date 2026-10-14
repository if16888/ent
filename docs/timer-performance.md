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
`perf_timer_scale`; extra `deadline_lag_pXX_us` columns are measured only
in the timerfd prototype, using kernel-arm deadline versus observed callback
timestamp. **The existing ent timer lacks that same metric**, so latency
percentiles must not be directly compared.

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

### Comparable scheduled-deadline lag (instrumented Linux ms timers)

The regular Linux POSIX timer benchmark now compiles its own copy of
`comm/utl_timer.c` with `ENT_TIMER_TEST_HOOKS`, exposing the **actual
absolute monotonic deadline** calculated for each fixed-delay period to
the invoking callback via thread-local state. This does not change or
expose the shipped ent timer library. It records callback-entry monotonic
time minus that stored deadline and emits
`deadline_lag_p50_us/p95_us/p99_us`, matching the timerfd prototype's
absolute-deadline-to-observed-callback definition.

The standalone baseline collector includes these optional deadline fields
in its CSV and per-scenario summaries when present (leaving them blank for
microsecond timers). The paired comparison collector **requires** valid
nonnegative deadline metrics from *both backends* for `ms` scenarios and
aggregates them across trials. Do not silently compare an old benchmark executable lacking the
hook: it now fails explicitly. This is not a formal real-time guarantee,
and instrumentation overhead, different callback dispatch architecture
and CPU contention remain relevant.

The microsecond `UTL_TimerCreateUs` path uses a separate scheduler and
callback worker; its intended deadlines are **not yet traced** with this
hook. Accordingly, `us` scenarios retain only inter-arrival comparisons
and optional prototype-only lag values. Do not compare those lags to
legacy scheduling metrics or claim a common deadline-latency result for
microsecond timers.

### Reentrant shutdown regression (production timer)

`test_utl_timer` exercises the production `UTL_TimerClose` callback guard
with both the ordinary millisecond timer and, on Linux, the separate
microsecond callback worker. A callback attempting to close the runtime must
receive `ENT_TMR_THREAD_FAILED` without shutting down the service. The owner
must subsequently be able to delete the timer and close the service.

This protects existing runtime behavior; it **does not** make the independent
timerfd benchmark lifecycle-equivalent. Production migration still requires
deterministic concurrent Close/Delete, self-deletion, callback overrun and
FD/thread cleanup evidence with sanitizer and repeated-stress coverage.

### Shared timerfd dispatcher: deterministic head-of-line warning

The benchmark-only `perf_timerfd_scale --selftest-hol` mode gates timer 1
*inside the real experimental timerfd/epoll dispatcher* and only then arms
timer 2. Timer 2's monotonic deadline expires while timer 1's synthetic
callback remains blocked. After releasing the gate, the test requires timer 2
to be observed with **at least 15 ms of lateness**, following a 30 ms
controlled block. It also compares process FD and thread counts before and
after shutdown to detect resource leaks in this isolated diagnostic.

This is a deliberate **negative architecture test**: the single dispatcher
cannot run an unrelated callback while its only worker is busy. It does
not mean fixed-delay timing or callback isolation is implemented, does not
establish parity with the public timer API, and should not be turned into a
benchmark latency result. It exposes a critical trade-off requiring an
explicit callback execution design (e.g., separate bounded workers with
specified ordering/backpressure semantics) before production migration.
Ordinary benchmark invocations are unchanged: the gate runs only under
`--selftest-hol`, and Linux CTest exercises the negative test in CI.

### External timer Delete/Close ordering regressions

The POSIX-only `test_utl_timer_thread_safety` now explicitly gates a live,
blocked callback while two external threads contend for shutdown. Its
test-only lifecycle counters establish each ordering without using a
timing assumption:

- **Delete-first:** the external Delete retains a lifecycle operation,
  then Close starts; Close must wait until Delete and callback completion
  before releasing the timer subsystem.
- **Close-first:** Close marks the subsystem closing, and a subsequent
  Delete must return `ENT_TMR_NOT_INITIALIZED` without dereferencing a
  stale handle. After Close completes, the owner must discard that handle.

Both orderings run for ordinary millisecond timers on POSIX, and for the
separate microsecond callback-worker path on Linux. The normal Linux x64
and ARM64 CI stress loops repeat these tests; sanitizer lanes additionally
exercise them. This establishes the **existing production** lifecycle
contract only. It does not prove the timerfd prototype implements
equivalent handle ownership or callback behavior.

### Paired deadline-lag evidence and four-CPU collection

After the benchmark-only `ENT_TIMER_TEST_HOOKS` instrumentation, Linux
millisecond timer comparisons now expose **paired** `deadline_lag_p99_us`
winner counts in `summary.json`, in addition to inter-arrival P99 wins.
Each row in `paired_samples.csv` retains the raw per-run deadline metrics.
Microsecond timer scenarios have **no** paired deadline-lag comparison:
only their inter-arrival measurements are currently comparable. These
comparisons are observational, not a hard real-time guarantee.

`--require-unrestricted-cpus 4` is a **fail-closed** host prerequisite for
four-CPU experiments: the benchmark child processes inherit the entire
four-CPU allowed affinity from the job without `taskset` pinning. The
collector rejects hosts whose allowed CPU affinity is not *exactly* four;
a CPU cgroup quota or noisy neighbors can still prevent four full CPUs of
capacity. The optional background loader is pinned to the first allowed
CPU, while both benchmark backends retain full inherited affinity.

Run from **Actions → ARM64 Self-hosted Validation → Run workflow** on the
chosen, fixed SHA with `full_four_cpu_ab=true`. This opt-in mode adds
20 paired trials × five scenarios × two backends for each of two conditions:
idle and 25%-duty single-CPU synthetic interference (**400 subprocess
runs total**). Raw CSV, summaries, exact git SHA, allowed affinity, CPU
cgroup metadata and the requested loader duty are retained in artifact
`timerfd-fourcpu-arm64-deadline-lag`. On failure, inspect the archived
partial data and error.txt; do not call the gate passed. This manual run
does **not** replace the separate, previously archived pinned single-core
baseline and does not qualify as representative controlled field load.

The opt-in four-CPU long experiment also requires
`--require-cpu-quota-capacity`: the collector must read and validate
`/sys/fs/cgroup/cpu.max` (cgroup v2). A finite quota of fewer than
four complete logical CPUs (for example, `300000 100000` = 3 cores)
fails **before sampling starts**. An unlimited quota (`max 100000`)
passes this particular quota check. Missing, invalid, or unsupported
quota evidence is a hard error for this manual acceptance experiment.
The raw `cpu_cgroup_max` field and resolved capacity remain in metadata.
This prevents a four-CPU *affinity* measurement under a smaller
cgroup CPU-time ceiling from being represented as a full four-CPU
capacity measurement. It still does not reserve dedicated cores, eliminate
CPU steal, or make a hard real-time guarantee.

A short four-CPU collector smoke runs automatically on ARM64 only when
the runner offers exactly four allowed CPUs. The standard PR tests also
exercise paired-summary semantics with deterministic fake backends; neither
smoke result establishes a latency budget. Evaluate both per-trial P99
medians and **worst-run** P99 deadline lag, alongside CPU, threads, memory,
lifecycle equivalence and rollback before deciding on migration.

### timerfd/eventfd architecture decision gate

Before changing the runtime implementation, create an **independent** Linux
timerfd/eventfd prototype and run the same counts/periods at least 20 times
on pinned hardware. Record inter-arrival error, true deadline-to-callback
latency where measurable, per-process CPU, thread counts, RSS and shutdown
latency. Cover callback overrun and self-delete cases. Compare P50/P95/P99
*and* resource budgets; only migrate if lifecycle guarantees and performance
both improve. Current output is baseline evidence only.
