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
 * @return `DB_SAFETY_SUCCESS` on success, `DB_SAFETY_RETRY` to indicate the
 *         caller may retry the operation, or `DB_SAFETY_FAIL` on a
 *         non-recoverable error.
 */
db_security_ret_code_t ops_init_env(const unsigned int max_dbis, const char* const path,
                                    const unsigned int mode, int* const out_err);

/**
 * @brief Open a sub-database (DBI) within the environment.
 * 
 * @details
 * This function opens (or creates if missing) a named sub-database (DBI)
 * within the LMDB environment using the provided transaction. The DBI is
 * identified by its name and index, and is configured according to the
 * specified type.
 * 
 * @param[in]  txn       Active LMDB transaction.
 * @param[in]  name      Name of the sub-database to open.
 * @param[in]  dbi_idx   Index of the DBI to open (must be < DataBase->n_dbis).
 * @param[in]  dbi_type  Type of the DBI (see dbi_type_t).
 * @param[out] out_err   Optional pointer to receive a negative errno or
 *                       LMDB code on failure (NULL to ignore).
 */
db_security_ret_code_t ops_init_dbi(MDB_txn* const txn, const char* const name,
                                    unsigned int dbi_idx, dbi_type_t dbi_type, int* const out_err);


#endif /* DB_OPERATIONS_OPS_SETUP_H_ */
