/**
 * @file db_lmdb_safety.h
 * @brief Centralized LMDB return-code policy and retry/resize guidance.
 *
 * This layer converts raw LMDB status codes into actionable decisions
 * so call sites stay small and consistent.
 */

#ifndef DB_LMDB_CORE_H
#define DB_LMDB_CORE_H

#include <lmdb.h>

enum db_lmdb_safety_ret_code
{
    DB_LMDB_SAFE_OK = 0, /* proceed */
    DB_LMDB_SAFE_RETRY,  /* retry the operation after cleanup */
    DB_LMDB_SAFE_FAIL    /* fail with mapped errno */
};

/**
 * @brief Create an LMDB environment using safety policy (no retry here).
 *
 * @param env        Filled with created environment on success.
 * @param path       Filesystem path for the environment.
 * @param max_dbis   Maximum number of sub-databases.
 * @param db_map_size Initial map size.
 * @return 0 on success, errno-style code on failure.
 */
int db_lmdb_core_create_env_safe(struct DB* DataBase, const char* path, unsigned int max_dbis,
                                 size_t db_map_size);

/**
 * @brief Begin a transaction using safety policy (resize+retry baked in).
 * 
 * @param env        LMDB environment.
 * @param flags      LMDB transaction flags.
 * @param out_txn    Filled with started transaction on success.
 * @param out_err    Optional mapped errno on FAIL.
 * @return DB_LMDB_SAFE_OK/RETRY/FAIL
 */
int db_lmdb_txn_begin_safe(MDB_env* env, unsigned flags, MDB_txn** out_txn, int* out_err);

/**
 * @brief Commit a transaction using safety policy (resize+retry baked in).
 * 
 * @param txn          LMDB transaction to commit.
 * @param retry_budget Optional retry counter (decremented on retry). May be NULL.
 * @param out_err      Optional mapped errno on FAIL.
 * @return DB_LMDB_SAFE_OK/RETRY/FAIL
 */
int db_lmdb_txn_commit_safe(MDB_txn* txn, size_t* retry_budget, int* out_err);

#endif /* DB_LMDB_CORE_H */
