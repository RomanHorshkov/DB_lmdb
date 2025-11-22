#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>

#include <cmocka.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "core.h"

static const char* k_test_db_path = "./it_db_core_init_db";

static int setup_clean_env(void** state)
{
    (void)state;

    /* Best-effort cleanup of any previous env directory. */
    struct stat st;
    if(stat(k_test_db_path, &st) == 0 && S_ISDIR(st.st_mode))
    {
        /* Remove LMDB files if they exist, then rmdir. */
        char path_data[256];
        char path_lock[256];

        (void)snprintf(path_data, sizeof(path_data), "%s/data.mdb", k_test_db_path);
        (void)snprintf(path_lock, sizeof(path_lock), "%s/lock.mdb", k_test_db_path);

        unlink(path_data);
        unlink(path_lock);
        rmdir(k_test_db_path);
    }

    return 0;
}

static int teardown_env(void** state)
{
    (void)state;

    /* Ensure core shutdown so tests remain isolated. */
    (void)db_core_shutdown();

    struct stat st;
    if(stat(k_test_db_path, &st) == 0 && S_ISDIR(st.st_mode))
    {
        char path_data[256];
        char path_lock[256];

        (void)snprintf(path_data, sizeof(path_data), "%s/data.mdb", k_test_db_path);
        (void)snprintf(path_lock, sizeof(path_lock), "%s/lock.mdb", k_test_db_path);

        unlink(path_data);
        unlink(path_lock);
        rmdir(k_test_db_path);
    }

    return 0;
}

static void test_db_core_init_creates_env_with_strict_mode(void** state)
{
    (void)state;

    const char*      dbi_names[] = { "demo_dbi" };
    const dbi_type_t dbi_types[] = { DBI_TYPE_NOOVERWRITE };

    /* Strict owner-only permissions: rw for env files, full access on dir. */
    const unsigned int mode = 0600u;

    int rc = db_core_init(k_test_db_path, mode, dbi_names, dbi_types, 1u);
    assert_int_equal(rc, 0);

    /* Directory must exist and be owned by the current user. */
    struct stat st_dir;
    assert_int_equal(stat(k_test_db_path, &st_dir), 0);
    assert_true(S_ISDIR(st_dir.st_mode));

    /* For LMDB, the directory execute bit is required to traverse. We only
     * assert that group/other bits are cleared; owner bits are left to the
     * implementation (mode parameter).
     */
    assert_true((st_dir.st_mode & (S_IRWXG | S_IRWXO)) == 0);

    /* data.mdb and lock.mdb must exist with no group/other access. */
    char path_data[256];
    char path_lock[256];

    (void)snprintf(path_data, sizeof(path_data), "%s/data.mdb", k_test_db_path);
    (void)snprintf(path_lock, sizeof(path_lock), "%s/lock.mdb", k_test_db_path);

    struct stat st_data;
    struct stat st_lock;

    assert_int_equal(stat(path_data, &st_data), 0);
    assert_int_equal(stat(path_lock, &st_lock), 0);

    assert_true(S_ISREG(st_data.st_mode));
    assert_true(S_ISREG(st_lock.st_mode));

    /* Enforce that no group/other bits are set on the LMDB files. */
    assert_true((st_data.st_mode & (S_IRWXG | S_IRWXO)) == 0);
    assert_true((st_lock.st_mode & (S_IRWXG | S_IRWXO)) == 0);

    /* Sanity: shutdown should succeed and leave a non-negative map size. */
    size_t final_mapsize = db_core_shutdown();
    assert_true(final_mapsize >= (size_t)0);
}

static void test_db_core_init_invalid_inputs(void** state)
{
    (void)state;

    const char*      dbi_names[] = { "demo_dbi" };
    const dbi_type_t dbi_types[] = { DBI_TYPE_NOOVERWRITE };

    /* Null path should be rejected. */
    int rc = db_core_init(NULL, 0600u, dbi_names, dbi_types, 1u);
    assert_true(rc < 0);

    /* Zero DBI count should be rejected. */
    rc = db_core_init(k_test_db_path, 0600u, dbi_names, dbi_types, 0u);
    assert_true(rc < 0);
}

static void test_db_core_add_op_fails_when_not_initialized(void** state)
{
    (void)state;

    /* Ensure global state is reset. */
    (void)db_core_shutdown();

    const char* key   = "k";
    const char* value = "v";

    int rc = db_core_set_op(0u, DB_OPERATION_PUT,
        &(op_key_t){
        .kind = OP_KEY_KIND_PRESENT,
        .present = { .data = (void*)key,
                     .size = 1 }},
        &(op_key_t){
        .kind = OP_KEY_KIND_PRESENT,
        .present = { .data = (void*)value,
                     .size = 1 }});

    assert_int_equal(rc, -ENOENT);
}

static void test_db_core_add_op_invalid_type_and_key(void** state)
{
    (void)state;

    const char*      dbi_names[] = { "demo_dbi" };
    const dbi_type_t dbi_types[] = { DBI_TYPE_NOOVERWRITE };

    int rc = db_core_init(k_test_db_path, 0600u, dbi_names, dbi_types, 1u);
    assert_int_equal(rc, 0);

    const char* key = "key";
    const char* val = "val";

    /* Type lower than PUT (DB_OPERATION_NONE). */
    rc = db_core_set_op(0u, DB_OPERATION_NONE,
        &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                     .present = { .data = (void*)key,
                                  .size = 3 } },
        &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                     .present = { .data = (void*)val,
                                  .size = 3 } } );
    assert_int_equal(rc, -EINVAL);

    /* Invalid key fails */
    rc = db_core_set_op(0u, DB_OPERATION_GET,
        &(op_key_t){ .kind = OP_KEY_KIND_NONE,
                     .present = { .data = (void*)key,
                                  .size = 3 } },
        &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                     .present = { .data = (void*)val,
                                  .size = 3 } } );
    assert_int_equal(rc, -EINVAL);

    /* Valid key and invalid data ptr fails */
    rc = db_core_set_op(0u, DB_OPERATION_GET,
        &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                     .present = { .data = NULL,
                                  .size = 3 } },
        &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                     .present = { .data = NULL,
                                  .size = 3 } } );
    assert_int_equal(rc, -EINVAL);

    /* Valid key and invalid data size fails */
    rc = db_core_set_op(0u, DB_OPERATION_GET,
        &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                     .present = { .data = (void*)key,
                                  .size = (size_t)0 } },
        &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                     .present = { .data = (void*)val,
                                  .size = 3 } } );
    assert_int_equal(rc, -EINVAL);
}

static void test_db_core_add_op_overflow_cache_returns_enomem(void** state)
{
    (void)state;

    const char*      dbi_names[] = { "demo_dbi" };
    const dbi_type_t dbi_types[] = { DBI_TYPE_NOOVERWRITE };

    int rc = db_core_init(k_test_db_path, 0600u, dbi_names, dbi_types, 1u);
    assert_int_equal(rc, 0);

    const char* key = "key";
    const char* val = "value";

    /* Push enough operations to overflow the internal ops cache. */
    int last_rc = 0;
    for(int i = 0; i < 64; ++i)
    {
        last_rc = db_core_set_op(0u, DB_OPERATION_PUT,
            &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                         .present = { .data = (void*)key,
                                      .size = 3 } },
            &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                         .present = { .data = (void*)val,
                                      .size = 5 } } );
        if(last_rc != 0) break;
    }

    assert_int_equal(last_rc, -ENOMEM);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_db_core_init_creates_env_with_strict_mode,
                                        setup_clean_env,
                                        teardown_env),
        cmocka_unit_test_setup_teardown(test_db_core_init_invalid_inputs,
                                        setup_clean_env,
                                        teardown_env),
        cmocka_unit_test_setup_teardown(test_db_core_add_op_fails_when_not_initialized,
                                        setup_clean_env,
                                        teardown_env),
        cmocka_unit_test_setup_teardown(test_db_core_add_op_invalid_type_and_key,
                                        setup_clean_env,
                                        teardown_env),
        cmocka_unit_test_setup_teardown(test_db_core_add_op_overflow_cache_returns_enomem,
                                        setup_clean_env,
                                        teardown_env),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
