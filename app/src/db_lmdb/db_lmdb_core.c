/**
 * @file db_lmdb_core.c
 */

#include "db_lmdb_core.h"
#include "db_lmdb_internal.h" /* interface, config, emlog */

/************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG                 "db_lmdb_core"

#define DB_LMDB_RETRY_OPERATION 3

/************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Core safety decision function.
 *
 * This function maps LMDB return codes into actionable decisions
 * according to the safety policy. If a transaction is provided, it
 * aborts the transaction on RETRY/FAIL if provided.
 * 
 * @param mdb_rc       LMDB return code.
 * @param is_write_txn Non-zero if the operation was in a write transaction.
 * @param retry_budget Optional retry counter (decremented on retry). May be NULL.
 * @param out_mapped_err Optional mapped errno on FAIL.
 * @param txn          Optional txn to abort on RETRY/FAIL. May be NULL.
 * @return DB_LMDB_SAFE_OK/RETRY/FAIL
 */
int _safety_check(int mdb_rc, int is_write_txn, size_t* retry_budget, int* out_mapped_err,
                  MDB_txn* txn);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int db_lmdb_create_env_safe(struct DB* DataBase, const char* path, unsigned int max_dbis,
                            size_t db_map_size)
{
    /* Check input */
    if(!path || !DataBase)
    {
        EML_ERROR(LOG_TAG, "_create_env_safe: invalid input");
        return -EINVAL;
    }

    /* Create environment and pass through safety */
    int res = mdb_env_create(&DataBase->env);
    if(_safety_check(res, 0, NULL, NULL, NULL) != DB_LMDB_SAFE_OK)
    {
        LMDB_LOG_ERR(LOG_TAG, "_create_env_safe: mdb_env_create failed", res);
        return res;
    }

    /* Set max sub-dbis */
    res = mdb_env_set_maxdbs(DataBase->env, (MDB_dbi)max_dbis);
    if(_safety_check(res, 0, NULL, NULL, NULL) != DB_LMDB_SAFE_OK)
    {
        LMDB_LOG_ERR(LOG_TAG, "_create_env_safe: mdb_env_set_maxdbs failed", res);
        return res;
    }

    /* Set initial map size */
    res = mdb_env_set_mapsize(DataBase->env, db_map_size);
    if(_safety_check(res, 0, NULL, NULL, NULL) != DB_LMDB_SAFE_OK)
    {
        LMDB_LOG_ERR(LOG_TAG, "_create_env_safe: mdb_env_set_mapsize failed", res);
        return res;
    }

    /* Open environment */
    res = mdb_env_open(DataBase->env, path, 0, 0770);
    if(_safety_check(res, 0, NULL, NULL, NULL) != DB_LMDB_SAFE_OK)
    {
        LMDB_LOG_ERR(LOG_TAG, "_create_env_safe: mdb_env_open failed", res);
        return res;
    }

    return DB_LMDB_SAFE_OK;
}

int db_lmdb_txn_begin_safe(MDB_env* env, unsigned flags, MDB_txn** out_txn, size_t* retry_budget,
                           int* out_err)
{
    /* Check input */
    if(!env || !out_txn)
    {
        EML_ERROR(LOG_TAG, "txn_begin_safe: invalid input (env=%p out_txn=%p)", (void*)env,
                  (void*)out_txn);
        return DB_LMDB_SAFE_FAIL;
    }
    size_t budget = retry_budget ? *retry_budget : DB_LMDB_RETRY_TRANSACTION;

retry:
{
    int res = mdb_txn_begin(env, NULL, flags, out_txn);
    if (res != 0)
    {
        int act = _safety_check(res, 0, &budget, out_err, out_txn);
        switch (act)
        {
        case DB_LMDB_SAFE_OK: /* should not happen */
            EML_WARN(LOG_TAG, "txn_begin_safe: unexpected SAFE_OK");
            break;
        case DB_LMDB_SAFE_RETRY:
            /* update retry budget decreased in safety check */
            if(retry_budget) *retry_budget = budget;
            EML_WARN(LOG_TAG, "txn_begin_safe: retrying, retry_budget=%zu", budget);
            goto retry;
        default:
            EML_ERROR(LOG_TAG, "txn_begin_safe: failed with err %d", res);
            return DB_LMDB_SAFE_FAIL;
        }
    }
}

    return DB_LMDB_SAFE_OK;
}

int db_lmdb_txn_commit_safe(MDB_txn* txn, size_t* retry_budget, int* out_err)
{
    /* Check input */
    if(!txn)
    {
        EML_ERROR(LOG_TAG, "txn_commit_safe: invalid input (txn=NULL)");
        return DB_LMDB_SAFE_FAIL;
    }
    size_t budget = retry_budget ? *retry_budget : DB_LMDB_RETRY_TRANSACTION;

retry:
{
    int res = mdb_txn_commit(txn);
    if (res != 0)
    {
        int act = _safety_check(res, 0, &budget, out_err, txn);
        switch (act)
        {
        case DB_LMDB_SAFE_OK: /* should not happen */
            EML_WARN(LOG_TAG, "txn_commit_safe: unexpected SAFE_OK");
            break;
        case DB_LMDB_SAFE_RETRY:
            /* update retry budget decreased in safety check */
            if(retry_budget) *retry_budget = budget;
            EML_WARN(LOG_TAG, "txn_commit_safe: retrying, retry_budget=%zu", budget);
            goto retry;
        default:
            EML_ERROR(LOG_TAG, "txn_commit_safe: failed with err %d", res);
            return DB_LMDB_SAFE_FAIL;
        }
    }
}

    return DB_LMDB_SAFE_OK;
}

int db_lmdb_dbi_open_safe(MDB_txn* txn, const char* name, unsigned int open_flags, MDB_dbi* out_dbi,
                          size_t* retry_budget, int* out_mdb_rc, int* out_err)
{
    if(out_err) *out_err = 0;
    if(out_mdb_rc) *out_mdb_rc = MDB_SUCCESS;

    if(!txn || !name || !out_dbi)
    {
        if(out_err) *out_err = -EINVAL;
        return DB_LMDB_SAFE_FAIL;
    }

    size_t budget = retry_budget ? *retry_budget : DB_LMDB_RETRY_DBI_OPEN;
    int    res    = mdb_dbi_open(txn, name, open_flags, out_dbi);
    if(out_mdb_rc) *out_mdb_rc = res;

    int act = _safety_check(res, 1, &budget, out_err, txn);
    if(retry_budget) *retry_budget = budget;
    return act;
}

int db_lmdb_dbi_get_flags_safe(MDB_txn* txn, MDB_dbi dbi, unsigned int* out_flags,
                               size_t* retry_budget, int* out_mdb_rc, int* out_err)
{
    if(out_err) *out_err = 0;
    if(out_mdb_rc) *out_mdb_rc = MDB_SUCCESS;

    if(!txn || !out_flags)
    {
        if(out_err) *out_err = -EINVAL;
        return DB_LMDB_SAFE_FAIL;
    }

    size_t budget = retry_budget ? *retry_budget : DB_LMDB_RETRY_DBI_FLAGS;
    int    res    = mdb_dbi_flags(txn, dbi, out_flags);
    if(out_mdb_rc) *out_mdb_rc = res;

    int act = _safety_check(res, 0, &budget, out_err, txn);
    if(retry_budget) *retry_budget = budget;
    return act;
}

int db_lmdb_safety_check(int mdb_rc, int is_write_txn, size_t* retry_budget, int* out_mapped_err,
                         MDB_txn* txn)
{
    return _safety_check(mdb_rc, is_write_txn, retry_budget, out_mapped_err, txn);
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
int _safety_check(int mdb_rc, int is_write_txn, size_t* retry_budget, int* out_mapped_err,
                  MDB_txn* txn)
{
    /* Map error if requested */
    if(out_mapped_err) *out_mapped_err = db_map_mdb_err(mdb_rc);

    /* Fast path */
    if(mdb_rc == MDB_SUCCESS) return DB_LMDB_SAFE_OK;

    /* Should be able to accept with null txn to just check the rc */
    if(!txn)
    {
        /* If not transaction in input, the operation does not have to retry,
        and if not MDB_SUCCESS, then return fail, nothing to retry */
        goto fatal;
    }

    /* Expected benign cases that callers may treat as fail */
    if(mdb_rc == MDB_NOTFOUND || mdb_rc == MDB_KEYEXIST) return DB_LMDB_SAFE_FAIL;

    /* Retryable classes */
    switch(mdb_rc)
    {
        /* Retry Cases */
        case MDB_MAP_RESIZED:
        case MDB_PAGE_FULL:
        case MDB_TXN_FULL:
        case MDB_CURSOR_FULL:
        case MDB_BAD_RSLOT:
        case MDB_READERS_FULL:
            break;
        case MDB_MAP_FULL:
            if(db_env_mapsize_expand() == 0) break; /* then retry */
            /* fallthrough to fail if resize impossible */
        default:
            goto fatal;
    }

    /* Check retry budget */
    if(!retry_budget)
    {
        EML_ERROR(LOG_TAG, "_decide: retry budget pointer missing");
        goto fatal;
    }

    if(*retry_budget == 0)
    {
        EML_ERROR(LOG_TAG, "_decide: retry budget exhausted");
        goto fatal;
    }
    else
    {
        EML_WARN(LOG_TAG, "_decide: retryable mdb_rc=%d, retry_budget=%zu", mdb_rc, *retry_budget);
        (*retry_budget)--;
    }
    if(txn) mdb_txn_abort(txn);
    return DB_LMDB_SAFE_RETRY;

fatal:
    (void)is_write_txn; /* reserved for future use */
    if(txn) mdb_txn_abort(txn);
    return DB_LMDB_SAFE_FAIL;
}
