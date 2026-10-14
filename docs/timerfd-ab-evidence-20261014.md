# ARM64 timerfd feasibility A/B — 2026-10-14

**Decision:** GO to controlled multi-core / loaded follow-up validation;
**NO-GO** for production timerfd migration or real-time performance claims.

## Provenance and protocol

- Source snapshot: `master@c359c90a755cb8229e86ae2128611df2ee4b64aa`;
  benchmark-only workflow trigger commit `228dd60109cc2d1cbf136d03503ab07d4e1b8d8d`.
  Production source and experimental benchmark programs were unchanged
  between those commits.
- Runner: Linux aarch64, Oracle-hosted ARM64; CPU affinity pinned to **CPU 0**
  for both binaries. Kernel `7.0.0-1012-oracle`.
- Repetitions: **20 independent runs per backend and per scenario**;
  5 scenarios × 2 backends × 20 = **200 total** runs.
- Duration: 1500 ms per run. Each trial executes both backends in
  alternating AB/BA order to reduce ordering bias.
- [Exact GitHub Actions run #38523577481](https://github.com/if16888/ent/actions/runs/38523577481)
  (successful). Artifact `timerfd-comparison-arm64` contains
  `paired_samples.csv` (all 200 rows) and `summary.json` (machine metadata
  and across-trial summaries). Preserve this artifact with the run.
- Statistics below are **medians across the 20 per-run metrics**, not a pooled
  callback percentile. All five scenarios have 20 successful runs for each
  backend; all trial entries have at least two callback intervals.

## Measured callback inter-arrival error

Absolute difference between consecutive callback timestamps and configured
period, in microseconds (µs). **Not** scheduled-deadline-to-callback latency.

| Scenario | Legacy median of per-run P99 (µs) | timerfd median (µs) | timerfd lower P99 in paired runs | Legacy worst per-run P99 (µs) | timerfd worst (µs) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 timer @ 20 ms | 86.05 | 33.97 | 18/20 | 2081.11 | 757.98 |
| 16 timers @ 20 ms | 109.27 | 61.17 | 13/20 | 137.01 | 575.90 |
| 64 timers @ 20 ms | 159.65 | 84.71 | 17/20 | 297.65 | 1488.14 |
| 128 timers @ 20 ms | 152.11 | 98.55 | 19/20 | 210.13 | 165.61 |
| 16 timers @ 1000 µs | 33.01 | 22.47 | 14/20 | 62.45 | 40.69 |

Some timerfd trials have **worse extreme P99** even when their median is
better (especially 16 and 64 timers). The prototype is therefore **not**
uniformly superior for all observed tail events. A sample of 20 runs, each
1.5 s, is insufficient to establish long-term rare-event latency bounds.

## CPU and thread footprints

| Scenario | CPU (% of one core): legacy / timerfd | Active thread snapshot: legacy / timerfd |
| --- | ---: | ---: |
| 1 timer @ 20 ms | 0.05 / 0.06 | 2 / 2 |
| 16 timers @ 20 ms | 0.34 / 0.19 | 17 / 2 |
| 64 timers @ 20 ms | 1.58 / 0.75 | 65 / 2 |
| 128 timers @ 20 ms | 3.36 / 1.54 | 129 / 2 |
| 16 timers @ 1000 µs | 8.35 / 3.15 | 33 / 2 |

CPU values are medians of per-process CPU divided by benchmark wall time.
They include setup and deletion. **Single-timer CPU showed no improvement.**
The thread count is a point-in-time snapshot, not a verified peak.
The prototype uses a single epoll dispatcher; the legacy timer uses one
worker per millisecond timer and additional workers for microsecond timers.

## Limitations and correctness gaps

1. **Pinned one-core test only.** It stresses scheduling contention but does
   not establish typical throughput or P99 on all four ARM cores, nor on x86.
   There was no controlled background load, overrun injection, or CPU governor
   control; no hard real-time SLA can be inferred.
2. **Not semantically equivalent.** The prototype only records cheap callback
   timestamps. It lacks slow-callback handling, callback self-delete,
   external concurrent cancellation, reentrancy guarantees, and lifecycle
   ownership contracts of the production runtime.
3. **Shutdown timings are not directly comparable.** Legacy per-timer delete
   joins workers; prototype deletion removes/close timer FDs; prototype
   `timer_close_us` includes dispatcher join while legacy Close occurs after
   timers have already been deleted.
4. **RSS comparison is biased.** Prototype records extra per-callback deadline
   lag samples, so its allocated sample buffers differ from the legacy
   benchmark. The reported `ru_maxrss_kib` is process high-water RSS, not
   isolated timer memory. Do not interpret its backend delta as allocation
   efficiency.
5. **Post-close thread snapshots were unstable.** Some runs reported a
   `threads_after` count of two immediately after join/close. This does not
   prove a leak; a subsequent benchmark-only change adds a bounded
   post-shutdown stabilization window. The archived dataset remains as run.

## Next acceptance gates

1. Repeat the **same 20 × 5 × 2** A/B under controlled multi-core affinity
   and under documented sustained CPU load. Retain full raw CSV; compare the
   distribution of per-run P99 and **worst-run** results, not just medians.
2. Create comparable end-to-end deadline-to-callback measures for the legacy
   runtime and prototype. Do not compare prototype-only `deadline_lag`
   directly to the legacy inter-arrival error.
3. Build independent stress coverage for slow callback/overrun, self-delete,
   cancellation from multiple threads, close-during-callback and FD/resource
   cleanup, including ASan/UBSan/TSan where supported.
4. Only consider production migration after lifecycle semantics and tail
   latency are demonstrated to improve under realistic multi-core workloads.

**Release decision:** performance experiment PASS; production
`timerfd/epoll/eventfd` replacement **NO-GO**. No release tag.
