/**
 * @file db_operations.c
 * @brief 
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025
 * (c) 2025
 */

#include "ops_exec.h"    /* op_t etc */
#include "ops_actions.h" /* ops_init_dbi etc */
// #include "ops_setup.h"  /* dbi_init_t */

#include "void_store.h" /* void_store_t etc */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG        "ops_exec"
#define OPS_CACHE_SIZE 8

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */
typedef enum
{
    OP_KIND_READ  = 0, /**< Read operation (GET). */
    OP_KIND_WRITE = 1  /**< Write operation (PUT/REP/DEL). */
} op_kind_t;

typedef struct
{
    op_kind_t    kind;                /**< Operation kind. */
    op_t         ops[OPS_CACHE_SIZE]; /**< Cached operations. */
    unsigned int n_ops;               /**< Number of cached operations. */
} op_batch_t;

op_batch_t ops_cache;

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

static db_security_ret_code_t _exec_op(MDB_txn* txn, op_t* op);

static inline op_kind_t _op_kind_from_type(const op_type_t* const type)
{
    switch(*type)
    {
        case DB_OPERATION_GET:
        case DB_OPERATION_LST:
            return OP_KIND_READ;
        // case DB_OPERATION_PUT:
        // case DB_OPERATION_REP:
        // case DB_OPERATION_DEL:
        default:
            return OP_KIND_WRITE; /* safe default */
    }
}

int ops_add_operation(const unsigned int dbi_idx, const op_type_t op_type, const op_key_t key,
                      const op_key_t val)
{
    /* Input check */

    /* Check if write op */
    if(ops_cache.kind != OP_KIND_WRITE && _op_kind_from_type(op_type) == OP_KIND_WRITE)
    {
        /* Set whole ops batch to write */
        ops_cache.kind = OP_KIND_WRITE;
    }

    /**
     * @note:
     * before caching the operation, if key is lookup, i.e.
     * it is required from prev op, check it does not exceed
     * current ops count (forward and backwards).
     * With this check later can safely access the prev op during exec.
     */
    if(key.kind == OP_KEY_KIND_LOOKUP && key.lookup.op_index > ops_cache.n_ops)
    {
        EML_ERROR(LOG_TAG, "ops_add_operation: invalid key lookup index %u (n_ops=%u)",
                  key.lookup.op_index, ops_cache.n_ops);
        return -EINVAL;
    }

    /* Add operation to cache */
    if(ops_cache.n_ops >= OPS_CACHE_SIZE)
    {
        EML_ERROR(LOG_TAG, "ops_add_operation: ops cache full, exceeded %d ops", OPS_CACHE_SIZE);
        return -ENOMEM;
    }

    op_t* op = &ops_cache.ops[ops_cache.n_ops++];
    op->dbi  = dbi_idx;
    op->type = op_type;
    op->key  = key;
    op->val  = val;

    return 0;
}

int ops_execute_operations(/* TODO params */)
{
    if(!ops_cache.ops || !ops_cache.n_ops == 0)
    {
        EML_ERROR(LOG_TAG, "ops_exec: invalid input");
        return -EINVAL;
    }

    /* Init retry count and result variable */
    int retry_count = 0;
    int res         = -1;

    /* Determine transaction flags */
    unsigned int txn_open_flags = 0;
    if(ops_cache.kind == OP_KIND_READ)
    {
        txn_open_flags = MDB_RDONLY;
    }

    /* Init transaction */
    MDB_txn* txn = NULL;
retry:
{
    /* Check retry */
    if(retry_count >= DB_LMDB_RETRY_OPS_EXEC)
    {
        EML_ERROR(LOG_TAG, "ops_exec: exceeded max retry count %d", retry_count);
        res = -EIO;
        goto fail;
    }

    /* Increase retry count */
    retry_count++;

    /* Begin transaction */
    switch(ops_txn_begin(&txn, txn_open_flags, &res))
    {
        case DB_SAFETY_SUCCESS:
            break;
        case DB_SAFETY_RETRY:
            goto retry;
        default:
            goto fail;
    }

    /* Execute all cached operations */
    switch(_exec_ops(txn))
    {
        case DB_SAFETY_SUCCESS:
            break;
        case DB_SAFETY_RETRY:
            goto retry;
        default:
            goto fail;
    }

    /* Commit transaction */
    switch(ops_txn_commit(txn, &res))
    {
        case DB_SAFETY_SUCCESS:
            break;
        case DB_SAFETY_RETRY:
            goto retry;
        default:
            goto fail;
    }
}  // retry

    res = 0;

fail:
    /* wipe the cache */
    EML_ERROR(LOG_TAG, "ops_exec: txn_commit failed, err=%d", res);
    memset(ops_cache, 0, sizeof(op_batch_t));
    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
static db_security_ret_code_t _exec_ops(MDB_txn* txn, int* const out_err)
{

    /* Execute all cached operations */
    for(unsigned int i = 0; i < ops_cache.n_ops; i++)
    {
        switch(_exec_op(txn, &ops_cache.ops[i], out_err))
        {
            case DB_SAFETY_SUCCESS:
                break;
            case DB_SAFETY_RETRY:
                EML_WARN(LOG_TAG, "_exec_ops: retry at %u", i);
                return DB_SAFETY_RETRY;
            default:
                EML_ERROR(LOG_TAG, "_exec_ops: op %u failed", i);
                return DB_SAFETY_FAIL;
        }
    }

    return DB_SAFETY_SUCCESS;
}

static db_security_ret_code_t _exec_op(MDB_txn* txn, op_t* op, int* const out_err)
{
    switch((unsigned int)op->type)
    {
        case DB_OPERATION_PUT:
            return op_put(txn, op, out_err);

        case DB_OPERATION_GET:
            return op_get(txn, op, out_err);

        // case DB_OPERATION_REP:
        //     return _op_rep(txn, op);

        // case DB_OPERATION_DEL:
        //     return _op_del(txn, op);

        default:
            EML_ERROR(LOG_TAG, "_exec_op: invalid op type=%d", op->type);
            return DB_SAFETY_FAIL;
    }
}
