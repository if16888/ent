add_compile_definitions(LINKING_LIBENT)

get_property(ENT_ENABLE_SQLITE CACHE ENT_ENABLE_SQLITE PROPERTY VALUE)
get_property(ENT_ENABLE_MYSQL CACHE ENT_ENABLE_MYSQL PROPERTY VALUE)
get_property(ENT_ENABLE_PGSQL CACHE ENT_ENABLE_PGSQL PROPERTY VALUE)
get_property(ENT_SQLITE_FOUND CACHE ENT_SQLITE_FOUND PROPERTY VALUE)
get_property(ENT_MYSQL_FOUND CACHE ENT_MYSQL_FOUND PROPERTY VALUE)
get_property(ENT_PGSQL_FOUND CACHE ENT_PGSQL_FOUND PROPERTY VALUE)

add_compile_definitions(
  ENT_ENABLE_SQLITE=$<BOOL:${ENT_ENABLE_SQLITE}>
  ENT_ENABLE_MYSQL=$<BOOL:${ENT_ENABLE_MYSQL}>
  ENT_ENABLE_PGSQL=$<BOOL:${ENT_ENABLE_PGSQL}>
  ENT_SQLITE_FOUND=$<BOOL:${ENT_SQLITE_FOUND}>
  ENT_MYSQL_FOUND=$<BOOL:${ENT_MYSQL_FOUND}>
  ENT_PGSQL_FOUND=$<BOOL:${ENT_PGSQL_FOUND}>
)

function(ent_test_link_db_deps target)
  if(ENT_SQLITE_FOUND AND ENT_SQLITE_INCLUDE_DIR)
    target_include_directories(${target} PRIVATE ${ENT_SQLITE_INCLUDE_DIR})
  endif()
  if(ENT_MYSQL_FOUND AND ENT_MYSQL_INCLUDE_DIR)
    target_include_directories(${target} PRIVATE ${ENT_MYSQL_INCLUDE_DIR})
  endif()
  if(ENT_PGSQL_FOUND AND ENT_PGSQL_INCLUDE_DIR)
    target_include_directories(${target} PRIVATE ${ENT_PGSQL_INCLUDE_DIR})
  endif()

  if(ENT_SQLITE_FOUND AND ENT_SQLITE_LIBRARY)
    target_link_libraries(${target} PRIVATE ${ENT_SQLITE_LIBRARY})
  endif()
  if(ENT_MYSQL_FOUND AND ENT_MYSQL_LIBRARY)
    target_link_libraries(${target} PRIVATE ${ENT_MYSQL_LIBRARY})
  endif()
  if(ENT_PGSQL_FOUND AND ENT_PGSQL_LIBRARY)
    target_link_libraries(${target} PRIVATE ${ENT_PGSQL_LIBRARY})
  endif()

  foreach(runtime ${ENT_SQLITE_RUNTIME} ${ENT_MYSQL_RUNTIME} ${ENT_PGSQL_RUNTIME})
    if(runtime)
      add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${runtime}
          $<TARGET_FILE_DIR:${target}>
      )
    endif()
  endforeach()
endfunction()

if(DEFINED ENT_MSG_GENERATED_DIR)
  include_directories(${ENT_MSG_GENERATED_DIR})
endif()

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

if(TARGET ent_msg_codegen)
  add_dependencies(test_ent_init ent_msg_codegen)
endif()

add_test(NAME test_ent_init COMMAND test_ent_init)

if(TARGET ent_s)
  add_executable(
    test_ent_msg
    test_ent_msg.c
  )

  target_include_directories(
    test_ent_msg
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../inc
      ${CMAKE_CURRENT_SOURCE_DIR}/../comm
  )

  if(TARGET ent_msg_codegen)
    add_dependencies(test_ent_msg ent_msg_codegen)
  endif()

  target_link_libraries(test_ent_msg PRIVATE ent_s)

  add_test(NAME test_ent_msg COMMAND test_ent_msg)

  add_executable(
    test_ent_script
    test_ent_script.c
  )

  target_include_directories(
    test_ent_script
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../inc
      ${CMAKE_CURRENT_SOURCE_DIR}/../comm
  )

  if(TARGET ent_msg_codegen)
    add_dependencies(test_ent_script ent_msg_codegen)
  endif()

  target_link_libraries(test_ent_script PRIVATE ent_s)

  if(APPLE OR UNIX)
    target_link_libraries(test_ent_script PRIVATE pthread)
  endif()

  add_test(NAME test_ent_script COMMAND test_ent_script)
endif()

add_executable(
  test_utl_dll
  test_utl_dll.c
  test_runtime_stub.c
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
  test_runtime_stub.c
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
  test_utl_lock_cv
  test_utl_lock_cv.c
  test_runtime_stub.c
  ../comm/utl_thread.c
)

target_include_directories(
  test_utl_lock_cv
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(test_utl_lock_cv PRIVATE pthread)
endif()

add_test(NAME test_utl_lock_cv COMMAND test_utl_lock_cv)

add_executable(
  test_ent_thread
  test_ent_thread.c
  test_runtime_stub.c
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

if(WIN32)
  target_link_libraries(test_ent_thread PRIVATE Winmm)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(test_ent_thread PRIVATE rt)
endif()

add_test(NAME test_ent_thread COMMAND test_ent_thread)

add_executable(
  test_ent_thread_failures
  test_ent_thread_failures.c
  test_runtime_stub.c
)

target_include_directories(
  test_ent_thread_failures
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(test_ent_thread_failures PRIVATE pthread)
endif()

add_test(NAME test_ent_thread_failures COMMAND test_ent_thread_failures)

add_executable(
  test_utl_tpool
  test_utl_tpool.c
  test_runtime_stub.c
  ../comm/utl_tpool.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
)

target_compile_definitions(test_utl_tpool PRIVATE ENT_TPOOL_TEST_HOOKS=1)

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
  test_utl_tpool_lifecycle
  test_utl_tpool_lifecycle.c
  test_runtime_stub.c
  ../comm/utl_tpool.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
)

target_include_directories(
  test_utl_tpool_lifecycle
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(test_utl_tpool_lifecycle PRIVATE pthread)
endif()

add_test(NAME test_utl_tpool_lifecycle COMMAND test_utl_tpool_lifecycle)

add_executable(
  test_utl_tpool_integration
  test_utl_tpool_integration.c
  test_runtime_stub.c
  ../comm/utl_tpool.c
  ../comm/ent_thread.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
  ../comm/utl_timer.c
)

target_include_directories(
  test_utl_tpool_integration
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(test_utl_tpool_integration PRIVATE pthread)
endif()

if(WIN32)
  target_link_libraries(test_utl_tpool_integration PRIVATE Winmm)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(test_utl_tpool_integration PRIVATE rt)
endif()

add_test(NAME test_utl_tpool_integration COMMAND test_utl_tpool_integration)

add_executable(
  test_utl_timer
  test_utl_timer.c
  test_runtime_stub.c
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

if(WIN32)
  target_link_libraries(test_utl_timer PRIVATE Winmm)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(test_utl_timer PRIVATE rt)
endif()

add_test(NAME test_utl_timer COMMAND test_utl_timer)

add_executable(
  test_utl_socket
  test_utl_socket.c
  test_runtime_stub.c
  ../comm/utl_socket.c
)

target_include_directories(
  test_utl_socket
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(WIN32)
  target_link_libraries(test_utl_socket PRIVATE Ws2_32)
endif()

add_test(NAME test_utl_socket COMMAND test_utl_socket)

if(ENT_SQLITE_FOUND)
  add_executable(
    test_ent_db
    test_ent_db.c
    test_runtime_stub.c
    ../comm/ent_db.c
    ../comm/ent_db_sqlite.c
    ../comm/ent_db_mysql.c
    ../comm/ent_db_pgsql.c
  )

  target_include_directories(
    test_ent_db
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../inc
      ${CMAKE_CURRENT_SOURCE_DIR}/../comm
  )

  ent_test_link_db_deps(test_ent_db)
  if(APPLE OR UNIX)
    target_link_libraries(test_ent_db PRIVATE pthread)
  endif()
  add_test(NAME test_ent_db COMMAND test_ent_db)
endif()

add_executable(
  test_ent_log
  test_ent_log.c
  ../comm/ent_log_core.c
  ../comm/ent_log_writer.c
  ../comm/ent_log_options.c
  ../comm/ent_log_compat.c
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
  test_ent_log_flush_deadline
  test_ent_log_flush_deadline.c
  ../comm/ent_log_core.c
  ../comm/ent_log_writer.c
  ../comm/ent_log_options.c
  ../comm/ent_log_compat.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
)

target_include_directories(
  test_ent_log_flush_deadline
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(test_ent_log_flush_deadline PRIVATE pthread)
endif()

add_test(NAME test_ent_log_flush_deadline COMMAND test_ent_log_flush_deadline)

add_executable(
  test_ent_shm
  test_ent_shm.c
)

target_include_directories(
  test_ent_shm
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

target_link_libraries(test_ent_shm PRIVATE ent_s)

add_test(NAME test_ent_shm COMMAND test_ent_shm)

if(ENT_ENABLE_SQLITE AND (NOT WIN32 OR ENT_SQLITE_LIBRARY))
  add_executable(
  test_security
  test_security.c
  ../comm/ent_init.c
  ../comm/ent_log_core.c
  ../comm/ent_log_writer.c
  ../comm/ent_log_options.c
  ../comm/ent_log_compat.c
  ../comm/ent_db.c
  ../comm/ent_db_sqlite.c
  ../comm/ent_db_mysql.c
    ../comm/ent_db_pgsql.c
    ../comm/utl_thread.c
    ../comm/utl_dll.c
  )

  target_include_directories(
    test_security
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../inc
      ${CMAKE_CURRENT_SOURCE_DIR}/../comm
  )

  if(TARGET ent_msg_codegen)
    add_dependencies(test_security ent_msg_codegen)
  endif()

  target_compile_definitions(
    test_security
    PRIVATE
      ENT_ENABLE_SQLITE=1
      ENT_ENABLE_MYSQL=$<BOOL:${ENT_ENABLE_MYSQL}>
      ENT_ENABLE_PGSQL=$<BOOL:${ENT_PGSQL_FOUND}>
  )

  if(WIN32)
    if(ENT_SQLITE_INCLUDE_DIR)
      target_include_directories(test_security PRIVATE ${ENT_SQLITE_INCLUDE_DIR})
    endif()
    if(ENT_MYSQL_INCLUDE_DIR)
      target_include_directories(test_security PRIVATE ${ENT_MYSQL_INCLUDE_DIR})
    endif()
    if(ENT_PGSQL_INCLUDE_DIR)
      target_include_directories(test_security PRIVATE ${ENT_PGSQL_INCLUDE_DIR})
    endif()
    target_link_libraries(test_security PRIVATE ${ENT_SQLITE_LIBRARY})
    if(ENT_SQLITE_RUNTIME)
      add_custom_command(
        TARGET test_security
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${ENT_SQLITE_RUNTIME}
          $<TARGET_FILE_DIR:test_security>
      )
    endif()
    if(ENT_MYSQL_INCLUDE_DIR)
      target_include_directories(test_security PRIVATE ${ENT_MYSQL_INCLUDE_DIR})
    endif()
    if(ENT_MYSQL_LIBRARY)
      target_link_libraries(test_security PRIVATE ${ENT_MYSQL_LIBRARY})
    endif()
    if(ENT_MYSQL_RUNTIME)
      add_custom_command(
        TARGET test_security
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${ENT_MYSQL_RUNTIME}
          $<TARGET_FILE_DIR:test_security>
      )
    endif()
  else()
    target_link_libraries(test_security PRIVATE pthread sqlite3)
    if(ENT_MYSQL_LIBRARY)
      target_link_libraries(test_security PRIVATE ${ENT_MYSQL_LIBRARY})
    endif()
  endif()

  if(ENT_PGSQL_FOUND AND ENT_PGSQL_LIBRARY)
      target_include_directories(test_security PRIVATE ${ENT_PGSQL_INCLUDE_DIR})
      target_link_libraries(test_security PRIVATE ${ENT_PGSQL_LIBRARY})
  endif()
  if(ENT_MYSQL_INCLUDE_DIR)
    target_include_directories(test_security PRIVATE ${ENT_MYSQL_INCLUDE_DIR})
  endif()
  add_test(NAME test_security COMMAND test_security)
endif()

add_executable(
  perf_utl_socket
  perf_utl_socket.c
  test_runtime_stub.c
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

if(WIN32)
  target_link_libraries(perf_utl_socket PRIVATE Ws2_32)
endif()

add_executable(
  perf_utl_timer
  perf_utl_timer.c
  test_runtime_stub.c
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

if(WIN32)
  target_link_libraries(perf_utl_timer PRIVATE Winmm)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(perf_utl_timer PRIVATE rt)
endif()

if(UNIX AND NOT APPLE)
  add_executable(
    perf_utl_timer_rt
    perf_utl_timer_rt.c
    test_runtime_stub.c
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

  target_link_libraries(perf_utl_timer_rt PRIVATE pthread)
  target_link_libraries(perf_utl_timer_rt PRIVATE rt)
endif()

add_executable(
  perf_utl_tpool
  perf_utl_tpool.c
  test_runtime_stub.c
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

if(WIN32)
  target_link_libraries(perf_utl_tpool PRIVATE Winmm)
endif()

add_executable(
  perf_ent_log
  perf_ent_log.c
  test_runtime_stub.c
  ../comm/ent_log_core.c
  ../comm/ent_log_writer.c
  ../comm/ent_log_options.c
  ../comm/ent_log_compat.c
  ../comm/utl_thread.c
  ../comm/utl_dll.c
)

target_include_directories(
  perf_ent_log
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../comm
)

if(APPLE OR UNIX)
  target_link_libraries(perf_ent_log PRIVATE pthread)
endif()

if(TARGET ent_msg_codegen)
  get_property(_ent_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_ent_target IN LISTS _ent_test_targets)
    if(NOT _ent_target STREQUAL "ent_msg_codegen")
      get_target_property(_ent_target_type ${_ent_target} TYPE)
      if(_ent_target_type STREQUAL "EXECUTABLE")
        add_dependencies(${_ent_target} ent_msg_codegen)
      endif()
    endif()
  endforeach()
endif()
