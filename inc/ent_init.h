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
#ifndef _ENT_INIT_H_
#define _ENT_INIT_H_

#include "ent_comm.h"
#include "ent_log.h"

#ifdef __cplusplus
extern "C" {
#endif

ENT_PUBLIC MSG_ID_T  ENT_Init(const char* name,const char* workPath,ENT_LOG_LEV_E logLevel);

ENT_PUBLIC MSG_ID_T  ENT_Close();

ENT_PUBLIC MSG_ID_T  ENT_Run();

ENT_PUBLIC MSG_ID_T  ENT_Helpers();

#ifdef __cplusplus
}
#endif

#endif