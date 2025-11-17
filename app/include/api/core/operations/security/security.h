

typedef enum
{
    DB_SAFETY_OK    = 0, /* proceed */
    DB_SAFETY_RETRY = 3, /* retry the operation after cleanup */
    DB_SAFETY_FAIL  = 7  /* fail with mapped errno */
} db_security_ret_code_t;

db_security_ret_code_t security_check(const int mdb_rc, int * const _Nullable out_errno);
