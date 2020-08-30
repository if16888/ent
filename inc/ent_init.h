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