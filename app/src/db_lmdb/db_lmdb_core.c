/**
 * @file db_lmdb_core.c
 */

#include "db_lmdb_core.h"
#include "db_lmdb_internal.h" /* interface, config, emlog */

/************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG                   "db_lmdb_core"

#define DB_LMDB_RETRY_TRANSACTION 2
#define DB_LMDB_RETRY_OPERATION   3
#define DB_LMDB_RETRY_GET_FLAGS   3

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
int _safety_check(int mdb_rc, int is_write_txn, uint8_t* retry_budget, int* out_mapped_err,
                  MDB_txn* txn);

/**
 * @brief Helper to get DBI flags with safety retry baked in.
 * 
 * @param txn       LMDB transaction.
 * @param dbi       LMDB DBI handle.
 * @param out_flags Filled with DBI flags on success.
 * @param out_err   Optional mapped errno on FAIL.
 * @return 0 on success, DB_LMDB_SAFE_FAIL on failure.
 */
static int _get_dbi_flags(MDB_txn* txn, MDB_dbi dbi, unsigned int* out_flags, int* out_err);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int db_lmdb_core_create_env_safe(struct DB* DataBase, const char* path, unsigned int max_dbis,
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

int db_lmdb_txn_begin_safe(MDB_env* env, unsigned flags, MDB_txn** out_txn, int* out_err)
{
    /* Check input */
    if(!env || !out_txn)
    {
        EML_ERROR(LOG_TAG, "txn_begin_safe: invalid input (env=%p out_txn=%p)", (void*)env,
                  (void*)out_txn);
        return DB_LMDB_SAFE_FAIL;
    }
    uint8_t retry_budget = DB_LMDB_RETRY_TRANSACTION;

retry:
{
    int res = mdb_txn_begin(env, NULL, flags, out_txn);
    int act = _safety_check(res, 0, &retry_budget, out_err, out_txn);
    if(act == DB_LMDB_SAFE_OK) return DB_LMDB_SAFE_OK;
    if(act == DB_LMDB_SAFE_RETRY) goto retry;
    return DB_LMDB_SAFE_FAIL;
}
}

int db_lmdb_txn_commit_safe(MDB_txn* txn, size_t* retry_budget, int* out_err)
{
    /* Check input */
    if(!txn)
    {
        EML_ERROR(LOG_TAG, "txn_commit_safe: invalid input (txn=NULL)");
        return DB_LMDB_SAFE_FAIL;
    }
    uint8_t retry_budget = DB_LMDB_RETRY_TRANSACTION;

retry:
{
    int res = mdb_txn_commit(txn);
    int act = _safety_check(res, 1, &retry_budget, out_err, NULL);
    if(act == DB_LMDB_SAFE_OK) return DB_LMDB_SAFE_OK;
    if(act == DB_LMDB_SAFE_RETRY) goto retry;
    return DB_LMDB_SAFE_FAIL;
}
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
int _safety_check(int mdb_rc, int is_write_txn, uint8_t* retry_budget, int* out_mapped_err,
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
    if(*retry_budget == 0)
    {
        EML_ERROR(LOG_TAG, "_decide: retry budget exhausted");
        return DB_LMDB_SAFE_FAIL;
    }
    else
    {
        EML_WARN(LOG_TAG, "_decide: retryable mdb_rc=%d, retry_budget=%zu", mdb_rc, retry_budget);
        *retry_budget--;
    }
    if(txn) mdb_txn_abort(txn);
    return DB_LMDB_SAFE_RETRY;

fatal:
    (void)is_write_txn; /* reserved for future use */
    if(txn) mdb_txn_abort(txn);
    return DB_LMDB_SAFE_FAIL;
}

static int _get_dbi_flags(MDB_txn* txn, MDB_dbi dbi, unsigned int* out_flags, int* out_err)
{
    if(!txn || !out_flags)
    {
        EML_ERROR(LOG_TAG, "db_lmdb_flags: invalid input (txn=%p out_flags=%p)", (void*)txn,
                  (void*)out_flags);
    }
    uint8_t retry_budget = DB_LMDB_RETRY_GET_FLAGS;

retry:
{
    int res = mdb_dbi_flags(txn, dbi, out_flags);
    int act = _safety_check(res, 0, &retry_budget, out_err, NULL);
    if(act == DB_LMDB_SAFE_OK) return 0;
    if(act == DB_LMDB_SAFE_RETRY) goto retry;
    return DB_LMDB_SAFE_FAIL;
}
}
