cmake_minimum_required(VERSION 3.16)
project(ent_package_smoke C)

find_package(ent CONFIG REQUIRED)

add_executable(ent_package_smoke main.c)
target_link_libraries(ent_package_smoke PRIVATE ent::ent)
