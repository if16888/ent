cmake_minimum_required(VERSION 3.6 FATAL_ERROR)

project(ent CXX)

set(LIB_NAME_SHARE ent)
set(LIB_NAME_STATIC ent_s)
set(LIB_NAME_OBJS ent_objs)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_SHARED_LIBS "Build shared libraries" ON)


if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
add_definitions(-D_UNICODE)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../3rd/sqlite)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../3rd/mysql/include)

link_directories( 
    "${CMAKE_CURRENT_SOURCE_DIR}/../3rd/sqlite" 
    "${CMAKE_CURRENT_SOURCE_DIR}/../3rd/mysql/lib"
    ${CMAKE_PREFIX_PATH})
else()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
include_directories(/usr/include/mysql)
link_directories( 
    "/usr/lib64/mysql"
    ${CMAKE_PREFIX_PATH})
endif()
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../inc)

if(BUILD_SHARED_LIBS)
  add_definitions(-DENT_MODULE_BEING_COMPILED)
else()
  add_definitions(-DLINKING_LIBENT)
endif()

add_library(
  ${LIB_NAME_OBJS}
  OBJECT
  ent_init.cpp ent_db.cpp ent_log.cpp ent_thread.cpp utl_socket.cpp utl_thread.cpp utl_timer.cpp utl_dll.cpp utl_tpool.cpp
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
    target_link_libraries(${LIB_NAME_SHARE} PRIVATE sqlite3 libmysql Ws2_32 Winmm)
else()
    target_link_libraries(${LIB_NAME_SHARE} PRIVATE mysqlclient sqlite3 pthread rt)
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




