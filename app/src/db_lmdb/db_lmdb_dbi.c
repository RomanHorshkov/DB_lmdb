/**
 * @file db_lmdb_dbi.c
 */

#include "db_lmdb_dbi.h"
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
static inline unsigned dbi_desc_open_flags(dbi_type_t type)
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
 * @return 0 on success, negative errno on failure.
 */
static int _dbi_desc_init_one(MDB_txn* txn, const dbi_decl_t* decl, unsigned put_flags_default,
                             dbi_desc_t* out);


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

    if (n_dbis == 0 || n_dbis > DB_MAX_DBIS)
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

    MDB_txn* txn = NULL;
retry:
{
    /* Begin transaction */
    switch(db_lmdb_txn_begin_safe(DB->env, 0, &txn, &res))
    {
        case DB_LMDB_SAFE_OK:
            break;
        case DB_LMDB_SAFE_RETRY:
            goto retry;
        default:
            goto fail;
    }

    for(size_t i = 0; i < n_dbis; ++i)
    {
        res = _dbi_desc_init_one(txn, &dbis[i], DBI_PUT_FLAGS_AUTO, &DB->dbis[i]);
        if(res != 0)
        {
            LMDB_LOG_ERR(LOG_TAG, "db_lmdb_dbi_init:desc_init failed", res);
            goto fail;
        }
    }

    res = mdb_txn_commit(txn);
    if(res != MDB_SUCCESS)
    {
        LMDB_LOG_ERR(LOG_TAG, "db_lmdb_dbi_init:txn_commit failed", res);
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

static int _dbi_desc_init_one(MDB_txn* txn, const dbi_decl_t* decl, unsigned put_flags_default,
                             dbi_desc_t* out)
{
    if(!txn || !decl || !decl->name || !out) return -EINVAL;

    MDB_dbi  dbi        = 0;
    unsigned open_flags = dbi_desc_open_flags(decl->type);
    int      rc         = mdb_dbi_open(txn, decl->name, open_flags, &dbi);
    if(rc != MDB_SUCCESS)
    {
        LMDB_LOG_ERR(LOG_TAG, "_dbi_desc_init_one:mdb_dbi_open", rc);
        return db_map_mdb_err(rc);
    }

    unsigned db_flags = 0;
    rc                = mdb_dbi_flags(txn, dbi, &db_flags);
    if(rc != MDB_SUCCESS)
    {
        LMDB_LOG_ERR(LOG_TAG, "_dbi_desc_init_one:mdb_dbi_flags", rc);
        return db_map_mdb_err(rc);
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

    return 0;
}
