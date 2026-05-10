#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "ent_shm.h"

#define TEST_SHM_SMALL_SIZE  ((ENT_SIZE)4096u)
#define TEST_SHM_LARGE_SIZE  ((ENT_SIZE)8192u)
#define TEST_SHM_SIZE_ZERO   ((ENT_SIZE)0u)
#define TEST_SHM_OFFSET_ZERO ((ENT_OFFSET)0u)

static int expect_true(int condition, const char* message)
{
    if(!condition)
    {
        fprintf(stderr, "%s\n", message);
        return 1;
    }

    return 0;
}

static int prepare_temp_path(char* path, size_t path_len)
{
#ifdef WIN32
    char temp_dir[MAX_PATH];
    char temp_file[MAX_PATH];

    if(GetTempPathA((DWORD)sizeof(temp_dir), temp_dir) == 0)
    {
        fprintf(stderr, "GetTempPathA failed\n");
        return 1;
    }

    if(GetTempFileNameA(temp_dir, "shm", 0, temp_file) == 0)
    {
        fprintf(stderr, "GetTempFileNameA failed\n");
        return 1;
    }

    DeleteFileA(temp_file);

    if(snprintf(path, path_len, "%s", temp_file) >= (int)path_len)
    {
        fprintf(stderr, "temporary path is too long\n");
        return 1;
    }

    DeleteFileA(path);
    return 0;
#else
    char temp_template[] = "/tmp/ent_shm_test_XXXXXX";
    int fd = mkstemp(temp_template);

    if(fd < 0)
    {
        perror("mkstemp");
        return 1;
    }

    close(fd);
    unlink(temp_template);

    if(snprintf(path, path_len, "%s", temp_template) >= (int)path_len)
    {
        fprintf(stderr, "temporary path is too long\n");
        return 1;
    }

    unlink(path);
    return 0;
#endif
}

static void cleanup_temp_path(const char* path)
{
#ifdef WIN32
    DeleteFileA(path);
#else
    unlink(path);
#endif
}

static int test_invalid_args(void)
{
    ENT_SharedMapOptions options;
    ENT_SharedMap* map = NULL;
    char path[512];

    memset(&options, 0, sizeof(options));
    memset(path, 0, sizeof(path));

    if(prepare_temp_path(path, sizeof(path)) != 0)
    {
        return 1;
    }

    options.path = path;
    options.size = TEST_SHM_SMALL_SIZE;
    options.mode = ENT_SHM_MODE_READ_WRITE;
    options.flags = ENT_SHM_F_CREATE_IF_MISSING | ENT_SHM_F_TRUNCATE_IF_EXISTS;

    if(expect_true(ENT_SharedMapOpen(NULL, &map) < 0,
                   "ENT_SharedMapOpen should reject NULL options") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    if(expect_true(ENT_SharedMapOpen(&options, NULL) < 0,
                   "ENT_SharedMapOpen should reject NULL out_map") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    options.path = NULL;
    if(expect_true(ENT_SharedMapOpen(&options, &map) < 0,
                   "ENT_SharedMapOpen should reject NULL path") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    options.path = path;
    options.size = TEST_SHM_SIZE_ZERO;
    options.flags = 0;
    if(expect_true(ENT_SharedMapOpen(&options, &map) < 0,
                   "ENT_SharedMapOpen should reject zero-sized missing files") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    options.size = TEST_SHM_SMALL_SIZE;
    options.flags = (ENT_FLAGS)0x80000000u;
    if(expect_true(ENT_SharedMapOpen(&options, &map) < 0,
                   "ENT_SharedMapOpen should reject unknown flags") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    if(expect_true(ENT_SharedMapClose(NULL) == 0,
                   "ENT_SharedMapClose should accept NULL") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    cleanup_temp_path(path);
    return 0;
}

static int test_create_map_write_read(void)
{
    ENT_SharedMapOptions options;
    ENT_SharedMap* map = NULL;
    ENT_SharedMap* read_only_map = NULL;
    char path[512];
    const char* magic = "ent-shm-magic";
    char* ptr = NULL;
    char* read_ptr = NULL;

    memset(&options, 0, sizeof(options));
    memset(path, 0, sizeof(path));

    if(prepare_temp_path(path, sizeof(path)) != 0)
    {
        return 1;
    }

    options.path = path;
    options.size = TEST_SHM_SMALL_SIZE;
    options.mode = ENT_SHM_MODE_READ_WRITE;
    options.flags = ENT_SHM_F_CREATE_IF_MISSING | ENT_SHM_F_TRUNCATE_IF_EXISTS;

    if(expect_true(ENT_SharedMapOpen(&options, &map) == 0,
                   "ENT_SharedMapOpen should create a writable mapping") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    if(expect_true(ENT_SharedMapSize(map) == TEST_SHM_SMALL_SIZE,
                   "ENT_SharedMapSize should report the requested size") != 0)
    {
        ENT_SharedMapClose(map);
        cleanup_temp_path(path);
        return 1;
    }

    ptr = (char*)ENT_SharedMapPtr(map);
    if(expect_true(ptr != NULL, "ENT_SharedMapPtr should return a valid pointer") != 0)
    {
        ENT_SharedMapClose(map);
        cleanup_temp_path(path);
        return 1;
    }

    memcpy(ptr, magic, strlen(magic) + 1);
    if(expect_true(ENT_SharedMapFlush(map, TEST_SHM_OFFSET_ZERO, TEST_SHM_SIZE_ZERO) == 0,
                   "ENT_SharedMapFlush should flush the full map when length is zero") != 0)
    {
        ENT_SharedMapClose(map);
        cleanup_temp_path(path);
        return 1;
    }

    if(expect_true(ENT_SharedMapClose(map) == 0,
                   "ENT_SharedMapClose should close the writable mapping") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }
    map = NULL;

    options.mode = ENT_SHM_MODE_READ_ONLY;
    options.size = TEST_SHM_SIZE_ZERO;
    options.flags = 0;

    if(expect_true(ENT_SharedMapOpen(&options, &read_only_map) == 0,
                   "ENT_SharedMapOpen should reopen the file read-only") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    if(expect_true(ENT_SharedMapSize(read_only_map) == TEST_SHM_SMALL_SIZE,
                   "ENT_SharedMapSize should report the existing file size") != 0)
    {
        ENT_SharedMapClose(read_only_map);
        cleanup_temp_path(path);
        return 1;
    }

    read_ptr = (char*)ENT_SharedMapPtr(read_only_map);
    if(expect_true(read_ptr != NULL, "read-only map should expose a pointer") != 0)
    {
        ENT_SharedMapClose(read_only_map);
        cleanup_temp_path(path);
        return 1;
    }

    if(expect_true(strcmp(read_ptr, magic) == 0,
                   "read-only mapping should still contain the flushed magic string") != 0)
    {
        ENT_SharedMapClose(read_only_map);
        cleanup_temp_path(path);
        return 1;
    }

    if(expect_true(ENT_SharedMapClose(read_only_map) == 0,
                   "ENT_SharedMapClose should close the read-only mapping") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    cleanup_temp_path(path);
    return 0;
}

static int test_open_existing_without_truncate(void)
{
    ENT_SharedMapOptions options;
    ENT_SharedMap* map = NULL;
    char path[512];
    char* ptr = NULL;

    memset(&options, 0, sizeof(options));
    memset(path, 0, sizeof(path));

    if(prepare_temp_path(path, sizeof(path)) != 0)
    {
        return 1;
    }

    options.path = path;
    options.size = TEST_SHM_LARGE_SIZE;
    options.mode = ENT_SHM_MODE_READ_WRITE;
    options.flags = ENT_SHM_F_CREATE_IF_MISSING | ENT_SHM_F_TRUNCATE_IF_EXISTS;

    if(expect_true(ENT_SharedMapOpen(&options, &map) == 0,
                   "ENT_SharedMapOpen should create the 8192-byte file") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    ptr = (char*)ENT_SharedMapPtr(map);
    if(expect_true(ptr != NULL, "writable mapping should expose a pointer") != 0)
    {
        ENT_SharedMapClose(map);
        cleanup_temp_path(path);
        return 1;
    }

    ptr[0] = 'x';
    if(expect_true(ENT_SharedMapFlush(map, TEST_SHM_OFFSET_ZERO, TEST_SHM_SIZE_ZERO) == 0,
                   "ENT_SharedMapFlush should succeed on a writable mapping") != 0)
    {
        ENT_SharedMapClose(map);
        cleanup_temp_path(path);
        return 1;
    }

    if(expect_true(ENT_SharedMapClose(map) == 0,
                   "ENT_SharedMapClose should close the initial mapping") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }
    map = NULL;

    options.size = TEST_SHM_SIZE_ZERO;
    options.flags = 0;

    if(expect_true(ENT_SharedMapOpen(&options, &map) == 0,
                   "ENT_SharedMapOpen should reopen an existing file without truncation") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    if(expect_true(ENT_SharedMapSize(map) >= TEST_SHM_LARGE_SIZE,
                   "ENT_SharedMapSize should preserve the existing file size") != 0)
    {
        ENT_SharedMapClose(map);
        cleanup_temp_path(path);
        return 1;
    }

    if(expect_true(ENT_SharedMapClose(map) == 0,
                   "ENT_SharedMapClose should close the reopened mapping") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    cleanup_temp_path(path);
    return 0;
}

static int test_multiple_map_same_file(void)
{
    ENT_SharedMapOptions writer_options;
    ENT_SharedMapOptions reader_options;
    ENT_SharedMap* writer = NULL;
    ENT_SharedMap* reader = NULL;
    char path[512];
    const char* payload = "shared-map";
    char* writer_ptr = NULL;
    char* reader_ptr = NULL;

    memset(&writer_options, 0, sizeof(writer_options));
    memset(&reader_options, 0, sizeof(reader_options));
    memset(path, 0, sizeof(path));

    if(prepare_temp_path(path, sizeof(path)) != 0)
    {
        return 1;
    }

    writer_options.path = path;
    writer_options.size = TEST_SHM_SMALL_SIZE;
    writer_options.mode = ENT_SHM_MODE_READ_WRITE;
    writer_options.flags = ENT_SHM_F_CREATE_IF_MISSING | ENT_SHM_F_TRUNCATE_IF_EXISTS;

    if(expect_true(ENT_SharedMapOpen(&writer_options, &writer) == 0,
                   "writer map should open") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    reader_options.path = path;
    reader_options.size = TEST_SHM_SIZE_ZERO;
    reader_options.mode = ENT_SHM_MODE_READ_ONLY;
    reader_options.flags = 0;

    if(expect_true(ENT_SharedMapOpen(&reader_options, &reader) == 0,
                   "reader map should open the same file") != 0)
    {
        ENT_SharedMapClose(writer);
        cleanup_temp_path(path);
        return 1;
    }

    writer_ptr = (char*)ENT_SharedMapPtr(writer);
    reader_ptr = (char*)ENT_SharedMapPtr(reader);

    if(expect_true(writer_ptr != NULL && reader_ptr != NULL,
                   "both mappings should expose pointers") != 0)
    {
        ENT_SharedMapClose(reader);
        ENT_SharedMapClose(writer);
        cleanup_temp_path(path);
        return 1;
    }

    memcpy(writer_ptr, payload, strlen(payload) + 1);
    if(expect_true(ENT_SharedMapFlush(writer, TEST_SHM_OFFSET_ZERO, TEST_SHM_SIZE_ZERO) == 0,
                   "writer flush should succeed") != 0)
    {
        ENT_SharedMapClose(reader);
        ENT_SharedMapClose(writer);
        cleanup_temp_path(path);
        return 1;
    }

    if(expect_true(strcmp(reader_ptr, payload) == 0,
                   "reader should observe the flushed payload") != 0)
    {
        ENT_SharedMapClose(reader);
        ENT_SharedMapClose(writer);
        cleanup_temp_path(path);
        return 1;
    }

    if(expect_true(ENT_SharedMapClose(reader) == 0,
                   "reader mapping should close") != 0)
    {
        ENT_SharedMapClose(writer);
        cleanup_temp_path(path);
        return 1;
    }

    if(expect_true(ENT_SharedMapClose(writer) == 0,
                   "writer mapping should close") != 0)
    {
        cleanup_temp_path(path);
        return 1;
    }

    cleanup_temp_path(path);
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_invalid_args();
    failures += test_create_map_write_read();
    failures += test_open_existing_without_truncate();
    failures += test_multiple_map_same_file();

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
