/**
 * @file ops_exec.h
 * 
 */

#ifndef DB_OPERATIONS_EXEC_H
#define DB_OPERATIONS_EXEC_H

#include "ops_internals.h" /* op_t etc */

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * PUBLIC STRUCTURED TYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTION PROTOTYPES
****************************************************************************
*/

int ops_add_operation(const op_t* operation);

int ops_execute_operations(void);


static db_security_ret_code_t _exec_ops(MDB_txn* txn, int* const out_err);

#ifdef __cplusplus
}
#endif

#endif /* DB_OPERATIONS_EXEC_H */
