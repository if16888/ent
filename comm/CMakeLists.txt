cmake_minimum_required(VERSION 3.6 FATAL_ERROR)

project(ent C)

set(LIB_NAME_SHARE ent)
set(LIB_NAME_STATIC ent_s)
set(LIB_NAME_OBJS ent_objs)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(ENT_ENABLE_SQLITE "Enable SQLite backend" ON)
option(ENT_ENABLE_MYSQL "Enable MySQL backend" ON)


if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
add_definitions(-D_UNICODE)
if(ENT_ENABLE_SQLITE)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../3rd/sqlite)
link_directories("${CMAKE_CURRENT_SOURCE_DIR}/../3rd/sqlite")
endif()
if(ENT_ENABLE_MYSQL)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../3rd/mysql/include)
link_directories("${CMAKE_CURRENT_SOURCE_DIR}/../3rd/mysql/lib")
endif()
link_directories(${CMAKE_PREFIX_PATH})
else()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
if(ENT_ENABLE_MYSQL)
find_path(ENT_MYSQL_INCLUDE_DIR mysql.h PATHS /usr/include/mysql /usr/local/include/mysql /opt/homebrew/include/mysql)
find_library(ENT_MYSQL_LIBRARY NAMES mysqlclient PATHS /usr/lib64/mysql /usr/lib/mysql /usr/local/lib /opt/homebrew/lib)
if(ENT_MYSQL_INCLUDE_DIR AND ENT_MYSQL_LIBRARY)
include_directories(${ENT_MYSQL_INCLUDE_DIR})
else()
message(WARNING "MySQL headers or library not found; disabling MySQL backend.")
set(ENT_ENABLE_MYSQL OFF)
endif()
endif()
if(ENT_ENABLE_SQLITE)
find_path(ENT_SQLITE_INCLUDE_DIR sqlite3.h PATHS /usr/include /usr/local/include /opt/homebrew/include)
find_library(ENT_SQLITE_LIBRARY NAMES sqlite3 PATHS /usr/lib /usr/local/lib /opt/homebrew/lib)
if(ENT_SQLITE_INCLUDE_DIR AND ENT_SQLITE_LIBRARY)
include_directories(${ENT_SQLITE_INCLUDE_DIR})
else()
message(WARNING "SQLite headers or library not found; disabling SQLite backend.")
set(ENT_ENABLE_SQLITE OFF)
endif()
endif()
link_directories(${CMAKE_PREFIX_PATH})
endif()
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
    if(ENT_ENABLE_SQLITE)
        target_link_libraries(${LIB_NAME_SHARE} PRIVATE sqlite3)
    endif()
    if(ENT_ENABLE_MYSQL)
        target_link_libraries(${LIB_NAME_SHARE} PRIVATE libmysql)
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




