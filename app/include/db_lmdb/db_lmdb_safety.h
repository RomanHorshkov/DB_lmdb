/**
 * @file db_lmdb_safety.h
 * @brief Centralized LMDB return-code policy and retry/resize guidance.
 *
 * This layer converts raw LMDB status codes into actionable decisions
 * so call sites stay small and consistent.
 */

#ifndef DB_LMDB_SAFETY_H
#define DB_LMDB_SAFETY_H

#include <lmdb.h>

enum db_lmdb_safety
{
    DB_LMDB_SAFE_OK = 0, /* proceed */
    DB_LMDB_SAFE_RETRY,  /* retry the operation after cleanup */
    DB_LMDB_SAFE_FAIL    /* fail with mapped errno */
};

/**
 * @brief Central decision helper; may resize map internally.
 *
 * Handles retries (with optional @p retry_budget) and returns a simple action.
 *
 * @param mdb_rc         LMDB return code to evaluate.
 * @param is_write_txn   Non-zero if the op was in a write txn.
 * @param retry_budget   Optional remaining retries counter (decremented on retry). May be NULL.
 * @param out_mapped_err Optional mapped errno-style code on FAIL. May be NULL.
 * @param txn            Optional txn to abort on RETRY/FAIL. May be NULL.
 * @return DB_LMDB_SAFE_OK/RETRY/FAIL
 */
int db_lmdb_safety_decide(int mdb_rc, int is_write_txn, size_t* retry_budget, int* out_mapped_err,
                          MDB_txn* txn);

/**
 * @brief Begin a transaction using safety policy (resize+retry baked in).
 *
 * @param env          LMDB environment.
 * @param flags        mdb_txn_begin flags.
 * @param out_txn      Filled on success.
 * @param retry_budget Optional retry counter (decremented on retry). May be NULL.
 * @param out_err      Optional mapped errno on FAIL.
 * @return DB_LMDB_SAFE_OK/RETRY/FAIL
 */
int db_lmdb_txn_begin_safe(MDB_env* env, unsigned flags, MDB_txn** out_txn, size_t* retry_budget,
                           int* out_err);

#endif /* DB_LMDB_SAFETY_H */
