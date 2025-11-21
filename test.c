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

    /* Initialize EMlogger with debug level and timestamps. */
    emlog_init(EML_LEVEL_DBG, true);

    /* Describe a single default DBI via simple arrays. */
    const char*      dbi_names[]  = { "demo_dbi" };
    const dbi_type_t dbi_types[]  = { DBI_TYPE_NOOVERWRITE };

    int rc = db_core_init(db_path, DB_LMDB_ENV_MODE, dbi_names, dbi_types, 1);
    if(rc != 0)
    {
        fprintf(stderr, "db_core_init failed: %d\n", rc);
        return 1;
    }

    rc = db_core_add_op(0, DB_OPERATION_PUT,
                         (const void*)"sample_gay", sizeof("sample_key"),
                         (const void*)"sample_values", sizeof("sample_value"));

    printf("db_core_add_op returned: %d\n", rc);

    rc = db_core_exec_ops();

    printf("db_core_exec_ops returned: %d\n", rc);

    // rc = db_core_add_op(0, DB_OPERATION_GET,
    //                      (const void*)"sample_key", sizeof("sample_key"),
    //                      NULL, 0);

    // rc = db_core_exec_ops();

    /* For now just init + shutdown; ops wiring will come next. */
    size_t final_mapsize = db_core_shutdown();
    printf("db_core_shutdown: final mapsize=%zu bytes\n", final_mapsize);

    return 0;
}
