#include "tests/UT/ut_env.h"

#include <stdarg.h>

/* Global DB handle expected by core code. */
DataBase_t* DataBase = NULL;

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
    (void)txn;
}

int mdb_env_info(MDB_env* env, MDB_envinfo* stat)
{
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
    (void)env;
    (void)size;
    return MDB_SUCCESS;
}

