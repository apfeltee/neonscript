
#include <assert.h>
/* for printing in malloc_stats */
#include <stdio.h>
#include "allocator.h"

#if defined(DEBUG)
    #define NNALLOC_CONF_DEBUG 1
#else
    #define NNALLOC_CONF_DEBUG 0
#endif

#if NNALLOC_CONF_USELOCKS
    #if !defined(NNALLOC_CONF_ISWIN32)
        /* By default use posix locks */
        #include <pthread.h>
    #endif
#endif

#if !defined(NEON_FORCEINLINE)
    #if defined(__STRICT_ANSI__)
        #define NEON_INLINE static
        #define NEON_FORCEINLINE static
        #define inline
    #else
        #define NEON_INLINE static inline
        #if defined(__GNUC__)
            #define NEON_FORCEINLINE static __attribute__((always_inline)) inline
        #else
            #define NEON_FORCEINLINE static inline
        #endif
    #endif
#endif
/*
  ========================================================================
  To make a fully customizable malloc.h header file, cut everything
  above this line, put into file malloc.h, edit to suit, and #include it
  on the next line, as well as in programs that use this malloc.
  ========================================================================
*/

/*------------------------------ internal #includes ---------------------- */

#ifdef _MSC_VER
    /* no "unsigned" warnings */
    #pragma warning(disable : 4146
#endif


#ifndef NNALLOC_CONF_LACKSERRNOH
    /* for NNALLOC_CONF_MALLOCFAILUREACTION */
    #include <errno.h>
#endif

#ifndef NNALLOC_CONF_LACKSSTDLIBH
    /* for abort() */
    #include <stdlib.h>
#endif

#if defined(NNALLOC_CONF_DEBUG) && (NNALLOC_CONF_DEBUG == 1)
    #if NNALLOC_CONF_ABORTONASSERTFAILURE
        #define NNALLOCASSERT(x)                   \
            if(!(x))                        \
            {                               \
                NNALLOC_CONF_ABORTCALLBACK; \
            }
    #else
        #define NNALLOCASSERT assert
    #endif
#else
    #define NNALLOCASSERT(x)
#endif

#ifndef NNALLOC_CONF_LACKSSTRINGH
    /* for memset etc */
    #include <string.h>
#endif

#if NNALLOC_CONF_USEBUILTINFFSFUNC
    #ifndef NNALLOC_CONF_LACKSSTRINGSH
        /* for ffs */
        #include <strings.h>
    #endif
#endif

#if NNALLOC_CONF_HAVEMMAP
    #ifndef NNALLOC_CONF_LACKSSYSMMANH
        /* for mmap */
        #include <sys/mman.h>
    #endif
    #ifndef NNALLOC_CONF_LACKSFCNTLH
        #include <fcntl.h>
    #endif
#endif

#if NNALLOC_CONF_HAVEMORECORE
    #ifndef NNALLOC_CONF_LACKSUNISTDH
        /* for sbrk */
        #include <unistd.h>
    #else 
        #if !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__)
            extern void* sbrk(ptrdiff_t);
        #endif
    #endif
#endif

#if !defined(NNALLOC_CONF_ISWIN32)
    #ifdef nn_allocator_malloc_getpagesize
        #define nn_allocator_malloc_getpagesize malloc_getpagesize
    #else
        /* some SVR4 systems omit an underscore */
        #ifdef _SC_PAGESIZE
            #ifndef _SC_PAGE_SIZE
                #define _SC_PAGE_SIZE _SC_PAGESIZE
            #endif
        #endif
        #ifdef _SC_PAGE_SIZE
            #define nn_allocator_malloc_getpagesize sysconf(_SC_PAGE_SIZE)
        #else
            #if defined(BSD) || defined(DGUX) || defined(HAVE_GETPAGESIZE)
                extern size_t getpagesize();
                #define nn_allocator_malloc_getpagesize getpagesize()
            #else
                /* use supplied emulation of getpagesize */
                #if defined(NNALLOC_CONF_ISWIN32)
                    #define nn_allocator_malloc_getpagesize getpagesize()
                #else
                    #ifndef NNALLOC_CONF_LACKSSYSPARAMH
                        #include <sys/param.h>
                    #endif
                    #ifdef EXEC_PAGESIZE
                        #define nn_allocator_malloc_getpagesize EXEC_PAGESIZE
                    #else
                        #ifdef NBPG
                            #ifndef CLSIZE
                                #define nn_allocator_malloc_getpagesize NBPG
                            #else
                                #define nn_allocator_malloc_getpagesize (NBPG * CLSIZE)
                            #endif
                        #else
                            #ifdef NBPC
                                #define nn_allocator_malloc_getpagesize NBPC
                            #else
                                #ifdef PAGESIZE
                                    #define nn_allocator_malloc_getpagesize PAGESIZE
                                #else
                                    /* just guess */
                                    #define nn_allocator_malloc_getpagesize ((size_t)4096U)
                                #endif
                            #endif
                        #endif
                    #endif
                #endif
            #endif
        #endif
    #endif
#endif

/* ------------------- size_t and alignment properties -------------------- */

/* The byte and bit size of a size_t */
#define NNALLOC_CONST_SIZETSIZE (sizeof(size_t))
#define NNALLOC_CONST_SIZETBITSIZE (sizeof(size_t) << 3)

/* Some constants coerced to size_t */
/* Annoying but necessary to avoid errors on some platforms */
#define NNALLOC_CONST_SIZETONE ((size_t)1)
#define NNALLOC_CONST_SIZETTWO ((size_t)2)
#define NNALLOC_CONST_TWOSIZETSIZES (NNALLOC_CONST_SIZETSIZE << 1)
#define NNALLOC_CONST_FOURSIZETSIZES (NNALLOC_CONST_SIZETSIZE << 2)
#define NNALLOC_CONST_SIXSIZETSIZES (NNALLOC_CONST_FOURSIZETSIZES + NNALLOC_CONST_TWOSIZETSIZES)
#define NNALLOC_CONST_HALFMAXSIZET (NNALLOC_CONF_MAXSIZET / 2U)

/* The bit mask value corresponding to NNALLOC_CONF_MALLOCALIGNMENT */
#define NNALLOC_CONST_CHUNKALIGNMASK (NNALLOC_CONF_MALLOCALIGNMENT - NNALLOC_CONST_SIZETONE)

#if defined(NNALLOC_CONF_DEBUG) && (NNALLOC_CONF_DEBUG == 1)
    /* True if address a has acceptable alignment */
    #define nn_allocator_isaligned(A) (((size_t)((A)) & (NNALLOC_CONST_CHUNKALIGNMASK)) == 0)
#endif


/* -------------------------- MMAP preliminaries ------------------------- */

/*
   If NNALLOC_CONF_HAVEMORECORE or NNALLOC_CONF_HAVEMMAP are false, we just define calls and
   checks to fail so compiler optimizer can delete code rather than
   using so many "#if"s.
*/

/* NNALLOC_CONF_MORECOREFUNCNAME and MMAP must return NNALLOC_CONST_MFAIL on failure */
#define NNALLOC_CONST_MFAIL ((void*)(NNALLOC_CONF_MAXSIZET))
/* defined for convenience */
#define NNALLOC_CONST_CMFAIL ((char*)(NNALLOC_CONST_MFAIL))

#if !NNALLOC_CONF_HAVEMMAP
    #define NNALLOC_CONST_SIZETZERO ((size_t)0)
    #define NNALLOC_CONST_ISMMAPPEDBIT (NNALLOC_CONST_SIZETZERO)
    #define NNALLOC_CONST_USEMMAPBIT (NNALLOC_CONST_SIZETZERO)
    #define nn_allocator_callmmap(s) NNALLOC_CONST_MFAIL
    #define NNALLOC_CMAC_CALLMUNMAP(a, s) (-1)
    #define NNALLOC_CMAC_DIRECTMMAP(s) NNALLOC_CONST_MFAIL
#else
    #define NNALLOC_CONST_ISMMAPPEDBIT (NNALLOC_CONST_SIZETONE)
    #define NNALLOC_CONST_USEMMAPBIT (NNALLOC_CONST_SIZETONE)

    #if !defined(NNALLOC_CONF_ISWIN32)
        #define NNALLOC_CMAC_CALLMUNMAP(a, s) munmap((a), (s))
        #define NNALLOC_CONST_MMAPPROT (PROT_READ | PROT_WRITE)
        #if !defined(NNALLOC_CONF_MAPANONYMOUS) && defined(NNALLOC_CONST_MAPANON)
            #define NNALLOC_CONF_MAPANONYMOUS NNALLOC_CONST_MAPANON
        #endif
        #ifdef NNALLOC_CONF_MAPANONYMOUS
            #define NNALLOC_CONST_MMAPFLAGS (MAP_PRIVATE | NNALLOC_CONF_MAPANONYMOUS)
            #define nn_allocator_callmmap(s) mmap(0, (s), NNALLOC_CONST_MMAPPROT, NNALLOC_CONST_MMAPFLAGS, -1, 0)
        #else
            /*
               Nearly all versions of mmap support NNALLOC_CONF_MAPANONYMOUS, so the following
               is unlikely to be needed, but is supplied just in case.
            */
            #define NNALLOC_CONST_MMAPFLAGS (MAP_PRIVATE)
        #endif
        #define NNALLOC_CMAC_DIRECTMMAP(s) nn_allocator_callmmap(s)
    #else
        #define nn_allocator_callmmap(s) nn_allocator_util_win32mmap(s)
        #define NNALLOC_CMAC_CALLMUNMAP(a, s) nn_allocator_util_win32munmap((a), (s))
        #define NNALLOC_CMAC_DIRECTMMAP(s) nn_allocator_util_win32directmmap(s)
    #endif
#endif

#if NNALLOC_CONF_HAVEMMAP && NNALLOC_CONF_HAVEMREMAP
    #define NNALLOC_CMAC_CALLMREMAP(addr, osz, nsz, mv) mremap((addr), (osz), (nsz), (mv))
#else
    #define NNALLOC_CMAC_CALLMREMAP(addr, osz, nsz, mv) NNALLOC_CONST_MFAIL
#endif

#if NNALLOC_CONF_HAVEMORECORE
    #define NNALLOC_CMAC_CALLMORECORE(S) NNALLOC_CONF_MORECOREFUNCNAME(S)
#else
    #define NNALLOC_CMAC_CALLMORECORE(S) NNALLOC_CONST_MFAIL
#endif

/* nnallocstate_t bit set if contiguous morecore disabled or failed */
#define NNALLOC_CONST_USENONCONTIGUOUSBIT (4U)

/* segment bit set in nn_allocator_mspacecreatewithbase */
#define NNALLOC_CONST_EXTERNBIT (8U)

/* --------------------------- Lock preliminaries ------------------------ */

#if NNALLOC_CONF_USELOCKS
    /*
      When locks are defined, there are up to two global locks:

      * If NNALLOC_CONF_HAVEMORECORE, g_allocvar_morecoremutex protects sequences of calls to
        NNALLOC_CONF_MORECOREFUNCNAME.  In many cases nn_allocator_mstate_sysalloc requires two calls, that should
        not be interleaved with calls by other threads.  This does not
        protect against direct calls to NNALLOC_CONF_MORECOREFUNCNAME by other threads not
        using this lock, so there is still code to cope the best we can on
        interference.

      * g_allocvar_magicinitmutex ensures that g_allocvar_mallocparams.magic and other
        unique g_allocvar_mallocparams values are initialized only once.
    */

    #if !defined(NNALLOC_CONF_ISWIN32)
        /* By default use posix locks */
        typedef pthread_mutex_t nnallocmutlock_t;
        #define NNALLOC_CMAC_INITIALLOCK(l) pthread_mutex_init(l, NULL)
        #define NNALLOC_CMAC_ACQUIRELOCK(l) pthread_mutex_lock(l)
        #define NNALLOC_CMAC_RELEASELOCK(l) pthread_mutex_unlock(l)
    #else
        /*
        * Because lock-protected regions have bounded times, and there
        * are no recursive lock calls, we can use simple spinlocks.
        */
        typedef long nnallocmutlock_t;
        #define NNALLOC_CMAC_INITIALLOCK(l) *(l) = 0
        #define NNALLOC_CMAC_ACQUIRELOCK(l) nn_allocator_util_win32acquirelock(l)
        #define NNALLOC_CMAC_RELEASELOCK(l) nn_allocator_util_win32releaselock(l)
    #endif
    #define NNALLOC_CONST_USELOCKBIT (2U)
#else
    #define NNALLOC_CONST_USELOCKBIT (0U)
    #define NNALLOC_CMAC_INITIALLOCK(l)
#endif

#if NNALLOC_CONF_USELOCKS && NNALLOC_CONF_HAVEMORECORE
    #define NNALLOC_CMAC_ACQUIREMORECORELOCK() NNALLOC_CMAC_ACQUIRELOCK(&g_allocvar_morecoremutex);
    #define NNALLOC_CMAC_RELEASEMORECORELOCK() NNALLOC_CMAC_RELEASELOCK(&g_allocvar_morecoremutex);
#else
    #define NNALLOC_CMAC_ACQUIREMORECORELOCK()
    #define NNALLOC_CMAC_RELEASEMORECORELOCK()
#endif

#if NNALLOC_CONF_USELOCKS
    #define NNALLOC_CMAC_ACQUIREMAGICINITLOCK() NNALLOC_CMAC_ACQUIRELOCK(&g_allocvar_magicinitmutex);
    #define NNALLOC_CMAC_RELEASEMAGICINITLOCK() NNALLOC_CMAC_RELEASELOCK(&g_allocvar_magicinitmutex);
#else
    #define NNALLOC_CMAC_ACQUIREMAGICINITLOCK()
    #define NNALLOC_CMAC_RELEASEMAGICINITLOCK()
#endif


/* -----------------------  Chunk representations ------------------------ */

/*
  (The following includes lightly edited explanations by Colin Plumb.)

  The nnallocchunkitem_t declaration below is misleading (but accurate and
  necessary).  It declares a "view" into memory allowing access to
  necessary fields at known offsets from a given base.

  Chunks of memory are maintained using a `boundary tag' method as
  originally described by Knuth.  (See the paper by Paul Wilson
  ftp://ftp.cs.utexas.edu/pub/garbage/allocsrv.ps for a survey of such
  techniques.)  Sizes of free chunks are stored both in the front of
  each chunk and at the end.  This makes consolidating fragmented
  chunks into bigger chunks fast.  The head fields also hold bits
  representing whether chunks are free or in use.

  Here are some pictures to make it clearer.  They are "exploded" to
  show that the state of a chunk can be thought of as extending from
  the high 31 bits of the head field of its header through the
  prevfoot and NNALLOC_CONST_PINUSEBIT bit of the following chunk header.

  A chunk that's in use looks like:

   chunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
           | Size of previous chunk (if P = 1)                             |
           +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ |P|
         | Size of this chunk                                         1| +-+
   mem-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         |                                                               |
         +-                                                             -+
         |                                                               |
         +-                                                             -+
         |                                                               :
         +-      size - sizeof(size_t) available payload bytes          -+
         :                                                               |
 chunk-> +-                                                             -+
         |                                                               |
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ |1|
       | Size of next chunk (may or may not be in use)               | +-+
 mem-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    And if it's free, it looks like this:

   chunk-> +-                                                             -+
           | User payload (must be in use, or we would have merged!)       |
           +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ |P|
         | Size of this chunk                                         0| +-+
   mem-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         | Next pointer                                                  |
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         | Prev pointer                                                  |
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         |                                                               :
         +-      size - sizeof(struct chunk) unused bytes               -+
         :                                                               |
 chunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         | Size of this chunk                                            |
         +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ |0|
       | Size of next chunk (must be in use, or we would have merged)| +-+
 mem-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |                                                               :
       +- User payload                                                -+
       :                                                               |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                                                                     |0|
                                                                     +-+
  Note that since we always merge adjacent free chunks, the chunks
  adjacent to a free chunk must be in use.

  Given a pointer to a chunk (which can be derived trivially from the
  payload pointer) we can, in O(1) time, find out whether the adjacent
  chunks are free, and if so, unlink them from the lists that they
  are on and merge them with the current chunk.

  Chunks always begin on even word boundaries, so the mem portion
  (which is returned to the user) is also on an even word boundary, and
  thus at least double-word aligned.

  The P (NNALLOC_CONST_PINUSEBIT) bit, stored in the unused low-order bit of the
  chunk size (which is always a multiple of two words), is an in-use
  bit for the *previous* chunk.  If that bit is *clear*, then the
  word before the current chunk size contains the previous chunk
  size, and can be used to find the front of the previous chunk.
  The very first chunk allocated always has this bit set, preventing
  access to non-existent (or non-owned) memory. If pinuse is set for
  any given chunk, then you CANNOT determine the size of the
  previous chunk, and might even get a memory addressing fault when
  trying to do so.

  The C (NNALLOC_CONST_CINUSEBIT) bit, stored in the unused second-lowest bit of
  the chunk size redundantly records whether the current chunk is
  inuse. This redundancy enables usage checks within free and realloc,
  and reduces indirection when freeing and consolidating chunks.

  Each freshly allocated chunk must have both cinuse and pinuse set.
  That is, each allocated chunk borders either a previously allocated
  and still in-use chunk, or the base of its memory arena. This is
  ensured by making all allocations from the the `lowest' part of any
  found chunk.  Further, no free chunk physically borders another one,
  so each free chunk is known to be preceded and followed by either
  inuse chunks or the ends of memory.

  Note that the `foot' of the current chunk is actually represented
  as the prevfoot of the NEXT chunk. This makes it easier to
  deal with alignments etc but can be very confusing when trying
  to extend or adapt this code.

  The exceptions to all this are

     1. The special chunk `top' is the top-most available chunk (i.e.,
        the one bordering the end of available memory). It is treated
        specially.  Top is never included in any bin, is used only if
        no other chunk is available, and is released back to the
        system if it is very large (see DLMALLOC_OPTMALLOPT_TRIMTHRESHOLD).  In effect,
        the top chunk is treated as larger (and thus less well
        fitting) than any other available chunk.  The top chunk
        doesn't update its trailing size field since there is no next
        contiguous chunk that would have to index off it. However,
        space is still allocated for it (nn_allocator_gettopfootsize()) to enable
        separation or merging when space is extended.

     3. Chunks allocated via mmap, which have the lowest-order bit
        (NNALLOC_CONST_ISMMAPPEDBIT) set in their prevfoot fields, and do not set
        NNALLOC_CONST_PINUSEBIT in their head fields.  Because they are allocated
        one-by-one, each must carry its own prevfoot field, which is
        also used to hold the offset this chunk has within its mmapped
        region, which is needed to preserve alignment. Each mmapped
        chunk is trailed by the first two fields of a fake next-chunk
        for sake of usage checks.

*/

/* ------------------- Chunks sizes and alignments ----------------------- */

#define NNALLOC_CONST_MCHUNKSIZE (sizeof(nnallocchunkitem_t))

#define NNALLOC_CONST_CHUNKOVERHEAD (NNALLOC_CONST_SIZETSIZE)

/* MMapped chunks need a second word of overhead ... */
#define NNALLOC_CONST_MMAPCHUNKOVERHEAD (NNALLOC_CONST_TWOSIZETSIZES)
/* ... and additional padding for fake next-chunk at foot */
#define NNALLOC_CONST_MMAPFOOTPAD (NNALLOC_CONST_FOURSIZETSIZES)

/* The smallest size we can malloc is an aligned minimal chunk */
#define NNALLOC_CONST_MINCHUNKSIZE ((NNALLOC_CONST_MCHUNKSIZE + NNALLOC_CONST_CHUNKALIGNMASK) & ~NNALLOC_CONST_CHUNKALIGNMASK)

/* conversion from malloc headers to user pointers, and back */
#define nn_allocator_chunk2mem(p) ((void*)((char*)(p) + NNALLOC_CONST_TWOSIZETSIZES))
#define nn_allocator_mem2chunk(mem) ((nnallocchunkitem_t*)((char*)(mem) - NNALLOC_CONST_TWOSIZETSIZES))
/* chunk associated with aligned address A */
#define nn_allocator_alignaschunk(A) (nnallocchunkitem_t*)((A) + nn_allocator_alignoffset(nn_allocator_chunk2mem(A)))

/* Bounds on request (not chunk) sizes. */
#define NNALLOC_CONST_MAXREQUEST ((-NNALLOC_CONST_MINCHUNKSIZE) << 2)
#define NNALLOC_CONST_MINREQUEST (NNALLOC_CONST_MINCHUNKSIZE - NNALLOC_CONST_CHUNKOVERHEAD - NNALLOC_CONST_SIZETONE)

/* pad request bytes into a usable size */
#define nn_allocator_padrequest(req) \
    (((req) + NNALLOC_CONST_CHUNKOVERHEAD + NNALLOC_CONST_CHUNKALIGNMASK) & ~NNALLOC_CONST_CHUNKALIGNMASK)

/* pad request, checking for minimum (but not maximum) */
#define nn_allocator_request2size(req) \
    (((req) < NNALLOC_CONST_MINREQUEST) ? NNALLOC_CONST_MINCHUNKSIZE : nn_allocator_padrequest(req))

/* ------------------ Operations on head and foot fields ----------------- */

/*
  The head field of a chunk is or'ed with NNALLOC_CONST_PINUSEBIT when previous
  adjacent chunk in use, and or'ed with NNALLOC_CONST_CINUSEBIT if this chunk is in
  use. If the chunk was obtained with mmap, the prevfoot field has
  NNALLOC_CONST_ISMMAPPEDBIT set, otherwise holding the offset of the base of the
  mmapped region to the base of the chunk.
*/

#define NNALLOC_CONST_PINUSEBIT (NNALLOC_CONST_SIZETONE)
#define NNALLOC_CONST_CINUSEBIT (NNALLOC_CONST_SIZETTWO)
#define NNALLOC_CONST_INUSEBITS (NNALLOC_CONST_PINUSEBIT | NNALLOC_CONST_CINUSEBIT)

/* Head value for fenceposts */
#define NNALLOC_CONST_FENCEPOSTHEAD (NNALLOC_CONST_INUSEBITS | NNALLOC_CONST_SIZETSIZE)

/* extraction of fields from head words */
#define nn_allocator_cinuse(p) ((p)->head & NNALLOC_CONST_CINUSEBIT)
#define nn_allocator_pinuse(p) ((p)->head & NNALLOC_CONST_PINUSEBIT)
#define nn_allocator_chunksize(p) ((p)->head & ~(NNALLOC_CONST_INUSEBITS))

#define nn_allocator_clearpinuse(p) ((p)->head &= ~NNALLOC_CONST_PINUSEBIT)

/* Treat space at ptr +/- offset as a chunk */
#define nn_allocator_chunkplusoffset(p, s) ((nnallocchunkitem_t*)(((char*)(p)) + (s)))
#define nn_allocator_chunkminusoffset(p, s) ((nnallocchunkitem_t*)(((char*)(p)) - (s)))

/* Ptr to next or previous physical nnallocchunkitem_t. */
#define nn_allocator_nextchunk(p) ((nnallocchunkitem_t*)(((char*)(p)) + ((p)->head & ~NNALLOC_CONST_INUSEBITS)))
#if defined(NNALLOC_CONF_DEBUG) && (NNALLOC_CONF_DEBUG == 1)
    #define nn_allocator_prevchunk(p) ((nnallocchunkitem_t*)(((char*)(p)) - ((p)->prevfoot)))
    /* extract next chunk's pinuse bit */
    #define nn_allocator_nextpinuse(p) ((nn_allocator_nextchunk(p)->head) & NNALLOC_CONST_PINUSEBIT)
#endif

/* Get/set size at footer */
#define nn_allocator_setfoot(p, s) (((nnallocchunkitem_t*)((char*)(p) + (s)))->prevfoot = (s))

/* Set size, pinuse bit, and foot */
#define nn_allocator_setsizeandpinuseoffreechunk(p, s) \
    ((p)->head = (s | NNALLOC_CONST_PINUSEBIT), nn_allocator_setfoot(p, s))

/* Set size, pinuse bit, foot, and clear next pinuse */
#define nn_allocator_setfreewithpinuse(p, s, n) \
    (nn_allocator_clearpinuse(n), nn_allocator_setsizeandpinuseoffreechunk(p, s))

#define nn_allocator_ismmapped(p) \
    (!((p)->head & NNALLOC_CONST_PINUSEBIT) && ((p)->prevfoot & NNALLOC_CONST_ISMMAPPEDBIT))

/* Get the internal overhead associated with chunk p */
#define nn_allocator_overheadfor(p) \
    (nn_allocator_ismmapped(p) ? NNALLOC_CONST_MMAPCHUNKOVERHEAD : NNALLOC_CONST_CHUNKOVERHEAD)

/* Return true if malloced space is not necessarily cleared */
#if NNALLOC_CONF_MMAPCLEARS
    #define nn_allocator_callocmustclear(p) (!nn_allocator_ismmapped(p))
#else
    #define nn_allocator_callocmustclear(p) (1)
#endif

/* ---------------------- Overlaid data structures ----------------------- */

/* A little helper macro for trees */
#define nn_allocator_leftmostchild(t) ((t)->child[0] != 0 ? (t)->child[0] : (t)->child[1])

/* ----------------------------- Segments -------------------------------- */

/*
  Each malloc space may include non-contiguous segments, held in a
  list headed by an embedded nnallocmemsegment_t record representing the
  top-most space. Segments also include flags holding properties of
  the space. Large chunks that are directly allocated by mmap are not
  included in this list. They are instead independently created and
  destroyed without otherwise keeping track of them.

  Segment management mainly comes into play for spaces allocated by
  MMAP.  Any call to MMAP might or might not return memory that is
  adjacent to an existing segment.  NNALLOC_CONF_MORECOREFUNCNAME normally contiguously
  extends the current space, so this space is almost always adjacent,
  which is simpler and faster to deal with. (This is why NNALLOC_CONF_MORECOREFUNCNAME is
  used preferentially to MMAP when both are available -- see
  nn_allocator_mstate_sysalloc.)  When allocating using MMAP, we don't use any of the
  hinting mechanisms (inconsistently) supported in various
  implementations of unix mmap, or distinguish reserving from
  committing memory. Instead, we just ask for space, and exploit
  contiguity when we get it.  It is probably possible to do
  better than this on some systems, but no general scheme seems
  to be significantly better.

  Management entails a simpler variant of the consolidation scheme
  used for chunks to reduce fragmentation -- new adjacent memory is
  normally prepended or appended to an existing segment. However,
  there are limitations compared to chunk consolidation that mostly
  reflect the fact that segment processing is relatively infrequent
  (occurring only when getting memory from system) and that we
  don't expect to have huge numbers of segments:

  * Segments are not indexed, so traversal requires linear scans.  (It
    would be possible to index these, but is not worth the extra
    overhead and complexity for most programs on most platforms.)
  * New segments are only appended to old ones when holding top-most
    memory; if they cannot be prepended to others, they are held in
    different segments.

  Except for the top-most segment of an nnallocstate_t, each segment record
  is kept at the tail of its segment. Segments are added by pushing
  segment records onto the list headed by &nnallocstate_t.seg for the
  containing nnallocstate_t.

  Segment flags control allocation/merge/deallocation policies:
  * If NNALLOC_CONST_EXTERNBIT set, then we did not allocate this segment,
    and so should not try to deallocate or merge with others.
    (This currently holds only for the initial segment passed
    into nn_allocator_mspacecreatewithbase.)
  * If NNALLOC_CONST_ISMMAPPEDBIT set, the segment may be merged with
    other surrounding mmapped segments and trimmed/de-allocated
    using munmap.
  * If neither bit is set, then the segment was obtained using
    NNALLOC_CONF_MORECOREFUNCNAME so can be merged with surrounding NNALLOC_CONF_MORECOREFUNCNAME'd segments
    and deallocated/trimmed using NNALLOC_CONF_MORECOREFUNCNAME with negative arguments.
*/

/* i actually am not sure where this is supposed to come from. not from libffi, probably. */
#if FFI_MMAP_EXEC_WRIT
/* The mmap magic is supposed to store the address of the executable
   segment at the very end of the requested block.  */

    #define nn_allocator_mmapexecoffset(b, s) (*(ptrdiff_t*)((b) + (s) - sizeof(ptrdiff_t)))

    /* We can only merge segments if their corresponding executable
       segments are at identical offsets.  */
    #define nn_allocator_checksegmentmerge(segment, b, s) \
        (nn_allocator_mmapexecoffset((b), (s)) == (segment)->execoffset)

    #define nn_allocator_mstate_addsegment_exec_offset(p, segment) ((char*)(p) + (segment)->execoffset)
    #define nn_allocator_subsegmentexecoffset(p, segment) ((char*)(p) - (segment)->execoffset)

    /* The removal of sflags only works with NNALLOC_CONF_HAVEMORECORE == 0.  */
    #define nn_allocator_getsegmentflags(segment) (NNALLOC_CONST_ISMMAPPEDBIT)
    #define nn_allocator_setsegmentflags(segment, v)                                                                                 \
        {                                                                                                                      \
            if((v) != NNALLOC_CONST_ISMMAPPEDBIT)                                                                             \
            {                                                                                                                  \
                (NNALLOC_CONF_ABORTCALLBACK, (v));                                                                            \
            }                                                                                                                  \
            else                                                                                                               \
            {                                                                                                                  \
                (segment)->execoffset = nn_allocator_mmapexecoffset((segment)->base, (segment)->size);                              \
                if(nn_allocator_mmapexecoffset((segment)->base + (segment)->execoffset, (segment)->size) != (segment)->execoffset) \
                {                                                                                                              \
                    NNALLOC_CONF_ABORTCALLBACK;                                                                               \
                }                                                                                                              \
                else                                                                                                           \
                {                                                                                                              \
                    (segment)->size) = 0;                                                                                      \
                    nn_allocator_mmapexecoffset((segment)->base, (segment)->size);                                                   \
                }                                                                                                              \
            }                                                                                                                  \
        }
#else
    #define nn_allocator_getsegmentflags(segment) ((segment)->sflags)
    #define nn_allocator_setsegmentflags(segment, v) ((segment)->sflags = (v))
    #define nn_allocator_checksegmentmerge(segment, b, s) (1)
#endif

#define nn_allocator_ismmappedsegment(segment) (nn_allocator_getsegmentflags(segment) & NNALLOC_CONST_ISMMAPPEDBIT)
#define nn_allocator_isexternsegment(segment) (nn_allocator_getsegmentflags(segment) & NNALLOC_CONST_EXTERNBIT)

#define nn_allocator_isglobal(M) ((M) == &g_allocvar_mallocstate)
#define nn_allocator_isinitialized(M) ((M)->top != 0)

/* -------------------------- system alloc setup ------------------------- */

/* Operations on mflags */

#if NNALLOC_CONF_USELOCKS
    #define nn_allocator_lockuse(M) ((M)->mflags & NNALLOC_CONST_USELOCKBIT)
    /* not currently in use ... */
    #if 0
        #define nn_allocator_lockenable(M) ((M)->mflags |= NNALLOC_CONST_USELOCKBIT)
        #define nn_allocator_lockdisable(M) ((M)->mflags &= ~NNALLOC_CONST_USELOCKBIT)
    #endif
    #define nn_allocator_lockset(M, L) ((M)->mflags = (L) ? ((M)->mflags | NNALLOC_CONST_USELOCKBIT) : ((M)->mflags & ~NNALLOC_CONST_USELOCKBIT))
#else
    #define nn_allocator_lockset(M, L)
#endif

#define nn_allocator_mmapuse(M) ((M)->mflags & NNALLOC_CONST_USEMMAPBIT)
#define nn_allocator_mmapenable(M) ((M)->mflags |= NNALLOC_CONST_USEMMAPBIT)
#define nn_allocator_mmapdisable(M) ((M)->mflags &= ~NNALLOC_CONST_USEMMAPBIT)

#define nn_allocator_usenoncontiguous(M) ((M)->mflags & NNALLOC_CONST_USENONCONTIGUOUSBIT)
#define nn_allocator_disablecontiguous(M) ((M)->mflags |= NNALLOC_CONST_USENONCONTIGUOUSBIT)

/* page-align a size */
#define nn_allocator_alignpage(sv) \
    (((sv) + (g_allocvar_mallocparams.pagesize)) & ~(g_allocvar_mallocparams.pagesize - NNALLOC_CONST_SIZETONE))

/* granularity-align a size */
#define nn_allocator_granularityalign(sv) \
    (((sv) + (g_allocvar_mallocparams.granularity)) & ~(g_allocvar_mallocparams.granularity - NNALLOC_CONST_SIZETONE))

#define nn_allocator_ispagealigned(sv) \
    (((size_t)(sv) & (g_allocvar_mallocparams.pagesize - NNALLOC_CONST_SIZETONE)) == 0)

/*  True if segment $segment holds address A */
#define nn_allocator_segmentholds(segment, A) \
    ((char*)(A) >= segment->base && (char*)(A) < segment->base + segment->size)


#ifndef MORECORE_CANNOT_TRIM
    #define nn_allocator_shouldtrim(M, s) ((s) > (M)->trimcheck)
#else
    #define nn_allocator_shouldtrim(M, s) (0)
#endif

/* -------------------------------  Hooks -------------------------------- */

/*
  NNALLOC_CMAC_PREACTION should be defined to return 0 on success, and nonzero on
  failure. If you are not using locking, you can redefine these to do
  anything you like.
*/

#if NNALLOC_CONF_USELOCKS

    /* Ensure locks are initialized */
    #define NNALLOC_CMAC_GLOBALLYINITIALIZE() (g_allocvar_mallocparams.pagesize == 0 && nn_allocator_mparams_init())

    #define NNALLOC_CMAC_PREACTION(M) ((NNALLOC_CMAC_GLOBALLYINITIALIZE() || nn_allocator_lockuse(M)) ? NNALLOC_CMAC_ACQUIRELOCK(&(M)->mutex) : 0)
    #define NNALLOC_CMAC_POSTACTION(M)                 \
        {                                               \
            if(nn_allocator_lockuse(M))                       \
            {                                           \
                NNALLOC_CMAC_RELEASELOCK(&(M)->mutex); \
            }                                           \
        }
#else
    #ifndef NNALLOC_CMAC_PREACTION
        #define NNALLOC_CMAC_PREACTION(M) (0)
    #endif
    #ifndef NNALLOC_CMAC_POSTACTION
        #define NNALLOC_CMAC_POSTACTION(M)
    #endif
#endif

/*
  NNALLOC_CMAC_CORRUPTIONERRORACTION is triggered upon detected bad addresses.
  NNALLOC_CMAC_USAGEERRORACTION is triggered on detected bad frees and
  reallocs. The argument p is an address that might have triggered the
  fault. It is ignored by the two predefined actions, but might be
  useful in custom actions that try to help diagnose errors.
*/

#if NNALLOC_CONF_PROCEEDONERROR
    #define NNALLOC_CMAC_CORRUPTIONERRORACTION(m) nn_allocator_resetonerror(m)
    #define NNALLOC_CMAC_USAGEERRORACTION(m, p)
#else
    #ifndef NNALLOC_CMAC_CORRUPTIONERRORACTION
        #define NNALLOC_CMAC_CORRUPTIONERRORACTION(m) NNALLOC_CONF_ABORTCALLBACK
    #endif
    #ifndef NNALLOC_CMAC_USAGEERRORACTION
        #define NNALLOC_CMAC_USAGEERRORACTION(m, p) NNALLOC_CONF_ABORTCALLBACK
    #endif
#endif

/* -------------------------- Debugging setup ---------------------------- */

#if !defined(NNALLOC_CONF_PERFORMCHECKS) || (NNALLOC_CONF_PERFORMCHECKS == 0)
    #define nn_allocator_checkfreechunk(M, P)
    #define nn_allocator_checkinusechunk(M, P)
    #define nn_allocator_checkmallocedchunk(M, P, N)
    #define nn_allocator_checkmmappedchunk(M, P)
    #define nn_allocator_checkmallocstate(M)
    #define nn_allocator_checktopchunk(M, P)
#else
    #define nn_allocator_checkfreechunk(M, P) nn_alloccheck_checkfreechunk(M, P)
    #define nn_allocator_checkinusechunk(M, P) nn_alloccheck_checkinusechunk(M, P)
    #define nn_allocator_checktopchunk(M, P) nn_alloccheck_checktopchunk(M, P)
    #define nn_allocator_checkmallocedchunk(M, P, N) nn_alloccheck_checkmallocedchunk(M, P, N)
    #define nn_allocator_checkmmappedchunk(M, P) nn_alloccheck_checkmmappedchunk(M, P)
    #define nn_allocator_checkmallocstate(M) nn_alloccheck_checkmallocstate(M)
#endif

/* ---------------------------- Indexing Bins ---------------------------- */

#define nn_allocator_issmall(s) (((s) >> NNALLOC_CONST_SMALLBINSHIFT) < NNALLOC_CONST_NSMALLBINS)
#define nn_allocator_smallindex(s) ((s) >> NNALLOC_CONST_SMALLBINSHIFT)
#define nn_allocator_smallindex2size(i) ((i) << NNALLOC_CONST_SMALLBINSHIFT)

/* addressing by index. See above about smallbin repositioning */
#define nn_allocator_smallbinat(M, i) ((nnallocchunkitem_t*)((char*)&((M)->smallbins[(i) << 1])))
#define nn_allocator_treebinat(M, i) (&((M)->treebins[i]))



/* Shift placing maximum resolved bit in a treebin at i as sign bit */
#define nn_allocator_leftshiftfortreeindex(i) \
    ((i == NNALLOC_CONST_NTREEBINS - 1) ? 0 : ((NNALLOC_CONST_SIZETBITSIZE - NNALLOC_CONST_SIZETONE) - (((i) >> 1) + NNALLOC_CONST_TREEBINSHIFT - 2)))

#if defined(NNALLOC_CONF_DEBUG) && (NNALLOC_CONF_DEBUG == 1)
    /* The size of the smallest chunk held in bin with index i */
    #define nn_allocator_minsizefortreeindex(i) \
        ((NNALLOC_CONST_SIZETONE << (((i) >> 1) + NNALLOC_CONST_TREEBINSHIFT)) | (((size_t)((i) & NNALLOC_CONST_SIZETONE)) << (((i) >> 1) + NNALLOC_CONST_TREEBINSHIFT - 1)))
#endif

/* ------------------------ Operations on bin maps ----------------------- */

/* bit corresponding to given index */
#define nn_allocator_idx2bit(i) ((nnallocbinmap_t)(1) << (i))

/* Mark/Clear bits with given index */
#define nn_allocator_marksmallmap(M, i) ((M)->smallmap |= nn_allocator_idx2bit(i))
#define nn_allocator_clearsmallmap(M, i) ((M)->smallmap &= ~nn_allocator_idx2bit(i))
#define nn_allocator_smallmapismarked(M, i) ((M)->smallmap & nn_allocator_idx2bit(i))

#define nn_allocator_marktreemap(M, i) ((M)->treemap |= nn_allocator_idx2bit(i))
#define nn_allocator_cleartreemap(M, i) ((M)->treemap &= ~nn_allocator_idx2bit(i))
#define nn_allocator_treemapismarked(M, i) ((M)->treemap & nn_allocator_idx2bit(i))

/* isolate the least set bit of a bitmap */
#define nn_allocator_leastbit(x) ((x) & -(x))

/* mask with all bits to left of least bit of x on */
#define nn_allocator_leftbits(x) ((x << 1) | -(x << 1))

/* ----------------------- Runtime Check Support ------------------------- */

/*
  For security, the main invariant is that malloc/free/etc never
  writes to a static address other than nnallocstate_t, unless static
  nnallocstate_t itself has been corrupted, which cannot occur via
  malloc (because of these checks). In essence this means that we
  believe all pointers, sizes, maps etc held in nnallocstate_t, but
  check all of those linked or offsetted from other embedded data
  structures.  These checks are interspersed with main code in a way
  that tends to minimize their run-time cost.

*/

#if !NNALLOC_CONF_ALLOWINSECURE
    /* Check if address a is at least as high as any from NNALLOC_CONF_MORECOREFUNCNAME or MMAP */
    #define nn_allocator_okaddress(M, a) (((char*)(a) >= ((M)->leastaddr)))
    /* Check if address of next chunk n is higher than base chunk p */
    #define nn_allocator_oknext(p, n) ((char*)(p) < (char*)(n))
    /* Check if p has its cinuse bit on */
    #define nn_allocator_okcinuse(p) nn_allocator_cinuse(p)
    /* Check if p has its pinuse bit on */
    #define nn_allocator_okpinuse(p) nn_allocator_pinuse(p)

#else /* !NNALLOC_CONF_ALLOWINSECURE */
    #define nn_allocator_okaddress(M, a) (1)
    #define nn_allocator_oknext(b, n) (1)
    #define nn_allocator_okcinuse(p) (1)
    #define nn_allocator_okpinuse(p) (1)
#endif

/* Check if (alleged) nnallocstate_t m has expected magic field */
#define nn_allocator_okmagic(M) ((M)->magictag == g_allocvar_mallocparams.magic)

/* In gcc, use __builtin_expect to minimize impact of checks */
#if !NNALLOC_CONF_ALLOWINSECURE
    #if defined(__GNUC__) && __GNUC__ >= 3
        #define NNALLOC_CMAC_RTCHECK(e) __builtin_expect((e), 1)
    #else
        #define NNALLOC_CMAC_RTCHECK(e) (e)
    #endif
#else
    #define NNALLOC_CMAC_RTCHECK(e) (1)
#endif

/* macros to set up inuse chunks with or without footers */

#define nn_allocator_markinusefoot(M, p, s)

/* Set cinuse bit and pinuse bit of next chunk */
#define nn_allocator_setinuse(M, p, s)                                                          \
    ((p)->head = (((p)->head & NNALLOC_CONST_PINUSEBIT) | s | NNALLOC_CONST_CINUSEBIT), \
     ((nnallocchunkitem_t*)(((char*)(p)) + (s)))->head |= NNALLOC_CONST_PINUSEBIT)

/* Set cinuse and pinuse of this chunk and pinuse of next chunk */
#define nn_allocator_setinuseandpinuse(M, p, s)                                   \
    ((p)->head = (s | NNALLOC_CONST_PINUSEBIT | NNALLOC_CONST_CINUSEBIT), \
     ((nnallocchunkitem_t*)(((char*)(p)) + (s)))->head |= NNALLOC_CONST_PINUSEBIT)

/* Set size, cinuse and pinuse bit of this chunk */
#define nn_allocator_setsizeandpinuseofinusechunk(M, p, s) \
    ((p)->head = (s | NNALLOC_CONST_PINUSEBIT | NNALLOC_CONST_CINUSEBIT))

/*
  mallopt tuning options.  SVID/XPG defines four standard parameter
  numbers for mallopt, normally defined in malloc.h.  None of these
  are used in this malloc, so setting them has no effect. But this
  malloc does support the following options.
*/

#define DLMALLOC_OPTMALLOPT_TRIMTHRESHOLD (-1)
#define DLMALLOC_OPTMALLOPT_GRANULARITY (-2)
#define DLMALLOC_OPTMALLOPT_MMAPTHRESHOLD (-3)




/*
  When chunks are not in use, they are treated as nodes of either
  lists or trees.

  "Small"  chunks are stored in circular doubly-linked lists, and look
  like this:

    chunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Size of previous chunk                            |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    `head:' |             Size of chunk, in bytes                         |P|
      mem-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Forward pointer to next chunk in list             |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Back pointer to previous chunk in list            |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Unused space (may be 0 bytes long)                .
            .                                                               .
            .                                                               |
nextchunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    `foot:' |             Size of chunk, in bytes                           |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

  Larger chunks are kept in a form of bitwise digital trees (aka
  tries) keyed on chunksizes.  Because malloc_tree_chunks are only for
  free chunks greater than 256 bytes, their size doesn't impose any
  constraints on user chunk sizes.  Each node looks like:

    chunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Size of previous chunk                            |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    `head:' |             Size of chunk, in bytes                         |P|
      mem-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Forward pointer to next chunk of same size        |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Back pointer to previous chunk of same size       |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Pointer to left child (child[0])                  |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Pointer to right child (child[1])                 |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Pointer to parent                                 |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             bin index of this chunk                           |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Unused space                                      .
            .                                                               |
nextchunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    `foot:' |             Size of chunk, in bytes                           |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

  Each tree holding treenodes is a tree of unique chunk sizes.  Chunks
  of the same size are arranged in a circularly-linked list, with only
  the oldest chunk (the next to be used, in our FIFO ordering)
  actually in the tree.  (Tree members are distinguished by a non-null
  parent pointer.)  If a chunk with the same size an an existing node
  is inserted, it is linked off the existing node using pointers that
  work in the same way as forwardptr/backwardptr pointers of small chunks.

  Each tree contains a power of 2 sized range of chunk sizes (the
  smallest is 0x100 <= x < 0x180), which is is divided in half at each
  tree level, with the chunks in the smaller half of the range (0x100
  <= x < 0x140 for the top nose) in the left subtree and the larger
  half (0x140 <= x < 0x180) in the right subtree.  This is, of course,
  done by inspecting individual bits.

  Using these rules, each node's left subtree contains all smaller
  sizes than its right subtree.  However, the node at the root of each
  subtree has no particular ordering relationship to either.  (The
  dividing line between the subtree sizes is based on trie relation.)
  If we remove the last chunk of a given size from the interior of the
  tree, we need to replace it with a leaf node.  The tree ordering
  rules permit a node to be replaced by any leaf below it.

  The smallest chunk in a tree (a common operation in a best-fit
  allocator) can be found by walking a path to the leftmost leaf in
  the tree.  Unlike a usual binary tree, where we follow left child
  pointers until we reach a null, here we follow the right child
  pointer any time the left one is null, until we reach a leaf with
  both child pointers null. The smallest chunk in the tree will be
  somewhere along that path.

  The worst case number of steps to add, find, or remove a node is
  bounded by the number of bits differentiating chunks within
  bins. Under current bin calculations, this ranges from 6 up to 21
  (for 32 bit sizes) or up to 53 (for 64 bit sizes). The typical case
  is of course much better.
*/

struct nnallocchunktree_t
{
    /* The first four fields must be compatible with nnallocchunkitem_t */
    size_t prevfoot;
    size_t head;
    nnallocchunktree_t* forwardptr;
    nnallocchunktree_t* backwardptr;
    nnallocchunktree_t* child[2];
    nnallocchunktree_t* parent;
    nnallocbindex_t index;
};

/*
   A nnallocstate_t holds all of the bookkeeping for a space.
   The main fields are:

  Top
    The topmost chunk of the currently active segment. Its size is
    cached in topsize.  The actual size of topmost space is
    topsize+nn_allocator_gettopfootsize(), which includes space reserved for adding
    fenceposts and segment records if necessary when getting more
    space from the system.  The size at which to autotrim top is
    cached from g_allocvar_mallocparams in trimcheck, except that it is disabled if
    an autotrim fails.

  Designated victim (dv)
    This is the preferred chunk for servicing small requests that
    don't have exact fits.  It is normally the chunk split off most
    recently to service another small request.  Its size is cached in
    designatvictimsize. The link fields of this chunk are not maintained since it
    is not kept in a bin.

  SmallBins
    An array of bin headers for free chunks.  These bins hold chunks
    with sizes less than NNALLOC_CONST_MINLARGESIZE bytes. Each bin contains
    chunks of all the same size, spaced 8 bytes apart.  To simplify
    use in double-linked lists, each bin header acts as a nnallocchunkitem_t
    pointing to the real first node, if it exists (else pointing to
    itself).  This avoids special-casing for headers.  But to avoid
    waste, we allocate only the forwardptr/backwardptr pointers of bins, and then use
    repositioning tricks to treat these as the fields of a chunk.

  TreeBins
    Treebins are pointers to the roots of trees holding a range of
    sizes. There are 2 equally spaced treebins for each power of two
    from TREE_SHIFT to TREE_SHIFT+16. The last bin holds anything
    larger.

  Bin maps
    There is one bit map for small bins ("smallmap") and one for
    treebins ("treemap).  Each bin sets its bit when non-empty, and
    clears the bit when empty.  Bit operations are then used to avoid
    bin-by-bin searching -- nearly all "search" is done without ever
    looking at bins that won't be selected.  The bit maps
    conservatively use 32 bits per map word, even if on 64bit system.
    For a good description of some of the bit-based techniques used
    here, see Henry S. Warren Jr's book "Hacker's Delight" (and
    supplement at http://hackersdelight.org/). Many of these are
    intended to reduce the branchiness of paths through malloc etc, as
    well as to reduce the number of memory locations read or written.

  Segments
    A list of segments headed by an embedded nnallocmemsegment_t record
    representing the initial space.

  Address check support
    The leastaddr field is the least address ever obtained from
    NNALLOC_CONF_MORECOREFUNCNAME or MMAP. Attempted frees and reallocs of any address less
    than this are trapped (unless NNALLOC_CONF_ALLOWINSECURE is defined).

  Magic tag
    A cross-check field that should always hold same value as g_allocvar_mallocparams.magic.

  Flags
    Bits recording whether to use MMAP, locks, or contiguous NNALLOC_CONF_MORECOREFUNCNAME

  Statistics
    Each space keeps track of current and maximum system memory
    obtained via NNALLOC_CONF_MORECOREFUNCNAME or MMAP.

  Locking
    If NNALLOC_CONF_USELOCKS is defined, the "mutex" lock is acquired and released
    around every public call using this mspace.
*/

/* Bin types, widths and sizes */
#define NNALLOC_CONST_NSMALLBINS (32U)
#define NNALLOC_CONST_NTREEBINS (32U)
#define NNALLOC_CONST_SMALLBINSHIFT (3U)
#define NNALLOC_CONST_TREEBINSHIFT (8U)
#define NNALLOC_CONST_MINLARGESIZE (NNALLOC_CONST_SIZETONE << NNALLOC_CONST_TREEBINSHIFT)
#define NNALLOC_CONST_MAXSMALLSIZE (NNALLOC_CONST_MINLARGESIZE - NNALLOC_CONST_SIZETONE)
#define NNALLOC_CONST_MAXSMALLREQUEST (NNALLOC_CONST_MAXSMALLSIZE - NNALLOC_CONST_CHUNKALIGNMASK - NNALLOC_CONST_CHUNKOVERHEAD)

struct nnallocstate_t
{
    nnallocbinmap_t smallmap;
    nnallocbinmap_t treemap;
    size_t designatvictimsize;
    size_t topsize;
    char* leastaddr;
    nnallocchunkitem_t* designatvictimchunk;
    nnallocchunkitem_t* top;
    size_t trimcheck;
    size_t magictag;
    nnallocchunkitem_t* smallbins[(NNALLOC_CONST_NSMALLBINS + 1) * 2];
    nnallocchunktree_t* treebins[NNALLOC_CONST_NTREEBINS];
    size_t footprint;
    size_t maxfootprint;
    nnallocflag_t mflags;
#if NNALLOC_CONF_USELOCKS
    /* locate lock among fields that rarely change */
    nnallocmutlock_t mutex;
#endif
    nnallocmemsegment_t seg;
};

/* ------------- Global nnallocstate_t and nnallocparams_t ------------------- */

/*
  nnallocparams_t holds global properties, including those that can be
  dynamically set using mallopt. There is a single instance, g_allocvar_mallocparams,
  initialized in nn_allocator_mparams_init.
*/

struct nnallocparams_t
{
    size_t magic;
    size_t pagesize;
    size_t granularity;
    size_t mmapthreshold;
    size_t trimthreshold;
    nnallocflag_t defaultmflags;
};

/* ------------------------ Mallinfo declarations ------------------------ */

/*
  This version of malloc supports the standard SVID/XPG mallinfo
  routine that returns a struct containing usage properties and
  statistics. It should work on any system that has a
  /usr/include/malloc.h defining struct mallinfo.  The main
  declaration needed is the mallinfo struct that is returned (by-copy)
  by mallinfo().  The malloinfo struct contains a bunch of fields that
  are not even meaningful in this version of malloc.  These fields are
  are instead filled by mallinfo() with other numbers that might be of
  interest.
*/

static nnallocparams_t g_allocvar_mallocparams;

/* The global nnallocstate_t used for all non-"mspace" calls */
static nnallocstate_t g_allocvar_mallocstate;

#if NNALLOC_CONF_USELOCKS
    #if !defined(NNALLOC_CONF_ISWIN32)
        /*
        * for various reasons, MUCC croaks if these are inited with PTHREAD_MUTEX_INITIALIZER.
        * that's why its necessary for nn_allocator_init() to exist;
        * they call the C functions to init these vars instead.
        */
        #if NNALLOC_CONF_HAVEMORECORE
            static nnallocmutlock_t g_allocvar_morecoremutex;
        #endif
        static nnallocmutlock_t g_allocvar_magicinitmutex;
    #else
        #if NNALLOC_CONF_HAVEMORECORE
            static nnallocmutlock_t g_allocvar_morecoremutex;
        #endif
        static nnallocmutlock_t g_allocvar_magicinitmutex;
    #endif
#endif

#if NNALLOC_CONF_PROCEEDONERROR

/* A count of the number of corruption errors causing resets */
static int g_allocvar_corruptionerrorcount = 0;

/* default corruption action */
NEON_FORCEINLINE void nn_allocator_resetonerror(nnallocstate_t* m);
#endif
#if defined(NNALLOC_CONF_PERFORMCHECKS) && (NNALLOC_CONF_PERFORMCHECKS == 1)
NEON_FORCEINLINE void nn_alloccheck_anychunk(nnallocstate_t* m, nnallocchunkitem_t* p);
NEON_FORCEINLINE void nn_alloccheck_checktopchunk(nnallocstate_t* m, nnallocchunkitem_t* p);
NEON_FORCEINLINE void nn_alloccheck_checkmmappedchunk(nnallocstate_t* m, nnallocchunkitem_t* p);
NEON_FORCEINLINE void nn_alloccheck_checkinusechunk(nnallocstate_t* m, nnallocchunkitem_t* p);
NEON_FORCEINLINE void nn_alloccheck_checkfreechunk(nnallocstate_t* m, nnallocchunkitem_t* p);
NEON_FORCEINLINE void nn_alloccheck_checkmallocedchunk(nnallocstate_t* m, void* mem, size_t s);
NEON_FORCEINLINE void nn_alloccheck_checktree(nnallocstate_t* m, nnallocchunktree_t* t);
NEON_FORCEINLINE void nn_alloccheck_checktreebin(nnallocstate_t* m, nnallocbindex_t i);
NEON_FORCEINLINE void nn_alloccheck_checksmallbin(nnallocstate_t* m, nnallocbindex_t i);
NEON_FORCEINLINE void nn_alloccheck_checkmallocstate(nnallocstate_t* m);
NEON_FORCEINLINE int nn_alloccheck_binfind(nnallocstate_t* m, nnallocchunkitem_t* x);
NEON_FORCEINLINE size_t nn_alloccheck_traverseandcheck(nnallocstate_t* m);
#endif

/* index corresponding to given bit */

void nn_allocator_init()
{
#if NNALLOC_CONF_USELOCKS
    #if !defined(NNALLOC_CONF_ISWIN32)
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        #if NNALLOC_CONF_HAVEMORECORE
            pthread_mutex_init(&g_allocvar_morecoremutex, &attr);
        #endif
        pthread_mutex_init(&g_allocvar_magicinitmutex, &attr);
    #endif
#endif
}

NEON_FORCEINLINE nnallocbindex_t nn_allocator_computebit2idx(nnallocbinmap_t xv)
{
    nnallocbindex_t res;
    #if NNALLOC_CONF_USEBUILTINFFSFUNC
        return (ffs(xv) - 1)
    #else
        unsigned int yv = (xv) - 1;
        unsigned int kv = yv >> (16 - 4) & 16;
        unsigned int nv = kv;
        yv >>= kv;
        nv += kv = yv >> (8 - 3) & 8;
        yv >>= kv;
        nv += kv = yv >> (4 - 2) & 4;
        yv >>= kv;
        nv += kv = yv >> (2 - 1) & 2;
        yv >>= kv;
        nv += kv = yv >> (1 - 0) & 1;
        yv >>= kv;
        res = (nnallocbindex_t)(nv + yv);
    #endif
    return res;
}

/* the number of bytes to offset an address to align it */
NEON_FORCEINLINE size_t nn_allocator_alignoffset(void* A)
{
    if(((size_t)(A) & NNALLOC_CONST_CHUNKALIGNMASK) == 0)
    {
        return 0;
    }
    return ((NNALLOC_CONF_MALLOCALIGNMENT - ((size_t)(A) & NNALLOC_CONST_CHUNKALIGNMASK)) & NNALLOC_CONST_CHUNKALIGNMASK);
}

/* assign tree index for size sz to variable I */
NEON_FORCEINLINE size_t nn_allocator_computetreeindex(size_t sz)
{
    size_t xi;
    size_t idx;
    unsigned int yi;
    unsigned int ni;
    unsigned int ki;
    idx = 0;
    xi = sz >> NNALLOC_CONST_TREEBINSHIFT;
    if(xi == 0)
    {
        idx = 0;
    }
    else if(xi > 0xFFFF)
    {
        idx = NNALLOC_CONST_NTREEBINS - 1;
    }
    else
    {
        yi = (unsigned int)xi;
        ni = ((yi - 0x100) >> 16) & 8;
        ki = (((yi <<= ni) - 0x1000) >> 16) & 4;
        ni += ki;
        ni += ki = (((yi <<= ki) - 0x4000) >> 16) & 2;
        ki = 14 - ni + ((yi <<= ki) >> 15);
        idx = (ki << 1) + ((sz >> (ki + (NNALLOC_CONST_TREEBINSHIFT - 1)) & 1));
    }
    return idx;
}

#if NNALLOC_CONF_HAVEMMAP
    #if defined(NNALLOC_CONF_ISWIN32)
        /* Win32 MMAP via VirtualAlloc */
        static void* nn_allocator_util_win32mmap(size_t size)
        {
            void* ptr;
            ptr = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if(ptr != 0)
            {
                return ptr;
            }
            return NNALLOC_CONST_MFAIL;
        }

        /* For direct MMAP, use MEM_TOP_DOWN to minimize interference */
        static void* nn_allocator_util_win32directmmap(size_t size)
        {
            void* ptr;
            ptr = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_EXECUTE_READWRITE);
            if(ptr != 0)
            {
                return ptr;
            }
            return NNALLOC_CONST_MFAIL;
        }

        /* This function supports releasing coalesed segments */
        static int nn_allocator_util_win32munmap(void* ptr, size_t size)
        {
            char* cptr;
            MEMORY_BASIC_INFORMATION minfo;
            cptr = ptr;
            while(size)
            {
                if(VirtualQuery(cptr, &minfo, sizeof(minfo)) == 0)
                {
                    return -1;
                }
                if(minfo.BaseAddress != cptr || minfo.AllocationBase != cptr || minfo.State != MEM_COMMIT || minfo.RegionSize > size)
                {
                    return -1;
                }
                if(VirtualFree(cptr, 0, MEM_RELEASE) == 0)
                {
                    return -1;
                }
                cptr += minfo.RegionSize;
                size -= minfo.RegionSize;
            }
            return 0;
        }
    #else
        /* Cached file descriptor for /dev/zero. */
        static int g_allocvar_devzerofd = -1;
        NEON_FORCEINLINE void* nn_allocator_callmmap(size_t s)
        {
            if(g_allocvar_devzerofd < 0)
            {
                g_allocvar_devzerofd = open("/dev/zero", O_RDWR);
            }
            return mmap(0, (s), NNALLOC_CONST_MMAPPROT, NNALLOC_CONST_MMAPFLAGS, g_allocvar_devzerofd, 0);
        }
    #endif
#endif


#if NNALLOC_CONF_USELOCKS
    #if defined(NNALLOC_CONF_ISWIN32)
        static int nn_allocator_util_win32acquirelock(nnallocmutlock_t* sl)
        {
            for(;;)
            {
                #ifdef InterlockedCompareExchangePointer
                if(!InterlockedCompareExchange(sl, 1, 0))
                {
                    return 0;
                }
                #else
                /* Use older void* version */
                if(!InterlockedCompareExchange((void**)sl, (void*)1, (void*)0))
                {
                    return 0;
                }
                #endif /* InterlockedCompareExchangePointer */
                Sleep(0);
            }
        }
        static void nn_allocator_util_win32releaselock(nnallocmutlock_t* sl)
        {
            InterlockedExchange(sl, 0);
        }
    #endif
#endif

/*
  nn_allocator_gettopfootsize() is padding at the end of a segment, including space
  that may be needed to place segment records and fenceposts when new
  noncontiguous segments are added.
*/
NEON_FORCEINLINE size_t nn_allocator_gettopfootsize()
{
    void* ptmp;
    void* dummy;
    size_t tmpa;
    size_t tmpb;
    dummy = NULL;
    ptmp = nn_allocator_chunk2mem(dummy);
    tmpa = nn_allocator_alignoffset(ptmp);
    tmpb = nn_allocator_padrequest(sizeof(nnallocmemsegment_t));
    return (tmpa + tmpb + NNALLOC_CONST_MINCHUNKSIZE);
}


/* Return segment holding given address */
NEON_FORCEINLINE nnallocmemsegment_t* nn_allocator_segment_holding(nnallocstate_t* m, char* addr)
{
    nnallocmemsegment_t* sp;
    sp = &m->seg;
    for(;;)
    {
        if(addr >= sp->base && addr < sp->base + sp->size)
        {
            return sp;
        }
        if((sp = sp->next) == 0)
        {
            return 0;
        }
    }
    return 0;
}

/* Return true if segment contains a segment link */
NEON_FORCEINLINE int nn_allocator_segment_haslink(nnallocstate_t* m, nnallocmemsegment_t* ss)
{
    nnallocmemsegment_t* sp;
    sp = &m->seg;
    for(;;)
    {
        if((char*)sp >= ss->base && (char*)sp < ss->base + ss->size)
        {
            return 1;
        }
        if((sp = sp->next) == 0)
        {
            return 0;
        }
    }
    return 0;
}


/* ---------------------------- setting g_allocvar_mallocparams -------------------------- */

/* Initialize g_allocvar_mallocparams */
static int nn_allocator_mparams_init()
{
    int ok;
    size_t s;
    #if defined(NNALLOC_CONF_ISWIN32)
        SYSTEM_INFO system_info;
    #endif
    if(g_allocvar_mallocparams.pagesize == 0)
    {

        g_allocvar_mallocparams.mmapthreshold = NNALLOC_CONF_DEFAULTMMAPTHRESHOLD;
        g_allocvar_mallocparams.trimthreshold = NNALLOC_CONF_DEFAULTTRIMTHRESHOLD;
#if NNALLOC_CONF_MORECORECONTIGUOUS
        g_allocvar_mallocparams.defaultmflags = NNALLOC_CONST_USELOCKBIT | NNALLOC_CONST_USEMMAPBIT;
#else
        g_allocvar_mallocparams.defaultmflags = NNALLOC_CONST_USELOCKBIT | NNALLOC_CONST_USEMMAPBIT | NNALLOC_CONST_USENONCONTIGUOUSBIT;
#endif

        s = (size_t)0x58585858U;
        NNALLOC_CMAC_ACQUIREMAGICINITLOCK();
        if(g_allocvar_mallocparams.magic == 0)
        {
            g_allocvar_mallocparams.magic = s;
            /* Set up lock for main malloc area */
            NNALLOC_CMAC_INITIALLOCK(&g_allocvar_mallocstate.mutex);
            g_allocvar_mallocstate.mflags = g_allocvar_mallocparams.defaultmflags;
        }
        NNALLOC_CMAC_RELEASEMAGICINITLOCK();

#if !defined(NNALLOC_CONF_ISWIN32)
        g_allocvar_mallocparams.pagesize = nn_allocator_malloc_getpagesize;
        g_allocvar_mallocparams.granularity = ((NNALLOC_CONF_DEFAULTGRANULARITY != 0) ? NNALLOC_CONF_DEFAULTGRANULARITY : g_allocvar_mallocparams.pagesize);

#else
        {
            GetSystemInfo(&system_info);
            g_allocvar_mallocparams.pagesize = system_info.dwPageSize;
            g_allocvar_mallocparams.granularity = system_info.dwAllocationGranularity;
        }
#endif

        /* Sanity-check configuration:
           size_t must be unsigned and as wide as pointer type.
           ints must be at least 4 bytes.
           alignment must be at least 8.
           Alignment, min chunk size, and page size must all be powers of 2.
        */
        ok = (
            (sizeof(size_t) != sizeof(char*)) ||
            (NNALLOC_CONF_MAXSIZET < NNALLOC_CONST_MINCHUNKSIZE) ||
            (sizeof(int) < 4) ||
            (NNALLOC_CONF_MALLOCALIGNMENT < (size_t)8U) ||
            ((NNALLOC_CONF_MALLOCALIGNMENT & (NNALLOC_CONF_MALLOCALIGNMENT - NNALLOC_CONST_SIZETONE)) != 0) ||
            ((NNALLOC_CONST_MCHUNKSIZE & (NNALLOC_CONST_MCHUNKSIZE - NNALLOC_CONST_SIZETONE)) != 0) ||
            ((g_allocvar_mallocparams.granularity & (g_allocvar_mallocparams.granularity - NNALLOC_CONST_SIZETONE)) != 0) ||
            ((g_allocvar_mallocparams.pagesize & (g_allocvar_mallocparams.pagesize - NNALLOC_CONST_SIZETONE)) != 0)
        );
        if(ok)
        {
            NNALLOC_CONF_ABORTCALLBACK;
        }
    }
    return 0;
}

/* support for mallopt */
static int nn_allocator_changemparam(int param_number, int value)
{
    size_t val;
    val = (size_t)value;
    nn_allocator_mparams_init();
    switch(param_number)
    {
        case DLMALLOC_OPTMALLOPT_TRIMTHRESHOLD:
            {
                g_allocvar_mallocparams.trimthreshold = val;
                return 1;
            }
            break;
        case DLMALLOC_OPTMALLOPT_GRANULARITY:
            {
                if(val >= g_allocvar_mallocparams.pagesize && ((val & (val - 1)) == 0))
                {
                    g_allocvar_mallocparams.granularity = val;
                    return 1;
                }
                else
                {
                    return 0;
                }
            }
            break;
        case DLMALLOC_OPTMALLOPT_MMAPTHRESHOLD:
            {
                g_allocvar_mallocparams.mmapthreshold = val;
                return 1;
            }
            break;
        default:
            {
                return 0;
            }
            break;
    }
    return 0;
}

#if defined(NNALLOC_CONF_PERFORMCHECKS) && (NNALLOC_CONF_PERFORMCHECKS == 1)
/* ------------------------- Debugging Support --------------------------- */

/* Check properties of any chunk, whether free, inuse, mmapped etc  */
NEON_FORCEINLINE void nn_alloccheck_anychunk(nnallocstate_t* m, nnallocchunkitem_t* p)
{
    NNALLOCASSERT((nn_allocator_isaligned(nn_allocator_chunk2mem(p))) || (p->head == NNALLOC_CONST_FENCEPOSTHEAD));
    NNALLOCASSERT(nn_allocator_okaddress(m, p));
}

/* Check properties of top chunk */
NEON_FORCEINLINE void nn_alloccheck_checktopchunk(nnallocstate_t* m, nnallocchunkitem_t* p)
{
    size_t sz;
    nnallocmemsegment_t* sp;
    sp = nn_allocator_segment_holding(m, (char*)p);
    sz = nn_allocator_chunksize(p);
    NNALLOCASSERT(sp != 0);
    NNALLOCASSERT((nn_allocator_isaligned(nn_allocator_chunk2mem(p))) || (p->head == NNALLOC_CONST_FENCEPOSTHEAD));
    NNALLOCASSERT(nn_allocator_okaddress(m, p));
    NNALLOCASSERT(sz == m->topsize);
    NNALLOCASSERT(sz > 0);
    NNALLOCASSERT(sz == ((sp->base + sp->size) - (char*)p) - nn_allocator_gettopfootsize());
    NNALLOCASSERT(nn_allocator_pinuse(p));
    NNALLOCASSERT(!nn_allocator_nextpinuse(p));
}

/* Check properties of (inuse) mmapped chunks */
NEON_FORCEINLINE void nn_alloccheck_checkmmappedchunk(nnallocstate_t* m, nnallocchunkitem_t* p)
{
    size_t sz;
    size_t len;
    sz = nn_allocator_chunksize(p);
    len = (sz + (p->prevfoot & ~NNALLOC_CONST_ISMMAPPEDBIT) + NNALLOC_CONST_MMAPFOOTPAD);
    NNALLOCASSERT(nn_allocator_ismmapped(p));
    NNALLOCASSERT(nn_allocator_mmapuse(m));
    NNALLOCASSERT((nn_allocator_isaligned(nn_allocator_chunk2mem(p))) || (p->head == NNALLOC_CONST_FENCEPOSTHEAD));
    NNALLOCASSERT(nn_allocator_okaddress(m, p));
    NNALLOCASSERT(!nn_allocator_issmall(sz));
    NNALLOCASSERT((len & (g_allocvar_mallocparams.pagesize - NNALLOC_CONST_SIZETONE)) == 0);
    NNALLOCASSERT(nn_allocator_chunkplusoffset(p, sz)->head == NNALLOC_CONST_FENCEPOSTHEAD);
    NNALLOCASSERT(nn_allocator_chunkplusoffset(p, sz + NNALLOC_CONST_SIZETSIZE)->head == 0);
}

/* Check properties of inuse chunks */
NEON_FORCEINLINE void nn_alloccheck_checkinusechunk(nnallocstate_t* m, nnallocchunkitem_t* p)
{
    nn_alloccheck_anychunk(m, p);
    NNALLOCASSERT(nn_allocator_cinuse(p));
    NNALLOCASSERT(nn_allocator_nextpinuse(p));
    /* If not pinuse and not mmapped, previous chunk has OK offset */
    NNALLOCASSERT(nn_allocator_ismmapped(p) || nn_allocator_pinuse(p) || nn_allocator_nextchunk(nn_allocator_prevchunk(p)) == p);
    if(nn_allocator_ismmapped(p))
    {
        nn_alloccheck_checkmmappedchunk(m, p);
    }
}

/* Check properties of free chunks */
NEON_FORCEINLINE void nn_alloccheck_checkfreechunk(nnallocstate_t* m, nnallocchunkitem_t* p)
{
    size_t sz;
    nnallocchunkitem_t* next;
    sz = p->head & ~(NNALLOC_CONST_PINUSEBIT | NNALLOC_CONST_CINUSEBIT);
    next = nn_allocator_chunkplusoffset(p, sz);
    nn_alloccheck_anychunk(m, p);
    NNALLOCASSERT(!nn_allocator_cinuse(p));
    NNALLOCASSERT(!nn_allocator_nextpinuse(p));
    NNALLOCASSERT(!nn_allocator_ismmapped(p));
    if(p != m->designatvictimchunk && p != m->top)
    {
        if(sz >= NNALLOC_CONST_MINCHUNKSIZE)
        {
            NNALLOCASSERT((sz & NNALLOC_CONST_CHUNKALIGNMASK) == 0);
            NNALLOCASSERT(nn_allocator_isaligned(nn_allocator_chunk2mem(p)));
            NNALLOCASSERT(next->prevfoot == sz);
            NNALLOCASSERT(nn_allocator_pinuse(p));
            NNALLOCASSERT(next == m->top || nn_allocator_cinuse(next));
            NNALLOCASSERT(p->forwardptr->backwardptr == p);
            NNALLOCASSERT(p->backwardptr->forwardptr == p);
        }
        else /* markers are always of size NNALLOC_CONST_SIZETSIZE */
        {
            NNALLOCASSERT(sz == NNALLOC_CONST_SIZETSIZE);
        }
    }
}

/* Check properties of malloced chunks at the point they are malloced */
NEON_FORCEINLINE void nn_alloccheck_checkmallocedchunk(nnallocstate_t* m, void* mem, size_t s)
{
    size_t sz;
    nnallocchunkitem_t* p;
    if(mem != 0)
    {
        p = nn_allocator_mem2chunk(mem);
        sz = p->head & ~(NNALLOC_CONST_PINUSEBIT | NNALLOC_CONST_CINUSEBIT);
        nn_alloccheck_checkinusechunk(m, p);
        NNALLOCASSERT((sz & NNALLOC_CONST_CHUNKALIGNMASK) == 0);
        NNALLOCASSERT(sz >= NNALLOC_CONST_MINCHUNKSIZE);
        NNALLOCASSERT(sz >= s);
        /* unless mmapped, size is less than NNALLOC_CONST_MINCHUNKSIZE more than request */
        NNALLOCASSERT(nn_allocator_ismmapped(p) || sz < (s + NNALLOC_CONST_MINCHUNKSIZE));
    }
}

/* Check a tree and its subtrees.  */
NEON_FORCEINLINE void nn_alloccheck_checktree(nnallocstate_t* m, nnallocchunktree_t* t)
{
    size_t tsize;
    nnallocbindex_t idx;
    nnallocbindex_t tindex;
    nnallocchunktree_t* u;
    nnallocchunktree_t* head;
    head = 0;
    u = t;
    tindex = t->index;
    tsize = nn_allocator_chunksize(t);
    idx = nn_allocator_computetreeindex(tsize);
    NNALLOCASSERT(tindex == idx);
    NNALLOCASSERT(tsize >= NNALLOC_CONST_MINLARGESIZE);
    NNALLOCASSERT(tsize >= nn_allocator_minsizefortreeindex(idx));
    NNALLOCASSERT((idx == NNALLOC_CONST_NTREEBINS - 1) || (tsize < nn_allocator_minsizefortreeindex((idx + 1))));

    do
    { /* traverse through chain of same-sized nodes */
        nn_alloccheck_anychunk(m, ((nnallocchunkitem_t*)u));
        NNALLOCASSERT(u->index == tindex);
        NNALLOCASSERT(nn_allocator_chunksize(u) == tsize);
        NNALLOCASSERT(!nn_allocator_cinuse(u));
        NNALLOCASSERT(!nn_allocator_nextpinuse(u));
        NNALLOCASSERT(u->forwardptr->backwardptr == u);
        NNALLOCASSERT(u->backwardptr->forwardptr == u);
        if(u->parent == 0)
        {
            NNALLOCASSERT(u->child[0] == 0);
            NNALLOCASSERT(u->child[1] == 0);
        }
        else
        {
            NNALLOCASSERT(head == 0); /* only one node on chain has parent */
            head = u;
            NNALLOCASSERT(u->parent != u);
            NNALLOCASSERT(u->parent->child[0] == u || u->parent->child[1] == u || *((nnallocchunktree_t**)(u->parent)) == u);
            if(u->child[0] != 0)
            {
                NNALLOCASSERT(u->child[0]->parent == u);
                NNALLOCASSERT(u->child[0] != u);
                nn_alloccheck_checktree(m, u->child[0]);
            }
            if(u->child[1] != 0)
            {
                NNALLOCASSERT(u->child[1]->parent == u);
                NNALLOCASSERT(u->child[1] != u);
                nn_alloccheck_checktree(m, u->child[1]);
            }
            if(u->child[0] != 0 && u->child[1] != 0)
            {
                NNALLOCASSERT(nn_allocator_chunksize(u->child[0]) < nn_allocator_chunksize(u->child[1]));
            }
        }
        u = u->forwardptr;
    } while(u != t);
    NNALLOCASSERT(head != 0);
}

/*  Check all the chunks in a treebin.  */
NEON_FORCEINLINE void nn_alloccheck_checktreebin(nnallocstate_t* m, nnallocbindex_t i)
{
    int empty;
    nnallocchunktree_t* t;
    nnallocchunktree_t** tb;
    tb = nn_allocator_treebinat(m, i);
    t = *tb;
    empty = (m->treemap & (1U << i)) == 0;
    if(t == 0)
    {
        NNALLOCASSERT(empty);
    }
    if(!empty)
    {
        nn_alloccheck_checktree(m, t);
    }
}

/*  Check all the chunks in a smallbin.  */
NEON_FORCEINLINE void nn_alloccheck_checksmallbin(nnallocstate_t* m, nnallocbindex_t i)
{
    size_t size;
    unsigned int empty;
    nnallocchunkitem_t* q;
    nnallocchunkitem_t* b;
    nnallocchunkitem_t* p;
    b = nn_allocator_smallbinat(m, i);
    p = b->backwardptr;
    empty = (m->smallmap & (1U << i)) == 0;
    if(p == b)
    {
        NNALLOCASSERT(empty);
    }
    if(!empty)
    {
        for(; p != b; p = p->backwardptr)
        {
            size = nn_allocator_chunksize(p);
            /* each chunk claims to be free */
            nn_alloccheck_checkfreechunk(m, p);
            /* chunk belongs in bin */
            NNALLOCASSERT(nn_allocator_smallindex(size) == i);
            NNALLOCASSERT(p->backwardptr == b || nn_allocator_chunksize(p->backwardptr) == nn_allocator_chunksize(p));
            /* chunk is followed by an inuse chunk */
            q = nn_allocator_nextchunk(p);
            if(q->head != NNALLOC_CONST_FENCEPOSTHEAD)
            {
                nn_alloccheck_checkinusechunk(m, q);
            }
        }
    }
}

/* Find x in a bin. Used in other check functions. */
NEON_FORCEINLINE int nn_alloccheck_binfind(nnallocstate_t* m, nnallocchunkitem_t* x)
{
    size_t size;
    size_t sizebits;
    nnallocbindex_t sidx;
    nnallocbindex_t tidx;
    nnallocchunkitem_t* b;
    nnallocchunkitem_t* p;
    nnallocchunktree_t* u;
    nnallocchunktree_t* t;
    size = nn_allocator_chunksize(x);
    if(nn_allocator_issmall(size))
    {
        sidx = nn_allocator_smallindex(size);
        b = nn_allocator_smallbinat(m, sidx);
        if(nn_allocator_smallmapismarked(m, sidx))
        {
            p = b;
            do
            {
                if(p == x)
                {
                    return 1;
                }
            } while((p = p->forwardptr) != b);
        }
    }
    else
    {
        tidx = nn_allocator_computetreeindex(size);
        if(nn_allocator_treemapismarked(m, tidx))
        {
            t = *nn_allocator_treebinat(m, tidx);
            sizebits = size << nn_allocator_leftshiftfortreeindex(tidx);
            while(t != 0 && nn_allocator_chunksize(t) != size)
            {
                t = t->child[(sizebits >> (NNALLOC_CONST_SIZETBITSIZE - NNALLOC_CONST_SIZETONE)) & 1];
                sizebits <<= 1;
            }
            if(t != 0)
            {
                u = t;
                do
                {
                    if(u == (nnallocchunktree_t*)x)
                    {
                        return 1;
                    }
                } while((u = u->forwardptr) != t);
            }
        }
    }
    return 0;
}

/* Traverse each chunk and check it; return total */
NEON_FORCEINLINE size_t nn_alloccheck_traverseandcheck(nnallocstate_t* m)
{
    size_t sum;
    nnallocmemsegment_t* s;
    nnallocchunkitem_t* q;
    nnallocchunkitem_t* lastq;
    sum = 0;
    if(nn_allocator_isinitialized(m))
    {
        s = &m->seg;
        sum += m->topsize + nn_allocator_gettopfootsize();
        while(s != 0)
        {
            q = nn_allocator_alignaschunk(s->base);
            lastq = 0;
            NNALLOCASSERT(nn_allocator_pinuse(q));
            while(nn_allocator_segmentholds(s, q) && q != m->top && q->head != NNALLOC_CONST_FENCEPOSTHEAD)
            {
                sum += nn_allocator_chunksize(q);
                if(nn_allocator_cinuse(q))
                {
                    NNALLOCASSERT(!nn_alloccheck_binfind(m, q));
                    nn_alloccheck_checkinusechunk(m, q);
                }
                else
                {
                    NNALLOCASSERT(q == m->designatvictimchunk || nn_alloccheck_binfind(m, q));
                    NNALLOCASSERT(lastq == 0 || nn_allocator_cinuse(lastq)); /* Not 2 consecutive free */
                    nn_alloccheck_checkfreechunk(m, q);
                }
                lastq = q;
                q = nn_allocator_nextchunk(q);
            }
            s = s->next;
        }
    }
    return sum;
}

/* Check all properties of nnallocstate_t. */
NEON_FORCEINLINE void nn_alloccheck_checkmallocstate(nnallocstate_t* m)
{
    nnallocbindex_t i;
    size_t total;
    /* check bins */
    for(i = 0; i < NNALLOC_CONST_NSMALLBINS; ++i)
    {
        nn_alloccheck_checksmallbin(m, i);
    }
    for(i = 0; i < NNALLOC_CONST_NTREEBINS; ++i)
    {
        nn_alloccheck_checktreebin(m, i);
    }
    if(m->designatvictimsize != 0)
    { /* check dv chunk */
        nn_alloccheck_anychunk(m, m->designatvictimchunk);
        NNALLOCASSERT(m->designatvictimsize == nn_allocator_chunksize(m->designatvictimchunk));
        NNALLOCASSERT(m->designatvictimsize >= NNALLOC_CONST_MINCHUNKSIZE);
        NNALLOCASSERT(nn_alloccheck_binfind(m, m->designatvictimchunk) == 0);
    }
    if(m->top != 0)
    { /* check top chunk */
        nn_alloccheck_checktopchunk(m, m->top);
        NNALLOCASSERT(m->topsize == nn_allocator_chunksize(m->top));
        NNALLOCASSERT(m->topsize > 0);
        NNALLOCASSERT(nn_alloccheck_binfind(m, m->top) == 0);
    }
    total = nn_alloccheck_traverseandcheck(m);
    NNALLOCASSERT(total <= m->footprint);
    NNALLOCASSERT(m->footprint <= m->maxfootprint);
}
#endif /* DEBUG */

/* ----------------------------- statistics ------------------------------ */

NEON_FORCEINLINE nnallocmallocinfo_t nn_allocator_mstate_internalmallinfo(nnallocstate_t* m)
{
    size_t sz;
    size_t sum;
    size_t mfree;
    size_t nfree;
    nnallocmemsegment_t* s;
    nnallocchunkitem_t* q;
    nnallocmallocinfo_t nm = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if(!NNALLOC_CMAC_PREACTION(m))
    {
        nn_allocator_checkmallocstate(m);
        if(nn_allocator_isinitialized(m))
        {
            nfree = NNALLOC_CONST_SIZETONE; /* top always free */
            mfree = m->topsize + nn_allocator_gettopfootsize();
            sum = mfree;
            s = &m->seg;
            while(s != 0)
            {
                q = nn_allocator_alignaschunk(s->base);
                while(nn_allocator_segmentholds(s, q) && q != m->top && q->head != NNALLOC_CONST_FENCEPOSTHEAD)
                {
                    sz = nn_allocator_chunksize(q);
                    sum += sz;
                    if(!nn_allocator_cinuse(q))
                    {
                        mfree += sz;
                        ++nfree;
                    }
                    q = nn_allocator_nextchunk(q);
                }
                s = s->next;
            }
            nm.arena = sum;
            nm.ordblks = nfree;
            nm.hblkhd = m->footprint - sum;
            nm.usmblks = m->maxfootprint;
            nm.uordblks = m->footprint - mfree;
            nm.fordblks = mfree;
            nm.keepcost = m->topsize;
        }
        NNALLOC_CMAC_POSTACTION(m);
    }
    return nm;
}

NEON_FORCEINLINE void nn_allocator_mstate_internalmallocstats(nnallocstate_t* m)
{
    size_t fp;
    size_t used;
    size_t maxfp;
    nnallocchunkitem_t* q;
    if(!NNALLOC_CMAC_PREACTION(m))
    {
        maxfp = 0;
        fp = 0;
        used = 0;
        nn_allocator_checkmallocstate(m);
        if(nn_allocator_isinitialized(m))
        {
            nnallocmemsegment_t* s = &m->seg;
            maxfp = m->maxfootprint;
            fp = m->footprint;
            used = fp - (m->topsize + nn_allocator_gettopfootsize());
            while(s != 0)
            {
                q = nn_allocator_alignaschunk(s->base);
                while(nn_allocator_segmentholds(s, q) && q != m->top && q->head != NNALLOC_CONST_FENCEPOSTHEAD)
                {
                    if(!nn_allocator_cinuse(q))
                    {
                        used -= nn_allocator_chunksize(q);
                    }
                    q = nn_allocator_nextchunk(q);
                }
                s = s->next;
            }
        }
        fprintf(stderr, "max system bytes = %10lu\n", (unsigned long)(maxfp));
        fprintf(stderr, "system bytes     = %10lu\n", (unsigned long)(fp));
        fprintf(stderr, "in use bytes     = %10lu\n", (unsigned long)(used));
        NNALLOC_CMAC_POSTACTION(m);
    }
}

/* ----------------------- Operations on smallbins ----------------------- */

/* Relays to internal calls to malloc/free from realloc, memalign etc */

NEON_FORCEINLINE void* nn_allocator_internalmalloc(nnallocmspace_t* msp, size_t sz)
{
#if NNALLOC_CONF_ONLYMSPACES
    return nn_allocator_mspacemalloc(msp, sz);
#else /* NNALLOC_CONF_ONLYMSPACES */
    if(msp == (&g_allocvar_mallocstate))
    {
        return nn_allocuser_malloc(sz);
    }
    return nn_allocator_mspacemalloc(msp, sz);
#endif /* NNALLOC_CONF_ONLYMSPACES */
}

NEON_FORCEINLINE void nn_allocator_internalfree(nnallocmspace_t *msp, void *mem)
{
#if NNALLOC_CONF_ONLYMSPACES
    return nn_allocator_mspacefree(msp, mem);
#else /* NNALLOC_CONF_ONLYMSPACES */
    if(msp == (&g_allocvar_mallocstate))
    {
        return nn_allocuser_free(mem);
    }
    else
    {
        return nn_allocator_mspacefree(msp, mem);
    }
#endif /* NNALLOC_CONF_ONLYMSPACES */
}

/*
  Various forms of linking and unlinking are defined as macros.  Even
  the ones for trees, which are very long but have very short typical
  paths.  This is ugly but reduces reliance on inlining support of
  compilers.
*/

/* Link a free chunk into a smallbin  */
NEON_FORCEINLINE void nn_allocator_insertsmallchunk(nnallocstate_t* mst, nnallocchunkitem_t* chunk, size_t segment)
{
    nnallocbindex_t bi = nn_allocator_smallindex(segment);
    nnallocchunkitem_t* bp = nn_allocator_smallbinat(mst, bi);
    nnallocchunkitem_t* fp = bp;
    NNALLOCASSERT(segment >= NNALLOC_CONST_MINCHUNKSIZE);
    if(!nn_allocator_smallmapismarked(mst, bi))
    {
        nn_allocator_marksmallmap(mst, bi);
    }
    else if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress(mst, bp->forwardptr)))
    {
        fp = bp->forwardptr;
    }
    else
    {
        NNALLOC_CMAC_CORRUPTIONERRORACTION(mst);
    }
    bp->forwardptr = chunk;
    fp->backwardptr = chunk;
    chunk->forwardptr = fp;
    chunk->backwardptr = bp;
}

/* Unlink a chunk from a smallbin  */
NEON_FORCEINLINE void nn_allocator_unlinksmallchunk(nnallocstate_t* mst, nnallocchunkitem_t* chunk, size_t segment)
{
    nnallocchunkitem_t* fp = chunk->forwardptr;
    nnallocchunkitem_t* bp = chunk->backwardptr;
    nnallocbindex_t I = nn_allocator_smallindex(segment);
    NNALLOCASSERT(chunk != bp);
    NNALLOCASSERT(chunk != fp);
    NNALLOCASSERT(nn_allocator_chunksize(chunk) == nn_allocator_smallindex2size(I));
    if(fp == bp)
    {
        nn_allocator_clearsmallmap(mst, I);
    }
    else if(NNALLOC_CMAC_RTCHECK((fp == nn_allocator_smallbinat(mst, I) || nn_allocator_okaddress(mst, fp)) && (bp == nn_allocator_smallbinat(mst, I) || nn_allocator_okaddress(mst, bp))))
    {
        fp->backwardptr = bp;
        bp->forwardptr = fp;
    }
    else
    {
        NNALLOC_CMAC_CORRUPTIONERRORACTION(mst);
    }
}

/* Unlink the first chunk from a smallbin */
NEON_FORCEINLINE void nn_allocator_unlinkfirstsmallchunk(nnallocstate_t* mst, nnallocchunkitem_t* bp, nnallocchunkitem_t* chunk, size_t idx)
{
    nnallocchunkitem_t* fp = chunk->forwardptr;
    NNALLOCASSERT(chunk != bp);
    NNALLOCASSERT(chunk != fp);
    NNALLOCASSERT(nn_allocator_chunksize(chunk) == nn_allocator_smallindex2size(idx));
    if(bp == fp)
    {
        nn_allocator_clearsmallmap(mst, idx);
    }
    else if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress(mst, fp)))
    {
        bp->forwardptr = fp;
        fp->backwardptr = bp;
    }
    else
    {
        NNALLOC_CMAC_CORRUPTIONERRORACTION(mst);
    }   
}

/* Replace dv node, binning the old one */
/* Used only when designatvictimsize known to be small */
NEON_FORCEINLINE void nn_allocator_replacedv(nnallocstate_t* mst, nnallocchunkitem_t* chunk, size_t segment)
{
    size_t DVS = (mst)->designatvictimsize;
    if(DVS != 0)
    {
        nnallocchunkitem_t* DV = (mst)->designatvictimchunk;
        NNALLOCASSERT(nn_allocator_issmall(DVS));
        nn_allocator_insertsmallchunk(mst, DV, DVS);
    }
    (mst)->designatvictimsize = segment;
    (mst)->designatvictimchunk = chunk;
}

/* ------------------------- Operations on trees ------------------------- */

/* Insert chunk into tree */

NEON_FORCEINLINE void nn_allocator_insertlargechunk(nnallocstate_t* mst, nnallocchunktree_t* chunk, size_t sv)
{
    size_t k;
    nnallocbindex_t bi;
    nnallocchunktree_t* f;
    nnallocchunktree_t* t;
    nnallocchunktree_t** h;
    nnallocchunktree_t** c;
    bi = nn_allocator_computetreeindex(sv);
    h = nn_allocator_treebinat((mst), bi);
    (chunk)->index = bi;
    (chunk)->child[0] = (chunk)->child[1] = 0;
    if(!nn_allocator_treemapismarked((mst), bi))
    {
        nn_allocator_marktreemap((mst), bi);
        *h = (chunk);
        (chunk)->parent = (nnallocchunktree_t*)h;
        (chunk)->forwardptr = (chunk)->backwardptr = (chunk);
    }
    else
    {
        t = *h;
        k = (sv) << nn_allocator_leftshiftfortreeindex(bi);
        for(;;)
        {
            if(nn_allocator_chunksize(t) != (sv))
            {
                c = &(t->child[(k >> (NNALLOC_CONST_SIZETBITSIZE - NNALLOC_CONST_SIZETONE)) & 1]);
                k <<= 1;
                if(*c != 0)
                {
                    t = *c;
                }
                else if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress((mst), c)))
                {
                    *c = (chunk);
                    (chunk)->parent = t;
                    (chunk)->forwardptr = (chunk)->backwardptr = (chunk);
                    break;
                }
                else
                {
                    NNALLOC_CMAC_CORRUPTIONERRORACTION((mst));
                    break;
                }
            }
            else
            {
                f = t->forwardptr;
                if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress((mst), t) && nn_allocator_okaddress((mst), f)))
                {
                    t->forwardptr = f->backwardptr = (chunk);
                    (chunk)->forwardptr = f;
                    (chunk)->backwardptr = t;
                    (chunk)->parent = 0;
                    break;
                }
                else
                {
                    NNALLOC_CMAC_CORRUPTIONERRORACTION((mst));
                    break;
                }
            }
        }
    }
}

/*
  Unlink steps:

  1. If x is a chained node, unlink it from its same-sized forwardptr/backwardptr links
     and choose its backwardptr node as its replacement.
  2. If x was the last node of its size, but not a leaf node, it must
     be replaced with a leaf node (not merely one with an open left or
     right), to make sure that lefts and rights of descendants
     correspond properly to bit masks.  We use the rightmost descendant
     of x.  We could use any other leaf, but this is easy to locate and
     tends to counteract removal of leftmosts elsewhere, and so keeps
     paths shorter than minimally guaranteed.  This doesn't loop much
     because on average a node in a tree is near the bottom.
  3. If x is the base of a chain (i.e., has parent links) relink
     x's parent and children to x's replacement (or null if none).
*/

NEON_FORCEINLINE void nn_allocator_unlinklargechunk(nnallocstate_t* mst, nnallocchunktree_t* chunk)
{
    nnallocchunktree_t* r;
    nnallocchunktree_t* f;
    nnallocchunktree_t** h;
    nnallocchunktree_t* xp;
    nnallocchunktree_t** rp;
    nnallocchunktree_t** cp;
    nnallocchunktree_t* c0;
    nnallocchunktree_t* c1;
    xp = (chunk)->parent;
    if((chunk)->backwardptr != (chunk))
    {
        f = (chunk)->forwardptr;
        r = (chunk)->backwardptr;
        if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress((mst), f)))
        {
            f->backwardptr = r;
            r->forwardptr = f;
        }
        else
        {
            NNALLOC_CMAC_CORRUPTIONERRORACTION((mst));
        }
    }
    else
    {
        if(((r = *(rp = &((chunk)->child[1]))) != 0) || ((r = *(rp = &((chunk)->child[0]))) != 0))
        {
            while((*(cp = &(r->child[1])) != 0) || (*(cp = &(r->child[0])) != 0))
            {
                r = *(rp = cp);
            }
            if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress((mst), rp)))
            {
                *rp = 0;
            }
            else
            {
                NNALLOC_CMAC_CORRUPTIONERRORACTION(mst);
            }
        }
    }
    if(xp != 0)
    {
        h = nn_allocator_treebinat((mst), (chunk)->index);
        if((chunk) == *h)
        {
            if((*h = r) == 0)
            {
                nn_allocator_cleartreemap(mst, (chunk)->index);
            }
        }
        else if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress(mst, xp)))
        {
            if(xp->child[0] == (chunk))
            {
                xp->child[0] = r;
            }
            else
            {
                xp->child[1] = r;
            }
        }
        else
        {
            NNALLOC_CMAC_CORRUPTIONERRORACTION(mst);
        }
        if(r != 0)
        {
            if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress(mst, r)))
            {
                r->parent = xp;
                if((c0 = (chunk)->child[0]) != 0)
                {
                    if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress(mst, c0)))
                    {
                        r->child[0] = c0;
                        c0->parent = r;
                    }
                    else
                    {
                        NNALLOC_CMAC_CORRUPTIONERRORACTION(mst);
                    }
                }
                if((c1 = (chunk)->child[1]) != 0)
                {
                    if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress(mst, c1)))
                    {
                        r->child[1] = c1;
                        c1->parent = r;
                    }
                    else
                    {
                        NNALLOC_CMAC_CORRUPTIONERRORACTION(mst);
                    }
                }
            }
            else
            {
                NNALLOC_CMAC_CORRUPTIONERRORACTION(mst);
            }
        }
    }
}

/* Relays to large vs small bin operations */
NEON_FORCEINLINE void nn_allocator_insertchunk(nnallocstate_t* msp, nnallocchunkitem_t* chunk, size_t sv)
{
    nnallocchunktree_t* tmpchunk;
    if(nn_allocator_issmall(sv))
    {
        nn_allocator_insertsmallchunk(msp, chunk, sv);
    }
    else
    {
        tmpchunk = (nnallocchunktree_t*)(chunk);
        nn_allocator_insertlargechunk(msp, tmpchunk, sv);
    }
}

NEON_FORCEINLINE void nn_allocator_unlinkchunk(nnallocstate_t* msp, nnallocchunkitem_t* chunk, size_t sv)
{
    nnallocchunktree_t* tmpchunk;
    if(nn_allocator_issmall(sv))
    {
        nn_allocator_unlinksmallchunk(msp, chunk, sv);
    }
    else
    {
        tmpchunk = (nnallocchunktree_t*)(chunk);
        nn_allocator_unlinklargechunk(msp, tmpchunk);
    }
}

/* -----------------------  Direct-mmapping chunks ----------------------- */

/*
  Directly mmapped chunks are set up with an offset to the start of
  the mmapped region stored in the prevfoot field of the chunk. This
  allows reconstruction of the required argument to MUNMAP when freed,
  and also allows adjustment of the returned chunk to meet alignment
  requirements (especially in memalign).  There is also enough space
  allocated to hold a fake next chunk of size NNALLOC_CONST_SIZETSIZE to maintain
  the PINUSE bit so frees can be checked.
*/

/* Malloc using mmap */
static void* nn_allocator_mstate_mmapalloc(nnallocstate_t* m, size_t nb)
{
    size_t mmsize;
    size_t offset;
    size_t psize;
    char* mm;
    nnallocchunkitem_t* p;
    mmsize = nn_allocator_granularityalign(nb + NNALLOC_CONST_SIXSIZETSIZES + NNALLOC_CONST_CHUNKALIGNMASK);
    if(mmsize > nb)
    { /* Check for wrap around 0 */
        mm = (char*)(NNALLOC_CMAC_DIRECTMMAP(mmsize));
        if(mm != NNALLOC_CONST_CMFAIL)
        {
            offset = nn_allocator_alignoffset(nn_allocator_chunk2mem(mm));
            psize = mmsize - offset - NNALLOC_CONST_MMAPFOOTPAD;
            p = (nnallocchunkitem_t*)(mm + offset);
            p->prevfoot = offset | NNALLOC_CONST_ISMMAPPEDBIT;
            (p)->head = (psize | NNALLOC_CONST_CINUSEBIT);
            nn_allocator_markinusefoot(m, p, psize);
            nn_allocator_chunkplusoffset(p, psize)->head = NNALLOC_CONST_FENCEPOSTHEAD;
            nn_allocator_chunkplusoffset(p, psize + NNALLOC_CONST_SIZETSIZE)->head = 0;
            if(mm < m->leastaddr)
            {
                m->leastaddr = mm;
            }
            if((m->footprint += mmsize) > m->maxfootprint)
            {
                m->maxfootprint = m->footprint;
            }
            NNALLOCASSERT(nn_allocator_isaligned(nn_allocator_chunk2mem(p)));
            nn_allocator_checkmmappedchunk(m, p);
            return nn_allocator_chunk2mem(p);
        }
    }
    return 0;
}

/* Realloc using mmap */
static nnallocchunkitem_t* nn_allocator_mstate_mmapresize(nnallocstate_t* m, nnallocchunkitem_t* oldp, size_t nb)
{
    size_t oldsize;
    size_t offset;
    size_t oldmmsize;
    size_t newmmsize;
    size_t psize;
    char* cp;
    nnallocchunkitem_t* newp;
    oldsize = nn_allocator_chunksize(oldp);
    if(nn_allocator_issmall(nb)) /* Can't shrink mmap regions below small size */
    {
        return 0;
    }
    /* Keep old chunk if big enough but not too big */
    if(oldsize >= nb + NNALLOC_CONST_SIZETSIZE && (oldsize - nb) <= (g_allocvar_mallocparams.granularity << 1))
    {
        return oldp;
    }
    else
    {
        offset = oldp->prevfoot & ~NNALLOC_CONST_ISMMAPPEDBIT;
        oldmmsize = oldsize + offset + NNALLOC_CONST_MMAPFOOTPAD;
        newmmsize = nn_allocator_granularityalign(nb + NNALLOC_CONST_SIXSIZETSIZES + NNALLOC_CONST_CHUNKALIGNMASK);
        cp = (char*)NNALLOC_CMAC_CALLMREMAP((char*)oldp - offset, oldmmsize, newmmsize, 1);
        if(cp != NNALLOC_CONST_CMFAIL)
        {
            newp = (nnallocchunkitem_t*)(cp + offset);
            psize = newmmsize - offset - NNALLOC_CONST_MMAPFOOTPAD;
            newp->head = (psize | NNALLOC_CONST_CINUSEBIT);
            nn_allocator_markinusefoot(m, newp, psize);
            nn_allocator_chunkplusoffset(newp, psize)->head = NNALLOC_CONST_FENCEPOSTHEAD;
            nn_allocator_chunkplusoffset(newp, psize + NNALLOC_CONST_SIZETSIZE)->head = 0;
            if(cp < m->leastaddr)
            {
                m->leastaddr = cp;
            }
            if((m->footprint += newmmsize - oldmmsize) > m->maxfootprint)
            {
                m->maxfootprint = m->footprint;
            }
            nn_allocator_checkmmappedchunk(m, newp);
            return newp;
        }
    }
    return 0;
}

/* -------------------------- mspace management -------------------------- */

/* Initialize top chunk and its size */
NEON_FORCEINLINE void nn_allocator_mstate_inittop(nnallocstate_t* m, nnallocchunkitem_t* p, size_t psize)
{
    size_t offset;
    /* Ensure alignment */
    offset = nn_allocator_alignoffset(nn_allocator_chunk2mem(p));
    p = (nnallocchunkitem_t*)((char*)p + offset);
    psize -= offset;
    m->top = p;
    m->topsize = psize;
    p->head = psize | NNALLOC_CONST_PINUSEBIT;
    /* set size of fake trailing chunk holding overhead space only once */
    nn_allocator_chunkplusoffset(p, psize)->head = nn_allocator_gettopfootsize();
    m->trimcheck = g_allocvar_mallocparams.trimthreshold; /* reset on each update */
}

/* Initialize bins for a new nnallocstate_t* that is otherwise zeroed out */
NEON_FORCEINLINE void nn_allocator_mstate_initbins(nnallocstate_t* m)
{
    /* Establish circular links for smallbins */
    nnallocbindex_t i;
    nnallocchunkitem_t* bin;
    for(i = 0; i < NNALLOC_CONST_NSMALLBINS; ++i)
    {
        bin = nn_allocator_smallbinat(m, i);
        bin->forwardptr = bin->backwardptr = bin;
    }
}

#if NNALLOC_CONF_PROCEEDONERROR

/* default corruption action */
NEON_FORCEINLINE void nn_allocator_resetonerror(nnallocstate_t* m)
{
    int i;
    ++g_allocvar_corruptionerrorcount;
    /* Reinitialize fields to forget about all memory */
    m->designatvictimsize = 0;
    m->topsize = 0;
    m->seg.base = 0;
    m->seg.size = 0;
    m->seg.next = 0;
    m->top = 0;
    m->designatvictimchunk = 0;
    for(i = 0; i < NNALLOC_CONST_NTREEBINS; ++i)
    {
        *nn_allocator_treebinat(m, i) = 0;
    }
    nn_allocator_mstate_initbins(m);
}
#endif /* NNALLOC_CONF_PROCEEDONERROR */

/* Allocate chunk and prepend remainder with chunk in successor base. */
static void* nn_allocator_mstate_prependalloc(nnallocstate_t* m, char* newbase, char* oldbase, size_t nb)
{
    size_t nsize;
    size_t dsize;
    size_t tsize;
    size_t qsize;
    size_t psize;
    nnallocchunkitem_t* p;
    nnallocchunkitem_t* q;
    nnallocchunkitem_t* oldfirst;
    p = nn_allocator_alignaschunk(newbase);
    oldfirst = nn_allocator_alignaschunk(oldbase);
    psize = (char*)oldfirst - (char*)p;
    q = nn_allocator_chunkplusoffset(p, nb);
    qsize = psize - nb;
    nn_allocator_setsizeandpinuseofinusechunk(m, p, nb);
    NNALLOCASSERT((char*)oldfirst > (char*)q);
    NNALLOCASSERT(nn_allocator_pinuse(oldfirst));
    NNALLOCASSERT(qsize >= NNALLOC_CONST_MINCHUNKSIZE);
    /* consolidate remainder with first chunk of old base */
    if(oldfirst == m->top)
    {
        tsize = m->topsize += qsize;
        m->top = q;
        q->head = tsize | NNALLOC_CONST_PINUSEBIT;
        nn_allocator_checktopchunk(m, q);
    }
    else if(oldfirst == m->designatvictimchunk)
    {
        dsize = m->designatvictimsize += qsize;
        m->designatvictimchunk = q;
        nn_allocator_setsizeandpinuseoffreechunk(q, dsize);
    }
    else
    {
        if(!nn_allocator_cinuse(oldfirst))
        {
            nsize = nn_allocator_chunksize(oldfirst);
            nn_allocator_unlinkchunk(m, oldfirst, nsize);
            oldfirst = nn_allocator_chunkplusoffset(oldfirst, nsize);
            qsize += nsize;
        }
        nn_allocator_setfreewithpinuse(q, qsize, oldfirst);
        nn_allocator_insertchunk(m, q, qsize);
        nn_allocator_checkfreechunk(m, q);
    }

    nn_allocator_checkmallocedchunk(m, nn_allocator_chunk2mem(p), nb);
    return nn_allocator_chunk2mem(p);
}

/* Add a segment to hold a new noncontiguous region */
static void nn_allocator_mstate_addsegment(nnallocstate_t* m, char* tbase, size_t tsize, nnallocflag_t mmapped)
{
    size_t psize;
    size_t ssize;
    size_t offset;
    char* old_top;
    char* old_end;
    char* rawsp;
    char* asp;
    char* csp;
    int nfences;
    nnallocchunkitem_t* sp;
    nnallocmemsegment_t* ss;
    nnallocchunkitem_t* tnext;
    nnallocchunkitem_t* p;
    nnallocchunkitem_t* nextp;
    nnallocchunkitem_t* q;
    nnallocchunkitem_t* tn;
    nnallocmemsegment_t* oldsp;
    (void)nfences;
    /* Determine locations and sizes of segment, fenceposts, old top */
    old_top = (char*)m->top;
    oldsp = nn_allocator_segment_holding(m, old_top);
    old_end = oldsp->base + oldsp->size;
    ssize = nn_allocator_padrequest(sizeof(nnallocmemsegment_t));
    rawsp = old_end - (ssize + NNALLOC_CONST_FOURSIZETSIZES + NNALLOC_CONST_CHUNKALIGNMASK);
    offset = nn_allocator_alignoffset(nn_allocator_chunk2mem(rawsp));
    asp = rawsp + offset;
    csp = (asp < (old_top + NNALLOC_CONST_MINCHUNKSIZE)) ? old_top : asp;
    sp = (nnallocchunkitem_t*)csp;
    ss = (nnallocmemsegment_t*)(nn_allocator_chunk2mem(sp));
    tnext = nn_allocator_chunkplusoffset(sp, ssize);
    p = tnext;
    nfences = 0;
    /* reset top to new space */
    nn_allocator_mstate_inittop(m, (nnallocchunkitem_t*)tbase, tsize - nn_allocator_gettopfootsize());
    /* Set up segment record */
    NNALLOCASSERT(nn_allocator_isaligned(ss));
    nn_allocator_setsizeandpinuseofinusechunk(m, sp, ssize);
    *ss = m->seg; /* Push current record */
    m->seg.base = tbase;
    m->seg.size = tsize;
    nn_allocator_setsegmentflags(&m->seg, mmapped);
    m->seg.next = ss;
    /* Insert trailing fenceposts */
    for(;;)
    {
        nextp = nn_allocator_chunkplusoffset(p, NNALLOC_CONST_SIZETSIZE);
        p->head = NNALLOC_CONST_FENCEPOSTHEAD;
        ++nfences;
        if((char*)(&(nextp->head)) < old_end)
        {
            p = nextp;
        }
        else
        {
            break;
        }
    }
    NNALLOCASSERT(nfences >= 2);
    /* Insert the rest of old top into a bin as an ordinary free chunk */
    if(csp != old_top)
    {
        q = (nnallocchunkitem_t*)old_top;
        psize = csp - old_top;
        tn = nn_allocator_chunkplusoffset(q, psize);
        nn_allocator_setfreewithpinuse(q, psize, tn);
        nn_allocator_insertchunk(m, q, psize);
    }
    nn_allocator_checktopchunk(m, m->top);
}

/* -------------------------- System allocation -------------------------- */

/* Get memory from system using NNALLOC_CONF_MORECOREFUNCNAME or MMAP */
static void* nn_allocator_mstate_sysalloc(nnallocstate_t* m, size_t nb)
{

    size_t asize;
    size_t esize;
    size_t req;
    size_t rsize;
    size_t ssize;
    size_t tsize;
    void* mem;
    char* base;
    char* br;
    char* end;
    char* mp;
    char* oldbase;
    char* tbase;
    nnallocflag_t mmap_flag;
    nnallocchunkitem_t* mn;
    nnallocchunkitem_t* p;
    nnallocchunkitem_t* r;        
    nnallocmemsegment_t* sp;
    nnallocmemsegment_t* ss;
    tbase = NNALLOC_CONST_CMFAIL;
    tsize = 0;
    mmap_flag = 0;
    nn_allocator_mparams_init();
    /* Directly map large chunks */
    if(nn_allocator_mmapuse(m) && nb >= g_allocvar_mallocparams.mmapthreshold)
    {
        mem = nn_allocator_mstate_mmapalloc(m, nb);
        if(mem != 0)
        {
            return mem;
        }
    }
    /*
      Try getting memory in any of three ways (in most-preferred to
      least-preferred order):
      1. A call to NNALLOC_CONF_MORECOREFUNCNAME that can normally contiguously extend memory.
         (disabled if not NNALLOC_CONF_MORECORECONTIGUOUS or not NNALLOC_CONF_HAVEMORECORE or
         or main space is mmapped or a previous contiguous call failed)
      2. A call to MMAP new space (disabled if not NNALLOC_CONF_HAVEMMAP).
         Note that under the default settings, if NNALLOC_CONF_MORECOREFUNCNAME is unable to
         fulfill a request, and NNALLOC_CONF_HAVEMMAP is true, then mmap is
         used as a noncontiguous system allocator. This is a useful backup
         strategy for systems with holes in address spaces -- in this case
         sbrk cannot contiguously expand the heap, but mmap may be able to
         find space.
      3. A call to NNALLOC_CONF_MORECOREFUNCNAME that cannot usually contiguously extend memory.
         (disabled if not NNALLOC_CONF_HAVEMORECORE)
    */
    if(NNALLOC_CONF_MORECORECONTIGUOUS && !nn_allocator_usenoncontiguous(m))
    {
        br = NNALLOC_CONST_CMFAIL;
        ss = (m->top == 0) ? 0 : nn_allocator_segment_holding(m, (char*)m->top);
        asize = 0;
        NNALLOC_CMAC_ACQUIREMORECORELOCK();
        if(ss == 0)
        { /* First time through or recovery */
            base = (char*)NNALLOC_CMAC_CALLMORECORE(0);
            if(base != NNALLOC_CONST_CMFAIL)
            {
                asize = nn_allocator_granularityalign(nb + nn_allocator_gettopfootsize() + NNALLOC_CONST_SIZETONE);
                /* Adjust to end on a page boundary */
                if(!nn_allocator_ispagealigned(base))
                {
                    asize += (nn_allocator_alignpage((size_t)base) - (size_t)base);
                }
                /* Can't call NNALLOC_CONF_MORECOREFUNCNAME if size is negative when treated as signed */
                if(asize < NNALLOC_CONST_HALFMAXSIZET && (br = (char*)(NNALLOC_CMAC_CALLMORECORE(asize))) == base)
                {
                    tbase = base;
                    tsize = asize;
                }
            }
        }
        else
        {
            /* Subtract out existing available top space from NNALLOC_CONF_MORECOREFUNCNAME request. */
            asize = nn_allocator_granularityalign(nb - m->topsize + nn_allocator_gettopfootsize() + NNALLOC_CONST_SIZETONE);
            /* Use mem here only if it did continuously extend old space */
            if(asize < NNALLOC_CONST_HALFMAXSIZET && (br = (char*)(NNALLOC_CMAC_CALLMORECORE(asize))) == ss->base + ss->size)
            {
                tbase = br;
                tsize = asize;
            }
        }
        if(tbase == NNALLOC_CONST_CMFAIL)
        { /* Cope with partial failure */
            if(br != NNALLOC_CONST_CMFAIL)
            { /* Try to use/extend the space we did get */
                if(asize < NNALLOC_CONST_HALFMAXSIZET && asize < nb + nn_allocator_gettopfootsize() + NNALLOC_CONST_SIZETONE)
                {
                    esize = nn_allocator_granularityalign(nb + nn_allocator_gettopfootsize() + NNALLOC_CONST_SIZETONE - asize);
                    if(esize < NNALLOC_CONST_HALFMAXSIZET)
                    {
                        end = (char*)NNALLOC_CMAC_CALLMORECORE(esize);
                        if(end != NNALLOC_CONST_CMFAIL)
                        {
                            asize += esize;
                        }
                        else
                        { /* Can't use; try to release */
                            NNALLOC_CMAC_CALLMORECORE(-asize);
                            br = NNALLOC_CONST_CMFAIL;
                        }
                    }
                }
            }
            if(br != NNALLOC_CONST_CMFAIL)
            { /* Use the space we did get */
                tbase = br;
                tsize = asize;
            }
            else
            {
                nn_allocator_disablecontiguous(m); /* Don't try contiguous path in the future */
            }
        }
        NNALLOC_CMAC_RELEASEMORECORELOCK();
    }
    if(NNALLOC_CONF_HAVEMMAP && tbase == NNALLOC_CONST_CMFAIL)
    { /* Try MMAP */
        req = nb + nn_allocator_gettopfootsize() + NNALLOC_CONST_SIZETONE;
        rsize = nn_allocator_granularityalign(req);
        if(rsize > nb)
        { /* Fail if wraps around zero */
            mp = (char*)(nn_allocator_callmmap(rsize));
            if(mp != NNALLOC_CONST_CMFAIL)
            {
                tbase = mp;
                tsize = rsize;
                mmap_flag = NNALLOC_CONST_ISMMAPPEDBIT;
            }
        }
    }
    if(NNALLOC_CONF_HAVEMORECORE && tbase == NNALLOC_CONST_CMFAIL)
    { /* Try noncontiguous NNALLOC_CONF_MORECOREFUNCNAME */
        asize = nn_allocator_granularityalign(nb + nn_allocator_gettopfootsize() + NNALLOC_CONST_SIZETONE);
        if(asize < NNALLOC_CONST_HALFMAXSIZET)
        {
            br = NNALLOC_CONST_CMFAIL;
            end = NNALLOC_CONST_CMFAIL;
            NNALLOC_CMAC_ACQUIREMORECORELOCK();
            br = (char*)(NNALLOC_CMAC_CALLMORECORE(asize));
            end = (char*)(NNALLOC_CMAC_CALLMORECORE(0));
            NNALLOC_CMAC_RELEASEMORECORELOCK();
            if(br != NNALLOC_CONST_CMFAIL && end != NNALLOC_CONST_CMFAIL && br < end)
            {
                ssize = end - br;
                if(ssize > nb + nn_allocator_gettopfootsize())
                {
                    tbase = br;
                    tsize = ssize;
                }
            }
        }
    }
    if(tbase != NNALLOC_CONST_CMFAIL)
    {
        if((m->footprint += tsize) > m->maxfootprint)
        {
            m->maxfootprint = m->footprint;
        }
        if(!nn_allocator_isinitialized(m))
        { /* first-time initialization */
            m->seg.base = m->leastaddr = tbase;
            m->seg.size = tsize;
            nn_allocator_setsegmentflags(&m->seg, mmap_flag);
            m->magictag = g_allocvar_mallocparams.magic;
            nn_allocator_mstate_initbins(m);
            if(nn_allocator_isglobal(m))
            {
                nn_allocator_mstate_inittop(m, (nnallocchunkitem_t*)tbase, tsize - nn_allocator_gettopfootsize());
            }
            else
            {
                /* Offset top by embedded nnallocstate_t */
                mn = nn_allocator_nextchunk(nn_allocator_mem2chunk(m));
                nn_allocator_mstate_inittop(m, mn, (size_t)((tbase + tsize) - (char*)mn) - nn_allocator_gettopfootsize());
            }
        }

        else
        {
            /* Try to merge with an existing segment */
            sp = &m->seg;
            while(sp != 0 && tbase != sp->base + sp->size)
            {
                sp = sp->next;
            }
            if(sp != 0 && !nn_allocator_isexternsegment(sp) && nn_allocator_checksegmentmerge(sp, tbase, tsize) && (nn_allocator_getsegmentflags(sp) & NNALLOC_CONST_ISMMAPPEDBIT) == mmap_flag && nn_allocator_segmentholds(sp, m->top))
            { /* append */
                sp->size += tsize;
                nn_allocator_mstate_inittop(m, m->top, m->topsize + tsize);
            }
            else
            {
                if(tbase < m->leastaddr)
                {
                    m->leastaddr = tbase;
                }
                sp = &m->seg;
                while(sp != 0 && sp->base != tbase + tsize)
                {
                    sp = sp->next;
                }
                if(sp != 0 && !nn_allocator_isexternsegment(sp) && nn_allocator_checksegmentmerge(sp, tbase, tsize) && (nn_allocator_getsegmentflags(sp) & NNALLOC_CONST_ISMMAPPEDBIT) == mmap_flag)
                {
                    oldbase = sp->base;
                    sp->base = tbase;
                    sp->size += tsize;
                    return nn_allocator_mstate_prependalloc(m, tbase, oldbase, nb);
                }
                else
                {
                    nn_allocator_mstate_addsegment(m, tbase, tsize, mmap_flag);
                }
            }
        }
        if(nb < m->topsize)
        { /* Allocate from new or extended top space */
            rsize = m->topsize -= nb;
            p = m->top;
            r = m->top = nn_allocator_chunkplusoffset(p, nb);
            r->head = rsize | NNALLOC_CONST_PINUSEBIT;
            nn_allocator_setsizeandpinuseofinusechunk(m, p, nb);
            nn_allocator_checktopchunk(m, m->top);
            nn_allocator_checkmallocedchunk(m, nn_allocator_chunk2mem(p), nb);
            return nn_allocator_chunk2mem(p);
        }
    }

    NNALLOC_CONF_MALLOCFAILUREACTION;
    return 0;
}

/* -----------------------  system deallocation -------------------------- */

/* Unmap and unlink any mmapped segments that don't contain used chunks */
static size_t nn_allocator_mstate_releaseunusedsegments(nnallocstate_t* m)
{
    size_t size;
    size_t psize;
    size_t released;
    char* base;
    nnallocchunkitem_t* p;
    nnallocchunktree_t* tp;
    nnallocmemsegment_t* sp;
    nnallocmemsegment_t* pred;
    nnallocmemsegment_t* next;
    released = 0;
    pred = &m->seg;
    sp = pred->next;
    while(sp != 0)
    {
        base = sp->base;
        size = sp->size;
        next = sp->next;
        if(nn_allocator_ismmappedsegment(sp) && !nn_allocator_isexternsegment(sp))
        {
            p = nn_allocator_alignaschunk(base);
            psize = nn_allocator_chunksize(p);
            /* Can unmap if first chunk holds entire segment and not pinned */
            if(!nn_allocator_cinuse(p) && (char*)p + psize >= base + size - nn_allocator_gettopfootsize())
            {
                tp = (nnallocchunktree_t*)p;
                NNALLOCASSERT(nn_allocator_segmentholds(sp, (char*)sp));
                if(p == m->designatvictimchunk)
                {
                    m->designatvictimchunk = 0;
                    m->designatvictimsize = 0;
                }
                else
                {
                    nn_allocator_unlinklargechunk(m, tp);
                }
                if(NNALLOC_CMAC_CALLMUNMAP(base, size) == 0)
                {
                    released += size;
                    m->footprint -= size;
                    /* unlink obsoleted record */
                    sp = pred;
                    sp->next = next;
                }
                else
                { /* back out if cannot unmap */
                    nn_allocator_insertlargechunk(m, tp, psize);
                }
            }
        }
        pred = sp;
        sp = next;
    }
    return released;
}

static int nn_allocator_mstate_systrim(nnallocstate_t* m, size_t pad)
{
    size_t released;
    size_t unit;
    size_t extra;
    size_t newsize;
    char* old_br;
    char* rel_br;
    char* new_br;
    nnallocmemsegment_t* sp;
    (void)newsize;
    released = 0;
    if(pad < NNALLOC_CONST_MAXREQUEST && nn_allocator_isinitialized(m))
    {
        pad += nn_allocator_gettopfootsize(); /* ensure enough room for segment overhead */
        if(m->topsize > pad)
        {
            /* Shrink top space in granularity-size units, keeping at least one */
            unit = g_allocvar_mallocparams.granularity;
            extra = ((m->topsize - pad + (unit - NNALLOC_CONST_SIZETONE)) / unit - NNALLOC_CONST_SIZETONE) * unit;
            sp = nn_allocator_segment_holding(m, (char*)m->top);
            if(!nn_allocator_isexternsegment(sp))
            {
                if(nn_allocator_ismmappedsegment(sp))
                {
                    if(NNALLOC_CONF_HAVEMMAP && sp->size >= extra && !nn_allocator_segment_haslink(m, sp))
                    { /* can't shrink if pinned */
                        newsize = sp->size - extra;
                        /* Prefer mremap, fall back to munmap */
                        if((NNALLOC_CMAC_CALLMREMAP(sp->base, sp->size, newsize, 0) != NNALLOC_CONST_MFAIL) || (NNALLOC_CMAC_CALLMUNMAP(sp->base + newsize, extra) == 0))
                        {
                            released = extra;
                        }
                    }
                }
                else if(NNALLOC_CONF_HAVEMORECORE)
                {
                    if(extra >= NNALLOC_CONST_HALFMAXSIZET) /* Avoid wrapping negative */
                    {
                        extra = (NNALLOC_CONST_HALFMAXSIZET) + NNALLOC_CONST_SIZETONE - unit;
                    }
                    NNALLOC_CMAC_ACQUIREMORECORELOCK();
                    {
                        /* Make sure end of memory is where we last set it. */
                        old_br = (char*)(NNALLOC_CMAC_CALLMORECORE(0));
                        if(old_br == sp->base + sp->size)
                        {
                            rel_br = (char*)(NNALLOC_CMAC_CALLMORECORE(-extra));
                            new_br = (char*)(NNALLOC_CMAC_CALLMORECORE(0));
                            if(rel_br != NNALLOC_CONST_CMFAIL && new_br < old_br)
                            {
                                released = old_br - new_br;
                            }
                        }
                    }
                    NNALLOC_CMAC_RELEASEMORECORELOCK();
                }
            }
            if(released != 0)
            {
                sp->size -= released;
                m->footprint -= released;
                nn_allocator_mstate_inittop(m, m->top, m->topsize - released);
                nn_allocator_checktopchunk(m, m->top);
            }
        }
        /* Unmap any unused mmapped segments */
        if(NNALLOC_CONF_HAVEMMAP)
        {
            released += nn_allocator_mstate_releaseunusedsegments(m);
        }
        /* On failure, disable autotrim to avoid repeated failed future calls */
        if(released == 0)
        {
            m->trimcheck = NNALLOC_CONF_MAXSIZET;
        }
    }
    return (released != 0) ? 1 : 0;
}

/* ---------------------------- malloc support --------------------------- */

/* allocate a large request from the best fitting chunk in a treebin */
static void* nn_allocator_mstate_tmalloclarge(nnallocstate_t* m, size_t nb)
{
    size_t trem;
    size_t rsize;
    size_t sizebits;
    nnallocbindex_t i;
    nnallocbindex_t idx;
    nnallocbinmap_t leastbit;
    nnallocbinmap_t leftbits;
    nnallocchunkitem_t* r;
    nnallocchunktree_t* t;
    nnallocchunktree_t* v;
    nnallocchunktree_t* rt;
    nnallocchunktree_t* rst;
    v = 0;
    rsize = -nb; /* Unsigned negation */
    idx = nn_allocator_computetreeindex(nb);
    if((t = *nn_allocator_treebinat(m, idx)) != 0)
    {
        /* Traverse tree for this bin looking for node with size == nb */
        sizebits = nb << nn_allocator_leftshiftfortreeindex(idx);
        rst = 0; /* The deepest untaken right subtree */
        for(;;)
        {
            trem = nn_allocator_chunksize(t) - nb;
            if(trem < rsize)
            {
                v = t;
                if((rsize = trem) == 0)
                {
                    break;
                }
            }
            rt = t->child[1];
            t = t->child[(sizebits >> (NNALLOC_CONST_SIZETBITSIZE - NNALLOC_CONST_SIZETONE)) & 1];
            if(rt != 0 && rt != t)
            {
                rst = rt;
            }
            if(t == 0)
            {
                t = rst; /* set t to least subtree holding sizes > nb */
                break;
            }
            sizebits <<= 1;
        }
    }
    if(t == 0 && v == 0)
    { /* set t to root of next non-empty treebin */
        leftbits = nn_allocator_leftbits(nn_allocator_idx2bit(idx)) & m->treemap;
        if(leftbits != 0)
        {
            leastbit = nn_allocator_leastbit(leftbits);
            i = nn_allocator_computebit2idx(leastbit);
            t = *nn_allocator_treebinat(m, i);
        }
    }
    while(t != 0)
    { /* find smallest of tree or subtree */
        trem = nn_allocator_chunksize(t) - nb;
        if(trem < rsize)
        {
            rsize = trem;
            v = t;
        }
        t = nn_allocator_leftmostchild(t);
    }
    /*  If dv is a better fit, return 0 so malloc will use it */
    if(v != 0 && rsize < (size_t)(m->designatvictimsize - nb))
    {
        if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress(m, v)))
        { /* split */
            r = nn_allocator_chunkplusoffset(v, nb);
            NNALLOCASSERT(nn_allocator_chunksize(v) == rsize + nb);
            if(NNALLOC_CMAC_RTCHECK(nn_allocator_oknext(v, r)))
            {
                nn_allocator_unlinklargechunk(m, v);
                if(rsize < NNALLOC_CONST_MINCHUNKSIZE)
                {
                    nn_allocator_setinuseandpinuse(m, v, (rsize + nb));
                }
                else
                {
                    nn_allocator_setsizeandpinuseofinusechunk(m, v, nb);
                    nn_allocator_setsizeandpinuseoffreechunk(r, rsize);
                    nn_allocator_insertchunk(m, r, rsize);
                }
                return nn_allocator_chunk2mem(v);
            }
        }
        NNALLOC_CMAC_CORRUPTIONERRORACTION(m);
    }
    return 0;
}

/* allocate a small request from the best fitting chunk in a treebin */
static void* nn_allocator_mstate_tmallocsmall(nnallocstate_t* m, size_t nb)
{
    size_t trem;
    size_t rsize;
    nnallocbindex_t i;
    nnallocbinmap_t leastbit;
    nnallocchunkitem_t* r;
    nnallocchunktree_t* t;
    nnallocchunktree_t* v;
    leastbit = nn_allocator_leastbit(m->treemap);
    i = nn_allocator_computebit2idx(leastbit);
    v = t = *nn_allocator_treebinat(m, i);
    rsize = nn_allocator_chunksize(t) - nb;
    while((t = nn_allocator_leftmostchild(t)) != 0)
    {
        trem = nn_allocator_chunksize(t) - nb;
        if(trem < rsize)
        {
            rsize = trem;
            v = t;
        }
    }
    if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress(m, v)))
    {
        r = nn_allocator_chunkplusoffset(v, nb);
        NNALLOCASSERT(nn_allocator_chunksize(v) == rsize + nb);
        if(NNALLOC_CMAC_RTCHECK(nn_allocator_oknext(v, r)))
        {
            nn_allocator_unlinklargechunk(m, v);
            if(rsize < NNALLOC_CONST_MINCHUNKSIZE)
            {
                nn_allocator_setinuseandpinuse(m, v, (rsize + nb));
            }
            else
            {
                nn_allocator_setsizeandpinuseofinusechunk(m, v, nb);
                nn_allocator_setsizeandpinuseoffreechunk(r, rsize);
                nn_allocator_replacedv(m, r, rsize);
            }
            return nn_allocator_chunk2mem(v);
        }
    }
    NNALLOC_CMAC_CORRUPTIONERRORACTION(m);
    return 0;
}

/* --------------------------- realloc support --------------------------- */

static void* nn_allocator_mstate_internalrealloc(nnallocstate_t* m, void* oldmem, size_t bytes)
{
    size_t nb;
    size_t rsize;
    size_t oldsize;
    size_t newsize;
    size_t newtopsize;
    size_t oc;
    void* extra;
    void* newmem;
    nnallocchunkitem_t* oldp;
    nnallocchunkitem_t* next;
    nnallocchunkitem_t* newp;
    nnallocchunkitem_t* newtop;
    nnallocchunkitem_t* remainder;
    if(bytes >= NNALLOC_CONST_MAXREQUEST)
    {
        NNALLOC_CONF_MALLOCFAILUREACTION;
        return 0;
    }
    if(!NNALLOC_CMAC_PREACTION(m))
    {
        oldp = nn_allocator_mem2chunk(oldmem);
        oldsize = nn_allocator_chunksize(oldp);
        next = nn_allocator_chunkplusoffset(oldp, oldsize);
        newp = 0;
        extra = 0;
        /* Try to either shrink or extend into top. Else malloc-copy-free */
        if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress(m, oldp) && nn_allocator_okcinuse(oldp) && nn_allocator_oknext(oldp, next) && nn_allocator_okpinuse(next)))
        {
            nb = nn_allocator_request2size(bytes);
            if(nn_allocator_ismmapped(oldp))
            {
                newp = nn_allocator_mstate_mmapresize(m, oldp, nb);
            }
            else if(oldsize >= nb)
            { /* already big enough */
                rsize = oldsize - nb;
                newp = oldp;
                if(rsize >= NNALLOC_CONST_MINCHUNKSIZE)
                {
                    remainder = nn_allocator_chunkplusoffset(newp, nb);
                    nn_allocator_setinuse(m, newp, nb);
                    nn_allocator_setinuse(m, remainder, rsize);
                    extra = nn_allocator_chunk2mem(remainder);
                }
            }
            else if(next == m->top && oldsize + m->topsize > nb)
            {
                /* Expand into top */
                newsize = oldsize + m->topsize;
                newtopsize = newsize - nb;
                newtop = nn_allocator_chunkplusoffset(oldp, nb);
                nn_allocator_setinuse(m, oldp, nb);
                newtop->head = newtopsize | NNALLOC_CONST_PINUSEBIT;
                m->top = newtop;
                m->topsize = newtopsize;
                newp = oldp;
            }
        }
        else
        {
            NNALLOC_CMAC_USAGEERRORACTION(m, oldmem);
            NNALLOC_CMAC_POSTACTION(m);
            return 0;
        }
        NNALLOC_CMAC_POSTACTION(m);
        if(newp != 0)
        {
            if(extra != 0)
            {
                nn_allocator_internalfree(m, extra);
            }
            nn_allocator_checkinusechunk(m, newp);
            return nn_allocator_chunk2mem(newp);
        }
        else
        {
            newmem = nn_allocator_internalmalloc(m, bytes);
            if(newmem != 0)
            {
                oc = oldsize - nn_allocator_overheadfor(oldp);
                memcpy(newmem, oldmem, ((oc < bytes) ? oc : bytes));
                nn_allocator_internalfree(m, oldmem);
            }
            return newmem;
        }
    }
    return 0;
}

/* --------------------------- memalign support -------------------------- */

static void* nn_allocator_mstate_internalmemalign(nnallocstate_t* m, size_t alignment, size_t bytes)
{
    size_t a;
    size_t nb;
    size_t req;
    size_t size;
    size_t newsize;
    size_t leadsize;
    size_t remainder_size;
    char* br;
    char* pos;
    char* mem;
    void* leader;
    void* trailer;
    nnallocchunkitem_t* p;
    nnallocchunkitem_t* newp;
    nnallocchunkitem_t* remainder;
    if(alignment <= NNALLOC_CONF_MALLOCALIGNMENT) /* Can just use malloc */
    {
        return nn_allocator_internalmalloc(m, bytes);
    }
    if(alignment < NNALLOC_CONST_MINCHUNKSIZE) /* must be at least a minimum chunk size */
    {
        alignment = NNALLOC_CONST_MINCHUNKSIZE;
    }
    if((alignment & (alignment - NNALLOC_CONST_SIZETONE)) != 0)
    { /* Ensure a power of 2 */
        a = NNALLOC_CONF_MALLOCALIGNMENT << 1;
        while(a < alignment)
        {
            a <<= 1;
        }
        alignment = a;
    }
    if(bytes >= NNALLOC_CONST_MAXREQUEST - alignment)
    {
        if(m != 0)
        { /* Test isn't needed but avoids compiler warning */
            NNALLOC_CONF_MALLOCFAILUREACTION;
        }
    }
    else
    {
        nb = nn_allocator_request2size(bytes);
        req = nb + alignment + NNALLOC_CONST_MINCHUNKSIZE - NNALLOC_CONST_CHUNKOVERHEAD;
        mem = (char*)nn_allocator_internalmalloc(m, req);
        if(mem != 0)
        {
            leader = 0;
            trailer = 0;
            p = nn_allocator_mem2chunk(mem);
            if(NNALLOC_CMAC_PREACTION(m))
            {
                return 0;
            }
            if((((size_t)(mem)) % alignment) != 0)
            { /* misaligned */
                /*
                  Find an aligned spot inside chunk.  Since we need to give
                  back leading space in a chunk of at least NNALLOC_CONST_MINCHUNKSIZE, if
                  the first calculation places us at a spot with less than
                  NNALLOC_CONST_MINCHUNKSIZE leader, we can move to the next aligned spot.
                  We've allocated enough total room so that this is always
                  possible.
                */
                br = (char*)nn_allocator_mem2chunk((size_t)(((size_t)(mem + alignment - NNALLOC_CONST_SIZETONE)) & -alignment));
                pos = ((size_t)(br - (char*)(p)) >= NNALLOC_CONST_MINCHUNKSIZE) ? br : br + alignment;
                newp = (nnallocchunkitem_t*)pos;
                leadsize = pos - (char*)(p);
                newsize = nn_allocator_chunksize(p) - leadsize;
                if(nn_allocator_ismmapped(p))
                { /* For mmapped chunks, just adjust offset */
                    newp->prevfoot = p->prevfoot + leadsize;
                    newp->head = (newsize | NNALLOC_CONST_CINUSEBIT);
                }
                else
                { /* Otherwise, give back leader, use the rest */
                    nn_allocator_setinuse(m, newp, newsize);
                    nn_allocator_setinuse(m, p, leadsize);
                    leader = nn_allocator_chunk2mem(p);
                }
                p = newp;
            }
            /* Give back spare room at the end */
            if(!nn_allocator_ismmapped(p))
            {
                size = nn_allocator_chunksize(p);
                if(size > nb + NNALLOC_CONST_MINCHUNKSIZE)
                {
                    remainder_size = size - nb;
                    remainder = nn_allocator_chunkplusoffset(p, nb);
                    nn_allocator_setinuse(m, p, nb);
                    nn_allocator_setinuse(m, remainder, remainder_size);
                    trailer = nn_allocator_chunk2mem(remainder);
                }
            }
            NNALLOCASSERT(nn_allocator_chunksize(p) >= nb);
            NNALLOCASSERT((((size_t)(nn_allocator_chunk2mem(p))) % alignment) == 0);
            nn_allocator_checkinusechunk(m, p);
            NNALLOC_CMAC_POSTACTION(m);
            if(leader != 0)
            {
                nn_allocator_internalfree(m, leader);
            }
            if(trailer != 0)
            {
                nn_allocator_internalfree(m, trailer);
            }
            return nn_allocator_chunk2mem(p);
        }
    }
    return 0;
}

/* ------------------------ comalloc/coalloc support --------------------- */

static void** nn_allocator_mstate_ialloc(nnallocstate_t* m, size_t n_elements, size_t* sizes, int opts, void* chunks[])
{
    size_t i;
    size_t size;
    size_t array_size; /* request size of pointer array */
    size_t element_size; /* chunksize of each element, if all same */
    size_t contents_size; /* total size of elements */
    size_t remainder_size; /* remaining bytes while splitting */
    size_t array_chunk_size;
    void* tmp;
    void* mem; /* malloced aggregate space */
    void** marray; /* either "chunks" or malloced ptr array */
    nnallocflag_t was_enabled; /* to disable mmap */
    nnallocchunkitem_t* p; /* corresponding chunk */
    nnallocchunkitem_t* array_chunk; /* chunk for malloced ptr array */
    /*
      This provides common support for independent_X routines, handling
      all of the combinations that can result.
      The opts arg has:
      bit 0 set if all elements are same size (using sizes[0])
      bit 1 set if elements should be zeroed
    */
    /* compute array length, if needed */
    if(chunks != 0)
    {
        if(n_elements == 0)
        {
            return chunks; /* nothing to do */
        }
        marray = chunks;
        array_size = 0;
    }
    else
    {
        /* if empty req, must still return chunk representing empty array */
        if(n_elements == 0)
        {
            tmp = nn_allocator_internalmalloc(m, 0);
            return (void**)tmp;
        }
        marray = 0;
        array_size = nn_allocator_request2size(n_elements * (sizeof(void*)));
    }
    /* compute total element size */
    if(opts & 0x1)
    { /* all-same-size */
        element_size = nn_allocator_request2size(*sizes);
        contents_size = n_elements * element_size;
    }
    else
    { /* add up all the sizes */
        element_size = 0;
        contents_size = 0;
        for(i = 0; i != n_elements; ++i)
        {
            contents_size += nn_allocator_request2size(sizes[i]);
        }
    }
    size = contents_size + array_size;
    /*
       Allocate the aggregate chunk.  First disable direct-mmapping so
       malloc won't use it, since we would not be able to later
       free/realloc space internal to a segregated mmap region.
    */
    was_enabled = nn_allocator_mmapuse(m);
    nn_allocator_mmapdisable(m);
    mem = nn_allocator_internalmalloc(m, size - NNALLOC_CONST_CHUNKOVERHEAD);
    if(was_enabled)
    {
        nn_allocator_mmapenable(m);
    }
    if(mem == 0)
    {
        return 0;
    }
    if(NNALLOC_CMAC_PREACTION(m))
    {
        return 0;
    }
    p = nn_allocator_mem2chunk(mem);
    remainder_size = nn_allocator_chunksize(p);
    NNALLOCASSERT(!nn_allocator_ismmapped(p));
    if(opts & 0x2)
    { /* optionally clear the elements */
        memset((size_t*)mem, 0, remainder_size - NNALLOC_CONST_SIZETSIZE - array_size);
    }
    /* If not provided, allocate the pointer array as final part of chunk */
    if(marray == 0)
    {
        array_chunk = nn_allocator_chunkplusoffset(p, contents_size);
        array_chunk_size = remainder_size - contents_size;
        marray = (void**)(nn_allocator_chunk2mem(array_chunk));
        nn_allocator_setsizeandpinuseofinusechunk(m, array_chunk, array_chunk_size);
        remainder_size = contents_size;
    }
    /* split out elements */
    for(i = 0;; ++i)
    {
        marray[i] = nn_allocator_chunk2mem(p);
        if(i != n_elements - 1)
        {
            if(element_size != 0)
            {
                size = element_size;
            }
            else
            {
                size = nn_allocator_request2size(sizes[i]);
            }
            remainder_size -= size;
            nn_allocator_setsizeandpinuseofinusechunk(m, p, size);
            p = nn_allocator_chunkplusoffset(p, size);
        }
        else
        { /* the final element absorbs any overallocation slop */
            nn_allocator_setsizeandpinuseofinusechunk(m, p, remainder_size);
            break;
        }
    }
#if defined(NNALLOC_CONF_DEBUG) && (NNALLOC_CONF_DEBUG == 1)
    if(marray != chunks)
    {
        /* final element must have exactly exhausted chunk */
        if(element_size != 0)
        {
            NNALLOCASSERT(remainder_size == element_size);
        }
        else
        {
            NNALLOCASSERT(remainder_size == nn_allocator_request2size(sizes[i]));
        }
        nn_allocator_checkinusechunk(m, nn_allocator_mem2chunk(marray));
    }
    for(i = 0; i != n_elements; ++i)
    {
        nn_allocator_checkinusechunk(m, nn_allocator_mem2chunk(marray[i]));
    }
#endif /* DEBUG */
    NNALLOC_CMAC_POSTACTION(m);
    return marray;
}

/* -------------------------- public routines ---------------------------- */

#if !NNALLOC_CONF_ONLYMSPACES

void* nn_allocuser_malloc(size_t bytes)
{
    size_t nb;
    size_t dvs;
    size_t rsize;
    void* mem;
    nnallocbindex_t i;
    nnallocbindex_t idx;
    nnallocbinmap_t leftbits;
    nnallocbinmap_t leastbit;
    nnallocbinmap_t smallbits;
    nnallocchunkitem_t* b;
    nnallocchunkitem_t* p;
    nnallocchunkitem_t* r;
    /*
       Basic algorithm:
       If a small request (< 256 bytes minus per-chunk overhead):
         1. If one exists, use a remainderless chunk in associated smallbin.
            (Remainderless means that there are too few excess bytes to
            represent as a chunk.)
         2. If it is big enough, use the dv chunk, which is normally the
            chunk adjacent to the one used for the most recent small request.
         3. If one exists, split the smallest available chunk in a bin,
            saving remainder in dv.
         4. If it is big enough, use the top chunk.
         5. If available, get memory from system and use it
       Otherwise, for a large request:
         1. Find the smallest available binned chunk that fits, and use it
            if it is better fitting than dv chunk, splitting if necessary.
         2. If better fitting than any binned chunk, use the dv chunk.
         3. If it is big enough, use the top chunk.
         4. If request size >= mmap threshold, try to directly mmap this chunk.
         5. If available, get memory from system and use it

       The ugly goto's here ensure that postaction occurs along all paths.
    */
    if(!NNALLOC_CMAC_PREACTION(&g_allocvar_mallocstate))
    {
        if(bytes <= NNALLOC_CONST_MAXSMALLREQUEST)
        {
            nb = 0;
            if(bytes < NNALLOC_CONST_MINREQUEST)
            {
                nb = NNALLOC_CONST_MINCHUNKSIZE;
            }
            else
            {
                nb = nn_allocator_padrequest(bytes);
            }
            idx = nn_allocator_smallindex(nb);
            smallbits = g_allocvar_mallocstate.smallmap >> idx;
            if((smallbits & 0x3U) != 0)
            {
                /* Remainderless fit to a smallbin. */
                idx += ~smallbits & 1; /* Uses next bin if idx empty */
                b = nn_allocator_smallbinat(&g_allocvar_mallocstate, idx);
                p = b->forwardptr;
                NNALLOCASSERT(nn_allocator_chunksize(p) == nn_allocator_smallindex2size(idx));
                nn_allocator_unlinkfirstsmallchunk(&g_allocvar_mallocstate, b, p, idx);
                nn_allocator_setinuseandpinuse(&g_allocvar_mallocstate, p, nn_allocator_smallindex2size(idx));
                mem = nn_allocator_chunk2mem(p);
                nn_allocator_checkmallocedchunk(&g_allocvar_mallocstate, mem, nb);
                goto postaction;
            }
            else if(nb > g_allocvar_mallocstate.designatvictimsize)
            {
                if(smallbits != 0)
                { /* Use chunk in next nonempty smallbin */
                    leftbits = (smallbits << idx) & nn_allocator_leftbits(nn_allocator_idx2bit(idx));
                    leastbit = nn_allocator_leastbit(leftbits);
                    i = nn_allocator_computebit2idx(leastbit);
                    b = nn_allocator_smallbinat(&g_allocvar_mallocstate, i);
                    p = b->forwardptr;
                    NNALLOCASSERT(nn_allocator_chunksize(p) == nn_allocator_smallindex2size(i));
                    nn_allocator_unlinkfirstsmallchunk(&g_allocvar_mallocstate, b, p, i);
                    rsize = nn_allocator_smallindex2size(i) - nb;
                    /* Fit here cannot be remainderless if 4byte sizes */
                    if(NNALLOC_CONST_SIZETSIZE != 4 && rsize < NNALLOC_CONST_MINCHUNKSIZE)
                    {
                        nn_allocator_setinuseandpinuse(&g_allocvar_mallocstate, p, nn_allocator_smallindex2size(i));
                    }
                    else
                    {
                        nn_allocator_setsizeandpinuseofinusechunk(&g_allocvar_mallocstate, p, nb);
                        r = nn_allocator_chunkplusoffset(p, nb);
                        nn_allocator_setsizeandpinuseoffreechunk(r, rsize);
                        nn_allocator_replacedv(&g_allocvar_mallocstate, r, rsize);
                    }
                    mem = nn_allocator_chunk2mem(p);
                    nn_allocator_checkmallocedchunk(&g_allocvar_mallocstate, mem, nb);
                    goto postaction;
                }
                else if(g_allocvar_mallocstate.treemap != 0 && (mem = nn_allocator_mstate_tmallocsmall(&g_allocvar_mallocstate, nb)) != 0)
                {
                    nn_allocator_checkmallocedchunk(&g_allocvar_mallocstate, mem, nb);
                    goto postaction;
                }
            }
        }
        else if(bytes >= NNALLOC_CONST_MAXREQUEST)
        {
            nb = NNALLOC_CONF_MAXSIZET; /* Too big to allocate. Force failure (in sys alloc) */
        }
        else
        {
            nb = nn_allocator_padrequest(bytes);
            if(g_allocvar_mallocstate.treemap != 0 && (mem = nn_allocator_mstate_tmalloclarge(&g_allocvar_mallocstate, nb)) != 0)
            {
                nn_allocator_checkmallocedchunk(&g_allocvar_mallocstate, mem, nb);
                goto postaction;
            }
        }
        if(nb <= g_allocvar_mallocstate.designatvictimsize)
        {
            rsize = g_allocvar_mallocstate.designatvictimsize - nb;
            p = g_allocvar_mallocstate.designatvictimchunk;
            if(rsize >= NNALLOC_CONST_MINCHUNKSIZE)
            {
                /* split dv */
                r = g_allocvar_mallocstate.designatvictimchunk = nn_allocator_chunkplusoffset(p, nb);
                g_allocvar_mallocstate.designatvictimsize = rsize;
                nn_allocator_setsizeandpinuseoffreechunk(r, rsize);
                nn_allocator_setsizeandpinuseofinusechunk(&g_allocvar_mallocstate, p, nb);
            }
            else
            {
                /* exhaust dv */
                dvs = g_allocvar_mallocstate.designatvictimsize;
                g_allocvar_mallocstate.designatvictimsize = 0;
                g_allocvar_mallocstate.designatvictimchunk = 0;
                nn_allocator_setinuseandpinuse(&g_allocvar_mallocstate, p, dvs);
            }
            mem = nn_allocator_chunk2mem(p);
            nn_allocator_checkmallocedchunk(&g_allocvar_mallocstate, mem, nb);
            goto postaction;
        }
        else if(nb < g_allocvar_mallocstate.topsize)
        { /* Split top */
            rsize = g_allocvar_mallocstate.topsize -= nb;
            p = g_allocvar_mallocstate.top;
            r = g_allocvar_mallocstate.top = nn_allocator_chunkplusoffset(p, nb);
            r->head = rsize | NNALLOC_CONST_PINUSEBIT;
            nn_allocator_setsizeandpinuseofinusechunk(&g_allocvar_mallocstate, p, nb);
            mem = nn_allocator_chunk2mem(p);
            nn_allocator_checktopchunk(&g_allocvar_mallocstate, g_allocvar_mallocstate.top);
            nn_allocator_checkmallocedchunk(&g_allocvar_mallocstate, mem, nb);
            goto postaction;
        }
        mem = nn_allocator_mstate_sysalloc(&g_allocvar_mallocstate, nb);
    postaction:
        NNALLOC_CMAC_POSTACTION(&g_allocvar_mallocstate);
        return mem;
    }
    return 0;
}

void nn_allocuser_free(void* mem)
{
    size_t tsize;
    size_t dsize;
    size_t nsize;
    size_t psize;
    size_t prevsize;
    nnallocchunkitem_t* p;
    nnallocchunkitem_t* next;
    nnallocchunkitem_t* prev;
    /*
       Consolidate freed chunks with preceding or succeeding bordering
       free chunks, if they exist, and then place in a bin.  Intermixed
       with special cases for top, dv, mmapped chunks, and usage errors.
    */
    if(mem != 0)
    {
        p = nn_allocator_mem2chunk(mem);
        if(!NNALLOC_CMAC_PREACTION(&g_allocvar_mallocstate))
        {
            nn_allocator_checkinusechunk(&g_allocvar_mallocstate, p);
            if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress(&g_allocvar_mallocstate, p) && nn_allocator_okcinuse(p)))
            {
                psize = nn_allocator_chunksize(p);
                next = nn_allocator_chunkplusoffset(p, psize);
                if(!nn_allocator_pinuse(p))
                {
                    prevsize = p->prevfoot;
                    if((prevsize & NNALLOC_CONST_ISMMAPPEDBIT) != 0)
                    {
                        prevsize &= ~NNALLOC_CONST_ISMMAPPEDBIT;
                        psize += prevsize + NNALLOC_CONST_MMAPFOOTPAD;
                        if(NNALLOC_CMAC_CALLMUNMAP((char*)p - prevsize, psize) == 0)
                        {
                            g_allocvar_mallocstate.footprint -= psize;
                        }
                        goto postaction;
                    }
                    else
                    {
                        prev = nn_allocator_chunkminusoffset(p, prevsize);
                        psize += prevsize;
                        p = prev;
                        if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress(&g_allocvar_mallocstate, prev)))
                        { /* consolidate backward */
                            if(p != g_allocvar_mallocstate.designatvictimchunk)
                            {
                                nn_allocator_unlinkchunk(&g_allocvar_mallocstate, p, prevsize);
                            }
                            else if((next->head & NNALLOC_CONST_INUSEBITS) == NNALLOC_CONST_INUSEBITS)
                            {
                                g_allocvar_mallocstate.designatvictimsize = psize;
                                nn_allocator_setfreewithpinuse(p, psize, next);
                                goto postaction;
                            }
                        }
                        else
                        {
                            goto erroraction;
                        }
                    }
                }
                if(NNALLOC_CMAC_RTCHECK(nn_allocator_oknext(p, next) && nn_allocator_okpinuse(next)))
                {
                    if(!nn_allocator_cinuse(next))
                    { /* consolidate forward */
                        if(next == g_allocvar_mallocstate.top)
                        {
                            tsize = g_allocvar_mallocstate.topsize += psize;
                            g_allocvar_mallocstate.top = p;
                            p->head = tsize | NNALLOC_CONST_PINUSEBIT;
                            if(p == g_allocvar_mallocstate.designatvictimchunk)
                            {
                                g_allocvar_mallocstate.designatvictimchunk = 0;
                                g_allocvar_mallocstate.designatvictimsize = 0;
                            }
                            if(nn_allocator_shouldtrim(&g_allocvar_mallocstate, tsize))
                            {
                                nn_allocator_mstate_systrim(&g_allocvar_mallocstate, 0);
                            }
                            goto postaction;
                        }
                        else if(next == g_allocvar_mallocstate.designatvictimchunk)
                        {
                            dsize = g_allocvar_mallocstate.designatvictimsize += psize;
                            g_allocvar_mallocstate.designatvictimchunk = p;
                            nn_allocator_setsizeandpinuseoffreechunk(p, dsize);
                            goto postaction;
                        }
                        else
                        {
                            nsize = nn_allocator_chunksize(next);
                            psize += nsize;
                            nn_allocator_unlinkchunk(&g_allocvar_mallocstate, next, nsize);
                            nn_allocator_setsizeandpinuseoffreechunk(p, psize);
                            if(p == g_allocvar_mallocstate.designatvictimchunk)
                            {
                                g_allocvar_mallocstate.designatvictimsize = psize;
                                goto postaction;
                            }
                        }
                    }
                    else
                    {
                        nn_allocator_setfreewithpinuse(p, psize, next);
                    }
                    nn_allocator_insertchunk(&g_allocvar_mallocstate, p, psize);
                    nn_allocator_checkfreechunk(&g_allocvar_mallocstate, p);
                    goto postaction;
                }
            }
        erroraction:
            NNALLOC_CMAC_USAGEERRORACTION(&g_allocvar_mallocstate, p);
        postaction:
            NNALLOC_CMAC_POSTACTION(&g_allocvar_mallocstate);
        }
    }
}

void* nn_allocuser_calloc(size_t n_elements, size_t elem_size)
{
    void* mem;
    size_t req = 0;
    if(n_elements != 0)
    {
        req = n_elements * elem_size;
        if(((n_elements | elem_size) & ~(size_t)0xffff) && (req / n_elements != elem_size))
        {
            req = NNALLOC_CONF_MAXSIZET; /* force downstream failure on overflow */
        }
    }
    mem = nn_allocuser_malloc(req);
    if(mem != 0 && nn_allocator_callocmustclear(nn_allocator_mem2chunk(mem)))
    {
        memset(mem, 0, req);
    }
    return mem;
}

void* nn_allocuser_realloc(void* oldmem, size_t bytes)
{
    if(oldmem == 0)
    {
        return nn_allocuser_malloc(bytes);
    }
    #ifdef REALLOC_ZERO_BYTES_FREES
    if(bytes == 0)
    {
        nn_allocuser_free(oldmem);
        return 0;
    }
    #endif /* REALLOC_ZERO_BYTES_FREES */
    else
    {
        nnallocstate_t* m = &g_allocvar_mallocstate;
        return nn_allocator_mstate_internalrealloc(m, oldmem, bytes);
    }
}

void* nn_allocuser_memalign(size_t alignment, size_t bytes)
{
    return nn_allocator_mstate_internalmemalign(&g_allocvar_mallocstate, alignment, bytes);
}

void** nn_allocuser_independentcalloc(size_t n_elements, size_t elem_size, void* chunks[])
{
    size_t sz;
    sz = elem_size; /* serves as 1-element array */
    return nn_allocator_mstate_ialloc(&g_allocvar_mallocstate, n_elements, &sz, 3, chunks);
}

void** nn_allocuser_independentcomalloc(size_t n_elements, size_t sizes[], void* chunks[])
{
    return nn_allocator_mstate_ialloc(&g_allocvar_mallocstate, n_elements, sizes, 0, chunks);
}

void* nn_allocuser_valloc(size_t bytes)
{
    size_t pagesz;
    nn_allocator_mparams_init();
    pagesz = g_allocvar_mallocparams.pagesize;
    return nn_allocuser_memalign(pagesz, bytes);
}

void* nn_allocuser_pvalloc(size_t bytes)
{
    size_t pagesz;
    nn_allocator_mparams_init();
    pagesz = g_allocvar_mallocparams.pagesize;
    return nn_allocuser_memalign(pagesz, (bytes + pagesz - NNALLOC_CONST_SIZETONE) & ~(pagesz - NNALLOC_CONST_SIZETONE));
}

int nn_allocuser_malloctrim(size_t pad)
{
    int result;
    result = 0;
    if(!NNALLOC_CMAC_PREACTION(&g_allocvar_mallocstate))
    {
        result = nn_allocator_mstate_systrim(&g_allocvar_mallocstate, pad);
        NNALLOC_CMAC_POSTACTION(&g_allocvar_mallocstate);
    }
    return result;
}

size_t nn_allocuser_getfootprint()
{
    return g_allocvar_mallocstate.footprint;
}

size_t nn_allocuser_getmaxfootprint()
{
    return g_allocvar_mallocstate.maxfootprint;
}

nnallocmallocinfo_t nn_allocuser_mallinfo()
{
    return nn_allocator_mstate_internalmallinfo(&g_allocvar_mallocstate);
}

void nn_allocuser_mallocstats()
{
    nn_allocator_mstate_internalmallocstats(&g_allocvar_mallocstate);
}

size_t nn_allocuser_usablesize(void* mem)
{
    nnallocchunkitem_t* p;
    if(mem != 0)
    {
        p = nn_allocator_mem2chunk(mem);
        if(nn_allocator_cinuse(p))
        {
            return nn_allocator_chunksize(p) - nn_allocator_overheadfor(p);
        }
    }
    return 0;
}

int nn_allocuser_mallopt(int param_number, int value)
{
    return nn_allocator_changemparam(param_number, value);
}

#endif /* !NNALLOC_CONF_ONLYMSPACES */


static nnallocstate_t* nn_allocator_mstate_initusermstate(char* tbase, size_t tsize)
{
    size_t msize;
    nnallocchunkitem_t* mn;
    nnallocchunkitem_t* msp;
    nnallocstate_t* m;
    msize = nn_allocator_padrequest(sizeof(nnallocstate_t));
    msp = nn_allocator_alignaschunk(tbase);
    m = (nnallocstate_t*)(nn_allocator_chunk2mem(msp));
    memset(m, 0, msize);
    NNALLOC_CMAC_INITIALLOCK(&m->mutex);
    msp->head = (msize | NNALLOC_CONST_PINUSEBIT | NNALLOC_CONST_CINUSEBIT);
    m->seg.base = m->leastaddr = tbase;
    m->seg.size = m->footprint = m->maxfootprint = tsize;
    m->magictag = g_allocvar_mallocparams.magic;
    m->mflags = g_allocvar_mallocparams.defaultmflags;
    nn_allocator_disablecontiguous(m);
    nn_allocator_mstate_initbins(m);
    mn = nn_allocator_nextchunk(nn_allocator_mem2chunk(m));
    nn_allocator_mstate_inittop(m, mn, (size_t)((tbase + tsize) - (char*)mn) - nn_allocator_gettopfootsize());
    nn_allocator_checktopchunk(m, m->top);
    return m;
}

nnallocmspace_t* nn_allocator_mspacecreate(size_t capacity, int locked)
{
    size_t rs;
    size_t msize;
    size_t tsize;
    char* tbase;
    nnallocstate_t* m;
    (void)locked;
    m = 0;
    msize = nn_allocator_padrequest(sizeof(nnallocstate_t));
    nn_allocator_mparams_init(); /* Ensure pagesize etc initialized */
    if(capacity < (size_t) - (msize + nn_allocator_gettopfootsize() + g_allocvar_mallocparams.pagesize))
    {
        rs = 0;
        if(capacity == 0)
        {
            rs = g_allocvar_mallocparams.granularity;
        }
        else
        {
            rs = (capacity + nn_allocator_gettopfootsize() + msize);
        }
        tsize = nn_allocator_granularityalign(rs);
        tbase = (char*)(nn_allocator_callmmap(tsize));
        if(tbase != NNALLOC_CONST_CMFAIL)
        {
            m = nn_allocator_mstate_initusermstate(tbase, tsize);
            nn_allocator_setsegmentflags(&m->seg, NNALLOC_CONST_ISMMAPPEDBIT);
            nn_allocator_lockset(m, locked);
        }
    }
    return (nnallocmspace_t*)m;
}

nnallocmspace_t* nn_allocator_mspacecreatewithbase(void* base, size_t capacity, int locked)
{
    size_t msize;
    nnallocstate_t* m;
    (void)locked;
    m = 0;
    msize = nn_allocator_padrequest(sizeof(nnallocstate_t));
    nn_allocator_mparams_init(); /* Ensure pagesize etc initialized */
    if(capacity > msize + nn_allocator_gettopfootsize() && capacity < (size_t) - (msize + nn_allocator_gettopfootsize() + g_allocvar_mallocparams.pagesize))
    {
        m = nn_allocator_mstate_initusermstate((char*)base, capacity);
        nn_allocator_setsegmentflags(&m->seg, NNALLOC_CONST_EXTERNBIT);
        nn_allocator_lockset(m, locked);
    }
    return (nnallocmspace_t*)m;
}

size_t nn_allocator_mspacedestroy(nnallocmspace_t* msp)
{
    size_t size;
    size_t freed;
    char* base;
    nnallocflag_t flag;
    nnallocstate_t* ms;
    nnallocmemsegment_t* sp;
    (void)base;
    freed = 0;
    ms = (nnallocstate_t*)msp;
    if(nn_allocator_okmagic(ms))
    {
        sp = &ms->seg;
        while(sp != 0)
        {
            base = sp->base;
            size = sp->size;
            flag = nn_allocator_getsegmentflags(sp);
            sp = sp->next;
            if((flag & NNALLOC_CONST_ISMMAPPEDBIT) && !(flag & NNALLOC_CONST_EXTERNBIT) && NNALLOC_CMAC_CALLMUNMAP(base, size) == 0)
            {
                freed += size;
            }
        }
    }
    else
    {
        NNALLOC_CMAC_USAGEERRORACTION(ms, ms);
    }
    return freed;
}

/*
  nnallocmspace_t* versions of routines are near-clones of the global
  versions. This is not so nice but better than the alternatives.
*/

void* nn_allocator_mspacemalloc(nnallocmspace_t* msp, size_t bytes)
{
    size_t nb;
    size_t dvs;
    size_t rsize;
    void* mem;
    nnallocbindex_t i;
    nnallocbindex_t idx;
    nnallocbinmap_t leftbits;
    nnallocbinmap_t leastbit;
    nnallocbinmap_t smallbits;
    nnallocchunkitem_t *b;
    nnallocchunkitem_t* p;
    nnallocchunkitem_t* r;
    nnallocstate_t* ms;
    ms = (nnallocstate_t*)msp;
    if(!nn_allocator_okmagic(ms))
    {
        NNALLOC_CMAC_USAGEERRORACTION(ms, ms);
        return 0;
    }
    if(!NNALLOC_CMAC_PREACTION(ms))
    {
        if(bytes <= NNALLOC_CONST_MAXSMALLREQUEST)
        {
            nb = 0;
            if(bytes < NNALLOC_CONST_MINREQUEST)
            {
                nb = NNALLOC_CONST_MINCHUNKSIZE;
            }
            else
            {
                nb = nn_allocator_padrequest(bytes);
            }
            idx = nn_allocator_smallindex(nb);
            smallbits = ms->smallmap >> idx;
            if((smallbits & 0x3U) != 0)
            {
                /* Remainderless fit to a smallbin. */
                idx += ~smallbits & 1; /* Uses next bin if idx empty */
                b = nn_allocator_smallbinat(ms, idx);
                p = b->forwardptr;
                NNALLOCASSERT(nn_allocator_chunksize(p) == nn_allocator_smallindex2size(idx));
                nn_allocator_unlinkfirstsmallchunk(ms, b, p, idx);
                nn_allocator_setinuseandpinuse(ms, p, nn_allocator_smallindex2size(idx));
                mem = nn_allocator_chunk2mem(p);
                nn_allocator_checkmallocedchunk(ms, mem, nb);
                goto postaction;
            }
            else if(nb > ms->designatvictimsize)
            {
                if(smallbits != 0)
                {
                    /* Use chunk in next nonempty smallbin */
                    leftbits = (smallbits << idx) & nn_allocator_leftbits(nn_allocator_idx2bit(idx));
                    leastbit = nn_allocator_leastbit(leftbits);
                    i = nn_allocator_computebit2idx(leastbit);
                    b = nn_allocator_smallbinat(ms, i);
                    p = b->forwardptr;
                    NNALLOCASSERT(nn_allocator_chunksize(p) == nn_allocator_smallindex2size(i));
                    nn_allocator_unlinkfirstsmallchunk(ms, b, p, i);
                    rsize = nn_allocator_smallindex2size(i) - nb;
                    /* Fit here cannot be remainderless if 4byte sizes */
                    if(NNALLOC_CONST_SIZETSIZE != 4 && rsize < NNALLOC_CONST_MINCHUNKSIZE)
                    {
                        nn_allocator_setinuseandpinuse(ms, p, nn_allocator_smallindex2size(i));
                    }
                    else
                    {
                        nn_allocator_setsizeandpinuseofinusechunk(ms, p, nb);
                        r = nn_allocator_chunkplusoffset(p, nb);
                        nn_allocator_setsizeandpinuseoffreechunk(r, rsize);
                        nn_allocator_replacedv(ms, r, rsize);
                    }
                    mem = nn_allocator_chunk2mem(p);
                    nn_allocator_checkmallocedchunk(ms, mem, nb);
                    goto postaction;
                }
                else if(ms->treemap != 0 && (mem = nn_allocator_mstate_tmallocsmall(ms, nb)) != 0)
                {
                    nn_allocator_checkmallocedchunk(ms, mem, nb);
                    goto postaction;
                }
            }
        }
        else if(bytes >= NNALLOC_CONST_MAXREQUEST)
        {
            nb = NNALLOC_CONF_MAXSIZET; /* Too big to allocate. Force failure (in sys alloc) */
        }
        else
        {
            nb = nn_allocator_padrequest(bytes);
            if(ms->treemap != 0 && (mem = nn_allocator_mstate_tmalloclarge(ms, nb)) != 0)
            {
                nn_allocator_checkmallocedchunk(ms, mem, nb);
                goto postaction;
            }
        }
        if(nb <= ms->designatvictimsize)
        {
            rsize = ms->designatvictimsize - nb;
            p = ms->designatvictimchunk;
            if(rsize >= NNALLOC_CONST_MINCHUNKSIZE)
            {
                /* split dv */
                r = ms->designatvictimchunk = nn_allocator_chunkplusoffset(p, nb);
                ms->designatvictimsize = rsize;
                nn_allocator_setsizeandpinuseoffreechunk(r, rsize);
                nn_allocator_setsizeandpinuseofinusechunk(ms, p, nb);
            }
            else
            {
                /* exhaust dv */
                dvs = ms->designatvictimsize;
                ms->designatvictimsize = 0;
                ms->designatvictimchunk = 0;
                nn_allocator_setinuseandpinuse(ms, p, dvs);
            }
            mem = nn_allocator_chunk2mem(p);
            nn_allocator_checkmallocedchunk(ms, mem, nb);
            goto postaction;
        }
        else if(nb < ms->topsize)
        {
            /* Split top */
            rsize = ms->topsize -= nb;
            p = ms->top;
            r = ms->top = nn_allocator_chunkplusoffset(p, nb);
            r->head = rsize | NNALLOC_CONST_PINUSEBIT;
            nn_allocator_setsizeandpinuseofinusechunk(ms, p, nb);
            mem = nn_allocator_chunk2mem(p);
            nn_allocator_checktopchunk(ms, ms->top);
            nn_allocator_checkmallocedchunk(ms, mem, nb);
            goto postaction;
        }

        mem = nn_allocator_mstate_sysalloc(ms, nb);

    postaction:
        NNALLOC_CMAC_POSTACTION(ms);
        return mem;
    }

    return 0;
}

void nn_allocator_mspacefree(nnallocmspace_t* msp, void* mem)
{
    size_t psize;
    size_t tsize;
    size_t dsize;
    size_t nsize;
    size_t prevsize;
    nnallocchunkitem_t* p;
    nnallocstate_t* fm;
    nnallocchunkitem_t* next;
    nnallocchunkitem_t* prev;
    if(mem != 0)
    {
        p = nn_allocator_mem2chunk(mem);
        fm = (nnallocstate_t*)msp;
        if(!nn_allocator_okmagic(fm))
        {
            NNALLOC_CMAC_USAGEERRORACTION(fm, p);
            return;
        }
        if(!NNALLOC_CMAC_PREACTION(fm))
        {
            nn_allocator_checkinusechunk(fm, p);
            if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress(fm, p) && nn_allocator_okcinuse(p)))
            {
                psize = nn_allocator_chunksize(p);
                next = nn_allocator_chunkplusoffset(p, psize);
                if(!nn_allocator_pinuse(p))
                {
                    prevsize = p->prevfoot;
                    if((prevsize & NNALLOC_CONST_ISMMAPPEDBIT) != 0)
                    {
                        prevsize &= ~NNALLOC_CONST_ISMMAPPEDBIT;
                        psize += prevsize + NNALLOC_CONST_MMAPFOOTPAD;
                        if(NNALLOC_CMAC_CALLMUNMAP((char*)p - prevsize, psize) == 0)
                        {
                            fm->footprint -= psize;
                        }
                        goto postaction;
                    }
                    else
                    {
                        prev = nn_allocator_chunkminusoffset(p, prevsize);
                        psize += prevsize;
                        p = prev;
                        if(NNALLOC_CMAC_RTCHECK(nn_allocator_okaddress(fm, prev)))
                        {
                            /* consolidate backward */
                            if(p != fm->designatvictimchunk)
                            {
                                nn_allocator_unlinkchunk(fm, p, prevsize);
                            }
                            else if((next->head & NNALLOC_CONST_INUSEBITS) == NNALLOC_CONST_INUSEBITS)
                            {
                                fm->designatvictimsize = psize;
                                nn_allocator_setfreewithpinuse(p, psize, next);
                                goto postaction;
                            }
                        }
                        else
                        {
                            goto erroraction;
                        }
                    }
                }
                if(NNALLOC_CMAC_RTCHECK(nn_allocator_oknext(p, next) && nn_allocator_okpinuse(next)))
                {
                    if(!nn_allocator_cinuse(next))
                    {
                        /* consolidate forward */
                        if(next == fm->top)
                        {
                            tsize = fm->topsize += psize;
                            fm->top = p;
                            p->head = tsize | NNALLOC_CONST_PINUSEBIT;
                            if(p == fm->designatvictimchunk)
                            {
                                fm->designatvictimchunk = 0;
                                fm->designatvictimsize = 0;
                            }
                            if(nn_allocator_shouldtrim(fm, tsize))
                            {
                                nn_allocator_mstate_systrim(fm, 0);
                            }
                            goto postaction;
                        }
                        else if(next == fm->designatvictimchunk)
                        {
                            dsize = fm->designatvictimsize += psize;
                            fm->designatvictimchunk = p;
                            nn_allocator_setsizeandpinuseoffreechunk(p, dsize);
                            goto postaction;
                        }
                        else
                        {
                            nsize = nn_allocator_chunksize(next);
                            psize += nsize;
                            nn_allocator_unlinkchunk(fm, next, nsize);
                            nn_allocator_setsizeandpinuseoffreechunk(p, psize);
                            if(p == fm->designatvictimchunk)
                            {
                                fm->designatvictimsize = psize;
                                goto postaction;
                            }
                        }
                    }
                    else
                    {
                        nn_allocator_setfreewithpinuse(p, psize, next);
                    }
                    nn_allocator_insertchunk(fm, p, psize);
                    nn_allocator_checkfreechunk(fm, p);
                    goto postaction;
                }
            }
        erroraction:
            NNALLOC_CMAC_USAGEERRORACTION(fm, p);
        postaction:
            NNALLOC_CMAC_POSTACTION(fm);
        }
    }
}

void* nn_allocator_mspacecalloc(nnallocmspace_t* msp, size_t n_elements, size_t elem_size)
{
    void* mem;
    size_t req;
    nnallocstate_t* ms;
    req = 0;
    ms = (nnallocstate_t*)msp;
    if(!nn_allocator_okmagic(ms))
    {
        NNALLOC_CMAC_USAGEERRORACTION(ms, ms);
        return 0;
    }
    if(n_elements != 0)
    {
        req = n_elements * elem_size;
        if(((n_elements | elem_size) & ~(size_t)0xffff) && (req / n_elements != elem_size))
        {
            /* force downstream failure on overflow */
            req = NNALLOC_CONF_MAXSIZET;
        }
    }
    mem = nn_allocator_internalmalloc(ms, req);
    if(mem != 0 && nn_allocator_callocmustclear(nn_allocator_mem2chunk(mem)))
    {
        memset(mem, 0, req);
    }
    return mem;
}

void* nn_allocator_mspacerealloc(nnallocmspace_t* msp, void* oldmem, size_t bytes)
{
    nnallocstate_t* ms;
    if(oldmem == 0)
    {
        return nn_allocator_mspacemalloc(msp, bytes);
    }
    #ifdef REALLOC_ZERO_BYTES_FREES
    if(bytes == 0)
    {
        nn_allocator_mspacefree(msp, oldmem);
        return 0;
    }
    #endif
    else
    {
        ms = (nnallocstate_t*)msp;
        if(!nn_allocator_okmagic(ms))
        {
            NNALLOC_CMAC_USAGEERRORACTION(ms, ms);
            return 0;
        }
        return nn_allocator_mstate_internalrealloc(ms, oldmem, bytes);
    }
}

void* nn_allocator_mspacememalign(nnallocmspace_t* msp, size_t alignment, size_t bytes)
{
    nnallocstate_t* ms;
    ms = (nnallocstate_t*)msp;
    if(!nn_allocator_okmagic(ms))
    {
        NNALLOC_CMAC_USAGEERRORACTION(ms, ms);
        return 0;
    }
    return nn_allocator_mstate_internalmemalign(ms, alignment, bytes);
}

void** nn_allocator_mspaceindependentcalloc(nnallocmspace_t* msp, size_t n_elements, size_t elem_size, void* chunks[])
{
    size_t sz;
    nnallocstate_t* ms;
    /* serves as 1-element array */
    sz = elem_size;
    ms = (nnallocstate_t*)msp;
    if(!nn_allocator_okmagic(ms))
    {
        NNALLOC_CMAC_USAGEERRORACTION(ms, ms);
        return 0;
    }
    return nn_allocator_mstate_ialloc(ms, n_elements, &sz, 3, chunks);
}

void** nn_allocator_mspaceindependentcomalloc(nnallocmspace_t* msp, size_t n_elements, size_t sizes[], void* chunks[])
{
    nnallocstate_t* ms;
    ms = (nnallocstate_t*)msp;
    if(!nn_allocator_okmagic(ms))
    {
        NNALLOC_CMAC_USAGEERRORACTION(ms, ms);
        return 0;
    }
    return nn_allocator_mstate_ialloc(ms, n_elements, sizes, 0, chunks);
}

int nn_allocator_mspacetrim(nnallocmspace_t* msp, size_t pad)
{
    int result;
    nnallocstate_t* ms;
    result = 0;
    ms = (nnallocstate_t*)msp;
    if(nn_allocator_okmagic(ms))
    {
        if(!NNALLOC_CMAC_PREACTION(ms))
        {
            result = nn_allocator_mstate_systrim(ms, pad);
            NNALLOC_CMAC_POSTACTION(ms);
        }
    }
    else
    {
        NNALLOC_CMAC_USAGEERRORACTION(ms, ms);
    }
    return result;
}

void nn_allocator_mspacemallocstats(nnallocmspace_t* msp)
{
    nnallocstate_t* ms;
    ms = (nnallocstate_t*)msp;
    if(nn_allocator_okmagic(ms))
    {
        nn_allocator_mstate_internalmallocstats(ms);
    }
    else
    {
        NNALLOC_CMAC_USAGEERRORACTION(ms, ms);
    }
}

size_t nn_allocator_mspacefootprint(nnallocmspace_t* msp)
{
    size_t result;
    nnallocstate_t* ms;
    ms = (nnallocstate_t*)msp;
    if(nn_allocator_okmagic(ms))
    {
        result = ms->footprint;
    }
    NNALLOC_CMAC_USAGEERRORACTION(ms, ms);
    return result;
}

size_t nn_allocator_mspacemaxfootprint(nnallocmspace_t* msp)
{
    size_t result;
    nnallocstate_t* ms;
    ms = (nnallocstate_t*)msp;
    if(nn_allocator_okmagic(ms))
    {
        result = ms->maxfootprint;
    }
    NNALLOC_CMAC_USAGEERRORACTION(ms, ms);
    return result;
}

nnallocmallocinfo_t nn_allocator_mspacemallinfo(nnallocmspace_t* msp)
{
    nnallocstate_t* ms;
    ms = (nnallocstate_t*)msp;
    if(!nn_allocator_okmagic(ms))
    {
        NNALLOC_CMAC_USAGEERRORACTION(ms, ms);
    }
    return nn_allocator_mstate_internalmallinfo(ms);
}

int nn_allocator_mspacemallopt(int param_number, int value)
{
    return nn_allocator_changemparam(param_number, value);
}


/* -------------------- Alternative NNALLOC_CONF_MORECOREFUNCNAME functions ------------------- */

/*
  Guidelines for creating a custom version of NNALLOC_CONF_MORECOREFUNCNAME:

  * For best performance, NNALLOC_CONF_MORECOREFUNCNAME should allocate in multiples of pagesize.
  * NNALLOC_CONF_MORECOREFUNCNAME may allocate more memory than requested. (Or even less,
      but this will usually result in a malloc failure.)
  * NNALLOC_CONF_MORECOREFUNCNAME must not allocate memory when given argument zero, but
      instead return one past the end address of memory from previous
      nonzero call.
  * For best performance, consecutive calls to NNALLOC_CONF_MORECOREFUNCNAME with positive
      arguments should return increasing addresses, indicating that
      space has been contiguously extended.
  * Even though consecutive calls to NNALLOC_CONF_MORECOREFUNCNAME need not return contiguous
      addresses, it must be OK for malloc'ed chunks to span multiple
      regions in those cases where they do happen to be contiguous.
  * NNALLOC_CONF_MORECOREFUNCNAME need not handle negative arguments -- it may instead
      just return NNALLOC_CONST_MFAIL when given negative arguments.
      Negative arguments are always multiples of pagesize. NNALLOC_CONF_MORECOREFUNCNAME
      must not misinterpret negative args as large positive unsigned
      args. You can suppress all such calls from even occurring by defining
      MORECORE_CANNOT_TRIM,

  As an example alternative NNALLOC_CONF_MORECOREFUNCNAME, here is a custom allocator
  kindly contributed for pre-OSX macOS.  It uses virtually but not
  necessarily physically contiguous non-paged memory (locked in,
  present and won't get swapped out).  You can use it by uncommenting
  this section, adding some #includes, and setting up the appropriate
  defines above:

      #define NNALLOC_CONF_MORECOREFUNCNAME osMoreCore

  There is also a shutdown routine that should somehow be called for
  cleanup upon program exit.

  enum
  {
      MAX_POOL_ENTRIES = 100,
      MINIMUM_MORECORE_SIZE = (64 * 1024U),
  };
  static int next_os_pool;
  void *our_os_pools[MAX_POOL_ENTRIES];

  void *osMoreCore(int size)
  {
    void *ptr = 0;
    static void *sbrk_top = 0;

    if (size > 0)
    {
      if (size < MINIMUM_MORECORE_SIZE)
      {
          size = MINIMUM_MORECORE_SIZE;
      }
      if (CurrentExecutionLevel() == kTaskLevel)
      {
          ptr = PoolAllocateResident(size + RM_PAGE_SIZE, 0);
      }
      if (ptr == 0)
      {
        return (void *) NNALLOC_CONST_MFAIL;
      }
      // save ptrs so they can be freed during cleanup
      our_os_pools[next_os_pool] = ptr;
      next_os_pool++;
      ptr = (void *) ((((size_t) ptr) + RM_PAGE_MASK) & ~RM_PAGE_MASK);
      sbrk_top = (char *) ptr + size;
      return ptr;
    }
    else if (size < 0)
    {
      // we don't currently support shrink behavior
      return (void *) NNALLOC_CONST_MFAIL;
    }
    else
    {
      return sbrk_top;
    }
  }

  // cleanup any allocated memory pools
  // called as last thing before shutting down driver

  void osCleanupMem()
  {
    void **ptr;

    for (ptr = our_os_pools; ptr < &our_os_pools[MAX_POOL_ENTRIES]; ptr++)
    {
      if (*ptr)
      {
         PoolDeallocate(*ptr);
         *ptr = 0;
      }
    }
  }

*/

