/**
 * @file db_operations.c
 * 
 */

#include <string.h> /* memset, memcpy */

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
    /* Cache needed for RW get operations:
    when get, obtain a ptr which after a read is not valid anymore.
    Thus a necessity to store the get results in a cache arises. */
    char rw_cache[DB_LMDB_RW_OPS_CACHE_SIZE];
    /* Number of bytes currently used in rw_cache. */
    size_t rw_cache_used;
} batch_t;

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */

batch_t ops_cache;

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

static db_security_ret_code_t _exec_op(MDB_txn* txn, op_t* op, int* const out_err);
static void*                  _rw_cache_alloc(size_t size);

static inline batch_kind_t _batch_type_from_op_type(const op_type_t* const type)
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

static inline unsigned int _txn_type_from_batch_type(void)
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
       _batch_type_from_op_type(&operation->type) == OPS_BATCH_KIND_RW)
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

int _exec_rw_ops(void)
{
    /* Init retry count and result variable */
    int retry_count = 0;
    int res         = -1;
    /* Init transaction */
    MDB_txn* txn    = NULL;

retry:
{
    /* Check retry */
    if(retry_count++ >= DB_LMDB_RETRY_OPS_EXEC)
    {
        EML_ERROR(LOG_TAG, "_exec_ops: exceeded max retry count %d", retry_count);
        res = -EIO;
        goto fail;
    }

    /* Begin transaction with no flags */
    switch(act_txn_begin(&txn, _txn_type_from_batch_type(), &res))
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
            /* TODO:
            If get op, and in rw, need to save the result!!! */
            /* A few cases here:
            1) get operation, ptr and buf given by the user,
            write immediately to user's */
            break;
        case DB_SAFETY_RETRY:
            goto retry;
        default:
            EML_ERROR(LOG_TAG, "_exec_ops failed, err=%d", res);
            goto fail;
    }

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

    /* proceed */
    res = 0;
    EML_DBG(LOG_TAG, "_exec_ro_ops: RO txn completed, aborted");

}  // retry
fail:
    /* wipe the cache */
    memset(&ops_cache, 0, sizeof(batch_t));
    return res;
}

int _exec_ro_ops(void)
{
    /* Init retry count and result variable */
    int retry_count = 0;
    int res         = -1;
    /* Init transaction */
    MDB_txn* txn    = NULL;
retry:
{
    /* Check retry and increase */
    if(retry_count++ >= DB_LMDB_RETRY_OPS_EXEC)
    {
        EML_ERROR(LOG_TAG, "_exec_ro_ops: exceeded max retry count %d", retry_count);
        res = -EIO;
        goto fail;
    }

    /* Begin transaction with RO flags */
    switch(act_txn_begin(&txn, _txn_type_from_batch_type(), &res))
    {
        case DB_SAFETY_SUCCESS:
            break;
        case DB_SAFETY_RETRY:
            goto retry;
        default:
            EML_ERROR(LOG_TAG, "_exec_ro_ops: _txn_begin failed, err=%d", res);
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
            EML_ERROR(LOG_TAG, "_exec_ro_ops failed, err=%d", res);
            goto fail;
    }

    /*  Abort txn and proceed */
    mdb_txn_abort(txn);
    res = 0;
    EML_DBG(LOG_TAG, "_exec_ro_ops: RO txn completed, aborted");

}  // retry
fail:
    /* wipe the cache */
    memset(&ops_cache, 0, sizeof(batch_t));
    return res;
}

int ops_execute_operations(void)
{
    if(ops_cache.n_ops == 0)
    {
        EML_ERROR(LOG_TAG, "ops_execute_operations: no ops in cache to execute");
        return -EINVAL;
    }

    /* Init result variable */
    int res = -1;
    switch(ops_cache.kind)
    {
        /* RO ops */
        case OPS_BATCH_KIND_RO:
            res = _exec_ro_ops();
            break;
        /* RW ops */
        default:
            res = _exec_rw_ops();
            break;
    }

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
                /* Here, in this case, if the op_batch is RW, I should
                take the "get" result and copy it inside the buff */
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

/**
 * @brief Allocate a slice from the RW cache.
 *
 * The function advances the internal offset and returns a pointer into
 * ops_cache.rw_cache, or NULL when there is not enough remaining space.
 */
static void* _rw_cache_alloc(size_t size)
{
    /* Zero-sized allocations are treated as no-op. */
    if(size == 0)
    {
        return NULL;
    }

    /* Ensure we do not overflow the fixed cache buffer. */
    if(size > (DB_LMDB_RW_OPS_CACHE_SIZE - ops_cache.rw_cache_used))
    {
        EML_ERROR(LOG_TAG,
                  "_rw_cache_alloc: insufficient space (requested=%zu used=%zu capacity=%zu)", size,
                  ops_cache.rw_cache_used, (size_t)DB_LMDB_RW_OPS_CACHE_SIZE);
        return NULL;
    }

    char* dst                = ops_cache.rw_cache + ops_cache.rw_cache_used;
    ops_cache.rw_cache_used += size;
    return dst;
}

static db_security_ret_code_t _exec_op(MDB_txn* txn, op_t* op, int* const out_err)
{
    db_security_ret_code_t ret = DB_SAFETY_FAIL;

    switch((unsigned int)op->type)
    {
        case DB_OPERATION_PUT:
            return act_put(txn, op, out_err);

        case DB_OPERATION_GET:
            ret = act_get(txn, op, out_err);
            /* If GET failed, propagate the safety decision. */
            if(ret != DB_SAFETY_SUCCESS) return ret;

            /* In case of RW operation, after a GET, save the result into the cache buffer. */
            if(ops_cache.kind == OPS_BATCH_KIND_RW)
            {
                /* At this point act_get() guarantees PRESENT with a valid pointer/size. */
                if(!op->val.present.ptr || op->val.present.size == 0)
                {
                    EML_ERROR(LOG_TAG,
                              "_exec_op: GET returned invalid value descriptor (ptr=%p size=%zu)",
                              op->val.present.ptr, op->val.present.size);
                    if(out_err) *out_err = -EIO;
                    return DB_SAFETY_FAIL;
                }

                void* dst = _rw_cache_alloc(op->val.present.size);
                if(!dst)
                {
                    /* Cache is too small for this value. */
                    if(out_err) *out_err = -ENOMEM;
                    return DB_SAFETY_FAIL;
                }

                /* Copy the obtained value into the RW cache and repoint op->val to it. */
                memcpy(dst, op->val.present.ptr, op->val.present.size);
                op->val.present.ptr = dst;
            }

            /* Value is now stable (either in user buffer or internal cache). */
            return DB_SAFETY_SUCCESS;

            // case DB_OPERATION_REP:
            //     return _op_rep(txn, op);

            // case DB_OPERATION_DEL:
            //     return _op_del(txn, op);

        default:
            EML_ERROR(LOG_TAG, "_exec_op: invalid op type=%d", op->type);
            return DB_SAFETY_FAIL;
    }
}
