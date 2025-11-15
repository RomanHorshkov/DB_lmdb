/**
 * @file db_operations.c
 * @brief 
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025
 * (c) 2025
 */

#include "db_lmdb_ops.h" /* db_internal.h -> lmdb.h, stdio, stdlib, string, time */

#include "void_store.h"

#include "emlog.h" /* EML_ERR etc */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

/* Log LMDB ret with a stable format */
#define LMDB_LOG_ERR(where, ret) \
    EML_ERROR("db_ops", "%s: ret=%d (%s)", where, (ret), mdb_strerror(ret))

#define LMDB_EML_WARN(where, ret) \
    EML_WARN("db_ops", "%s: ret=%d (%s)", where, (ret), mdb_strerror(ret))

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
 * @brief Query whether an LMDB database supports duplicate keys.
 *
 * Thin wrapper over mdb_dbi_flags() to expose the MDB_DUPSORT property.
 *
 * @param[in]  txn            Active transaction.
 * @param[in]  dbi            Database handle.
 * @param[out] out_is_dupsort 1 if DBI has MDB_DUPSORT, else 0.
 * @return MDB_SUCCESS on success, otherwise LMDB error code.
 */
static int _dbi_is_dupsort(MDB_txn* txn, MDB_dbi dbi, int* out_is_dupsort);

/**
 * @brief Initialize a key store from a single memory segment.
 *
 * Creates a one-segment @ref void_store and appends @p key_seg
 * of length @p seg_size. If @p key_seg is NULL or @p seg_size is 0,
 * this function returns -EINVAL (callers may instead rely on prev->dst).
 *
 * @param[out] st        Store to allocate and assign.
 * @param[in]  key_seg   Pointer to key bytes (may alias caller memory).
 * @param[in]  seg_size  Key size in bytes (must be > 0).
 * @return 0 on success, -EINVAL on bad input, -ENOMEM on allocation failure.
 */
static int _prepare_key(void_store_t** st, void* key_seg, size_t seg_size);

/**
 * @brief Initialize a DB operation descriptor with common fields.
 *
 * Resets stores/links and assigns type, dbi, and flags. No allocation here.
 *
 * @param[out] op    Operation descriptor.
 * @param[in]  type  Operation kind.
 * @param[in]  dbi   Target database handle.
 * @param[in]  flags LMDB flags for PUT/DEL (e.g., MDB_NOOVERWRITE).
 */
static void _prepare_op(DB_operation_t* op, DB_operation_type_t type, MDB_dbi dbi, unsigned flags);

/**
 * @brief Wire a contiguous array of operations into a doubly-linked chain.
 *
 * @param[in,out] ops   Array base.
 * @param[in]     n_ops Element count.
 */
static void _ops_link(DB_operation_t* ops, size_t n_ops);

/**
 * @brief Execute a single operation within an existing transaction.
 *
 * Dispatches to _op_put/_op_get/_op_rep/_op_del based on @p op->type.
 *
 * @param[in,out] txn Active write transaction.
 * @param[in,out] op  Operation to execute (may fill op->dst on GET).
 * @return MDB_SUCCESS on success, otherwise LMDB error code.
 */
static int _exec_op(MDB_txn* txn, DB_operation_t* op);

/**
 * @brief LMDB PUT with MDB_RESERVE + contiguous value assembly.
 *
 * Key is taken from @p op->key_store. Value is assembled from
 * @p op->val_store into the reserved page buffer.
 *
 * @param[in,out] txn Active write transaction.
 * @param[in]     op  Prepared PUT operation.
 * @return MDB_SUCCESS on success, otherwise LMDB error code.
 *
 * @warning Expects op->key_store and op->val_store to be non-empty.
 */
static int _op_put(MDB_txn* txn, DB_operation_t* op);

/**
 * @brief LMDB GET.
 *
 * If @p op->key_store is set, uses it; otherwise uses @p op->prev->dst
 * as the key. On success allocates and fills @p op->dst/@p op->dst_len.
 *
 * @param[in]  txn Active transaction (read or write).
 * @param[in]  op  Prepared GET operation.
 * @return MDB_SUCCESS, MDB_NOTFOUND, or LMDB error code.
 */
static int _op_get(MDB_txn* txn, DB_operation_t* op);

/**
 * @brief In-place partial replace (patch) of a value using a cursor.
 *
 * Modes:
 *  - If op->key_store present → key from store.
 *  - Else → key from op->prev->dst.
 *
 * The patch payload is described by op->val_store segments and applied
 * over the reserved buffer returned by mdb_cursor_put(..., MDB_RESERVE).
 *
 * @param[in,out] txn Active write transaction.
 * @param[in]     op  Prepared REP operation.
 * @return MDB_SUCCESS on success; LMDB error; or -EINVAL/-ENOMEM on client bugs.
 *
 * @note This does not change the value length; it overwrites in place.
 */
static int _op_rep(MDB_txn* txn, DB_operation_t* op);

/**
 * @brief Delete a key (and optionally a specific duplicate) from a DBI.
 *
 * If DB is DUPSORT:
 *  - With val_store → deletes the exact (key, value) dup.
 *  - Without       → deletes all duplicates for key.
 * Otherwise deletes the single (key) entry.
 *
 * Key is taken from key_store or prev->dst (like GET).
 *
 * @param[in,out] txn Active write transaction.
 * @param[in]     op  Prepared DEL operation (optional value for dup-exact).
 * @return MDB_SUCCESS, MDB_NOTFOUND (idempotent), or LMDB error code.
 */
static int _op_del(MDB_txn* txn, DB_operation_t* op);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
DB_operation_t* ops_create(size_t n_ops)
{
    return (DB_operation_t*)calloc(n_ops, sizeof(DB_operation_t));
}

int ops_put_one(MDB_dbi dbi, const void* key, size_t klen, const void* val, size_t vlen,
                unsigned flags) /* e.g. MDB_NOOVERWRITE | MDB_NODUPDATA */
{
    if(dbi == 0 || !key || !val || klen == 0 || vlen == 0) return -EINVAL;

    MDB_txn* txn = NULL;

retry:
{
    int ret = mdb_txn_begin(DB->env, NULL, 0, &txn);
    if(ret != MDB_SUCCESS)
    {
        LMDB_LOG_ERR("put_one:mdb_txn_begin", ret);
        return db_map_mdb_err(ret);
    }

    /* ---------------------------------------------------------------------
     * Critical: detect DUPSORT. If set, LMDB needs to compare the VALUE,
     * so we MUST provide real bytes (v.mv_data != NULL). MDB_RESERVE would
     * hand LMDB a NULL pointer during placement → memcmp(NULL, ...) crash.
     * --------------------------------------------------------------------- */
    unsigned dbif = 0;
    ret           = mdb_dbi_flags(txn, dbi, &dbif);
    if(ret != MDB_SUCCESS)
    {
        LMDB_LOG_ERR("put_one:mdb_dbi_flags", ret);
        mdb_txn_abort(txn);
        return db_map_mdb_err(ret);
    }
    const int is_dups = (dbif & MDB_DUPSORT) != 0;

    MDB_val k = {0};
    k.mv_size = klen;
    k.mv_data = (void*)key;
    MDB_val v = {0};

    unsigned put_flags = flags;

    if(is_dups)
    {
        /* ---- DUPSORT path: pass actual value bytes, NO MDB_RESERVE ---- */
        v.mv_size = vlen;
        v.mv_data = (void*)val;
        ret       = mdb_put(txn, dbi, &k, &v, put_flags /* no RESERVE */);
        if(ret != MDB_SUCCESS)
        {
            LMDB_LOG_ERR("put_one:mdb_put dupsort", ret);
            mdb_txn_abort(txn);
            return db_map_mdb_err(ret);
        }
    }
    else
    {
        /* ---- Non-DUPSORT path: keep zero-copy reserve + memcpy ---- */
        v.mv_size  = vlen;
        v.mv_data  = NULL;
        put_flags |= MDB_RESERVE;

        ret = mdb_put(txn, dbi, &k, &v, put_flags);
        if(ret == MDB_MAP_FULL)
        {
            mdb_txn_abort(txn);
            int xr = db_env_mapsize_expand();
            if(xr != 0)
            {
                EML_ERROR("db_ops", "put_one: mapsize_expand failed %d", xr);
                return xr;
            }
            goto retry;
        }
        if(ret != MDB_SUCCESS)
        {
            LMDB_LOG_ERR("put_one:mdb_put RESERVE", ret);
            mdb_txn_abort(txn);
            return db_map_mdb_err(ret);
        }

        /* Fill reserved page now that LMDB has placed the record */
        memcpy(v.mv_data, val, vlen);
    }

    ret = mdb_txn_commit(txn);
    if(ret == MDB_MAP_FULL)
    {
        EML_WARN("db_ops", "put_one: MAP_FULL at commit, expanding & retry");
        int xr = db_env_mapsize_expand();
        if(xr != 0)
        {
            EML_ERROR("db_ops", "put_one: mapsize_expand failed %d", xr);
            return xr;
        }
        goto retry; /* failed commit already aborted txn */
    }
    if(ret != MDB_SUCCESS)
    {
        LMDB_LOG_ERR("put_one:mdb_txn_commit", ret);
        /* failed commit already aborted txn */
        return db_map_mdb_err(ret);
    }
}
    return 0;
}

int ops_put_prepare(DB_operation_t* op, MDB_dbi dbi, const void* key_seg, size_t key_seg_size,
                    size_t nsegs, unsigned flags)
{
    if(!op || dbi == 0) return -EIO;

    /* set dbi, op_type, flags, null stores and prev/next */
    _prepare_op(op, DB_OPERATION_PUT, dbi, flags);

    /* Initialize key_store */
    if(_prepare_key(&op->key_store, key_seg, key_seg_size) != 0)
    {
        fprintf(stderr, "void_store_init / add failed\n");
        return -ENOMEM;
    }

    /* Initialize val_store for put_add */
    if(void_store_init(nsegs, &op->val_store) != 0) return -1;

    return 0;
}

int ops_put_prepare_add(DB_operation_t* op, const void* val_seg, size_t val_seg_size)
{
    return void_store_add(op->val_store, val_seg, val_seg_size);
}

int ops_get_one(MDB_dbi dbi, const void* key, size_t klen, void* out, size_t* out_len)
{
    if(dbi == 0 || !key || klen == 0) return -EINVAL;

    int      ret = -1;
    MDB_txn* txn = NULL;

retry:
{
    ret = mdb_txn_begin(DB->env, NULL, MDB_RDONLY, &txn);
    if(ret != MDB_SUCCESS) goto fail;

    MDB_val k = {0};
    k.mv_size = klen;
    k.mv_data = (void*)key;
    MDB_val v = {0};

    ret = mdb_get(txn, dbi, &k, &v);
    if(ret == MDB_NOTFOUND) goto fail;
    if(ret == MDB_MAP_RESIZED)
    {
        /* environment grew under us; abort and retry once more */
        mdb_txn_abort(txn);
        goto retry;
    }
    if(ret != MDB_SUCCESS) goto fail;

    if(out)
    {
        /* Copy and report exact size */
        memcpy(out, v.mv_data, v.mv_size);
    }

    if(out_len)
    {
        *out_len = v.mv_size;
    }

    mdb_txn_abort(txn); /* RO txn: abort to close */
    return 0;
}
fail:
{
    LMDB_EML_WARN("get_one", ret);
    mdb_txn_abort(txn); /* RO txn: abort to close */
    return db_map_mdb_err(ret);
}
}

int ops_get_prepare(DB_operation_t* op, MDB_dbi dbi, const void* key_seg, size_t seg_size)
{
    if(!op || dbi == 0) return -EIO;

    /* auto-size C strings if seg_size==0 */
    size_t klen = seg_size;
    if(key_seg && klen == 0)
    {
        klen = strnlen((const char*)key_seg, DB_EMAIL_MAX_LEN);
    }

    /* set dbi, op_type, flags, null stores and prev/next */
    _prepare_op(op, DB_OPERATION_GET, dbi, 0);

    /* Initialize key_store */
    if(_prepare_key(&op->key_store, key_seg, klen) != 0)
    {
        EML_ERROR("db_ops", "ops_get_prepare: prepare_key failed (klen=%zu)", klen);
        return -ENOMEM;
    }
    /* If no key in, will try to get the the prev result as key */

    return 0;
}

int ops_rep_prepare(DB_operation_t* op, MDB_dbi dbi, const void* key_seg, size_t key_seg_size,
                    size_t nsegs)
{
    if(!op || dbi == 0) return -EIO;

    /* set dbi, op_type, flags, null stores and prev/next */
    _prepare_op(op, DB_OPERATION_REP, dbi, 0);

    /* Initialize key_store */
    if(_prepare_key(&op->key_store, key_seg, key_seg_size) != 0)
    {
        fprintf(stderr, "void_store_init / add failed\n");
        return -ENOMEM;
    }

    /* Initialize val_store for rep_add */
    if(void_store_init(nsegs, &op->val_store) != 0) return -1;

    return 0;
}

int ops_rep_prepare_add(DB_operation_t* op, const void* val_seg, size_t val_seg_size)
{
    return void_store_add(op->val_store, val_seg, val_seg_size);
}

int ops_lst_prepare(void)
{
    return 0;
}

int ops_comp_prepare(void)
{
    return 0;
}

int ops_del_prepare(DB_operation_t* op, MDB_dbi dbi, const void* key_seg, size_t key_seg_size,
                    size_t nsegs)
{
    if(!op || dbi == 0) return -EIO;

    /* set dbi, op_type, flags, null stores and prev/next */
    _prepare_op(op, DB_OPERATION_DEL, dbi, 0);

    /* Initialize key_store (optional; if absent we’ll use prev->dst) */
    if(_prepare_key(&op->key_store, key_seg, key_seg_size) != 0)
    {
        fprintf(stderr, "ops_del_prepare: key store init failed\n");
        return -ENOMEM;
    }

    /* Optional value for duplicate-exact delete */
    if(nsegs > 0)
    {
        if(void_store_init(nsegs, &op->val_store) != 0) return -ENOMEM;
    }

    return 0;
}

int ops_del_prepare_add(DB_operation_t* op, const void* val_seg, size_t val_seg_size)
{
    return void_store_add(op->val_store, val_seg, val_seg_size);
}

int ops_del_one(MDB_dbi dbi, const void* key, size_t klen, const void* val, size_t vlen)
{
    if(dbi == 0 || !key || klen == 0) return -EINVAL;

    MDB_txn* txn = NULL;

retry:
{
    int ret = mdb_txn_begin(DB->env, NULL, 0, &txn);
    if(ret != MDB_SUCCESS)
    {
        LMDB_LOG_ERR("del_one:mdb_txn_begin", ret);
        return db_map_mdb_err(ret);
    }

    MDB_val  k    = {.mv_size = klen, .mv_data = (void*)key};
    MDB_val* vptr = NULL;
    MDB_val  v    = {0};

    if(val && vlen > 0)
    {
        v.mv_size = vlen;
        v.mv_data = (void*)val;
        vptr      = &v;
    }

    ret = mdb_del(txn, dbi, &k, vptr);
    if(ret == MDB_NOTFOUND)
    {
        mdb_txn_abort(txn);
        return -ENOENT;
    }

    if(ret == MDB_MAP_RESIZED)
    {
        mdb_txn_abort(txn);
        goto retry;
    }

    if(ret != MDB_SUCCESS)
    {
        LMDB_LOG_ERR("del_one:mdb_del", ret);
        mdb_txn_abort(txn);
        return db_map_mdb_err(ret);
    }

    ret = mdb_txn_commit(txn);
    if(ret == MDB_MAP_RESIZED)
    {
        goto retry; /* txn already aborted by failed commit */
    }

    if(ret != MDB_SUCCESS)
    {
        LMDB_LOG_ERR("del_one:mdb_txn_commit", ret);
        return db_map_mdb_err(ret);
    }
}
    return 0;
}

int ops_exec(DB_operation_t* ops, size_t* n_ops)
{
    if(!ops || !n_ops || *n_ops == 0)
    {
        EML_ERROR("db_ops", "ops_exec: invalid input (ops=%p n_ops=%p *n_ops=%zu)", (void*)ops,
                  (void*)n_ops, n_ops ? *n_ops : 0UL);
        return -EINVAL;
    }

    /* link the operations */
    _ops_link(ops, *n_ops);

    /* Initialize the transaction */
    MDB_txn* txn = NULL;

retry:
    int ret = mdb_txn_begin(DB->env, NULL, 0, &txn);
    if(ret != MDB_SUCCESS)
    {
        LMDB_LOG_ERR("ops_exec:mdb_txn_begin", ret);
        goto fail;
    }

    for(size_t i = 0; i < *n_ops; i++)
    {
        /* exec single operation */
        ret = _exec_op(txn, &ops[i]);

        if(ret != MDB_SUCCESS)
        {
            if(ret == MDB_MAP_FULL)
            {
                EML_WARN("db_ops",
                         "ops_exec: MDB_MAP_FULL during op %zu, expanding "
                         "mapsize & retry",
                         i);
                mdb_txn_abort(txn);
                int xr = db_env_mapsize_expand();
                if(xr != 0)
                {
                    EML_ERROR("db_ops", "ops_exec: mapsize_expand failed ret=%d", xr);
                    ret = xr;
                    goto fail;
                }
                goto retry;
            }
            // exec_op may already return mapped errors; log original if it’s LMDB-ish
            EML_ERROR("db_ops", "ops_exec: %d", ret);
            goto fail;
        }
    }

    /* Commit the transaction */
    ret = mdb_txn_commit(txn);
    if(ret == MDB_MAP_FULL)
    {
        EML_WARN("db_ops", "ops_exec: MDB_MAP_FULL at commit, expanding & retry");
        /* txn aborted by commit */
        int xr = db_env_mapsize_expand();
        if(xr != 0)
        {
            EML_ERROR("db_ops", "ops_exec: mapsize_expand failed ret=%d", xr);
            ret = xr;
            goto fail;
        }
        goto retry;
    }
    if(ret != MDB_SUCCESS)
    {
        LMDB_LOG_ERR("ops_exec:mdb_txn_commit", ret);
        goto fail;
    }

    return 0;
fail:
    mdb_txn_abort(txn);
    return db_map_mdb_err(ret);
}

void ops_free(DB_operation_t** ops, size_t* n_ops)
{
    if(!ops || !*ops || !n_ops) return;

    DB_operation_t* arr   = *ops;
    size_t          count = *n_ops;

    for(size_t i = 0; i < count; ++i)
    {
        /* Close the stores (frees internal arrays and the store object) */
        void_store_close(&arr[i].key_store);
        void_store_close(&arr[i].val_store);

        /* Free any result buffer allocated by _op_get */
        if(arr[i].dst)
        {
            free(arr[i].dst);
            arr[i].dst     = NULL;
            arr[i].dst_len = 0;
        }

        /* Clear other pointers for hygiene (optional) */
        arr[i].prev  = NULL;
        arr[i].next  = NULL;
        arr[i].dbi   = (MDB_dbi)0;
        arr[i].flags = 0;
        arr[i].type  = DB_OPERATION_NONE;
    }

    /* Free the operations array itself */
    free(arr);
    *ops   = NULL;
    *n_ops = 0;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int _dbi_is_dupsort(MDB_txn* txn, MDB_dbi dbi, int* out_is_dupsort)
{
    unsigned int flags = 0;
    int          ret   = mdb_dbi_flags(txn, dbi, &flags);
    if(ret != MDB_SUCCESS)
    {
        LMDB_LOG_ERR("_dbi_is_dupsort:mdb_dbi_flags", ret);
        return ret;
    }
    *out_is_dupsort = (flags & MDB_DUPSORT) ? 1 : 0;
    return 0;
}

static int _prepare_key(void_store_t** st, void* key_seg, size_t seg_size)
{
    if(key_seg && seg_size > 0)
    {
        if(void_store_init(1, st) != 0)
        {
            EML_ERROR("db_ops", "prepare_key: void_store_init failed");
            return -1;
        }
        if(void_store_add(*st, key_seg, seg_size) != 0)
        {
            EML_ERROR("db_ops", "prepare_key: void_store_add failed (seg_size=%zu)", seg_size);
            return -1;
        }
        EML_DBG("db_ops", "prepare_key: seg_size=%zu", seg_size);
    }
    else if(key_seg && seg_size == 0)
    {
        EML_ERROR("db_ops", "prepare_key: zero-length key passed in");
        return -EINVAL;
    }
    return 0;
}

static void _prepare_op(DB_operation_t* op, DB_operation_type_t type, MDB_dbi dbi, unsigned flags)
{
    op->type  = type;
    op->dbi   = dbi;
    op->flags = flags;

    /* void stores */
    op->key_store = NULL;
    op->val_store = NULL;

    /* linking */
    op->prev = NULL;
    op->next = NULL;
}

static void _ops_link(DB_operation_t* ops, size_t n_ops)
{
    /* Nothing to link */
    if(n_ops <= 1) return;

    /* Use size_t for indexing but compare with n_ops after cast */
    for(size_t i = 0; i < n_ops; ++i)
    {
        DB_operation_t* op = &ops[i];

        if(i == 0)
        {
            op->prev = NULL;
            op->next = &ops[i + 1];
        }
        else if(i == n_ops - 1)
        {
            op->prev = &ops[i - 1];
            op->next = NULL;
        }
        else
        {
            op->prev = &ops[i - 1];
            op->next = &ops[i + 1];
        }
    }
}

static int _exec_op(MDB_txn* txn, DB_operation_t* op)
{
    switch(op->type)
    {
        case DB_OPERATION_PUT:
            return _op_put(txn, op);

        case DB_OPERATION_GET:
            return _op_get(txn, op);

        case DB_OPERATION_REP:
            return _op_rep(txn, op);

        case DB_OPERATION_DEL:
            return _op_del(txn, op);
        default:
            return -1;
    }
}

static int _op_get(MDB_txn* txn, DB_operation_t* op)
{
    MDB_val k    = {0};
    MDB_val v    = {0};
    void*   kbuf = NULL;
    int     ret;

    EML_DBG("db_ops", "_op_get: starting get operation on dbi=%u", op->dbi);
    /* If a key store exists, build key buffer from it */
    if(op->key_store)
    {
        EML_DBG("db_ops", "_op_get: using key from key_store");
        size_t ksize = void_store_size(op->key_store);
        if(ksize == 0)
        {
            EML_ERROR("db_ops", "op_get: key_store present but size=0");
            return EINVAL;
        }
        kbuf = void_store_malloc_buf(op->key_store);
        if(!kbuf)
        {
            EML_ERROR("db_ops", "op_get: void_store_malloc_buf failed");
            return ENOMEM;
        }
        k.mv_size = void_store_size(op->key_store);
        k.mv_data = kbuf;
        EML_DBG("db_ops", "_op_get: built key from store, size=%zu", k.mv_size);
    }
    /* No key_store: try to use previous op's dst as key (runtime dependency) */
    else
    {
        EML_DBG("db_ops", "_op_get: no key_store, attempting to use prev->dst as key");
        if(!op->prev || !op->prev->dst || op->prev->dst_len == 0)
        {
            EML_ERROR("db_ops", "op_get: missing prev result for key");
            return EINVAL;
        }
        k.mv_size = op->prev->dst_len;
        k.mv_data = op->prev->dst; /* do NOT free this pointer here */
        EML_DBG("db_ops", "_op_get: using prev->dst as key, size=%zu", k.mv_size);
    }

    EML_DBG("db_ops", "_op_get: attempting get with key size=%zu", k.mv_size);
    ret = mdb_get(txn, op->dbi, &k, &v);

    /* free key buffer if allocated one */
    if(kbuf) free(kbuf);

    if(ret != MDB_SUCCESS)
    {
        if(ret == MDB_NOTFOUND)
        {
            EML_DBG("db_ops", "_op_get: key not found in dbi=%u, ret=%d", op->dbi, ret);
            LMDB_EML_WARN("op_get:mdb_get NOTFOUND", ret);
        }
        else
        {
            LMDB_LOG_ERR("op_get:mdb_get", ret);
        }
        return ret;
    }
    /* copy value out to a new buffer owned by op->dst */
    void* buf = malloc(v.mv_size);
    if(!buf)
    {
        EML_ERROR("db_ops", "op_get: malloc(%zu) failed", v.mv_size);
        return ENOMEM;
    }
    memcpy(buf, v.mv_data, v.mv_size);

    /* save result in op (caller/ops_free will free) */
    op->dst     = buf;
    op->dst_len = v.mv_size;

    EML_DBG("db_ops", "_op_get: successfully retrieved value of size=%zu", v.mv_size);
    return 0;
}

static int _op_put(MDB_txn* txn, DB_operation_t* op)
{
    EML_DBG("db_ops", "_op_put: starting put operation on dbi=%u", op->dbi);

    /* Build key buffer from store */
    void* kbuf = void_store_malloc_buf(op->key_store);
    if(!kbuf)
    {
        EML_ERROR("db_ops", "op_put: key malloc_buf failed");
        return ENOMEM;
    }

    /* Sanity: value must exist and be non-empty */
    size_t vlen = void_store_size(op->val_store);
    if(vlen == 0)
    {
        free(kbuf);
        EML_ERROR("db_ops", "op_put: empty value");
        return EINVAL;
    }

    /* Detect DUPSORT to decide RESERVE strategy */
    unsigned dbif = 0;
    int      rc   = mdb_dbi_flags(txn, op->dbi, &dbif);
    if(rc != MDB_SUCCESS)
    {
        free(kbuf);
        LMDB_LOG_ERR("_op_put:mdb_dbi_flags", rc);
        return rc;
    }
    const int is_dups = (dbif & MDB_DUPSORT) != 0;

    MDB_val k = {0};
    k.mv_size = void_store_size(op->key_store);
    k.mv_data = kbuf;
    int ret;

    if(is_dups)
    {
        /* -----------------------------------------------------------------
         * DUPSORT path: provide REAL value bytes (NO MDB_RESERVE).
         * LMDB needs to compare duplicates; passing NULL would crash.
         * ----------------------------------------------------------------- */
        void* vbuf = void_store_malloc_buf(op->val_store);
        if(!vbuf)
        {
            free(kbuf);
            return ENOMEM;
        }

        MDB_val v = {0};
        v.mv_size = vlen;
        v.mv_data = vbuf;

        /* DO NOT add MDB_RESERVE here */
        ret = mdb_put(txn, op->dbi, &k, &v, op->flags);
        if(ret != MDB_SUCCESS)
        {
            LMDB_LOG_ERR("op_put:mdb_put dupsort", ret);
            free(vbuf);
            free(kbuf);
            return ret;
        }

        /* We created a temporary value buffer; safe to free after put */
        free(vbuf);
        free(kbuf);
        EML_DBG("db_ops", "_op_put: dupsort put completed");
        return 0;
    }
    else
    {
        /* -----------------------------------------------------------------
         * Non-DUPSORT path: keep MDB_RESERVE (zero-copy into page).
         * ----------------------------------------------------------------- */
        MDB_val v = {0};
        v.mv_size = vlen;
        v.mv_data = NULL;

        ret = mdb_put(txn, op->dbi, &k, &v, op->flags | MDB_RESERVE);
        if(ret != MDB_SUCCESS)
        {
            LMDB_LOG_ERR("op_put:mdb_put RESERVE", ret);
            free(kbuf);
            return ret;
        }

        /* Encode into the reserved buffer */
        size_t wrote = void_store_memcpy(v.mv_data, v.mv_size, op->val_store);
        if(wrote != v.mv_size)
        {
            EML_ERROR("db_ops", "op_put: encode mismatch wrote=%zu expected=%zu", wrote, v.mv_size);
            free(kbuf);
            return EFAULT;
        }

        free(kbuf);
        EML_DBG("db_ops", "_op_put: successfully wrote value to database");
        return 0;
    }
}

static int _op_rep(MDB_txn* txn, DB_operation_t* op)
{
    MDB_val key  = {0};
    void*   kbuf = NULL;

    /* -------- Determine key to use -------- */
    if(op->key_store)
    {
        size_t ksize = void_store_size(op->key_store);
        if(ksize == 0)
        {
            EML_ERROR("db_ops", "op_rep: empty key_store");
            return EINVAL;
        }
        kbuf = void_store_malloc_buf(op->key_store);
        if(!kbuf)
        {
            EML_ERROR("db_ops", "op_rep: key malloc_buf failed");
            return ENOMEM;
        }
        key.mv_size = ksize;
        key.mv_data = kbuf;
    }
    else
    {
        if(!op->prev || !op->prev->dst || op->prev->dst_len == 0)
        {
            EML_ERROR("db_ops", "op_rep: missing prev->dst for key");
            return EINVAL;
        }
        key.mv_size = op->prev->dst_len;
        key.mv_data = op->prev->dst; /* owned by prev op */
    }

    /* -------- Reject DUPSORT: replacing in-place can break dup order -------- */
    unsigned dbif = 0;
    int      rc   = mdb_dbi_flags(txn, op->dbi, &dbif);
    if(rc != MDB_SUCCESS)
    {
        if(kbuf) free(kbuf);
        LMDB_LOG_ERR("op_rep:mdb_dbi_flags", rc);
        return rc;
    }
    if(dbif & MDB_DUPSORT)
    {
        if(kbuf) free(kbuf);
        EML_ERROR("db_ops", "op_rep: DUPSORT not supported; use delete+put instead");
        return ENOTSUP; /* Explicitly surface to caller/test */
    }

    /* -------- Open cursor on NON-DUPSORT DB -------- */
    MDB_cursor* cur;
    rc = mdb_cursor_open(txn, op->dbi, &cur);
    if(rc != MDB_SUCCESS)
    {
        if(kbuf) free(kbuf);
        LMDB_LOG_ERR("op_rep:mdb_cursor_open", rc);
        return rc;
    }

    /* Position at the single record for this key (non-dupsort) */
    MDB_val val;
    rc = mdb_cursor_get(cur, &key, &val, MDB_SET_KEY);
    if(rc != MDB_SUCCESS)
    {
        LMDB_LOG_ERR("op_rep:mdb_cursor_get SET_KEY", rc);
        mdb_cursor_close(cur);
        if(kbuf) free(kbuf);
        return rc;
    }

    if(val.mv_size == 0)
    {
        EML_ERROR("db_ops", "op_rep: cannot patch zero-length value");
        mdb_cursor_close(cur);
        if(kbuf) free(kbuf);
        return EINVAL;
    }

    /* Make a working copy, apply patch (must NOT change length) */
    void* tmp = calloc(1, val.mv_size);
    if(!tmp)
    {
        EML_ERROR("db_ops", "op_rep: malloc(%zu) failed", val.mv_size);
        mdb_cursor_close(cur);
        if(kbuf) free(kbuf);
        return ENOMEM;
    }
    memcpy(tmp, val.mv_data, val.mv_size);

    /* Patch semantics via void_store_memcpy:
     *  - Each segment is written sequentially into the target buffer.
     *  - NULL segment pointer => skip 'n' bytes (no write).
     *  - Total patch size must be <= current value size (enforced by memcpy helper).
     */
    if(!op->val_store)
    {
        EML_ERROR("db_ops", "op_rep: no val_store provided for patch");
        free(tmp);
        mdb_cursor_close(cur);
        if(kbuf) free(kbuf);
        return EINVAL;
    }
    size_t wrote = void_store_memcpy(tmp, val.mv_size, op->val_store);
    size_t need  = void_store_size(op->val_store);
    if(wrote != need)
    {
        EML_ERROR("db_ops", "op_rep: void_store_memcpy failed wrote=%zu expected=%zu", wrote, need);
        free(tmp);
        mdb_cursor_close(cur);
        if(kbuf) free(kbuf);
        return EINVAL;
    }

    /* Replace-in-place at current cursor (non-dupsort has exactly 1 value) */
    MDB_val newv = {.mv_size = val.mv_size, .mv_data = tmp};
    rc           = mdb_cursor_put(cur, &key, &newv, MDB_CURRENT);
    if(rc != MDB_SUCCESS)
    {
        LMDB_LOG_ERR("op_rep:mdb_cursor_put CURRENT", rc);
    }

    free(tmp);
    if(kbuf) free(kbuf);
    mdb_cursor_close(cur);
    return rc;
}

static int _op_del(MDB_txn* txn, DB_operation_t* op)
{
    MDB_val k    = {0};
    void*   kbuf = NULL;

    /* --- Build/select key --- */
    if(op->key_store)
    {
        kbuf = void_store_malloc_buf(op->key_store);
        if(!kbuf)
        {
            EML_ERROR("db_ops", "op_del: key malloc_buf failed");
            return ENOMEM;
        }
        k.mv_size = void_store_size(op->key_store);
        k.mv_data = kbuf;
        if(k.mv_size == 0)
        {
            free(kbuf);
            EML_ERROR("db_ops", "op_del: empty key");
            return EINVAL;
        }
    }
    else
    {
        if(!op->prev || !op->prev->dst || op->prev->dst_len == 0)
        {
            EML_ERROR("db_ops", "op_del: missing key (no key_store and no prev->dst)");
            return EINVAL;
        }
        k.mv_size = op->prev->dst_len;
        k.mv_data = op->prev->dst; /* owned by prev op */
    }

    /* --- Is this a dupsort DB? --- */
    int is_dups = 0;
    int ret     = _dbi_is_dupsort(txn, op->dbi, &is_dups);
    if(ret != MDB_SUCCESS)
    {
        if(kbuf) free(kbuf);
        return db_map_mdb_err(ret);
    }

    /* --- Optional value (for exact dup delete) --- */
    MDB_val v    = {0};
    void*   vbuf = NULL;
    size_t  vlen = 0;

    if(op->val_store)
    {
        vlen = void_store_size(op->val_store);
        if(vlen > 0)
        {
            vbuf = void_store_malloc_buf(op->val_store);
            if(!vbuf)
            {
                if(kbuf) free(kbuf);
                EML_ERROR("db_ops", "op_del: val malloc_buf failed");
                return ENOMEM;
            }
            v.mv_size = vlen;
            v.mv_data = vbuf;
        }
    }

    /* --- Execute --- */
    int delret;
    if(is_dups)
    {
        if(vlen > 0)
        {
            delret = mdb_del(txn, op->dbi, &k, &v);  // exact dup delete
            EML_DBG("db_ops", "op_del: dupsort exact key_len=%zu val_len=%zu", k.mv_size,
                    v.mv_size);
        }
        else
        {
            delret = mdb_del(txn, op->dbi, &k, NULL);  // all dups for key
            EML_DBG("db_ops", "op_del: dupsort all-for-key key_len=%zu", k.mv_size);
        }
    }
    else
    {
        delret = mdb_del(txn, op->dbi, &k, NULL);
        EML_DBG("db_ops", "op_del: single key key_len=%zu", k.mv_size);
    }

    if(vbuf) free(vbuf);
    if(kbuf) free(kbuf);

    if(delret != MDB_SUCCESS && delret != MDB_NOTFOUND)
    {
        LMDB_LOG_ERR("op_del:mdb_del", delret);
    }
    else if(delret == MDB_NOTFOUND)
    {
        LMDB_EML_WARN("op_del:mdb_del NOTFOUND (idempotent)", delret);
    }

    return 0;
}
