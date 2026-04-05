# Runtime And Performance Tests Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 `utl_socket`、`utl_tpool`、`utl_timer` 增加运行期功能测试，并补充独立的 `perf_*` 性能测试程序，同时保持默认 `ctest` 稳定。

**Architecture:** 复用现有轻量 C 测试风格。功能测试直接扩展现有 `test/test_utl_*.c` 并纳入默认 `ctest`；性能测试新增为独立 `perf_*.c` 可执行程序，由 `test/CMakeLists.txt` 构建但不注册进默认 `ctest`。所有行为修改必须遵守 TDD，先写失败测试，再补最小测试支撑代码。

**Tech Stack:** C, CMake, ctest, pthread, BSD sockets, existing ent utility APIs

---

### Task 1: Extend Socket Functional Tests

**Files:**
- Modify: `test/test_utl_socket.c`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing peer-close and large-payload tests**

Add these test skeletons near the existing socket tests in `test/test_utl_socket.c`:

```c
static int test_socket_reports_peer_close_without_crashing(void)
{
    return 1;
}

static int test_socket_large_payload_roundtrip(void)
{
    return 1;
}
```

Append them to `main()`:

```c
failures += test_socket_reports_peer_close_without_crashing();
failures += test_socket_large_payload_roundtrip();
```

- [ ] **Step 2: Run the socket test to verify it fails**

Run: `ctest --output-on-failure -R test_utl_socket`

Expected: `test_utl_socket` fails because the new tests return failure immediately.

- [ ] **Step 3: Implement the peer-close test with current API behavior**

Replace the placeholder with a real localhost test:

```c
static int test_socket_reports_peer_close_without_crashing(void)
{
    UTL_D_SOCKET server = -1;
    UTL_D_SOCKET client = -1;
    int accepted = -1;
    struct sockaddr_in addr;
    char recvBuf[16];
    int recvBytes = -1;
    int sendBytes = -1;
    char sendBuf[] = "after-close";
    socklen_t addrLen = sizeof(addr);

    memset(&addr, 0, sizeof(addr));
    memset(recvBuf, 0, sizeof(recvBuf));

    if(expect_true(UTL_SocketInit() == 0, "UTL_SocketInit should initialize for peer-close test") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_Socket(AF_INET, SOCK_STREAM, 0, &server) == 0, "server socket create should succeed") != 0)
    {
        return 1;
    }

    addr.sin_family = AF_INET;
#ifdef __APPLE__
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if(expect_true(UTL_Bind(server, (struct sockaddr*)&addr, sizeof(addr)) == 0, "bind should succeed") != 0)
    {
        UTL_CloseSocket(server);
        return 1;
    }

    if(expect_true(UTL_Listen(server, 1) == 0, "listen should succeed") != 0)
    {
        UTL_CloseSocket(server);
        return 1;
    }

    if(getsockname(server, (struct sockaddr*)&addr, &addrLen) != 0)
    {
        perror("getsockname");
        UTL_CloseSocket(server);
        return 1;
    }

    if(expect_true(UTL_Socket(AF_INET, SOCK_STREAM, 0, &client) == 0, "client socket create should succeed") != 0)
    {
        UTL_CloseSocket(server);
        return 1;
    }

    if(expect_true(UTL_Connect(client, (struct sockaddr*)&addr, sizeof(addr)) == 0, "connect should succeed") != 0)
    {
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        return 1;
    }

    accepted = accept(server, NULL, NULL);
    if(expect_true(accepted >= 0, "accept should succeed") != 0)
    {
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        return 1;
    }

    close(accepted);

    if(expect_true(UTL_Recv(client, recvBuf, sizeof(recvBuf), 0, &recvBytes) == 0 || recvBytes == 0,
                   "recv after peer close should be observable without crashing") != 0)
    {
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        return 1;
    }

    if(expect_true(UTL_Send(client, sendBuf, (int)strlen(sendBuf), 0, &sendBytes) <= 0 || sendBytes <= 0,
                   "send after peer close should not report a clean successful write") != 0)
    {
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        return 1;
    }

    UTL_CloseSocket(client);
    UTL_CloseSocket(server);
    return 0;
}
```

- [ ] **Step 4: Run the socket test to verify peer-close passes**

Run: `ctest --output-on-failure -R test_utl_socket`

Expected: either full pass already, or only the large-payload placeholder remains failing.

- [ ] **Step 5: Implement the large-payload roundtrip test**

Add a looped send/recv test using a larger buffer:

```c
static int test_socket_large_payload_roundtrip(void)
{
    UTL_D_SOCKET server = -1;
    UTL_D_SOCKET client = -1;
    int accepted = -1;
    struct sockaddr_in addr;
    enum { PAYLOAD_SIZE = 65536 };
    char* sendBuf = NULL;
    char* recvBuf = NULL;
    int totalSent = 0;
    int totalRecv = 0;
    int ioBytes = 0;
    socklen_t addrLen = sizeof(addr);

    memset(&addr, 0, sizeof(addr));

    sendBuf = (char*)malloc(PAYLOAD_SIZE);
    recvBuf = (char*)malloc(PAYLOAD_SIZE);
    if(expect_true(sendBuf != NULL && recvBuf != NULL, "payload buffers should allocate") != 0)
    {
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    for(int i = 0; i < PAYLOAD_SIZE; ++i)
    {
        sendBuf[i] = (char)(i % 251);
        recvBuf[i] = 0;
    }

    if(expect_true(UTL_SocketInit() == 0, "UTL_SocketInit should initialize for large payload test") != 0)
    {
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    if(expect_true(UTL_Socket(AF_INET, SOCK_STREAM, 0, &server) == 0, "server socket create should succeed") != 0)
    {
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    addr.sin_family = AF_INET;
#ifdef __APPLE__
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if(expect_true(UTL_Bind(server, (struct sockaddr*)&addr, sizeof(addr)) == 0, "bind should succeed") != 0)
    {
        UTL_CloseSocket(server);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    if(expect_true(UTL_Listen(server, 1) == 0, "listen should succeed") != 0)
    {
        UTL_CloseSocket(server);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    if(getsockname(server, (struct sockaddr*)&addr, &addrLen) != 0)
    {
        perror("getsockname");
        UTL_CloseSocket(server);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    if(expect_true(UTL_Socket(AF_INET, SOCK_STREAM, 0, &client) == 0, "client socket create should succeed") != 0)
    {
        UTL_CloseSocket(server);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    if(expect_true(UTL_Connect(client, (struct sockaddr*)&addr, sizeof(addr)) == 0, "connect should succeed") != 0)
    {
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    accepted = accept(server, NULL, NULL);
    if(expect_true(accepted >= 0, "accept should succeed") != 0)
    {
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    while(totalSent < PAYLOAD_SIZE)
    {
        ioBytes = 0;
        if(expect_true(UTL_Send(client, sendBuf + totalSent, PAYLOAD_SIZE - totalSent, 0, &ioBytes) == 0,
                       "UTL_Send should succeed for payload chunks") != 0)
        {
            close(accepted);
            UTL_CloseSocket(client);
            UTL_CloseSocket(server);
            free(sendBuf);
            free(recvBuf);
            return 1;
        }
        if(expect_true(ioBytes > 0, "UTL_Send should make forward progress") != 0)
        {
            close(accepted);
            UTL_CloseSocket(client);
            UTL_CloseSocket(server);
            free(sendBuf);
            free(recvBuf);
            return 1;
        }
        totalSent += ioBytes;
    }

    while(totalRecv < PAYLOAD_SIZE)
    {
        ioBytes = 0;
        if(expect_true(UTL_Recv(accepted, recvBuf + totalRecv, PAYLOAD_SIZE - totalRecv, 0, &ioBytes) == 0,
                       "UTL_Recv should succeed for payload chunks") != 0)
        {
            close(accepted);
            UTL_CloseSocket(client);
            UTL_CloseSocket(server);
            free(sendBuf);
            free(recvBuf);
            return 1;
        }
        if(expect_true(ioBytes > 0, "UTL_Recv should make forward progress") != 0)
        {
            close(accepted);
            UTL_CloseSocket(client);
            UTL_CloseSocket(server);
            free(sendBuf);
            free(recvBuf);
            return 1;
        }
        totalRecv += ioBytes;
    }

    if(expect_true(memcmp(sendBuf, recvBuf, PAYLOAD_SIZE) == 0, "payload content should roundtrip exactly") != 0)
    {
        close(accepted);
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    close(accepted);
    UTL_CloseSocket(client);
    UTL_CloseSocket(server);
    free(sendBuf);
    free(recvBuf);
    return 0;
}
```

- [ ] **Step 6: Run the socket test to verify it passes**

Run: `ctest --output-on-failure -R test_utl_socket`

Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add test/test_utl_socket.c
git commit -m "test: add socket runtime coverage"
```

### Task 2: Extend Thread Pool Functional Tests

**Files:**
- Modify: `test/test_utl_tpool.c`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing execution and close tests**

Add placeholders and register them in `main()`:

```c
static int test_tpool_executes_task_and_end_callback(void)
{
    return 1;
}

static int test_tpool_close_returns_while_task_is_running(void)
{
    return 1;
}
```

- [ ] **Step 2: Run the thread-pool test to verify it fails**

Run: `ctest --output-on-failure -R test_utl_tpool`

Expected: `test_utl_tpool` fails because the new tests return failure immediately.

- [ ] **Step 3: Replace thread stubs with controllable real-worker support**

Refactor `test/test_utl_tpool.c` so it can exercise actual worker execution:

```c
typedef struct
{
    pthread_t thread;
    int joined;
} TEST_THREAD_ID;

static TEST_THREAD_ID* s_worker_threads[32];
static int s_worker_thread_count = 0;
```

Update the local `ENT_ThreadInit`, `ENT_ThreadCreate`, `ENT_ThreadWaitById`, and `ENT_ThreadClose` stubs to:

- keep the failure-injection counters already used by startup tests
- create a real `pthread_t` when not failing
- join/free created worker thread ids in `ENT_ThreadWaitById`
- preserve existing create/close call counters

The thread create body should look like:

```c
MSG_ID_T ENT_ThreadCreate(ENT_THREAD_ID* tid, ENT_THREAD handle, PTHREAD_START_ROUTINE thProc, void* thData)
{
    TEST_THREAD_ID* threadId = NULL;
    (void)handle;
    s_thread_create_calls++;
    if(tid != NULL)
    {
        *tid = NULL;
    }
    if(s_fail_thread_create)
    {
        return -5;
    }
    if(s_fail_thread_create_on_call != 0 && s_thread_create_calls == s_fail_thread_create_on_call)
    {
        return -5;
    }
    threadId = (TEST_THREAD_ID*)calloc(1, sizeof(TEST_THREAD_ID));
    if(threadId == NULL)
    {
        return -6;
    }
    if(pthread_create(&threadId->thread, NULL, thProc, thData) != 0)
    {
        free(threadId);
        return -7;
    }
    s_worker_threads[s_worker_thread_count++] = threadId;
    *tid = (ENT_THREAD_ID)threadId;
    return 0;
}
```

- [ ] **Step 4: Run the thread-pool test to verify the new tests still fail for the right reason**

Run: `ctest --output-on-failure -R test_utl_tpool`

Expected: the placeholders are now executable in a real-worker environment, but still fail until their logic is implemented.

- [ ] **Step 5: Implement the task execution and callback test**

Add a shared probe and test:

```c
typedef struct
{
    volatile int task_hits;
    volatile int end_hits;
    volatile int task_started;
    MSG_ID_T observed_ret;
} TPOOL_TEST_PROBE;

static MSG_ID_T test_task_cb(void* data)
{
    TPOOL_TEST_PROBE* probe = (TPOOL_TEST_PROBE*)data;
    probe->task_started = 1;
    probe->task_hits++;
    return 42;
}

static void test_task_end_cb(void* data, MSG_ID_T* retVal)
{
    TPOOL_TEST_PROBE* probe = (TPOOL_TEST_PROBE*)data;
    probe->end_hits++;
    if(retVal != NULL)
    {
        probe->observed_ret = *retVal;
    }
}

static int test_tpool_executes_task_and_end_callback(void)
{
    UTL_TPOOL pool = NULL;
    TPOOL_TEST_PROBE probe;
    MSG_ID_T retVal = -1;

    memset(&probe, 0, sizeof(probe));

    if(expect_true(UTL_TPoolInit(&pool, 1) == 0, "UTL_TPoolInit should create a worker") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TPoolAddTask(pool, test_task_cb, test_task_end_cb, &probe, &retVal) == 0,
                   "UTL_TPoolAddTask should accept a simple task") != 0)
    {
        UTL_TPoolClose(pool);
        return 1;
    }

    for(int i = 0; i < 100 && probe.end_hits == 0; ++i)
    {
        UTL_Sleep(10);
    }

    if(expect_true(probe.task_hits == 1, "task callback should run exactly once") != 0)
    {
        UTL_TPoolClose(pool);
        return 1;
    }

    if(expect_true(probe.end_hits == 1, "task end callback should run exactly once") != 0)
    {
        UTL_TPoolClose(pool);
        return 1;
    }

    if(expect_true(retVal == 42 && probe.observed_ret == 42, "task return value should be observed") != 0)
    {
        UTL_TPoolClose(pool);
        return 1;
    }

    return expect_true(UTL_TPoolClose(pool) == 0, "UTL_TPoolClose should succeed after task execution");
}
```

- [ ] **Step 6: Implement the close-while-running test**

Add a slow task and close-path assertion:

```c
static MSG_ID_T slow_task_cb(void* data)
{
    TPOOL_TEST_PROBE* probe = (TPOOL_TEST_PROBE*)data;
    probe->task_started = 1;
    UTL_Sleep(100);
    probe->task_hits++;
    return 7;
}

static int test_tpool_close_returns_while_task_is_running(void)
{
    UTL_TPOOL pool = NULL;
    TPOOL_TEST_PROBE probe;
    MSG_ID_T retVal = -1;

    memset(&probe, 0, sizeof(probe));

    if(expect_true(UTL_TPoolInit(&pool, 1) == 0, "UTL_TPoolInit should create a worker for close test") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TPoolAddTask(pool, slow_task_cb, test_task_end_cb, &probe, &retVal) == 0,
                   "UTL_TPoolAddTask should accept the slow task") != 0)
    {
        UTL_TPoolClose(pool);
        return 1;
    }

    for(int i = 0; i < 50 && probe.task_started == 0; ++i)
    {
        UTL_Sleep(10);
    }

    if(expect_true(probe.task_started == 1, "slow task should start before close") != 0)
    {
        UTL_TPoolClose(pool);
        return 1;
    }

    return expect_true(UTL_TPoolClose(pool) == 0, "UTL_TPoolClose should return without deadlock");
}
```

- [ ] **Step 7: Run the thread-pool test to verify it passes**

Run: `ctest --output-on-failure -R test_utl_tpool`

Expected: PASS

- [ ] **Step 8: Commit**

```bash
git add test/test_utl_tpool.c
git commit -m "test: add tpool runtime coverage"
```

### Task 3: Extend Timer Functional Tests

**Files:**
- Modify: `test/test_utl_timer.c`

- [ ] **Step 1: Write the failing slow-callback test**

Add a placeholder and register it in `main()`:

```c
static int test_periodic_timer_remains_stable_with_slow_callback(void)
{
    return 1;
}
```

- [ ] **Step 2: Run the timer test to verify it fails**

Run: `ctest --output-on-failure -R test_utl_timer`

Expected: `test_utl_timer` fails because the new test returns failure immediately.

- [ ] **Step 3: Implement the slow-callback periodic test**

Add a non-reentrant probe and callback:

```c
typedef struct
{
    volatile int hits;
    volatile int in_callback;
    volatile int reentry_hits;
} TIMER_SLOW_PROBE;

static void* slow_timer_cb(void* data)
{
    TIMER_SLOW_PROBE* probe = (TIMER_SLOW_PROBE*)data;
    if(probe->in_callback)
    {
        probe->reentry_hits++;
    }
    probe->in_callback = 1;
    probe->hits++;
    UTL_Sleep(60);
    probe->in_callback = 0;
    return NULL;
}

static int test_periodic_timer_remains_stable_with_slow_callback(void)
{
    UTL_TIMER_T timer = NULL;
    TIMER_SLOW_PROBE probe;

    memset(&probe, 0, sizeof(probe));

    if(expect_true(UTL_TimerInit() == 0, "UTL_TimerInit should initialize for slow callback test") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_TimerCreate(&timer, UTL_TIMER_E_PERIOD, 20, slow_timer_cb, &probe) == 0,
                   "UTL_TimerCreate should create a periodic timer for slow callback test") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    UTL_Sleep(220);

    if(expect_true(probe.hits >= 2, "slow periodic timer should still fire") != 0)
    {
        UTL_TimerDelete(&timer);
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(probe.hits <= 8, "slow periodic timer should not spin out of control") != 0)
    {
        UTL_TimerDelete(&timer);
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(probe.reentry_hits == 0, "slow periodic timer should not reenter callback") != 0)
    {
        UTL_TimerDelete(&timer);
        UTL_TimerClose();
        return 1;
    }

    if(expect_true(UTL_TimerDelete(&timer) == 0, "UTL_TimerDelete should succeed after slow callback test") != 0)
    {
        UTL_TimerClose();
        return 1;
    }

    return expect_true(UTL_TimerClose() == 0, "UTL_TimerClose should succeed after slow callback test");
}
```

- [ ] **Step 4: Run the timer test to verify it passes**

Run: `ctest --output-on-failure -R test_utl_timer`

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add test/test_utl_timer.c
git commit -m "test: add timer slow-callback coverage"
```

### Task 4: Add Socket Performance Program

**Files:**
- Create: `test/perf_utl_socket.c`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing performance program skeleton**

Create `test/perf_utl_socket.c` with:

```c
#include <stdlib.h>

int main(void)
{
    return EXIT_FAILURE;
}
```

Register a build target in `test/CMakeLists.txt` without adding a `ctest` entry:

```cmake
add_executable(perf_utl_socket perf_utl_socket.c)
target_link_libraries(perf_utl_socket PRIVATE ent pthread)
```

- [ ] **Step 2: Build the performance target to verify it exists but fails at runtime**

Run: `cmake --build /Users/lifei/test/ent/build --target perf_utl_socket`

Then run: `/Users/lifei/test/ent/build/bin/perf_utl_socket`

Expected: build succeeds; runtime exits non-zero.

- [ ] **Step 3: Implement the socket throughput program**

Replace the file with:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include "ient_comm.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

static double now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

int main(void)
{
    enum { PAYLOAD_SIZE = 1024 * 1024 };
    UTL_D_SOCKET server = -1;
    UTL_D_SOCKET client = -1;
    int accepted = -1;
    struct sockaddr_in addr;
    char* sendBuf = NULL;
    char recvBuf[8192];
    int sent = 0;
    int recvd = 0;
    int ioBytes = 0;
    double startMs = 0.0;
    double elapsedMs = 0.0;
    socklen_t addrLen = sizeof(addr);

    memset(&addr, 0, sizeof(addr));
    sendBuf = (char*)malloc(PAYLOAD_SIZE);
    if(sendBuf == NULL)
    {
        return EXIT_FAILURE;
    }
    memset(sendBuf, 'a', PAYLOAD_SIZE);

    if(UTL_SocketInit() != 0) return EXIT_FAILURE;
    if(UTL_Socket(AF_INET, SOCK_STREAM, 0, &server) != 0) return EXIT_FAILURE;
    addr.sin_family = AF_INET;
#ifdef __APPLE__
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if(UTL_Bind(server, (struct sockaddr*)&addr, sizeof(addr)) != 0) return EXIT_FAILURE;
    if(UTL_Listen(server, 1) != 0) return EXIT_FAILURE;
    if(getsockname(server, (struct sockaddr*)&addr, &addrLen) != 0) return EXIT_FAILURE;
    if(UTL_Socket(AF_INET, SOCK_STREAM, 0, &client) != 0) return EXIT_FAILURE;
    if(UTL_Connect(client, (struct sockaddr*)&addr, sizeof(addr)) != 0) return EXIT_FAILURE;
    accepted = accept(server, NULL, NULL);
    if(accepted < 0) return EXIT_FAILURE;

    startMs = now_ms();
    while(sent < PAYLOAD_SIZE)
    {
        ioBytes = 0;
        if(UTL_Send(client, sendBuf + sent, PAYLOAD_SIZE - sent, 0, &ioBytes) != 0 || ioBytes <= 0)
        {
            return EXIT_FAILURE;
        }
        sent += ioBytes;
    }
    while(recvd < PAYLOAD_SIZE)
    {
        ioBytes = 0;
        if(UTL_Recv(accepted, recvBuf, sizeof(recvBuf), 0, &ioBytes) != 0 || ioBytes <= 0)
        {
            return EXIT_FAILURE;
        }
        recvd += ioBytes;
    }
    elapsedMs = now_ms() - startMs;

    printf("socket bytes=%d elapsed_ms=%.3f throughput_mb_s=%.3f\n",
           PAYLOAD_SIZE,
           elapsedMs,
           (PAYLOAD_SIZE / (1024.0 * 1024.0)) / (elapsedMs / 1000.0));

    close(accepted);
    UTL_CloseSocket(client);
    UTL_CloseSocket(server);
    free(sendBuf);
    return EXIT_SUCCESS;
}
```

- [ ] **Step 4: Run the socket performance program**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_socket`

Expected: exits 0 and prints `bytes=`, `elapsed_ms=`, and `throughput_mb_s=`.

- [ ] **Step 5: Commit**

```bash
git add test/perf_utl_socket.c test/CMakeLists.txt
git commit -m "perf: add socket throughput probe"
```

### Task 5: Add Timer Performance Program

**Files:**
- Create: `test/perf_utl_timer.c`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing performance program skeleton**

Create:

```c
#include <stdlib.h>

int main(void)
{
    return EXIT_FAILURE;
}
```

Register:

```cmake
add_executable(perf_utl_timer perf_utl_timer.c)
target_link_libraries(perf_utl_timer PRIVATE ent pthread)
```

- [ ] **Step 2: Build and run to verify the red step**

Run: `cmake --build /Users/lifei/test/ent/build --target perf_utl_timer`

Then run: `/Users/lifei/test/ent/build/bin/perf_utl_timer`

Expected: build succeeds; runtime exits non-zero.

- [ ] **Step 3: Implement the timer stability program**

Use a fixed-size timestamp ring:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "ient_comm.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

typedef struct
{
    double stamps[256];
    int count;
} PERF_TIMER_PROBE;

static double now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static void* perf_timer_cb(void* data)
{
    PERF_TIMER_PROBE* probe = (PERF_TIMER_PROBE*)data;
    if(probe->count < 256)
    {
        probe->stamps[probe->count++] = now_ms();
    }
    return NULL;
}

int main(void)
{
    UTL_TIMER_T timer = NULL;
    PERF_TIMER_PROBE probe;
    double minDelta = 0.0;
    double maxDelta = 0.0;
    double totalDelta = 0.0;

    memset(&probe, 0, sizeof(probe));
    if(UTL_TimerInit() != 0) return EXIT_FAILURE;
    if(UTL_TimerCreate(&timer, UTL_TIMER_E_PERIOD, 20, perf_timer_cb, &probe) != 0) return EXIT_FAILURE;
    UTL_Sleep(500);
    if(UTL_TimerDelete(&timer) != 0) return EXIT_FAILURE;
    if(UTL_TimerClose() != 0) return EXIT_FAILURE;
    if(probe.count < 2) return EXIT_FAILURE;

    minDelta = probe.stamps[1] - probe.stamps[0];
    maxDelta = minDelta;
    for(int i = 1; i < probe.count; ++i)
    {
        double delta = probe.stamps[i] - probe.stamps[i - 1];
        if(delta < minDelta) minDelta = delta;
        if(delta > maxDelta) maxDelta = delta;
        totalDelta += delta;
    }

    printf("timer hits=%d avg_ms=%.3f min_ms=%.3f max_ms=%.3f\n",
           probe.count,
           totalDelta / (probe.count - 1),
           minDelta,
           maxDelta);
    return EXIT_SUCCESS;
}
```

- [ ] **Step 4: Run the timer performance program**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_timer`

Expected: exits 0 and prints `hits=`, `avg_ms=`, `min_ms=`, and `max_ms=`.

- [ ] **Step 5: Commit**

```bash
git add test/perf_utl_timer.c test/CMakeLists.txt
git commit -m "perf: add timer stability probe"
```

### Task 6: Add Thread Pool Performance Program

**Files:**
- Create: `test/perf_utl_tpool.c`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing performance program skeleton**

Create:

```c
#include <stdlib.h>

int main(void)
{
    return EXIT_FAILURE;
}
```

Register:

```cmake
add_executable(perf_utl_tpool perf_utl_tpool.c)
target_link_libraries(perf_utl_tpool PRIVATE ent pthread)
```

- [ ] **Step 2: Build and run to verify the red step**

Run: `cmake --build /Users/lifei/test/ent/build --target perf_utl_tpool`

Then run: `/Users/lifei/test/ent/build/bin/perf_utl_tpool`

Expected: build succeeds; runtime exits non-zero.

- [ ] **Step 3: Implement the thread-pool throughput program**

Use simple increment tasks and a completion counter:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "ient_comm.h"
#include "ent_thread.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

typedef struct
{
    volatile int finished;
} PERF_TPOOL_PROBE;

static double now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static MSG_ID_T perf_task_cb(void* data)
{
    (void)data;
    return 0;
}

static void perf_task_end_cb(void* data, MSG_ID_T* retVal)
{
    PERF_TPOOL_PROBE* probe = (PERF_TPOOL_PROBE*)data;
    (void)retVal;
    probe->finished++;
}

int main(void)
{
    enum { TASKS = 1000 };
    UTL_TPOOL pool = NULL;
    PERF_TPOOL_PROBE probe;
    MSG_ID_T retVals[TASKS];
    double startMs = 0.0;
    double elapsedMs = 0.0;

    memset(&probe, 0, sizeof(probe));
    memset(retVals, 0, sizeof(retVals));

    if(UTL_TPoolInit(&pool, 4) != 0) return EXIT_FAILURE;

    startMs = now_ms();
    for(int i = 0; i < TASKS; ++i)
    {
        if(UTL_TPoolAddTask(pool, perf_task_cb, perf_task_end_cb, &probe, &retVals[i]) != 0)
        {
            return EXIT_FAILURE;
        }
    }
    while(probe.finished < TASKS)
    {
        UTL_Sleep(10);
    }
    elapsedMs = now_ms() - startMs;

    if(UTL_TPoolClose(pool) != 0) return EXIT_FAILURE;

    printf("tpool workers=%d tasks=%d elapsed_ms=%.3f tasks_per_sec=%.3f\n",
           4,
           TASKS,
           elapsedMs,
           TASKS / (elapsedMs / 1000.0));
    return EXIT_SUCCESS;
}
```

- [ ] **Step 4: Run the thread-pool performance program**

Run: `/Users/lifei/test/ent/build/bin/perf_utl_tpool`

Expected: exits 0 and prints `workers=`, `tasks=`, `elapsed_ms=`, and `tasks_per_sec=`.

- [ ] **Step 5: Commit**

```bash
git add test/perf_utl_tpool.c test/CMakeLists.txt
git commit -m "perf: add tpool throughput probe"
```

### Task 7: Final Verification

**Files:**
- Verify: `test/test_utl_socket.c`
- Verify: `test/test_utl_tpool.c`
- Verify: `test/test_utl_timer.c`
- Verify: `test/perf_utl_socket.c`
- Verify: `test/perf_utl_timer.c`
- Verify: `test/perf_utl_tpool.c`
- Verify: `test/CMakeLists.txt`

- [ ] **Step 1: Run the default regression suite**

Run: `ctest --output-on-failure`

Expected: all registered tests pass.

- [ ] **Step 2: Run the three performance programs**

Run:

```bash
/Users/lifei/test/ent/build/bin/perf_utl_socket
/Users/lifei/test/ent/build/bin/perf_utl_timer
/Users/lifei/test/ent/build/bin/perf_utl_tpool
```

Expected: all exit 0 and print summary metrics.

- [ ] **Step 3: Commit the verification-ready state**

```bash
git add test/CMakeLists.txt test/test_utl_socket.c test/test_utl_tpool.c test/test_utl_timer.c test/perf_utl_socket.c test/perf_utl_timer.c test/perf_utl_tpool.c
git commit -m "test: add runtime and perf coverage"
```
