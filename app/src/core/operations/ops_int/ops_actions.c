
#include "ops_actions.h"
#include <stddef.h> /* NULL */
#include <string.h> /* memset, memcpy */
#include "common.h" /* EML_* macros, LMDB_EML_* */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "ops_act"

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Resolve a key/value descriptor to an MDB_val pointer.
 *
 * This helper walks an @ref op_key_t descriptor and, when necessary,
 * follows backward references to earlier operations in the batch.
 * It supports:
 *  - OP_KEY_KIND_PRESENT: bytes are embedded in @p desc->present.
 *  - OP_KEY_KIND_LOOKUP + OP_KEY_SRC_KEY: data comes from a previous
 *    operation's key (recursively resolved, so chains are allowed).
 *  - OP_KEY_KIND_LOOKUP + OP_KEY_SRC_VAL: data comes from a previous
 *    operation's value (also recursively resolved).
 *
 * On any contract violation (invalid kind/src_type, empty PRESENT),
 * the function logs an error and returns NULL.
 *
 * @param base Operation whose descriptor is being resolved. It must
 *             belong to a contiguous array of ops; LOOKUP indices are
 *             interpreted as “how many positions back from base”.
 * @param desc Descriptor to resolve (key or value).
 */
static MDB_val* _resolve_desc(op_t* base, op_key_t* desc);

/**
 * @brief Resolve the key for an operation.
 *
 * Thin wrapper over _resolve_desc that binds the descriptor to op->key.
 */
static inline MDB_val* _get_key(op_t* op)
{
    return _resolve_desc(op, &op->key);
}

/**
 * @brief Resolve the value for an operation.
 *
 * Thin wrapper over _resolve_desc that binds the descriptor to op->val.
 */
static inline MDB_val* _get_val(op_t* op)
{
    return _resolve_desc(op, &op->val);
}
/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

db_security_ret_code_t act_txn_begin(MDB_txn** out_txn, const unsigned flags, int* const out_err)
{
    /* Check input */
    if(!DataBase || !DataBase->env || !out_txn)
    {
        EML_ERROR(LOG_TAG, "_txn_begin: invalid input");
        return DB_SAFETY_FAIL;
    }

    /* loosing const correctness */
    int mdb_res = mdb_txn_begin(DataBase->env, NULL, flags, out_txn);

    /* keep this light check to avoid jumping into 
    security_check on hot path */
    if(mdb_res != 0)
    {
        EML_ERROR(LOG_TAG, "_txn_begin: mdb_txn_begin failed, mdb_rc=%d", mdb_res);
        return security_check(mdb_res, NULL, out_err);
    }
    return DB_SAFETY_SUCCESS;
}

db_security_ret_code_t act_txn_commit(MDB_txn* const txn, int* const out_err)
{
    /* Check input */
    if(!txn)
    {
        EML_ERROR(LOG_TAG, "_txn_commit: invalid input (txn=NULL)");
        return DB_SAFETY_FAIL;
    }

    int mdb_res = mdb_txn_commit(txn);
    /* keep this light check to avoid jumping into 
    security_check on hot path */
    if(mdb_res != 0)
    {
        EML_ERROR(LOG_TAG, "_txn_commit: mdb_txn_commit failed, mdb_rc=%d", mdb_res);
        return security_check(mdb_res, txn, out_err);
    }
    EML_DBG(LOG_TAG, "act_txn_commit: txn committed");
    return DB_SAFETY_SUCCESS;
}

db_security_ret_code_t act_put(MDB_txn* txn, op_t* op, int* const out_err)
{
    /* Check input */
    if(!txn || !op)
    {
        EML_ERROR(LOG_TAG, "_op_get: invalid input");
        return DB_SAFETY_FAIL;
    }

    /* Get key pointer */
    MDB_val* k_ptr = _get_key(op);
    if(!k_ptr)
    {
        EML_ERROR(LOG_TAG, "_op_get: failed to retrieve key");
        return DB_SAFETY_FAIL;
    }

    /* Get val pointer */
    MDB_val* v_ptr = _get_val(op);
    if(!v_ptr)
    {
        EML_ERROR(LOG_TAG, "_op_get: failed to retrieve val");
        return DB_SAFETY_FAIL;
    }

    dbi_t* dbi = &DataBase->dbis[op->dbi];

    /* Put val */
    /* DO NOT add MDB_RESERVE here */
    /* Use the put flags in the Database */
    int mdb_res = mdb_put(txn, dbi->dbi, k_ptr, v_ptr, dbi->put_flags);
    if(mdb_res != MDB_SUCCESS) return security_check(mdb_res, txn, out_err);
    return DB_SAFETY_SUCCESS;
}

db_security_ret_code_t act_get(MDB_txn* txn, op_t* op, int* const out_err)
{
    /* Check input */
    if(!txn || !op)
    {
        EML_ERROR(LOG_TAG, "_op_get: invalid input");
        return DB_SAFETY_FAIL;
    }

    /* Get key pointer */
    MDB_val* k_ptr = _get_key(op);
    if(!k_ptr)
    {
        EML_ERROR(LOG_TAG, "_op_get: failed to retrieve key");
        return DB_SAFETY_FAIL;
    }

    /* Get immediately the result, then decide what to do with it. */
    MDB_val tmp_val;
    int     mdb_res = mdb_get(txn, op->dbi, k_ptr, &tmp_val);
    if(mdb_res != 0) goto fail;

    /* use op->val.present which is layout-compatible with MDB_val.
    If the user gave an input value then use another to get the result and copy it */
    if(op->val.kind == OP_KEY_KIND_PRESENT)
    {
        /* check size */
        if(tmp_val.mv_size > op->val.present.size)
        {
            EML_ERROR(LOG_TAG, "_op_get: user buffer too small (buf_size=%zu needed=%zu)",
                      op->val.present.size, tmp_val.mv_size);
            return DB_SAFETY_FAIL;
        }

        /* copy to user buffer */
        memcpy(op->val.present.ptr, tmp_val.mv_data, tmp_val.mv_size);
        /* set actual size */
        op->val.present.size = tmp_val.mv_size;
    }

    /* If the user gave no buffer, no need to memcpy for return, 
    just store the reference to result in the key. */
    else
    {
        op->val.kind    = OP_KEY_KIND_PRESENT;
        /* shallow copy tmp in op's val */
        op->val.present = *((op_val_t*)&tmp_val);
    }

    return DB_SAFETY_SUCCESS;

fail:
    return security_check(mdb_res, txn, out_err);
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static MDB_val* _resolve_desc(op_t* base, op_key_t* desc)
{
    if(!base || !desc)
    {
        EML_ERROR(LOG_TAG, "_resolve_desc: base=%p desc=%p", (void*)base, (void*)desc);
        return NULL;
    }

    MDB_val* k_ptr = NULL;

    switch(desc->kind)
    {
        case OP_KEY_KIND_NONE:
            return NULL;
        /* bytes are present in this descriptor */
        case OP_KEY_KIND_PRESENT:
            /**
             * Assume that desc->present is already populated, cast it to
             * MDB_val* and use it directly. This is safe because op_val_t
             * is layout-compatible with MDB_val.
             */
            if(!desc->present.ptr || desc->present.size == 0)
            {
                EML_ERROR(LOG_TAG, "_resolve_desc: PRESENT has invalid buffer (ptr=%p size=%zu)",
                          desc->present.ptr, desc->present.size);
                return NULL;
            }
            k_ptr = (MDB_val*)&desc->present;
            break;

        /* data is in some previous operation */
        case OP_KEY_KIND_LOOKUP:
        {
            /* Get the referenced operation (ops_add_operation validated index) */
            op_t* lookup_op = base - desc->lookup.op_index;

            /* Choose source from the referenced op */
            switch(desc->lookup.src_type)
            {
                case OP_KEY_SRC_KEY:
                    /**
                     * KEY-from-KEY (or VAL-from-KEY) lookup: resolve the
                     * referenced op's key recursively so chains are supported.
                     */
                    k_ptr = _resolve_desc(lookup_op, &lookup_op->key);
                    break;

                case OP_KEY_SRC_VAL:
                    /**
                     * KEY/VAL-from-VAL lookup: resolve the referenced
                     * op's value recursively.
                     */
                    k_ptr = _resolve_desc(lookup_op, &lookup_op->val);
                    break;

                default:
                    EML_ERROR(LOG_TAG, "_resolve_desc: invalid source type=%d",
                              desc->lookup.src_type);
                    return NULL;
            } /* switch(src_type) */
            break;
        }

        default:
            EML_ERROR(LOG_TAG, "_resolve_desc: invalid kind=%d", desc->kind);
            return NULL;
    } /* switch(kind) */

    return k_ptr;
}
