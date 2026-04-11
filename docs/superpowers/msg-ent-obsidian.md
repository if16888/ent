---
title: ENT message system notes
tags: [msg-system, ent, obsidian]
---

# ENT Message System Reference

Replicate the key details from `docs/ent-msg.md` so Obsidians notes stay in sync.

## Highlights
- `MSG_ID_T` is signed 32-bit: high bit shows severity (1=error, 0=ok).
- Bits 30..23 encode the module id, 22..15 encode the submodule id, and 14..0 carry the inner counter.
- Module `ENT` uses short module/submodule names (`<=4 chars`) to keep generated macros readable.
- Negative codes mean errors, non-negative mean success/normal status.
- Query helpers: `ENT_MsgText`, `ENT_MsgModuleName`, `ENT_MsgSubmoduleName`, `ENT_MsgIsError`.
- Format helpers: `ENT_MsgFormat`/`ENT_MsgVFormat` to expand `%s` placeholders.
- Generated files live under `build/comm/generated/ent_msg_gen.{h,c}`; runtime helpers are `inc/ent_msg.h` + `comm/ent_msg.c`.

## .msg Maintenance
- File layout is: `module`, `submodule`, `symbol ok|err inner message text...`.
- Keep one message per line; align the columns for readability.
- Use early severity (`ok/err`) and unique inner ids within the submodule.
- `ENT` submodules: `SYS`, `INIT`, `RT`, `THRD`, `UTHD`, `SOCK`, `TMR`, `TPL`, `DLL`.
- Always read/edit `msg/ent.msg` first; run `cmake --build build` to regen the headers/sources.

## Workflow Tips
- Reference this Obsidian note before adding or renaming a macro.
- Link back to `docs/ent-msg.md` for the authoritative spec.
- Keep message text descriptive but lean on placeholders for runtime data.

