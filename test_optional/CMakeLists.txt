cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

add_executable(
    test_ent_optional_backends
    test_ent_optional_backends.c
)

target_include_directories(
    test_ent_optional_backends
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../inc
        ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

target_link_libraries(test_ent_optional_backends PRIVATE ent_s)

add_test(NAME test_ent_optional_backends COMMAND test_ent_optional_backends)
