/**
 * @file ops_internals.h
 */

#ifndef DB_OPERATIONS_OPS_INTERNALS_H_
#define DB_OPERATIONS_OPS_INTERNALS_H_

#include "db.h"       /* MDB_txn etc */
#include "ops_externals.h"
#include "ops_facade.h"
#include "security.h" /* db_security_ret_code_t */

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

typedef struct
{
    unsigned int dbi;  /**< Target DBI handle. */
    op_type_t    type; /**< Operation type. */
    op_key_t     key;  /**< Key descriptor. */
    op_key_t     val;  /**< Value descriptor. */
} op_t;

/****************************************************************************
 * PRIVATE FUNCTION PROTOTYPES
 ****************************************************************************
 */

#ifdef __cplusplus
}
#endif
#endif /* DB_OPERATIONS_OPS_INTERNALS_H_ */
