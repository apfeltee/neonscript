
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

#endif

void nn_memory_init()
{
    #if defined(NEON_CONF_USEMEMPOOL) && (NEON_CONF_USEMEMPOOL == 1)
        zm_init();
    #endif
}

void nn_memory_finish()
{
    #if defined(NEON_CONF_USEMEMPOOL) && (NEON_CONF_USEMEMPOOL == 1)

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

void* nn_memory_malloc(size_t sz)
{
    void* p;
    #if defined(NEON_CONF_USEMEMPOOL) && (NEON_CONF_USEMEMPOOL == 1)
        p = zm_malloc(sz);
        if(p == NULL)
        {
            return NULL;
        }
        p = nn_memory_setsize(p, sz);
    #else
        p = (void*)malloc(sz);
    #endif
    return p;
}

size_t getsize(void * p)
{
    size_t* in = p;
    if (in)
    {
        --in;
        return *in;
    }
    return -1;
}
void* nn_memory_realloc(void* p, size_t nsz)
{
    void* retp;
    #if defined(NEON_CONF_USEMEMPOOL) && (NEON_CONF_USEMEMPOOL == 1)
        //retp = REALLOC(p, 0, nsz);
        int msize;
        msize = getsize(p);
        printf("msize=%d\n", msize);
        if ((int)nsz <= msize)
        {
            return p;
        }
        retp = nn_memory_malloc(nsz);
        memcpy(retp, p, msize);
        nn_memory_free(p);
        return retp;
    #else
        retp = (void*)realloc(p, nsz);
    #endif
    return retp;
}

void* nn_memory_calloc(size_t count, size_t typsize)
{
    void* p;
    #if defined(NEON_CONF_USEMEMPOOL) && (NEON_CONF_USEMEMPOOL == 1)
        p = nn_memory_malloc(count * typsize);
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
        zm_free(ptr);
    #else
        free(ptr);
    #endif
}
