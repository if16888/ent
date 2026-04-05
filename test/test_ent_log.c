#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef WIN32
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "ent_log.h"
#include "ient_comm.h"

ENT_CTX gEntCtx;

typedef struct TEST_BAD_LOG_CTX
{
    unsigned int tag;
    int isInit;
} TEST_BAD_LOG_CTX;

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }

    return 0;
}

static int path_exists(const char* path)
{
#ifdef WIN32
    FILE* fp = fopen(path, "r");
    if(fp != NULL)
    {
        fclose(fp);
        return 1;
    }
    return 0;
#else
    return access(path, F_OK) == 0;
#endif
}

#ifndef WIN32
static void remove_dir_contents(const char* path)
{
    DIR* dir = opendir(path);
    struct dirent* entry = NULL;
    char child[512];

    if(dir == NULL)
    {
        return;
    }

    while((entry = readdir(dir)) != NULL)
    {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        unlink(child);
    }

    closedir(dir);
    rmdir(path);
}

static int make_temp_dir(char* buffer, size_t size)
{
    const char* templateStr = "/tmp/ent_log_test_XXXXXX";
    if(size < strlen(templateStr) + 1)
    {
        return -1;
    }

    snprintf(buffer, size, "%s", templateStr);
    return mkdtemp(buffer) == NULL ? -1 : 0;
}
#endif

static void format_log_file_path(char* buffer, size_t size, const char* dir, const char* moduleName)
{
    time_t now = time(NULL);
    struct tm nowTm;

#ifdef WIN32
    localtime_s(&nowTm, &now);
#else
    localtime_r(&now, &nowTm);
#endif

    snprintf(buffer,
             size,
             "%s/%s_%04d%02d%02d.log",
             dir,
             moduleName,
             nowTm.tm_year + 1900,
             nowTm.tm_mon + 1,
             nowTm.tm_mday);
}

#ifndef WIN32
typedef struct TEST_LOG_RACE_CTX
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    ENT_LOG logHandle;
    int writerReady;
    int writerRelease;
    int writerAboutToLog;
    int writerDone;
} TEST_LOG_RACE_CTX;

static void race_ctx_reset(TEST_LOG_RACE_CTX* ctx)
{
    ctx->writerReady = 0;
    ctx->writerRelease = 0;
    ctx->writerAboutToLog = 0;
    ctx->writerDone = 0;
}

static void make_deadline(struct timespec* deadline, int timeout_ms)
{
    struct timeval now;

    gettimeofday(&now, NULL);
    deadline->tv_sec = now.tv_sec + timeout_ms / 1000;
    deadline->tv_nsec = now.tv_usec * 1000 + (timeout_ms % 1000) * 1000000L;
    if(deadline->tv_nsec >= 1000000000L)
    {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

static int wait_for_flag(TEST_LOG_RACE_CTX* ctx, int* flag, int expected, int timeout_ms)
{
    struct timespec deadline;
    int rc = 0;

    make_deadline(&deadline, timeout_ms);
    if(pthread_mutex_lock(&ctx->mutex) != 0)
    {
        return 1;
    }

    while(*flag != expected)
    {
        rc = pthread_cond_timedwait(&ctx->cond, &ctx->mutex, &deadline);
        if(rc == ETIMEDOUT)
        {
            break;
        }
        if(rc != 0)
        {
            pthread_mutex_unlock(&ctx->mutex);
            return 1;
        }
    }

    rc = (*flag == expected) ? 0 : 1;
    pthread_mutex_unlock(&ctx->mutex);
    return rc;
}

static void set_flag_and_wake(int* flag, TEST_LOG_RACE_CTX* ctx, int value)
{
    *flag = value;
    pthread_cond_broadcast(&ctx->cond);
}

static void* controlled_log_writer(void* data)
{
    TEST_LOG_RACE_CTX* ctx = (TEST_LOG_RACE_CTX*)data;
    char payload[32768];
    size_t i = 0;

    for(i = 0; i + 1 < sizeof(payload); ++i)
    {
        payload[i] = 'A';
    }
    payload[sizeof(payload) - 1] = '\0';

    pthread_mutex_lock(&ctx->mutex);
    set_flag_and_wake(&ctx->writerReady, ctx, 1);
    while(!ctx->writerRelease)
    {
        pthread_cond_wait(&ctx->cond, &ctx->mutex);
    }
    set_flag_and_wake(&ctx->writerAboutToLog, ctx, 1);
    pthread_mutex_unlock(&ctx->mutex);

    ENT_LogPrint(ctx->logHandle, "%s race write\n", payload);

    pthread_mutex_lock(&ctx->mutex);
    set_flag_and_wake(&ctx->writerDone, ctx, 1);
    pthread_mutex_unlock(&ctx->mutex);

    return NULL;
}
#endif

static int test_log_rejects_uninitialized_calls(void)
{
    ENT_LOG logHandle = NULL;
    ENT_LOG_LEV_E level = LOG_LEV_DEBUG_E;

    if(expect_true(ENT_LogInitHandle(&logHandle, "NoInit", ".") == -1,
                   "ENT_LogInitHandle should reject use before ENT_LogInit") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_LogSetOption(NULL, ENT_LOG_LEVEL_E, &level) == -1,
                   "ENT_LogSetOption should reject use before ENT_LogInit") != 0)
    {
        return 1;
    }

    return expect_true(ENT_LogCloseHandle(NULL) == -1,
                       "ENT_LogCloseHandle should reject use before ENT_LogInit");
}

static int test_default_log_handle_lifecycle(void)
{
    ENT_LOG_LEV_E level = LOG_LEV_INFO_E;

    if(expect_true(ENT_LogInit() == 0, "ENT_LogInit should initialize the log subsystem") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_LogInitHandle(NULL, "DefaultModule", ".") == 0,
                   "ENT_LogInitHandle should initialize the default log handle") != 0)
    {
        ENT_LogClose();
        return 1;
    }

    if(expect_true(ENT_LogInitHandle(NULL, "DefaultModule", ".") == 1,
                   "ENT_LogInitHandle should report an already-open default handle") != 0)
    {
        ENT_LogCloseHandle(NULL);
        ENT_LogClose();
        return 1;
    }

    if(expect_true(ENT_LogSetOption(NULL, ENT_LOG_LEVEL_E, &level) == 0,
                   "ENT_LogSetOption should accept the default log handle") != 0)
    {
        ENT_LogCloseHandle(NULL);
        ENT_LogClose();
        return 1;
    }

    if(expect_true(ENT_LogCloseHandle(NULL) == 0,
                   "ENT_LogCloseHandle should close the default log handle") != 0)
    {
        ENT_LogClose();
        return 1;
    }

    if(expect_true(ENT_LogRaw(NULL, "after close\n") == -2,
                   "ENT_LogRaw should reject the default handle after it is closed") != 0)
    {
        ENT_LogClose();
        return 1;
    }

    return expect_true(ENT_LogClose() == 0, "ENT_LogClose should shut down the log subsystem");
}

static int test_log_close_rejects_invalid_handle(void)
{
    TEST_BAD_LOG_CTX badLog;

    memset(&badLog, 0, sizeof(badLog));
    badLog.tag = 0x12345678;
    badLog.isInit = 1;

    if(expect_true(ENT_LogInit() == 0, "ENT_LogInit should initialize before close-handle validation") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_LogCloseHandle((ENT_LOG)&badLog) == -2,
                   "ENT_LogCloseHandle should reject invalid log handles") != 0)
    {
        ENT_LogClose();
        return 1;
    }

    return expect_true(ENT_LogClose() == 0, "ENT_LogClose should succeed after invalid handle validation");
}

static int test_log_set_option_validates_arguments(void)
{
    ENT_LOG logHandle = NULL;
    ENT_LOG_LEV_E level = LOG_LEV_WARN_E;
    TEST_BAD_LOG_CTX badLog;

    memset(&badLog, 0, sizeof(badLog));
    badLog.tag = 0x12345678;
    badLog.isInit = 1;

    if(expect_true(ENT_LogInit() == 0, "ENT_LogInit should initialize before option validation") != 0)
    {
        return 1;
    }

    if(expect_true(ENT_LogInitHandle(&logHandle, "OptionModule", ".") == 0,
                   "ENT_LogInitHandle should create a private log handle") != 0)
    {
        ENT_LogClose();
        return 1;
    }

    if(expect_true(ENT_LogSetOption(logHandle, ENT_LOG_LEVEL_E, NULL) == -1,
                   "ENT_LogSetOption should reject a NULL option argument") != 0)
    {
        ENT_LogCloseHandle(logHandle);
        ENT_LogClose();
        return 1;
    }

    if(expect_true(ENT_LogSetOption(logHandle, ENT_LOG_LEVEL_E, &level) == 0,
                   "ENT_LogSetOption should accept a valid log level") != 0)
    {
        ENT_LogCloseHandle(logHandle);
        ENT_LogClose();
        return 1;
    }

    if(expect_true(ENT_LogSetOption((ENT_LOG)&badLog, ENT_LOG_LEVEL_E, &level) == -2,
                   "ENT_LogSetOption should reject an invalid log handle") != 0)
    {
        ENT_LogCloseHandle(logHandle);
        ENT_LogClose();
        return 1;
    }

    if(expect_true(ENT_LogCloseHandle(logHandle) == 0, "ENT_LogCloseHandle should close the private handle") != 0)
    {
        ENT_LogClose();
        return 1;
    }

    return expect_true(ENT_LogClose() == 0, "ENT_LogClose should shut down after option validation");
}

static int test_log_path_option_trims_trailing_separator_and_writes_file(void)
{
#ifdef WIN32
    return 0;
#else
    ENT_LOG logHandle = NULL;
    char dirPath[256];
    char dirWithSlash[258];
    char logFilePath[512];
    FILE* fp = NULL;
    char buffer[512];
    size_t bytesRead = 0;

    if(make_temp_dir(dirPath, sizeof(dirPath)) != 0)
    {
        fprintf(stderr, "failed to create temp log directory\n");
        return 1;
    }

    snprintf(dirWithSlash, sizeof(dirWithSlash), "%s/", dirPath);
    format_log_file_path(logFilePath, sizeof(logFilePath), dirPath, "PathModule");

    if(expect_true(ENT_LogInit() == 0, "ENT_LogInit should initialize before path-option testing") != 0)
    {
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(ENT_LogInitHandle(&logHandle, "PathModule", ".") == 0,
                   "ENT_LogInitHandle should create a log handle for path testing") != 0)
    {
        ENT_LogClose();
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(ENT_LogSetOption(logHandle, ENT_LOG_PATH_E, dirWithSlash) == 0,
                   "ENT_LogSetOption should accept a path with a trailing separator") != 0)
    {
        ENT_LogCloseHandle(logHandle);
        ENT_LogClose();
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(ENT_LogWarn(logHandle, "Path message %d\n", 42) == 0,
                   "ENT_LogWarn should write to the updated path") != 0)
    {
        ENT_LogCloseHandle(logHandle);
        ENT_LogClose();
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(ENT_LogCloseHandle(logHandle) == 0, "ENT_LogCloseHandle should close the path-test handle") != 0)
    {
        ENT_LogClose();
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(ENT_LogClose() == 0, "ENT_LogClose should shut down after path testing") != 0)
    {
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(path_exists(logFilePath), "ENT_LogSetOption should trim the trailing separator when creating log files") != 0)
    {
        remove_dir_contents(dirPath);
        return 1;
    }

    fp = fopen(logFilePath, "r");
    if(expect_true(fp != NULL, "The updated log path should contain a log file") != 0)
    {
        remove_dir_contents(dirPath);
        return 1;
    }

    memset(buffer, 0, sizeof(buffer));
    bytesRead = fread(buffer, 1, sizeof(buffer) - 1, fp);
    fclose(fp);

    if(expect_true(bytesRead > 0, "The generated log file should not be empty") != 0)
    {
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(strstr(buffer, "Path message 42") != NULL,
                   "The generated log file should contain the emitted message") != 0)
    {
        remove_dir_contents(dirPath);
        return 1;
    }

    remove_dir_contents(dirPath);
    return 0;
#endif
}

static int test_log_level_filters_debug_messages(void)
{
#ifdef WIN32
    return 0;
#else
    ENT_LOG logHandle = NULL;
    ENT_LOG_LEV_E level = LOG_LEV_WARN_E;
    char dirPath[256];
    char logFilePath[512];
    FILE* fp = NULL;
    char buffer[1024];
    size_t bytesRead = 0;

    if(make_temp_dir(dirPath, sizeof(dirPath)) != 0)
    {
        fprintf(stderr, "failed to create temp filter directory\n");
        return 1;
    }

    format_log_file_path(logFilePath, sizeof(logFilePath), dirPath, "FilterModule");

    if(expect_true(ENT_LogInit() == 0, "ENT_LogInit should initialize before level filtering") != 0)
    {
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(ENT_LogInitHandle(&logHandle, "FilterModule", dirPath) == 0,
                   "ENT_LogInitHandle should create a log handle for filtering") != 0)
    {
        ENT_LogClose();
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(ENT_LogSetOption(logHandle, ENT_LOG_LEVEL_E, &level) == 0,
                   "ENT_LogSetOption should update the log level") != 0)
    {
        ENT_LogCloseHandle(logHandle);
        ENT_LogClose();
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(ENT_LogDebug(logHandle, "hidden debug\n") == 1,
                   "ENT_LogDebug should be filtered out when the level is WARN") != 0)
    {
        ENT_LogCloseHandle(logHandle);
        ENT_LogClose();
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(ENT_LogWarn(logHandle, "visible warning\n") == 0,
                   "ENT_LogWarn should still be written at WARN level") != 0)
    {
        ENT_LogCloseHandle(logHandle);
        ENT_LogClose();
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(ENT_LogCloseHandle(logHandle) == 0, "ENT_LogCloseHandle should close the filter handle") != 0)
    {
        ENT_LogClose();
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(ENT_LogClose() == 0, "ENT_LogClose should shut down after filtering checks") != 0)
    {
        remove_dir_contents(dirPath);
        return 1;
    }

    fp = fopen(logFilePath, "r");
    if(expect_true(fp != NULL, "The filter test log file should exist") != 0)
    {
        remove_dir_contents(dirPath);
        return 1;
    }

    memset(buffer, 0, sizeof(buffer));
    bytesRead = fread(buffer, 1, sizeof(buffer) - 1, fp);
    fclose(fp);

    if(expect_true(bytesRead > 0, "The filter test log file should contain data") != 0)
    {
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(strstr(buffer, "visible warning") != NULL,
                   "WARN messages should be present in the log file") != 0)
    {
        remove_dir_contents(dirPath);
        return 1;
    }

    if(expect_true(strstr(buffer, "hidden debug") == NULL,
                   "Filtered DEBUG messages should not be present in the log file") != 0)
    {
        remove_dir_contents(dirPath);
        return 1;
    }

    remove_dir_contents(dirPath);
    return 0;
#endif
}

static int test_log_close_handle_waits_for_active_writers(void)
{
#ifdef WIN32
    return 0;
#else
    ENT_LOG logHandle = NULL;
    pthread_t writerThread;
    TEST_LOG_RACE_CTX ctx;
    ENT_LOG_LEV_E level = LOG_LEV_INFO_E;
    char dirPath[256];
    MSG_ID_T closeStatus = 0;
    int writerStarted = 0;
    int logHandleOpen = 0;
    int logInitDone = 0;
    int mutexInitDone = 0;
    int condInitDone = 0;
    int tempDirCreated = 0;
    int rc = 1;

    memset(&ctx, 0, sizeof(ctx));

    if(make_temp_dir(dirPath, sizeof(dirPath)) != 0)
    {
        fprintf(stderr, "failed to create temp race directory\n");
        goto cleanup;
    }
    tempDirCreated = 1;

    if(pthread_mutex_init(&ctx.mutex, NULL) != 0)
    {
        goto cleanup;
    }
    mutexInitDone = 1;

    if(pthread_cond_init(&ctx.cond, NULL) != 0)
    {
        goto cleanup;
    }
    condInitDone = 1;

    if(expect_true(ENT_LogInit() == 0, "ENT_LogInit should initialize before close/write race test") != 0)
    {
        goto cleanup;
    }
    logInitDone = 1;

    if(expect_true(ENT_LogInitHandle(&logHandle, "RaceModule", dirPath) == 0,
                   "ENT_LogInitHandle should create a private handle for race test") != 0)
    {
        goto cleanup;
    }
    logHandleOpen = 1;

    if(expect_true(ENT_LogSetOption(logHandle, ENT_LOG_LEVEL_E, &level) == 0,
                   "ENT_LogSetOption should enable INFO writes for the race test") != 0)
    {
        goto cleanup;
    }

    race_ctx_reset(&ctx);
    ctx.logHandle = logHandle;

    if(expect_true(pthread_create(&writerThread, NULL, controlled_log_writer, &ctx) == 0,
                   "pthread_create should start a writer thread") != 0)
    {
        goto cleanup;
    }
    writerStarted = 1;

    if(expect_true(wait_for_flag(&ctx, &ctx.writerReady, 1, 1000) == 0,
                   "writer thread should signal readiness before close") != 0)
    {
        goto cleanup;
    }

    pthread_mutex_lock(&ctx.mutex);
    set_flag_and_wake(&ctx.writerRelease, &ctx, 1);
    pthread_mutex_unlock(&ctx.mutex);

    if(expect_true(wait_for_flag(&ctx, &ctx.writerAboutToLog, 1, 1000) == 0,
                   "writer thread should reach the log-call window before close") != 0)
    {
        goto cleanup;
    }

    closeStatus = ENT_LogCloseHandle(logHandle);
    if(expect_true(closeStatus == 0,
                   "ENT_LogCloseHandle should return success during the bounded overlap window") != 0)
    {
        goto cleanup;
    }
    logHandleOpen = 0;
    logHandle = NULL;

    if(expect_true(wait_for_flag(&ctx, &ctx.writerDone, 1, 2000) == 0,
                   "writer thread should finish after the handle closes") != 0)
    {
        goto cleanup;
    }

    if(expect_true(pthread_join(writerThread, NULL) == 0,
                   "pthread_join should join the writer thread") != 0)
    {
        goto cleanup;
    }

    writerStarted = 0;
    rc = 0;

cleanup:
    if(writerStarted)
    {
        pthread_mutex_lock(&ctx.mutex);
        ctx.writerRelease = 1;
        pthread_cond_broadcast(&ctx.cond);
        pthread_mutex_unlock(&ctx.mutex);
        pthread_join(writerThread, NULL);
    }

    if(logHandleOpen && logHandle != NULL)
    {
        ENT_LogCloseHandle(logHandle);
    }

    if(logInitDone)
    {
        ENT_LogClose();
    }

    if(condInitDone)
    {
        pthread_cond_destroy(&ctx.cond);
    }
    if(mutexInitDone)
    {
        pthread_mutex_destroy(&ctx.mutex);
    }
    if(tempDirCreated)
    {
        remove_dir_contents(dirPath);
    }
    return rc;
#endif
}

static int test_buffered_log_close_flushes_queued_messages(void)
{
#ifdef WIN32
    return 0;
#else
    ENT_LOG logHandle = NULL;
    ENT_LOG_LEV_E level = LOG_LEV_INFO_E;
    bool buffered = true;
    char dirPath[256];
    char logFilePath[512];
    FILE* fp = NULL;
    char* buffer = NULL;
    const char* needle = "buffered line=";
    const char* cursor = NULL;
    long fileSize = 0;
    size_t bytesRead = 0;
    int occurrences = 0;
    int rc = 1;

    if(make_temp_dir(dirPath, sizeof(dirPath)) != 0)
    {
        fprintf(stderr, "failed to create temp buffered directory\n");
        return 1;
    }

    format_log_file_path(logFilePath, sizeof(logFilePath), dirPath, "BufferedModule");

    if(expect_true(ENT_LogInit() == 0, "ENT_LogInit should initialize before buffered log testing") != 0)
    {
        goto cleanup;
    }

    if(expect_true(ENT_LogInitHandle(&logHandle, "BufferedModule", dirPath) == 0,
                   "ENT_LogInitHandle should create a handle for buffered log testing") != 0)
    {
        ENT_LogClose();
        goto cleanup;
    }

    if(expect_true(ENT_LogSetOption(logHandle, ENT_LOG_LEVEL_E, &level) == 0,
                   "ENT_LogSetOption should enable INFO writes for buffered log testing") != 0)
    {
        ENT_LogCloseHandle(logHandle);
        ENT_LogClose();
        goto cleanup;
    }

    if(expect_true(ENT_LogSetOption(logHandle, ENT_LOG_BUFFER_E, &buffered) == 0,
                   "ENT_LogSetOption should enable explicit buffered logging") != 0)
    {
        ENT_LogCloseHandle(logHandle);
        ENT_LogClose();
        goto cleanup;
    }

    for(int i = 0; i < 128; ++i)
    {
        if(expect_true(ENT_LogPrint(logHandle, "buffered line=%d\n", i) == 0,
                       "ENT_LogPrint should enqueue buffered writes") != 0)
        {
            ENT_LogCloseHandle(logHandle);
            ENT_LogClose();
            goto cleanup;
        }
    }

    if(expect_true(ENT_LogCloseHandle(logHandle) == 0,
                   "ENT_LogCloseHandle should flush buffered writes before closing") != 0)
    {
        ENT_LogClose();
        goto cleanup;
    }
    logHandle = NULL;

    if(expect_true(ENT_LogClose() == 0, "ENT_LogClose should shut down after buffered log testing") != 0)
    {
        goto cleanup;
    }

    fp = fopen(logFilePath, "r");
    if(expect_true(fp != NULL, "Buffered logging should create a log file") != 0)
    {
        goto cleanup;
    }

    if(fseek(fp, 0, SEEK_END) != 0)
    {
        goto cleanup;
    }
    fileSize = ftell(fp);
    if(fileSize < 0)
    {
        goto cleanup;
    }
    if(fseek(fp, 0, SEEK_SET) != 0)
    {
        goto cleanup;
    }
    buffer = (char*)calloc((size_t)fileSize + 1, 1);
    if(buffer == NULL)
    {
        goto cleanup;
    }
    bytesRead = fread(buffer, 1, (size_t)fileSize, fp);
    fclose(fp);
    fp = NULL;

    if(expect_true(bytesRead > 0, "Buffered logging should flush queued messages on close") != 0)
    {
        goto cleanup;
    }

    cursor = buffer;
    while((cursor = strstr(cursor, needle)) != NULL)
    {
        occurrences++;
        cursor += strlen(needle);
    }

    if(expect_true(occurrences == 128,
                   "Buffered logging should flush every queued message before close") != 0)
    {
        goto cleanup;
    }

    rc = 0;

cleanup:
    if(fp != NULL)
    {
        fclose(fp);
    }
    if(buffer != NULL)
    {
        free(buffer);
    }
    if(logHandle != NULL)
    {
        ENT_LogCloseHandle(logHandle);
        ENT_LogClose();
    }
    remove_dir_contents(dirPath);
    return rc;
#endif
}

int main(void)
{
    int failures = 0;

    failures += test_log_rejects_uninitialized_calls();
    failures += test_default_log_handle_lifecycle();
    failures += test_log_close_rejects_invalid_handle();
    failures += test_log_set_option_validates_arguments();
    failures += test_log_path_option_trims_trailing_separator_and_writes_file();
    failures += test_log_level_filters_debug_messages();
    failures += test_log_close_handle_waits_for_active_writers();
    failures += test_buffered_log_close_flushes_queued_messages();

    if(failures != 0)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
