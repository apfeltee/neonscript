
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "mem.h"

    
#if defined(NEON_CONF_USEMEMPOOL) && (NEON_CONF_USEMEMPOOL == 1)
int64_t g_malloccount = 0;
int64_t g_realloccount = 0;
talstate_t* g_memstate;
#endif

void nn_memory_init()
{
    #if defined(NEON_CONF_USEMEMPOOL) && (NEON_CONF_USEMEMPOOL == 1)
    fprintf(stderr, "TALLOC_SMALL_TO = %ld\n", TALLOC_SMALL_TO);
    fprintf(stderr, "TALLOC_POOL_GROUP_MULT = %ld\n", TALLOC_POOL_GROUP_MULT);
    if(TALLOC_SMALL_TO <= TALLOC_POOL_GROUP_MULT)
    {
        fprintf(stderr, "TALLOC_SMALL_TO (%ld) must be larger than TALLOC_POOL_GROUP_MULT (%ld)\n", TALLOC_SMALL_TO, TALLOC_POOL_GROUP_MULT);
        abort();
    }
    g_memstate = tal_state_init();
    #endif
}

void nn_memory_finish()
{
    #if defined(NEON_CONF_USEMEMPOOL) && (NEON_CONF_USEMEMPOOL == 1)
        tal_state_destroy(g_memstate);
    #endif
}

void* nn_memory_setsize(void* p, size_t sz)
{
    size_t* sp;
    sp = (size_t*)p;
    *sp = sz;
    ++sp;
    memset(sp,0,sz);
    return sp;
}

size_t nn_memory_getsize(void * p)
{
    size_t* in = (size_t*)p;
    if (in)
    {
        --in;
        return *in;
    }
    return -1;
}


void* nn_memory_malloc(size_t sz)
{
    void* p;
    #if defined(NEON_CONF_USEMEMPOOL) && (NEON_CONF_USEMEMPOOL == 1)
        p = tal_state_malloc(g_memstate, sz);
    #else
        p = (void*)malloc(sz);
    #endif
    return p;
}

void* nn_memory_realloc(void* p, size_t nsz)
{
    void* retp;
    #if defined(NEON_CONF_USEMEMPOOL) && (NEON_CONF_USEMEMPOOL == 1)
        retp = (void*)tal_state_realloc(g_memstate, p, nsz);
    #else
        retp = (void*)realloc(p, nsz);
    #endif
    return retp;
}

void* nn_memory_calloc(size_t count, size_t typsize)
{
    void* p;
    #if defined(NEON_CONF_USEMEMPOOL) && (NEON_CONF_USEMEMPOOL == 1)
        p = (void*)tal_state_calloc(g_memstate, count, typsize);
    #else
        p = (void*)calloc(count, typsize);
    #endif
    return p;
}

void nn_memory_free(void* ptr)
{
    if(ptr == NULL)
    {
        return;
    }
    #if defined(NEON_CONF_USEMEMPOOL) && (NEON_CONF_USEMEMPOOL == 1)
        tal_state_free(g_memstate, ptr);
    #else
        free(ptr);
    #endif
}
