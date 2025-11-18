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

#include "config.h" /* config file */
#include "emlog.h"  /* EML_ERROR etc, errno, stdint, stddef, sys/types  */

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */

/* Log LMDB ret with a stable format */
#define LMDB_EML_ERR(tag, where, ret) \
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

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_INTERNAL_H */
