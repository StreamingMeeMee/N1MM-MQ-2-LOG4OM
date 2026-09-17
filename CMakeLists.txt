cmake_minimum_required(VERSION 3.16)
project(n1mm_mq_2_log4om C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_executable(n1mm-mq-2-log4om
    src/main.c
    src/config.c
    src/json.c
    src/xmlflat.c
    src/mq_consumer.c
    src/db_client.c
    src/log.c
)

target_include_directories(n1mm-mq-2-log4om PRIVATE src)

if(MSVC)
    target_compile_options(n1mm-mq-2-log4om PRIVATE /W4)
else()
    target_compile_options(n1mm-mq-2-log4om PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Locate rabbitmq-c (librabbitmq). External dependency the user installs
# themselves (see README.md); try several common ways CMake can discover it.
# ---------------------------------------------------------------------------
set(N1MM_RABBITMQ_FOUND FALSE)

find_package(unofficial-rabbitmq-c CONFIG QUIET)
if(unofficial-rabbitmq-c_FOUND AND TARGET unofficial::rabbitmq-c::rabbitmq)
    target_link_libraries(n1mm-mq-2-log4om PRIVATE unofficial::rabbitmq-c::rabbitmq)
    set(N1MM_RABBITMQ_FOUND TRUE)
endif()

if(NOT N1MM_RABBITMQ_FOUND)
    find_package(rabbitmq-c CONFIG QUIET)
    if(rabbitmq-c_FOUND)
        if(TARGET rabbitmq::rabbitmq-static)
            target_link_libraries(n1mm-mq-2-log4om PRIVATE rabbitmq::rabbitmq-static)
            set(N1MM_RABBITMQ_FOUND TRUE)
        elseif(TARGET rabbitmq::rabbitmq)
            target_link_libraries(n1mm-mq-2-log4om PRIVATE rabbitmq::rabbitmq)
            set(N1MM_RABBITMQ_FOUND TRUE)
        endif()
    endif()
endif()

if(NOT N1MM_RABBITMQ_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(RABBITMQC QUIET librabbitmq)
        if(RABBITMQC_FOUND)
            target_include_directories(n1mm-mq-2-log4om PRIVATE ${RABBITMQC_INCLUDE_DIRS})
            target_link_directories(n1mm-mq-2-log4om PRIVATE ${RABBITMQC_LIBRARY_DIRS})
            target_link_libraries(n1mm-mq-2-log4om PRIVATE ${RABBITMQC_LIBRARIES})
            set(N1MM_RABBITMQ_FOUND TRUE)
        endif()
    endif()
endif()

if(NOT N1MM_RABBITMQ_FOUND)
    find_path(RABBITMQC_INCLUDE_DIR NAMES amqp.h)
    find_library(RABBITMQC_LIBRARY NAMES rabbitmq librabbitmq)
    if(RABBITMQC_INCLUDE_DIR AND RABBITMQC_LIBRARY)
        target_include_directories(n1mm-mq-2-log4om PRIVATE ${RABBITMQC_INCLUDE_DIR})
        target_link_libraries(n1mm-mq-2-log4om PRIVATE ${RABBITMQC_LIBRARY})
        set(N1MM_RABBITMQ_FOUND TRUE)
    endif()
endif()

if(NOT N1MM_RABBITMQ_FOUND)
    message(FATAL_ERROR
        "Could not find rabbitmq-c (librabbitmq). Install it first:\n"
        "  Linux (Debian/Ubuntu): sudo apt install librabbitmq-dev\n"
        "  Linux (Fedora/RHEL):   sudo dnf install librabbitmq-devel\n"
        "  Windows (vcpkg):       vcpkg install rabbitmq-c\n"
        "                         then configure with -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake\n"
        "  Windows (MSYS2):       pacman -S mingw-w64-x86_64-rabbitmq-c\n"
        "  Or build rabbitmq-c from source: https://github.com/alanxz/rabbitmq-c")
endif()

# ---------------------------------------------------------------------------
# Locate a MySQL-compatible C client (MariaDB Connector/C or Oracle's
# libmysqlclient -- both expose the same mysql.h API). External dependency.
# ---------------------------------------------------------------------------
set(N1MM_MYSQL_FOUND FALSE)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT N1MM_MYSQL_FOUND)
    pkg_check_modules(MYSQLC QUIET libmariadb)
    if(MYSQLC_FOUND)
        target_include_directories(n1mm-mq-2-log4om PRIVATE ${MYSQLC_INCLUDE_DIRS})
        target_link_directories(n1mm-mq-2-log4om PRIVATE ${MYSQLC_LIBRARY_DIRS})
        target_link_libraries(n1mm-mq-2-log4om PRIVATE ${MYSQLC_LIBRARIES})
        set(N1MM_MYSQL_FOUND TRUE)
    endif()
endif()

if(PkgConfig_FOUND AND NOT N1MM_MYSQL_FOUND)
    pkg_check_modules(MYSQLC QUIET mysqlclient)
    if(MYSQLC_FOUND)
        target_include_directories(n1mm-mq-2-log4om PRIVATE ${MYSQLC_INCLUDE_DIRS})
        target_link_directories(n1mm-mq-2-log4om PRIVATE ${MYSQLC_LIBRARY_DIRS})
        target_link_libraries(n1mm-mq-2-log4om PRIVATE ${MYSQLC_LIBRARIES})
        set(N1MM_MYSQL_FOUND TRUE)
    endif()
endif()

if(NOT N1MM_MYSQL_FOUND)
    find_program(MYSQL_CONFIG_EXECUTABLE NAMES mariadb_config mysql_config)
    if(MYSQL_CONFIG_EXECUTABLE)
        execute_process(COMMAND ${MYSQL_CONFIG_EXECUTABLE} --cflags OUTPUT_VARIABLE MYSQLC_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND ${MYSQL_CONFIG_EXECUTABLE} --libs OUTPUT_VARIABLE MYSQLC_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
        separate_arguments(MYSQLC_CFLAGS_LIST UNIX_COMMAND "${MYSQLC_CFLAGS}")
        separate_arguments(MYSQLC_LIBS_LIST UNIX_COMMAND "${MYSQLC_LIBS}")
        target_compile_options(n1mm-mq-2-log4om PRIVATE ${MYSQLC_CFLAGS_LIST})
        target_link_libraries(n1mm-mq-2-log4om PRIVATE ${MYSQLC_LIBS_LIST})
        set(N1MM_MYSQL_FOUND TRUE)
    endif()
endif()

if(NOT N1MM_MYSQL_FOUND)
    find_path(MYSQLC_INCLUDE_DIR NAMES mysql.h PATH_SUFFIXES mysql mariadb)
    find_library(MYSQLC_LIBRARY NAMES mariadbclient mariadb libmariadb mysqlclient)
    if(MYSQLC_INCLUDE_DIR AND MYSQLC_LIBRARY)
        target_include_directories(n1mm-mq-2-log4om PRIVATE ${MYSQLC_INCLUDE_DIR})
        target_link_libraries(n1mm-mq-2-log4om PRIVATE ${MYSQLC_LIBRARY})
        set(N1MM_MYSQL_FOUND TRUE)
    endif()
endif()

if(NOT N1MM_MYSQL_FOUND)
    message(FATAL_ERROR
        "Could not find a MySQL-compatible client library (MariaDB Connector/C or\n"
        "libmysqlclient). Install it first:\n"
        "  Linux (Debian/Ubuntu): sudo apt install libmariadb-dev (or libmysqlclient-dev)\n"
        "  Linux (Fedora/RHEL):   sudo dnf install mariadb-connector-c-devel\n"
        "  Windows (vcpkg):       vcpkg install libmariadb (or libmysql)\n"
        "  Windows (MSYS2):       pacman -S mingw-w64-x86_64-libmariadbclient")
endif()

# ---------------------------------------------------------------------------
# Platform-specific libraries. No threads are used in this app (single
# RabbitMQ connection, single MySQL connection).
# ---------------------------------------------------------------------------
if(WIN32)
    target_link_libraries(n1mm-mq-2-log4om PRIVATE ws2_32 iphlpapi)
endif()

install(TARGETS n1mm-mq-2-log4om RUNTIME DESTINATION bin)
