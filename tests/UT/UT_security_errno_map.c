#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include <cmocka.h>

#include "tests/UT/ut_env.h"
#include "core/operations/ops_int/security/security.h"

/* ------------------------------------------------------------------------- */
/* Under-test implementation                                                 */
/* ------------------------------------------------------------------------- */

/* Include the implementation with static stripped so we can exercise
 * internal helpers directly while still compiling the same code as
 * production. */
#define static
#include "app/src/core/operations/ops_int/security/security.c"
#undef static

/* ------------------------------------------------------------------------- */
/* Common helpers / fixtures                                                 */
/* ------------------------------------------------------------------------- */

static void ut_reset_all(void)
{
    ut_reset_lmdb_stubs();
    DataBase = NULL;
}

static int  g_abort_calls         = 0;
static MDB_txn* g_last_aborted_txn = NULL;

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

/* ------------------------------------------------------------------------- */
/* _map_mdb_err_to_errno() tests                                             */
/* ------------------------------------------------------------------------- */

static void test_map_mdb_success_returns_zero(void** state)
{
    (void)state;
    ut_reset_all();
    assert_int_equal(_map_mdb_err_to_errno(MDB_SUCCESS), 0);
}

static void test_map_mdb_known_and_unknown_errors_to_errno(void** state)
{
    (void)state;
    ut_reset_all();

    struct
    {
        int rc;
        int expected;
    } cases[] = {
        { MDB_SUCCESS, 0}, /* retest success case */
        { MDB_NOTFOUND, -ENOENT },
        { MDB_KEYEXIST, -EEXIST },
        { MDB_MAP_FULL, -ENOSPC },
        { MDB_DBS_FULL, -ENOSPC },
        { MDB_READERS_FULL, -EAGAIN },
        { MDB_TXN_FULL, -EOVERFLOW },
        { MDB_CURSOR_FULL, -EOVERFLOW },
        { MDB_PAGE_FULL, -ENOSPC },
        { MDB_MAP_RESIZED, -EAGAIN },
        { MDB_INCOMPATIBLE, -EPROTO },
        { MDB_VERSION_MISMATCH, -EINVAL },
        { MDB_INVALID, -EINVAL },
        { MDB_PAGE_NOTFOUND, -EIO },
        { MDB_CORRUPTED, -EIO },
        { MDB_PANIC, -EIO },
        { MDB_BAD_RSLOT, -EBUSY },
        { MDB_BAD_TXN, -EINVAL },
        { MDB_BAD_VALSIZE, -EINVAL },
        { MDB_BAD_DBI, -ESTALE },
        /* Unknown LMDB rc values should map to -rc, including negative. */
        { 1, -1 },
        { -9999, 9999 },
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        int mapped = _map_mdb_err_to_errno(cases[i].rc);
        assert_int_equal(mapped, cases[i].expected);
    }
}

/* ------------------------------------------------------------------------- */
/* _expand_env_mapsize() tests                                               */
/* ------------------------------------------------------------------------- */

static void test_expand_env_mapsize_returns_eio_when_db_invalid(void** state)
{
    (void)state;

    ut_reset_all();

    /* No DataBase and no env pointer. */
    assert_int_equal(_expand_env_mapsize(), -EIO);

    /* DataBase present but env is NULL. */
    static DataBase_t db;
    DataBase                  = &db;
    db.env                    = NULL;
    db.dbis                   = NULL;
    db.n_dbis                 = 0u;
    db.map_size_bytes_max     = 0u;

    assert_int_equal(_expand_env_mapsize(), -EIO);
}

static int ut_env_info_always_fail(MDB_env* env, MDB_envinfo* info)
{
    (void)env;
    (void)info;
    return MDB_PANIC;
}

static void test_expand_env_mapsize_fails_after_three_env_info_retries(void** state)
{
    (void)state;

    ut_reset_all();

    MDB_env*         dummy_env = (MDB_env*)0x1;
    static DataBase_t db;

    db.env                = dummy_env;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = (size_t)1u << 20;
    DataBase              = &db;

    g_ut_mdb_env_info = ut_env_info_always_fail;

    assert_int_equal(_expand_env_mapsize(), -EIO);
}

static int ut_env_info_small_mapsize(MDB_env* env, MDB_envinfo* info)
{
    (void)env;
    if(info)
    {
        info->me_mapsize = 1024u;
    }
    return MDB_SUCCESS;
}

static void test_expand_env_mapsize_fails_when_desired_exceeds_max(void** state)
{
    (void)state;

    ut_reset_all();

    MDB_env*         dummy_env = (MDB_env*)0x1;
    static DataBase_t db;

    db.env                = dummy_env;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = 1024u; /* desired will be 2048u */
    DataBase              = &db;

    g_ut_mdb_env_info = ut_env_info_small_mapsize;

    assert_int_equal(_expand_env_mapsize(), MDB_MAP_FULL);
}

static int ut_env_set_mapsize_fail(MDB_env* env, size_t size)
{
    (void)env;
    (void)size;
    return MDB_PANIC;
}

static void test_expand_env_mapsize_propagates_set_mapsize_error(void** state)
{
    (void)state;

    ut_reset_all();

    MDB_env*         dummy_env = (MDB_env*)0x1;
    static DataBase_t db;

    db.env                = dummy_env;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = (size_t)1u << 20;
    DataBase              = &db;

    g_ut_mdb_env_info        = ut_env_info_small_mapsize;
    g_ut_mdb_env_set_mapsize = ut_env_set_mapsize_fail;

    assert_int_equal(_expand_env_mapsize(), MDB_PANIC);
}

static int ut_env_set_mapsize_ok(MDB_env* env, size_t size)
{
    (void)env;
    (void)size;
    return MDB_SUCCESS;
}

static void test_expand_env_mapsize_success_returns_zero(void** state)
{
    (void)state;

    ut_reset_all();

    MDB_env*         dummy_env = (MDB_env*)0x1;
    static DataBase_t db;

    db.env                = dummy_env;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = (size_t)1u << 20;
    DataBase              = &db;

    g_ut_mdb_env_info        = ut_env_info_small_mapsize;
    g_ut_mdb_env_set_mapsize = ut_env_set_mapsize_ok;

    assert_int_equal(_expand_env_mapsize(), MDB_SUCCESS);
}

/* ------------------------------------------------------------------------- */
/* security_check() tests                                                    */
/* ------------------------------------------------------------------------- */

static void test_security_check_retry_case_aborts_txn_and_sets_errno(void** state)
{
    /* check success fast path does not touch errno */
    (void)state;

    ut_reset_all();
    reset_abort_tracking();

    int errno_out = 42;

    db_security_ret_code_t rc = security_check(MDB_SUCCESS, NULL, &errno_out);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(errno_out, 42);
    assert_int_equal(g_abort_calls, 0);

    /* _check_retry_case_aborts_txn_and_sets_errno */
    ut_reset_all();
    reset_abort_tracking();

    MDB_txn* dummy_txn = (MDB_txn*)0x2;
    rc = security_check(MDB_PAGE_FULL, dummy_txn, &errno_out);

    assert_int_equal(rc, DB_SAFETY_RETRY);
    assert_int_equal(errno_out, -ENOSPC);
    assert_int_equal(g_abort_calls, 1);
    assert_ptr_equal(g_last_aborted_txn, dummy_txn);
}

static void test_security_check_map_full_expand_success_returns_retry(void** state)
{
    (void)state;

    ut_reset_all();
    reset_abort_tracking();

    MDB_env*         dummy_env = (MDB_env*)0x3;
    static DataBase_t db;

    db.env                = dummy_env;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = (size_t)1u << 20;
    DataBase              = &db;

    g_ut_mdb_env_info        = ut_env_info_small_mapsize;
    g_ut_mdb_env_set_mapsize = ut_env_set_mapsize_ok;

    MDB_txn* dummy_txn = (MDB_txn*)0x4;
    int      errno_out = 0;

    db_security_ret_code_t rc = security_check(MDB_MAP_FULL, dummy_txn, &errno_out);

    assert_int_equal(rc, DB_SAFETY_RETRY);
    assert_int_equal(errno_out, -ENOSPC);
    assert_int_equal(g_abort_calls, 1);
    assert_ptr_equal(g_last_aborted_txn, dummy_txn);
}

static void test_security_check_map_full_expand_failure_returns_fail(void** state)
{
    (void)state;

    ut_reset_all();
    reset_abort_tracking();

    MDB_env*         dummy_env = (MDB_env*)0x5;
    static DataBase_t db;

    db.env                = dummy_env;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = (size_t)1u << 20;
    DataBase              = &db;

    g_ut_mdb_env_info        = ut_env_info_small_mapsize;
    g_ut_mdb_env_set_mapsize = ut_env_set_mapsize_fail;

    MDB_txn* dummy_txn = (MDB_txn*)0x6;
    int      errno_out = 0;

    db_security_ret_code_t rc = security_check(MDB_MAP_FULL, dummy_txn, &errno_out);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(errno_out, -ENOSPC);
    assert_int_equal(g_abort_calls, 1);
    assert_ptr_equal(g_last_aborted_txn, dummy_txn);
}

static void test_security_check_logic_failure_returns_fail_without_abort(void** state)
{
    (void)state;

    ut_reset_all();
    reset_abort_tracking();

    int errno_out = 0;

    db_security_ret_code_t rc = security_check(MDB_NOTFOUND, NULL, &errno_out);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(errno_out, -ENOENT);
    assert_int_equal(g_abort_calls, 0);
}

static void test_security_check_unknown_error_aborts_txn_and_sets_errno(void** state)
{
    (void)state;

    ut_reset_all();
    reset_abort_tracking();

    MDB_txn* dummy_txn = (MDB_txn*)0x7;
    int      errno_out = 0;

    const int mdb_rc = 123456; /* not explicitly handled in switch */

    db_security_ret_code_t rc = security_check(mdb_rc, dummy_txn, &errno_out);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(errno_out, -mdb_rc);
    assert_int_equal(g_abort_calls, 1);
    assert_ptr_equal(g_last_aborted_txn, dummy_txn);
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_map_mdb_success_returns_zero),
        cmocka_unit_test(test_map_mdb_known_and_unknown_errors_to_errno),
        cmocka_unit_test(test_expand_env_mapsize_returns_eio_when_db_invalid),
        cmocka_unit_test(test_expand_env_mapsize_fails_after_three_env_info_retries),
        cmocka_unit_test(test_expand_env_mapsize_fails_when_desired_exceeds_max),
        cmocka_unit_test(test_expand_env_mapsize_propagates_set_mapsize_error),
        cmocka_unit_test(test_expand_env_mapsize_success_returns_zero),
        cmocka_unit_test(test_security_check_retry_case_aborts_txn_and_sets_errno),
        cmocka_unit_test(test_security_check_map_full_expand_success_returns_retry),
        cmocka_unit_test(test_security_check_map_full_expand_failure_returns_fail),
        cmocka_unit_test(test_security_check_logic_failure_returns_fail_without_abort),
        cmocka_unit_test(test_security_check_unknown_error_aborts_txn_and_sets_errno),
    };

    int rc = cmocka_run_group_tests(tests, NULL, NULL);

    if(rc == 0)
    {
        printf(UT_COLOR_GREEN "[UT] security module tests: PASSED" UT_COLOR_RESET "\n");
    }
    else
    {
        printf(UT_COLOR_RED "[UT] security module tests: FAILED (rc=%d)" UT_COLOR_RESET "\n", rc);
    }

    return rc;
}
