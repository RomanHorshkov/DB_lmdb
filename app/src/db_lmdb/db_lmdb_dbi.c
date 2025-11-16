/**
 * @file db_lmdb_dbi.c
 */

#include "db_lmdb_dbi.h"

#include "db_lmdb_internal.h" /* db_map_mdb_err, emlog */

#define LOG_TAG "lmdb_dbi"

int dbi_desc_init(MDB_txn* txn, const char* name, unsigned open_flags, unsigned put_flags_default,
                  dbi_desc_t* out)
{
    if(!txn || !name || !out) return -EINVAL;

    MDB_dbi dbi = 0;
    int     rc  = mdb_dbi_open(txn, name, open_flags, &dbi);
    if(rc != MDB_SUCCESS)
    {
        LMDB_LOG_ERR(LOG_TAG, "dbi_desc_init:mdb_dbi_open", rc);
        return db_map_mdb_err(rc);
    }

    unsigned db_flags = 0;
    rc                = mdb_dbi_flags(txn, dbi, &db_flags);
    if(rc != MDB_SUCCESS)
    {
        LMDB_LOG_ERR(LOG_TAG, "dbi_desc_init:mdb_dbi_flags", rc);
        return db_map_mdb_err(rc);
    }

    out->dbi               = dbi;
    out->name              = name;
    out->open_flags        = open_flags;
    out->db_flags          = db_flags;
    out->is_dupsort        = (db_flags & MDB_DUPSORT) != 0;
    out->is_dupfixed       = (db_flags & MDB_DUPFIXED) != 0;
    out->put_flags_default = (put_flags_default == DBI_PUT_FLAGS_AUTO)
                               ? dbi_desc_default_put_flags(db_flags)
                               : put_flags_default;

    return 0;
}
