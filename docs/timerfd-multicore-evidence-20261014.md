# ARM64 timerfd multicore / controlled-load A/B evidence — 2026-10-14

**Decision:** Experimental performance evidence **PASS**; production timerfd
migration **NO-GO**. Follow-up lifecycle-equivalence and comparable deadline-lag
validation remain prerequisites. This document reports experiments, not a hard
real-time performance commitment.

## Provenance and reproducibility

- Production-equivalent source snapshot: `master@ca4fa546250de711e544d014ed94922dd8b33c9f`
  (PR #24, bounded load and multicore benchmark collector).
- Benchmark-only branch: `bench/timerfd-multicore-20261014`, workflow trigger
  commit `fd7388d977edc58b7684a91535bc6029d60fbb58`; this commit
  changes the Actions workflow, **not** the timer implementation.
- Completed [ARM64 Actions run #38549130236](https://github.com/if16888/ent/actions/runs/38549130236),
  result **success**, at 2026-10-14 10:22–10:33 UTC.
- Actions artifact **`timerfd-arm64-multicore-load`** (artifact ID
  `11886003442`) contains the actual raw `paired_samples.csv` and
  `summary.json` for **both conditions**.
- ARM64 Oracle-hosted Linux, *first two allowed logical CPUs* selected
  through `taskset` for **both** backends, not isolated dedicated physical
  cores. Both experiments use **five scenarios × 20 paired trials × two
  backends = 200 independent runs each**; **400 runs total**.
- Each run lasts 1500 ms; execution order alternates legacy→timerfd
  and timerfd→legacy. The load condition adds a separate process on the
  first selected CPU, with a synthetic 100 ms cycle and 25% requested
  busy-duty. This is **not** a measured or guaranteed 25% sustained CPU load.
  Its CPU time is **excluded** from the benchmark process CPU metric.
- Statistics below represent **median of each run's P99** and the
  **worst per-run P99**, not a pooled percentile of all callback samples.
  The error is the absolute difference between successive callback
  timestamps and the configured period, **not deadline-to-callback lag**.

## Condition A: two allowed logical CPUs, no injected load

| Scenario | Legacy median P99 (µs) | timerfd median P99 (µs) | timerfd paired wins | Legacy worst run P99 (µs) | timerfd worst run P99 (µs) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 timer × 20 ms | 105.47 | 46.99 | 20/20 | 690.93 | 65.61 |
| 16 timers × 20 ms | 123.93 | 66.47 | 17/20 | 2566.51 | 1151.94 |
| 64 timers × 20 ms | 133.31 | 88.79 | 18/20 | 285.33 | **871.26** |
| 128 timers × 20 ms | 109.93 | 92.43 | 16/20 | 122.29 | **1902.14** |
| 16 timers × 1 ms | 28.39 | 20.45 | 12/20 | 54.07 | 27.65 |

Although the timerfd **median** is lower in every scenario, its **worst
per-run P99 is worse** at 64 and 128 timers. The 128-timer timerfd
worst run exceeds 1.9 ms even though the legacy worst is around 0.12 ms.
This is a critical tail-risk observation, not grounds to declare a
uniform timerfd victory.

## Condition B: same two CPUs, 25%-duty synthetic load on one CPU

| Scenario | Legacy median P99 (µs) | timerfd median P99 (µs) | timerfd paired wins | Legacy worst run P99 (µs) | timerfd worst run P99 (µs) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 timer × 20 ms | 83.35 | 34.03 | 17/20 | 1515.82 | 446.41 |
| 16 timers × 20 ms | 378.91 | 53.57 | 20/20 | 2449.43 | 791.22 |
| 64 timers × 20 ms | 905.78 | 75.95 | 20/20 | 1790.90 | 536.97 |
| 128 timers × 20 ms | 960.52 | 87.67 | 20/20 | 1750.62 | 126.77 |
| 16 timers × 1 ms | 39.08 | 22.79 | 20/20 | 169.29 | 32.89 |

In this **particular** interference model, timerfd wins all 20 paired
trials in the four multi-timer scenarios. The large differences for
64/128 ordinary timers suggest that scheduling many individual timer
workers can amplify contention. However, host co-tenancy and phase
ordering were not fully controlled; the unloaded and loaded stages ran
sequentially, so the stages cannot be treated as a randomized
load-vs-idle causal experiment.

## Process CPU observations (median percentage of one logical core)

| Scenario | Unloaded legacy / timerfd | Loaded legacy / timerfd |
| --- | ---: | ---: |
| 1 timer × 20 ms | 0.06 / 0.07 | 0.06 / 0.07 |
| 16 timers × 20 ms | 0.49 / 0.18 | 0.53 / 0.18 |
| 64 timers × 20 ms | 1.98 / 0.76 | 2.12 / 0.76 |
| 128 timers × 20 ms | 4.25 / 1.55 | 4.32 / 1.55 |
| 16 timers × 1 ms | 12.27 / 3.21 | 11.59 / 3.12 |

The measured process CPU excludes the separate synthetic loader.
The single-timer comparison does not show a material CPU advantage.

## Interpretation limits and acceptance gates

1. This is a **synthetic and bounded** taskset test on a shared virtual
   machine, not an isolated RTDS or field workload. A per-core busy-duty
   helper is not an accurate representation of real power-station CPU
   contention. CPU governor, hypervisor steal, other host activity and
   thread placement are uncontrolled. No hard real-time SLA.
2. **Metrics are not yet semantically equivalent for deadline latency.**
   We compare observable callback inter-arrival error; the timerfd
   prototype's optional internal deadline-lag metric has no directly
   paired legacy counterpart in this dataset.
3. **Lifecycle semantics are not equivalent.** The standalone timerfd
   prototype only timestamps lightweight callbacks. It does not implement
   the production ent timer's self-delete, slow callbacks, ownership,
   reentrant close, or concurrent cancellation contracts. Its delete
   and close times do not measure identical work.
4. Re-run on controlled x86/ARM hosts under representative mixed load;
   preserve entire raw runs and compare **worst-run** tails as well as
   cross-run P95. Investigate the idle 64/128 timerfd outliers.
5. Before considering production replacement, prove lifecycle
   equivalence with ASan/UBSan/TSan and multi-thread stress, then compare
   **actual deadline-to-callback latency** defined consistently for both
   implementations.

**Decision:** Keep the existing ent timer production implementation.
The timerfd prototype remains an independent feasibility benchmark.
CI/benchmark evidence is **GO**; production timerfd migration and release
are **NO-GO**. No release tag created.
