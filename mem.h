
#include "allocator.h"

/*
* set to 1 to use allocator (default).
* stable, performs well, etc.
* might not be portable beyond linux/windows, and a couple unix derivatives.
* strives to use the least amount of memory (and does so very successfully).
*/
#define NEON_CONF_MEMUSEALLOCATOR 0

#define NEON_MEMORY_GROWCAPACITY(capacity) \
    ((capacity) < 4 ? 4 : (capacity)*2)


#if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
    /* if any global variables need to be declared, declare them here. */
    static void* g_mspcontext;
#endif


static inline void nn_memory_init()
{
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        g_mspcontext = nn_allocator_create();
    #endif
}

static inline void nn_memory_finish()
{
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        nn_allocator_destroy(g_mspcontext);
    #endif
}

static inline void* nn_memory_setsize(void* p, size_t sz)
{
    size_t* sp;
    sp = (size_t*)p;
    *sp = sz;
    ++sp;
    memset(sp,0,sz);
    return sp;
}

static inline size_t nn_memory_getsize(void * p)
{
    size_t* in = (size_t*)p;
    if (in)
    {
        --in;
        return *in;
    }
    return -1;
}

#if defined(NEON_CONFIG_DEBUGMEMORY) && (NEON_CONFIG_DEBUGMEMORY == 1)

static inline void nn_memory_printdebug(const char* file, int line, const char* exprstr, const char* from, size_t totalsz)
{
    fprintf(stderr, "from %s at %s:%d: %s: allocating %ld\n", from, file, line, exprstr, totalsz);
}

static inline void* nn_memory_debugmalloc(const char* file, int line, const char* exprstr, size_t sz)
{
    nn_memory_printdebug(file, line, exprstr, "malloc", sz);
    return nn_memory_mallocactual(sz);
}


static inline void* nn_memory_debugcalloc(const char* file, int line, const char* exprstr, size_t count, size_t typsz)
{
    nn_memory_printdebug(file, line, exprstr, "calloc", (count * typsz));
    return nn_memory_callocactual(count, typsz);
}


static inline void* nn_memory_debugrealloc(const char* file, int line, const char* exprstr, void* p, size_t sz)
{
    nn_memory_printdebug(file, line, exprstr, "realloc", sz);
    return nn_memory_reallocactual(p, sz);
}

#endif

#if defined(NEON_CONFIG_DEBUGMEMORY) && (NEON_CONFIG_DEBUGMEMORY == 1)
static inline void* nn_memory_mallocactual(size_t sz)
#else
static inline void* nn_memory_malloc(size_t sz)
#endif
{
    void* p;
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        p = (void*)nn_allocuser_malloc(g_mspcontext, sz);
    #else
        p = (void*)malloc(sz);
    #endif
    return p;
}

#if defined(NEON_CONFIG_DEBUGMEMORY) && (NEON_CONFIG_DEBUGMEMORY == 1)
static inline void* nn_memory_reallocactual(void* p, size_t nsz)
#else
static inline void* nn_memory_realloc(void* p, size_t nsz)
#endif
{
    void* retp;
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        if(p == NULL)
        {
            return nn_memory_malloc(nsz);
        }
        retp = (void*)nn_allocuser_realloc(g_mspcontext, p, nsz);
    #else
        retp = (void*)realloc(p, nsz);
    #endif
    return retp;
}

#if defined(NEON_CONFIG_DEBUGMEMORY) && (NEON_CONFIG_DEBUGMEMORY == 1)
static inline void* nn_memory_callocactual(size_t count, size_t typsize)
#else    
static inline void* nn_memory_calloc(size_t count, size_t typsize)
#endif
{
    void* p;
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        p = (void*)nn_allocuser_malloc(g_mspcontext, (count * typsize));
        memset(p, 0, (count * typsize));
    #else
        p = (void*)calloc(count, typsize);
    #endif
    return p;
}

static inline void nn_memory_free(void* ptr)
{
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        nn_allocuser_free(g_mspcontext, ptr);
    #else
        free(ptr);
    #endif
}

