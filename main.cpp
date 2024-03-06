
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
#include <bit>
#include <iostream>

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

#if defined(__GNUC__)
    #define NEON_FORCEINLINE __attribute__((always_inline)) inline
#else
    #define NEON_FORCEINLINE inline
#endif

#define NEON_CFG_FILEEXT ".nn"

/* global debug mode flag */
#define NEON_CFG_BUILDDEBUGMODE 0

#if NEON_CFG_BUILDDEBUGMODE == 1
    /*
    * will print information about get/set operations on HashTable.
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



/* check for miminum args $d ($d ... n) */
#define NEON_ARGS_CHECKMINARG(chp, d) \
    if((chp)->argc < (d)) \
    { \
        return state->m_vmstate->raiseFromFunction("ArgumentError", args, "%s() expects minimum of %d arguments, %d given", (chp)->funcname, d, (chp)->argc); \
    }

/* check for range of args ($low .. $up) */
#define NEON_ARGS_CHECKCOUNTRANGE(chp, low, up) \
    if((chp)->argc < (low) || (chp)->argc > (up)) \
    { \
        return state->m_vmstate->raiseFromFunction(args, "%s() expects between %d and %d arguments, %d given", (chp)->funcname, low, up, (chp)->argc); \
    }


/* reject argument $type at $index */
#define NEON_ARGS_REJECTTYPE(chp, __callfn__, index) \
    if((chp)->argv[index].__callfn__()) \
    { \
        return state->m_vmstate->raiseFromFunction(args, "invalid type %s() as argument %d in %s()", (chp)->argv[index].name(), (index) + 1, (chp)->funcname); \
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
        return Status::FAIL_RUNTIME; \
    }        

#define NEON_VMMAC_TRYRAISE(state, rtval, ...) \
    if(!state->m_vmstate->raiseClass(state->m_exceptions.stdexception, ##__VA_ARGS__)) \
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
    struct /**/CallState;
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
}

#include "memory.h"

namespace neon
{
    namespace Helper
    {
        size_t getStringLength(String* os);
        const char* getStringData(String* os);
        uint32_t getStringHash(String* os);
        String* copyObjString(State* state, const char* str);
        String* copyObjString(State* state, const char* str, size_t len);
        void printTableTo(Printer* pd, HashTable* table, const char* name);
    }

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

        // via https://stackoverflow.com/a/38478032
        template<typename Type>
        class Callable;

        template<typename RetType, typename... ArgsType>
        class Callable<RetType(ArgsType...)>
        {
            public:
                // function pointer types for the type-erasure behaviors
                // all these char* parameters are actually casted from some functor type
                using invoke_fn_t = RetType (*)(char*, ArgsType&&...);
                using construct_fn_t = void (*)(char*, char*);
                using destroy_fn_t = void (*)(char*);

            public:
                // type-aware generic functions for invoking
                // the specialization of these functions won't be capable with
                //   the above function pointer types, so we need some cast
                template<typename Functor> static RetType invoke_fn(Functor* fn, ArgsType&&... args)
                {
                    return (*fn)(std::forward<ArgsType>(args)...);
                }

                template<typename Functor> static void construct_fn(Functor* construct_dst, Functor* construct_src)
                {
                    // the functor type must be copy-constructible
                    new(construct_dst) Functor(*construct_src);
                }

                template<typename Functor> static void destroy_fn(Functor* f)
                {
                    f->~Functor();
                }

            private:
                // these pointers are storing behaviors
                invoke_fn_t m_fninvoke = nullptr;
                construct_fn_t m_fnconstruct = nullptr;
                destroy_fn_t m_fndestroy = nullptr;
                // erase the type of any functor and store it into a char*
                // so the storage size should be obtained as well
                //std::unique_ptr<char[]> m_dataptr;
                char* m_dataptr = nullptr;
                size_t m_datasize = 0;

            public:
                Callable() : m_fninvoke(nullptr), m_fnconstruct(nullptr), m_fndestroy(nullptr), m_dataptr(nullptr), m_datasize(0)
                {
                }

                // construct from any functor type
                // specialize functions and erase their type info by casting
                template<typename Functor>
                Callable(Functor f):
                    m_fninvoke(reinterpret_cast<invoke_fn_t>(invoke_fn<Functor>)),
                    m_fnconstruct(reinterpret_cast<construct_fn_t>(construct_fn<Functor>)),
                    m_fndestroy(reinterpret_cast<destroy_fn_t>(destroy_fn<Functor>)),
                    m_dataptr(new char[sizeof(Functor)]),
                    m_datasize(sizeof(Functor))
                {
                    // copy the functor to internal storage
                    m_fnconstruct(m_dataptr, reinterpret_cast<char*>(&f));
                }

                // copy constructor
                Callable(const Callable& rhs) : m_fninvoke(rhs.m_fninvoke), m_fnconstruct(rhs.m_fnconstruct), m_fndestroy(rhs.m_fndestroy), m_datasize(rhs.m_datasize)
                {
                    if(m_fninvoke)
                    {
                        // when the source is not a null function, copy its internal functor
                        if(m_dataptr != nullptr)
                        {
                            delete[] m_dataptr;
                        }
                        m_dataptr = new char[m_datasize];
                        m_fnconstruct(m_dataptr, rhs.m_dataptr);
                    }
                }

                ~Callable()
                {
                    if(m_dataptr != nullptr)
                    {
                        //m_fndestroy(m_dataptr);
                        delete[] m_dataptr;
                    }
                }

                // other constructors, from nullptr, from function pointers

                RetType operator()(ArgsType&&... args)
                {
                    return m_fninvoke(m_dataptr, std::forward<ArgsType>(args)...);
                }
        };

        struct Utf8Iterator
        {
            public:
                static NEON_FORCEINLINE uint32_t convertToCodepoint(const char* character, uint8_t size)
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
                static NEON_FORCEINLINE uint8_t getCharSize(const char* character)
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
                //chars = (char*)State::VM::gcAllocMemory(state, sizeof(char), (size_t)count + 1);
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
            //buf = (char*)State::VM::gcAllocMemory(state, sizeof(char), toldlen + 1);
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

        struct AnyStream
        {
            public:
                enum Type
                {
                    REC_INVALID,
                    REC_CFILE,
                    REC_CPPSTREAM,
                    REC_STRING,
                };

                union RecUnion
                {
                    Util::StrBuffer* strbuf;
                    FILE* handle;
                    std::ostream* ostrm;
                };

            private:
                Type m_type = REC_INVALID;
                RecUnion m_recs;

            private:

                template<typename ItemT>
                inline bool putToString(const ItemT* thing, size_t count) const
                {
                    m_recs.strbuf->append((const char*)thing, count);
                    return true;
                }

                template<typename ItemT>
                inline bool putToCFile(const ItemT* thing, size_t count) const
                {
                    fwrite(thing, sizeof(ItemT), count, m_recs.handle);
                    return true;
                }

                template<typename ItemT>
                inline bool putToCPPStream(const ItemT* thing, size_t count) const
                {
                    m_recs.ostrm->write((const char*)thing, count);
                    return true;
                }

                inline bool putOneToCFile(int b) const
                {
                    fputc(b, m_recs.handle);
                    return true;
                }

                template<typename ItemT>
                inline bool readFromCFile(ItemT* target, size_t tsz, size_t count) const
                {
                    size_t rd;
                    rd = fread(target, tsz, count, m_recs.handle);
                    if(rd == count)
                    {
                        return true;
                    }
                    return false;
                }

                inline int readOneFromCFile() const
                {
                    return fgetc(m_recs.handle);
                }

            public:
                inline AnyStream()
                {
                }

                inline AnyStream(FILE* hnd): m_type(REC_CFILE)
                {
                    m_recs.handle = hnd;
                }

                inline AnyStream(std::ostream& os): m_type(REC_CPPSTREAM)
                {
                    m_recs.ostrm = &os;
                }

                inline AnyStream(Util::StrBuffer& os): m_type(REC_STRING)
                {
                    m_recs.strbuf = &os;
                }

                template<typename ItemT>
                inline bool put(const ItemT* thing, size_t count) const
                {
                    switch(m_type)
                    {
                        case REC_CFILE:
                            return putToCFile(thing, count);
                        case REC_CPPSTREAM:
                            return putToCPPStream(thing, count);
                        case REC_STRING:
                            return putToString(thing, count);
                        default:
                            break;
                    }
                    return false;
                }

                inline bool put(int b) const
                {
                    switch(m_type)
                    {
                        case REC_CFILE:
                            return putOneToCFile(b);
                        default:
                            break;
                    }
                    return false;
                }

                template<typename ItemT>
                inline bool read(ItemT* target, size_t tsz, size_t count) const
                {
                    switch(m_type)
                    {
                        case REC_CFILE:
                            return readFromCFile(target, tsz, count);
                        default:
                            break;
                    }
                    return false;
                }

                inline int get() const
                {
                    switch(m_type)
                    {
                        case REC_CFILE:
                            return readOneFromCFile();
                        default:
                            break;
                    }
                    return -1;
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
                    for(; (*optstring != 0) && c != *optstring; optstring++)
                    {
                    }
                    if(*optstring == 0)
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
                    return !longopts[i].longname && !longopts[i].shortname;
                }

                static void fromLong(const LongFlags* longopts, char* optstring)
                {
                    int i;
                    int a;
                    char* p;
                    p = optstring;
                    for(i = 0; !isLongOptsEnd(longopts, i); i++)
                    {
                        if((longopts[i].shortname != 0) && longopts[i].shortname < 127)
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
                        return false;
                    }
                    for(; (*a != 0) && (*n != 0) && *a != '='; a++, n++)
                    {
                        if(*a != *n)
                        {
                            return false;
                        }
                    }
                    return *n == '\0' && (*a == '\0' || *a == '=');
                }

                /* Return the part after "=", or NULL. */
                static char* getLongOptsArg(char* option)
                {
                    for(; (*option != 0) && *option != '='; option++)
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
                    this->optind = static_cast<int>(argv[0] != 0);
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
                    while(*msg != 0)
                    {
                        this->errmsg[p++] = *msg++;
                    }
                    while(*sep != 0)
                    {
                        this->errmsg[p++] = *sep++;
                    }
                    while(p < sizeof(this->errmsg) - 2 && (*data != 0))
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
                        if(this->dopermute != 0)
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
                            if(longindex != nullptr)
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
                        if(this->dopermute != 0)
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
                                if(option[1] != 0)
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
                                if(option[1] != 0)
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
                                if(option[1] != 0)
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
                return osfn_isatty(m_fd) != 0;
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
                OBJTYPE_INVALID,
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

            enum ValCheck
            {
                
            };

            union ValUnion
            {
                bool boolean;
                double number;
                Object* obj;
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
                Value v = makeValue(VALTYPE_NULL);
                return v;
            }

            static NEON_FORCEINLINE Value makeBool(bool b)
            {
                Value v = makeValue(VALTYPE_BOOL);
                v.m_valunion.boolean = b;
                return v;
            }

            template<typename NumT>
            static NEON_FORCEINLINE Value makeNumber(NumT d)
            {
                Value v = makeValue(VALTYPE_NUMBER);
                v.m_valunion.number = d;
                return v;
            }

            static NEON_FORCEINLINE Value makeInt(int i)
            {
                Value v = makeValue(VALTYPE_NUMBER);
                v.m_valunion.number = i;
                return v;
            }

            template<typename SubObjT>
            static NEON_FORCEINLINE Value fromObject(SubObjT* obj)
            {
                Value v = makeValue(VALTYPE_OBJECT);
                v.m_valunion.obj = obj;
                return v;
            }

            static bool isObjFalse(Value val);

            static NEON_FORCEINLINE bool isFalse(Value val)
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

            static bool isLesserObject(Value a, Value b, bool defval);

            static NEON_FORCEINLINE bool isLesserValue(Value self, Value other)
            {
                if((other.canCoerceToNumber() != 0.0) && (self.canCoerceToNumber() != 0.0))
                {
                    return other.coerceToNumber() < self.coerceToNumber();
                }
                else if(other.isObject() && self.isObject())
                {
                    return isLesserObject(self, other, false);
                }
                return false;
            }

            static Value copyValue(Value value);

            static String* toString(State* state, Value value);

            static uint32_t getObjHash(Object* object);

            static NEON_FORCEINLINE uint32_t getHash(Value value)
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
            static ObjType objectType(Value val);

            static const char* objClassTypename(Object* obj);

            static NEON_FORCEINLINE const char* typenameFromEnum(ValType t)
            {
                switch(t)
                {
                    case VALTYPE_EMPTY: return "Empty";
                    case VALTYPE_NULL: return "Null";
                    case VALTYPE_BOOL: return "Bool";
                    case VALTYPE_NUMBER: return "Number";
                    case VALTYPE_OBJECT: return "Object";
                    default:
                        {
                        }
                        break;
                }
                return "?unknownvaltype?";
            }

            static NEON_FORCEINLINE const char* typenameFromEnum(ObjType t)
            {
                switch(t)
                {
                    case OBJTYPE_STRING: return "String";
                    case OBJTYPE_RANGE: return "Range";
                    case OBJTYPE_ARRAY: return "Array";
                    case OBJTYPE_DICT: return "Dict";
                    case OBJTYPE_FILE: return "File";
                    case OBJTYPE_UPVALUE: return "Upvalue";
                    case OBJTYPE_FUNCBOUND: return "Funcbound";
                    case OBJTYPE_FUNCCLOSURE: return "Funcclosure";
                    case OBJTYPE_FUNCSCRIPT: return "Funcscript";
                    case OBJTYPE_INSTANCE: return "Instance";
                    case OBJTYPE_FUNCNATIVE: return "Funcnative";
                    case OBJTYPE_CLASS: return "Class";
                    case OBJTYPE_MODULE: return "Module";
                    case OBJTYPE_SWITCH: return "Switch";
                    case OBJTYPE_USERDATA: return "Userdata";
                    default:
                        break;
                }
                return "?unknownobjtype?";
            }

            template<typename Func>
            static NEON_FORCEINLINE const char* typenameFromFuncPtr(Func fn)
            {
                if(fn == &Value::isEmpty)
                {
                    return typenameFromEnum(VALTYPE_EMPTY);
                }
                else if(fn == &Value::isNull)
                {
                    return typenameFromEnum(VALTYPE_NULL);
                }
                else if(fn == &Value::isNumber)
                {
                    return typenameFromEnum(VALTYPE_NUMBER);
                }
                else if(fn == &Value::isBool)
                {
                    return typenameFromEnum(VALTYPE_BOOL);
                }
                else if(fn == &Value::isString)
                {
                    return typenameFromEnum(OBJTYPE_STRING);
                }
                else if(fn == &Value::isArray)
                {
                    return typenameFromEnum(OBJTYPE_ARRAY);
                }
                else if(fn == &Value::isInstance)
                {
                    return typenameFromEnum(OBJTYPE_INSTANCE);
                }
                else if(fn == &Value::isClass)
                {
                    return typenameFromEnum(OBJTYPE_CLASS);
                }
                else if(fn == &Value::isCallable)
                {
                    /* synthetic type */
                    return "Callable";
                }
                return "?illegalornullfunc?";
            }

            static NEON_FORCEINLINE const char* objTypename(Value val)
            {
                ObjType ot;
                ot = objectType(val);
                if(ot == OBJTYPE_INSTANCE)
                {
                    return objClassTypename(val.asObject());
                }
                return typenameFromEnum(ot);
            }

            static NEON_FORCEINLINE const char* getTypename(const Value& value)
            {
                if(value.m_valtype == VALTYPE_OBJECT)
                {
                    return objTypename(value);
                }
                return typenameFromEnum(value.m_valtype);
            }

        public:
            ValType m_valtype;
            ValUnion m_valunion;

        public:
            #if 0
            bool operator<(const Value& other) const
            {
                return isLesserValue(*this, other);
            }
            #endif

            bool operator==(const Value& other) const
            {
                return compareActual(other);
            }

            /*
            int operator>(const Value& other) const
            {
                return isGreaterValue(*this, other);
            }
            */

            NEON_FORCEINLINE const char* name() const
            {
                return getTypename(*this);
            }

            NEON_FORCEINLINE bool canCoerceToNumber()
            {
                return (
                    isNull() || isBool() || isNumber()
                );
            }

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

            bool compareObject(Value other) const;

            NEON_FORCEINLINE bool compareActual(Value other) const
            {
                if(type() != other.type())
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
                            return asBool() == other.asBool();
                        }
                        break;
                    case Value::VALTYPE_NUMBER:
                        {
                            return (asNumber() == other.asNumber());
                        }
                        break;
                    case Value::VALTYPE_OBJECT:
                        {
                            if(asObject() == other.asObject())
                            {
                                return true;
                            }
                            return compareObject(other);
                        }
                        break;
                    default:
                        break;
                }
                return false;
            }

            NEON_FORCEINLINE bool compare(Value other) const
            {
                bool r;
                r = compareActual(other);
                return r;
            }

            Value::ObjType objectType() const
            {
                return objectType(*this);
            }

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

            NEON_FORCEINLINE bool isType(ValType t)
            {
                return (m_valtype == ValType(t));
            }

            NEON_FORCEINLINE bool isType(ObjType t)
            {
                if(m_valtype == VALTYPE_OBJECT)
                {
                    return isObjType(t);
                }
                return false;
            }

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

            NEON_FORCEINLINE Object* asObject() const
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

    struct ValArray: public Util::GenericArray<Value>
    {
        public:
            struct QuickSort
            {
                public:
                    template<typename ArrT>
                    static NEON_FORCEINLINE void swapGeneric(ArrT* varr, size_t idxleft, size_t idxright)
                    {
                        auto temp = varr[idxleft];
                        varr[idxleft] = varr[idxright];
                        varr[idxright] = temp;
                    }

                    static NEON_FORCEINLINE void swap(ValArray* varr, size_t idxleft, size_t idxright)
                    {
                        /*
                        Value temp;
                        temp = varr->m_values[idxleft];
                        varr->m_values[idxleft] = varr->m_values[idxright];
                        varr->m_values[idxright] = temp;
                        */
                        swapGeneric(varr->m_values, idxleft, idxright);
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
                            *dest = static_cast<size_t>(val.asBool());
                            return true;
                        }
                        return getObjNumberVal(val, dest);
                    }

                    static NEON_FORCEINLINE bool isLessOrEqual(Value a, Value b)
                    {
                        double ia;
                        double ib;
                        if(!getNumberVal(a, (size_t*)&ia))
                        {
                            return false;
                        }
                        if(!getNumberVal(b, (size_t*)&ib))
                        {
                            return false;
                        }
                        return (ia <= ib);
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
                            if(isLessOrEqual(arr->m_values[j], x))
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
            ValArray() 
            {
            }

            void gcMark(State* state);

            void insert(Value value, uint64_t index)
            {
                insertDefault(value, index, Value::makeNull());
            }

            Value shift()
            {
                return shiftDefault(1, Value::makeNull());
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

    #include "blob.h"

    struct ExceptionFrame
    {
        uint16_t address;
        uint16_t finallyaddress;
        bool hasclass;
        ClassObject* exklass;
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

    using NativeCallbackFN = Value (*)(State*, CallState*);

    struct ArgBase
    {
        public:
            State* m_pvm;
            const char* funcname;
            Value thisval;
            int argc;
            Value* argv;
            void* userptr;
    };

    struct State final
    {
        friend class CallState;
        public:
            struct VM
            {
                public:
                    /*
                    static void* allocate(State* state, size_t size, size_t amount)
                    {
                        return gcReallocate(state, nullptr, 0, size * amount);
                    }
                    */
                    static void* gcAllocMemory(State* state, size_t tsize, size_t count)
                    {
                        void* result;
                        size_t actualsz;
                        // this is a dummy; this function used be 'reallocate()'
                        size_t oldsize;
                        actualsz = (tsize * count);
                        oldsize = 0;
                        state->m_vmstate->gcMaybeCollect(actualsz - oldsize, actualsz > oldsize);
                        result = Memory::osMalloc(actualsz);
                        if(result == nullptr)
                        {
                            fprintf(stderr, "fatal error: failed to allocate %zd bytes (size: %zd, count: %zd)\n", actualsz, tsize, count);
                            abort();
                        }
                        return result;
                    }

                    static void gcFreeMemory(State* state, void* pointer, size_t oldsize)
                    {
                        state->m_vmstate->gcMaybeCollect(-oldsize, false);
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
                        void* buf;
                        Type* rt;
                        buf = gcAllocMemory(state, sizeof(Type), 1);
                        memset(buf, 0, sizeof(Type));
                        rt = new(buf) Type(args...);
                        return rt;
                    }

                public:
                    State* m_pvm;
                    size_t m_stackidx = 0;
                    size_t m_stackcapacity = 0;
                    size_t m_framecapacity = 0;
                    size_t m_framecount = 0;
                    Instruction m_currentinstr;
                    CallFrame* m_currentframe = nullptr;
                    ScopeUpvalue* m_openupvalues = nullptr;
                    Object* m_linkedobjects = nullptr;
                    CallFrame* m_framevalues = nullptr;
                    Value* m_stackvalues = nullptr;
                    bool m_currentmarkvalue;
                    int m_gcgraycount;
                    int m_gcgraycapacity;
                    int m_gcbytesallocated;
                    int m_gcnextgc;
                    Object** m_gcgraystack;

                public:
                    VM(State* state): m_pvm(state)
                    {
                        size_t i;
                        size_t j;
                        m_linkedobjects = nullptr;
                        m_currentframe = nullptr;
                        {
                            m_stackcapacity = NEON_CFG_INITSTACKCOUNT;
                            m_stackvalues = (Value*)Memory::osMalloc(NEON_CFG_INITSTACKCOUNT * sizeof(Value));
                            if(m_stackvalues == nullptr)
                            {
                                fprintf(stderr, "error: failed to allocate stackvalues!\n");
                                abort();
                            }
                            //memset(m_stackvalues, 0, NEON_CFG_INITSTACKCOUNT * sizeof(Value));
                        }
                        {
                            m_framecapacity = NEON_CFG_INITFRAMECOUNT;
                            m_framevalues = (CallFrame*)Memory::osMalloc(NEON_CFG_INITFRAMECOUNT * sizeof(CallFrame));
                            if(m_framevalues == nullptr)
                            {
                                fprintf(stderr, "error: failed to allocate framevalues!\n");
                                abort();
                            }
                            memset(m_framevalues, 0, NEON_CFG_INITFRAMECOUNT * sizeof(CallFrame));
                            for(i=0; i<NEON_CFG_INITFRAMECOUNT; i++)
                            {
                                for(j=0; j<NEON_CFG_MAXEXCEPTHANDLERS; j++)
                                {
                                    m_framevalues[i].handlers[j].hasclass = false;
                                    m_framevalues[i].handlers[j].exklass = nullptr;
                                }
                            }
                        }
                    }

                    ~VM()
                    {
                    }

                    void setInstanceProperty(ClassInstance* instance, const char* propname, Value value);

                    template<typename... ArgsT>
                    bool raiseAtSourceLocation(ClassObject* raiseme, const char* format, ArgsT&&... args)
                    {
                        static auto fn_snprintf = snprintf;
                        int length;
                        int needed;
                        char* msgbuf;
                        Value stacktrace;
                        ClassInstance* instance;
                        /* TODO: used to be vasprintf. need to check how much to actually allocate! */
                        needed = fn_snprintf(NULL, 0, format, args...);
                        needed += 1;
                        msgbuf = (char*)Memory::osMalloc(needed+1);
                        length = fn_snprintf(msgbuf, needed, format, args...);
                        instance = m_pvm->makeExceptionInstance(raiseme, msgbuf, length, true);
                        stackPush(Value::fromObject(instance));
                        stacktrace = getExceptionStacktrace();
                        stackPush(stacktrace);
                        setInstanceProperty(instance, "stacktrace", stacktrace);
                        stackPop();
                        return exceptionPropagate();
                    }

                    template<typename... ArgsT>
                    bool raiseClass(ClassObject* raiseme, const char* format, ArgsT&&... args)
                    {
                        return raiseAtSourceLocation(raiseme, format, std::forward<ArgsT>(args)...);
                    }

                    template<typename... ArgsT>
                    bool raiseClass(const char* clname, const char* format, ArgsT&&... args)
                    {
                        ClassObject* tmp;
                        ClassObject* vdef;
                        vdef = m_pvm->m_exceptions.stdexception;
                        tmp = m_pvm->findExceptionByName(clname);
                        if(tmp != nullptr)
                        {
                            vdef = tmp;
                        }
                        return raiseAtSourceLocation(vdef, format, std::forward<ArgsT>(args)...);
                    }

                    template<typename ExClassT, typename... ArgsT>
                    Value raiseFromFunction(ExClassT exclass, ArgBase* fnargs, const char* fmt, ArgsT&&... args)
                    {
                        stackPop(fnargs->argc);
                        raiseClass(exclass, fmt, std::forward<ArgsT>(args)...);
                        return Value::makeBool(false);
                    }

                    template<typename... ArgsT>
                    Value raiseFromFunction(ArgBase* fnargs, const char* fmt, ArgsT&&... args)
                    {
                        return raiseFromFunction(m_pvm->m_exceptions.stdexception, fnargs, fmt, args...);
                    }

                    void gcCollectGarbage();
                    void gcTraceRefs();
                    void gcMarkRoots();
                    void gcSweep();
                    void gcDestroyLinkedObjects();

                    void resetVMState()
                    {
                        m_framecount = 0;
                        m_stackidx = 0;
                        m_openupvalues = nullptr;
                    }

                    template<typename SubObjT>
                    SubObjT* gcProtect(SubObjT* object)
                    {
                        size_t frpos;
                        stackPush(Value::fromObject(object));
                        frpos = 0;
                        if(m_framecount > 0)
                        {
                            frpos = m_framecount - 1;
                        }
                        m_framevalues[frpos].gcprotcount++;
                        return object;
                    }

                    void gcClearProtect()
                    {
                        size_t frpos;
                        CallFrame* frame;
                        frpos = 0;
                        if(m_framecount > 0)
                        {
                            frpos = m_framecount - 1;
                        }
                        frame = &m_framevalues[frpos];
                        if(frame->gcprotcount > 0)
                        {
                            m_stackidx -= frame->gcprotcount;
                        }
                        frame->gcprotcount = 0;
                    }

                    NEON_FORCEINLINE uint8_t readByte()
                    {
                        uint8_t r;
                        r = m_currentframe->inscode->code;
                        m_currentframe->inscode++;
                        return r;
                    }

                    NEON_FORCEINLINE Instruction readInstruction()
                    {
                        Instruction r;
                        r = *m_currentframe->inscode;
                        m_currentframe->inscode++;
                        return r;
                    }

                    NEON_FORCEINLINE uint16_t readShort()
                    {
                        uint8_t b;
                        uint8_t a;
                        a = m_currentframe->inscode[0].code;
                        b = m_currentframe->inscode[1].code;
                        m_currentframe->inscode += 2;
                        return (uint16_t)((a << 8) | b);
                    }

                    Value readConst();

                    NEON_FORCEINLINE String* readString()
                    {
                        return readConst().asString();
                    }

                    NEON_FORCEINLINE void stackPush(Value value)
                    {
                        checkMaybeResize();
                        m_stackvalues[m_stackidx] = value;
                        m_stackidx++;
                    }

                    NEON_FORCEINLINE Value stackPop()
                    {
                        Value v;
                        if(m_stackidx == 0)
                        {
                            return Value::makeEmpty();
                        }
                        m_stackidx--;
                        v = m_stackvalues[m_stackidx];
                        return v;
                    }

                    NEON_FORCEINLINE Value stackPop(int n)
                    {
                        Value v;
                        m_stackidx -= n;
                        v = m_stackvalues[m_stackidx];
                        return v;
                    }

                    NEON_FORCEINLINE Value stackPeek(int distance)
                    {
                        Value v;
                        v = m_stackvalues[m_stackidx + (-1 - distance)];
                        return v;
                    }

                    bool checkMaybeResize()
                    {
                        if((m_stackidx+1) >= m_stackcapacity)
                        {
                            if(!resizeStack(m_stackidx + 1))
                            {
                                return raiseClass(m_pvm->m_exceptions.stdexception, "failed to resize stack due to overflow");
                            }
                            return true;
                        }
                        if(m_framecount >= m_framecapacity)
                        {
                            if(!resizeFrames(m_framecapacity + 1))
                            {
                                return raiseClass(m_pvm->m_exceptions.stdexception, "failed to resize frames due to overflow");
                            }
                            return true;
                        }
                        return false;
                    }

                    /*
                    * grows m_(stack|frame)values, respectively.
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
                        if(((int)needed < 0) /* || (needed < m_stackcapacity) */)
                        {
                            return true;
                        }
                        nforvals = (needed * 2);
                        oldsz = m_stackcapacity;
                        newsz = oldsz + nforvals;
                        allocsz = ((newsz + 1) * sizeof(Value));
                        fprintf(stderr, "*** resizing stack: needed %zd (%zd), from %zd to %zd, allocating %zd ***\n", nforvals, needed, oldsz, newsz, allocsz);
                        oldbuf = m_stackvalues;
                        newbuf = (Value*)Memory::osRealloc(oldbuf, allocsz );
                        if(newbuf == nullptr)
                        {
                            fprintf(stderr, "internal error: failed to resize stackvalues!\n");
                            abort();
                        }
                        m_stackvalues = (Value*)newbuf;
                        fprintf(stderr, "oldcap=%zd newsz=%zd\n", m_stackcapacity, newsz);
                        m_stackcapacity = newsz;
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
                        CallFrame* oldbuf;
                        CallFrame* newbuf;
                        fprintf(stderr, "*** resizing frames ***\n");
                        oldclosure = m_currentframe->closure;
                        oldip = m_currentframe->inscode;
                        oldhandlercnt = m_currentframe->handlercount;
                        oldsz = m_framecapacity;
                        newsz = oldsz + needed;
                        allocsz = ((newsz + 1) * sizeof(CallFrame));
                        #if 1
                            oldbuf = m_framevalues;
                            newbuf = (CallFrame*)Memory::osRealloc(oldbuf, allocsz);
                            if(newbuf == nullptr)
                            {
                                fprintf(stderr, "internal error: failed to resize framevalues!\n");
                                abort();
                            }
                        #endif
                        m_framevalues = (CallFrame*)newbuf;
                        m_framecapacity = newsz;
                        /*
                        * this bit is crucial: realloc changes pointer addresses, and to keep the
                        * current frame, re-read it from the new address.
                        */
                        m_currentframe = &m_framevalues[m_framecount - 1];
                        m_currentframe->handlercount = oldhandlercnt;
                        m_currentframe->inscode = oldip;
                        m_currentframe->closure = oldclosure;
                        return true;
                    }

                    void gcMaybeCollect(int addsize, bool wasnew)
                    {
                        m_gcbytesallocated += addsize;
                        if(m_gcnextgc > 0)
                        {
                            if(wasnew && m_gcbytesallocated > m_gcnextgc)
                            {
                                if(m_currentframe != nullptr)
                                {
                                    if(m_currentframe->gcprotcount == 0)
                                    {
                                        gcCollectGarbage();
                                    }
                                }
                            }
                        }
                    }

                    bool exceptionPushHandler(ClassObject* type, int address, int finallyaddress)
                    {
                        CallFrame* frame;
                        frame = &m_framevalues[m_framecount - 1];
                        if(frame->handlercount == NEON_CFG_MAXEXCEPTHANDLERS)
                        {
                            m_pvm->raiseFatal("too many nested exception handlers in one function");
                            return false;
                        }
                        frame->handlers[frame->handlercount].address = address;
                        frame->handlers[frame->handlercount].finallyaddress = finallyaddress;
                        frame->handlers[frame->handlercount].exklass = type;
                        frame->handlercount++;
                        return true;
                    }

                    bool exceptionHandleUncaught(ClassInstance* exception);
                    Value getExceptionStacktrace();
                    bool exceptionPropagate();
                    
                    bool vmCallNative(FuncNative* native, Value thisval, int argcount);

                    bool vmCallBoundValue(Value callable, Value thisval, int argcount);

                    bool callClosure(FuncClosure* closure, Value thisval, int argcount);

                    bool vmCallValue(Value callable, Value thisval, int argcount);

                    bool vmInvokeMethodFromClass(ClassObject* klass, String* name, int argcount);

                    bool vmInvokeSelfMethod(String* name, int argcount);

                    bool vmInvokeMethod(String* name, int argcount);

                    bool vmBindMethod(ClassObject* klass, String* name);

                    Status runVM(int exitframe, Value* rv);

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
            ClassObject* m_classprimcallable;
            ClassObject* m_classprimmath;

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

            ClassInstance* makeExceptionInstance(ClassObject* ofklass, char* msgbuf, size_t len, bool takestr);
            
            ClassObject* findExceptionByName(const char* name);

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
            FuncClosure* compileSource(Module* module, const char* source);

            FuncClosure* readBinaryFile(const char* filename);

            Status execFromClosure(FuncClosure* fn, Value* dest);            
            Status execSource(Module* module, const char* source, Value* dest);

            bool runCode(char* source)
            {
                Status result;
                m_rootphysfile = nullptr;
                result = execSource(m_toplevelmodule, source, nullptr);
                fflush(stdout);
                if(result == Status::FAIL_COMPILE)
                {
                    return false;
                }
                if(result == Status::FAIL_RUNTIME)
                {
                    return false;
                }
                return true;
            }

            FuncClosure* compileSourceToClosure(const char* source, size_t len, const char* filename);

            FuncClosure* compileFileToClosure(const char* file)
            {
                size_t fsz;
                char* source;
                const char* oldfile;
                FuncClosure* closure;
                source = Util::fileReadFile(this, file, &fsz);
                if(source == nullptr)
                {
                    oldfile = file;
                    source = Util::fileReadFile(this, file, &fsz);
                    if(source == nullptr)
                    {
                        fprintf(stderr, "failed to read from '%s': %s\n", oldfile, strerror(errno));
                        return nullptr;
                    }
                }
                closure = compileSourceToClosure(source, fsz, file);
                Memory::osFree(source);
                return closure;
            }

            bool runFile(const char* file)
            {
                Status result;
                FuncClosure* closure;
                closure = compileFileToClosure(file);
                result = execFromClosure(closure, nullptr);
                fflush(stdout);
                if(result == Status::FAIL_COMPILE)
                {
                    return false;
                }
                if(result == Status::FAIL_RUNTIME)
                {
                    return false;
                }
                return true;
            }

            void defGlobalValue(const char* name, Value val);
            void defNativeFunction(const char* name, NativeCallbackFN fptr);

            bool addSearchPathObjString(String* os);
            bool addSearchPath(const char* path);

            ClassObject* makeNamedClass(const char* name, ClassObject* parent);
    };

    // better name: CallState?
    struct CallState: public ArgBase
    {
        public:
            NEON_FORCEINLINE CallState(State* state, const char* n, Value tv, Value* vargv, int vargc, void* uptr)
            {
                this->m_pvm = state;
                this->funcname = n;
                this->thisval = tv;
                this->argc = vargc;
                this->argv = vargv;
                this->userptr = uptr;
            }

            NEON_FORCEINLINE Value at(size_t pos)
            {
                return this->argv[pos];
            }

            /* check for exact number of arguments $d */
            NEON_FORCEINLINE bool checkCount(size_t d)
            {
                if(this->argc != int(d))
                {
                    m_pvm->m_vmstate->raiseFromFunction("ArgumentError", this, "%s() expects %d arguments, %d given", this->funcname, d, this->argc);
                    return false; 
                }
                return true;
            }

            template<typename Func>
            NEON_FORCEINLINE bool checkByFunc(size_t i, Func fn)
            {
                /*
                if(!checkCount(i+1))
                {
                    return false;
                }
                */
                if(!(this->argv[i].*fn)())
                {
                    auto gtn = Value::typenameFromFuncPtr(fn);
                    auto vtn = this->argv[i].name();
                    m_pvm->m_vmstate->raiseFromFunction("ArgumentError", this, "%s() expects argument %d as %s, %s given", this->funcname, i+1, gtn, vtn);
                    return false;
                }
                return true;
            }

            template<typename EnumT>
            NEON_FORCEINLINE bool checkType(size_t i, EnumT t)
            {
                /*
                if(!checkCount(i+1))
                {
                    return false;
                }
                */
                if(!this->argv[i].isType(t))
                {
                    auto gtn = Value::typenameFromEnum(t);
                    auto vtn = this->argv[i].name();
                    m_pvm->m_vmstate->raiseFromFunction(this, "%s() expects argument %d as %s, %s given", this->funcname, i+1, gtn, vtn);
                    return true;
                }
                return true;
            }


    };


    struct Object
    {
        public:
            template<typename SubObjT, class = std::enable_if_t<std::is_base_of<Object, SubObjT>::value>>
            static SubObjT* initBasicObject(State* state, Value::ObjType type)
            {
                size_t size;
                Object* plain;
                (void)size;
                (void)plain;
                size = sizeof(SubObjT);
                plain = State::VM::gcCreate<SubObjT>(state);
                plain->m_objtype = type;
                plain->m_mark = !state->m_vmstate->m_currentmarkvalue;
                plain->m_isstale = false;
                plain->m_pvm = state;
                plain->m_nextobj = state->m_vmstate->m_linkedobjects;
                state->m_vmstate->m_linkedobjects = plain;
                #if defined(NEON_CFG_DEBUGGC) && NEON_CFG_DEBUGGC
                state->m_debugprinter->putformat("%p allocate %zd for %d\n", (void*)plain, size, type);
                #endif
                return static_cast<SubObjT*>(plain);
            }

            template<typename SubObjT, class = std::enable_if_t<std::is_base_of<Object, SubObjT>::value>>
            static void release(State* state, SubObjT* ptr)
            {
                if(ptr != nullptr)
                {
                    ptr->~SubObjT();
                    ptr->m_objtype = (Value::ObjType)0;
                    ptr->m_nextobj = nullptr;
                    State::VM::gcFreeMemory(state, ptr, sizeof(SubObjT));
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
    };

    template<typename DataValueT>
    struct PropBase
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
            Type m_proptype = PROPTYPE_INVALID;
            bool m_havegetset = false;
            DataValueT m_actualval;
            GetSetter m_getset;

        public:
            inline void init(DataValueT val, Type t)
            {
                m_proptype = t;
                m_actualval = val;
                m_havegetset = false;
            }

            inline PropBase()
            {
            }

            inline PropBase(DataValueT val, Value getter, Value setter, Type t)
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

            inline PropBase(DataValueT val, Type t)
            {
                init(val, t);
            }

            inline DataValueT value() const
            {
                return m_actualval;
            }

    };

    struct Property: public PropBase<Value>
    {
        public:
            using PropBase::PropBase;
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
                Entry* entry;
                Entry* tombstone;
                hash = Value::getHash(key);
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "looking for key ");
                m_pvm->m_debugprinter->printValue(key, true, false);
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
                    else if(key.compare(entry->key))
                    {
                        return entry;
                    }
                    index = (index + 1) & (availcap - 1);
                }
                return nullptr;
            }

            bool compareEntryString(Value vkey, const char* kstr, size_t klen)
            {
                size_t oslen;
                const char* osdata;
                String* entoskey;
                entoskey = vkey.asString();
                oslen = Helper::getStringLength(entoskey);
                osdata = Helper::getStringData(entoskey);
                if(oslen == klen)
                {
                    if(memcmp(kstr, osdata, klen) == 0)
                    {
                        return true;
                    }
                }
                return false;
            }

            Entry* findEntryByStr(Entry* availents, int availcap, Value valkey, const char* kstr, size_t klen, uint32_t hash)
            {
                bool havevalhash;
                uint32_t index;
                uint32_t valhash;
                Entry* entry;
                Entry* tombstone;
                (void)valhash;
                (void)havevalhash;
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "looking for key ");
                m_pvm->m_debugprinter->printValue(key, true, false);
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
                            if(valkey.compare(entry->key))
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
                Entry* entry;
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
                m_pvm->m_debugprinter->printValue(entry->value.m_actualval, true, false);
                fprintf(stderr, "\n");
                #endif
                return &entry->value;
            }

            Property* getFieldByStr(Value valkey, const char* kstr, size_t klen, uint32_t hash)
            {
                Entry* entry;
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
                m_pvm->m_debugprinter->printValue(entry->value.m_actualval, true, false);
                fprintf(stderr, "\n");
                #endif
                return &entry->value;
            }

            Property* getByObjString(String* str)
            {
                uint32_t oshash;
                size_t oslen;
                const char* osdata;
                oslen = Helper::getStringLength(str);
                osdata = Helper::getStringData(str);
                oshash = Helper::getStringHash(str);
                return getFieldByStr(Value::makeEmpty(), osdata, oslen, oshash);
            }

            Property* getByCStr(const char* kstr)
            {
                size_t klen;
                uint32_t hash;
                klen = strlen(kstr);
                hash = Util::hashString(kstr, klen);
                return getFieldByStr(Value::makeEmpty(), kstr, klen, hash);
            }

            Property* getField(Value key)
            {
                uint32_t oshash;
                size_t oslen;
                const char* osdata;
                String* oskey;
                if(key.isString())
                {
                    oskey = key.asString();
                    osdata = Helper::getStringData(oskey);
                    oslen = Helper::getStringLength(oskey);
                    oshash = Helper::getStringHash(oskey);
                    return getFieldByStr(key, osdata, oslen, oshash);
                }
                return getFieldByValue(key);
            }

            void adjustCapacity(uint64_t needcap)
            {
                uint64_t i;
                Entry* dest;
                Entry* entry;
                Entry* nents;
                //nents = (Entry*)State::VM::gcAllocMemory(m_pvm, sizeof(Entry), needcap);
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

            bool setCStrType(const char* cstrkey, Value value, Property::Type ftype)
            {
                String* os;
                os = Helper::copyObjString(m_pvm, cstrkey);
                return setType(Value::fromObject(os), value, ftype, true);
            }

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

            void addAll(HashTable* from)
            {
                uint64_t i;
                Entry* entry;
                for(i = 0; i < from->m_capacity; i++)
                {
                    entry = &from->m_entries[i];
                    if(!entry->key.isEmpty())
                    {
                        setType(entry->key, entry->value.m_actualval, entry->value.m_proptype, false);
                    }
                }
            }

            void copyFrom(HashTable* from)
            {
                uint64_t i;
                Entry* entry;
                for(i = 0; i < from->m_capacity; i++)
                {
                    entry = &from->m_entries[i];
                    if(!entry->key.isEmpty())
                    {
                        setType(entry->key, Value::copyValue(entry->value.m_actualval), entry->value.m_proptype, false);
                    }
                }
            }

            String* findString(const char* chars, size_t length, uint32_t hash)
            {
                size_t slen;
                uint32_t shash;
                uint32_t index;
                const char* sdata;
                Entry* entry;
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
                    slen = Helper::getStringLength(string);
                    sdata = Helper::getStringData(string);
                    shash = Helper::getStringHash(string);
                    if((slen == length) && (shash == hash) && memcmp(sdata, chars, length) == 0)
                    {
                        /* we found it */
                        return string;
                    }
                    index = (index + 1) & (m_capacity - 1);
                }
            }

            Value findKey(Value value)
            {
                uint64_t i;
                Entry* entry;
                for(i = 0; i < m_capacity; i++)
                {
                    entry = &m_entries[i];
                    if(!entry->key.isNull() && !entry->key.isEmpty())
                    {
                        if(entry->value.m_actualval.compare(value))
                        {
                            return entry->key;
                        }
                    }
                }
                return Value::makeNull();
            }

            void markTableValues()
            {
                uint64_t i;
                Entry* entry;
                if(!m_active)
                {
                    m_pvm->warn("trying to mark inactive hashtable <%p>!", this);
                    return;
                }
                for(i = 0; i < m_capacity; i++)
                {
                    entry = &m_entries[i];
                    if(entry != nullptr)
                    {
                        Object::markValue(m_pvm, entry->key);
                        Object::markValue(m_pvm, entry->value.m_actualval);
                    }
                }
            }

            void removeMarkedValues()
            {
                uint64_t i;
                Entry* entry;
                for(i = 0; i < m_capacity; i++)
                {
                    entry = &m_entries[i];
                    if(entry->key.isObject() && entry->key.asObject()->m_mark != m_pvm->m_vmstate->m_currentmarkvalue)
                    {
                        removeByKey(entry->key);
                    }
                }
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
                rs = Object::initBasicObject<String>(state, Value::OBJTYPE_STRING);
                rs->m_sbuf = sbuf;
                rs->m_hash = hash;
                //fprintf(stderr, "String: sbuf(%zd)=\"%.*s\"\n", sbuf->m_length, (int)sbuf->m_length, sbuf->data());
                selfCachePut(state, rs);
                return rs;
            }

            static String* makeFromStrbuf(State* state, Util::StrBuffer* sbuf)
            {   
                uint32_t h;
                h = Util::hashString(sbuf->data(), sbuf->length());
                return makeFromStrbuf(state, sbuf, h);
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

            static String* make(State* state)
            {
                return copy(state, "", 0);
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
            ~String()
            {
                if(m_sbuf != nullptr)
                {
                    Memory::destroy(m_sbuf);
                    m_sbuf = nullptr;
                }
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

            NEON_FORCEINLINE bool compare(const char* str, size_t len) const
            {
                return m_sbuf->compare(str, len);
            }

            NEON_FORCEINLINE bool compare(String* other) const
            {
                return compare(other->m_sbuf->m_data, other->m_sbuf->m_length);
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
                //raw = (char*)State::VM::gcAllocMemory(m_pvm, sizeof(char), asz);
                raw = (char*)Memory::osMalloc(sizeof(char) * asz);
                memset(raw, 0, asz);
                memcpy(raw, data() + start, len);
                return String::take(m_pvm, raw, len);
            }

            void append(char ch)
            {
                m_sbuf->append(ch);
            }

            void append(const char* str, size_t len)
            {
                m_sbuf->append(str, len);

            }

            void append(String* os)
            {
                append(os->m_sbuf->m_data, os->m_sbuf->m_length);
            }


    };

    struct ScopeUpvalue final: public Object
    {
        public:
            static ScopeUpvalue* make(State* state, Value* slot, int stackpos)
            {
                ScopeUpvalue* rt;
                rt = Object::initBasicObject<ScopeUpvalue>(state, Value::OBJTYPE_UPVALUE);
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
            ~ScopeUpvalue()
            {
            }
    };

    struct Array final: public Object
    {
        public:
            static Array* make(State* state, uint64_t cnt, Value filler)
            {
                uint64_t i;
                Array* rt;
                rt = Object::initBasicObject<Array>(state, Value::OBJTYPE_ARRAY);
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
            ~Array()
            {
                Memory::destroy(m_varray);
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
                rt = Object::initBasicObject<Range>(state, Value::OBJTYPE_RANGE);
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
            ~Range()
            {
            }
    };

    struct Dictionary final: public Object
    {
        public:
            static Dictionary* make(State* state)
            {
                Dictionary* rt;
                rt = Object::initBasicObject<Dictionary>(state, Value::OBJTYPE_DICT);
                rt->m_keynames = Memory::create<ValArray>();
                rt->m_valtable = Memory::create<HashTable>(state);
                return rt;
            }

        public:
            ValArray* m_keynames;
            HashTable* m_valtable;

        public:
            ~Dictionary()
            {
                Memory::destroy(m_keynames);
                Memory::destroy(m_valtable);
            }

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
                rt = Object::initBasicObject<File>(state, Value::OBJTYPE_FILE);
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
                Object::markObject(state, file->m_filemode);
                Object::markObject(state, file->m_filepath);
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
            ~File()
            {
                selfCloseFile();
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
                        m_istty = (osfn_isatty(m_filedesc) != 0);
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
                rt = Object::initBasicObject<VarSwitch>(state, Value::OBJTYPE_SWITCH);
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
            ~VarSwitch()
            {
                Memory::destroy(m_jumppositions);
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
                rt = Object::initBasicObject<Userdata>(state, Value::OBJTYPE_USERDATA);
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
            ~Userdata()
            {
                if(m_fnondestroy != nullptr)
                {
                    m_fnondestroy(m_udpointer);
                }
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
                rt = Object::initBasicObject<FuncScript>(state, Value::OBJTYPE_FUNCSCRIPT);
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
            ~FuncScript()
            {
                Memory::destroy(m_compiledblob);
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
                //upvals = (ScopeUpvalue**)State::VM::gcAllocMemory(state, sizeof(ScopeUpvalue*), function->m_upvalcount);
                upvals = (ScopeUpvalue**)Memory::osMalloc(sizeof(ScopeUpvalue*) * function->m_upvalcount);
                for(i = 0; i < function->m_upvalcount; i++)
                {
                    upvals[i] = nullptr;
                }
                rt = Object::initBasicObject<FuncClosure>(state, Value::OBJTYPE_FUNCCLOSURE);
                rt->m_scriptfunc = function;
                rt->m_storedupvals = upvals;
                rt->m_upvalcount = function->m_upvalcount;
                return rt;
            }

        public:
            int m_upvalcount;
            FuncScript* m_scriptfunc;
            ScopeUpvalue** m_storedupvals;

        public:
            ~FuncClosure()
            {
                Memory::freeArray(m_storedupvals, m_upvalcount);
                /*
                // there may be multiple closures that all reference the same function
                // for this reason, we do not free functions when freeing closures
                */
            }
    };

    struct FuncBound final: public FuncCommon
    {
        public:
            static FuncBound* make(State* state, Value receiver, FuncClosure* method)
            {
                FuncBound* rt;
                rt = Object::initBasicObject<FuncBound>(state, Value::OBJTYPE_FUNCBOUND);
                rt->receiver = receiver;
                rt->method = method;
                return rt;
            }

        public:
            Value receiver;
            FuncClosure* method;

        public:
            ~FuncBound()
            {
            }
    };

    struct FuncNative final: public FuncCommon
    {
        public:
            static FuncNative* make(State* state, NativeCallbackFN function, const char* name, void* uptr)
            {
                FuncNative* rt;
                rt = Object::initBasicObject<FuncNative>(state, Value::OBJTYPE_FUNCNATIVE);
                rt->m_natfunc = function;
                rt->m_nativefnname = name;
                rt->m_userptrforfn = uptr;
                rt->m_functype = FuncCommon::FUNCTYPE_FUNCTION;
                return rt;
            }

            static FuncNative* make(State* state, NativeCallbackFN function, const char* name)
            {
                return make(state, function, name, nullptr);
            }

        public:
            const char* m_nativefnname;
            NativeCallbackFN m_natfunc;
            void* m_userptrforfn;

        public:
            ~FuncNative()
            {
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
                    m_arity = closure->m_scriptfunc->m_arity;
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
                bool b;
                Status status;
                pidx = m_pvm->m_vmstate->m_stackidx;
                /* set the closure before the args */
                m_pvm->m_vmstate->stackPush(callable);
                argc = 0;
                vsz = args.size();
                argc = m_arity;
                if(vsz != 0u)
                {
                    for(i = 0; i < vsz; i++)
                    {
                        m_pvm->m_vmstate->stackPush(args.at(i));
                    }
                }
                b = false;
                if(callable.isFuncNative())
                {
                    fprintf(stderr, "calling native...\n");
                    b = m_pvm->m_vmstate->vmCallValue(callable, thisval, argc);
                }
                else
                {
                    b = m_pvm->m_vmstate->vmCallBoundValue(callable, thisval, argc);
                }
                if(!b)
                {
                    fprintf(stderr, "nestcall: vmCallValue() (argc=%zd) failed\n", argc);
                    abort();
                }
                status = m_pvm->m_vmstate->runVM(m_pvm->m_vmstate->m_framecount - 1, nullptr);
                if(status != Status::OK)
                {
                    fprintf(stderr, "nestcall: call to runvm (argc=%zd) failed\n", argc);
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
            static ClassObject* make(State* state, const Util::StrBuffer& name, ClassObject* parent)
            {
                ClassObject* rt;
                rt = Object::initBasicObject<ClassObject>(state, Value::OBJTYPE_CLASS);
                rt->m_classname = name;
                rt->m_instprops = Memory::create<HashTable>(state);
                rt->m_staticprops = Memory::create<HashTable>(state);
                rt->m_classmethods = Memory::create<HashTable>(state);
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
                if(expected == nullptr)
                {
                    return false;
                }
                if(klass1 == expected)
                {
                    return true;
                }
                while(klass1 != nullptr)
                {
                    elen = expected->m_classname.length();
                    klen = klass1->m_classname.length();
                    ename = expected->m_classname.data();
                    kname = klass1->m_classname.data();
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
            * read from $m_classmethods as-is. this includes adding methods!
            * TODO: introduce a new hashtable field for ClassInstance for unique methods, perhaps?
            * right now, this method just prevents unnecessary copying.
            */
            HashTable* m_classmethods;
            HashTable* m_staticmethods;
            Util::StrBuffer m_classname;
            ClassObject* m_superclass;

        public:
            ~ClassObject()
            {
                Memory::destroy(m_classmethods);
                Memory::destroy(m_staticmethods);
                Memory::destroy(m_instprops);
                Memory::destroy(m_staticprops);
            }

            void inheritFrom(ClassObject* superclass)
            {
                m_instprops->addAll(superclass->m_instprops);
                m_classmethods->addAll(superclass->m_classmethods);
                m_superclass = superclass;
            }

            void defProperty(const char* cstrname, Value val)
            {
                m_instprops->setCStr(cstrname, val);
            }

            void defCallableField(const char* cstrname, NativeCallbackFN function)
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

            void defNativeConstructor(NativeCallbackFN function)
            {
                const char* cname;
                FuncNative* ofn;
                cname = "constructor";
                ofn = FuncNative::make(m_pvm, function, cname);
                m_constructor = Value::fromObject(ofn);
            }

            void defMethod(const char* name, Value val)
            {
                m_classmethods->setCStr(name, val);
            }

            void defNativeMethod(const char* name, NativeCallbackFN function)
            {
                FuncNative* ofn;
                ofn = FuncNative::make(m_pvm, function, name);
                defMethod(name, Value::fromObject(ofn));
            }

            void defStaticNativeMethod(const char* name, NativeCallbackFN function)
            {
                FuncNative* ofn;
                ofn = FuncNative::make(m_pvm, function, name);
                m_staticmethods->setCStr(name, Value::fromObject(ofn));
            }

            Property* getMethodField(String* name)
            {
                Property* field;
                field = m_classmethods->getField(Value::fromObject(name));
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
                rt = Object::initBasicObject<ClassInstance>(state, Value::OBJTYPE_INSTANCE);
                /* gc fix */
                rt->m_active = true;
                rt->m_fromclass = klass;
                rt->m_properties = Memory::create<HashTable>(state);
                if(rt->m_fromclass->m_instprops->size() > 0)
                {
                    rt->m_properties->copyFrom(rt->m_fromclass->m_instprops);
                }
                return rt;
            }

            static void mark(State* state, ClassInstance* instance)
            {
                if(!instance->m_active)
                {
                    state->warn("trying to mark inactive instance <%p>!", instance);
                    return;
                }
                Object::markObject(state, instance->m_fromclass);
                instance->m_properties->markTableValues();
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
            ~ClassInstance()
            {
                Memory::destroy(m_properties);
                m_properties = nullptr;
                m_active = false;
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
                NativeCallbackFN function;
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
                rt = Object::initBasicObject<Module>(state, Value::OBJTYPE_MODULE);
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

            static char* resolvePath(State* state, const char* modulename, const char* currentfile, const char* rootfile, bool isrelative)
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
                state->m_loadedmodules->set(name, Value::fromObject(module));
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
                    state->m_vmstate->raiseClass(state->m_exceptions.stdexception, "module not found: '%s'\n", modulename->data());
                    return nullptr;
                }
                fprintf(stderr, "loading module from '%s'\n", physpath);
                source = Util::fileReadFile(state, physpath, &fsz);
                if(source == nullptr)
                {
                    state->m_vmstate->raiseClass(state->m_exceptions.stdexception, "could not read import file %s", physpath);
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
                    state->m_vmstate->raiseClass(state->m_exceptions.stdexception, "failed to call compiled import closure");
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
                            targetmod->m_deftable->set(fieldname, v);
                        }
                    }
                    if(regmod->functions != nullptr)
                    {
                        for(j = 0; regmod->functions[j].funcname != nullptr; j++)
                        {
                            func = regmod->functions[j];
                            funcname = Value::fromObject(state->m_vmstate->gcProtect(String::copy(state, func.funcname)));
                            funcrealvalue = Value::fromObject(state->m_vmstate->gcProtect(FuncNative::make(state, func.function, func.funcname)));
                            targetmod->m_deftable->set(funcname, funcrealvalue);
                        }
                    }
                    if(regmod->classes != nullptr)
                    {
                        for(j = 0; regmod->classes[j].classname != nullptr; j++)
                        {
                            klassreg = regmod->classes[j];
                            klass = state->m_vmstate->gcProtect(ClassObject::make(state, klassreg.classname, nullptr));
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
                                    if(func.isstatic)
                                    {
                                        klass->m_staticmethods->set(funcname, Value::fromObject(native));
                                    }
                                    else
                                    {
                                        klass->m_classmethods->set(funcname, Value::fromObject(native));
                                    }
                                }
                            }
                            if(klassreg.fields != nullptr)
                            {
                                for(k = 0; klassreg.fields[k].fieldname != nullptr; k++)
                                {
                                    field = klassreg.fields[k];
                                    fieldname = Value::fromObject(state->m_vmstate->gcProtect(String::copy(state, field.fieldname)));
                                    v = field.fieldvalfn(state);
                                    tabdest = klass->m_instprops;
                                    if(field.isstatic)
                                    {
                                        tabdest = klass->m_staticprops;
                                    }
                                    tabdest->set(fieldname, v);
                                }
                            }
                            auto osname = String::copy(state, klassreg.classname);
                            targetmod->m_deftable->set(Value::fromObject(osname), Value::fromObject(klass));
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
            ~Module()
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
                        putformat(" [%zd items]", vsz);
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
                        putformat(" [%zd items]", dsz);
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
                String* os;
                ValArray args;
                if(invmethod)
                {
                    field = instance->m_fromclass->m_classmethods->getByCStr("toString");
                    if(field != nullptr)
                    {
                        NestCall nc(m_pvm);
                        thisval = Value::fromObject(instance);
                        arity = nc.prepare(field->m_actualval, thisval, args);
                        fprintf(stderr, "arity = %d\n", arity);
                        if(nc.callNested(field->m_actualval, thisval, args, &resv))
                        {
                            Printer subp(m_pvm, &subw);
                            subp.printValue(resv, false, false);
                            os = subp.takeString();
                            put(os->data(), os->length());
                            return;
                        }
                    }
                }
                #endif
                putformat("<instance of %s at %p>", instance->m_fromclass->m_classname.data(), (void*)instance);
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
                            Userdata* ud;
                            ud = static_cast<Userdata*>(obj); 
                            putformat("<userdata %s>", ud->m_udtypename);
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
                            printObjFunction(bn->method->m_scriptfunc);
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
                            putformat("<class %s at %p>", klass->m_classname.data(), (void*)klass);
                        }
                        break;
                    case Value::OBJTYPE_FUNCCLOSURE:
                        {
                            FuncClosure* cls;
                            cls = static_cast<FuncClosure*>(obj);
                            printObjFunction(cls->m_scriptfunc);
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
                    default:
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
                String* os;
                hash = Util::hashString(m_ptostring->data(), m_ptostring->length());
                os = String::makeFromStrbuf(m_pvm, m_ptostring, hash);
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
            static inline bool formatArgs(Printer* pr, const char* fmt, const VargT&... args)
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
                                m_pvm->m_vmstate->raiseClass(m_pvm->m_exceptions.stdexception, "unknown/invalid format flag '%%c'", st.nextchar);
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
                    if(islocal != 0u)
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
            bool isglobal = false;
            bool iskeyident = false;
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
                buf = (char*)State::VM::gcAllocMemory(m_pvm, sizeof(char), 1024);
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
                tok.iskeyident = true;
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

    #include "parser.h"

    #include "ser.h"

    FuncScript* Module::compileModuleSource(State* state, Module* module, const char* source, Blob* blob, bool fromimport, bool keeplast)
    {
        return Parser::compileSource(state, module, source, blob, fromimport, keeplast);
    }

    void ValArray::gcMark(State* state)
    {
        uint64_t i;
        for(i = 0; i < m_count; i++)
        {
            Object::markValue(state, m_values[i]);
        }
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
                    module->m_deftable->markTableValues();
                }
                break;
            case Value::OBJTYPE_SWITCH:
                {
                    VarSwitch* sw;
                    sw = static_cast<VarSwitch*>(object);
                    sw->m_jumppositions->markTableValues();
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
                    dict->m_valtable->markTableValues();
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
                    Object::markObject(state, bound->method);
                }
                break;
            case Value::OBJTYPE_CLASS:
                {
                    ClassObject* klass;
                    klass = static_cast<ClassObject*>(object);
                    //Object::markObject(state, klass->m_classname);
                    klass->m_classmethods->markTableValues();
                    klass->m_staticmethods->markTableValues();
                    klass->m_staticprops->markTableValues();
                    Object::markValue(state, klass->m_constructor);
                    if(klass->m_superclass != nullptr)
                    {
                        Object::markObject(state, klass->m_superclass);
                    }
                }
                break;
            case Value::OBJTYPE_FUNCCLOSURE:
                {
                    int i;
                    FuncClosure* closure;
                    closure = static_cast<FuncClosure*>(object);
                    Object::markObject(state, closure->m_scriptfunc);
                    for(i = 0; i < closure->m_upvalcount; i++)
                    {
                        Object::markObject(state, closure->m_storedupvals[i]);
                    }
                }
                break;
            case Value::OBJTYPE_FUNCSCRIPT:
                {
                    FuncScript* function;
                    function = static_cast<FuncScript*>(object);
                    Object::markObject(state, function->m_scriptfnname);
                    Object::markObject(state, function->m_inmodule);
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
                    ScopeUpvalue* sco;
                    sco = static_cast<ScopeUpvalue*>(object);
                    Object::markValue(state, sco->m_closed);
                }
                break;
            case Value::OBJTYPE_RANGE:
            case Value::OBJTYPE_FUNCNATIVE:
            case Value::OBJTYPE_USERDATA:
            case Value::OBJTYPE_STRING:
                break;
            default:
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

    bool ValArray::QuickSort::getObjNumberVal(Value val, size_t* dest)
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

    bool Value::isLesserObject(Value self, Value other, bool defval)
    {
        String* osself;
        String* osother;   
        if(other.isString() && self.isString())
        {
            osother = other.asString();
            osself = self.asString();
            return osother->length() < osself->length();
        }
        else if(other.isFuncScript() && self.isFuncScript())
        {
            return (other.asFuncScript()->m_arity < self.asFuncScript()->m_arity);
        }
        else if(other.isFuncClosure() && self.isFuncClosure())
        {
            return (other.asFuncClosure()->m_scriptfunc->m_arity < self.asFuncClosure()->m_scriptfunc->m_arity);
        }
        else if(other.isRange() && self.isRange())
        {
            return (other.asRange()->m_lower < self.asRange()->m_lower);
        }
        else if(other.isClass() && self.isClass())
        {
            return (other.asClass()->m_classmethods->count() < self.asClass()->m_classmethods->count());
        }
        else if(other.isArray() && self.isArray())
        {
            return (other.asArray()->size() < self.asArray()->size());
        }
        else if(other.isDict() && self.isDict())
        {
            return (other.asDict()->m_keynames->m_count < self.asDict()->m_keynames->m_count);
            
        }
        /*
        else if(self.isObject())
        {
            return (other.asObject()->m_objtype < self.asObject()->m_objtype);
        }
        */
        return defval;
    }

    Value Value::copyValue(Value value)
    {
        if(value.isObject())
        {
            switch(value.asObject()->m_objtype)
            {
                case Value::OBJTYPE_STRING:
                    {
                        String* string;
                        string = value.asString();
                        auto state = string->m_pvm;
                        return Value::fromObject(String::copy(state, string->data(), string->length()));
                    }
                    break;
                case Value::OBJTYPE_ARRAY:
                {
                    size_t i;
                    Array* list;
                    Array* newlist;
                    list = value.asArray();
                    auto state = list->m_pvm;
                    newlist = Array::make(state);
                    for(i = 0; i < list->size(); i++)
                    {
                        newlist->push(list->at(i));
                    }
                    return Value::fromObject(newlist);
                }
                /*
                case Value::OBJTYPE_DICT:
                    {
                        Dictionary *dict;
                        Dictionary *newdict;
                        dict = value.asDict();
                        auto state = dict->m_pvm;
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
                    ClassObject* klass;
                    klass = static_cast<ClassObject*>(object);
                    /* Classes just use their name. */
                    return Util::hashString(klass->m_classname.data(), klass->m_classname.length());
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
                    String* str;
                    str = static_cast<String*>(object);
                    return str->m_hash;
                }
                break;
            default:
                break;
        }
        return 0;
    }

    const char* Value::objClassTypename(Object* obj)
    {
        ClassInstance* inst;
        inst = static_cast<ClassInstance*>(obj);
        return inst->m_fromclass->m_classname.data();
    }

    String* Value::toString(State* state, Value value)
    {
        String* s;
        Printer pd(state);
        pd.printValue( value, false, true);
        s = pd.takeString();
        return s;
    }

    bool Value::compareObject(Value other) const
    {
        size_t i;
        Value::ObjType typself;
        Value::ObjType typother;
        Object* selfobj;
        Object* othobj;
        String* strself;
        String* strother;
        Array* arrself;
        Array* arrother;
        selfobj = asObject();
        othobj = other.asObject();
        typself = selfobj->m_objtype;
        typother = othobj->m_objtype;
        if(typself == typother)
        {
            if(typself == Value::OBJTYPE_STRING)
            {
                strself = (String*)selfobj;
                strother = (String*)othobj;
                if(strself->length() == strother->length())
                {
                    return memcmp(strself->data(), strother->data(), strself->length()) == 0;
                }
            }
            if(typself == Value::OBJTYPE_ARRAY)
            {
                arrself = (Array*)selfobj;
                arrother = (Array*)othobj;
                if(arrself->size() == arrother->size())
                {
                    for(i=0; i<(size_t)arrself->size(); i++)
                    {
                        if(!arrself->at(i).compare(arrother->at(i)))
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

    Value::ObjType Value::objectType(Value val)
    {
        return val.asObject()->m_objtype;
    }

    namespace Helper
    {
        size_t getStringLength(String* os)
        {
            return os->length();
        }

        const char* getStringData(String* os)
        {
            return os->data();
        }

        uint32_t getStringHash(String* os)
        {
            return os->m_hash;
        }

        String* copyObjString(State* state, const char* str)
        {
            return String::copy(state, str);
        }

        String* copyObjString(State* state, const char* str, size_t len)
        {
            return String::copy(state, str, len);
        }

        /*
        void printTableTo(Printer* pd, HashTable* table, const char* name)
        {
            size_t i;
            HashTable::Entry* entry;
            pd->putformat("<HashTable of %s : {\n", name);
            for(i = 0; i < table->m_capacity; i++)
            {
                entry = &table->m_entries[i];
                if(!entry->key.isEmpty())
                {
                    pd->printValue(entry->key, true, true);
                    pd->putformat(": ");
                    pd->printValue(entry->value.m_actualval, true, true);
                    if(i != table->m_capacity - 1)
                    {
                        pd->putformat(",\n");
                    }
                }
            }
            pd->putformat("}>\n");
        }
        */
    }

    FuncCommon::Type FuncCommon::getMethodType(Value method)
    {
        switch(method.objectType())
        {
            case Value::OBJTYPE_FUNCNATIVE:
                return method.asFuncNative()->m_functype;
            case Value::OBJTYPE_FUNCCLOSURE:
                return method.asFuncClosure()->m_scriptfunc->m_functype;
            default:
                break;
        }
        return FUNCTYPE_FUNCTION;
    }

    Value State::VM::readConst()
    {
        uint16_t idx;
        Blob* bl;
        idx = readShort();
        bl = m_currentframe->closure->m_scriptfunc->m_compiledblob;
        return bl->m_constants->m_values[idx];
    }


    bool State::VM::callClosure(FuncClosure* closure, Value thisval, int argcount)
    {
        int i;
        int startva;
        CallFrame* frame;
        Array* argslist;
        NEON_APIDEBUG(m_pvm, "thisval.type=%s, argcount=%d", thisval.name(), argcount);
        /* fill empty parameters if not variadic */
        for(; !closure->m_scriptfunc->m_isvariadic && argcount < closure->m_scriptfunc->m_arity; argcount++)
        {
            stackPush(Value::makeNull());
        }
        /* handle variadic arguments... */
        if(closure->m_scriptfunc->m_isvariadic && argcount >= closure->m_scriptfunc->m_arity - 1)
        {
            startva = argcount - closure->m_scriptfunc->m_arity;
            argslist = Array::make(m_pvm);
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
        if(argcount != closure->m_scriptfunc->m_arity)
        {
            stackPop(argcount);
            if(closure->m_scriptfunc->m_isvariadic)
            {
                return raiseClass(m_pvm->m_exceptions.stdexception, "expected at least %d arguments but got %d", closure->m_scriptfunc->m_arity - 1, argcount);
            }
            else
            {
                return raiseClass(m_pvm->m_exceptions.stdexception, "expected %d arguments but got %d", closure->m_scriptfunc->m_arity, argcount);
            }
        }
        if(checkMaybeResize())
        {
            /* stackPop(argcount); */
        }
        frame = &m_framevalues[m_framecount++];
        frame->gcprotcount = 0;
        frame->handlercount = 0;
        frame->closure = closure;
        frame->inscode = closure->m_scriptfunc->m_compiledblob->m_instrucs;
        frame->stackslotpos = m_stackidx + (-argcount - 1);
        return true;
    }

    bool State::VM::vmCallNative(FuncNative* native, Value thisval, int argcount)
    {
        size_t spos;
        Value r;
        Value* vargs;
        NEON_APIDEBUG(m_pvm, "thisval.type=%s, argcount=%d", thisval.name(), argcount);
        spos = m_stackidx + (-argcount);
        vargs = &m_stackvalues[spos];
        CallState fnargs(m_pvm, native->m_nativefnname, thisval, vargs, argcount, native->m_userptrforfn);
        r = native->m_natfunc(m_pvm, &fnargs);
        {
            m_stackvalues[spos - 1] = r;
            m_stackidx -= argcount;
        }
        m_pvm->m_vmstate->gcClearProtect();
        return true;
    }

    bool State::VM::vmCallBoundValue(Value callable, Value thisval, int argcount)
    {
        size_t spos;
        NEON_APIDEBUG(m_pvm, "thisval.type=%s, argcount=%d", thisval.name(), argcount);
        if(callable.isObject())
        {
            switch(callable.objectType())
            {
                case Value::OBJTYPE_FUNCBOUND:
                    {
                        FuncBound* bound;
                        bound = callable.asFuncBound();
                        spos = (m_stackidx + (-argcount - 1));
                        m_stackvalues[spos] = thisval;
                        return callClosure(bound->method, thisval, argcount);
                    }
                    break;
                case Value::OBJTYPE_CLASS:
                    {
                        ClassObject* klass;
                        klass = callable.asClass();
                        spos = (m_stackidx + (-argcount - 1));
                        m_stackvalues[spos] = thisval;
                        if(!klass->m_constructor.isEmpty())
                        {
                            return vmCallBoundValue(klass->m_constructor, thisval, argcount);
                        }
                        else if(klass->m_superclass != nullptr && !klass->m_superclass->m_constructor.isEmpty())
                        {
                            return vmCallBoundValue(klass->m_superclass->m_constructor, thisval, argcount);
                        }
                        else if(argcount != 0)
                        {
                            return raiseClass(m_pvm->m_exceptions.stdexception, "%s constructor expects 0 arguments, %d given", klass->m_classname.data(), argcount);
                        }
                        return true;
                    }
                    break;
                case Value::OBJTYPE_MODULE:
                    {
                        Module* module;
                        Property* field;
                        module = callable.asModule();
                        field = module->m_deftable->getByObjString(module->m_modname);
                        if(field != nullptr)
                        {
                            return vmCallValue(field->m_actualval, thisval, argcount);
                        }
                        return raiseClass(m_pvm->m_exceptions.stdexception, "module %s does not export a default function", module->m_modname);
                    }
                    break;
                case Value::OBJTYPE_FUNCCLOSURE:
                    {
                        return callClosure(callable.asFuncClosure(), thisval, argcount);
                    }
                    break;
                case Value::OBJTYPE_FUNCNATIVE:
                    {
                        return vmCallNative(callable.asFuncNative(), thisval, argcount);
                    }
                    break;
                default:
                    break;
            }
        }
        return raiseClass(m_pvm->m_exceptions.stdexception, "object of type %s is not callable", callable.name());
    }

    bool State::VM::vmCallValue(Value callable, Value thisval, int argcount)
    {
        Value actualthisval;
        if(callable.isObject())
        {
            switch(callable.objectType())
            {
                case Value::OBJTYPE_FUNCBOUND:
                    {
                        FuncBound* bound;
                        bound = callable.asFuncBound();
                        actualthisval = bound->receiver;
                        if(!thisval.isEmpty())
                        {
                            actualthisval = thisval;
                        }
                        NEON_APIDEBUG(m_pvm, "actualthisval.type=%s, argcount=%d", actualthisval.name(), argcount);
                        return vmCallBoundValue(callable, actualthisval, argcount);
                    }
                    break;
                case Value::OBJTYPE_CLASS:
                    {
                        ClassObject* klass;
                        ClassInstance* instance;
                        klass = callable.asClass();
                        instance = ClassInstance::make(m_pvm, klass);
                        actualthisval = Value::fromObject(instance);
                        if(!thisval.isEmpty())
                        {
                            actualthisval = thisval;
                        }
                        NEON_APIDEBUG(m_pvm, "actualthisval.type=%s, argcount=%d", actualthisval.name(), argcount);
                        return vmCallBoundValue(callable, actualthisval, argcount);
                    }
                    break;
                default:
                    {
                    }
                    break;
            }
        }
        NEON_APIDEBUG(m_pvm, "thisval.type=%s, argcount=%d", thisval.name(), argcount);
        return vmCallBoundValue(callable, thisval, argcount);
    }

    bool State::VM::vmInvokeMethodFromClass(ClassObject* klass, String* name, int argcount)
    {
        Property* field;
        NEON_APIDEBUG(m_pvm, "argcount=%d", argcount);
        field = klass->m_classmethods->getByObjString(name);
        if(field != nullptr)
        {
            if(FuncCommon::getMethodType(field->m_actualval) == FuncCommon::FUNCTYPE_PRIVATE)
            {
                return raiseClass(m_pvm->m_exceptions.stdexception, "cannot call private method '%s' from instance of %s", name->data(), klass->m_classname.data());
            }
            return vmCallBoundValue(field->m_actualval, Value::fromObject(klass), argcount);
        }
        return raiseClass(m_pvm->m_exceptions.stdexception, "undefined method '%s' in %s", name->data(), klass->m_classname.data());
    }

    bool State::VM::vmInvokeSelfMethod(String* name, int argcount)
    {
        size_t spos;
        Value receiver;
        ClassInstance* instance;
        Property* field;
        NEON_APIDEBUG(m_pvm, "argcount=%d", argcount);
        receiver = stackPeek(argcount);
        if(receiver.isInstance())
        {
            instance = receiver.asInstance();
            field = instance->m_fromclass->m_classmethods->getByObjString(name);
            if(field != nullptr)
            {
                return vmCallBoundValue(field->m_actualval, receiver, argcount);
            }
            field = instance->m_properties->getByObjString(name);
            if(field != nullptr)
            {
                spos = (m_stackidx + (-argcount - 1));
                m_stackvalues[spos] = receiver;
                return vmCallBoundValue(field->m_actualval, receiver, argcount);
            }
        }
        else if(receiver.isClass())
        {
            field = receiver.asClass()->m_classmethods->getByObjString(name);
            if(field != nullptr)
            {
                if(FuncCommon::getMethodType(field->m_actualval) == FuncCommon::FUNCTYPE_STATIC)
                {
                    return vmCallBoundValue(field->m_actualval, receiver, argcount);
                }
                return raiseClass(m_pvm->m_exceptions.stdexception, "cannot call non-static method %s() on non instance", name->data());
            }
        }
        return raiseClass(m_pvm->m_exceptions.stdexception, "cannot call method '%s' on object of type '%s'", name->data(), receiver.name());
    }

    bool State::VM::vmInvokeMethod(String* name, int argcount)
    {
        size_t spos;
        Value::ObjType rectype;
        Value receiver;
        Property* field;
        ClassObject* klass;
        receiver = stackPeek(argcount);
        NEON_APIDEBUG(m_pvm, "receiver.type=%s, argcount=%d", receiver.name(), argcount);
        if(receiver.isObject())
        {
            rectype = receiver.asObject()->m_objtype;
            switch(rectype)
            {
                case Value::OBJTYPE_MODULE:
                    {
                        Module* module;
                        NEON_APIDEBUG(m_pvm, "receiver is a module");
                        module = receiver.asModule();
                        field = module->m_deftable->getByObjString(name);
                        if(field != nullptr)
                        {
                            return vmCallBoundValue(field->m_actualval, receiver, argcount);
                        }
                        return raiseClass(m_pvm->m_exceptions.stdexception, "module %s does not define class or method %s()", module->m_modname, name->data());
                    }
                    break;
                case Value::OBJTYPE_CLASS:
                    {
                        NEON_APIDEBUG(m_pvm, "receiver is a class");
                        klass = receiver.asClass();
                        /* first, check static properties ... */
                        field = klass->getStaticProperty(name);
                        if(field != nullptr)
                        {
                            return vmCallBoundValue(field->m_actualval, receiver, argcount);
                        }
                        /* then check static methods ... */
                        field = klass->getStaticMethodField(name);
                        if(field != nullptr)
                        {
                            return vmCallBoundValue(field->m_actualval, receiver, argcount);
                        }
                        return raiseClass(m_pvm->m_exceptions.stdexception, "unknown method %s() in class %s", name->data(), klass->m_classname.data());
                    }
                case Value::OBJTYPE_INSTANCE:
                    {
                        ClassInstance* instance;
                        NEON_APIDEBUG(m_pvm, "receiver is an instance");
                        instance = receiver.asInstance();
                        field = instance->m_properties->getByObjString(name);
                        if(field != nullptr)
                        {
                            spos = (m_stackidx + (-argcount - 1));
                            m_stackvalues[spos] = receiver;
                            return vmCallBoundValue(field->m_actualval, receiver, argcount);
                        }
                        return vmInvokeMethodFromClass(instance->m_fromclass, name, argcount);
                    }
                    break;
                case Value::OBJTYPE_DICT:
                    {
                        NEON_APIDEBUG(m_pvm, "receiver is a dictionary");
                        field = m_pvm->m_classprimdict->getMethodField(name);
                        if(field != nullptr)
                        {
                            return m_pvm->m_vmstate->vmCallNative(field->m_actualval.asFuncNative(), receiver, argcount);
                        }
                        /* NEW in v0.0.84, dictionaries can declare extra methods as part of their entries. */
                        else
                        {
                            field = receiver.asDict()->m_valtable->getByObjString(name);
                            if(field != nullptr)
                            {
                                if(field->m_actualval.isCallable())
                                {
                                    return vmCallBoundValue(field->m_actualval, receiver, argcount);
                                }
                            }
                        }
                        return raiseClass(m_pvm->m_exceptions.stdexception, "'dict' has no method %s()", name->data());
                    }
                    default:
                        {
                        }
                        break;
            }
        }
        klass = m_pvm->vmGetClassFor(receiver);
        if(klass == nullptr)
        {
            /* @TODO: have methods for non objects as well. */
            return raiseClass(m_pvm->m_exceptions.stdexception, "non-object %s has no method named '%s'", receiver.name(), name->data());
        }
        field = klass->getMethodField(name);
        if(field != nullptr)
        {
            return vmCallBoundValue(field->m_actualval, receiver, argcount);
        }
        return raiseClass(m_pvm->m_exceptions.stdexception, "'%s' has no method %s()", klass->m_classname.data(), name->data());
    }

    bool State::VM::vmBindMethod(ClassObject* klass, String* name)
    {
        Value val;
        Property* field;
        FuncBound* bound;
        field = klass->m_classmethods->getByObjString(name);
        if(field != nullptr)
        {
            if(FuncCommon::getMethodType(field->m_actualval) == FuncCommon::FUNCTYPE_PRIVATE)
            {
                return raiseClass(m_pvm->m_exceptions.stdexception, "cannot get private property '%s' from instance", name->data());
            }
            val = stackPeek(0);
            bound = FuncBound::make(m_pvm, val, field->m_actualval.asFuncClosure());
            stackPop();
            stackPush(Value::fromObject(bound));
            return true;
        }
        return raiseClass(m_pvm->m_exceptions.stdexception, "undefined property '%s'", name->data());
    }

    Value State::VM::getExceptionStacktrace()
    {
        int line;
        size_t i;
        size_t instruction;
        const char* fnname;
        const char* physfile;
        CallFrame* frame;
        FuncScript* function;
        String* os;
        Array* oa;
        oa = Array::make(m_pvm);
        {
            for(i = 0; i < m_framecount; i++)
            {
                Printer pd(m_pvm);
                frame = &m_framevalues[i];
                function = frame->closure->m_scriptfunc;
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
                if((i > 15) && (!m_pvm->m_conf.showfullstack))
                {
                    pd = Printer(m_pvm);
                    pd.putformat("(only upper 15 entries shown)");
                    os = pd.takeString();
                    oa->push(Value::fromObject(os));
                    break;
                }
            }
            return Value::fromObject(oa);
        }
        return Value::fromObject(String::copy(m_pvm, "", 0));
    }

    bool State::VM::exceptionHandleUncaught(ClassInstance* exception)
    {
        int i;
        int cnt;
        size_t emsglen;
        const char* emsgstr;
        const char* colred;
        const char* colreset;
        Value stackitm;
        Property* field;
        String* emsg;
        Array* oa;
        Terminal nc;
        colred = nc.color('r');
        colreset = nc.color('0');
        /* at this point, the exception is unhandled; so, print it out. */
        fprintf(stderr, "%sunhandled %s%s", colred, exception->m_fromclass->m_classname.data(), colreset);    
        field = exception->m_properties->getByCStr("message");
        if(field != nullptr)
        {
            emsg = Value::toString(m_pvm, field->m_actualval);
            emsgstr = emsg->data();
            emsglen = emsg->length();
        }
        else
        {
            emsgstr = "(no message)";
            emsglen = strlen(emsgstr);
        }

        if(emsglen > 0)
        {
            fprintf(stderr, ": %s", emsgstr);
        }
        fprintf(stderr, "\n");
        fprintf(stderr, "  stacktrace:\n");
        field = exception->m_properties->getByCStr("stacktrace");
        if(field != nullptr)
        {
            oa = field->m_actualval.asArray();
            cnt = oa->size();
            i = cnt-1;
            if(cnt > 0)
            {
                while(true)
                {
                    stackitm = oa->at(i);
                    m_pvm->m_debugprinter->putformat("  ");
                    m_pvm->m_debugprinter->printValue(stackitm, false, true);
                    m_pvm->m_debugprinter->putformat("\n");
                    if(i == 0)
                    {
                        break;
                    }
                    i--;
                }
            }
        }
        else
        {
            fprintf(stderr, "  (no traceback)\n");
        }
        return false;
    }        

    void State::VM::setInstanceProperty(ClassInstance* instance, const char* propname, Value value)
    {
        instance->defProperty(propname, value);
    }

    bool State::VM::exceptionPropagate()
    {
        int i;
        FuncScript* function;
        ExceptionFrame* handler;
        ClassInstance* exception;
        ClassObject* klassraised;
        exception = stackPeek(0).asInstance();
        while(m_framecount > 0)
        {
            m_currentframe = &m_framevalues[m_framecount - 1];
            for(i = m_currentframe->handlercount; i > 0; i--)
            {
                handler = &m_currentframe->handlers[i - 1];
                function = m_currentframe->closure->m_scriptfunc;
                klassraised = handler->exklass;
                //fprintf(stderr, "handler->address=%d handler->finallyaddress=%d\n", handler->address, handler->finallyaddress);
                if(handler->hasclass == false)
                {
                    klassraised = m_pvm->m_exceptions.stdexception;
                }
                if(handler->address != 0 && ClassObject::instanceOf(exception->m_fromclass, klassraised))
                {
                    m_currentframe->inscode = &function->m_compiledblob->m_instrucs[handler->address];
                    return true;
                }
                else if(handler->finallyaddress != 0)
                {
                    /* continue propagating once the 'finally' block completes */
                    stackPush(Value::makeBool(true));
                    m_currentframe->inscode = &function->m_compiledblob->m_instrucs[handler->finallyaddress];
                    return true;
                }
            }
            m_framecount--;
        }
        return exceptionHandleUncaught(exception);
    }

    void State::VM::gcDestroyLinkedObjects()
    {
        Object* next;
        Object* object;
        object = m_linkedobjects;
        if(object != nullptr)
        {
            while(object != nullptr)
            {
                next = object->m_nextobj;
                if(object != nullptr)
                {
                    Object::destroyObject(m_pvm, object);
                }
                object = next;
            }
        }
        Memory::osFree(m_gcgraystack);
        m_gcgraystack = nullptr;
    }

    void State::VM::gcTraceRefs()
    {
        Object* object;
        while(m_gcgraycount > 0)
        {
            object = m_gcgraystack[--m_gcgraycount];
            Object::blackenObject(m_pvm, object);
        }
    }

    void State::VM::gcMarkRoots()
    {
        int i;
        int j;
        Value* slot;
        ScopeUpvalue* upvalue;
        ExceptionFrame* handler;
        for(slot = m_stackvalues; slot < &m_stackvalues[m_stackidx]; slot++)
        {
            Object::markValue(m_pvm, *slot);
        }
        for(i = 0; i < (int)m_framecount; i++)
        {
            Object::markObject(m_pvm, m_framevalues[i].closure);
            for(j = 0; j < (int)m_framevalues[i].handlercount; j++)
            {
                handler = &m_framevalues[i].handlers[j];
                Object::markObject(m_pvm, handler->exklass);
            }
        }
        for(upvalue = m_openupvalues; upvalue != nullptr; upvalue = upvalue->m_nextupval)
        {
            Object::markObject(m_pvm, upvalue);
        }
        m_pvm->m_definedglobals->markTableValues();
        m_pvm->m_loadedmodules->markTableValues();
        Object::markObject(m_pvm, m_pvm->m_exceptions.stdexception);
        m_pvm->gcMarkCompilerRoots();
    }

    void State::VM::gcSweep()
    {
        Object* object;
        Object* previous;
        Object* unreached;
        previous = nullptr;
        object = m_linkedobjects;
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
                    m_linkedobjects = object;
                }
                Object::destroyObject(m_pvm, unreached);
            }
        }
    }

    void State::VM::gcCollectGarbage()
    {
        size_t before;
        (void)before;
        #if defined(NEON_CFG_DEBUGGC) && NEON_CFG_DEBUGGC
        m_pvm->m_debugprinter->putformat("GC: gc begins\n");
        before = m_gcbytesallocated;
        #endif
        /*
        //  REMOVE THE NEXT LINE TO DISABLE NESTED gcCollectGarbage() POSSIBILITY!
        */
        #if 0
        m_gcnextgc = m_gcbytesallocated;
        #endif
        gcMarkRoots();
        gcTraceRefs();
        m_pvm->m_cachedstrings->removeMarkedValues();
        m_pvm->m_loadedmodules->removeMarkedValues();
        gcSweep();
        m_gcnextgc = m_gcbytesallocated * NEON_CFG_GCHEAPGROWTHFACTOR;
        m_currentmarkvalue = !m_currentmarkvalue;
        #if defined(NEON_CFG_DEBUGGC) && NEON_CFG_DEBUGGC
        m_pvm->m_debugprinter->putformat("GC: gc ends\n");
        m_pvm->m_debugprinter->putformat("GC: collected %zu bytes (from %zu to %zu), next at %zu\n", before - m_gcbytesallocated, before, m_gcbytesallocated, m_gcnextgc);
        #endif
    }

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
                case Value::OBJTYPE_FUNCNATIVE:
                case Value::OBJTYPE_FUNCBOUND:
                case Value::OBJTYPE_FUNCCLOSURE:
                case Value::OBJTYPE_FUNCSCRIPT:
                    return m_classprimcallable;
                
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
                Object::markObject(this, compiler->m_targetfunc);
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
        function = frame->closure->m_scriptfunc;
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
                function = frame->closure->m_scriptfunc;
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

    ClassInstance* State::makeExceptionInstance(ClassObject* ofklass, char* msgbuf, size_t len, bool takestr)
    {
        String* omsg;
        ClassInstance* instance;
        if(takestr)
        {
            omsg = String::take(this, msgbuf, len);
        }
        else
        {
            omsg = String::copy(this, msgbuf, len);
        }
        instance = ClassInstance::make(this, ofklass);
        m_vmstate->stackPush(Value::fromObject(instance));
        instance->defProperty("message", Value::fromObject(omsg));
        m_vmstate->stackPop();
        return instance;
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
        klass = ClassObject::make(this, Util::StrBuffer(cstrname), nullptr);
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
        klass->m_classmethods->set(Value::fromObject(classname), Value::fromObject(closure));
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

neon::Value nn_modfn_os_readdir(neon::State* state, neon::CallState* args)
{
    const char* dirn;
    FSDirReader rd;
    FSDirItem itm;
    neon::String* os;
    neon::String* aval;
    neon::Array* res;
    args->checkType(0, neon::Value::OBJTYPE_STRING);
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
        state->m_vmstate->raiseClass(state->m_exceptions.stdexception, "cannot open directory '%s'", dirn);
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

neon::Value nn_modfn_astscan_scan(neon::State* state, neon::CallState* args)
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
    args->checkType(0, neon::Value::OBJTYPE_STRING);
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


namespace neon
{
    Value objfn_dict_member_length(State* state, CallState* args)
    {
        (void)state;
        args->checkCount(0);
        return Value::makeNumber(args->thisval.asDict()->m_keynames->m_count);
    }

    Value objfn_dict_member_add(State* state, CallState* args)
    {
        Dictionary* dict;
        args->checkCount(2);
        ENFORCE_VALID_DICT_KEY(args, 0);
        dict = args->thisval.asDict();
        auto field = dict->m_valtable->getField(args->argv[0]);
        if(field != nullptr)
        {
            return state->m_vmstate->raiseFromFunction(args, "duplicate key %s at add()", Value::toString(state, args->argv[0])->data());
        }
        dict->addEntry(args->argv[0], args->argv[1]);
        return Value::makeEmpty();
    }

    Value objfn_dict_member_set(State* state, CallState* args)
    {
        Dictionary* dict;
        args->checkCount(2);
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
        return Value::makeEmpty();
    }

    Value objfn_dict_member_clear(State* state, CallState* args)
    {
        Dictionary* dict;
        (void)state;
        args->checkCount(0);
        dict = args->thisval.asDict();
        Memory::destroy(dict->m_keynames);
        Memory::destroy(dict->m_valtable);
        return Value::makeEmpty();
    }

    Value objfn_dict_member_clone(State* state, CallState* args)
    {
        size_t i;
        Dictionary* dict;
        Dictionary* newdict;
        args->checkCount(0);
        dict = args->thisval.asDict();
        newdict = state->m_vmstate->gcProtect(Dictionary::make(state));
        newdict->m_valtable->addAll(dict->m_valtable);
        for(i = 0; i < dict->m_keynames->m_count; i++)
        {
            newdict->m_keynames->push(dict->m_keynames->m_values[i]);
        }
        return Value::fromObject(newdict);
    }

    Value objfn_dict_member_compact(State* state, CallState* args)
    {
        size_t i;
        Dictionary* dict;
        Dictionary* newdict;
        args->checkCount(0);
        dict = args->thisval.asDict();
        newdict = state->m_vmstate->gcProtect(Dictionary::make(state));
        for(i = 0; i < dict->m_keynames->m_count; i++)
        {
            auto field = dict->m_valtable->getField(dict->m_keynames->m_values[i]);
            if(!field->m_actualval.compare(Value::makeNull()))
            {
                newdict->addEntry(dict->m_keynames->m_values[i], field->m_actualval);
            }
        }
        return Value::fromObject(newdict);
    }

    Value objfn_dict_member_contains(State* state, CallState* args)
    {
        Dictionary* dict;
        args->checkCount(1);
        ENFORCE_VALID_DICT_KEY(args, 0);
        dict = args->thisval.asDict();
        auto field = dict->m_valtable->getField(args->argv[0]);
        return Value::makeBool(field != nullptr);
    }

    Value objfn_dict_member_extend(State* state, CallState* args)
    {
        size_t i;
        Dictionary* dict;
        Dictionary* dictcpy;
        (void)state;
        args->checkCount(1);
        args->checkType(0, neon::Value::OBJTYPE_DICT);
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
        dict->m_valtable->addAll(dictcpy->m_valtable);
        return Value::makeEmpty();
    }

    Value objfn_dict_member_get(State* state, CallState* args)
    {
        Dictionary* dict;
        Property* field;
        NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
        ENFORCE_VALID_DICT_KEY(args, 0);
        dict = args->thisval.asDict();
        field = dict->getEntry(args->argv[0]);
        if(field == nullptr)
        {
            if(args->argc == 1)
            {
                return Value::makeNull();
            }
            else
            {
                return args->argv[1];
            }
        }
        return field->m_actualval;
    }

    Value objfn_dict_member_keys(State* state, CallState* args)
    {
        size_t i;
        Dictionary* dict;
        Array* list;
        args->checkCount(0);
        dict = args->thisval.asDict();
        list = state->m_vmstate->gcProtect(Array::make(state));
        for(i = 0; i < dict->m_keynames->m_count; i++)
        {
            list->push(dict->m_keynames->m_values[i]);
        }
        return Value::fromObject(list);
    }

    Value objfn_dict_member_values(State* state, CallState* args)
    {
        size_t i;
        Dictionary* dict;
        Array* list;
        Property* field;
        args->checkCount(0);
        dict = args->thisval.asDict();
        list = state->m_vmstate->gcProtect(Array::make(state));
        for(i = 0; i < dict->m_keynames->m_count; i++)
        {
            field = dict->getEntry(dict->m_keynames->m_values[i]);
            list->push(field->m_actualval);
        }
        return Value::fromObject(list);
    }

    Value objfn_dict_member_remove(State* state, CallState* args)
    {
        size_t i;
        int index;
        Dictionary* dict;
        args->checkCount(1);
        ENFORCE_VALID_DICT_KEY(args, 0);
        dict = args->thisval.asDict();
        auto field = dict->m_valtable->getField(args->argv[0]);
        if(field != nullptr)
        {
            dict->m_valtable->removeByKey(args->argv[0]);
            index = -1;
            for(i = 0; i < dict->m_keynames->m_count; i++)
            {
                if(dict->m_keynames->m_values[i].compare(args->argv[0]))
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
        return Value::makeNull();
    }

    Value objfn_dict_member_isempty(State* state, CallState* args)
    {
        (void)state;
        args->checkCount(0);
        return Value::makeBool(args->thisval.asDict()->m_keynames->m_count == 0);
    }

    Value objfn_dict_member_findkey(State* state, CallState* args)
    {
        (void)state;
        args->checkCount(1);
        return args->thisval.asDict()->m_valtable->findKey(args->argv[0]);
    }

    Value objfn_dict_member_tolist(State* state, CallState* args)
    {
        size_t i;
        Array* list;
        Dictionary* dict;
        Array* namelist;
        Array* valuelist;
        args->checkCount(0);
        dict = args->thisval.asDict();
        namelist = state->m_vmstate->gcProtect(Array::make(state));
        valuelist = state->m_vmstate->gcProtect(Array::make(state));
        for(i = 0; i < dict->m_keynames->m_count; i++)
        {
            namelist->push(dict->m_keynames->m_values[i]);
            auto field = dict->m_valtable->getField(dict->m_keynames->m_values[i]);
            if(field != nullptr)
            {
                valuelist->push(field->value());
            }
            else
            {
                /* theoretically impossible */
                valuelist->push(Value::makeNull());
            }
        }
        list = state->m_vmstate->gcProtect(Array::make(state));
        list->push(Value::fromObject(namelist));
        list->push(Value::fromObject(valuelist));
        return Value::fromObject(list);
    }

    Value objfn_dict_member_iter(State* state, CallState* args)
    {
        Dictionary* dict;
        (void)state;
        args->checkCount(1);
        dict = args->thisval.asDict();
        auto field = dict->m_valtable->getField(args->argv[0]);
        if(field != nullptr)
        {
            return field->value();
        }
        return Value::makeNull();
    }

    Value objfn_dict_member_itern(State* state, CallState* args)
    {
        size_t i;
        Dictionary* dict;
        (void)state;
        args->checkCount(1);
        dict = args->thisval.asDict();
        if(args->argv[0].isNull())
        {
            if(dict->m_keynames->m_count == 0)
            {
                return Value::makeBool(false);
            }
            return dict->m_keynames->m_values[0];
        }
        for(i = 0; i < dict->m_keynames->m_count; i++)
        {
            if(args->argv[0].compare(dict->m_keynames->m_values[i]) && (i + 1) < dict->m_keynames->m_count)
            {
                return dict->m_keynames->m_values[i + 1];
            }
        }
        return Value::makeNull();
    }

    Value objfn_dict_member_each(State* state, CallState* args)
    {
        size_t i;
        int arity;
        Value callable;
        Value unused;
        Dictionary* dict;
        ValArray nestargs;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isCallable);
        dict = args->thisval.asDict();
        callable = args->argv[0];
        NestCall nc(state);
        arity = nc.prepare(callable, args->thisval, nestargs);
        for(i = 0; i < dict->m_keynames->m_count; i++)
        {
            if(arity > 0)
            {
                auto field = dict->m_valtable->getField(dict->m_keynames->m_values[i]);
                nestargs[0] = field->value();
                if(arity > 1)
                {
                    nestargs[1] = dict->m_keynames->m_values[i];
                }
            }
            nc.callNested(callable, args->thisval, nestargs, &unused);
        }
        /* pop the argument list */
        return Value::makeEmpty();
    }

    Value objfn_dict_member_filter(State* state, CallState* args)
    {
        size_t i;
        int arity;
        Value callable;
        Value result;
        Dictionary* dict;
        ValArray nestargs;
        Dictionary* resultdict;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isCallable);
        dict = args->thisval.asDict();
        callable = args->argv[0];
        NestCall nc(state);
        arity = nc.prepare(callable, args->thisval, nestargs);
        resultdict = state->m_vmstate->gcProtect(Dictionary::make(state));
        for(i = 0; i < dict->m_keynames->m_count; i++)
        {
            auto field = dict->m_valtable->getField(dict->m_keynames->m_values[i]);
            if(arity > 0)
            {
                nestargs[0] = field->value();
                if(arity > 1)
                {
                    nestargs[1] = dict->m_keynames->m_values[i];
                }
            }
            nc.callNested(callable, args->thisval, nestargs, &result);
            if(!Value::isFalse(result))
            {
                resultdict->addEntry(dict->m_keynames->m_values[i], field->value());
            }
        }
        /* pop the call list */
        return Value::fromObject(resultdict);
    }

    Value objfn_dict_member_some(State* state, CallState* args)
    {
        size_t i;
        int arity;
        Value result;
        Value callable;
        Dictionary* dict;
        ValArray nestargs;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isCallable);
        dict = args->thisval.asDict();
        callable = args->argv[0];
        NestCall nc(state);
        arity = nc.prepare(callable, args->thisval, nestargs);
        for(i = 0; i < dict->m_keynames->m_count; i++)
        {
            if(arity > 0)
            {
                auto field = dict->m_valtable->getField(dict->m_keynames->m_values[i]);
                nestargs[0] = field->value();
                if(arity > 1)
                {
                    nestargs[1] = dict->m_keynames->m_values[i];
                }
            }
            nc.callNested(callable, args->thisval, nestargs, &result);
            if(!Value::isFalse(result))
            {
                /* pop the call list */
                return Value::makeBool(true);
            }
        }
        /* pop the call list */
        return Value::makeBool(false);
    }

    Value objfn_dict_member_every(State* state, CallState* args)
    {
        size_t i;
        int arity;
        Value callable;  
        Value result;
        Dictionary* dict;
        ValArray nestargs;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isCallable);
        dict = args->thisval.asDict();
        callable = args->argv[0];
        NestCall nc(state);
        arity = nc.prepare(callable, args->thisval, nestargs);
        for(i = 0; i < dict->m_keynames->m_count; i++)
        {
            if(arity > 0)
            {
                auto field = dict->m_valtable->getField(dict->m_keynames->m_values[i]);
                nestargs[0] = field->value();
                if(arity > 1)
                {
                    nestargs[1] = dict->m_keynames->m_values[i];
                }
            }
            nc.callNested(callable, args->thisval, nestargs, &result);
            if(Value::isFalse(result))
            {
                /* pop the call list */
                return Value::makeBool(false);
            }
        }
        /* pop the call list */
        return Value::makeBool(true);
    }

    Value objfn_dict_member_reduce(State* state, CallState* args)
    {
        size_t i;
        int arity;
        int startindex;
        Value callable;
        Value accumulator;
        Dictionary* dict;
        ValArray nestargs;
        NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
        args->checkByFunc(0, &Value::isCallable);
        dict = args->thisval.asDict();
        callable = args->argv[0];
        startindex = 0;
        accumulator = Value::makeNull();
        if(args->argc == 2)
        {
            accumulator = args->argv[1];
        }
        if(accumulator.isNull() && dict->m_keynames->m_count > 0)
        {
            auto field = dict->m_valtable->getField(dict->m_keynames->m_values[0]);
            accumulator = field->value();
            startindex = 1;
        }
        NestCall nc(state);
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
                        auto field = dict->m_valtable->getField(dict->m_keynames->m_values[i]);
                        nestargs[1] = field->value();
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
        return accumulator;
    }

    #undef ENFORCE_VALID_DICT_KEY

    #define FILE_ERROR(state, args, type, message) \
        return state->m_vmstate->raiseFromFunction(args, #type " -> %s", message, file->m_filepath->data());

    #define RETURN_STATUS(status) \
        if((status) == 0) \
        { \
            return Value::makeBool(true); \
        } \
        else \
        { \
            FILE_ERROR(state, args, File, strerror(errno)); \
        }

    #define DENY_STD(state, file, args) \
        if(file->m_isstd) \
        return state->m_vmstate->raiseFromFunction(args, "method not supported for std files");

    Value objfn_file_member_constructor(State* state, CallState* args)
    {
        FILE* hnd;
        const char* path;
        const char* mode;
        String* opath;
        File* file;
        (void)hnd;
        NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
        args->checkByFunc(0, &Value::isString);
        opath = args->argv[0].asString();
        if(opath->length() == 0)
        {
            return state->m_vmstate->raiseFromFunction(args, "file path cannot be empty");
        }
        mode = "r";
        if(args->argc == 2)
        {
            args->checkByFunc(1, &Value::isString);
            mode = args->argv[1].asString()->data();
        }
        path = opath->data();
        file = state->m_vmstate->gcProtect(File::make(state, nullptr, false, path, mode));
        file->selfOpenFile();
        return Value::fromObject(file);
    }

    Value objfn_file_static_exists(State* state, CallState* args)
    {
        String* file;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isString);
        file = args->argv[0].asString();
        return Value::makeBool(osfn_fileexists(file->data()));
    }

    Value objfn_file_static_isfile(State* state, CallState* args)
    {
        String* file;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isString);
        file = args->argv[0].asString();
        return Value::makeBool(osfn_pathisfile(file->data()));
    }

    Value objfn_file_static_isdirectory(State* state, CallState* args)
    {
        String* file;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isString);
        file = args->argv[0].asString();
        return Value::makeBool(osfn_pathisdirectory(file->data()));
    }

    Value objfn_file_static_read(State* state, CallState* args)
    {
        size_t sz;
        char* src;
        String* file;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isString);
        file = args->argv[0].asString();
        src = Util::fileReadFile(state, file->data(), &sz);
        return Value::fromObject(String::take(state, src, sz));
    }

    Value objfn_file_member_close(State* state, CallState* args)
    {
        (void)state;
        args->checkCount(0);
        args->thisval.asFile()->selfCloseFile();
        return Value::makeEmpty();
    }

    Value objfn_file_member_open(State* state, CallState* args)
    {
        (void)state;
        args->checkCount(0);
        args->thisval.asFile()->selfOpenFile();
        return Value::makeEmpty();
    }

    Value objfn_file_member_isopen(State* state, CallState* args)
    {
        File* file;
        (void)state;
        file = args->thisval.asFile();
        return Value::makeBool(file->m_isstd || file->m_isopen);
    }

    Value objfn_file_member_isclosed(State* state, CallState* args)
    {
        File* file;
        (void)state;
        file = args->thisval.asFile();
        return Value::makeBool(!file->m_isstd && !file->m_isopen);
    }

    Value objfn_file_member_read(State* state, CallState* args)
    {
        size_t readhowmuch;
        File::IOResult res;
        File* file;
        String* os;
        NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
        readhowmuch = -1;
        if(args->argc == 1)
        {
            args->checkByFunc(0, &Value::isNumber);
            readhowmuch = (size_t)args->argv[0].asNumber();
        }
        file = args->thisval.asFile();
        if(!file->readChunk(readhowmuch, &res))
        {
            FILE_ERROR(state, args, NotFound, strerror(errno));
        }
        os = String::take(state, res.data, res.length);
        return Value::fromObject(os);
    }

    Value objfn_file_member_get(State* state, CallState* args)
    {
        int ch;
        File* file;
        (void)state;
        args->checkCount(0);
        file = args->thisval.asFile();
        ch = fgetc(file->m_filehandle);
        if(ch == EOF)
        {
            return Value::makeNull();
        }
        return Value::makeNumber(ch);
    }

    Value objfn_file_member_gets(State* state, CallState* args)
    {
        long end;
        long length;
        long currentpos;
        size_t bytesread;
        char* buffer;
        File* file;
        NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
        length = -1;
        if(args->argc == 1)
        {
            args->checkByFunc(0, &Value::isNumber);
            length = (size_t)args->argv[0].asNumber();
        }
        file = args->thisval.asFile();
        if(!file->m_isstd)
        {
            if(!osfn_fileexists(file->m_filepath->data()))
            {
                FILE_ERROR(state, args, NotFound, "no such file or directory");
            }
            else if(strstr(file->m_filemode->data(), "w") != nullptr && strstr(file->m_filemode->data(), "+") == nullptr)
            {
                FILE_ERROR(state, args, Unsupported, "cannot read file in write mode");
            }
            if(!file->m_isopen)
            {
                FILE_ERROR(state, args, Read, "file not open");
            }
            else if(file->m_filehandle == nullptr)
            {
                FILE_ERROR(state, args, Read, "could not read file");
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
                FILE_ERROR(state, args, Unsupported, "cannot read from output file");
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
        buffer = (char*)State::VM::gcAllocMemory(state, sizeof(char), length + 1);
        if(buffer == nullptr && length != 0)
        {
            FILE_ERROR(state, args, Buffer, "not enough memory to read file");
        }
        bytesread = fread(buffer, sizeof(char), length, file->m_filehandle);
        if(bytesread == 0 && length != 0)
        {
            FILE_ERROR(state, args, Read, "could not read file contents");
        }
        if(buffer != nullptr)
        {
            buffer[bytesread] = '\0';
        }
        return Value::fromObject(String::take(state, buffer, bytesread));
    }

    Value objfn_file_member_write(State* state, CallState* args)
    {
        size_t count;
        int length;
        unsigned char* data;
        File* file;
        String* string;
        args->checkCount(1);
        file = args->thisval.asFile();
        args->checkByFunc(0, &Value::isString);
        string = args->argv[0].asString();
        data = (unsigned char*)string->data();
        length = string->length();
        if(!file->m_isstd)
        {
            if(strstr(file->m_filemode->data(), "r") != nullptr && strstr(file->m_filemode->data(), "+") == nullptr)
            {
                FILE_ERROR(state, args, Unsupported, "cannot write into non-writable file");
            }
            else if(length == 0)
            {
                FILE_ERROR(state, args, Write, "cannot write empty buffer to file");
            }
            else if(file->m_filehandle == nullptr || !file->m_isopen)
            {
                file->selfOpenFile();
            }
            else if(file->m_filehandle == nullptr)
            {
                FILE_ERROR(state, args, Write, "could not write to file");
            }
        }
        else
        {
            if(fileno(stdin) == file->m_filedesc)
            {
                FILE_ERROR(state, args, Unsupported, "cannot write to input file");
            }
        }
        count = fwrite(data, sizeof(unsigned char), length, file->m_filehandle);
        fflush(file->m_filehandle);
        if(count > (size_t)0)
        {
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    }

    Value objfn_file_member_puts(State* state, CallState* args)
    {
        size_t count;
        int length;
        unsigned char* data;
        File* file;
        String* string;
        args->checkCount(1);
        file = args->thisval.asFile();
        args->checkByFunc(0, &Value::isString);
        string = args->argv[0].asString();
        data = (unsigned char*)string->data();
        length = string->length();
        if(!file->m_isstd)
        {
            if(strstr(file->m_filemode->data(), "r") != nullptr && strstr(file->m_filemode->data(), "+") == nullptr)
            {
                FILE_ERROR(state, args, Unsupported, "cannot write into non-writable file");
            }
            else if(length == 0)
            {
                FILE_ERROR(state, args, Write, "cannot write empty buffer to file");
            }
            else if(!file->m_isopen)
            {
                FILE_ERROR(state, args, Write, "file not open");
            }
            else if(file->m_filehandle == nullptr)
            {
                FILE_ERROR(state, args, Write, "could not write to file");
            }
        }
        else
        {
            if(fileno(stdin) == file->m_filedesc)
            {
                FILE_ERROR(state, args, Unsupported, "cannot write to input file");
            }
        }
        count = fwrite(data, sizeof(unsigned char), length, file->m_filehandle);
        if(count > (size_t)0 || length == 0)
        {
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    }

    Value objfn_file_member_printf(State* state, CallState* args)
    {
        File* file;
        String* ofmt;
        file = args->thisval.asFile();
        NEON_ARGS_CHECKMINARG(args, 1);
        args->checkByFunc(0, &Value::isString);
        ofmt = args->argv[0].asString();
        Printer pd(state, file->m_filehandle, false);
        FormatInfo nfi(state, &pd, ofmt->cstr(), ofmt->length());
        if(!nfi.format(args->argc, 1, args->argv))
        {
        }
        return Value::makeNull();
    }

    Value objfn_file_member_number(State* state, CallState* args)
    {
        (void)state;
        args->checkCount(0);
        return Value::makeNumber(args->thisval.asFile()->m_filedesc);
    }

    Value objfn_file_member_istty(State* state, CallState* args)
    {
        File* file;
        (void)state;
        args->checkCount(0);
        file = args->thisval.asFile();
        return Value::makeBool(file->m_istty);
    }

    Value objfn_file_member_flush(State* state, CallState* args)
    {
        File* file;
        args->checkCount(0);
        file = args->thisval.asFile();
        if(!file->m_isopen)
        {
            FILE_ERROR(state, args, Unsupported, "I/O operation on closed file");
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
        return Value::makeEmpty();
    }

    Value objfn_file_member_stats(State* state, CallState* args)
    {
        struct stat stats;
        File* file;
        Dictionary* dict;
        args->checkCount(0);
        file = args->thisval.asFile();
        dict = state->m_vmstate->gcProtect(Dictionary::make(state));
        if(!file->m_isstd)
        {
            if(osfn_fileexists(file->m_filepath->data()))
            {
                if(osfn_lstat(file->m_filepath->data(), &stats) == 0)
                {
                    #if !defined(NEON_PLAT_ISWINDOWS)
                    dict->addEntryCStr("isreadable", Value::makeBool(((stats.st_mode & S_IRUSR) != 0)));
                    dict->addEntryCStr("iswritable", Value::makeBool(((stats.st_mode & S_IWUSR) != 0)));
                    dict->addEntryCStr("isexecutable", Value::makeBool(((stats.st_mode & S_IXUSR) != 0)));
                    dict->addEntryCStr("issymbolic", Value::makeBool((S_ISLNK(stats.st_mode) != 0)));
                    #else
                    dict->addEntryCStr("isreadable", Value::makeBool(((stats.st_mode & S_IREAD) != 0)));
                    dict->addEntryCStr("iswritable", Value::makeBool(((stats.st_mode & S_IWRITE) != 0)));
                    dict->addEntryCStr("isexecutable", Value::makeBool(((stats.st_mode & S_IEXEC) != 0)));
                    dict->addEntryCStr("issymbolic", Value::makeBool(false));
                    #endif
                    dict->addEntryCStr("size", Value::makeNumber(stats.st_size));
                    dict->addEntryCStr("mode", Value::makeNumber(stats.st_mode));
                    dict->addEntryCStr("dev", Value::makeNumber(stats.st_dev));
                    dict->addEntryCStr("ino", Value::makeNumber(stats.st_ino));
                    dict->addEntryCStr("nlink", Value::makeNumber(stats.st_nlink));
                    dict->addEntryCStr("uid", Value::makeNumber(stats.st_uid));
                    dict->addEntryCStr("gid", Value::makeNumber(stats.st_gid));
                    dict->addEntryCStr("mtime", Value::makeNumber(stats.st_mtime));
                    dict->addEntryCStr("atime", Value::makeNumber(stats.st_atime));
                    dict->addEntryCStr("ctime", Value::makeNumber(stats.st_ctime));
                    dict->addEntryCStr("blocks", Value::makeNumber(0));
                    dict->addEntryCStr("blksize", Value::makeNumber(0));
                }
            }
            else
            {
                return state->m_vmstate->raiseFromFunction(args, "cannot get stats for non-existing file");
            }
        }
        else
        {
            if(fileno(stdin) == file->m_filedesc)
            {
                dict->addEntryCStr("isreadable", Value::makeBool(true));
                dict->addEntryCStr("iswritable", Value::makeBool(false));
            }
            else
            {
                dict->addEntryCStr("isreadable", Value::makeBool(false));
                dict->addEntryCStr("iswritable", Value::makeBool(true));
            }
            dict->addEntryCStr("isexecutable", Value::makeBool(false));
            dict->addEntryCStr("size", Value::makeNumber(1));
        }
        return Value::fromObject(dict);
    }

    Value objfn_file_member_path(State* state, CallState* args)
    {
        File* file;
        args->checkCount(0);
        file = args->thisval.asFile();
        DENY_STD(state, file, args);
        return Value::fromObject(file->m_filepath);
    }

    Value objfn_file_member_mode(State* state, CallState* args)
    {
        File* file;
        (void)state;
        args->checkCount(0);
        file = args->thisval.asFile();
        return Value::fromObject(file->m_filemode);
    }

    Value objfn_file_member_name(State* state, CallState* args)
    {
        char* name;
        File* file;
        args->checkCount(0);
        file = args->thisval.asFile();
        if(!file->m_isstd)
        {
            name = osfn_basename(file->m_filepath->data());
            return Value::fromObject(String::copy(state, name));
        }
        else if(file->m_istty)
        {
            /*name = ttyname(file->m_filedesc);*/
            name = Util::strCopy(state, "<tty>");
            if(name != nullptr)
            {
                return Value::fromObject(String::copy(state, name));
            }
        }
        return Value::makeNull();
    }

    Value objfn_file_member_seek(State* state, CallState* args)
    {
        long position;
        int seektype;
        File* file;
        args->checkCount(2);
        args->checkByFunc(0, &Value::isNumber);
        args->checkByFunc(1, &Value::isNumber);
        file = args->thisval.asFile();
        DENY_STD(state, file, args);
        position = (long)args->argv[0].asNumber();
        seektype = args->argv[1].asNumber();
        RETURN_STATUS(fseek(file->m_filehandle, position, seektype));
    }

    Value objfn_file_member_tell(State* state, CallState* args)
    {
        File* file;
        args->checkCount(0);
        file = args->thisval.asFile();
        DENY_STD(state, file, args);
        return Value::makeNumber(ftell(file->m_filehandle));
    }

    #undef FILE_ERROR
    #undef RETURN_STATUS
    #undef DENY_STD

    Value objfn_array_member_length(State* state, CallState* args)
    {
        Array* selfarr;
        (void)state;
        selfarr = args->thisval.asArray();
        return Value::makeNumber(selfarr->size());
    }

    Value objfn_array_member_append(State* state, CallState* args)
    {
        int i;
        (void)state;
        for(i = 0; i < args->argc; i++)
        {
            args->thisval.asArray()->push(args->argv[i]);
        }
        return Value::makeEmpty();
    }

    Value objfn_array_member_clear(State* state, CallState* args)
    {
        (void)state;
        args->checkCount(0);
        args->thisval.asArray()->clear();
        return Value::makeEmpty();
    }

    Value objfn_array_member_clone(State* state, CallState* args)
    {
        Array* list;
        (void)state;
        args->checkCount(0);
        list = args->thisval.asArray();
        return Value::fromObject(list->copy(0, list->size()));
    }

    Value objfn_array_member_count(State* state, CallState* args)
    {
        size_t i;
        size_t count;
        Array* list;
        (void)state;
        args->checkCount(1);
        list = args->thisval.asArray();
        count = 0;
        for(i = 0; i < list->size(); i++)
        {
            if(list->at(i).compare(args->argv[0]))
            {
                count++;
            }
        }
        return Value::makeNumber(count);
    }

    Value objfn_array_member_extend(State* state, CallState* args)
    {
        size_t i;
        Array* list;
        Array* list2;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isArray);
        list = args->thisval.asArray();
        list2 = args->argv[0].asArray();
        for(i = 0; i < list2->size(); i++)
        {
            list->push(list2->at(i));
        }
        return Value::makeEmpty();
    }

    Value objfn_array_member_indexof(State* state, CallState* args)
    {
        size_t i;
        Array* list;
        NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
        list = args->thisval.asArray();
        i = 0;
        if(args->argc == 2)
        {
            args->checkByFunc(1, &Value::isNumber);
            i = args->argv[1].asNumber();
        }
        for(; i < list->size(); i++)
        {
            if(list->at(i).compare(args->argv[0]))
            {
                return Value::makeNumber(i);
            }
        }
        return Value::makeNumber(-1);
    }

    Value objfn_array_member_insert(State* state, CallState* args)
    {
        size_t index;
        Array* list;
        (void)state;
        args->checkCount(2);
        args->checkByFunc(1, &Value::isNumber);
        list = args->thisval.asArray();
        index = args->argv[1].asNumber();
        list->m_varray->insert(args->argv[0], index);
        return Value::makeEmpty();
    }

    Value objfn_array_member_pop(State* state, CallState* args)
    {
        Value value;
        Array* list;
        (void)state;
        args->checkCount(0);
        list = args->thisval.asArray();
        if(list->size() > 0)
        {
            value = list->at(list->size() - 1);
            list->m_varray->m_count--;
            return value;
        }
        return Value::makeNull();
    }

    Value objfn_array_member_shift(State* state, CallState* args)
    {
        size_t i;
        size_t j;
        size_t count;
        Array* list;
        Array* newlist;
        NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
        count = 1;
        if(args->argc == 1)
        {
            args->checkByFunc(0, &Value::isNumber);
            count = args->argv[0].asNumber();
        }
        list = args->thisval.asArray();
        if(count >= list->size() || list->size() == 1)
        {
            list->m_varray->m_count = 0;
            return Value::makeNull();
        }
        else if(count > 0)
        {
            newlist = state->m_vmstate->gcProtect(Array::make(state));
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
                return Value::fromObject(newlist);
            }
        }
        return Value::makeNull();
    }

    Value objfn_array_member_removeat(State* state, CallState* args)
    {
        size_t i;
        int index;
        Value value;
        Array* list;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isNumber);
        list = args->thisval.asArray();
        index = args->argv[0].asNumber();
        if(index < 0 || index >= int(list->size()))
        {
            return state->m_vmstate->raiseFromFunction(args, "list index %d out of range at remove_at()", index);
        }
        value = list->at(index);
        for(i = index; i < list->size() - 1; i++)
        {
            list->m_varray->m_values[i] = list->at(i + 1);
        }
        list->m_varray->m_count--;
        return value;
    }

    Value objfn_array_member_remove(State* state, CallState* args)
    {
        size_t i;
        int index;
        Array* list;
        (void)state;
        args->checkCount(1);
        list = args->thisval.asArray();
        index = -1;
        for(i = 0; i < list->size(); i++)
        {
            if(list->at(i).compare(args->argv[0]))
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
        return Value::makeEmpty();
    }

    Value objfn_array_member_reverse(State* state, CallState* args)
    {
        int fromtop;
        Array* list;
        Array* nlist;
        args->checkCount(0);
        list = args->thisval.asArray();
        nlist = state->m_vmstate->gcProtect(Array::make(state));
        /* in-place reversal:*/
        /*
        int start = 0;
        int end = list->size() - 1;
        while(start < end)
        {
            Value temp = list->at(start);
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
        return Value::fromObject(nlist);
    }

    Value objfn_array_member_sort(State* state, CallState* args)
    {
        Array* list;
        ValArray::QuickSort qs(state);
        args->checkCount(0);
        list = args->thisval.asArray();
        qs.runSort(list->m_varray, 0, list->size()-0);
        return Value::fromObject(list);
    }

    Value objfn_array_member_contains(State* state, CallState* args)
    {
        size_t i;
        Array* list;
        (void)state;
        args->checkCount(1);
        list = args->thisval.asArray();
        for(i = 0; i < list->size(); i++)
        {
            if(args->argv[0].compare(list->at(i)))
            {
                return Value::makeBool(true);
            }
        }
        return Value::makeBool(false);
    }

    Value objfn_array_member_delete(State* state, CallState* args)
    {
        int i;
        int idxupper;
        int idxlower;
        Array* list;
        NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
        args->checkByFunc(0, &Value::isNumber);
        idxlower = args->argv[0].asNumber();
        idxupper = idxlower;
        if(args->argc == 2)
        {
            args->checkByFunc(1, &Value::isNumber);
            idxupper = args->argv[1].asNumber();
        }
        list = args->thisval.asArray();
        if(idxlower < 0 || idxlower >= int(list->size()))
        {
            return state->m_vmstate->raiseFromFunction(args, "list index %d out of range at delete()", idxlower);
        }
        else if(idxupper < idxlower || idxupper >= int(list->size()))
        {
            return state->m_vmstate->raiseFromFunction(args, "invalid upper limit %d at delete()", idxupper);
        }
        for(i = 0; i < int(list->size() - idxupper); i++)
        {
            list->m_varray->m_values[idxlower + i] = list->at(i + idxupper + 1);
        }
        list->m_varray->m_count -= idxupper - idxlower + 1;
        return Value::makeNumber((double)idxupper - (double)idxlower + 1);
    }

    Value objfn_array_member_first(State* state, CallState* args)
    {
        Array* list;
        (void)state;
        args->checkCount(0);
        list = args->thisval.asArray();
        if(list->size() > 0)
        {
            return list->at(0);
        }
        return Value::makeNull();
    }

    Value objfn_array_member_last(State* state, CallState* args)
    {
        Array* list;
        (void)state;
        args->checkCount(0);
        list = args->thisval.asArray();
        if(list->size() > 0)
        {
            return list->at(list->size() - 1);
        }
        return Value::makeNull();
    }

    Value objfn_array_member_isempty(State* state, CallState* args)
    {
        (void)state;
        args->checkCount(0);
        return Value::makeBool(args->thisval.asArray()->size() == 0);
    }

    Value objfn_array_member_take(State* state, CallState* args)
    {
        int count;
        Array* list;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isNumber);
        list = args->thisval.asArray();
        count = args->argv[0].asNumber();
        if(count < 0)
        {
            count = list->count() + count;
        }
        if(int(list->count()) < count)
        {
            return Value::fromObject(list->copy(0, list->count()));
        }
        return Value::fromObject(list->copy(0, count));
    }

    Value objfn_array_member_get(State* state, CallState* args)
    {
        int index;
        Array* list;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isNumber);
        list = args->thisval.asArray();
        index = args->argv[0].asNumber();
        if(index < 0 || index >= int(list->count()))
        {
            return Value::makeNull();
        }
        return list->at(index);
    }

    Value objfn_array_member_compact(State* state, CallState* args)
    {
        size_t i;
        Array* list;
        Array* newlist;
        args->checkCount(0);
        list = args->thisval.asArray();
        newlist = state->m_vmstate->gcProtect(Array::make(state));
        for(i = 0; i < list->count(); i++)
        {
            if(!list->at(i).compare(Value::makeNull()))
            {
                newlist->push(list->at(i));
            }
        }
        return Value::fromObject(newlist);
    }

    Value objfn_array_member_unique(State* state, CallState* args)
    {
        size_t i;
        size_t j;
        bool found;
        Array* list;
        Array* newlist;
        args->checkCount(0);
        list = args->thisval.asArray();
        newlist = state->m_vmstate->gcProtect(Array::make(state));
        for(i = 0; i < list->count(); i++)
        {
            found = false;
            for(j = 0; j < newlist->count(); j++)
            {
                if(newlist->at(j).compare(list->at(i)))
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
        return Value::fromObject(newlist);
    }

    Value objfn_array_member_zip(State* state, CallState* args)
    {
        size_t i;
        size_t j;
        Array* list;
        Array* newlist;
        Array* alist;
        Array** arglist;
        list = args->thisval.asArray();
        newlist = state->m_vmstate->gcProtect(Array::make(state));
        arglist = (Array**)State::VM::gcAllocMemory(state, sizeof(Array*), args->argc);
        for(i = 0; i < size_t(args->argc); i++)
        {
            args->checkByFunc(i, &Value::isArray);
            arglist[i] = args->argv[i].asArray();
        }
        for(i = 0; i < list->count(); i++)
        {
            alist = state->m_vmstate->gcProtect(Array::make(state));
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
                    alist->push(Value::makeNull());
                }
            }
            newlist->push(Value::fromObject(alist));
        }
        return Value::fromObject(newlist);
    }

    Value objfn_array_member_zipfrom(State* state, CallState* args)
    {
        size_t i;
        size_t j;
        Array* list;
        Array* newlist;
        Array* alist;
        Array* arglist;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isArray);
        list = args->thisval.asArray();
        newlist = state->m_vmstate->gcProtect(Array::make(state));
        arglist = args->argv[0].asArray();
        for(i = 0; i < arglist->count(); i++)
        {
            if(!arglist->at(i).isArray())
            {
                return state->m_vmstate->raiseFromFunction(args, "invalid list in zip entries");
            }
        }
        for(i = 0; i < list->count(); i++)
        {
            alist = state->m_vmstate->gcProtect(Array::make(state));
            alist->push(list->at(i));
            for(j = 0; j < arglist->count(); j++)
            {
                if(i < arglist->at(j).asArray()->count())
                {
                    alist->push(arglist->at(j).asArray()->at(i));
                }
                else
                {
                    alist->push(Value::makeNull());
                }
            }
            newlist->push(Value::fromObject(alist));
        }
        return Value::fromObject(newlist);
    }

    Value objfn_array_member_todict(State* state, CallState* args)
    {
        size_t i;
        Dictionary* dict;
        Array* list;
        args->checkCount(0);
        dict = state->m_vmstate->gcProtect(Dictionary::make(state));
        list = args->thisval.asArray();
        for(i = 0; i < list->count(); i++)
        {
            dict->setEntry(Value::makeNumber(i), list->at(i));
        }
        return Value::fromObject(dict);
    }

    Value objfn_array_member_iter(State* state, CallState* args)
    {
        int index;
        Array* list;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isNumber);
        list = args->thisval.asArray();
        index = args->argv[0].asNumber();
        if(index > -1 && index < int(list->count()))
        {
            return list->at(index);
        }
        return Value::makeNull();
    }

    Value objfn_array_member_itern(State* state, CallState* args)
    {
        int index;
        Array* list;
        args->checkCount(1);
        list = args->thisval.asArray();
        if(args->argv[0].isNull())
        {
            if(list->count() == 0)
            {
                return Value::makeBool(false);
            }
            return Value::makeNumber(0);
        }
        if(!args->argv[0].isNumber())
        {
            return state->m_vmstate->raiseFromFunction(args, "lists are numerically indexed");
        }
        index = args->argv[0].asNumber();
        if(index < int(list->count() - 1))
        {
            return Value::makeNumber((double)index + 1);
        }
        return Value::makeNull();
    }

    Value objfn_array_member_each(State* state, CallState* args)
    {
        size_t i;
        int arity;
        Value callable;
        Value unused;
        Array* list;
        ValArray nestargs;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isCallable);
        list = args->thisval.asArray();
        callable = args->argv[0];
        NestCall nc(state);
        arity = nc.prepare(callable, args->thisval, nestargs);
        for(i = 0; i < list->count(); i++)
        {
            if(arity > 0)
            {
                nestargs[0] = list->at(i);
                if(arity > 1)
                {
                    nestargs[1] = Value::makeNumber(i);
                }
            }
            nc.callNested(callable, args->thisval, nestargs, &unused);
        }
        return Value::makeEmpty();
    }

    Value objfn_array_member_map(State* state, CallState* args)
    {
        size_t i;
        int arity;
        Value res;
        Value callable;
        Array* selfarr;
        ValArray nestargs;
        Array* resultlist;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isCallable);
        selfarr = args->thisval.asArray();
        callable = args->argv[0];
        NestCall nc(state);
        arity = nc.prepare(callable, args->thisval, nestargs);
        resultlist = state->m_vmstate->gcProtect(Array::make(state));
        //nestargs->push(Value::makeEmpty());
        for(i = 0; i < selfarr->count(); i++)
        {
            //nestargs.push(selfarr->at(i));
            nestargs[0] = selfarr->at(i);
            if(arity > 1)
            {
                nestargs[1] = Value::makeNumber(i);
            }
            nc.callNested(callable, args->thisval, nestargs, &res);
            resultlist->push(res);
        }
        return Value::fromObject(resultlist);
    }

    Value objfn_array_member_filter(State* state, CallState* args)
    {
        size_t i;
        int arity;
        Value callable;
        Value result;
        Array* selfarr;
        ValArray nestargs;
        Array* resultlist;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isCallable);
        selfarr = args->thisval.asArray();
        callable = args->argv[0];
        NestCall nc(state);
        arity = nc.prepare(callable, args->thisval, nestargs);
        resultlist = state->m_vmstate->gcProtect(Array::make(state));
        for(i = 0; i < selfarr->count(); i++)
        {
            if(!selfarr->at(i).isEmpty())
            {
                if(arity > 0)
                {
                    nestargs[0] = selfarr->at(i);
                    if(arity > 1)
                    {
                        nestargs[1] = Value::makeNumber(i);
                    }
                }
                nc.callNested(callable, args->thisval, nestargs, &result);
                if(!Value::isFalse(result))
                {
                    resultlist->push(selfarr->at(i));
                }
            }
        }
        return Value::fromObject(resultlist);
    }

    Value objfn_array_member_some(State* state, CallState* args)
    {
        size_t i;
        int arity;
        Value callable;
        Value result;
        Array* selfarr;
        ValArray nestargs;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isCallable);
        selfarr = args->thisval.asArray();
        callable = args->argv[0];
        NestCall nc(state);
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
                        nestargs[1] = Value::makeNumber(i);
                    }
                }
                nc.callNested(callable, args->thisval, nestargs, &result);
                if(!Value::isFalse(result))
                {
                    return Value::makeBool(true);
                }
            }
        }
        return Value::makeBool(false);
    }

    Value objfn_array_member_every(State* state, CallState* args)
    {
        size_t i;
        int arity;
        Value result;
        Value callable;
        Array* selfarr;
        ValArray nestargs;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isCallable);
        selfarr = args->thisval.asArray();
        callable = args->argv[0];
        NestCall nc(state);
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
                        nestargs[1] = Value::makeNumber(i);
                    }
                }
                nc.callNested(callable, args->thisval, nestargs, &result);
                if(Value::isFalse(result))
                {
                    return Value::makeBool(false);
                }
            }
        }
        return Value::makeBool(true);
    }

    Value objfn_array_member_reduce(State* state, CallState* args)
    {
        size_t i;
        int arity;
        int startindex;
        Value callable;
        Value accumulator;
        Array* selfarr;
        ValArray nestargs;
        NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
        args->checkByFunc(0, &Value::isCallable);
        selfarr = args->thisval.asArray();
        callable = args->argv[0];
        startindex = 0;
        accumulator = Value::makeNull();
        if(args->argc == 2)
        {
            accumulator = args->argv[1];
        }
        if(accumulator.isNull() && selfarr->count() > 0)
        {
            accumulator = selfarr->at(0);
            startindex = 1;
        }
        NestCall nc(state);
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
                            nestargs[2] = Value::makeNumber(i);
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
        return accumulator;
    }

    Value objfn_range_member_lower(State* state, CallState* args)
    {
        (void)state;
        args->checkCount(0);
        return Value::makeNumber(args->thisval.asRange()->m_lower);
    }

    Value objfn_range_member_upper(State* state, CallState* args)
    {
        (void)state;
        args->checkCount(0);
        return Value::makeNumber(args->thisval.asRange()->m_upper);
    }

    Value objfn_range_member_range(State* state, CallState* args)
    {
        (void)state;
        args->checkCount(0);
        return Value::makeNumber(args->thisval.asRange()->m_range);
    }

    Value objfn_range_member_iter(State* state, CallState* args)
    {
        int val;
        int index;
        Range* range;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isNumber);
        range = args->thisval.asRange();
        index = args->argv[0].asNumber();
        if(index >= 0 && index < range->m_range)
        {
            if(index == 0)
            {
                return Value::makeNumber(range->m_lower);
            }
            if(range->m_lower > range->m_upper)
            {
                val = --range->m_lower;
            }
            else
            {
                val = ++range->m_lower;
            }
            return Value::makeNumber(val);
        }
        return Value::makeNull();
    }

    Value objfn_range_member_itern(State* state, CallState* args)
    {
        int index;
        Range* range;
        args->checkCount(1);
        range = args->thisval.asRange();
        if(args->argv[0].isNull())
        {
            if(range->m_range == 0)
            {
                return Value::makeNull();
            }
            return Value::makeNumber(0);
        }
        if(!args->argv[0].isNumber())
        {
            return state->m_vmstate->raiseFromFunction(args, "ranges are numerically indexed");
        }
        index = (int)args->argv[0].asNumber() + 1;
        if(index < range->m_range)
        {
            return Value::makeNumber(index);
        }
        return Value::makeNull();
    }

    Value objfn_range_member_loop(State* state, CallState* args)
    {
        int i;
        int arity;
        Value callable;
        Value unused;
        Range* range;
        ValArray nestargs;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isCallable);
        range = args->thisval.asRange();
        callable = args->argv[0];
        NestCall nc(state);
        arity = nc.prepare(callable, args->thisval, nestargs);
        for(i = 0; i < range->m_range; i++)
        {
            if(arity > 0)
            {
                nestargs[0] = Value::makeNumber(i);
                if(arity > 1)
                {
                    nestargs[1] = Value::makeNumber(i);
                }
            }
            nc.callNested(callable, args->thisval, nestargs, &unused);
        }
        return Value::makeEmpty();
    }

    Value objfn_range_member_expand(State* state, CallState* args)
    {
        int i;
        Value val;
        Range* range;
        Array* oa;
        range = args->thisval.asRange();
        oa = Array::make(state);
        for(i = 0; i < range->m_range; i++)
        {
            val = Value::makeNumber(i);
            oa->push(val);
        }
        return Value::fromObject(oa);
    }

    Value objfn_range_member_constructor(State* state, CallState* args)
    {
        int a;
        int b;
        Range* orng;
        a = args->argv[0].asNumber();
        b = args->argv[1].asNumber();
        orng = Range::make(state, a, b);
        return Value::fromObject(orng);
    }

    Value objfn_string_static_utf8numbytes(State* state, CallState* args)
    {
        int incode;
        int res;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isNumber);
        incode = args->argv[0].asNumber();
        //static int utf8NumBytes(int value)
        res = Util::utf8NumBytes(incode);
        return Value::makeNumber(res);
    }

    Value objfn_string_static_utf8decode(State* state, CallState* args)
    {
        int res;
        String* instr;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isString);
        instr = args->argv[0].asString();
        //static int utf8Decode(const uint8_t* bytes, uint32_t length)
        res = Util::utf8Decode((const uint8_t*)instr->data(), instr->length());
        return Value::makeNumber(res);
    }

    Value objfn_string_static_utf8encode(State* state, CallState* args)
    {
        int incode;
        size_t len;
        String* res;
        char* buf;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isNumber);
        incode = args->argv[0].asNumber();
        //static char* utf8Encode(unsigned int code)
        buf = Util::utf8Encode(state, incode, &len);
        res = String::take(state, buf, len);
        return Value::fromObject(res);
    }

    Value nn_util_stringutf8chars(State* state, CallState* args, bool onlycodepoint)
    {
        int cp;
        const char* cstr;
        Array* res;
        String* os;
        String* instr;
        (void)state;
        instr = args->thisval.asString();
        res = Array::make(state);
        Util::Utf8Iterator iter(instr->data(), instr->length());
        while(iter.next() != 0u)
        {
            cp = iter.m_codepoint;
            cstr = iter.getCharStr();
            if(onlycodepoint)
            {
                res->push(Value::makeNumber(cp));
            }
            else
            {
                os = String::copy(state, cstr, iter.m_charsize);
                res->push(Value::fromObject(os));
            }
        }
        return Value::fromObject(res);
    }

    Value objfn_string_member_utf8chars(State* state, CallState* args)
    {
        return nn_util_stringutf8chars(state, args, false);
    }

    Value objfn_string_member_utf8codepoints(State* state, CallState* args)
    {
        return nn_util_stringutf8chars(state, args, true);
    }

    Value objfn_string_static_fromcharcode(State* state, CallState* args)
    {
        char ch;
        String* os;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isNumber);
        ch = args->argv[0].asNumber();
        os = String::copy(state, &ch, 1);
        return Value::fromObject(os);
    }

    Value objfn_string_member_constructor(State* state, CallState* args)
    {
        String* os;
        args->checkCount(0);
        os = String::copy(state, "", 0);
        return Value::fromObject(os);
    }

    Value objfn_string_member_length(State* state, CallState* args)
    {
        String* selfstr;
        (void)state;
        args->checkCount(0);
        selfstr = args->thisval.asString();
        return Value::makeNumber(selfstr->length());
    }

    Value objfn_string_member_substring(State* state, CallState* args)
    {
        size_t end;
        size_t start;
        size_t maxlen;
        String* nos;
        String* selfstr;
        (void)state;
        selfstr = args->thisval.asString();
        args->checkByFunc(0, &Value::isNumber);
        maxlen = selfstr->length();
        end = maxlen;
        start = args->argv[0].asNumber();
        if(args->argc > 1)
        {
            args->checkByFunc(1, &Value::isNumber);
            end = args->argv[1].asNumber();
        }
        nos = selfstr->substr(start, end, true);
        return Value::fromObject(nos);
    }

    Value objfn_string_member_charcodeat(State* state, CallState* args)
    {
        int ch;
        int idx;
        int selflen;
        String* selfstr;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isNumber);
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
        return Value::makeNumber(ch);
    }

    Value objfn_string_member_charat(State* state, CallState* args)
    {
        char ch;
        int idx;
        int selflen;
        String* selfstr;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isNumber);
        selfstr = args->thisval.asString();
        idx = args->argv[0].asNumber();
        selflen = (int)selfstr->length();
        if((idx < 0) || (idx >= selflen))
        {
            return Value::fromObject(String::copy(state, "", 0));
        }
        else
        {
            ch = selfstr->data()[idx];
        }
        return Value::fromObject(String::copy(state, &ch, 1));
    }

    Value objfn_string_member_upper(State* state, CallState* args)
    {
        size_t slen;
        String* str;
        String* copied;
        args->checkCount(0);
        str = args->thisval.asString();
        copied = String::copy(state, str->data(), str->length());
        slen = copied->length();
        (void)Util::strToUpperInplace(copied->mutableData(), slen);
        return Value::fromObject(copied);
    }

    Value objfn_string_member_lower(State* state, CallState* args)
    {
        size_t slen;
        String* str;
        String* copied;
        args->checkCount(0);
        str = args->thisval.asString();
        copied = String::copy(state, str->data(), str->length());
        slen = copied->length();
        (void)Util::strToLowerInplace(copied->mutableData(), slen);
        return Value::fromObject(copied);
    }

    Value objfn_string_member_isalpha(State* state, CallState* args)
    {
        int i;
        String* selfstr;
        (void)state;
        args->checkCount(0);
        selfstr = args->thisval.asString();
        for(i = 0; i < (int)selfstr->length(); i++)
        {
            if(isalpha((unsigned char)selfstr->data()[i]) == 0)
            {
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(selfstr->length() != 0);
    }

    Value objfn_string_member_isalnum(State* state, CallState* args)
    {
        int i;
        String* selfstr;
        (void)state;
        args->checkCount(0);
        selfstr = args->thisval.asString();
        for(i = 0; i < (int)selfstr->length(); i++)
        {
            if(isalnum((unsigned char)selfstr->data()[i]) == 0)
            {
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(selfstr->length() != 0);
    }

    Value objfn_string_member_isfloat(State* state, CallState* args)
    {
        double f;
        char* p;
        String* selfstr;
        (void)f;
        (void)state;
        args->checkCount(0);
        selfstr = args->thisval.asString();
        errno = 0;
        if(selfstr->length() ==0)
        {
            return Value::makeBool(false);
        }
        f = strtod(selfstr->data(), &p);
        if(errno)
        {
            return Value::makeBool(false);
        }
        else
        {
            if(*p == 0)
            {
                return Value::makeBool(true);
            }
        }
        return Value::makeBool(false);
    }

    Value objfn_string_member_isnumber(State* state, CallState* args)
    {
        int i;
        String* selfstr;
        (void)state;
        args->checkCount(0);
        selfstr = args->thisval.asString();
        for(i = 0; i < (int)selfstr->length(); i++)
        {
            if(isdigit((unsigned char)selfstr->data()[i]) == 0)
            {
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(selfstr->length() != 0);
    }

    Value objfn_string_member_islower(State* state, CallState* args)
    {
        int i;
        bool alphafound;
        String* selfstr;
        (void)state;
        args->checkCount(0);
        selfstr = args->thisval.asString();
        alphafound = false;
        for(i = 0; i < (int)selfstr->length(); i++)
        {
            if(!alphafound && (isdigit(selfstr->data()[0]) == 0))
            {
                alphafound = true;
            }
            if(isupper(selfstr->data()[0]) != 0)
            {
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(alphafound);
    }

    Value objfn_string_member_isupper(State* state, CallState* args)
    {
        int i;
        bool alphafound;
        String* selfstr;
        (void)state;
        args->checkCount(0);
        selfstr = args->thisval.asString();
        alphafound = false;
        for(i = 0; i < (int)selfstr->length(); i++)
        {
            if(!alphafound && (isdigit(selfstr->data()[0]) == 0))
            {
                alphafound = true;
            }
            if(islower(selfstr->data()[0]) != 0)
            {
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(alphafound);
    }

    Value objfn_string_member_isspace(State* state, CallState* args)
    {
        int i;
        String* selfstr;
        (void)state;
        args->checkCount(0);
        selfstr = args->thisval.asString();
        for(i = 0; i < (int)selfstr->length(); i++)
        {
            if(isspace((unsigned char)selfstr->data()[i]) == 0)
            {
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(selfstr->length() != 0);
    }

    Value objfn_string_member_repeat(State* state, CallState* args)
    {
        size_t i;
        size_t cnt;
        String* selfstr;
        (void)state;
        args->checkCount(1);
        selfstr = args->thisval.asString();
        cnt = args->at(0).asNumber();
        auto os = String::make(state);
        for(i = 0; i < cnt; i++)
        {
            os->append(selfstr);
        }
        return Value::fromObject(os);
    }


    Value objfn_string_member_trim(State* state, CallState* args)
    {
        char trimmer;
        char* end;
        char* string;
        String* selfstr;
        String* copied;
        NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
        trimmer = '\0';
        if(args->argc == 1)
        {
            trimmer = (char)args->argv[0].asString()->data()[0];
        }
        selfstr = args->thisval.asString();
        copied = String::copy(state, selfstr->data(), selfstr->length());
        string = copied->mutableData();
        end = nullptr;
        /* Trim leading space*/
        if(trimmer == '\0')
        {
            while(isspace((unsigned char)*string) != 0)
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
            return Value::fromObject(String::copy(state, "", 0));
        }
        /* Trim trailing space */
        end = string + strlen(string) - 1;
        if(trimmer == '\0')
        {
            while(end > string && (isspace((unsigned char)*end) != 0))
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
        return Value::fromObject(String::copy(state, string));
    }

    Value objfn_string_member_ltrim(State* state, CallState* args)
    {
        char trimmer;
        char* end;
        char* string;
        String* selfstr;
        String* copied;
        NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
        trimmer = '\0';
        if(args->argc == 1)
        {
            trimmer = (char)args->argv[0].asString()->data()[0];
        }
        selfstr = args->thisval.asString();
        copied = String::copy(state, selfstr->data(), selfstr->length());
        string = copied->mutableData();
        end = nullptr;
        /* Trim leading space */
        if(trimmer == '\0')
        {
            while(isspace((unsigned char)*string) != 0)
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
            return Value::fromObject(String::copy(state, "", 0));
        }
        end = string + strlen(string) - 1;
        end[1] = '\0';
        return Value::fromObject(String::copy(state, string));
    }

    Value objfn_string_member_rtrim(State* state, CallState* args)
    {
        char trimmer;
        char* end;
        char* string;
        String* selfstr;
        String* copied;
        NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
        trimmer = '\0';
        if(args->argc == 1)
        {
            trimmer = (char)args->argv[0].asString()->data()[0];
        }
        selfstr = args->thisval.asString();
        copied = String::copy(state, selfstr->data(), selfstr->length());
        string = copied->mutableData();
        end = nullptr;
        /* All spaces? */
        if(*string == 0)
        {
            return Value::fromObject(String::copy(state, "", 0));
        }
        end = string + strlen(string) - 1;
        if(trimmer == '\0')
        {
            while(end > string && (isspace((unsigned char)*end) != 0))
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
        return Value::fromObject(String::copy(state, string));
    }

    Value objfn_array_member_constructor(State* state, CallState* args)
    {
        int cnt;
        Value filler;
        Array* arr;
        NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
        args->checkByFunc(0, &Value::isNumber);
        filler = Value::makeEmpty();
        if(args->argc > 1)
        {
            filler = args->argv[1];
        }
        cnt = args->argv[0].asNumber();
        arr = Array::make(state, cnt, filler);
        return Value::fromObject(arr);
    }

    Value objfn_array_member_join(State* state, CallState* args)
    {
        int i;
        int count;
        Value vjoinee;
        Array* selfarr;
        String* joinee;
        Value* list;
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
                joinee = Value::toString(state, vjoinee);
            }
        }
        list = selfarr->m_varray->m_values;
        count = selfarr->count();
        if(count == 0)
        {
            return Value::fromObject(String::copy(state, ""));
        }
        Printer pd(state);
        for(i = 0; i < count; i++)
        {
            pd.printValue(list[i], false, true);
            if((joinee != nullptr) && ((i+1) < count))
            {
                pd.put(joinee->data(), joinee->length());
            }
        }
        return Value::fromObject(pd.takeString());
    }

    Value objfn_string_member_indexof(State* state, CallState* args)
    {
        int startindex;
        const char* result;
        const char* haystack;
        String* string;
        String* needle;
        (void)state;
        NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
        args->checkByFunc(0, &Value::isString);
        string = args->thisval.asString();
        needle = args->argv[0].asString();
        startindex = 0;
        if(args->argc == 2)
        {
            args->checkByFunc(1, &Value::isNumber);
            startindex = args->argv[1].asNumber();
        }
        if(string->length() > 0 && needle->length() > 0)
        {
            haystack = string->data();
            result = strstr(haystack + startindex, needle->data());
            if(result != nullptr)
            {
                return Value::makeNumber((int)(result - haystack));
            }
        }
        return Value::makeNumber(-1);
    }

    Value objfn_string_member_startswith(State* state, CallState* args)
    {
        String* substr;
        String* string;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isString);
        string = args->thisval.asString();
        substr = args->argv[0].asString();
        if(string->length() == 0 || substr->length() == 0 || substr->length() > string->length())
        {
            return Value::makeBool(false);
        }
        return Value::makeBool(memcmp(substr->data(), string->data(), substr->length()) == 0);
    }

    Value objfn_string_member_endswith(State* state, CallState* args)
    {
        int difference;
        String* substr;
        String* string;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isString);
        string = args->thisval.asString();
        substr = args->argv[0].asString();
        if(string->length() == 0 || substr->length() == 0 || substr->length() > string->length())
        {
            return Value::makeBool(false);
        }
        difference = string->length() - substr->length();
        return Value::makeBool(memcmp(substr->data(), string->data() + difference, substr->length()) == 0);
    }

    Value objfn_string_member_count(State* state, CallState* args)
    {
        int count;
        const char* tmp;
        String* substr;
        String* string;
        (void)state;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isString);
        string = args->thisval.asString();
        substr = args->argv[0].asString();
        if(substr->length() == 0 || string->length() == 0)
        {
            return Value::makeNumber(0);
        }
        count = 0;
        tmp = string->data();
        while((tmp = Util::utf8Find(tmp, string->length(), substr->data(), substr->length())) != nullptr)
        {
            count++;
            tmp++;
        }
        return Value::makeNumber(count);
    }

    Value objfn_string_member_tonumber(State* state, CallState* args)
    {
        String* selfstr;
        (void)state;
        args->checkCount(0);
        selfstr = args->thisval.asString();
        return Value::makeNumber(strtod(selfstr->data(), nullptr));
    }

    Value objfn_string_member_isascii(State* state, CallState* args)
    {
        String* string;
        (void)state;
        NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
        if(args->argc == 1)
        {
            args->checkByFunc(0, &Value::isBool);
        }
        string = args->thisval.asString();
        return Value::fromObject(string);
    }

    Value objfn_string_member_tolist(State* state, CallState* args)
    {
        int i;
        int end;
        int start;
        int length;
        Array* list;
        String* string;
        args->checkCount(0);
        string = args->thisval.asString();
        list = state->m_vmstate->gcProtect(Array::make(state));
        length = string->length();
        if(length > 0)
        {
            for(i = 0; i < length; i++)
            {
                start = i;
                end = i + 1;
                list->push(Value::fromObject(String::copy(state, string->data() + start, (int)(end - start))));
            }
        }
        return Value::fromObject(list);
    }

    // TODO: lpad and rpad modify m_sbuf members!!!
    Value objfn_string_member_lpad(State* state, CallState* args)
    {
        int i;
        int width;
        int fillsize;
        int finalsize;
        int finalutf8size;
        char fillchar;
        char* str;
        char* fill;
        String* ofillstr;
        String* result;
        String* string;
        NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
        args->checkByFunc(0, &Value::isNumber);
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
        fill = (char*)State::VM::gcAllocMemory(state, sizeof(char), (size_t)fillsize + 1);
        finalsize = string->m_sbuf->m_length + fillsize;
        finalutf8size = string->m_sbuf->m_length + fillsize;
        for(i = 0; i < fillsize; i++)
        {
            fill[i] = fillchar;
        }
        str = (char*)State::VM::gcAllocMemory(state, sizeof(char), (size_t)finalsize + 1);
        memcpy(str, fill, fillsize);
        memcpy(str + fillsize, string->m_sbuf->m_data, string->m_sbuf->m_length);
        str[finalsize] = '\0';
        Memory::freeArray(fill, fillsize + 1);
        result = String::take(state, str, finalsize);
        result->m_sbuf->m_length = finalutf8size;
        result->m_sbuf->m_length = finalsize;
        return Value::fromObject(result);
    }

    Value objfn_string_member_rpad(State* state, CallState* args)
    {
        int i;
        int width;
        int fillsize;
        int finalsize;
        int finalutf8size;
        char fillchar;
        char* str;
        char* fill;
        String* ofillstr;
        String* string;
        String* result;
        NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
        args->checkByFunc(0, &Value::isNumber);
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
        fill = (char*)State::VM::gcAllocMemory(state, sizeof(char), (size_t)fillsize + 1);
        finalsize = string->m_sbuf->m_length + fillsize;
        finalutf8size = string->m_sbuf->m_length + fillsize;
        for(i = 0; i < fillsize; i++)
        {
            fill[i] = fillchar;
        }
        str = (char*)State::VM::gcAllocMemory(state, sizeof(char), (size_t)finalsize + 1);
        memcpy(str, string->m_sbuf->m_data, string->m_sbuf->m_length);
        memcpy(str + string->m_sbuf->m_length, fill, fillsize);
        str[finalsize] = '\0';
        Memory::freeArray(fill, fillsize + 1);
        result = String::take(state, str, finalsize);
        result->m_sbuf->m_length = finalutf8size;
        result->m_sbuf->m_length = finalsize;
        return Value::fromObject(result);
    }

    Value objfn_string_member_split(State* state, CallState* args)
    {
        int i;
        int end;
        int start;
        int length;
        Array* list;
        String* string;
        String* delimeter;
        NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
        args->checkByFunc(0, &Value::isString);
        string = args->thisval.asString();
        delimeter = args->argv[0].asString();
        /* empty string matches empty string to empty list */
        if(((string->length() == 0) && (delimeter->length() == 0)) || (string->length() == 0) || (delimeter->length() == 0))
        {
            return Value::fromObject(Array::make(state));
        }
        list = state->m_vmstate->gcProtect(Array::make(state));
        if(delimeter->length() > 0)
        {
            start = 0;
            for(i = 0; i <= (int)string->length(); i++)
            {
                /* match found. */
                if(memcmp(string->data() + i, delimeter->data(), delimeter->length()) == 0 || i == (int)string->length())
                {
                    list->push(Value::fromObject(String::copy(state, string->data() + start, i - start)));
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
                list->push(Value::fromObject(String::copy(state, string->data() + start, (int)(end - start))));
            }
        }
        return Value::fromObject(list);
    }

    Value objfn_string_member_replace(State* state, CallState* args)
    {
        int i;
        int totallength;
        Util::StrBuffer* result;
        String* substr;
        String* string;
        String* repsubstr;
        NEON_ARGS_CHECKCOUNTRANGE(args, 2, 3);
        args->checkByFunc(0, &Value::isString);
        args->checkByFunc(1, &Value::isString);
        string = args->thisval.asString();
        substr = args->argv[0].asString();
        repsubstr = args->argv[1].asString();
        if((string->length() == 0 && substr->length() == 0) || string->length() == 0 || substr->length() == 0)
        {
            return Value::fromObject(String::copy(state, string->data(), string->length()));
        }
        result = Memory::create<Util::StrBuffer>(0);
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
        return Value::fromObject(String::makeFromStrbuf(state, result, Util::hashString(result->m_data, result->m_length)));
    }

    #if 0
    Value objfn_string_member_chr(State* state, CallState* args)
    {
        size_t len;
        char* string;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isNumber);
        string = neon::Util::utf8Encode(state, (int)args->argv[0].asNumber(), &len);
        return neon::Value::fromObject(neon::String::take(state, string));
    }
    #endif

    Value objfn_string_member_iter(State* state, CallState* args)
    {
        int index;
        int length;
        String* string;
        String* result;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isNumber);
        string = args->thisval.asString();
        length = string->length();
        index = args->argv[0].asNumber();
        if(index > -1 && index < length)
        {
            result = String::copy(state, &string->data()[index], 1);
            return Value::fromObject(result);
        }
        return Value::makeNull();
    }

    Value objfn_string_member_itern(State* state, CallState* args)
    {
        int index;
        int length;
        String* string;
        args->checkCount(1);
        string = args->thisval.asString();
        length = string->length();
        if(args->argv[0].isNull())
        {
            if(length == 0)
            {
                return Value::makeBool(false);
            }
            return Value::makeNumber(0);
        }
        if(!args->argv[0].isNumber())
        {
            return state->m_vmstate->raiseFromFunction(args, "strings are numerically indexed");
        }
        index = args->argv[0].asNumber();
        if(index < length - 1)
        {
            return Value::makeNumber((double)index + 1);
        }
        return Value::makeNull();
    }

    Value objfn_string_member_each(State* state, CallState* args)
    {
        int i;
        int arity;
        Value callable;
        Value unused;
        String* string;
        ValArray nestargs;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isCallable);
        string = args->thisval.asString();
        callable = args->argv[0];
        NestCall nc(state);
        arity = nc.prepare(callable, args->thisval, nestargs);
        for(i = 0; i < (int)string->length(); i++)
        {
            if(arity > 0)
            {
                nestargs[0] = Value::fromObject(String::copy(state, string->data() + i, 1));
                if(arity > 1)
                {
                    nestargs[1] = Value::makeNumber(i);
                }
            }
            nc.callNested(callable, args->thisval, nestargs, &unused);
        }
        /* pop the argument list */
        return Value::makeEmpty();
    }

    Value objfn_object_member_dump(State* state, CallState* args)
    {
        Value v;
        String* os;
        v = args->thisval;
        Printer pd(state);
        pd.printValue(v, true, false);
        os = pd.takeString();
        return Value::fromObject(os);
    }

    Value objfn_object_member_tostring(State* state, CallState* args)
    {
        Value v;
        String* os;
        v = args->thisval;
        Printer pd(state);
        pd.printValue(v, false, true);
        os = pd.takeString();
        return Value::fromObject(os);
    }

    Value objfn_object_static_typename(State* state, CallState* args)
    {
        Value v;
        String* os;
        v = args->argv[0];
        os = String::copy(state, v.name());
        return Value::fromObject(os);
    }

    Value objfn_object_member_getclass(State* state, CallState* args)
    {
        auto klass = state->vmGetClassFor(args->thisval);
        return Value::fromObject(klass);
    }

    Value objfn_object_member_isstring(State* state, CallState* args)
    {
        Value v;
        (void)state;
        v = args->thisval;
        return Value::makeBool(v.isString());
    }

    Value objfn_object_member_isarray(State* state, CallState* args)
    {
        Value v;
        (void)state;
        v = args->thisval;
        return Value::makeBool(v.isArray());
    }

    Value objfn_object_member_iscallable(State* state, CallState* args)
    {
        Value selfval;
        (void)state;
        selfval = args->thisval;
        return Value::makeBool(
            selfval.isClass() ||
            selfval.isFuncScript() ||
            selfval.isFuncClosure() ||
            selfval.isFuncBound() ||
            selfval.isFuncNative()
        );
    }

    Value objfn_object_member_isbool(State* state, CallState* args)
    {
        Value selfval;
        (void)state;
        selfval = args->thisval;
        return Value::makeBool(selfval.isBool());
    }

    Value objfn_object_member_isnumber(State* state, CallState* args)
    {
        Value selfval;
        (void)state;
        selfval = args->thisval;
        return Value::makeBool(selfval.isNumber());
    }

    Value objfn_object_member_isint(State* state, CallState* args)
    {
        Value selfval;
        (void)state;
        selfval = args->thisval;
        return Value::makeBool(selfval.isNumber() && (((int)selfval.asNumber()) == selfval.asNumber()));
    }

    Value objfn_object_member_isdict(State* state, CallState* args)
    {
        Value selfval;
        (void)state;
        selfval = args->thisval;
        return Value::makeBool(selfval.isDict());
    }

    Value objfn_object_member_isobject(State* state, CallState* args)
    {
        Value selfval;
        (void)state;
        selfval = args->thisval;
        return Value::makeBool(selfval.isObject());
    }

    Value objfn_object_member_isfunction(State* state, CallState* args)
    {
        Value selfval;
        (void)state;
        selfval = args->thisval;
        return Value::makeBool(
            selfval.isFuncScript() ||
            selfval.isFuncClosure() ||
            selfval.isFuncBound() ||
            selfval.isFuncNative()
        );
    }

    Value objfn_object_member_isiterable(State* state, CallState* args)
    {
        bool isiterable;
        ClassObject* klass;
        Value selfval;
        (void)state;
        selfval = args->thisval;
        isiterable = selfval.isArray() || selfval.isDict() || selfval.isString();
        if(!isiterable && selfval.isInstance())
        {
            klass = selfval.asInstance()->m_fromclass;
            isiterable = (
                (klass->m_classmethods->getField(Value::fromObject(String::copy(state, "@iter"))) != nullptr) &&
                (klass->m_classmethods->getField(Value::fromObject(String::copy(state, "@itern"))) != nullptr)
            );
        }
        return Value::makeBool(isiterable);
    }

    Value objfn_object_member_isclass(State* state, CallState* args)
    {
        Value selfval;
        (void)state;
        selfval = args->thisval;
        return Value::makeBool(selfval.isClass());
    }

    Value objfn_object_member_isfile(State* state, CallState* args)
    {
        Value selfval;
        (void)state;
        selfval = args->thisval;
        return Value::makeBool(selfval.isFile());
    }

    Value objfn_object_member_isinstance(State* state, CallState* args)
    {
        Value selfval;
        (void)state;
        selfval = args->thisval;
        return Value::makeBool(selfval.isInstance());
    }

    Value objfn_number_member_tobinstring(State* state, CallState* args)
    {
        return Value::fromObject(String::numToBinString(state, args->thisval.asNumber()));
    }

    Value objfn_number_member_tooctstring(State* state, CallState* args)
    {
        return Value::fromObject(String::numToOctString(state, args->thisval.asNumber(), false));
    }

    Value objfn_number_member_tohexstring(State* state, CallState* args)
    {
        return Value::fromObject(String::numToHexString(state, args->thisval.asNumber(), false));
    }

    Value objfn_math_static_abs(State* state, CallState* args)
    {
        (void)state;
        args->checkType(0, Value::VALTYPE_NUMBER);
        auto nv = args->at(0).asNumber();
        auto rt = fabs(nv);
        //fprintf(stderr, "nv=%g -->  %g\n", nv, rt);
        return Value::makeNumber(rt);
    }

    Value objfn_math_static_round(State* state, CallState* args)
    {
        (void)state;
        args->checkType(0, Value::VALTYPE_NUMBER);
        auto nv = args->at(0).asNumber();
        auto rt = round(nv);
        //fprintf(stderr, "nv=%g -->  %g\n", nv, rt);
        return Value::makeNumber(rt);
    }

    Value objfn_math_static_min(State* state, CallState* args)
    {
        (void)state;
        //args->checkType(0, Value::VALTYPE_NUMBER);
        //args->checkType(1, Value::VALTYPE_NUMBER);
        auto x = args->at(0).asNumber();
        auto y = args->at(1).asNumber();
        auto b = (x < y) ? x : y;
        return Value::makeNumber(b);
    }

    void nn_state_initbuiltinmethods(State* state)
    {
        {
            state->m_classprimprocess->setStaticPropertyCstr("env", Value::fromObject(state->m_envdict));
            
        }
        {
            state->m_classprimobject->defCallableField("class", objfn_object_member_getclass);
            state->m_classprimobject->defStaticNativeMethod("typename", objfn_object_static_typename);
            state->m_classprimobject->defNativeMethod("dump", objfn_object_member_dump);
            state->m_classprimobject->defNativeMethod("toString", objfn_object_member_tostring);
            state->m_classprimobject->defNativeMethod("isArray", objfn_object_member_isarray);        
            state->m_classprimobject->defNativeMethod("isString", objfn_object_member_isstring);
            state->m_classprimobject->defNativeMethod("isCallable", objfn_object_member_iscallable);
            state->m_classprimobject->defNativeMethod("isBool", objfn_object_member_isbool);
            state->m_classprimobject->defNativeMethod("isNumber", objfn_object_member_isnumber);
            state->m_classprimobject->defNativeMethod("isInt", objfn_object_member_isint);
            state->m_classprimobject->defNativeMethod("isDict", objfn_object_member_isdict);
            state->m_classprimobject->defNativeMethod("isObject", objfn_object_member_isobject);
            state->m_classprimobject->defNativeMethod("isFunction", objfn_object_member_isfunction);
            state->m_classprimobject->defNativeMethod("isIterable", objfn_object_member_isiterable);
            state->m_classprimobject->defNativeMethod("isClass", objfn_object_member_isclass);
            state->m_classprimobject->defNativeMethod("isFile", objfn_object_member_isfile);
            state->m_classprimobject->defNativeMethod("isInstance", objfn_object_member_isinstance);

        }
        
        {
            state->m_classprimnumber->defNativeMethod("toHexString", objfn_number_member_tohexstring);
            state->m_classprimnumber->defNativeMethod("toOctString", objfn_number_member_tooctstring);
            state->m_classprimnumber->defNativeMethod("toBinString", objfn_number_member_tobinstring);
        }
        {
            state->m_classprimstring->defNativeConstructor(objfn_string_member_constructor);
            state->m_classprimstring->defStaticNativeMethod("fromCharCode", objfn_string_static_fromcharcode);
            state->m_classprimstring->defStaticNativeMethod("utf8Decode", objfn_string_static_utf8decode);
            state->m_classprimstring->defStaticNativeMethod("utf8Encode", objfn_string_static_utf8encode);
            state->m_classprimstring->defStaticNativeMethod("utf8NumBytes", objfn_string_static_utf8numbytes);
            
            state->m_classprimstring->defCallableField("length", objfn_string_member_length);
            state->m_classprimstring->defNativeMethod("utf8Chars", objfn_string_member_utf8chars);
            state->m_classprimstring->defNativeMethod("utf8Codepoints", objfn_string_member_utf8codepoints);
            state->m_classprimstring->defNativeMethod("utf8Bytes", objfn_string_member_utf8codepoints);

            state->m_classprimstring->defNativeMethod("@iter", objfn_string_member_iter);
            state->m_classprimstring->defNativeMethod("@itern", objfn_string_member_itern);
            state->m_classprimstring->defNativeMethod("size", objfn_string_member_length);
            state->m_classprimstring->defNativeMethod("substr", objfn_string_member_substring);
            state->m_classprimstring->defNativeMethod("substring", objfn_string_member_substring);
            state->m_classprimstring->defNativeMethod("charCodeAt", objfn_string_member_charcodeat);
            state->m_classprimstring->defNativeMethod("charAt", objfn_string_member_charat);
            state->m_classprimstring->defNativeMethod("upper", objfn_string_member_upper);
            state->m_classprimstring->defNativeMethod("lower", objfn_string_member_lower);
            state->m_classprimstring->defNativeMethod("trim", objfn_string_member_trim);
            state->m_classprimstring->defNativeMethod("ltrim", objfn_string_member_ltrim);
            state->m_classprimstring->defNativeMethod("rtrim", objfn_string_member_rtrim);
            state->m_classprimstring->defNativeMethod("split", objfn_string_member_split);
            state->m_classprimstring->defNativeMethod("indexOf", objfn_string_member_indexof);
            state->m_classprimstring->defNativeMethod("count", objfn_string_member_count);
            state->m_classprimstring->defNativeMethod("toNumber", objfn_string_member_tonumber);
            state->m_classprimstring->defNativeMethod("toList", objfn_string_member_tolist);
            state->m_classprimstring->defNativeMethod("lpad", objfn_string_member_lpad);
            state->m_classprimstring->defNativeMethod("rpad", objfn_string_member_rpad);
            state->m_classprimstring->defNativeMethod("replace", objfn_string_member_replace);
            state->m_classprimstring->defNativeMethod("each", objfn_string_member_each);
            state->m_classprimstring->defNativeMethod("startswith", objfn_string_member_startswith);
            state->m_classprimstring->defNativeMethod("endswith", objfn_string_member_endswith);
            state->m_classprimstring->defNativeMethod("isAscii", objfn_string_member_isascii);
            state->m_classprimstring->defNativeMethod("isAlpha", objfn_string_member_isalpha);
            state->m_classprimstring->defNativeMethod("isAlnum", objfn_string_member_isalnum);
            state->m_classprimstring->defNativeMethod("isNumber", objfn_string_member_isnumber);
            state->m_classprimstring->defNativeMethod("isFloat", objfn_string_member_isfloat);
            state->m_classprimstring->defNativeMethod("isLower", objfn_string_member_islower);
            state->m_classprimstring->defNativeMethod("isUpper", objfn_string_member_isupper);
            state->m_classprimstring->defNativeMethod("isSpace", objfn_string_member_isspace);
            state->m_classprimstring->defNativeMethod("repeat", objfn_string_member_repeat);

            
        }
        {
            #if 1
            state->m_classprimarray->defNativeConstructor(objfn_array_member_constructor);
            #endif
            state->m_classprimarray->defCallableField("length", objfn_array_member_length);
            state->m_classprimarray->defNativeMethod("size", objfn_array_member_length);
            state->m_classprimarray->defNativeMethod("join", objfn_array_member_join);
            state->m_classprimarray->defNativeMethod("append", objfn_array_member_append);
            state->m_classprimarray->defNativeMethod("push", objfn_array_member_append);
            state->m_classprimarray->defNativeMethod("clear", objfn_array_member_clear);
            state->m_classprimarray->defNativeMethod("clone", objfn_array_member_clone);
            state->m_classprimarray->defNativeMethod("count", objfn_array_member_count);
            state->m_classprimarray->defNativeMethod("extend", objfn_array_member_extend);
            state->m_classprimarray->defNativeMethod("indexOf", objfn_array_member_indexof);
            state->m_classprimarray->defNativeMethod("insert", objfn_array_member_insert);
            state->m_classprimarray->defNativeMethod("pop", objfn_array_member_pop);
            state->m_classprimarray->defNativeMethod("shift", objfn_array_member_shift);
            state->m_classprimarray->defNativeMethod("removeAt", objfn_array_member_removeat);
            state->m_classprimarray->defNativeMethod("remove", objfn_array_member_remove);
            state->m_classprimarray->defNativeMethod("reverse", objfn_array_member_reverse);
            state->m_classprimarray->defNativeMethod("sort", objfn_array_member_sort);
            state->m_classprimarray->defNativeMethod("contains", objfn_array_member_contains);
            state->m_classprimarray->defNativeMethod("delete", objfn_array_member_delete);
            state->m_classprimarray->defNativeMethod("first", objfn_array_member_first);
            state->m_classprimarray->defNativeMethod("last", objfn_array_member_last);
            state->m_classprimarray->defNativeMethod("isEmpty", objfn_array_member_isempty);
            state->m_classprimarray->defNativeMethod("take", objfn_array_member_take);
            state->m_classprimarray->defNativeMethod("get", objfn_array_member_get);
            state->m_classprimarray->defNativeMethod("compact", objfn_array_member_compact);
            state->m_classprimarray->defNativeMethod("unique", objfn_array_member_unique);
            state->m_classprimarray->defNativeMethod("zip", objfn_array_member_zip);
            state->m_classprimarray->defNativeMethod("zipFrom", objfn_array_member_zipfrom);
            state->m_classprimarray->defNativeMethod("toDict", objfn_array_member_todict);
            state->m_classprimarray->defNativeMethod("each", objfn_array_member_each);
            state->m_classprimarray->defNativeMethod("map", objfn_array_member_map);
            state->m_classprimarray->defNativeMethod("filter", objfn_array_member_filter);
            state->m_classprimarray->defNativeMethod("some", objfn_array_member_some);
            state->m_classprimarray->defNativeMethod("every", objfn_array_member_every);
            state->m_classprimarray->defNativeMethod("reduce", objfn_array_member_reduce);
            state->m_classprimarray->defNativeMethod("@iter", objfn_array_member_iter);
            state->m_classprimarray->defNativeMethod("@itern", objfn_array_member_itern);
        }
        {
            #if 0
            state->m_classprimdict->defNativeConstructor(objfn_dict_member_constructor);
            #endif
            state->m_classprimdict->defNativeMethod("keys", objfn_dict_member_keys);
            state->m_classprimdict->defNativeMethod("size", objfn_dict_member_length);
            state->m_classprimdict->defNativeMethod("add", objfn_dict_member_add);
            state->m_classprimdict->defNativeMethod("set", objfn_dict_member_set);
            state->m_classprimdict->defNativeMethod("clear", objfn_dict_member_clear);
            state->m_classprimdict->defNativeMethod("clone", objfn_dict_member_clone);
            state->m_classprimdict->defNativeMethod("compact", objfn_dict_member_compact);
            state->m_classprimdict->defNativeMethod("contains", objfn_dict_member_contains);
            state->m_classprimdict->defNativeMethod("extend", objfn_dict_member_extend);
            state->m_classprimdict->defNativeMethod("get", objfn_dict_member_get);
            state->m_classprimdict->defNativeMethod("values", objfn_dict_member_values);
            state->m_classprimdict->defNativeMethod("remove", objfn_dict_member_remove);
            state->m_classprimdict->defNativeMethod("isEmpty", objfn_dict_member_isempty);
            state->m_classprimdict->defNativeMethod("findKey", objfn_dict_member_findkey);
            state->m_classprimdict->defNativeMethod("toList", objfn_dict_member_tolist);
            state->m_classprimdict->defNativeMethod("each", objfn_dict_member_each);
            state->m_classprimdict->defNativeMethod("filter", objfn_dict_member_filter);
            state->m_classprimdict->defNativeMethod("some", objfn_dict_member_some);
            state->m_classprimdict->defNativeMethod("every", objfn_dict_member_every);
            state->m_classprimdict->defNativeMethod("reduce", objfn_dict_member_reduce);
            state->m_classprimdict->defNativeMethod("@iter", objfn_dict_member_iter);
            state->m_classprimdict->defNativeMethod("@itern", objfn_dict_member_itern);
        }
        {
            state->m_classprimfile->defNativeConstructor(objfn_file_member_constructor);
            state->m_classprimfile->defStaticNativeMethod("exists", objfn_file_static_exists);
            state->m_classprimfile->defStaticNativeMethod("isFile", objfn_file_static_isfile);
            state->m_classprimfile->defStaticNativeMethod("isDirectory", objfn_file_static_isdirectory);
            state->m_classprimfile->defStaticNativeMethod("read", objfn_file_static_read);
            state->m_classprimfile->defNativeMethod("close", objfn_file_member_close);
            state->m_classprimfile->defNativeMethod("open", objfn_file_member_open);
            state->m_classprimfile->defNativeMethod("read", objfn_file_member_read);
            state->m_classprimfile->defNativeMethod("get", objfn_file_member_get);
            state->m_classprimfile->defNativeMethod("gets", objfn_file_member_gets);
            state->m_classprimfile->defNativeMethod("write", objfn_file_member_write);
            state->m_classprimfile->defNativeMethod("puts", objfn_file_member_puts);
            state->m_classprimfile->defNativeMethod("printf", objfn_file_member_printf);
            state->m_classprimfile->defNativeMethod("number", objfn_file_member_number);
            state->m_classprimfile->defNativeMethod("isTTY", objfn_file_member_istty);
            state->m_classprimfile->defNativeMethod("isOpen", objfn_file_member_isopen);
            state->m_classprimfile->defNativeMethod("isClosed", objfn_file_member_isclosed);
            state->m_classprimfile->defNativeMethod("flush", objfn_file_member_flush);
            state->m_classprimfile->defNativeMethod("stats", objfn_file_member_stats);
            state->m_classprimfile->defNativeMethod("path", objfn_file_member_path);
            state->m_classprimfile->defNativeMethod("seek", objfn_file_member_seek);
            state->m_classprimfile->defNativeMethod("tell", objfn_file_member_tell);
            state->m_classprimfile->defNativeMethod("mode", objfn_file_member_mode);
            state->m_classprimfile->defNativeMethod("name", objfn_file_member_name);
        }
        {
            state->m_classprimrange->defNativeConstructor(objfn_range_member_constructor);
            state->m_classprimrange->defNativeMethod("lower", objfn_range_member_lower);
            state->m_classprimrange->defNativeMethod("upper", objfn_range_member_upper);
            state->m_classprimrange->defNativeMethod("range", objfn_range_member_range);
            state->m_classprimrange->defNativeMethod("loop", objfn_range_member_loop);
            state->m_classprimrange->defNativeMethod("expand", objfn_range_member_expand);
            state->m_classprimrange->defNativeMethod("toArray", objfn_range_member_expand);
            state->m_classprimrange->defNativeMethod("@iter", objfn_range_member_iter);
            state->m_classprimrange->defNativeMethod("@itern", objfn_range_member_itern);
        }
        {
            state->m_classprimmath->defStaticNativeMethod("abs", objfn_math_static_abs);
            state->m_classprimmath->defStaticNativeMethod("round", objfn_math_static_round);
            state->m_classprimmath->defStaticNativeMethod("min", objfn_math_static_min);
        }
        {
            
        }
    }

    Value nn_nativefn_time(State* state, CallState* args)
    {
        struct timeval tv;
        (void)args;
        (void)state;
        args->checkCount(0);
        osfn_gettimeofday(&tv, nullptr);
        return Value::makeNumber((double)tv.tv_sec + ((double)tv.tv_usec / 10000000));
    }

    Value nn_nativefn_microtime(State* state, CallState* args)
    {
        struct timeval tv;
        (void)args;
        (void)state;
        args->checkCount(0);
        osfn_gettimeofday(&tv, nullptr);
        return Value::makeNumber((1000000 * (double)tv.tv_sec) + ((double)tv.tv_usec / 10));
    }

    Value nn_nativefn_id(State* state, CallState* args)
    {
        long* lptr;
        Value val;
        (void)state;
        args->checkCount(1);
        val = args->argv[0];
        lptr = reinterpret_cast<long*>(&val);
        return Value::makeNumber(*lptr);
    }

    Value nn_nativefn_int(State* state, CallState* args)
    {
        (void)state;
        NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
        if(args->argc == 0)
        {
            return Value::makeNumber(0);
        }
        args->checkByFunc(0, &Value::isNumber);
        return Value::makeNumber((double)((int)args->argv[0].asNumber()));
    }

    Value nn_nativefn_chr(State* state, CallState* args)
    {
        size_t len;
        char* string;
        args->checkCount(1);
        args->checkByFunc(0, &Value::isNumber);
        string = Util::utf8Encode(state, (int)args->argv[0].asNumber(), &len);
        return Value::fromObject(String::take(state, string));
    }

    Value nn_nativefn_ord(State* state, CallState* args)
    {
        int ord;
        int length;
        String* string;
        if(!args->checkCount(1))
        {
            return Value::makeEmpty();
        }
        if(!args->checkByFunc(0, &Value::isString))
        {
            return Value::makeEmpty();
        }
        string = args->argv[0].asString();
        length = string->length();
        if(length > 1)
        {
            return state->m_vmstate->raiseFromFunction(args, "ord() expects character as argument, string given");
        }
        ord = (int)string->data()[0];
        if(ord < 0)
        {
            ord += 256;
        }
        return Value::makeNumber(ord);
    }

    Value nn_nativefn_rand(State* state, CallState* args)
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
            args->checkByFunc(0, &Value::isNumber);
            lowerlimit = args->argv[0].asNumber();
        }
        if(args->argc == 2)
        {
            args->checkByFunc(1, &Value::isNumber);
            upperlimit = args->argv[1].asNumber();
        }
        if(lowerlimit > upperlimit)
        {
            tmp = upperlimit;
            upperlimit = lowerlimit;
            lowerlimit = tmp;
        }
        return Value::makeNumber(Util::MTRand(lowerlimit, upperlimit));
    }

    Value nn_nativefn_typeof(State* state, CallState* args)
    {
        const char* result;
        args->checkCount(1);
        result = args->argv[0].name();
        return Value::fromObject(String::copy(state, result));
    }

    Value nn_nativefn_eval(State* state, CallState* args)
    {
        Value result;
        String* os;
        args->checkCount(1);
        os = args->argv[0].asString();
        /*fprintf(stderr, "eval:src=%s\n", os->data());*/
        result = state->evalSource(os->data());
        return result;
    }

    /*
    Value nn_nativefn_loadfile(State* state, CallState* args)
    {
        Value result;
        String* os;
        args->checkCount(1);
        os = args->argv[0].asString();
        fprintf(stderr, "eval:src=%s\n", os->data());
        result = state->evalSource(os->data());
        return result;
    }
    */

    Value nn_nativefn_instanceof(State* state, CallState* args)
    {
        (void)state;
        args->checkCount(2);
        args->checkByFunc(0, &Value::isInstance);
        args->checkByFunc(1, &Value::isClass);
        return Value::makeBool(ClassObject::instanceOf(args->argv[0].asInstance()->m_fromclass, args->argv[1].asClass()));
    }

    Value nn_nativefn_sprintf(State* state, CallState* args)
    {
        String* res;
        String* ofmt;
        NEON_ARGS_CHECKMINARG(args, 1);
        args->checkByFunc(0, &Value::isString);
        ofmt = args->argv[0].asString();
        Printer pd(state);
        FormatInfo nfi(state, &pd, ofmt->cstr(), ofmt->length());
        if(!nfi.format(args->argc, 1, args->argv))
        {
            return Value::makeNull();
        }
        res = pd.takeString();
        return Value::fromObject(res);
    }

    Value nn_nativefn_printf(State* state, CallState* args)
    {
        String* ofmt;
        NEON_ARGS_CHECKMINARG(args, 1);
        args->checkByFunc(0, &Value::isString);
        ofmt = args->argv[0].asString();
        FormatInfo nfi(state, state->m_stdoutprinter, ofmt->cstr(), ofmt->length());
        if(!nfi.format(args->argc, 1, args->argv))
        {
        }
        return Value::makeNull();
    }

    Value nn_nativefn_print(State* state, CallState* args)
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
        return Value::makeNull();
    }

    Value nn_nativefn_println(State* state, CallState* args)
    {
        Value v;
        v = nn_nativefn_print(state, args);
        state->m_stdoutprinter->put("\n");
        return v;
    }

    Value nn_nativefn_raise(State* state, CallState* args)
    {
        const char* msg;
        const char* clname;
        msg = "raised by raise()";
        NEON_ARGS_CHECKMINARG(args, 1);
        args->checkByFunc(0, &Value::isString);
        if(args->argc > 1)
        {
            args->checkByFunc(1, &Value::isString);
            msg = args->argv[1].asString()->data();
        }
        clname = args->argv[0].asString()->data();
        state->m_vmstate->raiseClass(clname, msg);
        return Value::makeEmpty();
    }


    void nn_state_initbuiltinfunctions(State* state)
    {
        state->defNativeFunction("print", nn_nativefn_print);
        state->defNativeFunction("println", nn_nativefn_println);
        state->defNativeFunction("sprintf", nn_nativefn_sprintf);
        state->defNativeFunction("printf", nn_nativefn_printf);
        state->defNativeFunction("chr", nn_nativefn_chr);
        state->defNativeFunction("id", nn_nativefn_id);
        state->defNativeFunction("int", nn_nativefn_int);
        state->defNativeFunction("instanceof", nn_nativefn_instanceof);
        state->defNativeFunction("microtime", nn_nativefn_microtime);
        state->defNativeFunction("ord", nn_nativefn_ord);
        state->defNativeFunction("rand", nn_nativefn_rand);
        state->defNativeFunction("time", nn_nativefn_time);
        state->defNativeFunction("eval", nn_nativefn_eval);
        state->defNativeFunction("raise", nn_nativefn_raise);
    }


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

    void State::defNativeFunction(const char* name, NativeCallbackFN fptr)
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
        cl = ClassObject::make(this, Util::StrBuffer(name), parent);
        m_definedglobals->set(Value::fromObject(os), Value::fromObject(cl));
        return cl;
    }

    FuncClosure* State::compileSourceToClosure(const char* source, size_t len, const char* filename)
    {
        const char* rp;
        FuncClosure* closure;
        (void)len;
        m_rootphysfile = (char*)filename;
        rp = filename;
        m_toplevelmodule->m_physlocation = String::copy(this, rp);
        closure = compileSource(m_toplevelmodule, source);
        return closure;
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

    FuncClosure* State::readBinaryFile(const char* filename)
    {
        bool ok;
        FILE* fh;
        fh = fopen(filename, "rb");
        if(fh == nullptr)
        {
            return nullptr;
        }
        auto fs = FuncScript::make(this, m_toplevelmodule, FuncCommon::FUNCTYPE_FUNCTION);
        auto cls = FuncClosure::make(this, fs);
        ok = fs->m_compiledblob->fromHandle(fh, true);
        fclose(fh);
        if(!ok)
        {
            return nullptr;
        }
        return cls;
    }

    FuncClosure* State::compileSource(Module* module, const char* source)
    {
        FuncClosure* closure;
        module->setFileField();
        closure = compileScriptSource(module, false, source);
        return closure;
    }

    Status State::execFromClosure(FuncClosure* closure, Value* dest)
    {
        Status status;        
        if(m_conf.exitafterbytecode)
        {
            return Status::OK;
        }
        m_vmstate->callClosure(closure, Value::makeNull(), 0);
        status = m_vmstate->runVM(0, dest);
        return status;

    }

    Status State::execSource(Module* module, const char* source, Value* dest)
    {
        FuncClosure* closure;
        closure = compileSource(module, source);
        if(closure == nullptr)
        {
            return Status::FAIL_COMPILE;
        }
        return execFromClosure(closure, dest);
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
            m_vmstate->raiseClass(m_exceptions.stdexception, "eval() failed");
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
        m_vmstate = neon::Memory::create<neon::State::VM>(this);
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
            m_classprimnumber = makeNamedClass(Value::typenameFromEnum(Value::VALTYPE_NUMBER), m_classprimobject);
            m_classprimstring = makeNamedClass(Value::typenameFromEnum(Value::OBJTYPE_STRING), m_classprimobject);
            m_classprimarray = makeNamedClass(Value::typenameFromEnum(Value::OBJTYPE_ARRAY), m_classprimobject);
            m_classprimdict = makeNamedClass(Value::typenameFromEnum(Value::OBJTYPE_DICT), m_classprimobject);
            m_classprimfile = makeNamedClass(Value::typenameFromEnum(Value::OBJTYPE_FILE), m_classprimobject);
            m_classprimrange = makeNamedClass(Value::typenameFromEnum(Value::OBJTYPE_RANGE), m_classprimobject);
            m_classprimcallable = makeNamedClass("Function", m_classprimobject);
            m_classprimmath = makeNamedClass("Math", m_classprimobject);
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


    static NEON_FORCEINLINE ScopeUpvalue* vmutil_upvaluescapture(State::VM* vm, Value* local, int stackpos)
    {
        ScopeUpvalue* upvalue;
        ScopeUpvalue* prevupvalue;
        ScopeUpvalue* createdupvalue;
        prevupvalue = nullptr;
        upvalue = vm->m_openupvalues;
        while(upvalue != nullptr && (&upvalue->m_location) > local)
        {
            prevupvalue = upvalue;
            upvalue = upvalue->m_nextupval;
        }
        if(upvalue != nullptr && (&upvalue->m_location) == local)
        {
            return upvalue;
        }
        createdupvalue = ScopeUpvalue::make(vm->m_pvm, local, stackpos);
        createdupvalue->m_nextupval = upvalue;
        if(prevupvalue == nullptr)
        {
            vm->m_openupvalues = createdupvalue;
        }
        else
        {
            prevupvalue->m_nextupval = createdupvalue;
        }
        return createdupvalue;
    }

    static NEON_FORCEINLINE void vmutil_upvaluesclose(State::VM* vm, const Value* last)
    {
        ScopeUpvalue* upvalue;
        while(vm->m_openupvalues != nullptr && (&vm->m_openupvalues->m_location) >= last)
        {
            upvalue = vm->m_openupvalues;
            upvalue->m_closed = upvalue->m_location;
            upvalue->m_location = upvalue->m_closed;
            vm->m_openupvalues = upvalue->m_nextupval;
        }
    }

    static NEON_FORCEINLINE void vmutil_definemethod(State::VM* vm, String* name)
    {
        Value method;
        ClassObject* klass;
        FuncCommon::Type t;
        method = vm->stackPeek(0);
        t = FuncCommon::getMethodType(method);
        klass = vm->stackPeek(1).asClass();
        //fprintf(stderr, "method '%s' type: %d\n", name->data(), t);
        if(t == FuncCommon::FUNCTYPE_STATIC)
        {
            klass->m_staticmethods->set(Value::fromObject(name), method);
        }
        else
        {
            klass->m_classmethods->set(Value::fromObject(name), method);
        }
        if(t == FuncCommon::FUNCTYPE_INITIALIZER)
        {
            klass->m_constructor = method;
        }
        vm->stackPop();
    }

    static NEON_FORCEINLINE void vmutil_defineproperty(State::VM* vm, String* name, bool isstatic)
    {
        Value property;
        ClassObject* klass;
        property = vm->stackPeek(0);
        klass = vm->stackPeek(1).asClass();
        if(!isstatic)
        {
            klass->defProperty(name->data(), property);
        }
        else
        {
            klass->setStaticProperty(name, property);
        }
        vm->stackPop();
    }

    static NEON_FORCEINLINE String* vmutil_multiplystring(State::VM* vm, String* str, double number)
    {
        int i;
        int times;
        times = (int)number;
        /* 'str' * 0 == '', 'str' * -1 == '' */
        if(times <= 0)
        {
            return String::copy(vm->m_pvm, "", 0);
        }
        /* 'str' * 1 == 'str' */
        else if(times == 1)
        {
            return str;
        }
        Printer pd(vm->m_pvm);
        for(i = 0; i < times; i++)
        {
            pd.put(str->data(), str->length());
        }
        return pd.takeString();
    }

    static NEON_FORCEINLINE Array* vmutil_combinearrays(State::VM* vm, Array* a, Array* b)
    {
        size_t i;
        Array* list;
        list = Array::make(vm->m_pvm);
        vm->stackPush(Value::fromObject(list));
        for(i = 0; i < a->count(); i++)
        {
            list->push(a->at(i));
        }
        for(i = 0; i < b->count(); i++)
        {
            list->push(b->at(i));
        }
        vm->stackPop();
        return list;
    }

    static NEON_FORCEINLINE void vmutil_multiplyarray(State::VM* vm, Array* from, Array* newlist, size_t times)
    {
        size_t i;
        size_t j;
        (void)vm;
        for(i = 0; i < times; i++)
        {
            for(j = 0; j < from->count(); j++)
            {
                newlist->push(from->at(j));
            }
        }
    }

    static NEON_FORCEINLINE bool vmutil_dogetrangedindexofarray(State::VM* vm, Array* list, bool willassign)
    {
        int i;
        int idxlower;
        int idxupper;
        Value valupper;
        Value vallower;
        Array* newlist;
        valupper = vm->stackPeek(0);
        vallower = vm->stackPeek(1);
        if(!(vallower.isNull() || vallower.isNumber()) || !(valupper.isNumber() || valupper.isNull()))
        {
            vm->stackPop(2);
            return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "list range index expects upper and lower to be numbers, but got '%s', '%s'", vallower.name(), valupper.name());
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
                vm->stackPop(3);
            }
            vm->stackPush(Value::fromObject(Array::make(vm->m_pvm)));
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
        newlist = Array::make(vm->m_pvm);
        vm->stackPush(Value::fromObject(newlist));
        for(i = idxlower; i < idxupper; i++)
        {
            newlist->push(list->at(i));
        }
        /* clear gc protect */
        vm->stackPop();
        if(!willassign)
        {
            /* +1 for the list itself */
            vm->stackPop(3);
        }
        vm->stackPush(Value::fromObject(newlist));
        return true;
    }

    static NEON_FORCEINLINE bool vmutil_dogetrangedindexofstring(State::VM* vm, String* string, bool willassign)
    {
        int end;
        int start;
        int length;
        int idxupper;
        int idxlower;
        Value valupper;
        Value vallower;
        valupper = vm->stackPeek(0);
        vallower = vm->stackPeek(1);
        if(!(vallower.isNull() || vallower.isNumber()) || !(valupper.isNumber() || valupper.isNull()))
        {
            vm->stackPop(2);
            return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "string range index expects upper and lower to be numbers, but got '%s', '%s'", vallower.name(), valupper.name());
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
                vm->stackPop(3);
            }
            vm->stackPush(Value::fromObject(String::copy(vm->m_pvm, "", 0)));
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
            vm->stackPop(3);
        }
        vm->stackPush(Value::fromObject(String::copy(vm->m_pvm, string->data() + start, end - start)));
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_getrangedindex(State::VM* vm)
    {
        bool isgotten;
        uint8_t willassign;
        Value vfrom;
        willassign = vm->readByte();
        isgotten = true;
        vfrom = vm->stackPeek(2);
        if(vfrom.isObject())
        {
            switch(vfrom.asObject()->m_objtype)
            {
                case Value::OBJTYPE_STRING:
                {
                    if(!vmutil_dogetrangedindexofstring(vm, vfrom.asString(), willassign != 0u))
                    {
                        return false;
                    }
                    break;
                }
                case Value::OBJTYPE_ARRAY:
                {
                    if(!vmutil_dogetrangedindexofarray(vm, vfrom.asArray(), willassign != 0u))
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
            return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "cannot range index object of type %s", vfrom.name());
        }
        return true;
    }

    static NEON_FORCEINLINE bool vmutil_doindexgetdict(State::VM* vm, Dictionary* dict, bool willassign)
    {
        Value vindex;
        Property* field;
        vindex = vm->stackPeek(0);
        field = dict->getEntry(vindex);
        if(field != nullptr)
        {
            if(!willassign)
            {
                /* we can safely get rid of the index from the stack */
                vm->stackPop(2);
            }
            vm->stackPush(field->m_actualval);
            return true;
        }
        vm->stackPop(1);
        vm->stackPush(Value::makeEmpty());
        return true;
    }

    static NEON_FORCEINLINE bool vmutil_doindexgetmodule(State::VM* vm, Module* module, bool willassign)
    {
        Value vindex;
        vindex = vm->stackPeek(0);
        auto field = module->m_deftable->getField(vindex);
        if(field != nullptr)
        {
            if(!willassign)
            {
                /* we can safely get rid of the index from the stack */
                vm->stackPop(2);
            }
            vm->stackPush(field->value());
            return true;
        }
        vm->stackPop();
        return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "%s is undefined in module %s", Value::toString(vm->m_pvm, vindex)->data(), module->m_modname);
    }

    static NEON_FORCEINLINE bool vmutil_doindexgetstring(State::VM* vm, String* string, bool willassign)
    {
        int end;
        int start;
        int index;
        int maxlength;
        int realindex;
        Value vindex;
        Range* rng;
        (void)realindex;
        vindex = vm->stackPeek(0);
        if(!vindex.isNumber())
        {
            if(vindex.isRange())
            {
                rng = vindex.asRange();
                vm->stackPop();
                vm->stackPush(Value::makeNumber(rng->m_lower));
                vm->stackPush(Value::makeNumber(rng->m_upper));
                return vmutil_dogetrangedindexofstring(vm, string, willassign);
            }
            vm->stackPop(1);
            return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "strings are numerically indexed");
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
                vm->stackPop(2);
            }
            vm->stackPush(Value::fromObject(String::copy(vm->m_pvm, string->data() + start, end - start)));
            return true;
        }
        vm->stackPop(1);
        vm->stackPush(Value::makeEmpty());
        return true;
    }

    static NEON_FORCEINLINE bool vmutil_doindexgetarray(State::VM* vm, Array* list, bool willassign)
    {
        int index;
        Value finalval;
        Value vindex;
        Range* rng;
        vindex = vm->stackPeek(0);
        if(NEON_UNLIKELY(!vindex.isNumber()))
        {
            if(vindex.isRange())
            {
                rng = vindex.asRange();
                vm->stackPop();
                vm->stackPush(Value::makeNumber(rng->m_lower));
                vm->stackPush(Value::makeNumber(rng->m_upper));
                return vmutil_dogetrangedindexofarray(vm, list, willassign);
            }
            vm->stackPop();
            return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "list are numerically indexed, but index type is '%s'", vindex.name());
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
            finalval = Value::makeNull();
        }
        if(!willassign)
        {
            /*
            // we can safely get rid of the index from the stack
            // +1 for the list itself
            */
            vm->stackPop(2);
        }
        vm->stackPush(finalval);
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_indexget(State::VM* vm)
    {
        bool isgotten;
        uint8_t willassign;
        Value peeked;
        willassign = vm->readByte();
        isgotten = true;
        peeked = vm->stackPeek(1);
        if(NEON_LIKELY(peeked.isObject()))
        {
            switch(peeked.asObject()->m_objtype)
            {
                case Value::OBJTYPE_STRING:
                {
                    if(!vmutil_doindexgetstring(vm, peeked.asString(), willassign != 0u))
                    {
                        return false;
                    }
                    break;
                }
                case Value::OBJTYPE_ARRAY:
                {
                    if(!vmutil_doindexgetarray(vm, peeked.asArray(), willassign != 0u))
                    {
                        return false;
                    }
                    break;
                }
                case Value::OBJTYPE_DICT:
                {
                    if(!vmutil_doindexgetdict(vm, peeked.asDict(), willassign != 0u))
                    {
                        return false;
                    }
                    break;
                }
                case Value::OBJTYPE_MODULE:
                {
                    if(!vmutil_doindexgetmodule(vm, peeked.asModule(), willassign != 0u))
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
            vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "cannot index object of type %s", peeked.name());
        }
        return true;
    }

    static NEON_FORCEINLINE bool vmutil_dosetindexdict(State::VM* vm, Dictionary* dict, Value index, Value value)
    {
        dict->setEntry(index, value);
        /* pop the value, index and dict out */
        vm->stackPop(3);
        /*
        // leave the value on the stack for consumption
        // e.g. variable = dict[index] = 10
        */
        vm->stackPush(value);
        return true;
    }

    static NEON_FORCEINLINE bool vmutil_dosetindexmodule(State::VM* vm, Module* module, Value index, Value value)
    {
        module->m_deftable->set(index, value);
        /* pop the value, index and dict out */
        vm->stackPop(3);
        /*
        // leave the value on the stack for consumption
        // e.g. variable = dict[index] = 10
        */
        vm->stackPush(value);
        return true;
    }

    static NEON_FORCEINLINE bool vmutil_doindexsetarray(State::VM* vm, Array* list, Value index, Value value)
    {
        int tmp;
        int rawpos;
        int position;
        int ocnt;
        int ocap;
        int vasz;
        if(NEON_UNLIKELY(!index.isNumber()))
        {
            vm->stackPop(3);
            /* pop the value, index and list out */
            return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "list are numerically indexed");
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
                    list->push(Value::makeEmpty());
                }
                else
                {
                    tmp = position + 1;
                    while(tmp > vasz)
                    {
                        list->push(Value::makeEmpty());
                        tmp--;
                    }
                }
            }
            fprintf(stderr, "setting value at position %zd (array count: %zd)\n", size_t(position), size_t(list->count()));
        }
        list->m_varray->m_values[position] = value;
        /* pop the value, index and list out */
        vm->stackPop(3);
        /*
        // leave the value on the stack for consumption
        // e.g. variable = list[index] = 10    
        */
        vm->stackPush(value);
        return true;
        /*
        // pop the value, index and list out
        //vm->stackPop(3);
        //return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "lists index %d out of range", rawpos);
        //vm->stackPush(Value::makeEmpty());
        //return true;
        */
    }

    static NEON_FORCEINLINE bool vmutil_dosetindexstring(State::VM* vm, String* os, Value index, Value value)
    {
        String* instr;
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
            vm->stackPop(3);
            /* pop the value, index and list out */
            return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "strings are numerically indexed");
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
            return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "cannot set string with type '%s'", value.name());
        }
        if(position < oslen && position > -oslen)
        {
            if(isstring)
            {
                if(instr->length() > 1)
                {
                    return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "expected a single-character string", value.name());
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
                return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "string prepending by set-assignment not supported yet");
            }
            /* pop the value, index and list out */
            vm->stackPop(3);
            /*
            // leave the value on the stack for consumption
            // e.g. variable = list[index] = 10
            */
            vm->stackPush(value);
            return true;
        }
        else
        {
            os->append(inchar);
            vm->stackPop(3);
            vm->stackPush(value);
        }
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_indexset(State::VM* vm)
    {
        bool isset;
        Value value;
        Value index;
        Value target;
        isset = true;
        target = vm->stackPeek(2);
        if(NEON_LIKELY(target.isObject()))
        {
            value = vm->stackPeek(0);
            index = vm->stackPeek(1);
            if(NEON_UNLIKELY(value.isEmpty()))
            {
                return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "empty cannot be assigned");
            }
            switch(target.asObject()->m_objtype)
            {
                case Value::OBJTYPE_ARRAY:
                    {
                        if(!vmutil_doindexsetarray(vm, target.asArray(), index, value))
                        {
                            return false;
                        }
                    }
                    break;
                case Value::OBJTYPE_STRING:
                    {
                        if(!vmutil_dosetindexstring(vm, target.asString(), index, value))
                        {
                            return false;
                        }
                    }
                    break;
                case Value::OBJTYPE_DICT:
                    {
                        return vmutil_dosetindexdict(vm, target.asDict(), index, value);
                    }
                    break;
                case Value::OBJTYPE_MODULE:
                    {
                        return vmutil_dosetindexmodule(vm, target.asModule(), index, value);
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
            return vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "type of %s is not a valid iterable", vm->stackPeek(3).name());
        }
        return true;
    }

    static NEON_FORCEINLINE bool vmutil_concatenate(State::VM* vm, bool ontoself)
    {
        Value vleft;
        Value vright;
        String* result;
        (void)ontoself;
        vright = vm->stackPeek(0);
        vleft = vm->stackPeek(1);
        Printer pd(vm->m_pvm);
        pd.printValue(vleft, false, true);
        pd.printValue(vright, false, true);
        result = pd.takeString();
        vm->stackPop(2);
        vm->stackPush(Value::fromObject(result));
        return true;
    }

    static NEON_FORCEINLINE int vmutil_floordiv(double a, double b)
    {
        int d;
        d = (int(a) / int(b));
        return (d - (int(((d * b) == a)) & ((a < 0) ^ (b < 0))));
    }

    static NEON_FORCEINLINE double vmutil_modulo(double a, double b)
    {
        double r;
        r = fmod(a, b);
        if(r != 0 && ((r < 0) != (b < 0)))
        {
            r += b;
        }
        return r;
    }

    static NEON_FORCEINLINE Property* vmutil_getproperty(State::VM* vm, Value peeked, String* name)
    {
        Property* field;
        Object* pobj;
        pobj = peeked.asObject();
        switch(pobj->m_objtype)
        {
            case Value::OBJTYPE_MODULE:
                {
                    Module* module;
                    module = peeked.asModule();
                    field = module->m_deftable->getByObjString(name);
                    if(field != nullptr)
                    {
                        return field;
                    }
                    goto failprop;
                }
                break;
            case Value::OBJTYPE_CLASS:
                {
                    ClassObject* klass;
                    klass = peeked.asClass();
                    field = klass->m_classmethods->getByObjString(name);
                    if(field != nullptr)
                    {
                        if(FuncCommon::getMethodType(field->m_actualval) == FuncCommon::FUNCTYPE_STATIC)
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
                    goto failprop;
                }
                break;
            case Value::OBJTYPE_INSTANCE:
                {
                    ClassInstance* instance;
                    instance = peeked.asInstance();
                    field = instance->m_properties->getByObjString(name);
                    if(field != nullptr)
                    {
                        return field;
                    }
                    if(vm->vmBindMethod(instance->m_fromclass, name))
                    {
                        return field;
                    }
                    goto failprop;
                }
                break;
            case Value::OBJTYPE_STRING:
                {
                    field = vm->m_pvm->m_classprimstring->getPropertyField(name);
                    if(field != nullptr)
                    {
                        return field;
                    }
                    goto failprop;
                }
                break;
            case Value::OBJTYPE_ARRAY:
                {
                    field = vm->m_pvm->m_classprimarray->getPropertyField(name);
                    if(field != nullptr)
                    {
                        return field;
                    }
                    goto failprop;
                }
                break;
            case Value::OBJTYPE_RANGE:
                {
                    field = vm->m_pvm->m_classprimrange->getPropertyField(name);
                    if(field != nullptr)
                    {
                        return field;
                    }
                    goto failprop;
                }
                break;
            case Value::OBJTYPE_DICT:
                {
                    field = peeked.asDict()->m_valtable->getByObjString(name);
                    if(field == nullptr)
                    {
                        field = vm->m_pvm->m_classprimdict->getPropertyField(name);
                    }
                    if(field != nullptr)
                    {
                        return field;
                    }
                    goto failprop;
                }
                break;
            case Value::OBJTYPE_FILE:
                {
                    field = vm->m_pvm->m_classprimfile->getPropertyField(name);
                    if(field != nullptr)
                    {
                        return field;
                    }
                    goto failprop;
                }
                break;
            default:
                {
                }
                break;
        }
        failprop:
        // does Object have this property?
        field = vm->m_pvm->m_classprimobject->getPropertyField(name);
        if(field != nullptr)
        {
            return field;
        }
        vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "object of type '%s' does not have a property named '%s'", peeked.name(), name->data());
        return nullptr;
    }

    static NEON_FORCEINLINE bool vmdo_propertyget(State::VM* vm)
    {
        Value peeked;
        Property* field;
        String* name;
        name = vm->readString();
        peeked = vm->stackPeek(0);
        if(peeked.isObject())
        {
            field = vmutil_getproperty(vm, peeked, name);
            if(field == nullptr)
            {
                return false;
            }
            else
            {
                if(field->m_proptype == Property::PROPTYPE_FUNCTION)
                {
                    vm->vmCallBoundValue(field->m_actualval, peeked, 0);
                }
                else
                {
                    vm->stackPop();
                    vm->stackPush(field->m_actualval);
                }
            }
            return true;
        }
        else
        {
            vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "'%s' of type %s does not have properties", Value::toString(vm->m_pvm, peeked)->data(),
                peeked.name());
        }
        return false;
    }

    static NEON_FORCEINLINE bool vmdo_propertygetself(State::VM* vm)
    {
        Value peeked;
        String* name;
        ClassObject* klass;
        ClassInstance* instance;
        Module* module;
        Property* field;
        name = vm->readString();
        peeked = vm->stackPeek(0);
        if(peeked.isInstance())
        {
            instance = peeked.asInstance();
            field = instance->m_properties->getByObjString(name);
            if(field != nullptr)
            {
                /* pop the instance... */
                vm->stackPop();
                vm->stackPush(field->m_actualval);
                return true;
            }
            if(vm->vmBindMethod(instance->m_fromclass, name))
            {
                return true;
            }
            NEON_VMMAC_TRYRAISE(vm->m_pvm, false, "instance of class %s does not have a property or method named '%s'",
                peeked.asInstance()->m_fromclass->m_classname.data(), name->data());
            return false;
        }
        else if(peeked.isClass())
        {
            klass = peeked.asClass();
            field = klass->m_classmethods->getByObjString(name);
            if(field != nullptr)
            {
                if(FuncCommon::getMethodType(field->m_actualval) == FuncCommon::FUNCTYPE_STATIC)
                {
                    /* pop the class... */
                    vm->stackPop();
                    vm->stackPush(field->m_actualval);
                    return true;
                }
            }
            else
            {
                field = klass->getStaticProperty(name);
                if(field != nullptr)
                {
                    /* pop the class... */
                    vm->stackPop();
                    vm->stackPush(field->m_actualval);
                    return true;
                }
            }
            NEON_VMMAC_TRYRAISE(vm->m_pvm, false, "cannot get method '%s' from instance of class '%s'", klass->m_classname.data(), name->data());
            return false;
        }
        else if(peeked.isModule())
        {
            module = peeked.asModule();
            field = module->m_deftable->getByObjString(name);
            if(field != nullptr)
            {
                /* pop the module... */
                vm->stackPop();
                vm->stackPush(field->m_actualval);
                return true;
            }
            NEON_VMMAC_TRYRAISE(vm->m_pvm, false, "module %s does not define '%s'", module->m_modname, name->data());
            return false;
        }
        NEON_VMMAC_TRYRAISE(vm->m_pvm, false, "'%s' of type %s does not have properties", Value::toString(vm->m_pvm, peeked)->data(),
            peeked.name());
        return false;
    }

    static NEON_FORCEINLINE bool vmdo_propertyset(State::VM* vm)
    {
        Value value;
        Value vtarget;
        Value vpeek;
        ClassObject* klass;
        String* name;
        Dictionary* dict;
        ClassInstance* instance;
        vtarget = vm->stackPeek(1);
        if(!vtarget.isClass() && !vtarget.isInstance() && !vtarget.isDict())
        {
            vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "object of type %s cannot carry properties", vtarget.name());
            return false;
        }
        else if(vm->stackPeek(0).isEmpty())
        {
            vm->raiseClass(vm->m_pvm->m_exceptions.stdexception, "empty cannot be assigned");
            return false;
        }
        name = vm->readString();
        vpeek = vm->stackPeek(0);
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
            value = vm->stackPop();
            /* removing the class object */
            vm->stackPop();
            vm->stackPush(value);
        }
        else if(vtarget.isInstance())
        {
            instance = vtarget.asInstance();
            instance->defProperty(name->data(), vpeek);
            value = vm->stackPop();
            /* removing the instance object */
            vm->stackPop();
            vm->stackPush(value);
        }
        else
        {
            dict = vtarget.asDict();
            dict->setEntry(Value::fromObject(name), vpeek);
            value = vm->stackPop();
            /* removing the dictionary object */
            vm->stackPop();
            vm->stackPush(value);
        }
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_dobinary(State::VM* vm)
    {
        bool isfail;
        long ibinright;
        long ibinleft;
        uint32_t ubinright;
        uint32_t ubinleft;
        double dbinright;
        double dbinleft;
        Instruction::OpCode instruction;
        Value res;
        Value binvalleft;
        Value binvalright;
        instruction = (Instruction::OpCode)vm->m_currentinstr.code;
        binvalright = vm->stackPeek(0);
        binvalleft = vm->stackPeek(1);
        isfail = (
            (!binvalright.isNumber() && !binvalright.isBool() && !binvalright.isNull()) ||
            (!binvalleft.isNumber() && !binvalleft.isBool() && !binvalleft.isNull())
        );
        if(isfail)
        {
            NEON_VMMAC_TRYRAISE(vm->m_pvm, false, "unsupported operand %s for %s and %s", Instruction::opName(instruction), binvalleft.name(), binvalright.name());
            return false;
        }
        binvalright = vm->stackPop();
        binvalleft = vm->stackPop();
        res = Value::makeEmpty();
        switch(instruction)
        {
            case Instruction::OP_PRIMADD:
                {
                    dbinright = binvalright.coerceToNumber();
                    dbinleft = binvalleft.coerceToNumber();
                    res = Value::makeNumber(dbinleft + dbinright);
                }
                break;
            case Instruction::OP_PRIMSUBTRACT:
                {
                    dbinright = binvalright.coerceToNumber();
                    dbinleft = binvalleft.coerceToNumber();
                    res = Value::makeNumber(dbinleft - dbinright);
                }
                break;
            case Instruction::OP_PRIMDIVIDE:
                {
                    dbinright = binvalright.coerceToNumber();
                    dbinleft = binvalleft.coerceToNumber();
                    res = Value::makeNumber(dbinleft / dbinright);
                }
                break;
            case Instruction::OP_PRIMMULTIPLY:
                {
                    dbinright = binvalright.coerceToNumber();
                    dbinleft = binvalleft.coerceToNumber();
                    res = Value::makeNumber(dbinleft * dbinright);
                }
                break;
            case Instruction::OP_PRIMAND:
                {
                    ibinright = binvalright.coerceToInt();
                    ibinleft = binvalleft.coerceToInt();
                    res = Value::makeInt(ibinleft & ibinright);
                }
                break;
            case Instruction::OP_PRIMOR:
                {
                    ibinright = binvalright.coerceToInt();
                    ibinleft = binvalleft.coerceToInt();
                    res = Value::makeInt(ibinleft | ibinright);
                }
                break;
            case Instruction::OP_PRIMBITXOR:
                {
                    ibinright = binvalright.coerceToInt();
                    ibinleft = binvalleft.coerceToInt();
                    res = Value::makeInt(ibinleft ^ ibinright);
                }
                break;
            case Instruction::OP_PRIMSHIFTLEFT:
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
                    //res = Value::makeInt(ibinleft << ibinright);
                    res = Value::makeInt(ubinleft << ubinright);

                }
                break;
            case Instruction::OP_PRIMSHIFTRIGHT:
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
                    res = Value::makeInt(ubinleft >> ubinright);
                }
                break;
            case Instruction::OP_PRIMGREATER:
                {
                    dbinright = binvalright.coerceToNumber();
                    dbinleft = binvalleft.coerceToNumber();
                    res = Value::makeBool(dbinleft > dbinright);
                }
                break;
            case Instruction::OP_PRIMLESSTHAN:
                {
                    dbinright = binvalright.coerceToNumber();
                    dbinleft = binvalleft.coerceToNumber();
                    res = Value::makeBool(dbinleft < dbinright);
                }
                break;
            default:
                {
                    fprintf(stderr, "unhandled instruction %d (%s)!\n", instruction, Instruction::opName(instruction));
                    return false;
                }
                break;
        }
        vm->stackPush(res);
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_globaldefine(State::VM* vm)
    {
        Value val;
        String* name;
        HashTable* tab;
        name = vm->readString();
        val = vm->stackPeek(0);
        if(val.isEmpty())
        {
            NEON_VMMAC_TRYRAISE(vm->m_pvm, false, "empty cannot be assigned");
            return false;
        }
        tab = vm->m_currentframe->closure->m_scriptfunc->m_inmodule->m_deftable;
        tab->set(Value::fromObject(name), val);
        vm->stackPop();
        #if (defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE) || 0
        Helper::printTableTo(vm->m_pvm->m_debugprinter, vm->m_pvm->m_definedglobals, "globals");
        #endif
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_globalget(State::VM* vm)
    {
        String* name;
        HashTable* tab;
        Property* field;
        name = vm->readString();
        tab = vm->m_currentframe->closure->m_scriptfunc->m_inmodule->m_deftable;
        field = tab->getByObjString(name);
        if(field == nullptr)
        {
            field = vm->m_pvm->m_definedglobals->getByObjString(name);
            if(field == nullptr)
            {
                NEON_VMMAC_TRYRAISE(vm->m_pvm, false, "global name '%s' is not defined", name->data());
                return false;
            }
        }
        vm->stackPush(field->m_actualval);
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_globalset(State::VM* vm)
    {
        String* name;
        HashTable* table;
        if(vm->stackPeek(0).isEmpty())
        {
            NEON_VMMAC_TRYRAISE(vm->m_pvm, false, "empty cannot be assigned");
            return false;
        }
        name = vm->readString();
        table = vm->m_currentframe->closure->m_scriptfunc->m_inmodule->m_deftable;
        if(table->set(Value::fromObject(name), vm->stackPeek(0)))
        {
            if(vm->m_pvm->m_conf.enablestrictmode)
            {
                table->removeByKey(Value::fromObject(name));
                NEON_VMMAC_TRYRAISE(vm->m_pvm, false, "global name '%s' was not declared", name->data());
                return false;
            }
        }
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_localget(State::VM* vm)
    {
        size_t ssp;
        uint16_t slot;
        Value val;
        slot = vm->readShort();
        ssp = vm->m_currentframe->stackslotpos;
        val = vm->m_stackvalues[ssp + slot];
        vm->stackPush(val);
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_localset(State::VM* vm)
    {
        size_t ssp;
        uint16_t slot;
        Value peeked;
        slot = vm->readShort();
        peeked = vm->stackPeek(0);
        if(peeked.isEmpty())
        {
            NEON_VMMAC_TRYRAISE(vm->m_pvm, false, "empty cannot be assigned");
            return false;
        }
        ssp = vm->m_currentframe->stackslotpos;
        vm->m_stackvalues[ssp + slot] = peeked;
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_funcargget(State::VM* vm)
    {
        size_t ssp;
        uint16_t slot;
        Value val;
        slot = vm->readShort();
        ssp = vm->m_currentframe->stackslotpos;
        //fprintf(stderr, "FUNCARGGET: %s\n", vm->m_currentframe->closure->m_scriptfunc->m_scriptfnname->data());
        val = vm->m_stackvalues[ssp + slot];
        vm->stackPush(val);
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_funcargset(State::VM* vm)
    {
        size_t ssp;
        uint16_t slot;
        Value peeked;
        slot = vm->readShort();
        peeked = vm->stackPeek(0);
        if(peeked.isEmpty())
        {
            NEON_VMMAC_TRYRAISE(vm->m_pvm, false, "empty cannot be assigned");
            return false;
        }
        ssp = vm->m_currentframe->stackslotpos;
        vm->m_stackvalues[ssp + slot] = peeked;
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_makeclosure(State::VM* vm)
    {
        int i;
        int index;
        size_t ssp;
        uint8_t islocal;
        Value* upvals;
        FuncScript* function;
        FuncClosure* closure;
        function = vm->readConst().asFuncScript();
        closure = FuncClosure::make(vm->m_pvm, function);
        vm->stackPush(Value::fromObject(closure));
        for(i = 0; i < closure->m_upvalcount; i++)
        {
            islocal = vm->readByte();
            index = vm->readShort();
            if(islocal != 0u)
            {
                ssp = vm->m_currentframe->stackslotpos;
                upvals = &vm->m_stackvalues[ssp + index];
                closure->m_storedupvals[i] = vmutil_upvaluescapture(vm, upvals, index);

            }
            else
            {
                closure->m_storedupvals[i] = vm->m_currentframe->closure->m_storedupvals[index];
            }
        }
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_makearray(State::VM* vm)
    {
        int i;
        int count;
        Array* array;
        count = vm->readShort();
        array = Array::make(vm->m_pvm);
        vm->m_stackvalues[vm->m_stackidx + (-count - 1)] = Value::fromObject(array);
        for(i = count - 1; i >= 0; i--)
        {
            array->push(vm->stackPeek(i));
        }
        vm->stackPop(count);
        return true;
    }

    static NEON_FORCEINLINE bool vmdo_makedict(State::VM* vm)
    {
        int i;
        int count;
        int realcount;
        Value name;
        Value value;
        Dictionary* dict;
        /* 1 for key, 1 for value */
        realcount = vm->readShort();
        count = realcount * 2;
        dict = Dictionary::make(vm->m_pvm);
        vm->m_stackvalues[vm->m_stackidx + (-count - 1)] = Value::fromObject(dict);
        for(i = 0; i < count; i += 2)
        {
            name = vm->m_stackvalues[vm->m_stackidx + (-count + i)];
            if(!name.isString() && !name.isNumber() && !name.isBool())
            {
                NEON_VMMAC_TRYRAISE(vm->m_pvm, false, "dictionary key must be one of string, number or boolean");
                return false;
            }
            value = vm->m_stackvalues[vm->m_stackidx + (-count + i + 1)];
            dict->setEntry(name, value);
        }
        vm->stackPop(count);
        return true;
    }

    #define BINARY_MOD_OP(vm, type, op) \
        do \
        { \
            double dbinright; \
            double dbinleft; \
            Value binvalright; \
            Value binvalleft; \
            binvalright = stackPeek(0); \
            binvalleft = stackPeek(1);\
            if((!binvalright.isNumber() && !binvalright.isBool()) \
            || (!binvalleft.isNumber() && !binvalleft.isBool())) \
            { \
                NEON_VMMAC_TRYRAISE(vm->m_pvm, Status::FAIL_RUNTIME, "unsupported operand %s for %s and %s", #op, binvalleft.name(), binvalright.name()); \
                break; \
            } \
            binvalright = stackPop(); \
            dbinright = binvalright.isBool() ? (binvalright.asBool() ? 1 : 0) : binvalright.asNumber(); \
            binvalleft = stackPop(); \
            dbinleft = binvalleft.isBool() ? (binvalleft.asBool() ? 1 : 0) : binvalleft.asNumber(); \
            stackPush(type(op(dbinleft, dbinright))); \
        } while(false)

    
    Status State::VM::runVM(int exitframe, Value* rv)
    {
        int iterpos;
        int printpos;
        int ofs;
        /*
        * this variable is a NOP; it only exists to ensure that functions outside of the
        * switch tree are not calling NEON_VMMAC_EXITVM(), as its behavior could be undefined.
        */
        bool you_are_calling_exit_vm_outside_of_runvm;
        Value* dbgslot;
        Instruction currinstr;
        Terminal nc;
        you_are_calling_exit_vm_outside_of_runvm = false;
        m_currentframe = &m_framevalues[m_framecount - 1];
        DebugPrinter dp(m_pvm, m_pvm->m_debugprinter);
        for(;;)
        {
            /*
            // try...finally... (i.e. try without a catch but finally
            // whose try body raises an exception)
            // can cause us to go into an invalid mode where frame count == 0
            // to fix this, we need to exit with an appropriate mode here.
            */
            if(m_framecount == 0)
            {
                return Status::FAIL_RUNTIME;
            }
            if(m_pvm->m_conf.dumpinstructions)
            {
                ofs = (int)(m_currentframe->inscode - m_currentframe->closure->m_scriptfunc->m_compiledblob->m_instrucs);
                dp.printInstructionAt(m_currentframe->closure->m_scriptfunc->m_compiledblob, ofs, false);
                if(m_pvm->m_conf.dumpprintstack)
                {
                    fprintf(stderr, "stack (before)=[\n");
                    iterpos = 0;
                    for(dbgslot = m_stackvalues; dbgslot < &m_stackvalues[m_stackidx]; dbgslot++)
                    {
                        printpos = iterpos + 1;
                        iterpos++;
                        fprintf(stderr, "  [%s%d%s] ", nc.color('y'), printpos, nc.color('0'));
                        m_pvm->m_debugprinter->putformat("%s", nc.color('y'));
                        m_pvm->m_debugprinter->printValue(*dbgslot, true, false);
                        m_pvm->m_debugprinter->putformat("%s", nc.color('0'));
                        fprintf(stderr, "\n");
                    }
                    fprintf(stderr, "]\n");
                }
            }
            currinstr = readInstruction();
            m_currentinstr = currinstr;
            //fprintf(stderr, "now executing at line %d\n", m_currentinstr.srcline);
            switch(currinstr.code)
            {
                case Instruction::OP_RETURN:
                    {
                        size_t ssp;
                        Value result;
                        result = stackPop();
                        if(rv != nullptr)
                        {
                            *rv = result;
                        }
                        ssp = m_currentframe->stackslotpos;
                        vmutil_upvaluesclose(this, &m_stackvalues[ssp]);
                        m_framecount--;
                        if(m_framecount == 0)
                        {
                            stackPop();
                            return Status::OK;
                        }
                        ssp = m_currentframe->stackslotpos;
                        m_stackidx = ssp;
                        stackPush(result);
                        m_currentframe = &m_framevalues[m_framecount - 1];
                        if(m_framecount == (size_t)exitframe)
                        {
                            return Status::OK;
                        }
                    }
                    break;
                case Instruction::OP_PUSHCONSTANT:
                    {
                        Value constant;
                        constant = readConst();
                        stackPush(constant);
                    }
                    break;
                case Instruction::OP_PRIMADD:
                    {
                        Value valright;
                        Value valleft;
                        Value result;
                        valright = stackPeek(0);
                        valleft = stackPeek(1);
                        if(valright.isString() || valleft.isString())
                        {
                            if(!vmutil_concatenate(this, false))
                            {
                                NEON_VMMAC_TRYRAISE(m_pvm, Status::FAIL_RUNTIME, "unsupported operand + for %s and %s", valleft.name(), valright.name());
                                break;
                            }
                        }
                        else if(valleft.isArray() && valright.isArray())
                        {
                            result = Value::fromObject(vmutil_combinearrays(this, valleft.asArray(), valright.asArray()));
                            stackPop(2);
                            stackPush(result);
                        }
                        else
                        {
                            vmdo_dobinary(this);
                        }
                    }
                    break;
                case Instruction::OP_PRIMSUBTRACT:
                    {
                        vmdo_dobinary(this);
                    }
                    break;
                case Instruction::OP_PRIMMULTIPLY:
                    {
                        int intnum;
                        double dbnum;
                        Value peekleft;
                        Value peekright;
                        Value result;
                        String* string;
                        Array* list;
                        Array* newlist;
                        peekright = stackPeek(0);
                        peekleft = stackPeek(1);
                        if(peekleft.isString() && peekright.isNumber())
                        {
                            dbnum = peekright.asNumber();
                            string = stackPeek(1).asString();
                            result = Value::fromObject(vmutil_multiplystring(this, string, dbnum));
                            stackPop(2);
                            stackPush(result);
                            break;
                        }
                        else if(peekleft.isArray() && peekright.isNumber())
                        {
                            intnum = (int)peekright.asNumber();
                            stackPop();
                            list = peekleft.asArray();
                            newlist = Array::make(m_pvm);
                            stackPush(Value::fromObject(newlist));
                            vmutil_multiplyarray(this, list, newlist, intnum);
                            stackPop(2);
                            stackPush(Value::fromObject(newlist));
                            break;
                        }
                        vmdo_dobinary(this);
                    }
                    break;
                case Instruction::OP_PRIMDIVIDE:
                    {
                        vmdo_dobinary(this);
                    }
                    break;
                case Instruction::OP_PRIMMODULO:
                    {
                        BINARY_MOD_OP(this, Value::makeNumber, vmutil_modulo);
                    }
                    break;
                case Instruction::OP_PRIMPOW:
                    {
                        BINARY_MOD_OP(this, Value::makeNumber, pow);
                    }
                    break;
                case Instruction::OP_PRIMFLOORDIVIDE:
                    {
                        BINARY_MOD_OP(this, Value::makeNumber, vmutil_floordiv);
                    }
                    break;
                case Instruction::OP_PRIMNEGATE:
                    {
                        Value peeked;
                        peeked = stackPeek(0);
                        if(!peeked.isNumber())
                        {
                            NEON_VMMAC_TRYRAISE(m_pvm, Status::FAIL_RUNTIME, "operator - not defined for object of type %s", peeked.name());
                            break;
                        }
                        stackPush(Value::makeNumber(-stackPop().asNumber()));
                    }
                    break;
                case Instruction::OP_PRIMBITNOT:
                {
                    Value peeked;
                    peeked = stackPeek(0);
                    if(!peeked.isNumber())
                    {
                        NEON_VMMAC_TRYRAISE(m_pvm, Status::FAIL_RUNTIME, "operator ~ not defined for object of type %s", peeked.name());
                        break;
                    }
                    stackPush(Value::makeInt(~((int)stackPop().asNumber())));
                    break;
                }
                case Instruction::OP_PRIMAND:
                    {
                        vmdo_dobinary(this);
                    }
                    break;
                case Instruction::OP_PRIMOR:
                    {
                        vmdo_dobinary(this);
                    }
                    break;
                case Instruction::OP_PRIMBITXOR:
                    {
                        vmdo_dobinary(this);
                    }
                    break;
                case Instruction::OP_PRIMSHIFTLEFT:
                    {
                        vmdo_dobinary(this);
                    }
                    break;
                case Instruction::OP_PRIMSHIFTRIGHT:
                    {
                        vmdo_dobinary(this);
                    }
                    break;
                case Instruction::OP_PUSHONE:
                    {
                        stackPush(Value::makeNumber(1));
                    }
                    break;
                /* comparisons */
                case Instruction::OP_EQUAL:
                    {
                        Value a;
                        Value b;
                        b = stackPop();
                        a = stackPop();
                        stackPush(Value::makeBool(a.compare(b)));
                    }
                    break;
                case Instruction::OP_PRIMGREATER:
                    {
                        vmdo_dobinary(this);
                    }
                    break;
                case Instruction::OP_PRIMLESSTHAN:
                    {
                        vmdo_dobinary(this);
                    }
                    break;
                case Instruction::OP_PRIMNOT:
                    {
                        stackPush(Value::makeBool(Value::isFalse(stackPop())));
                    }
                    break;
                case Instruction::OP_PUSHNULL:
                    {
                        stackPush(Value::makeNull());
                    }
                    break;
                case Instruction::OP_PUSHEMPTY:
                    {
                        stackPush(Value::makeEmpty());
                    }
                    break;
                case Instruction::OP_PUSHTRUE:
                    {
                        stackPush(Value::makeBool(true));
                    }
                    break;
                case Instruction::OP_PUSHFALSE:
                    {
                        stackPush(Value::makeBool(false));
                    }
                    break;

                case Instruction::OP_JUMPNOW:
                    {
                        uint16_t offset;
                        offset = readShort();
                        m_currentframe->inscode += offset;
                    }
                    break;
                case Instruction::OP_JUMPIFFALSE:
                    {
                        uint16_t offset;
                        offset = readShort();
                        if(Value::isFalse(stackPeek(0)))
                        {
                            m_currentframe->inscode += offset;
                        }
                    }
                    break;
                case Instruction::OP_LOOP:
                    {
                        uint16_t offset;
                        offset = readShort();
                        m_currentframe->inscode -= offset;
                    }
                    break;
                case Instruction::OP_ECHO:
                    {
                        Value val;
                        val = stackPeek(0);
                        m_pvm->m_stdoutprinter->printValue(val, m_pvm->m_isreplmode, true);
                        if(!val.isEmpty())
                        {
                            m_pvm->m_stdoutprinter->put("\n");
                        }
                        stackPop();
                    }
                    break;
                case Instruction::OP_STRINGIFY:
                    {
                        Value peeked;
                        String* value;
                        peeked = stackPeek(0);
                        if(!peeked.isString() && !peeked.isNull())
                        {
                            value = Value::toString(m_pvm, stackPop());
                            if(value->length() != 0)
                            {
                                stackPush(Value::fromObject(value));
                            }
                            else
                            {
                                stackPush(Value::makeNull());
                            }
                        }
                    }
                    break;
                case Instruction::OP_DUPONE:
                    {
                        stackPush(stackPeek(0));
                    }
                    break;
                case Instruction::OP_POPONE:
                    {
                        stackPop();
                    }
                    break;
                case Instruction::OP_POPN:
                    {
                        stackPop(readShort());
                    }
                    break;
                case Instruction::OP_UPVALUECLOSE:
                    {
                        vmutil_upvaluesclose(this, &m_stackvalues[m_stackidx - 1]);
                        stackPop();
                    }
                    break;
                case Instruction::OP_GLOBALDEFINE:
                    {
                        if(!vmdo_globaldefine(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_GLOBALGET:
                    {
                        if(!vmdo_globalget(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_GLOBALSET:
                    {
                        if(!vmdo_globalset(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_LOCALGET:
                    {
                        if(!vmdo_localget(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_LOCALSET:
                    {
                        if(!vmdo_localset(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_FUNCARGGET:
                    {
                        if(!vmdo_funcargget(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_FUNCARGSET:
                    {
                        if(!vmdo_funcargset(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;

                case Instruction::OP_PROPERTYGET:
                    {
                        if(!vmdo_propertyget(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_PROPERTYSET:
                    {
                        if(!vmdo_propertyset(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_PROPERTYGETSELF:
                    {
                        if(!vmdo_propertygetself(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_MAKECLOSURE:
                    {
                        if(!vmdo_makeclosure(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_UPVALUEGET:
                    {
                        int index;
                        FuncClosure* closure;
                        index = readShort();
                        closure = m_currentframe->closure;
                        if(index < closure->m_upvalcount)
                        {
                            stackPush(closure->m_storedupvals[index]->m_location);
                        }
                        else
                        {
                            stackPush(Value::makeEmpty());
                        }
                    }
                    break;
                case Instruction::OP_UPVALUESET:
                    {
                        int index;
                        index = readShort();
                        if(stackPeek(0).isEmpty())
                        {
                            NEON_VMMAC_TRYRAISE(m_pvm, Status::FAIL_RUNTIME, "empty cannot be assigned");
                            break;
                        }
                        m_currentframe->closure->m_storedupvals[index]->m_location = stackPeek(0);
                    }
                    break;
                case Instruction::OP_CALLFUNCTION:
                    {
                        int argcount;
                        Value func;
                        argcount = readByte();
                        func = stackPeek(argcount);
                        if(!vmCallValue(func, Value::makeEmpty(), argcount))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                        m_currentframe = &m_framevalues[m_framecount - 1];
                    }
                    break;
                case Instruction::OP_CALLMETHOD:
                    {
                        int argcount;
                        String* method;
                        method = readString();
                        argcount = readByte();
                        if(!vmInvokeMethod(method, argcount))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                        m_currentframe = &m_framevalues[m_framecount - 1];
                    }
                    break;
                case Instruction::OP_CLASSINVOKETHIS:
                    {
                        int argcount;
                        String* method;
                        method = readString();
                        argcount = readByte();
                        if(!vmInvokeSelfMethod(method, argcount))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                        m_currentframe = &m_framevalues[m_framecount - 1];
                    }
                    break;
                case Instruction::OP_MAKECLASS:
                    {
                        bool haveval;
                        Value pushme;
                        String* name;
                        ClassObject* klass;
                        Property* field;
                        haveval = false;
                        name = readString();
                        field = m_currentframe->closure->m_scriptfunc->m_inmodule->m_deftable->getByObjString(name);
                        if(field != nullptr)
                        {
                            if(field->m_actualval.isClass())
                            {
                                haveval = true;
                                pushme = field->m_actualval;
                            }
                        }
                        field = m_pvm->m_definedglobals->getByObjString(name);
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
                            klass = ClassObject::make(m_pvm, Util::StrBuffer(name->data(), name->length()), nullptr);
                            pushme = Value::fromObject(klass);
                        }
                        stackPush(pushme);
                    }
                    break;
                case Instruction::OP_MAKEMETHOD:
                    {
                        String* name;
                        name = readString();
                        vmutil_definemethod(this, name);
                    }
                    break;
                case Instruction::OP_CLASSPROPERTYDEFINE:
                    {
                        int isstatic;
                        String* name;
                        name = readString();
                        isstatic = readByte();
                        vmutil_defineproperty(this, name, isstatic == 1);
                    }
                    break;
                case Instruction::OP_CLASSINHERIT:
                    {
                        ClassObject* superclass;
                        ClassObject* subclass;
                        if(!stackPeek(1).isClass())
                        {
                            NEON_VMMAC_TRYRAISE(m_pvm, Status::FAIL_RUNTIME, "cannot inherit from non-class object");
                            break;
                        }
                        superclass = stackPeek(1).asClass();
                        subclass = stackPeek(0).asClass();
                        subclass->inheritFrom(superclass);
                        /* pop the subclass */
                        stackPop();
                    }
                    break;
                case Instruction::OP_CLASSGETSUPER:
                    {
                        ClassObject* klass;
                        String* name;
                        name = readString();
                        klass = stackPeek(0).asClass();
                        if(!vmBindMethod(klass->m_superclass, name))
                        {
                            NEON_VMMAC_TRYRAISE(m_pvm, Status::FAIL_RUNTIME, "class %s does not define a function %s", klass->m_classname.data(), name->data());
                        }
                    }
                    break;
                case Instruction::OP_CLASSINVOKESUPER:
                    {
                        int argcount;
                        ClassObject* klass;
                        String* method;
                        method = readString();
                        argcount = readByte();
                        klass = stackPop().asClass();
                        if(!vmInvokeMethodFromClass(klass, method, argcount))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                        m_currentframe = &m_framevalues[m_framecount - 1];
                    }
                    break;
                case Instruction::OP_CLASSINVOKESUPERSELF:
                    {
                        int argcount;
                        ClassObject* klass;
                        argcount = readByte();
                        klass = stackPop().asClass();
                        if(!vmInvokeMethodFromClass(klass, m_pvm->m_constructorname, argcount))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                        m_currentframe = &m_framevalues[m_framecount - 1];
                    }
                    break;
                case Instruction::OP_MAKEARRAY:
                    {
                        if(!vmdo_makearray(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_MAKERANGE:
                    {
                        double lower;
                        double upper;
                        Value vupper;
                        Value vlower;
                        vupper = stackPeek(0);
                        vlower = stackPeek(1);
                        if(!vupper.isNumber() || !vlower.isNumber())
                        {
                            NEON_VMMAC_TRYRAISE(m_pvm, Status::FAIL_RUNTIME, "invalid range boundaries");
                            break;
                        }
                        lower = vlower.asNumber();
                        upper = vupper.asNumber();
                        stackPop(2);
                        stackPush(Value::fromObject(Range::make(m_pvm, lower, upper)));
                    }
                    break;
                case Instruction::OP_MAKEDICT:
                    {
                        if(!vmdo_makedict(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_INDEXGETRANGED:
                    {
                        if(!vmdo_getrangedindex(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_INDEXGET:
                    {
                        if(!vmdo_indexget(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_INDEXSET:
                    {
                        if(!vmdo_indexset(this))
                        {
                            NEON_VMMAC_EXITVM(m_pvm);
                        }
                    }
                    break;
                case Instruction::OP_IMPORTIMPORT:
                    {
                        Value res;
                        String* name;
                        Module* mod;
                        name = stackPeek(0).asString();
                        fprintf(stderr, "IMPORTIMPORT: name='%s'\n", name->data());
                        mod = Module::loadModuleByName(m_pvm, m_pvm->m_toplevelmodule, name);
                        fprintf(stderr, "IMPORTIMPORT: mod='%p'\n", mod);
                        if(mod == nullptr)
                        {
                            res = Value::makeNull();
                        }
                        else
                        {
                            res = Value::fromObject(mod);
                        }
                        stackPush(res);
                    }
                    break;
                case Instruction::OP_TYPEOF:
                    {
                        Value res;
                        Value thing;
                        const char* result;
                        thing = stackPop();
                        result = thing.name();
                        res = Value::fromObject(String::copy(m_pvm, result));
                        stackPush(res);
                    }
                    break;
                case Instruction::OP_ASSERT:
                    {
                        Value message;
                        Value expression;
                        message = stackPop();
                        expression = stackPop();
                        if(Value::isFalse(expression))
                        {
                            if(!message.isNull())
                            {
                                raiseClass(m_pvm->m_exceptions.asserterror, Value::toString(m_pvm, message)->data());
                            }
                            else
                            {
                                raiseClass(m_pvm->m_exceptions.asserterror, "assertion failed");
                            }
                        }
                    }
                    break;
                case Instruction::OP_EXTHROW:
                    {
                        bool isok;
                        Value peeked;
                        Value stacktrace;
                        ClassInstance* instance;
                        peeked = stackPeek(0);
                        isok = (
                            peeked.isInstance() ||
                            ClassObject::instanceOf(peeked.asInstance()->m_fromclass, m_pvm->m_exceptions.stdexception)
                        );
                        if(!isok)
                        {
                            NEON_VMMAC_TRYRAISE(m_pvm, Status::FAIL_RUNTIME, "instance of Exception expected");
                            break;
                        }
                        stacktrace = getExceptionStacktrace();
                        instance = peeked.asInstance();
                        instance->defProperty("stacktrace", stacktrace);
                        if(exceptionPropagate())
                        {
                            m_currentframe = &m_framevalues[m_framecount - 1];
                            break;
                        }
                        NEON_VMMAC_EXITVM(m_pvm);
                    }
                case Instruction::OP_EXTRY:
                    {
                        uint16_t addr;
                        uint16_t finaddr;
                        Value value;
                        String* type;
                        Property* field;
                        type = readString();
                        addr = readShort();
                        finaddr = readShort();
                        if(addr != 0)
                        {
                            //if(!m_pvm->m_definedglobals->get(Value::fromObject(type), &value) || !value.isClass())
                            field = m_pvm->m_definedglobals->getField(Value::fromObject(type));
                            if((field == nullptr) || !field->value().isClass())
                            {
                                //if(!m_currentframe->closure->m_scriptfunc->m_inmodule->m_deftable->get(Value::fromObject(type), &value) || !value.isClass())
                                field = m_currentframe->closure->m_scriptfunc->m_inmodule->m_deftable->getField(Value::fromObject(type));
                                if(field == nullptr || !field->value().isClass())
                                {
                                    NEON_VMMAC_TRYRAISE(m_pvm, Status::FAIL_RUNTIME, "object of type '%s' is not an exception", type->data());
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
                case Instruction::OP_EXPOPTRY:
                    {
                        m_currentframe->handlercount--;
                    }
                    break;
                case Instruction::OP_EXPUBLISHTRY:
                    {
                        m_currentframe->handlercount--;
                        if(exceptionPropagate())
                        {
                            m_currentframe = &m_framevalues[m_framecount - 1];
                            break;
                        }
                        NEON_VMMAC_EXITVM(m_pvm);
                    }
                    break;
                case Instruction::OP_SWITCH:
                    {
                        Value expr;
                        VarSwitch* sw;
                        sw = readConst().asSwitch();
                        expr = stackPeek(0);
                        auto field = sw->m_jumppositions->getField(expr);
                        if(field != nullptr)
                        {
                            m_currentframe->inscode += (int)field->value().asNumber();
                        }
                        else if(sw->m_defaultjump != -1)
                        {
                            m_currentframe->inscode += sw->m_defaultjump;
                        }
                        else
                        {
                            m_currentframe->inscode += sw->m_exitjump;
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

static void nn_cli_freeline(const char* line)
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
            source.reset();
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


static bool nn_cli_compilefiletobinary(neon::State* state, const char* file, const char* destfile)
{
    FILE* fh;
    neon::FuncClosure* closure;
    closure = state->compileFileToClosure(file);
    if(closure != nullptr)
    {
        fh = fopen(destfile, "wb");
        if(fh == nullptr)
        {
            return false;
        }
        closure->m_scriptfunc->m_compiledblob->toStream(fh);
        fclose(fh);
        
    }
    return false;
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
    ptyp(neon::Util::StrBuffer);
    ptyp(neon::Printer);
    ptyp(neon::Value);
    ptyp(neon::Object);
    ptyp(neon::Property::GetSetter);
    ptyp(neon::Property);
    ptyp(neon::ValArray);
    ptyp(neon::Blob);
    //ptyp(neon::HashTable::Entry);
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
    ptyp(neon::SyntaxRule);
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
    bool comptofile;
    bool wasusage;
    bool quitafterinit;
    bool inputisbin;
    char *arg;
    char* source;
    const char* compdest;
    const char* filename;
    char* nargv[128];
    neon::State* state;
    neon::Util::setOSUnicode();
    ok = true;
    wasusage = false;
    comptofile = false;
    quitafterinit = false;
    inputisbin = false;
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
        {"compile", 'c', OptionParser::A_REQUIRED, "compile to file"},
        {"frombinary", 'b', OptionParser::A_NONE, "input file is a binary file"},
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
        else if(co == 'b')
        {
            inputisbin = true;
        }
        else if(co == 'c')
        {
            comptofile = true;
            compdest = options.optarg;
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
        ok = state->runCode(source);
    }
    else if(nargc > 0)
    {
        filename = state->m_cliargv->at(0).asString()->data();
        fprintf(stderr, "nargv[0]=%s\n", filename);
        if(inputisbin)
        {
            auto cls = state->readBinaryFile(filename);
            if(cls == nullptr)
            {
                fprintf(stderr, "failed to read binary!\n");
                goto cleanup;
            }
            auto s = state->execFromClosure(cls, nullptr);
            ok = s == neon::Status::OK;
        }
        else
        {
            if(comptofile)
            {
                ok = nn_cli_compilefiletobinary(state, filename, compdest);
            }
            else
            {
                ok = state->runFile(filename);
            }
        }
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

