# Monotonic elapsed-time clock

`UTL_MonotonicMs(ENT_U64 *value)` reads milliseconds from an unspecified
monotonic epoch. It needs no runtime initialization or cleanup. Successful reads
are nondecreasing; consecutive values may be equal. Use differences for local
elapsed-time budgets, never persisted timestamps or cross-process identities.
This is not a real-time scheduling guarantee.

Windows uses GetTickCount64 (including suspended time); POSIX uses
CLOCK_MONOTONIC (Linux excludes suspended time). Suspend behavior is therefore
platform dependent. Millisecond conversion truncates submillisecond values.

NULL returns ENT_UTHD_INVALID_ARGUMENT. A failed or unrepresentable POSIX clock
reading returns ENT_UTHD_CLOCK_FAILED. Failure leaves the output unchanged.
The new function adds an export without changing existing signatures or layouts.
