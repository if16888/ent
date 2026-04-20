cmake_minimum_required(VERSION 3.6 FATAL_ERROR)

project(example C)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  add_definitions(-D_UNICODE)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message("CMAKE_FIND_LIBRARY_SUFFIXES:  ${CMAKE_FIND_LIBRARY_SUFFIXES}")
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../inc)
message("CMAKE_FIND_LIBRARY_SUFFIXES:  ${CMAKE_LIBRARY_PATH}")
message("CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")

add_executable(example01 example01.c)
target_link_libraries(example01 ent)

add_executable(example02_multi_runtime example02_multi_runtime.c)
target_link_libraries(example02_multi_runtime ent)
