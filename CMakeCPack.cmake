set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "ent")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ent: entool library")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")

set(CPACK_PACKAGING_INSTALL_PREFIX "/${PROJECT_NAME}")

set(CPACK_PACKAGE_INSTALL_DIRECTORY "ent")

set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")

set(CPACK_SOURCE_IGNORE_FILES "${PROJECT_BINARY_DIR};/.git/;.gitignore;.vscode")
set(CPACK_SOURCE_GENERATOR "ZIP;TGZ")
set(CPACK_GENERATOR "ZIP;TGZ")

if(UNIX)
  if(CMAKE_SYSTEM_NAME MATCHES Linux)
    list(APPEND CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "if16888@foxmail.com")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "mysql")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "sqlite")

    list(APPEND CPACK_GENERATOR "RPM")
    set(CPACK_RPM_PACKAGE_RELEASE "1")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_REQUIRES "mysql")
    set(CPACK_RPM_PACKAGE_REQUIRES "sqlite")
  endif()
endif()

if(WIN32 OR MINGW)
  list(APPEND CPACK_GENERATOR "NSIS")
  set(CPACK_NSIS_PACKAGE_NAME "ent")
  set(CPACK_NSIS_CONTACT "if16888@foxmail.com")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  
endif()

message(STATUS "CPack generators: ${CPACK_GENERATOR}")

include(CPack)