/**
 * @file void_store.c
 * @brief Implementation of a tiny segmented byte store.
 */
#include "void_store.h"
#include "emlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define TAG "void_st"

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */

/** Internal representation. */
struct void_store
{
    size_t       n_elements;   /**< Number of managed slots */
    size_t       n_max;        /**< Max manageable slots */
    size_t       tot_size;     /**< Total size */
    const void** elements;     /**< Pointer array of length 'size' */
    size_t*      element_size; /**< Size array of length 'size' */
};

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int void_store_init(size_t len, void_store_t** st)
{
    if(len <= 0)
    {
        EML_ERROR(TAG, "init: wrong len");
        return -EINVAL;
    }

    void_store_t* store = calloc(1, sizeof(void_store_t));

    if(!store)
    {
        EML_ERROR(TAG, "init: calloc(store) failed");
        return -ENOMEM;
    }

    /* initialize max slots */
    store->n_max = len;

    /* Zero-initialized arrays */
    store->elements     = (const void**)calloc(len, sizeof(const void*));
    store->element_size = (size_t*)calloc(len, sizeof(size_t));

    /* If either allocation failed, clean up and return an invalid store */
    if(!store->elements || !store->element_size)
    {
        EML_ERROR(TAG, "init: calloc arrays failed");
        free(store->elements);
        free(store->element_size);
        free(store);
        return -ENOMEM;
    }

    /* assign the store */
    *st = store;
    return 0;
}

int void_store_add(void_store_t* st, const void* elem, size_t elem_size)
{
    if(!st)
    {
        EML_ERROR(TAG, "add: st=NULL");
        return -EINVAL;
    }
    if(st->n_elements >= st->n_max)
    {
        EML_ERROR(TAG, "add: capacity full (n=%zu max=%zu)", st->n_elements, st->n_max);
        return -ENOSPC;
    }

    /* allow elem == NULL ONLY if elem_size>0 (used by patch to skip bytes) */
    if(elem_size == 0)
    {
        EML_ERROR(TAG, "add: elem_size=0 not allowed");
        return -EINVAL;
    }

    /* set element ptr */
    st->elements[st->n_elements]      = elem;
    /* sel element size */
    st->element_size[st->n_elements]  = elem_size;
    /* add size to tot */
    st->tot_size                     += elem_size;
    /* increase elements counter */
    st->n_elements++;

    return 0;
}

void void_store_close(void_store_t** st)
{
    if(!st || !*st) return;
    void_store_t* store = *st;
    if(store->elements) free(store->elements);
    if(store->element_size) free(store->element_size);
    store->elements     = NULL;
    store->element_size = NULL;
    store->n_elements   = 0;
    store->n_max        = 0;
    store->tot_size     = 0;
    free(store);
    *st = NULL;
}

void* void_store_malloc_buf(void_store_t* st)
{
    if(!st)
    {
        EML_ERROR(TAG, "malloc_buf: st=NULL");
        return NULL;
    }

    const size_t len = void_store_size(st);
    if(len == 0)
    {
        EML_ERROR(TAG, "malloc_buf: empty store (len=0)");
        return NULL;
    }

    void* buf = calloc(1, len);
    if(!buf)
    {
        EML_ERROR(TAG, "malloc_buf: malloc(%zu) failed", len);
        return NULL;
    }

    const size_t copied = void_store_memcpy(buf, len, st);
    if(copied != len)
    {
        EML_ERROR(TAG, "malloc_buf: memcpy mismatch copied=%zu expected=%zu", copied, len);
        free(buf);
        return NULL;
    }

    return buf;
}

size_t void_store_size(const void_store_t* st)
{
    return st ? st->tot_size : 0;
}

size_t void_store_memcpy(void* dst, size_t dst_len, const void_store_t* st)
{
    if(!dst)
    {
        EML_ERROR(TAG, "memcpy: dst=NULL");
        return 0;
    }
    if(!st)
    {
        EML_ERROR(TAG, "memcpy: st=NULL");
        return 0;
    }
    if(!st->elements || !st->element_size)
    {
        EML_ERROR(TAG, "memcpy: store arrays not initialized");
        return 0;
    }

    const size_t need = void_store_size(st);
    if(need == 0)
    {
        EML_ERROR(TAG, "memcpy: need=0");
        return 0;
    }
    if(need > dst_len)
    {
        EML_ERROR(TAG, "memcpy: dst too small (need=%zu, dst=%zu)", need, dst_len);
        return 0;
    }

    unsigned char* out = (unsigned char*)dst;
    size_t         off = 0;

    for(size_t i = 0; i < st->n_elements; ++i)
    {
        const void*  p = st->elements[i];
        const size_t n = st->element_size[i];

        if(n == 0)
        {
            EML_ERROR(TAG, "memcpy: segment %zu has size 0", i);
            return 0;
        }
        if(!p)
        {
            /* NULL segment: skip */
            off += n;
            continue;
        }
        else
        {
            memcpy(out + off, p, n);
            off += n;
        }
    }

    if(off != need)
    {
        EML_ERROR(TAG, "memcpy: off(%zu) != need(%zu)", off, need);
        return 0;
    }
    return off;
}
