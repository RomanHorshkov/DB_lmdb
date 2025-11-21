/**
 * @file ut_env.h
 * @brief Shared unit-test environment: LMDB/EMlog stubs and helpers.
 */

#ifndef DB_LMDB_UT_ENV_H
#define DB_LMDB_UT_ENV_H

#include "core/common.h"
#include "core/operations/ops_int/db/db.h"
#include "lmdb.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Shared global DB handle expected by core code. */
extern DataBase_t* DataBase;

/* EMlog stubs used by the core for logging. */
void emlog_log(eml_level_t level, const char* comp, const char* fmt, ...);
void emlog_log_errno(eml_level_t level, const char* comp, int err, const char* fmt, ...);

/* Minimal LMDB stubs used in the core code paths exercised by UTs. */
char* mdb_strerror(int err);
void  mdb_txn_abort(MDB_txn* txn);
int   mdb_env_info(MDB_env* env, MDB_envinfo* stat);
int   mdb_env_set_mapsize(MDB_env* env, size_t size);

/* Simple ANSI color helpers for UT output. */
#define UT_COLOR_RED   "\x1b[31m"
#define UT_COLOR_GREEN "\x1b[32m"
#define UT_COLOR_BLUE  "\x1b[34m"
#define UT_COLOR_RESET "\x1b[0m"

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_UT_ENV_H */

