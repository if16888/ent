# Monotonic clock utility task

Goal: expose a stateless portable millisecond monotonic clock.
Non-goals: timer backend, scheduling, runtime lifecycle, wall clock or suspend normalization.
Scope check: existing basic utility scope; no product or distributed semantics.
Allowed: utl_time.c, ent_utility.h, source/test CMake entries, clock tests/docs.
Forbidden: existing timer/runtime implementation, message definitions, ABI layouts, CI changes.
Risk: R3 (public API and platform branches). Authorization: implement and validate
on a topic branch under the user's instruction to improve the foundation library.

Read-only impact scan: ent_utility.h has locks/timers but no elapsed-time reader;
utl_timer.c and log internals use platform clocks privately. No existing caller
changes are required. comm/CMakeLists.txt enumerates sources; both shared/static
objects need the new source. Existing ENT_UTHD_INVALID_ARGUMENT/CLOCK_FAILED apply.
Test gap: real nondecreasing reads and injected POSIX failures/conversion bounds.

Implementation: append API, implement checked conversion, test, build/install.
Validation: git diff --check; cmake -S . -B build -DENT_BUILD_EXAMPLES=OFF;
cmake --build build -j3; ctest --test-dir build --output-on-failure.
Windows cross compilation supplements Linux tests; Windows execution remains open.
