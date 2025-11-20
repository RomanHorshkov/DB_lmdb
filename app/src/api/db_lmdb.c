/**
 * @file db_lmdb.c
 * 
 * 
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025
 * (c) 2025
 */

#include "core.h" /* db_lmdb_create_env_safe etc */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "db_lmdb"

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
int main()
{
    db_core_init("path/to/db", 0644, NULL, 0);

    const unsigned int dbi_idx = 0;
    const op_type_t op_type = DB_OPERATION_PUT;
    const op_key_t   key = {0}
    const op_key_t   val = {0};
    

    db_core_op_add(dbi_idx, DB_OPERATION_PUT, &key, &val);

    db_core_shutdown();

    return 0;
}
/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
