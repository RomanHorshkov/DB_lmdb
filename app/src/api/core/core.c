/**
 * @file db_lmdb_core.c
 */

#include "core.h"
#include "db_lmdb_internal.h" /* interface, config, emlog */

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

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
