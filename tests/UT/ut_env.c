#include "tests/UT/ut_env.h"

#include <stdarg.h>

/* Global DB handle expected by core code. */
DataBase_t* DataBase = NULL;

/* Hook points used by tests to customize LMDB behavior. */
ut_mdb_env_info_fn         g_ut_mdb_env_info         = NULL;
ut_mdb_env_set_mapsize_fn  g_ut_mdb_env_set_mapsize  = NULL;
ut_mdb_txn_abort_fn        g_ut_mdb_txn_abort        = NULL;
ut_mdb_env_create_fn       g_ut_mdb_env_create       = NULL;
ut_mdb_env_close_fn        g_ut_mdb_env_close        = NULL;
ut_mdb_env_set_maxdbs_fn   g_ut_mdb_env_set_maxdbs   = NULL;
ut_mdb_env_open_fn         g_ut_mdb_env_open         = NULL;
ut_mdb_dbi_open_fn         g_ut_mdb_dbi_open         = NULL;
ut_mdb_dbi_flags_fn        g_ut_mdb_dbi_flags        = NULL;
ut_mdb_txn_begin_fn        g_ut_mdb_txn_begin        = NULL;
ut_mdb_txn_commit_fn       g_ut_mdb_txn_commit       = NULL;
ut_mdb_put_fn              g_ut_mdb_put              = NULL;
ut_mdb_get_fn              g_ut_mdb_get              = NULL;

void ut_reset_lmdb_stubs(void)
{
    g_ut_mdb_env_info         = NULL;
    g_ut_mdb_env_set_mapsize  = NULL;
    g_ut_mdb_txn_abort        = NULL;
    g_ut_mdb_env_create       = NULL;
    g_ut_mdb_env_close        = NULL;
    g_ut_mdb_env_set_maxdbs   = NULL;
    g_ut_mdb_env_open         = NULL;
    g_ut_mdb_dbi_open         = NULL;
    g_ut_mdb_dbi_flags        = NULL;
    g_ut_mdb_txn_begin        = NULL;
    g_ut_mdb_txn_commit       = NULL;
    g_ut_mdb_put              = NULL;
    g_ut_mdb_get              = NULL;
}

/* ------------------------------------------------------------------------- */
/* EMlog stubs                                                               */
/* ------------------------------------------------------------------------- */

void emlog_log(eml_level_t level, const char* comp, const char* fmt, ...)
{
    (void)level;
    (void)comp;
    (void)fmt;
}

void emlog_log_errno(eml_level_t level, const char* comp, int err, const char* fmt, ...)
{
    (void)level;
    (void)comp;
    (void)err;
    (void)fmt;
}

/* ------------------------------------------------------------------------- */
/* LMDB stubs                                                                */
/* ------------------------------------------------------------------------- */

char* mdb_strerror(int err)
{
    (void)err;
    return "mocked mdb_strerror";
}

void mdb_txn_abort(MDB_txn* txn)
{
    if(g_ut_mdb_txn_abort)
    {
        g_ut_mdb_txn_abort(txn);
        return;
    }

    (void)txn;
}

int mdb_env_info(MDB_env* env, MDB_envinfo* stat)
{
    if(g_ut_mdb_env_info)
    {
        return g_ut_mdb_env_info(env, stat);
    }

    (void)env;
    if(stat != NULL)
    {
        /* Provide a deterministic, non-zero mapsize for any tests that
         * exercise mapsize expansion code paths. */
        stat->me_mapsize = 4096;
    }
    return MDB_SUCCESS;
}

int mdb_env_set_mapsize(MDB_env* env, size_t size)
{
    if(g_ut_mdb_env_set_mapsize)
    {
        return g_ut_mdb_env_set_mapsize(env, size);
    }

    (void)env;
    (void)size;
    return MDB_SUCCESS;
}

int mdb_env_create(MDB_env** env)
{
    if(g_ut_mdb_env_create)
    {
        return g_ut_mdb_env_create(env);
    }

    if(env)
    {
        /* Non-NULL sentinel to mimic a created environment handle. */
        *env = (MDB_env*)0x1;
    }
    return MDB_SUCCESS;
}

void mdb_env_close(MDB_env* env)
{
    if(g_ut_mdb_env_close)
    {
        g_ut_mdb_env_close(env);
        return;
    }

    (void)env;
}

int mdb_env_set_maxdbs(MDB_env* env, MDB_dbi dbs)
{
    if(g_ut_mdb_env_set_maxdbs)
    {
        return g_ut_mdb_env_set_maxdbs(env, dbs);
    }

    (void)env;
    (void)dbs;
    return MDB_SUCCESS;
}

int mdb_env_open(MDB_env* env, const char* path, unsigned int flags, mdb_mode_t mode)
{
    if(g_ut_mdb_env_open)
    {
        return g_ut_mdb_env_open(env, path, flags, mode);
    }

    (void)env;
    (void)path;
    (void)flags;
    (void)mode;
    return MDB_SUCCESS;
}

int mdb_dbi_open(MDB_txn* txn, const char* name, unsigned int flags, MDB_dbi* dbi)
{
    if(g_ut_mdb_dbi_open)
    {
        return g_ut_mdb_dbi_open(txn, name, flags, dbi);
    }

    (void)txn;
    (void)name;
    (void)flags;
    if(dbi)
    {
        *dbi = (MDB_dbi)1;
    }
    return MDB_SUCCESS;
}

int mdb_dbi_flags(MDB_txn* txn, MDB_dbi dbi, unsigned int* flags)
{
    if(g_ut_mdb_dbi_flags)
    {
        return g_ut_mdb_dbi_flags(txn, dbi, flags);
    }

    (void)txn;
    (void)dbi;
    if(flags)
    {
        *flags = 0u;
    }
    return MDB_SUCCESS;
}

int mdb_txn_begin(MDB_env* env, MDB_txn* parent, unsigned int flags, MDB_txn** out)
{
    if(g_ut_mdb_txn_begin)
    {
        return g_ut_mdb_txn_begin(env, parent, flags, out);
    }

    (void)parent;
    (void)flags;
    if(out)
    {
        *out = (MDB_txn*)0x100;
    }
    (void)env;
    return MDB_SUCCESS;
}

int mdb_txn_commit(MDB_txn* txn)
{
    if(g_ut_mdb_txn_commit)
    {
        return g_ut_mdb_txn_commit(txn);
    }

    (void)txn;
    return MDB_SUCCESS;
}

int mdb_put(MDB_txn* txn, MDB_dbi dbi, MDB_val* key, MDB_val* data, unsigned int flags)
{
    if(g_ut_mdb_put)
    {
        return g_ut_mdb_put(txn, dbi, key, data, flags);
    }

    (void)txn;
    (void)dbi;
    (void)key;
    (void)data;
    (void)flags;
    return MDB_SUCCESS;
}

int mdb_get(MDB_txn* txn, MDB_dbi dbi, MDB_val* key, MDB_val* data)
{
    if(g_ut_mdb_get)
    {
        return g_ut_mdb_get(txn, dbi, key, data);
    }

    (void)txn;
    (void)dbi;
    (void)key;
    if(data)
    {
        data->mv_data = NULL;
        data->mv_size = 0;
    }
    return MDB_SUCCESS;
}
