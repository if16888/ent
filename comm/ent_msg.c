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
#include "ent_msg.h"
#include <stdio.h>

static const char* const gEntMsgUnknownText = "unknown message id";
static const char* const gEntMsgUnknownModule = "UNKNOWN_MODULE";
static const char* const gEntMsgUnknownSubmodule = "UNKNOWN_SUBMODULE";
static const char* const gEntMsgExternalModule = "APP_MODULE";
static const char* const gEntMsgExternalSubmodule = "APP_SUBMODULE";

ENT_PUBLIC bool ENT_MsgIsError(MSG_ID_T code)
{
    return ENT_MSG_IS_ERROR(code);
}

ENT_PUBLIC unsigned int ENT_MsgGetModule(MSG_ID_T code)
{
    return ENT_MSG_GET_MODULE(code);
}

ENT_PUBLIC unsigned int ENT_MsgGetSubmodule(MSG_ID_T code)
{
    return ENT_MSG_GET_SUBMODULE(code);
}

ENT_PUBLIC unsigned int ENT_MsgGetInner(MSG_ID_T code)
{
    return ENT_MSG_GET_INNER(code);
}

ENT_PUBLIC MSG_ID_T ENT_MsgBuild(bool isError,
                                 unsigned int moduleId,
                                 unsigned int submoduleId,
                                 unsigned int innerCode)
{
    if(isError)
    {
        return ENT_MSG_MAKE_ERR(moduleId, submoduleId, innerCode);
    }

    return ENT_MSG_MAKE_OK(moduleId, submoduleId, innerCode);
}

ENT_PUBLIC const char* ENT_MsgText(MSG_ID_T code)
{
    size_t i = 0;

    for(i = 0; i < g_ent_msg_items_count; ++i)
    {
        if(g_ent_msg_items[i].code == code)
        {
            return g_ent_msg_items[i].message;
        }
    }

    return gEntMsgUnknownText;
}

ENT_PUBLIC const char* ENT_MsgModuleName(MSG_ID_T code)
{
    unsigned int moduleId = ENT_MsgGetModule(code);
    size_t i = 0;

    for(i = 0; i < g_ent_msg_modules_count; ++i)
    {
        if((unsigned int)g_ent_msg_modules[i].module == moduleId)
        {
            return g_ent_msg_modules[i].moduleName;
        }
    }

    if(moduleId >= ENT_MSG_EXTERNAL_MODULE_MIN)
    {
        return gEntMsgExternalModule;
    }

    return gEntMsgUnknownModule;
}

ENT_PUBLIC const char* ENT_MsgSubmoduleName(MSG_ID_T code)
{
    unsigned int moduleId = ENT_MsgGetModule(code);
    unsigned int submoduleId = ENT_MsgGetSubmodule(code);
    size_t i = 0;

    for(i = 0; i < g_ent_msg_submodules_count; ++i)
    {
        if((unsigned int)g_ent_msg_submodules[i].module == moduleId &&
           (unsigned int)g_ent_msg_submodules[i].submodule == submoduleId)
        {
            return g_ent_msg_submodules[i].submoduleName;
        }
    }

    if(moduleId >= ENT_MSG_EXTERNAL_MODULE_MIN)
    {
        return gEntMsgExternalSubmodule;
    }

    return gEntMsgUnknownSubmodule;
}

ENT_PUBLIC int ENT_MsgVFormat(char* out,
                              size_t outSize,
                              MSG_ID_T code,
                              va_list ap)
{
    const char* fmt = ENT_MsgText(code);

    if(out == NULL || outSize == 0)
    {
        return -1;
    }

    return vsnprintf(out, outSize, fmt, ap);
}

ENT_PUBLIC int ENT_MsgFormat(char* out,
                             size_t outSize,
                             MSG_ID_T code,
                             ...)
{
    int n = 0;
    va_list ap;

    va_start(ap, code);
    n = ENT_MsgVFormat(out, outSize, code, ap);
    va_end(ap);
    return n;
}
