# ENT Shared Map

`ent_shm` is the portable file-backed shared-memory utility in `ent`.
It wraps the OS-specific mapping primitives behind a small C99 API so higher-level
code can reuse the same implementation on Windows, Linux, and macOS.

`ENT_SharedMap` is a caller-synchronized resource. It is intentionally a bare
shared-map handle, not a runtime-owned child resource.

## Why file-backed mmap instead of POSIX shm only

- A regular file gives the same fast shared-memory behavior as `shm_open`/`mmap`,
  but it is easier to inspect, recover, and version.
- Windows does not have POSIX `shm_open`, so a file-backed design keeps the API
  portable without adding a second abstraction for the same concept.
- A snapshot file can be deleted, copied, or archived with normal filesystem tools.
- Recovery is simpler because the backing file remains visible after a crash.

## Platform Mapping

`ENT_SharedMapOpen` returns `MSG_ID_T` and maps to the native primitives on each platform:

- Windows
  - `CreateFileA`
  - `CreateFileMappingA`
  - `MapViewOfFile`
  - `FlushViewOfFile`
  - `FlushFileBuffers`
  - `UnmapViewOfFile`
  - `CloseHandle`
- POSIX
  - `open`
  - `ftruncate`
  - `mmap`
  - `msync`
  - `munmap`
  - `close`

`ENT_SHM_F_LOCK_MEMORY` is best effort:

- Windows uses `VirtualLock`
- POSIX uses `mlock`

Lock failure does not fail the open call.

## Return Codes

- `ENT_SYS_NORMAL`: success.
- `ENT_SHM_BAD_ARGUMENT`: invalid options, NULL output pointer, or NULL map passed to flush.
- `ENT_SHM_BAD_SIZE`: invalid mapping size or flush span size.
- `ENT_SHM_PATH_FAILED`: path/open/stat access failed.
- `ENT_SHM_ALLOC_FAILED`: internal allocation failed.
- `ENT_SHM_RESIZE_FAILED`: backing file resize failed.
- `ENT_SHM_MAP_FAILED`: mapping creation failed.
- `ENT_SHM_RANGE_FAILED`: flush range is out of bounds.
- `ENT_SHM_FLUSH_FAILED`: flush to disk failed.
- `ENT_SHM_CLOSE_FAILED`: release of mapping resources failed.

`ENT_SharedMapClose(&map)` accepts a pointer to the map handle and sets `map`
to `NULL` after releasing the mapping.

The public lifecycle contract is conservative:

- callers must not invoke `ENT_SharedMapPtr()`, `ENT_SharedMapSize()`, or
  `ENT_SharedMapFlush()` concurrently with `ENT_SharedMapClose(&map)` on the same
  handle;
- once `ENT_SharedMapClose(&map)` succeeds, the caller's `map` variable becomes
  `NULL`;
- any raw pointer copied from a shared-map handle before close immediately loses
  its callable contract after close;
- there is no state machine, active-op counter, or registry behind
  `ENT_SharedMap` today;
- if runtime-owned shared-map management is ever needed, it should be designed
  as a separate task with state, registry, and activeOps support.

## API Example

```c
#include <stdio.h>
#include <string.h>
#include "ent_shm.h"

int main(void)
{
    ENT_SharedMapOptions opts;
    ENT_SharedMap* map = NULL;
    char* data = NULL;

    memset(&opts, 0, sizeof(opts));
    opts.path = "demo.fgnshm";
    opts.size = (ENT_SIZE)4096u;
    opts.mode = ENT_SHM_MODE_READ_WRITE;
    opts.flags = ENT_SHM_F_CREATE_IF_MISSING | ENT_SHM_F_TRUNCATE_IF_EXISTS;

    if(ENT_SharedMapOpen(&opts, &map) != ENT_SYS_NORMAL)
    {
        return 1;
    }

    data = (char*)ENT_SharedMapPtr(map);
    strcpy(data, "hello snapshot");
    if(ENT_SharedMapFlush(map, (ENT_OFFSET)0u, (ENT_SIZE)0u) != ENT_SYS_NORMAL)
    {
        ENT_SharedMapClose(&map);
        return 1;
    }
    if(ENT_SharedMapClose(&map) != ENT_SYS_NORMAL)
    {
        return 1;
    }
    return 0;
}
```

Reader side:

```c
ENT_SharedMapOptions opts;
ENT_SharedMap* map = NULL;
const char* data = NULL;

memset(&opts, 0, sizeof(opts));
opts.path = "demo.fgnshm";
opts.size = (ENT_SIZE)0u;
opts.mode = ENT_SHM_MODE_READ_ONLY;
opts.flags = 0;

if(ENT_SharedMapOpen(&opts, &map) == ENT_SYS_NORMAL)
{
    data = (const char*)ENT_SharedMapPtr(map);
    puts(data);
    ENT_SharedMapClose(&map);
}
```

The sequence above assumes caller-side serialization. It does not imply that
`ENT_SharedMapPtr()`, `ENT_SharedMapSize()`, `ENT_SharedMapFlush()`, and
`ENT_SharedMapClose(&map)` are safe to race on the same handle.

## fgn Integration Pattern

The expected fgn usage is a plain snapshot file such as `metadata.fgnshm`:

1. Writer opens the file with `ENT_SHM_MODE_READ_WRITE`.
2. Writer builds the in-memory snapshot layout.
3. Writer calls `ENT_SharedMapFlush` after publishing the update.
4. Reader opens the same file with `ENT_SHM_MODE_READ_ONLY`.
5. Reader consumes the snapshot without knowing any OS mapping details.

This keeps the file-mapping mechanics in `ent`, while the fgn layer owns only
the snapshot schema and update policy.

## Current Limits

- No complex concurrency lock protocol yet.
- No typed or versioned snapshot schema yet.
- No crash-consistent double-buffer or journal yet.
- No UTF-16 Windows path support yet; the API currently uses `CreateFileA`.
