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

add_executable(
  test_utl_dll
  test_utl_dll.c
  ../comm/utl_dll.c
)

target_include_directories(
  test_utl_dll
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

add_test(NAME test_utl_dll COMMAND test_utl_dll)

add_executable(
  test_utl_thread
  test_utl_thread.c
  ../comm/utl_thread.c
)

target_include_directories(
  test_utl_thread
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(test_utl_thread PRIVATE pthread)
endif()

add_test(NAME test_utl_thread COMMAND test_utl_thread)

add_executable(
  test_ent_thread
  test_ent_thread.c
  ../comm/ent_thread.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
  ../comm/utl_timer.c
)

target_include_directories(
  test_ent_thread
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(test_ent_thread PRIVATE pthread)
endif()

add_test(NAME test_ent_thread COMMAND test_ent_thread)

add_executable(
  test_utl_tpool
  test_utl_tpool.c
  ../comm/utl_tpool.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
)

target_include_directories(
  test_utl_tpool
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(test_utl_tpool PRIVATE pthread)
endif()

add_test(NAME test_utl_tpool COMMAND test_utl_tpool)

add_executable(
  test_utl_timer
  test_utl_timer.c
  ../comm/utl_timer.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
)

target_include_directories(
  test_utl_timer
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(test_utl_timer PRIVATE pthread)
endif()

add_test(NAME test_utl_timer COMMAND test_utl_timer)

add_executable(
  test_utl_socket
  test_utl_socket.c
  ../comm/utl_socket.c
)

target_include_directories(
  test_utl_socket
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

add_test(NAME test_utl_socket COMMAND test_utl_socket)

add_executable(
  test_ent_db
  test_ent_db.c
  ../comm/ent_db.c
)

target_include_directories(
  test_ent_db
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

target_compile_definitions(
  test_ent_db
  PRIVATE
    ENT_ENABLE_SQLITE=1
    ENT_ENABLE_MYSQL=0
)

if(APPLE OR UNIX)
  target_link_libraries(test_ent_db PRIVATE pthread sqlite3)
endif()

add_test(NAME test_ent_db COMMAND test_ent_db)

add_executable(
  test_ent_log
  test_ent_log.c
  ../comm/ent_log.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
)

target_include_directories(
  test_ent_log
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(test_ent_log PRIVATE pthread)
endif()

add_test(NAME test_ent_log COMMAND test_ent_log)

add_executable(
  test_security
  test_security.c
  ../comm/ent_init.c
  ../comm/ent_log.c
  ../comm/ent_db.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
)

target_include_directories(
  test_security
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

target_compile_definitions(
  test_security
  PRIVATE
    ENT_ENABLE_SQLITE=1
    ENT_ENABLE_MYSQL=0
)

if(APPLE OR UNIX)
  target_link_libraries(test_security PRIVATE pthread sqlite3)
endif()

add_test(NAME test_security COMMAND test_security)

add_executable(
  perf_utl_socket
  perf_utl_socket.c
  ../comm/utl_socket.c
)

target_include_directories(
  perf_utl_socket
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(perf_utl_socket PRIVATE pthread)
endif()

add_executable(
  perf_utl_timer
  perf_utl_timer.c
  ../comm/utl_timer.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
)

target_include_directories(
  perf_utl_timer
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(perf_utl_timer PRIVATE pthread)
endif()

add_executable(
  perf_utl_timer_rt
  perf_utl_timer_rt.c
  ../comm/utl_timer.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
)

target_include_directories(
  perf_utl_timer_rt
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(perf_utl_timer_rt PRIVATE pthread)
endif()

add_executable(
  perf_utl_tpool
  perf_utl_tpool.c
  ../comm/utl_tpool.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
  ../comm/ent_thread.c
  ../comm/utl_timer.c
)

target_include_directories(
  perf_utl_tpool
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(perf_utl_tpool PRIVATE pthread)
endif()
