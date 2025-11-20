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


/**
 * @brief Key kind for an operation.
 */
typedef enum
{
    OP_KEY_KIND_PRESENT = 0, /**< key is provided */
    OP_KEY_KIND_LOOKUP  = 1  /**< key is to be looked up from prev op */
} op_key_kind_t;

/**
 * @brief Source of the key for a referenced operation.
 */
typedef enum
{
    OP_KEY_SRC_KEY = 0, /**< key is from some prev operation's key */
    OP_KEY_SRC_VAL = 1  /**< key is from some prev operation's value */
} op_key_source_t;

typedef struct
{
    op_key_source_t src_type; /**< Source type (key or value). */
    unsigned int    op_index; /**< Index of the operation to source key from. */
} op_val_lookup_t;

/**
 * @brief Value descriptor for an operation.
 *
 * This struct is intentionally layout-compatible with LMDB's MDB_val:
 * the @ref size and @ref ptr fields mirror `mv_size` and `mv_data`.
 * This allows internal code to cast between op_val_t* and MDB_val*
 * without copying when calling LMDB primitives.
 *
 * @note The pointer stored in @ref ptr is typically owned by LMDB
 *       (e.g., from mdb_get) and is only valid for the lifetime of
 *       the transaction and as long as no write invalidates the
 *       underlying page. Callers must not assume it survives across
 *       transactions or arbitrary write operations.
 */
typedef struct
{
    size_t size; /**< Size of value bytes. */
    void*  ptr;  /**< Pointer to value bytes. */
} op_val_t;

/**
 * @brief Key descriptor for an operation.
 * 
 * This struct describes how to obtain the key for an operation,
 * either by providing the bytes directly or by referencing a previous
 * operation's key or value.
 */
typedef struct
{
    op_key_kind_t kind; /**< Key kind (present vs lookup). */

    union
    {
        op_val_t present;             /**< Present key info. */
        op_val_lookup_t lookup;      /**< Lookup key info. */
    };

} op_key_t;

typedef struct
{
    unsigned int dbi;  /**< Target DBI handle. */
    op_type_t    type; /**< Operation type. */
    op_key_t     key;  /**< Key descriptor. */
    op_key_t     val;  /**< Value descriptor. */
} op_t;

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
 * @return `DB_SAFETY_SUCCESS` on success, `DB_SAFETY_RETRY` to indicate the
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
 * @return `DB_SAFETY_SUCCESS` when commit succeeded. `DB_SAFETY_RETRY` for
 *         retryable conditions. `DB_SAFETY_FAIL` on terminal errors.
 */
db_security_ret_code_t ops_txn_commit(MDB_txn* const txn, int* const out_err);

#endif /* DB_OPERATIONS_OPS_SETUP_H_ */
