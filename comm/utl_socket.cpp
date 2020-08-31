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
#include <Winsock2.h>
#elif defined(__linux__)
#include <sys/socket.h>
#include <stdlib.h>	 /* Required by getenv()			     */
#include <errno.h>
#include <sys/types.h>	 /* Required by socket.h			     */
#include <unistd.h>
#include <string.h>
#endif
#include "ient_comm.h"
#include "ent_utility.h"

static bool sUtlInitFlag  = false;
static bool sUtlRetryFlag = true;

#ifdef WIN32
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_SocketInit
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
MSG_ID_T UTL_SocketInit()
{
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;
    
    if(sUtlInitFlag)
    {
        return 0;
    }

    sUtlInitFlag = true;
    wVersionRequested = MAKEWORD( 2, 2 );
     
    err = WSAStartup( wVersionRequested, &wsaData );
    if ( err != 0 ) {
        /* Tell the user that we could not find a usable */
        /* WinSock DLL.                                  */
        IENT_LOG_ERROR("WSAStartup failed,error [%d]\n",GetLastError());
        sUtlInitFlag = false;
        return -1;
    }
     
    /* Confirm that the WinSock DLL supports 2.2.*/
    /* Note that if the DLL supports versions greater    */
    /* than 2.2 in addition to 2.2, it will still return */
    /* 2.2 in wVersion since that is the version we      */
    /* requested.                                        */
     
    if ( LOBYTE( wsaData.wVersion ) != 2 ||
            HIBYTE( wsaData.wVersion ) != 2 ) {
        /* Tell the user that we could not find a usable */
        /* WinSock DLL.                                  */
        WSACleanup( );
        IENT_LOG_ERROR("win sock do not support 2.2\n");
        sUtlInitFlag = false;
        return -2; 
    }
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Socket
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
MSG_ID_T  UTL_Socket(
    int           AddrFamily,	  
    int           SocketType,	  
    int		      Protocol,	  
    UTL_D_SOCKET *pSocketDesc)
{
    MSG_ID_T  sts = 0;	       

    if (!sUtlInitFlag)
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ( (*pSocketDesc = socket (AddrFamily,SocketType,Protocol)) == INVALID_SOCKET )
	&& ( (WSAGetLastError() == WSAEINTR) && sUtlRetryFlag )  );

    if ( *pSocketDesc == INVALID_SOCKET )
    {
        sts = -2;
        IENT_LOG_ERROR("socket failed,error [%d].\n",WSAGetLastError());
    }

    return sts;
} 
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Bind
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
MSG_ID_T	UTL_Bind(
 UTL_D_SOCKET  SockDesc, 
 struct sockaddr* addr,
 int              addr_size)
{	 
    MSG_ID_T	    sts = 0;	     	          
    int     	    stat;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }
    if (addr == NULL )
    {
        IENT_LOG_ERROR("unvalid args.\n");
        return -2;
    }

	while ( ( (stat = bind (
	    SockDesc,
	    addr,
	    addr_size)) == SOCKET_ERROR )
	    && ( (WSAGetLastError() == WSAEINTR) && sUtlRetryFlag )  );

	if ( stat == SOCKET_ERROR )
    {
        sts = -3;
        IENT_LOG_ERROR("bind failed,error [%d].\n",WSAGetLastError());
    }

    return sts;
} 
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Connect
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
MSG_ID_T  UTL_Connect(
 UTL_D_SOCKET	        SocketDesc,	 
 const struct sockaddr* addr,
 int                    addr_size)
{
    MSG_ID_T	     sts = 0;	       
    int      	     stat;
    DWORD		     winerr;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }
    if (addr == NULL )
    {
        IENT_LOG_ERROR("unvalid args.\n");
        return -2;
    }

	while ( (stat = connect (
	    SocketDesc,
	    addr,
	    addr_size)) == SOCKET_ERROR ){
	    winerr = WSAGetLastError();
	    if ( (winerr != WSAEINTR) || (!sUtlRetryFlag) )
		    break;
	}

	if ( stat == SOCKET_ERROR )
    {
        sts = -3;
        IENT_LOG_ERROR("connect failed,error [%d].\n",WSAGetLastError());
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Listen
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
MSG_ID_T  UTL_Listen(
 UTL_D_SOCKET	SocketDesc, 
 int 		    MaxBacklog) 
{

    MSG_ID_T  sts = 0;	       
    int       stat;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ( (stat = listen (SocketDesc,MaxBacklog)) == SOCKET_ERROR )
	&& ( (WSAGetLastError() == WSAEINTR) && sUtlRetryFlag )  );

    if ( stat == SOCKET_ERROR )
    {
        sts = -3;
        IENT_LOG_ERROR("listen failed,error [%d].\n",WSAGetLastError());
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Accept
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
MSG_ID_T  UTL_Accept(
 UTL_D_SOCKET	        SocketListen,	 
 const struct sockaddr* addr,
 int                    addr_size) 

{
    MSG_ID_T	    sts = 0;	    
    struct sockaddr Sockaddr;	
    int     	    SockaddrLen;
    int             ret;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    SockaddrLen= sizeof(struct sockaddr);
    while ( ( ( ret = accept(
	SocketListen,
	&Sockaddr,
	&SockaddrLen) ) ==  INVALID_SOCKET )
	&& ( (WSAGetLastError() == WSAEINTR) && sUtlRetryFlag )  );

    if ( ret == INVALID_SOCKET )
    {
        sts = -3;
        IENT_LOG_ERROR("accpet failed,error [%d].\n",WSAGetLastError());
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_CloseSocket
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
MSG_ID_T  UTL_CloseSocket(
    UTL_D_SOCKET  SocketDesc) 
{
    MSG_ID_T  sts = 0;	       
    int       stat;		       

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ( (stat = closesocket (SocketDesc)) == SOCKET_ERROR )
	&& ( (WSAGetLastError() == WSAEINTR) && sUtlRetryFlag )  );

    if ( stat == SOCKET_ERROR )
    {
        sts = -3;
        IENT_LOG_ERROR("closesocket failed,error [%d].\n",WSAGetLastError());
    }
	
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Recv
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
MSG_ID_T  UTL_Recv(
    UTL_D_SOCKET  SocketDesc,  
    char		 *pBuffer,	       
    int		      BufferLength,	   
    int 		  Flags,	       
    int		     *pBytesRecvd)	   
{

    MSG_ID_T  sts = 0;	       
    int       BytesRecvd;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ((BytesRecvd=recv (
	        SocketDesc,
	        pBuffer,
	        BufferLength,
	        Flags )) < 0 )
	        && ( (WSAGetLastError() == WSAEINTR) && sUtlRetryFlag )  );

    if (BytesRecvd == SOCKET_ERROR )
    {
        sts = -3;
        IENT_LOG_ERROR("recv failed,error [%d].\n",WSAGetLastError());
    }
    else
    {
        *pBytesRecvd = BytesRecvd;
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Send
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
MSG_ID_T  UTL_Send(
    UTL_D_SOCKET   SocketDesc,	 
    char          *pBuffer,	 
    int            BufferLength,
    int            Flags,		 
    int           *pBytesSent)	 
{
    MSG_ID_T  sts = 0;	       
    int     TempBytesSent;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ((TempBytesSent=send (
	SocketDesc,
	pBuffer,
	BufferLength,
	Flags)) < 0 )
	&& ( (WSAGetLastError() == WSAEINTR) && sUtlRetryFlag ) );

    if ( TempBytesSent == SOCKET_ERROR )
    {
        sts = -3;
        IENT_LOG_ERROR("send failed,error [%d].\n",WSAGetLastError());
    }
    else
    {
        *pBytesSent = TempBytesSent;
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_SetSockOpt
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
MSG_ID_T  UTL_SetSockOpt(
    UTL_D_SOCKET	SocketDesc,	 
    int 		    ProtocolLevel,	 
    int 		    OptionName,	    
    char		   *pOptionValue,	
    int 		    OptionLength)	
{
    MSG_ID_T  sts = 0;	       /* Completion status			       */
    int       stat;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ((stat =  setsockopt (
	SocketDesc,
	ProtocolLevel,
	OptionName,
	pOptionValue,
	OptionLength )) < 0 )
	&& ( (WSAGetLastError() == WSAEINTR) && sUtlRetryFlag )  );

    if ( stat < 0 )
    {
        sts = -3;
        IENT_LOG_ERROR("setsockopt failed,error [%d].\n",WSAGetLastError());
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Shutdown
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
MSG_ID_T  UTL_Shutdown(
    UTL_D_SOCKET  SocketDesc,	 
    int    	   HowToShutdown)	 
{
    MSG_ID_T  sts = 0;	      
    int       stat;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ((stat = shutdown (
	SocketDesc,
	HowToShutdown)) < 0)
	&& ( (WSAGetLastError() == WSAEINTR) && sUtlRetryFlag )  );

    if ( stat < 0 )
    {
        sts = -3;
        IENT_LOG_ERROR("setsockopt failed,error [%d].\n",WSAGetLastError());
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_GetSockOpt
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
MSG_ID_T  UTL_GetSockOpt(
    UTL_D_SOCKET SocketDesc,	  
    int          ProtocolLevel,	  
    int          OptionName,	      
    char        *pOptionValue,	  
    int	        *pOptionLength)	  
{
    MSG_ID_T  sts = 0;	       
    int       stat;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    if ( ( (stat = getsockopt (
	     SocketDesc,
	     ProtocolLevel,
	     OptionName,
	     pOptionValue,
	     pOptionLength)) < 0 )
	     && ( (WSAGetLastError() == WSAEINTR) && sUtlRetryFlag )  )
    {
    }

    if (stat < 0)
    {
        sts = -3;
        IENT_LOG_ERROR("getsockopt failed,error [%d].\n",WSAGetLastError());
    }

    return sts;
}
#else
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_SocketInit
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
MSG_ID_T UTL_SocketInit()
{   
    if(sUtlInitFlag)
    {
        return 0;
    }

    sUtlInitFlag = true;
    return 0;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Socket
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
MSG_ID_T  UTL_Socket(
    int           AddrFamily,	  
    int           SocketType,	  
    int		      Protocol,	  
    UTL_D_SOCKET *pSocketDesc)
{
    MSG_ID_T  sts = 0;	       

    if (!sUtlInitFlag)
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ( (*pSocketDesc = socket(AddrFamily,SocketType,Protocol)) < 0 )
	&& ( (errno == EINTR) && sUtlRetryFlag )  );

    if ( *pSocketDesc < 0 )
    {
        sts = -2;
        IENT_LOG_ERROR("socket failed,error [%d]->[%s].\n",errno,strerror(errno));
    }

    return sts;
} 
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Bind
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
MSG_ID_T	UTL_Bind(
 UTL_D_SOCKET  SockDesc, 
 struct sockaddr* addr,
 int              addr_size)
{	 
    MSG_ID_T	    sts = 0;	     	          
    int     	    stat;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }
    if (addr == NULL )
    {
        IENT_LOG_ERROR("unvalid args.\n");
        return -2;
    }

	while ( ( (stat = bind (
	    SockDesc,
	    addr,
	    addr_size)) < 0 )
	    && ( (errno == EINTR) && sUtlRetryFlag )  );

	if ( stat <0 )
    {
        sts = -3;
        IENT_LOG_ERROR("bind failed,error [%d]->[%s].\n",errno,strerror(errno));
    }

    return sts;
} 
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Connect
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
MSG_ID_T  UTL_Connect(
 UTL_D_SOCKET	        SocketDesc,	 
 const struct sockaddr* addr,
 int                    addr_size)
{
    MSG_ID_T	     sts = 0;	       
    int      	     stat;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }
    if (addr == NULL )
    {
        IENT_LOG_ERROR("unvalid args.\n");
        return -2;
    }

	while ( (stat = connect (
	    SocketDesc,
	    addr,
	    addr_size)) < 0 )
    {
	    if ( (errno == EINTR) && sUtlRetryFlag )
		    continue;
	}

	if ( stat < 0 )
    {
        sts = -3;
        IENT_LOG_ERROR("connect failed,error [%d]->[%s].\n",errno,strerror(errno));
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Listen
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
MSG_ID_T  UTL_Listen(
 UTL_D_SOCKET	SocketDesc, 
 int 		    MaxBacklog) 
{

    MSG_ID_T  sts = 0;	       
    int       stat;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ( (stat = listen (SocketDesc,MaxBacklog)) < 0 )
	&& ( (errno == EINTR) && sUtlRetryFlag )  );

    if ( stat < 0 )
    {
        sts = -3;
        IENT_LOG_ERROR("listen failed,error [%d]->[%s].\n",errno,strerror(errno));
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Accept
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
MSG_ID_T  UTL_Accept(
 UTL_D_SOCKET	        SocketListen,	 
 const struct sockaddr* addr,
 int                    addr_size) 

{
    MSG_ID_T	    sts = 0;	    
    struct sockaddr Sockaddr;	
    socklen_t     	SockaddrLen;
    int             ret;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    SockaddrLen= sizeof(struct sockaddr);
    while ( ( ( ret = accept (
	SocketListen,
	&Sockaddr,
	&SockaddrLen) ) < 0 )
	&& ( (errno == EINTR) && sUtlRetryFlag )  );

    if ( ret < 0 )
    {
        sts = -3;
        IENT_LOG_ERROR("accpet failed,error [%d]->[%s].\n",errno,strerror(errno));
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_CloseSocket
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
MSG_ID_T  UTL_CloseSocket(
    UTL_D_SOCKET  SocketDesc) 
{
    MSG_ID_T  sts = 0;	       
    int       stat;		       

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ( (stat = close(SocketDesc)) < 0 )
	&& ( (errno == EINTR) && sUtlRetryFlag )  );

    if ( stat < 0 )
    {
        sts = -3;
        IENT_LOG_ERROR("closesocket failed,error [%d]->[%s].\n",errno,strerror(errno));
    }
	
    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Recv
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
MSG_ID_T  UTL_Recv(
    UTL_D_SOCKET  SocketDesc,  
    char		 *pBuffer,	       
    int		      BufferLength,	   
    int 		  Flags,	       
    int		     *pBytesRecvd)	   
{

    MSG_ID_T  sts = 0;	       
    int       BytesRecvd;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ((BytesRecvd=recv (
	        SocketDesc,
	        pBuffer,
	        BufferLength,
	        Flags )) < 0 )
	        && ( (errno == EINTR) && sUtlRetryFlag )  );

    if (BytesRecvd < 0 )
    {
        sts = -3;
        IENT_LOG_ERROR("recv failed,error [%d]->[%s].\n",errno,strerror(errno));
    }
    else
    {
        *pBytesRecvd = BytesRecvd;
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Send
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
MSG_ID_T  UTL_Send(
    UTL_D_SOCKET   SocketDesc,	 
    char          *pBuffer,	 
    int            BufferLength,
    int            Flags,		 
    int           *pBytesSent)	 
{
    MSG_ID_T  sts = 0;	       
    int     TempBytesSent;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ((TempBytesSent=send (
	SocketDesc,
	pBuffer,
	BufferLength,
	Flags)) < 0 )
	&& ( (errno == EINTR) && sUtlRetryFlag ) );

    if ( TempBytesSent < 0 )
    {
        sts = -3;
        IENT_LOG_ERROR("send failed,error [%d]->[%s].\n",errno,strerror(errno));
    }
    else
    {
        *pBytesSent = TempBytesSent;
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_SetSockOpt
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
MSG_ID_T  UTL_SetSockOpt(
    UTL_D_SOCKET	SocketDesc,	 
    int 		    ProtocolLevel,	 
    int 		    OptionName,	    
    char		   *pOptionValue,	
    int 		    OptionLength)	
{
    MSG_ID_T  sts = 0;	       /* Completion status			       */
    int       stat;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ((stat =  setsockopt (
	SocketDesc,
	ProtocolLevel,
	OptionName,
	pOptionValue,
	OptionLength )) < 0 )
	&& ( (errno == EINTR) && sUtlRetryFlag )  );

    if ( stat < 0 )
    {
        sts = -3;
        IENT_LOG_ERROR("setsockopt failed,error [%d]->[%s].\n",errno,strerror(errno));
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_Shutdown
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
MSG_ID_T  UTL_Shutdown(
    UTL_D_SOCKET  SocketDesc,	 
    int    	   HowToShutdown)	 
{
    MSG_ID_T  sts = 0;	      
    int       stat;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    while ( ((stat = shutdown (
	SocketDesc,
	HowToShutdown)) < 0)
	&& ( (errno == EINTR) && sUtlRetryFlag )  );

    if ( stat < 0 )
    {
        sts = -3;
        IENT_LOG_ERROR("setsockopt failed,error [%d]->[%s].\n",errno,strerror(errno));
    }

    return sts;
}
/*+++++++++++++++++++++++++ FUNCTION DESCRIPTION ++++++++++++++++++++++++++++++
 *
 * NAME        :UTL_GetSockOpt
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
MSG_ID_T  UTL_GetSockOpt(
    UTL_D_SOCKET SocketDesc,	  
    int          ProtocolLevel,	  
    int          OptionName,	      
    char        *pOptionValue,	  
    int	        *pOptionLength)	  
{
    MSG_ID_T  sts = 0;	       
    int       stat;

    if (!sUtlInitFlag) 
    {
        IENT_LOG_ERROR("unintilized.\n");
        return -1;
    }

    if ( ( (stat = getsockopt(
	     SocketDesc,
	     ProtocolLevel,
	     OptionName,
	     pOptionValue,
	     (socklen_t*)pOptionLength)) < 0 )
	     && ( (errno == EINTR) && sUtlRetryFlag )  )
    {
    }

    if (stat < 0)
    {
        sts = -3;
        IENT_LOG_ERROR("getsockopt failed,error [%d]->[%s].\n",errno,strerror(errno));
    }

    return sts;
}
#endif