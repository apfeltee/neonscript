
#if !defined(__NNALLOCATORHEADERFILE_H__)
#define __NNALLOCATORHEADERFILE_H__
/*
  This is a version (aka nn_allocuser_malloc) of malloc/free/realloc written by
  Doug Lea and released to the public domain, as explained at
  http://creativecommons.org/licenses/publicdomain.  Send questions,
  comments, complaints, performance data, etc to dl@cs.oswego.edu

* Version 2.8.3 Thu Sep 22 11:16:15 2005  Doug Lea  (dl at gee)

   Note: There may be an updated version of this malloc obtainable at
           ftp://gee.cs.oswego.edu/pub/misc/malloc.c
         Check before installing!

* Quickstart

  This library is all in one file to simplify the most common usage:
  ftp it, compile it (-O3), and link it into another program. All of
  the compile-time options default to reasonable values for use on
  most platforms.  You might later want to step through various
  compile-time and dynamic tuning options.

  For convenience, an include file for code using this malloc is at:
     ftp://gee.cs.oswego.edu/pub/misc/malloc-2.8.3.h
  You don't really need this .h file unless you call functions not
  defined in your system include files.  The .h file contains only the
  excerpts from this file needed for using this malloc on ANSI C/C++
  systems, so long as you haven't changed compile-time options about
  naming and tuning parameters.  If you do, then you can create your
  own malloc.h that does include all settings by cutting at the point
  indicated below. Note that you may already by default be using a C
  library containing a malloc that is based on some version of this
  malloc (for example in linux). You might still want to use the one
  in this file to customize settings or to avoid overheads associated
  with library versions.

* Vital statistics:

  Supported pointer/size_t representation:       4 or 8 bytes
       size_t MUST be an unsigned type of the same width as
       pointers. (If you are using an ancient system that declares
       size_t as a signed type, or need it to be a different width
       than pointers, you can use a previous release of this malloc
       (e.g. 2.7.2) supporting these.)

  Alignment:                                     8 bytes (default)
       This suffices for nearly all current machines and C compilers.
       However, you can define NNALLOC_CONF_MALLOCALIGNMENT to be wider than this
       if necessary (up to 128bytes), at the expense of using more space.

  Minimum overhead per allocated chunk:   4 or  8 bytes (if 4byte sizes)
                                          8 or 16 bytes (if 8byte sizes)
       Each malloced chunk has a hidden word of overhead holding size
       and status information, and additional cross-check word
       if NNALLOC_CONF_STOREFOOTERS is defined.

  Minimum allocated size: 4-byte ptrs:  16 bytes    (including overhead)
                          8-byte ptrs:  32 bytes    (including overhead)

       Even a request for zero bytes (i.e., malloc(0)) returns a
       pointer to something of the minimum allocatable size.
       The maximum overhead wastage (i.e., number of extra bytes
       allocated than were requested in malloc) is less than or equal
       to the minimum size, except for requests >= mmap_threshold that
       are serviced via mmap(), where the worst case wastage is about
       32 bytes plus the remainder from a system page (the minimal
       mmap unit); typically 4096 or 8192 bytes.

  Security: static-safe; optionally more or less
       The "security" of malloc refers to the ability of malicious
       code to accentuate the effects of errors (for example, freeing
       space that is not currently malloc'ed or overwriting past the
       ends of chunks) in code that calls malloc.  This malloc
       guarantees not to modify any memory locations below the base of
       heap, i.e., static variables, even in the presence of usage
       errors.  The routines additionally detect most improper frees
       and reallocs.  All this holds as long as the static bookkeeping
       for malloc itself is not corrupted by some other means.  This
       is only one aspect of security -- these checks do not, and
       cannot, detect all possible programming errors.

       If NNALLOC_CONF_STOREFOOTERS is defined nonzero, then each allocated chunk
       carries an additional check word to verify that it was malloced
       from its space.  These check words are the same within each
       execution of a program using malloc, but differ across
       executions, so externally crafted fake chunks cannot be
       freed. This improves security by rejecting frees/reallocs that
       could corrupt heap memory, in addition to the checks preventing
       writes to statics that are always on.  This may further improve
       security at the expense of time and space overhead.  (Note that
       NNALLOC_CONF_STOREFOOTERS may also be worth using with NNALLOC_CONF_DEFINEMSPACES.)

       By default detected errors cause the program to abort (calling
       "abort()"). You can override this to instead proceed past
       errors by defining NNALLOC_CONF_PROCEEDONERROR.  In this case, a bad free
       has no effect, and a malloc that encounters a bad address
       caused by user overwrites will ignore the bad address by
       dropping pointers and indices to all known memory. This may
       be appropriate for programs that should continue if at all
       possible in the face of programming errors, although they may
       run out of memory because dropped memory is never reclaimed.

       If you don't like either of these options, you can define
       CORRUPTION_ERROR_ACTION and USAGE_ERROR_ACTION to do anything
       else. And if if you are sure that your program using malloc has
       no errors or vulnerabilities, you can define NNALLOC_CONF_ALLOWINSECURE to 1,
       which might (or might not) provide a small performance improvement.

  Thread-safety: NOT thread-safe unless NNALLOC_CONF_USELOCKS defined
       When NNALLOC_CONF_USELOCKS is defined, each public call to malloc, free,
       etc is surrounded with either a pthread mutex or a win32
       spinlock (depending on NNALLOC_CONF_ISWIN32). This is not especially fast, and
       can be a major bottleneck.  It is designed only to provide
       minimal protection in concurrent environments, and to provide a
       basis for extensions.  If you are using malloc in a concurrent
       program, consider instead using ptmalloc, which is derived from
       a version of this malloc. (See http://www.malloc.de).

  System requirements: Any combination of NNALLOC_CONF_MORECOREFUNCNAME and/or MMAP/MUNMAP
       This malloc can use unix sbrk or any emulation (invoked using
       the CALL_MORECORE macro) and/or mmap/munmap or any emulation
       (invoked using CALL_MMAP/CALL_MUNMAP) to get and release system
       memory.  On most unix systems, it tends to work best if both
       NNALLOC_CONF_MORECOREFUNCNAME and MMAP are enabled.  On Win32, it uses emulations
       based on VirtualAlloc. It also uses common C library functions
       like memset.

  Compliance: I believe it is compliant with the Single Unix Specification
       (See http://www.unix.org). Also SVID/XPG, ANSI C, and probably
       others as well.

* Overview of algorithms

  This is not the fastest, most space-conserving, most portable, or
  most tunable malloc ever written. However it is among the fastest
  while also being among the most space-conserving, portable and
  tunable.  Consistent balance across these factors results in a good
  general-purpose allocator for malloc-intensive programs.

  In most ways, this malloc is a best-fit allocator. Generally, it
  chooses the best-fitting existing chunk for a request, with ties
  broken in approximately least-recently-used order. (This strategy
  normally maintains low fragmentation.) However, for requests less
  than 256bytes, it deviates from best-fit when there is not an
  exactly fitting available chunk by preferring to use space adjacent
  to that used for the previous small request, as well as by breaking
  ties in approximately most-recently-used order. (These enhance
  locality of series of small allocations.)  And for very large requests
  (>= 256Kb by default), it relies on system memory mapping
  facilities, if supported.  (This helps avoid carrying around and
  possibly fragmenting memory used only for large chunks.)

  All operations (except malloc_stats and mallinfo) have execution
  times that are bounded by a constant factor of the number of bits in
  a size_t, not counting any clearing in calloc or copying in realloc,
  or actions surrounding NNALLOC_CONF_MORECOREFUNCNAME and MMAP that have times
  proportional to the number of non-contiguous regions returned by
  system allocation routines, which is often just 1.

  The implementation is not very modular and seriously overuses
  macros. Perhaps someday all C compilers will do as good a job
  inlining modular code as can now be done by brute-force expansion,
  but now, enough of them seem not to.

  Some compilers issue a lot of warnings about code that is
  dead/unreachable only on some platforms, and also about intentional
  uses of negation on unsigned types. All known cases of each can be
  ignored.

  For a longer but out of date high-level description, see
     http://gee.cs.oswego.edu/dl/html/malloc.html

* NNALLOC_CONF_DEFINEMSPACES
  If NNALLOC_CONF_DEFINEMSPACES is defined, then in addition to malloc, free, etc.,
  this file also defines mspace_malloc, mspace_free, etc. These
  are versions of malloc routines that take an "NNAllocMSpace" argument
  obtained using create_mspace, to control all internal bookkeeping.
  If NNALLOC_CONF_ONLYMSPACES is defined, only these versions are compiled.
  So if you would like to use this allocator for only some allocations,
  and your system malloc for others, you can compile with
  NNALLOC_CONF_ONLYMSPACES and then do something like...
    static NNAllocMSpace* mymspace = create_mspace(0,0); // for example
    #define mymalloc(bytes)  mspace_malloc(mymspace, bytes)

  You can similarly create thread-local allocators by storing
  mspaces as thread-locals. For example:
    static __thread NNAllocMSpace* tlms = 0;
    void*  tlmalloc(size_t bytes) {
      if (tlms == 0) tlms = create_mspace(0, 0);
      return mspace_malloc(tlms, bytes);
    }
    void  tlfree(void* mem) { mspace_free(tlms, mem); }

  Unless NNALLOC_CONF_STOREFOOTERS is defined, each NNAllocMSpace is completely independent.
  You cannot allocate from one and free to another (although
  conformance is only weakly checked, so usage errors are not always
  caught). If NNALLOC_CONF_STOREFOOTERS is defined, then each chunk carries around a tag
  indicating its originating NNAllocMSpace, and frees are directed to their
  originating spaces.

 -------------------------  Compile-time options ---------------------------

Be careful in setting #define values for numerical constants of type
size_t. On some systems, literal values are not automatically extended
to size_t precision unless they are explicitly casted.

NNALLOC_CONF_ISWIN32                    default: defined if _WIN32 defined
  Defining NNALLOC_CONF_ISWIN32 sets up defaults for MS environment and compilers.
  Otherwise defaults are for unix.

NNALLOC_CONF_MALLOCALIGNMENT         default: (size_t)8
  Controls the minimum alignment for malloc'ed chunks.  It must be a
  power of two and at least 8, even on machines for which smaller
  alignments would suffice. It may be defined as larger than this
  though. Note however that code and data structures are optimized for
  the case of 8-byte alignment.

NNALLOC_CONF_DEFINEMSPACES                  default: 0 (false)
  If true, compile in support for independent allocation spaces.
  This is only supported if NNALLOC_CONF_HAVEMMAP is true.

NNALLOC_CONF_USELOCKS                default: 0 (false)
  Causes each call to each public routine to be surrounded with
  pthread or NNALLOC_CONF_ISWIN32 mutex lock/unlock. (If set true, this can be
  overridden on a per-NNAllocMSpace basis for NNAllocMSpace versions.)

NNALLOC_CONF_STOREFOOTERS                  default: 0
  If true, provide extra checking and dispatching by placing
  information in the footers of allocated chunks. This adds
  space and time overhead.

NNALLOC_CONF_ALLOWINSECURE                 default: 0
  If true, omit checks for usage errors and heap space overwrites.

NNALLOC_CONF_ABORTCALLBACK                    default: defined as abort()
  Defines how to abort on failed checks.  On most systems, a failed
  check cannot die with an "assert" or even print an informative
  message, because the underlying print routines in turn call malloc,
  which will fail again.  Generally, the best policy is to simply call
  abort(). It's not very useful to do more than this because many
  errors due to overwriting will show up as address faults (null, odd
  addresses etc) rather than malloc-triggered checks, so will also
  abort.  Also, most compilers know that abort() does not return, so
  can better optimize code conditionally calling it.

NNALLOC_CONF_PROCEEDONERROR           default: defined as 0 (false)
  Controls whether detected bad addresses cause them to bypassed
  rather than aborting. If set, detected bad arguments to free and
  realloc are ignored. And all bookkeeping information is zeroed out
  upon a detected overwrite of freed heap space, thus losing the
  ability to ever return it from malloc again, but enabling the
  application to proceed. If NNALLOC_CONF_PROCEEDONERROR is defined, the
  static variable malloc_corruption_error_count is compiled in
  and can be examined to see if errors have occurred. This option
  generates slower code than the default abort policy.

DEBUG                    default: NOT defined
  The DEBUG setting is mainly intended for people trying to modify
  this code or diagnose problems when porting to new platforms.
  However, it may also be able to better isolate user errors than just
  using runtime checks.  The assertions in the check routines spell
  out in more detail the assumptions and invariants underlying the
  algorithms.  The checking is fairly extensive, and will slow down
  execution noticeably. Calling malloc_stats or mallinfo with DEBUG
  set will attempt to check every non-mmapped allocated and free chunk
  in the course of computing the summaries.

NNALLOC_CONF_ABORTONASSERTFAILURE   default: defined as 1 (true)
  Debugging assertion failures can be nearly impossible if your
  version of the assert macro causes malloc to be called, which will
  lead to a cascade of further failures, blowing the runtime stack.
  NNALLOC_CONF_ABORTONASSERTFAILURE cause assertions failures to call abort(),
  which will usually make debugging easier.

NNALLOC_CONF_MALLOCFAILUREACTION     default: sets errno to ENOMEM, or no-op on win32
  The action to take before "return 0" when malloc fails to be able to
  return memory because there is none available.

NNALLOC_CONF_HAVEMORECORE             default: 1 (true) unless win32
  True if this system supports sbrk or an emulation of it.

NNALLOC_CONF_MORECOREFUNCNAME                  default: sbrk
  The name of the sbrk-style system routine to call to obtain more
  memory.  See below for guidance on writing custom NNALLOC_CONF_MORECOREFUNCNAME
  functions. The type of the argument to sbrk/NNALLOC_CONF_MORECOREFUNCNAME varies across
  systems.  It cannot be size_t, because it supports negative
  arguments, so it is normally the signed type of the same width as
  size_t (sometimes declared as "intptr_t").  It doesn't much matter
  though. Internally, we only call it with arguments less than half
  the max value of a size_t, which should work across all reasonable
  possibilities, although sometimes generating compiler warnings.  See
  near the end of this file for guidelines for creating a custom
  version of NNALLOC_CONF_MORECOREFUNCNAME.

NNALLOC_CONF_MORECORECONTIGUOUS       default: 1 (true)
  If true, take advantage of fact that consecutive calls to NNALLOC_CONF_MORECOREFUNCNAME
  with positive arguments always return contiguous increasing
  addresses.  This is true of unix sbrk. It does not hurt too much to
  set it true anyway, since malloc copes with non-contiguities.
  Setting it false when definitely non-contiguous saves time
  and possibly wasted space it would take to discover this though.

MORECORE_CANNOT_TRIM      default: NOT defined
  True if NNALLOC_CONF_MORECOREFUNCNAME cannot release space back to the system when given
  negative arguments. This is generally necessary only if you are
  using a hand-crafted NNALLOC_CONF_MORECOREFUNCNAME function that cannot handle negative
  arguments.

NNALLOC_CONF_HAVEMMAP                 default: 1 (true)
  True if this system supports mmap or an emulation of it.  If so, and
  NNALLOC_CONF_HAVEMORECORE is not true, MMAP is used for all system
  allocation. If set and NNALLOC_CONF_HAVEMORECORE is true as well, MMAP is
  primarily used to directly allocate very large blocks. It is also
  used as a backup strategy in cases where NNALLOC_CONF_MORECOREFUNCNAME fails to provide
  space from system. Note: A single call to MUNMAP is assumed to be
  able to unmap memory that may have be allocated using multiple calls
  to MMAP, so long as they are adjacent.

NNALLOC_CONF_HAVEMREMAP               default: 1 on linux, else 0
  If true realloc() uses mremap() to re-allocate large blocks and
  extend or shrink allocation spaces.

NNALLOC_CONF_MMAPCLEARS               default: 1 on unix
  True if mmap clears memory so calloc doesn't need to. This is true
  for standard unix mmap using /dev/zero.

NNALLOC_CONF_USEBUILTINFFSFUNC            default: 0 (i.e., not used)
  Causes malloc to use the builtin ffs() function to compute indices.
  Some compilers may recognize and intrinsify ffs to be faster than the
  supplied C version. Also, the case of x86 using gcc is special-cased
  to an asm instruction, so is already as fast as it can be, and so
  this setting has no effect. (On most x86s, the asm version is only
  slightly faster than the C version.)

malloc_getpagesize         default: derive from system includes, or 4096.
  The system page size. To the extent possible, this malloc manages
  memory from the system in page-size units.  This may be (and
  usually is) a function rather than a constant. This is ignored
  if NNALLOC_CONF_ISWIN32, where page size is determined using getSystemInfo during
  initialization.

NNALLOC_CONF_USEDEVRANDOM             default: 0 (i.e., not used)
  Causes malloc to use /dev/random to initialize secure magic seed for
  stamping footers. Otherwise, the current time is used.

REALLOC_ZERO_BYTES_FREES    default: not defined
  This should be set if a call to realloc with zero bytes should
  be the same as a call to free. Some people think it should. Otherwise,
  since this malloc returns a unique pointer for malloc(0), so does
  realloc(p, 0).

NNALLOC_CONF_LACKSUNISTDH, NNALLOC_CONF_LACKSFCNTLH, NNALLOC_CONF_LACKSSYSPARAMH, NNALLOC_CONF_LACKSSYSMMANH
NNALLOC_CONF_LACKSSTRINGSH, NNALLOC_CONF_LACKSSTRINGH, NNALLOC_CONF_LACKSSYSTYPESH,  NNALLOC_CONF_LACKSERRNOH
NNALLOC_CONF_LACKSSTDLIBH                default: NOT defined unless on NNALLOC_CONF_ISWIN32
  Define these if your system does not have these header files.
  You might need to manually insert some of the declarations they provide.

NNALLOC_CONF_DEFAULTGRANULARITY        default: page size if NNALLOC_CONF_MORECORECONTIGUOUS,
                                system_info.dwAllocationGranularity in NNALLOC_CONF_ISWIN32,
                                otherwise 64K.
      Also settable using mallopt(M_GRANULARITY, x)
  The unit for allocating and deallocating memory from the system.  On
  most systems with contiguous NNALLOC_CONF_MORECOREFUNCNAME, there is no reason to
  make this more than a page. However, systems with MMAP tend to
  either require or encourage larger granularities.  You can increase
  this value to prevent system allocation functions to be called so
  often, especially if they are slow.  The value must be at least one
  page and must be a power of two.  Setting to 0 causes initialization
  to either page size or win32 region size.  (Note: In previous
  versions of malloc, the equivalent of this option was called
  "TOP_PAD")

NNALLOC_CONF_DEFAULTTRIMTHRESHOLD    default: 2MB
      Also settable using mallopt(M_TRIM_THRESHOLD, x)
  The maximum amount of unused top-most memory to keep before
  releasing via malloc_trim in free().  Automatic trimming is mainly
  useful in long-lived programs using contiguous NNALLOC_CONF_MORECOREFUNCNAME.  Because
  trimming via sbrk can be slow on some systems, and can sometimes be
  wasteful (in cases where programs immediately afterward allocate
  more large chunks) the value should be high enough so that your
  overall system performance would improve by releasing this much
  memory.  As a rough guide, you might set to a value close to the
  average size of a process (program) running on your system.
  Releasing this much memory would allow such a process to run in
  memory.  Generally, it is worth tuning trim thresholds when a
  program undergoes phases where several large chunks are allocated
  and released in ways that can reuse each other's storage, perhaps
  mixed with phases where there are no such chunks at all. The trim
  value must be greater than page size to have any useful effect.  To
  disable trimming completely, you can set to NNALLOC_CONF_MAXSIZET. Note that the trick
  some people use of mallocing a huge space and then freeing it at
  program startup, in an attempt to reserve system memory, doesn't
  have the intended effect under automatic trimming, since that memory
  will immediately be returned to the system.

NNALLOC_CONF_DEFAULTMMAPTHRESHOLD       default: 256K
      Also settable using mallopt(M_MMAP_THRESHOLD, x)
  The request size threshold for using MMAP to directly service a
  request. Requests of at least this size that cannot be allocated
  using already-existing space will be serviced via mmap.  (If enough
  normal freed space already exists it is used instead.)  Using mmap
  segregates relatively large chunks of memory so that they can be
  individually obtained and released from the host system. A request
  serviced through mmap is never reused by any other request (at least
  not directly; the system may just so happen to remap successive
  requests to the same locations).  Segregating space in this way has
  the benefits that: Mmapped space can always be individually released
  back to the system, which helps keep the system level memory demands
  of a long-lived program low.  Also, mapped memory doesn't become
  `locked' between other chunks, as can happen with normally allocated
  chunks, which means that even trimming via malloc_trim would not
  release them.  However, it has the disadvantage that the space
  cannot be reclaimed, consolidated, and then used to service later
  requests, as happens with normal chunks.  The advantages of mmap
  nearly always outweigh disadvantages for "large" chunks, but the
  value of "large" may vary across systems.  The default is an
  empirically derived value that works well in most systems. You can
  disable mmap by setting to NNALLOC_CONF_MAXSIZET.

*/



#ifndef NNALLOC_CONF_ISWIN32
    #ifdef _WIN32
        #define NNALLOC_CONF_ISWIN32 1
    #endif
#endif
#ifdef NNALLOC_CONF_ISWIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #define NNALLOC_CONF_HAVEMMAP 1
    #define NNALLOC_CONF_HAVEMORECORE 0
    #define NNALLOC_CONF_LACKSUNISTDH
    #define NNALLOC_CONF_LACKSSYSPARAMH
    #define NNALLOC_CONF_LACKSSYSMMANH
    #define NNALLOC_CONF_LACKSSTRINGH
    #define NNALLOC_CONF_LACKSSTRINGSH
    #define NNALLOC_CONF_LACKSSYSTYPESH
    #define NNALLOC_CONF_LACKSERRNOH
    #define NNALLOC_CONF_MALLOCFAILUREACTION
    /* WINCE and some others apparently don't clear */
    #define NNALLOC_CONF_MMAPCLEARS 0
#elif !defined _GNU_SOURCE
    /* mremap() on Linux requires this via sys/mman.h
     * See roundup issue 10309
     */
    #define _GNU_SOURCE 1
#endif

#if defined(DARWIN) || defined(_DARWIN)
    /* Mac OSX docs advise not to use sbrk; it seems better to use mmap */
    #ifndef NNALLOC_CONF_HAVEMORECORE
        #define NNALLOC_CONF_HAVEMORECORE 0
        #define NNALLOC_CONF_HAVEMMAP 1
    #endif
#endif

#ifndef NNALLOC_CONF_LACKSSYSTYPESH
    /* For size_t */
    #include <sys/types.h>
#endif

/* The maximum possible size_t value has all bits set */
#define NNALLOC_CONF_MAXSIZET (~(size_t)0)

#ifndef NNALLOC_CONF_PERFORMCHECKS
    #define NNALLOC_CONF_PERFORMCHECKS 0
#endif

#ifndef NNALLOC_CONF_DEFINEMSPACES
        #define NNALLOC_CONF_DEFINEMSPACES 0
#endif

#ifndef NNALLOC_CONF_MALLOCALIGNMENT
    #define NNALLOC_CONF_MALLOCALIGNMENT ((size_t)8U)
#endif

#ifndef NNALLOC_CONF_STOREFOOTERS
    #define NNALLOC_CONF_STOREFOOTERS 0
#endif

#ifndef NNALLOC_CONF_ABORTCALLBACK
    #define NNALLOC_CONF_ABORTCALLBACK abort()
#endif

#ifndef NNALLOC_CONF_ABORTONASSERTFAILURE
    #define NNALLOC_CONF_ABORTONASSERTFAILURE 1
#endif

#ifndef NNALLOC_CONF_PROCEEDONERROR
    #define NNALLOC_CONF_PROCEEDONERROR 0
#endif

#ifndef NNALLOC_CONF_USELOCKS
    #define NNALLOC_CONF_USELOCKS 1
#endif

#ifndef NNALLOC_CONF_ALLOWINSECURE
    #define NNALLOC_CONF_ALLOWINSECURE 0
#endif

#ifndef NNALLOC_CONF_HAVEMMAP
    #define NNALLOC_CONF_HAVEMMAP 1
#endif

#ifndef NNALLOC_CONF_MMAPCLEARS
    #define NNALLOC_CONF_MMAPCLEARS 1
#endif

#ifndef NNALLOC_CONF_HAVEMREMAP
    #ifdef __linux__
        #define NNALLOC_CONF_HAVEMREMAP 1
    #else
        #define NNALLOC_CONF_HAVEMREMAP 0
    #endif
#endif

#ifndef NNALLOC_CONF_MALLOCFAILUREACTION
    #define NNALLOC_CONF_MALLOCFAILUREACTION errno = ENOMEM;
#endif

#ifndef NNALLOC_CONF_HAVEMORECORE
    #define NNALLOC_CONF_HAVEMORECORE 1
#endif

#if !NNALLOC_CONF_HAVEMORECORE
    #define NNALLOC_CONF_MORECORECONTIGUOUS 0
#else
    #ifndef NNALLOC_CONF_MORECOREFUNCNAME
        #define NNALLOC_CONF_MORECOREFUNCNAME sbrk
    #endif
    #ifndef NNALLOC_CONF_MORECORECONTIGUOUS
        #define NNALLOC_CONF_MORECORECONTIGUOUS 1
    #endif
#endif

#ifndef NNALLOC_CONF_DEFAULTGRANULARITY
    #if NNALLOC_CONF_MORECORECONTIGUOUS
        /* 0 means to compute in init_mparams */
        #define NNALLOC_CONF_DEFAULTGRANULARITY (0)
    #else
        #define NNALLOC_CONF_DEFAULTGRANULARITY ((size_t)64U * (size_t)1024U)
    #endif
#endif

#ifndef NNALLOC_CONF_DEFAULTTRIMTHRESHOLD
    #ifndef MORECORE_CANNOT_TRIM
        #define NNALLOC_CONF_DEFAULTTRIMTHRESHOLD ((size_t)2U * (size_t)1024U * (size_t)1024U)
    #else
        #define NNALLOC_CONF_DEFAULTTRIMTHRESHOLD NNALLOC_CONF_MAXSIZET
    #endif
#endif

#ifndef NNALLOC_CONF_DEFAULTMMAPTHRESHOLD
    #if NNALLOC_CONF_HAVEMMAP
        #define NNALLOC_CONF_DEFAULTMMAPTHRESHOLD ((size_t)256U * (size_t)1024U)
    #else
        #define NNALLOC_CONF_DEFAULTMMAPTHRESHOLD NNALLOC_CONF_MAXSIZET
    #endif
#endif

#ifndef NNALLOC_CONF_USEBUILTINFFSFUNC
    #define NNALLOC_CONF_USEBUILTINFFSFUNC 0
#endif

#ifndef NNALLOC_CONF_USEDEVRANDOM
    #define NNALLOC_CONF_USEDEVRANDOM 0
#endif

/*
* disable mmap() on linux, for now.
* nothing wrong with it, but it throws off valgrind, and
* makes it tricky to detect issues.
* NOTE: **MUST** be enabled on windows, or allocation will fail!
*/
#if !defined(NNALLOC_CONF_ISWIN32)
    #undef NNALLOC_CONF_HAVEMMAP
    #define NNALLOC_CONF_HAVEMMAP 0
#endif

/*
  The type of the fields in the NNAllocMallocInfo struct. This was originally
  defined as "int" in SVID etc, but is more usefully defined as
  size_t. The value is used only if  HAVE_USR_INCLUDE_MALLOC_H is not set
*/
typedef size_t NNAllocInfoField;

/*
  NNAllocMSpace is an opaque type representing an independent
  region of space that supports nn_allocator_mspace_malloc, etc.
*/
typedef void NNAllocMSpace;

/* Described below */
typedef size_t NNAllocBIndex;
/* Described below */
typedef unsigned int NNAllocBinMap;
/* The type of various bit flag sets */
typedef unsigned int NNAllocFlag;

typedef struct NNAllocMallocInfo NNAllocMallocInfo;

typedef struct NNAllocChunkItem NNAllocChunkItem;
typedef struct NNAllocMemSegment NNAllocMemSegment;
typedef struct NNAllocChunkTree NNAllocChunkTree;
typedef struct NNAllocState NNAllocState;
typedef struct NNAllocParams NNAllocParams;

struct NNAllocMallocInfo
{
    /* non-mmapped space allocated from system */
    NNAllocInfoField arena;
    /* number of free chunks */
    NNAllocInfoField ordblks;
    /* always 0 */
    NNAllocInfoField smblks;
    /* always 0 */
    NNAllocInfoField hblks;
    /* space in mmapped regions */
    NNAllocInfoField hblkhd;
    /* maximum total allocated space */
    NNAllocInfoField usmblks;
    /* always 0 */
    NNAllocInfoField fsmblks;
    /* total allocated space */
    NNAllocInfoField uordblks;
    /* total free space */
    NNAllocInfoField fordblks;
    /* releasable (via malloc_trim) space */
    NNAllocInfoField keepcost;
};

struct NNAllocChunkItem
{
    /* Size of previous chunk (if free).  */
    size_t prevfoot;
    /* Size and inuse bits. */
    size_t head;
    /* double links -- used only if free. */
    NNAllocChunkItem* forwardptr;
    NNAllocChunkItem* backwardptr;
};


struct NNAllocMemSegment
{
    /* base address */
    char* base;
    /* allocated size */
    size_t size;
    /* ptr to next segment */
    NNAllocMemSegment* next;
#if FFI_MMAP_EXEC_WRIT
    /* We use an offset here, instead of a pointer, because then, when
       base changes, we don't have to modify this.  On architectures
       with segmented addresses, this might not work.  */
    ptrdiff_t execoffset;
#else
    /* mmap and extern flag */
    NNAllocFlag sflags;
#endif
};

void nn_allocator_init();


/*
  malloc(size_t n)
  Returns a pointer to a newly allocated chunk of at least n bytes, or
  null if no space is available, in which case errno is set to ENOMEM
  on ANSI C systems.

  If n is zero, malloc returns a minimum-sized chunk. (The minimum
  size is 16 bytes on most 32bit systems, and 32 bytes on 64bit
  systems.)  Note that size_t is an unsigned type, so calls with
  arguments that would be negative if signed are interpreted as
  requests for huge amounts of space, which will often fail. The
  maximum supported value of n differs across systems, but is in all
  cases less than the maximum representable value of a size_t.
*/
void* nn_allocuser_malloc(size_t);

/*
  free(void* p)
  Releases the chunk of memory pointed to by p, that had been previously
  allocated using malloc or a related routine such as realloc.
  It has no effect if p is null. If p was not malloced or already
  freed, free(p) will by default cause the current program to abort.
*/
void nn_allocuser_free(void*);

/*
  calloc(size_t n_elements, size_t element_size);
  Returns a pointer to n_elements * element_size bytes, with all locations
  set to zero.
*/
void* nn_allocuser_calloc(size_t, size_t);

/*
  realloc(void* p, size_t n)
  Returns a pointer to a chunk of size n that contains the same data
  as does chunk p up to the minimum of (n, p's size) bytes, or null
  if no space is available.

  The returned pointer may or may not be the same as p. The algorithm
  prefers extending p in most cases when possible, otherwise it
  employs the equivalent of a malloc-copy-free sequence.

  If p is null, realloc is equivalent to malloc.

  If space is not available, realloc returns null, errno is set (if on
  ANSI) and p is NOT freed.

  if n is for fewer bytes than already held by p, the newly unused
  space is lopped off and freed if possible.  realloc with a size
  argument of zero (re)allocates a minimum-sized chunk.

  The old unix realloc convention of allowing the last-free'd chunk
  to be used as an argument to realloc is not supported.
*/

void* nn_allocuser_realloc(void*, size_t);

/*
  memalign(size_t alignment, size_t n);
  Returns a pointer to a newly allocated chunk of n bytes, aligned
  in accord with the alignment argument.

  The alignment argument should be a power of two. If the argument is
  not a power of two, the nearest greater power is used.
  8-byte alignment is guaranteed by normal malloc calls, so don't
  bother calling memalign with an argument of 8 or less.

  Overreliance on memalign is a sure way to fragment space.
*/
void* nn_allocuser_memalign(size_t, size_t);

/*
  valloc(size_t n);
  Equivalent to memalign(pagesize, n), where pagesize is the page
  size of the system. If the pagesize is unknown, 4096 is used.
*/
void* nn_allocuser_valloc(size_t);

/*
  mallopt(int parameter_number, int parameter_value)
  Sets tunable parameters The format is to provide a
  (parameter-number, parameter-value) pair.  mallopt then sets the
  corresponding parameter to the argument value if it can (i.e., so
  long as the value is meaningful), and returns 1 if successful else
  0.  SVID/XPG/ANSI defines four standard param numbers for mallopt,
  normally defined in malloc.h.  None of these are use in this malloc,
  so setting them has no effect. But this malloc also supports other
  options in mallopt. See below for details.  Briefly, supported
  parameters are as follows (listed defaults are for "typical"
  configurations).

  Symbol            param #  default    allowed param values
  M_TRIM_THRESHOLD     -1   2*1024*1024   any   (NNALLOC_CONF_MAXSIZET disables)
  M_GRANULARITY        -2     page size   any power of 2 >= page size
  M_MMAP_THRESHOLD     -3      256*1024   any   (or 0 if no MMAP support)
*/
int nn_allocuser_mallopt(int, int);

/*
  malloc_footprint();
  Returns the number of bytes obtained from the system.  The total
  number of bytes allocated by malloc, realloc etc., is less than this
  value. Unlike mallinfo, this function returns only a precomputed
  result, so can be called frequently to monitor memory consumption.
  Even if locks are otherwise defined, this function does not use them,
  so results might not be up to date.
*/
size_t nn_allocuser_getfootprint(void);

/*
  malloc_max_footprint();
  Returns the maximum number of bytes obtained from the system. This
  value will be greater than current footprint if deallocated space
  has been reclaimed by the system. The peak number of bytes allocated
  by malloc, realloc etc., is less than this value. Unlike mallinfo,
  this function returns only a precomputed result, so can be called
  frequently to monitor memory consumption.  Even if locks are
  otherwise defined, this function does not use them, so results might
  not be up to date.
*/
size_t nn_allocuser_getmaxfootprint(void);

/*
  mallinfo()
  Returns (by copy) a struct containing various summary statistics:

  arena:     current total non-mmapped bytes allocated from system
  ordblks:   the number of free chunks
  smblks:    always zero.
  hblks:     current number of mmapped regions
  hblkhd:    total bytes held in mmapped regions
  usmblks:   the maximum total allocated space. This will be greater
                than current total if trimming has occurred.
  fsmblks:   always zero
  uordblks:  current total allocated space (normal or mmapped)
  fordblks:  total free space
  keepcost:  the maximum number of bytes that could ideally be released
               back to system via malloc_trim. ("ideally" means that
               it ignores page restrictions etc.)

  Because these fields are ints, but internal bookkeeping may
  be kept as longs, the reported values may wrap around zero and
  thus be inaccurate.
*/
NNAllocMallocInfo nn_allocuser_mallinfo(void);

/*
  independent_calloc(size_t n_elements, size_t element_size, void* chunks[]);

  independent_calloc is similar to calloc, but instead of returning a
  single cleared space, it returns an array of pointers to n_elements
  independent elements that can hold contents of size elem_size, each
  of which starts out cleared, and can be independently freed,
  realloc'ed etc. The elements are guaranteed to be adjacently
  allocated (this is not guaranteed to occur with multiple callocs or
  mallocs), which may also improve cache locality in some
  applications.

  The "chunks" argument is optional (i.e., may be null, which is
  probably the most typical usage). If it is null, the returned array
  is itself dynamically allocated and should also be freed when it is
  no longer needed. Otherwise, the chunks array must be of at least
  n_elements in length. It is filled in with the pointers to the
  chunks.

  In either case, independent_calloc returns this pointer array, or
  null if the allocation failed.  If n_elements is zero and "chunks"
  is null, it returns a chunk representing an array with zero elements
  (which should be freed if not wanted).

  Each element must be individually freed when it is no longer
  needed. If you'd like to instead be able to free all at once, you
  should instead use regular calloc and assign pointers into this
  space to represent elements.  (In this case though, you cannot
  independently free elements.)

  independent_calloc simplifies and speeds up implementations of many
  kinds of pools.  It may also be useful when constructing large data
  structures that initially have a fixed number of fixed-sized nodes,
  but the number is not known at compile time, and some of the nodes
  may later need to be freed. For example:

  struct Node { int item; struct Node* next; };

  struct Node* build_list() {
    struct Node** pool;
    int n = read_number_of_nodes_needed();
    if (n <= 0) return 0;
    pool = (struct Node**)(independent_calloc(n, sizeof(struct Node), 0);
    if (pool == 0) die();
    // organize into a linked list...
    struct Node* first = pool[0];
    for (i = 0; i < n-1; ++i)
      pool[i]->next = pool[i+1];
    free(pool);     // Can now free the array (or not, if it is needed later)
    return first;
  }
*/
void** nn_allocuser_independentcalloc(size_t, size_t, void**);

/*
  independent_comalloc(size_t n_elements, size_t sizes[], void* chunks[]);

  independent_comalloc allocates, all at once, a set of n_elements
  chunks with sizes indicated in the "sizes" array.    It returns
  an array of pointers to these elements, each of which can be
  independently freed, realloc'ed etc. The elements are guaranteed to
  be adjacently allocated (this is not guaranteed to occur with
  multiple callocs or mallocs), which may also improve cache locality
  in some applications.

  The "chunks" argument is optional (i.e., may be null). If it is null
  the returned array is itself dynamically allocated and should also
  be freed when it is no longer needed. Otherwise, the chunks array
  must be of at least n_elements in length. It is filled in with the
  pointers to the chunks.

  In either case, independent_comalloc returns this pointer array, or
  null if the allocation failed.  If n_elements is zero and chunks is
  null, it returns a chunk representing an array with zero elements
  (which should be freed if not wanted).

  Each element must be individually freed when it is no longer
  needed. If you'd like to instead be able to free all at once, you
  should instead use a single regular malloc, and assign pointers at
  particular offsets in the aggregate space. (In this case though, you
  cannot independently free elements.)

  independent_comallac differs from independent_calloc in that each
  element may have a different size, and also that it does not
  automatically clear elements.

  independent_comalloc can be used to speed up allocation in cases
  where several structs or objects must always be allocated at the
  same time.  For example:

  struct Head { ... }
  struct Foot { ... }

  void send_message(char* msg) {
    int msglen = strlen(msg);
    size_t sizes[3] = { sizeof(struct Head), msglen, sizeof(struct Foot) };
    void* chunks[3];
    if (independent_comalloc(3, sizes, chunks) == 0)
      die();
    struct Head* head = (struct Head*)(chunks[0]);
    char*        body = (char*)(chunks[1]);
    struct Foot* foot = (struct Foot*)(chunks[2]);
    // ...
  }

  In general though, independent_comalloc is worth using only for
  larger values of n_elements. For small values, you probably won't
  detect enough difference from series of malloc calls to bother.

  Overuse of independent_comalloc can increase overall memory usage,
  since it cannot reuse existing noncontiguous small chunks that
  might be available for some of the elements.
*/
void** nn_allocuser_independentcomalloc(size_t, size_t*, void**);

/*
  pvalloc(size_t n);
  Equivalent to valloc(minimum-page-that-holds(n)), that is,
  round up n to nearest pagesize.
 */
void* nn_allocuser_pvalloc(size_t);

/*
  malloc_trim(size_t pad);

  If possible, gives memory back to the system (via negative arguments
  to sbrk) if there is unused memory at the `high' end of the malloc
  pool or in unused MMAP segments. You can call this after freeing
  large blocks of memory to potentially reduce the system-level memory
  requirements of a program. However, it cannot guarantee to reduce
  memory. Under some allocation patterns, some large free blocks of
  memory will be locked between two used chunks, so they cannot be
  given back to the system.

  The `pad' argument to malloc_trim represents the amount of free
  trailing space to leave untrimmed. If this argument is zero, only
  the minimum amount of memory to maintain internal data structures
  will be left. Non-zero arguments can be supplied to maintain enough
  trailing space to service future expected allocations without having
  to re-obtain memory from the system.

  Malloc_trim returns 1 if it actually released any memory, else 0.
*/
int nn_allocuser_malloctrim(size_t);

/*
  malloc_usable_size(void* p);

  Returns the number of bytes you can actually use in
  an allocated chunk, which may be more than you requested (although
  often not) due to alignment and minimum size constraints.
  You can use this many bytes without worrying about
  overwriting other allocated objects. This is not a particularly great
  programming practice. malloc_usable_size can be more useful in
  debugging and assertions, for example:

  p = malloc(n);
  assert(malloc_usable_size(p) >= 256);
*/
size_t nn_allocuser_usablesize(void*);

/*
  malloc_stats();
  Prints on stderr the amount of space obtained from the system (both
  via sbrk and mmap), the maximum amount (which may be more than
  current if malloc_trim and/or munmap got called), and the current
  number of bytes allocated via malloc (or realloc, etc) but not yet
  freed. Note that this is the number of bytes allocated, not the
  number requested. It will be larger than the number requested
  because of alignment and bookkeeping overhead. Because it includes
  alignment wastage as being in use, this figure may be greater than
  zero even when no user-level chunks are allocated.

  The reported current and maximum system memory can be inaccurate if
  a program makes other calls to system memory allocation functions
  (normally sbrk) outside of malloc.

  malloc_stats prints only the most commonly interesting statistics.
  More information can be obtained by calling mallinfo.
*/
void nn_allocuser_stats(void);

/*
  nn_allocator_mspacecreate creates and returns a new independent space with the
  given initial capacity, or, if 0, the default granularity size.  It
  returns null if there is no system memory available to create the
  space.  If argument locked is non-zero, the space uses a separate
  lock to control access. The capacity of the space will grow
  dynamically as needed to service nn_allocator_mspacemalloc requests.  You can
  control the sizes of incremental increases of this space by
  compiling with a different NNALLOC_CONF_DEFAULTGRANULARITY or dynamically
  setting with mallopt(DLMALLOC_OPTMALLOPT_GRANULARITY, value).
*/
NNAllocMSpace* nn_allocator_mspacecreate(size_t capacity, int locked);

/*
  nn_allocator_mspacedestroy destroys the given space, and attempts to return all
  of its memory back to the system, returning the total number of
  bytes freed. After destruction, the results of access to all memory
  used by the space become undefined.
*/
size_t nn_allocator_mspacedestroy(NNAllocMSpace* msp);

/*
  nn_allocator_mspacecreatewithbase uses the memory supplied as the initial base
  of a new NNAllocMSpace*. Part (less than 128*sizeof(size_t) bytes) of this
  space is used for bookkeeping, so the capacity must be at least this
  large. (Otherwise 0 is returned.) When this initial space is
  exhausted, additional memory will be obtained from the system.
  Destroying this space will deallocate all additionally allocated
  space (if possible) but not the initial base.
*/
NNAllocMSpace* nn_allocator_mspacecreatewithbase(void* base, size_t capacity, int locked);

/*
  nn_allocator_mspacemalloc behaves as malloc, but operates within
  the given space.
*/
void* nn_allocator_mspacemalloc(NNAllocMSpace* msp, size_t bytes);

/*
  nn_allocator_mspacefree behaves as free, but operates within
  the given space.

  If compiled with NNALLOC_CONF_STOREFOOTERS==1, nn_allocator_mspacefree is not actually needed.
  free may be called instead of nn_allocator_mspacefree because freed chunks from
  any space are handled by their originating spaces.
*/
void nn_allocator_mspacefree(NNAllocMSpace* msp, void* mem);

/*
  nn_allocator_mspacerealloc behaves as realloc, but operates within
  the given space.

  If compiled with NNALLOC_CONF_STOREFOOTERS==1, nn_allocator_mspacerealloc is not actually
  needed.  realloc may be called instead of nn_allocator_mspacerealloc because
  realloced chunks from any space are handled by their originating
  spaces.
*/
void* nn_allocator_mspacerealloc(NNAllocMSpace* msp, void* mem, size_t newsize);

/*
  nn_allocator_mspacecalloc behaves as calloc, but operates within
  the given space.
*/
void* nn_allocator_mspacecalloc(NNAllocMSpace* msp, size_t n_elements, size_t elem_size);

/*
  nn_allocator_mspacememalign behaves as memalign, but operates within
  the given space.
*/
void* nn_allocator_mspacememalign(NNAllocMSpace* msp, size_t alignment, size_t bytes);

/*
  nn_allocator_mspaceindependentcalloc behaves as independent_calloc, but
  operates within the given space.
*/
void** nn_allocator_mspaceindependentcalloc(NNAllocMSpace* msp, size_t n_elements, size_t elem_size, void* chunks[]);

/*
  nn_allocator_mspaceindependentcomalloc behaves as independent_comalloc, but
  operates within the given space.
*/
void** nn_allocator_mspaceindependentcomalloc(NNAllocMSpace* msp, size_t n_elements, size_t sizes[], void* chunks[]);

/*
  nn_allocator_mspacefootprint() returns the number of bytes obtained from the
  system for this space.
*/
size_t nn_allocator_mspacefootprint(NNAllocMSpace* msp);

/*
  nn_allocator_mspacemaxfootprint() returns the peak number of bytes obtained from the
  system for this space.
*/
size_t nn_allocator_mspacemaxfootprint(NNAllocMSpace* msp);

/*
  nn_allocator_mspacemallinfo behaves as mallinfo, but reports properties of
  the given space.
*/
NNAllocMallocInfo nn_allocator_mspacemallinfo(NNAllocMSpace* msp);

/*
  nn_allocator_mspacemallocstats behaves as malloc_stats, but reports
  properties of the given space.
*/
void nn_allocator_mspacemallocstats(NNAllocMSpace* msp);

/*
  nn_allocator_mspacetrim behaves as malloc_trim, but
  operates within the given space.
*/
int nn_allocator_mspacetrim(NNAllocMSpace* msp, size_t pad);

/*
  An alias for mallopt.
*/
int nn_allocator_mspacemallopt(int, int);

#endif
