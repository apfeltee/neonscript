
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ifexpr ? thenexpr : elseexpr
#define _da_if(...) (__VA_ARGS__) ?
#define _da_then(...) (__VA_ARGS__) :
#define _da_else(...) (__VA_ARGS__)

#ifndef JK_DYNARRAY_MAX
    #define JK_DYNARRAY_MAX(a, b) \
        ( \
            _da_if((a) > (b)) \
            _da_then(a) \
            _da_else(b) \
        )
#endif

#ifndef JK_DYNARRAY_MIN
    #define JK_DYNARRAY_MIN(a, b) \
        ( \
            _da_if((a) < (b)) \
            _da_then(a) \
            _da_else(b) \
        )
#endif


extern void* ds_extmalloc(size_t size, void* userptr);
extern void* ds_extrealloc(void* ptr, size_t oldsz, size_t newsz, void* userptr);
extern void ds_extfree(void* ptr, void* userptr);

static APE_INLINE intptr_t* da_grow_internal(void* uptr, intptr_t* arr, size_t capacity, size_t tsize);

#define da_count_internal(arr) \
    ((((intptr_t*)(arr)) - 2)[0])

#define da_capacity_internal(arr) \
    ((((intptr_t*)(arr)) - 1)[0])


#define da_need_to_grow_internal(arr, n) \
    ((!(arr)) || (da_count_internal(arr) + (n)) >= da_capacity_internal(arr))

#define da_maybe_grow_internal(uptr, arr, n) \
    ( \
        _da_if( \
            da_need_to_grow_internal((intptr_t*)(arr), (n)) \
        ) \
        _da_then( \
            (arr) = (intptr_t*)da_grow_internal(uptr, (intptr_t*)(arr), (n), sizeof(*(arr))) \
        ) \
        _da_else(0) \
    )

#define da_make(uptr, arr, n, sztyp) \
    ( \
        da_grow_internal(uptr, (intptr_t*)arr, n, sztyp) \
    )

#define da_destroy(uptr, arr) \
    ( \
        _da_if(arr) \
        _da_then( \
            ds_extfree(((intptr_t*)(arr)) - 2, uptr), (arr) = NULL, 0 \
        ) \
        _da_else(0) \
    )

#define da_clear(arr) \
    (\
        _da_if(da_count(arr) > 0) \
        _da_then( \
            memset((arr), 0, sizeof(*(arr)) * da_count_internal(arr)), \
            da_count_internal(arr) = 0, \
            0 \
        ) \
        _da_else(0) \
    )

#define da_count(arr) \
    ((unsigned int)( \
        _da_if((arr) == NULL) \
        _da_then(0) \
        _da_else( \
            da_count_internal(arr) \
        ) \
    ))

#define da_capacity(arr) \
    ((unsigned int)( \
        _da_if(arr) \
        _da_then(da_capacity_internal(arr)) \
        _da_else(0) \
    ))

#define da_get(arr, idx) \
    ( \
        _da_if((idx) <= da_count(arr)) \
        _da_then( \
            (void*)( \
                (arr)[(idx)] \
            ) \
        ) \
        _da_else( \
            (void*)(NULL) \
        ) \
    )

#define da_set(arr, idx, val) \
    ( \
        _da_if((idx) < da_count(arr)) \
        _da_then( \
            ( \
                (arr)[idx] = (intptr_t)val \
            ), \
            true \
        ) \
        _da_else( \
            false \
        ) \
    )

#define da_last(arr) \
    (void*)( \
        (arr)[da_count_internal(arr) - 1] \
    )

#define da_push(uptr, arr, ...) \
    ( \
        da_maybe_grow_internal(uptr, (arr), APE_CONF_PLAINLIST_CAPACITY_ADD), \
        ((intptr_t*)(arr))[da_count_internal(arr)++] = (intptr_t)(__VA_ARGS__) \
    )

#define da_pushn(uptr, arr, n) \
    ( \
        da_maybe_grow_internal(uptr, (arr), n), \
        da_count_internal(arr) += n \
    )


#define da_pop(arr) \
    ( \
        _da_if(da_count(arr) > 0) \
        _da_then( \
            memset((arr) + (--da_count_internal(arr)), 0, sizeof(*(arr))), \
            0 \
        ) \
        _da_else(0) \
    )

#define da_popn(arr, n) \
    (\
        _da_if(da_count(arr) > 0) \
        _da_then( \
            memset( \
                (arr) + (da_count_internal(arr) - JK_DYNARRAY_MIN((n), da_count_internal(arr))), \
                0, \
                sizeof(*(arr)) * (JK_DYNARRAY_MIN((n), da_count_internal(arr)))\
            ), \
            da_count_internal(arr) -= JK_DYNARRAY_MIN((n), \
            da_count_internal(arr)), 0 \
        ) \
        _da_else(0) \
    )

#define da_grow(uptr, arr, n) \
    ( \
        ((arr) = da_grow_internal(uptr, (intptr_t*)(arr), (n), sizeof(*(arr)))), \
        da_count_internal(arr) += (n) \
    )

#define da_remove_swap_last(arr, index) \
    ( \
        _da_if(((index) >= 0) && (index) < da_count_internal(arr)) \
        _da_then( \
            memcpy((arr) + (index), &da_last(arr), sizeof(*(arr))), \
            --da_count_internal(arr) \
        ) \
        _da_else(0)\
    )

#define da_sizeof(arr) (sizeof(*(arr)) * da_count(arr))

static APE_INLINE void da_copy(void* uptr, intptr_t* from, intptr_t* to, unsigned int begin, unsigned int end)
{
    unsigned int i;
    for(i=begin; i<end; i++)
    {
        da_maybe_grow_internal(uptr, to, APE_CONF_PLAINLIST_CAPACITY_ADD);
        to[i] = from[i];
    }
}

static APE_INLINE void da_removeat(intptr_t* from, size_t ix)
{
    (void)from;
    (void)ix;
}

/*
#undef _da_if
#undef _da_then
#undef _da_else
*/

static APE_INLINE intptr_t* da_grow_internal(void* uptr, intptr_t* arr, size_t capacity, size_t tsize)
{
    size_t asize;
    size_t acount;
    size_t zerosize;
    size_t actualcapacity;
    intptr_t* res;
    intptr_t* ptr;
    res = NULL;
    acount = 0;
    actualcapacity = capacity;
    tsize = tsize + sizeof(intptr_t);
    if(actualcapacity == 0)
    {
        actualcapacity = 1;
    }
    if(arr != NULL)
    {
        acount = JK_DYNARRAY_MAX(2 * da_count(arr), da_count(arr) + actualcapacity);
    }
    asize = ((2 * tsize) + (acount * tsize));


    //fprintf(stderr, "da_grow_internal: arg.uptr=%p arg.arr=%p arg.capacity=%zu arg.tsize=%zu asize=%zu acount=%zu\n", uptr, arr, actualcapacity, tsize, asize, acount);


    if(arr)
    {
        ptr = (intptr_t*)ds_extmalloc(asize * tsize, uptr);
        if(ptr)
        {
            memcpy(ptr, ((intptr_t*)arr) - 2, ((da_count(arr) * tsize) + (2 * tsize)));
            da_destroy(uptr, arr);
        }
        assert(ptr != NULL);
        zerosize = ((asize - (2 * tsize)) - (ptr[0] * tsize));
        memset(((intptr_t*)ptr) + (asize - zerosize), 0, zerosize);
        res = (intptr_t*)(ptr + 2);
        da_capacity_internal(res) = acount;
    }
    else
    {
        ptr = (intptr_t*)ds_extmalloc(asize*tsize, uptr);
        assert(ptr != NULL);
        res = (intptr_t*)(ptr + 2);
        memset(ptr, 0, asize);
        da_count_internal(res) = 0;
        da_capacity_internal(res) = acount;
    }
    assert(res != NULL);
    return res;
}

