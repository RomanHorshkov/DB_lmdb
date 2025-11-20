/**
 * @file ops_facade.h
 * @brief Facade layer for database operations.
 */

#ifndef DB_OPERATIONS_OPS_FACADE_H_
#define DB_OPERATIONS_OPS_FACADE_H_

#include "dbi_ext.h" /* dbi_type_t */


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
/* None */

/**
 * @brief Operation kind.
 */
typedef enum
{
    DB_OPERATION_NONE = 0, /**< Uninitialized placeholder. */
    DB_OPERATION_PUT,      /**< Insert/replace value; honors MDB flags. */
    DB_OPERATION_GET,      /**< Lookup by key; fills op->dst/op->dst_len. */
    // DB_OPERATION_REP,      /**< In-place patch of existing value (cursor + RESERVE). */
    // DB_OPERATION_LST,      /**< Reserved for future list/scan helpers. */
    DB_OPERATION_DEL,      /**< Delete by key or (key, dup-value). */
    DB_OPERATION_MAX
} op_type_t;



/****************************************************************************
 * PUBLIC FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

#ifdef __cplusplus
}
#endif

#endif /* DB_OPERATIONS_OPS_FACADE_H_ */
