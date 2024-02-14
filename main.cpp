
#ifdef __GNUC__
    //#pragma GCC poison alloca
#endif


#include <stdexcept>
inline void * operator new (std::size_t)
{
    extern void* calls_to_operator_new_are_not_allowed();
    return calls_to_operator_new_are_not_allowed();
}

#if defined(_WIN32)
    #include <fcntl.h>
    #include <io.h>
#endif

/* needed when compiling with wasi. must be defined *before* signal.h is included! */
#if defined(__wasi__)
    #define _WASI_EMULATED_SIGNAL
#endif
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <type_traits>
#include <new>
#include <source_location>

/*
* needed because clang + wasi (wasi headers, specifically) don't seem to define these.
* note: keep these below stdlib.h, so as to check whether __BEGIN_DECLS is defined.
*/
#if defined(__wasi__) && !defined(__BEGIN_DECLS)
    #define __BEGIN_DECLS
    #define __END_DECLS
    #define __THROWNL
    #define __THROW
    #define __nonnull(...)
#endif

#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <fcntl.h>
    #include <io.h>
    #include <sys/utime.h>
    #define NEON_PLAT_ISWINDOWS
#else
    #if defined(__wasi__)
        #define NEON_PLAT_ISWASM
    #endif
    #include <sys/time.h>
    #include <utime.h>
    #include <dirent.h>
    #include <dlfcn.h>
    #include <unistd.h>
    #if defined(__linux__) || defined(__CYGWIN__) || defined(__MINGW32_MAJOR_VERSION)
        #include <libgen.h>
        #include <limits.h>
    #endif
    #define NEON_PLAT_ISLINUX
#endif

#include "os.h"


#if 0 //defined(__GNUC__)
    #define NEON_FORCEINLINE __attribute__((always_inline)) inline
#else
    #define NEON_FORCEINLINE inline
#endif

#define NEON_CFG_FILEEXT ".nn"

/* global debug mode flag */
#define NEON_CFG_BUILDDEBUGMODE 0

#if NEON_CFG_BUILDDEBUGMODE == 1
    /*
    * will print information about get/set operations on neon::HashTable.
    * very verbose!
    */
    #define NEON_CFG_DEBUGTABLE 0

    /*
    * will print information about the garbage collector.
    * very verbose!
    */
    #define NEON_CFG_DEBUGGC 1
#endif

/*
* initial amount of frames (will grow dynamically if needed)
* *not* recommended to go below 64!
*/
#define NEON_CFG_INITFRAMECOUNT (64)

/*
* initial amount of stack values (will grow dynamically if needed)
* *not* recommended to go below 64!
*/
#define NEON_CFG_INITSTACKCOUNT (64 * 1)

/* max number of function parameters */
#define NEON_CFG_ASTMAXFUNCPARAMS 32

/* how deep template strings can be nested (i.e., "foo${getBar("quux${getBonk("...")}")}") */
#define NEON_CFG_ASTMAXSTRTPLDEPTH 8

/* how many catch() clauses per try statement */
#define NEON_CFG_MAXEXCEPTHANDLERS 16

/*
// Maximum load factor of 12/14
// see: https://engineering.fb.com/2019/04/25/developer-tools/f14/
*/
#define NEON_CFG_MAXTABLELOAD 0.85714286

/* how much memory can be allocated before the garbage collector kicks in */
#define NEON_CFG_DEFAULTGCSTART (1024 * 1024)

/* growth factor for GC heap objects */
#define NEON_CFG_GCHEAPGROWTHFACTOR 1.25

#define NEON_INFO_COPYRIGHT "based on the Blade Language, Copyright (c) 2021 - 2023 Ore Richard Muyiwa"

/* check for exact number of arguments $d */
#define NEON_ARGS_CHECKCOUNT(chp, d) \
    if((chp)->argc != (d)) \
    { \
        return state->raiseFromFunction(args, "%s() expects %d arguments, %d given", (chp)->funcname, d, (chp)->argc); \
    }

/* check for miminum args $d ($d ... n) */
#define NEON_ARGS_CHECKMINARG(chp, d) \
    if((chp)->argc < (d)) \
    { \
        return state->raiseFromFunction(args, "%s() expects minimum of %d arguments, %d given", (chp)->funcname, d, (chp)->argc); \
    }

/* check for range of args ($low .. $up) */
#define NEON_ARGS_CHECKCOUNTRANGE(chp, low, up) \
    if((chp)->argc < (low) || (chp)->argc > (up)) \
    { \
        return state->raiseFromFunction(args, "%s() expects between %d and %d arguments, %d given", (chp)->funcname, low, up, (chp)->argc); \
    }

/* check for argument at index $i for $type, where $type is a Value::is*() function */
#if 1
    #define NEON_ARGS_CHECKTYPE(chp, i, __callfn__) \
        if(!(chp)->argv[i].__callfn__()) \
        { \
            return state->raiseFromFunction(args, "%s() expects argument %d as " #__callfn__ ", %s given", (chp)->funcname, (i) + 1, neon::Value::Typename((chp)->argv[i])); \
        }

#else
    #define NEON_ARGS_CHECKTYPE(chp, i, type) \
        { \
        }
#endif

/* reject argument $type at $index */
#define NEON_ARGS_REJECTTYPE(chp, __callfn__, index) \
    if((chp)->argv[index].__callfn__()) \
    { \
        return state->raiseFromFunction(args, "invalid type %s() as argument %d in %s()", neon::Value::Typename((chp)->argv[index]), (index) + 1, (chp)->funcname); \
    }

#if defined(__GNUC__)
    #define NEON_LIKELY(x) \
        __builtin_expect(!!(x), 1)

    #define NEON_UNLIKELY(x) \
        __builtin_expect(!!(x), 0)
#else
    #define NEON_LIKELY(x) x
    #define NEON_UNLIKELY(x) x
#endif


#define NEON_APIDEBUG(state, ...) \
    if((NEON_UNLIKELY((state)->m_conf.enableapidebug))) \
    { \
        nn_state_apidebug(state, __FUNCTION__, __VA_ARGS__); \
    }

#define NEON_ASTDEBUG(state, ...) \
    if((NEON_UNLIKELY((state)->m_conf.enableastdebug))) \
    { \
        nn_state_astdebug(state, __FUNCTION__, __VA_ARGS__); \
    }

namespace neon
{

    enum class Status
    {
        OK,
        FAIL_COMPILE,
        FAIL_RUNTIME,
    };

    struct /**/Blob;
    struct /**/State;
    struct /**/HashTable;
    struct /**/Printer;
    struct /**/FormatInfo;
    struct /**/Token;
    struct /**/Lexer;
    struct /**/Parser;
    struct /**/RegModule;
    struct /**/Arguments;
    struct /**/Value;
    struct /**/ClassObject;
    struct /**/ClassInstance;
    struct /**/File;
    struct /**/Object;
    struct /**/String;
    struct /**/Module;
    struct /**/ValArray;
    struct /**/FuncClosure;
    struct /**/ScopeUpvalue;
    struct /**/Dictionary;
    struct /**/Array;
    struct /**/FuncScript;
    struct /**/FuncNative;
    struct /**/Range;
    struct /**/Userdata;
    struct /**/FuncBound;
    struct /**/VarSwitch;
    struct /**/Instruction;

    class Memory
    {
        public:
            static void* osRealloc(void* ptr, size_t size)
            {
                return realloc(ptr, size);
            }

            static void* osMalloc(size_t size)
            {
                return malloc(size);
            }

            static void* osCalloc(size_t count, size_t size)
            {
                return calloc(count, size);
            }

            static void osFree(void* ptr)
            {
                free(ptr);
            }

            template<typename Type, typename... ArgsT>
            static Type* create(ArgsT&&... args)
            {
                Type* rt;
                Type* buf;
                buf = (Type*)osMalloc(sizeof(Type));
                rt = new(buf) Type(args...);
                return rt;
            }

            template<typename Type>
            static void destroy(Type* ptr)
            {
                ptr->~Type();
                osFree(ptr);
            }
    };

#define NEON_UTIL_MIN(x, y) ((x) < (y) ? (x) : (y))
#define NEON_UTIL_MAX(x, y) ((x) > (y) ? (x) : (y))
#define NEON_UTIL_STRBUFBOUNDSCHECKINSERT(sbuf, pos) sbuf->callBoundsCheckInsert(pos, __FILE__, __LINE__, __func__)
#define NEON_UTIL_STRBUFBOUNDSCHECKREADRANGE(sbuf, start, len) sbuf->callBoundsCheckReadRange(start, len, __FILE__, __LINE__, __func__)

    namespace Util
    {
        enum
        {
            MT_STATE_SIZE = 624,
        };

        void MTSeed(uint32_t seed, uint32_t* binst, uint32_t* index)
        {
            uint32_t i;
            binst[0] = seed;
            for(i = 1; i < MT_STATE_SIZE; i++)
            {
                binst[i] = (uint32_t)(1812433253UL * (binst[i - 1] ^ (binst[i - 1] >> 30)) + i);
            }
            *index = MT_STATE_SIZE;
        }

        uint32_t MTGenerate(uint32_t* binst, uint32_t* index)
        {
            uint32_t i;
            uint32_t y;
            if(*index >= MT_STATE_SIZE)
            {
                for(i = 0; i < MT_STATE_SIZE - 397; i++)
                {
                    y = (binst[i] & 0x80000000) | (binst[i + 1] & 0x7fffffff);
                    binst[i] = binst[i + 397] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
                }
                for(; i < MT_STATE_SIZE - 1; i++)
                {
                    y = (binst[i] & 0x80000000) | (binst[i + 1] & 0x7fffffff);
                    binst[i] = binst[i + (397 - MT_STATE_SIZE)] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
                }
                y = (binst[MT_STATE_SIZE - 1] & 0x80000000) | (binst[0] & 0x7fffffff);
                binst[MT_STATE_SIZE - 1] = binst[396] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
                *index = 0;
            }
            y = binst[*index];
            *index = *index + 1;
            y = y ^ (y >> 11);
            y = y ^ ((y << 7) & 0x9d2c5680);
            y = y ^ ((y << 15) & 0xefc60000);
            y = y ^ (y >> 18);
            return y;
        }

        double MTRand(double lowerlimit, double upperlimit)
        {
            double randnum;
            uint32_t randval;
            struct timeval tv;
            static uint32_t mtstate[MT_STATE_SIZE];
            static uint32_t mtindex = MT_STATE_SIZE + 1;
            if(mtindex >= MT_STATE_SIZE)
            {
                osfn_gettimeofday(&tv, nullptr);
                MTSeed((uint32_t)(1000000 * tv.tv_sec + tv.tv_usec), mtstate, &mtindex);
            }
            randval = MTGenerate(mtstate, &mtindex);
            randnum = lowerlimit + ((double)randval / UINT32_MAX) * (upperlimit - lowerlimit);
            return randnum;
        }

        NEON_FORCEINLINE size_t growCapacity(size_t capacity)
        {
            if(capacity < 4)
            {
                return 4;
            }
            return (capacity * 2);
        }

        uint32_t hashBits(uint64_t hash)
        {
            /*
            // From v8's ComputeLongHash() which in turn cites:
            // Thomas Wang, Integer Hash Functions.
            // http://www.concentric.net/~Ttwang/tech/inthash.htm
            // hash = (hash << 18) - hash - 1;
            */
            hash = ~hash + (hash << 18);
            hash = hash ^ (hash >> 31);
            /* hash = (hash + (hash << 2)) + (hash << 4); */
            hash = hash * 21;
            hash = hash ^ (hash >> 11);
            hash = hash + (hash << 6);
            hash = hash ^ (hash >> 22);
            return (uint32_t)(hash & 0x3fffffff);
        }

        uint32_t hashDouble(double value)
        {
            union DoubleHashUnion
            {
                uint64_t bits;
                double num;
            };
            DoubleHashUnion bits;
            bits.num = value;
            return hashBits(bits.bits);
        }

        uint32_t hashString(const char* key, int length)
        {
            uint32_t hash;
            const char* be;
            hash = 2166136261u;
            be = key + length;
            while(key < be)
            {
                hash = (hash ^ *key++) * 16777619;
            }
            return hash;
            /* return siphash24(127, 255, key, length); */
        }

        struct Utf8Iterator
        {
            public:
                NEON_FORCEINLINE static uint32_t convertToCodepoint(const char* character, uint8_t size)
                {
                    uint8_t i;
                    static uint32_t codepoint = 0;
                    static const uint8_t g_utf8iter_table_unicode[] = { 0, 0, 0x1F, 0xF, 0x7, 0x3, 0x1 };
                    if(size == 0)
                    {
                        return 0;
                    }
                    if(character == NULL)
                    {
                        return 0;
                    }
                    if(character[0] == 0)
                    {
                        return 0;
                    }
                    if(size == 1)
                    {
                        return character[0];
                    }
                    codepoint = g_utf8iter_table_unicode[size] & character[0];
                    for(i = 1; i < size; i++)
                    {
                        codepoint = codepoint << 6;
                        codepoint = codepoint | (character[i] & 0x3F);
                    }
                    return codepoint;
                }

                /* calculate the number of bytes a UTF8 character occupies in a string. */
                NEON_FORCEINLINE static uint8_t getCharSize(const char* character)
                {
                    if(character == NULL)
                    {
                        return 0;
                    }
                    if(character[0] == 0)
                    {
                        return 0;
                    }
                    if((character[0] & 0x80) == 0)
                    {
                        return 1;
                    }
                    else if((character[0] & 0xE0) == 0xC0)
                    {
                        return 2;
                    }
                    else if((character[0] & 0xF0) == 0xE0)
                    {
                        return 3;
                    }
                    else if((character[0] & 0xF8) == 0xF0)
                    {
                        return 4;
                    }
                    else if((character[0] & 0xFC) == 0xF8)
                    {
                        return 5;
                    }
                    else if((character[0] & 0xFE) == 0xFC)
                    {
                        return 6;
                    }
                    return 0;
                }


            public:
                const char* m_plainstr;
                uint32_t m_codepoint;
                /* character size in bytes */
                uint8_t m_charsize;
                /* current character position */
                uint32_t m_currpos;
                /* next character position */
                uint32_t m_nextpos;
                /* number of counter characters currently */
                uint32_t m_currcount;
                /* strlen() */
                uint32_t m_plainlen;

            public:
                /* allows you to set a custom length. */
                NEON_FORCEINLINE Utf8Iterator(const char* ptr, uint32_t length)
                {
                    m_plainstr = ptr;
                    m_codepoint = 0;
                    m_currpos = 0;
                    m_nextpos = 0;
                    m_currcount = 0;
                    m_plainlen = length;
                }

                /* returns 1 if there is a character in the next position. If there is not, return 0. */
                NEON_FORCEINLINE uint8_t next()
                {
                    const char* pointer;
                    if(m_plainstr == NULL)
                    {
                        return 0;
                    }
                    if(m_nextpos < m_plainlen)
                    {
                        m_currpos = m_nextpos;
                        /* Set Current Pointer */
                        pointer = m_plainstr + m_nextpos;
                        m_charsize = getCharSize(pointer);
                        if(m_charsize == 0)
                        {
                            return 0;
                        }
                        m_nextpos = m_nextpos + m_charsize;
                        m_codepoint = convertToCodepoint(pointer, m_charsize);
                        if(m_codepoint == 0)
                        {
                            return 0;
                        }
                        m_currcount++;
                        return 1;
                    }
                    m_currpos = m_nextpos;
                    return 0;
                }

                /* return current character in UFT8 - no same that .m_codepoint (not codepoint/unicode) */
                NEON_FORCEINLINE const char* getCharStr()
                {
                    uint8_t i;
                    const char* pointer;
                    static char str[10];
                    str[0] = '\0';
                    if(m_plainstr == NULL)
                    {
                        return str;
                    }
                    if(m_charsize == 0)
                    {
                        return str;
                    }
                    if(m_charsize == 1)
                    {
                        str[0] = m_plainstr[m_currpos];
                        str[1] = '\0';
                        return str;
                    }
                    pointer = m_plainstr + m_currpos;
                    for(i = 0; i < m_charsize; i++)
                    {
                        str[i] = pointer[i];
                    }
                    str[m_charsize] = '\0';
                    return str;
                }
        };

        /* returns the number of bytes contained in a unicode character */
        int utf8NumBytes(int value)
        {
            if(value < 0)
            {
                return -1;
            }
            if(value <= 0x7f)
            {
                return 1;
            }
            if(value <= 0x7ff)
            {
                return 2;
            }
            if(value <= 0xffff)
            {
                return 3;
            }
            if(value <= 0x10ffff)
            {
                return 4;
            }
            return 0;
        }

        char* utf8Encode(State* state, unsigned int code, size_t* dlen)
        {
            int count;
            char* chars;
            (void)state;
            *dlen = 0;
            count = utf8NumBytes((int)code);
            if(count > 0)
            {
                *dlen = count;
                //chars = (char*)State::GC::allocate(state, sizeof(char), (size_t)count + 1);
                chars = (char*)Memory::osMalloc(sizeof(char)*(count+1));
                if(chars != nullptr)
                {
                    if(code <= 0x7F)
                    {
                        chars[0] = (char)(code & 0x7F);
                        chars[1] = '\0';
                    }
                    else if(code <= 0x7FF)
                    {
                        /* one continuation byte */
                        chars[1] = (char)(0x80 | (code & 0x3F));
                        code = (code >> 6);
                        chars[0] = (char)(0xC0 | (code & 0x1F));
                    }
                    else if(code <= 0xFFFF)
                    {
                        /* two continuation bytes */
                        chars[2] = (char)(0x80 | (code & 0x3F));
                        code = (code >> 6);
                        chars[1] = (char)(0x80 | (code & 0x3F));
                        code = (code >> 6);
                        chars[0] = (char)(0xE0 | (code & 0xF));
                    }
                    else if(code <= 0x10FFFF)
                    {
                        /* three continuation bytes */
                        chars[3] = (char)(0x80 | (code & 0x3F));
                        code = (code >> 6);
                        chars[2] = (char)(0x80 | (code & 0x3F));
                        code = (code >> 6);
                        chars[1] = (char)(0x80 | (code & 0x3F));
                        code = (code >> 6);
                        chars[0] = (char)(0xF0 | (code & 0x7));
                    }
                    else
                    {
                        /* unicode replacement character */
                        chars[2] = (char)0xEF;
                        chars[1] = (char)0xBF;
                        chars[0] = (char)0xBD;
                    }
                    return chars;
                }
            }
            return nullptr;
        }

        int utf8Decode(const uint8_t* bytes, uint32_t length)
        {
            int value;
            uint32_t remainingbytes;
            /* Single byte (i.e. fits in ASCII). */
            if(*bytes <= 0x7f)
            {
                return *bytes;
            }
            if((*bytes & 0xe0) == 0xc0)
            {
                /* Two byte sequence: 110xxxxx 10xxxxxx. */
                value = *bytes & 0x1f;
                remainingbytes = 1;
            }
            else if((*bytes & 0xf0) == 0xe0)
            {
                /* Three byte sequence: 1110xxxx	 10xxxxxx 10xxxxxx. */
                value = *bytes & 0x0f;
                remainingbytes = 2;
            }
            else if((*bytes & 0xf8) == 0xf0)
            {
                /* Four byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx. */
                value = *bytes & 0x07;
                remainingbytes = 3;
            }
            else
            {
                /* Invalid UTF-8 sequence. */
                return -1;
            }
            /* Don't read past the end of the buffer on truncated UTF-8. */
            if(remainingbytes > length - 1)
            {
                return -1;
            }
            while(remainingbytes > 0)
            {
                bytes++;
                remainingbytes--;
                /* Remaining bytes must be of form 10xxxxxx. */
                if((*bytes & 0xc0) != 0x80)
                {
                    return -1;
                }
                value = value << 6 | (*bytes & 0x3f);
            }
            return value;
        }

        char* utf8CodePoint(const char* str, char* outcodepoint)
        {
            if(0xf0 == (0xf8 & str[0]))
            {
                /* 4 byte utf8 codepoint */
                *outcodepoint = (
                    ((0x07 & str[0]) << 18) |
                    ((0x3f & str[1]) << 12) |
                    ((0x3f & str[2]) << 6) |
                    (0x3f & str[3])
                );
                str += 4;
            }
            else if(0xe0 == (0xf0 & str[0]))
            {
                /* 3 byte utf8 codepoint */
                *outcodepoint = (
                    ((0x0f & str[0]) << 12) |
                    ((0x3f & str[1]) << 6) |
                    (0x3f & str[2])
                );
                str += 3;
            }
            else if(0xc0 == (0xe0 & str[0]))
            {
                /* 2 byte utf8 codepoint */
                *outcodepoint = (
                    ((0x1f & str[0]) << 6) |
                    (0x3f & str[1])
                );
                str += 2;
            }
            else
            {
                /* 1 byte utf8 codepoint otherwise */
                *outcodepoint = str[0];
                str += 1;
            }
            return (char*)str;
        }

        char* utf8Find(const char* haystack, size_t hslen, const char* needle, size_t nlen)
        {
            size_t hsn;
            size_t hsi;
            char throwawaycodepoint;
            const char* n;
            const char* maybematch;
            throwawaycodepoint = 0;
            /* if needle has no utf8 codepoints before the null terminating
             * byte then return haystack */
            if('\0' == *needle)
            {
                return (char*)haystack;
            }
            hsi = 0;
            hsn = 0;
            while(hsi < hslen)
            {
                hsi++;
                maybematch = &haystack[hsi];
                n = needle;
                while(*haystack == *n && (hsi < hslen && hsn < nlen))
                {
                    hsi++;
                    hsn++;
                    n++;
                    haystack++;
                }
                if('\0' == *n)
                {
                    /* we found the whole utf8 string for needle in haystack at
                     * maybematch, so return it */
                    return (char*)maybematch;
                }
                else
                {
                    /* h could be in the middle of an unmatching utf8 codepoint,
                     * so we need to march it on to the next character beginning
                     * starting from the current character */
                    haystack = utf8CodePoint(maybematch, &throwawaycodepoint);
                }
            }
            /* no match */
            return nullptr;
        }

        char* strToUpperInplace(char* str, size_t length)
        {
            int c;
            size_t i;
            for(i=0; i<length; i++)
            {
                c = str[i];
                str[i] = toupper(c);
            }
            return str;
        }

        char* strToLowerInplace(char* str, size_t length)
        {
            int c;
            size_t i;
            for(i=0; i<length; i++)
            {
                c = str[i];
                str[i] = toupper(c);
            }
            return str;
        }

        char* strCopy(State* state, const char* src, size_t len)
        {
            char* buf;
            (void)state;
            buf = (char*)Memory::osMalloc(sizeof(char) * (len+1));
            if(buf == nullptr)
            {
                return nullptr;
            }
            memset(buf, 0, len+1);
            memcpy(buf, src, len);
            return buf;
        }

        char* strCopy(State* state, const char* src)
        {
            return strCopy(state, src, strlen(src));
        }

        char* fileReadHandle(State* state, FILE* hnd, size_t* dlen)
        {
            long rawtold;
            /*
            * the value returned by ftell() may not necessarily be the same as
            * the amount that can be read.
            * since we only ever read a maximum of $toldlen, there will
            * be no memory trashing.
            */
            size_t toldlen;
            size_t actuallen;
            char* buf;
            (void)state;
            if(fseek(hnd, 0, SEEK_END) == -1)
            {
                return nullptr;
            }
            if((rawtold = ftell(hnd)) == -1)
            {
                return nullptr;
            }
            toldlen = rawtold;
            if(fseek(hnd, 0, SEEK_SET) == -1)
            {
                return nullptr;
            }
            //buf = (char*)State::GC::allocate(state, sizeof(char), toldlen + 1);
            buf = (char*)Memory::osMalloc(sizeof(char)*(toldlen+1));
            memset(buf, 0, toldlen+1);
            if(buf != nullptr)
            {
                actuallen = fread(buf, sizeof(char), toldlen, hnd);
                /*
                // optionally, read remainder:
                size_t tmplen;
                if(actuallen < toldlen)
                {
                    tmplen = actuallen;
                    actuallen += fread(buf+tmplen, sizeof(char), actuallen-toldlen, hnd);
                    ...
                }
                // unlikely to be necessary, so not implemented.
                */
                if(dlen != nullptr)
                {
                    *dlen = actuallen;
                }
                return buf;
            }
            return nullptr;
        }

        char* fileReadFile(State* state, const char* filename, size_t* dlen)
        {
            char* b;
            FILE* fh;
            fh = fopen(filename, "rb");
            if(fh == nullptr)
            {
                return nullptr;
            }
            #if defined(NEON_PLAT_ISWINDOWS)
                _setmode(fileno(fh), _O_BINARY);
            #endif
            b = fileReadHandle(state, fh, dlen);
            fclose(fh);
            return b;
        }

        struct StrBuffer
        {
            public:
                static size_t roundUpToPowOf64(unsigned long long x)
                {
                    /* long long >=64 bits guaranteed in C99 */
                    --x;
                    x |= x >> 1;
                    x |= x >> 2;
                    x |= x >> 4;
                    x |= x >> 8;
                    x |= x >> 16;
                    x |= x >> 32;
                    ++x;
                    return x;
                }

                static void checkBufCapacity(char** buf, size_t* sizeptr, size_t len)
                {
                    /* for nul byte */
                    len++;
                    if(*sizeptr < len)
                    {
                        *sizeptr = roundUpToPowOf64(len);
                        /* fprintf(stderr, "sizeptr=%ld\n", *sizeptr); */
                        if((*buf = (char*)Memory::osRealloc(*buf, *sizeptr)) == NULL)
                        {
                            fprintf(stderr, "[%s:%i] Out of memory\n", __FILE__, __LINE__);
                            abort();
                        }
                    }
                }

                static bool replaceCharHelper(char *dest, const char *src, size_t srclen, int findme, const char* sstr, size_t slen, size_t maxlen, size_t* dlen)
                {
                    /* ch(ar) at pos(ition) */
                    int chatpos;
                    /* printf("'%p' '%s' %c\n", dest, src, findme); */
                    if(*src == findme)
                    {
                        if(slen > maxlen)
                        {
                            return false;
                        }
                        if(!replaceCharHelper(dest + slen, src + 1, srclen, findme, sstr, slen, maxlen - slen, dlen))
                        {
                            return false;
                        }
                        memcpy(dest, sstr, slen);
                        *dlen += slen;
                        return true;
                    }
                    if(maxlen == 0)
                    {
                        return false;
                    }
                    chatpos = *src;
                    if(*src)
                    {
                        *dlen += 1;
                        if(!replaceCharHelper(dest + 1, src + 1, srclen, findme, sstr, slen, maxlen - 1, dlen))
                        {
                            return false;
                        }
                    }
                    *dest = chatpos;
                    return true;
                }

                static size_t replaceCharInPlace(char* target, size_t tgtlen, int findme, const char* sstr, size_t slen, size_t maxlen)
                {
                    size_t nlen;
                    if(findme == 0)
                    {
                        return 0;
                    }
                    if(maxlen == 0)
                    {
                        return 0;
                    }
                    if(*sstr == 0)
                    {
                        /* Insure target does not shrink. */
                        return 0;
                    }
                    nlen = 0;
                    replaceCharHelper(target, target, tgtlen, findme, sstr, slen, maxlen - 1, &nlen);
                    return nlen;
                }

                /* via: https://stackoverflow.com/a/32413923 */
                NEON_FORCEINLINE static void replaceFullInPlace(char* target, size_t tgtlen, const char *fstr, size_t flen, const char *sstr, size_t slen)
                {
                    const char *p;
                    const char *tmp;
                    char *inspoint;
                    char buffer[1024] = {0};
                    (void)tgtlen;
                    inspoint = &buffer[0];
                    tmp = target;
                    while(true)
                    {
                        p = strstr(tmp, fstr);
                        /* walked past last occurrence of fstr; copy remaining part */
                        if (p == NULL)
                        {
                            strcpy(inspoint, tmp);
                            break;
                        }
                        /* copy part before fstr */
                        memcpy(inspoint, tmp, p - tmp);
                        inspoint += p - tmp;
                        /* copy sstr string */
                        memcpy(inspoint, sstr, slen);
                        inspoint += slen;
                        /* adjust pointers, move on */
                        tmp = p + flen;
                    }
                    /* write altered string back to target */
                    strcpy(target, buffer);
                }

                NEON_FORCEINLINE static size_t strReplaceCount(const char* str, size_t slen, const char* fstr, size_t flen, size_t slen)
                {
                    size_t i;
                    size_t count;
                    size_t total;
                    (void)total;
                    total = slen;
                    count = 0;
                    for(i=0; i<slen; i++)
                    {
                        if(str[i] == fstr[0])
                        {
                            if((i + flen) < slen)
                            {
                                if(memcmp(&str[i], fstr, flen) == 0)
                                {
                                    count++;
                                    total += slen;
                                }
                            }
                        }
                    }
                    if(count == 0)
                    {
                        return 0;
                    }
                    return slen + 1;
                }

                /*
                // Removes \r and \n from the ends of a string and returns the new length
                */
                NEON_FORCEINLINE static size_t strChomp(char* str, size_t len)
                {
                    while(len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n'))
                    {
                        len--;
                    }
                    str[len] = '\0';
                    return len;
                }

                /*
                // Reverse a string region
                */
                NEON_FORCEINLINE static void strReverseRegion(char* str, size_t length)
                {
                    char *a;
                    char* b;
                    char tmp;
                    a = str;
                    b = str + length - 1;
                    while(a < b)
                    {
                        tmp = *a;
                        *a = *b;
                        *b = tmp;
                        a++;
                        b--;
                    }
                }

                /*
                 * Integer to string functions adapted from:
                 *   https://www.facebook.com/notes/facebook-engineering/three-optimization-tips-for-c/10151361643253920
                 */
                enum
                {
                    DYN_STRCONST_P01 = 10,
                    DYN_STRCONST_P02 = 100,
                    DYN_STRCONST_P03 = 1000,
                    DYN_STRCONST_P04 = 10000,
                    DYN_STRCONST_P05 = 100000,
                    DYN_STRCONST_P06 = 1000000,
                    DYN_STRCONST_P07 = 10000000,
                    DYN_STRCONST_P08 = 100000000,
                    DYN_STRCONST_P09 = 1000000000,
                    DYN_STRCONST_P10 = 10000000000,
                    DYN_STRCONST_P11 = 100000000000,
                    DYN_STRCONST_P12 = 1000000000000,
                };

                /**
                 * Return number of digits required to represent `num` in base 10.
                 * Uses binary search to find number.
                 * Examples:
                 *   numOfDigits(0)   = 1
                 *   numOfDigits(1)   = 1
                 *   numOfDigits(10)  = 2
                 *   numOfDigits(123) = 3
                 */
                NEON_FORCEINLINE static size_t numOfDigits(unsigned long v)
                {
                    if(v < DYN_STRCONST_P01)
                    {
                        return 1;
                    }
                    if(v < DYN_STRCONST_P02)
                    {
                        return 2;
                    }
                    if(v < DYN_STRCONST_P03)
                    {
                        return 3;
                    }
                    if(v < DYN_STRCONST_P12)
                    {
                        if(v < DYN_STRCONST_P08)
                        {
                            if(v < DYN_STRCONST_P06)
                            {
                                if(v < DYN_STRCONST_P04)
                                {
                                    return 4;
                                }
                                return 5 + (v >= DYN_STRCONST_P05);
                            }
                            return 7 + (v >= DYN_STRCONST_P07);
                        }
                        if(v < DYN_STRCONST_P10)
                        {
                            return 9 + (v >= DYN_STRCONST_P09);
                        }
                        return 11 + (v >= DYN_STRCONST_P11);
                    }
                    return 12 + numOfDigits(v / DYN_STRCONST_P12);
                }


            public:
                char* m_data = nullptr;

                /* total length of this buffer */
                uint64_t m_length = 0;

                /* capacity should be >= length+1 to allow for \0 */
                uint64_t m_capacity = 0;

            private:
                /* Convert integers to string to append */
                NEON_FORCEINLINE bool appendNumULong(unsigned long value)
                {
                    size_t v;
                    size_t pos;
                    size_t numdigits;
                    char* dst;
                    /* Append two digits at a time */
                    static const char* digits = (
                        "0001020304050607080910111213141516171819"
                        "2021222324252627282930313233343536373839"
                        "4041424344454647484950515253545556575859"
                        "6061626364656667686970717273747576777879"
                        "8081828384858687888990919293949596979899"
                    );
                    numdigits = numOfDigits(value);
                    pos = numdigits - 1;
                    ensureCapacity(m_length + numdigits);
                    dst = m_data + m_length;
                    while(value >= 100)
                    {
                        v = value % 100;
                        value /= 100;
                        dst[pos] = digits[v * 2 + 1];
                        dst[pos - 1] = digits[v * 2];
                        pos -= 2;
                    }
                    /* Handle last 1-2 digits */
                    if(value < 10)
                    {
                        dst[pos] = '0' + value;
                    }
                    else
                    {
                        dst[pos] = digits[value * 2 + 1];
                        dst[pos - 1] = digits[value * 2];
                    }
                    m_length += numdigits;
                    m_data[m_length] = '\0';
                    return true;
                }

                NEON_FORCEINLINE bool appendNumLong(long value)
                {
                    if(value < 0)
                    {
                        append('-');
                        value = -value;
                    }
                    return appendNumULong(value);
                }

                NEON_FORCEINLINE bool appendNumInt(int value)
                {
                    return appendNumLong(value);
                }

            public:
                NEON_FORCEINLINE void callBoundsCheckInsert(size_t pos, const char* file, int line, const char* func) const
                {
                    if(pos > this->m_length)
                    {
                        fprintf(stderr, "%s:%i:%s() - out of bounds error [index: %zu, num_of_bits: %zu]\n",
                                file, line, func, pos, this->m_length);
                        errno = EDOM;
                        abort();
                    }
                }

                /* Bounds check when reading a range (start+len < strlen is valid) */
                NEON_FORCEINLINE void callBoundsCheckReadRange(size_t start, size_t len, const char* file, int line, const char* func) const
                {
                    if(start + len > this->m_length)
                    {
                        fprintf(stderr,"%s:%i:%s() - out of bounds error [start: %zu; length: %zu; strlen: %zu; buf:%.*s%s]\n",
                                file, line, func, start, len, this->m_length, (int)NEON_UTIL_MIN(5, this->m_length), this->m_data, this->m_length > 5 ? "..." : "");
                        errno = EDOM;
                        abort();
                    }
                }

                /* Ensure capacity for len characters plus '\0' character - exits on FAILURE */
                NEON_FORCEINLINE void ensureCapacity(uint64_t len)
                {
                    checkBufCapacity(&this->m_data, &this->m_capacity, len);
                }

            private:
                NEON_FORCEINLINE void resetVars()
                {
                    this->m_length = 0;
                    this->m_capacity = 0;
                    this->m_data = nullptr;
                }

                NEON_FORCEINLINE void initEmpty(size_t len)
                {
                    resetVars();
                    this->m_length = len;
                    this->m_capacity = roundUpToPowOf64(len + 1);
                    this->m_data = (char*)Memory::osMalloc(this->m_capacity);
                    this->m_data[0] = '\0';
                }


            public:
                NEON_FORCEINLINE StrBuffer()
                {
                    initEmpty(0);
                }

                NEON_FORCEINLINE StrBuffer(size_t len)
                {
                    (void)len;
                    //initEmpty(len);
                    initEmpty(0);
                }

                /*
                // Copy a string or existing string buffer
                */
                NEON_FORCEINLINE StrBuffer(const char* str, size_t slen)
                {
                    initEmpty(slen + 1);
                    memcpy(this->m_data, str, slen);
                    this->m_length = slen;
                    this->m_data[slen] = '\0';
                }

                NEON_FORCEINLINE ~StrBuffer()
                {
                    destroy();
                }

                NEON_FORCEINLINE uint64_t size() const
                {
                    return m_length;
                }

                NEON_FORCEINLINE uint64_t length() const
                {
                    return m_length;
                }

                NEON_FORCEINLINE const char* data() const
                {
                    return m_data;
                }

                bool destroy()
                {
                    if(this->m_data != nullptr)
                    {
                        Memory::osFree(this->m_data);
                        this->m_data = nullptr;
                        resetVars();
                    }
                    return true;
                }

                /*
                * Clear the content of an existing StrBuffer (sets size to 0)
                * but keeps capacity intact.
                */
                void reset()
                {
                    size_t olen;
                    if(this->m_data != nullptr)
                    {
                        olen = this->m_length;
                        this->m_length = 0;
                        memset(this->m_data, 0, olen);
                        this->m_data[0] = '\0';
                    }
                }

                /*
                // Resize the buffer to have capacity to hold a string of m_length newlen
                // (+ a null terminating character).  Can also be used to downsize the buffer's
                // memory usage.  Returns 1 on success, 0 on failure.
                */
                bool resize(size_t newlen)
                {
                    size_t ncap;
                    char* newbuf;
                    ncap = roundUpToPowOf64(newlen + 1);
                    newbuf = (char*)Memory::osRealloc(this->m_data, ncap * sizeof(char));
                    if(newbuf == NULL)
                    {
                        return false;
                    }
                    this->m_data = newbuf;
                    this->m_capacity = ncap;
                    if(this->m_length > newlen)
                    {
                        /* Buffer was shrunk - re-add null byte */
                        this->m_length = newlen;
                        this->m_data[this->m_length] = '\0';
                    }
                    return true;
                }

                /* Same as above, but update pointer if it pointed to resized array */
                void ensureCapacityUpdatePtr(size_t size, const char** ptr)
                {
                    size_t oldcap;
                    char* oldbuf;
                    if(this->m_capacity <= size + 1)
                    {
                        oldcap = this->m_capacity;
                        oldbuf = this->m_data;
                        if(!resize(size))
                        {
                            fprintf(stderr,
                                    "%s:%i:Error: _ensure_capacity_update_ptr couldn't resize "
                                    "buffer. [requested %zu bytes; capacity: %zu bytes]\n",
                                    __FILE__, __LINE__, size, this->m_capacity);
                            abort();
                        }
                        /* ptr may have pointed to sbuf, which has now moved */
                        if(*ptr >= oldbuf && *ptr < oldbuf + oldcap)
                        {
                            *ptr = this->m_data + (*ptr - oldbuf);
                        }
                    }
                }

                /*
                // Copy N characters from a character array to the end of this StrBuffer
                // strlen(str) must be >= len
                */
                bool append(const char* str, size_t len)
                {
                    ensureCapacityUpdatePtr(this->m_length + len, &str);
                    memcpy(this->m_data + this->m_length, str, len);
                    this->m_data[this->m_length = this->m_length + len] = '\0';
                    return true;
                }

                /* Copy a character array to the end of this StrBuffer */
                bool append(const char* str)
                {
                    return append(str, strlen(str));
                }

                bool append(const StrBuffer* sb2)
                {
                    return append(sb2->m_data, sb2->m_length);
                }

                /* Add a character to the end of this StrBuffer */
                bool append(int c)
                {
                    this->ensureCapacity(this->m_length + 1);
                    this->m_data[this->m_length] = c;
                    this->m_data[++this->m_length] = '\0';
                    return true;
                }

                /* Append char `c` `n` times */
                bool appendCharN(char c, size_t n)
                {
                    ensureCapacity(m_length + n);
                    memset(m_data + m_length, c, n);
                    m_length += n;
                    m_data[m_length] = '\0';
                    return true;
                }

                /*
                // sprintf
                */
                int appendFormatv(size_t pos, const char* fmt, va_list argptr)
                {
                    size_t buflen;
                    int numchars;
                    va_list vacpy;
                    NEON_UTIL_STRBUFBOUNDSCHECKINSERT(this, pos);
                    /* Length of remaining buffer */
                    buflen = this->m_capacity - pos;
                    if(buflen == 0 && !this->resize(this->m_capacity << 1))
                    {
                        fprintf(stderr, "%s:%i:Error: Out of memory\n", __FILE__, __LINE__);
                        abort();
                    }
                    /* Make a copy of the list of args incase we need to resize buff and try again */
                    va_copy(vacpy, argptr);
                    numchars = vsnprintf(this->m_data + pos, buflen, fmt, argptr);
                    va_end(argptr);
                    /*
                    // numchars is the number of chars that would be written (not including '\0')
                    // numchars < 0 => failure
                    */
                    if(numchars < 0)
                    {
                        fprintf(stderr, "Warning: appendFormatv something went wrong..\n");
                        abort();
                    }
                    /* numchars does not include the null terminating byte */
                    if((size_t)numchars + 1 > buflen)
                    {
                        this->ensureCapacity(pos + (size_t)numchars);
                        /*
                        // now use the argptr copy we made earlier
                        // Don't need to use vsnprintf now, vsprintf will do since we know it'll fit
                        */
                        numchars = vsprintf(this->m_data + pos, fmt, vacpy);
                        if(numchars < 0)
                        {
                            fprintf(stderr, "Warning: appendFormatv something went wrong..\n");
                            abort();
                        }
                    }
                    va_end(vacpy);
                    /*
                    // Don't need to NUL terminate, vsprintf/vnsprintf does that for us
                    // Update m_length
                    */
                    this->m_length = pos + (size_t)numchars;
                    return numchars;
                }

                /* sprintf to the end of a StrBuffer (adds string terminator after sprint) */
                int appendFormat(const char* fmt, ...)
                {
                    int numchars;
                    va_list argptr;
                    va_start(argptr, fmt);
                    numchars = this->appendFormatv(this->m_length, fmt, argptr);
                    va_end(argptr);
                    return numchars;
                }

                /* Print at a given position (overwrite chars at positions >= pos) */
                int appendFormatAt(size_t pos, const char* fmt, ...)
                {
                    int numchars;
                    va_list argptr;
                    NEON_UTIL_STRBUFBOUNDSCHECKINSERT(this, pos);
                    va_start(argptr, fmt);
                    numchars = this->appendFormatv(pos, fmt, argptr);
                    va_end(argptr);
                    return numchars;
                }

                /*
                // sprintf without terminating character
                // Does not prematurely end the string if you sprintf within the string
                // (terminates string if sprintf to the end)
                // Does not prematurely end the string if you sprintf within the string
                // (vs at the end)
                */
                int appendFormatNoTerm(size_t pos, const char* fmt, ...)
                {
                    size_t len;
                    int nchars;
                    char lastchar;
                    NEON_UTIL_STRBUFBOUNDSCHECKINSERT(this, pos);
                    len = this->m_length;
                    /* Call vsnprintf with NULL, 0 to get resulting string m_length without writing */
                    va_list argptr;
                    va_start(argptr, fmt);
                    nchars = vsnprintf(NULL, 0, fmt, argptr);
                    va_end(argptr);
                    if(nchars < 0)
                    {
                        fprintf(stderr, "Warning: appendFormatNoTerm something went wrong..\n");
                        abort();
                    }
                    /* Save overwritten char */
                    lastchar = (pos + (size_t)nchars < this->m_length) ? this->m_data[pos + (size_t)nchars] : 0;
                    va_start(argptr, fmt);
                    nchars = this->appendFormatv(pos, fmt, argptr);
                    va_end(argptr);
                    if(nchars < 0)
                    {
                        fprintf(stderr, "Warning: appendFormatNoTerm something went wrong..\n");
                        abort();
                    }
                    /* Restore m_length if shrunk, null terminate if extended */
                    if(this->m_length < len)
                    {
                        this->m_length = len;
                    }
                    else
                    {
                        this->m_data[this->m_length] = '\0';
                    }
                    /* Re-instate overwritten character */
                    this->m_data[pos + (size_t)nchars] = lastchar;
                    return nchars;
                }

                bool contains(char ch)
                {
                    size_t i;
                    for(i=0; i<this->m_length; i++)
                    {
                        if(this->m_data[i] == ch)
                        {
                            return true;
                        }
                    }
                    return false;
                }

                bool replace(int findme, const char* sstr, size_t slen)
                {
                    size_t i;
                    size_t nlen;
                    size_t needed;
                    needed = this->m_capacity;
                    for(i=0; i<this->m_length; i++)
                    {
                        if(this->m_data[i] == findme)
                        {
                            needed += slen;
                        }
                    }
                    if(!this->resize(needed+1))
                    {
                        return false;
                    }
                    nlen = replaceCharInPlace(this->m_data, this->m_length, findme, sstr, slen, this->m_capacity);
                    this->m_length = nlen;
                    return true;
                }

                bool fullReplace(const char* fstr, size_t flen, const char* sstr, size_t slen)
                {
                    size_t needed;
                    StrBuffer* nbuf;
                    needed = strReplaceCount(m_data, m_length, fstr, flen, slen);
                    if(needed == 0)
                    {
                        return false;
                    }
                    nbuf = Memory::create<StrBuffer>(needed);
                    nbuf->append(m_data, m_length);
                    StrBuffer::replaceFullInPlace(nbuf->m_data, nbuf->m_length, fstr, flen, sstr, slen);
                    nbuf->m_length = needed;
                    Memory::osFree(m_data);
                    m_data = nbuf->m_data;
                    m_length = nbuf->m_length;
                    m_capacity = nbuf->m_capacity;
                    return true;
                }

                /*
                // Copy a string to this StrBuffer, overwriting any existing characters
                // Note: dstpos + len can be longer the the current dst StrBuffer
                */
                void copyOver(size_t dstpos, const char* src, size_t len)
                {
                    size_t newlen;
                    if(src == NULL || len == 0)
                    {
                        return;
                    }
                    NEON_UTIL_STRBUFBOUNDSCHECKINSERT(this, dstpos);
                    /*
                    // Check if dst buffer can handle string
                    // src may have pointed to dst, which has now moved
                    */
                    newlen = NEON_UTIL_MAX(dstpos + len, m_length);
                    ensureCapacityUpdatePtr(newlen, &src);
                    /* memmove instead of strncpy, as it can handle overlapping regions */
                    memmove(m_data + dstpos, src, len * sizeof(char));
                    if(dstpos + len > m_length)
                    {
                        /* Extended string - add '\0' char */
                        m_length = dstpos + len;
                        m_data[m_length] = '\0';
                    }
                }

                /*
                // Overwrite dstpos..(dstpos+dstlen-1) with srclen chars from src
                // if dstlen != srclen, content to the right of dstlen is shifted
                // Example:
                //   sbuf = ... "aaabbccc";
                //   char *data = "xxx";
                //   sbuf->replaceAt(3,2,data,strlen(data));
                //   // sbuf is now "aaaxxxccc"
                //   sbuf->replaceAt(3,2,"_",1);
                //   // sbuf is now "aaa_ccc"
                */
                void replaceAt(size_t dstpos, size_t dstlen, const char* src, size_t srclen)
                {
                    size_t len;
                    size_t newlen;
                    char* tgt;
                    char* end;
                    NEON_UTIL_STRBUFBOUNDSCHECKREADRANGE(this, dstpos, dstlen);
                    if(src == NULL)
                    {
                        return;
                    }
                    if(dstlen == srclen)
                    {
                        this->copyOver(dstpos, src, srclen);
                    }
                    newlen = m_length + srclen - dstlen;
                    ensureCapacityUpdatePtr(newlen, &src);
                    if(src >= m_data && src < m_data + m_capacity)
                    {
                        if(srclen < dstlen)
                        {
                            /* copy */
                            memmove(m_data + dstpos, src, srclen * sizeof(char));
                            /* resize (shrink) */
                            memmove(m_data + dstpos + srclen, m_data + dstpos + dstlen, (m_length - dstpos - dstlen) * sizeof(char));
                        }
                        else
                        {
                            /*
                            // Buffer is going to grow and src points to this buffer
                            // resize (grow)
                            */
                            memmove(m_data + dstpos + srclen, m_data + dstpos + dstlen, (m_length - dstpos - dstlen) * sizeof(char));
                            tgt = m_data + dstpos;
                            end = m_data + dstpos + srclen;
                            if(src < tgt + dstlen)
                            {
                                len = NEON_UTIL_MIN((size_t)(end - src), srclen);
                                memmove(tgt, src, len);
                                tgt += len;
                                src += len;
                                srclen -= len;
                            }
                            if(src >= tgt + dstlen)
                            {
                                /* shift to account for resizing */
                                src += srclen - dstlen;
                                memmove(tgt, src, srclen);
                            }
                        }
                    }
                    else
                    {
                        /* resize */
                        memmove(m_data + dstpos + srclen, m_data + dstpos + dstlen, (m_length - dstpos - dstlen) * sizeof(char));
                        /* copy */
                        memcpy(m_data + dstpos, src, srclen * sizeof(char));
                    }
                    m_length = newlen;
                    m_data[m_length] = '\0';
                }

                void shrinkk(size_t len)
                {
                    m_data[m_length = (len)] = 0;
                }

                /*
                // Remove \r and \n characters from the end of this StringBuffesr
                // Returns the number of characters removed
                */
                size_t chomp()
                {
                    size_t oldlen;
                    oldlen = m_length;
                    m_length = strChomp(m_data, m_length);
                    return oldlen - m_length;
                }


                /* Reverse a string */
                void reverse()
                {
                    strReverseRegion(m_data, m_length);
                }

                /*
                // Get a substring as a new null terminated char array
                // (remember to free the returned char* after you're done with it!)
                */
                char* substr(size_t start, size_t len)
                {
                    char* newstr;
                    NEON_UTIL_STRBUFBOUNDSCHECKREADRANGE(this, start, len);
                    newstr = (char*)Memory::osMalloc((len + 1) * sizeof(char));
                    strncpy(newstr, m_data + start, len);
                    newstr[len] = '\0';
                    return newstr;
                }

                void toUpperCase()
                {
                    char* pos;
                    char* end;
                    end = m_data + m_length;
                    for(pos = m_data; pos < end; pos++)
                    {
                        *pos = (char)toupper(*pos);
                    }
                }

                void toLowerCase()
                {
                    char* pos;
                    char* end;
                    end = m_data + m_length;
                    for(pos = m_data; pos < end; pos++)
                    {
                        *pos = (char)tolower(*pos);
                    }
                }

                /* Insert: copy to a StrBuffer, shifting any existing characters along */
                void insertAt(StrBuffer* dst, size_t dstpos, const char* src, size_t len)
                {
                    char* insert;
                    if(src == NULL || len == 0)
                    {
                        return;
                    }
                    NEON_UTIL_STRBUFBOUNDSCHECKINSERT(dst, dstpos);
                    /*
                    // Check if dst buffer has capacity for inserted string plus \0
                    // src may have pointed to dst, which will be moved in realloc when
                    // calling ensure capacity
                    */
                    dst->ensureCapacityUpdatePtr(dst->m_length + len, &src);
                    insert = dst->m_data + dstpos;
                    /* dstpos could be at the end (== dst->m_length) */
                    if(dstpos < dst->m_length)
                    {
                        /* Shift some characters up */
                        memmove(insert + len, insert, (dst->m_length - dstpos) * sizeof(char));
                        if(src >= dst->m_data && src < dst->m_data + dst->m_capacity)
                        {
                            /* src/dst strings point to the same string in memory */
                            if(src < insert)
                            {
                                memmove(insert, src, len * sizeof(char));
                            }
                            else if(src > insert)
                            {
                                memmove(insert, src + len, len * sizeof(char));
                            }
                        }
                        else
                        {
                            memmove(insert, src, len * sizeof(char));
                        }
                    }
                    else
                    {
                        memmove(insert, src, len * sizeof(char));
                    }
                    /* Update size */
                    dst->m_length += len;
                    dst->m_data[dst->m_length] = '\0';
                }

                /*
                // Remove characters from the buffer
                //   sb = ... "aaaBBccc";
                //   sb->eraseAt(3, 2);
                //   // sb is now "aaaccc"
                */
                void eraseAt(size_t pos, size_t len)
                {
                    NEON_UTIL_STRBUFBOUNDSCHECKREADRANGE(this, pos, len);
                    memmove(m_data + pos, m_data + pos + len, m_length - pos - len);
                    m_length -= len;
                    m_data[m_length] = '\0';
                }

                /* Trim whitespace characters from the start and end of a string */
                void trimInplace()
                {
                    size_t start;
                    if(m_length == 0)
                    {
                        return;
                    }
                    /* Trim end first */
                    while(m_length > 0 && isspace((int)m_data[m_length - 1]))
                    {
                        m_length--;
                    }
                    m_data[m_length] = '\0';
                    if(m_length == 0)
                    {
                        return;
                    }
                    start = 0;
                    while(start < m_length && isspace((int)m_data[start]))
                    {
                        start++;
                    }
                    if(start != 0)
                    {
                        m_length -= start;
                        memmove(m_data, m_data + start, m_length * sizeof(char));
                        m_data[m_length] = '\0';
                    }
                }

                /*
                // Trim the characters listed in `list` from the left of `sbuf`
                // `list` is a null-terminated string of characters
                */
                void trimInplaceLeft(const char* list)
                {
                    size_t start;
                    start = 0;

                    while(start < m_length && (strchr(list, m_data[start]) != NULL))
                    {
                        start++;
                    }
                    if(start != 0)
                    {
                        m_length -= start;
                        memmove(m_data, m_data + start, m_length * sizeof(char));
                        m_data[m_length] = '\0';
                    }
                }

                /*
                // Trim the characters listed in `list` from the right of `sbuf`
                // `list` is a null-terminated string of characters
                */
                void trimInplaceRight(const char* list)
                {
                    if(m_length == 0)
                    {
                        return;
                    }
                    while(m_length > 0 && strchr(list, m_data[m_length - 1]) != NULL)
                    {
                        m_length--;
                    }
                    m_data[m_length] = '\0';
                }
        };

        struct OptionParser
        {
            public:
                enum ArgType
                {
                    A_NONE,
                    A_REQUIRED,
                    A_OPTIONAL
                };

                struct LongFlags
                {
                    const char* longname;
                    int shortname;
                    ArgType argtype;
                    const char* helptext;
                };


            public:
                bool isDashDash(const char* arg)
                {
                    if(arg != NULL)
                    {
                        if((arg[0] == '-') && (arg[1] == '-') && (arg[2] == '\0'))
                        {
                            return true;
                        }
                    }
                    return false;
                }

                bool isShortOpt(const char* arg)
                {
                    if(arg != NULL)
                    {
                        if((arg[0] == '-') && (arg[1] != '-') && (arg[1] != '\0'))
                        {
                            return true;
                        }
                    }
                    return false;
                }

                static bool isLongOpt(const char* arg)
                {
                    if(arg != NULL)
                    {
                        if((arg[0] == '-') && (arg[1] == '-') && (arg[2] != '\0'))
                        {
                            return true;
                        }
                    }
                    return false;
                }

                static int getArgType(const char* optstring, char c)
                {
                    int count;
                    count = A_NONE;
                    if(c == ':')
                    {
                        return -1;
                    }
                    for(; *optstring && c != *optstring; optstring++)
                    {
                    }
                    if(!*optstring)
                    {
                        return -1;
                    }
                    if(optstring[1] == ':')
                    {
                        count += optstring[2] == ':' ? 2 : 1;
                    }
                    return count;
                }

                static bool isLongOptsEnd(const LongFlags* longopts, int i)
                {
                    if(!longopts[i].longname && !longopts[i].shortname)
                    {
                        return true;
                    }
                    return false;
                }

                void fromLong(const LongFlags* longopts, char* optstring)
                {
                    int i;
                    int a;
                    char* p;
                    p = optstring;
                    for(i = 0; !isLongOptsEnd(longopts, i); i++)
                    {
                        if(longopts[i].shortname && longopts[i].shortname < 127)
                        {
                            *p++ = longopts[i].shortname;
                            for(a = 0; a < (int)longopts[i].argtype; a++)
                            {
                                *p++ = ':';
                            }
                        }
                    }
                    *p = '\0';
                }

                /* Unlike strcmp(), handles options containing "=". */
                static bool matchLongOpts(const char* longname, const char* option)
                {
                    const char *a;
                    const char* n;
                    a = option;
                    n = longname;
                    if(longname == 0)
                    {
                        return 0;
                    }
                    for(; *a && *n && *a != '='; a++, n++)
                    {
                        if(*a != *n)
                        {
                            return 0;
                        }
                    }
                    return *n == '\0' && (*a == '\0' || *a == '=');
                }

                /* Return the part after "=", or NULL. */
                static char* getLongOptsArg(char* option)
                {
                    for(; *option && *option != '='; option++)
                    {
                    }
                    if(*option == '=')
                    {
                        return option + 1;
                    }
                    return NULL;
                }

            public:
                char** argv;
                int argc;
                int dopermute;
                int optind;
                int optopt;
                char* optarg;
                char errmsg[64];
                int subopt;

            public:
                /**
                 * Initializes the parser state.
                 */
                OptionParser(int ac, char** av)
                {
                    this->argv = av;
                    this->argc = ac;
                    this->dopermute = 1;
                    this->optind = argv[0] != 0;
                    this->subopt = 0;
                    this->optarg = 0;
                    this->errmsg[0] = '\0';
                }

                int makeError(const char* msg, const char* data)
                {
                    unsigned p;
                    const char* sep;
                    p = 0;
                    sep = " -- '";
                    while(*msg)
                    {
                        this->errmsg[p++] = *msg++;
                    }
                    while(*sep)
                    {
                        this->errmsg[p++] = *sep++;
                    }
                    while(p < sizeof(this->errmsg) - 2 && *data)
                    {
                        this->errmsg[p++] = *data++;
                    }
                    this->errmsg[p++] = '\'';
                    this->errmsg[p++] = '\0';
                    return '?';
                }

                void permute(int index)
                {
                    int i;
                    char* nonoption;
                    nonoption = this->argv[index];
                    for(i = index; i < this->optind - 1; i++)
                    {
                        this->argv[i] = this->argv[i + 1];
                    }
                    this->argv[this->optind - 1] = nonoption;
                }

                /**
                 * Handles GNU-style long options in addition to getopt() options.
                 * This works a lot like GNU's getopt_long(). The last option in
                 * longopts must be all zeros, marking the end of the array. The
                 * longindex argument may be NULL.
                 */
                int nextLong(const LongFlags* longopts, int* longindex)
                {
                    int i;
                    int r;
                    int index;
                    char* arg;
                    char* option;
                    const char* name;
                    option = this->argv[this->optind];
                    if(option == 0)
                    {
                        return -1;
                    }
                    else if(isDashDash(option))
                    {
                        this->optind++; /* consume "--" */
                        return -1;
                    }
                    else if(isShortOpt(option))
                    {
                        return this->longFallback(longopts, longindex);
                    }
                    else if(!isLongOpt(option))
                    {
                        if(this->dopermute)
                        {
                            index = this->optind++;
                            r = this->nextLong(longopts, longindex);
                            this->permute(index);
                            this->optind--;
                            return r;
                        }
                        else
                        {
                            return -1;
                        }
                    }
                    /* Parse as long option. */
                    this->errmsg[0] = '\0';
                    this->optopt = 0;
                    this->optarg = 0;
                    option += 2; /* skip "--" */
                    this->optind++;
                    for(i = 0; !isLongOptsEnd(longopts, i); i++)
                    {
                        name = longopts[i].longname;
                        if(matchLongOpts(name, option))
                        {
                            if(longindex)
                            {
                                *longindex = i;
                            }
                            this->optopt = longopts[i].shortname;
                            arg = getLongOptsArg(option);
                            if(longopts[i].argtype == A_NONE && arg != 0)
                            {
                                return this->makeError("option takes no arguments", name);
                            }
                            if(arg != 0)
                            {
                                this->optarg = arg;
                            }
                            else if(longopts[i].argtype == A_REQUIRED)
                            {
                                this->optarg = this->argv[this->optind];
                                if(this->optarg == 0)
                                {
                                    return this->makeError("option requires an argument", name);
                                }
                                else
                                {
                                    this->optind++;
                                }
                            }
                            return this->optopt;
                        }
                    }
                    return this->makeError("invalid option", option);
                }

                /**
                 * Read the next option in the argv array.
                 * @param optstring a getopt()-formatted option string.
                 * @return the next option character, -1 for done, or '?' for error
                 *
                 * Just like getopt(), a character followed by no colons means no
                 * argument. One colon means the option has a required argument. Two
                 * colons means the option takes an optional argument.
                 */
                int nextShort(const char* optstring)
                {
                    int r;
                    int type;
                    int index;
                    char* next;
                    char* option;
                    char str[2] = { 0, 0 };
                    option = this->argv[this->optind];
                    this->errmsg[0] = '\0';
                    this->optopt = 0;
                    this->optarg = 0;
                    if(option == 0)
                    {
                        return -1;
                    }
                    else if(isDashDash(option))
                    {
                        /* consume "--" */
                        this->optind++;
                        return -1;
                    }
                    else if(!isShortOpt(option))
                    {
                        if(this->dopermute)
                        {
                            index = this->optind++;
                            r = this->nextShort(optstring);
                            this->permute(index);
                            this->optind--;
                            return r;
                        }
                        else
                        {
                            return -1;
                        }
                    }
                    option += this->subopt + 1;
                    this->optopt = option[0];
                    type = getArgType(optstring, option[0]);
                    next = this->argv[this->optind + 1];
                    switch(type)
                    {
                        case -1:
                            {
                                str[1] = 0;
                                str[0] = option[0];
                                this->optind++;
                                return this->makeError("invalid option", str);
                            }
                            break;
                        case A_NONE:
                            {
                                if(option[1])
                                {
                                    this->subopt++;
                                }
                                else
                                {
                                    this->subopt = 0;
                                    this->optind++;
                                }
                                return option[0];
                            }
                            break;
                        case A_REQUIRED:
                            {
                                this->subopt = 0;
                                this->optind++;
                                if(option[1])
                                {
                                    this->optarg = option + 1;
                                }
                                else if(next != 0)
                                {
                                    this->optarg = next;
                                    this->optind++;
                                }
                                else
                                {
                                    str[1] = 0;
                                    str[0] = option[0];
                                    this->optarg = 0;
                                    return this->makeError("option requires an argument", str);
                                }
                                return option[0];
                            }
                            break;
                        case A_OPTIONAL:
                            {
                                this->subopt = 0;
                                this->optind++;
                                if(option[1])
                                {
                                    this->optarg = option + 1;
                                }
                                else
                                {
                                    this->optarg = 0;
                                }
                                return option[0];
                            }
                            break;
                    }
                    return 0;
                }

                int longFallback(const LongFlags* longopts, int* longindex)
                {
                    int i;
                    int result;
                    /* 96 ASCII printable characters */
                    char optstring[96 * 3 + 1];
                    fromLong(longopts, optstring);
                    result = this->nextShort(optstring);
                    if(longindex != 0)
                    {
                        *longindex = -1;
                        if(result != -1)
                        {
                            for(i = 0; !isLongOptsEnd(longopts, i); i++)
                            {
                                if(longopts[i].shortname == this->optopt)
                                {
                                    *longindex = i;
                                }
                            }
                        }
                    }
                    return result;
                }

                /**
                 * Used for stepping over non-option arguments.
                 * @return the next non-option argument, or NULL for no more arguments
                 *
                 * Argument parsing can continue with optparse() after using this
                 * function. That would be used to parse the options for the
                 * subcommand returned by nextPositional(). This function allows you to
                 * ignore the value of optind.
                 */
                char* nextPositional()
                {
                    char* option;
                    option = this->argv[this->optind];
                    this->subopt = 0;
                    if(option != 0)
                    {
                        this->optind++;
                    }
                    return option;
                }
        };

    }

    struct Color
    {
        public:
            enum Code
            {
                COLOR_RESET,
                COLOR_RED,
                COLOR_GREEN,    
                COLOR_YELLOW,
                COLOR_BLUE,
                COLOR_MAGENTA,
                COLOR_CYAN
            };

        private:
            bool m_istty = false;
            int m_fdstdout = -1;
            int m_fdstderr = -1;

        public:
            Color()
            {
                #if !defined(NEON_CFG_FORCEDISABLECOLOR)
                    m_fdstdout = fileno(stdout);
                    m_fdstderr = fileno(stderr);
                    m_istty = (osfn_isatty(m_fdstderr) && osfn_isatty(m_fdstdout));
                #endif
            }

            NEON_FORCEINLINE Code codeFromChar(char c)
            {
                switch(c)
                {
                    case '0':
                        return COLOR_RESET;
                    case 'r':
                        return COLOR_RED;
                    case 'g':
                        return COLOR_GREEN;
                    case 'b':
                        return COLOR_BLUE;
                    case 'y':
                        return COLOR_YELLOW;
                    case 'm':
                        return COLOR_MAGENTA;
                    case 'c':
                        return COLOR_CYAN;
                }
                return COLOR_RESET;
            }

            NEON_FORCEINLINE const char* color(Code tc)
            {
                #if !defined(NEON_CFG_FORCEDISABLECOLOR)

                    if(m_istty)
                    {
                        switch(tc)
                        {
                            case COLOR_RESET:
                                return "\x1B[0m";
                            case COLOR_RED:
                                return "\x1B[31m";
                            case COLOR_GREEN:
                                return "\x1B[32m";
                            case COLOR_YELLOW:
                                return "\x1B[33m";
                            case COLOR_BLUE:
                                return "\x1B[34m";
                            case COLOR_MAGENTA:
                                return "\x1B[35m";
                            case COLOR_CYAN:
                                return "\x1B[36m";
                        }
                    }
                #else
                    (void)tc;
                #endif
                return "";
            }

            NEON_FORCEINLINE const char* color(char tc)
            {
                return color(codeFromChar(tc));
            }
    };
}


// preproto

bool nn_util_fsfileexists(neon::State *state, const char *filepath);
bool nn_util_fsfileisfile(neon::State *state, const char *filepath);
bool nn_util_fsfileisdirectory(neon::State *state, const char *filepath);
char *nn_util_fsgetbasename(neon::State *state, const char *path);

void nn_import_closemodule(void *dlw);
int nn_fileobject_close(neon::File *file);
bool nn_fileobject_open(neon::File *file);
bool nn_vm_callvaluewithobject(neon::State *state, neon::Value callable, neon::Value thisval, int argcount);

static NEON_FORCEINLINE void nn_state_astdebug(neon::State* state, const char* funcname, const char* format, ...);
static NEON_FORCEINLINE void nn_state_apidebug(neon::State* state, const char* funcname, const char* format, ...);

void nn_astparser_consumestmtend(neon::Parser *prs);
int nn_astparser_getcodeargscount(const neon::Instruction *bytecode, const neon::Value *constants, int ip);
int nn_astparser_makeidentconst(neon::Parser *prs, neon::Token *name);
int nn_astparser_addlocal(neon::Parser *prs, neon::Token name);
void nn_astparser_declarevariable(neon::Parser *prs);
int nn_astparser_parsevariable(neon::Parser *prs, const char *message);
void nn_astparser_markinitialized(neon::Parser *prs);
void nn_astparser_definevariable(neon::Parser *prs, int global);
neon::Token nn_astparser_synthtoken(const char *name);
void nn_astparser_scopebegin(neon::Parser *prs);
void nn_astparser_scopeend(neon::Parser *prs);
int nn_astparser_discardlocals(neon::Parser *prs, int depth);
void nn_astparser_endloop(neon::Parser *prs);
bool nn_astparser_rulebinary(neon::Parser *prs, neon::Token previous, bool canassign);
bool nn_astparser_rulecall(neon::Parser *prs, neon::Token previous, bool canassign);
bool nn_astparser_ruleliteral(neon::Parser *prs, bool canassign);
void nn_astparser_parseassign(neon::Parser *prs, uint8_t realop, uint8_t getop, uint8_t setop, int arg);
void nn_astparser_assignment(neon::Parser *prs, uint8_t getop, uint8_t setop, int arg, bool canassign);
bool nn_astparser_ruledot(neon::Parser *prs, neon::Token previous, bool canassign);
void nn_astparser_namedvar(neon::Parser *prs, neon::Token name, bool canassign);
void nn_astparser_createdvar(neon::Parser *prs, neon::Token name);
bool nn_astparser_rulearray(neon::Parser *prs, bool canassign);
bool nn_astparser_ruledictionary(neon::Parser *prs, bool canassign);
bool nn_astparser_ruleindexing(neon::Parser *prs, neon::Token previous, bool canassign);
bool nn_astparser_rulevarnormal(neon::Parser *prs, bool canassign);
bool nn_astparser_rulevarglobal(neon::Parser *prs, bool canassign);
bool nn_astparser_rulethis(neon::Parser *prs, bool canassign);
bool nn_astparser_rulesuper(neon::Parser *prs, bool canassign);
bool nn_astparser_rulegrouping(neon::Parser *prs, bool canassign);
bool nn_astparser_rulenumber(neon::Parser *prs, bool canassign);
bool nn_astparser_rulestring(neon::Parser *prs, bool canassign);
bool nn_astparser_ruleinterpolstring(neon::Parser *prs, bool canassign);
bool nn_astparser_ruleunary(neon::Parser *prs, bool canassign);
bool nn_astparser_ruleand(neon::Parser *prs, neon::Token previous, bool canassign);
bool nn_astparser_ruleor(neon::Parser *prs, neon::Token previous, bool canassign);
bool nn_astparser_ruleconditional(neon::Parser *prs, neon::Token previous, bool canassign);
bool nn_astparser_ruleimport(neon::Parser *prs, bool canassign);
bool nn_astparser_rulenew(neon::Parser *prs, bool canassign);
bool nn_astparser_ruletypeof(neon::Parser *prs, bool canassign);
bool nn_astparser_rulenothingprefix(neon::Parser *prs, bool canassign);
bool nn_astparser_rulenothinginfix(neon::Parser *prs, neon::Token previous, bool canassign);
bool nn_astparser_parseblock(neon::Parser *prs);
void nn_astparser_declarefuncargvar(neon::Parser *prs);
int nn_astparser_parsefuncparamvar(neon::Parser *prs, const char *message);
uint8_t nn_astparser_parsefunccallargs(neon::Parser *prs);
void nn_astparser_parsefuncparamlist(neon::Parser *prs);
void nn_astparser_parsemethod(neon::Parser *prs, neon::Token classname, bool isstatic);
bool nn_astparser_ruleanonfunc(neon::Parser *prs, bool canassign);
void nn_astparser_parsefield(neon::Parser *prs, bool isstatic);
void nn_astparser_parsefuncdecl(neon::Parser *prs);
void nn_astparser_parseclassdeclaration(neon::Parser *prs);
void nn_astparser_parsevardecl(neon::Parser *prs, bool isinitializer);
void nn_astparser_parseexprstmt(neon::Parser *prs, bool isinitializer, bool semi);
void nn_astparser_parseforstmt(neon::Parser *prs);
void nn_astparser_parseforeachstmt(neon::Parser *prs);
void nn_astparser_parseswitchstmt(neon::Parser *prs);
void nn_astparser_parseifstmt(neon::Parser *prs);
void nn_astparser_parseechostmt(neon::Parser *prs);
void nn_astparser_parsethrowstmt(neon::Parser *prs);
void nn_astparser_parseassertstmt(neon::Parser *prs);
void nn_astparser_parsetrystmt(neon::Parser *prs);
void nn_astparser_parsereturnstmt(neon::Parser *prs);
void nn_astparser_parsewhilestmt(neon::Parser *prs);
void nn_astparser_parsedo_whilestmt(neon::Parser *prs);
void nn_astparser_parsecontinuestmt(neon::Parser *prs);
void nn_astparser_parsebreakstmt(neon::Parser *prs);

/*
* quite a number of types ***MUST NOT EVER BE DERIVED FROM***, to ensure
* that they are correctly initialized.
* Just declare them as an instance variable instead.
*/

namespace neon
{
    struct Instruction
    {
        public:
            enum OpCode
            {
                OP_GLOBALDEFINE,
                OP_GLOBALGET,
                OP_GLOBALSET,
                OP_LOCALGET,
                OP_LOCALSET,
                OP_FUNCARGSET,
                OP_FUNCARGGET,
                OP_UPVALUEGET,
                OP_UPVALUESET,
                OP_UPVALUECLOSE,
                OP_PROPERTYGET,
                OP_PROPERTYGETSELF,
                OP_PROPERTYSET,
                OP_JUMPIFFALSE,
                OP_JUMPNOW,
                OP_LOOP,
                OP_EQUAL,
                OP_PRIMGREATER,
                OP_PRIMLESSTHAN,
                OP_PUSHEMPTY,
                OP_PUSHNULL,
                OP_PUSHTRUE,
                OP_PUSHFALSE,
                OP_PRIMADD,
                OP_PRIMSUBTRACT,
                OP_PRIMMULTIPLY,
                OP_PRIMDIVIDE,
                OP_PRIMFLOORDIVIDE,
                OP_PRIMMODULO,
                OP_PRIMPOW,
                OP_PRIMNEGATE,
                OP_PRIMNOT,
                OP_PRIMBITNOT,
                OP_PRIMAND,
                OP_PRIMOR,
                OP_PRIMBITXOR,
                OP_PRIMSHIFTLEFT,
                OP_PRIMSHIFTRIGHT,
                OP_PUSHONE,
                /* 8-bit constant address (0 - 255) */
                OP_PUSHCONSTANT,
                OP_ECHO,
                OP_POPONE,
                OP_DUPONE,
                OP_POPN,
                OP_ASSERT,
                OP_EXTHROW,
                OP_MAKECLOSURE,
                OP_CALLFUNCTION,
                OP_CALLMETHOD,
                OP_CLASSINVOKETHIS,
                OP_RETURN,
                OP_MAKECLASS,
                OP_MAKEMETHOD,
                OP_CLASSPROPERTYDEFINE,
                OP_CLASSINHERIT,
                OP_CLASSGETSUPER,
                OP_CLASSINVOKESUPER,
                OP_CLASSINVOKESUPERSELF,
                OP_MAKERANGE,
                OP_MAKEARRAY,
                OP_MAKEDICT,
                OP_INDEXGET,
                OP_INDEXGETRANGED,
                OP_INDEXSET,
                OP_IMPORTIMPORT,
                OP_EXTRY,
                OP_EXPOPTRY,
                OP_EXPUBLISHTRY,
                OP_STRINGIFY,
                OP_SWITCH,
                OP_TYPEOF,
                OP_BREAK_PL,
            };

        public:
            static const char* opName(uint8_t instruc)
            {
                switch(instruc)
                {
                    case OP_GLOBALDEFINE: return "OP_GLOBALDEFINE";
                    case OP_GLOBALGET: return "OP_GLOBALGET";
                    case OP_GLOBALSET: return "OP_GLOBALSET";
                    case OP_LOCALGET: return "OP_LOCALGET";
                    case OP_LOCALSET: return "OP_LOCALSET";
                    case OP_FUNCARGSET: return "OP_FUNCARGSET";
                    case OP_FUNCARGGET: return "OP_FUNCARGGET";
                    case OP_UPVALUEGET: return "OP_UPVALUEGET";
                    case OP_UPVALUESET: return "OP_UPVALUESET";
                    case OP_UPVALUECLOSE: return "OP_UPVALUECLOSE";
                    case OP_PROPERTYGET: return "OP_PROPERTYGET";
                    case OP_PROPERTYGETSELF: return "OP_PROPERTYGETSELF";
                    case OP_PROPERTYSET: return "OP_PROPERTYSET";
                    case OP_JUMPIFFALSE: return "OP_JUMPIFFALSE";
                    case OP_JUMPNOW: return "OP_JUMPNOW";
                    case OP_LOOP: return "OP_LOOP";
                    case OP_EQUAL: return "OP_EQUAL";
                    case OP_PRIMGREATER: return "OP_PRIMGREATER";
                    case OP_PRIMLESSTHAN: return "OP_PRIMLESSTHAN";
                    case OP_PUSHEMPTY: return "OP_PUSHEMPTY";
                    case OP_PUSHNULL: return "OP_PUSHNULL";
                    case OP_PUSHTRUE: return "OP_PUSHTRUE";
                    case OP_PUSHFALSE: return "OP_PUSHFALSE";
                    case OP_PRIMADD: return "OP_PRIMADD";
                    case OP_PRIMSUBTRACT: return "OP_PRIMSUBTRACT";
                    case OP_PRIMMULTIPLY: return "OP_PRIMMULTIPLY";
                    case OP_PRIMDIVIDE: return "OP_PRIMDIVIDE";
                    case OP_PRIMFLOORDIVIDE: return "OP_PRIMFLOORDIVIDE";
                    case OP_PRIMMODULO: return "OP_PRIMMODULO";
                    case OP_PRIMPOW: return "OP_PRIMPOW";
                    case OP_PRIMNEGATE: return "OP_PRIMNEGATE";
                    case OP_PRIMNOT: return "OP_PRIMNOT";
                    case OP_PRIMBITNOT: return "OP_PRIMBITNOT";
                    case OP_PRIMAND: return "OP_PRIMAND";
                    case OP_PRIMOR: return "OP_PRIMOR";
                    case OP_PRIMBITXOR: return "OP_PRIMBITXOR";
                    case OP_PRIMSHIFTLEFT: return "OP_PRIMSHIFTLEFT";
                    case OP_PRIMSHIFTRIGHT: return "OP_PRIMSHIFTRIGHT";
                    case OP_PUSHONE: return "OP_PUSHONE";
                    case OP_PUSHCONSTANT: return "OP_PUSHCONSTANT";
                    case OP_ECHO: return "OP_ECHO";
                    case OP_POPONE: return "OP_POPONE";
                    case OP_DUPONE: return "OP_DUPONE";
                    case OP_POPN: return "OP_POPN";
                    case OP_ASSERT: return "OP_ASSERT";
                    case OP_EXTHROW: return "OP_EXTHROW";
                    case OP_MAKECLOSURE: return "OP_MAKECLOSURE";
                    case OP_CALLFUNCTION: return "OP_CALLFUNCTION";
                    case OP_CALLMETHOD: return "OP_CALLMETHOD";
                    case OP_CLASSINVOKETHIS: return "OP_CLASSINVOKETHIS";
                    case OP_RETURN: return "OP_RETURN";
                    case OP_MAKECLASS: return "OP_MAKECLASS";
                    case OP_MAKEMETHOD: return "OP_MAKEMETHOD";
                    case OP_CLASSPROPERTYDEFINE: return "OP_CLASSPROPERTYDEFINE";
                    case OP_CLASSINHERIT: return "OP_CLASSINHERIT";
                    case OP_CLASSGETSUPER: return "OP_CLASSGETSUPER";
                    case OP_CLASSINVOKESUPER: return "OP_CLASSINVOKESUPER";
                    case OP_CLASSINVOKESUPERSELF: return "OP_CLASSINVOKESUPERSELF";
                    case OP_MAKERANGE: return "OP_MAKERANGE";
                    case OP_MAKEARRAY: return "OP_MAKEARRAY";
                    case OP_MAKEDICT: return "OP_MAKEDICT";
                    case OP_INDEXGET: return "OP_INDEXGET";
                    case OP_INDEXGETRANGED: return "OP_INDEXGETRANGED";
                    case OP_INDEXSET: return "OP_INDEXSET";
                    case OP_IMPORTIMPORT: return "OP_IMPORTIMPORT";
                    case OP_EXTRY: return "OP_EXTRY";
                    case OP_EXPOPTRY: return "OP_EXPOPTRY";
                    case OP_EXPUBLISHTRY: return "OP_EXPUBLISHTRY";
                    case OP_STRINGIFY: return "OP_STRINGIFY";
                    case OP_SWITCH: return "OP_SWITCH";
                    case OP_TYPEOF: return "OP_TYPEOF";
                    case OP_BREAK_PL: return "OP_BREAK_PL";
                    default:
                        break;
                }
                return "<?unknown?>";
            }

        public:
            /* is this instruction an opcode? */
            bool isop;
            /* opcode or value */
            uint8_t code;
            /* line corresponding to where this instruction was emitted */
            int srcline;
    };

    struct Value final
    {
        public:
            enum ValType
            {
                VALTYPE_EMPTY,
                VALTYPE_NULL,
                VALTYPE_BOOL,
                VALTYPE_NUMBER,
                VALTYPE_OBJECT,
            };

            enum ObjType
            {
                /* containers */
                OBJTYPE_STRING,
                OBJTYPE_RANGE,
                OBJTYPE_ARRAY,
                OBJTYPE_DICT,
                OBJTYPE_FILE,

                /* base object types */
                OBJTYPE_UPVALUE,
                OBJTYPE_FUNCBOUND,
                OBJTYPE_FUNCCLOSURE,
                OBJTYPE_FUNCSCRIPT,
                OBJTYPE_INSTANCE,
                OBJTYPE_FUNCNATIVE,
                OBJTYPE_CLASS,

                /* non-user objects */
                OBJTYPE_MODULE,
                OBJTYPE_SWITCH,
                /* object type that can hold any C pointer */
                OBJTYPE_USERDATA,
            };

        public:
            static NEON_FORCEINLINE Value makeValue(ValType type)
            {
                Value v;
                v.m_valtype = type;
                return v;
            }

            static NEON_FORCEINLINE Value makeEmpty()
            {
                return makeValue(VALTYPE_EMPTY);
            }

            static NEON_FORCEINLINE Value makeNull()
            {
                Value v;
                v = makeValue(VALTYPE_NULL);
                return v;
            }

            static NEON_FORCEINLINE Value makeBool(bool b)
            {
                Value v;
                v = makeValue(VALTYPE_BOOL);
                v.m_valunion.boolean = b;
                return v;
            }

            static NEON_FORCEINLINE Value makeNumber(double d)
            {
                Value v;
                v = makeValue(VALTYPE_NUMBER);
                v.m_valunion.number = d;
                return v;
            }

            static NEON_FORCEINLINE Value makeInt(int i)
            {
                Value v;
                v = makeValue(VALTYPE_NUMBER);
                v.m_valunion.number = i;
                return v;
            }

            template<typename SubObjT>
            static NEON_FORCEINLINE Value fromObject(SubObjT* obj)
            {
                Value v;
                v = makeValue(VALTYPE_OBJECT);
                v.m_valunion.obj = (Object*)obj;
                return v;
            }

            static bool getObjNumberVal(Value val, size_t* dest);

            static NEON_FORCEINLINE bool getNumberVal(Value val, size_t* dest)
            {
                if(val.isNumber())
                {
                    *dest = val.asNumber();
                    return true;
                }
                else if(val.isBool())
                {
                    *dest = val.asBool();
                    return true;
                }
                return getObjNumberVal(val, dest);
            }

            static NEON_FORCEINLINE bool isLessOrEqual(State* state, Value a, Value b)
            {
                double ia;
                double ib;
                (void)state;
                if(!Value::getNumberVal(a, (size_t*)&ia))
                {
                    return false;
                }
                if(!Value::getNumberVal(b, (size_t*)&ib))
                {
                    return false;
                }
                return (ia <= ib);
            }

            static bool isObjFalse(Value val);

            static bool isFalse(Value val)
            {
                switch(val.type())
                {
                    case VALTYPE_BOOL:
                        {
                            return !val.asBool();
                        }
                        break;
                    case VALTYPE_NULL:
                    case VALTYPE_EMPTY:
                        {
                            return true;
                        }
                        break;
                    case VALTYPE_NUMBER:
                        {
                            /* -1 is the number equivalent of false */
                            return val.asNumber() < 0;
                        }
                        break;
                    default:
                        {
                        }
                        break;
                }
                return isObjFalse(val);
            }

            static Value findGreaterObject(Value a, Value b);

            /**
             * returns the greater of the two values.
             * this function encapsulates the object hierarchy
             */
            NEON_FORCEINLINE static Value findGreater(Value a, Value b)
            {
                if(a.isNull())
                {
                    return b;
                }
                else if(a.isBool())
                {
                    if(b.isNull() || (b.isBool() && b.asBool() == false))
                    {
                        /* only null, false and false are lower than numbers */
                        return a;
                    }
                    else
                    {
                        return b;
                    }
                }
                else if(a.isNumber())
                {
                    if(b.isNull() || b.isBool())
                    {
                        return a;
                    }
                    else if(b.isNumber())
                    {
                        if(a.asNumber() >= b.asNumber())
                        {
                            return a;
                        }
                        return b;
                    }
                    else
                    {
                        /* every other thing is greater than a number */
                        return b;
                    }
                }
                else if(a.isObject())
                {
                    return findGreaterObject(a, b);
                }
                return a;
            }

            static Value copyValue(State* state, Value value);

            static String* toString(State* state, Value value);

            static uint32_t getObjHash(Object* object);

            NEON_FORCEINLINE static uint32_t getHash(Value value)
            {
                switch(value.type())
                {
                    case Value::VALTYPE_BOOL:
                        return value.asBool() ? 3 : 5;
                    case Value::VALTYPE_NULL:
                        return 7;
                    case Value::VALTYPE_NUMBER:
                        return Util::hashDouble(value.asNumber());
                    case Value::VALTYPE_OBJECT:
                        return getObjHash(value.asObject());
                    default:
                        /* Value::VALTYPE_EMPTY */
                        break;
                }
                return 0;
            }

            static const char* objClassTypename(Object* obj);

            NEON_FORCEINLINE static const char* objTypename(ObjType ot, Object* obj)
            {
                switch(ot)
                {
                    case OBJTYPE_MODULE:
                        return "module";
                    case OBJTYPE_RANGE:
                        return "range";
                    case OBJTYPE_FILE:
                        return "file";
                    case OBJTYPE_DICT:
                        return "dictionary";
                    case OBJTYPE_ARRAY:
                        return "array";
                    case OBJTYPE_CLASS:
                        return "class";
                    case OBJTYPE_FUNCSCRIPT:
                    case OBJTYPE_FUNCNATIVE:
                    case OBJTYPE_FUNCCLOSURE:
                    case OBJTYPE_FUNCBOUND:
                        return "function";
                    case OBJTYPE_INSTANCE:
                        {
                            return objClassTypename(obj);
                        }
                        break;
                    case OBJTYPE_STRING:
                        return "string";
                    case OBJTYPE_USERDATA:
                        return "userdata";
                    case OBJTYPE_SWITCH:
                        return "switch";
                    default:
                        break;
                }
                return "unknown";
            }

            NEON_FORCEINLINE static const char* Typename(Value value)
            {
                if(value.isEmpty())
                {
                    return "empty";
                }
                if(value.isNull())
                {
                    return "null";
                }
                else if(value.isBool())
                {
                    return "boolean";
                }
                else if(value.isNumber())
                {
                    return "number";
                }
                else if(value.isObject())
                {
                    return objTypename(value.objectType(), value.asObject());
                }
                return "unknown";
            }


        public:
            ValType m_valtype;
            union
            {
                bool boolean;
                double number;
                Object* obj;
            } m_valunion;

        public:
            bool compareObject(State* state, Value b);

            NEON_FORCEINLINE bool compareActual(State* state, Value b)
            {
                if(type() != b.type())
                {
                    return false;
                }
                switch(type())
                {
                    case Value::VALTYPE_NULL:
                    case Value::VALTYPE_EMPTY:
                        {
                            return true;
                        }
                        break;
                    case Value::VALTYPE_BOOL:
                        {
                            return asBool() == b.asBool();
                        }
                        break;
                    case Value::VALTYPE_NUMBER:
                        {
                            return (asNumber() == b.asNumber());
                        }
                        break;
                    case Value::VALTYPE_OBJECT:
                        {
                            if(asObject() == b.asObject())
                            {
                                return true;
                            }
                            return compareObject(state, b);
                        }
                        break;
                    default:
                        break;
                }
                return false;
            }


            NEON_FORCEINLINE bool compare(State* state, Value b)
            {
                bool r;
                r = compareActual(state, b);
                return r;
            }

            Value::ObjType objectType();

            NEON_FORCEINLINE ValType type() const
            {
                return m_valtype;
            }

            NEON_FORCEINLINE bool isPrimitive() const
            {
                return (
                    isEmpty() ||
                    isNull() ||
                    isBool() ||
                    isNumber()
                );
            }


            NEON_FORCEINLINE bool isObject() const
            {
                return (m_valtype == VALTYPE_OBJECT);
            }

            NEON_FORCEINLINE bool isNull() const
            {
                return (m_valtype == VALTYPE_NULL);
            }

            NEON_FORCEINLINE bool isEmpty() const
            {
                return (m_valtype == VALTYPE_EMPTY);
            }

            NEON_FORCEINLINE bool isObjType(ObjType t) const;

            NEON_FORCEINLINE bool isFuncNative() const
            {
                return isObjType(OBJTYPE_FUNCNATIVE);
            }

            NEON_FORCEINLINE bool isFuncScript() const
            {
                return isObjType(OBJTYPE_FUNCSCRIPT);
            }

            NEON_FORCEINLINE bool isFuncClosure() const
            {
                return isObjType(OBJTYPE_FUNCCLOSURE);
            }

            NEON_FORCEINLINE bool isFuncBound() const
            {
                return isObjType(OBJTYPE_FUNCBOUND);
            }

            NEON_FORCEINLINE bool isClass() const
            {
                return isObjType(OBJTYPE_CLASS);
            }

            NEON_FORCEINLINE bool isCallable() const
            {
                return (
                    isClass() ||
                    isFuncScript() ||
                    isFuncClosure() ||
                    isFuncBound() ||
                    isFuncNative()
                );
            }

            NEON_FORCEINLINE bool isString() const
            {
                return isObjType(OBJTYPE_STRING);
            }

            NEON_FORCEINLINE bool isBool() const
            {
                return (type() == Value::VALTYPE_BOOL);
            }

            NEON_FORCEINLINE bool isNumber() const
            {
                return (type() == Value::VALTYPE_NUMBER);
            }

            NEON_FORCEINLINE bool isInstance() const
            {
                return isObjType(OBJTYPE_INSTANCE);
            }

            NEON_FORCEINLINE bool isArray() const
            {
                return isObjType(OBJTYPE_ARRAY);
            }

            NEON_FORCEINLINE bool isDict() const
            {
                return isObjType(OBJTYPE_DICT);
            }

            NEON_FORCEINLINE bool isFile() const
            {
                return isObjType(OBJTYPE_FILE);
            }

            NEON_FORCEINLINE bool isRange() const
            {
                return isObjType(OBJTYPE_RANGE);
            }

            NEON_FORCEINLINE bool isModule() const
            {
                return isObjType(OBJTYPE_MODULE);
            }

            NEON_FORCEINLINE Object* asObject()
            {
                return (m_valunion.obj);
            }

            NEON_FORCEINLINE const Object* asObject() const
            {
                return (m_valunion.obj);
            }

            NEON_FORCEINLINE double asNumber() const
            {
                return (m_valunion.number);
            }

            NEON_FORCEINLINE String* asString() const
            {
                return (String*)asObject();
            }

            NEON_FORCEINLINE Array* asArray() const
            {
                return (Array*)asObject();
            }

            NEON_FORCEINLINE ClassInstance* asInstance() const
            {
                return (ClassInstance*)asObject();
            }

            NEON_FORCEINLINE Module* asModule() const
            {
                return (Module*)asObject();
            }

            NEON_FORCEINLINE FuncScript* asFuncScript() const
            {
                return (FuncScript*)asObject();
            }

            NEON_FORCEINLINE FuncClosure* asFuncClosure() const
            {
                return (FuncClosure*)asObject();
            }

            NEON_FORCEINLINE bool asBool() const
            {
                return (m_valunion.boolean);
            }

            NEON_FORCEINLINE FuncNative* asFuncNative() const
            {
                return (FuncNative*)asObject();
            }

            NEON_FORCEINLINE ClassObject* asClass() const
            {
                return (ClassObject*)asObject();
            }

            NEON_FORCEINLINE FuncBound* asFuncBound() const
            {
                return (FuncBound*)asObject();
            }

            NEON_FORCEINLINE VarSwitch* asSwitch() const
            {
                return (VarSwitch*)asObject();
            }

            NEON_FORCEINLINE Userdata* asUserdata() const
            {
                return (Userdata*)asObject();
            }

            NEON_FORCEINLINE Dictionary* asDict() const
            {
                return (Dictionary*)asObject();
            }

            NEON_FORCEINLINE File* asFile() const
            {
                return (File*)asObject();
            }

            NEON_FORCEINLINE Range* asRange() const
            {
                return (Range*)asObject();
            }
    };

    struct Arguments
    {
        public:
            State* m_pvm;
            const char* funcname;
            Value thisval;
            int argc;
            Value* argv;
            void* userptr;

        public:
            NEON_FORCEINLINE Arguments(State* state, const char* n, Value tv, Value* vargv, int vargc, void* uptr): m_pvm(state)
            {
                this->funcname = n;
                this->thisval = tv;
                this->argc = vargc;
                this->argv = vargv;
                this->userptr = uptr;
            }
    };

    struct State final
    {
        public:
            using CallbackFN = Value (*)(State*, Arguments*);

            struct ExceptionFrame
            {
                uint16_t address;
                uint16_t finallyaddress;
                ClassObject* klass;
            };

            struct CallFrame
            {
                int handlercount;
                int gcprotcount;
                int stackslotpos;
                Instruction* inscode;
                FuncClosure* closure;
                ExceptionFrame handlers[NEON_CFG_MAXEXCEPTHANDLERS];
            };

            struct GC
            {

                /*
                static void* allocate(State* state, size_t size, size_t amount)
                {
                    return GC::reallocate(state, nullptr, 0, size * amount);
                }
                */

                static void* allocate(State* state, size_t tsize, size_t count)
                {
                    void* result;
                    size_t actualsz;
                    // this is a dummy; this function used be 'reallocate()'
                    size_t oldsize;
                    actualsz = (tsize * count);
                    oldsize = 0;
                    state->gcMaybeCollect(actualsz - oldsize, actualsz > oldsize);
                    result = Memory::osMalloc(actualsz);
                    if(result == nullptr)
                    {
                        fprintf(stderr, "fatal error: failed to allocate %ld bytes (size: %ld, count: %ld)\n", actualsz, tsize, count);
                        abort();
                    }
                    return result;
                }

                static void gcRelease(State* state, void* pointer, size_t oldsize)
                {
                    state->gcMaybeCollect(-oldsize, false);
                    if(oldsize > 0)
                    {
                        memset(pointer, 0, oldsize);
                    }
                    Memory::osFree(pointer);
                    pointer = nullptr;
                }

                template<typename Type, typename... ArgsT>
                static Type* gcCreate(State* state, ArgsT&&... args)
                {
                    Type* rt;
                    Type* buf;
                    buf = (Type*)State::GC::allocate(state, sizeof(Type), 1);
                    rt = new(buf) Type(args...);
                    return rt;
                }

            };

        public:
            struct
            {
                /* for switching through the command line args... */
                bool enablewarnings = false;
                bool dumpbytecode = false;
                bool exitafterbytecode = false;
                bool dumpinstructions = false;
                bool enablestrictmode = false;
                bool showfullstack = false;
                bool enableapidebug = false;
                bool enableastdebug = false;
                bool dumpprintstack = true;
            } m_conf;

            struct
            {
                size_t stackidx = 0;
                size_t stackcapacity = 0;
                size_t framecapacity = 0;
                size_t framecount = 0;
                Instruction currentinstr;
                CallFrame* currentframe = nullptr;
                ScopeUpvalue* openupvalues = nullptr;
                Object* linkedobjects = nullptr;
                CallFrame* framevalues = nullptr;
                Value* stackvalues = nullptr;
            } m_vmstate;

            struct
            {
                int graycount;
                int graycapacity;
                int bytesallocated;
                int nextgc;
                Object** graystack;
            } m_gcstate;


            struct {
                ClassObject* stdexception;
                ClassObject* syntaxerror;
                ClassObject* asserterror;
                ClassObject* ioerror;
                ClassObject* oserror;
                ClassObject* argumenterror;
            } m_exceptions;

            void* m_memuserptr;

            Parser* m_activeparser;

            char* m_rootphysfile;

            Dictionary* m_envdict;

            String* m_constructorname;
            Module* m_toplevelmodule;
            ValArray* m_modimportpath;

            /* objects tracker */
            HashTable* m_loadedmodules;
            HashTable* m_cachedstrings;
            HashTable* m_definedglobals;
            HashTable* m_namedexceptions;

            /* object public methods */
            ClassObject* m_classprimprocess;
            ClassObject* m_classprimobject;
            ClassObject* m_classprimnumber;
            ClassObject* m_classprimstring;
            ClassObject* m_classprimarray;
            ClassObject* m_classprimdict;
            ClassObject* m_classprimfile;
            ClassObject* m_classprimrange;

            bool m_isreplmode;
            bool m_currentmarkvalue;
            Array* m_cliargv;
            File* m_filestdout;
            File* m_filestderr;
            File* m_filestdin;

            /* miscellaneous */
            Printer* m_stdoutprinter;
            Printer* m_stderrprinter;
            Printer* m_debugprinter;

        private:
            void init(void* userptr);

            bool exceptionPushHandler(ClassObject* type, int address, int finallyaddress)
            {
                State::CallFrame* frame;
                frame = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                if(frame->handlercount == NEON_CFG_MAXEXCEPTHANDLERS)
                {
                    raiseFatal("too many nested exception handlers in one function");
                    return false;
                }
                frame->handlers[frame->handlercount].address = address;
                frame->handlers[frame->handlercount].finallyaddress = finallyaddress;
                frame->handlers[frame->handlercount].klass = type;
                frame->handlercount++;
                return true;
            }

            bool exceptionHandleUncaught(ClassInstance* exception);
            Value getExceptionStacktrace();
            ClassObject* makeExceptionClass(Module* module, const char* cstrname);
            bool exceptionPropagate();

            ClassInstance* makeExceptionInstance(ClassObject* exklass, String* message);
            
            ClassObject* findExceptionByName(const char* name);
            
            template<typename... ArgsT>
            bool raiseAtSourceLocation(ClassObject* exklass, const char* format, ArgsT&&... args);

            bool checkMaybeResize()
            {
                if((m_vmstate.stackidx+1) >= m_vmstate.stackcapacity)
                {
                    if(!resizeStack(m_vmstate.stackidx + 1))
                    {
                        return raiseClass(m_exceptions.stdexception, "failed to resize stack due to overflow");
                    }
                    return true;
                }
                if(m_vmstate.framecount >= m_vmstate.framecapacity)
                {
                    if(!resizeFrames(m_vmstate.framecapacity + 1))
                    {
                        return raiseClass(m_exceptions.stdexception, "failed to resize frames due to overflow");
                    }
                    return true;
                }
                return false;
            }

            /*
            * grows m_vmstate.(stack|frame)values, respectively.
            * currently it works fine with mob.js (man-or-boy test), although
            * there are still some invalid reads regarding the closure;
            * almost definitely because the pointer address changes.
            *
            * currently, the implementation really does just increase the
            * memory block available:
            * i.e., [Value x 32] -> [Value x <newsize>], without
            * copying anything beyond primitive values.
            */
            bool resizeStack(size_t needed)
            {
                size_t oldsz;
                size_t newsz;
                size_t allocsz;
                size_t nforvals;
                Value* oldbuf;
                Value* newbuf;
                if(((int)needed < 0) /* || (needed < m_vmstate.stackcapacity) */)
                {
                    return true;
                }
                nforvals = (needed * 2);
                oldsz = m_vmstate.stackcapacity;
                newsz = oldsz + nforvals;
                allocsz = ((newsz + 1) * sizeof(Value));
                fprintf(stderr, "*** resizing stack: needed %ld (%ld), from %ld to %ld, allocating %ld ***\n", nforvals, needed, oldsz, newsz, allocsz);
                oldbuf = m_vmstate.stackvalues;
                newbuf = (Value*)Memory::osRealloc(oldbuf, allocsz );
                if(newbuf == nullptr)
                {
                    fprintf(stderr, "internal error: failed to resize stackvalues!\n");
                    abort();
                }
                m_vmstate.stackvalues = (Value*)newbuf;
                fprintf(stderr, "oldcap=%ld newsz=%ld\n", m_vmstate.stackcapacity, newsz);
                m_vmstate.stackcapacity = newsz;
                return true;
            }

            bool resizeFrames(size_t needed)
            {
                /* return false; */
                size_t oldsz;
                size_t newsz;
                size_t allocsz;
                int oldhandlercnt;
                Instruction* oldip;
                FuncClosure* oldclosure;
                State::CallFrame* oldbuf;
                State::CallFrame* newbuf;
                fprintf(stderr, "*** resizing frames ***\n");
                oldclosure = m_vmstate.currentframe->closure;
                oldip = m_vmstate.currentframe->inscode;
                oldhandlercnt = m_vmstate.currentframe->handlercount;
                oldsz = m_vmstate.framecapacity;
                newsz = oldsz + needed;
                allocsz = ((newsz + 1) * sizeof(State::CallFrame));
                #if 1
                    oldbuf = m_vmstate.framevalues;
                    newbuf = (State::CallFrame*)Memory::osRealloc(oldbuf, allocsz);
                    if(newbuf == nullptr)
                    {
                        fprintf(stderr, "internal error: failed to resize framevalues!\n");
                        abort();
                    }
                #endif
                m_vmstate.framevalues = (State::CallFrame*)newbuf;
                m_vmstate.framecapacity = newsz;
                /*
                * this bit is crucial: realloc changes pointer addresses, and to keep the
                * current frame, re-read it from the new address.
                */
                m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                m_vmstate.currentframe->handlercount = oldhandlercnt;
                m_vmstate.currentframe->inscode = oldip;
                m_vmstate.currentframe->closure = oldclosure;
                return true;
            }

            void gcMaybeCollect(int addsize, bool wasnew)
            {
                m_gcstate.bytesallocated += addsize;
                if(m_gcstate.nextgc > 0)
                {
                    if(wasnew && m_gcstate.bytesallocated > m_gcstate.nextgc)
                    {
                        if(m_vmstate.currentframe)
                        {
                            if(m_vmstate.currentframe->gcprotcount == 0)
                            {
                                gcCollectGarbage();
                            }
                        }
                    }
                }
            }

            void gcCollectGarbage();
            void gcTraceRefs();
            void gcMarkRoots();
            void gcMarkCompilerRoots();
            void gcSweep();
            void gcDestroyLinkedObjects();

        public:
            State()
            {
                init(nullptr);
            }

            ~State();

            template<typename SubObjT>
            SubObjT* gcProtect(SubObjT* object)
            {
                size_t frpos;
                stackPush(Value::fromObject(object));
                frpos = 0;
                if(m_vmstate.framecount > 0)
                {
                    frpos = m_vmstate.framecount - 1;
                }
                m_vmstate.framevalues[frpos].gcprotcount++;
                return object;
            }

            void clearProtect()
            {
                size_t frpos;
                CallFrame* frame;
                frpos = 0;
                if(m_vmstate.framecount > 0)
                {
                    frpos = m_vmstate.framecount - 1;
                }
                frame = &m_vmstate.framevalues[frpos];
                if(frame->gcprotcount > 0)
                {
                    m_vmstate.stackidx -= frame->gcprotcount;
                }
                frame->gcprotcount = 0;
            }

            template<typename... ArgsT>
            void raiseFatal(const char* format, ArgsT&&... args);

            template<typename... ArgsT>
            void warn(const char* fmt, ArgsT&&... args)
            {
                static auto fn_fprintf = fprintf;
                if(m_conf.enablewarnings)
                {
                    fprintf(stderr, "WARNING: ");
                    fn_fprintf(stderr, fmt, args...);
                    fprintf(stderr, "\n");
                }
            }

            template<typename... ArgsT>
            bool raiseClass(ClassObject* exklass, const char* format, ArgsT&&... args)
            {
                return raiseAtSourceLocation(exklass, format, args...);
            }

            template<typename... ArgsT>
            bool raiseClass(const char* clname, const char* format, ArgsT&&... args)
            {
                ClassObject* tmp;
                ClassObject* exklass;
                exklass = m_exceptions.stdexception;
                tmp = findExceptionByName(clname);
                if(tmp != nullptr)
                {
                    exklass = tmp;
                }
                return raiseAtSourceLocation(exklass, format, args...);
            }

            template<typename... ArgsT>
            Value raiseFromFunction(Arguments* fnargs, const char* fmt, ArgsT&&... args)
            {
                stackPop(fnargs->argc);
                raiseClass(m_exceptions.stdexception, fmt, args...);
                return Value::makeBool(false);
            }

            FuncClosure* compileScriptSource(Module* module, bool fromeval, const char* source);
            Value evalSource(const char* source);
            Status execSource(Module* module, const char* source, Value* dest);

            void defGlobalValue(const char* name, Value val);
            void defNativeFunction(const char* name, CallbackFN fptr);
            bool callClosure(FuncClosure* closure, Value thisval, int argcount);
            Status runVM(int exitframe, Value* rv);

            void resetVMState()
            {
                m_vmstate.framecount = 0;
                m_vmstate.stackidx = 0;
                m_vmstate.openupvalues = nullptr;
            }

            bool addSearchPathObjString(String* os);
            bool addSearchPath(const char* path);

            ClassObject* makeNamedClass(const char* name, ClassObject* parent);

            NEON_FORCEINLINE uint8_t readByte()
            {
                uint8_t r;
                r = m_vmstate.currentframe->inscode->code;
                m_vmstate.currentframe->inscode++;
                return r;
            }

            NEON_FORCEINLINE Instruction readInstruction()
            {
                Instruction r;
                r = *m_vmstate.currentframe->inscode;
                m_vmstate.currentframe->inscode++;
                return r;
            }

            NEON_FORCEINLINE uint16_t readShort()
            {
                uint8_t b;
                uint8_t a;
                a = m_vmstate.currentframe->inscode[0].code;
                b = m_vmstate.currentframe->inscode[1].code;
                m_vmstate.currentframe->inscode += 2;
                return (uint16_t)((a << 8) | b);
            }

            NEON_FORCEINLINE Value readConst();

            NEON_FORCEINLINE String* readString()
            {
                return readConst().asString();
            }


            NEON_FORCEINLINE void stackPush(Value value)
            {
                checkMaybeResize();
                m_vmstate.stackvalues[m_vmstate.stackidx] = value;
                m_vmstate.stackidx++;
            }

            NEON_FORCEINLINE Value stackPop()
            {
                Value v;
                m_vmstate.stackidx--;
                v = m_vmstate.stackvalues[m_vmstate.stackidx];
                return v;
            }

            NEON_FORCEINLINE Value stackPop(int n)
            {
                Value v;
                m_vmstate.stackidx -= n;
                v = m_vmstate.stackvalues[m_vmstate.stackidx];
                return v;
            }

            NEON_FORCEINLINE Value stackPeek(int distance)
            {
                Value v;
                v = m_vmstate.stackvalues[m_vmstate.stackidx + (-1 - distance)];
                return v;
            }
    };

    struct Object
    {
        public:
            template<typename SubObjT, class = std::enable_if_t<std::is_base_of<Object, SubObjT>::value>>
            static SubObjT* make(State* state, Value::ObjType type)
            {
                size_t size;
                Object* plain;
                (void)size;
                (void)plain;
                size = sizeof(SubObjT);
                plain = State::GC::gcCreate<SubObjT>(state);
                plain->m_objtype = type;
                plain->m_mark = !state->m_currentmarkvalue;
                plain->m_isstale = false;
                plain->m_pvm = state;
                plain->m_nextobj = state->m_vmstate.linkedobjects;
                state->m_vmstate.linkedobjects = plain;
                #if defined(NEON_CFG_DEBUGGC) && NEON_CFG_DEBUGGC
                state->m_debugprinter->putformat("%p allocate %ld for %d\n", (void*)plain, size, type);
                #endif
                return static_cast<SubObjT*>(plain);
            }

            template<typename SubObjT, class = std::enable_if_t<std::is_base_of<Object, SubObjT>::value>>
            static void release(State* state, SubObjT* ptr)
            {
                if(ptr != nullptr)
                {
                    ptr->destroyThisObject();
                    ptr->m_objtype = (Value::ObjType)0;
                    ptr->m_nextobj = nullptr;
                    State::GC::gcRelease(state, ptr, sizeof(SubObjT));
                }
            }

            static void markObject(State* state, Object* object)
            {
                if(object == nullptr)
                {
                    return;
                }
                if(object->m_mark == state->m_currentmarkvalue)
                {
                    return;
                }
                #if defined(NEON_CFG_DEBUGGC) && NEON_CFG_DEBUGGC
                state->m_debugprinter->putformat("GC: marking object at <%p> ", (void*)object);
                state->m_debugprinter->printValue(Value::fromObject(object), false);
                state->m_debugprinter->putformat("\n");
                #endif
                object->m_mark = state->m_currentmarkvalue;
                if(state->m_gcstate.graycapacity < state->m_gcstate.graycount + 1)
                {
                    state->m_gcstate.graycapacity = Util::growCapacity(state->m_gcstate.graycapacity);
                    state->m_gcstate.graystack = (Object**)Memory::osRealloc(state->m_gcstate.graystack, sizeof(Object*) * state->m_gcstate.graycapacity);
                    if(state->m_gcstate.graystack == nullptr)
                    {
                        fflush(stdout);
                        fprintf(stderr, "GC encountered an error");
                        abort();
                    }
                }
                state->m_gcstate.graystack[state->m_gcstate.graycount++] = object;
            }

            static void markValue(State* state, Value value)
            {
                if(value.isObject())
                {
                    markObject(state, value.asObject());
                }
            }

            static void blackenObject(State* state, Object* object);
            static void destroyObject(State* state, Object* object);

        public:
            Value::ObjType m_objtype;
            bool m_mark;
            State* m_pvm;
            /*
            // when an object is marked as stale, it means that the
            // GC will never collect this object. This can be useful
            // for library/package objects that want to reuse native
            // objects in their types/pointers. The GC cannot reach
            // them yet, so it's best for them to be kept stale.
            */
            bool m_isstale;
            Object* m_nextobj;

        public:
            //Object() = delete;
            //~Object() = delete;
            virtual bool destroyThisObject()
            {
                return false;
            }
    };

    struct BasicArray
    {
        public:
            template<typename ValType>
            static inline ValType* growArray(State* state, ValType* pointer, size_t oldcount, size_t newcount)
            {
                size_t tsz;
                (void)state;
                (void)oldcount;
                tsz = sizeof(ValType);
                //return (ValType*)State::GC::reallocate(state, pointer, tsz * oldcount, tsz * newcount);
                return (ValType*)Memory::osRealloc(pointer, tsz*newcount);
            }

            template<typename ValType>
            static inline void freeArray(State* state, ValType* pointer, size_t oldcount)
            {
                size_t tsz;
                tsz = sizeof(ValType);
                State::GC::gcRelease(state, pointer, tsz * oldcount);
            }
    };

    template<typename ValType>
    struct GenericArray: public BasicArray
    {
        public:
            State* m_pvm;
            /* how many entries are currently stored? */
            uint64_t m_count;
            /* how many entries can be stored before growing? */
            uint64_t m_capacity;
            ValType* m_values;

        private:
            bool destroy()
            {
                if(m_values != nullptr)
                {
                    freeArray(m_pvm, m_values, m_capacity);
                    m_values = nullptr;
                    m_count = 0;
                    m_capacity = 0;
                    return true;
                }
                return false;
            }

        public:
            GenericArray(): GenericArray(nullptr)
            {
            }

            GenericArray(State* state)
            {
                m_pvm = state;
                m_capacity = 0;
                m_count = 0;
                m_values = nullptr;
            }

            ~GenericArray()
            {
                destroy();
            }

            NEON_FORCEINLINE uint64_t size() const
            {
                return m_count;
            }

            NEON_FORCEINLINE uint64_t length() const
            {
                return m_count;
            }

            NEON_FORCEINLINE uint64_t count() const
            {
                return m_count;
            }

            NEON_FORCEINLINE ValType& at(uint64_t i)
            {
                return m_values[i];
            }

            NEON_FORCEINLINE ValType& operator[](uint64_t i)
            {
                return at(i);
            }

            void push(ValType value)
            {
                uint64_t oldcapacity;
                if(m_capacity < m_count + 1)
                {
                    oldcapacity = m_capacity;
                    m_capacity = Util::growCapacity(oldcapacity);
                    m_values = growArray(m_pvm, m_values, oldcapacity, m_capacity);
                }
                m_values[m_count] = value;
                m_count++;
            }

            void insertDefault(ValType value, uint64_t index, ValType defaultvalue)
            {
                uint64_t i;
                uint64_t oldcap;
                if(m_capacity <= index)
                {
                    m_capacity = Util::growCapacity(index);
                    m_values = growArray(m_pvm, m_values, m_count, m_capacity);
                }
                else if(m_capacity < m_count + 2)
                {
                    oldcap = m_capacity;
                    m_capacity = Util::growCapacity(oldcap);
                    m_values = growArray(m_pvm, m_values, oldcap, m_capacity);
                }
                if(index <= m_count)
                {
                    for(i = m_count - 1; i >= index; i--)
                    {
                        m_values[i + 1] = m_values[i];
                    }
                }
                else
                {
                    for(i = m_count; i < index; i++)
                    {
                        /* null out overflow indices */
                        m_values[i] = defaultvalue;
                        m_count++;
                    }
                }
                m_values[index] = value;
                m_count++;
            }

            ValType shiftDefault(uint64_t count, ValType defval)
            {
                uint64_t i;
                uint64_t j;
                uint64_t vsz;
                ValType temp;
                ValType item;
                vsz = m_count;
                if(count >= vsz || vsz == 1)
                {
                    m_count = 0;
                    return defval;
                }
                item = m_values[0];
                j = 0;
                for(i=1; i<vsz; i++)
                {
                    temp = m_values[i];
                    m_values[j] = temp;
                    j++;
                }
                m_count--;
                return item;
            }


    };

    struct ValArray: public GenericArray<Value>
    {
        public:
            struct QuickSort
            {
                public:
                    template<typename ArrT>
                    NEON_FORCEINLINE static void swapGeneric(ArrT* varr, size_t idxleft, size_t idxright)
                    {
                        auto temp = varr[idxleft];
                        varr[idxleft] = varr[idxright];
                        varr[idxright] = temp;
                    }

                    NEON_FORCEINLINE static void swap(ValArray* varr, size_t idxleft, size_t idxright)
                    {
                        /*
                        Value temp;
                        temp = varr->m_values[idxleft];
                        varr->m_values[idxleft] = varr->m_values[idxright];
                        varr->m_values[idxright] = temp;
                        */
                        swapGeneric(varr->m_values, idxleft, idxright);
                    }

                private:
                    State* m_pvm;

                public:
                    QuickSort(State* state): m_pvm(state)
                    {
                    }

                    /* This function is same in both iterative and recursive*/
                    int partition(ValArray* arr, int64_t istart, int64_t iend)
                    {
                        int64_t i;
                        int64_t j;
                        Value x;
                        x = arr->m_values[iend];
                        i = (istart - 1);
                        for (j = istart; (j <= (iend - 1)); j++)
                        {
                            //if (arr[j] <= x)
                            if(Value::isLessOrEqual(m_pvm, arr->m_values[j], x))
                            {
                                i++;
                                swap(arr, i, j);
                            }
                        }
                        swap(arr, i+1, iend);
                        return (i + 1);
                    } 
                      
                    /*
                        A[] --> Array to be sorted,  
                        idxstart --> Starting index,
                        idxend --> Ending index
                    */
                    void runSort(ValArray* arr, int64_t idxstart, int64_t idxend)
                    {
                        #if 0
                            int64_t p;
                            int64_t top;
                            // Create an auxiliary stack
                            int64_t stack[idxend - idxstart + 1];
                            // initialize top of stack
                            top = 0;
                            // push initial values of idxstart and idxend to stack
                            stack[top+0] = idxstart;
                            stack[top+1] = idxend;
                            top += 1;
                            // Keep popping from stack while is not empty
                            while(top >= 0)
                            {
                                // Pop idxend and idxstart
                                idxend = stack[top - 0];
                                idxstart = stack[top - 1];
                                top -= 2;
                                // Set pivot element at its correct position
                                // in sorted array
                                p = partition(arr, idxstart, idxend);
                                // If there are elements on left side of pivot,
                                // then push left side to stack
                                if((p - 1) > idxstart)
                                {
                                    stack[top+1] = idxstart;
                                    stack[top+2] = p - 1;
                                    top += 2;
                                }
                                // If there are elements on right side of pivot,
                                // then push right side to stack
                                if (p + 1 < idxend)
                                {
                                    stack[top+1] = p + 1;
                                    stack[top+2] = idxend;
                                    top += 2;
                                }
                            }
                        #else
                            if(idxstart < idxend)
                            {
                                int p = partition(arr, idxstart, idxend);
                                runSort(arr, idxstart, p - 1);
                                runSort(arr, p + 1, idxend);
                            }
                        #endif
                    }
            };
        public:
            using GenericArray::GenericArray;

            void gcMark()
            {
                uint64_t i;
                for(i = 0; i < m_count; i++)
                {
                    Object::markValue(m_pvm, m_values[i]);
                }
            }

            void insert(Value value, uint64_t index)
            {
                insertDefault(value, index, Value::makeNull());
            }

            Value shift()
            {
                return shiftDefault(1, Value::makeNull());
            }

    };

    struct Property
    {
        public:
            enum Type
            {
                PROPTYPE_INVALID,
                PROPTYPE_VALUE,
                /*
                * indicates this field contains a function, a pseudo-getter (i.e., ".length")
                * which is called upon getting
                */
                PROPTYPE_FUNCTION,
            };

            struct GetSetter
            {
                Value getter;
                Value setter;
            };

        public:
            Type m_proptype;
            Value m_actualval;
            bool m_havegetset;
            GetSetter m_getset;

        public:
            void init(Value val, Type t)
            {
                m_proptype = t;
                m_actualval = val;
                m_havegetset = false;
            }

            Property(Value val, Value getter, Value setter, Type t)
            {
                bool getisfn;
                bool setisfn;
                init(val, t);
                setisfn = setter.isCallable();
                getisfn = getter.isCallable();
                if(getisfn || setisfn)
                {
                    m_getset.setter = setter;
                    m_getset.getter = getter;
                }
            }

            Property(Value val, Type t)
            {
                init(val, t);
            }
    };

    struct HashTable final
    {
        public:
            struct Entry
            {
                Value key;
                Property value;
            };

        public:
            /*
            * FIXME: extremely stupid hack: $m_active ensures that a table that was destroyed
            * does not get marked again, et cetera.
            * since ~HashTable() zeroes the data before freeing, $m_active will be
            * false, and thus, no further marking occurs.
            * obviously the reason is that somewhere a table (from ClassInstance) is being
            * read after being freed, but for now, this will work(-ish).
            */
            State* m_pvm;
            bool m_active;
            uint64_t m_count;
            uint64_t m_capacity;
            Entry* m_entries;

        public:
            HashTable(State* state): m_pvm(state)
            {
                m_active = true;
                m_count = 0;
                m_capacity = 0;
                m_entries = nullptr;
            }

            ~HashTable()
            {
                BasicArray::freeArray(m_pvm, m_entries, m_capacity);
            }

            NEON_FORCEINLINE uint64_t size() const
            {
                return m_count;
            }

            NEON_FORCEINLINE uint64_t length() const
            {
                return m_count;
            }

            NEON_FORCEINLINE uint64_t count() const
            {
                return m_count;
            }

            Entry* findEntryByValue(Entry* availents, int availcap, Value key)
            {
                uint32_t hash;
                uint32_t index;
                State* state;
                Entry* entry;
                Entry* tombstone;
                state = m_pvm;
                hash = Value::getHash(key);
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "looking for key ");
                state->m_debugprinter->printValue(key, true, false);
                fprintf(stderr, " with hash %u in table...\n", hash);
                #endif
                index = hash & (availcap - 1);
                tombstone = nullptr;
                while(true)
                {
                    entry = &availents[index];
                    if(entry->key.isEmpty())
                    {
                        if(entry->value.m_actualval.isNull())
                        {
                            /* empty entry */
                            if(tombstone != nullptr)
                            {
                                return tombstone;
                            }
                            else
                            {
                                return entry;
                            }
                        }
                        else
                        {
                            /* we found a tombstone. */
                            if(tombstone == nullptr)
                            {
                                tombstone = entry;
                            }
                        }
                    }
                    else if(key.compare(state, entry->key))
                    {
                        return entry;
                    }
                    index = (index + 1) & (availcap - 1);
                }
                return nullptr;
            }

            bool compareEntryString(Value vkey, const char* kstr, size_t klen);

            Entry* findEntryByStr(Entry* availents, int availcap, Value valkey, const char* kstr, size_t klen, uint32_t hash)
            {
                bool havevalhash;
                uint32_t index;
                uint32_t valhash;
                HashTable::Entry* entry;
                HashTable::Entry* tombstone;
                State* state;
                state = m_pvm;
                (void)valhash;
                (void)havevalhash;
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "looking for key ");
                state->m_debugprinter->printValue(key, true, false);
                fprintf(stderr, " with hash %u in table...\n", hash);
                #endif
                valhash = 0;
                havevalhash = false;
                index = hash & (availcap - 1);
                tombstone = nullptr;
                while(true)
                {
                    entry = &availents[index];
                    if(entry->key.isEmpty())
                    {
                        if(entry->value.m_actualval.isNull())
                        {
                            /* empty entry */
                            if(tombstone != nullptr)
                            {
                                return tombstone;
                            }
                            else
                            {
                                return entry;
                            }
                        }
                        else
                        {
                            /* we found a tombstone. */
                            if(tombstone == nullptr)
                            {
                                tombstone = entry;
                            }
                        }
                    }
                    if(entry->key.isString())
                    {
                        /*
                            entoskey = entry->key.asString();
                            if(entoskey->length() == klen)
                            {
                                if(memcmp(kstr, entoskey->data(), klen) == 0)
                                {
                                    return entry;
                                }
                            }
                        */
                        if(compareEntryString(entry->key, kstr, klen))
                        {
                            return entry;
                        }
                    }
                    else
                    {
                        if(!valkey.isEmpty())
                        {
                            if(valkey.compare(state, entry->key))
                            {
                                return entry;
                            }
                        }
                    }
                    index = (index + 1) & (availcap - 1);
                }
                return nullptr;
            }

            Property* getFieldByValue(Value key)
            {
                State* state;
                Entry* entry;
                (void)state;
                state = m_pvm;
                if(m_count == 0 || m_entries == nullptr)
                {
                    return nullptr;
                }
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "getting entry with hash %u...\n", Value::getHash(key));
                #endif
                entry = findEntryByValue(m_entries, m_capacity, key);
                if(entry->key.isEmpty() || entry->key.isNull())
                {
                    return nullptr;
                }
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "found entry for hash %u == ", Value::getHash(entry->key));
                state->m_debugprinter->printValue(entry->value.m_actualval, true, false);
                fprintf(stderr, "\n");
                #endif
                return &entry->value;
            }

            Property* getFieldByStr(Value valkey, const char* kstr, size_t klen, uint32_t hash)
            {
                State* state;
                Entry* entry;
                (void)state;
                state = m_pvm;
                if(m_count == 0 || m_entries == nullptr)
                {
                    return nullptr;
                }
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "getting entry with hash %u...\n", Value::getHash(key));
                #endif
                entry = findEntryByStr(m_entries, m_capacity, valkey, kstr, klen, hash);
                if(entry->key.isEmpty() || entry->key.isNull())
                {
                    return nullptr;
                }
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "found entry for hash %u == ", Value::getHash(entry->key));
                state->m_debugprinter->printValue(entry->value.m_actualval, true, false);
                fprintf(stderr, "\n");
                #endif
                return &entry->value;
            }

            Property* getByObjString(String* str);

            Property* getByCStr(const char* kstr)
            {
                size_t klen;
                uint32_t hash;
                klen = strlen(kstr);
                hash = Util::hashString(kstr, klen);
                return getFieldByStr(Value::makeEmpty(), kstr, klen, hash);
            }

            Property* getField(Value key);

            bool get(Value key, Value* value)
            {
                Property* field;
                field = getField(key);
                if(field != nullptr)
                {
                    *value = field->m_actualval;
                    return true;
                }
                return false;
            }

            void adjustCapacity(uint64_t needcap)
            {
                uint64_t i;
                State* state;
                Entry* dest;
                Entry* entry;
                Entry* nents;
                state = m_pvm;
                //nents = (Entry*)State::GC::allocate(state, sizeof(Entry), needcap);
                nents = (Entry*)Memory::osMalloc(sizeof(Entry) * needcap);
                for(i = 0; i < needcap; i++)
                {
                    nents[i].key = Value::makeEmpty();
                    nents[i].value = Property(Value::makeNull(), Property::PROPTYPE_VALUE);
                }
                /* repopulate buckets */
                m_count = 0;
                for(i = 0; i < m_capacity; i++)
                {
                    entry = &m_entries[i];
                    if(entry->key.isEmpty())
                    {
                        continue;
                    }
                    dest = findEntryByValue(nents, needcap, entry->key);
                    dest->key = entry->key;
                    dest->value = entry->value;
                    m_count++;
                }
                /* free the old entries... */
                BasicArray::freeArray(state, m_entries, m_capacity);
                m_entries = nents;
                m_capacity = needcap;
            }

            bool setType(Value key, Value value, Property::Type ftyp, bool keyisstring)
            {
                bool isnew;
                uint64_t needcap;
                Entry* entry;
                (void)keyisstring;
                if(m_count + 1 > m_capacity * NEON_CFG_MAXTABLELOAD)
                {
                    needcap = Util::growCapacity(m_capacity);
                    adjustCapacity(needcap);
                }
                entry = findEntryByValue(m_entries, m_capacity, key);
                isnew = entry->key.isEmpty();
                if(isnew && entry->value.m_actualval.isNull())
                {
                    m_count++;
                }
                /* overwrites existing entries. */
                entry->key = key;
                entry->value = Property(value, ftyp);
                return isnew;
            }

            bool set(Value key, Value value)
            {
                return setType(key, value, Property::PROPTYPE_VALUE, key.isString());
            }

            bool setCStrType(const char* cstrkey, Value value, Property::Type ftype);

            bool setCStr(const char* cstrkey, Value value)
            {
                return setCStrType(cstrkey, value, Property::PROPTYPE_VALUE);
            }

            bool removeByKey(Value key)
            {
                Entry* entry;
                if(m_count == 0)
                {
                    return false;
                }
                /* find the entry */
                entry = findEntryByValue(m_entries, m_capacity, key);
                if(entry->key.isEmpty())
                {
                    return false;
                }
                /* place a tombstone in the entry. */
                entry->key = Value::makeEmpty();
                entry->value = Property(Value::makeBool(true), Property::PROPTYPE_VALUE);
                return true;
            }

            static void addAll(HashTable* from, HashTable* to)
            {
                uint64_t i;
                Entry* entry;
                for(i = 0; i < from->m_capacity; i++)
                {
                    entry = &from->m_entries[i];
                    if(!entry->key.isEmpty())
                    {
                        to->setType(entry->key, entry->value.m_actualval, entry->value.m_proptype, false);
                    }
                }
            }

            static void copy(HashTable* from, HashTable* to)
            {
                uint64_t i;
                State* state;
                Entry* entry;
                state = from->m_pvm;
                for(i = 0; i < from->m_capacity; i++)
                {
                    entry = &from->m_entries[i];
                    if(!entry->key.isEmpty())
                    {
                        to->setType(entry->key, Value::copyValue(state, entry->value.m_actualval), entry->value.m_proptype, false);
                    }
                }
            }

            String* findString(const char* chars, size_t length, uint32_t hash);

            Value findKey(Value value)
            {
                uint64_t i;
                Entry* entry;
                for(i = 0; i < m_capacity; i++)
                {
                    entry = &m_entries[i];
                    if(!entry->key.isNull() && !entry->key.isEmpty())
                    {
                        if(entry->value.m_actualval.compare(m_pvm, value))
                        {
                            return entry->key;
                        }
                    }
                }
                return Value::makeNull();
            }

            void printTo(Printer* pd, const char* name);

            static void mark(State* state, HashTable* table)
            {
                uint64_t i;
                Entry* entry;
                if(table == nullptr)
                {
                    return;
                }
                if(!table->m_active)
                {
                    state->warn("trying to mark inactive hashtable <%p>!", table);
                    return;
                }
                for(i = 0; i < table->m_capacity; i++)
                {
                    entry = &table->m_entries[i];
                    if(entry != nullptr)
                    {
                        Object::markValue(state, entry->key);
                        Object::markValue(state, entry->value.m_actualval);
                    }
                }
            }

            static void removeMarked(State* state, HashTable* table)
            {
                uint64_t i;
                Entry* entry;
                for(i = 0; i < table->m_capacity; i++)
                {
                    entry = &table->m_entries[i];
                    if(entry->key.isObject() && entry->key.asObject()->m_mark != state->m_currentmarkvalue)
                    {
                        table->removeByKey(entry->key);
                    }
                }
            }
    };

    struct Blob
    {
        public:
            State* m_pvm;
            uint64_t m_count;
            uint64_t m_capacity;
            Instruction* m_instrucs;
            ValArray* m_constants;
            ValArray* m_argdefvals;

        public:
            Blob(State* state): m_pvm(state)
            {
                m_count = 0;
                m_capacity = 0;
                m_instrucs = nullptr;
                m_constants = Memory::create<ValArray>(state);
                m_argdefvals = Memory::create<ValArray>(state);
            }

            ~Blob()
            {
                if(m_instrucs != nullptr)
                {
                    BasicArray::freeArray(m_pvm, m_instrucs, m_capacity);
                }
                Memory::destroy(m_constants);
                Memory::destroy(m_argdefvals);
            }

            void push(Instruction ins)
            {
                uint64_t oldcapacity;
                if(m_capacity < m_count + 1)
                {
                    oldcapacity = m_capacity;
                    m_capacity = Util::growCapacity(oldcapacity);
                    m_instrucs = BasicArray::growArray(m_pvm, m_instrucs, oldcapacity, m_capacity);
                }
                m_instrucs[m_count] = ins;
                m_count++;
            }

            int pushConst(Value value)
            {
                m_constants->push(value);
                return m_constants->m_count - 1;
            }

            int pushArgDefault(Value value)
            {
                m_argdefvals->push(value);
                return m_argdefvals->m_count - 1;
            }
    };

    NEON_FORCEINLINE bool Value::isObjType(Value::ObjType t) const
    {
        return isObject() && asObject()->m_objtype == t;
    }

    struct String final: public Object
    {
        public:
            static String* numToBinString(State* state, long n)
            {
                int i;
                int rem;
                int count;
                int length;
                long j;
                /* assume maximum of 1024 bits */
                char str[1024];
                char newstr[1027];
                count = 0;
                j = n;
                if(j == 0)
                {
                    str[count++] = '0';
                }
                while(j != 0)
                {
                    rem = abs((int)(j % 2));
                    j /= 2;
                    if(rem == 1)
                    {
                        str[count] = '1';
                    }
                    else
                    {
                        str[count] = '0';
                    }
                    count++;
                }
                /* assume maximum of 1024 bits + 0b (indicator) + sign (-). */
                length = 0;
                if(n < 0)
                {
                    newstr[length++] = '-';
                }
                newstr[length++] = '0';
                newstr[length++] = 'b';
                for(i = count - 1; i >= 0; i--)
                {
                    newstr[length++] = str[i];
                }
                newstr[length++] = 0;
                return String::copy(state, newstr, length);
                /*
                    // To store the binary number
                    long long number = 0;
                    int cnt = 0;
                    while(n != 0)
                    {
                        long long rem = n % 2;
                        long long c = (long long) pow(10, cnt);
                        number += rem * c;
                        n /= 2;
                        // Count used to store exponent value
                        cnt++;
                    }
                    char str[67]; // assume maximum of 64 bits + 2 binary indicators (0b)
                    int length = sprintf(str, "0b%lld", number);
                    return String::copy(state, str, length);
                */
            }

            static String* numToOctString(State* state, long long n, bool numeric)
            {
                int length;
                /* assume maximum of 64 bits + 2 octal indicators (0c) */
                char str[66];
                length = sprintf(str, numeric ? "0c%llo" : "%llo", n);
                return String::copy(state, str, length);
            }

            static String* numToHexString(State* state, long long n, bool numeric)
            {
                int length;
                /* assume maximum of 64 bits + 2 hex indicators (0x) */
                char str[66];
                length = sprintf(str, numeric ? "0x%llx" : "%llx", n);
                return String::copy(state, str, length);
            }

            static String* makeFromStrbuf(State* state, Util::StrBuffer* sbuf, uint32_t hash)
            {
                String* rs;
                rs = Object::make<String>(state, Value::OBJTYPE_STRING);
                rs->m_sbuf = sbuf;
                rs->m_hash = hash;
                //fprintf(stderr, "String: sbuf(%zd)=\"%.*s\"\n", sbuf->m_length, (int)sbuf->m_length, sbuf->data());
                selfCachePut(state, rs);
                return rs;
            }

            static String* take(State* state, char* chars, int length)
            {
                uint32_t hash;
                String* rs;
                hash = Util::hashString(chars, length);
                rs = selfCacheFind(state, chars, length, hash);
                if(rs == nullptr)
                {
                    rs = makeFromChars(state, chars, length, hash, true);
                }
                BasicArray::freeArray(state, chars, (size_t)length + 1);
                return rs;
            }

            static String* copy(State* state, const char* chars, int length)
            {
                uint32_t hash;
                String* rs;
                hash = Util::hashString(chars, length);
                rs = selfCacheFind(state, chars, length, hash);
                if(rs != nullptr)
                {
                    return rs;
                }
                rs = makeFromChars(state, chars, length, hash, false);
                return rs;
            }

            static String* take(State* state, char* chars)
            {
                return take(state, chars, strlen(chars));
            }

            static String* copy(State* state, const char* chars)
            {
                return copy(state, chars, strlen(chars));
            }

            static String* copyObjString(State* state, String* os)
            {
                return copy(state, os->data(), os->length());
            }

            // FIXME: does not actually return the range yet
            static String* fromRange(State* state, const char* buf, size_t len, size_t begin, size_t end)
            {
                String* str;
                (void)begin;
                (void)end;
                if(int(len) <= 0)
                {
                    return String::copy(state, "", 0);
                }
                str = String::copy(state, "", 0);
                str->append(buf, len);
                return str;
            }

        private:
            static String* makeFromChars(State* state, const char* estr, size_t elen, uint32_t hash, bool istaking)
            {
                Util::StrBuffer* sbuf;
                (void)istaking;
                sbuf = Memory::create<Util::StrBuffer>(elen);
                sbuf->append(estr, elen);
                return makeFromStrbuf(state, sbuf, hash);
            }

            static void selfCachePut(State* state, String* rs)
            {
                state->stackPush(Value::fromObject(rs));
                state->m_cachedstrings->set(Value::fromObject(rs), Value::makeNull());
                state->stackPop();
            }

            static String* selfCacheFind(State* state, const char* chars, size_t length, uint32_t hash)
            {
                return state->m_cachedstrings->findString(chars, length, hash);
            }

        public:
            uint32_t m_hash;
            Util::StrBuffer* m_sbuf;

        public:
            //String() = delete;
            //~String() = delete;

            inline bool destroyThisObject()
            {
                if(m_sbuf != nullptr)
                {
                    Memory::destroy(m_sbuf);
                    m_sbuf = nullptr;
                    return true;
                }
                return false;
            }

            NEON_FORCEINLINE uint64_t size() const
            {
                return m_sbuf->m_length;
            }

            NEON_FORCEINLINE uint64_t length() const
            {
                return m_sbuf->m_length;
            }

            NEON_FORCEINLINE const char* data() const
            {
                return m_sbuf->data();
            }

            NEON_FORCEINLINE char* mutableData()
            {
                return m_sbuf->m_data;
            }

            NEON_FORCEINLINE const char* cstr() const
            {
                return data();
            }

            NEON_FORCEINLINE int at(uint64_t pos) const
            {
                return m_sbuf->m_data[pos];
            }

            NEON_FORCEINLINE void set(uint64_t pos, int c)
            {
                m_sbuf->m_data[pos] = c;
            }

            String* substr(uint64_t start, uint64_t end, bool likejs) const
            {
                uint64_t asz;
                uint64_t len;
                uint64_t tmp;
                uint64_t maxlen;
                char* raw;
                (void)likejs;
                maxlen = length();
                len = maxlen;
                if(end > maxlen)
                {
                    tmp = start;
                    start = end;
                    end = tmp;
                    len = maxlen;
                }
                if(end < start)
                {
                    tmp = end;
                    end = start;
                    start = tmp;
                    len = end;
                }
                len = (end - start);
                if(len > maxlen)
                {
                    len = maxlen;
                }
                asz = ((end + 1) * sizeof(char));
                //raw = (char*)State::GC::allocate(m_pvm, sizeof(char), asz);
                raw = (char*)Memory::osMalloc(sizeof(char) * asz);
                memset(raw, 0, asz);
                memcpy(raw, data() + start, len);
                return String::take(m_pvm, raw, len);
            }

            template<typename... ArgsT>
            void append(ArgsT&&... args)
            {
                m_sbuf->append(args...);
            }
    };

    struct ScopeUpvalue final: public Object
    {
        public:
            static ScopeUpvalue* make(State* state, Value* slot, int stackpos)
            {
                ScopeUpvalue* rt;
                rt = Object::make<ScopeUpvalue>(state, Value::OBJTYPE_UPVALUE);
                rt->m_closed = Value::makeNull();
                rt->m_location = *slot;
                rt->m_nextupval = nullptr;
                rt->m_stackpos = stackpos;
                return rt;
            }

        public:
            int m_stackpos;
            Value m_closed;
            Value m_location;
            ScopeUpvalue* m_nextupval;

        public:
            //ScopeUpvalue() = delete;
            //~ScopeUpvalue() = delete;

            bool destroyThisObject()
            {
                return true;
            }
    };

    struct Array final: public Object
    {
        public:
            static Array* make(State* state, uint64_t cnt, Value filler)
            {
                uint64_t i;
                Array* rt;
                rt = Object::make<Array>(state, Value::OBJTYPE_ARRAY);
                rt->m_varray = Memory::create<ValArray>(state);
                if(cnt > 0)
                {
                    for(i=0; i<cnt; i++)
                    {
                        rt->push(filler);
                    }
                }
                return rt;
            }

            static Array* make(State* state)
            {
                return make(state, 0, Value::makeEmpty());
            }

        public:
            ValArray* m_varray;

        public:
            //Array() = delete;
            //~Array() = delete;

            bool destroyThisObject()
            {
                Memory::destroy(m_varray);
                return true;
            }

            NEON_FORCEINLINE size_t size() const
            {
                return m_varray->size();
            }

            NEON_FORCEINLINE size_t length() const
            {
                return m_varray->size();
            }

            NEON_FORCEINLINE size_t count() const
            {
                return m_varray->size();
            }

            NEON_FORCEINLINE Value at(size_t pos) const
            {
                return m_varray->m_values[pos];
            }

            void clear()
            {
            }

            NEON_FORCEINLINE void push(Value value)
            {
                /*m_pvm->stackPush(value);*/
                m_varray->push(value);
                /*m_pvm->stackPop(); */
            }

            Array* copy(int start, int length)
            {
                int i;
                Array* newlist;
                newlist = m_pvm->gcProtect(Array::make(m_pvm));
                if(start == -1)
                {
                    start = 0;
                }
                if(length == -1)
                {
                    length = m_varray->m_count - start;
                }
                for(i = start; i < start + length; i++)
                {
                    newlist->push(m_varray->m_values[i]);
                }
                return newlist;
            }
    };

    struct Range final: public Object
    {
        public:
            static Range* make(State* state, int lower, int upper)
            {
                Range* rt;
                rt = Object::make<Range>(state, Value::OBJTYPE_RANGE);
                rt->m_lower = lower;
                rt->m_upper = upper;
                if(upper > lower)
                {
                    rt->m_range = upper - lower;
                }
                else
                {
                    rt->m_range = lower - upper;
                }
                return rt;
            }


        public:
            int m_lower;
            int m_upper;
            int m_range;

        public:
            //Range() = delete;
            //~Range() = delete;

            bool destroyThisObject()
            {
                return true;
            }
    };

    struct Dictionary final: public Object
    {
        public:
            static Dictionary* make(State* state)
            {
                Dictionary* rt;
                rt = Object::make<Dictionary>(state, Value::OBJTYPE_DICT);
                rt->m_keynames = Memory::create<ValArray>(state);
                rt->m_valtable = Memory::create<HashTable>(state);
                return rt;
            }

        public:
            ValArray* m_keynames;
            HashTable* m_valtable;

        public:
            //Dictionary() = delete;
            //~Dictionary() = delete;

            NEON_FORCEINLINE size_t size() const
            {
                return m_keynames->size();
            }

            NEON_FORCEINLINE size_t length() const
            {
                return m_keynames->size();
            }

            NEON_FORCEINLINE size_t count() const
            {
                return m_keynames->size();
            }

            bool destroyThisObject()
            {
                Memory::destroy(m_keynames);
                Memory::destroy(m_valtable);
                return true;
            }

            bool setEntry(Value key, Value value)
            {
                auto field =  m_valtable->getField(key);
                if(field == nullptr)
                {
                    /* add key if it doesn't exist. */
                    m_keynames->push(key);
                }
                return m_valtable->set(key, value);
            }

            void addEntry(Value key, Value value)
            {
                setEntry(key, value);
            }

            void addEntryCStr(const char* ckey, Value value)
            {
                String* os;
                os = String::copy(m_pvm, ckey);
                addEntry(Value::fromObject(os), value);
            }

            Property* getEntry(Value key)
            {
                return m_valtable->getField(key);
            }
    };

    struct File final: public Object
    {
        public:
            struct IOResult
            {
                bool success;
                char* data;
                size_t length;    
            };

        public:
            static File* make(State* state, FILE* handle, bool isstd, const char* path, const char* mode)
            {
                File* rt;
                rt = Object::make<File>(state, Value::OBJTYPE_FILE);
                rt->m_isopen = false;
                rt->m_filemode = String::copy(state, mode);
                rt->m_filepath = String::copy(state, path);
                rt->m_isstd = isstd;
                rt->m_filehandle = handle;
                rt->m_istty = false;
                rt->m_filedesc = -1;
                if(rt->m_filehandle != nullptr)
                {
                    rt->m_isopen = true;
                }
                return rt;
            }

            static void mark(State* state, File* file)
            {
                Object::markObject(state, (Object*)file->m_filemode);
                Object::markObject(state, (Object*)file->m_filepath);
            }

        public:
            bool m_isopen;
            bool m_isstd;
            bool m_istty;
            int m_filedesc;
            FILE* m_filehandle;
            String* m_filemode;
            String* m_filepath;

        public:
            //File() = delete;
            //~File() = delete;

            bool destroyThisObject()
            {
                nn_fileobject_close(this);
                return true;
            }

            bool readChunk(size_t readhowmuch, IOResult* dest)
            {
                size_t filesizereal;
                struct stat stats;
                filesizereal = -1;
                dest->success = false;
                dest->length = 0;
                dest->data = nullptr;
                if(!m_isstd)
                {
                    if(!nn_util_fsfileexists(m_pvm, m_filepath->data()))
                    {
                        return false;
                    }
                    /* file is in write only mode */
                    /*
                    else if(strstr(m_filemode->data(), "w") != nullptr && strstr(m_filemode->data(), "+") == nullptr)
                    {
                        FILE_ERROR(Unsupported, "cannot read file in write mode");
                    }
                    */
                    if(!m_isopen)
                    {
                        /* open the file if it isn't open */
                        nn_fileobject_open(this);
                    }
                    else if(m_filehandle == nullptr)
                    {
                        return false;
                    }
                    if(osfn_lstat(m_filepath->data(), &stats) == 0)
                    {
                        filesizereal = (size_t)stats.st_size;
                    }
                    else
                    {
                        /* fallback */
                        fseek(m_filehandle, 0L, SEEK_END);
                        filesizereal = ftell(m_filehandle);
                        rewind(m_filehandle);
                    }
                    if(readhowmuch == (size_t)-1 || readhowmuch > filesizereal)
                    {
                        readhowmuch = filesizereal;
                    }
                }
                else
                {
                    /*
                    // for non-file objects such as stdin
                    // minimum read bytes should be 1
                    */
                    if(readhowmuch == (size_t)-1)
                    {
                        readhowmuch = 1;
                    }
                }
                /* +1 for terminator '\0' */
                dest->data = (char*)Memory::osMalloc(sizeof(char) * (readhowmuch + 1));
                if(dest->data == nullptr && readhowmuch != 0)
                {
                    return false;
                }
                dest->length = fread(dest->data, sizeof(char), readhowmuch, m_filehandle);
                if(dest->length == 0 && readhowmuch != 0 && readhowmuch == filesizereal)
                {
                    return false;
                }
                /* we made use of +1 so we can terminate the string. */
                if(dest->data != nullptr)
                {
                    dest->data[dest->length] = '\0';
                }
                return true;
            }


    };

    struct VarSwitch final: public Object
    {
        public:
            static VarSwitch* make(State* state)
            {
                VarSwitch* rt;
                rt = Object::make<VarSwitch>(state, Value::OBJTYPE_SWITCH);
                rt->m_jumppositions = Memory::create<HashTable>(state);
                rt->m_defaultjump = -1;
                rt->m_exitjump = -1;
                return rt;
            }

        public:
            int m_defaultjump;
            int m_exitjump;
            HashTable* m_jumppositions;

        public:
            //VarSwitch() = delete;
            //~VarSwitch() = delete;

            bool destroyThisObject()
            {
                Memory::destroy(m_jumppositions);
                return true;
            }
    };

    struct Userdata final: public Object
    {
        public:
            using CBOnFreeFN = void (*)(void*);

        public:
            static Userdata* make(State* state, void* pointer, const char* name)
            {
                Userdata* rt;
                rt = Object::make<Userdata>(state, Value::OBJTYPE_USERDATA);
                rt->m_udpointer = pointer;
                rt->m_udtypename = Util::strCopy(state, name);
                rt->m_fnondestroy = nullptr;
                return rt;
            }

        public:
            void* m_udpointer;
            char* m_udtypename;
            CBOnFreeFN m_fnondestroy;

        public:
            //Userdata() = delete;
            //~Userdata() = delete;

            bool destroyThisObject()
            {
                if(m_fnondestroy)
                {
                    m_fnondestroy(m_udpointer);
                    return true;
                }
                return false;
            }
    };

    struct FuncCommon: public Object
    {
        public:
            enum Type
            {
                FUNCTYPE_UNSPECIFIED,
                FUNCTYPE_ANONYMOUS,
                FUNCTYPE_FUNCTION,
                FUNCTYPE_METHOD,
                FUNCTYPE_INITIALIZER,
                FUNCTYPE_PRIVATE,
                FUNCTYPE_STATIC,
                FUNCTYPE_SCRIPT,
            };

        public:
            int m_arity = 0;
            FuncCommon::Type m_functype = FUNCTYPE_UNSPECIFIED;
    };

    struct FuncScript final: public FuncCommon
    {
        public:
            static FuncScript* make(State* state, Module* module, FuncCommon::Type type)
            {
                FuncScript* rt;
                rt = Object::make<FuncScript>(state, Value::OBJTYPE_FUNCSCRIPT);
                rt->m_arity = 0;
                rt->m_upvalcount = 0;
                rt->m_isvariadic = false;
                rt->m_scriptfnname = nullptr;
                rt->m_functype = type;
                rt->m_inmodule = module;
                rt->m_compiledblob = Memory::create<Blob>(state);
                return rt;
            }

        public:
            int m_upvalcount;
            bool m_isvariadic;
            Blob* m_compiledblob;
            String* m_scriptfnname;
            Module* m_inmodule;

        public:
            bool destroyThisObject()
            {
                Memory::destroy(m_compiledblob);
                return true;
            }
    };

    struct FuncClosure final: public FuncCommon
    {
        public:
            static FuncClosure* make(State* state, FuncScript* function)
            {
                int i;
                ScopeUpvalue** upvals;
                FuncClosure* rt;
                //upvals = (ScopeUpvalue**)State::GC::allocate(state, sizeof(ScopeUpvalue*), function->m_upvalcount);
                upvals = (ScopeUpvalue**)Memory::osMalloc(sizeof(ScopeUpvalue*) * function->m_upvalcount);
                for(i = 0; i < function->m_upvalcount; i++)
                {
                    upvals[i] = nullptr;
                }
                rt = Object::make<FuncClosure>(state, Value::OBJTYPE_FUNCCLOSURE);
                rt->scriptfunc = function;
                rt->m_storedupvals = upvals;
                rt->m_upvalcount = function->m_upvalcount;
                return rt;
            }

        public:
            int m_upvalcount;
            FuncScript* scriptfunc;
            ScopeUpvalue** m_storedupvals;

        public:
            bool destroyThisObject()
            {
                BasicArray::freeArray(m_pvm, m_storedupvals, m_upvalcount);
                /*
                // there may be multiple closures that all reference the same function
                // for this reason, we do not free functions when freeing closures
                */
                return true;
            }
    };

    struct FuncBound final: public FuncCommon
    {
        public:
            static FuncBound* make(State* state, Value receiver, FuncClosure* method)
            {
                FuncBound* rt;
                rt = Object::make<FuncBound>(state, Value::OBJTYPE_FUNCBOUND);
                rt->receiver = receiver;
                rt->method = method;
                return rt;
            }

        public:
            Value receiver;
            FuncClosure* method;

        public:
            //FuncBound() = delete;
            //~FuncBound() = delete;

            bool destroyThisObject()
            {
                return true;
            }
    };

    struct FuncNative final: public FuncCommon
    {
        public:
            static FuncNative* make(State* state, State::CallbackFN function, const char* name, void* uptr)
            {
                FuncNative* rt;
                rt = Object::make<FuncNative>(state, Value::OBJTYPE_FUNCNATIVE);
                rt->m_natfunc = function;
                rt->m_nativefnname = name;
                rt->m_userptrforfn = uptr;
                rt->m_functype = FuncCommon::FUNCTYPE_FUNCTION;
                return rt;
            }

            static FuncNative* make(State* state, State::CallbackFN function, const char* name)
            {
                return make(state, function, name, nullptr);
            }

        public:
            const char* m_nativefnname;
            State::CallbackFN m_natfunc;
            void* m_userptrforfn;

        public:
            //FuncNative() = delete;
            //~FuncNative() = delete;

            bool destroyThisObject()
            {
                return true;
            }
    };

    struct NestCall
    {
        public:
            State* m_pvm;

        public:
            NestCall(State* state): m_pvm(state)
            {
            }

            int prepare(Value callable, Value mthobj, Array* callarr)
            {
                int arity;
                FuncClosure* closure;
                arity = 0;
                if(callable.isFuncClosure())
                {
                    closure = callable.asFuncClosure();
                    arity = closure->scriptfunc->m_arity;
                }
                else if(callable.isFuncScript())
                {
                    arity = callable.asFuncScript()->m_arity;
                }
                else if(callable.isFuncNative())
                {
                    //arity = callable.asFuncNative();
                }
                if(arity > 0)
                {
                    callarr->push(Value::makeNull());
                    if(arity > 1)
                    {
                        callarr->push(Value::makeNull());
                        if(arity > 2)
                        {
                            callarr->push(mthobj);
                        }
                    }
                }
                return arity;
            }

            /* helper function to access call outside the state file. */
            bool call(Value callable, Value thisval, Array* args, Value* dest)
            {
                size_t i;
                size_t argc;
                size_t pidx;
                size_t vsz;
                Status status;
                pidx = m_pvm->m_vmstate.stackidx;
                /* set the closure before the args */
                m_pvm->stackPush(callable);
                argc = 0;
                vsz = args->size();
                if(args && ((argc = vsz)) > 0)
                {
                    for(i = 0; i < vsz; i++)
                    {
                        m_pvm->stackPush(args->at(i));
                    }
                }
                if(!nn_vm_callvaluewithobject(m_pvm, callable, thisval, argc))
                {
                    fprintf(stderr, "nestcall: nn_vm_callvalue() failed\n");
                    abort();
                }
                status = m_pvm->runVM(m_pvm->m_vmstate.framecount - 1, nullptr);
                if(status != Status::OK)
                {
                    fprintf(stderr, "nestcall: call to runvm failed\n");
                    abort();
                }
                *dest = m_pvm->m_vmstate.stackvalues[m_pvm->m_vmstate.stackidx - 1];
                m_pvm->stackPop(argc + 1);
                m_pvm->m_vmstate.stackidx = pidx;
                return true;
            }
    };

    struct ClassObject final: public Object
    {
        public:
            static ClassObject* make(State* state, String* name)
            {
                ClassObject* rt;
                rt = Object::make<ClassObject>(state, Value::OBJTYPE_CLASS);
                rt->m_classname = name;
                rt->m_instprops = Memory::create<HashTable>(state);
                rt->m_staticprops = Memory::create<HashTable>(state);
                rt->m_methods = Memory::create<HashTable>(state);
                rt->m_staticmethods = Memory::create<HashTable>(state);
                rt->m_constructor = Value::makeEmpty();
                rt->m_superclass = nullptr;
                return rt;
            }

        public:
            /*
            * the constructor, if any. defaults to <empty>, and if not <empty>, expects to be
            * some callable value.
            */
            Value m_constructor;

            /*
            * when declaring a class, $m_instprops (their names, and initial values) are
            * copied to ClassInstance::m_properties.
            * so `$m_instprops["something"] = somefunction;` remains untouched *until* an
            * instance of this class is created.
            */
            HashTable* m_instprops;

            /*
            * static, unchangeable(-ish) values. intended for values that are not unique, but shared
            * across classes, without being copied.
            */
            HashTable* m_staticprops;

            /*
            * method table; they are currently not unique when instantiated; instead, they are
            * read from $m_methods as-is. this includes adding methods!
            * TODO: introduce a new hashtable field for ClassInstance for unique methods, perhaps?
            * right now, this method just prevents unnecessary copying.
            */
            HashTable* m_methods;
            HashTable* m_staticmethods;
            String* m_classname;
            ClassObject* m_superclass;

        public:
            //ClassObject() = delete;
            //~ClassObject() = delete;

            bool destroyThisObject()
            {
                Memory::destroy(m_methods);
                Memory::destroy(m_staticmethods);
                Memory::destroy(m_instprops);
                Memory::destroy(m_staticprops);
                return true;
            }

            void inheritFrom(ClassObject* superclass)
            {
                HashTable::addAll(superclass->m_instprops, m_instprops);
                HashTable::addAll(superclass->m_methods, m_methods);
                m_superclass = superclass;
            }

            void defProperty(const char* cstrname, Value val)
            {
                m_instprops->setCStr(cstrname, val);
            }

            void defCallableField(const char* cstrname, State::CallbackFN function)
            {
                String* oname;
                FuncNative* ofn;
                oname = String::copy(m_pvm, cstrname);
                ofn = FuncNative::make(m_pvm, function, cstrname);
                m_instprops->setType(Value::fromObject(oname), Value::fromObject(ofn), Property::PROPTYPE_FUNCTION, true);
            }

            void setStaticPropertyCstr(const char* cstrname, Value val)
            {
                m_staticprops->setCStr(cstrname, val);
            }

            void setStaticProperty(String* name, Value val)
            {
                setStaticPropertyCstr(name->data(), val);
            }

            void defNativeConstructor(State::CallbackFN function)
            {
                const char* cname;
                FuncNative* ofn;
                cname = "constructor";
                ofn = FuncNative::make(m_pvm, function, cname);
                m_constructor = Value::fromObject(ofn);
            }

            void defMethod(const char* name, Value val)
            {
                m_methods->setCStr(name, val);
            }

            void defNativeMethod(const char* name, State::CallbackFN function)
            {
                FuncNative* ofn;
                ofn = FuncNative::make(m_pvm, function, name);
                defMethod(name, Value::fromObject(ofn));
            }

            void defStaticNativeMethod(const char* name, State::CallbackFN function)
            {
                FuncNative* ofn;
                ofn = FuncNative::make(m_pvm, function, name);
                m_staticmethods->setCStr(name, Value::fromObject(ofn));
            }

            Property* getMethodField(String* name)
            {
                Property* field;
                field = m_methods->getField(Value::fromObject(name));
                if(field != nullptr)
                {
                    return field;
                }
                if(m_superclass != nullptr)
                {
                    return m_superclass->getMethodField(name);
                }
                return nullptr;
            }

            Property* getPropertyField(String* name)
            {
                Property* field;
                field = m_instprops->getField(Value::fromObject(name));
                return field;
            }

            Property* getStaticProperty(String* name)
            {
                return m_staticprops->getByObjString(name);
            }

            Property* getStaticMethodField(String* name)
            {
                Property* field;
                field = m_staticmethods->getField(Value::fromObject(name));
                return field;
            }
    };

    struct ClassInstance final: public Object
    {
        public:
            static ClassInstance* make(State* state, ClassObject* klass)
            {
                ClassInstance* rt;
                rt = Object::make<ClassInstance>(state, Value::OBJTYPE_INSTANCE);
                /* gc fix */
                state->stackPush(Value::fromObject(rt));
                rt->m_active = true;
                rt->m_fromclass = klass;
                rt->m_properties = Memory::create<HashTable>(state);
                if(rt->m_fromclass->m_instprops->m_count > 0)
                {
                    HashTable::copy(rt->m_fromclass->m_instprops, rt->m_properties);
                }
                /* gc fix */
                state->stackPop();
                return rt;
            }

            static void mark(State* state, ClassInstance* instance)
            {
                if(instance->m_active == false)
                {
                    state->warn("trying to mark inactive instance <%p>!", instance);
                    return;
                }
                Object::markObject(state, (Object*)instance->m_fromclass);
                HashTable::mark(state, instance->m_properties);
            }

        public:
            /*
            * whether this instance is still "active", i.e., not destroyed, deallocated, etc.
            * in rare circumstances s
            */
            bool m_active;
            HashTable* m_properties;
            ClassObject* m_fromclass;

        public:
            //ClassInstance() = delete;
            //~ClassInstance() = delete;

            bool destroyThisObject()
            {
                Memory::destroy(m_properties);
                m_properties = nullptr;
                m_active = false;
                return true;
            }

            void defProperty(const char *cstrname, Value val)
            {
                m_properties->setCStr(cstrname, val);
            }
    };

    struct RegModule
    {
        public:
            using ModInitFN = RegModule* (*)(State*);
            using ClassFieldFN = Value (*)(State*);
            using ModLoaderFN = void (*)(State*);

            struct FuncInfo
            {
                const char* funcname;
                bool isstatic;
                State::CallbackFN function;
            };

            struct FieldInfo
            {
                const char* fieldname;
                bool isstatic;
                ClassFieldFN fieldvalfn;
            };

            struct ClassInfo
            {
                const char* classname;
                FieldInfo* fields;
                FuncInfo* functions;
            };

        public:
            const char* regmodname;
            FieldInfo* fields;
            FuncInfo* functions;
            ClassInfo* classes;
            ModLoaderFN preloader;
            ModLoaderFN unloader;

        public:
            /*
            RegModule(): RegModule(nullptr)
            {
            }
            */

            RegModule(const char* n)
            {
                this->regmodname = n;
                this->fields = nullptr;
                this->functions = nullptr;
                this->classes = nullptr;
                this->preloader = nullptr;
                this->unloader = nullptr;
            }
    };

    struct Module final: public Object
    {
        public:
            static Module* make(State* state, const char* name, const char* file, bool imported)
            {
                Module* rt;
                rt = Object::make<Module>(state, Value::OBJTYPE_MODULE);
                rt->m_deftable = Memory::create<HashTable>(state);
                rt->m_modname = String::copy(state, name);
                rt->m_physlocation = String::copy(state, file);
                rt->m_fnunloader = nullptr;
                rt->m_fnpreloader = nullptr;
                rt->m_libhandle = nullptr;
                rt->m_isimported = imported;
                return rt;
            }

            static char* resolvePath(State* state, const char* modulename, const char* currentfile, char* rootfile, bool isrelative)
            {
                size_t i;
                size_t mlen;
                size_t splen;
                char* path1;
                char* path2;
                char* retme;
                const char* cstrpath;
                struct stat stroot;
                struct stat stmod;
                String* pitem;
                Util::StrBuffer* pathbuf;
                (void)rootfile;
                (void)isrelative;
                (void)stroot;
                (void)stmod;
                pathbuf = nullptr;
                mlen = strlen(modulename);
                splen = state->m_modimportpath->m_count;
                for(i=0; i<splen; i++)
                {
                    pitem = state->m_modimportpath->m_values[i].asString();
                    if(pathbuf == nullptr)
                    {
                        pathbuf = Memory::create<Util::StrBuffer>(pitem->length() + mlen + 5);
                    }
                    else
                    {
                        pathbuf->reset();
                    }
                    pathbuf->append(pitem->data(), pitem->length());
                    if(pathbuf->contains('@'))
                    {
                        pathbuf->replace('@', modulename, mlen);
                    }
                    else
                    {
                        pathbuf->append("/");
                        pathbuf->append(modulename);
                        pathbuf->append(NEON_CFG_FILEEXT);
                    }
                    cstrpath = pathbuf->data(); 
                    fprintf(stderr, "import: trying '%s' ... ", cstrpath);
                    if(nn_util_fsfileexists(state, cstrpath))
                    {
                        fprintf(stderr, "found!\n");
                        /* stop a core library from importing itself */
                        #if 0
                        if(stat(currentfile, &stroot) == -1)
                        {
                            fprintf(stderr, "resolvepath: failed to stat current file '%s'\n", currentfile);
                            return nullptr;
                        }
                        if(stat(cstrpath, &stmod) == -1)
                        {
                            fprintf(stderr, "resolvepath: failed to stat module file '%s'\n", cstrpath);
                            return nullptr;
                        }
                        if(stroot.st_ino == stmod.st_ino)
                        {
                            fprintf(stderr, "resolvepath: refusing to import itself\n");
                            return nullptr;
                        }
                        #endif
                        path1 = osfn_realpath(cstrpath, nullptr);
                        path2 = osfn_realpath(currentfile, nullptr);
                        if(path1 != nullptr && path2 != nullptr)
                        {
                            if(memcmp(path1, path2, (int)strlen(path2)) == 0)
                            {
                                Memory::osFree(path1);
                                Memory::osFree(path2);
                                fprintf(stderr, "resolvepath: refusing to import itself\n");
                                return nullptr;
                            }
                            Memory::osFree(path2);
                            Memory::destroy(pathbuf);
                            pathbuf = nullptr;
                            retme = Util::strCopy(state, path1);
                            Memory::osFree(path1);
                            return retme;
                        }
                    }
                    else
                    {
                        fprintf(stderr, "does not exist\n");
                    }
                }
                if(pathbuf != nullptr)
                {
                    Memory::destroy(pathbuf);
                }
                return nullptr;
            }

            static void addNative(State* state, Module* module, const char* as)
            {
                Value name;
                if(as != nullptr)
                {
                    module->m_modname = String::copy(state, as);
                }
                name = Value::fromObject(String::copyObjString(state, module->m_modname));
                state->stackPush(name);
                state->stackPush(Value::fromObject(module));
                state->m_loadedmodules->set(name, Value::fromObject(module));
                state->stackPop(2);
            }

            static FuncScript* compileModuleSource(State* state, Module* module, const char* source, Blob* blob, bool fromimport, bool keeplast);

            static Module* loadModuleByName(State* state, Module* intomodule, String* modulename)
            {
                int argc;
                size_t fsz;
                char* source;
                char* physpath;
                Blob* blob;
                Value retv;
                Value callable;
                Property* field;
                Array* args;
                String* os;
                Module* module;
                FuncClosure* closure;
                FuncScript* function;
                (void)os;
                (void)argc;
                (void)intomodule;
                field = state->m_loadedmodules->getByObjString(modulename);
                if(field != nullptr)
                {
                    return field->m_actualval.asModule();
                }
                physpath = Module::resolvePath(state, modulename->data(), intomodule->m_physlocation->data(), nullptr, false);
                if(physpath == nullptr)
                {
                    state->raiseClass(state->m_exceptions.stdexception, "module not found: '%s'\n", modulename->data());
                    return nullptr;
                }
                fprintf(stderr, "loading module from '%s'\n", physpath);
                source = Util::fileReadFile(state, physpath, &fsz);
                if(source == nullptr)
                {
                    state->raiseClass(state->m_exceptions.stdexception, "could not read import file %s", physpath);
                    return nullptr;
                }
                blob = Memory::create<Blob>(state);
                module = Module::make(state, modulename->data(), physpath, true);
                Memory::osFree(physpath);
                function = compileModuleSource(state, module, source, blob, true, false);
                Memory::osFree(source);
                closure = FuncClosure::make(state, function);
                callable = Value::fromObject(closure);
                args = Array::make(state);
                NestCall nc(state);
                argc = nc.prepare(callable, Value::makeNull(), args);
                if(!nc.call(callable, Value::makeNull(), args, &retv))
                {
                    Memory::destroy(blob);
                    state->raiseClass(state->m_exceptions.stdexception, "failed to call compiled import closure");
                    return nullptr;
                }
                Memory::destroy(blob);
                return module;
            }

            static bool loadBuiltinModule(State* state, RegModule::ModInitFN init_fn, char* importname, const char* source, void* dlw)
            {
                int j;
                int k;
                Value v;
                Value fieldname;
                Value funcname;
                Value funcrealvalue;
                RegModule::FuncInfo func;
                RegModule::FieldInfo field;
                RegModule* regmod;
                Module* targetmod;
                RegModule::ClassInfo klassreg;
                String* classname;
                FuncNative* native;
                ClassObject* klass;
                HashTable* tabdest;
                regmod = init_fn(state);
                if(regmod != nullptr)
                {
                    targetmod = state->gcProtect(Module::make(state, (char*)regmod->regmodname, source, false));
                    targetmod->m_fnpreloader = (void*)regmod->preloader;
                    targetmod->m_fnunloader = (void*)regmod->unloader;
                    if(regmod->fields != nullptr)
                    {
                        for(j = 0; regmod->fields[j].fieldname != nullptr; j++)
                        {
                            field = regmod->fields[j];
                            fieldname = Value::fromObject(state->gcProtect(String::copy(state, field.fieldname)));
                            v = field.fieldvalfn(state);
                            state->stackPush(v);
                            targetmod->m_deftable->set(fieldname, v);
                            state->stackPop();
                        }
                    }
                    if(regmod->functions != nullptr)
                    {
                        for(j = 0; regmod->functions[j].funcname != nullptr; j++)
                        {
                            func = regmod->functions[j];
                            funcname = Value::fromObject(state->gcProtect(String::copy(state, func.funcname)));
                            funcrealvalue = Value::fromObject(state->gcProtect(FuncNative::make(state, func.function, func.funcname)));
                            state->stackPush(funcrealvalue);
                            targetmod->m_deftable->set(funcname, funcrealvalue);
                            state->stackPop();
                        }
                    }
                    if(regmod->classes != nullptr)
                    {
                        for(j = 0; regmod->classes[j].classname != nullptr; j++)
                        {
                            klassreg = regmod->classes[j];
                            classname = state->gcProtect(String::copy(state, klassreg.classname));
                            klass = state->gcProtect(ClassObject::make(state, classname));
                            if(klassreg.functions != nullptr)
                            {
                                for(k = 0; klassreg.functions[k].funcname != nullptr; k++)
                                {
                                    func = klassreg.functions[k];
                                    funcname = Value::fromObject(state->gcProtect(String::copy(state, func.funcname)));
                                    native = state->gcProtect(FuncNative::make(state, func.function, func.funcname));
                                    if(func.isstatic)
                                    {
                                        native->m_functype = FuncCommon::FUNCTYPE_STATIC;
                                    }
                                    else if(strlen(func.funcname) > 0 && func.funcname[0] == '_')
                                    {
                                        native->m_functype = FuncCommon::FUNCTYPE_PRIVATE;
                                    }
                                    klass->m_methods->set(funcname, Value::fromObject(native));
                                }
                            }
                            if(klassreg.fields != nullptr)
                            {
                                for(k = 0; klassreg.fields[k].fieldname != nullptr; k++)
                                {
                                    field = klassreg.fields[k];
                                    fieldname = Value::fromObject(state->gcProtect(String::copy(state, field.fieldname)));
                                    v = field.fieldvalfn(state);
                                    state->stackPush(v);
                                    tabdest = klass->m_instprops;
                                    if(field.isstatic)
                                    {
                                        tabdest = klass->m_staticprops;
                                    }
                                    tabdest->set(fieldname, v);
                                    state->stackPop();
                                }
                            }
                            targetmod->m_deftable->set(Value::fromObject(classname), Value::fromObject(klass));
                        }
                    }
                    if(dlw != nullptr)
                    {
                        targetmod->m_libhandle = dlw;
                    }
                    Module::addNative(state, targetmod, targetmod->m_modname->data());
                    state->clearProtect();
                    return true;
                }
                else
                {
                    state->warn("Error loading module: %s\n", importname);
                }
                return false;
            }

        public:
            bool m_isimported;
            HashTable* m_deftable;
            String* m_modname;
            String* m_physlocation;
            void* m_fnpreloader;
            void* m_fnunloader;
            void* m_libhandle;

        public:
            //Module() = delete;
            //~Module() = delete;

            bool destroyThisObject()
            {
                Memory::destroy(m_deftable);
                /*
                Memory::osFree(m_modname);
                Memory::osFree(m_physlocation);
                */
                if(m_fnunloader != nullptr && m_isimported)
                {
                    ((RegModule::ModLoaderFN)m_fnunloader)(m_pvm);
                }
                if(m_libhandle != nullptr)
                {
                    nn_import_closemodule(m_libhandle);
                }
                return true;
            }

            void setFileField()
            {
                return;
                m_deftable->setCStr("__file__", Value::fromObject(String::copyObjString(m_pvm, m_physlocation)));
            }
    };

    struct Printer
    {
        public:
            enum PrintMode
            {
                PMODE_UNDEFINED,
                PMODE_STRING,
                PMODE_FILE,
            };

        public:
            State* m_pvm;
            /* if file: should be closed when writer is destroyed? */
            bool m_shouldclose = false;
            /* if file: should write operations be flushed via fflush()? */
            bool m_shouldflush = false;
            /* if string: true if $strbuf was taken via nn_printer_take */
            bool m_stringtaken = false;
            /* was this writer instance created on stack? */
            bool m_fromstack = false;
            bool m_shortenvalues = false;
            size_t m_maxvallength = 0;
            /* the mode that determines what writer actually does */
            PrintMode m_pmode = PMODE_UNDEFINED;
            Util::StrBuffer* m_ptostring = nullptr;
            FILE* m_filehandle = nullptr;

        private:
            void initVars(PrintMode mode)
            {
                m_fromstack = false;
                m_pmode = PMODE_UNDEFINED;
                m_shouldclose = false;
                m_shouldflush = false;
                m_stringtaken = false;
                m_shortenvalues = false;
                m_maxvallength = 15;
                m_ptostring = nullptr;
                m_filehandle = nullptr;
                m_pmode = mode;
            }

            bool destroy()
            {
                if(m_pmode == PMODE_UNDEFINED)
                {
                    return false;
                }
                /*fprintf(stderr, "Printer::dtor: m_pmode=%d\n", m_pmode);*/
                if(m_pmode == PMODE_STRING)
                {
                    if(!m_stringtaken)
                    {
                        Memory::destroy(m_ptostring);
                        return true;
                    }
                    return false;
                }
                else if(m_pmode == PMODE_FILE)
                {
                    if(m_shouldclose)
                    {
                        //fclose(m_filehandle);
                    }
                }
                return true;
            }


            void printObjFunction(FuncScript* func)
            {
                if(func->m_scriptfnname == nullptr)
                {
                    putformat("<script at %p>", (void*)func);
                }
                else
                {
                    if(func->m_isvariadic)
                    {
                        putformat("<function %s(%d...) at %p>", func->m_scriptfnname->data(), func->m_arity, (void*)func);
                    }
                    else
                    {
                        putformat("<function %s(%d) at %p>", func->m_scriptfnname->data(), func->m_arity, (void*)func);
                    }
                }
            }

            void printObjArray(Array* list)
            {
                size_t i;
                size_t vsz;
                bool isrecur;
                Value val;
                Array* subarr;
                vsz = list->size();
                putformat("[");
                for(i = 0; i < vsz; i++)
                {
                    isrecur = false;
                    val = list->at(i);
                    if(val.isArray())
                    {
                        subarr = val.asArray();
                        if(subarr == list)
                        {
                            isrecur = true;
                        }
                    }
                    if(isrecur)
                    {
                        putformat("<recursion>");
                    }
                    else
                    {
                        printValue(val, true, true);
                    }
                    if(i != vsz - 1)
                    {
                        putformat(", ");
                    }
                    if(m_shortenvalues && (i >= m_maxvallength))
                    {
                        putformat(" [%ld items]", vsz);
                        break;
                    }
                }
                putformat("]");
            }

            void printObjDict(Dictionary* dict)
            {
                size_t i;
                size_t dsz;
                bool keyisrecur;
                bool valisrecur;
                Value val;
                Dictionary* subdict;
                Property* field;
                dsz = dict->m_keynames->m_count;
                putformat("{");
                for(i = 0; i < dsz; i++)
                {
                    valisrecur = false;
                    keyisrecur = false;
                    val = dict->m_keynames->m_values[i];
                    if(val.isDict())
                    {
                        subdict = val.asDict();
                        if(subdict == dict)
                        {
                            valisrecur = true;
                        }
                    }
                    if(valisrecur)
                    {
                        putformat("<recursion>");
                    }
                    else
                    {
                        printValue(val, true, true);
                    }
                    putformat(": ");
                    field = dict->m_valtable->getField(dict->m_keynames->m_values[i]);
                    if(field != nullptr)
                    {
                        if(field->m_actualval.isDict())
                        {
                            subdict = field->m_actualval.asDict();
                            if(subdict == dict)
                            {
                                keyisrecur = true;
                            }
                        }
                        if(keyisrecur)
                        {
                            putformat("<recursion>");
                        }
                        else
                        {
                            printValue(field->m_actualval, true, true);
                        }
                    }
                    if(i != dsz - 1)
                    {
                        putformat(", ");
                    }
                    if(m_shortenvalues && (m_maxvallength >= i))
                    {
                        putformat(" [%ld items]", dsz);
                        break;
                    }
                }
                putformat("}");
            }

            void printObjFile(File* file)
            {
                putformat("<file at %s in mode %s>", file->m_filepath->data(), file->m_filemode->data());
            }

            void printObjInstance(ClassInstance* instance, bool invmethod)
            {
                (void)invmethod;
                #if 0
                int arity;
                Value resv;
                Value thisval;
                Property* field;
                State* state;
                String* os;
                Array* args;
                state = m_pvm;
                if(invmethod)
                {
                    field = instance->m_fromclass->m_methods->getByCStr("toString");
                    if(field != nullptr)
                    {
                        NestCall nc(state);
                        args = Array::make(state);
                        thisval = Value::fromObject(instance);
                        arity = nc.prepare(field->m_actualval, thisval, args);
                        fprintf(stderr, "arity = %d\n", arity);
                        state->stackPop();
                        state->stackPush(thisval);
                        if(nc.call(field->m_actualval, thisval, args, &resv))
                        {
                            Printer subp(state, &subw);
                            subp.printValue(resv, false, false);
                            os = subp.takeString();
                            put(os->data(), os->length());
                            //state->stackPop();
                            return;
                        }
                    }
                }
                #endif
                putformat("<instance of %s at %p>", instance->m_fromclass->m_classname->data(), (void*)instance);
            }

            void doPrintObject(Value value, bool fixstring, bool invmethod)
            {
                Object* obj;
                obj = m_pvm->gcProtect(value.asObject());
                switch(obj->m_objtype)
                {
                    case Value::OBJTYPE_SWITCH:
                        {
                            put("<switch>");
                        }
                        break;
                    case Value::OBJTYPE_USERDATA:
                        {
                            putformat("<userdata %s>", static_cast<Userdata*>(obj)->m_udtypename);
                        }
                        break;
                    case Value::OBJTYPE_RANGE:
                        {
                            Range* range;
                            range = static_cast<Range*>(obj);
                            putformat("<range %d .. %d>", range->m_lower, range->m_upper);
                        }
                        break;
                    case Value::OBJTYPE_FILE:
                        {
                            printObjFile(static_cast<File*>(obj));
                        }
                        break;
                    case Value::OBJTYPE_DICT:
                        {
                            auto dict = m_pvm->gcProtect(static_cast<Dictionary*>(obj));
                            printObjDict(dict);
                            m_pvm->clearProtect();
                        }
                        break;
                    case Value::OBJTYPE_ARRAY:
                        {
                            auto arr = m_pvm->gcProtect(static_cast<Array*>(obj));
                            printObjArray(arr);
                            m_pvm->clearProtect();
                        }
                        break;
                    case Value::OBJTYPE_FUNCBOUND:
                        {
                            FuncBound* bn;
                            bn = static_cast<FuncBound*>(obj);
                            printObjFunction(bn->method->scriptfunc);
                        }
                        break;
                    case Value::OBJTYPE_MODULE:
                        {
                            Module* mod;
                            mod = static_cast<Module*>(obj);
                            putformat("<module '%s' at '%s'>", mod->m_modname->data(), mod->m_physlocation->data());
                        }
                        break;
                    case Value::OBJTYPE_CLASS:
                        {
                            ClassObject* klass;
                            klass = static_cast<ClassObject*>(obj);
                            putformat("<class %s at %p>", klass->m_classname->data(), (void*)klass);
                        }
                        break;
                    case Value::OBJTYPE_FUNCCLOSURE:
                        {
                            FuncClosure* cls;
                            cls = static_cast<FuncClosure*>(obj);
                            printObjFunction(cls->scriptfunc);
                        }
                        break;
                    case Value::OBJTYPE_FUNCSCRIPT:
                        {
                            FuncScript* fn;
                            fn = static_cast<FuncScript*>(obj);
                            printObjFunction(fn);
                        }
                        break;
                    case Value::OBJTYPE_INSTANCE:
                        {
                            /* @TODO: support the toString() override */
                            ClassInstance* instance;
                            instance = static_cast<ClassInstance*>(obj);
                            printObjInstance(instance, invmethod);
                        }
                        break;
                    case Value::OBJTYPE_FUNCNATIVE:
                        {
                            FuncNative* native;
                            native = static_cast<FuncNative*>(obj);
                            putformat("<function %s(native) at %p>", native->m_nativefnname, (void*)native);
                        }
                        break;
                    case Value::OBJTYPE_UPVALUE:
                        {
                            putformat("<upvalue>");
                        }
                        break;
                    case Value::OBJTYPE_STRING:
                        {
                            String* string;
                            string = m_pvm->gcProtect(static_cast<String*>(obj));
                            if(fixstring)
                            {
                                putQuotedString(string->data(), string->length(), true);
                            }
                            else
                            {
                                put(string->data(), string->length());
                            }
                            m_pvm->clearProtect();
                        }
                        break;
                }
                m_pvm->clearProtect();
            }

            void doPrintValue(Value value, bool fixstring, bool invmethod)
            {
                switch(value.type())
                {
                    case Value::VALTYPE_EMPTY:
                        {
                            put("<empty>");
                        }
                        break;
                    case Value::VALTYPE_NULL:
                        {
                            put("null");
                        }
                        break;
                    case Value::VALTYPE_BOOL:
                        {
                            put(value.asBool() ? "true" : "false");
                        }
                        break;
                    case Value::VALTYPE_NUMBER:
                        {
                            putformat("%.16g", value.asNumber());
                        }
                        break;
                    case Value::VALTYPE_OBJECT:
                        {
                            doPrintObject(value, fixstring, invmethod);
                        }
                        break;
                    default:
                        break;
                }
            }

        public:
            Printer(State* state, PrintMode mode): m_pvm(state)
            {
                initVars(mode);
            }

            Printer(State* state, FILE* fh, bool shouldclose): Printer(state, PMODE_FILE)
            {
                m_filehandle = fh;
                m_shouldclose = shouldclose;
            }

            Printer(State* state): Printer(state, PMODE_STRING)
            {
                m_fromstack = true;
                m_pmode = PMODE_STRING;
                m_ptostring = Memory::create<Util::StrBuffer>(0);
            }

            ~Printer()
            {
                destroy();
            }

            String* takeString()
            {
                uint32_t hash;
                State* state;
                String* os;
                state = m_pvm;
                hash = Util::hashString(m_ptostring->data(), m_ptostring->length());
                os = String::makeFromStrbuf(state, m_ptostring, hash);
                m_stringtaken = true;
                return os;
            }

            bool put(const char* estr, size_t elen)
            {
                if(m_pmode == PMODE_FILE)
                {
                    fwrite(estr, sizeof(char), elen, m_filehandle);
                    if(m_shouldflush)
                    {
                        fflush(m_filehandle);
                    }
                }
                else if(m_pmode == PMODE_STRING)
                {
                    m_ptostring->append(estr, elen);
                }
                else
                {
                    return false;
                }
                return true;
            }

            bool put(const char* estr)
            {
                return put(estr, strlen(estr));
            }

            bool putChar(int b)
            {
                char ch;
                if(m_pmode == PMODE_STRING)
                {
                    ch = b;
                    put(&ch, 1);
                }
                else if(m_pmode == PMODE_FILE)
                {
                    fputc(b, m_filehandle);
                    if(m_shouldflush)
                    {
                        fflush(m_filehandle);
                    }
                }
                return true;
            }

            bool putEscapedChar(int ch)
            {
                switch(ch)
                {
                    case '\'':
                        {
                            put("\\\'");
                        }
                        break;
                    case '\"':
                        {
                            put("\\\"");
                        }
                        break;
                    case '\\':
                        {
                            put("\\\\");
                        }
                        break;
                    case '\b':
                        {
                            put("\\b");
                        }
                        break;
                    case '\f':
                        {
                            put("\\f");
                        }
                        break;
                    case '\n':
                        {
                            put("\\n");
                        }
                        break;
                    case '\r':
                        {
                            put("\\r");
                        }
                        break;
                    case '\t':
                        {
                            put("\\t");
                        }
                        break;
                    case 0:
                        {
                            put("\\0");
                        }
                        break;
                    default:
                        {
                            putformat("\\x%02x", (unsigned char)ch);
                        }
                        break;
                }
                return true;
            }

            bool putQuotedString(const char* str, size_t len, bool withquot)
            {
                int bch;
                size_t i;
                bch = 0;
                if(withquot)
                {
                    putChar('"');
                }
                for(i = 0; i < len; i++)
                {
                    bch = str[i];
                    if((bch < 32) || (bch > 127) || (bch == '\"') || (bch == '\\'))
                    {
                        putEscapedChar(bch);
                    }
                    else
                    {
                        putChar(bch);
                    }
                }
                if(withquot)
                {
                    putChar('"');
                }
                return true;
            }

            template<typename... ArgsT>
            bool putFormatToString(const char* fmt, ArgsT&&... args)
            {
                m_ptostring->appendFormat(fmt, args...);
                return true;
            }

            template<typename... ArgsT>
            bool putformat(const char* fmt, ArgsT&&... args)
            {
                static auto fn_fprintf = fprintf;
                if(m_pmode == PMODE_STRING)
                {
                    return putFormatToString(fmt, args...);
                }
                else if(m_pmode == PMODE_FILE)
                {
                    fn_fprintf(m_filehandle, fmt, args...);
                    if(m_shouldflush)
                    {
                        fflush(m_filehandle);
                    }
                }
                return true;
            }

            void printObject(Value value, bool fixstring, bool invmethod)
            {
                return doPrintObject(value, fixstring, invmethod);
            }

            void printValue(Value value, bool fixstring, bool invmethod)
            {
                doPrintValue(value, fixstring, invmethod);
            }
    };

    #include "temp.h"

    struct DebugPrinter
    {
        private:
            State* m_pvm;
            Printer* m_printer;
            const char* m_insname;
            Blob* m_currblob;

        private:
            void doPrintInstrName()
            {
                Color nc;
                m_printer->putformat("%s%-16s%s ", nc.color('r'), m_insname, nc.color('0'));
            }

            size_t doPrintSimpleInstr(size_t offset)
            {
                doPrintInstrName();
                m_printer->putformat("\n");
                return offset + 1;
            }

            size_t doPrintConstInstr(size_t offset)
            {
                uint16_t constant;
                constant = (m_currblob->m_instrucs[offset + 1].code << 8) | m_currblob->m_instrucs[offset + 2].code;
                doPrintInstrName();
                m_printer->putformat("%8d ", constant);
                m_printer->printValue(m_currblob->m_constants->m_values[constant], true, false);
                m_printer->putformat("\n");
                return offset + 3;
            }

            size_t doPrintPropertyInstr(size_t offset)
            {
                const char* proptn;
                uint16_t constant;
                constant = (m_currblob->m_instrucs[offset + 1].code << 8) | m_currblob->m_instrucs[offset + 2].code;
                doPrintInstrName();
                m_printer->putformat("%8d ", constant);
                m_printer->printValue(m_currblob->m_constants->m_values[constant], true, false);
                proptn = "";
                if(m_currblob->m_instrucs[offset + 3].code == 1)
                {
                    proptn = "static";
                }
                m_printer->putformat(" (%s)", proptn);
                m_printer->putformat("\n");
                return offset + 4;
            }

            size_t doPrintShortInstr(size_t offset)
            {
                uint16_t slot;
                slot = (m_currblob->m_instrucs[offset + 1].code << 8) | m_currblob->m_instrucs[offset + 2].code;
                doPrintInstrName();
                m_printer->putformat("%8d\n", slot);
                return offset + 3;
            }

            size_t doPrintByteInstr(size_t offset)
            {
                uint8_t slot;
                slot = m_currblob->m_instrucs[offset + 1].code;
                doPrintInstrName();
                m_printer->putformat("%8d\n", slot);
                return offset + 2;
            }

            size_t doPrintJumpInstr(size_t sign, size_t offset)
            {
                uint16_t jump;
                jump = (uint16_t)(m_currblob->m_instrucs[offset + 1].code << 8);
                jump |= m_currblob->m_instrucs[offset + 2].code;
                doPrintInstrName();
                m_printer->putformat("%8d -> %d\n", offset, offset + 3 + sign * jump);
                return offset + 3;
            }

            size_t doPrintTryInstr(size_t offset)
            {
                uint16_t finally;
                uint16_t type;
                uint16_t address;
                type = (uint16_t)(m_currblob->m_instrucs[offset + 1].code << 8);
                type |= m_currblob->m_instrucs[offset + 2].code;
                address = (uint16_t)(m_currblob->m_instrucs[offset + 3].code << 8);
                address |= m_currblob->m_instrucs[offset + 4].code;
                finally = (uint16_t)(m_currblob->m_instrucs[offset + 5].code << 8);
                finally |= m_currblob->m_instrucs[offset + 6].code;
                doPrintInstrName();
                m_printer->putformat("%8d -> %d, %d\n", type, address, finally);
                return offset + 7;
            }

            size_t doPrintInvokeInstr(size_t offset)
            {
                uint16_t constant;
                uint8_t argcount;
                constant = (uint16_t)(m_currblob->m_instrucs[offset + 1].code << 8);
                constant |= m_currblob->m_instrucs[offset + 2].code;
                argcount = m_currblob->m_instrucs[offset + 3].code;
                doPrintInstrName();
                m_printer->putformat("(%d args) %8d ", argcount, constant);
                m_printer->printValue(m_currblob->m_constants->m_values[constant], true, false);
                m_printer->putformat("\n");
                return offset + 4;
            }

            size_t doPrintClosureInstr(size_t offset)
            {
                size_t j;
                size_t islocal;
                uint16_t index;
                uint16_t constant;
                const char* locn;
                FuncScript* function;
                offset++;
                constant = m_currblob->m_instrucs[offset++].code << 8;
                constant |= m_currblob->m_instrucs[offset++].code;
                m_printer->putformat("%-16s %8d ", m_insname, constant);
                m_printer->printValue(m_currblob->m_constants->m_values[constant], true, false);
                m_printer->putformat("\n");
                function = m_currblob->m_constants->m_values[constant].asFuncScript();
                for(j = 0; j < (size_t)function->m_upvalcount; j++)
                {
                    islocal = m_currblob->m_instrucs[offset++].code;
                    index = m_currblob->m_instrucs[offset++].code << 8;
                    index |= m_currblob->m_instrucs[offset++].code;
                    locn = "upvalue";
                    if(islocal)
                    {
                        locn = "local";
                    }
                    m_printer->putformat("%04d      |                     %s %d\n", offset - 3, locn, (int)index);
                }
                return offset;
            }

        public:
            DebugPrinter(State* state, Printer* pd)
            {
                m_pvm = state;
                m_printer = pd;
            }

            void printFunctionDisassembly(Blob* blob, const char* name)
            {
                size_t offset;
                m_printer->putformat("== compiled '%s' [[\n", name);
                for(offset = 0; offset < blob->m_count;)
                {
                    offset = printInstructionAt(blob, offset, true);
                }
                m_printer->putformat("]]\n");
            }

            size_t printInstructionAt(Blob* blob, size_t offset, bool ascompiled)
            {
                bool issame;
                size_t srcline;
                uint8_t instruction;
                m_currblob = blob;
                srcline = m_currblob->m_instrucs[offset].srcline;
                issame = (
                    (int(offset) > 0) &&
                    (int(srcline) == int(m_currblob->m_instrucs[offset - 1].srcline))
                );
                if(ascompiled)
                {
                    m_printer->putformat(" >> %d ", offset, srcline);
                }
                else
                {
                    m_printer->putformat("@%08d ", offset);
                }
                if(issame)
                {
                    if(ascompiled)
                    {
                        m_printer->putformat(" | ");
                    }
                    else
                    {
                        m_printer->putformat("       | ");
                    }
                }
                else
                {
                    m_printer->putformat("(line %d) ", m_currblob->m_instrucs[offset].srcline);
                }
                instruction = m_currblob->m_instrucs[offset].code;
                m_insname = Instruction::opName(instruction);
                switch(instruction)
                {
                    case Instruction::OP_JUMPIFFALSE:
                        return doPrintJumpInstr(1, offset);
                    case Instruction::OP_JUMPNOW:
                        return doPrintJumpInstr(1, offset);
                    case Instruction::OP_EXTRY:
                        return doPrintTryInstr(offset);
                    case Instruction::OP_LOOP:
                        return doPrintJumpInstr(-1, offset);
                    case Instruction::OP_GLOBALDEFINE:
                        return doPrintConstInstr(offset);
                    case Instruction::OP_GLOBALGET:
                        return doPrintConstInstr(offset);
                    case Instruction::OP_GLOBALSET:
                        return doPrintConstInstr(offset);
                    case Instruction::OP_LOCALGET:
                        return doPrintShortInstr(offset);
                    case Instruction::OP_LOCALSET:
                        return doPrintShortInstr(offset);
                    case Instruction::OP_FUNCARGGET:
                        return doPrintShortInstr(offset);
                    case Instruction::OP_FUNCARGSET:
                        return doPrintShortInstr(offset);
                    case Instruction::OP_PROPERTYGET:
                        return doPrintConstInstr(offset);
                    case Instruction::OP_PROPERTYGETSELF:
                        return doPrintConstInstr(offset);
                    case Instruction::OP_PROPERTYSET:
                        return doPrintConstInstr(offset);
                    case Instruction::OP_UPVALUEGET:
                        return doPrintShortInstr(offset);
                    case Instruction::OP_UPVALUESET:
                        return doPrintShortInstr(offset);
                    case Instruction::OP_EXPOPTRY:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_EXPUBLISHTRY:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PUSHCONSTANT:
                        return doPrintConstInstr(offset);
                    case Instruction::OP_EQUAL:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMGREATER:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMLESSTHAN:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PUSHEMPTY:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PUSHNULL:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PUSHTRUE:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PUSHFALSE:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMADD:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMSUBTRACT:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMMULTIPLY:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMDIVIDE:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMFLOORDIVIDE:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMMODULO:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMPOW:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMNEGATE:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMNOT:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMBITNOT:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMAND:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMOR:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMBITXOR:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMSHIFTLEFT:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PRIMSHIFTRIGHT:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_PUSHONE:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_IMPORTIMPORT:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_TYPEOF:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_ECHO:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_STRINGIFY:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_EXTHROW:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_POPONE:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_UPVALUECLOSE:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_DUPONE:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_ASSERT:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_POPN:
                        return doPrintShortInstr(offset);
                        /* non-user objects... */
                    case Instruction::OP_SWITCH:
                        return doPrintShortInstr(offset);
                        /* data container manipulators */
                    case Instruction::OP_MAKERANGE:
                        return doPrintShortInstr(offset);
                    case Instruction::OP_MAKEARRAY:
                        return doPrintShortInstr(offset);
                    case Instruction::OP_MAKEDICT:
                        return doPrintShortInstr(offset);
                    case Instruction::OP_INDEXGET:
                        return doPrintByteInstr(offset);
                    case Instruction::OP_INDEXGETRANGED:
                        return doPrintByteInstr(offset);
                    case Instruction::OP_INDEXSET:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_MAKECLOSURE:
                        return doPrintClosureInstr(offset);
                    case Instruction::OP_CALLFUNCTION:
                        return doPrintByteInstr(offset);
                    case Instruction::OP_CALLMETHOD:
                        return doPrintInvokeInstr(offset);
                    case Instruction::OP_CLASSINVOKETHIS:
                        return doPrintInvokeInstr(offset);
                    case Instruction::OP_RETURN:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_MAKECLASS:
                        return doPrintConstInstr(offset);
                    case Instruction::OP_MAKEMETHOD:
                        return doPrintConstInstr(offset);
                    case Instruction::OP_CLASSPROPERTYDEFINE:
                        return doPrintPropertyInstr(offset);
                    case Instruction::OP_CLASSGETSUPER:
                        return doPrintConstInstr(offset);
                    case Instruction::OP_CLASSINHERIT:
                        return doPrintSimpleInstr(offset);
                    case Instruction::OP_CLASSINVOKESUPER:
                        return doPrintInvokeInstr(offset);
                    case Instruction::OP_CLASSINVOKESUPERSELF:
                        return doPrintByteInstr(offset);
                    default:
                        {
                            m_printer->putformat("unknown opcode %d\n", instruction);
                        }
                        break;
                }
                return offset + 1;
            }
    };

    static const char* g_strthis = "this";
    static const char* g_strsuper = "super";

    struct Token
    {
        public:
            enum Type
            {
                TOK_NEWLINE,
                TOK_PARENOPEN,
                TOK_PARENCLOSE,
                TOK_BRACKETOPEN,
                TOK_BRACKETCLOSE,
                TOK_BRACEOPEN,
                TOK_BRACECLOSE,
                TOK_SEMICOLON,
                TOK_COMMA,
                TOK_BACKSLASH,
                TOK_EXCLMARK,
                TOK_NOTEQUAL,
                TOK_COLON,
                TOK_AT,
                TOK_DOT,
                TOK_DOUBLEDOT,
                TOK_TRIPLEDOT,
                TOK_PLUS,
                TOK_PLUSASSIGN,
                TOK_INCREMENT,
                TOK_MINUS,
                TOK_MINUSASSIGN,
                TOK_DECREMENT,
                TOK_MULTIPLY,
                TOK_MULTASSIGN,
                TOK_POWEROF,
                TOK_POWASSIGN,
                TOK_DIVIDE,
                TOK_DIVASSIGN,
                TOK_FLOOR,
                TOK_ASSIGN,
                TOK_EQUAL,
                TOK_LESSTHAN,
                TOK_LESSEQUAL,
                TOK_LEFTSHIFT,
                TOK_LEFTSHIFTASSIGN,
                TOK_GREATERTHAN,
                TOK_GREATERASSIGN,
                TOK_RIGHTSHIFT,
                TOK_RIGHTSHIFTASSIGN,
                TOK_MODULO,
                TOK_MODASSIGN,
                TOK_AMPERSAND,
                TOK_AMPASSIGN,
                TOK_BARSINGLE,
                TOK_BARASSIGN,
                TOK_TILDESINGLE,
                TOK_TILDEASSIGN,
                TOK_XORSINGLE,
                TOK_XORASSIGN,
                TOK_QUESTION,
                TOK_KWAND,
                TOK_KWAS,
                TOK_KWASSERT,
                TOK_KWBREAK,
                TOK_KWCATCH,
                TOK_KWCLASS,
                TOK_KWCONTINUE,
                TOK_KWFUNCTION,
                TOK_KWDEFAULT,
                TOK_KWTHROW,
                TOK_KWDO,
                TOK_KWECHO,
                TOK_KWELSE,
                TOK_KWFALSE,
                TOK_KWFINALLY,
                TOK_KWFOREACH,
                TOK_KWIF,
                TOK_KWIMPORT,
                TOK_KWIN,
                TOK_KWFOR,
                TOK_KWNULL,
                TOK_KWNEW,
                TOK_KWOR,
                TOK_KWSUPER,
                TOK_KWRETURN,
                TOK_KWTHIS,
                TOK_KWSTATIC,
                TOK_KWTRUE,
                TOK_KWTRY,
                TOK_KWTYPEOF,
                TOK_KWSWITCH,
                TOK_KWVAR,
                TOK_KWCASE,
                TOK_KWWHILE,
                TOK_LITERAL,
                TOK_LITNUMREG,
                TOK_LITNUMBIN,
                TOK_LITNUMOCT,
                TOK_LITNUMHEX,
                TOK_IDENTNORMAL,
                TOK_DECORATOR,
                TOK_INTERPOLATION,
                TOK_EOF,
                TOK_ERROR,
                TOK_KWEMPTY,
                TOK_UNDEFINED,
                TOK_TOKCOUNT,
            };

        public:
            static const char* tokenName(int t)
            {
                switch(t)
                {
                    case TOK_NEWLINE: return "TOK_NEWLINE";
                    case TOK_PARENOPEN: return "TOK_PARENOPEN";
                    case TOK_PARENCLOSE: return "TOK_PARENCLOSE";
                    case TOK_BRACKETOPEN: return "TOK_BRACKETOPEN";
                    case TOK_BRACKETCLOSE: return "TOK_BRACKETCLOSE";
                    case TOK_BRACEOPEN: return "TOK_BRACEOPEN";
                    case TOK_BRACECLOSE: return "TOK_BRACECLOSE";
                    case TOK_SEMICOLON: return "TOK_SEMICOLON";
                    case TOK_COMMA: return "TOK_COMMA";
                    case TOK_BACKSLASH: return "TOK_BACKSLASH";
                    case TOK_EXCLMARK: return "TOK_EXCLMARK";
                    case TOK_NOTEQUAL: return "TOK_NOTEQUAL";
                    case TOK_COLON: return "TOK_COLON";
                    case TOK_AT: return "TOK_AT";
                    case TOK_DOT: return "TOK_DOT";
                    case TOK_DOUBLEDOT: return "TOK_DOUBLEDOT";
                    case TOK_TRIPLEDOT: return "TOK_TRIPLEDOT";
                    case TOK_PLUS: return "TOK_PLUS";
                    case TOK_PLUSASSIGN: return "TOK_PLUSASSIGN";
                    case TOK_INCREMENT: return "TOK_INCREMENT";
                    case TOK_MINUS: return "TOK_MINUS";
                    case TOK_MINUSASSIGN: return "TOK_MINUSASSIGN";
                    case TOK_DECREMENT: return "TOK_DECREMENT";
                    case TOK_MULTIPLY: return "TOK_MULTIPLY";
                    case TOK_MULTASSIGN: return "TOK_MULTASSIGN";
                    case TOK_POWEROF: return "TOK_POWEROF";
                    case TOK_POWASSIGN: return "TOK_POWASSIGN";
                    case TOK_DIVIDE: return "TOK_DIVIDE";
                    case TOK_DIVASSIGN: return "TOK_DIVASSIGN";
                    case TOK_FLOOR: return "TOK_FLOOR";
                    case TOK_ASSIGN: return "TOK_ASSIGN";
                    case TOK_EQUAL: return "TOK_EQUAL";
                    case TOK_LESSTHAN: return "TOK_LESSTHAN";
                    case TOK_LESSEQUAL: return "TOK_LESSEQUAL";
                    case TOK_LEFTSHIFT: return "TOK_LEFTSHIFT";
                    case TOK_LEFTSHIFTASSIGN: return "TOK_LEFTSHIFTASSIGN";
                    case TOK_GREATERTHAN: return "TOK_GREATERTHAN";
                    case TOK_GREATERASSIGN: return "TOK_GREATERASSIGN";
                    case TOK_RIGHTSHIFT: return "TOK_RIGHTSHIFT";
                    case TOK_RIGHTSHIFTASSIGN: return "TOK_RIGHTSHIFTASSIGN";
                    case TOK_MODULO: return "TOK_MODULO";
                    case TOK_MODASSIGN: return "TOK_MODASSIGN";
                    case TOK_AMPERSAND: return "TOK_AMPERSAND";
                    case TOK_AMPASSIGN: return "TOK_AMPASSIGN";
                    case TOK_BARSINGLE: return "TOK_BARSINGLE";
                    case TOK_BARASSIGN: return "TOK_BARASSIGN";
                    case TOK_TILDESINGLE: return "TOK_TILDESINGLE";
                    case TOK_TILDEASSIGN: return "TOK_TILDEASSIGN";
                    case TOK_XORSINGLE: return "TOK_XORSINGLE";
                    case TOK_XORASSIGN: return "TOK_XORASSIGN";
                    case TOK_QUESTION: return "TOK_QUESTION";
                    case TOK_KWAND: return "TOK_KWAND";
                    case TOK_KWAS: return "TOK_KWAS";
                    case TOK_KWASSERT: return "TOK_KWASSERT";
                    case TOK_KWBREAK: return "TOK_KWBREAK";
                    case TOK_KWCATCH: return "TOK_KWCATCH";
                    case TOK_KWCLASS: return "TOK_KWCLASS";
                    case TOK_KWCONTINUE: return "TOK_KWCONTINUE";
                    case TOK_KWFUNCTION: return "TOK_KWFUNCTION";
                    case TOK_KWDEFAULT: return "TOK_KWDEFAULT";
                    case TOK_KWTHROW: return "TOK_KWTHROW";
                    case TOK_KWDO: return "TOK_KWDO";
                    case TOK_KWECHO: return "TOK_KWECHO";
                    case TOK_KWELSE: return "TOK_KWELSE";
                    case TOK_KWFALSE: return "TOK_KWFALSE";
                    case TOK_KWFINALLY: return "TOK_KWFINALLY";
                    case TOK_KWFOREACH: return "TOK_KWFOREACH";
                    case TOK_KWIF: return "TOK_KWIF";
                    case TOK_KWIMPORT: return "TOK_KWIMPORT";
                    case TOK_KWIN: return "TOK_KWIN";
                    case TOK_KWFOR: return "TOK_KWFOR";
                    case TOK_KWNULL: return "TOK_KWNULL";
                    case TOK_KWNEW: return "TOK_KWNEW";
                    case TOK_KWOR: return "TOK_KWOR";
                    case TOK_KWSUPER: return "TOK_KWSUPER";
                    case TOK_KWRETURN: return "TOK_KWRETURN";
                    case TOK_KWTHIS: return "TOK_KWTHIS";
                    case TOK_KWSTATIC: return "TOK_KWSTATIC";
                    case TOK_KWTRUE: return "TOK_KWTRUE";
                    case TOK_KWTRY: return "TOK_KWTRY";
                    case TOK_KWSWITCH: return "TOK_KWSWITCH";
                    case TOK_KWVAR: return "TOK_KWVAR";
                    case TOK_KWCASE: return "TOK_KWCASE";
                    case TOK_KWWHILE: return "TOK_KWWHILE";
                    case TOK_LITERAL: return "TOK_LITERAL";
                    case TOK_LITNUMREG: return "TOK_LITNUMREG";
                    case TOK_LITNUMBIN: return "TOK_LITNUMBIN";
                    case TOK_LITNUMOCT: return "TOK_LITNUMOCT";
                    case TOK_LITNUMHEX: return "TOK_LITNUMHEX";
                    case TOK_IDENTNORMAL: return "TOK_IDENTNORMAL";
                    case TOK_DECORATOR: return "TOK_DECORATOR";
                    case TOK_INTERPOLATION: return "TOK_INTERPOLATION";
                    case TOK_EOF: return "TOK_EOF";
                    case TOK_ERROR: return "TOK_ERROR";
                    case TOK_KWEMPTY: return "TOK_KWEMPTY";
                    case TOK_UNDEFINED: return "TOK_UNDEFINED";
                    case TOK_TOKCOUNT: return "TOK_TOKCOUNT";
                }
                return "?invalid?";
            }


        public:
            bool isglobal;
            Type type;
            const char* start;
            int length;
            int line;
    };

    struct Lexer
    {
        public:
            static bool charIsDigit(char c)
            {
                return c >= '0' && c <= '9';
            }

            static bool charIsBinary(char c)
            {
                return c == '0' || c == '1';
            }

            static bool charIsAlpha(char c)
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
            }

            static bool charIsOctal(char c)
            {
                return c >= '0' && c <= '7';
            }

            static bool charIsHexadecimal(char c)
            {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            }

            /*
            // Reads the next character, which should be a hex digit (0-9, a-f, or A-F) and
            // returns its numeric value. If the character isn't a hex digit, returns -1.
            */
            static int readHexDigit(char c)
            {
                if((c >= '0') && (c <= '9'))
                {
                    return (c - '0');
                }
                if((c >= 'a') && (c <= 'f'))
                {
                    return ((c - 'a') + 10);
                }
                if((c >= 'A') && (c <= 'F'))
                {
                    return ((c - 'A') + 10);
                }
                return -1;
            }

        public:
            State* m_pvm;
            const char* m_plainsource;
            const char* m_sourceptr;
            int m_currentline;
            int m_tplstringcount;
            int m_tplstringbuffer[NEON_CFG_ASTMAXSTRTPLDEPTH];

        public:
            Lexer(State* state, const char* source): m_pvm(state)
            {
                NEON_ASTDEBUG(state, "");
                m_sourceptr = source;
                m_plainsource = source;
                m_currentline = 1;
                m_tplstringcount = -1;
            }

            ~Lexer()
            {
                NEON_ASTDEBUG(m_pvm, "");
            }

            bool isAtEnd()
            {
                return *m_sourceptr == '\0';
            }

            Token makeToken(Token::Type type)
            {
                Token t;
                t.isglobal = false;
                t.type = type;
                t.start = m_plainsource;
                t.length = (int)(m_sourceptr - m_plainsource);
                t.line = m_currentline;
                return t;
            }

            Token errorToken(const char* fmt, ...)
            {
                int length;
                char* buf;
                va_list va;
                Token t;
                va_start(va, fmt);
                buf = (char*)State::GC::allocate(m_pvm, sizeof(char), 1024);
                /* TODO: used to be vasprintf. need to check how much to actually allocate! */
                length = vsprintf(buf, fmt, va);
                va_end(va);
                t.type = Token::TOK_ERROR;
                t.start = buf;
                t.isglobal = false;
                if(buf != nullptr)
                {
                    t.length = length;
                }
                else
                {
                    t.length = 0;
                }
                t.line = m_currentline;
                return t;
            }

            char advance()
            {
                m_sourceptr++;
                if(m_sourceptr[-1] == '\n')
                {
                    m_currentline++;
                }
                return m_sourceptr[-1];
            }

            bool match(char expected)
            {
                if(isAtEnd())
                {
                    return false;
                }
                if(*m_sourceptr != expected)
                {
                    return false;
                }
                m_sourceptr++;
                if(m_sourceptr[-1] == '\n')
                {
                    m_currentline++;
                }
                return true;
            }

            char peekCurrent()
            {
                return *m_sourceptr;
            }

            char peekPrevious()
            {
                return m_sourceptr[-1];
            }

            char peekNext()
            {
                if(isAtEnd())
                {
                    return '\0';
                }
                return m_sourceptr[1];
            }

            Token skipBlockComments()
            {
                int nesting;
                nesting = 1;
                while(nesting > 0)
                {
                    if(isAtEnd())
                    {
                        return errorToken("unclosed block comment");
                    }
                    /* internal comment open */
                    if(peekCurrent() == '/' && peekNext() == '*')
                    {
                        advance();
                        advance();
                        nesting++;
                        continue;
                    }
                    /* comment close */
                    if(peekCurrent() == '*' && peekNext() == '/')
                    {
                        advance();
                        advance();
                        nesting--;
                        continue;
                    }
                    /* regular comment body */
                    advance();
                }
                #if defined(NEON_PLAT_ISWINDOWS)
                //advance();
                #endif
                return makeToken(Token::TOK_UNDEFINED);
            }

            Token skipSpace()
            {
                char c;
                Token result;
                result.isglobal = false;
                for(;;)
                {
                    c = peekCurrent();
                    switch(c)
                    {
                        case ' ':
                        case '\r':
                        case '\t':
                        {
                            advance();
                        }
                        break;
                        /*
                        case '\n':
                            {
                                m_currentline++;
                                advance();
                            }
                            break;
                        */
                        /*
                        case '#':
                            // single line comment
                            {
                                while(peekCurrent() != '\n' && !isAtEnd())
                                    advance();

                            }
                            break;
                        */
                        case '/':
                        {
                            if(peekNext() == '/')
                            {
                                while(peekCurrent() != '\n' && !isAtEnd())
                                {
                                    advance();
                                }
                                return makeToken(Token::TOK_UNDEFINED);
                            }
                            else if(peekNext() == '*')
                            {
                                advance();
                                advance();
                                result = skipBlockComments();
                                if(result.type != Token::TOK_UNDEFINED)
                                {
                                    return result;
                                }
                                break;
                            }
                            else
                            {
                                return makeToken(Token::TOK_UNDEFINED);
                            }
                        }
                        break;
                        /* exit as soon as we see a non-whitespace... */
                        default:
                            goto finished;
                            break;
                    }
                }
                finished:
                return makeToken(Token::TOK_UNDEFINED);
            }

            Token scanString(char quote, bool withtemplate)
            {
                Token tkn;
                NEON_ASTDEBUG(m_pvm, "quote=[%c] withtemplate=%d", quote, withtemplate);
                while(peekCurrent() != quote && !isAtEnd())
                {
                    if(withtemplate)
                    {
                        /* interpolation started */
                        if(peekCurrent() == '$' && peekNext() == '{' && peekPrevious() != '\\')
                        {
                            if(m_tplstringcount - 1 < NEON_CFG_ASTMAXSTRTPLDEPTH)
                            {
                                m_tplstringcount++;
                                m_tplstringbuffer[m_tplstringcount] = (int)quote;
                                m_sourceptr++;
                                tkn = makeToken(Token::TOK_INTERPOLATION);
                                m_sourceptr++;
                                return tkn;
                            }
                            return errorToken("maximum interpolation nesting of %d exceeded by %d", NEON_CFG_ASTMAXSTRTPLDEPTH,
                                NEON_CFG_ASTMAXSTRTPLDEPTH - m_tplstringcount + 1);
                        }
                    }
                    if(peekCurrent() == '\\' && (peekNext() == quote || peekNext() == '\\'))
                    {
                        advance();
                    }
                    advance();
                }
                if(isAtEnd())
                {
                    return errorToken("unterminated string (opening quote not matched)");
                }
                /* the closing quote */
                match(quote);
                return makeToken(Token::TOK_LITERAL);
            }

            Token scanNumber()
            {
                NEON_ASTDEBUG(m_pvm, "");
                /* handle binary, octal and hexadecimals */
                if(peekPrevious() == '0')
                {
                    /* binary number */
                    if(match('b'))
                    {
                        while(charIsBinary(peekCurrent()))
                        {
                            advance();
                        }
                        return makeToken(Token::TOK_LITNUMBIN);
                    }
                    else if(match('c'))
                    {
                        while(charIsOctal(peekCurrent()))
                        {
                            advance();
                        }
                        return makeToken(Token::TOK_LITNUMOCT);
                    }
                    else if(match('x'))
                    {
                        while(charIsHexadecimal(peekCurrent()))
                        {
                            advance();
                        }
                        return makeToken(Token::TOK_LITNUMHEX);
                    }
                }
                while(charIsDigit(peekCurrent()))
                {
                    advance();
                }
                /* dots(.) are only valid here when followed by a digit */
                if(peekCurrent() == '.' && charIsDigit(peekNext()))
                {
                    advance();
                    while(charIsDigit(peekCurrent()))
                    {
                        advance();
                    }
                    /*
                    // E or e are only valid here when followed by a digit and occurring after a dot
                    */
                    if((peekCurrent() == 'e' || peekCurrent() == 'E') && (peekNext() == '+' || peekNext() == '-'))
                    {
                        advance();
                        advance();
                        while(charIsDigit(peekCurrent()))
                        {
                            advance();
                        }
                    }
                }
                return makeToken(Token::TOK_LITNUMREG);
            }

            Token::Type getIdentType()
            {
                static const struct
                {
                    const char* str;
                    int tokid;
                }
                keywords[] =
                {
                    { "and", Token::TOK_KWAND },
                    { "assert", Token::TOK_KWASSERT },
                    { "as", Token::TOK_KWAS },
                    { "break", Token::TOK_KWBREAK },
                    { "catch", Token::TOK_KWCATCH },
                    { "class", Token::TOK_KWCLASS },
                    { "continue", Token::TOK_KWCONTINUE },
                    { "default", Token::TOK_KWDEFAULT },
                    { "def", Token::TOK_KWFUNCTION },
                    { "function", Token::TOK_KWFUNCTION },
                    { "throw", Token::TOK_KWTHROW },
                    { "do", Token::TOK_KWDO },
                    { "echo", Token::TOK_KWECHO },
                    { "else", Token::TOK_KWELSE },
                    { "empty", Token::TOK_KWEMPTY },
                    { "false", Token::TOK_KWFALSE },
                    { "finally", Token::TOK_KWFINALLY },
                    { "foreach", Token::TOK_KWFOREACH },
                    { "if", Token::TOK_KWIF },
                    { "import", Token::TOK_KWIMPORT },
                    { "in", Token::TOK_KWIN },
                    { "for", Token::TOK_KWFOR },
                    { "null", Token::TOK_KWNULL },
                    { "new", Token::TOK_KWNEW },
                    { "or", Token::TOK_KWOR },
                    { "super", Token::TOK_KWSUPER },
                    { "return", Token::TOK_KWRETURN },
                    { "this", Token::TOK_KWTHIS },
                    { "static", Token::TOK_KWSTATIC },
                    { "true", Token::TOK_KWTRUE },
                    { "try", Token::TOK_KWTRY },
                    { "typeof", Token::TOK_KWTYPEOF },
                    { "switch", Token::TOK_KWSWITCH },
                    { "case", Token::TOK_KWCASE },
                    { "var", Token::TOK_KWVAR },
                    { "while", Token::TOK_KWWHILE },
                    { nullptr, (Token::Type)0 }
                };
                size_t i;
                size_t kwlen;
                size_t ofs;
                const char* kwtext;
                for(i = 0; keywords[i].str != nullptr; i++)
                {
                    kwtext = keywords[i].str;
                    kwlen = strlen(kwtext);
                    ofs = (m_sourceptr - m_plainsource);
                    if(ofs == kwlen)
                    {
                        if(memcmp(m_plainsource, kwtext, kwlen) == 0)
                        {
                            return (Token::Type)keywords[i].tokid;
                        }
                    }
                }
                return Token::TOK_IDENTNORMAL;
            }

            Token scanIdent(bool isdollar)
            {
                int cur;
                Token tok;
                cur = peekCurrent();
                if(cur == '$')
                {
                    advance();
                }
                while(true)
                {
                    cur = peekCurrent();
                    if(charIsAlpha(cur) || charIsDigit(cur))
                    {
                        advance();
                    }
                    else
                    {
                        break;
                    }
                }
                tok = makeToken(getIdentType());
                tok.isglobal = isdollar;
                return tok;
            }

            Token scanDecorator()
            {
                while(charIsAlpha(peekCurrent()) || charIsDigit(peekCurrent()))
                {
                    advance();
                }
                return makeToken(Token::TOK_DECORATOR);
            }

            Token scanToken()
            {
                char c;
                bool isdollar;
                Token tk;
                Token token;
                tk = skipSpace();
                if(tk.type != Token::TOK_UNDEFINED)
                {
                    return tk;
                }
                m_plainsource = m_sourceptr;
                if(isAtEnd())
                {
                    return makeToken(Token::TOK_EOF);
                }
                c = advance();
                if(charIsDigit(c))
                {
                    return scanNumber();
                }
                else if(charIsAlpha(c) || (c == '$'))
                {
                    isdollar = (c == '$');
                    return scanIdent(isdollar);
                }
                switch(c)
                {
                    case '(':
                        {
                            return makeToken(Token::TOK_PARENOPEN);
                        }
                        break;
                    case ')':
                        {
                            return makeToken(Token::TOK_PARENCLOSE);
                        }
                        break;
                    case '[':
                        {
                            return makeToken(Token::TOK_BRACKETOPEN);
                        }
                        break;
                    case ']':
                        {
                            return makeToken(Token::TOK_BRACKETCLOSE);
                        }
                        break;
                    case '{':
                        {
                            return makeToken(Token::TOK_BRACEOPEN);
                        }
                        break;
                    case '}':
                        {
                            if(m_tplstringcount > -1)
                            {
                                token = scanString((char)m_tplstringbuffer[m_tplstringcount], true);
                                m_tplstringcount--;
                                return token;
                            }
                            return makeToken(Token::TOK_BRACECLOSE);
                        }
                        break;
                    case ';':
                        {
                            return makeToken(Token::TOK_SEMICOLON);
                        }
                        break;
                    case '\\':
                        {
                            return makeToken(Token::TOK_BACKSLASH);
                        }
                        break;
                    case ':':
                        {
                            return makeToken(Token::TOK_COLON);
                        }
                        break;
                    case ',':
                        {
                            return makeToken(Token::TOK_COMMA);
                        }
                        break;
                    case '@':
                        {
                            if(!charIsAlpha(peekCurrent()))
                            {
                                return makeToken(Token::TOK_AT);
                            }
                            return scanDecorator();
                        }
                        break;
                    case '!':
                        {
                            if(match('='))
                            {
                                return makeToken(Token::TOK_NOTEQUAL);
                            }
                            return makeToken(Token::TOK_EXCLMARK);

                        }
                        break;
                    case '.':
                        {
                            if(match('.'))
                            {
                                if(match('.'))
                                {
                                    return makeToken(Token::TOK_TRIPLEDOT);
                                }
                                return makeToken(Token::TOK_DOUBLEDOT);
                            }
                            return makeToken(Token::TOK_DOT);
                        }
                        break;
                    case '+':
                    {
                        if(match('+'))
                        {
                            return makeToken(Token::TOK_INCREMENT);
                        }
                        if(match('='))
                        {
                            return makeToken(Token::TOK_PLUSASSIGN);
                        }
                        else
                        {
                            return makeToken(Token::TOK_PLUS);
                        }
                    }
                    break;
                    case '-':
                        {
                            if(match('-'))
                            {
                                return makeToken(Token::TOK_DECREMENT);
                            }
                            if(match('='))
                            {
                                return makeToken(Token::TOK_MINUSASSIGN);
                            }
                            else
                            {
                                return makeToken(Token::TOK_MINUS);
                            }
                        }
                        break;
                    case '*':
                        {
                            if(match('*'))
                            {
                                if(match('='))
                                {
                                    return makeToken(Token::TOK_POWASSIGN);
                                }
                                return makeToken(Token::TOK_POWEROF);
                            }
                            else
                            {
                                if(match('='))
                                {
                                    return makeToken(Token::TOK_MULTASSIGN);
                                }
                                return makeToken(Token::TOK_MULTIPLY);
                            }
                        }
                        break;
                    case '/':
                        {
                            if(match('='))
                            {
                                return makeToken(Token::TOK_DIVASSIGN);
                            }
                            return makeToken(Token::TOK_DIVIDE);
                        }
                        break;
                    case '=':
                        {
                            if(match('='))
                            {
                                return makeToken(Token::TOK_EQUAL);
                            }
                            return makeToken(Token::TOK_ASSIGN);
                        }        
                        break;
                    case '<':
                        {
                            if(match('<'))
                            {
                                if(match('='))
                                {
                                    return makeToken(Token::TOK_LEFTSHIFTASSIGN);
                                }
                                return makeToken(Token::TOK_LEFTSHIFT);
                            }
                            else
                            {
                                if(match('='))
                                {
                                    return makeToken(Token::TOK_LESSEQUAL);
                                }
                                return makeToken(Token::TOK_LESSTHAN);

                            }
                        }
                        break;
                    case '>':
                        {
                            if(match('>'))
                            {
                                if(match('='))
                                {
                                    return makeToken(Token::TOK_RIGHTSHIFTASSIGN);
                                }
                                return makeToken(Token::TOK_RIGHTSHIFT);
                            }
                            else
                            {
                                if(match('='))
                                {
                                    return makeToken(Token::TOK_GREATERASSIGN);
                                }
                                return makeToken(Token::TOK_GREATERTHAN);
                            }
                        }
                        break;
                    case '%':
                        {
                            if(match('='))
                            {
                                return makeToken(Token::TOK_MODASSIGN);
                            }
                            return makeToken(Token::TOK_MODULO);
                        }
                        break;
                    case '&':
                        {
                            if(match('&'))
                            {
                                return makeToken(Token::TOK_KWAND);
                            }
                            else if(match('='))
                            {
                                return makeToken(Token::TOK_AMPASSIGN);
                            }
                            return makeToken(Token::TOK_AMPERSAND);
                        }
                        break;
                    case '|':
                        {
                            if(match('|'))
                            {
                                return makeToken(Token::TOK_KWOR);
                            }
                            else if(match('='))
                            {
                                return makeToken(Token::TOK_BARASSIGN);
                            }
                            return makeToken(Token::TOK_BARSINGLE);
                        }
                        break;
                    case '~':
                        {
                            if(match('='))
                            {
                                return makeToken(Token::TOK_TILDEASSIGN);
                            }
                            return makeToken(Token::TOK_TILDESINGLE);
                        }
                        break;
                    case '^':
                        {
                            if(match('='))
                            {
                                return makeToken(Token::TOK_XORASSIGN);
                            }
                            return makeToken(Token::TOK_XORSINGLE);
                        }
                        break;
                    case '\n':
                        {
                            return makeToken(Token::TOK_NEWLINE);
                        }
                        break;
                    case '"':
                        {
                            return scanString('"', true);
                        }
                        break;
                    case '\'':
                        {
                            return scanString('\'', false);
                        }
                        break;
                    case '?':
                        {
                            return makeToken(Token::TOK_QUESTION);
                        }
                        break;
                    /*
                    // --- DO NOT MOVE ABOVE OR BELOW THE DEFAULT CASE ---
                    // fall-through tokens goes here... this tokens are only valid
                    // when the carry another token with them...
                    // be careful not to add break after them so that they may use the default
                    // case.
                    */
                    default:
                        {
                        }
                        break;
                }
                return errorToken("unexpected character %c", c);
            }
    };

    struct Parser
    {
        public:
            enum CompContext
            {
                COMPCONTEXT_NONE,
                COMPCONTEXT_CLASS,
                COMPCONTEXT_ARRAY,
                COMPCONTEXT_NESTEDFUNCTION,
            };

            struct Rule
            {
                public:
                    enum Precedence
                    {
                        PREC_NONE,

                        /* =, &=, |=, *=, +=, -=, /=, **=, %=, >>=, <<=, ^=, //= */
                        PREC_ASSIGNMENT,
                        /* ~= ?: */
                        PREC_CONDITIONAL,
                        /* 'or' || */
                        PREC_OR,
                        /* 'and' && */
                        PREC_AND,
                        /* ==, != */
                        PREC_EQUALITY,
                        /* <, >, <=, >= */
                        PREC_COMPARISON,
                        /* | */
                        PREC_BITOR,
                        /* ^ */
                        PREC_BITXOR,
                        /* & */
                        PREC_BITAND,
                        /* <<, >> */
                        PREC_SHIFT,
                        /* .. */
                        PREC_RANGE,
                        /* +, - */
                        PREC_TERM,
                        /* *, /, %, **, // */
                        PREC_FACTOR,
                        /* !, -, ~, (++, -- this two will now be treated as statements) */
                        PREC_UNARY,
                        /* ., () */
                        PREC_CALL,
                        PREC_PRIMARY
                    };

                    using PrefixFN = bool (*)(Parser*, bool);
                    using InfixFN = bool (*)(Parser*, Token, bool);

                public:
                    static Rule* make(Rule* dest, PrefixFN prefix, InfixFN infix, Precedence precedence)
                    {
                        dest->fnprefix = prefix;
                        dest->fninfix = infix;
                        dest->precedence = precedence;
                        return dest;
                    }
                
                public:
                    PrefixFN fnprefix;
                    InfixFN fninfix;
                    Precedence precedence;
            };

            struct CompiledUpvalue
            {
                bool islocal;
                uint16_t index;
            };

            struct CompiledLocal
            {
                bool iscaptured;
                int depth;
                Token varname;
            };

            struct FuncCompiler
            {
                public:
                    enum
                    {
                        /* how many locals per function can be compiled */
                        MaxLocals = (64 / 1),
    
                        /* how many upvalues per function can be compiled */
                        MaxUpvalues = (64 / 1),
                    };

                public:
                    Parser* m_prs;
                    int m_localcount;
                    int m_scopedepth;
                    int m_compiledexcepthandlercount;
                    bool m_fromimport;
                    FuncCompiler* m_enclosing;
                    /* current function */
                    FuncScript* m_targetfunc;
                    FuncCommon::Type m_type;
                    //CompiledLocal m_compiledlocals[MaxLocals];
                    CompiledUpvalue m_compiledupvals[MaxUpvalues];
                    GenericArray<CompiledLocal> m_compiledlocals;

                public:
                    FuncCompiler(Parser* prs, FuncCommon::Type t, bool isanon)
                    {
                        size_t i;
                        bool candeclthis;
                        CompiledLocal* local;
                        String* fname;
                        (void)i;
                        m_prs = prs;
                        m_enclosing = prs->m_pvm->m_activeparser->m_currfunccompiler;
                        m_targetfunc = nullptr;
                        m_type = t;
                        m_localcount = 0;
                        m_scopedepth = 0;
                        m_compiledexcepthandlercount = 0;
                        m_fromimport = false;
                        m_targetfunc = FuncScript::make(prs->m_pvm, prs->m_currmodule, t);
                        prs->m_currfunccompiler = this;
                        m_compiledlocals.m_pvm = prs->m_pvm;
                        m_compiledlocals.push(CompiledLocal{});
                        if(t != FuncCommon::FUNCTYPE_SCRIPT)
                        {
                            prs->m_pvm->stackPush(Value::fromObject(m_targetfunc));
                            if(isanon)
                            {
                                Printer ptmp(prs->m_pvm);
                                ptmp.putformat("anonymous@[%s:%d]", prs->m_currentphysfile, prs->m_prevtoken.line);
                                fname = ptmp.takeString();
                            }
                            else
                            {
                                fname = String::copy(prs->m_pvm, prs->m_prevtoken.start, prs->m_prevtoken.length);
                            }
                            prs->m_currfunccompiler->m_targetfunc->m_scriptfnname = fname;
                            prs->m_pvm->stackPop();
                        }
                        /* claiming slot zero for use in class methods */
                        local = &prs->m_currfunccompiler->m_compiledlocals[0];
                        prs->m_currfunccompiler->m_localcount++;
                        local->depth = 0;
                        local->iscaptured = false;
                        candeclthis = (
                            (t != FuncCommon::FUNCTYPE_FUNCTION) &&
                            (prs->m_compcontext == COMPCONTEXT_CLASS)
                        );
                        if(candeclthis || (/*(t == FuncCommon::FUNCTYPE_ANONYMOUS) &&*/ (prs->m_compcontext != COMPCONTEXT_CLASS)))
                        {
                            local->varname.start = g_strthis;
                            local->varname.length = 4;
                        }
                        else
                        {
                            local->varname.start = "";
                            local->varname.length = 0;
                        }
                    }

                    ~FuncCompiler()
                    {
                    }

                    void compileBody(bool closescope, bool isanon)
                    {
                        int i;
                        FuncScript* function;
                        (void)isanon;
                        /* compile the body */
                        m_prs->ignoreSpace();
                        m_prs->consume(Token::TOK_BRACEOPEN, "expected '{' before function body");
                        nn_astparser_parseblock(m_prs);
                        /* create the function object */
                        if(closescope)
                        {
                            nn_astparser_scopeend(m_prs);
                        }
                        function = m_prs->endCompiler();
                        m_prs->m_pvm->stackPush(Value::fromObject(function));
                        m_prs->emitInstruction(Instruction::OP_MAKECLOSURE);
                        m_prs->emit1short(m_prs->pushConst(Value::fromObject(function)));
                        for(i = 0; i < function->m_upvalcount; i++)
                        {
                            m_prs->emit1byte(m_compiledupvals[i].islocal ? 1 : 0);
                            m_prs->emit1short(m_compiledupvals[i].index);
                        }
                        m_prs->m_pvm->stackPop();
                    }

                    int resolveLocal(Token* name)
                    {
                        int i;
                        CompiledLocal* local;
                        for(i = m_localcount - 1; i >= 0; i--)
                        {
                            local = &m_compiledlocals[i];
                            if(Parser::identsEqual(&local->varname, name))
                            {
                                if(local->depth == -1)
                                {
                                    m_prs->raiseError("cannot read local variable in it's own initializer");
                                }
                                return i;
                            }
                        }
                        return -1;
                    }

                    int addUpvalue(uint16_t index, bool islocal)
                    {
                        int i;
                        int upcnt;
                        CompiledUpvalue* upvalue;
                        upcnt = m_targetfunc->m_upvalcount;
                        for(i = 0; i < upcnt; i++)
                        {
                            upvalue = &m_compiledupvals[i];
                            if(upvalue->index == index && upvalue->islocal == islocal)
                            {
                                return i;
                            }
                        }
                        if(upcnt == MaxUpvalues)
                        {
                            m_prs->raiseError("too many closure variables in function");
                            return 0;
                        }
                        m_compiledupvals[upcnt].islocal = islocal;
                        m_compiledupvals[upcnt].index = index;
                        return m_targetfunc->m_upvalcount++;
                    }

                    int resolveUpvalue(Token* name)
                    {
                        int local;
                        int upvalue;
                        if(m_enclosing == nullptr)
                        {
                            return -1;
                        }
                        local = m_enclosing->resolveLocal(name);
                        if(local != -1)
                        {
                            m_enclosing->m_compiledlocals[local].iscaptured = true;
                            return addUpvalue((uint16_t)local, true);
                        }
                        upvalue = m_enclosing->resolveUpvalue(name);
                        if(upvalue != -1)
                        {
                            return addUpvalue((uint16_t)upvalue, false);
                        }
                        return -1;
                    }
            };

            struct ClassCompiler
            {
                bool hassuperclass;
                ClassCompiler* m_enclosing;
                Token classname;
            };

        public:
            static bool identsEqual(Token* a, Token* b)
            {
                return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
            }

            /*
            * $keeplast: whether to emit code that retains or discards the value of the last statement/expression.
            * SHOULD NOT BE USED FOR ORDINARY SCRIPTS as it will almost definitely result in the stack containing invalid values.
            */
            static FuncScript* compileSource(State* state, Module* module, const char* source, Blob* blob, bool fromimport, bool keeplast)
            {
                Lexer* lexer;
                Parser* parser;
                FuncScript* function;
                (void)blob;
                NEON_ASTDEBUG(state, "module=%p source=[...] blob=[...] fromimport=%d keeplast=%d", module, fromimport, keeplast);
                lexer = Memory::create<Lexer>(state, source);
                parser = Memory::create<Parser>(state, lexer, module, keeplast);
                FuncCompiler compiler(parser, FuncCommon::FUNCTYPE_SCRIPT, true);
                compiler.m_fromimport = fromimport;
                parser->runParser();
                function = parser->endCompiler();
                if(parser->m_haderror)
                {
                    function = nullptr;
                }
                Memory::destroy(lexer);
                Memory::destroy(parser);
                state->m_activeparser = nullptr;
                return function;
            }

        public:
            State* m_pvm;
            bool m_haderror;
            bool m_panicmode;
            bool m_isreturning;
            bool m_istrying;
            bool m_replcanecho;
            bool m_keeplastvalue;
            bool m_lastwasstatement;
            bool m_infunction;
            /* used for tracking loops for the continue statement... */
            int m_innermostloopstart;
            int m_innermostloopscopedepth;
            int m_blockcount;
            /* the context in which the parser resides; none (outer level), inside a class, dict, array, etc */
            CompContext m_compcontext;
            const char* m_currentphysfile;
            Lexer* m_lexer;
            Token m_currtoken;
            Token m_prevtoken;
            ClassCompiler* m_currclasscompiler;
            FuncCompiler* m_currfunccompiler;
            Module* m_currmodule;

        public:
            Parser(State* state, Lexer* lexer, Module* module, bool keeplast): m_pvm(state)
            {
                NEON_ASTDEBUG(state, "");
                state->m_activeparser = this;
                m_currfunccompiler = nullptr;
                m_lexer = lexer;
                m_haderror = false;
                m_panicmode = false;
                m_blockcount = 0;
                m_replcanecho = false;
                m_isreturning = false;
                m_istrying = false;
                m_compcontext = COMPCONTEXT_NONE;
                m_innermostloopstart = -1;
                m_innermostloopscopedepth = 0;
                m_currclasscompiler = nullptr;
                m_currmodule = module;
                m_keeplastvalue = keeplast;
                m_lastwasstatement = false;
                m_infunction = false;
                m_currentphysfile = m_currmodule->m_physlocation->data();
            }

            ~Parser()
            {
            }

            template<typename... ArgsT>
            bool raiseErrorAt(Token* t, const char* message, ArgsT&&... args)
            {
                static auto fn_fprintf = fprintf;
                fflush(stdout);
                /*
                // do not cascade error
                // suppress error if already in panic mode
                */
                if(m_panicmode)
                {
                    return false;
                }
                m_panicmode = true;
                fprintf(stderr, "SyntaxError");
                if(t->type == Token::TOK_EOF)
                {
                    fprintf(stderr, " at end");
                }
                else if(t->type == Token::TOK_ERROR)
                {
                    /* do nothing */
                }
                else
                {
                    if(t->length == 1 && *t->start == '\n')
                    {
                        fprintf(stderr, " at newline");
                    }
                    else
                    {
                        fprintf(stderr, " at '%.*s'", t->length, t->start);
                    }
                }
                fprintf(stderr, ": ");
                fn_fprintf(stderr, message, args...);
                fputs("\n", stderr);
                fprintf(stderr, "  %s:%d\n", m_currmodule->m_physlocation->data(), t->line);
                m_haderror = true;
                return false;
            }

            template<typename... ArgsT>
            bool raiseError(const char* message, ArgsT&&... args)
            {
                raiseErrorAt(&m_prevtoken, message, args...);
                return false;
            }

            template<typename... ArgsT>
            bool raiseErrorAtCurrent(const char* message, ArgsT&&... args)
            {
                raiseErrorAt(&m_currtoken, message, args...);
                return false;
            }

            void synchronize()
            {
                m_panicmode = false;
                while(m_currtoken.type != Token::TOK_EOF)
                {
                    if(m_currtoken.type == Token::TOK_NEWLINE || m_currtoken.type == Token::TOK_SEMICOLON)
                    {
                        return;
                    }
                    switch(m_currtoken.type)
                    {
                        case Token::TOK_KWCLASS:
                        case Token::TOK_KWFUNCTION:
                        case Token::TOK_KWVAR:
                        case Token::TOK_KWFOREACH:
                        case Token::TOK_KWIF:
                        case Token::TOK_KWSWITCH:
                        case Token::TOK_KWCASE:
                        case Token::TOK_KWFOR:
                        case Token::TOK_KWDO:
                        case Token::TOK_KWWHILE:
                        case Token::TOK_KWECHO:
                        case Token::TOK_KWASSERT:
                        case Token::TOK_KWTRY:
                        case Token::TOK_KWCATCH:
                        case Token::TOK_KWTHROW:
                        case Token::TOK_KWRETURN:
                        case Token::TOK_KWSTATIC:
                        case Token::TOK_KWTHIS:
                        case Token::TOK_KWSUPER:
                        case Token::TOK_KWFINALLY:
                        case Token::TOK_KWIN:
                        case Token::TOK_KWIMPORT:
                        case Token::TOK_KWAS:
                            return;
                        default:
                            {
                                /* do nothing */
                            }
                    }
                    advance();
                }
            }


            Blob* currentBlob()
            {
                return m_currfunccompiler->m_targetfunc->m_compiledblob;
            }

            void advance()
            {
                m_prevtoken = m_currtoken;
                while(true)
                {
                    m_currtoken = m_lexer->scanToken();
                    if(m_currtoken.type != Token::TOK_ERROR)
                    {
                        break;
                    }
                    raiseErrorAtCurrent(m_currtoken.start);
                }
            }

            bool consume(Token::Type t, const char* message)
            {
                if(m_currtoken.type == t)
                {
                    advance();
                    return true;
                }
                return raiseErrorAtCurrent(message);
            }

            void consumeOr(const char* message, const Token::Type* ts, int count)
            {
                int i;
                for(i = 0; i < count; i++)
                {
                    if(m_currtoken.type == ts[i])
                    {
                        advance();
                        return;
                    }
                }
                raiseErrorAtCurrent(message);
            }

            void emit(uint8_t byte, int line, bool isop)
            {
                Instruction ins;
                ins.code = byte;
                ins.srcline = line;
                ins.isop = isop;
                currentBlob()->push(ins);
            }

            void patchAt(size_t idx, uint8_t byte)
            {
                currentBlob()->m_instrucs[idx].code = byte;
            }

            void emitInstruction(uint8_t byte)
            {
                emit(byte, m_prevtoken.line, true);
            }

            void emit1byte(uint8_t byte)
            {
                emit(byte, m_prevtoken.line, false);
            }

            void emit1short(uint16_t byte)
            {
                emit((byte >> 8) & 0xff, m_prevtoken.line, false);
                emit(byte & 0xff, m_prevtoken.line, false);
            }

            void emit2byte(uint8_t byte, uint8_t byte2)
            {
                emit(byte, m_prevtoken.line, false);
                emit(byte2, m_prevtoken.line, false);
            }

            void emitByteAndShort(uint8_t byte, uint16_t byte2)
            {
                emit(byte, m_prevtoken.line, false);
                emit((byte2 >> 8) & 0xff, m_prevtoken.line, false);
                emit(byte2 & 0xff, m_prevtoken.line, false);
            }

            void emitLoop(int loopstart)
            {
                int offset;
                emitInstruction(Instruction::OP_LOOP);
                offset = currentBlob()->m_count - loopstart + 2;
                if(offset > UINT16_MAX)
                {
                    raiseError("loop body too large");
                }
                emit1byte((offset >> 8) & 0xff);
                emit1byte(offset & 0xff);
            }

            void emitReturn()
            {
                if(m_istrying)
                {
                    emitInstruction(Instruction::OP_EXPOPTRY);
                }
                if(m_currfunccompiler->m_type == FuncCommon::FUNCTYPE_INITIALIZER)
                {
                    emitInstruction(Instruction::OP_LOCALGET);
                    emit1short(0);
                }
                else
                {
                    if(!m_keeplastvalue || m_lastwasstatement)
                    {
                        if(m_currfunccompiler->m_fromimport)
                        {
                            emitInstruction(Instruction::OP_PUSHNULL);
                        }
                        else
                        {
                            emitInstruction(Instruction::OP_PUSHEMPTY);
                        }
                    }
                }
                emitInstruction(Instruction::OP_RETURN);
            }

            int pushConst(Value value)
            {
                int constant;
                constant = currentBlob()->pushConst(value);
                if(constant >= UINT16_MAX)
                {
                    raiseError("too many constants in current scope");
                    return 0;
                }
                return constant;
            }

            void emitConst(Value value)
            {
                int constant;
                constant = pushConst(value);
                emitInstruction(Instruction::OP_PUSHCONSTANT);
                emit1short((uint16_t)constant);
            }

            int emitJump(uint8_t instruction)
            {
                emitInstruction(instruction);
                /* placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                return currentBlob()->m_count - 2;
            }

            int emitSwitch()
            {
                emitInstruction(Instruction::OP_SWITCH);
                /* placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                return currentBlob()->m_count - 2;
            }

            int emitTry()
            {
                emitInstruction(Instruction::OP_EXTRY);
                /* type placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                /* handler placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                /* finally placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                return currentBlob()->m_count - 6;
            }

            void emitPatchSwitch(int offset, int constant)
            {
                patchAt(offset, (constant >> 8) & 0xff);
                patchAt(offset + 1, constant & 0xff);
            }

            void emitPatchTry(int offset, int type, int address, int finally)
            {
                /* patch type */
                patchAt(offset, (type >> 8) & 0xff);
                patchAt(offset + 1, type & 0xff);
                /* patch address */
                patchAt(offset + 2, (address >> 8) & 0xff);
                patchAt(offset + 3, address & 0xff);
                /* patch finally */
                patchAt(offset + 4, (finally >> 8) & 0xff);
                patchAt(offset + 5, finally & 0xff);
            }

            void emitPatchJump(int offset)
            {
                /* -2 to adjust the bytecode for the offset itself */
                int jump;
                jump = currentBlob()->m_count - offset - 2;
                if(jump > UINT16_MAX)
                {
                    raiseError("body of conditional block too large");
                }
                patchAt(offset, (jump >> 8) & 0xff);
                patchAt(offset + 1, jump & 0xff);

            }

            bool checkNumber()
            {
                Token::Type t;
                t = m_prevtoken.type;
                if(t == Token::TOK_LITNUMREG || t == Token::TOK_LITNUMOCT || t == Token::TOK_LITNUMBIN || t == Token::TOK_LITNUMHEX)
                {
                    return true;
                }
                return false;
            }

            bool check(Token::Type t)
            {
                return m_currtoken.type == t;
            }

            bool match(Token::Type t)
            {
                if(!check(t))
                {
                    return false;
                }
                advance();
                return true;
            }

            void ignoreSpace()
            {
                while(true)
                {
                    if(check(Token::TOK_NEWLINE))
                    {
                        advance();
                    }
                    else
                    {
                        break;
                    }
                }
            }

            bool doParsePrecedence(Rule::Precedence precedence/*, AstExpression* dest*/)
            {
                bool canassign;
                Token previous;
                Rule::InfixFN infixrule;
                Rule::PrefixFN prefixrule;
                prefixrule = getRule(m_prevtoken.type)->fnprefix;
                if(prefixrule == nullptr)
                {
                    raiseError("expected expression");
                    return false;
                }
                canassign = precedence <= Rule::PREC_ASSIGNMENT;
                prefixrule(this, canassign);
                while(precedence <= getRule(m_currtoken.type)->precedence)
                {
                    previous = m_prevtoken;
                    ignoreSpace();
                    advance();
                    infixrule = getRule(m_prevtoken.type)->fninfix;
                    infixrule(this, previous, canassign);
                }
                if(canassign && match(Token::TOK_ASSIGN))
                {
                    raiseError("invalid assignment target");
                    return false;
                }
                return true;
            }

            bool parsePrecedence(Rule::Precedence precedence)
            {
                if(m_lexer->isAtEnd() && m_pvm->m_isreplmode)
                {
                    return false;
                }
                ignoreSpace();
                if(m_lexer->isAtEnd() && m_pvm->m_isreplmode)
                {
                    return false;
                }
                advance();
                return doParsePrecedence(precedence);
            }

            bool parsePrecNoAdvance(Rule::Precedence precedence)
            {
                if(m_lexer->isAtEnd() && m_pvm->m_isreplmode)
                {
                    return false;
                }
                ignoreSpace();
                if(m_lexer->isAtEnd() && m_pvm->m_isreplmode)
                {
                    return false;
                }
                return doParsePrecedence(precedence);
            }

            bool parseExpression()
            {
                return parsePrecedence(Rule::PREC_ASSIGNMENT);
            }

            void printCompiledFuncBlob(const char* fname)
            {
                DebugPrinter dp(m_pvm, m_pvm->m_debugprinter);
                dp.printFunctionDisassembly(currentBlob(), fname);
            }

            FuncScript* endCompiler()
            {
                const char* fname;
                FuncScript* function;
                emitReturn();
                function = m_currfunccompiler->m_targetfunc;
                fname = nullptr;
                if(function->m_scriptfnname == nullptr)
                {
                    fname = m_currmodule->m_physlocation->data();
                }
                else
                {
                    fname = function->m_scriptfnname->data();
                }
                if(!m_haderror && m_pvm->m_conf.dumpbytecode)
                {
                    printCompiledFuncBlob(fname);
                }
                NEON_ASTDEBUG(m_pvm, "for function '%s'", fname);
                m_currfunccompiler = m_currfunccompiler->m_enclosing;
                return function;
            }


            Rule* getRule(Token::Type type);

            /*
            // Reads [digits] hex digits in a string literal and returns their number value.
            */
            int readStringHexEscape(const char* str, int index, int count)
            {
                size_t pos;
                int i;
                int cval;
                int digit;
                int value;
                value = 0;
                i = 0;
                digit = 0;
                for(; i < count; i++)
                {
                    pos = (index + i + 2);
                    cval = str[pos];
                    digit = Lexer::readHexDigit(cval);
                    if(digit == -1)
                    {
                        raiseError("invalid hex escape sequence at #%d of \"%s\": '%c' (%d)", pos, str, cval, cval);
                    }
                    value = (value * 16) | digit;
                }
                if(count == 4 && (digit = Lexer::readHexDigit(str[index + i + 2])) != -1)
                {
                    value = (value * 16) | digit;
                }
                return value;
            }

            int readStringUnicodeEscape(char* string, const char* realstring, int numberbytes, int realindex, int index)
            {
                int value;
                int count;
                size_t len;
                char* chr;
                NEON_ASTDEBUG(m_pvm, "");
                value = readStringHexEscape(realstring, realindex, numberbytes);
                count = Util::utf8NumBytes(value);
                if(count == -1)
                {
                    raiseError("cannot encode a negative unicode value");
                }
                /* check for greater that \uffff */
                if(value > 65535)
                {
                    count++;
                }
                if(count != 0)
                {
                    chr = Util::utf8Encode(m_pvm, value, &len);
                    if(chr)
                    {
                        memcpy(string + index, chr, (size_t)count + 1);
                        Memory::osFree(chr);
                    }
                    else
                    {
                        raiseError("cannot decode unicode escape at index %d", realindex);
                    }
                }
                /* but greater than \uffff doesn't occupy any extra byte */
                /*
                if(value > 65535)
                {
                    count--;
                }
                */
                return count;
            }

            char* parseString(int* length)
            {
                int k;
                int i;
                int count;
                int reallength;
                int rawlen;
                char c;
                char quote;
                char* deststr;
                char* realstr;
                rawlen = (((size_t)m_prevtoken.length - 2) + 1);
                NEON_ASTDEBUG(m_pvm, "raw length=%d", rawlen);
                deststr = (char*)State::GC::allocate(m_pvm, sizeof(char), rawlen);
                quote = m_prevtoken.start[0];
                realstr = (char*)m_prevtoken.start + 1;
                reallength = m_prevtoken.length - 2;
                k = 0;
                for(i = 0; i < reallength; i++, k++)
                {
                    c = realstr[i];
                    if(c == '\\' && i < reallength - 1)
                    {
                        switch(realstr[i + 1])
                        {
                            case '0':
                                {
                                    c = '\0';
                                }
                                break;
                            case '$':
                                {
                                    c = '$';
                                }
                                break;
                            case '\'':
                                {
                                    if(quote == '\'' || quote == '}')
                                    {
                                        /* } handle closing of interpolation. */
                                        c = '\'';
                                    }
                                    else
                                    {
                                        i--;
                                    }
                                }
                                break;
                            case '"':
                                {
                                    if(quote == '"' || quote == '}')
                                    {
                                        c = '"';
                                    }
                                    else
                                    {
                                        i--;
                                    }
                                }
                                break;
                            case 'a':
                                {
                                    c = '\a';
                                }
                                break;
                            case 'b':
                                {
                                    c = '\b';
                                }
                                break;
                            case 'f':
                                {
                                    c = '\f';
                                }
                                break;
                            case 'n':
                                {
                                    c = '\n';
                                }
                                break;
                            case 'r':
                                {
                                    c = '\r';
                                }
                                break;
                            case 't':
                                {
                                    c = '\t';
                                }
                                break;
                            case 'e':
                                {
                                    c = 27;
                                }
                                break;
                            case '\\':
                                {
                                    c = '\\';
                                }
                                break;
                            case 'v':
                                {
                                    c = '\v';
                                }
                                break;
                            case 'x':
                                {
                                    //k += readStringUnicodeEscape(deststr, realstr, 2, i, k) - 1;
                                    //k += readStringHexEscape(deststr, i, 2) - 0;
                                    c = readStringHexEscape(realstr, i, 2) - 0;
                                    i += 2;
                                    //continue;
                                }
                                break;
                            case 'u':
                                {
                                    count = readStringUnicodeEscape(deststr, realstr, 4, i, k);
                                    if(count > 4)
                                    {
                                        k += count - 2;
                                    }
                                    else
                                    {
                                        k += count - 1;
                                    }
                                    if(count > 4)
                                    {
                                        i += 6;
                                    }
                                    else
                                    {
                                        i += 5;
                                    }
                                    continue;
                                }
                            case 'U':
                                {
                                    count = readStringUnicodeEscape(deststr, realstr, 8, i, k);
                                    if(count > 4)
                                    {
                                        k += count - 2;
                                    }
                                    else
                                    {
                                        k += count - 1;
                                    }
                                    i += 9;
                                    continue;
                                }
                            default:
                                {
                                    i--;
                                }
                                break;
                        }
                        i++;
                    }
                    memcpy(deststr + k, &c, 1);
                }
                *length = k;
                deststr[k] = '\0';
                return deststr;
            }

            Value parseNumber()
            {
                double dbval;
                long longval;
                long long llval;
                NEON_ASTDEBUG(m_pvm, "");
                if(m_prevtoken.type == Token::TOK_LITNUMBIN)
                {
                    llval = strtoll(m_prevtoken.start + 2, nullptr, 2);
                    return Value::makeNumber(llval);
                }
                else if(m_prevtoken.type == Token::TOK_LITNUMOCT)
                {
                    longval = strtol(m_prevtoken.start + 2, nullptr, 8);
                    return Value::makeNumber(longval);
                }
                else if(m_prevtoken.type == Token::TOK_LITNUMHEX)
                {
                    longval = strtol(m_prevtoken.start, nullptr, 16);
                    return Value::makeNumber(longval);
                }
                dbval = strtod(m_prevtoken.start, nullptr);
                return Value::makeNumber(dbval);
            }


            void parseDeclaration()
            {
                ignoreSpace();
                if(match(Token::TOK_KWCLASS))
                {
                    nn_astparser_parseclassdeclaration(this);
                }
                else if(match(Token::TOK_KWFUNCTION))
                {
                    nn_astparser_parsefuncdecl(this);
                }
                else if(match(Token::TOK_KWVAR))
                {
                    nn_astparser_parsevardecl(this, false);
                }
                else if(match(Token::TOK_BRACEOPEN))
                {
                    if(!check(Token::TOK_NEWLINE) && m_currfunccompiler->m_scopedepth == 0)
                    {
                        nn_astparser_parseexprstmt(this, false, true);
                    }
                    else
                    {
                        nn_astparser_scopebegin(this);
                        nn_astparser_parseblock(this);
                        nn_astparser_scopeend(this);
                    }
                }
                else
                {
                    parseStatement();
                }
                ignoreSpace();
                if(m_panicmode)
                {
                    synchronize();
                }
                ignoreSpace();
            }

            void parseStatement()
            {
                m_replcanecho = false;
                ignoreSpace();
                if(match(Token::TOK_KWECHO))
                {
                    nn_astparser_parseechostmt(this);
                }
                else if(match(Token::TOK_KWIF))
                {
                    nn_astparser_parseifstmt(this);
                }
                else if(match(Token::TOK_KWDO))
                {
                    nn_astparser_parsedo_whilestmt(this);
                }
                else if(match(Token::TOK_KWWHILE))
                {
                    nn_astparser_parsewhilestmt(this);
                }
                else if(match(Token::TOK_KWFOR))
                {
                    nn_astparser_parseforstmt(this);
                }
                else if(match(Token::TOK_KWFOREACH))
                {
                    nn_astparser_parseforeachstmt(this);
                }
                else if(match(Token::TOK_KWSWITCH))
                {
                    nn_astparser_parseswitchstmt(this);
                }
                else if(match(Token::TOK_KWCONTINUE))
                {
                    nn_astparser_parsecontinuestmt(this);
                }
                else if(match(Token::TOK_KWBREAK))
                {
                    nn_astparser_parsebreakstmt(this);
                }
                else if(match(Token::TOK_KWRETURN))
                {
                    nn_astparser_parsereturnstmt(this);
                }
                else if(match(Token::TOK_KWASSERT))
                {
                    nn_astparser_parseassertstmt(this);
                }
                else if(match(Token::TOK_KWTHROW))
                {
                    nn_astparser_parsethrowstmt(this);
                }
                else if(match(Token::TOK_BRACEOPEN))
                {
                    nn_astparser_scopebegin(this);
                    nn_astparser_parseblock(this);
                    nn_astparser_scopeend(this);
                }
                else if(match(Token::TOK_KWTRY))
                {
                    nn_astparser_parsetrystmt(this);
                }
                else
                {
                    nn_astparser_parseexprstmt(this, false, false);
                }
                ignoreSpace();
            }

            void runParser()
            {
                advance();
                ignoreSpace();
                while(!match(Token::TOK_EOF))
                {
                    parseDeclaration();
                }
            }
    };
}

#include "prot.inc"

static NEON_FORCEINLINE void nn_state_apidebugv(neon::State* state, const char* funcname, const char* format, va_list va)
{
    (void)state;
    fprintf(stderr, "API CALL: to '%s': ", funcname);
    vfprintf(stderr, format, va);
    fprintf(stderr, "\n");
}

static NEON_FORCEINLINE void nn_state_apidebug(neon::State* state, const char* funcname, const char* format, ...)
{
    va_list va;
    va_start(va, format);
    nn_state_apidebugv(state, funcname, format, va);
    va_end(va);
}

static NEON_FORCEINLINE void nn_state_astdebugv(neon::State* state, const char* funcname, const char* format, va_list va)
{
    (void)state;
    fprintf(stderr, "AST CALL: to '%s': ", funcname);
    vfprintf(stderr, format, va);
    fprintf(stderr, "\n");
}

static NEON_FORCEINLINE void nn_state_astdebug(neon::State* state, const char* funcname, const char* format, ...)
{
    va_list va;
    va_start(va, format);
    nn_state_astdebugv(state, funcname, format, va);
    va_end(va);
}

namespace neon
{
    FuncScript* Module::compileModuleSource(State* state, Module* module, const char* source, Blob* blob, bool fromimport, bool keeplast)
    {
        return Parser::compileSource(state, module, source, blob, fromimport, keeplast);
    }

    NEON_FORCEINLINE Value State::readConst()
    {
        uint16_t idx;
        idx = readShort();
        return m_vmstate.currentframe->closure->scriptfunc->m_compiledblob->m_constants->m_values[idx];
    }

    void Object::blackenObject(State* state, Object* object)
    {
        #if defined(NEON_CFG_DEBUGGC) && NEON_CFG_DEBUGGC
        state->m_debugprinter->putformat("GC: blacken object at <%p> ", (void*)object);
        state->m_debugprinter->printValue(Value::fromObject(object), false);
        state->m_debugprinter->putformat("\n");
        #endif
        switch(object->m_objtype)
        {
            case Value::OBJTYPE_MODULE:
                {
                    Module* module;
                    module = static_cast<Module*>(object);
                    HashTable::mark(state, module->m_deftable);
                }
                break;
            case Value::OBJTYPE_SWITCH:
                {
                    VarSwitch* sw;
                    sw = static_cast<VarSwitch*>(object);
                    HashTable::mark(state, sw->m_jumppositions);
                }
                break;
            case Value::OBJTYPE_FILE:
                {
                    File* file;
                    file = static_cast<File*>(object);
                    File::mark(state, file);
                }
                break;
            case Value::OBJTYPE_DICT:
                {
                    Dictionary* dict;
                    dict = static_cast<Dictionary*>(object);
                    dict->m_keynames->gcMark();
                    HashTable::mark(state, dict->m_valtable);
                }
                break;
            case Value::OBJTYPE_ARRAY:
                {
                    Array* list;
                    list = static_cast<Array*>(object);
                    list->m_varray->gcMark();
                }
                break;
            case Value::OBJTYPE_FUNCBOUND:
                {
                    FuncBound* bound;
                    bound = static_cast<FuncBound*>(object);
                    Object::markValue(state, bound->receiver);
                    Object::markObject(state, static_cast<Object*>(bound->method));
                }
                break;
            case Value::OBJTYPE_CLASS:
                {
                    ClassObject* klass;
                    klass = static_cast<ClassObject*>(object);
                    Object::markObject(state, static_cast<Object*>(klass->m_classname));
                    HashTable::mark(state, klass->m_methods);
                    HashTable::mark(state, klass->m_staticmethods);
                    HashTable::mark(state, klass->m_staticprops);
                    Object::markValue(state, klass->m_constructor);
                    if(klass->m_superclass != nullptr)
                    {
                        Object::markObject(state, static_cast<Object*>(klass->m_superclass));
                    }
                }
                break;
            case Value::OBJTYPE_FUNCCLOSURE:
                {
                    int i;
                    FuncClosure* closure;
                    closure = static_cast<FuncClosure*>(object);
                    Object::markObject(state, static_cast<Object*>(closure->scriptfunc));
                    for(i = 0; i < closure->m_upvalcount; i++)
                    {
                        Object::markObject(state, static_cast<Object*>(closure->m_storedupvals[i]));
                    }
                }
                break;
            case Value::OBJTYPE_FUNCSCRIPT:
                {
                    FuncScript* function;
                    function = static_cast<FuncScript*>(object);
                    Object::markObject(state, static_cast<Object*>(function->m_scriptfnname));
                    Object::markObject(state, static_cast<Object*>(function->m_inmodule));
                    function->m_compiledblob->m_constants->gcMark();
                }
                break;
            case Value::OBJTYPE_INSTANCE:
                {
                    ClassInstance* instance;
                    instance = static_cast<ClassInstance*>(object);
                    ClassInstance::mark(state, instance);
                }
                break;
            case Value::OBJTYPE_UPVALUE:
                {
                    Object::markValue(state, (static_cast<ScopeUpvalue*>(object))->m_closed);
                }
                break;
            case Value::OBJTYPE_RANGE:
            case Value::OBJTYPE_FUNCNATIVE:
            case Value::OBJTYPE_USERDATA:
            case Value::OBJTYPE_STRING:
                break;
        }
    }

    void Object::destroyObject(State* state, Object* object)
    {
        (void)state;
        #if defined(NEON_CFG_DEBUGGC) && NEON_CFG_DEBUGGC
        state->m_debugprinter->putformat("GC: freeing at <%p> of type %d\n", (void*)object, object->m_objtype);
        #endif
        if(object->m_isstale)
        {
            return;
        }
        switch(object->m_objtype)
        {
            case Value::OBJTYPE_MODULE:
                {
                    Module* module;
                    module = static_cast<Module*>(object);
                    Object::release(state, module);
                }
                break;
            case Value::OBJTYPE_FILE:
                {
                    File* file;
                    file = static_cast<File*>(object);
                    Object::release(state, file);
                }
                break;
            case Value::OBJTYPE_DICT:
                {
                    Dictionary* dict;
                    dict = static_cast<Dictionary*>(object);
                    Object::release(state, dict);
                }
                break;
            case Value::OBJTYPE_ARRAY:
                {
                    Array* list;
                    list = static_cast<Array*>(object);
                    if(list != nullptr)
                    {
                        Object::release(state, list);
                    }
                }
                break;
            case Value::OBJTYPE_FUNCBOUND:
                {
                    FuncBound* bound;
                    bound = static_cast<FuncBound*>(object);
                    /*
                    // a closure may be bound to multiple instances
                    // for this reason, we do not free closures when freeing bound methods
                    */
                    Object::release(state, bound);
                }
                break;
            case Value::OBJTYPE_CLASS:
                {
                    ClassObject* klass;
                    klass = static_cast<ClassObject*>(object);
                    Object::release(state, klass);
                }
                break;
            case Value::OBJTYPE_FUNCCLOSURE:
                {
                    FuncClosure* closure;
                    closure = static_cast<FuncClosure*>(object);
                    Object::release(state, closure);
                }
                break;
            case Value::OBJTYPE_FUNCSCRIPT:
                {
                    FuncScript* function;
                    function = static_cast<FuncScript*>(object);
                    Object::release(state, function);
                }
                break;
            case Value::OBJTYPE_INSTANCE:
                {
                    ClassInstance* instance;
                    instance = static_cast<ClassInstance*>(object);
                    Object::release(state, instance);
                }
                break;
            case Value::OBJTYPE_FUNCNATIVE:
                {
                    FuncNative* native;
                    native = static_cast<FuncNative*>(object);
                    Object::release(state, native);
                }
                break;
            case Value::OBJTYPE_UPVALUE:
                {
                    ScopeUpvalue* upv;
                    upv = static_cast<ScopeUpvalue*>(object);
                    Object::release(state, upv);
                }
                break;
            case Value::OBJTYPE_RANGE:
                {
                    Range* rng;
                    rng = static_cast<Range*>(object);
                    Object::release(state, rng);
                }
                break;
            case Value::OBJTYPE_STRING:
                {
                    String* string;
                    string = static_cast<String*>(object);
                    Object::release(state, string);
                }
                break;
            case Value::OBJTYPE_SWITCH:
                {
                    VarSwitch* sw;
                    sw = static_cast<VarSwitch*>(object);
                    Object::release(state, sw);
                }
                break;
            case Value::OBJTYPE_USERDATA:
                {
                    Userdata* uptr;
                    uptr = static_cast<Userdata*>(object);
                    Object::release(state, uptr);
                }
                break;
            default:
                break;
        }
    }


    bool Value::getObjNumberVal(Value val, size_t* dest)
    {
        if(val.isString())
        {
            *dest = val.asString()->length();
            return true;
        }
        else if(val.isArray())
        {
            *dest = val.asArray()->size();
            return true;
        }
        else if(val.isDict())
        {
            *dest = val.asDict()->size();
        }
        return false;
    }


    bool Value::isObjFalse(Value val)
    {
        /* Non-empty strings are true, empty strings are false.*/
        if(val.isString())
        {
            return val.asString()->length() < 1;
        }
        /* Non-empty lists are true, empty lists are false.*/
        else if(val.isArray())
        {
            return val.asArray()->size() == 0;
        }
        /* Non-empty dicts are true, empty dicts are false. */
        else if(val.isDict())
        {
            return val.asDict()->m_keynames->m_count == 0;
        }
        /*
        // All classes are true
        // All closures are true
        // All bound methods are true
        // All functions are in themselves true if you do not account for what they
        // return.
        */
        return false;
    }

    Value Value::findGreaterObject(Value a, Value b)
    {
        String* osa;
        String* osb;   
        if(a.isString() && b.isString())
        {
            osa = a.asString();
            osb = b.asString();
            if(strncmp(osa->data(), osb->data(), osa->length()) >= 0)
            {
                return a;
            }
            return b;
        }
        else if(a.isFuncScript() && b.isFuncScript())
        {
            if(a.asFuncScript()->m_arity >= b.asFuncScript()->m_arity)
            {
                return a;
            }
            return b;
        }
        else if(a.isFuncClosure() && b.isFuncClosure())
        {
            if(a.asFuncClosure()->scriptfunc->m_arity >= b.asFuncClosure()->scriptfunc->m_arity)
            {
                return a;
            }
            return b;
        }
        else if(a.isRange() && b.isRange())
        {
            if(a.asRange()->m_lower >= b.asRange()->m_lower)
            {
                return a;
            }
            return b;
        }
        else if(a.isClass() && b.isClass())
        {
            if(a.asClass()->m_methods->m_count >= b.asClass()->m_methods->m_count)
            {
                return a;
            }
            return b;
        }
        else if(a.isArray() && b.isArray())
        {
            if(a.asArray()->size() >= b.asArray()->size())
            {
                return a;
            }
            return b;
        }
        else if(a.isDict() && b.isDict())
        {
            if(a.asDict()->m_keynames->m_count >= b.asDict()->m_keynames->m_count)
            {
                return a;
            }
            return b;
        }
        else if(a.isFile() && b.isFile())
        {
            if(strcmp(a.asFile()->m_filepath->data(), b.asFile()->m_filepath->data()) >= 0)
            {
                return a;
            }
            return b;
        }
        else if(b.isObject())
        {
            if(a.asObject()->m_objtype >= b.asObject()->m_objtype)
            {
                return a;
            }
            return b;
        }
        else
        {
            return a;
        }
    }

    Value Value::copyValue(State* state, Value value)
    {
        if(value.isObject())
        {
            switch(value.asObject()->m_objtype)
            {
                case Value::OBJTYPE_STRING:
                    {
                        String* string;
                        string = value.asString();
                        return Value::fromObject(String::copy(state, string->data(), string->length()));
                    }
                    break;
                case Value::OBJTYPE_ARRAY:
                {
                    size_t i;
                    Array* list;
                    Array* newlist;
                    list = value.asArray();
                    newlist = Array::make(state);
                    state->stackPush(Value::fromObject(newlist));
                    for(i = 0; i < list->size(); i++)
                    {
                        newlist->push(list->at(i));
                    }
                    state->stackPop();
                    return Value::fromObject(newlist);
                }
                /*
                case Value::OBJTYPE_DICT:
                    {
                        Dictionary *dict;
                        Dictionary *newdict;
                        dict = value.asDict();
                        newdict = Dictionary::make(state);
                        // @TODO: Figure out how to handle dictionary values correctly
                        // remember that copying keys is redundant and unnecessary
                    }
                    break;
                */
                default:
                    break;
            }
        }
        return value;
    }


    uint32_t Value::getObjHash(Object* object)
    {
        switch(object->m_objtype)
        {
            case Value::OBJTYPE_CLASS:
                {
                    /* Classes just use their name. */
                    return (static_cast<ClassObject*>(object))->m_classname->m_hash;
                }
                break;
            case Value::OBJTYPE_FUNCSCRIPT:
                {
                    /*
                    // Allow bare (non-closure) functions so that we can use a map to find
                    // existing constants in a function's constant table. This is only used
                    // internally. Since user code never sees a non-closure function, they
                    // cannot use them as map keys.
                    */
                    FuncScript* fn;
                    fn = static_cast<FuncScript*>(object);
                    return Util::hashDouble(fn->m_arity) ^ Util::hashDouble(fn->m_compiledblob->m_count);
                }
                break;
            case Value::OBJTYPE_STRING:
                {
                    return (static_cast<String*>(object))->m_hash;
                }
                break;
            default:
                break;
        }
        return 0;
    }

    const char* Value::objClassTypename(Object* obj)
    {
        return (static_cast<ClassInstance*>(obj))->m_fromclass->m_classname->data();
    }

    String* Value::toString(State* state, Value value)
    {
        String* s;
        Printer pd(state);
        pd.printValue( value, false, true);
        s = pd.takeString();
        return s;
    }

    bool Value::compareObject(State* state, Value b)
    {
        size_t i;
        Value::ObjType ta;
        Value::ObjType tb;
        Object* oa;
        Object* ob;
        String* stra;
        String* strb;
        Array* arra;
        Array* arrb;
        oa = asObject();
        ob = b.asObject();
        ta = oa->m_objtype;
        tb = ob->m_objtype;
        if(ta == tb)
        {
            if(ta == Value::OBJTYPE_STRING)
            {
                stra = (String*)oa;
                strb = (String*)ob;
                if(stra->length() == strb->length())
                {
                    if(memcmp(stra->data(), strb->data(), stra->length()) == 0)
                    {
                        return true;
                    }
                    return false;
                }
            }
            if(ta == Value::OBJTYPE_ARRAY)
            {
                arra = (Array*)oa;
                arrb = (Array*)ob;
                if(arra->size() == arrb->size())
                {
                    for(i=0; i<(size_t)arra->size(); i++)
                    {
                        if(!arra->at(i).compare(state, arrb->at(i)))
                        {
                            return false;
                        }
                    }
                    return true;
                }
            }
        }
        return false;
    }

    Value::ObjType Value::objectType()
    {
        return asObject()->m_objtype;
    }

    bool HashTable::compareEntryString(Value vkey, const char* kstr, size_t klen)
    {
        String* entoskey;
        entoskey = vkey.asString();
        if(entoskey->length() == klen)
        {
            if(memcmp(kstr, entoskey->data(), klen) == 0)
            {
                return true;
            }
        }
        return false;
    }

    Property* HashTable::getByObjString(String* str)
    {
        return getFieldByStr(Value::makeEmpty(), str->data(), str->length(), str->m_hash);
    }

    Property* HashTable::getField(Value key)
    {
        String* oskey;
        if(key.isString())
        {
            oskey = key.asString();
            return getFieldByStr(key, oskey->data(), oskey->length(), oskey->m_hash);
        }
        return getFieldByValue(key);
    }

    bool HashTable::setCStrType(const char* cstrkey, Value value, Property::Type ftype)
    {
        String* os;
        State* state;
        state = m_pvm;
        os = String::copy(state, cstrkey);
        return setType(Value::fromObject(os), value, ftype, true);
    }

    String* HashTable::findString(const char* chars, size_t length, uint32_t hash)
    {
        size_t slen;
        uint32_t index;
        const char* sdata;
        HashTable::Entry* entry;
        String* string;
        if(m_count == 0)
        {
            return nullptr;
        }
        index = hash & (m_capacity - 1);
        while(true)
        {
            entry = &m_entries[index];
            if(entry->key.isEmpty())
            {
                /*
                // stop if we find an empty non-tombstone entry
                //if(entry->m_actualval.isNull())
                */
                {
                    return nullptr;
                }
            }
            string = entry->key.asString();
            slen = string->length();
            sdata = string->data();
            if((slen == length) && (string->m_hash == hash) && memcmp(sdata, chars, length) == 0)
            {
                /* we found it */
                return string;
            }
            index = (index + 1) & (m_capacity - 1);
        }
    }

    void HashTable::printTo(Printer* pd, const char* name)
    {
        size_t i;
        HashTable::Entry* entry;
        pd->putformat("<HashTable of %s : {\n", name);
        for(i = 0; i < m_capacity; i++)
        {
            entry = &m_entries[i];
            if(!entry->key.isEmpty())
            {
                pd->printValue(entry->key, true, true);
                pd->putformat(": ");
                pd->printValue(entry->value.m_actualval, true, true);
                if(i != m_capacity - 1)
                {
                    pd->putformat(",\n");
                }
            }
        }
        pd->putformat("}>\n");
    }

    ClassObject* State::findExceptionByName(const char* name)
    {
        auto field = m_namedexceptions->getByCStr(name);
        if(field != nullptr)
        {
            return field->m_actualval.asClass();
        }
        return nullptr;
    }

    template<typename... ArgsT>
    void State::raiseFatal(const char* format, ArgsT&&... args)
    {
        static auto fn_fprintf = fprintf;
        int i;
        int line;
        size_t instruction;
        State::CallFrame* frame;
        FuncScript* function;
        /* flush out anything on stdout first */
        fflush(stdout);
        frame = &m_vmstate.framevalues[m_vmstate.framecount - 1];
        function = frame->closure->scriptfunc;
        instruction = frame->inscode - function->m_compiledblob->m_instrucs - 1;
        line = function->m_compiledblob->m_instrucs[instruction].srcline;
        fprintf(stderr, "RuntimeError: ");
        fn_fprintf(stderr, format, args...);
        fprintf(stderr, " -> %s:%d ", function->m_inmodule->m_physlocation->data(), line);
        fputs("\n", stderr);
        if(m_vmstate.framecount > 1)
        {
            fprintf(stderr, "stacktrace:\n");
            for(i = m_vmstate.framecount - 1; i >= 0; i--)
            {
                frame = &m_vmstate.framevalues[i];
                function = frame->closure->scriptfunc;
                /* -1 because the IP is sitting on the next instruction to be executed */
                instruction = frame->inscode - function->m_compiledblob->m_instrucs - 1;
                fprintf(stderr, "    %s:%d -> ", function->m_inmodule->m_physlocation->data(), function->m_compiledblob->m_instrucs[instruction].srcline);
                if(function->m_scriptfnname == nullptr)
                {
                    fprintf(stderr, "<script>");
                }
                else
                {
                    fprintf(stderr, "%s()", function->m_scriptfnname->data());
                }
                fprintf(stderr, "\n");
            }
        }
        resetVMState();
    }

    ClassInstance* State::makeExceptionInstance(ClassObject* exklass, String* message)
    {
        ClassInstance* instance;
        instance = ClassInstance::make(this, exklass);
        stackPush(Value::fromObject(instance));
        instance->defProperty("message", Value::fromObject(message));
        stackPop();
        return instance;
    }

    template<typename... ArgsT>
    bool State::raiseAtSourceLocation(ClassObject* exklass, const char* format, ArgsT&&... args)
    {
        static auto fn_snprintf = snprintf;
        int length;
        int needed;
        char* message;
        Value stacktrace;
        ClassInstance* instance;
        /* TODO: used to be vasprintf. need to check how much to actually allocate! */
        needed = fn_snprintf(nullptr, 0, format, args...);
        needed += 1;
        message = (char*)Memory::osMalloc(needed+1);
        length = fn_snprintf(message, needed, format, args...);
        instance = makeExceptionInstance(exklass, String::take(this, message, length));
        stackPush(Value::fromObject(instance));
        stacktrace = getExceptionStacktrace();
        stackPush(stacktrace);
        instance->defProperty("stacktrace", stacktrace);
        stackPop();
        return exceptionPropagate();
    }


    ClassObject* State::makeExceptionClass(Module* module, const char* cstrname)
    {
        int messageconst;
        ClassObject* klass;
        String* classname;
        FuncScript* function;
        FuncClosure* closure;
        classname = String::copy(this, cstrname);
        stackPush(Value::fromObject(classname));
        klass = ClassObject::make(this, classname);
        stackPop();
        stackPush(Value::fromObject(klass));
        function = FuncScript::make(this, module, FuncCommon::FUNCTYPE_METHOD);
        function->m_arity = 1;
        function->m_isvariadic = false;
        stackPush(Value::fromObject(function));
        {
            /* g_loc 0 */
            function->m_compiledblob->push(Instruction{true, Instruction::OP_LOCALGET, 0});
            function->m_compiledblob->push(Instruction{false, (0 >> 8) & 0xff, 0});
            function->m_compiledblob->push(Instruction{false, 0 & 0xff, 0});
        }
        {
            /* g_loc 1 */
            function->m_compiledblob->push(Instruction{true, Instruction::OP_LOCALGET, 0});
            function->m_compiledblob->push(Instruction{false, (1 >> 8) & 0xff, 0});
            function->m_compiledblob->push(Instruction{false, 1 & 0xff, 0});
        }
        {
            messageconst = function->m_compiledblob->pushConst(Value::fromObject(String::copy(this, "message")));
            /* s_prop 0 */
            function->m_compiledblob->push(Instruction{true, Instruction::OP_PROPERTYSET, 0});
            function->m_compiledblob->push(Instruction{false, uint8_t((messageconst >> 8) & 0xff), 0});
            function->m_compiledblob->push(Instruction{false, uint8_t(messageconst & 0xff), 0});
        }
        {
            /* pop */
            function->m_compiledblob->push(Instruction{true, Instruction::OP_POPONE, 0});
            function->m_compiledblob->push(Instruction{true, Instruction::OP_POPONE, 0});
        }
        {
            /* g_loc 0 */
            /*
            //  function->m_compiledblob->push(Instruction{true, Instruction::OP_LOCALGET, 0});
            //  function->m_compiledblob->push(Instruction{false, (0 >> 8) & 0xff, 0));
            //  function->m_compiledblob->push(Instruction{false, 0 & 0xff, 0});
            */
        }
        {
            /* ret */
            function->m_compiledblob->push(Instruction{true, Instruction::OP_RETURN, 0});
        }
        closure = FuncClosure::make(this, function);
        stackPop();
        /* set class constructor */
        stackPush(Value::fromObject(closure));
        klass->m_methods->set(Value::fromObject(classname), Value::fromObject(closure));
        klass->m_constructor = Value::fromObject(closure);
        /* set class properties */
        klass->defProperty("message", Value::makeNull());
        klass->defProperty("stacktrace", Value::makeNull());
        m_definedglobals->set(Value::fromObject(classname), Value::fromObject(klass));
        m_namedexceptions->set(Value::fromObject(classname), Value::fromObject(klass));
        /* for class */
        stackPop();
        stackPop();
        /* assert error name */
        /* stackPop(); */
        return klass;
    }


    Value State::getExceptionStacktrace()
    {
        int line;
        size_t i;
        size_t instruction;
        const char* fnname;
        const char* physfile;
        State::CallFrame* frame;
        FuncScript* function;
        String* os;
        Array* oa;
        oa = Array::make(this);
        {
            for(i = 0; i < m_vmstate.framecount; i++)
            {
                Printer pd(this);
                frame = &m_vmstate.framevalues[i];
                function = frame->closure->scriptfunc;
                /* -1 because the IP is sitting on the next instruction to be executed */
                instruction = frame->inscode - function->m_compiledblob->m_instrucs - 1;
                line = function->m_compiledblob->m_instrucs[instruction].srcline;
                physfile = "(unknown)";
                if(function->m_inmodule->m_physlocation != nullptr)
                {
                    if(function->m_inmodule->m_physlocation->m_sbuf != nullptr)
                    {
                        physfile = function->m_inmodule->m_physlocation->data();
                    }
                }
                fnname = "<script>";
                if(function->m_scriptfnname != nullptr)
                {
                    fnname = function->m_scriptfnname->data();
                }
                pd.putformat("from %s() in %s:%d", fnname, physfile, line);
                os = pd.takeString();
                oa->push(Value::fromObject(os));
                if((i > 15) && (m_conf.showfullstack == false))
                {
                    pd = Printer(this);
                    pd.putformat("(only upper 15 entries shown)");
                    os = pd.takeString();
                    oa->push(Value::fromObject(os));
                    break;
                }
            }
            return Value::fromObject(oa);
        }
        return Value::fromObject(String::copy(this, "", 0));
    }

    bool State::exceptionHandleUncaught(ClassInstance* exception)
    {
        int i;
        int cnt;
        const char* colred;
        const char* colreset;
        Value stackitm;
        Property* field;
        String* emsg;
        Array* oa;
        Color nc;
        colred = nc.color('r');
        colreset = nc.color('0');
        /* at this point, the exception is unhandled; so, print it out. */
        fprintf(stderr, "%sunhandled %s%s", colred, exception->m_fromclass->m_classname->data(), colreset);    
        field = exception->m_properties->getByCStr("message");
        if(field != nullptr)
        {
            emsg = Value::toString(this, field->m_actualval);
            if(emsg->length() > 0)
            {
                fprintf(stderr, ": %s", emsg->data());
            }
            else
            {
                fprintf(stderr, ":");
            }
            fprintf(stderr, "\n");
        }
        else
        {
            fprintf(stderr, "\n");
        }
        field = exception->m_properties->getByCStr("stacktrace");
        if(field != nullptr)
        {
            fprintf(stderr, "  stacktrace:\n");
            oa = field->m_actualval.asArray();
            cnt = oa->size();
            i = cnt-1;
            if(cnt > 0)
            {
                while(true)
                {
                    stackitm = oa->at(i);
                    m_debugprinter->putformat("  ");
                    m_debugprinter->printValue(stackitm, false, true);
                    m_debugprinter->putformat("\n");
                    if(i == 0)
                    {
                        break;
                    }
                    i--;
                }
            }
        }
        return false;
    }        

    bool State::exceptionPropagate()
    {
        int i;
        FuncScript* function;
        State::ExceptionFrame* handler;
        ClassInstance* exception;
        exception = stackPeek(0).asInstance();
        while(m_vmstate.framecount > 0)
        {
            m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
            for(i = m_vmstate.currentframe->handlercount; i > 0; i--)
            {
                handler = &m_vmstate.currentframe->handlers[i - 1];
                function = m_vmstate.currentframe->closure->scriptfunc;
                if(handler->address != 0 && nn_util_isinstanceof(exception->m_fromclass, handler->klass))
                {
                    m_vmstate.currentframe->inscode = &function->m_compiledblob->m_instrucs[handler->address];
                    return true;
                }
                else if(handler->finallyaddress != 0)
                {
                    /* continue propagating once the 'finally' block completes */
                    stackPush(Value::makeBool(true));
                    m_vmstate.currentframe->inscode = &function->m_compiledblob->m_instrucs[handler->finallyaddress];
                    return true;
                }
            }
            m_vmstate.framecount--;
        }
        return exceptionHandleUncaught(exception);
    }





    void State::gcDestroyLinkedObjects()
    {
        Object* next;
        Object* object;
        object = m_vmstate.linkedobjects;
        if(object != nullptr)
        {
            while(object != nullptr)
            {
                next = object->m_nextobj;
                if(object != nullptr)
                {
                    Object::destroyObject(this, object);
                }
                object = next;
            }
        }
        Memory::osFree(m_gcstate.graystack);
        m_gcstate.graystack = nullptr;
    }

    void State::gcTraceRefs()
    {
        Object* object;
        while(m_gcstate.graycount > 0)
        {
            object = m_gcstate.graystack[--m_gcstate.graycount];
            Object::blackenObject(this, object);
        }
    }

    void State::gcMarkRoots()
    {
        int i;
        int j;
        Value* slot;
        ScopeUpvalue* upvalue;
        State::ExceptionFrame* handler;
        for(slot = m_vmstate.stackvalues; slot < &m_vmstate.stackvalues[m_vmstate.stackidx]; slot++)
        {
            Object::markValue(this, *slot);
        }
        for(i = 0; i < (int)m_vmstate.framecount; i++)
        {
            Object::markObject(this, static_cast<Object*>(m_vmstate.framevalues[i].closure));
            for(j = 0; j < (int)m_vmstate.framevalues[i].handlercount; j++)
            {
                handler = &m_vmstate.framevalues[i].handlers[j];
                Object::markObject(this, static_cast<Object*>(handler->klass));
            }
        }
        for(upvalue = m_vmstate.openupvalues; upvalue != nullptr; upvalue = upvalue->m_nextupval)
        {
            Object::markObject(this, static_cast<Object*>(upvalue));
        }
        HashTable::mark(this, m_definedglobals);
        HashTable::mark(this, m_loadedmodules);
        Object::markObject(this, static_cast<Object*>(m_exceptions.stdexception));
        gcMarkCompilerRoots();
    }

    void State::gcMarkCompilerRoots()
    {
        Parser::FuncCompiler* compiler;
        if(m_activeparser != nullptr)
        {
            compiler = m_activeparser->m_currfunccompiler;
            while(compiler != nullptr)
            {
                Object::markObject(this, (Object*)compiler->m_targetfunc);
                compiler = compiler->m_enclosing;
            }
        }
    }

    void State::gcSweep()
    {
        Object* object;
        Object* previous;
        Object* unreached;
        previous = nullptr;
        object = m_vmstate.linkedobjects;
        while(object != nullptr)
        {
            if(object->m_mark == m_currentmarkvalue)
            {
                previous = object;
                object = object->m_nextobj;
            }
            else
            {
                unreached = object;
                object = object->m_nextobj;
                if(previous != nullptr)
                {
                    previous->m_nextobj = object;
                }
                else
                {
                    m_vmstate.linkedobjects = object;
                }
                Object::destroyObject(this, unreached);
            }
        }
    }

    void State::gcCollectGarbage()
    {
        size_t before;
        (void)before;
        #if defined(NEON_CFG_DEBUGGC) && NEON_CFG_DEBUGGC
        m_debugprinter->putformat("GC: gc begins\n");
        before = m_gcstate.bytesallocated;
        #endif
        /*
        //  REMOVE THE NEXT LINE TO DISABLE NESTED gcCollectGarbage() POSSIBILITY!
        */
        #if 0
        m_gcstate.nextgc = m_gcstate.bytesallocated;
        #endif
        gcMarkRoots();
        gcTraceRefs();
        HashTable::removeMarked(this, m_cachedstrings);
        HashTable::removeMarked(this, m_loadedmodules);
        gcSweep();
        m_gcstate.nextgc = m_gcstate.bytesallocated * NEON_CFG_GCHEAPGROWTHFACTOR;
        m_currentmarkvalue = !m_currentmarkvalue;
        #if defined(NEON_CFG_DEBUGGC) && NEON_CFG_DEBUGGC
        m_debugprinter->putformat("GC: gc ends\n");
        m_debugprinter->putformat("GC: collected %zu bytes (from %zu to %zu), next at %zu\n", before - m_gcstate.bytesallocated, before, m_gcstate.bytesallocated, m_gcstate.nextgc);
        #endif
    }

}


void nn_astparser_consumestmtend(neon::Parser* prs)
{
    /* allow block last statement to omit statement end */
    if(prs->m_blockcount > 0 && prs->check(neon::Token::TOK_BRACECLOSE))
    {
        return;
    }
    if(prs->match(neon::Token::TOK_SEMICOLON))
    {
        while(prs->match(neon::Token::TOK_SEMICOLON) || prs->match(neon::Token::TOK_NEWLINE))
        {
        }
        return;
    }
    if(prs->match(neon::Token::TOK_EOF) || prs->m_prevtoken.type == neon::Token::TOK_EOF)
    {
        return;
    }
    /* prs->consume(neon::Token::TOK_NEWLINE, "end of statement expected"); */
    while(prs->match(neon::Token::TOK_SEMICOLON) || prs->match(neon::Token::TOK_NEWLINE))
    {
    }
}

int nn_astparser_getcodeargscount(const neon::Instruction* bytecode, const neon::Value* constants, int ip)
{
    int constant;
    neon::Instruction::OpCode code;
    neon::FuncScript* fn;
    code = (neon::Instruction::OpCode)bytecode[ip].code;
    switch(code)
    {
        case neon::Instruction::OP_EQUAL:
        case neon::Instruction::OP_PRIMGREATER:
        case neon::Instruction::OP_PRIMLESSTHAN:
        case neon::Instruction::OP_PUSHNULL:
        case neon::Instruction::OP_PUSHTRUE:
        case neon::Instruction::OP_PUSHFALSE:
        case neon::Instruction::OP_PRIMADD:
        case neon::Instruction::OP_PRIMSUBTRACT:
        case neon::Instruction::OP_PRIMMULTIPLY:
        case neon::Instruction::OP_PRIMDIVIDE:
        case neon::Instruction::OP_PRIMFLOORDIVIDE:
        case neon::Instruction::OP_PRIMMODULO:
        case neon::Instruction::OP_PRIMPOW:
        case neon::Instruction::OP_PRIMNEGATE:
        case neon::Instruction::OP_PRIMNOT:
        case neon::Instruction::OP_ECHO:
        case neon::Instruction::OP_TYPEOF:
        case neon::Instruction::OP_POPONE:
        case neon::Instruction::OP_UPVALUECLOSE:
        case neon::Instruction::OP_DUPONE:
        case neon::Instruction::OP_RETURN:
        case neon::Instruction::OP_CLASSINHERIT:
        case neon::Instruction::OP_CLASSGETSUPER:
        case neon::Instruction::OP_PRIMAND:
        case neon::Instruction::OP_PRIMOR:
        case neon::Instruction::OP_PRIMBITXOR:
        case neon::Instruction::OP_PRIMSHIFTLEFT:
        case neon::Instruction::OP_PRIMSHIFTRIGHT:
        case neon::Instruction::OP_PRIMBITNOT:
        case neon::Instruction::OP_PUSHONE:
        case neon::Instruction::OP_INDEXSET:
        case neon::Instruction::OP_ASSERT:
        case neon::Instruction::OP_EXTHROW:
        case neon::Instruction::OP_EXPOPTRY:
        case neon::Instruction::OP_MAKERANGE:
        case neon::Instruction::OP_STRINGIFY:
        case neon::Instruction::OP_PUSHEMPTY:
        case neon::Instruction::OP_EXPUBLISHTRY:
            return 0;
        case neon::Instruction::OP_CALLFUNCTION:
        case neon::Instruction::OP_CLASSINVOKESUPERSELF:
        case neon::Instruction::OP_INDEXGET:
        case neon::Instruction::OP_INDEXGETRANGED:
            return 1;
        case neon::Instruction::OP_GLOBALDEFINE:
        case neon::Instruction::OP_GLOBALGET:
        case neon::Instruction::OP_GLOBALSET:
        case neon::Instruction::OP_LOCALGET:
        case neon::Instruction::OP_LOCALSET:
        case neon::Instruction::OP_FUNCARGSET:
        case neon::Instruction::OP_FUNCARGGET:
        case neon::Instruction::OP_UPVALUEGET:
        case neon::Instruction::OP_UPVALUESET:
        case neon::Instruction::OP_JUMPIFFALSE:
        case neon::Instruction::OP_JUMPNOW:
        case neon::Instruction::OP_BREAK_PL:
        case neon::Instruction::OP_LOOP:
        case neon::Instruction::OP_PUSHCONSTANT:
        case neon::Instruction::OP_POPN:
        case neon::Instruction::OP_MAKECLASS:
        case neon::Instruction::OP_PROPERTYGET:
        case neon::Instruction::OP_PROPERTYGETSELF:
        case neon::Instruction::OP_PROPERTYSET:
        case neon::Instruction::OP_MAKEARRAY:
        case neon::Instruction::OP_MAKEDICT:
        case neon::Instruction::OP_IMPORTIMPORT:
        case neon::Instruction::OP_SWITCH:
        case neon::Instruction::OP_MAKEMETHOD:
        //case neon::Instruction::OP_FUNCOPTARG:
            return 2;
        case neon::Instruction::OP_CALLMETHOD:
        case neon::Instruction::OP_CLASSINVOKETHIS:
        case neon::Instruction::OP_CLASSINVOKESUPER:
        case neon::Instruction::OP_CLASSPROPERTYDEFINE:
            return 3;
        case neon::Instruction::OP_EXTRY:
            return 6;
        case neon::Instruction::OP_MAKECLOSURE:
            {
                constant = (bytecode[ip + 1].code << 8) | bytecode[ip + 2].code;
                fn = constants[constant].asFuncScript();
                /* There is two byte for the constant, then three for each up value. */
                return 2 + (fn->m_upvalcount * 3);
            }
            break;
        default:
            break;
    }
    return 0;
}


int nn_astparser_makeidentconst(neon::Parser* prs, neon::Token* name)
{
    int rawlen;
    const char* rawstr;
    neon::String* str;
    rawstr = name->start;
    rawlen = name->length;
    if(name->isglobal)
    {
        rawstr++;
        rawlen--;
    }
    str = neon::String::copy(prs->m_pvm, rawstr, rawlen);
    return prs->pushConst(neon::Value::fromObject(str));
}


int nn_astparser_addlocal(neon::Parser* prs, neon::Token name)
{
    neon::Parser::CompiledLocal local;
    if(prs->m_currfunccompiler->m_localcount == neon::Parser::FuncCompiler::MaxLocals)
    {
        /* we've reached maximum local variables per scope */
        prs->raiseError("too many local variables in scope");
        return -1;
    }
    local.varname = name;
    local.depth = -1;
    local.iscaptured = false;
    //prs->m_currfunccompiler->m_compiledlocals.push(local);
    prs->m_currfunccompiler->m_compiledlocals.insertDefault(local, prs->m_currfunccompiler->m_localcount, neon::Parser::CompiledLocal{});

    prs->m_currfunccompiler->m_localcount++;
    return prs->m_currfunccompiler->m_localcount;
}

void nn_astparser_declarevariable(neon::Parser* prs)
{
    int i;
    neon::Token* name;
    neon::Parser::CompiledLocal* local;
    /* global variables are implicitly declared... */
    if(prs->m_currfunccompiler->m_scopedepth == 0)
    {
        return;
    }
    name = &prs->m_prevtoken;
    for(i = prs->m_currfunccompiler->m_localcount - 1; i >= 0; i--)
    {
        local = &prs->m_currfunccompiler->m_compiledlocals[i];
        if(local->depth != -1 && local->depth < prs->m_currfunccompiler->m_scopedepth)
        {
            break;
        }
        if(neon::Parser::identsEqual(name, &local->varname))
        {
            prs->raiseError("%.*s already declared in current scope", name->length, name->start);
        }
    }
    nn_astparser_addlocal(prs, *name);
}

int nn_astparser_parsevariable(neon::Parser* prs, const char* message)
{
    if(!prs->consume(neon::Token::TOK_IDENTNORMAL, message))
    {
        /* what to do here? */
    }
    nn_astparser_declarevariable(prs);
    /* we are in a local scope... */
    if(prs->m_currfunccompiler->m_scopedepth > 0)
    {
        return 0;
    }
    return nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
}

void nn_astparser_markinitialized(neon::Parser* prs)
{
    if(prs->m_currfunccompiler->m_scopedepth == 0)
    {
        return;
    }
    prs->m_currfunccompiler->m_compiledlocals[prs->m_currfunccompiler->m_localcount - 1].depth = prs->m_currfunccompiler->m_scopedepth;
}

void nn_astparser_definevariable(neon::Parser* prs, int global)
{
    /* we are in a local scope... */
    if(prs->m_currfunccompiler->m_scopedepth > 0)
    {
        nn_astparser_markinitialized(prs);
        return;
    }
    prs->emitInstruction(neon::Instruction::OP_GLOBALDEFINE);
    prs->emit1short(global);
}

neon::Token nn_astparser_synthtoken(const char* name)
{
    neon::Token token;
    token.isglobal = false;
    token.line = 0;
    token.type = (neon::Token::Type)0;
    token.start = name;
    token.length = (int)strlen(name);
    return token;
}

void nn_astparser_scopebegin(neon::Parser* prs)
{
    NEON_ASTDEBUG(prs->m_pvm, "current depth=%d", prs->m_currfunccompiler->m_scopedepth);
    prs->m_currfunccompiler->m_scopedepth++;
}

bool nn_astutil_scopeendcancontinue(neon::Parser* prs)
{
    int lopos;
    int locount;
    int lodepth;
    int scodepth;
    NEON_ASTDEBUG(prs->m_pvm, "");
    locount = prs->m_currfunccompiler->m_localcount;
    lopos = prs->m_currfunccompiler->m_localcount - 1;
    lodepth = prs->m_currfunccompiler->m_compiledlocals[lopos].depth;
    scodepth = prs->m_currfunccompiler->m_scopedepth;
    if(locount > 0 && lodepth > scodepth)
    {
        return true;
    }
    return false;
}

void nn_astparser_scopeend(neon::Parser* prs)
{
    NEON_ASTDEBUG(prs->m_pvm, "current scope depth=%d", prs->m_currfunccompiler->m_scopedepth);
    prs->m_currfunccompiler->m_scopedepth--;
    /*
    // remove all variables declared in scope while exiting...
    */
    if(prs->m_keeplastvalue)
    {
        //return;
    }
    while(nn_astutil_scopeendcancontinue(prs))
    {
        if(prs->m_currfunccompiler->m_compiledlocals[prs->m_currfunccompiler->m_localcount - 1].iscaptured)
        {
            prs->emitInstruction(neon::Instruction::OP_UPVALUECLOSE);
        }
        else
        {
            prs->emitInstruction(neon::Instruction::OP_POPONE);
        }
        prs->m_currfunccompiler->m_localcount--;
    }
}

int nn_astparser_discardlocals(neon::Parser* prs, int depth)
{
    int local;
    NEON_ASTDEBUG(prs->m_pvm, "");
    if(prs->m_keeplastvalue)
    {
        //return 0;
    }
    if(prs->m_currfunccompiler->m_scopedepth == -1)
    {
        prs->raiseError("cannot exit top-level scope");
    }
    local = prs->m_currfunccompiler->m_localcount - 1;
    while(local >= 0 && prs->m_currfunccompiler->m_compiledlocals[local].depth >= depth)
    {
        if(prs->m_currfunccompiler->m_compiledlocals[local].iscaptured)
        {
            prs->emitInstruction(neon::Instruction::OP_UPVALUECLOSE);
        }
        else
        {
            prs->emitInstruction(neon::Instruction::OP_POPONE);
        }
        local--;
    }
    return prs->m_currfunccompiler->m_localcount - local - 1;
}

void nn_astparser_endloop(neon::Parser* prs)
{
    size_t i;
    neon::Instruction* bcode;
    neon::Value* cvals;
    NEON_ASTDEBUG(prs->m_pvm, "");
    /*
    // find all neon::Instruction::OP_BREAK_PL placeholder and replace with the appropriate jump...
    */
    i = prs->m_innermostloopstart;
    while(i < prs->m_currfunccompiler->m_targetfunc->m_compiledblob->m_count)
    {
        if(prs->m_currfunccompiler->m_targetfunc->m_compiledblob->m_instrucs[i].code == neon::Instruction::OP_BREAK_PL)
        {
            prs->m_currfunccompiler->m_targetfunc->m_compiledblob->m_instrucs[i].code = neon::Instruction::OP_JUMPNOW;
            prs->emitPatchJump(i + 1);
            i += 3;
        }
        else
        {
            bcode = prs->m_currfunccompiler->m_targetfunc->m_compiledblob->m_instrucs;
            cvals = prs->m_currfunccompiler->m_targetfunc->m_compiledblob->m_constants->m_values;
            i += 1 + nn_astparser_getcodeargscount(bcode, cvals, i);
        }
    }
}

bool nn_astparser_rulebinary(neon::Parser* prs, neon::Token previous, bool canassign)
{
    neon::Token::Type op;
    neon::Parser::Rule* rule;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    op = prs->m_prevtoken.type;
    /* compile the right operand */
    rule = prs->getRule(op);
    prs->parsePrecedence((neon::Parser::Rule::Precedence)(rule->precedence + 1));
    /* emit the operator instruction */
    switch(op)
    {
        case neon::Token::TOK_PLUS:
            prs->emitInstruction(neon::Instruction::OP_PRIMADD);
            break;
        case neon::Token::TOK_MINUS:
            prs->emitInstruction(neon::Instruction::OP_PRIMSUBTRACT);
            break;
        case neon::Token::TOK_MULTIPLY:
            prs->emitInstruction(neon::Instruction::OP_PRIMMULTIPLY);
            break;
        case neon::Token::TOK_DIVIDE:
            prs->emitInstruction(neon::Instruction::OP_PRIMDIVIDE);
            break;
        case neon::Token::TOK_MODULO:
            prs->emitInstruction(neon::Instruction::OP_PRIMMODULO);
            break;
        case neon::Token::TOK_POWEROF:
            prs->emitInstruction(neon::Instruction::OP_PRIMPOW);
            break;
        case neon::Token::TOK_FLOOR:
            prs->emitInstruction(neon::Instruction::OP_PRIMFLOORDIVIDE);
            break;
            /* equality */
        case neon::Token::TOK_EQUAL:
            prs->emitInstruction(neon::Instruction::OP_EQUAL);
            break;
        case neon::Token::TOK_NOTEQUAL:
            prs->emitInstruction(neon::Instruction::OP_EQUAL);
            prs->emitInstruction(neon::Instruction::OP_PRIMNOT);
            break;
        case neon::Token::TOK_GREATERTHAN:
            prs->emitInstruction(neon::Instruction::OP_PRIMGREATER);
            break;
        case neon::Token::TOK_GREATERASSIGN:
            prs->emitInstruction(neon::Instruction::OP_PRIMLESSTHAN);
            prs->emitInstruction(neon::Instruction::OP_PRIMNOT);
            break;
        case neon::Token::TOK_LESSTHAN:
            prs->emitInstruction(neon::Instruction::OP_PRIMLESSTHAN);
            break;
        case neon::Token::TOK_LESSEQUAL:
            prs->emitInstruction(neon::Instruction::OP_PRIMGREATER);
            prs->emitInstruction(neon::Instruction::OP_PRIMNOT);
            break;
            /* bitwise */
        case neon::Token::TOK_AMPERSAND:
            prs->emitInstruction(neon::Instruction::OP_PRIMAND);
            break;
        case neon::Token::TOK_BARSINGLE:
            prs->emitInstruction(neon::Instruction::OP_PRIMOR);
            break;
        case neon::Token::TOK_XORSINGLE:
            prs->emitInstruction(neon::Instruction::OP_PRIMBITXOR);
            break;
        case neon::Token::TOK_LEFTSHIFT:
            prs->emitInstruction(neon::Instruction::OP_PRIMSHIFTLEFT);
            break;
        case neon::Token::TOK_RIGHTSHIFT:
            prs->emitInstruction(neon::Instruction::OP_PRIMSHIFTRIGHT);
            break;
            /* range */
        case neon::Token::TOK_DOUBLEDOT:
            prs->emitInstruction(neon::Instruction::OP_MAKERANGE);
            break;
        default:
            break;
    }
    return true;
}

bool nn_astparser_rulecall(neon::Parser* prs, neon::Token previous, bool canassign)
{
    uint8_t argcount;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    argcount = nn_astparser_parsefunccallargs(prs);
    prs->emitInstruction(neon::Instruction::OP_CALLFUNCTION);
    prs->emit1byte(argcount);
    return true;
}

bool nn_astparser_ruleliteral(neon::Parser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    switch(prs->m_prevtoken.type)
    {
        case neon::Token::TOK_KWNULL:
            prs->emitInstruction(neon::Instruction::OP_PUSHNULL);
            break;
        case neon::Token::TOK_KWTRUE:
            prs->emitInstruction(neon::Instruction::OP_PUSHTRUE);
            break;
        case neon::Token::TOK_KWFALSE:
            prs->emitInstruction(neon::Instruction::OP_PUSHFALSE);
            break;
        default:
            /* TODO: assuming this is correct behaviour ... */
            return false;
    }
    return true;
}

void nn_astparser_parseassign(neon::Parser* prs, uint8_t realop, uint8_t getop, uint8_t setop, int arg)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    prs->m_replcanecho = false;
    if(getop == neon::Instruction::OP_PROPERTYGET || getop == neon::Instruction::OP_PROPERTYGETSELF)
    {
        prs->emitInstruction(neon::Instruction::OP_DUPONE);
    }
    if(arg != -1)
    {
        prs->emitByteAndShort(getop, arg);
    }
    else
    {
        prs->emitInstruction(getop);
        prs->emit1byte(1);
    }
    prs->parseExpression();
    prs->emitInstruction(realop);
    if(arg != -1)
    {
        prs->emitByteAndShort(setop, (uint16_t)arg);
    }
    else
    {
        prs->emitInstruction(setop);
    }
}

void nn_astparser_assignment(neon::Parser* prs, uint8_t getop, uint8_t setop, int arg, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    if(canassign && prs->match(neon::Token::TOK_ASSIGN))
    {
        prs->m_replcanecho = false;
        prs->parseExpression();
        if(arg != -1)
        {
            prs->emitByteAndShort(setop, (uint16_t)arg);
        }
        else
        {
            prs->emitInstruction(setop);
        }
    }
    else if(canassign && prs->match(neon::Token::TOK_PLUSASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMADD, getop, setop, arg);
    }
    else if(canassign && prs->match(neon::Token::TOK_MINUSASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMSUBTRACT, getop, setop, arg);
    }
    else if(canassign && prs->match(neon::Token::TOK_MULTASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMMULTIPLY, getop, setop, arg);
    }
    else if(canassign && prs->match(neon::Token::TOK_DIVASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMDIVIDE, getop, setop, arg);
    }
    else if(canassign && prs->match(neon::Token::TOK_POWASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMPOW, getop, setop, arg);
    }
    else if(canassign && prs->match(neon::Token::TOK_MODASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMMODULO, getop, setop, arg);
    }
    else if(canassign && prs->match(neon::Token::TOK_AMPASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMAND, getop, setop, arg);
    }
    else if(canassign && prs->match(neon::Token::TOK_BARASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMOR, getop, setop, arg);
    }
    else if(canassign && prs->match(neon::Token::TOK_TILDEASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMBITNOT, getop, setop, arg);
    }
    else if(canassign && prs->match(neon::Token::TOK_XORASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMBITXOR, getop, setop, arg);
    }
    else if(canassign && prs->match(neon::Token::TOK_LEFTSHIFTASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMSHIFTLEFT, getop, setop, arg);
    }
    else if(canassign && prs->match(neon::Token::TOK_RIGHTSHIFTASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMSHIFTRIGHT, getop, setop, arg);
    }
    else if(canassign && prs->match(neon::Token::TOK_INCREMENT))
    {
        prs->m_replcanecho = false;
        if(getop == neon::Instruction::OP_PROPERTYGET || getop == neon::Instruction::OP_PROPERTYGETSELF)
        {
            prs->emitInstruction(neon::Instruction::OP_DUPONE);
        }

        if(arg != -1)
        {
            prs->emitByteAndShort(getop, arg);
        }
        else
        {
            prs->emitInstruction(getop);
            prs->emit1byte(1);
        }

        prs->emitInstruction(neon::Instruction::OP_PUSHONE);
        prs->emitInstruction(neon::Instruction::OP_PRIMADD);
        prs->emitInstruction(setop);
        prs->emit1short((uint16_t)arg);
    }
    else if(canassign && prs->match(neon::Token::TOK_DECREMENT))
    {
        prs->m_replcanecho = false;
        if(getop == neon::Instruction::OP_PROPERTYGET || getop == neon::Instruction::OP_PROPERTYGETSELF)
        {
            prs->emitInstruction(neon::Instruction::OP_DUPONE);
        }

        if(arg != -1)
        {
            prs->emitByteAndShort(getop, arg);
        }
        else
        {
            prs->emitInstruction(getop);
            prs->emit1byte(1);
        }

        prs->emitInstruction(neon::Instruction::OP_PUSHONE);
        prs->emitInstruction(neon::Instruction::OP_PRIMSUBTRACT);
        prs->emitInstruction(setop);
        prs->emit1short((uint16_t)arg);
    }
    else
    {
        if(arg != -1)
        {
            if(getop == neon::Instruction::OP_INDEXGET || getop == neon::Instruction::OP_INDEXGETRANGED)
            {
                prs->emitInstruction(getop);
                prs->emit1byte((uint8_t)0);
            }
            else
            {
                prs->emitByteAndShort(getop, (uint16_t)arg);
            }
        }
        else
        {
            prs->emitInstruction(getop);
            prs->emit1byte((uint8_t)0);
        }
    }
}

bool nn_astparser_ruledot(neon::Parser* prs, neon::Token previous, bool canassign)
{
    int name;
    bool caninvoke;
    uint8_t argcount;
    neon::Instruction::OpCode getop;
    neon::Instruction::OpCode setop;
    NEON_ASTDEBUG(prs->m_pvm, "");
    prs->ignoreSpace();
    if(!prs->consume(neon::Token::TOK_IDENTNORMAL, "expected property name after '.'"))
    {
        return false;
    }
    name = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
    if(prs->match(neon::Token::TOK_PARENOPEN))
    {
        argcount = nn_astparser_parsefunccallargs(prs);
        caninvoke = (
            (prs->m_currclasscompiler != nullptr) &&
            (
                (previous.type == neon::Token::TOK_KWTHIS) ||
                (neon::Parser::identsEqual(&prs->m_prevtoken, &prs->m_currclasscompiler->classname))
            )
        );
        if(caninvoke)
        {
            prs->emitInstruction(neon::Instruction::OP_CLASSINVOKETHIS);
            prs->emit1short(name);
        }
        else
        {
            prs->emitInstruction(neon::Instruction::OP_CALLMETHOD);
            prs->emit1short(name);
        }
        prs->emit1byte(argcount);
    }
    else
    {
        getop = neon::Instruction::OP_PROPERTYGET;
        setop = neon::Instruction::OP_PROPERTYSET;
        if(prs->m_currclasscompiler != nullptr && (previous.type == neon::Token::TOK_KWTHIS || neon::Parser::identsEqual(&prs->m_prevtoken, &prs->m_currclasscompiler->classname)))
        {
            getop = neon::Instruction::OP_PROPERTYGETSELF;
        }
        nn_astparser_assignment(prs, getop, setop, name, canassign);
    }
    return true;
}

void nn_astparser_namedvar(neon::Parser* prs, neon::Token name, bool canassign)
{
    bool fromclass;
    uint8_t getop;
    uint8_t setop;
    int arg;
    (void)fromclass;
    NEON_ASTDEBUG(prs->m_pvm, " name=%.*s", name.length, name.start);
    fromclass = prs->m_currclasscompiler != nullptr;
    arg = prs->m_currfunccompiler->resolveLocal(&name);
    if(arg != -1)
    {
        if(prs->m_infunction)
        {
            getop = neon::Instruction::OP_FUNCARGGET;
            setop = neon::Instruction::OP_FUNCARGSET;
        }
        else
        {
            getop = neon::Instruction::OP_LOCALGET;
            setop = neon::Instruction::OP_LOCALSET;
        }
    }
    else
    {
        arg = prs->m_currfunccompiler->resolveUpvalue(&name);
        if((arg != -1) && (name.isglobal == false))
        {
            getop = neon::Instruction::OP_UPVALUEGET;
            setop = neon::Instruction::OP_UPVALUESET;
        }
        else
        {
            arg = nn_astparser_makeidentconst(prs, &name);
            getop = neon::Instruction::OP_GLOBALGET;
            setop = neon::Instruction::OP_GLOBALSET;
        }
    }
    nn_astparser_assignment(prs, getop, setop, arg, canassign);
}

void nn_astparser_createdvar(neon::Parser* prs, neon::Token name)
{
    int local;
    NEON_ASTDEBUG(prs->m_pvm, "name=%.*s", name.length, name.start);
    if(prs->m_currfunccompiler->m_targetfunc->m_scriptfnname != nullptr)
    {
        local = nn_astparser_addlocal(prs, name) - 1;
        nn_astparser_markinitialized(prs);
        prs->emitInstruction(neon::Instruction::OP_LOCALSET);
        prs->emit1short((uint16_t)local);
    }
    else
    {
        prs->emitInstruction(neon::Instruction::OP_GLOBALDEFINE);
        prs->emit1short((uint16_t)nn_astparser_makeidentconst(prs, &name));
    }
}

bool nn_astparser_rulearray(neon::Parser* prs, bool canassign)
{
    int count;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    /* placeholder for the list */
    prs->emitInstruction(neon::Instruction::OP_PUSHNULL);
    count = 0;
    prs->ignoreSpace();
    if(!prs->check(neon::Token::TOK_BRACKETCLOSE))
    {
        do
        {
            prs->ignoreSpace();
            if(!prs->check(neon::Token::TOK_BRACKETCLOSE))
            {
                /* allow comma to end lists */
                prs->parseExpression();
                prs->ignoreSpace();
                count++;
            }
            prs->ignoreSpace();
        } while(prs->match(neon::Token::TOK_COMMA));
    }
    prs->ignoreSpace();
    prs->consume(neon::Token::TOK_BRACKETCLOSE, "expected ']' at end of list");
    prs->emitInstruction(neon::Instruction::OP_MAKEARRAY);
    prs->emit1short(count);
    return true;
}

bool nn_astparser_ruledictionary(neon::Parser* prs, bool canassign)
{
    bool usedexpression;
    int itemcount;
    neon::Parser::CompContext oldctx;
    (void)canassign;
    (void)oldctx;
    NEON_ASTDEBUG(prs->m_pvm, "");
    /* placeholder for the dictionary */
    prs->emitInstruction(neon::Instruction::OP_PUSHNULL);
    itemcount = 0;
    prs->ignoreSpace();
    if(!prs->check(neon::Token::TOK_BRACECLOSE))
    {
        do
        {
            prs->ignoreSpace();
            if(!prs->check(neon::Token::TOK_BRACECLOSE))
            {
                /* allow last pair to end with a comma */
                usedexpression = false;
                if(prs->check(neon::Token::TOK_IDENTNORMAL))
                {
                    prs->consume(neon::Token::TOK_IDENTNORMAL, "");
                    prs->emitConst(neon::Value::fromObject(neon::String::copy(prs->m_pvm, prs->m_prevtoken.start, prs->m_prevtoken.length)));
                }
                else
                {
                    prs->parseExpression();
                    usedexpression = true;
                }
                prs->ignoreSpace();
                if(!prs->check(neon::Token::TOK_COMMA) && !prs->check(neon::Token::TOK_BRACECLOSE))
                {
                    prs->consume(neon::Token::TOK_COLON, "expected ':' after dictionary key");
                    prs->ignoreSpace();
                    prs->parseExpression();
                }
                else
                {
                    if(usedexpression)
                    {
                        prs->raiseError("cannot infer dictionary values from expressions");
                        return false;
                    }
                    else
                    {
                        nn_astparser_namedvar(prs, prs->m_prevtoken, false);
                    }
                }
                itemcount++;
            }
        } while(prs->match(neon::Token::TOK_COMMA));
    }
    prs->ignoreSpace();
    prs->consume(neon::Token::TOK_BRACECLOSE, "expected '}' after dictionary");
    prs->emitInstruction(neon::Instruction::OP_MAKEDICT);
    prs->emit1short(itemcount);
    return true;
}

bool nn_astparser_ruleindexing(neon::Parser* prs, neon::Token previous, bool canassign)
{
    bool assignable;
    bool commamatch;
    uint8_t getop;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    assignable = true;
    commamatch = false;
    getop = neon::Instruction::OP_INDEXGET;
    if(prs->match(neon::Token::TOK_COMMA))
    {
        prs->emitInstruction(neon::Instruction::OP_PUSHNULL);
        commamatch = true;
        getop = neon::Instruction::OP_INDEXGETRANGED;
    }
    else
    {
        prs->parseExpression();
    }
    if(!prs->match(neon::Token::TOK_BRACKETCLOSE))
    {
        getop = neon::Instruction::OP_INDEXGETRANGED;
        if(!commamatch)
        {
            prs->consume(neon::Token::TOK_COMMA, "expecting ',' or ']'");
        }
        if(prs->match(neon::Token::TOK_BRACKETCLOSE))
        {
            prs->emitInstruction(neon::Instruction::OP_PUSHNULL);
        }
        else
        {
            prs->parseExpression();
            prs->consume(neon::Token::TOK_BRACKETCLOSE, "expected ']' after indexing");
        }
        assignable = false;
    }
    else
    {
        if(commamatch)
        {
            prs->emitInstruction(neon::Instruction::OP_PUSHNULL);
        }
    }
    nn_astparser_assignment(prs, getop, neon::Instruction::OP_INDEXSET, -1, assignable);
    return true;
}

bool nn_astparser_rulevarnormal(neon::Parser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_namedvar(prs, prs->m_prevtoken, canassign);
    return true;
}

bool nn_astparser_rulevarglobal(neon::Parser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_namedvar(prs, prs->m_prevtoken, canassign);
    return true;
}

bool nn_astparser_rulethis(neon::Parser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    #if 0
    if(prs->m_currclasscompiler == nullptr)
    {
        prs->raiseError("cannot use keyword 'this' outside of a class");
        return false;
    }
    #endif
    //if(prs->m_currclasscompiler != nullptr)
    {
        nn_astparser_namedvar(prs, prs->m_prevtoken, false);
        //nn_astparser_namedvar(prs, nn_astparser_synthtoken(neon::g_strthis), false);
    }
    return true;
}

bool nn_astparser_rulesuper(neon::Parser* prs, bool canassign)
{
    int name;
    bool invokeself;
    uint8_t argcount;
    NEON_ASTDEBUG(prs->m_pvm, "");
    (void)canassign;
    if(prs->m_currclasscompiler == nullptr)
    {
        prs->raiseError("cannot use keyword 'super' outside of a class");
        return false;
    }
    else if(!prs->m_currclasscompiler->hassuperclass)
    {
        prs->raiseError("cannot use keyword 'super' in a class without a superclass");
        return false;
    }
    name = -1;
    invokeself = false;
    if(!prs->check(neon::Token::TOK_PARENOPEN))
    {
        prs->consume(neon::Token::TOK_DOT, "expected '.' or '(' after super");
        prs->consume(neon::Token::TOK_IDENTNORMAL, "expected super class method name after .");
        name = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
    }
    else
    {
        invokeself = true;
    }
    nn_astparser_namedvar(prs, nn_astparser_synthtoken(neon::g_strthis), false);
    if(prs->match(neon::Token::TOK_PARENOPEN))
    {
        argcount = nn_astparser_parsefunccallargs(prs);
        nn_astparser_namedvar(prs, nn_astparser_synthtoken(neon::g_strsuper), false);
        if(!invokeself)
        {
            prs->emitInstruction(neon::Instruction::OP_CLASSINVOKESUPER);
            prs->emit1short(name);
            prs->emit1byte(argcount);
        }
        else
        {
            prs->emitInstruction(neon::Instruction::OP_CLASSINVOKESUPERSELF);
            prs->emit1byte(argcount);
        }
    }
    else
    {
        nn_astparser_namedvar(prs, nn_astparser_synthtoken(neon::g_strsuper), false);
        prs->emitInstruction(neon::Instruction::OP_CLASSGETSUPER);
        prs->emit1short(name);
    }
    return true;
}

bool nn_astparser_rulegrouping(neon::Parser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    prs->ignoreSpace();
    prs->parseExpression();
    while(prs->match(neon::Token::TOK_COMMA))
    {
        prs->parseExpression();
    }
    prs->ignoreSpace();
    prs->consume(neon::Token::TOK_PARENCLOSE, "expected ')' after grouped expression");
    return true;
}

bool nn_astparser_rulenumber(neon::Parser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    prs->emitConst(prs->parseNumber());
    return true;
}

bool nn_astparser_rulestring(neon::Parser* prs, bool canassign)
{
    int length;
    char* str;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "canassign=%d", canassign);
    str = prs->parseString(&length);
    prs->emitConst(neon::Value::fromObject(neon::String::take(prs->m_pvm, str, length)));
    return true;
}

bool nn_astparser_ruleinterpolstring(neon::Parser* prs, bool canassign)
{
    int count;
    bool doadd;
    bool stringmatched;
    NEON_ASTDEBUG(prs->m_pvm, "canassign=%d", canassign);
    count = 0;
    do
    {
        doadd = false;
        stringmatched = false;
        if(prs->m_prevtoken.length - 2 > 0)
        {
            nn_astparser_rulestring(prs, canassign);
            doadd = true;
            stringmatched = true;
            if(count > 0)
            {
                prs->emitInstruction(neon::Instruction::OP_PRIMADD);
            }
        }
        prs->parseExpression();
        prs->emitInstruction(neon::Instruction::OP_STRINGIFY);
        if(doadd || (count >= 1 && stringmatched == false))
        {
            prs->emitInstruction(neon::Instruction::OP_PRIMADD);
        }
        count++;
    } while(prs->match(neon::Token::TOK_INTERPOLATION));
    prs->consume(neon::Token::TOK_LITERAL, "unterminated string interpolation");
    if(prs->m_prevtoken.length - 2 > 0)
    {
        nn_astparser_rulestring(prs, canassign);
        prs->emitInstruction(neon::Instruction::OP_PRIMADD);
    }
    return true;
}

bool nn_astparser_ruleunary(neon::Parser* prs, bool canassign)
{
    neon::Token::Type op;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    op = prs->m_prevtoken.type;
    /* compile the expression */
    prs->parsePrecedence(neon::Parser::Rule::PREC_UNARY);
    /* emit instruction */
    switch(op)
    {
        case neon::Token::TOK_MINUS:
            prs->emitInstruction(neon::Instruction::OP_PRIMNEGATE);
            break;
        case neon::Token::TOK_EXCLMARK:
            prs->emitInstruction(neon::Instruction::OP_PRIMNOT);
            break;
        case neon::Token::TOK_TILDESINGLE:
            prs->emitInstruction(neon::Instruction::OP_PRIMBITNOT);
            break;
        default:
            break;
    }
    return true;
}

bool nn_astparser_ruleand(neon::Parser* prs, neon::Token previous, bool canassign)
{
    int endjump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    endjump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    prs->parsePrecedence(neon::Parser::Rule::PREC_AND);
    prs->emitPatchJump(endjump);
    return true;
}

bool nn_astparser_ruleor(neon::Parser* prs, neon::Token previous, bool canassign)
{
    int endjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    elsejump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    endjump = prs->emitJump(neon::Instruction::OP_JUMPNOW);
    prs->emitPatchJump(elsejump);
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    prs->parsePrecedence(neon::Parser::Rule::PREC_OR);
    prs->emitPatchJump(endjump);
    return true;
}

bool nn_astparser_ruleconditional(neon::Parser* prs, neon::Token previous, bool canassign)
{
    int thenjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    thenjump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    prs->ignoreSpace();
    /* compile the then expression */
    prs->parsePrecedence(neon::Parser::Rule::PREC_CONDITIONAL);
    prs->ignoreSpace();
    elsejump = prs->emitJump(neon::Instruction::OP_JUMPNOW);
    prs->emitPatchJump(thenjump);
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    prs->consume(neon::Token::TOK_COLON, "expected matching ':' after '?' conditional");
    prs->ignoreSpace();
    /*
    // compile the else expression
    // here we parse at neon::Parser::Rule::PREC_ASSIGNMENT precedence as
    // linear conditionals can be nested.
    */
    prs->parsePrecedence(neon::Parser::Rule::PREC_ASSIGNMENT);
    prs->emitPatchJump(elsejump);
    return true;
}

bool nn_astparser_ruleimport(neon::Parser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    prs->parseExpression();
    prs->emitInstruction(neon::Instruction::OP_IMPORTIMPORT);
    return true;
}

bool nn_astparser_rulenew(neon::Parser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    prs->consume(neon::Token::TOK_IDENTNORMAL, "class name after 'new'");
    return nn_astparser_rulevarnormal(prs, canassign);
    //return nn_astparser_rulecall(prs, prs->m_prevtoken, canassign);
}

bool nn_astparser_ruletypeof(neon::Parser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    prs->consume(neon::Token::TOK_PARENOPEN, "expected '(' after 'typeof'");
    prs->parseExpression();
    prs->consume(neon::Token::TOK_PARENCLOSE, "expected ')' after 'typeof'");
    prs->emitInstruction(neon::Instruction::OP_TYPEOF);
    return true;
}

bool nn_astparser_rulenothingprefix(neon::Parser* prs, bool canassign)
{
    (void)prs;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    return true;
}

bool nn_astparser_rulenothinginfix(neon::Parser* prs, neon::Token previous, bool canassign)
{
    (void)prs;
    (void)previous;
    (void)canassign;
    return true;
}

#define dorule(tok, prefix, infix, precedence) \
    case tok: return Parser::Rule::make(&dest, prefix, infix, precedence);

neon::Parser::Rule* neon::Parser::getRule(neon::Token::Type type)
{
    static Parser::Rule dest;
    switch(type)
    {
        dorule(Token::TOK_NEWLINE, nn_astparser_rulenothingprefix, nn_astparser_rulenothinginfix, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_PARENOPEN, nn_astparser_rulegrouping, nn_astparser_rulecall, Parser::Rule::PREC_CALL );
        dorule(Token::TOK_PARENCLOSE, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_BRACKETOPEN, nn_astparser_rulearray, nn_astparser_ruleindexing, Parser::Rule::PREC_CALL );
        dorule(Token::TOK_BRACKETCLOSE, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_BRACEOPEN, nn_astparser_ruledictionary, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_BRACECLOSE, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_SEMICOLON, nn_astparser_rulenothingprefix, nn_astparser_rulenothinginfix, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_COMMA, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_BACKSLASH, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_EXCLMARK, nn_astparser_ruleunary, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_NOTEQUAL, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_EQUALITY );
        dorule(Token::TOK_COLON, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_AT, nn_astparser_ruleanonfunc, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_DOT, nullptr, nn_astparser_ruledot, Parser::Rule::PREC_CALL );
        dorule(Token::TOK_DOUBLEDOT, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_RANGE );
        dorule(Token::TOK_TRIPLEDOT, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_PLUS, nn_astparser_ruleunary, nn_astparser_rulebinary, Parser::Rule::PREC_TERM );
        dorule(Token::TOK_PLUSASSIGN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_INCREMENT, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_MINUS, nn_astparser_ruleunary, nn_astparser_rulebinary, Parser::Rule::PREC_TERM );
        dorule(Token::TOK_MINUSASSIGN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_DECREMENT, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_MULTIPLY, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_FACTOR );
        dorule(Token::TOK_MULTASSIGN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_POWEROF, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_FACTOR );
        dorule(Token::TOK_POWASSIGN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_DIVIDE, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_FACTOR );
        dorule(Token::TOK_DIVASSIGN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_FLOOR, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_FACTOR );
        dorule(Token::TOK_ASSIGN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_EQUAL, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_EQUALITY );
        dorule(Token::TOK_LESSTHAN, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_COMPARISON );
        dorule(Token::TOK_LESSEQUAL, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_COMPARISON );
        dorule(Token::TOK_LEFTSHIFT, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_SHIFT );
        dorule(Token::TOK_LEFTSHIFTASSIGN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_GREATERTHAN, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_COMPARISON );
        dorule(Token::TOK_GREATERASSIGN, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_COMPARISON );
        dorule(Token::TOK_RIGHTSHIFT, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_SHIFT );
        dorule(Token::TOK_RIGHTSHIFTASSIGN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_MODULO, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_FACTOR );
        dorule(Token::TOK_MODASSIGN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_AMPERSAND, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_BITAND );
        dorule(Token::TOK_AMPASSIGN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_BARSINGLE, /*nn_astparser_ruleanoncompat*/ nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_BITOR );
        dorule(Token::TOK_BARASSIGN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_TILDESINGLE, nn_astparser_ruleunary, nullptr, Parser::Rule::PREC_UNARY );
        dorule(Token::TOK_TILDEASSIGN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_XORSINGLE, nullptr, nn_astparser_rulebinary, Parser::Rule::PREC_BITXOR );
        dorule(Token::TOK_XORASSIGN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_QUESTION, nullptr, nn_astparser_ruleconditional, Parser::Rule::PREC_CONDITIONAL );
        dorule(Token::TOK_KWAND, nullptr, nn_astparser_ruleand, Parser::Rule::PREC_AND );
        dorule(Token::TOK_KWAS, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWASSERT, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWBREAK, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWCLASS, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWCONTINUE, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWFUNCTION, nn_astparser_ruleanonfunc, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWDEFAULT, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWTHROW, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWDO, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWECHO, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWELSE, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWFALSE, nn_astparser_ruleliteral, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWFOREACH, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWIF, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWIMPORT, nn_astparser_ruleimport, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWIN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWFOR, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWVAR, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWNULL, nn_astparser_ruleliteral, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWNEW, nn_astparser_rulenew, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWTYPEOF, nn_astparser_ruletypeof, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWOR, nullptr, nn_astparser_ruleor, Parser::Rule::PREC_OR );
        dorule(Token::TOK_KWSUPER, nn_astparser_rulesuper, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWRETURN, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWTHIS, nn_astparser_rulethis, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWSTATIC, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWTRUE, nn_astparser_ruleliteral, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWSWITCH, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWCASE, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWWHILE, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWTRY, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWCATCH, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWFINALLY, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_LITERAL, nn_astparser_rulestring, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_LITNUMREG, nn_astparser_rulenumber, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_LITNUMBIN, nn_astparser_rulenumber, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_LITNUMOCT, nn_astparser_rulenumber, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_LITNUMHEX, nn_astparser_rulenumber, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_IDENTNORMAL, nn_astparser_rulevarnormal, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_INTERPOLATION, nn_astparser_ruleinterpolstring, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_EOF, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_ERROR, nullptr, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_KWEMPTY, nn_astparser_ruleliteral, nullptr, Parser::Rule::PREC_NONE );
        dorule(Token::TOK_UNDEFINED, nullptr, nullptr, Parser::Rule::PREC_NONE );
        default:
            fprintf(stderr, "missing rule?\n");
            break;
    }
    return nullptr;
}
#undef dorule

bool nn_astparser_parseblock(neon::Parser* prs)
{
    prs->m_blockcount++;
    prs->ignoreSpace();
    while(!prs->check(neon::Token::TOK_BRACECLOSE) && !prs->check(neon::Token::TOK_EOF))
    {
        prs->parseDeclaration();
    }
    prs->m_blockcount--;
    if(!prs->consume(neon::Token::TOK_BRACECLOSE, "expected '}' after block"))
    {
        return false;
    }
    if(prs->match(neon::Token::TOK_SEMICOLON))
    {
    }
    return true;
}

void nn_astparser_declarefuncargvar(neon::Parser* prs)
{
    int i;
    neon::Token* name;
    neon::Parser::CompiledLocal* local;
    /* global variables are implicitly declared... */
    if(prs->m_currfunccompiler->m_scopedepth == 0)
    {
        return;
    }
    name = &prs->m_prevtoken;
    for(i = prs->m_currfunccompiler->m_localcount - 1; i >= 0; i--)
    {
        local = &prs->m_currfunccompiler->m_compiledlocals[i];
        if(local->depth != -1 && local->depth < prs->m_currfunccompiler->m_scopedepth)
        {
            break;
        }
        if(neon::Parser::identsEqual(name, &local->varname))
        {
            prs->raiseError("%.*s already declared in current scope", name->length, name->start);
        }
    }
    nn_astparser_addlocal(prs, *name);
}


int nn_astparser_parsefuncparamvar(neon::Parser* prs, const char* message)
{
    if(!prs->consume(neon::Token::TOK_IDENTNORMAL, message))
    {
        /* what to do here? */
    }
    nn_astparser_declarefuncargvar(prs);
    /* we are in a local scope... */
    if(prs->m_currfunccompiler->m_scopedepth > 0)
    {
        return 0;
    }
    return nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
}

uint8_t nn_astparser_parsefunccallargs(neon::Parser* prs)
{
    uint8_t argcount;
    argcount = 0;
    if(!prs->check(neon::Token::TOK_PARENCLOSE))
    {
        do
        {
            prs->ignoreSpace();
            prs->parseExpression();
            if(argcount == NEON_CFG_ASTMAXFUNCPARAMS)
            {
                prs->raiseError("cannot have more than %d arguments to a function", NEON_CFG_ASTMAXFUNCPARAMS);
            }
            argcount++;
        } while(prs->match(neon::Token::TOK_COMMA));
    }
    prs->ignoreSpace();
    if(!prs->consume(neon::Token::TOK_PARENCLOSE, "expected ')' after argument list"))
    {
        /* TODO: handle this, somehow. */
    }
    return argcount;
}

void nn_astparser_parsefuncparamlist(neon::Parser* prs)
{
    int paramconstant;
    /* compile argument list... */
    do
    {
        prs->ignoreSpace();
        prs->m_currfunccompiler->m_targetfunc->m_arity++;
        if(prs->m_currfunccompiler->m_targetfunc->m_arity > NEON_CFG_ASTMAXFUNCPARAMS)
        {
            prs->raiseErrorAtCurrent("cannot have more than %d function parameters", NEON_CFG_ASTMAXFUNCPARAMS);
        }
        if(prs->match(neon::Token::TOK_TRIPLEDOT))
        {
            prs->m_currfunccompiler->m_targetfunc->m_isvariadic = true;
            nn_astparser_addlocal(prs, nn_astparser_synthtoken("__args__"));
            nn_astparser_definevariable(prs, 0);
            break;
        }
        paramconstant = nn_astparser_parsefuncparamvar(prs, "expected parameter name");
        nn_astparser_definevariable(prs, paramconstant);
        prs->ignoreSpace();
    } while(prs->match(neon::Token::TOK_COMMA));
}


void nn_astparser_parsefuncfull(neon::Parser* prs, neon::FuncCommon::Type type, bool isanon)
{
    prs->m_infunction = true;
    neon::Parser::FuncCompiler compiler(prs, type, isanon);
    nn_astparser_scopebegin(prs);
    /* compile parameter list */
    prs->consume(neon::Token::TOK_PARENOPEN, "expected '(' after function name");
    if(!prs->check(neon::Token::TOK_PARENCLOSE))
    {
        nn_astparser_parsefuncparamlist(prs);
    }
    prs->consume(neon::Token::TOK_PARENCLOSE, "expected ')' after function parameters");
    compiler.compileBody(false, isanon);
    prs->m_infunction = false;
}

void nn_astparser_parsemethod(neon::Parser* prs, neon::Token classname, bool isstatic)
{
    size_t sn;
    int constant;
    const char* sc;
    neon::FuncCommon::Type type;
    static neon::Token::Type tkns[] = { neon::Token::TOK_IDENTNORMAL, neon::Token::TOK_DECORATOR };
    (void)classname;
    (void)isstatic;
    sc = "constructor";
    sn = strlen(sc);
    prs->consumeOr("method name expected", tkns, 2);
    constant = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
    type = neon::FuncCommon::FUNCTYPE_METHOD;
    if((prs->m_prevtoken.length == (int)sn) && (memcmp(prs->m_prevtoken.start, sc, sn) == 0))
    {
        type = neon::FuncCommon::FUNCTYPE_INITIALIZER;
    }
    else if((prs->m_prevtoken.length > 0) && (prs->m_prevtoken.start[0] == '_'))
    {
        type = neon::FuncCommon::FUNCTYPE_PRIVATE;
    }
    nn_astparser_parsefuncfull(prs, type, false);
    prs->emitInstruction(neon::Instruction::OP_MAKEMETHOD);
    prs->emit1short(constant);
}

bool nn_astparser_ruleanonfunc(neon::Parser* prs, bool canassign)
{
    (void)canassign;
    neon::Parser::FuncCompiler compiler(prs, neon::FuncCommon::FUNCTYPE_FUNCTION, true);
    nn_astparser_scopebegin(prs);
    /* compile parameter list */
    prs->consume(neon::Token::TOK_PARENOPEN, "expected '(' at start of anonymous function");
    if(!prs->check(neon::Token::TOK_PARENCLOSE))
    {
        nn_astparser_parsefuncparamlist(prs);
    }
    prs->consume(neon::Token::TOK_PARENCLOSE, "expected ')' after anonymous function parameters");
    compiler.compileBody(true, true);
    return true;
}

void nn_astparser_parsefield(neon::Parser* prs, bool isstatic)
{
    int fieldconstant;
    prs->consume(neon::Token::TOK_IDENTNORMAL, "class property name expected");
    fieldconstant = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
    if(prs->match(neon::Token::TOK_ASSIGN))
    {
        prs->parseExpression();
    }
    else
    {
        prs->emitInstruction(neon::Instruction::OP_PUSHNULL);
    }
    prs->emitInstruction(neon::Instruction::OP_CLASSPROPERTYDEFINE);
    prs->emit1short(fieldconstant);
    prs->emit1byte(isstatic ? 1 : 0);
    nn_astparser_consumestmtend(prs);
    prs->ignoreSpace();
}

void nn_astparser_parsefuncdecl(neon::Parser* prs)
{
    int global;
    global = nn_astparser_parsevariable(prs, "function name expected");
    nn_astparser_markinitialized(prs);
    nn_astparser_parsefuncfull(prs, neon::FuncCommon::FUNCTYPE_FUNCTION, false);
    nn_astparser_definevariable(prs, global);
}

void nn_astparser_parseclassdeclaration(neon::Parser* prs)
{
    bool isstatic;
    int nameconst;
    neon::Parser::CompContext oldctx;
    neon::Token classname;
    neon::Parser::ClassCompiler classcompiler;
    prs->consume(neon::Token::TOK_IDENTNORMAL, "class name expected");
    nameconst = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
    classname = prs->m_prevtoken;
    nn_astparser_declarevariable(prs);
    prs->emitInstruction(neon::Instruction::OP_MAKECLASS);
    prs->emit1short(nameconst);
    nn_astparser_definevariable(prs, nameconst);
    classcompiler.classname = prs->m_prevtoken;
    classcompiler.hassuperclass = false;
    classcompiler.m_enclosing = prs->m_currclasscompiler;
    prs->m_currclasscompiler = &classcompiler;
    oldctx = prs->m_compcontext;
    prs->m_compcontext = neon::Parser::COMPCONTEXT_CLASS;
    if(prs->match(neon::Token::TOK_LESSTHAN))
    {
        prs->consume(neon::Token::TOK_IDENTNORMAL, "name of superclass expected");
        nn_astparser_rulevarnormal(prs, false);
        if(neon::Parser::identsEqual(&classname, &prs->m_prevtoken))
        {
            prs->raiseError("class %.*s cannot inherit from itself", classname.length, classname.start);
        }
        nn_astparser_scopebegin(prs);
        nn_astparser_addlocal(prs, nn_astparser_synthtoken(neon::g_strsuper));
        nn_astparser_definevariable(prs, 0);
        nn_astparser_namedvar(prs, classname, false);
        prs->emitInstruction(neon::Instruction::OP_CLASSINHERIT);
        classcompiler.hassuperclass = true;
    }
    nn_astparser_namedvar(prs, classname, false);
    prs->ignoreSpace();
    prs->consume(neon::Token::TOK_BRACEOPEN, "expected '{' before class body");
    prs->ignoreSpace();
    while(!prs->check(neon::Token::TOK_BRACECLOSE) && !prs->check(neon::Token::TOK_EOF))
    {
        isstatic = false;
        if(prs->match(neon::Token::TOK_KWSTATIC))
        {
            isstatic = true;
        }

        if(prs->match(neon::Token::TOK_KWVAR))
        {
            nn_astparser_parsefield(prs, isstatic);
        }
        else
        {
            nn_astparser_parsemethod(prs, classname, isstatic);
            prs->ignoreSpace();
        }
    }
    prs->consume(neon::Token::TOK_BRACECLOSE, "expected '}' after class body");
    if(prs->match(neon::Token::TOK_SEMICOLON))
    {
    }
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    if(classcompiler.hassuperclass)
    {
        nn_astparser_scopeend(prs);
    }
    prs->m_currclasscompiler = prs->m_currclasscompiler->m_enclosing;
    prs->m_compcontext = oldctx;
}

void nn_astparser_parsevardecl(neon::Parser* prs, bool isinitializer)
{
    int global;
    int totalparsed;
    totalparsed = 0;
    do
    {
        if(totalparsed > 0)
        {
            prs->ignoreSpace();
        }
        global = nn_astparser_parsevariable(prs, "variable name expected");
        if(prs->match(neon::Token::TOK_ASSIGN))
        {
            prs->parseExpression();
        }
        else
        {
            prs->emitInstruction(neon::Instruction::OP_PUSHNULL);
        }
        nn_astparser_definevariable(prs, global);
        totalparsed++;
    } while(prs->match(neon::Token::TOK_COMMA));

    if(!isinitializer)
    {
        nn_astparser_consumestmtend(prs);
    }
    else
    {
        prs->consume(neon::Token::TOK_SEMICOLON, "expected ';' after initializer");
        prs->ignoreSpace();
    }
}

void nn_astparser_parseexprstmt(neon::Parser* prs, bool isinitializer, bool semi)
{
    if(prs->m_pvm->m_isreplmode && prs->m_currfunccompiler->m_scopedepth == 0)
    {
        prs->m_replcanecho = true;
    }
    if(!semi)
    {
        prs->parseExpression();
    }
    else
    {
        prs->parsePrecNoAdvance(neon::Parser::Rule::PREC_ASSIGNMENT);
    }
    if(!isinitializer)
    {
        if(prs->m_replcanecho && prs->m_pvm->m_isreplmode)
        {
            prs->emitInstruction(neon::Instruction::OP_ECHO);
            prs->m_replcanecho = false;
        }
        else
        {
            //if(!prs->m_keeplastvalue)
            {
                prs->emitInstruction(neon::Instruction::OP_POPONE);
            }
        }
        nn_astparser_consumestmtend(prs);
    }
    else
    {
        prs->consume(neon::Token::TOK_SEMICOLON, "expected ';' after initializer");
        prs->ignoreSpace();
        prs->emitInstruction(neon::Instruction::OP_POPONE);
    }
}

/**
 * iter statements are like for loops in c...
 * they are desugared into a while loop
 *
 * i.e.
 *
 * iter i = 0; i < 10; i++ {
 *    ...
 * }
 *
 * desugars into:
 *
 * var i = 0
 * while i < 10 {
 *    ...
 *    i = i + 1
 * }
 */
void nn_astparser_parseforstmt(neon::Parser* prs)
{
    int exitjump;
    int bodyjump;
    int incrstart;
    int surroundingloopstart;
    int surroundingscopedepth;
    nn_astparser_scopebegin(prs);
    prs->consume(neon::Token::TOK_PARENOPEN, "expected '(' after 'for'");
    /* parse initializer... */
    if(prs->match(neon::Token::TOK_SEMICOLON))
    {
        /* no initializer */
    }
    else if(prs->match(neon::Token::TOK_KWVAR))
    {
        nn_astparser_parsevardecl(prs, true);
    }
    else
    {
        nn_astparser_parseexprstmt(prs, true, false);
    }
    /* keep a copy of the surrounding loop's start and depth */
    surroundingloopstart = prs->m_innermostloopstart;
    surroundingscopedepth = prs->m_innermostloopscopedepth;
    /* update the parser's loop start and depth to the current */
    prs->m_innermostloopstart = prs->currentBlob()->m_count;
    prs->m_innermostloopscopedepth = prs->m_currfunccompiler->m_scopedepth;
    exitjump = -1;
    if(!prs->match(neon::Token::TOK_SEMICOLON))
    {
        /* the condition is optional */
        prs->parseExpression();
        prs->consume(neon::Token::TOK_SEMICOLON, "expected ';' after condition");
        prs->ignoreSpace();
        /* jump out of the loop if the condition is false... */
        exitjump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
        prs->emitInstruction(neon::Instruction::OP_POPONE);
        /* pop the condition */
    }
    /* the iterator... */
    if(!prs->check(neon::Token::TOK_BRACEOPEN))
    {
        bodyjump = prs->emitJump(neon::Instruction::OP_JUMPNOW);
        incrstart = prs->currentBlob()->m_count;
        prs->parseExpression();
        prs->ignoreSpace();
        prs->emitInstruction(neon::Instruction::OP_POPONE);
        prs->emitLoop(prs->m_innermostloopstart);
        prs->m_innermostloopstart = incrstart;
        prs->emitPatchJump(bodyjump);
    }
    prs->consume(neon::Token::TOK_PARENCLOSE, "expected ')' after 'for'");
    prs->parseStatement();
    prs->emitLoop(prs->m_innermostloopstart);
    if(exitjump != -1)
    {
        prs->emitPatchJump(exitjump);
        prs->emitInstruction(neon::Instruction::OP_POPONE);
    }
    nn_astparser_endloop(prs);
    /* reset the loop start and scope depth to the surrounding value */
    prs->m_innermostloopstart = surroundingloopstart;
    prs->m_innermostloopscopedepth = surroundingscopedepth;
    nn_astparser_scopeend(prs);
}

/**
 * for x in iterable {
 *    ...
 * }
 *
 * ==
 *
 * {
 *    var iterable = expression()
 *    var _
 *
 *    while _ = iterable.@itern() {
 *      var x = iterable.@iter()
 *      ...
 *    }
 * }
 *
 * ---------------------------------
 *
 * foreach x, y in iterable {
 *    ...
 * }
 *
 * ==
 *
 * {
 *    var iterable = expression()
 *    var x
 *
 *    while x = iterable.@itern() {
 *      var y = iterable.@iter()
 *      ...
 *    }
 * }
 *
 * Every iterable Object must implement the @iter(x) and the @itern(x)
 * function.
 *
 * to make instances of a user created class iterable,
 * the class must implement the @iter(x) and the @itern(x) function.
 * the @itern(x) must return the current iterating index of the object and
 * the
 * @iter(x) function must return the value at that index.
 * _NOTE_: the @iter(x) function will no longer be called after the
 * @itern(x) function returns a false value. so the @iter(x) never needs
 * to return a false value
 */
void nn_astparser_parseforeachstmt(neon::Parser* prs)
{
    int citer;
    int citern;
    int falsejump;
    int keyslot;
    int valueslot;
    int iteratorslot;
    int surroundingloopstart;
    int surroundingscopedepth;
    neon::Token iteratortoken;
    neon::Token keytoken;
    neon::Token valuetoken;
    nn_astparser_scopebegin(prs);
    /* define @iter and @itern constant */
    citer = prs->pushConst(neon::Value::fromObject(neon::String::copy(prs->m_pvm, "@iter")));
    citern = prs->pushConst(neon::Value::fromObject(neon::String::copy(prs->m_pvm, "@itern")));
    prs->consume(neon::Token::TOK_PARENOPEN, "expected '(' after 'foreach'");
    prs->consume(neon::Token::TOK_IDENTNORMAL, "expected variable name after 'foreach'");
    if(!prs->check(neon::Token::TOK_COMMA))
    {
        keytoken = nn_astparser_synthtoken(" _ ");
        valuetoken = prs->m_prevtoken;
    }
    else
    {
        keytoken = prs->m_prevtoken;
        prs->consume(neon::Token::TOK_COMMA, "");
        prs->consume(neon::Token::TOK_IDENTNORMAL, "expected variable name after ','");
        valuetoken = prs->m_prevtoken;
    }
    prs->consume(neon::Token::TOK_KWIN, "expected 'in' after for loop variable(s)");
    prs->ignoreSpace();
    /*
    // The space in the variable name ensures it won't collide with a user-defined
    // variable.
    */
    iteratortoken = nn_astparser_synthtoken(" iterator ");
    /* Evaluate the sequence expression and store it in a hidden local variable. */
    prs->parseExpression();
    prs->consume(neon::Token::TOK_PARENCLOSE, "expected ')' after 'foreach'");
    if(prs->m_currfunccompiler->m_localcount + 3 > neon::Parser::FuncCompiler::MaxLocals)
    {
        prs->raiseError("cannot declare more than %d variables in one scope", neon::Parser::FuncCompiler::MaxLocals);
        return;
    }
    /* add the iterator to the local scope */
    iteratorslot = nn_astparser_addlocal(prs, iteratortoken) - 1;
    nn_astparser_definevariable(prs, 0);
    /* Create the key local variable. */
    prs->emitInstruction(neon::Instruction::OP_PUSHNULL);
    keyslot = nn_astparser_addlocal(prs, keytoken) - 1;
    nn_astparser_definevariable(prs, keyslot);
    /* create the local value slot */
    prs->emitInstruction(neon::Instruction::OP_PUSHNULL);
    valueslot = nn_astparser_addlocal(prs, valuetoken) - 1;
    nn_astparser_definevariable(prs, 0);
    surroundingloopstart = prs->m_innermostloopstart;
    surroundingscopedepth = prs->m_innermostloopscopedepth;
    /*
    // we'll be jumping back to right before the
    // expression after the loop body
    */
    prs->m_innermostloopstart = prs->currentBlob()->m_count;
    prs->m_innermostloopscopedepth = prs->m_currfunccompiler->m_scopedepth;
    /* key = iterable.iter_n__(key) */
    prs->emitInstruction(neon::Instruction::OP_LOCALGET);
    prs->emit1short(iteratorslot);
    prs->emitInstruction(neon::Instruction::OP_LOCALGET);
    prs->emit1short(keyslot);
    prs->emitInstruction(neon::Instruction::OP_CALLMETHOD);
    prs->emit1short(citern);
    prs->emit1byte(1);
    prs->emitInstruction(neon::Instruction::OP_LOCALSET);
    prs->emit1short(keyslot);
    falsejump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    /* value = iterable.iter__(key) */
    prs->emitInstruction(neon::Instruction::OP_LOCALGET);
    prs->emit1short(iteratorslot);
    prs->emitInstruction(neon::Instruction::OP_LOCALGET);
    prs->emit1short(keyslot);
    prs->emitInstruction(neon::Instruction::OP_CALLMETHOD);
    prs->emit1short(citer);
    prs->emit1byte(1);
    /*
    // Bind the loop value in its own scope. This ensures we get a fresh
    // variable each iteration so that closures for it don't all see the same one.
    */
    nn_astparser_scopebegin(prs);
    /* update the value */
    prs->emitInstruction(neon::Instruction::OP_LOCALSET);
    prs->emit1short(valueslot);
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    prs->parseStatement();
    nn_astparser_scopeend(prs);
    prs->emitLoop(prs->m_innermostloopstart);
    prs->emitPatchJump(falsejump);
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    nn_astparser_endloop(prs);
    prs->m_innermostloopstart = surroundingloopstart;
    prs->m_innermostloopscopedepth = surroundingscopedepth;
    nn_astparser_scopeend(prs);
}

/**
 * switch expression {
 *    case expression {
 *      ...
 *    }
 *    case expression {
 *      ...
 *    }
 *    ...
 * }
 */
void nn_astparser_parseswitchstmt(neon::Parser* prs)
{
    int i;
    int length;
    int tgtaddr;
    int swstate;
    int casecount;
    int switchcode;
    int startoffset;
    char* str;
    neon::Value jump;
    neon::Token::Type casetype;
    neon::VarSwitch* sw;
    neon::String* string;
    neon::GenericArray<int> caseends(prs->m_pvm);
    /* the expression */
    prs->parseExpression();
    prs->consume(neon::Token::TOK_BRACEOPEN, "expected '{' after 'switch' expression");
    prs->ignoreSpace();
    /* 0: before all cases, 1: before default, 2: after default */
    swstate = 0;
    casecount = 0;
    sw = neon::VarSwitch::make(prs->m_pvm);
    prs->m_pvm->stackPush(neon::Value::fromObject(sw));
    switchcode = prs->emitSwitch();
    /*
    prs->emitInstruction(neon::Instruction::OP_SWITCH);
    prs->emit1short(prs->pushConst(neon::Value::fromObject(sw)));
    */
    startoffset = prs->currentBlob()->m_count;
    while(!prs->match(neon::Token::TOK_BRACECLOSE) && !prs->check(neon::Token::TOK_EOF))
    {
        if(prs->match(neon::Token::TOK_KWCASE) || prs->match(neon::Token::TOK_KWDEFAULT))
        {
            casetype = prs->m_prevtoken.type;
            if(swstate == 2)
            {
                prs->raiseError("cannot have another case after a default case");
            }
            if(swstate == 1)
            {
                /* at the end of the previous case, jump over the others... */
                tgtaddr = prs->emitJump(neon::Instruction::OP_JUMPNOW);
                //caseends[casecount++] = tgtaddr;
                caseends.push(tgtaddr);
                casecount++;
            }
            if(casetype == neon::Token::TOK_KWCASE)
            {
                swstate = 1;
                do
                {
                    prs->ignoreSpace();
                    prs->advance();
                    jump = neon::Value::makeNumber((double)prs->currentBlob()->m_count - (double)startoffset);
                    if(prs->m_prevtoken.type == neon::Token::TOK_KWTRUE)
                    {
                        sw->m_jumppositions->set(neon::Value::makeBool(true), jump);
                    }
                    else if(prs->m_prevtoken.type == neon::Token::TOK_KWFALSE)
                    {
                        sw->m_jumppositions->set(neon::Value::makeBool(false), jump);
                    }
                    else if(prs->m_prevtoken.type == neon::Token::TOK_LITERAL)
                    {
                        str = prs->parseString(&length);
                        string = neon::String::take(prs->m_pvm, str, length);
                        /* gc fix */
                        prs->m_pvm->stackPush(neon::Value::fromObject(string));
                        sw->m_jumppositions->set(neon::Value::fromObject(string), jump);
                        /* gc fix */
                        prs->m_pvm->stackPop();
                    }
                    else if(prs->checkNumber())
                    {
                        sw->m_jumppositions->set(prs->parseNumber(), jump);
                    }
                    else
                    {
                        /* pop the switch */
                        prs->m_pvm->stackPop();
                        prs->raiseError("only constants can be used in 'when' expressions");
                        return;
                    }
                } while(prs->match(neon::Token::TOK_COMMA));
            }
            else
            {
                swstate = 2;
                sw->m_defaultjump = prs->currentBlob()->m_count - startoffset;
            }
        }
        else
        {
            /* otherwise, it's a statement inside the current case */
            if(swstate == 0)
            {
                prs->raiseError("cannot have statements before any case");
            }
            prs->parseStatement();
        }
    }
    /* if we ended without a default case, patch its condition jump */
    if(swstate == 1)
    {
        tgtaddr = prs->emitJump(neon::Instruction::OP_JUMPNOW);
        //caseends[casecount++] = tgtaddr;
        caseends.push(tgtaddr);
        casecount++;
    }
    /* patch all the case jumps to the end */
    for(i = 0; i < casecount; i++)
    {
        prs->emitPatchJump(caseends[i]);
    }
    sw->m_exitjump = prs->currentBlob()->m_count - startoffset;
    prs->emitPatchSwitch(switchcode, prs->pushConst(neon::Value::fromObject(sw)));
    /* pop the switch */
    prs->m_pvm->stackPop();
}

void nn_astparser_parseifstmt(neon::Parser* prs)
{
    int elsejump;
    int thenjump;
    prs->parseExpression();
    thenjump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    prs->parseStatement();
    elsejump = prs->emitJump(neon::Instruction::OP_JUMPNOW);
    prs->emitPatchJump(thenjump);
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    if(prs->match(neon::Token::TOK_KWELSE))
    {
        prs->parseStatement();
    }
    prs->emitPatchJump(elsejump);
}

void nn_astparser_parseechostmt(neon::Parser* prs)
{
    prs->parseExpression();
    prs->emitInstruction(neon::Instruction::OP_ECHO);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parsethrowstmt(neon::Parser* prs)
{
    prs->parseExpression();
    prs->emitInstruction(neon::Instruction::OP_EXTHROW);
    nn_astparser_discardlocals(prs, prs->m_currfunccompiler->m_scopedepth - 1);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parseassertstmt(neon::Parser* prs)
{
    prs->consume(neon::Token::TOK_PARENOPEN, "expected '(' after 'assert'");
    prs->parseExpression();
    if(prs->match(neon::Token::TOK_COMMA))
    {
        prs->ignoreSpace();
        prs->parseExpression();
    }
    else
    {
        prs->emitInstruction(neon::Instruction::OP_PUSHNULL);
    }
    prs->emitInstruction(neon::Instruction::OP_ASSERT);
    prs->consume(neon::Token::TOK_PARENCLOSE, "expected ')' after 'assert'");
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parsetrystmt(neon::Parser* prs)
{
    int address;
    int type;
    int finally;
    int trybegins;
    int exitjump;
    int continueexecutionaddress;
    bool catchexists;
    bool finalexists;
    if(prs->m_currfunccompiler->m_compiledexcepthandlercount == NEON_CFG_MAXEXCEPTHANDLERS)
    {
        prs->raiseError("maximum exception handler in scope exceeded");
    }
    prs->m_currfunccompiler->m_compiledexcepthandlercount++;
    prs->m_istrying = true;
    prs->ignoreSpace();
    trybegins = prs->emitTry();
    /* compile the try body */
    prs->parseStatement();
    prs->emitInstruction(neon::Instruction::OP_EXPOPTRY);
    exitjump = prs->emitJump(neon::Instruction::OP_JUMPNOW);
    prs->m_istrying = false;
    /*
    // we can safely use 0 because a program cannot start with a
    // catch or finally block
    */
    address = 0;
    type = -1;
    finally = 0;
    catchexists = false;
    finalexists= false;
    /* catch body must maintain its own scope */
    if(prs->match(neon::Token::TOK_KWCATCH))
    {
        catchexists = true;
        nn_astparser_scopebegin(prs);
        prs->consume(neon::Token::TOK_PARENOPEN, "expected '(' after 'catch'");
        prs->consume(neon::Token::TOK_IDENTNORMAL, "missing exception class name");
        type = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
        address = prs->currentBlob()->m_count;
        if(prs->match(neon::Token::TOK_IDENTNORMAL))
        {
            nn_astparser_createdvar(prs, prs->m_prevtoken);
        }
        else
        {
            prs->emitInstruction(neon::Instruction::OP_POPONE);
        }
          prs->consume(neon::Token::TOK_PARENCLOSE, "expected ')' after 'catch'");
        prs->emitInstruction(neon::Instruction::OP_EXPOPTRY);
        prs->ignoreSpace();
        prs->parseStatement();
        nn_astparser_scopeend(prs);
    }
    else
    {
        type = prs->pushConst(neon::Value::fromObject(neon::String::copy(prs->m_pvm, "Exception")));
    }
    prs->emitPatchJump(exitjump);
    if(prs->match(neon::Token::TOK_KWFINALLY))
    {
        finalexists = true;
        /*
        // if we arrived here from either the try or handler block,
        // we don't want to continue propagating the exception
        */
        prs->emitInstruction(neon::Instruction::OP_PUSHFALSE);
        finally = prs->currentBlob()->m_count;
        prs->ignoreSpace();
        prs->parseStatement();
        continueexecutionaddress = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
        /* pop the bool off the stack */
        prs->emitInstruction(neon::Instruction::OP_POPONE);
        prs->emitInstruction(neon::Instruction::OP_EXPUBLISHTRY);
        prs->emitPatchJump(continueexecutionaddress);
        prs->emitInstruction(neon::Instruction::OP_POPONE);
    }
    if(!finalexists && !catchexists)
    {
        prs->raiseError("try block must contain at least one of catch or finally");
    }
    prs->emitPatchTry(trybegins, type, address, finally);
}

void nn_astparser_parsereturnstmt(neon::Parser* prs)
{
    prs->m_isreturning = true;
    /*
    if(prs->m_currfunccompiler->type == neon::FuncCommon::FUNCTYPE_SCRIPT)
    {
        prs->raiseError("cannot return from top-level code");
    }
    */
    if(prs->match(neon::Token::TOK_SEMICOLON) || prs->match(neon::Token::TOK_NEWLINE))
    {
        prs->emitReturn();
    }
    else
    {
        if(prs->m_currfunccompiler->m_type == neon::FuncCommon::FUNCTYPE_INITIALIZER)
        {
            prs->raiseError("cannot return value from constructor");
        }
        if(prs->m_istrying)
        {
            prs->emitInstruction(neon::Instruction::OP_EXPOPTRY);
        }
        prs->parseExpression();
        prs->emitInstruction(neon::Instruction::OP_RETURN);
        nn_astparser_consumestmtend(prs);
    }
    prs->m_isreturning = false;
}

void nn_astparser_parsewhilestmt(neon::Parser* prs)
{
    int exitjump;
    int surroundingloopstart;
    int surroundingscopedepth;
    surroundingloopstart = prs->m_innermostloopstart;
    surroundingscopedepth = prs->m_innermostloopscopedepth;
    /*
    // we'll be jumping back to right before the
    // expression after the loop body
    */
    prs->m_innermostloopstart = prs->currentBlob()->m_count;
    prs->m_innermostloopscopedepth = prs->m_currfunccompiler->m_scopedepth;
    prs->parseExpression();
    exitjump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    prs->parseStatement();
    prs->emitLoop(prs->m_innermostloopstart);
    prs->emitPatchJump(exitjump);
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    nn_astparser_endloop(prs);
    prs->m_innermostloopstart = surroundingloopstart;
    prs->m_innermostloopscopedepth = surroundingscopedepth;
}

void nn_astparser_parsedo_whilestmt(neon::Parser* prs)
{
    int exitjump;
    int surroundingloopstart;
    int surroundingscopedepth;
    surroundingloopstart = prs->m_innermostloopstart;
    surroundingscopedepth = prs->m_innermostloopscopedepth;
    /*
    // we'll be jumping back to right before the
    // statements after the loop body
    */
    prs->m_innermostloopstart = prs->currentBlob()->m_count;
    prs->m_innermostloopscopedepth = prs->m_currfunccompiler->m_scopedepth;
    prs->parseStatement();
    prs->consume(neon::Token::TOK_KWWHILE, "expecting 'while' statement");
    prs->parseExpression();
    exitjump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    prs->emitLoop(prs->m_innermostloopstart);
    prs->emitPatchJump(exitjump);
    prs->emitInstruction(neon::Instruction::OP_POPONE);
    nn_astparser_endloop(prs);
    prs->m_innermostloopstart = surroundingloopstart;
    prs->m_innermostloopscopedepth = surroundingscopedepth;
}

void nn_astparser_parsecontinuestmt(neon::Parser* prs)
{
    if(prs->m_innermostloopstart == -1)
    {
        prs->raiseError("'continue' can only be used in a loop");
    }
    /*
    // discard local variables created in the loop
    //  discard_local(prs, prs->m_innermostloopscopedepth);
    */
    nn_astparser_discardlocals(prs, prs->m_innermostloopscopedepth + 1);
    /* go back to the top of the loop */
    prs->emitLoop(prs->m_innermostloopstart);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parsebreakstmt(neon::Parser* prs)
{
    if(prs->m_innermostloopstart == -1)
    {
        prs->raiseError("'break' can only be used in a loop");
    }
    /* discard local variables created in the loop */
    /*
    int i;
    for(i = prs->m_currfunccompiler->m_localcount - 1; i >= 0 && prs->m_currfunccompiler->m_compiledlocals[i].depth >= prs->m_currfunccompiler->m_scopedepth; i--)
    {
        if(prs->m_currfunccompiler->m_compiledlocals[i].iscaptured)
        {
            prs->emitInstruction(neon::Instruction::OP_UPVALUECLOSE);
        }
        else
        {
            prs->emitInstruction(neon::Instruction::OP_POPONE);
        }
    }
    */
    nn_astparser_discardlocals(prs, prs->m_innermostloopscopedepth + 1);
    prs->emitJump(neon::Instruction::OP_BREAK_PL);
    nn_astparser_consumestmtend(prs);
}


neon::RegModule* nn_natmodule_load_null(neon::State* state)
{
    (void)state;
    static neon::RegModule::FuncInfo modfuncs[] =
    {
        /* {"somefunc",   true,  myfancymodulefunction},*/
        {nullptr, false, nullptr},
    };

    static neon::RegModule::FieldInfo modfields[] =
    {
        /*{"somefield", true, the_function_that_gets_called},*/
        {nullptr, false, nullptr},
    };

    static neon::RegModule module("null");
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = nullptr;
    module.preloader = nullptr;
    module.unloader = nullptr;
    return &module;
}

void nn_modfn_os_preloader(neon::State* state)
{
    (void)state;
}

neon::Value nn_modfn_os_readdir(neon::State* state, neon::Arguments* args)
{
    const char* dirn;
    FSDirReader rd;
    FSDirItem itm;
    neon::String* os;
    neon::String* aval;
    neon::Array* res;
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    os = args->argv[0].asString();
    dirn = os->data();
    if(fslib_diropen(&rd, dirn))
    {
        res = neon::Array::make(state);
        while(fslib_dirread(&rd, &itm))
        {
            aval = neon::String::copy(state, itm.name);
            res->push(neon::Value::fromObject(aval));
        }
        fslib_dirclose(&rd);
        return neon::Value::fromObject(res);
    }
    else
    {
        state->raiseClass(state->m_exceptions.stdexception, "cannot open directory '%s'", dirn);
    }
    return neon::Value::makeEmpty();
}

neon::RegModule* nn_natmodule_load_os(neon::State* state)
{
    (void)state;
    static neon::RegModule::FuncInfo modfuncs[] =
    {
        {"readdir",   true,  nn_modfn_os_readdir},
        {nullptr,     false, nullptr},
    };
    static neon::RegModule::FieldInfo modfields[] =
    {
        /*{"platform", true, get_os_platform},*/
        {nullptr,       false, nullptr},
    };
    static neon::RegModule module("os");
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = nullptr;
    module.preloader = &nn_modfn_os_preloader;
    module.unloader = nullptr;
    return &module;
}

neon::Value nn_modfn_astscan_scan(neon::State* state, neon::Arguments* args)
{
    size_t prelen;
    const char* prefix;
    const char* cstr;
    neon::String* insrc;
    neon::Lexer* lex;
    neon::Array* arr;
    neon::Dictionary* itm;
    neon::Token token;
    prefix = "TOK_";
    prelen = strlen(prefix);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    insrc = args->argv[0].asString();
    lex = neon::Memory::create<neon::Lexer>(state, insrc->data());
    arr = neon::Array::make(state);
    while(!lex->isAtEnd())
    {
        itm = neon::Dictionary::make(state);
        token = lex->scanToken();
        itm->addEntryCStr("line", neon::Value::makeNumber(token.line));
        cstr = neon::Token::tokenName(token.type);
        itm->addEntryCStr("type", neon::Value::fromObject(neon::String::copy(state, cstr + prelen)));
        itm->addEntryCStr("source", neon::Value::fromObject(neon::String::copy(state, token.start, token.length)));
        arr->push(neon::Value::fromObject(itm));
    }
    neon::Memory::destroy(lex);
    return neon::Value::fromObject(arr);
}

neon::RegModule* nn_natmodule_load_astscan(neon::State* state)
{
    neon::RegModule* ret;
    (void)state;
    static neon::RegModule::FuncInfo modfuncs[] =
    {
        {"scan",   true,  nn_modfn_astscan_scan},
        {nullptr,     false, nullptr},
    };
    static neon::RegModule::FieldInfo modfields[] =
    {
        {nullptr,       false, nullptr},
    };
    static neon::RegModule module("astscan");
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = nullptr;
    module.preloader = nullptr;
    module.unloader = nullptr;
    ret = &module;
    return ret;
}

neon::RegModule::ModInitFN g_builtinmodules[] =
{
    nn_natmodule_load_null,
    nn_natmodule_load_os,
    nn_natmodule_load_astscan,
    nullptr,
};

void nn_import_loadbuiltinmodules(neon::State* state)
{
    int i;
    for(i = 0; g_builtinmodules[i] != nullptr; i++)
    {
        neon::Module::loadBuiltinModule(state, g_builtinmodules[i], nullptr, "<__native__>", nullptr);
    }
}

void nn_import_closemodule(void* dlw)
{
    (void)dlw;
    //dlwrap_dlclose(dlw);
}


bool nn_util_fsfileexists(neon::State* state, const char* filepath)
{
    (void)state;
    #if !defined(NEON_PLAT_ISWINDOWS)
        return access(filepath, F_OK) == 0;
    #else
        struct stat sb;
        if(stat(filepath, &sb) == -1)
        {
            return false;
        }
        return true;
    #endif
}

bool nn_util_fsfileisfile(neon::State* state, const char* filepath)
{
    (void)state;
    (void)filepath;
    return false;
}

bool nn_util_fsfileisdirectory(neon::State* state, const char* filepath)
{
    (void)state;
    (void)filepath;
    return false;
}

char* nn_util_fsgetbasename(neon::State* state, const char* path)
{
    (void)state;
    return osfn_basename(path);
}

#define ENFORCE_VALID_DICT_KEY(chp, index) \
    NEON_ARGS_REJECTTYPE(chp, isArray, index); \
    NEON_ARGS_REJECTTYPE(chp, isDict, index); \
    NEON_ARGS_REJECTTYPE(chp, isFile, index);

neon::Value nn_memberfunc_dict_length(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return neon::Value::makeNumber(args->thisval.asDict()->m_keynames->m_count);
}

neon::Value nn_memberfunc_dict_add(neon::State* state, neon::Arguments* args)
{
    neon::Dictionary* dict;
    NEON_ARGS_CHECKCOUNT(args, 2);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = args->thisval.asDict();
    auto field = dict->m_valtable->getField(args->argv[0]);
    if(field != nullptr)
    {
        return state->raiseFromFunction(args, "duplicate key %s at add()", neon::Value::toString(state, args->argv[0])->data());
    }
    dict->addEntry(args->argv[0], args->argv[1]);
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_dict_set(neon::State* state, neon::Arguments* args)
{
    neon::Dictionary* dict;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 2);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = args->thisval.asDict();
    auto field = dict->m_valtable->getField(args->argv[0]);
    if(field == nullptr)
    {
        dict->addEntry(args->argv[0], args->argv[1]);
    }
    else
    {
        dict->setEntry(args->argv[0], args->argv[1]);
    }
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_dict_clear(neon::State* state, neon::Arguments* args)
{
    neon::Dictionary* dict;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = args->thisval.asDict();
    neon::Memory::destroy(dict->m_keynames);
    neon::Memory::destroy(dict->m_valtable);
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_dict_clone(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Dictionary* dict;
    neon::Dictionary* newdict;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = args->thisval.asDict();
    newdict = state->gcProtect(neon::Dictionary::make(state));
    neon::HashTable::addAll(dict->m_valtable, newdict->m_valtable);
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        newdict->m_keynames->push(dict->m_keynames->m_values[i]);
    }
    return neon::Value::fromObject(newdict);
}

neon::Value nn_memberfunc_dict_compact(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Dictionary* dict;
    neon::Dictionary* newdict;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = args->thisval.asDict();
    newdict = state->gcProtect(neon::Dictionary::make(state));
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        auto field = dict->m_valtable->getField(dict->m_keynames->m_values[i]);
        if(!field->m_actualval.compare(state, neon::Value::makeNull()))
        {
            newdict->addEntry(dict->m_keynames->m_values[i], field->m_actualval);
        }
    }
    return neon::Value::fromObject(newdict);
}

neon::Value nn_memberfunc_dict_contains(neon::State* state, neon::Arguments* args)
{
    (void)state;
    neon::Dictionary* dict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = args->thisval.asDict();
    auto field = dict->m_valtable->getField(args->argv[0]);
    return neon::Value::makeBool(field != nullptr);
}

neon::Value nn_memberfunc_dict_extend(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Dictionary* dict;
    neon::Dictionary* dictcpy;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isDict);
    dict = args->thisval.asDict();
    dictcpy = args->argv[0].asDict();
    for(i = 0; i < dictcpy->m_keynames->m_count; i++)
    {
        auto field = dict->m_valtable->getField(dictcpy->m_keynames->m_values[i]);
        if(field == nullptr)
        {
            dict->m_keynames->push(dictcpy->m_keynames->m_values[i]);
        }
    }
    neon::HashTable::addAll(dictcpy->m_valtable, dict->m_valtable);
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_dict_get(neon::State* state, neon::Arguments* args)
{
    neon::Dictionary* dict;
    neon::Property* field;
    (void)state;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = args->thisval.asDict();
    field = dict->getEntry(args->argv[0]);
    if(field == nullptr)
    {
        if(args->argc == 1)
        {
            return neon::Value::makeNull();
        }
        else
        {
            return args->argv[1];
        }
    }
    return field->m_actualval;
}

neon::Value nn_memberfunc_dict_keys(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Dictionary* dict;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = args->thisval.asDict();
    list = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        list->push(dict->m_keynames->m_values[i]);
    }
    return neon::Value::fromObject(list);
}

neon::Value nn_memberfunc_dict_values(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Dictionary* dict;
    neon::Array* list;
    neon::Property* field;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = args->thisval.asDict();
    list = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        field = dict->getEntry(dict->m_keynames->m_values[i]);
        list->push(field->m_actualval);
    }
    return neon::Value::fromObject(list);
}

neon::Value nn_memberfunc_dict_remove(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int index;
    neon::Dictionary* dict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = args->thisval.asDict();
    auto field = dict->m_valtable->getField(args->argv[0]);
    if(field != nullptr)
    {
        dict->m_valtable->removeByKey(args->argv[0]);
        index = -1;
        for(i = 0; i < dict->m_keynames->m_count; i++)
        {
            if(dict->m_keynames->m_values[i].compare(state, args->argv[0]))
            {
                index = i;
                break;
            }
        }
        for(i = index; i < dict->m_keynames->m_count; i++)
        {
            dict->m_keynames->m_values[i] = dict->m_keynames->m_values[i + 1];
        }
        dict->m_keynames->m_count--;
        return field->m_actualval;
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_dict_isempty(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return neon::Value::makeBool(args->thisval.asDict()->m_keynames->m_count == 0);
}

neon::Value nn_memberfunc_dict_findkey(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    return args->thisval.asDict()->m_valtable->findKey(args->argv[0]);
}

neon::Value nn_memberfunc_dict_tolist(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Array* list;
    neon::Dictionary* dict;
    neon::Array* namelist;
    neon::Array* valuelist;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = args->thisval.asDict();
    namelist = state->gcProtect(neon::Array::make(state));
    valuelist = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        namelist->push(dict->m_keynames->m_values[i]);
        neon::Value value;
        if(dict->m_valtable->get(dict->m_keynames->m_values[i], &value))
        {
            valuelist->push(value);
        }
        else
        {
            /* theoretically impossible */
            valuelist->push(neon::Value::makeNull());
        }
    }
    list = state->gcProtect(neon::Array::make(state));
    list->push(neon::Value::fromObject(namelist));
    list->push(neon::Value::fromObject(valuelist));
    return neon::Value::fromObject(list);
}

neon::Value nn_memberfunc_dict_iter(neon::State* state, neon::Arguments* args)
{
    neon::Value result;
    neon::Dictionary* dict;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    dict = args->thisval.asDict();
    if(dict->m_valtable->get(args->argv[0], &result))
    {
        return result;
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_dict_itern(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Dictionary* dict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    dict = args->thisval.asDict();
    if(args->argv[0].isNull())
    {
        if(dict->m_keynames->m_count == 0)
        {
            return neon::Value::makeBool(false);
        }
        return dict->m_keynames->m_values[0];
    }
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        if(args->argv[0].compare(state, dict->m_keynames->m_values[i]) && (i + 1) < dict->m_keynames->m_count)
        {
            return dict->m_keynames->m_values[i + 1];
        }
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_dict_each(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    neon::Value value;
    neon::Value callable;
    neon::Value unused;
    neon::Dictionary* dict;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = args->thisval.asDict();
    callable = args->argv[0];
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        if(arity > 0)
        {
            dict->m_valtable->get(dict->m_keynames->m_values[i], &value);
            nestargs->m_varray->m_values[0] = value;
            if(arity > 1)
            {
                nestargs->m_varray->m_values[1] = dict->m_keynames->m_values[i];
            }
        }
        nc.call(callable, args->thisval, nestargs, &unused);
    }
    /* pop the argument list */
    state->stackPop();
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_dict_filter(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    neon::Value value;
    neon::Value callable;
    neon::Value result;
    neon::Dictionary* dict;
    neon::Array* nestargs;
    neon::Dictionary* resultdict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = args->thisval.asDict();
    callable = args->argv[0];
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    resultdict = state->gcProtect(neon::Dictionary::make(state));
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        dict->m_valtable->get(dict->m_keynames->m_values[i], &value);
        if(arity > 0)
        {
            nestargs->m_varray->m_values[0] = value;
            if(arity > 1)
            {
                nestargs->m_varray->m_values[1] = dict->m_keynames->m_values[i];
            }
        }
        nc.call(callable, args->thisval, nestargs, &result);
        if(!neon::Value::isFalse(result))
        {
            resultdict->addEntry(dict->m_keynames->m_values[i], value);
        }
    }
    /* pop the call list */
    state->stackPop();
    return neon::Value::fromObject(resultdict);
}

neon::Value nn_memberfunc_dict_some(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    neon::Value result;
    neon::Value value;
    neon::Value callable;
    neon::Dictionary* dict;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = args->thisval.asDict();
    callable = args->argv[0];
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        if(arity > 0)
        {
            dict->m_valtable->get(dict->m_keynames->m_values[i], &value);
            nestargs->m_varray->m_values[0] = value;
            if(arity > 1)
            {
                nestargs->m_varray->m_values[1] = dict->m_keynames->m_values[i];
            }
        }
        nc.call(callable, args->thisval, nestargs, &result);
        if(!neon::Value::isFalse(result))
        {
            /* pop the call list */
            state->stackPop();
            return neon::Value::makeBool(true);
        }
    }
    /* pop the call list */
    state->stackPop();
    return neon::Value::makeBool(false);
}


neon::Value nn_memberfunc_dict_every(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    neon::Value value;
    neon::Value callable;  
    neon::Value result;
    neon::Dictionary* dict;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = args->thisval.asDict();
    callable = args->argv[0];
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        if(arity > 0)
        {
            dict->m_valtable->get(dict->m_keynames->m_values[i], &value);
            nestargs->m_varray->m_values[0] = value;
            if(arity > 1)
            {
                nestargs->m_varray->m_values[1] = dict->m_keynames->m_values[i];
            }
        }
        nc.call(callable, args->thisval, nestargs, &result);
        if(neon::Value::isFalse(result))
        {
            /* pop the call list */
            state->stackPop();
            return neon::Value::makeBool(false);
        }
    }
    /* pop the call list */
    state->stackPop();
    return neon::Value::makeBool(true);
}

neon::Value nn_memberfunc_dict_reduce(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    int startindex;
    neon::Value value;
    neon::Value callable;
    neon::Value accumulator;
    neon::Dictionary* dict;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = args->thisval.asDict();
    callable = args->argv[0];
    startindex = 0;
    accumulator = neon::Value::makeNull();
    if(args->argc == 2)
    {
        accumulator = args->argv[1];
    }
    if(accumulator.isNull() && dict->m_keynames->m_count > 0)
    {
        dict->m_valtable->get(dict->m_keynames->m_values[0], &accumulator);
        startindex = 1;
    }
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = startindex; i < dict->m_keynames->m_count; i++)
    {
        /* only call map for non-empty values in a list. */
        if(!dict->m_keynames->m_values[i].isNull() && !dict->m_keynames->m_values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs->m_varray->m_values[0] = accumulator;
                if(arity > 1)
                {
                    dict->m_valtable->get(dict->m_keynames->m_values[i], &value);
                    nestargs->m_varray->m_values[1] = value;
                    if(arity > 2)
                    {
                        nestargs->m_varray->m_values[2] = dict->m_keynames->m_values[i];
                        if(arity > 4)
                        {
                            nestargs->m_varray->m_values[3] = args->thisval;
                        }
                    }
                }
            }
            nc.call(callable, args->thisval, nestargs, &accumulator);
        }
    }
    /* pop the call list */
    state->stackPop();
    return accumulator;
}


#undef ENFORCE_VALID_DICT_KEY

#define FILE_ERROR(type, message) \
    return state->raiseFromFunction(args, #type " -> %s", message, file->m_filepath->data());

#define RETURN_STATUS(status) \
    if((status) == 0) \
    { \
        return neon::Value::makeBool(true); \
    } \
    else \
    { \
        FILE_ERROR(File, strerror(errno)); \
    }

#define DENY_STD() \
    if(file->m_isstd) \
    return state->raiseFromFunction(args, "method not supported for std files");

int nn_fileobject_close(neon::File* file)
{
    int result;
    if(file->m_filehandle != nullptr && !file->m_isstd)
    {
        fflush(file->m_filehandle);
        result = fclose(file->m_filehandle);
        file->m_filehandle = nullptr;
        file->m_isopen = false;
        file->m_filedesc = -1;
        file->m_istty = false;
        return result;
    }
    return -1;
}

bool nn_fileobject_open(neon::File* file)
{
    if(file->m_filehandle != nullptr)
    {
        return true;
    }
    if(file->m_filehandle == nullptr && !file->m_isstd)
    {
        file->m_filehandle = fopen(file->m_filepath->data(), file->m_filemode->data());
        if(file->m_filehandle != nullptr)
        {
            file->m_isopen = true;
            file->m_filedesc = fileno(file->m_filehandle);
            file->m_istty = osfn_isatty(file->m_filedesc);
            return true;
        }
        else
        {
            file->m_filedesc = -1;
            file->m_istty = false;
        }
        return false;
    }
    return false;
}

neon::Value nn_memberfunc_file_constructor(neon::State* state, neon::Arguments* args)
{
    FILE* hnd;
    const char* path;
    const char* mode;
    neon::String* opath;
    neon::File* file;
    (void)hnd;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    opath = args->argv[0].asString();
    if(opath->length() == 0)
    {
        return state->raiseFromFunction(args, "file path cannot be empty");
    }
    mode = "r";
    if(args->argc == 2)
    {
        NEON_ARGS_CHECKTYPE(args, 1, isString);
        mode = args->argv[1].asString()->data();
    }
    path = opath->data();
    file = state->gcProtect(neon::File::make(state, nullptr, false, path, mode));
    nn_fileobject_open(file);
    return neon::Value::fromObject(file);
}

neon::Value nn_memberfunc_file_exists(neon::State* state, neon::Arguments* args)
{
    neon::String* file;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    file = args->argv[0].asString();
    return neon::Value::makeBool(nn_util_fsfileexists(state, file->data()));
}


neon::Value nn_memberfunc_file_isfile(neon::State* state, neon::Arguments* args)
{
    neon::String* file;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    file = args->argv[0].asString();
    return neon::Value::makeBool(nn_util_fsfileisfile(state, file->data()));
}

neon::Value nn_memberfunc_file_isdirectory(neon::State* state, neon::Arguments* args)
{
    neon::String* file;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    file = args->argv[0].asString();
    return neon::Value::makeBool(nn_util_fsfileisdirectory(state, file->data()));
}

neon::Value nn_memberfunc_file_close(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    nn_fileobject_close(args->thisval.asFile());
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_file_open(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    nn_fileobject_open(args->thisval.asFile());
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_file_isopen(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    (void)state;
    file = args->thisval.asFile();
    return neon::Value::makeBool(file->m_isstd || file->m_isopen);
}

neon::Value nn_memberfunc_file_isclosed(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    (void)state;
    file = args->thisval.asFile();
    return neon::Value::makeBool(!file->m_isstd && !file->m_isopen);
}

neon::Value nn_memberfunc_file_read(neon::State* state, neon::Arguments* args)
{
    size_t readhowmuch;
    neon::File::IOResult res;
    neon::File* file;
    neon::String* os;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    readhowmuch = -1;
    if(args->argc == 1)
    {
        NEON_ARGS_CHECKTYPE(args, 0, isNumber);
        readhowmuch = (size_t)args->argv[0].asNumber();
    }
    file = args->thisval.asFile();
    if(!file->readChunk(readhowmuch, &res))
    {
        FILE_ERROR(NotFound, strerror(errno));
    }
    os = neon::String::take(state, res.data, res.length);
    return neon::Value::fromObject(os);
}

neon::Value nn_memberfunc_file_get(neon::State* state, neon::Arguments* args)
{
    int ch;
    neon::File* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = args->thisval.asFile();
    ch = fgetc(file->m_filehandle);
    if(ch == EOF)
    {
        return neon::Value::makeNull();
    }
    return neon::Value::makeNumber(ch);
}

neon::Value nn_memberfunc_file_gets(neon::State* state, neon::Arguments* args)
{
    long end;
    long length;
    long currentpos;
    size_t bytesread;
    char* buffer;
    neon::File* file;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    length = -1;
    if(args->argc == 1)
    {
        NEON_ARGS_CHECKTYPE(args, 0, isNumber);
        length = (size_t)args->argv[0].asNumber();
    }
    file = args->thisval.asFile();
    if(!file->m_isstd)
    {
        if(!nn_util_fsfileexists(state, file->m_filepath->data()))
        {
            FILE_ERROR(NotFound, "no such file or directory");
        }
        else if(strstr(file->m_filemode->data(), "w") != nullptr && strstr(file->m_filemode->data(), "+") == nullptr)
        {
            FILE_ERROR(Unsupported, "cannot read file in write mode");
        }
        if(!file->m_isopen)
        {
            FILE_ERROR(Read, "file not open");
        }
        else if(file->m_filehandle == nullptr)
        {
            FILE_ERROR(Read, "could not read file");
        }
        if(length == -1)
        {
            currentpos = ftell(file->m_filehandle);
            fseek(file->m_filehandle, 0L, SEEK_END);
            end = ftell(file->m_filehandle);
            fseek(file->m_filehandle, currentpos, SEEK_SET);
            length = end - currentpos;
        }
    }
    else
    {
        if(fileno(stdout) == file->m_filedesc || fileno(stderr) == file->m_filedesc)
        {
            FILE_ERROR(Unsupported, "cannot read from output file");
        }
        /*
        // for non-file objects such as stdin
        // minimum read bytes should be 1
        */
        if(length == -1)
        {
            length = 1;
        }
    }
    buffer = (char*)neon::State::GC::allocate(state, sizeof(char), length + 1);
    if(buffer == nullptr && length != 0)
    {
        FILE_ERROR(Buffer, "not enough memory to read file");
    }
    bytesread = fread(buffer, sizeof(char), length, file->m_filehandle);
    if(bytesread == 0 && length != 0)
    {
        FILE_ERROR(Read, "could not read file contents");
    }
    if(buffer != nullptr)
    {
        buffer[bytesread] = '\0';
    }
    return neon::Value::fromObject(neon::String::take(state, buffer, bytesread));
}

neon::Value nn_memberfunc_file_write(neon::State* state, neon::Arguments* args)
{
    size_t count;
    int length;
    unsigned char* data;
    neon::File* file;
    neon::String* string;
    NEON_ARGS_CHECKCOUNT(args, 1);
    file = args->thisval.asFile();
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->argv[0].asString();
    data = (unsigned char*)string->data();
    length = string->length();
    if(!file->m_isstd)
    {
        if(strstr(file->m_filemode->data(), "r") != nullptr && strstr(file->m_filemode->data(), "+") == nullptr)
        {
            FILE_ERROR(Unsupported, "cannot write into non-writable file");
        }
        else if(length == 0)
        {
            FILE_ERROR(Write, "cannot write empty buffer to file");
        }
        else if(file->m_filehandle == nullptr || !file->m_isopen)
        {
            nn_fileobject_open(file);
        }
        else if(file->m_filehandle == nullptr)
        {
            FILE_ERROR(Write, "could not write to file");
        }
    }
    else
    {
        if(fileno(stdin) == file->m_filedesc)
        {
            FILE_ERROR(Unsupported, "cannot write to input file");
        }
    }
    count = fwrite(data, sizeof(unsigned char), length, file->m_filehandle);
    fflush(file->m_filehandle);
    if(count > (size_t)0)
    {
        return neon::Value::makeBool(true);
    }
    return neon::Value::makeBool(false);
}

neon::Value nn_memberfunc_file_puts(neon::State* state, neon::Arguments* args)
{
    size_t count;
    int length;
    unsigned char* data;
    neon::File* file;
    neon::String* string;
    NEON_ARGS_CHECKCOUNT(args, 1);
    file = args->thisval.asFile();
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->argv[0].asString();
    data = (unsigned char*)string->data();
    length = string->length();
    if(!file->m_isstd)
    {
        if(strstr(file->m_filemode->data(), "r") != nullptr && strstr(file->m_filemode->data(), "+") == nullptr)
        {
            FILE_ERROR(Unsupported, "cannot write into non-writable file");
        }
        else if(length == 0)
        {
            FILE_ERROR(Write, "cannot write empty buffer to file");
        }
        else if(!file->m_isopen)
        {
            FILE_ERROR(Write, "file not open");
        }
        else if(file->m_filehandle == nullptr)
        {
            FILE_ERROR(Write, "could not write to file");
        }
    }
    else
    {
        if(fileno(stdin) == file->m_filedesc)
        {
            FILE_ERROR(Unsupported, "cannot write to input file");
        }
    }
    count = fwrite(data, sizeof(unsigned char), length, file->m_filehandle);
    if(count > (size_t)0 || length == 0)
    {
        return neon::Value::makeBool(true);
    }
    return neon::Value::makeBool(false);
}

neon::Value nn_memberfunc_file_printf(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    neon::String* ofmt;
    file = args->thisval.asFile();
    NEON_ARGS_CHECKMINARG(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    ofmt = args->argv[0].asString();
    neon::Printer pd(state, file->m_filehandle, false);
    neon::FormatInfo nfi(state, &pd, ofmt->cstr(), ofmt->length());
    if(!nfi.format(args->argc, 1, args->argv))
    {
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_file_number(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return neon::Value::makeNumber(args->thisval.asFile()->m_filedesc);
}

neon::Value nn_memberfunc_file_istty(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = args->thisval.asFile();
    return neon::Value::makeBool(file->m_istty);
}

neon::Value nn_memberfunc_file_flush(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = args->thisval.asFile();
    if(!file->m_isopen)
    {
        FILE_ERROR(Unsupported, "I/O operation on closed file");
    }
    #if defined(NEON_PLAT_ISLINUX)
    if(fileno(stdin) == file->m_filedesc)
    {
        while((getchar()) != '\n')
        {
        }
    }
    else
    {
        fflush(file->m_filehandle);
    }
    #else
    fflush(file->m_filehandle);
    #endif
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_file_stats(neon::State* state, neon::Arguments* args)
{
    struct stat stats;
    neon::File* file;
    neon::Dictionary* dict;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = args->thisval.asFile();
    dict = state->gcProtect(neon::Dictionary::make(state));
    if(!file->m_isstd)
    {
        if(nn_util_fsfileexists(state, file->m_filepath->data()))
        {
            if(osfn_lstat(file->m_filepath->data(), &stats) == 0)
            {
                #if !defined(NEON_PLAT_ISWINDOWS)
                dict->addEntryCStr("isreadable", neon::Value::makeBool(((stats.st_mode & S_IRUSR) != 0)));
                dict->addEntryCStr("iswritable", neon::Value::makeBool(((stats.st_mode & S_IWUSR) != 0)));
                dict->addEntryCStr("isexecutable", neon::Value::makeBool(((stats.st_mode & S_IXUSR) != 0)));
                dict->addEntryCStr("issymbolic", neon::Value::makeBool((S_ISLNK(stats.st_mode) != 0)));
                #else
                dict->addEntryCStr("isreadable", neon::Value::makeBool(((stats.st_mode & S_IREAD) != 0)));
                dict->addEntryCStr("iswritable", neon::Value::makeBool(((stats.st_mode & S_IWRITE) != 0)));
                dict->addEntryCStr("isexecutable", neon::Value::makeBool(((stats.st_mode & S_IEXEC) != 0)));
                dict->addEntryCStr("issymbolic", neon::Value::makeBool(false));
                #endif
                dict->addEntryCStr("size", neon::Value::makeNumber(stats.st_size));
                dict->addEntryCStr("mode", neon::Value::makeNumber(stats.st_mode));
                dict->addEntryCStr("dev", neon::Value::makeNumber(stats.st_dev));
                dict->addEntryCStr("ino", neon::Value::makeNumber(stats.st_ino));
                dict->addEntryCStr("nlink", neon::Value::makeNumber(stats.st_nlink));
                dict->addEntryCStr("uid", neon::Value::makeNumber(stats.st_uid));
                dict->addEntryCStr("gid", neon::Value::makeNumber(stats.st_gid));
                dict->addEntryCStr("mtime", neon::Value::makeNumber(stats.st_mtime));
                dict->addEntryCStr("atime", neon::Value::makeNumber(stats.st_atime));
                dict->addEntryCStr("ctime", neon::Value::makeNumber(stats.st_ctime));
                dict->addEntryCStr("blocks", neon::Value::makeNumber(0));
                dict->addEntryCStr("blksize", neon::Value::makeNumber(0));
            }
        }
        else
        {
            return state->raiseFromFunction(args, "cannot get stats for non-existing file");
        }
    }
    else
    {
        if(fileno(stdin) == file->m_filedesc)
        {
            dict->addEntryCStr("isreadable", neon::Value::makeBool(true));
            dict->addEntryCStr("iswritable", neon::Value::makeBool(false));
        }
        else
        {
            dict->addEntryCStr("isreadable", neon::Value::makeBool(false));
            dict->addEntryCStr("iswritable", neon::Value::makeBool(true));
        }
        dict->addEntryCStr("isexecutable", neon::Value::makeBool(false));
        dict->addEntryCStr("size", neon::Value::makeNumber(1));
    }
    return neon::Value::fromObject(dict);
}

neon::Value nn_memberfunc_file_path(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = args->thisval.asFile();
    DENY_STD();
    return neon::Value::fromObject(file->m_filepath);
}

neon::Value nn_memberfunc_file_mode(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = args->thisval.asFile();
    return neon::Value::fromObject(file->m_filemode);
}

neon::Value nn_memberfunc_file_name(neon::State* state, neon::Arguments* args)
{
    char* name;
    neon::File* file;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = args->thisval.asFile();
    if(!file->m_isstd)
    {
        name = nn_util_fsgetbasename(state, file->m_filepath->data());
        return neon::Value::fromObject(neon::String::copy(state, name));
    }
    else if(file->m_istty)
    {
        /*name = ttyname(file->m_filedesc);*/
        name = neon::Util::strCopy(state, "<tty>");
        if(name)
        {
            return neon::Value::fromObject(neon::String::copy(state, name));
        }
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_file_seek(neon::State* state, neon::Arguments* args)
{
    long position;
    int seektype;
    neon::File* file;
    NEON_ARGS_CHECKCOUNT(args, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    NEON_ARGS_CHECKTYPE(args, 1, isNumber);
    file = args->thisval.asFile();
    DENY_STD();
    position = (long)args->argv[0].asNumber();
    seektype = args->argv[1].asNumber();
    RETURN_STATUS(fseek(file->m_filehandle, position, seektype));
}

neon::Value nn_memberfunc_file_tell(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = args->thisval.asFile();
    DENY_STD();
    return neon::Value::makeNumber(ftell(file->m_filehandle));
}

#undef FILE_ERROR
#undef RETURN_STATUS
#undef DENY_STD

neon::Value nn_memberfunc_array_length(neon::State* state, neon::Arguments* args)
{
    neon::Array* selfarr;
    (void)state;
    selfarr = args->thisval.asArray();
    return neon::Value::makeNumber(selfarr->size());
}

neon::Value nn_memberfunc_array_append(neon::State* state, neon::Arguments* args)
{
    int i;
    (void)state;
    for(i = 0; i < args->argc; i++)
    {
        args->thisval.asArray()->push(args->argv[i]);
    }
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_array_clear(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    args->thisval.asArray()->clear();
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_array_clone(neon::State* state, neon::Arguments* args)
{
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    return neon::Value::fromObject(list->copy(0, list->size()));
}

neon::Value nn_memberfunc_array_count(neon::State* state, neon::Arguments* args)
{
    size_t i;
    size_t count;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    count = 0;
    for(i = 0; i < list->size(); i++)
    {
        if(list->at(i).compare(state, args->argv[0]))
        {
            count++;
        }
    }
    return neon::Value::makeNumber(count);
}

neon::Value nn_memberfunc_array_extend(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Array* list;
    neon::Array* list2;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isArray);
    list = args->thisval.asArray();
    list2 = args->argv[0].asArray();
    for(i = 0; i < list2->size(); i++)
    {
        list->push(list2->at(i));
    }
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_array_indexof(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    list = args->thisval.asArray();
    i = 0;
    if(args->argc == 2)
    {
        NEON_ARGS_CHECKTYPE(args, 1, isNumber);
        i = args->argv[1].asNumber();
    }
    for(; i < list->size(); i++)
    {
        if(list->at(i).compare(state, args->argv[0]))
        {
            return neon::Value::makeNumber(i);
        }
    }
    return neon::Value::makeNumber(-1);
}

neon::Value nn_memberfunc_array_insert(neon::State* state, neon::Arguments* args)
{
    size_t index;
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 2);
    NEON_ARGS_CHECKTYPE(args, 1, isNumber);
    list = args->thisval.asArray();
    index = args->argv[1].asNumber();
    list->m_varray->insert(args->argv[0], index);
    return neon::Value::makeEmpty();
}


neon::Value nn_memberfunc_array_pop(neon::State* state, neon::Arguments* args)
{
    neon::Value value;
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    if(list->size() > 0)
    {
        value = list->at(list->size() - 1);
        list->m_varray->m_count--;
        return value;
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_array_shift(neon::State* state, neon::Arguments* args)
{
    size_t i;
    size_t j;
    size_t count;
    neon::Array* list;
    neon::Array* newlist;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    count = 1;
    if(args->argc == 1)
    {
        NEON_ARGS_CHECKTYPE(args, 0, isNumber);
        count = args->argv[0].asNumber();
    }
    list = args->thisval.asArray();
    if(count >= list->size() || list->size() == 1)
    {
        list->m_varray->m_count = 0;
        return neon::Value::makeNull();
    }
    else if(count > 0)
    {
        newlist = state->gcProtect(neon::Array::make(state));
        for(i = 0; i < count; i++)
        {
            newlist->push(list->at(0));
            for(j = 0; j < list->size(); j++)
            {
                list->m_varray->m_values[j] = list->at(j + 1);
            }
            list->m_varray->m_count -= 1;
        }
        if(count == 1)
        {
            return newlist->at(0);
        }
        else
        {
            return neon::Value::fromObject(newlist);
        }
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_array_removeat(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int index;
    neon::Value value;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    list = args->thisval.asArray();
    index = args->argv[0].asNumber();
    if(index < 0 || index >= int(list->size()))
    {
        return state->raiseFromFunction(args, "list index %d out of range at remove_at()", index);
    }
    value = list->at(index);
    for(i = index; i < list->size() - 1; i++)
    {
        list->m_varray->m_values[i] = list->at(i + 1);
    }
    list->m_varray->m_count--;
    return value;
}

neon::Value nn_memberfunc_array_remove(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int index;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    index = -1;
    for(i = 0; i < list->size(); i++)
    {
        if(list->at(i).compare(state, args->argv[0]))
        {
            index = i;
            break;
        }
    }
    if(index != -1)
    {
        for(i = index; i < list->size(); i++)
        {
            list->m_varray->m_values[i] = list->at(i + 1);
        }
        list->m_varray->m_count--;
    }
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_array_reverse(neon::State* state, neon::Arguments* args)
{
    int fromtop;
    neon::Array* list;
    neon::Array* nlist;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    nlist = state->gcProtect(neon::Array::make(state));
    /* in-place reversal:*/
    /*
    int start = 0;
    int end = list->size() - 1;
    while(start < end)
    {
        neon::Value temp = list->at(start);
        list->m_varray->m_values[start] = list->at(end);
        list->m_varray->m_values[end] = temp;
        start++;
        end--;
    }
    */
    for(fromtop = list->size() - 1; fromtop >= 0; fromtop--)
    {
        nlist->push(list->at(fromtop));
    }
    return neon::Value::fromObject(nlist);
}


neon::Value nn_memberfunc_array_sort(neon::State* state, neon::Arguments* args)
{
    neon::Array* list;
    neon::ValArray::QuickSort qs(state);
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    qs.runSort(list->m_varray, 0, list->size()-0);
    return neon::Value::fromObject(list);
}

neon::Value nn_memberfunc_array_contains(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    for(i = 0; i < list->size(); i++)
    {
        if(args->argv[0].compare(state, list->at(i)))
        {
            return neon::Value::makeBool(true);
        }
    }
    return neon::Value::makeBool(false);
}

neon::Value nn_memberfunc_array_delete(neon::State* state, neon::Arguments* args)
{
    int i;
    int idxupper;
    int idxlower;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    idxlower = args->argv[0].asNumber();
    idxupper = idxlower;
    if(args->argc == 2)
    {
        NEON_ARGS_CHECKTYPE(args, 1, isNumber);
        idxupper = args->argv[1].asNumber();
    }
    list = args->thisval.asArray();
    if(idxlower < 0 || idxlower >= int(list->size()))
    {
        return state->raiseFromFunction(args, "list index %d out of range at delete()", idxlower);
    }
    else if(idxupper < idxlower || idxupper >= int(list->size()))
    {
        return state->raiseFromFunction(args, "invalid upper limit %d at delete()", idxupper);
    }
    for(i = 0; i < int(list->size() - idxupper); i++)
    {
        list->m_varray->m_values[idxlower + i] = list->at(i + idxupper + 1);
    }
    list->m_varray->m_count -= idxupper - idxlower + 1;
    return neon::Value::makeNumber((double)idxupper - (double)idxlower + 1);
}

neon::Value nn_memberfunc_array_first(neon::State* state, neon::Arguments* args)
{
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    if(list->size() > 0)
    {
        return list->at(0);
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_array_last(neon::State* state, neon::Arguments* args)
{
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    if(list->size() > 0)
    {
        return list->at(list->size() - 1);
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_array_isempty(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return neon::Value::makeBool(args->thisval.asArray()->size() == 0);
}

neon::Value nn_memberfunc_array_take(neon::State* state, neon::Arguments* args)
{
    int count;
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    list = args->thisval.asArray();
    count = args->argv[0].asNumber();
    if(count < 0)
    {
        count = list->count() + count;
    }
    if(int(list->count()) < count)
    {
        return neon::Value::fromObject(list->copy(0, list->count()));
    }
    return neon::Value::fromObject(list->copy(0, count));
}

neon::Value nn_memberfunc_array_get(neon::State* state, neon::Arguments* args)
{
    int index;
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    list = args->thisval.asArray();
    index = args->argv[0].asNumber();
    if(index < 0 || index >= int(list->count()))
    {
        return neon::Value::makeNull();
    }
    return list->at(index);
}

neon::Value nn_memberfunc_array_compact(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Array* list;
    neon::Array* newlist;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    newlist = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < list->count(); i++)
    {
        if(!list->at(i).compare(state, neon::Value::makeNull()))
        {
            newlist->push(list->at(i));
        }
    }
    return neon::Value::fromObject(newlist);
}

neon::Value nn_memberfunc_array_unique(neon::State* state, neon::Arguments* args)
{
    size_t i;
    size_t j;
    bool found;
    neon::Array* list;
    neon::Array* newlist;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    newlist = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < list->count(); i++)
    {
        found = false;
        for(j = 0; j < newlist->count(); j++)
        {
            if(newlist->at(j).compare(state, list->at(i)))
            {
                found = true;
                continue;
            }
        }
        if(!found)
        {
            newlist->push(list->at(i));
        }
    }
    return neon::Value::fromObject(newlist);
}

neon::Value nn_memberfunc_array_zip(neon::State* state, neon::Arguments* args)
{
    size_t i;
    size_t j;
    neon::Array* list;
    neon::Array* newlist;
    neon::Array* alist;
    neon::Array** arglist;
    list = args->thisval.asArray();
    newlist = state->gcProtect(neon::Array::make(state));
    arglist = (neon::Array**)neon::State::GC::allocate(state, sizeof(neon::Array*), args->argc);
    for(i = 0; i < size_t(args->argc); i++)
    {
        NEON_ARGS_CHECKTYPE(args, i, isArray);
        arglist[i] = args->argv[i].asArray();
    }
    for(i = 0; i < list->count(); i++)
    {
        alist = state->gcProtect(neon::Array::make(state));
        /* item of main list*/
        alist->push(list->at(i));
        for(j = 0; j < size_t(args->argc); j++)
        {
            if(i < arglist[j]->count())
            {
                alist->push(arglist[j]->at(i));
            }
            else
            {
                alist->push(neon::Value::makeNull());
            }
        }
        newlist->push(neon::Value::fromObject(alist));
    }
    return neon::Value::fromObject(newlist);
}


neon::Value nn_memberfunc_array_zipfrom(neon::State* state, neon::Arguments* args)
{
    size_t i;
    size_t j;
    neon::Array* list;
    neon::Array* newlist;
    neon::Array* alist;
    neon::Array* arglist;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isArray);
    list = args->thisval.asArray();
    newlist = state->gcProtect(neon::Array::make(state));
    arglist = args->argv[0].asArray();
    for(i = 0; i < arglist->count(); i++)
    {
        if(!arglist->at(i).isArray())
        {
            return state->raiseFromFunction(args, "invalid list in zip entries");
        }
    }
    for(i = 0; i < list->count(); i++)
    {
        alist = state->gcProtect(neon::Array::make(state));
        alist->push(list->at(i));
        for(j = 0; j < arglist->count(); j++)
        {
            if(i < arglist->at(j).asArray()->count())
            {
                alist->push(arglist->at(j).asArray()->at(i));
            }
            else
            {
                alist->push(neon::Value::makeNull());
            }
        }
        newlist->push(neon::Value::fromObject(alist));
    }
    return neon::Value::fromObject(newlist);
}

neon::Value nn_memberfunc_array_todict(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Dictionary* dict;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = state->gcProtect(neon::Dictionary::make(state));
    list = args->thisval.asArray();
    for(i = 0; i < list->count(); i++)
    {
        dict->setEntry(neon::Value::makeNumber(i), list->at(i));
    }
    return neon::Value::fromObject(dict);
}

neon::Value nn_memberfunc_array_iter(neon::State* state, neon::Arguments* args)
{
    int index;
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    list = args->thisval.asArray();
    index = args->argv[0].asNumber();
    if(index > -1 && index < int(list->count()))
    {
        return list->at(index);
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_array_itern(neon::State* state, neon::Arguments* args)
{
    int index;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    if(args->argv[0].isNull())
    {
        if(list->count() == 0)
        {
            return neon::Value::makeBool(false);
        }
        return neon::Value::makeNumber(0);
    }
    if(!args->argv[0].isNumber())
    {
        return state->raiseFromFunction(args, "lists are numerically indexed");
    }
    index = args->argv[0].asNumber();
    if(index < int(list->count() - 1))
    {
        return neon::Value::makeNumber((double)index + 1);
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_array_each(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    neon::Value callable;
    neon::Value unused;
    neon::Array* list;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    list = args->thisval.asArray();
    callable = args->argv[0];
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < list->count(); i++)
    {
        if(arity > 0)
        {
            nestargs->m_varray->m_values[0] = list->at(i);
            if(arity > 1)
            {
                nestargs->m_varray->m_values[1] = neon::Value::makeNumber(i);
            }
        }
        nc.call(callable, args->thisval, nestargs, &unused);
    }
    state->stackPop();
    return neon::Value::makeEmpty();
}


neon::Value nn_memberfunc_array_map(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    neon::Value res;
    neon::Value callable;
    neon::Array* list;
    neon::Array* nestargs;
    neon::Array* resultlist;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    list = args->thisval.asArray();
    callable = args->argv[0];
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    resultlist = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < list->count(); i++)
    {
        if(!list->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs->m_varray->m_values[0] = list->at(i);
                if(arity > 1)
                {
                    nestargs->m_varray->m_values[1] = neon::Value::makeNumber(i);
                }
            }
            nc.call(callable, args->thisval, nestargs, &res);
            resultlist->push(res);
        }
        else
        {
            resultlist->push(neon::Value::makeEmpty());
        }
    }
    state->stackPop();
    return neon::Value::fromObject(resultlist);
}


neon::Value nn_memberfunc_array_filter(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    neon::Value callable;
    neon::Value result;
    neon::Array* list;
    neon::Array* nestargs;
    neon::Array* resultlist;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    list = args->thisval.asArray();
    callable = args->argv[0];
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    resultlist = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < list->count(); i++)
    {
        if(!list->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs->m_varray->m_values[0] = list->at(i);
                if(arity > 1)
                {
                    nestargs->m_varray->m_values[1] = neon::Value::makeNumber(i);
                }
            }
            nc.call(callable, args->thisval, nestargs, &result);
            if(!neon::Value::isFalse(result))
            {
                resultlist->push(list->at(i));
            }
        }
    }
    state->stackPop();
    return neon::Value::fromObject(resultlist);
}

neon::Value nn_memberfunc_array_some(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    neon::Value callable;
    neon::Value result;
    neon::Array* list;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    list = args->thisval.asArray();
    callable = args->argv[0];
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < list->count(); i++)
    {
        if(!list->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs->m_varray->m_values[0] = list->at(i);
                if(arity > 1)
                {
                    nestargs->m_varray->m_values[1] = neon::Value::makeNumber(i);
                }
            }
            nc.call(callable, args->thisval, nestargs, &result);
            if(!neon::Value::isFalse(result))
            {
                state->stackPop();
                return neon::Value::makeBool(true);
            }
        }
    }
    state->stackPop();
    return neon::Value::makeBool(false);
}


neon::Value nn_memberfunc_array_every(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    neon::Value result;
    neon::Value callable;
    neon::Array* list;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    list = args->thisval.asArray();
    callable = args->argv[0];
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < list->count(); i++)
    {
        if(!list->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs->m_varray->m_values[0] = list->at(i);
                if(arity > 1)
                {
                    nestargs->m_varray->m_values[1] = neon::Value::makeNumber(i);
                }
            }
            nc.call(callable, args->thisval, nestargs, &result);
            if(neon::Value::isFalse(result))
            {
                state->stackPop();
                return neon::Value::makeBool(false);
            }
        }
    }
    state->stackPop();
    return neon::Value::makeBool(true);
}

neon::Value nn_memberfunc_array_reduce(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    int startindex;
    neon::Value callable;
    neon::Value accumulator;
    neon::Array* list;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    list = args->thisval.asArray();
    callable = args->argv[0];
    startindex = 0;
    accumulator = neon::Value::makeNull();
    if(args->argc == 2)
    {
        accumulator = args->argv[1];
    }
    if(accumulator.isNull() && list->count() > 0)
    {
        accumulator = list->at(0);
        startindex = 1;
    }
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = startindex; i < list->count(); i++)
    {
        if(!list->at(i).isNull() && !list->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs->m_varray->m_values[0] = accumulator;
                if(arity > 1)
                {
                    nestargs->m_varray->m_values[1] = list->at(i);
                    if(arity > 2)
                    {
                        nestargs->m_varray->m_values[2] = neon::Value::makeNumber(i);
                        if(arity > 4)
                        {
                            nestargs->m_varray->m_values[3] = args->thisval;
                        }
                    }
                }
            }
            nc.call(callable, args->thisval, nestargs, &accumulator);
        }
    }
    state->stackPop();
    return accumulator;
}

neon::Value nn_memberfunc_range_lower(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return neon::Value::makeNumber(args->thisval.asRange()->m_lower);
}

neon::Value nn_memberfunc_range_upper(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return neon::Value::makeNumber(args->thisval.asRange()->m_upper);
}

neon::Value nn_memberfunc_range_range(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return neon::Value::makeNumber(args->thisval.asRange()->m_range);
}

neon::Value nn_memberfunc_range_iter(neon::State* state, neon::Arguments* args)
{
    int val;
    int index;
    neon::Range* range;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    range = args->thisval.asRange();
    index = args->argv[0].asNumber();
    if(index >= 0 && index < range->m_range)
    {
        if(index == 0)
        {
            return neon::Value::makeNumber(range->m_lower);
        }
        if(range->m_lower > range->m_upper)
        {
            val = --range->m_lower;
        }
        else
        {
            val = ++range->m_lower;
        }
        return neon::Value::makeNumber(val);
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_range_itern(neon::State* state, neon::Arguments* args)
{
    int index;
    neon::Range* range;
    NEON_ARGS_CHECKCOUNT(args, 1);
    range = args->thisval.asRange();
    if(args->argv[0].isNull())
    {
        if(range->m_range == 0)
        {
            return neon::Value::makeNull();
        }
        return neon::Value::makeNumber(0);
    }
    if(!args->argv[0].isNumber())
    {
        return state->raiseFromFunction(args, "ranges are numerically indexed");
    }
    index = (int)args->argv[0].asNumber() + 1;
    if(index < range->m_range)
    {
        return neon::Value::makeNumber(index);
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_range_loop(neon::State* state, neon::Arguments* args)
{
    int i;
    int arity;
    neon::Value callable;
    neon::Value unused;
    neon::Range* range;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    range = args->thisval.asRange();
    callable = args->argv[0];
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < range->m_range; i++)
    {
        if(arity > 0)
        {
            nestargs->m_varray->m_values[0] = neon::Value::makeNumber(i);
            if(arity > 1)
            {
                nestargs->m_varray->m_values[1] = neon::Value::makeNumber(i);
            }
        }
        nc.call(callable, args->thisval, nestargs, &unused);
    }
    state->stackPop();
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_range_expand(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::Value val;
    neon::Range* range;
    neon::Array* oa;
    range = args->thisval.asRange();
    oa = neon::Array::make(state);
    for(i = 0; i < range->m_range; i++)
    {
        val = neon::Value::makeNumber(i);
        oa->push(val);
    }
    return neon::Value::fromObject(oa);
}

neon::Value nn_memberfunc_range_constructor(neon::State* state, neon::Arguments* args)
{
    int a;
    int b;
    neon::Range* orng;
    a = args->argv[0].asNumber();
    b = args->argv[1].asNumber();
    orng = neon::Range::make(state, a, b);
    return neon::Value::fromObject(orng);
}

neon::Value nn_memberfunc_string_utf8numbytes(neon::State* state, neon::Arguments* args)
{
    int incode;
    int res;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    incode = args->argv[0].asNumber();
    //static int utf8NumBytes(int value)
    res = neon::Util::utf8NumBytes(incode);
    return neon::Value::makeNumber(res);
}

neon::Value nn_memberfunc_string_utf8decode(neon::State* state, neon::Arguments* args)
{
    int res;
    neon::String* instr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    instr = args->argv[0].asString();
    //static int utf8Decode(const uint8_t* bytes, uint32_t length)
    res = neon::Util::utf8Decode((const uint8_t*)instr->data(), instr->length());
    return neon::Value::makeNumber(res);
}

neon::Value nn_memberfunc_string_utf8encode(neon::State* state, neon::Arguments* args)
{
    int incode;
    size_t len;
    neon::String* res;
    char* buf;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    incode = args->argv[0].asNumber();
    //static char* utf8Encode(unsigned int code)
    buf = neon::Util::utf8Encode(state, incode, &len);
    res = neon::String::take(state, buf, len);
    return neon::Value::fromObject(res);
}

neon::Value nn_util_stringutf8chars(neon::State* state, neon::Arguments* args, bool onlycodepoint)
{
    int cp;
    const char* cstr;
    neon::Array* res;
    neon::String* os;
    neon::String* instr;
    (void)state;
    instr = args->thisval.asString();
    res = neon::Array::make(state);
    neon::Util::Utf8Iterator iter(instr->data(), instr->length());
    while(iter.next())
    {
        cp = iter.m_codepoint;
        cstr = iter.getCharStr();
        if(onlycodepoint)
        {
            res->push(neon::Value::makeNumber(cp));
        }
        else
        {
            os = neon::String::copy(state, cstr, iter.m_charsize);
            res->push(neon::Value::fromObject(os));
        }
    }
    return neon::Value::fromObject(res);
}

neon::Value nn_memberfunc_string_utf8chars(neon::State* state, neon::Arguments* args)
{
    return nn_util_stringutf8chars(state, args, false);
}

neon::Value nn_memberfunc_string_utf8codepoints(neon::State* state, neon::Arguments* args)
{
    return nn_util_stringutf8chars(state, args, true);
}

neon::Value nn_memberfunc_string_fromcharcode(neon::State* state, neon::Arguments* args)
{
    char ch;
    neon::String* os;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    ch = args->argv[0].asNumber();
    os = neon::String::copy(state, &ch, 1);
    return neon::Value::fromObject(os);
}

neon::Value nn_memberfunc_string_constructor(neon::State* state, neon::Arguments* args)
{
    neon::String* os;
    NEON_ARGS_CHECKCOUNT(args, 0);
    os = neon::String::copy(state, "", 0);
    return neon::Value::fromObject(os);
}

neon::Value nn_memberfunc_string_length(neon::State* state, neon::Arguments* args)
{
    neon::String* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    return neon::Value::makeNumber(selfstr->length());
}

neon::Value nn_memberfunc_string_substring(neon::State* state, neon::Arguments* args)
{
    size_t end;
    size_t start;
    size_t maxlen;
    neon::String* nos;
    neon::String* selfstr;
    (void)state;
    selfstr = args->thisval.asString();
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    maxlen = selfstr->length();
    end = maxlen;
    start = args->argv[0].asNumber();
    if(args->argc > 1)
    {
        NEON_ARGS_CHECKTYPE(args, 1, isNumber);
        end = args->argv[1].asNumber();
    }
    nos = selfstr->substr(start, end, true);
    return neon::Value::fromObject(nos);
}

neon::Value nn_memberfunc_string_charcodeat(neon::State* state, neon::Arguments* args)
{
    int ch;
    int idx;
    int selflen;
    neon::String* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    selfstr = args->thisval.asString();
    idx = args->argv[0].asNumber();
    selflen = (int)selfstr->length();
    if((idx < 0) || (idx >= selflen))
    {
        ch = -1;
    }
    else
    {
        ch = selfstr->data()[idx];
    }
    return neon::Value::makeNumber(ch);
}

neon::Value nn_memberfunc_string_charat(neon::State* state, neon::Arguments* args)
{
    char ch;
    int idx;
    int selflen;
    neon::String* selfstr;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    selfstr = args->thisval.asString();
    idx = args->argv[0].asNumber();
    selflen = (int)selfstr->length();
    if((idx < 0) || (idx >= selflen))
    {
        return neon::Value::fromObject(neon::String::copy(state, "", 0));
    }
    else
    {
        ch = selfstr->data()[idx];
    }
    return neon::Value::fromObject(neon::String::copy(state, &ch, 1));
}

neon::Value nn_memberfunc_string_upper(neon::State* state, neon::Arguments* args)
{
    size_t slen;
    neon::String* str;
    neon::String* copied;
    NEON_ARGS_CHECKCOUNT(args, 0);
    str = args->thisval.asString();
    copied = neon::String::copy(state, str->data(), str->length());
    slen = copied->length();
    (void)neon::Util::strToUpperInplace(copied->mutableData(), slen);
    return neon::Value::fromObject(copied);
}

neon::Value nn_memberfunc_string_lower(neon::State* state, neon::Arguments* args)
{
    size_t slen;
    neon::String* str;
    neon::String* copied;
    NEON_ARGS_CHECKCOUNT(args, 0);
    str = args->thisval.asString();
    copied = neon::String::copy(state, str->data(), str->length());
    slen = copied->length();
    (void)neon::Util::strToLowerInplace(copied->mutableData(), slen);
    return neon::Value::fromObject(copied);
}

neon::Value nn_memberfunc_string_isalpha(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::String* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    for(i = 0; i < (int)selfstr->length(); i++)
    {
        if(!isalpha((unsigned char)selfstr->data()[i]))
        {
            return neon::Value::makeBool(false);
        }
    }
    return neon::Value::makeBool(selfstr->length() != 0);
}

neon::Value nn_memberfunc_string_isalnum(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::String* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    for(i = 0; i < (int)selfstr->length(); i++)
    {
        if(!isalnum((unsigned char)selfstr->data()[i]))
        {
            return neon::Value::makeBool(false);
        }
    }
    return neon::Value::makeBool(selfstr->length() != 0);
}

neon::Value nn_memberfunc_string_isfloat(neon::State* state, neon::Arguments* args)
{
    double f;
    char* p;
    neon::String* selfstr;
    (void)f;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    errno = 0;
    if(selfstr->length() ==0)
    {
        return neon::Value::makeBool(false);
    }
    f = strtod(selfstr->data(), &p);
    if(errno)
    {
        return neon::Value::makeBool(false);
    }
    else
    {
        if(*p == 0)
        {
            return neon::Value::makeBool(true);
        }
    }
    return neon::Value::makeBool(false);
}

neon::Value nn_memberfunc_string_isnumber(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::String* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    for(i = 0; i < (int)selfstr->length(); i++)
    {
        if(!isdigit((unsigned char)selfstr->data()[i]))
        {
            return neon::Value::makeBool(false);
        }
    }
    return neon::Value::makeBool(selfstr->length() != 0);
}

neon::Value nn_memberfunc_string_islower(neon::State* state, neon::Arguments* args)
{
    int i;
    bool alphafound;
    neon::String* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    alphafound = false;
    for(i = 0; i < (int)selfstr->length(); i++)
    {
        if(!alphafound && !isdigit(selfstr->data()[0]))
        {
            alphafound = true;
        }
        if(isupper(selfstr->data()[0]))
        {
            return neon::Value::makeBool(false);
        }
    }
    return neon::Value::makeBool(alphafound);
}

neon::Value nn_memberfunc_string_isupper(neon::State* state, neon::Arguments* args)
{
    int i;
    bool alphafound;
    neon::String* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    alphafound = false;
    for(i = 0; i < (int)selfstr->length(); i++)
    {
        if(!alphafound && !isdigit(selfstr->data()[0]))
        {
            alphafound = true;
        }
        if(islower(selfstr->data()[0]))
        {
            return neon::Value::makeBool(false);
        }
    }
    return neon::Value::makeBool(alphafound);
}

neon::Value nn_memberfunc_string_isspace(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::String* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    for(i = 0; i < (int)selfstr->length(); i++)
    {
        if(!isspace((unsigned char)selfstr->data()[i]))
        {
            return neon::Value::makeBool(false);
        }
    }
    return neon::Value::makeBool(selfstr->length() != 0);
}

neon::Value nn_memberfunc_string_trim(neon::State* state, neon::Arguments* args)
{
    char trimmer;
    char* end;
    char* string;
    neon::String* selfstr;
    neon::String* copied;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    trimmer = '\0';
    if(args->argc == 1)
    {
        trimmer = (char)args->argv[0].asString()->data()[0];
    }
    selfstr = args->thisval.asString();
    copied = neon::String::copy(state, selfstr->data(), selfstr->length());
    string = copied->mutableData();
    end = nullptr;
    /* Trim leading space*/
    if(trimmer == '\0')
    {
        while(isspace((unsigned char)*string))
        {
            string++;
        }
    }
    else
    {
        while(trimmer == *string)
        {
            string++;
        }
    }
    /* All spaces? */
    if(*string == 0)
    {
        return neon::Value::fromObject(neon::String::copy(state, "", 0));
    }
    /* Trim trailing space */
    end = string + strlen(string) - 1;
    if(trimmer == '\0')
    {
        while(end > string && isspace((unsigned char)*end))
        {
            end--;
        }
    }
    else
    {
        while(end > string && trimmer == *end)
        {
            end--;
        }
    }
    end[1] = '\0';
    return neon::Value::fromObject(neon::String::copy(state, string));
}

neon::Value nn_memberfunc_string_ltrim(neon::State* state, neon::Arguments* args)
{
    char trimmer;
    char* end;
    char* string;
    neon::String* selfstr;
    neon::String* copied;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    trimmer = '\0';
    if(args->argc == 1)
    {
        trimmer = (char)args->argv[0].asString()->data()[0];
    }
    selfstr = args->thisval.asString();
    copied = neon::String::copy(state, selfstr->data(), selfstr->length());
    string = copied->mutableData();
    end = nullptr;
    /* Trim leading space */
    if(trimmer == '\0')
    {
        while(isspace((unsigned char)*string))
        {
            string++;
        }
    }
    else
    {
        while(trimmer == *string)
        {
            string++;
        }
    }
    /* All spaces? */
    if(*string == 0)
    {
        return neon::Value::fromObject(neon::String::copy(state, "", 0));
    }
    end = string + strlen(string) - 1;
    end[1] = '\0';
    return neon::Value::fromObject(neon::String::copy(state, string));
}

neon::Value nn_memberfunc_string_rtrim(neon::State* state, neon::Arguments* args)
{
    char trimmer;
    char* end;
    char* string;
    neon::String* selfstr;
    neon::String* copied;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    trimmer = '\0';
    if(args->argc == 1)
    {
        trimmer = (char)args->argv[0].asString()->data()[0];
    }
    selfstr = args->thisval.asString();
    copied = neon::String::copy(state, selfstr->data(), selfstr->length());
    string = copied->mutableData();
    end = nullptr;
    /* All spaces? */
    if(*string == 0)
    {
        return neon::Value::fromObject(neon::String::copy(state, "", 0));
    }
    end = string + strlen(string) - 1;
    if(trimmer == '\0')
    {
        while(end > string && isspace((unsigned char)*end))
        {
            end--;
        }
    }
    else
    {
        while(end > string && trimmer == *end)
        {
            end--;
        }
    }
    /* Write new null terminator character */
    end[1] = '\0';
    return neon::Value::fromObject(neon::String::copy(state, string));
}


neon::Value nn_memberfunc_array_constructor(neon::State* state, neon::Arguments* args)
{
    int cnt;
    neon::Value filler;
    neon::Array* arr;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    filler = neon::Value::makeEmpty();
    if(args->argc > 1)
    {
        filler = args->argv[1];
    }
    cnt = args->argv[0].asNumber();
    arr = neon::Array::make(state, cnt, filler);
    return neon::Value::fromObject(arr);
}

neon::Value nn_memberfunc_array_join(neon::State* state, neon::Arguments* args)
{
    int i;
    int count;
    neon::Value vjoinee;
    neon::Array* selfarr;
    neon::String* joinee;
    neon::Value* list;
    selfarr = args->thisval.asArray();
    joinee = nullptr;
    if(args->argc > 0)
    {
        vjoinee = args->argv[0];
        if(vjoinee.isString())
        {
            joinee = vjoinee.asString();
        }
        else
        {
            joinee = neon::Value::toString(state, vjoinee);
        }
    }
    list = selfarr->m_varray->m_values;
    count = selfarr->count();
    if(count == 0)
    {
        return neon::Value::fromObject(neon::String::copy(state, ""));
    }
    neon::Printer pd(state);
    for(i = 0; i < count; i++)
    {
        pd.printValue(list[i], false, true);
        if((joinee != nullptr) && ((i+1) < count))
        {
            pd.put(joinee->data(), joinee->length());
        }
    }
    return neon::Value::fromObject(pd.takeString());
}

neon::Value nn_memberfunc_string_indexof(neon::State* state, neon::Arguments* args)
{
    int startindex;
    const char* result;
    const char* haystack;
    neon::String* string;
    neon::String* needle;
    (void)state;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->thisval.asString();
    needle = args->argv[0].asString();
    startindex = 0;
    if(args->argc == 2)
    {
        NEON_ARGS_CHECKTYPE(args, 1, isNumber);
        startindex = args->argv[1].asNumber();
    }
    if(string->length() > 0 && needle->length() > 0)
    {
        haystack = string->data();
        result = strstr(haystack + startindex, needle->data());
        if(result != nullptr)
        {
            return neon::Value::makeNumber((int)(result - haystack));
        }
    }
    return neon::Value::makeNumber(-1);
}

neon::Value nn_memberfunc_string_startswith(neon::State* state, neon::Arguments* args)
{
    neon::String* substr;
    neon::String* string;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->thisval.asString();
    substr = args->argv[0].asString();
    if(string->length() == 0 || substr->length() == 0 || substr->length() > string->length())
    {
        return neon::Value::makeBool(false);
    }
    return neon::Value::makeBool(memcmp(substr->data(), string->data(), substr->length()) == 0);
}

neon::Value nn_memberfunc_string_endswith(neon::State* state, neon::Arguments* args)
{
    int difference;
    neon::String* substr;
    neon::String* string;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->thisval.asString();
    substr = args->argv[0].asString();
    if(string->length() == 0 || substr->length() == 0 || substr->length() > string->length())
    {
        return neon::Value::makeBool(false);
    }
    difference = string->length() - substr->length();
    return neon::Value::makeBool(memcmp(substr->data(), string->data() + difference, substr->length()) == 0);
}

neon::Value nn_memberfunc_string_count(neon::State* state, neon::Arguments* args)
{
    int count;
    const char* tmp;
    neon::String* substr;
    neon::String* string;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->thisval.asString();
    substr = args->argv[0].asString();
    if(substr->length() == 0 || string->length() == 0)
    {
        return neon::Value::makeNumber(0);
    }
    count = 0;
    tmp = string->data();
    while((tmp = neon::Util::utf8Find(tmp, string->length(), substr->data(), substr->length())))
    {
        count++;
        tmp++;
    }
    return neon::Value::makeNumber(count);
}

neon::Value nn_memberfunc_string_tonumber(neon::State* state, neon::Arguments* args)
{
    neon::String* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    return neon::Value::makeNumber(strtod(selfstr->data(), nullptr));
}

neon::Value nn_memberfunc_string_isascii(neon::State* state, neon::Arguments* args)
{
    neon::String* string;
    (void)state;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    if(args->argc == 1)
    {
        NEON_ARGS_CHECKTYPE(args, 0, isBool);
    }
    string = args->thisval.asString();
    return neon::Value::fromObject(string);
}

neon::Value nn_memberfunc_string_tolist(neon::State* state, neon::Arguments* args)
{
    int i;
    int end;
    int start;
    int length;
    neon::Array* list;
    neon::String* string;
    NEON_ARGS_CHECKCOUNT(args, 0);
    string = args->thisval.asString();
    list = state->gcProtect(neon::Array::make(state));
    length = string->length();
    if(length > 0)
    {
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            list->push(neon::Value::fromObject(neon::String::copy(state, string->data() + start, (int)(end - start))));
        }
    }
    return neon::Value::fromObject(list);
}

// TODO: lpad and rpad modify m_sbuf members!!!
neon::Value nn_memberfunc_string_lpad(neon::State* state, neon::Arguments* args)
{
    int i;
    int width;
    int fillsize;
    int finalsize;
    int finalutf8size;
    char fillchar;
    char* str;
    char* fill;
    neon::String* ofillstr;
    neon::String* result;
    neon::String* string;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    string = args->thisval.asString();
    width = args->argv[0].asNumber();
    fillchar = ' ';
    if(args->argc == 2)
    {
        ofillstr = args->argv[1].asString();
        fillchar = ofillstr->m_sbuf->m_data[0];
    }
    if(width <= (int)string->m_sbuf->m_length)
    {
        return args->thisval;
    }
    fillsize = width - string->m_sbuf->m_length;
    fill = (char*)neon::State::GC::allocate(state, sizeof(char), (size_t)fillsize + 1);
    finalsize = string->m_sbuf->m_length + fillsize;
    finalutf8size = string->m_sbuf->m_length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)neon::State::GC::allocate(state, sizeof(char), (size_t)finalsize + 1);
    memcpy(str, fill, fillsize);
    memcpy(str + fillsize, string->m_sbuf->m_data, string->m_sbuf->m_length);
    str[finalsize] = '\0';
    neon::BasicArray::freeArray(state, fill, fillsize + 1);
    result = neon::String::take(state, str, finalsize);
    result->m_sbuf->m_length = finalutf8size;
    result->m_sbuf->m_length = finalsize;
    return neon::Value::fromObject(result);
}

neon::Value nn_memberfunc_string_rpad(neon::State* state, neon::Arguments* args)
{
    int i;
    int width;
    int fillsize;
    int finalsize;
    int finalutf8size;
    char fillchar;
    char* str;
    char* fill;
    neon::String* ofillstr;
    neon::String* string;
    neon::String* result;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    string = args->thisval.asString();
    width = args->argv[0].asNumber();
    fillchar = ' ';
    if(args->argc == 2)
    {
        ofillstr = args->argv[1].asString();
        fillchar = ofillstr->m_sbuf->m_data[0];
    }
    if(width <= (int)string->m_sbuf->m_length)
    {
        return args->thisval;
    }
    fillsize = width - string->m_sbuf->m_length;
    fill = (char*)neon::State::GC::allocate(state, sizeof(char), (size_t)fillsize + 1);
    finalsize = string->m_sbuf->m_length + fillsize;
    finalutf8size = string->m_sbuf->m_length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)neon::State::GC::allocate(state, sizeof(char), (size_t)finalsize + 1);
    memcpy(str, string->m_sbuf->m_data, string->m_sbuf->m_length);
    memcpy(str + string->m_sbuf->m_length, fill, fillsize);
    str[finalsize] = '\0';
    neon::BasicArray::freeArray(state, fill, fillsize + 1);
    result = neon::String::take(state, str, finalsize);
    result->m_sbuf->m_length = finalutf8size;
    result->m_sbuf->m_length = finalsize;
    return neon::Value::fromObject(result);
}

neon::Value nn_memberfunc_string_split(neon::State* state, neon::Arguments* args)
{
    int i;
    int end;
    int start;
    int length;
    neon::Array* list;
    neon::String* string;
    neon::String* delimeter;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->thisval.asString();
    delimeter = args->argv[0].asString();
    /* empty string matches empty string to empty list */
    if(((string->length() == 0) && (delimeter->length() == 0)) || (string->length() == 0) || (delimeter->length() == 0))
    {
        return neon::Value::fromObject(neon::Array::make(state));
    }
    list = state->gcProtect(neon::Array::make(state));
    if(delimeter->length() > 0)
    {
        start = 0;
        for(i = 0; i <= (int)string->length(); i++)
        {
            /* match found. */
            if(memcmp(string->data() + i, delimeter->data(), delimeter->length()) == 0 || i == (int)string->length())
            {
                list->push(neon::Value::fromObject(neon::String::copy(state, string->data() + start, i - start)));
                i += delimeter->length() - 1;
                start = i + 1;
            }
        }
    }
    else
    {
        length = (int)string->length();
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            list->push(neon::Value::fromObject(neon::String::copy(state, string->data() + start, (int)(end - start))));
        }
    }
    return neon::Value::fromObject(list);
}


neon::Value nn_memberfunc_string_replace(neon::State* state, neon::Arguments* args)
{
    int i;
    int totallength;
    neon::Util::StrBuffer* result;
    neon::String* substr;
    neon::String* string;
    neon::String* repsubstr;
    NEON_ARGS_CHECKCOUNTRANGE(args, 2, 3);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    NEON_ARGS_CHECKTYPE(args, 1, isString);
    string = args->thisval.asString();
    substr = args->argv[0].asString();
    repsubstr = args->argv[1].asString();
    if((string->length() == 0 && substr->length() == 0) || string->length() == 0 || substr->length() == 0)
    {
        return neon::Value::fromObject(neon::String::copy(state, string->data(), string->length()));
    }
    result = neon::Memory::create<neon::Util::StrBuffer>(0);
    totallength = 0;
    for(i = 0; i < (int)string->length(); i++)
    {
        if(memcmp(string->data() + i, substr->data(), substr->length()) == 0)
        {
            if(substr->length() > 0)
            {
                result->append(repsubstr->data(), repsubstr->length());
            }
            i += substr->length() - 1;
            totallength += repsubstr->length();
        }
        else
        {
            result->append(string->data()[i]);
            totallength++;
        }
    }
    return neon::Value::fromObject(neon::String::makeFromStrbuf(state, result, neon::Util::hashString(result->m_data, result->m_length)));
}

neon::Value nn_memberfunc_string_iter(neon::State* state, neon::Arguments* args)
{
    int index;
    int length;
    neon::String* string;
    neon::String* result;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    string = args->thisval.asString();
    length = string->length();
    index = args->argv[0].asNumber();
    if(index > -1 && index < length)
    {
        result = neon::String::copy(state, &string->data()[index], 1);
        return neon::Value::fromObject(result);
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_string_itern(neon::State* state, neon::Arguments* args)
{
    int index;
    int length;
    neon::String* string;
    NEON_ARGS_CHECKCOUNT(args, 1);
    string = args->thisval.asString();
    length = string->length();
    if(args->argv[0].isNull())
    {
        if(length == 0)
        {
            return neon::Value::makeBool(false);
        }
        return neon::Value::makeNumber(0);
    }
    if(!args->argv[0].isNumber())
    {
        return state->raiseFromFunction(args, "strings are numerically indexed");
    }
    index = args->argv[0].asNumber();
    if(index < length - 1)
    {
        return neon::Value::makeNumber((double)index + 1);
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_string_each(neon::State* state, neon::Arguments* args)
{
    int i;
    int arity;
    neon::Value callable;
    neon::Value unused;
    neon::String* string;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    string = args->thisval.asString();
    callable = args->argv[0];
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < (int)string->length(); i++)
    {
        if(arity > 0)
        {
            nestargs->m_varray->m_values[0] = neon::Value::fromObject(neon::String::copy(state, string->data() + i, 1));
            if(arity > 1)
            {
                nestargs->m_varray->m_values[1] = neon::Value::makeNumber(i);
            }
        }
        nc.call(callable, args->thisval, nestargs, &unused);
    }
    /* pop the argument list */
    state->stackPop();
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_object_dump(neon::State* state, neon::Arguments* args)
{
    neon::Value v;
    neon::String* os;
    v = args->thisval;
    neon::Printer pd(state);
    pd.printValue(v, true, false);
    os = pd.takeString();
    return neon::Value::fromObject(os);
}

neon::Value nn_memberfunc_object_tostring(neon::State* state, neon::Arguments* args)
{
    neon::Value v;
    neon::String* os;
    v = args->thisval;
    neon::Printer pd(state);
    pd.printValue(v, false, true);
    os = pd.takeString();
    return neon::Value::fromObject(os);
}

neon::Value nn_memberfunc_object_typename(neon::State* state, neon::Arguments* args)
{
    neon::Value v;
    neon::String* os;
    v = args->argv[0];
    os = neon::String::copy(state, neon::Value::Typename(v));
    return neon::Value::fromObject(os);
}

neon::Value nn_memberfunc_object_isstring(neon::State* state, neon::Arguments* args)
{
    neon::Value v;
    (void)state;
    v = args->thisval;
    return neon::Value::makeBool(v.isString());
}

neon::Value nn_memberfunc_object_isarray(neon::State* state, neon::Arguments* args)
{
    neon::Value v;
    (void)state;
    v = args->thisval;
    return neon::Value::makeBool(v.isArray());
}

neon::Value nn_memberfunc_object_iscallable(neon::State* state, neon::Arguments* args)
{
    neon::Value selfval;
    (void)state;
    selfval = args->thisval;
    return neon::Value::makeBool(
        selfval.isClass() ||
        selfval.isFuncScript() ||
        selfval.isFuncClosure() ||
        selfval.isFuncBound() ||
        selfval.isFuncNative()
    );
}

neon::Value nn_memberfunc_object_isbool(neon::State* state, neon::Arguments* args)
{
    neon::Value selfval;
    (void)state;
    selfval = args->thisval;
    return neon::Value::makeBool(selfval.isBool());
}

neon::Value nn_memberfunc_object_isnumber(neon::State* state, neon::Arguments* args)
{
    neon::Value selfval;
    (void)state;
    selfval = args->thisval;
    return neon::Value::makeBool(selfval.isNumber());
}

neon::Value nn_memberfunc_object_isint(neon::State* state, neon::Arguments* args)
{
    neon::Value selfval;
    (void)state;
    selfval = args->thisval;
    return neon::Value::makeBool(selfval.isNumber() && (((int)selfval.asNumber()) == selfval.asNumber()));
}

neon::Value nn_memberfunc_object_isdict(neon::State* state, neon::Arguments* args)
{
    neon::Value selfval;
    (void)state;
    selfval = args->thisval;
    return neon::Value::makeBool(selfval.isDict());
}

neon::Value nn_memberfunc_object_isobject(neon::State* state, neon::Arguments* args)
{
    neon::Value selfval;
    (void)state;
    selfval = args->thisval;
    return neon::Value::makeBool(selfval.isObject());
}

neon::Value nn_memberfunc_object_isfunction(neon::State* state, neon::Arguments* args)
{
    neon::Value selfval;
    (void)state;
    selfval = args->thisval;
    return neon::Value::makeBool(
        selfval.isFuncScript() ||
        selfval.isFuncClosure() ||
        selfval.isFuncBound() ||
        selfval.isFuncNative()
    );
}

neon::Value nn_memberfunc_object_isiterable(neon::State* state, neon::Arguments* args)
{
    bool isiterable;
    neon::Value dummy;
    neon::ClassObject* klass;
    neon::Value selfval;
    (void)state;
    selfval = args->thisval;
    isiterable = selfval.isArray() || selfval.isDict() || selfval.isString();
    if(!isiterable && selfval.isInstance())
    {
        klass = selfval.asInstance()->m_fromclass;
        isiterable = klass->m_methods->get(neon::Value::fromObject(neon::String::copy(state, "@iter")), &dummy)
            && klass->m_methods->get(neon::Value::fromObject(neon::String::copy(state, "@itern")), &dummy);
    }
    return neon::Value::makeBool(isiterable);
}

neon::Value nn_memberfunc_object_isclass(neon::State* state, neon::Arguments* args)
{
    neon::Value selfval;
    (void)state;
    selfval = args->thisval;
    return neon::Value::makeBool(selfval.isClass());
}

neon::Value nn_memberfunc_object_isfile(neon::State* state, neon::Arguments* args)
{
    neon::Value selfval;
    (void)state;
    selfval = args->thisval;
    return neon::Value::makeBool(selfval.isFile());
}

neon::Value nn_memberfunc_object_isinstance(neon::State* state, neon::Arguments* args)
{
    neon::Value selfval;
    (void)state;
    selfval = args->thisval;
    return neon::Value::makeBool(selfval.isInstance());
}

neon::Value nn_memberfunc_number_tobinstring(neon::State* state, neon::Arguments* args)
{
    return neon::Value::fromObject(neon::String::numToBinString(state, args->thisval.asNumber()));
}

neon::Value nn_memberfunc_number_tooctstring(neon::State* state, neon::Arguments* args)
{
    return neon::Value::fromObject(neon::String::numToOctString(state, args->thisval.asNumber(), false));
}

neon::Value nn_memberfunc_number_tohexstring(neon::State* state, neon::Arguments* args)
{
    return neon::Value::fromObject(neon::String::numToHexString(state, args->thisval.asNumber(), false));
}

void nn_state_initbuiltinmethods(neon::State* state)
{
    {
        state->m_classprimprocess->setStaticPropertyCstr("env", neon::Value::fromObject(state->m_envdict));
    }
    {
        state->m_classprimobject->defStaticNativeMethod("typename", nn_memberfunc_object_typename);
        state->m_classprimobject->defNativeMethod("dump", nn_memberfunc_object_dump);
        state->m_classprimobject->defNativeMethod("toString", nn_memberfunc_object_tostring);
        state->m_classprimobject->defNativeMethod("isArray", nn_memberfunc_object_isarray);        
        state->m_classprimobject->defNativeMethod("isString", nn_memberfunc_object_isstring);
        state->m_classprimobject->defNativeMethod("isCallable", nn_memberfunc_object_iscallable);
        state->m_classprimobject->defNativeMethod("isBool", nn_memberfunc_object_isbool);
        state->m_classprimobject->defNativeMethod("isNumber", nn_memberfunc_object_isnumber);
        state->m_classprimobject->defNativeMethod("isInt", nn_memberfunc_object_isint);
        state->m_classprimobject->defNativeMethod("isDict", nn_memberfunc_object_isdict);
        state->m_classprimobject->defNativeMethod("isObject", nn_memberfunc_object_isobject);
        state->m_classprimobject->defNativeMethod("isFunction", nn_memberfunc_object_isfunction);
        state->m_classprimobject->defNativeMethod("isIterable", nn_memberfunc_object_isiterable);
        state->m_classprimobject->defNativeMethod("isClass", nn_memberfunc_object_isclass);
        state->m_classprimobject->defNativeMethod("isFile", nn_memberfunc_object_isfile);
        state->m_classprimobject->defNativeMethod("isInstance", nn_memberfunc_object_isinstance);

    }
    
    {
        state->m_classprimnumber->defNativeMethod("toHexString", nn_memberfunc_number_tohexstring);
        state->m_classprimnumber->defNativeMethod("toOctString", nn_memberfunc_number_tooctstring);
        state->m_classprimnumber->defNativeMethod("toBinString", nn_memberfunc_number_tobinstring);
    }
    {
        state->m_classprimstring->defNativeConstructor(nn_memberfunc_string_constructor);
        state->m_classprimstring->defStaticNativeMethod("fromCharCode", nn_memberfunc_string_fromcharcode);
        state->m_classprimstring->defStaticNativeMethod("utf8Decode", nn_memberfunc_string_utf8decode);
        state->m_classprimstring->defStaticNativeMethod("utf8Encode", nn_memberfunc_string_utf8encode);
        state->m_classprimstring->defStaticNativeMethod("utf8NumBytes", nn_memberfunc_string_utf8numbytes);
        
        state->m_classprimstring->defCallableField("length", nn_memberfunc_string_length);
        state->m_classprimstring->defNativeMethod("utf8Chars", nn_memberfunc_string_utf8chars);
        state->m_classprimstring->defNativeMethod("utf8Codepoints", nn_memberfunc_string_utf8codepoints);
        state->m_classprimstring->defNativeMethod("utf8Bytes", nn_memberfunc_string_utf8codepoints);

        state->m_classprimstring->defNativeMethod("@iter", nn_memberfunc_string_iter);
        state->m_classprimstring->defNativeMethod("@itern", nn_memberfunc_string_itern);
        state->m_classprimstring->defNativeMethod("size", nn_memberfunc_string_length);
        state->m_classprimstring->defNativeMethod("substr", nn_memberfunc_string_substring);
        state->m_classprimstring->defNativeMethod("substring", nn_memberfunc_string_substring);
        state->m_classprimstring->defNativeMethod("charCodeAt", nn_memberfunc_string_charcodeat);
        state->m_classprimstring->defNativeMethod("charAt", nn_memberfunc_string_charat);
        state->m_classprimstring->defNativeMethod("upper", nn_memberfunc_string_upper);
        state->m_classprimstring->defNativeMethod("lower", nn_memberfunc_string_lower);
        state->m_classprimstring->defNativeMethod("trim", nn_memberfunc_string_trim);
        state->m_classprimstring->defNativeMethod("ltrim", nn_memberfunc_string_ltrim);
        state->m_classprimstring->defNativeMethod("rtrim", nn_memberfunc_string_rtrim);
        state->m_classprimstring->defNativeMethod("split", nn_memberfunc_string_split);
        state->m_classprimstring->defNativeMethod("indexOf", nn_memberfunc_string_indexof);
        state->m_classprimstring->defNativeMethod("count", nn_memberfunc_string_count);
        state->m_classprimstring->defNativeMethod("toNumber", nn_memberfunc_string_tonumber);
        state->m_classprimstring->defNativeMethod("toList", nn_memberfunc_string_tolist);
        state->m_classprimstring->defNativeMethod("lpad", nn_memberfunc_string_lpad);
        state->m_classprimstring->defNativeMethod("rpad", nn_memberfunc_string_rpad);
        state->m_classprimstring->defNativeMethod("replace", nn_memberfunc_string_replace);
        state->m_classprimstring->defNativeMethod("each", nn_memberfunc_string_each);
        state->m_classprimstring->defNativeMethod("startswith", nn_memberfunc_string_startswith);
        state->m_classprimstring->defNativeMethod("endswith", nn_memberfunc_string_endswith);
        state->m_classprimstring->defNativeMethod("isAscii", nn_memberfunc_string_isascii);
        state->m_classprimstring->defNativeMethod("isAlpha", nn_memberfunc_string_isalpha);
        state->m_classprimstring->defNativeMethod("isAlnum", nn_memberfunc_string_isalnum);
        state->m_classprimstring->defNativeMethod("isNumber", nn_memberfunc_string_isnumber);
        state->m_classprimstring->defNativeMethod("isFloat", nn_memberfunc_string_isfloat);
        state->m_classprimstring->defNativeMethod("isLower", nn_memberfunc_string_islower);
        state->m_classprimstring->defNativeMethod("isUpper", nn_memberfunc_string_isupper);
        state->m_classprimstring->defNativeMethod("isSpace", nn_memberfunc_string_isspace);
        
    }
    {
        #if 1
        state->m_classprimarray->defNativeConstructor(nn_memberfunc_array_constructor);
        #endif
        state->m_classprimarray->defCallableField("length", nn_memberfunc_array_length);
        state->m_classprimarray->defNativeMethod("size", nn_memberfunc_array_length);
        state->m_classprimarray->defNativeMethod("join", nn_memberfunc_array_join);
        state->m_classprimarray->defNativeMethod("append", nn_memberfunc_array_append);
        state->m_classprimarray->defNativeMethod("push", nn_memberfunc_array_append);
        state->m_classprimarray->defNativeMethod("clear", nn_memberfunc_array_clear);
        state->m_classprimarray->defNativeMethod("clone", nn_memberfunc_array_clone);
        state->m_classprimarray->defNativeMethod("count", nn_memberfunc_array_count);
        state->m_classprimarray->defNativeMethod("extend", nn_memberfunc_array_extend);
        state->m_classprimarray->defNativeMethod("indexOf", nn_memberfunc_array_indexof);
        state->m_classprimarray->defNativeMethod("insert", nn_memberfunc_array_insert);
        state->m_classprimarray->defNativeMethod("pop", nn_memberfunc_array_pop);
        state->m_classprimarray->defNativeMethod("shift", nn_memberfunc_array_shift);
        state->m_classprimarray->defNativeMethod("removeAt", nn_memberfunc_array_removeat);
        state->m_classprimarray->defNativeMethod("remove", nn_memberfunc_array_remove);
        state->m_classprimarray->defNativeMethod("reverse", nn_memberfunc_array_reverse);
        state->m_classprimarray->defNativeMethod("sort", nn_memberfunc_array_sort);
        state->m_classprimarray->defNativeMethod("contains", nn_memberfunc_array_contains);
        state->m_classprimarray->defNativeMethod("delete", nn_memberfunc_array_delete);
        state->m_classprimarray->defNativeMethod("first", nn_memberfunc_array_first);
        state->m_classprimarray->defNativeMethod("last", nn_memberfunc_array_last);
        state->m_classprimarray->defNativeMethod("isEmpty", nn_memberfunc_array_isempty);
        state->m_classprimarray->defNativeMethod("take", nn_memberfunc_array_take);
        state->m_classprimarray->defNativeMethod("get", nn_memberfunc_array_get);
        state->m_classprimarray->defNativeMethod("compact", nn_memberfunc_array_compact);
        state->m_classprimarray->defNativeMethod("unique", nn_memberfunc_array_unique);
        state->m_classprimarray->defNativeMethod("zip", nn_memberfunc_array_zip);
        state->m_classprimarray->defNativeMethod("zipFrom", nn_memberfunc_array_zipfrom);
        state->m_classprimarray->defNativeMethod("toDict", nn_memberfunc_array_todict);
        state->m_classprimarray->defNativeMethod("each", nn_memberfunc_array_each);
        state->m_classprimarray->defNativeMethod("map", nn_memberfunc_array_map);
        state->m_classprimarray->defNativeMethod("filter", nn_memberfunc_array_filter);
        state->m_classprimarray->defNativeMethod("some", nn_memberfunc_array_some);
        state->m_classprimarray->defNativeMethod("every", nn_memberfunc_array_every);
        state->m_classprimarray->defNativeMethod("reduce", nn_memberfunc_array_reduce);
        state->m_classprimarray->defNativeMethod("@iter", nn_memberfunc_array_iter);
        state->m_classprimarray->defNativeMethod("@itern", nn_memberfunc_array_itern);
    }
    {
        #if 0
        state->m_classprimdict->defNativeConstructor(nn_memberfunc_dict_constructor);
        state->m_classprimdict->defStaticNativeMethod("keys", nn_memberfunc_dict_keys);
        #endif
        state->m_classprimdict->defNativeMethod("keys", nn_memberfunc_dict_keys);
        state->m_classprimdict->defNativeMethod("size", nn_memberfunc_dict_length);
        state->m_classprimdict->defNativeMethod("add", nn_memberfunc_dict_add);
        state->m_classprimdict->defNativeMethod("set", nn_memberfunc_dict_set);
        state->m_classprimdict->defNativeMethod("clear", nn_memberfunc_dict_clear);
        state->m_classprimdict->defNativeMethod("clone", nn_memberfunc_dict_clone);
        state->m_classprimdict->defNativeMethod("compact", nn_memberfunc_dict_compact);
        state->m_classprimdict->defNativeMethod("contains", nn_memberfunc_dict_contains);
        state->m_classprimdict->defNativeMethod("extend", nn_memberfunc_dict_extend);
        state->m_classprimdict->defNativeMethod("get", nn_memberfunc_dict_get);
        state->m_classprimdict->defNativeMethod("values", nn_memberfunc_dict_values);
        state->m_classprimdict->defNativeMethod("remove", nn_memberfunc_dict_remove);
        state->m_classprimdict->defNativeMethod("isEmpty", nn_memberfunc_dict_isempty);
        state->m_classprimdict->defNativeMethod("findKey", nn_memberfunc_dict_findkey);
        state->m_classprimdict->defNativeMethod("toList", nn_memberfunc_dict_tolist);
        state->m_classprimdict->defNativeMethod("each", nn_memberfunc_dict_each);
        state->m_classprimdict->defNativeMethod("filter", nn_memberfunc_dict_filter);
        state->m_classprimdict->defNativeMethod("some", nn_memberfunc_dict_some);
        state->m_classprimdict->defNativeMethod("every", nn_memberfunc_dict_every);
        state->m_classprimdict->defNativeMethod("reduce", nn_memberfunc_dict_reduce);
        state->m_classprimdict->defNativeMethod("@iter", nn_memberfunc_dict_iter);
        state->m_classprimdict->defNativeMethod("@itern", nn_memberfunc_dict_itern);
    }
    {
        state->m_classprimfile->defNativeConstructor(nn_memberfunc_file_constructor);
        state->m_classprimfile->defStaticNativeMethod("exists", nn_memberfunc_file_exists);
        state->m_classprimfile->defNativeMethod("close", nn_memberfunc_file_close);
        state->m_classprimfile->defNativeMethod("open", nn_memberfunc_file_open);
        state->m_classprimfile->defNativeMethod("read", nn_memberfunc_file_read);
        state->m_classprimfile->defNativeMethod("get", nn_memberfunc_file_get);
        state->m_classprimfile->defNativeMethod("gets", nn_memberfunc_file_gets);
        state->m_classprimfile->defNativeMethod("write", nn_memberfunc_file_write);
        state->m_classprimfile->defNativeMethod("puts", nn_memberfunc_file_puts);
        state->m_classprimfile->defNativeMethod("printf", nn_memberfunc_file_printf);
        state->m_classprimfile->defNativeMethod("number", nn_memberfunc_file_number);
        state->m_classprimfile->defNativeMethod("isTTY", nn_memberfunc_file_istty);
        state->m_classprimfile->defNativeMethod("isOpen", nn_memberfunc_file_isopen);
        state->m_classprimfile->defNativeMethod("isClosed", nn_memberfunc_file_isclosed);
        state->m_classprimfile->defNativeMethod("flush", nn_memberfunc_file_flush);
        state->m_classprimfile->defNativeMethod("stats", nn_memberfunc_file_stats);
        state->m_classprimfile->defNativeMethod("path", nn_memberfunc_file_path);
        state->m_classprimfile->defNativeMethod("seek", nn_memberfunc_file_seek);
        state->m_classprimfile->defNativeMethod("tell", nn_memberfunc_file_tell);
        state->m_classprimfile->defNativeMethod("mode", nn_memberfunc_file_mode);
        state->m_classprimfile->defNativeMethod("name", nn_memberfunc_file_name);
    }
    {
        state->m_classprimrange->defNativeConstructor(nn_memberfunc_range_constructor);
        state->m_classprimrange->defNativeMethod("lower", nn_memberfunc_range_lower);
        state->m_classprimrange->defNativeMethod("upper", nn_memberfunc_range_upper);
        state->m_classprimrange->defNativeMethod("range", nn_memberfunc_range_range);
        state->m_classprimrange->defNativeMethod("loop", nn_memberfunc_range_loop);
        state->m_classprimrange->defNativeMethod("expand", nn_memberfunc_range_expand);
        state->m_classprimrange->defNativeMethod("toArray", nn_memberfunc_range_expand);
        state->m_classprimrange->defNativeMethod("@iter", nn_memberfunc_range_iter);
        state->m_classprimrange->defNativeMethod("@itern", nn_memberfunc_range_itern);
    }
}

neon::Value nn_nativefn_time(neon::State* state, neon::Arguments* args)
{
    struct timeval tv;
    (void)args;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    osfn_gettimeofday(&tv, nullptr);
    return neon::Value::makeNumber((double)tv.tv_sec + ((double)tv.tv_usec / 10000000));
}

neon::Value nn_nativefn_microtime(neon::State* state, neon::Arguments* args)
{
    struct timeval tv;
    (void)args;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    osfn_gettimeofday(&tv, nullptr);
    return neon::Value::makeNumber((1000000 * (double)tv.tv_sec) + ((double)tv.tv_usec / 10));
}

neon::Value nn_nativefn_id(neon::State* state, neon::Arguments* args)
{
    long* lptr;
    neon::Value val;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    val = args->argv[0];
    lptr = reinterpret_cast<long*>(&val);
    return neon::Value::makeNumber(*lptr);
}

neon::Value nn_nativefn_int(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    if(args->argc == 0)
    {
        return neon::Value::makeNumber(0);
    }
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    return neon::Value::makeNumber((double)((int)args->argv[0].asNumber()));
}

neon::Value nn_nativefn_chr(neon::State* state, neon::Arguments* args)
{
    size_t len;
    char* string;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    string = neon::Util::utf8Encode(state, (int)args->argv[0].asNumber(), &len);
    return neon::Value::fromObject(neon::String::take(state, string));
}

neon::Value nn_nativefn_ord(neon::State* state, neon::Arguments* args)
{
    int ord;
    int length;
    neon::String* string;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->argv[0].asString();
    length = string->length();
    if(length > 1)
    {
        return state->raiseFromFunction(args, "ord() expects character as argument, string given");
    }
    ord = (int)string->data()[0];
    if(ord < 0)
    {
        ord += 256;
    }
    return neon::Value::makeNumber(ord);
}


neon::Value nn_nativefn_rand(neon::State* state, neon::Arguments* args)
{
    int tmp;
    int lowerlimit;
    int upperlimit;
    (void)state;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 2);
    lowerlimit = 0;
    upperlimit = 1;
    if(args->argc > 0)
    {
        NEON_ARGS_CHECKTYPE(args, 0, isNumber);
        lowerlimit = args->argv[0].asNumber();
    }
    if(args->argc == 2)
    {
        NEON_ARGS_CHECKTYPE(args, 1, isNumber);
        upperlimit = args->argv[1].asNumber();
    }
    if(lowerlimit > upperlimit)
    {
        tmp = upperlimit;
        upperlimit = lowerlimit;
        lowerlimit = tmp;
    }
    return neon::Value::makeNumber(neon::Util::MTRand(lowerlimit, upperlimit));
}

neon::Value nn_nativefn_typeof(neon::State* state, neon::Arguments* args)
{
    const char* result;
    NEON_ARGS_CHECKCOUNT(args, 1);
    result = neon::Value::Typename(args->argv[0]);
    return neon::Value::fromObject(neon::String::copy(state, result));
}

neon::Value nn_nativefn_eval(neon::State* state, neon::Arguments* args)
{
    neon::Value result;
    neon::String* os;
    NEON_ARGS_CHECKCOUNT(args, 1);
    os = args->argv[0].asString();
    /*fprintf(stderr, "eval:src=%s\n", os->data());*/
    result = state->evalSource(os->data());
    return result;
}

/*
neon::Value nn_nativefn_loadfile(neon::State* state, neon::Arguments* args)
{
    neon::Value result;
    neon::String* os;
    NEON_ARGS_CHECKCOUNT(args, 1);
    os = args->argv[0].asString();
    fprintf(stderr, "eval:src=%s\n", os->data());
    result = state->evalSource(os->data());
    return result;
}
*/

neon::Value nn_nativefn_instanceof(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isInstance);
    NEON_ARGS_CHECKTYPE(args, 1, isClass);
    return neon::Value::makeBool(nn_util_isinstanceof(args->argv[0].asInstance()->m_fromclass, args->argv[1].asClass()));
}


neon::Value nn_nativefn_sprintf(neon::State* state, neon::Arguments* args)
{
    neon::String* res;
    neon::String* ofmt;
    NEON_ARGS_CHECKMINARG(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    ofmt = args->argv[0].asString();
    neon::Printer pd(state);
    neon::FormatInfo nfi(state, &pd, ofmt->cstr(), ofmt->length());
    if(!nfi.format(args->argc, 1, args->argv))
    {
        return neon::Value::makeNull();
    }
    res = pd.takeString();
    return neon::Value::fromObject(res);
}

neon::Value nn_nativefn_printf(neon::State* state, neon::Arguments* args)
{
    neon::String* ofmt;
    NEON_ARGS_CHECKMINARG(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    ofmt = args->argv[0].asString();
    neon::FormatInfo nfi(state, state->m_stdoutprinter, ofmt->cstr(), ofmt->length());
    if(!nfi.format(args->argc, 1, args->argv))
    {
    }
    return neon::Value::makeNull();
}

neon::Value nn_nativefn_print(neon::State* state, neon::Arguments* args)
{
    int i;
    for(i = 0; i < args->argc; i++)
    {
        state->m_stdoutprinter->printValue(args->argv[i], false, true);
    }
    if(state->m_isreplmode)
    {
        state->m_stdoutprinter->put("\n");
    }
    return neon::Value::makeEmpty();
}

neon::Value nn_nativefn_println(neon::State* state, neon::Arguments* args)
{
    neon::Value v;
    v = nn_nativefn_print(state, args);
    state->m_stdoutprinter->put("\n");
    return v;
}

void nn_state_initbuiltinfunctions(neon::State* state)
{
    state->defNativeFunction("chr", nn_nativefn_chr);
    state->defNativeFunction("id", nn_nativefn_id);
    state->defNativeFunction("int", nn_nativefn_int);
    state->defNativeFunction("instanceof", nn_nativefn_instanceof);
    state->defNativeFunction("microtime", nn_nativefn_microtime);
    state->defNativeFunction("ord", nn_nativefn_ord);
    state->defNativeFunction("sprintf", nn_nativefn_sprintf);
    state->defNativeFunction("printf", nn_nativefn_printf);
    state->defNativeFunction("print", nn_nativefn_print);
    state->defNativeFunction("println", nn_nativefn_println);
    state->defNativeFunction("rand", nn_nativefn_rand);
    state->defNativeFunction("time", nn_nativefn_time);
    state->defNativeFunction("eval", nn_nativefn_eval);    
}

void nn_vm_initvmstate(neon::State* state)
{
    size_t i;
    size_t j;
    state->m_vmstate.linkedobjects = nullptr;
    state->m_vmstate.currentframe = nullptr;
    {
        state->m_vmstate.stackcapacity = NEON_CFG_INITSTACKCOUNT;
        state->m_vmstate.stackvalues = (neon::Value*)neon::Memory::osMalloc(NEON_CFG_INITSTACKCOUNT * sizeof(neon::Value));
        if(state->m_vmstate.stackvalues == nullptr)
        {
            fprintf(stderr, "error: failed to allocate stackvalues!\n");
            abort();
        }
        memset(state->m_vmstate.stackvalues, 0, NEON_CFG_INITSTACKCOUNT * sizeof(neon::Value));
    }
    {
        state->m_vmstate.framecapacity = NEON_CFG_INITFRAMECOUNT;
        state->m_vmstate.framevalues = (neon::State::CallFrame*)neon::Memory::osMalloc(NEON_CFG_INITFRAMECOUNT * sizeof(neon::State::CallFrame));
        if(state->m_vmstate.framevalues == nullptr)
        {
            fprintf(stderr, "error: failed to allocate framevalues!\n");
            abort();
        }
        memset(state->m_vmstate.framevalues, 0, NEON_CFG_INITFRAMECOUNT * sizeof(neon::State::CallFrame));
        for(i=0; i<NEON_CFG_INITFRAMECOUNT; i++)
        {
            for(j=0; j<NEON_CFG_MAXEXCEPTHANDLERS; j++)
            {
                state->m_vmstate.framevalues[i].handlers[j].klass = nullptr;
            }
        }
    }
}

namespace neon
{
    #if 0
        #define destrdebug(...) \
            { \
                fprintf(stderr, "in ~State(): "); \
                fprintf(stderr, __VA_ARGS__); \
                fprintf(stderr, "\n"); \
            }
    #else
        #define destrdebug(...)     
    #endif

    bool State::callClosure(FuncClosure* closure, Value thisval, int argcount)
    {
        int i;
        int startva;
        State::CallFrame* frame;
        Array* argslist;
        NEON_APIDEBUG(this, "thisval.type=%s, argcount=%d", Value::Typename(thisval), argcount);
        /* fill empty parameters if not variadic */
        for(; !closure->scriptfunc->m_isvariadic && argcount < closure->scriptfunc->m_arity; argcount++)
        {
            stackPush(Value::makeNull());
        }
        /* handle variadic arguments... */
        if(closure->scriptfunc->m_isvariadic && argcount >= closure->scriptfunc->m_arity - 1)
        {
            startva = argcount - closure->scriptfunc->m_arity;
            argslist = Array::make(this);
            stackPush(Value::fromObject(argslist));
            for(i = startva; i >= 0; i--)
            {
                argslist->push(stackPeek(i + 1));
            }
            argcount -= startva;
            /* +1 for the gc protection push above */
            stackPop(startva + 2);
            stackPush(Value::fromObject(argslist));
        }
        if(argcount != closure->scriptfunc->m_arity)
        {
            stackPop(argcount);
            if(closure->scriptfunc->m_isvariadic)
            {
                return raiseClass(m_exceptions.stdexception, "expected at least %d arguments but got %d", closure->scriptfunc->m_arity - 1, argcount);
            }
            else
            {
                return raiseClass(m_exceptions.stdexception, "expected %d arguments but got %d", closure->scriptfunc->m_arity, argcount);
            }
        }
        if(checkMaybeResize())
        {
            /* stackPop(argcount); */
        }
        frame = &m_vmstate.framevalues[m_vmstate.framecount++];
        frame->gcprotcount = 0;
        frame->handlercount = 0;
        frame->closure = closure;
        frame->inscode = closure->scriptfunc->m_compiledblob->m_instrucs;
        frame->stackslotpos = m_vmstate.stackidx + (-argcount - 1);
        return true;
    }

    void State::defGlobalValue(const char* name, Value val)
    {
        String* oname;
        oname = String::copy(this, name);
        stackPush(Value::fromObject(oname));
        stackPush(val);
        m_definedglobals->set(m_vmstate.stackvalues[0], m_vmstate.stackvalues[1]);
        stackPop(2);
    }

    void State::defNativeFunction(const char* name, CallbackFN fptr)
    {
        FuncNative* func;
        func = FuncNative::make(this, fptr, name);
        return defGlobalValue(name, Value::fromObject(func));
    }

    bool State::addSearchPathObjString(String* os)
    {
        m_modimportpath->push(Value::fromObject(os));
        return true;
    }

    bool State::addSearchPath(const char* path)
    {
        return addSearchPathObjString(String::copy(this, path));
    }


    ClassObject* State::makeNamedClass(const char* name, ClassObject* parent)
    {
        ClassObject* cl;
        String* os;
        os = String::copy(this, name);
        cl = ClassObject::make(this, os);
        cl->m_superclass = parent;
        m_definedglobals->set(Value::fromObject(os), Value::fromObject(cl));
        return cl;
    }

    FuncClosure* State::compileScriptSource(Module* module, bool fromeval, const char* source)
    {
        Blob* blob;
        FuncScript* function;
        FuncClosure* closure;
        blob = Memory::create<Blob>(this);
        function = Parser::compileSource(this, module, source, blob, false, fromeval);
        if(function == nullptr)
        {
            Memory::destroy(blob);
            return nullptr;
        }
        if(!fromeval)
        {
            stackPush(Value::fromObject(function));
        }
        else
        {
            function->m_scriptfnname = String::copy(this, "(evaledcode)");
        }
        closure = FuncClosure::make(this, function);
        if(!fromeval)
        {
            stackPop();
            stackPush(Value::fromObject(closure));
        }
        Memory::destroy(blob);
        return closure;
    }

    Status State::execSource(Module* module, const char* source, Value* dest)
    {
        Status status;
        FuncClosure* closure;
        module->setFileField();
        closure = compileScriptSource(module, false, source);
        if(closure == nullptr)
        {
            return Status::FAIL_COMPILE;
        }
        if(m_conf.exitafterbytecode)
        {
            return Status::OK;
        }
        callClosure(closure, Value::makeNull(), 0);
        status = runVM(0, dest);
        return status;
    }

    Value State::evalSource(const char* source)
    {
        bool ok;
        int argc;
        Value callme;
        Value retval;
        FuncClosure* closure;
        Array* args;
        (void)argc;
        closure = compileScriptSource(m_toplevelmodule, true, source);
        callme = Value::fromObject(closure);
        args = Array::make(this);
        NestCall nc(this);
        argc = nc.prepare(callme, Value::makeNull(), args);
        ok = nc.call(callme, Value::makeNull(), args, &retval);
        if(!ok)
        {
            raiseClass(m_exceptions.stdexception, "eval() failed");
        }
        return retval;
    }


    State::~State()
    {
        destrdebug("destroying m_modimportpath...");
        Memory::destroy(m_modimportpath);
        destrdebug("destroying linked objects...");
        gcDestroyLinkedObjects();
        /* since object in module can exist in m_definedglobals, it must come before */
        destrdebug("destroying module table...");
        Memory::destroy(m_loadedmodules);
        destrdebug("destroying globals table...");
        Memory::destroy(m_definedglobals);
        destrdebug("destroying namedexceptions table...");
        Memory::destroy(m_namedexceptions);
        destrdebug("destroying strings table...");
        Memory::destroy(m_cachedstrings);
        destrdebug("destroying m_stdoutprinter...");
        Memory::destroy(m_stdoutprinter);
        destrdebug("destroying m_stderrprinter...");
        Memory::destroy(m_stderrprinter);
        destrdebug("destroying m_debugprinter...");
        Memory::destroy(m_debugprinter);
        destrdebug("destroying framevalues...");
        Memory::osFree(m_vmstate.framevalues);
        destrdebug("destroying stackvalues...");
        Memory::osFree(m_vmstate.stackvalues);
        destrdebug("destroying this...");
        destrdebug("done destroying!");
    }

    void State::init(void* userptr)
    {
        static const char* defaultsearchpaths[] =
        {
            "mods",
            "mods/@/index" NEON_CFG_FILEEXT,
            ".",
            nullptr
        };
        int i;
        m_memuserptr = userptr;
        m_activeparser = nullptr;
        m_exceptions.stdexception = nullptr;
        m_rootphysfile = nullptr;
        m_cliargv = nullptr;
        m_isreplmode = false;
        m_currentmarkvalue = true;
        nn_vm_initvmstate(this);
        resetVMState();
        {
            m_gcstate.bytesallocated = 0;
            /* default is 1mb. Can be modified via the -g flag. */
            m_gcstate.nextgc = NEON_CFG_DEFAULTGCSTART;
            m_gcstate.graycount = 0;
            m_gcstate.graycapacity = 0;
            m_gcstate.graystack = nullptr;
        }
        {
            m_stdoutprinter = Memory::create<Printer>(this, stdout, false);
            m_stdoutprinter->m_shouldflush = true;
            m_stderrprinter = Memory::create<Printer>(this, stderr, false);
            m_debugprinter = Memory::create<Printer>(this, stderr, false);
            m_debugprinter->m_shortenvalues = true;
            m_debugprinter->m_maxvallength = 15;
        }
        {
            m_loadedmodules = Memory::create<HashTable>(this);
            m_cachedstrings = Memory::create<HashTable>(this);
            m_definedglobals = Memory::create<HashTable>(this);
            m_namedexceptions = Memory::create<HashTable>(this);

        }
        {
            m_toplevelmodule = Module::make(this, "", "<this>", false);
            m_constructorname = String::copy(this, "constructor");
        }
        {
            m_modimportpath = Memory::create<ValArray>(this);
            for(i=0; defaultsearchpaths[i]!=nullptr; i++)
            {
                addSearchPath(defaultsearchpaths[i]);
            }
        }
        {
            m_classprimobject = makeNamedClass("Object", nullptr);
            m_classprimprocess = makeNamedClass("Process", m_classprimobject);
            m_classprimnumber = makeNamedClass("Number", m_classprimobject);
            m_classprimstring = makeNamedClass("String", m_classprimobject);
            m_classprimarray = makeNamedClass("Array", m_classprimobject);
            m_classprimdict = makeNamedClass("Dict", m_classprimobject);
            m_classprimfile = makeNamedClass("File", m_classprimobject);
            m_classprimrange = makeNamedClass("Range", m_classprimobject);
        }
        {
            m_envdict = Dictionary::make(this);
        }
        {
            if(m_exceptions.stdexception == nullptr)
            {
                m_exceptions.stdexception = makeExceptionClass(m_toplevelmodule, "Exception");
            }
            m_exceptions.asserterror = makeExceptionClass(m_toplevelmodule, "AssertError");
            m_exceptions.syntaxerror = makeExceptionClass(m_toplevelmodule, "SyntaxError");
            m_exceptions.ioerror = makeExceptionClass(m_toplevelmodule, "IOError");
            m_exceptions.oserror = makeExceptionClass(m_toplevelmodule, "OSError");
            m_exceptions.argumenterror = makeExceptionClass(m_toplevelmodule, "ArgumentError");
        }
        {
            nn_state_initbuiltinfunctions(this);
            nn_state_initbuiltinmethods(this);
        }
        {
            {
                m_filestdout = File::make(this, stdout, true, "<stdout>", "wb");
                defGlobalValue("STDOUT", Value::fromObject(m_filestdout));
            }
            {
                m_filestderr = File::make(this, stderr, true, "<stderr>", "wb");
                defGlobalValue("STDERR", Value::fromObject(m_filestderr));
            }
            {
                m_filestdin = File::make(this, stdin, true, "<stdin>", "rb");
                defGlobalValue("STDIN", Value::fromObject(m_filestdin));
            }
        }
    }
}

bool nn_util_methodisprivate(neon::String* name)
{
    return name->length() > 0 && name->data()[0] == '_';
}



bool nn_vm_callnative(neon::State* state, neon::FuncNative* native, neon::Value thisval, int argcount)
{
    size_t spos;
    neon::Value r;
    neon::Value* vargs;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", neon::Value::Typename(thisval), argcount);
    spos = state->m_vmstate.stackidx + (-argcount);
    vargs = &state->m_vmstate.stackvalues[spos];
    neon::Arguments fnargs(state, native->m_nativefnname, thisval, vargs, argcount, native->m_userptrforfn);
    r = native->m_natfunc(state, &fnargs);
    {
        state->m_vmstate.stackvalues[spos - 1] = r;
        state->m_vmstate.stackidx -= argcount;
    }
    state->clearProtect();
    return true;
}

bool nn_vm_callvaluewithobject(neon::State* state, neon::Value callable, neon::Value thisval, int argcount)
{
    size_t spos;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", neon::Value::Typename(thisval), argcount);
    if(callable.isObject())
    {
        switch(callable.objectType())
        {
            case neon::Value::OBJTYPE_FUNCBOUND:
                {
                    neon::FuncBound* bound;
                    bound = callable.asFuncBound();
                    spos = (state->m_vmstate.stackidx + (-argcount - 1));
                    state->m_vmstate.stackvalues[spos] = thisval;
                    return state->callClosure(bound->method, thisval, argcount);
                }
                break;
            case neon::Value::OBJTYPE_CLASS:
                {
                    neon::ClassObject* klass;
                    klass = callable.asClass();
                    spos = (state->m_vmstate.stackidx + (-argcount - 1));
                    state->m_vmstate.stackvalues[spos] = thisval;
                    if(!klass->m_constructor.isEmpty())
                    {
                        return nn_vm_callvaluewithobject(state, klass->m_constructor, thisval, argcount);
                    }
                    else if(klass->m_superclass != nullptr && !klass->m_superclass->m_constructor.isEmpty())
                    {
                        return nn_vm_callvaluewithobject(state, klass->m_superclass->m_constructor, thisval, argcount);
                    }
                    else if(argcount != 0)
                    {
                        return state->raiseClass(state->m_exceptions.stdexception, "%s constructor expects 0 arguments, %d given", klass->m_classname->data(), argcount);
                    }
                    return true;
                }
                break;
            case neon::Value::OBJTYPE_MODULE:
                {
                    neon::Module* module;
                    neon::Property* field;
                    module = callable.asModule();
                    field = module->m_deftable->getByObjString(module->m_modname);
                    if(field != nullptr)
                    {
                        return nn_vm_callvalue(state, field->m_actualval, thisval, argcount);
                    }
                    return state->raiseClass(state->m_exceptions.stdexception, "module %s does not export a default function", module->m_modname);
                }
                break;
            case neon::Value::OBJTYPE_FUNCCLOSURE:
                {
                    return state->callClosure(callable.asFuncClosure(), thisval, argcount);
                }
                break;
            case neon::Value::OBJTYPE_FUNCNATIVE:
                {
                    return nn_vm_callnative(state, callable.asFuncNative(), thisval, argcount);
                }
                break;
            default:
                break;
        }
    }
    return state->raiseClass(state->m_exceptions.stdexception, "object of type %s is not callable", neon::Value::Typename(callable));
}

bool nn_vm_callvalue(neon::State* state, neon::Value callable, neon::Value thisval, int argcount)
{
    neon::Value actualthisval;
    if(callable.isObject())
    {
        switch(callable.objectType())
        {
            case neon::Value::OBJTYPE_FUNCBOUND:
                {
                    neon::FuncBound* bound;
                    bound = callable.asFuncBound();
                    actualthisval = bound->receiver;
                    if(!thisval.isEmpty())
                    {
                        actualthisval = thisval;
                    }
                    NEON_APIDEBUG(state, "actualthisval.type=%s, argcount=%d", neon::Value::Typename(actualthisval), argcount);
                    return nn_vm_callvaluewithobject(state, callable, actualthisval, argcount);
                }
                break;
            case neon::Value::OBJTYPE_CLASS:
                {
                    neon::ClassObject* klass;
                    neon::ClassInstance* instance;
                    klass = callable.asClass();
                    instance = neon::ClassInstance::make(state, klass);
                    actualthisval = neon::Value::fromObject(instance);
                    if(!thisval.isEmpty())
                    {
                        actualthisval = thisval;
                    }
                    NEON_APIDEBUG(state, "actualthisval.type=%s, argcount=%d", neon::Value::Typename(actualthisval), argcount);
                    return nn_vm_callvaluewithobject(state, callable, actualthisval, argcount);
                }
                break;
            default:
                {
                }
                break;
        }
    }
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", neon::Value::Typename(thisval), argcount);
    return nn_vm_callvaluewithobject(state, callable, thisval, argcount);
}

neon::FuncCommon::Type nn_vmutil_getmethodtype(neon::Value method)
{
    switch(method.objectType())
    {
        case neon::Value::OBJTYPE_FUNCNATIVE:
            return method.asFuncNative()->m_functype;
        case neon::Value::OBJTYPE_FUNCCLOSURE:
            return method.asFuncClosure()->scriptfunc->m_functype;
        default:
            break;
    }
    return neon::FuncCommon::FUNCTYPE_FUNCTION;
}


neon::ClassObject* nn_vmutil_getclassfor(neon::State* state, neon::Value receiver)
{
    if(receiver.isNumber())
    {
        return state->m_classprimnumber;
    }
    if(receiver.isObject())
    {
        switch(receiver.asObject()->m_objtype)
        {
            case neon::Value::OBJTYPE_STRING:
                return state->m_classprimstring;
            case neon::Value::OBJTYPE_RANGE:
                return state->m_classprimrange;
            case neon::Value::OBJTYPE_ARRAY:
                return state->m_classprimarray;
            case neon::Value::OBJTYPE_DICT:
                return state->m_classprimdict;
            case neon::Value::OBJTYPE_FILE:
                return state->m_classprimfile;
            /*
            case neon::Value::OBJTYPE_FUNCBOUND:
            case neon::Value::OBJTYPE_FUNCCLOSURE:
            case neon::Value::OBJTYPE_FUNCSCRIPT:
                return state->m_classprimcallable;
            */
            default:
                {
                    fprintf(stderr, "getclassfor: unhandled type!\n");
                }
                break;
        }
    }
    return nullptr;
}

#define nn_vmmac_exitvm(state) \
    { \
        (void)you_are_calling_exit_vm_outside_of_runvm; \
        return neon::Status::FAIL_RUNTIME; \
    }        

#define nn_vmmac_tryraise(state, rtval, ...) \
    if(!state->raiseClass(state->m_exceptions.stdexception, ##__VA_ARGS__)) \
    { \
        return rtval; \
    }

static NEON_FORCEINLINE bool nn_vmutil_invokemethodfromclass(neon::State* state, neon::ClassObject* klass, neon::String* name, int argcount)
{
    neon::Property* field;
    NEON_APIDEBUG(state, "argcount=%d", argcount);
    field = klass->m_methods->getByObjString(name);
    if(field != nullptr)
    {
        if(nn_vmutil_getmethodtype(field->m_actualval) == neon::FuncCommon::FUNCTYPE_PRIVATE)
        {
            return state->raiseClass(state->m_exceptions.stdexception, "cannot call private method '%s' from instance of %s", name->data(), klass->m_classname->data());
        }
        return nn_vm_callvaluewithobject(state, field->m_actualval, neon::Value::fromObject(klass), argcount);
    }
    return state->raiseClass(state->m_exceptions.stdexception, "undefined method '%s' in %s", name->data(), klass->m_classname->data());
}

static NEON_FORCEINLINE bool nn_vmutil_invokemethodself(neon::State* state, neon::String* name, int argcount)
{
    size_t spos;
    neon::Value receiver;
    neon::ClassInstance* instance;
    neon::Property* field;
    NEON_APIDEBUG(state, "argcount=%d", argcount);
    receiver = state->stackPeek(argcount);
    if(receiver.isInstance())
    {
        instance = receiver.asInstance();
        field = instance->m_fromclass->m_methods->getByObjString(name);
        if(field != nullptr)
        {
            return nn_vm_callvaluewithobject(state, field->m_actualval, receiver, argcount);
        }
        field = instance->m_properties->getByObjString(name);
        if(field != nullptr)
        {
            spos = (state->m_vmstate.stackidx + (-argcount - 1));
            state->m_vmstate.stackvalues[spos] = receiver;
            return nn_vm_callvaluewithobject(state, field->m_actualval, receiver, argcount);
        }
    }
    else if(receiver.isClass())
    {
        field = receiver.asClass()->m_methods->getByObjString(name);
        if(field != nullptr)
        {
            if(nn_vmutil_getmethodtype(field->m_actualval) == neon::FuncCommon::FUNCTYPE_STATIC)
            {
                return nn_vm_callvaluewithobject(state, field->m_actualval, receiver, argcount);
            }
            return state->raiseClass(state->m_exceptions.stdexception, "cannot call non-static method %s() on non instance", name->data());
        }
    }
    return state->raiseClass(state->m_exceptions.stdexception, "cannot call method '%s' on object of type '%s'", name->data(), neon::Value::Typename(receiver));
}

static NEON_FORCEINLINE bool nn_vmutil_invokemethod(neon::State* state, neon::String* name, int argcount)
{
    size_t spos;
    neon::Value::ObjType rectype;
    neon::Value receiver;
    neon::Property* field;
    neon::ClassObject* klass;
    receiver = state->stackPeek(argcount);
    NEON_APIDEBUG(state, "receiver.type=%s, argcount=%d", neon::Value::Typename(receiver), argcount);
    if(receiver.isObject())
    {
        rectype = receiver.asObject()->m_objtype;
        switch(rectype)
        {
            case neon::Value::OBJTYPE_MODULE:
                {
                    neon::Module* module;
                    NEON_APIDEBUG(state, "receiver is a module");
                    module = receiver.asModule();
                    field = module->m_deftable->getByObjString(name);
                    if(field != nullptr)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            return state->raiseClass(state->m_exceptions.stdexception, "cannot call private module method '%s'", name->data());
                        }
                        return nn_vm_callvaluewithobject(state, field->m_actualval, receiver, argcount);
                    }
                    return state->raiseClass(state->m_exceptions.stdexception, "module %s does not define class or method %s()", module->m_modname, name->data());
                }
                break;
            case neon::Value::OBJTYPE_CLASS:
                {
                    NEON_APIDEBUG(state, "receiver is a class");
                    klass = receiver.asClass();
                    field = klass->m_methods->getByObjString(name);
                    if(field != nullptr)
                    {
                        if(nn_vmutil_getmethodtype(field->m_actualval) == neon::FuncCommon::FUNCTYPE_PRIVATE)
                        {
                            return state->raiseClass(state->m_exceptions.stdexception, "cannot call private method %s() on %s", name->data(), klass->m_classname->data());
                        }
                        return nn_vm_callvaluewithobject(state, field->m_actualval, receiver, argcount);
                    }
                    else
                    {
                        field = klass->getStaticProperty(name);
                        if(field != nullptr)
                        {
                            return nn_vm_callvaluewithobject(state, field->m_actualval, receiver, argcount);
                        }
                        field = klass->getStaticMethodField(name);
                        if(field != nullptr)
                        {
                            return nn_vm_callvaluewithobject(state, field->m_actualval, receiver, argcount);
                        }
                    }
                    return state->raiseClass(state->m_exceptions.stdexception, "unknown method %s() in class %s", name->data(), klass->m_classname->data());
                }
            case neon::Value::OBJTYPE_INSTANCE:
                {
                    neon::ClassInstance* instance;
                    NEON_APIDEBUG(state, "receiver is an instance");
                    instance = receiver.asInstance();
                    field = instance->m_properties->getByObjString(name);
                    if(field != nullptr)
                    {
                        spos = (state->m_vmstate.stackidx + (-argcount - 1));
                        state->m_vmstate.stackvalues[spos] = receiver;
                        return nn_vm_callvaluewithobject(state, field->m_actualval, receiver, argcount);
                    }
                    return nn_vmutil_invokemethodfromclass(state, instance->m_fromclass, name, argcount);
                }
                break;
            case neon::Value::OBJTYPE_DICT:
                {
                    NEON_APIDEBUG(state, "receiver is a dictionary");
                    field = state->m_classprimdict->getMethodField(name);
                    if(field != nullptr)
                    {
                        return nn_vm_callnative(state, field->m_actualval.asFuncNative(), receiver, argcount);
                    }
                    /* NEW in v0.0.84, dictionaries can declare extra methods as part of their entries. */
                    else
                    {
                        field = receiver.asDict()->m_valtable->getByObjString(name);
                        if(field != nullptr)
                        {
                            if(field->m_actualval.isCallable())
                            {
                                return nn_vm_callvaluewithobject(state, field->m_actualval, receiver, argcount);
                            }
                        }
                    }
                    return state->raiseClass(state->m_exceptions.stdexception, "'dict' has no method %s()", name->data());
                }
                default:
                    {
                    }
                    break;
        }
    }
    klass = nn_vmutil_getclassfor(state, receiver);
    if(klass == nullptr)
    {
        /* @TODO: have methods for non objects as well. */
        return state->raiseClass(state->m_exceptions.stdexception, "non-object %s has no method named '%s'", neon::Value::Typename(receiver), name->data());
    }
    field = klass->getMethodField(name);
    if(field != nullptr)
    {
        return nn_vm_callvaluewithobject(state, field->m_actualval, receiver, argcount);
    }
    return state->raiseClass(state->m_exceptions.stdexception, "'%s' has no method %s()", klass->m_classname->data(), name->data());
}

static NEON_FORCEINLINE bool nn_vmutil_bindmethod(neon::State* state, neon::ClassObject* klass, neon::String* name)
{
    neon::Value val;
    neon::Property* field;
    neon::FuncBound* bound;
    field = klass->m_methods->getByObjString(name);
    if(field != nullptr)
    {
        if(nn_vmutil_getmethodtype(field->m_actualval) == neon::FuncCommon::FUNCTYPE_PRIVATE)
        {
            return state->raiseClass(state->m_exceptions.stdexception, "cannot get private property '%s' from instance", name->data());
        }
        val = state->stackPeek(0);
        bound = neon::FuncBound::make(state, val, field->m_actualval.asFuncClosure());
        state->stackPop();
        state->stackPush(neon::Value::fromObject(bound));
        return true;
    }
    return state->raiseClass(state->m_exceptions.stdexception, "undefined property '%s'", name->data());
}

static NEON_FORCEINLINE neon::ScopeUpvalue* nn_vmutil_upvaluescapture(neon::State* state, neon::Value* local, int stackpos)
{
    neon::ScopeUpvalue* upvalue;
    neon::ScopeUpvalue* prevupvalue;
    neon::ScopeUpvalue* createdupvalue;
    prevupvalue = nullptr;
    upvalue = state->m_vmstate.openupvalues;
    while(upvalue != nullptr && (&upvalue->m_location) > local)
    {
        prevupvalue = upvalue;
        upvalue = upvalue->m_nextupval;
    }
    if(upvalue != nullptr && (&upvalue->m_location) == local)
    {
        return upvalue;
    }
    createdupvalue = neon::ScopeUpvalue::make(state, local, stackpos);
    createdupvalue->m_nextupval = upvalue;
    if(prevupvalue == nullptr)
    {
        state->m_vmstate.openupvalues = createdupvalue;
    }
    else
    {
        prevupvalue->m_nextupval = createdupvalue;
    }
    return createdupvalue;
}

static NEON_FORCEINLINE void nn_vmutil_upvaluesclose(neon::State* state, const neon::Value* last)
{
    neon::ScopeUpvalue* upvalue;
    while(state->m_vmstate.openupvalues != nullptr && (&state->m_vmstate.openupvalues->m_location) >= last)
    {
        upvalue = state->m_vmstate.openupvalues;
        upvalue->m_closed = upvalue->m_location;
        upvalue->m_location = upvalue->m_closed;
        state->m_vmstate.openupvalues = upvalue->m_nextupval;
    }
}

static NEON_FORCEINLINE void nn_vmutil_definemethod(neon::State* state, neon::String* name)
{
    neon::Value method;
    neon::ClassObject* klass;
    method = state->stackPeek(0);
    klass = state->stackPeek(1).asClass();
    klass->m_methods->set(neon::Value::fromObject(name), method);
    if(nn_vmutil_getmethodtype(method) == neon::FuncCommon::FUNCTYPE_INITIALIZER)
    {
        klass->m_constructor = method;
    }
    state->stackPop();
}

static NEON_FORCEINLINE void nn_vmutil_defineproperty(neon::State* state, neon::String* name, bool isstatic)
{
    neon::Value property;
    neon::ClassObject* klass;
    property = state->stackPeek(0);
    klass = state->stackPeek(1).asClass();
    if(!isstatic)
    {
        klass->defProperty(name->data(), property);
    }
    else
    {
        klass->setStaticProperty(name, property);
    }
    state->stackPop();
}


bool nn_util_isinstanceof(neon::ClassObject* klass1, neon::ClassObject* expected)
{
    size_t klen;
    size_t elen;
    const char* kname;
    const char* ename;
    while(klass1 != nullptr)
    {
        elen = expected->m_classname->length();
        klen = klass1->m_classname->length();
        ename = expected->m_classname->data();
        kname = klass1->m_classname->data();
        if(elen == klen && memcmp(kname, ename, klen) == 0)
        {
            return true;
        }
        klass1 = klass1->m_superclass;
    }
    return false;
}

static NEON_FORCEINLINE neon::String* nn_vmutil_multiplystring(neon::State* state, neon::String* str, double number)
{
    int i;
    int times;
    times = (int)number;
    /* 'str' * 0 == '', 'str' * -1 == '' */
    if(times <= 0)
    {
        return neon::String::copy(state, "", 0);
    }
    /* 'str' * 1 == 'str' */
    else if(times == 1)
    {
        return str;
    }
    neon::Printer pd(state);
    for(i = 0; i < times; i++)
    {
        pd.put(str->data(), str->length());
    }
    return pd.takeString();
}

static NEON_FORCEINLINE neon::Array* nn_vmutil_combinearrays(neon::State* state, neon::Array* a, neon::Array* b)
{
    size_t i;
    neon::Array* list;
    list = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(list));
    for(i = 0; i < a->count(); i++)
    {
        list->push(a->at(i));
    }
    for(i = 0; i < b->count(); i++)
    {
        list->push(b->at(i));
    }
    state->stackPop();
    return list;
}

static NEON_FORCEINLINE void nn_vmutil_multiplyarray(neon::State* state, neon::Array* from, neon::Array* newlist, size_t times)
{
    size_t i;
    size_t j;
    (void)state;
    for(i = 0; i < times; i++)
    {
        for(j = 0; j < from->count(); j++)
        {
            newlist->push(from->at(j));
        }
    }
}

static NEON_FORCEINLINE bool nn_vmutil_dogetrangedindexofarray(neon::State* state, neon::Array* list, bool willassign)
{
    int i;
    int idxlower;
    int idxupper;
    neon::Value valupper;
    neon::Value vallower;
    neon::Array* newlist;
    valupper = state->stackPeek(0);
    vallower = state->stackPeek(1);
    if(!(vallower.isNull() || vallower.isNumber()) || !(valupper.isNumber() || valupper.isNull()))
    {
        state->stackPop(2);
        return state->raiseClass(state->m_exceptions.stdexception, "list range index expects upper and lower to be numbers, but got '%s', '%s'", neon::Value::Typename(vallower), neon::Value::Typename(valupper));
    }
    idxlower = 0;
    if(vallower.isNumber())
    {
        idxlower = vallower.asNumber();
    }
    if(valupper.isNull())
    {
        idxupper = list->count();
    }
    else
    {
        idxupper = valupper.asNumber();
    }
    if(idxlower < 0 || (idxupper < 0 && (int(list->count() + idxupper) < 0)) || idxlower >= int(list->count()))
    {
        /* always return an empty list... */
        if(!willassign)
        {
            /* +1 for the list itself */
            state->stackPop(3);
        }
        state->stackPush(neon::Value::fromObject(neon::Array::make(state)));
        return true;
    }
    if(idxupper < 0)
    {
        idxupper = list->count() + idxupper;
    }
    if(idxupper > int(list->count()))
    {
        idxupper = list->count();
    }
    newlist = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(newlist));
    for(i = idxlower; i < idxupper; i++)
    {
        newlist->push(list->at(i));
    }
    /* clear gc protect */
    state->stackPop();
    if(!willassign)
    {
        /* +1 for the list itself */
        state->stackPop(3);
    }
    state->stackPush(neon::Value::fromObject(newlist));
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_dogetrangedindexofstring(neon::State* state, neon::String* string, bool willassign)
{
    int end;
    int start;
    int length;
    int idxupper;
    int idxlower;
    neon::Value valupper;
    neon::Value vallower;
    valupper = state->stackPeek(0);
    vallower = state->stackPeek(1);
    if(!(vallower.isNull() || vallower.isNumber()) || !(valupper.isNumber() || valupper.isNull()))
    {
        state->stackPop(2);
        return state->raiseClass(state->m_exceptions.stdexception, "string range index expects upper and lower to be numbers, but got '%s', '%s'", neon::Value::Typename(vallower), neon::Value::Typename(valupper));
    }
    length = string->length();
    idxlower = 0;
    if(vallower.isNumber())
    {
        idxlower = vallower.asNumber();
    }
    if(valupper.isNull())
    {
        idxupper = length;
    }
    else
    {
        idxupper = valupper.asNumber();
    }
    if(idxlower < 0 || (idxupper < 0 && ((length + idxupper) < 0)) || idxlower >= length)
    {
        /* always return an empty string... */
        if(!willassign)
        {
            /* +1 for the string itself */
            state->stackPop(3);
        }
        state->stackPush(neon::Value::fromObject(neon::String::copy(state, "", 0)));
        return true;
    }
    if(idxupper < 0)
    {
        idxupper = length + idxupper;
    }
    if(idxupper > length)
    {
        idxupper = length;
    }
    start = idxlower;
    end = idxupper;
    if(!willassign)
    {
        /* +1 for the string itself */
        state->stackPop(3);
    }
    state->stackPush(neon::Value::fromObject(neon::String::copy(state, string->data() + start, end - start)));
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_getrangedindex(neon::State* state)
{
    bool isgotten;
    uint8_t willassign;
    neon::Value vfrom;
    willassign = state->readByte();
    isgotten = true;
    vfrom = state->stackPeek(2);
    if(vfrom.isObject())
    {
        switch(vfrom.asObject()->m_objtype)
        {
            case neon::Value::OBJTYPE_STRING:
            {
                if(!nn_vmutil_dogetrangedindexofstring(state, vfrom.asString(), willassign))
                {
                    return false;
                }
                break;
            }
            case neon::Value::OBJTYPE_ARRAY:
            {
                if(!nn_vmutil_dogetrangedindexofarray(state, vfrom.asArray(), willassign))
                {
                    return false;
                }
                break;
            }
            default:
            {
                isgotten = false;
                break;
            }
        }
    }
    else
    {
        isgotten = false;
    }
    if(!isgotten)
    {
        return state->raiseClass(state->m_exceptions.stdexception, "cannot range index object of type %s", neon::Value::Typename(vfrom));
    }
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_doindexgetdict(neon::State* state, neon::Dictionary* dict, bool willassign)
{
    neon::Value vindex;
    neon::Property* field;
    vindex = state->stackPeek(0);
    field = dict->getEntry(vindex);
    if(field != nullptr)
    {
        if(!willassign)
        {
            /* we can safely get rid of the index from the stack */
            state->stackPop(2);
        }
        state->stackPush(field->m_actualval);
        return true;
    }
    state->stackPop(1);
    state->stackPush(neon::Value::makeEmpty());
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_doindexgetmodule(neon::State* state, neon::Module* module, bool willassign)
{
    neon::Value vindex;
    neon::Value result;
    vindex = state->stackPeek(0);
    if(module->m_deftable->get(vindex, &result))
    {
        if(!willassign)
        {
            /* we can safely get rid of the index from the stack */
            state->stackPop(2);
        }
        state->stackPush(result);
        return true;
    }
    state->stackPop();
    return state->raiseClass(state->m_exceptions.stdexception, "%s is undefined in module %s", neon::Value::toString(state, vindex)->data(), module->m_modname);
}

static NEON_FORCEINLINE bool nn_vmutil_doindexgetstring(neon::State* state, neon::String* string, bool willassign)
{
    int end;
    int start;
    int index;
    int maxlength;
    int realindex;
    neon::Value vindex;
    neon::Range* rng;
    (void)realindex;
    vindex = state->stackPeek(0);
    if(!vindex.isNumber())
    {
        if(vindex.isRange())
        {
            rng = vindex.asRange();
            state->stackPop();
            state->stackPush(neon::Value::makeNumber(rng->m_lower));
            state->stackPush(neon::Value::makeNumber(rng->m_upper));
            return nn_vmutil_dogetrangedindexofstring(state, string, willassign);
        }
        state->stackPop(1);
        return state->raiseClass(state->m_exceptions.stdexception, "strings are numerically indexed");
    }
    index = vindex.asNumber();
    maxlength = string->length();
    realindex = index;
    if(index < 0)
    {
        index = maxlength + index;
    }
    if(index < maxlength && index >= 0)
    {
        start = index;
        end = index + 1;
        if(!willassign)
        {
            /*
            // we can safely get rid of the index from the stack
            // +1 for the string itself
            */
            state->stackPop(2);
        }
        state->stackPush(neon::Value::fromObject(neon::String::copy(state, string->data() + start, end - start)));
        return true;
    }
    state->stackPop(1);
    state->stackPush(neon::Value::makeEmpty());
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_doindexgetarray(neon::State* state, neon::Array* list, bool willassign)
{
    int index;
    neon::Value finalval;
    neon::Value vindex;
    neon::Range* rng;
    vindex = state->stackPeek(0);
    if(NEON_UNLIKELY(!vindex.isNumber()))
    {
        if(vindex.isRange())
        {
            rng = vindex.asRange();
            state->stackPop();
            state->stackPush(neon::Value::makeNumber(rng->m_lower));
            state->stackPush(neon::Value::makeNumber(rng->m_upper));
            return nn_vmutil_dogetrangedindexofarray(state, list, willassign);
        }
        state->stackPop();
        return state->raiseClass(state->m_exceptions.stdexception, "list are numerically indexed");
    }
    index = vindex.asNumber();
    if(NEON_UNLIKELY(index < 0))
    {
        index = list->size() + index;
    }
    if((index < int(list->size())) && (index >= 0))
    {
        finalval = list->at(index);
    }
    else
    {
        finalval = neon::Value::makeNull();
    }
    if(!willassign)
    {
        /*
        // we can safely get rid of the index from the stack
        // +1 for the list itself
        */
        state->stackPop(2);
    }
    state->stackPush(finalval);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_indexget(neon::State* state)
{
    bool isgotten;
    uint8_t willassign;
    neon::Value peeked;
    willassign = state->readByte();
    isgotten = true;
    peeked = state->stackPeek(1);
    if(NEON_LIKELY(peeked.isObject()))
    {
        switch(peeked.asObject()->m_objtype)
        {
            case neon::Value::OBJTYPE_STRING:
            {
                if(!nn_vmutil_doindexgetstring(state, peeked.asString(), willassign))
                {
                    return false;
                }
                break;
            }
            case neon::Value::OBJTYPE_ARRAY:
            {
                if(!nn_vmutil_doindexgetarray(state, peeked.asArray(), willassign))
                {
                    return false;
                }
                break;
            }
            case neon::Value::OBJTYPE_DICT:
            {
                if(!nn_vmutil_doindexgetdict(state, peeked.asDict(), willassign))
                {
                    return false;
                }
                break;
            }
            case neon::Value::OBJTYPE_MODULE:
            {
                if(!nn_vmutil_doindexgetmodule(state, peeked.asModule(), willassign))
                {
                    return false;
                }
                break;
            }
            default:
            {
                isgotten = false;
                break;
            }
        }
    }
    else
    {
        isgotten = false;
    }
    if(!isgotten)
    {
        state->raiseClass(state->m_exceptions.stdexception, "cannot index object of type %s", neon::Value::Typename(peeked));
    }
    return true;
}


static NEON_FORCEINLINE bool nn_vmutil_dosetindexdict(neon::State* state, neon::Dictionary* dict, neon::Value index, neon::Value value)
{
    dict->setEntry(index, value);
    /* pop the value, index and dict out */
    state->stackPop(3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = dict[index] = 10
    */
    state->stackPush(value);
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_dosetindexmodule(neon::State* state, neon::Module* module, neon::Value index, neon::Value value)
{
    module->m_deftable->set(index, value);
    /* pop the value, index and dict out */
    state->stackPop(3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = dict[index] = 10
    */
    state->stackPush(value);
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_doindexsetarray(neon::State* state, neon::Array* list, neon::Value index, neon::Value value)
{
    int tmp;
    int rawpos;
    int position;
    int ocnt;
    int ocap;
    int vasz;
    if(NEON_UNLIKELY(!index.isNumber()))
    {
        state->stackPop(3);
        /* pop the value, index and list out */
        return state->raiseClass(state->m_exceptions.stdexception, "list are numerically indexed");
    }
    ocap = list->m_varray->m_capacity;
    ocnt = list->count();
    rawpos = index.asNumber();
    position = rawpos;
    if(rawpos < 0)
    {
        rawpos = list->count() + rawpos;
    }
    if(position < ocap && position > -(ocap))
    {
        list->m_varray->m_values[position] = value;
        if(position >= ocnt)
        {
            list->m_varray->m_count++;
        }
    }
    else
    {
        if(position < 0)
        {
            fprintf(stderr, "inverting negative position %d\n", position);
            position = (~position) + 1;
        }
        tmp = 0;
        vasz = list->count();
        if((position > vasz) || ((position == 0) && (vasz == 0)))
        {
            if(position == 0)
            {
                list->push(neon::Value::makeEmpty());
            }
            else
            {
                tmp = position + 1;
                while(tmp > vasz)
                {
                    list->push(neon::Value::makeEmpty());
                    tmp--;
                }
            }
        }
        fprintf(stderr, "setting value at position %ld (array count: %ld)\n", size_t(position), size_t(list->count()));
    }
    list->m_varray->m_values[position] = value;
    /* pop the value, index and list out */
    state->stackPop(3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = list[index] = 10    
    */
    state->stackPush(value);
    return true;
    /*
    // pop the value, index and list out
    //state->stackPop(3);
    //return state->raiseClass(state->m_exceptions.stdexception, "lists index %d out of range", rawpos);
    //state->stackPush(Value::makeEmpty());
    //return true;
    */
}

static NEON_FORCEINLINE bool nn_vmutil_dosetindexstring(neon::State* state, neon::String* os, neon::Value index, neon::Value value)
{
    neon::String* instr;
    int inchar;
    int rawpos;
    int position;
    int oslen;
    bool ischar;
    bool isstring;
    isstring = false;
    ischar = false;
    if(!index.isNumber())
    {
        state->stackPop(3);
        /* pop the value, index and list out */
        return state->raiseClass(state->m_exceptions.stdexception, "strings are numerically indexed");
    }
    inchar = 0;
    if(value.isNumber())
    {
        inchar = value.asNumber();
        ischar = true;
    }
    if(value.isString())
    {
        instr = value.asString();
        isstring = true;
    }
    rawpos = index.asNumber();
    oslen = os->length();
    position = rawpos;
    if(rawpos < 0)
    {
        position = (oslen + rawpos);
    }
    if(!isstring && !ischar)
    {
        return state->raiseClass(state->m_exceptions.stdexception, "cannot set string with type '%s'", neon::Value::Typename(value));
    }
    if(position < oslen && position > -oslen)
    {
        if(isstring)
        {
            if(instr->length() > 1)
            {
                return state->raiseClass(state->m_exceptions.stdexception, "expected a single-character string", neon::Value::Typename(value));
            }
            inchar = instr->at(0);
            ischar = true;
        }
        if(ischar)
        {
            os->set(position, inchar);
        }
        else if(isstring)
        {
            return state->raiseClass(state->m_exceptions.stdexception, "string prepending by set-assignment not supported yet");
        }
        /* pop the value, index and list out */
        state->stackPop(3);
        /*
        // leave the value on the stack for consumption
        // e.g. variable = list[index] = 10
        */
        state->stackPush(value);
        return true;
    }
    else
    {
        os->append(inchar);
        state->stackPop(3);
        state->stackPush(value);
    }
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_indexset(neon::State* state)
{
    bool isset;
    neon::Value value;
    neon::Value index;
    neon::Value target;
    isset = true;
    target = state->stackPeek(2);
    if(NEON_LIKELY(target.isObject()))
    {
        value = state->stackPeek(0);
        index = state->stackPeek(1);
        if(NEON_UNLIKELY(value.isEmpty()))
        {
            return state->raiseClass(state->m_exceptions.stdexception, "empty cannot be assigned");
        }
        switch(target.asObject()->m_objtype)
        {
            case neon::Value::OBJTYPE_ARRAY:
                {
                    if(!nn_vmutil_doindexsetarray(state, target.asArray(), index, value))
                    {
                        return false;
                    }
                }
                break;
            case neon::Value::OBJTYPE_STRING:
                {
                    if(!nn_vmutil_dosetindexstring(state, target.asString(), index, value))
                    {
                        return false;
                    }
                }
                break;
            case neon::Value::OBJTYPE_DICT:
                {
                    return nn_vmutil_dosetindexdict(state, target.asDict(), index, value);
                }
                break;
            case neon::Value::OBJTYPE_MODULE:
                {
                    return nn_vmutil_dosetindexmodule(state, target.asModule(), index, value);
                }
                break;
            default:
                {
                    isset = false;
                }
                break;
        }
    }
    else
    {
        isset = false;
    }
    if(!isset)
    {
        return state->raiseClass(state->m_exceptions.stdexception, "type of %s is not a valid iterable", neon::Value::Typename(state->stackPeek(3)));
    }
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_concatenate(neon::State* state, bool ontoself)
{
    neon::Value vleft;
    neon::Value vright;
    neon::String* result;
    (void)ontoself;
    vright = state->stackPeek(0);
    vleft = state->stackPeek(1);
    neon::Printer pd(state);
    pd.printValue(vleft, false, true);
    pd.printValue(vright, false, true);
    result = pd.takeString();
    state->stackPop(2);
    state->stackPush(neon::Value::fromObject(result));
    return true;
}

static NEON_FORCEINLINE int nn_vmutil_floordiv(double a, double b)
{
    int d;
    d = (int)a / (int)b;
    return d - ((d * b == a) & ((a < 0) ^ (b < 0)));
}

static NEON_FORCEINLINE double nn_vmutil_modulo(double a, double b)
{
    double r;
    r = fmod(a, b);
    if(r != 0 && ((r < 0) != (b < 0)))
    {
        r += b;
    }
    return r;
}

static NEON_FORCEINLINE neon::Property* nn_vmutil_getproperty(neon::State* state, neon::Value peeked, neon::String* name)
{
    neon::Property* field;
    switch(peeked.asObject()->m_objtype)
    {
        case neon::Value::OBJTYPE_MODULE:
            {
                neon::Module* module;
                module = peeked.asModule();
                field = module->m_deftable->getByObjString(name);
                if(field != nullptr)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        state->raiseClass(state->m_exceptions.stdexception, "cannot get private module property '%s'", name->data());
                        return nullptr;
                    }
                    return field;
                }
                state->raiseClass(state->m_exceptions.stdexception, "%s module does not define '%s'", module->m_modname, name->data());
                return nullptr;
            }
            break;
        case neon::Value::OBJTYPE_CLASS:
            {
                field = peeked.asClass()->m_methods->getByObjString(name);
                if(field != nullptr)
                {
                    if(nn_vmutil_getmethodtype(field->m_actualval) == neon::FuncCommon::FUNCTYPE_STATIC)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            state->raiseClass(state->m_exceptions.stdexception, "cannot call private property '%s' of class %s", name->data(),
                                peeked.asClass()->m_classname->data());
                            return nullptr;
                        }
                        return field;
                    }
                }
                else
                {
                    field = peeked.asClass()->getStaticProperty(name);
                    if(field != nullptr)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            state->raiseClass(state->m_exceptions.stdexception, "cannot call private property '%s' of class %s", name->data(),
                                peeked.asClass()->m_classname->data());
                            return nullptr;
                        }
                        return field;
                    }
                }
                state->raiseClass(state->m_exceptions.stdexception, "class %s does not have a static property or method named '%s'",
                    peeked.asClass()->m_classname->data(), name->data());
                return nullptr;
            }
            break;
        case neon::Value::OBJTYPE_INSTANCE:
            {
                neon::ClassInstance* instance;
                instance = peeked.asInstance();
                field = instance->m_properties->getByObjString(name);
                if(field != nullptr)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        state->raiseClass(state->m_exceptions.stdexception, "cannot call private property '%s' from instance of %s", name->data(), instance->m_fromclass->m_classname->data());
                        return nullptr;
                    }
                    return field;
                }
                if(nn_util_methodisprivate(name))
                {
                    state->raiseClass(state->m_exceptions.stdexception, "cannot bind private property '%s' to instance of %s", name->data(), instance->m_fromclass->m_classname->data());
                    return nullptr;
                }
                if(nn_vmutil_bindmethod(state, instance->m_fromclass, name))
                {
                    return field;
                }
                state->raiseClass(state->m_exceptions.stdexception, "instance of class %s does not have a property or method named '%s'",
                    peeked.asInstance()->m_fromclass->m_classname->data(), name->data());
                return nullptr;
            }
            break;
        case neon::Value::OBJTYPE_STRING:
            {
                field = state->m_classprimstring->getPropertyField(name);
                if(field != nullptr)
                {
                    return field;
                }
                state->raiseClass(state->m_exceptions.stdexception, "class String has no named property '%s'", name->data());
                return nullptr;
            }
            break;
        case neon::Value::OBJTYPE_ARRAY:
            {
                field = state->m_classprimarray->getPropertyField(name);
                if(field != nullptr)
                {
                    return field;
                }
                state->raiseClass(state->m_exceptions.stdexception, "class Array has no named property '%s'", name->data());
                return nullptr;
            }
            break;
        case neon::Value::OBJTYPE_RANGE:
            {
                field = state->m_classprimrange->getPropertyField(name);
                if(field != nullptr)
                {
                    return field;
                }
                state->raiseClass(state->m_exceptions.stdexception, "class Range has no named property '%s'", name->data());
                return nullptr;
            }
            break;
        case neon::Value::OBJTYPE_DICT:
            {
                field = peeked.asDict()->m_valtable->getByObjString(name);
                if(field == nullptr)
                {
                    field = state->m_classprimdict->getPropertyField(name);
                }
                if(field != nullptr)
                {
                    return field;
                }
                state->raiseClass(state->m_exceptions.stdexception, "unknown key or class Dict property '%s'", name->data());
                return nullptr;
            }
            break;
        case neon::Value::OBJTYPE_FILE:
            {
                field = state->m_classprimfile->getPropertyField(name);
                if(field != nullptr)
                {
                    return field;
                }
                state->raiseClass(state->m_exceptions.stdexception, "class File has no named property '%s'", name->data());
                return nullptr;
            }
            break;
        default:
            {
                state->raiseClass(state->m_exceptions.stdexception, "object of type %s does not carry properties", neon::Value::Typename(peeked));
                return nullptr;
            }
            break;
    }
    return nullptr;
}

static NEON_FORCEINLINE bool nn_vmdo_propertyget(neon::State* state)
{
    neon::Value peeked;
    neon::Property* field;
    neon::String* name;
    name = state->readString();
    peeked = state->stackPeek(0);
    if(peeked.isObject())
    {
        field = nn_vmutil_getproperty(state, peeked, name);
        if(field == nullptr)
        {
            return false;
        }
        else
        {
            if(field->m_proptype == neon::Property::PROPTYPE_FUNCTION)
            {
                nn_vm_callvaluewithobject(state, field->m_actualval, peeked, 0);
            }
            else
            {
                state->stackPop();
                state->stackPush(field->m_actualval);
            }
        }
        return true;
    }
    else
    {
        state->raiseClass(state->m_exceptions.stdexception, "'%s' of type %s does not have properties", neon::Value::toString(state, peeked)->data(),
            neon::Value::Typename(peeked));
    }
    return false;
}

static NEON_FORCEINLINE bool nn_vmdo_propertygetself(neon::State* state)
{
    neon::Value peeked;
    neon::String* name;
    neon::ClassObject* klass;
    neon::ClassInstance* instance;
    neon::Module* module;
    neon::Property* field;
    name = state->readString();
    peeked = state->stackPeek(0);
    if(peeked.isInstance())
    {
        instance = peeked.asInstance();
        field = instance->m_properties->getByObjString(name);
        if(field != nullptr)
        {
            /* pop the instance... */
            state->stackPop();
            state->stackPush(field->m_actualval);
            return true;
        }
        if(nn_vmutil_bindmethod(state, instance->m_fromclass, name))
        {
            return true;
        }
        nn_vmmac_tryraise(state, false, "instance of class %s does not have a property or method named '%s'",
            peeked.asInstance()->m_fromclass->m_classname->data(), name->data());
        return false;
    }
    else if(peeked.isClass())
    {
        klass = peeked.asClass();
        field = klass->m_methods->getByObjString(name);
        if(field != nullptr)
        {
            if(nn_vmutil_getmethodtype(field->m_actualval) == neon::FuncCommon::FUNCTYPE_STATIC)
            {
                /* pop the class... */
                state->stackPop();
                state->stackPush(field->m_actualval);
                return true;
            }
        }
        else
        {
            field = klass->getStaticProperty(name);
            if(field != nullptr)
            {
                /* pop the class... */
                state->stackPop();
                state->stackPush(field->m_actualval);
                return true;
            }
        }
        nn_vmmac_tryraise(state, false, "class %s does not have a static property or method named '%s'", klass->m_classname->data(), name->data());
        return false;
    }
    else if(peeked.isModule())
    {
        module = peeked.asModule();
        field = module->m_deftable->getByObjString(name);
        if(field != nullptr)
        {
            /* pop the module... */
            state->stackPop();
            state->stackPush(field->m_actualval);
            return true;
        }
        nn_vmmac_tryraise(state, false, "module %s does not define '%s'", module->m_modname, name->data());
        return false;
    }
    nn_vmmac_tryraise(state, false, "'%s' of type %s does not have properties", neon::Value::toString(state, peeked)->data(),
        neon::Value::Typename(peeked));
    return false;
}

static NEON_FORCEINLINE bool nn_vmdo_propertyset(neon::State* state)
{
    neon::Value value;
    neon::Value vtarget;
    neon::Value vpeek;
    neon::ClassObject* klass;
    neon::String* name;
    neon::Dictionary* dict;
    neon::ClassInstance* instance;
    vtarget = state->stackPeek(1);
    if(!vtarget.isClass() && !vtarget.isInstance() && !vtarget.isDict())
    {
        state->raiseClass(state->m_exceptions.stdexception, "object of type %s cannot carry properties", neon::Value::Typename(vtarget));
        return false;
    }
    else if(state->stackPeek(0).isEmpty())
    {
        state->raiseClass(state->m_exceptions.stdexception, "empty cannot be assigned");
        return false;
    }
    name = state->readString();
    vpeek = state->stackPeek(0);
    if(vtarget.isClass())
    {
        klass = vtarget.asClass();
        if(vpeek.isCallable())
        {
            //fprintf(stderr, "setting '%s' as method\n", name->data());
            klass->defMethod(name->data(), vpeek);
        }
        else
        {
            klass->defProperty(name->data(), vpeek);
        }
        value = state->stackPop();
        /* removing the class object */
        state->stackPop();
        state->stackPush(value);
    }
    else if(vtarget.isInstance())
    {
        instance = vtarget.asInstance();
        instance->defProperty(name->data(), vpeek);
        value = state->stackPop();
        /* removing the instance object */
        state->stackPop();
        state->stackPush(value);
    }
    else
    {
        dict = vtarget.asDict();
        dict->setEntry(neon::Value::fromObject(name), vpeek);
        value = state->stackPop();
        /* removing the dictionary object */
        state->stackPop();
        state->stackPush(value);
    }
    return true;
}

static NEON_FORCEINLINE double nn_vmutil_valtonum(neon::Value v)
{
    if(v.isNull())
    {
        return 0;
    }
    if(v.isBool())
    {
        if(v.asBool())
        {
            return 1;
        }
        return 0;
    }
    return v.asNumber();
}


static NEON_FORCEINLINE uint32_t nn_vmutil_valtouint(neon::Value v)
{
    if(v.isNull())
    {
        return 0;
    }
    if(v.isBool())
    {
        if(v.asBool())
        {
            return 1;
        }
        return 0;
    }
    return v.asNumber();
}

static NEON_FORCEINLINE long nn_vmutil_valtoint(neon::Value v)
{
    return (long)nn_vmutil_valtonum(v);
}

static NEON_FORCEINLINE bool nn_vmdo_dobinary(neon::State* state)
{
    bool isfail;
    long ibinright;
    long ibinleft;
    uint32_t ubinright;
    uint32_t ubinleft;
    double dbinright;
    double dbinleft;
    neon::Instruction::OpCode instruction;
    neon::Value res;
    neon::Value binvalleft;
    neon::Value binvalright;
    instruction = (neon::Instruction::OpCode)state->m_vmstate.currentinstr.code;
    binvalright = state->stackPeek(0);
    binvalleft = state->stackPeek(1);
    isfail = (
        (!binvalright.isNumber() && !binvalright.isBool() && !binvalright.isNull()) ||
        (!binvalleft.isNumber() && !binvalleft.isBool() && !binvalleft.isNull())
    );
    if(isfail)
    {
        nn_vmmac_tryraise(state, false, "unsupported operand %s for %s and %s", neon::Instruction::opName(instruction), neon::Value::Typename(binvalleft), neon::Value::Typename(binvalright));
        return false;
    }
    binvalright = state->stackPop();
    binvalleft = state->stackPop();
    res = neon::Value::makeEmpty();
    switch(instruction)
    {
        case neon::Instruction::OP_PRIMADD:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = neon::Value::makeNumber(dbinleft + dbinright);
            }
            break;
        case neon::Instruction::OP_PRIMSUBTRACT:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = neon::Value::makeNumber(dbinleft - dbinright);
            }
            break;
        case neon::Instruction::OP_PRIMDIVIDE:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = neon::Value::makeNumber(dbinleft / dbinright);
            }
            break;
        case neon::Instruction::OP_PRIMMULTIPLY:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = neon::Value::makeNumber(dbinleft * dbinright);
            }
            break;
        case neon::Instruction::OP_PRIMAND:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = neon::Value::makeInt(ibinleft & ibinright);
            }
            break;
        case neon::Instruction::OP_PRIMOR:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = neon::Value::makeInt(ibinleft | ibinright);
            }
            break;
        case neon::Instruction::OP_PRIMBITXOR:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = neon::Value::makeInt(ibinleft ^ ibinright);
            }
            break;
        case neon::Instruction::OP_PRIMSHIFTLEFT:
            {
                /*
                via quickjs:
                    uint32_t v1;
                    uint32_t v2;
                    v1 = JS_VALUE_GET_INT(op1);
                    v2 = JS_VALUE_GET_INT(op2);
                    v2 &= 0x1f;
                    sp[-2] = JS_NewInt32(ctx, v1 << v2);
                */
                ubinright = nn_vmutil_valtouint(binvalright);
                ubinleft = nn_vmutil_valtouint(binvalleft);
                ubinright &= 0x1f;
                //res = neon::Value::makeInt(ibinleft << ibinright);
                res = neon::Value::makeInt(ubinleft << ubinright);

            }
            break;
        case neon::Instruction::OP_PRIMSHIFTRIGHT:
            {
                /*
                    uint32_t v2;
                    v2 = JS_VALUE_GET_INT(op2);
                    v2 &= 0x1f;
                    sp[-2] = JS_NewUint32(ctx, (uint32_t)JS_VALUE_GET_INT(op1) >> v2);
                */
                ubinright = nn_vmutil_valtouint(binvalright);
                ubinleft = nn_vmutil_valtouint(binvalleft);
                ubinright &= 0x1f;
                res = neon::Value::makeInt(ubinleft >> ubinright);
            }
            break;
        case neon::Instruction::OP_PRIMGREATER:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = neon::Value::makeBool(dbinleft > dbinright);
            }
            break;
        case neon::Instruction::OP_PRIMLESSTHAN:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = neon::Value::makeBool(dbinleft < dbinright);
            }
            break;
        default:
            {
                fprintf(stderr, "unhandled instruction %d (%s)!\n", instruction, neon::Instruction::opName(instruction));
                return false;
            }
            break;
    }
    state->stackPush(res);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_globaldefine(neon::State* state)
{
    neon::Value val;
    neon::String* name;
    neon::HashTable* tab;
    name = state->readString();
    val = state->stackPeek(0);
    if(val.isEmpty())
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    tab = state->m_vmstate.currentframe->closure->scriptfunc->m_inmodule->m_deftable;
    tab->set(neon::Value::fromObject(name), val);
    state->stackPop();
    #if (defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE) || 0
    state->m_definedglobals->printTo(state->m_debugprinter, "globals");
    #endif
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_globalget(neon::State* state)
{
    neon::String* name;
    neon::HashTable* tab;
    neon::Property* field;
    name = state->readString();
    tab = state->m_vmstate.currentframe->closure->scriptfunc->m_inmodule->m_deftable;
    field = tab->getByObjString(name);
    if(field == nullptr)
    {
        field = state->m_definedglobals->getByObjString(name);
        if(field == nullptr)
        {
            nn_vmmac_tryraise(state, false, "global name '%s' is not defined", name->data());
            return false;
        }
    }
    state->stackPush(field->m_actualval);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_globalset(neon::State* state)
{
    neon::String* name;
    neon::HashTable* table;
    if(state->stackPeek(0).isEmpty())
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    name = state->readString();
    table = state->m_vmstate.currentframe->closure->scriptfunc->m_inmodule->m_deftable;
    if(table->set(neon::Value::fromObject(name), state->stackPeek(0)))
    {
        if(state->m_conf.enablestrictmode)
        {
            table->removeByKey(neon::Value::fromObject(name));
            nn_vmmac_tryraise(state, false, "global name '%s' was not declared", name->data());
            return false;
        }
    }
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_localget(neon::State* state)
{
    size_t ssp;
    uint16_t slot;
    neon::Value val;
    slot = state->readShort();
    ssp = state->m_vmstate.currentframe->stackslotpos;
    val = state->m_vmstate.stackvalues[ssp + slot];
    state->stackPush(val);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_localset(neon::State* state)
{
    size_t ssp;
    uint16_t slot;
    neon::Value peeked;
    slot = state->readShort();
    peeked = state->stackPeek(0);
    if(peeked.isEmpty())
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    ssp = state->m_vmstate.currentframe->stackslotpos;
    state->m_vmstate.stackvalues[ssp + slot] = peeked;
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_funcargget(neon::State* state)
{
    size_t ssp;
    uint16_t slot;
    neon::Value val;
    slot = state->readShort();
    ssp = state->m_vmstate.currentframe->stackslotpos;
    //fprintf(stderr, "FUNCARGGET: %s\n", state->m_vmstate.currentframe->closure->scriptfunc->m_scriptfnname->data());
    val = state->m_vmstate.stackvalues[ssp + slot];
    state->stackPush(val);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_funcargset(neon::State* state)
{
    size_t ssp;
    uint16_t slot;
    neon::Value peeked;
    slot = state->readShort();
    peeked = state->stackPeek(0);
    if(peeked.isEmpty())
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    ssp = state->m_vmstate.currentframe->stackslotpos;
    state->m_vmstate.stackvalues[ssp + slot] = peeked;
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_makeclosure(neon::State* state)
{
    int i;
    int index;
    size_t ssp;
    uint8_t islocal;
    neon::Value* upvals;
    neon::FuncScript* function;
    neon::FuncClosure* closure;
    function = state->readConst().asFuncScript();
    closure = neon::FuncClosure::make(state, function);
    state->stackPush(neon::Value::fromObject(closure));
    for(i = 0; i < closure->m_upvalcount; i++)
    {
        islocal = state->readByte();
        index = state->readShort();
        if(islocal)
        {
            ssp = state->m_vmstate.currentframe->stackslotpos;
            upvals = &state->m_vmstate.stackvalues[ssp + index];
            closure->m_storedupvals[i] = nn_vmutil_upvaluescapture(state, upvals, index);

        }
        else
        {
            closure->m_storedupvals[i] = state->m_vmstate.currentframe->closure->m_storedupvals[index];
        }
    }
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_makearray(neon::State* state)
{
    int i;
    int count;
    neon::Array* array;
    count = state->readShort();
    array = neon::Array::make(state);
    state->m_vmstate.stackvalues[state->m_vmstate.stackidx + (-count - 1)] = neon::Value::fromObject(array);
    for(i = count - 1; i >= 0; i--)
    {
        array->push(state->stackPeek(i));
    }
    state->stackPop(count);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_makedict(neon::State* state)
{
    int i;
    int count;
    int realcount;
    neon::Value name;
    neon::Value value;
    neon::Dictionary* dict;
    /* 1 for key, 1 for value */
    realcount = state->readShort();
    count = realcount * 2;
    dict = neon::Dictionary::make(state);
    state->m_vmstate.stackvalues[state->m_vmstate.stackidx + (-count - 1)] = neon::Value::fromObject(dict);
    for(i = 0; i < count; i += 2)
    {
        name = state->m_vmstate.stackvalues[state->m_vmstate.stackidx + (-count + i)];
        if(!name.isString() && !name.isNumber() && !name.isBool())
        {
            nn_vmmac_tryraise(state, false, "dictionary key must be one of string, number or boolean");
            return false;
        }
        value = state->m_vmstate.stackvalues[state->m_vmstate.stackidx + (-count + i + 1)];
        dict->setEntry(name, value);
    }
    state->stackPop(count);
    return true;
}

#define BINARY_MOD_OP(state, type, op) \
    do \
    { \
        double dbinright; \
        double dbinleft; \
        neon::Value binvalright; \
        neon::Value binvalleft; \
        binvalright = state->stackPeek(0); \
        binvalleft = state->stackPeek(1);\
        if((!binvalright.isNumber() && !binvalright.isBool()) \
        || (!binvalleft.isNumber() && !binvalleft.isBool())) \
        { \
            nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "unsupported operand %s for %s and %s", #op, neon::Value::Typename(binvalleft), neon::Value::Typename(binvalright)); \
            break; \
        } \
        binvalright = state->stackPop(); \
        dbinright = binvalright.isBool() ? (binvalright.asBool() ? 1 : 0) : binvalright.asNumber(); \
        binvalleft = state->stackPop(); \
        dbinleft = binvalleft.isBool() ? (binvalleft.asBool() ? 1 : 0) : binvalleft.asNumber(); \
        state->stackPush(type(op(dbinleft, dbinright))); \
    } while(false)


neon::Status neon::State::runVM(int exitframe, neon::Value* rv)
{
    int iterpos;
    int printpos;
    int ofs;
    /*
    * this variable is a NOP; it only exists to ensure that functions outside of the
    * switch tree are not calling nn_vmmac_exitvm(), as its behavior could be undefined.
    */
    bool you_are_calling_exit_vm_outside_of_runvm;
    neon::Value* dbgslot;
    neon::Instruction currinstr;
    neon::Color nc;
    you_are_calling_exit_vm_outside_of_runvm = false;
    m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
    DebugPrinter dp(this, m_debugprinter);
    for(;;)
    {
        /*
        // try...finally... (i.e. try without a catch but finally
        // whose try body raises an exception)
        // can cause us to go into an invalid mode where frame count == 0
        // to fix this, we need to exit with an appropriate mode here.
        */
        if(m_vmstate.framecount == 0)
        {
            return neon::Status::FAIL_RUNTIME;
        }
        if(m_conf.dumpinstructions)
        {
            ofs = (int)(m_vmstate.currentframe->inscode - m_vmstate.currentframe->closure->scriptfunc->m_compiledblob->m_instrucs);
            dp.printInstructionAt(m_vmstate.currentframe->closure->scriptfunc->m_compiledblob, ofs, false);
            if(m_conf.dumpprintstack)
            {
                fprintf(stderr, "stack (before)=[\n");
                iterpos = 0;
                for(dbgslot = m_vmstate.stackvalues; dbgslot < &m_vmstate.stackvalues[m_vmstate.stackidx]; dbgslot++)
                {
                    printpos = iterpos + 1;
                    iterpos++;
                    fprintf(stderr, "  [%s%d%s] ", nc.color('y'), printpos, nc.color('0'));
                    m_debugprinter->putformat("%s", nc.color('y'));
                    m_debugprinter->printValue(*dbgslot, true, false);
                    m_debugprinter->putformat("%s", nc.color('0'));
                    fprintf(stderr, "\n");
                }
                fprintf(stderr, "]\n");
            }
        }
        currinstr = readInstruction();
        m_vmstate.currentinstr = currinstr;
        //fprintf(stderr, "now executing at line %d\n", m_vmstate.currentinstr.srcline);
        switch(currinstr.code)
        {
            case neon::Instruction::OP_RETURN:
                {
                    size_t ssp;
                    neon::Value result;
                    result = stackPop();
                    if(rv != nullptr)
                    {
                        *rv = result;
                    }
                    ssp = m_vmstate.currentframe->stackslotpos;
                    nn_vmutil_upvaluesclose(this, &m_vmstate.stackvalues[ssp]);
                    m_vmstate.framecount--;
                    if(m_vmstate.framecount == 0)
                    {
                        stackPop();
                        return neon::Status::OK;
                    }
                    ssp = m_vmstate.currentframe->stackslotpos;
                    m_vmstate.stackidx = ssp;
                    stackPush(result);
                    m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                    if(m_vmstate.framecount == (size_t)exitframe)
                    {
                        return neon::Status::OK;
                    }
                }
                break;
            case neon::Instruction::OP_PUSHCONSTANT:
                {
                    neon::Value constant;
                    constant = readConst();
                    stackPush(constant);
                }
                break;
            case neon::Instruction::OP_PRIMADD:
                {
                    neon::Value valright;
                    neon::Value valleft;
                    neon::Value result;
                    valright = stackPeek(0);
                    valleft = stackPeek(1);
                    if(valright.isString() || valleft.isString())
                    {
                        if(!nn_vmutil_concatenate(this, false))
                        {
                            nn_vmmac_tryraise(this, neon::Status::FAIL_RUNTIME, "unsupported operand + for %s and %s", neon::Value::Typename(valleft), neon::Value::Typename(valright));
                            break;
                        }
                    }
                    else if(valleft.isArray() && valright.isArray())
                    {
                        result = neon::Value::fromObject(nn_vmutil_combinearrays(this, valleft.asArray(), valright.asArray()));
                        stackPop(2);
                        stackPush(result);
                    }
                    else
                    {
                        nn_vmdo_dobinary(this);
                    }
                }
                break;
            case neon::Instruction::OP_PRIMSUBTRACT:
                {
                    nn_vmdo_dobinary(this);
                }
                break;
            case neon::Instruction::OP_PRIMMULTIPLY:
                {
                    int intnum;
                    double dbnum;
                    neon::Value peekleft;
                    neon::Value peekright;
                    neon::Value result;
                    neon::String* string;
                    neon::Array* list;
                    neon::Array* newlist;
                    peekright = stackPeek(0);
                    peekleft = stackPeek(1);
                    if(peekleft.isString() && peekright.isNumber())
                    {
                        dbnum = peekright.asNumber();
                        string = stackPeek(1).asString();
                        result = neon::Value::fromObject(nn_vmutil_multiplystring(this, string, dbnum));
                        stackPop(2);
                        stackPush(result);
                        break;
                    }
                    else if(peekleft.isArray() && peekright.isNumber())
                    {
                        intnum = (int)peekright.asNumber();
                        stackPop();
                        list = peekleft.asArray();
                        newlist = neon::Array::make(this);
                        stackPush(neon::Value::fromObject(newlist));
                        nn_vmutil_multiplyarray(this, list, newlist, intnum);
                        stackPop(2);
                        stackPush(neon::Value::fromObject(newlist));
                        break;
                    }
                    nn_vmdo_dobinary(this);
                }
                break;
            case neon::Instruction::OP_PRIMDIVIDE:
                {
                    nn_vmdo_dobinary(this);
                }
                break;
            case neon::Instruction::OP_PRIMMODULO:
                {
                    BINARY_MOD_OP(this, neon::Value::makeNumber, nn_vmutil_modulo);
                }
                break;
            case neon::Instruction::OP_PRIMPOW:
                {
                    BINARY_MOD_OP(this, neon::Value::makeNumber, pow);
                }
                break;
            case neon::Instruction::OP_PRIMFLOORDIVIDE:
                {
                    BINARY_MOD_OP(this, neon::Value::makeNumber, nn_vmutil_floordiv);
                }
                break;
            case neon::Instruction::OP_PRIMNEGATE:
                {
                    neon::Value peeked;
                    peeked = stackPeek(0);
                    if(!peeked.isNumber())
                    {
                        nn_vmmac_tryraise(this, neon::Status::FAIL_RUNTIME, "operator - not defined for object of type %s", neon::Value::Typename(peeked));
                        break;
                    }
                    stackPush(neon::Value::makeNumber(-stackPop().asNumber()));
                }
                break;
            case neon::Instruction::OP_PRIMBITNOT:
            {
                neon::Value peeked;
                peeked = stackPeek(0);
                if(!peeked.isNumber())
                {
                    nn_vmmac_tryraise(this, neon::Status::FAIL_RUNTIME, "operator ~ not defined for object of type %s", neon::Value::Typename(peeked));
                    break;
                }
                stackPush(neon::Value::makeInt(~((int)stackPop().asNumber())));
                break;
            }
            case neon::Instruction::OP_PRIMAND:
                {
                    nn_vmdo_dobinary(this);
                }
                break;
            case neon::Instruction::OP_PRIMOR:
                {
                    nn_vmdo_dobinary(this);
                }
                break;
            case neon::Instruction::OP_PRIMBITXOR:
                {
                    nn_vmdo_dobinary(this);
                }
                break;
            case neon::Instruction::OP_PRIMSHIFTLEFT:
                {
                    nn_vmdo_dobinary(this);
                }
                break;
            case neon::Instruction::OP_PRIMSHIFTRIGHT:
                {
                    nn_vmdo_dobinary(this);
                }
                break;
            case neon::Instruction::OP_PUSHONE:
                {
                    stackPush(neon::Value::makeNumber(1));
                }
                break;
            /* comparisons */
            case neon::Instruction::OP_EQUAL:
                {
                    neon::Value a;
                    neon::Value b;
                    b = stackPop();
                    a = stackPop();
                    stackPush(neon::Value::makeBool(a.compare(this, b)));
                }
                break;
            case neon::Instruction::OP_PRIMGREATER:
                {
                    nn_vmdo_dobinary(this);
                }
                break;
            case neon::Instruction::OP_PRIMLESSTHAN:
                {
                    nn_vmdo_dobinary(this);
                }
                break;
            case neon::Instruction::OP_PRIMNOT:
                {
                    stackPush(neon::Value::makeBool(neon::Value::isFalse(stackPop())));
                }
                break;
            case neon::Instruction::OP_PUSHNULL:
                {
                    stackPush(neon::Value::makeNull());
                }
                break;
            case neon::Instruction::OP_PUSHEMPTY:
                {
                    stackPush(neon::Value::makeEmpty());
                }
                break;
            case neon::Instruction::OP_PUSHTRUE:
                {
                    stackPush(neon::Value::makeBool(true));
                }
                break;
            case neon::Instruction::OP_PUSHFALSE:
                {
                    stackPush(neon::Value::makeBool(false));
                }
                break;

            case neon::Instruction::OP_JUMPNOW:
                {
                    uint16_t offset;
                    offset = readShort();
                    m_vmstate.currentframe->inscode += offset;
                }
                break;
            case neon::Instruction::OP_JUMPIFFALSE:
                {
                    uint16_t offset;
                    offset = readShort();
                    if(neon::Value::isFalse(stackPeek(0)))
                    {
                        m_vmstate.currentframe->inscode += offset;
                    }
                }
                break;
            case neon::Instruction::OP_LOOP:
                {
                    uint16_t offset;
                    offset = readShort();
                    m_vmstate.currentframe->inscode -= offset;
                }
                break;
            case neon::Instruction::OP_ECHO:
                {
                    neon::Value val;
                    val = stackPeek(0);
                    m_stdoutprinter->printValue(val, m_isreplmode, true);
                    if(!val.isEmpty())
                    {
                        m_stdoutprinter->put("\n");
                    }
                    stackPop();
                }
                break;
            case neon::Instruction::OP_STRINGIFY:
                {
                    neon::Value peeked;
                    neon::String* value;
                    peeked = stackPeek(0);
                    if(!peeked.isString() && !peeked.isNull())
                    {
                        value = neon::Value::toString(this, stackPop());
                        if(value->length() != 0)
                        {
                            stackPush(neon::Value::fromObject(value));
                        }
                        else
                        {
                            stackPush(neon::Value::makeNull());
                        }
                    }
                }
                break;
            case neon::Instruction::OP_DUPONE:
                {
                    stackPush(stackPeek(0));
                }
                break;
            case neon::Instruction::OP_POPONE:
                {
                    stackPop();
                }
                break;
            case neon::Instruction::OP_POPN:
                {
                    stackPop(readShort());
                }
                break;
            case neon::Instruction::OP_UPVALUECLOSE:
                {
                    nn_vmutil_upvaluesclose(this, &m_vmstate.stackvalues[m_vmstate.stackidx - 1]);
                    stackPop();
                }
                break;
            case neon::Instruction::OP_GLOBALDEFINE:
                {
                    if(!nn_vmdo_globaldefine(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_GLOBALGET:
                {
                    if(!nn_vmdo_globalget(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_GLOBALSET:
                {
                    if(!nn_vmdo_globalset(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_LOCALGET:
                {
                    if(!nn_vmdo_localget(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_LOCALSET:
                {
                    if(!nn_vmdo_localset(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_FUNCARGGET:
                {
                    if(!nn_vmdo_funcargget(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_FUNCARGSET:
                {
                    if(!nn_vmdo_funcargset(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;

            case neon::Instruction::OP_PROPERTYGET:
                {
                    if(!nn_vmdo_propertyget(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_PROPERTYSET:
                {
                    if(!nn_vmdo_propertyset(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_PROPERTYGETSELF:
                {
                    if(!nn_vmdo_propertygetself(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_MAKECLOSURE:
                {
                    if(!nn_vmdo_makeclosure(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_UPVALUEGET:
                {
                    int index;
                    neon::FuncClosure* closure;
                    index = readShort();
                    closure = m_vmstate.currentframe->closure;
                    if(index < closure->m_upvalcount)
                    {
                        stackPush(closure->m_storedupvals[index]->m_location);
                    }
                    else
                    {
                        stackPush(neon::Value::makeEmpty());
                    }
                }
                break;
            case neon::Instruction::OP_UPVALUESET:
                {
                    int index;
                    index = readShort();
                    if(stackPeek(0).isEmpty())
                    {
                        nn_vmmac_tryraise(this, neon::Status::FAIL_RUNTIME, "empty cannot be assigned");
                        break;
                    }
                    m_vmstate.currentframe->closure->m_storedupvals[index]->m_location = stackPeek(0);
                }
                break;
            case neon::Instruction::OP_CALLFUNCTION:
                {
                    int argcount;
                    argcount = readByte();
                    if(!nn_vm_callvalue(this, stackPeek(argcount), neon::Value::makeEmpty(), argcount))
                    {
                        nn_vmmac_exitvm(this);
                    }
                    m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                }
                break;
            case neon::Instruction::OP_CALLMETHOD:
                {
                    int argcount;
                    neon::String* method;
                    method = readString();
                    argcount = readByte();
                    if(!nn_vmutil_invokemethod(this, method, argcount))
                    {
                        nn_vmmac_exitvm(this);
                    }
                    m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                }
                break;
            case neon::Instruction::OP_CLASSINVOKETHIS:
                {
                    int argcount;
                    neon::String* method;
                    method = readString();
                    argcount = readByte();
                    if(!nn_vmutil_invokemethodself(this, method, argcount))
                    {
                        nn_vmmac_exitvm(this);
                    }
                    m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                }
                break;
            case neon::Instruction::OP_MAKECLASS:
                {
                    bool haveval;
                    neon::Value pushme;
                    neon::String* name;
                    neon::ClassObject* klass;
                    neon::Property* field;
                    haveval = false;
                    name = readString();
                    field = m_vmstate.currentframe->closure->scriptfunc->m_inmodule->m_deftable->getByObjString(name);
                    if(field != nullptr)
                    {
                        if(field->m_actualval.isClass())
                        {
                            haveval = true;
                            pushme = field->m_actualval;
                        }
                    }
                    field = m_definedglobals->getByObjString(name);
                    if(field != nullptr)
                    {
                        if(field->m_actualval.isClass())
                        {
                            haveval = true;
                            pushme = field->m_actualval;
                        }
                    }
                    if(!haveval)
                    {
                        klass = neon::ClassObject::make(this, name);
                        pushme = neon::Value::fromObject(klass);
                    }
                    stackPush(pushme);
                }
                break;
            case neon::Instruction::OP_MAKEMETHOD:
                {
                    neon::String* name;
                    name = readString();
                    nn_vmutil_definemethod(this, name);
                }
                break;
            case neon::Instruction::OP_CLASSPROPERTYDEFINE:
                {
                    int isstatic;
                    neon::String* name;
                    name = readString();
                    isstatic = readByte();
                    nn_vmutil_defineproperty(this, name, isstatic == 1);
                }
                break;
            case neon::Instruction::OP_CLASSINHERIT:
                {
                    neon::ClassObject* superclass;
                    neon::ClassObject* subclass;
                    if(!stackPeek(1).isClass())
                    {
                        nn_vmmac_tryraise(this, neon::Status::FAIL_RUNTIME, "cannot inherit from non-class object");
                        break;
                    }
                    superclass = stackPeek(1).asClass();
                    subclass = stackPeek(0).asClass();
                    subclass->inheritFrom(superclass);
                    /* pop the subclass */
                    stackPop();
                }
                break;
            case neon::Instruction::OP_CLASSGETSUPER:
                {
                    neon::ClassObject* klass;
                    neon::String* name;
                    name = readString();
                    klass = stackPeek(0).asClass();
                    if(!nn_vmutil_bindmethod(this, klass->m_superclass, name))
                    {
                        nn_vmmac_tryraise(this, neon::Status::FAIL_RUNTIME, "class %s does not define a function %s", klass->m_classname->data(), name->data());
                    }
                }
                break;
            case neon::Instruction::OP_CLASSINVOKESUPER:
                {
                    int argcount;
                    neon::ClassObject* klass;
                    neon::String* method;
                    method = readString();
                    argcount = readByte();
                    klass = stackPop().asClass();
                    if(!nn_vmutil_invokemethodfromclass(this, klass, method, argcount))
                    {
                        nn_vmmac_exitvm(this);
                    }
                    m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                }
                break;
            case neon::Instruction::OP_CLASSINVOKESUPERSELF:
                {
                    int argcount;
                    neon::ClassObject* klass;
                    argcount = readByte();
                    klass = stackPop().asClass();
                    if(!nn_vmutil_invokemethodfromclass(this, klass, m_constructorname, argcount))
                    {
                        nn_vmmac_exitvm(this);
                    }
                    m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                }
                break;
            case neon::Instruction::OP_MAKEARRAY:
                {
                    if(!nn_vmdo_makearray(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_MAKERANGE:
                {
                    double lower;
                    double upper;
                    neon::Value vupper;
                    neon::Value vlower;
                    vupper = stackPeek(0);
                    vlower = stackPeek(1);
                    if(!vupper.isNumber() || !vlower.isNumber())
                    {
                        nn_vmmac_tryraise(this, neon::Status::FAIL_RUNTIME, "invalid range boundaries");
                        break;
                    }
                    lower = vlower.asNumber();
                    upper = vupper.asNumber();
                    stackPop(2);
                    stackPush(neon::Value::fromObject(neon::Range::make(this, lower, upper)));
                }
                break;
            case neon::Instruction::OP_MAKEDICT:
                {
                    if(!nn_vmdo_makedict(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_INDEXGETRANGED:
                {
                    if(!nn_vmdo_getrangedindex(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_INDEXGET:
                {
                    if(!nn_vmdo_indexget(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_INDEXSET:
                {
                    if(!nn_vmdo_indexset(this))
                    {
                        nn_vmmac_exitvm(this);
                    }
                }
                break;
            case neon::Instruction::OP_IMPORTIMPORT:
                {
                    neon::Value res;
                    neon::String* name;
                    neon::Module* mod;
                    name = stackPeek(0).asString();
                    fprintf(stderr, "IMPORTIMPORT: name='%s'\n", name->data());
                    mod = neon::Module::loadModuleByName(this, m_toplevelmodule, name);
                    fprintf(stderr, "IMPORTIMPORT: mod='%p'\n", mod);
                    if(mod == nullptr)
                    {
                        res = neon::Value::makeNull();
                    }
                    else
                    {
                        res = neon::Value::fromObject(mod);
                    }
                    stackPush(res);
                }
                break;
            case neon::Instruction::OP_TYPEOF:
                {
                    neon::Value res;
                    neon::Value thing;
                    const char* result;
                    thing = stackPop();
                    result = neon::Value::Typename(thing);
                    res = neon::Value::fromObject(neon::String::copy(this, result));
                    stackPush(res);
                }
                break;
            case neon::Instruction::OP_ASSERT:
                {
                    neon::Value message;
                    neon::Value expression;
                    message = stackPop();
                    expression = stackPop();
                    if(neon::Value::isFalse(expression))
                    {
                        if(!message.isNull())
                        {
                            raiseClass(m_exceptions.asserterror, neon::Value::toString(this, message)->data());
                        }
                        else
                        {
                            raiseClass(m_exceptions.asserterror, "assertion failed");
                        }
                    }
                }
                break;
            case neon::Instruction::OP_EXTHROW:
                {
                    bool isok;
                    neon::Value peeked;
                    neon::Value stacktrace;
                    neon::ClassInstance* instance;
                    peeked = stackPeek(0);
                    isok = (
                        peeked.isInstance() ||
                        nn_util_isinstanceof(peeked.asInstance()->m_fromclass, m_exceptions.stdexception)
                    );
                    if(!isok)
                    {
                        nn_vmmac_tryraise(this, neon::Status::FAIL_RUNTIME, "instance of Exception expected");
                        break;
                    }
                    stacktrace = getExceptionStacktrace();
                    instance = peeked.asInstance();
                    instance->defProperty("stacktrace", stacktrace);
                    if(exceptionPropagate())
                    {
                        m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                        break;
                    }
                    nn_vmmac_exitvm(this);
                }
            case neon::Instruction::OP_EXTRY:
                {
                    uint16_t addr;
                    uint16_t finaddr;
                    neon::Value value;
                    neon::String* type;
                    type = readString();
                    addr = readShort();
                    finaddr = readShort();
                    if(addr != 0)
                    {
                        if(!m_definedglobals->get(neon::Value::fromObject(type), &value) || !value.isClass())
                        {
                            if(!m_vmstate.currentframe->closure->scriptfunc->m_inmodule->m_deftable->get(neon::Value::fromObject(type), &value) || !value.isClass())
                            {
                                nn_vmmac_tryraise(this, neon::Status::FAIL_RUNTIME, "object of type '%s' is not an exception", type->data());
                                break;
                            }
                        }
                        exceptionPushHandler(value.asClass(), addr, finaddr);
                    }
                    else
                    {
                        exceptionPushHandler(nullptr, addr, finaddr);
                    }
                }
                break;
            case neon::Instruction::OP_EXPOPTRY:
                {
                    m_vmstate.currentframe->handlercount--;
                }
                break;
            case neon::Instruction::OP_EXPUBLISHTRY:
                {
                    m_vmstate.currentframe->handlercount--;
                    if(exceptionPropagate())
                    {
                        m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                        break;
                    }
                    nn_vmmac_exitvm(this);
                }
                break;
            case neon::Instruction::OP_SWITCH:
                {
                    neon::Value expr;
                    neon::Value value;
                    neon::VarSwitch* sw;
                    sw = readConst().asSwitch();
                    expr = stackPeek(0);
                    if(sw->m_jumppositions->get(expr, &value))
                    {
                        m_vmstate.currentframe->inscode += (int)value.asNumber();
                    }
                    else if(sw->m_defaultjump != -1)
                    {
                        m_vmstate.currentframe->inscode += sw->m_defaultjump;
                    }
                    else
                    {
                        m_vmstate.currentframe->inscode += sw->m_exitjump;
                    }
                    stackPop();
                }
                break;
            default:
                {
                }
                break;
        }

    }
}

static char* nn_cli_getinput(const char* prompt)
{
    enum { kMaxLineSize = 1024 };
    size_t len;
    char* rt;
    char rawline[kMaxLineSize+1] = {0};
    fprintf(stdout, "%s", prompt);
    fflush(stdout);
    rt = fgets(rawline, kMaxLineSize, stdin);
    len = strlen(rt);
    rt[len - 1] = 0;
    return rt;
}

static void nn_cli_addhistoryline(const char* line)
{
    (void)line;
}

static void nn_cli_freeline(char* line)
{
    (void)line;
}

#if !defined(NEON_PLAT_ISWASM)
static bool nn_cli_repl(neon::State* state)
{
    int i;
    int linelength;
    int bracecount;
    int parencount;
    int bracketcount;
    int doublequotecount;
    int singlequotecount;
    bool continuerepl;
    char* line;
    neon::Util::StrBuffer source;
    const char* cursor;
    neon::Value dest;
    state->m_isreplmode = true;
    continuerepl = true;
    printf("Type \".exit\" to quit or \".credits\" for more information\n");
    bracecount = 0;
    parencount = 0;
    bracketcount = 0;
    singlequotecount = 0;
    doublequotecount = 0;
    while(true)
    {
        if(!continuerepl)
        {
            bracecount = 0;
            parencount = 0;
            bracketcount = 0;
            singlequotecount = 0;
            doublequotecount = 0;
            source.reset();
            continuerepl = true;
        }
        cursor = "%> ";
        if(bracecount > 0 || bracketcount > 0 || parencount > 0)
        {
            cursor = ".. ";
        }
        else if(singlequotecount == 1 || doublequotecount == 1)
        {
            cursor = "";
        }
        line = nn_cli_getinput(cursor);
        fprintf(stderr, "line = %s. isexit=%d\n", line, strcmp(line, ".exit"));
        if(line == nullptr || strcmp(line, ".exit") == 0)
        {
            source.destroy();
            return true;
        }
        linelength = (int)strlen(line);
        if(strcmp(line, ".credits") == 0)
        {
            printf("\n" NEON_INFO_COPYRIGHT "\n\n");
            source.reset();
            continue;
        }
        nn_cli_addhistoryline(line);
        if(linelength > 0 && line[0] == '#')
        {
            continue;
        }
        /* find count of { and }, ( and ), [ and ], " and ' */
        for(i = 0; i < linelength; i++)
        {
            if(line[i] == '{')
            {
                bracecount++;
            }
            if(line[i] == '(')
            {
                parencount++;
            }
            if(line[i] == '[')
            {
                bracketcount++;
            }
            if(line[i] == '\'' && doublequotecount == 0)
            {
                if(singlequotecount == 0)
                {
                    singlequotecount++;
                }
                else
                {
                    singlequotecount--;
                }
            }
            if(line[i] == '"' && singlequotecount == 0)
            {
                if(doublequotecount == 0)
                {
                    doublequotecount++;
                }
                else
                {
                    doublequotecount--;
                }
            }
            if(line[i] == '\\' && (singlequotecount > 0 || doublequotecount > 0))
            {
                i++;
            }
            if(line[i] == '}' && bracecount > 0)
            {
                bracecount--;
            }
            if(line[i] == ')' && parencount > 0)
            {
                parencount--;
            }
            if(line[i] == ']' && bracketcount > 0)
            {
                bracketcount--;
            }
        }
        source.append(line);
        if(linelength > 0)
        {
            source.append("\n");
        }
        nn_cli_freeline(line);
        if(bracketcount == 0 && parencount == 0 && bracecount == 0 && singlequotecount == 0 && doublequotecount == 0)
        {
            state->execSource(state->m_toplevelmodule, source.m_data, &dest);
            fflush(stdout);
            continuerepl = false;
        }
    }
    return true;
}
#endif

static bool nn_cli_runfile(neon::State* state, const char* file)
{
    size_t fsz;
    char* rp;
    char* source;
    const char* oldfile;
    neon::Status result;
    source = neon::Util::fileReadFile(state, file, &fsz);
    if(source == nullptr)
    {
        oldfile = file;
        source = neon::Util::fileReadFile(state, file, &fsz);
        if(source == nullptr)
        {
            fprintf(stderr, "failed to read from '%s': %s\n", oldfile, strerror(errno));
            return false;
        }
    }
    state->m_rootphysfile = (char*)file;
    rp = osfn_realpath(file, nullptr);
    state->m_toplevelmodule->m_physlocation = neon::String::copy(state, rp);
    neon::Memory::osFree(rp);
    result = state->execSource(state->m_toplevelmodule, source, nullptr);
    neon::Memory::osFree(source);
    fflush(stdout);
    if(result == neon::Status::FAIL_COMPILE)
    {
        return false;
    }
    if(result == neon::Status::FAIL_RUNTIME)
    {
        return false;
    }
    return true;
}

static bool nn_cli_runcode(neon::State* state, char* source)
{
    neon::Status result;
    state->m_rootphysfile = nullptr;
    result = state->execSource(state->m_toplevelmodule, source, nullptr);
    fflush(stdout);
    if(result == neon::Status::FAIL_COMPILE)
    {
        return false;
    }
    if(result == neon::Status::FAIL_RUNTIME)
    {
        return false;
    }
    return true;
}

#if defined(NEON_PLAT_ISWASM)
int __multi3(int a, int b)
{
    return a*b;
}
#endif

int nn_util_findfirstpos(const char* str, int len, int ch)
{
    int i;
    for(i=0; i<len; i++)
    {
        if(str[i] == ch)
        {
            return i;
        }
    }
    return -1;
}

void nn_cli_parseenv(neon::State* state, char** envp)
{
    enum { kMaxKeyLen = 40 };
    int i;
    int len;
    int pos;
    char* raw;
    char* valbuf;
    char keybuf[kMaxKeyLen];
    neon::String* oskey;
    neon::String* osval;
    for(i=0; envp[i] != nullptr; i++)
    {
        raw = envp[i];
        len = strlen(raw);
        pos = nn_util_findfirstpos(raw, len, '=');
        if(pos == -1)
        {
            fprintf(stderr, "malformed environment string '%s'\n", raw);
        }
        else
        {
            memset(keybuf, 0, kMaxKeyLen);
            memcpy(keybuf, raw, pos);
            valbuf = &raw[pos+1];
            oskey = neon::String::copy(state, keybuf);
            osval = neon::String::copy(state, valbuf);
            state->m_envdict->setEntry(neon::Value::fromObject(oskey), neon::Value::fromObject(osval));
        }
    }
}

void nn_cli_printtypesizes()
{
    #define ptyp(t) \
        { \
            fprintf(stdout, "%d\t%s\n", (int)sizeof(t), #t); \
            fflush(stdout); \
        }
    ptyp(neon::Printer);
    ptyp(neon::Value);
    ptyp(neon::Object);
    ptyp(neon::Property::GetSetter);
    ptyp(neon::Property);
    ptyp(neon::ValArray);
    ptyp(neon::Blob);
    ptyp(neon::HashTable::Entry);
    ptyp(neon::HashTable);
    ptyp(neon::String);
    ptyp(neon::ScopeUpvalue);
    ptyp(neon::Module);
    ptyp(neon::FuncScript);
    ptyp(neon::FuncClosure);
    ptyp(neon::ClassObject);
    ptyp(neon::ClassInstance);
    ptyp(neon::FuncBound);
    ptyp(neon::FuncNative);
    ptyp(neon::Array);
    ptyp(neon::Range);
    ptyp(neon::Dictionary);
    ptyp(neon::File);
    ptyp(neon::VarSwitch);
    ptyp(neon::Userdata);
    ptyp(neon::State::ExceptionFrame);
    ptyp(neon::State::CallFrame);
    ptyp(neon::State);
    ptyp(neon::Token);
    ptyp(neon::Lexer);
    ptyp(neon::Parser::CompiledLocal);
    ptyp(neon::Parser::CompiledUpvalue);
    ptyp(neon::Parser::FuncCompiler);
    ptyp(neon::Parser::ClassCompiler);
    ptyp(neon::Parser);
    ptyp(neon::Parser::Rule);
    ptyp(neon::RegModule::FuncInfo);
    ptyp(neon::RegModule::FieldInfo);
    ptyp(neon::RegModule::ClassInfo);
    ptyp(neon::RegModule);
    ptyp(neon::Instruction)
    #undef ptyp
}

using OptionParser = neon::Util::OptionParser;

void nn_cli_fprintmaybearg(FILE* out, const char* begin, const char* flagname, size_t flaglen, bool needval, bool maybeval, const char* delim)
{
    fprintf(out, "%s%.*s", begin, (int)flaglen, flagname);
    if(needval)
    {
        if(maybeval)
        {
            fprintf(out, "[");
        }
        if(delim != nullptr)
        {
            fprintf(out, "%s", delim);
        }
        fprintf(out, "<val>");
        if(maybeval)
        {
            fprintf(out, "]");
        }
    }
}

void nn_cli_fprintusage(FILE* out, OptionParser::LongFlags* flags)
{
    size_t i;
    char ch;
    bool needval;
    bool maybeval;
    bool hadshort;
    OptionParser::LongFlags* flag;
    for(i=0; flags[i].longname != nullptr; i++)
    {
        flag = &flags[i];
        hadshort = false;
        needval = (flag->argtype > OptionParser::A_NONE);
        maybeval = (flag->argtype == OptionParser::A_OPTIONAL);
        if(flag->shortname > 0)
        {
            hadshort = true;
            ch = flag->shortname;
            fprintf(out, "    ");
            nn_cli_fprintmaybearg(out, "-", &ch, 1, needval, maybeval, nullptr);
        }
        if(flag->longname != nullptr)
        {
            if(hadshort)
            {
                fprintf(out, ", ");
            }
            else
            {
                fprintf(out, "    ");
            }
            nn_cli_fprintmaybearg(out, "--", flag->longname, strlen(flag->longname), needval, maybeval, "=");
        }
        if(flag->helptext != nullptr)
        {
            fprintf(out, "  -  %s", flag->helptext);
        }
        fprintf(out, "\n");
    }
}

void nn_cli_showusage(char* argv[], OptionParser::LongFlags* flags, bool fail)
{
    FILE* out;
    out = fail ? stderr : stdout;
    fprintf(out, "Usage: %s [<options>] [<filename> | -e <code>]\n", argv[0]);
    nn_cli_fprintusage(out, flags);
}


int main(int argc, char* argv[], char** envp)
{
    int i;
    int co;
    int opt;
    int nargc;
    int longindex;
    int nextgcstart;
    bool ok;
    bool wasusage;
    bool quitafterinit;
    char *arg;
    char* source;
    const char* filename;
    char* nargv[128];
    neon::State* state;
    ok = true;
    wasusage = false;
    quitafterinit = false;
    source = nullptr;
    nextgcstart = NEON_CFG_DEFAULTGCSTART;
    state = neon::Memory::create<neon::State>();
    OptionParser::LongFlags longopts[] =
    {
        {"help", 'h', OptionParser::A_NONE, "this help"},
        {"strict", 's', OptionParser::A_NONE, "enable strict mode, such as requiring explicit var declarations"},
        {"warn", 'w', OptionParser::A_NONE, "enable warnings"},
        {"debug", 'd', OptionParser::A_NONE, "enable debugging: print instructions and stack values during execution"},
        {"skipstack", 'D', OptionParser::A_NONE, "skip printing stack values when dumping"},
        {"exitaftercompile", 'x', OptionParser::A_NONE, "when using '-d', quit after printing compiled function(s)"},
        {"eval", 'e', OptionParser::A_REQUIRED, "evaluate a single line of code"},
        {"quit", 'q', OptionParser::A_NONE, "initiate, then immediately destroy the interpreter state"},
        {"types", 't', OptionParser::A_NONE, "print sizeof() of types"},
        {"apidebug", 'a', OptionParser::A_NONE, "print calls to API (very verbose, very slow)"},
        {"astdebug", 'A', OptionParser::A_NONE, "print calls to the parser (very verbose, very slow)"},
        {"gcstart", 'g', OptionParser::A_REQUIRED, "set minimum bytes at which the GC should kick in. 0 disables GC"},
        {0, 0, (OptionParser::ArgType)0, nullptr}
    };
    nargc = 0;
    OptionParser options(argc, argv);
    options.dopermute = 0;
    while((opt = options.nextLong(longopts, &longindex)) != -1)
    {
        co = longopts[longindex].shortname;
        if(opt == '?')
        {
            printf("%s: %s\n", argv[0], options.errmsg);
        }
        else if(co == 'g')
        {
            nextgcstart = atol(options.optarg);
        }
        else if(co == 't')
        {
            nn_cli_printtypesizes();
            return 0;
        }
        else if(co == 'h')
        {
            nn_cli_showusage(argv, longopts, false);
            wasusage = true;
        }
        else if(co == 'd' || co == 'j')
        {
            state->m_conf.dumpbytecode = true;
            state->m_conf.dumpinstructions = true;        
        }
        else if(co == 'D')
        {
            state->m_conf.dumpprintstack = false;
        }
        else if(co == 'x')
        {
            state->m_conf.exitafterbytecode = true;
        }
        else if(co == 'a')
        {
            state->m_conf.enableapidebug = true;
        }
        else if(co == 'A')
        {
            state->m_conf.enableastdebug = true;
        }
        else if(co == 's')
        {
            state->m_conf.enablestrictmode = true;            
        }
        else if(co == 'e')
        {
            source = options.optarg;
        }
        else if(co == 'w')
        {
            state->m_conf.enablewarnings = true;
        }
        else if(co == 'q')
        {
            quitafterinit = true;
        }
    }
    if(wasusage || quitafterinit)
    {
        goto cleanup;
    }
    nn_cli_parseenv(state, envp);
    while(true)
    {
        arg = options.nextPositional();
        if(arg == nullptr)
        {
            break;
        }
        fprintf(stderr, "arg=\"%s\" nargc=%d\n", arg, nargc);
        nargv[nargc] = arg;
        nargc++;
    }
    {
        neon::String* os;
        state->m_cliargv = neon::Array::make(state);
        for(i=0; i<nargc; i++)
        {
            os = neon::String::copy(state, nargv[i]);
            state->m_cliargv->push(neon::Value::fromObject(os));
        }
        state->m_definedglobals->setCStr("ARGV", neon::Value::fromObject(state->m_cliargv));
    }
    state->m_gcstate.nextgc = nextgcstart;
    nn_import_loadbuiltinmodules(state);
    if(source != nullptr)
    {
        ok = nn_cli_runcode(state, source);
    }
    else if(nargc > 0)
    {
        filename = state->m_cliargv->at(0).asString()->data();
        fprintf(stderr, "nargv[0]=%s\n", filename);
        ok = nn_cli_runfile(state, filename);
    }
    else
    {
        ok = nn_cli_repl(state);
    }
    cleanup:
    neon::Memory::destroy(state);
    if(ok)
    {
        return 0;
    }
    return 1;
}


