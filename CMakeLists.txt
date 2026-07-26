cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
project(ent LANGUAGES C VERSION 0.1.0)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(ENT_ENABLE_PGSQL "Enable PostgreSQL backend" ON)
option(ENT_BUILD_EXAMPLES "Build example programs" ON)
option(ENT_ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENT_ENABLE_ASAN)
    if(MSVC)
        add_compile_options(/fsanitize=address /Zi)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /INCREMENTAL:NO /DEBUG")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /INCREMENTAL:NO /DEBUG")
    else()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
    endif()
endif()

set(ENT_RUNTIME_COMPONENT runtime)
set(ENT_DEVEL_COMPONENT devel)

if(MSVC)
    add_compile_options(/utf-8)
endif()

message(STATUS "Project will be installed to ${CMAKE_INSTALL_PREFIX}")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build type set to ${CMAKE_BUILD_TYPE}")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
    ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR})

# Offer the user the choice of overriding the installation directories
set(INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR} CACHE PATH "Installation directory for libraries")
set(INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR} CACHE PATH "Installation directory for executables")
set(INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR} CACHE PATH "Installation directory for header files")
if(WIN32 AND NOT CYGWIN)
    set(DEF_INSTALL_CMAKEDIR CMake)
else()
    set(DEF_INSTALL_CMAKEDIR share/cmake/${PROJECT_NAME})
endif()
set(INSTALL_CMAKEDIR ${DEF_INSTALL_CMAKEDIR} CACHE PATH "Installation directory for CMake files")

# Report to user
foreach(p LIB BIN INCLUDE CMAKE)
  file(TO_NATIVE_PATH ${CMAKE_INSTALL_PREFIX}/${INSTALL_${p}DIR} _path )
  message(STATUS "Installing ${p} components to ${_path}")
  unset(_path)
endforeach()

message(STATUS "Binary  Dir : ${CMAKE_BINARY_DIR}")
message(STATUS "Install type: ${CMAKE_INSTALL_LIBDIR}")
message(STATUS "Path :${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")

# defines targets and sources
add_subdirectory(comm)

get_property(ENT_ENABLE_SQLITE CACHE ENT_ENABLE_SQLITE PROPERTY VALUE)
get_property(ENT_ENABLE_MYSQL CACHE ENT_ENABLE_MYSQL PROPERTY VALUE)
get_property(ENT_ENABLE_PGSQL CACHE ENT_ENABLE_PGSQL PROPERTY VALUE)
get_property(ENT_ENABLE_LUA CACHE ENT_ENABLE_LUA PROPERTY VALUE)

if(ENT_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()
if(BUILD_TESTING)
    add_subdirectory(test)
    add_subdirectory(test_optional)
    add_subdirectory(test_codegen)
endif()

if(WIN32)
# SQLite is public-domain software and may be bundled in the runtime package.
# MySQL/MariaDB client runtime libraries are deliberately not redistributed by
# ent releases; applications enabling that backend must deploy a compatible
# connector runtime themselves.
if(ENT_SQLITE_RUNTIME)
install(FILES
          ${ENT_SQLITE_RUNTIME}
        DESTINATION
          ${CMAKE_INSTALL_BINDIR}
        COMPONENT
          ${ENT_RUNTIME_COMPONENT}
          )
endif()
endif()

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/entConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/entConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/entConfig.cmake"
    INSTALL_DESTINATION ${INSTALL_CMAKEDIR}
)

install(
    EXPORT entTargets
    FILE entTargets.cmake
    NAMESPACE ent::
    DESTINATION ${INSTALL_CMAKEDIR}
    COMPONENT ${ENT_DEVEL_COMPONENT}
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/entConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/entConfigVersion.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindMariaDB.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindEntLua.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EntMsgCodegen.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_ent_msg.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_ent_msg_symbols.py"
    DESTINATION ${INSTALL_CMAKEDIR}
    COMPONENT ${ENT_DEVEL_COMPONENT}
)

include(InstallRequiredSystemLibraries)
include(CMakeCPack.cmake)
