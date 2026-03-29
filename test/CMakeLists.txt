add_executable(
  test_ent_init
  test_ent_init.c
  ../comm/ent_init.c
)

target_include_directories(
  test_ent_init
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

add_test(NAME test_ent_init COMMAND test_ent_init)
