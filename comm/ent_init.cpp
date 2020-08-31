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
#ifdef WIN32
#pragma warning(disable : 4996)
#endif
#include <stdio.h>
#include "ient_comm.h"
#include "ent_init.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;
static inline void iENT_CTXFree(ENT_CTX* ctx)
{
    if(ctx == NULL)
        return;

    if(ctx->entName)
    {
        free(ctx->entName);
        ctx->entName = NULL;
    }

    if(ctx->workPath)
    {
        free(ctx->workPath);
        ctx->workPath = NULL;
    }

    if(ctx->logName)
    {
        free(ctx->logName);
        ctx->logName = NULL;
    }

    if(ctx->logPath)
    {
        free(ctx->logPath);
        ctx->logPath = NULL;
    }
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_Init
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  ENT_Init(const char* name,const char* workPath,ENT_LOG_LEV_E logLevel)
{
    MSG_ID_T  sts=0;
    if(gEntCtx.isInit)
    {
        fprintf(stderr,"ENT_Init already init.\n");
        return 1;
    }
    if(name==NULL||workPath==NULL)
    {
        fprintf(stderr,"ENT_Init arguments invalid.\n");
        return -1;
    }

    gEntCtx.workPath = strdup(workPath);
    gEntCtx.entName  = strdup(name);

    int    len     = strlen(workPath);
    int    size    = len+4+1+1;//
    char*  tmpStr  = (char*)malloc(size);
    if(tmpStr == NULL)
    {
        fprintf(stderr,"malloc size [%d] failed.\n",size);
        iENT_CTXFree(&gEntCtx);
        return -2;
    }
#ifdef WIN32
    if(workPath[len-1]=='\\'||workPath[len-1]=='/')
#else
    if(workPath[len-1]=='/')
#endif
    {
        sprintf(tmpStr,"%slog",workPath);
    }
    else
    {
        sprintf(tmpStr,"%s%slog",ENT_FILE_SEP,workPath);
    }
    gEntCtx.logPath = tmpStr;

    len    = strlen(name);
    size   = 4+len+1;
    tmpStr = (char*)malloc(size);
    if(tmpStr == NULL)
    {
        fprintf(stderr,"malloc size [%d] failed.\n",size);
        iENT_CTXFree(&gEntCtx);
        return -3;
    }
    sprintf(tmpStr,"ent_%s",name);
    gEntCtx.logName = tmpStr;

    sts = ENT_LogInit();
    if(sts<0)
    {
        fprintf(stderr,"ENT_LogInit failed,sts [%d].\n",sts);
        iENT_CTXFree(&gEntCtx);
        return -4;
    }

    sts = ENT_LogInitHandle(&gEntCtx.entLog,gEntCtx.logName,gEntCtx.logPath);
    if(sts<0)
    {
        fprintf(stderr,"ENT_LogInitHandle failed,sts [%d].\n",sts);
        iENT_CTXFree(&gEntCtx);
        return -5;
    }
    sts = ENT_LogSetOption(NULL,ENT_LOG_LEVEL_E,&logLevel);
    if(sts < 0)
    {
        fprintf(stderr,"ENT_LogSetOption failed,sts [%d].\n",sts);
        iENT_CTXFree(&gEntCtx);
        return -6;
    }

    sts = UTL_LockInit(&gEntCtx.entLock,"ent");
    if(sts<0)
    {
        iENT_CTXFree(&gEntCtx);
        IENT_LOG_ERROR("UTL_LockInit failed,sts [%d]\n",sts);
        return -7;
    }

    sts = UTL_CVInit(&gEntCtx.entCV,"ent");
    if(sts<0)
    {
        iENT_CTXFree(&gEntCtx);
        UTL_LockClose(gEntCtx.entLock);
        IENT_LOG_ERROR("UTL_LockInit failed,sts [%d]\n",sts);
        return -8;
    }

    gEntCtx.isInit = true;

    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_Close
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  ENT_Close()
{
    MSG_ID_T  sts = 0;
    if(gEntCtx.isInit)
    {
        return 1;
    }

    sts = UTL_CVClose(gEntCtx.entCV);

    sts = UTL_LockClose(gEntCtx.entLock);

    sts = ENT_LogCloseHandle(gEntCtx.entLog);

    sts = ENT_LogClose();

    iENT_CTXFree(&gEntCtx);
    gEntCtx.isInit =false;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_Run
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  ENT_Run()
{
    while(1)
    {
        UTL_LockEnter(gEntCtx.entLock);
        UTL_CVWait(gEntCtx.entLock,gEntCtx.entCV,0,RW_WRITE_E);
        UTL_LockLeave(gEntCtx.entLock);
        break;
    }
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :ENT_Helpers
 *
 * DESCRIPTION :   
 *                 
 *                   
 *
 * COMPLETION
 * STATUS      :  0
 *                Success; Service has completed successfully.           
 *
 *                            
 *
 *-----------------------------------------------------------------------------
 */
MSG_ID_T  ENT_Helpers()
{
    return 0;
}



