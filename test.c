#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "emlog.h"
#include "core/common.h"
#include "core.h"

int main(void)
{
    const char* db_path = "./demo_db";

    const unsigned int n_dbis = 3u;

    /* Describe a single default DBI via simple arrays. */
    const char*      dbi_names[]  = { "user_dbi",
                                      "device_dbi",
                                      "auth_dbi" };
    const dbi_type_t dbi_types[]  = { DBI_TYPE_NOOVERWRITE,
                                      DBI_TYPE_NOOVERWRITE,
                                      DBI_TYPE_NOOVERWRITE };

    int rc = db_core_init(db_path, DB_LMDB_ENV_MODE, dbi_names, dbi_types, n_dbis);
    if(rc != 0)
    {
        fprintf(stderr, "db_core_init failed: %d\n", rc);
        return 1;
    }
    /* Initialize EMlogger with debug level and timestamps. */
    emlog_init(EML_LEVEL_DBG, true);

    rc = db_core_add_op(0, DB_OPERATION_PUT,
                         (const void*)"user_1", sizeof("user_1"),
                         (const void*)"user1val", sizeof("user1val"));
    rc = db_core_add_op(1, DB_OPERATION_PUT,
                         (const void*)"device_1", sizeof("device_1"),
                         (const void*)"device1_val", sizeof("device1_val"));
    rc = db_core_add_op(2, DB_OPERATION_PUT,
                         (const void*)"auth_1", sizeof("auth_1"),
                         (const void*)"auth1_val", sizeof("auth1_val"));

    rc = db_core_exec_ops();

    printf("db_core_exec_put returned: %d\n", rc);

    char val_buf[64] = {0}; /* buffer for get value */
    char val_buf2[64] = {0}; /* buffer for get value */

    rc = db_core_add_op(0, DB_OPERATION_GET,
                         (const void*)"user_1", sizeof("user_1"),
                         val_buf, 64);
    rc = db_core_add_op(1, DB_OPERATION_GET,
                         (const void*)"device_1", sizeof("device_1"),
                         val_buf2, 64);
    rc = db_core_add_op(2, DB_OPERATION_GET,
                         (const void*)"auth_1", sizeof("auth_1"),
                         NULL, sizeof("auth1_val")); /* no user buffer */

    rc = db_core_exec_ops();

    if(rc == 0)
    {
        printf("GET operation successful, value1: %s, value2: %s \n", val_buf, val_buf2);
    }
    else
    {
        printf("GET operation failed with code: %d\n", rc);
    }

    /* For now just init + shutdown; ops wiring will come next. */
    size_t final_mapsize = db_core_shutdown();
    printf("db_core_shutdown: final mapsize=%zu bytes\n", final_mapsize);

    return 0;
}
