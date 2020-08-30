#ifndef _ENT_UTILITY_H_
#define _ENT_UTILITY_H_

#include "ent_comm.h"
#include "ent_types.h"

#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <linux/limits.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif

typedef void*  UTL_LOCK;
typedef void*  UTL_CV;

typedef enum 
{
    LOCK_MUTEX_E = 0,
    LOCK_RW_E,
    LOCK_SPIN_E
}UTL_LOCK_TYPE_T;

typedef enum 
{
    RW_WRITE_E,
    RW_READ_E
}UTL_LOCK_RW_TYPE_T;

ENT_PUBLIC MSG_ID_T  UTL_LockInit(UTL_LOCK* lock,const char* name);
ENT_PUBLIC MSG_ID_T  UTL_LockInitEx(UTL_LOCK* lock,const char* name,UTL_LOCK_TYPE_T type);
ENT_PUBLIC MSG_ID_T  UTL_LockEnter(UTL_LOCK lock);
ENT_PUBLIC MSG_ID_T  UTL_LockEnterEx(UTL_LOCK lock,UTL_LOCK_RW_TYPE_T rwType);
ENT_PUBLIC MSG_ID_T  UTL_LockLeave(UTL_LOCK lock);
ENT_PUBLIC MSG_ID_T  UTL_LockLeaveEx(UTL_LOCK lock,UTL_LOCK_RW_TYPE_T rwType);
ENT_PUBLIC MSG_ID_T  UTL_LockClose(UTL_LOCK lock);

ENT_PUBLIC MSG_ID_T  UTL_CVInit(UTL_CV* cv,const char* name);
ENT_PUBLIC MSG_ID_T  UTL_CVClose(UTL_CV cv);
ENT_PUBLIC MSG_ID_T  UTL_CVWait(UTL_CV cv,UTL_LOCK lock,int ms,UTL_LOCK_RW_TYPE_T rwType);
ENT_PUBLIC MSG_ID_T  UTL_CVWake(UTL_CV cv);
ENT_PUBLIC MSG_ID_T  UTL_CVWakeAll(UTL_CV cv);

ENT_PUBLIC MSG_ID_T  UTL_Sleep(int ms);

typedef struct dll_header
{
  struct dll_header  *fw_ptr;
  struct dll_header  *bw_ptr;
} DLL_D_HDR;

ENT_PUBLIC MSG_ID_T  UTL_DllIsEmpty(BOOL* isEmpty,const DLL_D_HDR *dll_hdr);
ENT_PUBLIC MSG_ID_T  UTL_DllInitHead(DLL_D_HDR *dll_hdr);
ENT_PUBLIC MSG_ID_T  UTL_DllInsHead(DLL_D_HDR *dll_hdr, DLL_D_HDR *dll_elem);
ENT_PUBLIC MSG_ID_T  UTL_DllInsCurr(DLL_D_HDR *dll_hdr, DLL_D_HDR *dll_elem);
ENT_PUBLIC MSG_ID_T  UTL_DllInsTail(DLL_D_HDR *dll_hdr, DLL_D_HDR *dll_elem);
ENT_PUBLIC MSG_ID_T  UTL_DllRemHead(DLL_D_HDR *dll_hdr, DLL_D_HDR **dll_elem);
ENT_PUBLIC MSG_ID_T  UTL_DllRemCurr(DLL_D_HDR *dll_hdr, DLL_D_HDR **dll_elem);
ENT_PUBLIC MSG_ID_T  UTL_DllRemTail(DLL_D_HDR *dll_hdr, DLL_D_HDR **dll_elem);
ENT_PUBLIC MSG_ID_T  UTL_DllNextLe(DLL_D_HDR *dll_elem, DLL_D_HDR **next_elem);
ENT_PUBLIC MSG_ID_T  UTL_DllPrevLe(DLL_D_HDR *dll_elem, DLL_D_HDR **prev_elem);


typedef void*  UTL_TPOOL;
typedef MSG_ID_T (*UTL_TP_TASK_F)(void*);
typedef MSG_ID_T (*UTL_TP_TASK_END_F)(void*,MSG_ID_T*);

ENT_PUBLIC MSG_ID_T  UTL_TPoolInit(UTL_TPOOL*  pool,int num);
ENT_PUBLIC MSG_ID_T  UTL_TPoolClose(UTL_TPOOL  pool);
ENT_PUBLIC MSG_ID_T  UTL_TPoolAddTask(UTL_TPOOL pool,UTL_TP_TASK_F taskCb,UTL_TP_TASK_END_F taskEndCb,void* taskData,MSG_ID_T* retVal);


typedef enum 
{
    UTL_TIMER_E_PERIOD = 0x00,
    UTL_TIMER_E_ONESHOT= 0x01,
    UTL_TIMER_E_SIGNAL = 0x0100,
    UTL_TIMER_E_THREAD = 0x0200,
}UTL_TIMER_TYPE_E;

typedef void*(*UTL_TIMER_EV_F)(void*);

typedef void* UTL_TIMER_T;

ENT_PUBLIC MSG_ID_T  UTL_TimerInit();
ENT_PUBLIC MSG_ID_T  UTL_TimerCreate(UTL_TIMER_T* pTimer,unsigned int type, int ms,UTL_TIMER_EV_F evCb,void* data);
ENT_PUBLIC MSG_ID_T  UTL_TimerDelete(UTL_TIMER_T* pTimer);
ENT_PUBLIC MSG_ID_T  UTL_TimerClose();

#ifdef WIN32
typedef SOCKET  UTL_D_SOCKET;
#else
typedef int     UTL_D_SOCKET;
#endif 

ENT_PUBLIC MSG_ID_T UTL_SocketInit();
ENT_PUBLIC MSG_ID_T  UTL_Socket(
    int           AddrFamily,	  
    int           SocketType,	  
    int		      Protocol,	  
    UTL_D_SOCKET *pSocketDesc);

ENT_PUBLIC MSG_ID_T	UTL_Bind(
    UTL_D_SOCKET  SockDesc, 
    struct sockaddr* addr,
    int              addr_size);

ENT_PUBLIC MSG_ID_T  UTL_Connect(
    UTL_D_SOCKET	        SocketDesc,	 
    const struct sockaddr* addr,
    int                    addr_size);

ENT_PUBLIC MSG_ID_T  UTL_Listen(
    UTL_D_SOCKET	SocketDesc, 
    int 		    MaxBacklog);

ENT_PUBLIC MSG_ID_T  UTL_Accept (
    UTL_D_SOCKET	        SocketListen,	 
    const struct sockaddr* addr,
    int                    addr_size);

ENT_PUBLIC MSG_ID_T  UTL_CloseSocket(
    UTL_D_SOCKET  SocketDesc);

ENT_PUBLIC MSG_ID_T  UTL_Recv(
    UTL_D_SOCKET  SocketDesc,  
    char		 *pBuffer,	       
    int		      BufferLength,	   
    int 		  Flags,	       
    int		     *pBytesRecvd);

ENT_PUBLIC MSG_ID_T  UTL_Send(
    UTL_D_SOCKET   SocketDesc,	 
    char          *pBuffer,	 
    int            BufferLength,
    int            Flags,		 
    int           *pBytesSent);

ENT_PUBLIC MSG_ID_T  UTL_SetSockOpt(
    UTL_D_SOCKET	SocketDesc,	 
    int 		    ProtocolLevel,	 
    int 		    OptionName,	    
    char		   *pOptionValue,	
    int 		    OptionLength);

ENT_PUBLIC MSG_ID_T  UTL_Shutdown(
    UTL_D_SOCKET  SocketDesc,	 
    int    	   HowToShutdown);

ENT_PUBLIC MSG_ID_T  UTL_GetSockOpt(
    UTL_D_SOCKET SocketDesc,	  
    int          ProtocolLevel,	  
    int          OptionName,	      
    char        *pOptionValue,	  
    int	        *pOptionLength);

#ifdef __cplusplus
}
#endif

#endif