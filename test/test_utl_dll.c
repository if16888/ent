#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ient_comm.h"
#include "ent_msg.h"

typedef struct TEST_NODE
{
    DLL_D_HDR link;
    int value;
} TEST_NODE;

ENT_CTX gEntCtx;

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }

    return 0;
}

MSG_ID_T ENT_LogInit(void)
{
    return 0;
}

MSG_ID_T ENT_LogClose(void)
{
    return 0;
}

MSG_ID_T ENT_LogInitHandle(ENT_LOG* pLogHandle, const char* moduleName, const char* logPath)
{
    (void)pLogHandle;
    (void)moduleName;
    (void)logPath;
    return 0;
}

MSG_ID_T ENT_LogSetOption(ENT_LOG logHandle, ENT_LOG_OPTIONS_E option, const void* arg)
{
    (void)logHandle;
    (void)option;
    (void)arg;
    return 0;
}

MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle)
{
    (void)logHandle;
    return 0;
}

MSG_ID_T ENT_LogRaw(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogFatal(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogError(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogWarn(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogPrint(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogDebug(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

static int test_dll_init_marks_head_empty(void)
{
    DLL_D_HDR head;
    BOOL isEmpty = FALSE;
    DLL_D_HDR* next = (DLL_D_HDR*)0x1;
    DLL_D_HDR* prev = (DLL_D_HDR*)0x1;

    if(expect_true(UTL_DllInitHead(&head) == 0, "UTL_DllInitHead should initialize a head node") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_DllIsEmpty(&isEmpty, &head) == 0, "UTL_DllIsEmpty should accept an initialized head") != 0)
    {
        return 1;
    }

    if(expect_true(isEmpty == TRUE, "An initialized list head should be empty") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_DllNextLe(&head, &next) == ENT_DLL_EMPTY_LIST, "UTL_DllNextLe should report no next node for an empty list") != 0)
    {
        return 1;
    }

    if(expect_true(next == NULL, "UTL_DllNextLe should clear next output for an empty list") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_DllPrevLe(&head, &prev) == ENT_DLL_EMPTY_LIST, "UTL_DllPrevLe should report no previous node for an empty list") != 0)
    {
        return 1;
    }

    return expect_true(prev == NULL, "UTL_DllPrevLe should clear previous output for an empty list");
}

static int test_dll_insert_head_and_tail_preserve_order(void)
{
    DLL_D_HDR head;
    TEST_NODE first;
    TEST_NODE second;
    DLL_D_HDR* next = NULL;
    DLL_D_HDR* prev = NULL;
    BOOL isEmpty = TRUE;

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    first.value = 1;
    second.value = 2;

    UTL_DllInitHead(&head);

    if(expect_true(UTL_DllInsHead(&head, &first.link) == 0, "UTL_DllInsHead should insert the first node") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_DllInsTail(&head, &second.link) == 0, "UTL_DllInsTail should append a node to the tail") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_DllIsEmpty(&isEmpty, &head) == 0, "UTL_DllIsEmpty should inspect a populated list") != 0)
    {
        return 1;
    }

    if(expect_true(isEmpty == FALSE, "A populated list should not be empty") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_DllNextLe(&head, &next) == 0, "UTL_DllNextLe should return the first node from the head") != 0)
    {
        return 1;
    }

    if(expect_true(next == &first.link, "Head next should point to the head-inserted node") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_DllPrevLe(&head, &prev) == 0, "UTL_DllPrevLe should return the tail node from the head") != 0)
    {
        return 1;
    }

    return expect_true(prev == &second.link, "Head previous should point to the tail-inserted node");
}

static int test_dll_remove_head_then_tail_returns_inserted_nodes(void)
{
    DLL_D_HDR head;
    TEST_NODE first;
    TEST_NODE second;
    DLL_D_HDR* removed = NULL;
    BOOL isEmpty = FALSE;

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));

    UTL_DllInitHead(&head);
    UTL_DllInsHead(&head, &first.link);
    UTL_DllInsTail(&head, &second.link);

    if(expect_true(UTL_DllRemHead(&head, &removed) == 0, "UTL_DllRemHead should remove the first node") != 0)
    {
        return 1;
    }

    if(expect_true(removed == &first.link, "UTL_DllRemHead should return the head node first") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_DllRemTail(&head, &removed) == 0, "UTL_DllRemTail should remove the remaining tail node") != 0)
    {
        return 1;
    }

    if(expect_true(removed == &second.link, "UTL_DllRemTail should return the tail node") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_DllIsEmpty(&isEmpty, &head) == 0, "UTL_DllIsEmpty should still work after removals") != 0)
    {
        return 1;
    }

    return expect_true(isEmpty == TRUE, "Removing all nodes should leave the list empty");
}

static int test_dll_remove_head_reports_empty_list(void)
{
    DLL_D_HDR head;
    DLL_D_HDR* removed = (DLL_D_HDR*)0x1;

    UTL_DllInitHead(&head);

    if(expect_true(UTL_DllRemHead(&head, &removed) == ENT_DLL_EMPTY_LIST,
                   "UTL_DllRemHead should report an empty list when only the sentinel remains") != 0)
    {
        return 1;
    }

    return expect_true(removed == NULL, "UTL_DllRemHead should clear the output when the list is empty");
}

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

    if(expect_true(UTL_DllRemCurr(&head, &removed) == ENT_DLL_HEAD_NODE,
                   "UTL_DllRemCurr should reject attempts to remove the sentinel head") != 0)
    {
        return 1;
    }

    if(expect_true(removed == NULL,
                   "UTL_DllRemCurr should clear the output when rejecting the head") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_DllNextLe(&head, &next) == 0,
                   "UTL_DllNextLe should still return the first node after a rejected head removal") != 0)
    {
        return 1;
    }

    if(expect_true(next == &first.link,
                   "Rejected head removal should leave the first node unchanged") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_DllPrevLe(&head, &prev) == 0,
                   "UTL_DllPrevLe should still return the tail node after a rejected head removal") != 0)
    {
        return 1;
    }

    return expect_true(prev == &second.link,
                       "Rejected head removal should leave the tail node unchanged");
}

int main(void)
{
    int failures = 0;

    failures += test_dll_init_marks_head_empty();
    failures += test_dll_insert_head_and_tail_preserve_order();
    failures += test_dll_remove_head_then_tail_returns_inserted_nodes();
    failures += test_dll_remove_head_reports_empty_list();
    failures += test_dll_remove_curr_rejects_head_without_mutating_list();

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
