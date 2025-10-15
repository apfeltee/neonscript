/*
** Bundled memory allocator.
** Donated to the public domain.
* -------------------------------------
* this is the allocator used by LuaJIT.
* most modifications pertain to portability (added generic ffs/fls support),
* de-macro-ification of some very large macros, renaming types and typedefs, and code cleanliness.
* 
*/

#ifndef _LJ_ALLOC_H
#define _LJ_ALLOC_H

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Bin types, widths and sizes */
#define NNALLOC_CONST_MAXSIZET (~0)
#define NNALLOC_CONST_MALLOCALIGNMENT (8)

//#define NNALLOC_CONST_DEFAULTGRANULARITY (128 * 1024)
#define NNALLOC_CONST_DEFAULTGRANULARITY (32 * 1024)


//#define NNALLOC_CONST_DEFAULTTRIMTHRESHOLD (2 * (1024 * 1024))
#define NNALLOC_CONST_DEFAULTTRIMTHRESHOLD (1 * (1024 * 1024))

//#define NNALLOC_CONST_DEFAULTMMAPTHRESHOLD (128 * 1024)
#define NNALLOC_CONST_DEFAULTMMAPTHRESHOLD (32 * 1024)

#define NNALLOC_CONST_MAXRELEASECHECKRATE (255)

/* ------------------- size_t and alignment properties -------------------- */

/* The byte and bit size of a size_t */
#define NNALLOC_CONST_SIZETSIZE (sizeof(size_t))
#define NNALLOC_CONST_SIZETBITSIZE (sizeof(size_t) << 3)

/* Some constants coerced to size_t */
/* Annoying but necessary to avoid errors on some platforms */
#define NNALLOC_CONST_SIZETONE (1)
#define NNALLOC_CONST_SIZETTWO (2)
#define NNALLOC_CONST_TWOSIZETSIZES (NNALLOC_CONST_SIZETSIZE << 1)
#define NNALLOC_CONST_FOURSIZETSIZES (NNALLOC_CONST_SIZETSIZE << 2)
#define NNALLOC_CONST_SIXSIZETSIZES (NNALLOC_CONST_FOURSIZETSIZES + NNALLOC_CONST_TWOSIZETSIZES)


#define NNALLOC_CONST_NSMALLBINS (32)
#define NNALLOC_CONST_NTREEBINS (32)
#define NNALLOC_CONST_SMALLBINSHIFT (3)
#define NNALLOC_CONST_TREEBINSHIFT (8)
#define NNALLOC_CONST_MINLARGESIZE (NNALLOC_CONST_SIZETONE << NNALLOC_CONST_TREEBINSHIFT)
#define NNALLOC_CONST_MAXSMALLSIZE (NNALLOC_CONST_MINLARGESIZE - NNALLOC_CONST_SIZETONE)
#define NNALLOC_CONST_MAXSMALLREQUEST (NNALLOC_CONST_MAXSMALLSIZE - CHUNK_ALIGN_MASK - CHUNK_OVERHEAD)

typedef struct nnallocatorplainchunk_t nnallocatorplainchunk_t;
typedef struct nnallocatorsegment_t nnallocatorsegment_t;
typedef struct nnallocatortreechunk_t nnallocatortreechunk_t;
typedef struct nnallocatorstate_t nnallocatorstate_t;

struct nnallocatorplainchunk_t {
  size_t prev_foot;  /* Size of previous chunk (if free).  */
  size_t head;       /* Size and inuse bits. */
  nnallocatorplainchunk_t *fd;         /* double links -- used only if free. */
  nnallocatorplainchunk_t *bk;
};

typedef size_t bindex_t;               /* Described below */
typedef unsigned int binmap_t;         /* Described below */
typedef unsigned int flag_t;           /* The type of various bit flag sets */

struct nnallocatortreechunk_t {
  /* The first four fields must be compatible with nnallocatorplainchunk_t */
  size_t prev_foot;
  size_t head;
  nnallocatortreechunk_t *fd;
  nnallocatortreechunk_t *bk;

  nnallocatortreechunk_t *child[2];
  nnallocatortreechunk_t *parent;
  bindex_t index;
};

struct nnallocatorsegment_t {
  char *base;             /* base address */
  size_t size;             /* allocated size */
  nnallocatorsegment_t *next;   /* ptr to next segment */
};

struct nnallocatorstate_t {
  binmap_t smallmap;
  binmap_t treemap;
  size_t dvsize;
  size_t topsize;
  nnallocatorplainchunk_t* dv;
  nnallocatorplainchunk_t* top;
  size_t trim_check;
  size_t release_checks;
  nnallocatorplainchunk_t* smallbins[(NNALLOC_CONST_NSMALLBINS+1)*2];
  nnallocatortreechunk_t* treebins[NNALLOC_CONST_NTREEBINS];
  nnallocatorsegment_t seg;
};

void *nn_allocator_create(void);
void nn_allocator_destroy(void *msp);

void *nn_allocuser_malloc(void *msp, size_t nsize);
void *nn_allocuser_free(void *msp, void *ptr);
void *nn_allocuser_realloc(void *msp, void *ptr, size_t nsize);
void *nn_allocator_f(void *msp, void *ptr, size_t osize, size_t nsize);


#endif
