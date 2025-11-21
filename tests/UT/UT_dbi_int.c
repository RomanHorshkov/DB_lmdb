#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include <cmocka.h>

#include <lmdb.h>

#include "tests/UT/ut_env.h"
#include "core/operations/ops_int/db/dbi_int.h"

/* ------------------------------------------------------------------------- */
/* dbi_open_flags_from_type() tests                                          */
/* ------------------------------------------------------------------------- */

static void test_dbi_open_flags_default_type_uses_create_only(void** state)
{
    (void)state;
    ut_reset_lmdb_stubs();

    unsigned int flags = dbi_open_flags_from_type(DBI_TYPE_DEFAULT);
    assert_int_equal(flags, MDB_CREATE);
}

static void test_dbi_open_flags_dupsort_and_dupfixed_bits(void** state)
{
    (void)state;
    ut_reset_lmdb_stubs();

    unsigned int flags_dupsort = dbi_open_flags_from_type(DBI_TYPE_DUPSORT);
    assert_true((flags_dupsort & MDB_CREATE) != 0u);
    assert_true((flags_dupsort & MDB_DUPSORT) != 0u);
    assert_true((flags_dupsort & MDB_DUPFIXED) == 0u);

    unsigned int flags_dupfixed = dbi_open_flags_from_type(DBI_TYPE_DUPFIXED);
    assert_true((flags_dupfixed & MDB_CREATE) != 0u);
    assert_true((flags_dupfixed & MDB_DUPSORT) == 0u);
    assert_true((flags_dupfixed & MDB_DUPFIXED) != 0u);

    unsigned int flags_both = dbi_open_flags_from_type(DBI_TYPE_DUPSORT | DBI_TYPE_DUPFIXED);
    assert_true((flags_both & MDB_CREATE) != 0u);
    assert_true((flags_both & MDB_DUPSORT) != 0u);
    assert_true((flags_both & MDB_DUPFIXED) != 0u);
}

static void test_dbi_open_flags_ignores_nooverwrite_flag(void** state)
{
    (void)state;
    ut_reset_lmdb_stubs();

    unsigned int flags = dbi_open_flags_from_type(DBI_TYPE_NOOVERWRITE);
    assert_int_equal(flags, MDB_CREATE);
}

/* ------------------------------------------------------------------------- */
/* dbi_put_flags_from_type() tests                                           */
/* ------------------------------------------------------------------------- */

static void test_dbi_put_flags_default_type_returns_zero(void** state)
{
    (void)state;
    ut_reset_lmdb_stubs();

    unsigned int flags = dbi_put_flags_from_type(DBI_TYPE_DEFAULT);
    assert_int_equal(flags, 0u);
}

static void test_dbi_put_flags_nooverwrite_sets_mdb_nooverwrite(void** state)
{
    (void)state;
    ut_reset_lmdb_stubs();

    unsigned int flags = dbi_put_flags_from_type(DBI_TYPE_NOOVERWRITE);
    assert_int_equal(flags, MDB_NOOVERWRITE);
}

static void test_dbi_put_flags_nooverwrite_combined_with_other_bits(void** state)
{
    (void)state;
    ut_reset_lmdb_stubs();

    dbi_type_t type = DBI_TYPE_NOOVERWRITE | DBI_TYPE_DUPSORT | DBI_TYPE_DUPFIXED;
    unsigned int flags = dbi_put_flags_from_type(type);
    assert_int_equal(flags, MDB_NOOVERWRITE);
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dbi_open_flags_default_type_uses_create_only),
        cmocka_unit_test(test_dbi_open_flags_dupsort_and_dupfixed_bits),
        cmocka_unit_test(test_dbi_open_flags_ignores_nooverwrite_flag),
        cmocka_unit_test(test_dbi_put_flags_default_type_returns_zero),
        cmocka_unit_test(test_dbi_put_flags_nooverwrite_sets_mdb_nooverwrite),
        cmocka_unit_test(test_dbi_put_flags_nooverwrite_combined_with_other_bits),
    };

    int rc = cmocka_run_group_tests(tests, NULL, NULL);

    if(rc == 0)
    {
        printf(UT_COLOR_GREEN "[UT] dbi_int tests: PASSED" UT_COLOR_RESET "\n");
    }
    else
    {
        printf(UT_COLOR_RED "[UT] dbi_int tests: FAILED (rc=%d)" UT_COLOR_RESET "\n", rc);
    }

    return rc;
}
