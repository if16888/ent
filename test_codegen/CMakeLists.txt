include("${CMAKE_SOURCE_DIR}/cmake/EntMsgCodegen.cmake")

add_library(test_msg_catalog STATIC catalog_anchor.c)
target_include_directories(test_msg_catalog PRIVATE
  "${CMAKE_SOURCE_DIR}/inc"
)
ent_add_msg_catalog(
  TARGET test_msg_catalog
  SPEC "${CMAKE_CURRENT_SOURCE_DIR}/test_catalog.msg"
  PREFIX ZX
  TYPE_PREFIX ZX
  TABLE_PREFIX g_zx_msg
  HEADER zx_msg_gen.h
  SOURCE zx_msg_gen.c
  INCLUDE_GUARD ZX_MSG_GEN_H
  INCLUDE_VISIBILITY PUBLIC
  CODEGEN_TARGET test_msg_catalog_codegen
  EMIT_SYMBOL_NAME
)

add_executable(test_msg_catalog_exec test_msg_catalog.c)
target_include_directories(test_msg_catalog_exec PRIVATE
  "${CMAKE_SOURCE_DIR}/inc"
)
target_link_libraries(test_msg_catalog_exec PRIVATE test_msg_catalog)
add_test(NAME test_msg_catalog_exec COMMAND test_msg_catalog_exec)

if(CMAKE_VERSION VERSION_LESS "3.12")
  find_package(PythonInterp REQUIRED)
  set(_ent_codegen_python "${PYTHON_EXECUTABLE}")
else()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(_ent_codegen_python "${Python3_EXECUTABLE}")
endif()

add_test(
  NAME test_gen_ent_msg
  COMMAND
    "${_ent_codegen_python}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_gen_ent_msg.py"
    "${CMAKE_SOURCE_DIR}/scripts/gen_ent_msg.py"
    "${CMAKE_SOURCE_DIR}/msg/ent.msg"
)

add_test(
  NAME test_gen_ent_msg_symbols
  COMMAND
    "${_ent_codegen_python}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_gen_ent_msg_symbols.py"
    "${CMAKE_SOURCE_DIR}/scripts/gen_ent_msg_symbols.py"
)
