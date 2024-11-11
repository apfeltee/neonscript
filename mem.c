
#include <stdlib.h>
#include "mem.h"

void* nn_memory_malloc(size_t sz)
{
    void* p;
    p = (void*)malloc(sz);
    return p;
}

void* nn_memory_realloc(void* p, size_t nsz)
{
    void* retp;
    retp = (void*)realloc(p, nsz);
    return retp;
}

void* nn_memory_calloc(size_t count, size_t typsize)
{
    void* p;
    p = (void*)calloc(count, typsize);
    return p;
}

void nn_memory_free(void* ptr)
{
    free(ptr);
}
