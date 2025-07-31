/*
** Bundled memory allocator.
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

/* To get the mremap prototype. Must be defined before any system includes. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
    #define _GNU_SOURCE
#endif

/* Target architectures. */
enum
{
    NNALLOC_ARCH_X86 = 1,
    NNALLOC_ARCH_X64 = 2,
    NNALLOC_ARCH_ARM = 3,
};

/* Target OS. */
enum
{
    NNALLOC_OS_WINDOWS = 1,
    NNALLOC_OS_LINUX = 2,
    NNALLOC_OS_OSX = 3,
};

/* Select native target if no target defined. */
#ifndef NNALLOC_TARGET

    #if defined(__i386) || defined(__i386__) || defined(_M_IX86)
        #define NNALLOC_TARGET NNALLOC_ARCH_X86
    #elif defined(__x86_64__) || defined(__x86_64) || defined(_M_X64) || defined(_M_AMD64)
        #define NNALLOC_TARGET NNALLOC_ARCH_X64
    #elif defined(__arm__) || defined(__arm) || defined(__ARM__) || defined(__ARM)
        #define NNALLOC_TARGET NNALLOC_ARCH_ARM
    #else
        #error "No support for this architecture (yet)"
    #endif

#endif

/* Select native OS if no target OS defined. */
#ifndef NNALLOC_OS

    #if defined(_WIN32) && !defined(_XBOX_VER)
        #define NNALLOC_OS NNALLOC_OS_WINDOWS
    #elif defined(__linux__)
        #define NNALLOC_OS NNALLOC_OS_LINUX
    #elif defined(__MACH__) && defined(__APPLE__)
        #define NNALLOC_OS NNALLOC_OS_OSX
    #elif(defined(__sun__) && defined(__svr4__)) || defined(__CYGWIN__)
        #define NNALLOC_OS NNALLOC_OS_POSIX
    #else
        #define NNALLOC_OS NNALLOC_OS_OTHER
    #endif
#endif

enum
{
    NNALLOC_TARGET_WINDOWS = (NNALLOC_OS == NNALLOC_OS_WINDOWS),
    NNALLOC_TARGET_LINUX = (NNALLOC_OS == NNALLOC_OS_LINUX),
    NNALLOC_TARGET_OSX = (NNALLOC_OS == NNALLOC_OS_OSX),
    NNALLOC_TARGET_IOS = (NNALLOC_TARGET_OSX && NNALLOC_TARGET == NNALLOC_ARCH_ARM),
};

#if NNALLOC_TARGET_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <errno.h>
    #include <sys/mman.h>
#endif

#ifdef __CELLOS_LV2__
    #define NNALLOC_TARGET_PS3 1
#endif

#ifdef __ORBIS__
    #define NNALLOC_TARGET_PS4 1
    #undef NULL
    #define NULL ((void*)0)
#endif

/* Set target architecture properties. */
#if NNALLOC_TARGET == NNALLOC_ARCH_X86
    #define NNALLOC_ARCH_BITS 32
#elif NNALLOC_TARGET == NNALLOC_ARCH_X64
    #define NNALLOC_ARCH_BITS 64
#elif NNALLOC_TARGET == NNALLOC_ARCH_ARM
    #define NNALLOC_ARCH_BITS 32
#else
    #error "No target architecture defined"
#endif

#ifndef NNALLOC_PAGESIZE
    #define NNALLOC_PAGESIZE 4096
#endif

#if NNALLOC_ARCH_BITS == 32
    #define NNALLOC_CONST_IS64BIT 0
#else
    #define NNALLOC_CONST_IS64BIT 1
#endif

#include "ljalloc.h"

#if defined(__GNUC__)
    #define nn_util_likely(x) __builtin_expect(!!(x), 1)
    #define nn_util_unlikely(x) __builtin_expect(!!(x), 0)

    #define lj_ffs(x) ((uint32_t)__builtin_ctz(x))
    /* Don't ask ... */
    #if defined(__INTEL_COMPILER) && (defined(__i386__) || defined(__x86_64__))
        static uint32_t lj_fls(uint32_t x)
        {
            uint32_t r;
            __asm__("bsrl %1, %0" : "=r"(r) : "rm"(x) : "cc");
            return r;
        }
    #else
        #define lj_fls(x) ((uint32_t)(__builtin_clz(x) ^ 31))
    #endif
#elif defined(_MSC_VER)
    #ifdef _M_PPC
        unsigned int _CountLeadingZeros(long);
        #pragma intrinsic(_CountLeadingZeros)
        static uint32_t lj_fls(uint32_t x)
        {
            return _CountLeadingZeros(x) ^ 31;
        }
    #else
        unsigned char _BitScanForward(uint32_t*, unsigned long);
        unsigned char _BitScanReverse(uint32_t*, unsigned long);
        #pragma intrinsic(_BitScanForward)
        #pragma intrinsic(_BitScanReverse)

        static uint32_t lj_ffs(uint32_t x)
        {
            uint32_t r;
            _BitScanForward(&r, x);
            return r;
        }

        static uint32_t lj_fls(uint32_t x)
        {
            uint32_t r;
            _BitScanReverse(&r, x);
            return r;
        }
    #endif
#else
    #define nn_util_likely(x) x
    #define nn_util_unlikely(x) x

    #if 1
        #define lj_ffs(x) (ffs(x) - 1)
    #else
        static uint32_t lj_ffs(uint32_t x)
        {
            uint32_t r = 32;
            if (!x)
            {
                return 0;
            }
            if (!(x & 0xffff0000u))
            {
                x <<= 16;
                r -= 16;
            }
            if (!(x & 0xff000000u))
            {
                x <<= 8;
                r -= 8;
            }
            if (!(x & 0xf0000000u))
            {
                x <<= 4;
                r -= 4;
            }
            if (!(x & 0xc0000000u))
            {
                x <<= 2;
                r -= 2;
            }
            if (!(x & 0x80000000u))
            {
                x <<= 1;
                r -= 1;
            }
            return r;
        }
    #endif
    static uint32_t lj_fls(uint32_t x)
    {
        uint32_t r = 32;
        if (!x)
        {
            return 0;
        }
        if (!(x & 0xffff0000u))
        {
            x <<= 16;
            r -= 16;
        }
        if (!(x & 0xff000000u))
        {
            x <<= 8;
            r -= 8;
        }
        if (!(x & 0xf0000000u))
        {
            x <<= 4;
            r -= 4;
        }
        if (!(x & 0xc0000000u))
        {
            x <<= 2;
            r -= 2;
        }
        if (!(x & 0x80000000u))
        {
            x <<= 1;
            r -= 1;
        }
        return r;
    }
#endif

/* Optional defines. */
#ifndef nn_util_likely
    #define nn_util_likely(x) (x)
    #define nn_util_unlikely(x) (x)
#endif

/* The bit mask value corresponding to NNALLOC_CONST_MALLOCALIGNMENT */
#define CHUNK_ALIGN_MASK (NNALLOC_CONST_MALLOCALIGNMENT - NNALLOC_CONST_SIZETONE)

/* the number of bytes to offset an address to align it */
#define align_offset(A)                            \
    ((((size_t)(A) & CHUNK_ALIGN_MASK) == 0) ? 0 : \
                                               ((NNALLOC_CONST_MALLOCALIGNMENT - ((size_t)(A) & CHUNK_ALIGN_MASK)) & CHUNK_ALIGN_MASK))

/* -------------------------- MMAP support ------------------------------- */

#define MFAIL ((void*)(NNALLOC_CONST_MAXSIZET))
#define CMFAIL ((char*)(MFAIL)) /* defined for convenience */

#define IS_DIRECT_BIT (NNALLOC_CONST_SIZETONE)

#if NNALLOC_TARGET_WINDOWS
    #if NNALLOC_CONST_IS64BIT

        /* Undocumented, but hey, that's what we all love so much about Windows. */
        typedef long (*PNTAVM)(HANDLE handle, void** addr, ULONG zbits, size_t* size, ULONG alloctype, ULONG prot);
        static PNTAVM ntavm;

        /* Number of top bits of the lower 32 bits of an address that must be zero.
        ** Apparently 0 gives us full 64 bit addresses and 1 gives us the lower 2GB.
        */
        #define NTAVM_ZEROBITS 1

        static void INIT_MMAP(void)
        {
            ntavm = (PNTAVM)GetProcAddress(GetModuleHandleA("ntdll.dll"),
                                           "NtAllocateVirtualMemory");
        }

        /* Win64 32 bit MMAP via NtAllocateVirtualMemory. */
        static void* CALL_MMAP(size_t size)
        {
            DWORD olderr = GetLastError();
            void* ptr = NULL;
            long st = ntavm(INVALID_HANDLE_VALUE, &ptr, NTAVM_ZEROBITS, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            SetLastError(olderr);
            return st == 0 ? ptr : MFAIL;
        }

        /* For direct MMAP, use MEM_TOP_DOWN to minimize interference */
        static void* DIRECT_MMAP(size_t size)
        {
            long st;
            DWORD olderr;
            void* ptr;
            olderr = GetLastError();
            ptr = NULL;
            st = ntavm(INVALID_HANDLE_VALUE, &ptr, NTAVM_ZEROBITS, &size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE);
            SetLastError(olderr);
            return st == 0 ? ptr : MFAIL;
        }
    #else
        #define INIT_MMAP() ((void)0)

        /* Win32 MMAP via VirtualAlloc */
        static void* CALL_MMAP(size_t size)
        {
            DWORD olderr = GetLastError();
            void* ptr = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            SetLastError(olderr);
            return ptr ? ptr : MFAIL;
        }

        /* For direct MMAP, use MEM_TOP_DOWN to minimize interference */
        static void* DIRECT_MMAP(size_t size)
        {
            DWORD olderr = GetLastError();
            void* ptr = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN,
                                     PAGE_READWRITE);
            SetLastError(olderr);
            return ptr ? ptr : MFAIL;
        }
    #endif

    /* This function supports releasing coalesed segments */
    static int CALL_MUNMAP(void* ptr, size_t size)
    {
        DWORD olderr = GetLastError();
        MEMORY_BASIC_INFORMATION minfo;
        char* cptr = (char*)ptr;
        while(size)
        {
            if(VirtualQuery(cptr, &minfo, sizeof(minfo)) == 0)
                return -1;
            if(minfo.BaseAddress != cptr || minfo.AllocationBase != cptr || minfo.State != MEM_COMMIT || minfo.RegionSize > size)
                return -1;
            if(VirtualFree(cptr, 0, MEM_RELEASE) == 0)
                return -1;
            cptr += minfo.RegionSize;
            size -= minfo.RegionSize;
        }
        SetLastError(olderr);
        return 0;
    }
#else
    #define MMAP_PROT (PROT_READ | PROT_WRITE)
    #if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
        #define MAP_ANONYMOUS MAP_ANON
    #endif
    #define MMAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS)

    #if NNALLOC_CONST_IS64BIT
    /* 64 bit mode needs special support for allocating memory in the lower 2GB. */

        #if defined(MAP_32BIT)

            /* Actually this only gives us max. 1GB in current Linux kernels. */
            static void* CALL_MMAP(size_t size)
            {
                int olderr = errno;
                void* ptr = mmap(NULL, size, MMAP_PROT, MAP_32BIT | MMAP_FLAGS, -1, 0);
                errno = olderr;
                return ptr;
            }

        #elif NNALLOC_TARGET_OSX || NNALLOC_TARGET_PS4 || defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__sun__)

            /* OSX and FreeBSD mmap() use a naive first-fit linear search.
            ** That's perfect for us. Except that -pagezero_size must be set for OSX,
            ** otherwise the lower 4GB are blocked. And the 32GB RLIMIT_DATA needs
            ** to be reduced to 250MB on FreeBSD.
            */
            #if NNALLOC_TARGET_OSX
                #define MMAP_REGION_START ((uintptr_t)0x10000)
            #elif NNALLOC_TARGET_PS4
                #define MMAP_REGION_START ((uintptr_t)0x4000)
            #else
                #define MMAP_REGION_START ((uintptr_t)0x10000000)
            #endif
            #define MMAP_REGION_END ((uintptr_t)0x80000000)

            #if(defined(__FreeBSD__) || defined(__FreeBSD_kernel__)) && !NNALLOC_TARGET_PS4
                #include <sys/resource.h>
            #endif

            static void* CALL_MMAP(size_t size)
            {
                int olderr;
                int retry;
                static uintptr_t alloc_hint = MMAP_REGION_START;
                olderr = errno;
                /* Hint for next allocation. Doesn't need to be thread-safe. */
                retry = 0;
                #if(defined(__FreeBSD__) || defined(__FreeBSD_kernel__)) && !NNALLOC_TARGET_PS4
                    static int rlimit_modified = 0;
                    if(nn_util_unlikely(rlimit_modified == 0))
                    {
                        struct rlimit rlim;
                        rlim.rlim_cur = rlim.rlim_max = MMAP_REGION_START;
                        setrlimit(RLIMIT_DATA, &rlim); /* Ignore result. May fail below. */
                        rlimit_modified = 1;
                    }
                #endif
                for(;;)
                {
                    void* p = mmap((void*)alloc_hint, size, MMAP_PROT, MMAP_FLAGS, -1, 0);
                    if((uintptr_t)p >= MMAP_REGION_START && (uintptr_t)p + size < MMAP_REGION_END)
                    {
                        alloc_hint = (uintptr_t)p + size;
                        errno = olderr;
                        return p;
                    }
                    if(p != CMFAIL)
                    {
                        munmap(p, size);
                    }
                    #ifdef __sun__
                        alloc_hint += 0x1000000; /* Need near-exhaustive linear scan. */
                        if(alloc_hint + size < MMAP_REGION_END)
                        {
                            continue;
                        }
                    #endif
                    if(retry)
                    {
                        break;
                    }
                    retry = 1;
                    alloc_hint = MMAP_REGION_START;
                }
                errno = olderr;
                return CMFAIL;
            }
        #else
            #error "NYI: need an equivalent of MAP_32BIT for this 64 bit OS"
        #endif
    #else
        /* 32 bit mode is easy. */
        static void* CALL_MMAP(size_t size)
        {
            int olderr = errno;
            void* ptr = mmap(NULL, size, MMAP_PROT, MMAP_FLAGS, -1, 0);
            errno = olderr;
            return ptr;
        }
    #endif
    #define INIT_MMAP() ((void)0)
    #define DIRECT_MMAP(s) CALL_MMAP(s)

    static int CALL_MUNMAP(void* ptr, size_t size)
    {
        int olderr = errno;
        int ret = munmap(ptr, size);
        errno = olderr;
        return ret;
    }

    #if NNALLOC_TARGET_LINUX
        /* Need to define _GNU_SOURCE to get the mremap prototype. */
        static void* CALL_MREMAP_(void* ptr, size_t osz, size_t nsz, int flags)
        {
            int olderr = errno;
            ptr = mremap(ptr, osz, nsz, flags);
            errno = olderr;
            return ptr;
        }

        #define CALL_MREMAP(addr, osz, nsz, mv) CALL_MREMAP_((addr), (osz), (nsz), (mv))
        #define CALL_MREMAP_NOMOVE 0
        /* #define CALL_MREMAP_MAYMOVE 1 */
        #if NNALLOC_CONST_IS64BIT
            #define CALL_MREMAP_MV CALL_MREMAP_NOMOVE
        #else
            #define CALL_MREMAP_MV CALL_MREMAP_MAYMOVE
        #endif
    #endif
#endif

#ifndef CALL_MREMAP
    #define CALL_MREMAP(addr, osz, nsz, mv) ((void)osz, MFAIL)
#endif

/* ------------------- Chunks sizes and alignments ----------------------- */

#define MCHUNK_SIZE (sizeof(nnallocatorplainchunk_t))

#define CHUNK_OVERHEAD (NNALLOC_CONST_SIZETSIZE)

/* Direct chunks need a second word of overhead ... */
#define DIRECT_CHUNK_OVERHEAD (NNALLOC_CONST_TWOSIZETSIZES)
/* ... and additional padding for fake next-chunk at foot */
#define DIRECT_FOOT_PAD (NNALLOC_CONST_FOURSIZETSIZES)

/* The smallest size we can malloc is an aligned minimal chunk */
#define MIN_CHUNK_SIZE \
    ((MCHUNK_SIZE + CHUNK_ALIGN_MASK) & ~CHUNK_ALIGN_MASK)

/* conversion from malloc headers to user pointers, and back */
#define chunk2mem(p) ((void*)((char*)(p) + NNALLOC_CONST_TWOSIZETSIZES))
#define mem2chunk(mem) ((nnallocatorplainchunk_t*)((char*)(mem) - NNALLOC_CONST_TWOSIZETSIZES))
/* chunk associated with aligned address A */
#define align_as_chunk(A) (nnallocatorplainchunk_t*)((A) + align_offset(chunk2mem(A)))

/* Bounds on request (not chunk) sizes. */
#define MAX_REQUEST ((~MIN_CHUNK_SIZE + 1) << 2)
#define MIN_REQUEST (MIN_CHUNK_SIZE - CHUNK_OVERHEAD - NNALLOC_CONST_SIZETONE)

/* pad request bytes into a usable size */
#define pad_request(req) \
    (((req) + CHUNK_OVERHEAD + CHUNK_ALIGN_MASK) & ~CHUNK_ALIGN_MASK)

/* pad request, checking for minimum (but not maximum) */
#define request2size(req) \
    (((req) < MIN_REQUEST) ? MIN_CHUNK_SIZE : pad_request(req))

/* ------------------ Operations on head and foot fields ----------------- */

#define PINUSE_BIT (NNALLOC_CONST_SIZETONE)
#define CINUSE_BIT (NNALLOC_CONST_SIZETTWO)
#define INUSE_BITS (PINUSE_BIT | CINUSE_BIT)

/* Head value for fenceposts */
#define FENCEPOST_HEAD (INUSE_BITS | NNALLOC_CONST_SIZETSIZE)

/* extraction of fields from head words */
#define cinuse(p) ((p)->head & CINUSE_BIT)
#define pinuse(p) ((p)->head & PINUSE_BIT)
#define chunksize(p) ((p)->head & ~(INUSE_BITS))

#define clear_pinuse(p) ((p)->head &= ~PINUSE_BIT)

/* Treat space at ptr +/- offset as a chunk */
#define chunk_plus_offset(p, s) ((nnallocatorplainchunk_t*)(((char*)(p)) + (s)))
#define chunk_minus_offset(p, s) ((nnallocatorplainchunk_t*)(((char*)(p)) - (s)))

/* Ptr to next or previous physical nnallocatorplainchunk_t. */
#define next_chunk(p) ((nnallocatorplainchunk_t*)(((char*)(p)) + ((p)->head & ~INUSE_BITS)))

/* Get/set size at footer */
#define set_foot(p, s) (((nnallocatorplainchunk_t*)((char*)(p) + (s)))->prev_foot = (s))

/* Set size, pinuse bit, and foot */
#define set_size_and_pinuse_of_free_chunk(p, s) \
    ((p)->head = (s | PINUSE_BIT), set_foot(p, s))

/* Set size, pinuse bit, foot, and clear next pinuse */
#define set_free_with_pinuse(p, s, n) \
    (clear_pinuse(n), set_size_and_pinuse_of_free_chunk(p, s))

#define is_direct(p) \
    (!((p)->head & PINUSE_BIT) && ((p)->prev_foot & IS_DIRECT_BIT))

/* Get the internal overhead associated with chunk p */
#define overhead_for(p) \
    (is_direct(p) ? DIRECT_CHUNK_OVERHEAD : CHUNK_OVERHEAD)

/* ---------------------- Overlaid data structures ----------------------- */

/* A little helper macro for trees */
#define nn_allocator_leftmostchild(t) ((t)->child[0] != 0 ? (t)->child[0] : (t)->child[1])

#define nn_allocator_isinitialized(mst) ((mst)->top != 0)

/* -------------------------- system alloc setup ------------------------- */

/* page-align a size */
#define nn_allocator_pagealign(sz) \
    (((sz) + (NNALLOC_PAGESIZE - NNALLOC_CONST_SIZETONE)) & ~(NNALLOC_PAGESIZE - NNALLOC_CONST_SIZETONE))

/* granularity-align a size */
#define nn_allocator_granularityalign(sz)                    \
    (((sz) + (NNALLOC_CONST_DEFAULTGRANULARITY - NNALLOC_CONST_SIZETONE)) & ~(NNALLOC_CONST_DEFAULTGRANULARITY - NNALLOC_CONST_SIZETONE))

#if NNALLOC_TARGET_WINDOWS
    #define mmap_align(sz) nn_allocator_granularityalign(sz)
#else
    #define mmap_align(sz) nn_allocator_pagealign(sz)
#endif

/*  True if segment segm holds address A */
#define nn_allocator_segmentholds(segm, A) \
    ((char*)(A) >= segm->base && (char*)(A) < segm->base + segm->size)

/* Return segment holding given address */
static nnallocatorsegment_t* nn_allocator_segmentholding(nnallocatorstate_t* m, char* addr)
{
    nnallocatorsegment_t* sp = &m->seg;
    for(;;)
    {
        if(addr >= sp->base && addr < sp->base + sp->size)
            return sp;
        if((sp = sp->next) == 0)
            return 0;
    }
}

/* Return true if segment contains a segment link */
static int nn_allocator_hassegmentlink(nnallocatorstate_t* m, nnallocatorsegment_t* ss)
{
    nnallocatorsegment_t* sp = &m->seg;
    for(;;)
    {
        if((char*)sp >= ss->base && (char*)sp < ss->base + ss->size)
            return 1;
        if((sp = sp->next) == 0)
            return 0;
    }
}

/*
  nn_allocator_topfootsize is padding at the end of a segment, including space
  that may be needed to place segment records and fenceposts when new
  noncontiguous segments are added.
*/
#define nn_allocator_topfootsize() \
    (align_offset(chunk2mem(0)) + pad_request(sizeof(nnallocatorsegment_t)) + MIN_CHUNK_SIZE)

/* ---------------------------- Indexing Bins ---------------------------- */

#define nn_allocator_issmall(s) (((s) >> NNALLOC_CONST_SMALLBINSHIFT) < NNALLOC_CONST_NSMALLBINS)
#define nn_allocator_smallindex(s) ((s) >> NNALLOC_CONST_SMALLBINSHIFT)
#define nn_allocator_smallindex2size(i) ((i) << NNALLOC_CONST_SMALLBINSHIFT)

/* addressing by index. See above about smallbin repositioning */
#define nn_allocator_smallbinat(mst, i) ((nnallocatorplainchunk_t*)((char*)&((mst)->smallbins[(i) << 1])))
#define nn_allocator_treebinat(mst, i) (&((mst)->treebins[i]))

/* assign tree index for size sz to variable I */
#define nn_allocator_computetreeindex(sz, I)                                               \
    {                                                                          \
        unsigned int X = (unsigned int)(sz >> NNALLOC_CONST_TREEBINSHIFT);                   \
        if(X == 0)                                                             \
        {                                                                      \
            I = 0;                                                             \
        }                                                                      \
        else if(X > 0xFFFF)                                                    \
        {                                                                      \
            I = NNALLOC_CONST_NTREEBINS - 1;                                                 \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            unsigned int K = lj_fls(X);                                        \
            I = (bindex_t)((K << 1) + ((sz >> (K + (NNALLOC_CONST_TREEBINSHIFT - 1)) & 1))); \
        }                                                                      \
    }

/* Shift placing maximum resolved bit in a treebin at i as sign bit */
#define nn_allocator_leftshiftfortreeindex(i) \
    ((i == NNALLOC_CONST_NTREEBINS - 1) ? 0 :     \
                            ((NNALLOC_CONST_SIZETBITSIZE - NNALLOC_CONST_SIZETONE) - (((i) >> 1) + NNALLOC_CONST_TREEBINSHIFT - 2)))

/* ------------------------ Operations on bin maps ----------------------- */

/* bit corresponding to given index */
#define nn_allocator_idx2bit(i) ((binmap_t)(1) << (i))

/* Mark/Clear bits with given index */
#define nn_allocator_marksmallmap(mst, i) ((mst)->smallmap |= nn_allocator_idx2bit(i))
#define nn_allocator_clearsmallmap(mst, i) ((mst)->smallmap &= ~nn_allocator_idx2bit(i))
#define nn_allocator_smallmapismarked(mst, i) ((mst)->smallmap & nn_allocator_idx2bit(i))

#define nn_allocator_marktreemap(mst, i) ((mst)->treemap |= nn_allocator_idx2bit(i))
#define nn_allocator_cleartreemap(mst, i) ((mst)->treemap &= ~nn_allocator_idx2bit(i))
#define nn_allocator_treemapismarked(mst, i) ((mst)->treemap & nn_allocator_idx2bit(i))

/* mask with all bits to left of least bit of x on */
#define left_bits(x) ((x << 1) | (~(x << 1) + 1))

/* Set cinuse bit and pinuse bit of next chunk */
#define nn_allocator_setinuse(mst, p, s)                                    \
    ((p)->head = (((p)->head & PINUSE_BIT) | s | CINUSE_BIT), \
     ((nnallocatorplainchunk_t*)(((char*)(p)) + (s)))->head |= PINUSE_BIT)

/* Set cinuse and pinuse of this chunk and pinuse of next chunk */
#define nn_allocator_setinuseandpinuse(mst, p, s)           \
    ((p)->head = (s | PINUSE_BIT | CINUSE_BIT), \
     ((nnallocatorplainchunk_t*)(((char*)(p)) + (s)))->head |= PINUSE_BIT)

/* Set size, cinuse and pinuse bit of this chunk */
#define nn_allocator_setsizeandpinuseofinusechunk(mst, p, s) \
    ((p)->head = (s | PINUSE_BIT | CINUSE_BIT))

/* ----------------------- Operations on smallbins ----------------------- */

/* Link a free chunk into a smallbin  */
#define nn_allocator_insertsmallchunk(mst, P, sz)      \
    {                                    \
        bindex_t I = nn_allocator_smallindex(sz);     \
        nnallocatorplainchunk_t* B = nn_allocator_smallbinat(mst, I); \
        nnallocatorplainchunk_t* F = B;                 \
        if(!nn_allocator_smallmapismarked(mst, I))    \
        { \
            nn_allocator_marksmallmap(mst, I);         \
        } \
        else \
        { \
            F = B->fd;                   \
        } \
        B->fd = P;                       \
        F->bk = P;                       \
        P->fd = F;                       \
        P->bk = B;                       \
    }

/* Unlink a chunk from a smallbin  */
#define nn_allocator_unlinksmallchunk(mst, P, sz)  \
    {                                \
        nnallocatorplainchunk_t* F = P->fd;         \
        nnallocatorplainchunk_t* B = P->bk;         \
        bindex_t I = nn_allocator_smallindex(sz); \
        if(F == B)                   \
        {                            \
            nn_allocator_clearsmallmap(mst, I);    \
        }                            \
        else                         \
        {                            \
            F->bk = B;               \
            B->fd = F;               \
        }                            \
    }

/* Unlink the first chunk from a smallbin */
#define nn_allocator_unlinkfirstsmallchunk(mst, B, P, I) \
    {                                        \
        nnallocatorplainchunk_t* F = P->fd;                 \
        if(B == F)                           \
        {                                    \
            nn_allocator_clearsmallmap(mst, I);            \
        }                                    \
        else                                 \
        {                                    \
            B->fd = F;                       \
            F->bk = B;                       \
        }                                    \
    }

/* Replace dv node, binning the old one */
/* Used only when dvsize known to be small */
#define nn_allocator_replacedv(mst, P, sz)                 \
    {                                       \
        size_t DVS = mst->dvsize;             \
        if(DVS != 0)                        \
        {                                   \
            nnallocatorplainchunk_t* DV = mst->dv;           \
            nn_allocator_insertsmallchunk(mst, DV, DVS); \
        }                                   \
        mst->dvsize = sz;                      \
        mst->dv = P;                          \
    }

/* ------------------------- Operations on trees ------------------------- */

/* Insert chunk into tree */
static void nn_allocator_insertlargechunk(nnallocatorstate_t* mst, nnallocatortreechunk_t* tchunk, size_t psz)
{
    nnallocatortreechunk_t** hp;
    bindex_t i;
    nn_allocator_computetreeindex(psz, i);
    hp = nn_allocator_treebinat(mst, i);
    tchunk->index = i;
    tchunk->child[0] = tchunk->child[1] = 0;
    if(!nn_allocator_treemapismarked(mst, i))
    {
        nn_allocator_marktreemap(mst, i);
        *hp = tchunk;
        tchunk->parent = (nnallocatortreechunk_t*)hp;
        tchunk->fd = tchunk->bk = tchunk;
    }
    else
    {
        nnallocatortreechunk_t* t = *hp;
        size_t k = psz << nn_allocator_leftshiftfortreeindex(i);
        for(;;)
        {
            if(chunksize(t) != psz)
            {
                nnallocatortreechunk_t** cchunk = &(t->child[(k >> (NNALLOC_CONST_SIZETBITSIZE - NNALLOC_CONST_SIZETONE)) & 1]);
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
                nnallocatortreechunk_t* F = t->fd;
                t->fd = F->bk = tchunk;
                tchunk->fd = F;
                tchunk->bk = t;
                tchunk->parent = 0;
                break;
            }
        }
    }
}

static void nn_allocator_unlinklargechunk(nnallocatorstate_t* mst, nnallocatortreechunk_t* tchunk)
{
    nnallocatortreechunk_t* xp = tchunk->parent;
    nnallocatortreechunk_t* r;
    if(tchunk->bk != tchunk)
    {
        nnallocatortreechunk_t* f = tchunk->fd;
        r = tchunk->bk;
        f->bk = r;
        r->fd = f;
    }
    else
    {
        nnallocatortreechunk_t** rp;
        if(((r = *(rp = &(tchunk->child[1]))) != 0) || ((r = *(rp = &(tchunk->child[0]))) != 0))
        {
            nnallocatortreechunk_t** cp;
            while((*(cp = &(r->child[1])) != 0) || (*(cp = &(r->child[0])) != 0))
            {
                r = *(rp = cp);
            }
            *rp = 0;
        }
    }
    if(xp != 0)
    {
        nnallocatortreechunk_t** hp = nn_allocator_treebinat(mst, tchunk->index);
        if(tchunk == *hp)
        {
            if((*hp = r) == 0)
                nn_allocator_cleartreemap(mst, tchunk->index);
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
            nnallocatortreechunk_t* c0;
            nnallocatortreechunk_t* c1;
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

#define nn_allocator_insertchunk(mst, P, psz)          \
    if(nn_allocator_issmall(psz))                    \
    {                                  \
        nn_allocator_insertsmallchunk(mst, P, psz)    \
    }                                  \
    else                               \
    {                                  \
        nnallocatortreechunk_t* TP = (nnallocatortreechunk_t*)(P); \
        nn_allocator_insertlargechunk(mst, TP, psz);  \
    }

#define nn_allocator_unlinkchunk(mst, P, psz)          \
    if(nn_allocator_issmall(psz))                    \
    {                                  \
        nn_allocator_unlinksmallchunk(mst, P, psz)    \
    }                                  \
    else                               \
    {                                  \
        nnallocatortreechunk_t* TP = (nnallocatortreechunk_t*)(P); \
        nn_allocator_unlinklargechunk(mst, TP);     \
    }

/* -----------------------  Direct-mmapping chunks ----------------------- */

static void* nn_allocator_directalloc(size_t nb)
{
    size_t mmsize = mmap_align(nb + NNALLOC_CONST_SIXSIZETSIZES + CHUNK_ALIGN_MASK);
    if(nn_util_likely(mmsize > nb))
    { /* Check for wrap around 0 */
        char* mm = (char*)(DIRECT_MMAP(mmsize));
        if(mm != CMFAIL)
        {
            size_t offset = align_offset(chunk2mem(mm));
            size_t psize = mmsize - offset - DIRECT_FOOT_PAD;
            nnallocatorplainchunk_t* p = (nnallocatorplainchunk_t*)(mm + offset);
            p->prev_foot = offset | IS_DIRECT_BIT;
            p->head = psize | CINUSE_BIT;
            chunk_plus_offset(p, psize)->head = FENCEPOST_HEAD;
            chunk_plus_offset(p, psize + NNALLOC_CONST_SIZETSIZE)->head = 0;
            return chunk2mem(p);
        }
    }
    return NULL;
}

static nnallocatorplainchunk_t* nn_allocator_directresize(nnallocatorplainchunk_t* oldp, size_t nb)
{
    size_t oldsize = chunksize(oldp);
    if(nn_allocator_issmall(nb)) /* Can't shrink direct regions below small size */
        return NULL;
    /* Keep old chunk if big enough but not too big */
    if(oldsize >= nb + NNALLOC_CONST_SIZETSIZE && (oldsize - nb) <= (NNALLOC_CONST_DEFAULTGRANULARITY >> 1))
    {
        return oldp;
    }
    else
    {
        size_t offset = oldp->prev_foot & ~IS_DIRECT_BIT;
        size_t oldmmsize = oldsize + offset + DIRECT_FOOT_PAD;
        size_t newmmsize = mmap_align(nb + NNALLOC_CONST_SIXSIZETSIZES + CHUNK_ALIGN_MASK);
        char* cp = (char*)CALL_MREMAP((char*)oldp - offset,
                                      oldmmsize, newmmsize, CALL_MREMAP_MV);
        if(cp != CMFAIL)
        {
            nnallocatorplainchunk_t* newp = (nnallocatorplainchunk_t*)(cp + offset);
            size_t psize = newmmsize - offset - DIRECT_FOOT_PAD;
            newp->head = psize | CINUSE_BIT;
            chunk_plus_offset(newp, psize)->head = FENCEPOST_HEAD;
            chunk_plus_offset(newp, psize + NNALLOC_CONST_SIZETSIZE)->head = 0;
            return newp;
        }
    }
    return NULL;
}

/* -------------------------- mspace management -------------------------- */

/* Initialize top chunk and its size */
static void nn_allocator_inittop(nnallocatorstate_t* m, nnallocatorplainchunk_t* p, size_t psize)
{
    /* Ensure alignment */
    size_t offset = align_offset(chunk2mem(p));
    p = (nnallocatorplainchunk_t*)((char*)p + offset);
    psize -= offset;

    m->top = p;
    m->topsize = psize;
    p->head = psize | PINUSE_BIT;
    /* set size of fake trailing chunk holding overhead space only once */
    chunk_plus_offset(p, psize)->head = nn_allocator_topfootsize();
    m->trim_check = NNALLOC_CONST_DEFAULTTRIMTHRESHOLD; /* reset on each update */
}

/* Initialize bins for a new state that is otherwise zeroed out */
static void nn_allocator_initbins(nnallocatorstate_t* m)
{
    /* Establish circular links for smallbins */
    bindex_t i;
    for(i = 0; i < NNALLOC_CONST_NSMALLBINS; i++)
    {
        nnallocatorplainchunk_t* bin = nn_allocator_smallbinat(m, i);
        bin->fd = bin->bk = bin;
    }
}

/* Allocate chunk and prepend remainder with chunk in successor base. */
static void* nn_allocator_prependalloc(nnallocatorstate_t* m, char* newbase, char* oldbase, size_t nb)
{
    nnallocatorplainchunk_t* p = align_as_chunk(newbase);
    nnallocatorplainchunk_t* oldfirst = align_as_chunk(oldbase);
    size_t psize = (size_t)((char*)oldfirst - (char*)p);
    nnallocatorplainchunk_t* q = chunk_plus_offset(p, nb);
    size_t qsize = psize - nb;
    nn_allocator_setsizeandpinuseofinusechunk(m, p, nb);

    /* consolidate remainder with first chunk of old base */
    if(oldfirst == m->top)
    {
        size_t tsize = m->topsize += qsize;
        m->top = q;
        q->head = tsize | PINUSE_BIT;
    }
    else if(oldfirst == m->dv)
    {
        size_t dsize = m->dvsize += qsize;
        m->dv = q;
        set_size_and_pinuse_of_free_chunk(q, dsize);
    }
    else
    {
        if(!cinuse(oldfirst))
        {
            size_t nsize = chunksize(oldfirst);
            nn_allocator_unlinkchunk(m, oldfirst, nsize);
            oldfirst = chunk_plus_offset(oldfirst, nsize);
            qsize += nsize;
        }
        set_free_with_pinuse(q, qsize, oldfirst);
        nn_allocator_insertchunk(m, q, qsize);
    }

    return chunk2mem(p);
}

/* Add a segment to hold a new noncontiguous region */
static void nn_allocator_addsegment(nnallocatorstate_t* m, char* tbase, size_t tsize)
{
    /* Determine locations and sizes of segment, fenceposts, old top */
    char* old_top = (char*)m->top;
    nnallocatorsegment_t* oldsp = nn_allocator_segmentholding(m, old_top);
    char* old_end = oldsp->base + oldsp->size;
    size_t ssize = pad_request(sizeof(nnallocatorsegment_t));
    char* rawsp = old_end - (ssize + NNALLOC_CONST_FOURSIZETSIZES + CHUNK_ALIGN_MASK);
    size_t offset = align_offset(chunk2mem(rawsp));
    char* asp = rawsp + offset;
    char* csp = (asp < (old_top + MIN_CHUNK_SIZE)) ? old_top : asp;
    nnallocatorplainchunk_t* sp = (nnallocatorplainchunk_t*)csp;
    nnallocatorsegment_t* ss = (nnallocatorsegment_t*)(chunk2mem(sp));
    nnallocatorplainchunk_t* tnext = chunk_plus_offset(sp, ssize);
    nnallocatorplainchunk_t* p = tnext;

    /* reset top to new space */
    nn_allocator_inittop(m, (nnallocatorplainchunk_t*)tbase, tsize - nn_allocator_topfootsize());

    /* Set up segment record */
    nn_allocator_setsizeandpinuseofinusechunk(m, sp, ssize);
    *ss = m->seg; /* Push current record */
    m->seg.base = tbase;
    m->seg.size = tsize;
    m->seg.next = ss;

    /* Insert trailing fenceposts */
    for(;;)
    {
        nnallocatorplainchunk_t* nextp = chunk_plus_offset(p, NNALLOC_CONST_SIZETSIZE);
        p->head = FENCEPOST_HEAD;
        if((char*)(&(nextp->head)) < old_end)
            p = nextp;
        else
            break;
    }

    /* Insert the rest of old top into a bin as an ordinary free chunk */
    if(csp != old_top)
    {
        nnallocatorplainchunk_t* q = (nnallocatorplainchunk_t*)old_top;
        size_t psize = (size_t)(csp - old_top);
        nnallocatorplainchunk_t* tn = chunk_plus_offset(q, psize);
        set_free_with_pinuse(q, psize, tn);
        nn_allocator_insertchunk(m, q, psize);
    }
}

/* -------------------------- System allocation -------------------------- */

static void* nn_allocator_allocsys(nnallocatorstate_t* m, size_t nb)
{
    char* tbase = CMFAIL;
    size_t tsize = 0;

    /* Directly map large chunks */
    if(nn_util_unlikely(nb >= NNALLOC_CONST_DEFAULTMMAPTHRESHOLD))
    {
        void* mem = nn_allocator_directalloc(nb);
        if(mem != 0)
            return mem;
    }

    {
        size_t req = nb + nn_allocator_topfootsize() + NNALLOC_CONST_SIZETONE;
        size_t rsize = nn_allocator_granularityalign(req);
        if(nn_util_likely(rsize > nb))
        { /* Fail if wraps around zero */
            char* mp = (char*)(CALL_MMAP(rsize));
            if(mp != CMFAIL)
            {
                tbase = mp;
                tsize = rsize;
            }
        }
    }

    if(tbase != CMFAIL)
    {
        nnallocatorsegment_t* sp = &m->seg;
        /* Try to merge with an existing segment */
        while(sp != 0 && tbase != sp->base + sp->size)
            sp = sp->next;
        if(sp != 0 && nn_allocator_segmentholds(sp, m->top))
        { /* append */
            sp->size += tsize;
            nn_allocator_inittop(m, m->top, m->topsize + tsize);
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
                return nn_allocator_prependalloc(m, tbase, oldbase, nb);
            }
            else
            {
                nn_allocator_addsegment(m, tbase, tsize);
            }
        }

        if(nb < m->topsize)
        { /* Allocate from new or extended top space */
            size_t rsize = m->topsize -= nb;
            nnallocatorplainchunk_t* p = m->top;
            nnallocatorplainchunk_t* r = m->top = chunk_plus_offset(p, nb);
            r->head = rsize | PINUSE_BIT;
            nn_allocator_setsizeandpinuseofinusechunk(m, p, nb);
            return chunk2mem(p);
        }
    }

    return NULL;
}

/* -----------------------  system deallocation -------------------------- */

/* Unmap and unlink any mmapped segments that don't contain used chunks */
static size_t nn_allocator_releaseunusedsegments(nnallocatorstate_t* m)
{
    size_t released = 0;
    size_t nsegs = 0;
    nnallocatorsegment_t* pred = &m->seg;
    nnallocatorsegment_t* sp = pred->next;
    while(sp != 0)
    {
        char* base = sp->base;
        size_t size = sp->size;
        nnallocatorsegment_t* next = sp->next;
        nsegs++;
        {
            nnallocatorplainchunk_t* p = align_as_chunk(base);
            size_t psize = chunksize(p);
            /* Can unmap if first chunk holds entire segment and not pinned */
            if(!cinuse(p) && (char*)p + psize >= base + size - nn_allocator_topfootsize())
            {
                nnallocatortreechunk_t* tp = (nnallocatortreechunk_t*)p;
                if(p == m->dv)
                {
                    m->dv = 0;
                    m->dvsize = 0;
                }
                else
                {
                    nn_allocator_unlinklargechunk(m, tp);
                }
                if(CALL_MUNMAP(base, size) == 0)
                {
                    released += size;
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
    /* Reset check counter */
    m->release_checks = nsegs > NNALLOC_CONST_MAXRELEASECHECKRATE ?
                        nsegs :
                        NNALLOC_CONST_MAXRELEASECHECKRATE;
    return released;
}

static int nn_allocator_alloctrim(nnallocatorstate_t* m, size_t pad)
{
    size_t released = 0;
    if(pad < MAX_REQUEST && nn_allocator_isinitialized(m))
    {
        pad += nn_allocator_topfootsize(); /* ensure enough room for segment overhead */

        if(m->topsize > pad)
        {
            /* Shrink top space in granularity-size units, keeping at least one */
            size_t unit = NNALLOC_CONST_DEFAULTGRANULARITY;
            size_t extra = ((m->topsize - pad + (unit - NNALLOC_CONST_SIZETONE)) / unit - NNALLOC_CONST_SIZETONE) * unit;
            nnallocatorsegment_t* sp = nn_allocator_segmentholding(m, (char*)m->top);

            if(sp->size >= extra && !nn_allocator_hassegmentlink(m, sp))
            { /* can't shrink if pinned */
                size_t newsize = sp->size - extra;
                /* Prefer mremap, fall back to munmap */
                if((CALL_MREMAP(sp->base, sp->size, newsize, CALL_MREMAP_NOMOVE) != MFAIL) || (CALL_MUNMAP(sp->base + newsize, extra) == 0))
                {
                    released = extra;
                }
            }

            if(released != 0)
            {
                sp->size -= released;
                nn_allocator_inittop(m, m->top, m->topsize - released);
            }
        }

        /* Unmap any unused mmapped segments */
        released += nn_allocator_releaseunusedsegments(m);

        /* On failure, disable autotrim to avoid repeated failed future calls */
        if(released == 0 && m->topsize > m->trim_check)
            m->trim_check = NNALLOC_CONST_MAXSIZET;
    }

    return (released != 0) ? 1 : 0;
}

/* ---------------------------- malloc support --------------------------- */

/* allocate a large request from the best fitting chunk in a treebin */
static void* nn_allocator_tmalloclarge(nnallocatorstate_t* m, size_t nb)
{
    nnallocatortreechunk_t* v = 0;
    size_t rsize = ~nb + 1; /* Unsigned negation */
    nnallocatortreechunk_t* t;
    bindex_t idx;
    nn_allocator_computetreeindex(nb, idx);

    if((t = *nn_allocator_treebinat(m, idx)) != 0)
    {
        /* Traverse tree for this bin looking for node with size == nb */
        size_t sizebits = nb << nn_allocator_leftshiftfortreeindex(idx);
        nnallocatortreechunk_t* rst = 0; /* The deepest untaken right subtree */
        for(;;)
        {
            nnallocatortreechunk_t* rt;
            size_t trem = chunksize(t) - nb;
            if(trem < rsize)
            {
                v = t;
                if((rsize = trem) == 0)
                    break;
            }
            rt = t->child[1];
            t = t->child[(sizebits >> (NNALLOC_CONST_SIZETBITSIZE - NNALLOC_CONST_SIZETONE)) & 1];
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
        binmap_t leftbits = left_bits(nn_allocator_idx2bit(idx)) & m->treemap;
        if(leftbits != 0)
            t = *nn_allocator_treebinat(m, lj_ffs(leftbits));
    }

    while(t != 0)
    { /* find smallest of tree or subtree */
        size_t trem = chunksize(t) - nb;
        if(trem < rsize)
        {
            rsize = trem;
            v = t;
        }
        t = nn_allocator_leftmostchild(t);
    }

    /*  If dv is a better fit, return NULL so malloc will use it */
    if(v != 0 && rsize < (size_t)(m->dvsize - nb))
    {
        nnallocatorplainchunk_t* r = chunk_plus_offset(v, nb);
        nn_allocator_unlinklargechunk(m, v);
        if(rsize < MIN_CHUNK_SIZE)
        {
            nn_allocator_setinuseandpinuse(m, v, (rsize + nb));
        }
        else
        {
            nn_allocator_setsizeandpinuseofinusechunk(m, v, nb);
            set_size_and_pinuse_of_free_chunk(r, rsize);
            nn_allocator_insertchunk(m, r, rsize);
        }
        return chunk2mem(v);
    }
    return NULL;
}

/* allocate a small request from the best fitting chunk in a treebin */
static void* nn_allocator_tmallocsmall(nnallocatorstate_t* m, size_t nb)
{
    nnallocatortreechunk_t* t;
    nnallocatortreechunk_t* v;
    nnallocatorplainchunk_t* r;
    size_t rsize;
    bindex_t i = lj_ffs(m->treemap);

    v = t = *nn_allocator_treebinat(m, i);
    rsize = chunksize(t) - nb;

    while((t = nn_allocator_leftmostchild(t)) != 0)
    {
        size_t trem = chunksize(t) - nb;
        if(trem < rsize)
        {
            rsize = trem;
            v = t;
        }
    }

    r = chunk_plus_offset(v, nb);
    nn_allocator_unlinklargechunk(m, v);
    if(rsize < MIN_CHUNK_SIZE)
    {
        nn_allocator_setinuseandpinuse(m, v, (rsize + nb));
    }
    else
    {
        nn_allocator_setsizeandpinuseofinusechunk(m, v, nb);
        set_size_and_pinuse_of_free_chunk(r, rsize);
        nn_allocator_replacedv(m, r, rsize);
    }
    return chunk2mem(v);
}

/* ----------------------------------------------------------------------- */

void* nn_allocator_create(void)
{
    size_t tsize = NNALLOC_CONST_DEFAULTGRANULARITY;
    char* tbase;
    INIT_MMAP();
    tbase = (char*)(CALL_MMAP(tsize));
    if(tbase != CMFAIL)
    {
        size_t msize = pad_request(sizeof(nnallocatorstate_t));
        nnallocatorplainchunk_t* mn;
        nnallocatorplainchunk_t* msp = align_as_chunk(tbase);
        nnallocatorstate_t* m = (nnallocatorstate_t*)(chunk2mem(msp));
        memset(m, 0, msize);
        msp->head = (msize | PINUSE_BIT | CINUSE_BIT);
        m->seg.base = tbase;
        m->seg.size = tsize;
        m->release_checks = NNALLOC_CONST_MAXRELEASECHECKRATE;
        nn_allocator_initbins(m);
        mn = next_chunk(mem2chunk(m));
        nn_allocator_inittop(m, mn, (size_t)((tbase + tsize) - (char*)mn) - nn_allocator_topfootsize());
        return m;
    }
    return NULL;
}

void nn_allocator_destroy(void* msp)
{
    nnallocatorstate_t* ms = (nnallocatorstate_t*)msp;
    nnallocatorsegment_t* sp = &ms->seg;
    while(sp != 0)
    {
        char* base = sp->base;
        size_t size = sp->size;
        sp = sp->next;
        CALL_MUNMAP(base, size);
    }
}

void* nn_allocuser_malloc(void* msp, size_t nsize)
{
    nnallocatorstate_t* ms = (nnallocatorstate_t*)msp;
    void* mem;
    size_t nb;
    if(nsize <= NNALLOC_CONST_MAXSMALLREQUEST)
    {
        bindex_t idx;
        binmap_t smallbits;
        nb = (nsize < MIN_REQUEST) ? MIN_CHUNK_SIZE : pad_request(nsize);
        idx = nn_allocator_smallindex(nb);
        smallbits = ms->smallmap >> idx;

        if((smallbits & 0x3U) != 0)
        { /* Remainderless fit to a smallbin. */
            nnallocatorplainchunk_t* b;
            nnallocatorplainchunk_t* p;
            idx += ~smallbits & 1; /* Uses next bin if idx empty */
            b = nn_allocator_smallbinat(ms, idx);
            p = b->fd;
            nn_allocator_unlinkfirstsmallchunk(ms, b, p, idx);
            nn_allocator_setinuseandpinuse(ms, p, nn_allocator_smallindex2size(idx));
            mem = chunk2mem(p);
            return mem;
        }
        else if(nb > ms->dvsize)
        {
            if(smallbits != 0)
            { /* Use chunk in next nonempty smallbin */
                nnallocatorplainchunk_t* b;
                nnallocatorplainchunk_t* p;
                nnallocatorplainchunk_t* r;
                size_t rsize;
                binmap_t leftbits = (smallbits << idx) & left_bits(nn_allocator_idx2bit(idx));
                bindex_t i = lj_ffs(leftbits);
                b = nn_allocator_smallbinat(ms, i);
                p = b->fd;
                nn_allocator_unlinkfirstsmallchunk(ms, b, p, i);
                rsize = nn_allocator_smallindex2size(i) - nb;
                /* Fit here cannot be remainderless if 4byte sizes */
                if(NNALLOC_CONST_SIZETSIZE != 4 && rsize < MIN_CHUNK_SIZE)
                {
                    nn_allocator_setinuseandpinuse(ms, p, nn_allocator_smallindex2size(i));
                }
                else
                {
                    nn_allocator_setsizeandpinuseofinusechunk(ms, p, nb);
                    r = chunk_plus_offset(p, nb);
                    set_size_and_pinuse_of_free_chunk(r, rsize);
                    nn_allocator_replacedv(ms, r, rsize);
                }
                mem = chunk2mem(p);
                return mem;
            }
            else if(ms->treemap != 0 && (mem = nn_allocator_tmallocsmall(ms, nb)) != 0)
            {
                return mem;
            }
        }
    }
    else if(nsize >= MAX_REQUEST)
    {
        nb = NNALLOC_CONST_MAXSIZET; /* Too big to allocate. Force failure (in sys alloc) */
    }
    else
    {
        nb = pad_request(nsize);
        if(ms->treemap != 0 && (mem = nn_allocator_tmalloclarge(ms, nb)) != 0)
        {
            return mem;
        }
    }

    if(nb <= ms->dvsize)
    {
        size_t rsize = ms->dvsize - nb;
        nnallocatorplainchunk_t* p = ms->dv;
        if(rsize >= MIN_CHUNK_SIZE)
        { /* split dv */
            nnallocatorplainchunk_t* r = ms->dv = chunk_plus_offset(p, nb);
            ms->dvsize = rsize;
            set_size_and_pinuse_of_free_chunk(r, rsize);
            nn_allocator_setsizeandpinuseofinusechunk(ms, p, nb);
        }
        else
        { /* exhaust dv */
            size_t dvs = ms->dvsize;
            ms->dvsize = 0;
            ms->dv = 0;
            nn_allocator_setinuseandpinuse(ms, p, dvs);
        }
        mem = chunk2mem(p);
        return mem;
    }
    else if(nb < ms->topsize)
    { /* Split top */
        size_t rsize = ms->topsize -= nb;
        nnallocatorplainchunk_t* p = ms->top;
        nnallocatorplainchunk_t* r = ms->top = chunk_plus_offset(p, nb);
        r->head = rsize | PINUSE_BIT;
        nn_allocator_setsizeandpinuseofinusechunk(ms, p, nb);
        mem = chunk2mem(p);
        return mem;
    }
    return nn_allocator_allocsys(ms, nb);
}

void* nn_allocuser_free(void* msp, void* ptr)
{
    if(ptr != 0)
    {
        nnallocatorplainchunk_t* p = mem2chunk(ptr);
        nnallocatorstate_t* fm = (nnallocatorstate_t*)msp;
        size_t psize = chunksize(p);
        nnallocatorplainchunk_t* next = chunk_plus_offset(p, psize);
        if(!pinuse(p))
        {
            size_t prevsize = p->prev_foot;
            if((prevsize & IS_DIRECT_BIT) != 0)
            {
                prevsize &= ~IS_DIRECT_BIT;
                psize += prevsize + DIRECT_FOOT_PAD;
                CALL_MUNMAP((char*)p - prevsize, psize);
                return NULL;
            }
            else
            {
                nnallocatorplainchunk_t* prev = chunk_minus_offset(p, prevsize);
                psize += prevsize;
                p = prev;
                /* consolidate backward */
                if(p != fm->dv)
                {
                    nn_allocator_unlinkchunk(fm, p, prevsize);
                }
                else if((next->head & INUSE_BITS) == INUSE_BITS)
                {
                    fm->dvsize = psize;
                    set_free_with_pinuse(p, psize, next);
                    return NULL;
                }
            }
        }
        if(!cinuse(next))
        { /* consolidate forward */
            if(next == fm->top)
            {
                size_t tsize = fm->topsize += psize;
                fm->top = p;
                p->head = tsize | PINUSE_BIT;
                if(p == fm->dv)
                {
                    fm->dv = 0;
                    fm->dvsize = 0;
                }
                if(tsize > fm->trim_check)
                    nn_allocator_alloctrim(fm, 0);
                return NULL;
            }
            else if(next == fm->dv)
            {
                size_t dsize = fm->dvsize += psize;
                fm->dv = p;
                set_size_and_pinuse_of_free_chunk(p, dsize);
                return NULL;
            }
            else
            {
                size_t nsize = chunksize(next);
                psize += nsize;
                nn_allocator_unlinkchunk(fm, next, nsize);
                set_size_and_pinuse_of_free_chunk(p, psize);
                if(p == fm->dv)
                {
                    fm->dvsize = psize;
                    return NULL;
                }
            }
        }
        else
        {
            set_free_with_pinuse(p, psize, next);
        }

        if(nn_allocator_issmall(psize))
        {
            nn_allocator_insertsmallchunk(fm, p, psize);
        }
        else
        {
            nnallocatortreechunk_t* tp = (nnallocatortreechunk_t*)p;
            nn_allocator_insertlargechunk(fm, tp, psize);
            if(--fm->release_checks == 0)
                nn_allocator_releaseunusedsegments(fm);
        }
    }
    return NULL;
}

void* nn_allocuser_realloc(void* msp, void* ptr, size_t nsize)
{
    if(nsize >= MAX_REQUEST)
    {
        return NULL;
    }
    else
    {
        nnallocatorstate_t* m = (nnallocatorstate_t*)msp;
        nnallocatorplainchunk_t* oldp = mem2chunk(ptr);
        size_t oldsize = chunksize(oldp);
        nnallocatorplainchunk_t* next = chunk_plus_offset(oldp, oldsize);
        nnallocatorplainchunk_t* newp = 0;
        size_t nb = request2size(nsize);

        /* Try to either shrink or extend into top. Else malloc-copy-free */
        if(is_direct(oldp))
        {
            newp = nn_allocator_directresize(oldp, nb); /* this may return NULL. */
        }
        else if(oldsize >= nb)
        { /* already big enough */
            size_t rsize = oldsize - nb;
            newp = oldp;
            if(rsize >= MIN_CHUNK_SIZE)
            {
                nnallocatorplainchunk_t* rem = chunk_plus_offset(newp, nb);
                nn_allocator_setinuse(m, newp, nb);
                nn_allocator_setinuse(m, rem, rsize);
                nn_allocuser_free(m, chunk2mem(rem));
            }
        }
        else if(next == m->top && oldsize + m->topsize > nb)
        {
            /* Expand into top */
            size_t newsize = oldsize + m->topsize;
            size_t newtopsize = newsize - nb;
            nnallocatorplainchunk_t* newtop = chunk_plus_offset(oldp, nb);
            nn_allocator_setinuse(m, oldp, nb);
            newtop->head = newtopsize | PINUSE_BIT;
            m->top = newtop;
            m->topsize = newtopsize;
            newp = oldp;
        }

        if(newp != 0)
        {
            return chunk2mem(newp);
        }
        else
        {
            void* newmem = nn_allocuser_malloc(m, nsize);
            if(newmem != 0)
            {
                size_t oc = oldsize - overhead_for(oldp);
                memcpy(newmem, ptr, oc < nsize ? oc : nsize);
                nn_allocuser_free(m, ptr);
            }
            return newmem;
        }
    }
}


