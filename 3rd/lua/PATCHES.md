# Lua 5.4.8 downstream patches

The vendored source is Lua 5.4.8 plus the upstream corrections below. Links
point to the official `lua/lua` mirror referenced by
<https://www.lua.org/bugs.html>. Each correction stays as a small auditable
delta until it is included in a later official release.

## Lua 5.4.8 release errata

| Official issue | Upstream commit | Files | ent exposure and proof |
|---|---|---|---|
| 5.4.8-1: all-weak table metatable GC | `1b0f943da7dfb25987456a77259edbeea0b94edc` | `lgc.c` | Lua table/metatable support; vendored source compiles under ASan/UBSan. |
| 5.4.8-2: `--` without a script | `9ac9d23f4164fc7e1eeea8a3e8d4e453dade51ab` | `lua.c` | Standalone interpreter is not built by ent; patch retained for source completeness. |
| 5.4.8-3: constructor counter overflow | `934fdd481ced3a9d4a7aaace4479ce889ab23582` | `lparser.c` | Text parser is used by `ENT_ScriptReload`; parser and sanitizer tests compile and pass. |

Lua 5.4.8 issue 4 (a possible finalizer leak when no stack space is available)
still has no published patch on the official bug page. The ent Lua backend is
disabled by default and its script environment does not expose
`collectgarbage`.

## Cross-version fixes discovered after Lua 5.4.8

| Official issue | Upstream commit | Files | ent exposure and proof |
|---|---|---|---|
| 5.5.0-1: `collectgarbage("step")` overflow, present since 5.0 | `632a71b24d8661228a726deb5e1698e9638f96d8` | `lapi.c` | Backported to the 5.4 GC-step implementation; `collectgarbage` is also removed from the sandbox. |
| 5.5.0-3: invalid UTF-8 shift overflow, present since 5.4 | `10eb89d1141dc528806b32401e408e36fb2f3bf5` | `lutf8lib.c` | `utf8` is allowlisted; `test_ent_script` runs the official invalid-byte sequence. |
| 5.5.0-4: binary chunk load misses GC, present since 5.3 | `f1bb2773bba8b16f0f01c00e59a7be541ef88cb7` | `lapi.c` | Upstream fix backported; `ENT_ScriptReload` additionally uses text-only mode and rejects the Lua chunk signature. |
| 5.5.0-5: `string.gmatch` state after error, present since 5.3 | `efddc2309c5ff8a1842bea8a9c0d7d4a5d6e1e60` | `lstrlib.c` | `string` is allowlisted; `test_ent_script` repeats the official failing iterator call. |
| 5.5.0-7: incomplete auxiliary buffer metatable after OOM, present since 5.4 | `bc4bbcef651ba2870d6c68db16dc7d6ce6f68636` | `lauxlib.c` | Used internally by string buffers; official initialization ordering backported and sanitizer suite run. |
| 5.5.0-8: wrong `__newindex` write barrier, present since 5.4 | `b996f8fd1be7fb711cc6f754a31a1c87d2c2fd9b` | `lvm.c` | Base metatables are allowlisted; official saved-table write barrier strategy adapted to the 5.4 VM and sanitizer suite run. |

## ent-specific containment

- Lua is disabled unless `ENT_ENABLE_LUA=ON`.
- Only base, coroutine, table, string, math, and utf8 libraries are opened.
- Dynamic loading, package access, OS/I/O/debug access, and `collectgarbage` are
  removed from the script global environment.
- Script input is text-only and limited to 1 MiB.
- Lua heap usage is limited to 16 MiB and each load or call is limited to
  1,000,000 VM instructions.
