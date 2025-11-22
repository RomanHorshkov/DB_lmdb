cmake_minimum_required(VERSION 3.18)

project(db_lmdb LANGUAGES C)

# Export compile_commands.json so IDEs (e.g. VS Code)
# can pick up proper include paths and flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DB_LMDB_ENABLE_IT_COVERAGE "Enable coverage flags for integration tests" OFF)
option(DB_LMDB_ENABLE_UT_COVERAGE "Enable coverage flags for unit tests" OFF)

# ---------------------------------------------------------------------------
# External logging library: EMlog
# Build this first so the rest of the project can link against libemlog.so.
# ---------------------------------------------------------------------------

set(EMLOG_BUILD_STATIC OFF CACHE BOOL "Build the static EMlog archive")
set(EMLOG_BUILD_SHARED ON CACHE BOOL "Build the shared EMlog library")
set(EMLOG_BUILD_TESTS OFF CACHE BOOL "Build EMlog tests")

add_subdirectory(app/external/EMlog)

# ---------------------------------------------------------------------------
# Core LMDB wrapper library (WIP)
# For now just compile the internal core pieces into a static library
# so that IDEs can see a consistent target and can iterate on compile
# errors as flesh out the implementation.
# ---------------------------------------------------------------------------

add_library(db_core STATIC
    app/src/core/core.c
    app/src/core/operations/ops_int/security/security.c
    app/src/core/operations/ops_int/db/dbi_int.c
    app/src/core/operations/ops_int/ops_init.c
    app/src/core/operations/ops_int/ops_actions.c
    app/src/core/operations/ops_int/ops_exec.c
)

target_include_directories(db_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int/db
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int/security
        ${CMAKE_CURRENT_SOURCE_DIR}/app/external/EMlog/app/include
)

target_link_libraries(db_core
    PUBLIC
        emlog_shared
        lmdb
)

# Simple demo executable using the core library.
add_executable(db_core_demo test.c)
target_link_libraries(db_core_demo PRIVATE db_core)

# ---------------------------------------------------------------------------
# Integration tests (cmocka-based) for db_core_init
# ---------------------------------------------------------------------------

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CMOCKA QUIET cmocka)
endif()

if(CMOCKA_FOUND)
    add_library(db_core_cmocka_dep INTERFACE)
    target_include_directories(db_core_cmocka_dep INTERFACE ${CMOCKA_INCLUDE_DIRS})
    target_link_libraries(db_core_cmocka_dep INTERFACE ${CMOCKA_LIBRARIES})
else()
    find_path(CMOCKA_INCLUDE_DIR cmocka.h)
    find_library(CMOCKA_LIBRARY NAMES cmocka)
    if(NOT (CMOCKA_INCLUDE_DIR AND CMOCKA_LIBRARY))
        message(FATAL_ERROR "cmocka could not be found. Install libcmocka-dev (or equivalent) or set CMOCKA_INCLUDE_DIR / CMOCKA_LIBRARY.")
    endif()
    add_library(db_core_cmocka_dep INTERFACE)
    target_include_directories(db_core_cmocka_dep INTERFACE ${CMOCKA_INCLUDE_DIR})
    target_link_libraries(db_core_cmocka_dep INTERFACE ${CMOCKA_LIBRARY})
endif()

add_library(cmocka_db_core::cmocka ALIAS db_core_cmocka_dep)

add_executable(db_core_it_db_core_init
    tests/IT/IT_core/IT_db_core_init.c
)

target_include_directories(db_core_it_db_core_init
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include
)

target_link_libraries(db_core_it_db_core_init
    PRIVATE
        db_core
        cmocka_db_core::cmocka
)

if(DB_LMDB_ENABLE_IT_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(db_core_it_db_core_init PRIVATE --coverage -O2 -g)
        target_link_options(db_core_it_db_core_init PRIVATE --coverage)
    else()
        message(WARNING "DB_LMDB_ENABLE_IT_COVERAGE requested but compiler does not support --coverage")
    endif()
endif()

# ---------------------------------------------------------------------------
# Unit tests (cmocka-based) for internal helpers
# ---------------------------------------------------------------------------

add_executable(db_core_ut_security
    tests/UT/UT_security_errno_map.c
    tests/UT/ut_env.c
)

target_include_directories(db_core_ut_security
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int/db
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int/security
        ${CMAKE_CURRENT_SOURCE_DIR}/app/external/EMlog/app/include
)

target_link_libraries(db_core_ut_security
    PRIVATE
        cmocka_db_core::cmocka
)

add_executable(db_core_ut_ops_actions
    tests/UT/UT_ops_actions.c
    tests/UT/ut_env.c
    app/src/core/operations/ops_int/ops_actions.c
    app/src/core/operations/ops_int/security/security.c
    app/src/core/operations/ops_int/db/dbi_int.c
)

target_include_directories(db_core_ut_ops_actions
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int/db
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int/security
        ${CMAKE_CURRENT_SOURCE_DIR}/app/external/EMlog/app/include
)

target_link_libraries(db_core_ut_ops_actions
    PRIVATE
        cmocka_db_core::cmocka
)

add_executable(db_core_ut_dbi_int
    tests/UT/UT_dbi_int.c
    tests/UT/ut_env.c
    app/src/core/operations/ops_int/db/dbi_int.c
)

target_include_directories(db_core_ut_dbi_int
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int/db
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int/security
        ${CMAKE_CURRENT_SOURCE_DIR}/app/external/EMlog/app/include
)

target_link_libraries(db_core_ut_dbi_int
    PRIVATE
        cmocka_db_core::cmocka
)

add_executable(db_core_ut_ops_init
    tests/UT/UT_ops_init.c
    tests/UT/ut_env.c
    app/src/core/operations/ops_int/db/dbi_int.c
    app/src/core/operations/ops_int/security/security.c
)

target_include_directories(db_core_ut_ops_init
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int/db
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include/core/operations/ops_int/security
        ${CMAKE_CURRENT_SOURCE_DIR}/app/external/EMlog/app/include
)

target_link_libraries(db_core_ut_ops_init
    PRIVATE
        cmocka_db_core::cmocka
)

if(DB_LMDB_ENABLE_UT_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(db_core_ut_security PRIVATE --coverage -O2 -g)
        target_link_options(db_core_ut_security PRIVATE --coverage)
        target_compile_options(db_core_ut_ops_actions PRIVATE --coverage -O2 -g)
        target_link_options(db_core_ut_ops_actions PRIVATE --coverage)
        target_compile_options(db_core_ut_dbi_int PRIVATE --coverage -O2 -g)
        target_link_options(db_core_ut_dbi_int PRIVATE --coverage)
        target_compile_options(db_core_ut_ops_init PRIVATE --coverage -O2 -g)
        target_link_options(db_core_ut_ops_init PRIVATE --coverage)
    else()
        message(WARNING "DB_LMDB_ENABLE_UT_COVERAGE requested but compiler does not support --coverage")
    endif()
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------

add_executable(bench_db_init
    tests/benchmarks/bench_db_init.c
)

target_include_directories(bench_db_init
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include
)

target_link_libraries(bench_db_init
    PRIVATE
        db_core
        m  # Math library for sqrt()
)

add_executable(bench_db_write
    tests/benchmarks/bench_db_write.c
)

target_include_directories(bench_db_write
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include
)

target_link_libraries(bench_db_write
    PRIVATE
        db_core
        m  # Math library for sqrt()
)

add_executable(bench_db_get_batch
    tests/benchmarks/bench_db_get_batch.c
)

target_include_directories(bench_db_get_batch
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include
)

target_link_libraries(bench_db_get_batch
    PRIVATE
        db_core
        m  # Math library for sqrt()
)

add_executable(bench_db_read_mt
    tests/benchmarks/bench_db_read_mt.c
)

target_include_directories(bench_db_read_mt
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include
)

target_link_libraries(bench_db_read_mt
    PRIVATE
        db_core
        m
        pthread
)
