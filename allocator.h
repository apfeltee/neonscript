
/*
** Bundled memory allocator.
** based on LuaJITs allocator, HEAVILY modified to be mostly standalone.
**
** Beware: this is a HEAVILY CUSTOMIZED version of dlmalloc.
** The original bears the following remark:
**
**   This is a version (aka dlmalloc) of malloc/free/realloc written by
**   Doug Lea and released to the public domain, as explained at
**   http://creativecommons.org/licenses/publicdomain.
**
**   * Version pre-2.8.4 Wed Mar 29 19:46:29 2006    (dl at gee)
**
** No additional copyright is claimed over the customizations.
** Please do NOT bother the original author about this version here!
**
** If you want to use dlmalloc in another project, you should get
** the original from: ftp://gee.cs.oswego.edu/pub/misc/
** For thread-safe derivatives, take a look at:
** - ptmalloc: http://www.malloc.de/
** - nedmalloc: http://www.nedprod.com/programs/portable/nedmalloc/
*/

#ifndef _LJ_ALLOC_H
#define _LJ_ALLOC_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>


#if defined(__linux__) && !defined(_GNU_SOURCE)
    #define _GNU_SOURCE
#endif

#if defined(__GNUC__) || defined(__TINYC__)
    #define MEMPOOL_INLINE __attribute__((always_inline)) inline
#else
    #define MEMPOOL_INLINE inline
#endif

#if defined(__cplusplus)
    #define MEMPOOL_CPP_BEGINEXTERN() extern "C" {
    #define MEMPOOL_CPP_ENDEXTERN() }
#else
    #define MEMPOOL_CPP_BEGINEXTERN()
    #define MEMPOOL_CPP_ENDEXTERN()
#endif

/* Target architectures. */
enum
{
    MEMPOOL_ARCH_X86 = 1,
    MEMPOOL_ARCH_X64 = 2,
    MEMPOOL_ARCH_ARM = 3,
};


/* Select native OS if no target OS defined. */
#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)
    #define MEMPOOL_TARGET_WINDOWS
#elif defined(__linux__)
    #define MEMPOOL_TARGET_LINUX
#elif defined(__MACH__) && defined(__APPLE__)
    #define MEMPOOL_TARGET_OSX
#elif(defined(__sun__) && defined(__svr4__)) || defined(__CYGWIN__)
    #define MEMPOOL_TARGET_POSIX
#else
    #define MEMPOOL_TARGET_OTHER
#endif


#if defined(MEMPOOL_TARGET_WINDOWS)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #if defined(_MSC_VER)
        #include <intrin.h>
    #endif
#else
    #include <errno.h>
    #include <sys/mman.h>
#endif

#ifdef __CELLOS_LV2__
    #define MEMPOOL_TARGET_PS3 1
#endif

#ifdef __ORBIS__
    #define MEMPOOL_TARGET_PS4 1
    #undef NULL
    #define NULL ((void*)0)
#endif

/* Set target architecture properties. */

#if defined(__i386) || defined(__i386__) || defined(_M_IX86)
    #define MEMPOOL_ARCH_IS32BIT
#elif defined(__x86_64__) || defined(__x86_64) || defined(_M_X64) || defined(_M_AMD64)
    #define MEMPOOL_ARCH_IS64BIT
#elif defined(__arm__) || defined(__arm) || defined(__ARM__) || defined(__ARM)
    #define MEMPOOL_ARCH_IS32BIT
#else
    #error "No support for this architecture (yet)"
#endif

#ifndef MEMPOOL_PAGESIZE
    #define MEMPOOL_PAGESIZE 4096
#endif


#if !defined(MEMPOOL_LIKELY)
    #if defined(__GNUC__)
        #define MEMPOOL_LIKELY(x) __builtin_expect(!!(x), 1)
        #define MEMPOOL_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #else
        #define MEMPOOL_LIKELY(x) x
        #define MEMPOOL_UNLIKELY(x) x
    #endif
#endif

/* Bin types, widths and sizes */
#define MEMPOOL_CONST_MAXSIZET (~0)
#define MEMPOOL_CONST_MALLOCALIGNMENT (8)

//#define MEMPOOL_CONST_DEFAULTGRANULARITY (128 * 1024)
#define MEMPOOL_CONST_DEFAULTGRANULARITY (32 * 1024)


//#define MEMPOOL_CONST_DEFAULTTRIMTHRESHOLD (2 * (1024 * 1024))
#define MEMPOOL_CONST_DEFAULTTRIMTHRESHOLD (1 * (1024 * 1024))

//#define MEMPOOL_CONST_DEFAULTMMAPTHRESHOLD (128 * 1024)
#define MEMPOOL_CONST_DEFAULTMMAPTHRESHOLD (32 * 1024)

#define MEMPOOL_CONST_MAXRELEASECHECKRATE (255)

/* ------------------- size_t and alignment properties -------------------- */

/* The byte and bit size of a size_t */
#define MEMPOOL_CONST_SIZETSIZE (sizeof(size_t))
#define MEMPOOL_CONST_SIZETBITSIZE (sizeof(size_t) << 3)

/* Some constants coerced to size_t */
/* Annoying but necessary to avoid errors on some platforms */
#define MEMPOOL_CONST_SIZETONE (1)
#define MEMPOOL_CONST_SIZETTWO (2)
#define MEMPOOL_CONST_TWOSIZETSIZES (MEMPOOL_CONST_SIZETSIZE << 1)
#define MEMPOOL_CONST_FOURSIZETSIZES (MEMPOOL_CONST_SIZETSIZE << 2)
#define MEMPOOL_CONST_SIXSIZETSIZES (MEMPOOL_CONST_FOURSIZETSIZES + MEMPOOL_CONST_TWOSIZETSIZES)


#define MEMPOOL_CONST_NSMALLBINS (32)
#define MEMPOOL_CONST_NTREEBINS (32)
#define MEMPOOL_CONST_SMALLBINSHIFT (3)
#define MEMPOOL_CONST_TREEBINSHIFT (8)
#define MEMPOOL_CONST_MINLARGESIZE (MEMPOOL_CONST_SIZETONE << MEMPOOL_CONST_TREEBINSHIFT)
#define MEMPOOL_CONST_MAXSMALLSIZE (MEMPOOL_CONST_MINLARGESIZE - MEMPOOL_CONST_SIZETONE)
#define MEMPOOL_CONST_MAXSMALLREQUEST (MEMPOOL_CONST_MAXSMALLSIZE - MEMPOOL_CHUNK_ALIGNMASK - MEMPOOL_CHUNK_OVERHEAD)

/* weird cornercase in which mremap doesn't get declared despite _GNU_SOURCE. fucking dumb */
#if defined(MEMPOOL_TARGET_LINUX) || defined(MEMPOOL_TARGET_POSIX)
    MEMPOOL_CPP_BEGINEXTERN()
    void* mremap(void* old_address, size_t old_size, size_t new_size, int flags, ...);
    MEMPOOL_CPP_ENDEXTERN()
#endif

typedef size_t MempoolBindex;               /* Described below */
typedef unsigned int MempoolBinMap;         /* Described below */

typedef struct MempoolPlainChunk MempoolPlainChunk;
typedef struct MempoolSegment MempoolSegment;
typedef struct MempoolTreeChunk MempoolTreeChunk;
typedef struct MempoolState MempoolState;

struct MempoolPlainChunk {
  size_t prev_foot;  /* Size of previous chunk (if free).  */
  size_t head;       /* Size and inuse bits. */
  MempoolPlainChunk *fd;         /* double links -- used only if free. */
  MempoolPlainChunk *bk;
};


struct MempoolTreeChunk {
  /* The first four fields must be compatible with MempoolPlainChunk */
  size_t prev_foot;
  size_t head;
  MempoolTreeChunk *fd;
  MempoolTreeChunk *bk;

  MempoolTreeChunk *child[2];
  MempoolTreeChunk *parent;
  MempoolBindex index;
};

struct MempoolSegment {
  char *base;             /* base address */
  size_t size;             /* allocated size */
  MempoolSegment *next;   /* ptr to next segment */
};

struct MempoolState
{
    MempoolBinMap smallmap;
    MempoolBinMap treemap;
    size_t dvsize;
    size_t topsize;
    MempoolPlainChunk* dv;
    MempoolPlainChunk* top;
    size_t trim_check;
    size_t release_checks;
    MempoolPlainChunk* smallbins[(MEMPOOL_CONST_NSMALLBINS+1)*2];
    MempoolTreeChunk* treebins[MEMPOOL_CONST_NTREEBINS];
    MempoolSegment seg;
};

MEMPOOL_CPP_BEGINEXTERN()

/**
* the only functions you should be using
*/

/* creates a memory pool state. required to use mempool_user* */
void *mempool_createpool();

/* destroys a memory pool */
void mempool_destroypool(void *msp);

void *mempool_usermalloc(void *msp, size_t nsize);
void *mempool_userfree(void *msp, void *ptr);
void *mempool_userrealloc(void *msp, void *ptr, size_t nsize);

MEMPOOL_CPP_ENDEXTERN()

/*
* if need be, everything below this comment can be moved into a separate file
*/

/* The bit mask value corresponding to MEMPOOL_CONST_MALLOCALIGNMENT */
#define MEMPOOL_CHUNK_ALIGNMASK  (MEMPOOL_CONST_MALLOCALIGNMENT - MEMPOOL_CONST_SIZETONE)


/* -------------------------- MMAP support ------------------------------- */

#define MEMPOOL_ON_MFAIL ((void*)(MEMPOOL_CONST_MAXSIZET))
#define MEMPOOL_ON_CMFAIL ((char*)(MEMPOOL_ON_MFAIL)) /* defined for convenience */

#define MEMPOOL_ISDIRECTBIT (MEMPOOL_CONST_SIZETONE)

#if defined(MEMPOOL_TARGET_WINDOWS)
    #if defined(MEMPOOL_ARCH_IS64BIT)
        /* Number of top bits of the lower 32 bits of an address that must be zero.
        ** Apparently 0 gives us full 64 bit addresses and 1 gives us the lower 2GB.
        */
        #define MEMPOOL_NTAVM_ZEROBITS 1
    #else
        #define mempool_util_initmmap() ((void)0)
    #endif
#else
    #define MEMPOOL_MMAP_PROT (PROT_READ | PROT_WRITE)
    #if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
        #define MAP_ANONYMOUS MAP_ANON
    #endif
    #define MEMPOOL_MMAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS)

    #if defined(MEMPOOL_ARCH_IS64BIT)
    /* 64 bit mode needs special support for allocating memory in the lower 2GB. */

        #if defined(MAP_32BIT)
        #elif defined(MEMPOOL_TARGET_OSX) || defined(MEMPOOL_TARGET_PS4) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__sun__)

            /* OSX and FreeBSD mmap() use a naive first-fit linear search.
            ** That's perfect for us. Except that -pagezero_size must be set for OSX,
            ** otherwise the lower 4GB are blocked. And the 32GB RLIMIT_DATA needs
            ** to be reduced to 250MB on FreeBSD.
            */
            #if defined(MEMPOOL_TARGET_OSX)
                #define MEMPOOL_MMAP_REGION_START ((uintptr_t)0x10000)
            #elif defined(MEMPOOL_TARGET_PS4)
                #define MEMPOOL_MMAP_REGION_START ((uintptr_t)0x4000)
            #else
                #define MEMPOOL_MMAP_REGION_START ((uintptr_t)0x10000000)
            #endif
            #define MEMPOOL_MMAP_REGION_END ((uintptr_t)0x80000000)

            #if(defined(__FreeBSD__) || defined(__FreeBSD_kernel__)) && !defined(MEMPOOL_TARGET_PS4)
                #include <sys/resource.h>
            #endif
        #else
            #error "NYI: need an equivalent of MAP_32BIT for this 64 bit OS"
        #endif
    #endif
    #define mempool_util_initmmap() ((void)0)
    #define mempool_util_calldirectmmap(s) mempool_util_callmmap(s)
    #if defined(MEMPOOL_TARGET_LINUX)
        #define mempool_util_callmremap(addr, osz, nsz, mv) mempool_util_callmremapactual((addr), (osz), (nsz), (mv))
        #define MEMPOOL_CALLMREMAPNOMOVE 0
        /* #define CALL_MREMAP_MAYMOVE 1 */
        #if defined(MEMPOOL_ARCH_IS64BIT)
            #define MEMPOOL_CALLMREMAPMV MEMPOOL_CALLMREMAPNOMOVE
        #else
            #define MEMPOOL_CALLMREMAPMV CALL_MREMAP_MAYMOVE
        #endif
    #endif
#endif

#ifndef mempool_util_callmremap
    #define mempool_util_callmremap(addr, osz, nsz, mv) ((void)osz, MEMPOOL_ON_MFAIL)
#endif

/* ------------------- Chunks sizes and alignments ----------------------- */

#define MEMPOOL_MCHUNK_SIZE (sizeof(MempoolPlainChunk))

#define MEMPOOL_CHUNK_OVERHEAD (MEMPOOL_CONST_SIZETSIZE)

/* Direct chunks need a second word of overhead ... */
#define MEMPOOL_DIRECT_CHUNK_OVERHEAD (MEMPOOL_CONST_TWOSIZETSIZES)
/* ... and additional padding for fake next-chunk at foot */
#define MEMPOOL_DIRECTFOOTPAD (MEMPOOL_CONST_FOURSIZETSIZES)

/* The smallest size we can malloc is an aligned minimal chunk */
#define MEMPOOL_MINCHUNKSIZE \
    ((MEMPOOL_MCHUNK_SIZE + MEMPOOL_CHUNK_ALIGNMASK) & ~MEMPOOL_CHUNK_ALIGNMASK)


/* Bounds on request (not chunk) sizes. */
#define MEMPOOL_REQUESTS_MAX ((~MEMPOOL_MINCHUNKSIZE + 1) << 2)
#define MEMPOOL_REQUESTS_MIN (MEMPOOL_MINCHUNKSIZE - MEMPOOL_CHUNK_OVERHEAD - MEMPOOL_CONST_SIZETONE)


/* the number of bytes to offset an address to align it */
static MEMPOOL_INLINE size_t mempool_util_alignoffset(void* a)
{
    return ((((size_t)(a) & MEMPOOL_CHUNK_ALIGNMASK) == 0) ? 0 :((MEMPOOL_CONST_MALLOCALIGNMENT - ((size_t)(a) & MEMPOOL_CHUNK_ALIGNMASK)) & MEMPOOL_CHUNK_ALIGNMASK));
}

/* conversion from malloc headers to user pointers, and back */
static MEMPOOL_INLINE void* mempool_util_chunk2mem(void* p)
{
    return ((void*)((char*)(p) + MEMPOOL_CONST_TWOSIZETSIZES));
}

static MEMPOOL_INLINE MempoolPlainChunk* mempool_util_mem2chunk(void* mem)
{
    return ((MempoolPlainChunk*)((char*)(mem) - MEMPOOL_CONST_TWOSIZETSIZES));
}
    
/* chunk associated with aligned address a */
static MEMPOOL_INLINE MempoolPlainChunk* mempool_util_alignaschunk(char* a)
{
    return (MempoolPlainChunk*)((a) + mempool_util_alignoffset(mempool_util_chunk2mem(a)));
}

/* pad request bytes into a usable size */
static MEMPOOL_INLINE size_t mempool_util_padrequest(size_t req)
{
    return (((req) + MEMPOOL_CHUNK_OVERHEAD + MEMPOOL_CHUNK_ALIGNMASK) & ~MEMPOOL_CHUNK_ALIGNMASK);
}

/* pad request, checking for minimum (but not maximum) */
static MEMPOOL_INLINE size_t mempool_util_request2size(size_t req)
{
    return (((req) < MEMPOOL_REQUESTS_MIN) ? MEMPOOL_MINCHUNKSIZE : mempool_util_padrequest(req));
}

/* ------------------ Operations on head and foot fields ----------------- */

#define MEMPOOL_PINUSE_BIT (MEMPOOL_CONST_SIZETONE)
#define MEMPOOL_CINUSE_BIT (MEMPOOL_CONST_SIZETTWO)
#define MEMPOOL_INUSE_BITS (MEMPOOL_PINUSE_BIT | MEMPOOL_CINUSE_BIT)

/* Head value for fenceposts */
#define MEMPOOL_FENCEPOST_HEAD (MEMPOOL_INUSE_BITS | MEMPOOL_CONST_SIZETSIZE)

/* extraction of fields from head words */
#define mempool_util_cinuse(p) ((p)->head & MEMPOOL_CINUSE_BIT)
#define mempool_util_pinuse(p) ((p)->head & MEMPOOL_PINUSE_BIT)
#define mempool_util_chunksize(p) ((p)->head & ~(MEMPOOL_INUSE_BITS))

#define mempool_util_clearpinuse(p) ((p)->head &= ~MEMPOOL_PINUSE_BIT)

/* Treat space at ptr +/- offset as a chunk */
#define mempool_util_chunkplusoffset(p, s) ((MempoolPlainChunk*)(((char*)(p)) + (s)))
#define mempool_util_chunkminusoffset(p, s) ((MempoolPlainChunk*)(((char*)(p)) - (s)))


/* Set cinuse and pinuse of this chunk and pinuse of next chunk */
#define mempool_util_setinuseandpinuse(mst, p, s)           \
    ((p)->head = (s | MEMPOOL_PINUSE_BIT | MEMPOOL_CINUSE_BIT), \
     ((MempoolPlainChunk*)(((char*)(p)) + (s)))->head |= MEMPOOL_PINUSE_BIT)


/* Set size, cinuse and pinuse bit of this chunk */
#define mempool_util_setsizeandpinuseofinusechunk(mst, p, s) \
    ((p)->head = (s | MEMPOOL_PINUSE_BIT | MEMPOOL_CINUSE_BIT))


/* Ptr to next or previous physical MempoolPlainChunk. */
static MEMPOOL_INLINE MempoolPlainChunk* mempool_util_nextchunk(MempoolPlainChunk* p)
{
    return ((MempoolPlainChunk*)(((char*)(p)) + ((p)->head & ~MEMPOOL_INUSE_BITS)));
}

/* Get/set size at footer */
static MEMPOOL_INLINE void mempool_util_setfoot(MempoolPlainChunk* p, size_t s)
{
    (((MempoolPlainChunk*)((char*)(p) + (s)))->prev_foot = (s));
}

/* Set size, pinuse bit, and foot */
static MEMPOOL_INLINE void mempool_util_setsizeand_pinuseoffreechunk(MempoolPlainChunk* p, size_t s)
{
    (p)->head = (s | MEMPOOL_PINUSE_BIT);
    mempool_util_setfoot(p, s);
}

/* Set size, pinuse bit, foot, and clear next pinuse */
static MEMPOOL_INLINE void mempool_util_setfreewithpinuse(MempoolPlainChunk* p, size_t s, MempoolPlainChunk* n)
{
    mempool_util_clearpinuse(n);
    mempool_util_setsizeand_pinuseoffreechunk(p, s);
}

static MEMPOOL_INLINE bool mempool_util_isdirect(MempoolPlainChunk* p)
{
    return (!((p)->head & MEMPOOL_PINUSE_BIT) && ((p)->prev_foot & MEMPOOL_ISDIRECTBIT));
}

/* Get the internal overhead associated with chunk p */
static MEMPOOL_INLINE size_t mempool_util_overheadfor(MempoolPlainChunk* p)
{
    return (mempool_util_isdirect(p) ? MEMPOOL_DIRECT_CHUNK_OVERHEAD : MEMPOOL_CHUNK_OVERHEAD);
}

/* ---------------------- Overlaid data structures ----------------------- */

/* A little helper macro for trees */
static MEMPOOL_INLINE MempoolTreeChunk* mempool_util_leftmostchild(MempoolTreeChunk* t)
{
    return ((t)->child[0] != 0 ? (t)->child[0] : (t)->child[1]);
}

static MEMPOOL_INLINE bool mempool_util_isstateinitialized(MempoolState* mst)
{
    return ((mst)->top != 0);
}

/* -------------------------- system alloc setup ------------------------- */

/* page-align a size */
static MEMPOOL_INLINE size_t mempool_util_pagealign(size_t sz)
{
    return (((sz) + (MEMPOOL_PAGESIZE - MEMPOOL_CONST_SIZETONE)) & ~(MEMPOOL_PAGESIZE - MEMPOOL_CONST_SIZETONE));
}

/* granularity-align a size */
static MEMPOOL_INLINE size_t mempool_util_granularityalign(size_t sz)
{
    return (((sz) + (MEMPOOL_CONST_DEFAULTGRANULARITY - MEMPOOL_CONST_SIZETONE)) & ~(MEMPOOL_CONST_DEFAULTGRANULARITY - MEMPOOL_CONST_SIZETONE));
}

/*  True if segment segm holds address a */
static MEMPOOL_INLINE bool mempool_util_segmentholds(MempoolSegment* segm, void* a)
{
    return ((char*)(a) >= segm->base && (char*)(a) < segm->base + segm->size);
}

static MEMPOOL_INLINE size_t mempool_util_mmapalign(size_t sz)
{
    #if defined(MEMPOOL_TARGET_WINDOWS)
        return mempool_util_granularityalign(sz);
    #else
        return mempool_util_pagealign(sz);
    #endif
}

#if defined(MEMPOOL_TARGET_WINDOWS)
    #if defined(MEMPOOL_ARCH_IS64BIT)
        /* Undocumented, but hey, that's what we all love so much about Windows. */
        typedef long (*PNTAVM)(HANDLE handle, void** addr, ULONG zbits, size_t* size, ULONG alloctype, ULONG prot);
        static PNTAVM ntavm;

        static MEMPOOL_INLINE void mempool_util_initmmap()
        {
            ntavm = (PNTAVM)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtAllocateVirtualMemory");
        }

        /* Win64 32 bit MMAP via NtAllocateVirtualMemory. */
        static MEMPOOL_INLINE void* mempool_util_callmmap(size_t size)
        {
            long st;
            DWORD olderr;
            void* ptr;
            olderr = GetLastError();
            ptr = NULL;
            st = ntavm(INVALID_HANDLE_VALUE, &ptr, MEMPOOL_NTAVM_ZEROBITS, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            SetLastError(olderr);
            return st == 0 ? ptr : MEMPOOL_ON_MFAIL;
        }

        /* For direct MMAP, use MEM_TOP_DOWN to minimize interference */
        static MEMPOOL_INLINE void* mempool_util_calldirectmmap(size_t size)
        {
            long st;
            DWORD olderr;
            void* ptr;
            olderr = GetLastError();
            ptr = NULL;
            st = ntavm(INVALID_HANDLE_VALUE, &ptr, MEMPOOL_NTAVM_ZEROBITS, &size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE);
            SetLastError(olderr);
            return st == 0 ? ptr : MEMPOOL_ON_MFAIL;
        }
    #else
        /* Win32 MMAP via VirtualAlloc */
        static MEMPOOL_INLINE void* mempool_util_callmmap(size_t size)
        {
            void* ptr;
            DWORD olderr;
            olderr = GetLastError();
            ptr = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            SetLastError(olderr);
            return ptr ? ptr : MEMPOOL_ON_MFAIL;
        }

        /* For direct MMAP, use MEM_TOP_DOWN to minimize interference */
        static MEMPOOL_INLINE void* mempool_util_calldirectmmap(size_t size)
        {
            void* ptr;
            DWORD olderr;
            olderr = GetLastError();
            ptr = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE);
            SetLastError(olderr);
            return ptr ? ptr : MEMPOOL_ON_MFAIL;
        }
    #endif

    /* This function supports releasing coalesed segments */
    static MEMPOOL_INLINE int mempool_util_callmunmap(void* ptr, size_t size)
    {
        char* cptr;
        DWORD olderr;
        MEMORY_BASIC_INFORMATION minfo;
        olderr = GetLastError();
        cptr = (char*)ptr;
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
        SetLastError(olderr);
        return 0;
    }
#else
    #if defined(MEMPOOL_ARCH_IS64BIT)
    /* 64 bit mode needs special support for allocating memory in the lower 2GB. */
        #if defined(MAP_32BIT)
            /* Actually this only gives us max. 1GB in current Linux kernels. */
            static MEMPOOL_INLINE void* mempool_util_callmmap(size_t size)
            {
                int olderr;
                void* ptr;
                olderr = errno;
                ptr = mmap(NULL, size, MEMPOOL_MMAP_PROT, MAP_32BIT | MEMPOOL_MMAP_FLAGS, -1, 0);
                errno = olderr;
                return ptr;
            }
        #elif defined(MEMPOOL_TARGET_OSX) || defined(MEMPOOL_TARGET_PS4) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__sun__)
            static MEMPOOL_INLINE void* mempool_util_callmmap(size_t size)
            {
                int olderr;
                int retry;
                void* p;
                #if(defined(__FreeBSD__) || defined(__FreeBSD_kernel__)) && !defined(MEMPOOL_TARGET_PS4)
                    struct rlimit rlim;
                #endif
                static uintptr_t allochint = MEMPOOL_MMAP_REGION_START;
                #if(defined(__FreeBSD__) || defined(__FreeBSD_kernel__)) && !defined(MEMPOOL_TARGET_PS4)
                    static int rlimit_modified = 0;
                #endif
                olderr = errno;
                /* Hint for next allocation. Doesn't need to be thread-safe. */
                retry = 0;
                #if(defined(__FreeBSD__) || defined(__FreeBSD_kernel__)) && !defined(MEMPOOL_TARGET_PS4)
                    if(MEMPOOL_UNLIKELY(rlimit_modified == 0))
                    {
                        rlim.rlim_cur = rlim.rlim_max = MEMPOOL_MMAP_REGION_START;
                        setrlimit(RLIMIT_DATA, &rlim); /* Ignore result. May fail below. */
                        rlimit_modified = 1;
                    }
                #endif
                for(;;)
                {
                    p = mmap((void*)allochint, size, MEMPOOL_MMAP_PROT, MEMPOOL_MMAP_FLAGS, -1, 0);
                    if((uintptr_t)p >= MEMPOOL_MMAP_REGION_START && (uintptr_t)p + size < MEMPOOL_MMAP_REGION_END)
                    {
                        allochint = (uintptr_t)p + size;
                        errno = olderr;
                        return p;
                    }
                    if(p != MEMPOOL_ON_CMFAIL)
                    {
                        munmap(p, size);
                    }
                    #ifdef __sun__
                        allochint += 0x1000000; /* Need near-exhaustive linear scan. */
                        if(allochint + size < MEMPOOL_MMAP_REGION_END)
                        {
                            continue;
                        }
                    #endif
                    if(retry)
                    {
                        break;
                    }
                    retry = 1;
                    allochint = MEMPOOL_MMAP_REGION_START;
                }
                errno = olderr;
                return MEMPOOL_ON_CMFAIL;
            }
        #else
            #error "NYI: need an equivalent of MAP_32BIT for this 64 bit OS"
        #endif
    #else
        /* 32 bit mode is easy. */
        static MEMPOOL_INLINE void* mempool_util_callmmap(size_t size)
        {
            int olderr;
            void* ptr;
            olderr = errno;
            ptr = mmap(NULL, size, MEMPOOL_MMAP_PROT, MEMPOOL_MMAP_FLAGS, -1, 0);
            errno = olderr;
            return ptr;
        }
    #endif
    static MEMPOOL_INLINE int mempool_util_callmunmap(void* ptr, size_t size)
    {
        int ret;
        int olderr;
        olderr = errno;
        ret = munmap(ptr, size);
        errno = olderr;
        return ret;
    }

    #if defined(MEMPOOL_TARGET_LINUX)
        /* Need to define _GNU_SOURCE to get the mremap prototype. */
        static MEMPOOL_INLINE void* mempool_util_callmremapactual(void* ptr, size_t osz, size_t nsz, int flags)
        {
            int olderr;
            olderr = errno;
            ptr = mremap(ptr, osz, nsz, flags);
            errno = olderr;
            return ptr;
        }
    #endif
#endif

static MEMPOOL_INLINE int mempool_util_nativebitscanreverse(uint64_t x)
{
    static const char bsr_debruijntable[64] = {
        0,  47, 1,  56, 48, 27, 2,  60, 57, 49, 41, 37, 28, 16, 3,  61,
        54, 58, 35, 52, 50, 42, 21, 44, 38, 32, 29, 23, 17, 11, 4,  62,
        46, 55, 26, 59, 40, 36, 15, 53, 34, 51, 20, 43, 31, 22, 10, 45,
        25, 39, 14, 33, 19, 30, 9,  24, 13, 18, 8,  12, 7,  6,  5,  63,
    };
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    return bsr_debruijntable[(x * 0x03f79d71b4cb0a89) >> 58];
}

static MEMPOOL_INLINE int mempool_util_nativebitscanforward(uint64_t x)
{
    uint32_t l, r;
    x &= -x;
    l = x | x >> 32;
    r = !!(x >> 32), r <<= 1;
    r += !!((l & 0xffff0000)), r <<= 1;
    r += !!((l & 0xff00ff00)), r <<= 1;
    r += !!((l & 0xf0f0f0f0)), r <<= 1;
    r += !!((l & 0xcccccccc)), r <<= 1;
    r += !!((l & 0xaaaaaaaa));
    return r;
}

/* Return segment holding given address */
static MEMPOOL_INLINE MempoolSegment* mempool_util_segmentholding(MempoolState* m, char* addr)
{
    MempoolSegment* sp;
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
}

/* Return true if segment contains a segment link */
static MEMPOOL_INLINE int mempool_util_hassegmentlink(MempoolState* m, MempoolSegment* ss)
{
    MempoolSegment* sp = &m->seg;
    for(;;)
    {
        if((char*)sp >= ss->base && (char*)sp < ss->base + ss->size)
            return 1;
        if((sp = sp->next) == 0)
            return 0;
    }
}

/*
  mempool_util_topfootsize is padding at the end of a segment, including space
  that may be needed to place segment records and fenceposts when new
  noncontiguous segments are added.
*/
static MEMPOOL_INLINE size_t mempool_util_topfootsize()
{
    return (mempool_util_alignoffset(mempool_util_chunk2mem(0)) + mempool_util_padrequest(sizeof(MempoolSegment)) + MEMPOOL_MINCHUNKSIZE);
}

/* ---------------------------- Indexing Bins ---------------------------- */

static MEMPOOL_INLINE bool mempool_util_issmall(size_t s)
{
    return (((s) >> MEMPOOL_CONST_SMALLBINSHIFT) < MEMPOOL_CONST_NSMALLBINS);
}

static MEMPOOL_INLINE MempoolBindex mempool_util_smallindex(MempoolBindex s)
{
    return ((s) >> MEMPOOL_CONST_SMALLBINSHIFT);
}

static MEMPOOL_INLINE size_t mempool_util_smallindex2size(MempoolBindex i)
{
    return ((i) << MEMPOOL_CONST_SMALLBINSHIFT);
}

/* addressing by index. See above about smallbin repositioning */
static MEMPOOL_INLINE MempoolPlainChunk* mempool_util_smallbinat(MempoolState* mst, size_t i)
{
    return ((MempoolPlainChunk*)((char*)&((mst)->smallbins[(i) << 1])));
}

static MEMPOOL_INLINE MempoolTreeChunk** mempool_util_treebinat(MempoolState* mst, size_t i)
{
    return (&((mst)->treebins[i]));
}

/* assign tree index for size sz to variable desti */
static MEMPOOL_INLINE void mempool_util_computetreeindex(size_t sz, size_t* desti)
{
    unsigned int x;
    x = (unsigned int)(sz >> MEMPOOL_CONST_TREEBINSHIFT);
    if(x == 0)
    {
        (*desti) = 0;
    }
    else if(x > 0xFFFF)
    {
        (*desti) = MEMPOOL_CONST_NTREEBINS - 1;
    }
    else
    {
        unsigned int k = mempool_util_nativebitscanreverse(x);
        (*desti) = (MempoolBindex)((k << 1) + ((sz >> (k + (MEMPOOL_CONST_TREEBINSHIFT - 1)) & 1)));
    }
}

/* Shift placing maximum resolved bit in a treebin at i as sign bit */
static MEMPOOL_INLINE size_t mempool_util_leftshiftfortreeindex(size_t i)
{
    if(i == MEMPOOL_CONST_NTREEBINS - 1)
    {
        return 0;
    }
    return ((MEMPOOL_CONST_SIZETBITSIZE - MEMPOOL_CONST_SIZETONE) - (((i) >> 1) + MEMPOOL_CONST_TREEBINSHIFT - 2));
}


/* ------------------------ Operations on bin maps ----------------------- */

/* bit corresponding to given index */
static MEMPOOL_INLINE MempoolBinMap mempool_util_idx2bit(size_t i)
{
    return ((MempoolBinMap)(1) << (i));
}

/* Mark/Clear bits with given index */
static MEMPOOL_INLINE void mempool_util_marksmallmap(MempoolState* mst, size_t i)
{
    (mst)->smallmap |= mempool_util_idx2bit(i);
}

static MEMPOOL_INLINE void mempool_util_clearsmallmap(MempoolState* mst, size_t i)
{
    (mst)->smallmap &= ~mempool_util_idx2bit(i);
}

static MEMPOOL_INLINE bool mempool_util_smallmapismarked(MempoolState* mst, size_t i)
{
    return ((mst)->smallmap & mempool_util_idx2bit(i));
}

static MEMPOOL_INLINE void mempool_util_marktreemap(MempoolState* mst, size_t i)
{
    (mst)->treemap |= mempool_util_idx2bit(i);
}

static MEMPOOL_INLINE void mempool_util_cleartreemap(MempoolState* mst, size_t i)
{
    (mst)->treemap &= ~mempool_util_idx2bit(i);
}

static MEMPOOL_INLINE bool mempool_util_treemapismarked(MempoolState* mst, size_t i)
{
    return ((mst)->treemap & mempool_util_idx2bit(i));
}

/* mask with all bits to left of least bit of x on */
static MEMPOOL_INLINE MempoolBinMap mempool_util_leftbits(MempoolBinMap x)
{
    return ((x << 1) | (~(x << 1) + 1));
}

/* Set cinuse bit and pinuse bit of next chunk */
static MEMPOOL_INLINE void mempool_util_setinuse(MempoolState* mst, MempoolPlainChunk* p, size_t s)
{
    (void)mst;
    (p)->head = (((p)->head & MEMPOOL_PINUSE_BIT) | s | MEMPOOL_CINUSE_BIT);
    ((MempoolPlainChunk*)(((char*)(p)) + (s)))->head |= MEMPOOL_PINUSE_BIT;
}

/* ----------------------- Operations on smallbins ----------------------- */

/* Link a free chunk into a smallbin  */
static MEMPOOL_INLINE void mempool_util_insertsmallchunk(MempoolState* mst, MempoolPlainChunk* p, size_t sz)
{
    MempoolBindex i = mempool_util_smallindex(sz);
    MempoolPlainChunk* b = mempool_util_smallbinat(mst, i);
    MempoolPlainChunk* f = b;
    if(!mempool_util_smallmapismarked(mst, i))
    {
        mempool_util_marksmallmap(mst, i);
    }
    else
    {
        f = b->fd;
    }
    b->fd = p;
    f->bk = p;
    p->fd = f;
    p->bk = b;
}

/* Unlink a chunk from a smallbin  */
static MEMPOOL_INLINE void mempool_util_unlinksmallchunk(MempoolState* mst, MempoolPlainChunk* p, size_t sz)
{
    MempoolPlainChunk* f = p->fd;
    MempoolPlainChunk* b = p->bk;
    MempoolBindex i = mempool_util_smallindex(sz);
    if(f == b)
    {
        mempool_util_clearsmallmap(mst, i);
    }
    else
    {
        f->bk = b;
        b->fd = f;
    }
}

/* Unlink the first chunk from a smallbin */
static MEMPOOL_INLINE void mempool_util_unlinkfirstsmallchunk(MempoolState* mst, MempoolPlainChunk* b, MempoolPlainChunk* p, size_t i)
{
    MempoolPlainChunk* f = p->fd;
    if(b == f)
    {
        mempool_util_clearsmallmap(mst, i);
    }
    else
    {
        b->fd = f;
        f->bk = b;
    }
}

/* Replace dv node, binning the old one */
/* Used only when dvsize known to be small */
static MEMPOOL_INLINE void mempool_util_replacedv(MempoolState* mst, MempoolPlainChunk* p, size_t sz)
{
    size_t DVS = mst->dvsize;
    if(DVS != 0)
    {
        MempoolPlainChunk* DV = mst->dv;
        mempool_util_insertsmallchunk(mst, DV, DVS);
    }
    mst->dvsize = sz;
    mst->dv = p;
}


/* ------------------------- Operations on trees ------------------------- */

/* Insert chunk into tree */
static MEMPOOL_INLINE void mempool_util_insertlargechunk(MempoolState* mst, MempoolTreeChunk* tchunk, size_t psz)
{
    MempoolTreeChunk** hp;
    MempoolBindex i;
    mempool_util_computetreeindex(psz, &i);
    hp = mempool_util_treebinat(mst, i);
    tchunk->index = i;
    tchunk->child[0] = tchunk->child[1] = 0;
    if(!mempool_util_treemapismarked(mst, i))
    {
        mempool_util_marktreemap(mst, i);
        *hp = tchunk;
        tchunk->parent = (MempoolTreeChunk*)hp;
        tchunk->fd = tchunk->bk = tchunk;
    }
    else
    {
        MempoolTreeChunk* t = *hp;
        size_t k = psz << mempool_util_leftshiftfortreeindex(i);
        for(;;)
        {
            if(mempool_util_chunksize(t) != psz)
            {
                MempoolTreeChunk** cchunk = &(t->child[(k >> (MEMPOOL_CONST_SIZETBITSIZE - MEMPOOL_CONST_SIZETONE)) & 1]);
                k <<= 1;
                if(*cchunk != 0)
                {
                    t = *cchunk;
                }
                else
                {
                    *cchunk = tchunk;
                    tchunk->parent = t;
                    tchunk->fd = tchunk->bk = tchunk;
                    break;
                }
            }
            else
            {
                MempoolTreeChunk* f = t->fd;
                t->fd = f->bk = tchunk;
                tchunk->fd = f;
                tchunk->bk = t;
                tchunk->parent = 0;
                break;
            }
        }
    }
}

static MEMPOOL_INLINE void mempool_util_unlinklargechunk(MempoolState* mst, MempoolTreeChunk* tchunk)
{
    MempoolTreeChunk* xp = tchunk->parent;
    MempoolTreeChunk* r;
    if(tchunk->bk != tchunk)
    {
        MempoolTreeChunk* f = tchunk->fd;
        r = tchunk->bk;
        f->bk = r;
        r->fd = f;
    }
    else
    {
        MempoolTreeChunk** rp;
        if(((r = *(rp = &(tchunk->child[1]))) != 0) || ((r = *(rp = &(tchunk->child[0]))) != 0))
        {
            MempoolTreeChunk** cp;
            while((*(cp = &(r->child[1])) != 0) || (*(cp = &(r->child[0])) != 0))
            {
                r = *(rp = cp);
            }
            *rp = 0;
        }
    }
    if(xp != 0)
    {
        MempoolTreeChunk** hp = mempool_util_treebinat(mst, tchunk->index);
        if(tchunk == *hp)
        {
            if((*hp = r) == 0)
                mempool_util_cleartreemap(mst, tchunk->index);
        }
        else
        {
            if(xp->child[0] == tchunk)
                xp->child[0] = r;
            else
                xp->child[1] = r;
        }
        if(r != 0)
        {
            MempoolTreeChunk* c0;
            MempoolTreeChunk* c1;
            r->parent = xp;
            if((c0 = tchunk->child[0]) != 0)
            {
                r->child[0] = c0;
                c0->parent = r;
            }
            if((c1 = tchunk->child[1]) != 0)
            {
                r->child[1] = c1;
                c1->parent = r;
            }
        }
    }
}

/* Relays to large vs small bin operations */

static MEMPOOL_INLINE void mempool_util_insertchunk(MempoolState* mst, MempoolPlainChunk* p, size_t psz)
{
    if(mempool_util_issmall(psz))
    {
        mempool_util_insertsmallchunk(mst, p, psz);
    }
    else
    {
        MempoolTreeChunk* TP = (MempoolTreeChunk*)(p);
        mempool_util_insertlargechunk(mst, TP, psz);
    }
}

static MEMPOOL_INLINE void mempool_util_unlinkchunk(MempoolState* mst, MempoolPlainChunk* p, size_t psz)
{
    if(mempool_util_issmall(psz))
    {
        mempool_util_unlinksmallchunk(mst, p, psz);
    }
    else
    {
        MempoolTreeChunk* TP = (MempoolTreeChunk*)(p);
        mempool_util_unlinklargechunk(mst, TP);
    }
}

/* -----------------------  Direct-mmapping chunks ----------------------- */

static MEMPOOL_INLINE void* mempool_util_directalloc(size_t nb)
{
    size_t mmsize = mempool_util_mmapalign(nb + MEMPOOL_CONST_SIXSIZETSIZES + MEMPOOL_CHUNK_ALIGNMASK);
    if(MEMPOOL_LIKELY(mmsize > nb))
    { /* Check for wrap around 0 */
        char* mm = (char*)(mempool_util_calldirectmmap(mmsize));
        if(mm != MEMPOOL_ON_CMFAIL)
        {
            size_t offset = mempool_util_alignoffset(mempool_util_chunk2mem(mm));
            size_t psize = mmsize - offset - MEMPOOL_DIRECTFOOTPAD;
            MempoolPlainChunk* p = (MempoolPlainChunk*)(mm + offset);
            p->prev_foot = offset | MEMPOOL_ISDIRECTBIT;
            p->head = psize | MEMPOOL_CINUSE_BIT;
            mempool_util_chunkplusoffset(p, psize)->head = MEMPOOL_FENCEPOST_HEAD;
            mempool_util_chunkplusoffset(p, psize + MEMPOOL_CONST_SIZETSIZE)->head = 0;
            return mempool_util_chunk2mem(p);
        }
    }
    return NULL;
}

static MEMPOOL_INLINE MempoolPlainChunk* mempool_util_directresize(MempoolPlainChunk* oldp, size_t nb)
{
    size_t oldsize = mempool_util_chunksize(oldp);
    if(mempool_util_issmall(nb)) /* Can't shrink direct regions below small size */
        return NULL;
    /* Keep old chunk if big enough but not too big */
    if(oldsize >= nb + MEMPOOL_CONST_SIZETSIZE && (oldsize - nb) <= (MEMPOOL_CONST_DEFAULTGRANULARITY >> 1))
    {
        return oldp;
    }
    else
    {
        size_t offset = oldp->prev_foot & ~MEMPOOL_ISDIRECTBIT;
        size_t oldmmsize = oldsize + offset + MEMPOOL_DIRECTFOOTPAD;
        size_t newmmsize = mempool_util_mmapalign(nb + MEMPOOL_CONST_SIXSIZETSIZES + MEMPOOL_CHUNK_ALIGNMASK);
        char* cp = (char*)mempool_util_callmremap((char*)oldp - offset,
                                      oldmmsize, newmmsize, MEMPOOL_CALLMREMAPMV);
        if(cp != MEMPOOL_ON_CMFAIL)
        {
            MempoolPlainChunk* newp = (MempoolPlainChunk*)(cp + offset);
            size_t psize = newmmsize - offset - MEMPOOL_DIRECTFOOTPAD;
            newp->head = psize | MEMPOOL_CINUSE_BIT;
            mempool_util_chunkplusoffset(newp, psize)->head = MEMPOOL_FENCEPOST_HEAD;
            mempool_util_chunkplusoffset(newp, psize + MEMPOOL_CONST_SIZETSIZE)->head = 0;
            return newp;
        }
    }
    return NULL;
}

/* -------------------------- mspace management -------------------------- */

/* Initialize top chunk and its size */
static MEMPOOL_INLINE void mempool_util_inittop(MempoolState* m, MempoolPlainChunk* p, size_t psize)
{
    /* Ensure alignment */
    size_t offset = mempool_util_alignoffset(mempool_util_chunk2mem(p));
    p = (MempoolPlainChunk*)((char*)p + offset);
    psize -= offset;

    m->top = p;
    m->topsize = psize;
    p->head = psize | MEMPOOL_PINUSE_BIT;
    /* set size of fake trailing chunk holding overhead space only once */
    mempool_util_chunkplusoffset(p, psize)->head = mempool_util_topfootsize();
    m->trim_check = MEMPOOL_CONST_DEFAULTTRIMTHRESHOLD; /* reset on each update */
}

/* Initialize bins for a new state that is otherwise zeroed out */
static MEMPOOL_INLINE void mempool_util_initbins(MempoolState* m)
{
    /* Establish circular links for smallbins */
    MempoolBindex i;
    for(i = 0; i < MEMPOOL_CONST_NSMALLBINS; i++)
    {
        MempoolPlainChunk* bin = mempool_util_smallbinat(m, i);
        bin->fd = bin->bk = bin;
    }
}

/* Allocate chunk and prepend remainder with chunk in successor base. */
static MEMPOOL_INLINE void* mempool_util_prependalloc(MempoolState* m, char* newbase, char* oldbase, size_t nb)
{
    MempoolPlainChunk* p = mempool_util_alignaschunk(newbase);
    MempoolPlainChunk* oldfirst = mempool_util_alignaschunk(oldbase);
    size_t psize = (size_t)((char*)oldfirst - (char*)p);
    MempoolPlainChunk* q = mempool_util_chunkplusoffset(p, nb);
    size_t qsize = psize - nb;
    mempool_util_setsizeandpinuseofinusechunk(m, p, nb);

    /* consolidate remainder with first chunk of old base */
    if(oldfirst == m->top)
    {
        size_t tsize = m->topsize += qsize;
        m->top = q;
        q->head = tsize | MEMPOOL_PINUSE_BIT;
    }
    else if(oldfirst == m->dv)
    {
        size_t dsize = m->dvsize += qsize;
        m->dv = q;
        mempool_util_setsizeand_pinuseoffreechunk(q, dsize);
    }
    else
    {
        if(!mempool_util_cinuse(oldfirst))
        {
            size_t nsize = mempool_util_chunksize(oldfirst);
            mempool_util_unlinkchunk(m, oldfirst, nsize);
            oldfirst = mempool_util_chunkplusoffset(oldfirst, nsize);
            qsize += nsize;
        }
        mempool_util_setfreewithpinuse(q, qsize, oldfirst);
        mempool_util_insertchunk(m, q, qsize);
    }

    return mempool_util_chunk2mem(p);
}

/* Add a segment to hold a new noncontiguous region */
static MEMPOOL_INLINE void mempool_util_addsegment(MempoolState* m, char* tbase, size_t tsize)
{
    /* Determine locations and sizes of segment, fenceposts, old top */
    char* old_top = (char*)m->top;
    MempoolSegment* oldsp = mempool_util_segmentholding(m, old_top);
    char* old_end = oldsp->base + oldsp->size;
    size_t ssize = mempool_util_padrequest(sizeof(MempoolSegment));
    char* rawsp = old_end - (ssize + MEMPOOL_CONST_FOURSIZETSIZES + MEMPOOL_CHUNK_ALIGNMASK);
    size_t offset = mempool_util_alignoffset(mempool_util_chunk2mem(rawsp));
    char* asp = rawsp + offset;
    char* csp = (asp < (old_top + MEMPOOL_MINCHUNKSIZE)) ? old_top : asp;
    MempoolPlainChunk* sp = (MempoolPlainChunk*)csp;
    MempoolSegment* ss = (MempoolSegment*)(mempool_util_chunk2mem(sp));
    MempoolPlainChunk* tnext = mempool_util_chunkplusoffset(sp, ssize);
    MempoolPlainChunk* p = tnext;

    /* reset top to new space */
    mempool_util_inittop(m, (MempoolPlainChunk*)tbase, tsize - mempool_util_topfootsize());

    /* Set up segment record */
    mempool_util_setsizeandpinuseofinusechunk(m, sp, ssize);
    *ss = m->seg; /* Push current record */
    m->seg.base = tbase;
    m->seg.size = tsize;
    m->seg.next = ss;

    /* Insert trailing fenceposts */
    for(;;)
    {
        MempoolPlainChunk* nextp = mempool_util_chunkplusoffset(p, MEMPOOL_CONST_SIZETSIZE);
        p->head = MEMPOOL_FENCEPOST_HEAD;
        if((char*)(&(nextp->head)) < old_end)
            p = nextp;
        else
            break;
    }

    /* Insert the rest of old top into a bin as an ordinary free chunk */
    if(csp != old_top)
    {
        MempoolPlainChunk* q = (MempoolPlainChunk*)old_top;
        size_t psize = (size_t)(csp - old_top);
        MempoolPlainChunk* tn = mempool_util_chunkplusoffset(q, psize);
        mempool_util_setfreewithpinuse(q, psize, tn);
        mempool_util_insertchunk(m, q, psize);
    }
}

/* -------------------------- System allocation -------------------------- */

static MEMPOOL_INLINE void* mempool_util_allocsys(MempoolState* m, size_t nb)
{
    char* tbase = MEMPOOL_ON_CMFAIL;
    size_t tsize = 0;

    /* Directly map large chunks */
    if(MEMPOOL_UNLIKELY(nb >= MEMPOOL_CONST_DEFAULTMMAPTHRESHOLD))
    {
        void* mem = mempool_util_directalloc(nb);
        if(mem != 0)
            return mem;
    }

    {
        size_t req = nb + mempool_util_topfootsize() + MEMPOOL_CONST_SIZETONE;
        size_t rsize = mempool_util_granularityalign(req);
        if(MEMPOOL_LIKELY(rsize > nb))
        { /* Fail if wraps around zero */
            char* mp = (char*)(mempool_util_callmmap(rsize));
            if(mp != MEMPOOL_ON_CMFAIL)
            {
                tbase = mp;
                tsize = rsize;
            }
        }
    }

    if(tbase != MEMPOOL_ON_CMFAIL)
    {
        MempoolSegment* sp = &m->seg;
        /* Try to merge with an existing segment */
        while(sp != 0 && tbase != sp->base + sp->size)
            sp = sp->next;
        if(sp != 0 && mempool_util_segmentholds(sp, m->top))
        { /* append */
            sp->size += tsize;
            mempool_util_inittop(m, m->top, m->topsize + tsize);
        }
        else
        {
            sp = &m->seg;
            while(sp != 0 && sp->base != tbase + tsize)
                sp = sp->next;
            if(sp != 0)
            {
                char* oldbase = sp->base;
                sp->base = tbase;
                sp->size += tsize;
                return mempool_util_prependalloc(m, tbase, oldbase, nb);
            }
            else
            {
                mempool_util_addsegment(m, tbase, tsize);
            }
        }

        if(nb < m->topsize)
        { /* Allocate from new or extended top space */
            size_t rsize = m->topsize -= nb;
            MempoolPlainChunk* p = m->top;
            MempoolPlainChunk* r = m->top = mempool_util_chunkplusoffset(p, nb);
            r->head = rsize | MEMPOOL_PINUSE_BIT;
            mempool_util_setsizeandpinuseofinusechunk(m, p, nb);
            return mempool_util_chunk2mem(p);
        }
    }

    return NULL;
}

/* -----------------------  system deallocation -------------------------- */

/* Unmap and unlink any mmapped segments that don't contain used chunks */
static MEMPOOL_INLINE size_t mempool_util_releaseunusedsegments(MempoolState* m)
{
    size_t released = 0;
    size_t nsegs = 0;
    MempoolSegment* pred = &m->seg;
    MempoolSegment* sp = pred->next;
    while(sp != 0)
    {
        char* base = sp->base;
        size_t size = sp->size;
        MempoolSegment* next = sp->next;
        nsegs++;
        {
            MempoolPlainChunk* p = mempool_util_alignaschunk(base);
            size_t psize = mempool_util_chunksize(p);
            /* Can unmap if first chunk holds entire segment and not pinned */
            if(!mempool_util_cinuse(p) && (char*)p + psize >= base + size - mempool_util_topfootsize())
            {
                MempoolTreeChunk* tp = (MempoolTreeChunk*)p;
                if(p == m->dv)
                {
                    m->dv = 0;
                    m->dvsize = 0;
                }
                else
                {
                    mempool_util_unlinklargechunk(m, tp);
                }
                if(mempool_util_callmunmap(base, size) == 0)
                {
                    released += size;
                    /* unlink obsoleted record */
                    sp = pred;
                    sp->next = next;
                }
                else
                { /* back out if cannot unmap */
                    mempool_util_insertlargechunk(m, tp, psize);
                }
            }
        }
        pred = sp;
        sp = next;
    }
    /* Reset check counter */
    m->release_checks = nsegs > MEMPOOL_CONST_MAXRELEASECHECKRATE ?
                        nsegs :
                        MEMPOOL_CONST_MAXRELEASECHECKRATE;
    return released;
}

static MEMPOOL_INLINE int mempool_util_alloctrim(MempoolState* m, size_t pad)
{
    size_t released = 0;
    if(pad < MEMPOOL_REQUESTS_MAX && mempool_util_isstateinitialized(m))
    {
        pad += mempool_util_topfootsize(); /* ensure enough room for segment overhead */

        if(m->topsize > pad)
        {
            /* Shrink top space in granularity-size units, keeping at least one */
            size_t unit = MEMPOOL_CONST_DEFAULTGRANULARITY;
            size_t extra = ((m->topsize - pad + (unit - MEMPOOL_CONST_SIZETONE)) / unit - MEMPOOL_CONST_SIZETONE) * unit;
            MempoolSegment* sp = mempool_util_segmentholding(m, (char*)m->top);

            if(sp->size >= extra && !mempool_util_hassegmentlink(m, sp))
            { /* can't shrink if pinned */
                size_t newsize = sp->size - extra;
                /* Prefer mremap, fall back to munmap */
                if((mempool_util_callmremap(sp->base, sp->size, newsize, MEMPOOL_CALLMREMAPNOMOVE) != MEMPOOL_ON_MFAIL) || (mempool_util_callmunmap(sp->base + newsize, extra) == 0))
                {
                    released = extra;
                }
            }

            if(released != 0)
            {
                sp->size -= released;
                mempool_util_inittop(m, m->top, m->topsize - released);
            }
        }

        /* Unmap any unused mmapped segments */
        released += mempool_util_releaseunusedsegments(m);

        /* On failure, disable autotrim to avoid repeated failed future calls */
        if(released == 0 && m->topsize > m->trim_check)
            m->trim_check = MEMPOOL_CONST_MAXSIZET;
    }

    return (released != 0) ? 1 : 0;
}

/* ---------------------------- malloc support --------------------------- */

/* allocate a large request from the best fitting chunk in a treebin */
static MEMPOOL_INLINE void* mempool_util_tmalloclarge(MempoolState* m, size_t nb)
{
    MempoolTreeChunk* v = 0;
    size_t rsize = ~nb + 1; /* Unsigned negation */
    MempoolTreeChunk* t;
    MempoolBindex idx;
    mempool_util_computetreeindex(nb, &idx);

    if((t = *mempool_util_treebinat(m, idx)) != 0)
    {
        /* Traverse tree for this bin looking for node with size == nb */
        size_t sizebits = nb << mempool_util_leftshiftfortreeindex(idx);
        MempoolTreeChunk* rst = 0; /* The deepest untaken right subtree */
        for(;;)
        {
            MempoolTreeChunk* rt;
            size_t trem = mempool_util_chunksize(t) - nb;
            if(trem < rsize)
            {
                v = t;
                if((rsize = trem) == 0)
                    break;
            }
            rt = t->child[1];
            t = t->child[(sizebits >> (MEMPOOL_CONST_SIZETBITSIZE - MEMPOOL_CONST_SIZETONE)) & 1];
            if(rt != 0 && rt != t)
                rst = rt;
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
        MempoolBinMap leftbits = mempool_util_leftbits(mempool_util_idx2bit(idx)) & m->treemap;
        if(leftbits != 0)
            t = *mempool_util_treebinat(m, mempool_util_nativebitscanforward(leftbits));
    }

    while(t != 0)
    { /* find smallest of tree or subtree */
        size_t trem = mempool_util_chunksize(t) - nb;
        if(trem < rsize)
        {
            rsize = trem;
            v = t;
        }
        t = mempool_util_leftmostchild(t);
    }

    /*  If dv is a better fit, return NULL so malloc will use it */
    if(v != 0 && rsize < (size_t)(m->dvsize - nb))
    {
        MempoolPlainChunk* r = mempool_util_chunkplusoffset(v, nb);
        mempool_util_unlinklargechunk(m, v);
        if(rsize < MEMPOOL_MINCHUNKSIZE)
        {
            mempool_util_setinuseandpinuse(m, v, (rsize + nb));
        }
        else
        {
            mempool_util_setsizeandpinuseofinusechunk(m, v, nb);
            mempool_util_setsizeand_pinuseoffreechunk(r, rsize);
            mempool_util_insertchunk(m, r, rsize);
        }
        return mempool_util_chunk2mem(v);
    }
    return NULL;
}

/* allocate a small request from the best fitting chunk in a treebin */
static MEMPOOL_INLINE void* mempool_util_tmallocsmall(MempoolState* m, size_t nb)
{
    MempoolTreeChunk* t;
    MempoolTreeChunk* v;
    MempoolPlainChunk* r;
    size_t rsize;
    MempoolBindex i = mempool_util_nativebitscanforward(m->treemap);

    v = t = *mempool_util_treebinat(m, i);
    rsize = mempool_util_chunksize(t) - nb;

    while((t = mempool_util_leftmostchild(t)) != 0)
    {
        size_t trem = mempool_util_chunksize(t) - nb;
        if(trem < rsize)
        {
            rsize = trem;
            v = t;
        }
    }

    r = mempool_util_chunkplusoffset(v, nb);
    mempool_util_unlinklargechunk(m, v);
    if(rsize < MEMPOOL_MINCHUNKSIZE)
    {
        mempool_util_setinuseandpinuse(m, v, (rsize + nb));
    }
    else
    {
        mempool_util_setsizeandpinuseofinusechunk(m, v, nb);
        mempool_util_setsizeand_pinuseoffreechunk(r, rsize);
        mempool_util_replacedv(m, r, rsize);
    }
    return mempool_util_chunk2mem(v);
}

/* ----------------------------------------------------------------------- */

void* mempool_createpool()
{
    size_t tsize = MEMPOOL_CONST_DEFAULTGRANULARITY;
    char* tbase;
    mempool_util_initmmap();
    tbase = (char*)(mempool_util_callmmap(tsize));
    if(tbase != MEMPOOL_ON_CMFAIL)
    {
        size_t msize = mempool_util_padrequest(sizeof(MempoolState));
        MempoolPlainChunk* mn;
        MempoolPlainChunk* msp = mempool_util_alignaschunk(tbase);
        MempoolState* m = (MempoolState*)(mempool_util_chunk2mem(msp));
        memset(m, 0, msize);
        msp->head = (msize | MEMPOOL_PINUSE_BIT | MEMPOOL_CINUSE_BIT);
        m->seg.base = tbase;
        m->seg.size = tsize;
        m->release_checks = MEMPOOL_CONST_MAXRELEASECHECKRATE;
        mempool_util_initbins(m);
        mn = mempool_util_nextchunk(mempool_util_mem2chunk(m));
        mempool_util_inittop(m, mn, (size_t)((tbase + tsize) - (char*)mn) - mempool_util_topfootsize());
        return m;
    }
    return NULL;
}

void mempool_destroypool(void* msp)
{
    MempoolState* ms = (MempoolState*)msp;
    MempoolSegment* sp = &ms->seg;
    while(sp != 0)
    {
        char* base = sp->base;
        size_t size = sp->size;
        sp = sp->next;
        mempool_util_callmunmap(base, size);
    }
}

void* mempool_usermalloc(void* msp, size_t nsize)
{
    MempoolState* ms = (MempoolState*)msp;
    void* mem;
    size_t nb;
    if(nsize <= MEMPOOL_CONST_MAXSMALLREQUEST)
    {
        MempoolBindex idx;
        MempoolBinMap smallbits;
        nb = (nsize < MEMPOOL_REQUESTS_MIN) ? MEMPOOL_MINCHUNKSIZE : mempool_util_padrequest(nsize);
        idx = mempool_util_smallindex(nb);
        smallbits = ms->smallmap >> idx;

        if((smallbits & 0x3) != 0)
        { /* Remainderless fit to a smallbin. */
            MempoolPlainChunk* b;
            MempoolPlainChunk* p;
            idx += ~smallbits & 1; /* Uses next bin if idx empty */
            b = mempool_util_smallbinat(ms, idx);
            p = b->fd;
            mempool_util_unlinkfirstsmallchunk(ms, b, p, idx);
            mempool_util_setinuseandpinuse(ms, p, mempool_util_smallindex2size(idx));
            mem = mempool_util_chunk2mem(p);
            return mem;
        }
        else if(nb > ms->dvsize)
        {
            if(smallbits != 0)
            { /* Use chunk in next nonempty smallbin */
                MempoolPlainChunk* b;
                MempoolPlainChunk* p;
                MempoolPlainChunk* r;
                size_t rsize;
                MempoolBinMap leftbits = (smallbits << idx) & mempool_util_leftbits(mempool_util_idx2bit(idx));
                MempoolBindex i = mempool_util_nativebitscanforward(leftbits);
                b = mempool_util_smallbinat(ms, i);
                p = b->fd;
                mempool_util_unlinkfirstsmallchunk(ms, b, p, i);
                rsize = mempool_util_smallindex2size(i) - nb;
                /* Fit here cannot be remainderless if 4byte sizes */
                if(MEMPOOL_CONST_SIZETSIZE != 4 && rsize < MEMPOOL_MINCHUNKSIZE)
                {
                    mempool_util_setinuseandpinuse(ms, p, mempool_util_smallindex2size(i));
                }
                else
                {
                    mempool_util_setsizeandpinuseofinusechunk(ms, p, nb);
                    r = mempool_util_chunkplusoffset(p, nb);
                    mempool_util_setsizeand_pinuseoffreechunk(r, rsize);
                    mempool_util_replacedv(ms, r, rsize);
                }
                mem = mempool_util_chunk2mem(p);
                return mem;
            }
            else if(ms->treemap != 0 && (mem = mempool_util_tmallocsmall(ms, nb)) != 0)
            {
                return mem;
            }
        }
    }
    else if(nsize >= MEMPOOL_REQUESTS_MAX)
    {
        nb = MEMPOOL_CONST_MAXSIZET; /* Too big to allocate. Force failure (in sys alloc) */
    }
    else
    {
        nb = mempool_util_padrequest(nsize);
        if(ms->treemap != 0 && (mem = mempool_util_tmalloclarge(ms, nb)) != 0)
        {
            return mem;
        }
    }

    if(nb <= ms->dvsize)
    {
        size_t rsize = ms->dvsize - nb;
        MempoolPlainChunk* p = ms->dv;
        if(rsize >= MEMPOOL_MINCHUNKSIZE)
        { /* split dv */
            MempoolPlainChunk* r = ms->dv = mempool_util_chunkplusoffset(p, nb);
            ms->dvsize = rsize;
            mempool_util_setsizeand_pinuseoffreechunk(r, rsize);
            mempool_util_setsizeandpinuseofinusechunk(ms, p, nb);
        }
        else
        { /* exhaust dv */
            size_t dvs = ms->dvsize;
            ms->dvsize = 0;
            ms->dv = 0;
            mempool_util_setinuseandpinuse(ms, p, dvs);
        }
        mem = mempool_util_chunk2mem(p);
        return mem;
    }
    else if(nb < ms->topsize)
    { /* Split top */
        size_t rsize = ms->topsize -= nb;
        MempoolPlainChunk* p = ms->top;
        MempoolPlainChunk* r = ms->top = mempool_util_chunkplusoffset(p, nb);
        r->head = rsize | MEMPOOL_PINUSE_BIT;
        mempool_util_setsizeandpinuseofinusechunk(ms, p, nb);
        mem = mempool_util_chunk2mem(p);
        return mem;
    }
    return mempool_util_allocsys(ms, nb);
}

void* mempool_userfree(void* msp, void* ptr)
{
    size_t psize;
    size_t prevsize;
    size_t tsize;
    size_t dsize;
    size_t nsize;
    MempoolState* fm;
    MempoolTreeChunk* tp;
    MempoolPlainChunk* p;
    MempoolPlainChunk* prev;
    MempoolPlainChunk* next;
    if(ptr != 0)
    {
        p = mempool_util_mem2chunk(ptr);
        fm = (MempoolState*)msp;
        psize = mempool_util_chunksize(p);

        next = mempool_util_chunkplusoffset(p, psize);
        if(!mempool_util_pinuse(p))
        {
            prevsize = p->prev_foot;
            if((prevsize & MEMPOOL_ISDIRECTBIT) != 0)
            {
                prevsize &= ~MEMPOOL_ISDIRECTBIT;
                psize += prevsize + MEMPOOL_DIRECTFOOTPAD;
                mempool_util_callmunmap((char*)p - prevsize, psize);
                return NULL;
            }
            else
            {
                prev = mempool_util_chunkminusoffset(p, prevsize);
                psize += prevsize;
                p = prev;
                /* consolidate backward */
                if(p != fm->dv)
                {
                    mempool_util_unlinkchunk(fm, p, prevsize);
                }
                else if((next->head & MEMPOOL_INUSE_BITS) == MEMPOOL_INUSE_BITS)
                {
                    fm->dvsize = psize;
                    mempool_util_setfreewithpinuse(p, psize, next);
                    return NULL;
                }
            }
        }
        if(!mempool_util_cinuse(next))
        { /* consolidate forward */
            if(next == fm->top)
            {
                tsize = fm->topsize += psize;
                fm->top = p;
                p->head = tsize | MEMPOOL_PINUSE_BIT;
                if(p == fm->dv)
                {
                    fm->dv = 0;
                    fm->dvsize = 0;
                }
                if(tsize > fm->trim_check)
                    mempool_util_alloctrim(fm, 0);
                return NULL;
            }
            else if(next == fm->dv)
            {
                dsize = fm->dvsize += psize;
                fm->dv = p;
                mempool_util_setsizeand_pinuseoffreechunk(p, dsize);
                return NULL;
            }
            else
            {
                nsize = mempool_util_chunksize(next);
                psize += nsize;
                mempool_util_unlinkchunk(fm, next, nsize);
                mempool_util_setsizeand_pinuseoffreechunk(p, psize);
                if(p == fm->dv)
                {
                    fm->dvsize = psize;
                    return NULL;
                }
            }
        }
        else
        {
            mempool_util_setfreewithpinuse(p, psize, next);
        }

        if(mempool_util_issmall(psize))
        {
            mempool_util_insertsmallchunk(fm, p, psize);
        }
        else
        {
            tp = (MempoolTreeChunk*)p;
            mempool_util_insertlargechunk(fm, tp, psize);
            if(--fm->release_checks == 0)
                mempool_util_releaseunusedsegments(fm);
        }
    }
    return NULL;
}

void* mempool_userrealloc(void* msp, void* ptr, size_t nsize)
{
    size_t nb;
    size_t oc;
    size_t rsize;
    size_t oldsize;
    size_t newsize;
    size_t newtopsize;
    void* newmem;
    MempoolState* m;
    MempoolPlainChunk* oldp;
    MempoolPlainChunk* next;
    MempoolPlainChunk* newp;
    if(nsize >= MEMPOOL_REQUESTS_MAX)
    {
        return NULL;
    }
    else
    {
        m = (MempoolState*)msp;
        oldp = mempool_util_mem2chunk(ptr);
        oldsize = mempool_util_chunksize(oldp);
        next = mempool_util_chunkplusoffset(oldp, oldsize);
        newp = 0;
        nb = mempool_util_request2size(nsize);

        /* Try to either shrink or extend into top. Else malloc-copy-free */
        if(mempool_util_isdirect(oldp))
        {
            newp = mempool_util_directresize(oldp, nb); /* this may return NULL. */
        }
        else if(oldsize >= nb)
        { /* already big enough */
            rsize = oldsize - nb;
            newp = oldp;
            if(rsize >= MEMPOOL_MINCHUNKSIZE)
            {
                MempoolPlainChunk* rem = mempool_util_chunkplusoffset(newp, nb);
                mempool_util_setinuse(m, newp, nb);
                mempool_util_setinuse(m, rem, rsize);
                mempool_userfree(m, mempool_util_chunk2mem(rem));
            }
        }
        else if(next == m->top && oldsize + m->topsize > nb)
        {
            /* Expand into top */
            newsize = oldsize + m->topsize;
            newtopsize = newsize - nb;
            MempoolPlainChunk* newtop = mempool_util_chunkplusoffset(oldp, nb);
            mempool_util_setinuse(m, oldp, nb);
            newtop->head = newtopsize | MEMPOOL_PINUSE_BIT;
            m->top = newtop;
            m->topsize = newtopsize;
            newp = oldp;
        }

        if(newp != 0)
        {
            return mempool_util_chunk2mem(newp);
        }
        else
        {
            newmem = mempool_usermalloc(m, nsize);
            if(newmem != 0)
            {
                oc = oldsize - mempool_util_overheadfor(oldp);
                memcpy(newmem, ptr, oc < nsize ? oc : nsize);
                mempool_userfree(m, ptr);
            }
            return newmem;
        }
    }
}


#endif
