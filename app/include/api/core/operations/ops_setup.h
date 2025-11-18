/**
 * @file ops_setup.h
 * @brief LMDB environment setup and transaction helpers with safety policy.
 *
 * @details
 * This header declares operations used to initialize and manage the LMDB
 * environment and to start/commit transactions using the project's
 * centralized safety policy (`db_security_ret_code_t`). The functions
 * return a safety decision (OK / RETRY / FAIL) and optionally provide a
 * POSIX-style `errno` mapping via `out_err` for callers that integrate
 * with standard error reporting.
 *
 * The module cooperates with `security.h` which contains the decision
 * enums and helpers used to map LMDB return codes into the safety
 * policy and `errno` values.
 */

#ifndef DB_OPERATIONS_OPS_SETUP_H_
#define DB_OPERATIONS_OPS_SETUP_H_

#include "db.h"
#include "security.h" /* expose security policy and helpers */

/****************************************************************************
 * PUBLIC FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Create and open an LMDB environment configured for this application.
 *
 * @details
 * This function performs the necessary steps to create (if missing) and
 * configure the LMDB environment used by the database subsystem. Typical
 * actions include creating the directory, opening the LMDB environment via
 * `mdb_env_create`/`mdb_env_open`, setting map size, max DBs and reader
 * parameters according to compiled configuration and runtime input.
 *
 * On failure the function will return `DB_SAFETY_RETRY` for transient
 * or retryable conditions (for example when `security_check` indicates the
 * environment can be expanded), or `DB_SAFETY_FAIL` on terminal failures.
 * When `out_err` is provided it will contain a negative POSIX-style errno
 * or LMDB return code mapped by the security layer.
 *
 * @param[in]  max_dbis  Maximum number of named sub-databases to support.
 * @param[in]  path      Filesystem path to the database directory.
 * @param[in]  mode      Filesystem mode (owner/group/other bits) used when
 *                       creating directories or files.
 * @param[out] out_err   Optional pointer to receive a negative errno or
 *                       LMDB code on failure (NULL to ignore).
 *
 * @return `DB_SAFETY_OK` on success, `DB_SAFETY_RETRY` to indicate the
 *         caller may retry the operation, or `DB_SAFETY_FAIL` on a
 *         non-recoverable error.
 */
db_security_ret_code_t ops_init_env(const unsigned int max_dbis, const char* const path,
                                    const unsigned int mode, int* const out_err);

/**
 * @brief Allocate and begin a new LMDB transaction using the safety policy.
 *
 * @details
 * The function begins a transaction and wraps LMDB errors using the
 * `security_check` policy. On success `*out_txn` is populated with a valid
 * `MDB_txn *` that the caller owns and must commit/abort when done. If the
 * function returns anything other than `DB_SAFETY_OK`, `*out_txn` is
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
 * @return `DB_SAFETY_OK` on success. On transient conditions the function
 *         returns `DB_SAFETY_RETRY` indicating the caller may retry the
 *         begin. `DB_SAFETY_FAIL` indicates a terminal failure.
 */
db_security_ret_code_t ops_txn_begin(MDB_txn** out_txn, const unsigned flags, int* const out_err);

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
 *                    `ops_txn_begin`.
 * @param[out] out_err Optional pointer to receive a negative errno or
 *                    LMDB return code when the commit fails.
 *
 * @return `DB_SAFETY_OK` when commit succeeded. `DB_SAFETY_RETRY` for
 *         retryable conditions. `DB_SAFETY_FAIL` on terminal errors.
 */
db_security_ret_code_t ops_txn_commit(MDB_txn* const txn, int* const out_err);

#endif /* DB_OPERATIONS_OPS_SETUP_H_ */
