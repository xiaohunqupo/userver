option(USERVER_MYSQL_OLD_INCLUDE "If yes, uses old mysql/mysql.h instead of mariadb/mysql.h" OFF)
if(USERVER_MYSQL_OLD_INCLUDE)
  set(MYSQL_H mysql/mysql.h)
else()
  set(MYSQL_H mariadb/mysql.h)
endif()

_userver_module_begin(
    NAME libmariadb
    VERSION 3.0.3
    DEBIAN_NAMES libmariadb-dev
)

_userver_module_find_include(
    NAMES ${MYSQL_H}
)

_userver_module_find_library(
    NAMES mariadb
    PATH_SUFFIXES lib
)

_userver_module_end()
