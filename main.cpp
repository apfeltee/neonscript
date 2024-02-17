
#ifdef __GNUC__
    //#pragma GCC poison alloca
#endif

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
        state->apiDebugVMCall(__FUNCTION__, __VA_ARGS__); \
    }

#define NEON_ASTDEBUG(state, ...) \
    if((NEON_UNLIKELY((state)->m_conf.enableastdebug))) \
    { \
        state->apiDebugAstCall(__FUNCTION__, __VA_ARGS__); \
    }


#define NEON_VMMAC_EXITVM(state) \
    { \
        (void)you_are_calling_exit_vm_outside_of_runvm; \
        return neon::Status::FAIL_RUNTIME; \
    }        

#define NEON_VMMAC_TRYRAISE(state, rtval, ...) \
    if(!state->raiseClass(state->m_exceptions.stdexception, ##__VA_ARGS__)) \
    { \
        return rtval; \
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
    struct /**/VM;
}

#include "memory.h"

namespace neon
{
    namespace Util
    {
        // if at all necessary, add code here to support (or enforce) utf8 support.
        // currently only applies to windows.
        void setOSUnicode()
        {
            #if defined(NEON_PLAT_ISWINDOWS)
                _setmode(fileno(stdout), _O_BINARY);
                _setmode(fileno(stderr), _O_BINARY);
                // via: https://stackoverflow.com/a/45622802
                // Set console code page to UTF-8 so console known how to interpret string data
                SetConsoleOutputCP(CP_UTF8);
                // Enable buffering to prevent VS from chopping up UTF-8 byte sequences
                // probably very likely not necessary when std(out|err) is set to binary
                //setvbuf(stdout, nullptr, _IOFBF, 1000);
            #endif
        }

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
                //chars = (char*)VM::GC::allocate(state, sizeof(char), (size_t)count + 1);
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
            //buf = (char*)VM::GC::allocate(state, sizeof(char), toldlen + 1);
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
    }
}

#include "genericarray.h"
#include "sbuf.h"


namespace neon
{
    namespace Util
    {
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
                static bool isDashDash(const char* arg)
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

                static bool isShortOpt(const char* arg)
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

                static void fromLong(const LongFlags* longopts, char* optstring)
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

    struct Descriptor
    {
        public:
            int m_fd = -1;

        public:
            NEON_FORCEINLINE Descriptor()
            {
            }

            NEON_FORCEINLINE Descriptor(int fd): m_fd(fd)
            {
            }

            NEON_FORCEINLINE bool isTTY() const
            {
                return osfn_isatty(m_fd);
            }

            NEON_FORCEINLINE int fd() const
            {
                return m_fd;
            }

            size_t read(void* buf, size_t cnt)
            {
                //ssize_t read(int fd, void *buf, size_t count);
                return osfn_fdread(m_fd, buf, cnt);
            }

    };

    struct Terminal
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
            bool m_isalltty = false;
            Descriptor m_stdin;
            Descriptor m_stdout;
            Descriptor m_stderr;

        public:
            Terminal()
            {
                m_stdin = Descriptor(fileno(stdin));
                m_stdout = Descriptor(fileno(stdout));
                m_stdin = Descriptor(fileno(stderr));
                #if !defined(NEON_CFG_FORCEDISABLECOLOR)
                    m_isalltty = (m_stdout.isTTY() && m_stdin.isTTY());
                #endif
            }

            NEON_FORCEINLINE bool stdoutIsTTY() const
            {
                return m_isalltty;
            }

            NEON_FORCEINLINE Code codeFromChar(char c) const
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

            NEON_FORCEINLINE const char* color(Code tc) const
            {
                #if !defined(NEON_CFG_FORCEDISABLECOLOR)

                    if(m_isalltty)
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

            NEON_FORCEINLINE const char* color(char tc) const
            {
                return color(codeFromChar(tc));
            }
    };

    /*
    * quite a number of types ***MUST NOT EVER BE DERIVED FROM***, to ensure
    * that they are correctly initialized.
    * Just declare them as an instance variable instead.
    */
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
            NEON_FORCEINLINE double coerceToNumber()
            {
                if(isNull())
                {
                    return 0;
                }
                if(isBool())
                {
                    if(asBool())
                    {
                        return 1;
                    }
                    return 0;
                }
                return asNumber();
            }

            NEON_FORCEINLINE uint32_t coerceToUInt()
            {
                if(isNull())
                {
                    return 0;
                }
                if(isBool())
                {
                    if(asBool())
                    {
                        return 1;
                    }
                    return 0;
                }
                return asNumber();
            }

            NEON_FORCEINLINE long coerceToInt()
            {
                return (long)coerceToNumber();
            }

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

    struct State final
    {
        public:
            using CallbackFN = Value (*)(State*, Arguments*);

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

            VM* m_vmstate;

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

            ClassInstance* makeExceptionInstance(ClassObject* exklass, String* message);
            
            ClassObject* findExceptionByName(const char* name);
            
            template<typename... ArgsT>
            bool raiseAtSourceLocation(ClassObject* exklass, const char* format, ArgsT&&... args);



        public:
            State()
            {
                init(nullptr);
            }

            ~State();

            void gcMarkCompilerRoots();


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
                m_vmstate->stackPop(fnargs->argc);
                raiseClass(m_exceptions.stdexception, fmt, args...);
                return Value::makeBool(false);
            }

            template<typename... ArgsT>
            NEON_FORCEINLINE void apiDebugVMCall(const char* funcname, const char* format, ArgsT&&... args)
            {
                static auto fn_fprintf = fprintf;
                fprintf(stderr, "API CALL: to '%s': ", funcname);
                fn_fprintf(stderr, format, args...);
                fprintf(stderr, "\n");
            }

            template<typename... ArgsT>
            NEON_FORCEINLINE void apiDebugAstCall(const char* funcname, const char* format, ArgsT&&... args)
            {
                static auto fn_fprintf = fprintf;
                fprintf(stderr, "AST CALL: to '%s': ", funcname);
                fn_fprintf(stderr, format, args...);
                fprintf(stderr, "\n");
            }

            ClassObject* vmGetClassFor(Value receiver);

            ClassObject* makeExceptionClass(Module* module, const char* cstrname);


            FuncClosure* compileScriptSource(Module* module, bool fromeval, const char* source);
            Value evalSource(const char* source);
            Status execSource(Module* module, const char* source, Value* dest);

            void defGlobalValue(const char* name, Value val);
            void defNativeFunction(const char* name, CallbackFN fptr);

            bool addSearchPathObjString(String* os);
            bool addSearchPath(const char* path);

            ClassObject* makeNamedClass(const char* name, ClassObject* parent);

            Status callRunVM(int exitframe, Value* rv);

    };

    #include "vm.h"

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
                plain = VM::GC::gcCreate<SubObjT>(state);
                plain->m_objtype = type;
                plain->m_mark = !state->m_vmstate->m_currentmarkvalue;
                plain->m_isstale = false;
                plain->m_pvm = state;
                plain->m_nextobj = state->m_vmstate->m_linkedobjects;
                state->m_vmstate->m_linkedobjects = plain;
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
                    VM::GC::gcRelease(state, ptr, sizeof(SubObjT));
                }
            }

            static void markObject(State* state, Object* object)
            {
                if(object == nullptr)
                {
                    return;
                }
                if(object->m_mark == state->m_vmstate->m_currentmarkvalue)
                {
                    return;
                }
                #if defined(NEON_CFG_DEBUGGC) && NEON_CFG_DEBUGGC
                state->m_debugprinter->putformat("GC: marking object at <%p> ", (void*)object);
                state->m_debugprinter->printValue(Value::fromObject(object), false);
                state->m_debugprinter->putformat("\n");
                #endif
                object->m_mark = state->m_vmstate->m_currentmarkvalue;
                if(state->m_vmstate->m_gcgraycapacity < state->m_vmstate->m_gcgraycount + 1)
                {
                    state->m_vmstate->m_gcgraycapacity = Util::growCapacity(state->m_vmstate->m_gcgraycapacity);
                    state->m_vmstate->m_gcgraystack = (Object**)Memory::osRealloc(state->m_vmstate->m_gcgraystack, sizeof(Object*) * state->m_vmstate->m_gcgraycapacity);
                    if(state->m_vmstate->m_gcgraystack == nullptr)
                    {
                        fflush(stdout);
                        fprintf(stderr, "GC encountered an error");
                        abort();
                    }
                }
                state->m_vmstate->m_gcgraystack[state->m_vmstate->m_gcgraycount++] = object;
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

    struct ValArray: public Util::GenericArray<Value>
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
            ValArray(): GenericArray()
            {
            }

            void gcMark(State* state)
            {
                uint64_t i;
                for(i = 0; i < m_count; i++)
                {
                    Object::markValue(state, m_values[i]);
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
                Memory::freeArray(m_entries, m_capacity);
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
                Entry* dest;
                Entry* entry;
                Entry* nents;
                //nents = (Entry*)VM::GC::allocate(state, sizeof(Entry), needcap);
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
                Memory::freeArray(m_entries, m_capacity);
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
                    if(entry->key.isObject() && entry->key.asObject()->m_mark != state->m_vmstate->m_currentmarkvalue)
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
                m_constants = Memory::create<ValArray>();
                m_argdefvals = Memory::create<ValArray>();
            }

            ~Blob()
            {
                if(m_instrucs != nullptr)
                {
                    Memory::freeArray(m_instrucs, m_capacity);
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
                    m_instrucs = Memory::growArray(m_instrucs, oldcapacity, m_capacity);
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
                Memory::freeArray(chars, (size_t)length + 1);
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
                state->m_vmstate->stackPush(Value::fromObject(rs));
                state->m_cachedstrings->set(Value::fromObject(rs), Value::makeNull());
                state->m_vmstate->stackPop();
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
                //raw = (char*)VM::GC::allocate(m_pvm, sizeof(char), asz);
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
                rt->m_varray = Memory::create<ValArray>();
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
                /*m_pvm->m_vmstate->stackPush(value);*/
                m_varray->push(value);
                /*m_pvm->m_vmstate->stackPop(); */
            }

            NEON_FORCEINLINE Value pop()
            {
                return m_varray->pop();
            }

            Array* copy(int start, int length)
            {
                int i;
                Array* newlist;
                newlist = m_pvm->m_vmstate->gcProtect(Array::make(m_pvm));
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
                rt->m_keynames = Memory::create<ValArray>();
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
                selfCloseFile();
                return true;
            }

            bool selfOpenFile()
            {
                if(m_filehandle != nullptr)
                {
                    return true;
                }
                if(m_filehandle == nullptr && !m_isstd)
                {
                    m_filehandle = fopen(m_filepath->data(), m_filemode->data());
                    if(m_filehandle != nullptr)
                    {
                        m_isopen = true;
                        m_filedesc = fileno(m_filehandle);
                        m_istty = osfn_isatty(m_filedesc);
                        return true;
                    }
                    else
                    {
                        m_filedesc = -1;
                        m_istty = false;
                    }
                    return false;
                }
                return false;
            }


            int selfCloseFile()
            {
                int result;
                if(m_filehandle != nullptr && !m_isstd)
                {
                    fflush(m_filehandle);
                    result = fclose(m_filehandle);
                    m_filehandle = nullptr;
                    m_isopen = false;
                    m_filedesc = -1;
                    m_istty = false;
                    return result;
                }
                return -1;
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
                    if(!osfn_fileexists(m_filepath->data()))
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
                        selfOpenFile();
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
            static Type getMethodType(Value method);

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
                //upvals = (ScopeUpvalue**)VM::GC::allocate(state, sizeof(ScopeUpvalue*), function->m_upvalcount);
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
                Memory::freeArray(m_storedupvals, m_upvalcount);
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
            int m_arity = -1;

        public:
            NestCall(State* state): m_pvm(state)
            {
            }

            int prepare(Value callable, Value mthobj, ValArray& callarr)
            {
                FuncClosure* closure;
                if(callable.isFuncClosure())
                {
                    closure = callable.asFuncClosure();
                    m_arity = closure->scriptfunc->m_arity;
                }
                else if(callable.isFuncScript())
                {
                    m_arity = callable.asFuncScript()->m_arity;
                }
                else if(callable.isFuncNative())
                {
                    m_arity = 1;
                    if(mthobj.isArray())
                    {
                        //m_arity = mthobj.asArray()->size();
                    }
                }
                if(m_arity > 0)
                {
                    callarr.push(Value::makeNull());
                    if(m_arity > 1)
                    {
                        callarr.push(Value::makeNull());
                        if(m_arity > 2 && !callable.isFuncNative())
                        {
                            callarr.push(mthobj);
                        }
                    }
                }
                return m_arity;
            }

            /* helper function to access call outside the state file. */
            bool callNested(Value callable, Value thisval, const ValArray& args, Value* dest)
            {
                size_t i;
                size_t argc;
                size_t pidx;
                size_t vsz;
                Status status;
                pidx = m_pvm->m_vmstate->m_stackidx;
                /* set the closure before the args */
                m_pvm->m_vmstate->stackPush(callable);
                argc = 0;
                vsz = args.size();
                argc = m_arity;
                if(vsz)
                {
                    for(i = 0; i < vsz; i++)
                    {
                        m_pvm->m_vmstate->stackPush(args.at(i));
                    }
                }
                if(!m_pvm->m_vmstate->vmCallBoundValue(callable, thisval, argc))
                {
                    fprintf(stderr, "nestcall: vmCallValue() (argc=%d) failed\n", argc);
                    abort();
                }
                status = m_pvm->m_vmstate->runVM(m_pvm->m_vmstate->m_framecount - 1, nullptr);
                if(status != Status::OK)
                {
                    fprintf(stderr, "nestcall: call to runvm (argc=%d) failed\n", argc);
                    abort();
                }
                *dest = m_pvm->m_vmstate->m_stackvalues[m_pvm->m_vmstate->m_stackidx - 1];
                m_pvm->m_vmstate->stackPop(argc + 1);
                m_pvm->m_vmstate->m_stackidx = pidx;
                return true;
            }
    };

    struct ClassObject final: public Object
    {
        public:
            static ClassObject* make(State* state, String* name, ClassObject* parent)
            {
                ClassObject* rt;
                rt = Object::make<ClassObject>(state, Value::OBJTYPE_CLASS);
                rt->m_classname = name;
                rt->m_instprops = Memory::create<HashTable>(state);
                rt->m_staticprops = Memory::create<HashTable>(state);
                rt->m_methods = Memory::create<HashTable>(state);
                rt->m_staticmethods = Memory::create<HashTable>(state);
                rt->m_constructor = Value::makeEmpty();
                rt->m_superclass = parent;
                return rt;
            }

            static bool instanceOf(ClassObject* klass1, ClassObject* expected)
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
                state->m_vmstate->stackPush(Value::fromObject(rt));
                rt->m_active = true;
                rt->m_fromclass = klass;
                rt->m_properties = Memory::create<HashTable>(state);
                if(rt->m_fromclass->m_instprops->m_count > 0)
                {
                    HashTable::copy(rt->m_fromclass->m_instprops, rt->m_properties);
                }
                /* gc fix */
                state->m_vmstate->stackPop();
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

            static void closeLibHandle(void* dlw)
            {
                (void)dlw;
                //dlwrap_dlclose(dlw);
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
                    if(osfn_fileexists(cstrpath))
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
                state->m_vmstate->stackPush(name);
                state->m_vmstate->stackPush(Value::fromObject(module));
                state->m_loadedmodules->set(name, Value::fromObject(module));
                state->m_vmstate->stackPop(2);
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
                ValArray args;
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
                NestCall nc(state);
                argc = nc.prepare(callable, Value::makeNull(), args);
                if(!nc.callNested(callable, Value::makeNull(), args, &retv))
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
                    targetmod = state->m_vmstate->gcProtect(Module::make(state, (char*)regmod->regmodname, source, false));
                    targetmod->m_fnpreloader = (void*)regmod->preloader;
                    targetmod->m_fnunloader = (void*)regmod->unloader;
                    if(regmod->fields != nullptr)
                    {
                        for(j = 0; regmod->fields[j].fieldname != nullptr; j++)
                        {
                            field = regmod->fields[j];
                            fieldname = Value::fromObject(state->m_vmstate->gcProtect(String::copy(state, field.fieldname)));
                            v = field.fieldvalfn(state);
                            state->m_vmstate->stackPush(v);
                            targetmod->m_deftable->set(fieldname, v);
                            state->m_vmstate->stackPop();
                        }
                    }
                    if(regmod->functions != nullptr)
                    {
                        for(j = 0; regmod->functions[j].funcname != nullptr; j++)
                        {
                            func = regmod->functions[j];
                            funcname = Value::fromObject(state->m_vmstate->gcProtect(String::copy(state, func.funcname)));
                            funcrealvalue = Value::fromObject(state->m_vmstate->gcProtect(FuncNative::make(state, func.function, func.funcname)));
                            state->m_vmstate->stackPush(funcrealvalue);
                            targetmod->m_deftable->set(funcname, funcrealvalue);
                            state->m_vmstate->stackPop();
                        }
                    }
                    if(regmod->classes != nullptr)
                    {
                        for(j = 0; regmod->classes[j].classname != nullptr; j++)
                        {
                            klassreg = regmod->classes[j];
                            classname = state->m_vmstate->gcProtect(String::copy(state, klassreg.classname));
                            klass = state->m_vmstate->gcProtect(ClassObject::make(state, classname, nullptr));
                            if(klassreg.functions != nullptr)
                            {
                                for(k = 0; klassreg.functions[k].funcname != nullptr; k++)
                                {
                                    func = klassreg.functions[k];
                                    funcname = Value::fromObject(state->m_vmstate->gcProtect(String::copy(state, func.funcname)));
                                    native = state->m_vmstate->gcProtect(FuncNative::make(state, func.function, func.funcname));
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
                                    fieldname = Value::fromObject(state->m_vmstate->gcProtect(String::copy(state, field.fieldname)));
                                    v = field.fieldvalfn(state);
                                    state->m_vmstate->stackPush(v);
                                    tabdest = klass->m_instprops;
                                    if(field.isstatic)
                                    {
                                        tabdest = klass->m_staticprops;
                                    }
                                    tabdest->set(fieldname, v);
                                    state->m_vmstate->stackPop();
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
                    state->m_vmstate->gcClearProtect();
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
                    closeLibHandle(m_libhandle);
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
                ValArray args;
                state = m_pvm;
                if(invmethod)
                {
                    field = instance->m_fromclass->m_methods->getByCStr("toString");
                    if(field != nullptr)
                    {
                        NestCall nc(state);
                        thisval = Value::fromObject(instance);
                        arity = nc.prepare(field->m_actualval, thisval, args);
                        fprintf(stderr, "arity = %d\n", arity);
                        state->m_vmstate->stackPop();
                        state->m_vmstate->stackPush(thisval);
                        if(nc.callNested(field->m_actualval, thisval, args, &resv))
                        {
                            Printer subp(state, &subw);
                            subp.printValue(resv, false, false);
                            os = subp.takeString();
                            put(os->data(), os->length());
                            //state->m_vmstate->stackPop();
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
                obj = m_pvm->m_vmstate->gcProtect(value.asObject());
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
                            auto dict = m_pvm->m_vmstate->gcProtect(static_cast<Dictionary*>(obj));
                            printObjDict(dict);
                            m_pvm->m_vmstate->gcClearProtect();
                        }
                        break;
                    case Value::OBJTYPE_ARRAY:
                        {
                            auto arr = m_pvm->m_vmstate->gcProtect(static_cast<Array*>(obj));
                            printObjArray(arr);
                            m_pvm->m_vmstate->gcClearProtect();
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
                            string = m_pvm->m_vmstate->gcProtect(static_cast<String*>(obj));
                            if(fixstring)
                            {
                                putQuotedString(string->data(), string->length(), true);
                            }
                            else
                            {
                                put(string->data(), string->length());
                            }
                            m_pvm->m_vmstate->gcClearProtect();
                        }
                        break;
                }
                m_pvm->m_vmstate->gcClearProtect();
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


    struct FormatInfo
    {
        public:
            struct FlagParseState;
            using GetNextFN = Value(*)(FlagParseState*, int);

            struct FlagParseState
            {
                int currchar = -1;
                int nextchar = -1;
                bool failed = false;
                size_t position = 0;
                size_t argpos = 0;
                int argc = 0;
                Value* argv = nullptr;
                Value currvalue;
            };

        public:
            template<typename... VargT>
            static inline bool formatArgs(Printer* pr, const char* fmt, VargT&&... args)
            {
                size_t argc = sizeof...(args);
                Value fargs[] = {(args) ..., Value::makeEmpty()};
                FormatInfo inf(pr->m_pvm, pr, fmt, strlen(fmt));
                return inf.format(argc, 0, fargs);
            }

        private:
            static inline Value defaultGetNextValue(FlagParseState* st, int pos)
            {
                //st.currvalue = st.argv[st.argpos];
                return st->argv[pos];
            }

        public:
            State* m_pvm;
            /* length of the format string */
            size_t m_fmtlen = 0;
            /* the actual format string */
            const char* m_fmtstr = nullptr;
            /* destination printer */
            Printer* m_printer = nullptr;
            GetNextFN m_fngetnext = nullptr;


        private:
            inline void doHandleFlag(FlagParseState& st)
            {
                int intval;
                char chval;
                if(st.nextchar == '%')
                {
                    m_printer->putChar('%');
                }
                else
                {
                    st.position++;
                    if((int)st.argpos > st.argc)
                    {
                        st.failed = true;
                        st.currvalue = Value::makeEmpty();
                    }
                    else
                    {                        
                        if(m_fngetnext != nullptr)
                        {
                            st.currvalue = m_fngetnext(&st, st.argpos);
                        }
                        else
                        {
                            st.currvalue = st.argv[st.argpos];
                        }
                    }
                    st.argpos++;
                    switch(st.nextchar)
                    {
                        case 'q':
                        case 'p':
                            {
                                m_printer->printValue(st.currvalue, true, true);
                            }
                            break;
                        case 'c':
                            {
                                intval = (int)st.currvalue.asNumber();
                                chval = intval;
                                //m_printer->putformat("%c", intval);
                                m_printer->put(&chval, 1);
                            }
                            break;
                        /* TODO: implement actual field formatting */
                        case 's':
                        case 'd':
                        case 'i':
                        case 'g':
                            {
                                m_printer->printValue(st.currvalue, false, true);
                            }
                            break;
                        default:
                            {
                                m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "unknown/invalid format flag '%%c'", st.nextchar);
                            }
                            break;
                    }
                }
            }

        public:
            inline FormatInfo(State* state, Printer* pr, const char* fstr, size_t flen): m_pvm(state)
            {
                m_fmtstr = fstr;
                m_fmtlen = flen;
                m_printer = pr;
            }

            inline ~FormatInfo()
            {
            }

            inline bool format(int argc, int argbegin, Value* argv)
            {
                return format(argc, argbegin, argv, nullptr);
            }

            inline bool format(int argc, int argbegin, Value* argv, GetNextFN fn)
            {
                FlagParseState st;
                st.position = 0;
                st.argpos = argbegin;
                st.failed = false;
                st.argc = argc;
                st.argv = argv;
                m_fngetnext = fn;
                while(st.position < m_fmtlen)
                {
                    st.currchar = m_fmtstr[st.position];
                    st.nextchar = -1;
                    if((st.position + 1) < m_fmtlen)
                    {
                        st.nextchar = m_fmtstr[st.position+1];
                    }
                    st.position++;
                    if(st.currchar == '%')
                    {
                        doHandleFlag(st);
                    }
                    else
                    {
                        m_printer->putChar(st.currchar);
                    }
                }
                return st.failed;
            }
    };

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
                Terminal nc;
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
                buf = (char*)VM::GC::allocate(m_pvm, sizeof(char), 1024);
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
                    Util::GenericArray<CompiledLocal> m_compiledlocals;

                public:
                    FuncCompiler(Parser* prs, FuncCommon::Type t, bool isanon)
                    {
                        size_t i;
                        bool candeclthis;
                        CompiledLocal* local;
                        String* fname;
                        (void)i;
                        m_prs = prs;
                        m_enclosing = m_prs->m_pvm->m_activeparser->m_currfunccompiler;
                        m_targetfunc = nullptr;
                        m_type = t;
                        m_localcount = 0;
                        m_scopedepth = 0;
                        m_compiledexcepthandlercount = 0;
                        m_fromimport = false;
                        m_targetfunc = FuncScript::make(m_prs->m_pvm, m_prs->m_currmodule, t);
                        m_prs->m_currfunccompiler = this;
                        m_compiledlocals.push(CompiledLocal{});
                        if(t != FuncCommon::FUNCTYPE_SCRIPT)
                        {
                            m_prs->m_pvm->m_vmstate->stackPush(Value::fromObject(m_targetfunc));
                            if(isanon)
                            {
                                Printer ptmp(m_prs->m_pvm);
                                ptmp.putformat("anonymous@[%s:%d]", m_prs->m_currentphysfile, m_prs->m_prevtoken.line);
                                fname = ptmp.takeString();
                            }
                            else
                            {
                                fname = String::copy(m_prs->m_pvm, m_prs->m_prevtoken.start, m_prs->m_prevtoken.length);
                            }
                            m_prs->m_currfunccompiler->m_targetfunc->m_scriptfnname = fname;
                            m_prs->m_pvm->m_vmstate->stackPop();
                        }
                        /* claiming slot zero for use in class methods */
                        local = &m_prs->m_currfunccompiler->m_compiledlocals[0];
                        m_prs->m_currfunccompiler->m_localcount++;
                        local->depth = 0;
                        local->iscaptured = false;
                        candeclthis = (
                            (t != FuncCommon::FUNCTYPE_FUNCTION) &&
                            (m_prs->m_compcontext == COMPCONTEXT_CLASS)
                        );
                        if(candeclthis || (/*(t == FuncCommon::FUNCTYPE_ANONYMOUS) &&*/ (m_prs->m_compcontext != COMPCONTEXT_CLASS)))
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
                        m_prs->parseBlock();
                        /* create the function object */
                        if(closescope)
                        {
                            m_prs->scopeEnd();
                        }
                        function = m_prs->endCompiler();
                        m_prs->m_pvm->m_vmstate->stackPush(Value::fromObject(function));
                        m_prs->emitInstruction(Instruction::OP_MAKECLOSURE);
                        m_prs->emit1short(m_prs->pushConst(Value::fromObject(function)));
                        for(i = 0; i < function->m_upvalcount; i++)
                        {
                            m_prs->emit1byte(m_compiledupvals[i].islocal ? 1 : 0);
                            m_prs->emit1short(m_compiledupvals[i].index);
                        }
                        m_prs->m_pvm->m_vmstate->stackPop();
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

            static Token makeSynthToken(const char* name)
            {
                Token token;
                token.isglobal = false;
                token.line = 0;
                token.type = (Token::Type)0;
                token.start = name;
                token.length = (int)strlen(name);
                return token;
            }

            static int getCodeArgsCount(const Instruction* bytecode, const Value* constants, int ip)
            {
                int constant;
                Instruction::OpCode code;
                FuncScript* fn;
                code = (Instruction::OpCode)bytecode[ip].code;
                switch(code)
                {
                    case Instruction::OP_EQUAL:
                    case Instruction::OP_PRIMGREATER:
                    case Instruction::OP_PRIMLESSTHAN:
                    case Instruction::OP_PUSHNULL:
                    case Instruction::OP_PUSHTRUE:
                    case Instruction::OP_PUSHFALSE:
                    case Instruction::OP_PRIMADD:
                    case Instruction::OP_PRIMSUBTRACT:
                    case Instruction::OP_PRIMMULTIPLY:
                    case Instruction::OP_PRIMDIVIDE:
                    case Instruction::OP_PRIMFLOORDIVIDE:
                    case Instruction::OP_PRIMMODULO:
                    case Instruction::OP_PRIMPOW:
                    case Instruction::OP_PRIMNEGATE:
                    case Instruction::OP_PRIMNOT:
                    case Instruction::OP_ECHO:
                    case Instruction::OP_TYPEOF:
                    case Instruction::OP_POPONE:
                    case Instruction::OP_UPVALUECLOSE:
                    case Instruction::OP_DUPONE:
                    case Instruction::OP_RETURN:
                    case Instruction::OP_CLASSINHERIT:
                    case Instruction::OP_CLASSGETSUPER:
                    case Instruction::OP_PRIMAND:
                    case Instruction::OP_PRIMOR:
                    case Instruction::OP_PRIMBITXOR:
                    case Instruction::OP_PRIMSHIFTLEFT:
                    case Instruction::OP_PRIMSHIFTRIGHT:
                    case Instruction::OP_PRIMBITNOT:
                    case Instruction::OP_PUSHONE:
                    case Instruction::OP_INDEXSET:
                    case Instruction::OP_ASSERT:
                    case Instruction::OP_EXTHROW:
                    case Instruction::OP_EXPOPTRY:
                    case Instruction::OP_MAKERANGE:
                    case Instruction::OP_STRINGIFY:
                    case Instruction::OP_PUSHEMPTY:
                    case Instruction::OP_EXPUBLISHTRY:
                        return 0;
                    case Instruction::OP_CALLFUNCTION:
                    case Instruction::OP_CLASSINVOKESUPERSELF:
                    case Instruction::OP_INDEXGET:
                    case Instruction::OP_INDEXGETRANGED:
                        return 1;
                    case Instruction::OP_GLOBALDEFINE:
                    case Instruction::OP_GLOBALGET:
                    case Instruction::OP_GLOBALSET:
                    case Instruction::OP_LOCALGET:
                    case Instruction::OP_LOCALSET:
                    case Instruction::OP_FUNCARGSET:
                    case Instruction::OP_FUNCARGGET:
                    case Instruction::OP_UPVALUEGET:
                    case Instruction::OP_UPVALUESET:
                    case Instruction::OP_JUMPIFFALSE:
                    case Instruction::OP_JUMPNOW:
                    case Instruction::OP_BREAK_PL:
                    case Instruction::OP_LOOP:
                    case Instruction::OP_PUSHCONSTANT:
                    case Instruction::OP_POPN:
                    case Instruction::OP_MAKECLASS:
                    case Instruction::OP_PROPERTYGET:
                    case Instruction::OP_PROPERTYGETSELF:
                    case Instruction::OP_PROPERTYSET:
                    case Instruction::OP_MAKEARRAY:
                    case Instruction::OP_MAKEDICT:
                    case Instruction::OP_IMPORTIMPORT:
                    case Instruction::OP_SWITCH:
                    case Instruction::OP_MAKEMETHOD:
                    //case Instruction::OP_FUNCOPTARG:
                        return 2;
                    case Instruction::OP_CALLMETHOD:
                    case Instruction::OP_CLASSINVOKETHIS:
                    case Instruction::OP_CLASSINVOKESUPER:
                    case Instruction::OP_CLASSPROPERTYDEFINE:
                        return 3;
                    case Instruction::OP_EXTRY:
                        return 6;
                    case Instruction::OP_MAKECLOSURE:
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

            void consumeStmtEnd()
            {
                /* allow block last statement to omit statement end */
                if(m_blockcount > 0 && check(Token::TOK_BRACECLOSE))
                {
                    return;
                }
                if(match(Token::TOK_SEMICOLON))
                {
                    while(match(Token::TOK_SEMICOLON) || match(Token::TOK_NEWLINE))
                    {
                    }
                    return;
                }
                if(match(Token::TOK_EOF) || m_prevtoken.type == Token::TOK_EOF)
                {
                    return;
                }
                /* consume(Token::TOK_NEWLINE, "end of statement expected"); */
                while(match(Token::TOK_SEMICOLON) || match(Token::TOK_NEWLINE))
                {
                }
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

            void emitAssign(uint8_t realop, uint8_t getop, uint8_t setop, int arg)
            {
                NEON_ASTDEBUG(m_pvm, "");
                m_replcanecho = false;
                if(getop == Instruction::OP_PROPERTYGET || getop == Instruction::OP_PROPERTYGETSELF)
                {
                    emitInstruction(Instruction::OP_DUPONE);
                }
                if(arg != -1)
                {
                    emitByteAndShort(getop, arg);
                }
                else
                {
                    emitInstruction(getop);
                    emit1byte(1);
                }
                parseExpression();
                emitInstruction(realop);
                if(arg != -1)
                {
                    emitByteAndShort(setop, (uint16_t)arg);
                }
                else
                {
                    emitInstruction(setop);
                }
            }

            void emitNamedVar(Token name, bool canassign)
            {
                bool fromclass;
                uint8_t getop;
                uint8_t setop;
                int arg;
                (void)fromclass;
                NEON_ASTDEBUG(m_pvm, " name=%.*s", name.length, name.start);
                fromclass = m_currclasscompiler != nullptr;
                arg = m_currfunccompiler->resolveLocal(&name);
                if(arg != -1)
                {
                    if(m_infunction)
                    {
                        getop = Instruction::OP_FUNCARGGET;
                        setop = Instruction::OP_FUNCARGSET;
                    }
                    else
                    {
                        getop = Instruction::OP_LOCALGET;
                        setop = Instruction::OP_LOCALSET;
                    }
                }
                else
                {
                    arg = m_currfunccompiler->resolveUpvalue(&name);
                    if((arg != -1) && (name.isglobal == false))
                    {
                        getop = Instruction::OP_UPVALUEGET;
                        setop = Instruction::OP_UPVALUESET;
                    }
                    else
                    {
                        arg = makeIdentConst(&name);
                        getop = Instruction::OP_GLOBALGET;
                        setop = Instruction::OP_GLOBALSET;
                    }
                }
                parseAssign(getop, setop, arg, canassign);
            }


            void emitDefineVariable(int global)
            {
                /* we are in a local scope... */
                if(m_currfunccompiler->m_scopedepth > 0)
                {
                    markLocalInitialized();
                    return;
                }
                emitInstruction(Instruction::OP_GLOBALDEFINE);
                emit1short(global);
            }

            void emitSetStmtVar(Token name)
            {
                int local;
                NEON_ASTDEBUG(m_pvm, "name=%.*s", name.length, name.start);
                if(m_currfunccompiler->m_targetfunc->m_scriptfnname != nullptr)
                {
                    local = addLocal(name) - 1;
                    markLocalInitialized();
                    emitInstruction(Instruction::OP_LOCALSET);
                    emit1short((uint16_t)local);
                }
                else
                {
                    emitInstruction(Instruction::OP_GLOBALDEFINE);
                    emit1short((uint16_t)makeIdentConst(&name));
                }
            }

            void scopeBegin()
            {
                NEON_ASTDEBUG(m_pvm, "current depth=%d", m_currfunccompiler->m_scopedepth);
                m_currfunccompiler->m_scopedepth++;
            }

            bool scopeEndCanContinue()
            {
                int lopos;
                int locount;
                int lodepth;
                int scodepth;
                NEON_ASTDEBUG(m_pvm, "");
                locount = m_currfunccompiler->m_localcount;
                lopos = m_currfunccompiler->m_localcount - 1;
                lodepth = m_currfunccompiler->m_compiledlocals[lopos].depth;
                scodepth = m_currfunccompiler->m_scopedepth;
                if(locount > 0 && lodepth > scodepth)
                {
                    return true;
                }
                return false;
            }

            void scopeEnd()
            {
                NEON_ASTDEBUG(m_pvm, "current scope depth=%d", m_currfunccompiler->m_scopedepth);
                m_currfunccompiler->m_scopedepth--;
                /*
                // remove all variables declared in scope while exiting...
                */
                if(m_keeplastvalue)
                {
                    //return;
                }
                while(scopeEndCanContinue())
                {
                    if(m_currfunccompiler->m_compiledlocals[m_currfunccompiler->m_localcount - 1].iscaptured)
                    {
                        emitInstruction(Instruction::OP_UPVALUECLOSE);
                    }
                    else
                    {
                        emitInstruction(Instruction::OP_POPONE);
                    }
                    m_currfunccompiler->m_localcount--;
                }
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

            int makeIdentConst(Token* name)
            {
                int rawlen;
                const char* rawstr;
                String* str;
                rawstr = name->start;
                rawlen = name->length;
                if(name->isglobal)
                {
                    rawstr++;
                    rawlen--;
                }
                str = String::copy(m_pvm, rawstr, rawlen);
                return pushConst(Value::fromObject(str));
            }

            void declareVariable()
            {
                int i;
                Token* name;
                CompiledLocal* local;
                /* global variables are implicitly declared... */
                if(m_currfunccompiler->m_scopedepth == 0)
                {
                    return;
                }
                name = &m_prevtoken;
                for(i = m_currfunccompiler->m_localcount - 1; i >= 0; i--)
                {
                    local = &m_currfunccompiler->m_compiledlocals[i];
                    if(local->depth != -1 && local->depth < m_currfunccompiler->m_scopedepth)
                    {
                        break;
                    }
                    if(identsEqual(name, &local->varname))
                    {
                        raiseError("%.*s already declared in current scope", name->length, name->start);
                    }
                }
                addLocal(*name);
            }

            int addLocal(Token name)
            {
                CompiledLocal local;
                #if 0
                if(m_currfunccompiler->m_localcount == FuncCompiler::MaxLocals)
                {
                    /* we've reached maximum local variables per scope */
                    raiseError("too many local variables in scope");
                    return -1;
                }
                #endif
                local.varname = name;
                local.depth = -1;
                local.iscaptured = false;
                //m_currfunccompiler->m_compiledlocals.push(local);
                m_currfunccompiler->m_compiledlocals.insertDefault(local, m_currfunccompiler->m_localcount, CompiledLocal{});

                m_currfunccompiler->m_localcount++;
                return m_currfunccompiler->m_localcount;
            }

            int parseIdentVar(const char* message)
            {
                if(!consume(Token::TOK_IDENTNORMAL, message))
                {
                    /* what to do here? */
                }
                declareVariable();
                /* we are in a local scope... */
                if(m_currfunccompiler->m_scopedepth > 0)
                {
                    return 0;
                }
                return makeIdentConst(&m_prevtoken);
            }

            int discardLocalVars(int depth)
            {
                int local;
                NEON_ASTDEBUG(m_pvm, "");
                if(m_keeplastvalue)
                {
                    //return 0;
                }
                if(m_currfunccompiler->m_scopedepth == -1)
                {
                    raiseError("cannot exit top-level scope");
                }
                local = m_currfunccompiler->m_localcount - 1;
                while(local >= 0 && m_currfunccompiler->m_compiledlocals[local].depth >= depth)
                {
                    if(m_currfunccompiler->m_compiledlocals[local].iscaptured)
                    {
                        emitInstruction(Instruction::OP_UPVALUECLOSE);
                    }
                    else
                    {
                        emitInstruction(Instruction::OP_POPONE);
                    }
                    local--;
                }
                return m_currfunccompiler->m_localcount - local - 1;
            }

            void endLoop()
            {
                size_t i;
                Instruction* bcode;
                Value* cvals;
                NEON_ASTDEBUG(m_pvm, "");
                /*
                // find all Instruction::OP_BREAK_PL placeholder and replace with the appropriate jump...
                */
                i = m_innermostloopstart;
                while(i < m_currfunccompiler->m_targetfunc->m_compiledblob->m_count)
                {
                    if(m_currfunccompiler->m_targetfunc->m_compiledblob->m_instrucs[i].code == Instruction::OP_BREAK_PL)
                    {
                        m_currfunccompiler->m_targetfunc->m_compiledblob->m_instrucs[i].code = Instruction::OP_JUMPNOW;
                        emitPatchJump(i + 1);
                        i += 3;
                    }
                    else
                    {
                        bcode = m_currfunccompiler->m_targetfunc->m_compiledblob->m_instrucs;
                        cvals = m_currfunccompiler->m_targetfunc->m_compiledblob->m_constants->m_values;
                        i += 1 + getCodeArgsCount(bcode, cvals, i);
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

            void markLocalInitialized()
            {
                int xpos;
                if(m_currfunccompiler->m_scopedepth == 0)
                {
                    return;
                }
                xpos = (m_currfunccompiler->m_localcount - 1);
                m_currfunccompiler->m_compiledlocals[xpos].depth = m_currfunccompiler->m_scopedepth;
            }

            void parseAssign(uint8_t getop, uint8_t setop, int arg, bool canassign)
            {
                NEON_ASTDEBUG(m_pvm, "");
                if(canassign && match(Token::TOK_ASSIGN))
                {
                    m_replcanecho = false;
                    parseExpression();
                    if(arg != -1)
                    {
                        emitByteAndShort(setop, (uint16_t)arg);
                    }
                    else
                    {
                        emitInstruction(setop);
                    }
                }
                else if(canassign && match(Token::TOK_PLUSASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMADD, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_MINUSASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMSUBTRACT, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_MULTASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMMULTIPLY, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_DIVASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMDIVIDE, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_POWASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMPOW, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_MODASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMMODULO, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_AMPASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMAND, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_BARASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMOR, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_TILDEASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMBITNOT, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_XORASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMBITXOR, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_LEFTSHIFTASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMSHIFTLEFT, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_RIGHTSHIFTASSIGN))
                {
                    emitAssign(Instruction::OP_PRIMSHIFTRIGHT, getop, setop, arg);
                }
                else if(canassign && match(Token::TOK_INCREMENT))
                {
                    m_replcanecho = false;
                    if(getop == Instruction::OP_PROPERTYGET || getop == Instruction::OP_PROPERTYGETSELF)
                    {
                        emitInstruction(Instruction::OP_DUPONE);
                    }
                    if(arg != -1)
                    {
                        emitByteAndShort(getop, arg);
                    }
                    else
                    {
                        emitInstruction(getop);
                        emit1byte(1);
                    }
                    emitInstruction(Instruction::OP_PUSHONE);
                    emitInstruction(Instruction::OP_PRIMADD);
                    emitInstruction(setop);
                    emit1short((uint16_t)arg);
                }
                else if(canassign && match(Token::TOK_DECREMENT))
                {
                    m_replcanecho = false;
                    if(getop == Instruction::OP_PROPERTYGET || getop == Instruction::OP_PROPERTYGETSELF)
                    {
                        emitInstruction(Instruction::OP_DUPONE);
                    }
                    if(arg != -1)
                    {
                        emitByteAndShort(getop, arg);
                    }
                    else
                    {
                        emitInstruction(getop);
                        emit1byte(1);
                    }
                    emitInstruction(Instruction::OP_PUSHONE);
                    emitInstruction(Instruction::OP_PRIMSUBTRACT);
                    emitInstruction(setop);
                    emit1short((uint16_t)arg);
                }
                else
                {
                    if(arg != -1)
                    {
                        if(getop == Instruction::OP_INDEXGET || getop == Instruction::OP_INDEXGETRANGED)
                        {
                            emitInstruction(getop);
                            emit1byte((uint8_t)0);
                        }
                        else
                        {
                            emitByteAndShort(getop, (uint16_t)arg);
                        }
                    }
                    else
                    {
                        emitInstruction(getop);
                        emit1byte((uint8_t)0);
                    }
                }
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
                deststr = (char*)VM::GC::allocate(m_pvm, sizeof(char), rawlen);
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


            bool parseBlock()
            {
                m_blockcount++;
                ignoreSpace();
                while(!check(Token::TOK_BRACECLOSE) && !check(Token::TOK_EOF))
                {
                    parseDeclaration();
                }
                m_blockcount--;
                if(!consume(Token::TOK_BRACECLOSE, "expected '}' after block"))
                {
                    return false;
                }
                if(match(Token::TOK_SEMICOLON))
                {
                }
                return true;
            }

            void declareFuncArgumentVar()
            {
                int i;
                Token* name;
                CompiledLocal* local;
                /* global variables are implicitly declared... */
                if(m_currfunccompiler->m_scopedepth == 0)
                {
                    return;
                }
                name = &m_prevtoken;
                for(i = m_currfunccompiler->m_localcount - 1; i >= 0; i--)
                {
                    local = &m_currfunccompiler->m_compiledlocals[i];
                    if(local->depth != -1 && local->depth < m_currfunccompiler->m_scopedepth)
                    {
                        break;
                    }
                    if(identsEqual(name, &local->varname))
                    {
                        raiseError("%.*s already declared in current scope", name->length, name->start);
                    }
                }
                addLocal(*name);
            }

            int parseFuncDeclParamVar(const char* message)
            {
                if(!consume(Token::TOK_IDENTNORMAL, message))
                {
                    /* what to do here? */
                }
                declareFuncArgumentVar();
                /* we are in a local scope... */
                if(m_currfunccompiler->m_scopedepth > 0)
                {
                    return 0;
                }
                return makeIdentConst(&m_prevtoken);
            }

            uint8_t parseFuncCallArgs()
            {
                uint8_t argcount;
                argcount = 0;
                if(!check(Token::TOK_PARENCLOSE))
                {
                    do
                    {
                        ignoreSpace();
                        parseExpression();
                        #if 0
                        if(argcount == NEON_CFG_ASTMAXFUNCPARAMS)
                        {
                            raiseError("cannot have more than %d arguments to a function", NEON_CFG_ASTMAXFUNCPARAMS);
                        }
                        #endif
                        argcount++;
                    } while(match(Token::TOK_COMMA));
                }
                ignoreSpace();
                if(!consume(Token::TOK_PARENCLOSE, "expected ')' after argument list"))
                {
                    /* TODO: handle this, somehow. */
                }
                return argcount;
            }

            void parseFuncDeclParamList()
            {
                int paramconstant;
                /* compile argument list... */
                do
                {
                    ignoreSpace();
                    m_currfunccompiler->m_targetfunc->m_arity++;
                    #if 0
                    if(m_currfunccompiler->m_targetfunc->m_arity > NEON_CFG_ASTMAXFUNCPARAMS)
                    {
                        raiseErrorAtCurrent("cannot have more than %d function parameters", NEON_CFG_ASTMAXFUNCPARAMS);
                    }
                    #endif
                    if(match(Token::TOK_TRIPLEDOT))
                    {
                        m_currfunccompiler->m_targetfunc->m_isvariadic = true;
                        addLocal(makeSynthToken("__args__"));
                        emitDefineVariable(0);
                        break;
                    }
                    paramconstant = parseFuncDeclParamVar("expected parameter name");
                    emitDefineVariable(paramconstant);
                    ignoreSpace();
                } while(match(Token::TOK_COMMA));
            }

            void parseFuncDeclaration()
            {
                int global;
                global = parseIdentVar("function name expected");
                markLocalInitialized();
                parseFuncDeclFull(FuncCommon::FUNCTYPE_FUNCTION, false);
                emitDefineVariable(global);
            }

            void parseFuncDeclFull(FuncCommon::Type type, bool isanon)
            {
                m_infunction = true;
                FuncCompiler compiler(this, type, isanon);
                scopeBegin();
                /* compile parameter list */
                consume(Token::TOK_PARENOPEN, "expected '(' after function name");
                if(!check(Token::TOK_PARENCLOSE))
                {
                    parseFuncDeclParamList();
                }
                consume(Token::TOK_PARENCLOSE, "expected ')' after function parameters");
                compiler.compileBody(false, isanon);
                m_infunction = false;
            }

            void parseClassFieldDefinition(bool isstatic)
            {
                int fieldconstant;
                consume(Token::TOK_IDENTNORMAL, "class property name expected");
                fieldconstant = makeIdentConst(&m_prevtoken);
                if(match(Token::TOK_ASSIGN))
                {
                    parseExpression();
                }
                else
                {
                    emitInstruction(Instruction::OP_PUSHNULL);
                }
                emitInstruction(Instruction::OP_CLASSPROPERTYDEFINE);
                emit1short(fieldconstant);
                emit1byte(isstatic ? 1 : 0);
                consumeStmtEnd();
                ignoreSpace();
            }

            void parseClassDeclaration()
            {
                bool isstatic;
                int nameconst;
                CompContext oldctx;
                Token classname;
                ClassCompiler classcompiler;
                consume(Token::TOK_IDENTNORMAL, "class name expected");
                nameconst = makeIdentConst(&m_prevtoken);
                classname = m_prevtoken;
                declareVariable();
                emitInstruction(Instruction::OP_MAKECLASS);
                emit1short(nameconst);
                emitDefineVariable(nameconst);
                classcompiler.classname = m_prevtoken;
                classcompiler.hassuperclass = false;
                classcompiler.m_enclosing = m_currclasscompiler;
                m_currclasscompiler = &classcompiler;
                oldctx = m_compcontext;
                m_compcontext = COMPCONTEXT_CLASS;
                if(match(Token::TOK_LESSTHAN))
                {
                    consume(Token::TOK_IDENTNORMAL, "name of superclass expected");
                    //nn_astparser_rulevarnormal(this, false);
                    emitNamedVar(m_prevtoken, false);
                    if(identsEqual(&classname, &m_prevtoken))
                    {
                        raiseError("class %.*s cannot inherit from itself", classname.length, classname.start);
                    }
                    scopeBegin();
                    addLocal(makeSynthToken(g_strsuper));
                    emitDefineVariable(0);
                    emitNamedVar(classname, false);
                    emitInstruction(Instruction::OP_CLASSINHERIT);
                    classcompiler.hassuperclass = true;
                }
                emitNamedVar(classname, false);
                ignoreSpace();
                consume(Token::TOK_BRACEOPEN, "expected '{' before class body");
                ignoreSpace();
                while(!check(Token::TOK_BRACECLOSE) && !check(Token::TOK_EOF))
                {
                    isstatic = false;
                    if(match(Token::TOK_KWSTATIC))
                    {
                        isstatic = true;
                    }

                    if(match(Token::TOK_KWVAR))
                    {
                        parseClassFieldDefinition(isstatic);
                    }
                    else
                    {
                        parseMethod(classname, isstatic);
                        ignoreSpace();
                    }
                }
                consume(Token::TOK_BRACECLOSE, "expected '}' after class body");
                if(match(Token::TOK_SEMICOLON))
                {
                }
                emitInstruction(Instruction::OP_POPONE);
                if(classcompiler.hassuperclass)
                {
                    scopeEnd();
                }
                m_currclasscompiler = m_currclasscompiler->m_enclosing;
                m_compcontext = oldctx;
            }

            void parseMethod(Token classname, bool isstatic)
            {
                size_t sn;
                int constant;
                const char* sc;
                FuncCommon::Type type;
                static Token::Type tkns[] = { Token::TOK_IDENTNORMAL, Token::TOK_DECORATOR };
                (void)classname;
                (void)isstatic;
                sc = "constructor";
                sn = strlen(sc);
                consumeOr("method name expected", tkns, 2);
                constant = makeIdentConst(&m_prevtoken);
                type = FuncCommon::FUNCTYPE_METHOD;
                if((m_prevtoken.length == (int)sn) && (memcmp(m_prevtoken.start, sc, sn) == 0))
                {
                    type = FuncCommon::FUNCTYPE_INITIALIZER;
                }
                else if((m_prevtoken.length > 0) && (m_prevtoken.start[0] == '_'))
                {
                    type = FuncCommon::FUNCTYPE_PRIVATE;
                }
                parseFuncDeclFull(type, false);
                emitInstruction(Instruction::OP_MAKEMETHOD);
                emit1short(constant);
            }

            void parseVarDeclaration(bool isinitializer)
            {
                int global;
                int totalparsed;
                totalparsed = 0;
                do
                {
                    if(totalparsed > 0)
                    {
                        ignoreSpace();
                    }
                    global = parseIdentVar("variable name expected");
                    if(match(Token::TOK_ASSIGN))
                    {
                        parseExpression();
                    }
                    else
                    {
                        emitInstruction(Instruction::OP_PUSHNULL);
                    }
                    emitDefineVariable(global);
                    totalparsed++;
                } while(match(Token::TOK_COMMA));

                if(!isinitializer)
                {
                    consumeStmtEnd();
                }
                else
                {
                    consume(Token::TOK_SEMICOLON, "expected ';' after initializer");
                    ignoreSpace();
                }
            }

            void parseExprStatement(bool isinitializer, bool semi)
            {
                if(m_pvm->m_isreplmode && m_currfunccompiler->m_scopedepth == 0)
                {
                    m_replcanecho = true;
                }
                if(!semi)
                {
                    parseExpression();
                }
                else
                {
                    parsePrecNoAdvance(Rule::PREC_ASSIGNMENT);
                }
                if(!isinitializer)
                {
                    if(m_replcanecho && m_pvm->m_isreplmode)
                    {
                        emitInstruction(Instruction::OP_ECHO);
                        m_replcanecho = false;
                    }
                    else
                    {
                        //if(!m_keeplastvalue)
                        {
                            emitInstruction(Instruction::OP_POPONE);
                        }
                    }
                    consumeStmtEnd();
                }
                else
                {
                    consume(Token::TOK_SEMICOLON, "expected ';' after initializer");
                    ignoreSpace();
                    emitInstruction(Instruction::OP_POPONE);
                }
            }

            void parseDeclaration()
            {
                ignoreSpace();
                if(match(Token::TOK_KWCLASS))
                {
                    parseClassDeclaration();
                }
                else if(match(Token::TOK_KWFUNCTION))
                {
                    parseFuncDeclaration();
                }
                else if(match(Token::TOK_KWVAR))
                {
                    parseVarDeclaration(false);
                }
                else if(match(Token::TOK_BRACEOPEN))
                {
                    if(!check(Token::TOK_NEWLINE) && m_currfunccompiler->m_scopedepth == 0)
                    {
                        parseExprStatement(false, true);
                    }
                    else
                    {
                        scopeBegin();
                        parseBlock();
                        scopeEnd();
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
            void parseForStatement()
            {
                int exitjump;
                int bodyjump;
                int incrstart;
                int surroundingloopstart;
                int surroundingscopedepth;
                scopeBegin();
                consume(Token::TOK_PARENOPEN, "expected '(' after 'for'");
                /* parse initializer... */
                if(match(Token::TOK_SEMICOLON))
                {
                    /* no initializer */
                }
                else if(match(Token::TOK_KWVAR))
                {
                    parseVarDeclaration(true);
                }
                else
                {
                    parseExprStatement(true, false);
                }
                /* keep a copy of the surrounding loop's start and depth */
                surroundingloopstart = m_innermostloopstart;
                surroundingscopedepth = m_innermostloopscopedepth;
                /* update the parser's loop start and depth to the current */
                m_innermostloopstart = currentBlob()->m_count;
                m_innermostloopscopedepth = m_currfunccompiler->m_scopedepth;
                exitjump = -1;
                if(!match(Token::TOK_SEMICOLON))
                {
                    /* the condition is optional */
                    parseExpression();
                    consume(Token::TOK_SEMICOLON, "expected ';' after condition");
                    ignoreSpace();
                    /* jump out of the loop if the condition is false... */
                    exitjump = emitJump(Instruction::OP_JUMPIFFALSE);
                    emitInstruction(Instruction::OP_POPONE);
                    /* pop the condition */
                }
                /* the iterator... */
                if(!check(Token::TOK_BRACEOPEN))
                {
                    bodyjump = emitJump(Instruction::OP_JUMPNOW);
                    incrstart = currentBlob()->m_count;
                    parseExpression();
                    ignoreSpace();
                    emitInstruction(Instruction::OP_POPONE);
                    emitLoop(m_innermostloopstart);
                    m_innermostloopstart = incrstart;
                    emitPatchJump(bodyjump);
                }
                consume(Token::TOK_PARENCLOSE, "expected ')' after 'for'");
                parseStatement();
                emitLoop(m_innermostloopstart);
                if(exitjump != -1)
                {
                    emitPatchJump(exitjump);
                    emitInstruction(Instruction::OP_POPONE);
                }
                endLoop();
                /* reset the loop start and scope depth to the surrounding value */
                m_innermostloopstart = surroundingloopstart;
                m_innermostloopscopedepth = surroundingscopedepth;
                scopeEnd();
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
            void parseForeachStatement()
            {
                int citer;
                int citern;
                int falsejump;
                int keyslot;
                int valueslot;
                int iteratorslot;
                int surroundingloopstart;
                int surroundingscopedepth;
                Token iteratortoken;
                Token keytoken;
                Token valuetoken;
                scopeBegin();
                /* define @iter and @itern constant */
                citer = pushConst(Value::fromObject(String::copy(m_pvm, "@iter")));
                citern = pushConst(Value::fromObject(String::copy(m_pvm, "@itern")));
                consume(Token::TOK_PARENOPEN, "expected '(' after 'foreach'");
                consume(Token::TOK_IDENTNORMAL, "expected variable name after 'foreach'");
                if(!check(Token::TOK_COMMA))
                {
                    keytoken = makeSynthToken(" _ ");
                    valuetoken = m_prevtoken;
                }
                else
                {
                    keytoken = m_prevtoken;
                    consume(Token::TOK_COMMA, "");
                    consume(Token::TOK_IDENTNORMAL, "expected variable name after ','");
                    valuetoken = m_prevtoken;
                }
                consume(Token::TOK_KWIN, "expected 'in' after for loop variable(s)");
                ignoreSpace();
                /*
                // The space in the variable name ensures it won't collide with a user-defined
                // variable.
                */
                iteratortoken = makeSynthToken(" iterator ");
                /* Evaluate the sequence expression and store it in a hidden local variable. */
                parseExpression();
                consume(Token::TOK_PARENCLOSE, "expected ')' after 'foreach'");
                if(m_currfunccompiler->m_localcount + 3 > FuncCompiler::MaxLocals)
                {
                    raiseError("cannot declare more than %d variables in one scope", FuncCompiler::MaxLocals);
                    return;
                }
                /* add the iterator to the local scope */
                iteratorslot = addLocal(iteratortoken) - 1;
                emitDefineVariable(0);
                /* Create the key local variable. */
                emitInstruction(Instruction::OP_PUSHNULL);
                keyslot = addLocal(keytoken) - 1;
                emitDefineVariable(keyslot);
                /* create the local value slot */
                emitInstruction(Instruction::OP_PUSHNULL);
                valueslot = addLocal(valuetoken) - 1;
                emitDefineVariable(0);
                surroundingloopstart = m_innermostloopstart;
                surroundingscopedepth = m_innermostloopscopedepth;
                /*
                // we'll be jumping back to right before the
                // expression after the loop body
                */
                m_innermostloopstart = currentBlob()->m_count;
                m_innermostloopscopedepth = m_currfunccompiler->m_scopedepth;
                /* key = iterable.iter_n__(key) */
                emitInstruction(Instruction::OP_LOCALGET);
                emit1short(iteratorslot);
                emitInstruction(Instruction::OP_LOCALGET);
                emit1short(keyslot);
                emitInstruction(Instruction::OP_CALLMETHOD);
                emit1short(citern);
                emit1byte(1);
                emitInstruction(Instruction::OP_LOCALSET);
                emit1short(keyslot);
                falsejump = emitJump(Instruction::OP_JUMPIFFALSE);
                emitInstruction(Instruction::OP_POPONE);
                /* value = iterable.iter__(key) */
                emitInstruction(Instruction::OP_LOCALGET);
                emit1short(iteratorslot);
                emitInstruction(Instruction::OP_LOCALGET);
                emit1short(keyslot);
                emitInstruction(Instruction::OP_CALLMETHOD);
                emit1short(citer);
                emit1byte(1);
                /*
                // Bind the loop value in its own scope. This ensures we get a fresh
                // variable each iteration so that closures for it don't all see the same one.
                */
                scopeBegin();
                /* update the value */
                emitInstruction(Instruction::OP_LOCALSET);
                emit1short(valueslot);
                emitInstruction(Instruction::OP_POPONE);
                parseStatement();
                scopeEnd();
                emitLoop(m_innermostloopstart);
                emitPatchJump(falsejump);
                emitInstruction(Instruction::OP_POPONE);
                endLoop();
                m_innermostloopstart = surroundingloopstart;
                m_innermostloopscopedepth = surroundingscopedepth;
                scopeEnd();
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
            void parseSwitchStatement()
            {
                int i;
                int length;
                int tgtaddr;
                int swstate;
                int casecount;
                int switchcode;
                int startoffset;
                char* str;
                Value jump;
                Token::Type casetype;
                VarSwitch* sw;
                String* string;
                Util::GenericArray<int> caseends;
                /* the expression */
                parseExpression();
                consume(Token::TOK_BRACEOPEN, "expected '{' after 'switch' expression");
                ignoreSpace();
                /* 0: before all cases, 1: before default, 2: after default */
                swstate = 0;
                casecount = 0;
                sw = VarSwitch::make(m_pvm);
                m_pvm->m_vmstate->stackPush(Value::fromObject(sw));
                switchcode = emitSwitch();
                /*
                emitInstruction(Instruction::OP_SWITCH);
                emit1short(pushConst(Value::fromObject(sw)));
                */
                startoffset = currentBlob()->m_count;
                while(!match(Token::TOK_BRACECLOSE) && !check(Token::TOK_EOF))
                {
                    if(match(Token::TOK_KWCASE) || match(Token::TOK_KWDEFAULT))
                    {
                        casetype = m_prevtoken.type;
                        if(swstate == 2)
                        {
                            raiseError("cannot have another case after a default case");
                        }
                        if(swstate == 1)
                        {
                            /* at the end of the previous case, jump over the others... */
                            tgtaddr = emitJump(Instruction::OP_JUMPNOW);
                            //caseends[casecount++] = tgtaddr;
                            caseends.push(tgtaddr);
                            casecount++;
                        }
                        if(casetype == Token::TOK_KWCASE)
                        {
                            swstate = 1;
                            do
                            {
                                ignoreSpace();
                                advance();
                                jump = Value::makeNumber((double)currentBlob()->m_count - (double)startoffset);
                                if(m_prevtoken.type == Token::TOK_KWTRUE)
                                {
                                    sw->m_jumppositions->set(Value::makeBool(true), jump);
                                }
                                else if(m_prevtoken.type == Token::TOK_KWFALSE)
                                {
                                    sw->m_jumppositions->set(Value::makeBool(false), jump);
                                }
                                else if(m_prevtoken.type == Token::TOK_LITERAL)
                                {
                                    str = parseString(&length);
                                    string = String::take(m_pvm, str, length);
                                    /* gc fix */
                                    m_pvm->m_vmstate->stackPush(Value::fromObject(string));
                                    sw->m_jumppositions->set(Value::fromObject(string), jump);
                                    /* gc fix */
                                    m_pvm->m_vmstate->stackPop();
                                }
                                else if(checkNumber())
                                {
                                    sw->m_jumppositions->set(parseNumber(), jump);
                                }
                                else
                                {
                                    /* pop the switch */
                                    m_pvm->m_vmstate->stackPop();
                                    raiseError("only constants can be used in 'when' expressions");
                                    return;
                                }
                            } while(match(Token::TOK_COMMA));
                        }
                        else
                        {
                            swstate = 2;
                            sw->m_defaultjump = currentBlob()->m_count - startoffset;
                        }
                    }
                    else
                    {
                        /* otherwise, it's a statement inside the current case */
                        if(swstate == 0)
                        {
                            raiseError("cannot have statements before any case");
                        }
                        parseStatement();
                    }
                }
                /* if we ended without a default case, patch its condition jump */
                if(swstate == 1)
                {
                    tgtaddr = emitJump(Instruction::OP_JUMPNOW);
                    //caseends[casecount++] = tgtaddr;
                    caseends.push(tgtaddr);
                    casecount++;
                }
                /* patch all the case jumps to the end */
                for(i = 0; i < casecount; i++)
                {
                    emitPatchJump(caseends[i]);
                }
                sw->m_exitjump = currentBlob()->m_count - startoffset;
                emitPatchSwitch(switchcode, pushConst(Value::fromObject(sw)));
                /* pop the switch */
                m_pvm->m_vmstate->stackPop();
            }

            void parseIfStatement()
            {
                int elsejump;
                int thenjump;
                parseExpression();
                thenjump = emitJump(Instruction::OP_JUMPIFFALSE);
                emitInstruction(Instruction::OP_POPONE);
                parseStatement();
                elsejump = emitJump(Instruction::OP_JUMPNOW);
                emitPatchJump(thenjump);
                emitInstruction(Instruction::OP_POPONE);
                if(match(Token::TOK_KWELSE))
                {
                    parseStatement();
                }
                emitPatchJump(elsejump);
            }

            void parseEchoStatement()
            {
                parseExpression();
                emitInstruction(Instruction::OP_ECHO);
                consumeStmtEnd();
            }

            void parseThrowStatement()
            {
                parseExpression();
                emitInstruction(Instruction::OP_EXTHROW);
                discardLocalVars(m_currfunccompiler->m_scopedepth - 1);
                consumeStmtEnd();
            }

            void parseAssertStatement()
            {
                consume(Token::TOK_PARENOPEN, "expected '(' after 'assert'");
                parseExpression();
                if(match(Token::TOK_COMMA))
                {
                    ignoreSpace();
                    parseExpression();
                }
                else
                {
                    emitInstruction(Instruction::OP_PUSHNULL);
                }
                emitInstruction(Instruction::OP_ASSERT);
                consume(Token::TOK_PARENCLOSE, "expected ')' after 'assert'");
                consumeStmtEnd();
            }

            void parseTryStatement()
            {
                int address;
                int type;
                int finally;
                int trybegins;
                int exitjump;
                int continueexecutionaddress;
                bool catchexists;
                bool finalexists;
                if(m_currfunccompiler->m_compiledexcepthandlercount == NEON_CFG_MAXEXCEPTHANDLERS)
                {
                    raiseError("maximum exception handler in scope exceeded");
                }
                m_currfunccompiler->m_compiledexcepthandlercount++;
                m_istrying = true;
                ignoreSpace();
                trybegins = emitTry();
                /* compile the try body */
                parseStatement();
                emitInstruction(Instruction::OP_EXPOPTRY);
                exitjump = emitJump(Instruction::OP_JUMPNOW);
                m_istrying = false;
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
                if(match(Token::TOK_KWCATCH))
                {
                    catchexists = true;
                    scopeBegin();
                    consume(Token::TOK_PARENOPEN, "expected '(' after 'catch'");
                    consume(Token::TOK_IDENTNORMAL, "missing exception class name");
                    type = makeIdentConst(&m_prevtoken);
                    address = currentBlob()->m_count;
                    if(match(Token::TOK_IDENTNORMAL))
                    {
                        emitSetStmtVar(m_prevtoken);
                    }
                    else
                    {
                        emitInstruction(Instruction::OP_POPONE);
                    }
                      consume(Token::TOK_PARENCLOSE, "expected ')' after 'catch'");
                    emitInstruction(Instruction::OP_EXPOPTRY);
                    ignoreSpace();
                    parseStatement();
                    scopeEnd();
                }
                else
                {
                    type = pushConst(Value::fromObject(String::copy(m_pvm, "Exception")));
                }
                emitPatchJump(exitjump);
                if(match(Token::TOK_KWFINALLY))
                {
                    finalexists = true;
                    /*
                    // if we arrived here from either the try or handler block,
                    // we don't want to continue propagating the exception
                    */
                    emitInstruction(Instruction::OP_PUSHFALSE);
                    finally = currentBlob()->m_count;
                    ignoreSpace();
                    parseStatement();
                    continueexecutionaddress = emitJump(Instruction::OP_JUMPIFFALSE);
                    /* pop the bool off the stack */
                    emitInstruction(Instruction::OP_POPONE);
                    emitInstruction(Instruction::OP_EXPUBLISHTRY);
                    emitPatchJump(continueexecutionaddress);
                    emitInstruction(Instruction::OP_POPONE);
                }
                if(!finalexists && !catchexists)
                {
                    raiseError("try block must contain at least one of catch or finally");
                }
                emitPatchTry(trybegins, type, address, finally);
            }

            void parseReturnStatement()
            {
                m_isreturning = true;
                /*
                if(m_currfunccompiler->type == FuncCommon::FUNCTYPE_SCRIPT)
                {
                    raiseError("cannot return from top-level code");
                }
                */
                if(match(Token::TOK_SEMICOLON) || match(Token::TOK_NEWLINE))
                {
                    emitReturn();
                }
                else
                {
                    if(m_currfunccompiler->m_type == FuncCommon::FUNCTYPE_INITIALIZER)
                    {
                        raiseError("cannot return value from constructor");
                    }
                    if(m_istrying)
                    {
                        emitInstruction(Instruction::OP_EXPOPTRY);
                    }
                    parseExpression();
                    emitInstruction(Instruction::OP_RETURN);
                    consumeStmtEnd();
                }
                m_isreturning = false;
            }

            void parseLoopWhileStatement()
            {
                int exitjump;
                int surroundingloopstart;
                int surroundingscopedepth;
                surroundingloopstart = m_innermostloopstart;
                surroundingscopedepth = m_innermostloopscopedepth;
                /*
                // we'll be jumping back to right before the
                // expression after the loop body
                */
                m_innermostloopstart = currentBlob()->m_count;
                m_innermostloopscopedepth = m_currfunccompiler->m_scopedepth;
                parseExpression();
                exitjump = emitJump(Instruction::OP_JUMPIFFALSE);
                emitInstruction(Instruction::OP_POPONE);
                parseStatement();
                emitLoop(m_innermostloopstart);
                emitPatchJump(exitjump);
                emitInstruction(Instruction::OP_POPONE);
                endLoop();
                m_innermostloopstart = surroundingloopstart;
                m_innermostloopscopedepth = surroundingscopedepth;
            }

            void parseLoopDoWhileStatement()
            {
                int exitjump;
                int surroundingloopstart;
                int surroundingscopedepth;
                surroundingloopstart = m_innermostloopstart;
                surroundingscopedepth = m_innermostloopscopedepth;
                /*
                // we'll be jumping back to right before the
                // statements after the loop body
                */
                m_innermostloopstart = currentBlob()->m_count;
                m_innermostloopscopedepth = m_currfunccompiler->m_scopedepth;
                parseStatement();
                consume(Token::TOK_KWWHILE, "expecting 'while' statement");
                parseExpression();
                exitjump = emitJump(Instruction::OP_JUMPIFFALSE);
                emitInstruction(Instruction::OP_POPONE);
                emitLoop(m_innermostloopstart);
                emitPatchJump(exitjump);
                emitInstruction(Instruction::OP_POPONE);
                endLoop();
                m_innermostloopstart = surroundingloopstart;
                m_innermostloopscopedepth = surroundingscopedepth;
            }

            void parseContinueStatement()
            {
                if(m_innermostloopstart == -1)
                {
                    raiseError("'continue' can only be used in a loop");
                }
                /*
                // discard local variables created in the loop
                //  discard_local(, m_innermostloopscopedepth);
                */
                discardLocalVars(m_innermostloopscopedepth + 1);
                /* go back to the top of the loop */
                emitLoop(m_innermostloopstart);
                consumeStmtEnd();
            }

            void parseBreakStatement()
            {
                if(m_innermostloopstart == -1)
                {
                    raiseError("'break' can only be used in a loop");
                }
                /* discard local variables created in the loop */
                /*
                int i;
                for(i = m_currfunccompiler->m_localcount - 1; i >= 0 && m_currfunccompiler->m_compiledlocals[i].depth >= m_currfunccompiler->m_scopedepth; i--)
                {
                    if(m_currfunccompiler->m_compiledlocals[i].iscaptured)
                    {
                        emitInstruction(Instruction::OP_UPVALUECLOSE);
                    }
                    else
                    {
                        emitInstruction(Instruction::OP_POPONE);
                    }
                }
                */
                discardLocalVars(m_innermostloopscopedepth + 1);
                emitJump(Instruction::OP_BREAK_PL);
                consumeStmtEnd();
            }

            void parseStatement()
            {
                m_replcanecho = false;
                ignoreSpace();
                if(match(Token::TOK_KWECHO))
                {
                    parseEchoStatement();
                }
                else if(match(Token::TOK_KWIF))
                {
                    parseIfStatement();
                }
                else if(match(Token::TOK_KWDO))
                {
                    parseLoopDoWhileStatement();
                }
                else if(match(Token::TOK_KWWHILE))
                {
                    parseLoopWhileStatement();
                }
                else if(match(Token::TOK_KWFOR))
                {
                    parseForStatement();
                }
                else if(match(Token::TOK_KWFOREACH))
                {
                    parseForeachStatement();
                }
                else if(match(Token::TOK_KWSWITCH))
                {
                    parseSwitchStatement();
                }
                else if(match(Token::TOK_KWCONTINUE))
                {
                    parseContinueStatement();
                }
                else if(match(Token::TOK_KWBREAK))
                {
                    parseBreakStatement();
                }
                else if(match(Token::TOK_KWRETURN))
                {
                    parseReturnStatement();
                }
                else if(match(Token::TOK_KWASSERT))
                {
                    parseAssertStatement();
                }
                else if(match(Token::TOK_KWTHROW))
                {
                    parseThrowStatement();
                }
                else if(match(Token::TOK_BRACEOPEN))
                {
                    scopeBegin();
                    parseBlock();
                    scopeEnd();
                }
                else if(match(Token::TOK_KWTRY))
                {
                    parseTryStatement();
                }
                else
                {
                    parseExprStatement(false, false);
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

namespace neon
{
    FuncScript* Module::compileModuleSource(State* state, Module* module, const char* source, Blob* blob, bool fromimport, bool keeplast)
    {
        return Parser::compileSource(state, module, source, blob, fromimport, keeplast);
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
                    dict->m_keynames->gcMark(state);
                    HashTable::mark(state, dict->m_valtable);
                }
                break;
            case Value::OBJTYPE_ARRAY:
                {
                    Array* list;
                    list = static_cast<Array*>(object);
                    list->m_varray->gcMark(state);
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
                    function->m_compiledblob->m_constants->gcMark(state);
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
                    state->m_vmstate->stackPush(Value::fromObject(newlist));
                    for(i = 0; i < list->size(); i++)
                    {
                        newlist->push(list->at(i));
                    }
                    state->m_vmstate->stackPop();
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

    FuncCommon::Type FuncCommon::getMethodType(Value method)
    {
        switch(method.objectType())
        {
            case Value::OBJTYPE_FUNCNATIVE:
                return method.asFuncNative()->m_functype;
                //return static_cast<FuncCommon*>(method.asObject())->m_functype;
            case Value::OBJTYPE_FUNCCLOSURE:
                return method.asFuncClosure()->scriptfunc->m_functype;
                //return static_cast<FuncCommon*>(method.asObject())->m_functype;
            default:
                break;
        }
        return FUNCTYPE_FUNCTION;
    }

    #include "vmfuncs.h"

    ClassObject* State::vmGetClassFor(Value receiver)
    {
        if(receiver.isNumber())
        {
            return m_classprimnumber;
        }
        if(receiver.isObject())
        {
            switch(receiver.asObject()->m_objtype)
            {
                case Value::OBJTYPE_STRING:
                    return m_classprimstring;
                case Value::OBJTYPE_RANGE:
                    return m_classprimrange;
                case Value::OBJTYPE_ARRAY:
                    return m_classprimarray;
                case Value::OBJTYPE_DICT:
                    return m_classprimdict;
                case Value::OBJTYPE_FILE:
                    return m_classprimfile;
                /*
                case Value::OBJTYPE_FUNCBOUND:
                case Value::OBJTYPE_FUNCCLOSURE:
                case Value::OBJTYPE_FUNCSCRIPT:
                    return m_classprimcallable;
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
        CallFrame* frame;
        FuncScript* function;
        /* flush out anything on stdout first */
        fflush(stdout);
        frame = &m_vmstate->m_framevalues[m_vmstate->m_framecount - 1];
        function = frame->closure->scriptfunc;
        instruction = frame->inscode - function->m_compiledblob->m_instrucs - 1;
        line = function->m_compiledblob->m_instrucs[instruction].srcline;
        fprintf(stderr, "RuntimeError: ");
        fn_fprintf(stderr, format, args...);
        fprintf(stderr, " -> %s:%d ", function->m_inmodule->m_physlocation->data(), line);
        fputs("\n", stderr);
        if(m_vmstate->m_framecount > 1)
        {
            fprintf(stderr, "stacktrace:\n");
            for(i = m_vmstate->m_framecount - 1; i >= 0; i--)
            {
                frame = &m_vmstate->m_framevalues[i];
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
        m_vmstate->resetVMState();
    }

    ClassInstance* State::makeExceptionInstance(ClassObject* exklass, String* message)
    {
        ClassInstance* instance;
        instance = ClassInstance::make(this, exklass);
        m_vmstate->stackPush(Value::fromObject(instance));
        instance->defProperty("message", Value::fromObject(message));
        m_vmstate->stackPop();
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
        m_vmstate->stackPush(Value::fromObject(instance));
        stacktrace = m_vmstate->getExceptionStacktrace();
        m_vmstate->stackPush(stacktrace);
        instance->defProperty("stacktrace", stacktrace);
        m_vmstate->stackPop();
        return m_vmstate->exceptionPropagate();
    }

    ClassObject* State::makeExceptionClass(Module* module, const char* cstrname)
    {
        int messageconst;
        ClassObject* klass;
        String* classname;
        FuncScript* function;
        FuncClosure* closure;
        classname = String::copy(this, cstrname);
        m_vmstate->stackPush(Value::fromObject(classname));
        klass = ClassObject::make(this, classname, nullptr);
        m_vmstate->stackPop();
        m_vmstate->stackPush(Value::fromObject(klass));
        function = FuncScript::make(this, module, FuncCommon::FUNCTYPE_METHOD);
        function->m_arity = 1;
        function->m_isvariadic = false;
        m_vmstate->stackPush(Value::fromObject(function));
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
        m_vmstate->stackPop();
        /* set class constructor */
        m_vmstate->stackPush(Value::fromObject(closure));
        klass->m_methods->set(Value::fromObject(classname), Value::fromObject(closure));
        klass->m_constructor = Value::fromObject(closure);
        /* set class properties */
        klass->defProperty("message", Value::makeNull());
        klass->defProperty("stacktrace", Value::makeNull());
        m_definedglobals->set(Value::fromObject(classname), Value::fromObject(klass));
        m_namedexceptions->set(Value::fromObject(classname), Value::fromObject(klass));
        /* for class */
        m_vmstate->stackPop();
        m_vmstate->stackPop();
        /* assert error name */
        /* m_vmstate->stackPop(); */
        return klass;
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
    argcount = prs->parseFuncCallArgs();
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
    name = prs->makeIdentConst(&prs->m_prevtoken);
    if(prs->match(neon::Token::TOK_PARENOPEN))
    {
        argcount = prs->parseFuncCallArgs();
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
        prs->parseAssign(getop, setop, name, canassign);
    }
    return true;
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
                        prs->emitNamedVar(prs->m_prevtoken, false);
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
    prs->parseAssign(getop, neon::Instruction::OP_INDEXSET, -1, assignable);
    return true;
}

bool nn_astparser_rulevarnormal(neon::Parser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    prs->emitNamedVar(prs->m_prevtoken, canassign);
    return true;
}

bool nn_astparser_rulevarglobal(neon::Parser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    prs->emitNamedVar(prs->m_prevtoken, canassign);
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
        prs->emitNamedVar(prs->m_prevtoken, false);
        //prs->emitNamedVar(neon::Parser::makeSynthToken(neon::g_strthis), false);
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
        name = prs->makeIdentConst(&prs->m_prevtoken);
    }
    else
    {
        invokeself = true;
    }
    prs->emitNamedVar(neon::Parser::makeSynthToken(neon::g_strthis), false);
    if(prs->match(neon::Token::TOK_PARENOPEN))
    {
        argcount = prs->parseFuncCallArgs();
        prs->emitNamedVar(neon::Parser::makeSynthToken(neon::g_strsuper), false);
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
        prs->emitNamedVar(neon::Parser::makeSynthToken(neon::g_strsuper), false);
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



bool nn_astparser_ruleanonfunc(neon::Parser* prs, bool canassign)
{
    (void)canassign;
    neon::Parser::FuncCompiler compiler(prs, neon::FuncCommon::FUNCTYPE_FUNCTION, true);
    prs->scopeBegin();
    /* compile parameter list */
    prs->consume(neon::Token::TOK_PARENOPEN, "expected '(' at start of anonymous function");
    if(!prs->check(neon::Token::TOK_PARENCLOSE))
    {
        prs->parseFuncDeclParamList();
    }
    prs->consume(neon::Token::TOK_PARENCLOSE, "expected ')' after anonymous function parameters");
    compiler.compileBody(true, true);
    return true;
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
    newdict = state->m_vmstate->gcProtect(neon::Dictionary::make(state));
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
    newdict = state->m_vmstate->gcProtect(neon::Dictionary::make(state));
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
    list = state->m_vmstate->gcProtect(neon::Array::make(state));
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
    list = state->m_vmstate->gcProtect(neon::Array::make(state));
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
    namelist = state->m_vmstate->gcProtect(neon::Array::make(state));
    valuelist = state->m_vmstate->gcProtect(neon::Array::make(state));
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
    list = state->m_vmstate->gcProtect(neon::Array::make(state));
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
    neon::ValArray nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = args->thisval.asDict();
    callable = args->argv[0];
    //state->m_vmstate->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        if(arity > 0)
        {
            dict->m_valtable->get(dict->m_keynames->m_values[i], &value);
            nestargs[0] = value;
            if(arity > 1)
            {
                nestargs[1] = dict->m_keynames->m_values[i];
            }
        }
        nc.callNested(callable, args->thisval, nestargs, &unused);
    }
    /* pop the argument list */
    //state->m_vmstate->stackPop();
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
    neon::ValArray nestargs;
    neon::Dictionary* resultdict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = args->thisval.asDict();
    callable = args->argv[0];
    //state->m_vmstate->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    resultdict = state->m_vmstate->gcProtect(neon::Dictionary::make(state));
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        dict->m_valtable->get(dict->m_keynames->m_values[i], &value);
        if(arity > 0)
        {
            nestargs[0] = value;
            if(arity > 1)
            {
                nestargs[1] = dict->m_keynames->m_values[i];
            }
        }
        nc.callNested(callable, args->thisval, nestargs, &result);
        if(!neon::Value::isFalse(result))
        {
            resultdict->addEntry(dict->m_keynames->m_values[i], value);
        }
    }
    /* pop the call list */
    //state->m_vmstate->stackPop();
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
    neon::ValArray nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = args->thisval.asDict();
    callable = args->argv[0];
    //state->m_vmstate->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        if(arity > 0)
        {
            dict->m_valtable->get(dict->m_keynames->m_values[i], &value);
            nestargs[0] = value;
            if(arity > 1)
            {
                nestargs[1] = dict->m_keynames->m_values[i];
            }
        }
        nc.callNested(callable, args->thisval, nestargs, &result);
        if(!neon::Value::isFalse(result))
        {
            /* pop the call list */
            //state->m_vmstate->stackPop();
            return neon::Value::makeBool(true);
        }
    }
    /* pop the call list */
    //state->m_vmstate->stackPop();
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
    neon::ValArray nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = args->thisval.asDict();
    callable = args->argv[0];
    //state->m_vmstate->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        if(arity > 0)
        {
            dict->m_valtable->get(dict->m_keynames->m_values[i], &value);
            nestargs[0] = value;
            if(arity > 1)
            {
                nestargs[1] = dict->m_keynames->m_values[i];
            }
        }
        nc.callNested(callable, args->thisval, nestargs, &result);
        if(neon::Value::isFalse(result))
        {
            /* pop the call list */
            //state->m_vmstate->stackPop();
            return neon::Value::makeBool(false);
        }
    }
    /* pop the call list */
    //state->m_vmstate->stackPop();
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
    neon::ValArray nestargs;
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
    //state->m_vmstate->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = startindex; i < dict->m_keynames->m_count; i++)
    {
        /* only call map for non-empty values in a list. */
        if(!dict->m_keynames->m_values[i].isNull() && !dict->m_keynames->m_values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs[0] = accumulator;
                if(arity > 1)
                {
                    dict->m_valtable->get(dict->m_keynames->m_values[i], &value);
                    nestargs[1] = value;
                    if(arity > 2)
                    {
                        nestargs[2] = dict->m_keynames->m_values[i];
                        if(arity > 4)
                        {
                            nestargs[3] = args->thisval;
                        }
                    }
                }
            }
            nc.callNested(callable, args->thisval, nestargs, &accumulator);
        }
    }
    /* pop the call list */
    //state->m_vmstate->stackPop();
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
    file = state->m_vmstate->gcProtect(neon::File::make(state, nullptr, false, path, mode));
    file->selfOpenFile();
    return neon::Value::fromObject(file);
}

neon::Value nn_memberfunc_file_exists(neon::State* state, neon::Arguments* args)
{
    neon::String* file;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    file = args->argv[0].asString();
    return neon::Value::makeBool(osfn_fileexists(file->data()));
}


neon::Value nn_memberfunc_file_isfile(neon::State* state, neon::Arguments* args)
{
    neon::String* file;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    file = args->argv[0].asString();
    return neon::Value::makeBool(osfn_pathisfile(file->data()));
}

neon::Value nn_memberfunc_file_isdirectory(neon::State* state, neon::Arguments* args)
{
    neon::String* file;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    file = args->argv[0].asString();
    return neon::Value::makeBool(osfn_pathisdirectory(file->data()));
}

neon::Value nn_memberfunc_file_close(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    args->thisval.asFile()->selfCloseFile();
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_file_open(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    args->thisval.asFile()->selfOpenFile();
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
        if(!osfn_fileexists(file->m_filepath->data()))
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
    buffer = (char*)neon::VM::GC::allocate(state, sizeof(char), length + 1);
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
            file->selfOpenFile();
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
    dict = state->m_vmstate->gcProtect(neon::Dictionary::make(state));
    if(!file->m_isstd)
    {
        if(osfn_fileexists(file->m_filepath->data()))
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
        name = osfn_basename(file->m_filepath->data());
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
        newlist = state->m_vmstate->gcProtect(neon::Array::make(state));
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
    nlist = state->m_vmstate->gcProtect(neon::Array::make(state));
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
    newlist = state->m_vmstate->gcProtect(neon::Array::make(state));
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
    newlist = state->m_vmstate->gcProtect(neon::Array::make(state));
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
    newlist = state->m_vmstate->gcProtect(neon::Array::make(state));
    arglist = (neon::Array**)neon::VM::GC::allocate(state, sizeof(neon::Array*), args->argc);
    for(i = 0; i < size_t(args->argc); i++)
    {
        NEON_ARGS_CHECKTYPE(args, i, isArray);
        arglist[i] = args->argv[i].asArray();
    }
    for(i = 0; i < list->count(); i++)
    {
        alist = state->m_vmstate->gcProtect(neon::Array::make(state));
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
    newlist = state->m_vmstate->gcProtect(neon::Array::make(state));
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
        alist = state->m_vmstate->gcProtect(neon::Array::make(state));
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
    dict = state->m_vmstate->gcProtect(neon::Dictionary::make(state));
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
    neon::ValArray nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    list = args->thisval.asArray();
    callable = args->argv[0];
    //state->m_vmstate->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < list->count(); i++)
    {
        if(arity > 0)
        {
            nestargs[0] = list->at(i);
            if(arity > 1)
            {
                nestargs[1] = neon::Value::makeNumber(i);
            }
        }
        nc.callNested(callable, args->thisval, nestargs, &unused);
    }
    //state->m_vmstate->stackPop();
    return neon::Value::makeEmpty();
}


neon::Value nn_memberfunc_array_map(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    neon::Value res;
    neon::Value callable;
    neon::Array* selfarr;
    neon::ValArray nestargs;
    neon::Array* resultlist;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    selfarr = args->thisval.asArray();
    callable = args->argv[0];
    //state->m_vmstate->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    resultlist = state->m_vmstate->gcProtect(neon::Array::make(state));
    //nestargs->push(neon::Value::makeEmpty());
    for(i = 0; i < selfarr->count(); i++)
    {
        #if 0
            if(!selfarr->at(i).isEmpty())
            {
                if(arity > 0 || arity == -1)
                {
                    nestargs[0] = selfarr->at(i);
                    if(arity > 1)
                    {
                        nestargs[1] = neon::Value::makeNumber(i);
                    }
                }
                nc.callNested(callable, args->thisval, nestargs, &res);
                resultlist->push(res);
            }
            else
            {
                resultlist->push(neon::Value::makeEmpty());
            }
        #else
            #if 1
                //nestargs.push(selfarr->at(i));
                nestargs[0] = selfarr->at(i);
                if(arity > 1)
                {
                    nestargs[1] = neon::Value::makeNumber(i);
                }
                nc.callNested(callable, args->thisval, nestargs, &res);
                resultlist->push(res);
            #else
                if(!selfarr->at(i).isEmpty())
                {
                    //if(arity > 0 || arity == -1)
                    {
                        nestargs[0] = selfarr->at(i);
                        if(arity > 1)
                        {
                            nestargs[1] = neon::Value::makeNumber(i);
                        }
                    }
                    nc.callNested(callable, args->thisval, nestargs, &res);
                    resultlist->push(res);
                }
                else
                {
                    resultlist->push(neon::Value::makeEmpty());
                }
            #endif
        #endif
    }
    //state->m_vmstate->stackPop();
    return neon::Value::fromObject(resultlist);
}


neon::Value nn_memberfunc_array_filter(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    neon::Value callable;
    neon::Value result;
    neon::Array* selfarr;
    neon::ValArray nestargs;
    neon::Array* resultlist;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    selfarr = args->thisval.asArray();
    callable = args->argv[0];
    //state->m_vmstate->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    resultlist = state->m_vmstate->gcProtect(neon::Array::make(state));
    for(i = 0; i < selfarr->count(); i++)
    {
        if(!selfarr->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs[0] = selfarr->at(i);
                if(arity > 1)
                {
                    nestargs[1] = neon::Value::makeNumber(i);
                }
            }
            nc.callNested(callable, args->thisval, nestargs, &result);
            if(!neon::Value::isFalse(result))
            {
                resultlist->push(selfarr->at(i));
            }
        }
    }
    //state->m_vmstate->stackPop();
    return neon::Value::fromObject(resultlist);
}

neon::Value nn_memberfunc_array_some(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    neon::Value callable;
    neon::Value result;
    neon::Array* selfarr;
    neon::ValArray nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    selfarr = args->thisval.asArray();
    callable = args->argv[0];
    //state->m_vmstate->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < selfarr->count(); i++)
    {
        if(!selfarr->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs[0] = selfarr->at(i);
                if(arity > 1)
                {
                    nestargs[1] = neon::Value::makeNumber(i);
                }
            }
            nc.callNested(callable, args->thisval, nestargs, &result);
            if(!neon::Value::isFalse(result))
            {
                state->m_vmstate->stackPop();
                return neon::Value::makeBool(true);
            }
        }
    }
    //state->m_vmstate->stackPop();
    return neon::Value::makeBool(false);
}


neon::Value nn_memberfunc_array_every(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    neon::Value result;
    neon::Value callable;
    neon::Array* selfarr;
    neon::ValArray nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    selfarr = args->thisval.asArray();
    callable = args->argv[0];
    //state->m_vmstate->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < selfarr->count(); i++)
    {
        if(!selfarr->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs[0] = selfarr->at(i);
                if(arity > 1)
                {
                    nestargs[1] = neon::Value::makeNumber(i);
                }
            }
            nc.callNested(callable, args->thisval, nestargs, &result);
            if(neon::Value::isFalse(result))
            {
                //state->m_vmstate->stackPop();
                return neon::Value::makeBool(false);
            }
        }
    }
    //state->m_vmstate->stackPop();
    return neon::Value::makeBool(true);
}

neon::Value nn_memberfunc_array_reduce(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int arity;
    int startindex;
    neon::Value callable;
    neon::Value accumulator;
    neon::Array* selfarr;
    neon::ValArray nestargs;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    selfarr = args->thisval.asArray();
    callable = args->argv[0];
    startindex = 0;
    accumulator = neon::Value::makeNull();
    if(args->argc == 2)
    {
        accumulator = args->argv[1];
    }
    if(accumulator.isNull() && selfarr->count() > 0)
    {
        accumulator = selfarr->at(0);
        startindex = 1;
    }
    //state->m_vmstate->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = startindex; i < selfarr->count(); i++)
    {
        if(!selfarr->at(i).isNull() && !selfarr->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs[0] = accumulator;
                if(arity > 1)
                {
                    nestargs[1] = selfarr->at(i);
                    if(arity > 2)
                    {
                        nestargs[2] = neon::Value::makeNumber(i);
                        if(arity > 4)
                        {
                            nestargs[3] = args->thisval;
                        }
                    }
                }
            }
            nc.callNested(callable, args->thisval, nestargs, &accumulator);
        }
    }
    //state->m_vmstate->stackPop();
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
    neon::ValArray nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    range = args->thisval.asRange();
    callable = args->argv[0];
    //state->m_vmstate->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < range->m_range; i++)
    {
        if(arity > 0)
        {
            nestargs[0] = neon::Value::makeNumber(i);
            if(arity > 1)
            {
                nestargs[1] = neon::Value::makeNumber(i);
            }
        }
        nc.callNested(callable, args->thisval, nestargs, &unused);
    }
    //state->m_vmstate->stackPop();
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
    list = state->m_vmstate->gcProtect(neon::Array::make(state));
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
    fill = (char*)neon::VM::GC::allocate(state, sizeof(char), (size_t)fillsize + 1);
    finalsize = string->m_sbuf->m_length + fillsize;
    finalutf8size = string->m_sbuf->m_length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)neon::VM::GC::allocate(state, sizeof(char), (size_t)finalsize + 1);
    memcpy(str, fill, fillsize);
    memcpy(str + fillsize, string->m_sbuf->m_data, string->m_sbuf->m_length);
    str[finalsize] = '\0';
    neon::Memory::freeArray(fill, fillsize + 1);
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
    fill = (char*)neon::VM::GC::allocate(state, sizeof(char), (size_t)fillsize + 1);
    finalsize = string->m_sbuf->m_length + fillsize;
    finalutf8size = string->m_sbuf->m_length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)neon::VM::GC::allocate(state, sizeof(char), (size_t)finalsize + 1);
    memcpy(str, string->m_sbuf->m_data, string->m_sbuf->m_length);
    memcpy(str + string->m_sbuf->m_length, fill, fillsize);
    str[finalsize] = '\0';
    neon::Memory::freeArray(fill, fillsize + 1);
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
    list = state->m_vmstate->gcProtect(neon::Array::make(state));
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
    neon::ValArray nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    string = args->thisval.asString();
    callable = args->argv[0];
    //state->m_vmstate->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = 0; i < (int)string->length(); i++)
    {
        if(arity > 0)
        {
            nestargs[0] = neon::Value::fromObject(neon::String::copy(state, string->data() + i, 1));
            if(arity > 1)
            {
                nestargs[1] = neon::Value::makeNumber(i);
            }
        }
        nc.callNested(callable, args->thisval, nestargs, &unused);
    }
    /* pop the argument list */
    //state->m_vmstate->stackPop();
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
    return neon::Value::makeBool(neon::ClassObject::instanceOf(args->argv[0].asInstance()->m_fromclass, args->argv[1].asClass()));
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


    void State::defGlobalValue(const char* name, Value val)
    {
        String* oname;
        oname = String::copy(this, name);
        m_vmstate->stackPush(Value::fromObject(oname));
        m_vmstate->stackPush(val);
        m_definedglobals->set(m_vmstate->m_stackvalues[0], m_vmstate->m_stackvalues[1]);
        m_vmstate->stackPop(2);
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
        cl = ClassObject::make(this, os, parent);
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
            m_vmstate->stackPush(Value::fromObject(function));
        }
        else
        {
            function->m_scriptfnname = String::copy(this, "(evaledcode)");
        }
        closure = FuncClosure::make(this, function);
        if(!fromeval)
        {
            m_vmstate->stackPop();
            m_vmstate->stackPush(Value::fromObject(closure));
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
        m_vmstate->callClosure(closure, Value::makeNull(), 0);
        status = m_vmstate->runVM(0, dest);
        return status;
    }

    Value State::evalSource(const char* source)
    {
        bool ok;
        int argc;
        Value callme;
        Value retval;
        FuncClosure* closure;
        ValArray args;
        (void)argc;
        closure = compileScriptSource(m_toplevelmodule, true, source);
        callme = Value::fromObject(closure);
        NestCall nc(this);
        argc = nc.prepare(callme, Value::makeNull(), args);
        ok = nc.callNested(callme, Value::makeNull(), args, &retval);
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
        m_vmstate->gcDestroyLinkedObjects();
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
        Memory::osFree(m_vmstate->m_framevalues);
        destrdebug("destroying stackvalues...");
        Memory::osFree(m_vmstate->m_stackvalues);
        destrdebug("destroying this...");
        Memory::destroy(m_vmstate);
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
        m_vmstate = neon::Memory::create<neon::VM>(this);
        m_vmstate->m_currentmarkvalue = true;
        m_vmstate->resetVMState();
        {
            m_vmstate->m_gcbytesallocated = 0;
            /* default is 1mb. Can be modified via the -g flag. */
            m_vmstate->m_gcnextgc = NEON_CFG_DEFAULTGCSTART;
            m_vmstate->m_gcgraycount = 0;
            m_vmstate->m_gcgraycapacity = 0;
            m_vmstate->m_gcgraystack = nullptr;
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
            m_modimportpath = Memory::create<ValArray>();
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


static NEON_FORCEINLINE neon::ScopeUpvalue* nn_vmutil_upvaluescapture(neon::State* state, neon::Value* local, int stackpos)
{
    neon::ScopeUpvalue* upvalue;
    neon::ScopeUpvalue* prevupvalue;
    neon::ScopeUpvalue* createdupvalue;
    prevupvalue = nullptr;
    upvalue = state->m_vmstate->m_openupvalues;
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
        state->m_vmstate->m_openupvalues = createdupvalue;
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
    while(state->m_vmstate->m_openupvalues != nullptr && (&state->m_vmstate->m_openupvalues->m_location) >= last)
    {
        upvalue = state->m_vmstate->m_openupvalues;
        upvalue->m_closed = upvalue->m_location;
        upvalue->m_location = upvalue->m_closed;
        state->m_vmstate->m_openupvalues = upvalue->m_nextupval;
    }
}

static NEON_FORCEINLINE void nn_vmutil_definemethod(neon::State* state, neon::String* name)
{
    neon::Value method;
    neon::ClassObject* klass;
    method = state->m_vmstate->stackPeek(0);
    klass = state->m_vmstate->stackPeek(1).asClass();
    klass->m_methods->set(neon::Value::fromObject(name), method);
    if(neon::FuncCommon::getMethodType(method) == neon::FuncCommon::FUNCTYPE_INITIALIZER)
    {
        klass->m_constructor = method;
    }
    state->m_vmstate->stackPop();
}

static NEON_FORCEINLINE void nn_vmutil_defineproperty(neon::State* state, neon::String* name, bool isstatic)
{
    neon::Value property;
    neon::ClassObject* klass;
    property = state->m_vmstate->stackPeek(0);
    klass = state->m_vmstate->stackPeek(1).asClass();
    if(!isstatic)
    {
        klass->defProperty(name->data(), property);
    }
    else
    {
        klass->setStaticProperty(name, property);
    }
    state->m_vmstate->stackPop();
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
    state->m_vmstate->stackPush(neon::Value::fromObject(list));
    for(i = 0; i < a->count(); i++)
    {
        list->push(a->at(i));
    }
    for(i = 0; i < b->count(); i++)
    {
        list->push(b->at(i));
    }
    state->m_vmstate->stackPop();
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
    valupper = state->m_vmstate->stackPeek(0);
    vallower = state->m_vmstate->stackPeek(1);
    if(!(vallower.isNull() || vallower.isNumber()) || !(valupper.isNumber() || valupper.isNull()))
    {
        state->m_vmstate->stackPop(2);
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
            state->m_vmstate->stackPop(3);
        }
        state->m_vmstate->stackPush(neon::Value::fromObject(neon::Array::make(state)));
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
    state->m_vmstate->stackPush(neon::Value::fromObject(newlist));
    for(i = idxlower; i < idxupper; i++)
    {
        newlist->push(list->at(i));
    }
    /* clear gc protect */
    state->m_vmstate->stackPop();
    if(!willassign)
    {
        /* +1 for the list itself */
        state->m_vmstate->stackPop(3);
    }
    state->m_vmstate->stackPush(neon::Value::fromObject(newlist));
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
    valupper = state->m_vmstate->stackPeek(0);
    vallower = state->m_vmstate->stackPeek(1);
    if(!(vallower.isNull() || vallower.isNumber()) || !(valupper.isNumber() || valupper.isNull()))
    {
        state->m_vmstate->stackPop(2);
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
            state->m_vmstate->stackPop(3);
        }
        state->m_vmstate->stackPush(neon::Value::fromObject(neon::String::copy(state, "", 0)));
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
        state->m_vmstate->stackPop(3);
    }
    state->m_vmstate->stackPush(neon::Value::fromObject(neon::String::copy(state, string->data() + start, end - start)));
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_getrangedindex(neon::State* state)
{
    bool isgotten;
    uint8_t willassign;
    neon::Value vfrom;
    willassign = state->m_vmstate->readByte();
    isgotten = true;
    vfrom = state->m_vmstate->stackPeek(2);
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
    vindex = state->m_vmstate->stackPeek(0);
    field = dict->getEntry(vindex);
    if(field != nullptr)
    {
        if(!willassign)
        {
            /* we can safely get rid of the index from the stack */
            state->m_vmstate->stackPop(2);
        }
        state->m_vmstate->stackPush(field->m_actualval);
        return true;
    }
    state->m_vmstate->stackPop(1);
    state->m_vmstate->stackPush(neon::Value::makeEmpty());
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_doindexgetmodule(neon::State* state, neon::Module* module, bool willassign)
{
    neon::Value vindex;
    neon::Value result;
    vindex = state->m_vmstate->stackPeek(0);
    if(module->m_deftable->get(vindex, &result))
    {
        if(!willassign)
        {
            /* we can safely get rid of the index from the stack */
            state->m_vmstate->stackPop(2);
        }
        state->m_vmstate->stackPush(result);
        return true;
    }
    state->m_vmstate->stackPop();
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
    vindex = state->m_vmstate->stackPeek(0);
    if(!vindex.isNumber())
    {
        if(vindex.isRange())
        {
            rng = vindex.asRange();
            state->m_vmstate->stackPop();
            state->m_vmstate->stackPush(neon::Value::makeNumber(rng->m_lower));
            state->m_vmstate->stackPush(neon::Value::makeNumber(rng->m_upper));
            return nn_vmutil_dogetrangedindexofstring(state, string, willassign);
        }
        state->m_vmstate->stackPop(1);
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
            state->m_vmstate->stackPop(2);
        }
        state->m_vmstate->stackPush(neon::Value::fromObject(neon::String::copy(state, string->data() + start, end - start)));
        return true;
    }
    state->m_vmstate->stackPop(1);
    state->m_vmstate->stackPush(neon::Value::makeEmpty());
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_doindexgetarray(neon::State* state, neon::Array* list, bool willassign)
{
    int index;
    neon::Value finalval;
    neon::Value vindex;
    neon::Range* rng;
    vindex = state->m_vmstate->stackPeek(0);
    if(NEON_UNLIKELY(!vindex.isNumber()))
    {
        if(vindex.isRange())
        {
            rng = vindex.asRange();
            state->m_vmstate->stackPop();
            state->m_vmstate->stackPush(neon::Value::makeNumber(rng->m_lower));
            state->m_vmstate->stackPush(neon::Value::makeNumber(rng->m_upper));
            return nn_vmutil_dogetrangedindexofarray(state, list, willassign);
        }
        state->m_vmstate->stackPop();
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
        state->m_vmstate->stackPop(2);
    }
    state->m_vmstate->stackPush(finalval);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_indexget(neon::State* state)
{
    bool isgotten;
    uint8_t willassign;
    neon::Value peeked;
    willassign = state->m_vmstate->readByte();
    isgotten = true;
    peeked = state->m_vmstate->stackPeek(1);
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
    state->m_vmstate->stackPop(3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = dict[index] = 10
    */
    state->m_vmstate->stackPush(value);
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_dosetindexmodule(neon::State* state, neon::Module* module, neon::Value index, neon::Value value)
{
    module->m_deftable->set(index, value);
    /* pop the value, index and dict out */
    state->m_vmstate->stackPop(3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = dict[index] = 10
    */
    state->m_vmstate->stackPush(value);
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
        state->m_vmstate->stackPop(3);
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
    state->m_vmstate->stackPop(3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = list[index] = 10    
    */
    state->m_vmstate->stackPush(value);
    return true;
    /*
    // pop the value, index and list out
    //state->m_vmstate->stackPop(3);
    //return state->raiseClass(state->m_exceptions.stdexception, "lists index %d out of range", rawpos);
    //state->m_vmstate->stackPush(Value::makeEmpty());
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
        state->m_vmstate->stackPop(3);
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
        state->m_vmstate->stackPop(3);
        /*
        // leave the value on the stack for consumption
        // e.g. variable = list[index] = 10
        */
        state->m_vmstate->stackPush(value);
        return true;
    }
    else
    {
        os->append(inchar);
        state->m_vmstate->stackPop(3);
        state->m_vmstate->stackPush(value);
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
    target = state->m_vmstate->stackPeek(2);
    if(NEON_LIKELY(target.isObject()))
    {
        value = state->m_vmstate->stackPeek(0);
        index = state->m_vmstate->stackPeek(1);
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
        return state->raiseClass(state->m_exceptions.stdexception, "type of %s is not a valid iterable", neon::Value::Typename(state->m_vmstate->stackPeek(3)));
    }
    return true;
}

static NEON_FORCEINLINE bool nn_vmutil_concatenate(neon::State* state, bool ontoself)
{
    neon::Value vleft;
    neon::Value vright;
    neon::String* result;
    (void)ontoself;
    vright = state->m_vmstate->stackPeek(0);
    vleft = state->m_vmstate->stackPeek(1);
    neon::Printer pd(state);
    pd.printValue(vleft, false, true);
    pd.printValue(vright, false, true);
    result = pd.takeString();
    state->m_vmstate->stackPop(2);
    state->m_vmstate->stackPush(neon::Value::fromObject(result));
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
                    return field;
                }
                state->raiseClass(state->m_exceptions.stdexception, "%s module does not define '%s'", module->m_modname, name->data());
                return nullptr;
            }
            break;
        case neon::Value::OBJTYPE_CLASS:
            {
                neon::ClassObject* klass;
                klass = peeked.asClass();
                field = klass->m_methods->getByObjString(name);
                if(field != nullptr)
                {
                    if(neon::FuncCommon::getMethodType(field->m_actualval) == neon::FuncCommon::FUNCTYPE_STATIC)
                    {
                        return field;
                    }
                    return field;
                }
                field = klass->getStaticProperty(name);
                if(field != nullptr)
                {
                    return field;
                }
                field = klass->m_staticmethods->getByObjString(name);
                if(field != nullptr)
                {
                    return field;
                }
                state->raiseClass(state->m_exceptions.stdexception, "class '%s' does not have a static property or method named '%s'",
                    klass->m_classname->data(), name->data());
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
                    return field;
                }
                if(state->m_vmstate->vmBindMethod(instance->m_fromclass, name))
                {
                    return field;
                }
                state->raiseClass(state->m_exceptions.stdexception, "instance of class '%s' does not have a property or method named '%s'",
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
    name = state->m_vmstate->readString();
    peeked = state->m_vmstate->stackPeek(0);
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
                state->m_vmstate->vmCallBoundValue(field->m_actualval, peeked, 0);
            }
            else
            {
                state->m_vmstate->stackPop();
                state->m_vmstate->stackPush(field->m_actualval);
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
    name = state->m_vmstate->readString();
    peeked = state->m_vmstate->stackPeek(0);
    if(peeked.isInstance())
    {
        instance = peeked.asInstance();
        field = instance->m_properties->getByObjString(name);
        if(field != nullptr)
        {
            /* pop the instance... */
            state->m_vmstate->stackPop();
            state->m_vmstate->stackPush(field->m_actualval);
            return true;
        }
        if(state->m_vmstate->vmBindMethod(instance->m_fromclass, name))
        {
            return true;
        }
        NEON_VMMAC_TRYRAISE(state, false, "instance of class %s does not have a property or method named '%s'",
            peeked.asInstance()->m_fromclass->m_classname->data(), name->data());
        return false;
    }
    else if(peeked.isClass())
    {
        klass = peeked.asClass();
        field = klass->m_methods->getByObjString(name);
        if(field != nullptr)
        {
            if(neon::FuncCommon::getMethodType(field->m_actualval) == neon::FuncCommon::FUNCTYPE_STATIC)
            {
                /* pop the class... */
                state->m_vmstate->stackPop();
                state->m_vmstate->stackPush(field->m_actualval);
                return true;
            }
        }
        else
        {
            field = klass->getStaticProperty(name);
            if(field != nullptr)
            {
                /* pop the class... */
                state->m_vmstate->stackPop();
                state->m_vmstate->stackPush(field->m_actualval);
                return true;
            }
        }
        NEON_VMMAC_TRYRAISE(state, false, "cannot get method '%s' from instance of class '%s'", klass->m_classname->data(), name->data());
        return false;
    }
    else if(peeked.isModule())
    {
        module = peeked.asModule();
        field = module->m_deftable->getByObjString(name);
        if(field != nullptr)
        {
            /* pop the module... */
            state->m_vmstate->stackPop();
            state->m_vmstate->stackPush(field->m_actualval);
            return true;
        }
        NEON_VMMAC_TRYRAISE(state, false, "module %s does not define '%s'", module->m_modname, name->data());
        return false;
    }
    NEON_VMMAC_TRYRAISE(state, false, "'%s' of type %s does not have properties", neon::Value::toString(state, peeked)->data(),
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
    vtarget = state->m_vmstate->stackPeek(1);
    if(!vtarget.isClass() && !vtarget.isInstance() && !vtarget.isDict())
    {
        state->raiseClass(state->m_exceptions.stdexception, "object of type %s cannot carry properties", neon::Value::Typename(vtarget));
        return false;
    }
    else if(state->m_vmstate->stackPeek(0).isEmpty())
    {
        state->raiseClass(state->m_exceptions.stdexception, "empty cannot be assigned");
        return false;
    }
    name = state->m_vmstate->readString();
    vpeek = state->m_vmstate->stackPeek(0);
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
        value = state->m_vmstate->stackPop();
        /* removing the class object */
        state->m_vmstate->stackPop();
        state->m_vmstate->stackPush(value);
    }
    else if(vtarget.isInstance())
    {
        instance = vtarget.asInstance();
        instance->defProperty(name->data(), vpeek);
        value = state->m_vmstate->stackPop();
        /* removing the instance object */
        state->m_vmstate->stackPop();
        state->m_vmstate->stackPush(value);
    }
    else
    {
        dict = vtarget.asDict();
        dict->setEntry(neon::Value::fromObject(name), vpeek);
        value = state->m_vmstate->stackPop();
        /* removing the dictionary object */
        state->m_vmstate->stackPop();
        state->m_vmstate->stackPush(value);
    }
    return true;
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
    instruction = (neon::Instruction::OpCode)state->m_vmstate->m_currentinstr.code;
    binvalright = state->m_vmstate->stackPeek(0);
    binvalleft = state->m_vmstate->stackPeek(1);
    isfail = (
        (!binvalright.isNumber() && !binvalright.isBool() && !binvalright.isNull()) ||
        (!binvalleft.isNumber() && !binvalleft.isBool() && !binvalleft.isNull())
    );
    if(isfail)
    {
        NEON_VMMAC_TRYRAISE(state, false, "unsupported operand %s for %s and %s", neon::Instruction::opName(instruction), neon::Value::Typename(binvalleft), neon::Value::Typename(binvalright));
        return false;
    }
    binvalright = state->m_vmstate->stackPop();
    binvalleft = state->m_vmstate->stackPop();
    res = neon::Value::makeEmpty();
    switch(instruction)
    {
        case neon::Instruction::OP_PRIMADD:
            {
                dbinright = binvalright.coerceToNumber();
                dbinleft = binvalleft.coerceToNumber();
                res = neon::Value::makeNumber(dbinleft + dbinright);
            }
            break;
        case neon::Instruction::OP_PRIMSUBTRACT:
            {
                dbinright = binvalright.coerceToNumber();
                dbinleft = binvalleft.coerceToNumber();
                res = neon::Value::makeNumber(dbinleft - dbinright);
            }
            break;
        case neon::Instruction::OP_PRIMDIVIDE:
            {
                dbinright = binvalright.coerceToNumber();
                dbinleft = binvalleft.coerceToNumber();
                res = neon::Value::makeNumber(dbinleft / dbinright);
            }
            break;
        case neon::Instruction::OP_PRIMMULTIPLY:
            {
                dbinright = binvalright.coerceToNumber();
                dbinleft = binvalleft.coerceToNumber();
                res = neon::Value::makeNumber(dbinleft * dbinright);
            }
            break;
        case neon::Instruction::OP_PRIMAND:
            {
                ibinright = binvalright.coerceToInt();
                ibinleft = binvalleft.coerceToInt();
                res = neon::Value::makeInt(ibinleft & ibinright);
            }
            break;
        case neon::Instruction::OP_PRIMOR:
            {
                ibinright = binvalright.coerceToInt();
                ibinleft = binvalleft.coerceToInt();
                res = neon::Value::makeInt(ibinleft | ibinright);
            }
            break;
        case neon::Instruction::OP_PRIMBITXOR:
            {
                ibinright = binvalright.coerceToInt();
                ibinleft = binvalleft.coerceToInt();
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
                ubinright = binvalright.coerceToUInt();
                ubinleft = binvalleft.coerceToUInt();
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
                ubinright = binvalright.coerceToUInt();
                ubinleft = binvalleft.coerceToUInt();
                ubinright &= 0x1f;
                res = neon::Value::makeInt(ubinleft >> ubinright);
            }
            break;
        case neon::Instruction::OP_PRIMGREATER:
            {
                dbinright = binvalright.coerceToNumber();
                dbinleft = binvalleft.coerceToNumber();
                res = neon::Value::makeBool(dbinleft > dbinright);
            }
            break;
        case neon::Instruction::OP_PRIMLESSTHAN:
            {
                dbinright = binvalright.coerceToNumber();
                dbinleft = binvalleft.coerceToNumber();
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
    state->m_vmstate->stackPush(res);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_globaldefine(neon::State* state)
{
    neon::Value val;
    neon::String* name;
    neon::HashTable* tab;
    name = state->m_vmstate->readString();
    val = state->m_vmstate->stackPeek(0);
    if(val.isEmpty())
    {
        NEON_VMMAC_TRYRAISE(state, false, "empty cannot be assigned");
        return false;
    }
    tab = state->m_vmstate->m_currentframe->closure->scriptfunc->m_inmodule->m_deftable;
    tab->set(neon::Value::fromObject(name), val);
    state->m_vmstate->stackPop();
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
    name = state->m_vmstate->readString();
    tab = state->m_vmstate->m_currentframe->closure->scriptfunc->m_inmodule->m_deftable;
    field = tab->getByObjString(name);
    if(field == nullptr)
    {
        field = state->m_definedglobals->getByObjString(name);
        if(field == nullptr)
        {
            NEON_VMMAC_TRYRAISE(state, false, "global name '%s' is not defined", name->data());
            return false;
        }
    }
    state->m_vmstate->stackPush(field->m_actualval);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_globalset(neon::State* state)
{
    neon::String* name;
    neon::HashTable* table;
    if(state->m_vmstate->stackPeek(0).isEmpty())
    {
        NEON_VMMAC_TRYRAISE(state, false, "empty cannot be assigned");
        return false;
    }
    name = state->m_vmstate->readString();
    table = state->m_vmstate->m_currentframe->closure->scriptfunc->m_inmodule->m_deftable;
    if(table->set(neon::Value::fromObject(name), state->m_vmstate->stackPeek(0)))
    {
        if(state->m_conf.enablestrictmode)
        {
            table->removeByKey(neon::Value::fromObject(name));
            NEON_VMMAC_TRYRAISE(state, false, "global name '%s' was not declared", name->data());
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
    slot = state->m_vmstate->readShort();
    ssp = state->m_vmstate->m_currentframe->stackslotpos;
    val = state->m_vmstate->m_stackvalues[ssp + slot];
    state->m_vmstate->stackPush(val);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_localset(neon::State* state)
{
    size_t ssp;
    uint16_t slot;
    neon::Value peeked;
    slot = state->m_vmstate->readShort();
    peeked = state->m_vmstate->stackPeek(0);
    if(peeked.isEmpty())
    {
        NEON_VMMAC_TRYRAISE(state, false, "empty cannot be assigned");
        return false;
    }
    ssp = state->m_vmstate->m_currentframe->stackslotpos;
    state->m_vmstate->m_stackvalues[ssp + slot] = peeked;
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_funcargget(neon::State* state)
{
    size_t ssp;
    uint16_t slot;
    neon::Value val;
    slot = state->m_vmstate->readShort();
    ssp = state->m_vmstate->m_currentframe->stackslotpos;
    //fprintf(stderr, "FUNCARGGET: %s\n", state->m_vmstate->m_currentframe->closure->scriptfunc->m_scriptfnname->data());
    val = state->m_vmstate->m_stackvalues[ssp + slot];
    state->m_vmstate->stackPush(val);
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_funcargset(neon::State* state)
{
    size_t ssp;
    uint16_t slot;
    neon::Value peeked;
    slot = state->m_vmstate->readShort();
    peeked = state->m_vmstate->stackPeek(0);
    if(peeked.isEmpty())
    {
        NEON_VMMAC_TRYRAISE(state, false, "empty cannot be assigned");
        return false;
    }
    ssp = state->m_vmstate->m_currentframe->stackslotpos;
    state->m_vmstate->m_stackvalues[ssp + slot] = peeked;
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
    function = state->m_vmstate->readConst().asFuncScript();
    closure = neon::FuncClosure::make(state, function);
    state->m_vmstate->stackPush(neon::Value::fromObject(closure));
    for(i = 0; i < closure->m_upvalcount; i++)
    {
        islocal = state->m_vmstate->readByte();
        index = state->m_vmstate->readShort();
        if(islocal)
        {
            ssp = state->m_vmstate->m_currentframe->stackslotpos;
            upvals = &state->m_vmstate->m_stackvalues[ssp + index];
            closure->m_storedupvals[i] = nn_vmutil_upvaluescapture(state, upvals, index);

        }
        else
        {
            closure->m_storedupvals[i] = state->m_vmstate->m_currentframe->closure->m_storedupvals[index];
        }
    }
    return true;
}

static NEON_FORCEINLINE bool nn_vmdo_makearray(neon::State* state)
{
    int i;
    int count;
    neon::Array* array;
    count = state->m_vmstate->readShort();
    array = neon::Array::make(state);
    state->m_vmstate->m_stackvalues[state->m_vmstate->m_stackidx + (-count - 1)] = neon::Value::fromObject(array);
    for(i = count - 1; i >= 0; i--)
    {
        array->push(state->m_vmstate->stackPeek(i));
    }
    state->m_vmstate->stackPop(count);
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
    realcount = state->m_vmstate->readShort();
    count = realcount * 2;
    dict = neon::Dictionary::make(state);
    state->m_vmstate->m_stackvalues[state->m_vmstate->m_stackidx + (-count - 1)] = neon::Value::fromObject(dict);
    for(i = 0; i < count; i += 2)
    {
        name = state->m_vmstate->m_stackvalues[state->m_vmstate->m_stackidx + (-count + i)];
        if(!name.isString() && !name.isNumber() && !name.isBool())
        {
            NEON_VMMAC_TRYRAISE(state, false, "dictionary key must be one of string, number or boolean");
            return false;
        }
        value = state->m_vmstate->m_stackvalues[state->m_vmstate->m_stackidx + (-count + i + 1)];
        dict->setEntry(name, value);
    }
    state->m_vmstate->stackPop(count);
    return true;
}

#include "n.h"

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
    ptyp(neon::ExceptionFrame);
    ptyp(neon::CallFrame);
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
    neon::Util::setOSUnicode();
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
    state->m_vmstate->m_gcnextgc = nextgcstart;
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


