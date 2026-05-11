/*-----------------------------------------------------------------------------
 *   Copyright 2019 Fei Li
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
#include <windows.h>
#else
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "ent_msg.h"
#include "ent_shm.h"

struct ENT_SharedMap
{
    void*              ptr;
    ENT_SIZE           size;
    ENT_SharedMapMode  mode;
    int                memory_locked;
#ifdef WIN32
    HANDLE             file_handle;
    HANDLE             mapping_handle;
#else
    int                fd;
#endif
};

static MSG_ID_T iENT_SharedMapValidateOptions(const ENT_SharedMapOptions* options);
static MSG_ID_T iENT_SharedMapRelease(ENT_SharedMap* map);

#ifdef WIN32
static MSG_ID_T iENT_SharedMapOpenWindows(const ENT_SharedMapOptions* options, ENT_SharedMap** out_map);
static MSG_ID_T iENT_SharedMapFlushWindows(ENT_SharedMap* map, ENT_OFFSET offset, ENT_SIZE length);
#else
static MSG_ID_T iENT_SharedMapOpenPosix(const ENT_SharedMapOptions* options, ENT_SharedMap** out_map);
static MSG_ID_T iENT_SharedMapFlushPosix(ENT_SharedMap* map, ENT_OFFSET offset, ENT_SIZE length);
#endif

static MSG_ID_T iENT_SharedMapValidateOptions(const ENT_SharedMapOptions* options)
{
    if(options == NULL || options->path == NULL || options->path[0] == '\0')
    {
        return ENT_SHM_BAD_ARGUMENT;
    }

    if(options->mode != ENT_SHM_MODE_READ_ONLY &&
       options->mode != ENT_SHM_MODE_READ_WRITE)
    {
        return ENT_SHM_BAD_ARGUMENT;
    }

    if((options->flags & ~(ENT_SHM_F_CREATE_IF_MISSING |
                           ENT_SHM_F_TRUNCATE_IF_EXISTS |
                           ENT_SHM_F_LOCK_MEMORY)) != 0)
    {
        return ENT_SHM_BAD_ARGUMENT;
    }

    if(options->mode == ENT_SHM_MODE_READ_ONLY &&
       (options->flags & (ENT_SHM_F_CREATE_IF_MISSING | ENT_SHM_F_TRUNCATE_IF_EXISTS)))
    {
        return ENT_SHM_BAD_ARGUMENT;
    }

    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_SharedMapValidateNativeSize(ENT_SIZE size, size_t* native_size)
{
    if(native_size == NULL || size == 0ULL || size > (ENT_SIZE)SIZE_MAX)
    {
        return ENT_SHM_BAD_SIZE;
    }

    *native_size = (size_t)size;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_SharedMapRelease(ENT_SharedMap* map)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;

    if(map == NULL)
    {
        return ENT_SYS_NORMAL;
    }

    if(map->ptr != NULL)
    {
#ifdef WIN32
        if(map->memory_locked)
        {
            if(!VirtualUnlock(map->ptr, (SIZE_T)map->size))
            {
                sts = ENT_SHM_CLOSE_FAILED;
            }
        }
        if(!UnmapViewOfFile(map->ptr))
        {
            sts = ENT_SHM_CLOSE_FAILED;
        }
#else
        if(map->memory_locked)
        {
            if(munlock(map->ptr, (size_t)map->size) != 0)
            {
                sts = ENT_SHM_CLOSE_FAILED;
            }
        }
        if(munmap(map->ptr, (size_t)map->size) != 0)
        {
            sts = ENT_SHM_CLOSE_FAILED;
        }
#endif
        map->ptr = NULL;
    }

#ifdef WIN32
    if(map->mapping_handle != NULL)
    {
        if(!CloseHandle(map->mapping_handle))
        {
            sts = ENT_SHM_CLOSE_FAILED;
        }
        map->mapping_handle = NULL;
    }
    if(map->file_handle != INVALID_HANDLE_VALUE)
    {
        if(!CloseHandle(map->file_handle))
        {
            sts = ENT_SHM_CLOSE_FAILED;
        }
        map->file_handle = INVALID_HANDLE_VALUE;
    }
#else
    if(map->fd >= 0)
    {
        if(close(map->fd) != 0)
        {
            sts = ENT_SHM_CLOSE_FAILED;
        }
        map->fd = -1;
    }
#endif

    return sts;
}

#ifdef WIN32
static MSG_ID_T iENT_SharedMapOpenWindows(const ENT_SharedMapOptions* options, ENT_SharedMap** out_map)
{
    ENT_SharedMap* map = NULL;
    HANDLE file_handle = INVALID_HANDLE_VALUE;
    HANDLE mapping_handle = NULL;
    void* view_ptr = NULL;
    DWORD desired_access = 0;
    DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    DWORD creation_disposition = OPEN_EXISTING;
    DWORD create_error = 0;
    DWORD protect = 0;
    DWORD map_access = 0;
    LARGE_INTEGER file_size;
    ENT_SIZE final_size = 0ULL;
    size_t native_size = 0;
    BOOL created_new = FALSE;
    int create_if_missing = 0;
    int truncate_if_exists = 0;

    create_if_missing = (options->flags & ENT_SHM_F_CREATE_IF_MISSING) != 0;
    truncate_if_exists = (options->flags & ENT_SHM_F_TRUNCATE_IF_EXISTS) != 0;
    desired_access = (options->mode == ENT_SHM_MODE_READ_ONLY) ?
        GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    creation_disposition = create_if_missing ? OPEN_ALWAYS : OPEN_EXISTING;

    file_handle = CreateFileA(options->path,
                              desired_access,
                              share_mode,
                              NULL,
                              creation_disposition,
                              FILE_ATTRIBUTE_NORMAL,
                              NULL);
    if(file_handle == INVALID_HANDLE_VALUE)
    {
        return ENT_SHM_PATH_FAILED;
    }

    create_error = GetLastError();
    if(options->mode == ENT_SHM_MODE_READ_ONLY)
    {
        if(!GetFileSizeEx(file_handle, &file_size))
        {
            CloseHandle(file_handle);
            return ENT_SHM_PATH_FAILED;
        }
        if(file_size.QuadPart <= 0)
        {
            CloseHandle(file_handle);
            return ENT_SHM_BAD_SIZE;
        }
        final_size = (ENT_SIZE)file_size.QuadPart;
    }
    else
    {
        if(create_if_missing &&
           create_error != ERROR_ALREADY_EXISTS &&
           create_error != ERROR_FILE_EXISTS)
        {
            created_new = TRUE;
        }

        if(truncate_if_exists || created_new)
        {
            final_size = options->size;
            if(final_size == 0ULL || final_size > (ENT_SIZE)LLONG_MAX)
            {
                CloseHandle(file_handle);
                return ENT_SHM_BAD_SIZE;
            }

            file_size.QuadPart = (LONGLONG)final_size;
            if(!SetFilePointerEx(file_handle, file_size, NULL, FILE_BEGIN) ||
               !SetEndOfFile(file_handle))
            {
                CloseHandle(file_handle);
                return ENT_SHM_RESIZE_FAILED;
            }
        }
        else
        {
            if(!GetFileSizeEx(file_handle, &file_size))
            {
                CloseHandle(file_handle);
                return ENT_SHM_PATH_FAILED;
            }
            if(file_size.QuadPart <= 0)
            {
                CloseHandle(file_handle);
                return ENT_SHM_BAD_SIZE;
            }
            final_size = (ENT_SIZE)file_size.QuadPart;
        }
    }

    if(iENT_SharedMapValidateNativeSize(final_size, &native_size) != ENT_SYS_NORMAL)
    {
        CloseHandle(file_handle);
        return ENT_SHM_BAD_SIZE;
    }

    protect = (options->mode == ENT_SHM_MODE_READ_ONLY) ? PAGE_READONLY : PAGE_READWRITE;
    map_access = (options->mode == ENT_SHM_MODE_READ_ONLY) ?
        FILE_MAP_READ : (FILE_MAP_READ | FILE_MAP_WRITE);

    mapping_handle = CreateFileMappingA(file_handle,
                                        NULL,
                                        protect,
                                        (DWORD)(final_size >> 32),
                                        (DWORD)(final_size & 0xffffffffULL),
                                        NULL);
    if(mapping_handle == NULL)
    {
        CloseHandle(file_handle);
        return ENT_SHM_MAP_FAILED;
    }

    view_ptr = MapViewOfFile(mapping_handle, map_access, 0, 0, native_size);
    if(view_ptr == NULL)
    {
        CloseHandle(mapping_handle);
        CloseHandle(file_handle);
        return ENT_SHM_MAP_FAILED;
    }

    map = (ENT_SharedMap*)malloc(sizeof(*map));
    if(map == NULL)
    {
        UnmapViewOfFile(view_ptr);
        CloseHandle(mapping_handle);
        CloseHandle(file_handle);
        return ENT_SHM_ALLOC_FAILED;
    }

    memset(map, 0, sizeof(*map));
    map->ptr = view_ptr;
    map->size = final_size;
    map->mode = options->mode;
    map->file_handle = file_handle;
    map->mapping_handle = mapping_handle;
    map->memory_locked = 0;
    if(options->flags & ENT_SHM_F_LOCK_MEMORY)
    {
        if(VirtualLock(view_ptr, native_size))
        {
            map->memory_locked = 1;
        }
    }

    *out_map = map;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_SharedMapFlushWindows(ENT_SharedMap* map, ENT_OFFSET offset, ENT_SIZE length)
{
    SIZE_T native_length = 0;
    void* flush_ptr = NULL;

    if(map == NULL || map->ptr == NULL)
    {
        return ENT_SHM_BAD_ARGUMENT;
    }

    if(length == 0ULL)
    {
        flush_ptr = map->ptr;
        native_length = (SIZE_T)map->size;
    }
    else
    {
        if(offset > map->size || length > (map->size - offset))
        {
            return ENT_SHM_RANGE_FAILED;
        }
        if(iENT_SharedMapValidateNativeSize(length, &native_length) != ENT_SYS_NORMAL)
        {
            return ENT_SHM_BAD_SIZE;
        }
        flush_ptr = (void*)((unsigned char*)map->ptr + (SIZE_T)offset);
    }

    if(!FlushViewOfFile(flush_ptr, native_length))
    {
        return ENT_SHM_FLUSH_FAILED;
    }

    if(map->mode == ENT_SHM_MODE_READ_WRITE && map->file_handle != INVALID_HANDLE_VALUE)
    {
        if(!FlushFileBuffers(map->file_handle))
        {
            return ENT_SHM_FLUSH_FAILED;
        }
    }

    return ENT_SYS_NORMAL;
}
#else
static MSG_ID_T iENT_SharedMapOpenPosix(const ENT_SharedMapOptions* options, ENT_SharedMap** out_map)
{
    ENT_SharedMap* map = NULL;
    struct stat st;
    int file_existed = 0;
    int fd = -1;
    int open_flags = 0;
    int prot = 0;
    ENT_SIZE final_size = 0ULL;
    size_t native_size = 0;
    void* view_ptr = NULL;
    int create_if_missing = 0;
    int truncate_if_exists = 0;

    create_if_missing = (options->flags & ENT_SHM_F_CREATE_IF_MISSING) != 0;
    truncate_if_exists = (options->flags & ENT_SHM_F_TRUNCATE_IF_EXISTS) != 0;
    if(stat(options->path, &st) == 0)
    {
        file_existed = 1;
    }
    else if(errno != ENOENT)
    {
        return ENT_SHM_PATH_FAILED;
    }

    if(options->mode == ENT_SHM_MODE_READ_ONLY)
    {
        if(!file_existed)
        {
            return ENT_SHM_PATH_FAILED;
        }

        open_flags = O_RDONLY;
        prot = PROT_READ;
    }
    else
    {
        open_flags = O_RDWR;
        prot = PROT_READ | PROT_WRITE;
        if(create_if_missing)
        {
            open_flags |= O_CREAT;
        }
        if(!file_existed && !create_if_missing)
        {
            return ENT_SHM_PATH_FAILED;
        }
    }

    fd = open(options->path, open_flags, 0666);
    if(fd < 0)
    {
        return ENT_SHM_PATH_FAILED;
    }

    if(options->mode == ENT_SHM_MODE_READ_ONLY)
    {
        if(fstat(fd, &st) != 0)
        {
            close(fd);
            return ENT_SHM_PATH_FAILED;
        }
        if(st.st_size <= 0)
        {
            close(fd);
            return ENT_SHM_BAD_SIZE;
        }
        final_size = (ENT_SIZE)st.st_size;
    }
    else
    {
        if(truncate_if_exists || !file_existed)
        {
            final_size = options->size;
            if(final_size == 0ULL || final_size > (ENT_SIZE)LLONG_MAX)
            {
                close(fd);
                return ENT_SHM_BAD_SIZE;
            }
            if(ftruncate(fd, (off_t)final_size) != 0)
            {
                close(fd);
                return ENT_SHM_RESIZE_FAILED;
            }
        }
        else
        {
            if(fstat(fd, &st) != 0)
            {
                close(fd);
                return ENT_SHM_PATH_FAILED;
            }
            if(st.st_size <= 0)
            {
                close(fd);
                return ENT_SHM_BAD_SIZE;
            }
            final_size = (ENT_SIZE)st.st_size;
        }
    }

    if(iENT_SharedMapValidateNativeSize(final_size, &native_size) != ENT_SYS_NORMAL)
    {
        close(fd);
        return ENT_SHM_BAD_SIZE;
    }

    view_ptr = mmap(NULL, native_size, prot, MAP_SHARED, fd, 0);
    if(view_ptr == MAP_FAILED)
    {
        close(fd);
        return ENT_SHM_MAP_FAILED;
    }

    map = (ENT_SharedMap*)malloc(sizeof(*map));
    if(map == NULL)
    {
        munmap(view_ptr, native_size);
        close(fd);
        return ENT_SHM_ALLOC_FAILED;
    }

    memset(map, 0, sizeof(*map));
    map->ptr = view_ptr;
    map->size = final_size;
    map->mode = options->mode;
    map->fd = fd;
    map->memory_locked = 0;
    if(options->flags & ENT_SHM_F_LOCK_MEMORY)
    {
        if(mlock(view_ptr, native_size) == 0)
        {
            map->memory_locked = 1;
        }
    }

    *out_map = map;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_SharedMapFlushPosix(ENT_SharedMap* map, ENT_OFFSET offset, ENT_SIZE length)
{
    ENT_OFFSET flush_offset = 0ULL;
    size_t native_length = 0;
    void* flush_ptr = NULL;

    if(map == NULL || map->ptr == NULL)
    {
        return ENT_SHM_BAD_ARGUMENT;
    }

    if(length == 0ULL)
    {
        flush_offset = 0ULL;
        if(iENT_SharedMapValidateNativeSize(map->size, &native_length) != ENT_SYS_NORMAL)
        {
            return ENT_SHM_BAD_SIZE;
        }
    }
    else
    {
        if(offset > map->size || length > (map->size - offset))
        {
            return ENT_SHM_RANGE_FAILED;
        }
        flush_offset = offset;
        if(iENT_SharedMapValidateNativeSize(length, &native_length) != ENT_SYS_NORMAL)
        {
            return ENT_SHM_BAD_SIZE;
        }
    }

    flush_ptr = (void*)((unsigned char*)map->ptr + (size_t)flush_offset);
    if(msync(flush_ptr, native_length, MS_SYNC) != 0)
    {
        return ENT_SHM_FLUSH_FAILED;
    }

    return ENT_SYS_NORMAL;
}
#endif

ENT_PUBLIC MSG_ID_T ENT_SharedMapOpen(const ENT_SharedMapOptions* options, ENT_SharedMap** out_map)
{
    if(out_map != NULL)
    {
        *out_map = NULL;
    }

    if(iENT_SharedMapValidateOptions(options) != 0 || out_map == NULL)
    {
        return ENT_SHM_BAD_ARGUMENT;
    }

#ifdef WIN32
    return iENT_SharedMapOpenWindows(options, out_map);
#else
    return iENT_SharedMapOpenPosix(options, out_map);
#endif
}

ENT_PUBLIC void* ENT_SharedMapPtr(ENT_SharedMap* map)
{
    if(map == NULL)
    {
        return NULL;
    }

    return map->ptr;
}

ENT_PUBLIC ENT_SIZE ENT_SharedMapSize(ENT_SharedMap* map)
{
    if(map == NULL)
    {
        return 0ULL;
    }

    return map->size;
}

ENT_PUBLIC MSG_ID_T ENT_SharedMapFlush(ENT_SharedMap* map, ENT_OFFSET offset, ENT_SIZE length)
{
    if(map == NULL)
    {
        return ENT_SHM_BAD_ARGUMENT;
    }

#ifdef WIN32
    return iENT_SharedMapFlushWindows(map, offset, length);
#else
    return iENT_SharedMapFlushPosix(map, offset, length);
#endif
}

ENT_PUBLIC MSG_ID_T ENT_SharedMapClose(ENT_SharedMap** map)
{
    MSG_ID_T sts = ENT_SYS_NORMAL;
    ENT_SharedMap* map_to_close = NULL;

    if(map == NULL || *map == NULL)
    {
        return ENT_SYS_NORMAL;
    }

    map_to_close = *map;
    sts = iENT_SharedMapRelease(map_to_close);
    memset(map_to_close, 0, sizeof(*map_to_close));
    free(map_to_close);
    *map = NULL;
    return sts;
}
