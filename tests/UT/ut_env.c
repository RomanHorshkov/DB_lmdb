#include "tests/UT/ut_env.h"

#include <stdarg.h>

/* Global DB handle expected by core code. */
DataBase_t* DataBase = NULL;

/* Hook points used by tests to customize LMDB behavior. */
ut_mdb_env_info_fn        g_ut_mdb_env_info        = NULL;
ut_mdb_env_set_mapsize_fn g_ut_mdb_env_set_mapsize = NULL;
ut_mdb_txn_abort_fn       g_ut_mdb_txn_abort       = NULL;

void ut_reset_lmdb_stubs(void)
{
    g_ut_mdb_env_info        = NULL;
    g_ut_mdb_env_set_mapsize = NULL;
    g_ut_mdb_txn_abort       = NULL;
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
