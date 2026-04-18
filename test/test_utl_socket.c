#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ent_msg.h"
#include "ent_utility.h"

ENT_CTX gEntCtx;

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }

    return 0;
}

MSG_ID_T ENT_LogInit(void)
{
    return 0;
}

MSG_ID_T ENT_LogClose(void)
{
    return 0;
}

MSG_ID_T ENT_LogInitHandle(ENT_LOG* pLogHandle, const char* moduleName, const char* logPath)
{
    (void)pLogHandle;
    (void)moduleName;
    (void)logPath;
    return 0;
}

MSG_ID_T ENT_LogSetOption(ENT_LOG logHandle, ENT_LOG_OPTIONS_E option, const void* arg)
{
    (void)logHandle;
    (void)option;
    (void)arg;
    return 0;
}

MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle)
{
    (void)logHandle;
    return 0;
}

MSG_ID_T ENT_LogRaw(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogFatal(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogError(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogWarn(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogPrint(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

MSG_ID_T ENT_LogDebug(ENT_LOG logHandle, const char* format, ...)
{
    (void)logHandle;
    (void)format;
    return 0;
}

static void close_native_socket(UTL_D_SOCKET socketDesc)
{
#ifdef WIN32
    if(socketDesc != INVALID_SOCKET)
    {
        closesocket(socketDesc);
    }
#else
    if(socketDesc >= 0)
    {
        close(socketDesc);
    }
#endif
}

static UTL_D_SOCKET accept_native_socket(UTL_D_SOCKET listenSocket,
                                         struct sockaddr* addr,
                                         int* addrLen)
{
#ifdef WIN32
    return accept(listenSocket, addr, addrLen);
#else
    socklen_t nativeAddrLen = (addrLen != NULL) ? (socklen_t)*addrLen : 0;
    UTL_D_SOCKET accepted = accept(listenSocket, addr, (addrLen != NULL) ? &nativeAddrLen : NULL);
    if(addrLen != NULL)
    {
        *addrLen = (int)nativeAddrLen;
    }
    return accepted;
#endif
}

static int get_socket_name(UTL_D_SOCKET socketDesc, struct sockaddr* addr, int* addrLen)
{
#ifdef WIN32
    return getsockname(socketDesc, addr, addrLen);
#else
    socklen_t nativeAddrLen = (socklen_t)*addrLen;
    int rc = getsockname(socketDesc, addr, &nativeAddrLen);
    *addrLen = (int)nativeAddrLen;
    return rc;
#endif
}

static int test_socket_rejects_uninitialized_use(void)
{
    UTL_D_SOCKET sock = -1;
    int bytes = 0;

    if(expect_true(UTL_Socket(AF_INET, SOCK_STREAM, 0, &sock) == ENT_SOCK_NOT_INITIALIZED,
                   "UTL_Socket should reject use before UTL_SocketInit") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_Send(sock, "x", 1, 0, &bytes) == ENT_SOCK_NOT_INITIALIZED,
                   "UTL_Send should reject use before UTL_SocketInit") != 0)
    {
        return 1;
    }

    return expect_true(UTL_CloseSocket(sock) == ENT_SOCK_NOT_INITIALIZED,
                       "UTL_CloseSocket should reject use before UTL_SocketInit");
}

static int test_socket_bind_and_connect_reject_null_addr(void)
{
    UTL_D_SOCKET sock = -1;

    if(expect_true(UTL_SocketInit() == 0, "UTL_SocketInit should initialize the socket subsystem") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_Socket(AF_INET, SOCK_STREAM, 0, &sock) == 0,
                   "UTL_Socket should create a TCP socket after initialization") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_Bind(sock, NULL, 0) == ENT_SOCK_BAD_ARGUMENT,
                   "UTL_Bind should reject a NULL address") != 0)
    {
        UTL_CloseSocket(sock);
        return 1;
    }

    if(expect_true(UTL_Connect(sock, NULL, 0) == ENT_SOCK_BAD_ARGUMENT,
                   "UTL_Connect should reject a NULL address") != 0)
    {
        UTL_CloseSocket(sock);
        return 1;
    }

    return expect_true(UTL_CloseSocket(sock) == 0, "UTL_CloseSocket should close a created socket");
}

static int test_socket_local_roundtrip_send_and_recv(void)
{
    UTL_D_SOCKET server = -1;
    UTL_D_SOCKET client = -1;
    UTL_D_SOCKET accepted = (UTL_D_SOCKET)-1;
    struct sockaddr_in addr;
    struct sockaddr_in acceptedAddr;
    int acceptedLen = (int)sizeof(acceptedAddr);
    int addrLen = (int)sizeof(addr);
    char recvBuf[32];
    char sendBuf[] = "ping";
    int sent = 0;
    int recvd = 0;

    memset(&addr, 0, sizeof(addr));
    memset(&acceptedAddr, 0, sizeof(acceptedAddr));
    memset(recvBuf, 0, sizeof(recvBuf));

    if(expect_true(UTL_SocketInit() == 0, "UTL_SocketInit should be idempotent") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_Socket(AF_INET, SOCK_STREAM, 0, &server) == 0,
                   "UTL_Socket should create a server socket") != 0)
    {
        return 1;
    }

    addr.sin_family = AF_INET;
#ifdef __APPLE__
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if(expect_true(UTL_Bind(server, (struct sockaddr*)&addr, sizeof(addr)) == 0,
                   "UTL_Bind should bind the server socket to localhost") != 0)
    {
        UTL_CloseSocket(server);
        return 1;
    }

    if(expect_true(UTL_Listen(server, 1) == 0, "UTL_Listen should put the server socket into listening mode") != 0)
    {
        UTL_CloseSocket(server);
        return 1;
    }

    if(get_socket_name(server, (struct sockaddr*)&addr, &addrLen) != 0)
    {
        perror("getsockname");
        UTL_CloseSocket(server);
        return 1;
    }

    if(expect_true(UTL_Socket(AF_INET, SOCK_STREAM, 0, &client) == 0,
                   "UTL_Socket should create a client socket") != 0)
    {
        UTL_CloseSocket(server);
        return 1;
    }

    if(expect_true(UTL_Connect(client, (struct sockaddr*)&addr, sizeof(addr)) == 0,
                   "UTL_Connect should connect to the listening localhost socket") != 0)
    {
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        return 1;
    }

    accepted = accept_native_socket(server, (struct sockaddr*)&acceptedAddr, &acceptedLen);
#ifdef WIN32
    if(expect_true(accepted != INVALID_SOCKET, "The server should accept the localhost client connection") != 0)
#else
    if(expect_true(accepted >= 0, "The server should accept the localhost client connection") != 0)
#endif
    {
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        return 1;
    }

    if(expect_true(UTL_Send(client, sendBuf, 4, 0, &sent) == 0,
                   "UTL_Send should send data to the connected peer") != 0)
    {
        close_native_socket(accepted);
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        return 1;
    }

    if(expect_true(sent == 4, "UTL_Send should report the sent byte count") != 0)
    {
        close_native_socket(accepted);
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        return 1;
    }

    if(expect_true(UTL_Recv(accepted, recvBuf, (int)sizeof(recvBuf), 0, &recvd) == 0,
                   "UTL_Recv should read data from the peer") != 0)
    {
        close_native_socket(accepted);
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        return 1;
    }

    if(expect_true(recvd == 4, "UTL_Recv should report the received byte count") != 0)
    {
        close_native_socket(accepted);
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        return 1;
    }

    if(expect_true(memcmp(recvBuf, "ping", 4) == 0, "UTL_Recv should preserve the sent payload") != 0)
    {
        close_native_socket(accepted);
        UTL_CloseSocket(client);
        UTL_CloseSocket(server);
        return 1;
    }

    close_native_socket(accepted);
    UTL_CloseSocket(client);
    return expect_true(UTL_CloseSocket(server) == 0, "UTL_CloseSocket should close the listening socket");
}

static int setup_loopback_tcp_pair(UTL_D_SOCKET* server,
                                   UTL_D_SOCKET* client,
                                   UTL_D_SOCKET* accepted,
                                   struct sockaddr_in* addr)
{
    int addrLen = (int)sizeof(*addr);

    *server = -1;
    *client = -1;
    *accepted = -1;
    memset(addr, 0, sizeof(*addr));

    if(expect_true(UTL_SocketInit() == 0, "UTL_SocketInit should initialize the socket subsystem") != 0)
    {
        return 1;
    }

    if(expect_true(UTL_Socket(AF_INET, SOCK_STREAM, 0, server) == 0,
                   "server socket create should succeed") != 0)
    {
        return 1;
    }

    addr->sin_family = AF_INET;
#ifdef __APPLE__
    addr->sin_len = sizeof(*addr);
#endif
    addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr->sin_port = 0;

    if(expect_true(UTL_Bind(*server, (struct sockaddr*)addr, sizeof(*addr)) == 0,
                   "bind should succeed") != 0)
    {
        UTL_CloseSocket(*server);
        *server = -1;
        return 1;
    }

    if(expect_true(UTL_Listen(*server, 1) == 0, "listen should succeed") != 0)
    {
        UTL_CloseSocket(*server);
        *server = -1;
        return 1;
    }

    if(get_socket_name(*server, (struct sockaddr*)addr, &addrLen) != 0)
    {
        perror("getsockname");
        UTL_CloseSocket(*server);
        *server = -1;
        return 1;
    }

    if(expect_true(UTL_Socket(AF_INET, SOCK_STREAM, 0, client) == 0,
                   "client socket create should succeed") != 0)
    {
        UTL_CloseSocket(*server);
        *server = -1;
        return 1;
    }

    if(expect_true(UTL_Connect(*client, (struct sockaddr*)addr, sizeof(*addr)) == 0,
                   "connect should succeed") != 0)
    {
        UTL_CloseSocket(*client);
        UTL_CloseSocket(*server);
        *client = -1;
        *server = -1;
        return 1;
    }

    *accepted = accept_native_socket(*server, NULL, NULL);
#ifdef WIN32
    if(expect_true(*accepted != INVALID_SOCKET, "accept should succeed") != 0)
#else
    if(expect_true(*accepted >= 0, "accept should succeed") != 0)
#endif
    {
        UTL_CloseSocket(*client);
        UTL_CloseSocket(*server);
        *client = -1;
        *server = -1;
        *accepted = (UTL_D_SOCKET)-1;
        return 1;
    }

    return 0;
}

static void cleanup_loopback_tcp_pair(UTL_D_SOCKET server, UTL_D_SOCKET client, UTL_D_SOCKET accepted)
{
    if(accepted != (UTL_D_SOCKET)-1)
    {
        close_native_socket(accepted);
    }

    if(client >= 0)
    {
        UTL_CloseSocket(client);
    }

    if(server >= 0)
    {
        UTL_CloseSocket(server);
    }
}

static int send_all_in_chunks(UTL_D_SOCKET socketDesc, char* buffer, int bufferLength)
{
    int totalSent = 0;

    while(totalSent < bufferLength)
    {
        int chunk = bufferLength - totalSent;
        int sent = 0;

        if(chunk > 4096)
        {
            chunk = 4096;
        }

        if(expect_true(UTL_Send(socketDesc, buffer + totalSent, chunk, 0, &sent) == 0,
                       "UTL_Send should transfer a chunk") != 0)
        {
            return 1;
        }

        if(expect_true(sent > 0, "UTL_Send should transfer at least one byte") != 0)
        {
            return 1;
        }

        totalSent += sent;
    }

    return 0;
}

static int recv_all_in_chunks(UTL_D_SOCKET socketDesc, char* buffer, int bufferLength)
{
    int totalRecv = 0;

    while(totalRecv < bufferLength)
    {
        int chunk = bufferLength - totalRecv;
        int recvd = 0;

        if(chunk > 4096)
        {
            chunk = 4096;
        }

        if(expect_true(UTL_Recv(socketDesc, buffer + totalRecv, chunk, 0, &recvd) == 0,
                       "UTL_Recv should transfer a chunk") != 0)
        {
            return 1;
        }

        if(expect_true(recvd > 0, "UTL_Recv should transfer at least one byte") != 0)
        {
            return 1;
        }

        totalRecv += recvd;
    }

    return 0;
}

static int test_socket_reports_peer_close_without_crashing(void)
{
    UTL_D_SOCKET server = -1;
    UTL_D_SOCKET client = -1;
    UTL_D_SOCKET accepted = (UTL_D_SOCKET)-1;
    struct sockaddr_in addr;
    char recvBuf[16];
    int recvBytes = -1;

    memset(recvBuf, 0, sizeof(recvBuf));

    if(setup_loopback_tcp_pair(&server, &client, &accepted, &addr) != 0)
    {
        cleanup_loopback_tcp_pair(server, client, accepted);
        return 1;
    }

    close_native_socket(accepted);
    accepted = (UTL_D_SOCKET)-1;

    if(expect_true(UTL_Recv(client, recvBuf, (int)sizeof(recvBuf), 0, &recvBytes) == 0,
                   "recv after peer close should succeed cleanly") != 0)
    {
        cleanup_loopback_tcp_pair(server, client, accepted);
        return 1;
    }

    if(expect_true(recvBytes == 0, "recv after peer close should report EOF") != 0)
    {
        cleanup_loopback_tcp_pair(server, client, accepted);
        return 1;
    }

    cleanup_loopback_tcp_pair(server, client, accepted);
    return 0;
}

static int test_socket_large_payload_roundtrip(void)
{
    UTL_D_SOCKET server = -1;
    UTL_D_SOCKET client = -1;
    UTL_D_SOCKET accepted = (UTL_D_SOCKET)-1;
    struct sockaddr_in addr;
    enum { PAYLOAD_SIZE = 65536 };
    char* sendBuf = NULL;
    char* recvBuf = NULL;
    int recvd = 0;

    sendBuf = (char*)malloc(PAYLOAD_SIZE);
    recvBuf = (char*)malloc(PAYLOAD_SIZE);
    if(expect_true(sendBuf != NULL && recvBuf != NULL, "payload buffers should allocate") != 0)
    {
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    for(int i = 0; i < PAYLOAD_SIZE; ++i)
    {
        sendBuf[i] = (char)(i % 251);
        recvBuf[i] = 0;
    }

    if(setup_loopback_tcp_pair(&server, &client, &accepted, &addr) != 0)
    {
        cleanup_loopback_tcp_pair(server, client, accepted);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    if(send_all_in_chunks(client, sendBuf, PAYLOAD_SIZE) != 0)
    {
        cleanup_loopback_tcp_pair(server, client, accepted);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    if(recv_all_in_chunks(accepted, recvBuf, PAYLOAD_SIZE) != 0)
    {
        cleanup_loopback_tcp_pair(server, client, accepted);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    if(expect_true(memcmp(sendBuf, recvBuf, PAYLOAD_SIZE) == 0,
                   "server should receive the full payload without corruption") != 0)
    {
        cleanup_loopback_tcp_pair(server, client, accepted);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    if(send_all_in_chunks(accepted, recvBuf, PAYLOAD_SIZE) != 0)
    {
        cleanup_loopback_tcp_pair(server, client, accepted);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    memset(sendBuf, 0, PAYLOAD_SIZE);
    if(recv_all_in_chunks(client, sendBuf, PAYLOAD_SIZE) != 0)
    {
        cleanup_loopback_tcp_pair(server, client, accepted);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    if(expect_true(memcmp(sendBuf, recvBuf, PAYLOAD_SIZE) == 0,
                   "client should receive the echoed payload without corruption") != 0)
    {
        cleanup_loopback_tcp_pair(server, client, accepted);
        free(sendBuf);
        free(recvBuf);
        return 1;
    }

    cleanup_loopback_tcp_pair(server, client, accepted);
    free(sendBuf);
    free(recvBuf);
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_socket_rejects_uninitialized_use();
    failures += test_socket_bind_and_connect_reject_null_addr();
    failures += test_socket_local_roundtrip_send_and_recv();
    failures += test_socket_reports_peer_close_without_crashing();
    failures += test_socket_large_payload_roundtrip();

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
