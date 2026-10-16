# Follow-up review: driver row counts, runtime RT updates and log migration

Base: master f25fc2401f3cb4c1ca42738680d60d54ae58e257. User request:
implement the three supplied review items. Risk R3; all changes are public core.

Allowed: MySQL result count validation and failure injection; serialization of
runtime RT attribute updates; caller-thread scheduling documentation; logger
close migration example and retry/terminal-error regressions. Excluded: public
API/signature, message-code or ABI changes; registry redesign; timer backends;
private components; changing or weakening CI. Preserve the existing close
admission contract and PR #39 behaviour.

Read-only impact scan: MySQL matrix allocation uses the driver's advertised
row count; fetch indexing and completion need matching checks. Runtime admission
pins lifetime but releases entLock before RT writes/native scheduling calls;
reuse that per-instance mutex to serialize complete setting attempts. Native
scheduler and affinity operations target pthread_self, not a runtime worker.
Log context close already distinguishes completed I/O failure from incomplete
worker teardown; add an example owner helper which clears terminal handles and
preserves retryable ones, and exercise that actual helper in the log tests.

Acceptance from the review: normal/excess/missing MySQL fetch counts, including
empty edges, ASan; two concurrent RT setters repeated 50 rounds, TSan; terminal
logger I/O and retryable teardown failures; complete build/CTest and subsequent
platform CI. Local platform or service limitations must remain explicit.

Rollback: revert the focused follow-up change. No release publication.


## Local evidence

The same new tests were run against the unchanged f25fc24 implementations in a
separate /tmp archive (not a Git worktree), then against the patched sources:

- Baseline runtime + new stress test: TSan exit 66, concurrent writes in
  iENT_CTXApplyRtAttributes at ent_init.c:720. This is a reproduced runtime race.
- Baseline MySQL collector + injected extra row: ASan exit 1, 8-byte write past
  the row pointer matrix at ent_db_mysql.c:518. This reproduces the abnormal-driver
  fault path, not a claim of real MySQL server/driver count inconsistency.
- Patched Release build/full CTest: 36/36 passed, including the compiled migration
  example. MySQL cases cover normal/multiple/empty, extra/missing/zero-count
  mismatches, truncation, field length, cleanup after an allocated row and reuse.
- Patched ASan with leak detection: 3/3 passed (MySQL results, log close migration,
  runtime concurrency). Patched TSan: 2/2 passed (runtime and logging). The runtime
  test performs 50 rounds, two caller threads each applying settings 100 times.
- git diff --check passed. Public signatures/data layouts and message codes are
  unchanged. Windows/ARM/live database validation remains a remote CI gate.

Commands: cmake --build /tmp/ent-review-fixes-build -j4; ctest --test-dir that
build --output-on-failure --timeout 60. Corresponding focused Debug ASan and TSan
builds are /tmp/ent-review-fixes-asan and /tmp/ent-review-fixes-tsan. Logs use
/tmp/ent-round2-*; negative-control logs are /tmp/ent-round2-baseline-{asan,tsan}.log.

Delivery follows the user's standing push / merge-after-green workflow, with
PR CI and then the merged master CI checked independently. Do not merge a failing
or pending revision; do not modify CI gates to make the change pass.


## Subsequent review: interpretation and evidence limits

Review baseline: PR #40 merged as 8b5f8b8. This follow-up changes documentation
and an example comment only (low risk, public core scope). It does not change
runtime behaviour, signatures, ABI, error codes or CI. Acceptance: diff whitespace
check, local Markdown link targets, and confirmation that no executable source
statements changed. Business builds/sanitizers need not be repeated for prose.
Rollback: revert this documentation-only change.

The count guards, per-instance RT serialization and owner-side close pattern are
supported by the implementation and focused tests. Interpret them narrowly:

- The MySQL prepared-result path calls mysql_stmt_store_result before collecting
  rows. Count mismatch injection validates bounds and rejection/cleanup for this
  buffered path; it does not demonstrate a malicious-server or MITM exploit,
  streaming/cursor compatibility, or the absence of all backend memory bugs.
- Linux uses pthread_setaffinity_np and pthread_setschedparam on pthread_self.
  The private fields are a serialized last-attempt snapshot, not a public read-only
  query API or proof of every worker's current OS configuration. OTHER-policy
  concurrency tests do not establish FIFO/RR privileges, hard-real-time jitter,
  or optimal CPU selection on heterogeneous ARM systems.
- Runtime RT-setting failure paths can log while holding entLock. Therefore,
  holding only one runtime instance lock does not prove an absence of nested
  locks or ABBA deadlocks; logging locks must be included in any lock-order audit.
  No new deadlock is demonstrated by this observation.
- ASan/TSan and green CI are evidence for the executed configurations and paths,
  not a proof that all overflows, races or stale-alias accesses are impossible.
- Completed logger I/O failure means reclamation has finished, but data loss is
  possible rather than established for every such error. Retryable teardown and
  terminal I/O errors require different owner actions. Generalize the ownership
  procedure as documentation, not a shared status-code rule or new public API.

Preserve existing CI gates. Future executable lifecycle changes still require
failure-path regression coverage and the applicable platform/sanitizer checks;
this review provides no reason to weaken them or add duplicate CI jobs.

## RT failure logging lock follow-up (2026-10-16)

A lock-order walk found no logger-to-runtime `entLock` acquisition path, so the
previous observation did not establish an ABBA deadlock. The RT attribute error
path nevertheless performed logger I/O while holding the per-instance `entLock`,
which extended lock hold time and serialized unrelated state access behind I/O.

The RT attempt still updates native state and its snapshot under `entLock`, but
now copies the native error to a local value and emits diagnostics after releasing
the lock. The active-call pin remains held through logging, so `ENT_Close` still
waits for this admitted call before reclaiming the runtime context. A Linux test
injects `EPERM` from the scheduler call and checks the warning occurs with the
runtime lock available, while preserving the return status and error snapshot.

Validation: Release build and CTest passed 36/36; focused `test_ent_init` passed
under ASan; `test_ent_init_concurrency` passed under TSan. The lock probe verifies
the exercised scheduler-failure logging path only and is not a proof of global
lock-order safety.
