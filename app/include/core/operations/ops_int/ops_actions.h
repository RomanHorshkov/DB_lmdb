/**
 * @file ops_actions.h
 * 
 */

#ifndef DB_OPERATIONS_OPS_ACTIONS_H_
#define DB_OPERATIONS_OPS_ACTIONS_H_

#include "ops_internals.h" /* op_t etc */

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * PUBLIC FUNCTIONS PROTOTYPES
 ****************************************************************************
 */


/**
 * @brief Allocate and begin a new LMDB transaction using the safety policy.
 *
 * @details
 * The function begins a transaction and wraps LMDB errors using the
 * `security_check` policy. On success `*out_txn` is populated with a valid
 * `MDB_txn *` that the caller owns and must commit/abort when done. If the
 * function returns anything other than `DB_SAFETY_SUCCESS`, `*out_txn` is
 * guaranteed to be NULL.
 *
 * @param[out] out_txn Pointer to transaction pointer that will be filled on
 *                     success (`*out_txn` becomes non-NULL). Caller must
 *                     call `mdb_txn_commit()`/`mdb_txn_abort()` as
 *                     appropriate after use.
 * @param[in]  flags   LMDB transaction flags passed to `mdb_txn_begin`.
 * @param[out] out_err Optional pointer to receive a negative errno or
 *                     LMDB return code when the function fails.
 *
 * @return `DB_SAFETY_SUCCESS` on success. On transient conditions the function
 *         returns `DB_SAFETY_RETRY` indicating the caller may retry the
 *         begin. `DB_SAFETY_FAIL` indicates a terminal failure.
 */
db_security_ret_code_t act_txn_begin(MDB_txn** out_txn, const unsigned flags, int* const out_err);

/**
 * @brief Commit a transaction and map LMDB results to the safety policy.
 *
 * @details
 * This helper commits the provided transaction and interprets LMDB's return
 * value through the `security_check` policy. On recoverable situations the
 * function may return `DB_SAFETY_RETRY` (for example when mapsize expansion
 * was performed and a retry is suggested). On failure the transaction will
 * be aborted if required by the policy.
 *
 * @param[in] txn      Active transaction previously returned from
 *                    `act_txn_begin`.
 * @param[out] out_err Optional pointer to receive a negative errno or
 *                    LMDB return code when the commit fails.
 *
 * @return `DB_SAFETY_SUCCESS` when commit succeeded. `DB_SAFETY_RETRY` for
 *         retryable conditions. `DB_SAFETY_FAIL` on terminal errors.
 */
db_security_ret_code_t act_txn_commit(MDB_txn* const txn, int* const out_err);

/**
 * @brief Execute a single GET operation.
 *
 * @param[in]  txn  Active LMDB transaction.
 * @param[in,out]  op   Operation descriptor.
 * @param[out] out_err Optional pointer to errno-style error code.
 *
 * @return DB_SAFETY_SUCCESS on success; DB_SAFETY_RETRY if the operation should
 *         be retried; DB_SAFETY_FAIL on permanent failure.
 */
db_security_ret_code_t act_get(MDB_txn* txn, op_t* op, int* const out_err);

/**
 * @brief Execute a single PUT operation.
 *
 * @param[in]  txn  Active LMDB transaction.
 * @param[in,out]  op   Operation descriptor.
 * @param[out] out_err Optional pointer to errno-style error code.
 *
 * @return DB_SAFETY_SUCCESS on success; DB_SAFETY_RETRY if the operation should
 *         be retried; DB_SAFETY_FAIL on permanent failure.
 */
db_security_ret_code_t act_put(MDB_txn* txn, op_t* op, int* const out_err);

#ifdef __cplusplus
}
#endif

#endif /* DB_OPERATIONS_OPS_ACTIONS_H_ */
