/**
 * @file db_lmdb_dbi.c
 * 
 * @brief LMDB DBI management functions.
 * 
 * 
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025
 * (c) 2025
 */

#include "db.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "db_lmdb_dbi"

/************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */

/************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Initialize a single DBI descriptor from a declaration.
 * 
 * @param txn                   LMDB transaction.
 * @param decl                  Declaration to initialize from.
 * @param put_flags_default     Default put flags (or DBI_PUT_FLAGS_AUTO).
 * @param out                   Filled with initialized descriptor on success.
 * @param open_retry_budget     Retry budget for opening DBIs.
 * @param flags_retry_budget    Retry budget for fetching DBI flags.
 * @param out_err               Optional mapped errno on failure.
 * @return DB_SAFETY_OK/RETRY/FAIL
 */
static int _dbi_init_one(MDB_txn* txn, const dbi_decl_t* decl, unsigned put_flags_default,
                         dbi_desc_t* out, size_t* open_retry_budget, size_t* flags_retry_budget,
                         int* out_err);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int db_lmdb_dbi_init(const dbi_decl_t* dbis, const size_t n_dbis)
{
    int res = -1;
    if(!_DB_is_ok())
    {
        EML_ERROR(LOG_TAG, "_init: DB or env not initialized");
        res = -EINVAL;
        goto fail;
    }

    if(n_dbis == 0 || n_dbis > DB_MAX_DBIS)
    {
        EML_ERROR(LOG_TAG, "_dbi_init: invalid n_dbis=%zu", n_dbis);
        res = -EINVAL;
        goto fail;
    }

    /* Set number of sub-dbis */
    DB->n_dbis = (unsigned int)n_dbis;

    /* Allocate sub-dbis */
    dbi_desc_t* new_dbis = calloc(n_dbis, sizeof(dbi_desc_t));
    if(!new_dbis)
    {
        res = -ENOMEM;
        goto fail;
    }

    size_t open_retry_budget  = DB_LMDB_RETRY_DBI_OPEN;
    size_t flags_retry_budget = DB_LMDB_RETRY_DBI_FLAGS;

    size_t txn_retry_budget = DB_LMDB_RETRY_TRANSACTION;

    MDB_txn* txn            = NULL;
    size_t   opened_dbi_idx = 0;
retry:
{
    /* Begin transaction */
    switch(db_lmdb_txn_begin_safe(DB->env, 0, &txn, &txn_retry_budget, &res))
    {
        case DB_SAFETY_OK:
            break;
        case DB_SAFETY_RETRY:
            goto retry;
        default:
            goto fail;
    }

    for(opened_dbi_idx; opened_dbi_idx < n_dbis; ++opened_dbi_idx)
    {
        int act =
            _dbi_init_one(txn, &dbis[opened_dbi_idx], DBI_PUT_FLAGS_AUTO, &DB->dbis[opened_dbi_idx],
                          &open_retry_budget, &flags_retry_budget, &res);

        switch(act)
        {
            case DB_SAFETY_OK:
                continue;
            case DB_SAFETY_RETRY:
                EML_WARN(LOG_TAG, "_dbi_init: retrying at dbi %zu", opened_dbi_idx);
                goto retry;
            default:
                EML_ERROR(LOG_TAG, "_dbi_init: failed at dbi %zu", opened_dbi_idx);
                goto fail;
        }
    }

    switch(db_lmdb_txn_commit_safe(txn, &txn_retry_budget, &res))
    {
        case DB_SAFETY_OK:
            break;
        case DB_SAFETY_RETRY:
            EML_WARN(LOG_TAG, "db_lmdb_dbi_init: retrying txn commit");
            goto retry;
        default:
            EML_PERR(LOG_TAG, "db_lmdb_dbi_init:txn_commit_safe failed with err %d", res);
            goto fail;
    }

    EML_DBG(LOG_TAG, "Successfully opened all sub dbis");
    return 0;
}

fail:
    /* map lmdb error if >0 else return negative errno */
    return (res > 0) ? db_map_mdb_err(res) : res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int _dbi_init_one(MDB_txn* txn, const dbi_decl_t* decl, unsigned put_flags_default,
                         dbi_desc_t* out, size_t* open_retry_budget, size_t* flags_retry_budget,
                         int* out_err)
{
    if(out_err) *out_err = 0;
    if(!txn || !decl || !decl->name || !out)
    {
        if(out_err) *out_err = -EINVAL;
        EML_ERROR(LOG_TAG, "_dbi_init_one: invalid input");
        return DB_SAFETY_FAIL;
    }

    MDB_dbi  dbi        = 0;
    unsigned open_flags = _dbi_open_flags(decl->type);
    int      mdb_rc     = MDB_SUCCESS;

    int act = db_lmdb_dbi_open_safe(txn, decl->name, open_flags, &dbi, open_retry_budget, &mdb_rc,
                                    out_err);
    if(act != DB_SAFETY_OK)
    {
        LMDB_EML_ERR(LOG_TAG, "_dbi_init_one:mdb_dbi_open", mdb_rc);
        return act;
    }

    unsigned db_flags = 0;
    act = db_lmdb_dbi_get_flags_safe(txn, dbi, &db_flags, flags_retry_budget, &mdb_rc, out_err);
    if(act != DB_SAFETY_OK)
    {
        LMDB_EML_ERR(LOG_TAG, "_dbi_init_one:mdb_dbi_flags", mdb_rc);
        return act;
    }

    out->dbi               = dbi;
    out->name              = decl->name;
    out->type              = decl->type;
    out->open_flags        = open_flags;
    out->db_flags          = db_flags;
    out->is_dupsort        = (db_flags & MDB_DUPSORT) != 0;
    out->is_dupfixed       = (db_flags & MDB_DUPFIXED) != 0;
    out->put_flags_default = (put_flags_default == DBI_PUT_FLAGS_AUTO)
                               ? dbi_desc_default_put_flags(db_flags)
                               : put_flags_default;

    return DB_SAFETY_OK;
}
