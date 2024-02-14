
#pragma once
/* SDSLib 2.0 -- A C dynamic strings library
 *
 * Copyright (c) 2006-2015, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2015, Oran Agra
 * Copyright (c) 2015, Redis Labs, Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <limits.h>

#define sds_actualmalloc malloc
#define sds_actualrealloc realloc
#define sds_actualfree free

#define SDS_LLSTR_SIZE 21

#define SDS_MAX_PREALLOC (1024 * 1024)

#define SDS_TYPE_5 0
#define SDS_TYPE_8 1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4
#define SDS_TYPE_MASK 7
#define SDS_TYPE_BITS 3
#define SDS_HDR_VAR(T, s) T* sh = (T*)(void*)((s) - (sizeof(T)));
#define SDS_HDR(T, s) ((T*)((s) - (sizeof(T))))
#define SDS_TYPE_5_LEN(f) ((f) >> SDS_TYPE_BITS)

#if defined(__GNUC__)
    #define SDSSIZE_T ssize_t
    #define SDS_ATTRIBUTE(...) __attribute__(__VA_ARGS__)
#else
    #define SDSSIZE_T int32_t
    #define SDS_ATTRIBUTE(...)
#endif

static const char* SDS_NOINIT = "SDS_NOINIT";

typedef char* sdstring_t;
typedef struct sdshead5_t sdshead5_t;
typedef struct sdshead8_t sdshead8_t;
typedef struct sdshead16_t sdshead16_t;
typedef struct sdshead32_t sdshead32_t;
typedef struct sdshead64_t sdshead64_t;

/* Note: sdshead5_t is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings. */
struct SDS_ATTRIBUTE((__packed__)) sdshead5_t
{
    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */
    char* data;
};

struct SDS_ATTRIBUTE((__packed__)) sdshead8_t
{
    uint8_t length; /* used */
    uint8_t capacity; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char* data;
};

struct SDS_ATTRIBUTE((__packed__)) sdshead16_t
{
    uint16_t length; /* used */
    uint16_t capacity; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char* data;
};

struct SDS_ATTRIBUTE((__packed__)) sdshead32_t
{
    uint32_t length; /* used */
    uint32_t capacity; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char* data;
};

struct SDS_ATTRIBUTE((__packed__)) sdshead64_t
{
    uint64_t length; /* used */
    uint64_t capacity; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char* data;
};

static inline sdstring_t sds_makelength(const void* init, size_t initlen);
static inline sdstring_t sds_make(const char* init);
static inline sdstring_t sds_makeempty(void);
static inline sdstring_t sds_duplicate(const sdstring_t s);
static inline void sds_destroy(sdstring_t s);
static inline sdstring_t sds_growzero(sdstring_t s, size_t len);
static inline sdstring_t sds_appendlen(sdstring_t s, const void* t, size_t len);
static inline sdstring_t sds_append(sdstring_t s, const char* t);
static inline sdstring_t sds_appendsds(sdstring_t s, const sdstring_t t);
static inline sdstring_t sds_copylength(sdstring_t s, const char* t, size_t len);
static inline sdstring_t sds_copy(sdstring_t s, const char* t);

static inline sdstring_t sds_appendvprintf(sdstring_t s, const char* fmt, va_list ap);
#ifdef __GNUC__
static inline sdstring_t sds_appendprintf(sdstring_t s, const char* fmt, ...) SDS_ATTRIBUTE((format(printf, 2, 3)));
#else
static inline sdstring_t sds_appendprintf(sdstring_t s, const char* fmt, ...);
#endif

static inline sdstring_t sds_appendfmt(sdstring_t s, char const* fmt, ...);
static inline sdstring_t sds_trim(sdstring_t s, const char* cset);
static inline void sds_range(sdstring_t s, SDSSIZE_T start, SDSSIZE_T end);
static inline void sds_updatelength(sdstring_t s);
static inline void sds_clear(sdstring_t s);
static inline int sds_compare(const sdstring_t s1, const sdstring_t s2);
static inline sdstring_t* sds_splitlen(const char* s, SDSSIZE_T len, const char* sep, int seplen, int* count);
static inline void sds_freesplitres(sdstring_t* tokens, int count);
static inline void sds_tolower(sdstring_t s);
static inline void sds_toupper(sdstring_t s);
static inline sdstring_t sds_fromlonglong(long long value);
static inline sdstring_t sds_appendrepr(sdstring_t s, const char* p, size_t len);
static inline sdstring_t* sds_splitargs(const char* line, int* argc);
static inline sdstring_t sds_mapchars(sdstring_t s, const char* from, const char* to, size_t setlen);
static inline sdstring_t sds_join(char** argv, int argc, char* sep);
static inline sdstring_t sds_joinsds(sdstring_t* argv, int argc, const char* sep, size_t seplen);

/* Low level functions exposed to the user API */
static inline sdstring_t sds_allocroomfor(sdstring_t s, size_t addlen);
static inline void sds_internincrlength(sdstring_t s, SDSSIZE_T incr);
static inline sdstring_t sds_removefreespace(sdstring_t s);
static inline size_t sds_internallocsize(sdstring_t s);
static inline void* sds_allocptr(sdstring_t s);

/* Export the allocator used by SDS to the program using SDS.
 * Sometimes the program SDS is linked to, may use a different set of
 * allocators, but may want to allocate or free things that SDS will
 * respectively free or allocate. */
static inline void* sds_memmalloc(size_t size);
static inline void* sds_memrealloc(void* ptr, size_t size);
static inline void sds_memfree(void* ptr);

static inline size_t sds_getlength(const sdstring_t s)
{
    unsigned char flags = s[-1];
    switch(flags & SDS_TYPE_MASK)
    {
        case SDS_TYPE_5:
            {
                return SDS_TYPE_5_LEN(flags);
            }
            break;
        case SDS_TYPE_8:
            {
                return SDS_HDR(sdshead8_t, s)->length;
            }
            break;
        case SDS_TYPE_16:
            {
                return SDS_HDR(sdshead16_t, s)->length;
            }
            break;
        case SDS_TYPE_32:
            {
                return SDS_HDR(sdshead32_t, s)->length;
            }
            break;
        case SDS_TYPE_64:
            {
                return SDS_HDR(sdshead64_t, s)->length;
            }
            break;
    }
    return 0;
}

static inline size_t sds_getcapacity(const sdstring_t s)
{
    unsigned char flags = s[-1];
    switch(flags & SDS_TYPE_MASK)
    {
        case SDS_TYPE_5:
            {
                return 0;
            }
            break;
        case SDS_TYPE_8:
            {
                SDS_HDR_VAR(sdshead8_t, s);
                return sh->capacity - sh->length;
            }
            break;
        case SDS_TYPE_16:
            {
                SDS_HDR_VAR(sdshead16_t, s);
                return sh->capacity - sh->length;
            }
            break;
        case SDS_TYPE_32:
            {
                SDS_HDR_VAR(sdshead32_t, s);
                return sh->capacity - sh->length;
            }
            break;
        case SDS_TYPE_64:
            {
                SDS_HDR_VAR(sdshead64_t, s);
                return sh->capacity - sh->length;
            }
            break;
    }
    return 0;
}

static inline void sds_setlength(sdstring_t s, size_t newlen)
{
    unsigned char* fp;
    unsigned char flags;
    flags = s[-1];
    switch(flags & SDS_TYPE_MASK)
    {
        case SDS_TYPE_5:
            {
                fp = ((unsigned char*)s) - 1;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            {
                SDS_HDR(sdshead8_t, s)->length = newlen;
            }
            break;
        case SDS_TYPE_16:
            {
                SDS_HDR(sdshead16_t, s)->length = newlen;
            }
            break;
        case SDS_TYPE_32:
            {
                SDS_HDR(sdshead32_t, s)->length = newlen;
            }
            break;
        case SDS_TYPE_64:
            {
                SDS_HDR(sdshead64_t, s)->length = newlen;
            }
            break;
    }
}

static inline void sds_increaselength(sdstring_t s, size_t inc)
{
    unsigned char* fp;
    unsigned char flags;
    unsigned char newlen;
    flags = s[-1];
    switch(flags & SDS_TYPE_MASK)
    {
        case SDS_TYPE_5:
            {
                fp = ((unsigned char*)s) - 1;
                newlen = SDS_TYPE_5_LEN(flags) + inc;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            {
                SDS_HDR(sdshead8_t, s)->length += inc;
            }
            break;
        case SDS_TYPE_16:
            {
                SDS_HDR(sdshead16_t, s)->length += inc;
            }
            break;
        case SDS_TYPE_32:
            {
                SDS_HDR(sdshead32_t, s)->length += inc;
            }
            break;
        case SDS_TYPE_64:
            {
                SDS_HDR(sdshead64_t, s)->length += inc;
            }
            break;
    }
}

/* sds_getallocatedsize() = sds_getcapacity() + sds_getlength() */
static inline size_t sds_getallocatedsize(const sdstring_t s)
{
    unsigned char flags;
    flags = s[-1];
    switch(flags & SDS_TYPE_MASK)
    {
        case SDS_TYPE_5:
            {
                return SDS_TYPE_5_LEN(flags);
            }
            break;
        case SDS_TYPE_8:
            {
                return SDS_HDR(sdshead8_t, s)->capacity;
            }
            break;
        case SDS_TYPE_16:
            {
                return SDS_HDR(sdshead16_t, s)->capacity;
            }
            break;
        case SDS_TYPE_32:
            {
                return SDS_HDR(sdshead32_t, s)->capacity;
            }
            break;
        case SDS_TYPE_64:
            {
                return SDS_HDR(sdshead64_t, s)->capacity;
            }
            break;
    }
    return 0;
}

static inline void sds_setalloc(sdstring_t s, size_t newlen)
{
    unsigned char flags;
    flags = s[-1];
    switch(flags & SDS_TYPE_MASK)
    {
        case SDS_TYPE_5:
            {
                /* Nothing to do, this type has no total allocation info. */
            }
            break;
        case SDS_TYPE_8:
            {
                SDS_HDR(sdshead8_t, s)->capacity = newlen;
            }
            break;
        case SDS_TYPE_16:
            {
                SDS_HDR(sdshead16_t, s)->capacity = newlen;
            }
            break;
        case SDS_TYPE_32:
            {
                SDS_HDR(sdshead32_t, s)->capacity = newlen;
            }
            break;
        case SDS_TYPE_64:
            {
                SDS_HDR(sdshead64_t, s)->capacity = newlen;
            }
            break;
    }
}

static inline int sds_getheadersize(char type)
{
    switch(type & SDS_TYPE_MASK)
    {
        case SDS_TYPE_5:
            {
                return sizeof(sdshead5_t);
            }
            break;
        case SDS_TYPE_8:
            {
                return sizeof(sdshead8_t);
            }
            break;
        case SDS_TYPE_16:
            {
                return sizeof(sdshead16_t);
            }
            break;
        case SDS_TYPE_32:
            {
                return sizeof(sdshead32_t);
            }
            break;
        case SDS_TYPE_64:
            {
                return sizeof(sdshead64_t);
            }
            break;
    }
    return 0;
}

static inline char sds_reqtype(size_t string_size)
{
    if(string_size < 1 << 5)
    {
        return SDS_TYPE_5;
    }
    if(string_size < 1 << 8)
    {
        return SDS_TYPE_8;
    }
    if(string_size < 1 << 16)
    {
        return SDS_TYPE_16;
    }
#if(LONG_MAX == LLONG_MAX)
    if(string_size < 1ll << 32)
    {
        return SDS_TYPE_32;
    }
    return SDS_TYPE_64;
#else
    return SDS_TYPE_32;
#endif
}

/* Create a new sds string with the content specified by the 'init' pointer
 * and 'initlen'.
 * If NULL is used for 'init' the string is initialized with zero bytes.
 * If SDS_NOINIT is used, the buffer is left uninitialized;
 *
 * The string is always null-terminated (all the sds strings are, always) so
 * even if you create an sds string with:
 *
 * mystring = sds_makelength("abc",3);
 *
 * You can print the string with printf() as there is an implicit \0 at the
 * end of the string. However the string is binary safe and can contain
 * \0 characters in the middle, as the length is stored in the sds header. */
static inline sdstring_t sds_makelength(const void* init, size_t initlen)
{
    void* headbuf;
    sdstring_t s;
    /* flags pointer. */
    unsigned char* fp;
    char type;
    int hdrlen;
    type = sds_reqtype(initlen);
    /* Empty strings are usually created in order to append. Use type 8
     * since type 5 is not good at this. */
    if(type == SDS_TYPE_5 && initlen == 0)
    {
        type = SDS_TYPE_8;
    }
    hdrlen = sds_getheadersize(type);
    headbuf = sds_actualmalloc(hdrlen + initlen + 1);
    if(headbuf == NULL)
    {
        return NULL;
    }
    if(init == SDS_NOINIT)
    {
        init = NULL;
    }
    else if(!init)
    {
        memset(headbuf, 0, hdrlen + initlen + 1);
    }
    s = (char*)headbuf + hdrlen;
    fp = ((unsigned char*)s) - 1;
    switch(type)
    {
        case SDS_TYPE_5:
            {
                *fp = type | (initlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            {
                SDS_HDR_VAR(sdshead8_t, s);
                sh->length = initlen;
                sh->capacity = initlen;
                *fp = type;
            }
            break;
        case SDS_TYPE_16:
            {
                SDS_HDR_VAR(sdshead16_t, s);
                sh->length = initlen;
                sh->capacity = initlen;
                *fp = type;
            }
            break;
        case SDS_TYPE_32:
            {
                SDS_HDR_VAR(sdshead32_t, s);
                sh->length = initlen;
                sh->capacity = initlen;
                *fp = type;
            }
            break;
        case SDS_TYPE_64:
            {
                SDS_HDR_VAR(sdshead64_t, s);
                sh->length = initlen;
                sh->capacity = initlen;
                *fp = type;
            }
            break;
    }
    if(initlen && init)
    {
        memcpy(s, init, initlen);
    }
    s[initlen] = '\0';
    return s;
}

/* Create an empty (zero length) sds string. Even in this case the string
 * always has an implicit null term. */
static inline sdstring_t sds_makeempty(void)
{
    return sds_makelength("", 0);
}

/* Create a new sds string starting from a null terminated C string. */
static inline sdstring_t sds_make(const char* init)
{
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sds_makelength(init, initlen);
}

/* Duplicate an sds string. */
static inline sdstring_t sds_duplicate(const sdstring_t s)
{
    return sds_makelength(s, sds_getlength(s));
}

/* Free an sds string. No operation is performed if 's' is NULL. */
static inline void sds_destroy(sdstring_t s)
{
    if(s == NULL)
    {
        return;
    }
    sds_actualfree((char*)s - sds_getheadersize(s[-1]));
}

/* Set the sds string length to the length as obtained with strlen(), so
 * considering as content only up to the first null term character.
 *
 * This function is useful when the sds string is hacked manually in some
 * way, like in the following example:
 *
 * s = sds_make("foobar");
 * s[2] = '\0';
 * sds_updatelength(s);
 * printf("%d\n", sds_getlength(s));
 *
 * The output will be "2", but if we comment out the call to sds_updatelength()
 * the output will be "6" as the string was modified but the logical length
 * remains 6 bytes. */
static inline void sds_updatelength(sdstring_t s)
{
    size_t reallen;
    reallen = strlen(s);
    sds_setlength(s, reallen);
}

/* Modify an sds string in-place to make it empty (zero length).
 * However all the existing buffer is not discarded but set as free space
 * so that next append operations will not require allocations up to the
 * number of bytes previously available. */
static inline void sds_clear(sdstring_t s)
{
    sds_setlength(s, 0);
    s[0] = '\0';
}

/* Enlarge the free space at the end of the sds string so that the caller
 * is sure that after calling this function can overwrite up to addlen
 * bytes after the end of the string, plus one more byte for nul term.
 *
 * Note: this does not change the *length* of the sds string as returned
 * by sds_getlength(), but only the free buffer space we have. */
static inline sdstring_t sds_allocroomfor(sdstring_t s, size_t addlen)
{
    void* sh;
    void* newsh;
    size_t avail;
    size_t len;
    size_t newlen;
    size_t reqlen;
    char type;
    char oldtype;
    int hdrlen;
    avail = sds_getcapacity(s);
    oldtype = s[-1] & SDS_TYPE_MASK;
    /* Return ASAP if there is enough space left. */
    if(avail >= addlen)
    {
        return s;
    }
    len = sds_getlength(s);
    sh = (char*)s - sds_getheadersize(oldtype);
    reqlen = newlen = (len + addlen);
    if(newlen < SDS_MAX_PREALLOC)
    {
        newlen *= 2;
    }
    else
    {
        newlen += SDS_MAX_PREALLOC;
    }
    type = sds_reqtype(newlen);
    /*
    * Don't use type 5: the user is appending to the string and type 5 is
    * not able to remember empty space, so sds_allocroomfor() must be called
    * at every appending operation.
    */
    if(type == SDS_TYPE_5)
    {
        type = SDS_TYPE_8;
    }
    hdrlen = sds_getheadersize(type);
    /* Catch size_t overflow */
    assert(hdrlen + newlen + 1 > reqlen);
    if(oldtype == type)
    {
        newsh = sds_actualrealloc(sh, hdrlen + newlen + 1);
        if(newsh == NULL)
        {
            return NULL;
        }
        s = (char*)newsh + hdrlen;
    }
    else
    {
        /* Since the header size changes, need to move the string forward,
         * and can't use realloc */
        newsh = sds_actualmalloc(hdrlen + newlen + 1);
        if(newsh == NULL)
        {
            return NULL;
        }
        memcpy((char*)newsh + hdrlen, s, len + 1);
        sds_actualfree(sh);
        s = (char*)newsh + hdrlen;
        s[-1] = type;
        sds_setlength(s, len);
    }
    sds_setalloc(s, newlen);
    return s;
}

/* Reallocate the sds string so that it has no free space at the end. The
 * contained string remains not altered, but next concatenation operations
 * will require a reallocation.
 *
 * After the call, the passed sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
static inline sdstring_t sds_removefreespace(sdstring_t s)
{
    void* sh;
    void* newsh;
    char type;
    char oldtype;
    int hdrlen;
    int oldhdrlen;
    size_t len;
    size_t avail;
    avail = sds_getcapacity(s);
    oldtype = s[-1] & SDS_TYPE_MASK;
    oldhdrlen = sds_getheadersize(oldtype);
    sh = (char*)s - oldhdrlen;
    len = sds_getlength(s);
    /* Return ASAP if there is no space left. */
    if(avail == 0)
    {
        return s;
    }
    /*
    * Check what would be the minimum SDS header that is just good enough to
    * fit this string.
    */
    type = sds_reqtype(len);
    hdrlen = sds_getheadersize(type);
    /*
    * If the type is the same, or at least a large enough type is still
    * required, we just realloc(), letting the allocator to do the copy
    * only if really needed. Otherwise if the change is huge, we manually
    * reallocate the string to use the different header type.
    */
    if(oldtype == type || type > SDS_TYPE_8)
    {
        newsh = sds_actualrealloc(sh, oldhdrlen + len + 1);
        if(newsh == NULL)
        {
            return NULL;
        }
        s = (char*)newsh + oldhdrlen;
    }
    else
    {
        newsh = sds_actualmalloc(hdrlen + len + 1);
        if(newsh == NULL)
        {
            return NULL;
        }
        memcpy((char*)newsh + hdrlen, s, len + 1);
        sds_actualfree(sh);
        s = (char*)newsh + hdrlen;
        s[-1] = type;
        sds_setlength(s, len);
    }
    sds_setalloc(s, len);
    return s;
}

/* Return the total size of the allocation of the specified sds string,
 * including:
 * 1) The sds header before the pointer.
 * 2) The string.
 * 3) The free buffer at the end if any.
 * 4) The implicit null term.
 */
static inline size_t sds_internallocsize(sdstring_t s)
{
    size_t alloc;
    alloc = sds_getallocatedsize(s);
    return sds_getheadersize(s[-1]) + alloc + 1;
}

/* Return the pointer of the actual SDS allocation (normally SDS strings
 * are referenced by the start of the string buffer). */
static inline void* sds_allocptr(sdstring_t s)
{
    return (void*)(s - sds_getheadersize(s[-1]));
}

/* Increment the sds length and decrements the left free space at the
 * end of the string according to 'incr'. Also set the null term
 * in the new end of the string.
 *
 * This function is used in order to fix the string length after the
 * user calls sds_allocroomfor(), writes something after the end of
 * the current string, and finally needs to set the new length.
 *
 * Note: it is possible to use a negative increment in order to
 * right-trim the string.
 *
 * Usage example:
 *
 * Using sds_internincrlength() and sds_allocroomfor() it is possible to mount the
 * following schema, to cat bytes coming from the kernel to the end of an
 * sds string without copying into an intermediate buffer:
 *
 * oldlen = sds_getlength(s);
 * s = sds_allocroomfor(s, BUFFER_SIZE);
 * nread = read(fd, s+oldlen, BUFFER_SIZE);
 * ... check for nread <= 0 and handle it ...
 * sds_internincrlength(s, nread);
 */
static inline void sds_internincrlength(sdstring_t s, SDSSIZE_T incr)
{
    unsigned char flags;
    size_t len;
    unsigned char* fp;
    unsigned char oldlen;
    flags = s[-1];
    switch(flags & SDS_TYPE_MASK)
    {
        case SDS_TYPE_5:
            {
                fp = ((unsigned char*)s) - 1;
                oldlen = SDS_TYPE_5_LEN(flags);
                assert((incr > 0 && oldlen + incr < 32) || (incr < 0 && oldlen >= (unsigned int)(-incr)));
                *fp = SDS_TYPE_5 | ((oldlen + incr) << SDS_TYPE_BITS);
                len = oldlen + incr;
            }
            break;
        case SDS_TYPE_8:
            {
                SDS_HDR_VAR(sdshead8_t, s);
                assert((incr >= 0 && sh->capacity - sh->length >= incr) || (incr < 0 && sh->length >= (unsigned int)(-incr)));
                len = (sh->length += incr);
            }
            break;
        case SDS_TYPE_16:
            {
                SDS_HDR_VAR(sdshead16_t, s);
                assert((incr >= 0 && sh->capacity - sh->length >= incr) || (incr < 0 && sh->length >= (unsigned int)(-incr)));
                len = (sh->length += incr);
            }
            break;
        case SDS_TYPE_32:
            {
                SDS_HDR_VAR(sdshead32_t, s);
                assert((incr >= 0 && sh->capacity - sh->length >= (unsigned int)incr) || (incr < 0 && sh->length >= (unsigned int)(-incr)));
                len = (sh->length += incr);
            }
            break;
        case SDS_TYPE_64:
            {
                SDS_HDR_VAR(sdshead64_t, s);
                assert((incr >= 0 && sh->capacity - sh->length >= (uint64_t)incr) || (incr < 0 && sh->length >= (uint64_t)(-incr)));
                len = (sh->length += incr);
            }
            break;
        default:
            {
                /* Just to avoid compilation warnings. */
                len = 0;
            }
            break;
    }
    s[len] = '\0';
}

/* Grow the sds to have the specified length. Bytes that were not part of
 * the original length of the sds will be set to zero.
 *
 * if the specified length is smaller than the current length, no operation
 * is performed. */
static inline sdstring_t sds_growzero(sdstring_t s, size_t len)
{
    size_t curlen;
    curlen = sds_getlength(s);
    if(len <= curlen)
    {
        return s;
    }
    s = sds_allocroomfor(s, len - curlen);
    if(s == NULL)
    {
        return NULL;
    }
    /* Make sure added region doesn't contain garbage */
    memset(s + curlen, 0, (len - curlen + 1)); /* also set trailing \0 byte */
    sds_setlength(s, len);
    return s;
}

/* Append the specified binary-safe string pointed by 't' of 'len' bytes to the
 * end of the specified sds string 's'.
 *
 * After the call, the passed sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
static inline sdstring_t sds_appendlen(sdstring_t s, const void* t, size_t len)
{
    size_t curlen;
    curlen = sds_getlength(s);
    s = sds_allocroomfor(s, len);
    if(s == NULL)
    {
        return NULL;
    }
    memcpy(s + curlen, t, len);
    sds_setlength(s, curlen + len);
    s[curlen + len] = '\0';
    return s;
}

/* Append the specified null termianted C string to the sds string 's'.
 *
 * After the call, the passed sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
static inline sdstring_t sds_append(sdstring_t s, const char* t)
{
    return sds_appendlen(s, t, strlen(t));
}

/* Append the specified sds 't' to the existing sds 's'.
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
static inline sdstring_t sds_appendsds(sdstring_t s, const sdstring_t t)
{
    return sds_appendlen(s, t, sds_getlength(t));
}

/* Destructively modify the sds string 's' to hold the specified binary
 * safe string pointed by 't' of length 'len' bytes. */
static inline sdstring_t sds_copylength(sdstring_t s, const char* t, size_t len)
{
    if(sds_getallocatedsize(s) < len)
    {
        s = sds_allocroomfor(s, len - sds_getlength(s));
        if(s == NULL)
        {
            return NULL;
        }
    }
    memcpy(s, t, len);
    s[len] = '\0';
    sds_setlength(s, len);
    return s;
}

/* Like sds_copylength() but 't' must be a null-terminated string so that the length
 * of the string is obtained with strlen(). */
static inline sdstring_t sds_copy(sdstring_t s, const char* t)
{
    return sds_copylength(s, t, strlen(t));
}

/* Helper for sdscatlonglong() doing the actual number -> string
 * conversion. 's' must point to a string with room for at least
 * SDS_LLSTR_SIZE bytes.
 *
 * The function returns the length of the null-terminated string
 * representation stored at 's'. */

static inline int sdsutil_ll2str(char* s, long long value)
{
    char aux;
    unsigned long long v;
    size_t l;
    char *p;
    /* Generate the string representation, this method produces
     * an reversed string. */
    if(value < 0)
    {
        /* Since v is unsigned, if value==LLONG_MIN then
         * -LLONG_MIN will overflow. */
        if(value != LLONG_MIN)
        {
            v = -value;
        }
        else
        {
            v = ((unsigned long long)LLONG_MAX) + 1;
        }
    }
    else
    {
        v = value;
    }

    p = s;
    do
    {
        *p++ = '0' + (v % 10);
        v /= 10;
    } while(v);
    if(value < 0)
    {
        *p++ = '-';
    }
    /* Compute length and add null term. */
    l = p - s;
    *p = '\0';
    /* Reverse the string. */
    p--;
    while(s < p)
    {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

/* Identical sdsutil_ll2str(), but for unsigned long long type. */
static inline int sdsutil_ull2str(char* s, unsigned long long v)
{
    char *p;
    char aux;
    size_t l;
    /* Generate the string representation, this method produces
     * an reversed string. */
    p = s;
    do
    {
        *p++ = '0' + (v % 10);
        v /= 10;
    } while(v);
    /* Compute length and add null term. */
    l = p - s;
    *p = '\0';
    /* Reverse the string. */
    p--;
    while(s < p)
    {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

/* Create an sds string from a long long value. It is much faster than:
 *
 * sds_appendprintf(sds_makeempty(),"%lld\n", value);
 */
static inline sdstring_t sds_fromlonglong(long long value)
{
    int len;
    char buf[SDS_LLSTR_SIZE];
    len = sdsutil_ll2str(buf, value);
    return sds_makelength(buf, len);
}

/* Like sds_appendprintf() but gets va_list instead of being variadic. */
static inline sdstring_t sds_appendvprintf(sdstring_t s, const char* fmt, va_list ap)
{
    va_list cpy;
    char *buf;
    char* t;
    char staticbuf[1024];
    size_t buflen;
    int bufstrlen;
    buf = staticbuf;
    buflen = strlen(fmt) * 2;
    /* We try to start using a static buffer for speed.
     * If not possible we revert to heap allocation. */
    if(buflen > sizeof(staticbuf))
    {
        buf = (char*)sds_actualmalloc(buflen);
        if(buf == NULL)
        {
            return NULL;
        }
    }
    else
    {
        buflen = sizeof(staticbuf);
    }

    /* Alloc enough space for buffer and \0 after failing to
     * fit the string in the current buffer size. */
    while(1)
    {
        va_copy(cpy, ap);
        bufstrlen = vsnprintf(buf, buflen, fmt, cpy);
        va_end(cpy);
        if(bufstrlen < 0)
        {
            if(buf != staticbuf)
            {
                sds_actualfree(buf);
            }
            return NULL;
        }
        if(((size_t)bufstrlen) >= buflen)
        {
            if(buf != staticbuf)
            {
                sds_actualfree(buf);
            }
            buflen = ((size_t)bufstrlen) + 1;
            buf = (char*)sds_actualmalloc(buflen);
            if(buf == NULL)
            {
                return NULL;
            }
            continue;
        }
        break;
    }

    /* Finally concat the obtained string to the SDS string and return it. */
    t = sds_appendlen(s, buf, bufstrlen);
    if(buf != staticbuf)
    {
        sds_actualfree(buf);
    }
    return t;
}

/* Append to the sds string 's' a string obtained using printf-alike format
 * specifier.
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call.
 *
 * Example:
 *
 * s = sds_make("Sum is: ");
 * s = sds_appendprintf(s,"%d+%d = %d",a,b,a+b).
 *
 * Often you need to create a string from scratch with the printf-alike
 * format. When this is the need, just use sds_makeempty() as the target string:
 *
 * s = sds_appendprintf(sds_makeempty(), "... your format ...", args);
 */
static inline sdstring_t sds_appendprintf(sdstring_t s, const char* fmt, ...)
{
    va_list ap;
    char* t;
    va_start(ap, fmt);
    t = sds_appendvprintf(s, fmt, ap);
    va_end(ap);
    return t;
}

/* This function is similar to sds_appendprintf, but much faster as it does
 * not rely on sprintf() family functions implemented by the libc that
 * are often very slow. Moreover directly handling the sds string as
 * new data is concatenated provides a performance improvement.
 *
 * However this function only handles an incompatible subset of printf-alike
 * format specifiers:
 *
 * %s - C String
 * %S - SDS string
 * %i - signed int
 * %I - 64 bit signed integer (long long, int64_t)
 * %u - unsigned int
 * %U - 64 bit unsigned integer (unsigned long long, uint64_t)
 * %% - Verbatim "%" character.
 */
static inline sdstring_t sds_appendfmt(sdstring_t s, char const* fmt, ...)
{
    size_t l;
    size_t initlen;
    long i;
    long long num;
    unsigned long long unum;
    char next;
    char* str;
    const char* pfmt;
    initlen = sds_getlength(s);
    pfmt = fmt;
    va_list ap;

    /* To avoid continuous reallocations, let's start with a buffer that
     * can hold at least two times the format string itself. It's not the
     * best heuristic but seems to work in practice. */
    s = sds_allocroomfor(s, initlen + strlen(fmt) * 2);
    va_start(ap, fmt);
    pfmt = fmt; /* Next format specifier byte to process. */
    i = initlen; /* Position of the next byte to write to dest str. */
    while(*pfmt)
    {
        /* Make sure there is always space for at least 1 char. */
        if(sds_getcapacity(s) == 0)
        {
            s = sds_allocroomfor(s, 1);
        }
        switch(*pfmt)
        {
            case '%':
                {
                    next = *(pfmt + 1);
                    if(next == '\0')
                    {
                        break;
                    }
                    pfmt++;
                    switch(next)
                    {
                        case 's':
                        case 'S':
                            {
                                str = va_arg(ap, char*);
                                l = (next == 's') ? strlen(str) : sds_getlength(str);
                                if(sds_getcapacity(s) < l)
                                {
                                    s = sds_allocroomfor(s, l);
                                }
                                memcpy(s + i, str, l);
                                sds_increaselength(s, l);
                                i += l;
                            }
                            break;
                        case 'i':
                        case 'I':
                            {
                                if(next == 'i')
                                    num = va_arg(ap, int);
                                else
                                    num = va_arg(ap, long long);
                                {
                                    char buf[SDS_LLSTR_SIZE];
                                    l = sdsutil_ll2str(buf, num);
                                    if(sds_getcapacity(s) < l)
                                    {
                                        s = sds_allocroomfor(s, l);
                                    }
                                    memcpy(s + i, buf, l);
                                    sds_increaselength(s, l);
                                    i += l;
                                }
                            }
                            break;
                        case 'u':
                        case 'U':
                            {
                                if(next == 'u')
                                    unum = va_arg(ap, unsigned int);
                                else
                                    unum = va_arg(ap, unsigned long long);
                                {
                                    char buf[SDS_LLSTR_SIZE];
                                    l = sdsutil_ull2str(buf, unum);
                                    if(sds_getcapacity(s) < l)
                                    {
                                        s = sds_allocroomfor(s, l);
                                    }
                                    memcpy(s + i, buf, l);
                                    sds_increaselength(s, l);
                                    i += l;
                                }
                            }
                            break;
                        default: /* Handle %% and generally %<unknown>. */
                            {
                                s[i++] = next;
                                sds_increaselength(s, 1);
                            }
                            break;
                    }
                }
                break;
            default:
                {
                    s[i++] = *pfmt;
                    sds_increaselength(s, 1);
                }
                break;
        }
        pfmt++;
    }
    va_end(ap);
    /* Add null-term */
    s[i] = '\0';
    return s;
}

/* Remove the part of the string from left and from right composed just of
 * contiguous characters found in 'cset', that is a null terminated C string.
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call.
 *
 * Example:
 *
 * s = sds_make("AA...AA.a.aa.aHelloWorld     :::");
 * s = sds_trim(s,"Aa. :");
 * printf("%s\n", s);
 *
 * Output will be just "HelloWorld".
 */
static inline sdstring_t sds_trim(sdstring_t s, const char* cset)
{
    size_t len;
    char* sp;
    char* ep;
    char* end;
    sp = s;
    ep = end = s + sds_getlength(s) - 1;
    while(sp <= end && strchr(cset, *sp))
    {
        sp++;
    }
    while(ep > sp && strchr(cset, *ep))
    {
        ep--;
    }
    len = (ep - sp) + 1;
    if(s != sp)
    {
        memmove(s, sp, len);
    }
    s[len] = '\0';
    sds_setlength(s, len);
    return s;
}

/* Turn the string into a smaller (or equal) string containing only the
 * substring specified by the 'start' and 'end' indexes.
 *
 * start and end can be negative, where -1 means the last character of the
 * string, -2 the penultimate character, and so forth.
 *
 * The interval is inclusive, so the start and end characters will be part
 * of the resulting string.
 *
 * The string is modified in-place.
 *
 * Example:
 *
 * s = sds_make("Hello World");
 * sds_range(s,1,-1); => "ello World"
 */
static inline void sds_range(sdstring_t s, SDSSIZE_T start, SDSSIZE_T end)
{
    size_t newlen;
    size_t len;
    len = sds_getlength(s);
    if(len == 0)
    {
        return;
    }
    if(start < 0)
    {
        start = len + start;
        if(start < 0)
        {
            start = 0;
        }
    }
    if(end < 0)
    {
        end = len + end;
        if(end < 0)
        {
            end = 0;
        }
    }
    newlen = (start > end) ? 0 : (end - start) + 1;
    if(newlen != 0)
    {
        if(start >= (SDSSIZE_T)len)
        {
            newlen = 0;
        }
        else if(end >= (SDSSIZE_T)len)
        {
            end = len - 1;
            newlen = (end - start) + 1;
        }
    }
    if(start && newlen)
    {
        memmove(s, s + start, newlen);
    }
    s[newlen] = 0;
    sds_setlength(s, newlen);
}

/* Apply tolower() to every character of the sds string 's'. */
static inline void sds_tolower(sdstring_t s)
{
    size_t j;
    size_t len;
    len = sds_getlength(s);
    for(j = 0; j < len; j++)
    {
        s[j] = tolower(s[j]);
    }
}

/* Apply toupper() to every character of the sds string 's'. */
static inline void sds_toupper(sdstring_t s)
{
    size_t j;
    size_t len;
    len = sds_getlength(s);
    for(j = 0; j < len; j++)
    {
        s[j] = toupper(s[j]);
    }
}

/* Compare two sds strings s1 and s2 with memcmp().
 *
 * Return value:
 *
 *     positive if s1 > s2.
 *     negative if s1 < s2.
 *     0 if s1 and s2 are exactly the same binary string.
 *
 * If two strings share exactly the same prefix, but one of the two has
 * additional characters, the longer string is considered to be greater than
 * the smaller one. */
static inline int sds_compare(const sdstring_t s1, const sdstring_t s2)
{
    size_t l1;
    size_t l2;
    size_t minlen;
    int cmp;
    l1 = sds_getlength(s1);
    l2 = sds_getlength(s2);
    minlen = (l1 < l2) ? l1 : l2;
    cmp = memcmp(s1, s2, minlen);
    if(cmp == 0)
    {
        if(l1 > l2)
        {
            return 1;
        }
        if(l1 < l2)
        {
            return -1;
        }
        return 0;
    }
    return cmp;
}

/* Split 's' with separator in 'sep'. An array
 * of sds strings is returned. *count will be set
 * by reference to the number of tokens returned.
 *
 * On out of memory, zero length string, zero length
 * separator, NULL is returned.
 *
 * Note that 'sep' is able to split a string using
 * a multi-character separator. For example
 * sdssplit("foo_-_bar","_-_"); will return two
 * elements "foo" and "bar".
 *
 * This version of the function is binary-safe but
 * requires length arguments. sdssplit() is just the
 * same function but for zero-terminated strings.
 */
static inline sdstring_t* sds_splitlen(const char* s, SDSSIZE_T len, const char* sep, int seplen, int* count)
{
    int i;
    int slots;
    int elements;
    long j;
    long start;
    sdstring_t* tokens;
    sdstring_t* newtokens;
    elements = 0;
    slots = 5;
    start = 0;
    if(seplen < 1 || len <= 0)
    {
        *count = 0;
        return NULL;
    }
    tokens = (sdstring_t*)sds_actualmalloc(sizeof(sdstring_t) * slots);
    if(tokens == NULL)
    {
        return NULL;
    }
    for(j = 0; j < (len - (seplen - 1)); j++)
    {
        /* make sure there is room for the next element and the final one */
        if(slots < elements + 2)
        {
            slots *= 2;
            newtokens = (sdstring_t*)sds_actualrealloc(tokens, sizeof(sdstring_t) * slots);
            if(newtokens == NULL)
            {
                goto cleanup;
            }
            tokens = newtokens;
        }
        /* search the separator */
        if((seplen == 1 && *(s + j) == sep[0]) || (memcmp(s + j, sep, seplen) == 0))
        {
            tokens[elements] = sds_makelength(s + start, j - start);
            if(tokens[elements] == NULL)
            {
                goto cleanup;
            }
            elements++;
            start = j + seplen;
            j = j + seplen - 1; /* skip the separator */
        }
    }
    /* Add the final element. We are sure there is room in the tokens array. */
    tokens[elements] = sds_makelength(s + start, len - start);
    if(tokens[elements] == NULL)
    {
        goto cleanup;
    }
    elements++;
    *count = elements;
    return tokens;

    cleanup:
    {
        for(i = 0; i < elements; i++)
        {
            sds_destroy(tokens[i]);
        }
        sds_actualfree(tokens);
        *count = 0;
        return NULL;
    }
}

/* Free the result returned by sds_splitlen(), or do nothing if 'tokens' is NULL. */
static inline void sds_freesplitres(sdstring_t* tokens, int count)
{
    if(!tokens)
    {
        return;
    }
    while(count--)
    {
        sds_destroy(tokens[count]);
    }
    sds_actualfree(tokens);
}

/* Append to the sds string "s" an escaped string representation where
 * all the non-printable characters (tested with isprint()) are turned into
 * escapes in the form "\n\r\a...." or "\x<hex-number>".
 *
 * After the call, the modified sds string is no longer valid and all the
 * references must be substituted with the new pointer returned by the call. */
static inline sdstring_t sds_appendrepr(sdstring_t s, const char* p, size_t len)
{
    s = sds_appendlen(s, "\"", 1);
    while(len--)
    {
        switch(*p)
        {
            case '\\':
            case '"':
                {
                    s = sds_appendprintf(s, "\\%c", *p);
                }
                break;
            case '\n':
                {
                    s = sds_appendlen(s, "\\n", 2);
                }
                break;
            case '\r':
                {
                    s = sds_appendlen(s, "\\r", 2);
                }
                break;
            case '\t':
                {
                    s = sds_appendlen(s, "\\t", 2);
                }
                break;
            case '\a':
                {
                    s = sds_appendlen(s, "\\a", 2);
                }
                break;
            case '\b':
                {
                    s = sds_appendlen(s, "\\b", 2);
                }
                break;
            default:
                {
                    if(isprint(*p))
                    {
                        s = sds_appendprintf(s, "%c", *p);
                    }
                    else
                    {
                        s = sds_appendprintf(s, "\\x%02x", (unsigned char)*p);
                    }
                }
                break;
        }
        p++;
    }
    return sds_appendlen(s, "\"", 1);
}

/* Helper function for sds_splitargs() that returns non zero if 'c'
 * is a valid hex digit. */
static inline int sdsutil_ishexdigit(char c)
{
    return (
        ((c >= '0') && (c <= '9')) ||
        ((c >= 'a') && (c <= 'f')) ||
        ((c >= 'A') && (c <= 'F'))
    );
}

/* Helper function for sds_splitargs() that converts a hex digit into an
 * integer from 0 to 15 */
static inline int sdsutil_hexdigittoint(char c)
{
    switch(c)
    {
        case '0':
            return 0;
        case '1':
            return 1;
        case '2':
            return 2;
        case '3':
            return 3;
        case '4':
            return 4;
        case '5':
            return 5;
        case '6':
            return 6;
        case '7':
            return 7;
        case '8':
            return 8;
        case '9':
            return 9;
        case 'a':
        case 'A':
            return 10;
        case 'b':
        case 'B':
            return 11;
        case 'c':
        case 'C':
            return 12;
        case 'd':
        case 'D':
            return 13;
        case 'e':
        case 'E':
            return 14;
        case 'f':
        case 'F':
            return 15;
        default:
            break;
    }
    return 0;
}

/* Split a line into arguments, where every argument can be in the
 * following programming-language REPL-alike form:
 *
 * foo bar "newline are supported\n" and "\xff\x00otherstuff"
 *
 * The number of arguments is stored into *argc, and an array
 * of sds is returned.
 *
 * The caller should free the resulting array of sds strings with
 * sds_freesplitres().
 *
 * Note that sds_appendrepr() is able to convert back a string into
 * a quoted string in the same format sds_splitargs() is able to parse.
 *
 * The function returns the allocated tokens on success, even when the
 * input string is empty, or NULL if the input contains unbalanced
 * quotes or closed quotes followed by non space characters
 * as in: "foo"bar or "foo'
 */
static inline sdstring_t* sds_splitargs(const char* line, int* argc)
{
    int inq;
    int insq;
    int done;
    char c;
    unsigned char byte;
    const char* p;
    char* current;
    char** vector;
    p = line;
    current = NULL;
    vector = NULL;
    *argc = 0;
    while(1)
    {
        /* skip blanks */
        while(*p && isspace(*p))
            p++;
        if(*p)
        {
            /* get a token */
            inq = 0; /* set to 1 if we are in "quotes" */
            insq = 0; /* set to 1 if we are in 'single quotes' */
            done = 0;
            if(current == NULL)
            {
                current = sds_makeempty();
            }
            while(!done)
            {
                if(inq)
                {
                    if(*p == '\\' && *(p + 1) == 'x' && sdsutil_ishexdigit(*(p + 2)) && sdsutil_ishexdigit(*(p + 3)))
                    {
                        byte = (sdsutil_hexdigittoint(*(p + 2)) * 16) + sdsutil_hexdigittoint(*(p + 3));
                        current = sds_appendlen(current, (char*)&byte, 1);
                        p += 3;
                    }
                    else if(*p == '\\' && *(p + 1))
                    {
                        p++;
                        switch(*p)
                        {
                            case 'n':
                                c = '\n';
                                break;
                            case 'r':
                                c = '\r';
                                break;
                            case 't':
                                c = '\t';
                                break;
                            case 'b':
                                c = '\b';
                                break;
                            case 'a':
                                c = '\a';
                                break;
                            default:
                                c = *p;
                                break;
                        }
                        current = sds_appendlen(current, &c, 1);
                    }
                    else if(*p == '"')
                    {
                        /* closing quote must be followed by a space or
                         * nothing at all. */
                        if(*(p + 1) && !isspace(*(p + 1)))
                        {
                            goto err;
                        }
                        done = 1;
                    }
                    else if(!*p)
                    {
                        /* unterminated quotes */
                        goto err;
                    }
                    else
                    {
                        current = sds_appendlen(current, p, 1);
                    }
                }
                else if(insq)
                {
                    if(*p == '\\' && *(p + 1) == '\'')
                    {
                        p++;
                        current = sds_appendlen(current, "'", 1);
                    }
                    else if(*p == '\'')
                    {
                        /* closing quote must be followed by a space or
                         * nothing at all. */
                        if(*(p + 1) && !isspace(*(p + 1)))
                        {
                            goto err;
                        }
                        done = 1;
                    }
                    else if(!*p)
                    {
                        /* unterminated quotes */
                        goto err;
                    }
                    else
                    {
                        current = sds_appendlen(current, p, 1);
                    }
                }
                else
                {
                    switch(*p)
                    {
                        case ' ':
                        case '\n':
                        case '\r':
                        case '\t':
                        case '\0':
                            done = 1;
                            break;
                        case '"':
                            inq = 1;
                            break;
                        case '\'':
                            insq = 1;
                            break;
                        default:
                            current = sds_appendlen(current, p, 1);
                            break;
                    }
                }
                if(*p)
                {
                    p++;
                }
            }
            /* add the token to the vector */
            vector = (char**)sds_actualrealloc(vector, ((*argc) + 1) * sizeof(char*));
            vector[*argc] = current;
            (*argc)++;
            current = NULL;
        }
        else
        {
            /* Even on empty input string return something not NULL. */
            if(vector == NULL)
            {
                vector = (char**)sds_actualmalloc(sizeof(void*));
            }
            return vector;
        }
    }

err:
    while((*argc)--)
    {
        sds_destroy(vector[*argc]);
    }
    sds_actualfree(vector);
    if(current)
    {
        sds_destroy(current);
    }
    *argc = 0;
    return NULL;
}

/* Modify the string substituting all the occurrences of the set of
 * characters specified in the 'from' string to the corresponding character
 * in the 'to' array.
 *
 * For instance: sds_mapchars(mystring, "ho", "01", 2)
 * will have the effect of turning the string "hello" into "0ell1".
 *
 * The function returns the sds string pointer, that is always the same
 * as the input pointer since no resize is needed. */
static inline sdstring_t sds_mapchars(sdstring_t s, const char* from, const char* to, size_t setlen)
{
    size_t j;
    size_t i;
    size_t l;
    l = sds_getlength(s);
    for(j = 0; j < l; j++)
    {
        for(i = 0; i < setlen; i++)
        {
            if(s[j] == from[i])
            {
                s[j] = to[i];
                break;
            }
        }
    }
    return s;
}

/* Join an array of C strings using the specified separator (also a C string).
 * Returns the result as an sds string. */
static inline sdstring_t sds_join(char** argv, int argc, char* sep)
{
    int j;
    sdstring_t join;
    join = sds_makeempty();
    for(j = 0; j < argc; j++)
    {
        join = sds_append(join, argv[j]);
        if(j != argc - 1)
        {
            join = sds_append(join, sep);
        }
    }
    return join;
}

/* Like sds_join, but joins an array of SDS strings. */
static inline sdstring_t sds_joinsds(sdstring_t* argv, int argc, const char* sep, size_t seplen)
{
    int j;
    sdstring_t join;
    join = sds_makeempty();
    for(j = 0; j < argc; j++)
    {
        join = sds_appendsds(join, argv[j]);
        if(j != argc - 1)
        {
            join = sds_appendlen(join, sep, seplen);
        }
    }
    return join;
}

/* Wrappers to the allocators used by SDS. Note that SDS will actually
 * just use the macros defined into sds_getallocatedsize.h in order to avoid to pay
 * the overhead of function calls. Here we define these wrappers only for
 * the programs SDS is linked to, if they want to touch the SDS internals
 * even if they use a different allocator. */
static inline void* sds_memmalloc(size_t size)
{
    return sds_actualmalloc(size);
}

static inline void* sds_memrealloc(void* ptr, size_t size)
{
    return sds_actualrealloc(ptr, size);
}

static inline void sds_memfree(void* ptr)
{
    sds_actualfree(ptr);
}

