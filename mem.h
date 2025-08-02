
#if !defined(__NNMEMHEADERFILE_H__)
#define __NNMEMHEADERFILE_H__

#include <stdbool.h>
#include <stdint.h>
#include "allocator.h"

/*
* set to 1 to use allocator (default).
* stable, performs well, etc.
* might not be portable beyond linux/windows, and a couple unix derivatives.
* strives to use the least amount of memory (and does so very successfully).
*/
#define NEON_CONF_MEMUSEALLOCATOR 1

/*
* MUST be called before nn_memory_(malloc|calloc|realloc|free)
*/
void nn_memory_init();

/*
* SHOULD be called upon program exit.
*/
void nn_memory_finish();

/* see malloc(3) */
void* nn_memory_malloc(size_t sz);

/*
* if $p is NULL, calls nn_memory_malloc(nsz),
* otherwise see realloc(3)
*/
void* nn_memory_realloc(void* p, size_t nsz);

/*
* when the allocator is used, it calls nn_memory_malloc(count * typsize),
* otherwise see calloc(3)
*/
void* nn_memory_calloc(size_t count, size_t typsize);

/* see free(3) */
void nn_memory_free(void* ptr);

#endif
