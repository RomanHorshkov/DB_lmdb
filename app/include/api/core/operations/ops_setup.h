/**
 * @file ops_exec.h
 */

#ifndef DB_OPERATIONS_OPS_EXEC_H_
#define DB_OPERATIONS_OPS_EXEC_H_

#include "db.h"
#include "security.h" /* espose for ops_exec */

/****************************************************************************
 * PUBLIC FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Create and configure LMDB environment for operations.
 * 
 * @param[in] max_dbis     Maximum number of sub-databases.
 * @param[in] db_map_size  Initial map size in bytes.
 * @param[in] path         Filesystem path to the database directory.
 * @param[in] mode         Filesystem mode for created files/directories.
 * @param[out] out_err     Optional mapped errno on failure.
 * @return DB_SAFETY_OK/RETRY/FAIL
 */
db_security_ret_code_t ops_init_env(const unsigned int max_dbis, const size_t db_map_size,
                                    const char* const path, const unsigned int mode,
                                    int* const out_err);

/**
 * @brief Begin a new LMDB transaction with safety policy.
 * 
 * @param[out] out_txn  Filled with new transaction on success.
 * @param[in]  flags    LMDB transaction flags.
 * @param[out] out_err  Optional mapped errno on failure.
 * @return DB_SAFETY_OK/RETRY/FAIL
 */
db_security_ret_code_t ops_txn_begin(MDB_txn** out_txn, const unsigned flags, int* const out_err);

/**
 * @brief Commit an LMDB transaction with safety policy.
 * 
 * @param[in] txn       Active transaction to commit.
 * @param[out] out_err  Optional mapped errno on failure.
 * @return DB_SAFETY_OK/RETRY/FAIL
 */
db_security_ret_code_t ops_txn_commit(MDB_txn* const txn, int* const out_err);

#endif /* DB_OPERATIONS_OPS_EXEC_H_ */
