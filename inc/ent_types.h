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
#ifndef  _ENT_TYPES_H_
#define  _ENT_TYPES_H_

#include <stdbool.h>
#include <stdint.h>

typedef int8_t   ENT_I8;
typedef uint8_t  ENT_U8;
typedef int16_t  ENT_I16;
typedef uint16_t ENT_U16;
typedef int32_t  ENT_I32;
typedef uint32_t ENT_U32;
typedef int64_t  ENT_I64;
typedef uint64_t ENT_U64;

typedef ENT_I32 MSG_ID_T;
typedef ENT_I32 ENT_STATUS;
typedef ENT_U32 ENT_BOOL;
typedef ENT_U32 ENT_FLAGS;
typedef ENT_U64 ENT_SIZE;
typedef ENT_U64 ENT_OFFSET;

typedef void *DB_HANDLE;

#ifdef _WIN32
#define ENT_FILE_SEP "\\"
#define ENT_FILE_SEP_C '\\'
#else
#define ENT_FILE_SEP "/"
#define ENT_FILE_SEP_C '/'
#endif

#ifndef _WIN32
typedef int BOOL;
#define TRUE  1
#define FALSE 0
typedef unsigned long long UINT64;
typedef unsigned long      DWORD;
#define MAX_PATH PATH_MAX
#endif

#endif
