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
#include "ient_comm.h"
#include "ent_msg.h"
#include "ent_utility.h"

#define DLL_NULL 0

ENT_PUBLIC MSG_ID_T UTL_DllIsEmpty(BOOL* isEmpty,const DLL_D_HDR *dll_hdr)
{
    if(dll_hdr==NULL || isEmpty==NULL)
    {
        IENT_LOG_ERROR("arguments validations is NULL\n");
        return ENT_DLL_BAD_ARGUMENT;
    }
    *isEmpty = (dll_hdr->fw_ptr == dll_hdr) ? TRUE : FALSE;
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_DllInitHead(DLL_D_HDR *dll_hdr)
{
    if(dll_hdr == NULL)
    {
        IENT_LOG_ERROR("arguments validations is NULL\n");
        return ENT_DLL_BAD_ARGUMENT;
    }

    dll_hdr->fw_ptr = dll_hdr;
    dll_hdr->bw_ptr = dll_hdr;
    dll_hdr->is_head = 1;
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_DllInsHead(DLL_D_HDR *dll_hdr, DLL_D_HDR *dll_elem)
{
    DLL_D_HDR* next_dll_elem = NULL;

    if(dll_hdr == DLL_NULL || dll_elem == DLL_NULL)
    {
        IENT_LOG_ERROR("arguments validations is NULL\n");
        return ENT_DLL_BAD_ARGUMENT;
    }
    if(dll_elem == dll_hdr)
    {
        IENT_LOG_ERROR("dll_elem == dll_hdr\n");
        return ENT_DLL_SAME_NODE;
    }

    dll_elem->is_head = 0;
    dll_elem->fw_ptr = dll_hdr->fw_ptr;
    dll_elem->bw_ptr = dll_hdr;

    next_dll_elem = dll_hdr->fw_ptr;
    next_dll_elem->bw_ptr = dll_elem;
    dll_hdr->fw_ptr = dll_elem;
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_DllInsCurr(DLL_D_HDR *dll_hdr,DLL_D_HDR *dll_elem)
{
    if(dll_hdr == DLL_NULL || dll_elem == DLL_NULL)
    {
        IENT_LOG_ERROR("arguments validations is NULL\n");
        return ENT_DLL_BAD_ARGUMENT;
    }
    return UTL_DllInsHead(dll_hdr->bw_ptr,dll_elem);
}

ENT_PUBLIC MSG_ID_T UTL_DllInsTail(DLL_D_HDR *dll_hdr,DLL_D_HDR *dll_elem)
{
    DLL_D_HDR* prev_dll_elem = NULL;

    if(dll_hdr == DLL_NULL || dll_elem == DLL_NULL)
    {
        IENT_LOG_ERROR("arguments validations is NULL\n");
        return ENT_DLL_BAD_ARGUMENT;
    }
    if(dll_elem == dll_hdr)
    {
        IENT_LOG_ERROR("dll_elem == dll_hdr\n");
        return ENT_DLL_SAME_NODE;
    }

    dll_elem->is_head = 0;
    dll_elem->fw_ptr = dll_hdr;
    dll_elem->bw_ptr = dll_hdr->bw_ptr;

    prev_dll_elem = dll_hdr->bw_ptr;
    prev_dll_elem->fw_ptr = dll_elem;
    dll_hdr->bw_ptr = dll_elem;
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_DllRemHead(DLL_D_HDR *dll_hdr,DLL_D_HDR **dll_elem)
{
    DLL_D_HDR* removed_elem = NULL;
    DLL_D_HDR* next_dll_elem = NULL;

    if(dll_hdr == DLL_NULL || dll_elem == DLL_NULL)
    {
        IENT_LOG_ERROR("arguments validations is NULL\n");
        return ENT_DLL_BAD_ARGUMENT;
    }
    if(dll_hdr->fw_ptr == dll_hdr)
    {
        *dll_elem = NULL;
        return ENT_DLL_EMPTY_LIST;
    }

    removed_elem = dll_hdr->fw_ptr;
    dll_hdr->fw_ptr = removed_elem->fw_ptr;
    next_dll_elem = removed_elem->fw_ptr;
    next_dll_elem->bw_ptr = dll_hdr;
    *dll_elem = removed_elem;
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_DllRemCurr(DLL_D_HDR *dll_hdr,DLL_D_HDR **dll_elem)
{
    DLL_D_HDR* prev = NULL;
    DLL_D_HDR* next = NULL;

    if(dll_hdr == DLL_NULL || dll_elem == DLL_NULL)
    {
        IENT_LOG_ERROR("arguments validations is NULL\n");
        return ENT_DLL_BAD_ARGUMENT;
    }

    *dll_elem = NULL;
    if(dll_hdr->is_head)
    {
        IENT_LOG_ERROR("cannot remove list head with UTL_DllRemCurr\n");
        return ENT_DLL_HEAD_NODE;
    }

    prev = dll_hdr->bw_ptr;
    next = dll_hdr->fw_ptr;
    prev->fw_ptr = next;
    next->bw_ptr = prev;
    *dll_elem = dll_hdr;
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_DllRemTail(DLL_D_HDR *dll_hdr,DLL_D_HDR **dll_elem)
{
    DLL_D_HDR* removed_elem = NULL;
    DLL_D_HDR* prev_dll_elem = NULL;

    if(dll_hdr == DLL_NULL || dll_elem == DLL_NULL)
    {
        IENT_LOG_ERROR("arguments validations is NULL\n");
        return ENT_DLL_BAD_ARGUMENT;
    }
    if(dll_hdr->bw_ptr == dll_hdr)
    {
        *dll_elem = NULL;
        return ENT_DLL_EMPTY_LIST;
    }

    removed_elem = dll_hdr->bw_ptr;
    dll_hdr->bw_ptr = removed_elem->bw_ptr;
    prev_dll_elem = removed_elem->bw_ptr;
    prev_dll_elem->fw_ptr = dll_hdr;
    *dll_elem = removed_elem;
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_DllNextLe(DLL_D_HDR *dll_elem,DLL_D_HDR **next_elem)
{
    DLL_D_HDR* le_ptr = NULL;

    if(dll_elem == DLL_NULL || next_elem==DLL_NULL)
    {
        IENT_LOG_ERROR("arguments validations is NULL\n");
        return ENT_DLL_BAD_ARGUMENT;
    }

    le_ptr = dll_elem->fw_ptr;
    if(le_ptr == dll_elem)
    {
        *next_elem = NULL;
        return ENT_DLL_EMPTY_LIST;
    }

    *next_elem = le_ptr;
    return ENT_SYS_NORMAL;
}

ENT_PUBLIC MSG_ID_T UTL_DllPrevLe(DLL_D_HDR *dll_elem,DLL_D_HDR **prev_elem)
{
    DLL_D_HDR* le_ptr = NULL;

    if(dll_elem == DLL_NULL || prev_elem == DLL_NULL)
    {
        IENT_LOG_ERROR("arguments validations is NULL\n");
        return ENT_DLL_BAD_ARGUMENT;
    }

    le_ptr = dll_elem->bw_ptr;
    if(le_ptr == dll_elem)
    {
        *prev_elem = NULL;
        return ENT_DLL_EMPTY_LIST;
    }

    *prev_elem = le_ptr;
    return ENT_SYS_NORMAL;
}
