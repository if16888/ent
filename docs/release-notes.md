# Unreleased core fixes

- Concurrent `ENT_LogCtxInitHandle` calls reserve the empty context before
  allocating a handle. Only one initializer succeeds; failures release their
  reservation for retry. Explicit logger output slots are cleared on failure;
  close a live handle before reusing its variable.
- `ENT_LogCtxClose` completes context destruction after a completed logger I/O
  failure and still returns `ENT_LOG_IO_FAILED`. Do not retry that old context
  pointer. Incomplete background-worker teardown retains a retryable context.
- Existing exported message arrays retain their original 131 entries, count and
  order for binary compatibility. New message codes are resolved through a
  generated library-internal lookup used by `ENT_MsgText`; use that API rather
  than treating the compatibility array as a complete catalog of newer codes.
- A concurrent `UTL_TimerClose` returns the new `ENT_TMR_IN_USE` error while
  another close is active. Wait for that close to finish before retrying.
- A duplicate `ENT_Run` on a running instance returns
  `ENT_SYS_ALREADY_INITIALIZED`. Runtime handle allocation failure returns the
  new `ENT_INIT_HANDLE_ALLOCFAIL` error. Existing message-code values are retained.
- Stop and realtime attribute entry rely on the locked admission check instead
  of reading the stop flag again without synchronization. Calls admitted before
  stop may finish; owners must fence new calls against close.
- MySQL prepared results that are truncated or exceed their bound column buffer
  return `ENT_DBS_RESULT_FAILED` without delivering partial results. Successful
  field copies use validated lengths and append their own terminator.
- Socket accept restores the peer-address buffer capacity before each EINTR
  retry. `UTL_Bind` definitions explicitly carry the public export annotation.
- Condition-variable documentation now specifies Windows-only RW-lock waits;
  POSIX RW-lock waits return `ENT_UTHD_UNSUPPORTED_LOCK`.

## DB handle migration from historical auto-replacement

`ENT_DbInitHandle` requires a caller-initialized NULL slot. It rejects a non-NULL
slot with `ENT_DBS_IN_USE` without dereferencing, replacing or closing it. Code
that relied on initialization implicitly replacing a live handle must first call
`ENT_DbCloseHandle`, verify successful closure, then initialize a new handle.
Do not pass an uninitialized local handle as the output slot.

## Follow-up result and concurrency checks

- MySQL prepared-result collection rejects both extra and missing fetched rows
  relative to the driver's buffered count with `ENT_DBS_RESULT_FAILED`; no partial
  result callback is invoked. Empty-result mismatches follow the same rule.
- Concurrent `ENT_SetRtAttributes` calls on one runtime serialize the native
  updates and last-attempt snapshot. On Linux, scheduling/affinity still target
  the calling thread. They do not configure all workers in the runtime.
- Logger-close migration now has a compiled owner helper and an executable
  example: clear the context slot after success or completed I/O error; preserve
  it after incomplete worker-stop failure. No public API/ABI or message code is
  added by this follow-up.
