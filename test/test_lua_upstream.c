#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static int run_chunk(lua_State *state, const char *source) {
  int status = luaL_loadbufferx(state, source, strlen(source),
                                "@lua-upstream-regressions", "t");
  if (status == LUA_OK) {
    status = lua_pcall(state, 0, 0, 0);
  }
  if (status != LUA_OK) {
    const char *message = lua_tostring(state, -1);
    fprintf(stderr, "Lua upstream regression failed: %s\n",
            message != NULL ? message : "unknown error");
    lua_pop(state, 1);
    return 0;
  }
  return 1;
}

int main(void) {
  static const char *const regressions =
      "do\n"
      "  local function overflow_step(n)\n"
      "    if n > 0 then return overflow_step(n - 1) end\n"
      "    collectgarbage('step', math.maxinteger)\n"
      "  end\n"
      "  overflow_step(26)\n"
      "end\n"
      "do\n"
      "  local invalid = string.char(255, 143, 143, 143, 143, 143, 143, 143)\n"
      "  assert(utf8.len(invalid) == nil)\n"
      "end\n"
      "do\n"
      "  local n = 20000\n"
      "  local iterator = string.gmatch(string.rep('a', n), string.rep('a?', n))\n"
      "  pcall(iterator)\n"
      "  pcall(iterator)\n"
      "end\n"
      "do\n"
      "  local parent = {}\n"
      "  parent.__newindex = parent\n"
      "  local child = setmetatable({}, parent)\n"
      "  collectgarbage()\n"
      "  child.answer = {}\n"
      "  collectgarbage('step')\n"
      "  assert(parent.answer ~= nil)\n"
      "end\n";
  lua_State *state = luaL_newstate();
  int ok;

  if (state == NULL) {
    fprintf(stderr, "failed to create Lua state\n");
    return EXIT_FAILURE;
  }

  luaL_openlibs(state);
  ok = run_chunk(state, regressions);
  lua_close(state);
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
