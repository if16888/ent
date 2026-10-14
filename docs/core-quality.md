# Public core contracts and quality gates

`ent` remains a 0.x preview. A passing build does not certify hard real-time
behavior or every possible concurrent interleaving.

## Resource and ownership contracts

- Parameterized database calls reject more than 4,096 parameters, a text/blob
  field larger than 16 MiB, or aggregate input payload larger than 64 MiB with
  `ENT_DBS_BAD_PARAMS`, before opening or executing a backend query. Numeric
  payload sizes count toward the aggregate; SQL text and backend engine memory
  are outside this budget. Backend limits can be lower. Use `NULL_E` for SQL
  NULL; a NULL `TEXT_E` pointer is invalid on all backends.
- Materialized database results have a 1,048,576-cell, 64 MiB field-payload
  limit, including terminators, and a 16 MiB per-field limit. Budget rejection
  returns `ENT_DBS_ALLOC_FAILED` without invoking the result callback. These
  budgets do not constrain memory already allocated inside SQLite/libpq/MySQL.
  SQLite legacy `ENT_DbRead` uses row callbacks rather than full materialization;
  use `ENT_DbReadParams` with zero parameters when the materialization contract
  is required. Callbacks borrow result storage and must not reenter/close the
  same DB handle. Non-NULL callbacks require non-NULL user data.
- DB password copies held by ent are zeroed before freeing on reconfigure and close; this does not scrub independent database client or allocator copies.
- `ENT_DbInitHandle(&handle, ...)` treats `handle` as output-only and clears the slot without reading its previous contents, so an uninitialized local variable is valid. Close any live handle before reusing its variable; initialization does not close or preserve a previous handle.
- DB and runtime close wait for admitted operations. The caller must fence new
  entries against close and stop using copied raw handles after close. Logging
  uses its own registry protection; that guarantee must not be generalized to
  every handle type.
- Async logs default to an 8 MiB budget; `ENT_LOG_QUEUE_FULL` rejects overload.
  Thread pools reject a full pending queue with `ENT_TPL_QUEUE_FULL`. Thread
  pool close discards pending work; callers requiring completion for every
  submission must drain before close.
- Linux microsecond timers coalesce expired periodic callbacks. This prevents
  unbounded catch-up work; it does not guarantee execution deadlines. Jitter
  thresholds belong to a measured machine/load profile.
- Shared mappings accept regular files, reject final-component symlinks/reparse
  points, create POSIX files with owner-only permissions, and validate native
  size before truncation. Parent directories, existing-file permissions and
  cross-process synchronization are the caller's responsibility. Concurrent
  external truncation of a live mapping is unsupported. Memory locking is
  best-effort. No distributed or multi-writer protocol is provided.

Library DB budgets are compile-time defaults in the public header/internal
implementation. Defining a macro only in a consumer does not reconfigure an
already-built library. No public function signature or type layout changes in
this task. The new shared-map regression checks rejected operations preserve
file sizes, in addition to existing close/flush tests.

## Reproducible checks

```bash
cmake -S . -B build -DENT_ENABLE_WARNINGS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 scripts/check_public_scope.py
bash scripts/run-quality-linux.sh warnings origin/master
bash scripts/run-quality-linux.sh analyze origin/master
bash scripts/run-quality-linux.sh coverage
bash scripts/run-quality-linux.sh abi origin/master
bash scripts/run-quality-linux.sh sdk
python3 scripts/fuzz-msg-parser.py --iterations 10000 --output parser-fuzz.json
```

Quality checks require GCC, Python, CMake, SQLite development files; coverage
also requires gcovr, and ABI comparison requires libabigail's `abidiff`. They
store evidence in `build-quality` (`QUALITY_DIR` can move it outside the
checkout). Run diagnostics and ABI against a real base commit using the same
compiler/profile. Baseline extraction uses `git archive`, never changes the
user's checkout, and clean rebuilds preserve warning evidence on repeated runs.
GCC analysis findings require triage; comparison rejects new diagnostics without
pretending old baseline findings are confirmed vulnerabilities or resolved.

`ENT_ENABLE_WARNINGS` applies `-Wall -Wextra` on GCC/Clang or `/W4` on MSVC to
core objects. `ENT_ENABLE_ANALYZER` is GCC-only. `ENT_ENABLE_COVERAGE` is a
non-Windows GCC test-build option, never a release-package setting. Coverage
reports are a baseline, with no arbitrary percentage gate.

The CI also compares GCC/Clang warnings and MSVC `/W4` diagnostics, and runs
Linux SQLite SDK and Windows no-DB archive consumers.

ABI checking uses identical no-DB debug builds and public headers. It checks
binary signatures/layouts and symbols, not behavioral compatibility or all
feature combinations. Parser fuzzing is a bounded seeded mutation campaign with
accepted and rejected inputs; it is not coverage-guided native fuzzing. Unexpected
failures preserve a reproducer. CTest runs a short campaign; CI runs 10,000 cases.

SDK acceptance must build and run shared/static consumers from a real extracted
archive containing runtime plus development components; metadata-only checks
are insufficient. Linux and Windows validators reject missing CMake metadata.
Windows validation requires a shell-local VS environment. See the existing
release workflow for platform packaging and third-party runtime exclusions.

`test_ent_db_limits_live` checks exact-limit, over-limit and post-rejection
queries against disposable MySQL/PostgreSQL services. It reports CTest skip
code 77 when fixtures are absent. The integration CI sets
`ENT_DB_REQUIRE_FIXTURES=1`, so missing fixtures fail rather than silently skip.
