cmake_minimum_required(VERSION 3.18)

project(db_lmdb LANGUAGES C)

# Export compile_commands.json so IDEs (e.g. VS Code)
# can pick up proper include paths and flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
