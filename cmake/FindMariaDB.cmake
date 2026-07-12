if(TARGET MariaDB::MariaDB)
  set(MariaDB_FOUND TRUE)
  return()
endif()

find_package(unofficial-libmariadb CONFIG QUIET)
if(TARGET unofficial::libmariadb)
  add_library(MariaDB::MariaDB INTERFACE IMPORTED)
  set_property(TARGET MariaDB::MariaDB PROPERTY
    INTERFACE_LINK_LIBRARIES unofficial::libmariadb)
  set(MariaDB_FOUND TRUE)
  return()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PC_MariaDB QUIET IMPORTED_TARGET mariadb)
  if(NOT PC_MariaDB_FOUND)
    pkg_check_modules(PC_MariaDB QUIET IMPORTED_TARGET mysqlclient)
  endif()
endif()

find_path(MariaDB_INCLUDE_DIR mysql.h
  HINTS ${PC_MariaDB_INCLUDE_DIRS}
  PATH_SUFFIXES mysql mariadb)
find_library(MariaDB_LIBRARY
  NAMES mariadb libmariadb mysqlclient libmysql
  HINTS ${PC_MariaDB_LIBRARY_DIRS})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MariaDB
  REQUIRED_VARS MariaDB_LIBRARY MariaDB_INCLUDE_DIR)

if(MariaDB_FOUND)
  add_library(MariaDB::MariaDB UNKNOWN IMPORTED)
  set_target_properties(MariaDB::MariaDB PROPERTIES
    IMPORTED_LOCATION "${MariaDB_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MariaDB_INCLUDE_DIR}")
endif()

mark_as_advanced(MariaDB_INCLUDE_DIR MariaDB_LIBRARY)
