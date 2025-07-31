
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "mem.h"

#if !defined(NEON_CONF_MEMUSEALLOCATOR) || (NEON_CONF_MEMUSEALLOCATOR == 0)
    #define NEON_CONF_MEMUSEALLOCATOR 1
#endif

#if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
    void* g_msp = NULL;
#endif


void nn_memory_init()
{
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        g_msp = nn_allocator_create();
    #endif
}

void nn_memory_finish()
{
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        nn_allocator_destroy(g_msp);
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
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        p = (void*)nn_allocuser_malloc(g_msp, sz);
    #else
        p = (void*)malloc(sz);
    #endif
    return p;
}

void* nn_memory_realloc(void* p, size_t nsz)
{
    void* retp;
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        if(p == NULL)
        {
            return nn_memory_malloc(nsz);
        }
        retp = (void*)nn_allocuser_realloc(g_msp, p, nsz);
    #else
        retp = (void*)realloc(p, nsz);
    #endif
    return retp;
}

void* nn_memory_calloc(size_t count, size_t typsize)
{
    void* p;
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        p = (void*)nn_allocuser_malloc(g_msp, count * typsize);
    #else
        p = (void*)calloc(count, typsize);
    #endif
    return p;
}

void nn_memory_free(void* ptr)
{
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        nn_allocuser_free(g_msp, ptr);
    #else
        free(ptr);
    #endif
}
