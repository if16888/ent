#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "ent_shm.h"

#define TEST_MAP_SIZE ((ENT_SIZE)4096u)

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

#ifdef _WIN32
static int make_temp_path(char* path, size_t path_size)
{
    char directory[MAX_PATH];
    char temporary[MAX_PATH];

    if(GetTempPathA((DWORD)sizeof(directory), directory) == 0 ||
       GetTempFileNameA(directory, "esh", 0, temporary) == 0)
    {
        return 1;
    }
    DeleteFileA(temporary);
    return snprintf(path, path_size, "%s", temporary) >= (int)path_size;
}

static int test_windows_security_boundaries(void)
{
    ENT_SharedMapOptions options;
    ENT_SharedMap* map = NULL;
    char path[MAX_PATH] = {0};
    char directory[MAX_PATH] = {0};
    char target[MAX_PATH] = {0};
    char link_path[MAX_PATH] = {0};
    DWORD link_error;
    int failed = 0;

    if(make_temp_path(path, sizeof(path)) != 0 ||
       snprintf(directory, sizeof(directory), "%s.dir", path) >= (int)sizeof(directory) ||
       snprintf(target, sizeof(target), "%s.target", path) >= (int)sizeof(target) ||
       snprintf(link_path, sizeof(link_path), "%s.link", path) >= (int)sizeof(link_path))
    {
        return 1;
    }

    memset(&options, 0, sizeof(options));
    options.path = path;
    options.size = TEST_MAP_SIZE;
    options.mode = ENT_SHM_MODE_READ_WRITE;
    options.flags = ENT_SHM_F_CREATE_IF_MISSING | ENT_SHM_F_TRUNCATE_IF_EXISTS;
    failed |= expect_true(ENT_SharedMapOpen(&options, &map) == ENT_SYS_NORMAL,
                          "Windows secure shared-map creation should succeed");
    if(map != NULL)
    {
        failed |= expect_true(ENT_SharedMapClose(&map) == ENT_SYS_NORMAL,
                              "Windows secure shared-map close should succeed");
    }
    DeleteFileA(path);

    if(!CreateDirectoryA(directory, NULL))
    {
        return 1;
    }
    options.path = directory;
    failed |= expect_true(ENT_SharedMapOpen(&options, &map) == ENT_SHM_PATH_FAILED,
                          "Windows shared-map directory path must be rejected");
    RemoveDirectoryA(directory);

    {
        HANDLE file = CreateFileA(target, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if(file == INVALID_HANDLE_VALUE)
        {
            return 1;
        }
        CloseHandle(file);
    }
    if(!CreateSymbolicLinkA(link_path, target, 0x2u))
    {
        link_error = GetLastError();
        if(link_error == ERROR_PRIVILEGE_NOT_HELD ||
           link_error == ERROR_INVALID_PARAMETER ||
           link_error == ERROR_NOT_SUPPORTED)
        {
            fprintf(stderr,
                    "SKIP: Windows reparse-point test requires Developer Mode or symbolic-link privilege,error[%lu]\n",
                    (unsigned long)link_error);
            DeleteFileA(target);
            return failed;
        }
        DeleteFileA(target);
        return 1;
    }
    options.path = link_path;
    options.size = 0;
    options.flags = 0;
    failed |= expect_true(ENT_SharedMapOpen(&options, &map) == ENT_SHM_PATH_FAILED,
                          "Windows shared-map reparse point must be rejected");
    DeleteFileA(link_path);
    DeleteFileA(target);
    return failed;
}
#else
static int make_temp_dir(char* path, size_t path_size)
{
    char pattern[] = "/tmp/ent_shm_security_XXXXXX";
    char* created = mkdtemp(pattern);

    if(created == NULL)
    {
        perror("mkdtemp");
        return 1;
    }
    if(snprintf(path, path_size, "%s", created) >= (int)path_size)
    {
        rmdir(created);
        return 1;
    }
    return 0;
}

static int join_path(char* out,
                     size_t out_size,
                     const char* directory,
                     const char* leaf)
{
    return snprintf(out, out_size, "%s/%s", directory, leaf) >=
        (int)out_size;
}

static void cleanup_path(const char* path)
{
    if(path != NULL && path[0] != '\0')
    {
        unlink(path);
    }
}

static int test_created_file_is_private(void)
{
    ENT_SharedMapOptions options;
    ENT_SharedMap* map = NULL;
    struct stat st;
    char directory[256] = {0};
    char path[512] = {0};
    int failed = 0;

    if(make_temp_dir(directory, sizeof(directory)) != 0 ||
       join_path(path, sizeof(path), directory, "region.bin") != 0)
    {
        return 1;
    }

    memset(&options, 0, sizeof(options));
    options.path = path;
    options.size = TEST_MAP_SIZE;
    options.mode = ENT_SHM_MODE_READ_WRITE;
    options.flags = ENT_SHM_F_CREATE_IF_MISSING |
                    ENT_SHM_F_TRUNCATE_IF_EXISTS;

    failed |= expect_true(ENT_SharedMapOpen(&options, &map) == ENT_SYS_NORMAL,
                          "secure shared map creation should succeed");
    if(map != NULL)
    {
        failed |= expect_true(ENT_SharedMapClose(&map) == ENT_SYS_NORMAL,
                              "secure shared map close should succeed");
    }
    failed |= expect_true(stat(path, &st) == 0,
                          "created shared map file should exist");
    if(stat(path, &st) == 0)
    {
        failed |= expect_true(S_ISREG(st.st_mode),
                              "created shared map must be a regular file");
        failed |= expect_true((st.st_mode & (S_IRWXG | S_IRWXO)) == 0,
                              "created shared map must not grant group or other access");
    }

    cleanup_path(path);
    rmdir(directory);
    return failed;
}

static int test_symbolic_link_is_rejected(void)
{
    ENT_SharedMapOptions options;
    ENT_SharedMap* map = NULL;
    char directory[256] = {0};
    char target[512] = {0};
    char link_path[512] = {0};
    int fd = -1;
    int failed = 0;

    if(make_temp_dir(directory, sizeof(directory)) != 0 ||
       join_path(target, sizeof(target), directory, "target.bin") != 0 ||
       join_path(link_path, sizeof(link_path), directory, "region.bin") != 0)
    {
        return 1;
    }

    fd = open(target, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if(fd < 0 || ftruncate(fd, (off_t)TEST_MAP_SIZE) != 0)
    {
        if(fd >= 0)
        {
            close(fd);
        }
        cleanup_path(target);
        rmdir(directory);
        return 1;
    }
    close(fd);
    if(symlink(target, link_path) != 0)
    {
        cleanup_path(target);
        rmdir(directory);
        return 1;
    }

    memset(&options, 0, sizeof(options));
    options.path = link_path;
    options.mode = ENT_SHM_MODE_READ_WRITE;

    failed |= expect_true(ENT_SharedMapOpen(&options, &map) ==
                              ENT_SHM_PATH_FAILED,
                          "symbolic-link shared map path must be rejected");
    failed |= expect_true(map == NULL,
                          "rejected symbolic-link path must not return a map");

    cleanup_path(link_path);
    cleanup_path(target);
    rmdir(directory);
    return failed;
}

static int test_non_regular_file_is_rejected(void)
{
    ENT_SharedMapOptions options;
    ENT_SharedMap* map = NULL;
    char directory[256] = {0};
    char fifo_path[512] = {0};
    int failed = 0;

    if(make_temp_dir(directory, sizeof(directory)) != 0 ||
       join_path(fifo_path, sizeof(fifo_path), directory, "region.fifo") != 0)
    {
        return 1;
    }
    if(mkfifo(fifo_path, S_IRUSR | S_IWUSR) != 0)
    {
        rmdir(directory);
        return 1;
    }

    memset(&options, 0, sizeof(options));
    options.path = fifo_path;
    options.mode = ENT_SHM_MODE_READ_ONLY;

    failed |= expect_true(ENT_SharedMapOpen(&options, &map) ==
                              ENT_SHM_PATH_FAILED,
                          "non-regular shared map path must be rejected");
    failed |= expect_true(map == NULL,
                          "rejected non-regular path must not return a map");

    cleanup_path(fifo_path);
    rmdir(directory);
    return failed;
}
#endif

int main(void)
{
#ifdef _WIN32
    return test_windows_security_boundaries() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
#else
    int failed = 0;

    failed |= test_created_file_is_private();
    failed |= test_symbolic_link_is_rejected();
    failed |= test_non_regular_file_is_rejected();

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
#endif
}
