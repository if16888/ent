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
