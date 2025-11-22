#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "emlog.h"
#include "core/common.h"
#include "core.h"

int main(void)
{
    /* Initialize EMlogger with debug level and timestamps. */
    emlog_init(EML_LEVEL_DBG, false);

    const char* db_path = "./demo_db";

    const unsigned int n_dbis = 3u;

    /* Describe a single default DBI via simple arrays. */
    const char*      dbi_names[]  = { "user_dbi",
                                      "device_dbi",
                                      "auth_dbi" };
    const dbi_type_t dbi_types[]  = { DBI_TYPE_NOOVERWRITE,
                                      DBI_TYPE_NOOVERWRITE,
                                      DBI_TYPE_NOOVERWRITE | DBI_TYPE_APPENDABLE };

    int rc = db_core_init(db_path, DB_ENV_MODE, dbi_names, dbi_types, n_dbis);
    if(rc != 0)
    {
        fprintf(stderr, "db_core_init failed: %d\n", rc);
        return 1;
    }

    uint8_t auth_k1[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8_t auth_k2[8] = {0, 1, 2, 3, 4, 5, 6, 8};
    uint8_t auth_k3[8] = {0, 1, 2, 3, 4, 5, 6, 9};

    rc = db_core_set_op(0, DB_OPERATION_PUT,
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)"user_1",
                                                  .size = sizeof("user_1") } },
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)"device_1",
                                                  .size = sizeof("device_1") } });
    rc = db_core_set_op(1, DB_OPERATION_PUT,
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)"device_1",
                                                  .size = sizeof("device_1") } },
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)"device1_val",
                                                  .size = sizeof("device1_val") } });
    rc = db_core_set_op(2, DB_OPERATION_PUT,
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)auth_k1,
                                                  .size = sizeof(uint8_t) * 8 } },
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)"auth_val1",
                                                  .size = sizeof("auth_val1") } });
    rc = db_core_set_op(2, DB_OPERATION_PUT,
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)auth_k2,
                                                  .size = sizeof(uint8_t) * 8 } },
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)"auth_val2",
                                                  .size = sizeof("auth_val2") } });
    rc = db_core_set_op(2, DB_OPERATION_PUT,
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)auth_k3,
                                                  .size = sizeof(uint8_t) * 8 } },
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)"auth_val3",
                                                  .size = sizeof("auth_val3") } });

    rc = db_core_exec_ops();

    printf("db_core_exec_put returned: %d\n", rc);

    char val_buf[64] = {0}; /* buffer for get value */
    char val_buf2[64] = {0}; /* buffer for get value */

    rc = db_core_set_op(0, DB_OPERATION_GET,
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)"user_1",
                                                  .size = sizeof("user_1") } },
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)val_buf,
                                                  .size = sizeof(val_buf) } });
    rc = db_core_set_op(1, DB_OPERATION_GET,
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)"device_1",
                                                  .size = sizeof("device_1") } },
                         &(op_key_t){ .kind = OP_KEY_KIND_PRESENT,
                                     .present = { .data = (void*)val_buf2,
                                                  .size = sizeof(val_buf2) } });

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
