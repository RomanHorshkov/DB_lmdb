/**
 * @file db_internal.h
 * @brief 
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025
 * (c) 2025
 */

#ifndef DB_LMDB_INTERNAL_H
#define DB_LMDB_INTERNAL_H

#include "db_lmdb.h"        /* interface dbi_decl_t */
#include "db_lmdb_config.h" /* config file */
#include "emlog.h"          /* EML_ERROR etc */

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */

/* Log LMDB ret with a stable format */
#define LMDB_LOG_ERR(tag, where, ret) \
    EML_ERROR("%s", "%s: ret=%d (%s)", tag, where, (ret), mdb_strerror(ret))

#define LMDB_EML_WARN(tag, where, ret) \
    EML_WARN("%s", "%s: ret=%d (%s)", tag, where, (ret), mdb_strerror(ret))

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES
 ****************************************************************************
*/
/* None */

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */

/**
 * @brief Map LMDB return codes to our DB error codes.
 * 
 * @param mdb_rc LMDB return code.
 * @return Mapped DB error code.
 */
int db_map_mdb_err(int mdb_rc);

/**
 * @brief Expand LMDB environment map size by DB_ENV_MAPSIZE_EXPAND_STEP bytes.
 * 
 * @return 0 on success, negative error code otherwise.
 */
int db_env_mapsize_expand(void);

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_INTERNAL_H */
