

#include "common.h"     /* EMlog, config */

#include "db.h"         /* DB, DBI, lmdb */
#include "operations.h" /* DB_operation_t etc */
#include "security.h"   /* security_check */

#include <lmdb.h>

/************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "ops_cre"

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

db_security_ret_code_t _db_create_env(int* const out_err);

db_security_ret_code_t _db_set_max_dbis(const unsigned int max_dbis, int* const out_err);

db_security_ret_code_t _db_set_map_size(const size_t db_map_size, int* const out_err);

// @param[in] mode The UNIX permissions to set on created files and semaphores.
db_security_ret_code_t _db_open_env(const char* const path, const unsigned int mode,
                                       int* const out_err);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

db_security_ret_code_t ops_txn_begin(MDB_txn** out_txn, const unsigned flags, int* const out_err)
{
    /* Check input */
    if(!DataBase || !DataBase->env || !out_txn)
    {
        EML_ERROR(LOG_TAG, "_txn_begin: invalid input");
        return DB_SAFETY_FAIL;
    }

    /* loosing const correctness */
    int mdb_res = mdb_txn_begin(DataBase->env, NULL, flags, out_txn);

    /* keep this light check to avoid jumping into 
    security_check on hot path */
    if(mdb_res != 0)
    {
        return security_check(mdb_res, out_err);
    }

    return DB_SAFETY_OK;
}

db_security_ret_code_t ops_txn_commit(MDB_txn* const txn, int* const out_err)
{
    /* Check input */
    if(!txn)
    {
        EML_ERROR(LOG_TAG, "_txn_commit: invalid input (txn=NULL)");
        return DB_SAFETY_FAIL;
    }

    int mdb_res = mdb_txn_commit(txn);
    /* keep this light check to avoid jumping into 
    security_check on hot path */
    if(mdb_res != 0)
    {
        return security_check(mdb_res, out_err);
    }
    return DB_SAFETY_OK;
}


db_security_ret_code_t ops_exec_create_env(const unsigned int max_dbis, const size_t db_map_size, const char* const path,
                    const unsigned int mode, int* const out_err)
{
    /* Create environment */
    switch(_db_create_env(out_err))
    {
        case DB_SAFETY_OK:
            break;
        default:
            EML_ERROR(LOG_TAG, "ops_exec_create_env: _db_create_env failed");
            return DB_SAFETY_FAIL;
    }

    /* Set max DBIs */
    switch(_db_set_max_dbis(max_dbis, out_err))
    {
        case DB_SAFETY_OK:
            break;
        default:
            EML_ERROR(LOG_TAG, "ops_exec_create_env: _db_set_max_dbis failed");
            return DB_SAFETY_FAIL;
    }

    /* Set map size */
    switch(_db_set_map_size(db_map_size, out_err))
    {
        case DB_SAFETY_OK:
            break;
        default:
            EML_ERROR(LOG_TAG, "ops_exec_create_env: _db_set_map_size failed");
            return DB_SAFETY_FAIL;
    }
    
    /* Open environment */
    switch(_db_open_env(path, mode, out_err))
    {
        case DB_SAFETY_OK:
            break;
        default:
            EML_ERROR(LOG_TAG, "ops_exec_create_env: _db_open_env failed");
            return DB_SAFETY_FAIL;
    }

    return DB_SAFETY_OK;
}


/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

db_security_ret_code_t _db_create_env(int* const out_err)
{
    /* Create environment */
    int mdb_res = mdb_env_create(&DataBase->env);
    if(mdb_res != 0) goto fail;
    return 0;

fail:
    if(DataBase->env)
    {
        mdb_env_close(DataBase->env);
        DataBase->env = NULL;
    }
    LMDB_EML_ERR(LOG_TAG, "_create_env: mdb_env_create failed", mdb_res);
    return security_check(mdb_res, out_err);
}

db_security_ret_code_t _db_set_max_dbis(const unsigned int max_dbis, int* const out_err)
{
    /* Check input */
    if(max_dbis == 0)
    {
        EML_ERROR(LOG_TAG, "_set_max_dbis: max_dbis cannot be zero");
        return -EINVAL;
    }

    /* Set max sub-dbis */
    int mdb_res = mdb_env_set_maxdbs(DataBase->env, (MDB_dbi)max_dbis);
    if(mdb_res != 0) goto fail;
    return 0;

fail:
    LMDB_EML_ERR(LOG_TAG, "_set_max_dbis failed", mdb_res);
    return security_check(mdb_res, out_err);
}

db_security_ret_code_t _db_set_map_size(const size_t db_map_size, int* const out_err)
{
    /* Check input */
    if(db_map_size == 0)
    {
        EML_ERROR(LOG_TAG, "_set_map_size: db_map_size cannot be zero");
        return -EINVAL;
    }

    /* Set initial map size */
    int mdb_res = mdb_env_set_mapsize(DataBase->env, db_map_size);
    if(mdb_res != 0) goto fail;
    return 0;

fail:
    LMDB_EML_ERR(LOG_TAG, "_set_map_size failed", mdb_res);
    return security_check(mdb_res, out_err);
}

// @param[in] mode The UNIX permissions to set on created files and semaphores.
db_security_ret_code_t _db_open_env(const char* const path, const unsigned int mode,
                                       int* const out_err)
{
    /* Open environment */
    int mdb_res = mdb_env_open(DataBase->env, path, 0, mode);
    if(mdb_res != 0) goto fail;
    return 0;

fail:
    LMDB_EML_ERR(LOG_TAG, "_open_env failed", mdb_res);
    return security_check(mdb_res, out_err);
}
