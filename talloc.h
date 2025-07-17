
#ifndef TALLOC_H_QYTR1XNS
#define TALLOC_H_QYTR1XNS

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef _MSC_VER
    #define TALLOC_CONFIG_ATOMICSWINDOWS
#else
    #define TALLOC_CONFIG_ATOMICSTDATOMICS
#endif


/**
 * @def Default preallocated block size. When allocator is out of space, new
 * memory block of this size will be allocated. This value is also smallest
 * memory space allocated on first call of tal_state_malloc(). Try to choose best
 * value for your use.
 */
// 4 MB
//#define TALLOC_CONFIG_BLOCKSIZE ((1024 * 1024) * 4)
//#define TALLOC_CONFIG_BLOCKSIZE ((1024 * 1024) * 1)
//#define TALLOC_CONFIG_BLOCKSIZE ((1024 * 1024) / 2)
//#define TALLOC_CONFIG_BLOCKSIZE ((1024 * 64))
/*
* *** IMPORTANT ***
* as far as i can tell, this is the MINIMUM size to prevent memory errors like invalid read/writes, and such.
* feel free to experiment with this value, but going below seems to only cause trouble.
*/
//#define TALLOC_CONFIG_BLOCKSIZE ((1024 * 256))
#define TALLOC_CONFIG_BLOCKSIZE ((1024 * 64))

/**
 * @def Every allocation with pool allocator is rounded up to next multiply of
 * this value. Objects of same size are in same pool.
 */
//#define TALLOC_CONFIG_POOLGROUPMULT (32)
//#define TALLOC_CONFIG_POOLGROUPMULT (TALLOC_CONFIG_BLOCKSIZE / 128)
#define TALLOC_CONFIG_POOLGROUPMULT (32)

/**
 * @def Count of cells allocated on heap in every pool. When count of
 * per-pool allocations reach this value, new pool block of this size will be
 * allocated on heap.
 */
//#define TALLOC_CONFIG_INITPOOLSIZE (128 * 2)
//#define TALLOC_CONFIG_INITPOOLSIZE (128 / 16)
#define TALLOC_CONFIG_INITPOOLSIZE (8)

/**
 * @def Objects with size under this value will be allocated in pools, larger
 * objects will be allocated directly on heap.
 */
// 2KB
//#define TALLOC_CONFIG_SMALLALLOC (2048
//#define TALLOC_CONFIG_SMALLALLOC (TALLOC_CONFIG_BLOCKSIZE / 64)
#define TALLOC_CONFIG_SMALLALLOC (2048)


/**
 * @def Every allocation of talloc is aligned using this alignment
 */
#ifdef _MSC_VER
    #define TALLOC_CONFIG_ALIGNMENT 16
#else
    #define TALLOC_CONFIG_ALIGNMENT alignof(max_align_t)
#endif


/**
 * @def Enable or disable force freeing of system memory and reset of allocator.
 */
#define TALLOC_FORCE_RESET 1

/**
 * @brief Enable error notification system.
 *
 * Mathod tal_exception(const char *) must be implemented on client side to handle
 * allocator errors. Abort is called after tal_exception call.
 */
#define TALLOC_EXCEPTION_HANDLING 1

#define TALLOC_VERSION_MAJOR 1
#define TALLOC_VERSION_MINOR 4
#define TALLOC_VERSION_PATCH 0


typedef struct talcategory_t talcategory_t;
typedef struct talmetamemory_t talmetamemory_t;
typedef struct talvector_t talvector_t;
typedef struct talstate_t talstate_t;
typedef void (*talonerrorfunc_t)(const char*);


talstate_t* tal_state_init();
void tal_state_destroy(talstate_t* state);


/**
 * Check if pointer is properly aligned for specified alignment.
 * @param p Pointer to check.
 * @param alignment Alignment.
 * @return True if pointer is aligned.
 */
bool tal_util_isaligned(const void* p, size_t alignment);

/**
 * Will align given address up to next aligned address based on alignment.
 * Memory allocated for aligned data must be sizeof(useful data) + alignment
 * due to memory leak prevent.
 * Alignment must be power of two.
 *
 * @param unaligned Pointer to be aligned. Also output value.
 * @param alignment Alignment must be power of two.
 * @param adjustment [out] The amount of bytes used for alignment.
 */
void tal_util_alignptrup(void** p, size_t alignment, ptrdiff_t* adjustment);

/**
 * Alignment of address with extra space for allocation header.
 *
 * @param unaligned Pointer to be aligned. Also output value.
 * @param alignment Alignment must be power of two.
 * @param header_size Size of header structure.
 * @param adjustment [out] The amount of bytes used for alignment.
 */
void tal_util_alignptrwithheader(void** p, size_t alignment, int64_t header_size, ptrdiff_t* adjustment);

/**
 * @brief Memory allocation.
 * Allocates memory of requested size. Automatic pooling is allowed for objects
 * with size up to TALLOC_CONFIG_SMALLALLOC (check the config.h).
 * Large objects are allocated directly on heap.
 *
 * @param count Byte count.
 * @return Pointer to allocated memory block.
 */
void* tal_state_malloc(talstate_t* state, int64_t count);

/**
 * @brief Reuse or reallocate memory with different size
 * Reuse memory block when requested size fits already allocated block or
 * allocates new block of size. Null pointer as input is valid and function
 * will act like tal_state_malloc.
 *
 * @param ptr Pointer to block to reuse.
 * @param size New requested size of block.
 * @return Pointer to allocated memory block.
 */
void* tal_state_reallocwithold(talstate_t* state, void* from, int64_t size, int64_t oldsize);
void* tal_state_realloc(talstate_t* state, void* ptr, int64_t size);

/**
 * @brief Allocate memory and set it value to 0.
 * @param nelem Number of elements.
 * @param elsize Element size.
 * @return Pointer to allocated memory block.
 */
void* tal_state_calloc(talstate_t* state, int64_t nelem, int64_t elsize);

/**
 * @brief Free allocated memory.
 * Use this function only with tal_state_malloc(). Freeing of null address is valid.
 * Double freeing will cause call to tal_exception (when enabled) and immediate
 * crash the application.
 *
 * @param ptr Memory to be freed.
 */
void tal_state_free(talstate_t* state, void* ptr);

/**
 * @brief Preallocate memory block.
 * Allocates new block of system memory using default malloc. Use this method
 * in cases when allocated space is known to be not enough. Minimum reserve
 * size is TALLOC_CONFIG_BLOCKSIZE.
 *
 * @param count Bytes to be preallocated.
 */
void tal_state_expand(talstate_t* state, int64_t count);

/**
 * @brief Print free block table into file stream.
 * @param file Appended file stream.
 */
void tal_state_printblocks(talstate_t* state, FILE* file);

/**
 * @brief Removes unused pools of memory.
 */
void tal_state_optimize(talstate_t* state);

/**
 * @brief Returns allocated system memory in bytes.
 */
int64_t tal_state_getallocatedsystemmemory(talstate_t* state);

/**
 * @brief Returns used memory.
 */
int64_t tal_state_getusedmemory(talstate_t* state);

/**
 * Set custom callback called instead of direct abort.
 * @param func Callback function.
 */
void tal_state_seterrfunc(talstate_t* state, talonerrorfunc_t func);

/**
 * @brief Force free allocated system memory and reset allocator.
 * Call this method only in special cases when memory allocated by the talloc
 * will never be used anymore.
 */

int64_t tal_pool_cellsize(talstate_t *state, int64_t size);
void tal_vector_double_if_needed(talvector_t *vec);
void tal_vector_init(talvector_t *vec);
void tal_vector_free(talvector_t *vec);
void tal_vector_push_back(talvector_t *vec, void *data);
void *tal_vector_at(talvector_t *vec, int64_t index);


#endif /* end of include guard: TALLOC_H_QYTR1XNS */

