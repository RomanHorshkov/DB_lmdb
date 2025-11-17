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
#include <stddef.h>
#include <stdint.h>

#define DB_LMDB_RETRY_TRANSACTION 2
#define DB_LMDB_RETRY_DBI_OPEN    3
#define DB_LMDB_RETRY_DBI_FLAGS   3

/**
 * @brief Create an LMDB environment using safety policy (no retry here).
 *
 * @param env        Filled with created environment on success.
 * @param path       Filesystem path for the environment.
 * @param max_dbis   Maximum number of sub-databases.
 * @param db_map_size Initial map size.
 * @return 0 on success, errno-style code on failure.
 */
int db_lmdb_create_env_safe(struct DB* DataBase, const char* path, unsigned int max_dbis,
                            size_t db_map_size);

/**
 * @brief Begin a transaction using safety policy (resize+retry baked in).
 * 
 * @param env        LMDB environment.
 * @param flags      LMDB transaction flags.
 * @param out_txn    Filled with started transaction on success.
 * @param retry_budget Optional retry counter (decremented on retry). May be NULL.
 * @param out_err    Optional mapped errno on FAIL.
 * @return DB_SAFETY_OK/RETRY/FAIL
 */
int db_lmdb_txn_begin_safe(MDB_env* env, unsigned flags, MDB_txn** out_txn, size_t* retry_budget,
                           int* out_err);

/**
 * @brief Commit a transaction using safety policy (resize+retry baked in).
 * 
 * @param txn          LMDB transaction to commit.
 * @param retry_budget Optional retry counter (decremented on retry). May be NULL.
 * @param out_err      Optional mapped errno on FAIL.
 * @return DB_SAFETY_OK/RETRY/FAIL
 */
int db_lmdb_txn_commit_safe(MDB_txn* txn, size_t* retry_budget, int* out_err);

/**
 * @brief Open a named DBI using the safety policy (resize+retry guidance).
 * 
 * @param txn           Active LMDB transaction (write).
 * @param name          DBI name.
 * @param open_flags    Flags passed to mdb_dbi_open.
 * @param out_dbi       Filled with DBI handle on success.
 * @param retry_budget  Optional retry counter (decremented on retry). May be NULL.
 * @param out_mdb_rc    Optional raw LMDB rc for logging.
 * @param out_err       Optional mapped errno on FAIL/RETRY.
 * @return DB_SAFETY_OK/RETRY/FAIL
 */
int db_lmdb_dbi_open_safe(MDB_txn* txn, const char* name, unsigned int open_flags, MDB_dbi* out_dbi,
                          size_t* retry_budget, int* out_mdb_rc, int* out_err);

/**
 * @brief Fetch DBI flags using the safety policy (resize+retry guidance).
 * 
 * @param txn           Active LMDB transaction.
 * @param dbi           DBI handle.
 * @param out_flags     Filled with DBI flags on success.
 * @param retry_budget  Optional retry counter (decremented on retry). May be NULL.
 * @param out_mdb_rc    Optional raw LMDB rc for logging.
 * @param out_err       Optional mapped errno on FAIL/RETRY.
 * @return DB_SAFETY_OK/RETRY/FAIL
 */
int db_lmdb_dbi_get_flags_safe(MDB_txn* txn, MDB_dbi dbi, unsigned int* out_flags,
                               size_t* retry_budget, int* out_mdb_rc, int* out_err);

/**
 * @brief Perform mdb_put with safety policy (resize+retry guidance).
 *
 * @param txn           Active write transaction.
 * @param dbi           Target DBI handle.
 * @param key           Key buffer (non-owning).
 * @param data          Value buffer (non-owning).
 * @param flags         mdb_put flags.
 * @param retry_budget  Optional retry counter (decremented on retry). May be NULL.
 * @param out_mdb_rc    Optional raw LMDB rc for logging.
 * @param out_err       Optional mapped errno on FAIL/RETRY.
 * @return DB_SAFETY_OK/RETRY/FAIL
 */
int db_lmdb_put_safe(MDB_txn* txn, MDB_dbi dbi, MDB_val* key, MDB_val* data, unsigned int flags,
                     size_t* retry_budget, int* out_mdb_rc, int* out_err);

/**
 * @brief Perform mdb_get with safety policy (resize+retry guidance).
 *
 * @param txn           Active transaction (read or write).
 * @param dbi           Target DBI handle.
 * @param key           Key buffer (non-owning).
 * @param data          Filled with value on success.
 * @param retry_budget  Optional retry counter (decremented on retry). May be NULL.
 * @param out_mdb_rc    Optional raw LMDB rc for logging.
 * @param out_err       Optional mapped errno on FAIL/RETRY.
 * @return DB_SAFETY_OK/RETRY/FAIL (MDB_NOTFOUND maps to FAIL)
 */
int db_lmdb_get_safe(MDB_txn* txn, MDB_dbi dbi, MDB_val* key, MDB_val* data, size_t* retry_budget,
                     int* out_mdb_rc, int* out_err);

/**
 * @brief Expose the core safety decision helper for callers needing fine control.
 */
int db_lmdb_safety_check(int mdb_rc, int is_write_txn, size_t* retry_budget, int* out_mapped_err,
                         MDB_txn* txn);

#endif /* DB_LMDB_CORE_H */
