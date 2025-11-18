/**
 * @file db_lmdb_core.c
 */

#include "core.h"
#include "ops_exec.h"  /* ops_init_env etc */
#include "ops_setup.h" /* ops_init_dbi etc */

/************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG                 "db_core"

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

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int core_init_db(const char* const path, const unsigned int mode, dbi_init_t* init_dbis,
                 unsigned n_dbis, int* const out_err)
{
    if(!path)
    {
        if(out_err) *out_err = -EINVAL;
        EML_ERROR(LOG_TAG, "_init_db: invalid input (path=NULL)");
        return DB_SAFETY_FAIL;
    }

    switch(ops_init_env(DB_MAX_DBIS, (size_t)DB_MAP_SIZE_INIT, path, mode, out_err))
    {
        case DB_SAFETY_OK:
            break;
        default:
            EML_ERROR(LOG_TAG, "_init_db: _init_env failed");
            return DB_SAFETY_FAIL;
    }

    /* Init a transaction */
    MDB_txn* txn = NULL;

    switch(ops_txn_begin(&txn, 0, out_err))
    {
        case DB_SAFETY_OK:
            break;
        default:
            EML_ERROR(LOG_TAG, "_init_db: _txn_begin failed, err=%d", (out_err) ? *out_err : -1);
            return DB_SAFETY_FAIL;
    }

    /* Initialize all requested dbis */
    for(size_t i = 0; i < (size_t)n_dbis; i++)
    {
        if(!&init_dbis[i])
        {
            EML_ERROR(LOG_TAG, "_init_db: invalid dbi_init_t at index %zu", i);
            goto fail_txn;
        }

        dbi_init_t* init_dbi = &init_dbis[i];
        switch(ops_init_dbi(txn, init_dbi->name, init_dbi->dbi_idx, init_dbi->type, out_err))
        {
            case DB_SAFETY_OK:
                break;
            default:
                EML_ERROR(LOG_TAG, "_init_db: _init_dbi failed for dbi %s, err=%d", init_dbi->name,
                          (out_err) ? *out_err : -1);
                goto fail_txn;
        }
    }

    switch(ops_txn_commit(txn, out_err))
    {
        case DB_SAFETY_OK:
            break;
        default:
            EML_PERR(LOG_TAG, "_init_db: _txn_commit failed err=%d", *out_err);
            return DB_SAFETY_FAIL;
    }

    EML_INFO(LOG_TAG, "_init_db: database initialized with %u dbis at %s", n_dbis, path);
    return DB_SAFETY_OK;

fail_txn:
    if(txn) mdb_txn_abort(txn);
    return DB_SAFETY_FAIL;
}
