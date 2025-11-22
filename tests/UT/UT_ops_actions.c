#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <cmocka.h>

#include "tests/UT/ut_env.h"
#include "core/operations/ops_int/ops_actions.h"

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static void ut_reset_all(void)
{
    ut_reset_lmdb_stubs();
    DataBase = NULL;
}

/* _resolve_desc() is exercised indirectly via act_put/act_get tests. */

/* ------------------------------------------------------------------------- */
/* act_txn_begin / act_txn_commit tests                                      */
/* ------------------------------------------------------------------------- */

static int ut_txn_begin_capture(MDB_env* env, MDB_txn* parent, unsigned int flags, MDB_txn** out)
{
    (void)parent;
    assert_non_null(env);
    assert_int_equal(flags, 123u);
    if(out)
    {
        *out = (MDB_txn*)0x200;
    }
    return MDB_SUCCESS;
}

static void test_act_txn_begin_success(void** state)
{
    (void)state;

    ut_reset_all();

    static DataBase_t db;
    db.env                = (MDB_env*)0x10;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    g_ut_mdb_txn_begin = ut_txn_begin_capture;

    MDB_txn* out_txn = NULL;
    int      err     = -1;

    db_security_ret_code_t rc = act_txn_begin(&out_txn, 123u, &err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, -1);
    assert_ptr_equal(out_txn, (MDB_txn*)0x200);
}

static void test_act_txn_begin_invalid_input_fails(void** state)
{
    (void)state;

    ut_reset_all();

    MDB_txn* out_txn = (MDB_txn*)0x1;
    int      err     = 0;

    db_security_ret_code_t rc = act_txn_begin(&out_txn, 0u, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
}

static int ut_txn_begin_fail_readers_full(MDB_env* env, MDB_txn* parent, unsigned int flags, MDB_txn** out)
{
    (void)env;
    (void)parent;
    (void)flags;
    if(out)
    {
        *out = NULL;
    }
    return MDB_READERS_FULL;
}

static void test_act_txn_begin_lmdb_error_maps_via_security_check(void** state)
{
    (void)state;

    ut_reset_all();

    static DataBase_t db;
    db.env                = (MDB_env*)0x11;
    db.dbis               = NULL;
    db.n_dbis             = 0u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    g_ut_mdb_txn_begin = ut_txn_begin_fail_readers_full;

    MDB_txn* out_txn = (MDB_txn*)0x2;
    int      err     = 0;

    db_security_ret_code_t rc = act_txn_begin(&out_txn, 0u, &err);

    assert_int_equal(rc, DB_SAFETY_RETRY); /* READERS_FULL -> EAGAIN -> RETRY */
    assert_int_equal(err, -EAGAIN);
    assert_null(out_txn);
}

static int ut_txn_commit_fail_corrupted(MDB_txn* txn)
{
    (void)txn;
    return MDB_CORRUPTED;
}

static void test_act_txn_commit_success(void** state)
{
    (void)state;

    ut_reset_all();

    MDB_txn* txn = (MDB_txn*)0x30;
    int      err = -5;

    db_security_ret_code_t rc = act_txn_commit(txn, &err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, -5);
}

static void test_act_txn_commit_invalid_input_fails(void** state)
{
    (void)state;

    ut_reset_all();

    int err = 0;
    db_security_ret_code_t rc = act_txn_commit(NULL, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
}

static void test_act_txn_commit_lmdb_error_maps_via_security_check(void** state)
{
    (void)state;

    ut_reset_all();

    g_ut_mdb_txn_commit = ut_txn_commit_fail_corrupted;

    MDB_txn* txn = (MDB_txn*)0x31;
    int      err = 0;

    db_security_ret_code_t rc = act_txn_commit(txn, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -EIO); /* MDB_CORRUPTED -> -EIO */
}

/* ------------------------------------------------------------------------- */
/* act_put() / act_get() tests                                               */
/* ------------------------------------------------------------------------- */

static void test_act_put_rejects_invalid_input(void** state)
{
    (void)state;

    ut_reset_all();

    int err = 0;
    assert_int_equal(act_put(NULL, NULL, &err), DB_SAFETY_FAIL);
}

static int ut_put_capture(MDB_txn* txn, MDB_dbi dbi, MDB_val* key, MDB_val* val, unsigned int flags)
{
    (void)txn;
    assert_int_equal(dbi, (MDB_dbi)7u);
    assert_non_null(key);
    assert_non_null(val);
    assert_int_equal(flags, 0xA5u);
    return MDB_SUCCESS;
}

static void test_act_put_success_uses_dbi_flags_and_resolved_kv(void** state)
{
    (void)state;

    ut_reset_all();

    static dbi_t      dbis[1];
    static DataBase_t db;
    db.env                = (MDB_env*)0x40;
    db.dbis               = dbis;
    db.n_dbis             = 1u;
    db.map_size_bytes_max = 0u;
    DataBase              = &db;

    dbis[0].dbi      = 7u;
    dbis[0].put_flags = 0xA5u;

    op_t op;
    memset(&op, 0, sizeof(op));
    op.dbi = 0u;

    char key_buf[3] = { 'k', 'e', 'y' };
    op.key.kind         = OP_KEY_KIND_PRESENT;
    op.key.present.data  = key_buf;
    op.key.present.size = sizeof(key_buf);

    char val_buf[2] = { 'v', '1' };
    op.val.kind         = OP_KEY_KIND_PRESENT;
    op.val.present.data  = val_buf;
    op.val.present.size = sizeof(val_buf);

    g_ut_mdb_put = ut_put_capture;

    int err = -1;
    db_security_ret_code_t rc = act_put((MDB_txn*)0x41, &op, &err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, -1);
}

static void test_act_put_resolve_failure_propagates_fail(void** state)
{
    (void)state;

    ut_reset_all();

    op_t op;
    memset(&op, 0, sizeof(op));

    /* No key data => _get_key returns NULL. */
    int err = 0;
    assert_int_equal(act_put((MDB_txn*)0x42, &op, &err), DB_SAFETY_FAIL);
}

static int ut_put_fail_map_full(MDB_txn* txn, MDB_dbi dbi, MDB_val* key, MDB_val* val, unsigned int flags)
{
    (void)txn;
    (void)dbi;
    (void)key;
    (void)val;
    (void)flags;
    return MDB_MAP_FULL;
}

static void test_act_put_lmdb_error_goes_through_security_check(void** state)
{
    (void)state;

    ut_reset_all();

    static dbi_t      dbis[1];
    static DataBase_t db;
    db.env                = (MDB_env*)0x50;
    db.dbis               = dbis;
    db.n_dbis             = 1u;
    db.map_size_bytes_max = (size_t)DB_MAP_SIZE_MAX;
    DataBase              = &db;

    dbis[0].dbi       = 1u;
    dbis[0].put_flags = 0u;

    /* Minimal valid op descriptor. */
    op_t op;
    memset(&op, 0, sizeof(op));
    op.dbi = 0;

    char key_buf[1] = { 'k' };
    op.key.kind         = OP_KEY_KIND_PRESENT;
    op.key.present.data  = key_buf;
    op.key.present.size = sizeof(key_buf);

    char val_buf[1] = { 'v' };
    op.val.kind         = OP_KEY_KIND_PRESENT;
    op.val.present.data  = val_buf;
    op.val.present.size = sizeof(val_buf);

    g_ut_mdb_put = ut_put_fail_map_full;

    int err = 0;
    db_security_ret_code_t rc = act_put((MDB_txn*)0x51, &op, &err);

    /* For MAP_FULL, with default stubs, security_check attempts expansion
     * and returns RETRY, with errno mapped to -ENOSPC. */
    assert_int_equal(rc, DB_SAFETY_RETRY);
    assert_int_equal(err, -ENOSPC);
}

static void test_act_get_rejects_invalid_input(void** state)
{
    (void)state;

    ut_reset_all();

    op_t op;
    memset(&op, 0, sizeof(op));

    int err = 0;
    assert_int_equal(act_get(NULL, &op, &err), DB_SAFETY_FAIL);
    assert_int_equal(act_get((MDB_txn*)0x60, NULL, &err), DB_SAFETY_FAIL);
}

static void ut_prepare_db_for_get(void)
{
    static dbi_t      dbis[1];
    static DataBase_t db;

    memset(dbis, 0, sizeof(dbis));
    memset(&db, 0, sizeof(db));

    db.env                = (MDB_env*)0x70;
    db.dbis               = dbis;
    db.n_dbis             = 1u;
    db.map_size_bytes_max = 0u;
    dbis[0].dbi           = 1u;

    DataBase = &db;
}

static int ut_get_success_in_place_buffer(MDB_txn* txn, MDB_dbi dbi, MDB_val* key, MDB_val* val)
{
    (void)txn;
    (void)dbi;
    (void)key;

    static char data[] = "val";
    val->mv_data       = data;
    val->mv_size       = strlen(data);
    return MDB_SUCCESS;
}

static void test_act_get_copies_into_user_buffer_when_present(void** state)
{
    (void)state;

    ut_reset_all();

    op_t op;
    memset(&op, 0, sizeof(op));

    char key_buf[3] = { 'k', 'e', 'y' };
    op.key.kind         = OP_KEY_KIND_PRESENT;
    op.key.present.data  = key_buf;
    op.key.present.size = sizeof(key_buf);

    char val_buf[8] = { 0 };
    op.val.kind         = OP_KEY_KIND_PRESENT;
    op.val.present.data  = val_buf;
    op.val.present.size = sizeof(val_buf);

    ut_prepare_db_for_get();
    g_ut_mdb_get = ut_get_success_in_place_buffer;

    int err = 0;
    db_security_ret_code_t rc = act_get((MDB_txn*)0x61, &op, &err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, 0);
    assert_string_equal(val_buf, "val");
}

static int ut_get_success_no_user_buffer(MDB_txn* txn, MDB_dbi dbi, MDB_val* key, MDB_val* val)
{
    (void)txn;
    (void)dbi;
    (void)key;

    static char data[] = "X";
    val->mv_data       = data;
    val->mv_size       = 1;
    return MDB_SUCCESS;
}

static void test_act_get_populates_present_when_no_buffer(void** state)
{
    (void)state;

    ut_reset_all();

    op_t op;
    memset(&op, 0, sizeof(op));

    char key_buf[1] = { 'k' };
    op.key.kind         = OP_KEY_KIND_PRESENT;
    op.key.present.data  = key_buf;
    op.key.present.size = sizeof(key_buf);

    op.val.kind = OP_KEY_KIND_NONE;

    ut_prepare_db_for_get();
    g_ut_mdb_get = ut_get_success_no_user_buffer;

    int err = 0;
    db_security_ret_code_t rc = act_get((MDB_txn*)0x62, &op, &err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, 0);
    assert_int_equal(op.val.kind, OP_KEY_KIND_PRESENT);
    assert_int_equal(op.val.present.size, 1u);
    assert_non_null(op.val.present.data);
}

static int ut_get_sets_large_value(MDB_txn* txn, MDB_dbi dbi, MDB_val* key, MDB_val* val)
{
    (void)txn;
    (void)dbi;
    (void)key;

    static char data[16];
    val->mv_data = data;
    val->mv_size = sizeof(data);
    return MDB_SUCCESS;
}

static void test_act_get_user_buffer_too_small_fails(void** state)
{
    (void)state;

    ut_reset_all();

    op_t op;
    memset(&op, 0, sizeof(op));

    char key_buf[1] = { 'k' };
    op.key.kind         = OP_KEY_KIND_PRESENT;
    op.key.present.data  = key_buf;
    op.key.present.size = sizeof(key_buf);

    char val_buf[4];
    op.val.kind         = OP_KEY_KIND_PRESENT;
    op.val.present.data  = val_buf;
    op.val.present.size = sizeof(val_buf); /* smaller than mv_size in hook */

    ut_prepare_db_for_get();
    g_ut_mdb_get = ut_get_sets_large_value;

    int err = 0;
    db_security_ret_code_t rc = act_get((MDB_txn*)0x63, &op, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
}

static int ut_get_fail_notfound(MDB_txn* txn, MDB_dbi dbi, MDB_val* key, MDB_val* val)
{
    (void)txn;
    (void)dbi;
    (void)key;
    (void)val;
    return MDB_NOTFOUND;
}

static void test_act_get_lmdb_error_maps_via_security_check(void** state)
{
    (void)state;

    ut_reset_all();

    op_t op;
    memset(&op, 0, sizeof(op));

    char key_buf[1] = { 'k' };
    op.key.kind         = OP_KEY_KIND_PRESENT;
    op.key.present.data  = key_buf;
    op.key.present.size = sizeof(key_buf);

    ut_prepare_db_for_get();
    g_ut_mdb_get = ut_get_fail_notfound;

    int err = 0;
    db_security_ret_code_t rc = act_get((MDB_txn*)0x64, &op, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
    assert_int_equal(err, -ENOENT);
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_act_txn_begin_success),
        cmocka_unit_test(test_act_txn_begin_invalid_input_fails),
        cmocka_unit_test(test_act_txn_begin_lmdb_error_maps_via_security_check),
        cmocka_unit_test(test_act_txn_commit_success),
        cmocka_unit_test(test_act_txn_commit_invalid_input_fails),
        cmocka_unit_test(test_act_txn_commit_lmdb_error_maps_via_security_check),
        cmocka_unit_test(test_act_put_rejects_invalid_input),
        cmocka_unit_test(test_act_put_success_uses_dbi_flags_and_resolved_kv),
        cmocka_unit_test(test_act_put_resolve_failure_propagates_fail),
        cmocka_unit_test(test_act_put_lmdb_error_goes_through_security_check),
        cmocka_unit_test(test_act_get_rejects_invalid_input),
        cmocka_unit_test(test_act_get_copies_into_user_buffer_when_present),
        cmocka_unit_test(test_act_get_populates_present_when_no_buffer),
        cmocka_unit_test(test_act_get_user_buffer_too_small_fails),
        cmocka_unit_test(test_act_get_lmdb_error_maps_via_security_check),
    };

    int rc = cmocka_run_group_tests(tests, NULL, NULL);

    if(rc == 0)
    {
        printf(UT_COLOR_GREEN "[UT] ops_actions tests: PASSED" UT_COLOR_RESET "\n");
    }
    else
    {
        printf(UT_COLOR_RED "[UT] ops_actions tests: FAILED (rc=%d)" UT_COLOR_RESET "\n", rc);
    }

    return rc;
}
