/**
 * @file ops_externals.h
 * 
 * 
 */

#ifndef DB_OPERATIONS_OPS_EXTERNALS_H_
#define DB_OPERATIONS_OPS_EXTERNALS_H_

#include <stddef.h> /* size_t */

/****************************************************************************
 * PUBLIC STRUCTURED TYPES
 ****************************************************************************
 */

/**
 * @brief Key kind for an operation.
 */
typedef enum
{
    OP_KEY_KIND_NONE    = 0, /**< uninitialized */
    OP_KEY_KIND_PRESENT = 1, /**< key is provided */
    OP_KEY_KIND_LOOKUP  = 2  /**< key is to be looked up from prev op */
} op_key_kind_t;

/**
 * @brief Source of the key for a referenced operation.
 */
typedef enum
{
    OP_KEY_SRC_KEY = 0, /**< key is from some prev operation's key */
    OP_KEY_SRC_VAL = 1  /**< key is from some prev operation's value */
} op_key_source_t;

/**
 * @brief Lookup descriptor for an operation's key or value.
 */
typedef struct op_t
{
    op_key_source_t src_type; /**< Source type (key or value). */
    unsigned int    op_index; /**< Index of the operation to source key from. */
} op_key_lookup_t;

/**
 * @brief Value descriptor for an operation.
 */
typedef struct
{
    size_t size; /**< Size of value bytes. */
    void*  data; /**< Pointer to value bytes. */
} op_key_present_t;

/**
 * @brief Key descriptor for an operation.
 */
typedef struct
{
    op_key_kind_t kind; /**< Key kind (present vs lookup). */

    union
    {
        op_key_present_t present; /**< Present key info. */
        op_key_lookup_t  lookup;  /**< Lookup key info. */
    };

} op_key_t;

#endif /* DB_OPERATIONS_OPS_EXTERNALS_H_ */
