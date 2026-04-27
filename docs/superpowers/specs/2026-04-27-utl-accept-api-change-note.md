# `UTL_Accept` API Change Note

## Change

`UTL_Accept` now returns the accepted socket through an output parameter:

```c
ENT_PUBLIC MSG_ID_T UTL_Accept(
    UTL_D_SOCKET SocketListen,
    struct sockaddr* addr,
    int* addr_size,
    UTL_D_SOCKET* pSocketDesc);
```

## Usage

- Pass `pSocketDesc` to receive the accepted socket.
- Pass `addr` and `addr_size` if peer address data is needed.
- Pass `NULL` for `addr` and `addr_size` when only the accepted socket is needed.

## Migration

- Update old call sites that treated the return value as the accepted socket.
- Check the returned `MSG_ID_T` first, then use `*pSocketDesc`.
- On success, the implementation writes the accepted descriptor to `*pSocketDesc`.

## Reason

The old signature mixed the status code and accepted socket ownership across platforms. The new form makes the API consistent with the rest of the utility layer and allows the caller to retrieve the accepted descriptor explicitly.
