# DLL Head Flag Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `UTL_DllRemCurr()` safely reject the sentinel head node by adding explicit head metadata to `DLL_D_HDR`.

**Architecture:** Extend `DLL_D_HDR` with a lightweight `is_head` flag so head and element nodes are distinguishable at runtime. Keep the public APIs unchanged, initialize the flag in `UTL_DllInitHead()`, clear it on inserted nodes, and add a regression test that proves rejecting the head has no side effects.

**Tech Stack:** C99, CMake, ctest

---

### Task 1: Add Failing Regression Test For Head Rejection

**Files:**
- Modify: `test/test_utl_dll.c`
- Test: `test/test_utl_dll.c`

- [ ] **Step 1: Write the failing test**

```c
static int test_dll_remove_curr_rejects_head_without_mutating_list(void)
{
    DLL_D_HDR head;
    TEST_NODE first;
    TEST_NODE second;
    DLL_D_HDR* removed = (DLL_D_HDR*)0x1;
    DLL_D_HDR* next = NULL;
    DLL_D_HDR* prev = NULL;

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));

    UTL_DllInitHead(&head);
    UTL_DllInsHead(&head, &first.link);
    UTL_DllInsTail(&head, &second.link);

    if(expect_true(UTL_DllRemCurr(&head, &removed) == -2,
                   "UTL_DllRemCurr should reject attempts to remove the sentinel head") != 0)
    {
        return 1;
    }

    if(expect_true(removed == NULL,
                   "UTL_DllRemCurr should clear the output when rejecting the head") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_DllNextLe(&head, &next) == 0, "UTL_DllNextLe should still return the first node") != 0)
    {
        return 1;
    }

    if(expect_true(next == &first.link, "Rejected head removal should leave the first node unchanged") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_DllPrevLe(&head, &prev) == 0, "UTL_DllPrevLe should still return the tail node") != 0)
    {
        return 1;
    }

    return expect_true(prev == &second.link,
                       "Rejected head removal should leave the tail node unchanged");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_utl_dll && ./build/bin/test_utl_dll`
Expected: FAIL because the current `UTL_DllRemCurr()` mutates the list before returning `-2`.

- [ ] **Step 3: Write minimal implementation**

```c
typedef struct dll_header
{
  struct dll_header  *fw_ptr;
  struct dll_header  *bw_ptr;
  int                is_head;
} DLL_D_HDR;
```

```c
MSG_ID_T  UTL_DllInitHead(DLL_D_HDR *dll_hdr)
{
    ...
    dll_hdr->fw_ptr = dll_hdr;
    dll_hdr->bw_ptr = dll_hdr;
    dll_hdr->is_head = 1;
}
```

```c
dll_elem->is_head = 0;
```

```c
if(dll_hdr->is_head)
{
    *dll_elem = NULL;
    sts = -2;
    IENT_LOG_ERROR("cannot remove the sentinel head with UTL_DllRemCurr\n");
    goto EXIT;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_utl_dll && ./build/bin/test_utl_dll`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add inc/ent_utility.h comm/utl_dll.c test/test_utl_dll.c docs/superpowers/plans/2026-04-04-dll-head-flag-fix.md
git commit -m "fix dll head removal detection"
```

### Task 2: Run Regression Suite

**Files:**
- Modify: `inc/ent_utility.h`
- Modify: `comm/utl_dll.c`
- Test: `test/test_utl_dll.c`

- [ ] **Step 1: Run focused ctest**

Run: `ctest --output-on-failure -R test_utl_dll --test-dir build`
Expected: PASS

- [ ] **Step 2: Run full suite**

Run: `ctest --output-on-failure --test-dir build`
Expected: PASS all registered tests

- [ ] **Step 3: Inspect remaining worktree changes**

Run: `git status --short`
Expected: Only the intended DLL-related files are additionally changed.
