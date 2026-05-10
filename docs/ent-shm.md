# ENT Shared Map

`ent_shm` is the portable file-backed shared-memory utility in `ent`.
It wraps the OS-specific mapping primitives behind a small C99 API so higher-level
code can reuse the same implementation on Windows, Linux, and macOS.

## Why file-backed mmap instead of POSIX shm only

- A regular file gives the same fast shared-memory behavior as `shm_open`/`mmap`,
  but it is easier to inspect, recover, and version.
- Windows does not have POSIX `shm_open`, so a file-backed design keeps the API
  portable without adding a second abstraction for the same concept.
- A snapshot file can be deleted, copied, or archived with normal filesystem tools.
- Recovery is simpler because the backing file remains visible after a crash.

## Platform Mapping

`ENT_SharedMapOpen` maps to the native primitives on each platform:

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

    if(ENT_SharedMapOpen(&opts, &map) != 0)
    {
        return 1;
    }

    data = (char*)ENT_SharedMapPtr(map);
    strcpy(data, "hello snapshot");
    ENT_SharedMapFlush(map, (ENT_OFFSET)0u, (ENT_SIZE)0u);
    ENT_SharedMapClose(map);
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

if(ENT_SharedMapOpen(&opts, &map) == 0)
{
    data = (const char*)ENT_SharedMapPtr(map);
    puts(data);
    ENT_SharedMapClose(map);
}
```

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
