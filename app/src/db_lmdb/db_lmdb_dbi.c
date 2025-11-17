/**
 * @file db_lmdb_dbi.c
 */

#include "db_lmdb_dbi.h"      /* db_lmdb_dbi_*, dbi_desc_t */
#include "db_lmdb_core.h"
#include "db_lmdb_internal.h" /* interface, config, emlog */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "db_lmdb_dbi"

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
 * @brief Compute the LMDB open flags for a given @p type.
 *
 * This helper always includes MDB_CREATE so callers can rely on the flag
 * set enabling creation of the named DB when opening via `mdb_dbi_open`.
 *
 * @param type Logical DB type from `dbi_type_t`.
 * @return A bitmask of MDB_* flags appropriate for `mdb_dbi_open`.
 */
static inline unsigned _dbi_open_flags(dbi_type_t type)
{
    unsigned flags = MDB_CREATE;
    if(type & DBI_TYPE_DUPSORT) flags |= MDB_DUPSORT;
    if(type & DBI_TYPE_DUPFIXED) flags |= MDB_DUPFIXED;
    return flags;
}

/**
 * @brief Choose a safe default set of mdb_put flags from cached DB flags.
 *
 * This function returns MDB_NODUPDATA when the DB is a DUPSORT database
 * (to enforce unique duplicates), or MDB_NOOVERWRITE for non-DUPSORT DBs
 * (to enforce unique keys).
 *
 * @param db_flags Cached flags (as returned by `mdb_dbi_flags`).
 * @return MDB_NODUPDATA or MDB_NOOVERWRITE depending on @p db_flags.
 */
static inline unsigned dbi_desc_default_put_flags(unsigned db_flags)
{
    return (db_flags & MDB_DUPSORT) ? MDB_NODUPDATA : MDB_NOOVERWRITE;
}

static inline int _DB_is_ok()
{
    return (DB && DB->env) ? 1 : 0;
}

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
 * @return DB_LMDB_SAFE_OK/RETRY/FAIL
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
        case DB_LMDB_SAFE_OK:
            break;
        case DB_LMDB_SAFE_RETRY:
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
            case DB_LMDB_SAFE_OK:
                continue;
            case DB_LMDB_SAFE_RETRY:
                EML_WARN(LOG_TAG, "_dbi_init: retrying at dbi %zu", opened_dbi_idx);
                goto retry;
            default:
                EML_ERROR(LOG_TAG, "_dbi_init: failed at dbi %zu", opened_dbi_idx);
                goto fail;
        }
    }

    switch(db_lmdb_txn_commit_safe(txn, &txn_retry_budget, &res))
    {
        case DB_LMDB_SAFE_OK:
            break;
        case DB_LMDB_SAFE_RETRY:
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
        return DB_LMDB_SAFE_FAIL;
    }

    MDB_dbi  dbi        = 0;
    unsigned open_flags = _dbi_open_flags(decl->type);
    int      mdb_rc     = MDB_SUCCESS;

    int act = db_lmdb_dbi_open_safe(txn, decl->name, open_flags, &dbi, open_retry_budget, &mdb_rc,
                                    out_err);
    if(act != DB_LMDB_SAFE_OK)
    {
        LMDB_LOG_ERR(LOG_TAG, "_dbi_init_one:mdb_dbi_open", mdb_rc);
        return act;
    }

    unsigned db_flags = 0;
    act = db_lmdb_dbi_get_flags_safe(txn, dbi, &db_flags, flags_retry_budget, &mdb_rc, out_err);
    if(act != DB_LMDB_SAFE_OK)
    {
        LMDB_LOG_ERR(LOG_TAG, "_dbi_init_one:mdb_dbi_flags", mdb_rc);
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

    return DB_LMDB_SAFE_OK;
}
