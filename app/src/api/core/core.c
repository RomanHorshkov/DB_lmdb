/**
 * @file db_lmdb_core.c
 */

#include "core.h"
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

int db_lmdb_dbi_open_safe(MDB_txn* txn, const char* name, unsigned int open_flags, MDB_dbi* out_dbi,
                          size_t* retry_budget, int* out_mdb_rc, int* out_err)
{
    if(out_err) *out_err = 0;
    if(out_mdb_rc) *out_mdb_rc = MDB_SUCCESS;

    if(!txn || !name || !out_dbi)
    {
        if(out_err) *out_err = -EINVAL;
        return DB_SAFETY_FAIL;
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
        return DB_SAFETY_FAIL;
    }

    size_t budget = retry_budget ? *retry_budget : DB_LMDB_RETRY_DBI_FLAGS;
    int    res    = mdb_dbi_flags(txn, dbi, out_flags);
    if(out_mdb_rc) *out_mdb_rc = res;

    int act = _safety_check(res, 0, &budget, out_err, txn);
    if(retry_budget) *retry_budget = budget;
    return act;
}

int db_lmdb_put_safe(MDB_txn* txn, MDB_dbi dbi, MDB_val* key, MDB_val* data, unsigned int flags,
                     size_t* retry_budget, int* out_mdb_rc, int* out_err)
{
    if(out_err) *out_err = 0;
    if(out_mdb_rc) *out_mdb_rc = MDB_SUCCESS;

    if(!txn || !key || !data)
    {
        if(out_err) *out_err = -EINVAL;
        return DB_SAFETY_FAIL;
    }

    size_t budget = retry_budget ? *retry_budget : DB_LMDB_RETRY_OPERATION;

retry:
{
    int res = mdb_put(txn, dbi, key, data, flags);
    if(out_mdb_rc) *out_mdb_rc = res;
    if(res == MDB_SUCCESS)
    {
        if(retry_budget) *retry_budget = budget;
        return DB_SAFETY_OK;
    }

    int act = _safety_check(res, 1, &budget, out_err, txn);
    switch(act)
    {
        case DB_SAFETY_RETRY:
            if(retry_budget) *retry_budget = budget;
            EML_WARN(LOG_TAG, "put_safe: retrying, retry_budget=%zu", budget);
            goto retry;
        case DB_SAFETY_OK: /* should not happen */
            EML_WARN(LOG_TAG, "put_safe: unexpected SAFE_OK");
            break;
        default:
            break;
    }
    return act;
}
}

int db_lmdb_get_safe(MDB_txn* txn, MDB_dbi dbi, MDB_val* key, MDB_val* data, size_t* retry_budget,
                     int* out_mdb_rc, int* out_err)
{
    if(out_err) *out_err = 0;
    if(out_mdb_rc) *out_mdb_rc = MDB_SUCCESS;

    if(!txn || !key || !data)
    {
        if(out_err) *out_err = -EINVAL;
        return DB_SAFETY_FAIL;
    }

    size_t budget = retry_budget ? *retry_budget : DB_LMDB_RETRY_OPERATION;

retry:
{
    int res = mdb_get(txn, dbi, key, data);
    if(out_mdb_rc) *out_mdb_rc = res;
    if(res == MDB_SUCCESS)
    {
        if(retry_budget) *retry_budget = budget;
        return DB_SAFETY_OK;
    }

    int act = _safety_check(res, 0, &budget, out_err, txn);
    switch(act)
    {
        case DB_SAFETY_RETRY:
            if(retry_budget) *retry_budget = budget;
            EML_WARN(LOG_TAG, "get_safe: retrying, retry_budget=%zu", budget);
            goto retry;
        case DB_SAFETY_OK: /* should not happen */
            EML_WARN(LOG_TAG, "get_safe: unexpected SAFE_OK");
            break;
        default:
            break;
    }
    return act;
}
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
