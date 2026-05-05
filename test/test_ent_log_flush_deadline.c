#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef WIN32
#include <Windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "ent_log.h"
#include "ient_comm.h"
#include "ient_runtime.h"

ENT_CTX gEntCtx;

ENT_CTX* iENT_RuntimeActiveCtx(void)
{
    return &gEntCtx;
}

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

static long long now_ms(void)
{
#ifdef WIN32
    return (long long)GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long long)tv.tv_sec * 1000LL) + ((long long)tv.tv_usec / 1000LL);
#endif
}

static void sleep_ms(int ms)
{
#ifdef WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000U);
#endif
}

static void remove_dir_contents(const char* path)
{
#ifdef WIN32
    WIN32_FIND_DATAA findData;
    HANDLE findHandle = INVALID_HANDLE_VALUE;
    char pattern[512];
    char child[512];

    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    findHandle = FindFirstFileA(pattern, &findData);
    if(findHandle == INVALID_HANDLE_VALUE)
    {
        RemoveDirectoryA(path);
        return;
    }

    do
    {
        if(strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
        {
            continue;
        }
        snprintf(child, sizeof(child), "%s\\%s", path, findData.cFileName);
        if(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            remove_dir_contents(child);
        }
        else
        {
            DeleteFileA(child);
        }
    } while(FindNextFileA(findHandle, &findData));

    FindClose(findHandle);
    RemoveDirectoryA(path);
#else
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
#endif
}

static int make_temp_dir(char* buffer, size_t size)
{
#ifdef WIN32
    char tempPath[MAX_PATH];
    char tempFile[MAX_PATH];

    if(GetTempPathA(MAX_PATH, tempPath) == 0)
    {
        return -1;
    }
    if(GetTempFileNameA(tempPath, "elf", 0, tempFile) == 0)
    {
        return -1;
    }
    DeleteFileA(tempFile);
    if(CreateDirectoryA(tempFile, NULL) == 0)
    {
        return -1;
    }
    if(strlen(tempFile) + 1 > size)
    {
        remove_dir_contents(tempFile);
        return -1;
    }
    snprintf(buffer, size, "%s", tempFile);
    return 0;
#else
    const char* templateStr = "/tmp/ent_log_flush_deadline_XXXXXX";
    if(size < strlen(templateStr) + 1)
    {
        return -1;
    }
    snprintf(buffer, size, "%s", templateStr);
    return mkdtemp(buffer) == NULL ? -1 : 0;
#endif
}

static void format_log_file_path(char* buffer, size_t size, const char* dir, const char* moduleName)
{
    time_t now = time(NULL);
    struct tm nowTm;
#ifdef WIN32
    localtime_s(&nowTm, &now);
    snprintf(buffer,
             size,
             "%s\\%s_%04d%02d%02d.log",
             dir,
             moduleName,
             nowTm.tm_year + 1900,
             nowTm.tm_mon + 1,
             nowTm.tm_mday);
#else
    localtime_r(&now, &nowTm);
    snprintf(buffer,
             size,
             "%s/%s_%04d%02d%02d.log",
             dir,
             moduleName,
             nowTm.tm_year + 1900,
             nowTm.tm_mon + 1,
             nowTm.tm_mday);
#endif
}

static int file_contains(const char* filePath, const char* needle)
{
    FILE* fp = ENT_FOpen(filePath, "r");
    char buffer[2048];
    size_t bytesRead = 0;

    if(fp == NULL)
    {
        return 0;
    }

    memset(buffer, 0, sizeof(buffer));
    bytesRead = fread(buffer, 1, sizeof(buffer) - 1, fp);
    fclose(fp);

    return bytesRead > 0 && strstr(buffer, needle) != NULL;
}

static int path_exists(const char* filePath)
{
#ifdef WIN32
    DWORD attrs = GetFileAttributesA(filePath);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    return access(filePath, F_OK) == 0;
#endif
}

int main(void)
{
    ENT_LOG logHandle = NULL;
    ENT_LOG_LEV_E level = LOG_LEV_INFO_E;
    bool buffered = true;
    int flushBatch = 10000;
    int flushIntervalMs = 20;
    const int flushDeadlineMs = 1000;
    const char* moduleName = "FlushDeadlineModule";
    const char* message = "flush deadline sentinel";
    char dirPath[256];
    char logFilePath[512];
    long long startMs = 0;
    long long elapsedMs = 0;
    int found = 0;
    int rc = 1;

    if(make_temp_dir(dirPath, sizeof(dirPath)) != 0)
    {
        fprintf(stderr, "failed to create temp flush-deadline directory\n");
        return EXIT_FAILURE;
    }

    format_log_file_path(logFilePath, sizeof(logFilePath), dirPath, moduleName);

    if(expect_true(ENT_LogInit() == ENT_SYS_NORMAL,
                   "ENT_LogInit should initialize before flush-deadline testing") != 0)
    {
        goto cleanup_dir;
    }

    if(expect_true(ENT_LogInitHandle(&logHandle, moduleName, dirPath) == ENT_SYS_NORMAL,
                   "ENT_LogInitHandle should create a handle for flush-deadline testing") != 0)
    {
        ENT_LogClose();
        goto cleanup_dir;
    }

    if(expect_true(ENT_LogSetOption(logHandle, ENT_LOG_LEVEL_E, &level) == ENT_SYS_NORMAL,
                   "ENT_LogSetOption should enable INFO writes for flush-deadline testing") != 0)
    {
        goto cleanup_log;
    }

    if(expect_true(ENT_LogSetOption(logHandle, ENT_LOG_BUFFER_E, &buffered) == ENT_SYS_NORMAL,
                   "ENT_LogSetOption should enable buffered logging for flush-deadline testing") != 0)
    {
        goto cleanup_log;
    }

    if(expect_true(ENT_LogSetOption(logHandle, ENT_LOG_FLUSH_BATCH_E, &flushBatch) == ENT_SYS_NORMAL,
                   "ENT_LogSetOption should apply a high flush batch for deadline testing") != 0)
    {
        goto cleanup_log;
    }

    if(expect_true(ENT_LogSetOption(logHandle, ENT_LOG_FLUSH_INTERVAL_E, &flushIntervalMs) == ENT_SYS_NORMAL,
                   "ENT_LogSetOption should apply the flush interval for deadline testing") != 0)
    {
        goto cleanup_log;
    }

    startMs = now_ms();
    if(expect_true(ENT_LogPrint(logHandle, "%s\n", message) == ENT_SYS_NORMAL,
                   "ENT_LogPrint should enqueue the flush-deadline message") != 0)
    {
        goto cleanup_log;
    }

    while((elapsedMs = now_ms() - startMs) <= flushDeadlineMs)
    {
        if(file_contains(logFilePath, message))
        {
            found = 1;
            break;
        }
#ifdef WIN32
        if(path_exists(logFilePath))
        {
            found = 1;
            break;
        }
#endif
        sleep_ms(10);
    }

    if(expect_true(found == 1,
                   "Buffered logging should flush by interval within the deadline before close") != 0)
    {
        goto cleanup_log;
    }

    if(expect_true(elapsedMs <= flushDeadlineMs,
                   "Flush interval exceeded the accepted deadline") != 0)
    {
        goto cleanup_log;
    }

    rc = 0;

cleanup_log:
    if(logHandle != NULL)
    {
        ENT_LogCloseHandle(logHandle);
        logHandle = NULL;
    }
    ENT_LogClose();
    if(expect_true(file_contains(logFilePath, message),
                   "Buffered logging should persist the flushed message to disk") != 0)
    {
        goto cleanup_dir;
    }
cleanup_dir:
    remove_dir_contents(dirPath);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
