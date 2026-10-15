# Unreleased core fixes

- Concurrent `ENT_LogCtxInitHandle` calls reserve the empty context before
  allocating a handle. Only one initializer succeeds; failures release their
  reservation for retry. Explicit logger output slots are cleared on failure;
  close a live handle before reusing its variable.
- `ENT_LogCtxClose` completes context destruction after a completed logger I/O
  failure and still returns `ENT_LOG_IO_FAILED`. Do not retry that old context
  pointer. Incomplete background-worker teardown retains a retryable context.
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
