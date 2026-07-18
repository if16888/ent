# Lua 5.4.8 downstream patches

The vendored Lua 5.4.8 source includes the following upstream corrections from
the official Lua bug list at <https://www.lua.org/bugs.html#5.4.8>:

1. `lgc.c`: revisit all-weak table metatables during GC propagation.
2. `lua.c`: handle `--` without a following script name.
3. `lparser.c`: prevent constructor item counters from overflowing.

These changes are kept as a small, auditable downstream delta until they are
included in a later official Lua release. The fourth issue currently listed for
Lua 5.4.8 (a possible finalizer leak when no stack space is available) has no
published patch on the official bug page. ent's Lua backend remains disabled by
default and additionally enforces fixed memory and instruction budgets.
