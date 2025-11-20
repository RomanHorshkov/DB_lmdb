
#include <errno.h>     /* EINVAL etc */
#include "ops_init.h"  /* env/DBI init helpers */
#include "common.h"    /* EML_* macros, LMDB_EML_* */

/************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "ops_init"

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

/**
 * @brief Create and initialize the database environment.
 *
 * This internal helper initializes the underlying database environment used
 * by the application. The function performs only the minimal setup required
 * for the environment to be used by other database operations.
 *
 * @param[out] out_err Optional pointer to an integer to receive a platform-
 * specific or implementation-specific error code when the operation fails.
 * If @p out_err is non-NULL *out_err will be set to an errno-style value on
 * failure, or left unchanged on success. The error code provides additional
 * context describing the underlying cause.
 *
 * @return db_security_ret_code_t value. On success returns DB_SAFETY_SUCCESS.
 * on failure it returns one of the error codes defined by
 * db_security_ret_code_t. Consult the enum definition.
 *
 * @note This function is part of the module's internal API (name begins with
 * an underscore) and may have side effects such as creating directories or
 * files and modifying global environment state. Callers must ensure it is
 * invoked in an appropriate context (for example, only once at startup
 * or while holding the required initialization lock) and avoid concurrent
 * calls from multiple threads unless external synchronization is provided.
 *
 * @warning If the function fails, the environment may be left in a partially
 * initialized state; callers should treat failure as non-recoverable unless
 * specific recovery steps are documented elsewhere.
 */
db_security_ret_code_t _db_create_env(int* const out_err);

/**
 * @brief Set the maximum number of sub-databases (DBIs) in the environment.
 *
 * This internal helper configures the maximum number of named sub-databases
 * (DBIs) that can be opened within the LMDB environment. This setting must be
 * applied before opening the environment and affects resource allocation.
 *
 * @param max_dbis Maximum number of sub-databases to allow (must be > 0).
 * @param[out] out_err Optional pointer to an integer to receive a platform-
 * specific or implementation-specific error code when the operation fails.
 * If @p out_err is non-NULL *out_err will be set to an errno-style value on
 * failure, or left unchanged on success. The error code provides additional
 * context describing the underlying cause.
 *
 * @return db_security_ret_code_t value. On success returns DB_SAFETY_SUCCESS.
 * on failure it returns one of the error codes defined by
 * db_security_ret_code_t. Consult the enum definition.
 *
 * @note This function is part of the module's internal API and may have side
 * effects such as creating directories or files and modifying global
 * environment state. Callers must ensure it is invoked in an appropriate
 * context (only once at startup) and avoid concurrent calls from multiple
 * threads unless external synchronization is provided.
 *
 * @warning If the function fails, the environment may be left in a partially
 * initialized state; callers should treat failure as non-recoverable unless
 * specific recovery steps are documented elsewhere.
 */
db_security_ret_code_t _db_set_max_dbis(const unsigned int max_dbis, int* const out_err);

/**
 * @brief Set the initial map size for the database environment.
 *
 * This internal helper configures the initial map size for the LMDB
 * environment. The map size determines the maximum size of the database
 * and must be set before opening the environment.
 *
 * @param db_map_size Initial map size in bytes (must be > 0).
 * @param[out] out_err Optional pointer to an integer to receive a platform-
 * specific or implementation-specific error code when the operation fails.
 * If @p out_err is non-NULL *out_err will be set to an errno-style value on
 * failure, or left unchanged on success. The error code provides additional
 * context describing the underlying cause.
 *
 * @return db_security_ret_code_t value. On success returns DB_SAFETY_SUCCESS.
 * on failure it returns one of the error codes defined by
 * db_security_ret_code_t. Consult the enum definition.
 *
 * @note This function is part of the module's internal API and may have side
 * effects such as creating directories or files and modifying global
 * environment state. Callers must ensure it is invoked in an appropriate
 * context (only once at startup) and avoid concurrent calls from multiple
 * threads unless external synchronization is provided.
 *
 * @warning If the function fails, the environment may be left in a partially
 * initialized state; callers should treat failure as non-recoverable unless
 * specific recovery steps are documented elsewhere.
 */
db_security_ret_code_t _db_set_map_size(int* const out_err);

/**
 * @brief Open the database environment at the specified path.
 *
 * This internal helper opens the LMDB environment located at the given
 * filesystem path. The function assumes that the environment has been
 * created and configured prior to this call.
 *
 * @param path Filesystem path to the database environment directory.
 * @param mode The UNIX permissions to set on created files and semaphores.
 * @param[out] out_err Optional pointer to an integer to receive a platform-
 * specific or implementation-specific error code when the operation fails.
 * If @p out_err is non-NULL *out_err will be set to an errno-style value on
 * failure, or left unchanged on success. The error code provides additional
 * context describing the underlying cause.
 *
 * @return db_security_ret_code_t value. On success returns DB_SAFETY_SUCCESS.
 * on failure it returns one of the error codes defined by
 * db_security_ret_code_t. Consult the enum definition.
 *
 * @note This function is part of the module's internal API and may have side
 * effects such as creating directories or files and modifying global
 * environment state. Callers must ensure it is invoked in an appropriate
 * context (only once at startup) and avoid concurrent calls from multiple
 * threads unless external synchronization is provided.
 *
 * @warning If the function fails, the environment may be left in a partially
 * initialized state; callers should treat failure as non-recoverable unless
 * specific recovery steps are documented elsewhere.
 */
db_security_ret_code_t _db_open_env(const char* const path, const unsigned int mode,
                                    int* const out_err);

/**
 * @brief Open a sub-database (DBI) within the environment.
 *
 * This internal helper opens a named sub-database (DBI) within the LMDB
 * environment using the specified transaction. The function assumes that
 * the environment and transaction are valid and that the DBI index is
 * within bounds.
 *
 * @param txn Active LMDB transaction.
 * @param dbi_idx Index of the DBI to open (must be < DataBase->n_dbis).
 * @param name Name of the sub-database to open.
 * @param open_flags Flags to use when opening the DBI (e.g., MDB_CREATE).
 * @param[out] out_err Optional pointer to an integer to receive a platform-
 * specific or implementation-specific error code when the operation fails.
 * If @p out_err is non-NULL *out_err will be set to an errno-style value on
 * failure, or left unchanged on success. The error code provides additional
 * context describing the underlying cause.
 *
 * @return db_security_ret_code_t value. On success returns DB_SAFETY_SUCCESS.
 * on failure it returns one of the error codes defined by
 * db_security_ret_code_t. Consult the enum definition.
 *
 * @note This function is part of the module's internal API and may have side
 * effects such as creating directories or files and modifying global
 * environment state. Callers must ensure it is invoked in an appropriate
 * context and avoid concurrent calls from multiple threads unless external
 * synchronization is provided.
 *
 * @warning If the function fails, the DBI may be left in a partially
 * initialized state; callers should treat failure as non-recoverable unless
 * specific recovery steps are documented elsewhere.
 */
db_security_ret_code_t _dbi_open(MDB_txn* const txn, const unsigned int dbi_idx,
                                 const char* const name, const unsigned int open_flags,
                                 int* const out_err);

/**
 * @brief Retrieve and cache the flags for a sub-database (DBI).
 * This internal helper fetches the flags associated with a previously
 * opened sub-database (DBI) and caches them in the corresponding DBI
 * descriptor within the global DataBase structure. The function assumes that
 * the environment and transaction are valid and that the DBI index is
 * within bounds.
 * @param txn Active LMDB transaction.
 * @param dbi_idx Index of the DBI to query (must be < DataBase->n_dbis).
 * @param[out] out_err Optional pointer to an integer to receive a platform-
 * specific or implementation-specific error code when the operation fails.
 * If @p out_err is non-NULL *out_err will be set to an errno-style value on
 * failure, or left unchanged on success. The error code provides additional
 * context describing the underlying cause.
 * @return db_security_ret_code_t value. On success returns DB_SAFETY_SUCCESS.
 * on failure it returns one of the error codes defined by
 * db_security_ret_code_t. Consult the enum definition.
 * @note This function is part of the module's internal API and may have side
 * effects such as creating directories or files and modifying global
 * environment state. Callers must ensure it is invoked in an appropriate
 * context and avoid concurrent calls from multiple threads unless external
 * synchronization is provided.
 * @warning If the function fails, the DBI flags may be left in an inconsistent
 * state; callers should treat failure as non-recoverable unless specific
 * recovery steps are documented elsewhere.
 */
db_security_ret_code_t _dbi_get_flags(MDB_txn* const txn, const unsigned int dbi_idx,
                                      int* const out_err);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
db_security_ret_code_t ops_init_env(const unsigned int max_dbis, const char* const path,
                                    const unsigned int mode, int* const out_err)
{
    EML_INFO(LOG_TAG, "ops_init_env: creating LMDB env (path=%s, mode=%o, max_dbis=%u)",
             path ? path : "(null)", mode, max_dbis);

    /* Do NOT allow retry at initialization */

    /* Create environment */
    switch(_db_create_env(out_err))
    {
        case DB_SAFETY_SUCCESS:
            EML_DBG(LOG_TAG, "ops_init_env: environment handle created");
            break;
        default:
            EML_ERROR(LOG_TAG, "_init_env: _create_env failed");
            return DB_SAFETY_FAIL;
    }

    /* Set max DBIs */
    switch(_db_set_max_dbis(max_dbis, out_err))
    {
        case DB_SAFETY_SUCCESS:
            EML_DBG(LOG_TAG, "ops_init_env: max DBIs set to %u", max_dbis);
            break;
        default:
            EML_ERROR(LOG_TAG, "_init_env: _set_max_dbis failed");
            return DB_SAFETY_FAIL;
    }

    /* Set map size */
    switch(_db_set_map_size(out_err))
    {
        case DB_SAFETY_SUCCESS:
            EML_DBG(LOG_TAG, "ops_init_env: initial map size set to %zu", (size_t)DB_MAP_SIZE_INIT);
            break;
        default:
            EML_ERROR(LOG_TAG, "_init_env: _set_map_size failed");
            return DB_SAFETY_FAIL;
    }

    /* Open environment */
    switch(_db_open_env(path, mode, out_err))
    {
        case DB_SAFETY_SUCCESS:
            EML_INFO(LOG_TAG, "ops_init_env: environment opened at %s", path);
            break;
        default:
            EML_ERROR(LOG_TAG, "_init_env: _open_env failed");
            return DB_SAFETY_FAIL;
    }

    return DB_SAFETY_SUCCESS;
}

db_security_ret_code_t ops_init_dbi(MDB_txn* const txn, const char* const name,
                                    unsigned int dbi_idx, dbi_type_t dbi_type, int* const out_err)
{
    if(!txn || dbi_idx >= DataBase->n_dbis)
    {
        if(out_err) *out_err = -EINVAL;
        EML_ERROR(LOG_TAG, "ops_init_dbi: invalid input");
        return DB_SAFETY_FAIL;
    }

    /* derive open flags */
    unsigned open_flags = dbi_open_flags_from_type(dbi_type);

    /* Open DBI */
    switch(_dbi_open(txn, dbi_idx, name, open_flags, out_err))
    {
        case DB_SAFETY_SUCCESS:
            EML_DBG(LOG_TAG, "ops_init_dbi: opened DBI[%u] \"%s\" (flags=0x%x)", dbi_idx, name,
                    open_flags);
            break;
        default:
            EML_ERROR(LOG_TAG, "ops_init_dbi: _dbi_open failed");
            return DB_SAFETY_FAIL;
    }

    /* set db_flags */
    switch(_dbi_get_flags(txn, dbi_idx, out_err))
    {
        case DB_SAFETY_SUCCESS:
            break;
        default:
            EML_ERROR(LOG_TAG, "ops_init_dbi: _dbi_open failed");
            return DB_SAFETY_FAIL;
    }

    /* Get the indexed dbi */
    dbi_t* dbi = &DataBase->dbis[dbi_idx];

    // /* set type */
    // dbi->type = dbi_type;
    // /* derive open flags */
    // dbi->open_flags = open_flags;

    /* derive put flags */
    dbi->put_flags = dbi_desc_default_put_flags(dbi->db_flags);

    /* derive dupfix and dupsort flags */
    dbi->is_dupsort  = (dbi->db_flags & MDB_DUPSORT) != 0;
    dbi->is_dupfixed = (dbi->db_flags & MDB_DUPFIXED) != 0;

    EML_INFO(LOG_TAG,
             "ops_init_dbi: DBI[%u] \"%s\" ready (db_flags=0x%x dupsort=%u dupfixed=%u)",
             dbi_idx, name, dbi->db_flags, dbi->is_dupsort, dbi->is_dupfixed);

    return DB_SAFETY_SUCCESS;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
db_security_ret_code_t _db_create_env(int* const out_err)
{
    /* Check database existence */
    if(!DataBase)
    {
        if(out_err) *out_err = -EIO;
        EML_ERROR(LOG_TAG, "_create_env: DataBase is NULL");
        return DB_SAFETY_FAIL;
    }

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
    return security_check(mdb_res, NULL, out_err);
}

db_security_ret_code_t _db_set_max_dbis(const unsigned int max_dbis, int* const out_err)
{
    /* Check input */
    if(max_dbis == 0)
    {
        if(out_err) *out_err = -EINVAL;
        EML_ERROR(LOG_TAG, "_set_max_dbis: max_dbis cannot be zero");
        return DB_SAFETY_FAIL;
    }

    /* Set max sub-dbis */
    int mdb_res = mdb_env_set_maxdbs(DataBase->env, (MDB_dbi)max_dbis);
    if(mdb_res != 0) goto fail;
    return 0;

fail:
    LMDB_EML_ERR(LOG_TAG, "_set_max_dbis failed", mdb_res);
    return security_check(mdb_res, NULL, out_err);
}

db_security_ret_code_t _db_set_map_size(int* const out_err)
{
    /* Set initial map size */
    int mdb_res = mdb_env_set_mapsize(DataBase->env, DB_MAP_SIZE_INIT);
    if(mdb_res != 0) goto fail;
    return 0;

fail:
    LMDB_EML_ERR(LOG_TAG, "_set_map_size failed", mdb_res);
    return security_check(mdb_res, NULL, out_err);
}

db_security_ret_code_t _db_open_env(const char* const path, const unsigned int mode,
                                    int* const out_err)
{
    if(!path)
    {
        if(out_err) *out_err = -EINVAL;
        EML_ERROR(LOG_TAG, "_open_env: invalid input (path=NULL)");
        return DB_SAFETY_FAIL;
    }

    /* Open environment */
    int mdb_res = mdb_env_open(DataBase->env, path, 0, mode);
    if(mdb_res != 0) goto fail;
    return 0;

fail:
    LMDB_EML_ERR(LOG_TAG, "_open_env failed", mdb_res);
    return security_check(mdb_res, NULL, out_err);
}

db_security_ret_code_t _dbi_open(MDB_txn* const txn, const unsigned int dbi_idx,
                                 const char* const name, const unsigned int open_flags,
                                 int* const out_err)
{
    if(!txn || !name)
    {
        if(out_err) *out_err = -EINVAL;
        EML_ERROR(LOG_TAG, "_dbi_open: invalid input");
        return DB_SAFETY_FAIL;
    }

    /* Open DBI */
    int mdb_res = mdb_dbi_open(txn, name, open_flags, (MDB_dbi*)&DataBase->dbis[dbi_idx].dbi);
    if(mdb_res != 0) goto fail;

    return 0;

fail:
    LMDB_EML_ERR(LOG_TAG, "_dbi_open failed", mdb_res);
    return security_check(mdb_res, txn, out_err);
}

db_security_ret_code_t _dbi_get_flags(MDB_txn* const txn, const unsigned int dbi_idx,
                                      int* const out_err)
{
    if(!txn)
    {
        if(out_err) *out_err = -EINVAL;
        EML_ERROR(LOG_TAG, "_dbi_get_flags: invalid input");
        return DB_SAFETY_FAIL;
    }

    /* Get DBI flags */
    int mdb_res =
        mdb_dbi_flags(txn, DataBase->dbis[dbi_idx].dbi, &DataBase->dbis[dbi_idx].db_flags);
    if(mdb_res != 0) goto fail;

    return 0;
fail:
    LMDB_EML_ERR(LOG_TAG, "_dbi_get_flags failed", mdb_res);
    return security_check(mdb_res, txn, out_err);
}
