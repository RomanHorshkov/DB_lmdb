#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cmocka.h>

#include "tests/UT/ut_env.h"
#include "core/operations/ops_int/ops_init.h"
#include "core/operations/ops_int/db/dbi_int.h"

/* ------------------------------------------------------------------------- */
/* Under-test implementation                                                 */
/* ------------------------------------------------------------------------- */

/* Include ops_init.c with static stripped so we can exercise internal
 * helpers (e.g. _ensure_env_dir) while still compiling the same code
 * as production. */
#define static
#include "app/src/core/operations/ops_int/ops_init.c"
#undef static

/* ------------------------------------------------------------------------- */
/* Common helpers / fixtures                                                 */
/* ------------------------------------------------------------------------- */

static void ut_reset_all(void)
{
    ut_reset_lmdb_stubs();
    DataBase = NULL;
}

/* Simple abort tracker reused across tests which exercise security_check()
 * paths via LMDB error returns. */
static int      g_abort_calls         = 0;
static MDB_txn* g_last_aborted_txn    = NULL;

static void ut_abort_counter(MDB_txn* txn)
{
    g_abort_calls++;
    g_last_aborted_txn = txn;
}

static void reset_abort_tracking(void)
{
    g_abort_calls      = 0;
    g_last_aborted_txn = NULL;
    g_ut_mdb_txn_abort = ut_abort_counter;
}

/* Helper: ensure a clean directory path is removed before use. */
static void ut_rm_path(const char* path)
{
    struct stat st;
    if(stat(path, &st) == 0)
    {
        if(S_ISDIR(st.st_mode))
        {
            rmdir(path);
        }
        else
        {
            unlink(path);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* _db_set_max_dbis() tests                                                  */
/* ------------------------------------------------------------------------- */

static void test_db_set_max_dbis_rejects_zero(void** state)
{
    (void)state;

    ut_reset_all();

    int err = 0;
    db_security_ret_code_t rc = _db_set_max_dbis(0u, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -EINVAL);
}

static int ut_env_set_maxdbs_capture(MDB_env* env, MDB_dbi dbs)
{
    /* Basic sanity: env should be non-NULL and dbs propagated. */
    assert_non_null(env);
    assert_int_equal(dbs, (MDB_dbi)4u);
    return MDB_SUCCESS;
}

static void test_db_set_max_dbis_success_calls_lmdb_and_returns_success(void** state)
{
    (void)state;

    ut_reset_all();

    static DataBase_t db;
    MDB_env*          env = (MDB_env*)0x1;
    db.env                = env;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    g_ut_mdb_env_set_maxdbs = ut_env_set_maxdbs_capture;

    int err = 123;
    db_security_ret_code_t rc = _db_set_max_dbis(4u, &err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, 123); /* unchanged on success */
}

static int ut_env_set_maxdbs_fail(MDB_env* env, MDB_dbi dbs)
{
    (void)env;
    (void)dbs;
    return MDB_DBS_FULL;
}

static void test_db_set_max_dbis_lmdb_error_uses_security_check(void** state)
{
    (void)state;

    ut_reset_all();
    reset_abort_tracking();

    static DataBase_t db;
    db.env                = (MDB_env*)0x2;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    g_ut_mdb_env_set_maxdbs = ut_env_set_maxdbs_fail;

    int err = 0;
    db_security_ret_code_t rc = _db_set_max_dbis(8u, &err);

    /* MDB_DBS_FULL is mapped by security_check as a fatal failure. */
    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -ENOSPC);
    /* No txn supplied, so no aborts expected. */
    assert_int_equal(g_abort_calls, 0);
}

/* ------------------------------------------------------------------------- */
/* _db_set_map_size() tests                                                  */
/* ------------------------------------------------------------------------- */

static int ut_env_set_mapsize_fail_panic(MDB_env* env, size_t size)
{
    (void)env;
    (void)size;
    return MDB_PANIC;
}

static void test_db_set_map_size_success(void** state)
{
    (void)state;

    ut_reset_all();

    static DataBase_t db;
    db.env                = (MDB_env*)0x3;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    int err = 0;
    db_security_ret_code_t rc = _db_set_map_size(&err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, 0);
}

static void test_db_set_map_size_lmdb_error_uses_security_check(void** state)
{
    (void)state;

    ut_reset_all();
    reset_abort_tracking();

    static DataBase_t db;
    db.env                = (MDB_env*)0x4;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    g_ut_mdb_env_set_mapsize = ut_env_set_mapsize_fail_panic;

    int err = 0;
    db_security_ret_code_t rc = _db_set_map_size(&err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -EIO); /* MDB_PANIC -> -EIO */
    assert_int_equal(g_abort_calls, 0);
}

/* ------------------------------------------------------------------------- */
/* _db_create_env() tests                                                    */
/* ------------------------------------------------------------------------- */

static void test_db_create_env_fails_when_database_null(void** state)
{
    (void)state;

    ut_reset_all();

    int err = 0;
    db_security_ret_code_t rc = _db_create_env(&err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -EIO);
}

static int ut_env_create_fail_sets_env(MDB_env** env)
{
    if(env)
    {
        *env = (MDB_env*)0x5;
    }
    return MDB_VERSION_MISMATCH;
}

static int g_env_close_calls = 0;
static void ut_env_close_counter(MDB_env* env)
{
    (void)env;
    g_env_close_calls++;
}

static void test_db_create_env_handles_lmdb_failure_and_closes_env(void** state)
{
    (void)state;

    ut_reset_all();
    reset_abort_tracking();

    static DataBase_t db;
    db.env                = NULL;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    g_ut_mdb_env_create = ut_env_create_fail_sets_env;
    g_ut_mdb_env_close  = ut_env_close_counter;
    g_env_close_calls   = 0;

    int err = 0;
    db_security_ret_code_t rc = _db_create_env(&err);

    /* env should have been closed and cleared on error. */
    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -EINVAL); /* VERSION_MISMATCH -> -EINVAL */
    assert_int_equal(g_env_close_calls, 1);
    assert_null(DataBase->env);
}

static int ut_env_create_success(MDB_env** env)
{
    if(env)
    {
        *env = (MDB_env*)0x6;
    }
    return MDB_SUCCESS;
}

static void test_db_create_env_success_sets_env_handle(void** state)
{
    (void)state;

    ut_reset_all();

    static DataBase_t db;
    db.env                = NULL;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    g_ut_mdb_env_create = ut_env_create_success;

    int err = -1;
    db_security_ret_code_t rc = _db_create_env(&err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, -1);
    assert_ptr_equal(DataBase->env, (MDB_env*)0x6);
}

/* ------------------------------------------------------------------------- */
/* _db_open_env() and _ensure_env_dir() tests                                */
/* ------------------------------------------------------------------------- */

static void test_db_open_env_rejects_null_path(void** state)
{
    (void)state;

    ut_reset_all();

    int err = 0;
    db_security_ret_code_t rc = _db_open_env(NULL, 0600u, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -EINVAL);
}

static void test_ensure_env_dir_rejects_non_directory_path(void** state)
{
    (void)state;

    ut_reset_all();

    const char* path = "tests/UT/tmp_ops_init_notdir";
    ut_rm_path(path);

    FILE* f = fopen(path, "w");
    assert_non_null(f);
    fclose(f);

    int rc = _ensure_env_dir(path);
    assert_int_equal(rc, -ENOTDIR);

    ut_rm_path(path);
}

static void test_ensure_env_dir_rejects_world_readable_dir(void** state)
{
    (void)state;

    ut_reset_all();

    const char* path = "tests/UT/tmp_ops_init_world";
    ut_rm_path(path);

    assert_int_equal(mkdir(path, 0755), 0);

    int rc = _ensure_env_dir(path);
    assert_int_equal(rc, -EACCES);

    ut_rm_path(path);
}

static void test_ensure_env_dir_creates_directory_with_strict_perms(void** state)
{
    (void)state;

    ut_reset_all();

    const char* path = "tests/UT/tmp_ops_init_dir";
    ut_rm_path(path);

    int rc = _ensure_env_dir(path);
    assert_int_equal(rc, 0);

    struct stat st;
    assert_int_equal(stat(path, &st), 0);
    assert_true(S_ISDIR(st.st_mode));
    assert_true((st.st_mode & (S_IRWXG | S_IRWXO)) == 0);

    ut_rm_path(path);
}

static int ut_env_open_fail_incompatible(MDB_env* env, const char* path, unsigned int flags, mdb_mode_t mode)
{
    (void)env;
    (void)path;
    (void)flags;
    (void)mode;
    return MDB_INCOMPATIBLE;
}

static void test_db_open_env_propagates_lmdb_error_via_security_check(void** state)
{
    (void)state;

    ut_reset_all();
    reset_abort_tracking();

    static DataBase_t db;
    db.env                = (MDB_env*)0x7;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    g_ut_mdb_env_open = ut_env_open_fail_incompatible;

    const char* path = "tests/UT/tmp_ops_init_env_open";
    ut_rm_path(path);

    int err = 0;
    db_security_ret_code_t rc = _db_open_env(path, 0600u, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -EPROTO); /* MDB_INCOMPATIBLE -> -EPROTO */
    assert_int_equal(g_abort_calls, 0);

    ut_rm_path(path);
}

/* ------------------------------------------------------------------------- */
/* _dbi_open() and _dbi_get_flags() tests                                    */
/* ------------------------------------------------------------------------- */

static void test_dbi_open_rejects_null_inputs(void** state)
{
    (void)state;

    ut_reset_all();

    MDB_txn* txn = NULL;
    int      err = 0;

    db_security_ret_code_t rc = _dbi_open(txn, 0u, "name", 0u, &err);
    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -EINVAL);

    txn = (MDB_txn*)0x8;
    rc  = _dbi_open(txn, 0u, NULL, 0u, &err);
    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -EINVAL);
}

static int ut_dbi_open_success(MDB_txn* txn, const char* name, unsigned int flags, MDB_dbi* dbi)
{
    (void)txn;
    (void)name;
    (void)flags;
    if(dbi)
    {
        *dbi = (MDB_dbi)42u;
    }
    return MDB_SUCCESS;
}

static void test_dbi_open_success_sets_dbi_handle(void** state)
{
    (void)state;

    ut_reset_all();

    static dbi_t      dbis[1];
    static DataBase_t db;

    db.env                = (MDB_env*)0x9;
    db.dbis               = dbis;
    db.n_dbis             = 1u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    g_ut_mdb_dbi_open = ut_dbi_open_success;

    MDB_txn* txn = (MDB_txn*)0x10;
    int      err = 0;

    db_security_ret_code_t rc = _dbi_open(txn, 0u, "name", 0u, &err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, 0);
    assert_int_equal(db.dbis[0].dbi, 42u);
}

static int ut_dbi_open_fail_dbs_full(MDB_txn* txn, const char* name, unsigned int flags, MDB_dbi* dbi)
{
    (void)txn;
    (void)name;
    (void)flags;
    (void)dbi;
    return MDB_DBS_FULL;
}

static void test_dbi_open_lmdb_error_uses_security_check_and_aborts_txn(void** state)
{
    (void)state;

    ut_reset_all();
    reset_abort_tracking();

    static dbi_t      dbis[1];
    static DataBase_t db;

    db.env                = (MDB_env*)0x11;
    db.dbis               = dbis;
    db.n_dbis             = 1u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    g_ut_mdb_dbi_open = ut_dbi_open_fail_dbs_full;

    MDB_txn* txn = (MDB_txn*)0x12;
    int      err = 0;

    db_security_ret_code_t rc = _dbi_open(txn, 0u, "name", 0u, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -ENOSPC);
    assert_int_equal(g_abort_calls, 1);
    assert_ptr_equal(g_last_aborted_txn, txn);
}

static void test_dbi_get_flags_rejects_null_txn(void** state)
{
    (void)state;

    ut_reset_all();

    int err = 0;
    db_security_ret_code_t rc = _dbi_get_flags(NULL, 0u, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -EINVAL);
}

static int ut_dbi_flags_success(MDB_txn* txn, MDB_dbi dbi, unsigned int* flags)
{
    (void)txn;
    (void)dbi;
    if(flags)
    {
        *flags = MDB_DUPSORT | MDB_DUPFIXED;
    }
    return MDB_SUCCESS;
}

static void test_dbi_get_flags_success_populates_dbi_flags(void** state)
{
    (void)state;

    ut_reset_all();

    static dbi_t      dbis[1];
    static DataBase_t db;

    db.env                = (MDB_env*)0x13;
    db.dbis               = dbis;
    db.n_dbis             = 1u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    db.dbis[0].dbi = 7u;

    g_ut_mdb_dbi_flags = ut_dbi_flags_success;

    MDB_txn* txn = (MDB_txn*)0x14;
    int      err = 0;

    db_security_ret_code_t rc = _dbi_get_flags(txn, 0u, &err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, 0);
    assert_int_equal(db.dbis[0].db_flags, (unsigned)(MDB_DUPSORT | MDB_DUPFIXED));
}

static int ut_dbi_flags_fail_bad_dbi(MDB_txn* txn, MDB_dbi dbi, unsigned int* flags)
{
    (void)txn;
    (void)dbi;
    (void)flags;
    return MDB_BAD_DBI;
}

static void test_dbi_get_flags_lmdb_error_uses_security_check_and_aborts_txn(void** state)
{
    (void)state;

    ut_reset_all();
    reset_abort_tracking();

    static dbi_t      dbis[1];
    static DataBase_t db;

    db.env                = (MDB_env*)0x15;
    db.dbis               = dbis;
    db.n_dbis             = 1u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    db.dbis[0].dbi = 9u;

    g_ut_mdb_dbi_flags = ut_dbi_flags_fail_bad_dbi;

    MDB_txn* txn = (MDB_txn*)0x16;
    int      err = 0;

    db_security_ret_code_t rc = _dbi_get_flags(txn, 0u, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -ESTALE);
    assert_int_equal(g_abort_calls, 1);
    assert_ptr_equal(g_last_aborted_txn, txn);
}

/* ------------------------------------------------------------------------- */
/* Public ops_init_env() / ops_init_dbi() tests                              */
/* ------------------------------------------------------------------------- */

static int ut_env_create_assign(MDB_env** env)
{
    if(env)
    {
        *env = (MDB_env*)0x20;
    }
    return MDB_SUCCESS;
}

static int ut_env_set_maxdbs_ok(MDB_env* env, MDB_dbi dbs)
{
    assert_ptr_equal(env, (MDB_env*)0x20);
    assert_true(dbs > 0);
    return MDB_SUCCESS;
}

static int ut_env_set_mapsize_ok(MDB_env* env, size_t size)
{
    assert_ptr_equal(env, (MDB_env*)0x20);
    assert_true(size > 0);
    return MDB_SUCCESS;
}

static int ut_env_open_ok(MDB_env* env, const char* path, unsigned int flags, mdb_mode_t mode)
{
    assert_ptr_equal(env, (MDB_env*)0x20);
    assert_non_null(path);
    (void)flags;
    (void)mode;
    return MDB_SUCCESS;
}

static void test_ops_init_env_success_happy_path(void** state)
{
    (void)state;

    ut_reset_all();

    static DataBase_t db;
    db.env                = NULL;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = (size_t)DB_MAP_SIZE_MAX;
    DataBase              = &db;

    g_ut_mdb_env_create     = ut_env_create_assign;
    g_ut_mdb_env_set_maxdbs = ut_env_set_maxdbs_ok;
    g_ut_mdb_env_set_mapsize = ut_env_set_mapsize_ok;
    g_ut_mdb_env_open       = ut_env_open_ok;

    const char* path = "tests/UT/tmp_ops_init_env_full";
    ut_rm_path(path);

    int err = -1;
    db_security_ret_code_t rc = ops_init_env(4u, path, 0600u, &err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, -1);
    assert_ptr_equal(DataBase->env, (MDB_env*)0x20);

    ut_rm_path(path);
}

static void test_ops_init_dbi_success_configures_cached_flags(void** state)
{
    (void)state;

    ut_reset_all();

    static dbi_t      dbis[1];
    static DataBase_t db;

    db.env                = (MDB_env*)0x30;
    db.dbis               = dbis;
    db.n_dbis             = 1u;
    db.map_size_bytes_max = (size_t)DB_MAP_SIZE_MAX;
    DataBase              = &db;

    dbis[0].dbi      = 0u;
    dbis[0].db_flags = MDB_DUPSORT | MDB_DUPFIXED;

    g_ut_mdb_dbi_open = ut_dbi_open_success;
    g_ut_mdb_dbi_flags = ut_dbi_flags_success;

    MDB_txn*    txn  = (MDB_txn*)0x31;
    dbi_type_t type = DBI_TYPE_NOOVERWRITE | DBI_TYPE_DUPSORT;

    int err = -1;
    db_security_ret_code_t rc = ops_init_dbi(txn, "demo", 0u, type, &err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, -1);

    assert_int_equal(dbis[0].dbi, 42u);
    assert_int_equal(dbis[0].db_flags, (unsigned)(MDB_DUPSORT | MDB_DUPFIXED));
    assert_int_equal(dbis[0].put_flags, MDB_NOOVERWRITE);
    assert_int_equal(dbis[0].is_dupsort, 1u);
    assert_int_equal(dbis[0].is_dupfixed, 1u);
}

static void test_ops_init_dbi_rejects_invalid_input(void** state)
{
    (void)state;

    ut_reset_all();

    static dbi_t      dbis[1];
    static DataBase_t db;

    db.env                = (MDB_env*)0x40;
    db.dbis               = dbis;
    db.n_dbis             = 1u;
    db.map_size_bytes_max = (size_t)DB_MAP_SIZE_MAX;
    DataBase              = &db;

    int err = 0;
    db_security_ret_code_t rc =
        ops_init_dbi(NULL, "demo", 0u, DBI_TYPE_DEFAULT, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -EINVAL);

    MDB_txn* txn = (MDB_txn*)0x41;
    err          = 0;
    rc           = ops_init_dbi(txn, "demo", 1u, DBI_TYPE_DEFAULT, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -EINVAL);
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_db_set_max_dbis_rejects_zero),
        cmocka_unit_test(test_db_set_max_dbis_success_calls_lmdb_and_returns_success),
        cmocka_unit_test(test_db_set_max_dbis_lmdb_error_uses_security_check),
        cmocka_unit_test(test_db_set_map_size_success),
        cmocka_unit_test(test_db_set_map_size_lmdb_error_uses_security_check),
        cmocka_unit_test(test_db_create_env_fails_when_database_null),
        cmocka_unit_test(test_db_create_env_handles_lmdb_failure_and_closes_env),
        cmocka_unit_test(test_db_create_env_success_sets_env_handle),
        cmocka_unit_test(test_db_open_env_rejects_null_path),
        cmocka_unit_test(test_ensure_env_dir_rejects_non_directory_path),
        cmocka_unit_test(test_ensure_env_dir_rejects_world_readable_dir),
        cmocka_unit_test(test_ensure_env_dir_creates_directory_with_strict_perms),
        cmocka_unit_test(test_db_open_env_propagates_lmdb_error_via_security_check),
        cmocka_unit_test(test_dbi_open_rejects_null_inputs),
        cmocka_unit_test(test_dbi_open_success_sets_dbi_handle),
        cmocka_unit_test(test_dbi_open_lmdb_error_uses_security_check_and_aborts_txn),
        cmocka_unit_test(test_dbi_get_flags_rejects_null_txn),
        cmocka_unit_test(test_dbi_get_flags_success_populates_dbi_flags),
        cmocka_unit_test(test_dbi_get_flags_lmdb_error_uses_security_check_and_aborts_txn),
        cmocka_unit_test(test_ops_init_env_success_happy_path),
        cmocka_unit_test(test_ops_init_dbi_success_configures_cached_flags),
        cmocka_unit_test(test_ops_init_dbi_rejects_invalid_input),
    };

    int rc = cmocka_run_group_tests(tests, NULL, NULL);

    if(rc == 0)
    {
        printf(UT_COLOR_GREEN "[UT] ops_init tests: PASSED" UT_COLOR_RESET "\n");
    }
    else
    {
        printf(UT_COLOR_RED "[UT] ops_init tests: FAILED (rc=%d)" UT_COLOR_RESET "\n", rc);
    }

    return rc;
}

