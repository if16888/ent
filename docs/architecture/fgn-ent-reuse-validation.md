# fgn ent Reuse Validation

## 1. Goal

Validate whether `ent` can serve as a lightweight common runtime/library for
`fgn` and the protocol-analyzer tooling.

The first-pass focus is the smallest reusable surface:

- `ent_log`
- `ent_msg`
- `ent_shm`

This document defines a validation plan, not an implementation plan.

## 2. Non-goals

- Do not migrate all of `fgn`.
- Do not change `fgn` core overlay, CBFS, or metadata resolver logic.
- Do not introduce an `ent` runtime registry.
- Do not force `fgn` to depend on every `ent` module.
- Do not design or implement runtime-owned `fgn` / `ent` integration here.
- Do not expand the scope into a generic `ent` platform rewrite.

## 3. Candidate Reuse Modules

### Primary candidates

- `ent_log`
- `ent_msg`
- `ent_shm`

### Optional candidate

- `utl_tpool`

### Not now

- `ent_db`
- `UTL_Timer*`
- full `ENT_HANDLE` runtime-owner migration
- runtime resource registry

## 4. fgn Read-only Scan Scope

Scan these areas only:

- logging / trace / debug output
- shared memory / mmap / metadata cache
- error code / status code handling
- thread pool / async task execution
- benchmark / smoke test output

The scan is intentionally read-only. It should identify candidate seams and
integration friction, not modify `fgn`.

## 5. Read-only Scan Findings

| Area | fgn file / symbol | Current behavior | Candidate ent module | Reuse benefit | Integration cost | Risk | Recommendation | Priority |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Logging / trace / debug | `src/fgd/fgd_init.cpp`, `src/fgd/fgd_arc.cpp`, `src/fgd/fgd_config.cpp`, `src/fds/fds_init.cpp`, `src/fas/fas_init.cpp`, `src/test/metadata_store_test.cpp`, `src/test/metadata_store_shm_test.cpp`, `scripts/ci/ent_perf_probe.c` | Already uses `ENT_LogInit`, `ENT_LogInitHandle`, `ENT_LogSetOption`, `ENT_LogCloseHandle`, and `ENT_LogClose` in multiple places. | `ent_log` | Good fit for a first low-risk integration point and for shared diagnostics formatting. | Low to medium. The module is already present in many call sites, but ownership and lifecycle conventions still need care. | Medium | First prototype candidate for `fgn`. | High |
| Error / status normalization | `src/fds/fds_service.cpp`, `src/fds/fds_init.cpp`, `src/fgd/fgd_arc.cpp`, `src/fgd/fgd_config.cpp`, `src/fas/report.cpp`, `scripts/dev/apply_enfs_linux_nas_metadata_sync.py`, `scripts/dev/apply_enfs_linux_nas_round2.py` | `fgn` uses `MSG_ID_T` as plain integer status in many paths. The scan did not find direct `ent_msg` call sites, only local status variables and helper scripts. | `ent_msg` | Useful for protocol-analyzer error normalization and for making result taxonomy explicit. | Medium. A thin adapter/mapping layer is likely needed before callers can benefit. | Medium to high | Strongest candidate for the protocol-analyzer path, but validate with an adapter first. | High for analyzer, medium for app code |
| Shared memory / snapshot | `src/test/metadata_store_shm_test.cpp`, `docs/native-perf-benchmark.md`, `docs/windows-overlay-enum-mode.md`, `docs/windows-fgd-benchmark.md`, `scripts/ci/poc_shm_benchmark_compare.py` | `fgn` already has a file-backed SHM snapshot path. The test writes snapshots with `shm_open`, `mmap`, `msync`, and `shm_unlink`. | `ent_shm` | A wrapper could reduce platform-specific mapping boilerplate if the snapshot code becomes shared. | Medium to high. `fgn` already has bespoke snapshot code and tests, so the fit is not as direct as logging. | Medium | Secondary candidate. Validate after the logging / message path stays clean. | Medium |
| Thread pool / async tasks | `scripts/ci/ent_perf_probe.c`, `src/test/example_thread.cpp`, benchmark and automation scripts | `fgn` already uses `UTL_TPoolInit` / `UTL_TPoolAddTask` / `UTL_TPoolClose` for batch work and probes. | `utl_tpool` | Good for batch parsing or analyzer fan-out. | Low to medium. The abstraction is already close to the use case. | Low to medium | Optional follow-on module after log / message prove out. | Optional |
| Benchmark / smoke output | `src/test/performance_analysis.py`, `src/test/security_analysis.py`, `scripts/ci/poc_shm_benchmark_compare.py`, `docs/native-perf-benchmark.md` | Analysis tools emit plain metrics, text summaries, and return codes. There is no unified reusable diagnostic layer today. | `ent_msg` + `ent_log` | Could standardize analyzer output and make failures easier to classify. | Low to medium for formatting, higher if a full structured result model is attempted. | Low to medium | Useful for the protocol-analyzer path, but not the first code prototype. | Medium |

## 6. Minimal Validation Path

### Phase A: Read-only analysis

- Locate `fgn` logging, shared metadata, error code, thread pool, and benchmark
  output code paths.
- Map each candidate `ent` module to the corresponding `fgn` integration point.
- Identify low-risk replacement seams.
- Output impact notes only; do not change code.

### Phase B: Smallest possible prototype

- Pick only one module for the first prototype.
- Prefer `ent_log` for `fgn` runtime logging.
- Prefer `ent_msg` for protocol-analyzer result normalization.
- Keep the prototype easy to roll back.
- Avoid changing `fgn` main business behavior.

### Phase C: Build validation

- Validate on Windows native build.
- Validate on Linux if the environment is available.
- Confirm that `fgn` smoke and benchmark paths do not regress.
- If the build becomes materially more complex, stop.

## 7. Acceptance Criteria

- `fgn` can link against at least one `ent` module.
- The integration does not materially increase build complexity.
- Existing `fgn` smoke paths continue to pass.
- `ent` APIs are clear enough for `fgn` users.
- If integration is awkward, the issue should be recorded as an `ent` API
  design problem.

## 8. Stop Conditions

- Stop if adopting `ent` requires a major `fgn` architecture rewrite.
- Stop if `ent` dependencies significantly complicate `fgn` builds.
- Stop if runtime registry work is required before reuse is viable.
- Stop if even the `ent_log` or `ent_msg` single-module path is awkward.
- Stop if `ent_shm` cannot be used without a new lifecycle framework.

## 9. Protocol Analyzer Reuse Outlook

The protocol analyzer is the second consumer that should benefit from the same
ent baseline.

- `ent_msg`: normalize protocol parsing errors and analyzer result codes.
- `ent_log`: capture and format analyzer traces and diagnostics.
- `ent_db`: possible later persistence layer for analysis history.
- `utl_tpool`: batch parse and fan-out tasks.
- `ent_shm`: only if a shared cache or high-throughput result store becomes
  necessary.

This is a direction check only. It is not an implementation design.

## 10. Decision Checkpoint

After the minimal `fgn` reuse validation, decide:

- Does `ent` already work as a lightweight shared library for `fgn`?
- Which module should get the first prototype: `ent_log` or `ent_msg`?
- Is `ent_shm` still only a secondary candidate?
- Do we need to pause deeper `ent` investment?
- Would a registry be over-engineering for the observed use cases?

If the answer to the first two questions is yes, keep `ent` at contract-freeze
depth and continue only with the smallest reuse path.

## 11. Rollout Notes

- Keep the validation order small and reversible.
- Prefer one module, one prototype, one rollback path.
- Record any API friction directly so future `ent` changes can stay compatible.
- If the first useful reuse is only `ent_log` or `ent_msg`, do not expand the
  scope into runtime-owner migration.

## 12. Follow-up Artifacts

- `ENT-027` produced [`docs/architecture/fgn-ent-log-prototype-plan.md`](./fgn-ent-log-prototype-plan.md).
- `ent_log` remains the first `fgn` prototype candidate.
- `ent_msg` remains the protocol-analyzer-first candidate.
- `ent_shm` remains the secondary candidate.
