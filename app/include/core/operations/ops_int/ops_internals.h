/**
 * @file ops_internals.h
 */

#ifndef DB_OPERATIONS_OPS_INTERNALS_H_
#define DB_OPERATIONS_OPS_INTERNALS_H_

#include <stddef.h>   /* size_t */
#include "db.h"       /* MDB_txn etc */
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

typedef struct op_t
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
        op_val_t        present; /**< Present key info. */
        op_val_lookup_t lookup;  /**< Lookup key info. */
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
 * PRIVATE FUNCTION PROTOTYPES
 ****************************************************************************
 */

#ifdef __cplusplus
}
#endif
#endif /* DB_OPERATIONS_OPS_INTERNALS_H_ */
