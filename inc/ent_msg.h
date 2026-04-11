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
#ifndef _ENT_MSG_H_
#define _ENT_MSG_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "ent_comm.h"
#include "ent_msg_gen.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MSG_ID layout (signed 32-bit):
 * bit31:    sign bit (1 => error, 0 => non-error)
 * bit30-23: module id (8 bits)
 * bit22-15: submodule id (8 bits)
 * bit14-0 : inner code (15 bits)
 */
#define ENT_MSG_SIGN_MASK        0x80000000u
#define ENT_MSG_MODULE_SHIFT     23u
#define ENT_MSG_SUBMODULE_SHIFT  15u
#define ENT_MSG_MODULE_MASK      0xFFu
#define ENT_MSG_SUBMODULE_MASK   0xFFu
#define ENT_MSG_INNER_MASK       0x7FFFu

/* module id policy: 0-15 reserved for internal use, 16+ for external apps */
#define ENT_MSG_INTERNAL_MODULE_MIN 0u
#define ENT_MSG_INTERNAL_MODULE_MAX 15u
#define ENT_MSG_EXTERNAL_MODULE_MIN 16u

#define ENT_MSG_MODULE_ENT 0u

#define ENT_MSG_MAKE_RAW(moduleId, submoduleId, innerCode) \
    ((((uint32_t)(moduleId) & ENT_MSG_MODULE_MASK) << ENT_MSG_MODULE_SHIFT) | \
     (((uint32_t)(submoduleId) & ENT_MSG_SUBMODULE_MASK) << ENT_MSG_SUBMODULE_SHIFT) | \
     ((uint32_t)(innerCode) & ENT_MSG_INNER_MASK))

#define ENT_MSG_MAKE_OK(moduleId, submoduleId, innerCode) \
    ((MSG_ID_T)ENT_MSG_MAKE_RAW((moduleId), (submoduleId), (innerCode)))

#define ENT_MSG_MAKE_ERR(moduleId, submoduleId, innerCode) \
    ((MSG_ID_T)(ENT_MSG_SIGN_MASK | ENT_MSG_MAKE_RAW((moduleId), (submoduleId), (innerCode))))

#define ENT_MSG_IS_ERROR(code) \
    ((((uint32_t)(code)) & ENT_MSG_SIGN_MASK) != 0u)

#define ENT_MSG_GET_MODULE(code) \
    (unsigned int)((((uint32_t)(code)) >> ENT_MSG_MODULE_SHIFT) & ENT_MSG_MODULE_MASK)

#define ENT_MSG_GET_SUBMODULE(code) \
    (unsigned int)((((uint32_t)(code)) >> ENT_MSG_SUBMODULE_SHIFT) & ENT_MSG_SUBMODULE_MASK)

#define ENT_MSG_GET_INNER(code) \
    (unsigned int)(((uint32_t)(code)) & ENT_MSG_INNER_MASK)

ENT_PUBLIC bool ENT_MsgIsError(MSG_ID_T code);
ENT_PUBLIC unsigned int ENT_MsgGetModule(MSG_ID_T code);
ENT_PUBLIC unsigned int ENT_MsgGetSubmodule(MSG_ID_T code);
ENT_PUBLIC unsigned int ENT_MsgGetInner(MSG_ID_T code);
ENT_PUBLIC MSG_ID_T ENT_MsgBuild(bool isError,
                                 unsigned int moduleId,
                                 unsigned int submoduleId,
                                 unsigned int innerCode);

ENT_PUBLIC const char* ENT_MsgText(MSG_ID_T code);
ENT_PUBLIC const char* ENT_MsgModuleName(MSG_ID_T code);
ENT_PUBLIC const char* ENT_MsgSubmoduleName(MSG_ID_T code);
ENT_PUBLIC int ENT_MsgVFormat(char* out,
                              size_t outSize,
                              MSG_ID_T code,
                              va_list ap);
ENT_PUBLIC int ENT_MsgFormat(char* out,
                             size_t outSize,
                             MSG_ID_T code,
                             ...);

#ifdef __cplusplus
}
#endif

#endif
