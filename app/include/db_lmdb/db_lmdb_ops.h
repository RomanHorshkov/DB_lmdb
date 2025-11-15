/**
 * @file db_operations.h
 * @brief Minimal, composable LMDB operation layer (PUT/GET/REP/DEL).
 *
 * Provides small, heap-free descriptors to chain database actions
 * into a single transaction via @ref ops_exec. Key/value material
 * is carried in @ref void_store_t segments, avoiding transient copies.
 *
 * Design goals:
 *  - Keep call sites compact and explicit.
 *  - No hidden allocations in hot paths (only result buffers on GET).
 *  - Clear behavior on DUPSORT databases (exact dup vs delete-all).
 *
 * Usage snippet:
 * @code
 * DB_operation_t* ops = ops_create(2);
 * ops_get_prepare(&ops[0], db_user_mail2id, email, strlen(email));
 * ops_get_prepare(&ops[1], db_user_id2pwd, NULL, 0); // key from prev->dst
 * size_t n = 2;
 * int rc = ops_exec(ops, &n);
 * if (rc == 0)
 * use ops[0].dst and ops[1].dst 
 * ops_free(&ops, &n);
 * @endcode
 */

#ifndef DB_OPERATIONS_H
#define DB_OPERATIONS_H

#include "db_internal.h" /* lmdb.h, stdio, stdlib, string, time */
#include "void_store.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @defgroup dbops DB Operation Layer
 * @brief Tiny building blocks for LMDB transactions.
 * @{
 */

/**
 * @brief Operation kind.
 */
typedef enum
{
    DB_OPERATION_NONE = 0, /**< Uninitialized placeholder. */
    DB_OPERATION_PUT,      /**< Insert/replace value; honors MDB flags. */
    DB_OPERATION_GET,      /**< Lookup by key; fills op->dst/op->dst_len. */
    DB_OPERATION_REP,      /**< In-place patch of existing value (cursor + RESERVE). */
    DB_OPERATION_LST,      /**< Reserved for future list/scan helpers. */
    DB_OPERATION_DEL,      /**< Delete by key or (key, dup-value). */
    DB_OPERATION_MAX
} DB_operation_type_t;

struct DB_operation
{
    DB_operation_type_t type;      /**< Operation kind. */
    MDB_dbi             dbi;       /**< Target DBI handle. */
    void_store_t*       key_store; /**< Key bytes store. */
    void_store_t*       val_store; /**< Concatenated value bytes/patch. */
    unsigned            flags;     /**< LMDB flags (e.g., MDB_NOOVERWRITE/MDB_APPEND). */

    struct DB_operation* prev;     /**< Previous op in chain (wired by ops_exec). */
    struct DB_operation* next;     /**< Next op in chain (wired by ops_exec). */

    void*  dst;                    /**< Result buffer for GET (owned by op). */
    size_t dst_len;                /**< Result length for GET. */
};

typedef struct DB_operation DB_operation_t;

/**
 * @brief Allocate a zero-initialized array of operations.
 *
 * @param[in] n_ops Number of elements.
 * @return Pointer to array or NULL on allocation failure.
 *
 * @note Free with @ref ops_free.
 */
DB_operation_t* ops_create(size_t n_ops);

/**
 * @brief Execute a single PUT operation (one-shot helper).
 * 
 * @param[in]  dbi   Target database handle.
 * @param[in]  key   Pointer to key bytes (non-NULL).
 * @param[in]  klen  Key length in bytes (>0).
 * @param[in]  val   Pointer to value bytes (non-NULL).
 * @param[in]  vlen  Value length in bytes (>0).
 * @param[in]  flags LMDB put flags (e.g., MDB_NOOVERWRITE).
 * 
 * @return 0 on success; negative errno on failure.
 */
int ops_put_one(MDB_dbi dbi, const void* key, size_t klen, const void* val, size_t vlen,
                unsigned flags);

/**
 * @brief Prepare a PUT with a fixed key and a value composed later.
 *
 * Initializes @p op as PUT and builds the key from @p key_seg.
 * Reserves space for @p nsegs value segments to be appended with
 * @ref ops_put_prepare_add.
 *
 * @param[out] op            Operation to initialize.
 * @param[in]  dbi           Target database.
 * @param[in]  key_seg       Pointer to key bytes.
 * @param[in]  key_seg_size  Size of key bytes (>0).
 * @param[in]  nsegs         Number of value segments to add.
 * @param[in]  flags         LMDB put flags (e.g., MDB_NOOVERWRITE).
 * @return 0 on success, negative errno on failure.
 */
int ops_put_prepare(DB_operation_t* op, MDB_dbi dbi, const void* key_seg, size_t key_seg_size,
                    size_t nsegs, unsigned flags);
/**
 * @brief Append one value segment to the prepared PUT.
 *
 * @param[in,out] op           A PUT operation prepared by ops_put_prepare.
 * @param[in]     val_seg      Pointer to value bytes (can be NULL for patch holes).
 * @param[in]     val_seg_size Segment size (>0).
 * @return 0 on success, negative errno on failure.
 */
int ops_put_prepare_add(DB_operation_t* op, const void* val_seg, size_t val_seg_size);

/** @} */

/**
 * @name GET operations
 * @{
 */

/**
 * @brief Prepare a GET.
 *
 * If @p key_seg is provided with @p seg_size>0, uses it as key.
 * Otherwise the key will be taken from @c op->prev->dst at exec time.
 *
 * @param[out] op        Operation to initialize.
 * @param[in]  dbi       Target database.
 * @param[in]  key_seg   Optional key bytes (may be NULL).
 * @param[in]  seg_size  Size of key (ignored if key_seg==NULL).
 * @return 0 on success, negative errno on failure.
 *
 * @note On success, @ref ops_exec allocates @c op->dst for the value.
 */
int ops_get_prepare(DB_operation_t* op, MDB_dbi dbi, const void* key_seg, size_t seg_size);

/**
 * @brief Single-shot GET for a key.
 *
 * Opens a read-only LMDB transaction, fetches the value for @p key, and
 * copies it into @p out when provided. If @p out is NULL or *out_len is
 * smaller than the stored value, the function writes the required size into
 * *out_len and returns without copying.
 *
 * @param[in]  dbi      Target database handle (must be valid).
 * @param[in]  key      Pointer to key bytes (non-NULL).
 * @param[in]  klen     Key length in bytes (>0).
 * @param[out] out      Optional destination buffer for the value (may be NULL to size-probe).
 * @param[in,out] out_len
 *                      In: capacity of @p out in bytes. Out: actual value size.
 *                      If @p out is NULL, only the size is returned here.
 *
 * @retval 0            Success (value copied or size reported).
 * @retval -ENOENT      Key not found (in this case, *out_len is set to 0 if provided).
 * @retval -ENOSPC      @p out provided but too small; *out_len holds required size.
 * @retval <0           Negative errno mapped from LMDB or internal errors.
 *
 * @note Uses a short, read-only transaction and never modifies the DB.
 * @note On success with copy, *out_len is set to the exact value size.
 */
int ops_get_one(MDB_dbi dbi, const void* key, size_t klen, void* out, size_t* out_len);

/** @} */

/**
 * @name REP (in-place replace) operations
 * @{
 */

/**
 * @brief Prepare an in-place replace (patch) operation.
 *
 * Key from @p key_seg (if provided) or from @c prev->dst otherwise.
 * Reserves @p nsegs segments describing the patch (see @ref void_store_memcpy
 * in `void_store.h` for patch semantics: NULL segments skip bytes, non-NULL
 * segments overwrite the destination buffer).
 *
 * @param[out] op            Operation to initialize.
 * @param[in]  dbi           Target database.
 * @param[in]  key_seg       Optional key bytes.
 * @param[in]  key_seg_size  Size of key bytes when provided.
 * @param[in]  nsegs         Number of patch segments.
 * @return 0 on success, negative errno on failure.
 */
int ops_rep_prepare(DB_operation_t* op, MDB_dbi dbi, const void* key_seg, size_t key_seg_size,
                    size_t nsegs);
/**
 * @brief Append one patch segment for REP.
 *
 * @param[in,out] op            A REP operation prepared by ops_rep_prepare.
 * @param[in]     val_seg       Bytes to write (NULL means skip region).
 * @param[in]     val_seg_size  Length in bytes (>0).
 * @return 0 on success, negative errno on failure.
 */
int ops_rep_prepare_add(DB_operation_t* op, const void* val_seg, size_t val_seg_size);

/** @} */

/**
 * @name LST/COMP placeholders
 * @{
 */
int ops_comp_prepare(void);
/* list */
int ops_lst_prepare(void);
/** @} */

/**
 * @name DEL operations
 * @{
 */

/**
 * @brief Prepare a delete.
 *
 * If the DBI is DUPSORT and no value is provided, all duplicates
 * for the key will be deleted. If a value is attached (via
 * @ref ops_del_prepare_add), only that exact duplicate is removed.
 *
 * @param[out] op            Operation to initialize.
 * @param[in]  dbi           Target database.
 * @param[in]  key_seg       Optional key bytes (or NULL to use prev->dst).
 * @param[in]  key_seg_size  Size of key when provided.
 * @param[in]  nsegs         0 or 1 value segment for dup-exact delete.
 * @return 0 on success, negative errno on failure.
 */
int ops_del_prepare(DB_operation_t* op, MDB_dbi dbi, const void* key_seg, size_t key_seg_size,
                    size_t nsegs);

/**
 * @brief Attach the value for an exact dup delete (DUPSORT only).
 *
 * @param[in,out] op            A DEL operation prepared by ops_del_prepare.
 * @param[in]     val_seg       Pointer to dup value bytes.
 * @param[in]     val_seg_size  Size in bytes (>0).
 * @return 0 on success, negative errno on failure.
 */
int ops_del_prepare_add(DB_operation_t* op, const void* val_seg, size_t val_seg_size);

/**
 * @brief Single-shot DEL for a key (and optional exact value).
 *
 * Begins a RW transaction and deletes:
 *  - if @p val==NULL: all values for @p key (normal DB or DUPSORT DB).
 *  - if @p val!=NULL: the exact (key,val) pair (requires DUPSORT DB).
 *
 * @param[in] dbi    Target database handle (must be valid).
 * @param[in] key    Pointer to key bytes (non-NULL).
 * @param[in] klen   Key length in bytes (>0).
 * @param[in] val    Optional value bytes for exact delete (may be NULL).
 * @param[in] vlen   Value length in bytes (0 if val==NULL).
 *
 * @retval 0        Success.
 * @retval -ENOENT  Key (or key,value) not found.
 * @retval <0       Negative errno mapped from LMDB or internal checks.
 */
int ops_del_one(MDB_dbi dbi, const void* key, size_t klen, const void* val, size_t vlen);
/** @} */

/**
 * @brief Execute a chain of operations in a single write transaction.
 *
 * Handles MDB_MAP_FULL by expanding the map and retrying the whole batch.
 * Links @p ops into a doubly-linked chain (prev/next) before execution.
 *
 * @param[in,out] ops   Array of operations.
 * @param[in,out] n_ops In: count; Out: unchanged (reserved).
 * @return 0 on success (committed), negative errno on failure (aborted).
 *
 * @warning On GET success, @c op->dst is heap-allocated and must be
 *          released by @ref ops_free (or manually by the caller).
 */
int ops_exec(DB_operation_t* ops, size_t* n_ops);

/**
 * @brief Free an array of operations and all owned resources.
 *
 * Behavior:
 *  - Closes and frees @c key_store / @c val_store of each op.
 *  - Frees any @c op->dst buffer allocated by GETs.
 *  - Frees the array and nulls the caller pointers.
 *
 * @param[in,out] ops   Pointer to array pointer.
 * @param[in,out] n_ops Pointer to count (will be set to 0).
 */
void ops_free(DB_operation_t** ops, size_t* n_ops);

/** @} */ /* end of group dbops */

#ifdef __cplusplus
}
#endif

#endif /* DB_OPERATIONS_H */
