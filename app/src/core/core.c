/**
 * @file db_lmdb_core.c
 */

#include <errno.h>         /* EINVAL, ENOMEM, EALREADY */
#include <stdint.h>        /* uint8_t */
#include <stdlib.h>        /* calloc, free */

#include "common.h"        /* EML_* macros, LMDB_EML_* */
#include "core.h"
#include "db.h"            /* DataBase_t, MDB_envinfo */
#include "dbi_int.h"       /* dbi_t */
#include "ops_actions.h"   /* act_txn_begin, act_txn_commit */
#include "ops_exec.h"      /* ops_add_operation, ops_execute_operations */
#include "ops_facade.h"    /* DB_OPERATION_* */
#include "ops_init.h"      /* ops_init_env, ops_init_dbi */
#include "ops_internals.h" /* op_t, op_key_t, op_type_t */

/* Definition of the global DB handle declared in db.h */
DataBase_t* DataBase = NULL;

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

static int _prepare_op_key_and_val(op_t* op, const op_type_t type, const void* key_data,
                                   const size_t key_size, const void* val_data,
                                   const size_t val_size)
{
    /* In any case key_size has to be != 0 */
    if(key_size == 0)
    {
        EML_ERROR(LOG_TAG, "_prepare_op_key_and_val: invalid key size=0");
        return -EINVAL;
    }

    /* switch the operation type */
    switch(type)
    {
        case DB_OPERATION_PUT:
            /* Put operation needs as well a clear val */
            if(val_size == 0)
            {
                EML_ERROR(LOG_TAG, "_prepare_op_key_and_val: invalid val for PUT (size=0)");
                return -EINVAL;
            }

            /* put value have been given by user */
            if(val_data)
            {
                op->val.kind         = OP_KEY_KIND_PRESENT;
                op->val.present.ptr  = (void*)val_data;
                op->val.present.size = val_size;
            }
            else
            {
                op->val.kind            = OP_KEY_KIND_LOOKUP;
                op->val.lookup.op_index = val_size;
                /* TODO:
            HARDCODED PATH. PUT ALLOWS LOOKING JUST 
            FOR PREVIOUS -VAL- RESULT */
                op->val.lookup.src_type = OP_KEY_SRC_VAL;
            }

            /* Set the key */
            if(key_data)
            {
                op->key.kind         = OP_KEY_KIND_PRESENT;
                op->key.present.ptr  = (void*)key_data;
                op->key.present.size = key_size;
            }
            else
            {
                op->key.kind            = OP_KEY_KIND_LOOKUP;
                op->key.lookup.op_index = key_size;
                op->key.lookup.src_type = OP_KEY_SRC_KEY;
            }

            break;

        case DB_OPERATION_GET:
            /* Key resolution */
            /* Key is given by the user */
            if(key_data)
            {
                op->key.kind         = OP_KEY_KIND_PRESENT;
                op->key.present.ptr  = (void*)key_data;
                op->key.present.size = key_size;
            }
            /* Key is not given, has to be looked up */
            else
            {
                op->key.kind            = OP_KEY_KIND_LOOKUP;
                op->key.lookup.op_index = key_size;
                op->key.lookup.src_type = OP_KEY_SRC_KEY;
            }

            /* Value handling:
             * - When val_data is provided, treat it as a user buffer that
             *   act_get() should copy into (PRESENT).
             * - When val_data is NULL, this means "no user buffer"; act_get()
             *   will populate op->val with an LMDB-backed view (PRESENT) on
             *   success. */
            if(val_data)
            {
                if(val_size == 0)
                {
                    EML_ERROR(LOG_TAG,
                              "_prepare_op_key_and_val: invalid val_size=0 for GET with val_data");
                    return -EINVAL;
                }
                /* User provided buffer for GET result. */
                op->val.kind         = OP_KEY_KIND_PRESENT;
                op->val.present.size = val_size;
                op->val.present.ptr  = (void*)val_data;
            }
            else
            {
                /* No user buffer: let act_get() populate op->val with a
                 * PRESENT descriptor that points into LMDB-managed memory. */
                op->val.kind = OP_KEY_KIND_NONE;
            }

            break;

        default:
            EML_ERROR(LOG_TAG, "db_core_add_op: unsupported operation type=%d", type);
            return -EINVAL;
            break;
    }

    /* Set the operation type */
    op->type = type;
    return 0;
}

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int db_core_init(const char* const path, const unsigned int mode, const char* const* dbi_names,
                 const dbi_type_t* dbi_types, unsigned n_dbis)
{
    if(DataBase)
    {
        EML_ERROR(LOG_TAG, "_db_init: database already initialized");
        return -EALREADY;
    }

    /* Check input */
    if(!path || !(*path) || !dbi_names || !dbi_types || n_dbis == 0)
    {
        EML_ERROR(LOG_TAG, "_init_db: invalid input");
        return -EINVAL;
    }

    /* Prepare error output */
    int  out_err_val = -EINVAL;
    int* out_err     = &out_err_val;

    /* Allocate DB */
    DataBase_t* new_DataBase = NULL;
    new_DataBase             = calloc(1, sizeof(DataBase_t));
    if(!new_DataBase)
    {
        EML_ERROR(LOG_TAG, "_init_db: calloc(DataBase) failed");
        return -ENOMEM;
    }

    /* Set max map size */
    new_DataBase->map_size_bytes_max = (size_t)DB_MAP_SIZE_MAX;

    /* Allocate DBI descriptor array */
    new_DataBase->dbis = calloc(n_dbis, sizeof(dbi_t));
    if(!new_DataBase->dbis)
    {
        EML_ERROR(LOG_TAG, "_init_db: calloc(dbis) failed");
        free(new_DataBase);
        return -ENOMEM;
    }
    new_DataBase->n_dbis = n_dbis;

    /* Set the global handle */
    DataBase = new_DataBase;

    /* Do not allow retry on init */
    switch(ops_init_env(DB_MAX_DBIS, path, mode, out_err))
    {
        case DB_SAFETY_SUCCESS:
            break;
        default:
            EML_ERROR(LOG_TAG, "_init_db: _init_env failed, err=%d", out_err_val);
            goto fail;
    }

    /* Init a transaction */
    MDB_txn* txn = NULL;

    switch(act_txn_begin(&txn, 0, out_err))
    {
        case DB_SAFETY_SUCCESS:
            break;
        default:
            EML_ERROR(LOG_TAG, "_init_db: _txn_begin failed, err=%d", out_err_val);
            goto fail;
    }

    /* Initialize all requested DBIs */
    for(unsigned i = 0; i < n_dbis; ++i)
    {
        const char*      name = dbi_names[i];
        const dbi_type_t type = dbi_types[i];

        if(!name || !(*name))
        {
            EML_ERROR(LOG_TAG, "_init_db: invalid dbi name at index %u", i);
            out_err_val = -EINVAL;
            goto fail;
        }

        switch(ops_init_dbi(txn, name, i, type, out_err))
        {
            case DB_SAFETY_SUCCESS:
                break;
            default:
                EML_ERROR(LOG_TAG, "_init_db: _init_dbi failed for dbi %s, err=%d", name,
                          (out_err) ? *out_err : -1);
                goto fail;
        }
    }

    switch(act_txn_commit(txn, out_err))
    {
        case DB_SAFETY_SUCCESS:
            break;
        default:
            EML_ERROR(LOG_TAG, "_init_db: _txn_commit failed err=%d", *out_err);
            goto fail;
    }

    EML_INFO(
        LOG_TAG,
        "_init_db: database initialized with %u dbis, with initial size %llu and max size %llu",
        n_dbis, DB_MAP_SIZE_INIT, DB_MAP_SIZE_MAX);
    return 0;

fail:
    /* Best-effort cleanup on initialization failure. */
    (void)db_core_shutdown();
    return out_err_val;
}

int db_core_add_op(const unsigned dbi_idx, const op_type_t type, const void* key_data,
                   const size_t key_size, const void* val_data, const size_t val_size)
{
    /* Validate global DB and DBI index */
    if(!DataBase || !DataBase->dbis || dbi_idx >= DataBase->n_dbis)
    {
        EML_ERROR(LOG_TAG, "db_core_add_op: invalid db/dbi (db=%p idx=%u n_dbis=%zu)",
                  (void*)DataBase, dbi_idx, DataBase ? DataBase->n_dbis : 0);
        return -EINVAL;
    }

    /* Get nex op */
    op_t* op = ops_get_next_op();
    if(!op)
    {
        EML_ERROR(LOG_TAG, "db_core_add_op: ops_get_next_op failed");
        return -ENOMEM;
    }

    int res = _prepare_op_key_and_val(op, type, key_data, key_size, val_data, val_size);
    if(res != 0)
    {
        EML_ERROR(LOG_TAG, "db_core_add_op: _prepare_op_key_and_val failed");
        return res;
    }

    /* Assemble operation, shallow copied into ops cache */
    op->dbi  = dbi_idx;
    op->type = type;

    return ops_add_operation(op);
}

int db_core_exec_ops(void)
{
    int rc = ops_execute_operations();
    if(rc != 0)
    {
        EML_ERROR(LOG_TAG, "db_core_exec_ops: batch failed, rc=%d", rc);
    }
    return rc;
}

size_t db_core_shutdown(void)
{
    /* No database initialized: idempotent no-op. */
    if(!DataBase) return 0;

    size_t final_mapsize = 0;

    /* Best-effort: ask LMDB for the current mapsize. */
    if(DataBase->env)
    {
        EML_INFO(LOG_TAG, "_shutdown: starting LMDB env teardown");
        MDB_envinfo info;
        int         rc = mdb_env_info(DataBase->env, &info);
        if(rc == MDB_SUCCESS)
        {
            final_mapsize = (size_t)info.me_mapsize;
        }
        else
        {
            LMDB_EML_WARN(LOG_TAG, "_shutdown:mdb_env_info failed", rc);
            final_mapsize = 0;
        }

        /* Close any opened DBIs before closing the environment. */
        if(DataBase->dbis && DataBase->n_dbis > 0)
        {
            for(uint8_t i = 0; i < DataBase->n_dbis; ++i)
            {
                MDB_dbi handle = (MDB_dbi)DataBase->dbis[i].dbi;
                if(handle != (MDB_dbi)0)
                {
                    mdb_dbi_close(DataBase->env, handle);
                }
            }
        }

        /* Close LMDB environment (aborts any remaining txns). */
        mdb_env_close(DataBase->env);
        DataBase->env = NULL;
    }

    /* Free DBI descriptor array. */
    if(DataBase->dbis)
    {
        free(DataBase->dbis);
        DataBase->dbis = NULL;
    }
    DataBase->n_dbis             = 0;
    DataBase->map_size_bytes_max = 0;

    /* Free main DB structure and clear global. */
    free(DataBase);
    DataBase = NULL;

    EML_INFO(LOG_TAG, "_shutdown: database shut down, final mapsize=%zu", final_mapsize);

    return final_mapsize;
}
