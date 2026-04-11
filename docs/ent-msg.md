# ENT Msg Guide

## Overview

ENT uses a generated message-code mechanism for public return values.

- Every public return code maps to one stable integer value.
- Negative values mean error.
- Non-negative values mean normal or expected status.
- A code can be converted to text with `ENT_MsgText`.
- A format string message can be expanded with `ENT_MsgFormat`.

The source of truth is [`msg/ent.msg`](/Users/lifei/test/ent/msg/ent.msg).

## Encoding

`MSG_ID_T` is a signed 32-bit integer.

- `bit31`: sign bit, `1=error`, `0=ok`
- `bit30..23`: module id
- `bit22..15`: submodule id
- `bit14..0`: inner code

Current internal module policy:

- `0..15`: reserved for internal modules
- `16+`: reserved for external applications using the same mechanism

Compatibility rule:

- `ENT_SYS_NORMAL` is `0`
- `module=ENT`, `submodule=SYS`, `inner=0`

## .msg File Format

Each `.msg` file uses three line types:

```text
module    <MODULE_NAME>    <MODULE_ID>
submodule <SUBMODULE_NAME> <SUBMODULE_ID>
<MSG_NAME> <ok|err> <INNER_ID> <message text...>
```

Example:

```text
module ENT 0

submodule SOCK 5
BAD_ARGUMENT   err 4  invalid socket argument
CONNECT_FAILED err 7  connect socket failed
```

## Naming Rules

`module` and `submodule` are intentionally short for readable generated macros.

- `MODULE_NAME` must be uppercase
- `SUBMODULE_NAME` must be uppercase
- `MODULE_NAME` length must be `<= 4`
- `SUBMODULE_NAME` length must be `<= 4`
- `MSG_NAME` uses uppercase snake case

Current `ENT` submodules:

- `SYS`
- `INIT`
- `RT`
- `THRD`
- `UTHD`
- `SOCK`
- `TMR`
- `TPL`
- `DLL`

## Generated Macros

The generator produces macros in this shape:

```c
ENT_<SUBMODULE>_<MESSAGE>
```

Examples:

```c
ENT_SYS_NORMAL
ENT_INIT_INVALID_ARGUMENT
ENT_SOCK_CONNECT_FAILED
ENT_TMR_NOT_INITIALIZED
ENT_TPL_WORKER_CREATEFAIL
```

Generated files:

- header: `build/comm/generated/ent_msg_gen.h`
- source: `build/comm/generated/ent_msg_gen.c`

Public runtime helpers:

- [`ent_msg.h`](/Users/lifei/test/ent/inc/ent_msg.h)
- [`ent_msg.c`](/Users/lifei/test/ent/comm/ent_msg.c)

## Runtime APIs

Query helpers:

```c
const char* ENT_MsgText(MSG_ID_T code);
const char* ENT_MsgModuleName(MSG_ID_T code);
const char* ENT_MsgSubmoduleName(MSG_ID_T code);
bool ENT_MsgIsError(MSG_ID_T code);
```

Build helpers:

```c
MSG_ID_T ENT_MsgBuild(bool isError, unsigned int moduleId, unsigned int submoduleId, unsigned int innerCode);
MSG_ID_T ENT_MsgTryBuild(MSG_ID_T* outCode, bool isError, unsigned int moduleId, unsigned int submoduleId, unsigned int innerCode);
```

`ENT_MsgTryBuild` is the checked form and returns `ENT_SYS_NORMAL` only when all parts fit in the encoded bit-width.
`ENT_MsgBuild` keeps the direct return-value style, but now returns `ENT_SYS_INVALID_MSGCODE` instead of silently truncating invalid parts.

Formatting helpers:

```c
int ENT_MsgFormat(char* out, size_t outSize, MSG_ID_T code, ...);
int ENT_MsgVFormat(char* out, size_t outSize, MSG_ID_T code, va_list ap);
```

Example:

```c
char buf[128];
MSG_ID_T sts = ENT_INIT_INVALID_ARGUMENT;

ENT_MsgFormat(buf, sizeof(buf), sts, "demo", "/tmp/demo");
```

If the message text is:

```text
invalid init argument: name[%s] workPath[%s]
```

then `buf` becomes:

```text
invalid init argument: name[demo] workPath[/tmp/demo]
```

## Editing Workflow

When adding or changing a message:

1. Edit [`ent.msg`](/Users/lifei/test/ent/msg/ent.msg).
2. Put the message in the correct submodule block.
3. Choose a unique `INNER_ID` within that `severity + submodule`.
4. Use `ok` only for non-error statuses.
5. Use precise names such as `BIND_FAILED`, `WAIT_TIMEOUT`, `BAD_ARGUMENT`.
6. Rebuild so generated code is refreshed.

Build commands:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Editing Recommendations

Keep `.msg` easy to scan for humans.

- Use the existing block comments and separators.
- Keep one message per line.
- Prefer aligned columns when adding nearby items.
- Keep messages specific and API-facing.
- Put variable data in placeholders instead of hardcoding detail into the code path.

Good:

```text
CONNECT_FAILED err 7 connect socket failed, peer[%s]
```

Bad:

```text
FAILED err 7 failed
```

## Notes

- The generator rejects duplicate symbol names.
- The generator rejects duplicate `(severity, module, submodule, inner)` tuples.
- The generator rejects symbols with more than 4 underscore-separated parts.
- The generator now rejects module and submodule names longer than 4 characters.
