cmake_minimum_required(VERSION 3.15)
project(ent_downstream_consumer C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(ent CONFIG REQUIRED)

add_executable(ent_downstream_consumer main.c)
target_link_libraries(ent_downstream_consumer PRIVATE ent::ent_s)
