/**
 * @file security.h
 * @brief Centralized LMDB return-code policy and retry/resize guidance.
 */

#ifndef DB_LMDB_SECURITY_H
#define DB_LMDB_SECURITY_H

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
*/
/* None */

/****************************************************************************
 * PUBLIC STRUCTURED TYPES
 ****************************************************************************
*/
typedef enum
{
    DB_SAFETY_OK    = 0, /* proceed */
    DB_SAFETY_RETRY = 3, /* retry */
    DB_SAFETY_FAIL  = 7  /* fail */
} db_security_ret_code_t;

/************************************************************************
 * PUBLIC VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Check LMDB return code and decide action.
 * 
 * @param mdb_rc     LMDB return code.
 * @param out_errno  Optional mapped errno on failure.
 * @return DB_SAFETY_OK/RETRY/FAIL
 */
db_security_ret_code_t security_check(const int mdb_rc, int* const out_errno);

#ifdef __cplusplus
}
#endif

#endif /* DB_LMDB_SECURITY_H */
