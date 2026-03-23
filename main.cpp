
#include <new>
#include <type_traits>
#include <functional>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)
    #define NEON_PLAT_ISWINDOWS
#else
    #define NEON_PLAT_ISLINUX
#endif

#if defined(NEON_PLAT_ISWINDOWS)
    #include <windows.h>
    #include <sys/utime.h>
    #include <fcntl.h>
    #include <io.h>
#else
    #include <sys/time.h>
    #include <unistd.h>
    #include <dirent.h>
    #include <libgen.h>
#endif

#if 0
    #if defined(__GNUC__) || defined(__TINYC__)
        #define NEON_INLINE __attribute__((always_inline)) inline
    #else
        #define NEON_INLINE inline
    #endif
#else
    #define NEON_INLINE
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define NEON_LIKELY(x) (__builtin_expect(!!(x), 1))
    #define NEON_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
    #define NEON_LIKELY(x) (x)
    #define NEON_UNLIKELY(x) (x)
#endif

#include "optparse.h"
#include "lino.h"
#include "allocator.h"

/*
* set to 1 to use allocator (default).
* stable, performs well, etc.
* might not be portable beyond linux/windows, and a couple unix derivatives.
* strives to use the least amount of memory (and does so very successfully).
*/
#define NEON_CONF_MEMUSEALLOCATOR 0

#define NEON_CONF_OSPATHSIZE 1024

#define NEON_CONFIG_FILEEXT ".nn"

/* global debug mode flag */
#define NEON_CONFIG_DEBUGGC 0

#define NEON_INFO_COPYRIGHT "based on the Blade Language, Copyright (c) 2021 - 2023 Ore Richard Muyiwa"

#if !defined(S_IFLNK)
    #define S_IFLNK 0120000
#endif
#if !defined(S_IFREG)
    #define S_IFREG  0100000
#endif
#if !defined(S_IFDIR)
    #define S_IFDIR  0040000
#endif
#if !defined(S_ISDIR)
    #define	S_ISDIR(m) (((m)&S_IFMT) == S_IFDIR)	/* directory */
#endif

#if !defined(S_ISREG)
    #define	S_ISREG(m) (((m)&S_IFMT) == S_IFREG)	/* file */
#endif

#if !defined(S_ISLNK)
    #define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif

extern "C"
{
    extern int kill(int, int);
}

/**
 * if enabled, most API calls will check for null pointers, and either
 * return immediately or return an appropiate default value if a nullpointer is encountered.
 * this will make the API much less likely to crash out in a segmentation fault,
 * **BUT** the added branching will likely reduce performance.
 */
#define NEON_CONFIG_USENULLPTRCHECKS 0

#if defined(NEON_CONFIG_USENULLPTRCHECKS) && (NEON_CONFIG_USENULLPTRCHECKS == 1)
    #define NN_NULLPTRCHECK_RETURNVALUE(var, defval) \
        {                                            \
            if((var) == nullptr)                     \
            {                                        \
                return (defval);                     \
            }                                        \
        }
    #define NN_NULLPTRCHECK_RETURN(var) \
        {                               \
            if((var) == nullptr)        \
            {                           \
                return;                 \
            }                           \
        }
#else
    #define NN_NULLPTRCHECK_RETURNVALUE(var, defval)
    #define NN_NULLPTRCHECK_RETURN(var)
#endif

#define NEON_THROWCLASSWITHSOURCEINFO(exklass, ...) throwScriptException(exklass, __FILE__, __LINE__, __VA_ARGS__)

#define NEON_THROWEXCEPTION(...) NEON_THROWCLASSWITHSOURCEINFO(SharedState::get()->m_exceptions.stdexception, __VA_ARGS__)

#define NEON_RETURNERROR(scfn, ...)               \
    {                                       \
        SharedState::get()->vmStackPop(scfn.argc); \
        NEON_THROWEXCEPTION(__VA_ARGS__);       \
    }                                       \
    return Value::makeBool(false);

#define NEON_ARGS_FAIL(chp, ...) chp.thenFail(__FILE__, __LINE__, __VA_ARGS__)

/* check for exact number of arguments $d */
#define NEON_ARGS_CHECKCOUNT(chp, d)                                                                    \
    if(NEON_UNLIKELY(chp.m_scriptfnctx.argc != (d)))                                                            \
    {                                                                                                   \
        return NEON_ARGS_FAIL(chp, "%s() expects %d arguments, %d given", chp.m_argcheckfuncname, d, chp.m_scriptfnctx.argc); \
    }

/* check for miminum args $d ($d ... n) */
#define NEON_ARGS_CHECKMINARG(chp, d)                                                                              \
    if(NEON_UNLIKELY(chp.m_scriptfnctx.argc < (d)))                                                                        \
    {                                                                                                              \
        return NEON_ARGS_FAIL(chp, "%s() expects minimum of %d arguments, %d given", chp.m_argcheckfuncname, d, chp.m_scriptfnctx.argc); \
    }

/* check for range of args ($low .. $up) */
#define NEON_ARGS_CHECKCOUNTRANGE(chp, low, up)                                                                              \
    if((int(NEON_UNLIKELY(chp.m_scriptfnctx.argc) < int(low)) || (int(chp.m_scriptfnctx.argc) > int(up))))                                                          \
    {                                                                                                                        \
        return NEON_ARGS_FAIL(chp, "%s() expects between %d and %d arguments, %d given", chp.m_argcheckfuncname, low, up, chp.m_scriptfnctx.argc); \
    }

/* check for argument at index $i for $type, where $type is a Value::is*() function */
#define NEON_ARGS_CHECKTYPE(chp, i, typefunc) \
    if(NEON_UNLIKELY(!((chp.m_scriptfnctx.argv[i].*typefunc)()))) \
    { \
        return NEON_ARGS_FAIL(chp, "%s() expects argument %d as %s, %s given", chp.m_argcheckfuncname, (i) + 1, Value::typeFromFunction(typefunc), Value::typeName(chp.m_scriptfnctx.argv[i], false), false); \
    }

#if 0
    #define NEON_APIDEBUG(...) \
        if((NEON_UNLIKELY((<fixme>)->m_conf.enableapidebug))) \
        { \
            apiDebug(__FUNCTION__, __VA_ARGS__); \
        }
#else
    #define NEON_APIDEBUG(...)
#endif

namespace neon
{
    enum class Status
    {
        Ok,
        CompileFailed,
        RuntimeFail
    };

    enum Color
    {
        NEON_COLOR_RESET,
        NEON_COLOR_RED,
        NEON_COLOR_GREEN,
        NEON_COLOR_YELLOW,
        NEON_COLOR_BLUE,
        NEON_COLOR_MAGENTA,
        NEON_COLOR_CYAN
    };

    class /**/ Value;
    class /**/ Object;
    class /**/ AstParser;
    class /**/ AstToken;
    class /**/ String;
    class /**/ Array;
    class /**/ Property;
    class /**/ IOStream;
    class /**/ Blob;
    class /**/ Function;
    class /**/ Dict;
    class /**/ Object;
    class /**/ File;
    class /**/ Instance;
    class /**/ Class;
    class /**/ IOResult;
    class /**/ Instruction;
    class /**/ AstLexer;
    class /**/ FormatInfo;
    class /**/ utf8iterator_t;
    class /**/ Module;
    class /**/ Upvalue;
    class /**/ Userdata;
    class /**/ Switch;
    class /**/ Dict;
    class /**/ Range;
    class /**/ FuncContext;
    class ArgCheck;

    template<typename CharT>
    class StrBufBasic;
    
    template<typename StoredType>
    class ValList;
    
    template<typename HTKeyT, typename HTValT>
    class HashTable;

    using StrBuffer = StrBufBasic<char>;
    using NativeFN = Value(*)(const FuncContext&);
    using BinOpFuncFN = Value(*)(double, double);

    void initBuiltinObjects();
    Instance* makeExceptionInstance(Class* exklass, const char* srcfile, int srcline, String* message);
    bool defineGlobalValue(const char* name, Value val);
    bool defineGlobalNativeFuncPtr(const char* name, NativeFN fptr, void* uptr);
    bool defineGlobalNativeFunction(const char* name, NativeFN fptr);
    void installObjArray();
    void installObjDict();
    void installObjFile();
    void installObjDirectory();
    void setupModulePaths();
    void installObjNumber();
    void installModMath();
    void installObjObject();
    void installObjProcess();
    void installObjRange();
    void installObjString();
    void initBuiltinFunctions();

    Function* compileSourceIntern(Module* module, const char* source, Blob* blob, bool keeplast);

    template<typename... ArgsT>
    bool throwScriptException(Class* exklass, const char* srcfile, int srcline, const char* format, ArgsT&&... args);

    template <typename ClassT>
    concept MemoryClassHasDestroyFunc = requires(ClassT* ptr)
    {
        { ClassT::destroy(ptr) };
    };

    class Memory
    {
        public:
            static size_t getNextCapacity(size_t capacity)
            {
                if(capacity < 4)
                {
                    return 4;
                }
                return (capacity * 2);
            }

            #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
                /* if any global variables need to be declared, declare them here. */
                static void* g_mspcontext;
            #endif

            static inline void mempoolInit()
            {
                #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
                    g_mspcontext = mempool_createpool();
                #endif
            }

            static inline void mempoolDestroy()
            {
                #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
                    mempool_destroypool(g_mspcontext);
                #endif
            }

            static inline void* sysMalloc(size_t sz)
            {
                void* p;
                #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
                    p = (void*)mempool_usermalloc(g_mspcontext, sz);
                #else
                    p = (void*)malloc(sz);
                #endif
                return p;
            }

            static inline void* sysRealloc(void* p, size_t nsz)
            {
                void* retp;
                #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
                    if(p == NULL)
                    {
                        return sysMalloc(nsz);
                    }
                    retp = (void*)mempool_userrealloc(g_mspcontext, p, nsz);
                #else
                    retp = (void*)realloc(p, nsz);
                #endif
                return retp;
            }

            static inline void* sysCalloc(size_t count, size_t typsize)
            {
                void* p;
                #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
                    p = (void*)mempool_usermalloc(g_mspcontext, (count * typsize));
                    memset(p, 0, (count * typsize));
                #else
                    p = (void*)calloc(count, typsize);
                #endif
                return p;
            }

            static inline void sysFree(void* ptr)
            {
                #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
                    mempool_userfree(g_mspcontext, ptr);
                #else
                    free(ptr);
                #endif
            }

            template<typename ClassT, typename... ArgsT>
            static inline ClassT* make(ArgsT&&... args)
            {
                ClassT* tmp;
                ClassT* ret;
                tmp = (ClassT*)sysMalloc(sizeof(ClassT));
                ret = new(tmp) ClassT(args...);
                return ret;
            }

            template<typename ClassT, typename... ArgsT>
            static inline void destroy(ClassT* cls, ArgsT&&... args)
            {
                if constexpr (MemoryClassHasDestroyFunc<ClassT>)
                {
                    //fprintf(stderr, "Memory::destroy: using destroy\n");
                    ClassT::destroy(cls, args...);
                }
                else
                {
                    //fprintf(stderr, "Memory::destroy: using free()");
                    sysFree(cls);
                }
            }
    };
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        /* if any global variables need to be declared, declare them here. */
        void* Memory::g_mspcontext = nullptr;
    #endif

    namespace Util
    {
        enum
        {
            CONF_MERSENNESTATESIZE = 624,
        };

        struct FSStat
        {
            public:
                static const char* modeToName(int t)
                {
                    switch(t)
                    {
                        #if defined(S_IFBLK)
                            case S_IFBLK:
                                return "blockdevice";
                                break;
                        #endif
                        #if defined(S_IFCHR)
                            case S_IFCHR:
                                return "characterdevice";
                                break;
                        #endif
                        #if defined(S_IFDIR)
                            case S_IFDIR:
                                return "directory";
                                break;
                        #endif
                        #if defined(S_IFIFO)
                            case S_IFIFO:
                                return "pipe";
                                break;
                        #endif
                        #if defined(S_IFLNK)
                            case S_IFLNK:
                                return "symlink";
                                break;
                        #endif
                        #if defined(S_IFREG)
                            case S_IFREG:
                                return "file";
                                break;
                        #endif
                        #if defined(S_IFSOCK)
                            case S_IFSOCK:
                                return "socket";
                                break;
                        #endif
                            default:
                                break;
                    }
                    return "unknown";
                }

            public:
                struct stat m_rawstbuf = {};
                int m_mode = 0;
                int m_inode = 0;
                int m_numlinks = 0;
                int m_owneruid = 0;
                int m_ownergid = 0;
                const char* m_modename = nullptr;
                bool m_isfile = false;
                size_t m_blocksize = 0;
                size_t m_blockcount  = 0;
                size_t m_filesize = 0;
                const time_t* m_tmlastchanged = nullptr;
                const time_t* m_tmlastaccessed = nullptr;
                const time_t* m_tmlastmodified = nullptr;

            private:
                bool fillout()
                {
                    m_inode = m_rawstbuf.st_ino;
                    m_mode = (m_rawstbuf.st_mode & S_IFMT);
                    m_numlinks = m_rawstbuf.st_nlink;
                    m_owneruid = m_rawstbuf.st_uid;
                    m_ownergid = m_rawstbuf.st_gid;
                    #if !defined(_WIN32) && !defined(_WIN64)
                        m_blocksize = m_rawstbuf.st_blksize;
                        m_blockcount = m_rawstbuf.st_blocks;
                    #else
                        m_blocksize = 8;
                        m_blockcount = (1024 * 4);
                    #endif
                    m_filesize = m_rawstbuf.st_size;
                    m_modename = modeToName(m_mode);
                    m_tmlastchanged = (&m_rawstbuf.st_ctime);
                    m_tmlastaccessed = (&m_rawstbuf.st_atime);
                    m_tmlastmodified = (&m_rawstbuf.st_mtime);
                    return true;
                }

            public:
                bool fromPath(const char* path)
                {
                    if(stat(path, &m_rawstbuf) == -1)
                    {
                        return false;
                    }
                    return fillout();
                }
        };

        class FSDirReader
        {
            public:
                struct Item
                {
                    bool isdir;
                    bool isfile;
                    size_t namelength;
                    char namedata[NEON_CONF_OSPATHSIZE + 1];

                };

            public:
                #if defined(NEON_PLAT_ISLINUX)
                    DIR* m_handle;
                #else
                    WIN32_FIND_DATA m_finddata;
                    HANDLE m_findhnd;
                #endif

            public:
                bool openDir(const char* path)
                {
                    #if defined(NEON_PLAT_ISLINUX)
                        if((m_handle = opendir(path)) == NULL)
                        {
                            return false;
                        }
                        return true;
                    #else
                        size_t msz;
                        size_t plen;
                        char* itempattern;
                        /*
                        * dumb-as-shit windows has AI, but retarded API:
                        * unlike dirent.h, the method for reading items in a directory
                        * requires '<path>' + "/" "*"', that is; one must add a glob character.
                        * no idea if this interferes with dot files.
                        */
                        plen = strlen(path);
                        msz = (sizeof(char) * (plen + 5));
                        itempattern = (char*)Memory::sysMalloc(msz);
                        memset(itempattern, 0, msz);
                        strncat(itempattern, path, plen);
                        {
                            strncat(itempattern, "/*", 2);
                        }
                        m_findhnd = FindFirstFile(itempattern, &m_finddata);
                        Memory::sysFree(itempattern);
                        if(INVALID_HANDLE_VALUE == m_findhnd)
                        {
                           return false;
                        }
                        return true;
                    #endif
                    return false;
                }

                bool readItem(Item* itm)
                {
                    #if defined(NEON_PLAT_ISLINUX)
                        struct dirent* ent;
                    #else
                        int ok;
                    #endif
                    itm->isdir = false;
                    itm->isfile = false;
                    itm->namelength = 0;
                    memset(itm->namedata, 0, NEON_CONF_OSPATHSIZE);
                    #if defined(NEON_PLAT_ISLINUX)
                        if((ent = readdir((DIR*)(m_handle))) == NULL)
                        {
                            return false;
                        }
                        if(ent->d_type == DT_DIR)
                        {
                            itm->isdir = true;
                        }
                        if(ent->d_type == DT_REG)
                        {
                            itm->isfile = true;
                        }
                        strcpy(itm->namedata, ent->d_name);
                        itm->namelength = ent->d_reclen;
                        return true;
                    #else
                        ok = FindNextFile(m_findhnd, &m_finddata);
                        if(ok != 0)
                        {
                            if(INVALID_HANDLE_VALUE == m_findhnd)
                            {
                                return false;
                            }
                            if(m_finddata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                            {
                                itm->isfile = false;
                                itm->isdir = true;
                            }
                            else
                            {
                                itm->isfile = true;
                                itm->isdir = false;
                            }
                            itm->namelength = strlen(m_finddata.cFileName);
                            strcpy(itm->namedata, m_finddata.cFileName);
                            return true;
                        }
                    #endif
                    return false;
                }

                bool closeDir()
                {
                    #if defined(NEON_PLAT_ISLINUX)
                        closedir((DIR*)(m_handle));
                    #else
                        FindClose(m_findhnd);
                    #endif
                    return false;
                }

        };

        union DblUnion
        {
            uint64_t bits;
            double num;
        };

        static int osfn_gettimeofday(struct timeval* tp, void* tzp);

        size_t roundUpToPowe64(uint64_t x)
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

        NEON_INLINE uint32_t hashBits(uint64_t hs)
        {
            /*
            // From v8's ComputeLongHash() which in turn cites:
            // Thomas Wang, Integer Hash Functions.
            // http://www.concentric.net/~Ttwang/tech/inthash.htm
            // hs = (hs << 18) - hs - 1;
            */
            hs = ~hs + (hs << 18);
            hs = hs ^ (hs >> 31);
            /* hs = (hs + (hs << 2)) + (hs << 4); */
            hs = hs * 21;
            hs = hs ^ (hs >> 11);
            hs = hs + (hs << 6);
            hs = hs ^ (hs >> 22);
            return (uint32_t)(hs & 0x3fffffff);
        }

        NEON_INLINE uint32_t hashDouble(double value)
        {
            DblUnion bits;
            bits.num = value;
            return hashBits(bits.bits);
        }

        NEON_INLINE uint32_t hashString(const char* str, size_t length)
        {
            /* Source: https://stackoverflow.com/a/21001712 */
            size_t ci;
            unsigned int byte;
            unsigned int crc;
            unsigned int mask;
            int i = 0;
            int j;
            crc = 0xFFFFFFFF;
            ci = 0;
            while(ci < length)
            {
                byte = str[i];
                crc = crc ^ byte;
                for(j = 7; j >= 0; j--)
                {
                    mask = -(crc & 1);
                    crc = (crc >> 1) ^ (0xEDB88320 & mask);
                }
                i = i + 1;
                ci++;
            }
            return ~crc;
        }

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

        char* utf8Encode(unsigned int code, size_t* dlen)
        {
            int count;
            char* chars;
            *dlen = 0;
            count = utf8NumBytes((int)code);
            if(NEON_LIKELY(count > 0))
            {
                *dlen = count;
                chars = (char*)Memory::sysMalloc(sizeof(char) * ((size_t)count + 1));
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

        char* utf8Codepoint(const char* str, char* outcodepoint)
        {
            if(0xf0 == (0xf8 & str[0]))
            {
                /* 4 byte utf8 codepoint */
                *outcodepoint = (((0x07 & str[0]) << 18) | ((0x3f & str[1]) << 12) | ((0x3f & str[2]) << 6) | (0x3f & str[3]));
                str += 4;
            }
            else if(0xe0 == (0xf0 & str[0]))
            {
                /* 3 byte utf8 codepoint */
                *outcodepoint = (((0x0f & str[0]) << 12) | ((0x3f & str[1]) << 6) | (0x3f & str[2]));
                str += 3;
            }
            else if(0xc0 == (0xe0 & str[0]))
            {
                /* 2 byte utf8 codepoint */
                *outcodepoint = (((0x1f & str[0]) << 6) | (0x3f & str[1]));
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

        char* utf8Strstr(const char* haystack, const char* needle)
        {
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
            while('\0' != *haystack)
            {
                maybematch = haystack;
                n = needle;
                while(*haystack == *n && (*haystack != '\0' && *n != '\0'))
                {
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
                    haystack = utf8Codepoint(maybematch, &throwawaycodepoint);
                }
            }
            /* no match */
            return nullptr;
        }

        /*
        // returns a pointer to the beginning of the pos'th utf8 codepoint
        // in the buffer at s
        */
        char* utf8Index(char* s, int pos)
        {
            ++pos;
            for(; *s; ++s)
            {
                if((*s & 0xC0) != 0x80)
                {
                    --pos;
                }
                if(pos == 0)
                {
                    return s;
                }
            }
            return nullptr;
        }

        /*
        // converts codepoint indexes start and end to byte offsets in the buffer at s
        */
        void utf8Slice(char* s, int* start, int* end)
        {
            char* p;
            p = utf8Index(s, *start);
            if(p != nullptr)
            {
                *start = (int)(p - s);
            }
            else
            {
                *start = -1;
            }
            p = utf8Index(s, *end);
            if(p != nullptr)
            {
                *end = (int)(p - s);
            }
            else
            {
                *end = (int)strlen(s);
            }
        }

        char* stringDup(const char* src, size_t len)
        {
            char* buf;
            buf = (char*)Memory::sysMalloc(sizeof(char) * (len + 1));
            if(buf == nullptr)
            {
                return nullptr;
            }
            memset(buf, 0, len + 1);
            memcpy(buf, src, len);
            return buf;
        }

        char* stringDup(const char* src)
        {
            return stringDup(src, strlen(src));
        }

        void mtrandSeed(uint32_t seed, uint32_t* binst, uint32_t* index)
        {
            uint32_t i;
            binst[0] = seed;
            for(i = 1; i < CONF_MERSENNESTATESIZE; i++)
            {
                binst[i] = (uint32_t)(1812433253UL * (binst[i - 1] ^ (binst[i - 1] >> 30)) + i);
            }
            *index = CONF_MERSENNESTATESIZE;
        }

        uint32_t mtrandGenerate(uint32_t* binst, uint32_t* index)
        {
            uint32_t i;
            uint32_t y;
            if(*index >= CONF_MERSENNESTATESIZE)
            {
                for(i = 0; i < CONF_MERSENNESTATESIZE - 397; i++)
                {
                    y = (binst[i] & 0x80000000) | (binst[i + 1] & 0x7fffffff);
                    binst[i] = binst[i + 397] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
                }
                for(; i < CONF_MERSENNESTATESIZE - 1; i++)
                {
                    y = (binst[i] & 0x80000000) | (binst[i + 1] & 0x7fffffff);
                    binst[i] = binst[i + (397 - CONF_MERSENNESTATESIZE)] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
                }
                y = (binst[CONF_MERSENNESTATESIZE - 1] & 0x80000000) | (binst[0] & 0x7fffffff);
                binst[CONF_MERSENNESTATESIZE - 1] = binst[396] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
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

        double mtrandRandom(double lowerlimit, double upperlimit)
        {
            double randnum;
            uint32_t randval;
            struct timeval tv;
            static uint32_t mtstate[CONF_MERSENNESTATESIZE];
            static uint32_t mtindex = CONF_MERSENNESTATESIZE + 1;
            if(mtindex >= CONF_MERSENNESTATESIZE)
            {
                osfn_gettimeofday(&tv, nullptr);
                mtrandSeed((uint32_t)(1000000 * tv.tv_sec + tv.tv_usec), mtstate, &mtindex);
            }
            randval = mtrandGenerate(mtstate, &mtindex);
            randnum = lowerlimit + ((double)randval / UINT32_MAX) * (upperlimit - lowerlimit);
            return randnum;
        }

        char* stringToUpper(char* str, size_t length)
        {
            int c;
            size_t i;
            for(i = 0; i < length; i++)
            {
                c = str[i];
                str[i] = toupper(c);
            }
            return str;
        }

        char* stringToLower(char* str, size_t length)
        {
            int c;
            size_t i;
            for(i = 0; i < length; i++)
            {
                c = str[i];
                str[i] = toupper(c);
            }
            return str;
        }

        int osfn_chmod(const char* path, int mode)
        {
            #if defined(NEON_PLAT_ISLINUX)
                return chmod(path, mode);
            #else
                return -1;
            #endif
        }

        char* osfn_realpath(const char* path, char* respath)
        {
            char* copy;
            #if defined(NEON_PLAT_ISLINUX)
                char* rt;
                rt = realpath(path, respath);
                if(rt != NULL)
                {
                    copy = Util::stringDup(rt);
                    free(rt);
                    return copy;
                }
            #else
            #endif
            copy = Util::stringDup(path);
            respath = copy;
            return copy;

        }

        char* osfn_dirname(const char *fname)
        {
            size_t dirlen;
            char * dirpart;
            const char *p;
            const char *slash;
            p = fname;
            slash = NULL;
            if(fname)
            {
                if(*fname && fname[1] == ':')
                {
                    slash = fname + 1;
                    p += 2;
                }
                /* Find the rightmost slash.  */
                while (*p)
                {
                    if (*p == '/' || *p == '\\')
                    {
                        slash = p;
                    }
                    p++;
                }
                if(slash == NULL)
                {
                    fname = ".";
                    dirlen = 1;
                }
                else
                {
                    /* Remove any trailing slashes.  */
                    while(slash > fname && (slash[-1] == '/' || slash[-1] == '\\'))
                    {
                        slash--;
                    }
                    /* How long is the directory we will return?  */
                    dirlen = slash - fname + (slash == fname || slash[-1] == ':');
                    if (*slash == ':' && dirlen == 1)
                    {
                        dirlen += 2;
                    }
                }
                dirpart = (char *)Memory::sysMalloc(dirlen + 1);
                if(dirpart != NULL)
                {
                    strncpy(dirpart, fname, dirlen);
                    if (slash && *slash == ':' && dirlen == 3)
                    {
                        dirpart[2] = '.';	/* for "x:foo" return "x:." */
                    }
                    dirpart[dirlen] = '\0';
                }
                return dirpart;
            }
            return NULL;
        }

        char* osfn_fallbackbasename(const char* opath)
        {
            char* strbeg;
            char* strend;
            char* cpath;
            strend = cpath = (char*)opath;
            while(*strend)
            {
                strend++;
            }
            while(strend > cpath && strend[-1] == '/')
            {
                strend--;
            }
            strbeg = strend;
            while(strbeg > cpath && strbeg[-1] != '/')
            {
                strbeg--;
            }
            /* len = (strend - strbeg) */
            cpath = strbeg;
            cpath[(strend - strbeg)] = 0;
            return strbeg;
        }

        char* osfn_basename(const char* path)
        {
            #if defined(NEON_PLAT_ISLINUX)
                char* rt;
                rt = basename((char*)path);
                if(rt != NULL)
                {
                    return rt;
                }
            #else
            #endif
            return osfn_fallbackbasename(path);
        }

        int osfn_isatty(int fd)
        {
            #if defined(NEON_PLAT_ISLINUX)
                return isatty(fd);
            #else
                return 0;
            #endif
        }

        int osfn_symlink(const char* path1, const char* path2)
        {
            #if defined(NEON_PLAT_ISLINUX)
                return symlink(path1, path2);
            #else
                return -1;
            #endif
        }

        int osfn_symlinkat(const char* path1, int fd, const char* path2)
        {
            #if defined(NEON_PLAT_ISLINUX)
                return symlinkat(path1, fd, path2);
            #else
                return -1;
            #endif
        }

        char* osfn_getcwd(char* buf, size_t size)
        {
            #if defined(NEON_PLAT_ISLINUX)
                return getcwd(buf, size);
            #else
                #if defined(NEON_PLAT_ISWINDOWS)
                    GetCurrentDirectory(size, buf);
                    return buf;
                #endif
            #endif
            return NULL;

        }

        int osfn_lstat(const char* path, struct stat* buf)
        {
            #if defined(NEON_PLAT_ISLINUX)
                return lstat(path, buf);
            #else
                return stat(path, buf);
            #endif
        }

        int osfn_gettimeofday(struct timeval* tp, void* tzp)
        {
            #if defined(NEON_PLAT_ISLINUX)
                return gettimeofday(tp, tzp);
            #else
                return 0;
            #endif
        }

        int osfn_mkdir(const char* path, size_t mode)
        {
            #if defined(NEON_PLAT_ISLINUX)
                return mkdir(path, mode);
            #else
                return -1;
            #endif
        }

        int osfn_rmdir(const char* path)
        {
            #if defined(NEON_PLAT_ISLINUX)
                return rmdir(path);
            #else
                return -1;
            #endif
        }

        int osfn_unlink(const char* path)
        {
            #if defined(NEON_PLAT_ISLINUX)
                return unlink(path);
            #else
                return -1;
            #endif
        }

        const char* osfn_getenv(const char* key)
        {
            return getenv(key);
        }

        bool osfn_setenv(const char* key, const char* value, bool replace)
        {
            #if defined(NEON_PLAT_ISLINUX)
                return (setenv(key, value, replace) == 0);
            #else
                int errcode;
                size_t envsize;
                errcode = 0;
                if(!replace)
                {
                    envsize = 0;
                    errcode = getenv_s(&envsize, NULL, 0, key);
                    if(errcode || envsize) return errcode;
                }
                if(_putenv_s(key, value) == -1)
                {
                    return false;
                }
                return true;
            #endif
        }

        int osfn_chdir(const char* path)
        {
            #if defined(NEON_PLAT_ISLINUX)
                return chdir(path);
            #else
                return -1;
            #endif
        }

        int osfn_getpid()
        {
            #if defined(NEON_PLAT_ISLINUX)
                return getpid();
            #endif
            return -1;
        }

        int osfn_kill(int pid, int code)
        {
            #if defined(NEON_PLAT_ISLINUX)
                return kill(pid, code);
            #else
                return -1;
            #endif
        }

        bool nn_util_fsfileexists(const char* filepath)
        {
            #if !defined(NEON_PLAT_ISWINDOWS)
                return access(filepath, F_OK) == 0;
            #else
                struct stat st;
                if(stat(filepath, &st) == -1)
                {
                    return false;
                }
                return true;
            #endif
        }

        bool nn_util_fsfileistype(const char* filepath, int typ)
        {
            struct stat st;
            (void)filepath;
            if(stat(filepath, &st) == -1)
            {
                return false;
            }
            if(typ == 'f')
            {
                return S_ISREG(st.st_mode);
            }
            else if(typ == 'd')
            {
                return S_ISDIR(st.st_mode);
            }
            return false;
        }

        bool nn_util_fsfileisfile(const char* filepath)
        {
            return nn_util_fsfileistype(filepath, 'f');
        }

        bool nn_util_fsfileisdirectory(const char* filepath)
        {
            return nn_util_fsfileistype(filepath, 'd');
        }

        char* nn_util_fsgetbasename(const char* path)
        {
            return osfn_basename(path);
        }

        const char* filestat_ctimetostring(const time_t *timep)
        {
            size_t len;
            char* r;
            r = ctime(timep);
            len = strlen(r);
            if(r[len - 1] == '\n')
            {
                r[len - 1] = 0;
            }
            return r;
        }

        static int g_neon_ttycheck = -1;

        const char* termColor(Color tc)
        {
        #if !defined(NEON_CONFIG_FORCEDISABLECOLOR)
            int fdstdout;
            int fdstderr;
            if(g_neon_ttycheck == -1)
            {
                fdstdout = fileno(stdout);
                fdstderr = fileno(stderr);
                g_neon_ttycheck = (osfn_isatty(fdstderr) && osfn_isatty(fdstdout));
            }
            if(g_neon_ttycheck)
            {
                switch(tc)
                {
                    case NEON_COLOR_RESET:
                        return "\x1B[0m";
                    case NEON_COLOR_RED:
                        return "\x1B[31m";
                    case NEON_COLOR_GREEN:
                        return "\x1B[32m";
                    case NEON_COLOR_YELLOW:
                        return "\x1B[33m";
                    case NEON_COLOR_BLUE:
                        return "\x1B[34m";
                    case NEON_COLOR_MAGENTA:
                        return "\x1B[35m";
                    case NEON_COLOR_CYAN:
                        return "\x1B[36m";
                }
            }
        #else
            (void)tc;
        #endif
            return "";
        }
    }

    class Wrappers
    {
        public:
            static String* wrapClassGetName(Class* cl);
            static uint32_t wrapStrGetHash(String* os);
            static const char* wrapStrGetData(String* os);
            static size_t wrapStrGetLength(String* os);
            static String* wrapMakeFromStrBuf(const StrBuffer& buf, uint32_t hsv, size_t length);
            static String* wrapStringCopy(const char* data, size_t len);
            static String* wrapStringCopy(const char* str);
            static Blob* wrapGetBlobOfClosure(Function* fn);
            static size_t wrapGetArityOfClosure(Function* ofn);
            static size_t wrapGetArityOfFuncScript(Function* ofn);
            static size_t wrapGetBlobcountOfFuncScript(Function* ofn);
            static const char* wrapGetInstanceName(Instance* inst);
    };

    class utf8iterator_t
    {
        public:
            /*input string pointer */
            const char* m_plainstr;

            /* input string length */
            uint32_t m_plainlen;

            /* the codepoint, or char */
            uint32_t m_codepoint;

            /* character size in bytes */
            uint16_t m_charsize;

            /* current character position */
            uint32_t m_currpos;

            /* next character position */
            uint32_t m_nextpos;

            /* number of counter characters currently */
            uint32_t m_currcount;

        public:
            /* allows you to set a custom length. */
            utf8iterator_t(const char* ptr, uint32_t length)
            {
                m_plainstr = ptr;
                m_plainlen = length;
                m_codepoint = 0;
                m_currpos = 0;
                m_nextpos = 0;
                m_currcount = 0;
            }

            /* calculate the number of bytes a UTF8 character occupies in a string. */
            uint16_t charSize(const char* character)
            {
                if(character == nullptr)
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

            uint32_t converter(const char* character, uint16_t size)
            {
                uint16_t i;
                static uint32_t currcodepoint = 0;
                static const uint16_t g_utf8iter_table_unicode[] = { 0, 0, 0x1F, 0xF, 0x7, 0x3, 0x1 };
                if(size == 0)
                {
                    return 0;
                }
                if(character == nullptr)
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
                currcodepoint = g_utf8iter_table_unicode[size] & character[0];
                for(i = 1; i < size; i++)
                {
                    currcodepoint = currcodepoint << 6;
                    currcodepoint = currcodepoint | (character[i] & 0x3F);
                }
                return currcodepoint;
            }

            /* returns 1 if there is a character in the next position. If there is not, return 0. */
            uint16_t next()
            {
                const char* pointer;
                if(m_plainstr == nullptr)
                {
                    return 0;
                }
                if(m_nextpos < m_plainlen)
                {
                    m_currpos = m_nextpos;
                    /* Set Current Pointer */
                    pointer = m_plainstr + m_nextpos;
                    m_charsize = charSize(pointer);
                    if(m_charsize == 0)
                    {
                        return 0;
                    }
                    m_nextpos = m_nextpos + m_charsize;
                    m_codepoint = converter(pointer, m_charsize);
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

            /* return current character in UFT8 - no same that this.codepoint (not codepoint/unicode) */
            const char* getChar()
            {
                uint16_t i;
                const char* pointer;
                static char str[10];
                str[0] = '\0';
                if(m_plainstr == nullptr)
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

    template<typename CharT>
    class StrBufBasic
    {
        public:
            static StrBufBasic* fromPtr(StrBufBasic* sbuf, size_t len)
            {
                sbuf->m_length = 0;
                sbuf->m_capacity = 0;
                sbuf->m_data = nullptr;
                sbuf->m_isintern = false;
                if(len > 0)
                {
                    sbuf->m_capacity = Util::roundUpToPowe64(len + 1);
                    sbuf->m_data = (CharT*)Memory::sysMalloc(sbuf->m_capacity);
                    if(!sbuf->m_data)
                    {
                        return NULL;
                    }
                    sbuf->m_data[0] = '\0';
                }
                return sbuf;
            }

            static bool destroy(StrBufBasic* sb)
            {
                destroyFromPtr(sb);
                if(!sb->m_isintern)
                {
                    Memory::sysFree(sb);
                }
                return true;
            }

            static bool destroyFromPtr(StrBufBasic* sb)
            {
                if((sb->m_data != nullptr) && (sb->m_isintern == false))
                {
                    Memory::sysFree(sb->m_data);
                }
                return true;
            }

        public:
            CharT* m_data = nullptr;
            /* total length of this buffer */
            size_t m_length = 0;

            /* capacity should be >= length+1 to allow for \0 */
            size_t m_capacity = 0;

            /* if this instance is allocated or not, i.e., whether m_data points to const data */
            bool m_isintern = false;

        public:

            template<typename InputFirstT, typename InputSecondT>
            static inline auto utilMin(InputFirstT x, InputSecondT y)
            {
                return ((x < y) ? x : y);
            }

            static void exitOnError()
            {
                abort();
                exit(EXIT_FAILURE);
            }

            static inline void checkBufCapacity(CharT** buf, size_t* sizeptr, size_t len)
            {
                /* for nul byte */
                len++;
                if(*sizeptr < len)
                {
                    *sizeptr = Util::roundUpToPowe64(len);
                    /* fprintf(stderr, "sizeptr=%ld\n", *sizeptr); */
                    if((*buf = (CharT*)Memory::sysRealloc(*buf, ((*sizeptr) * sizeof(CharT)))) == NULL)
                    {
                        fprintf(stderr, "[%s:%i] Out of memory\n", __FILE__, __LINE__);
                        abort();
                    }
                }
            }

            inline void checkBoundsInsert(size_t pos) const
            {
                if(pos > m_length)
                {
                    fprintf(stderr, "StrBufBasic: out of bounds error [index: %zd, num_of_bits: %zd]\n", pos, m_length);
                    errno = EDOM;
                    exitOnError();
                }
            }

            /* Bounds check when reading a range (start+len < strlen is valid) */
            inline void checkBoundsReadRange(size_t start, size_t len) const
            {
                const CharT* endstr;
                if(start + len > m_length)
                {
                    endstr = (m_length > 5 ? "..." : "");
                    fprintf(stderr,"StrBufBasic: out of bounds error [start: %zd; length: %zd; strlen: %zd; buf:%.*s%s]\n", start, len, m_length, (int)utilMin(size_t(5), m_length), m_data, endstr);
                    errno = EDOM;
                    exitOnError();
                }
            }

            static bool inPlaceReplaceHelper(CharT *dest, const CharT *src, size_t srclen, int findme, const CharT* newsubstr, size_t sublen, size_t maxlen, size_t* dlen)
            {
                /* ch(ar) at pos(ition) */
                int chatpos;
                /* printf("'%p' '%s' %c\n", dest, src, findme); */
                if(*src == findme)
                {
                    if(sublen > maxlen)
                    {
                        return false;
                    }
                    if(!inPlaceReplaceHelper(dest + sublen, src + 1, srclen, findme, newsubstr, sublen, maxlen - sublen, dlen))
                    {
                        return false;
                    }
                    memcpy(dest, newsubstr, sublen);
                    *dlen += sublen;
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
                    if(!inPlaceReplaceHelper(dest + 1, src + 1, srclen, findme, newsubstr, sublen, maxlen - 1, dlen))
                    {
                        return false;
                    }
                }
                *dest = chatpos;
                return true;
            }

            /*
            * right now, this is only good for replacing a single character with any-length string.
            * kind of lazy, but it works.
            */
            static size_t inPlaceReplaceUtil(CharT* target, size_t tgtlen, int findme, const CharT* newsubstr, size_t sublen, size_t maxlen)
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
                if(*newsubstr == 0)
                {
                    /* Insure target does not shrink. */
                    return 0;
                }
                nlen = 0;
                inPlaceReplaceHelper(target, target, tgtlen, findme, newsubstr, sublen, maxlen - 1, &nlen);
                return nlen;
            }

        private:
            /* Ensure capacity for len characters plus '\0' character - exits on FAILURE */
            NEON_INLINE void ensureCapacity(size_t len)
            {
                checkBufCapacity(&m_data, &m_capacity, len);
            }

            /* Same as above, but update pointer if it pointed to resized array */
            NEON_INLINE void ensureCapacityUpdatePtr(size_t size, const CharT** ptr)
            {
                size_t oldcap;
                CharT* oldbuf;
                if(m_capacity <= size + 1)
                {
                    oldcap = m_capacity;
                    oldbuf = m_data;
                    if(!resize(size))
                    {
                        fprintf(stderr, "StrBufBasic:Error: failed to resize [requested %zd bytes; capacity: %zd bytes]\n", size, m_capacity);
                        exitOnError();
                        return;
                    }
                    /* ptr may have pointed to this, which has now moved */
                    if(*ptr >= oldbuf && *ptr < oldbuf + oldcap)
                    {
                        *ptr = m_data + (*ptr - oldbuf);
                    }
                }
            }

            NEON_INLINE void initBlank()
            {
                m_length = 0;
                m_capacity = 0;
                m_isintern = false;
                m_data = NULL;

            }

        public:
            NEON_INLINE StrBufBasic()
            {
                initBlank();
            }

            NEON_INLINE StrBufBasic(size_t len)
            {
                initBlank();
                assert(fromPtr(this, len));
            }

            NEON_INLINE ~StrBufBasic()
            {
            }

            /* Clear the content of an existing StrBufBasic (sets size to 0) */
            NEON_INLINE void reset()
            {
                if(m_data)
                {
                    memset(m_data, 0, m_length);
                }
                m_length = 0;
            }

            NEON_INLINE bool contains(CharT ch)
            {
                size_t i;
                for(i=0; i<m_length; i++)
                {
                    if(m_data[i] == ch)
                    {
                        return true;
                    }
                }
                return false;
            }

            NEON_INLINE bool replace(int findme, const CharT* newsubstr, size_t sublen)
            {
                size_t i;
                size_t nlen;
                size_t needed;
                needed = m_capacity;
                for(i=0; i<m_length; i++)
                {
                    if(m_data[i] == findme)
                    {
                        needed += sublen;
                    }
                }
                if(!resize(needed+1))
                {
                    return false;
                }
                nlen = inPlaceReplaceUtil(m_data, m_length, findme, newsubstr, sublen, m_capacity);
                m_length = nlen;
                return true;
            }

            NEON_INLINE bool replace(StrBufBasic* targetbuf, const CharT* findmestr, size_t findmelen, const CharT* repwithstr, size_t repwithlen)
            {
                size_t i;
                if((length() == 0 && findmelen == 0) || length() == 0 || findmelen == 0)
                {
                    return false;
                }
                for(i = 0; i < length(); i++)
                {
                    if(memcmp(data() + i, findmestr, findmelen) == 0)
                    {
                        if(findmelen > 0)
                        {
                            targetbuf->append(repwithstr, repwithlen);
                        }
                        i += findmelen - 1;
                    }
                    else
                    {
                        targetbuf->append(get(i));
                    }
                }
                return true;
            }

            NEON_INLINE void set(size_t idx, int b)
            {
                m_data[idx] = b;
            }

            NEON_INLINE int get(size_t idx)
            {
                return m_data[idx];
            }

            /*
            // Resize the buffer to have capacity to hold a string of length newlen
            // (+ a null terminating character).  Can also be used to downsize the buffer's
            // memory usage.  Returns 1 on success, 0 on failure.
            */
            NEON_INLINE bool resize(size_t newlen)
            {
                size_t cap;
                CharT* newbuf;
                cap = Util::roundUpToPowe64(newlen + 1);
                newbuf = (CharT*)Memory::sysRealloc(m_data, cap * sizeof(CharT));
                if(newbuf == NULL)
                {
                    return false;
                }
                m_data = newbuf;
                m_capacity = cap;
                if(m_length > newlen)
                {
                    /* Buffer was shrunk - re-add null byte */
                    m_length = newlen;
                    m_data[m_length] = '\0';
                }
                return true;
            }

            size_t length() const
            {
                return m_length;
            }

            CharT* data() const
            {
                return m_data;
            }

            void setData(const CharT* str)
            {
                m_data = (CharT*)str;
            }

            void setLength(size_t sz)
            {
                m_length = sz;
            }

            /*
            // Get a substring as a new null terminated CharT array
            // (remember to free the returned CharT* after you're done with it!)
            */
            CharT* substr(size_t start, size_t len)
            {
                CharT* newstr;
                checkBoundsReadRange(start, len);
                newstr = (CharT*)Memory::sysMalloc((len + 1) * sizeof(CharT));
                strncpy(newstr, m_data + start, len);
                newstr[len] = '\0';
                return newstr;
            }

            /*
            // Copy N characters from a character array to the end of this StrBufBasic
            // strlen(str) must be >= len
            */
            bool append(const CharT* str, size_t len)
            {
                ensureCapacityUpdatePtr(m_length + len, &str);
                memcpy(m_data + m_length, str, len);
                m_data[m_length = m_length + len] = '\0';
                return true;
            }

            bool append(const CharT* str)
            {
                return append(str, strlen(str));
            }

            bool append(int b)
            {
                CharT ch = b;
                return append(&ch, 1);
            }

            template<typename... ArgsT>
            int appendFormatAtPos(size_t pos, const CharT* fmt, ArgsT&&... args)
            {
                static auto wrapsnprintf = snprintf;
                static auto wrapsprintf = sprintf;
                size_t buflen;
                int numchars;
                checkBoundsInsert(pos);
                /* Length of remaining buffer */
                buflen = m_capacity - pos;
                if(buflen == 0 && !resize(m_capacity << 1))
                {
                    fprintf(stderr, "%s:%i:Error: Out of memory\n", __FILE__, __LINE__);
                    exitOnError();
                }
                /* Make a copy of the list of args incase we need to resize buff and try again */
                numchars = wrapsnprintf(m_data + pos, buflen, fmt, args...);
                /*
                // numchars is the number of chars that would be written (not including '\0')
                // numchars < 0 => failure
                */
                if(numchars < 0)
                {
                    fprintf(stderr, "Warning: dyn_strbuf_appendformatv something went wrong..\n");
                    exitOnError();
                }
                /* numchars does not include the null terminating byte */
                if((size_t)numchars + 1 > buflen)
                {
                    ensureCapacity(pos + (size_t)numchars);
                    /*
                    // now use the argptr copy we made earlier
                    // Don't need to use vsnprintf now, vsprintf will do since we know it'll fit
                    */
                    numchars = wrapsprintf(m_data + pos, fmt, args...);
                    if(numchars < 0)
                    {
                        fprintf(stderr, "Warning: dyn_strbuf_appendformatv something went wrong..\n");
                        exitOnError();
                    }
                }
                /*
                // Don't need to NUL terminate, vsprintf/vnsprintf does that for us
                // Update m_length
                */
                m_length = pos + (size_t)numchars;
                return numchars;
            }

            /* sprintf to the end of a StrBufBasic (adds string terminator after sprint) */
            template<typename... ArgsT>
            int appendFormat(const CharT* fmt, ArgsT&&... args)
            {
                int numchars;
                numchars = appendFormatAtPos(m_length, fmt, args...);
                return numchars;
            }
    };


    /************

        REMIMU: SINGLE HEADER C/C++ REGEX LIBRARY

    PERFORMANCE

        On simple cases, Remimu's match speed is similar to PCRE2.
        Regex parsing/compilation is also much faster (around 4x to 10x), so single-shot regexes are often faster than PCRE2.
        HOWEVER: Remimu is a pure backtracking engine, and has `O(2^x)` complexity on regexes with catastrophic backtracking.
        It can be much, much, MUCH slower than PCRE2. Beware!
        Remimu uses length-checked fixed memory buffers with no recursion, so memory usage is statically known.

    FEATURES

        - Lowest-common-denominator common regex syntax
        - Based on backtracking (slow in the worst case, but fast in the best case)
        - 8-bit only, no utf-16 or utf-32
        - Statically known memory usage (no heap allocation or recursion)
        - Groups with or without capture, and with or without quantifiers
        - Supported escapes:
        - - 2-digit hex: e.g. \x00, \xFF, or lowercase, or mixed case
        - - \r, \n, \t, \v, \f (whitespace characters)
        - - \d, \s, \w, \D, \S, \W (digit, space, and word character classes)
        - - \b, \B word boundary and non-word-boundary anchors (not fully supported in zero-size quantified groups, but even then, usually supported)
        - - Escaped literal characters: {}[]-()|^$*+?:./\
        - - - Escapes work in character classes, except for'b'
        - Character classes, including disjoint ranges, proper handling of bare [ and trailing -, etc
        - - Dot (.) matches all characters, including newlines, unless FLAG_DOTNONEWLINES is passed as a flag to parse()
        - - Dot (.) only matches at most one byte at a time, so matching \r\n requires two dots (and not using FLAG_DOTNONEWLINES)
        - Anchors (^ and $)
        - - Same support caveats as \b, \B apply
        - Basic quantifiers (*, +, ?)
        - - Quantifiers are greedy by default.
        - Explicit quantifiers ({2}, {5}, {5,}, {5,7})
        - Alternation e.g. (asdf|foo)
        - Lazy quantifiers e.g. (asdf)*? or \w+?
        - Possessive greedy quantifiers e.g. (asdf)*+ or \w++
        - - NOTE: Capture groups forand inside of possessive groups return no capture information.
        - Atomic groups e.g. (?>(asdf))
        - - NOTE: Capture groups inside of atomic groups return no capture information.

    NOT SUPPORTED

        - Strings with non-terminal null characters
        - Unicode character classes (matching single utf-8 characters works regardless)
        - Exact POSIX regex semantics (posix-style greediness etc)
        - Backreferences
        - Lookbehind/Lookahead
        - Named groups
        - Most other weird flavor-specific regex stuff
        - Capture of or inside of possessive-quantified groups (still take up a capture slot, but no data is returned)

    USAGE

        // minimal:

        RegexContext ctx;
        RegexContext::Token tokens[1024];
        int16_t tokencount = 1024;
        RegexContext::initStack(&ctx);
        int e = ctx.parse("[0-9]+\\.[0-9]+", tokens, &tokencount, 0);
        assert(!e);

        int64_t matchlen = ctx.match(tokens, "23.53) ", 0, 0, 0, 0);
        printf("########### return: %d\n", matchlen);

        // with captures:
        RegexContext ctx;
        RegexContext::Token tokens[256];
        int16_t tokencount = sizeof(tokens)/sizeof(tokens[0]);
        RegexContext::initStack(&xtx);
        int e = ctx.parse("((a)|(b))++", tokens, &tokencount, 0);
        assert(!e);

        int64_t cappos[5];
        int64_t capspan[5];
        memset(cappos, 0xFF, sizeof(cappos));
        memset(capspan, 0xFF, sizeof(capspan));

        int64_t matchlen = ctx.match(tokens, "aaaaaabbbabaqa", 0, 5, cappos, capspan);
        printf("Match length: %d\n", matchlen);
        for(int i = 0; i < 5; i++)
            printf("Capture %d: %d plus %d\n", i, cappos[i], capspan[i]);
        RegexContext::printTokens(tokens);

    LICENSE
        Creative Commons Zero, public domain.
    */

    struct RegexContext
    {
        public:
            enum
            {
                FLAG_DOTNONEWLINES = 1
            };

            enum
            {
                RXTOKTYP_NORMAL = 0,
                RXTOKTYP_OPEN = 1,
                RXTOKTYP_NCOPEN = 2,
                RXTOKTYP_CLOSE = 3,
                RXTOKTYP_OR = 4,
                RXTOKTYP_CARET = 5,
                RXTOKTYP_DOLLAR = 6,
                RXTOKTYP_BOUND = 7,
                RXTOKTYP_NBOUND = 8,
                RXTOKTYP_END = 9
            };

            enum
            {
                RXTOKMODE_POSSESSIVE = 1,
                RXTOKMODE_LAZY = 2,
                /*  temporary; gets cleared later */
                RXTOKMODE_INVERTED = 128
            };

            enum
            {
                /*
                0: init
                1: normal
                2: in char class, initial state
                3: in char class, but possibly looking fora range marker
                4: in char class, but just saw a range marker
                5: immediately after quantifiable token
                6: immediately after quantifier
                */
                RXSTATE_NORMAL = 1,
                RXSTATE_QUANT = 2,
                RXSTATE_MODE = 3,
                RXSTATE_CCINIT = 4,
                RXSTATE_CCNORMAL = 5,
                RXSTATE_CCRANGE = 6
            };

            enum
            {
                stacksizemax = 1024,
                auxstatssize = 1024
            };

            struct Token
            {
                uint8_t kind;
                uint8_t mode;
                uint16_t count_lo;
                /*  0 means no limit */
                uint16_t count_hi;
                /*  forgroups: mask 0 stores group-with-quantifier number (quantifiers are +, *, ?, {n}, {n,}, or {n,m}) */
                uint16_t mask[16];
                /*  from ( or ), offset in token list to matching paren. TODO: move into mask maybe */
                int16_t pair_offset;
            };

            struct MState
            {
                uint32_t k;
                uint32_t group_state; /*  quantified group temp state (e.g. number of repetitions) */
                uint32_t prev; /*  for)s, stack index of corresponding previous quantified state */
                uint64_t i;
                uint64_t range_min;
                uint64_t range_max;
            };

        public:
            RegexContext()
            {
                m_isallocated = false;
                m_haderror = false;
                m_tokens = nullptr;
                m_maxtokens = 0;
            }

            RegexContext(Token* tokens, size_t maxtokens)
            {
                m_isallocated = false;
                m_haderror = false;
                m_tokens = tokens;
                m_maxtokens = maxtokens;
            }

            static void destroy(RegexContext* ctx)
            {
                (void)ctx;
            }

            static NEON_INLINE bool checkIsW(uint64_t* wmask, int byte)
            {
                return (!!(wmask[((uint8_t)byte) >> 4] & (1 << ((uint8_t)byte & 0xF))));
            }

            /*  NOTE: undef'd later */
            static NEON_INLINE int checkMask(Token* tokens, size_t k, uint8_t byte)
            {
                return (!!(tokens[k].mask[((uint8_t)byte) >> 4] & (1 << ((uint8_t)byte & 0xF))));
            }

            static void printCSmart(int c)
            {
                if(c >= 0x20 && c <= 0x7E)
                {
                    printf("%c", c);
                }
                else
                {
                    printf("\\x%02x", c);
                }
            }

            static void printTokens(Token* tokens)
            {
                int c;
                int k;
                int cold;
                static const char* kindtostr[] = {
                    "NORMAL", "OPEN", "NCOPEN", "CLOSE", "OR", "CARET", "DOLLAR", "BOUND", "NBOUND", "END",
                };
                static const char* modetostr[] = {
                    "GREEDY",
                    "POSSESS",
                    "LAZY",
                };
                for(k = 0;; k++)
                {
                    printf("%s\t%s\t", kindtostr[tokens[k].kind], modetostr[tokens[k].mode]);
                    cold = -1;
                    for(c = 0; c < (tokens[k].kind ? 0 : 256); c++)
                    {
                        if(checkMask(tokens, k, c))
                        {
                            if(cold == -1)
                            {
                                cold = c;
                            }
                        }
                        else if(cold != -1)
                        {
                            if(c - 1 == cold)
                            {
                                printCSmart(cold);
                                cold = -1;
                            }
                            else if(c - 2 == cold)
                            {
                                printCSmart(cold);
                                printCSmart(cold + 1);
                                cold = -1;
                            }
                            else
                            {
                                printCSmart(cold);
                                printf("-");
                                printCSmart(c - 1);
                                cold = -1;
                            }
                        }
                    }
                    /*
                    printf("\t");
                    for(int i = 0; i < 16; i++)
                        printf("%04x", tokens[k].mask[i]);
                    */
                    printf("\t{%d,%d}\t(%d)\n", tokens[k].count_lo, tokens[k].count_hi - 1, tokens[k].pair_offset);
                    if(tokens[k].kind == RXTOKTYP_END)
                    {
                        break;
                    }
                }
            }

        public:
            bool m_haderror;
            bool m_isallocated;
            size_t m_maxtokens;
            size_t m_tokencount;
            char m_errorbuf[1024];
            Token* m_tokens;

        public:
            template<typename... ArgsT>
            NEON_INLINE void setError(const char* fmt, ArgsT&&... args)
            {
                static constexpr auto tmpsprintf = sprintf;
                m_haderror = true;
                fprintf(stderr, "ERROR: ");
                tmpsprintf(m_errorbuf, fmt, args...);
            }

            bool rewindOrFail(uint64_t& k, uint32_t& i, uint16_t& stackn, uint8_t& justrewinded, uint64_t& range_min, uint64_t& range_max, uint32_t* qgroupstack, uint32_t* qgroupstate, MState* rewindstack)
            {
                if(stackn == 0)
                {
                    return false;
                }
                stackn -= 1;
                while(stackn > 0 && rewindstack[stackn].prev == 0xFAC7)
                {
                    stackn -= 1;
                }
                justrewinded = 1;
                range_min = rewindstack[stackn].range_min;
                range_max = rewindstack[stackn].range_max;
                assert(rewindstack[stackn].i <= i);
                i = rewindstack[stackn].i;
                k = rewindstack[stackn].k;
                if(m_tokens[k].kind == RXTOKTYP_CLOSE)
                {
                    qgroupstate[m_tokens[k].mask[0]] = rewindstack[stackn].group_state;
                    qgroupstack[m_tokens[k].mask[0]] = rewindstack[stackn].prev;
                }
                k -= 1;
                return true;
            }

            bool rewindDoSaveRaw(uint32_t k, bool isdum, uint64_t& i, uint16_t& stackn, uint64_t& range_min, uint64_t& range_max, uint32_t* qgroupstack, uint32_t* qgroupstate, MState* rewindstack)
            {
                MState s;
                if(stackn >= stacksizemax)
                {
                    setError("out of backtracking room. returning");
                    return false;
                }
                memset(&s, 0, sizeof(MState));
                s.i = i;
                s.k = (k);
                s.range_min = range_min;
                s.range_max = range_max;
                s.prev = 0;
                if(isdum)
                {
                    s.prev = 0xFAC7;
                }
                else if(m_tokens[s.k].kind == RXTOKTYP_CLOSE)
                {
                    s.group_state = qgroupstate[m_tokens[s.k].mask[0]];
                    s.prev = qgroupstack[m_tokens[s.k].mask[0]];
                    qgroupstack[m_tokens[s.k].mask[0]] = stackn;
                }
                rewindstack[stackn++] = s;
                return true;
            }

            /*
             Returns a negative number on failure:
             -1: Regex string is invalid or using unsupported features or too long.
             -2: Provided buffer not long enough. Give up, or reallocate with more length and retry.
              Returns 0 on success.
              On call, m_tokencount pointer must point to the number of tokens that can be written to the m_tokens buffer.
              On successful return, the number of actually used tokens is written to tokencount.
              Sets m_tokencount to zero ifa regex is not created but no error happened (e.g. empty pattern).
              Flags: Not yet used.
              SAFETY: Pattern must be null-terminated.
              SAFETY: m_tokens buffer must have at least the input m_tokencount number of Token objects. They are allowed to be uninitialized.
            */
            NEON_INLINE int parse(const char* pattern, int32_t flags)
            {
                int escstate;
                int state;
                int charclassmem;
                int parencount;
                int16_t k;
                int64_t tokenslen;
                uint64_t i;
                uint64_t mi;
                uint64_t patternlen;
                uint8_t clsi;
                char c;
                ptrdiff_t l;
                int macn;
                uint32_t val;
                uint32_t val2;
                uint8_t escc;
                uint8_t n0;
                uint8_t n1;
                uint8_t isupper;
                uint64_t n;
                int16_t k3;
                int16_t k2;
                ptrdiff_t diff;
                int balance;
                ptrdiff_t found;
                uint16_t m[16];
                Token token;
                tokenslen = m_maxtokens;
                patternlen = strlen(pattern);
                if(m_maxtokens == 0)
                {
                    return -2;
                }
                /*
                0: normal
                1: just saw a backslash
                */
                escstate = 0;
                state = RXSTATE_NORMAL;
                charclassmem = -1;
                clearToken(&token);
                k = 0;
                /*
                start with an invisible group specifier
                (this allows the matcher to not need to have a special root-level alternation operator case)
                */
                token.kind = RXTOKTYP_OPEN;
                token.count_lo = 0;
                token.count_hi = 0;
                parencount = 0;
                for(i = 0; i < patternlen; i++)
                {
                    c = pattern[i];
                    if(state == RXSTATE_QUANT)
                    {
                        state = RXSTATE_MODE;
                        if(c == '?')
                        {
                            /* first non-allowed amount */
                            token.count_lo = 0;
                            token.count_hi = 2;
                            continue;
                        }
                        else if(c == '+')
                        {
                            /* unlimited */
                            token.count_lo = 1;
                            token.count_hi = 0;
                            continue;
                        }
                        else if(c == '*')
                        {
                            /* unlimited */
                            token.count_lo = 0;
                            token.count_hi = 0;
                            continue;
                        }
                        else if(c == '{')
                        {
                            if(pattern[i + 1] == 0 || pattern[i + 1] < '0' || pattern[i + 1] > '9')
                            {
                                state = RXSTATE_NORMAL;
                            }
                            else
                            {
                                i += 1;
                                val = 0;
                                while(pattern[i] >= '0' && pattern[i] <= '9')
                                {
                                    val *= 10;
                                    val += (uint32_t)(pattern[i] - '0');
                                    if(val > 0xFFFF)
                                    {
                                        /*  unsupported length */
                                        setError("quantifier range too long");
                                        return -1;
                                    }
                                    i += 1;
                                }
                                token.count_lo = val;
                                token.count_hi = val + 1;
                                if(pattern[i] == ',')
                                {
                                    token.count_hi = 0; /*  unlimited */
                                    i += 1;

                                    if(pattern[i] >= '0' && pattern[i] <= '9')
                                    {
                                        val2 = 0;
                                        while(pattern[i] >= '0' && pattern[i] <= '9')
                                        {
                                            val2 *= 10;
                                            val2 += (uint32_t)(pattern[i] - '0');
                                            if(val2 > 0xFFFF)
                                            {
                                                /*  unsupported length */
                                                setError("quantifier range too long");
                                                return -1;
                                            }
                                            i += 1;
                                        }
                                        if(val2 < val)
                                        {
                                            setError("quantifier range is backwards");
                                            return -1; /*  unsupported length */
                                        }
                                        token.count_hi = val2 + 1;
                                    }
                                }
                                if(pattern[i] == '}')
                                {
                                    /*  quantifier range parsed successfully */
                                    continue;
                                }
                                else
                                {
                                    setError("quantifier range syntax broken (no terminator)");
                                    return -1;
                                }
                            }
                        }
                    }
                    if(state == RXSTATE_MODE)
                    {
                        state = RXSTATE_NORMAL;
                        if(c == '?')
                        {
                            token.mode |= RXTOKMODE_LAZY;
                            continue;
                        }
                        else if(c == '+')
                        {
                            token.mode |= RXTOKMODE_POSSESSIVE;
                            continue;
                        }
                    }
                    if(state == RXSTATE_NORMAL)
                    {
                        if(escstate == 1)
                        {
                            escstate = 0;
                            if(c == 'n')
                            {
                                setMask(&token, '\n');
                            }
                            else if(c == 'r')
                            {
                                setMask(&token, '\r');
                            }
                            else if(c == 't')
                            {
                                setMask(&token, '\t');
                            }
                            else if(c == 'v')
                            {
                                setMask(&token, '\v');
                            }
                            else if(c == 'f')
                            {
                                setMask(&token, '\f');
                            }
                            else if(c == 'x')
                            {
                                if(pattern[i + 1] == 0 || pattern[i + 2] == 0)
                                {
                                    return -1; /*  too-short hex pattern */
                                }
                                n0 = pattern[i + 1];
                                n1 = pattern[i + 1];
                                if(n0 < '0' || n0 > 'f' || n1 < '0' || n1 > 'f' || (n0 > '9' && n0 < 'A') || (n1 > '9' && n1 < 'A'))
                                {
                                    setError("invalid hex digit");
                                    return -1; /*  invalid hex digit */
                                }
                                if(n0 > 'F')
                                {
                                    n0 -= 0x20;
                                }
                                if(n1 > 'F')
                                {
                                    n1 -= 0x20;
                                }
                                if(n0 >= 'A')
                                {
                                    n0 -= 'A' - 10;
                                }
                                if(n1 >= 'A')
                                {
                                    n1 -= 'A' - 10;
                                }
                                n0 -= '0';
                                n1 -= '0';
                                setMask(&token, (n1 << 4) | n0);
                                i += 2;
                            }
                            else if(isQuantChar(c))
                            {
                                setMask(&token, c);
                                state = RXSTATE_QUANT;
                            }
                            else if(c == 'd' || c == 's' || c == 'w' || c == 'D' || c == 'S' || c == 'W')
                            {
                                isupper = c <= 'Z';
                                memset(m, 0, sizeof(m));
                                if(isupper)
                                {
                                    c += 0x20;
                                }
                                if(c == 'd' || c == 'w')
                                {
                                    m[3] |= 0x03FF; /*  0~7 */
                                }
                                if(c == 's')
                                {
                                    m[0] |= 0x3E00; /*  \t-\r (includes \n, \v, and \f in the middle. 5 enabled bits.) */
                                    m[2] |= 1; /*  ' ' */
                                }
                                if(c == 'w')
                                {
                                    m[4] |= 0xFFFE; /*  A-O */
                                    m[5] |= 0x87FF; /*  P-Z_ */
                                    m[6] |= 0xFFFE; /*  a-o */
                                    m[7] |= 0x07FF; /*  p-z */
                                }
                                for(mi = 0; mi < 16; mi++)
                                {
                                    token.mask[mi] |= isupper ? ~m[mi] : m[mi];
                                }
                                token.kind = RXTOKTYP_NORMAL;
                                state = RXSTATE_QUANT;
                            }
                            else if(c == 'b')
                            {
                                token.kind = RXTOKTYP_BOUND;
                                state = RXSTATE_NORMAL;
                            }
                            else if(c == 'B')
                            {
                                token.kind = RXTOKTYP_NBOUND;
                                state = RXSTATE_NORMAL;
                            }
                            else
                            {
                                setError("unsupported escape sequence");
                                return -1; /*  unknown/unsupported escape sequence */
                            }
                        }
                        else
                        {
                            if(!pushToken(token, tokenslen, &k, &macn))
                            {
                                return -2;
                            }
                            if(c == '\\')
                            {
                                escstate = 1;
                            }
                            else if(c == '[')
                            {
                                state = RXSTATE_CCINIT;
                                charclassmem = -1;
                                token.kind = RXTOKTYP_NORMAL;
                                if(pattern[i + 1] == '^')
                                {
                                    token.mode |= RXTOKMODE_INVERTED;
                                    i += 1;
                                }
                            }
                            else if(c == '(')
                            {
                                parencount += 1;
                                state = RXSTATE_NORMAL;
                                token.kind = RXTOKTYP_OPEN;
                                token.count_lo = 0;
                                token.count_hi = 1;
                                if(pattern[i + 1] == '?' && pattern[i + 2] == ':')
                                {
                                    token.kind = RXTOKTYP_NCOPEN;
                                    i += 2;
                                }
                                else if(pattern[i + 1] == '?' && pattern[i + 2] == '>')
                                {
                                    token.kind = RXTOKTYP_NCOPEN;
                                    if(!pushToken(token, tokenslen, &k, &macn))
                                    {
                                        return -2;
                                    }
                                    state = RXSTATE_NORMAL;
                                    token.kind = RXTOKTYP_NCOPEN;
                                    token.mode = RXTOKMODE_POSSESSIVE;
                                    token.count_lo = 1;
                                    token.count_hi = 2;
                                    i += 2;
                                }
                            }
                            else if(c == ')')
                            {
                                parencount -= 1;
                                if(parencount < 0 || k == 0)
                                {
                                    setError("unbalanced parentheses");
                                    return -1; /*  unbalanced parens */
                                }
                                token.kind = RXTOKTYP_CLOSE;
                                state = RXSTATE_QUANT;
                                balance = 0;
                                found = -1;
                                for(l = k - 1; l >= 0; l--)
                                {
                                    if(m_tokens[l].kind == RXTOKTYP_NCOPEN || m_tokens[l].kind == RXTOKTYP_OPEN)
                                    {
                                        if(balance == 0)
                                        {
                                            found = l;
                                            break;
                                        }
                                        else
                                        {
                                            balance -= 1;
                                        }
                                    }
                                    else if(m_tokens[l].kind == RXTOKTYP_CLOSE)
                                    {
                                        balance += 1;
                                    }
                                }
                                if(found == -1)
                                {
                                    setError("unbalanced parentheses");
                                    return -1; /*  unbalanced parens */
                                }
                                diff = k - found;
                                if(diff > 32767)
                                {
                                    setError("difference too large");
                                    return -1; /*  too long */
                                }
                                token.pair_offset = -diff;
                                m_tokens[found].pair_offset = diff;
                                /*  phantom group foratomic group emulation */
                                if(m_tokens[found].mode == RXTOKMODE_POSSESSIVE)
                                {
                                    if(!pushToken(token, tokenslen, &k, &macn))
                                    {
                                        return -2;
                                    }
                                    token.kind = RXTOKTYP_CLOSE;
                                    token.mode = RXTOKMODE_POSSESSIVE;
                                    token.pair_offset = -diff - 2;
                                    m_tokens[found - 1].pair_offset = diff + 2;
                                }
                            }
                            else if(c == '?' || c == '+' || c == '*' || c == '{')
                            {
                                setError("quantifier in non-quantifier context");
                                return -1; /*  quantifier in non-quantifier context */
                            }
                            else if(c == '.')
                            {
                                /* puts("setting ALL of mask..."); */
                                macn = setMaskAll(&token, macn);
                                if(flags & FLAG_DOTNONEWLINES)
                                {
                                    token.mask[1] ^= 0x04; /*  \n */
                                    token.mask[1] ^= 0x20; /*  \r */
                                }
                                state = RXSTATE_QUANT;
                            }
                            else if(c == '^')
                            {
                                token.kind = RXTOKTYP_CARET;
                                state = RXSTATE_NORMAL;
                            }
                            else if(c == '$')
                            {
                                token.kind = RXTOKTYP_DOLLAR;
                                state = RXSTATE_NORMAL;
                            }
                            else if(c == '|')
                            {
                                token.kind = RXTOKTYP_OR;
                                state = RXSTATE_NORMAL;
                            }
                            else
                            {
                                setMask(&token, c);
                                state = RXSTATE_QUANT;
                            }
                        }
                    }
                    else if(state == RXSTATE_CCINIT || state == RXSTATE_CCNORMAL || state == RXSTATE_CCRANGE)
                    {
                        if(c == '\\' && escstate == 0)
                        {
                            escstate = 1;
                            continue;
                        }
                        escc = 0;
                        if(escstate == 1)
                        {
                            escstate = 0;
                            if(c == 'n')
                            {
                                escc = '\n';
                            }
                            else if(c == 'r')
                            {
                                escc = '\r';
                            }
                            else if(c == 't')
                            {
                                escc = '\t';
                            }
                            else if(c == 'v')
                            {
                                escc = '\v';
                            }
                            else if(c == 'f')
                            {
                                escc = '\f';
                            }
                            else if(c == 'x')
                            {
                                if(pattern[i + 1] == 0 || pattern[i + 2] == 0)
                                {
                                    setError("hex pattern too short");
                                    return -1; /*  too-short hex pattern */
                                }
                                n0 = pattern[i + 1];
                                n1 = pattern[i + 1];
                                if(n0 < '0' || n0 > 'f' || n1 < '0' || n1 > 'f' || (n0 > '9' && n0 < 'A') || (n1 > '9' && n1 < 'A'))
                                {
                                    setError("invalid hex digit");
                                    return -1; /*  invalid hex digit */
                                }
                                if(n0 > 'F')
                                {
                                    n0 -= 0x20;
                                }
                                if(n1 > 'F')
                                {
                                    n1 -= 0x20;
                                }
                                if(n0 >= 'A')
                                {
                                    n0 -= 'A' - 10;
                                }
                                if(n1 >= 'A')
                                {
                                    n1 -= 'A' - 10;
                                }
                                n0 -= '0';
                                n1 -= '0';
                                escc = (n1 << 4) | n0;
                                i += 2;
                            }
                            else if(c == '{' || c == '}' || c == '[' || c == ']' || c == '-' || c == '(' || c == ')' || c == '|' || c == '^' || c == '$' || c == '*' || c == '+' || c == '?' || c == ':' || c == '.' || c == '/' || c == '\\')
                            {
                                escc = c;
                            }
                            else if(c == 'd' || c == 's' || c == 'w' || c == 'D' || c == 'S' || c == 'W')
                            {
                                if(state == RXSTATE_CCRANGE)
                                {
                                    setError("tried to use a shorthand as part of a range");
                                    return -1; /*  range shorthands can't be part of a range */
                                }
                                isupper = c <= 'Z';
                                memset(m, 0, sizeof(m));
                                if(isupper)
                                {
                                    c += 0x20;
                                }
                                if(c == 'd' || c == 'w')
                                {
                                    m[3] |= 0x03FF; /*  0~7 */
                                }
                                if(c == 's')
                                {
                                    m[0] |= 0x3E00; /*  \t-\r (includes \n, \v, and \f in the middle. 5 enabled bits.) */
                                    m[2] |= 1; /*  ' ' */
                                }
                                if(c == 'w')
                                {
                                    m[4] |= 0xFFFE; /*  A-O */
                                    m[5] |= 0x87FF; /*  P-Z_ */
                                    m[6] |= 0xFFFE; /*  a-o */
                                    m[7] |= 0x07FF; /*  p-z */
                                }
                                for(mi = 0; mi < 16; mi++)
                                {
                                    token.mask[mi] |= isupper ? ~m[mi] : m[mi];
                                }
                                charclassmem = -1; /*  range shorthands can't be part of a range */
                                continue;
                            }
                            else
                            {
                                printf("unknown/unsupported escape sequence in character class (\\%c)\n", c);
                                return -1; /*  unknown/unsupported escape sequence */
                            }
                        }
                        if(state == RXSTATE_CCINIT)
                        {
                            charclassmem = c;
                            setMask(&token, c);
                            state = RXSTATE_CCNORMAL;
                        }
                        else if(state == RXSTATE_CCNORMAL)
                        {
                            if(c == ']' && escc == 0)
                            {
                                charclassmem = -1;
                                state = RXSTATE_QUANT;
                                continue;
                            }
                            else if(c == '-' && escc == 0 && charclassmem >= 0)
                            {
                                state = RXSTATE_CCRANGE;
                                continue;
                            }
                            else
                            {
                                charclassmem = c;
                                setMask(&token, c);
                                state = RXSTATE_CCNORMAL;
                            }
                        }
                        else if(state == RXSTATE_CCRANGE)
                        {
                            if(c == ']' && escc == 0)
                            {
                                charclassmem = -1;
                                setMask(&token, '-');
                                state = RXSTATE_QUANT;
                                continue;
                            }
                            else
                            {
                                if(charclassmem == -1)
                                {
                                    setError("character class range is broken");
                                    return -1; /*  probably tried to use a character class shorthand as part of a range */
                                }
                                if((uint8_t)c < charclassmem)
                                {
                                    setError("character class range is misordered");
                                    return -1; /*  range is in wrong order */
                                }
                                /* printf("enabling char class from %d to %d...\n", charclassmem, c); */
                                for(clsi = c; clsi > charclassmem; clsi--)
                                {
                                    setMask(&token, clsi);
                                }
                                state = RXSTATE_CCNORMAL;
                                charclassmem = -1;
                            }
                        }
                    }
                    else
                    {
                        assert(0);
                    }
                }
                if(parencount > 0)
                {
                    setError("(parencount > 0)");
                    return -1; /*  unbalanced parens */
                }
                if(escstate != 0)
                {
                    setError("(escstate != 0)");
                    return -1; /*  open escape sequence */
                }
                if(state >= RXSTATE_CCINIT)
                {
                    setError("(state >= RXSTATE_CCINIT)");
                    return -1; /*  open character class */
                }
                if(!pushToken(token, tokenslen, &k, &macn))
                {
                    return -2;
                }
                /*  add invisible non-capturing group specifier */
                token.kind = RXTOKTYP_CLOSE;
                token.count_lo = 1;
                token.count_hi = 2;
                if(!pushToken(token, tokenslen, &k, &macn))
                {
                    return -2;
                }
                /*  add end token (tells matcher that it's done) */
                token.kind = RXTOKTYP_END;
                if(!pushToken(token, tokenslen, &k, &macn))
                {
                    return -2;
                }
                m_tokens[0].pair_offset = k - 2;
                m_tokens[k - 2].pair_offset = -(k - 2);
                m_tokencount = k;
                /*  copy quantifiers from )s to (s (so (s know whether they're optional) */
                /*  also take the opportunity to smuggle "quantified group index" into the mask field forthe ) */
                n = 0;
                for(k2 = 0; k2 < k; k2++)
                {
                    if(m_tokens[k2].kind == RXTOKTYP_CLOSE)
                    {
                        m_tokens[k2].mask[0] = n++;
                        k3 = k2 + m_tokens[k2].pair_offset;
                        m_tokens[k3].count_lo = m_tokens[k2].count_lo;
                        m_tokens[k3].count_hi = m_tokens[k2].count_hi;
                        m_tokens[k3].mask[0] = n++;
                        m_tokens[k3].mode = m_tokens[k2].mode;
                        /* if(n > 65535) */
                        if(n > 1024)
                        {
                            return -1; /*  too many quantified groups */
                        }
                    }
                    else if(m_tokens[k2].kind == RXTOKTYP_OR || m_tokens[k2].kind == RXTOKTYP_OPEN || m_tokens[k2].kind == RXTOKTYP_NCOPEN)
                    {
                        /*  find next | or ) and how far away it is. store in token */
                        balance = 0;
                        found = -1;
                        for(l = k2 + 1; l < tokenslen; l++)
                        {
                            if(m_tokens[l].kind == RXTOKTYP_OR && balance == 0)
                            {
                                found = l;
                                break;
                            }
                            else if(m_tokens[l].kind == RXTOKTYP_CLOSE)
                            {
                                if(balance == 0)
                                {
                                    found = l;
                                    break;
                                }
                                else
                                {
                                    balance -= 1;
                                }
                            }
                            else if(m_tokens[l].kind == RXTOKTYP_NCOPEN || m_tokens[l].kind == RXTOKTYP_OPEN)
                            {
                                balance += 1;
                            }
                        }
                        if(found == -1)
                        {
                            setError("unbalanced parens...");
                            return -1; /*  unbalanced parens */
                        }
                        diff = found - k2;
                        if(diff > 32767)
                        {
                            setError("too long...");
                            return -1; /*  too long */
                        }
                        if(m_tokens[k2].kind == RXTOKTYP_OR)
                        {
                            m_tokens[k2].pair_offset = diff;
                        }
                        else
                        {
                            m_tokens[k2].mask[15] = diff;
                        }
                    }
                }
                return 0;
            }

            /*  Returns match length iftext starts with a regex match.
             * Returns -1 ifthe text doesn't start with a regex match.
             * Returns -2 ifthe matcher ran out of memory or the regex is too complex.
             * Returns -3 ifthe regex is somehow invalid.
             * The first capslots capture positions and spans (lengths) will be written to cappos and capspan. If zero, will not be written to.
             * SAFETY: The text variable must be null-terminated, and starti must be the index of a character within the string or its null terminator.
             * SAFETY: Tokens array must be terminated by a RXTOKTYP_END token (done by default by parse()).
             * SAFETY: Partial capture data may be written even ifthe match fails.
             */

            NEON_INLINE int64_t match(const char* text, size_t starti, uint16_t capslots, int64_t* cappos, int64_t* capspan)
            {
                int kind;
                size_t n;
                uint64_t tokenslen;
                uint32_t k;
                uint16_t caps;
                uint16_t stackn;
                uint64_t i;
                uint64_t range_min;
                uint64_t range_max;
                uint8_t justrewinded;
                size_t iterlimit;
                uint64_t origk;
                ptrdiff_t kdiff;
                uint32_t prev;
                uint8_t forcezero;
                uint32_t k2;
                uint64_t ntcnt;
                uint64_t oldi;
                uint64_t hiclimit;
                uint64_t rangelimit;
                uint16_t capindex;
                uint64_t wmask[16];
                /* quantified group state */
                uint8_t qgroupacceptszero[auxstatssize];
                /* number of repetitions */
                uint32_t qgroupstate[auxstatssize];
                /* location of most recent corresponding ) on stack. 0 means nowhere */
                uint32_t qgroupstack[auxstatssize];
                uint16_t qgroupcapindex[auxstatssize];
                MState rewindstack[stacksizemax];

                if(capslots > auxstatssize)
                {
                    capslots = auxstatssize;
                }
                memset(qgroupcapindex, 0xFF, sizeof(qgroupcapindex));
                tokenslen = 0;
                k = 0;
                caps = 0;
                while(m_tokens[k].kind != RXTOKTYP_END)
                {
                    if(m_tokens[k].kind == RXTOKTYP_OPEN && caps < capslots)
                    {
                        qgroupcapindex[m_tokens[k].mask[0]] = caps;
                        qgroupcapindex[m_tokens[k + m_tokens[k].pair_offset].mask[0]] = caps;
                        cappos[caps] = -1;
                        capspan[caps] = -1;
                        caps += 1;
                    }
                    k += 1;
                    if(m_tokens[k].kind == RXTOKTYP_CLOSE || m_tokens[k].kind == RXTOKTYP_OPEN || m_tokens[k].kind == RXTOKTYP_NCOPEN)
                    {
                        if(m_tokens[k].mask[0] >= auxstatssize)
                        {
                            setError("too many qualified groups. returning");
                            /* OOM: too many quantified groups */
                            return -2;
                        }
                        qgroupstate[m_tokens[k].mask[0]] = 0;
                        qgroupstack[m_tokens[k].mask[0]] = 0;
                        qgroupacceptszero[m_tokens[k].mask[0]] = 0;
                    }
                }
                tokenslen = k;
                stackn = 0;
                i = starti;
                range_min = 0;
                range_max = 0;
                justrewinded = 0;
                /* used in boundary anchor checker */
                memset(wmask, 0, sizeof(wmask));
                wmask[3] = 0x03FF;
                wmask[4] = 0xFFFE;
                wmask[5] = 0x87FF;
                wmask[6] = 0xFFFE;
                wmask[7] = 0x07FF;
                iterlimit = 10000;
                for(k = 0; k < tokenslen; k++)
                {
                    /* iterlimit--; */
                    if(iterlimit == 0)
                    {
                        setError("iteration limit exceeded. returning");
                        return -2;
                    }
                    if(m_tokens[k].kind == RXTOKTYP_CARET)
                    {
                        if(i != 0)
                        {
                            if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                            {
                                return -1;
                            }
                        }
                        continue;
                    }
                    else if(m_tokens[k].kind == RXTOKTYP_DOLLAR)
                    {
                        if(text[i] != 0)
                        {
                            if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                            {
                                return -1;
                            }
                        }
                        continue;
                    }
                    else if(m_tokens[k].kind == RXTOKTYP_BOUND)
                    {
                        if(i == 0 && !checkIsW(wmask, text[i]))
                        {
                            if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                            {
                                return -1;
                            }
                        }
                        else if(i != 0 && text[i] == 0 && !checkIsW(wmask, text[i - 1]))
                        {
                            if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                            {
                                return -1;
                            }
                        }
                        else if(i != 0 && text[i] != 0 && checkIsW(wmask, text[i - 1]) == checkIsW(wmask, text[i]))
                        {
                            if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                            {
                                return -1;
                            }
                        }
                    }
                    else if(m_tokens[k].kind == RXTOKTYP_NBOUND)
                    {
                        if(i == 0 && checkIsW(wmask, text[i]))
                        {
                            if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                            {
                                return -1;
                            }
                        }
                        else if(i != 0 && text[i] == 0 && checkIsW(wmask, text[i - 1]))
                        {
                            if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                            {
                                return -1;
                            }
                        }
                        else if(i != 0 && text[i] != 0 && checkIsW(wmask, text[i - 1]) != checkIsW(wmask, text[i]))
                        {
                            if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                            {
                                return -1;
                            }
                        }
                    }
                    else
                    {
                        /* deliberately unmatchable token (e.g. a{0}, a{0,0}) */
                        if(m_tokens[k].count_hi == 1)
                        {
                            if(m_tokens[k].kind == RXTOKTYP_OPEN || m_tokens[k].kind == RXTOKTYP_NCOPEN)
                            {
                                k += m_tokens[k].pair_offset;
                            }
                            else
                            {
                                k += 1;
                            }
                            continue;
                        }
                        if(m_tokens[k].kind == RXTOKTYP_OPEN || m_tokens[k].kind == RXTOKTYP_NCOPEN)
                        {
                            if(!justrewinded)
                            {
                                /*  need this to be able to detect and reject zero-size matches */
                                /* qgroupstate[m_tokens[k].mask[0]] = i; */

                                /*  ifwe're lazy and the min length is 0, we need to try the non-group case first */
                                if((m_tokens[k].mode & RXTOKMODE_LAZY) && (m_tokens[k].count_lo == 0 || qgroupacceptszero[m_tokens[k + m_tokens[k].pair_offset].mask[0]]))
                                {
                                    range_min = 0;
                                    range_max = 0;
                                    if(!rewindDoSaveRaw(k, 0, i, stackn, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                    {
                                        return -2;
                                    };
                                    k += m_tokens[k].pair_offset; /*  automatic += 1 will put us past the matching ) */
                                }
                                else
                                {
                                    range_min = 1;
                                    range_max = 0;
                                    if(!rewindDoSaveRaw(k, 0, i, stackn, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                    {
                                        return -2;
                                    };
                                }
                            }
                            else
                            {
                                justrewinded = 0;
                                origk = k;
                                if(range_min != 0)
                                {
                                    k += range_min;
                                    if(m_tokens[k - 1].kind == RXTOKTYP_OR)
                                    {
                                        k += m_tokens[k - 1].pair_offset - 1;
                                    }
                                    else if(m_tokens[k - 1].kind == RXTOKTYP_OPEN || m_tokens[k - 1].kind == RXTOKTYP_NCOPEN)
                                    {
                                        k += m_tokens[k - 1].mask[15] - 1;
                                    }
                                    if(m_tokens[k].kind == RXTOKTYP_END) /*  unbalanced parens */
                                    {
                                        return -3;
                                    }
                                    if(m_tokens[k].kind == RXTOKTYP_CLOSE)
                                    {
                                        /*  do nothing and continue on ifwe don't need this group */
                                        if(m_tokens[k].count_lo == 0 || qgroupacceptszero[m_tokens[k].mask[0]])
                                        {
                                            qgroupstate[m_tokens[k].mask[0]] = 0;
                                            if(!(m_tokens[k].mode & RXTOKMODE_LAZY))
                                            {
                                                qgroupstack[m_tokens[k].mask[0]] = 0;
                                            }
                                            continue;
                                        }
                                        /*  otherwise go to the last point before the group */
                                        else
                                        {
                                            if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                            {
                                                return -1;
                                            }
                                            continue;
                                        }
                                    }

                                    assert(m_tokens[k].kind == RXTOKTYP_OR);
                                }
                                kdiff = k - origk;
                                range_min = kdiff + 1;
                                if(!rewindDoSaveRaw(k - kdiff, 0, i, stackn, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                {
                                    return -2;
                                };
                            }
                        }
                        else if(m_tokens[k].kind == RXTOKTYP_CLOSE)
                        {
                            /*  unquantified */
                            if(m_tokens[k].count_lo == 1 && m_tokens[k].count_hi == 2)
                            {
                                /*  forcaptures */
                                capindex = qgroupcapindex[m_tokens[k].mask[0]];
                                if(capindex != 0xFFFF)
                                {
                                    if(!rewindDoSaveRaw(k, 1, i, stackn, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                    {
                                        return -2;
                                    };
                                }
                            }
                            /*  quantified */
                            else
                            {
                                if(!justrewinded)
                                {
                                    prev = qgroupstack[m_tokens[k].mask[0]];

                                    range_max = m_tokens[k].count_hi;
                                    range_max -= 1;
                                    range_min = qgroupacceptszero[m_tokens[k].mask[0]] ? 0 : m_tokens[k].count_lo;
                                    /* assert(qgroupstate[m_tokens[k + m_tokens[k].pair_offset].mask[0]] <= i); */
                                    /* if(prev) assert(rewindstack[prev].i <= i); */
                                    /*  minimum requirement not yet met */
                                    if(qgroupstate[m_tokens[k].mask[0]] + 1 < range_min)
                                    {
                                        qgroupstate[m_tokens[k].mask[0]] += 1;
                                        if(!rewindDoSaveRaw(k, 0, i, stackn, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                        {
                                            return -2;
                                        };

                                        k += m_tokens[k].pair_offset; /*  back to start of group */
                                        k -= 1; /*  ensure we actually hit the group node next and not the node after it */
                                        continue;
                                    }
                                    /*  maximum allowance exceeded */
                                    else if(m_tokens[k].count_hi != 0 && qgroupstate[m_tokens[k].mask[0]] + 1 > range_max)
                                    {
                                        range_max -= 1;
                                        if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                        {
                                            return -1;
                                        }
                                        continue;
                                    }

                                    /*  fallback case to detect zero-length matches when we backtracked into the inside of this group */
                                    /*  after an attempted parse of a second copy of itself */
                                    forcezero = 0;
                                    if(prev != 0 && (uint32_t)rewindstack[prev].i > (uint32_t)i)
                                    {
                                        /*  find matching open paren */
                                        n = stackn - 1;
                                        while(n > 0 && rewindstack[n].k != k + m_tokens[k].pair_offset)
                                        {
                                            n -= 1;
                                        }
                                        assert(n > 0);
                                        if(rewindstack[n].i == i)
                                        {
                                            forcezero = 1;
                                        }
                                    }

                                    /*  reject zero-length matches */
                                    if((forcezero || (prev != 0 && (uint32_t)rewindstack[prev].i == (uint32_t)i))) /*   && qgroupstate[m_tokens[k].mask[0]] > 0 */
                                    {
                                        qgroupacceptszero[m_tokens[k].mask[0]] = 1;
                                        if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                        {
                                            return -1;
                                        }
                                        /* range_max = qgroupstate[m_tokens[k].mask[0]]; */
                                        /* range_min = 0; */
                                    }
                                    else if(m_tokens[k].mode & RXTOKMODE_LAZY) /*  lazy */
                                    {
                                        if(prev)
                                        {
                                        }
                                        /*  continue on to past the group; group retry is in rewind state */
                                        qgroupstate[m_tokens[k].mask[0]] += 1;
                                        if(!rewindDoSaveRaw(k, 0, i, stackn, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                        {
                                            return -2;
                                        };

                                        qgroupstate[m_tokens[k].mask[0]] = 0;
                                    }
                                    else /*  greedy */
                                    {
                                        /*  clear unwanted memory ifpossessive */
                                        if((m_tokens[k].mode & RXTOKMODE_POSSESSIVE))
                                        {
                                            k2 = k;
                                            /*  special case forfirst, only rewind to (, not to ) */
                                            if(qgroupstate[m_tokens[k].mask[0]] == 0)
                                            {
                                                k2 = k + m_tokens[k].pair_offset;
                                            }
                                            if(stackn == 0)
                                            {
                                                return -1;
                                            }
                                            stackn -= 1;
                                            while(stackn > 0 && rewindstack[stackn].k != k2)
                                            {
                                                stackn -= 1;
                                            }
                                            if(stackn == 0)
                                            {
                                                return -1;
                                            }
                                        }
                                        /*  continue to next match ifsane */
                                        if((uint32_t)qgroupstate[m_tokens[k + m_tokens[k].pair_offset].mask[0]] < (uint32_t)i)
                                        {
                                            qgroupstate[m_tokens[k].mask[0]] += 1;
                                            if(!rewindDoSaveRaw(k, 0, i, stackn, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                            {
                                                return -2;
                                            };

                                            k += m_tokens[k].pair_offset; /*  back to start of group */
                                            k -= 1; /*  ensure we actually hit the group node next and not the node after it */
                                        }
                                        else
                                        {
                                        }
                                    }
                                }
                                else
                                {
                                    justrewinded = 0;

                                    if(m_tokens[k].mode & RXTOKMODE_LAZY)
                                    {
                                        /*  lazy rewind: need to try matching the group again */
                                        if(!rewindDoSaveRaw(k, 1, i, stackn, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                        {
                                            return -2;
                                        };

                                        qgroupstack[m_tokens[k].mask[0]] = stackn;
                                        k += m_tokens[k].pair_offset; /*  back to start of group */
                                        k -= 1; /*  ensure we actually hit the group node next and not the node after it */
                                    }
                                    else
                                    {
                                        /*  greedy. ifwe're going to go outside the acceptable range, rewind */
                                        /* uint64_t oldi = i; */
                                        if(qgroupstate[m_tokens[k].mask[0]] < range_min && !qgroupacceptszero[m_tokens[k].mask[0]])
                                        {
                                            /* i = oldi; */
                                            if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                            {
                                                return -1;
                                            }
                                        }
                                        /*  otherwise continue on to past the group */
                                        else
                                        {
                                            qgroupstate[m_tokens[k].mask[0]] = 0;
                                            /*  forcaptures */
                                            capindex = qgroupcapindex[m_tokens[k].mask[0]];
                                            if(capindex != 0xFFFF)
                                            {
                                                if(!rewindDoSaveRaw(k, 1, i, stackn, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                                {
                                                    return -2;
                                                };
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        else if(m_tokens[k].kind == RXTOKTYP_OR)
                        {
                            k += m_tokens[k].pair_offset;
                            k -= 1;
                        }
                        else if(m_tokens[k].kind == RXTOKTYP_NORMAL)
                        {
                            if(!justrewinded)
                            {
                                ntcnt = 0;
                                /*  do whatever the obligatory minimum amount of matching is */
                                oldi = i;
                                while(ntcnt < m_tokens[k].count_lo && text[i] != 0 && checkMask(m_tokens, k, text[i]))
                                {
                                    i += 1;
                                    ntcnt += 1;
                                }
                                if(ntcnt < m_tokens[k].count_lo)
                                {
                                    i = oldi;
                                    if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                    {
                                        return -1;
                                    }
                                    continue;
                                }
                                if(m_tokens[k].mode & RXTOKMODE_LAZY)
                                {
                                    range_min = ntcnt;
                                    range_max = m_tokens[k].count_hi - 1;
                                    if(!rewindDoSaveRaw(k, 0, i, stackn, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                    {
                                        return -2;
                                    };
                                }
                                else
                                {
                                    hiclimit = m_tokens[k].count_hi;
                                    if(hiclimit == 0)
                                    {
                                        hiclimit = ~hiclimit;
                                    }
                                    range_min = ntcnt;
                                    while(text[i] != 0 && checkMask(m_tokens, k, text[i]) && ntcnt + 1 < hiclimit)
                                    {
                                        i += 1;
                                        ntcnt += 1;
                                    }
                                    range_max = ntcnt;
                                    if(!(m_tokens[k].mode & RXTOKMODE_POSSESSIVE))
                                    {
                                        if(!rewindDoSaveRaw(k, 0, i, stackn, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                        {
                                            return -2;
                                        };
                                    }
                                }
                            }
                            else
                            {
                                justrewinded = 0;
                                if(m_tokens[k].mode & RXTOKMODE_LAZY)
                                {
                                    rangelimit = range_max;
                                    if(rangelimit == 0)
                                    {
                                        rangelimit = ~rangelimit;
                                    }
                                    if(checkMask(m_tokens, k, text[i]) && text[i] != 0 && range_min < rangelimit)
                                    {
                                        i += 1;
                                        range_min += 1;
                                        if(!rewindDoSaveRaw(k, 0, i, stackn, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                        {
                                            return -2;
                                        };
                                    }
                                    else
                                    {
                                        if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                        {
                                            return -1;
                                        }
                                    }
                                }
                                else
                                {
                                    if(range_max > range_min)
                                    {
                                        i -= 1;
                                        range_max -= 1;
                                        if(!rewindDoSaveRaw(k, 0, i, stackn, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                        {
                                            return -2;
                                        };
                                    }
                                    else
                                    {
                                        if(!rewindOrFail(i, k, stackn, justrewinded, range_min, range_max, qgroupstack, qgroupstate, rewindstack))
                                        {
                                            return -1;
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            fprintf(stderr, "unimplemented token kind %d\n", m_tokens[k].kind);
                            assert(0);
                        }
                    }
                    /*
                    printf("k... %d\n", k);
                    */
                }
                if(caps != 0)
                {
                    /*
                    printf("stackn: %d\n", stackn);
                    */
                    fflush(stdout);
                    for(n = 0; n < stackn; n++)
                    {
                        MState s = rewindstack[n];
                        kind = m_tokens[s.k].kind;
                        if(kind == RXTOKTYP_OPEN || kind == RXTOKTYP_CLOSE)
                        {
                            capindex = qgroupcapindex[m_tokens[s.k].mask[0]];
                            if(capindex == 0xFFFF)
                            {
                                continue;
                            }
                            if(m_tokens[s.k].kind == RXTOKTYP_OPEN)
                            {
                                cappos[capindex] = s.i;
                            }
                            else if(cappos[capindex] >= 0)
                            {
                                capspan[capindex] = s.i - cappos[capindex];
                            }
                        }
                    }
                    /*  re-deinitialize capture positions that have no associated capture span */
                    for(n = 0; n < caps; n++)
                    {
                        if(capspan[n] == -1)
                        {
                            cappos[n] = -1;
                        }
                    }
                }
                return i;
            }

            NEON_INLINE int doInvert(Token* token, int macn)
            {
                for(macn = 0; macn < 16; macn++)
                {
                    token->mask[macn] = ~token->mask[macn];
                }
                token->mode &= ~RXTOKMODE_INVERTED;
                return macn;
            }

            NEON_INLINE void clearToken(Token* token)
            {
                memset(token, 0, sizeof(Token));
                token->count_lo = 1;
                token->count_hi = 2;
            }

            NEON_INLINE bool pushToken(Token token, int64_t tokenslen, int16_t* k, int* macn)
            {
                if(((*k) == 0) || m_tokens[(*k) - 1].kind != token.kind || (token.kind != RXTOKTYP_BOUND && token.kind != RXTOKTYP_NBOUND))
                {
                    if(token.mode & RXTOKMODE_INVERTED)
                    {
                        (*macn) = doInvert(&token, *macn);
                    }
                    if((*k) >= tokenslen)
                    {
                        puts("buffer overflow");
                        return false;
                    }
                    m_tokens[(*k)++] = token;
                    clearToken(&token);
                }
                return true;
            }

            NEON_INLINE void setMask(Token* token, int byte)
            {
                token->mask[((uint8_t)(byte)) >> 4] |= 1 << ((uint8_t)(byte) & 0xF);
            }

            NEON_INLINE int setMaskAll(Token* token, int macn)
            {
                for(macn = 0; macn < 16; macn++)
                {
                    token->mask[macn] = 0xFFFF;
                }
                return macn;
            }

            NEON_INLINE int isQuantChar(int c)
            {
                return ((c == '{') || (c == '}') || (c == '[') || (c == ']') || (c == '-') || (c == '(') || (c == ')') || (c == '|') || (c == '^') || (c == '$') || (c == '*') || (c == '+') || (c == '?') || (c == ':') || (c == '.') || (c == '/') || (c == '\\'));
            }
    };

    class IOResult
    {
        public:
            uint8_t success;
            char* data;
            size_t length;
    };

    class IOStream
    {
        public:
            enum Mode
            {
                PRMODE_UNDEFINED,
                PRMODE_STRING,
                PRMODE_FILE
            };

        public:
            static bool makeStackIO(IOStream* pr, FILE* fh, bool shouldclose)
            {
                initStreamVars(pr, PRMODE_FILE);
                pr->m_fromstack = true;
                pr->m_handle = fh;
                pr->m_shouldclose = shouldclose;
                return true;
            }

            static bool makeStackString(IOStream* pr)
            {
                initStreamVars(pr, PRMODE_STRING);
                pr->m_fromstack = true;
                pr->m_wrmode = PRMODE_STRING;
                StrBuffer::fromPtr(&pr->m_strbuf, 0);
                return true;
            }

            static IOStream* makeUndefStream(Mode mode)
            {
                IOStream* pr;
                pr = Memory::make<IOStream>();
                if(!pr)
                {
                    fprintf(stderr, "cannot allocate IOStream\n");
                    return nullptr;
                }
                initStreamVars(pr, mode);
                return pr;
            }

            static IOStream* makeIO(FILE* fh, bool shouldclose)
            {
                IOStream* pr;
                pr = makeUndefStream(PRMODE_FILE);
                pr->m_handle = fh;
                pr->m_shouldclose = shouldclose;
                return pr;
            }

            static void destroy(IOStream* pr)
            {
                if(pr == nullptr)
                {
                    return;
                }
                if(pr->m_wrmode == PRMODE_UNDEFINED)
                {
                    return;
                }
                /*fprintf(stderr, "IOStream::destroy: pr->m_wrmode=%d\n", pr->m_wrmode);*/
                if(pr->m_wrmode == PRMODE_STRING)
                {
                    if(!pr->m_stringtaken)
                    {
                        StrBuffer::destroyFromPtr(&pr->m_strbuf);
                    }
                }
                else if(pr->m_wrmode == PRMODE_FILE)
                {
                    if(pr->m_shouldclose)
                    {
            #if 0
                        fclose(pr->m_handle);
            #endif
                    }
                }
                if(!pr->m_fromstack)
                {
                    Memory::sysFree(pr);
                    pr = nullptr;
                }
            }

            static void initStreamVars(IOStream* pr, Mode mode)
            {
                pr->m_fromstack = false;
                pr->m_wrmode = PRMODE_UNDEFINED;
                pr->m_shouldclose = false;
                pr->m_shouldflush = false;
                pr->m_stringtaken = false;
                pr->m_shortenvalues = false;
                pr->m_jsonmode = false;
                pr->m_maxvallength = 15;
                pr->m_handle = nullptr;
                pr->m_wrmode = mode;
            }

        public:
            /* if file: should be closed when writer is destroyed? */
            uint8_t m_shouldclose;
            /* if file: should write operations be flushed via fflush()? */
            uint8_t m_shouldflush;
            /* if string: true if $m_strbuf was taken via take() */
            uint8_t m_stringtaken;
            /* was this writer instance created on stack? */
            uint8_t m_fromstack;
            uint8_t m_shortenvalues;
            uint8_t m_jsonmode;
            size_t m_maxvallength;
            /* the mode that determines what writer actually does */
            Mode m_wrmode;
            StrBuffer m_strbuf;
            FILE* m_handle;

        public:

            String* takeString()
            {
                size_t xlen;
                String* os;
                xlen = m_strbuf.length();
                os = Wrappers::wrapMakeFromStrBuf(m_strbuf, Util::hashString(m_strbuf.data(), xlen), xlen);
                m_stringtaken = true;
                return os;
            }

            String* copyString()
            {
                String* os;
                os = Wrappers::wrapStringCopy(m_strbuf.data(), m_strbuf.length());
                return os;
            }

            void flush()
            {
                // if(m_shouldflush)
                {
                    fflush(m_handle);
                }
            }

            bool writeString(const char* estr, size_t elen)
            {
                // fprintf(stderr, "writestringl: (%d) <<<%.*s>>>\n", elen, elen, estr);
                size_t chlen;
                chlen = sizeof(char);
                if(elen > 0)
                {
                    if(m_wrmode == PRMODE_FILE)
                    {
                        fwrite(estr, chlen, elen, m_handle);
                        flush();
                    }
                    else if(m_wrmode == PRMODE_STRING)
                    {
                        m_strbuf.append(estr, elen);
                    }
                    else
                    {
                        return false;
                    }
                }
                return true;
            }

            bool writeString(const char* estr)
            {
                return writeString(estr, strlen(estr));
            }

            bool writeChar(int b)
            {
                char ch;
                if(m_wrmode == PRMODE_STRING)
                {
                    ch = b;
                    writeString(&ch, 1);
                }
                else if(m_wrmode == PRMODE_FILE)
                {
                    fputc(b, m_handle);
                    flush();
                }
                return true;
            }

            bool writeEscapedChar(int ch)
            {
                switch(ch)
                {
                    case '\'':
                    {
                        writeString("\\\'");
                    }
                    break;
                    case '\"':
                    {
                        writeString("\\\"");
                    }
                    break;
                    case '\\':
                    {
                        writeString("\\\\");
                    }
                    break;
                    case '\b':
                    {
                        writeString("\\b");
                    }
                    break;
                    case '\f':
                    {
                        writeString("\\f");
                    }
                    break;
                    case '\n':
                    {
                        writeString("\\n");
                    }
                    break;
                    case '\r':
                    {
                        writeString("\\r");
                    }
                    break;
                    case '\t':
                    {
                        writeString("\\t");
                    }
                    break;
                    case 0:
                    {
                        writeString("\\0");
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

            bool writeQuotedString(const char* str, size_t len, bool withquot)
            {
                int bch;
                size_t i;
                bch = 0;
                if(withquot)
                {
                    writeChar('"');
                }
                for(i = 0; i < len; i++)
                {
                    bch = str[i];
                    if((bch < 32) || (bch > 127) || (bch == '\"') || (bch == '\\'))
                    {
                        writeEscapedChar(bch);
                    }
                    else
                    {
                        writeChar(bch);
                    }
                }
                if(withquot)
                {
                    writeChar('"');
                }
                return true;
            }

            template<typename... ArgsT>
            bool format(const char* fmt, ArgsT&&... args)
            {
                constexpr static auto tmpfprintf = fprintf;
                if(m_wrmode == PRMODE_STRING)
                {
                    m_strbuf.appendFormat(fmt, args...);
                }
                else if(m_wrmode == PRMODE_FILE)
                {
                    tmpfprintf(m_handle, fmt, args...);
                    flush();
                }
                return true;
            }
    };

    class Object
    {    
        public:
            enum Type
            {
                OTYP_INVALID,
                /* containers */
                OTYP_STRING,
                OTYP_RANGE,
                OTYP_ARRAY,
                OTYP_DICT,
                OTYP_FILE,

                /* base object types */
                OTYP_UPVALUE,
                OTYP_FUNCBOUND,
                OTYP_FUNCCLOSURE,
                OTYP_FUNCSCRIPT,
                OTYP_INSTANCE,
                OTYP_FUNCNATIVE,
                OTYP_CLASS,

                /* non-user objects */
                OTYP_MODULE,
                OTYP_SWITCH,
                /* object type that can hold any C pointer */
                OTYP_USERDATA
            };

        public:
            static void markObject(Object* object);

            static void blackenObject(Object* object);

            static void destroyObject(Object* object);

        public:
            Type m_objtype = OTYP_INVALID;
            bool m_objmark = false;
            /*
            // when an object is marked as stale, it means that the
            // GC will never collect this object. This can be useful
            // for library/package objects that want to reuse native
            // objects in their types/pointers. The GC cannot reach
            // them yet, so it's best for them to be kept stale.
            */
            bool m_objstale = false;
            Object* m_objnext = nullptr;

        public:
            Object()
            {
            }
    };

    class Value
    {
        public:
            enum Type
            {
                VT_NULL,
                VT_BOOL,
                VT_NUMBER,
                VT_OBJ,
            };

        public:

            static uint32_t hashObject(Object* object)
            {
                switch(object->m_objtype)
                {
                    case Object::OTYP_CLASS:
                        {
                            auto cl = (Class*)object;
                            /* Classes just use their name. */
                            return Wrappers::wrapStrGetHash(Wrappers::wrapClassGetName(cl));
                        }
                        break;
                    case Object::OTYP_FUNCSCRIPT:
                    {
                        /*
                        // Allow bare (non-closure) functions so that we can use a map to find
                        // existing constants in a function's constant table. This is only used
                        // internally. Since user code never sees a non-closure function, they
                        // cannot use them as map keys.
                        */
                        uint32_t tmpa;
                        uint32_t tmpb;
                        uint32_t tmpres;
                        uint32_t tmpptr;
                        uint32_t arity;
                        uint32_t blobcnt;
                        Function* fn;
                        fn = (Function*)object;
                        tmpptr = (uint32_t)(uintptr_t)fn;
                        arity = Wrappers::wrapGetArityOfFuncScript(fn);
                        blobcnt = Wrappers::wrapGetBlobcountOfFuncScript(fn);
                        tmpa = Util::hashDouble(arity);
                        tmpb = Util::hashDouble(blobcnt);
                        tmpres = tmpa ^ tmpb;
                        tmpres = tmpres ^ tmpptr;
                        return tmpres;
                    }
                    break;
                    case Object::OTYP_STRING:
                    {
                        auto os = (String*)object;
                        return Wrappers::wrapStrGetHash(os);
                    }
                    break;
                    default:
                        break;
                }
                return 0;
            }

            static uint32_t hashValue(Value value)
            {
                if(value.isBool())
                {
                    return value.asBool() ? 3 : 5;
                }
                else if(value.isNull())
                {
                    return 7;
                }
                else if(value.isNumber())
                {
                    return Util::hashDouble(value.asNumber());
                }
                else if(value.isObject())
                {
                    return hashObject(value.asObject());
                }
                return 0;
            }

            static bool compareArrays(Array* oa, Array* ob);
            static bool compareStrings(Object* oa, Object* ob);
            static bool compareDicts(Object* oa, Object* ob);
            static bool compareObjects(Value a, Value b);
            static bool compareValActual(Value a, Value b);

            static bool compareValues(Value a, Value b);

            /**
             * returns the greater of the two values.
             * this function encapsulates the object hierarchy
             */
            static Value findGreater(Value a, Value b);

            /** sorts values in an array using the bubble-sort algorithm */
            static void sortValues(Value* values, int count);
            static Value copyValue(Value value);
            static void valtabRemoveWhites(HashTable<Value, Value>* table);
            static void markValArray(ValList<Value>* list);
            static void markValTable(HashTable<Value, Value>* table);
            static void markDict(Dict* dict);

            static NEON_INLINE double valToNumber(Value v)
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

            static NEON_INLINE uint32_t valToUint(Value v)
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

            static NEON_INLINE long valToInt(Value v)
            {
                return (long)valToNumber(v);
            }

            template<typename FNType>
            static NEON_INLINE const char* typeFromFunction(FNType func)
            {
                (void)func;
            #if 1
                if(func == &Value::isString)
                {
                    return "string";
                }
                else if(func == &Value::isNull)
                {
                    return "null";
                }        
                else if(func == &Value::isBool)
                {
                    return "bool";
                }        
                else if(func == &Value::isNumber)
                {
                    return "number";
                }
                else if(func == &Value::isString)
                {
                    return "string";
                }
                else if((func == &Value::isFuncnative) || (func == &Value::isFuncbound) || (func == &Value::isFuncscript) || (func == &Value::isFuncclosure) || (func == &Value::isCallable))
                {
                    return "function";
                }
                else if(func == &Value::isClass)
                {
                    return "class";
                }
                else if(func == &Value::isInstance)
                {
                    return "instance";
                }
                else if(func == &Value::isArray)
                {
                    return "array";
                }
                else if(func == &Value::isDict)
                {
                    return "dictionary";
                }
                else if(func == &Value::isFile)
                {
                    return "file";
                }
                else if(func == &Value::isRange)
                {
                    return "range";
                }
                else if(func == &Value::isModule)
                {
                    return "module";
                }
            #endif
                return "?unknown?";
            }

            static const char* objectTypename(Object* object, bool detailed)
            {
                static char buf[60];
                if(detailed)
                {
                    switch(object->m_objtype)
                    {
                        case Object::OTYP_FUNCSCRIPT:
                            return "funcscript";
                        case Object::OTYP_FUNCNATIVE:
                            return "funcnative";
                        case Object::OTYP_FUNCCLOSURE:
                            return "funcclosure";
                        case Object::OTYP_FUNCBOUND:
                            return "funcbound";
                        default:
                            break;
                    }
                }
                switch(object->m_objtype)
                {
                    case Object::OTYP_MODULE:
                        return "module";
                    case Object::OTYP_RANGE:
                        return "range";
                    case Object::OTYP_FILE:
                        return "file";
                    case Object::OTYP_DICT:
                        return "dictionary";
                    case Object::OTYP_ARRAY:
                        return "array";
                    case Object::OTYP_CLASS:
                        return "class";
                    case Object::OTYP_FUNCSCRIPT:
                    case Object::OTYP_FUNCNATIVE:
                    case Object::OTYP_FUNCCLOSURE:
                    case Object::OTYP_FUNCBOUND:
                        return "function";
                    case Object::OTYP_INSTANCE:
                    {
                        const char* klassname;
                        Instance* inst;
                        inst = ((Instance*)object);
                        klassname = Wrappers::wrapGetInstanceName(inst);
                        sprintf(buf, "instance@%s", klassname);
                        return buf;
                    }
                    break;
                    case Object::OTYP_STRING:
                        return "string";
                    case Object::OTYP_USERDATA:
                        return "userdata";
                    case Object::OTYP_SWITCH:
                        return "switch";
                    default:
                        break;
                }
                return "unknown";
            }

            static const char* typeName(Value value, bool detailed)
            {
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
                    return objectTypename(value.asObject(), detailed);
                }
                return "?unknown?";
            }

        public:
            Type m_valtype;
            union
            {
                uint8_t vbool;
                double vfltnum;
                Object* vobjpointer;
            } m_valunion;

        public:
            static String* toString(Value value);

            static NEON_INLINE Value makeTypedValue(Type type)
            {
                Value v;
                memset(&v, 0, sizeof(Value));
                v.m_valtype = type;
                return v;
            }

            template<typename InputT>
            static NEON_INLINE Value fromObject(InputT* obj)
            {
                Value v;
                v = makeTypedValue(VT_OBJ);
                v.m_valunion.vobjpointer = obj;
                return v;
            }

            static NEON_INLINE Value makeNull()
            {
                Value v;
                v = makeTypedValue(VT_NULL);
                return v;
            }

            static NEON_INLINE Value makeBool(bool b)
            {
                Value v;
                v = makeTypedValue(VT_BOOL);
                v.m_valunion.vbool = b;
                return v;
            }

            static NEON_INLINE Value makeNumber(double d)
            {
                Value v;
                v = makeTypedValue(VT_NUMBER);
                v.m_valunion.vfltnum = d;
                return v;
            }

        public:
            Value() = default;

            NEON_INLINE Object::Type objType() const
            {
                return asObject()->m_objtype;
            }

            NEON_INLINE Function* asFunction() const
            {
                return ((Function*)asObject());
            }

            NEON_INLINE Class* asClass() const
            {
                return ((Class*)asObject());
            }

            NEON_INLINE Instance* asInstance() const
            {
                return ((Instance*)asObject());
            }

            NEON_INLINE Switch* asSwitch() const
            {
                return ((Switch*)asObject());
            }

            NEON_INLINE Userdata* asUserdata() const
            {
                return ((Userdata*)asObject());
            }

            NEON_INLINE Module* asModule() const
            {
                return ((Module*)asObject());
            }

            NEON_INLINE File* asFile() const
            {
                return ((File*)asObject());
            }

            NEON_INLINE Range* asRange() const
            {
                return ((Range*)asObject());
            }

            NEON_INLINE bool isNull() const
            {
                return (m_valtype == VT_NULL);
            }

            NEON_INLINE bool isObject() const
            {
                return (m_valtype == VT_OBJ);
            }

            NEON_INLINE bool isObjtype(Object::Type t) const
            {
                return isObject() && (asObject()->m_objtype == t);
            }

            NEON_INLINE bool isBool() const
            {
                return (m_valtype == VT_BOOL);
            }

            NEON_INLINE bool isNumber() const
            {
                return (m_valtype == VT_NUMBER);
            }

            NEON_INLINE bool isString() const
            {
                return isObjtype(Object::OTYP_STRING);
            }

            NEON_INLINE bool isFuncnative() const
            {
                return isObjtype(Object::OTYP_FUNCNATIVE);
            }

            NEON_INLINE bool isFuncscript() const
            {
                return isObjtype(Object::OTYP_FUNCSCRIPT);
            }

            NEON_INLINE bool isFuncclosure() const
            {
                return isObjtype(Object::OTYP_FUNCCLOSURE);
            }

            NEON_INLINE bool isFuncbound() const
            {
                return isObjtype(Object::OTYP_FUNCBOUND);
            }

            NEON_INLINE bool isClass() const
            {
                return isObjtype(Object::OTYP_CLASS);
            }

            NEON_INLINE bool isInstance() const
            {
                return isObjtype(Object::OTYP_INSTANCE);
            }

            NEON_INLINE bool isArray() const
            {
                return isObjtype(Object::OTYP_ARRAY);
            }

            NEON_INLINE bool isDict() const
            {
                return isObjtype(Object::OTYP_DICT);
            }

            NEON_INLINE bool isFile() const
            {
                return isObjtype(Object::OTYP_FILE);
            }

            NEON_INLINE bool isRange() const
            {
                return isObjtype(Object::OTYP_RANGE);
            }

            NEON_INLINE bool isModule() const
            {
                return isObjtype(Object::OTYP_MODULE);
            }

            NEON_INLINE bool isCallable() const
            {
                return (isClass() || isFuncscript() || isFuncclosure() || isFuncbound() || isFuncnative());
            }

            NEON_INLINE Object* asObject() const
            {
                return (m_valunion.vobjpointer);
            }

            NEON_INLINE double asNumber() const
            {
                return (m_valunion.vfltnum);
            }

            NEON_INLINE bool asBool() const
            {
                if(isNumber())
                {
                    return asNumber();
                }
                return (m_valunion.vbool);
            }

            NEON_INLINE String* asString() const
            {
                return ((String*)asObject());
            }

            NEON_INLINE Array* asArray() const
            {
                return ((Array*)asObject());
            }

            NEON_INLINE Dict* asDict() const
            {
                return ((Dict*)asObject());
            }

            bool isFalse() const;
    };

    class FuncContext
    {
        public:
            Value thisval;
            Value* argv;
            size_t argc;
    };

    template<typename StoredTyp>
    class ValList
    {
        public:
            static void destroy(ValList* list)
            {
                if(list != nullptr)
                {
                    list->deInit();
                    Memory::sysFree(list);
                }
            }

            static size_t nextCapacity(size_t oldcap)
            {
                if((oldcap) < 8)
                {
                    return 8;
                }
                #if 1
                    return (oldcap * 2);
                #else
                    return (((oldcap * 3) / 2) + 1);
                #endif
            }

            static NEON_INLINE void initItems(StoredTyp* inbuf, size_t begin, size_t end)
            {
                size_t i;
                for(i=begin; i<end; i++)
                {
                    auto tmp = new(&inbuf[i]) StoredTyp();
                    inbuf[i] = *tmp;
                }
            }

        private:
            size_t m_listcapacity = 0;
            size_t m_listcount = 0;
            StoredTyp* m_listitems = nullptr;

        private:
            NEON_INLINE bool removeAtIntern(unsigned int ix)
            {
                size_t tomovebytes;
                void* src;
                void* dest;
                if(ix == (m_listcount - 1))
                {
                    m_listcount--;
                    return true;
                }
                tomovebytes = (m_listcount - 1 - ix) * sizeof(StoredTyp);
                dest = m_listitems + (ix * sizeof(StoredTyp));
                src = m_listitems + ((ix + 1) * sizeof(StoredTyp));
                memmove(dest, src, tomovebytes);
                m_listcount--;
                return true;
            }

        public:
            NEON_INLINE ValList(): ValList(32)
            {
            }

            NEON_INLINE ValList(size_t initialsize)
            {
                m_listcount = 0;
                m_listcapacity = 0;
                m_listitems = nullptr;
                if(initialsize > 0)
                {
                    ensureCapacity(initialsize);
                }
            }

            NEON_INLINE ~ValList()
            {
                //deInit();
            }

            NEON_INLINE void ensureCapacity(size_t needsize)
            {
                size_t ncap;
                size_t oldcap;
                if(m_listcapacity < needsize)
                {
                    oldcap = m_listcapacity;
                    ncap = nextCapacity(oldcap + needsize);
                    m_listcapacity = ncap;
                    if(m_listitems == nullptr)
                    {
                        m_listitems = (StoredTyp*)Memory::sysMalloc(sizeof(StoredTyp) * ncap);
                        initItems(m_listitems, 0, ncap);
                    }
                    else
                    {
                        m_listitems = (StoredTyp*)Memory::sysRealloc(m_listitems, sizeof(StoredTyp) * ncap);
                        initItems(m_listitems, oldcap, ncap);
                    }

                }
            }

            NEON_INLINE void deInit()
            {
                if(m_listitems != nullptr)
                {
                    Memory::sysFree(m_listitems);
                }
                //m_listitems = nullptr;
                m_listcount = 0;
                m_listcapacity = 0;
            }

            NEON_INLINE void clear()
            {
                m_listcount = 0;
            }

            NEON_INLINE size_t count() const
            {
                return m_listcount;
            }

            NEON_INLINE size_t capacity() const
            {
                return m_listcapacity;
            }

            NEON_INLINE StoredTyp* data() const
            {
                return m_listitems;
            }

            NEON_INLINE StoredTyp& get(size_t idx) const
            {
                return m_listitems[idx];
            }

            NEON_INLINE StoredTyp& operator[](size_t idx) const
            {
                return m_listitems[idx];
            }

            NEON_INLINE StoredTyp* getp(size_t idx) const
            {
                return &m_listitems[idx];
            }

            NEON_INLINE StoredTyp* set(size_t idx, const StoredTyp& val)
            {
                size_t need;
                need = idx + 1;
                if(((idx == 0) || (m_listcapacity == 0)) || (idx >= m_listcapacity))
                {
                    ensureCapacity(need);
                }
                if(idx >= m_listcount)
                {
                    m_listcount = idx + 1;
                }
                m_listitems[idx] = val;
                return &m_listitems[idx];
            }

            NEON_INLINE bool push(const StoredTyp& value)
            {
                size_t need;
                size_t oldcap;
                if(m_listcapacity < m_listcount + 1)
                {
                    oldcap = m_listcapacity;
                    need =  nextCapacity(oldcap);
                    ensureCapacity(need);
                }
                m_listitems[m_listcount] = value;
                m_listcount++;
                return true;
            }

            NEON_INLINE bool pop(StoredTyp* dest)
            {
                if(m_listcount > 0)
                {
                    if(dest != nullptr)
                    {
                        *dest = m_listitems[m_listcount - 1];
                    }
                    m_listcount--;
                    return true;
                }
                return false;
            }

            NEON_INLINE bool removeAt(unsigned int ix)
            {
                if(ix >= m_listcount)
                {
                    return false;
                }
                if(ix == 0)
                {
                    m_listitems += sizeof(StoredTyp);
                    m_listcapacity--;
                    m_listcount--;
                    return true;
                }
                return removeAtIntern(ix);
            }

            NEON_INLINE void setEmpty()
            {
                if((m_listcapacity > 0) && (m_listitems != nullptr))
                {
                    memset(m_listitems, 0, sizeof(StoredTyp) * m_listcapacity);
                }
                m_listcount = 0;
                m_listcapacity = 0;
            }
    };

    class Property
    {
        public:
            enum FieldType
            {
                FTYP_INVALID,
                FTYP_VALUE,
                /*
                 * indicates this field contains a function, a pseudo-getter (i.e., ".length")
                 * which is called upon getting
                 */
                FTYP_FUNCTION
            };

            struct GetSet
            {
                Value getter;
                Value setter;
            };

        public:
            bool havegetset;
            FieldType m_fieldtype;
            Value value;
            GetSet getset;

        public:
            static Property makeWithPointer(Value val, FieldType type)
            {
                Property vf;
                memset(&vf, 0, sizeof(Property));
                vf.m_fieldtype = type;
                vf.value = val;
                vf.havegetset = false;
                return vf;
            }

            static Property makeWithGetSet(Value val, Value getter, Value setter, FieldType type)
            {
                bool getisfn;
                bool setisfn;
                Property np;
                np = makeWithPointer(val, type);
                setisfn = setter.isCallable();
                getisfn = getter.isCallable();
                if(getisfn || setisfn)
                {
                    np.getset.setter = setter;
                    np.getset.getter = getter;
                }
                return np;
            }

            static Property make(Value val, FieldType type)
            {
                return makeWithPointer(val, type);
            }

    };

    template <typename HTKeyT, typename HTValT>
    class HashTable
    {
        public:
            /*
            // Maximum load factor of 12/14
            // see: https://engineering.fb.com/2019/04/25/developer-tools/f14/
            */
            static constexpr auto CONF_MAXTABLELOAD = (0.85714286);

            struct Entry
            {
                HTKeyT key;
                Property value;
            };

        public:
            /*
             * FIXME: extremely stupid hack: $m_htactive ensures that a table that was destroyed
             * does not get marked again, et cetera.
             * since destroy() zeroes the data before freeing, $active will be
             * false, and thus, no further marking occurs.
             * obviously the reason is that somewhere a table (from Instance) is being
             * read after being freed, but for now, this will work(-ish).
             */
            bool m_htactive;
            int m_htcount;
            int m_htcapacity;
            Entry* m_htentries;

        public:
            static uint64_t getNextCapacity(uint64_t capacity)
            {
                if(capacity < 4)
                {
                    return 4;
                }
                return Util::roundUpToPowe64(capacity + 1);
            }

            void initTable()
            {
                m_htactive = true;
                m_htcount = 0;
                m_htcapacity = 0;
                m_htentries = nullptr;
            }

            void deInit()
            {
                Memory::sysFree(m_htentries);
            }

            NEON_INLINE size_t count() const
            {
                return m_htcount;
            }

            NEON_INLINE size_t capacity() const
            {
                return m_htcapacity;
            }

            NEON_INLINE Entry* entryatindex(size_t idx) const
            {
                return &m_htentries[idx];
            }

            NEON_INLINE Entry* findentrybyvalue(Entry* entries, int capacity, HTKeyT key) const
            {
                uint32_t hsv;
                uint32_t index;
                Entry* entry;
                Entry* tombstone;
                hsv = Value::hashValue(key);
                index = hsv & (capacity - 1);
                tombstone = nullptr;
                while(true)
                {
                    entry = &entries[index];
                    if(entry->key.isNull())
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
                    else if(Value::compareValues(key, (HTKeyT)entry->key))
                    {
                        return entry;
                    }
                    index = (index + 1) & (capacity - 1);
                }
                return nullptr;
            }

            NEON_INLINE Entry* findentrybystr(Entry* entries, int capacity, HTKeyT valkey, const char* kstr, size_t klen, uint32_t hsv) const
            {
                bool havevalhash;
                uint32_t index;
                uint32_t valhash;
                String* entoskey;
                Entry* entry;
                Entry* tombstone;
                (void)valhash;
                (void)havevalhash;
                valhash = 0;
                havevalhash = false;
                index = hsv & (capacity - 1);
                tombstone = nullptr;
                while(true)
                {
                    entry = &entries[index];
                    if(entry->key.isNull())
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
                        if(Wrappers::wrapStrGetLength(entoskey) == klen)
                        {
                            if(memcmp(kstr, Wrappers::wrapStrGetData(entoskey), klen) == 0)
                            {
                                return entry;
                            }
                        }
                    }
                    else
                    {
                        if(!valkey.isNull())
                        {
                            if(Value::compareValues(valkey, (HTKeyT)entry->key))
                            {
                                return entry;
                            }
                        }
                    }
                    index = (index + 1) & (capacity - 1);
                }
                return nullptr;
            }

            NEON_INLINE Property* getfieldbyvalue(HTKeyT key) const
            {
                Entry* entry;
                if(m_htcount == 0 || m_htentries == nullptr)
                {
                    return nullptr;
                }
                entry = findentrybyvalue(m_htentries, m_htcapacity, key);
                if(entry->key.isNull() || entry->key.isNull())
                {
                    return nullptr;
                }
                return &entry->value;
            }

            NEON_INLINE Property* getfieldbystr(HTKeyT valkey, const char* kstr, size_t klen, uint32_t hsv) const
            {
                Entry* entry;
                if(m_htcount == 0 || m_htentries == nullptr)
                {
                    return nullptr;
                }
                entry = findentrybystr(m_htentries, m_htcapacity, valkey, kstr, klen, hsv);
                if(entry->key.isNull() || entry->key.isNull())
                {
                    return nullptr;
                }
                return &entry->value;
            }

            Property* getfieldbyostr(String* str) const;

            Property* getfieldbycstr(const char* kstr) const;
            Property* getfield(HTKeyT key) const;

            NEON_INLINE bool get(HTKeyT key, HTValT* value) const
            {
                Property* field;
                field = getfield(key);
                if(field != nullptr)
                {
                    *value = (HTValT)field->value;
                    return true;
                }
                return false;
            }

            NEON_INLINE bool adjustcapacity(int capacity)
            {
                int i;
                size_t sz;
                Entry* dest;
                Entry* entry;
                Entry* entries;
                sz = sizeof(Entry) * capacity;
                entries = (Entry*)Memory::sysMalloc(sz);
                if(entries == nullptr)
                {
                    fprintf(stderr, "hashtab:adjustcapacity: failed to allocate %zd bytes\n", sz);
                    abort();
                    return false;
                }
                for(i = 0; i < capacity; i++)
                {
                    entries[i].key = HTKeyT{};
                    entries[i].value = Property::make(HTValT{}, Property::FTYP_VALUE);
                }
                m_htcount = 0;
                for(i = 0; i < m_htcapacity; i++)
                {
                    entry = &m_htentries[i];
                    if(entry->key.isNull())
                    {
                        continue;
                    }
                    dest = findentrybyvalue(entries, capacity, (HTKeyT)entry->key);
                    dest->key = entry->key;
                    dest->value = entry->value;
                    m_htcount++;
                }
                Memory::sysFree(m_htentries);
                m_htentries = entries;
                m_htcapacity = capacity;
                return true;
            }

            NEON_INLINE bool setwithtype(HTKeyT key, HTValT value, Property::FieldType ftyp, bool keyisstring)
            {
                bool isnew;
                int capacity;
                Entry* entry;
                (void)keyisstring;
                if((m_htcount + 1) > (m_htcapacity * CONF_MAXTABLELOAD))
                {
                    capacity = getNextCapacity(m_htcapacity);
                    if(!adjustcapacity(capacity))
                    {
                        return false;
                    }
                }
                entry = findentrybyvalue(m_htentries, m_htcapacity, key);
                isnew = entry->key.isNull();
                if(isnew && entry->value.value.isNull())
                {
                    m_htcount++;
                }
                /* overwrites existing entries. */
                entry->key = key;
                entry->value = Property::make(value, ftyp);
                return isnew;
            }

            NEON_INLINE bool set(HTKeyT key, HTValT value)
            {
                return setwithtype(key, value, Property::FTYP_VALUE, key.isString());
            }

            bool remove(HTKeyT key);

            NEON_INLINE bool copyTo(HashTable* to, bool keepgoing)
            {
                int i;
                int failcnt;
                Entry* entry;
                failcnt = 0;
                for(i = 0; i < m_htcapacity; i++)
                {
                    entry = &m_htentries[i];
                    if(!entry->key.isNull())
                    {
                        if(!to->setwithtype((HTKeyT)entry->key, (HTValT)entry->value.value, entry->value.m_fieldtype, false))
                        {
                            if(keepgoing)
                            {
                                failcnt++;
                            }
                            else
                            {
                                return false;
                            }
                        }
                    }
                }
                if(failcnt == 0)
                {
                    return true;
                }
                return false;
            }

            NEON_INLINE void importall(HashTable* from, HashTable* to)
            {
                int i;
                Entry* entry;
                for(i = 0; i < (int)from->m_htcapacity; i++)
                {
                    entry = &from->m_htentries[i];
                    if(!entry->key.isNull() && !(HTValT)entry->value.value.isModule())
                    {
                        /* Don't import private values */
                        if((HTKeyT)entry->key.isString() && entry->key.asString()->data()[0] == '_')
                        {
                            continue;
                        }
                        to->setwithtype((HTKeyT)entry->key, (HTValT)entry->value.value, entry->value.m_fieldtype, false);
                    }
                }
            }

            NEON_INLINE bool copy(HashTable* to)
            {
                int i;
                Entry* entry;
                NN_NULLPTRCHECK_RETURNVALUE(this, false);
                NN_NULLPTRCHECK_RETURNVALUE(to, false);
                for(i = 0; i < (int)m_htcapacity; i++)
                {
                    entry = &m_htentries[i];
                    if(!entry->key.isNull())
                    {
                        to->setwithtype((HTKeyT)entry->key, Value::copyValue((HTValT)entry->value.value), entry->value.m_fieldtype, false);
                    }
                }
                return true;
            }

            template<typename InputValT>
            NEON_INLINE HTKeyT findkey(InputValT value, InputValT defval)
            {
                int i;
                Entry* entry;
                NN_NULLPTRCHECK_RETURNVALUE(this, Value::makeNull());
                for(i = 0; i < (int)m_htcapacity; i++)
                {
                    entry = &m_htentries[i];
                    if(!entry->key.isNull() && !entry->key.isNull())
                    {
                        if(Value::compareValues((HTValT)entry->value.value, value))
                        {
                            return (HTKeyT)entry->key;
                        }
                    }
                }
                return defval;
            }
    };

    class Instruction
    {
        public:
            enum OpCode
            {
                OPC_GLOBALDEFINE,
                OPC_GLOBALGET,
                OPC_GLOBALSET,
                OPC_LOCALGET,
                OPC_LOCALSET,
                OPC_FUNCARGOPTIONAL,
                OPC_FUNCARGSET,
                OPC_FUNCARGGET,
                OPC_UPVALUEGET,
                OPC_UPVALUESET,
                OPC_UPVALUECLOSE,
                OPC_PROPERTYGET,
                OPC_PROPERTYGETSELF,
                OPC_PROPERTYSET,
                OPC_JUMPIFFALSE,
                OPC_JUMPNOW,
                OPC_LOOP,
                OPC_EQUAL,
                OPC_PRIMGREATER,
                OPC_PRIMLESSTHAN,
                OPC_PUSHEMPTY,
                OPC_PUSHNULL,
                OPC_PUSHTRUE,
                OPC_PUSHFALSE,
                OPC_PRIMADD,
                OPC_PRIMSUBTRACT,
                OPC_PRIMMULTIPLY,
                OPC_PRIMDIVIDE,
                OPC_PRIMMODULO,
                OPC_PRIMPOW,
                OPC_PRIMNEGATE,
                OPC_PRIMNOT,
                OPC_PRIMBITNOT,
                OPC_PRIMAND,
                OPC_PRIMOR,
                OPC_PRIMBITXOR,
                OPC_PRIMSHIFTLEFT,
                OPC_PRIMSHIFTRIGHT,
                OPC_PUSHONE,
                /* 8-bit constant address (0 - 255) */
                OPC_PUSHCONSTANT,
                OPC_ECHO,
                OPC_POPONE,
                OPC_DUPONE,
                OPC_POPN,
                OPC_ASSERT,
                OPC_EXTHROW,
                OPC_MAKECLOSURE,
                OPC_CALLFUNCTION,
                OPC_CALLMETHOD,
                OPC_CLASSINVOKETHIS,
                OPC_RETURN,
                OPC_MAKECLASS,
                OPC_MAKEMETHOD,
                OPC_CLASSGETTHIS,
                OPC_CLASSPROPERTYDEFINE,
                OPC_CLASSINHERIT,
                OPC_CLASSGETSUPER,
                OPC_CLASSINVOKESUPER,
                OPC_CLASSINVOKESUPERSELF,
                OPC_MAKERANGE,
                OPC_MAKEARRAY,
                OPC_MAKEDICT,
                OPC_INDEXGET,
                OPC_INDEXGETRANGED,
                OPC_INDEXSET,
                OPC_EXTRY,
                OPC_EXPOPTRY,
                OPC_EXPUBLISHTRY,
                OPC_STRINGIFY,
                OPC_SWITCH,
                OPC_TYPEOF,
                OPC_OPINSTANCEOF,
                OPC_HALT,
                OPC_BREAK_PL
            };

        public:
            /**
             * helper for Class::makeExceptionClass.
             */
            static Instruction make(bool isop, uint16_t code, int srcline)
            {
                Instruction inst;
                inst.isop = isop;
                inst.code = code;
                inst.fromsourceline = srcline;
                return inst;
            }

        public:
            /* opcode or value */
            uint16_t code;
            /* is this instruction an opcode? */
            uint8_t isop : 1;
            /* line corresponding to where this instruction was emitted */
            uint8_t fromsourceline;
    };

    class Blob
    {
        public:
            static void init(Blob* blob)
            {
                blob->m_count = 0;
                blob->m_capacity = 0;
            }

            static void destroy(Blob* blob)
            {
                blob->m_instrucs.deInit();
                blob->m_constants.deInit();
                blob->m_argdefvals.deInit();
            }

        public:
            int m_count;
            int m_capacity;
            ValList<Instruction> m_instrucs;
            ValList<Value> m_constants;
            ValList<Value> m_argdefvals;

        public:
            Blob()
            {
                init(this);
            }

            void push(Instruction ins)
            {
                m_instrucs.push(ins);
                m_count++;
            }

            int addConstant(Value value)
            {
                m_constants.push(value);
                return m_constants.count() - 1;
            }
    };

    class CallFrame
    {
        public:
            enum
            {
                /* how many catch() clauses per try statement */
                CONF_MAXEXCEPTHANDLERS = (4),
            };

            struct ExceptionInfo
            {
                uint16_t address = 0;
                uint16_t finallyaddress = 0;
            };

        public:
            int m_handlercount = 0;
            int gcprotcount = 0;
            int stackslotpos = 0;
            Instruction* inscode = nullptr;
            Function* closure = nullptr;
            /* TODO: should be dynamically allocated */
            ExceptionInfo handlers[CONF_MAXEXCEPTHANDLERS];
    };

    class ProcessInfo
    {
        public:
            int cliprocessid;
            Array* cliargv;
            String* cliexedirectory;
            String* cliscriptfile;
            String* cliscriptdirectory;
            File* filestdout;
            File* filestderr;
            File* filestdin;
    };

    class SharedState
    {
        public:
            /*
            * (1024*1024) * 1 = 1mb
            *             * 2 = 2mb
            * et cetera
            */
            static constexpr auto CONF_DEFAULTGCSTART = ((1024 * 1024) * 1);

            /* growth factor for GC heap objects */
            static constexpr auto CONF_GCHEAPGROWTHFACTOR = 1.25;

            /* maximum number of syntax errors to show before bailing out */
            static constexpr auto CONF_MAXSYNTAXERRORS = 10;

            class InternedStringTab
            {
                public:
                    HashTable<Value, Value> m_htab;

                public:
                    InternedStringTab()
                    {
                        m_htab.initTable();
                    }

                    void deInit()
                    {
                        m_htab.deInit();
                    }

                    void store(String* os)
                    {
                        m_htab.set(Value::fromObject(os), Value::makeNull());
                    }

                    String* findstring(const char* findstr, size_t findlen, uint32_t findhash)
                    {
                        size_t slen;
                        uint32_t index;
                        const char* sdata;
                        String* string;
                        (void)findstr;
                        (void)sdata;
                        NN_NULLPTRCHECK_RETURNVALUE(m_htab, nullptr);
                        if(m_htab.m_htcount == 0)
                        {
                            return nullptr;
                        }
                        index = findhash & (m_htab.m_htcapacity - 1);
                        while(true)
                        {
                            auto entry = &m_htab.m_htentries[index];
                            if(entry->key.isNull())
                            {
                                /*
                                // stop if we find an empty non-tombstone entry
                                //if (entry->value.isNull())
                                */
                                {
                                    return nullptr;
                                }
                            }
                            string = entry->key.asString();
                            slen = Wrappers::wrapStrGetLength(string);
                            sdata = Wrappers::wrapStrGetData(string);
                            if((slen == findlen) && (Wrappers::wrapStrGetHash(string) == findhash))
                            {
                                //if(memcmp(sdata, findstr, findlen) == 0)
                                {
                                    /* we found it */
                                    return string;
                                }
                            }
                            index = (index + 1) & (m_htab.m_htcapacity - 1);
                        }
                        return nullptr;
                    }
            };

        public:
            static SharedState* m_myself;

            struct
            {
                /* for switching through the command line args... */
                bool enablewarnings;
                bool dumpbytecode;
                bool exitafterbytecode;
                bool shoulddumpstack;
                bool enablestrictmode;
                bool showfullstack;
                bool enableapidebug;
                int maxsyntaxerrors;
            } m_conf;

            struct
            {
                int64_t graycount;
                int64_t graycapacity;
                int64_t bytesallocated;
                int64_t nextgc;
                Object** graystack;
            } m_gcstate;

            struct
            {
                bool m_unhandledexceptionstate;
                int64_t stackidx;
                int64_t stackcapacity;
                int64_t framecapacity;
                int64_t framecount;
                Instruction currentinstr;
                CallFrame* currentframe;
                Upvalue* openupvalues;
                ValList<CallFrame> framevalues;
                ValList<Value> stackvalues;
            } m_vmstate;

            struct
            {
                Class* stdexception;
                Class* syntaxerror;
                Class* asserterror;
                Class* ioerror;
                Class* oserror;
                Class* argumenterror;
                Class* regexerror;
                Class* importerror;
            } m_exceptions;

            struct
            {
                /* __indexget__ */
                String* nmindexget;
                /* __indexset__ */
                String* nmindexset;
                /* __add__ */
                String* nmadd;
                /* __sub__ */
                String* nmsub;
                /* __div__ */
                String* nmdiv;
                /* __mul__ */
                String* nmmul;
                /* __band__ */
                String* nmband;
                /* __bor__ */
                String* nmbor;
                /* __bxor__ */
                String* nmbxor;

                String* nmconstructor;
            } m_defaultstrings;

            Object* linkedobjects;
            bool markvalue;
            InternedStringTab m_allocatedstrings;
            HashTable<Value, Value> m_openedmodules;
            HashTable<Value, Value> m_declaredglobals;

            /*
             * these classes are used for runtime objects, specifically in getClassFor.
             * every other global class needed (as created by Class::makeScriptClass) need NOT be declared here.
             * simply put, these classes are primitive types.
             */
            /* the class from which every other class derives */
            Class* m_classprimobject;
            /**/
            Class* m_classprimclass;
            /* class for numbers, et al */
            Class* m_classprimnumber;
            /* class for strings */
            Class* m_classprimstring;
            /* class for arrays */
            Class* m_classprimarray;
            /* class for dictionaries */
            Class* m_classprimdict;
            /* class for I/O file objects */
            Class* m_classprimfile;
            Class* m_classprimdirectory;
            /* class for range constructs */
            Class* m_classprimrange;
            /* class for anything callable: functions, lambdas, constructors ... */
            Class* m_classprimcallable;
            Class* m_classprimprocess;

            bool m_isrepl;
            ProcessInfo* m_processinfo;

            /* miscellaneous */
            IOStream* m_stdoutprinter;
            IOStream* m_stderrprinter;
            IOStream* m_debugwriter;
            const char* m_rootphysfile;
            Dict* m_envdict;
            ValList<Value> m_importpath;

            Value m_lastreplvalue;
            void* m_memuserptr;
            Module* m_topmodule;

        private:
            static bool defVarsFor(SharedState* gcs)
            {
                gcs->markvalue = true;
                gcs->m_gcstate.bytesallocated = 0;
                /* default is 1mb. Can be modified via the -g flag. */
                gcs->m_gcstate.nextgc = CONF_DEFAULTGCSTART;
                gcs->m_gcstate.graycount = 0;
                gcs->m_gcstate.graycapacity = 0;
                gcs->m_gcstate.graystack = nullptr;
                return true;
            }

        public:
            static bool init()
            {
                SharedState::m_myself = Memory::make<SharedState>();
                return defVarsFor(SharedState::m_myself);
            }

            static void destroy()
            {
                Memory::destroy(SharedState::m_myself);
            }

            static NEON_INLINE SharedState* get()
            {
                return SharedState::m_myself;
            }

            template<typename... ArgsT>
            static void raiseWarning(const char* fmt, ArgsT&&... args)
            {
                auto gcs = SharedState::get();
                if(gcs->m_conf.enablewarnings)
                {
                    fprintf(stderr, "WARNING: ");
                    fprintf(stderr, fmt, args...);
                    fprintf(stderr, "\n");
                }
            }

            template<typename InputT>
            static InputT* gcMakeObject(Object::Type type, bool retain)
            {
                size_t size = sizeof(InputT);
                auto gcs = SharedState::get();
                auto temp = (InputT*)SharedState::gcAllocate(size, 1, retain);
                auto object = new(temp) InputT();
                object->m_objtype = type;
                object->m_objmark = !gcs->markvalue;
                object->m_objstale = false;
                object->m_objnext = gcs->linkedobjects;
                gcs->linkedobjects = object;
                return object;
            }

            static void* gcAllocate(size_t newsize, size_t amount, bool retain)
            {
                size_t oldsize;
                void* result;
                auto gcs = SharedState::get();
                oldsize = 0;
                if(!retain)
                {
                    gcs->gcMaybeCollect(newsize - oldsize, newsize > oldsize);
                }
                result = Memory::sysMalloc(newsize * amount);
                /*
                // just in case reallocation fails... computers ain't infinite!
                */
                if(result == nullptr)
                {
                    fprintf(stderr, "fatal error: failed to allocate %zd bytes\n", newsize);
                    abort();
                }
                return result;
            }

            /*
            // NOTE:
            // 1. Any call to SharedState::gcProtect() within a function/block must be accompanied by
            // at least one call to SharedState::clearGCProtect() before exiting the function/block
            // otherwise, expect unexpected behavior
            // 2. The call to SharedState::clearGCProtect() will be automatic for native functions.
            // 3. $thisval must be retrieved before any call to SharedState::gcProtect in a
            // native function.
            */
            template<typename InputT>
            static InputT* gcProtect(InputT* object)
            {
                size_t frpos;
                auto gcs = SharedState::get();
                gcs->vmStackPush(Value::fromObject(object));
                frpos = 0;
                if(gcs->m_vmstate.framecount > 0)
                {
                    frpos = gcs->m_vmstate.framecount - 1;
                }
                gcs->m_vmstate.framevalues[frpos].gcprotcount++;
                return object;
            }

            static NEON_INLINE void clearGCProtect()
            {
                size_t frpos;
                CallFrame* frame;
                frpos = 0;
                auto gcs = SharedState::get();
                if(gcs->m_vmstate.framecount > 0)
                {
                    frpos = gcs->m_vmstate.framecount - 1;
                }
                frame = &gcs->m_vmstate.framevalues[frpos];
                if(frame->gcprotcount > 0)
                {
                    if(frame->gcprotcount > 0)
                    {
                        gcs->m_vmstate.stackidx -= frame->gcprotcount;
                    }
                    frame->gcprotcount = 0;
                }
            }

            static void markValue(Value value)
            {
                if(value.isObject())
                {
                    Object::markObject(value.asObject());
                }
            }

            void gcMarkRoots();

            void gcTraceRefs()
            {
                Object* object;
                while(m_gcstate.graycount > 0)
                {
                    m_gcstate.graycount--;
                    object = m_gcstate.graystack[m_gcstate.graycount];
                    Object::blackenObject(object);
                }
            }

            void gcSweep()
            {
                Object* object;
                Object* previous;
                Object* unreached;
                previous = nullptr;
                object = linkedobjects;
                while(object != nullptr)
                {
                    if(object->m_objmark == markvalue)
                    {
                        previous = object;
                        object = object->m_objnext;
                    }
                    else
                    {
                        unreached = object;
                        object = object->m_objnext;
                        if(previous != nullptr)
                        {
                            previous->m_objnext = object;
                        }
                        else
                        {
                            linkedobjects = object;
                        }
                        Object::destroyObject(unreached);
                    }
                }
            }

            void gcLinkedObjectsDestroy()
            {
                Object* next;
                Object* object;
                object = linkedobjects;
                while(object != nullptr)
                {
                    next = object->m_objnext;
                    Object::destroyObject(object);
                    object = next;
                }
                Memory::sysFree(m_gcstate.graystack);
                m_gcstate.graystack = nullptr;
            }

            void gcMarkCompilerRoots()
            {
                /*
                FuncCompiler* fnc;
                fnc = ->fnc;
                while(fnc != nullptr)
                {
                    Object::markObject((Object*)fnc->m_targetfunc);
                    fnc = fnc->m_enclosing;
                }
                */
            }

            void gcCollectGarbage()
            {
                size_t before;
                (void)before;
                /*
                //  REMOVE THE NEXT LINE TO DISABLE NESTED gcCollectGarbage() POSSIBILITY!
                */
                m_gcstate.nextgc = m_gcstate.bytesallocated;
                gcMarkRoots();
                gcTraceRefs();
                Value::valtabRemoveWhites(&m_allocatedstrings.m_htab);
                Value::valtabRemoveWhites(&m_openedmodules);
                gcSweep();
                m_gcstate.nextgc = m_gcstate.bytesallocated * CONF_GCHEAPGROWTHFACTOR;
                markvalue = !markvalue;
            }

            void gcMaybeCollect(int addsize, bool wasnew)
            {
                m_gcstate.bytesallocated += addsize;
                if(m_gcstate.nextgc > 0)
                {
                    if(wasnew && m_gcstate.bytesallocated > m_gcstate.nextgc)
                    {
                        if(m_vmstate.currentframe && m_vmstate.currentframe->gcprotcount == 0)
                        {
                            gcCollectGarbage();
                        }
                    }
                }
            }

            template<typename InputT>
            void gcRelease(InputT* pointer, size_t oldsize)
            {
                gcMaybeCollect(-oldsize, false);
                if(oldsize > 0)
                {
                    #if 0
                    memset(pointer, 0, oldsize);
                    #endif
                }
                // N.B.: Memory::destroy() MIGHT call InputT::destroy() if InputT defines such a function,
                // which would lead to illegal double-frees!
                Memory::sysFree(pointer);
                pointer = nullptr;
            }

            template<typename InputT>
            void gcRelease(InputT* pointer)
            {
                gcRelease(pointer, sizeof(InputT));
            }

            Function* compileSourceToFunction(Module* module, bool fromeval, const char* source, bool toplevel);
            Status execSource(Module* module, const char* source, const char* filename, Value* dest);
            Value evalSource(const char* source);

            bool vmExceptionPushHandler(Class* type, int address, int finallyaddress);
            Value vmExceptionGetStackTrace();
            bool vmExceptionPropagate();

            /**
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
            NEON_INLINE bool resizeStack(size_t needed)
            {
                size_t oldsz;
                size_t newsz;
                size_t nforvals;
                nforvals = (needed * 2);
                oldsz = m_vmstate.stackcapacity;
                newsz = oldsz + nforvals;
                m_vmstate.stackvalues.ensureCapacity(newsz);
                m_vmstate.stackcapacity = m_vmstate.stackvalues.capacity();
                return true;
            }

            NEON_INLINE bool resizeFrames(size_t needed)
            {
                size_t oldsz;
                size_t newsz;
                int oldhandlercnt;
                Instruction* oldip;
                Function* oldclosure;
                oldclosure = m_vmstate.currentframe->closure;
                oldip = m_vmstate.currentframe->inscode;
                oldhandlercnt = m_vmstate.currentframe->m_handlercount;
                oldsz = m_vmstate.framecapacity;
                newsz = oldsz + needed;

                m_vmstate.framevalues.ensureCapacity(newsz);
                m_vmstate.framecapacity = m_vmstate.framevalues.capacity();

                /*
                 * this bit is crucial: realloc changes pointer addresses, and to keep the
                 * current frame, re-read it from the new address.
                 */
                m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                m_vmstate.currentframe->m_handlercount = oldhandlercnt;
                m_vmstate.currentframe->inscode = oldip;
                m_vmstate.currentframe->closure = oldclosure;
                return true;
            }

            NEON_INLINE bool checkMaybeResizeStack()
            {
                if((m_vmstate.stackidx + 1) >= m_vmstate.stackcapacity)
                {
                    if(!resizeStack((m_vmstate.stackidx + 1)))
                    {
                        fprintf(stderr, "failed to resize stack due to overflow");
                        return false;
                    }
                    return true;
                }
                return false;
            }

            NEON_INLINE bool checkMaybeResizeFrames()
            {
                //fprintf(stderr, "checking if frames need resizing...\n");
                if(m_vmstate.framecount >= m_vmstate.framecapacity)
                {
                    if(!resizeFrames((m_vmstate.framecapacity + 1)))
                    {
                        fprintf(stderr, "failed to resize frames due to overflow");
                        return false;
                    }
                    return true;
                }
                return false;
            }

            /* initial amount of frames (will grow dynamically if needed) */
            #define NEON_CONFIG_INITFRAMECOUNT (16)

            /* initial amount of stack values (will grow dynamically if needed) */
            #define NEON_CONFIG_INITSTACKCOUNT (4 * 1)

            void resetVMState()
            {
                m_vmstate.framecount = 0;
                m_vmstate.stackidx = 0;
                m_vmstate.openupvalues = nullptr;
            }

            void initVMState()
            {
                linkedobjects = nullptr;
                m_vmstate.m_unhandledexceptionstate = false;
                m_vmstate.currentframe = nullptr;
                {
                    m_vmstate.stackcapacity = NEON_CONFIG_INITSTACKCOUNT;
                    m_vmstate.stackvalues.ensureCapacity(NEON_CONFIG_INITSTACKCOUNT);
                }
                {
                    m_vmstate.framecapacity = NEON_CONFIG_INITFRAMECOUNT;
                    m_vmstate.framevalues.ensureCapacity(NEON_CONFIG_INITFRAMECOUNT);
                }
            }

            /*
             * don't try to further optimize vmbits functions, unless you *really* know what you are doing.
             * they were initially macros; but for better debugging, and better type-enforcement, they
             * are now inlined functions.
             */

            NEON_INLINE uint16_t vmReadByte()
            {
                uint16_t r;
                r = m_vmstate.currentframe->inscode->code;
                m_vmstate.currentframe->inscode++;
                return r;
            }

            NEON_INLINE Instruction vmReadInstruction()
            {
                Instruction r;
                r = *m_vmstate.currentframe->inscode;
                m_vmstate.currentframe->inscode++;
                return r;
            }

            NEON_INLINE uint16_t vmReadShort()
            {
                uint16_t b;
                uint16_t a;
                a = m_vmstate.currentframe->inscode[0].code;
                b = m_vmstate.currentframe->inscode[1].code;
                m_vmstate.currentframe->inscode += 2;
                return (uint16_t)((a << 8) | b);
            }

            Value vmReadConst()
            {
                uint16_t idx;
                idx = vmReadShort();
                auto blob = Wrappers::wrapGetBlobOfClosure(m_vmstate.currentframe->closure);
                return blob->m_constants.get(idx);
            }

            NEON_INLINE String* vmReadString()
            {
                return vmReadConst().asString();
            }

            NEON_INLINE Value vmStackPeek(int distance)
            {
                Value v;
                v = m_vmstate.stackvalues[m_vmstate.stackidx + (-1 - distance)];
                return v;
            }

            NEON_INLINE void vmStackPush(Value value)
            {
                checkMaybeResizeStack();
                m_vmstate.stackvalues[m_vmstate.stackidx] = value;
                m_vmstate.stackidx++;
            }

            NEON_INLINE auto vmStackPop()
            {
                m_vmstate.stackidx--;
                if(m_vmstate.stackidx < 0)
                {
                    m_vmstate.stackidx = 0;
                }
                return m_vmstate.stackvalues[m_vmstate.stackidx];
            }

            NEON_INLINE auto vmStackPop(int n)
            {
                m_vmstate.stackidx -= n;
                if(m_vmstate.stackidx < 0)
                {
                    m_vmstate.stackidx = 0;
                }
                return m_vmstate.stackvalues[m_vmstate.stackidx];
            }

            Status runVM(int exitframe, Value* rv);
            NEON_INLINE bool vmDoBinaryFunc(const char* opname, BinOpFuncFN opfn);
            NEON_INLINE bool vmDoMakeDict();
            NEON_INLINE bool vmDoMakeArray();
            NEON_INLINE bool vmDoMakeClosure();
            NEON_INLINE bool vmDoBinaryDirect();
            NEON_INLINE bool vmDoGlobalDefine();
            NEON_INLINE bool vmDoGlobalGet();
            NEON_INLINE bool vmDoGlobalSet();
            NEON_INLINE bool vmDoLocalGet();
            NEON_INLINE bool vmDoLocalSet();
            NEON_INLINE bool vmDoFuncArgOptional();
            NEON_INLINE bool vmDoFuncArgGet();
            NEON_INLINE bool vmDoFuncArgSet();
            NEON_INLINE bool vmUtilBindMethod(Class* klass, String* name);
            NEON_INLINE Property* vmUtilGetProperty(Value peeked, String* name);
            NEON_INLINE bool vmDoPropertyGetNormal();
            NEON_INLINE bool vmDoPropertyGetSelf();
            NEON_INLINE bool vmDoPropertySet();
            NEON_INLINE bool vmDoIndexGet();
            NEON_INLINE bool vmUtilConcatenate();
            NEON_INLINE bool vmDoIndexSet();
            NEON_INLINE bool vmUtilDoSetIndexString(String* os, Value index, Value value);
            NEON_INLINE bool vmUtilDoSetIndexArray(Array* list, Value index, Value value);
            NEON_INLINE bool vmUtilDoSetIndexModule(Module* module, Value index, Value value);
            NEON_INLINE bool vmUtilDoSetIndexDict(Dict* dict, Value index, Value value);

            bool vmDoCallClosure(Function* closure, Value thisval, size_t argcount, bool fromoperator);
            NEON_INLINE bool vmDoCallNative(Function* native, Value thisval, size_t argcount);
            bool vmCallWithObject(Value callable, Value thisval, size_t argcount, bool fromoper);
            bool vmCallValue(Value callable, Value thisval, size_t argcount, bool fromoperator);

            Class* getClassFor(Value receiver);

            NEON_INLINE bool vmUtilInvokeMethodFromClass(Class* klass, String* name, size_t argcount);
            NEON_INLINE bool vmUtilInvokeMethodSelf(String* name, size_t argcount);
            NEON_INLINE bool vmUtilInvokeMethodNormal(String* name, size_t argcount);
            NEON_INLINE Upvalue* vmUtilUpvaluesCapture(Value* local, int stackpos);
            NEON_INLINE void vmUtilUpvaluesClose(const Value* last);
            NEON_INLINE void vmUtilDefineMethod(String* name);
            NEON_INLINE void vmUtilDefineProperty(String* name, bool isstatic);
            NEON_INLINE String* vmUtilMultString(String* str, double number);
            NEON_INLINE Array* vmUtilCombineArrays(Array* a, Array* b);
            NEON_INLINE void vmUtilMultArray(Array* from, Array* newlist, size_t times);
            NEON_INLINE bool vmUtilDoGetRangedIndexOfArray(Array* list, bool willassign);
            NEON_INLINE bool vmUtilDoGetRangedIndexOfString(String* string, bool willassign);
            NEON_INLINE bool vmDoGetRangedIndex();
            NEON_INLINE bool vmUtilDoIndexGetDict(Dict* dict, bool willassign);
            NEON_INLINE bool vmUtilDoIndexGetModule(Module* module, bool willassign);
            NEON_INLINE bool vmUtilDoIndexGetString(String* string, bool willassign);
            NEON_INLINE bool vmUtilDoIndexGetArray(Array* list, bool willassign);
            Property* vmUtilCheckOverloadRequirements(const char* ccallername, Value target, String* name);
            NEON_INLINE bool vmUtilTryOverloadBasic(String* name, Value target, Value firstargvval, Value setvalue, bool willassign);
            NEON_INLINE bool vmUtilTryOverloadMath(String* name, Value target, Value right, bool willassign);
            NEON_INLINE bool vmUtilTryOverloadGeneric(String* name, Value target, bool willassign);
            NEON_INLINE Property* vmUtilGetClassProperty(Class* klass, String* name, bool alsothrow);

            int vmNestCallPrepare(Value callable, Value mthobj, Value* callarr, int maxcallarr)
            {
                int arity;
                Function* ofn;
                (void)maxcallarr;
                arity = 0;
                ofn = callable.asFunction();
                if(callable.isFuncclosure())
                {
                    arity = Wrappers::wrapGetArityOfClosure(ofn);
                }
                else if(callable.isFuncscript())
                {
                    arity = Wrappers::wrapGetArityOfFuncScript(ofn);
                }
                else if(callable.isFuncnative())
                {
            #if 0
                        arity = ofn;
            #endif
                }
                if(arity > 0)
                {
                    callarr[0] = Value::makeNull();
                    if(arity > 1)
                    {
                        callarr[1] = Value::makeNull();
                        if(arity > 2)
                        {
                            callarr[2] = mthobj;
                        }
                    }
                }
                return arity;
            }

            /* helper function to access call outside the file. */
            bool vmNestCallFunction(Value callable, Value thisval, Value* argv, size_t argc, Value* dest, bool fromoper)
            {
                bool needvm;
                size_t i;
                int64_t pidx;
                Status status;
                Value rtv;
                pidx = m_vmstate.stackidx;
                /* set the closure before the args */
                vmStackPush(callable);
                if((argv != nullptr) && (argc > 0))
                {
                    for(i = 0; i < argc; i++)
                    {
                        vmStackPush(argv[i]);
                    }
                }
                if(!vmCallWithObject(callable, thisval, argc, fromoper))
                {
                    fprintf(stderr, "nestcall: vmCallValue() failed\n");
                    abort();
                }
                needvm = true;
                if(callable.isFuncnative())
                {
                    needvm = false;
                }
                if(needvm)
                {
                    status = runVM(m_vmstate.framecount - 1, nullptr);
                    if(status != Status::Ok)
                    {
                        fprintf(stderr, "nestcall: call to runvm failed\n");
                        abort();
                    }
                }
                rtv = m_vmstate.stackvalues[m_vmstate.stackidx - 1];
                *dest = rtv;
                vmStackPop(argc + 0);
                m_vmstate.stackidx = pidx;
                return true;
            }

    };

    SharedState* SharedState::m_myself = nullptr;

    class Array : public Object
    {
        public:
            ValList<Value> m_objvarray;

        public:
            static Array* makeFilled(size_t cnt, Value filler)
            {
                size_t i;
                Array* list;
                list = SharedState::gcMakeObject<Array>(Object::OTYP_ARRAY, false);
                if(cnt > 0)
                {
                    for(i = 0; i < cnt; i++)
                    {
                        list->push(filler);
                    }
                }
                return list;
            }

            static Array* make()
            {
                return makeFilled(0, Value::makeNull());
            }

            NEON_INLINE void push(Value value)
            {
                auto gcs = SharedState::get();
                (void)gcs;
                /*gcs->vmStackPush(value);*/
                m_objvarray.push(value);
                /*gcs->vmStackPop(); */
            }

            NEON_INLINE size_t count() const
            {
                return m_objvarray.count();
            }

            NEON_INLINE Value* data() const
            {
                return (Value*)m_objvarray.data();
            }

            NEON_INLINE bool pop(Value* dest)
            {
                return m_objvarray.pop(dest);
            }

            NEON_INLINE bool set(size_t idx, Value val)
            {
                return m_objvarray.set(idx, val);
            }

            NEON_INLINE bool get(size_t idx, Value* vdest) const
            {
                size_t vc;
                vc = count();
                if((vc > 0) && (idx < vc))
                {
                    *vdest = m_objvarray.get(idx);
                    return true;
                }
                return false;
            }

            NEON_INLINE Value get(size_t idx) const
            {
                Value vd;
                if(get(idx, &vd))
                {
                    return vd;
                }
                return Value::makeNull();
            }

            NEON_INLINE Array* copy(long start, long length)
            {
                size_t i;
                Array* newlist;
                newlist = Array::make();
                if(start == -1)
                {
                    start = 0;
                }
                if(length == -1)
                {
                    length = count() - start;
                }
                for(i = start; i < (size_t)(start + length); i++)
                {
                    newlist->push(get(i));
                }
                return newlist;
            }
    };

    class Dict : public Object
    {
        public:
            ValList<Value> m_htkeys;
            HashTable<Value, Value> m_htvalues;

        public:
            static Dict* make()
            {
                Dict* dict;
                dict = SharedState::gcMakeObject<Dict>(Object::OTYP_DICT, false);
                dict->m_htvalues.initTable();
                return dict;
            }

            static void destroy(Dict* dict)
            {
                dict->m_htkeys.deInit();
                dict->m_htvalues.deInit();
            }

            bool set(Value key, Value value)
            {
                Value tempvalue;
                if(!m_htvalues.get(key, &tempvalue))
                {
                    /* add key if it doesn't exist. */
                    m_htkeys.push(key);
                }
                return m_htvalues.set(key, value);
            }

            void add(Value key, Value value)
            {
                set(key, value);
            }

            void addCstr(const char* ckey, Value value)
            {
                String* os;
                os = Wrappers::wrapStringCopy(ckey);
                add(Value::fromObject(os), value);
            }

            Property* get(Value key)
            {
                return m_htvalues.getfield(key);
            }

            Dict* copy()
            {
                size_t i;
                size_t dsz;
                Value key;
                Property* field;
                Dict* ndict;
                ndict = Dict::make();
                /*
                // @TODO: Figure out how to handle dictionary values correctly
                // remember that copying keys is redundant and unnecessary
                */
                dsz = m_htkeys.count();
                for(i = 0; i < dsz; i++)
                {
                    key = m_htkeys.get(i);
                    field = m_htvalues.getfield(m_htkeys.get(i));
                    ndict->m_htkeys.push(key);
                    ndict->m_htvalues.setwithtype(key, field->value, field->m_fieldtype, key.isString());
                }
                return ndict;
            }
    };

    class String : public Object
    {
        public:
            static String* utilNumberToBinString(long n)
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
                return String::copy(newstr, length);
                /*
                //  // To store the binary number
                //  int64_t number = 0;
                //  int cnt = 0;
                //  while (n != 0) {
                //    int64_t rem = n % 2;
                //    int64_t c = (int64_t) pow(10, cnt);
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
                //  return String::copy(str, length);
                */
            }

            static String* utilNumberToOctString(int64_t n, bool numeric)
            {
                int length;
                /* assume maximum of 64 bits + 2 octal indicators (0c) */
                char str[66];
                length = sprintf(str, numeric ? "0c%lo" : "%lo", n);
                return String::copy(str, length);
            }

            static String* utilNumberToHexString(int64_t n, bool numeric)
            {
                int length;
                /* assume maximum of 64 bits + 2 hex indicators (0x) */
                char str[66];
                length = sprintf(str, numeric ? "0x%lx" : "%lx", n);
                return String::copy(str, length);
            }

            static Value regexMatch(String* string, String* pattern, bool capture)
            {
                enum
                {
                    matchMaxTokens = 128 * 4,
                    matchMaxCaptures = 128,
                };
                int prc;
                int64_t i;
                int64_t mtstart;
                int64_t mtlength;
                int64_t actualmaxcaptures;
                int64_t cpres;
                int16_t restokens;
                int64_t capstarts[matchMaxCaptures + 1];
                int64_t caplengths[matchMaxCaptures + 1];
                RegexContext::Token tokens[matchMaxTokens + 1];
                auto gcs = SharedState::get();
                memset(tokens, 0, (matchMaxTokens + 1) * sizeof(RegexContext::Token));
                memset(caplengths, 0, (matchMaxCaptures + 1) * sizeof(int64_t));
                memset(capstarts, 0, (matchMaxCaptures + 1) * sizeof(int64_t));
                const char* strstart;
                String* rstr;
                Array* oa;
                Dict* dm;
                restokens = matchMaxTokens;
                actualmaxcaptures = 0;
                RegexContext pctx(tokens, restokens);
                if(capture)
                {
                    actualmaxcaptures = matchMaxCaptures;
                }
                prc = pctx.parse(pattern->data(), 0);
                if(prc == 0)
                {
                    cpres = pctx.match(string->data(), 0, actualmaxcaptures, capstarts, caplengths);
                    if(cpres > 0)
                    {
                        if(capture)
                        {
                            oa = Array::make();
                            for(i = 0; i < cpres; i++)
                            {
                                mtstart = capstarts[i];
                                mtlength = caplengths[i];
                                if(mtlength > 0)
                                {
                                    strstart = &string->data()[mtstart];
                                    rstr = String::copy(strstart, mtlength);
                                    dm = Dict::make();
                                    dm->addCstr("string", Value::fromObject(rstr));
                                    dm->addCstr("start", Value::makeNumber(mtstart));
                                    dm->addCstr("length", Value::makeNumber(mtlength));
                                    oa->push(Value::fromObject(dm));
                                }
                            }
                            return Value::fromObject(oa);
                        }
                        else
                        {
                            return Value::makeBool(true);
                        }
                    }
                }
                else
                {
                    NEON_THROWCLASSWITHSOURCEINFO(gcs->m_exceptions.regexerror, pctx.m_errorbuf);
                }
                RegexContext::destroy(&pctx);
                if(capture)
                {
                    return Value::makeNull();
                }
                return Value::makeBool(false);
            }

            static void strtabStore(String* os)
            {
                auto gcs = SharedState::get();
                gcs->m_allocatedstrings.store(os);
            }

            static String* strTabFind(const char* str, size_t len, uint32_t hsv)
            {
                String* rs;
                auto gcs = SharedState::get();
                rs = gcs->m_allocatedstrings.findstring(str, len, hsv);
                return rs;
            }

            static String* makeFromStrbuf(const StrBuffer& buf, uint32_t hsv, size_t length)
            {
                String* rs;
                rs = SharedState::gcMakeObject<String>(Object::OTYP_STRING, false);
                rs->m_sbuf = buf;
                rs->m_hashvalue = hsv;
                if(length > 0)
                {
                    strtabStore(rs);
                }
                return rs;
            }

            static void destroy(String* str)
            {
                StrBuffer::destroyFromPtr(&str->m_sbuf);
            }

            static String* intern(const char* strdata, int length)
            {
                uint32_t hsv;
                StrBuffer buf;
                String* rs;
                hsv = Util::hashString(strdata, length);
                rs = strTabFind(strdata, length, hsv);
                if(rs != nullptr)
                {
                    return rs;
                }
                StrBuffer::fromPtr(&buf, 0);
                buf.m_isintern = true;
                buf.setData(strdata);
                buf.setLength(length);
                return makeFromStrbuf(buf, hsv, length);
            }

            static String* intern(const char* strdata)
            {
                return intern(strdata, strlen(strdata));
            }

            static String* take(char* strdata, int length)
            {
                uint32_t hsv;
                String* rs;
                StrBuffer buf;
                hsv = Util::hashString(strdata, length);
                rs = strTabFind(strdata, length, hsv);
                if(rs != nullptr)
                {
                    Memory::sysFree(strdata);
                    return rs;
                }
                StrBuffer::fromPtr(&buf, 0);
                buf.setData(strdata);
                buf.setLength(length);
                return makeFromStrbuf(buf, hsv, length);
            }

            static String* take(char* strdata)
            {
                return take(strdata, strlen(strdata));
            }

            static String* copy(const char* strdata, int length)
            {
                uint32_t hsv;
                StrBuffer sb;
                String* rs;
                hsv = Util::hashString(strdata, length);
                if(length == 0)
                {
                    return intern(strdata, length);
                }
                {
                    rs = strTabFind(strdata, length, hsv);
                    if(rs != nullptr)
                    {
                        return rs;
                    }
                }
                StrBuffer::fromPtr(&sb, length);
                sb.append(strdata, length);
                rs = makeFromStrbuf(sb, hsv, length);
                return rs;
            }

            static String* copy(const char* strdata)
            {
                return copy(strdata, strlen(strdata));
            }

            static String* copyObject(String* origos)
            {
                return copy(origos->data(), origos->length());
            }

        public:
            uint32_t m_hashvalue;
            StrBuffer m_sbuf;

        public:
            const char* data() const
            {
                return m_sbuf.data();
            }

            char* mutdata()
            {
                return m_sbuf.data();
            }

            size_t length() const
            {
                return m_sbuf.length();
            }

            bool setLength(size_t nlen)
            {
                m_sbuf.setLength(nlen);
                return true;
            }

            bool set(size_t idx, int byte)
            {
                m_sbuf.set(idx, byte);
                return true;
            }

            int get(size_t idx)
            {
                return m_sbuf.get(idx);
            }

            bool append(const char* str, size_t len)
            {
                return m_sbuf.append(str, len);
            }

            bool append(const char* str)
            {
                return append(str, strlen(str));
            }

            bool appendObject(String* other)
            {
                return append(other->data(), other->length());
            }

            bool appendByte(int ch)
            {
                char cch = ch;
                return m_sbuf.append(&cch, 1);
            }

            template<typename... ArgsT>
            int appendfmt(const char* fmt, ArgsT&&... args)
            {
                return m_sbuf.appendFormat(fmt, args...);
            }

            String* substr(size_t start, size_t maxlen)
            {
                char* str;
                String* rt;
                str = m_sbuf.substr(start, maxlen);
                rt = take(str, maxlen);
                return rt;
            }

            String* substring(size_t start, size_t end, bool likejs)
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
                raw = (char*)Memory::sysMalloc(sizeof(char) * asz);
                memset(raw, 0, asz);
                memcpy(raw, data() + start, len);
                return String::take(raw, len);
            }

            String* substr(size_t start)
            {
                return substr(start, length());
            }
    };

    class Upvalue : public Object
    {
        public:
            static Upvalue* make(Value* slot, int stackpos)
            {
                Upvalue* upvalue;
                upvalue = SharedState::gcMakeObject<Upvalue>(Object::OTYP_UPVALUE, false);
                upvalue->m_closed = Value::makeNull();
                upvalue->m_location = *slot;
                upvalue->m_next = nullptr;
                upvalue->m_stackpos = stackpos;
                return upvalue;
            }

        public:
            int m_stackpos;
            Value m_closed;
            Value m_location;
            Upvalue* m_next;

    };

    class Function : public Object
    {
        public:
            enum ContextType
            {
                CTXTYPE_ANONYMOUS,
                CTXTYPE_FUNCTION,
                CTXTYPE_METHOD,
                CTXTYPE_INITIALIZER,
                CTXTYPE_PRIVATE,
                CTXTYPE_STATIC,
                CTXTYPE_SCRIPT
            };

        public:
            static NEON_INLINE Function::ContextType getMethodType(Value method)
            {
                Function* ofn;
                ofn = method.asFunction();
                switch(method.objType())
                {
                    case Object::OTYP_FUNCNATIVE:
                        return ofn->m_contexttype;
                    case Object::OTYP_FUNCCLOSURE:
                        return ofn->m_fnvals.fnclosure.scriptfunc->m_contexttype;
                    default:
                        break;
                }
                return Function::CTXTYPE_FUNCTION;
            }

        public:
            ContextType m_contexttype;
            String* m_funcname;
            int m_upvalcount;
            Value m_clsthisval;
            union FuncUnion
            {
                struct FNDataClosure
                {
                    Function* scriptfunc;
                    Upvalue** m_upvalues;
                } fnclosure;
                struct FNDataScriptFunc
                {
                    int arity;
                    bool isvariadic;
                    Blob* blob;
                    Module* module;
                } fnscriptfunc;
                struct FNDataNativeFunc
                {
                    NativeFN natfunc;
                    void* userptr;
                } fnnativefunc;
                struct FNDataMethod
                {
                    Value receiver;
                    Function* method;
                } fnmethod;
            } m_fnvals;

        public:
            static Function* makeFuncGeneric(String* name, Object::Type fntype, Value thisval)
            {
                Function* ofn;
                ofn = SharedState::gcMakeObject<Function>(fntype, false);
                ofn->m_clsthisval = thisval;
                ofn->m_funcname = name;
                return ofn;
            }

            static Function* makeFuncBound(Value receiver, Function* method)
            {
                Function* ofn;
                ofn = makeFuncGeneric(method->m_funcname, Object::OTYP_FUNCBOUND, Value::makeNull());
                ofn->m_fnvals.fnmethod.receiver = receiver;
                ofn->m_fnvals.fnmethod.method = method;
                return ofn;
            }

            static Function* makeFuncScript(Module* module, int type)
            {
                Function* ofn;
                ofn = makeFuncGeneric(String::intern("<script>"), Object::OTYP_FUNCSCRIPT, Value::makeNull());
                ofn->m_fnvals.fnscriptfunc.arity = 0;
                ofn->m_upvalcount = 0;
                ofn->m_fnvals.fnscriptfunc.isvariadic = false;
                ofn->m_funcname = nullptr;
                ofn->m_contexttype = (Function::ContextType)type;
                ofn->m_fnvals.fnscriptfunc.module = module;
                ofn->m_fnvals.fnscriptfunc.blob = Memory::make<Blob>();
                return ofn;
            }

            static Function* makeFuncNative(NativeFN natfn, const char* name, void* uptr)
            {
                Function* ofn;
                ofn = makeFuncGeneric(String::copy(name), Object::OTYP_FUNCNATIVE, Value::makeNull());
                ofn->m_fnvals.fnnativefunc.natfunc = natfn;
                ofn->m_contexttype = Function::CTXTYPE_FUNCTION;
                ofn->m_fnvals.fnnativefunc.userptr = uptr;
                return ofn;
            }

            static Function* makeFuncClosure(Function* innerfn, Value thisval)
            {
                int i;
                Upvalue** upvals;
                Function* ofn;
                upvals = nullptr;
                if(innerfn->m_upvalcount > 0)
                {
                    upvals = (Upvalue**)SharedState::gcAllocate(sizeof(Upvalue*), innerfn->m_upvalcount + 1, false);
                    for(i = 0; i < innerfn->m_upvalcount; i++)
                    {
                        upvals[i] = nullptr;
                    }
                }
                ofn = makeFuncGeneric(innerfn->m_funcname, Object::OTYP_FUNCCLOSURE, thisval);
                ofn->m_fnvals.fnclosure.scriptfunc = innerfn;
                ofn->m_fnvals.fnclosure.m_upvalues = upvals;
                ofn->m_upvalcount = innerfn->m_upvalcount;
                return ofn;
            }

            static void destroy(Function* ofn)
            {
                Blob::destroy(ofn->m_fnvals.fnscriptfunc.blob);
                Memory::sysFree(ofn->m_fnvals.fnscriptfunc.blob);
            }

        public:

    };


    /**
     * TODO: use a different table implementation to avoid allocating so many strings...
     */
    class Class : public Object
    {
        public:
            class ConstItem
            {
                public:
                    const char* m_constclassmthname;
                    NativeFN fn;
            };

            static bool methodNameIsPrivate(String* name)
            {
                return name->length() > 0 && name->data()[0] == '_';
            }

            /*
             * TODO: this is somewhat basic instanceof checking;
             * it does not account for namespacing, which could spell issues in the future.
             * maybe classes should be given a distinct, unique, internal name, which would
             * incidentally remove the need to walk namespaces.
             * something like `class Foo{...}` -> 'Foo@<filename>:<lineno>', i.e., "Foo.class@path/to/somefile.nn:42".
             */
            static bool isInstanceOf(Class* klass1, Class* expected)
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

            /**
             * generate bytecode for a nativee exception class.
             * script-side it is enough to just derive from Exception, of course.
             */
            static Class* makeExceptionClass(Class* baseclass, Module* module, const char* cstrname, bool iscs)
            {
                int messageconst;
                Class* klass;
                String* classname;
                Function* function;
                Function* closure;
                if(iscs)
                {
                    classname = String::copy(cstrname);
                }
                else
                {
                    classname = String::copy(cstrname);
                }
                auto gcs = SharedState::get();
                gcs->vmStackPush(Value::fromObject(classname));
                klass = Class::make(classname, baseclass);
                gcs->vmStackPop();
                gcs->vmStackPush(Value::fromObject(klass));
                function = Function::makeFuncScript(module, Function::CTXTYPE_METHOD);
                function->m_fnvals.fnscriptfunc.arity = 1;
                function->m_fnvals.fnscriptfunc.isvariadic = false;
                gcs->vmStackPush(Value::fromObject(function));
                {
                    /* g_loc 0 */
                    function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(true, Instruction::OPC_LOCALGET, 0));
                    function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(false, (0 >> 8) & 0xff, 0));
                    function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(false, 0 & 0xff, 0));
                }
                {
                    /* g_loc 1 */
                    function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(true, Instruction::OPC_LOCALGET, 0));
                    function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(false, (1 >> 8) & 0xff, 0));
                    function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(false, 1 & 0xff, 0));
                }
                {
                    messageconst = function->m_fnvals.fnscriptfunc.blob->addConstant(Value::fromObject(String::intern("message")));
                    /* s_prop 0 */
                    function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(true, Instruction::OPC_PROPERTYSET, 0));
                    function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(false, (messageconst >> 8) & 0xff, 0));
                    function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(false, messageconst & 0xff, 0));
                }
                {
                    /* pop */
                    function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(true, Instruction::OPC_POPONE, 0));
                    function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(true, Instruction::OPC_POPONE, 0));
                }
                {
                    /* g_loc 0 */
                    /*
                    //  function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(true, Instruction::OPC_LOCALGET, 0));
                    //  function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(false, (0 >> 8) & 0xff, 0));
                    //  function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(false, 0 & 0xff, 0));
                    */
                }
                {
                    /* ret */
                    function->m_fnvals.fnscriptfunc.blob->push(Instruction::make(true, Instruction::OPC_RETURN, 0));
                }
                closure = Function::makeFuncClosure(function, Value::makeNull());
                gcs->vmStackPop();
                /* set class constructor */
                gcs->vmStackPush(Value::fromObject(closure));
                klass->m_instmethods.set(Value::fromObject(classname), Value::fromObject(closure));
                klass->m_constructor = Value::fromObject(closure);
                /* set class properties */
                klass->defProperty(String::intern("message"), Value::makeNull());
                klass->defProperty(String::intern("stacktrace"), Value::makeNull());
                klass->defProperty(String::intern("srcfile"), Value::makeNull());
                klass->defProperty(String::intern("srcline"), Value::makeNull());
                klass->defProperty(String::intern("class"), Value::fromObject(klass));
                gcs->m_declaredglobals.set(Value::fromObject(classname), Value::fromObject(klass));
                /* for class */
                gcs->vmStackPop();
                gcs->vmStackPop();
                /* assert error name */
                /* gcs->vmStackPop(); */
                return klass;
            }

        public:
            /*
             * the constructor, if any. defaults to <empty>, and if not <empty>, expects to be
             * some callable value.
             */
            Value m_constructor;
            Value m_destructor;

            /*
             * when declaring a class, $instproperties (their names, and initial values) are
             * copied to Instance::properties.
             * so `$instproperties["something"] = somefunction;` remains untouched *until* an
             * instance of this class is created.
             */
            HashTable<Value, Value> m_instproperties;

            /*
             * static, unchangeable(-ish) values. intended for values that are not unique, but shared
             * across classes, without being copied.
             */
            HashTable<Value, Value> m_staticproperties;

            /*
             * method table; they are currently not unique when instantiated; instead, they are
             * read from $methods as-is. this includes adding methods!
             * TODO: introduce a new hashtable field for Instance for unique methods, perhaps?
             * right now, this method just prevents unnecessary copying.
             */
            HashTable<Value, Value> m_instmethods;
            HashTable<Value, Value> m_staticmethods;
            String* m_classname;
            Class* m_superclass;

        public:
            static Class* make(String* name, Class* parent)
            {
                Class* klass;
                klass = SharedState::gcMakeObject<Class>(Object::OTYP_CLASS, false);
                klass->m_classname = name;
                klass->m_instproperties.initTable();
                klass->m_staticproperties.initTable();
                klass->m_instmethods.initTable();
                klass->m_staticmethods.initTable();
                klass->m_constructor = Value::makeNull();
                klass->m_destructor = Value::makeNull();
                klass->m_superclass = parent;
                return klass;
            }

            static void destroy(Class* klass)
            {
                auto gcs = SharedState::get();
                klass->m_instmethods.deInit();
                klass->m_staticmethods.deInit();
                klass->m_instproperties.deInit();
                klass->m_staticproperties.deInit();
                gcs->gcRelease(klass);
            }

            static Class* makeScriptClass(const char* name, Class* parent)
            {
                Class* cl;
                String* os;
                auto gcs = SharedState::get();
                os = String::copy(name);
                cl = Class::make(os, parent);
                gcs->m_declaredglobals.set(Value::fromObject(os), Value::fromObject(cl));
                return cl;
            }

            bool inheritFrom(Class* superclass)
            {
                int failcnt;
                failcnt = 0;
                if(!superclass->m_instproperties.copyTo(&m_instproperties, true))
                {
                    failcnt++;
                }
                if(!superclass->m_instmethods.copyTo(&m_instmethods, true))
                {
                    failcnt++;
                }
                m_superclass = superclass;
                if(failcnt == 0)
                {
                    return true;
                }
                return false;
            }

            bool defProperty(String* cstrname, Value val)
            {
                return m_instproperties.set(Value::fromObject(cstrname), val);
            }

            bool defCallableFieldPtr(String* name, NativeFN function, void* uptr)
            {
                Function* ofn;
                ofn = Function::makeFuncNative(function, name->data(), uptr);
                return m_instproperties.setwithtype(Value::fromObject(name), Value::fromObject(ofn), Property::FTYP_FUNCTION, true);
            }

            bool defCallableField(String* name, NativeFN function)
            {
                return defCallableFieldPtr(name, function, nullptr);
            }

            bool defStaticCallableFieldPtr(String* name, NativeFN function, void* uptr)
            {
                Function* ofn;
                ofn = Function::makeFuncNative(function, name->data(), uptr);
                return m_staticproperties.setwithtype(Value::fromObject(name), Value::fromObject(ofn), Property::FTYP_FUNCTION, true);
            }

            bool defStaticCallableField(String* name, NativeFN function)
            {
                return defStaticCallableFieldPtr(name, function, nullptr);
            }

            bool setStaticProperty(String* name, Value val)
            {
                return m_staticproperties.set(Value::fromObject(name), val);
            }

            bool defNativeConstructorPtr(NativeFN function, void* uptr)
            {
                const char* cname;
                Function* ofn;
                cname = "constructor";
                ofn = Function::makeFuncNative(function, cname, uptr);
                m_constructor = Value::fromObject(ofn);
                return true;
            }

            bool defNativeConstructor(NativeFN function)
            {
                return defNativeConstructorPtr(function, nullptr);
            }

            bool defMethod(String* name, Value val)
            {
                return m_instmethods.set(Value::fromObject(name), val);
            }

            bool defNativeMethodPtr(String* name, NativeFN function, void* ptr)
            {
                Function* ofn;
                ofn = Function::makeFuncNative(function, name->data(), ptr);
                return defMethod(name, Value::fromObject(ofn));
            }

            bool defNativeMethod(String* name, NativeFN function)
            {
                return defNativeMethodPtr(name, function, nullptr);
            }

            bool defStaticNativeMethodPtr(String* name, NativeFN function, void* uptr)
            {
                Function* ofn;
                ofn = Function::makeFuncNative(function, name->data(), uptr);
                return m_staticmethods.set(Value::fromObject(name), Value::fromObject(ofn));
            }

            bool defStaticNativeMethod(String* name, NativeFN function)
            {
                return defStaticNativeMethodPtr(name, function, nullptr);
            }

            Property* getMethodField(String* name)
            {
                Property* field;
                field = m_instmethods.getfield(Value::fromObject(name));
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
                field = m_instproperties.getfield(Value::fromObject(name));
            #if 0
                if(field == nullptr)
                {
                    if(m_superclass != nullptr)
                    {
                        return m_superclass->getPropertyField(name);
                    }
                }
            #endif
                return field;
            }

            Property* getStaticProperty(String* name)
            {
                Property* np;
                np = m_staticproperties.getfieldbyostr(name);
                if(np != nullptr)
                {
                    return np;
                }
                if(m_superclass != nullptr)
                {
                    return m_superclass->getStaticProperty(name);
                }
                return nullptr;
            }

            Property* getStaticMethodField(String* name)
            {
                Property* field;
                field = m_staticmethods.getfield(Value::fromObject(name));
                return field;
            }

            void installMethods(ConstItem* listmethods)
            {
                int i;
                const char* rawname;
                NativeFN rawfn;
                String* osname;
                for(i = 0; listmethods[i].m_constclassmthname != nullptr; i++)
                {
                    rawname = listmethods[i].m_constclassmthname;
                    rawfn = listmethods[i].fn;
                    osname = String::intern(rawname);
                    defNativeMethod(osname, rawfn);
                }
            }
    };

    class Instance : public Object
    {
        public:
            template<typename InputT>
            static Instance* makeInstanceOfSize(Class* klass)
            {
                Instance* oinst;
                Instance* instance;
                oinst = nullptr;
                instance = (Instance*)SharedState::gcMakeObject<InputT>(Object::OTYP_INSTANCE, false);
                instance->m_instactive = true;
                instance->m_instanceclass = klass;
                instance->m_instancesuperinstance = nullptr;
                instance->m_instanceprops.initTable();
                if(klass->m_instproperties.count() > 0)
                {
                    klass->m_instproperties.copy(&instance->m_instanceprops);
                }
                if(klass->m_superclass != nullptr)
                {
                    oinst = make(klass->m_superclass);
                    instance->m_instancesuperinstance = oinst;
                }
                return instance;
            }

            static Instance* make(Class* klass)
            {
                return makeInstanceOfSize<Instance>(klass);
            }

            static void mark(Instance* instance)
            {
                if(instance->m_instactive == false)
                {
                    // raiseWarning("trying to mark inactive instance <%p>!", instance);
                    return;
                }
                Value::markValTable(&instance->m_instanceprops);
                Object::markObject((Object*)instance->m_instanceclass);
            }

            static void destroy(Instance* instance)
            {
                auto gcs = SharedState::get();
                if(!instance->m_instanceclass->m_destructor.isNull())
                {
                    // if(!vmCallWithObject(instance->klass->m_destructor, Value::fromObject(instance), 0, false))
                    {
                    }
                }
                instance->m_instanceprops.deInit();
                instance->m_instactive = false;
                gcs->gcRelease(instance);
            }

        public:
            /*
             * whether this instance is still "active", i.e., not destroyed, deallocated, etc.
             */
            bool m_instactive;
            HashTable<Value, Value> m_instanceprops;
            Class* m_instanceclass;
            Instance* m_instancesuperinstance;

        public:
            bool defProperty(String* name, Value val)
            {
                return m_instanceprops.set(Value::fromObject(name), val);
            }

            Property* getProperty(String* name)
            {
                Property* field;
                field = m_instanceprops.getfield(Value::fromObject(name));
                if(field == nullptr)
                {
                    if(m_instancesuperinstance != nullptr)
                    {
                        return m_instancesuperinstance->getProperty(name);
                    }
                }
                return field;
            }

            Property* getMethod(String* name)
            {
                Property* field;
                field = m_instanceclass->getMethodField(name);
                if(field == nullptr)
                {
                    if(m_instancesuperinstance != nullptr)
                    {
                        return m_instancesuperinstance->getMethod(name);
                    }
                }
                return field;
            }
    };

    class File : public Object
    {
        public:
            static bool fileExists(const char* path)
            {
                return Util::nn_util_fsfileexists(path);
            }

            static bool pathIsFile(const char* path)
            {
                return Util::nn_util_fsfileisfile(path);
            }

            static bool pathIsDirectory(const char* path)
            {
                return Util::nn_util_fsfileisdirectory(path);
            }

            static char* getBasename(const char* path)
            {
                return Util::nn_util_fsgetbasename(path);
            }

            static char* readFromHandle(FILE* hnd, size_t* dlen, bool havemaxsz, size_t maxsize)
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
                if(havemaxsz)
                {
                    if(toldlen > maxsize)
                    {
                        toldlen = maxsize;
                    }
                }
                buf = (char*)Memory::sysMalloc(sizeof(char) * (toldlen + 1));
                memset(buf, 0, toldlen + 1);
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

            static char* readFile(const char* filename, size_t* dlen, bool havemaxsz, size_t maxsize)
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
                b = readFromHandle(fh, dlen, havemaxsz, maxsize);
                fclose(fh);
                return b;
            }

            static char* fileHandleGets(char* s, int size, FILE* f, size_t* lendest)
            {
                int c;
                char* p;
                p = s;
                (*lendest) = 0;
                if(size > 0)
                {
                    while(--size > 0)
                    {
                        if((c = getc(f)) == -1)
                        {
                            if(ferror(f) == EINTR)
                            {
                                continue;
                            }
                            break;
                        }
                        *p++ = c & 0xff;
                        (*lendest) += 1;
                        if(c == '\n')
                        {
                            break;
                        }
                    }
                    *p = '\0';
                }
                if(p > s)
                {
                    return s;
                }
                return nullptr;
            }

            static int readLineFromHandle(char** lineptr, size_t* destlen, FILE* hnd)
            {
                enum
                {
                    kInitialStrBufSize = 256
                };
                static char stackbuf[kInitialStrBufSize];
                char* heapbuf;
                size_t getlen;
                unsigned int linelen;
                getlen = 0;
                if(lineptr == nullptr || destlen == nullptr)
                {
                    errno = EINVAL;
                    return -1;
                }
                if(ferror(hnd))
                {
                    return -1;
                }
                if(feof(hnd))
                {
                    return -1;
                }
                fileHandleGets(stackbuf, kInitialStrBufSize, hnd, &getlen);
                heapbuf = strchr(stackbuf, '\n');
                if(heapbuf)
                {
                    *heapbuf = '\0';
                }
                linelen = getlen;
                if((linelen + 1) < kInitialStrBufSize)
                {
                    heapbuf = (char*)Memory::sysRealloc(*lineptr, kInitialStrBufSize);
                    if(heapbuf == nullptr)
                    {
                        return -1;
                    }
                    *lineptr = heapbuf;
                    *destlen = kInitialStrBufSize;
                }
                strcpy(*lineptr, stackbuf);
                *destlen = linelen;
                return linelen;
            }

        public:
            static File* make(FILE* handle, bool isstd, const char* path, const char* mode)
            {
                File* file;
                file = SharedState::gcMakeObject<File>(Object::OTYP_FILE, false);
                file->m_isopen = false;
                file->m_mode = String::copy(mode);
                file->m_path = String::copy(path);
                file->m_isstd = isstd;
                file->m_handle = handle;
                file->m_istty = false;
                file->m_number = -1;
                if(file->m_handle != nullptr)
                {
                    file->m_isopen = true;
                }
                return file;
            }

            static void destroy(File* file)
            {
                auto gcs = SharedState::get();
                file->closeFile();
                gcs->gcRelease(file);
            }

            static void mark(File* file)
            {
                Object::markObject((Object*)file->m_mode);
                Object::markObject((Object*)file->m_path);
            }

        public:
            bool m_isopen;
            bool m_isstd;
            bool m_istty;
            int m_number;
            FILE* m_handle;
            String* m_mode;
            String* m_path;

        public:
            bool readData(size_t readhowmuch, IOResult* dest)
            {
                size_t filesizereal;
                struct stat stats;
                filesizereal = -1;
                dest->success = false;
                dest->length = 0;
                dest->data = nullptr;
                if(!m_isstd)
                {
                    if(!File::fileExists(m_path->data()))
                    {
                        return false;
                    }
                    /* file is in write only mode */
                    /*
                    else if(strstr(m_mode->data(), "w") != nullptr && strstr(m_mode->data(), "+") == nullptr)
                    {
                        NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot read file in write mode");
                    }
                    */
                    if(!m_isopen)
                    {
                        /* open the file if it isn't open */
                        openWithoutParams();
                    }
                    else if(m_handle == nullptr)
                    {
                        return false;
                    }
                    if(Util::osfn_lstat(m_path->data(), &stats) == 0)
                    {
                        filesizereal = (size_t)stats.st_size;
                    }
                    else
                    {
                        /* fallback */
                        fseek(m_handle, 0L, SEEK_END);
                        filesizereal = ftell(m_handle);
                        rewind(m_handle);
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
                dest->data = (char*)Memory::sysMalloc(sizeof(char) * (readhowmuch + 1));
                if(dest->data == nullptr && readhowmuch != 0)
                {
                    return false;
                }
                dest->length = fread(dest->data, sizeof(char), readhowmuch, m_handle);
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

            int closeFile()
            {
                int result;
                if(m_handle != nullptr && !m_isstd)
                {
                    fflush(m_handle);
                    result = fclose(m_handle);
                    m_handle = nullptr;
                    m_isopen = false;
                    m_number = -1;
                    m_istty = false;
                    return result;
                }
                return -1;
            }

            bool openWithoutParams()
            {
                if(m_handle != nullptr)
                {
                    return true;
                }
                if(m_handle == nullptr && !m_isstd)
                {
                    m_handle = fopen(m_path->data(), m_mode->data());
                    if(m_handle != nullptr)
                    {
                        m_isopen = true;
                        m_number = fileno(m_handle);
                        m_istty = Util::osfn_isatty(m_number);
                        return true;
                    }
                    else
                    {
                        m_number = -1;
                        m_istty = false;
                    }
                    return false;
                }
                return false;
            }

    };

    class Module : public Object
    {
        public:
            using ModLoaderFN = void(*)();

        public:
            static Module* make(const char* name, const char* file, bool imported, bool retain)
            {
                Module* module;
                module = SharedState::gcMakeObject<Module>(Object::OTYP_MODULE, retain);
                module->m_deftable.initTable();
                module->m_modname = String::copy(name);
                module->m_physicalpath = String::copy(file);
                module->m_fnunloaderptr = nullptr;
                module->m_fnpreloaderptr = nullptr;
                module->m_handle = nullptr;
                module->m_imported = imported;
                return module;
            }

            static bool addSearchPathObj(String* os)
            {
                auto gcs = SharedState::get();
                gcs->m_importpath.push(Value::fromObject(os));
                return true;
            }

            static void closeImportHandle(void* hnd)
            {
                (void)hnd;
            }

            static bool addSearchPath(const char* path)
            {
                return addSearchPathObj(String::copy(path));
            }

            static void destroy(Module* module)
            {
                module->m_deftable.deInit();
                /*
                Memory::sysFree(module->m_modname);
                Memory::sysFree(module->m_physicalpath);
                */
                if(module->m_fnunloaderptr != nullptr && module->m_imported)
                {
                    auto asfn = module->m_fnunloaderptr;
                    asfn();
                }
                if(module->m_handle != nullptr)
                {
                    Module::closeImportHandle(module->m_handle);
                }
            }

            static StrBuffer* resolvePath(const char* modulename, const char* currentfile, char* rootfile, bool isrelative)
            {
                size_t i;
                size_t mlen;
                size_t splen;
                char* path1;
                char* path2;
                const char* cstrpath;
                struct stat stroot;
                struct stat stmod;
                String* pitem;
                StrBuffer* pathbuf;
                (void)rootfile;
                (void)isrelative;
                (void)stroot;
                (void)stmod;
                path1 = nullptr;
                path2 = nullptr;
                auto gcs = SharedState::get();
                mlen = strlen(modulename);
                splen = gcs->m_importpath.count();
                for(i = 0; i < splen; i++)
                {
                    auto pobj = gcs->m_importpath.get(i);
                    pitem = pobj.asString();
                    pathbuf = Memory::make<StrBuffer>();
                    pathbuf->append(pitem->data(), pitem->length());
                    if(pathbuf->contains('@'))
                    {
                        pathbuf->replace('@', modulename, mlen);
                    }
                    else
                    {
                        pathbuf->append("/");
                        pathbuf->append(modulename);
                        pathbuf->append(NEON_CONFIG_FILEEXT);
                    }
                    cstrpath = pathbuf->data();
                    fprintf(stderr, "import: trying '%s' ... ", cstrpath);
                    if(File::fileExists(cstrpath))
                    {
                        path1 = Util::osfn_realpath(cstrpath, nullptr);
                        fprintf(stderr, "found!\n");
                        /* stop a core library from importing itself */
                        if(currentfile != nullptr)
                        {
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
                            path2 = Util::osfn_realpath(currentfile, nullptr);
                            if(path1 != nullptr && path2 != nullptr)
                            {
                                if(memcmp(path1, path2, (int)strlen(path2)) == 0)
                                {
                                    Memory::sysFree(path1);
                                    Memory::sysFree(path2);
                                    path1 = nullptr;
                                    path2 = nullptr;
                                    fprintf(stderr, "resolvepath: refusing to import itself\n");
                                    return nullptr;
                                }
                            }
                        }
                        if(path2 != nullptr)
                        {
                            Memory::sysFree(path2);
                        }
                        if(path1 != nullptr)
                        {
                            Memory::sysFree(path1);
                        }
                        return pathbuf;
                    }
                    else
                    {
                        fprintf(stderr, "does not exist\n");
                    }
                    Memory::destroy(pathbuf);
                }
                return nullptr;
            }

            static Module* loadScriptModule(String* modulename)
            {
                size_t fsz;
                char* source;
                StrBuffer* physpath;
                Blob blob;
                Value retv;
                Value callable;
                Property* field;
                String* os;
                Module* module;
                Function* closure;
                Function* function;
                (void)os;
                auto gcs = SharedState::get();
                field = gcs->m_openedmodules.getfieldbyostr(modulename);
                if(field != nullptr)
                {
                    Blob::destroy(&blob);
                    return field->value.asModule();
                }
                physpath = resolvePath(modulename->data(), nullptr, nullptr, false);
                if(physpath == nullptr)
                {
                    Blob::destroy(&blob);
                    NEON_THROWCLASSWITHSOURCEINFO(gcs->m_exceptions.importerror, "module not found: '%s'", modulename->data());
                    return nullptr;
                }
                fprintf(stderr, "loading module from '%s'\n", physpath->data());
                source = neon::File::readFile(physpath->data(), &fsz, false, 0);
                if(source == nullptr)
                {
                    Memory::destroy(physpath);
                    NEON_THROWCLASSWITHSOURCEINFO(gcs->m_exceptions.importerror, "could not read import file %s", physpath);
                    Blob::destroy(&blob);
                    return nullptr;
                }
                module = Module::make(modulename->data(), physpath->data(), true, true);
                Memory::destroy(physpath);
                function = compileSourceIntern(module, source, &blob, false);
                Memory::sysFree(source);
                closure = Function::makeFuncClosure(function, Value::makeNull());
                callable = Value::fromObject(closure);
                gcs->vmNestCallPrepare(callable, Value::makeNull(), nullptr, 0);
                if(!gcs->vmNestCallFunction(callable, Value::makeNull(), nullptr, 0, &retv, false))
                {
                    Blob::destroy(&blob);
                    NEON_THROWCLASSWITHSOURCEINFO(gcs->m_exceptions.importerror, "failed to call compiled import closure");
                    return nullptr;
                }
                Blob::destroy(&blob);
                return module;
            }

        public:
            /* was this module imported? */
            bool m_imported;
            /* named exports */
            HashTable<Value, Value> m_deftable;
            /* the name of this module */
            String* m_modname;
            /* physsical location of this module, or nullptr if some other non-physical location */
            String* m_physicalpath;
            /* callback to call BEFORE this module is loaded */
            ModLoaderFN m_fnpreloaderptr;
            /* callbac to call AFTER this module is unloaded */
            ModLoaderFN m_fnunloaderptr;
            /* pointer that is based to preloader/unloader */
            void* m_handle;

        public:
            void setInternFileField()
            {
                return;
                m_deftable.set(Value::fromObject(String::intern("__file__")), Value::fromObject(String::copyObject(m_physicalpath)));
            }
    };

    class Range : public Object
    {
        public:
            static Range* make(int lower, int upper)
            {
                Range* range;
                range = SharedState::gcMakeObject<Range>(Object::OTYP_RANGE, false);
                range->m_lower = lower;
                range->m_upper = upper;
                if(upper > lower)
                {
                    range->m_range = upper - lower;
                }
                else
                {
                    range->m_range = lower - upper;
                }
                return range;
            }

        public:
            int m_lower;
            int m_upper;
            int m_range;
    };

    class Switch : public Object
    {
        public:
            static Switch* make()
            {
                Switch* sw;
                sw = SharedState::gcMakeObject<Switch>(Object::OTYP_SWITCH, false);
                sw->m_table.initTable();
                sw->m_defaultjump = -1;
                sw->m_exitjump = -1;
                return sw;
            }

        public:
            int m_defaultjump;
            int m_exitjump;
            HashTable<Value, Value> m_table;
    };

    class Userdata : public Object
    {
        public:
            using PtrFreeFN = void(*)(void*);

        public:
            static Userdata* make(void* pointer, const char* name)
            {
                Userdata* ptr;
                ptr = SharedState::gcMakeObject<Userdata>(Object::OTYP_USERDATA, false);
                ptr->m_pointer = pointer;
                ptr->m_udname = Util::stringDup(name);
                ptr->m_ondestroyfn = nullptr;
                return ptr;
            }

        public:
            void* m_pointer;
            char* m_udname;
            PtrFreeFN m_ondestroyfn;
    };

    class ValPrinter
    {
        public:
            ValPrinter(IOStream* pr) = delete;

            static void printValue(IOStream* pr, Value value, bool fixstring, bool invmethod)
            {
                if(value.isNull())
                {
                    pr->writeString("null");
                }
                else if(value.isBool())
                {
                    pr->writeString(value.asBool() ? "true" : "false");
                }
                else if(value.isNumber())
                {
                    printNumber(pr, value);
                }
                else
                {
                    printObject(pr, value, fixstring, invmethod);
                }
            }

            static void printObject(IOStream* pr, Value value, bool fixstring, bool invmethod)
            {
                Object* obj;
                obj = value.asObject();
                switch(obj->m_objtype)
                {
                    case Object::OTYP_INVALID:
                        {
                        }
                        break;
                    case Object::OTYP_SWITCH:
                        {
                            pr->writeString("<switch>");
                        }
                        break;
                    case Object::OTYP_USERDATA:
                        {
                            pr->format("<userdata %s>", value.asUserdata()->m_udname);
                        }
                        break;
                    case Object::OTYP_RANGE:
                        {
                            Range* range;
                            range = value.asRange();
                            pr->format("<range %d .. %d>", range->m_lower, range->m_upper);
                        }
                        break;
                    case Object::OTYP_FILE:
                        {
                            printFile(pr, value.asFile());
                        }
                        break;
                    case Object::OTYP_DICT:
                        {
                            printDict(pr, value.asDict());
                        }
                        break;
                    case Object::OTYP_ARRAY:
                        {
                            printArray(pr, value.asArray());
                        }
                        break;
                    case Object::OTYP_FUNCBOUND:
                        {
                            Function* bn;
                            bn = value.asFunction();
                            printFunction(pr, bn->m_fnvals.fnmethod.method->m_fnvals.fnclosure.scriptfunc);
                        }
                        break;
                    case Object::OTYP_MODULE:
                        {
                            Module* mod;
                            mod = value.asModule();
                            pr->format("<module '%s' at '%s'>", mod->m_modname->data(), mod->m_physicalpath->data());
                        }
                        break;
                    case Object::OTYP_CLASS:
                        {
                            printClass(pr, value, fixstring, invmethod);
                        }
                        break;
                    case Object::OTYP_FUNCCLOSURE:
                        {
                            Function* cls;
                            cls = value.asFunction();
                            printFunction(pr, cls->m_fnvals.fnclosure.scriptfunc);
                        }
                        break;
                    case Object::OTYP_FUNCSCRIPT:
                        {
                            Function* fn;
                            fn = value.asFunction();
                            printFunction(pr, fn);
                        }
                        break;
                    case Object::OTYP_INSTANCE:
                        {
                            /* @TODO: support the toString() override */
                            Instance* instance;
                            instance = value.asInstance();
                            printInstance(pr, instance, invmethod);
                        }
                        break;
                    case Object::OTYP_FUNCNATIVE:
                        {
                            Function* native;
                            native = value.asFunction();
                            pr->format("<function %s(native) at %p>", native->m_funcname->data(), (void*)native);
                        }
                        break;
                    case Object::OTYP_UPVALUE:
                        {
                            pr->format("<upvalue>");
                        }
                        break;
                    case Object::OTYP_STRING:
                        {
                            String* string;
                            string = value.asString();
                            if(fixstring)
                            {
                                pr->writeQuotedString(string->data(), string->length(), true);
                            }
                            else
                            {
                                pr->writeString(string->data(), string->length());
                            }
                        }
                        break;
                }
            }

            static void printNumber(IOStream* pr, Value value)
            {
                double dn;
                dn = value.asNumber();
                pr->format("%.16g", dn);
            }

            static void printFunction(IOStream* pr, Function* func)
            {
                if(func->m_funcname == nullptr)
                {
                    pr->format("<script at %p>", (void*)func);
                }
                else
                {
                    if(func->m_fnvals.fnscriptfunc.isvariadic)
                    {
                        pr->format("<function %s(%d...) at %p>", func->m_funcname->data(), func->m_fnvals.fnscriptfunc.arity, (void*)func);
                    }
                    else
                    {
                        pr->format("<function %s(%d) at %p>", func->m_funcname->data(), func->m_fnvals.fnscriptfunc.arity, (void*)func);
                    }
                }
            }

            static void printArray(IOStream* pr, Array* list)
            {
                size_t i;
                size_t vsz;
                bool isrecur;
                Value val;
                Array* subarr;
                vsz = list->count();
                pr->format("[");
                for(i = 0; i < vsz; i++)
                {
                    isrecur = false;
                    val = list->get(i);
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
                        pr->format("<recursion>");
                    }
                    else
                    {
                        printValue(pr, val, true, true);
                    }
                    if(i != vsz - 1)
                    {
                        pr->format(",");
                    }
                    if(pr->m_shortenvalues && (i >= pr->m_maxvallength))
                    {
                        pr->format(" [%zd items]", vsz);
                        break;
                    }
                }
                pr->format("]");
            }

            static void printDict(IOStream* pr, Dict* dict)
            {
                size_t i;
                size_t dsz;
                bool keyisrecur;
                bool valisrecur;
                Value val;
                Dict* subdict;
                dsz = dict->m_htkeys.count();
                pr->format("{");
                for(i = 0; i < dsz; i++)
                {
                    valisrecur = false;
                    keyisrecur = false;
                    val = dict->m_htkeys.get(i);
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
                        pr->format("<recursion>");
                    }
                    else
                    {
                        printValue(pr, val, true, true);
                    }
                    pr->format(": ");
                    auto field = dict->m_htvalues.getfield(dict->m_htkeys.get(i));
                    if(field != nullptr)
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
                            pr->format("<recursion>");
                        }
                        else
                        {
                            printValue(pr, field->value, true, true);
                        }
                    }
                    if(i != dsz - 1)
                    {
                        pr->format(", ");
                    }
                    if(pr->m_shortenvalues && (pr->m_maxvallength >= i))
                    {
                        pr->format(" [%zd items]", dsz);
                        break;
                    }
                }
                pr->format("}");
            }

            static void printFile(IOStream* pr, File* file)
            {
                pr->format("<file at %s in mode %s>", file->m_path->data(), file->m_mode->data());
            }

            static void printInstance(IOStream* pr, Instance* instance, bool invmethod)
            {
                (void)invmethod;
                pr->format("<instance of %s at %p>", instance->m_instanceclass->m_classname->data(), (void*)instance);
            }

            static void printTable(IOStream* pr, HashTable<Value, Value>* table)
            {
                size_t i;
                size_t hcap;
                hcap = table->capacity();
                pr->format("{");
                for(i = 0; i < (size_t)hcap; i++)
                {
                    auto entry = table->entryatindex(i);
                    if(entry->key.isNull())
                    {
                        continue;
                    }
                    printValue(pr, entry->key, true, false);
                    pr->format(":");
                    printValue(pr, entry->value.value, true, false);
                    auto nextentry = table->entryatindex(i + 1);
                    if((nextentry != nullptr) && !nextentry->key.isNull())
                    {
                        if((i + 1) < (size_t)hcap)
                        {
                            pr->format(",");
                        }
                    }
                }
                pr->format("}");
            }

            static void printClass(IOStream* pr, Value value, bool fixstring, bool invmethod)
            {
                bool oldexp;
                Class* klass;
                (void)fixstring;
                (void)invmethod;
                klass = value.asClass();
                if(pr->m_jsonmode)
                {
                    pr->format("{");
                    {
                        {
                            pr->format("name: ");
                            ValPrinter::printValue(pr, Value::fromObject(klass->m_classname), true, false);
                            pr->format(",");
                        }
                        {
                            pr->format("superclass: ");
                            oldexp = pr->m_jsonmode;
                            pr->m_jsonmode = false;
                            printValue(pr, Value::fromObject(klass->m_superclass), true, false);
                            pr->m_jsonmode = oldexp;
                            pr->format(",");
                        }
                        {
                            pr->format("constructor: ");
                            printValue(pr, klass->m_constructor, true, false);
                            pr->format(",");
                        }
                        {
                            pr->format("instanceproperties:");
                            printTable(pr, &klass->m_instproperties);
                            pr->format(",");
                        }
                        {
                            pr->format("staticproperties:");
                            printTable(pr, &klass->m_staticproperties);
                            pr->format(",");
                        }
                        {
                            pr->format("instancemethods:");
                            printTable(pr, &klass->m_instmethods);
                            pr->format(",");
                        }
                        {
                            pr->format("staticmethods:");
                            printTable(pr, &klass->m_staticmethods);
                        }
                    }
                    pr->format("}");
                }
                else
                {
                    pr->format("<class %s at %p>", klass->m_classname->data(), (void*)klass);
                }
            }
    };

    class FormatInfo
    {
        public:
            /* length of the format string */
            size_t m_fmtlen;
            /* the actual format string */
            const char* m_fmtstr;
            IOStream* m_writer;

        public:
            FormatInfo(IOStream* writer, const char* fmtstr, size_t fmtlen)
            {
                m_fmtstr = fmtstr;
                m_fmtlen = fmtlen;
                m_writer = writer;
            }

            bool formatWithArgs(size_t argc, size_t argbegin, Value* argv)
            {
                int ch;
                int ival;
                int nextch;
                bool ok;
                size_t i;
                size_t argpos;
                Value cval;
                i = 0;
                auto gcs = SharedState::get();
                argpos = argbegin;
                ok = true;
                while(i < m_fmtlen)
                {
                    ch = m_fmtstr[i];
                    nextch = -1;
                    if((i + 1) < m_fmtlen)
                    {
                        nextch = m_fmtstr[i + 1];
                    }
                    i++;
                    if(ch == '%')
                    {
                        if(nextch == '%')
                        {
                            m_writer->writeChar('%');
                        }
                        else
                        {
                            i++;
                            if(argpos > argc)
                            {
                                NEON_THROWCLASSWITHSOURCEINFO(gcs->m_exceptions.argumenterror, "too few arguments");
                                ok = false;
                                cval = Value::makeNull();
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
                                    ValPrinter::printValue(m_writer, cval, true, true);
                                }
                                break;
                                case 'c':
                                {
                                    ival = (int)cval.asNumber();
                                    m_writer->format("%c", ival);
                                }
                                break;
                                /* TODO: implement actual field formatting */
                                case 's':
                                case 'd':
                                case 'i':
                                case 'g':
                                {
                                    ValPrinter::printValue(m_writer, cval, false, true);
                                }
                                break;
                                default:
                                {
                                    NEON_THROWCLASSWITHSOURCEINFO(gcs->m_exceptions.argumenterror, "unknown/invalid format flag '%%c'", nextch);
                                }
                                break;
                            }
                        }
                    }
                    else
                    {
                        m_writer->writeChar(ch);
                    }
                }
                return ok;
            }
    };

    class AstToken
    {
        public:
            enum Type
            {
                T_NEWLINE,
                T_PARENOPEN,
                T_PARENCLOSE,
                T_BRACKETOPEN,
                T_BRACKETCLOSE,
                T_BRACEOPEN,
                T_BRACECLOSE,
                T_SEMICOLON,
                T_COMMA,
                T_BACKSLASH,
                T_EXCLMARK,
                T_NOTEQUAL,
                T_COLON,
                T_AT,
                T_DOT,
                T_DOUBLEDOT,
                T_TRIPLEDOT,
                T_PLUS,
                T_PLUSASSIGN,
                T_INCREMENT,
                T_MINUS,
                T_MINUSASSIGN,
                T_DECREMENT,
                T_MULTIPLY,
                T_MULTASSIGN,
                T_POWEROF,
                T_POWASSIGN,
                T_DIVIDE,
                T_DIVASSIGN,
                T_ASSIGN,
                T_EQUAL,
                T_LESSTHAN,
                T_LESSEQUAL,
                T_LEFTSHIFT,
                T_LEFTSHIFTASSIGN,
                T_GREATERTHAN,
                T_GREATER_EQ,
                T_RIGHTSHIFT,
                T_RIGHTSHIFTASSIGN,
                T_MODULO,
                T_PERCENT_EQ,
                T_AMP,
                T_AMP_EQ,
                T_BAR,
                T_BAR_EQ,
                T_TILDE,
                T_TILDE_EQ,
                T_XOR,
                T_XOR_EQ,
                T_QUESTION,
                T_KWAND,
                T_KWAS,
                T_KWASSERT,
                T_KWBREAK,
                T_KWCATCH,
                T_KWCLASS,
                T_KWCONTINUE,
                T_KWFUNCTION,
                T_KWDEFAULT,
                T_KWTHROW,
                T_KWDO,
                T_KWECHO,
                T_KWELSE,
                T_KWFALSE,
                T_KWFINALLY,
                T_KWFOREACH,
                T_KWIF,
                T_KWIN,
                T_KWINSTANCEOF,
                T_KWFOR,
                T_KWNULL,
                T_KWNEW,
                T_KWOR,
                T_KWSUPER,
                T_KWRETURN,
                T_KWTHIS,
                T_KWSTATIC,
                T_KWTRUE,
                T_KWTRY,
                T_KWTYPEOF,
                T_KWSWITCH,
                T_KWVAR,
                T_KWCONST,
                T_KWCASE,
                T_KWWHILE,
                T_KWEXTENDS,
                T_LITERALSTRING,
                T_LITERALRAWSTRING,
                T_LITNUMREG,
                T_LITNUMBIN,
                T_LITNUMOCT,
                T_LITNUMHEX,
                T_IDENTNORMAL,
                T_DECORATOR,
                T_INTERPOLATION,
                T_EOF,
                T_ERROR,
                T_KWEMPTY,
                T_UNDEFINED,
                T_TOKCOUNT
            };

        public:
            bool isglobal;
            Type m_toktype;
            const char* m_start;
            int length;
            int m_line;
    };

    class AstLexer
    {
        public:
            enum
            {
                /* how deep template strings can be nested (i.e., "foo${getBar("quux${getBonk("...")}")}") */
                MaxStrTplDepth = 8,
            };

        public:
            static bool lexUtilIsDigit(char c)
            {
                return c >= '0' && c <= '9';
            }

            static bool lexUtilIsBinary(char c)
            {
                return c == '0' || c == '1';
            }

            static bool lexUtilIsAlpha(char c)
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
            }

            static bool lexUtilIsOctal(char c)
            {
                return c >= '0' && c <= '7';
            }

            static bool lexUtilIsHexadecimal(char c)
            {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            }

            static const char* tokTypeToString(int t)
            {
                switch(t)
                {
                    case AstToken::T_NEWLINE:
                        return "AstToken::T_NEWLINE";
                    case AstToken::T_PARENOPEN:
                        return "AstToken::T_PARENOPEN";
                    case AstToken::T_PARENCLOSE:
                        return "AstToken::T_PARENCLOSE";
                    case AstToken::T_BRACKETOPEN:
                        return "AstToken::T_BRACKETOPEN";
                    case AstToken::T_BRACKETCLOSE:
                        return "AstToken::T_BRACKETCLOSE";
                    case AstToken::T_BRACEOPEN:
                        return "AstToken::T_BRACEOPEN";
                    case AstToken::T_BRACECLOSE:
                        return "AstToken::T_BRACECLOSE";
                    case AstToken::T_SEMICOLON:
                        return "AstToken::T_SEMICOLON";
                    case AstToken::T_COMMA:
                        return "AstToken::T_COMMA";
                    case AstToken::T_BACKSLASH:
                        return "AstToken::T_BACKSLASH";
                    case AstToken::T_EXCLMARK:
                        return "AstToken::T_EXCLMARK";
                    case AstToken::T_NOTEQUAL:
                        return "AstToken::T_NOTEQUAL";
                    case AstToken::T_COLON:
                        return "AstToken::T_COLON";
                    case AstToken::T_AT:
                        return "AstToken::T_AT";
                    case AstToken::T_DOT:
                        return "AstToken::T_DOT";
                    case AstToken::T_DOUBLEDOT:
                        return "AstToken::T_DOUBLEDOT";
                    case AstToken::T_TRIPLEDOT:
                        return "AstToken::T_TRIPLEDOT";
                    case AstToken::T_PLUS:
                        return "AstToken::T_PLUS";
                    case AstToken::T_PLUSASSIGN:
                        return "AstToken::T_PLUSASSIGN";
                    case AstToken::T_INCREMENT:
                        return "AstToken::T_INCREMENT";
                    case AstToken::T_MINUS:
                        return "AstToken::T_MINUS";
                    case AstToken::T_MINUSASSIGN:
                        return "AstToken::T_MINUSASSIGN";
                    case AstToken::T_DECREMENT:
                        return "AstToken::T_DECREMENT";
                    case AstToken::T_MULTIPLY:
                        return "AstToken::T_MULTIPLY";
                    case AstToken::T_MULTASSIGN:
                        return "AstToken::T_MULTASSIGN";
                    case AstToken::T_POWEROF:
                        return "AstToken::T_POWEROF";
                    case AstToken::T_POWASSIGN:
                        return "AstToken::T_POWASSIGN";
                    case AstToken::T_DIVIDE:
                        return "AstToken::T_DIVIDE";
                    case AstToken::T_DIVASSIGN:
                        return "AstToken::T_DIVASSIGN";
                    case AstToken::T_ASSIGN:
                        return "AstToken::T_ASSIGN";
                    case AstToken::T_EQUAL:
                        return "AstToken::T_EQUAL";
                    case AstToken::T_LESSTHAN:
                        return "AstToken::T_LESSTHAN";
                    case AstToken::T_LESSEQUAL:
                        return "AstToken::T_LESSEQUAL";
                    case AstToken::T_LEFTSHIFT:
                        return "AstToken::T_LEFTSHIFT";
                    case AstToken::T_LEFTSHIFTASSIGN:
                        return "AstToken::T_LEFTSHIFTASSIGN";
                    case AstToken::T_GREATERTHAN:
                        return "AstToken::T_GREATERTHAN";
                    case AstToken::T_GREATER_EQ:
                        return "AstToken::T_GREATER_EQ";
                    case AstToken::T_RIGHTSHIFT:
                        return "AstToken::T_RIGHTSHIFT";
                    case AstToken::T_RIGHTSHIFTASSIGN:
                        return "AstToken::T_RIGHTSHIFTASSIGN";
                    case AstToken::T_MODULO:
                        return "AstToken::T_MODULO";
                    case AstToken::T_PERCENT_EQ:
                        return "AstToken::T_PERCENT_EQ";
                    case AstToken::T_AMP:
                        return "AstToken::T_AMP";
                    case AstToken::T_AMP_EQ:
                        return "AstToken::T_AMP_EQ";
                    case AstToken::T_BAR:
                        return "AstToken::T_BAR";
                    case AstToken::T_BAR_EQ:
                        return "AstToken::T_BAR_EQ";
                    case AstToken::T_TILDE:
                        return "AstToken::T_TILDE";
                    case AstToken::T_TILDE_EQ:
                        return "AstToken::T_TILDE_EQ";
                    case AstToken::T_XOR:
                        return "AstToken::T_XOR";
                    case AstToken::T_XOR_EQ:
                        return "AstToken::T_XOR_EQ";
                    case AstToken::T_QUESTION:
                        return "AstToken::T_QUESTION";
                    case AstToken::T_KWAND:
                        return "AstToken::T_KWAND";
                    case AstToken::T_KWAS:
                        return "AstToken::T_KWAS";
                    case AstToken::T_KWASSERT:
                        return "AstToken::T_KWASSERT";
                    case AstToken::T_KWBREAK:
                        return "AstToken::T_KWBREAK";
                    case AstToken::T_KWCATCH:
                        return "AstToken::T_KWCATCH";
                    case AstToken::T_KWCLASS:
                        return "AstToken::T_KWCLASS";
                    case AstToken::T_KWCONTINUE:
                        return "AstToken::T_KWCONTINUE";
                    case AstToken::T_KWFUNCTION:
                        return "AstToken::T_KWFUNCTION";
                    case AstToken::T_KWDEFAULT:
                        return "AstToken::T_KWDEFAULT";
                    case AstToken::T_KWTHROW:
                        return "AstToken::T_KWTHROW";
                    case AstToken::T_KWDO:
                        return "AstToken::T_KWDO";
                    case AstToken::T_KWECHO:
                        return "AstToken::T_KWECHO";
                    case AstToken::T_KWELSE:
                        return "AstToken::T_KWELSE";
                    case AstToken::T_KWFALSE:
                        return "AstToken::T_KWFALSE";
                    case AstToken::T_KWFINALLY:
                        return "AstToken::T_KWFINALLY";
                    case AstToken::T_KWFOREACH:
                        return "AstToken::T_KWFOREACH";
                    case AstToken::T_KWIF:
                        return "AstToken::T_KWIF";
                    case AstToken::T_KWIN:
                        return "AstToken::T_KWIN";
                    case AstToken::T_KWFOR:
                        return "AstToken::T_KWFOR";
                    case AstToken::T_KWNULL:
                        return "AstToken::T_KWNULL";
                    case AstToken::T_KWNEW:
                        return "AstToken::T_KWNEW";
                    case AstToken::T_KWOR:
                        return "AstToken::T_KWOR";
                    case AstToken::T_KWSUPER:
                        return "AstToken::T_KWSUPER";
                    case AstToken::T_KWRETURN:
                        return "AstToken::T_KWRETURN";
                    case AstToken::T_KWTHIS:
                        return "AstToken::T_KWTHIS";
                    case AstToken::T_KWSTATIC:
                        return "AstToken::T_KWSTATIC";
                    case AstToken::T_KWTRUE:
                        return "AstToken::T_KWTRUE";
                    case AstToken::T_KWTRY:
                        return "AstToken::T_KWTRY";
                    case AstToken::T_KWSWITCH:
                        return "AstToken::T_KWSWITCH";
                    case AstToken::T_KWVAR:
                        return "AstToken::T_KWVAR";
                    case AstToken::T_KWCONST:
                        return "AstToken::T_KWCONST";
                    case AstToken::T_KWCASE:
                        return "AstToken::T_KWCASE";
                    case AstToken::T_KWWHILE:
                        return "AstToken::T_KWWHILE";
                    case AstToken::T_KWINSTANCEOF:
                        return "AstToken::T_KWINSTANCEOF";
                    case AstToken::T_KWEXTENDS:
                        return "AstToken::T_KWEXTENDS";
                    case AstToken::T_LITERALSTRING:
                        return "AstToken::T_LITERALSTRING";
                    case AstToken::T_LITERALRAWSTRING:
                        return "AstToken::T_LITERALRAWSTRING";
                    case AstToken::T_LITNUMREG:
                        return "AstToken::T_LITNUMREG";
                    case AstToken::T_LITNUMBIN:
                        return "AstToken::T_LITNUMBIN";
                    case AstToken::T_LITNUMOCT:
                        return "AstToken::T_LITNUMOCT";
                    case AstToken::T_LITNUMHEX:
                        return "AstToken::T_LITNUMHEX";
                    case AstToken::T_IDENTNORMAL:
                        return "AstToken::T_IDENTNORMAL";
                    case AstToken::T_DECORATOR:
                        return "AstToken::T_DECORATOR";
                    case AstToken::T_INTERPOLATION:
                        return "AstToken::T_INTERPOLATION";
                    case AstToken::T_EOF:
                        return "AstToken::T_EOF";
                    case AstToken::T_ERROR:
                        return "AstToken::T_ERROR";
                    case AstToken::T_KWEMPTY:
                        return "AstToken::T_KWEMPTY";
                    case AstToken::T_UNDEFINED:
                        return "AstToken::T_UNDEFINED";
                    case AstToken::T_TOKCOUNT:
                        return "AstToken::T_TOKCOUNT";
                }
                return "?invalid?";
            }

            static AstLexer* make(const char* source)
            {
                AstLexer* lex;
                lex = Memory::make<AstLexer>();
                lex->init(source);
                lex->m_onstack = false;
                return lex;
            }

            static void destroy(AstLexer* lex)
            {
                if(!lex->m_onstack)
                {
                    Memory::sysFree(lex);
                }
            }

        public:
            bool m_onstack;
            const char* m_start;
            const char* m_sourceptr;
            int m_line;
            int m_tplstringcount;
            int m_tplstringbuffer[MaxStrTplDepth];

        public:
            /*
             * allows for the lexer to created on the stack.
             */
            void init(const char* source)
            {
                m_sourceptr = source;
                m_start = source;
                m_sourceptr = m_start;
                m_line = 1;
                m_tplstringcount = -1;
                m_onstack = true;
            }

            bool isatend()
            {
                return *m_sourceptr == '\0';
            }

            AstToken createtoken(AstToken::Type type)
            {
                AstToken t;
                t.isglobal = false;
                t.m_toktype = type;
                t.m_start = m_start;
                t.length = (int)(m_sourceptr - m_start);
                t.m_line = m_line;
                return t;
            }

            template<typename... ArgsT>
            AstToken errortoken(const char* fmt, ArgsT&&... args)
            {
                constexpr static auto tmpsprintf = sprintf;
                int length;
                char* buf;
                AstToken t;
                buf = (char*)Memory::sysMalloc(sizeof(char) * 1024);
                /* TODO: used to be vasprintf. need to check how much to actually allocate! */
                length = tmpsprintf(buf, fmt, args...);
                t.m_toktype = AstToken::T_ERROR;
                t.m_start = buf;
                t.isglobal = false;
                if(buf != nullptr)
                {
                    t.length = length;
                }
                else
                {
                    t.length = 0;
                }
                t.m_line = m_line;
                return t;
            }

            char advance()
            {
                m_sourceptr++;
                if(m_sourceptr[-1] == '\n')
                {
                    m_line++;
                }
                return m_sourceptr[-1];
            }

            bool match(char expected)
            {
                if(isatend())
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
                    m_line++;
                }
                return true;
            }

            char peekcurr()
            {
                return *m_sourceptr;
            }

            char peekprev()
            {
                if(m_sourceptr == m_start)
                {
                    return -1;
                }
                return m_sourceptr[-1];
            }

            char peeknext()
            {
                if(isatend())
                {
                    return '\0';
                }
                return m_sourceptr[1];
            }

            AstToken skipblockcomments()
            {
                int nesting;
                nesting = 1;
                while(nesting > 0)
                {
                    if(isatend())
                    {
                        return errortoken("unclosed block comment");
                    }
                    /* internal comment open */
                    if(peekcurr() == '/' && peeknext() == '*')
                    {
                        advance();
                        advance();
                        nesting++;
                        continue;
                    }
                    /* comment close */
                    if(peekcurr() == '*' && peeknext() == '/')
                    {
                        advance();
                        advance();
                        nesting--;
                        continue;
                    }
                    /* regular comment body */
                    advance();
                }
                return createtoken(AstToken::T_UNDEFINED);
            }

            AstToken skipspace()
            {
                char c;
                AstToken result;
                result.isglobal = false;
                for(;;)
                {
                    c = peekcurr();
                    switch(c)
                    {
                        case ' ':
                        case '\r':
                        case '\t':
                            {
                                advance();
                            }
                            break;
                        case '/':
                            {
                                if(peeknext() == '/')
                                {
                                    while(peekcurr() != '\n' && !isatend())
                                    {
                                        advance();
                                    }
                                    return createtoken(AstToken::T_UNDEFINED);
                                }
                                else if(peeknext() == '*')
                                {
                                    advance();
                                    advance();
                                    result = skipblockcomments();
                                    if(result.m_toktype != AstToken::T_UNDEFINED)
                                    {
                                        return result;
                                    }
                                    break;
                                }
                                else
                                {
                                    return createtoken(AstToken::T_UNDEFINED);
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
                return createtoken(AstToken::T_UNDEFINED);
            }

            AstToken scanstring(char quote, bool withtemplate, bool permitescapes)
            {
                AstToken tkn;
                while(peekcurr() != quote && !isatend())
                {
                    if(withtemplate)
                    {
                        /* interpolation started */
                        if(peekcurr() == '$' && peeknext() == '{' && peekprev() != '\\')
                        {
                            if(m_tplstringcount - 1 < MaxStrTplDepth)
                            {
                                m_tplstringcount++;
                                m_tplstringbuffer[m_tplstringcount] = (int)quote;
                                m_sourceptr++;
                                tkn = createtoken(AstToken::T_INTERPOLATION);
                                m_sourceptr++;
                                return tkn;
                            }
                            return errortoken("maximum interpolation nesting of %d exceeded by %d", MaxStrTplDepth, MaxStrTplDepth - m_tplstringcount + 1);
                        }
                    }
                    if(peekcurr() == '\\' && (peeknext() == quote || peeknext() == '\\'))
                    {
                        advance();
                    }
                    advance();
                }
                if(isatend())
                {
                    return errortoken("unterminated string (opening quote not matched)");
                }
                /* the closing quote */
                match(quote);
                if(permitescapes)
                {
                    return createtoken(AstToken::T_LITERALSTRING);
                }
                return createtoken(AstToken::T_LITERALRAWSTRING);
            }

            AstToken scannumber()
            {
                /* handle binary, octal and hexadecimals */
                if(peekprev() == '0')
                {
                    /* binary number */
                    if(match('b'))
                    {
                        while(lexUtilIsBinary(peekcurr()))
                        {
                            advance();
                        }
                        return createtoken(AstToken::T_LITNUMBIN);
                    }
                    else if(match('c'))
                    {
                        while(lexUtilIsOctal(peekcurr()))
                        {
                            advance();
                        }
                        return createtoken(AstToken::T_LITNUMOCT);
                    }
                    else if(match('x'))
                    {
                        while(lexUtilIsHexadecimal(peekcurr()))
                        {
                            advance();
                        }
                        return createtoken(AstToken::T_LITNUMHEX);
                    }
                }
                while(lexUtilIsDigit(peekcurr()))
                {
                    advance();
                }
                /* dots(.) are only valid here when followed by a digit */
                if(peekcurr() == '.' && lexUtilIsDigit(peeknext()))
                {
                    advance();
                    while(lexUtilIsDigit(peekcurr()))
                    {
                        advance();
                    }
                    /*
                    // E or e are only valid here when followed by a digit and occurring after a dot
                    */
                    if((peekcurr() == 'e' || peekcurr() == 'E') && (peeknext() == '+' || peeknext() == '-'))
                    {
                        advance();
                        advance();
                        while(lexUtilIsDigit(peekcurr()))
                        {
                            advance();
                        }
                    }
                }
                return createtoken(AstToken::T_LITNUMREG);
            }

            AstToken::Type getidenttype()
            {
                /* clang-format off */
                static const struct
                {
                    const char* str;
                    int tokid;
                } keywords[] = {
                    { "and", AstToken::T_KWAND },
                    { "assert", AstToken::T_KWASSERT },
                    { "as", AstToken::T_KWAS },
                    { "break", AstToken::T_KWBREAK },
                    { "catch", AstToken::T_KWCATCH },
                    { "class", AstToken::T_KWCLASS },
                    { "continue", AstToken::T_KWCONTINUE },
                    { "default", AstToken::T_KWDEFAULT },
                    { "def", AstToken::T_KWFUNCTION },
                    { "function", AstToken::T_KWFUNCTION },
                    { "throw", AstToken::T_KWTHROW },
                    { "do", AstToken::T_KWDO },
                    { "echo", AstToken::T_KWECHO },
                    { "else", AstToken::T_KWELSE },
                    { "empty", AstToken::T_KWEMPTY },
                    { "extends", AstToken::T_KWEXTENDS },
                    { "false", AstToken::T_KWFALSE },
                    { "finally", AstToken::T_KWFINALLY },
                    { "foreach", AstToken::T_KWFOREACH },
                    { "if", AstToken::T_KWIF },
                    { "in", AstToken::T_KWIN },
                    { "instanceof", AstToken::T_KWINSTANCEOF },
                    { "for", AstToken::T_KWFOR },
                    { "null", AstToken::T_KWNULL },
                    { "new", AstToken::T_KWNEW },
                    { "or", AstToken::T_KWOR },
                    { "super", AstToken::T_KWSUPER },
                    { "return", AstToken::T_KWRETURN },
                    { "this", AstToken::T_KWTHIS },
                    { "static", AstToken::T_KWSTATIC },
                    { "true", AstToken::T_KWTRUE },
                    { "try", AstToken::T_KWTRY },
                    { "typeof", AstToken::T_KWTYPEOF },
                    { "switch", AstToken::T_KWSWITCH },
                    { "case", AstToken::T_KWCASE },
                    { "var", AstToken::T_KWVAR },
                    { "let", AstToken::T_KWVAR },
                    { "const", AstToken::T_KWCONST },
                    { "while", AstToken::T_KWWHILE },
                    { nullptr, (AstToken::Type)0 }
                };
                /* clang-format on */
                size_t i;
                size_t kwlen;
                size_t ofs;
                const char* kwtext;
                for(i = 0; keywords[i].str != nullptr; i++)
                {
                    kwtext = keywords[i].str;
                    kwlen = strlen(kwtext);
                    ofs = (m_sourceptr - m_start);
                    if(ofs == kwlen)
                    {
                        if(memcmp(m_start, kwtext, kwlen) == 0)
                        {
                            return (AstToken::Type)keywords[i].tokid;
                        }
                    }
                }
                return AstToken::T_IDENTNORMAL;
            }

            AstToken scanident(bool isdollar)
            {
                int cur;
                AstToken tok;
                cur = peekcurr();
                if(cur == '$')
                {
                    advance();
                }
                while(true)
                {
                    cur = peekcurr();
                    if(lexUtilIsAlpha(cur) || lexUtilIsDigit(cur))
                    {
                        advance();
                    }
                    else
                    {
                        break;
                    }
                }
                tok = createtoken(getidenttype());
                tok.isglobal = isdollar;
                return tok;
            }

            AstToken scandecorator()
            {
                while(lexUtilIsAlpha(peekcurr()) || lexUtilIsDigit(peekcurr()))
                {
                    advance();
                }
                return createtoken(AstToken::T_DECORATOR);
            }

            AstToken scantoken()
            {
                char c;
                bool isdollar;
                AstToken tk;
                AstToken token;
                tk = skipspace();
                if(tk.m_toktype != AstToken::T_UNDEFINED)
                {
                    return tk;
                }
                m_start = m_sourceptr;
                if(isatend())
                {
                    return createtoken(AstToken::T_EOF);
                }
                c = advance();
                if(lexUtilIsDigit(c))
                {
                    return scannumber();
                }
                else if(lexUtilIsAlpha(c) || (c == '$'))
                {
                    isdollar = (c == '$');
                    return scanident(isdollar);
                }
                switch(c)
                {
                    case '(':
                        {
                            return createtoken(AstToken::T_PARENOPEN);
                        }
                        break;
                    case ')':
                        {
                            return createtoken(AstToken::T_PARENCLOSE);
                        }
                        break;
                    case '[':
                        {
                            return createtoken(AstToken::T_BRACKETOPEN);
                        }
                        break;
                    case ']':
                        {
                            return createtoken(AstToken::T_BRACKETCLOSE);
                        }
                        break;
                    case '{':
                        {
                            return createtoken(AstToken::T_BRACEOPEN);
                        }
                        break;
                    case '}':
                        {
                            if(m_tplstringcount > -1)
                            {
                                token = scanstring((char)m_tplstringbuffer[m_tplstringcount], true, true);
                                m_tplstringcount--;
                                return token;
                            }
                            return createtoken(AstToken::T_BRACECLOSE);
                        }
                        break;
                    case ';':
                        {
                            return createtoken(AstToken::T_SEMICOLON);
                        }
                        break;
                    case '\\':
                        {
                            return createtoken(AstToken::T_BACKSLASH);
                        }
                        break;
                    case ':':
                        {
                            return createtoken(AstToken::T_COLON);
                        }
                        break;
                    case ',':
                        {
                            return createtoken(AstToken::T_COMMA);
                        }
                        break;
                    case '@':
                        {
                            if(!lexUtilIsAlpha(peekcurr()))
                            {
                                return createtoken(AstToken::T_AT);
                            }
                            return scandecorator();
                        }
                        break;
                    case '!':
                        {
                            if(match('='))
                            {
                                /* pseudo-handle '!==' */
                                if(match('='))
                                {
                                    return createtoken(AstToken::T_NOTEQUAL);
                                }
                                return createtoken(AstToken::T_NOTEQUAL);
                            }
                            return createtoken(AstToken::T_EXCLMARK);
                        }
                        break;
                    case '.':
                        {
                            if(match('.'))
                            {
                                if(match('.'))
                                {
                                    return createtoken(AstToken::T_TRIPLEDOT);
                                }
                                return createtoken(AstToken::T_DOUBLEDOT);
                            }
                            return createtoken(AstToken::T_DOT);
                        }
                        break;
                    case '+':
                        {
                            if(match('+'))
                            {
                                return createtoken(AstToken::T_INCREMENT);
                            }
                            if(match('='))
                            {
                                return createtoken(AstToken::T_PLUSASSIGN);
                            }
                            else
                            {
                                return createtoken(AstToken::T_PLUS);
                            }
                        }
                        break;
                    case '-':
                        {
                            if(match('-'))
                            {
                                return createtoken(AstToken::T_DECREMENT);
                            }
                            if(match('='))
                            {
                                return createtoken(AstToken::T_MINUSASSIGN);
                            }
                            else
                            {
                                return createtoken(AstToken::T_MINUS);
                            }
                        }
                        break;
                    case '*':
                        {
                            if(match('*'))
                            {
                                if(match('='))
                                {
                                    return createtoken(AstToken::T_POWASSIGN);
                                }
                                return createtoken(AstToken::T_POWEROF);
                            }
                            else
                            {
                                if(match('='))
                                {
                                    return createtoken(AstToken::T_MULTASSIGN);
                                }
                                return createtoken(AstToken::T_MULTIPLY);
                            }
                        }
                        break;
                    case '/':
                        {
                            if(match('='))
                            {
                                return createtoken(AstToken::T_DIVASSIGN);
                            }
                            return createtoken(AstToken::T_DIVIDE);
                        }
                        break;
                    case '=':
                        {
                            if(match('='))
                            {
                                /* pseudo-handle === */
                                if(match('='))
                                {
                                    return createtoken(AstToken::T_EQUAL);
                                }
                                return createtoken(AstToken::T_EQUAL);
                            }
                            return createtoken(AstToken::T_ASSIGN);
                        }
                        break;
                    case '<':
                        {
                            if(match('<'))
                            {
                                if(match('='))
                                {
                                    return createtoken(AstToken::T_LEFTSHIFTASSIGN);
                                }
                                return createtoken(AstToken::T_LEFTSHIFT);
                            }
                            else
                            {
                                if(match('='))
                                {
                                    return createtoken(AstToken::T_LESSEQUAL);
                                }
                                return createtoken(AstToken::T_LESSTHAN);
                            }
                        }
                        break;
                    case '>':
                        {
                            if(match('>'))
                            {
                                if(match('='))
                                {
                                    return createtoken(AstToken::T_RIGHTSHIFTASSIGN);
                                }
                                return createtoken(AstToken::T_RIGHTSHIFT);
                            }
                            else
                            {
                                if(match('='))
                                {
                                    return createtoken(AstToken::T_GREATER_EQ);
                                }
                                return createtoken(AstToken::T_GREATERTHAN);
                            }
                        }
                        break;
                    case '%':
                        {
                            if(match('='))
                            {
                                return createtoken(AstToken::T_PERCENT_EQ);
                            }
                            return createtoken(AstToken::T_MODULO);
                        }
                        break;
                    case '&':
                        {
                            if(match('&'))
                            {
                                return createtoken(AstToken::T_KWAND);
                            }
                            else if(match('='))
                            {
                                return createtoken(AstToken::T_AMP_EQ);
                            }
                            return createtoken(AstToken::T_AMP);
                        }
                        break;
                    case '|':
                        {
                            if(match('|'))
                            {
                                return createtoken(AstToken::T_KWOR);
                            }
                            else if(match('='))
                            {
                                return createtoken(AstToken::T_BAR_EQ);
                            }
                            return createtoken(AstToken::T_BAR);
                        }
                        break;
                    case '~':
                        {
                            if(match('='))
                            {
                                return createtoken(AstToken::T_TILDE_EQ);
                            }
                            return createtoken(AstToken::T_TILDE);
                        }
                        break;
                    case '^':
                        {
                            if(match('='))
                            {
                                return createtoken(AstToken::T_XOR_EQ);
                            }
                            return createtoken(AstToken::T_XOR);
                        }
                        break;
                    case '\n':
                        {
                            return createtoken(AstToken::T_NEWLINE);
                        }
                        break;
                    case '"':
                        {
                            return scanstring('"', false, true);
                        }
                        break;
                    case '\'':
                        {
                            return scanstring('\'', false, false);
                        }
                        break;
                    case '`':
                        {
                            return scanstring('`', true, true);
                        }
                        break;
                    case '?':
                        {
                            return createtoken(AstToken::T_QUESTION);
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
                return errortoken("unexpected character %c", c);
            }
    };

    static const char* g_strthis = "this";
    static const char* g_strsuper = "super";

    class AstParser
    {
        public:
            enum
            {
                /* how many locals per function can be compiled */
                CONF_MAXLOCALS = (64 * 2),

                /* how many upvalues per function can be compiled */
                CONF_MAXUPVALS = (64 * 2),

                /* how many switch cases per switch statement */
                CONF_MAXSWITCHES = (32),

                /* max number of function parameters */
                CONF_MAXFUNCPARAMS = (32),              
            };

            enum CompContext
            {
                COMPCTX_NONE,
                COMPCTX_CLASS,
                COMPCTX_ARRAY,
                COMPCTX_NESTEDFUNCTION
            };

            class Rule
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

                    using ParsePrefixFN = bool(*)(AstParser*, bool);
                    using ParseInfixFN = bool(*)(AstParser*, AstToken, bool);

                public:
                    ParsePrefixFN prefix;
                    ParseInfixFN infix;
                    Precedence precedence;
            };

            struct LocalVarInfo
            {
                bool iscaptured;
                int depth;
                AstToken m_localname;
            };

            struct UpvalInfo
            {
                bool islocal;
                uint16_t index;
            };

            class FuncCompiler
            {
                public:
                    int m_localcount;
                    int m_scopedepth;
                    int m_handlercount;
                    FuncCompiler* m_enclosing;
                    AstParser* m_sharedprs;
                    /* current function */
                    Function* m_targetfunc;
                    Function::ContextType m_contexttype;
                    /* TODO: these should be dynamically allocated */
                    LocalVarInfo m_locals[CONF_MAXLOCALS];
                    UpvalInfo m_upvalues[CONF_MAXUPVALS];

                public:
                    FuncCompiler(AstParser* prs, Function::ContextType type, bool isanon)
                    {
                        bool candeclthis;
                        IOStream wtmp;
                        LocalVarInfo* local;
                        String* fname;
                        auto gcs = SharedState::get();
                        m_sharedprs = prs;
                        m_enclosing = m_sharedprs->m_currentfunccompiler;
                        m_targetfunc = nullptr;
                        m_contexttype = type;
                        m_localcount = 0;
                        m_scopedepth = 0;
                        m_handlercount = 0;
                        m_targetfunc = Function::makeFuncScript(m_sharedprs->m_currentmodule, type);
                        m_sharedprs->m_currentfunccompiler = this;
                        if(type != Function::CTXTYPE_SCRIPT)
                        {
                            gcs->vmStackPush(Value::fromObject(m_targetfunc));
                            if(isanon)
                            {
                                IOStream::makeStackString(&wtmp);
                                wtmp.format("anonymous@[%s:%d]", m_sharedprs->m_currentfile, m_sharedprs->m_prevtoken.m_line);
                                fname = wtmp.takeString();
                                IOStream::destroy(&wtmp);
                            }
                            else
                            {
                                fname = String::copy(m_sharedprs->m_prevtoken.m_start, m_sharedprs->m_prevtoken.length);
                            }
                            m_sharedprs->m_currentfunccompiler->m_targetfunc->m_funcname = fname;
                            gcs->vmStackPop();
                        }
                        /* claiming slot zero for use in class methods */
                        local = &m_sharedprs->m_currentfunccompiler->m_locals[0];
                        m_sharedprs->m_currentfunccompiler->m_localcount++;
                        local->depth = 0;
                        local->iscaptured = false;
                        candeclthis = ((type != Function::CTXTYPE_FUNCTION) && (m_sharedprs->m_compcontext == COMPCTX_CLASS));
                        if(candeclthis || (/*(type == Function::CTXTYPE_ANONYMOUS) &&*/ (m_sharedprs->m_compcontext != COMPCTX_CLASS)))
                        {
                            local->m_localname.m_start = g_strthis;
                            local->m_localname.length = 4;
                        }
                        else
                        {
                            local->m_localname.m_start = "";
                            local->m_localname.length = 0;
                        }
                    }

                    int resolveLocal(AstToken* name)
                    {
                        int i;
                        LocalVarInfo* local;
                        for(i = m_localcount - 1; i >= 0; i--)
                        {
                            local = &m_locals[i];
                            if(utilIdentsEqual(&local->m_localname, name))
                            {
                                #if 0
                                    if(local->depth == -1)
                                    {
                                        m_sharedprs->raiseerror("cannot read local variable in it's own initializer");
                                    }
                                #endif
                                return i;
                            }
                        }
                        return -1;
                    }

                    int addUpvalue(uint16_t index, bool islocal)
                    {
                        int i;
                        int upcnt;
                        UpvalInfo* upvalue;
                        upcnt = m_targetfunc->m_upvalcount;
                        for(i = 0; i < upcnt; i++)
                        {
                            upvalue = &m_upvalues[i];
                            if(upvalue->index == index && upvalue->islocal == islocal)
                            {
                                return i;
                            }
                        }
                        if(upcnt == CONF_MAXUPVALS)
                        {
                            m_sharedprs->raiseerror("too many closure variables in function");
                            return 0;
                        }
                        m_upvalues[upcnt].islocal = islocal;
                        m_upvalues[upcnt].index = index;
                        return m_targetfunc->m_upvalcount++;
                    }

                    int resolveUpvalue(AstToken* name)
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
                            m_enclosing->m_locals[local].iscaptured = true;
                            return addUpvalue((uint16_t)local, true);
                        }
                        upvalue = m_enclosing->resolveUpvalue(name);
                        if(upvalue != -1)
                        {
                            return addUpvalue((uint16_t)upvalue, false);
                        }
                        return -1;
                    }

                    void parseParamList()
                    {
                        int defvalconst;
                        int paramconst;
                        size_t paramid;
                        AstToken paramname;
                        AstToken vargname;
                        (void)paramid;
                        (void)paramname;
                        (void)defvalconst;
                        paramid = 0;
                        /* compile argument list... */
                        do
                        {
                            m_sharedprs->ignorewhitespace();
                            m_sharedprs->m_currentfunccompiler->m_targetfunc->m_fnvals.fnscriptfunc.arity++;
                            if(m_sharedprs->match(AstToken::T_TRIPLEDOT))
                            {
                                m_sharedprs->m_currentfunccompiler->m_targetfunc->m_fnvals.fnscriptfunc.isvariadic = true;
                                m_sharedprs->consume(AstToken::T_IDENTNORMAL, "expected identifier after '...'");
                                vargname = m_sharedprs->m_prevtoken;
                                m_sharedprs->addlocal(vargname);
                                m_sharedprs->definevariable(0);
                                break;
                            }
                            paramconst = m_sharedprs->parsefuncparamvar("expected parameter name");
                            paramname = m_sharedprs->m_prevtoken;
                            m_sharedprs->definevariable(paramconst);
                            m_sharedprs->ignorewhitespace();
                            if(m_sharedprs->match(AstToken::T_ASSIGN))
                            {
                                fprintf(stderr, "parsing optional argument....\n");
                                if(!m_sharedprs->parseexpression())
                                {
                                    m_sharedprs->raiseerror("failed to parse function default paramter value");
                                }
                                #if 0
                                    defvalconst = m_sharedprs->addlocal(paramname);
                                #else
                                    defvalconst = paramconst;
                                #endif
                                #if 1
                                    #if 1
                                        m_sharedprs->emitbyteandshort(Instruction::OPC_FUNCARGOPTIONAL, defvalconst);
                                        // emit1short(paramid);
                                    #else
                                        m_sharedprs->emitbyteandshort(Instruction::OPC_LOCALSET, defvalconst);
                                    #endif
                                #endif
                            }
                            m_sharedprs->ignorewhitespace();
                            paramid++;

                        } while(m_sharedprs->match(AstToken::T_COMMA));
                    }

                    void compileBody(bool closescope, bool isanon)
                    {
                        int i;
                        Function* function;
                        (void)isanon;
                        auto gcs = SharedState::get();
                        /* compile the body */
                        m_sharedprs->ignorewhitespace();
                        m_sharedprs->consume(AstToken::T_BRACEOPEN, "expected '{' before function body");
                        m_sharedprs->parseblock();
                        /* create the function object */
                        if(closescope)
                        {
                            m_sharedprs->scopeend();
                        }
                        function = m_sharedprs->endcompiler(false);
                        gcs->vmStackPush(Value::fromObject(function));
                        m_sharedprs->emitbyteandshort(Instruction::OPC_MAKECLOSURE, m_sharedprs->pushconst(Value::fromObject(function)));
                        for(i = 0; i < function->m_upvalcount; i++)
                        {
                            m_sharedprs->emit1byte(m_upvalues[i].islocal ? 1 : 0);
                            m_sharedprs->emit1short(m_upvalues[i].index);
                        }
                        gcs->vmStackPop();
                    }
            };

            class ClassCompiler
            {
                public:
                    bool hassuperclass;
                    ClassCompiler* m_enclosing;
                    AstToken m_classcompilername;
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
            bool m_inswitch;
            bool m_stopprintingsyntaxerrors;

            /* used for tracking loops for the continue statement... */
            int m_innermostloopstart;
            int m_innermostloopscopedepth;
            int m_blockcount;
            int m_errorcount;
            /* the context in which the parser resides; none (outer level), inside a class, dict, array, etc */
            CompContext m_compcontext;
            const char* m_currentfile;
            AstLexer* m_lexer;
            AstToken m_currtoken;
            AstToken m_prevtoken;
            FuncCompiler* m_currentfunccompiler;
            ClassCompiler* m_currentclasscompiler;
            Module* m_currentmodule;

        public:
            static AstParser* make(AstLexer* m_lexer, Module* module, bool keeplast)
            {
                AstParser* parser;
                parser = Memory::make<AstParser>();
                parser->m_lexer = m_lexer;
                parser->m_currentfunccompiler = nullptr;
                parser->m_haderror = false;
                parser->m_panicmode = false;
                parser->m_stopprintingsyntaxerrors = false;
                parser->m_blockcount = 0;
                parser->m_errorcount = 0;
                parser->m_replcanecho = false;
                parser->m_isreturning = false;
                parser->m_istrying = false;
                parser->m_compcontext = COMPCTX_NONE;
                parser->m_innermostloopstart = -1;
                parser->m_innermostloopscopedepth = 0;
                parser->m_currentclasscompiler = nullptr;
                parser->m_currentmodule = module;
                parser->m_keeplastvalue = keeplast;
                parser->m_lastwasstatement = false;
                parser->m_infunction = false;
                parser->m_inswitch = false;
                parser->m_currentfile = parser->m_currentmodule->m_physicalpath->data();
                return parser;
            }

            static void destroy(AstParser* parser)
            {
                Memory::sysFree(parser);
            }

            static Value utilConvertNumberString(AstToken::Type type, const char* source)
            {
                double dbval;
                long longval;
                int64_t llval;
                if(type == AstToken::T_LITNUMBIN)
                {
                    llval = strtoll(source + 2, nullptr, 2);
                    return Value::makeNumber(llval);
                }
                else if(type == AstToken::T_LITNUMOCT)
                {
                    longval = strtol(source + 2, nullptr, 8);
                    return Value::makeNumber(longval);
                }
                else if(type == AstToken::T_LITNUMHEX)
                {
                    longval = strtol(source, nullptr, 16);
                    return Value::makeNumber(longval);
                }
                dbval = strtod(source, nullptr);
                return Value::makeNumber(dbval);
            }

            static AstToken utilMakeSynthToken(const char* name)
            {
                AstToken token;
                token.isglobal = false;
                token.m_line = 0;
                token.m_toktype = (AstToken::Type)0;
                token.m_start = name;
                token.length = (int)strlen(name);
                return token;
            }

            static bool utilIdentsEqual(AstToken* a, AstToken* b)
            {
                return a->length == b->length && memcmp(a->m_start, b->m_start, a->length) == 0;
            }

            static bool astruleanonfunc(AstParser* prs, bool canassign)
            {
                (void)canassign;
                FuncCompiler fnc(prs, Function::CTXTYPE_FUNCTION, true);
                prs->scopebegin();
                /* compile parameter list */
                if(prs->check(AstToken::T_IDENTNORMAL))
                {
                    prs->consume(AstToken::T_IDENTNORMAL, "optional name for anonymous function");
                }
                prs->consume(AstToken::T_PARENOPEN, "expected '(' at start of anonymous function");
                if(!prs->check(AstToken::T_PARENCLOSE))
                {
                    fnc.parseParamList();
                }
                prs->consume(AstToken::T_PARENCLOSE, "expected ')' after anonymous function parameters");
                fnc.compileBody(true, true);
                return true;
            }

            static bool astruleanonclass(AstParser* prs, bool canassign)
            {
                (void)canassign;
                prs->parseclassdecl(false);
                return true;
            }

            static bool astrulenumber(AstParser* prs, bool canassign)
            {
                (void)canassign;
                prs->emitconst(prs->compilenumber());
                return true;
            }

            static bool astrulebinary(AstParser* prs, AstToken previous, bool canassign)
            {
                AstToken::Type op;
                Rule* rule;
                (void)previous;
                (void)canassign;
                op = prs->m_prevtoken.m_toktype;
                /* compile the right operand */
                rule = getRule(op);
                prs->parseprecedence((Rule::Precedence)(rule->precedence + 1));
                /* emit the operator instruction */
                switch(op)
                {
                    case AstToken::T_PLUS:
                        prs->emitinstruc(Instruction::OPC_PRIMADD);
                        break;
                    case AstToken::T_MINUS:
                        prs->emitinstruc(Instruction::OPC_PRIMSUBTRACT);
                        break;
                    case AstToken::T_MULTIPLY:
                        prs->emitinstruc(Instruction::OPC_PRIMMULTIPLY);
                        break;
                    case AstToken::T_DIVIDE:
                        prs->emitinstruc(Instruction::OPC_PRIMDIVIDE);
                        break;
                    case AstToken::T_MODULO:
                        prs->emitinstruc(Instruction::OPC_PRIMMODULO);
                        break;
                    case AstToken::T_POWEROF:
                        prs->emitinstruc(Instruction::OPC_PRIMPOW);
                        break;
                        /* equality */
                    case AstToken::T_EQUAL:
                        prs->emitinstruc(Instruction::OPC_EQUAL);
                        break;
                    case AstToken::T_NOTEQUAL:
                        prs->emit2byte(Instruction::OPC_EQUAL, Instruction::OPC_PRIMNOT);
                        break;
                    case AstToken::T_GREATERTHAN:
                        prs->emitinstruc(Instruction::OPC_PRIMGREATER);
                        break;
                    case AstToken::T_GREATER_EQ:
                        prs->emit2byte(Instruction::OPC_PRIMLESSTHAN, Instruction::OPC_PRIMNOT);
                        break;
                    case AstToken::T_LESSTHAN:
                        prs->emitinstruc(Instruction::OPC_PRIMLESSTHAN);
                        break;
                    case AstToken::T_LESSEQUAL:
                        prs->emit2byte(Instruction::OPC_PRIMGREATER, Instruction::OPC_PRIMNOT);
                        break;
                        /* bitwise */
                    case AstToken::T_AMP:
                        prs->emitinstruc(Instruction::OPC_PRIMAND);
                        break;
                    case AstToken::T_BAR:
                        prs->emitinstruc(Instruction::OPC_PRIMOR);
                        break;
                    case AstToken::T_XOR:
                        prs->emitinstruc(Instruction::OPC_PRIMBITXOR);
                        break;
                    case AstToken::T_LEFTSHIFT:
                        prs->emitinstruc(Instruction::OPC_PRIMSHIFTLEFT);
                        break;
                    case AstToken::T_RIGHTSHIFT:
                        prs->emitinstruc(Instruction::OPC_PRIMSHIFTRIGHT);
                        break;
                        /* range */
                    case AstToken::T_DOUBLEDOT:
                        prs->emitinstruc(Instruction::OPC_MAKERANGE);
                        break;
                    default:
                        break;
                }
                return true;
            }

            static bool astrulecall(AstParser* prs, AstToken previous, bool canassign)
            {
                uint32_t argcount;
                (void)previous;
                (void)canassign;
                argcount = prs->parsefunccallargs();
                prs->emit2byte(Instruction::OPC_CALLFUNCTION, argcount);
                return true;
            }

            static bool astruleliteral(AstParser* prs, bool canassign)
            {
                (void)canassign;
                switch(prs->m_prevtoken.m_toktype)
                {
                    case AstToken::T_KWNULL:
                        prs->emitinstruc(Instruction::OPC_PUSHNULL);
                        break;
                    case AstToken::T_KWTRUE:
                        prs->emitinstruc(Instruction::OPC_PUSHTRUE);
                        break;
                    case AstToken::T_KWFALSE:
                        prs->emitinstruc(Instruction::OPC_PUSHFALSE);
                        break;
                    default:
                        /* TODO: assuming this is correct behaviour ... */
                        return false;
                }
                return true;
            }

            static bool astruledot(AstParser* prs, AstToken previous, bool canassign)
            {
                int name;
                bool caninvoke;
                uint16_t argcount;
                Instruction::OpCode getop;
                Instruction::OpCode setop;
                prs->ignorewhitespace();
                if(!prs->consume(AstToken::T_IDENTNORMAL, "expected property name after '.'"))
                {
                    return false;
                }
                name = prs->makeidentconst(&prs->m_prevtoken);
                if(prs->match(AstToken::T_PARENOPEN))
                {
                    argcount = prs->parsefunccallargs();
                    caninvoke = ((prs->m_currentclasscompiler != nullptr) && ((previous.m_toktype == AstToken::T_KWTHIS) || (utilIdentsEqual(&prs->m_prevtoken, &prs->m_currentclasscompiler->m_classcompilername))));
                    if(caninvoke)
                    {
                        prs->emitbyteandshort(Instruction::OPC_CLASSINVOKETHIS, name);
                    }
                    else
                    {
                        prs->emitbyteandshort(Instruction::OPC_CALLMETHOD, name);
                    }
                    prs->emit1byte(argcount);
                }
                else
                {
                    getop = Instruction::OPC_PROPERTYGET;
                    setop = Instruction::OPC_PROPERTYSET;
                    if(prs->m_currentclasscompiler != nullptr && (previous.m_toktype == AstToken::T_KWTHIS || utilIdentsEqual(&prs->m_prevtoken, &prs->m_currentclasscompiler->m_classcompilername)))
                    {
                        getop = Instruction::OPC_PROPERTYGETSELF;
                    }
                    prs->assignment(getop, setop, name, canassign);
                }
                return true;
            }

            static bool astrulearray(AstParser* prs, bool canassign)
            {
                int count;
                (void)canassign;
                /* placeholder for the list */
                prs->emitinstruc(Instruction::OPC_PUSHNULL);
                count = 0;
                prs->ignorewhitespace();
                if(!prs->check(AstToken::T_BRACKETCLOSE))
                {
                    do
                    {
                        prs->ignorewhitespace();
                        if(!prs->check(AstToken::T_BRACKETCLOSE))
                        {
                            /* allow comma to end lists */
                            prs->parseexpression();
                            prs->ignorewhitespace();
                            count++;
                        }
                        prs->ignorewhitespace();
                    } while(prs->match(AstToken::T_COMMA));
                }
                prs->ignorewhitespace();
                prs->consume(AstToken::T_BRACKETCLOSE, "expected ']' at end of list");
                prs->emitbyteandshort(Instruction::OPC_MAKEARRAY, count);
                return true;
            }

            static bool astruledictionary(AstParser* prs, bool canassign)
            {
                bool usedexpression;
                int itemcount;
                (void)canassign;
                /* placeholder for the dictionary */
                prs->emitinstruc(Instruction::OPC_PUSHNULL);
                itemcount = 0;
                prs->ignorewhitespace();
                if(!prs->check(AstToken::T_BRACECLOSE))
                {
                    do
                    {
                        prs->ignorewhitespace();
                        if(!prs->check(AstToken::T_BRACECLOSE))
                        {
                            /* allow last pair to end with a comma */
                            usedexpression = false;
                            if(prs->check(AstToken::T_IDENTNORMAL))
                            {
                                prs->consume(AstToken::T_IDENTNORMAL, "");
                                prs->emitconst(Value::fromObject(String::copy(prs->m_prevtoken.m_start, prs->m_prevtoken.length)));
                            }
                            else
                            {
                                prs->parseexpression();
                                usedexpression = true;
                            }
                            prs->ignorewhitespace();
                            if(!prs->check(AstToken::T_COMMA) && !prs->check(AstToken::T_BRACECLOSE))
                            {
                                prs->consume(AstToken::T_COLON, "expected ':' after dictionary key");
                                prs->ignorewhitespace();

                                prs->parseexpression();
                            }
                            else
                            {
                                if(usedexpression)
                                {
                                    prs->raiseerror("cannot infer dictionary values from expressions");
                                    return false;
                                }
                                else
                                {
                                    prs->namedvar(prs->m_prevtoken, false);
                                }
                            }
                            itemcount++;
                        }
                    } while(prs->match(AstToken::T_COMMA));
                }
                prs->ignorewhitespace();
                prs->consume(AstToken::T_BRACECLOSE, "expected '}' after dictionary");
                prs->emitbyteandshort(Instruction::OPC_MAKEDICT, itemcount);
                return true;
            }

            static bool astruleindexing(AstParser* prs, AstToken previous, bool canassign)
            {
                bool assignable;
                bool commamatch;
                uint16_t getop;
                (void)previous;
                (void)canassign;
                assignable = true;
                commamatch = false;
                getop = Instruction::OPC_INDEXGET;
                if(prs->match(AstToken::T_COMMA))
                {
                    prs->emitinstruc(Instruction::OPC_PUSHNULL);
                    commamatch = true;
                    getop = Instruction::OPC_INDEXGETRANGED;
                }
                else
                {
                    prs->parseexpression();
                }
                if(!prs->match(AstToken::T_BRACKETCLOSE))
                {
                    getop = Instruction::OPC_INDEXGETRANGED;
                    if(!commamatch)
                    {
                        prs->consume(AstToken::T_COMMA, "expecting ',' or ']'");
                    }
                    if(prs->match(AstToken::T_BRACKETCLOSE))
                    {
                        prs->emitinstruc(Instruction::OPC_PUSHNULL);
                    }
                    else
                    {
                        prs->parseexpression();
                        prs->consume(AstToken::T_BRACKETCLOSE, "expected ']' after indexing");
                    }
                    assignable = false;
                }
                else
                {
                    if(commamatch)
                    {
                        prs->emitinstruc(Instruction::OPC_PUSHNULL);
                    }
                }
                prs->assignment(getop, Instruction::OPC_INDEXSET, -1, assignable);
                return true;
            }

            static bool astrulevarnormal(AstParser* prs, bool canassign)
            {
                prs->namedvar(prs->m_prevtoken, canassign);
                return true;
            }

            static bool astrulethis(AstParser* prs, bool canassign)
            {
                (void)canassign;
            #if 0
                if(prs->m_currentclasscompiler == nullptr)
                {
                    prs->raiseerror("cannot use keyword 'this' outside of a class");
                    return false;
                }
            #endif
            #if 0
                if(prs->m_currentclasscompiler != nullptr)
            #endif
                {
                    prs->namedvar(prs->m_prevtoken, false);
            #if 0
                        prs->namedvar(utilMakeSynthToken(g_strthis), false);
            #endif
                }
            #if 0
                    prs->emitinstruc(Instruction::OPC_CLASSGETTHIS);
            #endif
                return true;
            }

            static bool astrulesuper(AstParser* prs, bool canassign)
            {
                int name;
                bool invokeself;
                uint16_t argcount;
                (void)canassign;
                if(prs->m_currentclasscompiler == nullptr)
                {
                    prs->raiseerror("cannot use keyword 'super' outside of a class");
                    return false;
                }
                else if(!prs->m_currentclasscompiler->hassuperclass)
                {
                    prs->raiseerror("cannot use keyword 'super' in a class without a superclass");
                    return false;
                }
                name = -1;
                invokeself = false;
                if(!prs->check(AstToken::T_PARENOPEN))
                {
                    prs->consume(AstToken::T_DOT, "expected '.' or '(' after super");
                    prs->consume(AstToken::T_IDENTNORMAL, "expected super class method name after .");
                    name = prs->makeidentconst(&prs->m_prevtoken);
                }
                else
                {
                    invokeself = true;
                }
                prs->namedvar(utilMakeSynthToken(g_strthis), false);
                if(prs->match(AstToken::T_PARENOPEN))
                {
                    argcount = prs->parsefunccallargs();
                    prs->namedvar(utilMakeSynthToken(g_strsuper), false);
                    if(!invokeself)
                    {
                        prs->emitbyteandshort(Instruction::OPC_CLASSINVOKESUPER, name);
                        prs->emit1byte(argcount);
                    }
                    else
                    {
                        prs->emit2byte(Instruction::OPC_CLASSINVOKESUPERSELF, argcount);
                    }
                }
                else
                {
                    prs->namedvar(utilMakeSynthToken(g_strsuper), false);
                    prs->emitbyteandshort(Instruction::OPC_CLASSGETSUPER, name);
                }
                return true;
            }

            static bool astrulegrouping(AstParser* prs, bool canassign)
            {
                (void)canassign;
                prs->ignorewhitespace();
                prs->parseexpression();
                while(prs->match(AstToken::T_COMMA))
                {
                    prs->parseexpression();
                }
                prs->ignorewhitespace();
                prs->consume(AstToken::T_PARENCLOSE, "expected ')' after grouped expression");
                return true;
            }

            static bool astrulestring(AstParser* prs, bool canassign)
            {
                int length;
                char* str;
                (void)canassign;
                str = prs->compilestring(&length, true);
                prs->emitconst(Value::fromObject(String::take(str, length)));
                return true;
            }

            static bool astrulerawstring(AstParser* prs, bool canassign)
            {
                int length;
                char* str;
                (void)canassign;
                str = prs->compilestring(&length, false);
                prs->emitconst(Value::fromObject(String::take(str, length)));
                return true;
            }

            static bool astruleinterpolstring(AstParser* prs, bool canassign)
            {
                int count;
                bool doadd;
                bool stringmatched;
                count = 0;
                do
                {
                    doadd = false;
                    stringmatched = false;
                    if(prs->m_prevtoken.length - 2 > 0)
                    {
                        astrulestring(prs, canassign);
                        doadd = true;
                        stringmatched = true;
                        if(count > 0)
                        {
                            prs->emitinstruc(Instruction::OPC_PRIMADD);
                        }
                    }
                    prs->parseexpression();
                    prs->emitinstruc(Instruction::OPC_STRINGIFY);
                    if(doadd || (count >= 1 && stringmatched == false))
                    {
                        prs->emitinstruc(Instruction::OPC_PRIMADD);
                    }
                    count++;
                } while(prs->match(AstToken::T_INTERPOLATION));
                prs->consume(AstToken::T_LITERALSTRING, "unterminated string interpolation");
                if(prs->m_prevtoken.length - 2 > 0)
                {
                    astrulestring(prs, canassign);
                    prs->emitinstruc(Instruction::OPC_PRIMADD);
                }
                return true;
            }

            static bool astruleunary(AstParser* prs, bool canassign)
            {
                AstToken::Type op;
                (void)canassign;
                op = prs->m_prevtoken.m_toktype;
                /* compile the expression */
                prs->parseprecedence(Rule::PREC_UNARY);
                /* emit instruction */
                switch(op)
                {
                    case AstToken::T_MINUS:
                        prs->emitinstruc(Instruction::OPC_PRIMNEGATE);
                        break;
                    case AstToken::T_EXCLMARK:
                        prs->emitinstruc(Instruction::OPC_PRIMNOT);
                        break;
                    case AstToken::T_TILDE:
                        prs->emitinstruc(Instruction::OPC_PRIMBITNOT);
                        break;
                    default:
                        break;
                }
                return true;
            }

            static bool astruleand(AstParser* prs, AstToken previous, bool canassign)
            {
                int endjump;
                (void)previous;
                (void)canassign;
                endjump = prs->emitjump(Instruction::OPC_JUMPIFFALSE);
                prs->emitinstruc(Instruction::OPC_POPONE);
                prs->parseprecedence(Rule::PREC_AND);
                prs->patchjump(endjump);
                return true;
            }

            static bool astruleor(AstParser* prs, AstToken previous, bool canassign)
            {
                int endjump;
                int elsejump;
                (void)previous;
                (void)canassign;
                elsejump = prs->emitjump(Instruction::OPC_JUMPIFFALSE);
                endjump = prs->emitjump(Instruction::OPC_JUMPNOW);
                prs->patchjump(elsejump);
                prs->emitinstruc(Instruction::OPC_POPONE);
                prs->parseprecedence(Rule::PREC_OR);
                prs->patchjump(endjump);
                return true;
            }

            static bool astruleinstanceof(AstParser* prs, AstToken previous, bool canassign)
            {
                (void)previous;
                (void)canassign;
                prs->parseexpression();
                prs->emitinstruc(Instruction::OPC_OPINSTANCEOF);

                return true;
            }

            static bool astruleconditional(AstParser* prs, AstToken previous, bool canassign)
            {
                int thenjump;
                int elsejump;
                (void)previous;
                (void)canassign;
                thenjump = prs->emitjump(Instruction::OPC_JUMPIFFALSE);
                prs->emitinstruc(Instruction::OPC_POPONE);
                prs->ignorewhitespace();
                /* compile the then expression */
                prs->parseprecedence(Rule::PREC_CONDITIONAL);
                prs->ignorewhitespace();
                elsejump = prs->emitjump(Instruction::OPC_JUMPNOW);
                prs->patchjump(thenjump);
                prs->emitinstruc(Instruction::OPC_POPONE);
                prs->consume(AstToken::T_COLON, "expected matching ':' after '?' conditional");
                prs->ignorewhitespace();
                /*
                // compile the else expression
                // here we parse at Rule::PREC_ASSIGNMENT precedence as
                // linear conditionals can be nested.
                */
                prs->parseprecedence(Rule::PREC_ASSIGNMENT);
                prs->patchjump(elsejump);
                return true;
            }


            static bool astrulenew(AstParser* prs, bool canassign)
            {
                prs->consume(AstToken::T_IDENTNORMAL, "class name after 'new'");
                return astrulevarnormal(prs, canassign);
            }

            static bool astruletypeof(AstParser* prs, bool canassign)
            {
                (void)canassign;
                prs->consume(AstToken::T_PARENOPEN, "expected '(' after 'typeof'");
                prs->parseexpression();
                prs->consume(AstToken::T_PARENCLOSE, "expected ')' after 'typeof'");
                prs->emitinstruc(Instruction::OPC_TYPEOF);
                return true;
            }

            static bool astrulenothingprefix(AstParser* prs, bool canassign)
            {
                (void)prs;
                (void)canassign;
                return true;
            }

            static bool astrulenothinginfix(AstParser* prs, AstToken previous, bool canassign)
            {
                (void)prs;
                (void)previous;
                (void)canassign;
                return true;
            }

            static Rule* putrule(Rule* dest, Rule::ParsePrefixFN prefix, Rule::ParseInfixFN infix, Rule::Precedence precedence)
            {
                dest->prefix = prefix;
                dest->infix = infix;
                dest->precedence = precedence;
                return dest;
            }

            #define dorule(tok, prefix, infix, precedence) \
                case tok:                                  \
                    return putrule(&dest, prefix, infix, precedence);

            static Rule* getRule(AstToken::Type type)
            {
                static Rule dest;
                switch(type)
                {
                    dorule(AstToken::T_NEWLINE, astrulenothingprefix, astrulenothinginfix, Rule::PREC_NONE);
                    dorule(AstToken::T_PARENOPEN, astrulegrouping, astrulecall, Rule::PREC_CALL);
                    dorule(AstToken::T_PARENCLOSE, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_BRACKETOPEN, astrulearray, astruleindexing, Rule::PREC_CALL);
                    dorule(AstToken::T_BRACKETCLOSE, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_BRACEOPEN, astruledictionary, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_BRACECLOSE, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_SEMICOLON, astrulenothingprefix, astrulenothinginfix, Rule::PREC_NONE);
                    dorule(AstToken::T_COMMA, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_BACKSLASH, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_EXCLMARK, astruleunary, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_NOTEQUAL, nullptr, astrulebinary, Rule::PREC_EQUALITY);
                    dorule(AstToken::T_COLON, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_AT, astruleanonfunc, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_DOT, nullptr, astruledot, Rule::PREC_CALL);
                    dorule(AstToken::T_DOUBLEDOT, nullptr, astrulebinary, Rule::PREC_RANGE);
                    dorule(AstToken::T_TRIPLEDOT, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_PLUS, astruleunary, astrulebinary, Rule::PREC_TERM);
                    dorule(AstToken::T_PLUSASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_INCREMENT, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_MINUS, astruleunary, astrulebinary, Rule::PREC_TERM);
                    dorule(AstToken::T_MINUSASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_DECREMENT, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_MULTIPLY, nullptr, astrulebinary, Rule::PREC_FACTOR);
                    dorule(AstToken::T_MULTASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_POWEROF, nullptr, astrulebinary, Rule::PREC_FACTOR);
                    dorule(AstToken::T_POWASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_DIVIDE, nullptr, astrulebinary, Rule::PREC_FACTOR);
                    dorule(AstToken::T_DIVASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_EQUAL, nullptr, astrulebinary, Rule::PREC_EQUALITY);
                    dorule(AstToken::T_LESSTHAN, nullptr, astrulebinary, Rule::PREC_COMPARISON);
                    dorule(AstToken::T_LESSEQUAL, nullptr, astrulebinary, Rule::PREC_COMPARISON);
                    dorule(AstToken::T_LEFTSHIFT, nullptr, astrulebinary, Rule::PREC_SHIFT);
                    dorule(AstToken::T_LEFTSHIFTASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_GREATERTHAN, nullptr, astrulebinary, Rule::PREC_COMPARISON);
                    dorule(AstToken::T_GREATER_EQ, nullptr, astrulebinary, Rule::PREC_COMPARISON);
                    dorule(AstToken::T_RIGHTSHIFT, nullptr, astrulebinary, Rule::PREC_SHIFT);
                    dorule(AstToken::T_RIGHTSHIFTASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_MODULO, nullptr, astrulebinary, Rule::PREC_FACTOR);
                    dorule(AstToken::T_PERCENT_EQ, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_AMP, nullptr, astrulebinary, Rule::PREC_BITAND);
                    dorule(AstToken::T_AMP_EQ, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_BAR, /*astruleanoncompat*/ nullptr, astrulebinary, Rule::PREC_BITOR);
                    dorule(AstToken::T_BAR_EQ, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_TILDE, astruleunary, nullptr, Rule::PREC_UNARY);
                    dorule(AstToken::T_TILDE_EQ, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_XOR, nullptr, astrulebinary, Rule::PREC_BITXOR);
                    dorule(AstToken::T_XOR_EQ, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_QUESTION, nullptr, astruleconditional, Rule::PREC_CONDITIONAL);
                    dorule(AstToken::T_KWAND, nullptr, astruleand, Rule::PREC_AND);
                    dorule(AstToken::T_KWAS, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWASSERT, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWBREAK, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWCLASS, astruleanonclass, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWCONTINUE, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWFUNCTION, astruleanonfunc, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWDEFAULT, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWTHROW, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWDO, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWECHO, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWELSE, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWFALSE, astruleliteral, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWFOREACH, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWIF, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWIN, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWINSTANCEOF, nullptr, astruleinstanceof, Rule::PREC_OR);
                    dorule(AstToken::T_KWFOR, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWVAR, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWNULL, astruleliteral, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWNEW, astrulenew, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWTYPEOF, astruletypeof, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWOR, nullptr, astruleor, Rule::PREC_OR);
                    dorule(AstToken::T_KWSUPER, astrulesuper, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWRETURN, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWTHIS, astrulethis, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWSTATIC, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWTRUE, astruleliteral, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWSWITCH, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWCASE, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWWHILE, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWTRY, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWCATCH, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWFINALLY, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_LITERALSTRING, astrulestring, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_LITERALRAWSTRING, astrulerawstring, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_LITNUMREG, astrulenumber, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_LITNUMBIN, astrulenumber, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_LITNUMOCT, astrulenumber, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_LITNUMHEX, astrulenumber, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_IDENTNORMAL, astrulevarnormal, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_INTERPOLATION, astruleinterpolstring, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_EOF, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_ERROR, nullptr, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_KWEMPTY, astruleliteral, nullptr, Rule::PREC_NONE);
                    dorule(AstToken::T_UNDEFINED, nullptr, nullptr, Rule::PREC_NONE);
                    default:
                        fprintf(stderr, "missing rule for %s?\n", AstLexer::tokTypeToString(type));
                        break;
                }
                return nullptr;
            }
            #undef dorule

        public:

            Blob* currentblob()
            {
                return m_currentfunccompiler->m_targetfunc->m_fnvals.fnscriptfunc.blob;
            }

            bool raiseerroratv(AstToken* t, const char* message, va_list args)
            {
                const char* colred;
                const char* colreset;
                auto gcs = SharedState::get();
                colred = Util::termColor(NEON_COLOR_RED);
                colreset = Util::termColor(NEON_COLOR_RESET);
                fflush(stdout);
                if(m_stopprintingsyntaxerrors)
                {
                    return false;
                }
                if((gcs->m_conf.maxsyntaxerrors != 0) && (m_errorcount >= gcs->m_conf.maxsyntaxerrors))
                {
                    fprintf(stderr, "%stoo many errors emitted%s (maximum is %d)\n", colred, colreset, gcs->m_conf.maxsyntaxerrors);
                    m_stopprintingsyntaxerrors = true;
                    return false;
                }
                /*
                // do not cascade error
                // suppress error if already in panic mode
                */
                if(m_panicmode)
                {
                    return false;
                }
                m_panicmode = true;
                fprintf(stderr, "(%d) %sSyntaxError%s", m_errorcount, colred, colreset);
                fprintf(stderr, " in [%s:%d]: ", m_currentmodule->m_physicalpath->data(), t->m_line);
                vfprintf(stderr, message, args);
                fprintf(stderr, " ");
                if(t->m_toktype == AstToken::T_EOF)
                {
                    fprintf(stderr, " at end");
                }
                else if(t->m_toktype == AstToken::T_ERROR)
                {
                    /* do nothing */
                    fprintf(stderr, "at <internal error>");
                }
                else
                {
                    if(t->length == 1 && *t->m_start == '\n')
                    {
                        fprintf(stderr, " at newline");
                    }
                    else
                    {
                        fprintf(stderr, " at '%.*s'", t->length, t->m_start);
                    }
                }
                fprintf(stderr, "\n");
                m_haderror = true;
                m_errorcount++;
                return false;
            }

            bool raiseerror(const char* message, ...)
            {
                va_list args;
                va_start(args, message);
                raiseerroratv(&m_prevtoken, message, args);
                va_end(args);
                return false;
            }

            bool raiseerroratcurrent(const char* message, ...)
            {
                va_list args;
                va_start(args, message);
                raiseerroratv(&m_currtoken, message, args);
                va_end(args);
                return false;
            }

            void advance()
            {
                m_prevtoken = m_currtoken;
                while(true)
                {
                    m_currtoken = m_lexer->scantoken();
                    if(m_currtoken.m_toktype != AstToken::T_ERROR)
                    {
                        break;
                    }
                    raiseerroratcurrent(m_currtoken.m_start);
                }
            }

            bool istype(AstToken::Type prev, AstToken::Type t)
            {
                if(t == AstToken::T_IDENTNORMAL)
                {
                    if(prev == AstToken::T_KWCLASS)
                    {
                        return true;
                    }
                }
                return (prev == t);
            }

            bool consume(AstToken::Type t, const char* message)
            {
                if(istype(m_currtoken.m_toktype, t))
                {
                    advance();
                    return true;
                }
                return raiseerroratcurrent(message);
            }

            void consumeor(const char* message, const AstToken::Type* ts, int count)
            {
                int i;
                for(i = 0; i < count; i++)
                {
                    if(m_currtoken.m_toktype == ts[i])
                    {
                        advance();
                        return;
                    }
                }
                raiseerroratcurrent(message);
            }

            bool checknumber()
            {
                AstToken::Type t;
                t = m_prevtoken.m_toktype;
                if(t == AstToken::T_LITNUMREG || t == AstToken::T_LITNUMOCT || t == AstToken::T_LITNUMBIN || t == AstToken::T_LITNUMHEX)
                {
                    return true;
                }
                return false;
            }

            bool check(AstToken::Type t)
            {
                return istype(m_currtoken.m_toktype, t);
            }

            bool match(AstToken::Type t)
            {
                if(!check(t))
                {
                    return false;
                }
                advance();
                return true;
            }

            void runparser()
            {
                advance();
                ignorewhitespace();
                while(!match(AstToken::T_EOF))
                {
                    parsedeclaration();
                }
            }

            void parsedeclaration()
            {
                ignorewhitespace();
                if(match(AstToken::T_KWCLASS))
                {
                    parseclassdecl(true);
                }
                else if(match(AstToken::T_KWFUNCTION))
                {
                    parsefuncdecl();
                }
                else if(match(AstToken::T_KWVAR))
                {
                    parsevardecl(false, false);
                }
                else if(match(AstToken::T_KWCONST))
                {
                    parsevardecl(false, true);
                }
                else if(match(AstToken::T_BRACEOPEN))
                {
                    if(!check(AstToken::T_NEWLINE) && m_currentfunccompiler->m_scopedepth == 0)
                    {
                        parseexprstmt(false, true);
                    }
                    else
                    {
                        scopebegin();
                        parseblock();
                        scopeend();
                    }
                }
                else
                {
                    parsestmt();
                }
                ignorewhitespace();
                if(m_panicmode)
                {
                    synchronize();
                }
                ignorewhitespace();
            }

            void parsestmt()
            {
                m_replcanecho = false;
                ignorewhitespace();
                if(match(AstToken::T_KWECHO))
                {
                    parseechostmt();
                }
                else if(match(AstToken::T_KWIF))
                {
                    parseifstmt();
                }
                else if(match(AstToken::T_KWDO))
                {
                    parsedo_whilestmt();
                }
                else if(match(AstToken::T_KWWHILE))
                {
                    parsewhilestmt();
                }
                else if(match(AstToken::T_KWFOR))
                {
                    parseforstmt();
                }
                else if(match(AstToken::T_KWFOREACH))
                {
                    parseforeachstmt();
                }
                else if(match(AstToken::T_KWSWITCH))
                {
                    parseswitchstmt();
                }
                else if(match(AstToken::T_KWCONTINUE))
                {
                    parsecontinuestmt();
                }
                else if(match(AstToken::T_KWBREAK))
                {
                    parsebreakstmt();
                }
                else if(match(AstToken::T_KWRETURN))
                {
                    parsereturnstmt();
                }
                else if(match(AstToken::T_KWASSERT))
                {
                    parseassertstmt();
                }
                else if(match(AstToken::T_KWTHROW))
                {
                    parsethrowstmt();
                }
                else if(match(AstToken::T_BRACEOPEN))
                {
                    scopebegin();
                    parseblock();
                    scopeend();
                }
                else if(match(AstToken::T_KWTRY))
                {
                    parsetrystmt();
                }
                else
                {
                    parseexprstmt(false, false);
                }
                ignorewhitespace();
            }

            void consumestmtend()
            {
                /* allow block last statement to omit statement end */
                if(m_blockcount > 0 && check(AstToken::T_BRACECLOSE))
                {
                    return;
                }
                if(match(AstToken::T_SEMICOLON))
                {
                    while(match(AstToken::T_SEMICOLON) || match(AstToken::T_NEWLINE))
                    {
                    }
                    return;
                }
                if(match(AstToken::T_EOF) || m_prevtoken.m_toktype == AstToken::T_EOF)
                {
                    return;
                }
                /* consume(AstToken::T_NEWLINE, "end of statement expected"); */
                while(match(AstToken::T_SEMICOLON) || match(AstToken::T_NEWLINE))
                {
                }
            }

            void ignorewhitespace()
            {
                while(true)
                {
                    if(check(AstToken::T_NEWLINE))
                    {
                        advance();
                    }
                    else
                    {
                        break;
                    }
                }
            }

            int getcodeargscount(const Instruction* bytecode, const Value* constants, int ip)
            {
                int constant;
                Instruction::OpCode code;
                Function* fn;
                code = (Instruction::OpCode)bytecode[ip].code;
                switch(code)
                {
                    case Instruction::OPC_EQUAL:
                    case Instruction::OPC_PRIMGREATER:
                    case Instruction::OPC_PRIMLESSTHAN:
                    case Instruction::OPC_PUSHNULL:
                    case Instruction::OPC_PUSHTRUE:
                    case Instruction::OPC_PUSHFALSE:
                    case Instruction::OPC_PRIMADD:
                    case Instruction::OPC_PRIMSUBTRACT:
                    case Instruction::OPC_PRIMMULTIPLY:
                    case Instruction::OPC_PRIMDIVIDE:
                    case Instruction::OPC_PRIMMODULO:
                    case Instruction::OPC_PRIMPOW:
                    case Instruction::OPC_PRIMNEGATE:
                    case Instruction::OPC_PRIMNOT:
                    case Instruction::OPC_ECHO:
                    case Instruction::OPC_TYPEOF:
                    case Instruction::OPC_POPONE:
                    case Instruction::OPC_UPVALUECLOSE:
                    case Instruction::OPC_DUPONE:
                    case Instruction::OPC_RETURN:
                    case Instruction::OPC_CLASSINHERIT:
                    case Instruction::OPC_CLASSGETSUPER:
                    case Instruction::OPC_PRIMAND:
                    case Instruction::OPC_PRIMOR:
                    case Instruction::OPC_PRIMBITXOR:
                    case Instruction::OPC_PRIMSHIFTLEFT:
                    case Instruction::OPC_PRIMSHIFTRIGHT:
                    case Instruction::OPC_PRIMBITNOT:
                    case Instruction::OPC_PUSHONE:
                    case Instruction::OPC_INDEXSET:
                    case Instruction::OPC_ASSERT:
                    case Instruction::OPC_EXTHROW:
                    case Instruction::OPC_EXPOPTRY:
                    case Instruction::OPC_MAKERANGE:
                    case Instruction::OPC_STRINGIFY:
                    case Instruction::OPC_PUSHEMPTY:
                    case Instruction::OPC_EXPUBLISHTRY:
                    case Instruction::OPC_CLASSGETTHIS:
                    case Instruction::OPC_HALT:
                        return 0;
                    case Instruction::OPC_CALLFUNCTION:
                    case Instruction::OPC_CLASSINVOKESUPERSELF:
                    case Instruction::OPC_INDEXGET:
                    case Instruction::OPC_INDEXGETRANGED:
                        return 1;
                    case Instruction::OPC_GLOBALDEFINE:
                    case Instruction::OPC_GLOBALGET:
                    case Instruction::OPC_GLOBALSET:
                    case Instruction::OPC_LOCALGET:
                    case Instruction::OPC_LOCALSET:
                    case Instruction::OPC_FUNCARGOPTIONAL:
                    case Instruction::OPC_FUNCARGSET:
                    case Instruction::OPC_FUNCARGGET:
                    case Instruction::OPC_UPVALUEGET:
                    case Instruction::OPC_UPVALUESET:
                    case Instruction::OPC_JUMPIFFALSE:
                    case Instruction::OPC_JUMPNOW:
                    case Instruction::OPC_BREAK_PL:
                    case Instruction::OPC_LOOP:
                    case Instruction::OPC_PUSHCONSTANT:
                    case Instruction::OPC_POPN:
                    case Instruction::OPC_MAKECLASS:
                    case Instruction::OPC_PROPERTYGET:
                    case Instruction::OPC_PROPERTYGETSELF:
                    case Instruction::OPC_PROPERTYSET:
                    case Instruction::OPC_MAKEARRAY:
                    case Instruction::OPC_MAKEDICT:
                    case Instruction::OPC_SWITCH:
                    case Instruction::OPC_MAKEMETHOD:
            #if 0
                    case Instruction::OPC_FUNCOPTARG:
            #endif
                        return 2;
                    case Instruction::OPC_CALLMETHOD:
                    case Instruction::OPC_CLASSINVOKETHIS:
                    case Instruction::OPC_CLASSINVOKESUPER:
                    case Instruction::OPC_CLASSPROPERTYDEFINE:
                        return 3;
                    case Instruction::OPC_EXTRY:
                        return 6;
                    case Instruction::OPC_MAKECLOSURE:
                    {
                        constant = (bytecode[ip + 1].code << 8) | bytecode[ip + 2].code;
                        fn = constants[constant].asFunction();
                        /* There is two byte for the constant, then three for each up value. */
                        return 2 + (fn->m_upvalcount * 3);
                    }
                    break;
                    default:
                        break;
                }
                return 0;
            }

            void emit(uint16_t byte, int line, bool isop)
            {
                Instruction ins;
                ins.code = byte;
                ins.fromsourceline = line;
                ins.isop = isop;
                currentblob()->push(ins);
            }

            void patchat(size_t idx, uint16_t byte)
            {
                currentblob()->m_instrucs[idx].code = byte;
            }

            void emitinstruc(uint16_t byte)
            {
                emit(byte, m_prevtoken.m_line, true);
            }

            void emit1byte(uint16_t byte)
            {
                emit(byte, m_prevtoken.m_line, false);
            }

            void emit1short(uint16_t byte)
            {
                emit((byte >> 8) & 0xff, m_prevtoken.m_line, false);
                emit(byte & 0xff, m_prevtoken.m_line, false);
            }

            void emit2byte(uint16_t byte, uint16_t byte2)
            {
                emit(byte, m_prevtoken.m_line, false);
                emit(byte2, m_prevtoken.m_line, false);
            }

            void emitbyteandshort(uint16_t byte, uint16_t byte2)
            {
                emit(byte, m_prevtoken.m_line, false);
                emit((byte2 >> 8) & 0xff, m_prevtoken.m_line, false);
                emit(byte2 & 0xff, m_prevtoken.m_line, false);
            }

            void emitloop(int loopstart)
            {
                int offset;
                emitinstruc(Instruction::OPC_LOOP);
                offset = currentblob()->m_count - loopstart + 2;
                if(offset > UINT16_MAX)
                {
                    raiseerror("loop body too large");
                }
                emit1byte((offset >> 8) & 0xff);
                emit1byte(offset & 0xff);
            }

            void emitreturn()
            {
                if(m_istrying)
                {
                    emitinstruc(Instruction::OPC_EXPOPTRY);
                }
                if(m_currentfunccompiler->m_contexttype == Function::CTXTYPE_INITIALIZER)
                {
                    emitbyteandshort(Instruction::OPC_LOCALGET, 0);
                }
                else
                {
                    if(!m_keeplastvalue || m_lastwasstatement)
                    {
                        emitinstruc(Instruction::OPC_PUSHEMPTY);
                    }
                }
                emitinstruc(Instruction::OPC_RETURN);
            }

            int pushconst(Value value)
            {
                int constant;
                constant = currentblob()->addConstant(value);
                return constant;
            }

            void emitconst(Value value)
            {
                int constant;
                constant = pushconst(value);
                emitbyteandshort(Instruction::OPC_PUSHCONSTANT, (uint16_t)constant);
            }

            int emitjump(uint16_t instruction)
            {
                emitinstruc(instruction);
                /* placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                return currentblob()->m_count - 2;
            }

            int emitswitch()
            {
                emitinstruc(Instruction::OPC_SWITCH);
                /* placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                return currentblob()->m_count - 2;
            }

            int emittry()
            {
                emitinstruc(Instruction::OPC_EXTRY);
                /* type placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                /* handler placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                /* finally placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                return currentblob()->m_count - 6;
            }

            void patchswitch(int offset, int constant)
            {
                patchat(offset, (constant >> 8) & 0xff);
                patchat(offset + 1, constant & 0xff);
            }

            void patchtry(int offset, int type, int address, int finally)
            {
                /* patch type */
                patchat(offset, (type >> 8) & 0xff);
                patchat(offset + 1, type & 0xff);
                /* patch address */
                patchat(offset + 2, (address >> 8) & 0xff);
                patchat(offset + 3, address & 0xff);
                /* patch finally */
                patchat(offset + 4, (finally >> 8) & 0xff);
                patchat(offset + 5, finally & 0xff);
            }

            void patchjump(int offset)
            {
                /* -2 to adjust the bytecode for the offset itself */
                int jump;
                jump = currentblob()->m_count - offset - 2;
                if(jump > UINT16_MAX)
                {
                    raiseerror("body of conditional block too large");
                }
                patchat(offset, (jump >> 8) & 0xff);
                patchat(offset + 1, jump & 0xff);
            }

            int makeidentconst(AstToken* name)
            {
                int rawlen;
                const char* rawstr;
                String* str;
                rawstr = name->m_start;
                rawlen = name->length;
                if(name->isglobal)
                {
                    rawstr++;
                    rawlen--;
                }
            #if 0
                if(strcmp(rawstr, g_strthis))
                {
                    
                }
            #endif
                str = String::copy(rawstr, rawlen);
                return pushconst(Value::fromObject(str));
            }

            int addlocal(AstToken name)
            {
                LocalVarInfo* local;
                if(m_currentfunccompiler->m_localcount == CONF_MAXLOCALS)
                {
                    /* we've reached maximum local variables per scope */
                    raiseerror("too many local variables in scope");
                    return -1;
                }
                local = &m_currentfunccompiler->m_locals[m_currentfunccompiler->m_localcount++];
                local->m_localname = name;
                local->depth = -1;
                local->iscaptured = false;
                return m_currentfunccompiler->m_localcount;
            }

            void declarevariable()
            {
                int i;
                AstToken* name;
                LocalVarInfo* local;
                /* global variables are implicitly declared... */
                if(m_currentfunccompiler->m_scopedepth == 0)
                {
                    return;
                }
                name = &m_prevtoken;
                for(i = m_currentfunccompiler->m_localcount - 1; i >= 0; i--)
                {
                    local = &m_currentfunccompiler->m_locals[i];
                    if(local->depth != -1 && local->depth < m_currentfunccompiler->m_scopedepth)
                    {
                        break;
                    }
                    if(utilIdentsEqual(name, &local->m_localname))
                    {
                        raiseerror("%.*s already declared in current scope", name->length, name->m_start);
                    }
                }
                addlocal(*name);
            }

            int parsevariable(const char* message)
            {
                if(!consume(AstToken::T_IDENTNORMAL, message))
                {
                    /* what to do here? */
                }
                declarevariable();
                /* we are in a local scope... */
                if(m_currentfunccompiler->m_scopedepth > 0)
                {
                    return 0;
                }
                return makeidentconst(&m_prevtoken);
            }

            void markinitialized()
            {
                if(m_currentfunccompiler->m_scopedepth == 0)
                {
                    return;
                }
                m_currentfunccompiler->m_locals[m_currentfunccompiler->m_localcount - 1].depth = m_currentfunccompiler->m_scopedepth;
            }

            void definevariable(int global)
            {
                /* we are in a local scope... */
                if(m_currentfunccompiler->m_scopedepth > 0)
                {
                    markinitialized();
                    return;
                }
                emitbyteandshort(Instruction::OPC_GLOBALDEFINE, global);
            }

            Function* endcompiler(bool istoplevel)
            {
                emitreturn();
                if(istoplevel)
                {
                }
                auto function = m_currentfunccompiler->m_targetfunc;
                m_currentfunccompiler = m_currentfunccompiler->m_enclosing;
                return function;
            }

            void scopebegin()
            {
                m_currentfunccompiler->m_scopedepth++;
            }

            bool scopeCanEndContinue()
            {
                int lopos;
                int locount;
                int lodepth;
                int scodepth;
                locount = m_currentfunccompiler->m_localcount;
                lopos = m_currentfunccompiler->m_localcount - 1;
                lodepth = m_currentfunccompiler->m_locals[lopos].depth;
                scodepth = m_currentfunccompiler->m_scopedepth;
                if(locount > 0 && lodepth > scodepth)
                {
                    return true;
                }
                return false;
            }

            void scopeend()
            {
                m_currentfunccompiler->m_scopedepth--;
                /*
                // remove all variables declared in scope while exiting...
                */
                if(m_keeplastvalue)
                {
            #if 0
                        return;
            #endif
                }
                while(scopeCanEndContinue())
                {
                    if(m_currentfunccompiler->m_locals[m_currentfunccompiler->m_localcount - 1].iscaptured)
                    {
                        emitinstruc(Instruction::OPC_UPVALUECLOSE);
                    }
                    else
                    {
                        emitinstruc(Instruction::OPC_POPONE);
                    }
                    m_currentfunccompiler->m_localcount--;
                }
            }

            int discardlocals(int depth)
            {
                int local;
                if(m_keeplastvalue)
                {
            #if 0
                        return 0;
            #endif
                }
                if(m_currentfunccompiler->m_scopedepth == -1)
                {
                    raiseerror("cannot exit top-level scope");
                }
                local = m_currentfunccompiler->m_localcount - 1;
                while(local >= 0 && m_currentfunccompiler->m_locals[local].depth >= depth)
                {
                    if(m_currentfunccompiler->m_locals[local].iscaptured)
                    {
                        emitinstruc(Instruction::OPC_UPVALUECLOSE);
                    }
                    else
                    {
                        emitinstruc(Instruction::OPC_POPONE);
                    }
                    local--;
                }
                return m_currentfunccompiler->m_localcount - local - 1;
            }

            void endloop()
            {
                int i;
                Instruction* bcode;
                Value* cvals;
                /*
                // find all Instruction::OPC_BREAK_PL placeholder and replace with the appropriate jump...
                */
                i = m_innermostloopstart;
                while(i < m_currentfunccompiler->m_targetfunc->m_fnvals.fnscriptfunc.blob->m_count)
                {
                    if(m_currentfunccompiler->m_targetfunc->m_fnvals.fnscriptfunc.blob->m_instrucs[i].code == Instruction::OPC_BREAK_PL)
                    {
                        m_currentfunccompiler->m_targetfunc->m_fnvals.fnscriptfunc.blob->m_instrucs[i].code = Instruction::OPC_JUMPNOW;
                        patchjump(i + 1);
                        i += 3;
                    }
                    else
                    {
                        bcode = m_currentfunccompiler->m_targetfunc->m_fnvals.fnscriptfunc.blob->m_instrucs.data();
                        cvals = (Value*)m_currentfunccompiler->m_targetfunc->m_fnvals.fnscriptfunc.blob->m_constants.data();
                        i += 1 + getcodeargscount(bcode, cvals, i);
                    }
                }
            }

            void parseassign(uint16_t realop, uint16_t getop, uint16_t setop, int arg)
            {
                m_replcanecho = false;
                if(getop == Instruction::OPC_PROPERTYGET || getop == Instruction::OPC_PROPERTYGETSELF)
                {
                    emitinstruc(Instruction::OPC_DUPONE);
                }
                if(arg != -1)
                {
                    emitbyteandshort(getop, arg);
                }
                else
                {
                    emit2byte(getop, 1);
                }
                parseexpression();
                emitinstruc(realop);
                if(arg != -1)
                {
                    emitbyteandshort(setop, (uint16_t)arg);
                }
                else
                {
                    emitinstruc(setop);
                }
            }

            void assignment(uint16_t getop, uint16_t setop, int arg, bool canassign)
            {
                if(canassign && match(AstToken::T_ASSIGN))
                {
                    m_replcanecho = false;
                    parseexpression();
                    if(arg != -1)
                    {
                        emitbyteandshort(setop, (uint16_t)arg);
                    }
                    else
                    {
                        emitinstruc(setop);
                    }
                }
                else if(canassign && match(AstToken::T_PLUSASSIGN))
                {
                    parseassign(Instruction::OPC_PRIMADD, getop, setop, arg);
                }
                else if(canassign && match(AstToken::T_MINUSASSIGN))
                {
                    parseassign(Instruction::OPC_PRIMSUBTRACT, getop, setop, arg);
                }
                else if(canassign && match(AstToken::T_MULTASSIGN))
                {
                    parseassign(Instruction::OPC_PRIMMULTIPLY, getop, setop, arg);
                }
                else if(canassign && match(AstToken::T_DIVASSIGN))
                {
                    parseassign(Instruction::OPC_PRIMDIVIDE, getop, setop, arg);
                }
                else if(canassign && match(AstToken::T_POWASSIGN))
                {
                    parseassign(Instruction::OPC_PRIMPOW, getop, setop, arg);
                }
                else if(canassign && match(AstToken::T_PERCENT_EQ))
                {
                    parseassign(Instruction::OPC_PRIMMODULO, getop, setop, arg);
                }
                else if(canassign && match(AstToken::T_AMP_EQ))
                {
                    parseassign(Instruction::OPC_PRIMAND, getop, setop, arg);
                }
                else if(canassign && match(AstToken::T_BAR_EQ))
                {
                    parseassign(Instruction::OPC_PRIMOR, getop, setop, arg);
                }
                else if(canassign && match(AstToken::T_TILDE_EQ))
                {
                    parseassign(Instruction::OPC_PRIMBITNOT, getop, setop, arg);
                }
                else if(canassign && match(AstToken::T_XOR_EQ))
                {
                    parseassign(Instruction::OPC_PRIMBITXOR, getop, setop, arg);
                }
                else if(canassign && match(AstToken::T_LEFTSHIFTASSIGN))
                {
                    parseassign(Instruction::OPC_PRIMSHIFTLEFT, getop, setop, arg);
                }
                else if(canassign && match(AstToken::T_RIGHTSHIFTASSIGN))
                {
                    parseassign(Instruction::OPC_PRIMSHIFTRIGHT, getop, setop, arg);
                }
                else if(canassign && match(AstToken::T_INCREMENT))
                {
                    m_replcanecho = false;
                    if(getop == Instruction::OPC_PROPERTYGET || getop == Instruction::OPC_PROPERTYGETSELF)
                    {
                        emitinstruc(Instruction::OPC_DUPONE);
                    }
                    if(arg != -1)
                    {
                        emitbyteandshort(getop, arg);
                    }
                    else
                    {
                        emit2byte(getop, 1);
                    }
                    emit2byte(Instruction::OPC_PUSHONE, Instruction::OPC_PRIMADD);
                    emitbyteandshort(setop, (uint16_t)arg);
                }
                else if(canassign && match(AstToken::T_DECREMENT))
                {
                    m_replcanecho = false;
                    if(getop == Instruction::OPC_PROPERTYGET || getop == Instruction::OPC_PROPERTYGETSELF)
                    {
                        emitinstruc(Instruction::OPC_DUPONE);
                    }

                    if(arg != -1)
                    {
                        emitbyteandshort(getop, arg);
                    }
                    else
                    {
                        emit2byte(getop, 1);
                    }

                    emit2byte(Instruction::OPC_PUSHONE, Instruction::OPC_PRIMSUBTRACT);
                    emitbyteandshort(setop, (uint16_t)arg);
                }
                else
                {
                    if(arg != -1)
                    {
                        if(getop == Instruction::OPC_INDEXGET || getop == Instruction::OPC_INDEXGETRANGED)
                        {
                            emit2byte(getop, (uint16_t)0);
                        }
                        else
                        {
                            emitbyteandshort(getop, (uint16_t)arg);
                        }
                    }
                    else
                    {
                        emit2byte(getop, (uint16_t)0);
                    }
                }
            }

            void namedvar(AstToken name, bool canassign)
            {
                bool fromclass;
                uint16_t getop;
                uint16_t setop;
                int arg;
                (void)fromclass;
                fromclass = m_currentclasscompiler != nullptr;
                arg = m_currentfunccompiler->resolveLocal(&name);
                if(arg != -1)
                {
                    if(m_infunction)
                    {
                        getop = Instruction::OPC_FUNCARGGET;
                        setop = Instruction::OPC_FUNCARGSET;
                    }
                    else
                    {
                        getop = Instruction::OPC_LOCALGET;
                        setop = Instruction::OPC_LOCALSET;
                    }
                }
                else
                {
                    arg = m_currentfunccompiler->resolveUpvalue(&name);
                    if((arg != -1) && (name.isglobal == false))
                    {
                        getop = Instruction::OPC_UPVALUEGET;
                        setop = Instruction::OPC_UPVALUESET;
                    }
                    else
                    {
                        arg = makeidentconst(&name);
                        getop = Instruction::OPC_GLOBALGET;
                        setop = Instruction::OPC_GLOBALSET;
                    }
                }
                assignment(getop, setop, arg, canassign);
            }

            void createdvar(AstToken name)
            {
                int local;
                if(m_currentfunccompiler->m_targetfunc->m_funcname != nullptr)
                {
                    local = addlocal(name) - 1;
                    markinitialized();
                    emitbyteandshort(Instruction::OPC_LOCALSET, (uint16_t)local);
                }
                else
                {
                    emitbyteandshort(Instruction::OPC_GLOBALDEFINE, (uint16_t)makeidentconst(&name));
                }
            }

            Value compilenumber()
            {
                return utilConvertNumberString(m_prevtoken.m_toktype, m_prevtoken.m_start);
            }

            /*
            // Reads the next character, which should be a hex digit (0-9, a-f, or A-F) and
            // returns its numeric value. If the character isn't a hex digit, returns -1.
            */
            int readhexdigit(char c)
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
            int readhexescape(const char* str, int index, int count)
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
                    digit = readhexdigit(cval);
                    if(digit == -1)
                    {
                        raiseerror("invalid hex escape sequence at #%d of \"%s\": '%c' (%d)", pos, str, cval, cval);
                    }
                    value = (value * 16) | digit;
                }
                if(count == 4 && (digit = readhexdigit(str[index + i + 2])) != -1)
                {
                    value = (value * 16) | digit;
                }
                return value;
            }

            int readunicodeescape(char* string, const char* realstring, int numberbytes, int realindex, int index)
            {
                int value;
                int count;
                size_t len;
                char* chr;
                value = readhexescape(realstring, realindex, numberbytes);
                count = Util::utf8NumBytes(value);
                if(count == -1)
                {
                    raiseerror("cannot encode a negative unicode value");
                }
                /* check for greater that \uffff */
                if(value > 65535)
                {
                    count++;
                }
                if(count != 0)
                {
                    chr = Util::utf8Encode(value, &len);
                    if(chr)
                    {
                        memcpy(string + index, chr, (size_t)count + 1);
                        Memory::sysFree(chr);
                    }
                    else
                    {
                        raiseerror("cannot decode unicode escape at index %d", realindex);
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

            char* compilestring(int* length, bool permitescapes)
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
                deststr = (char*)Memory::sysMalloc(sizeof(char) * rawlen);
                quote = m_prevtoken.m_start[0];
                realstr = (char*)m_prevtoken.m_start + 1;
                reallength = m_prevtoken.length - 2;
                k = 0;
                for(i = 0; i < reallength; i++, k++)
                {
                    c = realstr[i];
                    if(permitescapes)
                    {
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
            #if 0
                                            int readunicodeescape(AstParser* this, char* string, char* realstring, int numberbytes, int realindex, int index)
                                            int readhexescape(AstParser* this, const char* str, int index, int count)
                                            k += readunicodeescape(deststr, realstr, 2, i, k) - 1;
                                            k += readhexescape(deststr, i, 2) - 0;
            #endif
                                    c = readhexescape(realstr, i, 2) - 0;
                                    i += 2;
            #if 0
                                            continue;
            #endif
                                }
                                break;
                                case 'u':
                                {
                                    count = readunicodeescape(deststr, realstr, 4, i, k);
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
                                    count = readunicodeescape(deststr, realstr, 8, i, k);
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
                    }
                    memcpy(deststr + k, &c, 1);
                }
                *length = k;
                deststr[k] = '\0';
                return deststr;
            }

            bool doparseprecedence(Rule::Precedence precedence /*, AstExpression* dest*/)
            {
                bool canassign;
                Rule* rule;
                AstToken previous;
                Rule::ParseInfixFN infixrule;
                Rule::ParsePrefixFN prefixrule;
                rule = getRule(m_prevtoken.m_toktype);
                if(rule == nullptr)
                {
                    return false;
                }
                prefixrule = rule->prefix;
                if(prefixrule == nullptr)
                {
                    raiseerror("expected expression");
                    return false;
                }
                canassign = precedence <= Rule::PREC_ASSIGNMENT;
                prefixrule(this, canassign);
                while(true)
                {
                    rule = getRule(m_currtoken.m_toktype);
                    if(rule == nullptr)
                    {
                        return false;
                    }
                    if(precedence <= rule->precedence)
                    {
                        previous = m_prevtoken;
                        ignorewhitespace();
                        advance();
                        infixrule = getRule(m_prevtoken.m_toktype)->infix;
                        infixrule(this, previous, canassign);
                    }
                    else
                    {
                        break;
                    }
                }
                if(canassign && match(AstToken::T_ASSIGN))
                {
                    raiseerror("invalid assignment target");
                    return false;
                }
                return true;
            }

            bool parseprecedence(Rule::Precedence precedence)
            {
                auto gcs = SharedState::get();
                if(m_lexer->isatend() && gcs->m_isrepl)
                {
                    return false;
                }
                ignorewhitespace();
                if(m_lexer->isatend() && gcs->m_isrepl)
                {
                    return false;
                }
                advance();
                return doparseprecedence(precedence);
            }

            bool parseprecnoadvance(Rule::Precedence precedence)
            {
                auto gcs = SharedState::get();
                if(m_lexer->isatend() && gcs->m_isrepl)
                {
                    return false;
                }
                ignorewhitespace();
                if(m_lexer->isatend() && gcs->m_isrepl)
                {
                    return false;
                }
                return doparseprecedence(precedence);
            }

            bool parseexpression()
            {
                return parseprecedence(Rule::PREC_ASSIGNMENT);
            }

            bool parseblock()
            {
                m_blockcount++;
                ignorewhitespace();
                while(!check(AstToken::T_BRACECLOSE) && !check(AstToken::T_EOF))
                {
                    parsedeclaration();
                }
                m_blockcount--;
                if(!consume(AstToken::T_BRACECLOSE, "expected '}' after block"))
                {
                    return false;
                }
                if(match(AstToken::T_SEMICOLON))
                {
                }
                return true;
            }

            void declarefuncargvar()
            {
                int i;
                AstToken* name;
                LocalVarInfo* local;
                /* global variables are implicitly declared... */
                if(m_currentfunccompiler->m_scopedepth == 0)
                {
                    return;
                }
                name = &m_prevtoken;
                for(i = m_currentfunccompiler->m_localcount - 1; i >= 0; i--)
                {
                    local = &m_currentfunccompiler->m_locals[i];
                    if(local->depth != -1 && local->depth < m_currentfunccompiler->m_scopedepth)
                    {
                        break;
                    }
                    if(utilIdentsEqual(name, &local->m_localname))
                    {
                        raiseerror("%.*s already declared in current scope", name->length, name->m_start);
                    }
                }
                addlocal(*name);
            }

            int parsefuncparamvar(const char* message)
            {
                if(!consume(AstToken::T_IDENTNORMAL, message))
                {
                    /* what to do here? */
                }
                declarefuncargvar();
                /* we are in a local scope... */
                if(m_currentfunccompiler->m_scopedepth > 0)
                {
                    return 0;
                }
                return makeidentconst(&m_prevtoken);
            }

            uint32_t parsefunccallargs()
            {
                uint16_t argcount;
                argcount = 0;
                if(!check(AstToken::T_PARENCLOSE))
                {
                    do
                    {
                        ignorewhitespace();
                        parseexpression();
                        if(argcount == CONF_MAXFUNCPARAMS)
                        {
                            raiseerror("cannot have more than %d arguments to a function", CONF_MAXFUNCPARAMS);
                        }
                        argcount++;
                    } while(match(AstToken::T_COMMA));
                }
                ignorewhitespace();
                if(!consume(AstToken::T_PARENCLOSE, "expected ')' after argument list"))
                {
                    /* TODO: handle this, somehow. */
                }
                return argcount;
            }

            void parsefuncfull(Function::ContextType type, bool isanon)
            {
                m_infunction = true;
                FuncCompiler fnc(this, type, isanon);
                scopebegin();
                /* compile parameter list */
                consume(AstToken::T_PARENOPEN, "expected '(' after function name");
                if(!check(AstToken::T_PARENCLOSE))
                {
                    fnc.parseParamList();
                }
                consume(AstToken::T_PARENCLOSE, "expected ')' after function parameters");
                fnc.compileBody(false, isanon);
                m_infunction = false;
            }

            void parseclassmethod(AstToken classname, AstToken methodname, bool havenametoken, bool isstatic)
            {
                size_t sn;
                int constant;
                const char* sc;
                Function::ContextType type;
                AstToken actualmthname;
                AstToken::Type tkns[] = { AstToken::T_IDENTNORMAL, AstToken::T_DECORATOR };
                (void)classname;
                sc = "constructor";
                sn = strlen(sc);
                if(havenametoken)
                {
                    actualmthname = methodname;
                }
                else
                {
                    consumeor("method name expected", tkns, 2);
                    actualmthname = m_prevtoken;
                }
                constant = makeidentconst(&actualmthname);
                type = Function::CTXTYPE_METHOD;
                if(isstatic)
                {
                    type = Function::CTXTYPE_STATIC;
                }
                if((m_prevtoken.length == (int)sn) && (memcmp(m_prevtoken.m_start, sc, sn) == 0))
                {
                    type = Function::CTXTYPE_INITIALIZER;
                }
                else if((m_prevtoken.length > 0) && (m_prevtoken.m_start[0] == '_'))
                {
                    type = Function::CTXTYPE_PRIVATE;
                }
                parsefuncfull(type, false);
                emitbyteandshort(Instruction::OPC_MAKEMETHOD, constant);
            }

            bool parseclassfield(AstToken* nametokendest, bool* havenamedest, bool isstatic)
            {
                int fieldconstant;
                AstToken fieldname;
                *havenamedest = false;
                if(match(AstToken::T_IDENTNORMAL))
                {
                    fieldname = m_prevtoken;
                    *nametokendest = fieldname;
                    if(check(AstToken::T_ASSIGN))
                    {
                        consume(AstToken::T_ASSIGN, "expected '=' after ident");
                        fieldconstant = makeidentconst(&fieldname);
                        parseexpression();
                        emitbyteandshort(Instruction::OPC_CLASSPROPERTYDEFINE, fieldconstant);
                        emit1byte(isstatic ? 1 : 0);
                        consumestmtend();
                        ignorewhitespace();
                        return true;
                    }
                }
                *havenamedest = true;
                return false;
            }

            void parsefuncdecl()
            {
                int global;
                global = parsevariable("function name expected");
                markinitialized();
                parsefuncfull(Function::CTXTYPE_FUNCTION, false);
                definevariable(global);
            }

            void parseclassdecl(bool named)
            {
                bool isstatic;
                bool havenametoken;
                int nameconst;
                AstToken nametoken;
                CompContext oldctx;
                AstToken classname;
                ClassCompiler classcompiler;
                /*
                            ClassCompiler classcompiler;
                            classcompiler.hasname = named;
                            if(named)
                            {
                                consume(Token::TOK_IDENTNORMAL, "class name expected");
                                classname = m_prevtoken;
                                declareVariable();
                            }
                            else
                            {
                                classname = makeSynthToken("<anonclass>");
                            }
                            nameconst = makeIdentConst(&classname);
                */
                if(named)
                {
                    consume(AstToken::T_IDENTNORMAL, "class name expected");
                    classname = m_prevtoken;
                    declarevariable();
                }
                else
                {
                    classname = utilMakeSynthToken("<anonclass>");
                }
                nameconst = makeidentconst(&classname);
                emitbyteandshort(Instruction::OPC_MAKECLASS, nameconst);
                if(named)
                {
                    definevariable(nameconst);
                }
                classcompiler.m_classcompilername = m_prevtoken;
                classcompiler.hassuperclass = false;
                classcompiler.m_enclosing = m_currentclasscompiler;
                m_currentclasscompiler = &classcompiler;
                oldctx = m_compcontext;
                m_compcontext = COMPCTX_CLASS;
                if(match(AstToken::T_KWEXTENDS))
                {
                    consume(AstToken::T_IDENTNORMAL, "name of superclass expected");
                    astrulevarnormal(this, false);
                    if(utilIdentsEqual(&classname, &m_prevtoken))
                    {
                        raiseerror("class %.*s cannot inherit from itself", classname.length, classname.m_start);
                    }
                    scopebegin();
                    addlocal(utilMakeSynthToken(g_strsuper));
                    definevariable(0);
                    namedvar(classname, false);
                    emitinstruc(Instruction::OPC_CLASSINHERIT);
                    classcompiler.hassuperclass = true;
                }
                if(named)
                {
                    namedvar(classname, false);
                }
                ignorewhitespace();
                consume(AstToken::T_BRACEOPEN, "expected '{' before class body");
                ignorewhitespace();
                while(!check(AstToken::T_BRACECLOSE) && !check(AstToken::T_EOF))
                {
                    isstatic = false;
                    if(match(AstToken::T_KWSTATIC))
                    {
                        isstatic = true;
                    }
                    if(match(AstToken::T_KWVAR))
                    {
                        /*
                         * TODO:
                         * using 'var ... =' in a class is actually semantically superfluous,
                         * but not incorrect either. maybe warn that this syntax is deprecated?
                         */
                    }
                    if(!parseclassfield(&nametoken, &havenametoken, isstatic))
                    {
                        parseclassmethod(classname, nametoken, havenametoken, isstatic);
                    }
                    ignorewhitespace();
                }
                consume(AstToken::T_BRACECLOSE, "expected '}' after class body");
                if(match(AstToken::T_SEMICOLON))
                {
                }
                if(named)
                {
                    emitinstruc(Instruction::OPC_POPONE);
                }
                if(classcompiler.hassuperclass)
                {
                    scopeend();
                }
                m_currentclasscompiler = m_currentclasscompiler->m_enclosing;
                m_compcontext = oldctx;
            }

            void parsevardecl(bool isinitializer, bool isconst)
            {
                int global;
                int totalparsed;
                (void)isconst;
                totalparsed = 0;
                do
                {
                    if(totalparsed > 0)
                    {
                        ignorewhitespace();
                    }
                    global = parsevariable("variable name expected");
                    if(match(AstToken::T_ASSIGN))
                    {
                        parseexpression();
                    }
                    else
                    {
                        emitinstruc(Instruction::OPC_PUSHNULL);
                    }
                    definevariable(global);
                    totalparsed++;
                } while(match(AstToken::T_COMMA));
                if(!isinitializer)
                {
                    consumestmtend();
                }
                else
                {
                    consume(AstToken::T_SEMICOLON, "expected ';' after initializer");
                    ignorewhitespace();
                }
            }

            void parseexprstmt(bool isinitializer, bool semi)
            {
                auto gcs = SharedState::get();
                if(gcs->m_isrepl && m_currentfunccompiler->m_scopedepth == 0)
                {
                    m_replcanecho = true;
                }
                if(!semi)
                {
                    parseexpression();
                }
                else
                {
                    parseprecnoadvance(Rule::PREC_ASSIGNMENT);
                }
                if(!isinitializer)
                {
                    if(m_replcanecho && gcs->m_isrepl)
                    {
                        emitinstruc(Instruction::OPC_ECHO);
                        m_replcanecho = false;
                    }
                    else
                    {
                        #if 0
                        if(!m_keeplastvalue)
                        #endif
                        {
                            emitinstruc(Instruction::OPC_POPONE);
                        }
                    }
                    consumestmtend();
                }
                else
                {
                    consume(AstToken::T_SEMICOLON, "expected ';' after initializer");
                    ignorewhitespace();
                    emitinstruc(Instruction::OPC_POPONE);
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
            void parseforstmt()
            {
                int exitjump;
                int bodyjump;
                int incrstart;
                int surroundingloopstart;
                int surroundingscopedepth;
                scopebegin();
                consume(AstToken::T_PARENOPEN, "expected '(' after 'for'");
                /* parse initializer... */
                if(match(AstToken::T_SEMICOLON))
                {
                    /* no initializer */
                }
                else if(match(AstToken::T_KWVAR))
                {
                    parsevardecl(true, false);
                }
                else
                {
                    parseexprstmt(true, false);
                }
                /* keep a copy of the surrounding loop's start and depth */
                surroundingloopstart = m_innermostloopstart;
                surroundingscopedepth = m_innermostloopscopedepth;
                /* update the this's loop start and depth to the current */
                m_innermostloopstart = currentblob()->m_count;
                m_innermostloopscopedepth = m_currentfunccompiler->m_scopedepth;
                exitjump = -1;
                if(!match(AstToken::T_SEMICOLON))
                {
                    /* the condition is optional */
                    parseexpression();
                    consume(AstToken::T_SEMICOLON, "expected ';' after condition");
                    ignorewhitespace();
                    /* jump out of the loop if the condition is false... */
                    exitjump = emitjump(Instruction::OPC_JUMPIFFALSE);
                    emitinstruc(Instruction::OPC_POPONE);
                    /* pop the condition */
                }
                /* the iterator... */
                if(!check(AstToken::T_BRACEOPEN))
                {
                    bodyjump = emitjump(Instruction::OPC_JUMPNOW);
                    incrstart = currentblob()->m_count;
                    parseexpression();
                    ignorewhitespace();
                    emitinstruc(Instruction::OPC_POPONE);
                    emitloop(m_innermostloopstart);
                    m_innermostloopstart = incrstart;
                    patchjump(bodyjump);
                }
                consume(AstToken::T_PARENCLOSE, "expected ')' after 'for'");
                parsestmt();
                emitloop(m_innermostloopstart);
                if(exitjump != -1)
                {
                    patchjump(exitjump);
                    emitinstruc(Instruction::OPC_POPONE);
                }
                endloop();
                /* reset the loop start and scope depth to the surrounding value */
                m_innermostloopstart = surroundingloopstart;
                m_innermostloopscopedepth = surroundingscopedepth;
                scopeend();
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
            void parseforeachstmt()
            {
                int citer;
                int citern;
                int falsejump;
                int keyslot;
                int valueslot;
                int iteratorslot;
                int surroundingloopstart;
                int surroundingscopedepth;
                AstToken iteratortoken;
                AstToken keytoken;
                AstToken valuetoken;
                scopebegin();
                /* define @iter and @itern constant */
                citer = pushconst(Value::fromObject(String::intern("@iter")));
                citern = pushconst(Value::fromObject(String::intern("@itern")));
                consume(AstToken::T_PARENOPEN, "expected '(' after 'foreach'");
                consume(AstToken::T_IDENTNORMAL, "expected variable name after 'foreach'");
                if(!check(AstToken::T_COMMA))
                {
                    keytoken = utilMakeSynthToken(" _ ");
                    valuetoken = m_prevtoken;
                }
                else
                {
                    keytoken = m_prevtoken;
                    consume(AstToken::T_COMMA, "");
                    consume(AstToken::T_IDENTNORMAL, "expected variable name after ','");
                    valuetoken = m_prevtoken;
                }
                consume(AstToken::T_KWIN, "expected 'in' after for loop variable(s)");
                ignorewhitespace();
                /*
                // The space in the variable name ensures it won't collide with a user-defined
                // variable.
                */
                iteratortoken = utilMakeSynthToken(" iterator ");
                /* Evaluate the sequence expression and store it in a hidden local variable. */
                parseexpression();
                consume(AstToken::T_PARENCLOSE, "expected ')' after 'foreach'");
                if(m_currentfunccompiler->m_localcount + 3 > CONF_MAXLOCALS)
                {
                    raiseerror("cannot declare more than %d variables in one scope", CONF_MAXLOCALS);
                    return;
                }
                /* add the iterator to the local scope */
                iteratorslot = addlocal(iteratortoken) - 1;
                definevariable(0);
                /* Create the key local variable. */
                emitinstruc(Instruction::OPC_PUSHNULL);
                keyslot = addlocal(keytoken) - 1;
                definevariable(keyslot);
                /* create the local value slot */
                emitinstruc(Instruction::OPC_PUSHNULL);
                valueslot = addlocal(valuetoken) - 1;
                definevariable(0);
                surroundingloopstart = m_innermostloopstart;
                surroundingscopedepth = m_innermostloopscopedepth;
                /*
                // we'll be jumping back to right before the
                // expression after the loop body
                */
                m_innermostloopstart = currentblob()->m_count;
                m_innermostloopscopedepth = m_currentfunccompiler->m_scopedepth;
                /* key = iterable.iter_n__(key) */
                emitbyteandshort(Instruction::OPC_LOCALGET, iteratorslot);
                emitbyteandshort(Instruction::OPC_LOCALGET, keyslot);
                emitbyteandshort(Instruction::OPC_CALLMETHOD, citern);
                emit1byte(1);
                emitbyteandshort(Instruction::OPC_LOCALSET, keyslot);
                falsejump = emitjump(Instruction::OPC_JUMPIFFALSE);
                emitinstruc(Instruction::OPC_POPONE);
                /* value = iterable.iter__(key) */
                emitbyteandshort(Instruction::OPC_LOCALGET, iteratorslot);
                emitbyteandshort(Instruction::OPC_LOCALGET, keyslot);
                emitbyteandshort(Instruction::OPC_CALLMETHOD, citer);
                emit1byte(1);
                /*
                // Bind the loop value in its own scope. This ensures we get a fresh
                // variable each iteration so that closures for it don't all see the same one.
                */
                scopebegin();
                /* update the value */
                emitbyteandshort(Instruction::OPC_LOCALSET, valueslot);
                emitinstruc(Instruction::OPC_POPONE);
                parsestmt();
                scopeend();
                emitloop(m_innermostloopstart);
                patchjump(falsejump);
                emitinstruc(Instruction::OPC_POPONE);
                endloop();
                m_innermostloopstart = surroundingloopstart;
                m_innermostloopscopedepth = surroundingscopedepth;
                scopeend();
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
            void parseswitchstmt()
            {
                int i;
                int length;
                int swstate;
                int casecount;
                int switchcode;
                int startoffset;
                int caseends[CONF_MAXSWITCHES];
                char* str;
                Value jump;
                AstToken::Type casetype;
                Switch* sw;
                String* string;
                auto gcs = SharedState::get();
                /* the expression */
                consume(AstToken::T_PARENOPEN, "expected '(' before 'switch'");

                parseexpression();
                consume(AstToken::T_PARENCLOSE, "expected ')' after 'switch'");
                ignorewhitespace();
                consume(AstToken::T_BRACEOPEN, "expected '{' after 'switch' expression");
                ignorewhitespace();
                /* 0: before all cases, 1: before default, 2: after default */
                swstate = 0;
                casecount = 0;
                sw = Switch::make();
                gcs->vmStackPush(Value::fromObject(sw));
                switchcode = emitswitch();
                /* emitbyteandshort(Instruction::OPC_SWITCH, pushconst(Value::fromObject(sw))); */
                startoffset = currentblob()->m_count;
                m_inswitch = true;
                while(!match(AstToken::T_BRACECLOSE) && !check(AstToken::T_EOF))
                {
                    if(match(AstToken::T_KWCASE) || match(AstToken::T_KWDEFAULT))
                    {
                        casetype = m_prevtoken.m_toktype;
                        if(swstate == 2)
                        {
                            raiseerror("cannot have another case after a default case");
                        }
                        if(swstate == 1)
                        {
                            /* at the end of the previous case, jump over the others... */
                            caseends[casecount++] = emitjump(Instruction::OPC_JUMPNOW);
                        }
                        if(casetype == AstToken::T_KWCASE)
                        {
                            swstate = 1;
                            do
                            {
                                ignorewhitespace();
                                advance();
                                jump = Value::makeNumber((double)currentblob()->m_count - (double)startoffset);
                                if(m_prevtoken.m_toktype == AstToken::T_KWTRUE)
                                {
                                    sw->m_table.set(Value::makeBool(true), jump);
                                }
                                else if(m_prevtoken.m_toktype == AstToken::T_KWFALSE)
                                {
                                    sw->m_table.set(Value::makeBool(false), jump);
                                }
                                else if(m_prevtoken.m_toktype == AstToken::T_LITERALSTRING || m_prevtoken.m_toktype == AstToken::T_LITERALRAWSTRING)
                                {
                                    str = compilestring(&length, true);
                                    string = String::take(str, length);
                                    /* gc fix */
                                    gcs->vmStackPush(Value::fromObject(string));
                                    sw->m_table.set(Value::fromObject(string), jump);
                                    /* gc fix */
                                    gcs->vmStackPop();
                                }
                                else if(checknumber())
                                {
                                    sw->m_table.set(compilenumber(), jump);
                                }
                                else
                                {
                                    /* pop the switch */
                                    gcs->vmStackPop();
                                    raiseerror("only constants can be used in 'case' expressions");
                                    return;
                                }
                            } while(match(AstToken::T_COMMA));
                            consume(AstToken::T_COLON, "expected ':' after 'case' constants");
                        }
                        else
                        {
                            consume(AstToken::T_COLON, "expected ':' after 'default'");
                            swstate = 2;
                            sw->m_defaultjump = currentblob()->m_count - startoffset;
                        }
                    }
                    else
                    {
                        /* otherwise, it's a statement inside the current case */
                        if(swstate == 0)
                        {
                            raiseerror("cannot have statements before any case");
                        }
                        parsestmt();
                    }
                }
                m_inswitch = false;
                /* if we ended without a default case, patch its condition jump */
                if(swstate == 1)
                {
                    caseends[casecount++] = emitjump(Instruction::OPC_JUMPNOW);
                }
                /* patch all the case jumps to the end */
                for(i = 0; i < casecount; i++)
                {
                    patchjump(caseends[i]);
                }
                sw->m_exitjump = currentblob()->m_count - startoffset;
                patchswitch(switchcode, pushconst(Value::fromObject(sw)));
                /* pop the switch */
                gcs->vmStackPop();
            }

            void parseifstmt()
            {
                int elsejump;
                int thenjump;
                parseexpression();
                thenjump = emitjump(Instruction::OPC_JUMPIFFALSE);
                emitinstruc(Instruction::OPC_POPONE);
                parsestmt();
                elsejump = emitjump(Instruction::OPC_JUMPNOW);
                patchjump(thenjump);
                emitinstruc(Instruction::OPC_POPONE);
                if(match(AstToken::T_KWELSE))
                {
                    parsestmt();
                }
                patchjump(elsejump);
            }

            void parseechostmt()
            {
                parseexpression();
                emitinstruc(Instruction::OPC_ECHO);
                consumestmtend();
            }

            void parsethrowstmt()
            {
                parseexpression();
                emitinstruc(Instruction::OPC_EXTHROW);
                discardlocals(m_currentfunccompiler->m_scopedepth - 1);
                consumestmtend();
            }

            void parseassertstmt()
            {
                consume(AstToken::T_PARENOPEN, "expected '(' after 'assert'");
                parseexpression();
                if(match(AstToken::T_COMMA))
                {
                    ignorewhitespace();
                    parseexpression();
                }
                else
                {
                    emitinstruc(Instruction::OPC_PUSHNULL);
                }
                emitinstruc(Instruction::OPC_ASSERT);
                consume(AstToken::T_PARENCLOSE, "expected ')' after 'assert'");
                consumestmtend();
            }

            void parsetrystmt()
            {
                int address;
                int type;
                int finally;
                int trybegins;
                int exitjump;
                int continueexecutionaddress;
                bool catchexists;
                bool finalexists;
            #if 0
                if(m_currentfunccompiler->m_handlercount == CallFrame::CONF_MAXEXCEPTHANDLERS)
                {
                    raiseerror("maximum exception handler in scope exceeded");
                }
            #endif
                m_currentfunccompiler->m_handlercount++;
                m_istrying = true;
                ignorewhitespace();
                trybegins = emittry();
                /* compile the try body */
                parsestmt();
                emitinstruc(Instruction::OPC_EXPOPTRY);
                exitjump = emitjump(Instruction::OPC_JUMPNOW);
                m_istrying = false;
                /*
                // we can safely use 0 because a program cannot start with a
                // catch or finally block
                */
                address = 0;
                type = -1;
                finally = 0;
                catchexists = false;
                finalexists = false;
                /* catch body must maintain its own scope */
                if(match(AstToken::T_KWCATCH))
                {
                    catchexists = true;
                    scopebegin();
                    consume(AstToken::T_PARENOPEN, "expected '(' after 'catch'");
                    /*
                    consume(AstToken::T_IDENTNORMAL, "missing exception class name");
                    */
                    type = makeidentconst(&m_prevtoken);
                    address = currentblob()->m_count;
                    if(match(AstToken::T_IDENTNORMAL))
                    {
                        createdvar(m_prevtoken);
                    }
                    else
                    {
                        emitinstruc(Instruction::OPC_POPONE);
                    }
                    consume(AstToken::T_PARENCLOSE, "expected ')' after 'catch'");
                    emitinstruc(Instruction::OPC_EXPOPTRY);
                    ignorewhitespace();
                    parsestmt();
                    scopeend();
                }
                else
                {
                    type = pushconst(Value::fromObject(String::intern("Exception")));
                }
                patchjump(exitjump);
                if(match(AstToken::T_KWFINALLY))
                {
                    finalexists = true;
                    /*
                    // if we arrived here from either the try or handler block,
                    // we don't want to continue propagating the exception
                    */
                    emitinstruc(Instruction::OPC_PUSHFALSE);
                    finally = currentblob()->m_count;
                    ignorewhitespace();
                    parsestmt();
                    continueexecutionaddress = emitjump(Instruction::OPC_JUMPIFFALSE);
                    /* pop the bool off the stack */
                    emitinstruc(Instruction::OPC_POPONE);
                    emitinstruc(Instruction::OPC_EXPUBLISHTRY);
                    patchjump(continueexecutionaddress);
                    emitinstruc(Instruction::OPC_POPONE);
                }
                if(!finalexists && !catchexists)
                {
                    raiseerror("try block must contain at least one of catch or finally");
                }
                patchtry(trybegins, type, address, finally);
            }

            void parsereturnstmt()
            {
                m_isreturning = true;
                /*
                if(m_currentfunccompiler->m_contexttype == Function::CTXTYPE_SCRIPT)
                {
                    raiseerror("cannot return from top-level code");
                }
                */
                if(match(AstToken::T_SEMICOLON) || match(AstToken::T_NEWLINE))
                {
                    emitreturn();
                }
                else
                {
                    if(m_currentfunccompiler->m_contexttype == Function::CTXTYPE_INITIALIZER)
                    {
                        raiseerror("cannot return value from constructor");
                    }
                    if(m_istrying)
                    {
                        emitinstruc(Instruction::OPC_EXPOPTRY);
                    }
                    parseexpression();
                    emitinstruc(Instruction::OPC_RETURN);
                    consumestmtend();
                }
                m_isreturning = false;
            }

            void parsewhilestmt()
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
                m_innermostloopstart = currentblob()->m_count;
                m_innermostloopscopedepth = m_currentfunccompiler->m_scopedepth;
                parseexpression();
                exitjump = emitjump(Instruction::OPC_JUMPIFFALSE);
                emitinstruc(Instruction::OPC_POPONE);
                parsestmt();
                emitloop(m_innermostloopstart);
                patchjump(exitjump);
                emitinstruc(Instruction::OPC_POPONE);
                endloop();
                m_innermostloopstart = surroundingloopstart;
                m_innermostloopscopedepth = surroundingscopedepth;
            }

            void parsedo_whilestmt()
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
                m_innermostloopstart = currentblob()->m_count;
                m_innermostloopscopedepth = m_currentfunccompiler->m_scopedepth;
                parsestmt();
                consume(AstToken::T_KWWHILE, "expecting 'while' statement");
                parseexpression();
                exitjump = emitjump(Instruction::OPC_JUMPIFFALSE);
                emitinstruc(Instruction::OPC_POPONE);
                emitloop(m_innermostloopstart);
                patchjump(exitjump);
                emitinstruc(Instruction::OPC_POPONE);
                endloop();
                m_innermostloopstart = surroundingloopstart;
                m_innermostloopscopedepth = surroundingscopedepth;
            }

            void parsecontinuestmt()
            {
                if(m_innermostloopstart == -1)
                {
                    raiseerror("'continue' can only be used in a loop");
                }
                /*
                // discard local variables created in the loop
                //  discard_local(this, m_innermostloopscopedepth);
                */
                discardlocals(m_innermostloopscopedepth + 1);
                /* go back to the top of the loop */
                emitloop(m_innermostloopstart);
                consumestmtend();
            }

            void parsebreakstmt()
            {
                if(!m_inswitch)
                {
                    if(m_innermostloopstart == -1)
                    {
                        raiseerror("'break' can only be used in a loop");
                    }
            /* discard local variables created in the loop */
            #if 0
                    int i;
                    for(i = m_currentfunccompiler->m_localcount - 1; i >= 0 && m_currentfunccompiler->m_locals[i].depth >= m_currentfunccompiler->m_scopedepth; i--)
                    {
                        if (m_currentfunccompiler->m_locals[i].iscaptured)
                        {
                            emitinstruc(Instruction::OPC_UPVALUECLOSE);
                        }
                        else
                        {
                            emitinstruc(Instruction::OPC_POPONE);
                        }
                    }
            #endif
                    discardlocals(m_innermostloopscopedepth + 1);
                    emitjump(Instruction::OPC_BREAK_PL);
                }
                consumestmtend();
            }

            void synchronize()
            {
                m_panicmode = false;
                while(m_currtoken.m_toktype != AstToken::T_EOF)
                {
                    if(m_currtoken.m_toktype == AstToken::T_NEWLINE || m_currtoken.m_toktype == AstToken::T_SEMICOLON)
                    {
                        return;
                    }
                    switch(m_currtoken.m_toktype)
                    {
                        case AstToken::T_KWCLASS:
                        case AstToken::T_KWFUNCTION:
                        case AstToken::T_KWVAR:
                        case AstToken::T_KWFOREACH:
                        case AstToken::T_KWIF:
                        case AstToken::T_KWEXTENDS:
                        case AstToken::T_KWSWITCH:
                        case AstToken::T_KWCASE:
                        case AstToken::T_KWFOR:
                        case AstToken::T_KWDO:
                        case AstToken::T_KWWHILE:
                        case AstToken::T_KWECHO:
                        case AstToken::T_KWASSERT:
                        case AstToken::T_KWTRY:
                        case AstToken::T_KWCATCH:
                        case AstToken::T_KWTHROW:
                        case AstToken::T_KWRETURN:
                        case AstToken::T_KWSTATIC:
                        case AstToken::T_KWTHIS:
                        case AstToken::T_KWSUPER:
                        case AstToken::T_KWFINALLY:
                        case AstToken::T_KWIN:
                        case AstToken::T_KWAS:
                            return;
                        default:
                        /* do nothing */
                        ;
                    }
                    advance();
                }
            }
    };

    class ArgCheck
    {
        public:
            const char* m_argcheckfuncname;
            FuncContext m_scriptfnctx;

        public:
            NEON_INLINE ArgCheck(const char* name, const FuncContext& scfn)
            {
                m_scriptfnctx = scfn;
                m_argcheckfuncname = name;
            }

            template<typename... ArgsT>
            NEON_INLINE Value thenFail(const char* srcfile, int srcline, const char* fmt, ArgsT&&... args)
            {
                auto gcs = SharedState::get();
            #if 0
                    gcs->vmStackPop(m_scriptfnctx.argc);
            #endif
                if(!throwScriptException(gcs->m_exceptions.argumenterror, srcfile, srcline, fmt, args...))
                {
                }
                return Value::makeBool(false);
            }
    };

    /* bottom */

    String* Wrappers::wrapClassGetName(Class* cl)
    {
        return cl->m_classname;
    }

    uint32_t Wrappers::wrapStrGetHash(String* os)
    {
        return os->m_hashvalue;
    }

    const char* Wrappers::wrapStrGetData(String* os)
    {
        return os->data();
    }

    size_t Wrappers::wrapStrGetLength(String* os)
    {
        return os->length();
    }

    String* Wrappers::wrapMakeFromStrBuf(const StrBuffer& buf, uint32_t hsv, size_t length)
    {
        return String::makeFromStrbuf(buf, hsv, length);
    }

    String* wrapStringCopy(const char* data, size_t len)
    {
        return String::copy(data, len);
    }

    String* Wrappers::wrapStringCopy(const char* str)
    {
        return String::copy(str);
    }

    Blob* Wrappers::wrapGetBlobOfClosure(Function* fn)
    {
        return fn->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.blob;
    }

    size_t Wrappers::wrapGetArityOfClosure(Function* ofn)
    {
        return ofn->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity;
    }

    size_t Wrappers::wrapGetArityOfFuncScript(Function* ofn)
    {
        return ofn->m_fnvals.fnscriptfunc.arity;
    }

    size_t Wrappers::wrapGetBlobcountOfFuncScript(Function* ofn)
    {
        return ofn->m_fnvals.fnscriptfunc.blob->m_count;
    }

    const char* Wrappers::wrapGetInstanceName(Instance* inst)
    {
        return inst->m_instanceclass->m_classname->data();
    }

    String* Value::toString(Value value)
    {
        IOStream pr;
        String* s;
        IOStream::makeStackString(&pr);
        ValPrinter::printValue(&pr, value, false, true);
        s = pr.takeString();
        return s;
    }

    void Value::markDict(Dict* dict)
    {
        Value::markValArray(&dict->m_htkeys);
        Value::markValTable(&dict->m_htvalues);
    }

    void Value::markValArray(ValList<Value>* list)
    {
        size_t i;
        NN_NULLPTRCHECK_RETURN(list);
        for(i = 0; i < list->count(); i++)
        {
            SharedState::markValue(list->get(i));
        }
    }

    void Value::valtabRemoveWhites(HashTable<Value, Value>* table)
    {
        int i;
        auto gcs = SharedState::get();
        for(i = 0; i < table->m_htcapacity; i++)
        {
            auto entry = &table->m_htentries[i];
            if(entry->key.isObject() && entry->key.asObject()->m_objmark != gcs->markvalue)
            {
                table->remove(entry->key);
            }
        }
    }

    void Value::markValTable(HashTable<Value, Value>* table)
    {
        int i;
        NN_NULLPTRCHECK_RETURN(table);
        if(table == nullptr)
        {
            return;
        }
        if(!table->m_htactive)
        {
            // SharedState::raiseWarning("trying to mark inactive hashtable <%p>!", table);
            return;
        }
        for(i = 0; i < table->m_htcapacity; i++)
        {
            auto entry = &table->m_htentries[i];
            if(entry != nullptr)
            {
                if(!entry->key.isNull())
                {
                    SharedState::markValue(entry->key);
                    SharedState::markValue(entry->value.value);
                }
            }
        }
    }

    bool Value::isFalse() const
    {
        if(isNull())
        {
            return true;
        }
        if(isBool())
        {
            return !asBool();
        }
        /* -1 is the number equivalent of false */
        if(isNumber())
        {
            return asNumber() < 0;
        }
        /* Non-empty strings are true, empty strings are false.*/
        if(isString())
        {
            return asString()->length() < 1;
        }
        /* Non-empty lists are true, empty lists are false.*/
        if(isArray())
        {
            return asArray()->count() == 0;
        }
        /* Non-empty dicts are true, empty dicts are false. */
        if(isDict())
        {
            return asDict()->m_htkeys.count() == 0;
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

    bool Value::compareArrays(Array* oa, Array* ob)
    {
        size_t i;
        Array* arra;
        Array* arrb;
        arra = (Array*)oa;
        arrb = (Array*)ob;
        /* unlike Dict, array order matters */
        if(arra->count() != arrb->count())
        {
            return false;
        }
        for(i = 0; i < (size_t)arra->count(); i++)
        {
            if(!Value::compareValues(arra->get(i), arrb->get(i)))
            {
                return false;
            }
        }
        return true;
    }

    bool Value::compareStrings(Object* oa, Object* ob)
    {
        size_t alen;
        size_t blen;
        const char* adata;
        const char* bdata;
        String* stra;
        String* strb;
        stra = (String*)oa;
        strb = (String*)ob;
        alen = stra->length();
        blen = strb->length();
        if(alen != blen)
        {
            return false;
        }
        adata = stra->data();
        bdata = strb->data();
        return (memcmp(adata, bdata, alen) == 0);
    }

    bool Value::compareDicts(Object* oa, Object* ob)
    {
        Dict* dicta;
        Dict* dictb;
        Property* fielda;
        Property* fieldb;
        size_t ai;
        size_t lena;
        size_t lenb;
        Value keya;
        dicta = (Dict*)oa;
        dictb = (Dict*)ob;
        lena = dicta->m_htkeys.count();
        lenb = dictb->m_htkeys.count();
        if(lena != lenb)
        {
            return false;
        }
        ai = 0;
        while(ai < lena)
        {
            /* first, get the key name off of dicta ... */
            keya = dicta->m_htkeys.get(ai);
            fielda = dicta->m_htvalues.getfield(dicta->m_htkeys.get(ai));
            if(fielda != nullptr)
            {
                /* then look up that key in dictb ... */
                fieldb = dictb->get(keya);
                if((fielda != nullptr) && (fieldb != nullptr))
                {
                    /* if it exists, compare their values */
                    if(!Value::compareValues(fielda->value, fieldb->value))
                    {
                        return false;
                    }
                }
            }
            ai++;
        }
        return true;
    }

    bool Value::compareObjects(Value a, Value b)
    {
        Object::Type ta;
        Object::Type tb;
        Object* oa;
        Object* ob;
        oa = a.asObject();
        ob = b.asObject();
        ta = oa->m_objtype;
        tb = ob->m_objtype;
        if(ta == tb)
        {
            /* we might not need to do a deep comparison if its the same object */
            if(oa == ob)
            {
                return true;
            }
            else if(ta == Object::OTYP_STRING)
            {
                return compareStrings(oa, ob);
            }
            else if(ta == Object::OTYP_ARRAY)
            {
                return compareArrays(a.asArray(), b.asArray());
            }
            else if(ta == Object::OTYP_DICT)
            {
                return compareDicts(oa, ob);
            }
        }
        return false;
    }

    bool Value::compareValActual(Value a, Value b)
    {
        /*
        if(a.m_valtype != b.m_valtype)
        {
            return false;
        }
        */
        if(a.isNull())
        {
            return true;
        }
        else if(a.isBool())
        {
            return a.asBool() == b.asBool();
        }
        else if(a.isNumber())
        {
            return (a.asNumber() == b.asNumber());
        }
        else
        {
            if(a.isObject() && b.isObject())
            {
                if(a.asObject() == b.asObject())
                {
                    return true;
                }
                return compareObjects(a, b);
            }
        }

        return false;
    }

    bool Value::compareValues(Value a, Value b)
    {
        bool r;
        r = compareValActual(a, b);
        return r;
    }

    /**
     * returns the greater of the two values.
     * this function encapsulates the object hierarchy
     */
    Value Value::findGreater(Value a, Value b)
    {
        size_t alen;
        const char* adata;
        const char* bdata;
        String* osa;
        String* osb;
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
                adata = osa->data();
                bdata = osb->data();
                alen = osa->length();
                if(strncmp(adata, bdata, alen) >= 0)
                {
                    return a;
                }
                return b;
            }
            else if(a.isFuncscript() && b.isFuncscript())
            {
                if(a.asFunction()->m_fnvals.fnscriptfunc.arity >= b.asFunction()->m_fnvals.fnscriptfunc.arity)
                {
                    return a;
                }
                return b;
            }
            else if(a.isFuncclosure() && b.isFuncclosure())
            {
                if(a.asFunction()->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity >= b.asFunction()->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity)
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
                if(a.asClass()->m_instmethods.count() >= b.asClass()->m_instmethods.count())
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
                if(a.asDict()->m_htkeys.count() >= b.asDict()->m_htkeys.count())
                {
                    return a;
                }
                return b;
            }
            else if(a.isFile() && b.isFile())
            {
                if(strcmp(a.asFile()->m_path->data(), b.asFile()->m_path->data()) >= 0)
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
    void Value::sortValues(Value* values, int count)
    {
        int i;
        int j;
        Value temp;
        for(i = 0; i < count; i++)
        {
            for(j = 0; j < count; j++)
            {
                if(Value::compareValues(values[j], Value::findGreater(values[i], values[j])))
                {
                    temp = values[i];
                    values[i] = values[j];
                    values[j] = temp;
                    if(values[i].isArray())
                    {
                        sortValues((Value*)values[i].asArray()->data(), values[i].asArray()->count());
                    }

                    if(values[j].isArray())
                    {
                        sortValues((Value*)values[j].asArray()->data(), values[j].asArray()->count());
                    }
                }
            }
        }
    }

    Value Value::copyValue(Value value)
    {
        if(value.isObject())
        {
            switch(value.asObject()->m_objtype)
            {
                case Object::OTYP_STRING:
                {
                    String* string;
                    string = value.asString();
                    return Value::fromObject(String::copyObject(string));
                }
                break;
                case Object::OTYP_ARRAY:
                {
                    size_t i;
                    Array* list;
                    Array* newlist;
                    list = value.asArray();
                    newlist = Array::make();
                    for(i = 0; i < list->count(); i++)
                    {
                        newlist->push(list->get(i));
                    }
                    return Value::fromObject(newlist);
                }
                break;
                case Object::OTYP_DICT:
                {
                    Dict* dict;
                    dict = value.asDict();
                    return Value::fromObject(dict->copy());
                }
                break;
                default:
                    break;
            }
        }
        return value;
    }

    void Object::markObject(Object* object)
    {
        if(object == nullptr)
        {
            return;
        }
        auto gcs = SharedState::get();
        if(object->m_objmark == gcs->markvalue)
        {
            return;
        }
    #if defined(NEON_CONFIG_DEBUGGC) && NEON_CONFIG_DEBUGGC
        gcs->m_debugwriter->format("GC: marking object at <%p> ", (void*)object);
        ValPrinter::printValue(gcs->m_debugwriter, Value::fromObject(object), false);
        gcs->m_debugwriter->format("\n");
    #endif
        object->m_objmark = gcs->markvalue;
        if(gcs->m_gcstate.graycapacity < gcs->m_gcstate.graycount + 1)
        {
            gcs->m_gcstate.graycapacity = Memory::getNextCapacity(gcs->m_gcstate.graycapacity);
            gcs->m_gcstate.graystack = (Object**)Memory::sysRealloc(gcs->m_gcstate.graystack, sizeof(Object*) * gcs->m_gcstate.graycapacity);
            if(gcs->m_gcstate.graystack == nullptr)
            {
                fflush(stdout);
                fprintf(stderr, "GC encountered an error");
                abort();
            }
        }
        gcs->m_gcstate.graystack[gcs->m_gcstate.graycount++] = object;
    }

    void Object::blackenObject(Object* object)
    {
    #if defined(NEON_CONFIG_DEBUGGC) && NEON_CONFIG_DEBUGGC
        auto gcs = SharedState::get();
        gcs->m_debugwriter->format("GC: blacken object at <%p> ", (void*)object);
        ValPrinter::printValue(gcs->m_debugwriter, Value::fromObject(object), false);
        gcs->m_debugwriter->format("\n");
    #endif
        switch(object->m_objtype)
        {
            case Object::OTYP_INVALID:
                {
                }
                break;
            case Object::OTYP_MODULE:
                {
                    Module* module;
                    module = (Module*)object;
                    Value::markValTable(&module->m_deftable);
                }
                break;
            case Object::OTYP_SWITCH:
                {
                    Switch* sw;
                    sw = (Switch*)object;
                    Value::markValTable(&sw->m_table);
                }
                break;
            case Object::OTYP_FILE:
                {
                    File* file;
                    file = (File*)object;
                    File::mark(file);
                }
                break;
            case Object::OTYP_DICT:
                {
                    Dict* dict;
                    dict = (Dict*)object;
                    Value::markDict(dict);
                }
                break;
            case Object::OTYP_ARRAY:
                {
                    Array* list;
                    list = (Array*)object;
                    Value::markValArray(&list->m_objvarray);
                }
                break;
            case Object::OTYP_FUNCBOUND:
                {
                    Function* bound;
                    bound = (Function*)object;
                    SharedState::markValue(bound->m_fnvals.fnmethod.receiver);
                    Object::markObject((Object*)bound->m_fnvals.fnmethod.method);
                }
                break;
            case Object::OTYP_CLASS:
                {
                    Class* klass;
                    klass = (Class*)object;
                    Object::markObject((Object*)klass->m_classname);
                    Value::markValTable(&klass->m_instmethods);
                    Value::markValTable(&klass->m_staticmethods);
                    Value::markValTable(&klass->m_staticproperties);
                    SharedState::markValue(klass->m_constructor);
                    SharedState::markValue(klass->m_destructor);
                    if(klass->m_superclass != nullptr)
                    {
                        Object::markObject((Object*)klass->m_superclass);
                    }
                }
                break;
            case Object::OTYP_FUNCCLOSURE:
                {
                    int i;
                    Function* closure;
                    closure = (Function*)object;
                    Object::markObject((Object*)closure->m_fnvals.fnclosure.scriptfunc);
                    for(i = 0; i < closure->m_upvalcount; i++)
                    {
                        Object::markObject((Object*)closure->m_fnvals.fnclosure.m_upvalues[i]);
                    }
                }
                break;
            case Object::OTYP_FUNCSCRIPT:
                {
                    Function* function;
                    function = (Function*)object;
                    Object::markObject((Object*)function->m_funcname);
                    Object::markObject((Object*)function->m_fnvals.fnscriptfunc.module);
                    Value::markValArray(&function->m_fnvals.fnscriptfunc.blob->m_constants);
                }
                break;
            case Object::OTYP_INSTANCE:
                {
                    Instance* instance;
                    instance = (Instance*)object;
                    Instance::mark(instance);
                }
                break;
            case Object::OTYP_UPVALUE:
                {
                    auto upv = (Upvalue*)object;
                    SharedState::markValue(upv->m_closed);
                }
                break;
            case Object::OTYP_RANGE:
            case Object::OTYP_FUNCNATIVE:
            case Object::OTYP_USERDATA:
            case Object::OTYP_STRING:
                break;
        }
    }

    void Object::destroyObject(Object* object)
    {
        auto gcs = SharedState::get();
        if(object->m_objstale)
        {
            return;
        }
        switch(object->m_objtype)
        {
            case Object::OTYP_MODULE:
            {
                Module* module;
                module = (Module*)object;
                Module::destroy(module);
                gcs->gcRelease(module);
            }
            break;
            case Object::OTYP_FILE:
            {
                File* file;
                file = (File*)object;
                File::destroy(file);
            }
            break;
            case Object::OTYP_DICT:
            {
                Dict* dict;
                
                dict = (Dict*)object;
                Dict::destroy(dict);
                gcs->gcRelease(dict);
            }
            break;
            case Object::OTYP_ARRAY:
            {
                Array* list;
                list = (Array*)object;
                list->m_objvarray.deInit();
                gcs->gcRelease(list);
            }
            break;
            case Object::OTYP_FUNCBOUND:
            {
                /*
                // a closure may be bound to multiple instances
                // for this reason, we do not free closures when freeing bound methods
                */
                auto fn = (Function*)object;
                gcs->gcRelease(fn);
            }
            break;
            case Object::OTYP_CLASS:
            {
                Class* klass;
                klass = (Class*)object;
                Class::destroy(klass);
            }
            break;
            case Object::OTYP_FUNCCLOSURE:
            {
                Function* closure;
                closure = (Function*)object;
                gcs->gcRelease(closure->m_fnvals.fnclosure.m_upvalues, (sizeof(Upvalue*) * closure->m_upvalcount));
                /*
                // there may be multiple closures that all reference the same function
                // for this reason, we do not free functions when freeing closures
                */
                gcs->gcRelease(closure);
            }
            break;
            case Object::OTYP_FUNCSCRIPT:
            {
                Function* function;
                function = (Function*)object;
                Function::destroy(function);
                gcs->gcRelease(function);
            }
            break;
            case Object::OTYP_INSTANCE:
            {
                Instance* instance;
                instance = (Instance*)object;
                Instance::destroy(instance);
            }
            break;
            case Object::OTYP_FUNCNATIVE:
            {
                auto fn = (Function*)object;
                gcs->gcRelease(fn);
            }
            break;
            case Object::OTYP_UPVALUE:
            {
                auto upv = (Upvalue*)object;
                gcs->gcRelease(upv);
            }
            break;
            case Object::OTYP_RANGE:
            {
                auto rn = (Range*)object;
                gcs->gcRelease(rn);
            }
            break;
            case Object::OTYP_STRING:
            {
                String* string;
                string = (String*)object;
                String::destroy(string);
                gcs->gcRelease(string);
            }
            break;
            case Object::OTYP_SWITCH:
            {
                Switch* sw;
                sw = (Switch*)object;
                sw->m_table.deInit();
                gcs->gcRelease(sw);
            }
            break;
            case Object::OTYP_USERDATA:
            {
                Userdata* ptr;
                ptr = (Userdata*)object;
                if(ptr->m_ondestroyfn)
                {
                    ptr->m_ondestroyfn(ptr->m_pointer);
                }
                gcs->gcRelease(ptr);
            }
            break;
            default:
                break;
        }
    }

    void SharedState::gcMarkRoots()
    {
        int i;
        int j;
        Value* slot;
        Upvalue* upvalue;
        CallFrame::ExceptionInfo* handler;
        (void)handler;
        for(slot = m_vmstate.stackvalues.data(); slot < m_vmstate.stackvalues.getp(m_vmstate.stackidx); slot++)
        {
            SharedState::markValue(*slot);
        }
        for(i = 0; i < (int)m_vmstate.framecount; i++)
        {
            Object::markObject((Object*)m_vmstate.framevalues[i].closure);
            for(j = 0; j < (int)m_vmstate.framevalues[i].m_handlercount; j++)
            {
                handler = &m_vmstate.framevalues[i].handlers[j];
            }
        }
        for(upvalue = m_vmstate.openupvalues; upvalue != nullptr; upvalue = upvalue->m_next)
        {
            Object::markObject((Object*)upvalue);
        }
        Value::markValTable(&m_declaredglobals);
        Value::markValTable(&m_openedmodules);
        // Object::markObject((Object*)m_exceptions.stdexception);
        gcMarkCompilerRoots();
    }

    template<typename HTKeyT, typename HTValT>
    Property* HashTable<HTKeyT, HTValT>::getfieldbyostr(String* str) const
    {
        return getfieldbystr(Value::makeNull(), str->data(), str->length(), str->m_hashvalue);
    }

    template<typename HTKeyT, typename HTValT>
    Property* HashTable<HTKeyT, HTValT>::getfieldbycstr(const char* kstr) const
    {
        size_t klen;
        uint32_t hsv;
        klen = strlen(kstr);
        hsv = Util::hashString(kstr, klen);
        return getfieldbystr(Value::makeNull(), kstr, klen, hsv);
    }

    template<typename HTKeyT, typename HTValT>
    Property* HashTable<HTKeyT, HTValT>::getfield(HTKeyT key) const
    {
        String* oskey;
        if(key.isString())
        {
            oskey = key.asString();
            return getfieldbystr(key, oskey->data(), oskey->length(), oskey->m_hashvalue);
        }
        return getfieldbyvalue(key);
    }

    template<typename HTKeyT, typename HTValT>
    bool HashTable<HTKeyT, HTValT>::remove(HTKeyT key)
    {
        Entry* entry;
        if(m_htcount == 0)
        {
            return false;
        }
        /* find the entry */
        entry = findentrybyvalue(m_htentries, m_htcapacity, key);
        if(entry->key.isNull())
        {
            return false;
        }
        /* place a tombstone in the entry. */
        entry->key = HTKeyT{};
        entry->value = Property::make(Value::makeBool(true), Property::FTYP_VALUE);
        return true;
    }

    
    template<typename... ArgsT>
    static void vmDebugPrintVal(Value val, const char* fmt, ArgsT&&... args)
    {
        IOStream* pr;
        auto gcs = SharedState::get();
        pr = gcs->m_stderrprinter;
        pr->format("VMDEBUG: val=<<<");
        ValPrinter::printValue(pr, val, true, false);
        pr->format(">>> ");
        pr->format(fmt, args...);
        pr->format("\n");
    }

    template<typename... ArgsT>
    bool throwScriptException(Class* exklass, const char* srcfile, int srcline, const char* format, ArgsT&&... args)
    {
        enum
        {
            kMaxBufSize = 1024
        };
        constexpr static auto tmpsnprintf = snprintf;
        int length;
        char* message;
        Value stacktrace;
        Instance* instance;
        auto gcs = SharedState::get();
        message = (char*)Memory::sysMalloc(kMaxBufSize + 1);
        length = tmpsnprintf(message, kMaxBufSize, format, args...);
        instance = makeExceptionInstance(exklass, srcfile, srcline, String::take(message, length));
        gcs->vmStackPush(Value::fromObject(instance));
        stacktrace = gcs->vmExceptionGetStackTrace();
        gcs->vmStackPush(stacktrace);
        instance->defProperty(String::intern("stacktrace"), stacktrace);
        gcs->vmStackPop();
        return gcs->vmExceptionPropagate();
    }

    /**
    * raise a fatal error that cannot recover.
    */

    template<typename... ArgsT>
    void raiseFatalError(const char* format, ArgsT&&... args)
    {
        constexpr static auto tmpfprintf = fprintf;
        int i;
        int line;
        size_t instruction;
        CallFrame* frame;
        Function* function;
        /* flush out anything on stdout first */
        fflush(stdout);
        auto gcs = SharedState::get();
        frame = &gcs->m_vmstate.framevalues[gcs->m_vmstate.framecount - 1];
        function = frame->closure->m_fnvals.fnclosure.scriptfunc;
        instruction = frame->inscode - function->m_fnvals.fnscriptfunc.blob->m_instrucs.data() - 1;
        line = function->m_fnvals.fnscriptfunc.blob->m_instrucs[instruction].fromsourceline;
        fprintf(stderr, "RuntimeError: ");
        tmpfprintf(stderr, format, args...);
        fprintf(stderr, " -> %s:%d ", function->m_fnvals.fnscriptfunc.module->m_physicalpath->data(), line);
        fputs("\n", stderr);
        if(gcs->m_vmstate.framecount > 1)
        {
            fprintf(stderr, "stacktrace:\n");
            for(i = gcs->m_vmstate.framecount - 1; i >= 0; i--)
            {
                frame = &gcs->m_vmstate.framevalues[i];
                function = frame->closure->m_fnvals.fnclosure.scriptfunc;
                /* -1 because the IP is sitting on the next instruction to be executed */
                instruction = frame->inscode - function->m_fnvals.fnscriptfunc.blob->m_instrucs.data() - 1;
                fprintf(stderr, "    %s:%d -> ", function->m_fnvals.fnscriptfunc.module->m_physicalpath->data(), function->m_fnvals.fnscriptfunc.blob->m_instrucs[instruction].fromsourceline);
                if(function->m_funcname == nullptr)
                {
                    fprintf(stderr, "<script>");
                }
                else
                {
                    fprintf(stderr, "%s()", function->m_funcname->data());
                }
                fprintf(stderr, "\n");
            }
        }
        gcs->resetVMState();
    }

    class Debug
    {
        public:
            static const char* opcodeToString(uint16_t instruc)
            {
                switch(instruc)
                {
                    case Instruction::OPC_GLOBALDEFINE:
                        return "OPC_GLOBALDEFINE";
                    case Instruction::OPC_GLOBALGET:
                        return "OPC_GLOBALGET";
                    case Instruction::OPC_GLOBALSET:
                        return "OPC_GLOBALSET";
                    case Instruction::OPC_LOCALGET:
                        return "OPC_LOCALGET";
                    case Instruction::OPC_LOCALSET:
                        return "OPC_LOCALSET";
                    case Instruction::OPC_FUNCARGOPTIONAL:
                        return "OPC_FUNCARGOPTIONAL";
                    case Instruction::OPC_FUNCARGSET:
                        return "OPC_FUNCARGSET";
                    case Instruction::OPC_FUNCARGGET:
                        return "OPC_FUNCARGGET";
                    case Instruction::OPC_UPVALUEGET:
                        return "OPC_UPVALUEGET";
                    case Instruction::OPC_UPVALUESET:
                        return "OPC_UPVALUESET";
                    case Instruction::OPC_UPVALUECLOSE:
                        return "OPC_UPVALUECLOSE";
                    case Instruction::OPC_PROPERTYGET:
                        return "OPC_PROPERTYGET";
                    case Instruction::OPC_PROPERTYGETSELF:
                        return "OPC_PROPERTYGETSELF";
                    case Instruction::OPC_PROPERTYSET:
                        return "OPC_PROPERTYSET";
                    case Instruction::OPC_JUMPIFFALSE:
                        return "OPC_JUMPIFFALSE";
                    case Instruction::OPC_JUMPNOW:
                        return "OPC_JUMPNOW";
                    case Instruction::OPC_LOOP:
                        return "OPC_LOOP";
                    case Instruction::OPC_EQUAL:
                        return "OPC_EQUAL";
                    case Instruction::OPC_PRIMGREATER:
                        return "OPC_PRIMGREATER";
                    case Instruction::OPC_PRIMLESSTHAN:
                        return "OPC_PRIMLESSTHAN";
                    case Instruction::OPC_PUSHEMPTY:
                        return "OPC_PUSHEMPTY";
                    case Instruction::OPC_PUSHNULL:
                        return "OPC_PUSHNULL";
                    case Instruction::OPC_PUSHTRUE:
                        return "OPC_PUSHTRUE";
                    case Instruction::OPC_PUSHFALSE:
                        return "OPC_PUSHFALSE";
                    case Instruction::OPC_PRIMADD:
                        return "OPC_PRIMADD";
                    case Instruction::OPC_PRIMSUBTRACT:
                        return "OPC_PRIMSUBTRACT";
                    case Instruction::OPC_PRIMMULTIPLY:
                        return "OPC_PRIMMULTIPLY";
                    case Instruction::OPC_PRIMDIVIDE:
                        return "OPC_PRIMDIVIDE";
                    case Instruction::OPC_PRIMMODULO:
                        return "OPC_PRIMMODULO";
                    case Instruction::OPC_PRIMPOW:
                        return "OPC_PRIMPOW";
                    case Instruction::OPC_PRIMNEGATE:
                        return "OPC_PRIMNEGATE";
                    case Instruction::OPC_PRIMNOT:
                        return "OPC_PRIMNOT";
                    case Instruction::OPC_PRIMBITNOT:
                        return "OPC_PRIMBITNOT";
                    case Instruction::OPC_PRIMAND:
                        return "OPC_PRIMAND";
                    case Instruction::OPC_PRIMOR:
                        return "OPC_PRIMOR";
                    case Instruction::OPC_PRIMBITXOR:
                        return "OPC_PRIMBITXOR";
                    case Instruction::OPC_PRIMSHIFTLEFT:
                        return "OPC_PRIMSHIFTLEFT";
                    case Instruction::OPC_PRIMSHIFTRIGHT:
                        return "OPC_PRIMSHIFTRIGHT";
                    case Instruction::OPC_PUSHONE:
                        return "OPC_PUSHONE";
                    case Instruction::OPC_PUSHCONSTANT:
                        return "OPC_PUSHCONSTANT";
                    case Instruction::OPC_ECHO:
                        return "OPC_ECHO";
                    case Instruction::OPC_POPONE:
                        return "OPC_POPONE";
                    case Instruction::OPC_DUPONE:
                        return "OPC_DUPONE";
                    case Instruction::OPC_POPN:
                        return "OPC_POPN";
                    case Instruction::OPC_ASSERT:
                        return "OPC_ASSERT";
                    case Instruction::OPC_EXTHROW:
                        return "OPC_EXTHROW";
                    case Instruction::OPC_MAKECLOSURE:
                        return "OPC_MAKECLOSURE";
                    case Instruction::OPC_CALLFUNCTION:
                        return "OPC_CALLFUNCTION";
                    case Instruction::OPC_CALLMETHOD:
                        return "OPC_CALLMETHOD";
                    case Instruction::OPC_CLASSINVOKETHIS:
                        return "OPC_CLASSINVOKETHIS";
                    case Instruction::OPC_CLASSGETTHIS:
                        return "OPC_CLASSGETTHIS";
                    case Instruction::OPC_RETURN:
                        return "OPC_RETURN";
                    case Instruction::OPC_MAKECLASS:
                        return "OPC_MAKECLASS";
                    case Instruction::OPC_MAKEMETHOD:
                        return "OPC_MAKEMETHOD";
                    case Instruction::OPC_CLASSPROPERTYDEFINE:
                        return "OPC_CLASSPROPERTYDEFINE";
                    case Instruction::OPC_CLASSINHERIT:
                        return "OPC_CLASSINHERIT";
                    case Instruction::OPC_CLASSGETSUPER:
                        return "OPC_CLASSGETSUPER";
                    case Instruction::OPC_CLASSINVOKESUPER:
                        return "OPC_CLASSINVOKESUPER";
                    case Instruction::OPC_CLASSINVOKESUPERSELF:
                        return "OPC_CLASSINVOKESUPERSELF";
                    case Instruction::OPC_MAKERANGE:
                        return "OPC_MAKERANGE";
                    case Instruction::OPC_MAKEARRAY:
                        return "OPC_MAKEARRAY";
                    case Instruction::OPC_MAKEDICT:
                        return "OPC_MAKEDICT";
                    case Instruction::OPC_INDEXGET:
                        return "OPC_INDEXGET";
                    case Instruction::OPC_INDEXGETRANGED:
                        return "OPC_INDEXGETRANGED";
                    case Instruction::OPC_INDEXSET:
                        return "OPC_INDEXSET";
                    case Instruction::OPC_EXTRY:
                        return "OPC_EXTRY";
                    case Instruction::OPC_EXPOPTRY:
                        return "OPC_EXPOPTRY";
                    case Instruction::OPC_EXPUBLISHTRY:
                        return "OPC_EXPUBLISHTRY";
                    case Instruction::OPC_STRINGIFY:
                        return "OPC_STRINGIFY";
                    case Instruction::OPC_SWITCH:
                        return "OPC_SWITCH";
                    case Instruction::OPC_TYPEOF:
                        return "OPC_TYPEOF";
                    case Instruction::OPC_BREAK_PL:
                        return "OPC_BREAK_PL";
                    case Instruction::OPC_OPINSTANCEOF:
                        return "OPC_OPINSTANCEOF";
                    case Instruction::OPC_HALT:
                        return "OPC_HALT";
                }
                return "<?unknown?>";
            }

        public:
            IOStream* m_outstream;

        public:
            Debug(IOStream* pr)
            {
                m_outstream = pr;
            }

            void disasmBlob(Blob* blob, const char* name)
            {
                int offset;
                m_outstream->format("== compiled '%s' [[\n", name);
                for(offset = 0; offset < blob->m_count;)
                {
                    offset = printInstructionAt(blob, offset);
                }
                m_outstream->format("]]\n");
            }

            void printInstructionName(const char* name)
            {
                m_outstream->format("%s%-16s%s ", Util::termColor(NEON_COLOR_RED), name, Util::termColor(NEON_COLOR_RESET));
            }

            int printSimpleInstruction(const char* name, int offset)
            {
                printInstructionName(name);
                m_outstream->format("\n");
                return offset + 1;
            }

            int printConstInstruction(const char* name, Blob* blob, int offset)
            {
                uint16_t constant;
                constant = (blob->m_instrucs[offset + 1].code << 8) | blob->m_instrucs[offset + 2].code;
                printInstructionName(name);
                m_outstream->format("%8d ", constant);
                ValPrinter::printValue(m_outstream, blob->m_constants.get(constant), true, false);
                m_outstream->format("\n");
                return offset + 3;
            }

            int printPropertyInstruction(const char* name, Blob* blob, int offset)
            {
                const char* proptn;
                uint16_t constant;
                constant = (blob->m_instrucs[offset + 1].code << 8) | blob->m_instrucs[offset + 2].code;
                printInstructionName(name);
                m_outstream->format("%8d ", constant);
                ValPrinter::printValue(m_outstream, blob->m_constants.get(constant), true, false);
                proptn = "";
                if(blob->m_instrucs[offset + 3].code == 1)
                {
                    proptn = "static";
                }
                m_outstream->format(" (%s)", proptn);
                m_outstream->format("\n");
                return offset + 4;
            }

            int printShortInstruction(const char* name, Blob* blob, int offset)
            {
                uint16_t slot;
                slot = (blob->m_instrucs[offset + 1].code << 8) | blob->m_instrucs[offset + 2].code;
                printInstructionName(name);
                m_outstream->format("%8d\n", slot);
                return offset + 3;
            }

            int printByteInstruction(const char* name, Blob* blob, int offset)
            {
                uint16_t slot;
                slot = blob->m_instrucs[offset + 1].code;
                printInstructionName(name);
                m_outstream->format("%8d\n", slot);
                return offset + 2;
            }

            int printJumpInstruction(const char* name, int sign, Blob* blob, int offset)
            {
                uint16_t jump;
                jump = (uint16_t)(blob->m_instrucs[offset + 1].code << 8);
                jump |= blob->m_instrucs[offset + 2].code;
                printInstructionName(name);
                m_outstream->format("%8d -> %d\n", offset, offset + 3 + sign * jump);
                return offset + 3;
            }

            int printTryInstruction(const char* name, Blob* blob, int offset)
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
                printInstructionName(name);
                m_outstream->format("%8d -> %d, %d\n", type, address, finally);
                return offset + 7;
            }

            int printInvokeInstruction(const char* name, Blob* blob, int offset)
            {
                uint16_t constant;
                uint16_t argcount;
                constant = (uint16_t)(blob->m_instrucs[offset + 1].code << 8);
                constant |= blob->m_instrucs[offset + 2].code;
                argcount = blob->m_instrucs[offset + 3].code;
                printInstructionName(name);
                m_outstream->format("(%d args) %8d ", argcount, constant);
                ValPrinter::printValue(m_outstream, blob->m_constants.get(constant), true, false);
                m_outstream->format("\n");
                return offset + 4;
            }

            int printClosureInstruction(const char* name, Blob* blob, int offset)
            {
                int j;
                int islocal;
                uint16_t index;
                uint16_t constant;
                const char* locn;
                Function* function;
                offset++;
                constant = blob->m_instrucs[offset++].code << 8;
                constant |= blob->m_instrucs[offset++].code;
                m_outstream->format("%-16s %8d ", name, constant);
                ValPrinter::printValue(m_outstream, blob->m_constants.get(constant), true, false);
                m_outstream->format("\n");
                function = blob->m_constants.get(constant).asFunction();
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
                    m_outstream->format("%04d      |                     %s %d\n", offset - 3, locn, (int)index);
                }
                return offset;
            }

            int printInstructionAt(Blob* blob, int offset)
            {
                uint16_t instruction;
                const char* opname;
                m_outstream->format("%08d ", offset);
                if(offset > 0 && blob->m_instrucs[offset].fromsourceline == blob->m_instrucs[offset - 1].fromsourceline)
                {
                    m_outstream->format("       | ");
                }
                else
                {
                    m_outstream->format("%8d ", blob->m_instrucs[offset].fromsourceline);
                }
                instruction = blob->m_instrucs[offset].code;
                opname = Debug::opcodeToString(instruction);
                switch(instruction)
                {
                    case Instruction::OPC_JUMPIFFALSE:
                        return printJumpInstruction(opname, 1, blob, offset);
                    case Instruction::OPC_JUMPNOW:
                        return printJumpInstruction(opname, 1, blob, offset);
                    case Instruction::OPC_EXTRY:
                        return printTryInstruction(opname, blob, offset);
                    case Instruction::OPC_LOOP:
                        return printJumpInstruction(opname, -1, blob, offset);
                    case Instruction::OPC_GLOBALDEFINE:
                        return printConstInstruction(opname, blob, offset);
                    case Instruction::OPC_GLOBALGET:
                        return printConstInstruction(opname, blob, offset);
                    case Instruction::OPC_GLOBALSET:
                        return printConstInstruction(opname, blob, offset);
                    case Instruction::OPC_LOCALGET:
                        return printShortInstruction(opname, blob, offset);
                    case Instruction::OPC_LOCALSET:
                        return printShortInstruction(opname, blob, offset);
                    case Instruction::OPC_FUNCARGOPTIONAL:
                        return printShortInstruction(opname, blob, offset);
                    case Instruction::OPC_FUNCARGGET:
                        return printShortInstruction(opname, blob, offset);
                    case Instruction::OPC_FUNCARGSET:
                        return printShortInstruction(opname, blob, offset);
                    case Instruction::OPC_PROPERTYGET:
                        return printConstInstruction(opname, blob, offset);
                    case Instruction::OPC_PROPERTYGETSELF:
                        return printConstInstruction(opname, blob, offset);
                    case Instruction::OPC_PROPERTYSET:
                        return printConstInstruction(opname, blob, offset);
                    case Instruction::OPC_UPVALUEGET:
                        return printShortInstruction(opname, blob, offset);
                    case Instruction::OPC_UPVALUESET:
                        return printShortInstruction(opname, blob, offset);
                    case Instruction::OPC_EXPOPTRY:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_EXPUBLISHTRY:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PUSHCONSTANT:
                        return printConstInstruction(opname, blob, offset);
                    case Instruction::OPC_EQUAL:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMGREATER:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMLESSTHAN:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PUSHEMPTY:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PUSHNULL:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PUSHTRUE:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PUSHFALSE:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMADD:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMSUBTRACT:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMMULTIPLY:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMDIVIDE:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMMODULO:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMPOW:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMNEGATE:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMNOT:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMBITNOT:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMAND:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMOR:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMBITXOR:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMSHIFTLEFT:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PRIMSHIFTRIGHT:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_PUSHONE:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_TYPEOF:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_ECHO:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_STRINGIFY:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_EXTHROW:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_POPONE:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_OPINSTANCEOF:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_UPVALUECLOSE:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_DUPONE:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_ASSERT:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_POPN:
                        return printShortInstruction(opname, blob, offset);
                        /* non-user objects... */
                    case Instruction::OPC_SWITCH:
                        return printShortInstruction(opname, blob, offset);
                        /* data container manipulators */
                    case Instruction::OPC_MAKERANGE:
                        return printShortInstruction(opname, blob, offset);
                    case Instruction::OPC_MAKEARRAY:
                        return printShortInstruction(opname, blob, offset);
                    case Instruction::OPC_MAKEDICT:
                        return printShortInstruction(opname, blob, offset);
                    case Instruction::OPC_INDEXGET:
                        return printByteInstruction(opname, blob, offset);
                    case Instruction::OPC_INDEXGETRANGED:
                        return printByteInstruction(opname, blob, offset);
                    case Instruction::OPC_INDEXSET:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_MAKECLOSURE:
                        return printClosureInstruction(opname, blob, offset);
                    case Instruction::OPC_CALLFUNCTION:
                        return printByteInstruction(opname, blob, offset);
                    case Instruction::OPC_CALLMETHOD:
                        return printInvokeInstruction(opname, blob, offset);
                    case Instruction::OPC_CLASSINVOKETHIS:
                        return printInvokeInstruction(opname, blob, offset);
                    case Instruction::OPC_RETURN:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_CLASSGETTHIS:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_MAKECLASS:
                        return printConstInstruction(opname, blob, offset);
                    case Instruction::OPC_MAKEMETHOD:
                        return printConstInstruction(opname, blob, offset);
                    case Instruction::OPC_CLASSPROPERTYDEFINE:
                        return printPropertyInstruction(opname, blob, offset);
                    case Instruction::OPC_CLASSGETSUPER:
                        return printConstInstruction(opname, blob, offset);
                    case Instruction::OPC_CLASSINHERIT:
                        return printSimpleInstruction(opname, offset);
                    case Instruction::OPC_CLASSINVOKESUPER:
                        return printInvokeInstruction(opname, blob, offset);
                    case Instruction::OPC_CLASSINVOKESUPERSELF:
                        return printByteInstruction(opname, blob, offset);
                    case Instruction::OPC_HALT:
                        return printByteInstruction(opname, blob, offset);
                    default:
                    {
                        m_outstream->format("unknown opcode %d\n", instruction);
                    }
                    break;
                }
                return offset + 1;
            }
    };

    void initBuiltinObjects()
    {
        installObjProcess();
        installObjObject();
        installObjNumber();
        installObjString();
        installObjArray();
        installObjDict();
        installObjFile();
        installObjDirectory();
        installObjRange();
        installModMath();
    }

    /**
     * procuce a stacktrace array; it is an object array, because it used both internally and in scripts.
     * cannot take any shortcuts here.
     */
    Value SharedState::vmExceptionGetStackTrace()
    {
        int line;
        int64_t i;
        size_t instruction;
        const char* fnname;
        const char* physfile;
        CallFrame* frame;
        Function* function;
        String* os;
        Array* oa;
        IOStream pr;
        oa = Array::make();
        {
            for(i = 0; i < m_vmstate.framecount; i++)
            {
                IOStream::makeStackString(&pr);
                frame = &m_vmstate.framevalues[i];
                function = frame->closure->m_fnvals.fnclosure.scriptfunc;
                /* -1 because the IP is sitting on the next instruction to be executed */
                instruction = frame->inscode - function->m_fnvals.fnscriptfunc.blob->m_instrucs.data() - 1;
                line = function->m_fnvals.fnscriptfunc.blob->m_instrucs[instruction].fromsourceline;
                physfile = "(unknown)";
                if(function->m_fnvals.fnscriptfunc.module->m_physicalpath != nullptr)
                {
                    physfile = function->m_fnvals.fnscriptfunc.module->m_physicalpath->data();
                }
                fnname = "<script>";
                if(function->m_funcname != nullptr)
                {
                    fnname = function->m_funcname->data();
                }
                pr.format("from %s() in %s:%d", fnname, physfile, line);
                os = pr.takeString();
                IOStream::destroy(&pr);
                oa->push(Value::fromObject(os));
                if((i > 15) && (m_conf.showfullstack == false))
                {
                    IOStream::makeStackString(&pr);
                    pr.format("(only upper 15 entries shown)");
                    os = pr.takeString();
                    IOStream::destroy(&pr);
                    oa->push(Value::fromObject(os));
                    break;
                }
            }
            return Value::fromObject(oa);
        }
        return Value::fromObject(String::intern("", 0));
    }

    /**
     * when an exception occured that was not caught, it is handled here.
     */
    bool SharedState::vmExceptionPropagate()
    {
        int i;
        int cnt;
        int srcline;
        const char* colred;
        const char* colreset;
        const char* colyellow;
        const char* colblue;
        const char* srcfile;
        Value stackitm;
        Array* oa;
        Function* function;
        CallFrame::ExceptionInfo* handler;
        String* emsg;
        String* tmp;
        Instance* exception;
        Property* field;
        exception = vmStackPeek(0).asInstance();
        /* look for a handler .... */
        while(m_vmstate.framecount > 0)
        {
            m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
            for(i = m_vmstate.currentframe->m_handlercount; i > 0; i--)
            {
                handler = &m_vmstate.currentframe->handlers[i - 1];
                function = m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc;
                if(handler->address != 0 /*&& Class::isInstanceOf(exception->m_instanceclass, handler->handlerklass)*/)
                {
                    m_vmstate.currentframe->inscode = &function->m_fnvals.fnscriptfunc.blob->m_instrucs[handler->address];
                    return true;
                }
                else if(handler->finallyaddress != 0)
                {
                    /* continue propagating once the 'finally' block completes */
                    vmStackPush(Value::makeBool(true));
                    m_vmstate.currentframe->inscode = &function->m_fnvals.fnscriptfunc.blob->m_instrucs[handler->finallyaddress];
                    return true;
                }
            }
            m_vmstate.framecount--;
        }
        m_vmstate.m_unhandledexceptionstate = true;
        /* at this point, the exception is unhandled; so, print it out. */
        colred = Util::termColor(NEON_COLOR_RED);
        colblue = Util::termColor(NEON_COLOR_BLUE);
        colreset = Util::termColor(NEON_COLOR_RESET);
        colyellow = Util::termColor(NEON_COLOR_YELLOW);
        m_debugwriter->format("%sunhandled %s%s", colred, exception->m_instanceclass->m_classname->data(), colreset);
        srcfile = "none";
        srcline = 0;
        field = exception->m_instanceprops.getfieldbycstr("srcline");
        if(field != nullptr)
        {
            /* why does this happen? */
            if(field->value.isNumber())
            {
                srcline = field->value.asNumber();
            }
        }
        field = exception->m_instanceprops.getfieldbycstr("srcfile");
        if(field != nullptr)
        {
            if(field->value.isString())
            {
                tmp = field->value.asString();
                srcfile = tmp->data();
            }
        }
        m_debugwriter->format(" [from native %s%s:%d%s]", colyellow, srcfile, srcline, colreset);
        field = exception->m_instanceprops.getfieldbycstr("message");
        if(field != nullptr)
        {
            emsg = Value::toString(field->value);
            if(emsg->length() > 0)
            {
                m_debugwriter->format( ": %s", emsg->data());
            }
            else
            {
                m_debugwriter->format(":");
            }
        }
        m_debugwriter->format("\n");
        field = exception->m_instanceprops.getfieldbycstr("stacktrace");
        if(field != nullptr)
        {
            m_debugwriter->format("%sstacktrace%s:\n", colblue, colreset);
            oa = field->value.asArray();
            cnt = oa->count();
            i = cnt - 1;
            if(cnt > 0)
            {
                while(true)
                {
                    stackitm = oa->get(i);
                    m_debugwriter->format("%s", colyellow);
                    m_debugwriter->format("  ");
                    ValPrinter::printValue(m_debugwriter, stackitm, false, true);
                    m_debugwriter->format("%s\n", colreset);
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

    /**
     * push an exception handler, assuming it does not exceed CallFrame::CONF_MAXEXCEPTHANDLERS.
     */
    bool SharedState::vmExceptionPushHandler(Class* type, int address, int finallyaddress)
    {
        CallFrame* frame;
        (void)type;
        frame = &m_vmstate.framevalues[m_vmstate.framecount - 1];
        if(frame->m_handlercount == CallFrame::CONF_MAXEXCEPTHANDLERS)
        {
            raiseFatalError("too many nested exception handlers in one function");
            return false;
        }
        frame->handlers[frame->m_handlercount].address = address;
        frame->handlers[frame->m_handlercount].finallyaddress = finallyaddress;
        /*frame->handlers[frame->m_handlercount].handlerklass = type;*/
        frame->m_handlercount++;
        return true;
    }

    /**
     * create an instance of an exception class.
     */
    Instance* makeExceptionInstance(Class* exklass, const char* srcfile, int srcline, String* message)
    {
        Instance* instance;
        String* osfile;
        auto gcs = SharedState::get();
        instance = Instance::make(exklass);
        osfile = String::copy(srcfile);
        gcs->vmStackPush(Value::fromObject(instance));
        instance->defProperty(String::intern("class"), Value::fromObject(exklass));
        instance->defProperty(String::intern("message"), Value::fromObject(message));
        instance->defProperty(String::intern("srcfile"), Value::fromObject(osfile));
        instance->defProperty(String::intern("srcline"), Value::makeNumber(srcline));
        gcs->vmStackPop();
        return instance;
    }

    bool defineGlobalValue(const char* name, Value val)
    {
        bool r;
        String* oname;
        oname = String::intern(name);
        auto gcs = SharedState::get();
        gcs->vmStackPush(Value::fromObject(oname));
        gcs->vmStackPush(val);
        r = gcs->m_declaredglobals.set(gcs->m_vmstate.stackvalues[0], gcs->m_vmstate.stackvalues[1]);
        gcs->vmStackPop(2);
        return r;
    }

    bool defineGlobalNativeFuncPtr(const char* name, NativeFN fptr, void* uptr)
    {
        Function* func;
        func = Function::makeFuncNative(fptr, name, uptr);
        return defineGlobalValue(name, Value::fromObject(func));
    }

    bool defineGlobalNativeFunction(const char* name, NativeFN fptr)
    {
        return defineGlobalNativeFuncPtr(name, fptr, nullptr);
    }

    /*
     * $keeplast: whether to emit code that retains or discards the value of the last statement/expression.
     * SHOULD NOT BE USED FOR ORDINARY SCRIPTS as it will almost definitely result in the stack containing invalid values.
     */
    Function* compileSourceIntern(Module* module, const char* source, Blob* blob, bool keeplast)
    {
        AstLexer* lexer;
        AstParser* parser;
        Function* function;
        (void)blob;
        auto gcs = SharedState::get();
        lexer = AstLexer::make(source);
        parser = AstParser::make(lexer, module, keeplast);
        AstParser::FuncCompiler fnc(parser, Function::CTXTYPE_SCRIPT, true);
        parser->runparser();
        function = parser->endcompiler(true);
        if(!parser->m_haderror)
        {
            if(gcs->m_conf.dumpbytecode)
            {
                Debug dbg(gcs->m_debugwriter);
                dbg.disasmBlob(parser->currentblob(), "<file>");
            }
        }
        else
        {
            function = nullptr;
        }
        AstLexer::destroy(lexer);
        AstParser::destroy(parser);
        return function;
    }

    static Value objfnarray_constructor(const FuncContext& scfn)
    {
        int cnt;
        Value filler;
        Array* arr;
        ArgCheck check("constructor", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 1, 2);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        filler = Value::makeNull();
        if(scfn.argc > 1)
        {
            filler = scfn.argv[1];
        }
        cnt = scfn.argv[0].asNumber();
        arr = Array::makeFilled(cnt, filler);
        return Value::fromObject(arr);
    }

    static Value objfnarray_join(const FuncContext& scfn)
    {
        bool havejoinee;
        size_t i;
        size_t count;
        IOStream pr;
        Value ret;
        Value vjoinee;
        Array* selfarr;
        String* joinee;
        Value* list;
        selfarr = scfn.thisval.asArray();
        joinee = nullptr;
        havejoinee = false;
        if(scfn.argc > 0)
        {
            vjoinee = scfn.argv[0];
            if(vjoinee.isString())
            {
                joinee = vjoinee.asString();
                havejoinee = true;
            }
            else
            {
                joinee = Value::toString(vjoinee);
                havejoinee = true;
            }
        }
        list = (Value*)selfarr->data();
        count = selfarr->count();
        if(count == 0)
        {
            return Value::fromObject(String::intern(""));
        }
        IOStream::makeStackString(&pr);
        for(i = 0; i < count; i++)
        {
            ValPrinter::printValue(&pr, list[i], false, true);
            if((havejoinee && (joinee != nullptr)) && ((i + 1) < count))
            {
                pr.writeString(joinee->data(), joinee->length());
            }
        }
        ret = Value::fromObject(pr.takeString());
        IOStream::destroy(&pr);
        return ret;
    }

    static Value objfnarray_length(const FuncContext& scfn)
    {
        Array* selfarr;
        ArgCheck check("length", scfn);
        selfarr = scfn.thisval.asArray();
        return Value::makeNumber(selfarr->count());
    }

    static Value objfnarray_append(const FuncContext& scfn)
    {
        size_t i;
        for(i = 0; i < scfn.argc; i++)
        {
            scfn.thisval.asArray()->push(scfn.argv[i]);
        }
        return Value::makeNull();
    }

    static Value objfnarray_clear(const FuncContext& scfn)
    {
        ArgCheck check("clear", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        scfn.thisval.asArray()->m_objvarray.deInit();
        return Value::makeNull();
    }

    static Value objfnarray_clone(const FuncContext& scfn)
    {
        Array* list;
        ArgCheck check("clone", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        list = scfn.thisval.asArray();
        return Value::fromObject(list->copy(0, list->count()));
    }

    static Value objfnarray_count(const FuncContext& scfn)
    {
        size_t i;
        int count;
        Array* list;
        ArgCheck check("count", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        list = scfn.thisval.asArray();
        count = 0;
        for(i = 0; i < list->count(); i++)
        {
            if(Value::compareValues(list->get(i), scfn.argv[0]))
            {
                count++;
            }
        }
        return Value::makeNumber(count);
    }

    static Value objfnarray_extend(const FuncContext& scfn)
    {
        size_t i;
        Array* list;
        Array* list2;
        ArgCheck check("extend", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isArray);
        list = scfn.thisval.asArray();
        list2 = scfn.argv[0].asArray();
        for(i = 0; i < list2->count(); i++)
        {
            list->push(list2->get(i));
        }
        return Value::makeNull();
    }

    static Value objfnarray_indexof(const FuncContext& scfn)
    {
        size_t i;
        Array* list;
        ArgCheck check("indexOf", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 1, 2);
        list = scfn.thisval.asArray();
        i = 0;
        if(scfn.argc == 2)
        {
            NEON_ARGS_CHECKTYPE(check, 1, &Value::isNumber);
            i = scfn.argv[1].asNumber();
        }
        for(; i < list->count(); i++)
        {
            if(Value::compareValues(list->get(i), scfn.argv[0]))
            {
                return Value::makeNumber(i);
            }
        }
        return Value::makeNumber(-1);
    }

    static Value objfnarray_insert(const FuncContext& scfn)
    {
        (void)scfn;
        //! FIXME
        /*
        int index;
        Array* list;
        ArgCheck check("insert", scfn);
        NEON_ARGS_CHECKCOUNT(check, 2);
        NEON_ARGS_CHECKTYPE(check, 1, &Value::isNumber);
        list = scfn.thisval.asArray();
        index = (int)scfn.argv[1].asNumber();
        list->m_objvarray.insert(scfn.argv[0], index);
        */
        return Value::makeNull();
    }

    static Value objfnarray_pop(const FuncContext& scfn)
    {
        Value value;
        Array* list;
        ArgCheck check("pop", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        list = scfn.thisval.asArray();
        if(list->pop(&value))
        {
            return value;
        }
        return Value::makeNull();
    }

    static Value objfnarray_shift(const FuncContext& scfn)
    {
        (void)scfn;
        ///FIXME
        /*
        size_t i;
        size_t j;
        size_t count;
        Array* list;
        Array* newlist;
        ArgCheck check("shift", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 0, 1);
        count = 1;
        if(scfn.argc == 1)
        {
            NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
            count = scfn.argv[0].asNumber();
        }
        list = scfn.thisval.asArray();
        if(count >= list->count() || list->count() == 1)
        {
            list->m_objvarray.setCount(0);
            return Value::makeNull();
        }
        else if(count > 0)
        {
            newlist = (Array*)((Object*)Array::make());
            for(i = 0; i < count; i++)
            {
                newlist->push(list->get(0));
                for(j = 0; j < list->count(); j++)
                {
                    list->set(j, list->get(j + 1));
                }
                list->m_objvarray.pop(nullptr);
            }
            if(count == 1)
            {
                return newlist->get(0);
            }
            else
            {
                return Value::fromObject(newlist);
            }
        }
        */
        return Value::makeNull();
    }

    static Value objfnarray_removeat(const FuncContext& scfn)
    {
        size_t i;
        size_t index;
        Value value;
        Array* list;
        ArgCheck check("removeAt", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        list = scfn.thisval.asArray();
        index = scfn.argv[0].asNumber();
        if(((int)index < 0) || index >= list->count())
        {
            NEON_RETURNERROR(scfn, "list index %d out of range at remove_at()", index);
        }
        value = list->get(index);
        for(i = index; i < list->count() - 1; i++)
        {
            list->set(i, list->get(i + 1));
        }
        list->m_objvarray.pop(nullptr);
        return value;
    }

    static Value objfnarray_remove(const FuncContext& scfn)
    {
        size_t i;
        size_t index;
        Array* list;
        ArgCheck check("remove", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        list = scfn.thisval.asArray();
        index = -1;
        for(i = 0; i < list->count(); i++)
        {
            if(Value::compareValues(list->get(i), scfn.argv[0]))
            {
                index = i;
                break;
            }
        }
        if((int)index != -1)
        {
            for(i = index; i < list->count(); i++)
            {
                list->set(i, list->get(i + 1));
            }
            list->m_objvarray.pop(nullptr);
        }
        return Value::makeNull();
    }

    static Value objfnarray_reverse(const FuncContext& scfn)
    {
        int fromtop;
        Array* list;
        Array* nlist;
        ArgCheck check("reverse", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        list = scfn.thisval.asArray();
        nlist = SharedState::gcProtect(Array::make());
        /* in-place reversal:*/
        /*
        int start = 0;
        int end = list->count() - 1;
        while (start < end)
        {
            Value temp = list->get(start);
            list->set(start, list->get(end));
            list->set(end, temp);
            start++;
            end--;
        }
        */
        for(fromtop = list->count() - 1; fromtop >= 0; fromtop--)
        {
            nlist->push(list->get(fromtop));
        }
        return Value::fromObject(nlist);
    }

    static Value objfnarray_sort(const FuncContext& scfn)
    {
        Array* list;
        ArgCheck check("sort", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        list = scfn.thisval.asArray();
        Value::sortValues((Value*)list->data(), list->count());
        return Value::makeNull();
    }

    static Value objfnarray_contains(const FuncContext& scfn)
    {
        size_t i;
        Array* list;
        ArgCheck check("contains", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        list = scfn.thisval.asArray();
        for(i = 0; i < list->count(); i++)
        {
            if(Value::compareValues(scfn.argv[0], list->get(i)))
            {
                return Value::makeBool(true);
            }
        }
        return Value::makeBool(false);
    }

    static Value objfnarray_delete(const FuncContext& scfn)
    {
        (void)scfn;
        //! FIXME
        /*
        size_t i;
        size_t idxupper;
        size_t idxlower;
        Array* list;
        ArgCheck check("delete", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 1, 2);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        idxlower = scfn.argv[0].asNumber();
        idxupper = idxlower;
        if(scfn.argc == 2)
        {
            NEON_ARGS_CHECKTYPE(check, 1, &Value::isNumber);
            idxupper = scfn.argv[1].asNumber();
        }
        list = scfn.thisval.asArray();
        if(((int)idxlower < 0) || idxlower >= list->count())
        {
            NEON_RETURNERROR(scfn, "list index %d out of range at delete()", idxlower);
        }
        else if(idxupper < idxlower || idxupper >= list->count())
        {
            NEON_RETURNERROR(scfn, "invalid upper limit %d at delete()", idxupper);
        }
        for(i = 0; i < list->count() - idxupper; i++)
        {
            list->set(idxlower + i, list->get(i + idxupper + 1));
        }
        list->m_objvarray.m_listcount -= (idxupper - idxlower + 1);
        return Value::makeNumber((double)idxupper - (double)idxlower + 1);
        */
        return Value::makeNull();
    }

    static Value objfnarray_first(const FuncContext& scfn)
    {
        Array* list;
        ArgCheck check("first", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        list = scfn.thisval.asArray();
        if(list->count() > 0)
        {
            return list->get(0);
        }
        return Value::makeNull();
    }

    static Value objfnarray_last(const FuncContext& scfn)
    {
        Array* list;
        ArgCheck check("last", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        list = scfn.thisval.asArray();
        if(list->count() > 0)
        {
            return list->get(list->count() - 1);
        }
        return Value::makeNull();
    }

    static Value objfnarray_isempty(const FuncContext& scfn)
    {
        ArgCheck check("isEmpty", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        return Value::makeBool(scfn.thisval.asArray()->count() == 0);
    }

    static Value objfnarray_take(const FuncContext& scfn)
    {
        size_t count;
        Array* list;
        ArgCheck check("take", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        list = scfn.thisval.asArray();
        count = scfn.argv[0].asNumber();
        if((int)count < 0)
        {
            count = list->count() + count;
        }
        if(list->count() < count)
        {
            return Value::fromObject(list->copy(0, list->count()));
        }
        return Value::fromObject(list->copy(0, count));
    }

    static Value objfnarray_get(const FuncContext& scfn)
    {
        size_t index;
        Array* list;
        ArgCheck check("get", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        list = scfn.thisval.asArray();
        index = scfn.argv[0].asNumber();
        if((int)index < 0 || index >= list->count())
        {
            return Value::makeNull();
        }
        return list->get(index);
    }

    static Value objfnarray_compact(const FuncContext& scfn)
    {
        size_t i;
        Array* list;
        Array* newlist;
        ArgCheck check("compact", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        list = scfn.thisval.asArray();
        newlist = SharedState::gcProtect(Array::make());
        for(i = 0; i < list->count(); i++)
        {
            if(!Value::compareValues(list->get(i), Value::makeNull()))
            {
                newlist->push(list->get(i));
            }
        }
        return Value::fromObject(newlist);
    }

    static Value objfnarray_unique(const FuncContext& scfn)
    {
        size_t i;
        size_t j;
        bool found;
        Array* list;
        Array* newlist;
        ArgCheck check("unique", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        list = scfn.thisval.asArray();
        newlist = SharedState::gcProtect(Array::make());
        for(i = 0; i < list->count(); i++)
        {
            found = false;
            for(j = 0; j < newlist->count(); j++)
            {
                if(Value::compareValues(newlist->get(j), list->get(i)))
                {
                    found = true;
                    continue;
                }
            }
            if(!found)
            {
                newlist->push(list->get(i));
            }
        }
        return Value::fromObject(newlist);
    }

    static Value objfnarray_zip(const FuncContext& scfn)
    {
        size_t i;
        size_t j;
        Array* list;
        Array* newlist;
        Array* alist;
        Array** arglist;
        ArgCheck check("zip", scfn);
        list = scfn.thisval.asArray();
        newlist = SharedState::gcProtect(Array::make());
        arglist = (Array**)SharedState::gcAllocate(sizeof(Array*), scfn.argc, false);
        for(i = 0; i < scfn.argc; i++)
        {
            NEON_ARGS_CHECKTYPE(check, i, &Value::isArray);
            arglist[i] = scfn.argv[i].asArray();
        }
        for(i = 0; i < list->count(); i++)
        {
            alist = SharedState::gcProtect(Array::make());
            /* item of main list*/
            alist->push(list->get(i));
            for(j = 0; j < scfn.argc; j++)
            {
                if(i < arglist[j]->count())
                {
                    alist->push(arglist[j]->get(i));
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

    static Value objfnarray_zipfrom(const FuncContext& scfn)
    {
        size_t i;
        size_t j;
        Array* list;
        Array* newlist;
        Array* alist;
        Array* arglist;
        ArgCheck check("zipFrom", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isArray);
        list = scfn.thisval.asArray();
        newlist = SharedState::gcProtect(Array::make());
        arglist = scfn.argv[0].asArray();
        for(i = 0; i < arglist->count(); i++)
        {
            if(!arglist->get(i).isArray())
            {
                NEON_RETURNERROR(scfn, "invalid list in zip entries");
            }
        }
        for(i = 0; i < list->count(); i++)
        {
            alist = SharedState::gcProtect(Array::make());
            alist->push(list->get(i));
            for(j = 0; j < arglist->count(); j++)
            {
                if(i < arglist->get(j).asArray()->count())
                {
                    alist->push(arglist->get(j).asArray()->get(i));
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

    static Value objfnarray_todict(const FuncContext& scfn)
    {
        size_t i;
        Dict* dict;
        Array* list;
        ArgCheck check("toDict", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        dict = SharedState::gcProtect(Dict::make());
        list = scfn.thisval.asArray();
        for(i = 0; i < list->count(); i++)
        {
            dict->set(Value::makeNumber(i), list->get(i));
        }
        return Value::fromObject(dict);
    }

    static Value objfnarray_iter(const FuncContext& scfn)
    {
        size_t index;
        Array* list;
        ArgCheck check("iter", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        list = scfn.thisval.asArray();
        index = scfn.argv[0].asNumber();
        if(((int)index > -1) && index < list->count())
        {
            return list->get(index);
        }
        return Value::makeNull();
    }

    static Value objfnarray_itern(const FuncContext& scfn)
    {
        size_t index;
        Array* list;
        ArgCheck check("itern", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        list = scfn.thisval.asArray();
        if(scfn.argv[0].isNull())
        {
            if(list->count() == 0)
            {
                return Value::makeBool(false);
            }
            return Value::makeNumber(0);
        }
        if(!scfn.argv[0].isNumber())
        {
            NEON_RETURNERROR(scfn, "lists are numerically indexed");
        }
        index = scfn.argv[0].asNumber();
        if(index < list->count() - 1)
        {
            return Value::makeNumber((double)index + 1);
        }
        return Value::makeNull();
    }

    static Value objfnarray_each(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        Value callable;
        Value unused;
        Array* list;
        ArgCheck check("each", scfn);
        Value nestargs[3];
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isCallable);
        list = scfn.thisval.asArray();
        callable = scfn.argv[0];
        arity = gcs->vmNestCallPrepare(callable, scfn.thisval, nestargs, 2);
        for(i = 0; i < list->count(); i++)
        {
            passi = 0;
            if(arity > 0)
            {
                passi++;
                nestargs[0] = list->get(i);
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = Value::makeNumber(i);
                }
            }
            gcs->vmNestCallFunction(callable, scfn.thisval, nestargs, passi, &unused, false);
        }
        return Value::makeNull();
    }

    static Value objfnarray_map(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        Value res;
        Value callable;
        Array* list;
        Array* resultlist;
        ArgCheck check("map", scfn);
        Value nestargs[3];
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isCallable);
        list = scfn.thisval.asArray();
        callable = scfn.argv[0];
        arity = gcs->vmNestCallPrepare(callable, scfn.thisval, nestargs, 2);
        resultlist = SharedState::gcProtect(Array::make());
        for(i = 0; i < list->count(); i++)
        {
            passi = 0;
            if(!list->get(i).isNull())
            {
                if(arity > 0)
                {
                    passi++;
                    nestargs[0] = list->get(i);
                    if(arity > 1)
                    {
                        passi++;
                        nestargs[1] = Value::makeNumber(i);
                    }
                }
                gcs->vmNestCallFunction(callable, scfn.thisval, nestargs, passi, &res, false);
                resultlist->push(res);
            }
            else
            {
                resultlist->push(Value::makeNull());
            }
        }
        return Value::fromObject(resultlist);
    }

    static Value objfnarray_filter(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        Value callable;
        Value result;
        Array* list;
        Array* resultlist;
        ArgCheck check("filter", scfn);
        Value nestargs[3];
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isCallable);
        list = scfn.thisval.asArray();
        callable = scfn.argv[0];
        arity = gcs->vmNestCallPrepare(callable, scfn.thisval, nestargs, 2);
        resultlist = SharedState::gcProtect(Array::make());
        for(i = 0; i < list->count(); i++)
        {
            passi = 0;
            if(!list->get(i).isNull())
            {
                if(arity > 0)
                {
                    passi++;
                    nestargs[0] = list->get(i);
                    if(arity > 1)
                    {
                        passi++;
                        nestargs[1] = Value::makeNumber(i);
                    }
                }
                gcs->vmNestCallFunction(callable, scfn.thisval, nestargs, passi, &result, false);
                if(!result.isFalse())
                {
                    resultlist->push(list->get(i));
                }
            }
        }
        return Value::fromObject(resultlist);
    }

    static Value objfnarray_some(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        Value callable;
        Value result;
        Array* list;
        ArgCheck check("some", scfn);
        Value nestargs[3];
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isCallable);
        list = scfn.thisval.asArray();
        callable = scfn.argv[0];
        arity = gcs->vmNestCallPrepare(callable, scfn.thisval, nestargs, 2);
        for(i = 0; i < list->count(); i++)
        {
            passi = 0;
            if(!list->get(i).isNull())
            {
                if(arity > 0)
                {
                    passi++;
                    nestargs[0] = list->get(i);
                    if(arity > 1)
                    {
                        passi++;
                        nestargs[1] = Value::makeNumber(i);
                    }
                }
                gcs->vmNestCallFunction(callable, scfn.thisval, nestargs, passi, &result, false);
                if(!result.isFalse())
                {
                    return Value::makeBool(true);
                }
            }
        }
        return Value::makeBool(false);
    }

    static Value objfnarray_every(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        Value result;
        Value callable;
        Array* list;
        ArgCheck check("every", scfn);
        Value nestargs[3];
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isCallable);
        list = scfn.thisval.asArray();
        callable = scfn.argv[0];
        arity = gcs->vmNestCallPrepare(callable, scfn.thisval, nestargs, 2);
        for(i = 0; i < list->count(); i++)
        {
            passi = 0;
            if(!list->get(i).isNull())
            {
                if(arity > 0)
                {
                    passi++;
                    nestargs[0] = list->get(i);
                    if(arity > 1)
                    {
                        passi++;
                        nestargs[1] = Value::makeNumber(i);
                    }
                }
                gcs->vmNestCallFunction(callable, scfn.thisval, nestargs, passi, &result, false);
                if(result.isFalse())
                {
                    return Value::makeBool(false);
                }
            }
        }
        return Value::makeBool(true);
    }

    static Value objfnarray_reduce(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        size_t startindex;
        Value callable;
        Value accumulator;
        Array* list;
        ArgCheck check("reduce", scfn);
        Value nestargs[5];
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKCOUNTRANGE(check, 1, 2);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isCallable);
        list = scfn.thisval.asArray();
        callable = scfn.argv[0];
        startindex = 0;
        accumulator = Value::makeNull();
        if(scfn.argc == 2)
        {
            accumulator = scfn.argv[1];
        }
        if(accumulator.isNull() && list->count() > 0)
        {
            accumulator = list->get(0);
            startindex = 1;
        }
        arity = gcs->vmNestCallPrepare(callable, scfn.thisval, nullptr, 4);
        for(i = startindex; i < list->count(); i++)
        {
            passi = 0;
            if(!list->get(i).isNull() && !list->get(i).isNull())
            {
                if(arity > 0)
                {
                    passi++;
                    nestargs[0] = accumulator;
                    if(arity > 1)
                    {
                        passi++;
                        nestargs[1] = list->get(i);
                        if(arity > 2)
                        {
                            passi++;
                            nestargs[2] = Value::makeNumber(i);
                            if(arity > 4)
                            {
                                passi++;
                                nestargs[3] = scfn.thisval;
                            }
                        }
                    }
                }
                gcs->vmNestCallFunction(callable, scfn.thisval, nestargs, passi, &accumulator, false);
            }
        }
        return accumulator;
    }

    static Value objfnarray_slice(const FuncContext& scfn)
    {
        int64_t i;
        int64_t until;
        int64_t start;
        int64_t end;
        int64_t ibegin;
        int64_t iend;
        int64_t salen;
        bool backwards;
        Array* selfarr;
        Array* narr;
        ArgCheck check("slice", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 1, 2);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        selfarr = scfn.thisval.asArray();
        salen = selfarr->count();
        end = salen;
        start = scfn.argv[0].asNumber();
        backwards = false;
        if(start < 0)
        {
            backwards = true;
        }
        if(scfn.argc > 1)
        {
            end = scfn.argv[1].asNumber();
        }
        narr = Array::make();
        i = 0;
        if(backwards)
        {
            i = (end - start);
            until = 0;
            ibegin = ((salen + start) - 0);
            iend = end + 0;
        }
        else
        {
            until = end;
            ibegin = start;
            iend = until;
        }
        for(i = ibegin; i != iend; i++)
        {
            narr->push(selfarr->get(i));
        }
        return Value::fromObject(narr);
    }

    void installObjArray()
    {
        /* clang-format off */
        static Class::ConstItem arraymethods[] =
        {
            { "size", objfnarray_length },
            { "join", objfnarray_join },
            { "append", objfnarray_append },
            { "push", objfnarray_append },
            { "clear", objfnarray_clear },
            { "clone", objfnarray_clone },
            { "count", objfnarray_count },
            { "extend", objfnarray_extend },
            { "indexOf", objfnarray_indexof },
            { "insert", objfnarray_insert },
            { "pop", objfnarray_pop },
            { "shift", objfnarray_shift },
            { "removeAt", objfnarray_removeat },
            { "remove", objfnarray_remove },
            { "reverse", objfnarray_reverse },
            { "sort", objfnarray_sort },
            { "contains", objfnarray_contains },
            { "delete", objfnarray_delete },
            { "first", objfnarray_first },
            { "last", objfnarray_last },
            { "isEmpty", objfnarray_isempty },
            { "take", objfnarray_take },
            { "get", objfnarray_get },
            { "compact", objfnarray_compact },
            { "unique", objfnarray_unique },
            { "zip", objfnarray_zip },
            { "zipFrom", objfnarray_zipfrom },
            { "toDict", objfnarray_todict },
            { "each", objfnarray_each },
            { "map", objfnarray_map },
            { "filter", objfnarray_filter },
            { "some", objfnarray_some },
            { "every", objfnarray_every },
            { "reduce", objfnarray_reduce },
            { "slice", objfnarray_slice },
            { "@iter", objfnarray_iter },
            { "@itern", objfnarray_itern },
            { nullptr, nullptr } };
        /* clang-format on */
        auto gcs = SharedState::get();
        gcs->m_classprimarray->defNativeConstructor(objfnarray_constructor);
        gcs->m_classprimarray->defCallableField(String::intern("length"), objfnarray_length);
        gcs->m_classprimarray->installMethods(arraymethods);
    }

    static Value objfndict_length(const FuncContext& scfn)
    {
        ArgCheck check("length", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        return Value::makeNumber(scfn.thisval.asDict()->m_htkeys.count());
    }

    static Value objfndict_add(const FuncContext& scfn)
    {
        Value tempvalue;
        Dict* dict;
        ArgCheck check("add", scfn);
        NEON_ARGS_CHECKCOUNT(check, 2);
        dict = scfn.thisval.asDict();
        if(dict->m_htvalues.get(scfn.argv[0], &tempvalue))
        {
            NEON_RETURNERROR(scfn, "duplicate key %s at add()", Value::toString(scfn.argv[0])->data());
        }
        dict->add(scfn.argv[0], scfn.argv[1]);
        return Value::makeNull();
    }

    static Value objfndict_set(const FuncContext& scfn)
    {
        Value value;
        Dict* dict;
        ArgCheck check("set", scfn);
        NEON_ARGS_CHECKCOUNT(check, 2);
        dict = scfn.thisval.asDict();
        if(!dict->m_htvalues.get(scfn.argv[0], &value))
        {
            dict->add(scfn.argv[0], scfn.argv[1]);
        }
        else
        {
            dict->set(scfn.argv[0], scfn.argv[1]);
        }
        return Value::makeNull();
    }

    static Value objfndict_clear(const FuncContext& scfn)
    {
        Dict* dict;
        ArgCheck check("clear", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        dict = scfn.thisval.asDict();
        dict->m_htkeys.deInit();
        dict->m_htvalues.deInit();
        return Value::makeNull();
    }

    static Value objfndict_clone(const FuncContext& scfn)
    {
        size_t i;
        Dict* dict;
        Dict* newdict;
        ArgCheck check("clone", scfn);
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKCOUNT(check, 0);
        dict = scfn.thisval.asDict();
        newdict = SharedState::gcProtect(Dict::make());
        if(!dict->m_htvalues.copyTo(&newdict->m_htvalues, true))
        {
            NEON_THROWCLASSWITHSOURCEINFO(gcs->m_exceptions.argumenterror, "failed to copy table");
            return Value::makeNull();
        }
        for(i = 0; i < dict->m_htkeys.count(); i++)
        {
            newdict->m_htkeys.push(dict->m_htkeys.get(i));
        }
        return Value::fromObject(newdict);
    }

    static Value objfndict_compact(const FuncContext& scfn)
    {
        size_t i;
        Dict* dict;
        Dict* newdict;
        Value tmpvalue;
        ArgCheck check("compact", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        dict = scfn.thisval.asDict();
        newdict = (Dict*)SharedState::gcProtect(Dict::make());
        tmpvalue = Value::makeNull();
        for(i = 0; i < dict->m_htkeys.count(); i++)
        {
            dict->m_htvalues.get(dict->m_htkeys.get(i), &tmpvalue);
            if(!Value::compareValues(tmpvalue, Value::makeNull()))
            {
                newdict->add(dict->m_htkeys.get(i), tmpvalue);
            }
        }
        return Value::fromObject(newdict);
    }

    static Value objfndict_contains(const FuncContext& scfn)
    {
        Value value;
        Dict* dict;
        ArgCheck check("contains", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        dict = scfn.thisval.asDict();
        return Value::makeBool(dict->m_htvalues.get(scfn.argv[0], &value));
    }

    static Value objfndict_extend(const FuncContext& scfn)
    {
        size_t i;
        Value tmp;
        Dict* dict;
        Dict* dictcpy;
        ArgCheck check("extend", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isDict);
        dict = scfn.thisval.asDict();
        dictcpy = scfn.argv[0].asDict();
        for(i = 0; i < dictcpy->m_htkeys.count(); i++)
        {
            if(!dict->m_htvalues.get(dictcpy->m_htkeys.get(i), &tmp))
            {
                dict->m_htkeys.push(dictcpy->m_htkeys.get(i));
            }
        }
        dictcpy->m_htvalues.copyTo(&dict->m_htvalues, true);
        return Value::makeNull();
    }

    static Value objfndict_get(const FuncContext& scfn)
    {
        Dict* dict;
        Property* field;
        ArgCheck check("get", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 1, 2);
        dict = scfn.thisval.asDict();
        field = dict->get(scfn.argv[0]);
        if(field == nullptr)
        {
            if(scfn.argc == 1)
            {
                return Value::makeNull();
            }
            else
            {
                return scfn.argv[1];
            }
        }
        return field->value;
    }

    static Value objfndict_keys(const FuncContext& scfn)
    {
        size_t i;
        Dict* dict;
        Array* list;
        ArgCheck check("keys", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        dict = scfn.thisval.asDict();
        list = SharedState::gcProtect(Array::make());
        for(i = 0; i < dict->m_htkeys.count(); i++)
        {
            list->push(dict->m_htkeys.get(i));
        }
        return Value::fromObject(list);
    }

    static Value objfndict_values(const FuncContext& scfn)
    {
        size_t i;
        Dict* dict;
        Array* list;
        Property* field;
        ArgCheck check("values", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        dict = scfn.thisval.asDict();
        list =SharedState::gcProtect(Array::make());
        for(i = 0; i < dict->m_htkeys.count(); i++)
        {
            field = dict->get(dict->m_htkeys.get(i));
            list->push(field->value);
        }
        return Value::fromObject(list);
    }

    static Value objfndict_remove(const FuncContext& scfn)
    {
        size_t i;
        int index;
        Value value;
        Dict* dict;
        ArgCheck check("remove", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        dict = scfn.thisval.asDict();
        if(dict->m_htvalues.get(scfn.argv[0], &value))
        {
            dict->m_htvalues.remove(scfn.argv[0]);
            index = -1;
            for(i = 0; i < dict->m_htkeys.count(); i++)
            {
                if(Value::compareValues(dict->m_htkeys.get(i), scfn.argv[0]))
                {
                    index = i;
                    break;
                }
            }
            for(i = index; i < dict->m_htkeys.count(); i++)
            {
                dict->m_htkeys.set(i, dict->m_htkeys.get(i + 1));
            }
            dict->m_htkeys.pop(nullptr);
            return value;
        }
        return Value::makeNull();
    }

    static Value objfndict_isempty(const FuncContext& scfn)
    {
        ArgCheck check("isempty", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        return Value::makeBool(scfn.thisval.asDict()->m_htkeys.count() == 0);
    }

    static Value objfndict_findkey(const FuncContext& scfn)
    {
        ArgCheck check("findkey", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        auto ht = scfn.thisval.asDict()->m_htvalues;
        return ht.findkey(scfn.argv[0], Value::makeNull());
    }

    static Value objfndict_tolist(const FuncContext& scfn)
    {
        size_t i;
        Array* list;
        Dict* dict;
        Array* namelist;
        Array* valuelist;
        ArgCheck check("tolist", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        dict = scfn.thisval.asDict();
        namelist = SharedState::gcProtect(Array::make());
        valuelist = SharedState::gcProtect(Array::make());
        for(i = 0; i < dict->m_htkeys.count(); i++)
        {
            namelist->push(dict->m_htkeys.get(i));
            Value value;
            if(dict->m_htvalues.get(dict->m_htkeys.get(i), &value))
            {
                valuelist->push(value);
            }
            else
            {
                /* theoretically impossible */
                valuelist->push(Value::makeNull());
            }
        }
        list = SharedState::gcProtect(Array::make());
        list->push(Value::fromObject(namelist));
        list->push(Value::fromObject(valuelist));
        return Value::fromObject(list);
    }

    static Value objfndict_iter(const FuncContext& scfn)
    {
        Value result;
        Dict* dict;
        ArgCheck check("iter", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        dict = scfn.thisval.asDict();
        if(dict->m_htvalues.get(scfn.argv[0], &result))
        {
            return result;
        }
        return Value::makeNull();
    }

    static Value objfndict_itern(const FuncContext& scfn)
    {
        size_t i;
        Dict* dict;
        ArgCheck check("itern", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        dict = scfn.thisval.asDict();
        if(scfn.argv[0].isNull())
        {
            if(dict->m_htkeys.count() == 0)
            {
                return Value::makeBool(false);
            }
            return dict->m_htkeys.get(0);
        }
        for(i = 0; i < dict->m_htkeys.count(); i++)
        {
            if(Value::compareValues(scfn.argv[0], dict->m_htkeys.get(i)) && (i + 1) < dict->m_htkeys.count())
            {
                return dict->m_htkeys.get(i + 1);
            }
        }
        return Value::makeNull();
    }

    static Value objfndict_each(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        int arity;
        Value value;
        Value callable;
        Value unused;
        Dict* dict;
        ArgCheck check("each", scfn);
        Value nestargs[3];
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isCallable);
        dict = scfn.thisval.asDict();
        callable = scfn.argv[0];
        arity = gcs->vmNestCallPrepare(callable, scfn.thisval, nestargs, 2);
        value = Value::makeNull();
        for(i = 0; i < dict->m_htkeys.count(); i++)
        {
            passi = 0;
            if(arity > 0)
            {
                dict->m_htvalues.get(dict->m_htkeys.get(i), &value);
                passi++;
                nestargs[0] = value;
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = dict->m_htkeys.get(i);
                }
            }
            gcs->vmNestCallFunction(callable, scfn.thisval, nestargs, passi, &unused, false);
        }
        return Value::makeNull();
    }

    static Value objfndict_filter(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        int arity;
        Value value;
        Value callable;
        Value result;
        Dict* dict;
        Dict* resultdict;
        ArgCheck check("filter", scfn);
        Value nestargs[3];
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isCallable);
        dict = scfn.thisval.asDict();
        callable = scfn.argv[0];
        arity = gcs->vmNestCallPrepare(callable, scfn.thisval, nestargs, 2);
        resultdict = SharedState::gcProtect(Dict::make());
        value = Value::makeNull();
        for(i = 0; i < dict->m_htkeys.count(); i++)
        {
            passi = 0;
            dict->m_htvalues.get(dict->m_htkeys.get(i), &value);
            if(arity > 0)
            {
                passi++;
                nestargs[0] = value;
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = dict->m_htkeys.get(i);
                }
            }
            gcs->vmNestCallFunction(callable, scfn.thisval, nestargs, passi, &result, false);
            if(!result.isFalse())
            {
                resultdict->add(dict->m_htkeys.get(i), value);
            }
        }
        /* pop the call list */
        return Value::fromObject(resultdict);
    }

    static Value objfndict_some(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        int arity;
        Value result;
        Value value;
        Value callable;
        Dict* dict;
        ArgCheck check("some", scfn);
        Value nestargs[3];
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isCallable);
        dict = scfn.thisval.asDict();
        callable = scfn.argv[0];
        arity = gcs->vmNestCallPrepare(callable, scfn.thisval, nestargs, 2);
        value = Value::makeNull();
        for(i = 0; i < dict->m_htkeys.count(); i++)
        {
            passi = 0;
            if(arity > 0)
            {
                dict->m_htvalues.get(dict->m_htkeys.get(i), &value);
                passi++;
                nestargs[0] = value;
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = dict->m_htkeys.get(i);
                }
            }
            gcs->vmNestCallFunction(callable, scfn.thisval, nestargs, passi, &result, false);
            if(!result.isFalse())
            {
                /* pop the call list */
                return Value::makeBool(true);
            }
        }
        /* pop the call list */
        return Value::makeBool(false);
    }

    static Value objfndict_every(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        int arity;
        Value value;
        Value callable;
        Value result;
        Dict* dict;
        ArgCheck check("every", scfn);
        Value nestargs[3];
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isCallable);
        dict = scfn.thisval.asDict();
        callable = scfn.argv[0];
        arity = gcs->vmNestCallPrepare(callable, scfn.thisval, nestargs, 2);
        value = Value::makeNull();
        for(i = 0; i < dict->m_htkeys.count(); i++)
        {
            passi = 0;
            if(arity > 0)
            {
                dict->m_htvalues.get(dict->m_htkeys.get(i), &value);
                passi++;
                nestargs[0] = value;
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = dict->m_htkeys.get(i);
                }
            }
            gcs->vmNestCallFunction(callable, scfn.thisval, nestargs, passi, &result, false);
            if(result.isFalse())
            {
                /* pop the call list */
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(true);
    }

    static Value objfndict_reduce(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        int arity;
        int startindex;
        Value value;
        Value callable;
        Value accumulator;
        Dict* dict;
        ArgCheck check("reduce", scfn);
        Value nestargs[5];
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKCOUNTRANGE(check, 1, 2);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isCallable);
        dict = scfn.thisval.asDict();
        callable = scfn.argv[0];
        startindex = 0;
        accumulator = Value::makeNull();
        if(scfn.argc == 2)
        {
            accumulator = scfn.argv[1];
        }
        if(accumulator.isNull() && dict->m_htkeys.count() > 0)
        {
            dict->m_htvalues.get(dict->m_htkeys.get(0), &accumulator);
            startindex = 1;
        }
        arity = gcs->vmNestCallPrepare(callable, scfn.thisval, nestargs, 4);
        value = Value::makeNull();
        for(i = startindex; i < dict->m_htkeys.count(); i++)
        {
            passi = 0;
            /* only call map for non-empty values in a list. */
            if(!dict->m_htkeys.get(i).isNull() && !dict->m_htkeys.get(i).isNull())
            {
                if(arity > 0)
                {
                    passi++;
                    nestargs[0] = accumulator;
                    if(arity > 1)
                    {
                        dict->m_htvalues.get(dict->m_htkeys.get(i), &value);
                        passi++;
                        nestargs[1] = value;
                        if(arity > 2)
                        {
                            passi++;
                            nestargs[2] = dict->m_htkeys.get(i);
                            if(arity > 4)
                            {
                                passi++;
                                nestargs[3] = scfn.thisval;
                            }
                        }
                    }
                }
                gcs->vmNestCallFunction(callable, scfn.thisval, nestargs, passi, &accumulator, false);
            }
        }
        return accumulator;
    }

    void installObjDict()
    {
        /* clang-format off */
        static Class::ConstItem dictmethods[] = {
            { "keys", objfndict_keys },
            { "size", objfndict_length },
            { "add", objfndict_add },
            { "set", objfndict_set },
            { "clear", objfndict_clear },
            { "clone", objfndict_clone },
            { "compact", objfndict_compact },
            { "contains", objfndict_contains },
            { "extend", objfndict_extend },
            { "get", objfndict_get },
            { "values", objfndict_values },
            { "remove", objfndict_remove },
            { "isEmpty", objfndict_isempty },
            { "findKey", objfndict_findkey },
            { "toList", objfndict_tolist },
            { "each", objfndict_each },
            { "filter", objfndict_filter },
            { "some", objfndict_some },
            { "every", objfndict_every },
            { "reduce", objfndict_reduce },
            { "@iter", objfndict_iter },
            { "@itern", objfndict_itern },
            { nullptr, nullptr },
        };
        /* clang-format on */
        auto gcs = SharedState::get();
    #if 0
        gcs->m_classprimdict->defNativeConstructor(objfndict_constructor);
        gcs->m_classprimdict->defStaticNativeMethod(String::copy("keys"), objfndict_keys);
    #endif
        gcs->m_classprimdict->installMethods(dictmethods);
    }

    static Value objfnfile_constructor(const FuncContext& scfn)
    {
        FILE* hnd;
        const char* path;
        const char* mode;
        String* opath;
        File* file;
        (void)hnd;
        ArgCheck check("constructor", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 1, 2);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        opath = scfn.argv[0].asString();
        if(opath->length() == 0)
        {
            NEON_RETURNERROR(scfn, "file path cannot be empty");
        }
        mode = "r";
        if(scfn.argc == 2)
        {
            NEON_ARGS_CHECKTYPE(check, 1, &Value::isString);
            mode = scfn.argv[1].asString()->data();
        }
        path = opath->data();
        file = SharedState::gcProtect(File::make(nullptr, false, path, mode));
        file->openWithoutParams();
        return Value::fromObject(file);
    }



    #define putorgetkey(md, name, value) \
        { \
            if(havekey && (keywanted != nullptr)) \
            { \
                if(strcmp(name, keywanted->data()) == 0) \
                { \
                    return value; \
                } \
            } \
            else \
            { \
                md->addCstr(name, value); \
            } \
        }

    Value objfnfile_stat(const FuncContext& scfn)
    {
        bool havekey;
        String* keywanted;
        String* path;
        Dict* md;
        Util::FSStat nfs;
        const char* strp;
        ArgCheck check("stat", scfn);
        auto gcs = SharedState::get();
        havekey = false;
        keywanted = nullptr;
        md = nullptr;
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        path = scfn.argv[0].asString();
        if(scfn.argc > 1)
        {
            NEON_ARGS_CHECKTYPE(check, 1, &Value::isString);
            keywanted = scfn.argv[1].asString();
            havekey = true;
        }
        strp = path->data();
        if(!nfs.fromPath(strp))
        {
            NEON_THROWCLASSWITHSOURCEINFO(gcs->m_exceptions.ioerror, "%s: %s", strp, strerror(errno));
            return Value::makeNull();
        }
        md = nullptr;
        if(!havekey)
        {
            md = Dict::make();
        }
        putorgetkey(md, "path", Value::fromObject(path));
        putorgetkey(md, "mode", Value::makeNumber(nfs.m_mode));
        putorgetkey(md, "modename", Value::fromObject(String::copy(nfs.m_modename)));
        putorgetkey(md, "inode", Value::makeNumber(nfs.m_inode));
        putorgetkey(md, "links", Value::makeNumber(nfs.m_numlinks));
        putorgetkey(md, "uid", Value::makeNumber(nfs.m_owneruid));
        putorgetkey(md, "gid", Value::makeNumber(nfs.m_ownergid));
        putorgetkey(md, "blocksize", Value::makeNumber(nfs.m_blocksize));
        putorgetkey(md, "blocks", Value::makeNumber(nfs.m_blockcount));
        putorgetkey(md, "filesize", Value::makeNumber(nfs.m_filesize));
        putorgetkey(md, "lastchanged", Value::fromObject(String::copy(Util::filestat_ctimetostring(nfs.m_tmlastchanged))));
        putorgetkey(md, "lastaccess", Value::fromObject(String::copy(Util::filestat_ctimetostring(nfs.m_tmlastaccessed))));
        putorgetkey(md, "lastmodified", Value::fromObject(String::copy(Util::filestat_ctimetostring(nfs.m_tmlastmodified))));
        return Value::fromObject(md);
    }
    #undef putorgetkey

    static Value objfnfile_basename(const FuncContext& scfn)
    {
        const char* r;
        String* path;
        ArgCheck check("basename", scfn);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        path = scfn.argv[0].asString();
        r = Util::osfn_basename(path->data());
        if(r == nullptr)
        {
            return Value::makeNull();
        }
        return Value::fromObject(String::copy(r));
    }

    static Value objfnfile_dirname(const FuncContext& scfn)
    {
        const char* r;
        String* path;
        ArgCheck check("dirname", scfn);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        path = scfn.argv[0].asString();
        r = Util::osfn_dirname(path->data());
        if(r == nullptr)
        {
            return Value::makeNull();
        }
        return Value::fromObject(String::copy(r));
    }

    static Value objfnfile_chmod(const FuncContext& scfn)
    {
        int64_t r;
        int64_t mod;
        String* path;
        ArgCheck check("chmod", scfn);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        NEON_ARGS_CHECKTYPE(check, 1, &Value::isNumber);
        path = scfn.argv[0].asString();
        mod = scfn.argv[1].asNumber();
        r = Util::osfn_chmod(path->data(), mod);
        return Value::makeNumber(r);
    }

    static Value objfnfile_unlink(const FuncContext& scfn)
    {
        int64_t r;
        String* path;
        ArgCheck check("unlink", scfn);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        path = scfn.argv[0].asString();
        r = Util::osfn_unlink(path->data());
        return Value::makeNumber(r);
    }

    static Value objfnfile_exists(const FuncContext& scfn)
    {
        String* file;
        ArgCheck check("exists", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        file = scfn.argv[0].asString();
        return Value::makeBool(File::fileExists(file->data()));
    }

    static Value objfnfile_isfile(const FuncContext& scfn)
    {
        String* file;
        ArgCheck check("isfile", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        file = scfn.argv[0].asString();
        return Value::makeBool(File::pathIsFile(file->data()));
    }

    static Value objfnfile_isdirectory(const FuncContext& scfn)
    {
        String* file;
        ArgCheck check("isdirectory", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        file = scfn.argv[0].asString();
        return Value::makeBool(File::pathIsDirectory(file->data()));
    }

    static Value objfnfile_readstatic(const FuncContext& scfn)
    {
        char* buf;
        size_t thismuch;
        size_t actualsz;
        String* filepath;
        ArgCheck check("read", scfn);
        auto gcs = SharedState::get();
        thismuch = -1;
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        if(scfn.argc > 1)
        {
            NEON_ARGS_CHECKTYPE(check, 1, &Value::isNumber);
            thismuch = (size_t)scfn.argv[1].asNumber();
        }
        filepath = scfn.argv[0].asString();
        buf = File::readFile(filepath->data(), &actualsz, true, thismuch);
        if(buf == nullptr)
        {
            NEON_THROWCLASSWITHSOURCEINFO(gcs->m_exceptions.ioerror, "%s: %s", filepath->data(), strerror(errno));
            return Value::makeNull();
        }
        return Value::fromObject(String::take(buf, actualsz));
    }

    static Value objfnfile_writestatic(const FuncContext& scfn)
    {
        bool appending;
        size_t rt;
        FILE* fh;
        const char* mode;
        String* filepath;
        String* data;
        ArgCheck check("write", scfn);
        auto gcs = SharedState::get();
        appending = false;
        mode = "wb";
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        NEON_ARGS_CHECKTYPE(check, 1, &Value::isString);
        if(scfn.argc > 2)
        {
            NEON_ARGS_CHECKTYPE(check, 2, &Value::isBool);
            appending = scfn.argv[2].asBool();
        }
        if(appending)
        {
            mode = "ab";
        }
        filepath = scfn.argv[0].asString();
        data = scfn.argv[1].asString();
        fh = fopen(filepath->data(), mode);
        if(fh == nullptr)
        {
            NEON_THROWCLASSWITHSOURCEINFO(gcs->m_exceptions.ioerror, strerror(errno));
            return Value::makeNull();
        }
        rt = fwrite(data->data(), sizeof(char), data->length(), fh);
        fclose(fh);
        return Value::makeNumber(rt);
    }

    static Value objfnfile_close(const FuncContext& scfn)
    {
        ArgCheck check("close", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        scfn.thisval.asFile()->closeFile();
        return Value::makeNull();
    }

    static Value objfnfile_open(const FuncContext& scfn)
    {
        ArgCheck check("open", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        scfn.thisval.asFile()->openWithoutParams();
        return Value::makeNull();
    }

    static Value objfnfile_isopen(const FuncContext& scfn)
    {
        File* file;
        file = scfn.thisval.asFile();
        return Value::makeBool(file->m_isstd || file->m_isopen);
    }

    static Value objfnfile_isclosed(const FuncContext& scfn)
    {
        File* file;
        file = scfn.thisval.asFile();
        return Value::makeBool(!file->m_isstd && !file->m_isopen);
    }

    static Value objfnfile_readmethod(const FuncContext& scfn)
    {
        size_t readhowmuch;
        IOResult res;
        File* file;
        ArgCheck check("read", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 0, 1);
        readhowmuch = -1;
        if(scfn.argc == 1)
        {
            NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
            readhowmuch = (size_t)scfn.argv[0].asNumber();
        }
        file = scfn.thisval.asFile();
        //#define FILE_ERROR_(scfn, type, message) NEON_RETURNERROR(scfn, #type " -> %s", message, file->m_path->data());

        if(!file->readData(readhowmuch, &res))
        {
            NEON_RETURNERROR(scfn, "NotFound -> %s" , strerror(errno));
        }
        return Value::fromObject(String::take(res.data, res.length));
    }

    static Value objfnfile_readline(const FuncContext& scfn)
    {
        long rdline;
        size_t linelen;
        char* strline;
        File* file;
        String* nos;
        ArgCheck check("readLine", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 0, 1);
        file = scfn.thisval.asFile();
        linelen = 0;
        strline = nullptr;
        rdline = File::readLineFromHandle(&strline, &linelen, file->m_handle);
        if(rdline == -1)
        {
            return Value::makeNull();
        }
        nos = String::take(strline, rdline);
        return Value::fromObject(nos);
    }

    static Value objfnfile_get(const FuncContext& scfn)
    {
        int ch;
        File* file;
        ArgCheck check("get", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        file = scfn.thisval.asFile();
        ch = fgetc(file->m_handle);
        if(ch == EOF)
        {
            return Value::makeNull();
        }
        return Value::makeNumber(ch);
    }

    static Value objfnfile_gets(const FuncContext& scfn)
    {
        long end;
        long length;
        long currentpos;
        size_t bytesread;
        char* buffer;
        File* file;
        ArgCheck check("gets", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 0, 1);
        length = -1;
        if(scfn.argc == 1)
        {
            NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
            length = (size_t)scfn.argv[0].asNumber();
        }
        file = scfn.thisval.asFile();
        if(!file->m_isstd)
        {
            if(!File::fileExists(file->m_path->data()))
            {
                NEON_RETURNERROR(scfn, "NotFound -> %s" , "no such file or directory");
            }
            else if(strstr(file->m_mode->data(), "w") != nullptr && strstr(file->m_mode->data(), "+") == nullptr)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot read file in write mode");
            }
            if(!file->m_isopen)
            {
                NEON_RETURNERROR(scfn, "Read -> %s" , "file not open");
            }
            else if(file->m_handle == nullptr)
            {
                NEON_RETURNERROR(scfn, "Read -> %s" , "could not read file");
            }
            if(length == -1)
            {
                currentpos = ftell(file->m_handle);
                fseek(file->m_handle, 0L, SEEK_END);
                end = ftell(file->m_handle);
                fseek(file->m_handle, currentpos, SEEK_SET);
                length = end - currentpos;
            }
        }
        else
        {
            if(fileno(stdout) == file->m_number || fileno(stderr) == file->m_number)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot read from output file");
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
        buffer = (char*)Memory::sysMalloc(sizeof(char) * (length + 1));
        if(buffer == nullptr && length != 0)
        {
            NEON_RETURNERROR(scfn, "Buffer -> %s" , "not enough memory to read file");
        }
        bytesread = fread(buffer, sizeof(char), length, file->m_handle);
        if(bytesread == 0 && length != 0)
        {
            NEON_RETURNERROR(scfn, "Read -> %s" , "could not read file contents");
        }
        if(buffer != nullptr)
        {
            buffer[bytesread] = '\0';
        }
        return Value::fromObject(String::take(buffer, bytesread));
    }

    static Value objfnfile_write(const FuncContext& scfn)
    {
        size_t count;
        int length;
        unsigned char* data;
        File* file;
        String* string;
        ArgCheck check("write", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        file = scfn.thisval.asFile();
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        string = scfn.argv[0].asString();
        data = (unsigned char*)string->data();
        length = string->length();
        if(!file->m_isstd)
        {
            if(strstr(file->m_mode->data(), "r") != nullptr && strstr(file->m_mode->data(), "+") == nullptr)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot write into non-writable file");
            }
            else if(length == 0)
            {
                NEON_RETURNERROR(scfn, "Write -> %s" , "cannot write empty buffer to file");
            }
            else if(file->m_handle == nullptr || !file->m_isopen)
            {
                file->openWithoutParams();
            }
            else if(file->m_handle == nullptr)
            {
                NEON_RETURNERROR(scfn, "Write -> %s" , "could not write to file");
            }
        }
        else
        {
            if(fileno(stdin) == file->m_number)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot write to input file");
            }
        }
        count = fwrite(data, sizeof(unsigned char), length, file->m_handle);
        fflush(file->m_handle);
        if(count > (size_t)0)
        {
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    }

    static Value objfnfile_puts(const FuncContext& scfn)
    {
        size_t i;
        size_t count;
        int rc;
        int length;
        unsigned char* data;
        File* file;
        String* string;
        ArgCheck check("puts", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        file = scfn.thisval.asFile();

        if(!file->m_isstd)
        {
            if(strstr(file->m_mode->data(), "r") != nullptr && strstr(file->m_mode->data(), "+") == nullptr)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot write into non-writable file");
            }
            else if(!file->m_isopen)
            {
                NEON_RETURNERROR(scfn, "Write -> %s" , "file not open");
            }
            else if(file->m_handle == nullptr)
            {
                NEON_RETURNERROR(scfn, "Write -> %s" , "could not write to file");
            }
        }
        else
        {
            if(fileno(stdin) == file->m_number)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot write to input file");
            }
        }
        rc = 0;
        for(i = 0; i < scfn.argc; i++)
        {
            NEON_ARGS_CHECKTYPE(check, i, &Value::isString);
            string = scfn.argv[i].asString();
            data = (unsigned char*)string->data();
            length = string->length();
            count = fwrite(data, sizeof(unsigned char), length, file->m_handle);
            if(count > (size_t)0 || length == 0)
            {
                return Value::makeNumber(0);
            }
            rc += count;
        }
        return Value::makeNumber(rc);
    }

    static Value objfnfile_putc(const FuncContext& scfn)
    {
        size_t i;
        int rc;
        File* file;
        ArgCheck check("puts", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        file = scfn.thisval.asFile();
        if(!file->m_isstd)
        {
            if(strstr(file->m_mode->data(), "r") != nullptr && strstr(file->m_mode->data(), "+") == nullptr)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot write into non-writable file");
            }
            else if(!file->m_isopen)
            {
                NEON_RETURNERROR(scfn, "Write -> %s" , "file not open");
            }
            else if(file->m_handle == nullptr)
            {
                NEON_RETURNERROR(scfn, "Write -> %s" , "could not write to file");
            }
        }
        else
        {
            if(fileno(stdin) == file->m_number)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot write to input file");
            }
        }
        rc = 0;
        for(i = 0; i < scfn.argc; i++)
        {
            NEON_ARGS_CHECKTYPE(check, i, &Value::isNumber);
            int cv = scfn.argv[i].asNumber();
            rc += fputc(cv, file->m_handle);
        }
        return Value::makeNumber(rc);
    }

    static Value objfnfile_printf(const FuncContext& scfn)
    {
        File* file;
        IOStream pr;
        String* ofmt;
        ArgCheck check("printf", scfn);
        file = scfn.thisval.asFile();
        NEON_ARGS_CHECKMINARG(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        ofmt = scfn.argv[0].asString();
        IOStream::makeStackIO(&pr, file->m_handle, false);
        FormatInfo nfi(&pr, ofmt->data(), ofmt->length());
        if(!nfi.formatWithArgs(scfn.argc, 1, scfn.argv))
        {
        }
        return Value::makeNull();
    }

    static Value objfnfile_number(const FuncContext& scfn)
    {
        ArgCheck check("number", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        return Value::makeNumber(scfn.thisval.asFile()->m_number);
    }

    static Value objfnfile_istty(const FuncContext& scfn)
    {
        File* file;
        ArgCheck check("istty", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        file = scfn.thisval.asFile();
        return Value::makeBool(file->m_istty);
    }

    static Value objfnfile_flush(const FuncContext& scfn)
    {
        File* file;
        ArgCheck check("flush", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        file = scfn.thisval.asFile();
        if(!file->m_isopen)
        {
            NEON_RETURNERROR(scfn, "Unsupported -> %s" , "I/O operation on closed file");
        }
    #if defined(NEON_PLAT_ISLINUX)
        if(fileno(stdin) == file->m_number)
        {
            while((getchar()) != '\n')
            {
            }
        }
        else
        {
            fflush(file->m_handle);
        }
    #else
        fflush(file->m_handle);
    #endif
        return Value::makeNull();
    }

    static Value objfnfile_path(const FuncContext& scfn)
    {
        File* file;
        ArgCheck check("path", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        file = scfn.thisval.asFile();
        if(file->m_isstd)
        {
            NEON_RETURNERROR(scfn, "method not supported for std files");
        }
        return Value::fromObject(file->m_path);
    }

    static Value objfnfile_mode(const FuncContext& scfn)
    {
        File* file;
        ArgCheck check("mode", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        file = scfn.thisval.asFile();
        return Value::fromObject(file->m_mode);
    }

    static Value objfnfile_name(const FuncContext& scfn)
    {
        char* name;
        File* file;
        ArgCheck check("name", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        file = scfn.thisval.asFile();
        if(!file->m_isstd)
        {
            name = File::getBasename(file->m_path->data());
            return Value::fromObject(String::copy(name));
        }
        else if(file->m_istty)
        {
            return Value::fromObject(String::copy("<tty>"));
        }
        return Value::makeNull();
    }

    static Value objfnfile_seek(const FuncContext& scfn)
    {
        long position;
        int seektype;
        File* file;
        ArgCheck check("seek", scfn);
        NEON_ARGS_CHECKCOUNT(check, 2);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        NEON_ARGS_CHECKTYPE(check, 1, &Value::isNumber);
        file = scfn.thisval.asFile();
        if(file->m_isstd)
        {
            NEON_RETURNERROR(scfn, "method not supported for std files");
        }
        position = (long)scfn.argv[0].asNumber();
        seektype = scfn.argv[1].asNumber();
        if(fseek(file->m_handle, position, seektype) == 0)
        {
            return Value::makeBool(true);
        }
        NEON_RETURNERROR(scfn, "File -> %s" , strerror(errno));
    }

    static Value objfnfile_tell(const FuncContext& scfn)
    {
        File* file;
        ArgCheck check("tell", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        file = scfn.thisval.asFile();
        if(file->m_isstd)
        {
            NEON_RETURNERROR(scfn, "method not supported for std files");
        }
        return Value::makeNumber(ftell(file->m_handle));
    }


    void installObjFile()
    {
        /* clang-format off */
        static Class::ConstItem filemethods[] = {
            { "close", objfnfile_close },
            { "open", objfnfile_open },
            { "read", objfnfile_readmethod },
            { "get", objfnfile_get },
            { "gets", objfnfile_gets },
            { "write", objfnfile_write },
            { "puts", objfnfile_puts },
            { "putc", objfnfile_putc },
            { "printf", objfnfile_printf },
            { "number", objfnfile_number },
            { "isTTY", objfnfile_istty },
            { "isOpen", objfnfile_isopen },
            { "isClosed", objfnfile_isclosed },
            { "flush", objfnfile_flush },
            { "path", objfnfile_path },
            { "seek", objfnfile_seek },
            { "tell", objfnfile_tell },
            { "mode", objfnfile_mode },
            { "name", objfnfile_name },
            { "readLine", objfnfile_readline },
            { nullptr, nullptr },
        };
        /* clang-format on */
        auto gcs = SharedState::get();
        gcs->m_classprimfile->defNativeConstructor(objfnfile_constructor);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("read"), objfnfile_readstatic);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("write"), objfnfile_writestatic);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("put"), objfnfile_writestatic);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("exists"), objfnfile_exists);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("chmod"), objfnfile_chmod);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("basename"), objfnfile_basename);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("dirname"), objfnfile_dirname);


        gcs->m_classprimfile->defStaticNativeMethod(String::copy("stat"), objfnfile_stat);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("unlink"), objfnfile_unlink);


        gcs->m_classprimfile->defStaticNativeMethod(String::copy("isFile"), objfnfile_isfile);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("isDirectory"), objfnfile_isdirectory);
        gcs->m_classprimfile->installMethods(filemethods);
    }

    static Value objfndir_readdir(const FuncContext& scfn)
    {
        bool havecallable;
        Util::FSDirReader rd;
        Util::FSDirReader::Item itm;
        Value res;
        Value temp;
        Value callable;
        ArgCheck check("readdir", scfn);
        Value nestargs[2];
        havecallable = false;
        callable = Value::makeNull();
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        if(scfn.argc > 1)
        {
            NEON_ARGS_CHECKTYPE(check, 1, &Value::isCallable);
            callable = scfn.argv[1];
            havecallable = true;
            res = Value::makeNull();
        }
        else
        {
            res = Value::fromObject(Array::make());
        }
        auto os = scfn.argv[0].asString();
        auto dirn = os->data();
        if(rd.openDir(dirn))
        {
            while(rd.readItem(&itm))
            {
                auto isdotsingle = ((itm.namelength == 1) && (itm.namedata[0] == '.'));
                auto isdotdouble = ((itm.namelength == 2) && (itm.namedata[0] == '.' && itm.namedata[1] == '.'));
                auto canignore = (isdotsingle || isdotdouble);
                if(!canignore)
                {
                    auto itemstr = String::copy(itm.namedata);
                    auto itemval = Value::fromObject(itemstr);
                    if(havecallable)
                    {
                        nestargs[0] = itemval;
                        gcs->vmNestCallFunction(callable, scfn.thisval, nestargs, 1, &temp, false);
                    }
                    else
                    {
                        res.asArray()->push(itemval);
                    }
                }
            }
            rd.closeDir();
            return res;
        }
        else
        {
            NEON_THROWEXCEPTION("cannot open directory '%s'", dirn);
        }
        return Value::makeNull();
    }

    static Value objfndir_mkdir(const FuncContext& scfn)
    {
        int64_t r;
        int64_t mod;
        String* path;
        ArgCheck check("mkdir", scfn);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        NEON_ARGS_CHECKTYPE(check, 1, &Value::isNumber);
        path = scfn.argv[0].asString();
        mod = scfn.argv[1].asNumber();
        r = Util::osfn_mkdir(path->data(), mod);
        return Value::makeNumber(r);
    }

    static Value objfndir_chdir(const FuncContext& scfn)
    {
        int64_t r;
        String* path;
        ArgCheck check("chdir", scfn);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        path = scfn.argv[0].asString();
        r = Util::osfn_chdir(path->data());
        return Value::makeNumber(r);
    }

    static Value objfndir_rmdir(const FuncContext& scfn)
    {
        int64_t r;
        String* path;
        ArgCheck check("rmdir", scfn);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        NEON_ARGS_CHECKTYPE(check, 1, &Value::isNumber);
        path = scfn.argv[0].asString();
        r = Util::osfn_rmdir(path->data());
        return Value::makeNumber(r);
    }

    static Value objfndir_cwdhelper(const FuncContext& scfn, const char* name)
    {
        enum
        {
            kMaxBufSz = 1024
        };
        char* r;
        char buf[kMaxBufSz];
        ArgCheck check(name, scfn);
        r = Util::osfn_getcwd(buf, kMaxBufSz);
        if(r == nullptr)
        {
            return Value::makeNull();
        }
        return Value::fromObject(String::copy(r));
    }

    static Value objfndir_cwd(const FuncContext& scfn)
    {
        return objfndir_cwdhelper(scfn, "cwd");
    }

    static Value objfndir_pwd(const FuncContext& scfn)
    {
        return objfndir_cwdhelper(scfn, "pwd");
    }

    void installObjDirectory()
    {
        /* clang-format off */
        static Class::ConstItem dirmethods[] =
        {
            { nullptr, nullptr },
        };
        /* clang-format on */
        auto gcs = SharedState::get();
        gcs->m_classprimdirectory->defStaticNativeMethod(String::copy("readdir"), objfndir_readdir);

        gcs->m_classprimdirectory->defStaticNativeMethod(String::copy("mkdir"), objfndir_mkdir);
        gcs->m_classprimdirectory->defStaticNativeMethod(String::copy("chdir"), objfndir_chdir);
        gcs->m_classprimdirectory->defStaticNativeMethod(String::copy("rmdir"), objfndir_rmdir);
        gcs->m_classprimdirectory->defStaticNativeMethod(String::copy("cwd"), objfndir_cwd);
        gcs->m_classprimdirectory->defStaticNativeMethod(String::copy("pwd"), objfndir_pwd);

        gcs->m_classprimdirectory->installMethods(dirmethods);
    }

    void setupModulePaths()
    {
        int i;
        /* clang-format off */
        static const char* defaultsearchpaths[] =
        {
            "mods",
            "mods/@/index" NEON_CONFIG_FILEEXT,
            ".",
            nullptr
        };
        /* clang-format on */
        auto gcs = SharedState::get();
        Module::addSearchPathObj(gcs->m_processinfo->cliexedirectory);
        for(i = 0; defaultsearchpaths[i] != nullptr; i++)
        {
            Module::addSearchPath(defaultsearchpaths[i]);
        }
    }

    static Value objfnnumber_tohexstring(const FuncContext& scfn)
    {
        return Value::fromObject(String::utilNumberToHexString(scfn.thisval.asNumber(), false));
    }

    static Value objfnmath_hypot(const FuncContext& scfn)
    {
        return Value::makeNumber(hypot(scfn.argv[0].asNumber(), scfn.argv[1].asNumber()));
    }

    static Value objfnmath_abs(const FuncContext& scfn)
    {
        return Value::makeNumber(fabs(scfn.argv[0].asNumber()));
    }

    static Value objfnmath_round(const FuncContext& scfn)
    {
        return Value::makeNumber(round(scfn.argv[0].asNumber()));
    }

    static Value objfnmath_sqrt(const FuncContext& scfn)
    {
        return Value::makeNumber(sqrt(scfn.argv[0].asNumber()));
    }

    static Value objfnmath_ceil(const FuncContext& scfn)
    {
        return Value::makeNumber(ceil(scfn.argv[0].asNumber()));
    }

    static Value objfnmath_floor(const FuncContext& scfn)
    {
        return Value::makeNumber(floor(scfn.argv[0].asNumber()));
    }

    static Value objfnmath_min(const FuncContext& scfn)
    {
        double b;
        double x;
        double y;
        x = scfn.argv[0].asNumber();
        y = scfn.argv[1].asNumber();
        b = (x < y) ? x : y;
        return Value::makeNumber(b);
    }

    static Value objfnmath_pow(const FuncContext& scfn)
    {
        double a;
        double b;
        double res;
        a = scfn.argv[0].asNumber();
        b = scfn.argv[1].asNumber();
        res = pow(a, b);
        return Value::makeNumber(res);
    }

    static Value objfnnumber_tobinstring(const FuncContext& scfn)
    {
        return Value::fromObject(String::utilNumberToBinString(scfn.thisval.asNumber()));
    }

    static Value objfnnumber_tooctstring(const FuncContext& scfn)
    {
        return Value::fromObject(String::utilNumberToOctString(scfn.thisval.asNumber(), false));
    }

    static Value objfnnumber_constructor(const FuncContext& scfn)
    {
        Value val;
        Value rtval;
        String* os;
        if(scfn.argc == 0)
        {
            return Value::makeNumber(0);
        }
        val = scfn.argv[0];
        if(val.isNumber())
        {
            return val;
        }
        if(val.isNull())
        {
            return Value::makeNumber(0);
        }
        if(!val.isString())
        {
            NEON_RETURNERROR(scfn, "Number() expects no arguments, or number, or string");
        }
        AstToken tok;
        AstLexer lex;
        os = val.asString();
        lex.init(os->data());
        tok = lex.scannumber();
        rtval = AstParser::utilConvertNumberString(tok.m_toktype, tok.m_start);
        return rtval;
    }

    void installObjNumber()
    {
        /* clang-format off */
        static Class::ConstItem numbermethods[] = {
            { "toHexString", objfnnumber_tohexstring },
            { "toOctString", objfnnumber_tooctstring },
            { "toBinString", objfnnumber_tobinstring },
            { nullptr, nullptr },
        };
        /* clang-format on */
        auto gcs = SharedState::get();
        gcs->m_classprimnumber->defNativeConstructor(objfnnumber_constructor);
        gcs->m_classprimnumber->installMethods(numbermethods);
    }

    void installModMath()
    {
        Class* klass;
        auto gcs = SharedState::get();
        klass = Class::makeScriptClass("Math", gcs->m_classprimobject);
        klass->defStaticNativeMethod(String::intern("hypot"), objfnmath_hypot);
        klass->defStaticNativeMethod(String::intern("abs"), objfnmath_abs);
        klass->defStaticNativeMethod(String::intern("round"), objfnmath_round);
        klass->defStaticNativeMethod(String::intern("sqrt"), objfnmath_sqrt);
        klass->defStaticNativeMethod(String::intern("ceil"), objfnmath_ceil);
        klass->defStaticNativeMethod(String::intern("floor"), objfnmath_floor);
        klass->defStaticNativeMethod(String::intern("min"), objfnmath_min);
        klass->defStaticNativeMethod(String::intern("pow"), objfnmath_pow);
    }

    static Value objfnobject_dumpself(const FuncContext& scfn)
    {
        Value v;
        IOStream pr;
        String* os;
        v = scfn.thisval;
        IOStream::makeStackString(&pr);
        ValPrinter::printValue(&pr, v, true, false);
        os = pr.takeString();
        IOStream::destroy(&pr);
        return Value::fromObject(os);
    }

    static Value objfnobject_tostring(const FuncContext& scfn)
    {
        Value v;
        IOStream pr;
        String* os;
        v = scfn.thisval;
        IOStream::makeStackString(&pr);
        ValPrinter::printValue(&pr, v, false, true);
        os = pr.takeString();
        IOStream::destroy(&pr);
        return Value::fromObject(os);
    }

    static Value objfnobject_typename(const FuncContext& scfn)
    {
        Value v;
        String* os;
        v = scfn.argv[0];
        os = String::copy(Value::typeName(v, false));
        return Value::fromObject(os);
    }

    static Value objfnobject_getselfinstance(const FuncContext& scfn)
    {
        return scfn.thisval;
    }

    static Value objfnobject_getselfclass(const FuncContext& scfn)
    {
        #if 0
            vmDebugPrintVal(scfn.thisval, "<object>.class:scfn.thisval=");
        #endif
        if(scfn.thisval.isInstance())
        {
            return Value::fromObject(scfn.thisval.asInstance()->m_instanceclass);
        }
        return Value::makeNull();
    }

    static Value objfnobject_isstring(const FuncContext& scfn)
    {
        Value v;
        v = scfn.thisval;
        return Value::makeBool(v.isString());
    }

    static Value objfnobject_isarray(const FuncContext& scfn)
    {
        Value v;
        v = scfn.thisval;
        return Value::makeBool(v.isArray());
    }

    static Value objfnobject_isa(const FuncContext& scfn)
    {
        Value v;
        Value otherclval;
        Class* oclass;
        Class* selfclass;
        auto gcs = SharedState::get();
        v = scfn.thisval;
        otherclval = scfn.argv[0];
        if(otherclval.isClass())
        {
            oclass = otherclval.asClass();
            selfclass = gcs->getClassFor(v);
            if(selfclass != nullptr)
            {
                return Value::makeBool(Class::isInstanceOf(selfclass, oclass));
            }
        }
        return Value::makeBool(false);
    }

    static Value objfnobject_iscallable(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return (Value::makeBool(selfval.isClass() || selfval.isFuncscript() || selfval.isFuncclosure() || selfval.isFuncbound() || selfval.isFuncnative()));
    }

    static Value objfnobject_isbool(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isBool());
    }

    static Value objfnobject_isnumber(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isNumber());
    }

    static Value objfnobject_isint(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isNumber() && (((int)selfval.asNumber()) == selfval.asNumber()));
    }

    static Value objfnobject_isdict(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isDict());
    }

    static Value objfnobject_isobject(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isObject());
    }

    static Value objfnobject_isfunction(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isFuncscript() || selfval.isFuncclosure() || selfval.isFuncbound() || selfval.isFuncnative());
    }

    static Value objfnobject_isiterable(const FuncContext& scfn)
    {
        bool isiterable;
        Value dummy;
        Class* klass;
        Value selfval;
        selfval = scfn.thisval;
        isiterable = selfval.isArray() || selfval.isDict() || selfval.isString();
        if(!isiterable && selfval.isInstance())
        {
            klass = selfval.asInstance()->m_instanceclass;
            isiterable = klass->m_instmethods.get(Value::fromObject(String::intern("@iter")), &dummy) && klass->m_instmethods.get(Value::fromObject(String::intern("@itern")), &dummy);
        }
        return Value::makeBool(isiterable);
    }

    static Value objfnobject_isclass(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isClass());
    }

    static Value objfnobject_isfile(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isFile());
    }

    static Value objfnobject_isinstance(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isInstance());
    }

    static Value objfnclass_getselfname(const FuncContext& scfn)
    {
        Value selfval;
        Class* klass;
        selfval = scfn.thisval;
        klass = selfval.asClass();
        return Value::fromObject(klass->m_classname);
    }

    void installObjObject()
    {
        /* clang-format off */
        static Class::ConstItem objectmethods[] = {
            { "dump", objfnobject_dumpself },
            { "isa", objfnobject_isa },
            { "toString", objfnobject_tostring },
            { "isArray", objfnobject_isarray },
            { "isString", objfnobject_isstring },
            { "isCallable", objfnobject_iscallable },
            { "isBool", objfnobject_isbool },
            { "isNumber", objfnobject_isnumber },
            { "isInt", objfnobject_isint },
            { "isDict", objfnobject_isdict },
            { "isObject", objfnobject_isobject },
            { "isFunction", objfnobject_isfunction },
            { "isIterable", objfnobject_isiterable },
            { "isClass", objfnobject_isclass },
            { "isFile", objfnobject_isfile },
            { "isInstance", objfnobject_isinstance },
            { nullptr, nullptr },
        };
        /* clang-format on */
        auto gcs = SharedState::get();
        gcs->m_classprimobject->defCallableField(String::intern("class"), objfnobject_getselfclass);
        gcs->m_classprimobject->defStaticNativeMethod(String::intern("typename"), objfnobject_typename);
        gcs->m_classprimobject->defStaticCallableField(String::intern("prototype"), objfnobject_getselfinstance);
        gcs->m_classprimobject->installMethods(objectmethods);
        {
            gcs->m_classprimclass->defStaticCallableField(String::intern("name"), objfnclass_getselfname);
        }
    }

    static Value objfnprocess_exedirectory(const FuncContext& scfn)
    {
        (void)scfn;
        auto gcs = SharedState::get();
        if(gcs->m_processinfo->cliexedirectory != nullptr)
        {
            return Value::fromObject(gcs->m_processinfo->cliexedirectory);
        }
        return Value::makeNull();
    }

    static Value objfnprocess_scriptfile(const FuncContext& scfn)
    {
        (void)scfn;
        auto gcs = SharedState::get();
        if(gcs->m_processinfo->cliscriptfile != nullptr)
        {
            return Value::fromObject(gcs->m_processinfo->cliscriptfile);
        }
        return Value::makeNull();
    }

    static Value objfnprocess_scriptdirectory(const FuncContext& scfn)
    {
        (void)scfn;
        auto gcs = SharedState::get();
        if(gcs->m_processinfo->cliscriptdirectory != nullptr)
        {
            return Value::fromObject(gcs->m_processinfo->cliscriptdirectory);
        }
        return Value::makeNull();
    }

    static Value objfnprocess_exit(const FuncContext& scfn)
    {
        int rc;
        rc = 0;
        if(scfn.argc > 0)
        {
            rc = scfn.argv[0].asNumber();
        }
        exit(rc);
        return Value::makeNull();
    }

    static Value objfnprocess_kill(const FuncContext& scfn)
    {
        int pid;
        int code;
        pid = scfn.argv[0].asNumber();
        code = scfn.argv[1].asNumber();
        Util::osfn_kill(pid, code);
        return Value::makeNull();
    }

    static Value objfnprocess_getenv(const FuncContext& scfn)
    {
        const char* r;
        String* key;
        ArgCheck check("getenv", scfn);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        key = scfn.argv[0].asString();
        r = Util::osfn_getenv(key->data());
        if(r == nullptr)
        {
            return Value::makeNull();
        }
        return Value::fromObject(String::copy(r));
    }

    static Value objfnprocess_setenv(const FuncContext& scfn)
    {
        String* key;
        String* value;
        ArgCheck check("setenv", scfn);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        NEON_ARGS_CHECKTYPE(check, 1, &Value::isString);
        key = scfn.argv[0].asString();
        value = scfn.argv[1].asString();
        return Value::makeBool(Util::osfn_setenv(key->data(), value->data(), true));
    }


    void installObjProcess()
    {
        Class* klass;
        auto gcs = SharedState::get();
        klass = gcs->m_classprimprocess;
        klass->setStaticProperty(String::copy("directory"), Value::fromObject(gcs->m_processinfo->cliexedirectory));
        klass->setStaticProperty(String::copy("env"), Value::fromObject(gcs->m_envdict));
        klass->setStaticProperty(String::copy("stdin"), Value::fromObject(gcs->m_processinfo->filestdin));
        klass->setStaticProperty(String::copy("stdout"), Value::fromObject(gcs->m_processinfo->filestdout));
        klass->setStaticProperty(String::copy("stderr"), Value::fromObject(gcs->m_processinfo->filestderr));
        klass->setStaticProperty(String::copy("pid"), Value::makeNumber(gcs->m_processinfo->cliprocessid));
        klass->defStaticNativeMethod(String::copy("kill"), objfnprocess_kill);
        klass->defStaticNativeMethod(String::copy("exit"), objfnprocess_exit);
        klass->defStaticNativeMethod(String::copy("exedirectory"), objfnprocess_exedirectory);
        klass->defStaticNativeMethod(String::copy("scriptdirectory"), objfnprocess_scriptdirectory);
        klass->defStaticNativeMethod(String::copy("script"), objfnprocess_scriptfile);

        klass->defStaticNativeMethod(String::copy("getenv"), objfnprocess_getenv);
        klass->defStaticNativeMethod(String::copy("setenv"), objfnprocess_setenv);


    }

    static Value objfnrange_lower(const FuncContext& scfn)
    {
        ArgCheck check("lower", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        return Value::makeNumber(scfn.thisval.asRange()->m_lower);
    }

    static Value objfnrange_upper(const FuncContext& scfn)
    {
        ArgCheck check("upper", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        return Value::makeNumber(scfn.thisval.asRange()->m_upper);
    }

    static Value objfnrange_range(const FuncContext& scfn)
    {
        ArgCheck check("range", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        return Value::makeNumber(scfn.thisval.asRange()->m_range);
    }

    static Value objfnrange_iter(const FuncContext& scfn)
    {
        int val;
        int index;
        Range* range;
        ArgCheck check("iter", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        range = scfn.thisval.asRange();
        index = scfn.argv[0].asNumber();
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

    static Value objfnrange_itern(const FuncContext& scfn)
    {
        int index;
        Range* range;
        ArgCheck check("itern", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        range = scfn.thisval.asRange();
        if(scfn.argv[0].isNull())
        {
            if(range->m_range == 0)
            {
                return Value::makeNull();
            }
            return Value::makeNumber(0);
        }
        if(!scfn.argv[0].isNumber())
        {
            NEON_RETURNERROR(scfn, "ranges are numerically indexed");
        }
        index = (int)scfn.argv[0].asNumber() + 1;
        if(index < range->m_range)
        {
            return Value::makeNumber(index);
        }
        return Value::makeNull();
    }

    static Value objfnrange_expand(const FuncContext& scfn)
    {
        int i;
        Value val;
        Range* range;
        Array* oa;
        range = scfn.thisval.asRange();
        oa = Array::make();
        for(i = 0; i < range->m_range; i++)
        {
            val = Value::makeNumber(i);
            oa->push(val);
        }
        return Value::fromObject(oa);
    }

    static Value objfnrange_constructor(const FuncContext& scfn)
    {
        int a;
        int b;
        Range* orng;
        a = scfn.argv[0].asNumber();
        b = scfn.argv[1].asNumber();
        orng = Range::make(a, b);
        return Value::fromObject(orng);
    }

    void installObjRange()
    {
        /* clang-format off */
        static Class::ConstItem rangemethods[] = {
            { "lower", objfnrange_lower },
            { "upper", objfnrange_upper },
            { "range", objfnrange_range },
            { "expand", objfnrange_expand },
            { "toArray", objfnrange_expand },
            { "@iter", objfnrange_iter },
            { "@itern", objfnrange_itern },
            { nullptr, nullptr },
        };
        /* clang-format on */
        auto gcs = SharedState::get();
        gcs->m_classprimrange->defNativeConstructor(objfnrange_constructor);
        gcs->m_classprimrange->installMethods(rangemethods);
    }

    static Value objfnstring_utf8numbytes(const FuncContext& scfn)
    {
        int incode;
        int res;
        ArgCheck check("utf8NumBytes", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        incode = scfn.argv[0].asNumber();
        res = Util::utf8NumBytes(incode);
        return Value::makeNumber(res);
    }

    static Value objfnstring_utf8decode(const FuncContext& scfn)
    {
        int res;
        String* instr;
        ArgCheck check("utf8Decode", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        instr = scfn.argv[0].asString();
        res = Util::utf8Decode((const uint8_t*)instr->data(), instr->length());
        return Value::makeNumber(res);
    }

    static Value objfnstring_utf8encode(const FuncContext& scfn)
    {
        int incode;
        size_t len;
        String* res;
        char* buf;
        ArgCheck check("utf8Encode", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        incode = scfn.argv[0].asNumber();
        buf = Util::utf8Encode(incode, &len);
        res = String::take(buf, len);
        return Value::fromObject(res);
    }

    static Value objfnutilstring_utf8chars(const FuncContext& scfn, bool onlycodepoint)
    {
        int cp;
        bool havemax;
        size_t counter;
        size_t maxamount;
        const char* cstr;
        Array* res;
        String* os;
        String* instr;
        havemax = false;
        instr = scfn.thisval.asString();
        if(scfn.argc > 0)
        {
            havemax = true;
            maxamount = scfn.argv[0].asNumber();
        }
        res = Array::make();
        utf8iterator_t iter(instr->data(), instr->length());
        counter = 0;
        while(iter.next())
        {
            cp = iter.m_codepoint;
            cstr = iter.getChar();
            counter++;
            if(havemax)
            {
                if(counter == maxamount)
                {
                    goto finalize;
                }
            }
            if(onlycodepoint)
            {
                res->push(Value::makeNumber(cp));
            }
            else
            {
                os = String::copy(cstr, iter.m_charsize);
                res->push(Value::fromObject(os));
            }
        }
    finalize:
        return Value::fromObject(res);
    }

    static Value objfnstring_utf8chars(const FuncContext& scfn)
    {
        return objfnutilstring_utf8chars(scfn, false);
    }

    static Value objfnstring_utf8codepoints(const FuncContext& scfn)
    {
        return objfnutilstring_utf8chars(scfn, true);
    }

    static Value objfnstring_fromcharcode(const FuncContext& scfn)
    {
        char ch;
        String* os;
        ArgCheck check("fromCharCode", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        ch = scfn.argv[0].asNumber();
        os = String::copy(&ch, 1);
        return Value::fromObject(os);
    }

    static Value objfnstring_constructor(const FuncContext& scfn)
    {
        String* os;
        ArgCheck check("constructor", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        os = String::intern("", 0);
        return Value::fromObject(os);
    }

    static Value objfnstring_length(const FuncContext& scfn)
    {
        String* selfstr;
        ArgCheck check("length", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        selfstr = scfn.thisval.asString();
        return Value::makeNumber(selfstr->length());
    }

    static Value objfnstring_substring(const FuncContext& scfn)
    {
        size_t end;
        size_t start;
        size_t maxlen;
        String* nos;
        String* selfstr;
        ArgCheck check("substring", scfn);
        selfstr = scfn.thisval.asString();
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        maxlen = selfstr->length();
        end = maxlen;
        start = scfn.argv[0].asNumber();
        if(scfn.argc > 1)
        {
            NEON_ARGS_CHECKTYPE(check, 1, &Value::isNumber);
            end = scfn.argv[1].asNumber();
        }
        nos = selfstr->substring(start, end, true);
        return Value::fromObject(nos);
    }

    static Value objfnstring_charcodeat(const FuncContext& scfn)
    {
        int ch;
        int idx;
        int selflen;
        String* selfstr;
        ArgCheck check("charCodeAt", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        selfstr = scfn.thisval.asString();
        idx = scfn.argv[0].asNumber();
        selflen = (int)selfstr->length();
        if((idx < 0) || (idx >= selflen))
        {
            ch = -1;
        }
        else
        {
            ch = selfstr->get(idx);
        }
        return Value::makeNumber(ch);
    }

    static Value objfnstring_charat(const FuncContext& scfn)
    {
        char ch;
        int idx;
        int selflen;
        String* selfstr;
        ArgCheck check("charAt", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        selfstr = scfn.thisval.asString();
        idx = scfn.argv[0].asNumber();
        selflen = (int)selfstr->length();
        if((idx < 0) || (idx >= selflen))
        {
            return Value::fromObject(String::intern("", 0));
        }
        else
        {
            ch = selfstr->get(idx);
        }
        return Value::fromObject(String::copy(&ch, 1));
    }

    static Value objfnstring_upper(const FuncContext& scfn)
    {
        size_t slen;
        char* string;
        String* str;
        ArgCheck check("upper", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        str = scfn.thisval.asString();
        slen = str->length();
        string = Util::stringToUpper(str->mutdata(), slen);
        return Value::fromObject(String::copy(string, slen));
    }

    static Value objfnstring_lower(const FuncContext& scfn)
    {
        size_t slen;
        char* string;
        String* str;
        ArgCheck check("lower", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        str = scfn.thisval.asString();
        slen = str->length();
        string = Util::stringToLower(str->mutdata(), slen);
        return Value::fromObject(String::copy(string, slen));
    }

    static Value objfnstring_isalpha(const FuncContext& scfn)
    {
        size_t i;
        size_t len;
        String* selfstr;
        ArgCheck check("isAlpha", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        selfstr = scfn.thisval.asString();
        len = selfstr->length();
        for(i = 0; i < len; i++)
        {
            if(!isalpha((unsigned char)selfstr->get(i)))
            {
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(selfstr->length() != 0);
    }

    static Value objfnstring_isalnum(const FuncContext& scfn)
    {
        size_t i;
        size_t len;
        String* selfstr;
        ArgCheck check("isAlnum", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        selfstr = scfn.thisval.asString();
        len = selfstr->length();
        for(i = 0; i < len; i++)
        {
            if(!isalnum((unsigned char)selfstr->get(i)))
            {
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(selfstr->length() != 0);
    }

    static Value objfnstring_isfloat(const FuncContext& scfn)
    {
        double f;
        char* p;
        String* selfstr;
        ArgCheck check("isFloat", scfn);
        (void)f;
        NEON_ARGS_CHECKCOUNT(check, 0);
        selfstr = scfn.thisval.asString();
        errno = 0;
        if(selfstr->length() == 0)
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

    static Value objfnstring_isnumber(const FuncContext& scfn)
    {
        size_t i;
        size_t len;
        String* selfstr;
        ArgCheck check("isNumber", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        selfstr = scfn.thisval.asString();
        len = selfstr->length();
        for(i = 0; i < len; i++)
        {
            if(!isdigit((unsigned char)selfstr->get(i)))
            {
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(selfstr->length() != 0);
    }

    static Value objfnstring_islower(const FuncContext& scfn)
    {
        size_t i;
        size_t len;
        bool alphafound;
        String* selfstr;
        ArgCheck check("isLower", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        selfstr = scfn.thisval.asString();
        alphafound = false;
        len = selfstr->length();
        for(i = 0; i < len; i++)
        {
            if(!alphafound && !isdigit(selfstr->get(0)))
            {
                alphafound = true;
            }
            if(isupper(selfstr->get(0)))
            {
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(alphafound);
    }

    static Value objfnstring_isupper(const FuncContext& scfn)
    {
        size_t i;
        size_t len;
        bool alphafound;
        String* selfstr;
        ArgCheck check("isUpper", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        selfstr = scfn.thisval.asString();
        alphafound = false;
        len = selfstr->length();
        for(i = 0; i < len; i++)
        {
            if(!alphafound && !isdigit(selfstr->get(0)))
            {
                alphafound = true;
            }
            if(islower(selfstr->get(i)))
            {
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(alphafound);
    }

    static Value objfnstring_isspace(const FuncContext& scfn)
    {
        size_t i;
        size_t len;
        String* selfstr;
        ArgCheck check("isSpace", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        selfstr = scfn.thisval.asString();
        len = selfstr->length();
        for(i = 0; i < len; i++)
        {
            if(!isspace((unsigned char)selfstr->get(i)))
            {
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(selfstr->length() != 0);
    }

    static Value objfnstring_trim(const FuncContext& scfn)
    {
        char trimmer;
        char* end;
        char* string;
        String* selfstr;
        ArgCheck check("trim", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 0, 1);
        trimmer = '\0';
        if(scfn.argc == 1)
        {
            trimmer = (char)scfn.argv[0].asString()->get(0);
        }
        selfstr = scfn.thisval.asString();
        string = selfstr->mutdata();
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
            return Value::fromObject(String::intern("", 0));
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
        return Value::fromObject(String::copy(string));
    }

    static Value objfnstring_ltrim(const FuncContext& scfn)
    {
        char trimmer;
        char* end;
        char* string;
        String* selfstr;
        ArgCheck check("ltrim", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 0, 1);
        trimmer = '\0';
        if(scfn.argc == 1)
        {
            trimmer = (char)scfn.argv[0].asString()->get(0);
        }
        selfstr = scfn.thisval.asString();
        string = selfstr->mutdata();
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
            return Value::fromObject(String::intern("", 0));
        }
        end = string + strlen(string) - 1;
        end[1] = '\0';
        return Value::fromObject(String::copy(string));
    }

    static Value objfnstring_rtrim(const FuncContext& scfn)
    {
        char trimmer;
        char* end;
        char* string;
        String* selfstr;
        ArgCheck check("rtrim", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 0, 1);
        trimmer = '\0';
        if(scfn.argc == 1)
        {
            trimmer = (char)scfn.argv[0].asString()->get(0);
        }
        selfstr = scfn.thisval.asString();
        string = selfstr->mutdata();
        end = nullptr;
        /* All spaces? */
        if(*string == 0)
        {
            return Value::fromObject(String::intern("", 0));
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
        return Value::fromObject(String::copy(string));
    }

    static Value objfnstring_indexof(const FuncContext& scfn)
    {
        int startindex;
        char* result;
        const char* haystack;
        String* string;
        String* needle;
        ArgCheck check("indexOf", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 1, 2);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        string = scfn.thisval.asString();
        needle = scfn.argv[0].asString();
        startindex = 0;
        if(scfn.argc == 2)
        {
            NEON_ARGS_CHECKTYPE(check, 1, &Value::isNumber);
            startindex = scfn.argv[1].asNumber();
        }
        if(string->length() > 0 && needle->length() > 0)
        {
            haystack = string->data();
            result = (char*)strstr(haystack + startindex, needle->data());
            if(result != nullptr)
            {
                return Value::makeNumber((int)(result - haystack));
            }
        }
        return Value::makeNumber(-1);
    }

    static Value objfnstring_startswith(const FuncContext& scfn)
    {
        String* substr;
        String* string;
        ArgCheck check("startsWith", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        string = scfn.thisval.asString();
        substr = scfn.argv[0].asString();
        if(string->length() == 0 || substr->length() == 0 || substr->length() > string->length())
        {
            return Value::makeBool(false);
        }
        return Value::makeBool(memcmp(substr->data(), string->data(), substr->length()) == 0);
    }

    static Value objfnstring_endswith(const FuncContext& scfn)
    {
        int difference;
        String* substr;
        String* string;
        ArgCheck check("endsWith", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        string = scfn.thisval.asString();
        substr = scfn.argv[0].asString();
        if(string->length() == 0 || substr->length() == 0 || substr->length() > string->length())
        {
            return Value::makeBool(false);
        }
        difference = string->length() - substr->length();
        return Value::makeBool(memcmp(substr->data(), string->data() + difference, substr->length()) == 0);
    }

    static Value objfnstring_matchcapture(const FuncContext& scfn)
    {
        String* pattern;
        String* string;
        ArgCheck check("match", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        string = scfn.thisval.asString();
        pattern = scfn.argv[0].asString();
        return String::regexMatch(string, pattern, true);
    }

    static Value objfnstring_matchonly(const FuncContext& scfn)
    {
        String* pattern;
        String* string;
        ArgCheck check("match", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        string = scfn.thisval.asString();
        pattern = scfn.argv[0].asString();
        return String::regexMatch(string, pattern, false);
    }

    static Value objfnstring_count(const FuncContext& scfn)
    {
        int count;
        const char* tmp;
        String* substr;
        String* string;
        ArgCheck check("count", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        string = scfn.thisval.asString();
        substr = scfn.argv[0].asString();
        if(substr->length() == 0 || string->length() == 0)
        {
            return Value::makeNumber(0);
        }
        count = 0;
        tmp = string->data();
        while((tmp = Util::utf8Strstr(tmp, substr->data())))
        {
            count++;
            tmp++;
        }
        return Value::makeNumber(count);
    }

    static Value objfnstring_tonumber(const FuncContext& scfn)
    {
        String* selfstr;
        ArgCheck check("toNumber", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        selfstr = scfn.thisval.asString();
        return Value::makeNumber(strtod(selfstr->data(), nullptr));
    }

    static Value objfnstring_isascii(const FuncContext& scfn)
    {
        String* string;
        ArgCheck check("isAscii", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 0, 1);
        if(scfn.argc == 1)
        {
            NEON_ARGS_CHECKTYPE(check, 0, &Value::isBool);
        }
        string = scfn.thisval.asString();
        return Value::fromObject(string);
    }

    static Value objfnstring_tolist(const FuncContext& scfn)
    {
        size_t i;
        size_t end;
        size_t start;
        size_t length;
        Array* list;
        String* string;
        ArgCheck check("toList", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        string = scfn.thisval.asString();
        list = SharedState::gcProtect(Array::make());
        length = string->length();
        if(length > 0)
        {
            for(i = 0; i < length; i++)
            {
                start = i;
                end = i + 1;
                list->push(Value::fromObject(String::copy(string->data() + start, (int)(end - start))));
            }
        }
        return Value::fromObject(list);
    }

    static Value objfnstring_tobytes(const FuncContext& scfn)
    {
        size_t i;
        size_t length;
        Array* list;
        String* string;
        ArgCheck check("toBytes", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        string = scfn.thisval.asString();
        list = SharedState::gcProtect(Array::make());
        length = string->length();
        if(length > 0)
        {
            for(i = 0; i < length; i++)
            {
                list->push(Value::makeNumber(string->get(i)));
            }
        }
        return Value::fromObject(list);
    }

    static Value objfnstring_lpad(const FuncContext& scfn)
    {
        size_t i;
        size_t width;
        size_t fillsize;
        size_t finalsize;
        char fillchar;
        char* str;
        char* fill;
        String* ofillstr;
        String* result;
        String* string;
        ArgCheck check("lpad", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 1, 2);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        string = scfn.thisval.asString();
        width = scfn.argv[0].asNumber();
        fillchar = ' ';
        if(scfn.argc == 2)
        {
            ofillstr = scfn.argv[1].asString();
            fillchar = ofillstr->get(0);
        }
        if(width <= string->length())
        {
            return scfn.thisval;
        }
        fillsize = width - string->length();
        fill = (char*)Memory::sysMalloc(sizeof(char) * ((size_t)fillsize + 1));
        finalsize = string->length() + fillsize;
        for(i = 0; i < fillsize; i++)
        {
            fill[i] = fillchar;
        }
        str = (char*)Memory::sysMalloc(sizeof(char) * ((size_t)finalsize + 1));
        memcpy(str, fill, fillsize);
        memcpy(str + fillsize, string->data(), string->length());
        str[finalsize] = '\0';
        Memory::sysFree(fill);
        result = String::take(str, finalsize);
        result->setLength(finalsize);
        return Value::fromObject(result);
    }

    static Value objfnstring_rpad(const FuncContext& scfn)
    {
        size_t i;
        size_t width;
        size_t fillsize;
        size_t finalsize;
        char fillchar;
        char* str;
        char* fill;
        String* ofillstr;
        String* string;
        String* result;
        ArgCheck check("rpad", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 1, 2);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        string = scfn.thisval.asString();
        width = scfn.argv[0].asNumber();
        fillchar = ' ';
        if(scfn.argc == 2)
        {
            ofillstr = scfn.argv[1].asString();
            fillchar = ofillstr->get(0);
        }
        if(width <= string->length())
        {
            return scfn.thisval;
        }
        fillsize = width - string->length();
        fill = (char*)Memory::sysMalloc(sizeof(char) * ((size_t)fillsize + 1));
        finalsize = string->length() + fillsize;
        for(i = 0; i < fillsize; i++)
        {
            fill[i] = fillchar;
        }
        str = (char*)Memory::sysMalloc(sizeof(char) * ((size_t)finalsize + 1));
        memcpy(str, string->data(), string->length());
        memcpy(str + string->length(), fill, fillsize);
        str[finalsize] = '\0';
        Memory::sysFree(fill);
        result = String::take(str, finalsize);
        result->setLength(finalsize);
        return Value::fromObject(result);
    }

    static Value objfnstring_split(const FuncContext& scfn)
    {
        size_t i;
        size_t end;
        size_t start;
        size_t length;
        Array* list;
        String* string;
        String* delimeter;
        ArgCheck check("split", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 1, 2);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        string = scfn.thisval.asString();
        delimeter = scfn.argv[0].asString();
        /* empty string matches empty string to empty list */
        if(((string->length() == 0) && (delimeter->length() == 0)) || (string->length() == 0) || (delimeter->length() == 0))
        {
            return Value::fromObject(Array::make());
        }
        list = SharedState::gcProtect(Array::make());
        if(delimeter->length() > 0)
        {
            start = 0;
            for(i = 0; i <= string->length(); i++)
            {
                /* match found. */
                if(memcmp(string->data() + i, delimeter->data(), delimeter->length()) == 0 || i == string->length())
                {
                    list->push(Value::fromObject(String::copy(string->data() + start, i - start)));
                    i += delimeter->length() - 1;
                    start = i + 1;
                }
            }
        }
        else
        {
            length = string->length();
            for(i = 0; i < length; i++)
            {
                start = i;
                end = i + 1;
                list->push(Value::fromObject(String::copy(string->data() + start, (int)(end - start))));
            }
        }
        return Value::fromObject(list);
    }

    static Value objfnstring_replace(const FuncContext& scfn)
    {
        bool ok;
        size_t xlen;
        StrBuffer result;
        String* findme;
        String* string;
        String* repwith;
        ArgCheck check("replace", scfn);
        NEON_ARGS_CHECKCOUNT(check, 2);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        NEON_ARGS_CHECKTYPE(check, 1, &Value::isString);
        string = scfn.thisval.asString();
        findme = scfn.argv[0].asString();
        repwith = scfn.argv[1].asString();
        StrBuffer::fromPtr(&result, 0);
        ok = string->m_sbuf.replace(&result, findme->data(), findme->length(), repwith->data(), repwith->length());
        if(ok)
        {
            xlen = result.length();
            return Value::fromObject(String::makeFromStrbuf(result, Util::hashString(result.data(), xlen), xlen));
        }
        StrBuffer::destroyFromPtr(&result);
        return Value::makeNull();
    }

    static Value objfnstring_iter(const FuncContext& scfn)
    {
        size_t index;
        size_t length;
        String* string;
        String* result;
        ArgCheck check("iter", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        string = scfn.thisval.asString();
        length = string->length();
        index = scfn.argv[0].asNumber();
        if(((int)index > -1) && (index < length))
        {
            result = String::copy(&string->data()[index], 1);
            return Value::fromObject(result);
        }
        return Value::makeNull();
    }

    static Value objfnstring_itern(const FuncContext& scfn)
    {
        size_t index;
        size_t length;
        String* string;
        ArgCheck check("itern", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        string = scfn.thisval.asString();
        length = string->length();
        if(scfn.argv[0].isNull())
        {
            if(length == 0)
            {
                return Value::makeBool(false);
            }
            return Value::makeNumber(0);
        }
        if(!scfn.argv[0].isNumber())
        {
            NEON_RETURNERROR(scfn, "strings are numerically indexed");
        }
        index = scfn.argv[0].asNumber();
        if(index < length - 1)
        {
            return Value::makeNumber((double)index + 1);
        }
        return Value::makeNull();
    }

    static Value objfnstring_each(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        Value callable;
        Value unused;
        String* string;
        ArgCheck check("each", scfn);
        Value nestargs[3];
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isCallable);
        string = scfn.thisval.asString();
        callable = scfn.argv[0];
        arity = gcs->vmNestCallPrepare(callable, scfn.thisval, nestargs, 2);
        for(i = 0; i < string->length(); i++)
        {
            passi = 0;
            if(arity > 0)
            {
                passi++;
                nestargs[0] = Value::fromObject(String::copy(string->data() + i, 1));
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = Value::makeNumber(i);
                }
            }
            gcs->vmNestCallFunction(callable, scfn.thisval, nestargs, passi, &unused, false);
        }
        /* pop the argument list */
        return Value::makeNull();
    }

    static Value objfnstring_appendany(const FuncContext& scfn)
    {
        size_t i;
        Value arg;
        String* oss;
        String* selfstring;
        selfstring = scfn.thisval.asString();
        for(i = 0; i < scfn.argc; i++)
        {
            arg = scfn.argv[i];
            if(arg.isNumber())
            {
                selfstring->appendByte(arg.asNumber());
            }
            else
            {
                oss = Value::toString(arg);
                selfstring->appendObject(oss);
            }
        }
        /* pop the argument list */
        return scfn.thisval;
    }

    static Value objfnstring_appendbytes(const FuncContext& scfn)
    {
        size_t i;
        Value arg;
        String* selfstring;
        selfstring = scfn.thisval.asString();
        for(i = 0; i < scfn.argc; i++)
        {
            arg = scfn.argv[i];
            if(arg.isNumber())
            {
                selfstring->appendByte(arg.asNumber());
            }
            else
            {
                NEON_RETURNERROR(scfn, "appendbytes expects number types");
            }
        }
        /* pop the argument list */
        return scfn.thisval;
    }

    void installObjString()
    {
        /* clang-format off */
        static Class::ConstItem stringmethods[] = {
            { "@iter", objfnstring_iter },
            { "@itern", objfnstring_itern },
            { "size", objfnstring_length },
            { "substr", objfnstring_substring },
            { "substring", objfnstring_substring },
            { "charCodeAt", objfnstring_charcodeat },
            { "charAt", objfnstring_charat },
            { "upper", objfnstring_upper },
            { "lower", objfnstring_lower },
            { "trim", objfnstring_trim },
            { "ltrim", objfnstring_ltrim },
            { "rtrim", objfnstring_rtrim },
            { "split", objfnstring_split },
            { "indexOf", objfnstring_indexof },
            { "count", objfnstring_count },
            { "toNumber", objfnstring_tonumber },
            { "toList", objfnstring_tolist },
            { "toBytes", objfnstring_tobytes },
            { "lpad", objfnstring_lpad },
            { "rpad", objfnstring_rpad },
            { "replace", objfnstring_replace },
            { "each", objfnstring_each },
            { "startsWith", objfnstring_startswith },
            { "endsWith", objfnstring_endswith },
            { "isAscii", objfnstring_isascii },
            { "isAlpha", objfnstring_isalpha },
            { "isAlnum", objfnstring_isalnum },
            { "isNumber", objfnstring_isnumber },
            { "isFloat", objfnstring_isfloat },
            { "isLower", objfnstring_islower },
            { "isUpper", objfnstring_isupper },
            { "isSpace", objfnstring_isspace },
            { "utf8Chars", objfnstring_utf8chars },
            { "utf8Codepoints", objfnstring_utf8codepoints },
            { "utf8Bytes", objfnstring_utf8codepoints },
            { "match", objfnstring_matchcapture },
            { "matches", objfnstring_matchonly },
            { "append", objfnstring_appendany },
            { "push", objfnstring_appendany },
            { "appendbytes", objfnstring_appendbytes },
            { "appendbyte", objfnstring_appendbytes },
            { nullptr, nullptr },
        };
        /* clang-format on */
        auto gcs = SharedState::get();
        gcs->m_classprimstring->defNativeConstructor(objfnstring_constructor);
        gcs->m_classprimstring->defStaticNativeMethod(String::intern("fromCharCode"), objfnstring_fromcharcode);
        gcs->m_classprimstring->defStaticNativeMethod(String::intern("utf8Decode"), objfnstring_utf8decode);
        gcs->m_classprimstring->defStaticNativeMethod(String::intern("utf8Encode"), objfnstring_utf8encode);
        gcs->m_classprimstring->defStaticNativeMethod(String::intern("utf8NumBytes"), objfnstring_utf8numbytes);
        gcs->m_classprimstring->defCallableField(String::intern("length"), objfnstring_length);
        gcs->m_classprimstring->installMethods(stringmethods);
    }


    static Value nativefn_time(const FuncContext& scfn)
    {
        struct timeval tv;
        ArgCheck check("time", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        Util::osfn_gettimeofday(&tv, nullptr);
        return Value::makeNumber((double)tv.tv_sec + ((double)tv.tv_usec / 10000000));
    }

    static Value nativefn_microtime(const FuncContext& scfn)
    {
        struct timeval tv;
        ArgCheck check("microtime", scfn);
        NEON_ARGS_CHECKCOUNT(check, 0);
        Util::osfn_gettimeofday(&tv, nullptr);
        return Value::makeNumber((1000000 * (double)tv.tv_sec) + ((double)tv.tv_usec / 10));
    }

    static Value nativefn_id(const FuncContext& scfn)
    {
        Value val;
        ArgCheck check("id", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        val = scfn.argv[0];
        return Value::makeNumber(*(long*)&val);
    }

    static Value nativefn_int(const FuncContext& scfn)
    {
        ArgCheck check("int", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 0, 1);
        if(scfn.argc == 0)
        {
            return Value::makeNumber(0);
        }
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        return Value::makeNumber((double)((int)scfn.argv[0].asNumber()));
    }

    static Value nativefn_chr(const FuncContext& scfn)
    {
        size_t len;
        char* string;
        int ch;
        ArgCheck check("chr", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
        ch = scfn.argv[0].asNumber();
        string = Util::utf8Encode(ch, &len);
        return Value::fromObject(String::take(string, len));
    }

    static Value nativefn_ord(const FuncContext& scfn)
    {
        int ord;
        int length;
        String* string;
        ArgCheck check("ord", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        string = scfn.argv[0].asString();
        length = string->length();
        if(length > 1)
        {
            NEON_RETURNERROR(scfn, "ord() expects character as argument, string given");
        }
        ord = (int)string->data()[0];
        if(ord < 0)
        {
            ord += 256;
        }
        return Value::makeNumber(ord);
    }

    static Value nativefn_srand(const FuncContext& scfn)
    {
        ArgCheck check("srand", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 0, 2);
        auto val = scfn.argv[0].asNumber();
        srand(val);
        return Value::makeNull();
    }

    static Value nativefn_rand(const FuncContext& scfn)
    {
        int tmp;
        int lowerlimit;
        int upperlimit;
        ArgCheck check("rand", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(check, 0, 2);
        lowerlimit = 0;
        upperlimit = 1;
        if(scfn.argc > 0)
        {
            NEON_ARGS_CHECKTYPE(check, 0, &Value::isNumber);
            lowerlimit = scfn.argv[0].asNumber();
        }
        if(scfn.argc == 2)
        {
            NEON_ARGS_CHECKTYPE(check, 1, &Value::isNumber);
            upperlimit = scfn.argv[1].asNumber();
        }
        if(lowerlimit > upperlimit)
        {
            tmp = upperlimit;
            upperlimit = lowerlimit;
            lowerlimit = tmp;
        }
        return Value::makeNumber(Util::mtrandRandom(lowerlimit, upperlimit));
    }

    static Value nativefn_eval(const FuncContext& scfn)
    {
        Value result;
        String* os;
        ArgCheck check("eval", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        auto gcs = SharedState::get();
        os = scfn.argv[0].asString();
        /*fprintf(stderr, "eval:src=%s\n", os->data());*/
        result = gcs->evalSource(os->data());
        return result;
    }

    static Value nativefn_require(const FuncContext& scfn)
    {
        ArgCheck check("require", scfn);
        NEON_ARGS_CHECKCOUNT(check, 1);
        auto modname = scfn.argv[0].asString();
        auto mod = Module::loadScriptModule(modname);
        if(mod == nullptr)
        {
            //NEON_THROWEXCEPTION("cannot find module '%s'", modname->data());
            return Value::makeNull();
        }
        return Value::fromObject(mod);
    }

    static Value nativefn_instanceof(const FuncContext& scfn)
    {
        ArgCheck check("instanceof", scfn);
        NEON_ARGS_CHECKCOUNT(check, 2);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isInstance);
        NEON_ARGS_CHECKTYPE(check, 1, &Value::isClass);
        return Value::makeBool(Class::isInstanceOf(scfn.argv[0].asInstance()->m_instanceclass, scfn.argv[1].asClass()));
    }

    static Value nativefn_sprintf(const FuncContext& scfn)
    {
        IOStream pr;
        String* res;
        String* ofmt;
        ArgCheck check("sprintf", scfn);
        NEON_ARGS_CHECKMINARG(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        ofmt = scfn.argv[0].asString();
        IOStream::makeStackString(&pr);
        FormatInfo nfi(&pr, ofmt->data(), ofmt->length());
        if(!nfi.formatWithArgs(scfn.argc, 1, scfn.argv))
        {
            return Value::makeNull();
        }
        res = pr.takeString();
        IOStream::destroy(&pr);
        return Value::fromObject(res);
    }

    static Value nativefn_printf(const FuncContext& scfn)
    {
        String* ofmt;
        ArgCheck check("printf", scfn);
        auto gcs = SharedState::get();
        NEON_ARGS_CHECKMINARG(check, 1);
        NEON_ARGS_CHECKTYPE(check, 0, &Value::isString);
        ofmt = scfn.argv[0].asString();
        FormatInfo nfi(gcs->m_stdoutprinter, ofmt->data(), ofmt->length());
        if(!nfi.formatWithArgs(scfn.argc, 1, scfn.argv))
        {
        }
        return Value::makeNull();
    }

    static Value nativefn_print(const FuncContext& scfn)
    {
        size_t i;
        auto gcs = SharedState::get();
        for(i = 0; i < scfn.argc; i++)
        {
            ValPrinter::printValue(gcs->m_stdoutprinter, scfn.argv[i], false, true);
        }
        return Value::makeNull();
    }

    static Value nativefn_println(const FuncContext& scfn)
    {
        Value v;
        auto gcs = SharedState::get();
        v = nativefn_print(scfn);
        gcs->m_stdoutprinter->writeString("\n");
        return v;
    }

    static Value nativefn_isnan(const FuncContext& scfn)
    {
        (void)scfn;
        return Value::makeBool(false);
    }

    static Value objfnjson_stringify(const FuncContext& scfn)
    {
        Value v;
        IOStream pr;
        String* os;
        v = scfn.argv[0];
        IOStream::makeStackString(&pr);
        pr.m_jsonmode = true;
        ValPrinter::printValue(&pr, v, true, false);
        os = pr.takeString();
        IOStream::destroy(&pr);
        return Value::fromObject(os);
    }

    /**
     * setup global functions.
     */
    void initBuiltinFunctions()
    {
        Class* klass;
        auto gcs = SharedState::get();
        defineGlobalNativeFunction("chr", nativefn_chr);
        defineGlobalNativeFunction("id", nativefn_id);
        defineGlobalNativeFunction("int", nativefn_int);
        defineGlobalNativeFunction("instanceof", nativefn_instanceof);
        defineGlobalNativeFunction("ord", nativefn_ord);
        defineGlobalNativeFunction("sprintf", nativefn_sprintf);
        defineGlobalNativeFunction("printf", nativefn_printf);
        defineGlobalNativeFunction("print", nativefn_print);
        defineGlobalNativeFunction("println", nativefn_println);
        defineGlobalNativeFunction("srand", nativefn_srand);
        defineGlobalNativeFunction("rand", nativefn_rand);
        defineGlobalNativeFunction("eval", nativefn_eval);
        defineGlobalNativeFunction("require", nativefn_require);
        defineGlobalNativeFunction("isNaN", nativefn_isnan);
        defineGlobalNativeFunction("microtime", nativefn_microtime);
        defineGlobalNativeFunction("time", nativefn_time);
        {
            klass = Class::makeScriptClass("JSON", gcs->m_classprimobject);
            klass->defStaticNativeMethod(String::copy("stringify"), objfnjson_stringify);
        }
    }

    bool SharedState::vmDoCallClosure(Function* closure, Value thisval, size_t argcount, bool fromoperator)
    {
        int i;
        int startva;
        CallFrame* frame;
        Array* argslist;
        // closure->m_clsthisval = thisval;
        NEON_APIDEBUG("thisval.m_valtype=%s, argcount=%d", Value::typeName(thisval, true), argcount);
        /* fill empty parameters if not variadic */
        for(; !closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.isvariadic && (argcount < size_t(closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity)); argcount++)
        {
            vmStackPush(Value::makeNull());
        }
        /* handle variadic arguments... */
        if(closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.isvariadic && (argcount >= size_t(closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity) - 1))
        {
            startva = argcount - closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity;
            argslist = Array::make();
            vmStackPush(Value::fromObject(argslist));
            for(i = startva; i >= 0; i--)
            {
                argslist->push(vmStackPeek(i + 1));
            }
            argcount -= startva;
            /* +1 for the gc protection push above */
            vmStackPop(startva + 2);
            vmStackPush(Value::fromObject(argslist));
        }
        if(argcount != size_t(closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity))
        {
            vmStackPop(argcount);
            if(closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.isvariadic)
            {
                return NEON_THROWEXCEPTION("function '%s' expected at least %d arguments but got %d", closure->m_funcname->data(), closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity - 1, argcount);
            }
            else
            {
                return NEON_THROWEXCEPTION("function '%s' expected %d arguments but got %d", closure->m_funcname->data(), closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity, argcount);
            }
        }
        if(checkMaybeResizeFrames() /*|| checkMaybeResizeStack()*/)
        {
            #if 0
                vmStackPop(argcount);
            #endif
        }
        if(fromoperator)
        {
            int64_t spos;
            spos = (m_vmstate.stackidx + (-argcount - 1));
            m_vmstate.stackvalues[spos] = thisval;
        }
        frame = &m_vmstate.framevalues[m_vmstate.framecount++];
        frame->closure = closure;
        frame->inscode = closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.blob->m_instrucs.data();
        frame->stackslotpos = m_vmstate.stackidx + (-argcount - 1);
        return true;
    }

    NEON_INLINE bool SharedState::vmDoCallNative(Function* native, Value thisval, size_t argcount)
    {
        int64_t spos;
        Value r;
        Value* vargs;
        NEON_APIDEBUG("thisval.m_valtype=%s, argcount=%d", Value::typeName(thisval, true), argcount);
        spos = m_vmstate.stackidx + (-argcount);
        vargs = m_vmstate.stackvalues.getp(spos);
        r = native->m_fnvals.fnnativefunc.natfunc(FuncContext{thisval, vargs, argcount});
        {
            m_vmstate.stackvalues[spos - 1] = r;
            m_vmstate.stackidx -= argcount;
        }
        SharedState::clearGCProtect();
        return true;
    }

    bool SharedState::vmCallWithObject(Value callable, Value thisval, size_t argcount, bool fromoper)
    {
        int64_t spos;
        Function* ofn;
        ofn = callable.asFunction();
    #if 0
        #define NEON_APIPRINT(...) fprintf(stderr, __VA_ARGS__), fprintf(stderr, "\n");
    #else
        #define NEON_APIPRINT(...)
    #endif
        NEON_APIPRINT("*callvaluewithobject*: thisval.m_valtype=%s, callable.m_valtype=%s, argcount=%d", Value::typeName(thisval, true), Value::typeName(callable, true), argcount);
        if(callable.isObject())
        {
            switch(callable.objType())
            {
                case Object::OTYP_FUNCCLOSURE:
                {
                    return vmDoCallClosure(ofn, thisval, argcount, fromoper);
                }
                break;
                case Object::OTYP_FUNCNATIVE:
                {
                    return vmDoCallNative(ofn, thisval, argcount);
                }
                break;
                case Object::OTYP_FUNCBOUND:
                {
                    Function* bound;
                    bound = ofn;
                    spos = (m_vmstate.stackidx + (-argcount - 1));
                    m_vmstate.stackvalues[spos] = thisval;
                    return vmDoCallClosure(bound->m_fnvals.fnmethod.method, thisval, argcount, fromoper);
                }
                break;
                case Object::OTYP_CLASS:
                {
                    Class* klass;
                    klass = callable.asClass();
                    spos = (m_vmstate.stackidx + (-argcount - 1));
                    m_vmstate.stackvalues[spos] = thisval;
                    if(!klass->m_constructor.isNull())
                    {
                        return vmCallWithObject(klass->m_constructor, thisval, argcount, false);
                    }
                    else if(klass->m_superclass != nullptr && !klass->m_superclass->m_constructor.isNull())
                    {
                        return vmCallWithObject(klass->m_superclass->m_constructor, thisval, argcount, false);
                    }
                    else if(argcount != 0)
                    {
                        return NEON_THROWEXCEPTION("%s constructor expects 0 arguments, %d given", klass->m_classname->data(), argcount);
                    }
                    return true;
                }
                break;
                case Object::OTYP_MODULE:
                {
                    Module* module;
                    Property* field;
                    module = callable.asModule();
                    field = module->m_deftable.getfieldbyostr(module->m_modname);
                    if(field != nullptr)
                    {
                        return vmCallValue(field->value, thisval, argcount, false);
                    }
                    return NEON_THROWEXCEPTION("module %s does not export a default function", module->m_modname);
                }
                break;
                default:
                    break;
            }
        }
        return NEON_THROWEXCEPTION("object of type %s is not callable", Value::typeName(callable, false));
    }

    bool SharedState::vmCallValue(Value callable, Value thisval, size_t argcount, bool fromoperator)
    {
        Value actualthisval;
        Function* ofn;
        if(callable.isObject())
        {
            ofn = callable.asFunction();
            switch(callable.objType())
            {
                case Object::OTYP_FUNCBOUND:
                {
                    Function* bound;
                    bound = ofn;
                    actualthisval = bound->m_fnvals.fnmethod.receiver;
                    if(!thisval.isNull())
                    {
                        actualthisval = thisval;
                    }
                    NEON_APIDEBUG("actualthisval.m_valtype=%s, argcount=%d", Value::typeName(actualthisval, true), argcount);
                    return vmCallWithObject(callable, actualthisval, argcount, fromoperator);
                }
                break;
                case Object::OTYP_CLASS:
                {
                    Class* klass;
                    Instance* instance;
                    klass = callable.asClass();
                    instance = Instance::make(klass);
                    actualthisval = Value::fromObject(instance);
                    if(!thisval.isNull())
                    {
                        actualthisval = thisval;
                    }
                    NEON_APIDEBUG("actualthisval.m_valtype=%s, argcount=%d", Value::typeName(actualthisval, true), argcount);
                    return vmCallWithObject(callable, actualthisval, argcount, fromoperator);
                }
                break;
                default:
                {
                }
                break;
            }
        }
        NEON_APIDEBUG("thisval.m_valtype=%s, argcount=%d", Value::typeName(thisval, true), argcount);
        return vmCallWithObject(callable, thisval, argcount, fromoperator);
    }

    Class* SharedState::getClassFor(Value receiver)
    {
        if(receiver.isNumber())
        {
            return m_classprimnumber;
        }
        if(receiver.isObject())
        {
            auto ot = receiver.asObject()->m_objtype;
            switch(ot)
            {
                case Object::OTYP_STRING:
                    return m_classprimstring;
                case Object::OTYP_RANGE:
                    return m_classprimrange;
                case Object::OTYP_ARRAY:
                    return m_classprimarray;
                case Object::OTYP_DICT:
                    return m_classprimdict;
                case Object::OTYP_FILE:
                    return m_classprimfile;
                case Object::OTYP_FUNCBOUND:
                case Object::OTYP_FUNCCLOSURE:
                case Object::OTYP_FUNCSCRIPT:
                    return m_classprimcallable;
                case Object::OTYP_INSTANCE:
                    {
                        auto inst = receiver.asInstance();
                        //fprintf(stderr, "instanceclass=%p\n", inst->m_instanceclass);
                        return inst->m_instanceclass;
                    }
                    break;
                default:
                {
                    fprintf(stderr, "getclassfor: unhandled type '%s' (%d)\n", Value::typeName(receiver, false), ot);
                }
                break;
            }
        }
        return nullptr;
    }

    /*
     * this macro cannot (rather, should not) be used outside of runVM().
     * if you need to halt the vm, throw an exception instead.
     * this macro is EXCLUSIVELY for non-recoverable errors!
     */
    #define VMMAC_EXITVM()                               \
        {                                                   \
            (void)you_are_calling_exit_vm_outside_of_runvm; \
            return Status::RuntimeFail;                 \
        }

    #define VMMAC_TRYRAISE(rtval, ...) \
        if(!NEON_THROWEXCEPTION(__VA_ARGS__)) \
        {                                 \
            return rtval;                 \
        }

    NEON_INLINE bool SharedState::vmUtilInvokeMethodFromClass(Class* klass, String* name, size_t argcount)
    {
        Property* field;
        NEON_APIDEBUG("argcount=%d", argcount);
        field = klass->m_instmethods.getfieldbyostr(name);
        if(field != nullptr)
        {
            if(Function::getMethodType(field->value) == Function::CTXTYPE_PRIVATE)
            {
                return NEON_THROWEXCEPTION("cannot call private method '%s' from instance of %s", name->data(), klass->m_classname->data());
            }
            return vmCallWithObject(field->value, Value::fromObject(klass), argcount, false);
        }
        return NEON_THROWEXCEPTION("undefined method '%s' in %s", name->data(), klass->m_classname->data());
    }

    NEON_INLINE bool SharedState::vmUtilInvokeMethodSelf(String* name, size_t argcount)
    {
        int64_t spos;
        Value receiver;
        Instance* instance;
        Property* field;
        NEON_APIDEBUG("argcount=%d", argcount);
        receiver = vmStackPeek(argcount);
        if(receiver.isInstance())
        {
            instance = receiver.asInstance();
            field = instance->m_instanceclass->m_instmethods.getfieldbyostr(name);
            if(field != nullptr)
            {
                return vmCallWithObject(field->value, receiver, argcount, false);
            }
            field = instance->m_instanceprops.getfieldbyostr(name);
            if(field != nullptr)
            {
                spos = (m_vmstate.stackidx + (-argcount - 1));
                m_vmstate.stackvalues[spos] = receiver;
                return vmCallWithObject(field->value, receiver, argcount, false);
            }
        }
        else if(receiver.isClass())
        {
            field = receiver.asClass()->m_instmethods.getfieldbyostr(name);
            if(field != nullptr)
            {
                if(Function::getMethodType(field->value) == Function::CTXTYPE_STATIC)
                {
                    return vmCallWithObject(field->value, receiver, argcount, false);
                }
                return NEON_THROWEXCEPTION("cannot call non-static method %s() on non instance", name->data());
            }
        }
        return NEON_THROWEXCEPTION("cannot call method '%s' on object of type '%s'", name->data(), Value::typeName(receiver, false));
    }

    NEON_INLINE bool SharedState::vmUtilInvokeMethodNormal(String* name, size_t argcount)
    {
        size_t spos;
        Object::Type rectype;
        Value receiver;
        Property* field;
        Class* klass;
        receiver = vmStackPeek(argcount);
        NEON_APIDEBUG("receiver.m_valtype=%s, argcount=%d", Value::typeName(receiver, true), argcount);
        if(receiver.isObject())
        {
            rectype = receiver.asObject()->m_objtype;
            switch(rectype)
            {
                case Object::OTYP_MODULE:
                {
                    Module* module;
                    NEON_APIDEBUG("receiver is a module");
                    module = receiver.asModule();
                    field = module->m_deftable.getfieldbyostr(name);
                    if(field != nullptr)
                    {
                        if(Class::methodNameIsPrivate(name))
                        {
                            return NEON_THROWEXCEPTION("cannot call private module method '%s'", name->data());
                        }
                        return vmCallWithObject(field->value, receiver, argcount, false);
                    }
                    return NEON_THROWEXCEPTION("module '%s' does not have a field named '%s'", module->m_modname->data(), name->data());
                }
                break;
                case Object::OTYP_CLASS:
                {
                    NEON_APIDEBUG("receiver is a class");
                    klass = receiver.asClass();
                    field = klass->getStaticProperty(name);
                    if(field != nullptr)
                    {
                        return vmCallWithObject(field->value, receiver, argcount, false);
                    }
                    field = klass->getStaticMethodField(name);
                    if(field != nullptr)
                    {
                        return vmCallWithObject(field->value, receiver, argcount, false);
                    }
    /*
     * TODO:
     * should this return the function? the returned value cannot be called without an object,
     * so ... whats the right move here?
     */
    #if 1
                    else
                    {
                        Function::ContextType fntyp;
                        field = klass->m_instmethods.getfieldbyostr(name);
                        if(field != nullptr)
                        {
                            fntyp = Function::getMethodType(field->value);
                            fprintf(stderr, "fntyp: %d\n", fntyp);
                            if(fntyp == Function::CTXTYPE_PRIVATE)
                            {
                                return NEON_THROWEXCEPTION("cannot call private method %s() on %s", name->data(), klass->m_classname->data());
                            }
                            if(fntyp == Function::CTXTYPE_STATIC)
                            {
                                return vmCallWithObject(field->value, receiver, argcount, false);
                            }
                        }
                    }
    #endif
                    return NEON_THROWEXCEPTION("unknown method %s() in class %s", name->data(), klass->m_classname->data());
                }
                case Object::OTYP_INSTANCE:
                {
                    Instance* instance;
                    NEON_APIDEBUG("receiver is an instance");
                    instance = receiver.asInstance();
                    field = instance->m_instanceprops.getfieldbyostr(name);
                    if(field != nullptr)
                    {
                        spos = (m_vmstate.stackidx + (-argcount - 1));
                        m_vmstate.stackvalues[spos] = receiver;
                        return vmCallWithObject(field->value, receiver, argcount, false);
                    }
                    return vmUtilInvokeMethodFromClass(instance->m_instanceclass, name, argcount);
                }
                break;
                case Object::OTYP_DICT:
                {
                    NEON_APIDEBUG("receiver is a dictionary");
                    field = m_classprimdict->getMethodField(name);
                    if(field != nullptr)
                    {
                        return vmDoCallNative(field->value.asFunction(), receiver, argcount);
                    }
                    /* NEW in v0.0.84, dictionaries can declare extra methods as part of their entries. */
                    else
                    {
                        field = receiver.asDict()->m_htvalues.getfieldbyostr(name);
                        if(field != nullptr)
                        {
                            if(field->value.isCallable())
                            {
                                return vmCallWithObject(field->value, receiver, argcount, false);
                            }
                        }
                    }
                    return NEON_THROWEXCEPTION("'dict' has no method %s()", name->data());
                }
                default:
                {
                }
                break;
            }
        }
        klass = getClassFor(receiver);
        if(klass == nullptr)
        {
            /* @TODO: have methods for non objects as well. */
            return NEON_THROWEXCEPTION("non-object %s has no method named '%s'", Value::typeName(receiver, false), name->data());
        }
        field = klass->getMethodField(name);
        if(field != nullptr)
        {
            return vmCallWithObject(field->value, receiver, argcount, false);
        }
        return NEON_THROWEXCEPTION("'%s' has no method %s()", klass->m_classname->data(), name->data());
    }

    NEON_INLINE bool SharedState::vmUtilBindMethod(Class* klass, String* name)
    {
        Value val;
        Property* field;
        Function* bound;
        field = klass->m_instmethods.getfieldbyostr(name);
        if(field != nullptr)
        {
            if(Function::getMethodType(field->value) == Function::CTXTYPE_PRIVATE)
            {
                return NEON_THROWEXCEPTION("cannot get private property '%s' from instance", name->data());
            }
            val = vmStackPeek(0);
            bound = Function::makeFuncBound(val, field->value.asFunction());
            vmStackPop();
            vmStackPush(Value::fromObject(bound));
            return true;
        }
        return NEON_THROWEXCEPTION("undefined property '%s'", name->data());
    }

    NEON_INLINE Upvalue* SharedState::vmUtilUpvaluesCapture(Value* local, int stackpos)
    {
        Upvalue* upvalue;
        Upvalue* prevupvalue;
        Upvalue* createdupvalue;
        prevupvalue = nullptr;
        upvalue = m_vmstate.openupvalues;
        while(upvalue != nullptr && (&upvalue->m_location) > local)
        {
            prevupvalue = upvalue;
            upvalue = upvalue->m_next;
        }
        if(upvalue != nullptr && (&upvalue->m_location) == local)
        {
            return upvalue;
        }
        createdupvalue = Upvalue::make(local, stackpos);
        createdupvalue->m_next = upvalue;
        if(prevupvalue == nullptr)
        {
            m_vmstate.openupvalues = createdupvalue;
        }
        else
        {
            prevupvalue->m_next = createdupvalue;
        }
        return createdupvalue;
    }

    NEON_INLINE void SharedState::vmUtilUpvaluesClose(const Value* last)
    {
        Upvalue* upvalue;
        while(m_vmstate.openupvalues != nullptr && (&m_vmstate.openupvalues->m_location) >= last)
        {
            upvalue = m_vmstate.openupvalues;
            upvalue->m_closed = upvalue->m_location;
            upvalue->m_location = upvalue->m_closed;
            m_vmstate.openupvalues = upvalue->m_next;
        }
    }

    NEON_INLINE void SharedState::vmUtilDefineMethod(String* name)
    {
        Value method;
        Class* klass;
        method = vmStackPeek(0);
        klass = vmStackPeek(1).asClass();
        klass->m_instmethods.set(Value::fromObject(name), method);
        if(Function::getMethodType(method) == Function::CTXTYPE_INITIALIZER)
        {
            klass->m_constructor = method;
        }
        vmStackPop();
    }

    NEON_INLINE void SharedState::vmUtilDefineProperty(String* name, bool isstatic)
    {
        Value property;
        Class* klass;
        property = vmStackPeek(0);
        klass = vmStackPeek(1).asClass();
        if(!isstatic)
        {
            klass->defProperty(name, property);
        }
        else
        {
            klass->setStaticProperty(name, property);
        }
        vmStackPop();
    }

    /*
     * don' try to optimize too much here, since its largely irrelevant how big or small
     * the strings are; inevitably it will always be <length-of-string> * number.
     * not preallocating also means that the allocator only allocates as much as actually needed.
     */
    NEON_INLINE String* SharedState::vmUtilMultString(String* str, double number)
    {
        size_t i;
        size_t times;
        IOStream pr;
        String* os;
        times = (size_t)number;
        /* 'str' * 0 == '', 'str' * -1 == '' */
        if(times <= 0)
        {
            return String::intern("", 0);
        }
        /* 'str' * 1 == 'str' */
        else if(times == 1)
        {
            return str;
        }
        IOStream::makeStackString(&pr);
        for(i = 0; i < times; i++)
        {
            pr.writeString(str->data(), str->length());
        }
        os = pr.takeString();
        IOStream::destroy(&pr);
        return os;
    }

    NEON_INLINE Array* SharedState::vmUtilCombineArrays(Array* a, Array* b)
    {
        size_t i;
        Array* list;
        list = Array::make();
        vmStackPush(Value::fromObject(list));
        for(i = 0; i < a->count(); i++)
        {
            list->push(a->get(i));
        }
        for(i = 0; i < b->count(); i++)
        {
            list->push(b->get(i));
        }
        vmStackPop();
        return list;
    }

    NEON_INLINE void SharedState::vmUtilMultArray(Array* from, Array* newlist, size_t times)
    {
        size_t i;
        size_t j;
        for(i = 0; i < times; i++)
        {
            for(j = 0; j < from->count(); j++)
            {
                newlist->push(from->get(j));
            }
        }
    }

    NEON_INLINE bool SharedState::vmUtilDoGetRangedIndexOfArray(Array* list, bool willassign)
    {
        long i;
        long idxlower;
        long idxupper;
        Value valupper;
        Value vallower;
        Array* newlist;
        valupper = vmStackPeek(0);
        vallower = vmStackPeek(1);
        if(!(vallower.isNull() || vallower.isNumber()) || !(valupper.isNumber() || valupper.isNull()))
        {
            vmStackPop(2);
            return NEON_THROWEXCEPTION("list range index expects upper and lower to be numbers, but got '%s', '%s'", Value::typeName(vallower, false), Value::typeName(valupper, false));
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
        if((idxlower < 0) || ((idxupper < 0) && ((long)(list->count() + idxupper) < 0)) || (idxlower >= (long)list->count()))
        {
            /* always return an empty list... */
            if(!willassign)
            {
                /* +1 for the list itself */
                vmStackPop(3);
            }
            vmStackPush(Value::fromObject(Array::make()));
            return true;
        }
        if(idxupper < 0)
        {
            idxupper = list->count() + idxupper;
        }
        if(idxupper > (long)list->count())
        {
            idxupper = list->count();
        }
        newlist = Array::make();
        vmStackPush(Value::fromObject(newlist));
        for(i = idxlower; i < idxupper; i++)
        {
            newlist->push(list->get(i));
        }
        /* clear gc protect */
        vmStackPop();
        if(!willassign)
        {
            /* +1 for the list itself */
            vmStackPop(3);
        }
        vmStackPush(Value::fromObject(newlist));
        return true;
    }

    NEON_INLINE bool SharedState::vmUtilDoGetRangedIndexOfString(String* string, bool willassign)
    {
        int end;
        int start;
        int length;
        int idxupper;
        int idxlower;
        Value valupper;
        Value vallower;
        valupper = vmStackPeek(0);
        vallower = vmStackPeek(1);
        if(!(vallower.isNull() || vallower.isNumber()) || !(valupper.isNumber() || valupper.isNull()))
        {
            vmStackPop(2);
            return NEON_THROWEXCEPTION("string range index expects upper and lower to be numbers, but got '%s', '%s'", Value::typeName(vallower, false), Value::typeName(valupper, false));
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
                vmStackPop(3);
            }
            vmStackPush(Value::fromObject(String::intern("", 0)));
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
            vmStackPop(3);
        }
        vmStackPush(Value::fromObject(String::copy(string->data() + start, end - start)));
        return true;
    }

    NEON_INLINE bool SharedState::vmDoGetRangedIndex()
    {
        bool isgotten;
        uint16_t willassign;
        Value vfrom;
        willassign = vmReadByte();
        isgotten = true;
        vfrom = vmStackPeek(2);
        if(vfrom.isObject())
        {
            switch(vfrom.asObject()->m_objtype)
            {
                case Object::OTYP_STRING:
                {
                    if(!vmUtilDoGetRangedIndexOfString(vfrom.asString(), willassign))
                    {
                        return false;
                    }
                    break;
                }
                case Object::OTYP_ARRAY:
                {
                    if(!vmUtilDoGetRangedIndexOfArray(vfrom.asArray(), willassign))
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
            return NEON_THROWEXCEPTION("cannot range index object of type %s", Value::typeName(vfrom, false));
        }
        return true;
    }

    NEON_INLINE bool SharedState::vmUtilDoIndexGetDict(Dict* dict, bool willassign)
    {
        Value vindex;
        Property* field;
        vindex = vmStackPeek(0);
        field = dict->get(vindex);
        if(field != nullptr)
        {
            if(!willassign)
            {
                /* we can safely get rid of the index from the stack */
                vmStackPop(2);
            }
            vmStackPush(field->value);
            return true;
        }
        vmStackPop(1);
        vmStackPush(Value::makeNull());
        return true;
    }

    NEON_INLINE bool SharedState::vmUtilDoIndexGetModule(Module* module, bool willassign)
    {
        Value vindex;
        Value result;
        vindex = vmStackPeek(0);
        if(module->m_deftable.get(vindex, &result))
        {
            if(!willassign)
            {
                /* we can safely get rid of the index from the stack */
                vmStackPop(2);
            }
            vmStackPush(result);
            return true;
        }
        vmStackPop();
        return NEON_THROWEXCEPTION("%s is undefined in module %s", Value::toString(vindex)->data(), module->m_modname);
    }

    NEON_INLINE bool SharedState::vmUtilDoIndexGetString(String* string, bool willassign)
    {
        bool okindex;
        int end;
        int start;
        int index;
        int maxlength;
        int realindex;
        Value vindex;
        Range* rng;
        (void)realindex;
        okindex = false;
        vindex = vmStackPeek(0);
        if(!vindex.isNumber())
        {
            if(vindex.isRange())
            {
                rng = vindex.asRange();
                vmStackPop();
                vmStackPush(Value::makeNumber(rng->m_lower));
                vmStackPush(Value::makeNumber(rng->m_upper));
                return vmUtilDoGetRangedIndexOfString(string, willassign);
            }
            vmStackPop(1);
            return NEON_THROWEXCEPTION("strings are numerically indexed");
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
            okindex = true;
        }
        start = index;
        end = index + 1;
        if(!willassign)
        {
            /*
            // we can safely get rid of the index from the stack
            // +1 for the string itself
            */
            vmStackPop(2);
        }
        if(okindex)
        {
            vmStackPush(Value::fromObject(String::copy(string->data() + start, end - start)));
        }
        else
        {
            vmStackPush(Value::makeNull());
        }
        return true;

        vmStackPop(1);
    #if 0
            return NEON_THROWEXCEPTION("string index %d out of range of %d", realindex, maxlength);
    #else
        vmStackPush(Value::makeNull());
        return true;
    #endif
    }

    NEON_INLINE bool SharedState::vmUtilDoIndexGetArray(Array* list, bool willassign)
    {
        long index;
        Value finalval;
        Value vindex;
        Range* rng;
        vindex = vmStackPeek(0);
        if(NEON_UNLIKELY(!vindex.isNumber()))
        {
            if(vindex.isRange())
            {
                rng = vindex.asRange();
                vmStackPop();
                vmStackPush(Value::makeNumber(rng->m_lower));
                vmStackPush(Value::makeNumber(rng->m_upper));
                return vmUtilDoGetRangedIndexOfArray(list, willassign);
            }
            vmStackPop();
            return NEON_THROWEXCEPTION("list are numerically indexed");
        }
        index = vindex.asNumber();
        /*
        if(NEON_UNLIKELY(index < 0))
        {
            index = list->count() + index;
        }
        if((index < (long)list->count()) && (index >= 0))
        {
        */
            finalval = list->get(index);
        /*}
        else
        {
            finalval = Value::makeNull();
        }
        */
        if(!willassign)
        {
            /*
            // we can safely get rid of the index from the stack
            // +1 for the list itself
            */
            vmStackPop(2);
        }
        vmStackPush(finalval);
        return true;
    }

    Property* SharedState::vmUtilCheckOverloadRequirements(const char* ccallername, Value target, String* name)
    {
        Property* field;
        if(!target.isInstance())
        {
            fprintf(stderr, "%s: not an instance\n", ccallername);
            return nullptr;
        }
        field = target.asInstance()->getMethod(name);
        if(field == nullptr)
        {
            fprintf(stderr, "%s: failed to get '%s'\n", ccallername, name->data());
            return nullptr;
        }
        if(!field->value.isCallable())
        {
            fprintf(stderr, "%s: field not callable\n", ccallername);
            return nullptr;
        }
        return field;
    }

    NEON_INLINE bool SharedState::vmUtilTryOverloadBasic(String* name, Value target, Value firstargvval, Value setvalue, bool willassign)
    {
        size_t nargc;
        Value finalval;
        Property* field;
        Value scrargv[3];
        nargc = 1;
        field = vmUtilCheckOverloadRequirements("tryoverloadgeneric", target, name);
        if(field == nullptr)
        {
            return NEON_THROWEXCEPTION("failed to get operator overload");
        }
        scrargv[0] = firstargvval;
        if(willassign)
        {
            scrargv[0] = setvalue;
            scrargv[1] = firstargvval;
            nargc = 2;
        }
        if(vmNestCallFunction(field->value, target, scrargv, nargc, &finalval, true))
        {
            if(!willassign)
            {
                vmStackPop(2);
            }
            vmStackPush(finalval);
            return true;
        }
        return false;
    }

    NEON_INLINE bool SharedState::vmUtilTryOverloadMath(String* name, Value target, Value right, bool willassign)
    {
        return vmUtilTryOverloadBasic(name, target, right, Value::makeNull(), willassign);
    }

    NEON_INLINE bool SharedState::vmUtilTryOverloadGeneric(String* name, Value target, bool willassign)
    {
        Value setval;
        Value firstargval;
        firstargval = vmStackPeek(0);
        setval = vmStackPeek(1);
        return vmUtilTryOverloadBasic(name, target, firstargval, setval, willassign);
    }

    NEON_INLINE bool SharedState::vmDoIndexGet()
    {
        bool isgotten;
        uint16_t willassign;
        Value thisval;
        willassign = vmReadByte();
        isgotten = true;
        thisval = vmStackPeek(1);
        if(NEON_UNLIKELY(thisval.isInstance()))
        {
            if(vmUtilTryOverloadGeneric(m_defaultstrings.nmindexget, thisval, willassign))
            {
                return true;
            }
        }
        if(NEON_LIKELY(thisval.isObject()))
        {
            switch(thisval.asObject()->m_objtype)
            {
                case Object::OTYP_STRING:
                {
                    if(!vmUtilDoIndexGetString(thisval.asString(), willassign))
                    {
                        return false;
                    }
                    break;
                }
                case Object::OTYP_ARRAY:
                {
                    if(!vmUtilDoIndexGetArray(thisval.asArray(), willassign))
                    {
                        return false;
                    }
                    break;
                }
                case Object::OTYP_DICT:
                {
                    if(!vmUtilDoIndexGetDict(thisval.asDict(), willassign))
                    {
                        return false;
                    }
                    break;
                }
                case Object::OTYP_MODULE:
                {
                    if(!vmUtilDoIndexGetModule(thisval.asModule(), willassign))
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
            NEON_THROWEXCEPTION("cannot index object of type %s", Value::typeName(thisval, false));
        }
        return true;
    }

    NEON_INLINE bool SharedState::vmUtilDoSetIndexDict(Dict* dict, Value index, Value value)
    {
        dict->set(index, value);
        /* pop the value, index and dict out */
        vmStackPop(3);
        /*
        // leave the value on the stack for consumption
        // e.g. variable = dict[index] = 10
        */
        vmStackPush(value);
        return true;
    }

    NEON_INLINE bool SharedState::vmUtilDoSetIndexModule(Module* module, Value index, Value value)
    {
        module->m_deftable.set(index, value);
        /* pop the value, index and dict out */
        vmStackPop(3);
        /*
        // leave the value on the stack for consumption
        // e.g. variable = dict[index] = 10
        */
        vmStackPush(value);
        return true;
    }
    NEON_INLINE bool SharedState::vmUtilDoSetIndexArray(Array* list, Value index, Value value)
    {
        int rawpos;
        int position;
        if(NEON_UNLIKELY(!index.isNumber()))
        {
            vmStackPop(3);
            /* pop the value, index and list out */
            return NEON_THROWEXCEPTION("list are numerically indexed");
        }
        rawpos = index.asNumber();
        position = rawpos;
        list->set(position, value);
        /* pop the value, index and list out */
        vmStackPop(3);
        /*
        // leave the value on the stack for consumption
        // e.g. variable = list[index] = 10
        */
        vmStackPush(value);
        return true;
        /*
        // pop the value, index and list out
        //vmStackPop(3);
        //return NEON_THROWEXCEPTION("lists index %d out of range", rawpos);
        //vmStackPush(Value::makeNull());
        //return true;
        */
    }

    NEON_INLINE bool SharedState::vmUtilDoSetIndexString(String* os, Value index, Value value)
    {
        int iv;
        int rawpos;
        int position;
        int oslen;
        if(!index.isNumber())
        {
            vmStackPop(3);
            /* pop the value, index and list out */
            return NEON_THROWEXCEPTION("strings are numerically indexed");
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
            os->set(position, iv);
            /* pop the value, index and list out */
            vmStackPop(3);
            /*
            // leave the value on the stack for consumption
            // e.g. variable = list[index] = 10
            */
            vmStackPush(value);
            return true;
        }
        else
        {
            os->appendByte(iv);
            vmStackPop(3);
            vmStackPush(value);
        }
        return true;
    }

    NEON_INLINE bool SharedState::vmDoIndexSet()
    {
        bool isset;
        Value value;
        Value index;
        Value thisval;
        isset = true;
        thisval = vmStackPeek(2);
        if(NEON_UNLIKELY(thisval.isInstance()))
        {
            if(vmUtilTryOverloadGeneric(m_defaultstrings.nmindexset, thisval, true))
            {
                return true;
            }
        }
        if(NEON_LIKELY(thisval.isObject()))
        {
            value = vmStackPeek(0);
            index = vmStackPeek(1);
            switch(thisval.asObject()->m_objtype)
            {
                case Object::OTYP_ARRAY:
                {
                    if(!vmUtilDoSetIndexArray(thisval.asArray(), index, value))
                    {
                        return false;
                    }
                }
                break;
                case Object::OTYP_STRING:
                {
                    if(!vmUtilDoSetIndexString(thisval.asString(), index, value))
                    {
                        return false;
                    }
                }
                break;
                case Object::OTYP_DICT:
                {
                    return vmUtilDoSetIndexDict(thisval.asDict(), index, value);
                }
                break;
                case Object::OTYP_MODULE:
                {
                    return vmUtilDoSetIndexModule(thisval.asModule(), index, value);
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
            return NEON_THROWEXCEPTION("type of %s is not a valid iterable", Value::typeName(vmStackPeek(3), false));
        }
        return true;
    }

    NEON_INLINE bool SharedState::vmUtilConcatenate()
    {
        Value vleft;
        Value vright;
        IOStream pr;
        String* result;
        vright = vmStackPeek(0);
        vleft = vmStackPeek(1);
        IOStream::makeStackString(&pr);
        ValPrinter::printValue(&pr, vleft, false, true);
        ValPrinter::printValue(&pr, vright, false, true);
        result = pr.takeString();
        IOStream::destroy(&pr);
        vmStackPop(2);
        vmStackPush(Value::fromObject(result));
        return true;
    }

    NEON_INLINE Value vmCallbackModulo(double a, double b)
    {
        double r;
        r = fmod(a, b);
        if(r != 0 && ((r < 0) != (b < 0)))
        {
            r += b;
        }
        return Value::makeNumber(r);
    }

    NEON_INLINE Value vmCallbackPow(double a, double b)
    {
        double r;
        r = pow(a, b);
        return Value::makeNumber(r);
    }

    NEON_INLINE Property* SharedState::vmUtilGetClassProperty(Class* klass, String* name, bool alsothrow)
    {
        Property* field;
        field = klass->m_instmethods.getfieldbyostr(name);
        if(field != nullptr)
        {
            if(Function::getMethodType(field->value) == Function::CTXTYPE_STATIC)
            {
                if(Class::methodNameIsPrivate(name))
                {
                    if(alsothrow)
                    {
                        NEON_THROWEXCEPTION("cannot call private property '%s' of class %s", name->data(), klass->m_classname->data());
                    }
                    return nullptr;
                }
                return field;
            }
        }
        else
        {
            field = klass->getStaticProperty(name);
            if(field != nullptr)
            {
                if(Class::methodNameIsPrivate(name))
                {
                    if(alsothrow)
                    {
                        NEON_THROWEXCEPTION("cannot call private property '%s' of class %s", name->data(), klass->m_classname->data());
                    }
                    return nullptr;
                }
                return field;
            }
            else
            {
                field = klass->getStaticMethodField(name);
                if(field != nullptr)
                {
                    return field;
                }
            }
        }
        if(alsothrow)
        {
            NEON_THROWEXCEPTION("class %s does not have a static property or method named '%s'", klass->m_classname->data(), name->data());
        }
        return nullptr;
    }

    NEON_INLINE Property* SharedState::vmUtilGetProperty(Value peeked, String* name)
    {
        Property* field;
        switch(peeked.asObject()->m_objtype)
        {
            case Object::OTYP_MODULE:
            {
                Module* module;
                module = peeked.asModule();
                field = module->m_deftable.getfieldbyostr(name);
                if(field != nullptr)
                {
                    if(Class::methodNameIsPrivate(name))
                    {
                        NEON_THROWEXCEPTION("cannot get private module property '%s'", name->data());
                        return nullptr;
                    }
                    return field;
                }
                NEON_THROWEXCEPTION("module '%s' does not have a field named '%s'", module->m_modname->data(), name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_CLASS:
            {
                Class* klass;
                klass = peeked.asClass();
                field = vmUtilGetClassProperty(klass, name, true);
                if(field != nullptr)
                {
                    return field;
                }
                return nullptr;
            }
            break;
            case Object::OTYP_INSTANCE:
            {
                Instance* instance;
                instance = peeked.asInstance();
                field = instance->m_instanceprops.getfieldbyostr(name);
                if(field != nullptr)
                {
                    if(Class::methodNameIsPrivate(name))
                    {
                        NEON_THROWEXCEPTION("cannot call private property '%s' from instance of %s", name->data(), instance->m_instanceclass->m_classname->data());
                        return nullptr;
                    }
                    return field;
                }
                if(Class::methodNameIsPrivate(name))
                {
                    NEON_THROWEXCEPTION("cannot bind private property '%s' to instance of %s", name->data(), instance->m_instanceclass->m_classname->data());
                    return nullptr;
                }
                if(vmUtilBindMethod(instance->m_instanceclass, name))
                {
                    return field;
                }
                NEON_THROWEXCEPTION("instance of class %s does not have a property or method named '%s'", peeked.asInstance()->m_instanceclass->m_classname->data(), name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_STRING:
            {
                field = m_classprimstring->getPropertyField(name);
                if(field == nullptr)
                {
                    field = vmUtilGetClassProperty(m_classprimstring, name, false);
                }
                if(field != nullptr)
                {
                    return field;
                }
                NEON_THROWEXCEPTION("class 'String' has no named property '%s'", name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_ARRAY:
            {
                field = m_classprimarray->getPropertyField(name);
                if(field == nullptr)
                {
                    field = vmUtilGetClassProperty(m_classprimarray, name, false);
                }
                if(field != nullptr)
                {
                    return field;
                }
                NEON_THROWEXCEPTION("class 'Array' has no named property '%s'", name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_RANGE:
            {
                field = m_classprimrange->getPropertyField(name);
                if(field == nullptr)
                {
                    field = vmUtilGetClassProperty(m_classprimrange, name, false);
                }
                if(field != nullptr)
                {
                    return field;
                }
                NEON_THROWEXCEPTION("class 'Range' has no named property '%s'", name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_DICT:
            {
                field = peeked.asDict()->m_htvalues.getfieldbyostr(name);
                if(field == nullptr)
                {
                    field = m_classprimdict->getPropertyField(name);
                }
                if(field != nullptr)
                {
                    return field;
                }
                NEON_THROWEXCEPTION("unknown key or class 'Dict' property '%s'", name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_FILE:
            {
                field = m_classprimfile->getPropertyField(name);
                if(field == nullptr)
                {
                    field = vmUtilGetClassProperty(m_classprimfile, name, false);
                }
                if(field != nullptr)
                {
                    return field;
                }
                NEON_THROWEXCEPTION("class 'File' has no named property '%s'", name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_FUNCBOUND:
            case Object::OTYP_FUNCCLOSURE:
            case Object::OTYP_FUNCSCRIPT:
            case Object::OTYP_FUNCNATIVE:
            {
                field = m_classprimcallable->getPropertyField(name);
                if(field != nullptr)
                {
                    return field;
                }
                else
                {
                    field = vmUtilGetClassProperty(m_classprimcallable, name, false);
                    if(field != nullptr)
                    {
                        return field;
                    }
                }
                NEON_THROWEXCEPTION("class 'Function' has no named property '%s'", name->data());
                return nullptr;
            }
            break;
            default:
            {
                NEON_THROWEXCEPTION("object of type %s does not carry properties", Value::typeName(peeked, false));
                return nullptr;
            }
            break;
        }
        return nullptr;
    }

    NEON_INLINE bool SharedState::vmDoPropertyGetNormal()
    {
        Value peeked;
        Property* field;
        String* name;
        name = vmReadString();
        peeked = vmStackPeek(0);
        if(peeked.isObject())
        {
            field = vmUtilGetProperty(peeked, name);
            if(field == nullptr)
            {
                return false;
            }
            else
            {
                if(field->m_fieldtype == Property::FTYP_FUNCTION)
                {
                    vmCallWithObject(field->value, peeked, 0, false);
                }
                else
                {
                    vmStackPop();
                    vmStackPush(field->value);
                }
            }
            return true;
        }
        else
        {
            NEON_THROWEXCEPTION("'%s' of type %s does not have properties", Value::toString(peeked)->data(), Value::typeName(peeked, false));
        }
        return false;
    }

    NEON_INLINE bool SharedState::vmDoPropertyGetSelf()
    {
        Value peeked;
        String* name;
        Class* klass;
        Instance* instance;
        Module* module;
        Property* field;
        name = vmReadString();
        peeked = vmStackPeek(0);
        if(peeked.isInstance())
        {
            instance = peeked.asInstance();
            field = instance->m_instanceprops.getfieldbyostr(name);
            if(field != nullptr)
            {
                /* pop the instance... */
                vmStackPop();
                vmStackPush(field->value);
                return true;
            }
            if(vmUtilBindMethod(instance->m_instanceclass, name))
            {
                return true;
            }
            VMMAC_TRYRAISE(false, "instance of class %s does not have a property or method named '%s'", peeked.asInstance()->m_instanceclass->m_classname->data(), name->data());
            return false;
        }
        else if(peeked.isClass())
        {
            klass = peeked.asClass();
            field = klass->m_instmethods.getfieldbyostr(name);
            if(field != nullptr)
            {
                if(Function::getMethodType(field->value) == Function::CTXTYPE_STATIC)
                {
                    /* pop the class... */
                    vmStackPop();
                    vmStackPush(field->value);
                    return true;
                }
            }
            else
            {
                field = klass->getStaticProperty(name);
                if(field != nullptr)
                {
                    /* pop the class... */
                    vmStackPop();
                    vmStackPush(field->value);
                    return true;
                }
            }
            VMMAC_TRYRAISE(false, "class %s does not have a static property or method named '%s'", klass->m_classname->data(), name->data());
            return false;
        }
        else if(peeked.isModule())
        {
            module = peeked.asModule();
            field = module->m_deftable.getfieldbyostr(name);
            if(field != nullptr)
            {
                /* pop the module... */
                vmStackPop();
                vmStackPush(field->value);
                return true;
            }
            VMMAC_TRYRAISE(false, "module '%s' does not have a field named '%s'", module->m_modname->data(), name->data());
            return false;
        }
        VMMAC_TRYRAISE(false, "'%s' of type %s does not have properties", Value::toString(peeked)->data(), Value::typeName(peeked, false));
        return false;
    }

    NEON_INLINE bool SharedState::vmDoPropertySet()
    {
        Value value;
        Value vtarget;
        Value vpeek;
        Class* klass;
        String* name;
        Dict* dict;
        Instance* instance;
        vtarget = vmStackPeek(1);
        name = vmReadString();
        vpeek = vmStackPeek(0);
        if(vtarget.isInstance())
        {
            instance = vtarget.asInstance();
            instance->defProperty(name, vpeek);
            value = vmStackPop();
            /* removing the instance object */
            vmStackPop();
            vmStackPush(value);
        }
        else if(vtarget.isDict())
        {
            dict = vtarget.asDict();
            dict->set(Value::fromObject(name), vpeek);
            value = vmStackPop();
            /* removing the dictionary object */
            vmStackPop();
            vmStackPush(value);
        }
        /* ....isClass() */
        else
        {
            klass = nullptr;
            if(vtarget.isClass())
            {
                klass = vtarget.asClass();
            }
            else if(vtarget.isInstance())
            {
                klass = vtarget.asInstance()->m_instanceclass;
            }
            else
            {
                klass = getClassFor(vtarget);
                /* still no class found? then it cannot carry properties */
                if(klass == nullptr)
                {
                    NEON_THROWEXCEPTION("object of type %s cannot carry properties", Value::typeName(vtarget, false));
                    return false;
                }
            }
            if(vpeek.isCallable())
            {
                klass->defMethod(name, vpeek);
            }
            else
            {
                klass->defProperty(name, vpeek);
            }
            value = vmStackPop();
            /* removing the class object */
            vmStackPop();
            vmStackPush(value);
        }

        return true;
    }

    NEON_INLINE bool SharedState::vmDoBinaryDirect()
    {
        bool isfail;
        bool willassign;
        int64_t ibinright;
        int64_t ibinleft;
        uint32_t ubinright;
        uint32_t ubinleft;
        double dbinright;
        double dbinleft;
        Instruction::OpCode instruction;
        Value res;
        Value binvalleft;
        Value binvalright;
        willassign = false;
        instruction = (Instruction::OpCode)m_vmstate.currentinstr.code;
        binvalright = vmStackPeek(0);
        binvalleft = vmStackPeek(1);
        if(NEON_UNLIKELY(binvalleft.isInstance()))
        {
            switch(instruction)
            {
                case Instruction::OPC_PRIMADD:
                {
                    if(vmUtilTryOverloadMath(m_defaultstrings.nmadd, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
                case Instruction::OPC_PRIMSUBTRACT:
                {
                    if(vmUtilTryOverloadMath(m_defaultstrings.nmsub, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
                case Instruction::OPC_PRIMDIVIDE:
                {
                    if(vmUtilTryOverloadMath(m_defaultstrings.nmdiv, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
                case Instruction::OPC_PRIMMULTIPLY:
                {
                    if(vmUtilTryOverloadMath(m_defaultstrings.nmmul, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
                case Instruction::OPC_PRIMAND:
                {
                    if(vmUtilTryOverloadMath(m_defaultstrings.nmband, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
                case Instruction::OPC_PRIMOR:
                {
                    if(vmUtilTryOverloadMath(m_defaultstrings.nmbor, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
                case Instruction::OPC_PRIMBITXOR:
                {
                    if(vmUtilTryOverloadMath(m_defaultstrings.nmbxor, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
                default:
                {
                }
                break;
            }
        }
        isfail = ((!binvalright.isNumber() && !binvalright.isBool() && !binvalright.isNull()) || (!binvalleft.isNumber() && !binvalleft.isBool() && !binvalleft.isNull()));
        if(isfail)
        {
            VMMAC_TRYRAISE(false, "unsupported operand %s for %s and %s", Debug::opcodeToString(instruction), Value::typeName(binvalleft, false), Value::typeName(binvalright, false));
            return false;
        }
        binvalright = vmStackPop();
        binvalleft = vmStackPop();
        res = Value::makeNull();
        switch(instruction)
        {
            case Instruction::OPC_PRIMADD:
            {
                dbinright = Value::valToNumber(binvalright);
                dbinleft = Value::valToNumber(binvalleft);
                res = Value::makeNumber(dbinleft + dbinright);
            }
            break;
            case Instruction::OPC_PRIMSUBTRACT:
            {
                dbinright = Value::valToNumber(binvalright);
                dbinleft = Value::valToNumber(binvalleft);
                res = Value::makeNumber(dbinleft - dbinright);
            }
            break;
            case Instruction::OPC_PRIMDIVIDE:
            {
                dbinright = Value::valToNumber(binvalright);
                dbinleft = Value::valToNumber(binvalleft);
                res = Value::makeNumber(dbinleft / dbinright);
            }
            break;
            case Instruction::OPC_PRIMMULTIPLY:
            {
                dbinright = Value::valToNumber(binvalright);
                dbinleft = Value::valToNumber(binvalleft);
                res = Value::makeNumber(dbinleft * dbinright);
            }
            break;
            case Instruction::OPC_PRIMAND:
            {
                ibinright = Value::valToInt(binvalright);
                ibinleft = Value::valToInt(binvalleft);
                res = Value::makeNumber(ibinleft & ibinright);
            }
            break;
            case Instruction::OPC_PRIMOR:
            {
                ibinright = Value::valToInt(binvalright);
                ibinleft = Value::valToInt(binvalleft);
                res = Value::makeNumber(ibinleft | ibinright);
            }
            break;
            case Instruction::OPC_PRIMBITXOR:
            {
                ibinright = Value::valToInt(binvalright);
                ibinleft = Value::valToInt(binvalleft);
                res = Value::makeNumber(ibinleft ^ ibinright);
            }
            break;
            case Instruction::OPC_PRIMSHIFTLEFT:
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
                ubinright = Value::valToUint(binvalright);
                ubinleft = Value::valToUint(binvalleft);
                ubinright &= 0x1f;
                res = Value::makeNumber(ubinleft << ubinright);
            }
            break;
            case Instruction::OPC_PRIMSHIFTRIGHT:
            {
                /*
                    uint32_t v2;
                    v2 = JS_VALUE_GET_INT(op2);
                    v2 &= 0x1f;
                    sp[-2] = JS_NewUint32(ctx, (uint32_t)JS_VALUE_GET_INT(op1) >> v2);
                */
                ubinright = Value::valToUint(binvalright);
                ubinleft = Value::valToUint(binvalleft);
                ubinright &= 0x1f;
                res = Value::makeNumber(ubinleft >> ubinright);
            }
            break;
            case Instruction::OPC_PRIMGREATER:
            {
                dbinright = Value::valToNumber(binvalright);
                dbinleft = Value::valToNumber(binvalleft);
                res = Value::makeBool(dbinleft > dbinright);
            }
            break;
            case Instruction::OPC_PRIMLESSTHAN:
            {
                dbinright = Value::valToNumber(binvalright);
                dbinleft = Value::valToNumber(binvalleft);
                res = Value::makeBool(dbinleft < dbinright);
            }
            break;
            default:
            {
                fprintf(stderr, "unhandled instruction %d (%s)!\n", instruction, Debug::opcodeToString(instruction));
                return false;
            }
            break;
        }
        vmStackPush(res);
        return true;
    }

    NEON_INLINE bool SharedState::vmDoGlobalDefine()
    {
        Value val;
        String* name;
        name = vmReadString();
        val = vmStackPeek(0);
        auto tab = &m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.module->m_deftable;
        tab->set(Value::fromObject(name), val);
        vmStackPop();
        return true;
    }

    NEON_INLINE bool SharedState::vmDoGlobalGet()
    {
        String* name;
        Property* field;
        name = vmReadString();
        auto tab = &m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.module->m_deftable;
        field = tab->getfieldbyostr(name);
        if(field == nullptr)
        {
            field = m_declaredglobals.getfieldbyostr(name);
            if(field == nullptr)
            {
                NEON_THROWCLASSWITHSOURCEINFO(m_exceptions.stdexception, "global name '%s' is not defined", name->data());
                return false;
            }
        }
        vmStackPush(field->value);
        return true;
    }

    NEON_INLINE bool SharedState::vmDoGlobalSet()
    {
        String* name;
        Module* module;
        name = vmReadString();
        module = m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.module;
        auto table = &module->m_deftable;
        if(table->set(Value::fromObject(name), vmStackPeek(0)))
        {
            if(m_conf.enablestrictmode)
            {
                table->remove(Value::fromObject(name));
                VMMAC_TRYRAISE(false, "global name '%s' was not declared", name->data());
                return false;
            }
        }
        return true;
    }

    NEON_INLINE bool SharedState::vmDoLocalGet()
    {
        size_t ssp;
        uint16_t slot;
        Value val;
        slot = vmReadShort();
        ssp = m_vmstate.currentframe->stackslotpos;
        val = m_vmstate.stackvalues[ssp + slot];
        vmStackPush(val);
        return true;
    }

    NEON_INLINE bool SharedState::vmDoLocalSet()
    {
        size_t ssp;
        uint16_t slot;
        Value peeked;
        slot = vmReadShort();
        peeked = vmStackPeek(0);
        ssp = m_vmstate.currentframe->stackslotpos;
        m_vmstate.stackvalues[ssp + slot] = peeked;
        return true;
    }

    /*Instruction::OPC_FUNCARGOPTIONAL*/
    NEON_INLINE bool SharedState::vmDoFuncArgOptional()
    {
        size_t ssp;
        size_t putpos;
        uint16_t slot;
        Value cval;
        Value peeked;
        slot = vmReadShort();
        peeked = vmStackPeek(1);
        cval = vmStackPeek(2);
        ssp = m_vmstate.currentframe->stackslotpos;
        #if 0
            putpos = (m_vmstate.stackidx + (-1 - 1)) ;
        #else
            #if 0
                putpos = m_vmstate.stackidx + (slot - 0);
            #else
                #if 0
                    putpos = m_vmstate.stackidx + (slot);
                #else
                    putpos = (ssp + slot) + 0;
                #endif
            #endif
        #endif
        #if 1
            {
                IOStream* pr = m_stderrprinter;
                pr->format("funcargoptional: slot=%d putpos=%zd cval=<", slot, putpos);
                ValPrinter::printValue(pr, cval, true, false);
                pr->format(">, peeked=<");
                ValPrinter::printValue(pr, peeked, true, false);
                pr->format(">\n");
            }
        #endif
        if(cval.isNull())
        {
            m_vmstate.stackvalues[putpos] = peeked;
        }
        /*
        else
        {
            #if 0
                vmStackPop();
            #endif
        }
        */
        vmStackPop();
        return true;
    }

    NEON_INLINE bool SharedState::vmDoFuncArgGet()
    {
        size_t ssp;
        uint16_t slot;
        Value val;
        slot = vmReadShort();
        ssp = m_vmstate.currentframe->stackslotpos;
        val = m_vmstate.stackvalues[ssp + slot];
        vmStackPush(val);
        return true;
    }

    NEON_INLINE bool SharedState::vmDoFuncArgSet()
    {
        size_t ssp;
        uint16_t slot;
        Value peeked;
        slot = vmReadShort();
        peeked = vmStackPeek(0);
        ssp = m_vmstate.currentframe->stackslotpos;
        m_vmstate.stackvalues[ssp + slot] = peeked;
        return true;
    }

    NEON_INLINE bool SharedState::vmDoMakeClosure()
    {
        size_t i;
        int upvidx;
        size_t ssp;
        uint16_t islocal;
        Value thisval;
        Value* upvals;
        Function* function;
        Function* closure;
        function = vmReadConst().asFunction();
    #if 0
            thisval = vmStackPeek(3);
    #else
        thisval = Value::makeNull();
    #endif
        closure = Function::makeFuncClosure(function, thisval);
        vmStackPush(Value::fromObject(closure));
        for(i = 0; i < (size_t)closure->m_upvalcount; i++)
        {
            islocal = vmReadByte();
            upvidx = vmReadShort();
            if(islocal)
            {
                ssp = m_vmstate.currentframe->stackslotpos;
                upvals = m_vmstate.stackvalues.getp(ssp + upvidx);
                closure->m_fnvals.fnclosure.m_upvalues[i] = vmUtilUpvaluesCapture(upvals, upvidx);
            }
            else
            {
                closure->m_fnvals.fnclosure.m_upvalues[i] = m_vmstate.currentframe->closure->m_fnvals.fnclosure.m_upvalues[upvidx];
            }
        }
        return true;
    }

    NEON_INLINE bool SharedState::vmDoMakeArray()
    {
        int i;
        int count;
        Array* array;
        count = vmReadShort();
        array = Array::make();
        m_vmstate.stackvalues[m_vmstate.stackidx + (-count - 1)] = Value::fromObject(array);
        for(i = count - 1; i >= 0; i--)
        {
            array->push(vmStackPeek(i));
        }
        vmStackPop(count);
        return true;
    }

    NEON_INLINE bool SharedState::vmDoMakeDict()
    {
        size_t i;
        size_t count;
        size_t realcount;
        Value name;
        Value value;
        Dict* dict;
        /* 1 for key, 1 for value */
        realcount = vmReadShort();
        count = realcount * 2;
        dict = Dict::make();
        m_vmstate.stackvalues[m_vmstate.stackidx + (-count - 1)] = Value::fromObject(dict);
        for(i = 0; i < count; i += 2)
        {
            name = m_vmstate.stackvalues[m_vmstate.stackidx + (-count + i)];
            if(!name.isString() && !name.isNumber() && !name.isBool())
            {
                VMMAC_TRYRAISE(false, "dictionary key must be one of string, number or boolean");
                return false;
            }
            value = m_vmstate.stackvalues[m_vmstate.stackidx + (-count + i + 1)];
            dict->set(name, value);
        }
        vmStackPop(count);
        return true;
    }

    NEON_INLINE bool SharedState::vmDoBinaryFunc(const char* opname, BinOpFuncFN opfn)
    {
        double dbinright;
        double dbinleft;
        Value binvalright;
        Value binvalleft;
        binvalright = vmStackPeek(0);
        binvalleft = vmStackPeek(1);
        if((!binvalright.isNumber() && !binvalright.isBool()) || (!binvalleft.isNumber() && !binvalleft.isBool()))
        {
            VMMAC_TRYRAISE(false, "unsupported operand %s for %s and %s", opname, Value::typeName(binvalleft, false), Value::typeName(binvalright, false));
            return false;
        }
        binvalright = vmStackPop();
        dbinright = binvalright.isBool() ? (binvalright.asBool() ? 1 : 0) : binvalright.asNumber();
        binvalleft = vmStackPop();
        dbinleft = binvalleft.isBool() ? (binvalleft.asBool() ? 1 : 0) : binvalleft.asNumber();
        vmStackPush(opfn(dbinleft, dbinright));
        return true;
    }

    /*
     * something about using computed goto is currently breaking some scripts, specifically
     * code generated for things like `somevar[idx]++`
     * no issue with switch/case, though.
     */

    #define NEON_CONFIG_USECOMPUTEDGOTO 0
    /*
    #if defined(__GNUC__)
        #if defined(NEON_CONFIG_USECOMPUTEDGOTO)
            #undef NEON_CONFIG_USECOMPUTEDGOTO
            #define NEON_CONFIG_USECOMPUTEDGOTO 1
        #endif
    #endif
    */
    #if defined(NEON_CONFIG_USECOMPUTEDGOTO) && (NEON_CONFIG_USECOMPUTEDGOTO == 1)
        #define NEON_SETDISPATCHIDX(idx, val) [Instruction::idx] = val
        #define VM_MAKELABEL(op) LABEL_##op
        #define VM_CASE(op) VM_MAKELABEL(op) :
        #if 1
            #define VMMAC_DISPATCH() goto readnextinstruction
        #else
            #define VMMAC_DISPATCH() continue
        #endif
    #else
        #define VM_CASE(op) case Instruction::op:
        #define VMMAC_DISPATCH() break
    #endif

    Status SharedState::runVM(int exitframe, Value* rv)
    {
        int iterpos;
        int printpos;
        int ofs;
        /*
         * this variable is a NOP; it only exists to ensure that functions outside of the
         * switch tree are not calling VMMAC_EXITVM(), as its behavior could be undefined.
         */
        bool you_are_calling_exit_vm_outside_of_runvm;
        Value* dbgslot;
        Instruction currinstr;
    #if defined(NEON_CONFIG_USECOMPUTEDGOTO) && (NEON_CONFIG_USECOMPUTEDGOTO == 1)
        static void* dispatchtable[] = {
            NEON_SETDISPATCHIDX(OPC_GLOBALDEFINE, &&VM_MAKELABEL(OPC_GLOBALDEFINE)),
            NEON_SETDISPATCHIDX(OPC_GLOBALGET, &&VM_MAKELABEL(OPC_GLOBALGET)),
            NEON_SETDISPATCHIDX(OPC_GLOBALSET, &&VM_MAKELABEL(OPC_GLOBALSET)),
            NEON_SETDISPATCHIDX(OPC_LOCALGET, &&VM_MAKELABEL(OPC_LOCALGET)),
            NEON_SETDISPATCHIDX(OPC_LOCALSET, &&VM_MAKELABEL(OPC_LOCALSET)),
            NEON_SETDISPATCHIDX(OPC_FUNCARGOPTIONAL, &&VM_MAKELABEL(OPC_FUNCARGOPTIONAL)),
            NEON_SETDISPATCHIDX(OPC_FUNCARGSET, &&VM_MAKELABEL(OPC_FUNCARGSET)),
            NEON_SETDISPATCHIDX(OPC_FUNCARGGET, &&VM_MAKELABEL(OPC_FUNCARGGET)),
            NEON_SETDISPATCHIDX(OPC_UPVALUEGET, &&VM_MAKELABEL(OPC_UPVALUEGET)),
            NEON_SETDISPATCHIDX(OPC_UPVALUESET, &&VM_MAKELABEL(OPC_UPVALUESET)),
            NEON_SETDISPATCHIDX(OPC_UPVALUECLOSE, &&VM_MAKELABEL(OPC_UPVALUECLOSE)),
            NEON_SETDISPATCHIDX(OPC_PROPERTYGET, &&VM_MAKELABEL(OPC_PROPERTYGET)),
            NEON_SETDISPATCHIDX(OPC_PROPERTYGETSELF, &&VM_MAKELABEL(OPC_PROPERTYGETSELF)),
            NEON_SETDISPATCHIDX(OPC_PROPERTYSET, &&VM_MAKELABEL(OPC_PROPERTYSET)),
            NEON_SETDISPATCHIDX(OPC_JUMPIFFALSE, &&VM_MAKELABEL(OPC_JUMPIFFALSE)),
            NEON_SETDISPATCHIDX(OPC_JUMPNOW, &&VM_MAKELABEL(OPC_JUMPNOW)),
            NEON_SETDISPATCHIDX(OPC_LOOP, &&VM_MAKELABEL(OPC_LOOP)),
            NEON_SETDISPATCHIDX(OPC_EQUAL, &&VM_MAKELABEL(OPC_EQUAL)),
            NEON_SETDISPATCHIDX(OPC_PRIMGREATER, &&VM_MAKELABEL(OPC_PRIMGREATER)),
            NEON_SETDISPATCHIDX(OPC_PRIMLESSTHAN, &&VM_MAKELABEL(OPC_PRIMLESSTHAN)),
            NEON_SETDISPATCHIDX(OPC_PUSHEMPTY, &&VM_MAKELABEL(OPC_PUSHEMPTY)),
            NEON_SETDISPATCHIDX(OPC_PUSHNULL, &&VM_MAKELABEL(OPC_PUSHNULL)),
            NEON_SETDISPATCHIDX(OPC_PUSHTRUE, &&VM_MAKELABEL(OPC_PUSHTRUE)),
            NEON_SETDISPATCHIDX(OPC_PUSHFALSE, &&VM_MAKELABEL(OPC_PUSHFALSE)),
            NEON_SETDISPATCHIDX(OPC_PRIMADD, &&VM_MAKELABEL(OPC_PRIMADD)),
            NEON_SETDISPATCHIDX(OPC_PRIMSUBTRACT, &&VM_MAKELABEL(OPC_PRIMSUBTRACT)),
            NEON_SETDISPATCHIDX(OPC_PRIMMULTIPLY, &&VM_MAKELABEL(OPC_PRIMMULTIPLY)),
            NEON_SETDISPATCHIDX(OPC_PRIMDIVIDE, &&VM_MAKELABEL(OPC_PRIMDIVIDE)),
            NEON_SETDISPATCHIDX(OPC_PRIMMODULO, &&VM_MAKELABEL(OPC_PRIMMODULO)),
            NEON_SETDISPATCHIDX(OPC_PRIMPOW, &&VM_MAKELABEL(OPC_PRIMPOW)),
            NEON_SETDISPATCHIDX(OPC_PRIMNEGATE, &&VM_MAKELABEL(OPC_PRIMNEGATE)),
            NEON_SETDISPATCHIDX(OPC_PRIMNOT, &&VM_MAKELABEL(OPC_PRIMNOT)),
            NEON_SETDISPATCHIDX(OPC_PRIMBITNOT, &&VM_MAKELABEL(OPC_PRIMBITNOT)),
            NEON_SETDISPATCHIDX(OPC_PRIMAND, &&VM_MAKELABEL(OPC_PRIMAND)),
            NEON_SETDISPATCHIDX(OPC_PRIMOR, &&VM_MAKELABEL(OPC_PRIMOR)),
            NEON_SETDISPATCHIDX(OPC_PRIMBITXOR, &&VM_MAKELABEL(OPC_PRIMBITXOR)),
            NEON_SETDISPATCHIDX(OPC_PRIMSHIFTLEFT, &&VM_MAKELABEL(OPC_PRIMSHIFTLEFT)),
            NEON_SETDISPATCHIDX(OPC_PRIMSHIFTRIGHT, &&VM_MAKELABEL(OPC_PRIMSHIFTRIGHT)),
            NEON_SETDISPATCHIDX(OPC_PUSHONE, &&VM_MAKELABEL(OPC_PUSHONE)),
            NEON_SETDISPATCHIDX(OPC_PUSHCONSTANT, &&VM_MAKELABEL(OPC_PUSHCONSTANT)),
            NEON_SETDISPATCHIDX(OPC_ECHO, &&VM_MAKELABEL(OPC_ECHO)),
            NEON_SETDISPATCHIDX(OPC_POPONE, &&VM_MAKELABEL(OPC_POPONE)),
            NEON_SETDISPATCHIDX(OPC_DUPONE, &&VM_MAKELABEL(OPC_DUPONE)),
            NEON_SETDISPATCHIDX(OPC_POPN, &&VM_MAKELABEL(OPC_POPN)),
            NEON_SETDISPATCHIDX(OPC_ASSERT, &&VM_MAKELABEL(OPC_ASSERT)),
            NEON_SETDISPATCHIDX(OPC_EXTHROW, &&VM_MAKELABEL(OPC_EXTHROW)),
            NEON_SETDISPATCHIDX(OPC_MAKECLOSURE, &&VM_MAKELABEL(OPC_MAKECLOSURE)),
            NEON_SETDISPATCHIDX(OPC_CALLFUNCTION, &&VM_MAKELABEL(OPC_CALLFUNCTION)),
            NEON_SETDISPATCHIDX(OPC_CALLMETHOD, &&VM_MAKELABEL(OPC_CALLMETHOD)),
            NEON_SETDISPATCHIDX(OPC_CLASSINVOKETHIS, &&VM_MAKELABEL(OPC_CLASSINVOKETHIS)),
            NEON_SETDISPATCHIDX(OPC_RETURN, &&VM_MAKELABEL(OPC_RETURN)),
            NEON_SETDISPATCHIDX(OPC_MAKECLASS, &&VM_MAKELABEL(OPC_MAKECLASS)),
            NEON_SETDISPATCHIDX(OPC_MAKEMETHOD, &&VM_MAKELABEL(OPC_MAKEMETHOD)),
            NEON_SETDISPATCHIDX(OPC_CLASSGETTHIS, &&VM_MAKELABEL(OPC_CLASSGETTHIS)),
            NEON_SETDISPATCHIDX(OPC_CLASSPROPERTYDEFINE, &&VM_MAKELABEL(OPC_CLASSPROPERTYDEFINE)),
            NEON_SETDISPATCHIDX(OPC_CLASSINHERIT, &&VM_MAKELABEL(OPC_CLASSINHERIT)),
            NEON_SETDISPATCHIDX(OPC_CLASSGETSUPER, &&VM_MAKELABEL(OPC_CLASSGETSUPER)),
            NEON_SETDISPATCHIDX(OPC_CLASSINVOKESUPER, &&VM_MAKELABEL(OPC_CLASSINVOKESUPER)),
            NEON_SETDISPATCHIDX(OPC_CLASSINVOKESUPERSELF, &&VM_MAKELABEL(OPC_CLASSINVOKESUPERSELF)),
            NEON_SETDISPATCHIDX(OPC_MAKERANGE, &&VM_MAKELABEL(OPC_MAKERANGE)),
            NEON_SETDISPATCHIDX(OPC_MAKEARRAY, &&VM_MAKELABEL(OPC_MAKEARRAY)),
            NEON_SETDISPATCHIDX(OPC_MAKEDICT, &&VM_MAKELABEL(OPC_MAKEDICT)),
            NEON_SETDISPATCHIDX(OPC_INDEXGET, &&VM_MAKELABEL(OPC_INDEXGET)),
            NEON_SETDISPATCHIDX(OPC_INDEXGETRANGED, &&VM_MAKELABEL(OPC_INDEXGETRANGED)),
            NEON_SETDISPATCHIDX(OPC_INDEXSET, &&VM_MAKELABEL(OPC_INDEXSET)),
            NEON_SETDISPATCHIDX(OPC_EXTRY, &&VM_MAKELABEL(OPC_EXTRY)),
            NEON_SETDISPATCHIDX(OPC_EXPOPTRY, &&VM_MAKELABEL(OPC_EXPOPTRY)),
            NEON_SETDISPATCHIDX(OPC_EXPUBLISHTRY, &&VM_MAKELABEL(OPC_EXPUBLISHTRY)),
            NEON_SETDISPATCHIDX(OPC_STRINGIFY, &&VM_MAKELABEL(OPC_STRINGIFY)),
            NEON_SETDISPATCHIDX(OPC_SWITCH, &&VM_MAKELABEL(OPC_SWITCH)),
            NEON_SETDISPATCHIDX(OPC_TYPEOF, &&VM_MAKELABEL(OPC_TYPEOF)),
            NEON_SETDISPATCHIDX(OPC_OPINSTANCEOF, &&VM_MAKELABEL(OPC_OPINSTANCEOF)),
            NEON_SETDISPATCHIDX(OPC_HALT, &&VM_MAKELABEL(OPC_HALT)),
        };
    #endif
        you_are_calling_exit_vm_outside_of_runvm = false;
        m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
        Debug vmdbg(m_debugwriter);
        while(true)
        {
    #if defined(NEON_CONFIG_USECOMPUTEDGOTO) && (NEON_CONFIG_USECOMPUTEDGOTO == 1)
        readnextinstruction:
    #endif
            /*
            // try...finally... (i.e. try without a catch but finally
            // whose try body raises an exception)
            // can cause us to go into an invalid mode where frame count == 0
            // to fix this, we need to exit with an appropriate mode here.
            */
            if(m_vmstate.framecount == 0)
            {
                return Status::RuntimeFail;
            }
            if(NEON_UNLIKELY(m_conf.shoulddumpstack))
            {
                ofs = (int)(m_vmstate.currentframe->inscode - m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.blob->m_instrucs.data());
                vmdbg.printInstructionAt(m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.blob, ofs);
                fprintf(stderr, "stack (before)=[\n");
                iterpos = 0;
                dbgslot = m_vmstate.stackvalues.data();
                while(dbgslot < m_vmstate.stackvalues.getp(m_vmstate.stackidx))
                {
                    printpos = iterpos + 1;
                    iterpos++;
                    fprintf(stderr, "  [%s%d%s] ", Util::termColor(NEON_COLOR_YELLOW), printpos, Util::termColor(NEON_COLOR_RESET));
                    m_debugwriter->format("%s", Util::termColor(NEON_COLOR_YELLOW));
                    ValPrinter::printValue(m_debugwriter, *dbgslot, true, false);
                    m_debugwriter->format("%s", Util::termColor(NEON_COLOR_RESET));
                    fprintf(stderr, "\n");
                    dbgslot++;
                }
                fprintf(stderr, "]\n");
            }
    #if defined(NEON_CONFIG_USECOMPUTEDGOTO) && (NEON_CONFIG_USECOMPUTEDGOTO == 1)
        #if 0
                    trynext:
        #endif
    #endif
            currinstr = vmReadInstruction();
            m_vmstate.currentinstr = currinstr;
    #if defined(NEON_CONFIG_USECOMPUTEDGOTO) && (NEON_CONFIG_USECOMPUTEDGOTO == 1)
            auto address = dispatchtable[currinstr.code];
            fprintf(stderr, "gotoaddress=%p (for %s (%d))\n", address, Debug::opcodeToString(currinstr.code), currinstr.code);
            goto* address;
    #else
            switch(currinstr.code)
    #endif
            {
                VM_CASE(OPC_RETURN)
                {
                    size_t ssp;
                    Value result;
                    result = vmStackPop();
                    if(rv != nullptr)
                    {
                        *rv = result;
                    }
                    ssp = m_vmstate.currentframe->stackslotpos;
                    vmUtilUpvaluesClose(m_vmstate.stackvalues.getp(ssp));
                    m_vmstate.framecount--;
                    if(m_vmstate.framecount == 0)
                    {
                        vmStackPop();
                        return Status::Ok;
                    }
                    ssp = m_vmstate.currentframe->stackslotpos;
                    m_vmstate.stackidx = ssp;
                    vmStackPush(result);
                    m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                    if(m_vmstate.framecount == (int64_t)exitframe)
                    {
                        return Status::Ok;
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_HALT)
                {
                    printf("**halting vm**\n");
                }
                goto finished;
                VM_CASE(OPC_PUSHCONSTANT)
                {
                    Value constant;
                    constant = vmReadConst();
                    vmStackPush(constant);
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMADD)
                {
                    Value valright;
                    Value valleft;
                    Value result;
                    valright = vmStackPeek(0);
                    valleft = vmStackPeek(1);
                    if(valright.isString() || valleft.isString())
                    {
                        if(NEON_UNLIKELY(!vmUtilConcatenate()))
                        {
                            VMMAC_TRYRAISE(Status::RuntimeFail, "unsupported operand + for %s and %s", Value::typeName(valleft, false), Value::typeName(valright, false));
                            VMMAC_DISPATCH();
                        }
                    }
                    else if(valleft.isArray() && valright.isArray())
                    {
                        result = Value::fromObject(vmUtilCombineArrays(valleft.asArray(), valright.asArray()));
                        vmStackPop(2);
                        vmStackPush(result);
                    }
                    else
                    {
                        vmDoBinaryDirect();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMSUBTRACT)
                {
                    vmDoBinaryDirect();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMMULTIPLY)
                {
                    int intnum;
                    double dbnum;
                    Value peekleft;
                    Value peekright;
                    Value result;
                    String* string;
                    Array* list;
                    Array* newlist;
                    peekright = vmStackPeek(0);
                    peekleft = vmStackPeek(1);
                    if(peekleft.isString() && peekright.isNumber())
                    {
                        dbnum = peekright.asNumber();
                        string = peekleft.asString();
                        result = Value::fromObject(vmUtilMultString(string, dbnum));
                        vmStackPop(2);
                        vmStackPush(result);
                        VMMAC_DISPATCH();
                    }
                    else if(peekleft.isArray() && peekright.isNumber())
                    {
                        intnum = (int)peekright.asNumber();
                        vmStackPop();
                        list = peekleft.asArray();
                        newlist = Array::make();
                        vmStackPush(Value::fromObject(newlist));
                        vmUtilMultArray(list, newlist, intnum);
                        vmStackPop(2);
                        vmStackPush(Value::fromObject(newlist));
                        VMMAC_DISPATCH();
                    }
                    else
                    {
                        vmDoBinaryDirect();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMDIVIDE)
                {
                    vmDoBinaryDirect();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMMODULO)
                {
                    if(vmDoBinaryFunc("%", (BinOpFuncFN)vmCallbackModulo))
                    {
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMPOW)
                {
                    if(vmDoBinaryFunc("**", (BinOpFuncFN)vmCallbackPow))
                    {
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMNEGATE)
                {
                    Value peeked;
                    peeked = vmStackPeek(0);
                    if(!peeked.isNumber())
                    {
                        VMMAC_TRYRAISE(Status::RuntimeFail, "operator - not defined for object of type %s", Value::typeName(peeked, false));
                        VMMAC_DISPATCH();
                    }
                    peeked = vmStackPop();
                    vmStackPush(Value::makeNumber(-peeked.asNumber()));
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMBITNOT)
                {
                    Value peeked;
                    peeked = vmStackPeek(0);
                    if(!peeked.isNumber())
                    {
                        VMMAC_TRYRAISE(Status::RuntimeFail, "operator ~ not defined for object of type %s", Value::typeName(peeked, false));
                        VMMAC_DISPATCH();
                    }
                    peeked = vmStackPop();
                    vmStackPush(Value::makeNumber(~((int)peeked.asNumber())));
                    VMMAC_DISPATCH();
                }
                VM_CASE(OPC_PRIMAND)
                {
                    vmDoBinaryDirect();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMOR)
                {
                    vmDoBinaryDirect();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMBITXOR)
                {
                    vmDoBinaryDirect();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMSHIFTLEFT)
                {
                    vmDoBinaryDirect();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMSHIFTRIGHT)
                {
                    vmDoBinaryDirect();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PUSHONE)
                {
                    vmStackPush(Value::makeNumber(1));
                }
                VMMAC_DISPATCH();
                /* comparisons */
                VM_CASE(OPC_EQUAL)
                {
                    Value a;
                    Value b;
                    b = vmStackPop();
                    a = vmStackPop();
                    vmStackPush(Value::makeBool(Value::compareValues(a, b)));
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMGREATER)
                {
                    vmDoBinaryDirect();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMLESSTHAN)
                {
                    vmDoBinaryDirect();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PRIMNOT)
                {
                    Value val;
                    val = vmStackPop();
                    vmStackPush(Value::makeBool(val.isFalse()));
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PUSHNULL)
                {
                    vmStackPush(Value::makeNull());
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PUSHEMPTY)
                {
                    vmStackPush(Value::makeNull());
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PUSHTRUE)
                {
                    vmStackPush(Value::makeBool(true));
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PUSHFALSE)
                {
                    vmStackPush(Value::makeBool(false));
                }
                VMMAC_DISPATCH();

                VM_CASE(OPC_JUMPNOW)
                {
                    uint16_t offset;
                    offset = vmReadShort();
                    m_vmstate.currentframe->inscode += offset;
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_JUMPIFFALSE)
                {
                    uint16_t offset;
                    Value val;
                    offset = vmReadShort();
                    val = vmStackPeek(0);
                    if(val.isFalse())
                    {
                        m_vmstate.currentframe->inscode += offset;
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_LOOP)
                {
                    uint16_t offset;
                    offset = vmReadShort();
                    m_vmstate.currentframe->inscode -= offset;
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_ECHO)
                {
                    Value val;
                    val = vmStackPeek(0);
                    ValPrinter::printValue(m_stdoutprinter, val, m_isrepl, true);
                    if(!val.isNull())
                    {
                        m_stdoutprinter->writeString("\n");
                    }
                    vmStackPop();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_STRINGIFY)
                {
                    Value peeked;
                    Value popped;
                    String* os;
                    peeked = vmStackPeek(0);
                    if(!peeked.isString() && !peeked.isNull())
                    {
                        popped = vmStackPop();
                        os = Value::toString(popped);
                        if(os->length() != 0)
                        {
                            vmStackPush(Value::fromObject(os));
                        }
                        else
                        {
                            vmStackPush(Value::makeNull());
                        }
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_DUPONE)
                {
                    Value val;
                    val = vmStackPeek(0);
                    vmStackPush(val);
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_POPONE)
                {
                    vmStackPop();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_POPN)
                {
                    vmStackPop(vmReadShort());
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_UPVALUECLOSE)
                {
                    vmUtilUpvaluesClose(m_vmstate.stackvalues.getp(m_vmstate.stackidx - 1));
                    vmStackPop();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_OPINSTANCEOF)
                {
                    bool rt;
                    Value first;
                    Value second;
                    Class* vclass;
                    Class* checkclass;
                    rt = false;
                    first = vmStackPop();
                    second = vmStackPop();
                    #if 0
                        vmDebugPrintVal(first, "first value");
                        vmDebugPrintVal(second, "second value");
                    #endif
                    if(!first.isClass())
                    {
                        VMMAC_TRYRAISE(Status::RuntimeFail, "invalid use of 'is' on non-class");
                    }
                    checkclass = first.asClass();
                    vclass = getClassFor(second);
                    if(vclass)
                    {
                        rt = Class::isInstanceOf(vclass, checkclass);
                    }
                    vmStackPush(Value::makeBool(rt));
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_GLOBALDEFINE)
                {
                    if(!vmDoGlobalDefine())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_GLOBALGET)
                {
                    if(!vmDoGlobalGet())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_GLOBALSET)
                {
                    if(!vmDoGlobalSet())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_LOCALGET)
                {
                    if(!vmDoLocalGet())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_LOCALSET)
                {
                    if(!vmDoLocalSet())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_FUNCARGGET)
                {
                    if(!vmDoFuncArgGet())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_FUNCARGOPTIONAL)
                {
                    if(!vmDoFuncArgOptional())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_FUNCARGSET)
                {
                    if(!vmDoFuncArgSet())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();

                VM_CASE(OPC_PROPERTYGET)
                {
                    if(!vmDoPropertyGetNormal())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PROPERTYSET)
                {
                    if(!vmDoPropertySet())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_PROPERTYGETSELF)
                {
                    if(!vmDoPropertyGetSelf())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_MAKECLOSURE)
                {
                    if(!vmDoMakeClosure())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_UPVALUEGET)
                {
                    int upvidx;
                    Function* closure;
                    upvidx = vmReadShort();
                    closure = m_vmstate.currentframe->closure;
                    if(upvidx < closure->m_upvalcount)
                    {
                        vmStackPush((closure->m_fnvals.fnclosure.m_upvalues[upvidx]->m_location));
                    }
                    else
                    {
                        vmStackPush(closure->m_clsthisval);
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_UPVALUESET)
                {
                    int upvidx;
                    Value val;
                    upvidx = vmReadShort();
                    val = vmStackPeek(0);
                    m_vmstate.currentframe->closure->m_fnvals.fnclosure.m_upvalues[upvidx]->m_location = val;
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_CALLFUNCTION)
                {
                    size_t argcount;
                    Value callee;
                    Value thisval;
                    thisval = Value::makeNull();
                    argcount = vmReadByte();
                    callee = vmStackPeek(argcount);
                    if(callee.isFuncclosure())
                    {
                        thisval = (callee.asFunction()->m_clsthisval);
                    }
                    if(!vmCallValue(callee, thisval, argcount, false))
                    {
                        VMMAC_EXITVM();
                    }
                    m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_CALLMETHOD)
                {
                    size_t argcount;
                    String* method;
                    method = vmReadString();
                    argcount = vmReadByte();
                    if(!vmUtilInvokeMethodNormal(method, argcount))
                    {
                        VMMAC_EXITVM();
                    }
                    m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_CLASSGETTHIS)
                {
                    Value thisval;
                    thisval = vmStackPeek(3);
                    m_debugwriter->format("CLASSGETTHIS: thisval=");
                    ValPrinter::printValue(m_debugwriter, thisval, true, false);
                    m_debugwriter->format("\n");
                    vmStackPush(thisval);
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_CLASSINVOKETHIS)
                {
                    size_t argcount;
                    String* method;
                    method = vmReadString();
                    argcount = vmReadByte();
                    if(!vmUtilInvokeMethodSelf(method, argcount))
                    {
                        VMMAC_EXITVM();
                    }
                    m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_MAKECLASS)
                {
                    bool haveval;
                    Value pushme;
                    String* name;
                    Class* klass;
                    Property* field;
                    haveval = false;
                    name = vmReadString();
                    field = m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.module->m_deftable.getfieldbyostr(name);
                    if(field != nullptr)
                    {
                        if(field->value.isClass())
                        {
                            haveval = true;
                            pushme = field->value;
                        }
                    }
                    field = m_declaredglobals.getfieldbyostr(name);
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
                        klass = Class::make(name, m_classprimobject);
                        pushme = Value::fromObject(klass);
                    }
                    vmStackPush(pushme);
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_MAKEMETHOD)
                {
                    String* name;
                    name = vmReadString();
                    vmUtilDefineMethod(name);
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_CLASSPROPERTYDEFINE)
                {
                    int isstatic;
                    String* name;
                    name = vmReadString();
                    isstatic = vmReadByte();
                    vmUtilDefineProperty(name, isstatic == 1);
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_CLASSINHERIT)
                {
                    Value vclass;
                    Value vsuper;
                    Class* superclass;
                    Class* subclass;
                    vsuper = vmStackPeek(1);
                    if(!vsuper.isClass())
                    {
                        VMMAC_TRYRAISE(Status::RuntimeFail, "cannot inherit from non-class object");
                        VMMAC_DISPATCH();
                    }
                    vclass = vmStackPeek(0);
                    superclass = vsuper.asClass();
                    subclass = vclass.asClass();
                    subclass->inheritFrom(superclass);
                    /* pop the subclass */
                    vmStackPop();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_CLASSGETSUPER)
                {
                    Value vclass;
                    Class* klass;
                    String* name;
                    name = vmReadString();
                    vclass = vmStackPeek(0);
                    klass = vclass.asClass();
                    if(!vmUtilBindMethod(klass->m_superclass, name))
                    {
                        VMMAC_TRYRAISE(Status::RuntimeFail, "class '%s' does not have a function '%s'", klass->m_classname->data(), name->data());
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_CLASSINVOKESUPER)
                {
                    size_t argcount;
                    Value vclass;
                    Class* klass;
                    String* method;
                    method = vmReadString();
                    argcount = vmReadByte();
                    vclass = vmStackPop();
                    klass = vclass.asClass();
                    if(!vmUtilInvokeMethodFromClass(klass, method, argcount))
                    {
                        VMMAC_EXITVM();
                    }
                    m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_CLASSINVOKESUPERSELF)
                {
                    size_t argcount;
                    Value vclass;
                    Class* klass;
                    argcount = vmReadByte();
                    vclass = vmStackPop();
                    klass = vclass.asClass();
                    if(!vmUtilInvokeMethodFromClass(klass, m_defaultstrings.nmconstructor, argcount))
                    {
                        VMMAC_EXITVM();
                    }
                    m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_MAKEARRAY)
                {
                    if(!vmDoMakeArray())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();

                VM_CASE(OPC_MAKERANGE)
                {
                    double lower;
                    double upper;
                    Value vupper;
                    Value vlower;
                    vupper = vmStackPeek(0);
                    vlower = vmStackPeek(1);
                    if(!vupper.isNumber() || !vlower.isNumber())
                    {
                        VMMAC_TRYRAISE(Status::RuntimeFail, "invalid range boundaries");
                        VMMAC_DISPATCH();
                    }
                    lower = vlower.asNumber();
                    upper = vupper.asNumber();
                    vmStackPop(2);
                    vmStackPush(Value::fromObject(Range::make(lower, upper)));
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_MAKEDICT)
                {
                    if(!vmDoMakeDict())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_INDEXGETRANGED)
                {
                    if(!vmDoGetRangedIndex())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_INDEXGET)
                {
                    if(!vmDoIndexGet())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_INDEXSET)
                {
                    if(!vmDoIndexSet())
                    {
                        VMMAC_EXITVM();
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_TYPEOF)
                {
                    Value res;
                    Value thing;
                    const char* result;
                    thing = vmStackPop();
                    result = Value::typeName(thing, false);
                    res = Value::fromObject(String::copy(result));
                    vmStackPush(res);
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_ASSERT)
                {
                    Value message;
                    Value expression;
                    message = vmStackPop();
                    expression = vmStackPop();
                    if(expression.isFalse())
                    {
                        if(!message.isNull())
                        {
                            NEON_THROWCLASSWITHSOURCEINFO(m_exceptions.asserterror, Value::toString(message)->data());
                        }
                        else
                        {
                            NEON_THROWCLASSWITHSOURCEINFO(m_exceptions.asserterror, "assertion failed");
                        }
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_EXTHROW)
                {
                    bool isok;
                    Value peeked;
                    Value stacktrace;
                    Instance* instance;
                    peeked = vmStackPeek(0);
                    isok = (peeked.isInstance() || Class::isInstanceOf(peeked.asInstance()->m_instanceclass, m_exceptions.stdexception));
                    if(!isok)
                    {
                        VMMAC_TRYRAISE(Status::RuntimeFail, "instance of Exception expected");
                        VMMAC_DISPATCH();
                    }
                    stacktrace = vmExceptionGetStackTrace();
                    instance = peeked.asInstance();
                    instance->defProperty(String::intern("stacktrace"), stacktrace);
                    if(vmExceptionPropagate())
                    {
                        m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                        VMMAC_DISPATCH();
                    }
                    VMMAC_EXITVM();
                }
                VM_CASE(OPC_EXTRY)
                {
                    bool haveclass;
                    uint16_t addr;
                    uint16_t finaddr;
                    Value value;
                    String* type;
                    Class* exclass;
                    haveclass = false;
                    exclass = nullptr;
                    type = vmReadString();
                    addr = vmReadShort();
                    finaddr = vmReadShort();
                    if(addr != 0)
                    {
                        value = Value::makeNull();
                        if(!m_declaredglobals.get(Value::fromObject(type), &value))
                        {
                            if(value.isClass())
                            {
                                haveclass = true;
                                exclass = value.asClass();
                            }
                        }
                        if(!haveclass)
                        {
                            /*
                            if(!m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.module->m_deftable.get(Value::fromObject(type), &value) || !value.isClass())
                            {
                                VMMAC_TRYRAISE(Status::RuntimeFail, "object of type '%s' is not an exception", type->data());
                                VMMAC_DISPATCH();
                            }
                            */
                            exclass = m_exceptions.stdexception;
                        }
                        vmExceptionPushHandler(exclass, addr, finaddr);
                    }
                    else
                    {
                        vmExceptionPushHandler(nullptr, addr, finaddr);
                    }
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_EXPOPTRY)
                {
                    m_vmstate.currentframe->m_handlercount--;
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_EXPUBLISHTRY)
                {
                    m_vmstate.currentframe->m_handlercount--;
                    if(vmExceptionPropagate())
                    {
                        m_vmstate.currentframe = &m_vmstate.framevalues[m_vmstate.framecount - 1];
                        VMMAC_DISPATCH();
                    }
                    VMMAC_EXITVM();
                }
                VMMAC_DISPATCH();
                VM_CASE(OPC_SWITCH)
                {
                    Value expr;
                    Value value;
                    Switch* sw;
                    sw = vmReadConst().asSwitch();
                    expr = vmStackPeek(0);
                    if(sw->m_table.get(expr, &value))
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
                    vmStackPop();
                }
                VMMAC_DISPATCH();
    #if 0
                default:
                    {
                        fprintf(stderr, "UNHANDLED OPCODE %d\n", currinstr.code);
                    }
                    break;
    #endif
            }
        }
    finished:
        return Status::Ok;
    }

    void buildProcessInfo()
    {
        enum
        {
            kMaxBuf = 1024
        };
        char* pathp;
        char pathbuf[kMaxBuf];
        auto gcs = SharedState::get();
        gcs->m_processinfo = Memory::make<ProcessInfo>();
        gcs->m_processinfo->cliscriptfile = nullptr;
        gcs->m_processinfo->cliscriptdirectory = nullptr;
        gcs->m_processinfo->cliargv = Array::make();
        {
            pathp = Util::osfn_getcwd(pathbuf, kMaxBuf);
            if(pathp == nullptr)
            {
                pathp = (char*)".";
            }
            gcs->m_processinfo->cliexedirectory = String::copy(pathp);
        }
        {
            gcs->m_processinfo->cliprocessid = Util::osfn_getpid();
        }
        {
            {
                gcs->m_processinfo->filestdout = File::make(stdout, true, "<stdout>", "wb");
                defineGlobalValue("STDOUT", Value::fromObject(gcs->m_processinfo->filestdout));
            }
            {
                gcs->m_processinfo->filestderr = File::make(stderr, true, "<stderr>", "wb");
                defineGlobalValue("STDERR", Value::fromObject(gcs->m_processinfo->filestderr));
            }
            {
                gcs->m_processinfo->filestdin = File::make(stdin, true, "<stdin>", "rb");
                defineGlobalValue("STDIN", Value::fromObject(gcs->m_processinfo->filestdin));
            }
        }
    }

    void updateProcessInfo()
    {
        char* prealpath;
        char* prealdir;
        auto gcs = SharedState::get();
        if(gcs->m_rootphysfile != nullptr)
        {
            prealpath = Util::osfn_realpath(gcs->m_rootphysfile, nullptr);
            prealdir = Util::osfn_dirname(prealpath);
            gcs->m_processinfo->cliscriptfile = String::copy(prealpath);
            gcs->m_processinfo->cliscriptdirectory = String::copy(prealdir);
            Memory::sysFree(prealpath);
            Memory::sysFree(prealdir);
        }
        if(gcs->m_processinfo->cliscriptdirectory != nullptr)
        {
            Module::addSearchPathObj(gcs->m_processinfo->cliscriptdirectory);
        }
    }

    bool initState()
    {
        Memory::mempoolInit();
        if(!SharedState::init())
        {
            return false;
        }
        auto gcs = SharedState::get();
        gcs->m_memuserptr = nullptr;
        gcs->m_exceptions.stdexception = nullptr;
        gcs->m_rootphysfile = nullptr;
        gcs->m_processinfo = nullptr;
        gcs->m_isrepl = false;
        gcs->initVMState();
        gcs->resetVMState();
        /*
         * setup default config
         */
        {
            gcs->m_conf.enablestrictmode = false;
            gcs->m_conf.shoulddumpstack = false;
            gcs->m_conf.enablewarnings = false;
            gcs->m_conf.dumpbytecode = false;
            gcs->m_conf.exitafterbytecode = false;
            gcs->m_conf.showfullstack = false;
            gcs->m_conf.enableapidebug = false;
            gcs->m_conf.maxsyntaxerrors = SharedState::CONF_MAXSYNTAXERRORS;
        }
        /*
         * initialize GC state
         */
        {
            gcs->m_lastreplvalue = Value::makeNull();
        }
        /*
         * initialize various printer instances
         */
        {
            gcs->m_stdoutprinter = IOStream::makeIO(stdout, false);
            gcs->m_stdoutprinter->m_shouldflush = false;
            gcs->m_stderrprinter = IOStream::makeIO(stderr, false);
            gcs->m_debugwriter = IOStream::makeIO(stderr, false);
            gcs->m_debugwriter->m_shortenvalues = true;
            gcs->m_debugwriter->m_maxvallength = 15;
        }
        /*
         * initialize runtime tables
         */
        {
            gcs->m_openedmodules.initTable();
            gcs->m_declaredglobals.initTable();
        }
        /*
         * initialize the toplevel module
         */
        {
            gcs->m_topmodule = Module::make("", "<state>", false, true);
        }
        {
            gcs->m_defaultstrings.nmconstructor = String::intern("constructor");
            gcs->m_defaultstrings.nmindexget = String::copy("__indexget__");
            gcs->m_defaultstrings.nmindexset = String::copy("__indexset__");
            gcs->m_defaultstrings.nmadd = String::copy("__add__");
            gcs->m_defaultstrings.nmsub = String::copy("__sub__");
            gcs->m_defaultstrings.nmdiv = String::copy("__div__");
            gcs->m_defaultstrings.nmmul = String::copy("__mul__");
            gcs->m_defaultstrings.nmband = String::copy("__band__");
            gcs->m_defaultstrings.nmbor = String::copy("__bor__");
            gcs->m_defaultstrings.nmbxor = String::copy("__bxor__");
        }
        /*
         * declare default classes
         */
        {
            gcs->m_classprimclass = Class::makeScriptClass("Class", nullptr);
            gcs->m_classprimobject = Class::makeScriptClass("Object", gcs->m_classprimclass);
            gcs->m_classprimnumber = Class::makeScriptClass("Number", gcs->m_classprimobject);
            gcs->m_classprimstring = Class::makeScriptClass("String", gcs->m_classprimobject);
            gcs->m_classprimarray = Class::makeScriptClass("Array", gcs->m_classprimobject);
            gcs->m_classprimdict = Class::makeScriptClass("Dict", gcs->m_classprimobject);
            gcs->m_classprimfile = Class::makeScriptClass("File", gcs->m_classprimobject);
            gcs->m_classprimdirectory = Class::makeScriptClass("Dir", gcs->m_classprimobject);
            gcs->m_classprimrange = Class::makeScriptClass("Range", gcs->m_classprimobject);
            gcs->m_classprimcallable = Class::makeScriptClass("Function", gcs->m_classprimobject);
            gcs->m_classprimprocess = Class::makeScriptClass("Process", gcs->m_classprimobject);
        }
        /*
         * declare environment variables dictionary
         */
        {
            gcs->m_envdict = Dict::make();
        }
        /*
         * declare default exception types
         */
        {
            if(gcs->m_exceptions.stdexception == nullptr)
            {
                gcs->m_exceptions.stdexception = Class::makeExceptionClass(gcs->m_classprimobject, nullptr, "Exception", true);
            }
            gcs->m_exceptions.asserterror = Class::makeExceptionClass(gcs->m_classprimobject, nullptr, "AssertError", true);
            gcs->m_exceptions.syntaxerror = Class::makeExceptionClass(gcs->m_classprimobject, nullptr, "SyntaxError", true);
            gcs->m_exceptions.ioerror = Class::makeExceptionClass(gcs->m_classprimobject, nullptr, "IOError", true);
            gcs->m_exceptions.oserror = Class::makeExceptionClass(gcs->m_classprimobject, nullptr, "OSError", true);
            gcs->m_exceptions.argumenterror = Class::makeExceptionClass(gcs->m_classprimobject, nullptr, "ArgumentError", true);
            gcs->m_exceptions.regexerror = Class::makeExceptionClass(gcs->m_classprimobject, nullptr, "RegexError", true);
            gcs->m_exceptions.importerror = Class::makeExceptionClass(gcs->m_classprimobject, nullptr, "ImportError", true);
        }
        /* all the other bits .... */
        buildProcessInfo();
        /* NOW the module paths can be set up */
        setupModulePaths();
        {
            initBuiltinFunctions();
            initBuiltinObjects();
        }
        return true;
    }

    #if 0
        #define destrdebug(...)                           \
            {                                             \
                fprintf(stderr, "in destroyState: "); \
                fprintf(stderr, __VA_ARGS__);             \
                fprintf(stderr, "\n");                    \
            }
    #else
        #define destrdebug(...)
    #endif
    
    void destroyState()
    {
        auto gcs = SharedState::get();
        destrdebug("destroying m_importpath...");
        gcs->m_importpath.deInit();
        destrdebug("destroying linked objects...");
        gcs->gcLinkedObjectsDestroy();
        /* since object in module can exist in m_declaredglobals, it must come before */
        destrdebug("destroying module table...");
        gcs->m_openedmodules.deInit();
        destrdebug("destroying globals table...");
        gcs->m_declaredglobals.deInit();
        destrdebug("destroying strings table...");
        gcs->m_allocatedstrings.deInit();
        destrdebug("destroying m_stdoutprinter...");
        IOStream::destroy(gcs->m_stdoutprinter);
        destrdebug("destroying m_stderrprinter...");
        IOStream::destroy(gcs->m_stderrprinter);
        destrdebug("destroying m_debugwriter...");
        IOStream::destroy(gcs->m_debugwriter);
        destrdebug("destroying framevalues...");
        gcs->m_vmstate.framevalues.deInit();
        destrdebug("destroying stackvalues...");
        gcs->m_vmstate.stackvalues.deInit();
        Memory::sysFree(gcs->m_processinfo);
        destrdebug("destroying state...");
        SharedState::destroy();
        destrdebug("done destroying!");
        Memory::mempoolDestroy();
    }

    Function* SharedState::compileSourceToFunction(Module* module, bool fromeval, const char* source, bool toplevel)
    {
        Blob blob;
        Function* function;
        Function* closure;
        (void)toplevel;
        function = compileSourceIntern(module, source, &blob, fromeval);
        if(function == nullptr)
        {
            Blob::destroy(&blob);
            return nullptr;
        }
        if(!fromeval)
        {
            vmStackPush(Value::fromObject(function));
        }
        else
        {
            function->m_funcname = String::intern("(evaledcode)");
        }
        closure = Function::makeFuncClosure(function, Value::makeNull());
        if(!fromeval)
        {
            vmStackPop();
            vmStackPush(Value::fromObject(closure));
        }
        Blob::destroy(&blob);
        return closure;
    }

    Status SharedState::execSource(Module* module, const char* source, const char* filename, Value* dest)
    {
        char* rp;
        Status status;
        Function* closure;
        m_rootphysfile = filename;
        updateProcessInfo();
        rp = (char*)filename;
        m_topmodule->m_physicalpath = String::copy(rp);
        module->setInternFileField();
        closure = compileSourceToFunction(module, false, source, true);
        if(closure == nullptr)
        {
            return Status::CompileFailed;
        }
        if(m_conf.exitafterbytecode)
        {
            return Status::Ok;
        }
        /*
         * NB. it is a closure, since it's compiled code.
         * so no need to create a Value and call vmCallValue().
         */
        if(!vmDoCallClosure(closure, Value::makeNull(), 0, false))
        {
            return Status::RuntimeFail;
        }
        status = runVM(0, dest);
        fprintf(stderr, "m_vmstate.m_unhandledexceptionstate=%d\n", m_vmstate.m_unhandledexceptionstate);
        if(m_vmstate.m_unhandledexceptionstate)
        {
            return Status::RuntimeFail;
        }
        return status;
    }

    Value SharedState::evalSource(const char* source)
    {
        bool ok;
        size_t argc;
        Value callme;
        Value retval;
        Function* closure;
        (void)argc;
        closure = compileSourceToFunction(m_topmodule, true, source, false);
        callme = Value::fromObject(closure);
        argc = vmNestCallPrepare(callme, Value::makeNull(), nullptr, 0);
        ok = vmNestCallFunction(callme, Value::makeNull(), nullptr, 0, &retval, false);
        if(!ok)
        {
            NEON_THROWEXCEPTION("eval() failed");
        }
        return retval;
    }
}
// endnamespace

struct ConsoleProg
{
    class InteractiveRepl
    {
        using CallbackFN = bool(*)();

        public:
            neon::HashTable<const char*, CallbackFN> m_callbacks;
            LineReader m_replctx;

        public:
            InteractiveRepl()
            {
            }

            char* replGetInput(const char* prompt)
            {
                return m_replctx.readLine(prompt);
            }

            void replHistoryAdd(const char* line)
            {
                m_replctx.historyAdd(line);
            }

            void replFreeLine(char* line)
            {
                m_replctx.freeLine(line);
            }

            bool runRepl()
            {
                enum
                {
                    kMaxVarName = 512
                };
                size_t i;
                size_t rescnt;
                int linelength;
                int bracecount;
                int parencount;
                int bracketcount;
                int doublequotecount;
                int singlequotecount;
                bool continuerepl;
                char* line;
                char varnamebuf[kMaxVarName];
                neon::StrBuffer* source;
                const char* cursor;
                neon::Value dest;
                neon::IOStream* pr;
                auto gcs = neon::SharedState::get();
                pr = gcs->m_stdoutprinter;
                rescnt = 0;
                gcs->m_isrepl = true;
                continuerepl = true;
                printf("Type \".exit\" to quit or \".credits\" for more information\n");
                source = neon::Memory::make<neon::StrBuffer>();
                bracecount = 0;
                parencount = 0;
                bracketcount = 0;
                singlequotecount = 0;
                doublequotecount = 0;
                m_replctx.setMultiline(0);
                m_replctx.historyAdd(".exit");
                while(true)
                {
                    if(!continuerepl)
                    {
                        bracecount = 0;
                        parencount = 0;
                        bracketcount = 0;
                        singlequotecount = 0;
                        doublequotecount = 0;
                        source->reset();
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
                    line = replGetInput(cursor);
                    if(line == nullptr || strcmp(line, ".exit") == 0)
                    {
                        neon::Memory::destroy(source);
                        return true;
                    }
                    linelength = (int)strlen(line);
                    if(strcmp(line, ".credits") == 0)
                    {
                        printf("\n" NEON_INFO_COPYRIGHT "\n\n");
                        source->reset();
                        continue;
                    }
                    replHistoryAdd(line);
                    if(linelength > 0 && line[0] == '#')
                    {
                        continue;
                    }
                    /* find count of { and }, ( and ), [ and ], " and ' */
                    for(i = 0; i < (size_t)linelength; i++)
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
                    source->append(line);
                    if(linelength > 0)
                    {
                        source->append("\n");
                    }
                    replFreeLine(line);
                    if(bracketcount == 0 && parencount == 0 && bracecount == 0 && singlequotecount == 0 && doublequotecount == 0)
                    {
                        memset(varnamebuf, 0, kMaxVarName);
                        sprintf(varnamebuf, "_%ld", (long)rescnt);
                        gcs->execSource(gcs->m_topmodule, source->data(), "<repl>", &dest);
                        dest = gcs->m_lastreplvalue;
                        if(!dest.isNull())
                        {
                            pr->format("%s = ", varnamebuf);
                            neon::ValPrinter::printValue(pr, dest, true, true);
                            defineGlobalValue(varnamebuf, dest);
                            pr->format("\n");
                            rescnt++;
                        }
                        gcs->m_lastreplvalue = neon::Value::makeNull();
                        fflush(stdout);
                        continuerepl = false;
                    }
                }
                return true;
            }
    };

    static bool runFile(const char* file)
    {
        size_t fsz;
        char* source;
        const char* oldfile;
        neon::Status result;
        auto gcs = neon::SharedState::get();
        source = neon::File::readFile(file, &fsz, false, 0);
        if(source == nullptr)
        {
            oldfile = file;
            source = neon::File::readFile(file, &fsz, false, 0);
            if(source == nullptr)
            {
                fprintf(stderr, "failed to read from '%s': %s\n", oldfile, strerror(errno));
                return false;
            }
        }
        result = gcs->execSource(gcs->m_topmodule, source, file, nullptr);
        neon::Memory::sysFree(source);
        fflush(stdout);
        return (result == neon::Status::Ok);
    }

    static bool runCode(char* source)
    {
        auto gcs = neon::SharedState::get();
        gcs->m_rootphysfile = nullptr;
        auto result = gcs->execSource(gcs->m_topmodule, source, "<-e>", nullptr);
        fflush(stdout);
        return (result == neon::Status::Ok);
    }

    static int utilFindFirstPos(const char* str, size_t len, int ch)
    {
        size_t i;
        for(i = 0; i < len; i++)
        {
            if(str[i] == ch)
            {
                return i;
            }
        }
        return -1;
    }

    static void parseEnv(char** envp)
    {
        enum
        {
            kMaxKeyLen = 40
        };
        size_t i;
        int len;
        int pos;
        char* raw;
        char* valbuf;
        char keybuf[kMaxKeyLen];
        auto gcs = neon::SharedState::get();
        if(envp == nullptr)
        {
            return;
        }
        for(i = 0; envp[i] != nullptr; i++)
        {
            raw = envp[i];
            len = strlen(raw);
            pos = utilFindFirstPos(raw, len, '=');
            if(pos == -1)
            {
                fprintf(stderr, "malformed environment string '%s'\n", raw);
            }
            else
            {
                memset(keybuf, 0, kMaxKeyLen);
                memcpy(keybuf, raw, pos);
                valbuf = &raw[pos + 1];
                auto oskey = neon::String::copy(keybuf);
                auto osval = neon::String::copy(valbuf);
                gcs->m_envdict->set(neon::Value::fromObject(oskey), neon::Value::fromObject(osval));
            }
        }
    }

    static void fprintMaybeArg(FILE* out, const char* begin, const char* flagname, size_t flaglen, bool needval, bool maybeval, const char* delim)
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

    static void fprintUsageFlags(FILE* out, optlongflags_t* flags)
    {
        size_t i;
        char ch;
        bool needval;
        bool maybeval;
        bool hadshort;
        optlongflags_t* flag;
        for(i = 0; flags[i].longname != nullptr; i++)
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
                fprintMaybeArg(out, "-", &ch, 1, needval, maybeval, nullptr);
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
                fprintMaybeArg(out, "--", flag->longname, strlen(flag->longname), needval, maybeval, "=");
            }
            if(flag->helptext != nullptr)
            {
                fprintf(out, "  -  %s", flag->helptext);
            }
            fprintf(out, "\n");
        }
    }

    static void fprintUsageText(char* argv[], optlongflags_t* flags, bool fail)
    {
        FILE* out;
        out = fail ? stderr : stdout;
        fprintf(out, "Usage: %s [<options>] [<filename> | -e <code>]\n", argv[0]);
        fprintUsageFlags(out, flags);
    }

    static int actualMain(int argc, char* argv[], char** envp)
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
        char* evalmesrc;
        char* nargv[128];
        optcontext_t options;
        static optlongflags_t longopts[] = {
            { "help", 'h', OPTPARSE_NONE, "this help" },
            { "strict", 's', OPTPARSE_NONE, "enable strict mode, such as requiring explicit var declarations" },
            { "warn", 'w', OPTPARSE_NONE, "enable warnings" },
            { "debug", 'd', OPTPARSE_NONE, "enable debugging: print instructions and stack values during execution" },
            { "exitaftercompile", 'x', OPTPARSE_NONE, "when using '-d', quit after printing compiled function(s)" },
            { "eval", 'e', OPTPARSE_REQUIRED, "evaluate a single line of code" },
            { "quit", 'q', OPTPARSE_NONE, "initiate, then immediately destroy the interpreter state" },
            { "types", 't', OPTPARSE_NONE, "print sizeof() of types" },
            { "apidebug", 'a', OPTPARSE_NONE, "print calls to API (very verbose, very slow)" },
            { "gcstart", 'g', OPTPARSE_REQUIRED, "set minimum bytes at which the GC should kick in. 0 disables GC" },
            { 0, 0, (optargtype_t)0, nullptr }
        };
    #if defined(NEON_PLAT_ISWINDOWS) || defined(_MSC_VER)
        _setmode(fileno(stdin), _O_BINARY);
        _setmode(fileno(stdout), _O_BINARY);
        _setmode(fileno(stderr), _O_BINARY);
    #endif
        ok = true;
        wasusage = false;
        quitafterinit = false;
        evalmesrc = nullptr;
        nextgcstart = neon::SharedState::CONF_DEFAULTGCSTART;
        if(!neon::initState())
        {
            fprintf(stderr, "failed to create state\n");
            return 0;
        }
        auto gcs = neon::SharedState::get();
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
            else if(co == 'h')
            {
                fprintUsageText(argv, longopts, false);
                wasusage = true;
            }
            else if(co == 'd' || co == 'j')
            {
                gcs->m_conf.dumpbytecode = true;
                gcs->m_conf.shoulddumpstack = true;
            }
            else if(co == 'x')
            {
                gcs->m_conf.exitafterbytecode = true;
            }
            else if(co == 'a')
            {
                gcs->m_conf.enableapidebug = true;
            }
            else if(co == 's')
            {
                gcs->m_conf.enablestrictmode = true;
            }
            else if(co == 'e')
            {
                evalmesrc = options.optarg;
            }
            else if(co == 'w')
            {
                gcs->m_conf.enablewarnings = true;
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
        parseEnv(envp);
        while(true)
        {
            auto arg = optprs_nextpositional(&options);
            if(arg == nullptr)
            {
                break;
            }
            nargv[nargc] = arg;
            nargc++;
        }
        {
            for(i = 0; i < nargc; i++)
            {
                auto os = neon::String::copy(nargv[i]);
                gcs->m_processinfo->cliargv->push(neon::Value::fromObject(os));
            }
            gcs->m_declaredglobals.set(neon::Value::fromObject(neon::String::copy("ARGV")), neon::Value::fromObject(gcs->m_processinfo->cliargv));
        }
        gcs->m_gcstate.nextgc = nextgcstart;
        if(evalmesrc != nullptr)
        {
            ok = runCode(evalmesrc);
        }
        else if(nargc > 0)
        {
            auto os = gcs->m_processinfo->cliargv->get(0).asString();
            auto filename = os->data();
            ok = runFile(filename);
        }
        else
        {
            InteractiveRepl rp;
            ok = rp.runRepl();
        }
    cleanup:
        neon::destroyState();
        if(ok)
        {
            return 0;
        }
        return 1;
    }
};

int main(int argc, char** argv, char** envp)
{
    return ConsoleProg::actualMain(argc, argv, envp);
}

/**
 * this function is used by clang-repl ONLY. don't call it directly, or bad things will happen!
 */
int replmain(const char* file)
{
    const char* deffile;
    deffile = "mandel1.nn";
    if(file != nullptr)
    {
        deffile = file;
    }
    char* realargv[1024] = { (char*)"a.out", (char*)deffile, nullptr };
    return main(1, realargv, nullptr);
}
