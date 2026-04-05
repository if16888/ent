# Linux RT Timer Jitter Evaluation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新增一个面向 Linux 实时核用户态的 `utl_timer` 周期精度与抖动评估程序，输出对 `100 us` 抖动目标有判断价值的统计结果。

**Architecture:** 保持 `utl_timer` 生产实现不变，只在 `test/` 目录新增一个独立 `perf_utl_timer_rt` 程序和对应的 CMake 构建入口。程序优先沿用当前 `UTL_TimerCreate`/`UTL_TimerDelete`/`UTL_TimerClose` 路径做观测，在回调中采集时间戳，并补充 Linux RT 运行环境信息、统计计算和超阈值计数输出。

**Tech Stack:** C, CMake, pthread, Linux scheduling APIs, existing ent timer APIs

---

### Task 1: Add Build Target For Linux RT Timer Evaluation

**Files:**
- Modify: `test/CMakeLists.txt`
- Create: `test/perf_utl_timer_rt.c`

- [ ] **Step 1: Write the failing program skeleton**

Create `test/perf_utl_timer_rt.c`:

```c
#include <stdlib.h>

int main(void)
{
    return EXIT_FAILURE;
}
```

Add a non-ctest target to `test/CMakeLists.txt`:

```cmake
add_executable(
  perf_utl_timer_rt
  perf_utl_timer_rt.c
  ../comm/utl_timer.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
)

target_include_directories(
  perf_utl_timer_rt
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(perf_utl_timer_rt PRIVATE pthread)
endif()
```

- [ ] **Step 2: Reconfigure and build to verify the target exists**

Run: `cmake -S /Users/lifei/test/ent -B /Users/lifei/test/ent/build`

Then run: `cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt`

Expected: build succeeds and creates `build/bin/perf_utl_timer_rt`.

- [ ] **Step 3: Run the program to verify the red step**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt`

Expected: exits non-zero because the skeleton intentionally fails.

- [ ] **Step 4: Commit**

```bash
git add test/CMakeLists.txt test/perf_utl_timer_rt.c
git commit -m "build: add linux rt timer perf target"
```

### Task 2: Emit Linux RT Environment Metadata

**Files:**
- Modify: `test/perf_utl_timer_rt.c`

- [ ] **Step 1: Write the failing environment-output expectation**

Replace the skeleton with a minimal program that prints nothing and returns failure after checking for Linux:

```c
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
#ifndef __linux__
    fprintf(stderr, "linux-only\n");
    return EXIT_FAILURE;
#else
    return EXIT_FAILURE;
#endif
}
```

- [ ] **Step 2: Build and run to verify it still fails**

Run: `cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt`

Then run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt`

Expected: still exits non-zero, confirming the program remains in red state.

- [ ] **Step 3: Implement environment reporting and argument defaults**

Replace the file body with a Linux-only scaffold that:

- defines defaults:
  - `target_period_us = 1000`
  - `sample_count = 100000`
  - `cpu = 0`
  - `sched_policy = SCHED_FIFO`
  - `sched_priority = 80`
- supports optional CLI overrides for `period_us`, `samples`, `cpu`, `priority`
- prints:
  - `target_period_us`
  - `samples`
  - `requested_cpu`
  - `requested_policy`
  - `requested_priority`

Use this structure:

```c
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int_arg(const char* arg, int default_value)
{
    if(arg == NULL)
    {
        return default_value;
    }
    return atoi(arg);
}

int main(int argc, char** argv)
{
#ifndef __linux__
    fprintf(stderr, "perf_utl_timer_rt requires Linux\n");
    return EXIT_FAILURE;
#else
    int target_period_us = argc > 1 ? parse_int_arg(argv[1], 1000) : 1000;
    int sample_count = argc > 2 ? parse_int_arg(argv[2], 100000) : 100000;
    int cpu = argc > 3 ? parse_int_arg(argv[3], 0) : 0;
    int priority = argc > 4 ? parse_int_arg(argv[4], 80) : 80;

    printf("env target_period_us=%d samples=%d requested_cpu=%d requested_policy=SCHED_FIFO requested_priority=%d\n",
           target_period_us,
           sample_count,
           cpu,
           priority);

    return EXIT_SUCCESS;
#endif
}
```

- [ ] **Step 4: Run to verify the environment line prints**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 1000 0 80`

Expected: exits 0 and prints the `env` line with the passed values.

- [ ] **Step 5: Commit**

```bash
git add test/perf_utl_timer_rt.c
git commit -m "perf: add linux rt timer env reporting"
```

### Task 3: Add Scheduling And CPU Affinity Reporting

**Files:**
- Modify: `test/perf_utl_timer_rt.c`

- [ ] **Step 1: Write the failing expectation for runtime policy reporting**

Temporarily add an assertion path that returns failure if current policy data is not printed:

```c
fprintf(stderr, "missing runtime policy report\n");
return EXIT_FAILURE;
```

- [ ] **Step 2: Run to verify the red step**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 1000 0 80`

Expected: exits non-zero because runtime scheduling report is not implemented yet.

- [ ] **Step 3: Implement scheduling and affinity setup with fallback reporting**

Add Linux code that:

- attempts `pthread_setaffinity_np` for the requested CPU
- attempts `pthread_setschedparam` with `SCHED_FIFO` and requested priority
- reads back the actual thread scheduling policy using `pthread_getschedparam`
- reads back affinity status
- prints one line summarizing:
  - `affinity_set=0/1`
  - `sched_set=0/1`
  - `actual_policy`
  - `actual_priority`

Use this code shape:

```c
#include <pthread.h>

static const char* policy_name(int policy)
{
    switch(policy)
    {
        case SCHED_FIFO: return "SCHED_FIFO";
        case SCHED_RR: return "SCHED_RR";
        case SCHED_OTHER: return "SCHED_OTHER";
        default: return "UNKNOWN";
    }
}
```

And print:

```c
printf("runtime affinity_set=%d sched_set=%d actual_policy=%s actual_priority=%d\n",
       affinity_ok,
       sched_ok,
       policy_name(actual_policy),
       actual_priority);
```

- [ ] **Step 4: Run to verify the runtime line prints**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 1000 0 80`

Expected: exits 0 and prints both `env` and `runtime` lines even if scheduling setup is not permitted.

- [ ] **Step 5: Commit**

```bash
git add test/perf_utl_timer_rt.c
git commit -m "perf: add linux rt scheduling metadata"
```

### Task 4: Add Timestamp Sampling Through `utl_timer`

**Files:**
- Modify: `test/perf_utl_timer_rt.c`

- [ ] **Step 1: Write the failing sampling skeleton**

Add placeholders for the probe and callback:

```c
typedef struct
{
    long long* stamps_ns;
    int sample_count;
    volatile int hits;
} PERF_RT_PROBE;

static void* perf_rt_timer_cb(void* data)
{
    (void)data;
    return NULL;
}
```

Return failure after printing env/runtime:

```c
fprintf(stderr, "sampling not implemented\n");
return EXIT_FAILURE;
```

- [ ] **Step 2: Run to verify the red step**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 1000 0 80`

Expected: exits non-zero because sampling is intentionally not implemented yet.

- [ ] **Step 3: Implement timestamp capture through `UTL_TimerCreate`**

Add the following pieces:

- local no-op log stubs matching other perf programs
- `ENT_CTX gEntCtx;`
- `clock_gettime(CLOCK_MONOTONIC, ...)` helper returning nanoseconds
- probe allocation for `sample_count`
- callback that records up to `sample_count` timestamps
- `UTL_TimerInit()`
- `UTL_TimerCreate(&timer, UTL_TIMER_E_PERIOD, target_period_us / 1000, perf_rt_timer_cb, &probe)`
- polling loop waiting until `probe.hits >= sample_count`
- `UTL_TimerDelete(&timer)`
- `UTL_TimerClose()`

Use this callback shape:

```c
static long long monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void* perf_rt_timer_cb(void* data)
{
    PERF_RT_PROBE* probe = (PERF_RT_PROBE*)data;
    int idx = __sync_fetch_and_add(&probe->hits, 1);
    if(idx < probe->sample_count)
    {
        probe->stamps_ns[idx] = monotonic_ns();
    }
    return NULL;
}
```

- [ ] **Step 4: Run to verify sampling works**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 5000 0 80`

Expected: exits 0 after collecting 5000 hits using the current timer path.

- [ ] **Step 5: Commit**

```bash
git add test/perf_utl_timer_rt.c
git commit -m "perf: add linux rt timer sampling"
```

### Task 5: Compute Period And Jitter Statistics

**Files:**
- Modify: `test/perf_utl_timer_rt.c`

- [ ] **Step 1: Write the failing statistics-output expectation**

After sampling completes, print a placeholder and fail:

```c
fprintf(stderr, "stats not implemented\n");
return EXIT_FAILURE;
```

- [ ] **Step 2: Run to verify the red step**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 5000 0 80`

Expected: exits non-zero because statistics are not implemented yet.

- [ ] **Step 3: Implement period and jitter calculations**

Compute from recorded timestamps:

- `period_us[i] = (stamps_ns[i] - stamps_ns[i - 1]) / 1000.0`
- `jitter_us[i] = fabs(period_us[i] - target_period_us)`

Then calculate:

- `avg_period_us`
- `min_period_us`
- `max_period_us`
- `avg_jitter_us`
- `max_jitter_us`
- `over_100us_count`
- `over_100us_ratio`

Store jitters in a copy buffer so percentiles can be computed later.

Print:

```c
printf("stats avg_period_us=%.3f min_period_us=%.3f max_period_us=%.3f avg_jitter_us=%.3f max_jitter_us=%.3f over_100us_count=%d over_100us_ratio=%.6f\n",
       avg_period_us,
       min_period_us,
       max_period_us,
       avg_jitter_us,
       max_jitter_us,
       over_100us_count,
       over_100us_ratio);
```

- [ ] **Step 4: Run to verify the stats line prints**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 5000 0 80`

Expected: exits 0 and prints a `stats` line with non-zero numeric values.

- [ ] **Step 5: Commit**

```bash
git add test/perf_utl_timer_rt.c
git commit -m "perf: add linux rt timer jitter stats"
```

### Task 6: Add Percentiles And Final Output Contract

**Files:**
- Modify: `test/perf_utl_timer_rt.c`

- [ ] **Step 1: Write the failing percentile-output expectation**

Return failure unless `p99_jitter_us` and `p999_jitter_us` are printed:

```c
fprintf(stderr, "percentiles not implemented\n");
return EXIT_FAILURE;
```

- [ ] **Step 2: Run to verify the red step**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 5000 0 80`

Expected: exits non-zero because percentile reporting is not implemented yet.

- [ ] **Step 3: Implement jitter sorting and percentile selection**

Add:

- a `qsort` comparator for `double`
- sorted copy of the jitter buffer
- `p99_jitter_us`
- `p999_jitter_us`

Then print the final output contract:

```c
printf("result target_period_us=%d samples=%d avg_period_us=%.3f min_period_us=%.3f max_period_us=%.3f avg_jitter_us=%.3f max_jitter_us=%.3f p99_jitter_us=%.3f p999_jitter_us=%.3f over_100us_count=%d over_100us_ratio=%.6f\n",
       target_period_us,
       measured_intervals,
       avg_period_us,
       min_period_us,
       max_period_us,
       avg_jitter_us,
       max_jitter_us,
       p99_jitter_us,
       p999_jitter_us,
       over_100us_count,
       over_100us_ratio);
```

- [ ] **Step 4: Run to verify the final output**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 5000 0 80`

Expected: exits 0 and prints `env`, `runtime`, `stats`, and `result` lines.

- [ ] **Step 5: Commit**

```bash
git add test/perf_utl_timer_rt.c
git commit -m "perf: finalize linux rt timer jitter report"
```

### Task 7: Final Verification

**Files:**
- Verify: `test/perf_utl_timer_rt.c`
- Verify: `test/CMakeLists.txt`

- [ ] **Step 1: Reconfigure and rebuild the target**

Run:

```bash
cmake -S /Users/lifei/test/ent -B /Users/lifei/test/ent/build
cmake --build /Users/lifei/test/ent/build --target perf_utl_timer_rt
```

Expected: build succeeds.

- [ ] **Step 2: Run a short verification sample**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 5000 0 80`

Expected: exits 0 and prints the full report.

- [ ] **Step 3: Document the long-run command for real RT evaluation**

Use this command in the final summary:

```bash
/Users/lifei/test/ent/build/bin/perf_utl_timer_rt 1000 100000 0 80
```

Expected: on a Linux RT target, this gives a roughly 100-second sample window for `1 ms` period jitter evaluation.

- [ ] **Step 4: Commit the verification-ready state**

```bash
git add test/CMakeLists.txt test/perf_utl_timer_rt.c
git commit -m "perf: add linux rt timer jitter evaluator"
```
