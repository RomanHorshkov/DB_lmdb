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
