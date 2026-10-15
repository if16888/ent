#include <stdio.h>
#include "log_ctx_close.h"

int main(void)
{
    ENT_LOG_CTX ctx = NULL;
    ENT_LOG handle = NULL;
    MSG_ID_T status;
    MSG_ID_T closeStatus;
    MSG_ID_T serviceStatus;

    if(ENT_LogInit() < 0)
    {
        return 1;
    }
    status = ENT_LogCtxInit(&ctx);
    if(status == ENT_SYS_NORMAL)
    {
        status = ENT_LogCtxInitHandle(ctx, &handle, "close_migration", ".");
        if(status == ENT_SYS_NORMAL)
        {
            status = ENT_LogCtxPrint(ctx, handle, "owner clears a fully closed context\n");
        }
    }
    closeStatus = iExample_LogCtxClose(&ctx);
    if(closeStatus == ENT_LOG_IO_FAILED)
    {
        fprintf(stderr, "Log I/O failed; context is released and must not be retried.\n");
    }
    if(ctx != NULL)
    {
        fprintf(stderr, "Context retained after status %d; diagnose before retrying.\n", (int)closeStatus);
        return 1;
    }
    handle = NULL; /* The context owned and released this handle. */
    serviceStatus = ENT_LogClose();
    return status < 0 || closeStatus < 0 || serviceStatus < 0 ? 1 : 0;
}
