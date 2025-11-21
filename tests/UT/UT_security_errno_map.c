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
 * _map_mdb_err_to_errno() directly while still compiling the same code
 * as production. */
#define static
#include "app/src/core/operations/ops_int/security/security.c"
#undef static

/* ------------------------------------------------------------------------- */
/* Tests                                                                     */
/* ------------------------------------------------------------------------- */

static void test_map_mdb_success_returns_zero(void** state)
{
    (void)state;
    assert_int_equal(_map_mdb_err_to_errno(MDB_SUCCESS), 0);
}

static void test_map_mdb_known_errors_to_errno(void** state)
{
    (void)state;

    struct
    {
        int rc;
        int expected;
    } cases[] = {
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
        { 1, -1 }, /* Unknow error uses rc */
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        int mapped = _map_mdb_err_to_errno(cases[i].rc);
        assert_int_equal(mapped, cases[i].expected);
    }
}


int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_map_mdb_success_returns_zero),
        cmocka_unit_test(test_map_mdb_known_errors_to_errno),
    };

    int rc = cmocka_run_group_tests(tests, NULL, NULL);

    if(rc == 0)
    {
        printf(UT_COLOR_GREEN "[UT] security errno mapping: PASSED" UT_COLOR_RESET "\n");
    }
    else
    {
        printf(UT_COLOR_RED "[UT] security errno mapping: FAILED (rc=%d)" UT_COLOR_RESET "\n", rc);
    }

    return rc;
}
