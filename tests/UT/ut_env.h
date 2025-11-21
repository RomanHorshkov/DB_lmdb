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
int   mdb_env_create(MDB_env** env);
void  mdb_env_close(MDB_env* env);
int   mdb_env_set_maxdbs(MDB_env* env, MDB_dbi dbs);
int   mdb_env_open(MDB_env* env, const char* path, unsigned int flags, mdb_mode_t mode);
int   mdb_dbi_open(MDB_txn* txn, const char* name, unsigned int flags, MDB_dbi* dbi);
int   mdb_dbi_flags(MDB_txn* txn, MDB_dbi dbi, unsigned int* flags);

/* Hook points so individual tests can override LMDB behavior without
 * having to redefine symbols. When these function pointers are NULL a
 * reasonable default stub behavior is used. */
typedef int  (*ut_mdb_env_info_fn)(MDB_env* env, MDB_envinfo* stat);
typedef int  (*ut_mdb_env_set_mapsize_fn)(MDB_env* env, size_t size);
typedef void (*ut_mdb_txn_abort_fn)(MDB_txn* txn);
typedef int  (*ut_mdb_env_create_fn)(MDB_env** env);
typedef void (*ut_mdb_env_close_fn)(MDB_env* env);
typedef int  (*ut_mdb_env_set_maxdbs_fn)(MDB_env* env, MDB_dbi dbs);
typedef int  (*ut_mdb_env_open_fn)(MDB_env* env, const char* path, unsigned int flags, mdb_mode_t mode);
typedef int  (*ut_mdb_dbi_open_fn)(MDB_txn* txn, const char* name, unsigned int flags, MDB_dbi* dbi);
typedef int  (*ut_mdb_dbi_flags_fn)(MDB_txn* txn, MDB_dbi dbi, unsigned int* flags);

extern ut_mdb_env_info_fn         g_ut_mdb_env_info;
extern ut_mdb_env_set_mapsize_fn  g_ut_mdb_env_set_mapsize;
extern ut_mdb_txn_abort_fn        g_ut_mdb_txn_abort;
extern ut_mdb_env_create_fn       g_ut_mdb_env_create;
extern ut_mdb_env_close_fn        g_ut_mdb_env_close;
extern ut_mdb_env_set_maxdbs_fn   g_ut_mdb_env_set_maxdbs;
extern ut_mdb_env_open_fn         g_ut_mdb_env_open;
extern ut_mdb_dbi_open_fn         g_ut_mdb_dbi_open;
extern ut_mdb_dbi_flags_fn        g_ut_mdb_dbi_flags;

/* Reset all LMDB stub hooks back to their defaults. */
void ut_reset_lmdb_stubs(void);

/* Simple ANSI color helpers for UT output. */
#define UT_COLOR_RED   "\x1b[31m"
#define UT_COLOR_GREEN "\x1b[32m"
#define UT_COLOR_BLUE  "\x1b[34m"
#define UT_COLOR_RESET "\x1b[0m"

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_UT_ENV_H */
