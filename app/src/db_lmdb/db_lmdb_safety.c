/**
 * @file db_lmdb_safety.c
 * @brief Map LMDB return codes to retry/resize/fatal decisions.
 */

#include "db_lmdb_safety.h"

#include "db_lmdb_internal.h"

#define LOG_TAG "lmdb_safety"

static int _mapped(int mdb_rc)
{
    return db_map_mdb_err(mdb_rc);
}

int db_lmdb_safety_decide(int mdb_rc, int is_write_txn, size_t* retry_budget, int* out_mapped_err,
                          MDB_txn* txn)
{
    /* Input check */
    if(!txn)
    {
        EML_ERROR(LOG_TAG, "_decide: NULL txn pointer");
        return DB_LMDB_SAFE_FAIL;
    }

    if(out_mapped_err) *out_mapped_err = _mapped(mdb_rc);

    /* Fast path */
    if(mdb_rc == MDB_SUCCESS) return DB_LMDB_SAFE_OK;

    /* Expected benign cases that callers may treat as fail */
    if(mdb_rc == MDB_NOTFOUND || mdb_rc == MDB_KEYEXIST) return DB_LMDB_SAFE_FAIL;

    /* Retryable classes */
    switch(mdb_rc)
    {
        case MDB_MAP_RESIZED:
        case MDB_PAGE_FULL:
        case MDB_TXN_FULL:
        case MDB_CURSOR_FULL:
        case MDB_BAD_RSLOT:
        case MDB_READERS_FULL:
            break;                                  /* go to retry */
        case MDB_MAP_FULL:
            if(db_env_mapsize_expand() == 0) break; /* then retry */
            /* fallthrough to fail if resize impossible */
        default:
            goto fatal;
    }

    /* Check retry budget */
    if(!retry_budget || *retry_budget == 0)
    {
        EML_ERROR(LOG_TAG, "_decide: retry budget exhausted");
        return DB_LMDB_SAFE_FAIL;
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

int db_lmdb_txn_begin_safe(MDB_env* env, unsigned flags, MDB_txn** out_txn, size_t* retry_budget,
                           int* out_err)
{
    /* Check input */
    if(!env || !out_txn) return DB_LMDB_SAFE_FAIL;

retry:
{
    int res = mdb_txn_begin(env, NULL, flags, out_txn);
    int act = db_lmdb_safety_decide(res, 0, retry_budget, out_err, out_txn);
    if(act == DB_LMDB_SAFE_OK) return DB_LMDB_SAFE_OK;
    if(act == DB_LMDB_SAFE_RETRY) goto retry;
    return DB_LMDB_SAFE_FAIL;
}
}

int db_lmdb_txn_commit_safe(MDB_txn* txn, size_t* retry_budget, int* out_err)
{
    /* Check input */
    if(!txn) return DB_LMDB_SAFE_FAIL;

retry:
{
    int res = mdb_txn_commit(txn);
    int act = db_lmdb_safety_decide(res, 1, retry_budget, out_err, NULL);
    if(act == DB_LMDB_SAFE_OK) return DB_LMDB_SAFE_OK;
    if(act == DB_LMDB_SAFE_RETRY) goto retry;
    return DB_LMDB_SAFE_FAIL;
}
}

int db_lmdb_get_db_flags(MDB_txn* txn, MDB_dbi dbi, unsigned int* out_flags, size_t* retry_budget,
                  int* out_err)
{
    if(!txn || !out_flags)
    {
        EML_ERROR(LOG_TAG, "db_lmdb_flags: invalid input (txn=%p out_flags=%p)", (void*)txn,
                  (void*)out_flags);
    }

retry:
{
    int res = mdb_dbi_flags(txn, dbi, out_flags);
    int act = db_lmdb_safety_decide(res, 0, retry_budget, out_err, NULL);
    if(act == DB_LMDB_SAFE_OK) return 0;
    if(act == DB_LMDB_SAFE_RETRY) goto retry;
    return DB_LMDB_SAFE_FAIL;
}
}
