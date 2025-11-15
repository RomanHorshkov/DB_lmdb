/**
 * @file void_store.h
 * @brief Tiny segmented byte store used to build LMDB keys/values without copies.
 *
 * The store tracks up to @p n segments as (pointer, size) pairs. It does **not**
 * own the pointed data; it only describes where to read from when assembling a
 * contiguous buffer (e.g., for LMDB keys/values) or when applying a patch.
 *
 * - No implicit allocations on hot paths (except when caller requests a packed buffer).
 * - Segments may be NULL **iff** size>0, which means “skip N bytes” (useful for patches).
 * - Segment size must be >0. Zero-sized segments are rejected.
 *
 * Typical usage:
 * @code
 * void_store_t* st = NULL;
 * void_store_init(2, &st);
 * void_store_add(st, key_part1, len1);
 * void_store_add(st, key_part2, len2);
 * size_t klen = void_store_size(st);
 * void*   kbuf = void_store_malloc_buf(st); // packed, caller frees
 * // use kbuf/klen...
 * free(kbuf);
 * void_store_close(&st);
 * @endcode
 * 
 * @author Roman Horshkov <roman.horshkov@gmail.com>
 * @date   2025
 * (c) 2025
 */

#ifndef VOID_STORE_H
#define VOID_STORE_H

#include <stddef.h>  // size_t

#ifdef __cplusplus
extern "C"
{
#endif

/** Opaque container type (see implementation for layout). */
typedef struct void_store void_store_t;

/**
 * @brief Initialize an empty store capable of holding up to @p len segments.
 *
 * Allocates internal arrays and zero-initializes them.
 *
 * @param len  Maximum number of segments (must be > 0).
 * @param st   Out: on success, points to a valid store to be closed with
 *             @ref void_store_close. On failure, left unchanged.
 * @return 0 on success; -EINVAL on bad args; -ENOMEM on allocation failure.
 */
int void_store_init(size_t len, void_store_t** st);

/**
 * @brief Append a segment (pointer + length) to the store.
 *
 * Ownership of @p elem stays with the caller; memory must remain valid until
 * the store is serialized or no longer used. A NULL @p elem is allowed only
 * if @p elem_size > 0, and means “advance by @p elem_size bytes without copying”
 * (used by @ref void_store_apply_patch).
 *
 * @param st         Initialized store (non-NULL).
 * @param elem       Pointer to data (NULL allowed iff elem_size>0).
 * @param elem_size  Size in bytes (must be > 0).
 * @return 0 on success; -EINVAL on bad args; -ENOSPC if capacity exceeded.
 */
int void_store_add(void_store_t* st, const void* elem, size_t elem_size);

/**
 * @brief Free the store and its internal arrays; does **not** free segment data.
 *
 * After this call, *st is set to NULL.
 *
 * @param st Pointer to store pointer (may be NULL).
 */
void void_store_close(void_store_t** st);

/**
 * @brief Allocate and return a contiguous buffer with the concatenation
 *        of all segments in the store.
 *
 * @return Newly allocated buffer on success (caller must @c free), NULL on failure.
 *
 * @note Fails if the store is empty or if copying one segment fails validation.
 */
void* void_store_malloc_buf(void_store_t* st);

/**
 * @brief Total byte length represented by the store (sum of segment sizes).
 *
 * @param st Store (may be NULL).
 * @return Sum in bytes, or 0 if @p st is NULL/empty.
 */
size_t void_store_size(const void_store_t* st);

/**
 * @brief Copy all segments contiguously into @p dst.
 *
 * Validates that @p dst_len is sufficient and that each segment has a
 * non-zero length. Segments with a NULL pointer are allowed and mean
 * "skip N bytes" (useful for patch semantics). Returns the exact number
 * of bytes accounted for (sum of segment sizes) on success.
 *
 * @param dst     Output buffer.
 * @param dst_len Capacity of @p dst.
 * @param st      Store to serialize.
 * @return Bytes written on success; 0 on failure.
 */
size_t void_store_memcpy(void* dst, size_t dst_len, const void_store_t* st);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VOID_STORE_H
