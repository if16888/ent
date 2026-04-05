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
option(ENT_ALLOW_VENDORED_DB_LIBS "Allow vendored DB libraries as a fallback" ON)

set(ENT_SQLITE_INCLUDE_DIR "")
set(ENT_SQLITE_LIBRARY "")
set(ENT_SQLITE_RUNTIME "")
set(ENT_MYSQL_INCLUDE_DIR "")
set(ENT_MYSQL_LIBRARY "")
set(ENT_MYSQL_RUNTIME "")
set(ENT_USING_VENDORED_SQLITE OFF CACHE INTERNAL "Using vendored SQLite library")
set(ENT_USING_VENDORED_MYSQL OFF CACHE INTERNAL "Using vendored MySQL library")
set(ENT_SQLITE_RUNTIME "" CACHE INTERNAL "SQLite runtime library")
set(ENT_MYSQL_RUNTIME "" CACHE INTERNAL "MySQL runtime library")

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
if(ENT_ENABLE_MYSQL)
if(APPLE)
find_path(ENT_MYSQL_INCLUDE_DIR mysql.h PATH_SUFFIXES mysql mariadb PATHS ${ENT_APPLE_SDK_INCLUDE})
find_library(ENT_MYSQL_LIBRARY NAMES mysqlclient mariadb libmariadb PATHS ${ENT_APPLE_SDK_LIB})
else()
find_path(ENT_MYSQL_INCLUDE_DIR mysql.h PATH_SUFFIXES mysql mariadb)
find_library(ENT_MYSQL_LIBRARY NAMES mysqlclient mariadb libmariadb)
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
find_path(ENT_SQLITE_INCLUDE_DIR sqlite3.h)
find_library(ENT_SQLITE_LIBRARY NAMES sqlite3)
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
ent_export_dependency_settings()
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../inc)
add_definitions(-DENT_ENABLE_SQLITE=$<BOOL:${ENT_ENABLE_SQLITE}>)
add_definitions(-DENT_ENABLE_MYSQL=$<BOOL:${ENT_ENABLE_MYSQL}>)

if(BUILD_SHARED_LIBS)
  add_definitions(-DENT_MODULE_BEING_COMPILED)
else()
  add_definitions(-DLINKING_LIBENT)
endif()

add_library(
  ${LIB_NAME_OBJS}
  OBJECT
  ent_init.c ent_db.c ent_log.c ent_thread.c utl_socket.c utl_thread.c utl_timer.c utl_dll.c utl_tpool.c
  )

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
  
  set_target_properties(${LIB_NAME_SHARE} 
                         PROPERTIES 
                          PUBLIC_HEADER "../inc/ent_comm.h;../inc/ent_init.h;../inc/ent_types.h;../inc/ent_db.h;../inc/ent_log.h;../inc/ent_thread.h;../inc/ent_utility.h"
                         )
  
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(${LIB_NAME_SHARE} PRIVATE Ws2_32 Winmm)
    if(ENT_ENABLE_SQLITE AND ENT_SQLITE_LIBRARY)
        target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_SQLITE_LIBRARY})
    endif()
    if(ENT_ENABLE_MYSQL AND ENT_MYSQL_LIBRARY)
        target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_MYSQL_LIBRARY})
    endif()
else()
    target_link_libraries(${LIB_NAME_SHARE} PRIVATE pthread)
    if(ENT_ENABLE_MYSQL)
        target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_MYSQL_LIBRARY})
    endif()
    if(ENT_ENABLE_SQLITE)
        target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_SQLITE_LIBRARY})
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

install(
  TARGETS
      ${LIB_NAME_SHARE} ${LIB_NAME_STATIC}
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




