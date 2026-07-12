find_path(EntLua_INCLUDE_DIR lua.h PATH_SUFFIXES lua lua5.4)
find_library(EntLua_LIBRARY NAMES lua54 lua5.4 lua)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(EntLua
  REQUIRED_VARS EntLua_LIBRARY EntLua_INCLUDE_DIR)

if(EntLua_FOUND AND NOT TARGET EntLua::Lua)
  add_library(EntLua::Lua UNKNOWN IMPORTED)
  set_target_properties(EntLua::Lua PROPERTIES
    IMPORTED_LOCATION "${EntLua_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${EntLua_INCLUDE_DIR}")
endif()

mark_as_advanced(EntLua_INCLUDE_DIR EntLua_LIBRARY)
