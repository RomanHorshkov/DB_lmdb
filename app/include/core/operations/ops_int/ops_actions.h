/**
 * @file ops_actions.h
 */

#ifndef DB_OPERATIONS_OPS_ACTIONS_H_
#define DB_OPERATIONS_OPS_ACTIONS_H_

// #include "ops_setup.h" /* ops_init_dbi etc */

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * PUBLIC FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

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
db_security_ret_code_t op_get(MDB_txn* txn, op_t* op, int* const out_err);

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
db_security_ret_code_t op_put(MDB_txn* txn, op_t* op, int* const out_err);

#ifdef __cplusplus
}
#endif

#endif /* DB_OPERATIONS_OPS_ACTIONS_H_ */
