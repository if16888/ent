# ENT Shared Map

`ent_shm` is the portable file-backed shared-memory utility in `ent`. It wraps the
OS-specific mapping primitives behind a small C99 API so higher-level code can reuse
the same implementation on Windows, Linux and macOS.

`ENT_SharedMap` is a caller-synchronized resource. It is intentionally a bare
shared-map handle, not a runtime-owned child resource and not a distributed state
platform.

## File-backed design

A regular file provides the same mapped-memory data path as `shm_open`/`mmap`, while
remaining visible for inspection, recovery, copying and format migration. Windows uses
file mapping over a regular file for the same cross-platform contract.

## Secure path contract

The backing path is a security boundary. `ENT_SharedMapOpen` now enforces the following
baseline rules:

- POSIX creation is atomic with `O_CREAT | O_EXCL` before falling back to an existing
  file;
- new POSIX files are created with owner-only `0600` permissions before umask;
- POSIX opens request `O_NOFOLLOW`, `O_CLOEXEC` and nonblocking inspection;
- the opened descriptor is validated with `fstat` and must refer to a regular file;
- symbolic links and non-regular objects such as FIFOs are rejected;
- Windows rejects directories and reparse-point handles before mapping.

Callers must still place backing files in a trusted directory. The API does not create
or validate the parent directory, enforce a service account, change permissions on an
existing file, or provide application-level authorization. Production applications
should use a dedicated non-public directory and explicitly validate any stronger owner,
group or ACL policy they require.

## Platform mapping

Windows uses `CreateFileA`, `CreateFileMappingA`, `MapViewOfFile`,
`FlushViewOfFile`, `FlushFileBuffers`, `UnmapViewOfFile` and `CloseHandle`.

POSIX uses `open`, `fstat`, `ftruncate`, `mmap`, `msync`, `munmap` and `close`.

`ENT_SHM_F_LOCK_MEMORY` is best effort: Windows uses `VirtualLock` and POSIX uses
`mlock`. Lock failure does not fail the open call.

## Lifecycle

- `ENT_SharedMapOpen` returns a mapped handle or a precise `ENT_SHM_*` error.
- `ENT_SharedMapPtr` and `ENT_SharedMapSize` expose the mapped address and size.
- `ENT_SharedMapFlush` flushes a selected range; a zero length means the whole map.
- `ENT_SharedMapClose(&map)` releases the mapping and sets the caller's handle to
  `NULL`.

Callers must not race pointer, size or flush operations with close on the same handle.
Any raw pointer copied before close loses its contract immediately after close.

## Example

```c
ENT_SharedMapOptions options;
ENT_SharedMap* map = NULL;

memset(&options, 0, sizeof(options));
options.path = "/var/lib/my-service/state.bin";
options.size = (ENT_SIZE)4096u;
options.mode = ENT_SHM_MODE_READ_WRITE;
options.flags = ENT_SHM_F_CREATE_IF_MISSING |
                ENT_SHM_F_TRUNCATE_IF_EXISTS;

if(ENT_SharedMapOpen(&options, &map) == ENT_SYS_NORMAL)
{
    memcpy(ENT_SharedMapPtr(map), "snapshot", 9u);
    ENT_SharedMapFlush(map, (ENT_OFFSET)0u, (ENT_SIZE)0u);
    ENT_SharedMapClose(&map);
}
```

## Current limits

- caller-side synchronization only;
- no typed or versioned schema in the core utility;
- no crash-consistent double buffer or journal;
- no multi-node consistency or replication;
- no UTF-16 Windows path API yet;
- existing-file ownership and ACL policy remain the caller's responsibility.
