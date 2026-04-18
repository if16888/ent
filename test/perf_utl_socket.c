#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "ient_comm.h"
#include "ient_runtime.h"
#include "ent_utility.h"

#define FAIL_STEP(step)                         \
    do                                         \
    {                                          \
        fprintf(stderr, "%s errno=%d\n", step, errno); \
        return EXIT_FAILURE;                   \
    } while(0)

ENT_CTX gEntCtx;

MSG_ID_T ENT_LogInit(void) { return 0; }
MSG_ID_T ENT_LogClose(void) { return 0; }
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
MSG_ID_T ENT_LogCloseHandle(ENT_LOG logHandle) { (void)logHandle; return 0; }
MSG_ID_T ENT_LogRaw(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogFatal(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogError(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogWarn(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogPrint(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }
MSG_ID_T ENT_LogDebug(ENT_LOG logHandle, const char* format, ...) { (void)logHandle; (void)format; return 0; }

typedef struct
{
    UTL_D_SOCKET socket_desc;
    int total_bytes;
    int received_bytes;
#ifdef WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
} SOCKET_RECV_CTX;

static double now_ms(void)
{
#ifdef WIN32
    static LARGE_INTEGER frequency;
    static int frequency_initialized = 0;
    LARGE_INTEGER counter;

    if(!frequency_initialized)
    {
        QueryPerformanceFrequency(&frequency);
        frequency_initialized = 1;
    }

    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
#endif
}

static void close_native_socket(UTL_D_SOCKET socket_desc)
{
#ifdef WIN32
    if(socket_desc != INVALID_SOCKET)
    {
        closesocket(socket_desc);
    }
#else
    if(socket_desc >= 0)
    {
        close(socket_desc);
    }
#endif
}

static UTL_D_SOCKET accept_native_socket(UTL_D_SOCKET listen_socket,
                                         struct sockaddr* addr,
                                         int* addr_len)
{
#ifdef WIN32
    return accept(listen_socket, addr, addr_len);
#else
    socklen_t native_addr_len = (addr_len != NULL) ? (socklen_t)*addr_len : 0;
    UTL_D_SOCKET accepted = accept(listen_socket, addr, (addr_len != NULL) ? &native_addr_len : NULL);
    if(addr_len != NULL)
    {
        *addr_len = (int)native_addr_len;
    }
    return accepted;
#endif
}

static int get_socket_name(UTL_D_SOCKET socket_desc, struct sockaddr* addr, int* addr_len)
{
#ifdef WIN32
    return getsockname(socket_desc, addr, addr_len);
#else
    socklen_t native_addr_len = (socklen_t)*addr_len;
    int rc = getsockname(socket_desc, addr, &native_addr_len);
    *addr_len = (int)native_addr_len;
    return rc;
#endif
}

static int send_all_in_chunks(UTL_D_SOCKET socket_desc, char* buffer, int buffer_length)
{
    int total_sent = 0;

    while(total_sent < buffer_length)
    {
        int sent = 0;
        int chunk = buffer_length - total_sent;

        if(chunk > 4096)
        {
            chunk = 4096;
        }

        if(UTL_Send(socket_desc, buffer + total_sent, chunk, 0, &sent) != 0 || sent <= 0)
        {
            return -1;
        }
        total_sent += sent;
    }

    return 0;
}

static int recv_all_in_chunks(UTL_D_SOCKET socket_desc, int total_bytes)
{
    int total_recv = 0;
    char recv_buf[4096];

    while(total_recv < total_bytes)
    {
        int recvd = 0;
        int chunk = total_bytes - total_recv;

        if(chunk > (int)sizeof(recv_buf))
        {
            chunk = (int)sizeof(recv_buf);
        }

        if(UTL_Recv(socket_desc, recv_buf, chunk, 0, &recvd) != 0 || recvd <= 0)
        {
            return -1;
        }
        total_recv += recvd;
    }

    return 0;
}

#ifdef WIN32
static DWORD WINAPI recv_worker_main(LPVOID data)
#else
static void* recv_worker_main(void* data)
#endif
{
    SOCKET_RECV_CTX* ctx = (SOCKET_RECV_CTX*)data;

    if(recv_all_in_chunks(ctx->socket_desc, ctx->total_bytes) != 0)
    {
        ctx->received_bytes = -1;
    }
    else
    {
        ctx->received_bytes = ctx->total_bytes;
    }

#ifdef WIN32
    return 0;
#else
    return NULL;
#endif
}

static int setup_loopback_tcp_pair(UTL_D_SOCKET* server,
                                   UTL_D_SOCKET* client,
                                   UTL_D_SOCKET* accepted,
                                   struct sockaddr_in* addr)
{
    int addr_len = (int)sizeof(*addr);

    *server = (UTL_D_SOCKET)-1;
    *client = (UTL_D_SOCKET)-1;
    *accepted = (UTL_D_SOCKET)-1;
    memset(addr, 0, sizeof(*addr));

    if(UTL_SocketInit() != 0)
    {
        fprintf(stderr, "setup_loopback_tcp_pair: UTL_SocketInit failed\n");
        return -1;
    }
    if(UTL_Socket(AF_INET, SOCK_STREAM, 0, server) != 0)
    {
        fprintf(stderr, "setup_loopback_tcp_pair: UTL_Socket failed\n");
        return -1;
    }

    addr->sin_family = AF_INET;
#ifdef __APPLE__
    addr->sin_len = sizeof(*addr);
#endif
    addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr->sin_port = 0;

    if(UTL_Bind(*server, (struct sockaddr*)addr, sizeof(*addr)) != 0)
    {
        fprintf(stderr, "setup_loopback_tcp_pair: UTL_Bind failed\n");
        return -1;
    }
    if(UTL_Listen(*server, 1) != 0)
    {
        fprintf(stderr, "setup_loopback_tcp_pair: UTL_Listen failed\n");
        return -1;
    }
    if(get_socket_name(*server, (struct sockaddr*)addr, &addr_len) != 0)
    {
        perror("setup_loopback_tcp_pair: getsockname");
        return -1;
    }
    if(UTL_Socket(AF_INET, SOCK_STREAM, 0, client) != 0)
    {
        fprintf(stderr, "setup_loopback_tcp_pair: client UTL_Socket failed\n");
        return -1;
    }
    if(UTL_Connect(*client, (struct sockaddr*)addr, sizeof(*addr)) != 0)
    {
        fprintf(stderr, "setup_loopback_tcp_pair: UTL_Connect failed\n");
        return -1;
    }

    *accepted = accept_native_socket(*server, NULL, NULL);
#ifdef WIN32
    if(*accepted == INVALID_SOCKET)
    {
        fprintf(stderr, "setup_loopback_tcp_pair: accept failed\n");
        return -1;
    }
#else
    if(*accepted < 0)
    {
        perror("setup_loopback_tcp_pair: accept");
        return -1;
    }
#endif

    return 0;
}

int main(void)
{
    enum { PAYLOAD_SIZE = 65536 };
    UTL_D_SOCKET server = (UTL_D_SOCKET)-1;
    UTL_D_SOCKET client = (UTL_D_SOCKET)-1;
    UTL_D_SOCKET accepted = (UTL_D_SOCKET)-1;
    struct sockaddr_in addr;
    SOCKET_RECV_CTX recv_ctx;
    char* send_buf = NULL;
    double start_ms = 0.0;
    double elapsed_ms = 0.0;

    memset(&addr, 0, sizeof(addr));
    memset(&recv_ctx, 0, sizeof(recv_ctx));

    send_buf = (char*)malloc(PAYLOAD_SIZE);
    if(send_buf == NULL) FAIL_STEP("malloc");
    memset(send_buf, 'a', PAYLOAD_SIZE);

    if(setup_loopback_tcp_pair(&server, &client, &accepted, &addr) != 0) FAIL_STEP("setup_loopback_tcp_pair");

    recv_ctx.socket_desc = accepted;
    recv_ctx.total_bytes = PAYLOAD_SIZE;

#ifdef WIN32
    recv_ctx.thread = CreateThread(NULL, 0, recv_worker_main, &recv_ctx, 0, NULL);
    if(recv_ctx.thread == NULL) FAIL_STEP("CreateThread");
#else
    if(pthread_create(&recv_ctx.thread, NULL, recv_worker_main, &recv_ctx) != 0) FAIL_STEP("pthread_create");
#endif

    start_ms = now_ms();
    if(send_all_in_chunks(client, send_buf, PAYLOAD_SIZE) != 0) FAIL_STEP("send_all_in_chunks");

#ifdef WIN32
    WaitForSingleObject(recv_ctx.thread, INFINITE);
    CloseHandle(recv_ctx.thread);
#else
    pthread_join(recv_ctx.thread, NULL);
#endif

    if(recv_ctx.received_bytes != PAYLOAD_SIZE) FAIL_STEP("recv_worker");

    elapsed_ms = now_ms() - start_ms;

    printf("socket bytes=%d elapsed_ms=%.3f throughput_mb_s=%.3f\n",
           PAYLOAD_SIZE,
           elapsed_ms,
           (PAYLOAD_SIZE / (1024.0 * 1024.0)) / (elapsed_ms / 1000.0));

    close_native_socket(accepted);
    UTL_CloseSocket(client);
    UTL_CloseSocket(server);
    free(send_buf);
    return EXIT_SUCCESS;
}
