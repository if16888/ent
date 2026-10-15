# Review fixes: lifecycle admission and result completeness

Goal: fix the confirmed findings in the review of 5060c27..9c860f1 on top of
origin/master 7f63768. The user's instruction authorizes implementation and
local verification. Risk R3. Preserve the pre-existing branch and all tests.

Scope: runtime status/admitted-call semantics, log context initialization and
close disposition, timer close-in-progress status, socket EINTR capacity,
MySQL prepared result completeness, affected public contracts and regressions.
Excluded: handle registry redesign, implicit thread/runtime log binding,
UTF-8 path migration, timer backend migration, CI workflow changes and releases.
All changes are within public ent core scope.

Impact scan: runtime calls admit under entLock; duplicate unlocked stop reads
race with Stop. Log context entry pins lifetime but previously did not reserve
initialization. Log handle close already reports completed disposition separately
from I/O errors; context close must use it. Timer service close must report busy
while another close is active. MySQL collection must reject truncated fetches
before callback/copy and copy only validated byte counts. Socket accept must
restore its value-result address capacity for every EINTR retry.

Acceptance: git diff --check; Release CMake build with BUILD_TESTING=ON;
full CTest; focused ASan lifecycle/result regressions. Existing default build
commands in AGENTS.md apply; record unavailable platforms/services separately.

Compatibility:

- ENT_TMR_IN_USE reports concurrent close instead of premature success. Wait for
  the first close to finish before retrying; this status does not mean completion.
- Duplicate ENT_Run returns ENT_SYS_ALREADY_INITIALIZED, not BAD_HANDLE.
- Runtime handle allocation returns ENT_INIT_HANDLE_ALLOCFAIL.
- ENT_LogCtxClose releases its context after completed I/O cleanup even when it
  returns ENT_LOG_IO_FAILED. Incomplete worker teardown remains retryable.
- Failed explicit logger initialization clears its output; do not reuse a live
  handle variable without closing it first.
- MySQL prepared reads return ENT_DBS_RESULT_FAILED without invoking callbacks
  on truncated results or lengths that do not fit the bound column buffer.
- ENT_DbInitHandle on this mainline requires a NULL-initialized slot and rejects
  non-NULL slots without replacing/closing them. Consumers of the historical
  auto-replace behavior must explicitly close first, then initialize again.
  The prior local output-only DB change is preserved on its original branch.

Validation and limitations are recorded in the implementation response. Rollback
is restoration of this focused diff; the user subsequently authorized committing and pushing this branch; merge and release remain outside this task.

## Executed validation

- Release configure/build: passed with BUILD_TESTING=ON, SQLite and MariaDB
  enabled. CMake was installed under /tmp/ent-cmake; matching MariaDB development
  headers were extracted under /tmp/ent-dev-sdk because system package installation
  requires root. SQLite used the existing header SDK and installed runtime.
- Full CTest: 35/35 passed, including the new MySQL result regression. This
  test injects client fetch outcomes; it does not claim a live-server result.
- ASan with leak detection: 5/5 passed (runtime, logging, socket, timer thread
  safety and MySQL result failure cleanup).
- TSan: 3/3 passed (runtime concurrency, logging, timer thread safety).
- git diff --check: passed.
- Unverified: Windows DLL exports/native tests, ARM runner, PostgreSQL and live
  MySQL server integration. PostgreSQL was disabled for this local build.
- Local validation does not establish remote CI success. Check the pushed commit workflows separately.

Commands used: CMake Release configure/build and ctest --output-on-failure
--timeout 60; separate Debug ASan and TSan builds with their focused CTest
selections. Build and test logs are /tmp/ent-review-fixes-{build,ctest}.log,
/tmp/ent-review-fixes-{asan,tsan}-{build,ctest}.log.

Files changed: comm/ent_init.c, ent_log_core.c, ient_log.h, ent_db_mysql.c,
utl_timer.c, utl_socket.c, utl_tpool.c; inc/ent_init.h, ent_log.h, ent_db.h,
ent_utility.h; msg/ent.msg and AGENTS.md; test/CMakeLists.txt, test_ent_init.c,
test_ent_log.c, test_utl_socket.c, test_utl_timer_thread_safety.c and the new
test_ent_db_mysql_results.c; docs/log-return-codes.md, docs/release-notes.md and
this task record.
