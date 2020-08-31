/*-----------------------------------------------------------------------------
 *   Copyright 2019 Fei Li<if16888@foxmail.com>
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

typedef int MSG_ID_T;

typedef void *DB_HANDLE;

#ifdef WIN32
#define ENT_FILE_SEP "\\"
#define ENT_FILE_SEP_C '\\'
#else
#define ENT_FILE_SEP "/"
#define ENT_FILE_SEP_C '/'
#endif

#ifdef __linux__
typedef int BOOL;
#define TRUE  1
#define FALSE 0
typedef unsigned long long UINT64;
typedef unsigned long      DWORD;
#define MAX_PATH PATH_MAX
#endif

#endif