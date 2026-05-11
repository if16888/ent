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
#ifndef _ENT_SHM_H_
#define _ENT_SHM_H_

#include "ent_comm.h"
#include "ent_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ENT_SharedMap ENT_SharedMap;

typedef enum ENT_SharedMapMode
{
    ENT_SHM_MODE_READ_ONLY = 0,
    ENT_SHM_MODE_READ_WRITE = 1
} ENT_SharedMapMode;

#define ENT_SHM_F_CREATE_IF_MISSING ((ENT_FLAGS)0x00000001u)
#define ENT_SHM_F_TRUNCATE_IF_EXISTS ((ENT_FLAGS)0x00000002u)
#define ENT_SHM_F_LOCK_MEMORY        ((ENT_FLAGS)0x00000004u)

typedef struct ENT_SharedMapOptions
{
    const char* path;
    ENT_SIZE size;
    ENT_SharedMapMode mode;
    ENT_FLAGS flags;
} ENT_SharedMapOptions;

ENT_PUBLIC MSG_ID_T ENT_SharedMapOpen(const ENT_SharedMapOptions* options, ENT_SharedMap** out_map);
ENT_PUBLIC void* ENT_SharedMapPtr(ENT_SharedMap* map);
ENT_PUBLIC ENT_SIZE ENT_SharedMapSize(ENT_SharedMap* map);
ENT_PUBLIC MSG_ID_T ENT_SharedMapFlush(ENT_SharedMap* map, ENT_OFFSET offset, ENT_SIZE length);
ENT_PUBLIC MSG_ID_T ENT_SharedMapClose(ENT_SharedMap** map);

#ifdef __cplusplus
}
#endif

#endif
