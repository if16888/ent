cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(ent LANGUAGES C VERSION 0.1)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

message(STATUS "Project will be installed to ${CMAKE_INSTALL_PREFIX}")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build type set to ${CMAKE_BUILD_TYPE}")

include(GNUInstallDirs)
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
message(ATATUS "Path :${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
# defines targets and sources
add_subdirectory(comm)
add_subdirectory(example)
# enable testing and define tests
enable_testing()
add_subdirectory(test)

if(WIN32)
if(ENT_MYSQL_RUNTIME)
install(FILES
          ${ENT_MYSQL_RUNTIME}
        DESTINATION
          ${CMAKE_INSTALL_BINDIR}
          )
endif()
if(ENT_SQLITE_RUNTIME)
install(FILES
          ${ENT_SQLITE_RUNTIME}
        DESTINATION
          ${CMAKE_INSTALL_BINDIR}
          )
endif()
endif()


include(InstallRequiredSystemLibraries)
include(CMakeCPack.cmake)
