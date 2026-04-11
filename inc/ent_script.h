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
#ifndef _ENT_SCRIPT_H_
#define _ENT_SCRIPT_H_

#include <stddef.h>

#include "ent_comm.h"
#include "ent_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const char* key;
    /*
     * Backward-compatible string payload.
     * Used when type is ENT_SCRIPT_ARG_STRING_E or type is unset/invalid.
     */
    const char* value;
    long long intValue;
    double numValue;
    bool boolValue;
    int type;
} ENT_SCRIPT_KV_T;

typedef enum
{
    ENT_SCRIPT_ARG_STRING_E = 0,
    ENT_SCRIPT_ARG_INT_E = 1,
    ENT_SCRIPT_ARG_DOUBLE_E = 2,
    ENT_SCRIPT_ARG_BOOL_E = 3,
    ENT_SCRIPT_ARG_NULL_E = 4
} ENT_SCRIPT_ARG_TYPE_E;

typedef struct
{
    const ENT_SCRIPT_KV_T* items;
    size_t count;
} ENT_SCRIPT_ARG_T;

typedef struct
{
    int code;
    char message[256];
} ENT_SCRIPT_RET_T;

ENT_PUBLIC MSG_ID_T ENT_ScriptInit(const char* scriptRoot);
ENT_PUBLIC MSG_ID_T ENT_ScriptReload(const char* scriptName);
ENT_PUBLIC MSG_ID_T ENT_ScriptCall(const char* fn,
                                   const ENT_SCRIPT_ARG_T* in,
                                   ENT_SCRIPT_RET_T* out);
ENT_PUBLIC MSG_ID_T ENT_ScriptClose(void);

#ifdef __cplusplus
}
#endif

#endif
