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
#ifndef _ENT_INIT_H_
#define _ENT_INIT_H_

#include "ent_comm.h"
#include "ent_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    ENT_MODE_NORMAL_E = 0,
    ENT_MODE_REALTIME_E
} ENT_MODE_E;

typedef enum
{
    ENT_RT_POLICY_OTHER_E = 0,
    ENT_RT_POLICY_FIFO_E,
    ENT_RT_POLICY_RR_E
} ENT_RT_POLICY_E;

typedef struct ENT_HANDLE_TAG* ENT_HANDLE;

/* Initialization failures leave the caller-initialized NULL slot unchanged.
 * ENT_INIT_HANDLE_ALLOCFAIL means runtime handle allocation failed. */
ENT_PUBLIC MSG_ID_T  ENT_Init(ENT_HANDLE* handle,
                              const char* name,
                              const char* workPath,
                              ENT_LOG_LEV_E logLevel,
                              ENT_MODE_E mode);

/*
 * The owner must fence new Run/Stop/SetRtAttributes entries against close.
 * Old raw handle copies are invalid after close.
 * A retryable close failure leaves *handle valid. If logger I/O or service
 * shutdown fails after teardown completes, the error is returned and
 * *handle is cleared.
 */
ENT_PUBLIC MSG_ID_T  ENT_Close(ENT_HANDLE* handle);

/* On Linux, affinity/scheduling apply to the CALLING thread, not another runtime
 * worker. rtCpu < 0 leaves affinity unchanged; OTHER uses priority zero.
 * Calls on one runtime serialize native operations and its last-attempt snapshot.
 * Failures may leave partial native changes; no transaction/rollback is promised.
 * Settings are not propagated automatically to other existing or future threads. */
ENT_PUBLIC MSG_ID_T  ENT_SetRtAttributes(ENT_HANDLE handle,
                                          int rtCpu,
                                          ENT_RT_POLICY_E rtPolicy,
                                          int rtPriority);

ENT_PUBLIC MSG_ID_T  ENT_Stop(ENT_HANDLE handle);

/* Returns ENT_SYS_ALREADY_INITIALIZED if this instance is already running. */
ENT_PUBLIC MSG_ID_T  ENT_Run(ENT_HANDLE handle);

ENT_PUBLIC MSG_ID_T  ENT_Helpers(void);

#ifdef __cplusplus
}
#endif

#endif
