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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ENT_SharedMap ENT_SharedMap;

typedef enum ENT_SharedMapMode
{
    ENT_SHM_MODE_READ_ONLY = 0,
    ENT_SHM_MODE_READ_WRITE = 1
} ENT_SharedMapMode;

typedef struct ENT_SharedMapOptions
{
    const char* path;
    unsigned long long size;
    ENT_SharedMapMode mode;
    int create_if_missing;
    int truncate_if_exists;
    int lock_memory;
} ENT_SharedMapOptions;

ENT_PUBLIC int ENT_SharedMapOpen(const ENT_SharedMapOptions* options, ENT_SharedMap** out_map);
ENT_PUBLIC void* ENT_SharedMapPtr(ENT_SharedMap* map);
ENT_PUBLIC unsigned long long ENT_SharedMapSize(ENT_SharedMap* map);
ENT_PUBLIC int ENT_SharedMapFlush(ENT_SharedMap* map, unsigned long long offset, unsigned long long length);
ENT_PUBLIC int ENT_SharedMapClose(ENT_SharedMap* map);

#ifdef __cplusplus
}
#endif

#endif
