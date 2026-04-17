cmake_minimum_required(VERSION 3.6 FATAL_ERROR)

project(ent C)

set(LIB_NAME_SHARE ent)
set(LIB_NAME_STATIC ent_s)
set(LIB_NAME_OBJS ent_objs)

if(UNIX AND NOT APPLE)
add_compile_definitions(_GNU_SOURCE)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(ENT_ENABLE_SQLITE "Enable SQLite backend" ON)
option(ENT_ENABLE_MYSQL "Enable MySQL backend" ON)
option(ENT_ENABLE_LUA "Enable Lua scripting backend" OFF)
option(ENT_ALLOW_VENDORED_DB_LIBS "Allow vendored DB libraries as a fallback" ON)
set(ENT_LUA_LINK_MODE "static" CACHE STRING "Lua link mode: static or shared")
set_property(CACHE ENT_LUA_LINK_MODE PROPERTY STRINGS static shared)

if(CMAKE_VERSION VERSION_LESS "3.12")
  find_package(PythonInterp REQUIRED)
  set(ENT_MSG_PYTHON_EXECUTABLE ${PYTHON_EXECUTABLE})
else()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(ENT_MSG_PYTHON_EXECUTABLE ${Python3_EXECUTABLE})
endif()

set(ENT_USING_VENDORED_SQLITE OFF CACHE INTERNAL "Using vendored SQLite library")
set(ENT_USING_VENDORED_MYSQL OFF CACHE INTERNAL "Using vendored MySQL library")
set(ENT_SQLITE_RUNTIME "" CACHE INTERNAL "SQLite runtime library")
set(ENT_MYSQL_RUNTIME "" CACHE INTERNAL "MySQL runtime library")
set(ENT_LUA_INCLUDE_DIR "" CACHE INTERNAL "Lua include directory")
set(ENT_LUA_LIBRARY "" CACHE INTERNAL "Lua shared library")
set(ENT_LUA_TARGET "" CACHE INTERNAL "Lua static target")

set(ENT_MSG_SPEC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../msg/ent.msg)
set(ENT_MSG_GENERATOR ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/gen_ent_msg.py)
set(ENT_MSG_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(ENT_MSG_GENERATED_HEADER ${ENT_MSG_GENERATED_DIR}/ent_msg_gen.h)
set(ENT_MSG_GENERATED_SOURCE ${ENT_MSG_GENERATED_DIR}/ent_msg_gen.c)

add_custom_command(
  OUTPUT
    ${ENT_MSG_GENERATED_HEADER}
    ${ENT_MSG_GENERATED_SOURCE}
  COMMAND
    ${ENT_MSG_PYTHON_EXECUTABLE}
    ${ENT_MSG_GENERATOR}
    --input ${ENT_MSG_SPEC_FILE}
    --header ${ENT_MSG_GENERATED_HEADER}
    --source ${ENT_MSG_GENERATED_SOURCE}
  DEPENDS
    ${ENT_MSG_SPEC_FILE}
    ${ENT_MSG_GENERATOR}
  COMMENT "Generating ent message tables"
  VERBATIM
)

set_source_files_properties(
  ${ENT_MSG_GENERATED_HEADER}
  ${ENT_MSG_GENERATED_SOURCE}
  PROPERTIES GENERATED TRUE
)

add_custom_target(
  ent_msg_codegen
  DEPENDS
    ${ENT_MSG_GENERATED_HEADER}
    ${ENT_MSG_GENERATED_SOURCE}
)

set(ENT_MSG_GENERATED_DIR ${ENT_MSG_GENERATED_DIR} PARENT_SCOPE)
set(ENT_MSG_GENERATED_SOURCE ${ENT_MSG_GENERATED_SOURCE} PARENT_SCOPE)

function(ent_export_dependency_settings)
if(DEFINED ENT_SQLITE_INCLUDE_DIR)
set(ENT_SQLITE_INCLUDE_DIR "${ENT_SQLITE_INCLUDE_DIR}" CACHE PATH "SQLite include directory" FORCE)
endif()
if(DEFINED ENT_SQLITE_LIBRARY)
set(ENT_SQLITE_LIBRARY "${ENT_SQLITE_LIBRARY}" CACHE FILEPATH "SQLite library" FORCE)
endif()
if(DEFINED ENT_MYSQL_INCLUDE_DIR)
set(ENT_MYSQL_INCLUDE_DIR "${ENT_MYSQL_INCLUDE_DIR}" CACHE PATH "MySQL include directory" FORCE)
endif()
if(DEFINED ENT_MYSQL_LIBRARY)
set(ENT_MYSQL_LIBRARY "${ENT_MYSQL_LIBRARY}" CACHE FILEPATH "MySQL library" FORCE)
endif()
set(ENT_ENABLE_SQLITE "${ENT_ENABLE_SQLITE}" CACHE BOOL "Enable SQLite backend" FORCE)
set(ENT_ENABLE_MYSQL "${ENT_ENABLE_MYSQL}" CACHE BOOL "Enable MySQL backend" FORCE)
set(ENT_ENABLE_LUA "${ENT_ENABLE_LUA}" CACHE BOOL "Enable Lua scripting backend" FORCE)
set(ENT_LUA_LINK_MODE "${ENT_LUA_LINK_MODE}" CACHE STRING "Lua link mode: static or shared" FORCE)
endfunction()


if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
add_definitions(-DUNICODE -D_UNICODE)
set(ENT_VENDORED_SQLITE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/sqlite)
set(ENT_VENDORED_SQLITE_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/sqlite/sqlite3.lib)
set(ENT_VENDORED_SQLITE_RUNTIME ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/sqlite/sqlite3.dll)
set(ENT_VENDORED_MYSQL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/mysql/include)
set(ENT_VENDORED_MYSQL_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/mysql/lib/libmysql.lib)
set(ENT_VENDORED_MYSQL_RUNTIME ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/mysql/lib/libmysql.dll)
if(ENT_ENABLE_SQLITE)
find_path(ENT_SQLITE_INCLUDE_DIR sqlite3.h)
find_library(ENT_SQLITE_LIBRARY NAMES sqlite3 sqlite3.lib)
if((NOT ENT_SQLITE_INCLUDE_DIR OR NOT ENT_SQLITE_LIBRARY) AND ENT_ALLOW_VENDORED_DB_LIBS)
if(NOT ENT_SQLITE_INCLUDE_DIR AND EXISTS ${ENT_VENDORED_SQLITE_INCLUDE_DIR}/sqlite3.h)
set(ENT_SQLITE_INCLUDE_DIR ${ENT_VENDORED_SQLITE_INCLUDE_DIR})
endif()
if(NOT ENT_SQLITE_LIBRARY AND EXISTS ${ENT_VENDORED_SQLITE_LIBRARY})
set(ENT_SQLITE_LIBRARY ${ENT_VENDORED_SQLITE_LIBRARY})
endif()
if(ENT_SQLITE_INCLUDE_DIR AND ENT_SQLITE_LIBRARY)
set(ENT_USING_VENDORED_SQLITE ON CACHE INTERNAL "Using vendored SQLite library" FORCE)
endif()
endif()
if(ENT_SQLITE_INCLUDE_DIR AND ENT_SQLITE_LIBRARY)
include_directories(${ENT_SQLITE_INCLUDE_DIR})
get_filename_component(ENT_SQLITE_LIBRARY_DIR ${ENT_SQLITE_LIBRARY} DIRECTORY)
find_file(ENT_SQLITE_RUNTIME NAMES sqlite3.dll HINTS ${ENT_SQLITE_LIBRARY_DIR} ${ENT_SQLITE_LIBRARY_DIR}/../bin NO_DEFAULT_PATH)
if(NOT ENT_SQLITE_RUNTIME AND EXISTS ${ENT_VENDORED_SQLITE_RUNTIME})
set(ENT_SQLITE_RUNTIME ${ENT_VENDORED_SQLITE_RUNTIME})
endif()
if(NOT ENT_SQLITE_RUNTIME)
find_file(ENT_SQLITE_RUNTIME NAMES sqlite3.dll)
endif()
if(ENT_SQLITE_RUNTIME)
set(ENT_SQLITE_RUNTIME ${ENT_SQLITE_RUNTIME} CACHE INTERNAL "SQLite runtime library" FORCE)
endif()
link_directories("${ENT_SQLITE_LIBRARY_DIR}")
else()
message(WARNING "SQLite headers or library not found; disabling SQLite backend.")
set(ENT_ENABLE_SQLITE OFF)
endif()
endif()
if(ENT_ENABLE_MYSQL)
find_path(ENT_MYSQL_INCLUDE_DIR mysql.h PATH_SUFFIXES mysql mariadb)
find_library(ENT_MYSQL_LIBRARY NAMES libmysql mysql libmariadb mariadb)
if((NOT ENT_MYSQL_INCLUDE_DIR OR NOT ENT_MYSQL_LIBRARY) AND ENT_ALLOW_VENDORED_DB_LIBS)
if(NOT ENT_MYSQL_INCLUDE_DIR AND EXISTS ${ENT_VENDORED_MYSQL_INCLUDE_DIR}/mysql.h)
set(ENT_MYSQL_INCLUDE_DIR ${ENT_VENDORED_MYSQL_INCLUDE_DIR})
endif()
if(NOT ENT_MYSQL_LIBRARY AND EXISTS ${ENT_VENDORED_MYSQL_LIBRARY})
set(ENT_MYSQL_LIBRARY ${ENT_VENDORED_MYSQL_LIBRARY})
endif()
if(ENT_MYSQL_INCLUDE_DIR AND ENT_MYSQL_LIBRARY)
set(ENT_USING_VENDORED_MYSQL ON CACHE INTERNAL "Using vendored MySQL library" FORCE)
endif()
endif()
if(ENT_MYSQL_INCLUDE_DIR AND ENT_MYSQL_LIBRARY)
include_directories(${ENT_MYSQL_INCLUDE_DIR})
get_filename_component(ENT_MYSQL_LIBRARY_DIR ${ENT_MYSQL_LIBRARY} DIRECTORY)
find_file(ENT_MYSQL_RUNTIME NAMES libmysql.dll mysql.dll libmariadb.dll mariadb.dll HINTS ${ENT_MYSQL_LIBRARY_DIR} ${ENT_MYSQL_LIBRARY_DIR}/../bin NO_DEFAULT_PATH)
if(NOT ENT_MYSQL_RUNTIME AND EXISTS ${ENT_VENDORED_MYSQL_RUNTIME})
set(ENT_MYSQL_RUNTIME ${ENT_VENDORED_MYSQL_RUNTIME})
endif()
if(NOT ENT_MYSQL_RUNTIME)
find_file(ENT_MYSQL_RUNTIME NAMES libmysql.dll mysql.dll libmariadb.dll mariadb.dll)
endif()
if(ENT_MYSQL_RUNTIME)
set(ENT_MYSQL_RUNTIME ${ENT_MYSQL_RUNTIME} CACHE INTERNAL "MySQL runtime library" FORCE)
endif()
link_directories("${ENT_MYSQL_LIBRARY_DIR}")
else()
message(WARNING "MySQL headers or library not found; disabling MySQL backend.")
set(ENT_ENABLE_MYSQL OFF)
endif()
endif()
link_directories(${CMAKE_PREFIX_PATH})
else()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
set(ENT_APPLE_SDK_INCLUDE "")
set(ENT_APPLE_SDK_LIB "")
set(ENT_PKGCONFIG_MYSQL_INCLUDE_DIRS "")
set(ENT_PKGCONFIG_MYSQL_LIBRARY_DIRS "")
set(ENT_PKGCONFIG_SQLITE_INCLUDE_DIRS "")
set(ENT_PKGCONFIG_SQLITE_LIBRARY_DIRS "")
if(APPLE)
if(CMAKE_OSX_SYSROOT)
set(ENT_APPLE_SDK_INCLUDE ${CMAKE_OSX_SYSROOT}/usr/include)
set(ENT_APPLE_SDK_LIB ${CMAKE_OSX_SYSROOT}/usr/lib)
else()
execute_process(
  COMMAND xcrun --sdk macosx --show-sdk-path
  OUTPUT_VARIABLE ENT_APPLE_SDK_ROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(ENT_APPLE_SDK_ROOT)
set(ENT_APPLE_SDK_INCLUDE ${ENT_APPLE_SDK_ROOT}/usr/include)
set(ENT_APPLE_SDK_LIB ${ENT_APPLE_SDK_ROOT}/usr/lib)
endif()
endif()
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
pkg_check_modules(ENT_PKGCONFIG_MYSQL QUIET mysqlclient)
if(NOT ENT_PKGCONFIG_MYSQL_FOUND)
pkg_check_modules(ENT_PKGCONFIG_MYSQL QUIET mariadb)
endif()
pkg_check_modules(ENT_PKGCONFIG_SQLITE QUIET sqlite3)
endif()
endif()
if(ENT_ENABLE_MYSQL)
if(APPLE)
find_path(ENT_MYSQL_INCLUDE_DIR mysql.h PATH_SUFFIXES mysql mariadb PATHS ${ENT_APPLE_SDK_INCLUDE})
find_library(ENT_MYSQL_LIBRARY NAMES mysqlclient mariadb libmariadb PATHS ${ENT_APPLE_SDK_LIB})
else()
find_path(
  ENT_MYSQL_INCLUDE_DIR
  mysql.h
  PATH_SUFFIXES mysql mariadb
  HINTS ${ENT_PKGCONFIG_MYSQL_INCLUDE_DIRS}
)
find_library(
  ENT_MYSQL_LIBRARY
  NAMES mysqlclient mariadb libmariadb
  HINTS ${ENT_PKGCONFIG_MYSQL_LIBRARY_DIRS}
)
endif()
if(ENT_MYSQL_INCLUDE_DIR AND ENT_MYSQL_LIBRARY)
include_directories(${ENT_MYSQL_INCLUDE_DIR})
else()
message(WARNING "MySQL headers or library not found; disabling MySQL backend.")
set(ENT_ENABLE_MYSQL OFF)
endif()
endif()
if(ENT_ENABLE_SQLITE)
if(APPLE)
find_path(ENT_SQLITE_INCLUDE_DIR sqlite3.h PATHS ${ENT_APPLE_SDK_INCLUDE})
find_library(ENT_SQLITE_LIBRARY NAMES sqlite3 PATHS ${ENT_APPLE_SDK_LIB})
else()
find_path(
  ENT_SQLITE_INCLUDE_DIR
  sqlite3.h
  HINTS ${ENT_PKGCONFIG_SQLITE_INCLUDE_DIRS}
)
find_library(
  ENT_SQLITE_LIBRARY
  NAMES sqlite3
  HINTS ${ENT_PKGCONFIG_SQLITE_LIBRARY_DIRS}
)
if(NOT ENT_SQLITE_INCLUDE_DIR AND ENT_ALLOW_VENDORED_DB_LIBS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/sqlite/sqlite3.h)
set(ENT_SQLITE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/sqlite)
endif()
endif()
if(ENT_SQLITE_INCLUDE_DIR AND ENT_SQLITE_LIBRARY)
include_directories(${ENT_SQLITE_INCLUDE_DIR})
else()
message(WARNING "SQLite headers or library not found; disabling SQLite backend.")
set(ENT_ENABLE_SQLITE OFF)
endif()
endif()
link_directories(${CMAKE_PREFIX_PATH})
endif()

if(ENT_ENABLE_LUA)
if(NOT (ENT_LUA_LINK_MODE STREQUAL "static" OR ENT_LUA_LINK_MODE STREQUAL "shared"))
message(FATAL_ERROR "ENT_LUA_LINK_MODE must be one of: static, shared")
endif()

set(ENT_LUA_VENDORED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/lua)
set(ENT_LUA_VENDORED_SRC_DIR ${ENT_LUA_VENDORED_DIR}/src)

if(ENT_LUA_LINK_MODE STREQUAL "static")
set(ENT_LUA_CORE_FILES
  lapi.c
  lauxlib.c
  lbaselib.c
  lcode.c
  lcorolib.c
  lctype.c
  ldblib.c
  ldebug.c
  ldo.c
  ldump.c
  lfunc.c
  lgc.c
  linit.c
  liolib.c
  llex.c
  lmathlib.c
  lmem.c
  loadlib.c
  lobject.c
  lopcodes.c
  loslib.c
  lparser.c
  lstate.c
  lstring.c
  lstrlib.c
  ltable.c
  ltablib.c
  ltm.c
  lua.c
  luac.c
  lundump.c
  lutf8lib.c
  lvm.c
  lzio.c
)
list(REMOVE_ITEM ENT_LUA_CORE_FILES lua.c luac.c)
set(ENT_LUA_SOURCE_FILES "")
set(ENT_LUA_SOURCE_MISSING OFF)

if(NOT EXISTS ${ENT_LUA_VENDORED_SRC_DIR}/lua.h)
set(ENT_LUA_SOURCE_MISSING ON)
endif()

foreach(lua_file ${ENT_LUA_CORE_FILES})
set(lua_path ${ENT_LUA_VENDORED_SRC_DIR}/${lua_file})
if(NOT EXISTS ${lua_path})
set(ENT_LUA_SOURCE_MISSING ON)
break()
endif()
list(APPEND ENT_LUA_SOURCE_FILES ${lua_path})
endforeach()

if(ENT_LUA_SOURCE_MISSING)
message(WARNING "Lua static source tree not found under 3rd/lua/src; disabling Lua backend.")
set(ENT_ENABLE_LUA OFF)
else()
add_library(ent_lua_static STATIC ${ENT_LUA_SOURCE_FILES})
set_target_properties(ent_lua_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(ent_lua_static PRIVATE ${ENT_LUA_VENDORED_SRC_DIR})
include_directories(${ENT_LUA_VENDORED_SRC_DIR})
if(UNIX)
target_link_libraries(ent_lua_static PRIVATE m)
endif()
set(ENT_LUA_TARGET ent_lua_static CACHE INTERNAL "Lua static target" FORCE)
endif()
else()
find_path(
  ENT_LUA_INCLUDE_DIR
  lua.h
  HINTS ${ENT_LUA_VENDORED_SRC_DIR}
)
find_library(
  ENT_LUA_LIBRARY
  NAMES lua5.4 lua54 lua
)
if(ENT_LUA_INCLUDE_DIR AND ENT_LUA_LIBRARY)
include_directories(${ENT_LUA_INCLUDE_DIR})
set(ENT_LUA_LIBRARY "${ENT_LUA_LIBRARY}" CACHE FILEPATH "Lua shared library" FORCE)
else()
message(WARNING "Lua shared library not found; disabling Lua backend.")
set(ENT_ENABLE_LUA OFF)
endif()
endif()
endif()

ent_export_dependency_settings()
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../inc)
include_directories(${ENT_MSG_GENERATED_DIR})
add_definitions(-DENT_ENABLE_SQLITE=$<BOOL:${ENT_ENABLE_SQLITE}>)
add_definitions(-DENT_ENABLE_MYSQL=$<BOOL:${ENT_ENABLE_MYSQL}>)
add_definitions(-DENT_ENABLE_LUA=$<BOOL:${ENT_ENABLE_LUA}>)

if(BUILD_SHARED_LIBS)
  add_definitions(-DENT_MODULE_BEING_COMPILED)
else()
  add_definitions(-DLINKING_LIBENT)
endif()

add_library(
  ${LIB_NAME_OBJS}
  OBJECT
  ent_init.c ent_db.c ent_db_sqlite.c ent_log.c ent_thread.c ent_msg.c ent_script.c
  utl_socket.c utl_thread.c utl_timer.c utl_dll.c utl_tpool.c
  ${ENT_MSG_GENERATED_SOURCE}
  )

add_dependencies(${LIB_NAME_OBJS} ent_msg_codegen)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
#    find_package(PkgConfig REQUIRED QUIET)
#    pkg_search_module(mysqlclient REQUIRED mysqlclient)
#    if(TARGET PkgConfig::mysqlclient)
#        message(STATUS "Found mysqlclient")
#	      include_directories(${mysqlclient_INCLUDE_DIRS})
#    endif()
#    pkg_search_module(sqlite3 REQUIRED sqlite3)
#    if(TARGET PkgConfig::sqlite3)
#        message(STATUS "Found sqlite3")
#	      include_directories(${sqlite3_INCLUDE_DIRS})
#    endif()

endif()


add_library(
  ${LIB_NAME_SHARE}
  SHARED
        $<TARGET_OBJECTS:${LIB_NAME_OBJS}>
  )

target_include_directories(
  ${LIB_NAME_SHARE}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../inc>
    $<BUILD_INTERFACE:${ENT_MSG_GENERATED_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_definitions(
  ${LIB_NAME_SHARE}
  PUBLIC
    ENT_ENABLE_SQLITE=$<BOOL:${ENT_ENABLE_SQLITE}>
    ENT_ENABLE_MYSQL=$<BOOL:${ENT_ENABLE_MYSQL}>
    ENT_ENABLE_PGSQL=$<BOOL:${PostgreSQL_FOUND}>
    ENT_ENABLE_LUA=$<BOOL:${ENT_ENABLE_LUA}>
)
  
  set_target_properties(${LIB_NAME_SHARE}
                         PROPERTIES
                          PUBLIC_HEADER "../inc/ent_comm.h;../inc/ent_init.h;../inc/ent_types.h;../inc/ent_db.h;../inc/ent_log.h;../inc/ent_thread.h;../inc/ent_utility.h;../inc/ent_msg.h;../inc/ent_script.h;${ENT_MSG_GENERATED_HEADER}"
                         )
  
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(${LIB_NAME_SHARE} PRIVATE Ws2_32 Winmm)
    if(ENT_ENABLE_SQLITE AND ENT_SQLITE_LIBRARY)
        target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_SQLITE_LIBRARY})
    endif()
    if(ENT_ENABLE_MYSQL AND ENT_MYSQL_LIBRARY)
        target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_MYSQL_LIBRARY})
    endif()
    if(PostgreSQL_FOUND)
        target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${PostgreSQL_LIBRARIES})
    endif()
    if(ENT_ENABLE_LUA)
        if(ENT_LUA_TARGET)
            target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_LUA_TARGET})
        elseif(ENT_LUA_LIBRARY)
            target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_LUA_LIBRARY})
        endif()
    endif()
else()
    target_link_libraries(${LIB_NAME_SHARE} PRIVATE pthread)
    if(ENT_ENABLE_MYSQL)
        target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_MYSQL_LIBRARY})
    endif()
    if(ENT_ENABLE_SQLITE)
        target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_SQLITE_LIBRARY})
    endif()
    if(PostgreSQL_FOUND)
        target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${PostgreSQL_LIBRARIES})
    endif()
    if(ENT_ENABLE_LUA)
        if(ENT_LUA_TARGET)
            target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_LUA_TARGET})
        elseif(ENT_LUA_LIBRARY)
            target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_LUA_LIBRARY})
        endif()
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(${LIB_NAME_SHARE} PRIVATE rt)
    endif()
endif()

add_library(
  ${LIB_NAME_STATIC}
  STATIC
        $<TARGET_OBJECTS:${LIB_NAME_OBJS}>
  )

target_include_directories(
  ${LIB_NAME_STATIC}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../inc>
    $<BUILD_INTERFACE:${ENT_MSG_GENERATED_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_definitions(
  ${LIB_NAME_STATIC}
  PUBLIC
    ENT_ENABLE_SQLITE=$<BOOL:${ENT_ENABLE_SQLITE}>
    ENT_ENABLE_MYSQL=$<BOOL:${ENT_ENABLE_MYSQL}>
    ENT_ENABLE_PGSQL=$<BOOL:${PostgreSQL_FOUND}>
    ENT_ENABLE_LUA=$<BOOL:${ENT_ENABLE_LUA}>
)

if(PostgreSQL_FOUND)
  target_link_libraries(${LIB_NAME_STATIC} PUBLIC ${PostgreSQL_LIBRARIES})
endif()

if(ENT_ENABLE_LUA)
  if(ENT_LUA_TARGET)
    target_link_libraries(${LIB_NAME_STATIC} PUBLIC ${ENT_LUA_TARGET})
  elseif(ENT_LUA_LIBRARY)
    target_link_libraries(${LIB_NAME_STATIC} PUBLIC ${ENT_LUA_LIBRARY})
  endif()
endif()

set(ENT_INSTALL_TARGETS ${LIB_NAME_SHARE} ${LIB_NAME_STATIC})
if(ENT_ENABLE_LUA AND ENT_LUA_TARGET)
  list(APPEND ENT_INSTALL_TARGETS ${ENT_LUA_TARGET})
endif()

install(
  TARGETS
      ${ENT_INSTALL_TARGETS}
  EXPORT
    entTargets
  ARCHIVE
    DESTINATION ${INSTALL_LIBDIR}
    COMPONENT devel
  RUNTIME
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT lib
  LIBRARY
    DESTINATION ${INSTALL_LIBDIR}
    COMPONENT devel
  PUBLIC_HEADER
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT devel
  )




