add_compile_definitions(LINKING_LIBENT)

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

  add_test(NAME test_ent_script COMMAND test_ent_script)
endif()

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

if(WIN32)
  target_link_libraries(test_ent_thread PRIVATE Winmm)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(test_ent_thread PRIVATE rt)
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

if(ENT_ENABLE_SQLITE AND (NOT WIN32 OR ENT_SQLITE_LIBRARY))
  add_executable(
    test_ent_db
    test_ent_db.c
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

  target_compile_definitions(
    test_ent_db
    PRIVATE
      ENT_ENABLE_SQLITE=1
      ENT_ENABLE_MYSQL=0
      ENT_ENABLE_PGSQL=$<BOOL:${PostgreSQL_FOUND}>
  )

  if(WIN32)
    if(ENT_SQLITE_INCLUDE_DIR)
      target_include_directories(test_ent_db PRIVATE ${ENT_SQLITE_INCLUDE_DIR})
    endif()
    target_link_libraries(test_ent_db PRIVATE ${ENT_SQLITE_LIBRARY})
    if(ENT_SQLITE_RUNTIME)
      add_custom_command(
        TARGET test_ent_db
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${ENT_SQLITE_RUNTIME}
          $<TARGET_FILE_DIR:test_ent_db>
      )
    endif()
  else()
    target_link_libraries(test_ent_db PRIVATE pthread sqlite3)
  endif()

  
  if(PostgreSQL_FOUND)
      target_include_directories(test_ent_db PRIVATE ${PostgreSQL_INCLUDE_DIRS})
      target_link_libraries(test_ent_db PRIVATE ${PostgreSQL_LIBRARIES})
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
      ENT_ENABLE_MYSQL=0
      ENT_ENABLE_PGSQL=$<BOOL:${PostgreSQL_FOUND}>
  )

  if(WIN32)
    if(ENT_SQLITE_INCLUDE_DIR)
      target_include_directories(test_security PRIVATE ${ENT_SQLITE_INCLUDE_DIR})
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
  else()
    target_link_libraries(test_security PRIVATE pthread sqlite3)
  endif()

  
  if(PostgreSQL_FOUND)
      target_include_directories(test_security PRIVATE ${PostgreSQL_INCLUDE_DIRS})
      target_link_libraries(test_security PRIVATE ${PostgreSQL_LIBRARIES})
  endif()
  add_test(NAME test_security COMMAND test_security)
endif()

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

if(WIN32)
  target_link_libraries(perf_utl_socket PRIVATE Ws2_32)
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
