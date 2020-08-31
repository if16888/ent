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
#include "ient_comm.h"
#include "ent_utility.h"


#define DLL_NULL 0
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  UTL_DllIsEmpty
 *
 * DESCRIPTION :  init the head of absolute linked list
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  UTL_DllIsEmpty(BOOL* isEmpty,const DLL_D_HDR *dll_hdr) 
{
    if(dll_hdr==NULL || isEmpty==NULL)
    {
        IENT_LOG_ERROR("arguments validations is NULL\n") ;
        return -1;
    }
    if(dll_hdr->fw_ptr == dll_hdr && dll_hdr->bw_ptr == dll_hdr)
    {
        *isEmpty = TRUE;
    }
    else
    {
        *isEmpty = FALSE;
    }
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  UTL_DllInitHead
 *
 * DESCRIPTION :  init the head of absolute linked list
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  UTL_DllInitHead(DLL_D_HDR *dll_hdr) 
{
    MSG_ID_T    sts = 0;

    if(dll_hdr == NULL)
    {
        sts = -1;
        IENT_LOG_ERROR("arguments validations is NULL\n") ;
        goto EXIT;
    }
    else
    {
        dll_hdr->fw_ptr = dll_hdr;
        dll_hdr->bw_ptr = dll_hdr;
    }

EXIT:
     return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  UTL_DllInsHead
 *
 * DESCRIPTION :  insert element at head of absolute linked list
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  UTL_DllInsHead(DLL_D_HDR *dll_hdr, DLL_D_HDR *dll_elem) 
{   
     DLL_D_HDR  *next_dll_elem;
     MSG_ID_T   sts = 0;

     if(dll_hdr == DLL_NULL || dll_elem == DLL_NULL)
     {
        sts = -1;
        IENT_LOG_ERROR("arguments validations is NULL\n") ;
        goto EXIT;
     }
    else if (dll_elem == dll_hdr)
    {
        sts = -2;
        IENT_LOG_ERROR("dll_elem == dll_hdr\n") ;
        goto EXIT;
    }
    else
    {
		dll_elem->fw_ptr = dll_hdr->fw_ptr;
		dll_elem->bw_ptr = dll_hdr;
		
		next_dll_elem   = dll_hdr->fw_ptr;
		next_dll_elem->bw_ptr = dll_elem;
		dll_hdr->fw_ptr = dll_elem;
    }

EXIT:
     return sts;
 }
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  UTL_DllInsCurr
 *
 * DESCRIPTION :  insert element from current of absolute linked list.
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  UTL_DllInsCurr(DLL_D_HDR *dll_hdr,DLL_D_HDR *dll_elem)   
{
     MSG_ID_T   sts = 0;

     if(dll_hdr == DLL_NULL || dll_elem == DLL_NULL)
     {
        sts = -1;
        IENT_LOG_ERROR("arguments validations is NULL\n") ;
        goto EXIT;
     }
     else
     {
        sts = UTL_DllInsHead(dll_hdr->bw_ptr,dll_elem);
     }

EXIT:
     return sts;
 }
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  UTL_DllInsTail
 *
 * DESCRIPTION :  insert element at tail of absolute linked list.
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  UTL_DllInsTail(DLL_D_HDR *dll_hdr,DLL_D_HDR *dll_elem )
{
     DLL_D_HDR   *prev_dll_elem;
     MSG_ID_T    sts = 0;

    if(dll_hdr == DLL_NULL || dll_elem == DLL_NULL)
    {
        sts = -1;
        IENT_LOG_ERROR("arguments validations is NULL\n") ;
        goto EXIT;
    }
    else if (dll_elem == dll_hdr)
    {
        sts = -2;
        IENT_LOG_ERROR("dll_elem == dll_hdr\n") ;
        goto EXIT;
    }
    else
    {
        dll_elem->fw_ptr = dll_hdr;
        dll_elem->bw_ptr = dll_hdr->bw_ptr;

        prev_dll_elem  = dll_hdr->bw_ptr;
        prev_dll_elem->fw_ptr = dll_elem;
        dll_hdr->bw_ptr = dll_elem;
    }

EXIT:
     return sts;
 }
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  UTL_DllRemHead
 *
 * DESCRIPTION :  remove element from head of absolute linked list.
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  UTL_DllRemHead(DLL_D_HDR *dll_hdr,DLL_D_HDR **dll_elem )   
{
     DLL_D_HDR  *removed_elem;
     DLL_D_HDR  *next_dll_elem;
     MSG_ID_T   sts = 0;

     if(dll_hdr == DLL_NULL || dll_elem == DLL_NULL)
     {
        sts = -1;
        IENT_LOG_ERROR("arguments validations is NULL\n") ;
        goto EXIT;
     }
     else
     {
        removed_elem = dll_hdr->fw_ptr;
        dll_hdr->fw_ptr = removed_elem->fw_ptr;
        next_dll_elem = removed_elem->fw_ptr;
        next_dll_elem->bw_ptr = dll_hdr;

        *dll_elem = removed_elem;
        if( removed_elem == dll_hdr )
        {
            *dll_elem = NULL;
            sts = -2;
        }
     }

EXIT:
     return sts;
 }
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  UTL_DllRemCurr
 *
 * DESCRIPTION :  remove current element from  absolute linked list.
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  UTL_DllRemCurr(DLL_D_HDR *dll_hdr,DLL_D_HDR **dll_elem)   
{
     MSG_ID_T   sts = 0;

     if(dll_hdr == DLL_NULL || dll_elem == DLL_NULL)
     {
        sts = -1;
        IENT_LOG_ERROR("arguments validations is NULL\n") ;
        goto EXIT;
     }
     else
     {
        sts = UTL_DllRemHead(dll_hdr->bw_ptr,dll_elem);
     }

EXIT:
     return sts;
 }
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  UTL_DllRemTail
 *
 * DESCRIPTION :  remove element from tail of absolute linked list
 *
 * COMPLETION
 * STATUS      :  0
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  UTL_DllRemTail(DLL_D_HDR   *dll_hdr,DLL_D_HDR   **dll_elem )
{
     DLL_D_HDR   *removed_elem;
     DLL_D_HDR   *prev_dll_elem;
     MSG_ID_T    sts = 0;

     if (dll_hdr == DLL_NULL || dll_elem == DLL_NULL)
     {
        sts = -1;
        IENT_LOG_ERROR("arguments validations is NULL\n") ;
        goto EXIT;
     }
     else
     {
        removed_elem = dll_hdr->bw_ptr;
        dll_hdr->bw_ptr = removed_elem->bw_ptr;
        prev_dll_elem = removed_elem->bw_ptr;
        prev_dll_elem->fw_ptr = dll_hdr;

        *dll_elem = removed_elem;
        if ( removed_elem == dll_hdr )
        {
            *dll_elem = NULL;
            sts = -2;
        }
     }

EXIT:
     return sts;

}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  UTL_DllNextLe
 *
 * DESCRIPTION :  return the next element in the absolute linked list.
 *
 * COMPLETION
 * STATUS      :  0

 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  UTL_DllNextLe(DLL_D_HDR   *dll_elem,DLL_D_HDR   **next_elem )
{
     DLL_D_HDR   *le_ptr;
     MSG_ID_T    sts = 0;

     if (dll_elem == DLL_NULL || next_elem==DLL_NULL)
     {
        sts = -1;
        IENT_LOG_ERROR("arguments validations is NULL\n") ;
        goto EXIT;
     }
     else
     {
        le_ptr = dll_elem->fw_ptr;
        if (le_ptr == dll_elem)
        {
            *next_elem = NULL;
            sts = -2;
        }
        else
            *next_elem = le_ptr;
     }

EXIT:
      return sts;

 }
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :  UTL_DllPrevLe
 *
 * DESCRIPTION :  return the previous element in absolute linked list.
 *
 * COMPLETION
 * STATUS      :  0
 *
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  UTL_DllPrevLe(DLL_D_HDR   *dll_elem,DLL_D_HDR   **prev_elem)
{
     DLL_D_HDR  *le_ptr;
     MSG_ID_T   sts = 0;

     if (dll_elem == DLL_NULL || prev_elem == DLL_NULL)
     {
        sts = -1;
        IENT_LOG_ERROR("arguments validations is NULL\n") ;
        goto EXIT;
     }
     else
     {
        le_ptr = dll_elem->bw_ptr;
        if( le_ptr == dll_elem )
        {
            *prev_elem = NULL;
            sts = -2;
        }
        else
            *prev_elem = le_ptr;
     }

EXIT:
     return sts;
 }
