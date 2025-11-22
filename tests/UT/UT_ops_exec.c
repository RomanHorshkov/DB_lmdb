#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <cmocka.h>

#include "tests/UT/ut_env.h"
#include "core/operations/ops_int/ops_exec.h"

/* ------------------------------------------------------------------------- */
/* Under-test implementation                                                 */
/* ------------------------------------------------------------------------- */

/* Pull in the implementation with static stripped so we can call internal
 * helpers such as _exec_op and _rw_cache_alloc directly. */
#define static
#include "app/src/core/operations/ops_int/ops_exec.c"
#undef static

/* ------------------------------------------------------------------------- */
/* Lightweight stubs for ops_actions layer                                   */
/* ------------------------------------------------------------------------- */

/* We test ops_actions in its own UT suite; here we only need predictable
 * behavior from act_put/act_get/txn helpers, so provide simple stubs. */

static db_security_ret_code_t g_next_put_rc = DB_SAFETY_SUCCESS;
static db_security_ret_code_t g_next_get_rc = DB_SAFETY_SUCCESS;

db_security_ret_code_t act_txn_begin(MDB_txn** out_txn, const unsigned flags, int* const out_err)
{
    (void)flags;
    if(out_txn) *out_txn = (MDB_txn*)0x500;
    if(out_err) *out_err = 0;
    return DB_SAFETY_SUCCESS;
}

db_security_ret_code_t act_txn_commit(MDB_txn* const txn, int* const out_err)
{
    (void)txn;
    if(out_err) *out_err = 0;
    return DB_SAFETY_SUCCESS;
}

db_security_ret_code_t act_put(MDB_txn* txn, op_t* op, int* const out_err)
{
    (void)txn;
    (void)op;
    if(out_err) *out_err = (g_next_put_rc == DB_SAFETY_FAIL) ? -EIO : 0;
    return g_next_put_rc;
}

db_security_ret_code_t act_get(MDB_txn* txn, op_t* op, int* const out_err)
{
    (void)txn;
    if(out_err) *out_err = (g_next_get_rc == DB_SAFETY_FAIL) ? -EIO : 0;

    if(g_next_get_rc == DB_SAFETY_SUCCESS)
    {
        /* Simulate a successful GET that populates op->val.present. */
        static char val[] = "X";
        op->val.kind         = OP_KEY_KIND_PRESENT;
        op->val.present.data  = val;
        op->val.present.size = sizeof(val);
    }
    else if(g_next_get_rc == DB_SAFETY_RETRY)
    {
        op->val.kind = OP_KEY_KIND_NONE;
    }

    return g_next_get_rc;
}

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static void ut_reset_all(void)
{
    ut_reset_lmdb_stubs();
    memset(&ops_cache, 0, sizeof(ops_cache));
    g_next_put_rc = DB_SAFETY_SUCCESS;
    g_next_get_rc = DB_SAFETY_SUCCESS;
}

/* ------------------------------------------------------------------------- */
/* _rw_cache_alloc() tests                                                   */
/* ------------------------------------------------------------------------- */

static void test_rw_cache_alloc_zero_size_returns_null(void** state)
{
    (void)state;

    ut_reset_all();

    assert_null(_rw_cache_alloc(0u));
    assert_int_equal(ops_cache.rw_cache_used, 0u);
}

static void test_rw_cache_alloc_within_capacity_advances_offset(void** state)
{
    (void)state;

    ut_reset_all();

    void* p1 = _rw_cache_alloc(16u);
    assert_non_null(p1);
    assert_int_equal(ops_cache.rw_cache_used, 16u);

    void* p2 = _rw_cache_alloc(32u);
    assert_non_null(p2);
    assert_true((char*)p2 > (char*)p1);
    assert_int_equal(ops_cache.rw_cache_used, 48u);
}

static void test_rw_cache_alloc_beyond_capacity_fails(void** state)
{
    (void)state;

    ut_reset_all();

    ops_cache.rw_cache_used = DB_RW_OPS_CACHE_SIZE - 4u;
    void* p = _rw_cache_alloc(8u);
    assert_null(p);
    assert_int_equal(ops_cache.rw_cache_used, DB_RW_OPS_CACHE_SIZE - 4u);
}

/* ------------------------------------------------------------------------- */
/* _exec_op() tests                                                          */
/* ------------------------------------------------------------------------- */

static void test_exec_op_invalid_type_fails(void** state)
{
    (void)state;

    ut_reset_all();

    op_t op;
    memset(&op, 0, sizeof(op));
    op.type = DB_OPERATION_NONE;

    int err = 0;
    db_security_ret_code_t rc = _exec_op((MDB_txn*)0x700, &op, &err);

    assert_int_equal(rc, DB_SAFETY_FAIL);
}

static void test_exec_op_put_delegates_to_act_put(void** state)
{
    (void)state;

    ut_reset_all();

    op_t op;
    memset(&op, 0, sizeof(op));
    op.type = DB_OPERATION_PUT;

    g_next_put_rc = DB_SAFETY_SUCCESS;

    int err = -1;
    db_security_ret_code_t rc = _exec_op((MDB_txn*)0x701, &op, &err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, 0);
}

static void test_exec_op_get_rw_caches_value(void** state)
{
    (void)state;

    ut_reset_all();

    ops_cache.kind = OPS_BATCH_KIND_RW;

    op_t op;
    memset(&op, 0, sizeof(op));
    op.type       = DB_OPERATION_GET;
    op.val.kind   = OP_KEY_KIND_NONE;

    int err = 0;
    db_security_ret_code_t rc = _exec_op((MDB_txn*)0x702, &op, &err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, 0);
    assert_int_equal(op.val.kind, OP_KEY_KIND_PRESENT);
    assert_non_null(op.val.present.data);
    assert_true(ops_cache.rw_cache_used > 0u);
}

static void test_exec_op_get_retry_propagates_retry(void** state)
{
    (void)state;

    ut_reset_all();

    ops_cache.kind = OPS_BATCH_KIND_RO;

    op_t op;
    memset(&op, 0, sizeof(op));
    op.type = DB_OPERATION_GET;

    g_next_get_rc = DB_SAFETY_RETRY;

    int err = 0;
    db_security_ret_code_t rc = _exec_op((MDB_txn*)0x703, &op, &err);

    assert_int_equal(rc, DB_SAFETY_RETRY);
}

/* ------------------------------------------------------------------------- */
/* _exec_ops() tests                                                         */
/* ------------------------------------------------------------------------- */

static void test_exec_ops_all_success_returns_success(void** state)
{
    (void)state;

    ut_reset_all();

    ops_cache.n_ops   = 2u;
    ops_cache.ops[0].type = DB_OPERATION_PUT;
    ops_cache.ops[1].type = DB_OPERATION_GET;

    g_next_put_rc = DB_SAFETY_SUCCESS;
    g_next_get_rc = DB_SAFETY_SUCCESS;

    int err = 0;
    db_security_ret_code_t rc = _exec_ops((MDB_txn*)0x710, &err);

    assert_int_equal(rc, DB_SAFETY_SUCCESS);
    assert_int_equal(err, 0);
}

static void test_exec_ops_stops_on_retry(void** state)
{
    (void)state;

    ut_reset_all();

    ops_cache.n_ops   = 2u;
    ops_cache.ops[0].type = DB_OPERATION_GET;
    ops_cache.ops[1].type = DB_OPERATION_PUT;

    g_next_get_rc = DB_SAFETY_RETRY;

    int err = 0;
    db_security_ret_code_t rc = _exec_ops((MDB_txn*)0x711, &err);

    assert_int_equal(rc, DB_SAFETY_RETRY);
}

/* ------------------------------------------------------------------------- */
/* ops_add_operation() / ops_execute_operations() tests                      */
/* ------------------------------------------------------------------------- */

static void test_ops_add_operation_rejects_null_input(void** state)
{
    (void)state;

    ut_reset_all();

    assert_int_equal(ops_add_operation(NULL), -EINVAL);
}

static void test_ops_add_operation_updates_batch_kind_and_validates_lookup(void** state)
{
    (void)state;

    ut_reset_all();

    op_t op;
    memset(&op, 0, sizeof(op));

    /* First op: GET => RO batch. */
    op.type          = DB_OPERATION_GET;
    op.key.kind      = OP_KEY_KIND_PRESENT;
    op.key.present.data  = (void*)"k";
    op.key.present.size = 1u;

    assert_int_equal(ops_add_operation(&op), 0);
    assert_int_equal(ops_cache.kind, OPS_BATCH_KIND_RO);
    assert_int_equal(ops_cache.n_ops, 1u);

    /* Second op: PUT => batch kind becomes RW. */
    op.type = DB_OPERATION_PUT;
    assert_int_equal(ops_add_operation(&op), 0);
    assert_int_equal(ops_cache.kind, OPS_BATCH_KIND_RW);
    assert_int_equal(ops_cache.n_ops, 2u);

    /* Invalid lookup index beyond n_ops. */
    op.type                  = DB_OPERATION_GET;
    op.key.kind              = OP_KEY_KIND_LOOKUP;
    op.key.lookup.op_index   = 10u;
    assert_int_equal(ops_add_operation(&op), -EINVAL);
}

static void test_ops_execute_operations_rejects_empty_cache(void** state)
{
    (void)state;

    ut_reset_all();

    assert_int_equal(ops_execute_operations(), -EINVAL);
}

static void test_ops_execute_operations_ro_uses_exec_ro_ops(void** state)
{
    (void)state;

    ut_reset_all();

    op_t op;
    memset(&op, 0, sizeof(op));
    op.type          = DB_OPERATION_GET;
    op.key.kind      = OP_KEY_KIND_PRESENT;
    op.key.present.data  = (void*)"k";
    op.key.present.size = 1u;

    assert_int_equal(ops_add_operation(&op), 0);
    assert_int_equal(ops_cache.kind, OPS_BATCH_KIND_RO);

    int rc = ops_execute_operations();
    assert_int_equal(rc, 0);
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rw_cache_alloc_zero_size_returns_null),
        cmocka_unit_test(test_rw_cache_alloc_within_capacity_advances_offset),
        cmocka_unit_test(test_rw_cache_alloc_beyond_capacity_fails),
        cmocka_unit_test(test_exec_op_invalid_type_fails),
        cmocka_unit_test(test_exec_op_put_delegates_to_act_put),
        cmocka_unit_test(test_exec_op_get_rw_caches_value),
        cmocka_unit_test(test_exec_op_get_retry_propagates_retry),
        cmocka_unit_test(test_exec_ops_all_success_returns_success),
        cmocka_unit_test(test_exec_ops_stops_on_retry),
        cmocka_unit_test(test_ops_add_operation_rejects_null_input),
        cmocka_unit_test(test_ops_add_operation_updates_batch_kind_and_validates_lookup),
        cmocka_unit_test(test_ops_execute_operations_rejects_empty_cache),
        cmocka_unit_test(test_ops_execute_operations_ro_uses_exec_ro_ops),
    };

    int rc = cmocka_run_group_tests(tests, NULL, NULL);

    if(rc == 0)
    {
        printf(UT_COLOR_GREEN "[UT] ops_exec tests: PASSED" UT_COLOR_RESET "\n");
    }
    else
    {
        printf(UT_COLOR_RED "[UT] ops_exec tests: FAILED (rc=%d)" UT_COLOR_RESET "\n", rc);
    }

    return rc;
}
