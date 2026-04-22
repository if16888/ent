/*-----------------------------------------------------------------------------
 *   Copyright 2019 Fei Li
 *-----------------------------------------------------------------------------
 */
#include <stdarg.h>
#include <stdio.h>

#include "ent_log.h"
#include "ient_log.h"

MSG_ID_T ENT_LogInit(void)
{
    return iENT_LogInit();
}

MSG_ID_T ENT_LogClose(void)
{
    return iENT_LogClose();
}

MSG_ID_T ENT_LogInitHandle(ENT_LOG* pLogHandle, const char* moduleName, const char* logPath)
{
    return iENT_LogInitHandle(pLogHandle, moduleName, logPath);
}

MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle)
{
    return iENT_LogCloseHandle(logHandle);
}

MSG_ID_T ENT_LogRaw(ENT_LOG logHandle, const char* format, ...)
{
    MSG_ID_T sts = 0;
    ENT_LOG_CTX_INTERNAL* logCtx = NULL;
    va_list va_args;

    sts = iENT_LogAcquireWriter(&logCtx, logHandle);
    if(sts < 0)
    {
        return sts;
    }
    va_start(va_args, format);
    sts = iENT_LogVRaw(logCtx, format, va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    return sts;
}

MSG_ID_T ENT_LogFatal(ENT_LOG logHandle, const char* format, ...)
{
    MSG_ID_T sts = 0;
    ENT_LOG_CTX_INTERNAL* logCtx = NULL;
    va_list va_args;

    sts = iENT_LogAcquireWriter(&logCtx, logHandle);
    if(sts < 0)
    {
        return sts;
    }
    if(LOG_LEV_FATAL_E > logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        return ENT_LOG_RC_NON_FATAL;
    }

    va_start(va_args, format);
    sts = iENT_LogVPrint(logCtx, LOG_LEV_FATAL_E, format, va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    return sts;
}

MSG_ID_T ENT_LogError(ENT_LOG logHandle, const char* format, ...)
{
    MSG_ID_T sts = 0;
    ENT_LOG_CTX_INTERNAL* logCtx = NULL;
    va_list va_args;

    sts = iENT_LogAcquireWriter(&logCtx, logHandle);
    if(sts < 0)
    {
        return sts;
    }
    if(LOG_LEV_ERROR_E > logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        return ENT_LOG_RC_NON_FATAL;
    }

    va_start(va_args, format);
    sts = iENT_LogVPrint(logCtx, LOG_LEV_ERROR_E, format, va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    return sts;
}

MSG_ID_T ENT_LogWarn(ENT_LOG logHandle, const char* format, ...)
{
    MSG_ID_T sts = 0;
    ENT_LOG_CTX_INTERNAL* logCtx = NULL;
    va_list va_args;

    sts = iENT_LogAcquireWriter(&logCtx, logHandle);
    if(sts < 0)
    {
        return sts;
    }
    if(LOG_LEV_WARN_E > logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        return ENT_LOG_RC_NON_FATAL;
    }

    va_start(va_args, format);
    sts = iENT_LogVPrint(logCtx, LOG_LEV_WARN_E, format, va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    return sts;
}

MSG_ID_T ENT_LogPrint(ENT_LOG logHandle, const char* format, ...)
{
    MSG_ID_T sts = 0;
    ENT_LOG_CTX_INTERNAL* logCtx = NULL;
    va_list va_args;

    sts = iENT_LogAcquireWriter(&logCtx, logHandle);
    if(sts < 0)
    {
        return sts;
    }
    if(LOG_LEV_INFO_E > logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        return ENT_LOG_RC_NON_FATAL;
    }

    va_start(va_args, format);
    sts = iENT_LogVPrint(logCtx, LOG_LEV_INFO_E, format, va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    return sts;
}

MSG_ID_T ENT_LogDebug(ENT_LOG logHandle, const char* format, ...)
{
    MSG_ID_T sts = 0;
    ENT_LOG_CTX_INTERNAL* logCtx = NULL;
    va_list va_args;

    sts = iENT_LogAcquireWriter(&logCtx, logHandle);
    if(sts < 0)
    {
        return sts;
    }
    if(LOG_LEV_DEBUG_E > logCtx->logLevel)
    {
        iENT_LogReleaseWriter(logCtx);
        return ENT_LOG_RC_NON_FATAL;
    }

    va_start(va_args, format);
    sts = iENT_LogVPrint(logCtx, LOG_LEV_DEBUG_E, format, va_args);
    va_end(va_args);
    iENT_LogReleaseWriter(logCtx);
    return sts;
}
