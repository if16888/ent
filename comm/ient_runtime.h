/*-----------------------------------------------------------------------------
 *   Copyright 2019 Fei Li
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 *-----------------------------------------------------------------------------
 */
#ifndef _I_ENT_RUNTIME_H_
#define _I_ENT_RUNTIME_H_

#include <stdlib.h>
#include <string.h>

#include "ent_log.h"
#include "ent_utility.h"

typedef enum
{
    ENT_HANDLE_STATE_ACTIVE_E = 0,
    ENT_HANDLE_STATE_CLOSING_E,
    ENT_HANDLE_STATE_CLOSED_E
} ENT_HANDLE_STATE_E;

typedef struct ENT_CTX
{
    bool           isInit;
    bool           rtRequested;
    bool           rtEnabled;
    bool           running;
    bool           stopRequested;
    ENT_HANDLE_STATE_E handleState;
    unsigned int   activeCalls;
    char*          entName;
    char*          workPath;
    char*          logName;
    char*          logPath;
    int            rtCpu;
    int            rtPolicy;
    int            rtPriority;
    int            rtLastError;
    ENT_LOG_LEV_E  logLevel;
    ENT_LOG        entLog;
    UTL_LOCK       entLock;
    UTL_CV         entCV;
} ENT_CTX;

typedef struct ENT_HANDLE_TAG
{
    unsigned int   magic;
    ENT_CTX ctx;
} ENT_HANDLE_CTX_T;

extern ENT_CTX gEntCtx;

#define ENT_HANDLE_MAGIC 0x454E5448u

ENT_CTX* iENT_RuntimeActiveCtx(void);

static inline ENT_LOG iENT_LogDefaultHandle(void)
{
    ENT_CTX* ctx = iENT_RuntimeActiveCtx();

    if(ctx != NULL && ctx->entLog != NULL)
    {
        return ctx->entLog;
    }

    return gEntCtx.entLog;
}

#endif
