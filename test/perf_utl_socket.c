#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>

#ifndef WIN32
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "ient_comm.h"
#include "ent_utility.h"

#define FAIL_STEP(step)                                 \
    do                                  \
    {                                   \
        fprintf(stderr, "%s errno=%d\n", step, errno);  \
        return EXIT_FAILURE;            \
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
    int socketDesc;
    int totalBytes;
    int receivedBytes;
} SOCKET_RECV_CTX;

static double now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static int send_all_in_chunks(UTL_D_SOCKET socketDesc, char* buffer, int bufferLength)
{
    int totalSent = 0;

    while(totalSent < bufferLength)
    {
        int sent = 0;
        int chunk = bufferLength - totalSent;

        if(chunk > 4096)
        {
            chunk = 4096;
        }

        if(UTL_Send(socketDesc, buffer + totalSent, chunk, 0, &sent) != 0 || sent <= 0)
        {
            return -1;
        }
        totalSent += sent;
    }

    return 0;
}

static int recv_all_in_chunks(UTL_D_SOCKET socketDesc, int totalBytes)
{
    int totalRecv = 0;
    char recvBuf[4096];

    while(totalRecv < totalBytes)
    {
        int recvd = 0;
        int chunk = totalBytes - totalRecv;

        if(chunk > (int)sizeof(recvBuf))
        {
            chunk = (int)sizeof(recvBuf);
        }

        if(UTL_Recv(socketDesc, recvBuf, chunk, 0, &recvd) != 0 || recvd <= 0)
        {
            return -1;
        }
        totalRecv += recvd;
    }

    return 0;
}

static void* recv_worker(void* data)
{
    SOCKET_RECV_CTX* ctx = (SOCKET_RECV_CTX*)data;

    if(recv_all_in_chunks(ctx->socketDesc, ctx->totalBytes) != 0)
    {
        ctx->receivedBytes = -1;
        return NULL;
    }

    ctx->receivedBytes = ctx->totalBytes;
    return NULL;
}

int main(void)
{
#ifndef WIN32
    enum { PAYLOAD_SIZE = 65536 };
    UTL_D_SOCKET sockets[2] = { -1, -1 };
    char* sendBuf = NULL;
    double startMs = 0.0;
    double elapsedMs = 0.0;
    SOCKET_RECV_CTX recvCtx;
    pthread_t recvThread;

    memset(&recvCtx, 0, sizeof(recvCtx));

    sendBuf = (char*)malloc(PAYLOAD_SIZE);
    if(sendBuf == NULL) FAIL_STEP("malloc");
    memset(sendBuf, 'a', PAYLOAD_SIZE);

    if(UTL_SocketInit() != 0) FAIL_STEP("UTL_SocketInit");
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) FAIL_STEP("socketpair");

    recvCtx.socketDesc = sockets[1];
    recvCtx.totalBytes = PAYLOAD_SIZE;
    if(pthread_create(&recvThread, NULL, recv_worker, &recvCtx) != 0) FAIL_STEP("pthread_create");

    startMs = now_ms();
    if(send_all_in_chunks(sockets[0], sendBuf, PAYLOAD_SIZE) != 0) FAIL_STEP("send_all_in_chunks");
    pthread_join(recvThread, NULL);
    if(recvCtx.receivedBytes != PAYLOAD_SIZE) FAIL_STEP("recv_worker");

    elapsedMs = now_ms() - startMs;

    printf("socket bytes=%d elapsed_ms=%.3f throughput_mb_s=%.3f\n",
           PAYLOAD_SIZE,
           elapsedMs,
           (PAYLOAD_SIZE / (1024.0 * 1024.0)) / (elapsedMs / 1000.0));

    UTL_CloseSocket(sockets[0]);
    UTL_CloseSocket(sockets[1]);
    free(sendBuf);
    return EXIT_SUCCESS;
#else
    return EXIT_FAILURE;
#endif
}
