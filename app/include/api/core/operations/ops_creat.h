/**
 * @file ops_exec.h
 */

#ifndef DB_OPERATIONS_OPS_EXEC_H_
#define DB_OPERATIONS_OPS_EXEC_H_

#include "db.h"

/****************************************************************************
 * PUBLIC FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

db_security_ret_code_t ops_txn_begin(MDB_txn** out_txn, const unsigned flags, int* const out_err);

db_security_ret_code_t ops_txn_commit(MDB_txn* const txn, int* const out_err);

#endif /* DB_OPERATIONS_OPS_EXEC_H_ */
