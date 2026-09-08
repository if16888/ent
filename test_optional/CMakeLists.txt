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

add_executable(
    test_ent_shm_security
    test_ent_shm_security.c
)

target_include_directories(
    test_ent_shm_security
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../inc
        ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

target_link_libraries(test_ent_shm_security PRIVATE ent_s)

add_test(NAME test_ent_shm_security COMMAND test_ent_shm_security)

if(ENT_SQLITE_FOUND)
    add_executable(
        test_ent_db_reinit_failure
        test_ent_db_reinit_failure.c
    )

    target_include_directories(
        test_ent_db_reinit_failure
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../inc
            ${CMAKE_CURRENT_SOURCE_DIR}/../comm
    )

    target_link_libraries(test_ent_db_reinit_failure PRIVATE ent_s)
    add_test(NAME test_ent_db_reinit_failure COMMAND test_ent_db_reinit_failure)
endif()

if(ENT_ENABLE_LUA)
    add_executable(
        test_ent_script_return_compat
        test_ent_script_return_compat.c
    )

    target_include_directories(
        test_ent_script_return_compat
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../inc
            ${CMAKE_CURRENT_SOURCE_DIR}/../comm
    )

    target_link_libraries(test_ent_script_return_compat PRIVATE ent_s)
    add_test(NAME test_ent_script_return_compat COMMAND test_ent_script_return_compat)
endif()

if(UNIX AND NOT APPLE)
    add_executable(
        test_utl_timer_rt_self_delete
        test_utl_timer_rt_self_delete.c
        ../test/test_runtime_stub.c
        ../comm/utl_timer.c
        ../comm/utl_thread.c
        ../comm/utl_dll.c
    )

    target_compile_definitions(test_utl_timer_rt_self_delete PRIVATE ENT_TIMER_TEST_HOOKS=1)
    target_include_directories(
        test_utl_timer_rt_self_delete
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../inc
            ${CMAKE_CURRENT_SOURCE_DIR}/../comm
    )
    target_link_libraries(test_utl_timer_rt_self_delete PRIVATE pthread rt)
    add_test(NAME test_utl_timer_rt_self_delete COMMAND test_utl_timer_rt_self_delete)
endif()
