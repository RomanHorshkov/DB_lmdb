/**
 * @file db_operations.c
 * 
 */

#include <string.h> /* memset */

#include "common.h" /* EML_* macros, LMDB_EML_* */
#include "ops_actions.h"
#include "ops_exec.h"

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
    OPS_BATCH_KIND_RO = 0, /**< Read operation (GET). */
    OPS_BATCH_KIND_RW = 1  /**< Write operation (PUT/DEL). */
} batch_kind_t;

typedef struct
{
    batch_kind_t kind;                /**< Operations batch kind. */
    op_t         ops[OPS_CACHE_SIZE]; /**< Cached operations. */
    size_t       n_ops;               /**< Number of cached operations. */
} batch_t;

batch_t ops_cache;

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

static db_security_ret_code_t _exec_op(MDB_txn* txn, op_t* op, int* const out_err);

static inline batch_kind_t _op_kind_from_type(const op_type_t* const type)
{
    switch(*type)
    {
        case DB_OPERATION_GET:
            // case DB_OPERATION_LST:
            return OPS_BATCH_KIND_RO;
        default:
            return OPS_BATCH_KIND_RW; /* safe default */
    }
}

static inline unsigned int _op_kind_from_op(void)
{
    switch(ops_cache.kind)
    {
        case OPS_BATCH_KIND_RO:
            return MDB_RDONLY;

        default:
            return 0;
    }
}

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
int ops_add_operation(const op_t* operation)
{
    /* Input check */
    if(!operation)
    {
        EML_ERROR(LOG_TAG, "_add_op: invalid input");
        return -EINVAL;
    }

    /* Check if write op */
    if(ops_cache.kind == OPS_BATCH_KIND_RO &&
       _op_kind_from_type(&operation->type) == OPS_BATCH_KIND_RW)
    {
        /* Set whole ops batch to write */
        ops_cache.kind = OPS_BATCH_KIND_RW;
    }

    /**
     * @note:
     * before caching the operation, if key is lookup, i.e.
     * it is required from prev op, check it does not exceed
     * current ops count (forward and backwards).
     * With this check later can safely access the prev op during exec.
     */
    if(operation->key.kind == OP_KEY_KIND_LOOKUP &&
       operation->key.lookup.op_index > ops_cache.n_ops)
    {
        EML_ERROR(LOG_TAG, "_add_op: invalid key lookup index %u (n_ops=%zu)",
                  operation->key.lookup.op_index, ops_cache.n_ops);
        return -EINVAL;
    }

    /* Add operation to cache */
    if(ops_cache.n_ops >= OPS_CACHE_SIZE)
    {
        EML_ERROR(LOG_TAG, "_add_op: ops cache full, exceeded %d ops", OPS_CACHE_SIZE);
        return -ENOMEM;
    }

    /* shallow struct copy */
    ops_cache.ops[ops_cache.n_ops++] = *operation;

    EML_DBG(LOG_TAG, "_add_op: queued op #%zu (dbi=%u type=%d key_kind=%d val_kind=%d)",
            ops_cache.n_ops - 1, operation->dbi, (int)operation->type, operation->key.kind,
            operation->val.kind);

    return 0;
}

int ops_execute_operations(void)
{
    if(!ops_cache.ops || ops_cache.n_ops == 0)
    {
        EML_ERROR(LOG_TAG, "_exec_ops: invalid input");
        return -EINVAL;
    }

    /* Init retry count and result variable */
    int retry_count = 0;
    int res         = -1;

    /* Determine transaction flags */
    unsigned int txn_open_flags = _op_kind_from_op();

    EML_DBG(LOG_TAG, "_exec_ops: starting batch of %zu ops (kind=%d)", ops_cache.n_ops,
            (int)ops_cache.kind);

    /* Init transaction */
    MDB_txn* txn = NULL;
retry:
{
    /* Check retry */
    if(retry_count >= DB_LMDB_RETRY_OPS_EXEC)
    {
        EML_ERROR(LOG_TAG, "_exec_ops: exceeded max retry count %d", retry_count);
        res = -EIO;
        goto fail;
    }

    /* Increase retry count */
    retry_count++;
    /* Begin transaction with correct flags */
    switch(act_txn_begin(&txn, txn_open_flags, &res))
    {
        case DB_SAFETY_SUCCESS:
            break;
        case DB_SAFETY_RETRY:
            goto retry;
        default:
            EML_ERROR(LOG_TAG, "_exec_ops: _txn_begin failed, err=%d", res);
            goto fail;
    }

    /* Execute all cached operations */
    switch(_exec_ops(txn, &res))
    {
        case DB_SAFETY_SUCCESS:
            break;
        case DB_SAFETY_RETRY:
            goto retry;
        default:
            EML_ERROR(LOG_TAG, "_exec_ops failed, err=%d", res);
            goto fail;
    }

    /* If RO no commit, if RW commit */
    switch(txn_open_flags)
    {
        /* ROnly, no commit just abort, fast path */
        case MDB_RDONLY:
            mdb_txn_abort(txn);
            res = 0;
            EML_DBG(LOG_TAG, "_exec_op: RO txn completed, aborted");
            goto done;

        default:
            /* Commit transaction */
            switch(act_txn_commit(txn, &res))
            {
                case DB_SAFETY_SUCCESS:
                    break;
                case DB_SAFETY_RETRY:
                    goto retry;
                default:
                    EML_ERROR(LOG_TAG, "_exec_op: _txn_commit failed, err=%d", res);
                    goto fail;
            }
            EML_DBG(LOG_TAG, "_exec_op: RW txn committed");
            break;
    }

}  // retry

done:
    res = 0;

fail:
    /* wipe the cache */
    memset(&ops_cache, 0, sizeof(batch_t));
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
                EML_WARN(LOG_TAG, "_exec_op: retry at %u", i);
                return DB_SAFETY_RETRY;
            default:
                EML_ERROR(LOG_TAG, "_exec_op: op %u failed", i);
                return DB_SAFETY_FAIL;
        }
        EML_DBG(LOG_TAG, "_exec_ops: op %u executed successfully", i);
    }

    return DB_SAFETY_SUCCESS;
}

static db_security_ret_code_t _exec_op(MDB_txn* txn, op_t* op, int* const out_err)
{
    switch((unsigned int)op->type)
    {
        case DB_OPERATION_PUT:
            return act_put(txn, op, out_err);

        case DB_OPERATION_GET:
            return act_get(txn, op, out_err);

            // case DB_OPERATION_REP:
            //     return _op_rep(txn, op);

            // case DB_OPERATION_DEL:
            //     return _op_del(txn, op);

        default:
            EML_ERROR(LOG_TAG, "_exec_op: invalid op type=%d", op->type);
            return DB_SAFETY_FAIL;
    }
}
