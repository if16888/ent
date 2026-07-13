cmake_minimum_required(VERSION 3.15)
project(ent_downstream_consumer C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(ent CONFIG REQUIRED)

set(ENT_CONSUMER_LINKAGE "static" CACHE STRING "ent linkage: shared or static")
set_property(CACHE ENT_CONSUMER_LINKAGE PROPERTY STRINGS shared static)

add_executable(ent_downstream_consumer main.c)
if(ENT_CONSUMER_LINKAGE STREQUAL "shared")
  target_link_libraries(ent_downstream_consumer PRIVATE ent::ent)
elseif(ENT_CONSUMER_LINKAGE STREQUAL "static")
  target_link_libraries(ent_downstream_consumer PRIVATE ent::ent_s)
else()
  message(FATAL_ERROR "ENT_CONSUMER_LINKAGE must be shared or static")
endif()
