#ifndef ENT_EXAMPLE_LOG_CTX_CLOSE_H
#define ENT_EXAMPLE_LOG_CTX_CLOSE_H

#include "ent_log.h"

/* Example owner-side migration helper, not a library API. The owner must stop
 * new users before close, and discard any aliases when this slot is cleared. */
static inline MSG_ID_T iExample_LogCtxClose(ENT_LOG_CTX* ctx)
{
    MSG_ID_T status;
    if(ctx == NULL)
    {
        return ENT_LOG_BAD_ARGUMENT;
    }
    if(*ctx == NULL)
    {
        return ENT_SYS_NORMAL;
    }
    status = ENT_LogCtxClose(*ctx);
    if(status == ENT_SYS_NORMAL || status == ENT_LOG_IO_FAILED)
    {
        /* I/O failure reports lost data, but teardown is already complete. */
        *ctx = NULL;
    }
    /* THREAD_FAILED keeps the context for a later retry. BAD_HANDLE and
     * IN_USE require ownership/concurrency diagnosis, not a blind retry loop. */
    return status;
}

#endif
