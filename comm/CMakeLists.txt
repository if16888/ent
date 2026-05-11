cmake_minimum_required(VERSION 3.6 FATAL_ERROR)

project(ent C)

set(LIB_NAME_SHARE ent)
set(LIB_NAME_STATIC ent_s)
set(LIB_NAME_OBJS_SHARE ent_objs_shared)
set(LIB_NAME_OBJS_STATIC ent_objs_static)

if(UNIX AND NOT APPLE)
  add_compile_definitions(_GNU_SOURCE)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(ENT_BUILD_SHARED "Build the shared ent library target" ${BUILD_SHARED_LIBS})
option(ENT_BUILD_STATIC "Build the static ent library target" ON)
option(ENT_ENABLE_SQLITE "Enable SQLite backend" ON)
option(ENT_ENABLE_MYSQL "Enable MySQL backend" ON)
option(ENT_ENABLE_LUA "Enable Lua scripting backend" OFF)
set(ENT_LUA_LINK_MODE "static" CACHE STRING "Lua link mode: static or shared")
set_property(CACHE ENT_LUA_LINK_MODE PROPERTY STRINGS static shared)

if(NOT ENT_BUILD_SHARED AND NOT ENT_BUILD_STATIC)
  message(FATAL_ERROR "At least one of ENT_BUILD_SHARED or ENT_BUILD_STATIC must be enabled.")
endif()

if(CMAKE_VERSION VERSION_LESS "3.12")
  find_package(PythonInterp REQUIRED)
  set(ENT_MSG_PYTHON_EXECUTABLE ${PYTHON_EXECUTABLE})
else()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(ENT_MSG_PYTHON_EXECUTABLE ${Python3_EXECUTABLE})
endif()

set(ENT_SQLITE_FOUND OFF CACHE INTERNAL "SQLite backend discovery result")
set(ENT_MYSQL_FOUND OFF CACHE INTERNAL "MySQL backend discovery result")
set(ENT_PGSQL_FOUND OFF CACHE INTERNAL "PostgreSQL backend discovery result")
set(ENT_SQLITE_RUNTIME "" CACHE INTERNAL "SQLite runtime library")
set(ENT_MYSQL_RUNTIME "" CACHE INTERNAL "MySQL runtime library")
set(ENT_PGSQL_RUNTIME "" CACHE INTERNAL "PostgreSQL runtime library")
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
  if(DEFINED ENT_PGSQL_INCLUDE_DIR)
    set(ENT_PGSQL_INCLUDE_DIR "${ENT_PGSQL_INCLUDE_DIR}" CACHE PATH "PostgreSQL include directory" FORCE)
  endif()
  if(DEFINED ENT_PGSQL_LIBRARY)
    set(ENT_PGSQL_LIBRARY "${ENT_PGSQL_LIBRARY}" CACHE FILEPATH "PostgreSQL library" FORCE)
  endif()
  set(ENT_ENABLE_SQLITE "${ENT_ENABLE_SQLITE}" CACHE BOOL "Enable SQLite backend" FORCE)
  set(ENT_ENABLE_MYSQL "${ENT_ENABLE_MYSQL}" CACHE BOOL "Enable MySQL backend" FORCE)
  set(ENT_ENABLE_PGSQL "${ENT_ENABLE_PGSQL}" CACHE BOOL "Enable PostgreSQL backend" FORCE)
  set(ENT_ENABLE_LUA "${ENT_ENABLE_LUA}" CACHE BOOL "Enable Lua scripting backend" FORCE)
  set(ENT_LUA_LINK_MODE "${ENT_LUA_LINK_MODE}" CACHE STRING "Lua link mode: static or shared" FORCE)
endfunction()

function(ent_prefer_release_library library_var)
  if(DEFINED ${library_var} AND ${library_var} MATCHES "/debug/lib/")
    string(REPLACE "/debug/lib/" "/lib/" ENT_RELEASE_LIBRARY_CANDIDATE "${${library_var}}")
    if(EXISTS "${ENT_RELEASE_LIBRARY_CANDIDATE}")
      set(${library_var} "${ENT_RELEASE_LIBRARY_CANDIDATE}" CACHE FILEPATH "${library_var}" FORCE)
      set(${library_var} "${ENT_RELEASE_LIBRARY_CANDIDATE}" PARENT_SCOPE)
    endif()
  endif()
endfunction()

function(ent_target_db_settings target definition_visibility include_visibility)
  target_compile_definitions(
    ${target}
    ${definition_visibility}
      ENT_ENABLE_SQLITE=$<BOOL:${ENT_ENABLE_SQLITE}>
      ENT_ENABLE_MYSQL=$<BOOL:${ENT_ENABLE_MYSQL}>
      ENT_ENABLE_PGSQL=$<BOOL:${ENT_ENABLE_PGSQL}>
      ENT_SQLITE_FOUND=$<BOOL:${ENT_SQLITE_FOUND}>
      ENT_MYSQL_FOUND=$<BOOL:${ENT_MYSQL_FOUND}>
      ENT_PGSQL_FOUND=$<BOOL:${ENT_PGSQL_FOUND}>
  )

  if(ENT_SQLITE_FOUND AND ENT_SQLITE_INCLUDE_DIR)
    target_include_directories(${target} ${include_visibility} ${ENT_SQLITE_INCLUDE_DIR})
  endif()
  if(ENT_MYSQL_FOUND AND ENT_MYSQL_INCLUDE_DIR)
    target_include_directories(${target} ${include_visibility} ${ENT_MYSQL_INCLUDE_DIR})
  endif()
  if(ENT_PGSQL_FOUND AND ENT_PGSQL_INCLUDE_DIR)
    target_include_directories(${target} ${include_visibility} ${ENT_PGSQL_INCLUDE_DIR})
  endif()
endfunction()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  add_definitions(-DUNICODE -D_UNICODE)
  if(ENT_ENABLE_SQLITE)
    find_path(ENT_SQLITE_INCLUDE_DIR sqlite3.h)
    find_library(ENT_SQLITE_LIBRARY NAMES sqlite3)
    if(ENT_SQLITE_INCLUDE_DIR AND ENT_SQLITE_LIBRARY)
      include_directories(${ENT_SQLITE_INCLUDE_DIR})
      get_filename_component(ENT_SQLITE_LIBRARY_DIR ${ENT_SQLITE_LIBRARY} DIRECTORY)
      if(WIN32)
        find_file(ENT_SQLITE_RUNTIME NAMES sqlite3.dll HINTS ${ENT_SQLITE_LIBRARY_DIR} ${ENT_SQLITE_LIBRARY_DIR}/../bin)
        if(ENT_SQLITE_RUNTIME)
          set(ENT_SQLITE_RUNTIME ${ENT_SQLITE_RUNTIME} CACHE INTERNAL "SQLite runtime library" FORCE)
        endif()
      endif()
      set(ENT_SQLITE_FOUND ON CACHE INTERNAL "SQLite backend discovery result" FORCE)
    else()
      message(FATAL_ERROR "SQLite headers or library not found; SQLite backend is required.")
    endif()
  endif()

  if(ENT_ENABLE_MYSQL)
    find_path(ENT_MYSQL_INCLUDE_DIR mysql.h PATH_SUFFIXES mysql mariadb)
    find_library(ENT_MYSQL_LIBRARY NAMES mysqlclient mariadb libmariadb libmysql)
    if(ENT_MYSQL_INCLUDE_DIR AND ENT_MYSQL_LIBRARY)
      include_directories(${ENT_MYSQL_INCLUDE_DIR})
      get_filename_component(ENT_MYSQL_LIBRARY_DIR ${ENT_MYSQL_LIBRARY} DIRECTORY)
      if(WIN32)
        find_file(ENT_MYSQL_RUNTIME NAMES libmysql.dll mysql.dll libmariadb.dll mariadb.dll HINTS ${ENT_MYSQL_LIBRARY_DIR} ${ENT_MYSQL_LIBRARY_DIR}/../bin)
        if(ENT_MYSQL_RUNTIME)
          set(ENT_MYSQL_RUNTIME ${ENT_MYSQL_RUNTIME} CACHE INTERNAL "MySQL runtime library" FORCE)
        endif()
      endif()
      set(ENT_MYSQL_FOUND ON CACHE INTERNAL "MySQL backend discovery result" FORCE)
    else()
      message(FATAL_ERROR "MySQL headers or library not found; MySQL backend is required.")
    endif()
  endif()

  if(ENT_ENABLE_PGSQL)
    find_path(ENT_PGSQL_INCLUDE_DIR libpq-fe.h PATH_SUFFIXES postgresql)
    find_library(ENT_PGSQL_LIBRARY NAMES pq libpq)
    if(ENT_PGSQL_INCLUDE_DIR AND ENT_PGSQL_LIBRARY)
      include_directories(${ENT_PGSQL_INCLUDE_DIR})
      set(ENT_PGSQL_FOUND ON CACHE INTERNAL "PostgreSQL backend discovery result" FORCE)
    else()
      message(FATAL_ERROR "PostgreSQL headers or library not found; PostgreSQL backend is required.")
    endif()
  endif()
else()
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
  set(ENT_APPLE_SDK_INCLUDE "")
  set(ENT_APPLE_SDK_LIB "")
  set(ENT_PKGCONFIG_MYSQL_INCLUDE_DIRS "")
  set(ENT_PKGCONFIG_MYSQL_LIBRARY_DIRS "")
  set(ENT_PKGCONFIG_SQLITE_INCLUDE_DIRS "")
  set(ENT_PKGCONFIG_SQLITE_LIBRARY_DIRS "")
  set(ENT_PKGCONFIG_PGSQL_INCLUDE_DIRS "")
  set(ENT_PKGCONFIG_PGSQL_LIBRARY_DIRS "")

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
      pkg_check_modules(ENT_PKGCONFIG_PGSQL QUIET libpq)
      if(NOT ENT_PKGCONFIG_PGSQL_FOUND)
        pkg_check_modules(ENT_PKGCONFIG_PGSQL QUIET postgresql)
      endif()
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
      set(ENT_MYSQL_FOUND ON CACHE INTERNAL "MySQL backend discovery result" FORCE)
    else()
      message(FATAL_ERROR "MySQL headers or library not found; MySQL backend is required.")
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
    endif()
    if(ENT_SQLITE_INCLUDE_DIR AND ENT_SQLITE_LIBRARY)
      include_directories(${ENT_SQLITE_INCLUDE_DIR})
      set(ENT_SQLITE_FOUND ON CACHE INTERNAL "SQLite backend discovery result" FORCE)
    else()
      message(FATAL_ERROR "SQLite headers or library not found; SQLite backend is required.")
    endif()
  endif()

  if(ENT_ENABLE_PGSQL)
    if(APPLE)
      find_path(ENT_PGSQL_INCLUDE_DIR libpq-fe.h PATHS ${ENT_APPLE_SDK_INCLUDE})
      find_library(ENT_PGSQL_LIBRARY NAMES pq libpq PATHS ${ENT_APPLE_SDK_LIB})
    else()
      find_path(
        ENT_PGSQL_INCLUDE_DIR
        libpq-fe.h
        HINTS ${ENT_PKGCONFIG_PGSQL_INCLUDE_DIRS}
        PATH_SUFFIXES postgresql
      )
      find_library(
        ENT_PGSQL_LIBRARY
        NAMES pq libpq
        HINTS ${ENT_PKGCONFIG_PGSQL_LIBRARY_DIRS}
      )
    endif()
    if(ENT_PGSQL_INCLUDE_DIR AND ENT_PGSQL_LIBRARY)
      include_directories(${ENT_PGSQL_INCLUDE_DIR})
      set(ENT_PGSQL_FOUND ON CACHE INTERNAL "PostgreSQL backend discovery result" FORCE)
    else()
      message(FATAL_ERROR "PostgreSQL headers or library not found; PostgreSQL backend is required.")
    endif()
  endif()
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

ent_prefer_release_library(ENT_SQLITE_LIBRARY)
ent_prefer_release_library(ENT_MYSQL_LIBRARY)
ent_prefer_release_library(ENT_PGSQL_LIBRARY)
ent_export_dependency_settings()
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../inc)
include_directories(${ENT_MSG_GENERATED_DIR})
add_definitions(-DENT_ENABLE_LUA=$<BOOL:${ENT_ENABLE_LUA}>)

set(ENT_LIBRARY_SOURCES
  ent_init.c ent_db.c ent_db_sqlite.c ent_db_mysql.c ent_db_pgsql.c
  ent_log_core.c ent_log_writer.c ent_log_options.c ent_log_compat.c
  ent_thread.c ent_msg.c ent_script.c ent_shm.c
  utl_socket.c utl_thread.c utl_timer.c utl_dll.c utl_tpool.c
  ${ENT_MSG_GENERATED_SOURCE}
)

set(ENT_PUBLIC_HEADERS
  "../inc/ent_comm.h;../inc/ent_init.h;../inc/ent_types.h;../inc/ent_db.h;../inc/ent_log.h;../inc/ent_thread.h;../inc/ent_utility.h;../inc/ent_msg.h;../inc/ent_script.h;../inc/ent_shm.h;${ENT_MSG_GENERATED_HEADER}"
)

function(ent_add_object_library target export_definition)
  add_library(
    ${target}
    OBJECT
    ${ENT_LIBRARY_SOURCES}
  )

  add_dependencies(${target} ent_msg_codegen)
  target_include_directories(
    ${target}
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../inc
      ${ENT_MSG_GENERATED_DIR}
  )
  target_compile_definitions(
    ${target}
    PRIVATE
      ENT_ENABLE_LUA=$<BOOL:${ENT_ENABLE_LUA}>
      ${export_definition}
  )
  ent_target_db_settings(${target} PRIVATE PRIVATE)
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

if(ENT_BUILD_SHARED)
  ent_add_object_library(${LIB_NAME_OBJS_SHARE} ENT_MODULE_BEING_COMPILED)
endif()

if(ENT_BUILD_STATIC)
  ent_add_object_library(${LIB_NAME_OBJS_STATIC} LINKING_LIBENT)
endif()

function(ent_link_backend_deps target visibility)
  if(WIN32)
    target_link_libraries(${target} ${visibility} Ws2_32 Winmm)
  else()
    target_link_libraries(${target} ${visibility} pthread)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_link_libraries(${target} ${visibility} rt)
    endif()
  endif()

  if(ENT_SQLITE_FOUND AND ENT_SQLITE_LIBRARY)
    target_link_libraries(${target} ${visibility} ${ENT_SQLITE_LIBRARY})
  endif()

  if(ENT_MYSQL_FOUND AND ENT_MYSQL_LIBRARY)
    target_link_libraries(${target} ${visibility} ${ENT_MYSQL_LIBRARY})
  endif()

  if(ENT_PGSQL_FOUND AND ENT_PGSQL_LIBRARY)
    target_link_libraries(${target} ${visibility} ${ENT_PGSQL_LIBRARY})
  endif()

  if(ENT_ENABLE_LUA)
    if(ENT_LUA_TARGET)
      target_link_libraries(${target} ${visibility} ${ENT_LUA_TARGET})
    elseif(ENT_LUA_LIBRARY)
      target_link_libraries(${target} ${visibility} ${ENT_LUA_LIBRARY})
    endif()
  endif()
endfunction()

if(ENT_BUILD_SHARED)
  add_library(
    ${LIB_NAME_SHARE}
    SHARED
    $<TARGET_OBJECTS:${LIB_NAME_OBJS_SHARE}>
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
      ENT_ENABLE_LUA=$<BOOL:${ENT_ENABLE_LUA}>
  )
  ent_target_db_settings(${LIB_NAME_SHARE} PUBLIC PRIVATE)

  set_target_properties(
    ${LIB_NAME_SHARE}
    PROPERTIES
      PUBLIC_HEADER "${ENT_PUBLIC_HEADERS}"
  )

  ent_link_backend_deps(${LIB_NAME_SHARE} PRIVATE)
endif()

if(ENT_BUILD_STATIC)
  add_library(
    ${LIB_NAME_STATIC}
    STATIC
    $<TARGET_OBJECTS:${LIB_NAME_OBJS_STATIC}>
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
      ENT_ENABLE_LUA=$<BOOL:${ENT_ENABLE_LUA}>
      LINKING_LIBENT
  )
  ent_target_db_settings(${LIB_NAME_STATIC} PUBLIC PRIVATE)

  set_target_properties(
    ${LIB_NAME_STATIC}
    PROPERTIES
      PUBLIC_HEADER "${ENT_PUBLIC_HEADERS}"
  )

  ent_link_backend_deps(${LIB_NAME_STATIC} PUBLIC)
endif()

set(ENT_INSTALL_TARGETS)
if(TARGET ${LIB_NAME_SHARE})
  list(APPEND ENT_INSTALL_TARGETS ${LIB_NAME_SHARE})
endif()
if(TARGET ${LIB_NAME_STATIC})
  list(APPEND ENT_INSTALL_TARGETS ${LIB_NAME_STATIC})
endif()
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
    COMPONENT ${ENT_DEVEL_COMPONENT}
  RUNTIME
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT ${ENT_RUNTIME_COMPONENT}
  LIBRARY
    DESTINATION ${INSTALL_LIBDIR}
    COMPONENT ${ENT_RUNTIME_COMPONENT}
  PUBLIC_HEADER
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT ${ENT_DEVEL_COMPONENT}
)
