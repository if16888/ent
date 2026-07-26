include(CMakeParseArguments)

set(_ENT_MSG_CODEGEN_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(ent_add_msg_catalog)
  set(options EMIT_SYMBOL_NAME)
  set(one_value_args
    TARGET
    SPEC
    PREFIX
    TYPE_PREFIX
    TABLE_PREFIX
    HEADER
    SOURCE
    OUTPUT_DIR
    INCLUDE_GUARD
    TYPES_HEADER
    SOURCE_HEADER
    INCLUDE_VISIBILITY
    CODEGEN_TARGET
    GENERATOR
    OUT_HEADER
    OUT_SOURCE
    OUT_DIR
  )
  set(multi_value_args SPECS)

  cmake_parse_arguments(ENT_MSG
    "${options}"
    "${one_value_args}"
    "${multi_value_args}"
    ${ARGN}
  )

  if(NOT ENT_MSG_TARGET)
    message(FATAL_ERROR "ent_add_msg_catalog requires TARGET")
  endif()
  if(NOT TARGET ${ENT_MSG_TARGET})
    message(FATAL_ERROR
      "ent_add_msg_catalog target does not exist: ${ENT_MSG_TARGET}")
  endif()
  if(ENT_MSG_SPEC AND ENT_MSG_SPECS)
    message(FATAL_ERROR "Use SPEC or SPECS, not both")
  endif()
  if(ENT_MSG_SPEC)
    set(_ent_msg_specs "${ENT_MSG_SPEC}")
  else()
    set(_ent_msg_specs ${ENT_MSG_SPECS})
  endif()
  if(NOT _ent_msg_specs)
    message(FATAL_ERROR "ent_add_msg_catalog requires SPEC or SPECS")
  endif()
  if(NOT ENT_MSG_PREFIX)
    message(FATAL_ERROR "ent_add_msg_catalog requires PREFIX")
  endif()

  if(ENT_MSG_GENERATOR)
    set(_ent_msg_generator "${ENT_MSG_GENERATOR}")
  elseif(EXISTS "${_ENT_MSG_CODEGEN_MODULE_DIR}/../scripts/gen_ent_msg.py")
    set(_ent_msg_generator
      "${_ENT_MSG_CODEGEN_MODULE_DIR}/../scripts/gen_ent_msg.py")
  elseif(EXISTS "${_ENT_MSG_CODEGEN_MODULE_DIR}/gen_ent_msg.py")
    set(_ent_msg_generator
      "${_ENT_MSG_CODEGEN_MODULE_DIR}/gen_ent_msg.py")
  else()
    message(FATAL_ERROR
      "gen_ent_msg.py was not found; pass GENERATOR explicitly")
  endif()

  if(DEFINED ENT_MSG_PYTHON_EXECUTABLE AND ENT_MSG_PYTHON_EXECUTABLE)
    set(_ent_msg_python "${ENT_MSG_PYTHON_EXECUTABLE}")
  elseif(CMAKE_VERSION VERSION_LESS "3.12")
    find_package(PythonInterp REQUIRED)
    set(_ent_msg_python "${PYTHON_EXECUTABLE}")
  else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(_ent_msg_python "${Python3_EXECUTABLE}")
  endif()

  string(TOLOWER "${ENT_MSG_PREFIX}" _ent_msg_prefix_lower)
  if(ENT_MSG_TYPE_PREFIX)
    set(_ent_msg_type_prefix "${ENT_MSG_TYPE_PREFIX}")
  else()
    set(_ent_msg_type_prefix "${ENT_MSG_PREFIX}")
  endif()
  if(ENT_MSG_TABLE_PREFIX)
    set(_ent_msg_table_prefix "${ENT_MSG_TABLE_PREFIX}")
  else()
    set(_ent_msg_table_prefix "g_${_ent_msg_prefix_lower}_msg")
  endif()
  if(ENT_MSG_OUTPUT_DIR)
    set(_ent_msg_output_dir "${ENT_MSG_OUTPUT_DIR}")
  else()
    set(_ent_msg_output_dir
      "${CMAKE_CURRENT_BINARY_DIR}/generated/${ENT_MSG_TARGET}")
  endif()
  if(ENT_MSG_HEADER)
    set(_ent_msg_header_name "${ENT_MSG_HEADER}")
  else()
    set(_ent_msg_header_name "${_ent_msg_prefix_lower}_msg_gen.h")
  endif()
  if(ENT_MSG_SOURCE)
    set(_ent_msg_source_name "${ENT_MSG_SOURCE}")
  else()
    set(_ent_msg_source_name "${_ent_msg_prefix_lower}_msg_gen.c")
  endif()
  if(ENT_MSG_INCLUDE_GUARD)
    set(_ent_msg_include_guard "${ENT_MSG_INCLUDE_GUARD}")
  else()
    string(TOUPPER "${ENT_MSG_PREFIX}_MSG_GEN_H" _ent_msg_include_guard)
  endif()
  if(ENT_MSG_TYPES_HEADER)
    set(_ent_msg_types_header "${ENT_MSG_TYPES_HEADER}")
  else()
    set(_ent_msg_types_header "ent_types.h")
  endif()
  if(ENT_MSG_SOURCE_HEADER)
    set(_ent_msg_source_header "${ENT_MSG_SOURCE_HEADER}")
  else()
    set(_ent_msg_source_header "${_ent_msg_header_name}")
  endif()
  if(ENT_MSG_INCLUDE_VISIBILITY)
    set(_ent_msg_include_visibility "${ENT_MSG_INCLUDE_VISIBILITY}")
  else()
    set(_ent_msg_include_visibility PRIVATE)
  endif()

  set(_ent_msg_header "${_ent_msg_output_dir}/${_ent_msg_header_name}")
  set(_ent_msg_source "${_ent_msg_output_dir}/${_ent_msg_source_name}")
  set(_ent_msg_command_args)
  foreach(_ent_msg_spec IN LISTS _ent_msg_specs)
    list(APPEND _ent_msg_command_args --input "${_ent_msg_spec}")
  endforeach()
  list(APPEND _ent_msg_command_args
    --header "${_ent_msg_header}"
    --source "${_ent_msg_source}"
    --symbol-prefix "${ENT_MSG_PREFIX}"
    --type-prefix "${_ent_msg_type_prefix}"
    --table-prefix "${_ent_msg_table_prefix}"
    --include-guard "${_ent_msg_include_guard}"
    --types-header "${_ent_msg_types_header}"
    --source-header "${_ent_msg_source_header}"
  )
  if(ENT_MSG_EMIT_SYMBOL_NAME)
    list(APPEND _ent_msg_command_args --emit-symbol-name)
  endif()

  add_custom_command(
    OUTPUT
      "${_ent_msg_header}"
      "${_ent_msg_source}"
    COMMAND
      "${_ent_msg_python}"
      "${_ent_msg_generator}"
      ${_ent_msg_command_args}
    DEPENDS
      ${_ent_msg_specs}
      "${_ent_msg_generator}"
    COMMENT "Generating ${ENT_MSG_PREFIX} message catalog"
    VERBATIM
  )

  set_source_files_properties(
    "${_ent_msg_header}"
    "${_ent_msg_source}"
    PROPERTIES GENERATED TRUE
  )
  target_sources(${ENT_MSG_TARGET} PRIVATE
    "${_ent_msg_header}"
    "${_ent_msg_source}"
  )
  target_include_directories(${ENT_MSG_TARGET}
    ${_ent_msg_include_visibility}
      "${_ent_msg_output_dir}"
  )

  if(ENT_MSG_CODEGEN_TARGET)
    if(TARGET ${ENT_MSG_CODEGEN_TARGET})
      message(FATAL_ERROR
        "Codegen target already exists: ${ENT_MSG_CODEGEN_TARGET}")
    endif()
    add_custom_target(${ENT_MSG_CODEGEN_TARGET}
      DEPENDS "${_ent_msg_header}" "${_ent_msg_source}")
    add_dependencies(${ENT_MSG_TARGET} ${ENT_MSG_CODEGEN_TARGET})
  endif()

  if(ENT_MSG_OUT_HEADER)
    set(${ENT_MSG_OUT_HEADER} "${_ent_msg_header}" PARENT_SCOPE)
  endif()
  if(ENT_MSG_OUT_SOURCE)
    set(${ENT_MSG_OUT_SOURCE} "${_ent_msg_source}" PARENT_SCOPE)
  endif()
  if(ENT_MSG_OUT_DIR)
    set(${ENT_MSG_OUT_DIR} "${_ent_msg_output_dir}" PARENT_SCOPE)
  endif()
endfunction()
