
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
    #define NEON_PLAT_ISLINUX
#endif

#include "strbuf.h"
#include "optparse.h"
#include "os.h"

#if 0 //defined(__GNUC__)
    #define NEON_ALWAYSINLINE __attribute__((always_inline)) inline
#else
    #define NEON_ALWAYSINLINE inline
#endif

#define NEON_CFG_FILEEXT ".nn"

/* global debug mode flag */
#define NEON_CFG_BUILDDEBUGMODE 0

#if NEON_CFG_BUILDDEBUGMODE == 1
    #define DEBUG_PRINT_CODE 1
    #define DEBUG_TABLE 0
    #define DEBUG_GC 1
    #define DEBUG_STACK 0
#endif

/* initial amount of frames (will grow dynamically if needed) */
#define NEON_CFG_INITFRAMECOUNT (32)

/* initial amount of stack values (will grow dynamically if needed) */
#define NEON_CFG_INITSTACKCOUNT (32 * 1)

/* how many locals per function can be compiled */
#define NEON_CFG_ASTMAXLOCALS 64

/* how many upvalues per function can be compiled */
#define NEON_CFG_ASTMAXUPVALS 64

/* how many switch cases per switch statement */
#define NEON_CFG_ASTMAXSWITCHCASES 32

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

#if defined(__GNUC__)
    #define NEON_ATTR_PRINTFLIKE(fmtarg, firstvararg) __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#else
    #define NEON_ATTR_PRINTFLIKE(a, b)
#endif

/*
// NOTE:
// 1. Any call to gcProtect() within a function/block must be accompanied by
// at least one call to clearProtect() before exiting the function/block
// otherwise, expected unexpected behavior
// 2. The call to clearProtect() will be automatic for native functions.
// 3. $thisval must be retrieved before any call to nn_gcmem_protect in a
// native function.
*/
#define GROW_CAPACITY(capacity) \
    ((capacity) < 4 ? 4 : (capacity)*2)

#define nn_gcmem_growarray(state, typsz, pointer, oldcount, newcount) \
    neon::State::GC::reallocate(state, pointer, (typsz) * (oldcount), (typsz) * (newcount))

#define nn_gcmem_freearray(state, typsz, pointer, oldcount) \
    neon::State::GC::release(state, pointer, (typsz) * (oldcount))

#define NORMALIZE(token) #token

#define nn_exceptions_throwclass(state, exklass, ...) \
    state->raiseClass(exklass, __FILE__, __LINE__, __VA_ARGS__)

#define nn_exceptions_throwException(state, ...) \
    nn_exceptions_throwclass(state, state->m_exceptions.stdexception, __VA_ARGS__)

#if defined(__linux__) || defined(__CYGWIN__) || defined(__MINGW32_MAJOR_VERSION)
    #include <libgen.h>
    #include <limits.h>
#endif

#define NEON_RETURNERROR(...) \
    { \
        state->stackPop(args->argc); \
        nn_exceptions_throwException(state, ##__VA_ARGS__); \
    } \
    return neon::Value::makeBool(false);

#define NEON_ARGS_FAIL(chp, ...) \
    (chp)->fail(__FILE__, __LINE__, __VA_ARGS__)

/* check for exact number of arguments $d */
#define NEON_ARGS_CHECKCOUNT(chp, d) \
    if((chp)->argc != (d)) \
    { \
        return NEON_ARGS_FAIL(chp, "%s() expects %d arguments, %d given", (chp)->name, d, (chp)->argc); \
    }

/* check for miminum args $d ($d ... n) */
#define NEON_ARGS_CHECKMINARG(chp, d) \
    if((chp)->argc < (d)) \
    { \
        return NEON_ARGS_FAIL(chp, "%s() expects minimum of %d arguments, %d given", (chp)->name, d, (chp)->argc); \
    }

/* check for range of args ($low .. $up) */
#define NEON_ARGS_CHECKCOUNTRANGE(chp, low, up) \
    if((chp)->argc < (low) || (chp)->argc > (up)) \
    { \
        return NEON_ARGS_FAIL(chp, "%s() expects between %d and %d arguments, %d given", (chp)->name, low, up, (chp)->argc); \
    }

/* check for argument at index $i for $type, where $type is a nn_value_is*() function */
#if 1
    #define NEON_ARGS_CHECKTYPE(chp, i, __callfn__) \
        if(!(chp)->argv[i].__callfn__()) \
        { \
            return NEON_ARGS_FAIL(chp, "%s() expects argument %d as " NORMALIZE(__callfn__) ", %s given", (chp)->name, (i) + 1, nn_value_typename((chp)->argv[i])); \
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
        return NEON_ARGS_FAIL(chp, "invalid type %s() as argument %d in %s()", nn_value_typename((chp)->argv[index]), (index) + 1, (chp)->name); \
    }

#if defined(_WIN32)
    #include <fcntl.h>
    #include <io.h>
#endif


#if defined(__STRICT_ANSI__) && !defined(__cplusplus)
    #define va_copy(...)
#endif

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
    struct /**/AstToken;
    struct /**/AstLexer;
    struct /**/AstFuncCompiler;
    struct /**/AstClassCompiler;
    struct /**/AstParser;
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


    namespace Util
    {
        struct Utf8Iterator
        {
            public:
                NEON_ALWAYSINLINE static uint32_t convertToCodepoint(const char* character, uint8_t size)
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
                NEON_ALWAYSINLINE static uint8_t getCharSize(const char* character)
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
                NEON_ALWAYSINLINE Utf8Iterator(const char* ptr, uint32_t length)
                {
                    m_plainstr = ptr;
                    m_codepoint = 0;
                    m_currpos = 0;
                    m_nextpos = 0;
                    m_currcount = 0;
                    m_plainlen = length;
                }

                /* returns 1 if there is a character in the next position. If there is not, return 0. */
                NEON_ALWAYSINLINE uint8_t next()
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
                NEON_ALWAYSINLINE const char* getCharStr()
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
                chars = (char*)malloc(sizeof(char)*(count+1));
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
            buf = (char*)malloc(sizeof(char) * (len+1));
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
            //buf = (char*)neon::State::GC::allocate(state, sizeof(char), toldlen + 1);
            buf = (char*)malloc(sizeof(char)*(toldlen+1));
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


    union DoubleHashUnion
    {
        uint64_t bits;
        double num;
    };

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

            NEON_ALWAYSINLINE Code codeFromChar(char c)
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

            NEON_ALWAYSINLINE const char* color(Code tc)
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

            NEON_ALWAYSINLINE const char* color(char tc)
            {
                return color(codeFromChar(tc));
            }
    };
}


// preproto

uint32_t nn_util_hashstring(const char* key, int length);
bool nn_util_fsfileexists(neon::State *state, const char *filepath);
bool nn_util_fsfileisfile(neon::State *state, const char *filepath);
bool nn_util_fsfileisdirectory(neon::State *state, const char *filepath);
char *nn_util_fsgetbasename(neon::State *state, const char *path);

neon::FuncScript *nn_astparser_compilesource(neon::State *state, neon::Module *module, const char *source, neon::Blob *blob, bool fromimport, bool keeplast);

void nn_gcmem_markvalue(neon::State* state, neon::Value value);
void nn_import_closemodule(void *dlw);
void nn_printer_printvalue(neon::Printer *wr, neon::Value value, bool fixstring, bool invmethod);
uint32_t nn_value_hashvalue(neon::Value value);
bool nn_value_compare_actual(neon::State* state, neon::Value a, neon::Value b);
bool nn_value_compare(neon::State *state, neon::Value a, neon::Value b);
neon::Value nn_value_copyvalue(neon::State* state, neon::Value value);
void nn_gcmem_destroylinkedobjects(neon::State *state);

neon::ClassInstance *nn_exceptions_makeinstance(neon::State *state, neon::ClassObject *exklass, const char *srcfile, int srcline, neon::String *message);
int nn_fileobject_close(neon::File *file);
bool nn_fileobject_open(neon::File *file);
bool nn_vm_callvaluewithobject(neon::State *state, neon::Value callable, neon::Value thisval, int argcount);
neon::Status nn_vm_runvm(neon::State *state, int exitframe, neon::Value *rv);


static NEON_ALWAYSINLINE void nn_state_astdebug(neon::State* state, const char* funcname, const char* format, ...);
static NEON_ALWAYSINLINE void nn_state_apidebug(neon::State* state, const char* funcname, const char* format, ...);


/*
* quite a number of types ***MUST NOT EVER BE DERIVED FROM***, to ensure
* that they are correctly initialized.
* Just declare them as an instance variable instead.
*/

namespace neon
{
    class Memory
    {
        public:
            static void* osRealloc(void* userptr, void* ptr, size_t size)
            {
                (void)userptr;
                return realloc(ptr, size);
            }

            static void* osMalloc(void* userptr, size_t size)
            {
                (void)userptr;
                return malloc(size);
            }

            static void* osCalloc(void* userptr, size_t count, size_t size)
            {
                (void)userptr;
                return calloc(count, size);
            }

            static void osFree(void* userptr, void* ptr)
            {
                (void)userptr;
                free(ptr);
            }

    };


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
            static NEON_ALWAYSINLINE Value makeValue(ValType type)
            {
                Value v;
                v.m_valtype = type;
                return v;
            }

            static NEON_ALWAYSINLINE Value makeEmpty()
            {
                return makeValue(VALTYPE_EMPTY);
            }

            static NEON_ALWAYSINLINE Value makeNull()
            {
                Value v;
                v = makeValue(VALTYPE_NULL);
                return v;
            }

            static NEON_ALWAYSINLINE Value makeBool(bool b)
            {
                Value v;
                v = makeValue(VALTYPE_BOOL);
                v.m_valunion.boolean = b;
                return v;
            }

            static NEON_ALWAYSINLINE Value makeNumber(double d)
            {
                Value v;
                v = makeValue(VALTYPE_NUMBER);
                v.m_valunion.number = d;
                return v;
            }

            static NEON_ALWAYSINLINE Value makeInt(int i)
            {
                Value v;
                v = makeValue(VALTYPE_NUMBER);
                v.m_valunion.number = i;
                return v;
            }

            template<typename SubObjT>
            static NEON_ALWAYSINLINE Value fromObject(SubObjT* obj)
            {
                Value v;
                v = makeValue(VALTYPE_OBJECT);
                v.m_valunion.obj = (Object*)obj;
                return v;
            }

            static String* toString(State* state, Value value);

        public:
            ValType m_valtype;
            union
            {
                bool boolean;
                double number;
                Object* obj;
            } m_valunion;

        public:

            NEON_ALWAYSINLINE ValType type() const
            {
                return m_valtype;
            }

            NEON_ALWAYSINLINE bool isObject() const
            {
                return (m_valtype == VALTYPE_OBJECT);
            }

            NEON_ALWAYSINLINE bool isNull() const
            {
                return (m_valtype == VALTYPE_NULL);
            }

            NEON_ALWAYSINLINE bool isEmpty() const
            {
                return (m_valtype == VALTYPE_EMPTY);
            }

            NEON_ALWAYSINLINE bool isObjType(ObjType t) const;

            NEON_ALWAYSINLINE bool isFuncNative() const
            {
                return isObjType(OBJTYPE_FUNCNATIVE);
            }

            NEON_ALWAYSINLINE bool isFuncScript() const
            {
                return isObjType(OBJTYPE_FUNCSCRIPT);
            }

            NEON_ALWAYSINLINE bool isFuncClosure() const
            {
                return isObjType(OBJTYPE_FUNCCLOSURE);
            }

            NEON_ALWAYSINLINE bool isFuncBound() const
            {
                return isObjType(OBJTYPE_FUNCBOUND);
            }

            NEON_ALWAYSINLINE bool isClass() const
            {
                return isObjType(OBJTYPE_CLASS);
            }

            NEON_ALWAYSINLINE bool isCallable() const
            {
                return (
                    isClass() ||
                    isFuncScript() ||
                    isFuncClosure() ||
                    isFuncBound() ||
                    isFuncNative()
                );
            }

            NEON_ALWAYSINLINE bool isString() const
            {
                return isObjType(OBJTYPE_STRING);
            }

            NEON_ALWAYSINLINE bool isBool() const
            {
                return (type() == Value::VALTYPE_BOOL);
            }

            NEON_ALWAYSINLINE bool isNumber() const
            {
                return (type() == Value::VALTYPE_NUMBER);
            }

            NEON_ALWAYSINLINE bool isInstance() const
            {
                return isObjType(OBJTYPE_INSTANCE);
            }

            NEON_ALWAYSINLINE bool isArray() const
            {
                return isObjType(OBJTYPE_ARRAY);
            }

            NEON_ALWAYSINLINE bool isDict() const
            {
                return isObjType(OBJTYPE_DICT);
            }

            NEON_ALWAYSINLINE bool isFile() const
            {
                return isObjType(OBJTYPE_FILE);
            }

            NEON_ALWAYSINLINE bool isRange() const
            {
                return isObjType(OBJTYPE_RANGE);
            }

            NEON_ALWAYSINLINE bool isModule() const
            {
                return isObjType(OBJTYPE_MODULE);
            }

            NEON_ALWAYSINLINE Object* asObject()
            {
                return (m_valunion.obj);
            }

            NEON_ALWAYSINLINE const Object* asObject() const
            {
                return (m_valunion.obj);
            }

            NEON_ALWAYSINLINE double asNumber() const
            {
                return (m_valunion.number);
            }

            NEON_ALWAYSINLINE String* asString() const
            {
                return ((String*)asObject());
            }

            NEON_ALWAYSINLINE Array* asArray() const
            {
                return ((Array*)asObject());
            }

            NEON_ALWAYSINLINE ClassInstance* asInstance() const
            {
                return ((ClassInstance*)asObject());
            }

            NEON_ALWAYSINLINE Module* asModule() const
            {
                return ((Module*)asObject());
            }

            NEON_ALWAYSINLINE FuncScript* asFuncScript() const
            {
                return ((FuncScript*)asObject());
            }

            NEON_ALWAYSINLINE FuncClosure* asFuncClosure() const
            {
                return ((FuncClosure*)asObject());
            }

    };

    struct State final
    {
        public:
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
                static void collectGarbage(State *state);

                static void maybeCollect(State* state, int addsize, bool wasnew)
                {
                    state->m_gcstate.bytesallocated += addsize;
                    if(state->m_gcstate.nextgc > 0)
                    {
                        if(wasnew && state->m_gcstate.bytesallocated > state->m_gcstate.nextgc)
                        {
                            if(state->m_vmstate.currentframe && state->m_vmstate.currentframe->gcprotcount == 0)
                            {
                                GC::collectGarbage(state);
                            }
                        }
                    }
                }

                static void* reallocate(State* state, void* pointer, size_t oldsize, size_t newsize)
                {
                    void* result;
                    GC::maybeCollect(state, newsize - oldsize, newsize > oldsize);
                    result = realloc(pointer, newsize);
                    /*
                    // just in case reallocation fails... computers ain't infinite!
                    */
                    if(result == nullptr)
                    {
                        fprintf(stderr, "fatal error: failed to allocate %ld bytes\n", newsize);
                        abort();
                    }
                    return result;
                }

                static void release(State* state, void* pointer, size_t oldsize)
                {
                    GC::maybeCollect(state, -oldsize, false);
                    if(oldsize > 0)
                    {
                        memset(pointer, 0, oldsize);
                    }
                    free(pointer);
                    pointer = nullptr;
                }

                static void* allocate(State* state, size_t size, size_t amount)
                {
                    return GC::reallocate(state, nullptr, 0, size * amount);
                }

            };

        public:
            struct
            {
                /* for switching through the command line args... */
                bool enablewarnings = false;
                bool dumpbytecode = false;
                bool exitafterbytecode = false;
                bool shoulddumpstack = false;
                bool enablestrictmode = false;
                bool showfullstack = false;
                bool enableapidebug = false;
                bool enableastdebug = false;
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

            AstParser* m_activeparser;

            char* m_rootphysfile;

            Dictionary* m_envdict;

            String* m_constructorname;
            Module* m_toplevelmodule;
            ValArray* m_modimportpath;

            /* objects tracker */
            HashTable* m_loadedmodules;
            HashTable* m_cachedstrings;
            HashTable* m_definedglobals;

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
                newbuf = (Value*)realloc(oldbuf, allocsz );
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
                    newbuf = (State::CallFrame*)realloc(oldbuf, allocsz);
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

            void init(void* userptr);

        public:
            State()
            {
                init(nullptr);
            }

            ~State();

            bool checkMaybeResize()
            {
                if((m_vmstate.stackidx+1) >= m_vmstate.stackcapacity)
                {
                    if(!resizeStack(m_vmstate.stackidx + 1))
                    {
                        return nn_exceptions_throwException(this, "failed to resize stack due to overflow");
                    }
                    return true;
                }
                if(m_vmstate.framecount >= m_vmstate.framecapacity)
                {
                    if(!resizeFrames(m_vmstate.framecapacity + 1))
                    {
                        return nn_exceptions_throwException(this, "failed to resize frames due to overflow");
                    }
                    return true;
                }
                return false;
            }

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
                if(m_conf.enablewarnings)
                {
                    fprintf(stderr, "WARNING: ");
                    fprintf(stderr, fmt, args...);
                    fprintf(stderr, "\n");
                }
            }

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

            bool exceptionPropagate();

            template<typename... ArgsT>
            bool raiseClass(ClassObject* exklass, const char* srcfile, int srcline, const char* format, ArgsT&&... args);

            NEON_ALWAYSINLINE void stackPush(Value value)
            {
                checkMaybeResize();
                m_vmstate.stackvalues[m_vmstate.stackidx] = value;
                m_vmstate.stackidx++;
            }

            NEON_ALWAYSINLINE Value stackPop()
            {
                Value v;
                m_vmstate.stackidx--;
                v = m_vmstate.stackvalues[m_vmstate.stackidx];
                return v;
            }

            NEON_ALWAYSINLINE Value stackPop(int n)
            {
                Value v;
                m_vmstate.stackidx -= n;
                v = m_vmstate.stackvalues[m_vmstate.stackidx];
                return v;
            }

            NEON_ALWAYSINLINE Value stackPeek(int distance)
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
                Object* buf;
                Object* plain;
                (void)size;
                (void)buf;
                (void)plain;
                size = sizeof(SubObjT);
                buf = (Object*)State::GC::allocate(state, size+sizeof(Object), 1);
                plain = new(buf) SubObjT;
                plain->m_objtype = type;
                plain->m_mark = !state->m_currentmarkvalue;
                plain->m_isstale = false;
                plain->m_pvm = state;
                plain->m_nextobj = state->m_vmstate.linkedobjects;
                state->m_vmstate.linkedobjects = plain;
                #if defined(DEBUG_GC) && DEBUG_GC
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
                    State::GC::release(state, ptr, sizeof(SubObjT));
                }
            }

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

    struct ValArray final
    {
        public:
            State* m_pvm;
            /* type size of the stored value (via sizeof) */
            int m_typsize;
            /* how many entries are currently stored? */
            int m_count;
            /* how many entries can be stored before growing? */
            int m_capacity;
            Value* m_values;

        private:
            void makeSize(State* state, size_t size)
            {
                m_pvm = state;
                m_typsize = size;
                m_capacity = 0;
                m_count = 0;
                m_values = nullptr;
            }

            bool destroy()
            {
                if(m_values != nullptr)
                {
                    nn_gcmem_freearray(m_pvm, m_typsize, m_values, m_capacity);
                    m_values = nullptr;
                    m_count = 0;
                    m_capacity = 0;
                    return true;
                }
                return false;
            }

        public:
            ValArray(State* state, size_t size)
            {
                makeSize(state, size);
            }

            ValArray(State* state)
            {
                makeSize(state, sizeof(Value));
            }

            ~ValArray()
            {
                destroy();
            }

            void gcMark()
            {
                int i;
                for(i = 0; i < m_count; i++)
                {
                    nn_gcmem_markvalue(m_pvm, m_values[i]);
                }
            }

            void push(Value value)
            {
                int oldcapacity;
                if(m_capacity < m_count + 1)
                {
                    oldcapacity = m_capacity;
                    m_capacity = GROW_CAPACITY(oldcapacity);
                    m_values = (Value*)nn_gcmem_growarray(m_pvm, m_typsize, m_values, oldcapacity, m_capacity);
                }
                m_values[m_count] = value;
                m_count++;
            }

            void insert(Value value, int index)
            {
                int i;
                int oldcap;
                if(m_capacity <= index)
                {
                    m_capacity = GROW_CAPACITY(index);
                    m_values = (Value*)nn_gcmem_growarray(m_pvm, m_typsize, m_values, m_count, m_capacity);
                }
                else if(m_capacity < m_count + 2)
                {
                    oldcap = m_capacity;
                    m_capacity = GROW_CAPACITY(oldcap);
                    m_values = (Value*)nn_gcmem_growarray(m_pvm, m_typsize, m_values, oldcap, m_capacity);
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
                        m_values[i] = Value::makeNull();
                        m_count++;
                    }
                }
                m_values[index] = value;
                m_count++;
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
            Type type;
            Value value;
            bool havegetset;
            GetSetter getset;

        public:
            void init(Value val, Type t)
            {
                this->type = t;
                this->value = val;
                this->havegetset = false;
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
                    this->getset.setter = setter;
                    this->getset.getter = getter;
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
            bool m_active;
            int m_count;
            int m_capacity;
            State* m_pvm;
            Entry* m_entries;

        public:

            HashTable(State* state)
            {
                m_pvm = state;
                m_active = true;
                m_count = 0;
                m_capacity = 0;
                m_entries = nullptr;
            }

            ~HashTable()
            {
                State* state;
                state = m_pvm;
                nn_gcmem_freearray(state, sizeof(Entry), m_entries, m_capacity);
            }

            Entry* findEntryByValue(Entry* availents, int availcap, Value key)
            {
                uint32_t hash;
                uint32_t index;
                State* state;
                Entry* entry;
                Entry* tombstone;
                state = m_pvm;
                hash = nn_value_hashvalue(key);
                #if defined(DEBUG_TABLE) && DEBUG_TABLE
                fprintf(stderr, "looking for key ");
                nn_printer_printvalue(state->m_debugprinter, key, true, false);
                fprintf(stderr, " with hash %u in table...\n", hash);
                #endif
                index = hash & (availcap - 1);
                tombstone = nullptr;
                while(true)
                {
                    entry = &availents[index];
                    if(entry->key.isEmpty())
                    {
                        if(entry->value.value.isNull())
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
                    else if(nn_value_compare(state, key, entry->key))
                    {
                        return entry;
                    }
                    index = (index + 1) & (availcap - 1);
                }
                return nullptr;
            }

            Entry* findEntryByStr(Entry* availents, int availcap, Value valkey, const char* kstr, size_t klen, uint32_t hash);

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
                #if defined(DEBUG_TABLE) && DEBUG_TABLE
                fprintf(stderr, "getting entry with hash %u...\n", nn_value_hashvalue(key));
                #endif
                entry = findEntryByValue(m_entries, m_capacity, key);
                if(entry->key.isEmpty() || entry->key.isNull())
                {
                    return nullptr;
                }
                #if defined(DEBUG_TABLE) && DEBUG_TABLE
                fprintf(stderr, "found entry for hash %u == ", nn_value_hashvalue(entry->key));
                nn_printer_printvalue(state->m_debugprinter, entry->value.value, true, false);
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
                #if defined(DEBUG_TABLE) && DEBUG_TABLE
                fprintf(stderr, "getting entry with hash %u...\n", nn_value_hashvalue(key));
                #endif
                entry = findEntryByStr(m_entries, m_capacity, valkey, kstr, klen, hash);
                if(entry->key.isEmpty() || entry->key.isNull())
                {
                    return nullptr;
                }
                #if defined(DEBUG_TABLE) && DEBUG_TABLE
                fprintf(stderr, "found entry for hash %u == ", nn_value_hashvalue(entry->key));
                nn_printer_printvalue(state->m_debugprinter, entry->value.value, true, false);
                fprintf(stderr, "\n");
                #endif
                return &entry->value;
            }

            Property* getFieldByObjStr(String* str);

            Property* getFieldByCStr(const char* kstr)
            {
                size_t klen;
                uint32_t hash;
                klen = strlen(kstr);
                hash = nn_util_hashstring(kstr, klen);
                return getFieldByStr(Value::makeEmpty(), kstr, klen, hash);
            }

            Property* getField(Value key);

            bool get(Value key, Value* value)
            {
                Property* field;
                field = getField(key);
                if(field != nullptr)
                {
                    *value = field->value;
                    return true;
                }
                return false;
            }

            void adjustCapacity(int needcap)
            {
                int i;
                State* state;
                Entry* dest;
                Entry* entry;
                Entry* nents;
                state = m_pvm;
                nents = (Entry*)State::GC::allocate(state, sizeof(Entry), needcap);
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
                nn_gcmem_freearray(state, sizeof(Entry), m_entries, m_capacity);
                m_entries = nents;
                m_capacity = needcap;
            }

            bool setType(Value key, Value value, Property::Type ftyp, bool keyisstring)
            {
                bool isnew;
                int needcap;
                Entry* entry;
                (void)keyisstring;
                if(m_count + 1 > m_capacity * NEON_CFG_MAXTABLELOAD)
                {
                    needcap = GROW_CAPACITY(m_capacity);
                    adjustCapacity(needcap);
                }
                entry = findEntryByValue(m_entries, m_capacity, key);
                isnew = entry->key.isEmpty();
                if(isnew && entry->value.value.isNull())
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
                int i;
                Entry* entry;
                for(i = 0; i < from->m_capacity; i++)
                {
                    entry = &from->m_entries[i];
                    if(!entry->key.isEmpty())
                    {
                        to->setType(entry->key, entry->value.value, entry->value.type, false);
                    }
                }
            }

            static void copy(HashTable* from, HashTable* to)
            {
                int i;
                State* state;
                Entry* entry;
                state = from->m_pvm;
                for(i = 0; i < (int)from->m_capacity; i++)
                {
                    entry = &from->m_entries[i];
                    if(!entry->key.isEmpty())
                    {
                        to->setType(entry->key, nn_value_copyvalue(state, entry->value.value), entry->value.type, false);
                    }
                }
            }

            String* findString(const char* chars, size_t length, uint32_t hash);

            Value findKey(Value value)
            {
                int i;
                Entry* entry;
                for(i = 0; i < (int)m_capacity; i++)
                {
                    entry = &m_entries[i];
                    if(!entry->key.isNull() && !entry->key.isEmpty())
                    {
                        if(nn_value_compare(m_pvm, entry->value.value, value))
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
                int i;
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
                        nn_gcmem_markvalue(state, entry->key);
                        nn_gcmem_markvalue(state, entry->value.value);
                    }
                }
            }

            static void removeMarked(State* state, HashTable* table)
            {
                int i;
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
            int m_count;
            int m_capacity;
            Instruction* m_instrucs;
            ValArray* m_constants;
            ValArray* m_argdefvals;

        public:
            Blob(State* state)
            {
                m_pvm = state;
                m_count = 0;
                m_capacity = 0;
                m_instrucs = nullptr;
                m_constants = new ValArray(state);
                m_argdefvals = new ValArray(state);
            }

            ~Blob()
            {
                if(m_instrucs != nullptr)
                {
                    nn_gcmem_freearray(m_pvm, sizeof(Instruction), m_instrucs, m_capacity);
                }
                delete m_constants;
                delete m_argdefvals;
            }

            void push(Instruction ins)
            {
                int oldcapacity;
                if(m_capacity < m_count + 1)
                {
                    oldcapacity = m_capacity;
                    m_capacity = GROW_CAPACITY(oldcapacity);
                    m_instrucs = (Instruction*)nn_gcmem_growarray(m_pvm, sizeof(Instruction), m_instrucs, oldcapacity, m_capacity);
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

    NEON_ALWAYSINLINE bool Value::isObjType(Value::ObjType t) const
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

            static String* makeFromStrbuf(State* state, StringBuffer* sbuf, uint32_t hash)
            {
                String* rs;
                rs = Object::make<String>(state, Value::OBJTYPE_STRING);
                rs->m_sbuf = sbuf;
                rs->m_hash = hash;
                selfCachePut(state, rs);
                return rs;
            }

            static String* take(State* state, char* chars, int length)
            {
                uint32_t hash;
                String* rs;
                hash = nn_util_hashstring(chars, length);
                rs = selfCacheFind(state, chars, length, hash);
                if(rs == nullptr)
                {
                    rs = makeFromChars(state, chars, length, hash, true);
                }
                nn_gcmem_freearray(state, sizeof(char), chars, (size_t)length + 1);
                return rs;
            }

            static String* copy(State* state, const char* chars, int length)
            {
                uint32_t hash;
                String* rs;
                hash = nn_util_hashstring(chars, length);
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
                str->m_sbuf->append(buf, len);
                return str;
            }

        private:
            static String* makeFromChars(State* state, const char* estr, size_t elen, uint32_t hash, bool istaking)
            {
                StringBuffer* sbuf;
                (void)istaking;
                sbuf = new StringBuffer(elen);
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
            StringBuffer* m_sbuf;

        public:
            //String() = delete;
            //~String() = delete;

            inline bool destroyThisObject()
            {
                if(m_sbuf != nullptr)
                {
                    delete m_sbuf;
                    m_sbuf = nullptr;
                    return true;
                }
                return false;
            }


            inline size_t length() const
            {
                return m_sbuf->length;
            }

            inline const char* data() const
            {
                return m_sbuf->data;
            }

            inline char* mutableData()
            {
                return m_sbuf->data;
            }

            inline const char* cstr() const
            {
                return data();
            }

            String* substr(size_t start, size_t end, bool likejs) const
            {
                size_t asz;
                size_t len;
                size_t tmp;
                size_t maxlen;
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
                raw = (char*)State::GC::allocate(m_pvm, sizeof(char), asz);
                memset(raw, 0, asz);
                memcpy(raw, data() + start, len);
                return String::take(m_pvm, raw, len);
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
            static Array* make(State* state, size_t cnt, Value filler)
            {
                size_t i;
                Array* rt;
                rt = Object::make<Array>(state, Value::OBJTYPE_ARRAY);
                rt->m_varray = new ValArray(state);
                if(cnt > 0)
                {
                    for(i=0; i<cnt; i++)
                    {
                        rt->m_varray->push(filler);
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
                delete m_varray;
                return true;
            }

            void clear()
            {
            }

            void push(Value value)
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
                rt->m_keynames = new ValArray(state);
                rt->m_valtable = new HashTable(state);
                return rt;
            }

        public:
            ValArray* m_keynames;
            HashTable* m_valtable;

        public:
            //Dictionary() = delete;
            //~Dictionary() = delete;

            bool destroyThisObject()
            {
                delete m_keynames;
                delete m_valtable;
                return true;
            }

            bool setEntry(Value key, Value value)
            {
                Value tempvalue;
                if(!m_valtable->get(key, &tempvalue))
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
    };

    struct VarSwitch final: public Object
    {
        public:
            static VarSwitch* make(State* state)
            {
                VarSwitch* rt;
                rt = Object::make<VarSwitch>(state, Value::OBJTYPE_SWITCH);
                rt->m_jumppositions = new HashTable(state);
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
                delete m_jumppositions;
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
                rt->pointer = pointer;
                rt->name = neon::Util::strCopy(state, name);
                rt->ondestroyfn = nullptr;
                return rt;
            }

        public:
            void* pointer;
            char* name;
            CBOnFreeFN ondestroyfn;

        public:
            //Userdata() = delete;
            //~Userdata() = delete;

            bool destroyThisObject()
            {
                if(this->ondestroyfn)
                {
                    ondestroyfn(this->pointer);
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
            int arity = 0;
            FuncCommon::Type type = FUNCTYPE_UNSPECIFIED;
    };

    struct FuncScript final: public FuncCommon
    {
        public:
            static FuncScript* make(State* state, Module* module, FuncCommon::Type type)
            {
                FuncScript* rt;
                rt = Object::make<FuncScript>(state, Value::OBJTYPE_FUNCSCRIPT);
                rt->arity = 0;
                rt->upvalcount = 0;
                rt->isvariadic = false;
                rt->name = nullptr;
                rt->type = type;
                rt->module = module;
                rt->blob = new Blob(state);
                return rt;
            }

        public:
            int upvalcount;
            bool isvariadic;
            Blob* blob;
            String* name;
            Module* module;

        public:
            bool destroyThisObject()
            {
                delete this->blob;
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
                upvals = (ScopeUpvalue**)State::GC::allocate(state, sizeof(ScopeUpvalue*), function->upvalcount);
                for(i = 0; i < function->upvalcount; i++)
                {
                    upvals[i] = nullptr;
                }
                rt = Object::make<FuncClosure>(state, Value::OBJTYPE_FUNCCLOSURE);
                rt->scriptfunc = function;
                rt->upvalues = upvals;
                rt->upvalcount = function->upvalcount;
                return rt;
            }

        public:
            int upvalcount;
            FuncScript* scriptfunc;
            ScopeUpvalue** upvalues;

        public:
            bool destroyThisObject()
            {
                nn_gcmem_freearray(m_pvm, sizeof(ScopeUpvalue*), this->upvalues, this->upvalcount);
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
            using CallbackFN = Value (*)(State*, Arguments*);

        public:
            static FuncNative* make(State* state, CallbackFN function, const char* name, void* uptr)
            {
                FuncNative* rt;
                rt = Object::make<FuncNative>(state, Value::OBJTYPE_FUNCNATIVE);
                rt->natfunc = function;
                rt->name = name;
                rt->userptr = uptr;
                rt->type = FuncCommon::FUNCTYPE_FUNCTION;
                return rt;
            }

            static FuncNative* make(State* state, CallbackFN function, const char* name)
            {
                return make(state, function, name, nullptr);
            }

        public:
            const char* name;
            CallbackFN natfunc;
            void* userptr;

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
                    arity = closure->scriptfunc->arity;
                }
                else if(callable.isFuncScript())
                {
                    arity = callable.asFuncScript()->arity;
                }
                else if(callable.isFuncNative())
                {
                    //arity = nn_value_asfuncnative(callable);
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
                int i;
                int argc;
                size_t pidx;
                Status status;
                pidx = m_pvm->m_vmstate.stackidx;
                /* set the closure before the args */
                m_pvm->stackPush(callable);
                argc = 0;
                if(args && (argc = args->m_varray->m_count))
                {
                    for(i = 0; i < args->m_varray->m_count; i++)
                    {
                        m_pvm->stackPush(args->m_varray->m_values[i]);
                    }
                }
                if(!nn_vm_callvaluewithobject(m_pvm, callable, thisval, argc))
                {
                    fprintf(stderr, "nestcall: nn_vm_callvalue() failed\n");
                    abort();
                }
                status = nn_vm_runvm(m_pvm, m_pvm->m_vmstate.framecount - 1, nullptr);
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
                rt->m_instprops = new HashTable(state);
                rt->m_staticprops = new HashTable(state);
                rt->m_methods = new HashTable(state);
                rt->m_staticmethods = new HashTable(state);
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
                delete m_methods;
                delete m_staticmethods;
                delete m_instprops;
                delete m_staticprops;
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

            void defCallableField(const char* cstrname, FuncNative::CallbackFN function)
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

            void defNativeConstructor(FuncNative::CallbackFN function)
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

            void defNativeMethod(const char* name, FuncNative::CallbackFN function)
            {
                FuncNative* ofn;
                ofn = FuncNative::make(m_pvm, function, name);
                defMethod(name, Value::fromObject(ofn));
            }

            void defStaticNativeMethod(const char* name, FuncNative::CallbackFN function)
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
                return m_staticprops->getFieldByObjStr(name);
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
                rt->m_properties = new HashTable(state);
                if(rt->m_fromclass->m_instprops->m_count > 0)
                {
                    HashTable::copy(rt->m_fromclass->m_instprops, rt->m_properties);
                }
                /* gc fix */
                state->stackPop();
                return rt;
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
                delete this->m_properties;
                this->m_properties = nullptr;
                m_active = false;
                return true;
            }

            void defProperty(const char *cstrname, Value val)
            {
                this->m_properties->setCStr(cstrname, val);
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
                const char* name;
                bool isstatic;
                FuncNative::CallbackFN function;
            };

            struct FieldInfo
            {
                const char* name;
                bool isstatic;
                ClassFieldFN fieldvalfn;
            };

            struct ClassInfo
            {
                const char* name;
                FieldInfo* fields;
                FuncInfo* functions;
            };

        public:
            const char* name;
            FieldInfo* fields;
            FuncInfo* functions;
            ClassInfo* classes;
            ModLoaderFN preloader;
            ModLoaderFN unloader;
    };

    struct Module final: public Object
    {
        public:
            static Module* make(State* state, const char* name, const char* file, bool imported)
            {
                Module* rt;
                rt = Object::make<Module>(state, Value::OBJTYPE_MODULE);
                rt->deftable = new HashTable(state);
                rt->name = String::copy(state, name);
                rt->physicalpath = String::copy(state, file);
                rt->unloader = nullptr;
                rt->preloader = nullptr;
                rt->handle = nullptr;
                rt->imported = imported;
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
                StringBuffer* pathbuf;
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
                        pathbuf = new StringBuffer(pitem->length() + mlen + 5);
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
                    cstrpath = pathbuf->data; 
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
                                free(path1);
                                free(path2);
                                fprintf(stderr, "resolvepath: refusing to import itself\n");
                                return nullptr;
                            }
                            free(path2);
                            delete pathbuf;
                            pathbuf = nullptr;
                            retme = Util::strCopy(state, path1);
                            free(path1);
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
                    delete pathbuf;
                }
                return nullptr;
            }

            static void addNative(State* state, Module* module, const char* as)
            {
                Value name;
                if(as != nullptr)
                {
                    module->name = String::copy(state, as);
                }
                name = Value::fromObject(String::copyObjString(state, module->name));
                state->stackPush(name);
                state->stackPush(Value::fromObject(module));
                state->m_loadedmodules->set(name, Value::fromObject(module));
                state->stackPop(2);
            }

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
                field = state->m_loadedmodules->getFieldByObjStr(modulename);
                if(field != nullptr)
                {
                    return field->value.asModule();
                }
                physpath = Module::resolvePath(state, modulename->data(), intomodule->physicalpath->data(), nullptr, false);
                if(physpath == nullptr)
                {
                    nn_exceptions_throwException(state, "module not found: '%s'\n", modulename->data());
                    return nullptr;
                }
                fprintf(stderr, "loading module from '%s'\n", physpath);
                source = Util::fileReadFile(state, physpath, &fsz);
                if(source == nullptr)
                {
                    nn_exceptions_throwException(state, "could not read import file %s", physpath);
                    return nullptr;
                }
                blob = new Blob(state);
                module = Module::make(state, modulename->data(), physpath, true);
                free(physpath);
                function = nn_astparser_compilesource(state, module, source, blob, true, false);
                free(source);
                closure = FuncClosure::make(state, function);
                callable = Value::fromObject(closure);
                args = Array::make(state);
                NestCall nc(state);
                argc = nc.prepare(callable, Value::makeNull(), args);
                if(!nc.call(callable, Value::makeNull(), args, &retv))
                {
                    delete blob;
                    nn_exceptions_throwException(state, "failed to call compiled import closure");
                    return nullptr;
                }
                delete blob;
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
                RegModule* module;
                Module* themodule;
                RegModule::ClassInfo klassreg;
                String* classname;
                FuncNative* native;
                ClassObject* klass;
                HashTable* tabdest;
                module = init_fn(state);
                if(module != nullptr)
                {
                    themodule = state->gcProtect(Module::make(state, (char*)module->name, source, false));
                    themodule->preloader = (void*)module->preloader;
                    themodule->unloader = (void*)module->unloader;
                    if(module->fields != nullptr)
                    {
                        for(j = 0; module->fields[j].name != nullptr; j++)
                        {
                            field = module->fields[j];
                            fieldname = Value::fromObject(state->gcProtect(String::copy(state, field.name)));
                            v = field.fieldvalfn(state);
                            state->stackPush(v);
                            themodule->deftable->set(fieldname, v);
                            state->stackPop();
                        }
                    }
                    if(module->functions != nullptr)
                    {
                        for(j = 0; module->functions[j].name != nullptr; j++)
                        {
                            func = module->functions[j];
                            funcname = Value::fromObject(state->gcProtect(String::copy(state, func.name)));
                            funcrealvalue = Value::fromObject(state->gcProtect(FuncNative::make(state, func.function, func.name)));
                            state->stackPush(funcrealvalue);
                            themodule->deftable->set(funcname, funcrealvalue);
                            state->stackPop();
                        }
                    }
                    if(module->classes != nullptr)
                    {
                        for(j = 0; module->classes[j].name != nullptr; j++)
                        {
                            klassreg = module->classes[j];
                            classname = state->gcProtect(String::copy(state, klassreg.name));
                            klass = state->gcProtect(ClassObject::make(state, classname));
                            if(klassreg.functions != nullptr)
                            {
                                for(k = 0; klassreg.functions[k].name != nullptr; k++)
                                {
                                    func = klassreg.functions[k];
                                    funcname = Value::fromObject(state->gcProtect(String::copy(state, func.name)));
                                    native = state->gcProtect(FuncNative::make(state, func.function, func.name));
                                    if(func.isstatic)
                                    {
                                        native->type = FuncCommon::FUNCTYPE_STATIC;
                                    }
                                    else if(strlen(func.name) > 0 && func.name[0] == '_')
                                    {
                                        native->type = FuncCommon::FUNCTYPE_PRIVATE;
                                    }
                                    klass->m_methods->set(funcname, Value::fromObject(native));
                                }
                            }
                            if(klassreg.fields != nullptr)
                            {
                                for(k = 0; klassreg.fields[k].name != nullptr; k++)
                                {
                                    field = klassreg.fields[k];
                                    fieldname = Value::fromObject(state->gcProtect(String::copy(state, field.name)));
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
                            themodule->deftable->set(Value::fromObject(classname), Value::fromObject(klass));
                        }
                    }
                    if(dlw != nullptr)
                    {
                        themodule->handle = dlw;
                    }
                    Module::addNative(state, themodule, themodule->name->data());
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
            bool imported;
            HashTable* deftable;
            String* name;
            String* physicalpath;
            void* preloader;
            void* unloader;
            void* handle;

        public:
            //Module() = delete;
            //~Module() = delete;

            bool destroyThisObject()
            {
                delete this->deftable;
                /*
                free(this->name);
                free(this->physicalpath);
                */
                if(this->unloader != nullptr && this->imported)
                {
                    ((RegModule::ModLoaderFN)this->unloader)(m_pvm);
                }
                if(this->handle != nullptr)
                {
                    nn_import_closemodule(this->handle);
                }
                return true;
            }

            void setFileField()
            {
                return;
                this->deftable->setCStr("__file__", Value::fromObject(String::copyObjString(m_pvm, this->physicalpath)));
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
            State* m_pvm = nullptr;
            StringBuffer* m_strbuf = nullptr;
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
                m_strbuf = nullptr;
                m_filehandle = nullptr;
                m_pmode = mode;
            }

            bool destroy()
            {
                State* state;
                (void)state;
                if(m_pmode == PMODE_UNDEFINED)
                {
                    return false;
                }
                /*fprintf(stderr, "Printer::dtor: m_pmode=%d\n", m_pmode);*/
                state = m_pvm;
                if(m_pmode == PMODE_STRING)
                {
                    if(!m_stringtaken)
                    {
                        delete m_strbuf;
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


        public:
            Printer(State* state, PrintMode mode)
            {
                m_pvm = state;
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
                m_strbuf = new StringBuffer(0);
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
                hash = nn_util_hashstring(m_strbuf->data, m_strbuf->length);
                os = String::makeFromStrbuf(state, m_strbuf, hash);
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
                    m_strbuf->append(estr, elen);
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

            bool vputFormatToString(const char* fmt, va_list va)
            {
                m_strbuf->appendFormatv(m_strbuf->length, fmt, va);
                return true;
            }

            bool vputFormat(const char* fmt, va_list va)
            {
                if(m_pmode == PMODE_STRING)
                {
                    return vputFormatToString(fmt, va);
                }
                else if(m_pmode == PMODE_FILE)
                {
                    vfprintf(m_filehandle, fmt, va);
                    if(m_shouldflush)
                    {
                        fflush(m_filehandle);
                    }
                }
                return true;
            }

            bool putformat(const char* fmt, ...) NEON_ATTR_PRINTFLIKE(2, 3)
            {
                bool b;
                va_list va;
                va_start(va, fmt);
                b = vputFormat(fmt, va);
                va_end(va);
                return b;
            }
    };

    struct FormatInfo
    {
        public:
            template<typename... VargT>
            static bool formatArgs(Printer* pr, const char* fmt, VargT&&... args)
            {
                size_t argc = sizeof...(args);
                Value fargs[] = {(args) ..., Value::makeEmpty()};
                FormatInfo inf(pr->m_pvm, pr, fmt, strlen(fmt));
                return inf.format(argc, 0, fargs);
            }

        public:
            /* length of the format string */
            size_t fmtlen = 0;
            /* the actual format string */
            const char* fmtstr = nullptr;
            /* destination writer */
            Printer* writer = nullptr;
            State* m_pvm = nullptr;

        private:
            void init(State* state, Printer* pr, const char* fstr, size_t flen)
            {
                m_pvm = state;
                this->fmtstr = fstr;
                this->fmtlen = flen;
                this->writer = pr;
            }

        public:
            FormatInfo()
            {
            }

            FormatInfo(State* state, Printer* pr, const char* fstr, size_t flen)
            {
                init(state, pr, fstr, flen);
            }

            ~FormatInfo()
            {
            }

            bool format(int argc, int argbegin, Value* argv)
            {
                int ch;
                int ival;
                int nextch;
                bool failed;
                size_t i;
                size_t argpos;
                Value cval;
                i = 0;
                argpos = argbegin;
                failed = false;
                while(i < this->fmtlen)
                {
                    ch = this->fmtstr[i];
                    nextch = -1;
                    if((i + 1) < this->fmtlen)
                    {
                        nextch = this->fmtstr[i+1];
                    }
                    i++;
                    if(ch == '%')
                    {
                        if(nextch == '%')
                        {
                            this->writer->putChar('%');
                        }
                        else
                        {
                            i++;
                            if((int)argpos > argc)
                            {
                                failed = true;
                                cval = Value::makeEmpty();
                            }
                            else
                            {
                                cval = argv[argpos];
                            }
                            argpos++;
                            switch(nextch)
                            {
                                case 'q':
                                case 'p':
                                    {
                                        nn_printer_printvalue(this->writer, cval, true, true);
                                    }
                                    break;
                                case 'c':
                                    {
                                        ival = (int)cval.asNumber();
                                        this->writer->putformat("%c", ival);
                                    }
                                    break;
                                /* TODO: implement actual field formatting */
                                case 's':
                                case 'd':
                                case 'i':
                                case 'g':
                                    {
                                        nn_printer_printvalue(this->writer, cval, false, true);
                                    }
                                    break;
                                default:
                                    {
                                        nn_exceptions_throwException(m_pvm, "unknown/invalid format flag '%%c'", nextch);
                                    }
                                    break;
                            }
                        }
                    }
                    else
                    {
                        this->writer->putChar(ch);
                    }
                }
                return failed;
            }
    };

    static const char* g_strthis = "this";
    static const char* g_strsuper = "super";

    struct AstToken
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
                TOK_GREATER_EQ,
                TOK_RIGHTSHIFT,
                TOK_RIGHTSHIFTASSIGN,
                TOK_MODULO,
                TOK_PERCENT_EQ,
                TOK_AMP,
                TOK_AMP_EQ,
                TOK_BAR,
                TOK_BAR_EQ,
                TOK_TILDE,
                TOK_TILDE_EQ,
                TOK_XOR,
                TOK_XOR_EQ,
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
            bool isglobal;
            Type type;
            const char* start;
            int length;
            int line;
    };

    struct AstLexer
    {
        public:
            State* m_pvm;
            const char* m_plainsource;
            const char* m_sourceptr;
            int m_currentline;
            int m_tplstringcount;
            int m_tplstringbuffer[NEON_CFG_ASTMAXSTRTPLDEPTH];

        public:
            AstLexer(State* state, const char* source)
            {
                NEON_ASTDEBUG(state, "");
                m_pvm = state;
                m_sourceptr = source;
                m_plainsource = source;
                m_currentline = 1;
                m_tplstringcount = -1;
            }

            ~AstLexer()
            {
                NEON_ASTDEBUG(m_pvm, "");
            }

            bool isAtEnd()
            {
                return *m_sourceptr == '\0';
            }

            AstToken makeToken(AstToken::Type type)
            {
                AstToken t;
                t.isglobal = false;
                t.type = type;
                t.start = m_plainsource;
                t.length = (int)(m_sourceptr - m_plainsource);
                t.line = m_currentline;
                return t;
            }

            AstToken errorToken(const char* fmt, ...)
            {
                int length;
                char* buf;
                va_list va;
                AstToken t;
                va_start(va, fmt);
                buf = (char*)State::GC::allocate(m_pvm, sizeof(char), 1024);
                /* TODO: used to be vasprintf. need to check how much to actually allocate! */
                length = vsprintf(buf, fmt, va);
                va_end(va);
                t.type = AstToken::TOK_ERROR;
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

    };

    struct AstFuncCompiler
    {
        public:
            struct CompiledUpvalue
            {
                bool islocal;
                uint16_t index;
            };

            struct CompiledLocal
            {
                bool iscaptured;
                int depth;
                AstToken name;
            };

        public:
            int localcount;
            int scopedepth;
            int handlercount;
            bool fromimport;
            AstFuncCompiler* enclosing;
            /* current function */
            FuncScript* targetfunc;
            FuncCommon::Type type;
            CompiledLocal locals[NEON_CFG_ASTMAXLOCALS];
            CompiledUpvalue upvalues[NEON_CFG_ASTMAXUPVALS];
    };

    struct AstClassCompiler
    {
        bool hassuperclass;
        AstClassCompiler* enclosing;
        AstToken name;
    };

    struct AstParser
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

                    using PrefixFN = bool (*)(AstParser*, bool);
                    using InfixFN = bool (*)(AstParser*, AstToken, bool);

                public:
                    PrefixFN prefix;
                    InfixFN infix;
                    Precedence precedence;
            };


        public:
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
            State* m_pvm;
            AstLexer* m_lexer;
            AstToken m_currtoken;
            AstToken m_prevtoken;
            AstClassCompiler* m_currclasscompiler;
            AstFuncCompiler* m_currfunccompiler;
            Module* m_currmodule;

        public:
            AstParser(State* state, AstLexer* lexer, Module* module, bool keeplast)
            {
                NEON_ASTDEBUG(state, "");
                m_pvm = state;
                state->m_activeparser = this;
                m_currfunccompiler = nullptr;
                m_lexer = lexer;
                m_haderror = false;
                m_panicmode = false;
                m_blockcount = 0;
                m_replcanecho = false;
                m_isreturning = false;
                m_istrying = false;
                m_compcontext = AstParser::COMPCONTEXT_NONE;
                m_innermostloopstart = -1;
                m_innermostloopscopedepth = 0;
                m_currclasscompiler = nullptr;
                m_currmodule = module;
                m_keeplastvalue = keeplast;
                m_lastwasstatement = false;
                m_infunction = false;
                m_currentphysfile = m_currmodule->physicalpath->data();
            }

            ~AstParser()
            {
            }
    };

    struct Arguments
    {
        public:
            State* m_pvm;
            const char* name;
            Value thisval;
            int argc;
            Value* argv;
            void* userptr;

        public:
            inline Arguments(State* state, const char* n, Value tv, Value* vargv, int vargc, void* uptr)
            {
                m_pvm = state;
                this->name = n;
                this->thisval = tv;
                this->argc = vargc;
                this->argv = vargv;
                this->userptr = uptr;
            }

            template<typename... ArgsT>
            Value fail(const char* srcfile, int srcline, const char* fmt, ArgsT&&... args)
            {
                m_pvm->stackPop(this->argc);
                m_pvm->raiseClass(m_pvm->m_exceptions.argumenterror, srcfile, srcline, fmt, args...);
                return Value::makeBool(false);
            }
    };

}

#include "prot.inc"

namespace neon
{

}

neon::String* neon::Value::toString(neon::State* state, neon::Value value)
{
    neon::String* s;
    neon::Printer pd(state);
    nn_printer_printvalue(&pd, value, false, true);
    s = pd.takeString();
    return s;
}

neon::HashTable::Entry* neon::HashTable::findEntryByStr(neon::HashTable::Entry* availents, int availcap, neon::Value valkey, const char* kstr, size_t klen, uint32_t hash)
{
    bool havevalhash;
    uint32_t index;
    uint32_t valhash;
    neon::String* entoskey;
    neon::HashTable::Entry* entry;
    neon::HashTable::Entry* tombstone;
    neon::State* state;
    state = m_pvm;
    (void)valhash;
    (void)havevalhash;
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "looking for key ");
    nn_printer_printvalue(state->m_debugprinter, key, true, false);
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
            if(entry->value.value.isNull())
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
            entoskey = entry->key.asString();
            if(entoskey->length() == klen)
            {
                if(memcmp(kstr, entoskey->data(), klen) == 0)
                {
                    return entry;
                }
            }
        }
        else
        {
            if(!valkey.isEmpty())
            {
                if(nn_value_compare(state, valkey, entry->key))
                {
                    return entry;
                }
            }
        }
        index = (index + 1) & (availcap - 1);
    }
    return nullptr;
}

neon::Property* neon::HashTable::getFieldByObjStr(neon::String* str)
{
    return getFieldByStr(neon::Value::makeEmpty(), str->data(), str->length(), str->m_hash);
}

neon::Property* neon::HashTable::getField(neon::Value key)
{
    neon::String* oskey;
    if(key.isString())
    {
        oskey = key.asString();
        return getFieldByStr(key, oskey->data(), oskey->length(), oskey->m_hash);
    }
    return getFieldByValue(key);
}

bool neon::HashTable::setCStrType(const char* cstrkey, neon::Value value, neon::Property::Type ftype)
{
    neon::String* os;
    neon::State* state;
    state = m_pvm;
    os = neon::String::copy(state, cstrkey);
    return setType(neon::Value::fromObject(os), value, ftype, true);
}

neon::String* neon::HashTable::findString(const char* chars, size_t length, uint32_t hash)
{
    size_t slen;
    uint32_t index;
    const char* sdata;
    neon::HashTable::Entry* entry;
    neon::String* string;
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
            //if(entry->value.isNull())
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

void neon::HashTable::printTo(neon::Printer* pd, const char* name)
{
    int i;
    neon::HashTable::Entry* entry;
    pd->putformat("<HashTable of %s : {\n", name);
    for(i = 0; i < m_capacity; i++)
    {
        entry = &m_entries[i];
        if(!entry->key.isEmpty())
        {
            nn_printer_printvalue(pd, entry->key, true, true);
            pd->putformat(": ");
            nn_printer_printvalue(pd, entry->value.value, true, true);
            if(i != m_capacity - 1)
            {
                pd->putformat(",\n");
            }
        }
    }
    pd->putformat("}>\n");
}

template<typename... ArgsT>
void neon::State::raiseFatal(const char* format, ArgsT&&... args)
{
    static auto fn_fprintf = fprintf;
    int i;
    int line;
    size_t instruction;
    neon::State::CallFrame* frame;
    neon::FuncScript* function;
    /* flush out anything on stdout first */
    fflush(stdout);
    frame = &m_vmstate.framevalues[m_vmstate.framecount - 1];
    function = frame->closure->scriptfunc;
    instruction = frame->inscode - function->blob->m_instrucs - 1;
    line = function->blob->m_instrucs[instruction].srcline;
    fprintf(stderr, "RuntimeError: ");
    fn_fprintf(stderr, format, args...);
    fprintf(stderr, " -> %s:%d ", function->module->physicalpath->data(), line);
    fputs("\n", stderr);
    if(m_vmstate.framecount > 1)
    {
        fprintf(stderr, "stacktrace:\n");
        for(i = m_vmstate.framecount - 1; i >= 0; i--)
        {
            frame = &m_vmstate.framevalues[i];
            function = frame->closure->scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->blob->m_instrucs - 1;
            fprintf(stderr, "    %s:%d -> ", function->module->physicalpath->data(), function->blob->m_instrucs[instruction].srcline);
            if(function->name == nullptr)
            {
                fprintf(stderr, "<script>");
            }
            else
            {
                fprintf(stderr, "%s()", function->name->data());
            }
            fprintf(stderr, "\n");
        }
    }
    nn_state_resetvmstate(this);
}

template<typename... ArgsT>
bool neon::State::raiseClass(neon::ClassObject* exklass, const char* srcfile, int srcline, const char* format, ArgsT&&... args)
{
    static auto fn_snprintf = snprintf;
    int length;
    int needed;
    char* message;
    neon::Value stacktrace;
    neon::ClassInstance* instance;
    /* TODO: used to be vasprintf. need to check how much to actually allocate! */
    needed = fn_snprintf(nullptr, 0, format, args...);
    needed += 1;
    message = (char*)malloc(needed+1);
    length = fn_snprintf(message, needed, format, args...);
    instance = nn_exceptions_makeinstance(this, exklass, srcfile, srcline, neon::String::take(this, message, length));
    stackPush(neon::Value::fromObject(instance));
    stacktrace = nn_exceptions_getstacktrace(this);
    stackPush(stacktrace);
    instance->defProperty("stacktrace", stacktrace);
    stackPop();
    return exceptionPropagate();
}

bool neon::State::exceptionHandleUncaught(neon::ClassInstance* exception)
{
    int i;
    int cnt;
    int srcline;
    const char* colred;
    const char* colreset;
    const char* colyellow;
    const char* srcfile;
    neon::Value stackitm;
    neon::Property* field;
    neon::String* emsg;
    neon::Array* oa;
    neon::Color nc;
    colred = nc.color('r');
    colreset = nc.color('0');
    colyellow = nc.color('y');
    /* at this point, the exception is unhandled; so, print it out. */
    fprintf(stderr, "%sunhandled %s%s", colred, exception->m_fromclass->m_classname->data(), colreset);
    srcfile = "none";
    srcline = 0;
    field = exception->m_properties->getFieldByCStr("srcline");
    if(field != nullptr)
    {
        srcline = field->value.asNumber();
    }
    field = exception->m_properties->getFieldByCStr("srcfile");
    if(field != nullptr)
    {
        srcfile = field->value.asString()->data();
    }
    fprintf(stderr, " [from native %s%s:%d%s]", colyellow, srcfile, srcline, colreset);
    
    field = exception->m_properties->getFieldByCStr("message");
    if(field != nullptr)
    {
        emsg = neon::Value::toString(this, field->value);
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
    field = exception->m_properties->getFieldByCStr("stacktrace");
    if(field != nullptr)
    {
        fprintf(stderr, "  stacktrace:\n");
        oa = field->value.asArray();
        cnt = oa->m_varray->m_count;
        i = cnt-1;
        if(cnt > 0)
        {
            while(true)
            {
                stackitm = oa->m_varray->m_values[i];
                m_debugprinter->putformat("  ");
                nn_printer_printvalue(m_debugprinter, stackitm, false, true);
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

bool neon::State::exceptionPropagate()
{
    int i;
    neon::FuncScript* function;
    neon::State::ExceptionFrame* handler;
    neon::ClassInstance* exception;
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
                m_vmstate.currentframe->inscode = &function->blob->m_instrucs[handler->address];
                return true;
            }
            else if(handler->finallyaddress != 0)
            {
                /* continue propagating once the 'finally' block completes */
                stackPush(neon::Value::makeBool(true));
                m_vmstate.currentframe->inscode = &function->blob->m_instrucs[handler->finallyaddress];
                return true;
            }
        }
        m_vmstate.framecount--;
    }
    return exceptionHandleUncaught(exception);
}

static NEON_ALWAYSINLINE neon::Value::ObjType nn_value_objtype(neon::Value v)
{
    return v.asObject()->m_objtype;
}

static NEON_ALWAYSINLINE bool nn_value_asbool(neon::Value v)
{
    return (v.m_valunion.boolean);
}


static NEON_ALWAYSINLINE neon::FuncNative* nn_value_asfuncnative(neon::Value v)
{
    return ((neon::FuncNative*)v.asObject());
}


static NEON_ALWAYSINLINE neon::ClassObject* nn_value_asclass(neon::Value v)
{
    return ((neon::ClassObject*)v.asObject());
}

static NEON_ALWAYSINLINE neon::FuncBound* nn_value_asfuncbound(neon::Value v)
{
    return ((neon::FuncBound*)v.asObject());
}

static NEON_ALWAYSINLINE neon::VarSwitch* nn_value_asswitch(neon::Value v)
{
    return ((neon::VarSwitch*)v.asObject());
}

static NEON_ALWAYSINLINE neon::Userdata* nn_value_asuserdata(neon::Value v)
{
    return ((neon::Userdata*)v.asObject());
}

static NEON_ALWAYSINLINE neon::Dictionary* nn_value_asdict(neon::Value v)
{
    return ((neon::Dictionary*)v.asObject());
}

static NEON_ALWAYSINLINE neon::File* nn_value_asfile(neon::Value v)
{
    return ((neon::File*)v.asObject());
}

static NEON_ALWAYSINLINE neon::Range* nn_value_asrange(neon::Value v)
{
    return ((neon::Range*)v.asObject());
}


static NEON_ALWAYSINLINE void nn_state_apidebugv(neon::State* state, const char* funcname, const char* format, va_list va)
{
    (void)state;
    fprintf(stderr, "API CALL: to '%s': ", funcname);
    vfprintf(stderr, format, va);
    fprintf(stderr, "\n");
}

static NEON_ALWAYSINLINE void nn_state_apidebug(neon::State* state, const char* funcname, const char* format, ...)
{
    va_list va;
    va_start(va, format);
    nn_state_apidebugv(state, funcname, format, va);
    va_end(va);
}

static NEON_ALWAYSINLINE void nn_state_astdebugv(neon::State* state, const char* funcname, const char* format, va_list va)
{
    (void)state;
    fprintf(stderr, "AST CALL: to '%s': ", funcname);
    vfprintf(stderr, format, va);
    fprintf(stderr, "\n");
}

static NEON_ALWAYSINLINE void nn_state_astdebug(neon::State* state, const char* funcname, const char* format, ...)
{
    va_list va;
    va_start(va, format);
    nn_state_astdebugv(state, funcname, format, va);
    va_end(va);
}

void nn_gcmem_markobject(neon::State* state, neon::Object* object)
{
    if(object == nullptr)
    {
        return;
    }
    if(object->m_mark == state->m_currentmarkvalue)
    {
        return;
    }
    #if defined(DEBUG_GC) && DEBUG_GC
    state->m_debugprinter->putformat("GC: marking object at <%p> ", (void*)object);
    nn_printer_printvalue(state->m_debugprinter, neon::Value::fromObject(object), false);
    state->m_debugprinter->putformat("\n");
    #endif
    object->m_mark = state->m_currentmarkvalue;
    if(state->m_gcstate.graycapacity < state->m_gcstate.graycount + 1)
    {
        state->m_gcstate.graycapacity = GROW_CAPACITY(state->m_gcstate.graycapacity);
        state->m_gcstate.graystack = (neon::Object**)realloc(state->m_gcstate.graystack, sizeof(neon::Object*) * state->m_gcstate.graycapacity);
        if(state->m_gcstate.graystack == nullptr)
        {
            fflush(stdout);
            fprintf(stderr, "GC encountered an error");
            abort();
        }
    }
    state->m_gcstate.graystack[state->m_gcstate.graycount++] = object;
}

void nn_gcmem_markvalue(neon::State* state, neon::Value value)
{
    if(value.isObject())
    {
        nn_gcmem_markobject(state, value.asObject());
    }
}

void nn_gcmem_blackenobject(neon::State* state, neon::Object* object)
{
    #if defined(DEBUG_GC) && DEBUG_GC
    state->m_debugprinter->putformat("GC: blacken object at <%p> ", (void*)object);
    nn_printer_printvalue(state->m_debugprinter, neon::Value::fromObject(object), false);
    state->m_debugprinter->putformat("\n");
    #endif
    switch(object->m_objtype)
    {
        case neon::Value::OBJTYPE_MODULE:
            {
                neon::Module* module;
                module = static_cast<neon::Module*>(object);
                neon::HashTable::mark(state, module->deftable);
            }
            break;
        case neon::Value::OBJTYPE_SWITCH:
            {
                neon::VarSwitch* sw;
                sw = static_cast<neon::VarSwitch*>(object);
                neon::HashTable::mark(state, sw->m_jumppositions);
            }
            break;
        case neon::Value::OBJTYPE_FILE:
            {
                neon::File* file;
                file = static_cast<neon::File*>(object);
                nn_file_mark(state, file);
            }
            break;
        case neon::Value::OBJTYPE_DICT:
            {
                neon::Dictionary* dict;
                dict = static_cast<neon::Dictionary*>(object);
                dict->m_keynames->gcMark();
                neon::HashTable::mark(state, dict->m_valtable);
            }
            break;
        case neon::Value::OBJTYPE_ARRAY:
            {
                neon::Array* list;
                list = static_cast<neon::Array*>(object);
                list->m_varray->gcMark();
            }
            break;
        case neon::Value::OBJTYPE_FUNCBOUND:
            {
                neon::FuncBound* bound;
                bound = static_cast<neon::FuncBound*>(object);
                nn_gcmem_markvalue(state, bound->receiver);
                nn_gcmem_markobject(state, static_cast<neon::Object*>(bound->method));
            }
            break;
        case neon::Value::OBJTYPE_CLASS:
            {
                neon::ClassObject* klass;
                klass = static_cast<neon::ClassObject*>(object);
                nn_gcmem_markobject(state, static_cast<neon::Object*>(klass->m_classname));
                neon::HashTable::mark(state, klass->m_methods);
                neon::HashTable::mark(state, klass->m_staticmethods);
                neon::HashTable::mark(state, klass->m_staticprops);
                nn_gcmem_markvalue(state, klass->m_constructor);
                if(klass->m_superclass != nullptr)
                {
                    nn_gcmem_markobject(state, static_cast<neon::Object*>(klass->m_superclass));
                }
            }
            break;
        case neon::Value::OBJTYPE_FUNCCLOSURE:
            {
                int i;
                neon::FuncClosure* closure;
                closure = static_cast<neon::FuncClosure*>(object);
                nn_gcmem_markobject(state, static_cast<neon::Object*>(closure->scriptfunc));
                for(i = 0; i < closure->upvalcount; i++)
                {
                    nn_gcmem_markobject(state, static_cast<neon::Object*>(closure->upvalues[i]));
                }
            }
            break;
        case neon::Value::OBJTYPE_FUNCSCRIPT:
            {
                neon::FuncScript* function;
                function = static_cast<neon::FuncScript*>(object);
                nn_gcmem_markobject(state, static_cast<neon::Object*>(function->name));
                nn_gcmem_markobject(state, static_cast<neon::Object*>(function->module));
                function->blob->m_constants->gcMark();
            }
            break;
        case neon::Value::OBJTYPE_INSTANCE:
            {
                neon::ClassInstance* instance;
                instance = static_cast<neon::ClassInstance*>(object);
                nn_instance_mark(state, instance);
            }
            break;
        case neon::Value::OBJTYPE_UPVALUE:
            {
                nn_gcmem_markvalue(state, (static_cast<neon::ScopeUpvalue*>(object))->m_closed);
            }
            break;
        case neon::Value::OBJTYPE_RANGE:
        case neon::Value::OBJTYPE_FUNCNATIVE:
        case neon::Value::OBJTYPE_USERDATA:
        case neon::Value::OBJTYPE_STRING:
            break;
    }
}

void nn_gcmem_destroyobject(neon::State* state, neon::Object* object)
{
    (void)state;
    #if defined(DEBUG_GC) && DEBUG_GC
    state->m_debugprinter->putformat("GC: freeing at <%p> of type %d\n", (void*)object, object->m_objtype);
    #endif
    if(object->m_isstale)
    {
        return;
    }
    switch(object->m_objtype)
    {
        case neon::Value::OBJTYPE_MODULE:
            {
                neon::Module* module;
                module = static_cast<neon::Module*>(object);
                neon::Object::release(state, module);
            }
            break;
        case neon::Value::OBJTYPE_FILE:
            {
                neon::File* file;
                file = static_cast<neon::File*>(object);
                neon::Object::release(state, file);
            }
            break;
        case neon::Value::OBJTYPE_DICT:
            {
                neon::Dictionary* dict;
                dict = static_cast<neon::Dictionary*>(object);
                neon::Object::release(state, dict);
            }
            break;
        case neon::Value::OBJTYPE_ARRAY:
            {
                neon::Array* list;
                list = static_cast<neon::Array*>(object);
                if(list != nullptr)
                {
                    neon::Object::release(state, list);
                }
            }
            break;
        case neon::Value::OBJTYPE_FUNCBOUND:
            {
                neon::FuncBound* bound;
                bound = static_cast<neon::FuncBound*>(object);
                /*
                // a closure may be bound to multiple instances
                // for this reason, we do not free closures when freeing bound methods
                */
                neon::Object::release(state, bound);
            }
            break;
        case neon::Value::OBJTYPE_CLASS:
            {
                neon::ClassObject* klass;
                klass = static_cast<neon::ClassObject*>(object);
                neon::Object::release(state, klass);
            }
            break;
        case neon::Value::OBJTYPE_FUNCCLOSURE:
            {
                neon::FuncClosure* closure;
                closure = static_cast<neon::FuncClosure*>(object);
                neon::Object::release(state, closure);
            }
            break;
        case neon::Value::OBJTYPE_FUNCSCRIPT:
            {
                neon::FuncScript* function;
                function = static_cast<neon::FuncScript*>(object);
                neon::Object::release(state, function);
            }
            break;
        case neon::Value::OBJTYPE_INSTANCE:
            {
                neon::ClassInstance* instance;
                instance = static_cast<neon::ClassInstance*>(object);
                neon::Object::release(state, instance);
            }
            break;
        case neon::Value::OBJTYPE_FUNCNATIVE:
            {
                neon::FuncNative* native;
                native = static_cast<neon::FuncNative*>(object);
                neon::Object::release(state, native);
            }
            break;
        case neon::Value::OBJTYPE_UPVALUE:
            {
                neon::ScopeUpvalue* upv;
                upv = static_cast<neon::ScopeUpvalue*>(object);
                neon::Object::release(state, upv);
            }
            break;
        case neon::Value::OBJTYPE_RANGE:
            {
                neon::Range* rng;
                rng = static_cast<neon::Range*>(object);
                neon::Object::release(state, rng);
            }
            break;
        case neon::Value::OBJTYPE_STRING:
            {
                neon::String* string;
                string = static_cast<neon::String*>(object);
                neon::Object::release(state, string);
            }
            break;
        case neon::Value::OBJTYPE_SWITCH:
            {
                neon::VarSwitch* sw;
                sw = static_cast<neon::VarSwitch*>(object);
                neon::Object::release(state, sw);
            }
            break;
        case neon::Value::OBJTYPE_USERDATA:
            {
                neon::Userdata* uptr;
                uptr = static_cast<neon::Userdata*>(object);
                neon::Object::release(state, uptr);
            }
            break;
        default:
            break;
    }
}

void nn_gcmem_markroots(neon::State* state)
{
    int i;
    int j;
    neon::Value* slot;
    neon::ScopeUpvalue* upvalue;
    neon::State::ExceptionFrame* handler;
    for(slot = state->m_vmstate.stackvalues; slot < &state->m_vmstate.stackvalues[state->m_vmstate.stackidx]; slot++)
    {
        nn_gcmem_markvalue(state, *slot);
    }
    for(i = 0; i < (int)state->m_vmstate.framecount; i++)
    {
        nn_gcmem_markobject(state, static_cast<neon::Object*>(state->m_vmstate.framevalues[i].closure));
        for(j = 0; j < (int)state->m_vmstate.framevalues[i].handlercount; j++)
        {
            handler = &state->m_vmstate.framevalues[i].handlers[j];
            nn_gcmem_markobject(state, static_cast<neon::Object*>(handler->klass));
        }
    }
    for(upvalue = state->m_vmstate.openupvalues; upvalue != nullptr; upvalue = upvalue->m_nextupval)
    {
        nn_gcmem_markobject(state, static_cast<neon::Object*>(upvalue));
    }
    neon::HashTable::mark(state, state->m_definedglobals);
    neon::HashTable::mark(state, state->m_loadedmodules);
    nn_gcmem_markobject(state, static_cast<neon::Object*>(state->m_exceptions.stdexception));
    nn_gcmem_markcompilerroots(state);
}

void nn_gcmem_tracerefs(neon::State* state)
{
    neon::Object* object;
    while(state->m_gcstate.graycount > 0)
    {
        object = state->m_gcstate.graystack[--state->m_gcstate.graycount];
        nn_gcmem_blackenobject(state, object);
    }
}

void nn_gcmem_sweep(neon::State* state)
{
    neon::Object* object;
    neon::Object* previous;
    neon::Object* unreached;
    previous = nullptr;
    object = state->m_vmstate.linkedobjects;
    while(object != nullptr)
    {
        if(object->m_mark == state->m_currentmarkvalue)
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
                state->m_vmstate.linkedobjects = object;
            }
            nn_gcmem_destroyobject(state, unreached);
        }
    }
}

void nn_gcmem_destroylinkedobjects(neon::State* state)
{
    neon::Object* next;
    neon::Object* object;
    object = state->m_vmstate.linkedobjects;
    if(object != nullptr)
    {
        while(object != nullptr)
        {
            next = object->m_nextobj;
            if(object != nullptr)
            {
                nn_gcmem_destroyobject(state, object);
            }
            object = next;
        }
    }
    free(state->m_gcstate.graystack);
    state->m_gcstate.graystack = nullptr;
}

void neon::State::GC::collectGarbage(neon::State* state)
{
    size_t before;
    (void)before;
    #if defined(DEBUG_GC) && DEBUG_GC
    state->m_debugprinter->putformat("GC: gc begins\n");
    before = state->m_gcstate.bytesallocated;
    #endif
    /*
    //  REMOVE THE NEXT LINE TO DISABLE NESTED collectGarbage() POSSIBILITY!
    */
    #if 0
    state->m_gcstate.nextgc = state->m_gcstate.bytesallocated;
    #endif
    nn_gcmem_markroots(state);
    nn_gcmem_tracerefs(state);
    neon::HashTable::removeMarked(state, state->m_cachedstrings);
    neon::HashTable::removeMarked(state, state->m_loadedmodules);
    nn_gcmem_sweep(state);
    state->m_gcstate.nextgc = state->m_gcstate.bytesallocated * NEON_CFG_GCHEAPGROWTHFACTOR;
    state->m_currentmarkvalue = !state->m_currentmarkvalue;
    #if defined(DEBUG_GC) && DEBUG_GC
    state->m_debugprinter->putformat("GC: gc ends\n");
    state->m_debugprinter->putformat("GC: collected %zu bytes (from %zu to %zu), next at %zu\n", before - state->m_gcstate.bytesallocated, before, state->m_gcstate.bytesallocated, state->m_gcstate.nextgc);
    #endif
}


void nn_dbg_disasmblob(neon::Printer* pd, neon::Blob* blob, const char* name)
{
    int offset;
    pd->putformat("== compiled '%s' [[\n", name);
    for(offset = 0; offset < blob->m_count;)
    {
        offset = nn_dbg_printinstructionat(pd, blob, offset);
    }
    pd->putformat("]]\n");
}

void nn_dbg_printinstrname(neon::Printer* pd, const char* name)
{
    neon::Color nc;
    pd->putformat("%s%-16s%s ", nc.color('r'), name, nc.color('0'));
}

int nn_dbg_printsimpleinstr(neon::Printer* pd, const char* name, int offset)
{
    nn_dbg_printinstrname(pd, name);
    pd->putformat("\n");
    return offset + 1;
}

int nn_dbg_printconstinstr(neon::Printer* pd, const char* name, neon::Blob* blob, int offset)
{
    uint16_t constant;
    constant = (blob->m_instrucs[offset + 1].code << 8) | blob->m_instrucs[offset + 2].code;
    nn_dbg_printinstrname(pd, name);
    pd->putformat("%8d ", constant);
    nn_printer_printvalue(pd, blob->m_constants->m_values[constant], true, false);
    pd->putformat("\n");
    return offset + 3;
}

int nn_dbg_printpropertyinstr(neon::Printer* pd, const char* name, neon::Blob* blob, int offset)
{
    const char* proptn;
    uint16_t constant;
    constant = (blob->m_instrucs[offset + 1].code << 8) | blob->m_instrucs[offset + 2].code;
    nn_dbg_printinstrname(pd, name);
    pd->putformat("%8d ", constant);
    nn_printer_printvalue(pd, blob->m_constants->m_values[constant], true, false);
    proptn = "";
    if(blob->m_instrucs[offset + 3].code == 1)
    {
        proptn = "static";
    }
    pd->putformat(" (%s)", proptn);
    pd->putformat("\n");
    return offset + 4;
}

int nn_dbg_printshortinstr(neon::Printer* pd, const char* name, neon::Blob* blob, int offset)
{
    uint16_t slot;
    slot = (blob->m_instrucs[offset + 1].code << 8) | blob->m_instrucs[offset + 2].code;
    nn_dbg_printinstrname(pd, name);
    pd->putformat("%8d\n", slot);
    return offset + 3;
}

int nn_dbg_printbyteinstr(neon::Printer* pd, const char* name, neon::Blob* blob, int offset)
{
    uint8_t slot;
    slot = blob->m_instrucs[offset + 1].code;
    nn_dbg_printinstrname(pd, name);
    pd->putformat("%8d\n", slot);
    return offset + 2;
}

int nn_dbg_printjumpinstr(neon::Printer* pd, const char* name, int sign, neon::Blob* blob, int offset)
{
    uint16_t jump;
    jump = (uint16_t)(blob->m_instrucs[offset + 1].code << 8);
    jump |= blob->m_instrucs[offset + 2].code;
    nn_dbg_printinstrname(pd, name);
    pd->putformat("%8d -> %d\n", offset, offset + 3 + sign * jump);
    return offset + 3;
}

int nn_dbg_printtryinstr(neon::Printer* pd, const char* name, neon::Blob* blob, int offset)
{
    uint16_t finally;
    uint16_t type;
    uint16_t address;
    type = (uint16_t)(blob->m_instrucs[offset + 1].code << 8);
    type |= blob->m_instrucs[offset + 2].code;
    address = (uint16_t)(blob->m_instrucs[offset + 3].code << 8);
    address |= blob->m_instrucs[offset + 4].code;
    finally = (uint16_t)(blob->m_instrucs[offset + 5].code << 8);
    finally |= blob->m_instrucs[offset + 6].code;
    nn_dbg_printinstrname(pd, name);
    pd->putformat("%8d -> %d, %d\n", type, address, finally);
    return offset + 7;
}

int nn_dbg_printinvokeinstr(neon::Printer* pd, const char* name, neon::Blob* blob, int offset)
{
    uint16_t constant;
    uint8_t argcount;
    constant = (uint16_t)(blob->m_instrucs[offset + 1].code << 8);
    constant |= blob->m_instrucs[offset + 2].code;
    argcount = blob->m_instrucs[offset + 3].code;
    nn_dbg_printinstrname(pd, name);
    pd->putformat("(%d args) %8d ", argcount, constant);
    nn_printer_printvalue(pd, blob->m_constants->m_values[constant], true, false);
    pd->putformat("\n");
    return offset + 4;
}

const char* nn_dbg_op2str(uint8_t instruc)
{
    switch(instruc)
    {
        case neon::Instruction::OP_GLOBALDEFINE: return "neon::Instruction::OP_GLOBALDEFINE";
        case neon::Instruction::OP_GLOBALGET: return "neon::Instruction::OP_GLOBALGET";
        case neon::Instruction::OP_GLOBALSET: return "neon::Instruction::OP_GLOBALSET";
        case neon::Instruction::OP_LOCALGET: return "neon::Instruction::OP_LOCALGET";
        case neon::Instruction::OP_LOCALSET: return "neon::Instruction::OP_LOCALSET";
        case neon::Instruction::OP_FUNCARGSET: return "neon::Instruction::OP_FUNCARGSET";
        case neon::Instruction::OP_FUNCARGGET: return "neon::Instruction::OP_FUNCARGGET";
        case neon::Instruction::OP_UPVALUEGET: return "neon::Instruction::OP_UPVALUEGET";
        case neon::Instruction::OP_UPVALUESET: return "neon::Instruction::OP_UPVALUESET";
        case neon::Instruction::OP_UPVALUECLOSE: return "neon::Instruction::OP_UPVALUECLOSE";
        case neon::Instruction::OP_PROPERTYGET: return "neon::Instruction::OP_PROPERTYGET";
        case neon::Instruction::OP_PROPERTYGETSELF: return "neon::Instruction::OP_PROPERTYGETSELF";
        case neon::Instruction::OP_PROPERTYSET: return "neon::Instruction::OP_PROPERTYSET";
        case neon::Instruction::OP_JUMPIFFALSE: return "neon::Instruction::OP_JUMPIFFALSE";
        case neon::Instruction::OP_JUMPNOW: return "neon::Instruction::OP_JUMPNOW";
        case neon::Instruction::OP_LOOP: return "neon::Instruction::OP_LOOP";
        case neon::Instruction::OP_EQUAL: return "neon::Instruction::OP_EQUAL";
        case neon::Instruction::OP_PRIMGREATER: return "neon::Instruction::OP_PRIMGREATER";
        case neon::Instruction::OP_PRIMLESSTHAN: return "neon::Instruction::OP_PRIMLESSTHAN";
        case neon::Instruction::OP_PUSHEMPTY: return "neon::Instruction::OP_PUSHEMPTY";
        case neon::Instruction::OP_PUSHNULL: return "neon::Instruction::OP_PUSHNULL";
        case neon::Instruction::OP_PUSHTRUE: return "neon::Instruction::OP_PUSHTRUE";
        case neon::Instruction::OP_PUSHFALSE: return "neon::Instruction::OP_PUSHFALSE";
        case neon::Instruction::OP_PRIMADD: return "neon::Instruction::OP_PRIMADD";
        case neon::Instruction::OP_PRIMSUBTRACT: return "neon::Instruction::OP_PRIMSUBTRACT";
        case neon::Instruction::OP_PRIMMULTIPLY: return "neon::Instruction::OP_PRIMMULTIPLY";
        case neon::Instruction::OP_PRIMDIVIDE: return "neon::Instruction::OP_PRIMDIVIDE";
        case neon::Instruction::OP_PRIMFLOORDIVIDE: return "neon::Instruction::OP_PRIMFLOORDIVIDE";
        case neon::Instruction::OP_PRIMMODULO: return "neon::Instruction::OP_PRIMMODULO";
        case neon::Instruction::OP_PRIMPOW: return "neon::Instruction::OP_PRIMPOW";
        case neon::Instruction::OP_PRIMNEGATE: return "neon::Instruction::OP_PRIMNEGATE";
        case neon::Instruction::OP_PRIMNOT: return "neon::Instruction::OP_PRIMNOT";
        case neon::Instruction::OP_PRIMBITNOT: return "neon::Instruction::OP_PRIMBITNOT";
        case neon::Instruction::OP_PRIMAND: return "neon::Instruction::OP_PRIMAND";
        case neon::Instruction::OP_PRIMOR: return "neon::Instruction::OP_PRIMOR";
        case neon::Instruction::OP_PRIMBITXOR: return "neon::Instruction::OP_PRIMBITXOR";
        case neon::Instruction::OP_PRIMSHIFTLEFT: return "neon::Instruction::OP_PRIMSHIFTLEFT";
        case neon::Instruction::OP_PRIMSHIFTRIGHT: return "neon::Instruction::OP_PRIMSHIFTRIGHT";
        case neon::Instruction::OP_PUSHONE: return "neon::Instruction::OP_PUSHONE";
        case neon::Instruction::OP_PUSHCONSTANT: return "neon::Instruction::OP_PUSHCONSTANT";
        case neon::Instruction::OP_ECHO: return "neon::Instruction::OP_ECHO";
        case neon::Instruction::OP_POPONE: return "neon::Instruction::OP_POPONE";
        case neon::Instruction::OP_DUPONE: return "neon::Instruction::OP_DUPONE";
        case neon::Instruction::OP_POPN: return "neon::Instruction::OP_POPN";
        case neon::Instruction::OP_ASSERT: return "neon::Instruction::OP_ASSERT";
        case neon::Instruction::OP_EXTHROW: return "neon::Instruction::OP_EXTHROW";
        case neon::Instruction::OP_MAKECLOSURE: return "neon::Instruction::OP_MAKECLOSURE";
        case neon::Instruction::OP_CALLFUNCTION: return "neon::Instruction::OP_CALLFUNCTION";
        case neon::Instruction::OP_CALLMETHOD: return "neon::Instruction::OP_CALLMETHOD";
        case neon::Instruction::OP_CLASSINVOKETHIS: return "neon::Instruction::OP_CLASSINVOKETHIS";
        case neon::Instruction::OP_RETURN: return "neon::Instruction::OP_RETURN";
        case neon::Instruction::OP_MAKECLASS: return "neon::Instruction::OP_MAKECLASS";
        case neon::Instruction::OP_MAKEMETHOD: return "neon::Instruction::OP_MAKEMETHOD";
        case neon::Instruction::OP_CLASSPROPERTYDEFINE: return "neon::Instruction::OP_CLASSPROPERTYDEFINE";
        case neon::Instruction::OP_CLASSINHERIT: return "neon::Instruction::OP_CLASSINHERIT";
        case neon::Instruction::OP_CLASSGETSUPER: return "neon::Instruction::OP_CLASSGETSUPER";
        case neon::Instruction::OP_CLASSINVOKESUPER: return "neon::Instruction::OP_CLASSINVOKESUPER";
        case neon::Instruction::OP_CLASSINVOKESUPERSELF: return "neon::Instruction::OP_CLASSINVOKESUPERSELF";
        case neon::Instruction::OP_MAKERANGE: return "neon::Instruction::OP_MAKERANGE";
        case neon::Instruction::OP_MAKEARRAY: return "neon::Instruction::OP_MAKEARRAY";
        case neon::Instruction::OP_MAKEDICT: return "neon::Instruction::OP_MAKEDICT";
        case neon::Instruction::OP_INDEXGET: return "neon::Instruction::OP_INDEXGET";
        case neon::Instruction::OP_INDEXGETRANGED: return "neon::Instruction::OP_INDEXGETRANGED";
        case neon::Instruction::OP_INDEXSET: return "neon::Instruction::OP_INDEXSET";
        case neon::Instruction::OP_IMPORTIMPORT: return "neon::Instruction::OP_IMPORTIMPORT";
        case neon::Instruction::OP_EXTRY: return "neon::Instruction::OP_EXTRY";
        case neon::Instruction::OP_EXPOPTRY: return "neon::Instruction::OP_EXPOPTRY";
        case neon::Instruction::OP_EXPUBLISHTRY: return "neon::Instruction::OP_EXPUBLISHTRY";
        case neon::Instruction::OP_STRINGIFY: return "neon::Instruction::OP_STRINGIFY";
        case neon::Instruction::OP_SWITCH: return "neon::Instruction::OP_SWITCH";
        case neon::Instruction::OP_TYPEOF: return "neon::Instruction::OP_TYPEOF";
        case neon::Instruction::OP_BREAK_PL: return "neon::Instruction::OP_BREAK_PL";
        default:
            break;
    }
    return "<?unknown?>";
}

int nn_dbg_printclosureinstr(neon::Printer* pd, const char* name, neon::Blob* blob, int offset)
{
    int j;
    int islocal;
    uint16_t index;
    uint16_t constant;
    const char* locn;
    neon::FuncScript* function;
    offset++;
    constant = blob->m_instrucs[offset++].code << 8;
    constant |= blob->m_instrucs[offset++].code;
    pd->putformat("%-16s %8d ", name, constant);
    nn_printer_printvalue(pd, blob->m_constants->m_values[constant], true, false);
    pd->putformat("\n");
    function = blob->m_constants->m_values[constant].asFuncScript();
    for(j = 0; j < function->upvalcount; j++)
    {
        islocal = blob->m_instrucs[offset++].code;
        index = blob->m_instrucs[offset++].code << 8;
        index |= blob->m_instrucs[offset++].code;
        locn = "upvalue";
        if(islocal)
        {
            locn = "local";
        }
        pd->putformat("%04d      |                     %s %d\n", offset - 3, locn, (int)index);
    }
    return offset;
}

int nn_dbg_printinstructionat(neon::Printer* pd, neon::Blob* blob, int offset)
{
    uint8_t instruction;
    const char* opname;
    pd->putformat("%08d ", offset);
    if(offset > 0 && blob->m_instrucs[offset].srcline == blob->m_instrucs[offset - 1].srcline)
    {
        pd->putformat("       | ");
    }
    else
    {
        pd->putformat("%8d ", blob->m_instrucs[offset].srcline);
    }
    instruction = blob->m_instrucs[offset].code;
    opname = nn_dbg_op2str(instruction);
    switch(instruction)
    {
        case neon::Instruction::OP_JUMPIFFALSE:
            return nn_dbg_printjumpinstr(pd, opname, 1, blob, offset);
        case neon::Instruction::OP_JUMPNOW:
            return nn_dbg_printjumpinstr(pd, opname, 1, blob, offset);
        case neon::Instruction::OP_EXTRY:
            return nn_dbg_printtryinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_LOOP:
            return nn_dbg_printjumpinstr(pd, opname, -1, blob, offset);
        case neon::Instruction::OP_GLOBALDEFINE:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_GLOBALGET:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_GLOBALSET:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_LOCALGET:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_LOCALSET:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_FUNCARGGET:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_FUNCARGSET:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_PROPERTYGET:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_PROPERTYGETSELF:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_PROPERTYSET:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_UPVALUEGET:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_UPVALUESET:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_EXPOPTRY:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_EXPUBLISHTRY:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PUSHCONSTANT:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_EQUAL:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMGREATER:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMLESSTHAN:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PUSHEMPTY:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PUSHNULL:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PUSHTRUE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PUSHFALSE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMADD:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMSUBTRACT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMMULTIPLY:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMDIVIDE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMFLOORDIVIDE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMMODULO:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMPOW:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMNEGATE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMNOT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMBITNOT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMAND:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMOR:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMBITXOR:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMSHIFTLEFT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PRIMSHIFTRIGHT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_PUSHONE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_IMPORTIMPORT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_TYPEOF:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_ECHO:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_STRINGIFY:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_EXTHROW:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_POPONE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_UPVALUECLOSE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_DUPONE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_ASSERT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_POPN:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
            /* non-user objects... */
        case neon::Instruction::OP_SWITCH:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
            /* data container manipulators */
        case neon::Instruction::OP_MAKERANGE:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_MAKEARRAY:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_MAKEDICT:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_INDEXGET:
            return nn_dbg_printbyteinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_INDEXGETRANGED:
            return nn_dbg_printbyteinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_INDEXSET:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_MAKECLOSURE:
            return nn_dbg_printclosureinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_CALLFUNCTION:
            return nn_dbg_printbyteinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_CALLMETHOD:
            return nn_dbg_printinvokeinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_CLASSINVOKETHIS:
            return nn_dbg_printinvokeinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_RETURN:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_MAKECLASS:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_MAKEMETHOD:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_CLASSPROPERTYDEFINE:
            return nn_dbg_printpropertyinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_CLASSGETSUPER:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_CLASSINHERIT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case neon::Instruction::OP_CLASSINVOKESUPER:
            return nn_dbg_printinvokeinstr(pd, opname, blob, offset);
        case neon::Instruction::OP_CLASSINVOKESUPERSELF:
            return nn_dbg_printbyteinstr(pd, opname, blob, offset);
        default:
            {
                pd->putformat("unknown opcode %d\n", instruction);
            }
            break;
    }
    return offset + 1;
}


void nn_printer_printfunction(neon::Printer* pd, neon::FuncScript* func)
{
    if(func->name == nullptr)
    {
        pd->putformat("<script at %p>", (void*)func);
    }
    else
    {
        if(func->isvariadic)
        {
            pd->putformat("<function %s(%d...) at %p>", func->name->data(), func->arity, (void*)func);
        }
        else
        {
            pd->putformat("<function %s(%d) at %p>", func->name->data(), func->arity, (void*)func);
        }
    }
}

void nn_printer_printarray(neon::Printer* pd, neon::Array* list)
{
    size_t i;
    size_t vsz;
    bool isrecur;
    neon::Value val;
    neon::Array* subarr;
    vsz = list->m_varray->m_count;
    pd->putformat("[");
    for(i = 0; i < vsz; i++)
    {
        isrecur = false;
        val = list->m_varray->m_values[i];
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
            pd->putformat("<recursion>");
        }
        else
        {
            nn_printer_printvalue(pd, val, true, true);
        }
        if(i != vsz - 1)
        {
            pd->putformat(", ");
        }
        if(pd->m_shortenvalues && (i >= pd->m_maxvallength))
        {
            pd->putformat(" [%ld items]", vsz);
            break;
        }
    }
    pd->putformat("]");
}

void nn_printer_printdict(neon::Printer* pd, neon::Dictionary* dict)
{
    size_t i;
    size_t dsz;
    bool keyisrecur;
    bool valisrecur;
    neon::Value val;
    neon::Dictionary* subdict;
    neon::Property* field;
    dsz = dict->m_keynames->m_count;
    pd->putformat("{");
    for(i = 0; i < dsz; i++)
    {
        valisrecur = false;
        keyisrecur = false;
        val = dict->m_keynames->m_values[i];
        if(val.isDict())
        {
            subdict = nn_value_asdict(val);
            if(subdict == dict)
            {
                valisrecur = true;
            }
        }
        if(valisrecur)
        {
            pd->putformat("<recursion>");
        }
        else
        {
            nn_printer_printvalue(pd, val, true, true);
        }
        pd->putformat(": ");
        field = dict->m_valtable->getField(dict->m_keynames->m_values[i]);
        if(field != nullptr)
        {
            if(field->value.isDict())
            {
                subdict = nn_value_asdict(field->value);
                if(subdict == dict)
                {
                    keyisrecur = true;
                }
            }
            if(keyisrecur)
            {
                pd->putformat("<recursion>");
            }
            else
            {
                nn_printer_printvalue(pd, field->value, true, true);
            }
        }
        if(i != dsz - 1)
        {
            pd->putformat(", ");
        }
        if(pd->m_shortenvalues && (pd->m_maxvallength >= i))
        {
            pd->putformat(" [%ld items]", dsz);
            break;
        }
    }
    pd->putformat("}");
}

void nn_printer_printfile(neon::Printer* pd, neon::File* file)
{
    pd->putformat("<file at %s in mode %s>", file->m_filepath->data(), file->m_filemode->data());
}

void nn_printer_printinstance(neon::Printer* pd, neon::ClassInstance* instance, bool invmethod)
{
    (void)invmethod;
    #if 0
    int arity;
    neon::Value resv;
    neon::Value thisval;
    neon::Property* field;
    neon::State* state;
    neon::String* os;
    neon::Array* args;
    state = pd->m_pvm;
    if(invmethod)
    {
        field = instance->m_fromclass->m_methods->getFieldByCStr("toString");
        if(field != nullptr)
        {
            neon::NestCall nc(state);
            args = neon::Array::make(state);
            thisval = neon::Value::fromObject(instance);
            arity = nc.prepare(field->value, thisval, args);
            fprintf(stderr, "arity = %d\n", arity);
            state->stackPop();
            state->stackPush(thisval);
            if(nc.call(field->value, thisval, args, &resv))
            {
                neon::Printer subp(state, &subw);
                nn_printer_printvalue(&subp, resv, false, false);
                os = subp.takeString();
                pd->put(os->data(), os->length());
                //state->stackPop();
                return;
            }
        }
    }
    #endif
    pd->putformat("<instance of %s at %p>", instance->m_fromclass->m_classname->data(), (void*)instance);
}

void nn_printer_printobject(neon::Printer* pd, neon::Value value, bool fixstring, bool invmethod)
{
    neon::Object* obj;
    obj = value.asObject();
    switch(obj->m_objtype)
    {
        case neon::Value::OBJTYPE_SWITCH:
            {
                pd->put("<switch>");
            }
            break;
        case neon::Value::OBJTYPE_USERDATA:
            {
                pd->putformat("<userdata %s>", nn_value_asuserdata(value)->name);
            }
            break;
        case neon::Value::OBJTYPE_RANGE:
            {
                neon::Range* range;
                range = nn_value_asrange(value);
                pd->putformat("<range %d .. %d>", range->m_lower, range->m_upper);
            }
            break;
        case neon::Value::OBJTYPE_FILE:
            {
                nn_printer_printfile(pd, nn_value_asfile(value));
            }
            break;
        case neon::Value::OBJTYPE_DICT:
            {
                nn_printer_printdict(pd, nn_value_asdict(value));
            }
            break;
        case neon::Value::OBJTYPE_ARRAY:
            {
                nn_printer_printarray(pd, value.asArray());
            }
            break;
        case neon::Value::OBJTYPE_FUNCBOUND:
            {
                neon::FuncBound* bn;
                bn = nn_value_asfuncbound(value);
                nn_printer_printfunction(pd, bn->method->scriptfunc);
            }
            break;
        case neon::Value::OBJTYPE_MODULE:
            {
                neon::Module* mod;
                mod = value.asModule();
                pd->putformat("<module '%s' at '%s'>", mod->name->data(), mod->physicalpath->data());
            }
            break;
        case neon::Value::OBJTYPE_CLASS:
            {
                neon::ClassObject* klass;
                klass = nn_value_asclass(value);
                pd->putformat("<class %s at %p>", klass->m_classname->data(), (void*)klass);
            }
            break;
        case neon::Value::OBJTYPE_FUNCCLOSURE:
            {
                neon::FuncClosure* cls;
                cls = value.asFuncClosure();
                nn_printer_printfunction(pd, cls->scriptfunc);
            }
            break;
        case neon::Value::OBJTYPE_FUNCSCRIPT:
            {
                neon::FuncScript* fn;
                fn = value.asFuncScript();
                nn_printer_printfunction(pd, fn);
            }
            break;
        case neon::Value::OBJTYPE_INSTANCE:
            {
                /* @TODO: support the toString() override */
                neon::ClassInstance* instance;
                instance = value.asInstance();
                nn_printer_printinstance(pd, instance, invmethod);
            }
            break;
        case neon::Value::OBJTYPE_FUNCNATIVE:
            {
                neon::FuncNative* native;
                native = nn_value_asfuncnative(value);
                pd->putformat("<function %s(native) at %p>", native->name, (void*)native);
            }
            break;
        case neon::Value::OBJTYPE_UPVALUE:
            {
                pd->putformat("<upvalue>");
            }
            break;
        case neon::Value::OBJTYPE_STRING:
            {
                neon::String* string;
                string = value.asString();
                if(fixstring)
                {
                    pd->putQuotedString(string->data(), string->length(), true);
                }
                else
                {
                    pd->put(string->data(), string->length());
                }
            }
            break;
    }
}

void nn_printer_printvalue(neon::Printer* pd, neon::Value value, bool fixstring, bool invmethod)
{
    switch(value.type())
    {
        case neon::Value::VALTYPE_EMPTY:
            {
                pd->put("<empty>");
            }
            break;
        case neon::Value::VALTYPE_NULL:
            {
                pd->put("null");
            }
            break;
        case neon::Value::VALTYPE_BOOL:
            {
                pd->put(nn_value_asbool(value) ? "true" : "false");
            }
            break;
        case neon::Value::VALTYPE_NUMBER:
            {
                pd->putformat("%.16g", value.asNumber());
            }
            break;
        case neon::Value::VALTYPE_OBJECT:
            {
                nn_printer_printobject(pd, value, fixstring, invmethod);
            }
            break;
        default:
            break;
    }
}



const char* nn_value_objecttypename(neon::Object* object)
{
    switch(object->m_objtype)
    {
        case neon::Value::OBJTYPE_MODULE:
            return "module";
        case neon::Value::OBJTYPE_RANGE:
            return "range";
        case neon::Value::OBJTYPE_FILE:
            return "file";
        case neon::Value::OBJTYPE_DICT:
            return "dictionary";
        case neon::Value::OBJTYPE_ARRAY:
            return "array";
        case neon::Value::OBJTYPE_CLASS:
            return "class";
        case neon::Value::OBJTYPE_FUNCSCRIPT:
        case neon::Value::OBJTYPE_FUNCNATIVE:
        case neon::Value::OBJTYPE_FUNCCLOSURE:
        case neon::Value::OBJTYPE_FUNCBOUND:
            return "function";
        case neon::Value::OBJTYPE_INSTANCE:
            return (static_cast<neon::ClassInstance*>(object))->m_fromclass->m_classname->data();
        case neon::Value::OBJTYPE_STRING:
            return "string";
        case neon::Value::OBJTYPE_USERDATA:
            return "userdata";
        case neon::Value::OBJTYPE_SWITCH:
            return "switch";
        default:
            break;
    }
    return "unknown";
}

const char* nn_value_typename(neon::Value value)
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
        return nn_value_objecttypename(value.asObject());
    }
    return "unknown";
}

bool nn_value_compobject(neon::State* state, neon::Value a, neon::Value b)
{
    size_t i;
    neon::Value::ObjType ta;
    neon::Value::ObjType tb;
    neon::Object* oa;
    neon::Object* ob;
    neon::String* stra;
    neon::String* strb;
    neon::Array* arra;
    neon::Array* arrb;
    oa = a.asObject();
    ob = b.asObject();
    ta = oa->m_objtype;
    tb = ob->m_objtype;
    if(ta == tb)
    {
        if(ta == neon::Value::OBJTYPE_STRING)
        {
            stra = (neon::String*)oa;
            strb = (neon::String*)ob;
            if(stra->length() == strb->length())
            {
                if(memcmp(stra->data(), strb->data(), stra->length()) == 0)
                {
                    return true;
                }
                return false;
            }
        }
        if(ta == neon::Value::OBJTYPE_ARRAY)
        {
            arra = (neon::Array*)oa;
            arrb = (neon::Array*)ob;
            if(arra->m_varray->m_count == arrb->m_varray->m_count)
            {
                for(i=0; i<(size_t)arra->m_varray->m_count; i++)
                {
                    if(!nn_value_compare(state, arra->m_varray->m_values[i], arrb->m_varray->m_values[i]))
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

bool nn_value_compare_actual(neon::State* state, neon::Value a, neon::Value b)
{
    if(a.type() != b.type())
    {
        return false;
    }
    switch(a.type())
    {
        case neon::Value::VALTYPE_NULL:
        case neon::Value::VALTYPE_EMPTY:
            {
                return true;
            }
            break;
        case neon::Value::VALTYPE_BOOL:
            {
                return nn_value_asbool(a) == nn_value_asbool(b);
            }
            break;
        case neon::Value::VALTYPE_NUMBER:
            {
                return (a.asNumber() == b.asNumber());
            }
            break;
        case neon::Value::VALTYPE_OBJECT:
            {
                if(a.asObject() == b.asObject())
                {
                    return true;
                }
                return nn_value_compobject(state, a, b);
            }
            break;
        default:
            break;
    }
    return false;
}


bool nn_value_compare(neon::State* state, neon::Value a, neon::Value b)
{
    bool r;
    r = nn_value_compare_actual(state, a, b);
    return r;
}

uint32_t nn_util_hashbits(uint64_t hash)
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

uint32_t nn_util_hashdouble(double value)
{
    neon::DoubleHashUnion bits;
    bits.num = value;
    return nn_util_hashbits(bits.bits);
}

uint32_t nn_util_hashstring(const char* key, int length)
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

uint32_t nn_object_hashobject(neon::Object* object)
{
    switch(object->m_objtype)
    {
        case neon::Value::OBJTYPE_CLASS:
            {
                /* Classes just use their name. */
                return (static_cast<neon::ClassObject*>(object))->m_classname->m_hash;
            }
            break;
        case neon::Value::OBJTYPE_FUNCSCRIPT:
            {
                /*
                // Allow bare (non-closure) functions so that we can use a map to find
                // existing constants in a function's constant table. This is only used
                // internally. Since user code never sees a non-closure function, they
                // cannot use them as map keys.
                */
                neon::FuncScript* fn;
                fn = static_cast<neon::FuncScript*>(object);
                return nn_util_hashdouble(fn->arity) ^ nn_util_hashdouble(fn->blob->m_count);
            }
            break;
        case neon::Value::OBJTYPE_STRING:
            {
                return (static_cast<neon::String*>(object))->m_hash;
            }
            break;
        default:
            break;
    }
    return 0;
}

uint32_t nn_value_hashvalue(neon::Value value)
{
    switch(value.type())
    {
        case neon::Value::VALTYPE_BOOL:
            return nn_value_asbool(value) ? 3 : 5;
        case neon::Value::VALTYPE_NULL:
            return 7;
        case neon::Value::VALTYPE_NUMBER:
            return nn_util_hashdouble(value.asNumber());
        case neon::Value::VALTYPE_OBJECT:
            return nn_object_hashobject(value.asObject());
        default:
            /* neon::Value::VALTYPE_EMPTY */
            break;
    }
    return 0;
}


/**
 * returns the greater of the two values.
 * this function encapsulates the object hierarchy
 */
neon::Value nn_value_findgreater(neon::Value a, neon::Value b)
{
    neon::String* osa;
    neon::String* osb;    
    if(a.isNull())
    {
        return b;
    }
    else if(a.isBool())
    {
        if(b.isNull() || (b.isBool() && nn_value_asbool(b) == false))
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
            if(a.asFuncScript()->arity >= b.asFuncScript()->arity)
            {
                return a;
            }
            return b;
        }
        else if(a.isFuncClosure() && b.isFuncClosure())
        {
            if(a.asFuncClosure()->scriptfunc->arity >= b.asFuncClosure()->scriptfunc->arity)
            {
                return a;
            }
            return b;
        }
        else if(a.isRange() && b.isRange())
        {
            if(nn_value_asrange(a)->m_lower >= nn_value_asrange(b)->m_lower)
            {
                return a;
            }
            return b;
        }
        else if(a.isClass() && b.isClass())
        {
            if(nn_value_asclass(a)->m_methods->m_count >= nn_value_asclass(b)->m_methods->m_count)
            {
                return a;
            }
            return b;
        }
        else if(a.isArray() && b.isArray())
        {
            if(a.asArray()->m_varray->m_count >= b.asArray()->m_varray->m_count)
            {
                return a;
            }
            return b;
        }
        else if(a.isDict() && b.isDict())
        {
            if(nn_value_asdict(a)->m_keynames->m_count >= nn_value_asdict(b)->m_keynames->m_count)
            {
                return a;
            }
            return b;
        }
        else if(a.isFile() && b.isFile())
        {
            if(strcmp(nn_value_asfile(a)->m_filepath->data(), nn_value_asfile(b)->m_filepath->data()) >= 0)
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
    return a;
}

/**
 * sorts values in an array using the bubble-sort algorithm
 */
void nn_value_sortvalues(neon::State* state, neon::Value* values, int count)
{
    int i;
    int j;
    neon::Value temp;
    for(i = 0; i < count; i++)
    {
        for(j = 0; j < count; j++)
        {
            if(nn_value_compare(state, values[j], nn_value_findgreater(values[i], values[j])))
            {
                temp = values[i];
                values[i] = values[j];
                values[j] = temp;
                if(values[i].isArray())
                {
                    nn_value_sortvalues(state, values[i].asArray()->m_varray->m_values, values[i].asArray()->m_varray->m_count);
                }

                if(values[j].isArray())
                {
                    nn_value_sortvalues(state, values[j].asArray()->m_varray->m_values, values[j].asArray()->m_varray->m_count);
                }
            }
        }
    }
}

neon::Value nn_value_copyvalue(neon::State* state, neon::Value value)
{
    if(value.isObject())
    {
        switch(value.asObject()->m_objtype)
        {
            case neon::Value::OBJTYPE_STRING:
                {
                    neon::String* string;
                    string = value.asString();
                    return neon::Value::fromObject(neon::String::copy(state, string->data(), string->length()));
                }
                break;
            case neon::Value::OBJTYPE_ARRAY:
            {
                int i;
                neon::Array* list;
                neon::Array* newlist;
                list = value.asArray();
                newlist = neon::Array::make(state);
                state->stackPush(neon::Value::fromObject(newlist));
                for(i = 0; i < list->m_varray->m_count; i++)
                {
                    newlist->m_varray->push(list->m_varray->m_values[i]);
                }
                state->stackPop();
                return neon::Value::fromObject(newlist);
            }
            /*
            case neon::Value::OBJTYPE_DICT:
                {
                    neon::Dictionary *dict;
                    neon::Dictionary *newdict;
                    dict = nn_value_asdict(value);
                    newdict = neon::Dictionary::make(state);
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


void nn_file_mark(neon::State* state, neon::File* file)
{
    nn_gcmem_markobject(state, (neon::Object*)file->m_filemode);
    nn_gcmem_markobject(state, (neon::Object*)file->m_filepath);
}




void nn_instance_mark(neon::State* state, neon::ClassInstance* instance)
{
    if(instance->m_active == false)
    {
        state->warn("trying to mark inactive instance <%p>!", instance);
        return;
    }
    nn_gcmem_markobject(state, (neon::Object*)instance->m_fromclass);
    neon::HashTable::mark(state, instance->m_properties);
}


bool nn_astutil_isdigit(char c)
{
    return c >= '0' && c <= '9';
}

bool nn_astutil_isbinary(char c)
{
    return c == '0' || c == '1';
}

bool nn_astutil_isalpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool nn_astutil_isoctal(char c)
{
    return c >= '0' && c <= '7';
}

bool nn_astutil_ishexadecimal(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

const char* nn_astutil_toktype2str(int t)
{
    switch(t)
    {
        case neon::AstToken::TOK_NEWLINE: return "neon::AstToken::TOK_NEWLINE";
        case neon::AstToken::TOK_PARENOPEN: return "neon::AstToken::TOK_PARENOPEN";
        case neon::AstToken::TOK_PARENCLOSE: return "neon::AstToken::TOK_PARENCLOSE";
        case neon::AstToken::TOK_BRACKETOPEN: return "neon::AstToken::TOK_BRACKETOPEN";
        case neon::AstToken::TOK_BRACKETCLOSE: return "neon::AstToken::TOK_BRACKETCLOSE";
        case neon::AstToken::TOK_BRACEOPEN: return "neon::AstToken::TOK_BRACEOPEN";
        case neon::AstToken::TOK_BRACECLOSE: return "neon::AstToken::TOK_BRACECLOSE";
        case neon::AstToken::TOK_SEMICOLON: return "neon::AstToken::TOK_SEMICOLON";
        case neon::AstToken::TOK_COMMA: return "neon::AstToken::TOK_COMMA";
        case neon::AstToken::TOK_BACKSLASH: return "neon::AstToken::TOK_BACKSLASH";
        case neon::AstToken::TOK_EXCLMARK: return "neon::AstToken::TOK_EXCLMARK";
        case neon::AstToken::TOK_NOTEQUAL: return "neon::AstToken::TOK_NOTEQUAL";
        case neon::AstToken::TOK_COLON: return "neon::AstToken::TOK_COLON";
        case neon::AstToken::TOK_AT: return "neon::AstToken::TOK_AT";
        case neon::AstToken::TOK_DOT: return "neon::AstToken::TOK_DOT";
        case neon::AstToken::TOK_DOUBLEDOT: return "neon::AstToken::TOK_DOUBLEDOT";
        case neon::AstToken::TOK_TRIPLEDOT: return "neon::AstToken::TOK_TRIPLEDOT";
        case neon::AstToken::TOK_PLUS: return "neon::AstToken::TOK_PLUS";
        case neon::AstToken::TOK_PLUSASSIGN: return "neon::AstToken::TOK_PLUSASSIGN";
        case neon::AstToken::TOK_INCREMENT: return "neon::AstToken::TOK_INCREMENT";
        case neon::AstToken::TOK_MINUS: return "neon::AstToken::TOK_MINUS";
        case neon::AstToken::TOK_MINUSASSIGN: return "neon::AstToken::TOK_MINUSASSIGN";
        case neon::AstToken::TOK_DECREMENT: return "neon::AstToken::TOK_DECREMENT";
        case neon::AstToken::TOK_MULTIPLY: return "neon::AstToken::TOK_MULTIPLY";
        case neon::AstToken::TOK_MULTASSIGN: return "neon::AstToken::TOK_MULTASSIGN";
        case neon::AstToken::TOK_POWEROF: return "neon::AstToken::TOK_POWEROF";
        case neon::AstToken::TOK_POWASSIGN: return "neon::AstToken::TOK_POWASSIGN";
        case neon::AstToken::TOK_DIVIDE: return "neon::AstToken::TOK_DIVIDE";
        case neon::AstToken::TOK_DIVASSIGN: return "neon::AstToken::TOK_DIVASSIGN";
        case neon::AstToken::TOK_FLOOR: return "neon::AstToken::TOK_FLOOR";
        case neon::AstToken::TOK_ASSIGN: return "neon::AstToken::TOK_ASSIGN";
        case neon::AstToken::TOK_EQUAL: return "neon::AstToken::TOK_EQUAL";
        case neon::AstToken::TOK_LESSTHAN: return "neon::AstToken::TOK_LESSTHAN";
        case neon::AstToken::TOK_LESSEQUAL: return "neon::AstToken::TOK_LESSEQUAL";
        case neon::AstToken::TOK_LEFTSHIFT: return "neon::AstToken::TOK_LEFTSHIFT";
        case neon::AstToken::TOK_LEFTSHIFTASSIGN: return "neon::AstToken::TOK_LEFTSHIFTASSIGN";
        case neon::AstToken::TOK_GREATERTHAN: return "neon::AstToken::TOK_GREATERTHAN";
        case neon::AstToken::TOK_GREATER_EQ: return "neon::AstToken::TOK_GREATER_EQ";
        case neon::AstToken::TOK_RIGHTSHIFT: return "neon::AstToken::TOK_RIGHTSHIFT";
        case neon::AstToken::TOK_RIGHTSHIFTASSIGN: return "neon::AstToken::TOK_RIGHTSHIFTASSIGN";
        case neon::AstToken::TOK_MODULO: return "neon::AstToken::TOK_MODULO";
        case neon::AstToken::TOK_PERCENT_EQ: return "neon::AstToken::TOK_PERCENT_EQ";
        case neon::AstToken::TOK_AMP: return "neon::AstToken::TOK_AMP";
        case neon::AstToken::TOK_AMP_EQ: return "neon::AstToken::TOK_AMP_EQ";
        case neon::AstToken::TOK_BAR: return "neon::AstToken::TOK_BAR";
        case neon::AstToken::TOK_BAR_EQ: return "neon::AstToken::TOK_BAR_EQ";
        case neon::AstToken::TOK_TILDE: return "neon::AstToken::TOK_TILDE";
        case neon::AstToken::TOK_TILDE_EQ: return "neon::AstToken::TOK_TILDE_EQ";
        case neon::AstToken::TOK_XOR: return "neon::AstToken::TOK_XOR";
        case neon::AstToken::TOK_XOR_EQ: return "neon::AstToken::TOK_XOR_EQ";
        case neon::AstToken::TOK_QUESTION: return "neon::AstToken::TOK_QUESTION";
        case neon::AstToken::TOK_KWAND: return "neon::AstToken::TOK_KWAND";
        case neon::AstToken::TOK_KWAS: return "neon::AstToken::TOK_KWAS";
        case neon::AstToken::TOK_KWASSERT: return "neon::AstToken::TOK_KWASSERT";
        case neon::AstToken::TOK_KWBREAK: return "neon::AstToken::TOK_KWBREAK";
        case neon::AstToken::TOK_KWCATCH: return "neon::AstToken::TOK_KWCATCH";
        case neon::AstToken::TOK_KWCLASS: return "neon::AstToken::TOK_KWCLASS";
        case neon::AstToken::TOK_KWCONTINUE: return "neon::AstToken::TOK_KWCONTINUE";
        case neon::AstToken::TOK_KWFUNCTION: return "neon::AstToken::TOK_KWFUNCTION";
        case neon::AstToken::TOK_KWDEFAULT: return "neon::AstToken::TOK_KWDEFAULT";
        case neon::AstToken::TOK_KWTHROW: return "neon::AstToken::TOK_KWTHROW";
        case neon::AstToken::TOK_KWDO: return "neon::AstToken::TOK_KWDO";
        case neon::AstToken::TOK_KWECHO: return "neon::AstToken::TOK_KWECHO";
        case neon::AstToken::TOK_KWELSE: return "neon::AstToken::TOK_KWELSE";
        case neon::AstToken::TOK_KWFALSE: return "neon::AstToken::TOK_KWFALSE";
        case neon::AstToken::TOK_KWFINALLY: return "neon::AstToken::TOK_KWFINALLY";
        case neon::AstToken::TOK_KWFOREACH: return "neon::AstToken::TOK_KWFOREACH";
        case neon::AstToken::TOK_KWIF: return "neon::AstToken::TOK_KWIF";
        case neon::AstToken::TOK_KWIMPORT: return "neon::AstToken::TOK_KWIMPORT";
        case neon::AstToken::TOK_KWIN: return "neon::AstToken::TOK_KWIN";
        case neon::AstToken::TOK_KWFOR: return "neon::AstToken::TOK_KWFOR";
        case neon::AstToken::TOK_KWNULL: return "neon::AstToken::TOK_KWNULL";
        case neon::AstToken::TOK_KWNEW: return "neon::AstToken::TOK_KWNEW";
        case neon::AstToken::TOK_KWOR: return "neon::AstToken::TOK_KWOR";
        case neon::AstToken::TOK_KWSUPER: return "neon::AstToken::TOK_KWSUPER";
        case neon::AstToken::TOK_KWRETURN: return "neon::AstToken::TOK_KWRETURN";
        case neon::AstToken::TOK_KWTHIS: return "neon::AstToken::TOK_KWTHIS";
        case neon::AstToken::TOK_KWSTATIC: return "neon::AstToken::TOK_KWSTATIC";
        case neon::AstToken::TOK_KWTRUE: return "neon::AstToken::TOK_KWTRUE";
        case neon::AstToken::TOK_KWTRY: return "neon::AstToken::TOK_KWTRY";
        case neon::AstToken::TOK_KWSWITCH: return "neon::AstToken::TOK_KWSWITCH";
        case neon::AstToken::TOK_KWVAR: return "neon::AstToken::TOK_KWVAR";
        case neon::AstToken::TOK_KWCASE: return "neon::AstToken::TOK_KWCASE";
        case neon::AstToken::TOK_KWWHILE: return "neon::AstToken::TOK_KWWHILE";
        case neon::AstToken::TOK_LITERAL: return "neon::AstToken::TOK_LITERAL";
        case neon::AstToken::TOK_LITNUMREG: return "neon::AstToken::TOK_LITNUMREG";
        case neon::AstToken::TOK_LITNUMBIN: return "neon::AstToken::TOK_LITNUMBIN";
        case neon::AstToken::TOK_LITNUMOCT: return "neon::AstToken::TOK_LITNUMOCT";
        case neon::AstToken::TOK_LITNUMHEX: return "neon::AstToken::TOK_LITNUMHEX";
        case neon::AstToken::TOK_IDENTNORMAL: return "neon::AstToken::TOK_IDENTNORMAL";
        case neon::AstToken::TOK_DECORATOR: return "neon::AstToken::TOK_DECORATOR";
        case neon::AstToken::TOK_INTERPOLATION: return "neon::AstToken::TOK_INTERPOLATION";
        case neon::AstToken::TOK_EOF: return "neon::AstToken::TOK_EOF";
        case neon::AstToken::TOK_ERROR: return "neon::AstToken::TOK_ERROR";
        case neon::AstToken::TOK_KWEMPTY: return "neon::AstToken::TOK_KWEMPTY";
        case neon::AstToken::TOK_UNDEFINED: return "neon::AstToken::TOK_UNDEFINED";
        case neon::AstToken::TOK_TOKCOUNT: return "neon::AstToken::TOK_TOKCOUNT";
    }
    return "?invalid?";
}

char nn_astlex_advance(neon::AstLexer* lex)
{
    lex->m_sourceptr++;
    if(lex->m_sourceptr[-1] == '\n')
    {
        lex->m_currentline++;
    }
    return lex->m_sourceptr[-1];
}

bool nn_astlex_match(neon::AstLexer* lex, char expected)
{
    if(lex->isAtEnd())
    {
        return false;
    }
    if(*lex->m_sourceptr != expected)
    {
        return false;
    }
    lex->m_sourceptr++;
    if(lex->m_sourceptr[-1] == '\n')
    {
        lex->m_currentline++;
    }
    return true;
}

char nn_astlex_peekcurr(neon::AstLexer* lex)
{
    return *lex->m_sourceptr;
}

char nn_astlex_peekprev(neon::AstLexer* lex)
{
    return lex->m_sourceptr[-1];
}

char nn_astlex_peeknext(neon::AstLexer* lex)
{
    if(lex->isAtEnd())
    {
        return '\0';
    }
    return lex->m_sourceptr[1];
}

neon::AstToken nn_astlex_skipblockcomments(neon::AstLexer* lex)
{
    int nesting;
    nesting = 1;
    while(nesting > 0)
    {
        if(lex->isAtEnd())
        {
            return lex->errorToken("unclosed block comment");
        }
        /* internal comment open */
        if(nn_astlex_peekcurr(lex) == '/' && nn_astlex_peeknext(lex) == '*')
        {
            nn_astlex_advance(lex);
            nn_astlex_advance(lex);
            nesting++;
            continue;
        }
        /* comment close */
        if(nn_astlex_peekcurr(lex) == '*' && nn_astlex_peeknext(lex) == '/')
        {
            nn_astlex_advance(lex);
            nn_astlex_advance(lex);
            nesting--;
            continue;
        }
        /* regular comment body */
        nn_astlex_advance(lex);
    }
    #if defined(NEON_PLAT_ISWINDOWS)
    //nn_astlex_advance(lex);
    #endif
    return lex->makeToken(neon::AstToken::TOK_UNDEFINED);
}

neon::AstToken nn_astlex_skipspace(neon::AstLexer* lex)
{
    char c;
    neon::AstToken result;
    result.isglobal = false;
    for(;;)
    {
        c = nn_astlex_peekcurr(lex);
        switch(c)
        {
            case ' ':
            case '\r':
            case '\t':
            {
                nn_astlex_advance(lex);
            }
            break;
            /*
            case '\n':
                {
                    lex->m_currentline++;
                    nn_astlex_advance(lex);
                }
                break;
            */
            /*
            case '#':
                // single line comment
                {
                    while(nn_astlex_peekcurr(lex) != '\n' && !lex->isAtEnd())
                        nn_astlex_advance(lex);

                }
                break;
            */
            case '/':
            {
                if(nn_astlex_peeknext(lex) == '/')
                {
                    while(nn_astlex_peekcurr(lex) != '\n' && !lex->isAtEnd())
                    {
                        nn_astlex_advance(lex);
                    }
                    return lex->makeToken(neon::AstToken::TOK_UNDEFINED);
                }
                else if(nn_astlex_peeknext(lex) == '*')
                {
                    nn_astlex_advance(lex);
                    nn_astlex_advance(lex);
                    result = nn_astlex_skipblockcomments(lex);
                    if(result.type != neon::AstToken::TOK_UNDEFINED)
                    {
                        return result;
                    }
                    break;
                }
                else
                {
                    return lex->makeToken(neon::AstToken::TOK_UNDEFINED);
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
    return lex->makeToken(neon::AstToken::TOK_UNDEFINED);
}

neon::AstToken nn_astlex_scanstring(neon::AstLexer* lex, char quote, bool withtemplate)
{
    neon::AstToken tkn;
    NEON_ASTDEBUG(lex->m_pvm, "quote=[%c] withtemplate=%d", quote, withtemplate);
    while(nn_astlex_peekcurr(lex) != quote && !lex->isAtEnd())
    {
        if(withtemplate)
        {
            /* interpolation started */
            if(nn_astlex_peekcurr(lex) == '$' && nn_astlex_peeknext(lex) == '{' && nn_astlex_peekprev(lex) != '\\')
            {
                if(lex->m_tplstringcount - 1 < NEON_CFG_ASTMAXSTRTPLDEPTH)
                {
                    lex->m_tplstringcount++;
                    lex->m_tplstringbuffer[lex->m_tplstringcount] = (int)quote;
                    lex->m_sourceptr++;
                    tkn = lex->makeToken(neon::AstToken::TOK_INTERPOLATION);
                    lex->m_sourceptr++;
                    return tkn;
                }
                return lex->errorToken("maximum interpolation nesting of %d exceeded by %d", NEON_CFG_ASTMAXSTRTPLDEPTH,
                    NEON_CFG_ASTMAXSTRTPLDEPTH - lex->m_tplstringcount + 1);
            }
        }
        if(nn_astlex_peekcurr(lex) == '\\' && (nn_astlex_peeknext(lex) == quote || nn_astlex_peeknext(lex) == '\\'))
        {
            nn_astlex_advance(lex);
        }
        nn_astlex_advance(lex);
    }
    if(lex->isAtEnd())
    {
        return lex->errorToken("unterminated string (opening quote not matched)");
    }
    /* the closing quote */
    nn_astlex_match(lex, quote);
    return lex->makeToken(neon::AstToken::TOK_LITERAL);
}

neon::AstToken nn_astlex_scannumber(neon::AstLexer* lex)
{
    NEON_ASTDEBUG(lex->m_pvm, "");
    /* handle binary, octal and hexadecimals */
    if(nn_astlex_peekprev(lex) == '0')
    {
        /* binary number */
        if(nn_astlex_match(lex, 'b'))
        {
            while(nn_astutil_isbinary(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
            return lex->makeToken(neon::AstToken::TOK_LITNUMBIN);
        }
        else if(nn_astlex_match(lex, 'c'))
        {
            while(nn_astutil_isoctal(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
            return lex->makeToken(neon::AstToken::TOK_LITNUMOCT);
        }
        else if(nn_astlex_match(lex, 'x'))
        {
            while(nn_astutil_ishexadecimal(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
            return lex->makeToken(neon::AstToken::TOK_LITNUMHEX);
        }
    }
    while(nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
    {
        nn_astlex_advance(lex);
    }
    /* dots(.) are only valid here when followed by a digit */
    if(nn_astlex_peekcurr(lex) == '.' && nn_astutil_isdigit(nn_astlex_peeknext(lex)))
    {
        nn_astlex_advance(lex);
        while(nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
        {
            nn_astlex_advance(lex);
        }
        /*
        // E or e are only valid here when followed by a digit and occurring after a dot
        */
        if((nn_astlex_peekcurr(lex) == 'e' || nn_astlex_peekcurr(lex) == 'E') && (nn_astlex_peeknext(lex) == '+' || nn_astlex_peeknext(lex) == '-'))
        {
            nn_astlex_advance(lex);
            nn_astlex_advance(lex);
            while(nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
        }
    }
    return lex->makeToken(neon::AstToken::TOK_LITNUMREG);
}

neon::AstToken::Type nn_astlex_getidenttype(neon::AstLexer* lex)
{
    static const struct
    {
        const char* str;
        int tokid;
    }
    keywords[] =
    {
        { "and", neon::AstToken::TOK_KWAND },
        { "assert", neon::AstToken::TOK_KWASSERT },
        { "as", neon::AstToken::TOK_KWAS },
        { "break", neon::AstToken::TOK_KWBREAK },
        { "catch", neon::AstToken::TOK_KWCATCH },
        { "class", neon::AstToken::TOK_KWCLASS },
        { "continue", neon::AstToken::TOK_KWCONTINUE },
        { "default", neon::AstToken::TOK_KWDEFAULT },
        { "def", neon::AstToken::TOK_KWFUNCTION },
        { "function", neon::AstToken::TOK_KWFUNCTION },
        { "throw", neon::AstToken::TOK_KWTHROW },
        { "do", neon::AstToken::TOK_KWDO },
        { "echo", neon::AstToken::TOK_KWECHO },
        { "else", neon::AstToken::TOK_KWELSE },
        { "empty", neon::AstToken::TOK_KWEMPTY },
        { "false", neon::AstToken::TOK_KWFALSE },
        { "finally", neon::AstToken::TOK_KWFINALLY },
        { "foreach", neon::AstToken::TOK_KWFOREACH },
        { "if", neon::AstToken::TOK_KWIF },
        { "import", neon::AstToken::TOK_KWIMPORT },
        { "in", neon::AstToken::TOK_KWIN },
        { "for", neon::AstToken::TOK_KWFOR },
        { "null", neon::AstToken::TOK_KWNULL },
        { "new", neon::AstToken::TOK_KWNEW },
        { "or", neon::AstToken::TOK_KWOR },
        { "super", neon::AstToken::TOK_KWSUPER },
        { "return", neon::AstToken::TOK_KWRETURN },
        { "this", neon::AstToken::TOK_KWTHIS },
        { "static", neon::AstToken::TOK_KWSTATIC },
        { "true", neon::AstToken::TOK_KWTRUE },
        { "try", neon::AstToken::TOK_KWTRY },
        { "typeof", neon::AstToken::TOK_KWTYPEOF },
        { "switch", neon::AstToken::TOK_KWSWITCH },
        { "case", neon::AstToken::TOK_KWCASE },
        { "var", neon::AstToken::TOK_KWVAR },
        { "while", neon::AstToken::TOK_KWWHILE },
        { nullptr, (neon::AstToken::Type)0 }
    };
    size_t i;
    size_t kwlen;
    size_t ofs;
    const char* kwtext;
    for(i = 0; keywords[i].str != nullptr; i++)
    {
        kwtext = keywords[i].str;
        kwlen = strlen(kwtext);
        ofs = (lex->m_sourceptr - lex->m_plainsource);
        if(ofs == kwlen)
        {
            if(memcmp(lex->m_plainsource, kwtext, kwlen) == 0)
            {
                return (neon::AstToken::Type)keywords[i].tokid;
            }
        }
    }
    return neon::AstToken::TOK_IDENTNORMAL;
}

neon::AstToken nn_astlex_scanident(neon::AstLexer* lex, bool isdollar)
{
    int cur;
    neon::AstToken tok;
    cur = nn_astlex_peekcurr(lex);
    if(cur == '$')
    {
        nn_astlex_advance(lex);
    }
    while(true)
    {
        cur = nn_astlex_peekcurr(lex);
        if(nn_astutil_isalpha(cur) || nn_astutil_isdigit(cur))
        {
            nn_astlex_advance(lex);
        }
        else
        {
            break;
        }
    }
    tok = lex->makeToken(nn_astlex_getidenttype(lex));
    tok.isglobal = isdollar;
    return tok;
}

neon::AstToken nn_astlex_scandecorator(neon::AstLexer* lex)
{
    while(nn_astutil_isalpha(nn_astlex_peekcurr(lex)) || nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
    {
        nn_astlex_advance(lex);
    }
    return lex->makeToken(neon::AstToken::TOK_DECORATOR);
}

neon::AstToken nn_astlex_scantoken(neon::AstLexer* lex)
{
    char c;
    bool isdollar;
    neon::AstToken tk;
    neon::AstToken token;
    tk = nn_astlex_skipspace(lex);
    if(tk.type != neon::AstToken::TOK_UNDEFINED)
    {
        return tk;
    }
    lex->m_plainsource = lex->m_sourceptr;
    if(lex->isAtEnd())
    {
        return lex->makeToken(neon::AstToken::TOK_EOF);
    }
    c = nn_astlex_advance(lex);
    if(nn_astutil_isdigit(c))
    {
        return nn_astlex_scannumber(lex);
    }
    else if(nn_astutil_isalpha(c) || (c == '$'))
    {
        isdollar = (c == '$');
        return nn_astlex_scanident(lex, isdollar);
    }
    switch(c)
    {
        case '(':
            {
                return lex->makeToken(neon::AstToken::TOK_PARENOPEN);
            }
            break;
        case ')':
            {
                return lex->makeToken(neon::AstToken::TOK_PARENCLOSE);
            }
            break;
        case '[':
            {
                return lex->makeToken(neon::AstToken::TOK_BRACKETOPEN);
            }
            break;
        case ']':
            {
                return lex->makeToken(neon::AstToken::TOK_BRACKETCLOSE);
            }
            break;
        case '{':
            {
                return lex->makeToken(neon::AstToken::TOK_BRACEOPEN);
            }
            break;
        case '}':
            {
                if(lex->m_tplstringcount > -1)
                {
                    token = nn_astlex_scanstring(lex, (char)lex->m_tplstringbuffer[lex->m_tplstringcount], true);
                    lex->m_tplstringcount--;
                    return token;
                }
                return lex->makeToken(neon::AstToken::TOK_BRACECLOSE);
            }
            break;
        case ';':
            {
                return lex->makeToken(neon::AstToken::TOK_SEMICOLON);
            }
            break;
        case '\\':
            {
                return lex->makeToken(neon::AstToken::TOK_BACKSLASH);
            }
            break;
        case ':':
            {
                return lex->makeToken(neon::AstToken::TOK_COLON);
            }
            break;
        case ',':
            {
                return lex->makeToken(neon::AstToken::TOK_COMMA);
            }
            break;
        case '@':
            {
                if(!nn_astutil_isalpha(nn_astlex_peekcurr(lex)))
                {
                    return lex->makeToken(neon::AstToken::TOK_AT);
                }
                return nn_astlex_scandecorator(lex);
            }
            break;
        case '!':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return lex->makeToken(neon::AstToken::TOK_NOTEQUAL);
                }
                return lex->makeToken(neon::AstToken::TOK_EXCLMARK);

            }
            break;
        case '.':
            {
                if(nn_astlex_match(lex, '.'))
                {
                    if(nn_astlex_match(lex, '.'))
                    {
                        return lex->makeToken(neon::AstToken::TOK_TRIPLEDOT);
                    }
                    return lex->makeToken(neon::AstToken::TOK_DOUBLEDOT);
                }
                return lex->makeToken(neon::AstToken::TOK_DOT);
            }
            break;
        case '+':
        {
            if(nn_astlex_match(lex, '+'))
            {
                return lex->makeToken(neon::AstToken::TOK_INCREMENT);
            }
            if(nn_astlex_match(lex, '='))
            {
                return lex->makeToken(neon::AstToken::TOK_PLUSASSIGN);
            }
            else
            {
                return lex->makeToken(neon::AstToken::TOK_PLUS);
            }
        }
        break;
        case '-':
            {
                if(nn_astlex_match(lex, '-'))
                {
                    return lex->makeToken(neon::AstToken::TOK_DECREMENT);
                }
                if(nn_astlex_match(lex, '='))
                {
                    return lex->makeToken(neon::AstToken::TOK_MINUSASSIGN);
                }
                else
                {
                    return lex->makeToken(neon::AstToken::TOK_MINUS);
                }
            }
            break;
        case '*':
            {
                if(nn_astlex_match(lex, '*'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return lex->makeToken(neon::AstToken::TOK_POWASSIGN);
                    }
                    return lex->makeToken(neon::AstToken::TOK_POWEROF);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return lex->makeToken(neon::AstToken::TOK_MULTASSIGN);
                    }
                    return lex->makeToken(neon::AstToken::TOK_MULTIPLY);
                }
            }
            break;
        case '/':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return lex->makeToken(neon::AstToken::TOK_DIVASSIGN);
                }
                return lex->makeToken(neon::AstToken::TOK_DIVIDE);
            }
            break;
        case '=':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return lex->makeToken(neon::AstToken::TOK_EQUAL);
                }
                return lex->makeToken(neon::AstToken::TOK_ASSIGN);
            }        
            break;
        case '<':
            {
                if(nn_astlex_match(lex, '<'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return lex->makeToken(neon::AstToken::TOK_LEFTSHIFTASSIGN);
                    }
                    return lex->makeToken(neon::AstToken::TOK_LEFTSHIFT);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return lex->makeToken(neon::AstToken::TOK_LESSEQUAL);
                    }
                    return lex->makeToken(neon::AstToken::TOK_LESSTHAN);

                }
            }
            break;
        case '>':
            {
                if(nn_astlex_match(lex, '>'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return lex->makeToken(neon::AstToken::TOK_RIGHTSHIFTASSIGN);
                    }
                    return lex->makeToken(neon::AstToken::TOK_RIGHTSHIFT);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return lex->makeToken(neon::AstToken::TOK_GREATER_EQ);
                    }
                    return lex->makeToken(neon::AstToken::TOK_GREATERTHAN);
                }
            }
            break;
        case '%':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return lex->makeToken(neon::AstToken::TOK_PERCENT_EQ);
                }
                return lex->makeToken(neon::AstToken::TOK_MODULO);
            }
            break;
        case '&':
            {
                if(nn_astlex_match(lex, '&'))
                {
                    return lex->makeToken(neon::AstToken::TOK_KWAND);
                }
                else if(nn_astlex_match(lex, '='))
                {
                    return lex->makeToken(neon::AstToken::TOK_AMP_EQ);
                }
                return lex->makeToken(neon::AstToken::TOK_AMP);
            }
            break;
        case '|':
            {
                if(nn_astlex_match(lex, '|'))
                {
                    return lex->makeToken(neon::AstToken::TOK_KWOR);
                }
                else if(nn_astlex_match(lex, '='))
                {
                    return lex->makeToken(neon::AstToken::TOK_BAR_EQ);
                }
                return lex->makeToken(neon::AstToken::TOK_BAR);
            }
            break;
        case '~':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return lex->makeToken(neon::AstToken::TOK_TILDE_EQ);
                }
                return lex->makeToken(neon::AstToken::TOK_TILDE);
            }
            break;
        case '^':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return lex->makeToken(neon::AstToken::TOK_XOR_EQ);
                }
                return lex->makeToken(neon::AstToken::TOK_XOR);
            }
            break;
        case '\n':
            {
                return lex->makeToken(neon::AstToken::TOK_NEWLINE);
            }
            break;
        case '"':
            {
                return nn_astlex_scanstring(lex, '"', true);
            }
            break;
        case '\'':
            {
                return nn_astlex_scanstring(lex, '\'', false);
            }
            break;
        case '?':
            {
                return lex->makeToken(neon::AstToken::TOK_QUESTION);
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
            break;
    }
    return lex->errorToken("unexpected character %c", c);
}


neon::Blob* nn_astparser_currentblob(neon::AstParser* prs)
{
    return prs->m_currfunccompiler->targetfunc->blob;
}

bool nn_astparser_raiseerroratv(neon::AstParser* prs, neon::AstToken* t, const char* message, va_list args)
{
    fflush(stdout);
    /*
    // do not cascade error
    // suppress error if already in panic mode
    */
    if(prs->m_panicmode)
    {
        return false;
    }
    prs->m_panicmode = true;
    fprintf(stderr, "SyntaxError");
    if(t->type == neon::AstToken::TOK_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if(t->type == neon::AstToken::TOK_ERROR)
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
    vfprintf(stderr, message, args);
    fputs("\n", stderr);
    fprintf(stderr, "  %s:%d\n", prs->m_currmodule->physicalpath->data(), t->line);
    prs->m_haderror = true;
    return false;
}

bool nn_astparser_raiseerror(neon::AstParser* prs, const char* message, ...)
{
    va_list args;
    va_start(args, message);
    nn_astparser_raiseerroratv(prs, &prs->m_prevtoken, message, args);
    va_end(args);
    return false;
}

bool nn_astparser_raiseerroratcurrent(neon::AstParser* prs, const char* message, ...)
{
    va_list args;
    va_start(args, message);
    nn_astparser_raiseerroratv(prs, &prs->m_currtoken, message, args);
    va_end(args);
    return false;
}

void nn_astparser_advance(neon::AstParser* prs)
{
    prs->m_prevtoken = prs->m_currtoken;
    while(true)
    {
        prs->m_currtoken = nn_astlex_scantoken(prs->m_lexer);
        if(prs->m_currtoken.type != neon::AstToken::TOK_ERROR)
        {
            break;
        }
        nn_astparser_raiseerroratcurrent(prs, prs->m_currtoken.start);
    }
}

bool nn_astparser_consume(neon::AstParser* prs, neon::AstToken::Type t, const char* message)
{
    if(prs->m_currtoken.type == t)
    {
        nn_astparser_advance(prs);
        return true;
    }
    return nn_astparser_raiseerroratcurrent(prs, message);
}

void nn_astparser_consumeor(neon::AstParser* prs, const char* message, const neon::AstToken::Type* ts, int count)
{
    int i;
    for(i = 0; i < count; i++)
    {
        if(prs->m_currtoken.type == ts[i])
        {
            nn_astparser_advance(prs);
            return;
        }
    }
    nn_astparser_raiseerroratcurrent(prs, message);
}

bool nn_astparser_checknumber(neon::AstParser* prs)
{
    neon::AstToken::Type t;
    t = prs->m_prevtoken.type;
    if(t == neon::AstToken::TOK_LITNUMREG || t == neon::AstToken::TOK_LITNUMOCT || t == neon::AstToken::TOK_LITNUMBIN || t == neon::AstToken::TOK_LITNUMHEX)
    {
        return true;
    }
    return false;
}

bool nn_astparser_check(neon::AstParser* prs, neon::AstToken::Type t)
{
    return prs->m_currtoken.type == t;
}

bool nn_astparser_match(neon::AstParser* prs, neon::AstToken::Type t)
{
    if(!nn_astparser_check(prs, t))
    {
        return false;
    }
    nn_astparser_advance(prs);
    return true;
}

void nn_astparser_runparser(neon::AstParser* parser)
{
    nn_astparser_advance(parser);
    nn_astparser_ignorewhitespace(parser);
    while(!nn_astparser_match(parser, neon::AstToken::TOK_EOF))
    {
        nn_astparser_parsedeclaration(parser);
    }
}

void nn_astparser_parsedeclaration(neon::AstParser* prs)
{
    nn_astparser_ignorewhitespace(prs);
    if(nn_astparser_match(prs, neon::AstToken::TOK_KWCLASS))
    {
        nn_astparser_parseclassdeclaration(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWFUNCTION))
    {
        nn_astparser_parsefuncdecl(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWVAR))
    {
        nn_astparser_parsevardecl(prs, false);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_BRACEOPEN))
    {
        if(!nn_astparser_check(prs, neon::AstToken::TOK_NEWLINE) && prs->m_currfunccompiler->scopedepth == 0)
        {
            nn_astparser_parseexprstmt(prs, false, true);
        }
        else
        {
            nn_astparser_scopebegin(prs);
            nn_astparser_parseblock(prs);
            nn_astparser_scopeend(prs);
        }
    }
    else
    {
        nn_astparser_parsestmt(prs);
    }
    nn_astparser_ignorewhitespace(prs);
    if(prs->m_panicmode)
    {
        nn_astparser_synchronize(prs);
    }
    nn_astparser_ignorewhitespace(prs);
}

void nn_astparser_parsestmt(neon::AstParser* prs)
{
    prs->m_replcanecho = false;
    nn_astparser_ignorewhitespace(prs);
    if(nn_astparser_match(prs, neon::AstToken::TOK_KWECHO))
    {
        nn_astparser_parseechostmt(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWIF))
    {
        nn_astparser_parseifstmt(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWDO))
    {
        nn_astparser_parsedo_whilestmt(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWWHILE))
    {
        nn_astparser_parsewhilestmt(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWFOR))
    {
        nn_astparser_parseforstmt(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWFOREACH))
    {
        nn_astparser_parseforeachstmt(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWSWITCH))
    {
        nn_astparser_parseswitchstmt(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWCONTINUE))
    {
        nn_astparser_parsecontinuestmt(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWBREAK))
    {
        nn_astparser_parsebreakstmt(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWRETURN))
    {
        nn_astparser_parsereturnstmt(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWASSERT))
    {
        nn_astparser_parseassertstmt(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWTHROW))
    {
        nn_astparser_parsethrowstmt(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_BRACEOPEN))
    {
        nn_astparser_scopebegin(prs);
        nn_astparser_parseblock(prs);
        nn_astparser_scopeend(prs);
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWTRY))
    {
        nn_astparser_parsetrystmt(prs);
    }
    else
    {
        nn_astparser_parseexprstmt(prs, false, false);
    }
    nn_astparser_ignorewhitespace(prs);
}

void nn_astparser_consumestmtend(neon::AstParser* prs)
{
    /* allow block last statement to omit statement end */
    if(prs->m_blockcount > 0 && nn_astparser_check(prs, neon::AstToken::TOK_BRACECLOSE))
    {
        return;
    }
    if(nn_astparser_match(prs, neon::AstToken::TOK_SEMICOLON))
    {
        while(nn_astparser_match(prs, neon::AstToken::TOK_SEMICOLON) || nn_astparser_match(prs, neon::AstToken::TOK_NEWLINE))
        {
        }
        return;
    }
    if(nn_astparser_match(prs, neon::AstToken::TOK_EOF) || prs->m_prevtoken.type == neon::AstToken::TOK_EOF)
    {
        return;
    }
    /* nn_astparser_consume(prs, neon::AstToken::TOK_NEWLINE, "end of statement expected"); */
    while(nn_astparser_match(prs, neon::AstToken::TOK_SEMICOLON) || nn_astparser_match(prs, neon::AstToken::TOK_NEWLINE))
    {
    }
}

void nn_astparser_ignorewhitespace(neon::AstParser* prs)
{
    while(true)
    {
        if(nn_astparser_check(prs, neon::AstToken::TOK_NEWLINE))
        {
            nn_astparser_advance(prs);
        }
        else
        {
            break;
        }
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
                return 2 + (fn->upvalcount * 3);
            }
            break;
        default:
            break;
    }
    return 0;
}

void nn_astemit_emit(neon::AstParser* prs, uint8_t byte, int line, bool isop)
{
    neon::Instruction ins;
    ins.code = byte;
    ins.srcline = line;
    ins.isop = isop;
    nn_astparser_currentblob(prs)->push(ins);
}

void nn_astemit_patchat(neon::AstParser* prs, size_t idx, uint8_t byte)
{
    nn_astparser_currentblob(prs)->m_instrucs[idx].code = byte;
}

void nn_astemit_emitinstruc(neon::AstParser* prs, uint8_t byte)
{
    nn_astemit_emit(prs, byte, prs->m_prevtoken.line, true);
}

void nn_astemit_emit1byte(neon::AstParser* prs, uint8_t byte)
{
    nn_astemit_emit(prs, byte, prs->m_prevtoken.line, false);
}

void nn_astemit_emit1short(neon::AstParser* prs, uint16_t byte)
{
    nn_astemit_emit(prs, (byte >> 8) & 0xff, prs->m_prevtoken.line, false);
    nn_astemit_emit(prs, byte & 0xff, prs->m_prevtoken.line, false);
}

void nn_astemit_emit2byte(neon::AstParser* prs, uint8_t byte, uint8_t byte2)
{
    nn_astemit_emit(prs, byte, prs->m_prevtoken.line, false);
    nn_astemit_emit(prs, byte2, prs->m_prevtoken.line, false);
}

void nn_astemit_emitbyteandshort(neon::AstParser* prs, uint8_t byte, uint16_t byte2)
{
    nn_astemit_emit(prs, byte, prs->m_prevtoken.line, false);
    nn_astemit_emit(prs, (byte2 >> 8) & 0xff, prs->m_prevtoken.line, false);
    nn_astemit_emit(prs, byte2 & 0xff, prs->m_prevtoken.line, false);
}

void nn_astemit_emitloop(neon::AstParser* prs, int loopstart)
{
    int offset;
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_LOOP);
    offset = nn_astparser_currentblob(prs)->m_count - loopstart + 2;
    if(offset > UINT16_MAX)
    {
        nn_astparser_raiseerror(prs, "loop body too large");
    }
    nn_astemit_emit1byte(prs, (offset >> 8) & 0xff);
    nn_astemit_emit1byte(prs, offset & 0xff);
}

void nn_astemit_emitreturn(neon::AstParser* prs)
{
    if(prs->m_istrying)
    {
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_EXPOPTRY);
    }
    if(prs->m_currfunccompiler->type == neon::FuncCommon::FUNCTYPE_INITIALIZER)
    {
        nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_LOCALGET, 0);
    }
    else
    {
        if(!prs->m_keeplastvalue || prs->m_lastwasstatement)
        {
            if(prs->m_currfunccompiler->fromimport)
            {
                nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHNULL);
            }
            else
            {
                nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHEMPTY);
            }
        }
    }
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_RETURN);
}

int nn_astparser_pushconst(neon::AstParser* prs, neon::Value value)
{
    int constant;
    constant = nn_astparser_currentblob(prs)->pushConst(value);
    if(constant >= UINT16_MAX)
    {
        nn_astparser_raiseerror(prs, "too many constants in current scope");
        return 0;
    }
    return constant;
}

void nn_astemit_emitconst(neon::AstParser* prs, neon::Value value)
{
    int constant;
    constant = nn_astparser_pushconst(prs, value);
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_PUSHCONSTANT, (uint16_t)constant);
}

int nn_astemit_emitjump(neon::AstParser* prs, uint8_t instruction)
{
    nn_astemit_emitinstruc(prs, instruction);
    /* placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->m_count - 2;
}

int nn_astemit_emitswitch(neon::AstParser* prs)
{
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_SWITCH);
    /* placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->m_count - 2;
}

int nn_astemit_emittry(neon::AstParser* prs)
{
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_EXTRY);
    /* type placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    /* handler placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    /* finally placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->m_count - 6;
}

void nn_astemit_patchswitch(neon::AstParser* prs, int offset, int constant)
{
    nn_astemit_patchat(prs, offset, (constant >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 1, constant & 0xff);
}

void nn_astemit_patchtry(neon::AstParser* prs, int offset, int type, int address, int finally)
{
    /* patch type */
    nn_astemit_patchat(prs, offset, (type >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 1, type & 0xff);
    /* patch address */
    nn_astemit_patchat(prs, offset + 2, (address >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 3, address & 0xff);
    /* patch finally */
    nn_astemit_patchat(prs, offset + 4, (finally >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 5, finally & 0xff);
}

void nn_astemit_patchjump(neon::AstParser* prs, int offset)
{
    /* -2 to adjust the bytecode for the offset itself */
    int jump;
    jump = nn_astparser_currentblob(prs)->m_count - offset - 2;
    if(jump > UINT16_MAX)
    {
        nn_astparser_raiseerror(prs, "body of conditional block too large");
    }
    nn_astemit_patchat(prs, offset, (jump >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 1, jump & 0xff);
}

void nn_astfunccompiler_init(neon::AstParser* prs, neon::AstFuncCompiler* compiler, neon::FuncCommon::Type type, bool isanon)
{
    bool candeclthis;
    neon::AstFuncCompiler::CompiledLocal* local;
    neon::String* fname;
    compiler->enclosing = prs->m_pvm->m_activeparser->m_currfunccompiler;
    compiler->targetfunc = nullptr;
    compiler->type = type;
    compiler->localcount = 0;
    compiler->scopedepth = 0;
    compiler->handlercount = 0;
    compiler->fromimport = false;
    compiler->targetfunc = neon::FuncScript::make(prs->m_pvm, prs->m_currmodule, type);
    prs->m_currfunccompiler = compiler;
    if(type != neon::FuncCommon::FUNCTYPE_SCRIPT)
    {
        prs->m_pvm->stackPush(neon::Value::fromObject(compiler->targetfunc));
        if(isanon)
        {
            neon::Printer ptmp(prs->m_pvm);
            ptmp.putformat("anonymous@[%s:%d]", prs->m_currentphysfile, prs->m_prevtoken.line);
            fname = ptmp.takeString();
        }
        else
        {
            fname = neon::String::copy(prs->m_pvm, prs->m_prevtoken.start, prs->m_prevtoken.length);
        }
        prs->m_currfunccompiler->targetfunc->name = fname;
        prs->m_pvm->stackPop();
    }
    /* claiming slot zero for use in class methods */
    local = &prs->m_currfunccompiler->locals[0];
    prs->m_currfunccompiler->localcount++;
    local->depth = 0;
    local->iscaptured = false;
    candeclthis = (
        (type != neon::FuncCommon::FUNCTYPE_FUNCTION) &&
        (prs->m_compcontext == neon::AstParser::COMPCONTEXT_CLASS)
    );
    if(candeclthis || (/*(type == neon::FuncCommon::FUNCTYPE_ANONYMOUS) &&*/ (prs->m_compcontext != neon::AstParser::COMPCONTEXT_CLASS)))
    {
        local->name.start = neon::g_strthis;
        local->name.length = 4;
    }
    else
    {
        local->name.start = "";
        local->name.length = 0;
    }
}

int nn_astparser_makeidentconst(neon::AstParser* prs, neon::AstToken* name)
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
    return nn_astparser_pushconst(prs, neon::Value::fromObject(str));
}

bool nn_astparser_identsequal(neon::AstToken* a, neon::AstToken* b)
{
    return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
}

int nn_astfunccompiler_resolvelocal(neon::AstParser* prs, neon::AstFuncCompiler* compiler, neon::AstToken* name)
{
    int i;
    neon::AstFuncCompiler::CompiledLocal* local;
    for(i = compiler->localcount - 1; i >= 0; i--)
    {
        local = &compiler->locals[i];
        if(nn_astparser_identsequal(&local->name, name))
        {
            if(local->depth == -1)
            {
                nn_astparser_raiseerror(prs, "cannot read local variable in it's own initializer");
            }
            return i;
        }
    }
    return -1;
}

int nn_astfunccompiler_addupvalue(neon::AstParser* prs, neon::AstFuncCompiler* compiler, uint16_t index, bool islocal)
{
    int i;
    int upcnt;
    neon::AstFuncCompiler::CompiledUpvalue* upvalue;
    upcnt = compiler->targetfunc->upvalcount;
    for(i = 0; i < upcnt; i++)
    {
        upvalue = &compiler->upvalues[i];
        if(upvalue->index == index && upvalue->islocal == islocal)
        {
            return i;
        }
    }
    if(upcnt == NEON_CFG_ASTMAXUPVALS)
    {
        nn_astparser_raiseerror(prs, "too many closure variables in function");
        return 0;
    }
    compiler->upvalues[upcnt].islocal = islocal;
    compiler->upvalues[upcnt].index = index;
    return compiler->targetfunc->upvalcount++;
}

int nn_astfunccompiler_resolveupvalue(neon::AstParser* prs, neon::AstFuncCompiler* compiler, neon::AstToken* name)
{
    int local;
    int upvalue;
    if(compiler->enclosing == nullptr)
    {
        return -1;
    }
    local = nn_astfunccompiler_resolvelocal(prs, compiler->enclosing, name);
    if(local != -1)
    {
        compiler->enclosing->locals[local].iscaptured = true;
        return nn_astfunccompiler_addupvalue(prs, compiler, (uint16_t)local, true);
    }
    upvalue = nn_astfunccompiler_resolveupvalue(prs, compiler->enclosing, name);
    if(upvalue != -1)
    {
        return nn_astfunccompiler_addupvalue(prs, compiler, (uint16_t)upvalue, false);
    }
    return -1;
}

int nn_astparser_addlocal(neon::AstParser* prs, neon::AstToken name)
{
    neon::AstFuncCompiler::CompiledLocal* local;
    if(prs->m_currfunccompiler->localcount == NEON_CFG_ASTMAXLOCALS)
    {
        /* we've reached maximum local variables per scope */
        nn_astparser_raiseerror(prs, "too many local variables in scope");
        return -1;
    }
    local = &prs->m_currfunccompiler->locals[prs->m_currfunccompiler->localcount++];
    local->name = name;
    local->depth = -1;
    local->iscaptured = false;
    return prs->m_currfunccompiler->localcount;
}

void nn_astparser_declarevariable(neon::AstParser* prs)
{
    int i;
    neon::AstToken* name;
    neon::AstFuncCompiler::CompiledLocal* local;
    /* global variables are implicitly declared... */
    if(prs->m_currfunccompiler->scopedepth == 0)
    {
        return;
    }
    name = &prs->m_prevtoken;
    for(i = prs->m_currfunccompiler->localcount - 1; i >= 0; i--)
    {
        local = &prs->m_currfunccompiler->locals[i];
        if(local->depth != -1 && local->depth < prs->m_currfunccompiler->scopedepth)
        {
            break;
        }
        if(nn_astparser_identsequal(name, &local->name))
        {
            nn_astparser_raiseerror(prs, "%.*s already declared in current scope", name->length, name->start);
        }
    }
    nn_astparser_addlocal(prs, *name);
}

int nn_astparser_parsevariable(neon::AstParser* prs, const char* message)
{
    if(!nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, message))
    {
        /* what to do here? */
    }
    nn_astparser_declarevariable(prs);
    /* we are in a local scope... */
    if(prs->m_currfunccompiler->scopedepth > 0)
    {
        return 0;
    }
    return nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
}

void nn_astparser_markinitialized(neon::AstParser* prs)
{
    if(prs->m_currfunccompiler->scopedepth == 0)
    {
        return;
    }
    prs->m_currfunccompiler->locals[prs->m_currfunccompiler->localcount - 1].depth = prs->m_currfunccompiler->scopedepth;
}

void nn_astparser_definevariable(neon::AstParser* prs, int global)
{
    /* we are in a local scope... */
    if(prs->m_currfunccompiler->scopedepth > 0)
    {
        nn_astparser_markinitialized(prs);
        return;
    }
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_GLOBALDEFINE, global);
}

neon::AstToken nn_astparser_synthtoken(const char* name)
{
    neon::AstToken token;
    token.isglobal = false;
    token.line = 0;
    token.type = (neon::AstToken::Type)0;
    token.start = name;
    token.length = (int)strlen(name);
    return token;
}

neon::FuncScript* nn_astparser_endcompiler(neon::AstParser* prs)
{
    const char* fname;
    neon::FuncScript* function;
    nn_astemit_emitreturn(prs);
    function = prs->m_currfunccompiler->targetfunc;
    fname = nullptr;
    if(function->name == nullptr)
    {
        fname = prs->m_currmodule->physicalpath->data();
    }
    else
    {
        fname = function->name->data();
    }
    if(!prs->m_haderror && prs->m_pvm->m_conf.dumpbytecode)
    {
        nn_dbg_disasmblob(prs->m_pvm->m_debugprinter, nn_astparser_currentblob(prs), fname);
    }
    NEON_ASTDEBUG(prs->m_pvm, "for function '%s'", fname);
    prs->m_currfunccompiler = prs->m_currfunccompiler->enclosing;
    return function;
}

void nn_astparser_scopebegin(neon::AstParser* prs)
{
    NEON_ASTDEBUG(prs->m_pvm, "current depth=%d", prs->m_currfunccompiler->scopedepth);
    prs->m_currfunccompiler->scopedepth++;
}

bool nn_astutil_scopeendcancontinue(neon::AstParser* prs)
{
    int lopos;
    int locount;
    int lodepth;
    int scodepth;
    NEON_ASTDEBUG(prs->m_pvm, "");
    locount = prs->m_currfunccompiler->localcount;
    lopos = prs->m_currfunccompiler->localcount - 1;
    lodepth = prs->m_currfunccompiler->locals[lopos].depth;
    scodepth = prs->m_currfunccompiler->scopedepth;
    if(locount > 0 && lodepth > scodepth)
    {
        return true;
    }
    return false;
}

void nn_astparser_scopeend(neon::AstParser* prs)
{
    NEON_ASTDEBUG(prs->m_pvm, "current scope depth=%d", prs->m_currfunccompiler->scopedepth);
    prs->m_currfunccompiler->scopedepth--;
    /*
    // remove all variables declared in scope while exiting...
    */
    if(prs->m_keeplastvalue)
    {
        //return;
    }
    while(nn_astutil_scopeendcancontinue(prs))
    {
        if(prs->m_currfunccompiler->locals[prs->m_currfunccompiler->localcount - 1].iscaptured)
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_UPVALUECLOSE);
        }
        else
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
        }
        prs->m_currfunccompiler->localcount--;
    }
}

int nn_astparser_discardlocals(neon::AstParser* prs, int depth)
{
    int local;
    NEON_ASTDEBUG(prs->m_pvm, "");
    if(prs->m_keeplastvalue)
    {
        //return 0;
    }
    if(prs->m_currfunccompiler->scopedepth == -1)
    {
        nn_astparser_raiseerror(prs, "cannot exit top-level scope");
    }
    local = prs->m_currfunccompiler->localcount - 1;
    while(local >= 0 && prs->m_currfunccompiler->locals[local].depth >= depth)
    {
        if(prs->m_currfunccompiler->locals[local].iscaptured)
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_UPVALUECLOSE);
        }
        else
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
        }
        local--;
    }
    return prs->m_currfunccompiler->localcount - local - 1;
}

void nn_astparser_endloop(neon::AstParser* prs)
{
    int i;
    neon::Instruction* bcode;
    neon::Value* cvals;
    NEON_ASTDEBUG(prs->m_pvm, "");
    /*
    // find all neon::Instruction::OP_BREAK_PL placeholder and replace with the appropriate jump...
    */
    i = prs->m_innermostloopstart;
    while(i < prs->m_currfunccompiler->targetfunc->blob->m_count)
    {
        if(prs->m_currfunccompiler->targetfunc->blob->m_instrucs[i].code == neon::Instruction::OP_BREAK_PL)
        {
            prs->m_currfunccompiler->targetfunc->blob->m_instrucs[i].code = neon::Instruction::OP_JUMPNOW;
            nn_astemit_patchjump(prs, i + 1);
            i += 3;
        }
        else
        {
            bcode = prs->m_currfunccompiler->targetfunc->blob->m_instrucs;
            cvals = prs->m_currfunccompiler->targetfunc->blob->m_constants->m_values;
            i += 1 + nn_astparser_getcodeargscount(bcode, cvals, i);
        }
    }
}

bool nn_astparser_rulebinary(neon::AstParser* prs, neon::AstToken previous, bool canassign)
{
    neon::AstToken::Type op;
    neon::AstParser::Rule* rule;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    op = prs->m_prevtoken.type;
    /* compile the right operand */
    rule = nn_astparser_getrule(op);
    nn_astparser_parseprecedence(prs, (neon::AstParser::Rule::Precedence)(rule->precedence + 1));
    /* emit the operator instruction */
    switch(op)
    {
        case neon::AstToken::TOK_PLUS:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMADD);
            break;
        case neon::AstToken::TOK_MINUS:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMSUBTRACT);
            break;
        case neon::AstToken::TOK_MULTIPLY:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMMULTIPLY);
            break;
        case neon::AstToken::TOK_DIVIDE:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMDIVIDE);
            break;
        case neon::AstToken::TOK_MODULO:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMMODULO);
            break;
        case neon::AstToken::TOK_POWEROF:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMPOW);
            break;
        case neon::AstToken::TOK_FLOOR:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMFLOORDIVIDE);
            break;
            /* equality */
        case neon::AstToken::TOK_EQUAL:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_EQUAL);
            break;
        case neon::AstToken::TOK_NOTEQUAL:
            nn_astemit_emit2byte(prs, neon::Instruction::OP_EQUAL, neon::Instruction::OP_PRIMNOT);
            break;
        case neon::AstToken::TOK_GREATERTHAN:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMGREATER);
            break;
        case neon::AstToken::TOK_GREATER_EQ:
            nn_astemit_emit2byte(prs, neon::Instruction::OP_PRIMLESSTHAN, neon::Instruction::OP_PRIMNOT);
            break;
        case neon::AstToken::TOK_LESSTHAN:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMLESSTHAN);
            break;
        case neon::AstToken::TOK_LESSEQUAL:
            nn_astemit_emit2byte(prs, neon::Instruction::OP_PRIMGREATER, neon::Instruction::OP_PRIMNOT);
            break;
            /* bitwise */
        case neon::AstToken::TOK_AMP:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMAND);
            break;
        case neon::AstToken::TOK_BAR:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMOR);
            break;
        case neon::AstToken::TOK_XOR:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMBITXOR);
            break;
        case neon::AstToken::TOK_LEFTSHIFT:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMSHIFTLEFT);
            break;
        case neon::AstToken::TOK_RIGHTSHIFT:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMSHIFTRIGHT);
            break;
            /* range */
        case neon::AstToken::TOK_DOUBLEDOT:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_MAKERANGE);
            break;
        default:
            break;
    }
    return true;
}

bool nn_astparser_rulecall(neon::AstParser* prs, neon::AstToken previous, bool canassign)
{
    uint8_t argcount;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    argcount = nn_astparser_parsefunccallargs(prs);
    nn_astemit_emit2byte(prs, neon::Instruction::OP_CALLFUNCTION, argcount);
    return true;
}

bool nn_astparser_ruleliteral(neon::AstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    switch(prs->m_prevtoken.type)
    {
        case neon::AstToken::TOK_KWNULL:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHNULL);
            break;
        case neon::AstToken::TOK_KWTRUE:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHTRUE);
            break;
        case neon::AstToken::TOK_KWFALSE:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHFALSE);
            break;
        default:
            /* TODO: assuming this is correct behaviour ... */
            return false;
    }
    return true;
}

void nn_astparser_parseassign(neon::AstParser* prs, uint8_t realop, uint8_t getop, uint8_t setop, int arg)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    prs->m_replcanecho = false;
    if(getop == neon::Instruction::OP_PROPERTYGET || getop == neon::Instruction::OP_PROPERTYGETSELF)
    {
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_DUPONE);
    }
    if(arg != -1)
    {
        nn_astemit_emitbyteandshort(prs, getop, arg);
    }
    else
    {
        nn_astemit_emit2byte(prs, getop, 1);
    }
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, realop);
    if(arg != -1)
    {
        nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
    }
    else
    {
        nn_astemit_emitinstruc(prs, setop);
    }
}

void nn_astparser_assignment(neon::AstParser* prs, uint8_t getop, uint8_t setop, int arg, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_ASSIGN))
    {
        prs->m_replcanecho = false;
        nn_astparser_parseexpression(prs);
        if(arg != -1)
        {
            nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
        }
        else
        {
            nn_astemit_emitinstruc(prs, setop);
        }
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_PLUSASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMADD, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_MINUSASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMSUBTRACT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_MULTASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMMULTIPLY, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_DIVASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMDIVIDE, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_POWASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMPOW, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_PERCENT_EQ))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMMODULO, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_AMP_EQ))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMAND, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_BAR_EQ))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMOR, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_TILDE_EQ))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMBITNOT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_XOR_EQ))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMBITXOR, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_LEFTSHIFTASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMSHIFTLEFT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_RIGHTSHIFTASSIGN))
    {
        nn_astparser_parseassign(prs, neon::Instruction::OP_PRIMSHIFTRIGHT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_INCREMENT))
    {
        prs->m_replcanecho = false;
        if(getop == neon::Instruction::OP_PROPERTYGET || getop == neon::Instruction::OP_PROPERTYGETSELF)
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_DUPONE);
        }

        if(arg != -1)
        {
            nn_astemit_emitbyteandshort(prs, getop, arg);
        }
        else
        {
            nn_astemit_emit2byte(prs, getop, 1);
        }

        nn_astemit_emit2byte(prs, neon::Instruction::OP_PUSHONE, neon::Instruction::OP_PRIMADD);
        nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_DECREMENT))
    {
        prs->m_replcanecho = false;
        if(getop == neon::Instruction::OP_PROPERTYGET || getop == neon::Instruction::OP_PROPERTYGETSELF)
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_DUPONE);
        }

        if(arg != -1)
        {
            nn_astemit_emitbyteandshort(prs, getop, arg);
        }
        else
        {
            nn_astemit_emit2byte(prs, getop, 1);
        }

        nn_astemit_emit2byte(prs, neon::Instruction::OP_PUSHONE, neon::Instruction::OP_PRIMSUBTRACT);
        nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
    }
    else
    {
        if(arg != -1)
        {
            if(getop == neon::Instruction::OP_INDEXGET || getop == neon::Instruction::OP_INDEXGETRANGED)
            {
                nn_astemit_emit2byte(prs, getop, (uint8_t)0);
            }
            else
            {
                nn_astemit_emitbyteandshort(prs, getop, (uint16_t)arg);
            }
        }
        else
        {
            nn_astemit_emit2byte(prs, getop, (uint8_t)0);
        }
    }
}

bool nn_astparser_ruledot(neon::AstParser* prs, neon::AstToken previous, bool canassign)
{
    int name;
    bool caninvoke;
    uint8_t argcount;
    neon::Instruction::OpCode getop;
    neon::Instruction::OpCode setop;
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, "expected property name after '.'"))
    {
        return false;
    }
    name = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
    if(nn_astparser_match(prs, neon::AstToken::TOK_PARENOPEN))
    {
        argcount = nn_astparser_parsefunccallargs(prs);
        caninvoke = (
            (prs->m_currclasscompiler != nullptr) &&
            (
                (previous.type == neon::AstToken::TOK_KWTHIS) ||
                (nn_astparser_identsequal(&prs->m_prevtoken, &prs->m_currclasscompiler->name))
            )
        );
        if(caninvoke)
        {
            nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_CLASSINVOKETHIS, name);
        }
        else
        {
            nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_CALLMETHOD, name);
        }
        nn_astemit_emit1byte(prs, argcount);
    }
    else
    {
        getop = neon::Instruction::OP_PROPERTYGET;
        setop = neon::Instruction::OP_PROPERTYSET;
        if(prs->m_currclasscompiler != nullptr && (previous.type == neon::AstToken::TOK_KWTHIS || nn_astparser_identsequal(&prs->m_prevtoken, &prs->m_currclasscompiler->name)))
        {
            getop = neon::Instruction::OP_PROPERTYGETSELF;
        }
        nn_astparser_assignment(prs, getop, setop, name, canassign);
    }
    return true;
}

void nn_astparser_namedvar(neon::AstParser* prs, neon::AstToken name, bool canassign)
{
    bool fromclass;
    uint8_t getop;
    uint8_t setop;
    int arg;
    (void)fromclass;
    NEON_ASTDEBUG(prs->m_pvm, " name=%.*s", name.length, name.start);
    fromclass = prs->m_currclasscompiler != nullptr;
    arg = nn_astfunccompiler_resolvelocal(prs, prs->m_currfunccompiler, &name);
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
        arg = nn_astfunccompiler_resolveupvalue(prs, prs->m_currfunccompiler, &name);
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

void nn_astparser_createdvar(neon::AstParser* prs, neon::AstToken name)
{
    int local;
    NEON_ASTDEBUG(prs->m_pvm, "name=%.*s", name.length, name.start);
    if(prs->m_currfunccompiler->targetfunc->name != nullptr)
    {
        local = nn_astparser_addlocal(prs, name) - 1;
        nn_astparser_markinitialized(prs);
        nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_LOCALSET, (uint16_t)local);
    }
    else
    {
        nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_GLOBALDEFINE, (uint16_t)nn_astparser_makeidentconst(prs, &name));
    }
}

bool nn_astparser_rulearray(neon::AstParser* prs, bool canassign)
{
    int count;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    /* placeholder for the list */
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHNULL);
    count = 0;
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_check(prs, neon::AstToken::TOK_BRACKETCLOSE))
    {
        do
        {
            nn_astparser_ignorewhitespace(prs);
            if(!nn_astparser_check(prs, neon::AstToken::TOK_BRACKETCLOSE))
            {
                /* allow comma to end lists */
                nn_astparser_parseexpression(prs);
                nn_astparser_ignorewhitespace(prs);
                count++;
            }
            nn_astparser_ignorewhitespace(prs);
        } while(nn_astparser_match(prs, neon::AstToken::TOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, neon::AstToken::TOK_BRACKETCLOSE, "expected ']' at end of list");
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_MAKEARRAY, count);
    return true;
}

bool nn_astparser_ruledictionary(neon::AstParser* prs, bool canassign)
{
    bool usedexpression;
    int itemcount;
    neon::AstParser::CompContext oldctx;
    (void)canassign;
    (void)oldctx;
    NEON_ASTDEBUG(prs->m_pvm, "");
    /* placeholder for the dictionary */
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHNULL);
    itemcount = 0;
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_check(prs, neon::AstToken::TOK_BRACECLOSE))
    {
        do
        {
            nn_astparser_ignorewhitespace(prs);
            if(!nn_astparser_check(prs, neon::AstToken::TOK_BRACECLOSE))
            {
                /* allow last pair to end with a comma */
                usedexpression = false;
                if(nn_astparser_check(prs, neon::AstToken::TOK_IDENTNORMAL))
                {
                    nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, "");
                    nn_astemit_emitconst(prs, neon::Value::fromObject(neon::String::copy(prs->m_pvm, prs->m_prevtoken.start, prs->m_prevtoken.length)));
                }
                else
                {
                    nn_astparser_parseexpression(prs);
                    usedexpression = true;
                }
                nn_astparser_ignorewhitespace(prs);
                if(!nn_astparser_check(prs, neon::AstToken::TOK_COMMA) && !nn_astparser_check(prs, neon::AstToken::TOK_BRACECLOSE))
                {
                    nn_astparser_consume(prs, neon::AstToken::TOK_COLON, "expected ':' after dictionary key");
                    nn_astparser_ignorewhitespace(prs);

                    nn_astparser_parseexpression(prs);
                }
                else
                {
                    if(usedexpression)
                    {
                        nn_astparser_raiseerror(prs, "cannot infer dictionary values from expressions");
                        return false;
                    }
                    else
                    {
                        nn_astparser_namedvar(prs, prs->m_prevtoken, false);
                    }
                }
                itemcount++;
            }
        } while(nn_astparser_match(prs, neon::AstToken::TOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, neon::AstToken::TOK_BRACECLOSE, "expected '}' after dictionary");
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_MAKEDICT, itemcount);
    return true;
}

bool nn_astparser_ruleindexing(neon::AstParser* prs, neon::AstToken previous, bool canassign)
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
    if(nn_astparser_match(prs, neon::AstToken::TOK_COMMA))
    {
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHNULL);
        commamatch = true;
        getop = neon::Instruction::OP_INDEXGETRANGED;
    }
    else
    {
        nn_astparser_parseexpression(prs);
    }
    if(!nn_astparser_match(prs, neon::AstToken::TOK_BRACKETCLOSE))
    {
        getop = neon::Instruction::OP_INDEXGETRANGED;
        if(!commamatch)
        {
            nn_astparser_consume(prs, neon::AstToken::TOK_COMMA, "expecting ',' or ']'");
        }
        if(nn_astparser_match(prs, neon::AstToken::TOK_BRACKETCLOSE))
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHNULL);
        }
        else
        {
            nn_astparser_parseexpression(prs);
            nn_astparser_consume(prs, neon::AstToken::TOK_BRACKETCLOSE, "expected ']' after indexing");
        }
        assignable = false;
    }
    else
    {
        if(commamatch)
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHNULL);
        }
    }
    nn_astparser_assignment(prs, getop, neon::Instruction::OP_INDEXSET, -1, assignable);
    return true;
}

bool nn_astparser_rulevarnormal(neon::AstParser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_namedvar(prs, prs->m_prevtoken, canassign);
    return true;
}

bool nn_astparser_rulevarglobal(neon::AstParser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_namedvar(prs, prs->m_prevtoken, canassign);
    return true;
}

bool nn_astparser_rulethis(neon::AstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    #if 0
    if(prs->m_currclasscompiler == nullptr)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'this' outside of a class");
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

bool nn_astparser_rulesuper(neon::AstParser* prs, bool canassign)
{
    int name;
    bool invokeself;
    uint8_t argcount;
    NEON_ASTDEBUG(prs->m_pvm, "");
    (void)canassign;
    if(prs->m_currclasscompiler == nullptr)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'super' outside of a class");
        return false;
    }
    else if(!prs->m_currclasscompiler->hassuperclass)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'super' in a class without a superclass");
        return false;
    }
    name = -1;
    invokeself = false;
    if(!nn_astparser_check(prs, neon::AstToken::TOK_PARENOPEN))
    {
        nn_astparser_consume(prs, neon::AstToken::TOK_DOT, "expected '.' or '(' after super");
        nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, "expected super class method name after .");
        name = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
    }
    else
    {
        invokeself = true;
    }
    nn_astparser_namedvar(prs, nn_astparser_synthtoken(neon::g_strthis), false);
    if(nn_astparser_match(prs, neon::AstToken::TOK_PARENOPEN))
    {
        argcount = nn_astparser_parsefunccallargs(prs);
        nn_astparser_namedvar(prs, nn_astparser_synthtoken(neon::g_strsuper), false);
        if(!invokeself)
        {
            nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_CLASSINVOKESUPER, name);
            nn_astemit_emit1byte(prs, argcount);
        }
        else
        {
            nn_astemit_emit2byte(prs, neon::Instruction::OP_CLASSINVOKESUPERSELF, argcount);
        }
    }
    else
    {
        nn_astparser_namedvar(prs, nn_astparser_synthtoken(neon::g_strsuper), false);
        nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_CLASSGETSUPER, name);
    }
    return true;
}

bool nn_astparser_rulegrouping(neon::AstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_parseexpression(prs);
    while(nn_astparser_match(prs, neon::AstToken::TOK_COMMA))
    {
        nn_astparser_parseexpression(prs);
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENCLOSE, "expected ')' after grouped expression");
    return true;
}

neon::Value nn_astparser_compilenumber(neon::AstParser* prs)
{
    double dbval;
    long longval;
    long long llval;
    NEON_ASTDEBUG(prs->m_pvm, "");
    if(prs->m_prevtoken.type == neon::AstToken::TOK_LITNUMBIN)
    {
        llval = strtoll(prs->m_prevtoken.start + 2, nullptr, 2);
        return neon::Value::makeNumber(llval);
    }
    else if(prs->m_prevtoken.type == neon::AstToken::TOK_LITNUMOCT)
    {
        longval = strtol(prs->m_prevtoken.start + 2, nullptr, 8);
        return neon::Value::makeNumber(longval);
    }
    else if(prs->m_prevtoken.type == neon::AstToken::TOK_LITNUMHEX)
    {
        longval = strtol(prs->m_prevtoken.start, nullptr, 16);
        return neon::Value::makeNumber(longval);
    }
    else
    {
        dbval = strtod(prs->m_prevtoken.start, nullptr);
        return neon::Value::makeNumber(dbval);
    }
}

bool nn_astparser_rulenumber(neon::AstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astemit_emitconst(prs, nn_astparser_compilenumber(prs));
    return true;
}

/*
// Reads the next character, which should be a hex digit (0-9, a-f, or A-F) and
// returns its numeric value. If the character isn't a hex digit, returns -1.
*/
int nn_astparser_readhexdigit(char c)
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

/*
// Reads [digits] hex digits in a string literal and returns their number value.
*/
int nn_astparser_readhexescape(neon::AstParser* prs, const char* str, int index, int count)
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
        digit = nn_astparser_readhexdigit(cval);
        if(digit == -1)
        {
            nn_astparser_raiseerror(prs, "invalid hex escape sequence at #%d of \"%s\": '%c' (%d)", pos, str, cval, cval);
        }
        value = (value * 16) | digit;
    }
    if(count == 4 && (digit = nn_astparser_readhexdigit(str[index + i + 2])) != -1)
    {
        value = (value * 16) | digit;
    }
    return value;
}

int nn_astparser_readunicodeescape(neon::AstParser* prs, char* string, const char* realstring, int numberbytes, int realindex, int index)
{
    int value;
    int count;
    size_t len;
    char* chr;
    NEON_ASTDEBUG(prs->m_pvm, "");
    value = nn_astparser_readhexescape(prs, realstring, realindex, numberbytes);
    count = neon::Util::utf8NumBytes(value);
    if(count == -1)
    {
        nn_astparser_raiseerror(prs, "cannot encode a negative unicode value");
    }
    /* check for greater that \uffff */
    if(value > 65535)
    {
        count++;
    }
    if(count != 0)
    {
        chr = neon::Util::utf8Encode(prs->m_pvm, value, &len);
        if(chr)
        {
            memcpy(string + index, chr, (size_t)count + 1);
            free(chr);
        }
        else
        {
            nn_astparser_raiseerror(prs, "cannot decode unicode escape at index %d", realindex);
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

char* nn_astparser_compilestring(neon::AstParser* prs, int* length)
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
    rawlen = (((size_t)prs->m_prevtoken.length - 2) + 1);
    NEON_ASTDEBUG(prs->m_pvm, "raw length=%d", rawlen);
    deststr = (char*)neon::State::GC::allocate(prs->m_pvm, sizeof(char), rawlen);
    quote = prs->m_prevtoken.start[0];
    realstr = (char*)prs->m_prevtoken.start + 1;
    reallength = prs->m_prevtoken.length - 2;
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
                        //int nn_astparser_readunicodeescape(neon::AstParser* prs, char* string, char* realstring, int numberbytes, int realindex, int index)
                        //int nn_astparser_readhexescape(neon::AstParser* prs, const char* str, int index, int count)
                        //k += nn_astparser_readunicodeescape(prs, deststr, realstr, 2, i, k) - 1;
                        //k += nn_astparser_readhexescape(prs, deststr, i, 2) - 0;
                        c = nn_astparser_readhexescape(prs, realstr, i, 2) - 0;
                        i += 2;
                        //continue;
                    }
                    break;
                case 'u':
                    {
                        count = nn_astparser_readunicodeescape(prs, deststr, realstr, 4, i, k);
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
                        count = nn_astparser_readunicodeescape(prs, deststr, realstr, 8, i, k);
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

bool nn_astparser_rulestring(neon::AstParser* prs, bool canassign)
{
    int length;
    char* str;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "canassign=%d", canassign);
    str = nn_astparser_compilestring(prs, &length);
    nn_astemit_emitconst(prs, neon::Value::fromObject(neon::String::take(prs->m_pvm, str, length)));
    return true;
}

bool nn_astparser_ruleinterpolstring(neon::AstParser* prs, bool canassign)
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
                nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMADD);
            }
        }
        nn_astparser_parseexpression(prs);
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_STRINGIFY);
        if(doadd || (count >= 1 && stringmatched == false))
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMADD);
        }
        count++;
    } while(nn_astparser_match(prs, neon::AstToken::TOK_INTERPOLATION));
    nn_astparser_consume(prs, neon::AstToken::TOK_LITERAL, "unterminated string interpolation");
    if(prs->m_prevtoken.length - 2 > 0)
    {
        nn_astparser_rulestring(prs, canassign);
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMADD);
    }
    return true;
}

bool nn_astparser_ruleunary(neon::AstParser* prs, bool canassign)
{
    neon::AstToken::Type op;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    op = prs->m_prevtoken.type;
    /* compile the expression */
    nn_astparser_parseprecedence(prs, neon::AstParser::Rule::PREC_UNARY);
    /* emit instruction */
    switch(op)
    {
        case neon::AstToken::TOK_MINUS:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMNEGATE);
            break;
        case neon::AstToken::TOK_EXCLMARK:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMNOT);
            break;
        case neon::AstToken::TOK_TILDE:
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PRIMBITNOT);
            break;
        default:
            break;
    }
    return true;
}

bool nn_astparser_ruleand(neon::AstParser* prs, neon::AstToken previous, bool canassign)
{
    int endjump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    endjump = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    nn_astparser_parseprecedence(prs, neon::AstParser::Rule::PREC_AND);
    nn_astemit_patchjump(prs, endjump);
    return true;
}

bool nn_astparser_ruleor(neon::AstParser* prs, neon::AstToken previous, bool canassign)
{
    int endjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    elsejump = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPIFFALSE);
    endjump = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPNOW);
    nn_astemit_patchjump(prs, elsejump);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    nn_astparser_parseprecedence(prs, neon::AstParser::Rule::PREC_OR);
    nn_astemit_patchjump(prs, endjump);
    return true;
}

bool nn_astparser_ruleconditional(neon::AstParser* prs, neon::AstToken previous, bool canassign)
{
    int thenjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    thenjump = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    nn_astparser_ignorewhitespace(prs);
    /* compile the then expression */
    nn_astparser_parseprecedence(prs, neon::AstParser::Rule::PREC_CONDITIONAL);
    nn_astparser_ignorewhitespace(prs);
    elsejump = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPNOW);
    nn_astemit_patchjump(prs, thenjump);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    nn_astparser_consume(prs, neon::AstToken::TOK_COLON, "expected matching ':' after '?' conditional");
    nn_astparser_ignorewhitespace(prs);
    /*
    // compile the else expression
    // here we parse at neon::AstParser::Rule::PREC_ASSIGNMENT precedence as
    // linear conditionals can be nested.
    */
    nn_astparser_parseprecedence(prs, neon::AstParser::Rule::PREC_ASSIGNMENT);
    nn_astemit_patchjump(prs, elsejump);
    return true;
}

bool nn_astparser_ruleimport(neon::AstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_IMPORTIMPORT);
    return true;
}

bool nn_astparser_rulenew(neon::AstParser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, "class name after 'new'");
    return nn_astparser_rulevarnormal(prs, canassign);
    //return nn_astparser_rulecall(prs, prs->m_prevtoken, canassign);
}

bool nn_astparser_ruletypeof(neon::AstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENOPEN, "expected '(' after 'typeof'");
    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENCLOSE, "expected ')' after 'typeof'");
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_TYPEOF);
    return true;
}

bool nn_astparser_rulenothingprefix(neon::AstParser* prs, bool canassign)
{
    (void)prs;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    return true;
}

bool nn_astparser_rulenothinginfix(neon::AstParser* prs, neon::AstToken previous, bool canassign)
{
    (void)prs;
    (void)previous;
    (void)canassign;
    return true;
}

neon::AstParser::Rule* nn_astparser_putrule(neon::AstParser::Rule* dest, neon::AstParser::Rule::PrefixFN prefix, neon::AstParser::Rule::InfixFN infix, neon::AstParser::Rule::Precedence precedence)
{
    dest->prefix = prefix;
    dest->infix = infix;
    dest->precedence = precedence;
    return dest;
}

#define dorule(tok, prefix, infix, precedence) \
    case tok: return nn_astparser_putrule(&dest, prefix, infix, precedence);

neon::AstParser::Rule* nn_astparser_getrule(neon::AstToken::Type type)
{
    static neon::AstParser::Rule dest;
    switch(type)
    {
        dorule(neon::AstToken::TOK_NEWLINE, nn_astparser_rulenothingprefix, nn_astparser_rulenothinginfix, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_PARENOPEN, nn_astparser_rulegrouping, nn_astparser_rulecall, neon::AstParser::Rule::PREC_CALL );
        dorule(neon::AstToken::TOK_PARENCLOSE, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_BRACKETOPEN, nn_astparser_rulearray, nn_astparser_ruleindexing, neon::AstParser::Rule::PREC_CALL );
        dorule(neon::AstToken::TOK_BRACKETCLOSE, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_BRACEOPEN, nn_astparser_ruledictionary, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_BRACECLOSE, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_SEMICOLON, nn_astparser_rulenothingprefix, nn_astparser_rulenothinginfix, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_COMMA, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_BACKSLASH, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_EXCLMARK, nn_astparser_ruleunary, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_NOTEQUAL, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_EQUALITY );
        dorule(neon::AstToken::TOK_COLON, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_AT, nn_astparser_ruleanonfunc, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_DOT, nullptr, nn_astparser_ruledot, neon::AstParser::Rule::PREC_CALL );
        dorule(neon::AstToken::TOK_DOUBLEDOT, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_RANGE );
        dorule(neon::AstToken::TOK_TRIPLEDOT, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_PLUS, nn_astparser_ruleunary, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_TERM );
        dorule(neon::AstToken::TOK_PLUSASSIGN, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_INCREMENT, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_MINUS, nn_astparser_ruleunary, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_TERM );
        dorule(neon::AstToken::TOK_MINUSASSIGN, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_DECREMENT, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_MULTIPLY, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_FACTOR );
        dorule(neon::AstToken::TOK_MULTASSIGN, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_POWEROF, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_FACTOR );
        dorule(neon::AstToken::TOK_POWASSIGN, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_DIVIDE, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_FACTOR );
        dorule(neon::AstToken::TOK_DIVASSIGN, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_FLOOR, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_FACTOR );
        dorule(neon::AstToken::TOK_ASSIGN, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_EQUAL, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_EQUALITY );
        dorule(neon::AstToken::TOK_LESSTHAN, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_COMPARISON );
        dorule(neon::AstToken::TOK_LESSEQUAL, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_COMPARISON );
        dorule(neon::AstToken::TOK_LEFTSHIFT, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_SHIFT );
        dorule(neon::AstToken::TOK_LEFTSHIFTASSIGN, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_GREATERTHAN, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_COMPARISON );
        dorule(neon::AstToken::TOK_GREATER_EQ, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_COMPARISON );
        dorule(neon::AstToken::TOK_RIGHTSHIFT, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_SHIFT );
        dorule(neon::AstToken::TOK_RIGHTSHIFTASSIGN, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_MODULO, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_FACTOR );
        dorule(neon::AstToken::TOK_PERCENT_EQ, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_AMP, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_BITAND );
        dorule(neon::AstToken::TOK_AMP_EQ, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_BAR, /*nn_astparser_ruleanoncompat*/ nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_BITOR );
        dorule(neon::AstToken::TOK_BAR_EQ, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_TILDE, nn_astparser_ruleunary, nullptr, neon::AstParser::Rule::PREC_UNARY );
        dorule(neon::AstToken::TOK_TILDE_EQ, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_XOR, nullptr, nn_astparser_rulebinary, neon::AstParser::Rule::PREC_BITXOR );
        dorule(neon::AstToken::TOK_XOR_EQ, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_QUESTION, nullptr, nn_astparser_ruleconditional, neon::AstParser::Rule::PREC_CONDITIONAL );
        dorule(neon::AstToken::TOK_KWAND, nullptr, nn_astparser_ruleand, neon::AstParser::Rule::PREC_AND );
        dorule(neon::AstToken::TOK_KWAS, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWASSERT, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWBREAK, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWCLASS, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWCONTINUE, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWFUNCTION, nn_astparser_ruleanonfunc, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWDEFAULT, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWTHROW, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWDO, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWECHO, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWELSE, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWFALSE, nn_astparser_ruleliteral, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWFOREACH, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWIF, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWIMPORT, nn_astparser_ruleimport, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWIN, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWFOR, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWVAR, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWNULL, nn_astparser_ruleliteral, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWNEW, nn_astparser_rulenew, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWTYPEOF, nn_astparser_ruletypeof, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWOR, nullptr, nn_astparser_ruleor, neon::AstParser::Rule::PREC_OR );
        dorule(neon::AstToken::TOK_KWSUPER, nn_astparser_rulesuper, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWRETURN, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWTHIS, nn_astparser_rulethis, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWSTATIC, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWTRUE, nn_astparser_ruleliteral, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWSWITCH, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWCASE, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWWHILE, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWTRY, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWCATCH, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWFINALLY, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_LITERAL, nn_astparser_rulestring, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_LITNUMREG, nn_astparser_rulenumber, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_LITNUMBIN, nn_astparser_rulenumber, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_LITNUMOCT, nn_astparser_rulenumber, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_LITNUMHEX, nn_astparser_rulenumber, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_IDENTNORMAL, nn_astparser_rulevarnormal, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_INTERPOLATION, nn_astparser_ruleinterpolstring, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_EOF, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_ERROR, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWEMPTY, nn_astparser_ruleliteral, nullptr, neon::AstParser::Rule::PREC_NONE );
        dorule(neon::AstToken::TOK_UNDEFINED, nullptr, nullptr, neon::AstParser::Rule::PREC_NONE );
        default:
            fprintf(stderr, "missing rule?\n");
            break;
    }
    return nullptr;
}
#undef dorule

bool nn_astparser_doparseprecedence(neon::AstParser* prs, neon::AstParser::Rule::Precedence precedence/*, neon::AstExpression* dest*/)
{
    bool canassign;
    neon::AstToken previous;
    neon::AstParser::Rule::InfixFN infixrule;
    neon::AstParser::Rule::PrefixFN prefixrule;
    prefixrule = nn_astparser_getrule(prs->m_prevtoken.type)->prefix;
    if(prefixrule == nullptr)
    {
        nn_astparser_raiseerror(prs, "expected expression");
        return false;
    }
    canassign = precedence <= neon::AstParser::Rule::PREC_ASSIGNMENT;
    prefixrule(prs, canassign);
    while(precedence <= nn_astparser_getrule(prs->m_currtoken.type)->precedence)
    {
        previous = prs->m_prevtoken;
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_advance(prs);
        infixrule = nn_astparser_getrule(prs->m_prevtoken.type)->infix;
        infixrule(prs, previous, canassign);
    }
    if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_ASSIGN))
    {
        nn_astparser_raiseerror(prs, "invalid assignment target");
        return false;
    }
    return true;
}

bool nn_astparser_parseprecedence(neon::AstParser* prs, neon::AstParser::Rule::Precedence precedence)
{
    if(prs->m_lexer->isAtEnd() && prs->m_pvm->m_isreplmode)
    {
        return false;
    }
    nn_astparser_ignorewhitespace(prs);
    if(prs->m_lexer->isAtEnd() && prs->m_pvm->m_isreplmode)
    {
        return false;
    }
    nn_astparser_advance(prs);
    return nn_astparser_doparseprecedence(prs, precedence);
}

bool nn_astparser_parseprecnoadvance(neon::AstParser* prs, neon::AstParser::Rule::Precedence precedence)
{
    if(prs->m_lexer->isAtEnd() && prs->m_pvm->m_isreplmode)
    {
        return false;
    }
    nn_astparser_ignorewhitespace(prs);
    if(prs->m_lexer->isAtEnd() && prs->m_pvm->m_isreplmode)
    {
        return false;
    }
    return nn_astparser_doparseprecedence(prs, precedence);
}

bool nn_astparser_parseexpression(neon::AstParser* prs)
{
    return nn_astparser_parseprecedence(prs, neon::AstParser::Rule::PREC_ASSIGNMENT);
}

bool nn_astparser_parseblock(neon::AstParser* prs)
{
    prs->m_blockcount++;
    nn_astparser_ignorewhitespace(prs);
    while(!nn_astparser_check(prs, neon::AstToken::TOK_BRACECLOSE) && !nn_astparser_check(prs, neon::AstToken::TOK_EOF))
    {
        nn_astparser_parsedeclaration(prs);
    }
    prs->m_blockcount--;
    if(!nn_astparser_consume(prs, neon::AstToken::TOK_BRACECLOSE, "expected '}' after block"))
    {
        return false;
    }
    if(nn_astparser_match(prs, neon::AstToken::TOK_SEMICOLON))
    {
    }
    return true;
}

void nn_astparser_declarefuncargvar(neon::AstParser* prs)
{
    int i;
    neon::AstToken* name;
    neon::AstFuncCompiler::CompiledLocal* local;
    /* global variables are implicitly declared... */
    if(prs->m_currfunccompiler->scopedepth == 0)
    {
        return;
    }
    name = &prs->m_prevtoken;
    for(i = prs->m_currfunccompiler->localcount - 1; i >= 0; i--)
    {
        local = &prs->m_currfunccompiler->locals[i];
        if(local->depth != -1 && local->depth < prs->m_currfunccompiler->scopedepth)
        {
            break;
        }
        if(nn_astparser_identsequal(name, &local->name))
        {
            nn_astparser_raiseerror(prs, "%.*s already declared in current scope", name->length, name->start);
        }
    }
    nn_astparser_addlocal(prs, *name);
}


int nn_astparser_parsefuncparamvar(neon::AstParser* prs, const char* message)
{
    if(!nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, message))
    {
        /* what to do here? */
    }
    nn_astparser_declarefuncargvar(prs);
    /* we are in a local scope... */
    if(prs->m_currfunccompiler->scopedepth > 0)
    {
        return 0;
    }
    return nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
}

uint8_t nn_astparser_parsefunccallargs(neon::AstParser* prs)
{
    uint8_t argcount;
    argcount = 0;
    if(!nn_astparser_check(prs, neon::AstToken::TOK_PARENCLOSE))
    {
        do
        {
            nn_astparser_ignorewhitespace(prs);
            nn_astparser_parseexpression(prs);
            if(argcount == NEON_CFG_ASTMAXFUNCPARAMS)
            {
                nn_astparser_raiseerror(prs, "cannot have more than %d arguments to a function", NEON_CFG_ASTMAXFUNCPARAMS);
            }
            argcount++;
        } while(nn_astparser_match(prs, neon::AstToken::TOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_consume(prs, neon::AstToken::TOK_PARENCLOSE, "expected ')' after argument list"))
    {
        /* TODO: handle this, somehow. */
    }
    return argcount;
}

void nn_astparser_parsefuncparamlist(neon::AstParser* prs)
{
    int paramconstant;
    /* compile argument list... */
    do
    {
        nn_astparser_ignorewhitespace(prs);
        prs->m_currfunccompiler->targetfunc->arity++;
        if(prs->m_currfunccompiler->targetfunc->arity > NEON_CFG_ASTMAXFUNCPARAMS)
        {
            nn_astparser_raiseerroratcurrent(prs, "cannot have more than %d function parameters", NEON_CFG_ASTMAXFUNCPARAMS);
        }
        if(nn_astparser_match(prs, neon::AstToken::TOK_TRIPLEDOT))
        {
            prs->m_currfunccompiler->targetfunc->isvariadic = true;
            nn_astparser_addlocal(prs, nn_astparser_synthtoken("__args__"));
            nn_astparser_definevariable(prs, 0);
            break;
        }
        paramconstant = nn_astparser_parsefuncparamvar(prs, "expected parameter name");
        nn_astparser_definevariable(prs, paramconstant);
        nn_astparser_ignorewhitespace(prs);
    } while(nn_astparser_match(prs, neon::AstToken::TOK_COMMA));
}

void nn_astfunccompiler_compilebody(neon::AstParser* prs, neon::AstFuncCompiler* compiler, bool closescope, bool isanon)
{
    int i;
    neon::FuncScript* function;
    (void)isanon;
    /* compile the body */
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, neon::AstToken::TOK_BRACEOPEN, "expected '{' before function body");
    nn_astparser_parseblock(prs);
    /* create the function object */
    if(closescope)
    {
        nn_astparser_scopeend(prs);
    }
    function = nn_astparser_endcompiler(prs);
    prs->m_pvm->stackPush(neon::Value::fromObject(function));
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_MAKECLOSURE, nn_astparser_pushconst(prs, neon::Value::fromObject(function)));
    for(i = 0; i < function->upvalcount; i++)
    {
        nn_astemit_emit1byte(prs, compiler->upvalues[i].islocal ? 1 : 0);
        nn_astemit_emit1short(prs, compiler->upvalues[i].index);
    }
    prs->m_pvm->stackPop();
}

void nn_astparser_parsefuncfull(neon::AstParser* prs, neon::FuncCommon::Type type, bool isanon)
{
    neon::AstFuncCompiler compiler;
    prs->m_infunction = true;
    nn_astfunccompiler_init(prs, &compiler, type, isanon);
    nn_astparser_scopebegin(prs);
    /* compile parameter list */
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENOPEN, "expected '(' after function name");
    if(!nn_astparser_check(prs, neon::AstToken::TOK_PARENCLOSE))
    {
        nn_astparser_parsefuncparamlist(prs);
    }
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENCLOSE, "expected ')' after function parameters");
    nn_astfunccompiler_compilebody(prs, &compiler, false, isanon);
    prs->m_infunction = false;
}

void nn_astparser_parsemethod(neon::AstParser* prs, neon::AstToken classname, bool isstatic)
{
    size_t sn;
    int constant;
    const char* sc;
    neon::FuncCommon::Type type;
    static neon::AstToken::Type tkns[] = { neon::AstToken::TOK_IDENTNORMAL, neon::AstToken::TOK_DECORATOR };
    (void)classname;
    (void)isstatic;
    sc = "constructor";
    sn = strlen(sc);
    nn_astparser_consumeor(prs, "method name expected", tkns, 2);
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
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_MAKEMETHOD, constant);
}

bool nn_astparser_ruleanonfunc(neon::AstParser* prs, bool canassign)
{
    neon::AstFuncCompiler compiler;
    (void)canassign;
    nn_astfunccompiler_init(prs, &compiler, neon::FuncCommon::FUNCTYPE_FUNCTION, true);
    nn_astparser_scopebegin(prs);
    /* compile parameter list */
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENOPEN, "expected '(' at start of anonymous function");
    if(!nn_astparser_check(prs, neon::AstToken::TOK_PARENCLOSE))
    {
        nn_astparser_parsefuncparamlist(prs);
    }
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENCLOSE, "expected ')' after anonymous function parameters");
    nn_astfunccompiler_compilebody(prs, &compiler, true, true);
    return true;
}

void nn_astparser_parsefield(neon::AstParser* prs, bool isstatic)
{
    int fieldconstant;
    nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, "class property name expected");
    fieldconstant = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
    if(nn_astparser_match(prs, neon::AstToken::TOK_ASSIGN))
    {
        nn_astparser_parseexpression(prs);
    }
    else
    {
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHNULL);
    }
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_CLASSPROPERTYDEFINE, fieldconstant);
    nn_astemit_emit1byte(prs, isstatic ? 1 : 0);
    nn_astparser_consumestmtend(prs);
    nn_astparser_ignorewhitespace(prs);
}

void nn_astparser_parsefuncdecl(neon::AstParser* prs)
{
    int global;
    global = nn_astparser_parsevariable(prs, "function name expected");
    nn_astparser_markinitialized(prs);
    nn_astparser_parsefuncfull(prs, neon::FuncCommon::FUNCTYPE_FUNCTION, false);
    nn_astparser_definevariable(prs, global);
}

void nn_astparser_parseclassdeclaration(neon::AstParser* prs)
{
    bool isstatic;
    int nameconst;
    neon::AstParser::CompContext oldctx;
    neon::AstToken classname;
    neon::AstClassCompiler classcompiler;
    nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, "class name expected");
    nameconst = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
    classname = prs->m_prevtoken;
    nn_astparser_declarevariable(prs);
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_MAKECLASS, nameconst);
    nn_astparser_definevariable(prs, nameconst);
    classcompiler.name = prs->m_prevtoken;
    classcompiler.hassuperclass = false;
    classcompiler.enclosing = prs->m_currclasscompiler;
    prs->m_currclasscompiler = &classcompiler;
    oldctx = prs->m_compcontext;
    prs->m_compcontext = neon::AstParser::COMPCONTEXT_CLASS;
    if(nn_astparser_match(prs, neon::AstToken::TOK_LESSTHAN))
    {
        nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, "name of superclass expected");
        nn_astparser_rulevarnormal(prs, false);
        if(nn_astparser_identsequal(&classname, &prs->m_prevtoken))
        {
            nn_astparser_raiseerror(prs, "class %.*s cannot inherit from itself", classname.length, classname.start);
        }
        nn_astparser_scopebegin(prs);
        nn_astparser_addlocal(prs, nn_astparser_synthtoken(neon::g_strsuper));
        nn_astparser_definevariable(prs, 0);
        nn_astparser_namedvar(prs, classname, false);
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_CLASSINHERIT);
        classcompiler.hassuperclass = true;
    }
    nn_astparser_namedvar(prs, classname, false);
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, neon::AstToken::TOK_BRACEOPEN, "expected '{' before class body");
    nn_astparser_ignorewhitespace(prs);
    while(!nn_astparser_check(prs, neon::AstToken::TOK_BRACECLOSE) && !nn_astparser_check(prs, neon::AstToken::TOK_EOF))
    {
        isstatic = false;
        if(nn_astparser_match(prs, neon::AstToken::TOK_KWSTATIC))
        {
            isstatic = true;
        }

        if(nn_astparser_match(prs, neon::AstToken::TOK_KWVAR))
        {
            nn_astparser_parsefield(prs, isstatic);
        }
        else
        {
            nn_astparser_parsemethod(prs, classname, isstatic);
            nn_astparser_ignorewhitespace(prs);
        }
    }
    nn_astparser_consume(prs, neon::AstToken::TOK_BRACECLOSE, "expected '}' after class body");
    if(nn_astparser_match(prs, neon::AstToken::TOK_SEMICOLON))
    {
    }
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    if(classcompiler.hassuperclass)
    {
        nn_astparser_scopeend(prs);
    }
    prs->m_currclasscompiler = prs->m_currclasscompiler->enclosing;
    prs->m_compcontext = oldctx;
}

void nn_astparser_parsevardecl(neon::AstParser* prs, bool isinitializer)
{
    int global;
    int totalparsed;
    totalparsed = 0;
    do
    {
        if(totalparsed > 0)
        {
            nn_astparser_ignorewhitespace(prs);
        }
        global = nn_astparser_parsevariable(prs, "variable name expected");
        if(nn_astparser_match(prs, neon::AstToken::TOK_ASSIGN))
        {
            nn_astparser_parseexpression(prs);
        }
        else
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHNULL);
        }
        nn_astparser_definevariable(prs, global);
        totalparsed++;
    } while(nn_astparser_match(prs, neon::AstToken::TOK_COMMA));

    if(!isinitializer)
    {
        nn_astparser_consumestmtend(prs);
    }
    else
    {
        nn_astparser_consume(prs, neon::AstToken::TOK_SEMICOLON, "expected ';' after initializer");
        nn_astparser_ignorewhitespace(prs);
    }
}

void nn_astparser_parseexprstmt(neon::AstParser* prs, bool isinitializer, bool semi)
{
    if(prs->m_pvm->m_isreplmode && prs->m_currfunccompiler->scopedepth == 0)
    {
        prs->m_replcanecho = true;
    }
    if(!semi)
    {
        nn_astparser_parseexpression(prs);
    }
    else
    {
        nn_astparser_parseprecnoadvance(prs, neon::AstParser::Rule::PREC_ASSIGNMENT);
    }
    if(!isinitializer)
    {
        if(prs->m_replcanecho && prs->m_pvm->m_isreplmode)
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_ECHO);
            prs->m_replcanecho = false;
        }
        else
        {
            //if(!prs->m_keeplastvalue)
            {
                nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
            }
        }
        nn_astparser_consumestmtend(prs);
    }
    else
    {
        nn_astparser_consume(prs, neon::AstToken::TOK_SEMICOLON, "expected ';' after initializer");
        nn_astparser_ignorewhitespace(prs);
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
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
void nn_astparser_parseforstmt(neon::AstParser* prs)
{
    int exitjump;
    int bodyjump;
    int incrstart;
    int surroundingloopstart;
    int surroundingscopedepth;
    nn_astparser_scopebegin(prs);
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENOPEN, "expected '(' after 'for'");
    /* parse initializer... */
    if(nn_astparser_match(prs, neon::AstToken::TOK_SEMICOLON))
    {
        /* no initializer */
    }
    else if(nn_astparser_match(prs, neon::AstToken::TOK_KWVAR))
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
    prs->m_innermostloopstart = nn_astparser_currentblob(prs)->m_count;
    prs->m_innermostloopscopedepth = prs->m_currfunccompiler->scopedepth;
    exitjump = -1;
    if(!nn_astparser_match(prs, neon::AstToken::TOK_SEMICOLON))
    {
        /* the condition is optional */
        nn_astparser_parseexpression(prs);
        nn_astparser_consume(prs, neon::AstToken::TOK_SEMICOLON, "expected ';' after condition");
        nn_astparser_ignorewhitespace(prs);
        /* jump out of the loop if the condition is false... */
        exitjump = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPIFFALSE);
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
        /* pop the condition */
    }
    /* the iterator... */
    if(!nn_astparser_check(prs, neon::AstToken::TOK_BRACEOPEN))
    {
        bodyjump = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPNOW);
        incrstart = nn_astparser_currentblob(prs)->m_count;
        nn_astparser_parseexpression(prs);
        nn_astparser_ignorewhitespace(prs);
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
        nn_astemit_emitloop(prs, prs->m_innermostloopstart);
        prs->m_innermostloopstart = incrstart;
        nn_astemit_patchjump(prs, bodyjump);
    }
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENCLOSE, "expected ')' after 'for'");
    nn_astparser_parsestmt(prs);
    nn_astemit_emitloop(prs, prs->m_innermostloopstart);
    if(exitjump != -1)
    {
        nn_astemit_patchjump(prs, exitjump);
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
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
void nn_astparser_parseforeachstmt(neon::AstParser* prs)
{
    int citer;
    int citern;
    int falsejump;
    int keyslot;
    int valueslot;
    int iteratorslot;
    int surroundingloopstart;
    int surroundingscopedepth;
    neon::AstToken iteratortoken;
    neon::AstToken keytoken;
    neon::AstToken valuetoken;
    nn_astparser_scopebegin(prs);
    /* define @iter and @itern constant */
    citer = nn_astparser_pushconst(prs, neon::Value::fromObject(neon::String::copy(prs->m_pvm, "@iter")));
    citern = nn_astparser_pushconst(prs, neon::Value::fromObject(neon::String::copy(prs->m_pvm, "@itern")));
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENOPEN, "expected '(' after 'foreach'");
    nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, "expected variable name after 'foreach'");
    if(!nn_astparser_check(prs, neon::AstToken::TOK_COMMA))
    {
        keytoken = nn_astparser_synthtoken(" _ ");
        valuetoken = prs->m_prevtoken;
    }
    else
    {
        keytoken = prs->m_prevtoken;
        nn_astparser_consume(prs, neon::AstToken::TOK_COMMA, "");
        nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, "expected variable name after ','");
        valuetoken = prs->m_prevtoken;
    }
    nn_astparser_consume(prs, neon::AstToken::TOK_KWIN, "expected 'in' after for loop variable(s)");
    nn_astparser_ignorewhitespace(prs);
    /*
    // The space in the variable name ensures it won't collide with a user-defined
    // variable.
    */
    iteratortoken = nn_astparser_synthtoken(" iterator ");
    /* Evaluate the sequence expression and store it in a hidden local variable. */
    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENCLOSE, "expected ')' after 'foreach'");
    if(prs->m_currfunccompiler->localcount + 3 > NEON_CFG_ASTMAXLOCALS)
    {
        nn_astparser_raiseerror(prs, "cannot declare more than %d variables in one scope", NEON_CFG_ASTMAXLOCALS);
        return;
    }
    /* add the iterator to the local scope */
    iteratorslot = nn_astparser_addlocal(prs, iteratortoken) - 1;
    nn_astparser_definevariable(prs, 0);
    /* Create the key local variable. */
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHNULL);
    keyslot = nn_astparser_addlocal(prs, keytoken) - 1;
    nn_astparser_definevariable(prs, keyslot);
    /* create the local value slot */
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHNULL);
    valueslot = nn_astparser_addlocal(prs, valuetoken) - 1;
    nn_astparser_definevariable(prs, 0);
    surroundingloopstart = prs->m_innermostloopstart;
    surroundingscopedepth = prs->m_innermostloopscopedepth;
    /*
    // we'll be jumping back to right before the
    // expression after the loop body
    */
    prs->m_innermostloopstart = nn_astparser_currentblob(prs)->m_count;
    prs->m_innermostloopscopedepth = prs->m_currfunccompiler->scopedepth;
    /* key = iterable.iter_n__(key) */
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_LOCALGET, iteratorslot);
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_LOCALGET, keyslot);
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_CALLMETHOD, citern);
    nn_astemit_emit1byte(prs, 1);
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_LOCALSET, keyslot);
    falsejump = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    /* value = iterable.iter__(key) */
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_LOCALGET, iteratorslot);
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_LOCALGET, keyslot);
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_CALLMETHOD, citer);
    nn_astemit_emit1byte(prs, 1);
    /*
    // Bind the loop value in its own scope. This ensures we get a fresh
    // variable each iteration so that closures for it don't all see the same one.
    */
    nn_astparser_scopebegin(prs);
    /* update the value */
    nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_LOCALSET, valueslot);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    nn_astparser_parsestmt(prs);
    nn_astparser_scopeend(prs);
    nn_astemit_emitloop(prs, prs->m_innermostloopstart);
    nn_astemit_patchjump(prs, falsejump);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
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
void nn_astparser_parseswitchstmt(neon::AstParser* prs)
{
    int i;
    int length;
    int swstate;
    int casecount;
    int switchcode;
    int startoffset;
    int caseends[NEON_CFG_ASTMAXSWITCHCASES];
    char* str;
    neon::Value jump;
    neon::AstToken::Type casetype;
    neon::VarSwitch* sw;
    neon::String* string;
    /* the expression */
    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, neon::AstToken::TOK_BRACEOPEN, "expected '{' after 'switch' expression");
    nn_astparser_ignorewhitespace(prs);
    /* 0: before all cases, 1: before default, 2: after default */
    swstate = 0;
    casecount = 0;
    sw = neon::VarSwitch::make(prs->m_pvm);
    prs->m_pvm->stackPush(neon::Value::fromObject(sw));
    switchcode = nn_astemit_emitswitch(prs);
    /* nn_astemit_emitbyteandshort(prs, neon::Instruction::OP_SWITCH, nn_astparser_pushconst(prs, neon::Value::fromObject(sw))); */
    startoffset = nn_astparser_currentblob(prs)->m_count;
    while(!nn_astparser_match(prs, neon::AstToken::TOK_BRACECLOSE) && !nn_astparser_check(prs, neon::AstToken::TOK_EOF))
    {
        if(nn_astparser_match(prs, neon::AstToken::TOK_KWCASE) || nn_astparser_match(prs, neon::AstToken::TOK_KWDEFAULT))
        {
            casetype = prs->m_prevtoken.type;
            if(swstate == 2)
            {
                nn_astparser_raiseerror(prs, "cannot have another case after a default case");
            }
            if(swstate == 1)
            {
                /* at the end of the previous case, jump over the others... */
                caseends[casecount++] = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPNOW);
            }
            if(casetype == neon::AstToken::TOK_KWCASE)
            {
                swstate = 1;
                do
                {
                    nn_astparser_ignorewhitespace(prs);
                    nn_astparser_advance(prs);
                    jump = neon::Value::makeNumber((double)nn_astparser_currentblob(prs)->m_count - (double)startoffset);
                    if(prs->m_prevtoken.type == neon::AstToken::TOK_KWTRUE)
                    {
                        sw->m_jumppositions->set(neon::Value::makeBool(true), jump);
                    }
                    else if(prs->m_prevtoken.type == neon::AstToken::TOK_KWFALSE)
                    {
                        sw->m_jumppositions->set(neon::Value::makeBool(false), jump);
                    }
                    else if(prs->m_prevtoken.type == neon::AstToken::TOK_LITERAL)
                    {
                        str = nn_astparser_compilestring(prs, &length);
                        string = neon::String::take(prs->m_pvm, str, length);
                        /* gc fix */
                        prs->m_pvm->stackPush(neon::Value::fromObject(string));
                        sw->m_jumppositions->set(neon::Value::fromObject(string), jump);
                        /* gc fix */
                        prs->m_pvm->stackPop();
                    }
                    else if(nn_astparser_checknumber(prs))
                    {
                        sw->m_jumppositions->set(nn_astparser_compilenumber(prs), jump);
                    }
                    else
                    {
                        /* pop the switch */
                        prs->m_pvm->stackPop();
                        nn_astparser_raiseerror(prs, "only constants can be used in 'when' expressions");
                        return;
                    }
                } while(nn_astparser_match(prs, neon::AstToken::TOK_COMMA));
            }
            else
            {
                swstate = 2;
                sw->m_defaultjump = nn_astparser_currentblob(prs)->m_count - startoffset;
            }
        }
        else
        {
            /* otherwise, it's a statement inside the current case */
            if(swstate == 0)
            {
                nn_astparser_raiseerror(prs, "cannot have statements before any case");
            }
            nn_astparser_parsestmt(prs);
        }
    }
    /* if we ended without a default case, patch its condition jump */
    if(swstate == 1)
    {
        caseends[casecount++] = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPNOW);
    }
    /* patch all the case jumps to the end */
    for(i = 0; i < casecount; i++)
    {
        nn_astemit_patchjump(prs, caseends[i]);
    }
    sw->m_exitjump = nn_astparser_currentblob(prs)->m_count - startoffset;
    nn_astemit_patchswitch(prs, switchcode, nn_astparser_pushconst(prs, neon::Value::fromObject(sw)));
    /* pop the switch */
    prs->m_pvm->stackPop();
}

void nn_astparser_parseifstmt(neon::AstParser* prs)
{
    int elsejump;
    int thenjump;
    nn_astparser_parseexpression(prs);
    thenjump = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    nn_astparser_parsestmt(prs);
    elsejump = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPNOW);
    nn_astemit_patchjump(prs, thenjump);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    if(nn_astparser_match(prs, neon::AstToken::TOK_KWELSE))
    {
        nn_astparser_parsestmt(prs);
    }
    nn_astemit_patchjump(prs, elsejump);
}

void nn_astparser_parseechostmt(neon::AstParser* prs)
{
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_ECHO);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parsethrowstmt(neon::AstParser* prs)
{
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_EXTHROW);
    nn_astparser_discardlocals(prs, prs->m_currfunccompiler->scopedepth - 1);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parseassertstmt(neon::AstParser* prs)
{
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENOPEN, "expected '(' after 'assert'");
    nn_astparser_parseexpression(prs);
    if(nn_astparser_match(prs, neon::AstToken::TOK_COMMA))
    {
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parseexpression(prs);
    }
    else
    {
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHNULL);
    }
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_ASSERT);
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENCLOSE, "expected ')' after 'assert'");
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parsetrystmt(neon::AstParser* prs)
{
    int address;
    int type;
    int finally;
    int trybegins;
    int exitjump;
    int continueexecutionaddress;
    bool catchexists;
    bool finalexists;
    if(prs->m_currfunccompiler->handlercount == NEON_CFG_MAXEXCEPTHANDLERS)
    {
        nn_astparser_raiseerror(prs, "maximum exception handler in scope exceeded");
    }
    prs->m_currfunccompiler->handlercount++;
    prs->m_istrying = true;
    nn_astparser_ignorewhitespace(prs);
    trybegins = nn_astemit_emittry(prs);
    /* compile the try body */
    nn_astparser_parsestmt(prs);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_EXPOPTRY);
    exitjump = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPNOW);
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
    if(nn_astparser_match(prs, neon::AstToken::TOK_KWCATCH))
    {
        catchexists = true;
        nn_astparser_scopebegin(prs);
        nn_astparser_consume(prs, neon::AstToken::TOK_PARENOPEN, "expected '(' after 'catch'");
        nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, "missing exception class name");
        type = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
        address = nn_astparser_currentblob(prs)->m_count;
        if(nn_astparser_match(prs, neon::AstToken::TOK_IDENTNORMAL))
        {
            nn_astparser_createdvar(prs, prs->m_prevtoken);
        }
        else
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
        }
          nn_astparser_consume(prs, neon::AstToken::TOK_PARENCLOSE, "expected ')' after 'catch'");
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_EXPOPTRY);
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parsestmt(prs);
        nn_astparser_scopeend(prs);
    }
    else
    {
        type = nn_astparser_pushconst(prs, neon::Value::fromObject(neon::String::copy(prs->m_pvm, "Exception")));
    }
    nn_astemit_patchjump(prs, exitjump);
    if(nn_astparser_match(prs, neon::AstToken::TOK_KWFINALLY))
    {
        finalexists = true;
        /*
        // if we arrived here from either the try or handler block,
        // we don't want to continue propagating the exception
        */
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_PUSHFALSE);
        finally = nn_astparser_currentblob(prs)->m_count;
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parsestmt(prs);
        continueexecutionaddress = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPIFFALSE);
        /* pop the bool off the stack */
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_EXPUBLISHTRY);
        nn_astemit_patchjump(prs, continueexecutionaddress);
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    }
    if(!finalexists && !catchexists)
    {
        nn_astparser_raiseerror(prs, "try block must contain at least one of catch or finally");
    }
    nn_astemit_patchtry(prs, trybegins, type, address, finally);
}

void nn_astparser_parsereturnstmt(neon::AstParser* prs)
{
    prs->m_isreturning = true;
    /*
    if(prs->m_currfunccompiler->type == neon::FuncCommon::FUNCTYPE_SCRIPT)
    {
        nn_astparser_raiseerror(prs, "cannot return from top-level code");
    }
    */
    if(nn_astparser_match(prs, neon::AstToken::TOK_SEMICOLON) || nn_astparser_match(prs, neon::AstToken::TOK_NEWLINE))
    {
        nn_astemit_emitreturn(prs);
    }
    else
    {
        if(prs->m_currfunccompiler->type == neon::FuncCommon::FUNCTYPE_INITIALIZER)
        {
            nn_astparser_raiseerror(prs, "cannot return value from constructor");
        }
        if(prs->m_istrying)
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_EXPOPTRY);
        }
        nn_astparser_parseexpression(prs);
        nn_astemit_emitinstruc(prs, neon::Instruction::OP_RETURN);
        nn_astparser_consumestmtend(prs);
    }
    prs->m_isreturning = false;
}

void nn_astparser_parsewhilestmt(neon::AstParser* prs)
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
    prs->m_innermostloopstart = nn_astparser_currentblob(prs)->m_count;
    prs->m_innermostloopscopedepth = prs->m_currfunccompiler->scopedepth;
    nn_astparser_parseexpression(prs);
    exitjump = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    nn_astparser_parsestmt(prs);
    nn_astemit_emitloop(prs, prs->m_innermostloopstart);
    nn_astemit_patchjump(prs, exitjump);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    nn_astparser_endloop(prs);
    prs->m_innermostloopstart = surroundingloopstart;
    prs->m_innermostloopscopedepth = surroundingscopedepth;
}

void nn_astparser_parsedo_whilestmt(neon::AstParser* prs)
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
    prs->m_innermostloopstart = nn_astparser_currentblob(prs)->m_count;
    prs->m_innermostloopscopedepth = prs->m_currfunccompiler->scopedepth;
    nn_astparser_parsestmt(prs);
    nn_astparser_consume(prs, neon::AstToken::TOK_KWWHILE, "expecting 'while' statement");
    nn_astparser_parseexpression(prs);
    exitjump = nn_astemit_emitjump(prs, neon::Instruction::OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    nn_astemit_emitloop(prs, prs->m_innermostloopstart);
    nn_astemit_patchjump(prs, exitjump);
    nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
    nn_astparser_endloop(prs);
    prs->m_innermostloopstart = surroundingloopstart;
    prs->m_innermostloopscopedepth = surroundingscopedepth;
}

void nn_astparser_parsecontinuestmt(neon::AstParser* prs)
{
    if(prs->m_innermostloopstart == -1)
    {
        nn_astparser_raiseerror(prs, "'continue' can only be used in a loop");
    }
    /*
    // discard local variables created in the loop
    //  discard_local(prs, prs->m_innermostloopscopedepth);
    */
    nn_astparser_discardlocals(prs, prs->m_innermostloopscopedepth + 1);
    /* go back to the top of the loop */
    nn_astemit_emitloop(prs, prs->m_innermostloopstart);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parsebreakstmt(neon::AstParser* prs)
{
    if(prs->m_innermostloopstart == -1)
    {
        nn_astparser_raiseerror(prs, "'break' can only be used in a loop");
    }
    /* discard local variables created in the loop */
    /*
    int i;
    for(i = prs->m_currfunccompiler->localcount - 1; i >= 0 && prs->m_currfunccompiler->locals[i].depth >= prs->m_currfunccompiler->scopedepth; i--)
    {
        if(prs->m_currfunccompiler->locals[i].iscaptured)
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_UPVALUECLOSE);
        }
        else
        {
            nn_astemit_emitinstruc(prs, neon::Instruction::OP_POPONE);
        }
    }
    */
    nn_astparser_discardlocals(prs, prs->m_innermostloopscopedepth + 1);
    nn_astemit_emitjump(prs, neon::Instruction::OP_BREAK_PL);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_synchronize(neon::AstParser* prs)
{
    prs->m_panicmode = false;
    while(prs->m_currtoken.type != neon::AstToken::TOK_EOF)
    {
        if(prs->m_currtoken.type == neon::AstToken::TOK_NEWLINE || prs->m_currtoken.type == neon::AstToken::TOK_SEMICOLON)
        {
            return;
        }
        switch(prs->m_currtoken.type)
        {
            case neon::AstToken::TOK_KWCLASS:
            case neon::AstToken::TOK_KWFUNCTION:
            case neon::AstToken::TOK_KWVAR:
            case neon::AstToken::TOK_KWFOREACH:
            case neon::AstToken::TOK_KWIF:
            case neon::AstToken::TOK_KWSWITCH:
            case neon::AstToken::TOK_KWCASE:
            case neon::AstToken::TOK_KWFOR:
            case neon::AstToken::TOK_KWDO:
            case neon::AstToken::TOK_KWWHILE:
            case neon::AstToken::TOK_KWECHO:
            case neon::AstToken::TOK_KWASSERT:
            case neon::AstToken::TOK_KWTRY:
            case neon::AstToken::TOK_KWCATCH:
            case neon::AstToken::TOK_KWTHROW:
            case neon::AstToken::TOK_KWRETURN:
            case neon::AstToken::TOK_KWSTATIC:
            case neon::AstToken::TOK_KWTHIS:
            case neon::AstToken::TOK_KWSUPER:
            case neon::AstToken::TOK_KWFINALLY:
            case neon::AstToken::TOK_KWIN:
            case neon::AstToken::TOK_KWIMPORT:
            case neon::AstToken::TOK_KWAS:
                return;
            default:
                /* do nothing */
            ;
        }
        nn_astparser_advance(prs);
    }
}

/*
* $keeplast: whether to emit code that retains or discards the value of the last statement/expression.
* SHOULD NOT BE USED FOR ORDINARY SCRIPTS as it will almost definitely result in the stack containing invalid values.
*/
neon::FuncScript* nn_astparser_compilesource(neon::State* state, neon::Module* module, const char* source, neon::Blob* blob, bool fromimport, bool keeplast)
{
    neon::AstFuncCompiler compiler;
    neon::AstLexer* lexer;
    neon::AstParser* parser;
    neon::FuncScript* function;
    (void)blob;
    NEON_ASTDEBUG(state, "module=%p source=[...] blob=[...] fromimport=%d keeplast=%d", module, fromimport, keeplast);
    lexer = new neon::AstLexer(state, source);
    parser = new neon::AstParser(state, lexer, module, keeplast);
    nn_astfunccompiler_init(parser, &compiler, neon::FuncCommon::FUNCTYPE_SCRIPT, true);
    compiler.fromimport = fromimport;
    nn_astparser_runparser(parser);
    function = nn_astparser_endcompiler(parser);
    if(parser->m_haderror)
    {
        function = nullptr;
    }
    delete lexer;
    delete parser;
    state->m_activeparser = nullptr;
    return function;
}

void nn_gcmem_markcompilerroots(neon::State* state)
{
    neon::AstFuncCompiler* compiler;
    if(state->m_activeparser != nullptr)
    {
        compiler = state->m_activeparser->m_currfunccompiler;
        while(compiler != nullptr)
        {
            nn_gcmem_markobject(state, (neon::Object*)compiler->targetfunc);
            compiler = compiler->enclosing;
        }
    }
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

    static neon::RegModule module;
    module.name = "null";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = nullptr;
    module.preloader= nullptr;
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
        nn_exceptions_throwException(state, "cannot open directory '%s'", dirn);
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
    static neon::RegModule module;
    module.name = "os";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = nullptr;
    module.preloader= &nn_modfn_os_preloader;
    module.unloader = nullptr;
    return &module;
}

neon::Value nn_modfn_astscan_scan(neon::State* state, neon::Arguments* args)
{
    const char* cstr;
    neon::String* insrc;
    neon::AstLexer* lex;
    neon::Array* arr;
    neon::Dictionary* itm;
    neon::AstToken token;
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    insrc = args->argv[0].asString();
    lex = new neon::AstLexer(state, insrc->data());
    arr = neon::Array::make(state);
    while(!lex->isAtEnd())
    {
        itm = neon::Dictionary::make(state);
        token = nn_astlex_scantoken(lex);
        itm->addEntryCStr("line", neon::Value::makeNumber(token.line));
        cstr = nn_astutil_toktype2str(token.type);
        itm->addEntryCStr("type", neon::Value::fromObject(neon::String::copy(state, cstr + 12)));
        itm->addEntryCStr("source", neon::Value::fromObject(neon::String::copy(state, token.start, token.length)));
        arr->push(neon::Value::fromObject(itm));
    }
    delete lex;
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
    static neon::RegModule module;
    module.name = "astscan";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = nullptr;
    module.preloader= nullptr;
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
    return neon::Value::makeNumber(nn_value_asdict(args->thisval)->m_keynames->m_count);
}

neon::Value nn_memberfunc_dict_add(neon::State* state, neon::Arguments* args)
{
    neon::Value tempvalue;
    neon::Dictionary* dict;
    NEON_ARGS_CHECKCOUNT(args, 2);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = nn_value_asdict(args->thisval);
    if(dict->m_valtable->get(args->argv[0], &tempvalue))
    {
        NEON_RETURNERROR("duplicate key %s at add()", neon::Value::toString(state, args->argv[0])->data());
    }
    dict->addEntry(args->argv[0], args->argv[1]);
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_dict_set(neon::State* state, neon::Arguments* args)
{
    neon::Value value;
    neon::Dictionary* dict;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 2);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = nn_value_asdict(args->thisval);
    if(!dict->m_valtable->get(args->argv[0], &value))
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
    dict = nn_value_asdict(args->thisval);
    delete dict->m_keynames;
    delete dict->m_valtable;
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_dict_clone(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::Dictionary* dict;
    neon::Dictionary* newdict;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = nn_value_asdict(args->thisval);
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
    int i;
    neon::Dictionary* dict;
    neon::Dictionary* newdict;
    neon::Value tmpvalue;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = nn_value_asdict(args->thisval);
    newdict = state->gcProtect(neon::Dictionary::make(state));
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        dict->m_valtable->get(dict->m_keynames->m_values[i], &tmpvalue);
        if(!nn_value_compare(state, tmpvalue, neon::Value::makeNull()))
        {
            newdict->addEntry(dict->m_keynames->m_values[i], tmpvalue);
        }
    }
    return neon::Value::fromObject(newdict);
}

neon::Value nn_memberfunc_dict_contains(neon::State* state, neon::Arguments* args)
{
    neon::Value value;
    (void)state;
    neon::Dictionary* dict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = nn_value_asdict(args->thisval);
    return neon::Value::makeBool(dict->m_valtable->get(args->argv[0], &value));
}

neon::Value nn_memberfunc_dict_extend(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::Value tmp;
    neon::Dictionary* dict;
    neon::Dictionary* dictcpy;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isDict);
    dict = nn_value_asdict(args->thisval);
    dictcpy = nn_value_asdict(args->argv[0]);
    for(i = 0; i < dictcpy->m_keynames->m_count; i++)
    {
        if(!dict->m_valtable->get(dictcpy->m_keynames->m_values[i], &tmp))
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
    dict = nn_value_asdict(args->thisval);
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
    return field->value;
}

neon::Value nn_memberfunc_dict_keys(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::Dictionary* dict;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = nn_value_asdict(args->thisval);
    list = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        list->push(dict->m_keynames->m_values[i]);
    }
    return neon::Value::fromObject(list);
}

neon::Value nn_memberfunc_dict_values(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::Dictionary* dict;
    neon::Array* list;
    neon::Property* field;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = nn_value_asdict(args->thisval);
    list = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < dict->m_keynames->m_count; i++)
    {
        field = dict->getEntry(dict->m_keynames->m_values[i]);
        list->push(field->value);
    }
    return neon::Value::fromObject(list);
}

neon::Value nn_memberfunc_dict_remove(neon::State* state, neon::Arguments* args)
{
    int i;
    int index;
    neon::Value value;
    neon::Dictionary* dict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = nn_value_asdict(args->thisval);
    if(dict->m_valtable->get(args->argv[0], &value))
    {
        dict->m_valtable->removeByKey(args->argv[0]);
        index = -1;
        for(i = 0; i < dict->m_keynames->m_count; i++)
        {
            if(nn_value_compare(state, dict->m_keynames->m_values[i], args->argv[0]))
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
        return value;
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_dict_isempty(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return neon::Value::makeBool(nn_value_asdict(args->thisval)->m_keynames->m_count == 0);
}

neon::Value nn_memberfunc_dict_findkey(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    return nn_value_asdict(args->thisval)->m_valtable->findKey(args->argv[0]);
}

neon::Value nn_memberfunc_dict_tolist(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::Array* list;
    neon::Dictionary* dict;
    neon::Array* namelist;
    neon::Array* valuelist;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = nn_value_asdict(args->thisval);
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
    dict = nn_value_asdict(args->thisval);
    if(dict->m_valtable->get(args->argv[0], &result))
    {
        return result;
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_dict_itern(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::Dictionary* dict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    dict = nn_value_asdict(args->thisval);
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
        if(nn_value_compare(state, args->argv[0], dict->m_keynames->m_values[i]) && (i + 1) < dict->m_keynames->m_count)
        {
            return dict->m_keynames->m_values[i + 1];
        }
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_dict_each(neon::State* state, neon::Arguments* args)
{
    int i;
    int arity;
    neon::Value value;
    neon::Value callable;
    neon::Value unused;
    neon::Dictionary* dict;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = nn_value_asdict(args->thisval);
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
    int i;
    int arity;
    neon::Value value;
    neon::Value callable;
    neon::Value result;
    neon::Dictionary* dict;
    neon::Array* nestargs;
    neon::Dictionary* resultdict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = nn_value_asdict(args->thisval);
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
        if(!nn_value_isfalse(result))
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
    int i;
    int arity;
    neon::Value result;
    neon::Value value;
    neon::Value callable;
    neon::Dictionary* dict;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = nn_value_asdict(args->thisval);
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
        if(!nn_value_isfalse(result))
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
    int i;
    int arity;
    neon::Value value;
    neon::Value callable;  
    neon::Value result;
    neon::Dictionary* dict;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = nn_value_asdict(args->thisval);
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
        if(nn_value_isfalse(result))
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
    int i;
    int arity;
    int startindex;
    neon::Value value;
    neon::Value callable;
    neon::Value accumulator;
    neon::Dictionary* dict;
    neon::Array* nestargs;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = nn_value_asdict(args->thisval);
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
    NEON_RETURNERROR(#type " -> %s", message, file->m_filepath->data());

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
    NEON_RETURNERROR("method not supported for std files");

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
        NEON_RETURNERROR("file path cannot be empty");
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
    nn_fileobject_close(nn_value_asfile(args->thisval));
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_file_open(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    nn_fileobject_open(nn_value_asfile(args->thisval));
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_file_isopen(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    (void)state;
    file = nn_value_asfile(args->thisval);
    return neon::Value::makeBool(file->m_isstd || file->m_isopen);
}

neon::Value nn_memberfunc_file_isclosed(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    (void)state;
    file = nn_value_asfile(args->thisval);
    return neon::Value::makeBool(!file->m_isstd && !file->m_isopen);
}

bool nn_file_read(neon::State* state, neon::File* file, size_t readhowmuch, neon::File::IOResult* dest)
{
    size_t filesizereal;
    struct stat stats;
    filesizereal = -1;
    dest->success = false;
    dest->length = 0;
    dest->data = nullptr;
    if(!file->m_isstd)
    {
        if(!nn_util_fsfileexists(state, file->m_filepath->data()))
        {
            return false;
        }
        /* file is in write only mode */
        /*
        else if(strstr(file->m_filemode->data(), "w") != nullptr && strstr(file->m_filemode->data(), "+") == nullptr)
        {
            FILE_ERROR(Unsupported, "cannot read file in write mode");
        }
        */
        if(!file->m_isopen)
        {
            /* open the file if it isn't open */
            nn_fileobject_open(file);
        }
        else if(file->m_filehandle == nullptr)
        {
            return false;
        }
        if(osfn_lstat(file->m_filepath->data(), &stats) == 0)
        {
            filesizereal = (size_t)stats.st_size;
        }
        else
        {
            /* fallback */
            fseek(file->m_filehandle, 0L, SEEK_END);
            filesizereal = ftell(file->m_filehandle);
            rewind(file->m_filehandle);
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
    dest->data = (char*)neon::State::GC::allocate(state, sizeof(char), readhowmuch + 1);
    if(dest->data == nullptr && readhowmuch != 0)
    {
        return false;
    }
    dest->length = fread(dest->data, sizeof(char), readhowmuch, file->m_filehandle);
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

neon::Value nn_memberfunc_file_read(neon::State* state, neon::Arguments* args)
{
    size_t readhowmuch;
    neon::File::IOResult res;
    neon::File* file;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    readhowmuch = -1;
    if(args->argc == 1)
    {
        NEON_ARGS_CHECKTYPE(args, 0, isNumber);
        readhowmuch = (size_t)args->argv[0].asNumber();
    }
    file = nn_value_asfile(args->thisval);
    if(!nn_file_read(state, file, readhowmuch, &res))
    {
        FILE_ERROR(NotFound, strerror(errno));
    }
    return neon::Value::fromObject(neon::String::take(state, res.data, res.length));
}

neon::Value nn_memberfunc_file_get(neon::State* state, neon::Arguments* args)
{
    int ch;
    neon::File* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
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
    file = nn_value_asfile(args->thisval);
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
    file = nn_value_asfile(args->thisval);
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
    file = nn_value_asfile(args->thisval);
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
    file = nn_value_asfile(args->thisval);
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
    return neon::Value::makeNumber(nn_value_asfile(args->thisval)->m_filedesc);
}

neon::Value nn_memberfunc_file_istty(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
    return neon::Value::makeBool(file->m_istty);
}

neon::Value nn_memberfunc_file_flush(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
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
    file = nn_value_asfile(args->thisval);
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
            NEON_RETURNERROR("cannot get stats for non-existing file");
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
    file = nn_value_asfile(args->thisval);
    DENY_STD();
    return neon::Value::fromObject(file->m_filepath);
}

neon::Value nn_memberfunc_file_mode(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
    return neon::Value::fromObject(file->m_filemode);
}

neon::Value nn_memberfunc_file_name(neon::State* state, neon::Arguments* args)
{
    char* name;
    neon::File* file;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
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
    file = nn_value_asfile(args->thisval);
    DENY_STD();
    position = (long)args->argv[0].asNumber();
    seektype = args->argv[1].asNumber();
    RETURN_STATUS(fseek(file->m_filehandle, position, seektype));
}

neon::Value nn_memberfunc_file_tell(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
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
    return neon::Value::makeNumber(selfarr->m_varray->m_count);
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
    return neon::Value::fromObject(list->copy(0, list->m_varray->m_count));
}

neon::Value nn_memberfunc_array_count(neon::State* state, neon::Arguments* args)
{
    int i;
    int count;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    count = 0;
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(nn_value_compare(state, list->m_varray->m_values[i], args->argv[0]))
        {
            count++;
        }
    }
    return neon::Value::makeNumber(count);
}

neon::Value nn_memberfunc_array_extend(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::Array* list;
    neon::Array* list2;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isArray);
    list = args->thisval.asArray();
    list2 = args->argv[0].asArray();
    for(i = 0; i < list2->m_varray->m_count; i++)
    {
        list->push(list2->m_varray->m_values[i]);
    }
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_array_indexof(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    list = args->thisval.asArray();
    i = 0;
    if(args->argc == 2)
    {
        NEON_ARGS_CHECKTYPE(args, 1, isNumber);
        i = args->argv[1].asNumber();
    }
    for(; i < list->m_varray->m_count; i++)
    {
        if(nn_value_compare(state, list->m_varray->m_values[i], args->argv[0]))
        {
            return neon::Value::makeNumber(i);
        }
    }
    return neon::Value::makeNumber(-1);
}

neon::Value nn_memberfunc_array_insert(neon::State* state, neon::Arguments* args)
{
    int index;
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 2);
    NEON_ARGS_CHECKTYPE(args, 1, isNumber);
    list = args->thisval.asArray();
    index = (int)args->argv[1].asNumber();
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
    if(list->m_varray->m_count > 0)
    {
        value = list->m_varray->m_values[list->m_varray->m_count - 1];
        list->m_varray->m_count--;
        return value;
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_array_shift(neon::State* state, neon::Arguments* args)
{
    int i;
    int j;
    int count;
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
    if(count >= list->m_varray->m_count || list->m_varray->m_count == 1)
    {
        list->m_varray->m_count = 0;
        return neon::Value::makeNull();
    }
    else if(count > 0)
    {
        newlist = state->gcProtect(neon::Array::make(state));
        for(i = 0; i < count; i++)
        {
            newlist->push(list->m_varray->m_values[0]);
            for(j = 0; j < list->m_varray->m_count; j++)
            {
                list->m_varray->m_values[j] = list->m_varray->m_values[j + 1];
            }
            list->m_varray->m_count -= 1;
        }
        if(count == 1)
        {
            return newlist->m_varray->m_values[0];
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
    int i;
    int index;
    neon::Value value;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    list = args->thisval.asArray();
    index = args->argv[0].asNumber();
    if(index < 0 || index >= list->m_varray->m_count)
    {
        NEON_RETURNERROR("list index %d out of range at remove_at()", index);
    }
    value = list->m_varray->m_values[index];
    for(i = index; i < list->m_varray->m_count - 1; i++)
    {
        list->m_varray->m_values[i] = list->m_varray->m_values[i + 1];
    }
    list->m_varray->m_count--;
    return value;
}

neon::Value nn_memberfunc_array_remove(neon::State* state, neon::Arguments* args)
{
    int i;
    int index;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    index = -1;
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(nn_value_compare(state, list->m_varray->m_values[i], args->argv[0]))
        {
            index = i;
            break;
        }
    }
    if(index != -1)
    {
        for(i = index; i < list->m_varray->m_count; i++)
        {
            list->m_varray->m_values[i] = list->m_varray->m_values[i + 1];
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
    int end = list->m_varray->m_count - 1;
    while(start < end)
    {
        neon::Value temp = list->m_varray->m_values[start];
        list->m_varray->m_values[start] = list->m_varray->m_values[end];
        list->m_varray->m_values[end] = temp;
        start++;
        end--;
    }
    */
    for(fromtop = list->m_varray->m_count - 1; fromtop >= 0; fromtop--)
    {
        nlist->push(list->m_varray->m_values[fromtop]);
    }
    return neon::Value::fromObject(nlist);
}

neon::Value nn_memberfunc_array_sort(neon::State* state, neon::Arguments* args)
{
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    nn_value_sortvalues(state, list->m_varray->m_values, list->m_varray->m_count);
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_array_contains(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(nn_value_compare(state, args->argv[0], list->m_varray->m_values[i]))
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
    if(idxlower < 0 || idxlower >= list->m_varray->m_count)
    {
        NEON_RETURNERROR("list index %d out of range at delete()", idxlower);
    }
    else if(idxupper < idxlower || idxupper >= list->m_varray->m_count)
    {
        NEON_RETURNERROR("invalid upper limit %d at delete()", idxupper);
    }
    for(i = 0; i < list->m_varray->m_count - idxupper; i++)
    {
        list->m_varray->m_values[idxlower + i] = list->m_varray->m_values[i + idxupper + 1];
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
    if(list->m_varray->m_count > 0)
    {
        return list->m_varray->m_values[0];
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_array_last(neon::State* state, neon::Arguments* args)
{
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    if(list->m_varray->m_count > 0)
    {
        return list->m_varray->m_values[list->m_varray->m_count - 1];
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_array_isempty(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return neon::Value::makeBool(args->thisval.asArray()->m_varray->m_count == 0);
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
        count = list->m_varray->m_count + count;
    }
    if(list->m_varray->m_count < count)
    {
        return neon::Value::fromObject(list->copy(0, list->m_varray->m_count));
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
    if(index < 0 || index >= list->m_varray->m_count)
    {
        return neon::Value::makeNull();
    }
    return list->m_varray->m_values[index];
}

neon::Value nn_memberfunc_array_compact(neon::State* state, neon::Arguments* args)
{
    int i;
    neon::Array* list;
    neon::Array* newlist;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    newlist = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(!nn_value_compare(state, list->m_varray->m_values[i], neon::Value::makeNull()))
        {
            newlist->push(list->m_varray->m_values[i]);
        }
    }
    return neon::Value::fromObject(newlist);
}


neon::Value nn_memberfunc_array_unique(neon::State* state, neon::Arguments* args)
{
    int i;
    int j;
    bool found;
    neon::Array* list;
    neon::Array* newlist;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    newlist = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        found = false;
        for(j = 0; j < newlist->m_varray->m_count; j++)
        {
            if(nn_value_compare(state, newlist->m_varray->m_values[j], list->m_varray->m_values[i]))
            {
                found = true;
                continue;
            }
        }
        if(!found)
        {
            newlist->push(list->m_varray->m_values[i]);
        }
    }
    return neon::Value::fromObject(newlist);
}

neon::Value nn_memberfunc_array_zip(neon::State* state, neon::Arguments* args)
{
    int i;
    int j;
    neon::Array* list;
    neon::Array* newlist;
    neon::Array* alist;
    neon::Array** arglist;
    list = args->thisval.asArray();
    newlist = state->gcProtect(neon::Array::make(state));
    arglist = (neon::Array**)neon::State::GC::allocate(state, sizeof(neon::Array*), args->argc);
    for(i = 0; i < args->argc; i++)
    {
        NEON_ARGS_CHECKTYPE(args, i, isArray);
        arglist[i] = args->argv[i].asArray();
    }
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        alist = state->gcProtect(neon::Array::make(state));
        /* item of main list*/
        alist->push(list->m_varray->m_values[i]);
        for(j = 0; j < args->argc; j++)
        {
            if(i < arglist[j]->m_varray->m_count)
            {
                alist->push(arglist[j]->m_varray->m_values[i]);
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
    int i;
    int j;
    neon::Array* list;
    neon::Array* newlist;
    neon::Array* alist;
    neon::Array* arglist;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isArray);
    list = args->thisval.asArray();
    newlist = state->gcProtect(neon::Array::make(state));
    arglist = args->argv[0].asArray();
    for(i = 0; i < arglist->m_varray->m_count; i++)
    {
        if(!arglist->m_varray->m_values[i].isArray())
        {
            NEON_RETURNERROR("invalid list in zip entries");
        }
    }
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        alist = state->gcProtect(neon::Array::make(state));
        alist->push(list->m_varray->m_values[i]);
        for(j = 0; j < arglist->m_varray->m_count; j++)
        {
            if(i < arglist->m_varray->m_values[j].asArray()->m_varray->m_count)
            {
                alist->push(arglist->m_varray->m_values[j].asArray()->m_varray->m_values[i]);
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
    int i;
    neon::Dictionary* dict;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = state->gcProtect(neon::Dictionary::make(state));
    list = args->thisval.asArray();
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        dict->setEntry(neon::Value::makeNumber(i), list->m_varray->m_values[i]);
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
    if(index > -1 && index < list->m_varray->m_count)
    {
        return list->m_varray->m_values[index];
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
        if(list->m_varray->m_count == 0)
        {
            return neon::Value::makeBool(false);
        }
        return neon::Value::makeNumber(0);
    }
    if(!args->argv[0].isNumber())
    {
        NEON_RETURNERROR("lists are numerically indexed");
    }
    index = args->argv[0].asNumber();
    if(index < list->m_varray->m_count - 1)
    {
        return neon::Value::makeNumber((double)index + 1);
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_array_each(neon::State* state, neon::Arguments* args)
{
    int i;
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
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(arity > 0)
        {
            nestargs->m_varray->m_values[0] = list->m_varray->m_values[i];
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
    int i;
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
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(!list->m_varray->m_values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs->m_varray->m_values[0] = list->m_varray->m_values[i];
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
    int i;
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
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(!list->m_varray->m_values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs->m_varray->m_values[0] = list->m_varray->m_values[i];
                if(arity > 1)
                {
                    nestargs->m_varray->m_values[1] = neon::Value::makeNumber(i);
                }
            }
            nc.call(callable, args->thisval, nestargs, &result);
            if(!nn_value_isfalse(result))
            {
                resultlist->push(list->m_varray->m_values[i]);
            }
        }
    }
    state->stackPop();
    return neon::Value::fromObject(resultlist);
}

neon::Value nn_memberfunc_array_some(neon::State* state, neon::Arguments* args)
{
    int i;
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
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(!list->m_varray->m_values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs->m_varray->m_values[0] = list->m_varray->m_values[i];
                if(arity > 1)
                {
                    nestargs->m_varray->m_values[1] = neon::Value::makeNumber(i);
                }
            }
            nc.call(callable, args->thisval, nestargs, &result);
            if(!nn_value_isfalse(result))
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
    int i;
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
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(!list->m_varray->m_values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs->m_varray->m_values[0] = list->m_varray->m_values[i];
                if(arity > 1)
                {
                    nestargs->m_varray->m_values[1] = neon::Value::makeNumber(i);
                }
            }
            nc.call(callable, args->thisval, nestargs, &result);
            if(nn_value_isfalse(result))
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
    int i;
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
    if(accumulator.isNull() && list->m_varray->m_count > 0)
    {
        accumulator = list->m_varray->m_values[0];
        startindex = 1;
    }
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    neon::NestCall nc(state);
    arity = nc.prepare(callable, args->thisval, nestargs);
    for(i = startindex; i < list->m_varray->m_count; i++)
    {
        if(!list->m_varray->m_values[i].isNull() && !list->m_varray->m_values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs->m_varray->m_values[0] = accumulator;
                if(arity > 1)
                {
                    nestargs->m_varray->m_values[1] = list->m_varray->m_values[i];
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
    return neon::Value::makeNumber(nn_value_asrange(args->thisval)->m_lower);
}

neon::Value nn_memberfunc_range_upper(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return neon::Value::makeNumber(nn_value_asrange(args->thisval)->m_upper);
}

neon::Value nn_memberfunc_range_range(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return neon::Value::makeNumber(nn_value_asrange(args->thisval)->m_range);
}

neon::Value nn_memberfunc_range_iter(neon::State* state, neon::Arguments* args)
{
    int val;
    int index;
    neon::Range* range;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    range = nn_value_asrange(args->thisval);
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
    range = nn_value_asrange(args->thisval);
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
        NEON_RETURNERROR("ranges are numerically indexed");
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
    range = nn_value_asrange(args->thisval);
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
    range = nn_value_asrange(args->thisval);
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
    count = selfarr->m_varray->m_count;
    if(count == 0)
    {
        return neon::Value::fromObject(neon::String::copy(state, ""));
    }
    neon::Printer pd(state);
    for(i = 0; i < count; i++)
    {
        nn_printer_printvalue(&pd, list[i], false, true);
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
        fillchar = ofillstr->m_sbuf->data[0];
    }
    if(width <= (int)string->m_sbuf->length)
    {
        return args->thisval;
    }
    fillsize = width - string->m_sbuf->length;
    fill = (char*)neon::State::GC::allocate(state, sizeof(char), (size_t)fillsize + 1);
    finalsize = string->m_sbuf->length + fillsize;
    finalutf8size = string->m_sbuf->length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)neon::State::GC::allocate(state, sizeof(char), (size_t)finalsize + 1);
    memcpy(str, fill, fillsize);
    memcpy(str + fillsize, string->m_sbuf->data, string->m_sbuf->length);
    str[finalsize] = '\0';
    nn_gcmem_freearray(state, sizeof(char), fill, fillsize + 1);
    result = neon::String::take(state, str, finalsize);
    result->m_sbuf->length = finalutf8size;
    result->m_sbuf->length = finalsize;
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
        fillchar = ofillstr->m_sbuf->data[0];
    }
    if(width <= (int)string->m_sbuf->length)
    {
        return args->thisval;
    }
    fillsize = width - string->m_sbuf->length;
    fill = (char*)neon::State::GC::allocate(state, sizeof(char), (size_t)fillsize + 1);
    finalsize = string->m_sbuf->length + fillsize;
    finalutf8size = string->m_sbuf->length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)neon::State::GC::allocate(state, sizeof(char), (size_t)finalsize + 1);
    memcpy(str, string->m_sbuf->data, string->m_sbuf->length);
    memcpy(str + string->m_sbuf->length, fill, fillsize);
    str[finalsize] = '\0';
    nn_gcmem_freearray(state, sizeof(char), fill, fillsize + 1);
    result = neon::String::take(state, str, finalsize);
    result->m_sbuf->length = finalutf8size;
    result->m_sbuf->length = finalsize;
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
    StringBuffer* result;
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
    result = new StringBuffer(0);
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
    return neon::Value::fromObject(neon::String::makeFromStrbuf(state, result, nn_util_hashstring(result->data, result->length)));
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
        NEON_RETURNERROR("strings are numerically indexed");
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
    nn_printer_printvalue(&pd, v, true, false);
    os = pd.takeString();
    return neon::Value::fromObject(os);
}

neon::Value nn_memberfunc_object_tostring(neon::State* state, neon::Arguments* args)
{
    neon::Value v;
    neon::String* os;
    v = args->thisval;
    neon::Printer pd(state);
    nn_printer_printvalue(&pd, v, false, true);
    os = pd.takeString();
    return neon::Value::fromObject(os);
}

neon::Value nn_memberfunc_object_typename(neon::State* state, neon::Arguments* args)
{
    neon::Value v;
    neon::String* os;
    v = args->argv[0];
    os = neon::String::copy(state, nn_value_typename(v));
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
    neon::Value val;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    val = args->argv[0];
    return neon::Value::makeNumber(*(long*)&val);
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
        NEON_RETURNERROR("ord() expects character as argument, string given");
    }
    ord = (int)string->data()[0];
    if(ord < 0)
    {
        ord += 256;
    }
    return neon::Value::makeNumber(ord);
}

#define MT_STATE_SIZE 624
void nn_util_mtseed(uint32_t seed, uint32_t* binst, uint32_t* index)
{
    uint32_t i;
    binst[0] = seed;
    for(i = 1; i < MT_STATE_SIZE; i++)
    {
        binst[i] = (uint32_t)(1812433253UL * (binst[i - 1] ^ (binst[i - 1] >> 30)) + i);
    }
    *index = MT_STATE_SIZE;
}

uint32_t nn_util_mtgenerate(uint32_t* binst, uint32_t* index)
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

double nn_util_mtrand(double lowerlimit, double upperlimit)
{
    double randnum;
    uint32_t randval;
    struct timeval tv;
    static uint32_t mtstate[MT_STATE_SIZE];
    static uint32_t mtindex = MT_STATE_SIZE + 1;
    if(mtindex >= MT_STATE_SIZE)
    {
        osfn_gettimeofday(&tv, nullptr);
        nn_util_mtseed((uint32_t)(1000000 * tv.tv_sec + tv.tv_usec), mtstate, &mtindex);
    }
    randval = nn_util_mtgenerate(mtstate, &mtindex);
    randnum = lowerlimit + ((double)randval / UINT32_MAX) * (upperlimit - lowerlimit);
    return randnum;
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
    return neon::Value::makeNumber(nn_util_mtrand(lowerlimit, upperlimit));
}

neon::Value nn_nativefn_typeof(neon::State* state, neon::Arguments* args)
{
    const char* result;
    NEON_ARGS_CHECKCOUNT(args, 1);
    result = nn_value_typename(args->argv[0]);
    return neon::Value::fromObject(neon::String::copy(state, result));
}

neon::Value nn_nativefn_eval(neon::State* state, neon::Arguments* args)
{
    neon::Value result;
    neon::String* os;
    NEON_ARGS_CHECKCOUNT(args, 1);
    os = args->argv[0].asString();
    /*fprintf(stderr, "eval:src=%s\n", os->data());*/
    result = nn_state_evalsource(state, os->data());
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
    result = nn_state_evalsource(state, os->data());
    return result;
}
*/

neon::Value nn_nativefn_instanceof(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isInstance);
    NEON_ARGS_CHECKTYPE(args, 1, isClass);
    return neon::Value::makeBool(nn_util_isinstanceof(args->argv[0].asInstance()->m_fromclass, nn_value_asclass(args->argv[1])));
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
        nn_printer_printvalue(state->m_stdoutprinter, args->argv[i], false, true);
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
    nn_state_defnativefunction(state, "chr", nn_nativefn_chr);
    nn_state_defnativefunction(state, "id", nn_nativefn_id);
    nn_state_defnativefunction(state, "int", nn_nativefn_int);
    nn_state_defnativefunction(state, "instanceof", nn_nativefn_instanceof);
    nn_state_defnativefunction(state, "microtime", nn_nativefn_microtime);
    nn_state_defnativefunction(state, "ord", nn_nativefn_ord);
    nn_state_defnativefunction(state, "sprintf", nn_nativefn_sprintf);
    nn_state_defnativefunction(state, "printf", nn_nativefn_printf);
    nn_state_defnativefunction(state, "print", nn_nativefn_print);
    nn_state_defnativefunction(state, "println", nn_nativefn_println);
    nn_state_defnativefunction(state, "rand", nn_nativefn_rand);
    nn_state_defnativefunction(state, "time", nn_nativefn_time);
    nn_state_defnativefunction(state, "eval", nn_nativefn_eval);    
}

neon::Value nn_exceptions_getstacktrace(neon::State* state)
{
    int line;
    size_t i;
    size_t instruction;
    const char* fnname;
    const char* physfile;
    neon::State::CallFrame* frame;
    neon::FuncScript* function;
    neon::String* os;
    neon::Array* oa;
    oa = neon::Array::make(state);
    {
        for(i = 0; i < state->m_vmstate.framecount; i++)
        {
            neon::Printer pd(state);
            frame = &state->m_vmstate.framevalues[i];
            function = frame->closure->scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->blob->m_instrucs - 1;
            line = function->blob->m_instrucs[instruction].srcline;
            physfile = "(unknown)";
            if(function->module->physicalpath != nullptr)
            {
                if(function->module->physicalpath->m_sbuf != nullptr)
                {
                    physfile = function->module->physicalpath->data();
                }
            }
            fnname = "<script>";
            if(function->name != nullptr)
            {
                fnname = function->name->data();
            }
            pd.putformat("from %s() in %s:%d", fnname, physfile, line);
            os = pd.takeString();
            oa->push(neon::Value::fromObject(os));
            if((i > 15) && (state->m_conf.showfullstack == false))
            {
                pd = neon::Printer(state);
                pd.putformat("(only upper 15 entries shown)");
                os = pd.takeString();
                oa->push(neon::Value::fromObject(os));
                break;
            }
        }
        return neon::Value::fromObject(oa);
    }
    return neon::Value::fromObject(neon::String::copy(state, "", 0));
}

static NEON_ALWAYSINLINE neon::Instruction nn_util_makeinst(bool isop, uint8_t code, int srcline)
{
    neon::Instruction inst;
    inst.isop = isop;
    inst.code = code;
    inst.srcline = srcline;
    return inst;
}

neon::ClassObject* nn_exceptions_makeclass(neon::State* state, neon::Module* module, const char* cstrname)
{
    int messageconst;
    neon::ClassObject* klass;
    neon::String* classname;
    neon::FuncScript* function;
    neon::FuncClosure* closure;
    classname = neon::String::copy(state, cstrname);
    state->stackPush(neon::Value::fromObject(classname));
    klass = neon::ClassObject::make(state, classname);
    state->stackPop();
    state->stackPush(neon::Value::fromObject(klass));
    function = neon::FuncScript::make(state, module, neon::FuncCommon::FUNCTYPE_METHOD);
    function->arity = 1;
    function->isvariadic = false;
    state->stackPush(neon::Value::fromObject(function));
    {
        /* g_loc 0 */
        function->blob->push(nn_util_makeinst(true, neon::Instruction::OP_LOCALGET, 0));
        function->blob->push(nn_util_makeinst(false, (0 >> 8) & 0xff, 0));
        function->blob->push(nn_util_makeinst(false, 0 & 0xff, 0));
    }
    {
        /* g_loc 1 */
        function->blob->push(nn_util_makeinst(true, neon::Instruction::OP_LOCALGET, 0));
        function->blob->push(nn_util_makeinst(false, (1 >> 8) & 0xff, 0));
        function->blob->push(nn_util_makeinst(false, 1 & 0xff, 0));
    }
    {
        messageconst = function->blob->pushConst(neon::Value::fromObject(neon::String::copy(state, "message")));
        /* s_prop 0 */
        function->blob->push(nn_util_makeinst(true, neon::Instruction::OP_PROPERTYSET, 0));
        function->blob->push(nn_util_makeinst(false, (messageconst >> 8) & 0xff, 0));
        function->blob->push(nn_util_makeinst(false, messageconst & 0xff, 0));
    }
    {
        /* pop */
        function->blob->push(nn_util_makeinst(true, neon::Instruction::OP_POPONE, 0));
        function->blob->push(nn_util_makeinst(true, neon::Instruction::OP_POPONE, 0));
    }
    {
        /* g_loc 0 */
        /*
        //  function->blob->push(nn_util_makeinst(true, neon::Instruction::OP_LOCALGET, 0));
        //  function->blob->push(nn_util_makeinst(false, (0 >> 8) & 0xff, 0));
        //  function->blob->push(nn_util_makeinst(false, 0 & 0xff, 0));
        */
    }
    {
        /* ret */
        function->blob->push(nn_util_makeinst(true, neon::Instruction::OP_RETURN, 0));
    }
    closure = neon::FuncClosure::make(state, function);
    state->stackPop();
    /* set class constructor */
    state->stackPush(neon::Value::fromObject(closure));
    klass->m_methods->set(neon::Value::fromObject(classname), neon::Value::fromObject(closure));
    klass->m_constructor = neon::Value::fromObject(closure);
    /* set class properties */
    klass->defProperty("message", neon::Value::makeNull());
    klass->defProperty("stacktrace", neon::Value::makeNull());
    state->m_definedglobals->set(neon::Value::fromObject(classname), neon::Value::fromObject(klass));
    /* for class */
    state->stackPop();
    state->stackPop();
    /* assert error name */
    /* state->stackPop(); */
    return klass;
}

neon::ClassInstance* nn_exceptions_makeinstance(neon::State* state, neon::ClassObject* exklass, const char* srcfile, int srcline, neon::String* message)
{
    neon::ClassInstance* instance;
    neon::String* osfile;
    instance = neon::ClassInstance::make(state, exklass);
    osfile = neon::String::copy(state, srcfile);
    state->stackPush(neon::Value::fromObject(instance));
    instance->defProperty("message", neon::Value::fromObject(message));
    instance->defProperty("srcfile", neon::Value::fromObject(osfile));
    instance->defProperty("srcline", neon::Value::makeNumber(srcline));
    state->stackPop();
    return instance;
}



void nn_state_defglobalvalue(neon::State* state, const char* name, neon::Value val)
{
    neon::String* oname;
    oname = neon::String::copy(state, name);
    state->stackPush(neon::Value::fromObject(oname));
    state->stackPush(val);
    state->m_definedglobals->set(state->m_vmstate.stackvalues[0], state->m_vmstate.stackvalues[1]);
    state->stackPop(2);
}

void nn_state_defnativefunction(neon::State* state, const char* name, neon::FuncNative::CallbackFN fptr)
{
    neon::FuncNative* func;
    func = neon::FuncNative::make(state, fptr, name);
    return nn_state_defglobalvalue(state, name, neon::Value::fromObject(func));
}

neon::ClassObject* nn_util_makeclass(neon::State* state, const char* name, neon::ClassObject* parent)
{
    neon::ClassObject* cl;
    neon::String* os;
    os = neon::String::copy(state, name);
    cl = neon::ClassObject::make(state, os);
    cl->m_superclass = parent;
    state->m_definedglobals->set(neon::Value::fromObject(os), neon::Value::fromObject(cl));
    return cl;
}

void nn_vm_initvmstate(neon::State* state)
{
    state->m_vmstate.linkedobjects = nullptr;
    state->m_vmstate.currentframe = nullptr;
    {
        state->m_vmstate.stackcapacity = NEON_CFG_INITSTACKCOUNT;
        state->m_vmstate.stackvalues = (neon::Value*)malloc(NEON_CFG_INITSTACKCOUNT * sizeof(neon::Value));
        if(state->m_vmstate.stackvalues == nullptr)
        {
            fprintf(stderr, "error: failed to allocate stackvalues!\n");
            abort();
        }
        memset(state->m_vmstate.stackvalues, 0, NEON_CFG_INITSTACKCOUNT * sizeof(neon::Value));
    }
    {
        state->m_vmstate.framecapacity = NEON_CFG_INITFRAMECOUNT;
        state->m_vmstate.framevalues = (neon::State::CallFrame*)malloc(NEON_CFG_INITFRAMECOUNT * sizeof(neon::State::CallFrame));
        if(state->m_vmstate.framevalues == nullptr)
        {
            fprintf(stderr, "error: failed to allocate framevalues!\n");
            abort();
        }
        memset(state->m_vmstate.framevalues, 0, NEON_CFG_INITFRAMECOUNT * sizeof(neon::State::CallFrame));
    }
}


void nn_state_resetvmstate(neon::State* state)
{
    state->m_vmstate.framecount = 0;
    state->m_vmstate.stackidx = 0;
    state->m_vmstate.openupvalues = nullptr;
}

bool nn_state_addsearchpathobj(neon::State* state, neon::String* os)
{
    state->m_modimportpath->push(neon::Value::fromObject(os));
    return true;
}

bool nn_state_addsearchpath(neon::State* state, const char* path)
{
    return nn_state_addsearchpathobj(state, neon::String::copy(state, path));
}

#if 0
    #define destrdebug(...) \
        { \
            fprintf(stderr, "in nn_state_destroy: "); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
        }
#else
    #define destrdebug(...)     
#endif

neon::State::~State()
{
    destrdebug("destroying m_modimportpath...");
    delete m_modimportpath;
    destrdebug("destroying linked objects...");
    nn_gcmem_destroylinkedobjects(this);
    /* since object in module can exist in m_definedglobals, it must come before */
    destrdebug("destroying module table...");
    delete m_loadedmodules;
    destrdebug("destroying globals table...");
    delete m_definedglobals;
    destrdebug("destroying strings table...");
    delete m_cachedstrings;
    destrdebug("destroying m_stdoutprinter...");
    delete m_stdoutprinter;
    destrdebug("destroying m_stderrprinter...");
    delete m_stderrprinter;
    destrdebug("destroying m_debugprinter...");
    delete m_debugprinter;
    destrdebug("destroying framevalues...");
    free(m_vmstate.framevalues);
    destrdebug("destroying stackvalues...");
    free(m_vmstate.stackvalues);
    destrdebug("destroying this...");
    destrdebug("done destroying!");
}

void neon::State::init(void* userptr)
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
    nn_state_resetvmstate(this);
    {
        m_gcstate.bytesallocated = 0;
        /* default is 1mb. Can be modified via the -g flag. */
        m_gcstate.nextgc = NEON_CFG_DEFAULTGCSTART;
        m_gcstate.graycount = 0;
        m_gcstate.graycapacity = 0;
        m_gcstate.graystack = nullptr;
    }
    {
        m_stdoutprinter = new neon::Printer(this, stdout, false);
        m_stdoutprinter->m_shouldflush = true;
        m_stderrprinter = new neon::Printer(this, stderr, false);
        m_debugprinter = new neon::Printer(this, stderr, false);
        m_debugprinter->m_shortenvalues = true;
        m_debugprinter->m_maxvallength = 15;
    }
    {
        m_loadedmodules = new neon::HashTable(this);
        m_cachedstrings = new neon::HashTable(this);
        m_definedglobals = new neon::HashTable(this);
    }
    {
        m_toplevelmodule = neon::Module::make(this, "", "<this>", false);
        m_constructorname = neon::String::copy(this, "constructor");
    }
    {
        m_modimportpath = new neon::ValArray(this);
        for(i=0; defaultsearchpaths[i]!=nullptr; i++)
        {
            nn_state_addsearchpath(this, defaultsearchpaths[i]);
        }
    }
    {
        m_classprimobject = nn_util_makeclass(this, "Object", nullptr);
        m_classprimprocess = nn_util_makeclass(this, "Process", m_classprimobject);
        m_classprimnumber = nn_util_makeclass(this, "Number", m_classprimobject);
        m_classprimstring = nn_util_makeclass(this, "String", m_classprimobject);
        m_classprimarray = nn_util_makeclass(this, "Array", m_classprimobject);
        m_classprimdict = nn_util_makeclass(this, "Dict", m_classprimobject);
        m_classprimfile = nn_util_makeclass(this, "File", m_classprimobject);
        m_classprimrange = nn_util_makeclass(this, "Range", m_classprimobject);
    }
    {
        m_envdict = neon::Dictionary::make(this);
    }
    {
        if(m_exceptions.stdexception == nullptr)
        {
            m_exceptions.stdexception = nn_exceptions_makeclass(this, nullptr, "Exception");
        }
        m_exceptions.asserterror = nn_exceptions_makeclass(this, nullptr, "AssertError");
        m_exceptions.syntaxerror = nn_exceptions_makeclass(this, nullptr, "SyntaxError");
        m_exceptions.ioerror = nn_exceptions_makeclass(this, nullptr, "IOError");
        m_exceptions.oserror = nn_exceptions_makeclass(this, nullptr, "OSError");
        m_exceptions.argumenterror = nn_exceptions_makeclass(this, nullptr, "ArgumentError");
    }
    {
        nn_state_initbuiltinfunctions(this);
        nn_state_initbuiltinmethods(this);
    }
    {
        {
            m_filestdout = neon::File::make(this, stdout, true, "<stdout>", "wb");
            nn_state_defglobalvalue(this, "STDOUT", neon::Value::fromObject(m_filestdout));
        }
        {
            m_filestderr = neon::File::make(this, stderr, true, "<stderr>", "wb");
            nn_state_defglobalvalue(this, "STDERR", neon::Value::fromObject(m_filestderr));
        }
        {
            m_filestdin = neon::File::make(this, stdin, true, "<stdin>", "rb");
            nn_state_defglobalvalue(this, "STDIN", neon::Value::fromObject(m_filestdin));
        }
    }
}

bool nn_util_methodisprivate(neon::String* name)
{
    return name->length() > 0 && name->data()[0] == '_';
}

bool nn_vm_callclosure(neon::State* state, neon::FuncClosure* closure, neon::Value thisval, int argcount)
{
    int i;
    int startva;
    neon::State::CallFrame* frame;
    neon::Array* argslist;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    /* fill empty parameters if not variadic */
    for(; !closure->scriptfunc->isvariadic && argcount < closure->scriptfunc->arity; argcount++)
    {
        state->stackPush(neon::Value::makeNull());
    }
    /* handle variadic arguments... */
    if(closure->scriptfunc->isvariadic && argcount >= closure->scriptfunc->arity - 1)
    {
        startva = argcount - closure->scriptfunc->arity;
        argslist = neon::Array::make(state);
        state->stackPush(neon::Value::fromObject(argslist));
        for(i = startva; i >= 0; i--)
        {
            argslist->m_varray->push(state->stackPeek(i + 1));
        }
        argcount -= startva;
        /* +1 for the gc protection push above */
        state->stackPop(startva + 2);
        state->stackPush(neon::Value::fromObject(argslist));
    }
    if(argcount != closure->scriptfunc->arity)
    {
        state->stackPop(argcount);
        if(closure->scriptfunc->isvariadic)
        {
            return nn_exceptions_throwException(state, "expected at least %d arguments but got %d", closure->scriptfunc->arity - 1, argcount);
        }
        else
        {
            return nn_exceptions_throwException(state, "expected %d arguments but got %d", closure->scriptfunc->arity, argcount);
        }
    }
    if(state->checkMaybeResize())
    {
        /* state->stackPop(argcount); */
    }
    frame = &state->m_vmstate.framevalues[state->m_vmstate.framecount++];
    frame->closure = closure;
    frame->inscode = closure->scriptfunc->blob->m_instrucs;
    frame->stackslotpos = state->m_vmstate.stackidx + (-argcount - 1);
    return true;
}

bool nn_vm_callnative(neon::State* state, neon::FuncNative* native, neon::Value thisval, int argcount)
{
    size_t spos;
    neon::Value r;
    neon::Value* vargs;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    spos = state->m_vmstate.stackidx + (-argcount);
    vargs = &state->m_vmstate.stackvalues[spos];
    neon::Arguments fnargs(state, native->name, thisval, vargs, argcount, native->userptr);
    r = native->natfunc(state, &fnargs);
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
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    if(callable.isObject())
    {
        switch(nn_value_objtype(callable))
        {
            case neon::Value::OBJTYPE_FUNCBOUND:
                {
                    neon::FuncBound* bound;
                    bound = nn_value_asfuncbound(callable);
                    spos = (state->m_vmstate.stackidx + (-argcount - 1));
                    state->m_vmstate.stackvalues[spos] = thisval;
                    return nn_vm_callclosure(state, bound->method, thisval, argcount);
                }
                break;
            case neon::Value::OBJTYPE_CLASS:
                {
                    neon::ClassObject* klass;
                    klass = nn_value_asclass(callable);
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
                        return nn_exceptions_throwException(state, "%s constructor expects 0 arguments, %d given", klass->m_classname->data(), argcount);
                    }
                    return true;
                }
                break;
            case neon::Value::OBJTYPE_MODULE:
                {
                    neon::Module* module;
                    neon::Property* field;
                    module = callable.asModule();
                    field = module->deftable->getFieldByObjStr(module->name);
                    if(field != nullptr)
                    {
                        return nn_vm_callvalue(state, field->value, thisval, argcount);
                    }
                    return nn_exceptions_throwException(state, "module %s does not export a default function", module->name);
                }
                break;
            case neon::Value::OBJTYPE_FUNCCLOSURE:
                {
                    return nn_vm_callclosure(state, callable.asFuncClosure(), thisval, argcount);
                }
                break;
            case neon::Value::OBJTYPE_FUNCNATIVE:
                {
                    return nn_vm_callnative(state, nn_value_asfuncnative(callable), thisval, argcount);
                }
                break;
            default:
                break;
        }
    }
    return nn_exceptions_throwException(state, "object of type %s is not callable", nn_value_typename(callable));
}

bool nn_vm_callvalue(neon::State* state, neon::Value callable, neon::Value thisval, int argcount)
{
    neon::Value actualthisval;
    if(callable.isObject())
    {
        switch(nn_value_objtype(callable))
        {
            case neon::Value::OBJTYPE_FUNCBOUND:
                {
                    neon::FuncBound* bound;
                    bound = nn_value_asfuncbound(callable);
                    actualthisval = bound->receiver;
                    if(!thisval.isEmpty())
                    {
                        actualthisval = thisval;
                    }
                    NEON_APIDEBUG(state, "actualthisval.type=%s, argcount=%d", nn_value_typename(actualthisval), argcount);
                    return nn_vm_callvaluewithobject(state, callable, actualthisval, argcount);
                }
                break;
            case neon::Value::OBJTYPE_CLASS:
                {
                    neon::ClassObject* klass;
                    neon::ClassInstance* instance;
                    klass = nn_value_asclass(callable);
                    instance = neon::ClassInstance::make(state, klass);
                    actualthisval = neon::Value::fromObject(instance);
                    if(!thisval.isEmpty())
                    {
                        actualthisval = thisval;
                    }
                    NEON_APIDEBUG(state, "actualthisval.type=%s, argcount=%d", nn_value_typename(actualthisval), argcount);
                    return nn_vm_callvaluewithobject(state, callable, actualthisval, argcount);
                }
                break;
            default:
                {
                }
                break;
        }
    }
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    return nn_vm_callvaluewithobject(state, callable, thisval, argcount);
}

neon::FuncCommon::Type nn_value_getmethodtype(neon::Value method)
{
    switch(nn_value_objtype(method))
    {
        case neon::Value::OBJTYPE_FUNCNATIVE:
            return nn_value_asfuncnative(method)->type;
        case neon::Value::OBJTYPE_FUNCCLOSURE:
            return method.asFuncClosure()->scriptfunc->type;
        default:
            break;
    }
    return neon::FuncCommon::FUNCTYPE_FUNCTION;
}


neon::ClassObject* nn_value_getclassfor(neon::State* state, neon::Value receiver)
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
    if(!nn_exceptions_throwException(state, ##__VA_ARGS__)) \
    { \
        return rtval; \
    }

static NEON_ALWAYSINLINE uint8_t nn_vmbits_readbyte(neon::State* state)
{
    uint8_t r;
    r = state->m_vmstate.currentframe->inscode->code;
    state->m_vmstate.currentframe->inscode++;
    return r;
}

static NEON_ALWAYSINLINE neon::Instruction nn_vmbits_readinstruction(neon::State* state)
{
    neon::Instruction r;
    r = *state->m_vmstate.currentframe->inscode;
    state->m_vmstate.currentframe->inscode++;
    return r;
}

static NEON_ALWAYSINLINE uint16_t nn_vmbits_readshort(neon::State* state)
{
    uint8_t b;
    uint8_t a;
    a = state->m_vmstate.currentframe->inscode[0].code;
    b = state->m_vmstate.currentframe->inscode[1].code;
    state->m_vmstate.currentframe->inscode += 2;
    return (uint16_t)((a << 8) | b);
}

static NEON_ALWAYSINLINE neon::Value nn_vmbits_readconst(neon::State* state)
{
    uint16_t idx;
    idx = nn_vmbits_readshort(state);
    return state->m_vmstate.currentframe->closure->scriptfunc->blob->m_constants->m_values[idx];
}

static NEON_ALWAYSINLINE neon::String* nn_vmbits_readstring(neon::State* state)
{
    return nn_vmbits_readconst(state).asString();
}

static NEON_ALWAYSINLINE bool nn_vmutil_invokemethodfromclass(neon::State* state, neon::ClassObject* klass, neon::String* name, int argcount)
{
    neon::Property* field;
    NEON_APIDEBUG(state, "argcount=%d", argcount);
    field = klass->m_methods->getFieldByObjStr(name);
    if(field != nullptr)
    {
        if(nn_value_getmethodtype(field->value) == neon::FuncCommon::FUNCTYPE_PRIVATE)
        {
            return nn_exceptions_throwException(state, "cannot call private method '%s' from instance of %s", name->data(), klass->m_classname->data());
        }
        return nn_vm_callvaluewithobject(state, field->value, neon::Value::fromObject(klass), argcount);
    }
    return nn_exceptions_throwException(state, "undefined method '%s' in %s", name->data(), klass->m_classname->data());
}

static NEON_ALWAYSINLINE bool nn_vmutil_invokemethodself(neon::State* state, neon::String* name, int argcount)
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
        field = instance->m_fromclass->m_methods->getFieldByObjStr(name);
        if(field != nullptr)
        {
            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
        }
        field = instance->m_properties->getFieldByObjStr(name);
        if(field != nullptr)
        {
            spos = (state->m_vmstate.stackidx + (-argcount - 1));
            state->m_vmstate.stackvalues[spos] = receiver;
            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
        }
    }
    else if(receiver.isClass())
    {
        field = nn_value_asclass(receiver)->m_methods->getFieldByObjStr(name);
        if(field != nullptr)
        {
            if(nn_value_getmethodtype(field->value) == neon::FuncCommon::FUNCTYPE_STATIC)
            {
                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
            }
            return nn_exceptions_throwException(state, "cannot call non-static method %s() on non instance", name->data());
        }
    }
    return nn_exceptions_throwException(state, "cannot call method '%s' on object of type '%s'", name->data(), nn_value_typename(receiver));
}

static NEON_ALWAYSINLINE bool nn_vmutil_invokemethod(neon::State* state, neon::String* name, int argcount)
{
    size_t spos;
    neon::Value::ObjType rectype;
    neon::Value receiver;
    neon::Property* field;
    neon::ClassObject* klass;
    receiver = state->stackPeek(argcount);
    NEON_APIDEBUG(state, "receiver.type=%s, argcount=%d", nn_value_typename(receiver), argcount);
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
                    field = module->deftable->getFieldByObjStr(name);
                    if(field != nullptr)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            return nn_exceptions_throwException(state, "cannot call private module method '%s'", name->data());
                        }
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                    }
                    return nn_exceptions_throwException(state, "module %s does not define class or method %s()", module->name, name->data());
                }
                break;
            case neon::Value::OBJTYPE_CLASS:
                {
                    NEON_APIDEBUG(state, "receiver is a class");
                    klass = nn_value_asclass(receiver);
                    field = klass->m_methods->getFieldByObjStr(name);
                    if(field != nullptr)
                    {
                        if(nn_value_getmethodtype(field->value) == neon::FuncCommon::FUNCTYPE_PRIVATE)
                        {
                            return nn_exceptions_throwException(state, "cannot call private method %s() on %s", name->data(), klass->m_classname->data());
                        }
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                    }
                    else
                    {
                        field = klass->getStaticProperty(name);
                        if(field != nullptr)
                        {
                            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                        }
                        field = klass->getStaticMethodField(name);
                        if(field != nullptr)
                        {
                            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                        }
                    }
                    return nn_exceptions_throwException(state, "unknown method %s() in class %s", name->data(), klass->m_classname->data());
                }
            case neon::Value::OBJTYPE_INSTANCE:
                {
                    neon::ClassInstance* instance;
                    NEON_APIDEBUG(state, "receiver is an instance");
                    instance = receiver.asInstance();
                    field = instance->m_properties->getFieldByObjStr(name);
                    if(field != nullptr)
                    {
                        spos = (state->m_vmstate.stackidx + (-argcount - 1));
                        state->m_vmstate.stackvalues[spos] = receiver;
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
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
                        return nn_vm_callnative(state, nn_value_asfuncnative(field->value), receiver, argcount);
                    }
                    /* NEW in v0.0.84, dictionaries can declare extra methods as part of their entries. */
                    else
                    {
                        field = nn_value_asdict(receiver)->m_valtable->getFieldByObjStr(name);
                        if(field != nullptr)
                        {
                            if(field->value.isCallable())
                            {
                                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                            }
                        }
                    }
                    return nn_exceptions_throwException(state, "'dict' has no method %s()", name->data());
                }
                default:
                    {
                    }
                    break;
        }
    }
    klass = nn_value_getclassfor(state, receiver);
    if(klass == nullptr)
    {
        /* @TODO: have methods for non objects as well. */
        return nn_exceptions_throwException(state, "non-object %s has no method named '%s'", nn_value_typename(receiver), name->data());
    }
    field = klass->getMethodField(name);
    if(field != nullptr)
    {
        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
    }
    return nn_exceptions_throwException(state, "'%s' has no method %s()", klass->m_classname->data(), name->data());
}

static NEON_ALWAYSINLINE bool nn_vmutil_bindmethod(neon::State* state, neon::ClassObject* klass, neon::String* name)
{
    neon::Value val;
    neon::Property* field;
    neon::FuncBound* bound;
    field = klass->m_methods->getFieldByObjStr(name);
    if(field != nullptr)
    {
        if(nn_value_getmethodtype(field->value) == neon::FuncCommon::FUNCTYPE_PRIVATE)
        {
            return nn_exceptions_throwException(state, "cannot get private property '%s' from instance", name->data());
        }
        val = state->stackPeek(0);
        bound = neon::FuncBound::make(state, val, field->value.asFuncClosure());
        state->stackPop();
        state->stackPush(neon::Value::fromObject(bound));
        return true;
    }
    return nn_exceptions_throwException(state, "undefined property '%s'", name->data());
}

static NEON_ALWAYSINLINE neon::ScopeUpvalue* nn_vmutil_upvaluescapture(neon::State* state, neon::Value* local, int stackpos)
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

static NEON_ALWAYSINLINE void nn_vmutil_upvaluesclose(neon::State* state, const neon::Value* last)
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

static NEON_ALWAYSINLINE void nn_vmutil_definemethod(neon::State* state, neon::String* name)
{
    neon::Value method;
    neon::ClassObject* klass;
    method = state->stackPeek(0);
    klass = nn_value_asclass(state->stackPeek(1));
    klass->m_methods->set(neon::Value::fromObject(name), method);
    if(nn_value_getmethodtype(method) == neon::FuncCommon::FUNCTYPE_INITIALIZER)
    {
        klass->m_constructor = method;
    }
    state->stackPop();
}

static NEON_ALWAYSINLINE void nn_vmutil_defineproperty(neon::State* state, neon::String* name, bool isstatic)
{
    neon::Value property;
    neon::ClassObject* klass;
    property = state->stackPeek(0);
    klass = nn_value_asclass(state->stackPeek(1));
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

bool nn_value_isfalse(neon::Value value)
{
    if(value.isBool())
    {
        return value.isBool() && !nn_value_asbool(value);
    }
    if(value.isNull() || value.isEmpty())
    {
        return true;
    }
    /* -1 is the number equivalent of false */
    if(value.isNumber())
    {
        return value.asNumber() < 0;
    }
    /* Non-empty strings are true, empty strings are false.*/
    if(value.isString())
    {
        return value.asString()->length() < 1;
    }
    /* Non-empty lists are true, empty lists are false.*/
    if(value.isArray())
    {
        return value.asArray()->m_varray->m_count == 0;
    }
    /* Non-empty dicts are true, empty dicts are false. */
    if(value.isDict())
    {
        return nn_value_asdict(value)->m_keynames->m_count == 0;
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

static NEON_ALWAYSINLINE neon::String* nn_vmutil_multiplystring(neon::State* state, neon::String* str, double number)
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

static NEON_ALWAYSINLINE neon::Array* nn_vmutil_combinearrays(neon::State* state, neon::Array* a, neon::Array* b)
{
    int i;
    neon::Array* list;
    list = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(list));
    for(i = 0; i < a->m_varray->m_count; i++)
    {
        list->m_varray->push(a->m_varray->m_values[i]);
    }
    for(i = 0; i < b->m_varray->m_count; i++)
    {
        list->m_varray->push(b->m_varray->m_values[i]);
    }
    state->stackPop();
    return list;
}

static NEON_ALWAYSINLINE void nn_vmutil_multiplyarray(neon::State* state, neon::Array* from, neon::Array* newlist, int times)
{
    int i;
    int j;
    (void)state;
    for(i = 0; i < times; i++)
    {
        for(j = 0; j < from->m_varray->m_count; j++)
        {
            newlist->m_varray->push(from->m_varray->m_values[j]);
        }
    }
}

static NEON_ALWAYSINLINE bool nn_vmutil_dogetrangedindexofarray(neon::State* state, neon::Array* list, bool willassign)
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
        return nn_exceptions_throwException(state, "list range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower), nn_value_typename(valupper));
    }
    idxlower = 0;
    if(vallower.isNumber())
    {
        idxlower = vallower.asNumber();
    }
    if(valupper.isNull())
    {
        idxupper = list->m_varray->m_count;
    }
    else
    {
        idxupper = valupper.asNumber();
    }
    if(idxlower < 0 || (idxupper < 0 && ((list->m_varray->m_count + idxupper) < 0)) || idxlower >= list->m_varray->m_count)
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
        idxupper = list->m_varray->m_count + idxupper;
    }
    if(idxupper > list->m_varray->m_count)
    {
        idxupper = list->m_varray->m_count;
    }
    newlist = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(newlist));
    for(i = idxlower; i < idxupper; i++)
    {
        newlist->m_varray->push(list->m_varray->m_values[i]);
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

static NEON_ALWAYSINLINE bool nn_vmutil_dogetrangedindexofstring(neon::State* state, neon::String* string, bool willassign)
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
        return nn_exceptions_throwException(state, "string range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower), nn_value_typename(valupper));
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

static NEON_ALWAYSINLINE bool nn_vmdo_getrangedindex(neon::State* state)
{
    bool isgotten;
    uint8_t willassign;
    neon::Value vfrom;
    willassign = nn_vmbits_readbyte(state);
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
        return nn_exceptions_throwException(state, "cannot range index object of type %s", nn_value_typename(vfrom));
    }
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmutil_doindexgetdict(neon::State* state, neon::Dictionary* dict, bool willassign)
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
        state->stackPush(field->value);
        return true;
    }
    state->stackPop(1);
    state->stackPush(neon::Value::makeEmpty());
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmutil_doindexgetmodule(neon::State* state, neon::Module* module, bool willassign)
{
    neon::Value vindex;
    neon::Value result;
    vindex = state->stackPeek(0);
    if(module->deftable->get(vindex, &result))
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
    return nn_exceptions_throwException(state, "%s is undefined in module %s", neon::Value::toString(state, vindex)->data(), module->name);
}

static NEON_ALWAYSINLINE bool nn_vmutil_doindexgetstring(neon::State* state, neon::String* string, bool willassign)
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
            rng = nn_value_asrange(vindex);
            state->stackPop();
            state->stackPush(neon::Value::makeNumber(rng->m_lower));
            state->stackPush(neon::Value::makeNumber(rng->m_upper));
            return nn_vmutil_dogetrangedindexofstring(state, string, willassign);
        }
        state->stackPop(1);
        return nn_exceptions_throwException(state, "strings are numerically indexed");
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

static NEON_ALWAYSINLINE bool nn_vmutil_doindexgetarray(neon::State* state, neon::Array* list, bool willassign)
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
            rng = nn_value_asrange(vindex);
            state->stackPop();
            state->stackPush(neon::Value::makeNumber(rng->m_lower));
            state->stackPush(neon::Value::makeNumber(rng->m_upper));
            return nn_vmutil_dogetrangedindexofarray(state, list, willassign);
        }
        state->stackPop();
        return nn_exceptions_throwException(state, "list are numerically indexed");
    }
    index = vindex.asNumber();
    if(NEON_UNLIKELY(index < 0))
    {
        index = list->m_varray->m_count + index;
    }
    if(index < list->m_varray->m_count && index >= 0)
    {
        finalval = list->m_varray->m_values[index];
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

static NEON_ALWAYSINLINE bool nn_vmdo_indexget(neon::State* state)
{
    bool isgotten;
    uint8_t willassign;
    neon::Value peeked;
    willassign = nn_vmbits_readbyte(state);
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
                if(!nn_vmutil_doindexgetdict(state, nn_value_asdict(peeked), willassign))
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
        nn_exceptions_throwException(state, "cannot index object of type %s", nn_value_typename(peeked));
    }
    return true;
}


static NEON_ALWAYSINLINE bool nn_vmutil_dosetindexdict(neon::State* state, neon::Dictionary* dict, neon::Value index, neon::Value value)
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

static NEON_ALWAYSINLINE bool nn_vmutil_dosetindexmodule(neon::State* state, neon::Module* module, neon::Value index, neon::Value value)
{
    module->deftable->set(index, value);
    /* pop the value, index and dict out */
    state->stackPop(3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = dict[index] = 10
    */
    state->stackPush(value);
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmutil_doindexsetarray(neon::State* state, neon::Array* list, neon::Value index, neon::Value value)
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
        return nn_exceptions_throwException(state, "list are numerically indexed");
    }
    ocap = list->m_varray->m_capacity;
    ocnt = list->m_varray->m_count;
    rawpos = index.asNumber();
    position = rawpos;
    if(rawpos < 0)
    {
        rawpos = list->m_varray->m_count + rawpos;
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
        vasz = list->m_varray->m_count;
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
        fprintf(stderr, "setting value at position %d (array count: %d)\n", position, list->m_varray->m_count);
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
    //return nn_exceptions_throwException(state, "lists index %d out of range", rawpos);
    //state->stackPush(nn_value_makeempty());
    //return true;
    */
}

static NEON_ALWAYSINLINE bool nn_vmutil_dosetindexstring(neon::State* state, neon::String* os, neon::Value index, neon::Value value)
{
    int iv;
    int rawpos;
    int position;
    int oslen;
    if(!index.isNumber())
    {
        state->stackPop(3);
        /* pop the value, index and list out */
        return nn_exceptions_throwException(state, "strings are numerically indexed");
    }
    iv = value.asNumber();
    rawpos = index.asNumber();
    oslen = os->length();
    position = rawpos;
    if(rawpos < 0)
    {
        position = (oslen + rawpos);
    }
    if(position < oslen && position > -oslen)
    {
        os->m_sbuf->data[position] = iv;
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
        os->m_sbuf->append(iv);
        state->stackPop(3);
        state->stackPush(value);
    }
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_indexset(neon::State* state)
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
            return nn_exceptions_throwException(state, "empty cannot be assigned");
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
                    return nn_vmutil_dosetindexdict(state, nn_value_asdict(target), index, value);
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
        return nn_exceptions_throwException(state, "type of %s is not a valid iterable", nn_value_typename(state->stackPeek(3)));
    }
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmutil_concatenate(neon::State* state)
{
    neon::Value vleft;
    neon::Value vright;
    neon::String* result;
    vright = state->stackPeek(0);
    vleft = state->stackPeek(1);
    neon::Printer pd(state);
    nn_printer_printvalue(&pd, vleft, false, true);
    nn_printer_printvalue(&pd, vright, false, true);
    result = pd.takeString();
    state->stackPop(2);
    state->stackPush(neon::Value::fromObject(result));
    return true;
}

static NEON_ALWAYSINLINE int nn_vmutil_floordiv(double a, double b)
{
    int d;
    d = (int)a / (int)b;
    return d - ((d * b == a) & ((a < 0) ^ (b < 0)));
}

static NEON_ALWAYSINLINE double nn_vmutil_modulo(double a, double b)
{
    double r;
    r = fmod(a, b);
    if(r != 0 && ((r < 0) != (b < 0)))
    {
        r += b;
    }
    return r;
}

static NEON_ALWAYSINLINE neon::Property* nn_vmutil_getproperty(neon::State* state, neon::Value peeked, neon::String* name)
{
    neon::Property* field;
    switch(peeked.asObject()->m_objtype)
    {
        case neon::Value::OBJTYPE_MODULE:
            {
                neon::Module* module;
                module = peeked.asModule();
                field = module->deftable->getFieldByObjStr(name);
                if(field != nullptr)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        nn_exceptions_throwException(state, "cannot get private module property '%s'", name->data());
                        return nullptr;
                    }
                    return field;
                }
                nn_exceptions_throwException(state, "%s module does not define '%s'", module->name, name->data());
                return nullptr;
            }
            break;
        case neon::Value::OBJTYPE_CLASS:
            {
                field = nn_value_asclass(peeked)->m_methods->getFieldByObjStr(name);
                if(field != nullptr)
                {
                    if(nn_value_getmethodtype(field->value) == neon::FuncCommon::FUNCTYPE_STATIC)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            nn_exceptions_throwException(state, "cannot call private property '%s' of class %s", name->data(),
                                nn_value_asclass(peeked)->m_classname->data());
                            return nullptr;
                        }
                        return field;
                    }
                }
                else
                {
                    field = nn_value_asclass(peeked)->getStaticProperty(name);
                    if(field != nullptr)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            nn_exceptions_throwException(state, "cannot call private property '%s' of class %s", name->data(),
                                nn_value_asclass(peeked)->m_classname->data());
                            return nullptr;
                        }
                        return field;
                    }
                }
                nn_exceptions_throwException(state, "class %s does not have a static property or method named '%s'",
                    nn_value_asclass(peeked)->m_classname->data(), name->data());
                return nullptr;
            }
            break;
        case neon::Value::OBJTYPE_INSTANCE:
            {
                neon::ClassInstance* instance;
                instance = peeked.asInstance();
                field = instance->m_properties->getFieldByObjStr(name);
                if(field != nullptr)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        nn_exceptions_throwException(state, "cannot call private property '%s' from instance of %s", name->data(), instance->m_fromclass->m_classname->data());
                        return nullptr;
                    }
                    return field;
                }
                if(nn_util_methodisprivate(name))
                {
                    nn_exceptions_throwException(state, "cannot bind private property '%s' to instance of %s", name->data(), instance->m_fromclass->m_classname->data());
                    return nullptr;
                }
                if(nn_vmutil_bindmethod(state, instance->m_fromclass, name))
                {
                    return field;
                }
                nn_exceptions_throwException(state, "instance of class %s does not have a property or method named '%s'",
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
                nn_exceptions_throwException(state, "class String has no named property '%s'", name->data());
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
                nn_exceptions_throwException(state, "class Array has no named property '%s'", name->data());
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
                nn_exceptions_throwException(state, "class Range has no named property '%s'", name->data());
                return nullptr;
            }
            break;
        case neon::Value::OBJTYPE_DICT:
            {
                field = nn_value_asdict(peeked)->m_valtable->getFieldByObjStr(name);
                if(field == nullptr)
                {
                    field = state->m_classprimdict->getPropertyField(name);
                }
                if(field != nullptr)
                {
                    return field;
                }
                nn_exceptions_throwException(state, "unknown key or class Dict property '%s'", name->data());
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
                nn_exceptions_throwException(state, "class File has no named property '%s'", name->data());
                return nullptr;
            }
            break;
        default:
            {
                nn_exceptions_throwException(state, "object of type %s does not carry properties", nn_value_typename(peeked));
                return nullptr;
            }
            break;
    }
    return nullptr;
}

static NEON_ALWAYSINLINE bool nn_vmdo_propertyget(neon::State* state)
{
    neon::Value peeked;
    neon::Property* field;
    neon::String* name;
    name = nn_vmbits_readstring(state);
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
            if(field->type == neon::Property::PROPTYPE_FUNCTION)
            {
                nn_vm_callvaluewithobject(state, field->value, peeked, 0);
            }
            else
            {
                state->stackPop();
                state->stackPush(field->value);
            }
        }
        return true;
    }
    else
    {
        nn_exceptions_throwException(state, "'%s' of type %s does not have properties", neon::Value::toString(state, peeked)->data(),
            nn_value_typename(peeked));
    }
    return false;
}

static NEON_ALWAYSINLINE bool nn_vmdo_propertygetself(neon::State* state)
{
    neon::Value peeked;
    neon::String* name;
    neon::ClassObject* klass;
    neon::ClassInstance* instance;
    neon::Module* module;
    neon::Property* field;
    name = nn_vmbits_readstring(state);
    peeked = state->stackPeek(0);
    if(peeked.isInstance())
    {
        instance = peeked.asInstance();
        field = instance->m_properties->getFieldByObjStr(name);
        if(field != nullptr)
        {
            /* pop the instance... */
            state->stackPop();
            state->stackPush(field->value);
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
        klass = nn_value_asclass(peeked);
        field = klass->m_methods->getFieldByObjStr(name);
        if(field != nullptr)
        {
            if(nn_value_getmethodtype(field->value) == neon::FuncCommon::FUNCTYPE_STATIC)
            {
                /* pop the class... */
                state->stackPop();
                state->stackPush(field->value);
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
                state->stackPush(field->value);
                return true;
            }
        }
        nn_vmmac_tryraise(state, false, "class %s does not have a static property or method named '%s'", klass->m_classname->data(), name->data());
        return false;
    }
    else if(peeked.isModule())
    {
        module = peeked.asModule();
        field = module->deftable->getFieldByObjStr(name);
        if(field != nullptr)
        {
            /* pop the module... */
            state->stackPop();
            state->stackPush(field->value);
            return true;
        }
        nn_vmmac_tryraise(state, false, "module %s does not define '%s'", module->name, name->data());
        return false;
    }
    nn_vmmac_tryraise(state, false, "'%s' of type %s does not have properties", neon::Value::toString(state, peeked)->data(),
        nn_value_typename(peeked));
    return false;
}

static NEON_ALWAYSINLINE bool nn_vmdo_propertyset(neon::State* state)
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
        nn_exceptions_throwException(state, "object of type %s cannot carry properties", nn_value_typename(vtarget));
        return false;
    }
    else if(state->stackPeek(0).isEmpty())
    {
        nn_exceptions_throwException(state, "empty cannot be assigned");
        return false;
    }
    name = nn_vmbits_readstring(state);
    vpeek = state->stackPeek(0);
    if(vtarget.isClass())
    {
        klass = nn_value_asclass(vtarget);
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
        dict = nn_value_asdict(vtarget);
        dict->setEntry(neon::Value::fromObject(name), vpeek);
        value = state->stackPop();
        /* removing the dictionary object */
        state->stackPop();
        state->stackPush(value);
    }
    return true;
}

static NEON_ALWAYSINLINE double nn_vmutil_valtonum(neon::Value v)
{
    if(v.isNull())
    {
        return 0;
    }
    if(v.isBool())
    {
        if(nn_value_asbool(v))
        {
            return 1;
        }
        return 0;
    }
    return v.asNumber();
}


static NEON_ALWAYSINLINE uint32_t nn_vmutil_valtouint(neon::Value v)
{
    if(v.isNull())
    {
        return 0;
    }
    if(v.isBool())
    {
        if(nn_value_asbool(v))
        {
            return 1;
        }
        return 0;
    }
    return v.asNumber();
}

static NEON_ALWAYSINLINE long nn_vmutil_valtoint(neon::Value v)
{
    return (long)nn_vmutil_valtonum(v);
}

static NEON_ALWAYSINLINE bool nn_vmdo_dobinary(neon::State* state)
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
        nn_vmmac_tryraise(state, false, "unsupported operand %s for %s and %s", nn_dbg_op2str(instruction), nn_value_typename(binvalleft), nn_value_typename(binvalright));
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
                fprintf(stderr, "unhandled instruction %d (%s)!\n", instruction, nn_dbg_op2str(instruction));
                return false;
            }
            break;
    }
    state->stackPush(res);
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_globaldefine(neon::State* state)
{
    neon::Value val;
    neon::String* name;
    neon::HashTable* tab;
    name = nn_vmbits_readstring(state);
    val = state->stackPeek(0);
    if(val.isEmpty())
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    tab = state->m_vmstate.currentframe->closure->scriptfunc->module->deftable;
    tab->set(neon::Value::fromObject(name), val);
    state->stackPop();
    #if (defined(DEBUG_TABLE) && DEBUG_TABLE) || 0
    state->m_definedglobals->printTo(state->m_debugprinter, "globals");
    #endif
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_globalget(neon::State* state)
{
    neon::String* name;
    neon::HashTable* tab;
    neon::Property* field;
    name = nn_vmbits_readstring(state);
    tab = state->m_vmstate.currentframe->closure->scriptfunc->module->deftable;
    field = tab->getFieldByObjStr(name);
    if(field == nullptr)
    {
        field = state->m_definedglobals->getFieldByObjStr(name);
        if(field == nullptr)
        {
            nn_vmmac_tryraise(state, false, "global name '%s' is not defined", name->data());
            return false;
        }
    }
    state->stackPush(field->value);
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_globalset(neon::State* state)
{
    neon::String* name;
    neon::HashTable* table;
    if(state->stackPeek(0).isEmpty())
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    name = nn_vmbits_readstring(state);
    table = state->m_vmstate.currentframe->closure->scriptfunc->module->deftable;
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

static NEON_ALWAYSINLINE bool nn_vmdo_localget(neon::State* state)
{
    size_t ssp;
    uint16_t slot;
    neon::Value val;
    slot = nn_vmbits_readshort(state);
    ssp = state->m_vmstate.currentframe->stackslotpos;
    val = state->m_vmstate.stackvalues[ssp + slot];
    state->stackPush(val);
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_localset(neon::State* state)
{
    size_t ssp;
    uint16_t slot;
    neon::Value peeked;
    slot = nn_vmbits_readshort(state);
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

static NEON_ALWAYSINLINE bool nn_vmdo_funcargget(neon::State* state)
{
    size_t ssp;
    uint16_t slot;
    neon::Value val;
    slot = nn_vmbits_readshort(state);
    ssp = state->m_vmstate.currentframe->stackslotpos;
    //fprintf(stderr, "FUNCARGGET: %s\n", state->m_vmstate.currentframe->closure->scriptfunc->name->data());
    val = state->m_vmstate.stackvalues[ssp + slot];
    state->stackPush(val);
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_funcargset(neon::State* state)
{
    size_t ssp;
    uint16_t slot;
    neon::Value peeked;
    slot = nn_vmbits_readshort(state);
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

static NEON_ALWAYSINLINE bool nn_vmdo_makeclosure(neon::State* state)
{
    int i;
    int index;
    size_t ssp;
    uint8_t islocal;
    neon::Value* upvals;
    neon::FuncScript* function;
    neon::FuncClosure* closure;
    function = nn_vmbits_readconst(state).asFuncScript();
    closure = neon::FuncClosure::make(state, function);
    state->stackPush(neon::Value::fromObject(closure));
    for(i = 0; i < closure->upvalcount; i++)
    {
        islocal = nn_vmbits_readbyte(state);
        index = nn_vmbits_readshort(state);
        if(islocal)
        {
            ssp = state->m_vmstate.currentframe->stackslotpos;
            upvals = &state->m_vmstate.stackvalues[ssp + index];
            closure->upvalues[i] = nn_vmutil_upvaluescapture(state, upvals, index);

        }
        else
        {
            closure->upvalues[i] = state->m_vmstate.currentframe->closure->upvalues[index];
        }
    }
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_makearray(neon::State* state)
{
    int i;
    int count;
    neon::Array* array;
    count = nn_vmbits_readshort(state);
    array = neon::Array::make(state);
    state->m_vmstate.stackvalues[state->m_vmstate.stackidx + (-count - 1)] = neon::Value::fromObject(array);
    for(i = count - 1; i >= 0; i--)
    {
        array->push(state->stackPeek(i));
    }
    state->stackPop(count);
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_makedict(neon::State* state)
{
    int i;
    int count;
    int realcount;
    neon::Value name;
    neon::Value value;
    neon::Dictionary* dict;
    /* 1 for key, 1 for value */
    realcount = nn_vmbits_readshort(state);
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
            nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "unsupported operand %s for %s and %s", #op, nn_value_typename(binvalleft), nn_value_typename(binvalright)); \
            break; \
        } \
        binvalright = state->stackPop(); \
        dbinright = binvalright.isBool() ? (nn_value_asbool(binvalright) ? 1 : 0) : binvalright.asNumber(); \
        binvalleft = state->stackPop(); \
        dbinleft = binvalleft.isBool() ? (nn_value_asbool(binvalleft) ? 1 : 0) : binvalleft.asNumber(); \
        state->stackPush(type(op(dbinleft, dbinright))); \
    } while(false)


neon::Status nn_vm_runvm(neon::State* state, int exitframe, neon::Value* rv)
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
    state->m_vmstate.currentframe = &state->m_vmstate.framevalues[state->m_vmstate.framecount - 1];
    for(;;)
    {
        /*
        // try...finally... (i.e. try without a catch but finally
        // whose try body raises an exception)
        // can cause us to go into an invalid mode where frame count == 0
        // to fix this, we need to exit with an appropriate mode here.
        */
        if(state->m_vmstate.framecount == 0)
        {
            return neon::Status::FAIL_RUNTIME;
        }
        if(state->m_conf.shoulddumpstack)
        {
            ofs = (int)(state->m_vmstate.currentframe->inscode - state->m_vmstate.currentframe->closure->scriptfunc->blob->m_instrucs);
            nn_dbg_printinstructionat(state->m_debugprinter, state->m_vmstate.currentframe->closure->scriptfunc->blob, ofs);
            fprintf(stderr, "stack (before)=[\n");
            iterpos = 0;
            for(dbgslot = state->m_vmstate.stackvalues; dbgslot < &state->m_vmstate.stackvalues[state->m_vmstate.stackidx]; dbgslot++)
            {
                printpos = iterpos + 1;
                iterpos++;
                fprintf(stderr, "  [%s%d%s] ", nc.color('y'), printpos, nc.color('0'));
                state->m_debugprinter->putformat("%s", nc.color('y'));
                nn_printer_printvalue(state->m_debugprinter, *dbgslot, true, false);
                state->m_debugprinter->putformat("%s", nc.color('0'));
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "]\n");
        }
        currinstr = nn_vmbits_readinstruction(state);
        state->m_vmstate.currentinstr = currinstr;
        //fprintf(stderr, "now executing at line %d\n", state->m_vmstate.currentinstr.srcline);
        switch(currinstr.code)
        {
            case neon::Instruction::OP_RETURN:
                {
                    size_t ssp;
                    neon::Value result;
                    result = state->stackPop();
                    if(rv != nullptr)
                    {
                        *rv = result;
                    }
                    ssp = state->m_vmstate.currentframe->stackslotpos;
                    nn_vmutil_upvaluesclose(state, &state->m_vmstate.stackvalues[ssp]);
                    state->m_vmstate.framecount--;
                    if(state->m_vmstate.framecount == 0)
                    {
                        state->stackPop();
                        return neon::Status::OK;
                    }
                    ssp = state->m_vmstate.currentframe->stackslotpos;
                    state->m_vmstate.stackidx = ssp;
                    state->stackPush(result);
                    state->m_vmstate.currentframe = &state->m_vmstate.framevalues[state->m_vmstate.framecount - 1];
                    if(state->m_vmstate.framecount == (size_t)exitframe)
                    {
                        return neon::Status::OK;
                    }
                }
                break;
            case neon::Instruction::OP_PUSHCONSTANT:
                {
                    neon::Value constant;
                    constant = nn_vmbits_readconst(state);
                    state->stackPush(constant);
                }
                break;
            case neon::Instruction::OP_PRIMADD:
                {
                    neon::Value valright;
                    neon::Value valleft;
                    neon::Value result;
                    valright = state->stackPeek(0);
                    valleft = state->stackPeek(1);
                    if(valright.isString() || valleft.isString())
                    {
                        if(!nn_vmutil_concatenate(state))
                        {
                            nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "unsupported operand + for %s and %s", nn_value_typename(valleft), nn_value_typename(valright));
                            break;
                        }
                    }
                    else if(valleft.isArray() && valright.isArray())
                    {
                        result = neon::Value::fromObject(nn_vmutil_combinearrays(state, valleft.asArray(), valright.asArray()));
                        state->stackPop(2);
                        state->stackPush(result);
                    }
                    else
                    {
                        nn_vmdo_dobinary(state);
                    }
                }
                break;
            case neon::Instruction::OP_PRIMSUBTRACT:
                {
                    nn_vmdo_dobinary(state);
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
                    peekright = state->stackPeek(0);
                    peekleft = state->stackPeek(1);
                    if(peekleft.isString() && peekright.isNumber())
                    {
                        dbnum = peekright.asNumber();
                        string = state->stackPeek(1).asString();
                        result = neon::Value::fromObject(nn_vmutil_multiplystring(state, string, dbnum));
                        state->stackPop(2);
                        state->stackPush(result);
                        break;
                    }
                    else if(peekleft.isArray() && peekright.isNumber())
                    {
                        intnum = (int)peekright.asNumber();
                        state->stackPop();
                        list = peekleft.asArray();
                        newlist = neon::Array::make(state);
                        state->stackPush(neon::Value::fromObject(newlist));
                        nn_vmutil_multiplyarray(state, list, newlist, intnum);
                        state->stackPop(2);
                        state->stackPush(neon::Value::fromObject(newlist));
                        break;
                    }
                    nn_vmdo_dobinary(state);
                }
                break;
            case neon::Instruction::OP_PRIMDIVIDE:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case neon::Instruction::OP_PRIMMODULO:
                {
                    BINARY_MOD_OP(state, neon::Value::makeNumber, nn_vmutil_modulo);
                }
                break;
            case neon::Instruction::OP_PRIMPOW:
                {
                    BINARY_MOD_OP(state, neon::Value::makeNumber, pow);
                }
                break;
            case neon::Instruction::OP_PRIMFLOORDIVIDE:
                {
                    BINARY_MOD_OP(state, neon::Value::makeNumber, nn_vmutil_floordiv);
                }
                break;
            case neon::Instruction::OP_PRIMNEGATE:
                {
                    neon::Value peeked;
                    peeked = state->stackPeek(0);
                    if(!peeked.isNumber())
                    {
                        nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "operator - not defined for object of type %s", nn_value_typename(peeked));
                        break;
                    }
                    state->stackPush(neon::Value::makeNumber(-state->stackPop().asNumber()));
                }
                break;
            case neon::Instruction::OP_PRIMBITNOT:
            {
                neon::Value peeked;
                peeked = state->stackPeek(0);
                if(!peeked.isNumber())
                {
                    nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "operator ~ not defined for object of type %s", nn_value_typename(peeked));
                    break;
                }
                state->stackPush(neon::Value::makeInt(~((int)state->stackPop().asNumber())));
                break;
            }
            case neon::Instruction::OP_PRIMAND:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case neon::Instruction::OP_PRIMOR:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case neon::Instruction::OP_PRIMBITXOR:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case neon::Instruction::OP_PRIMSHIFTLEFT:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case neon::Instruction::OP_PRIMSHIFTRIGHT:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case neon::Instruction::OP_PUSHONE:
                {
                    state->stackPush(neon::Value::makeNumber(1));
                }
                break;
            /* comparisons */
            case neon::Instruction::OP_EQUAL:
                {
                    neon::Value a;
                    neon::Value b;
                    b = state->stackPop();
                    a = state->stackPop();
                    state->stackPush(neon::Value::makeBool(nn_value_compare(state, a, b)));
                }
                break;
            case neon::Instruction::OP_PRIMGREATER:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case neon::Instruction::OP_PRIMLESSTHAN:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case neon::Instruction::OP_PRIMNOT:
                {
                    state->stackPush(neon::Value::makeBool(nn_value_isfalse(state->stackPop())));
                }
                break;
            case neon::Instruction::OP_PUSHNULL:
                {
                    state->stackPush(neon::Value::makeNull());
                }
                break;
            case neon::Instruction::OP_PUSHEMPTY:
                {
                    state->stackPush(neon::Value::makeEmpty());
                }
                break;
            case neon::Instruction::OP_PUSHTRUE:
                {
                    state->stackPush(neon::Value::makeBool(true));
                }
                break;
            case neon::Instruction::OP_PUSHFALSE:
                {
                    state->stackPush(neon::Value::makeBool(false));
                }
                break;

            case neon::Instruction::OP_JUMPNOW:
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    state->m_vmstate.currentframe->inscode += offset;
                }
                break;
            case neon::Instruction::OP_JUMPIFFALSE:
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    if(nn_value_isfalse(state->stackPeek(0)))
                    {
                        state->m_vmstate.currentframe->inscode += offset;
                    }
                }
                break;
            case neon::Instruction::OP_LOOP:
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    state->m_vmstate.currentframe->inscode -= offset;
                }
                break;
            case neon::Instruction::OP_ECHO:
                {
                    neon::Value val;
                    val = state->stackPeek(0);
                    nn_printer_printvalue(state->m_stdoutprinter, val, state->m_isreplmode, true);
                    if(!val.isEmpty())
                    {
                        state->m_stdoutprinter->put("\n");
                    }
                    state->stackPop();
                }
                break;
            case neon::Instruction::OP_STRINGIFY:
                {
                    neon::Value peeked;
                    neon::String* value;
                    peeked = state->stackPeek(0);
                    if(!peeked.isString() && !peeked.isNull())
                    {
                        value = neon::Value::toString(state, state->stackPop());
                        if(value->length() != 0)
                        {
                            state->stackPush(neon::Value::fromObject(value));
                        }
                        else
                        {
                            state->stackPush(neon::Value::makeNull());
                        }
                    }
                }
                break;
            case neon::Instruction::OP_DUPONE:
                {
                    state->stackPush(state->stackPeek(0));
                }
                break;
            case neon::Instruction::OP_POPONE:
                {
                    state->stackPop();
                }
                break;
            case neon::Instruction::OP_POPN:
                {
                    state->stackPop(nn_vmbits_readshort(state));
                }
                break;
            case neon::Instruction::OP_UPVALUECLOSE:
                {
                    nn_vmutil_upvaluesclose(state, &state->m_vmstate.stackvalues[state->m_vmstate.stackidx - 1]);
                    state->stackPop();
                }
                break;
            case neon::Instruction::OP_GLOBALDEFINE:
                {
                    if(!nn_vmdo_globaldefine(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_GLOBALGET:
                {
                    if(!nn_vmdo_globalget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_GLOBALSET:
                {
                    if(!nn_vmdo_globalset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_LOCALGET:
                {
                    if(!nn_vmdo_localget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_LOCALSET:
                {
                    if(!nn_vmdo_localset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_FUNCARGGET:
                {
                    if(!nn_vmdo_funcargget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_FUNCARGSET:
                {
                    if(!nn_vmdo_funcargset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;

            case neon::Instruction::OP_PROPERTYGET:
                {
                    if(!nn_vmdo_propertyget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_PROPERTYSET:
                {
                    if(!nn_vmdo_propertyset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_PROPERTYGETSELF:
                {
                    if(!nn_vmdo_propertygetself(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_MAKECLOSURE:
                {
                    if(!nn_vmdo_makeclosure(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_UPVALUEGET:
                {
                    int index;
                    neon::FuncClosure* closure;
                    index = nn_vmbits_readshort(state);
                    closure = state->m_vmstate.currentframe->closure;
                    if(index < closure->upvalcount)
                    {
                        state->stackPush(closure->upvalues[index]->m_location);
                    }
                    else
                    {
                        state->stackPush(neon::Value::makeEmpty());
                    }
                }
                break;
            case neon::Instruction::OP_UPVALUESET:
                {
                    int index;
                    index = nn_vmbits_readshort(state);
                    if(state->stackPeek(0).isEmpty())
                    {
                        nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "empty cannot be assigned");
                        break;
                    }
                    state->m_vmstate.currentframe->closure->upvalues[index]->m_location = state->stackPeek(0);
                }
                break;
            case neon::Instruction::OP_CALLFUNCTION:
                {
                    int argcount;
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vm_callvalue(state, state->stackPeek(argcount), neon::Value::makeEmpty(), argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->m_vmstate.currentframe = &state->m_vmstate.framevalues[state->m_vmstate.framecount - 1];
                }
                break;
            case neon::Instruction::OP_CALLMETHOD:
                {
                    int argcount;
                    neon::String* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vmutil_invokemethod(state, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->m_vmstate.currentframe = &state->m_vmstate.framevalues[state->m_vmstate.framecount - 1];
                }
                break;
            case neon::Instruction::OP_CLASSINVOKETHIS:
                {
                    int argcount;
                    neon::String* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vmutil_invokemethodself(state, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->m_vmstate.currentframe = &state->m_vmstate.framevalues[state->m_vmstate.framecount - 1];
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
                    name = nn_vmbits_readstring(state);
                    field = state->m_vmstate.currentframe->closure->scriptfunc->module->deftable->getFieldByObjStr(name);
                    if(field != nullptr)
                    {
                        if(field->value.isClass())
                        {
                            haveval = true;
                            pushme = field->value;
                        }
                    }
                    field = state->m_definedglobals->getFieldByObjStr(name);
                    if(field != nullptr)
                    {
                        if(field->value.isClass())
                        {
                            haveval = true;
                            pushme = field->value;
                        }
                    }
                    if(!haveval)
                    {
                        klass = neon::ClassObject::make(state, name);
                        pushme = neon::Value::fromObject(klass);
                    }
                    state->stackPush(pushme);
                }
                break;
            case neon::Instruction::OP_MAKEMETHOD:
                {
                    neon::String* name;
                    name = nn_vmbits_readstring(state);
                    nn_vmutil_definemethod(state, name);
                }
                break;
            case neon::Instruction::OP_CLASSPROPERTYDEFINE:
                {
                    int isstatic;
                    neon::String* name;
                    name = nn_vmbits_readstring(state);
                    isstatic = nn_vmbits_readbyte(state);
                    nn_vmutil_defineproperty(state, name, isstatic == 1);
                }
                break;
            case neon::Instruction::OP_CLASSINHERIT:
                {
                    neon::ClassObject* superclass;
                    neon::ClassObject* subclass;
                    if(!state->stackPeek(1).isClass())
                    {
                        nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "cannot inherit from non-class object");
                        break;
                    }
                    superclass = nn_value_asclass(state->stackPeek(1));
                    subclass = nn_value_asclass(state->stackPeek(0));
                    subclass->inheritFrom(superclass);
                    /* pop the subclass */
                    state->stackPop();
                }
                break;
            case neon::Instruction::OP_CLASSGETSUPER:
                {
                    neon::ClassObject* klass;
                    neon::String* name;
                    name = nn_vmbits_readstring(state);
                    klass = nn_value_asclass(state->stackPeek(0));
                    if(!nn_vmutil_bindmethod(state, klass->m_superclass, name))
                    {
                        nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "class %s does not define a function %s", klass->m_classname->data(), name->data());
                    }
                }
                break;
            case neon::Instruction::OP_CLASSINVOKESUPER:
                {
                    int argcount;
                    neon::ClassObject* klass;
                    neon::String* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    klass = nn_value_asclass(state->stackPop());
                    if(!nn_vmutil_invokemethodfromclass(state, klass, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->m_vmstate.currentframe = &state->m_vmstate.framevalues[state->m_vmstate.framecount - 1];
                }
                break;
            case neon::Instruction::OP_CLASSINVOKESUPERSELF:
                {
                    int argcount;
                    neon::ClassObject* klass;
                    argcount = nn_vmbits_readbyte(state);
                    klass = nn_value_asclass(state->stackPop());
                    if(!nn_vmutil_invokemethodfromclass(state, klass, state->m_constructorname, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->m_vmstate.currentframe = &state->m_vmstate.framevalues[state->m_vmstate.framecount - 1];
                }
                break;
            case neon::Instruction::OP_MAKEARRAY:
                {
                    if(!nn_vmdo_makearray(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_MAKERANGE:
                {
                    double lower;
                    double upper;
                    neon::Value vupper;
                    neon::Value vlower;
                    vupper = state->stackPeek(0);
                    vlower = state->stackPeek(1);
                    if(!vupper.isNumber() || !vlower.isNumber())
                    {
                        nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "invalid range boundaries");
                        break;
                    }
                    lower = vlower.asNumber();
                    upper = vupper.asNumber();
                    state->stackPop(2);
                    state->stackPush(neon::Value::fromObject(neon::Range::make(state, lower, upper)));
                }
                break;
            case neon::Instruction::OP_MAKEDICT:
                {
                    if(!nn_vmdo_makedict(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_INDEXGETRANGED:
                {
                    if(!nn_vmdo_getrangedindex(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_INDEXGET:
                {
                    if(!nn_vmdo_indexget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_INDEXSET:
                {
                    if(!nn_vmdo_indexset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case neon::Instruction::OP_IMPORTIMPORT:
                {
                    neon::Value res;
                    neon::String* name;
                    neon::Module* mod;
                    name = state->stackPeek(0).asString();
                    fprintf(stderr, "IMPORTIMPORT: name='%s'\n", name->data());
                    mod = neon::Module::loadModuleByName(state, state->m_toplevelmodule, name);
                    fprintf(stderr, "IMPORTIMPORT: mod='%p'\n", mod);
                    if(mod == nullptr)
                    {
                        res = neon::Value::makeNull();
                    }
                    else
                    {
                        res = neon::Value::fromObject(mod);
                    }
                    state->stackPush(res);
                }
                break;
            case neon::Instruction::OP_TYPEOF:
                {
                    neon::Value res;
                    neon::Value thing;
                    const char* result;
                    thing = state->stackPop();
                    result = nn_value_typename(thing);
                    res = neon::Value::fromObject(neon::String::copy(state, result));
                    state->stackPush(res);
                }
                break;
            case neon::Instruction::OP_ASSERT:
                {
                    neon::Value message;
                    neon::Value expression;
                    message = state->stackPop();
                    expression = state->stackPop();
                    if(nn_value_isfalse(expression))
                    {
                        if(!message.isNull())
                        {
                            nn_exceptions_throwclass(state, state->m_exceptions.asserterror, neon::Value::toString(state, message)->data());
                        }
                        else
                        {
                            nn_exceptions_throwclass(state, state->m_exceptions.asserterror, "assertion failed");
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
                    peeked = state->stackPeek(0);
                    isok = (
                        peeked.isInstance() ||
                        nn_util_isinstanceof(peeked.asInstance()->m_fromclass, state->m_exceptions.stdexception)
                    );
                    if(!isok)
                    {
                        nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "instance of Exception expected");
                        break;
                    }
                    stacktrace = nn_exceptions_getstacktrace(state);
                    instance = peeked.asInstance();
                    instance->defProperty("stacktrace", stacktrace);
                    if(state->exceptionPropagate())
                    {
                        state->m_vmstate.currentframe = &state->m_vmstate.framevalues[state->m_vmstate.framecount - 1];
                        break;
                    }
                    nn_vmmac_exitvm(state);
                }
            case neon::Instruction::OP_EXTRY:
                {
                    uint16_t addr;
                    uint16_t finaddr;
                    neon::Value value;
                    neon::String* type;
                    type = nn_vmbits_readstring(state);
                    addr = nn_vmbits_readshort(state);
                    finaddr = nn_vmbits_readshort(state);
                    if(addr != 0)
                    {
                        if(!state->m_definedglobals->get(neon::Value::fromObject(type), &value) || !value.isClass())
                        {
                            if(!state->m_vmstate.currentframe->closure->scriptfunc->module->deftable->get(neon::Value::fromObject(type), &value) || !value.isClass())
                            {
                                nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "object of type '%s' is not an exception", type->data());
                                break;
                            }
                        }
                        state->exceptionPushHandler(nn_value_asclass(value), addr, finaddr);
                    }
                    else
                    {
                        state->exceptionPushHandler(nullptr, addr, finaddr);
                    }
                }
                break;
            case neon::Instruction::OP_EXPOPTRY:
                {
                    state->m_vmstate.currentframe->handlercount--;
                }
                break;
            case neon::Instruction::OP_EXPUBLISHTRY:
                {
                    state->m_vmstate.currentframe->handlercount--;
                    if(state->exceptionPropagate())
                    {
                        state->m_vmstate.currentframe = &state->m_vmstate.framevalues[state->m_vmstate.framecount - 1];
                        break;
                    }
                    nn_vmmac_exitvm(state);
                }
                break;
            case neon::Instruction::OP_SWITCH:
                {
                    neon::Value expr;
                    neon::Value value;
                    neon::VarSwitch* sw;
                    sw = nn_value_asswitch(nn_vmbits_readconst(state));
                    expr = state->stackPeek(0);
                    if(sw->m_jumppositions->get(expr, &value))
                    {
                        state->m_vmstate.currentframe->inscode += (int)value.asNumber();
                    }
                    else if(sw->m_defaultjump != -1)
                    {
                        state->m_vmstate.currentframe->inscode += sw->m_defaultjump;
                    }
                    else
                    {
                        state->m_vmstate.currentframe->inscode += sw->m_exitjump;
                    }
                    state->stackPop();
                }
                break;
            default:
                {
                }
                break;
        }

    }
}

neon::FuncClosure* nn_state_compilesource(neon::State* state, neon::Module* module, bool fromeval, const char* source)
{
    neon::Blob* blob;
    neon::FuncScript* function;
    neon::FuncClosure* closure;
    blob = new neon::Blob(state);
    function = nn_astparser_compilesource(state, module, source, blob, false, fromeval);
    if(function == nullptr)
    {
        delete blob;
        return nullptr;
    }
    if(!fromeval)
    {
        state->stackPush(neon::Value::fromObject(function));
    }
    else
    {
        function->name = neon::String::copy(state, "(evaledcode)");
    }
    closure = neon::FuncClosure::make(state, function);
    if(!fromeval)
    {
        state->stackPop();
        state->stackPush(neon::Value::fromObject(closure));
    }
    delete blob;
    return closure;
}

neon::Status nn_state_execsource(neon::State* state, neon::Module* module, const char* source, neon::Value* dest)
{
    neon::Status status;
    neon::FuncClosure* closure;
    module->setFileField();
    closure = nn_state_compilesource(state, module, false, source);
    if(closure == nullptr)
    {
        return neon::Status::FAIL_COMPILE;
    }
    if(state->m_conf.exitafterbytecode)
    {
        return neon::Status::OK;
    }
    nn_vm_callclosure(state, closure, neon::Value::makeNull(), 0);
    status = nn_vm_runvm(state, 0, dest);
    return status;
}

neon::Value nn_state_evalsource(neon::State* state, const char* source)
{
    bool ok;
    int argc;
    neon::Value callme;
    neon::Value retval;
    neon::FuncClosure* closure;
    neon::Array* args;
    (void)argc;
    closure = nn_state_compilesource(state, state->m_toplevelmodule, true, source);
    callme = neon::Value::fromObject(closure);
    args = neon::Array::make(state);
    neon::NestCall nc(state);
    argc = nc.prepare(callme, neon::Value::makeNull(), args);
    ok = nc.call(callme, neon::Value::makeNull(), args, &retval);
    if(!ok)
    {
        nn_exceptions_throwException(state, "eval() failed");
    }
    return retval;
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
    StringBuffer source;
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
            nn_state_execsource(state, state->m_toplevelmodule, source.data, &dest);
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
    state->m_toplevelmodule->physicalpath = neon::String::copy(state, rp);
    free(rp);
    result = nn_state_execsource(state, state->m_toplevelmodule, source, nullptr);
    free(source);
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
    result = nn_state_execsource(state, state->m_toplevelmodule, source, nullptr);
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
    ptyp(neon::AstToken);
    ptyp(neon::AstLexer);
    ptyp(neon::AstFuncCompiler::CompiledLocal);
    ptyp(neon::AstFuncCompiler::CompiledUpvalue);
    ptyp(neon::AstFuncCompiler);
    ptyp(neon::AstClassCompiler);
    ptyp(neon::AstParser);
    ptyp(neon::AstParser::Rule);
    ptyp(neon::RegModule::FuncInfo);
    ptyp(neon::RegModule::FieldInfo);
    ptyp(neon::RegModule::ClassInfo);
    ptyp(neon::RegModule);
    ptyp(neon::Instruction)
    #undef ptyp
}


void optprs_fprintmaybearg(FILE* out, const char* begin, const char* flagname, size_t flaglen, bool needval, bool maybeval, const char* delim)
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

void optprs_fprintusage(FILE* out, optlongflags_t* flags)
{
    size_t i;
    char ch;
    bool needval;
    bool maybeval;
    bool hadshort;
    optlongflags_t* flag;
    for(i=0; flags[i].longname != nullptr; i++)
    {
        flag = &flags[i];
        hadshort = false;
        needval = (flag->argtype > OPTPARSE_NONE);
        maybeval = (flag->argtype == OPTPARSE_OPTIONAL);
        if(flag->shortname > 0)
        {
            hadshort = true;
            ch = flag->shortname;
            fprintf(out, "    ");
            optprs_fprintmaybearg(out, "-", &ch, 1, needval, maybeval, nullptr);
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
            optprs_fprintmaybearg(out, "--", flag->longname, strlen(flag->longname), needval, maybeval, "=");
        }
        if(flag->helptext != nullptr)
        {
            fprintf(out, "  -  %s", flag->helptext);
        }
        fprintf(out, "\n");
    }
}

void nn_cli_showusage(char* argv[], optlongflags_t* flags, bool fail)
{
    FILE* out;
    out = fail ? stderr : stdout;
    fprintf(out, "Usage: %s [<options>] [<filename> | -e <code>]\n", argv[0]);
    optprs_fprintusage(out, flags);
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
    optcontext_t options;
    neon::State* state;
    ok = true;
    wasusage = false;
    quitafterinit = false;
    source = nullptr;
    nextgcstart = NEON_CFG_DEFAULTGCSTART;
    state = new neon::State();
    optlongflags_t longopts[] =
    {
        {"help", 'h', OPTPARSE_NONE, "this help"},
        {"strict", 's', OPTPARSE_NONE, "enable strict mode, such as requiring explicit var declarations"},
        {"warn", 'w', OPTPARSE_NONE, "enable warnings"},
        {"debug", 'd', OPTPARSE_NONE, "enable debugging: print instructions and stack values during execution"},
        {"exitaftercompile", 'x', OPTPARSE_NONE, "when using '-d', quit after printing compiled function(s)"},
        {"eval", 'e', OPTPARSE_REQUIRED, "evaluate a single line of code"},
        {"quit", 'q', OPTPARSE_NONE, "initiate, then immediately destroy the interpreter state"},
        {"types", 't', OPTPARSE_NONE, "print sizeof() of types"},
        {"apidebug", 'a', OPTPARSE_NONE, "print calls to API (very verbose, very slow)"},
        {"astdebug", 'A', OPTPARSE_NONE, "print calls to the parser (very verbose, very slow)"},
        {"gcstart", 'g', OPTPARSE_REQUIRED, "set minimum bytes at which the GC should kick in. 0 disables GC"},
        {0, 0, (optargtype_t)0, nullptr}
    };
    nargc = 0;
    optprs_init(&options, argc, argv);
    options.permute = 0;
    while((opt = optprs_nextlongflag(&options, longopts, &longindex)) != -1)
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
            state->m_conf.shoulddumpstack = true;        
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
        arg = optprs_nextpositional(&options);
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
        filename = state->m_cliargv->m_varray->m_values[0].asString()->data();
        fprintf(stderr, "nargv[0]=%s\n", filename);
        ok = nn_cli_runfile(state, filename);
    }
    else
    {
        ok = nn_cli_repl(state);
    }
    cleanup:
    delete state;
    if(ok)
    {
        return 0;
    }
    return 1;
}


