#ifndef  _ENT_THREAD_H_
#define  _ENT_THREAD_H_

#include "ent_comm.h"
#ifdef WIN32
#include "windows.h"
#elif defined(__linux__)

#endif

typedef void* ENT_THREAD;
typedef void* ENT_THREAD_ID;

#ifdef __linux__
typedef void* ( *PTHREAD_START_ROUTINE)(void* );
#endif

#ifdef __cplusplus
extern "C" {
#endif

ENT_PUBLIC MSG_ID_T ENT_ThreadInit(ENT_THREAD* pthHandle);

ENT_PUBLIC MSG_ID_T ENT_ThreadDetachCreate(ENT_THREAD handle,PTHREAD_START_ROUTINE thProc,void* thData);

ENT_PUBLIC MSG_ID_T ENT_ThreadCreate(ENT_THREAD_ID* tid,ENT_THREAD handle,PTHREAD_START_ROUTINE thProc,void* thData);

ENT_PUBLIC MSG_ID_T ENT_ThreadWaitById(ENT_THREAD_ID* tid,ENT_THREAD handle,int ms);

ENT_PUBLIC MSG_ID_T ENT_ThreadClose(ENT_THREAD handle);

#ifdef __cplusplus
}
#endif

#endif