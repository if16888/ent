# fgn ent_log Minimal Prototype Plan

## 1. Goal

Design the smallest possible `fgn` reuse of `ent_log`.

The objective is to determine whether `ent_log` can reduce `fgn` logging
maintenance cost with a low-risk, reversible, small-scope prototype.

This is a pre-prototype plan, not an implementation proposal.

## 2. Non-goals

- Do not change `ent_log` API.
- Do not change `ent` runtime lifecycle.
- Do not introduce a runtime registry.
- Do not migrate `ent_msg` or `ent_shm`.
- Do not change `fgn` core business behavior.
- Do not replace all `fgn` logging call sites at once.
- Do not introduce a new logging framework.

## 3. Current fgn ent_log Usage

### Scan legend

- `A`: already using `ent_log` cleanly.
- `B`: using `ent_log`, but lifecycle ownership is still somewhat unclear.
- `C`: could benefit from a very small `fgn`-local wrapper.
- `D`: not suitable for the first prototype.

| Area | File | Function / Symbol | Current API | Lifecycle owner | Issue / Friction | Prototype suitability |
| --- | --- | --- | --- | --- | --- | --- |
| Production service init / close | `src/fgd/fgd_init.cpp` | `FGD_EnvInit`, `FGD_Init`, `FGD_Close` | `ENT_LogInit`, `ENT_LogInitHandle(NULL, "fgd", gFgdCfg.logPath)`, `ENT_LogSetOption(NULL, ...)`, `ENT_LogCloseHandle(NULL)`, `ENT_LogClose()` | Mixed global service + component startup/shutdown | Repeated init / close sequencing is spread across service code; ownership is understandable but not centralized. | `B`, `C` |
| Dump / report path | `src/fgd/fgd_arc.cpp` | `FGD_ArcPrint`, `FGD_ArcClose` | `ENT_LogInitHandle(&dumpLog, "dump", cfg->logPath)`, `ENT_LogRaw(dumpLog, ...)`, `ENT_LogCloseHandle(dumpLog)` | Local handle-owned logger | Clear, isolated usage. This is already close to the desired pattern. | `A` |
| Config / option setup | `src/fgd/fgd_config.cpp` | `FGD_ConfigInit` helpers | `ENT_LogSetOption(NULL, ENT_LOG_LEVEL_E, ...)`, `ENT_LogSetOption(NULL, ENT_LOG_PATH_E, ...)` | Global service | Option writes are simple, but the caller assumes the global service is alive. | `B` |
| FDS service init / close | `src/fds/fds_init.cpp` | `FDS_SrvInit`, `FDS_SrvClose` | `ENT_LogInit`, `ENT_LogInitHandle(NULL, "fds", "log")`, `ENT_LogClose()` | Global service + default handle | Repeated setup/teardown with implicit ownership. | `B`, `C` |
| FAS service init / close | `src/fas/fas_init.cpp` | `FAS_Init`, `FAS_Close` | `ENT_LogInit`, `ENT_LogInitHandle(NULL, "fas", "log")`, `ENT_LogSetOption(NULL, ...)`, `ENT_LogClose()` | Global service + default handle | Similar to FDS: repeated global init plus service-local usage. | `B`, `C` |
| Performance probe | `scripts/ci/ent_perf_probe.c` | `run_log_benchmark` | `ENT_LogInit`, `ENT_LogInitHandle`, `ENT_LogSetOption`, `ENT_LogPrint`, `ENT_LogCloseHandle`, `ENT_LogClose()` | Probe-local | Good diagnostic harness, but not a realistic production prototype target. | `D` |
| Metadata tests | `src/test/metadata_store_test.cpp`, `src/test/metadata_resolver_test.cpp`, `src/test/metadata_store_shm_test.cpp` | test setup / teardown | `ENT_LogInit`, `ENT_LogClose()` | Test-local | Useful as smoke coverage, but not a production reuse prototype. | `D` |
| Example programs | `src/test/example.cpp`, `src/test/example_comm.cpp`, `src/test/example_thread.cpp` | example harness init / close | `ENT_LogInit`, `ENT_LogInitHandle`, `ENT_LogClose()` | Example-local | Demonstrates current usage but is not a good first reuse target. | `D` |

## 4. Candidate Prototype Options

### Option A: Documentation-only cleanup

- Add or refine `fgn` logging usage guidelines.
- Do not change code.
- Lowest risk.
- Useful if the scan shows the current usage is good enough and the only problem is inconsistent documentation.

### Option B: `fgn`-local thin wrapper

- Add a tiny `fgn` wrapper such as `fgn_log_init`, `fgn_log_close`, or `fgn_log_set_level`.
- Only wrap existing `ent_log` calls.
- Keep the wrapper local to `fgn`.
- No `ent` changes.
- Reversible with a small diff.

### Option C: Single-component pilot

- Pick one component only, preferably `fgd` or `fds`.
- Normalize its log init / close path.
- Do not touch other components.
- Verify that the build and smoke path stay stable.

## 5. Recommended Option

Recommended starting point: **Option B**.

Reasoning:

- The scan shows repeated `ent_log` setup and shutdown patterns across `fgd`,
  `fds`, and `fas`.
- A thin wrapper can reduce repetition without changing `ent`.
- The wrapper can stay local and reversible.
- This is a better first move than a broad logging refactor.

If the build or review cost grows, fall back to Option A.

## 6. Prototype Boundary

- The first prototype must stay within a single `fgn` component or wrapper
  boundary.
- Do not change log format.
- Do not change log-level semantics.
- Do not change log file path rules except for documentation cleanup.
- Do not change close order unless a component-local wrapper can prove the
  current order clearer.
- Do not touch benchmark semantics.

## 7. Build Impact

- `fgn` already links `ent`, and `ent_log` is already used in multiple source
  trees.
- A thin wrapper should not require a new include path or new library
  dependency.
- Windows native builds should remain unchanged if the wrapper stays local.
- Linux builds should remain unchanged for the same reason.
- CI impact should be minimal if no build-script changes are introduced.

## 8. Test Plan

If the plan moves into an implementation prototype, run:

- Windows native build
- `fgn` smoke tests
- current metadata / overlay smoke
- a minimal benchmark or probe path if applicable
- log file generation checks
- rollback build check

## 9. Acceptance Criteria

- Only one small scope is changed.
- Build complexity does not materially increase.
- Log behavior does not regress.
- Smoke behavior does not regress.
- Rollback remains simple.
- No runtime registry is needed.
- No `ent` public API changes are needed.

## 10. Stop Conditions

Stop if any of the following appears:

- `ent` public API changes are required.
- runtime registry becomes necessary.
- `fgn` logging architecture needs a broad rewrite.
- more than one component must be reworked to see value.
- build complexity rises noticeably.
- the prototype only improves cosmetic consistency without reducing real
  duplication or debugging cost.

## 11. Recommended Next PR

If validation stays positive, the next task should be:

- `FGN-ENT-001`: `fgn ent_log minimal prototype`

That next task must:

- modify only `fgn`
- touch only one component first
- remain rollback-friendly
- avoid any `ent` code changes
