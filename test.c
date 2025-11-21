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


    /* Describe a single default DBI via simple arrays. */
    const char*      dbi_names[]  = { "demo_dbi" };
    const dbi_type_t dbi_types[]  = { DBI_TYPE_NOOVERWRITE };

    int rc = db_core_init(db_path, DB_LMDB_ENV_MODE, dbi_names, dbi_types, 1);
    if(rc != 0)
    {
        fprintf(stderr, "db_core_init failed: %d\n", rc);
        return 1;
    }
    /* Initialize EMlogger with debug level and timestamps. */
    emlog_init(EML_LEVEL_DBG, true);

    rc = db_core_add_op(0, DB_OPERATION_PUT,
                         (const void*)"sample_gay", sizeof("sample_gay"),
                         (const void*)"sample_values", sizeof("sample_values"));

    rc = db_core_exec_ops();

    printf("db_core_exec_put returned: %d\n", rc);

    rc = db_core_add_op(0, DB_OPERATION_GET,
                         (const void*)"sample_gay", sizeof("sample_gay"),
                         NULL, 0);

    rc = db_core_exec_ops();
    printf("db_core_exec_get returned: %d\n", rc);

    /* For now just init + shutdown; ops wiring will come next. */
    size_t final_mapsize = db_core_shutdown();
    printf("db_core_shutdown: final mapsize=%zu bytes\n", final_mapsize);

    return 0;
}
