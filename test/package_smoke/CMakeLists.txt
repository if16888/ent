cmake_minimum_required(VERSION 3.16)
project(ent_package_smoke C)

find_package(ent CONFIG REQUIRED)

add_executable(ent_package_smoke main.c)
if(TARGET ent::ent)
  target_link_libraries(ent_package_smoke PRIVATE ent::ent)
elseif(TARGET ent::ent_s)
  target_link_libraries(ent_package_smoke PRIVATE ent::ent_s)
else()
  message(FATAL_ERROR "No exported ent target is available.")
endif()
