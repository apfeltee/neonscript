
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
#include "utf8.h"

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

#define NORMALIZE(token) #token

#define NEON_RAISEEXCLASS(state, exklass, ...) \
    state->raiseClass(exklass, __FILE__, __LINE__, __VA_ARGS__)

#define NEON_RAISEEXCEPTION(state, ...) \
    NEON_RAISEEXCLASS(state, state->m_exceptions.stdexception, __VA_ARGS__)

#if defined(__linux__) || defined(__CYGWIN__) || defined(__MINGW32_MAJOR_VERSION)
    #include <libgen.h>
    #include <limits.h>
#endif


#if defined(_WIN32)
    #include <fcntl.h>
    #include <io.h>
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

#define NEON_ARGS_FAIL(chp, ...) \
    (chp)->failSrcPos(__FILE__, __LINE__, __VA_ARGS__)

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

#define NEON_APIDEBUG(state, ...) \
    if((NEON_UNLIKELY((state)->m_conf.enableapidebug))) \
    { \
        neon::State::debugCallAPI(state, __FUNCTION__, __VA_ARGS__); \
    }


#define NEON_ASTDEBUG(state, ...) \
    if((NEON_UNLIKELY((state)->m_conf.enableastdebug))) \
    { \
        neon::State::debugCallAST(state, __FUNCTION__, __VA_ARGS__); \
    }


namespace neon
{
    enum class Status
    {
        OK,
        FAIL_COMPILE,
        FAIL_RUNTIME,
    };

    enum AstCompContext
    {
        NEON_COMPCONTEXT_NONE,
        NEON_COMPCONTEXT_CLASS,
        NEON_COMPCONTEXT_ARRAY,
        NEON_COMPCONTEXT_NESTEDFUNCTION,
    };

    typedef enum /**/AstCompContext AstCompContext;
    typedef enum /**/ Status Status;
    typedef struct /**/FormatInfo FormatInfo;
    typedef struct /**/ Object Object;
    typedef struct /**/ String String;
    typedef struct /**/ Array Array;
    typedef struct /**/ ScopeUpvalue ScopeUpvalue;
    typedef struct /**/ ClassObject ClassObject;
    typedef struct /**/ FuncNative FuncNative;
    typedef struct /**/ Module Module;
    typedef struct /**/ FuncScript FuncScript;
    typedef struct /**/ FuncClosure FuncClosure;
    typedef struct /**/ ClassInstance ClassInstance;
    typedef struct /**/ FuncBound FuncBound;
    typedef struct /**/ Range Range;
    typedef struct /**/ Dictionary Dictionary;
    typedef struct /**/ File File;
    typedef struct /**/ UserData UserData;
    typedef struct /**/ Value Value;
    typedef struct /**/Property Property;
    typedef struct /**/ ValArray ValArray;
    typedef struct /**/ Blob Blob;
    typedef struct /**/ HashTable HashTable;
    typedef struct /**/ AstToken AstToken;
    typedef struct /**/ AstLexer AstLexer;
    typedef struct /**/ AstParser AstParser;
    typedef struct /**/ AstRule AstRule;
    typedef struct /**/ ModExport ModExport;
    typedef struct /**/ State State;
    typedef struct /**/ Printer Printer;
    typedef struct /**/ Arguments Arguments;
    typedef struct /**/Instruction Instruction;
    typedef struct /**/VarSwitch VarSwitch;

    typedef Value (*NativeFN)(State*, Arguments*);
    typedef void (*PtrFreeFN)(void*);
    typedef bool (*AstParsePrefixFN)(AstParser*, bool);
    typedef bool (*AstParseInfixFN)(AstParser*, AstToken, bool);
    typedef Value (*ClassFieldFN)(State*);
    typedef void (*ModLoaderFN)(State*);
    typedef ModExport* (*ModInitFN)(State*);

}

    // preproto
    neon::String *nn_util_numbertobinstring(neon::State *state, long n);
    neon::String *nn_util_numbertooctstring(neon::State *state, long long n, bool numeric);
    neon::String *nn_util_numbertohexstring(neon::State *state, long long n, bool numeric);
    void nn_util_mtseed(uint32_t seed, uint32_t *binst, uint32_t *index);
    uint32_t nn_util_mtgenerate(uint32_t *binst, uint32_t *index);
    double nn_util_mtrand(double lowerlimit, double upperlimit);
    neon::ClassObject *nn_util_makeclass(neon::State *state, const char *name, neon::ClassObject *parent);
    bool nn_util_methodisprivate(neon::String *name);
    bool nn_util_isinstanceof(neon::ClassObject *klass1, neon::ClassObject *expected);
    int nn_util_findfirstpos(const char *str, int len, int ch);
    void nn_gcmem_markvalue(neon::State* state, neon::Value value);
    void nn_gcmem_markobject(neon::State *state, neon::Object *object);
    void nn_import_closemodule(void *dlw);
    void nn_printer_printvalue(neon::Printer *wr, neon::Value value, bool fixstring, bool invmethod);
    uint32_t nn_value_hashvalue(neon::Value value);
    bool nn_value_compare_actual(neon::State* state, neon::Value a, neon::Value b);
    bool nn_value_compare(neon::State *state, neon::Value a, neon::Value b);
    neon::Value nn_value_copyvalue(neon::State* state, neon::Value value);
    void nn_gcmem_destroylinkedobjects(neon::State *state);
    neon::FuncScript* nn_astparser_compilesource(neon::State* state, neon::Module* module, const char* source, neon::Blob* blob, bool fromimport, bool keeplast);
    int nn_nestcall_prepare(neon::State *state, neon::Value callable, neon::Value mthobj, neon::Array *callarr);
    bool nn_nestcall_callfunction(neon::State *state, neon::Value callable, neon::Value thisval, neon::Array *args, neon::Value *dest);

namespace neon
{
    namespace Util
    {
        static char* strCopy(const char* src, size_t len)
        {
            char* buf;
            buf = (char*)malloc(sizeof(char) * (len+1));
            if(buf == NULL)
            {
                return NULL;
            }
            memset(buf, 0, len+1);
            memcpy(buf, src, len);
            return buf;
        }

        static char* strCopy(const char* src)
        {
            return strCopy(src, strlen(src));
        }

        template<typename FuncT>
        static char* charCaseChange(char* str, size_t length, FuncT changerfn)
        {
            int c;
            size_t i;
            for(i=0; i<length; i++)
            {
                c = str[i];
                str[i] = changerfn(c);
            }
            return str;
        }

        static int charToUpper(int c)
        {
            return toupper(c);
        }

        static int charToLower(int c)
        {
            return tolower(c);
        }

        static char* strToUpperInplace(char* str, size_t length)
        {
            return charCaseChange(str, length, charToUpper);
        }

        static char* strToLowerInplace(char* str, size_t length)
        {
            return charCaseChange(str, length, charToLower);
        }

        /* returns the number of bytes contained in a unicode character */
        static int utf8NumBytes(int value)
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

        static char* utf8Encode(unsigned int code, size_t* len)
        {
            int count;
            char* chars;
            count = utf8NumBytes((int)code);
            *len = 0;
            if(count > 0)
            {
                *len = count;
                chars = (char*)malloc(sizeof(char) * ((size_t)count + 1));
                if(chars != NULL)
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
            return NULL;
        }

        static int utf8Decode(const uint8_t* bytes, uint32_t length)
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

        static char* utf8CodePoint(const char* str, char* outcodepoint)
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

        static char* utf8Find(const char* haystack, size_t hslen, const char* needle, size_t nlen)
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
                    haystack = Util::utf8CodePoint(maybematch, &throwawaycodepoint);
                }
            }
            /* no match */
            return NULL;
        }

        static char* fileReadHandle(FILE* hnd, size_t* dlen)
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
            if(fseek(hnd, 0, SEEK_END) == -1)
            {
                return NULL;
            }
            if((rawtold = ftell(hnd)) == -1)
            {
                return NULL;
            }
            toldlen = rawtold;
            if(fseek(hnd, 0, SEEK_SET) == -1)
            {
                return NULL;
            }
            buf = (char*)malloc(sizeof(char) * toldlen + 1);
            memset(buf, 0, toldlen+1);
            if(buf != NULL)
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
                if(dlen != NULL)
                {
                    *dlen = actuallen;
                }
                return buf;
            }
            return NULL;
        }

        static char* fileReadFile(const char* filename, size_t* dlen)
        {
            char* b;
            FILE* fh;
            fh = fopen(filename, "rb");
            if(fh == NULL)
            {
                return NULL;
            }
            #if defined(NEON_PLAT_ISWINDOWS)
                _setmode(fileno(fh), _O_BINARY);
            #endif
            b = fileReadHandle(fh, dlen);
            fclose(fh);
            return b;
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
        }

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

    };

    class Memory
    {
        public:
    };

    struct Color
    {
        public:
            enum Code
            {
                C_RESET,
                C_RED,
                C_GREEN,    
                C_YELLOW,
                C_BLUE,
                C_MAGENTA,
                C_CYAN
            };

        private:
            bool m_istty = false;

        public:
            NEON_ALWAYSINLINE Color()
            {
                #if !defined(NEON_CFG_FORCEDISABLECOLOR)
                int fdstdout;
                int fdstderr;
                fdstdout = fileno(stdout);
                fdstderr = fileno(stderr);
                m_istty = (osfn_isatty(fdstderr) && osfn_isatty(fdstdout));
                #endif
            }

            NEON_ALWAYSINLINE const char* color(Code tc)
            {
                #if !defined(NEON_CFG_FORCEDISABLECOLOR)
                    if(m_istty)
                    {
                        switch(tc)
                        {
                            case Color::C_RESET:
                                return "\x1B[0m";
                            case Color::C_RED:
                                return "\x1B[31m";
                            case Color::C_GREEN:
                                return "\x1B[32m";
                            case Color::C_YELLOW:
                                return "\x1B[33m";
                            case Color::C_BLUE:
                                return "\x1B[34m";
                            case Color::C_MAGENTA:
                                return "\x1B[35m";
                            case Color::C_CYAN:
                                return "\x1B[36m";
                        }
                    }
                #else
                    (void)tc;
                #endif
                return "";
            }
    };

    struct Value
    {
        public:
            enum class Type
            {
                T_EMPTY,
                T_NULL,
                T_BOOL,
                T_NUMBER,
                T_OBJECT,
            };

            enum ObjType
            {
                /* containers */
                OBJT_STRING,
                OBJT_RANGE,
                OBJT_ARRAY,
                OBJT_DICT,
                OBJT_FILE,

                /* base object types */
                OBJT_UPVALUE,
                OBJT_FUNCBOUND,
                OBJT_FUNCCLOSURE,
                OBJT_FUNCSCRIPT,
                OBJT_INSTANCE,
                OBJT_FUNCNATIVE,
                OBJT_CLASS,

                /* non-user objects */
                OBJT_MODULE,
                OBJT_SWITCH,
                /* object type that can hold any C pointer */
                OBJT_USERDATA,
            };

        public:
            static NEON_ALWAYSINLINE Value makeValue(Type type)
            {
                Value v;
                v.m_valtype = type;
                return v;
            }

            static NEON_ALWAYSINLINE Value makeEmpty()
            {
                return makeValue(Type::T_EMPTY);
            }

            static NEON_ALWAYSINLINE Value makeNull()
            {
                Value v;
                v = makeValue(Type::T_NULL);
                return v;
            }

            static NEON_ALWAYSINLINE Value makeBool(bool b)
            {
                Value v;
                v = makeValue(Type::T_BOOL);
                v.m_valunion.vbool = b;
                return v;
            }

            static NEON_ALWAYSINLINE Value makeNumber(double d)
            {
                Value v;
                v = makeValue(Type::T_NUMBER);
                v.m_valunion.vfltnum = d;
                return v;
            }

            static NEON_ALWAYSINLINE Value makeInt(int i)
            {
                Value v;
                v = makeValue(Type::T_NUMBER);
                v.m_valunion.vfltnum = i;
                return v;
            }

            template<typename ObjT>
            static NEON_ALWAYSINLINE Value fromObject(ObjT* obj)
            {
                Value v;
                v = makeValue(Type::T_OBJECT);
                v.m_valunion.vobjpointer = (Object*)obj;
                return v;
            }

        public:
            Type m_valtype;
            union
            {
                bool vbool;
                double vfltnum;
                Object* vobjpointer;
            } m_valunion;

        public:
            /*
            * important:
            * do not add any additional constructors here;
            * this way neon::Value remains a trivial type, with trivial copying.
            */
            Value() = default;
            ~Value() = default;

            NEON_ALWAYSINLINE Type type() const
            {
                return m_valtype;
            }

            NEON_ALWAYSINLINE bool isObject() const
            {
                return (m_valtype == Type::T_OBJECT);
            }

            NEON_ALWAYSINLINE bool isNull() const
            {
                return (m_valtype == Type::T_NULL);
            }

            NEON_ALWAYSINLINE bool isEmpty() const
            {
                return (m_valtype == Type::T_EMPTY);
            }

            NEON_ALWAYSINLINE bool isObjType(Value::ObjType t) const;

            NEON_ALWAYSINLINE bool isFuncNative() const
            {
                return isObjType(Value::OBJT_FUNCNATIVE);
            }

            NEON_ALWAYSINLINE bool isFuncScript() const
            {
                return isObjType(Value::OBJT_FUNCSCRIPT);
            }

            NEON_ALWAYSINLINE bool isFuncClosure() const
            {
                return isObjType(Value::OBJT_FUNCCLOSURE);
            }

            NEON_ALWAYSINLINE bool isFuncBound() const
            {
                return isObjType(Value::OBJT_FUNCBOUND);
            }

            NEON_ALWAYSINLINE bool isClass() const
            {
                return isObjType(Value::OBJT_CLASS);
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
                return isObjType(Value::OBJT_STRING);
            }

            NEON_ALWAYSINLINE bool isBool() const
            {
                return (type() == Value::Type::T_BOOL);
            }

            NEON_ALWAYSINLINE bool isNumber() const
            {
                return (type() == Value::Type::T_NUMBER);
            }

            NEON_ALWAYSINLINE bool isInstance() const
            {
                return isObjType(Value::OBJT_INSTANCE);
            }

            NEON_ALWAYSINLINE bool isArray() const
            {
                return isObjType(Value::OBJT_ARRAY);
            }

            NEON_ALWAYSINLINE bool isDict() const
            {
                return isObjType(Value::OBJT_DICT);
            }

            NEON_ALWAYSINLINE bool isFile() const
            {
                return isObjType(Value::OBJT_FILE);
            }

            NEON_ALWAYSINLINE bool isRange() const
            {
                return isObjType(Value::OBJT_RANGE);
            }

            NEON_ALWAYSINLINE bool isModule() const
            {
                return isObjType(Value::OBJT_MODULE);
            }

            NEON_ALWAYSINLINE Object* asObject() const
            {
                return (m_valunion.vobjpointer);
            }

            NEON_ALWAYSINLINE Value::ObjType objType() const;

            NEON_ALWAYSINLINE bool asBool() const
            {
                return (m_valunion.vbool);
            }

            NEON_ALWAYSINLINE FuncNative* asFuncNative() const
            {
                return ((FuncNative*)asObject());
            }

            NEON_ALWAYSINLINE FuncScript* asFuncScript() const
            {
                return ((FuncScript*)asObject());
            }

            NEON_ALWAYSINLINE FuncClosure* asFuncClosure() const
            {
                return ((FuncClosure*)asObject());
            }

            NEON_ALWAYSINLINE ClassObject* asClass() const
            {
                return ((ClassObject*)asObject());
            }

            NEON_ALWAYSINLINE FuncBound* asFuncBound() const
            {
                return ((FuncBound*)asObject());
            }

            NEON_ALWAYSINLINE VarSwitch* asSwitch() const
            {
                return ((VarSwitch*)asObject());
            }

            NEON_ALWAYSINLINE UserData* asUserdata() const
            {
                return ((UserData*)asObject());
            }

            NEON_ALWAYSINLINE Module* asModule() const
            {
                return ((Module*)asObject());
            }

            NEON_ALWAYSINLINE Dictionary* asDict() const
            {
                return ((Dictionary*)asObject());
            }

            NEON_ALWAYSINLINE File* asFile() const
            {
                return ((File*)asObject());
            }

            NEON_ALWAYSINLINE Range* asRange() const
            {
                return ((Range*)asObject());
            }

            NEON_ALWAYSINLINE double asNumber() const
            {
                return (m_valunion.vfltnum);
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
            /* if file: should be closed when printer is destroyed? */
            bool m_shouldclose = false;
            /* if file: should write operations be flushed via fflush()? */
            bool m_shouldflush = false;
            /* if string: true if $strbuf was taken via nn_printer_take */
            bool m_stringtaken = false;
            /* was this printer instance created on stack? */
            bool m_fromstack = false;
            bool m_shortenvalues = false;
            size_t m_maxvallength = 0;
            /* the mode that determines what printer actually does */
            PrintMode m_pmode = PMODE_UNDEFINED;
            State* m_pvm = nullptr;
            StringBuffer* m_strbuf = nullptr;
            FILE* m_filehandle = nullptr;

        private:
            void initVars(State* state, PrintMode mode)
            {
                m_pvm = state;
                m_fromstack = false;
                m_pmode = PMODE_UNDEFINED;
                m_shouldclose = false;
                m_shouldflush = false;
                m_stringtaken = false;
                m_shortenvalues = false;
                m_maxvallength = 15;
                m_strbuf = NULL;
                m_filehandle = NULL;
                m_pmode = mode;
            }

        public:
            static Printer* makeUndefined(State* state, PrintMode mode)
            {
                Printer* pd;
                (void)state;
                pd = new Printer;
                pd->initVars(state, mode);
                return pd;
            }

            static Printer* makeIO(State* state, FILE* fh, bool shouldclose)
            {
                Printer* pd;
                pd = makeUndefined(state, PMODE_FILE);
                pd->m_filehandle = fh;
                pd->m_shouldclose = shouldclose;
                return pd;
            }

            static Printer* makeString(State* state)
            {
                Printer* pd;
                pd = makeUndefined(state, PMODE_STRING);
                pd->m_strbuf = new StringBuffer(0);
                return pd;
            }

            static void makeStackIO(State* state, Printer* pd, FILE* fh, bool shouldclose)
            {
                pd->initVars(state, PMODE_FILE);
                pd->m_fromstack = true;
                pd->m_filehandle = fh;
                pd->m_shouldclose = shouldclose;
            }

            static void makeStackString(State* state, Printer* pd)
            {
                pd->initVars(state, PMODE_STRING);
                pd->m_fromstack = true;
                pd->m_pmode = PMODE_STRING;
                pd->m_strbuf = new StringBuffer(0);
            }

        public:
            Printer()
            {
            }

            ~Printer()
            {
                destroy();
            }

            bool destroy()
            {
                PrintMode omode;
                omode = m_pmode;
                m_pmode = PMODE_UNDEFINED;
                if(omode == PMODE_UNDEFINED)
                {
                    return false;
                }
                fprintf(stderr, "Printer::dtor: m_pmode=%d\n", omode);
                if(omode == PMODE_STRING)
                {
                    if(!m_stringtaken)
                    {
                        delete m_strbuf;
                        return true;
                    }
                    return false;
                }
                else if(omode == PMODE_FILE)
                {
                    if(m_shouldclose)
                    {
                        //fclose(m_filehandle);
                    }
                }
                return true;
            }

            String* takeString();

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
                            format("\\x%02x", (unsigned char)ch);
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
                m_strbuf->appendFormat(fmt, args...);
                return true;
            }

            template<typename... ArgsT>
            bool format(const char* fmt, ArgsT&&... args)
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
    };


    struct State
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
                size_t handlercount;
                size_t gcprotcount;
                size_t stackslotpos;
                Instruction* inscode;
                FuncClosure* closure;
                ExceptionFrame handlers[NEON_CFG_MAXEXCEPTHANDLERS];
            };

            struct GC
            {
                template<typename InType>
                static InType* growArray(State* state, InType* ptr, size_t oldcnt, size_t neucnt)
                {
                    size_t tsz;
                    tsz = sizeof(InType);
                    return (InType*)GC::reallocate(state, ptr, tsz * oldcnt, tsz * neucnt);
                }

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
                    if(result == NULL)
                    {
                        fprintf(stderr, "fatal error: failed to allocate %zd bytes\n", newsize);
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
                    pointer = NULL;
                }

                template<typename Type>
                static void release(State* state, Type* pointer)
                {
                    release(state, pointer, sizeof(Type));
                }

                static void* allocate(State* state, size_t size, size_t amount)
                {
                    return GC::reallocate(state, NULL, 0, size * amount);
                }
            };

        public:
            template<typename... ArgsT>
            static NEON_ALWAYSINLINE void debugCallAPI(State* state, const char* funcname, const char* format, ArgsT&&... args)
            {
                static auto fn_fprintf = fprintf;
                (void)state;
                fprintf(stderr, "API CALL: to '%s': ", funcname);
                fn_fprintf(stderr, format, args...);
                fprintf(stderr, "\n");
            }

            template<typename... ArgsT>
            static NEON_ALWAYSINLINE void debugCallAST(State* state, const char* funcname, const char* format, ArgsT&&... args)
            {
                static auto fn_fprintf = fprintf;
                (void)state;
                fprintf(stderr, "AST CALL: to '%s': ", funcname);
                fn_fprintf(stderr, format, args...);
                fprintf(stderr, "\n");
            }

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
                bool m_debuggc = false;
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
                size_t graycount;
                size_t graycapacity;
                size_t bytesallocated;
                size_t nextgc;
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

            char* m_rootphysfile;

            Dictionary* m_envdict;

            String* m_classconstructorname;
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

            bool m_isrepl;
            bool m_markvalue;
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
                if(((int)needed < 0)
                    #if 0
                    || (needed < m_vmstate.stackcapacity)
                    #endif
                )
                {
                    return true;
                }
                nforvals = (needed * 2);
                oldsz = m_vmstate.stackcapacity;
                newsz = oldsz + nforvals;
                allocsz = ((newsz + 1) * sizeof(Value));
                fprintf(stderr, "*** resizing stack: needed %zd (%zd), from %zd to %zd, allocating %zd ***\n", nforvals, needed, oldsz, newsz, allocsz);
                oldbuf = m_vmstate.stackvalues;
                newbuf = (Value*)realloc(oldbuf, allocsz);
                if(newbuf == NULL)
                {
                    fprintf(stderr, "internal error: failed to resize stackvalues!\n");
                    abort();
                }
                m_vmstate.stackvalues = (Value*)newbuf;
                fprintf(stderr, "oldcap=%zd newsz=%zd\n", m_vmstate.stackcapacity, newsz);
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
                    if(newbuf == NULL)
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

            void init();

        public:
            State()
            {
                init();
            }

            ~State();

            void defGlobalValue(const char* name, neon::Value val);
            void defNativeFunction(const char* name, neon::NativeFN fptr, void* uptr);
            void defNativeFunction(const char* name, neon::NativeFN fptr);

            bool checkMaybeResize()
            {
                if((m_vmstate.stackidx+1) >= m_vmstate.stackcapacity)
                {
                    if(!resizeStack(m_vmstate.stackidx + 1))
                    {
                        return NEON_RAISEEXCEPTION(this, "failed to resize stack due to overflow");
                    }
                    return true;
                }
                if(m_vmstate.framecount >= m_vmstate.framecapacity)
                {
                    if(!resizeFrames(m_vmstate.framecapacity + 1))
                    {
                        return NEON_RAISEEXCEPTION(this, "failed to resize frames due to overflow");
                    }
                    return true;
                }
                return false;
            }

            template<typename ObjT>
            ObjT* gcProtect(ObjT* object)
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

            NEON_ALWAYSINLINE uint8_t vmReadByte()
            {
                uint8_t r;
                r = m_vmstate.currentframe->inscode->code;
                m_vmstate.currentframe->inscode++;
                return r;
            }

            NEON_ALWAYSINLINE Instruction vmReadInstruction()
            {
                Instruction r;
                r = *m_vmstate.currentframe->inscode;
                m_vmstate.currentframe->inscode++;
                return r;
            }

            NEON_ALWAYSINLINE uint16_t vmReadShort()
            {
                uint8_t b;
                uint8_t a;
                a = m_vmstate.currentframe->inscode[0].code;
                b = m_vmstate.currentframe->inscode[1].code;
                m_vmstate.currentframe->inscode += 2;
                return (uint16_t)((a << 8) | b);
            }

            Value vmReadConst();

            NEON_ALWAYSINLINE String* vmReadString()
            {
                return vmReadConst().asString();
            }
    };

    struct Object
    {
        protected:
            template<typename Type>
            static Type* make(State* state, Value::ObjType type)
            {
                size_t size;
                Object* object;
                size = sizeof(Type);
                object = (Object*)State::GC::allocate(state, size, 1);
                object->m_objtype = type;
                object->m_mark = !state->m_markvalue;
                object->m_isstale = false;
                object->m_pvm = state;
                object->m_nextobj = state->m_vmstate.linkedobjects;
                state->m_vmstate.linkedobjects = object;
                #if 0
                if(NEON_UNLIKELY(state->m_conf.m_debuggc))
                {
                    state->m_debugprinter->format("%p allocate %ld for %d\n", (void*)object, size, type);
                }
                #endif
                return (Type*)object;
            }

        public:
            template<typename Type>
            static void releaseObject(State* state, Type* obj)
            {
                obj->destroyThisObject();
                State::GC::release(state, obj);
            }

        public:
            State* m_pvm;
            Value::ObjType m_objtype;
            bool m_mark;
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
            Object() = delete;
            Object(Object&) = delete;
            Object(const Object&) = delete;
            ~Object() = delete;            
            virtual bool destroyThisObject() = 0;

    };

    NEON_ALWAYSINLINE Value::ObjType Value::objType() const
    {
        return asObject()->m_objtype;
    }

    NEON_ALWAYSINLINE bool Value::isObjType(Value::ObjType t) const
    {
        return isObject() && asObject()->m_objtype == t;
    }


    struct ValArray
    {
        public:
            State* m_pvm;
            /* type size of the stored value (via sizeof) */
            size_t m_typsize;
            /* how many entries are currently stored? */
            size_t m_count;
            /* how many entries can be stored before growing? */
            size_t m_capacity;
            Value* m_values;

        private:
            void makeSize(State* state, size_t size)
            {
                m_pvm = state;
                m_typsize = size;
                m_capacity = 0;
                m_count = 0;
                m_values = NULL;
            }

            bool destroy()
            {
                if(m_values != nullptr)
                {
                    State::GC::release(m_pvm, m_values, m_typsize*m_capacity);
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

            inline size_t count() const
            {
                return m_count;
            }

            inline size_t size() const
            {
                return m_count;
            }

            void gcMark()
            {
                size_t i;
                for(i = 0; i < m_count; i++)
                {
                    nn_gcmem_markvalue(m_pvm, m_values[i]);
                }
            }

            void push(Value value)
            {
                size_t oldcapacity;
                if(m_capacity < m_count + 1)
                {
                    oldcapacity = m_capacity;
                    m_capacity = GROW_CAPACITY(oldcapacity);
                    m_values = State::GC::growArray(m_pvm, m_values, oldcapacity, m_capacity);
                }
                m_values[m_count] = value;
                m_count++;
            }

            void insert(Value value, size_t index)
            {
                size_t i;
                size_t oldcap;
                if(m_capacity <= index)
                {
                    m_capacity = GROW_CAPACITY(index);
                    m_values = State::GC::growArray(m_pvm, m_values, m_count, m_capacity);
                }
                else if(m_capacity < m_count + 2)
                {
                    oldcap = m_capacity;
                    m_capacity = GROW_CAPACITY(oldcap);
                    m_values = State::GC::growArray(m_pvm, m_values, oldcap, m_capacity);
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
                T_INVALID,
                T_VALUE,
                /*
                * indicates this field contains a function, a pseudo-getter (i.e., ".length")
                * which is called upon getting
                */
                T_FUNCTION,
            };

            struct PropGetSet
            {
                Value getter;
                Value setter;
            };

        public:
            Type m_type;
            Value value;
            bool havegetset;
            PropGetSet getset;

        public:
            void init(Value val, Type t)
            {
                this->m_type = t;
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

    struct HashTable
    {
        public:
            struct Entry
            {
                Value key;
                Property value;
            };

        public:
            /*
            * FIXME: extremely stupid hack: $active ensures that a table that was destroyed
            * does not get marked again, et cetera.
            * since ~HashTable() zeroes the data before freeing, $active will be
            * false, and thus, no further marking occurs.
            * obviously the reason is that somewhere a table (from ClassInstance) is being
            * read after being freed, but for now, this will work(-ish).
            */
            bool m_active;
            size_t m_count;
            size_t m_capacity;
            State* m_pvm;
            Entry* m_entries;

        public:

            HashTable(State* state)
            {
                m_pvm = state;
                m_active = true;
                m_count = 0;
                m_capacity = 0;
                m_entries = NULL;
            }

            ~HashTable()
            {
                State* state;
                state = m_pvm;
                State::GC::release(state, m_entries, sizeof(Entry)*m_capacity);
            }

            Entry* findEntryByValue(Entry* availents, size_t availcap, Value key)
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
                tombstone = NULL;
                while(true)
                {
                    entry = &availents[index];
                    if(entry->key.isEmpty())
                    {
                        if(entry->value.value.isNull())
                        {
                            /* empty entry */
                            if(tombstone != NULL)
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
                            if(tombstone == NULL)
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
                return NULL;
            }

            Entry* findEntryByStr(Entry* availents, size_t availcap, Value valkey, const char* kstr, size_t klen, uint32_t hash);

            Property* getFieldByValue(Value key)
            {
                State* state;
                Entry* entry;
                (void)state;
                state = m_pvm;
                if(m_count == 0 || m_entries == NULL)
                {
                    return NULL;
                }
                #if defined(DEBUG_TABLE) && DEBUG_TABLE
                fprintf(stderr, "getting entry with hash %u...\n", nn_value_hashvalue(key));
                #endif
                entry = findEntryByValue(m_entries, m_capacity, key);
                if(entry->key.isEmpty() || entry->key.isNull())
                {
                    return NULL;
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
                if(m_count == 0 || m_entries == NULL)
                {
                    return NULL;
                }
                #if defined(DEBUG_TABLE) && DEBUG_TABLE
                fprintf(stderr, "getting entry with hash %u...\n", nn_value_hashvalue(key));
                #endif
                entry = findEntryByStr(m_entries, m_capacity, valkey, kstr, klen, hash);
                if(entry->key.isEmpty() || entry->key.isNull())
                {
                    return NULL;
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
                hash = Util::hashString(kstr, klen);
                return getFieldByStr(Value::makeEmpty(), kstr, klen, hash);
            }

            Property* getField(Value key);

            bool get(Value key, Value* value)
            {
                Property* field;
                field = getField(key);
                if(field != NULL)
                {
                    *value = field->value;
                    return true;
                }
                return false;
            }

            void adjustCapacity(size_t needcap)
            {
                size_t i;
                State* state;
                Entry* dest;
                Entry* entry;
                Entry* nents;
                state = m_pvm;
                nents = (Entry*)State::GC::allocate(state, sizeof(Entry), needcap);
                for(i = 0; i < needcap; i++)
                {
                    nents[i].key = Value::makeEmpty();
                    nents[i].value = Property(Value::makeNull(), Property::Type::T_VALUE);
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
                State::GC::release(state, m_entries, sizeof(Entry) * m_capacity);
                m_entries = nents;
                m_capacity = needcap;
            }

            bool setType(Value key, Value value, Property::Type ftyp, bool keyisstring)
            {
                bool isnew;
                size_t needcap;
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
                return setType(key, value, Property::Type::T_VALUE, key.isString());
            }

            bool setCStrType(const char* cstrkey, Value value, Property::Type ftype);

            bool setCStr(const char* cstrkey, Value value)
            {
                return setCStrType(cstrkey, value, Property::Type::T_VALUE);
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
                entry->value = Property(Value::makeBool(true), Property::Type::T_VALUE);
                return true;
            }

            static void addAll(HashTable* from, HashTable* to)
            {
                size_t i;
                Entry* entry;
                for(i = 0; i < from->m_capacity; i++)
                {
                    entry = &from->m_entries[i];
                    if(!entry->key.isEmpty())
                    {
                        to->setType(entry->key, entry->value.value, entry->value.m_type, false);
                    }
                }
            }

            static void copy(HashTable* from, HashTable* to)
            {
                size_t i;
                State* state;
                Entry* entry;
                state = from->m_pvm;
                for(i = 0; i < (size_t)from->m_capacity; i++)
                {
                    entry = &from->m_entries[i];
                    if(!entry->key.isEmpty())
                    {
                        to->setType(entry->key, nn_value_copyvalue(state, entry->value.value), entry->value.m_type, false);
                    }
                }
            }

            String* findString(const char* chars, size_t length, uint32_t hash);

            Value findKey(Value value)
            {
                size_t i;
                Entry* entry;
                for(i = 0; i < (size_t)m_capacity; i++)
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
                size_t i;
                Entry* entry;
                if(table == NULL)
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
                    if(entry != NULL)
                    {
                        nn_gcmem_markvalue(state, entry->key);
                        nn_gcmem_markvalue(state, entry->value.value);
                    }
                }
            }

            static void removeMarked(State* state, HashTable* table)
            {
                size_t i;
                Entry* entry;
                for(i = 0; i < table->m_capacity; i++)
                {
                    entry = &table->m_entries[i];
                    if(entry->key.isObject() && entry->key.asObject()->m_mark != state->m_markvalue)
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
            size_t m_count;
            size_t m_capacity;
            Instruction* m_instrucs;
            ValArray* m_constants;
            ValArray* m_argdefvals;

        public:
            Blob(State* state)
            {
                m_pvm = state;
                m_count = 0;
                m_capacity = 0;
                m_instrucs = NULL;
                m_constants = new ValArray(state);
                m_argdefvals = new ValArray(state);
            }

            ~Blob()
            {
                if(m_instrucs != NULL)
                {
                    State::GC::release(m_pvm, m_instrucs, sizeof(Instruction) * m_capacity);
                }
                delete m_constants;
                delete m_argdefvals;
            }

            void push(Instruction ins)
            {
                size_t oldcapacity;
                if(m_capacity < m_count + 1)
                {
                    oldcapacity = m_capacity;
                    m_capacity = GROW_CAPACITY(oldcapacity);
                    m_instrucs = State::GC::growArray(m_pvm, m_instrucs, oldcapacity, m_capacity);
                }
                m_instrucs[m_count] = ins;
                m_count++;
            }

            void pushInst(bool isop, uint8_t code, int srcline)
            {
                return push(Instruction{isop, code, srcline});
            }

            size_t pushConst(Value value)
            {
                m_constants->push(value);
                return m_constants->m_count - 1;
            }

            size_t pushArgDefVal(Value value)
            {
                m_argdefvals->push(value);
                return m_argdefvals->m_count - 1;
            }
    };

    struct String: public Object
    {
        private:
            static void strCachePut(State* state, String* rs)
            {
                state->stackPush(Value::fromObject(rs));
                state->m_cachedstrings->set(Value::fromObject(rs), Value::makeNull());
                state->stackPop();
            }

            static String* strCacheFind(State* state, const char* chars, size_t length, uint32_t hash)
            {
                return state->m_cachedstrings->findString(chars, length, hash);
            }

        public:
            static String* strMakeHashedBuffer(State* state, const char* estr, size_t elen, uint32_t hash, bool istaking)
            {
                StringBuffer* sbuf;
                (void)istaking;
                sbuf = new StringBuffer(elen);
                sbuf->append(estr, elen);
                return String::makeBuffer(state, sbuf, hash);
            }

            static String* makeBuffer(State* state, StringBuffer* sbuf, uint32_t hash)
            {
                String* rt;
                rt = Object::make<String>(state, Value::OBJT_STRING);
                rt->m_sbuf = sbuf;
                rt->m_hash = hash;
                strCachePut(state, rt);
                return rt;
            }

            static String* take(State* state, char* chars, size_t length)
            {
                uint32_t hash;
                String* rs;
                hash = Util::hashString(chars, length);
                rs = strCacheFind(state, chars, length, hash);
                if(rs == NULL)
                {
                    rs = strMakeHashedBuffer(state, chars, length, hash, true);
                }
                State::GC::release(state, chars, sizeof(char) * ((size_t)length + 1));
                return rs;
            }

            static String* copy(State* state, const char* chars, size_t length)
            {
                uint32_t hash;
                String* rs;
                hash = Util::hashString(chars, length);
                rs = strCacheFind(state, chars, length, hash);
                if(rs != NULL)
                {
                    return rs;
                }
                rs = strMakeHashedBuffer(state, chars, length, hash, false);
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

        public:
            uint32_t m_hash;
            StringBuffer* m_sbuf;

        public:
            String() = delete;
            ~String() = delete;

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


    struct Array: public Object
    {
        public:
            static Array* make(State* state, size_t cnt, Value filler)
            {
                Array* rt;
                size_t i;
                rt = Object::make<Array>(state, Value::OBJT_ARRAY);
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
            Array() = delete;
            ~Array() = delete;

            bool destroyThisObject()
            {
                delete m_varray;
                return true;
            }

            inline size_t size() const
            {
                return m_varray->size();
            }

            inline size_t count() const
            {
                return m_varray->count();
            }

            void push(Value value)
            {
                /*m_pvm->stackPush(value);*/
                m_varray->push(value);
                /*m_pvm->stackPop(); */
            }

            Value at(size_t i) const
            {
                return m_varray->m_values[i];
            }

            void set(size_t i, Value v)
            {
                m_varray->m_values[i] = v;
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
                    newlist->push(at(i));
                }
                return newlist;
            }
    };

    struct Dictionary: public Object
    {
        public:
            static Dictionary* make(State* state)
            {
                Dictionary* rt;
                rt = Object::make<Dictionary>(state, Value::OBJT_DICT);
                rt->m_names = new ValArray(state);
                rt->m_htab = new HashTable(state);
                return rt;
            }

        public:
            ValArray* m_names;
            HashTable* m_htab;

        public:
            Dictionary() = delete;
            ~Dictionary() = delete;

            bool destroyThisObject()
            {
                delete m_names;
                delete m_htab;
                return true;
            }

            void mark()
            {
                m_names->gcMark();
                HashTable::mark(m_pvm, m_htab);
            }

            bool set(Value key, Value value)
            {
                Value tempvalue;
                if(!m_htab->get(key, &tempvalue))
                {
                    /* add key if it doesn't exist. */
                    m_names->push(key);
                }
                return m_htab->set(key, value);
            }

            void add(Value key, Value value)
            {
                set(key, value);
            }

            void addCstr(const char* ckey, Value value)
            {
                String* os;
                os = String::copy(m_pvm, ckey);
                add(Value::fromObject(os), value);
            }

            Property* get(Value key)
            {
                return m_htab->getField(key);
            }
    };

    struct ScopeUpvalue: public Object
    {
        public:
            static ScopeUpvalue* make(State* state, Value* sl, size_t pos)
            {
                ScopeUpvalue* rt;
                rt = Object::make<ScopeUpvalue>(state, Value::OBJT_UPVALUE);
                rt->m_closed = Value::makeNull();
                rt->m_location = *sl;
                rt->m_next = NULL;
                rt->m_stackpos = pos;
                return rt;
            }

        public:
            size_t m_stackpos;
            Value m_closed;
            Value m_location;
            ScopeUpvalue* m_next;

        public:
            ScopeUpvalue() = delete;
            ~ScopeUpvalue() = delete;

    };

    struct FuncCommon: public Object
    {
        public:
            enum FuncType
            {
                FT_ANONYMOUS,
                FT_FUNCTION,
                FT_METHOD,
                FT_INITIALIZER,
                FT_PRIVATE,
                FT_STATIC,
                FT_SCRIPT,
            };

        public:
            template<typename ObjT>
            static ObjT* make(State* state, Value::ObjType ot)
            {
                ObjT* rt;
                rt = Object::make<ObjT>(state, ot);
                return rt;
            }

        public:
            FuncCommon() = delete;
            ~FuncCommon() = delete;

    };

    struct FuncScript: public FuncCommon
    {
        public:
            static FuncScript* make(State* state, Module* mod, FuncCommon::FuncType t)
            {
                FuncScript* rt;
                rt = FuncCommon::make<FuncScript>(state, Value::OBJT_FUNCSCRIPT);
                rt->m_arity = 0;
                rt->m_upvalcount = 0;
                rt->m_isvariadic = false;
                rt->m_name = NULL;
                rt->m_functype = t;
                rt->m_module = mod;
                rt->m_binblob = new Blob(state);
                return rt;
            }

        public:
            FuncCommon::FuncType m_functype;
            size_t m_arity;
            size_t m_upvalcount;
            bool m_isvariadic;
            Blob* m_binblob;
            String* m_name;
            Module* m_module;

        public:
            FuncScript() = delete;
            ~FuncScript() = delete;

            bool destroyThisObject()
            {
                delete m_binblob;
                return true;
            }
    };

    struct FuncClosure: public FuncCommon
    {
        public:
            static FuncClosure* make(State* state, FuncScript* function)
            {
                size_t i;
                FuncClosure* rt;
                ScopeUpvalue** upvals;
                rt = FuncCommon::make<FuncClosure>(state, Value::OBJT_FUNCCLOSURE);
                upvals = (ScopeUpvalue**)State::GC::allocate(state, sizeof(ScopeUpvalue*), function->m_upvalcount);
                for(i = 0; i < function->m_upvalcount; i++)
                {
                    upvals[i] = NULL;
                }
                rt->m_scriptfunc = function;
                rt->m_upvalues = upvals;
                rt->m_upvalcount = function->m_upvalcount;
                return rt;
            }

        public:
            size_t m_upvalcount;
            FuncScript* m_scriptfunc;
            ScopeUpvalue** m_upvalues;

        public:
            FuncClosure() = delete;
            ~FuncClosure() = delete;

            bool destroyThisObject()
            {
                State::GC::release(m_pvm, m_upvalues, sizeof(ScopeUpvalue*) * m_upvalcount);
                /*
                // there may be multiple closures that all reference the same function
                // for this reason, we do not free functions when freeing closures
                */
                return true;
            }
    };

    struct FuncBound: public FuncCommon
    {
        public:
            static FuncBound* make(State* state, Value rec, FuncClosure* mth)
            {
                FuncBound* rt;
                rt = FuncCommon::make<FuncBound>(state, Value::OBJT_FUNCBOUND);
                rt->m_receiver = rec;
                rt->m_method = mth;
                return rt;
            }

        public:
            Value m_receiver;
            FuncClosure* m_method;

        public:
            FuncBound() = delete;
            ~FuncBound() = delete;

            bool destroyThisObject()
            {
                return true;
            }
    };

    struct FuncNative: public FuncCommon
    {
        public:
            static FuncNative* make(State* state, NativeFN function, const char* name, void* uptr)
            {
                FuncNative* rt;
                rt = FuncCommon::make<FuncNative>(state, Value::OBJT_FUNCNATIVE);
                rt->m_natfunc = function;
                rt->m_name = name;
                rt->m_functype = FuncCommon::FT_FUNCTION;
                rt->m_userptr = uptr;
                return rt;
            }

        public:
            FuncCommon::FuncType m_functype;
            const char* m_name;
            NativeFN m_natfunc;
            void* m_userptr;

        public:
            FuncNative() = delete;
            ~FuncNative() = delete;

            bool destroyThisObject()
            {
                return true;
            }
    };

    struct ClassObject: public Object
    {
        public:
            static ClassObject* make(State* state, String* name)
            {
                ClassObject* rt;
                rt = Object::make<ClassObject>(state, Value::OBJT_CLASS);
                rt->m_name = name;
                rt->m_instprops = new HashTable(state);
                rt->m_staticproperties = new HashTable(state);
                rt->m_methods = new HashTable(state);
                rt->m_staticmethods = new HashTable(state);
                rt->m_constructor = Value::makeEmpty();
                rt->m_superclass = NULL;
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
            * copied to ClassInstance::properties.
            * so `$m_instprops["something"] = somefunction;` remains untouched *until* an
            * instance of this class is created.
            */
            HashTable* m_instprops;

            /*
            * static, unchangeable(-ish) values. intended for values that are not unique, but shared
            * across classes, without being copied.
            */
            HashTable* m_staticproperties;

            /*
            * method table; they are currently not unique when instantiated; instead, they are
            * read from $methods as-is. this includes adding methods!
            * TODO: introduce a new hashtable field for ClassInstance for unique methods, perhaps?
            * right now, this method just prevents unnecessary copying.
            */
            HashTable* m_methods;
            HashTable* m_staticmethods;
            String* m_name;
            ClassObject* m_superclass;

        public:
            ClassObject() = delete;
            ~ClassObject() = delete;

            bool destroyThisObject()
            {
                delete m_methods;
                delete m_staticmethods;
                delete m_instprops;
                delete m_staticproperties;
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

            void defCallableField(const char* cstrname, NativeFN function, void* uptr)
            {
                String* oname;
                FuncNative* ofn;
                oname = String::copy(m_pvm, cstrname);
                ofn = FuncNative::make(m_pvm, function, cstrname, uptr);
                m_instprops->setType(Value::fromObject(oname), Value::fromObject(ofn), Property::Type::T_FUNCTION, true);
            }

            void defCallableField(const char* cstrname, NativeFN function)
            {
                return defCallableField(cstrname, function, nullptr);
            }

            void setStaticPropertyCstr(const char* cstrname, Value val)
            {
                m_staticproperties->setCStr(cstrname, val);
            }

            void setStaticProperty(String* name, Value val)
            {
                setStaticPropertyCstr(name->data(), val);
            }

            void defConstructor(NativeFN function, void* uptr)
            {
                const char* cname;
                FuncNative* ofn;
                cname = "constructor";
                ofn = FuncNative::make(m_pvm, function, cname, uptr);
                m_constructor = Value::fromObject(ofn);
            }

            void defConstructor(NativeFN function)
            {
                return defConstructor(function, nullptr);
            }

            void defMethod(const char* name, Value val)
            {
                m_methods->setCStr(name, val);
            }

            void defNativeMethod(const char* name, NativeFN function, void* uptr)
            {
                FuncNative* ofn;
                ofn = FuncNative::make(m_pvm, function, name, uptr);
                defMethod(name, Value::fromObject(ofn));
            }

            void defNativeMethod(const char* name, NativeFN function)
            {
                return defNativeMethod(name, function, nullptr);
            }

            void defStaticNativeMethod(const char* name, NativeFN function, void* uptr)
            {
                FuncNative* ofn;
                ofn = FuncNative::make(m_pvm, function, name, uptr);
                m_staticmethods->setCStr(name, Value::fromObject(ofn));
            }

            void defStaticNativeMethod(const char* name, NativeFN function)
            {
                return defStaticNativeMethod(name, function, nullptr);
            }

            Property* getMethod(String* name)
            {
                Property* field;
                field = m_methods->getField(Value::fromObject(name));
                if(field != NULL)
                {
                    return field;
                }
                if(m_superclass != NULL)
                {
                    return m_superclass->getMethod(name);
                }
                return NULL;
            }

            Property* getProperty(String* name)
            {
                Property* field;
                field = m_instprops->getField(Value::fromObject(name));
                return field;
            }

            Property* getStaticProperty(String* name)
            {
                return m_staticproperties->getFieldByObjStr(name);
            }

            Property* getStaticMethodField(String* name)
            {
                Property* field;
                field = m_staticmethods->getField(Value::fromObject(name));
                return field;
            }
    };

    struct ClassInstance: public Object
    {
        public:
            static ClassInstance* make(State* state, ClassObject* klass)
            {
                ClassInstance* rt;
                rt = Object::make<ClassInstance>(state, Value::OBJT_INSTANCE);
                /* gc fix */
                state->stackPush(Value::fromObject(rt));
                rt->m_active = true;
                rt->m_klass = klass;
                rt->m_properties = new HashTable(state);
                if(klass->m_instprops->m_count > 0)
                {
                    HashTable::copy(klass->m_instprops, rt->m_properties);
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
            ClassObject* m_klass;

        public:
            ClassInstance() = delete;
            ~ClassInstance() = delete;

            bool destroyThisObject()
            {
                delete m_properties;
                m_properties = NULL;
                m_active = false;
                return true;
            }
    };

    struct Range: public Object
    {
        public:
            static Range* make(State* state, int low, int up)
            {
                Range* rt;
                rt = Object::make<Range>(state, Value::OBJT_RANGE);
                rt->m_lower = low;
                rt->m_upper = up;
                if(up > low)
                {
                    rt->m_range = up - low;
                }
                else
                {
                    rt->m_range = low - up;
                }
                return rt;
            }

        public:
            int m_lower;
            int m_upper;
            int m_range;

        public:
            Range() = delete;
            ~Range() = delete;

            bool destroyThisObject()
            {
                return true;
            }
    };

    struct File: public Object
    {
        public:
            struct IOResult
            {
                bool success;
                char* data;
                size_t length;    
            };

        public:
            static File* make(State* state, FILE* hnd, bool iss, const char* pa, const char* mo)
            {
                File* rt;
                rt = Object::make<File>(state, Value::OBJT_FILE);
                rt->m_isopen = false;
                rt->m_fmode = String::copy(state, mo);
                rt->m_fpath = String::copy(state, pa);
                rt->m_isstd = iss;
                rt->m_fhandle = hnd;
                rt->m_istty = false;
                rt->m_fnumber = -1;
                if(rt->m_fhandle != NULL)
                {
                    rt->m_isopen = true;
                }
                return rt;
            }


            static bool exists(const char* filepath)
            {
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

            static bool isFile(const char* filepath)
            {
                (void)filepath;
                return false;
            }

            static bool isDirectory(const char* filepath)
            {
                (void)filepath;
                return false;
            }

            static char* getBasename(const char* path)
            {
                return osfn_basename(path);
            }

        public:
            bool m_isopen;
            bool m_isstd;
            bool m_istty;
            int m_fnumber;
            FILE* m_fhandle;
            String* m_fmode;
            String* m_fpath;

        public:
            File() = delete;
            ~File() = delete;

            bool destroyThisObject()
            {
                return closeFile();
            }

            void mark()
            {
                nn_gcmem_markobject(m_pvm, (Object*)m_fmode);
                nn_gcmem_markobject(m_pvm, (Object*)m_fpath);
            }

            bool closeFile()
            {
                int result;
                if(m_fhandle != NULL && !m_isstd)
                {
                    fflush(m_fhandle);
                    result = fclose(m_fhandle);
                    m_fhandle = NULL;
                    m_isopen = false;
                    m_fnumber = -1;
                    m_istty = false;
                    return result != -1;
                }
                return false;
            }

            bool openFile()
            {
                if(m_fhandle != NULL)
                {
                    return true;
                }
                if(m_fhandle == NULL && !m_isstd)
                {
                    m_fhandle = fopen(m_fpath->data(), m_fmode->data());
                    if(m_fhandle != NULL)
                    {
                        m_isopen = true;
                        m_fnumber = fileno(m_fhandle);
                        m_istty = osfn_isatty(m_fnumber);
                        return true;
                    }
                    else
                    {
                        m_fnumber = -1;
                        m_istty = false;
                    }
                    return false;
                }
                return false;
            }

            bool read(size_t readhowmuch, IOResult* dest)
            {
                size_t filesizereal;
                struct stat stats;
                filesizereal = -1;
                dest->success = false;
                dest->length = 0;
                dest->data = NULL;
                if(!m_isstd)
                {
                    if(!File::exists(m_fpath->data()))
                    {
                        return false;
                    }
                    /* file is in write only mode */
                    /*
                    else if(strstr(m_fmode->data(), "w") != NULL && strstr(m_fmode->data(), "+") == NULL)
                    {
                        FILE_ERROR(Unsupported, "cannot read file in write mode");
                    }
                    */
                    if(!m_isopen)
                    {
                        /* open the file if it isn't open */
                        openFile();
                    }
                    else if(m_fhandle == NULL)
                    {
                        return false;
                    }
                    if(osfn_lstat(m_fpath->data(), &stats) == 0)
                    {
                        filesizereal = (size_t)stats.st_size;
                    }
                    else
                    {
                        /* fallback */
                        fseek(m_fhandle, 0L, SEEK_END);
                        filesizereal = ftell(m_fhandle);
                        rewind(m_fhandle);
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
                dest->data = (char*)State::GC::allocate(m_pvm, sizeof(char), readhowmuch + 1);
                if(dest->data == NULL && readhowmuch != 0)
                {
                    return false;
                }
                dest->length = fread(dest->data, sizeof(char), readhowmuch, m_fhandle);
                if(dest->length == 0 && readhowmuch != 0 && readhowmuch == filesizereal)
                {
                    return false;
                }
                /* we made use of +1 so we can terminate the string. */
                if(dest->data != NULL)
                {
                    dest->data[dest->length] = '\0';
                }
                return true;
            }
    };

    struct Module: public Object
    {
        public:
            static Module* make(State* state, const char* n, const char* f, bool isimp)
            {
                Module* rt;
                rt = Object::make<Module>(state, Value::OBJT_MODULE);
                rt->m_deftable = new HashTable(state);
                rt->m_name = String::copy(state, n);
                rt->m_physpath = String::copy(state, f);
                rt->m_unloader = NULL;
                rt->m_preloader = NULL;
                rt->m_libhandle = NULL;
                rt->m_isimported = isimp;
                return rt;
            }

            static char* resolvePath(State* state, const char* modulename, const char* currentfile, const char* rootfile, bool isrelative)
            {
                size_t i;
                size_t mlen;
                size_t splen;
                char* cbmodfile;
                char* cbselfpath;
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
                pathbuf = NULL;
                mlen = strlen(modulename);
                splen = state->m_modimportpath->m_count;
                for(i=0; i<splen; i++)
                {
                    pitem = state->m_modimportpath->m_values[i].asString();
                    if(pathbuf == NULL)
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
                    if(File::exists(cstrpath))
                    {
                        fprintf(stderr, "found!\n");
                        /* stop a core library from importing itself */
                        #if 0
                        if(stat(currentfile, &stroot) == -1)
                        {
                            fprintf(stderr, "resolvepath: failed to stat current file '%s'\n", currentfile);
                            return NULL;
                        }
                        if(stat(cstrpath, &stmod) == -1)
                        {
                            fprintf(stderr, "resolvepath: failed to stat module file '%s'\n", cstrpath);
                            return NULL;
                        }
                        if(stroot.st_ino == stmod.st_ino)
                        {
                            fprintf(stderr, "resolvepath: refusing to import itself\n");
                            return NULL;
                        }
                        #endif
                        #if 0
                            cbmodfile = osfn_realpath(cstrpath, NULL);
                            cbselfpath = osfn_realpath(currentfile, NULL);
                        #else
                            cbmodfile = Util::strCopy(cstrpath);
                            cbselfpath = Util::strCopy(currentfile);
                        #endif
                        if(cbmodfile != NULL && cbselfpath != NULL)
                        {
                            if(memcmp(cbmodfile, cbselfpath, (int)strlen(cbselfpath)) == 0)
                            {
                                free(cbmodfile);
                                free(cbselfpath);
                                fprintf(stderr, "resolvepath: refusing to import itself\n");
                                return NULL;
                            }
                            free(cbselfpath);
                            delete pathbuf;
                            pathbuf = NULL;
                            retme = Util::strCopy(cbmodfile);
                            free(cbmodfile);
                            return retme;
                        }
                    }
                    else
                    {
                        fprintf(stderr, "does not exist\n");
                    }
                }
                if(pathbuf != NULL)
                {
                    delete pathbuf;
                }
                return NULL;
            }


        public:
            bool m_isimported;
            HashTable* m_deftable;
            String* m_name;
            String* m_physpath;
            void* m_preloader;
            void* m_unloader;
            void* m_libhandle;

        public:
            Module() = delete;
            ~Module() = delete;

            bool destroyThisObject()
            {
                delete m_deftable;
                /*
                free(m_name);
                free(m_physpath);
                */
                if(m_unloader != NULL && m_isimported)
                {
                    ((ModLoaderFN)m_unloader)(m_pvm);
                }
                if(m_libhandle != NULL)
                {
                    nn_import_closemodule(m_libhandle);
                }
                return true;
            }

            Module* loadModule(String* modulename)
            {
                size_t argc;
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
                field = m_pvm->m_loadedmodules->getFieldByObjStr(modulename);
                if(field != NULL)
                {
                    return field->value.asModule();
                }
                physpath = Module::resolvePath(m_pvm, modulename->data(), m_physpath->data(), NULL, false);
                if(physpath == NULL)
                {
                    NEON_RAISEEXCEPTION(m_pvm, "module not found: '%s'\n", modulename->data());
                    return NULL;
                }
                fprintf(stderr, "loading module from '%s'\n", physpath);
                source = Util::fileReadFile(physpath, &fsz);
                if(source == NULL)
                {
                    NEON_RAISEEXCEPTION(m_pvm, "could not read import file %s", physpath);
                    return NULL;
                }
                blob = new Blob(m_pvm);
                module = Module::make(m_pvm, modulename->data(), physpath, true);
                free(physpath);
                function = nn_astparser_compilesource(m_pvm, module, source, blob, true, false);
                free(source);
                closure = FuncClosure::make(m_pvm, function);
                callable = Value::fromObject(closure);
                args = Array::make(m_pvm);
                argc = nn_nestcall_prepare(m_pvm, callable, Value::makeNull(), args);
                if(!nn_nestcall_callfunction(m_pvm, callable, Value::makeNull(), args, &retv))
                {
                    delete blob;
                    NEON_RAISEEXCEPTION(m_pvm, "failed to call compiled import closure");
                    return NULL;
                }
                delete blob;
                return module;
            }
    };


    struct VarSwitch: public Object
    {
        public:
            static VarSwitch* make(State* state)
            {
                VarSwitch* rt;
                rt = Object::make<VarSwitch>(state, Value::OBJT_SWITCH);
                rt->m_jumpvals = new HashTable(state);
                rt->m_defaultjump = -1;
                rt->m_exitjump = -1;
                return rt;
            }

        public:
            int m_defaultjump;
            int m_exitjump;
            HashTable* m_jumpvals;

        public:
            VarSwitch() = delete;
            ~VarSwitch() = delete;

    };

    struct UserData: public Object
    {
        public:
            static UserData* make(State* state, void* p, const char* n)
            {
                UserData* rt;
                rt = Object::make<UserData>(state, Value::OBJT_USERDATA);
                rt->m_pointer = p;
                rt->m_name = Util::strCopy(n);
                rt->m_ondestroyfn = NULL;
                return rt;
            }


        public:
            void* m_pointer;
            char* m_name;
            PtrFreeFN m_ondestroyfn;

        public:
            UserData() = delete;
            ~UserData() = delete;

            bool destroyThisObject()
            {
                if(m_ondestroyfn)
                {
                    m_ondestroyfn(m_pointer);
                }
                return true;
            }
    };

    struct FormatInfo
    {
        public:
            State* m_pvm = nullptr;
            /* destination printer */
            Printer* m_printer = nullptr;
            /* the actual format string */
            const char* m_fmtstr = nullptr;
            /* length of the format string */
            size_t m_fmtlen = 0;

        public:
            FormatInfo() = delete;

            FormatInfo(State* state, Printer* pr, const char* fstr, size_t flen):
                m_pvm(state), m_printer(pr), m_fmtstr(fstr), m_fmtlen(flen)
            {
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
                while(i < m_fmtlen)
                {
                    ch = m_fmtstr[i];
                    nextch = -1;
                    if((i + 1) < m_fmtlen)
                    {
                        nextch = m_fmtstr[i+1];
                    }
                    i++;
                    if(ch == '%')
                    {
                        if(nextch == '%')
                        {
                            m_printer->putChar('%');
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
                                        nn_printer_printvalue(m_printer, cval, true, true);
                                    }
                                    break;
                                case 'c':
                                    {
                                        ival = (int)cval.asNumber();
                                        m_printer->format("%c", ival);
                                    }
                                    break;
                                /* TODO: implement actual field formatting */
                                case 's':
                                case 'd':
                                case 'i':
                                case 'g':
                                    {
                                        nn_printer_printvalue(m_printer, cval, false, true);
                                    }
                                    break;
                                default:
                                    {
                                        NEON_RAISEEXCEPTION(m_pvm, "unknown/invalid format flag '%%c'", nextch);
                                    }
                                    break;
                            }
                        }
                    }
                    else
                    {
                        m_printer->putChar(ch);
                    }
                }
                return failed;
            }


    };

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
            static const char* tokenTypeName(int t)
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
                    case TOK_GREATER_EQ: return "TOK_GREATER_EQ";
                    case TOK_RIGHTSHIFT: return "TOK_RIGHTSHIFT";
                    case TOK_RIGHTSHIFTASSIGN: return "TOK_RIGHTSHIFTASSIGN";
                    case TOK_MODULO: return "TOK_MODULO";
                    case TOK_PERCENT_EQ: return "TOK_PERCENT_EQ";
                    case TOK_AMP: return "TOK_AMP";
                    case TOK_AMP_EQ: return "TOK_AMP_EQ";
                    case TOK_BAR: return "TOK_BAR";
                    case TOK_BAR_EQ: return "TOK_BAR_EQ";
                    case TOK_TILDE: return "TOK_TILDE";
                    case TOK_TILDE_EQ: return "TOK_TILDE_EQ";
                    case TOK_XOR: return "TOK_XOR";
                    case TOK_XOR_EQ: return "TOK_XOR_EQ";
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

    struct AstLexer
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

        public:
            State* m_pvm;
            const char* m_sourcestart;
            const char* m_sourceptr;
            int m_sourceline;
            int m_tplstringcount;
            int m_tplstringbuffer[NEON_CFG_ASTMAXSTRTPLDEPTH];

        public:
            AstLexer(State* state, const char* source)
            {
                NEON_ASTDEBUG(state, "");
                m_pvm = state;
                m_sourceptr = source;
                m_sourcestart = source;
                m_sourceline = 1;
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
                t.start = m_sourcestart;
                t.length = (int)(m_sourceptr - m_sourcestart);
                t.line = m_sourceline;
                return t;
            }

            template<typename... ArgsT>
            AstToken errorToken(const char* fmt, ArgsT&&... args)
            {
                static auto fn_sprintf = sprintf;
                int length;
                char* buf;
                AstToken t;
                buf = (char*)State::GC::allocate(m_pvm, sizeof(char), 1024);
                /* TODO: used to be vasprintf. need to check how much to actually allocate! */
                length = fn_sprintf(buf, fmt, args...);
                t.type = AstToken::TOK_ERROR;
                t.start = buf;
                t.isglobal = false;
                if(buf != NULL)
                {
                    t.length = length;
                }
                else
                {
                    t.length = 0;
                }
                t.line = m_sourceline;
                return t;
            }

            char advance()
            {
                m_sourceptr++;
                if(m_sourceptr[-1] == '\n')
                {
                    m_sourceline++;
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
                    m_sourceline++;
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

            AstToken skipBlockComments()
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
                return makeToken(AstToken::TOK_UNDEFINED);
            }

            AstToken skipSpace()
            {
                char c;
                AstToken result;
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
                                m_sourceline++;
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
                                return makeToken(AstToken::TOK_UNDEFINED);
                            }
                            else if(peekNext() == '*')
                            {
                                advance();
                                advance();
                                result = skipBlockComments();
                                if(result.type != AstToken::TOK_UNDEFINED)
                                {
                                    return result;
                                }
                                break;
                            }
                            else
                            {
                                return makeToken(AstToken::TOK_UNDEFINED);
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
                return makeToken(AstToken::TOK_UNDEFINED);
            }

            AstToken scanString(char quote, bool withtemplate)
            {
                AstToken tkn;
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
                                tkn = makeToken(AstToken::TOK_INTERPOLATION);
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
                return makeToken(AstToken::TOK_LITERAL);
            }

            AstToken scanNumber()
            {
                NEON_ASTDEBUG(m_pvm, "");
                /* handle binary, octal and hexadecimals */
                if(peekPrevious() == '0')
                {
                    /* binary number */
                    if(match('b'))
                    {
                        while(AstLexer::charIsBinary(peekCurrent()))
                        {
                            advance();
                        }
                        return makeToken(AstToken::TOK_LITNUMBIN);
                    }
                    else if(match('c'))
                    {
                        while(AstLexer::charIsOctal(peekCurrent()))
                        {
                            advance();
                        }
                        return makeToken(AstToken::TOK_LITNUMOCT);
                    }
                    else if(match('x'))
                    {
                        while(AstLexer::charIsHexadecimal(peekCurrent()))
                        {
                            advance();
                        }
                        return makeToken(AstToken::TOK_LITNUMHEX);
                    }
                }
                while(AstLexer::charIsDigit(peekCurrent()))
                {
                    advance();
                }
                /* dots(.) are only valid here when followed by a digit */
                if(peekCurrent() == '.' && AstLexer::charIsDigit(peekNext()))
                {
                    advance();
                    while(AstLexer::charIsDigit(peekCurrent()))
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
                        while(AstLexer::charIsDigit(peekCurrent()))
                        {
                            advance();
                        }
                    }
                }
                return makeToken(AstToken::TOK_LITNUMREG);
            }

            AstToken::Type getIdentType()
            {
                static const struct
                {
                    const char* str;
                    int tokid;
                }
                keywords[] =
                {
                    { "and", AstToken::TOK_KWAND },
                    { "assert", AstToken::TOK_KWASSERT },
                    { "as", AstToken::TOK_KWAS },
                    { "break", AstToken::TOK_KWBREAK },
                    { "catch", AstToken::TOK_KWCATCH },
                    { "class", AstToken::TOK_KWCLASS },
                    { "continue", AstToken::TOK_KWCONTINUE },
                    { "default", AstToken::TOK_KWDEFAULT },
                    { "def", AstToken::TOK_KWFUNCTION },
                    { "function", AstToken::TOK_KWFUNCTION },
                    { "throw", AstToken::TOK_KWTHROW },
                    { "do", AstToken::TOK_KWDO },
                    { "echo", AstToken::TOK_KWECHO },
                    { "else", AstToken::TOK_KWELSE },
                    { "empty", AstToken::TOK_KWEMPTY },
                    { "false", AstToken::TOK_KWFALSE },
                    { "finally", AstToken::TOK_KWFINALLY },
                    { "foreach", AstToken::TOK_KWFOREACH },
                    { "if", AstToken::TOK_KWIF },
                    { "import", AstToken::TOK_KWIMPORT },
                    { "in", AstToken::TOK_KWIN },
                    { "for", AstToken::TOK_KWFOR },
                    { "null", AstToken::TOK_KWNULL },
                    { "new", AstToken::TOK_KWNEW },
                    { "or", AstToken::TOK_KWOR },
                    { "super", AstToken::TOK_KWSUPER },
                    { "return", AstToken::TOK_KWRETURN },
                    { "this", AstToken::TOK_KWTHIS },
                    { "static", AstToken::TOK_KWSTATIC },
                    { "true", AstToken::TOK_KWTRUE },
                    { "try", AstToken::TOK_KWTRY },
                    { "typeof", AstToken::TOK_KWTYPEOF },
                    { "switch", AstToken::TOK_KWSWITCH },
                    { "case", AstToken::TOK_KWCASE },
                    { "var", AstToken::TOK_KWVAR },
                    { "while", AstToken::TOK_KWWHILE },
                    { NULL, (AstToken::Type)0 }
                };
                size_t i;
                size_t kwlen;
                size_t ofs;
                const char* kwtext;
                for(i = 0; keywords[i].str != NULL; i++)
                {
                    kwtext = keywords[i].str;
                    kwlen = strlen(kwtext);
                    ofs = (m_sourceptr - m_sourcestart);
                    if(ofs == kwlen)
                    {
                        if(memcmp(m_sourcestart, kwtext, kwlen) == 0)
                        {
                            return (AstToken::Type)keywords[i].tokid;
                        }
                    }
                }
                return AstToken::TOK_IDENTNORMAL;
            }

            AstToken scanIdent(bool isdollar)
            {
                int cur;
                AstToken tok;
                cur = peekCurrent();
                if(cur == '$')
                {
                    advance();
                }
                while(true)
                {
                    cur = peekCurrent();
                    if(AstLexer::charIsAlpha(cur) || AstLexer::charIsDigit(cur))
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

            AstToken scanDecorator()
            {
                while(AstLexer::charIsAlpha(peekCurrent()) || AstLexer::charIsDigit(peekCurrent()))
                {
                    advance();
                }
                return makeToken(AstToken::TOK_DECORATOR);
            }

            AstToken scanToken()
            {
                char c;
                bool isdollar;
                AstToken tk;
                AstToken token;
                tk = skipSpace();
                if(tk.type != AstToken::TOK_UNDEFINED)
                {
                    return tk;
                }
                m_sourcestart = m_sourceptr;
                if(isAtEnd())
                {
                    return makeToken(AstToken::TOK_EOF);
                }
                c = advance();
                if(AstLexer::charIsDigit(c))
                {
                    return scanNumber();
                }
                else if(AstLexer::charIsAlpha(c) || (c == '$'))
                {
                    isdollar = (c == '$');
                    return scanIdent(isdollar);
                }
                switch(c)
                {
                    case '(':
                        {
                            return makeToken(AstToken::TOK_PARENOPEN);
                        }
                        break;
                    case ')':
                        {
                            return makeToken(AstToken::TOK_PARENCLOSE);
                        }
                        break;
                    case '[':
                        {
                            return makeToken(AstToken::TOK_BRACKETOPEN);
                        }
                        break;
                    case ']':
                        {
                            return makeToken(AstToken::TOK_BRACKETCLOSE);
                        }
                        break;
                    case '{':
                        {
                            return makeToken(AstToken::TOK_BRACEOPEN);
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
                            return makeToken(AstToken::TOK_BRACECLOSE);
                        }
                        break;
                    case ';':
                        {
                            return makeToken(AstToken::TOK_SEMICOLON);
                        }
                        break;
                    case '\\':
                        {
                            return makeToken(AstToken::TOK_BACKSLASH);
                        }
                        break;
                    case ':':
                        {
                            return makeToken(AstToken::TOK_COLON);
                        }
                        break;
                    case ',':
                        {
                            return makeToken(AstToken::TOK_COMMA);
                        }
                        break;
                    case '@':
                        {
                            if(!AstLexer::charIsAlpha(peekCurrent()))
                            {
                                return makeToken(AstToken::TOK_AT);
                            }
                            return scanDecorator();
                        }
                        break;
                    case '!':
                        {
                            if(match('='))
                            {
                                return makeToken(AstToken::TOK_NOTEQUAL);
                            }
                            return makeToken(AstToken::TOK_EXCLMARK);

                        }
                        break;
                    case '.':
                        {
                            if(match('.'))
                            {
                                if(match('.'))
                                {
                                    return makeToken(AstToken::TOK_TRIPLEDOT);
                                }
                                return makeToken(AstToken::TOK_DOUBLEDOT);
                            }
                            return makeToken(AstToken::TOK_DOT);
                        }
                        break;
                    case '+':
                    {
                        if(match('+'))
                        {
                            return makeToken(AstToken::TOK_INCREMENT);
                        }
                        if(match('='))
                        {
                            return makeToken(AstToken::TOK_PLUSASSIGN);
                        }
                        else
                        {
                            return makeToken(AstToken::TOK_PLUS);
                        }
                    }
                    break;
                    case '-':
                        {
                            if(match('-'))
                            {
                                return makeToken(AstToken::TOK_DECREMENT);
                            }
                            if(match('='))
                            {
                                return makeToken(AstToken::TOK_MINUSASSIGN);
                            }
                            else
                            {
                                return makeToken(AstToken::TOK_MINUS);
                            }
                        }
                        break;
                    case '*':
                        {
                            if(match('*'))
                            {
                                if(match('='))
                                {
                                    return makeToken(AstToken::TOK_POWASSIGN);
                                }
                                return makeToken(AstToken::TOK_POWEROF);
                            }
                            else
                            {
                                if(match('='))
                                {
                                    return makeToken(AstToken::TOK_MULTASSIGN);
                                }
                                return makeToken(AstToken::TOK_MULTIPLY);
                            }
                        }
                        break;
                    case '/':
                        {
                            if(match('='))
                            {
                                return makeToken(AstToken::TOK_DIVASSIGN);
                            }
                            return makeToken(AstToken::TOK_DIVIDE);
                        }
                        break;
                    case '=':
                        {
                            if(match('='))
                            {
                                return makeToken(AstToken::TOK_EQUAL);
                            }
                            return makeToken(AstToken::TOK_ASSIGN);
                        }        
                        break;
                    case '<':
                        {
                            if(match('<'))
                            {
                                if(match('='))
                                {
                                    return makeToken(AstToken::TOK_LEFTSHIFTASSIGN);
                                }
                                return makeToken(AstToken::TOK_LEFTSHIFT);
                            }
                            else
                            {
                                if(match('='))
                                {
                                    return makeToken(AstToken::TOK_LESSEQUAL);
                                }
                                return makeToken(AstToken::TOK_LESSTHAN);

                            }
                        }
                        break;
                    case '>':
                        {
                            if(match('>'))
                            {
                                if(match('='))
                                {
                                    return makeToken(AstToken::TOK_RIGHTSHIFTASSIGN);
                                }
                                return makeToken(AstToken::TOK_RIGHTSHIFT);
                            }
                            else
                            {
                                if(match('='))
                                {
                                    return makeToken(AstToken::TOK_GREATER_EQ);
                                }
                                return makeToken(AstToken::TOK_GREATERTHAN);
                            }
                        }
                        break;
                    case '%':
                        {
                            if(match('='))
                            {
                                return makeToken(AstToken::TOK_PERCENT_EQ);
                            }
                            return makeToken(AstToken::TOK_MODULO);
                        }
                        break;
                    case '&':
                        {
                            if(match('&'))
                            {
                                return makeToken(AstToken::TOK_KWAND);
                            }
                            else if(match('='))
                            {
                                return makeToken(AstToken::TOK_AMP_EQ);
                            }
                            return makeToken(AstToken::TOK_AMP);
                        }
                        break;
                    case '|':
                        {
                            if(match('|'))
                            {
                                return makeToken(AstToken::TOK_KWOR);
                            }
                            else if(match('='))
                            {
                                return makeToken(AstToken::TOK_BAR_EQ);
                            }
                            return makeToken(AstToken::TOK_BAR);
                        }
                        break;
                    case '~':
                        {
                            if(match('='))
                            {
                                return makeToken(AstToken::TOK_TILDE_EQ);
                            }
                            return makeToken(AstToken::TOK_TILDE);
                        }
                        break;
                    case '^':
                        {
                            if(match('='))
                            {
                                return makeToken(AstToken::TOK_XOR_EQ);
                            }
                            return makeToken(AstToken::TOK_XOR);
                        }
                        break;
                    case '\n':
                        {
                            return makeToken(AstToken::TOK_NEWLINE);
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
                            return makeToken(AstToken::TOK_QUESTION);
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
                return errorToken("unexpected character %c", c);
            }
    };

    static const char* g_strthis = "this";
    static const char* g_strsuper = "super";

    struct AstParser
    {
        public:
            struct CompiledLocal
            {
                bool iscaptured;
                int depth;
                AstToken name;
            };

            struct CompiledUpvalue
            {
                bool islocal;
                uint16_t index;
            };

            struct ClassCompiler
            {
                bool hassuperclass;
                ClassCompiler* enclosing;
                AstToken name;
            };

            struct FuncCompiler
            {
                public:
                    int m_localcount = 0;
                    int m_scopedepth = 0;
                    int m_compiledexhandlercount = 0;
                    bool m_funcfromimport = false;
                    FuncCompiler* m_enclosingfunccompiler = nullptr;
                    /* current function */
                    FuncScript* m_targetfunc = nullptr;
                    FuncCommon::FuncType m_type;
                    CompiledLocal m_compiledlocals[NEON_CFG_ASTMAXLOCALS];
                    CompiledUpvalue m_compiledupvalues[NEON_CFG_ASTMAXUPVALS];

                public:
                    FuncCompiler(AstParser* prs, FuncCommon::FuncType type, bool isanon)
                    {
                        bool candeclthis;
                        Printer wtmp;
                        CompiledLocal* local;
                        String* fname;
                        m_enclosingfunccompiler = prs->m_currfunccompiler;
                        m_targetfunc = NULL;
                        m_type = type;
                        m_localcount = 0;
                        m_scopedepth = 0;
                        m_compiledexhandlercount = 0;
                        m_funcfromimport = false;
                        m_targetfunc = FuncScript::make(prs->m_pvm, prs->m_currentmodule, type);
                        prs->m_currfunccompiler = this;
                        if(type != FuncCommon::FT_SCRIPT)
                        {
                            prs->m_pvm->stackPush(Value::fromObject(m_targetfunc));
                            if(isanon)
                            {
                                Printer::makeStackString(prs->m_pvm, &wtmp);
                                wtmp.format("anonymous@[%s:%d]", prs->m_currentfile, prs->m_prevtoken.line);
                                fname = wtmp.takeString();
                            }
                            else
                            {
                                fname = String::copy(prs->m_pvm, prs->m_prevtoken.start, prs->m_prevtoken.length);
                            }
                            prs->m_currfunccompiler->m_targetfunc->m_name = fname;
                            prs->m_pvm->stackPop();
                        }
                        /* claiming slot zero for use in class methods */
                        local = &prs->m_currfunccompiler->m_compiledlocals[0];
                        prs->m_currfunccompiler->m_localcount++;
                        local->depth = 0;
                        local->iscaptured = false;
                        candeclthis = (
                            (type != FuncCommon::FT_FUNCTION) &&
                            (prs->m_compcontext == NEON_COMPCONTEXT_CLASS)
                        );
                        if(candeclthis || (/*(type == FuncCommon::FT_ANONYMOUS) &&*/ (prs->m_compcontext != NEON_COMPCONTEXT_CLASS)))
                        {
                            local->name.start = g_strthis;
                            local->name.length = 4;
                        }
                        else
                        {
                            local->name.start = "";
                            local->name.length = 0;
                        }
                    }

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
            AstCompContext m_compcontext;
            const char* m_currentfile;
            State* m_pvm;
            AstLexer* m_lexer;
            AstToken m_currtoken;
            AstToken m_prevtoken;
            ClassCompiler* m_currentclasscompiler;
            Module* m_currentmodule;        
            FuncCompiler* m_currfunccompiler;


        public:
            AstParser(State* state, AstLexer* lexer, Module* mod, bool keeplast)
            {
                NEON_ASTDEBUG(state, "");
                m_pvm = state;
                m_lexer = lexer;
                m_haderror = false;
                m_panicmode = false;
                m_blockcount = 0;
                m_replcanecho = false;
                m_isreturning = false;
                m_istrying = false;
                m_currfunccompiler = nullptr;
                m_compcontext = NEON_COMPCONTEXT_NONE;
                m_innermostloopstart = -1;
                m_innermostloopscopedepth = 0;
                m_currentclasscompiler = NULL;
                m_currentmodule = mod;
                m_keeplastvalue = keeplast;
                m_lastwasstatement = false;
                m_infunction = false;
                m_currentfile = m_currentmodule->m_physpath->data();
            }

            ~AstParser()
            {
            }


            template<typename... ArgsT>
            bool raiseErrorAt(AstToken* t, const char* message, ArgsT&&... args)
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
                if(t->type == AstToken::TOK_EOF)
                {
                    fprintf(stderr, " at end");
                }
                else if(t->type == AstToken::TOK_ERROR)
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
                fprintf(stderr, "  %s:%d\n", m_currentmodule->m_physpath->data(), t->line);
                m_haderror = true;
                return false;
            }

            template<typename... ArgsT>
            bool raiseErrorHere(const char* message, ArgsT&&... args)
            {
                return raiseErrorAt(&m_currtoken, message, args...);
            }

            template<typename... ArgsT>
            bool raiseError(const char* message, ArgsT&&... args)
            {
                return raiseErrorAt(&m_prevtoken, message, args...);
            }

            Blob* currentBlob()
            {
                return m_currfunccompiler->m_targetfunc->m_binblob;
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

            void emitInstruc(uint8_t byte)
            {
                emit(byte, m_prevtoken.line, true);
            }

            void emitByte(uint8_t byte)
            {
                emit(byte, m_prevtoken.line, false);
            }

            void emitShort(uint16_t byte)
            {
                emit((byte >> 8) & 0xff, m_prevtoken.line, false);
                emit(byte & 0xff, m_prevtoken.line, false);
            }

            void emitBytes(uint8_t byte, uint8_t byte2)
            {
                emit(byte, m_prevtoken.line, false);
                emit(byte2, m_prevtoken.line, false);
            }

            void emitInstrucAndShort(uint8_t op, uint16_t byte)
            {
                emit(op, m_prevtoken.line, false);
                emit((byte >> 8) & 0xff, m_prevtoken.line, false);
                emit(byte & 0xff, m_prevtoken.line, false);
            }

            void emitLoop(int loopstart)
            {
                int offset;
                emitInstruc(Instruction::OP_LOOP);
                offset = currentBlob()->m_count - loopstart + 2;
                if(offset > UINT16_MAX)
                {
                    raiseError("loop body too large");
                }
                emitByte((offset >> 8) & 0xff);
                emitByte(offset & 0xff);
            }

            void emitReturn()
            {
                if(m_istrying)
                {
                    emitInstruc(Instruction::OP_EXPOPTRY);
                }
                if(m_currfunccompiler->m_type == FuncCommon::FT_INITIALIZER)
                {
                    emitInstrucAndShort(Instruction::OP_LOCALGET, 0);
                }
                else
                {
                    if(!m_keeplastvalue || m_lastwasstatement)
                    {
                        if(m_currfunccompiler->m_funcfromimport)
                        {
                            emitInstruc(Instruction::OP_PUSHNULL);
                        }
                        else
                        {
                            emitInstruc(Instruction::OP_PUSHEMPTY);
                        }
                    }
                }
                emitInstruc(Instruction::OP_RETURN);
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
                emitInstrucAndShort(Instruction::OP_PUSHCONSTANT, (uint16_t)constant);
            }

            int emitJump(uint8_t instruction)
            {
                emitInstruc(instruction);
                /* placeholders */
                emitByte(0xff);
                emitByte(0xff);
                return currentBlob()->m_count - 2;
            }

            int emitSwitch()
            {
                emitInstruc(Instruction::OP_SWITCH);
                /* placeholders */
                emitByte(0xff);
                emitByte(0xff);
                return currentBlob()->m_count - 2;
            }

            int emitTry()
            {
                emitInstruc(Instruction::OP_EXTRY);
                /* type placeholders */
                emitByte(0xff);
                emitByte(0xff);
                /* handler placeholders */
                emitByte(0xff);
                emitByte(0xff);
                /* finally placeholders */
                emitByte(0xff);
                emitByte(0xff);
                return currentBlob()->m_count - 6;
            }

            void patchSwitch(int offset, int constant)
            {
                patchAt(offset, (constant >> 8) & 0xff);
                patchAt(offset + 1, constant & 0xff);
            }

            void patchTry(int offset, int type, int address, int finally)
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

            void patchJump(int offset)
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

    };

    struct AstRule
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

        public:
            AstParsePrefixFN prefix;
            AstParseInfixFN infix;
            Precedence precedence;
    };

    struct ModExport
    {
        public:
            struct FuncInfo
            {
                const char* name;
                bool isstatic;
                NativeFN function;
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
            Value failSrcPos(const char* srcfile, int srcline, const char* fmt, ArgsT&&... args)
            {
                m_pvm->stackPop(this->argc);
                m_pvm->raiseClass(m_pvm->m_exceptions.argumenterror, srcfile, srcline, fmt, args...);
                return Value::makeBool(false);
            }

            //std::source_location location = std::source_location::current()
            template<typename... ArgsT>
            Value failHere(const char* fmt, ArgsT&&... args)
            {
                return failSrcPos("(unknown)", 0, fmt, args...);
            }
            
    };
}


#include "prot.inc"


neon::HashTable::Entry* neon::HashTable::findEntryByStr(neon::HashTable::Entry* availents, size_t availcap, neon::Value valkey, const char* kstr, size_t klen, uint32_t hash)
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
    tombstone = NULL;
    while(true)
    {
        entry = &availents[index];
        if(entry->key.isEmpty())
        {
            if(entry->value.value.isNull())
            {
                /* empty entry */
                if(tombstone != NULL)
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
                if(tombstone == NULL)
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
    return NULL;
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
        return NULL;
    }
    index = hash & (m_capacity - 1);
    while(true)
    {
        entry = &m_entries[index];
        if(entry->key.isEmpty())
        {
            /*
            // stop if we find an empty non-tombstone entry
            //if (entry->value.isNull())
            */
            {
                return NULL;
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
    size_t i;
    neon::HashTable::Entry* entry;
    pd->format("<HashTable of %s : {\n", name);
    for(i = 0; i < m_capacity; i++)
    {
        entry = &m_entries[i];
        if(!entry->key.isEmpty())
        {
            nn_printer_printvalue(pd, entry->key, true, true);
            pd->format(": ");
            nn_printer_printvalue(pd, entry->value.value, true, true);
            if(i != m_capacity - 1)
            {
                pd->format(",\n");
            }
        }
    }
    pd->format("}>\n");
}

neon::Value neon::State::vmReadConst()
{
    uint16_t idx;
    idx = vmReadShort();
    return m_vmstate.currentframe->closure->m_scriptfunc->m_binblob->m_constants->m_values[idx];
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
    function = frame->closure->m_scriptfunc;
    instruction = frame->inscode - function->m_binblob->m_instrucs - 1;
    line = function->m_binblob->m_instrucs[instruction].srcline;
    fprintf(stderr, "RuntimeError: ");
    fn_fprintf(stderr, format, args...);
    fprintf(stderr, " -> %s:%d ", function->m_module->m_physpath->data(), line);
    fputs("\n", stderr);
    if(m_vmstate.framecount > 1)
    {
        fprintf(stderr, "stacktrace:\n");
        for(i = m_vmstate.framecount - 1; i >= 0; i--)
        {
            frame = &m_vmstate.framevalues[i];
            function = frame->closure->m_scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->m_binblob->m_instrucs - 1;
            fprintf(stderr, "    %s:%d -> ", function->m_module->m_physpath->data(), function->m_binblob->m_instrucs[instruction].srcline);
            if(function->m_name == NULL)
            {
                fprintf(stderr, "<script>");
            }
            else
            {
                fprintf(stderr, "%s()", function->m_name->data());
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
    needed = fn_snprintf(NULL, 0, format, args...);
    needed += 1;
    message = (char*)malloc(needed+1);
    length = fn_snprintf(message, needed, format, args...);
    instance = nn_exceptions_makeinstance(this, exklass, srcfile, srcline, neon::String::take(this, message, length));
    stackPush(neon::Value::fromObject(instance));
    stacktrace = nn_exceptions_getstacktrace(this);
    stackPush(stacktrace);
    nn_instance_defproperty(instance, "stacktrace", stacktrace);
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
    colred = nc.color(neon::Color::C_RED);
    colreset = nc.color(neon::Color::C_RESET);
    colyellow = nc.color(neon::Color::C_YELLOW);
    /* at this point, the exception is unhandled; so, print it out. */
    fprintf(stderr, "%sunhandled %s%s", colred, exception->m_klass->m_name->data(), colreset);
    srcfile = "none";
    srcline = 0;
    field = exception->m_properties->getFieldByCStr("srcline");
    if(field != NULL)
    {
        srcline = field->value.asNumber();
    }
    field = exception->m_properties->getFieldByCStr("srcfile");
    if(field != NULL)
    {
        srcfile = field->value.asString()->data();
    }
    fprintf(stderr, " [from native %s%s:%d%s]", colyellow, srcfile, srcline, colreset);
    
    field = exception->m_properties->getFieldByCStr("message");
    if(field != NULL)
    {
        emsg = nn_value_tostring(this, field->value);
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
    if(field != NULL)
    {
        fprintf(stderr, "  stacktrace:\n");
        oa = field->value.asArray();
        cnt = oa->m_varray->m_count;
        i = cnt-1;
        if(cnt > 0)
        {
            while(true)
            {
                stackitm = oa->at(i);
                m_debugprinter->format("  ");
                nn_printer_printvalue(m_debugprinter, stackitm, false, true);
                m_debugprinter->format("\n");
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
            function = m_vmstate.currentframe->closure->m_scriptfunc;
            if(handler->address != 0 && nn_util_isinstanceof(exception->m_klass, handler->klass))
            {
                m_vmstate.currentframe->inscode = &function->m_binblob->m_instrucs[handler->address];
                return true;
            }
            else if(handler->finallyaddress != 0)
            {
                /* continue propagating once the 'finally' block completes */
                stackPush(neon::Value::makeBool(true));
                m_vmstate.currentframe->inscode = &function->m_binblob->m_instrucs[handler->finallyaddress];
                return true;
            }
        }
        m_vmstate.framecount--;
    }
    return exceptionHandleUncaught(exception);
}



void nn_gcmem_markobject(neon::State* state, neon::Object* object)
{
    if(object == NULL)
    {
        return;
    }
    if(object->m_mark == state->m_markvalue)
    {
        return;
    }
    #if 0
    if(NEON_UNLIKELY(state->m_conf.m_debuggc))
    {
        state->m_debugprinter->format("GC: marking object at <%p> ", (void*)object);
        nn_printer_printvalue(state->m_debugprinter, neon::Value::fromObject(object), false, false);
        state->m_debugprinter->format("\n");
    }
    #endif
    object->m_mark = state->m_markvalue;
    if(state->m_gcstate.graycapacity < state->m_gcstate.graycount + 1)
    {
        state->m_gcstate.graycapacity = GROW_CAPACITY(state->m_gcstate.graycapacity);
        state->m_gcstate.graystack = (neon::Object**)realloc(state->m_gcstate.graystack, sizeof(neon::Object*) * state->m_gcstate.graycapacity);
        if(state->m_gcstate.graystack == NULL)
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
    #if 0
    if(NEON_UNLIKELY(state->m_conf.m_debuggc))
    {
        state->m_debugprinter->format("GC: blacken object at <%p> ", (void*)object);
        nn_printer_printvalue(state->m_debugprinter, neon::Value::fromObject(object), false, false);
        state->m_debugprinter->format("\n");
    }
    #endif
    switch(object->m_objtype)
    {
        case neon::Value::OBJT_MODULE:
            {
                neon::Module* module;
                module = (neon::Module*)object;
                neon::HashTable::mark(state, module->m_deftable);
            }
            break;
        case neon::Value::OBJT_SWITCH:
            {
                neon::VarSwitch* sw;
                sw = (neon::VarSwitch*)object;
                neon::HashTable::mark(state, sw->m_jumpvals);
            }
            break;
        case neon::Value::OBJT_FILE:
            {
                neon::File* file;
                file = (neon::File*)object;
                file->mark();
            }
            break;
        case neon::Value::OBJT_DICT:
            {
                neon::Dictionary* dict;
                dict = (neon::Dictionary*)object;
                dict->mark();
            }
            break;
        case neon::Value::OBJT_ARRAY:
            {
                neon::Array* list;
                list = (neon::Array*)object;
                list->m_varray->gcMark();
            }
            break;
        case neon::Value::OBJT_FUNCBOUND:
            {
                neon::FuncBound* bound;
                bound = (neon::FuncBound*)object;
                nn_gcmem_markvalue(state, bound->m_receiver);
                nn_gcmem_markobject(state, (neon::Object*)bound->m_method);
            }
            break;
        case neon::Value::OBJT_CLASS:
            {
                neon::ClassObject* klass;
                klass = (neon::ClassObject*)object;
                nn_gcmem_markobject(state, (neon::Object*)klass->m_name);
                neon::HashTable::mark(state, klass->m_methods);
                neon::HashTable::mark(state, klass->m_staticmethods);
                neon::HashTable::mark(state, klass->m_staticproperties);
                nn_gcmem_markvalue(state, klass->m_constructor);
                if(klass->m_superclass != NULL)
                {
                    nn_gcmem_markobject(state, (neon::Object*)klass->m_superclass);
                }
            }
            break;
        case neon::Value::OBJT_FUNCCLOSURE:
            {
                size_t i;
                neon::FuncClosure* closure;
                closure = (neon::FuncClosure*)object;
                nn_gcmem_markobject(state, (neon::Object*)closure->m_scriptfunc);
                for(i = 0; i < closure->m_upvalcount; i++)
                {
                    nn_gcmem_markobject(state, (neon::Object*)closure->m_upvalues[i]);
                }
            }
            break;
        case neon::Value::OBJT_FUNCSCRIPT:
            {
                neon::FuncScript* function;
                function = (neon::FuncScript*)object;
                nn_gcmem_markobject(state, (neon::Object*)function->m_name);
                nn_gcmem_markobject(state, (neon::Object*)function->m_module);
                function->m_binblob->m_constants->gcMark();
            }
            break;
        case neon::Value::OBJT_INSTANCE:
            {
                neon::ClassInstance* instance;
                instance = (neon::ClassInstance*)object;
                nn_instance_mark(state, instance);
            }
            break;
        case neon::Value::OBJT_UPVALUE:
            {
                nn_gcmem_markvalue(state, ((neon::ScopeUpvalue*)object)->m_closed);
            }
            break;
        case neon::Value::OBJT_RANGE:
        case neon::Value::OBJT_FUNCNATIVE:
        case neon::Value::OBJT_USERDATA:
        case neon::Value::OBJT_STRING:
            break;
    }
}

void nn_gcmem_destroyobject(neon::State* state, neon::Object* object)
{
    (void)state;
    #if 0
    if(NEON_UNLIKELY(state->m_conf.m_debuggc))
    {
        state->m_debugprinter->format("GC: freeing at <%p> of type %d\n", (void*)object, object->m_objtype);
    }
    #endif
    if(object->m_isstale)
    {
        return;
    }
    switch(object->m_objtype)
    {
        case neon::Value::OBJT_MODULE:
            {
                neon::Module* module;
                module = (neon::Module*)object;
                neon::Object::releaseObject(state, module);

            }
            break;
        case neon::Value::OBJT_FILE:
            {
                neon::File* file;
                file = (neon::File*)object;
                neon::Object::releaseObject(state, file);
            }
            break;
        case neon::Value::OBJT_DICT:
            {
                neon::Dictionary* dict;
                dict = (neon::Dictionary*)object;
                neon::Object::releaseObject(state, dict);
            }
            break;
        case neon::Value::OBJT_ARRAY:
            {
                neon::Array* list;
                list = (neon::Array*)object;
                neon::Object::releaseObject(state, list);
            }
            break;
        case neon::Value::OBJT_FUNCBOUND:
            {
                neon::FuncBound* bound;
                bound = (neon::FuncBound*)object;
                /*
                // a closure may be bound to multiple instances
                // for this reason, we do not free closures when freeing bound methods
                */
                neon::Object::releaseObject(state, bound);
            }
            break;
        case neon::Value::OBJT_CLASS:
            {
                neon::ClassObject* klass;
                klass = (neon::ClassObject*)object;
                neon::Object::releaseObject(state, klass);
            }
            break;
        case neon::Value::OBJT_FUNCCLOSURE:
            {
                neon::FuncClosure* closure;
                closure = (neon::FuncClosure*)object;
                neon::Object::releaseObject(state, closure);
            }
            break;
        case neon::Value::OBJT_FUNCSCRIPT:
            {
                neon::FuncScript* function;
                function = (neon::FuncScript*)object;
                neon::Object::releaseObject(state, function);
            }
            break;
        case neon::Value::OBJT_INSTANCE:
            {
                neon::ClassInstance* instance;
                instance = (neon::ClassInstance*)object;
                neon::Object::releaseObject(state, instance);
            }
            break;
        case neon::Value::OBJT_FUNCNATIVE:
            {
                neon::FuncNative* native;
                native = (neon::FuncNative*)object;
                neon::Object::releaseObject(state, native);
            }
            break;
        case neon::Value::OBJT_UPVALUE:
            {
                neon::ScopeUpvalue* upv;
                upv = (neon::ScopeUpvalue*)object;
                neon::Object::releaseObject(state, upv);
            }
            break;
        case neon::Value::OBJT_RANGE:
            {
                neon::Range* rng;
                rng = (neon::Range*)object;
                neon::Object::releaseObject(state, rng);
            }
            break;
        case neon::Value::OBJT_STRING:
            {
                neon::String* string;
                string = (neon::String*)object;
                neon::Object::releaseObject(state, string);
            }
            break;
        case neon::Value::OBJT_SWITCH:
            {
                neon::VarSwitch* sw;
                sw = (neon::VarSwitch*)object;
                delete sw->m_jumpvals;
                neon::Object::releaseObject(state, sw);
            }
            break;
        case neon::Value::OBJT_USERDATA:
            {
                neon::UserData* uptr;
                uptr = (neon::UserData*)object;
                neon::Object::releaseObject(state, uptr);
            }
            break;
        default:
            break;
    }
}

void nn_gcmem_markroots(neon::State* state)
{
    size_t i;
    size_t j;
    neon::Value* slot;
    neon::ScopeUpvalue* upvalue;
    neon::State::ExceptionFrame* handler;
    for(slot = state->m_vmstate.stackvalues; slot < &state->m_vmstate.stackvalues[state->m_vmstate.stackidx]; slot++)
    {
        nn_gcmem_markvalue(state, *slot);
    }
    for(i = 0; i < state->m_vmstate.framecount; i++)
    {
        nn_gcmem_markobject(state, (neon::Object*)state->m_vmstate.framevalues[i].closure);
        for(j = 0; j < state->m_vmstate.framevalues[i].handlercount; j++)
        {
            handler = &state->m_vmstate.framevalues[i].handlers[j];
            nn_gcmem_markobject(state, (neon::Object*)handler->klass);
        }
    }
    for(upvalue = state->m_vmstate.openupvalues; upvalue != NULL; upvalue = upvalue->m_next)
    {
        nn_gcmem_markobject(state, (neon::Object*)upvalue);
    }
    neon::HashTable::mark(state, state->m_definedglobals);
    neon::HashTable::mark(state, state->m_loadedmodules);
    nn_gcmem_markobject(state, (neon::Object*)state->m_exceptions.stdexception);
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
    previous = NULL;
    object = state->m_vmstate.linkedobjects;
    while(object != NULL)
    {
        if(object->m_mark == state->m_markvalue)
        {
            previous = object;
            object = object->m_nextobj;
        }
        else
        {
            unreached = object;
            object = object->m_nextobj;
            if(previous != NULL)
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
    while(object != NULL)
    {
        next = object->m_nextobj;
        nn_gcmem_destroyobject(state, object);
        object = next;
    }
    free(state->m_gcstate.graystack);
    state->m_gcstate.graystack = NULL;
}


void neon::State::GC::collectGarbage(neon::State* state)
{
    size_t before;
    (void)before;
    #if 0
    if(NEON_UNLIKELY(state->m_conf.m_debuggc))
    {
        state->m_debugprinter->format("GC: gc begins\n");
        before = state->m_gcstate.bytesallocated;
    }
    #endif
    /*
    //  REMOVE THE NEXT LINE TO DISABLE NESTED collectGarbage() POSSIBILITY!
    */
    #if 1
    state->m_gcstate.nextgc = state->m_gcstate.bytesallocated;
    #endif
    nn_gcmem_markroots(state);
    nn_gcmem_tracerefs(state);
    neon::HashTable::removeMarked(state, state->m_cachedstrings);
    neon::HashTable::removeMarked(state, state->m_loadedmodules);
    nn_gcmem_sweep(state);
    state->m_gcstate.nextgc = state->m_gcstate.bytesallocated * NEON_CFG_GCHEAPGROWTHFACTOR;
    state->m_markvalue = !state->m_markvalue;
    #if 0
    if(NEON_UNLIKELY(state->m_conf.m_debuggc))
    {
        state->m_debugprinter->format("GC: gc ends\n");
        state->m_debugprinter->format("GC: collected %zu bytes (from %zu to %zu), next at %zu\n", before - state->m_gcstate.bytesallocated, before, state->m_gcstate.bytesallocated, state->m_gcstate.nextgc);
    }
    #endif
}


void nn_dbg_disasmblob(neon::Printer* pd, neon::Blob* blob, const char* name)
{
    size_t offset;
    pd->format("== compiled '%s' [[\n", name);
    for(offset = 0; offset < blob->m_count;)
    {
        offset = nn_dbg_printinstructionat(pd, blob, offset);
    }
    pd->format("]]\n");
}

void nn_dbg_printinstrname(neon::Printer* pd, const char* name)
{
    neon::Color nc;
    pd->format("%s%-16s%s ", nc.color(neon::Color::C_RED), name, nc.color(neon::Color::C_RESET));
}

int nn_dbg_printsimpleinstr(neon::Printer* pd, const char* name, int offset)
{
    nn_dbg_printinstrname(pd, name);
    pd->format("\n");
    return offset + 1;
}

int nn_dbg_printconstinstr(neon::Printer* pd, const char* name, neon::Blob* blob, int offset)
{
    uint16_t constant;
    constant = (blob->m_instrucs[offset + 1].code << 8) | blob->m_instrucs[offset + 2].code;
    nn_dbg_printinstrname(pd, name);
    pd->format("%8d ", constant);
    nn_printer_printvalue(pd, blob->m_constants->m_values[constant], true, false);
    pd->format("\n");
    return offset + 3;
}

int nn_dbg_printpropertyinstr(neon::Printer* pd, const char* name, neon::Blob* blob, int offset)
{
    const char* proptn;
    uint16_t constant;
    constant = (blob->m_instrucs[offset + 1].code << 8) | blob->m_instrucs[offset + 2].code;
    nn_dbg_printinstrname(pd, name);
    pd->format("%8d ", constant);
    nn_printer_printvalue(pd, blob->m_constants->m_values[constant], true, false);
    proptn = "";
    if(blob->m_instrucs[offset + 3].code == 1)
    {
        proptn = "static";
    }
    pd->format(" (%s)", proptn);
    pd->format("\n");
    return offset + 4;
}

int nn_dbg_printshortinstr(neon::Printer* pd, const char* name, neon::Blob* blob, int offset)
{
    uint16_t slot;
    slot = (blob->m_instrucs[offset + 1].code << 8) | blob->m_instrucs[offset + 2].code;
    nn_dbg_printinstrname(pd, name);
    pd->format("%8d\n", slot);
    return offset + 3;
}

int nn_dbg_printbyteinstr(neon::Printer* pd, const char* name, neon::Blob* blob, int offset)
{
    uint8_t slot;
    slot = blob->m_instrucs[offset + 1].code;
    nn_dbg_printinstrname(pd, name);
    pd->format("%8d\n", slot);
    return offset + 2;
}

int nn_dbg_printjumpinstr(neon::Printer* pd, const char* name, int sign, neon::Blob* blob, int offset)
{
    uint16_t jump;
    jump = (uint16_t)(blob->m_instrucs[offset + 1].code << 8);
    jump |= blob->m_instrucs[offset + 2].code;
    nn_dbg_printinstrname(pd, name);
    pd->format("%8d -> %d\n", offset, offset + 3 + sign * jump);
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
    pd->format("%8d -> %d, %d\n", type, address, finally);
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
    pd->format("(%d args) %8d ", argcount, constant);
    nn_printer_printvalue(pd, blob->m_constants->m_values[constant], true, false);
    pd->format("\n");
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
    size_t j;
    int islocal;
    uint16_t index;
    uint16_t constant;
    const char* locn;
    neon::FuncScript* function;
    offset++;
    constant = blob->m_instrucs[offset++].code << 8;
    constant |= blob->m_instrucs[offset++].code;
    pd->format("%-16s %8d ", name, constant);
    nn_printer_printvalue(pd, blob->m_constants->m_values[constant], true, false);
    pd->format("\n");
    function = blob->m_constants->m_values[constant].asFuncScript();
    for(j = 0; j < function->m_upvalcount; j++)
    {
        islocal = blob->m_instrucs[offset++].code;
        index = blob->m_instrucs[offset++].code << 8;
        index |= blob->m_instrucs[offset++].code;
        locn = "upvalue";
        if(islocal)
        {
            locn = "local";
        }
        pd->format("%04d      |                     %s %d\n", offset - 3, locn, (int)index);
    }
    return offset;
}

int nn_dbg_printinstructionat(neon::Printer* pd, neon::Blob* blob, int offset)
{
    uint8_t instruction;
    const char* opname;
    pd->format("%08d ", offset);
    if(offset > 0 && blob->m_instrucs[offset].srcline == blob->m_instrucs[offset - 1].srcline)
    {
        pd->format("       | ");
    }
    else
    {
        pd->format("%8d ", blob->m_instrucs[offset].srcline);
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
                pd->format("unknown opcode %d\n", instruction);
            }
            break;
    }
    return offset + 1;
}


neon::String* neon::Printer::takeString()
{
    //uint32_t hash;
    neon::String* os;
    //hash = neon::Util::hashString(m_strbuf->data, m_strbuf->length);
    //os = neon::String::makeBuffer(m_pvm, m_strbuf, hash);
    //static String* strMakeHashedBuffer(State* state, const char* estr, size_t elen, uint32_t hash, bool istaking)
    //os = neon::String::strMakeHashedBuffer(state, );
    //m_stringtaken = true;
    os = neon::String::copy(m_pvm, m_strbuf->data, m_strbuf->length);
    return os;
}

void nn_printer_printfunction(neon::Printer* pd, neon::FuncScript* func)
{
    if(func->m_name == NULL)
    {
        pd->format("<script at %p>", (void*)func);
    }
    else
    {
        if(func->m_isvariadic)
        {
            pd->format("<function %s(%d...) at %p>", func->m_name->data(), func->m_arity, (void*)func);
        }
        else
        {
            pd->format("<function %s(%d) at %p>", func->m_name->data(), func->m_arity, (void*)func);
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
    vsz = list->count();
    pd->format("[");
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
            pd->format("<recursion>");
        }
        else
        {
            nn_printer_printvalue(pd, val, true, true);
        }
        if(i != vsz - 1)
        {
            pd->format(", ");
        }
        if(pd->m_shortenvalues && (i >= pd->m_maxvallength))
        {
            pd->format(" [%zd items]", vsz);
            break;
        }
    }
    pd->format("]");
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
    dsz = dict->m_names->m_count;
    pd->format("{");
    for(i = 0; i < dsz; i++)
    {
        valisrecur = false;
        keyisrecur = false;
        val = dict->m_names->m_values[i];
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
            pd->format("<recursion>");
        }
        else
        {
            nn_printer_printvalue(pd, val, true, true);
        }
        pd->format(": ");
        field = dict->m_htab->getField(dict->m_names->m_values[i]);
        if(field != NULL)
        {
            if(field->value.isDict())
            {
                subdict = field->value.asDict();
                if(subdict == dict)
                {
                    keyisrecur = true;
                }
            }
            if(keyisrecur)
            {
                pd->format("<recursion>");
            }
            else
            {
                nn_printer_printvalue(pd, field->value, true, true);
            }
        }
        if(i != dsz - 1)
        {
            pd->format(", ");
        }
        if(pd->m_shortenvalues && (pd->m_maxvallength >= i))
        {
            pd->format(" [%zd items]", dsz);
            break;
        }
    }
    pd->format("}");
}

void nn_printer_printfile(neon::Printer* pd, neon::File* file)
{
    pd->format("<file at %s in mode %s>", file->m_fpath->data(), file->m_fmode->data());
}

void nn_printer_printinstance(neon::Printer* pd, neon::ClassInstance* instance, bool invmethod)
{
    (void)invmethod;
    #if 0
    size_t arity;
    neon::Printer subw;
    neon::Value resv;
    neon::Value thisval;
    neon::Property* field;
    neon::State* state;
    neon::String* os;
    neon::Array* args;
    state = pd->m_pvm;
    if(invmethod)
    {
        field = instance->m_klass->m_methods->getFieldByCStr("toString");
        if(field != NULL)
        {
            args = neon::Array::make(state);
            thisval = neon::Value::fromObject(instance);
            arity = nn_nestcall_prepare(state, field->value, thisval, args);
            fprintf(stderr, "arity = %d\n", arity);
            state->stackPop();
            state->stackPush(thisval);
            if(nn_nestcall_callfunction(state, field->value, thisval, args, &resv))
            {
                neon::Printer::makeStackString(state, &subw);
                nn_printer_printvalue(&subw, resv, false, false);
                os = subw.takeString();
                pd->put(os->data(), os->length());
                //state->stackPop();
                return;
            }
        }
    }
    #endif
    pd->format("<instance of %s at %p>", instance->m_klass->m_name->data(), (void*)instance);
}

void nn_printer_printobject(neon::Printer* pd, neon::Value value, bool fixstring, bool invmethod)
{
    neon::Object* obj;
    obj = value.asObject();
    switch(obj->m_objtype)
    {
        case neon::Value::OBJT_SWITCH:
            {
                pd->put("<switch>");
            }
            break;
        case neon::Value::OBJT_USERDATA:
            {
                pd->format("<userdata %s>", value.asUserdata()->m_name);
            }
            break;
        case neon::Value::OBJT_RANGE:
            {
                neon::Range* range;
                range = value.asRange();
                pd->format("<range %d .. %d>", range->m_lower, range->m_upper);
            }
            break;
        case neon::Value::OBJT_FILE:
            {
                nn_printer_printfile(pd, value.asFile());
            }
            break;
        case neon::Value::OBJT_DICT:
            {
                nn_printer_printdict(pd, value.asDict());
            }
            break;
        case neon::Value::OBJT_ARRAY:
            {
                nn_printer_printarray(pd, value.asArray());
            }
            break;
        case neon::Value::OBJT_FUNCBOUND:
            {
                neon::FuncBound* bn;
                bn = value.asFuncBound();
                nn_printer_printfunction(pd, bn->m_method->m_scriptfunc);
            }
            break;
        case neon::Value::OBJT_MODULE:
            {
                neon::Module* mod;
                mod = value.asModule();
                pd->format("<module '%s' at '%s'>", mod->m_name->data(), mod->m_physpath->data());
            }
            break;
        case neon::Value::OBJT_CLASS:
            {
                neon::ClassObject* klass;
                klass = value.asClass();
                pd->format("<class %s at %p>", klass->m_name->data(), (void*)klass);
            }
            break;
        case neon::Value::OBJT_FUNCCLOSURE:
            {
                neon::FuncClosure* cls;
                cls = value.asFuncClosure();
                nn_printer_printfunction(pd, cls->m_scriptfunc);
            }
            break;
        case neon::Value::OBJT_FUNCSCRIPT:
            {
                neon::FuncScript* fn;
                fn = value.asFuncScript();
                nn_printer_printfunction(pd, fn);
            }
            break;
        case neon::Value::OBJT_INSTANCE:
            {
                /* @TODO: support the toString() override */
                neon::ClassInstance* instance;
                instance = value.asInstance();
                nn_printer_printinstance(pd, instance, invmethod);
            }
            break;
        case neon::Value::OBJT_FUNCNATIVE:
            {
                neon::FuncNative* native;
                native = value.asFuncNative();
                pd->format("<function %s(native) at %p>", native->m_name, (void*)native);
            }
            break;
        case neon::Value::OBJT_UPVALUE:
            {
                pd->format("<upvalue>");
            }
            break;
        case neon::Value::OBJT_STRING:
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
        case neon::Value::Type::T_EMPTY:
            {
                pd->put("<empty>");
            }
            break;
        case neon::Value::Type::T_NULL:
            {
                pd->put("null");
            }
            break;
        case neon::Value::Type::T_BOOL:
            {
                pd->put(value.asBool() ? "true" : "false");
            }
            break;
        case neon::Value::Type::T_NUMBER:
            {
                pd->format("%.16g", value.asNumber());
            }
            break;
        case neon::Value::Type::T_OBJECT:
            {
                nn_printer_printobject(pd, value, fixstring, invmethod);
            }
            break;
        default:
            break;
    }
}

neon::String* nn_value_tostring(neon::State* state, neon::Value value)
{
    neon::Printer pd;
    neon::String* s;
    neon::Printer::makeStackString(state, &pd);
    nn_printer_printvalue(&pd, value, false, true);
    s = pd.takeString();
    return s;
}

const char* nn_value_objecttypename(neon::Object* object)
{
    switch(object->m_objtype)
    {
        case neon::Value::OBJT_MODULE:
            return "module";
        case neon::Value::OBJT_RANGE:
            return "range";
        case neon::Value::OBJT_FILE:
            return "file";
        case neon::Value::OBJT_DICT:
            return "dictionary";
        case neon::Value::OBJT_ARRAY:
            return "array";
        case neon::Value::OBJT_CLASS:
            return "class";
        case neon::Value::OBJT_FUNCSCRIPT:
        case neon::Value::OBJT_FUNCNATIVE:
        case neon::Value::OBJT_FUNCCLOSURE:
        case neon::Value::OBJT_FUNCBOUND:
            return "function";
        case neon::Value::OBJT_INSTANCE:
            return ((neon::ClassInstance*)object)->m_klass->m_name->data();
        case neon::Value::OBJT_STRING:
            return "string";
        case neon::Value::OBJT_USERDATA:
            return "userdata";
        case neon::Value::OBJT_SWITCH:
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
        if(ta == neon::Value::OBJT_STRING)
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
        if(ta == neon::Value::OBJT_ARRAY)
        {
            arra = (neon::Array*)oa;
            arrb = (neon::Array*)ob;
            if(arra->count() == arrb->count())
            {
                for(i=0; i<(size_t)arra->count(); i++)
                {
                    if(!nn_value_compare(state, arra->at(i), arrb->at(i)))
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
        case neon::Value::Type::T_NULL:
        case neon::Value::Type::T_EMPTY:
            {
                return true;
            }
            break;
        case neon::Value::Type::T_BOOL:
            {
                return a.asBool() == b.asBool();
            }
            break;
        case neon::Value::Type::T_NUMBER:
            {
                return (a.asNumber() == b.asNumber());
            }
            break;
        case neon::Value::Type::T_OBJECT:
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


uint32_t nn_object_hashobject(neon::Object* object)
{
    switch(object->m_objtype)
    {
        case neon::Value::OBJT_CLASS:
            {
                /* Classes just use their name. */
                return ((neon::ClassObject*)object)->m_name->m_hash;
            }
            break;
        case neon::Value::OBJT_FUNCSCRIPT:
            {
                /*
                // Allow bare (non-closure) functions so that we can use a map to find
                // existing constants in a function's constant table. This is only used
                // internally. Since user code never sees a non-closure function, they
                // cannot use them as map keys.
                */
                neon::FuncScript* fn;
                fn = (neon::FuncScript*)object;
                return neon::Util::hashDouble(fn->m_arity) ^ neon::Util::hashDouble(fn->m_binblob->m_count);
            }
            break;
        case neon::Value::OBJT_STRING:
            {
                return ((neon::String*)object)->m_hash;
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
        case neon::Value::Type::T_BOOL:
            return value.asBool() ? 3 : 5;
        case neon::Value::Type::T_NULL:
            return 7;
        case neon::Value::Type::T_NUMBER:
            return neon::Util::hashDouble(value.asNumber());
        case neon::Value::Type::T_OBJECT:
            return nn_object_hashobject(value.asObject());
        default:
            /* neon::Value::Type::T_EMPTY */
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
            if(a.asFuncClosure()->m_scriptfunc->m_arity >= b.asFuncClosure()->m_scriptfunc->m_arity)
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
            if(a.asArray()->count() >= b.asArray()->count())
            {
                return a;
            }
            return b;
        }
        else if(a.isDict() && b.isDict())
        {
            if(a.asDict()->m_names->m_count >= b.asDict()->m_names->m_count)
            {
                return a;
            }
            return b;
        }
        else if(a.isFile() && b.isFile())
        {
            if(strcmp(a.asFile()->m_fpath->data(), b.asFile()->m_fpath->data()) >= 0)
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
void nn_value_sortvalues(neon::State* state, neon::Value* values, size_t count)
{
    size_t i;
    size_t j;
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
                    nn_value_sortvalues(state, values[i].asArray()->m_varray->m_values, values[i].asArray()->count());
                }

                if(values[j].isArray())
                {
                    nn_value_sortvalues(state, values[j].asArray()->m_varray->m_values, values[j].asArray()->count());
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
            case neon::Value::OBJT_STRING:
                {
                    neon::String* string;
                    string = value.asString();
                    return neon::Value::fromObject(neon::String::copy(state, string->data(), string->length()));
                }
                break;
            case neon::Value::OBJT_ARRAY:
            {
                size_t i;
                neon::Array* list;
                neon::Array* newlist;
                list = value.asArray();
                newlist = neon::Array::make(state);
                state->stackPush(neon::Value::fromObject(newlist));
                for(i = 0; i < list->count(); i++)
                {
                    newlist->m_varray->push(list->at(i));
                }
                state->stackPop();
                return neon::Value::fromObject(newlist);
            }
            /*
            case neon::Value::OBJT_DICT:
                {
                    neon::Dictionary *dict;
                    neon::Dictionary *newdict;
                    dict = value.asDict();
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

void nn_module_setfilefield(neon::State* state, neon::Module* module)
{
    return;
    module->m_deftable->setCStr("__file__", neon::Value::fromObject(neon::String::copyObjString(state, module->m_physpath)));
}


void nn_instance_mark(neon::State* state, neon::ClassInstance* instance)
{
    if(instance->m_active == false)
    {
        state->warn("trying to mark inactive instance <%p>!", instance);
        return;
    }
    nn_gcmem_markobject(state, (neon::Object*)instance->m_klass);
    neon::HashTable::mark(state, instance->m_properties);
}


void nn_instance_defproperty(neon::ClassInstance* instance, const char *cstrname, neon::Value val)
{
    instance->m_properties->setCStr(cstrname, val);
}

void nn_astparser_advance(neon::AstParser* prs)
{
    prs->m_prevtoken = prs->m_currtoken;
    while(true)
    {
        prs->m_currtoken = prs->m_lexer->scanToken();
        if(prs->m_currtoken.type != neon::AstToken::TOK_ERROR)
        {
            break;
        }
        prs->raiseErrorHere(prs->m_currtoken.start);
    }
}

bool nn_astparser_consume(neon::AstParser* prs, neon::AstToken::Type t, const char* message)
{
    if(prs->m_currtoken.type == t)
    {
        nn_astparser_advance(prs);
        return true;
    }
    return prs->raiseErrorHere(message);
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
    prs->raiseErrorHere(message);
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
        if(!nn_astparser_check(prs, neon::AstToken::TOK_NEWLINE) && prs->m_currfunccompiler->m_scopedepth == 0)
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
                return 2 + (fn->m_upvalcount * 3);
            }
            break;
        default:
            break;
    }
    return 0;
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
    return prs->pushConst(neon::Value::fromObject(str));
}

bool nn_astparser_identsequal(neon::AstToken* a, neon::AstToken* b)
{
    return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
}

int nn_astfunccompiler_resolvelocal(neon::AstParser* prs, neon::AstParser::FuncCompiler* compiler, neon::AstToken* name)
{
    int i;
    neon::AstParser::CompiledLocal* local;
    for(i = compiler->m_localcount - 1; i >= 0; i--)
    {
        local = &compiler->m_compiledlocals[i];
        if(nn_astparser_identsequal(&local->name, name))
        {
            if(local->depth == -1)
            {
                prs->raiseError("cannot read local variable in it's own initializer");
            }
            return i;
        }
    }
    return -1;
}

int nn_astfunccompiler_addupvalue(neon::AstParser* prs, neon::AstParser::FuncCompiler* compiler, uint16_t index, bool islocal)
{
    int i;
    int upcnt;
    neon::AstParser::CompiledUpvalue* upvalue;
    upcnt = compiler->m_targetfunc->m_upvalcount;
    for(i = 0; i < upcnt; i++)
    {
        upvalue = &compiler->m_compiledupvalues[i];
        if(upvalue->index == index && upvalue->islocal == islocal)
        {
            return i;
        }
    }
    if(upcnt == NEON_CFG_ASTMAXUPVALS)
    {
        prs->raiseError("too many closure variables in function");
        return 0;
    }
    compiler->m_compiledupvalues[upcnt].islocal = islocal;
    compiler->m_compiledupvalues[upcnt].index = index;
    return compiler->m_targetfunc->m_upvalcount++;
}

int nn_astfunccompiler_resolveupvalue(neon::AstParser* prs, neon::AstParser::FuncCompiler* compiler, neon::AstToken* name)
{
    int local;
    int upvalue;
    if(compiler->m_enclosingfunccompiler == NULL)
    {
        return -1;
    }
    local = nn_astfunccompiler_resolvelocal(prs, compiler->m_enclosingfunccompiler, name);
    if(local != -1)
    {
        compiler->m_enclosingfunccompiler->m_compiledlocals[local].iscaptured = true;
        return nn_astfunccompiler_addupvalue(prs, compiler, (uint16_t)local, true);
    }
    upvalue = nn_astfunccompiler_resolveupvalue(prs, compiler->m_enclosingfunccompiler, name);
    if(upvalue != -1)
    {
        return nn_astfunccompiler_addupvalue(prs, compiler, (uint16_t)upvalue, false);
    }
    return -1;
}

int nn_astparser_addlocal(neon::AstParser* prs, neon::AstToken name)
{
    neon::AstParser::CompiledLocal* local;
    if(prs->m_currfunccompiler->m_localcount == NEON_CFG_ASTMAXLOCALS)
    {
        /* we've reached maximum local variables per scope */
        prs->raiseError("too many local variables in scope");
        return -1;
    }
    local = &prs->m_currfunccompiler->m_compiledlocals[prs->m_currfunccompiler->m_localcount++];
    local->name = name;
    local->depth = -1;
    local->iscaptured = false;
    return prs->m_currfunccompiler->m_localcount;
}

void nn_astparser_declarevariable(neon::AstParser* prs)
{
    int i;
    neon::AstToken* name;
    neon::AstParser::CompiledLocal* local;
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
        if(nn_astparser_identsequal(name, &local->name))
        {
            prs->raiseError("%.*s already declared in current scope", name->length, name->start);
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
    if(prs->m_currfunccompiler->m_scopedepth > 0)
    {
        return 0;
    }
    return nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
}

void nn_astparser_markinitialized(neon::AstParser* prs)
{
    if(prs->m_currfunccompiler->m_scopedepth == 0)
    {
        return;
    }
    prs->m_currfunccompiler->m_compiledlocals[prs->m_currfunccompiler->m_localcount - 1].depth = prs->m_currfunccompiler->m_scopedepth;
}

void nn_astparser_definevariable(neon::AstParser* prs, int global)
{
    /* we are in a local scope... */
    if(prs->m_currfunccompiler->m_scopedepth > 0)
    {
        nn_astparser_markinitialized(prs);
        return;
    }
    prs->emitInstrucAndShort(neon::Instruction::OP_GLOBALDEFINE, global);
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
    prs->emitReturn();
    function = prs->m_currfunccompiler->m_targetfunc;
    fname = NULL;
    if(function->m_name == NULL)
    {
        fname = prs->m_currentmodule->m_physpath->data();
    }
    else
    {
        fname = function->m_name->data();
    }
    if(!prs->m_haderror && prs->m_pvm->m_conf.dumpbytecode)
    {
        nn_dbg_disasmblob(prs->m_pvm->m_debugprinter, prs->currentBlob(), fname);
    }
    NEON_ASTDEBUG(prs->m_pvm, "for function '%s'", fname);
    prs->m_currfunccompiler = prs->m_currfunccompiler->m_enclosingfunccompiler;
    return function;
}

void nn_astparser_scopebegin(neon::AstParser* prs)
{
    NEON_ASTDEBUG(prs->m_pvm, "current depth=%d", prs->m_currfunccompiler->m_scopedepth);
    prs->m_currfunccompiler->m_scopedepth++;
}

bool nn_astutil_scopeendcancontinue(neon::AstParser* prs)
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

void nn_astparser_scopeend(neon::AstParser* prs)
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
            prs->emitInstruc(neon::Instruction::OP_UPVALUECLOSE);
        }
        else
        {
            prs->emitInstruc(neon::Instruction::OP_POPONE);
        }
        prs->m_currfunccompiler->m_localcount--;
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
    if(prs->m_currfunccompiler->m_scopedepth == -1)
    {
        prs->raiseError("cannot exit top-level scope");
    }
    local = prs->m_currfunccompiler->m_localcount - 1;
    while(local >= 0 && prs->m_currfunccompiler->m_compiledlocals[local].depth >= depth)
    {
        if(prs->m_currfunccompiler->m_compiledlocals[local].iscaptured)
        {
            prs->emitInstruc(neon::Instruction::OP_UPVALUECLOSE);
        }
        else
        {
            prs->emitInstruc(neon::Instruction::OP_POPONE);
        }
        local--;
    }
    return prs->m_currfunccompiler->m_localcount - local - 1;
}

void nn_astparser_endloop(neon::AstParser* prs)
{
    size_t i;
    neon::Instruction* bcode;
    neon::Value* cvals;
    NEON_ASTDEBUG(prs->m_pvm, "");
    /*
    // find all neon::Instruction::OP_BREAK_PL placeholder and replace with the appropriate jump...
    */
    i = prs->m_innermostloopstart;
    while(i < prs->m_currfunccompiler->m_targetfunc->m_binblob->m_count)
    {
        if(prs->m_currfunccompiler->m_targetfunc->m_binblob->m_instrucs[i].code == neon::Instruction::OP_BREAK_PL)
        {
            prs->m_currfunccompiler->m_targetfunc->m_binblob->m_instrucs[i].code = neon::Instruction::OP_JUMPNOW;
            prs->patchJump(i + 1);
            i += 3;
        }
        else
        {
            bcode = prs->m_currfunccompiler->m_targetfunc->m_binblob->m_instrucs;
            cvals = prs->m_currfunccompiler->m_targetfunc->m_binblob->m_constants->m_values;
            i += 1 + nn_astparser_getcodeargscount(bcode, cvals, i);
        }
    }
}

bool nn_astparser_rulebinary(neon::AstParser* prs, neon::AstToken previous, bool canassign)
{
    neon::AstToken::Type op;
    neon::AstRule* rule;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    op = prs->m_prevtoken.type;
    /* compile the right operand */
    rule = nn_astparser_getrule(op);
    nn_astparser_parseprecedence(prs, (neon::AstRule::Precedence)(rule->precedence + 1));
    /* emit the operator instruction */
    switch(op)
    {
        case neon::AstToken::TOK_PLUS:
            prs->emitInstruc(neon::Instruction::OP_PRIMADD);
            break;
        case neon::AstToken::TOK_MINUS:
            prs->emitInstruc(neon::Instruction::OP_PRIMSUBTRACT);
            break;
        case neon::AstToken::TOK_MULTIPLY:
            prs->emitInstruc(neon::Instruction::OP_PRIMMULTIPLY);
            break;
        case neon::AstToken::TOK_DIVIDE:
            prs->emitInstruc(neon::Instruction::OP_PRIMDIVIDE);
            break;
        case neon::AstToken::TOK_MODULO:
            prs->emitInstruc(neon::Instruction::OP_PRIMMODULO);
            break;
        case neon::AstToken::TOK_POWEROF:
            prs->emitInstruc(neon::Instruction::OP_PRIMPOW);
            break;
        case neon::AstToken::TOK_FLOOR:
            prs->emitInstruc(neon::Instruction::OP_PRIMFLOORDIVIDE);
            break;
            /* equality */
        case neon::AstToken::TOK_EQUAL:
            prs->emitInstruc(neon::Instruction::OP_EQUAL);
            break;
        case neon::AstToken::TOK_NOTEQUAL:
            prs->emitInstruc(neon::Instruction::OP_EQUAL);
            prs->emitInstruc(neon::Instruction::OP_PRIMNOT);
            break;
        case neon::AstToken::TOK_GREATERTHAN:
            prs->emitInstruc(neon::Instruction::OP_PRIMGREATER);
            break;
        case neon::AstToken::TOK_GREATER_EQ:
            prs->emitInstruc(neon::Instruction::OP_PRIMLESSTHAN);
            prs->emitInstruc(neon::Instruction::OP_PRIMNOT);
            break;
        case neon::AstToken::TOK_LESSTHAN:
            prs->emitInstruc(neon::Instruction::OP_PRIMLESSTHAN);
            break;
        case neon::AstToken::TOK_LESSEQUAL:
            prs->emitInstruc(neon::Instruction::OP_PRIMGREATER);
            prs->emitInstruc(neon::Instruction::OP_PRIMNOT);
            break;
            /* bitwise */
        case neon::AstToken::TOK_AMP:
            prs->emitInstruc(neon::Instruction::OP_PRIMAND);
            break;
        case neon::AstToken::TOK_BAR:
            prs->emitInstruc(neon::Instruction::OP_PRIMOR);
            break;
        case neon::AstToken::TOK_XOR:
            prs->emitInstruc(neon::Instruction::OP_PRIMBITXOR);
            break;
        case neon::AstToken::TOK_LEFTSHIFT:
            prs->emitInstruc(neon::Instruction::OP_PRIMSHIFTLEFT);
            break;
        case neon::AstToken::TOK_RIGHTSHIFT:
            prs->emitInstruc(neon::Instruction::OP_PRIMSHIFTRIGHT);
            break;
            /* range */
        case neon::AstToken::TOK_DOUBLEDOT:
            prs->emitInstruc(neon::Instruction::OP_MAKERANGE);
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
    prs->emitBytes(neon::Instruction::OP_CALLFUNCTION, argcount);
    return true;
}

bool nn_astparser_ruleliteral(neon::AstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    switch(prs->m_prevtoken.type)
    {
        case neon::AstToken::TOK_KWNULL:
            prs->emitInstruc(neon::Instruction::OP_PUSHNULL);
            break;
        case neon::AstToken::TOK_KWTRUE:
            prs->emitInstruc(neon::Instruction::OP_PUSHTRUE);
            break;
        case neon::AstToken::TOK_KWFALSE:
            prs->emitInstruc(neon::Instruction::OP_PUSHFALSE);
            break;
        default:
            /* TODO: assuming this is correct behaviour ... */
            return false;
    }
    return true;
}

void nn_astparser_parseassign(neon::AstParser* prs, neon::Instruction::OpCode realop, uint8_t getop, uint8_t setop, int arg)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    prs->m_replcanecho = false;
    if(getop == neon::Instruction::OP_PROPERTYGET || getop == neon::Instruction::OP_PROPERTYGETSELF)
    {
        prs->emitInstruc(neon::Instruction::OP_DUPONE);
    }
    if(arg != -1)
    {
        prs->emitInstrucAndShort(getop, arg);
    }
    else
    {
        prs->emitBytes(getop, 1);
    }
    nn_astparser_parseexpression(prs);
    prs->emitInstruc(realop);
    if(arg != -1)
    {
        prs->emitInstrucAndShort(setop, (uint16_t)arg);
    }
    else
    {
        prs->emitInstruc(setop);
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
            prs->emitInstrucAndShort(setop, (uint16_t)arg);
        }
        else
        {
            prs->emitInstruc(setop);
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
            prs->emitInstruc(neon::Instruction::OP_DUPONE);
        }

        if(arg != -1)
        {
            prs->emitInstrucAndShort(getop, arg);
        }
        else
        {
            prs->emitBytes(getop, 1);
        }

        prs->emitInstruc(neon::Instruction::OP_PUSHONE);
        prs->emitInstruc(neon::Instruction::OP_PRIMADD);
        prs->emitInstrucAndShort(setop, (uint16_t)arg);
    }
    else if(canassign && nn_astparser_match(prs, neon::AstToken::TOK_DECREMENT))
    {
        prs->m_replcanecho = false;
        if(getop == neon::Instruction::OP_PROPERTYGET || getop == neon::Instruction::OP_PROPERTYGETSELF)
        {
            prs->emitInstruc(neon::Instruction::OP_DUPONE);
        }

        if(arg != -1)
        {
            prs->emitInstrucAndShort(getop, arg);
        }
        else
        {
            prs->emitBytes(getop, 1);
        }

        prs->emitInstruc(neon::Instruction::OP_PUSHONE);
        prs->emitInstruc(neon::Instruction::OP_PRIMSUBTRACT);
        prs->emitInstrucAndShort(setop, (uint16_t)arg);
    }
    else
    {
        if(arg != -1)
        {
            if(getop == neon::Instruction::OP_INDEXGET || getop == neon::Instruction::OP_INDEXGETRANGED)
            {
                prs->emitBytes(getop, (uint8_t)0);
            }
            else
            {
                prs->emitInstrucAndShort(getop, (uint16_t)arg);
            }
        }
        else
        {
            prs->emitBytes(getop, (uint8_t)0);
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
            (prs->m_currentclasscompiler != NULL) &&
            (
                (previous.type == neon::AstToken::TOK_KWTHIS) ||
                (nn_astparser_identsequal(&prs->m_prevtoken, &prs->m_currentclasscompiler->name))
            )
        );
        if(caninvoke)
        {
            prs->emitInstrucAndShort(neon::Instruction::OP_CLASSINVOKETHIS, name);
        }
        else
        {
            prs->emitInstrucAndShort(neon::Instruction::OP_CALLMETHOD, name);
        }
        prs->emitByte(argcount);
    }
    else
    {
        getop = neon::Instruction::OP_PROPERTYGET;
        setop = neon::Instruction::OP_PROPERTYSET;
        if(prs->m_currentclasscompiler != NULL && (previous.type == neon::AstToken::TOK_KWTHIS || nn_astparser_identsequal(&prs->m_prevtoken, &prs->m_currentclasscompiler->name)))
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
    fromclass = prs->m_currentclasscompiler != NULL;
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
    if(prs->m_currfunccompiler->m_targetfunc->m_name != NULL)
    {
        local = nn_astparser_addlocal(prs, name) - 1;
        nn_astparser_markinitialized(prs);
        prs->emitInstrucAndShort(neon::Instruction::OP_LOCALSET, (uint16_t)local);
    }
    else
    {
        prs->emitInstrucAndShort(neon::Instruction::OP_GLOBALDEFINE, (uint16_t)nn_astparser_makeidentconst(prs, &name));
    }
}

bool nn_astparser_rulearray(neon::AstParser* prs, bool canassign)
{
    int count;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    /* placeholder for the list */
    prs->emitInstruc(neon::Instruction::OP_PUSHNULL);
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
    prs->emitInstrucAndShort(neon::Instruction::OP_MAKEARRAY, count);
    return true;
}

bool nn_astparser_ruledictionary(neon::AstParser* prs, bool canassign)
{
    bool usedexpression;
    int itemcount;
    neon::AstCompContext oldctx;
    (void)canassign;
    (void)oldctx;
    NEON_ASTDEBUG(prs->m_pvm, "");
    /* placeholder for the dictionary */
    prs->emitInstruc(neon::Instruction::OP_PUSHNULL);
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
                    prs->emitConst(neon::Value::fromObject(neon::String::copy(prs->m_pvm, prs->m_prevtoken.start, prs->m_prevtoken.length)));
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
        } while(nn_astparser_match(prs, neon::AstToken::TOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, neon::AstToken::TOK_BRACECLOSE, "expected '}' after dictionary");
    prs->emitInstrucAndShort(neon::Instruction::OP_MAKEDICT, itemcount);
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
        prs->emitInstruc(neon::Instruction::OP_PUSHNULL);
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
            prs->emitInstruc(neon::Instruction::OP_PUSHNULL);
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
            prs->emitInstruc(neon::Instruction::OP_PUSHNULL);
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
    if(prs->m_currentclasscompiler == NULL)
    {
        prs->raiseError("cannot use keyword 'this' outside of a class");
        return false;
    }
    #endif
    //if(prs->m_currentclasscompiler != NULL)
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
    if(prs->m_currentclasscompiler == NULL)
    {
        prs->raiseError("cannot use keyword 'super' outside of a class");
        return false;
    }
    else if(!prs->m_currentclasscompiler->hassuperclass)
    {
        prs->raiseError("cannot use keyword 'super' in a class without a superclass");
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
            prs->emitInstrucAndShort(neon::Instruction::OP_CLASSINVOKESUPER, name);
            prs->emitByte(argcount);
        }
        else
        {
            prs->emitBytes(neon::Instruction::OP_CLASSINVOKESUPERSELF, argcount);
        }
    }
    else
    {
        nn_astparser_namedvar(prs, nn_astparser_synthtoken(neon::g_strsuper), false);
        prs->emitInstrucAndShort(neon::Instruction::OP_CLASSGETSUPER, name);
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
        llval = strtoll(prs->m_prevtoken.start + 2, NULL, 2);
        return neon::Value::makeNumber(llval);
    }
    else if(prs->m_prevtoken.type == neon::AstToken::TOK_LITNUMOCT)
    {
        longval = strtol(prs->m_prevtoken.start + 2, NULL, 8);
        return neon::Value::makeNumber(longval);
    }
    else if(prs->m_prevtoken.type == neon::AstToken::TOK_LITNUMHEX)
    {
        longval = strtol(prs->m_prevtoken.start, NULL, 16);
        return neon::Value::makeNumber(longval);
    }
    else
    {
        dbval = strtod(prs->m_prevtoken.start, NULL);
        return neon::Value::makeNumber(dbval);
    }
}

bool nn_astparser_rulenumber(neon::AstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    prs->emitConst(nn_astparser_compilenumber(prs));
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
            prs->raiseError("invalid hex escape sequence at #%d of \"%s\": '%c' (%d)", pos, str, cval, cval);
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
    size_t len;
    int value;
    int count;
    char* chr;
    NEON_ASTDEBUG(prs->m_pvm, "");
    value = nn_astparser_readhexescape(prs, realstring, realindex, numberbytes);
    count = neon::Util::utf8NumBytes(value);
    if(count == -1)
    {
        prs->raiseError("cannot encode a negative unicode value");
    }
    /* check for greater that \uffff */
    if(value > 65535)
    {
        count++;
    }
    if(count != 0)
    {
        chr = neon::Util::utf8Encode(value, &len);
        if(chr)
        {
            memcpy(string + index, chr, (size_t)count + 1);
            free(chr);
        }
        else
        {
            prs->raiseError("cannot decode unicode escape at index %d", realindex);
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
    prs->emitConst(neon::Value::fromObject(neon::String::take(prs->m_pvm, str, length)));
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
                prs->emitInstruc(neon::Instruction::OP_PRIMADD);
            }
        }
        nn_astparser_parseexpression(prs);
        prs->emitInstruc(neon::Instruction::OP_STRINGIFY);
        if(doadd || (count >= 1 && stringmatched == false))
        {
            prs->emitInstruc(neon::Instruction::OP_PRIMADD);
        }
        count++;
    } while(nn_astparser_match(prs, neon::AstToken::TOK_INTERPOLATION));
    nn_astparser_consume(prs, neon::AstToken::TOK_LITERAL, "unterminated string interpolation");
    if(prs->m_prevtoken.length - 2 > 0)
    {
        nn_astparser_rulestring(prs, canassign);
        prs->emitInstruc(neon::Instruction::OP_PRIMADD);
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
    nn_astparser_parseprecedence(prs, neon::AstRule::PREC_UNARY);
    /* emit instruction */
    switch(op)
    {
        case neon::AstToken::TOK_MINUS:
            prs->emitInstruc(neon::Instruction::OP_PRIMNEGATE);
            break;
        case neon::AstToken::TOK_EXCLMARK:
            prs->emitInstruc(neon::Instruction::OP_PRIMNOT);
            break;
        case neon::AstToken::TOK_TILDE:
            prs->emitInstruc(neon::Instruction::OP_PRIMBITNOT);
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
    endjump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    prs->emitInstruc(neon::Instruction::OP_POPONE);
    nn_astparser_parseprecedence(prs, neon::AstRule::PREC_AND);
    prs->patchJump(endjump);
    return true;
}

bool nn_astparser_ruleor(neon::AstParser* prs, neon::AstToken previous, bool canassign)
{
    int endjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    elsejump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    endjump = prs->emitJump(neon::Instruction::OP_JUMPNOW);
    prs->patchJump(elsejump);
    prs->emitInstruc(neon::Instruction::OP_POPONE);
    nn_astparser_parseprecedence(prs, neon::AstRule::PREC_OR);
    prs->patchJump(endjump);
    return true;
}

bool nn_astparser_ruleconditional(neon::AstParser* prs, neon::AstToken previous, bool canassign)
{
    int thenjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    thenjump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    prs->emitInstruc(neon::Instruction::OP_POPONE);
    nn_astparser_ignorewhitespace(prs);
    /* compile the then expression */
    nn_astparser_parseprecedence(prs, neon::AstRule::PREC_CONDITIONAL);
    nn_astparser_ignorewhitespace(prs);
    elsejump = prs->emitJump(neon::Instruction::OP_JUMPNOW);
    prs->patchJump(thenjump);
    prs->emitInstruc(neon::Instruction::OP_POPONE);
    nn_astparser_consume(prs, neon::AstToken::TOK_COLON, "expected matching ':' after '?' conditional");
    nn_astparser_ignorewhitespace(prs);
    /*
    // compile the else expression
    // here we parse at neon::AstRule::PREC_ASSIGNMENT precedence as
    // linear conditionals can be nested.
    */
    nn_astparser_parseprecedence(prs, neon::AstRule::PREC_ASSIGNMENT);
    prs->patchJump(elsejump);
    return true;
}

bool nn_astparser_ruleimport(neon::AstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_parseexpression(prs);
    prs->emitInstruc(neon::Instruction::OP_IMPORTIMPORT);
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
    prs->emitInstruc(neon::Instruction::OP_TYPEOF);
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

neon::AstRule* nn_astparser_putrule(neon::AstRule* dest, neon::AstParsePrefixFN prefix, neon::AstParseInfixFN infix, neon::AstRule::Precedence precedence)
{
    dest->prefix = prefix;
    dest->infix = infix;
    dest->precedence = precedence;
    return dest;
}

#define dorule(tok, prefix, infix, precedence) \
    case tok: return nn_astparser_putrule(&dest, prefix, infix, precedence);

neon::AstRule* nn_astparser_getrule(neon::AstToken::Type type)
{
    static neon::AstRule dest;
    switch(type)
    {
        dorule(neon::AstToken::TOK_NEWLINE, nn_astparser_rulenothingprefix, nn_astparser_rulenothinginfix, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_PARENOPEN, nn_astparser_rulegrouping, nn_astparser_rulecall, neon::AstRule::PREC_CALL );
        dorule(neon::AstToken::TOK_PARENCLOSE, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_BRACKETOPEN, nn_astparser_rulearray, nn_astparser_ruleindexing, neon::AstRule::PREC_CALL );
        dorule(neon::AstToken::TOK_BRACKETCLOSE, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_BRACEOPEN, nn_astparser_ruledictionary, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_BRACECLOSE, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_SEMICOLON, nn_astparser_rulenothingprefix, nn_astparser_rulenothinginfix, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_COMMA, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_BACKSLASH, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_EXCLMARK, nn_astparser_ruleunary, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_NOTEQUAL, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_EQUALITY );
        dorule(neon::AstToken::TOK_COLON, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_AT, nn_astparser_ruleanonfunc, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_DOT, NULL, nn_astparser_ruledot, neon::AstRule::PREC_CALL );
        dorule(neon::AstToken::TOK_DOUBLEDOT, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_RANGE );
        dorule(neon::AstToken::TOK_TRIPLEDOT, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_PLUS, nn_astparser_ruleunary, nn_astparser_rulebinary, neon::AstRule::PREC_TERM );
        dorule(neon::AstToken::TOK_PLUSASSIGN, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_INCREMENT, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_MINUS, nn_astparser_ruleunary, nn_astparser_rulebinary, neon::AstRule::PREC_TERM );
        dorule(neon::AstToken::TOK_MINUSASSIGN, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_DECREMENT, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_MULTIPLY, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_FACTOR );
        dorule(neon::AstToken::TOK_MULTASSIGN, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_POWEROF, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_FACTOR );
        dorule(neon::AstToken::TOK_POWASSIGN, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_DIVIDE, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_FACTOR );
        dorule(neon::AstToken::TOK_DIVASSIGN, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_FLOOR, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_FACTOR );
        dorule(neon::AstToken::TOK_ASSIGN, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_EQUAL, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_EQUALITY );
        dorule(neon::AstToken::TOK_LESSTHAN, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_COMPARISON );
        dorule(neon::AstToken::TOK_LESSEQUAL, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_COMPARISON );
        dorule(neon::AstToken::TOK_LEFTSHIFT, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_SHIFT );
        dorule(neon::AstToken::TOK_LEFTSHIFTASSIGN, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_GREATERTHAN, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_COMPARISON );
        dorule(neon::AstToken::TOK_GREATER_EQ, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_COMPARISON );
        dorule(neon::AstToken::TOK_RIGHTSHIFT, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_SHIFT );
        dorule(neon::AstToken::TOK_RIGHTSHIFTASSIGN, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_MODULO, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_FACTOR );
        dorule(neon::AstToken::TOK_PERCENT_EQ, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_AMP, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_BITAND );
        dorule(neon::AstToken::TOK_AMP_EQ, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_BAR, /*nn_astparser_ruleanoncompat*/ NULL, nn_astparser_rulebinary, neon::AstRule::PREC_BITOR );
        dorule(neon::AstToken::TOK_BAR_EQ, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_TILDE, nn_astparser_ruleunary, NULL, neon::AstRule::PREC_UNARY );
        dorule(neon::AstToken::TOK_TILDE_EQ, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_XOR, NULL, nn_astparser_rulebinary, neon::AstRule::PREC_BITXOR );
        dorule(neon::AstToken::TOK_XOR_EQ, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_QUESTION, NULL, nn_astparser_ruleconditional, neon::AstRule::PREC_CONDITIONAL );
        dorule(neon::AstToken::TOK_KWAND, NULL, nn_astparser_ruleand, neon::AstRule::PREC_AND );
        dorule(neon::AstToken::TOK_KWAS, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWASSERT, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWBREAK, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWCLASS, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWCONTINUE, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWFUNCTION, nn_astparser_ruleanonfunc, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWDEFAULT, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWTHROW, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWDO, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWECHO, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWELSE, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWFALSE, nn_astparser_ruleliteral, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWFOREACH, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWIF, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWIMPORT, nn_astparser_ruleimport, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWIN, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWFOR, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWVAR, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWNULL, nn_astparser_ruleliteral, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWNEW, nn_astparser_rulenew, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWTYPEOF, nn_astparser_ruletypeof, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWOR, NULL, nn_astparser_ruleor, neon::AstRule::PREC_OR );
        dorule(neon::AstToken::TOK_KWSUPER, nn_astparser_rulesuper, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWRETURN, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWTHIS, nn_astparser_rulethis, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWSTATIC, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWTRUE, nn_astparser_ruleliteral, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWSWITCH, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWCASE, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWWHILE, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWTRY, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWCATCH, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWFINALLY, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_LITERAL, nn_astparser_rulestring, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_LITNUMREG, nn_astparser_rulenumber, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_LITNUMBIN, nn_astparser_rulenumber, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_LITNUMOCT, nn_astparser_rulenumber, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_LITNUMHEX, nn_astparser_rulenumber, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_IDENTNORMAL, nn_astparser_rulevarnormal, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_INTERPOLATION, nn_astparser_ruleinterpolstring, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_EOF, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_ERROR, NULL, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_KWEMPTY, nn_astparser_ruleliteral, NULL, neon::AstRule::PREC_NONE );
        dorule(neon::AstToken::TOK_UNDEFINED, NULL, NULL, neon::AstRule::PREC_NONE );
        default:
            fprintf(stderr, "missing rule?\n");
            break;
    }
    return NULL;
}
#undef dorule

bool nn_astparser_doparseprecedence(neon::AstParser* prs, neon::AstRule::Precedence precedence/*, neon::AstExpression* dest*/)
{
    bool canassign;
    neon::AstToken previous;
    neon::AstParseInfixFN infixrule;
    neon::AstParsePrefixFN prefixrule;
    prefixrule = nn_astparser_getrule(prs->m_prevtoken.type)->prefix;
    if(prefixrule == NULL)
    {
        prs->raiseError("expected expression");
        return false;
    }
    canassign = precedence <= neon::AstRule::PREC_ASSIGNMENT;
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
        prs->raiseError("invalid assignment target");
        return false;
    }
    return true;
}

bool nn_astparser_parseprecedence(neon::AstParser* prs, neon::AstRule::Precedence precedence)
{
    if(prs->m_lexer->isAtEnd() && prs->m_pvm->m_isrepl)
    {
        return false;
    }
    nn_astparser_ignorewhitespace(prs);
    if(prs->m_lexer->isAtEnd() && prs->m_pvm->m_isrepl)
    {
        return false;
    }
    nn_astparser_advance(prs);
    return nn_astparser_doparseprecedence(prs, precedence);
}

bool nn_astparser_parseprecnoadvance(neon::AstParser* prs, neon::AstRule::Precedence precedence)
{
    if(prs->m_lexer->isAtEnd() && prs->m_pvm->m_isrepl)
    {
        return false;
    }
    nn_astparser_ignorewhitespace(prs);
    if(prs->m_lexer->isAtEnd() && prs->m_pvm->m_isrepl)
    {
        return false;
    }
    return nn_astparser_doparseprecedence(prs, precedence);
}

bool nn_astparser_parseexpression(neon::AstParser* prs)
{
    return nn_astparser_parseprecedence(prs, neon::AstRule::PREC_ASSIGNMENT);
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
    neon::AstParser::CompiledLocal* local;
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
        if(nn_astparser_identsequal(name, &local->name))
        {
            prs->raiseError("%.*s already declared in current scope", name->length, name->start);
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
    if(prs->m_currfunccompiler->m_scopedepth > 0)
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
                prs->raiseError("cannot have more than %d arguments to a function", NEON_CFG_ASTMAXFUNCPARAMS);
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
        prs->m_currfunccompiler->m_targetfunc->m_arity++;
        if(prs->m_currfunccompiler->m_targetfunc->m_arity > NEON_CFG_ASTMAXFUNCPARAMS)
        {
            prs->raiseErrorHere("cannot have more than %d function parameters", NEON_CFG_ASTMAXFUNCPARAMS);
        }
        if(nn_astparser_match(prs, neon::AstToken::TOK_TRIPLEDOT))
        {
            prs->m_currfunccompiler->m_targetfunc->m_isvariadic = true;
            nn_astparser_addlocal(prs, nn_astparser_synthtoken("__args__"));
            nn_astparser_definevariable(prs, 0);
            break;
        }
        paramconstant = nn_astparser_parsefuncparamvar(prs, "expected parameter name");
        nn_astparser_definevariable(prs, paramconstant);
        nn_astparser_ignorewhitespace(prs);
    } while(nn_astparser_match(prs, neon::AstToken::TOK_COMMA));
}

void nn_astfunccompiler_compilebody(neon::AstParser* prs, neon::AstParser::FuncCompiler* compiler, bool closescope, bool isanon)
{
    size_t i;
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
    prs->emitInstrucAndShort(neon::Instruction::OP_MAKECLOSURE, prs->pushConst(neon::Value::fromObject(function)));
    for(i = 0; i < function->m_upvalcount; i++)
    {
        prs->emitByte(compiler->m_compiledupvalues[i].islocal ? 1 : 0);
        prs->emitShort(compiler->m_compiledupvalues[i].index);
    }
    prs->m_pvm->stackPop();
}

void nn_astparser_parsefuncfull(neon::AstParser* prs, neon::FuncCommon::FuncType type, bool isanon)
{
    prs->m_infunction = true;
    neon::AstParser::FuncCompiler compiler(prs, type, isanon);
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
    neon::FuncCommon::FuncType type;
    static neon::AstToken::Type tkns[] = { neon::AstToken::TOK_IDENTNORMAL, neon::AstToken::TOK_DECORATOR };
    (void)classname;
    (void)isstatic;
    sc = "constructor";
    sn = strlen(sc);
    nn_astparser_consumeor(prs, "method name expected", tkns, 2);
    constant = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
    type = neon::FuncCommon::FT_METHOD;
    if((prs->m_prevtoken.length == (int)sn) && (memcmp(prs->m_prevtoken.start, sc, sn) == 0))
    {
        type = neon::FuncCommon::FT_INITIALIZER;
    }
    else if((prs->m_prevtoken.length > 0) && (prs->m_prevtoken.start[0] == '_'))
    {
        type = neon::FuncCommon::FT_PRIVATE;
    }
    nn_astparser_parsefuncfull(prs, type, false);
    prs->emitInstrucAndShort(neon::Instruction::OP_MAKEMETHOD, constant);
}

bool nn_astparser_ruleanonfunc(neon::AstParser* prs, bool canassign)
{
    (void)canassign;
    neon::AstParser::FuncCompiler compiler(prs, neon::FuncCommon::FT_FUNCTION, true);
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
        prs->emitInstruc(neon::Instruction::OP_PUSHNULL);
    }
    prs->emitInstrucAndShort(neon::Instruction::OP_CLASSPROPERTYDEFINE, fieldconstant);
    prs->emitByte(isstatic ? 1 : 0);
    nn_astparser_consumestmtend(prs);
    nn_astparser_ignorewhitespace(prs);
}

void nn_astparser_parsefuncdecl(neon::AstParser* prs)
{
    int global;
    global = nn_astparser_parsevariable(prs, "function name expected");
    nn_astparser_markinitialized(prs);
    nn_astparser_parsefuncfull(prs, neon::FuncCommon::FT_FUNCTION, false);
    nn_astparser_definevariable(prs, global);
}

void nn_astparser_parseclassdeclaration(neon::AstParser* prs)
{
    bool isstatic;
    int nameconst;
    neon::AstCompContext oldctx;
    neon::AstToken classname;
    neon::AstParser::ClassCompiler classcompiler;
    nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, "class name expected");
    nameconst = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
    classname = prs->m_prevtoken;
    nn_astparser_declarevariable(prs);
    prs->emitInstrucAndShort(neon::Instruction::OP_MAKECLASS, nameconst);
    nn_astparser_definevariable(prs, nameconst);
    classcompiler.name = prs->m_prevtoken;
    classcompiler.hassuperclass = false;
    classcompiler.enclosing = prs->m_currentclasscompiler;
    prs->m_currentclasscompiler = &classcompiler;
    oldctx = prs->m_compcontext;
    prs->m_compcontext = neon::NEON_COMPCONTEXT_CLASS;
    if(nn_astparser_match(prs, neon::AstToken::TOK_LESSTHAN))
    {
        nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, "name of superclass expected");
        nn_astparser_rulevarnormal(prs, false);
        if(nn_astparser_identsequal(&classname, &prs->m_prevtoken))
        {
            prs->raiseError("class %.*s cannot inherit from itself", classname.length, classname.start);
        }
        nn_astparser_scopebegin(prs);
        nn_astparser_addlocal(prs, nn_astparser_synthtoken(neon::g_strsuper));
        nn_astparser_definevariable(prs, 0);
        nn_astparser_namedvar(prs, classname, false);
        prs->emitInstruc(neon::Instruction::OP_CLASSINHERIT);
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
    prs->emitInstruc(neon::Instruction::OP_POPONE);
    if(classcompiler.hassuperclass)
    {
        nn_astparser_scopeend(prs);
    }
    prs->m_currentclasscompiler = prs->m_currentclasscompiler->enclosing;
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
            prs->emitInstruc(neon::Instruction::OP_PUSHNULL);
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
    if(prs->m_pvm->m_isrepl && prs->m_currfunccompiler->m_scopedepth == 0)
    {
        prs->m_replcanecho = true;
    }
    if(!semi)
    {
        nn_astparser_parseexpression(prs);
    }
    else
    {
        nn_astparser_parseprecnoadvance(prs, neon::AstRule::PREC_ASSIGNMENT);
    }
    if(!isinitializer)
    {
        if(prs->m_replcanecho && prs->m_pvm->m_isrepl)
        {
            prs->emitInstruc(neon::Instruction::OP_ECHO);
            prs->m_replcanecho = false;
        }
        else
        {
            //if(!prs->m_keeplastvalue)
            {
                prs->emitInstruc(neon::Instruction::OP_POPONE);
            }
        }
        nn_astparser_consumestmtend(prs);
    }
    else
    {
        nn_astparser_consume(prs, neon::AstToken::TOK_SEMICOLON, "expected ';' after initializer");
        nn_astparser_ignorewhitespace(prs);
        prs->emitInstruc(neon::Instruction::OP_POPONE);
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
    prs->m_innermostloopstart = prs->currentBlob()->m_count;
    prs->m_innermostloopscopedepth = prs->m_currfunccompiler->m_scopedepth;
    exitjump = -1;
    if(!nn_astparser_match(prs, neon::AstToken::TOK_SEMICOLON))
    {
        /* the condition is optional */
        nn_astparser_parseexpression(prs);
        nn_astparser_consume(prs, neon::AstToken::TOK_SEMICOLON, "expected ';' after condition");
        nn_astparser_ignorewhitespace(prs);
        /* jump out of the loop if the condition is false... */
        exitjump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
        prs->emitInstruc(neon::Instruction::OP_POPONE);
        /* pop the condition */
    }
    /* the iterator... */
    if(!nn_astparser_check(prs, neon::AstToken::TOK_BRACEOPEN))
    {
        bodyjump = prs->emitJump(neon::Instruction::OP_JUMPNOW);
        incrstart = prs->currentBlob()->m_count;
        nn_astparser_parseexpression(prs);
        nn_astparser_ignorewhitespace(prs);
        prs->emitInstruc(neon::Instruction::OP_POPONE);
        prs->emitLoop(prs->m_innermostloopstart);
        prs->m_innermostloopstart = incrstart;
        prs->patchJump(bodyjump);
    }
    nn_astparser_consume(prs, neon::AstToken::TOK_PARENCLOSE, "expected ')' after 'for'");
    nn_astparser_parsestmt(prs);
    prs->emitLoop(prs->m_innermostloopstart);
    if(exitjump != -1)
    {
        prs->patchJump(exitjump);
        prs->emitInstruc(neon::Instruction::OP_POPONE);
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
    citer = prs->pushConst(neon::Value::fromObject(neon::String::copy(prs->m_pvm, "@iter")));
    citern = prs->pushConst(neon::Value::fromObject(neon::String::copy(prs->m_pvm, "@itern")));
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
    if(prs->m_currfunccompiler->m_localcount + 3 > NEON_CFG_ASTMAXLOCALS)
    {
        prs->raiseError("cannot declare more than %d variables in one scope", NEON_CFG_ASTMAXLOCALS);
        return;
    }
    /* add the iterator to the local scope */
    iteratorslot = nn_astparser_addlocal(prs, iteratortoken) - 1;
    nn_astparser_definevariable(prs, 0);
    /* Create the key local variable. */
    prs->emitInstruc(neon::Instruction::OP_PUSHNULL);
    keyslot = nn_astparser_addlocal(prs, keytoken) - 1;
    nn_astparser_definevariable(prs, keyslot);
    /* create the local value slot */
    prs->emitInstruc(neon::Instruction::OP_PUSHNULL);
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
    prs->emitInstrucAndShort(neon::Instruction::OP_LOCALGET, iteratorslot);
    prs->emitInstrucAndShort(neon::Instruction::OP_LOCALGET, keyslot);
    prs->emitInstrucAndShort(neon::Instruction::OP_CALLMETHOD, citern);
    prs->emitByte(1);
    prs->emitInstrucAndShort(neon::Instruction::OP_LOCALSET, keyslot);
    falsejump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    prs->emitInstruc(neon::Instruction::OP_POPONE);
    /* value = iterable.iter__(key) */
    prs->emitInstrucAndShort(neon::Instruction::OP_LOCALGET, iteratorslot);
    prs->emitInstrucAndShort(neon::Instruction::OP_LOCALGET, keyslot);
    prs->emitInstrucAndShort(neon::Instruction::OP_CALLMETHOD, citer);
    prs->emitByte(1);
    /*
    // Bind the loop value in its own scope. This ensures we get a fresh
    // variable each iteration so that closures for it don't all see the same one.
    */
    nn_astparser_scopebegin(prs);
    /* update the value */
    prs->emitInstrucAndShort(neon::Instruction::OP_LOCALSET, valueslot);
    prs->emitInstruc(neon::Instruction::OP_POPONE);
    nn_astparser_parsestmt(prs);
    nn_astparser_scopeend(prs);
    prs->emitLoop(prs->m_innermostloopstart);
    prs->patchJump(falsejump);
    prs->emitInstruc(neon::Instruction::OP_POPONE);
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
    switchcode = prs->emitSwitch();
    /* prs->emitInstrucAndShort(neon::Instruction::OP_SWITCH, prs->pushConst(neon::Value::fromObject(sw))); */
    startoffset = prs->currentBlob()->m_count;
    while(!nn_astparser_match(prs, neon::AstToken::TOK_BRACECLOSE) && !nn_astparser_check(prs, neon::AstToken::TOK_EOF))
    {
        if(nn_astparser_match(prs, neon::AstToken::TOK_KWCASE) || nn_astparser_match(prs, neon::AstToken::TOK_KWDEFAULT))
        {
            casetype = prs->m_prevtoken.type;
            if(swstate == 2)
            {
                prs->raiseError("cannot have another case after a default case");
            }
            if(swstate == 1)
            {
                /* at the end of the previous case, jump over the others... */
                caseends[casecount++] = prs->emitJump(neon::Instruction::OP_JUMPNOW);
            }
            if(casetype == neon::AstToken::TOK_KWCASE)
            {
                swstate = 1;
                do
                {
                    nn_astparser_ignorewhitespace(prs);
                    nn_astparser_advance(prs);
                    jump = neon::Value::makeNumber((double)prs->currentBlob()->m_count - (double)startoffset);
                    if(prs->m_prevtoken.type == neon::AstToken::TOK_KWTRUE)
                    {
                        sw->m_jumpvals->set(neon::Value::makeBool(true), jump);
                    }
                    else if(prs->m_prevtoken.type == neon::AstToken::TOK_KWFALSE)
                    {
                        sw->m_jumpvals->set(neon::Value::makeBool(false), jump);
                    }
                    else if(prs->m_prevtoken.type == neon::AstToken::TOK_LITERAL)
                    {
                        str = nn_astparser_compilestring(prs, &length);
                        string = neon::String::take(prs->m_pvm, str, length);
                        /* gc fix */
                        prs->m_pvm->stackPush(neon::Value::fromObject(string));
                        sw->m_jumpvals->set(neon::Value::fromObject(string), jump);
                        /* gc fix */
                        prs->m_pvm->stackPop();
                    }
                    else if(nn_astparser_checknumber(prs))
                    {
                        sw->m_jumpvals->set(nn_astparser_compilenumber(prs), jump);
                    }
                    else
                    {
                        /* pop the switch */
                        prs->m_pvm->stackPop();
                        prs->raiseError("only constants can be used in 'when' expressions");
                        return;
                    }
                } while(nn_astparser_match(prs, neon::AstToken::TOK_COMMA));
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
            nn_astparser_parsestmt(prs);
        }
    }
    /* if we ended without a default case, patch its condition jump */
    if(swstate == 1)
    {
        caseends[casecount++] = prs->emitJump(neon::Instruction::OP_JUMPNOW);
    }
    /* patch all the case jumps to the end */
    for(i = 0; i < casecount; i++)
    {
        prs->patchJump(caseends[i]);
    }
    sw->m_exitjump = prs->currentBlob()->m_count - startoffset;
    prs->patchSwitch(switchcode, prs->pushConst(neon::Value::fromObject(sw)));
    /* pop the switch */
    prs->m_pvm->stackPop();
}

void nn_astparser_parseifstmt(neon::AstParser* prs)
{
    int elsejump;
    int thenjump;
    nn_astparser_parseexpression(prs);
    thenjump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    prs->emitInstruc(neon::Instruction::OP_POPONE);
    nn_astparser_parsestmt(prs);
    elsejump = prs->emitJump(neon::Instruction::OP_JUMPNOW);
    prs->patchJump(thenjump);
    prs->emitInstruc(neon::Instruction::OP_POPONE);
    if(nn_astparser_match(prs, neon::AstToken::TOK_KWELSE))
    {
        nn_astparser_parsestmt(prs);
    }
    prs->patchJump(elsejump);
}

void nn_astparser_parseechostmt(neon::AstParser* prs)
{
    nn_astparser_parseexpression(prs);
    prs->emitInstruc(neon::Instruction::OP_ECHO);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parsethrowstmt(neon::AstParser* prs)
{
    nn_astparser_parseexpression(prs);
    prs->emitInstruc(neon::Instruction::OP_EXTHROW);
    nn_astparser_discardlocals(prs, prs->m_currfunccompiler->m_scopedepth - 1);
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
        prs->emitInstruc(neon::Instruction::OP_PUSHNULL);
    }
    prs->emitInstruc(neon::Instruction::OP_ASSERT);
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
    if(prs->m_currfunccompiler->m_compiledexhandlercount == NEON_CFG_MAXEXCEPTHANDLERS)
    {
        prs->raiseError("maximum exception handler in scope exceeded");
    }
    prs->m_currfunccompiler->m_compiledexhandlercount++;
    prs->m_istrying = true;
    nn_astparser_ignorewhitespace(prs);
    trybegins = prs->emitTry();
    /* compile the try body */
    nn_astparser_parsestmt(prs);
    prs->emitInstruc(neon::Instruction::OP_EXPOPTRY);
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
    if(nn_astparser_match(prs, neon::AstToken::TOK_KWCATCH))
    {
        catchexists = true;
        nn_astparser_scopebegin(prs);
        nn_astparser_consume(prs, neon::AstToken::TOK_PARENOPEN, "expected '(' after 'catch'");
        nn_astparser_consume(prs, neon::AstToken::TOK_IDENTNORMAL, "missing exception class name");
        type = nn_astparser_makeidentconst(prs, &prs->m_prevtoken);
        address = prs->currentBlob()->m_count;
        if(nn_astparser_match(prs, neon::AstToken::TOK_IDENTNORMAL))
        {
            nn_astparser_createdvar(prs, prs->m_prevtoken);
        }
        else
        {
            prs->emitInstruc(neon::Instruction::OP_POPONE);
        }
          nn_astparser_consume(prs, neon::AstToken::TOK_PARENCLOSE, "expected ')' after 'catch'");
        prs->emitInstruc(neon::Instruction::OP_EXPOPTRY);
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parsestmt(prs);
        nn_astparser_scopeend(prs);
    }
    else
    {
        type = prs->pushConst(neon::Value::fromObject(neon::String::copy(prs->m_pvm, "Exception")));
    }
    prs->patchJump(exitjump);
    if(nn_astparser_match(prs, neon::AstToken::TOK_KWFINALLY))
    {
        finalexists = true;
        /*
        // if we arrived here from either the try or handler block,
        // we don't want to continue propagating the exception
        */
        prs->emitInstruc(neon::Instruction::OP_PUSHFALSE);
        finally = prs->currentBlob()->m_count;
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parsestmt(prs);
        continueexecutionaddress = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
        /* pop the bool off the stack */
        prs->emitInstruc(neon::Instruction::OP_POPONE);
        prs->emitInstruc(neon::Instruction::OP_EXPUBLISHTRY);
        prs->patchJump(continueexecutionaddress);
        prs->emitInstruc(neon::Instruction::OP_POPONE);
    }
    if(!finalexists && !catchexists)
    {
        prs->raiseError("try block must contain at least one of catch or finally");
    }
    prs->patchTry(trybegins, type, address, finally);
}

void nn_astparser_parsereturnstmt(neon::AstParser* prs)
{
    prs->m_isreturning = true;
    /*
    if(prs->m_currfunccompiler->m_type == neon::FuncCommon::FT_SCRIPT)
    {
        prs->raiseError("cannot return from top-level code");
    }
    */
    if(nn_astparser_match(prs, neon::AstToken::TOK_SEMICOLON) || nn_astparser_match(prs, neon::AstToken::TOK_NEWLINE))
    {
        prs->emitReturn();
    }
    else
    {
        if(prs->m_currfunccompiler->m_type == neon::FuncCommon::FT_INITIALIZER)
        {
            prs->raiseError("cannot return value from constructor");
        }
        if(prs->m_istrying)
        {
            prs->emitInstruc(neon::Instruction::OP_EXPOPTRY);
        }
        nn_astparser_parseexpression(prs);
        prs->emitInstruc(neon::Instruction::OP_RETURN);
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
    prs->m_innermostloopstart = prs->currentBlob()->m_count;
    prs->m_innermostloopscopedepth = prs->m_currfunccompiler->m_scopedepth;
    nn_astparser_parseexpression(prs);
    exitjump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    prs->emitInstruc(neon::Instruction::OP_POPONE);
    nn_astparser_parsestmt(prs);
    prs->emitLoop(prs->m_innermostloopstart);
    prs->patchJump(exitjump);
    prs->emitInstruc(neon::Instruction::OP_POPONE);
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
    prs->m_innermostloopstart = prs->currentBlob()->m_count;
    prs->m_innermostloopscopedepth = prs->m_currfunccompiler->m_scopedepth;
    nn_astparser_parsestmt(prs);
    nn_astparser_consume(prs, neon::AstToken::TOK_KWWHILE, "expecting 'while' statement");
    nn_astparser_parseexpression(prs);
    exitjump = prs->emitJump(neon::Instruction::OP_JUMPIFFALSE);
    prs->emitInstruc(neon::Instruction::OP_POPONE);
    prs->emitLoop(prs->m_innermostloopstart);
    prs->patchJump(exitjump);
    prs->emitInstruc(neon::Instruction::OP_POPONE);
    nn_astparser_endloop(prs);
    prs->m_innermostloopstart = surroundingloopstart;
    prs->m_innermostloopscopedepth = surroundingscopedepth;
}

void nn_astparser_parsecontinuestmt(neon::AstParser* prs)
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

void nn_astparser_parsebreakstmt(neon::AstParser* prs)
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
        if (prs->m_currfunccompiler->m_compiledlocals[i].iscaptured)
        {
            prs->emitInstruc(neon::Instruction::OP_UPVALUECLOSE);
        }
        else
        {
            prs->emitInstruc(neon::Instruction::OP_POPONE);
        }
    }
    */
    nn_astparser_discardlocals(prs, prs->m_innermostloopscopedepth + 1);
    prs->emitJump(neon::Instruction::OP_BREAK_PL);
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
    neon::AstLexer* lexer;
    neon::AstParser* parser;
    neon::FuncScript* function;
    (void)blob;
    NEON_ASTDEBUG(state, "module=%p source=[...] blob=[...] fromimport=%d keeplast=%d", module, fromimport, keeplast);
    lexer = new neon::AstLexer(state, source);
    parser = new neon::AstParser(state, lexer, module, keeplast);
    neon::AstParser::FuncCompiler compiler(parser, neon::FuncCommon::FT_SCRIPT, true);
    compiler.m_funcfromimport = fromimport;
    nn_astparser_runparser(parser);
    function = nn_astparser_endcompiler(parser);
    if(parser->m_haderror)
    {
        function = NULL;
    }
    delete lexer;
    delete parser;
    return function;
}

void nn_gcmem_markcompilerroots(neon::State* state)
{
    (void)state;
    /*
    neon::AstParser::FuncCompiler* compiler;
    compiler = state->m_currfunccompiler;
    while(compiler != NULL)
    {
        nn_gcmem_markobject(state, (neon::Object*)compiler->m_targetfunc);
        compiler = compiler->m_enclosingfunccompiler;
    }
    */
}

neon::ModExport* nn_natmodule_load_null(neon::State* state)
{
    (void)state;
    static neon::ModExport::FuncInfo modfuncs[] =
    {
        /* {"somefunc",   true,  myfancymodulefunction},*/
        {NULL, false, NULL},
    };

    static neon::ModExport::FieldInfo modfields[] =
    {
        /*{"somefield", true, the_function_that_gets_called},*/
        {NULL, false, NULL},
    };

    static neon::ModExport module;
    module.name = "null";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = NULL;
    module.preloader= NULL;
    module.unloader = NULL;
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
        NEON_RAISEEXCEPTION(state, "cannot open directory '%s'", dirn);
    }
    return neon::Value::makeEmpty();
}

neon::ModExport* nn_natmodule_load_os(neon::State* state)
{
    (void)state;
    static neon::ModExport::FuncInfo modfuncs[] =
    {
        {"readdir",   true,  nn_modfn_os_readdir},
        {NULL,     false, NULL},
    };
    static neon::ModExport::FieldInfo modfields[] =
    {
        /*{"platform", true, get_os_platform},*/
        {NULL,       false, NULL},
    };
    static neon::ModExport module;
    module.name = "os";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = NULL;
    module.preloader= &nn_modfn_os_preloader;
    module.unloader = NULL;
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
        token = lex->scanToken();
        itm->addCstr("line", neon::Value::makeNumber(token.line));
        cstr = neon::AstToken::tokenTypeName(token.type);
        itm->addCstr("type", neon::Value::fromObject(neon::String::copy(state, cstr + 12)));
        itm->addCstr("source", neon::Value::fromObject(neon::String::copy(state, token.start, token.length)));
        arr->push(neon::Value::fromObject(itm));
    }
    delete lex;
    return neon::Value::fromObject(arr);
}

neon::ModExport* nn_natmodule_load_astscan(neon::State* state)
{
    neon::ModExport* ret;
    (void)state;
    static neon::ModExport::FuncInfo modfuncs[] =
    {
        {"scan",   true,  nn_modfn_astscan_scan},
        {NULL,     false, NULL},
    };
    static neon::ModExport::FieldInfo modfields[] =
    {
        {NULL,       false, NULL},
    };
    static neon::ModExport module;
    module.name = "astscan";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = NULL;
    module.preloader= NULL;
    module.unloader = NULL;
    ret = &module;
    return ret;
}

neon::ModInitFN g_builtinmodules[] =
{
    nn_natmodule_load_null,
    nn_natmodule_load_os,
    nn_natmodule_load_astscan,
    NULL,
};

bool nn_import_loadnativemodule(neon::State* state, neon::ModInitFN init_fn, char* importname, const char* source, void* dlw)
{
    int j;
    int k;
    neon::Value v;
    neon::Value fieldname;
    neon::Value funcname;
    neon::Value funcrealvalue;
    neon::ModExport::FuncInfo func;
    neon::ModExport::FieldInfo field;
    neon::ModExport* module;
    neon::Module* themodule;
    neon::ModExport::ClassInfo klassreg;
    neon::String* classname;
    neon::FuncNative* native;
    neon::ClassObject* klass;
    neon::HashTable* tabdest;
    module = init_fn(state);
    if(module != NULL)
    {
        themodule = state->gcProtect(neon::Module::make(state, (char*)module->name, source, false));
        themodule->m_preloader = (void*)module->preloader;
        themodule->m_unloader = (void*)module->unloader;
        if(module->fields != NULL)
        {
            for(j = 0; module->fields[j].name != NULL; j++)
            {
                field = module->fields[j];
                fieldname = neon::Value::fromObject(state->gcProtect(neon::String::copy(state, field.name)));
                v = field.fieldvalfn(state);
                state->stackPush(v);
                themodule->m_deftable->set(fieldname, v);
                state->stackPop();
            }
        }
        if(module->functions != NULL)
        {
            for(j = 0; module->functions[j].name != NULL; j++)
            {
                func = module->functions[j];
                funcname = neon::Value::fromObject(state->gcProtect(neon::String::copy(state, func.name)));
                funcrealvalue = neon::Value::fromObject(state->gcProtect(neon::FuncNative::make(state, func.function, func.name, nullptr)));
                state->stackPush(funcrealvalue);
                themodule->m_deftable->set(funcname, funcrealvalue);
                state->stackPop();
            }
        }
        if(module->classes != NULL)
        {
            for(j = 0; module->classes[j].name != NULL; j++)
            {
                klassreg = module->classes[j];
                classname = state->gcProtect(neon::String::copy(state, klassreg.name));
                klass = state->gcProtect(neon::ClassObject::make(state, classname));
                if(klassreg.functions != NULL)
                {
                    for(k = 0; klassreg.functions[k].name != NULL; k++)
                    {
                        func = klassreg.functions[k];
                        funcname = neon::Value::fromObject(state->gcProtect(neon::String::copy(state, func.name)));
                        native = state->gcProtect(neon::FuncNative::make(state, func.function, func.name, nullptr));
                        if(func.isstatic)
                        {
                            native->m_functype = neon::FuncCommon::FT_STATIC;
                        }
                        else if(strlen(func.name) > 0 && func.name[0] == '_')
                        {
                            native->m_functype = neon::FuncCommon::FT_PRIVATE;
                        }
                        klass->m_methods->set(funcname, neon::Value::fromObject(native));
                    }
                }
                if(klassreg.fields != NULL)
                {
                    for(k = 0; klassreg.fields[k].name != NULL; k++)
                    {
                        field = klassreg.fields[k];
                        fieldname = neon::Value::fromObject(state->gcProtect(neon::String::copy(state, field.name)));
                        v = field.fieldvalfn(state);
                        state->stackPush(v);
                        tabdest = klass->m_instprops;
                        if(field.isstatic)
                        {
                            tabdest = klass->m_staticproperties;
                        }
                        tabdest->set(fieldname, v);
                        state->stackPop();
                    }
                }
                themodule->m_deftable->set(neon::Value::fromObject(classname), neon::Value::fromObject(klass));
            }
        }
        if(dlw != NULL)
        {
            themodule->m_libhandle = dlw;
        }
        nn_import_addnativemodule(state, themodule, themodule->m_name->data());
        state->clearProtect();
        return true;
    }
    else
    {
        state->warn("Error loading module: %s\n", importname);
    }
    return false;
}

void nn_import_addnativemodule(neon::State* state, neon::Module* module, const char* as)
{
    neon::Value name;
    if(as != NULL)
    {
        module->m_name = neon::String::copy(state, as);
    }
    name = neon::Value::fromObject(neon::String::copyObjString(state, module->m_name));
    state->stackPush(name);
    state->stackPush(neon::Value::fromObject(module));
    state->m_loadedmodules->set(name, neon::Value::fromObject(module));
    state->stackPop(2);
}

void nn_import_loadbuiltinmodules(neon::State* state)
{
    int i;
    for(i = 0; g_builtinmodules[i] != NULL; i++)
    {
        nn_import_loadnativemodule(state, g_builtinmodules[i], NULL, "<__native__>", NULL);
    }
}

void nn_import_closemodule(void* dlw)
{
    (void)dlw;
    //dlwrap_dlclose(dlw);
}




#define ENFORCE_VALID_DICT_KEY(chp, index) \
    NEON_ARGS_REJECTTYPE(chp, isArray, index); \
    NEON_ARGS_REJECTTYPE(chp, isDict, index); \
    NEON_ARGS_REJECTTYPE(chp, isFile, index);

neon::Value nn_memberfunc_dict_length(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return neon::Value::makeNumber(args->thisval.asDict()->m_names->m_count);
}

neon::Value nn_memberfunc_dict_add(neon::State* state, neon::Arguments* args)
{
    neon::Value tempvalue;
    neon::Dictionary* dict;
    NEON_ARGS_CHECKCOUNT(args, 2);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = args->thisval.asDict();
    if(dict->m_htab->get(args->argv[0], &tempvalue))
    {
        return args->failHere("duplicate key %s at add()", nn_value_tostring(state, args->argv[0])->data());
    }
    dict->add(args->argv[0], args->argv[1]);
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_dict_set(neon::State* state, neon::Arguments* args)
{
    neon::Value value;
    neon::Dictionary* dict;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 2);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = args->thisval.asDict();
    if(!dict->m_htab->get(args->argv[0], &value))
    {
        dict->add(args->argv[0], args->argv[1]);
    }
    else
    {
        dict->set(args->argv[0], args->argv[1]);
    }
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_dict_clear(neon::State* state, neon::Arguments* args)
{
    neon::Dictionary* dict;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = args->thisval.asDict();
    delete dict->m_names;
    delete dict->m_htab;
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
    neon::HashTable::addAll(dict->m_htab, newdict->m_htab);
    for(i = 0; i < dict->m_names->m_count; i++)
    {
        newdict->m_names->push(dict->m_names->m_values[i]);
    }
    return neon::Value::fromObject(newdict);
}

neon::Value nn_memberfunc_dict_compact(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Dictionary* dict;
    neon::Dictionary* newdict;
    neon::Value tmpvalue;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = args->thisval.asDict();
    newdict = state->gcProtect(neon::Dictionary::make(state));
    for(i = 0; i < dict->m_names->m_count; i++)
    {
        dict->m_htab->get(dict->m_names->m_values[i], &tmpvalue);
        if(!nn_value_compare(state, tmpvalue, neon::Value::makeNull()))
        {
            newdict->add(dict->m_names->m_values[i], tmpvalue);
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
    dict = args->thisval.asDict();
    return neon::Value::makeBool(dict->m_htab->get(args->argv[0], &value));
}

neon::Value nn_memberfunc_dict_extend(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Value tmp;
    neon::Dictionary* dict;
    neon::Dictionary* dictcpy;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isDict);
    dict = args->thisval.asDict();
    dictcpy = args->argv[0].asDict();
    for(i = 0; i < dictcpy->m_names->m_count; i++)
    {
        if(!dict->m_htab->get(dictcpy->m_names->m_values[i], &tmp))
        {
            dict->m_names->push(dictcpy->m_names->m_values[i]);
        }
    }
    neon::HashTable::addAll(dictcpy->m_htab, dict->m_htab);
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
    field = dict->get(args->argv[0]);
    if(field == NULL)
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
    size_t i;
    neon::Dictionary* dict;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = args->thisval.asDict();
    list = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < dict->m_names->m_count; i++)
    {
        list->push(dict->m_names->m_values[i]);
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
    for(i = 0; i < dict->m_names->m_count; i++)
    {
        field = dict->get(dict->m_names->m_values[i]);
        list->push(field->value);
    }
    return neon::Value::fromObject(list);
}

neon::Value nn_memberfunc_dict_remove(neon::State* state, neon::Arguments* args)
{
    size_t i;
    int index;
    neon::Value value;
    neon::Dictionary* dict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = args->thisval.asDict();
    if(dict->m_htab->get(args->argv[0], &value))
    {
        dict->m_htab->removeByKey(args->argv[0]);
        index = -1;
        for(i = 0; i < dict->m_names->m_count; i++)
        {
            if(nn_value_compare(state, dict->m_names->m_values[i], args->argv[0]))
            {
                index = i;
                break;
            }
        }
        for(i = index; i < dict->m_names->m_count; i++)
        {
            dict->m_names->m_values[i] = dict->m_names->m_values[i + 1];
        }
        dict->m_names->m_count--;
        return value;
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_dict_isempty(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return neon::Value::makeBool(args->thisval.asDict()->m_names->m_count == 0);
}

neon::Value nn_memberfunc_dict_findkey(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    return args->thisval.asDict()->m_htab->findKey(args->argv[0]);
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
    for(i = 0; i < dict->m_names->m_count; i++)
    {
        namelist->push(dict->m_names->m_values[i]);
        neon::Value value;
        if(dict->m_htab->get(dict->m_names->m_values[i], &value))
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
    if(dict->m_htab->get(args->argv[0], &result))
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
        if(dict->m_names->m_count == 0)
        {
            return neon::Value::makeBool(false);
        }
        return dict->m_names->m_values[0];
    }
    for(i = 0; i < dict->m_names->m_count; i++)
    {
        if(nn_value_compare(state, args->argv[0], dict->m_names->m_values[i]) && (i + 1) < dict->m_names->m_count)
        {
            return dict->m_names->m_values[i + 1];
        }
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_dict_each(neon::State* state, neon::Arguments* args)
{
    size_t i;
    size_t arity;
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
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < dict->m_names->m_count; i++)
    {
        if(arity > 0)
        {
            dict->m_htab->get(dict->m_names->m_values[i], &value);
            nestargs->set(0, value);
            if(arity > 1)
            {
                nestargs->set(1, dict->m_names->m_values[i]);
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &unused);
    }
    /* pop the argument list */
    state->stackPop();
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_dict_filter(neon::State* state, neon::Arguments* args)
{
    size_t i;
    size_t arity;
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
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    resultdict = state->gcProtect(neon::Dictionary::make(state));
    for(i = 0; i < dict->m_names->m_count; i++)
    {
        dict->m_htab->get(dict->m_names->m_values[i], &value);
        if(arity > 0)
        {
            nestargs->set(0, value);
            if(arity > 1)
            {
                nestargs->set(1, dict->m_names->m_values[i]);
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
        if(!nn_value_isfalse(result))
        {
            resultdict->add(dict->m_names->m_values[i], value);
        }
    }
    /* pop the call list */
    state->stackPop();
    return neon::Value::fromObject(resultdict);
}

neon::Value nn_memberfunc_dict_some(neon::State* state, neon::Arguments* args)
{
    size_t i;
    size_t arity;
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
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < dict->m_names->m_count; i++)
    {
        if(arity > 0)
        {
            dict->m_htab->get(dict->m_names->m_values[i], &value);
            nestargs->set(0, value);
            if(arity > 1)
            {
                nestargs->set(1, dict->m_names->m_values[i]);
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
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
    size_t i;
    size_t arity;
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
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < dict->m_names->m_count; i++)
    {
        if(arity > 0)
        {
            dict->m_htab->get(dict->m_names->m_values[i], &value);
            nestargs->set(0, value);
            if(arity > 1)
            {
                nestargs->set(1, dict->m_names->m_values[i]);
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
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
    size_t i;
    size_t arity;
    size_t startindex;
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
    if(accumulator.isNull() && dict->m_names->m_count > 0)
    {
        dict->m_htab->get(dict->m_names->m_values[0], &accumulator);
        startindex = 1;
    }
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = startindex; i < dict->m_names->m_count; i++)
    {
        /* only call map for non-empty m_values in a list. */
        if(!dict->m_names->m_values[i].isNull() && !dict->m_names->m_values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs->set(0, accumulator);
                if(arity > 1)
                {
                    dict->m_htab->get(dict->m_names->m_values[i], &value);
                    nestargs->set(1, value);
                    if(arity > 2)
                    {
                        nestargs->set(2, dict->m_names->m_values[i]);
                        if(arity > 4)
                        {
                            nestargs->set(3, args->thisval);
                        }
                    }
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &accumulator);
        }
    }
    /* pop the call list */
    state->stackPop();
    return accumulator;
}


#undef ENFORCE_VALID_DICT_KEY

#define FILE_ERROR(type, message) \
    return args->failHere(#type " -> %s", message, file->m_fpath->data());

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
    { \
        return args->failHere("method not supported for std files"); \
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
        return args->failHere("file path cannot be empty");
    }
    mode = "r";
    if(args->argc == 2)
    {
        NEON_ARGS_CHECKTYPE(args, 1, isString);
        mode = args->argv[1].asString()->data();
    }
    path = opath->data();
    file = state->gcProtect(neon::File::make(state, NULL, false, path, mode));
    file->openFile();
    return neon::Value::fromObject(file);
}

neon::Value nn_memberfunc_file_exists(neon::State* state, neon::Arguments* args)
{
    neon::String* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    file = args->argv[0].asString();
    return neon::Value::makeBool(neon::File::exists(file->data()));
}


neon::Value nn_memberfunc_file_isfile(neon::State* state, neon::Arguments* args)
{
    neon::String* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    file = args->argv[0].asString();
    return neon::Value::makeBool(neon::File::isFile(file->data()));
}

neon::Value nn_memberfunc_file_isdirectory(neon::State* state, neon::Arguments* args)
{
    neon::String* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    file = args->argv[0].asString();
    return neon::Value::makeBool(neon::File::isDirectory(file->data()));
}

neon::Value nn_memberfunc_file_close(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    args->thisval.asFile()->closeFile();
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_file_open(neon::State* state, neon::Arguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    args->thisval.asFile()->openFile();
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
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    readhowmuch = -1;
    if(args->argc == 1)
    {
        NEON_ARGS_CHECKTYPE(args, 0, isNumber);
        readhowmuch = (size_t)args->argv[0].asNumber();
    }
    file = args->thisval.asFile();
    if(!file->read(readhowmuch, &res))
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
    file = args->thisval.asFile();
    ch = fgetc(file->m_fhandle);
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
        if(!neon::File::exists(file->m_fpath->data()))
        {
            FILE_ERROR(NotFound, "no such file or directory");
        }
        else if(strstr(file->m_fmode->data(), "w") != NULL && strstr(file->m_fmode->data(), "+") == NULL)
        {
            FILE_ERROR(Unsupported, "cannot read file in write mode");
        }
        if(!file->m_isopen)
        {
            FILE_ERROR(Read, "file not open");
        }
        else if(file->m_fhandle == NULL)
        {
            FILE_ERROR(Read, "could not read file");
        }
        if(length == -1)
        {
            currentpos = ftell(file->m_fhandle);
            fseek(file->m_fhandle, 0L, SEEK_END);
            end = ftell(file->m_fhandle);
            fseek(file->m_fhandle, currentpos, SEEK_SET);
            length = end - currentpos;
        }
    }
    else
    {
        if(fileno(stdout) == file->m_fnumber || fileno(stderr) == file->m_fnumber)
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
    if(buffer == NULL && length != 0)
    {
        FILE_ERROR(Buffer, "not enough memory to read file");
    }
    bytesread = fread(buffer, sizeof(char), length, file->m_fhandle);
    if(bytesread == 0 && length != 0)
    {
        FILE_ERROR(Read, "could not read file contents");
    }
    if(buffer != NULL)
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
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    file = args->thisval.asFile();
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->argv[0].asString();
    data = (unsigned char*)string->data();
    length = string->length();
    if(!file->m_isstd)
    {
        if(strstr(file->m_fmode->data(), "r") != NULL && strstr(file->m_fmode->data(), "+") == NULL)
        {
            FILE_ERROR(Unsupported, "cannot write into non-writable file");
        }
        else if(length == 0)
        {
            FILE_ERROR(Write, "cannot write empty buffer to file");
        }
        else if(file->m_fhandle == NULL)
        {
            FILE_ERROR(Write, "could not write to file");
        }
    }
    else
    {
        if(fileno(stdin) == file->m_fnumber)
        {
            FILE_ERROR(Unsupported, "cannot write to input file");
        }
    }
    count = fwrite(data, sizeof(unsigned char), length, file->m_fhandle);
    fflush(file->m_fhandle);
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
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    file = args->thisval.asFile();
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->argv[0].asString();
    data = (unsigned char*)string->data();
    length = string->length();
    if(!file->m_isstd)
    {
        if(strstr(file->m_fmode->data(), "r") != NULL && strstr(file->m_fmode->data(), "+") == NULL)
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
        else if(file->m_fhandle == NULL)
        {
            FILE_ERROR(Write, "could not write to file");
        }
    }
    else
    {
        if(fileno(stdin) == file->m_fnumber)
        {
            FILE_ERROR(Unsupported, "cannot write to input file");
        }
    }
    count = fwrite(data, sizeof(unsigned char), length, file->m_fhandle);
    if(count > (size_t)0 || length == 0)
    {
        return neon::Value::makeBool(true);
    }
    return neon::Value::makeBool(false);
}

neon::Value nn_memberfunc_file_printf(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    neon::Printer pd;
    neon::String* ofmt;
    file = args->thisval.asFile();
    NEON_ARGS_CHECKMINARG(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    ofmt = args->argv[0].asString();
    neon::Printer::makeStackIO(state, &pd, file->m_fhandle, false);
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
    return neon::Value::makeNumber(args->thisval.asFile()->m_fnumber);
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
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = args->thisval.asFile();
    if(!file->m_isopen)
    {
        FILE_ERROR(Unsupported, "I/O operation on closed file");
    }
    #if defined(NEON_PLAT_ISLINUX)
    if(fileno(stdin) == file->m_fnumber)
    {
        while((getchar()) != '\n')
        {
        }
    }
    else
    {
        fflush(file->m_fhandle);
    }
    #else
    fflush(file->m_fhandle);
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
        if(neon::File::exists(file->m_fpath->data()))
        {
            if(osfn_lstat(file->m_fpath->data(), &stats) == 0)
            {
                #if !defined(NEON_PLAT_ISWINDOWS)
                dict->addCstr("isreadable", neon::Value::makeBool(((stats.st_mode & S_IRUSR) != 0)));
                dict->addCstr("iswritable", neon::Value::makeBool(((stats.st_mode & S_IWUSR) != 0)));
                dict->addCstr("isexecutable", neon::Value::makeBool(((stats.st_mode & S_IXUSR) != 0)));
                dict->addCstr("issymbolic", neon::Value::makeBool((S_ISLNK(stats.st_mode) != 0)));
                #else
                dict->addCstr("isreadable", neon::Value::makeBool(((stats.st_mode & S_IREAD) != 0)));
                dict->addCstr("iswritable", neon::Value::makeBool(((stats.st_mode & S_IWRITE) != 0)));
                dict->addCstr("isexecutable", neon::Value::makeBool(((stats.st_mode & S_IEXEC) != 0)));
                dict->addCstr("issymbolic", neon::Value::makeBool(false));
                #endif
                dict->addCstr("size", neon::Value::makeNumber(stats.st_size));
                dict->addCstr("mode", neon::Value::makeNumber(stats.st_mode));
                dict->addCstr("dev", neon::Value::makeNumber(stats.st_dev));
                dict->addCstr("ino", neon::Value::makeNumber(stats.st_ino));
                dict->addCstr("nlink", neon::Value::makeNumber(stats.st_nlink));
                dict->addCstr("uid", neon::Value::makeNumber(stats.st_uid));
                dict->addCstr("gid", neon::Value::makeNumber(stats.st_gid));
                dict->addCstr("mtime", neon::Value::makeNumber(stats.st_mtime));
                dict->addCstr("atime", neon::Value::makeNumber(stats.st_atime));
                dict->addCstr("ctime", neon::Value::makeNumber(stats.st_ctime));
                dict->addCstr("blocks", neon::Value::makeNumber(0));
                dict->addCstr("blksize", neon::Value::makeNumber(0));
            }
        }
        else
        {
            return args->failHere("cannot get stats for non-existing file");
        }
    }
    else
    {
        if(fileno(stdin) == file->m_fnumber)
        {
            dict->addCstr("isreadable", neon::Value::makeBool(true));
            dict->addCstr("iswritable", neon::Value::makeBool(false));
        }
        else
        {
            dict->addCstr("isreadable", neon::Value::makeBool(false));
            dict->addCstr("iswritable", neon::Value::makeBool(true));
        }
        dict->addCstr("isexecutable", neon::Value::makeBool(false));
        dict->addCstr("size", neon::Value::makeNumber(1));
    }
    return neon::Value::fromObject(dict);
}

neon::Value nn_memberfunc_file_path(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = args->thisval.asFile();
    DENY_STD();
    return neon::Value::fromObject(file->m_fpath);
}

neon::Value nn_memberfunc_file_mode(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = args->thisval.asFile();
    return neon::Value::fromObject(file->m_fmode);
}

neon::Value nn_memberfunc_file_name(neon::State* state, neon::Arguments* args)
{
    char* name;
    neon::File* file;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = args->thisval.asFile();
    if(!file->m_isstd)
    {
        name = neon::File::getBasename(file->m_fpath->data());
        return neon::Value::fromObject(neon::String::copy(state, name));
    }
    else if(file->m_istty)
    {
        /*name = ttyname(file->m_fnumber);*/
        name = neon::Util::strCopy("<tty>");
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
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    NEON_ARGS_CHECKTYPE(args, 1, isNumber);
    file = args->thisval.asFile();
    DENY_STD();
    position = (long)args->argv[0].asNumber();
    seektype = args->argv[1].asNumber();
    RETURN_STATUS(fseek(file->m_fhandle, position, seektype));
}

neon::Value nn_memberfunc_file_tell(neon::State* state, neon::Arguments* args)
{
    neon::File* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = args->thisval.asFile();
    DENY_STD();
    return neon::Value::makeNumber(ftell(file->m_fhandle));
}

#undef FILE_ERROR
#undef RETURN_STATUS
#undef DENY_STD

neon::Value nn_memberfunc_array_length(neon::State* state, neon::Arguments* args)
{
    neon::Array* selfarr;
    (void)state;
    selfarr = args->thisval.asArray();
    return neon::Value::makeNumber(selfarr->count());
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
    delete args->thisval.asArray()->m_varray;
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_array_clone(neon::State* state, neon::Arguments* args)
{
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    return neon::Value::fromObject(list->copy(0, list->count()));
}

neon::Value nn_memberfunc_array_count(neon::State* state, neon::Arguments* args)
{
    size_t i;
    size_t count;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    count = 0;
    for(i = 0; i < list->count(); i++)
    {
        if(nn_value_compare(state, list->at(i), args->argv[0]))
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
    for(i = 0; i < list2->count(); i++)
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
    for(; i < list->count(); i++)
    {
        if(nn_value_compare(state, list->at(i), args->argv[0]))
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
    if(list->count() > 0)
    {
        value = list->at(list->count() - 1);
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
    if(count >= list->count() || list->count() == 1)
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
            for(j = 0; j < list->count(); j++)
            {
                list->set(j, list->at(j + 1));
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
    size_t index;
    neon::Value value;
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    list = args->thisval.asArray();
    index = args->argv[0].asNumber();
    if(((int)index < 0) || (index >= list->count()))
    {
        return args->failHere("list index %d out of range at remove_at()", index);
    }
    value = list->at(index);
    for(i = index; i < list->m_varray->m_count - 1; i++)
    {
        list->set(i, list->at(i + 1));
    }
    list->m_varray->m_count--;
    return value;
}

neon::Value nn_memberfunc_array_remove(neon::State* state, neon::Arguments* args)
{
    size_t i;
    size_t index;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    index = -1;
    for(i = 0; i < list->count(); i++)
    {
        if(nn_value_compare(state, list->at(i), args->argv[0]))
        {
            index = i;
            break;
        }
    }
    if((int)index != -1)
    {
        for(i = index; i < list->count(); i++)
        {
            list->set(i, list->at(i + 1));
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
    int end = list->count() - 1;
    while (start < end)
    {
        neon::Value temp = list->at(start);
        list->set(start, list->at(end));
        list->set(end, temp);
        start++;
        end--;
    }
    */
    for(fromtop = list->count() - 1; fromtop >= 0; fromtop--)
    {
        nlist->push(list->at(fromtop));
    }
    return neon::Value::fromObject(nlist);
}

neon::Value nn_memberfunc_array_sort(neon::State* state, neon::Arguments* args)
{
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    nn_value_sortvalues(state, list->m_varray->m_values, list->count());
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_array_contains(neon::State* state, neon::Arguments* args)
{
    size_t i;
    neon::Array* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(nn_value_compare(state, args->argv[0], list->at(i)))
        {
            return neon::Value::makeBool(true);
        }
    }
    return neon::Value::makeBool(false);
}

neon::Value nn_memberfunc_array_delete(neon::State* state, neon::Arguments* args)
{
    size_t i;
    size_t idxupper;
    size_t idxlower;
    neon::Array* list;
    (void)state;
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
    if(((int)idxlower < 0) || (idxlower >= list->m_varray->m_count))
    {
        return args->failHere("list index %d out of range at delete()", idxlower);
    }
    else if(idxupper < idxlower || idxupper >= list->m_varray->m_count)
    {
        return args->failHere("invalid upper limit %d at delete()", idxupper);
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
    size_t count;
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    list = args->thisval.asArray();
    count = args->argv[0].asNumber();
    if((int)count < 0)
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
    size_t index;
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    list = args->thisval.asArray();
    index = args->argv[0].asNumber();
    if(((int)index < 0) || (index >= list->m_varray->m_count))
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
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(!nn_value_compare(state, list->at(i), neon::Value::makeNull()))
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
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        found = false;
        for(j = 0; j < newlist->m_varray->m_count; j++)
        {
            if(nn_value_compare(state, newlist->at(j), list->at(i)))
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
    for(i = 0; i < (size_t)args->argc; i++)
    {
        NEON_ARGS_CHECKTYPE(args, i, isArray);
        arglist[i] = args->argv[i].asArray();
    }
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        alist = state->gcProtect(neon::Array::make(state));
        /* item of main list*/
        alist->push(list->at(i));
        for(j = 0; j < (size_t)args->argc; j++)
        {
            if(i < arglist[j]->m_varray->m_count)
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
    for(i = 0; i < arglist->m_varray->m_count; i++)
    {
        if(!arglist->at(i).isArray())
        {
            return args->failHere("invalid list in zip entries");
        }
    }
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        alist = state->gcProtect(neon::Array::make(state));
        alist->push(list->at(i));
        for(j = 0; j < arglist->m_varray->m_count; j++)
        {
            if(i < arglist->at(j).asArray()->m_varray->m_count)
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
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        dict->set(neon::Value::makeNumber(i), list->at(i));
    }
    return neon::Value::fromObject(dict);
}

neon::Value nn_memberfunc_array_iter(neon::State* state, neon::Arguments* args)
{
    size_t index;
    neon::Array* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    list = args->thisval.asArray();
    index = args->argv[0].asNumber();
    if(((int)index > -1) && (index < list->m_varray->m_count))
    {
        return list->at(index);
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_array_itern(neon::State* state, neon::Arguments* args)
{
    size_t index;
    neon::Array* list;
    (void)state;
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
        return args->failHere("lists are numerically indexed");
    }
    index = args->argv[0].asNumber();
    if(index < (list->m_varray->m_count - 1))
    {
        return neon::Value::makeNumber((double)index + 1);
    }
    return neon::Value::makeNull();
}

neon::Value nn_memberfunc_array_each(neon::State* state, neon::Arguments* args)
{
    size_t i;
    size_t arity;
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
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(arity > 0)
        {
            nestargs->set(0, list->at(i));
            if(arity > 1)
            {
                nestargs->set(1, neon::Value::makeNumber(i));
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &unused);
    }
    state->stackPop();
    return neon::Value::makeEmpty();
}


neon::Value nn_memberfunc_array_map(neon::State* state, neon::Arguments* args)
{
    size_t i;
    size_t arity;
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
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    resultlist = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(!list->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs->set(0, list->at(i));
                if(arity > 1)
                {
                    nestargs->set(1, neon::Value::makeNumber(i));
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &res);
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
    size_t arity;
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
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    resultlist = state->gcProtect(neon::Array::make(state));
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(!list->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs->set(0, list->at(i));
                if(arity > 1)
                {
                    nestargs->set(1, neon::Value::makeNumber(i));
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
            if(!nn_value_isfalse(result))
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
    size_t arity;
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
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(!list->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs->set(0, list->at(i));
                if(arity > 1)
                {
                    nestargs->set(1, neon::Value::makeNumber(i));
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
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
    size_t i;
    size_t arity;
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
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < list->m_varray->m_count; i++)
    {
        if(!list->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs->set(0, list->at(i));
                if(arity > 1)
                {
                    nestargs->set(1, neon::Value::makeNumber(i));
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
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
    size_t i;
    size_t arity;
    size_t startindex;
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
        accumulator = list->at(0);
        startindex = 1;
    }
    nestargs = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = startindex; i < list->m_varray->m_count; i++)
    {
        if(!list->at(i).isNull() && !list->at(i).isEmpty())
        {
            if(arity > 0)
            {
                nestargs->set(0, accumulator);
                if(arity > 1)
                {
                    nestargs->set(1, list->at(i));
                    if(arity > 2)
                    {
                        nestargs->set(2, neon::Value::makeNumber(i));
                        if(arity > 4)
                        {
                            nestargs->set(3, args->thisval);
                        }
                    }
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &accumulator);
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
    size_t index;
    neon::Range* range;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    range = args->thisval.asRange();
    index = args->argv[0].asNumber();
    if(((int)index >= 0) && (index < (size_t)range->m_range))
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
    (void)state;
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
        return args->failHere("ranges are numerically indexed");
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
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < range->m_range; i++)
    {
        if(arity > 0)
        {
            nestargs->set(0, neon::Value::makeNumber(i));
            if(arity > 1)
            {
                nestargs->set(1, neon::Value::makeNumber(i));
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &unused);
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
    buf = neon::Util::utf8Encode(incode, &len);
    res = neon::String::take(state, buf, len);
    return neon::Value::fromObject(res);
}

neon::Value nn_util_stringutf8chars(neon::State* state, neon::Arguments* args, bool onlycodepoint)
{
    int cp;
    const char* cstr;
    utf8iterator_t iter;
    neon::Array* res;
    neon::String* os;
    neon::String* instr;
    (void)state;
    instr = args->thisval.asString();
    res = neon::Array::make(state);
    utf8_init(&iter, instr->data(), instr->length());
    while (utf8_next(&iter))
    {
        cp = iter.codepoint;
        cstr = utf8_getchar(&iter);
        if(onlycodepoint)
        {
            res->push(neon::Value::makeNumber(cp));
        }
        else
        {
            os = neon::String::copy(state, cstr, iter.size);
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
    end = NULL;
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
    end = NULL;
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
    end = NULL;
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
    neon::Printer pd;
    neon::Value vjoinee;
    neon::Array* selfarr;
    neon::String* joinee;
    neon::Value* list;
    selfarr = args->thisval.asArray();
    joinee = NULL;
    if(args->argc > 0)
    {
        vjoinee = args->argv[0];
        if(vjoinee.isString())
        {
            joinee = vjoinee.asString();
        }
        else
        {
            joinee = nn_value_tostring(state, vjoinee);
        }
    }
    list = selfarr->m_varray->m_values;
    count = selfarr->m_varray->m_count;
    if(count == 0)
    {
        return neon::Value::fromObject(neon::String::copy(state, ""));
    }
    neon::Printer::makeStackString(state, &pd);
    for(i = 0; i < count; i++)
    {
        nn_printer_printvalue(&pd, list[i], false, true);
        if((joinee != NULL) && ((i+1) < count))
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
        if(result != NULL)
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
    return neon::Value::makeNumber(strtod(selfstr->data(), NULL));
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
    neon::State::GC::release(state, fill, sizeof(char) * (fillsize + 1));
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
    neon::State::GC::release(state, fill, sizeof(char) * (fillsize + 1));
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
    return neon::Value::fromObject(neon::String::makeBuffer(state, result, neon::Util::hashString(result->data, result->length)));
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
    (void)state;
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
        return args->failHere("strings are numerically indexed");
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
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < (int)string->length(); i++)
    {
        if(arity > 0)
        {
            nestargs->set(0, neon::Value::fromObject(neon::String::copy(state, string->data() + i, 1)));
            if(arity > 1)
            {
                nestargs->set(1, neon::Value::makeNumber(i));
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &unused);
    }
    /* pop the argument list */
    state->stackPop();
    return neon::Value::makeEmpty();
}

neon::Value nn_memberfunc_object_dump(neon::State* state, neon::Arguments* args)
{
    neon::Value v;
    neon::Printer pd;
    neon::String* os;
    v = args->thisval;
    neon::Printer::makeStackString(state, &pd);
    nn_printer_printvalue(&pd, v, true, false);
    os = pd.takeString();
    return neon::Value::fromObject(os);
}

neon::Value nn_memberfunc_object_tostring(neon::State* state, neon::Arguments* args)
{
    neon::Value v;
    neon::Printer pd;
    neon::String* os;
    v = args->thisval;
    neon::Printer::makeStackString(state, &pd);
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
    return (neon::Value::makeBool(
        selfval.isClass() ||
        selfval.isFuncScript() ||
        selfval.isFuncClosure() ||
        selfval.isFuncBound() ||
        selfval.isFuncNative()
    ));
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
        klass = selfval.asInstance()->m_klass;
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


neon::String* nn_util_numbertobinstring(neon::State* state, long n)
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
    return neon::String::copy(state, newstr, length);
    /*
    //  // To store the binary number
    //  long long number = 0;
    //  int cnt = 0;
    //  while (n != 0) {
    //    long long rem = n % 2;
    //    long long c = (long long) pow(10, cnt);
    //    number += rem * c;
    //    n /= 2;
    //
    //    // Count used to store exponent value
    //    cnt++;
    //  }
    //
    //  char str[67]; // assume maximum of 64 bits + 2 binary indicators (0b)
    //  int length = sprintf(str, "0b%lld", number);
    //
    //  return neon::String::copy(state, str, length);
    */
}

neon::String* nn_util_numbertooctstring(neon::State* state, long long n, bool numeric)
{
    int length;
    /* assume maximum of 64 bits + 2 octal indicators (0c) */
    char str[66];
    length = sprintf(str, numeric ? "0c%llo" : "%llo", n);
    return neon::String::copy(state, str, length);
}

neon::String* nn_util_numbertohexstring(neon::State* state, long long n, bool numeric)
{
    int length;
    /* assume maximum of 64 bits + 2 hex indicators (0x) */
    char str[66];
    length = sprintf(str, numeric ? "0x%llx" : "%llx", n);
    return neon::String::copy(state, str, length);
}

neon::Value nn_memberfunc_number_tobinstring(neon::State* state, neon::Arguments* args)
{
    return neon::Value::fromObject(nn_util_numbertobinstring(state, args->thisval.asNumber()));
}

neon::Value nn_memberfunc_number_tooctstring(neon::State* state, neon::Arguments* args)
{
    return neon::Value::fromObject(nn_util_numbertooctstring(state, args->thisval.asNumber(), false));
}

neon::Value nn_memberfunc_number_tohexstring(neon::State* state, neon::Arguments* args)
{
    return neon::Value::fromObject(nn_util_numbertohexstring(state, args->thisval.asNumber(), false));
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
        state->m_classprimstring->defConstructor(nn_memberfunc_string_constructor);
        state->m_classprimstring->defCallableField("length", nn_memberfunc_string_length);
        state->m_classprimstring->defStaticNativeMethod("fromCharCode", nn_memberfunc_string_fromcharcode);
        state->m_classprimstring->defStaticNativeMethod("utf8Decode", nn_memberfunc_string_utf8decode);
        state->m_classprimstring->defStaticNativeMethod("utf8Encode", nn_memberfunc_string_utf8encode);
        state->m_classprimstring->defStaticNativeMethod("utf8NumBytes", nn_memberfunc_string_utf8numbytes);
        state->m_classprimstring->defNativeMethod("@iter", nn_memberfunc_string_iter);
        state->m_classprimstring->defNativeMethod("@itern", nn_memberfunc_string_itern);
        state->m_classprimstring->defNativeMethod("utf8Chars", nn_memberfunc_string_utf8chars);
        state->m_classprimstring->defNativeMethod("utf8Codepoints", nn_memberfunc_string_utf8codepoints);
        state->m_classprimstring->defNativeMethod("utf8Bytes", nn_memberfunc_string_utf8codepoints);
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
        state->m_classprimarray->defConstructor(nn_memberfunc_array_constructor);
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
        state->m_classprimdict->defConstructor(nn_memberfunc_dict_constructor);
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
        state->m_classprimfile->defConstructor(nn_memberfunc_file_constructor);
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
        state->m_classprimrange->defConstructor(nn_memberfunc_range_constructor);
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
    osfn_gettimeofday(&tv, NULL);
    return neon::Value::makeNumber((double)tv.tv_sec + ((double)tv.tv_usec / 10000000));
}

neon::Value nn_nativefn_microtime(neon::State* state, neon::Arguments* args)
{
    struct timeval tv;
    (void)args;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    osfn_gettimeofday(&tv, NULL);
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
    char* buf;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    buf = neon::Util::utf8Encode((int)args->argv[0].asNumber(), &len);
    return neon::Value::fromObject(neon::String::take(state, buf, len));
}

neon::Value nn_nativefn_ord(neon::State* state, neon::Arguments* args)
{
    int ord;
    int length;
    neon::String* string;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->argv[0].asString();
    length = string->length();
    if(length > 1)
    {
        return args->failHere("ord() expects character as argument, string given");
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
        osfn_gettimeofday(&tv, NULL);
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
    return neon::Value::makeBool(nn_util_isinstanceof(args->argv[0].asInstance()->m_klass, args->argv[1].asClass()));
}


neon::Value nn_nativefn_sprintf(neon::State* state, neon::Arguments* args)
{
    neon::Printer pd;
    neon::String* res;
    neon::String* ofmt;
    NEON_ARGS_CHECKMINARG(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    ofmt = args->argv[0].asString();
    neon::Printer::makeStackString(state, &pd);
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
    if(state->m_isrepl)
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
    neon::Printer pd;
    oa = neon::Array::make(state);
    {
        for(i = 0; i < state->m_vmstate.framecount; i++)
        {
            neon::Printer::makeStackString(state, &pd);
            frame = &state->m_vmstate.framevalues[i];
            function = frame->closure->m_scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->m_binblob->m_instrucs - 1;
            line = function->m_binblob->m_instrucs[instruction].srcline;
            physfile = "(unknown)";
            if(function->m_module->m_physpath != NULL)
            {
                if(function->m_module->m_physpath->m_sbuf != NULL)
                {
                    physfile = function->m_module->m_physpath->data();
                }
            }
            fnname = "<script>";
            if(function->m_name != NULL)
            {
                fnname = function->m_name->data();
            }
            pd.format("from %s() in %s:%d", fnname, physfile, line);
            os = pd.takeString();
            oa->push(neon::Value::fromObject(os));
            if((i > 15) && (state->m_conf.showfullstack == false))
            {
                neon::Printer::makeStackString(state, &pd);
                pd.format("(only upper 15 entries shown)");
                os = pd.takeString();
                oa->push(neon::Value::fromObject(os));
                break;
            }
        }
        return neon::Value::fromObject(oa);
    }
    return neon::Value::fromObject(neon::String::copy(state, "", 0));
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
    function = neon::FuncScript::make(state, module, neon::FuncCommon::FT_METHOD);
    function->m_arity = 1;
    function->m_isvariadic = false;
    state->stackPush(neon::Value::fromObject(function));
    {
        /* g_loc 0 */
        function->m_binblob->pushInst(true, neon::Instruction::OP_LOCALGET, 0);
        function->m_binblob->pushInst(false, (0 >> 8) & 0xff, 0);
        function->m_binblob->pushInst(false, 0 & 0xff, 0);
    }
    {
        /* g_loc 1 */
        function->m_binblob->pushInst(true, neon::Instruction::OP_LOCALGET, 0);
        function->m_binblob->pushInst(false, (1 >> 8) & 0xff, 0);
        function->m_binblob->pushInst(false, 1 & 0xff, 0);
    }
    {
        messageconst = function->m_binblob->pushConst(neon::Value::fromObject(neon::String::copy(state, "message")));
        /* s_prop 0 */
        function->m_binblob->pushInst(true, neon::Instruction::OP_PROPERTYSET, 0);
        function->m_binblob->pushInst(false, (messageconst >> 8) & 0xff, 0);
        function->m_binblob->pushInst(false, messageconst & 0xff, 0);
    }
    {
        /* pop */
        function->m_binblob->pushInst(true, neon::Instruction::OP_POPONE, 0);
        function->m_binblob->pushInst(true, neon::Instruction::OP_POPONE, 0);
    }
    {
        /* g_loc 0 */
        /*
        //  function->m_binblob->pushInst(true, neon::Instruction::OP_LOCALGET, 0);
        //  function->m_binblob->pushInst(false, (0 >> 8) & 0xff, 0);
        //  function->m_binblob->pushInst(false, 0 & 0xff, 0);
        */
    }
    {
        /* ret */
        function->m_binblob->pushInst(true, neon::Instruction::OP_RETURN, 0);
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
    nn_instance_defproperty(instance, "message", neon::Value::fromObject(message));
    nn_instance_defproperty(instance, "srcfile", neon::Value::fromObject(osfile));
    nn_instance_defproperty(instance, "srcline", neon::Value::makeNumber(srcline));
    state->stackPop();
    return instance;
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
    state->m_vmstate.linkedobjects = NULL;
    state->m_vmstate.currentframe = NULL;
    {
        state->m_vmstate.stackcapacity = NEON_CFG_INITSTACKCOUNT;
        state->m_vmstate.stackvalues = (neon::Value*)malloc(NEON_CFG_INITSTACKCOUNT * sizeof(neon::Value));
        if(state->m_vmstate.stackvalues == NULL)
        {
            fprintf(stderr, "error: failed to allocate stackvalues!\n");
            abort();
        }
        memset(state->m_vmstate.stackvalues, 0, NEON_CFG_INITSTACKCOUNT * sizeof(neon::Value));
    }
    {
        state->m_vmstate.framecapacity = NEON_CFG_INITFRAMECOUNT;
        state->m_vmstate.framevalues = (neon::State::CallFrame*)malloc(NEON_CFG_INITFRAMECOUNT * sizeof(neon::State::CallFrame));
        if(state->m_vmstate.framevalues == NULL)
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
    state->m_vmstate.openupvalues = NULL;
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
    destrdebug("destroying importpath...");
    delete m_modimportpath;
    destrdebug("destroying linked objects...");
    /* since object in module can exist in globals, it must come before */
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
    nn_gcmem_destroylinkedobjects(this);
    destrdebug("destroying framevalues...");
    free(m_vmstate.framevalues);
    destrdebug("destroying stackvalues...");
    free(m_vmstate.stackvalues);
    destrdebug("destroying this...");

    destrdebug("done destroying!");
}

void neon::State::init()
{
    static const char* defaultsearchpaths[] =
    {
        "mods",
        "mods/@/index" NEON_CFG_FILEEXT,
        ".",
        NULL
    };
    int i;
    m_exceptions.stdexception = NULL;
    m_rootphysfile = NULL;
    m_cliargv = NULL;
    m_isrepl = false;
    m_markvalue = true;
    nn_vm_initvmstate(this);
    nn_state_resetvmstate(this);
    {
        m_gcstate.bytesallocated = 0;
        /* default is 1mb. Can be modified via the -g flag. */
        m_gcstate.nextgc = NEON_CFG_DEFAULTGCSTART;
        m_gcstate.graycount = 0;
        m_gcstate.graycapacity = 0;
        m_gcstate.graystack = NULL;
    }
    {
        m_stdoutprinter = neon::Printer::makeIO(this, stdout, false);
        m_stdoutprinter->m_shouldflush = true;
        m_stderrprinter = neon::Printer::makeIO(this, stderr, false);
        m_debugprinter = neon::Printer::makeIO(this, stderr, false);
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
        m_classconstructorname = neon::String::copy(this, "constructor");
    }
    {
        m_modimportpath = new neon::ValArray(this);
        for(i=0; defaultsearchpaths[i]!=NULL; i++)
        {
            nn_state_addsearchpath(this, defaultsearchpaths[i]);
        }
    }
    {
        m_classprimobject = nn_util_makeclass(this, "Object", NULL);
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
        if(m_exceptions.stdexception == NULL)
        {
            m_exceptions.stdexception = nn_exceptions_makeclass(this, NULL, "Exception");
        }
        m_exceptions.asserterror = nn_exceptions_makeclass(this, NULL, "AssertError");
        m_exceptions.syntaxerror = nn_exceptions_makeclass(this, NULL, "SyntaxError");
        m_exceptions.ioerror = nn_exceptions_makeclass(this, NULL, "IOError");
        m_exceptions.oserror = nn_exceptions_makeclass(this, NULL, "OSError");
        m_exceptions.argumenterror = nn_exceptions_makeclass(this, NULL, "ArgumentError");
    }
    {
        nn_state_initbuiltinfunctions(this);
        nn_state_initbuiltinmethods(this);
    }
    {
        {
            m_filestdout = neon::File::make(this, stdout, true, "<stdout>", "wb");
            defGlobalValue("STDOUT", neon::Value::fromObject(m_filestdout));
        }
        {
            m_filestderr = neon::File::make(this, stderr, true, "<stderr>", "wb");
            defGlobalValue("STDERR", neon::Value::fromObject(m_filestderr));
        }
        {
            m_filestdin = neon::File::make(this, stdin, true, "<stdin>", "rb");
            defGlobalValue("STDIN", neon::Value::fromObject(m_filestdin));
        }
    }
}

void neon::State::defGlobalValue(const char* name, neon::Value val)
{
    neon::String* oname;
    oname = neon::String::copy(this, name);
    stackPush(neon::Value::fromObject(oname));
    stackPush(val);
    m_definedglobals->set(m_vmstate.stackvalues[0], m_vmstate.stackvalues[1]);
    stackPop(2);
}

void neon::State::defNativeFunction(const char* name, neon::NativeFN fptr, void* uptr)
{
    neon::FuncNative* func;
    func = neon::FuncNative::make(this, fptr, name, uptr);
    return defGlobalValue(name, neon::Value::fromObject(func));
}

void neon::State::defNativeFunction(const char* name, neon::NativeFN fptr)
{
    return defNativeFunction(name, fptr, nullptr);
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
    for(; !closure->m_scriptfunc->m_isvariadic && argcount < (int)closure->m_scriptfunc->m_arity; argcount++)
    {
        state->stackPush(neon::Value::makeNull());
    }
    /* handle variadic arguments... */
    if(closure->m_scriptfunc->m_isvariadic && argcount >= (int)(closure->m_scriptfunc->m_arity - 1))
    {
        startva = argcount - closure->m_scriptfunc->m_arity;
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
    if(argcount != (int)closure->m_scriptfunc->m_arity)
    {
        state->stackPop(argcount);
        if(closure->m_scriptfunc->m_isvariadic)
        {
            return NEON_RAISEEXCEPTION(state, "expected at least %d arguments but got %d", closure->m_scriptfunc->m_arity - 1, argcount);
        }
        else
        {
            return NEON_RAISEEXCEPTION(state, "expected %d arguments but got %d", closure->m_scriptfunc->m_arity, argcount);
        }
    }
    if(state->checkMaybeResize())
    {
        /* state->stackPop(argcount); */
    }
    frame = &state->m_vmstate.framevalues[state->m_vmstate.framecount++];
    frame->closure = closure;
    frame->inscode = closure->m_scriptfunc->m_binblob->m_instrucs;
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
    neon::Arguments fnargs(state, native->m_name, thisval, vargs, argcount, native->m_userptr);
    r = native->m_natfunc(state, &fnargs);
    {
        state->m_vmstate.stackvalues[spos - 1] = r;
        state->m_vmstate.stackidx -= argcount;
    }
    state->clearProtect();
    return true;
}

bool nn_vm_callvalue(neon::State* state, neon::Value callable, neon::Value thisval, int argcount);

bool nn_vm_callvaluewithobject(neon::State* state, neon::Value callable, neon::Value thisval, int argcount)
{
    size_t spos;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    if(callable.isObject())
    {
        switch(callable.objType())
        {
            case neon::Value::OBJT_FUNCBOUND:
                {
                    neon::FuncBound* bound;
                    bound = callable.asFuncBound();
                    spos = (state->m_vmstate.stackidx + (-argcount - 1));
                    state->m_vmstate.stackvalues[spos] = thisval;
                    return nn_vm_callclosure(state, bound->m_method, thisval, argcount);
                }
                break;
            case neon::Value::OBJT_CLASS:
                {
                    neon::ClassObject* klass;
                    klass = callable.asClass();
                    spos = (state->m_vmstate.stackidx + (-argcount - 1));
                    state->m_vmstate.stackvalues[spos] = thisval;
                    if(!klass->m_constructor.isEmpty())
                    {
                        return nn_vm_callvaluewithobject(state, klass->m_constructor, thisval, argcount);
                    }
                    else if(klass->m_superclass != NULL && !klass->m_superclass->m_constructor.isEmpty())
                    {
                        return nn_vm_callvaluewithobject(state, klass->m_superclass->m_constructor, thisval, argcount);
                    }
                    else if(argcount != 0)
                    {
                        return NEON_RAISEEXCEPTION(state, "%s constructor expects 0 arguments, %d given", klass->m_name->data(), argcount);
                    }
                    return true;
                }
                break;
            case neon::Value::OBJT_MODULE:
                {
                    neon::Module* module;
                    neon::Property* field;
                    module = callable.asModule();
                    field = module->m_deftable->getFieldByObjStr(module->m_name);
                    if(field != NULL)
                    {
                        return nn_vm_callvalue(state, field->value, thisval, argcount);
                    }
                    return NEON_RAISEEXCEPTION(state, "module %s does not export a default function", module->m_name);
                }
                break;
            case neon::Value::OBJT_FUNCCLOSURE:
                {
                    return nn_vm_callclosure(state, callable.asFuncClosure(), thisval, argcount);
                }
                break;
            case neon::Value::OBJT_FUNCNATIVE:
                {
                    return nn_vm_callnative(state, callable.asFuncNative(), thisval, argcount);
                }
                break;
            default:
                break;
        }
    }
    return NEON_RAISEEXCEPTION(state, "object of type %s is not callable", nn_value_typename(callable));
}

bool nn_vm_callvalue(neon::State* state, neon::Value callable, neon::Value thisval, int argcount)
{
    neon::Value actualthisval;
    if(callable.isObject())
    {
        switch(callable.objType())
        {
            case neon::Value::OBJT_FUNCBOUND:
                {
                    neon::FuncBound* bound;
                    bound = callable.asFuncBound();
                    actualthisval = bound->m_receiver;
                    if(!thisval.isEmpty())
                    {
                        actualthisval = thisval;
                    }
                    NEON_APIDEBUG(state, "actualthisval.type=%s, argcount=%d", nn_value_typename(actualthisval), argcount);
                    return nn_vm_callvaluewithobject(state, callable, actualthisval, argcount);
                }
                break;
            case neon::Value::OBJT_CLASS:
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

neon::FuncCommon::FuncType nn_value_getmethodtype(neon::Value method)
{
    switch(method.objType())
    {
        case neon::Value::OBJT_FUNCNATIVE:
            return method.asFuncNative()->m_functype;
        case neon::Value::OBJT_FUNCCLOSURE:
            return method.asFuncClosure()->m_scriptfunc->m_functype;
        default:
            break;
    }
    return neon::FuncCommon::FT_FUNCTION;
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
            case neon::Value::OBJT_STRING:
                return state->m_classprimstring;
            case neon::Value::OBJT_RANGE:
                return state->m_classprimrange;
            case neon::Value::OBJT_ARRAY:
                return state->m_classprimarray;
            case neon::Value::OBJT_DICT:
                return state->m_classprimdict;
            case neon::Value::OBJT_FILE:
                return state->m_classprimfile;
            /*
            case neon::Value::OBJT_FUNCBOUND:
            case neon::Value::OBJT_FUNCCLOSURE:
            case neon::Value::OBJT_FUNCSCRIPT:
                return state->m_classprimcallable;
            */
            default:
                {
                    fprintf(stderr, "getclassfor: unhandled type!\n");
                }
                break;
        }
    }
    return NULL;
}

#define nn_vmmac_exitvm(state) \
    { \
        (void)you_are_calling_exit_vm_outside_of_runvm; \
        return neon::Status::FAIL_RUNTIME; \
    }        

#define nn_vmmac_tryraise(state, rtval, ...) \
    if(!NEON_RAISEEXCEPTION(state, ##__VA_ARGS__)) \
    { \
        return rtval; \
    }


static NEON_ALWAYSINLINE bool nn_vmutil_invokemethodfromclass(neon::State* state, neon::ClassObject* klass, neon::String* name, int argcount)
{
    neon::Property* field;
    NEON_APIDEBUG(state, "argcount=%d", argcount);
    field = klass->m_methods->getFieldByObjStr(name);
    if(field != NULL)
    {
        if(nn_value_getmethodtype(field->value) == neon::FuncCommon::FT_PRIVATE)
        {
            return NEON_RAISEEXCEPTION(state, "cannot call private method '%s' from instance of %s", name->data(), klass->m_name->data());
        }
        return nn_vm_callvaluewithobject(state, field->value, neon::Value::fromObject(klass), argcount);
    }
    return NEON_RAISEEXCEPTION(state, "undefined method '%s' in %s", name->data(), klass->m_name->data());
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
        field = instance->m_klass->m_methods->getFieldByObjStr(name);
        if(field != NULL)
        {
            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
        }
        field = instance->m_properties->getFieldByObjStr(name);
        if(field != NULL)
        {
            spos = (state->m_vmstate.stackidx + (-argcount - 1));
            #if 0
                state->m_vmstate.stackvalues[spos] = field->value;
                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
            #else
                state->m_vmstate.stackvalues[spos] = receiver;
                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
            #endif
        }
    }
    else if(receiver.isClass())
    {
        field = receiver.asClass()->m_methods->getFieldByObjStr(name);
        if(field != NULL)
        {
            if(nn_value_getmethodtype(field->value) == neon::FuncCommon::FT_STATIC)
            {
                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
            }
            return NEON_RAISEEXCEPTION(state, "cannot call non-static method %s() on non instance", name->data());
        }
    }
    return NEON_RAISEEXCEPTION(state, "cannot call method '%s' on object of type '%s'", name->data(), nn_value_typename(receiver));
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
            case neon::Value::OBJT_MODULE:
                {
                    neon::Module* module;
                    NEON_APIDEBUG(state, "receiver is a module");
                    module = receiver.asModule();
                    field = module->m_deftable->getFieldByObjStr(name);
                    if(field != NULL)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            return NEON_RAISEEXCEPTION(state, "cannot call private module method '%s'", name->data());
                        }
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                    }
                    return NEON_RAISEEXCEPTION(state, "module %s does not define class or method %s()", module->m_name, name->data());
                }
                break;
            case neon::Value::OBJT_CLASS:
                {
                    NEON_APIDEBUG(state, "receiver is a class");
                    klass = receiver.asClass();
                    field = klass->m_methods->getFieldByObjStr(name);
                    if(field != NULL)
                    {
                        if(nn_value_getmethodtype(field->value) == neon::FuncCommon::FT_PRIVATE)
                        {
                            return NEON_RAISEEXCEPTION(state, "cannot call private method %s() on %s", name->data(), klass->m_name->data());
                        }
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                    }
                    else
                    {
                        field = klass->getStaticProperty(name);
                        if(field != NULL)
                        {
                            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                        }
                        field = klass->getStaticMethodField(name);
                        if(field != NULL)
                        {
                            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                        }
                    }
                    return NEON_RAISEEXCEPTION(state, "unknown method %s() in class %s", name->data(), klass->m_name->data());
                }
            case neon::Value::OBJT_INSTANCE:
                {
                    neon::ClassInstance* instance;
                    NEON_APIDEBUG(state, "receiver is an instance");
                    instance = receiver.asInstance();
                    field = instance->m_properties->getFieldByObjStr(name);
                    if(field != NULL)
                    {
                        spos = (state->m_vmstate.stackidx + (-argcount - 1));
                        #if 0
                            state->m_vmstate.stackvalues[spos] = field->value;
                        #else
                            state->m_vmstate.stackvalues[spos] = receiver;
                        #endif
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                    }
                    return nn_vmutil_invokemethodfromclass(state, instance->m_klass, name, argcount);
                }
                break;
            case neon::Value::OBJT_DICT:
                {
                    NEON_APIDEBUG(state, "receiver is a dictionary");
                    field = state->m_classprimdict->getMethod(name);
                    if(field != NULL)
                    {
                        return nn_vm_callnative(state, field->value.asFuncNative(), receiver, argcount);
                    }
                    /* NEW in v0.0.84, dictionaries can declare extra methods as part of their entries. */
                    else
                    {
                        field = receiver.asDict()->m_htab->getFieldByObjStr(name);
                        if(field != NULL)
                        {
                            if(field->value.isCallable())
                            {
                                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                            }
                        }
                    }
                    return NEON_RAISEEXCEPTION(state, "'dict' has no method %s()", name->data());
                }
                default:
                    {
                    }
                    break;
        }
    }
    klass = nn_value_getclassfor(state, receiver);
    if(klass == NULL)
    {
        /* @TODO: have methods for non objects as well. */
        return NEON_RAISEEXCEPTION(state, "non-object %s has no method named '%s'", nn_value_typename(receiver), name->data());
    }
    field = klass->getMethod(name);
    if(field != NULL)
    {
        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
    }
    return NEON_RAISEEXCEPTION(state, "'%s' has no method %s()", klass->m_name->data(), name->data());
}

static NEON_ALWAYSINLINE bool nn_vmutil_bindmethod(neon::State* state, neon::ClassObject* klass, neon::String* name)
{
    neon::Value val;
    neon::Property* field;
    neon::FuncBound* bound;
    field = klass->m_methods->getFieldByObjStr(name);
    if(field != NULL)
    {
        if(nn_value_getmethodtype(field->value) == neon::FuncCommon::FT_PRIVATE)
        {
            return NEON_RAISEEXCEPTION(state, "cannot get private property '%s' from instance", name->data());
        }
        val = state->stackPeek(0);
        bound = neon::FuncBound::make(state, val, field->value.asFuncClosure());
        state->stackPop();
        state->stackPush(neon::Value::fromObject(bound));
        return true;
    }
    return NEON_RAISEEXCEPTION(state, "undefined property '%s'", name->data());
}

static NEON_ALWAYSINLINE neon::ScopeUpvalue* nn_vmutil_upvaluescapture(neon::State* state, neon::Value* local, int stackpos)
{
    neon::ScopeUpvalue* upvalue;
    neon::ScopeUpvalue* prevupvalue;
    neon::ScopeUpvalue* createdupvalue;
    prevupvalue = NULL;
    upvalue = state->m_vmstate.openupvalues;
    while(upvalue != NULL && (&upvalue->m_location) > local)
    {
        prevupvalue = upvalue;
        upvalue = upvalue->m_next;
    }
    if(upvalue != NULL && (&upvalue->m_location) == local)
    {
        return upvalue;
    }
    createdupvalue = neon::ScopeUpvalue::make(state, local, stackpos);
    createdupvalue->m_next = upvalue;
    if(prevupvalue == NULL)
    {
        state->m_vmstate.openupvalues = createdupvalue;
    }
    else
    {
        prevupvalue->m_next = createdupvalue;
    }
    return createdupvalue;
}

static NEON_ALWAYSINLINE void nn_vmutil_upvaluesclose(neon::State* state, const neon::Value* last)
{
    neon::ScopeUpvalue* upvalue;
    while(state->m_vmstate.openupvalues != NULL && (&state->m_vmstate.openupvalues->m_location) >= last)
    {
        upvalue = state->m_vmstate.openupvalues;
        upvalue->m_closed = upvalue->m_location;
        upvalue->m_location = upvalue->m_closed;
        state->m_vmstate.openupvalues = upvalue->m_next;
    }
}

static NEON_ALWAYSINLINE void nn_vmutil_definemethod(neon::State* state, neon::String* name)
{
    neon::Value method;
    neon::ClassObject* klass;
    method = state->stackPeek(0);
    klass = state->stackPeek(1).asClass();
    klass->m_methods->set(neon::Value::fromObject(name), method);
    if(nn_value_getmethodtype(method) == neon::FuncCommon::FT_INITIALIZER)
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

bool nn_value_isfalse(neon::Value value)
{
    if(value.isBool())
    {
        return value.isBool() && !value.asBool();
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
        return value.asDict()->m_names->m_count == 0;
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
    while(klass1 != NULL)
    {
        elen = expected->m_name->length();
        klen = klass1->m_name->length();
        ename = expected->m_name->data();
        kname = klass1->m_name->data();
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
    neon::Printer pd;
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
    neon::Printer::makeStackString(state, &pd);
    for(i = 0; i < times; i++)
    {
        pd.put(str->data(), str->length());
    }
    return pd.takeString();
}

static NEON_ALWAYSINLINE neon::Array* nn_vmutil_combinearrays(neon::State* state, neon::Array* a, neon::Array* b)
{
    size_t i;
    neon::Array* list;
    list = neon::Array::make(state);
    state->stackPush(neon::Value::fromObject(list));
    for(i = 0; i < a->m_varray->m_count; i++)
    {
        list->m_varray->push(a->at(i));
    }
    for(i = 0; i < b->m_varray->m_count; i++)
    {
        list->m_varray->push(b->at(i));
    }
    state->stackPop();
    return list;
}

static NEON_ALWAYSINLINE void nn_vmutil_multiplyarray(neon::State* state, neon::Array* from, neon::Array* newlist, size_t times)
{
    size_t i;
    size_t j;
    (void)state;
    for(i = 0; i < times; i++)
    {
        for(j = 0; j < from->m_varray->m_count; j++)
        {
            newlist->m_varray->push(from->at(j));
        }
    }
}

static NEON_ALWAYSINLINE bool nn_vmutil_dogetrangedindexofarray(neon::State* state, neon::Array* list, bool willassign)
{
    size_t i;
    size_t idxlower;
    size_t idxupper;
    neon::Value valupper;
    neon::Value vallower;
    neon::Array* newlist;
    valupper = state->stackPeek(0);
    vallower = state->stackPeek(1);
    if(!(vallower.isNull() || vallower.isNumber()) || !(valupper.isNumber() || valupper.isNull()))
    {
        state->stackPop(2);
        return NEON_RAISEEXCEPTION(state, "list range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower), nn_value_typename(valupper));
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
    if((int(idxlower) < 0) || ((int(idxupper) < 0) && (int(list->m_varray->m_count + idxupper) < 0)) || idxlower >= list->m_varray->m_count)
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
    if(int(idxupper) < 0)
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
        newlist->m_varray->push(list->at(i));
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
        return NEON_RAISEEXCEPTION(state, "string range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower), nn_value_typename(valupper));
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
    willassign = state->vmReadByte();
    isgotten = true;
    vfrom = state->stackPeek(2);
    if(vfrom.isObject())
    {
        switch(vfrom.asObject()->m_objtype)
        {
            case neon::Value::OBJT_STRING:
            {
                if(!nn_vmutil_dogetrangedindexofstring(state, vfrom.asString(), willassign))
                {
                    return false;
                }
                break;
            }
            case neon::Value::OBJT_ARRAY:
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
        return NEON_RAISEEXCEPTION(state, "cannot range index object of type %s", nn_value_typename(vfrom));
    }
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmutil_doindexgetdict(neon::State* state, neon::Dictionary* dict, bool willassign)
{
    neon::Value vindex;
    neon::Property* field;
    vindex = state->stackPeek(0);
    field = dict->get(vindex);
    if(field != NULL)
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
    return NEON_RAISEEXCEPTION(state, "%s is undefined in module %s", nn_value_tostring(state, vindex)->data(), module->m_name);
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
            rng = vindex.asRange();
            state->stackPop();
            state->stackPush(neon::Value::makeNumber(rng->m_lower));
            state->stackPush(neon::Value::makeNumber(rng->m_upper));
            return nn_vmutil_dogetrangedindexofstring(state, string, willassign);
        }
        state->stackPop(1);
        return NEON_RAISEEXCEPTION(state, "strings are numerically indexed");
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
    #if 0
        return NEON_RAISEEXCEPTION(state, "string index %d out of range of %d", realindex, maxlength);
    #else
        state->stackPush(neon::Value::makeEmpty());
        return true;
    #endif
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
            rng = vindex.asRange();
            state->stackPop();
            state->stackPush(neon::Value::makeNumber(rng->m_lower));
            state->stackPush(neon::Value::makeNumber(rng->m_upper));
            return nn_vmutil_dogetrangedindexofarray(state, list, willassign);
        }
        state->stackPop();
        return NEON_RAISEEXCEPTION(state, "list are numerically indexed");
    }
    index = vindex.asNumber();
    if(NEON_UNLIKELY(index < 0))
    {
        index = list->m_varray->m_count + index;
    }
    if((index < int(list->m_varray->m_count)) && index >= 0)
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

static NEON_ALWAYSINLINE bool nn_vmdo_indexget(neon::State* state)
{
    bool isgotten;
    uint8_t willassign;
    neon::Value peeked;
    willassign = state->vmReadByte();
    isgotten = true;
    peeked = state->stackPeek(1);
    if(NEON_LIKELY(peeked.isObject()))
    {
        switch(peeked.asObject()->m_objtype)
        {
            case neon::Value::OBJT_STRING:
            {
                if(!nn_vmutil_doindexgetstring(state, peeked.asString(), willassign))
                {
                    return false;
                }
                break;
            }
            case neon::Value::OBJT_ARRAY:
            {
                if(!nn_vmutil_doindexgetarray(state, peeked.asArray(), willassign))
                {
                    return false;
                }
                break;
            }
            case neon::Value::OBJT_DICT:
            {
                if(!nn_vmutil_doindexgetdict(state, peeked.asDict(), willassign))
                {
                    return false;
                }
                break;
            }
            case neon::Value::OBJT_MODULE:
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
        NEON_RAISEEXCEPTION(state, "cannot index object of type %s", nn_value_typename(peeked));
    }
    return true;
}


static NEON_ALWAYSINLINE bool nn_vmutil_dosetindexdict(neon::State* state, neon::Dictionary* dict, neon::Value index, neon::Value value)
{
    dict->set(index, value);
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
        return NEON_RAISEEXCEPTION(state, "list are numerically indexed");
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
        list->set(position, value);
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
        fprintf(stderr, "setting value at position %d (array count: %zd)\n", position, list->m_varray->m_count);
    }
    list->set(position, value);
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
    //return NEON_RAISEEXCEPTION(state, "lists index %d out of range", rawpos);
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
        return NEON_RAISEEXCEPTION(state, "strings are numerically indexed");
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
            return NEON_RAISEEXCEPTION(state, "empty cannot be assigned");
        }
        switch(target.asObject()->m_objtype)
        {
            case neon::Value::OBJT_ARRAY:
                {
                    if(!nn_vmutil_doindexsetarray(state, target.asArray(), index, value))
                    {
                        return false;
                    }
                }
                break;
            case neon::Value::OBJT_STRING:
                {
                    if(!nn_vmutil_dosetindexstring(state, target.asString(), index, value))
                    {
                        return false;
                    }
                }
                break;
            case neon::Value::OBJT_DICT:
                {
                    return nn_vmutil_dosetindexdict(state, target.asDict(), index, value);
                }
                break;
            case neon::Value::OBJT_MODULE:
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
        return NEON_RAISEEXCEPTION(state, "type of %s is not a valid iterable", nn_value_typename(state->stackPeek(3)));
    }
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmutil_concatenate(neon::State* state)
{
    neon::Value vleft;
    neon::Value vright;
    neon::Printer pd;
    neon::String* result;
    vright = state->stackPeek(0);
    vleft = state->stackPeek(1);
    neon::Printer::makeStackString(state, &pd);
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
        case neon::Value::OBJT_MODULE:
            {
                neon::Module* module;
                module = peeked.asModule();
                field = module->m_deftable->getFieldByObjStr(name);
                if(field != NULL)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        NEON_RAISEEXCEPTION(state, "cannot get private module property '%s'", name->data());
                        return NULL;
                    }
                    return field;
                }
                NEON_RAISEEXCEPTION(state, "%s module does not define '%s'", module->m_name, name->data());
                return NULL;
            }
            break;
        case neon::Value::OBJT_CLASS:
            {
                field = peeked.asClass()->m_methods->getFieldByObjStr(name);
                if(field != NULL)
                {
                    if(nn_value_getmethodtype(field->value) == neon::FuncCommon::FT_STATIC)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            NEON_RAISEEXCEPTION(state, "cannot call private property '%s' of class %s", name->data(),
                                peeked.asClass()->m_name->data());
                            return NULL;
                        }
                        return field;
                    }
                }
                else
                {
                    field = peeked.asClass()->getStaticProperty(name);
                    if(field != NULL)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            NEON_RAISEEXCEPTION(state, "cannot call private property '%s' of class %s", name->data(),
                                peeked.asClass()->m_name->data());
                            return NULL;
                        }
                        return field;
                    }
                }
                NEON_RAISEEXCEPTION(state, "class %s does not have a static property or method named '%s'",
                    peeked.asClass()->m_name->data(), name->data());
                return NULL;
            }
            break;
        case neon::Value::OBJT_INSTANCE:
            {
                neon::ClassInstance* instance;
                instance = peeked.asInstance();
                field = instance->m_properties->getFieldByObjStr(name);
                if(field != NULL)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        NEON_RAISEEXCEPTION(state, "cannot call private property '%s' from instance of %s", name->data(), instance->m_klass->m_name->data());
                        return NULL;
                    }
                    return field;
                }
                if(nn_util_methodisprivate(name))
                {
                    NEON_RAISEEXCEPTION(state, "cannot bind private property '%s' to instance of %s", name->data(), instance->m_klass->m_name->data());
                    return NULL;
                }
                if(nn_vmutil_bindmethod(state, instance->m_klass, name))
                {
                    return field;
                }
                NEON_RAISEEXCEPTION(state, "instance of class %s does not have a property or method named '%s'",
                    peeked.asInstance()->m_klass->m_name->data(), name->data());
                return NULL;
            }
            break;
        case neon::Value::OBJT_STRING:
            {
                field = state->m_classprimstring->getProperty(name);
                if(field != NULL)
                {
                    return field;
                }
                NEON_RAISEEXCEPTION(state, "class String has no named property '%s'", name->data());
                return NULL;
            }
            break;
        case neon::Value::OBJT_ARRAY:
            {
                field = state->m_classprimarray->getProperty(name);
                if(field != NULL)
                {
                    return field;
                }
                NEON_RAISEEXCEPTION(state, "class Array has no named property '%s'", name->data());
                return NULL;
            }
            break;
        case neon::Value::OBJT_RANGE:
            {
                field = state->m_classprimrange->getProperty(name);
                if(field != NULL)
                {
                    return field;
                }
                NEON_RAISEEXCEPTION(state, "class Range has no named property '%s'", name->data());
                return NULL;
            }
            break;
        case neon::Value::OBJT_DICT:
            {
                field = peeked.asDict()->m_htab->getFieldByObjStr(name);
                if(field == NULL)
                {
                    field = state->m_classprimdict->getProperty(name);
                }
                if(field != NULL)
                {
                    return field;
                }
                NEON_RAISEEXCEPTION(state, "unknown key or class Dict property '%s'", name->data());
                return NULL;
            }
            break;
        case neon::Value::OBJT_FILE:
            {
                field = state->m_classprimfile->getProperty(name);
                if(field != NULL)
                {
                    return field;
                }
                NEON_RAISEEXCEPTION(state, "class File has no named property '%s'", name->data());
                return NULL;
            }
            break;
        default:
            {
                NEON_RAISEEXCEPTION(state, "object of type %s does not carry properties", nn_value_typename(peeked));
                return NULL;
            }
            break;
    }
    return NULL;
}

static NEON_ALWAYSINLINE bool nn_vmdo_propertyget(neon::State* state)
{
    neon::Value peeked;
    neon::Property* field;
    neon::String* name;
    name = state->vmReadString();
    peeked = state->stackPeek(0);
    if(peeked.isObject())
    {
        field = nn_vmutil_getproperty(state, peeked, name);
        if(field == NULL)
        {
            return false;
        }
        else
        {
            if(field->m_type == neon::Property::Type::T_FUNCTION)
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
        NEON_RAISEEXCEPTION(state, "'%s' of type %s does not have properties", nn_value_tostring(state, peeked)->data(),
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
    name = state->vmReadString();
    peeked = state->stackPeek(0);
    if(peeked.isInstance())
    {
        instance = peeked.asInstance();
        field = instance->m_properties->getFieldByObjStr(name);
        if(field != NULL)
        {
            /* pop the instance... */
            state->stackPop();
            state->stackPush(field->value);
            return true;
        }
        if(nn_vmutil_bindmethod(state, instance->m_klass, name))
        {
            return true;
        }
        nn_vmmac_tryraise(state, false, "instance of class %s does not have a property or method named '%s'",
            peeked.asInstance()->m_klass->m_name->data(), name->data());
        return false;
    }
    else if(peeked.isClass())
    {
        klass = peeked.asClass();
        field = klass->m_methods->getFieldByObjStr(name);
        if(field != NULL)
        {
            if(nn_value_getmethodtype(field->value) == neon::FuncCommon::FT_STATIC)
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
            if(field != NULL)
            {
                /* pop the class... */
                state->stackPop();
                state->stackPush(field->value);
                return true;
            }
        }
        nn_vmmac_tryraise(state, false, "class %s does not have a static property or method named '%s'", klass->m_name->data(), name->data());
        return false;
    }
    else if(peeked.isModule())
    {
        module = peeked.asModule();
        field = module->m_deftable->getFieldByObjStr(name);
        if(field != NULL)
        {
            /* pop the module... */
            state->stackPop();
            state->stackPush(field->value);
            return true;
        }
        nn_vmmac_tryraise(state, false, "module %s does not define '%s'", module->m_name, name->data());
        return false;
    }
    nn_vmmac_tryraise(state, false, "'%s' of type %s does not have properties", nn_value_tostring(state, peeked)->data(),
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
        NEON_RAISEEXCEPTION(state, "object of type %s cannot carry properties", nn_value_typename(vtarget));
        return false;
    }
    else if(state->stackPeek(0).isEmpty())
    {
        NEON_RAISEEXCEPTION(state, "empty cannot be assigned");
        return false;
    }
    name = state->vmReadString();
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
        nn_instance_defproperty(instance, name->data(), vpeek);
        value = state->stackPop();
        /* removing the instance object */
        state->stackPop();
        state->stackPush(value);
    }
    else
    {
        dict = vtarget.asDict();
        dict->set(neon::Value::fromObject(name), vpeek);
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
        if(v.asBool())
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
        if(v.asBool())
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
    name = state->vmReadString();
    val = state->stackPeek(0);
    if(val.isEmpty())
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    tab = state->m_vmstate.currentframe->closure->m_scriptfunc->m_module->m_deftable;
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
    name = state->vmReadString();
    tab = state->m_vmstate.currentframe->closure->m_scriptfunc->m_module->m_deftable;
    field = tab->getFieldByObjStr(name);
    if(field == NULL)
    {
        field = state->m_definedglobals->getFieldByObjStr(name);
        if(field == NULL)
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
    name = state->vmReadString();
    table = state->m_vmstate.currentframe->closure->m_scriptfunc->m_module->m_deftable;
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
    slot = state->vmReadShort();
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
    slot = state->vmReadShort();
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
    slot = state->vmReadShort();
    ssp = state->m_vmstate.currentframe->stackslotpos;
    //fprintf(stderr, "FUNCARGGET: %s\n", state->m_vmstate.currentframe->closure->m_scriptfunc->m_name->data());
    val = state->m_vmstate.stackvalues[ssp + slot];
    state->stackPush(val);
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_funcargset(neon::State* state)
{
    size_t ssp;
    uint16_t slot;
    neon::Value peeked;
    slot = state->vmReadShort();
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
    size_t i;
    size_t index;
    size_t ssp;
    uint8_t islocal;
    neon::Value* upvals;
    neon::FuncScript* function;
    neon::FuncClosure* closure;
    function = state->vmReadConst().asFuncScript();
    closure = neon::FuncClosure::make(state, function);
    state->stackPush(neon::Value::fromObject(closure));
    for(i = 0; i < closure->m_upvalcount; i++)
    {
        islocal = state->vmReadByte();
        index = state->vmReadShort();
        if(islocal)
        {
            ssp = state->m_vmstate.currentframe->stackslotpos;
            upvals = &state->m_vmstate.stackvalues[ssp + index];
            closure->m_upvalues[i] = nn_vmutil_upvaluescapture(state, upvals, index);
        }
        else
        {
            closure->m_upvalues[i] = state->m_vmstate.currentframe->closure->m_upvalues[index];
        }
    }
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_makearray(neon::State* state)
{
    int i;
    int count;
    neon::Array* array;
    count = state->vmReadShort();
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
    realcount = state->vmReadShort();
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
        dict->set(name, value);
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
        dbinright = binvalright.isBool() ? (binvalright.asBool() ? 1 : 0) : binvalright.asNumber(); \
        binvalleft = state->stackPop(); \
        dbinleft = binvalleft.isBool() ? (binvalleft.asBool() ? 1 : 0) : binvalleft.asNumber(); \
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
            ofs = (int)(state->m_vmstate.currentframe->inscode - state->m_vmstate.currentframe->closure->m_scriptfunc->m_binblob->m_instrucs);
            nn_dbg_printinstructionat(state->m_debugprinter, state->m_vmstate.currentframe->closure->m_scriptfunc->m_binblob, ofs);
            fprintf(stderr, "stack (before)=[\n");
            iterpos = 0;
            for(dbgslot = state->m_vmstate.stackvalues; dbgslot < &state->m_vmstate.stackvalues[state->m_vmstate.stackidx]; dbgslot++)
            {
                printpos = iterpos + 1;
                iterpos++;
                fprintf(stderr, "  [%s%d%s] ", nc.color(neon::Color::C_YELLOW), printpos, nc.color(neon::Color::C_RESET));
                state->m_debugprinter->format("%s", nc.color(neon::Color::C_YELLOW));
                nn_printer_printvalue(state->m_debugprinter, *dbgslot, true, false);
                state->m_debugprinter->format("%s", nc.color(neon::Color::C_RESET));
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "]\n");
        }
        currinstr = state->vmReadInstruction();
        state->m_vmstate.currentinstr = currinstr;
        //fprintf(stderr, "now executing at line %d\n", state->m_vmstate.currentinstr.srcline);
        switch((neon::Instruction::OpCode)currinstr.code)
        {
            case neon::Instruction::OP_RETURN:
                {
                    size_t ssp;
                    neon::Value result;
                    result = state->stackPop();
                    if(rv != NULL)
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
                    constant = state->vmReadConst();
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
                    offset = state->vmReadShort();
                    state->m_vmstate.currentframe->inscode += offset;
                }
                break;
            case neon::Instruction::OP_JUMPIFFALSE:
                {
                    uint16_t offset;
                    offset = state->vmReadShort();
                    if(nn_value_isfalse(state->stackPeek(0)))
                    {
                        state->m_vmstate.currentframe->inscode += offset;
                    }
                }
                break;
            case neon::Instruction::OP_LOOP:
                {
                    uint16_t offset;
                    offset = state->vmReadShort();
                    state->m_vmstate.currentframe->inscode -= offset;
                }
                break;
            case neon::Instruction::OP_ECHO:
                {
                    neon::Value val;
                    val = state->stackPeek(0);
                    nn_printer_printvalue(state->m_stdoutprinter, val, state->m_isrepl, true);
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
                        value = nn_value_tostring(state, state->stackPop());
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
                    state->stackPop(state->vmReadShort());
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
                    size_t index;
                    neon::FuncClosure* closure;
                    index = state->vmReadShort();
                    closure = state->m_vmstate.currentframe->closure;
                    if(index < closure->m_upvalcount)
                    {
                        state->stackPush(closure->m_upvalues[index]->m_location);
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
                    index = state->vmReadShort();
                    if(state->stackPeek(0).isEmpty())
                    {
                        nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "empty cannot be assigned");
                        break;
                    }
                    state->m_vmstate.currentframe->closure->m_upvalues[index]->m_location = state->stackPeek(0);
                }
                break;
            case neon::Instruction::OP_CALLFUNCTION:
                {
                    int argcount;
                    argcount = state->vmReadByte();
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
                    method = state->vmReadString();
                    argcount = state->vmReadByte();
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
                    method = state->vmReadString();
                    argcount = state->vmReadByte();
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
                    name = state->vmReadString();
                    field = state->m_vmstate.currentframe->closure->m_scriptfunc->m_module->m_deftable->getFieldByObjStr(name);
                    if(field != NULL)
                    {
                        if(field->value.isClass())
                        {
                            haveval = true;
                            pushme = field->value;
                        }
                    }
                    field = state->m_definedglobals->getFieldByObjStr(name);
                    if(field != NULL)
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
                    name = state->vmReadString();
                    nn_vmutil_definemethod(state, name);
                }
                break;
            case neon::Instruction::OP_CLASSPROPERTYDEFINE:
                {
                    int isstatic;
                    neon::String* name;
                    name = state->vmReadString();
                    isstatic = state->vmReadByte();
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
                    superclass = state->stackPeek(1).asClass();
                    subclass = state->stackPeek(0).asClass();
                    subclass->inheritFrom(superclass);
                    /* pop the subclass */
                    state->stackPop();
                }
                break;
            case neon::Instruction::OP_CLASSGETSUPER:
                {
                    neon::ClassObject* klass;
                    neon::String* name;
                    name = state->vmReadString();
                    klass = state->stackPeek(0).asClass();
                    if(!nn_vmutil_bindmethod(state, klass->m_superclass, name))
                    {
                        nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "class %s does not define a function %s", klass->m_name->data(), name->data());
                    }
                }
                break;
            case neon::Instruction::OP_CLASSINVOKESUPER:
                {
                    int argcount;
                    neon::ClassObject* klass;
                    neon::String* method;
                    method = state->vmReadString();
                    argcount = state->vmReadByte();
                    klass = state->stackPop().asClass();
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
                    argcount = state->vmReadByte();
                    klass = state->stackPop().asClass();
                    if(!nn_vmutil_invokemethodfromclass(state, klass, state->m_classconstructorname, argcount))
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
                    mod = state->m_toplevelmodule->loadModule(name);
                    fprintf(stderr, "IMPORTIMPORT: mod='%p'\n", mod);
                    if(mod == NULL)
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
                            NEON_RAISEEXCLASS(state, state->m_exceptions.asserterror, nn_value_tostring(state, message)->data());
                        }
                        else
                        {
                            NEON_RAISEEXCLASS(state, state->m_exceptions.asserterror, "assertion failed");
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
                        nn_util_isinstanceof(peeked.asInstance()->m_klass, state->m_exceptions.stdexception)
                    );
                    if(!isok)
                    {
                        nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "instance of Exception expected");
                        break;
                    }
                    stacktrace = nn_exceptions_getstacktrace(state);
                    instance = peeked.asInstance();
                    nn_instance_defproperty(instance, "stacktrace", stacktrace);
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
                    type = state->vmReadString();
                    addr = state->vmReadShort();
                    finaddr = state->vmReadShort();
                    if(addr != 0)
                    {
                        if(!state->m_definedglobals->get(neon::Value::fromObject(type), &value) || !value.isClass())
                        {
                            if(!state->m_vmstate.currentframe->closure->m_scriptfunc->m_module->m_deftable->get(neon::Value::fromObject(type), &value) || !value.isClass())
                            {
                                nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "object of type '%s' is not an exception", type->data());
                                break;
                            }
                        }
                        state->exceptionPushHandler(value.asClass(), addr, finaddr);
                    }
                    else
                    {
                        state->exceptionPushHandler(NULL, addr, finaddr);
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
                    sw = state->vmReadConst().asSwitch();
                    expr = state->stackPeek(0);
                    if(sw->m_jumpvals->get(expr, &value))
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

int nn_nestcall_prepare(neon::State* state, neon::Value callable, neon::Value mthobj, neon::Array* callarr)
{
    int arity;
    neon::FuncClosure* closure;
    (void)state;
    arity = 0;
    if(callable.isFuncClosure())
    {
        closure = callable.asFuncClosure();
        arity = closure->m_scriptfunc->m_arity;
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
        callarr->push(neon::Value::makeNull());
        if(arity > 1)
        {
            callarr->push(neon::Value::makeNull());
            if(arity > 2)
            {
                callarr->push(mthobj);
            }
        }
    }
    return arity;
}

/* helper function to access call outside the state file. */
bool nn_nestcall_callfunction(neon::State* state, neon::Value callable, neon::Value thisval, neon::Array* args, neon::Value* dest)
{
    int argc;
    size_t i;
    size_t pidx;
    neon::Status status;
    pidx = state->m_vmstate.stackidx;
    /* set the closure before the args */
    state->stackPush(callable);
    argc = 0;
    if(args && (argc = args->m_varray->m_count))
    {
        for(i = 0; i < args->m_varray->m_count; i++)
        {
            state->stackPush(args->at(i));
        }
    }
    if(!nn_vm_callvaluewithobject(state, callable, thisval, argc))
    {
        fprintf(stderr, "nestcall: nn_vm_callvalue() failed\n");
        abort();
    }
    status = nn_vm_runvm(state, state->m_vmstate.framecount - 1, NULL);
    if(status != neon::Status::OK)
    {
        fprintf(stderr, "nestcall: call to runvm failed\n");
        abort();
    }
    *dest = state->m_vmstate.stackvalues[state->m_vmstate.stackidx - 1];
    state->stackPop(argc + 1);
    state->m_vmstate.stackidx = pidx;
    return true;
}

neon::FuncClosure* nn_state_compilesource(neon::State* state, neon::Module* module, bool fromeval, const char* source)
{
    neon::Blob* blob;
    neon::FuncScript* function;
    neon::FuncClosure* closure;
    blob = new neon::Blob(state);
    function = nn_astparser_compilesource(state, module, source, blob, false, fromeval);
    if(function == NULL)
    {
        delete blob;
        return NULL;
    }
    if(!fromeval)
    {
        state->stackPush(neon::Value::fromObject(function));
    }
    else
    {
        function->m_name = neon::String::copy(state, "(evaledcode)");
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
    nn_module_setfilefield(state, module);
    closure = nn_state_compilesource(state, module, false, source);
    if(closure == NULL)
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
    argc = nn_nestcall_prepare(state, callme, neon::Value::makeNull(), args);
    ok = nn_nestcall_callfunction(state, callme, neon::Value::makeNull(), args, &retval);
    if(!ok)
    {
        NEON_RAISEEXCEPTION(state, "eval() failed");
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
    state->m_isrepl = true;
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
        if(line == NULL || strcmp(line, ".exit") == 0)
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
    char* source;
    const char* oldfile;
    neon::Status result;
    source = neon::Util::fileReadFile(file, &fsz);
    if(source == NULL)
    {
        oldfile = file;
        source = neon::Util::fileReadFile(file, &fsz);
        if(source == NULL)
        {
            fprintf(stderr, "failed to read from '%s': %s\n", oldfile, strerror(errno));
            return false;
        }
    }
    state->m_rootphysfile = (char*)file;
    state->m_toplevelmodule->m_physpath = neon::String::copy(state, file);
    result = nn_state_execsource(state, state->m_toplevelmodule, source, NULL);
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
    state->m_rootphysfile = NULL;
    result = nn_state_execsource(state, state->m_toplevelmodule, source, NULL);
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
    for(i=0; envp[i] != NULL; i++)
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
            state->m_envdict->set(neon::Value::fromObject(oskey), neon::Value::fromObject(osval));
        }
    }
}

void nn_cli_printtypesizes()
{
    #define ptyp(t) \
        fprintf(stderr, "%d\t%s\n", (int)sizeof(t), #t);
    ptyp(neon::Printer);
    ptyp(neon::Value);
    ptyp(neon::Object);
    ptyp(neon::Property::PropGetSet);
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
    ptyp(neon::UserData);
    ptyp(neon::State::ExceptionFrame);
    ptyp(neon::State::CallFrame);
    ptyp(neon::State);
    ptyp(neon::AstToken);
    ptyp(neon::AstLexer);
    ptyp(neon::AstParser::CompiledLocal);
    ptyp(neon::AstParser::CompiledUpvalue);
    ptyp(neon::AstParser::FuncCompiler);
    ptyp(neon::AstParser::ClassCompiler);
    ptyp(neon::AstParser);
    ptyp(neon::AstRule);
    ptyp(neon::ModExport::FuncInfo);
    ptyp(neon::ModExport::FieldInfo);
    ptyp(neon::ModExport::ClassInfo);
    ptyp(neon::ModExport);
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
        if(delim != NULL)
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
    for(i=0; flags[i].longname != NULL; i++)
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
            optprs_fprintmaybearg(out, "-", &ch, 1, needval, maybeval, NULL);
        }
        if(flag->longname != NULL)
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
        if(flag->helptext != NULL)
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
    source = NULL;
    nextgcstart = NEON_CFG_DEFAULTGCSTART;
    neon::Util::setOSUnicode();
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
        {"debuggc", 'G', OPTPARSE_REQUIRED, "debug GC (very verbose)"},
        {0, 0, (optargtype_t)0, NULL}
    };
    nargc = 0;
    optprs_init(&options, argc, argv);
    options.permute = 0;
    while ((opt = optprs_nextlongflag(&options, longopts, &longindex)) != -1)
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
        else if(co == 'G')
        {
            state->m_conf.m_debuggc = true;
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
        if(arg == NULL)
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
    if(source != NULL)
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
    delete state;
    if(ok)
    {
        return 0;
    }
    return 1;
}


