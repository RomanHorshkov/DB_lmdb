/**
 * @file security.h
 * @brief Centralized LMDB return-code policy and retry/resize guidance.
 */

#ifndef DB_LMDB_SECURITY_H
#define DB_LMDB_SECURITY_H

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
*/
/* None */

/****************************************************************************
 * PUBLIC STRUCTURED TYPES
 ****************************************************************************
*/
typedef enum
{
    DB_SAFETY_SUCCESS    = 0, /* proceed */
    DB_SAFETY_RETRY = 3, /* retry */
    DB_SAFETY_FAIL  = 7  /* fail */
} db_security_ret_code_t;

/************************************************************************
 * PUBLIC VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Evaluate an LMDB return code and advise caller action.
 *
 * @details
 * The function maps the LMDB return code `mdb_rc` to the project's safety
 * policy `db_security_ret_code_t` which indicates whether the caller should
 * treat the operation as successful, retry the operation, or consider it a
 * permanent failure. When requested via `out_errno`, it also provides a
 * POSIX-style `errno` mapping for the underlying condition.
 *
 * Decision summary:
 * - MDB_SUCCESS -> `DB_SAFETY_SUCCESS`
 * - Resizable or transient conditions (e.g. `MDB_MAP_RESIZED`, `MDB_MAP_FULL`,
 *   `MDB_PAGE_FULL`, `MDB_TXN_FULL`, `MDB_CURSOR_FULL`, `MDB_BAD_RSLOT`,
 *   `MDB_READERS_FULL`) -> abort supplied txn (if any) and either attempt map
 *   expansion (on `MDB_MAP_FULL`) or return `DB_SAFETY_RETRY` to indicate the
 *   caller may retry the operation.
 * - Logic-level results (e.g. `MDB_NOTFOUND`, `MDB_KEYEXIST`) -> `DB_SAFETY_FAIL`
 *   (transaction is not necessarily invalidated for these cases).
 * - Any other LMDB error -> log, abort the txn (if any) and return
 *   `DB_SAFETY_FAIL`.
 *
 * Side effects:
 * - The function may call `mdb_txn_abort(txn)` when `txn` is non-NULL and the
 *   error requires transaction invalidation.
 * - On `MDB_MAP_FULL` it will attempt `_expand_env_mapsize()`; if expansion
 *   succeeds the function returns `DB_SAFETY_RETRY` to indicate the caller may
 *   retry the operation.
 *
 * @param mdb_rc The raw LMDB return code to evaluate.
 * @param txn Optional pointer to an LMDB transaction. If provided and the
 *            detected error requires invalidation, the transaction will be
 *            aborted by this function.
 * @param out_errno Optional output pointer. When non-NULL it will be filled
 *                  with a negative POSIX-style `errno` mapping for the
 *                  provided `mdb_rc` (or 0 for `MDB_SUCCESS`).
 *
 * @return One of db_security_ret_code_t.
 */
db_security_ret_code_t security_check(const int mdb_rc, MDB_txn* txn, int* const out_errno);

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_SECURITY_H */
