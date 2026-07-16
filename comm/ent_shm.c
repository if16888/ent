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

#include <stdint.h>
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
static MSG_ID_T iENT_SharedMapValidateNativeSize(ENT_SIZE size, size_t* native_size);
static MSG_ID_T iENT_SharedMapRelease(ENT_SharedMap* map);

#ifdef WIN32
static MSG_ID_T iENT_SharedMapOpenWindows(const ENT_SharedMapOptions* options,
                                           ENT_SharedMap** out_map);
static MSG_ID_T iENT_SharedMapFlushWindows(ENT_SharedMap* map,
                                            ENT_OFFSET offset,
                                            ENT_SIZE length);
#else
static MSG_ID_T iENT_SharedMapOpenPosix(const ENT_SharedMapOptions* options,
                                         ENT_SharedMap** out_map);
static MSG_ID_T iENT_SharedMapFlushPosix(ENT_SharedMap* map,
                                          ENT_OFFSET offset,
                                          ENT_SIZE length);
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
       (options->flags & (ENT_SHM_F_CREATE_IF_MISSING |
                          ENT_SHM_F_TRUNCATE_IF_EXISTS)))
    {
        return ENT_SHM_BAD_ARGUMENT;
    }

    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_SharedMapValidateNativeSize(ENT_SIZE size,
                                                  size_t* native_size)
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
        if(map->memory_locked && !VirtualUnlock(map->ptr, (SIZE_T)map->size))
        {
            sts = ENT_SHM_CLOSE_FAILED;
        }
        if(!UnmapViewOfFile(map->ptr))
        {
            sts = ENT_SHM_CLOSE_FAILED;
        }
#else
        if(map->memory_locked && munlock(map->ptr, (size_t)map->size) != 0)
        {
            sts = ENT_SHM_CLOSE_FAILED;
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
static int iENT_SharedMapWindowsHandleIsRegular(HANDLE file_handle)
{
    FILE_ATTRIBUTE_TAG_INFO tag_info;

    memset(&tag_info, 0, sizeof(tag_info));
    if(!GetFileInformationByHandleEx(file_handle,
                                     FileAttributeTagInfo,
                                     &tag_info,
                                     sizeof(tag_info)))
    {
        return 0;
    }

    return (tag_info.FileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
}

static MSG_ID_T iENT_SharedMapOpenWindows(const ENT_SharedMapOptions* options,
                                           ENT_SharedMap** out_map)
{
    ENT_SharedMap* map = NULL;
    HANDLE file_handle = INVALID_HANDLE_VALUE;
    HANDLE mapping_handle = NULL;
    void* view_ptr = NULL;
    DWORD desired_access;
    DWORD creation_disposition;
    DWORD create_error;
    DWORD protect;
    DWORD map_access;
    LARGE_INTEGER file_size;
    ENT_SIZE final_size = 0ULL;
    size_t native_size = 0;
    BOOL created_new = FALSE;
    int create_if_missing;
    int truncate_if_exists;

    create_if_missing =
        (options->flags & ENT_SHM_F_CREATE_IF_MISSING) != 0;
    truncate_if_exists =
        (options->flags & ENT_SHM_F_TRUNCATE_IF_EXISTS) != 0;
    desired_access = options->mode == ENT_SHM_MODE_READ_ONLY ?
        GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    creation_disposition = create_if_missing ? OPEN_ALWAYS : OPEN_EXISTING;

    file_handle = CreateFileA(options->path,
                              desired_access,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              NULL,
                              creation_disposition,
                              FILE_ATTRIBUTE_NORMAL,
                              NULL);
    if(file_handle == INVALID_HANDLE_VALUE)
    {
        return ENT_SHM_PATH_FAILED;
    }

    if(!iENT_SharedMapWindowsHandleIsRegular(file_handle))
    {
        CloseHandle(file_handle);
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

    if(iENT_SharedMapValidateNativeSize(final_size, &native_size) !=
       ENT_SYS_NORMAL)
    {
        CloseHandle(file_handle);
        return ENT_SHM_BAD_SIZE;
    }

    protect = options->mode == ENT_SHM_MODE_READ_ONLY ?
        PAGE_READONLY : PAGE_READWRITE;
    map_access = options->mode == ENT_SHM_MODE_READ_ONLY ?
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

    map = (ENT_SharedMap*)calloc(1u, sizeof(*map));
    if(map == NULL)
    {
        UnmapViewOfFile(view_ptr);
        CloseHandle(mapping_handle);
        CloseHandle(file_handle);
        return ENT_SHM_ALLOC_FAILED;
    }

    map->ptr = view_ptr;
    map->size = final_size;
    map->mode = options->mode;
    map->file_handle = file_handle;
    map->mapping_handle = mapping_handle;
    if((options->flags & ENT_SHM_F_LOCK_MEMORY) != 0 &&
       VirtualLock(view_ptr, native_size))
    {
        map->memory_locked = 1;
    }

    *out_map = map;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_SharedMapFlushWindows(ENT_SharedMap* map,
                                            ENT_OFFSET offset,
                                            ENT_SIZE length)
{
    SIZE_T native_length;
    void* flush_ptr;

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
        if(offset > map->size || length > map->size - offset)
        {
            return ENT_SHM_RANGE_FAILED;
        }
        if(iENT_SharedMapValidateNativeSize(length, &native_length) !=
           ENT_SYS_NORMAL)
        {
            return ENT_SHM_BAD_SIZE;
        }
        flush_ptr = (void*)((unsigned char*)map->ptr + (SIZE_T)offset);
    }

    if(!FlushViewOfFile(flush_ptr, native_length))
    {
        return ENT_SHM_FLUSH_FAILED;
    }
    if(map->mode == ENT_SHM_MODE_READ_WRITE &&
       map->file_handle != INVALID_HANDLE_VALUE &&
       !FlushFileBuffers(map->file_handle))
    {
        return ENT_SHM_FLUSH_FAILED;
    }

    return ENT_SYS_NORMAL;
}
#else
static int iENT_SharedMapSetCloseOnExec(int fd)
{
#ifdef O_CLOEXEC
    (void)fd;
    return 0;
#else
    int descriptor_flags = fcntl(fd, F_GETFD);
    if(descriptor_flags < 0 ||
       fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0)
    {
        return -1;
    }
    return 0;
#endif
}

static int iENT_SharedMapPosixBaseFlags(int access_flags)
{
    int flags = access_flags | O_NONBLOCK;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    return flags;
}

static int iENT_SharedMapOpenExistingPosix(const char* path, int access_flags)
{
#ifndef O_NOFOLLOW
    struct stat path_stat;
    if(lstat(path, &path_stat) != 0 || S_ISLNK(path_stat.st_mode))
    {
        errno = ELOOP;
        return -1;
    }
#endif
    return open(path, iENT_SharedMapPosixBaseFlags(access_flags));
}

static int iENT_SharedMapOpenPosixFile(const char* path,
                                        int access_flags,
                                        int create_if_missing,
                                        int* out_created_new)
{
    int fd;

    *out_created_new = 0;
    if(!create_if_missing)
    {
        return iENT_SharedMapOpenExistingPosix(path, access_flags);
    }

    fd = open(path,
              iENT_SharedMapPosixBaseFlags(access_flags) |
                  O_CREAT | O_EXCL,
              S_IRUSR | S_IWUSR);
    if(fd >= 0)
    {
        *out_created_new = 1;
        return fd;
    }
    if(errno != EEXIST)
    {
        return -1;
    }

    return iENT_SharedMapOpenExistingPosix(path, access_flags);
}

static MSG_ID_T iENT_SharedMapOpenPosix(const ENT_SharedMapOptions* options,
                                         ENT_SharedMap** out_map)
{
    ENT_SharedMap* map = NULL;
    struct stat st;
    int fd = -1;
    int access_flags;
    int prot;
    int created_new = 0;
    int create_if_missing;
    int truncate_if_exists;
    ENT_SIZE final_size = 0ULL;
    size_t native_size = 0;
    void* view_ptr;

    create_if_missing =
        (options->flags & ENT_SHM_F_CREATE_IF_MISSING) != 0;
    truncate_if_exists =
        (options->flags & ENT_SHM_F_TRUNCATE_IF_EXISTS) != 0;
    access_flags = options->mode == ENT_SHM_MODE_READ_ONLY ?
        O_RDONLY : O_RDWR;
    prot = options->mode == ENT_SHM_MODE_READ_ONLY ?
        PROT_READ : (PROT_READ | PROT_WRITE);

    fd = iENT_SharedMapOpenPosixFile(options->path,
                                     access_flags,
                                     create_if_missing,
                                     &created_new);
    if(fd < 0)
    {
        return ENT_SHM_PATH_FAILED;
    }
    if(iENT_SharedMapSetCloseOnExec(fd) != 0)
    {
        close(fd);
        return ENT_SHM_PATH_FAILED;
    }
    if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
    {
        close(fd);
        return ENT_SHM_PATH_FAILED;
    }

    if(options->mode == ENT_SHM_MODE_READ_ONLY)
    {
        if(st.st_size <= 0)
        {
            close(fd);
            return ENT_SHM_BAD_SIZE;
        }
        final_size = (ENT_SIZE)st.st_size;
    }
    else if(truncate_if_exists || created_new)
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
        if(st.st_size <= 0)
        {
            close(fd);
            return ENT_SHM_BAD_SIZE;
        }
        final_size = (ENT_SIZE)st.st_size;
    }

    if(iENT_SharedMapValidateNativeSize(final_size, &native_size) !=
       ENT_SYS_NORMAL)
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

    map = (ENT_SharedMap*)calloc(1u, sizeof(*map));
    if(map == NULL)
    {
        munmap(view_ptr, native_size);
        close(fd);
        return ENT_SHM_ALLOC_FAILED;
    }

    map->ptr = view_ptr;
    map->size = final_size;
    map->mode = options->mode;
    map->fd = fd;
    if((options->flags & ENT_SHM_F_LOCK_MEMORY) != 0 &&
       mlock(view_ptr, native_size) == 0)
    {
        map->memory_locked = 1;
    }

    *out_map = map;
    return ENT_SYS_NORMAL;
}

static MSG_ID_T iENT_SharedMapFlushPosix(ENT_SharedMap* map,
                                          ENT_OFFSET offset,
                                          ENT_SIZE length)
{
    ENT_OFFSET flush_offset = 0ULL;
    ENT_SIZE flush_length = length;
    size_t native_length;
    void* flush_ptr;

    if(map == NULL || map->ptr == NULL)
    {
        return ENT_SHM_BAD_ARGUMENT;
    }

    if(length == 0ULL)
    {
        if(iENT_SharedMapValidateNativeSize(map->size, &native_length) !=
           ENT_SYS_NORMAL)
        {
            return ENT_SHM_BAD_SIZE;
        }
    }
    else
    {
        long page_size;
        ENT_OFFSET page_size_offset;
        ENT_OFFSET aligned_offset;

        if(offset > map->size || length > map->size - offset)
        {
            return ENT_SHM_RANGE_FAILED;
        }

        page_size = sysconf(_SC_PAGESIZE);
        if(page_size <= 0)
        {
            return ENT_SHM_FLUSH_FAILED;
        }

        page_size_offset = (ENT_OFFSET)page_size;
        aligned_offset = offset - (offset % page_size_offset);
        flush_offset = aligned_offset;
        flush_length = length + (ENT_SIZE)(offset - aligned_offset);
        if(iENT_SharedMapValidateNativeSize(flush_length, &native_length) !=
           ENT_SYS_NORMAL)
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

ENT_PUBLIC MSG_ID_T ENT_SharedMapOpen(const ENT_SharedMapOptions* options,
                                      ENT_SharedMap** out_map)
{
    if(out_map != NULL)
    {
        *out_map = NULL;
    }

    if(iENT_SharedMapValidateOptions(options) != ENT_SYS_NORMAL ||
       out_map == NULL)
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
    return map == NULL ? NULL : map->ptr;
}

ENT_PUBLIC ENT_SIZE ENT_SharedMapSize(ENT_SharedMap* map)
{
    return map == NULL ? 0ULL : map->size;
}

ENT_PUBLIC MSG_ID_T ENT_SharedMapFlush(ENT_SharedMap* map,
                                       ENT_OFFSET offset,
                                       ENT_SIZE length)
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
    ENT_SharedMap* map_to_close;
    MSG_ID_T sts;

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
