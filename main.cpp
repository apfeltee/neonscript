
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
    #if defined(__wasi__)
        #define NEON_PLAT_ISWASM
    #endif
    #define NEON_PLAT_ISLINUX
#endif

#define NEON_CONF_REPLSUPPORT 1

#if defined(NEON_PLAT_ISWINDOWS)
    #include <windows.h>
    #include <sys/utime.h>
    #include <fcntl.h>
    #include <io.h>
#else
    #include <sys/time.h>
    #include <unistd.h>
#endif


#include "mem.h"
#include "mrx.h"
#include "strbuf.h"
#include "oslib.h"
#include "optparse.h"
#if defined(NEON_CONF_REPLSUPPORT) && (NEON_CONF_REPLSUPPORT == 1)
    #include "lino.h"
#endif


/* needed when compiling with wasi. must be defined *before* signal.h is included! */
#if defined(__wasi__)
    #define _WASI_EMULATED_SIGNAL
#endif

/**
 */
#define NEON_CONFIG_DEBUGMEMORY 0

/**
 * if enabled, most API calls will check for null pointers, and either
 * return immediately or return an appropiate default value if a nullpointer is encountered.
 * this will make the API much less likely to crash out in a segmentation fault,
 * **BUT** the added branching will likely reduce performance.
 */
#define NEON_CONFIG_USENULLPTRCHECKS 0

#if 1
    #if defined(__GNUC__) || defined(__TINYC__)
        #define NEON_INLINE __attribute__((always_inline)) inline
    #else
        #define NEON_INLINE inline
    #endif
#endif

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

#if defined(__GNUC__) || defined(__clang__)
    #define nn_util_likely(x) (__builtin_expect(!!(x), 1))
    #define nn_util_unlikely(x) (__builtin_expect(!!(x), 0))
#else
    #define nn_util_likely(x) (x)
    #define nn_util_unlikely(x) (x)
#endif

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

namespace neon
{
    #define NEON_CONFIG_MTSTATESIZE 624

    #define NEON_CONFIG_FILEEXT ".nn"

    /* global debug mode flag */
    #define NEON_CONFIG_BUILDDEBUGMODE 0
    #define NEON_CONFIG_MAXSYNTAXERRORS 10

    #if NEON_CONFIG_BUILDDEBUGMODE == 1
        #define DEBUG_PRINT_CODE 1
        #define DEBUG_TABLE 0
        #define DEBUG_GC 1
        #define DEBUG_STACK 0
    #endif

    /* how many locals per function can be compiled */
    #define NEON_CONFIG_ASTMAXLOCALS (64 * 2)

    /* how many upvalues per function can be compiled */
    #define NEON_CONFIG_ASTMAXUPVALS (64 * 2)

    /* how many switch cases per switch statement */
    #define NEON_CONFIG_ASTMAXSWITCHCASES (32)

    /* max number of function parameters */
    #define NEON_CONFIG_ASTMAXFUNCPARAMS (32)

    /* how many catch() clauses per try statement */
    #define NEON_CONFIG_MAXEXCEPTHANDLERS (2)

    /*
    // Maximum load factor of 12/14
    // see: https://engineering.fb.com/2019/04/25/developer-tools/f14/
    */
    #define NEON_CONFIG_MAXTABLELOAD (0.85714286)

    /* how much memory can be allocated before the garbage collector kicks in */
    #define NEON_CONFIG_DEFAULTGCSTART (1024 * 1024)

    /* growth factor for GC heap objects */
    #define NEON_CONFIG_GCHEAPGROWTHFACTOR (1.25)

    #define NEON_INFO_COPYRIGHT "based on the Blade Language, Copyright (c) 2021 - 2023 Ore Richard Muyiwa"

    #if 0
        #if defined(__GNUC__)
            #define NEON_ATTR_PRINTFLIKE(fmtarg, firstvararg) __attribute__((__format__(__printf__, fmtarg, firstvararg)))
        #else
            #define NEON_ATTR_PRINTFLIKE(a, b)
        #endif
    #endif
    /*
    // NOTE:
    // 1. Any call to nn_gcmem_protect() within a function/block must be accompanied by
    // at least one call to SharedState::clearGCProtect() before exiting the function/block
    // otherwise, expected unexpected behavior
    // 2. The call to SharedState::clearGCProtect() will be automatic for native functions.
    // 3. $thisval must be retrieved before any call to nn_gcmem_protect in a
    // native function.
    */

    #define nn_gcmem_freearray(typsz, pointer, oldcount) SharedState::gcRelease(pointer, (typsz) * (oldcount))

    #define nn_except_throwclass(exklass, ...) throwScriptException(exklass, __FILE__, __LINE__, __VA_ARGS__)

    #define nn_except_throw(...) nn_except_throwclass(SharedState::get()->m_exceptions.stdexception, __VA_ARGS__)

    #define NEON_RETURNERROR(scfn, ...)               \
        {                                       \
            SharedState::get()->stackPop(scfn.argc); \
            nn_except_throw(__VA_ARGS__);       \
        }                                       \
        return Value::makeBool(false);

    #define NEON_ARGS_FAIL(chp, ...) nn_argcheck_fail((chp), __FILE__, __LINE__, __VA_ARGS__)

    /* check for exact number of arguments $d */
    #define NEON_ARGS_CHECKCOUNT(chp, d)                                                                    \
        if(nn_util_unlikely((chp)->m_scriptfnctx.argc != (d)))                                                            \
        {                                                                                                   \
            return NEON_ARGS_FAIL(chp, "%s() expects %d arguments, %d given", (chp)->m_argcheckfuncname, d, (chp)->m_scriptfnctx.argc); \
        }

    /* check for miminum args $d ($d ... n) */
    #define NEON_ARGS_CHECKMINARG(chp, d)                                                                              \
        if(nn_util_unlikely((chp)->m_scriptfnctx.argc < (d)))                                                                        \
        {                                                                                                              \
            return NEON_ARGS_FAIL(chp, "%s() expects minimum of %d arguments, %d given", (chp)->m_argcheckfuncname, d, (chp)->m_scriptfnctx.argc); \
        }

    /* check for range of args ($low .. $up) */
    #define NEON_ARGS_CHECKCOUNTRANGE(chp, low, up)                                                                              \
        if((int(nn_util_unlikely((chp)->m_scriptfnctx.argc) < int(low)) || (int((chp)->m_scriptfnctx.argc) > int(up))))                                                          \
        {                                                                                                                        \
            return NEON_ARGS_FAIL(chp, "%s() expects between %d and %d arguments, %d given", (chp)->m_argcheckfuncname, low, up, (chp)->m_scriptfnctx.argc); \
        }

    /* check for argument at index $i for $type, where $type is a nn_value_is*() function */
    #if 1
        #define NEON_ARGS_CHECKTYPE(chp, i, typefunc)
    #else
        #define NEON_ARGS_CHECKTYPE(chp, i, typefunc)                                                                                                                                               \
            if(nn_util_unlikely(!typefunc((chp)->m_scriptfnctx.argv[i])))                                                                                                                                         \
            {                                                                                                                                                                                       \
                return NEON_ARGS_FAIL(chp, "%s() expects argument %d as %s, %s given", (chp)->m_argcheckfuncname, (i) + 1, nn_value_typefromfunction(typefunc), nn_value_typename((chp)->m_scriptfnctx.argv[i], false), false); \
            }
    #endif

    #if 0
        #define NEON_APIDEBUG(...)                                 \
            if((nn_util_unlikely((<fixme>)->m_conf.enableapidebug))) \
            {                                                      \
                nn_state_apidebug(__FUNCTION__, __VA_ARGS__);      \
            }
    #else
        #define NEON_APIDEBUG(...)
    #endif

    #define NN_ASTPARSER_GROWCAPACITY(capacity) ((capacity) < 4 ? 4 : (capacity) * 2)




    enum Status
    {
        NEON_STATUS_OK,
        NEON_STATUS_FAILCOMPILE,
        NEON_STATUS_FAILRUNTIME
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

    enum AstCompContext
    {
        NEON_COMPCONTEXT_NONE,
        NEON_COMPCONTEXT_CLASS,
        NEON_COMPCONTEXT_ARRAY,
        NEON_COMPCONTEXT_NESTEDFUNCTION
    };

    class /**/ Value;
    class /**/ Object;
    class /**/ AstParser;
    class /**/ AstToken;
    class /**/ DefExport;
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
    template <typename StoredType> class ValArray;

    template <typename HTKeyT, typename HTValT> class HashTable;

    typedef bool(*ValIsFuncFN);
    typedef Value (*NativeFN)(const FuncContext&);
    typedef void (*PtrFreeFN)(void*);
    typedef bool (*AstParsePrefixFN)(AstParser*, bool);
    typedef bool (*AstParseInfixFN)(AstParser*, AstToken, bool);
    typedef Value (*ClassFieldFN)();
    typedef void (*ModLoaderFN)();
    typedef DefExport* (*ModInitFN)();
    typedef Value (*nnbinopfunc_t)(double, double);

    typedef size_t (*mcitemhashfn_t)(void*);
    typedef bool (*mcitemcomparefn_t)(void*, void*);
    typedef int (*StrBufCharModFunc)(int);

    template <typename ClassT>
    concept MemoryClassHasDestroyFunc = requires(ClassT* ptr)
    {
        { ClassT::destroy(ptr) };
    };

    class Memory
    {
        public:
            template<typename ClassT, typename... ArgsT>
            static inline ClassT* make(ArgsT&&... args)
            {
                ClassT* tmp;
                ClassT* ret;
                tmp = (ClassT*)nn_memory_malloc(sizeof(ClassT));
                ret = new(tmp) ClassT(args...);
                return ret;
            }

            template<typename ClassT, typename... ArgsT>
            static inline void destroy(ClassT* cls, ArgsT&&... args)
            {
                if constexpr (MemoryClassHasDestroyFunc<ClassT>)
                {
                    //std::cerr << "Memory::destroy: using destroy" << std::endl;
                    ClassT::destroy(cls, args...);
                }
                else
                {
                    //std::cerr << "Memory::destroy: using free()" << std::endl;
                    nn_memory_free(cls);
                }
            }
    };




    /* allocator.c */
    void* nn_allocator_create();
    void nn_allocator_destroy(void* msp);
    void* nn_allocuser_malloc(void* msp, size_t nsize);
    void* nn_allocuser_free(void* msp, void* ptr);
    void* nn_allocuser_realloc(void* msp, void* ptr, size_t nsize);
    /* core.c */
    void nn_argcheck_init(ArgCheck* ch, const char* name, const FuncContext&);



    void nn_state_initbuiltinmethods();
    Value nn_except_getstacktrace();
    bool nn_except_propagate();
    bool nn_except_pushhandler(Class* type, int address, int finallyaddress);
    Class* nn_except_makeclass(Class* baseclass, Module* module, const char* cstrname, bool iscs);
    Instance* nn_except_makeinstance(Class* exklass, const char* srcfile, int srcline, String* message);
    bool nn_state_defglobalvalue(const char* name, Value val);
    bool nn_state_defnativefunctionptr(const char* name, NativeFN fptr, void* uptr);
    bool nn_state_defnativefunction(const char* name, NativeFN fptr);
    Class* nn_util_makeclass(const char* name, Class* parent);
    bool nn_util_methodisprivate(String* name);
    Status execSource(Module* module, const char* source, const char* filename, Value* dest);
    Value evalSource(const char* source);
    /* dbg.c */
    void nn_dbg_disasmblob(IOStream* pr, Blob* blob, const char* name);
    void nn_dbg_printinstrname(IOStream* pr, const char* name);
    int nn_dbg_printsimpleinstr(IOStream* pr, const char* name, int offset);
    int nn_dbg_printconstinstr(IOStream* pr, const char* name, Blob* blob, int offset);
    int nn_dbg_printpropertyinstr(IOStream* pr, const char* name, Blob* blob, int offset);
    int nn_dbg_printshortinstr(IOStream* pr, const char* name, Blob* blob, int offset);
    int nn_dbg_printbyteinstr(IOStream* pr, const char* name, Blob* blob, int offset);
    int nn_dbg_printjumpinstr(IOStream* pr, const char* name, int sign, Blob* blob, int offset);
    int nn_dbg_printtryinstr(IOStream* pr, const char* name, Blob* blob, int offset);
    int nn_dbg_printinvokeinstr(IOStream* pr, const char* name, Blob* blob, int offset);
    const char* nn_dbg_op2str(uint16_t instruc);
    int nn_dbg_printclosureinstr(IOStream* pr, const char* name, Blob* blob, int offset);
    int nn_dbg_printinstructionat(IOStream* pr, Blob* blob, int offset);


    void nn_state_installobjectarray();
    /* libdict.c */
    void nn_state_installobjectdict();
    void nn_state_installobjectfile();
    /* libfunc.c */
    void nn_funcscript_destroy(Function* ofn);
    /* libmodule.c */
    void loadBuiltinMethods();

    void nn_state_setupmodulepaths();
    bool nn_import_loadnativemodule(ModInitFN init_fn, char* importname, const char* source, void* dlw);
    void nn_import_addnativemodule(Module* module, const char* as);
    void nn_import_closemodule(void* hnd);
    /* libnumber.c */
    void nn_state_installobjectnumber();
    void nn_state_installmodmath();
    /* libobject.c */
    void nn_state_installobjectobject();
    /* libprocess.c */
    void nn_state_installobjectprocess();
    /* librange.c */
    void nn_state_installobjectrange();

    double nn_string_tabhashvaluecombine(const char* data, size_t len, uint32_t hsv);
    void nn_state_installobjectstring();

    Object* nn_gcmem_protect(Object* object);

    void nn_gcmem_markvalue(Value value);

    /* modast.c */
    DefExport* nn_natmodule_load_astscan();
    /* modcplx.c */
    DefExport* nn_natmodule_load_complex();
    /* modglobal.c */
    void nn_state_initbuiltinfunctions();
    /* modnull.c */
    DefExport* nn_natmodule_load_null();
    /* modos.c */
    void nn_modfn_os_preloader();
    DefExport* nn_natmodule_load_os();
    /* object.c */
    Upvalue* nn_object_makeupvalue(Value* slot, int stackpos);
    Userdata* nn_object_makeuserdata(void* pointer, const char* name);
    Switch* nn_object_makeswitch();
    Range* nn_object_makerange(int lower, int upper);



    /* utf.c */
    void nn_utf8iter_init(utf8iterator_t* iter, const char* ptr, uint32_t length);
    uint16_t nn_utf8iter_charsize(const char* character);
    uint32_t nn_utf8iter_converter(const char* character, uint16_t size);
    uint16_t nn_utf8iter_next(utf8iterator_t* iter);
    const char* nn_utf8iter_getchar(utf8iterator_t* iter);
    int nn_util_utf8numbytes(int value);
    char* nn_util_utf8encode(unsigned int code, size_t* dlen);
    int nn_util_utf8decode(const uint16_t* bytes, uint32_t length);
    char* nn_util_utf8codepoint(const char* str, char* outcodepoint);
    char* nn_util_utf8strstr(const char* haystack, const char* needle);
    char* nn_util_utf8index(char* s, int pos);
    void nn_util_utf8slice(char* s, int* start, int* end);
    /* utilhmap.c */
    void nn_valtable_mark(HashTable<Value, Value>* table);
    void nn_valtable_removewhites(HashTable<Value, Value>* table);
    /* utilstd.c */
    size_t nn_util_rndup2pow64(uint64_t x);
    const char* nn_util_color(Color tc);
    char* nn_util_strndup(const char* src, size_t len);
    char* nn_util_strdup(const char* src);
    void nn_util_mtseed(uint32_t seed, uint32_t* binst, uint32_t* index);
    uint32_t nn_util_mtgenerate(uint32_t* binst, uint32_t* index);
    double nn_util_mtrand(double lowerlimit, double upperlimit);
    char* nn_util_filereadhandle(FILE* hnd, size_t* dlen, bool havemaxsz, size_t maxsize);
    char* nn_util_filereadfile(const char* filename, size_t* dlen, bool havemaxsz, size_t maxsize);
    char* nn_util_filegetshandle(char* s, int size, FILE* f, size_t* lendest);
    int nn_util_filegetlinehandle(char** lineptr, size_t* destlen, FILE* hnd);
    char* nn_util_strtoupper(char* str, size_t length);
    char* nn_util_strtolower(char* str, size_t length);
    String* nn_util_numbertobinstring(long n);
    String* nn_util_numbertooctstring(int64_t n, bool numeric);
    String* nn_util_numbertohexstring(int64_t n, bool numeric);
    uint32_t nn_object_hashobject(Object* object);
    uint32_t nn_value_hashvalue(Value value);
    /* value.c */
    Value nn_value_copystrlen(const char* str, size_t len);
    Value nn_value_copystr(const char* str);
    String* nn_value_tostring(Value value);
    const char* nn_value_objecttypename(Object* object, bool detailed);
    const char* nn_value_typename(Value value, bool detailed);
    bool nn_value_compobjarray(Object* oa, Object* ob);
    bool nn_value_compobjstring(Object* oa, Object* ob);
    bool nn_value_compobjdict(Object* oa, Object* ob);
    bool nn_value_compobject(Value a, Value b);
    bool nn_value_compare_actual(Value a, Value b);
    bool nn_value_compare(Value a, Value b);
    Value nn_value_findgreater(Value a, Value b);
    void nn_value_sortvalues(Value* values, int count);
    Value nn_value_copyvalue(Value value);
    /* vm.c */
    void nn_vm_initvmstate();
    void nn_state_resetvmstate();
    bool nn_vm_callclosure(Function* closure, Value thisval, size_t argcount, bool fromoperator);
    bool nn_vm_callvaluewithobject(Value callable, Value thisval, size_t argcount, bool fromoper);
    bool nn_vm_callvalue(Value callable, Value thisval, size_t argcount, bool fromoperator);
    Class* nn_value_getclassfor(Value receiver);

    Value nn_vm_stackpeek(int distance);
    bool nn_util_isinstanceof(Class* klass1, Class* expected);
    Status nn_vm_runvm(int exitframe, Value* rv);
    int nn_nestcall_prepare(Value callable, Value mthobj, Value* callarr, int maxcallarr);
    bool nn_nestcall_callfunction(Value callable, Value thisval, Value* argv, size_t argc, Value* dest, bool fromoper);

    Function* nn_astutil_compilesource(Module* module, const char* source, Blob* blob, bool fromimport, bool keeplast);

    template<typename... ArgsT>
    bool throwScriptException(Class* exklass, const char* srcfile, int srcline, const char* format, ArgsT&&... args);

    /* topproto */

    union UtilDblUnion
    {
        uint64_t bits;
        double num;
    };

    size_t nn_util_rndup2pow64(uint64_t x)
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

    NEON_INLINE uint32_t nn_util_hashbits(uint64_t hs)
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

    NEON_INLINE uint32_t nn_util_hashdouble(double value)
    {
        UtilDblUnion bits;
        bits.num = value;
        return nn_util_hashbits(bits.bits);
    }

    NEON_INLINE uint32_t nn_util_hashstring(const char* str, size_t length)
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

    class Wrappers
    {
        public:
            static uint32_t wrapStrGetHash(String* os);
            static const char* wrapStrGetData(String* os);
            static size_t wrapStrGetLength(String* os);

    };

    class utf8iterator_t
    {
        public:
            /*input string pointer */
            const char* plainstr;

            /* input string length */
            uint32_t plainlen;

            /* the codepoint, or char */
            uint32_t codepoint;

            /* character size in bytes */
            uint16_t charsize;

            /* current character position */
            uint32_t currpos;

            /* next character position */
            uint32_t nextpos;

            /* number of counter characters currently */
            uint32_t currcount;
    };

    class IOResult
    {
        public:
            uint8_t success;
            char* data;
            size_t length;
    };

    class Object
    {    
        public:
            enum Type
            {
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
            Type m_objtype;
            bool m_objmark;
            /*
            // when an object is marked as stale, it means that the
            // GC will never collect this object. This can be useful
            // for library/package objects that want to reuse native
            // objects in their types/pointers. The GC cannot reach
            // them yet, so it's best for them to be kept stale.
            */
            bool m_objstale;
            Object* m_objnext;

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
            static void markValArray(ValArray<Value>* list);
            static void markDict(Dict* dict);

        public:
            Type m_valtype;
            union
            {
                uint8_t vbool;
                double vfltnum;
                Object* vobjpointer;
            } m_valunion;

        public:
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

    #include "list.h"

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
            struct Entry
            {
                HTKeyT key;
                Property value;
            };

        public:
            /*
             * FIXME: extremely stupid hack: $htactive ensures that a table that was destroyed
             * does not get marked again, et cetera.
             * since destroy() zeroes the data before freeing, $active will be
             * false, and thus, no further marking occurs.
             * obviously the reason is that somewhere a table (from Instance) is being
             * read after being freed, but for now, this will work(-ish).
             */
            bool htactive;
            int htcount;
            int htcapacity;
            Entry* htentries;

        public:
            static uint64_t getNextCapacity(uint64_t capacity)
            {
                if(capacity < 4)
                {
                    return 4;
                }
                return nn_util_rndup2pow64(capacity + 1);
            }

            void initTable()
            {
                this->htactive = true;
                this->htcount = 0;
                this->htcapacity = 0;
                this->htentries = nullptr;
            }

            void deInit()
            {
                nn_memory_free(this->htentries);
            }

            NEON_INLINE size_t count() const
            {
                return this->htcount;
            }

            NEON_INLINE size_t capacity() const
            {
                return this->htcapacity;
            }

            NEON_INLINE Entry* entryatindex(size_t idx) const
            {
                return &this->htentries[idx];
            }

            NEON_INLINE Entry* findentrybyvalue(Entry* entries, int capacity, HTKeyT key) const
            {
                uint32_t hsv;
                uint32_t index;
                Entry* entry;
                Entry* tombstone;
                hsv = nn_value_hashvalue(key);
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
                    else if(nn_value_compare(key, (HTKeyT)entry->key))
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
                            if(nn_value_compare(valkey, (HTKeyT)entry->key))
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
                if(this->htcount == 0 || this->htentries == nullptr)
                {
                    return nullptr;
                }
                entry = findentrybyvalue(this->htentries, this->htcapacity, key);
                if(entry->key.isNull() || entry->key.isNull())
                {
                    return nullptr;
                }
                return &entry->value;
            }

            NEON_INLINE Property* getfieldbystr(HTKeyT valkey, const char* kstr, size_t klen, uint32_t hsv) const
            {
                Entry* entry;
                if(this->htcount == 0 || this->htentries == nullptr)
                {
                    return nullptr;
                }
                entry = findentrybystr(this->htentries, this->htcapacity, valkey, kstr, klen, hsv);
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
                entries = (Entry*)nn_memory_malloc(sz);
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
                this->htcount = 0;
                for(i = 0; i < this->htcapacity; i++)
                {
                    entry = &this->htentries[i];
                    if(entry->key.isNull())
                    {
                        continue;
                    }
                    dest = findentrybyvalue(entries, capacity, (HTKeyT)entry->key);
                    dest->key = entry->key;
                    dest->value = entry->value;
                    this->htcount++;
                }
                nn_memory_free(this->htentries);
                this->htentries = entries;
                this->htcapacity = capacity;
                return true;
            }

            NEON_INLINE bool setwithtype(HTKeyT key, HTValT value, Property::FieldType ftyp, bool keyisstring)
            {
                bool isnew;
                int capacity;
                Entry* entry;
                (void)keyisstring;
                if((this->htcount + 1) > (this->htcapacity * NEON_CONFIG_MAXTABLELOAD))
                {
                    capacity = getNextCapacity(this->htcapacity);
                    if(!adjustcapacity(capacity))
                    {
                        return false;
                    }
                }
                entry = findentrybyvalue(this->htentries, this->htcapacity, key);
                isnew = entry->key.isNull();
                if(isnew && entry->value.value.isNull())
                {
                    this->htcount++;
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
                for(i = 0; i < this->htcapacity; i++)
                {
                    entry = &this->htentries[i];
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
                for(i = 0; i < (int)from->htcapacity; i++)
                {
                    entry = &from->htentries[i];
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
                for(i = 0; i < (int)this->htcapacity; i++)
                {
                    entry = &this->htentries[i];
                    if(!entry->key.isNull())
                    {
                        to->setwithtype((HTKeyT)entry->key, nn_value_copyvalue((HTValT)entry->value.value), entry->value.m_fieldtype, false);
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
                for(i = 0; i < (int)this->htcapacity; i++)
                {
                    entry = &this->htentries[i];
                    if(!entry->key.isNull() && !entry->key.isNull())
                    {
                        if(nn_value_compare((HTValT)entry->value.value, value))
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
                OPC_IMPORTIMPORT,
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
             * helper for nn_except_makeclass.
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
                blob->count = 0;
                blob->capacity = 0;
                blob->instrucs = nullptr;
            }

            static void destroy(Blob* blob)
            {
                if(blob->instrucs != nullptr)
                {
                    nn_memory_free(blob->instrucs);
                }
                blob->constants.deInit();
                blob->argdefvals.deInit();
            }


        public:
            int count;
            int capacity;
            Instruction* instrucs;
            ValArray<Value> constants;
            ValArray<Value> argdefvals;

        public:
            Blob()
            {
                init(this);
            }

            void push(Instruction ins)
            {
                int oldcapacity;
                if(this->capacity < this->count + 1)
                {
                    oldcapacity = this->capacity;
                    this->capacity = NN_ASTPARSER_GROWCAPACITY(oldcapacity);
                    this->instrucs = (Instruction*)nn_memory_realloc(this->instrucs, this->capacity * sizeof(Instruction));
                }
                this->instrucs[this->count] = ins;
                this->count++;
            }

            int addConstant(Value value)
            {
                this->constants.push(value);
                return this->constants.count() - 1;
            }
    };

    class CallFrame
    {
        public:
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
            ExceptionInfo handlers[NEON_CONFIG_MAXEXCEPTHANDLERS];
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
                        if(m_htab.htcount == 0)
                        {
                            return nullptr;
                        }
                        index = findhash & (m_htab.htcapacity - 1);
                        while(true)
                        {
                            auto entry = &m_htab.htentries[index];
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
                            index = (index + 1) & (m_htab.htcapacity - 1);
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
                int64_t stackidx;
                int64_t stackcapacity;
                int64_t framecapacity;
                int64_t framecount;
                Instruction currentinstr;
                CallFrame* currentframe;
                Upvalue* openupvalues;
                CallFrame* framevalues;
                Value* stackvalues;
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
             * these classes are used for runtime objects, specifically in nn_value_getclassfor.
             * every other global class needed (as created by nn_util_makeclass) need NOT be declared here.
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
            ValArray<Value> m_importpath;

            Value m_lastreplvalue;
            void* m_memuserptr;
            Module* m_topmodule;

        private:
            static bool defVarsFor(SharedState* gcs)
            {
                gcs->markvalue = true;
                gcs->m_gcstate.bytesallocated = 0;
                /* default is 1mb. Can be modified via the -g flag. */
                gcs->m_gcstate.nextgc = NEON_CONFIG_DEFAULTGCSTART;
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
                oldsize = 0;
                if(!retain)
                {
                    gcMaybeCollect(newsize - oldsize, newsize > oldsize);
                }
                result = nn_memory_malloc(newsize * amount);
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

            static void gcMarkRoots();

            static void gcTraceRefs()
            {
                Object* object;
                auto gcs = SharedState::get();
                while(gcs->m_gcstate.graycount > 0)
                {
                    gcs->m_gcstate.graycount--;
                    object = gcs->m_gcstate.graystack[gcs->m_gcstate.graycount];
                    Object::blackenObject(object);
                }
            }

            static void gcSweep()
            {
                Object* object;
                Object* previous;
                Object* unreached;
                previous = nullptr;
                auto gcs = SharedState::get();
                object = gcs->linkedobjects;
                while(object != nullptr)
                {
                    if(object->m_objmark == gcs->markvalue)
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
                            gcs->linkedobjects = object;
                        }
                        Object::destroyObject(unreached);
                    }
                }
            }

            static void gcLinkedObjectsDestroy()
            {
                Object* next;
                Object* object;
                auto gcs = SharedState::get();
                object = gcs->linkedobjects;
                while(object != nullptr)
                {
                    next = object->m_objnext;
                    Object::destroyObject(object);
                    object = next;
                }
                nn_memory_free(gcs->m_gcstate.graystack);
                gcs->m_gcstate.graystack = nullptr;
            }

            static void gcMarkCompilerRoots()
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

            static void gcCollectGarbage()
            {
                size_t before;
                (void)before;
                /*
                //  REMOVE THE NEXT LINE TO DISABLE NESTED gcCollectGarbage() POSSIBILITY!
                */
                auto gcs = SharedState::get();
                gcs->m_gcstate.nextgc = gcs->m_gcstate.bytesallocated;

                gcMarkRoots();
                gcTraceRefs();
                nn_valtable_removewhites(&gcs->m_allocatedstrings.m_htab);
                nn_valtable_removewhites(&gcs->m_openedmodules);
                gcSweep();
                gcs->m_gcstate.nextgc = gcs->m_gcstate.bytesallocated * NEON_CONFIG_GCHEAPGROWTHFACTOR;
                gcs->markvalue = !gcs->markvalue;
            }

            static void gcMaybeCollect(int addsize, bool wasnew)
            {
                auto gcs = SharedState::get();
                gcs->m_gcstate.bytesallocated += addsize;
                if(gcs->m_gcstate.nextgc > 0)
                {
                    if(wasnew && gcs->m_gcstate.bytesallocated > gcs->m_gcstate.nextgc)
                    {
                        if(gcs->m_vmstate.currentframe && gcs->m_vmstate.currentframe->gcprotcount == 0)
                        {
                            gcCollectGarbage();
                        }
                    }
                }
            }

            template<typename InputT>
            static void gcRelease(InputT* pointer, size_t oldsize)
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
                nn_memory_free(pointer);
                pointer = nullptr;
            }

            template<typename InputT>
            static void gcRelease(InputT* pointer)
            {
                gcRelease(pointer, sizeof(InputT));
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
                size_t allocsz;
                size_t nforvals;
                Value* oldbuf;
                Value* newbuf;
                nforvals = (needed * 2);
                oldsz = m_vmstate.stackcapacity;
                newsz = oldsz + nforvals;
                allocsz = ((newsz + 1) * sizeof(Value));
                oldbuf = m_vmstate.stackvalues;
                newbuf = (Value*)nn_memory_realloc(oldbuf, allocsz);
                if(newbuf == nullptr)
                {
                    fprintf(stderr, "internal error: failed to resize stackvalues!\n");
                    abort();
                }
                m_vmstate.stackvalues = (Value*)newbuf;
                m_vmstate.stackcapacity = newsz;
                return true;
            }

            NEON_INLINE bool resizeFrames(size_t needed)
            {
                /* return false; */
                size_t i;
                size_t oldsz;
                size_t newsz;
                size_t allocsz;
                int oldhandlercnt;
                Instruction* oldip;
                Function* oldclosure;
                CallFrame* oldbuf;
                CallFrame* newbuf;
                (void)i;
                oldclosure = m_vmstate.currentframe->closure;
                oldip = m_vmstate.currentframe->inscode;
                oldhandlercnt = m_vmstate.currentframe->m_handlercount;
                oldsz = m_vmstate.framecapacity;
                newsz = oldsz + needed;
                allocsz = ((newsz + 1) * sizeof(CallFrame));
                oldbuf = m_vmstate.framevalues;
                newbuf = (CallFrame*)nn_memory_realloc(oldbuf, allocsz);
                if(newbuf == nullptr)
                {
                    fprintf(stderr, "internal error: failed to resize framevalues!\n");
                    abort();
                }
                m_vmstate.framevalues = (CallFrame*)newbuf;
                m_vmstate.framecapacity = newsz;
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
                    if(!resizeStack(nn_util_rndup2pow64(m_vmstate.stackidx + 1)))
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
                    if(!resizeFrames(nn_util_rndup2pow64(m_vmstate.framecapacity + 1)))
                    {
                        fprintf(stderr, "failed to resize frames due to overflow");
                        return false;
                    }
                    return true;
                }
                return false;
            }

            NEON_INLINE void stackPush(Value value)
            {
                checkMaybeResizeStack();
                m_vmstate.stackvalues[m_vmstate.stackidx] = value;
                m_vmstate.stackidx++;
            }

            NEON_INLINE auto stackPop()
            {
                m_vmstate.stackidx--;
                if(m_vmstate.stackidx < 0)
                {
                    m_vmstate.stackidx = 0;
                }
                return m_vmstate.stackvalues[m_vmstate.stackidx];
            }

            NEON_INLINE auto stackPop(int n)
            {
                m_vmstate.stackidx -= n;
                if(m_vmstate.stackidx < 0)
                {
                    m_vmstate.stackidx = 0;
                }
                return m_vmstate.stackvalues[m_vmstate.stackidx];
            }
    };

    SharedState* SharedState::m_myself = nullptr;

    class String : public Object
    {
        public:
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

            static String* makeFromStrbuf(NNStringBuffer buf, uint32_t hsv, size_t length)
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
                nn_strbuf_destroyfromstack(&str->m_sbuf);
            }

            static String* intern(const char* strdata, int length)
            {
                uint32_t hsv;
                NNStringBuffer buf;
                String* rs;
                hsv = nn_util_hashstring(strdata, length);
                rs = strTabFind(strdata, length, hsv);
                if(rs != nullptr)
                {
                    return rs;
                }
                nn_strbuf_makebasicemptystack(&buf, nullptr, 0);
                buf.isintern = true;
                nn_strbuf_setdata(&buf, (char*)strdata);
                nn_strbuf_setlength(&buf, length);
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
                NNStringBuffer buf;
                hsv = nn_util_hashstring(strdata, length);
                rs = strTabFind(strdata, length, hsv);
                if(rs != nullptr)
                {
                    nn_memory_free(strdata);
                    return rs;
                }
                nn_strbuf_makebasicemptystack(&buf, nullptr, 0);
                nn_strbuf_setdata(&buf, strdata);
                nn_strbuf_setlength(&buf, length);
                return makeFromStrbuf(buf, hsv, length);
            }

            static String* take(char* strdata)
            {
                return take(strdata, strlen(strdata));
            }

            static String* copy(const char* strdata, int length)
            {
                uint32_t hsv;
                NNStringBuffer sb;
                String* rs;
                hsv = nn_util_hashstring(strdata, length);
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
                nn_strbuf_makebasicemptystack(&sb, strdata, length);
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
            NNStringBuffer m_sbuf;

        public:
            const char* data() const
            {
                return nn_strbuf_data((NNStringBuffer*)&m_sbuf);
            }

            char* mutdata()
            {
                return nn_strbuf_mutdata(&m_sbuf);
            }

            size_t length() const
            {
                return nn_strbuf_length((NNStringBuffer*)&m_sbuf);
            }

            bool setLength(size_t nlen)
            {
                return nn_strbuf_setlength(&m_sbuf, nlen);
            }

            bool set(size_t idx, int byte)
            {
                return nn_strbuf_set(&m_sbuf, idx, byte);
            }

            int get(size_t idx)
            {
                return nn_strbuf_get(&m_sbuf, idx);
            }

            bool append(const char* str, size_t len)
            {
                return nn_strbuf_appendstrn(&m_sbuf, str, len);
            }

            bool append(const char* str)
            {
                return this->append(str, strlen(str));
            }

            bool appendObject(String* other)
            {
                return this->append(other->data(), other->length());
            }

            bool appendByte(int ch)
            {
                return nn_strbuf_appendchar(&m_sbuf, ch);
            }

            template<typename... ArgsT>
            int appendfmt(const char* fmt, ArgsT&&... args)
            {
                return nn_strbuf_appendformat(&m_sbuf, fmt, args...);
            }

            String* substr(size_t start, size_t maxlen)
            {
                char* str;
                String* rt;
                str = nn_strbuf_substr(&m_sbuf, start, maxlen);
                rt = take(str, maxlen);
                return rt;
            }

            String* substr(size_t start)
            {
                return this->substr(start, this->length());
            }
    };

    class Upvalue : public Object
    {
        public:
            int stackpos;
            Value closed;
            Value location;
            Upvalue* next;
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
            ContextType m_contexttype;
            String* m_funcname;
            int upvalcount;
            Value clsthisval;
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
                ofn->clsthisval = thisval;
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
                ofn->upvalcount = 0;
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
                if(innerfn->upvalcount > 0)
                {
                    upvals = (Upvalue**)SharedState::gcAllocate(sizeof(Upvalue*), innerfn->upvalcount + 1, false);
                    for(i = 0; i < innerfn->upvalcount; i++)
                    {
                        upvals[i] = nullptr;
                    }
                }
                ofn = makeFuncGeneric(innerfn->m_funcname, Object::OTYP_FUNCCLOSURE, thisval);
                ofn->m_fnvals.fnclosure.scriptfunc = innerfn;
                ofn->m_fnvals.fnclosure.m_upvalues = upvals;
                ofn->upvalcount = innerfn->upvalcount;
                return ofn;
            }

            public:

    };

    class Module : public Object
    {
        public:
            static Module* make(const char* name, const char* file, bool imported, bool retain)
            {
                Module* module;
                module = SharedState::gcMakeObject<Module>(Object::OTYP_MODULE, retain);
                module->deftable.initTable();
                module->m_modname = String::copy(name);
                module->physicalpath = String::copy(file);
                module->fnunloaderptr = nullptr;
                module->fnpreloaderptr = nullptr;
                module->handle = nullptr;
                module->imported = imported;
                return module;
            }

            static bool addSearchPathObj(String* os)
            {
                auto gcs = SharedState::get();
                gcs->m_importpath.push(Value::fromObject(os));
                return true;
            }

            static bool addSearchPath(const char* path)
            {
                return addSearchPathObj(String::copy(path));
            }

            static void destroy(Module* module)
            {
                ModLoaderFN asfn;
                module->deftable.deInit();
                /*
                nn_memory_free(module->m_modname);
                nn_memory_free(module->physicalpath);
                */
                if(module->fnunloaderptr != nullptr && module->imported)
                {
                    asfn = *(ModLoaderFN*)module->fnunloaderptr;
                    asfn();
                }
                if(module->handle != nullptr)
                {
                    nn_import_closemodule(module->handle);
                }
            }

            static char* resolvePath(const char* modulename, const char* currentfile, char* rootfile, bool isrelative)
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
                NNStringBuffer* pathbuf;
                (void)rootfile;
                (void)isrelative;
                (void)stroot;
                (void)stmod;
                auto gcs = SharedState::get();
                mlen = strlen(modulename);
                splen = gcs->m_importpath.count();
                pathbuf = nn_strbuf_makebasicempty(nullptr, 0);
                for(i = 0; i < splen; i++)
                {
                    pitem = gcs->m_importpath.get(i).asString();
                    nn_strbuf_reset(pathbuf);
                    nn_strbuf_appendstrn(pathbuf, pitem->data(), pitem->length());
                    if(nn_strbuf_containschar(pathbuf, '@'))
                    {
                        nn_strbuf_charreplace(pathbuf, '@', modulename, mlen);
                    }
                    else
                    {
                        nn_strbuf_appendstr(pathbuf, "/");
                        nn_strbuf_appendstr(pathbuf, modulename);
                        nn_strbuf_appendstr(pathbuf, NEON_CONFIG_FILEEXT);
                    }
                    cstrpath = nn_strbuf_data(pathbuf);
                    fprintf(stderr, "import: trying '%s' ... ", cstrpath);
                    if(nn_util_fsfileexists(cstrpath))
                    {
                        fprintf(stderr, "found!\n");
            /* stop a core library from importing itself */
            #if 1
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
            #if 1
                        path1 = osfn_realpath(cstrpath, nullptr);
                        path2 = osfn_realpath(currentfile, nullptr);
            #else
                        path1 = strdup(cstrpath);
                        path2 = strdup(currentfile);
            #endif
                        if(path1 != nullptr && path2 != nullptr)
                        {
                            if(memcmp(path1, path2, (int)strlen(path2)) == 0)
                            {
                                nn_memory_free(path1);
                                nn_memory_free(path2);
                                path1 = nullptr;
                                path2 = nullptr;
                                fprintf(stderr, "resolvepath: refusing to import itself\n");
                                return nullptr;
                            }
                            if(path2 != nullptr)
                            {
                                nn_memory_free(path2);
                            }
                            nn_strbuf_destroy(pathbuf);
                            pathbuf = nullptr;
                            retme = nn_util_strdup(path1);
                            if(path1 != nullptr)
                            {
                                nn_memory_free(path1);
                            }
                            return retme;
                        }
                    }
                    else
                    {
                        fprintf(stderr, "does not exist\n");
                    }
                }
                nn_strbuf_destroy(pathbuf);
                return nullptr;
            }

            static Module* loadScriptModule(Module* intomodule, String* modulename)
            {
                size_t fsz;
                char* source;
                char* physpath;
                Blob blob;
                Value retv;
                Value callable;
                Property* field;
                String* os;
                Module* module;
                Function* closure;
                Function* function;
                (void)os;
                (void)intomodule;
                auto gcs = SharedState::get();
                field = gcs->m_openedmodules.getfieldbyostr(modulename);
                if(field != nullptr)
                {
                    Blob::destroy(&blob);
                    return field->value.asModule();
                }
                physpath = resolvePath(modulename->data(), intomodule->physicalpath->data(), nullptr, false);
                if(physpath == nullptr)
                {
                    Blob::destroy(&blob);
                    nn_except_throwclass(gcs->m_exceptions.importerror, "module not found: '%s'", modulename->data());
                    return nullptr;
                }
                fprintf(stderr, "loading module from '%s'\n", physpath);
                source = nn_util_filereadfile(physpath, &fsz, false, 0);
                if(source == nullptr)
                {
                    nn_except_throwclass(gcs->m_exceptions.importerror, "could not read import file %s", physpath);
                    Blob::destroy(&blob);
                    return nullptr;
                }
                module = Module::make(modulename->data(), physpath, true, true);
                nn_memory_free(physpath);
                function = nn_astutil_compilesource(module, source, &blob, true, false);
                nn_memory_free(source);
                closure = Function::makeFuncClosure(function, Value::makeNull());
                callable = Value::fromObject(closure);
                nn_nestcall_prepare(callable, Value::makeNull(), nullptr, 0);
                if(!nn_nestcall_callfunction(callable, Value::makeNull(), nullptr, 0, &retv, false))
                {
                    Blob::destroy(&blob);
                    nn_except_throwclass(gcs->m_exceptions.importerror, "failed to call compiled import closure");
                    return nullptr;
                }
                Blob::destroy(&blob);
                return module;
            }

        public:
            /* was this module imported? */
            bool imported;
            /* named exports */
            HashTable<Value, Value> deftable;
            /* the name of this module */
            String* m_modname;
            /* physsical location of this module, or nullptr if some other non-physical location */
            String* physicalpath;
            /* callback to call BEFORE this module is loaded */
            void* fnpreloaderptr;
            /* callbac to call AFTER this module is unloaded */
            void* fnunloaderptr;
            /* pointer that is based to preloader/unloader */
            void* handle;

        public:
            void setInternFileField()
            {
                return;
                this->deftable.set(Value::fromObject(String::intern("__file__")), Value::fromObject(String::copyObject(this->physicalpath)));
            }
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
                klass->m_instmethods.deInit();
                klass->m_staticmethods.deInit();
                klass->m_instproperties.deInit();
                klass->m_staticproperties.deInit();
                /*
                // We are not freeing the initializer because it's a closure and will still be freed accordingly later.
                */
                memset(klass, 0, sizeof(Class));
                SharedState::gcRelease(klass);
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
                return this->defCallableFieldPtr(name, function, nullptr);
            }

            bool defStaticCallableFieldPtr(String* name, NativeFN function, void* uptr)
            {
                Function* ofn;
                ofn = Function::makeFuncNative(function, name->data(), uptr);
                return m_staticproperties.setwithtype(Value::fromObject(name), Value::fromObject(ofn), Property::FTYP_FUNCTION, true);
            }

            bool defStaticCallableField(String* name, NativeFN function)
            {
                return this->defStaticCallableFieldPtr(name, function, nullptr);
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
                return this->defNativeConstructorPtr(function, nullptr);
            }

            bool defMethod(String* name, Value val)
            {
                return m_instmethods.set(Value::fromObject(name), val);
            }

            bool defNativeMethodPtr(String* name, NativeFN function, void* ptr)
            {
                Function* ofn;
                ofn = Function::makeFuncNative(function, name->data(), ptr);
                return this->defMethod(name, Value::fromObject(ofn));
            }

            bool defNativeMethod(String* name, NativeFN function)
            {
                return this->defNativeMethodPtr(name, function, nullptr);
            }

            bool defStaticNativeMethodPtr(String* name, NativeFN function, void* uptr)
            {
                Function* ofn;
                ofn = Function::makeFuncNative(function, name->data(), uptr);
                return m_staticmethods.set(Value::fromObject(name), Value::fromObject(ofn));
            }

            bool defStaticNativeMethod(String* name, NativeFN function)
            {
                return this->defStaticNativeMethodPtr(name, function, nullptr);
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
                    // nn_state_warn("trying to mark inactive instance <%p>!", instance);
                    return;
                }
                nn_valtable_mark(&instance->m_instanceprops);
                Object::markObject((Object*)instance->m_instanceclass);
            }

            static void destroy(Instance* instance)
            {
                if(!instance->m_instanceclass->m_destructor.isNull())
                {
                    // if(!nn_vm_callvaluewithobject(instance->klass->m_destructor, Value::fromObject(instance), 0, false))
                    {
                    }
                }
                instance->m_instanceprops.deInit();
                instance->m_instactive = false;
                SharedState::gcRelease(instance);
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

            Property* getPropertycstr(const char* name)
            {
                String* os;
                os = String::intern(name);
                return this->getProperty(os);
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

            Property* getMethodcstr(const char* name)
            {
                String* os;
                os = String::intern(name);
                return this->getMethod(os);
            }
    };

    class Array : public Object
    {
        public:
            ValArray<Value> m_objvarray;

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
                /*gcs->stackPush(value);*/
                m_objvarray.push(value);
                /*gcs->stackPop(); */
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

    class Range : public Object
    {
        public:
            int lower;
            int upper;
            int range;
    };

    class Dict : public Object
    {
        public:
            ValArray<Value> htnames;
            HashTable<Value, Value> htab;

        public:
            static Dict* make()
            {
                Dict* dict;
                dict = SharedState::gcMakeObject<Dict>(Object::OTYP_DICT, false);
                dict->htab.initTable();
                return dict;
            }

            static void destroy(Dict* dict)
            {
                dict->htnames.deInit();
                dict->htab.deInit();
            }

            bool set(Value key, Value value)
            {
                Value tempvalue;
                if(!this->htab.get(key, &tempvalue))
                {
                    /* add key if it doesn't exist. */
                    this->htnames.push(key);
                }
                return this->htab.set(key, value);
            }

            void add(Value key, Value value)
            {
                this->set(key, value);
            }

            void addCstr(const char* ckey, Value value)
            {
                String* os;
                os = String::copy(ckey);
                this->add(Value::fromObject(os), value);
            }

            Property* get(Value key)
            {
                return this->htab.getfield(key);
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
                dsz = this->htnames.count();
                for(i = 0; i < dsz; i++)
                {
                    key = this->htnames.get(i);
                    field = this->htab.getfield(this->htnames.get(i));
                    ndict->htnames.push(key);
                    ndict->htab.setwithtype(key, field->value, field->m_fieldtype, key.isString());
                }
                return ndict;
            }
    };

    class File : public Object
    {
        public:
            static File* make(FILE* handle, bool isstd, const char* path, const char* mode)
            {
                File* file;
                file = SharedState::gcMakeObject<File>(Object::OTYP_FILE, false);
                file->isopen = false;
                file->mode = String::copy(mode);
                file->path = String::copy(path);
                file->isstd = isstd;
                file->handle = handle;
                file->istty = false;
                file->number = -1;
                if(file->handle != nullptr)
                {
                    file->isopen = true;
                }
                return file;
            }

            static void destroy(File* file)
            {
                file->closeFile();
                SharedState::gcRelease(file);
            }

            static void mark(File* file)
            {
                Object::markObject((Object*)file->mode);
                Object::markObject((Object*)file->path);
            }

        public:
            bool isopen;
            bool isstd;
            bool istty;
            int number;
            FILE* handle;
            String* mode;
            String* path;

        public:
            bool readData(size_t readhowmuch, IOResult* dest)
            {
                size_t filesizereal;
                struct stat stats;
                filesizereal = -1;
                dest->success = false;
                dest->length = 0;
                dest->data = nullptr;
                if(!this->isstd)
                {
                    if(!nn_util_fsfileexists(this->path->data()))
                    {
                        return false;
                    }
                    /* file is in write only mode */
                    /*
                    else if(strstr(this->mode->data(), "w") != nullptr && strstr(this->mode->data(), "+") == nullptr)
                    {
                        NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot read file in write mode");
                    }
                    */
                    if(!this->isopen)
                    {
                        /* open the file if it isn't open */
                        this->openWithoutParams();
                    }
                    else if(this->handle == nullptr)
                    {
                        return false;
                    }
                    if(osfn_lstat(this->path->data(), &stats) == 0)
                    {
                        filesizereal = (size_t)stats.st_size;
                    }
                    else
                    {
                        /* fallback */
                        fseek(this->handle, 0L, SEEK_END);
                        filesizereal = ftell(this->handle);
                        rewind(this->handle);
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
                dest->data = (char*)nn_memory_malloc(sizeof(char) * (readhowmuch + 1));
                if(dest->data == nullptr && readhowmuch != 0)
                {
                    return false;
                }
                dest->length = fread(dest->data, sizeof(char), readhowmuch, this->handle);
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
                if(this->handle != nullptr && !this->isstd)
                {
                    fflush(this->handle);
                    result = fclose(this->handle);
                    this->handle = nullptr;
                    this->isopen = false;
                    this->number = -1;
                    this->istty = false;
                    return result;
                }
                return -1;
            }

            bool openWithoutParams()
            {
                if(this->handle != nullptr)
                {
                    return true;
                }
                if(this->handle == nullptr && !this->isstd)
                {
                    this->handle = fopen(this->path->data(), this->mode->data());
                    if(this->handle != nullptr)
                    {
                        this->isopen = true;
                        this->number = fileno(this->handle);
                        this->istty = osfn_isatty(this->number);
                        return true;
                    }
                    else
                    {
                        this->number = -1;
                        this->istty = false;
                    }
                    return false;
                }
                return false;
            }

    };

    class Switch : public Object
    {
        public:
            int defaultjump;
            int exitjump;
            HashTable<Value, Value> table;
    };

    class Userdata : public Object
    {
        public:
            void* pointer;
            char* m_udname;
            PtrFreeFN ondestroyfn;
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
                pr->fromstack = true;
                pr->handle = fh;
                pr->shouldclose = shouldclose;
                return true;
            }

            static bool makeStackString(IOStream* pr)
            {
                initStreamVars(pr, PRMODE_STRING);
                pr->fromstack = true;
                pr->wrmode = PRMODE_STRING;
                nn_strbuf_makebasicemptystack(&pr->strbuf, nullptr, 0);
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
                pr->handle = fh;
                pr->shouldclose = shouldclose;
                return pr;
            }

            static void destroy(IOStream* pr)
            {
                if(pr == nullptr)
                {
                    return;
                }
                if(pr->wrmode == PRMODE_UNDEFINED)
                {
                    return;
                }
                /*fprintf(stderr, "IOStream::destroy: pr->wrmode=%d\n", pr->wrmode);*/
                if(pr->wrmode == PRMODE_STRING)
                {
                    if(!pr->stringtaken)
                    {
                        nn_strbuf_destroyfromstack(&pr->strbuf);
                    }
                }
                else if(pr->wrmode == PRMODE_FILE)
                {
                    if(pr->shouldclose)
                    {
            #if 0
                        fclose(pr->handle);
            #endif
                    }
                }
                if(!pr->fromstack)
                {
                    nn_memory_free(pr);
                    pr = nullptr;
                }
            }

            static void initStreamVars(IOStream* pr, Mode mode)
            {
                pr->fromstack = false;
                pr->wrmode = PRMODE_UNDEFINED;
                pr->shouldclose = false;
                pr->shouldflush = false;
                pr->stringtaken = false;
                pr->shortenvalues = false;
                pr->jsonmode = false;
                pr->maxvallength = 15;
                pr->handle = nullptr;
                pr->wrmode = mode;
            }

        public:
            /* if file: should be closed when writer is destroyed? */
            uint8_t shouldclose;
            /* if file: should write operations be flushed via fflush()? */
            uint8_t shouldflush;
            /* if string: true if $strbuf was taken via nn_iostream_take */
            uint8_t stringtaken;
            /* was this writer instance created on stack? */
            uint8_t fromstack;
            uint8_t shortenvalues;
            uint8_t jsonmode;
            size_t maxvallength;
            /* the mode that determines what writer actually does */
            Mode wrmode;
            NNStringBuffer strbuf;
            FILE* handle;

        public:

            String* takeString()
            {
                size_t xlen;
                String* os;
                xlen = nn_strbuf_length(&this->strbuf);
                os = String::makeFromStrbuf(this->strbuf, nn_util_hashstring(nn_strbuf_data(&this->strbuf), xlen), xlen);
                this->stringtaken = true;
                return os;
            }

            String* copyString()
            {
                String* os;
                os = String::copy(nn_strbuf_data(&this->strbuf), nn_strbuf_length(&this->strbuf));
                return os;
            }

            void flush()
            {
                // if(this->shouldflush)
                {
                    fflush(this->handle);
                }
            }

            bool writeString(const char* estr, size_t elen)
            {
                // fprintf(stderr, "writestringl: (%d) <<<%.*s>>>\n", elen, elen, estr);
                size_t chlen;
                chlen = sizeof(char);
                if(elen > 0)
                {
                    if(this->wrmode == PRMODE_FILE)
                    {
                        fwrite(estr, chlen, elen, this->handle);
                        flush();
                    }
                    else if(this->wrmode == PRMODE_STRING)
                    {
                        nn_strbuf_appendstrn(&this->strbuf, estr, elen);
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
                if(this->wrmode == PRMODE_STRING)
                {
                    ch = b;
                    writeString(&ch, 1);
                }
                else if(this->wrmode == PRMODE_FILE)
                {
                    fputc(b, this->handle);
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
                if(this->wrmode == PRMODE_STRING)
                {
                    nn_strbuf_appendformat(&this->strbuf, fmt, args...);
                }
                else if(this->wrmode == PRMODE_FILE)
                {
                    tmpfprintf(this->handle, fmt, args...);
                    flush();
                }
                return true;
            }
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
                            pr->format("<range %d .. %d>", range->lower, range->upper);
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
                            pr->format("<module '%s' at '%s'>", mod->m_modname->data(), mod->physicalpath->data());
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
                    if(pr->shortenvalues && (i >= pr->maxvallength))
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
                dsz = dict->htnames.count();
                pr->format("{");
                for(i = 0; i < dsz; i++)
                {
                    valisrecur = false;
                    keyisrecur = false;
                    val = dict->htnames.get(i);
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
                    auto field = dict->htab.getfield(dict->htnames.get(i));
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
                    if(pr->shortenvalues && (pr->maxvallength >= i))
                    {
                        pr->format(" [%zd items]", dsz);
                        break;
                    }
                }
                pr->format("}");
            }

            static void printFile(IOStream* pr, File* file)
            {
                pr->format("<file at %s in mode %s>", file->path->data(), file->mode->data());
            }

            static void printInstance(IOStream* pr, Instance* instance, bool invmethod)
            {
                (void)invmethod;
            #if 0
                /*
                int arity;
                IOStream subw;
                Value resv;
                Value thisval;
                Property* field;
                String* os;
                Array* args;
                auto gcs = SharedState::get();
                if(invmethod)
                {
                    field = instance->m_instanceclass->m_instmethods.getfieldbycstr("toString");
                    if(field != nullptr)
                    {
                        args = Array::make();
                        thisval = Value::fromObject(instance);
                        arity = nn_nestcall_prepare(field->value, thisval, nullptr, 0);
                        fprintf(stderr, "arity = %d\n", arity);
                        gcs->stackPop();
                        gcs->stackPush(thisval);
                        if(nn_nestcall_callfunction(field->value, thisval, nullptr, 0, &resv, false))
                        {
                            IOStream::makeStackString(&subw);
                            printValue(pr, &subw, resv, false, false);
                            os = subw.takeString();
                            pr->writeString(os->data(), os->length());
                            #if 0
                                gcs->stackPop();
                            #endif
                            return;
                        }
                    }
                }
                */
            #endif
                pr->format("<instance of %s at %p>", instance->m_instanceclass->m_classname->data(), (void*)instance);
            }


            static void printValTable(IOStream* pr, HashTable<Value, Value>* table, const char* name)
            {
                size_t i;
                size_t hcap;
                hcap = table->capacity();
                pr->format("<HashTable of %s : {\n", name);
                for(i = 0; i < hcap; i++)
                {
                    auto entry = table->entryatindex(i);
                    if(!entry->key.isNull())
                    {
                        printValue(pr, entry->key, true, true);
                        pr->format(": ");
                        printValue(pr, entry->value.value, true, true);
                        if(i != (hcap - 1))
                        {
                            pr->format(",\n");
                        }
                    }
                }
                pr->format("}>\n");
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
                if(pr->jsonmode)
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
                            oldexp = pr->jsonmode;
                            pr->jsonmode = false;
                            printValue(pr, Value::fromObject(klass->m_superclass), true, false);
                            pr->jsonmode = oldexp;
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
                                nn_except_throwclass(gcs->m_exceptions.argumenterror, "too few arguments");
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
                                    nn_except_throwclass(gcs->m_exceptions.argumenterror, "unknown/invalid format flag '%%c'", nextch);
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
                T_KWIMPORT,
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
                    case AstToken::T_KWIMPORT:
                        return "AstToken::T_KWIMPORT";
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
                    nn_memory_free(lex);
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
                buf = (char*)nn_memory_malloc(sizeof(char) * 1024);
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
                    { "import", AstToken::T_KWIMPORT },                
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

                public:
                    AstParsePrefixFN prefix;
                    AstParseInfixFN infix;
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
                    bool m_fromimport;
                    FuncCompiler* m_enclosing;
                    AstParser* m_sharedprs;
                    /* current function */
                    Function* m_targetfunc;
                    Function::ContextType m_contexttype;
                    /* TODO: these should be dynamically allocated */
                    LocalVarInfo m_locals[NEON_CONFIG_ASTMAXLOCALS];
                    UpvalInfo m_upvalues[NEON_CONFIG_ASTMAXUPVALS];

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
                        m_fromimport = false;
                        m_targetfunc = Function::makeFuncScript(m_sharedprs->m_currentmodule, type);
                        m_sharedprs->m_currentfunccompiler = this;
                        if(type != Function::CTXTYPE_SCRIPT)
                        {
                            gcs->stackPush(Value::fromObject(m_targetfunc));
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
                            gcs->stackPop();
                        }
                        /* claiming slot zero for use in class methods */
                        local = &m_sharedprs->m_currentfunccompiler->m_locals[0];
                        m_sharedprs->m_currentfunccompiler->m_localcount++;
                        local->depth = 0;
                        local->iscaptured = false;
                        candeclthis = ((type != Function::CTXTYPE_FUNCTION) && (m_sharedprs->m_compcontext == NEON_COMPCONTEXT_CLASS));
                        if(candeclthis || (/*(type == Function::CTXTYPE_ANONYMOUS) &&*/ (m_sharedprs->m_compcontext != NEON_COMPCONTEXT_CLASS)))
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
                        upcnt = m_targetfunc->upvalcount;
                        for(i = 0; i < upcnt; i++)
                        {
                            upvalue = &m_upvalues[i];
                            if(upvalue->index == index && upvalue->islocal == islocal)
                            {
                                return i;
                            }
                        }
                        if(upcnt == NEON_CONFIG_ASTMAXUPVALS)
                        {
                            m_sharedprs->raiseerror("too many closure variables in function");
                            return 0;
                        }
                        m_upvalues[upcnt].islocal = islocal;
                        m_upvalues[upcnt].index = index;
                        return m_targetfunc->upvalcount++;
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
                            return this->addUpvalue((uint16_t)local, true);
                        }
                        upvalue = m_enclosing->resolveUpvalue(name);
                        if(upvalue != -1)
                        {
                            return this->addUpvalue((uint16_t)upvalue, false);
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
                        gcs->stackPush(Value::fromObject(function));
                        m_sharedprs->emitbyteandshort(Instruction::OPC_MAKECLOSURE, m_sharedprs->pushconst(Value::fromObject(function)));
                        for(i = 0; i < function->upvalcount; i++)
                        {
                            m_sharedprs->emit1byte(m_upvalues[i].islocal ? 1 : 0);
                            m_sharedprs->emit1short(m_upvalues[i].index);
                        }
                        gcs->stackPop();
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
            AstCompContext m_compcontext;
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
                parser->m_compcontext = NEON_COMPCONTEXT_NONE;
                parser->m_innermostloopstart = -1;
                parser->m_innermostloopscopedepth = 0;
                parser->m_currentclasscompiler = nullptr;
                parser->m_currentmodule = module;
                parser->m_keeplastvalue = keeplast;
                parser->m_lastwasstatement = false;
                parser->m_infunction = false;
                parser->m_inswitch = false;
                parser->m_currentfile = parser->m_currentmodule->physicalpath->data();
                return parser;
            }

            static void destroy(AstParser* parser)
            {
                nn_memory_free(parser);
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

            static bool astruleimport(AstParser* prs, bool canassign)
            {
                (void)canassign;
                prs->parseexpression();
                prs->emitinstruc(Instruction::OPC_IMPORTIMPORT);
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

            static Rule* putrule(Rule* dest, AstParsePrefixFN prefix, AstParseInfixFN infix, Rule::Precedence precedence)
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
                    dorule(AstToken::T_KWIMPORT, astruleimport, nullptr, Rule::PREC_NONE);
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
                colred = nn_util_color(NEON_COLOR_RED);
                colreset = nn_util_color(NEON_COLOR_RESET);
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
                fprintf(stderr, " in [%s:%d]: ", m_currentmodule->physicalpath->data(), t->m_line);
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
                    case Instruction::OPC_IMPORTIMPORT:
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
                        return 2 + (fn->upvalcount * 3);
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
                currentblob()->instrucs[idx].code = byte;
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
                offset = currentblob()->count - loopstart + 2;
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
                        if(m_currentfunccompiler->m_fromimport)
                        {
                            emitinstruc(Instruction::OPC_PUSHNULL);
                        }
                        else
                        {
                            emitinstruc(Instruction::OPC_PUSHEMPTY);
                        }
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
                return currentblob()->count - 2;
            }

            int emitswitch()
            {
                emitinstruc(Instruction::OPC_SWITCH);
                /* placeholders */
                emit1byte(0xff);
                emit1byte(0xff);
                return currentblob()->count - 2;
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
                return currentblob()->count - 6;
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
                jump = currentblob()->count - offset - 2;
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
                if(m_currentfunccompiler->m_localcount == NEON_CONFIG_ASTMAXLOCALS)
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
                const char* fname;
                Function* function;
                auto gcs = SharedState::get();
                emitreturn();
                if(istoplevel)
                {
                }
                function = m_currentfunccompiler->m_targetfunc;
                fname = nullptr;
                if(function->m_funcname == nullptr)
                {
                    fname = m_currentmodule->physicalpath->data();
                }
                else
                {
                    fname = function->m_funcname->data();
                }
                if(!m_haderror && gcs->m_conf.dumpbytecode)
                {
                    nn_dbg_disasmblob(gcs->m_debugwriter, currentblob(), fname);
                }
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
                while(i < m_currentfunccompiler->m_targetfunc->m_fnvals.fnscriptfunc.blob->count)
                {
                    if(m_currentfunccompiler->m_targetfunc->m_fnvals.fnscriptfunc.blob->instrucs[i].code == Instruction::OPC_BREAK_PL)
                    {
                        m_currentfunccompiler->m_targetfunc->m_fnvals.fnscriptfunc.blob->instrucs[i].code = Instruction::OPC_JUMPNOW;
                        patchjump(i + 1);
                        i += 3;
                    }
                    else
                    {
                        bcode = m_currentfunccompiler->m_targetfunc->m_fnvals.fnscriptfunc.blob->instrucs;
                        cvals = (Value*)m_currentfunccompiler->m_targetfunc->m_fnvals.fnscriptfunc.blob->constants.data();
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
                count = nn_util_utf8numbytes(value);
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
                    chr = nn_util_utf8encode(value, &len);
                    if(chr)
                    {
                        memcpy(string + index, chr, (size_t)count + 1);
                        nn_memory_free(chr);
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
                deststr = (char*)nn_memory_malloc(sizeof(char) * rawlen);
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
                AstParseInfixFN infixrule;
                AstParsePrefixFN prefixrule;
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
                        if(argcount == NEON_CONFIG_ASTMAXFUNCPARAMS)
                        {
                            raiseerror("cannot have more than %d arguments to a function", NEON_CONFIG_ASTMAXFUNCPARAMS);
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
                AstCompContext oldctx;
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
                m_compcontext = NEON_COMPCONTEXT_CLASS;
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
                m_innermostloopstart = currentblob()->count;
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
                    incrstart = currentblob()->count;
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
                if(m_currentfunccompiler->m_localcount + 3 > NEON_CONFIG_ASTMAXLOCALS)
                {
                    raiseerror("cannot declare more than %d variables in one scope", NEON_CONFIG_ASTMAXLOCALS);
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
                m_innermostloopstart = currentblob()->count;
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
                int caseends[NEON_CONFIG_ASTMAXSWITCHCASES];
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
                sw = nn_object_makeswitch();
                gcs->stackPush(Value::fromObject(sw));
                switchcode = emitswitch();
                /* emitbyteandshort(Instruction::OPC_SWITCH, pushconst(Value::fromObject(sw))); */
                startoffset = currentblob()->count;
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
                                jump = Value::makeNumber((double)currentblob()->count - (double)startoffset);
                                if(m_prevtoken.m_toktype == AstToken::T_KWTRUE)
                                {
                                    sw->table.set(Value::makeBool(true), jump);
                                }
                                else if(m_prevtoken.m_toktype == AstToken::T_KWFALSE)
                                {
                                    sw->table.set(Value::makeBool(false), jump);
                                }
                                else if(m_prevtoken.m_toktype == AstToken::T_LITERALSTRING || m_prevtoken.m_toktype == AstToken::T_LITERALRAWSTRING)
                                {
                                    str = compilestring(&length, true);
                                    string = String::take(str, length);
                                    /* gc fix */
                                    gcs->stackPush(Value::fromObject(string));
                                    sw->table.set(Value::fromObject(string), jump);
                                    /* gc fix */
                                    gcs->stackPop();
                                }
                                else if(checknumber())
                                {
                                    sw->table.set(compilenumber(), jump);
                                }
                                else
                                {
                                    /* pop the switch */
                                    gcs->stackPop();
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
                            sw->defaultjump = currentblob()->count - startoffset;
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
                sw->exitjump = currentblob()->count - startoffset;
                patchswitch(switchcode, pushconst(Value::fromObject(sw)));
                /* pop the switch */
                gcs->stackPop();
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
                if(m_currentfunccompiler->m_handlercount == NEON_CONFIG_MAXEXCEPTHANDLERS)
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
                    address = currentblob()->count;
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
                    finally = currentblob()->count;
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
                m_innermostloopstart = currentblob()->count;
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
                m_innermostloopstart = currentblob()->count;
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
                        case AstToken::T_KWIMPORT:
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

    class DefExport
    {
        public:
            struct Function
            {
                const char* m_deffuncname;
                bool isstatic;
                NativeFN function;
            };

            struct Field
            {
                const char* m_deffieldname;
                bool isstatic;
                NativeFN fieldvalfn;
            };

            struct Class
            {
                const char* m_defclassname;
                Field* defpubfields;
                Function* defpubfunctions;
            };

        public:
            /*
             * the name of this module.
             * note: if the name must be preserved, copy it; it is only a pointer to a
             * string that gets freed past loading.
             */
            const char* m_defmodname;

            /* exported fields, if any. */
            Field* definedfields;

            /* regular functions, if any. */
            Function* definedfunctions;

            /* exported classes, if any.
             * i.e.:
             * {"Stuff",
             *   (Field[]){
             *       {"enabled", true},
             *       ...
             *   },
             *   (Function[]){
             *       {"isStuff", myclass_fn_isstuff},
             *       ...
             * }})*/
            Class* definedclasses;

            /* function that is called directly upon loading the module. can be nullptr. */
            ModLoaderFN fnpreloaderfunc;

            /* function that is called before unloading the module. can be nullptr. */
            ModLoaderFN fnunloaderfunc;
    };


    class FuncContext
    {
        public:
            Value thisval;
            Value* argv;
            size_t argc;
    };


    class ArgCheck
    {
        public:
            const char* m_argcheckfuncname;
            FuncContext m_scriptfnctx;
    };


    class UClassComplex: public Object
    {
        public:
            Instance selfinstance;
            double re;
            double im;
    };

    #if defined(NEON_CONFIG_DEBUGMEMORY) && (NEON_CONFIG_DEBUGMEMORY == 1)
        #define nn_memory_malloc(...) nn_memory_debugmalloc(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)
        #define nn_memory_calloc(...) nn_memory_debugcalloc(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)
        #define nn_memory_realloc(...) nn_memory_debugrealloc(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)
    #endif

    /* bottom */


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


    void Value::markDict(Dict* dict)
    {
        Value::markValArray(&dict->htnames);
        nn_valtable_mark(&dict->htab);
    }

    void Value::markValArray(ValArray<Value>* list)
    {
        size_t i;
        NN_NULLPTRCHECK_RETURN(list);
        for(i = 0; i < list->count(); i++)
        {
            nn_gcmem_markvalue(list->get(i));
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
            return asDict()->htnames.count() == 0;
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
    #if defined(DEBUG_GC) && DEBUG_GC
        gcs->m_debugwriter->format("GC: marking object at <%p> ", (void*)object);
        ValPrinter::printValue(gcs->m_debugwriter, Value::fromObject(object), false);
        gcs->m_debugwriter->format("\n");
    #endif
        object->m_objmark = gcs->markvalue;
        if(gcs->m_gcstate.graycapacity < gcs->m_gcstate.graycount + 1)
        {
            gcs->m_gcstate.graycapacity = NEON_MEMORY_GROWCAPACITY(gcs->m_gcstate.graycapacity);
            gcs->m_gcstate.graystack = (Object**)nn_memory_realloc(gcs->m_gcstate.graystack, sizeof(Object*) * gcs->m_gcstate.graycapacity);
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
    #if defined(DEBUG_GC) && DEBUG_GC
        auto gcs = SharedState::get();
        gcs->m_debugwriter->format("GC: blacken object at <%p> ", (void*)object);
        ValPrinter::printValue(gcs->m_debugwriter, Value::fromObject(object), false);
        gcs->m_debugwriter->format("\n");
    #endif
        switch(object->m_objtype)
        {
            case Object::OTYP_MODULE:
            {
                Module* module;
                module = (Module*)object;
                nn_valtable_mark(&module->deftable);
            }
            break;
            case Object::OTYP_SWITCH:
            {
                Switch* sw;
                sw = (Switch*)object;
                nn_valtable_mark(&sw->table);
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
                nn_gcmem_markvalue(bound->m_fnvals.fnmethod.receiver);
                Object::markObject((Object*)bound->m_fnvals.fnmethod.method);
            }
            break;
            case Object::OTYP_CLASS:
            {
                Class* klass;
                klass = (Class*)object;
                Object::markObject((Object*)klass->m_classname);
                nn_valtable_mark(&klass->m_instmethods);
                nn_valtable_mark(&klass->m_staticmethods);
                nn_valtable_mark(&klass->m_staticproperties);
                nn_gcmem_markvalue(klass->m_constructor);
                nn_gcmem_markvalue(klass->m_destructor);
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
                for(i = 0; i < closure->upvalcount; i++)
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
                Value::markValArray(&function->m_fnvals.fnscriptfunc.blob->constants);
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
                nn_gcmem_markvalue(upv->closed);
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
                SharedState::gcRelease(module);
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
                SharedState::gcRelease(dict);
            }
            break;
            case Object::OTYP_ARRAY:
            {
                Array* list;
                list = (Array*)object;
                list->m_objvarray.deInit();
                SharedState::gcRelease(list);
            }
            break;
            case Object::OTYP_FUNCBOUND:
            {
                /*
                // a closure may be bound to multiple instances
                // for this reason, we do not free closures when freeing bound methods
                */
                auto fn = (Function*)object;
                SharedState::gcRelease(fn);
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
                nn_gcmem_freearray(sizeof(Upvalue*), closure->m_fnvals.fnclosure.m_upvalues, closure->upvalcount);
                /*
                // there may be multiple closures that all reference the same function
                // for this reason, we do not free functions when freeing closures
                */
                SharedState::gcRelease(closure);
            }
            break;
            case Object::OTYP_FUNCSCRIPT:
            {
                Function* function;
                function = (Function*)object;
                nn_funcscript_destroy(function);
                SharedState::gcRelease(function);
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
                SharedState::gcRelease(fn);
            }
            break;
            case Object::OTYP_UPVALUE:
            {
                auto upv = (Upvalue*)object;
                SharedState::gcRelease(upv);
            }
            break;
            case Object::OTYP_RANGE:
            {
                auto rn = (Range*)object;
                SharedState::gcRelease(rn);
            }
            break;
            case Object::OTYP_STRING:
            {
                String* string;
                string = (String*)object;
                String::destroy(string);
                SharedState::gcRelease(string);
            }
            break;
            case Object::OTYP_SWITCH:
            {
                Switch* sw;
                sw = (Switch*)object;
                sw->table.deInit();
                SharedState::gcRelease(sw);
            }
            break;
            case Object::OTYP_USERDATA:
            {
                Userdata* ptr;
                ptr = (Userdata*)object;
                if(ptr->ondestroyfn)
                {
                    ptr->ondestroyfn(ptr->pointer);
                }
                SharedState::gcRelease(ptr);
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
        auto gcs = SharedState::get();
        for(slot = gcs->m_vmstate.stackvalues; slot < &gcs->m_vmstate.stackvalues[gcs->m_vmstate.stackidx]; slot++)
        {
            nn_gcmem_markvalue(*slot);
        }
        for(i = 0; i < (int)gcs->m_vmstate.framecount; i++)
        {
            Object::markObject((Object*)gcs->m_vmstate.framevalues[i].closure);
            for(j = 0; j < (int)gcs->m_vmstate.framevalues[i].m_handlercount; j++)
            {
                handler = &gcs->m_vmstate.framevalues[i].handlers[j];
            }
        }
        for(upvalue = gcs->m_vmstate.openupvalues; upvalue != nullptr; upvalue = upvalue->next)
        {
            Object::markObject((Object*)upvalue);
        }
        nn_valtable_mark(&gcs->m_declaredglobals);
        nn_valtable_mark(&gcs->m_openedmodules);
        // Object::markObject((Object*)gcs->m_exceptions.stdexception);
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
        hsv = nn_util_hashstring(kstr, klen);
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
        if(this->htcount == 0)
        {
            return false;
        }
        /* find the entry */
        entry = findentrybyvalue(this->htentries, this->htcapacity, key);
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
    static void nn_vmdebug_printvalue(Value val, const char* fmt, ArgsT&&... args)
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
        message = (char*)nn_memory_malloc(kMaxBufSize + 1);
        length = tmpsnprintf(message, kMaxBufSize, format, args...);
        instance = nn_except_makeinstance(exklass, srcfile, srcline, String::take(message, length));
        gcs->stackPush(Value::fromObject(instance));
        stacktrace = nn_except_getstacktrace();
        gcs->stackPush(stacktrace);
        instance->defProperty(String::intern("stacktrace"), stacktrace);
        gcs->stackPop();
        return nn_except_propagate();
    }

    /**
    * raise a fatal error that cannot recover.
    */

    template<typename... ArgsT>
    void nn_state_raisefatalerror(const char* format, ArgsT&&... args)
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
        instruction = frame->inscode - function->m_fnvals.fnscriptfunc.blob->instrucs - 1;
        line = function->m_fnvals.fnscriptfunc.blob->instrucs[instruction].fromsourceline;
        fprintf(stderr, "RuntimeError: ");
        tmpfprintf(stderr, format, args...);
        fprintf(stderr, " -> %s:%d ", function->m_fnvals.fnscriptfunc.module->physicalpath->data(), line);
        fputs("\n", stderr);
        if(gcs->m_vmstate.framecount > 1)
        {
            fprintf(stderr, "stacktrace:\n");
            for(i = gcs->m_vmstate.framecount - 1; i >= 0; i--)
            {
                frame = &gcs->m_vmstate.framevalues[i];
                function = frame->closure->m_fnvals.fnclosure.scriptfunc;
                /* -1 because the IP is sitting on the next instruction to be executed */
                instruction = frame->inscode - function->m_fnvals.fnscriptfunc.blob->instrucs - 1;
                fprintf(stderr, "    %s:%d -> ", function->m_fnvals.fnscriptfunc.module->physicalpath->data(), function->m_fnvals.fnscriptfunc.blob->instrucs[instruction].fromsourceline);
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
        nn_state_resetvmstate();
    }

    NEON_INLINE const char* nn_value_typefromfunction(ValIsFuncFN func)
    {
        (void)func;
    #if 0
        if(func == nn_value_isstring)
        {
            return "string";
        }
        else if(func == &nn_value_isnull)
        {
            return "null";
        }        
        else if(func == nn_value_isbool)
        {
            return "bool";
        }        
        else if(func == nn_value_isnumber)
        {
            return "number";
        }
        else if(func == nn_value_isstring)
        {
            return "string";
        }
        else if((func == nn_value_isfuncnative) || (func == nn_value_isfuncbound) || (func == nn_value_isfuncscript) || (func == nn_value_isfuncclosure) || (func == nn_value_iscallable))
        {
            return "function";
        }
        else if(func == nn_value_isclass)
        {
            return "class";
        }
        else if(func == nn_value_isinstance)
        {
            return "instance";
        }
        else if(func == nn_value_isarray)
        {
            return "array";
        }
        else if(func == nn_value_isdict)
        {
            return "dictionary";
        }
        else if(func == nn_value_isfile)
        {
            return "file";
        }
        else if(func == nn_value_isrange)
        {
            return "range";
        }
        else if(func == nn_value_ismodule)
        {
            return "module";
        }
    #endif
        return "?unknown?";
    }



    Object* nn_gcmem_protect(Object* object)
    {
        size_t frpos;
        auto gcs = SharedState::get();
        gcs->stackPush(Value::fromObject(object));
        frpos = 0;
        if(gcs->m_vmstate.framecount > 0)
        {
            frpos = gcs->m_vmstate.framecount - 1;
        }
        gcs->m_vmstate.framevalues[frpos].gcprotcount++;
        return object;
    }


    void nn_gcmem_markvalue(Value value)
    {
        if(value.isObject())
        {
            Object::markObject(value.asObject());
        }
    }




    /*
    via: https://github.com/adrianwk94/utf8-iterator
    UTF-8 Iterator. Version 0.1.3

    Original code by Adrian Guerrero Vera (adrianwk94@gmail.com)
    MIT License
    Copyright (c) 2016 Adrian Guerrero Vera

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

    */

    /* allows you to set a custom length. */
    void nn_utf8iter_init(utf8iterator_t* iter, const char* ptr, uint32_t length)
    {
        iter->plainstr = ptr;
        iter->plainlen = length;
        iter->codepoint = 0;
        iter->currpos = 0;
        iter->nextpos = 0;
        iter->currcount = 0;
    }

    /* calculate the number of bytes a UTF8 character occupies in a string. */
    uint16_t nn_utf8iter_charsize(const char* character)
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

    uint32_t nn_utf8iter_converter(const char* character, uint16_t size)
    {
        uint16_t i;
        static uint32_t codepoint = 0;
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
        codepoint = g_utf8iter_table_unicode[size] & character[0];
        for(i = 1; i < size; i++)
        {
            codepoint = codepoint << 6;
            codepoint = codepoint | (character[i] & 0x3F);
        }
        return codepoint;
    }

    /* returns 1 if there is a character in the next position. If there is not, return 0. */
    uint16_t nn_utf8iter_next(utf8iterator_t* iter)
    {
        const char* pointer;
        if(iter == nullptr)
        {
            return 0;
        }
        if(iter->plainstr == nullptr)
        {
            return 0;
        }
        if(iter->nextpos < iter->plainlen)
        {
            iter->currpos = iter->nextpos;
            /* Set Current Pointer */
            pointer = iter->plainstr + iter->nextpos;
            iter->charsize = nn_utf8iter_charsize(pointer);
            if(iter->charsize == 0)
            {
                return 0;
            }
            iter->nextpos = iter->nextpos + iter->charsize;
            iter->codepoint = nn_utf8iter_converter(pointer, iter->charsize);
            if(iter->codepoint == 0)
            {
                return 0;
            }
            iter->currcount++;
            return 1;
        }
        iter->currpos = iter->nextpos;
        return 0;
    }

    /* return current character in UFT8 - no same that iter.codepoint (not codepoint/unicode) */
    const char* nn_utf8iter_getchar(utf8iterator_t* iter)
    {
        uint16_t i;
        const char* pointer;
        static char str[10];
        str[0] = '\0';
        if(iter == nullptr)
        {
            return str;
        }
        if(iter->plainstr == nullptr)
        {
            return str;
        }
        if(iter->charsize == 0)
        {
            return str;
        }
        if(iter->charsize == 1)
        {
            str[0] = iter->plainstr[iter->currpos];
            str[1] = '\0';
            return str;
        }
        pointer = iter->plainstr + iter->currpos;
        for(i = 0; i < iter->charsize; i++)
        {
            str[i] = pointer[i];
        }
        str[iter->charsize] = '\0';
        return str;
    }

    /* returns the number of bytes contained in a unicode character */
    int nn_util_utf8numbytes(int value)
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

    char* nn_util_utf8encode(unsigned int code, size_t* dlen)
    {
        int count;
        char* chars;
        *dlen = 0;
        count = nn_util_utf8numbytes((int)code);
        if(nn_util_likely(count > 0))
        {
            *dlen = count;
            chars = (char*)nn_memory_malloc(sizeof(char) * ((size_t)count + 1));
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

    int nn_util_utf8decode(const uint8_t* bytes, uint32_t length)
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

    char* nn_util_utf8codepoint(const char* str, char* outcodepoint)
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

    char* nn_util_utf8strstr(const char* haystack, const char* needle)
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
                haystack = nn_util_utf8codepoint(maybematch, &throwawaycodepoint);
            }
        }
        /* no match */
        return nullptr;
    }

    /*
    // returns a pointer to the beginning of the pos'th utf8 codepoint
    // in the buffer at s
    */
    char* nn_util_utf8index(char* s, int pos)
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
    void nn_util_utf8slice(char* s, int* start, int* end)
    {
        char* p;
        p = nn_util_utf8index(s, *start);
        if(p != nullptr)
        {
            *start = (int)(p - s);
        }
        else
        {
            *start = -1;
        }
        p = nn_util_utf8index(s, *end);
        if(p != nullptr)
        {
            *end = (int)(p - s);
        }
        else
        {
            *end = (int)strlen(s);
        }
    }

    static int g_neon_ttycheck = -1;

    const char* nn_util_color(Color tc)
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

    char* nn_util_strndup(const char* src, size_t len)
    {
        char* buf;
        buf = (char*)nn_memory_malloc(sizeof(char) * (len + 1));
        if(buf == nullptr)
        {
            return nullptr;
        }
        memset(buf, 0, len + 1);
        memcpy(buf, src, len);
        return buf;
    }

    char* nn_util_strdup(const char* src)
    {
        return nn_util_strndup(src, strlen(src));
    }

    void nn_util_mtseed(uint32_t seed, uint32_t* binst, uint32_t* index)
    {
        uint32_t i;
        binst[0] = seed;
        for(i = 1; i < NEON_CONFIG_MTSTATESIZE; i++)
        {
            binst[i] = (uint32_t)(1812433253UL * (binst[i - 1] ^ (binst[i - 1] >> 30)) + i);
        }
        *index = NEON_CONFIG_MTSTATESIZE;
    }

    uint32_t nn_util_mtgenerate(uint32_t* binst, uint32_t* index)
    {
        uint32_t i;
        uint32_t y;
        if(*index >= NEON_CONFIG_MTSTATESIZE)
        {
            for(i = 0; i < NEON_CONFIG_MTSTATESIZE - 397; i++)
            {
                y = (binst[i] & 0x80000000) | (binst[i + 1] & 0x7fffffff);
                binst[i] = binst[i + 397] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
            }
            for(; i < NEON_CONFIG_MTSTATESIZE - 1; i++)
            {
                y = (binst[i] & 0x80000000) | (binst[i + 1] & 0x7fffffff);
                binst[i] = binst[i + (397 - NEON_CONFIG_MTSTATESIZE)] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
            }
            y = (binst[NEON_CONFIG_MTSTATESIZE - 1] & 0x80000000) | (binst[0] & 0x7fffffff);
            binst[NEON_CONFIG_MTSTATESIZE - 1] = binst[396] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
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
        static uint32_t mtstate[NEON_CONFIG_MTSTATESIZE];
        static uint32_t mtindex = NEON_CONFIG_MTSTATESIZE + 1;
        if(mtindex >= NEON_CONFIG_MTSTATESIZE)
        {
            osfn_gettimeofday(&tv, nullptr);
            nn_util_mtseed((uint32_t)(1000000 * tv.tv_sec + tv.tv_usec), mtstate, &mtindex);
        }
        randval = nn_util_mtgenerate(mtstate, &mtindex);
        randnum = lowerlimit + ((double)randval / UINT32_MAX) * (upperlimit - lowerlimit);
        return randnum;
    }

    char* nn_util_filereadhandle(FILE* hnd, size_t* dlen, bool havemaxsz, size_t maxsize)
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
        buf = (char*)nn_memory_malloc(sizeof(char) * (toldlen + 1));
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

    char* nn_util_filereadfile(const char* filename, size_t* dlen, bool havemaxsz, size_t maxsize)
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
        b = nn_util_filereadhandle(fh, dlen, havemaxsz, maxsize);
        fclose(fh);
        return b;
    }

    char* nn_util_filegetshandle(char* s, int size, FILE* f, size_t* lendest)
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

    int nn_util_filegetlinehandle(char** lineptr, size_t* destlen, FILE* hnd)
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
        nn_util_filegetshandle(stackbuf, kInitialStrBufSize, hnd, &getlen);
        heapbuf = strchr(stackbuf, '\n');
        if(heapbuf)
        {
            *heapbuf = '\0';
        }
        linelen = getlen;
        if((linelen + 1) < kInitialStrBufSize)
        {
            heapbuf = (char*)nn_memory_realloc(*lineptr, kInitialStrBufSize);
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

    char* nn_util_strtoupper(char* str, size_t length)
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

    char* nn_util_strtolower(char* str, size_t length)
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

    String* nn_util_numbertobinstring(long n)
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

    String* nn_util_numbertooctstring(int64_t n, bool numeric)
    {
        int length;
        /* assume maximum of 64 bits + 2 octal indicators (0c) */
        char str[66];
        length = sprintf(str, numeric ? "0c%lo" : "%lo", n);
        return String::copy(str, length);
    }

    String* nn_util_numbertohexstring(int64_t n, bool numeric)
    {
        int length;
        /* assume maximum of 64 bits + 2 hex indicators (0x) */
        char str[66];
        length = sprintf(str, numeric ? "0x%lx" : "%lx", n);
        return String::copy(str, length);
    }

    uint32_t nn_object_hashobject(Object* object)
    {
        switch(object->m_objtype)
        {
            case Object::OTYP_CLASS:
            {
                /* Classes just use their name. */
                return ((Class*)object)->m_classname->m_hashvalue;
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
                Function* fn;
                fn = (Function*)object;
                tmpptr = (uint32_t)(uintptr_t)fn;
    #if 1
                tmpa = nn_util_hashdouble(fn->m_fnvals.fnscriptfunc.arity);
                tmpb = nn_util_hashdouble(fn->m_fnvals.fnscriptfunc.blob->count);
                tmpres = tmpa ^ tmpb;
                tmpres = tmpres ^ tmpptr;
    #else
                tmpres = tmpptr;
    #endif
                return tmpres;
            }
            break;
            case Object::OTYP_STRING:
            {
                return ((String*)object)->m_hashvalue;
            }
            break;
            default:
                break;
        }
        return 0;
    }

    uint32_t nn_value_hashvalue(Value value)
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
            return nn_util_hashdouble(value.asNumber());
        }
        else if(value.isObject())
        {
            return nn_object_hashobject(value.asObject());
        }
        return 0;
    }


    void nn_valtable_removewhites(HashTable<Value, Value>* table)
    {
        int i;
        auto gcs = SharedState::get();
        for(i = 0; i < table->htcapacity; i++)
        {
            auto entry = &table->htentries[i];
            if(entry->key.isObject() && entry->key.asObject()->m_objmark != gcs->markvalue)
            {
                table->remove(entry->key);
            }
        }
    }

    void nn_valtable_mark(HashTable<Value, Value>* table)
    {
        int i;
        NN_NULLPTRCHECK_RETURN(table);
        if(table == nullptr)
        {
            return;
        }
        if(!table->htactive)
        {
            // nn_state_warn("trying to mark inactive hashtable <%p>!", table);
            return;
        }
        for(i = 0; i < table->htcapacity; i++)
        {
            auto entry = &table->htentries[i];
            if(entry != nullptr)
            {
                if(!entry->key.isNull())
                {
                    nn_gcmem_markvalue(entry->key);
                    nn_gcmem_markvalue(entry->value.value);
                }
            }
        }
    }

    /*
     * TODO: get rid of unused functions
     */

    double nn_string_tabhashvaluecombine(const char* data, size_t len, uint32_t hsv)
    {
        double dn;
        dn = 0;
        dn += hsv;
        dn += len;
        dn += data[0];
        return dn;
    }

    void nn_dbg_disasmblob(IOStream* pr, Blob* blob, const char* name)
    {
        int offset;
        pr->format("== compiled '%s' [[\n", name);
        for(offset = 0; offset < blob->count;)
        {
            offset = nn_dbg_printinstructionat(pr, blob, offset);
        }
        pr->format("]]\n");
    }

    void nn_dbg_printinstrname(IOStream* pr, const char* name)
    {
        pr->format("%s%-16s%s ", nn_util_color(NEON_COLOR_RED), name, nn_util_color(NEON_COLOR_RESET));
    }

    int nn_dbg_printsimpleinstr(IOStream* pr, const char* name, int offset)
    {
        nn_dbg_printinstrname(pr, name);
        pr->format("\n");
        return offset + 1;
    }

    int nn_dbg_printconstinstr(IOStream* pr, const char* name, Blob* blob, int offset)
    {
        uint16_t constant;
        constant = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
        nn_dbg_printinstrname(pr, name);
        pr->format("%8d ", constant);
        ValPrinter::printValue(pr, blob->constants.get(constant), true, false);
        pr->format("\n");
        return offset + 3;
    }

    int nn_dbg_printpropertyinstr(IOStream* pr, const char* name, Blob* blob, int offset)
    {
        const char* proptn;
        uint16_t constant;
        constant = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
        nn_dbg_printinstrname(pr, name);
        pr->format("%8d ", constant);
        ValPrinter::printValue(pr, blob->constants.get(constant), true, false);
        proptn = "";
        if(blob->instrucs[offset + 3].code == 1)
        {
            proptn = "static";
        }
        pr->format(" (%s)", proptn);
        pr->format("\n");
        return offset + 4;
    }

    int nn_dbg_printshortinstr(IOStream* pr, const char* name, Blob* blob, int offset)
    {
        uint16_t slot;
        slot = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
        nn_dbg_printinstrname(pr, name);
        pr->format("%8d\n", slot);
        return offset + 3;
    }

    int nn_dbg_printbyteinstr(IOStream* pr, const char* name, Blob* blob, int offset)
    {
        uint16_t slot;
        slot = blob->instrucs[offset + 1].code;
        nn_dbg_printinstrname(pr, name);
        pr->format("%8d\n", slot);
        return offset + 2;
    }

    int nn_dbg_printjumpinstr(IOStream* pr, const char* name, int sign, Blob* blob, int offset)
    {
        uint16_t jump;
        jump = (uint16_t)(blob->instrucs[offset + 1].code << 8);
        jump |= blob->instrucs[offset + 2].code;
        nn_dbg_printinstrname(pr, name);
        pr->format("%8d -> %d\n", offset, offset + 3 + sign * jump);
        return offset + 3;
    }

    int nn_dbg_printtryinstr(IOStream* pr, const char* name, Blob* blob, int offset)
    {
        uint16_t finally;
        uint16_t type;
        uint16_t address;
        type = (uint16_t)(blob->instrucs[offset + 1].code << 8);
        type |= blob->instrucs[offset + 2].code;
        address = (uint16_t)(blob->instrucs[offset + 3].code << 8);
        address |= blob->instrucs[offset + 4].code;
        finally = (uint16_t)(blob->instrucs[offset + 5].code << 8);
        finally |= blob->instrucs[offset + 6].code;
        nn_dbg_printinstrname(pr, name);
        pr->format("%8d -> %d, %d\n", type, address, finally);
        return offset + 7;
    }

    int nn_dbg_printinvokeinstr(IOStream* pr, const char* name, Blob* blob, int offset)
    {
        uint16_t constant;
        uint16_t argcount;
        constant = (uint16_t)(blob->instrucs[offset + 1].code << 8);
        constant |= blob->instrucs[offset + 2].code;
        argcount = blob->instrucs[offset + 3].code;
        nn_dbg_printinstrname(pr, name);
        pr->format("(%d args) %8d ", argcount, constant);
        ValPrinter::printValue(pr, blob->constants.get(constant), true, false);
        pr->format("\n");
        return offset + 4;
    }

    const char* nn_dbg_op2str(uint16_t instruc)
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
            case Instruction::OPC_IMPORTIMPORT:
                return "OPC_IMPORTIMPORT";
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

    int nn_dbg_printclosureinstr(IOStream* pr, const char* name, Blob* blob, int offset)
    {
        int j;
        int islocal;
        uint16_t index;
        uint16_t constant;
        const char* locn;
        Function* function;
        offset++;
        constant = blob->instrucs[offset++].code << 8;
        constant |= blob->instrucs[offset++].code;
        pr->format("%-16s %8d ", name, constant);
        ValPrinter::printValue(pr, blob->constants.get(constant), true, false);
        pr->format("\n");
        function = blob->constants.get(constant).asFunction();
        for(j = 0; j < function->upvalcount; j++)
        {
            islocal = blob->instrucs[offset++].code;
            index = blob->instrucs[offset++].code << 8;
            index |= blob->instrucs[offset++].code;
            locn = "upvalue";
            if(islocal)
            {
                locn = "local";
            }
            pr->format("%04d      |                     %s %d\n", offset - 3, locn, (int)index);
        }
        return offset;
    }

    int nn_dbg_printinstructionat(IOStream* pr, Blob* blob, int offset)
    {
        uint16_t instruction;
        const char* opname;
        pr->format("%08d ", offset);
        if(offset > 0 && blob->instrucs[offset].fromsourceline == blob->instrucs[offset - 1].fromsourceline)
        {
            pr->format("       | ");
        }
        else
        {
            pr->format("%8d ", blob->instrucs[offset].fromsourceline);
        }
        instruction = blob->instrucs[offset].code;
        opname = nn_dbg_op2str(instruction);
        switch(instruction)
        {
            case Instruction::OPC_JUMPIFFALSE:
                return nn_dbg_printjumpinstr(pr, opname, 1, blob, offset);
            case Instruction::OPC_JUMPNOW:
                return nn_dbg_printjumpinstr(pr, opname, 1, blob, offset);
            case Instruction::OPC_EXTRY:
                return nn_dbg_printtryinstr(pr, opname, blob, offset);
            case Instruction::OPC_LOOP:
                return nn_dbg_printjumpinstr(pr, opname, -1, blob, offset);
            case Instruction::OPC_GLOBALDEFINE:
                return nn_dbg_printconstinstr(pr, opname, blob, offset);
            case Instruction::OPC_GLOBALGET:
                return nn_dbg_printconstinstr(pr, opname, blob, offset);
            case Instruction::OPC_GLOBALSET:
                return nn_dbg_printconstinstr(pr, opname, blob, offset);
            case Instruction::OPC_LOCALGET:
                return nn_dbg_printshortinstr(pr, opname, blob, offset);
            case Instruction::OPC_LOCALSET:
                return nn_dbg_printshortinstr(pr, opname, blob, offset);
            case Instruction::OPC_FUNCARGOPTIONAL:
                return nn_dbg_printshortinstr(pr, opname, blob, offset);
            case Instruction::OPC_FUNCARGGET:
                return nn_dbg_printshortinstr(pr, opname, blob, offset);
            case Instruction::OPC_FUNCARGSET:
                return nn_dbg_printshortinstr(pr, opname, blob, offset);
            case Instruction::OPC_PROPERTYGET:
                return nn_dbg_printconstinstr(pr, opname, blob, offset);
            case Instruction::OPC_PROPERTYGETSELF:
                return nn_dbg_printconstinstr(pr, opname, blob, offset);
            case Instruction::OPC_PROPERTYSET:
                return nn_dbg_printconstinstr(pr, opname, blob, offset);
            case Instruction::OPC_UPVALUEGET:
                return nn_dbg_printshortinstr(pr, opname, blob, offset);
            case Instruction::OPC_UPVALUESET:
                return nn_dbg_printshortinstr(pr, opname, blob, offset);
            case Instruction::OPC_EXPOPTRY:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_EXPUBLISHTRY:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PUSHCONSTANT:
                return nn_dbg_printconstinstr(pr, opname, blob, offset);
            case Instruction::OPC_EQUAL:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMGREATER:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMLESSTHAN:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PUSHEMPTY:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PUSHNULL:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PUSHTRUE:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PUSHFALSE:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMADD:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMSUBTRACT:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMMULTIPLY:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMDIVIDE:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMMODULO:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMPOW:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMNEGATE:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMNOT:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMBITNOT:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMAND:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMOR:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMBITXOR:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMSHIFTLEFT:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PRIMSHIFTRIGHT:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_PUSHONE:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_IMPORTIMPORT:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_TYPEOF:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_ECHO:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_STRINGIFY:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_EXTHROW:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_POPONE:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_OPINSTANCEOF:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_UPVALUECLOSE:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_DUPONE:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_ASSERT:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_POPN:
                return nn_dbg_printshortinstr(pr, opname, blob, offset);
                /* non-user objects... */
            case Instruction::OPC_SWITCH:
                return nn_dbg_printshortinstr(pr, opname, blob, offset);
                /* data container manipulators */
            case Instruction::OPC_MAKERANGE:
                return nn_dbg_printshortinstr(pr, opname, blob, offset);
            case Instruction::OPC_MAKEARRAY:
                return nn_dbg_printshortinstr(pr, opname, blob, offset);
            case Instruction::OPC_MAKEDICT:
                return nn_dbg_printshortinstr(pr, opname, blob, offset);
            case Instruction::OPC_INDEXGET:
                return nn_dbg_printbyteinstr(pr, opname, blob, offset);
            case Instruction::OPC_INDEXGETRANGED:
                return nn_dbg_printbyteinstr(pr, opname, blob, offset);
            case Instruction::OPC_INDEXSET:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_MAKECLOSURE:
                return nn_dbg_printclosureinstr(pr, opname, blob, offset);
            case Instruction::OPC_CALLFUNCTION:
                return nn_dbg_printbyteinstr(pr, opname, blob, offset);
            case Instruction::OPC_CALLMETHOD:
                return nn_dbg_printinvokeinstr(pr, opname, blob, offset);
            case Instruction::OPC_CLASSINVOKETHIS:
                return nn_dbg_printinvokeinstr(pr, opname, blob, offset);
            case Instruction::OPC_RETURN:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_CLASSGETTHIS:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_MAKECLASS:
                return nn_dbg_printconstinstr(pr, opname, blob, offset);
            case Instruction::OPC_MAKEMETHOD:
                return nn_dbg_printconstinstr(pr, opname, blob, offset);
            case Instruction::OPC_CLASSPROPERTYDEFINE:
                return nn_dbg_printpropertyinstr(pr, opname, blob, offset);
            case Instruction::OPC_CLASSGETSUPER:
                return nn_dbg_printconstinstr(pr, opname, blob, offset);
            case Instruction::OPC_CLASSINHERIT:
                return nn_dbg_printsimpleinstr(pr, opname, offset);
            case Instruction::OPC_CLASSINVOKESUPER:
                return nn_dbg_printinvokeinstr(pr, opname, blob, offset);
            case Instruction::OPC_CLASSINVOKESUPERSELF:
                return nn_dbg_printbyteinstr(pr, opname, blob, offset);
            case Instruction::OPC_HALT:
                return nn_dbg_printbyteinstr(pr, opname, blob, offset);
            default:
            {
                pr->format("unknown opcode %d\n", instruction);
            }
            break;
        }
        return offset + 1;
    }

    Value nn_value_copystrlen(const char* str, size_t len)
    {
        return Value::fromObject(String::copy(str, len));
    }

    Value nn_value_copystr(const char* str)
    {
        return nn_value_copystrlen(str, strlen(str));
    }

    String* nn_value_tostring(Value value)
    {
        IOStream pr;
        String* s;
        IOStream::makeStackString(&pr);
        ValPrinter::printValue(&pr, value, false, true);
        s = pr.takeString();
        return s;
    }

    const char* nn_value_objecttypename(Object* object, bool detailed)
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
                klassname = inst->m_instanceclass->m_classname->data();
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

    const char* nn_value_typename(Value value, bool detailed)
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
            return nn_value_objecttypename(value.asObject(), detailed);
        }
        return "?unknown?";
    }

    bool nn_value_compobjarray(Object* oa, Object* ob)
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
            if(!nn_value_compare(arra->get(i), arrb->get(i)))
            {
                return false;
            }
        }
        return true;
    }

    bool nn_value_compobjstring(Object* oa, Object* ob)
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

    bool nn_value_compobjdict(Object* oa, Object* ob)
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
        lena = dicta->htnames.count();
        lenb = dictb->htnames.count();
        if(lena != lenb)
        {
            return false;
        }
        ai = 0;
        while(ai < lena)
        {
            /* first, get the key name off of dicta ... */
            keya = dicta->htnames.get(ai);
            fielda = dicta->htab.getfield(dicta->htnames.get(ai));
            if(fielda != nullptr)
            {
                /* then look up that key in dictb ... */
                fieldb = dictb->get(keya);
                if((fielda != nullptr) && (fieldb != nullptr))
                {
                    /* if it exists, compare their values */
                    if(!nn_value_compare(fielda->value, fieldb->value))
                    {
                        return false;
                    }
                }
            }
            ai++;
        }
        return true;
    }

    bool nn_value_compobject(Value a, Value b)
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
                return nn_value_compobjstring(oa, ob);
            }
            else if(ta == Object::OTYP_ARRAY)
            {
                return nn_value_compobjarray(oa, ob);
            }
            else if(ta == Object::OTYP_DICT)
            {
                return nn_value_compobjdict(oa, ob);
            }
        }
        return false;
    }

    bool nn_value_compare_actual(Value a, Value b)
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
                return nn_value_compobject(a, b);
            }
        }

        return false;
    }

    bool nn_value_compare(Value a, Value b)
    {
        bool r;
        r = nn_value_compare_actual(a, b);
        return r;
    }

    /**
     * returns the greater of the two values.
     * this function encapsulates the object hierarchy
     */
    Value nn_value_findgreater(Value a, Value b)
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
                if(a.asRange()->lower >= b.asRange()->lower)
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
                if(a.asDict()->htnames.count() >= b.asDict()->htnames.count())
                {
                    return a;
                }
                return b;
            }
            else if(a.isFile() && b.isFile())
            {
                if(strcmp(a.asFile()->path->data(), b.asFile()->path->data()) >= 0)
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
    void nn_value_sortvalues(Value* values, int count)
    {
        int i;
        int j;
        Value temp;
        for(i = 0; i < count; i++)
        {
            for(j = 0; j < count; j++)
            {
                if(nn_value_compare(values[j], nn_value_findgreater(values[i], values[j])))
                {
                    temp = values[i];
                    values[i] = values[j];
                    values[j] = temp;
                    if(values[i].isArray())
                    {
                        nn_value_sortvalues((Value*)values[i].asArray()->data(), values[i].asArray()->count());
                    }

                    if(values[j].isArray())
                    {
                        nn_value_sortvalues((Value*)values[j].asArray()->data(), values[j].asArray()->count());
                    }
                }
            }
        }
    }

    Value nn_value_copyvalue(Value value)
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


    Upvalue* nn_object_makeupvalue(Value* slot, int stackpos)
    {
        Upvalue* upvalue;
        upvalue = SharedState::gcMakeObject<Upvalue>(Object::OTYP_UPVALUE, false);
        upvalue->closed = Value::makeNull();
        upvalue->location = *slot;
        upvalue->next = nullptr;
        upvalue->stackpos = stackpos;
        return upvalue;
    }

    Userdata* nn_object_makeuserdata(void* pointer, const char* name)
    {
        Userdata* ptr;
        ptr = SharedState::gcMakeObject<Userdata>(Object::OTYP_USERDATA, false);
        ptr->pointer = pointer;
        ptr->m_udname = nn_util_strdup(name);
        ptr->ondestroyfn = nullptr;
        return ptr;
    }



    Switch* nn_object_makeswitch()
    {
        Switch* sw;
        sw = SharedState::gcMakeObject<Switch>(Object::OTYP_SWITCH, false);
        sw->table.initTable();
        sw->defaultjump = -1;
        sw->exitjump = -1;
        return sw;
    }

    Range* nn_object_makerange(int lower, int upper)
    {
        Range* range;
        range = SharedState::gcMakeObject<Range>(Object::OTYP_RANGE, false);
        range->lower = lower;
        range->upper = upper;
        if(upper > lower)
        {
            range->range = upper - lower;
        }
        else
        {
            range->range = lower - upper;
        }
        return range;
    }


    void nn_argcheck_init(ArgCheck* ch, const char* name, const FuncContext& scfn)
    {
        ch->m_scriptfnctx = scfn;
        ch->m_argcheckfuncname = name;
    }

    template<typename... ArgsT>
    Value nn_argcheck_fail(ArgCheck* ch, const char* srcfile, int srcline, const char* fmt, ArgsT&&... args)
    {
        auto gcs = SharedState::get();
        (void)ch;
    #if 0
            gcs->stackPop(ch->m_scriptfnctx.argc);
    #endif
        if(!throwScriptException(gcs->m_exceptions.argumenterror, srcfile, srcline, fmt, args...))
        {
        }
        return Value::makeBool(false);
    }

    void nn_state_initbuiltinmethods()
    {
        nn_state_installobjectprocess();
        nn_state_installobjectobject();
        nn_state_installobjectnumber();
        nn_state_installobjectstring();
        nn_state_installobjectarray();
        nn_state_installobjectdict();
        nn_state_installobjectfile();
        nn_state_installobjectrange();
        nn_state_installmodmath();
    }

    /**
     * see @nn_state_warn
     */
    template<typename... ArgsT>
    void nn_state_warn(const char* fmt, ArgsT&&... args)
    {
        auto gcs = SharedState::get();
        if(gcs->m_conf.enablewarnings)
        {
            fprintf(stderr, "WARNING: ");
            fprintf(stderr, fmt, args...);
            fprintf(stderr, "\n");
        }
    }


    /**
     * procuce a stacktrace array; it is an object array, because it used both internally and in scripts.
     * cannot take any shortcuts here.
     */
    Value nn_except_getstacktrace()
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
        auto gcs = SharedState::get();
        oa = Array::make();
        {
            for(i = 0; i < gcs->m_vmstate.framecount; i++)
            {
                IOStream::makeStackString(&pr);
                frame = &gcs->m_vmstate.framevalues[i];
                function = frame->closure->m_fnvals.fnclosure.scriptfunc;
                /* -1 because the IP is sitting on the next instruction to be executed */
                instruction = frame->inscode - function->m_fnvals.fnscriptfunc.blob->instrucs - 1;
                line = function->m_fnvals.fnscriptfunc.blob->instrucs[instruction].fromsourceline;
                physfile = "(unknown)";
                if(function->m_fnvals.fnscriptfunc.module->physicalpath != nullptr)
                {
                    physfile = function->m_fnvals.fnscriptfunc.module->physicalpath->data();
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
                if((i > 15) && (gcs->m_conf.showfullstack == false))
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
    bool nn_except_propagate()
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
        auto gcs = SharedState::get();
        exception = nn_vm_stackpeek(0).asInstance();
        /* look for a handler .... */
        while(gcs->m_vmstate.framecount > 0)
        {
            gcs->m_vmstate.currentframe = &gcs->m_vmstate.framevalues[gcs->m_vmstate.framecount - 1];
            for(i = gcs->m_vmstate.currentframe->m_handlercount; i > 0; i--)
            {
                handler = &gcs->m_vmstate.currentframe->handlers[i - 1];
                function = gcs->m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc;
                if(handler->address != 0 /*&& nn_util_isinstanceof(exception->m_instanceclass, handler->handlerklass)*/)
                {
                    gcs->m_vmstate.currentframe->inscode = &function->m_fnvals.fnscriptfunc.blob->instrucs[handler->address];
                    return true;
                }
                else if(handler->finallyaddress != 0)
                {
                    /* continue propagating once the 'finally' block completes */
                    gcs->stackPush(Value::makeBool(true));
                    gcs->m_vmstate.currentframe->inscode = &function->m_fnvals.fnscriptfunc.blob->instrucs[handler->finallyaddress];
                    return true;
                }
            }
            gcs->m_vmstate.framecount--;
        }
        /* at this point, the exception is unhandled; so, print it out. */
        colred = nn_util_color(NEON_COLOR_RED);
        colblue = nn_util_color(NEON_COLOR_BLUE);
        colreset = nn_util_color(NEON_COLOR_RESET);
        colyellow = nn_util_color(NEON_COLOR_YELLOW);
        gcs->m_debugwriter->format("%sunhandled %s%s", colred, exception->m_instanceclass->m_classname->data(), colreset);
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
        gcs->m_debugwriter->format(" [from native %s%s:%d%s]", colyellow, srcfile, srcline, colreset);
        field = exception->m_instanceprops.getfieldbycstr("message");
        if(field != nullptr)
        {
            emsg = nn_value_tostring(field->value);
            if(emsg->length() > 0)
            {
                gcs->m_debugwriter->format( ": %s", emsg->data());
            }
            else
            {
                gcs->m_debugwriter->format(":");
            }
        }
        gcs->m_debugwriter->format("\n");
        field = exception->m_instanceprops.getfieldbycstr("stacktrace");
        if(field != nullptr)
        {
            gcs->m_debugwriter->format("%sstacktrace%s:\n", colblue, colreset);
            oa = field->value.asArray();
            cnt = oa->count();
            i = cnt - 1;
            if(cnt > 0)
            {
                while(true)
                {
                    stackitm = oa->get(i);
                    gcs->m_debugwriter->format("%s", colyellow);
                    gcs->m_debugwriter->format("  ");
                    ValPrinter::printValue(gcs->m_debugwriter, stackitm, false, true);
                    gcs->m_debugwriter->format("%s\n", colreset);
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
     * push an exception handler, assuming it does not exceed NEON_CONFIG_MAXEXCEPTHANDLERS.
     */
    bool nn_except_pushhandler(Class* type, int address, int finallyaddress)
    {
        CallFrame* frame;
        (void)type;
        auto gcs = SharedState::get();
        frame = &gcs->m_vmstate.framevalues[gcs->m_vmstate.framecount - 1];
        if(frame->m_handlercount == NEON_CONFIG_MAXEXCEPTHANDLERS)
        {
            nn_state_raisefatalerror("too many nested exception handlers in one function");
            return false;
        }
        frame->handlers[frame->m_handlercount].address = address;
        frame->handlers[frame->m_handlercount].finallyaddress = finallyaddress;
        /*frame->handlers[frame->m_handlercount].handlerklass = type;*/
        frame->m_handlercount++;
        return true;
    }



    /**
     * generate bytecode for a nativee exception class.
     * script-side it is enough to just derive from Exception, of course.
     */
    Class* nn_except_makeclass(Class* baseclass, Module* module, const char* cstrname, bool iscs)
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
        gcs->stackPush(Value::fromObject(classname));
        klass = Class::make(classname, baseclass);
        gcs->stackPop();
        gcs->stackPush(Value::fromObject(klass));
        function = Function::makeFuncScript(module, Function::CTXTYPE_METHOD);
        function->m_fnvals.fnscriptfunc.arity = 1;
        function->m_fnvals.fnscriptfunc.isvariadic = false;
        gcs->stackPush(Value::fromObject(function));
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
        gcs->stackPop();
        /* set class constructor */
        gcs->stackPush(Value::fromObject(closure));
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
        gcs->stackPop();
        gcs->stackPop();
        /* assert error name */
        /* gcs->stackPop(); */
        return klass;
    }

    /**
     * create an instance of an exception class.
     */
    Instance* nn_except_makeinstance(Class* exklass, const char* srcfile, int srcline, String* message)
    {
        Instance* instance;
        String* osfile;
        auto gcs = SharedState::get();
        instance = Instance::make(exklass);
        osfile = String::copy(srcfile);
        gcs->stackPush(Value::fromObject(instance));
        instance->defProperty(String::intern("class"), Value::fromObject(exklass));
        instance->defProperty(String::intern("message"), Value::fromObject(message));
        instance->defProperty(String::intern("srcfile"), Value::fromObject(osfile));
        instance->defProperty(String::intern("srcline"), Value::makeNumber(srcline));
        gcs->stackPop();
        return instance;
    }



    bool nn_state_defglobalvalue(const char* name, Value val)
    {
        bool r;
        String* oname;
        oname = String::intern(name);
        auto gcs = SharedState::get();
        gcs->stackPush(Value::fromObject(oname));
        gcs->stackPush(val);
        r = gcs->m_declaredglobals.set(gcs->m_vmstate.stackvalues[0], gcs->m_vmstate.stackvalues[1]);
        gcs->stackPop(2);
        return r;
    }

    bool nn_state_defnativefunctionptr(const char* name, NativeFN fptr, void* uptr)
    {
        Function* func;
        func = Function::makeFuncNative(fptr, name, uptr);
        return nn_state_defglobalvalue(name, Value::fromObject(func));
    }

    bool nn_state_defnativefunction(const char* name, NativeFN fptr)
    {
        return nn_state_defnativefunctionptr(name, fptr, nullptr);
    }

    Class* nn_util_makeclass(const char* name, Class* parent)
    {
        Class* cl;
        String* os;
        auto gcs = SharedState::get();
        os = String::copy(name);
        cl = Class::make(os, parent);
        gcs->m_declaredglobals.set(Value::fromObject(os), Value::fromObject(cl));
        return cl;
    }


    bool nn_util_methodisprivate(String* name)
    {
        return name->length() > 0 && name->data()[0] == '_';
    }


    /*
     * $keeplast: whether to emit code that retains or discards the value of the last statement/expression.
     * SHOULD NOT BE USED FOR ORDINARY SCRIPTS as it will almost definitely result in the stack containing invalid values.
     */
    Function* nn_astutil_compilesource(Module* module, const char* source, Blob* blob, bool fromimport, bool keeplast)
    {
        AstLexer* lexer;
        AstParser* parser;
        Function* function;
        (void)blob;
        lexer = AstLexer::make(source);
        parser = AstParser::make(lexer, module, keeplast);
        AstParser::FuncCompiler fnc(parser, Function::CTXTYPE_SCRIPT, true);
        fnc.m_fromimport = fromimport;
        parser->runparser();
        function = parser->endcompiler(true);
        if(parser->m_haderror)
        {
            function = nullptr;
        }
        AstLexer::destroy(lexer);
        AstParser::destroy(parser);
        return function;
    }


    static Value nn_objfnarray_constructor(const FuncContext& scfn)
    {
        int cnt;
        Value filler;
        Array* arr;
        ArgCheck check;
        nn_argcheck_init(&check, "constructor", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        filler = Value::makeNull();
        if(scfn.argc > 1)
        {
            filler = scfn.argv[1];
        }
        cnt = scfn.argv[0].asNumber();
        arr = Array::makeFilled(cnt, filler);
        return Value::fromObject(arr);
    }

    static Value nn_objfnarray_join(const FuncContext& scfn)
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
                joinee = nn_value_tostring(vjoinee);
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

    static Value nn_objfnarray_length(const FuncContext& scfn)
    {
        Array* selfarr;
        ArgCheck check;
        nn_argcheck_init(&check, "length", scfn);
        selfarr = scfn.thisval.asArray();
        return Value::makeNumber(selfarr->count());
    }

    static Value nn_objfnarray_append(const FuncContext& scfn)
    {
        size_t i;
        for(i = 0; i < scfn.argc; i++)
        {
            scfn.thisval.asArray()->push(scfn.argv[i]);
        }
        return Value::makeNull();
    }

    static Value nn_objfnarray_clear(const FuncContext& scfn)
    {
        ArgCheck check;
        nn_argcheck_init(&check, "clear", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        scfn.thisval.asArray()->m_objvarray.deInit();
        return Value::makeNull();
    }

    static Value nn_objfnarray_clone(const FuncContext& scfn)
    {
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "clone", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        list = scfn.thisval.asArray();
        return Value::fromObject(list->copy(0, list->count()));
    }

    static Value nn_objfnarray_count(const FuncContext& scfn)
    {
        size_t i;
        int count;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "count", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        list = scfn.thisval.asArray();
        count = 0;
        for(i = 0; i < list->count(); i++)
        {
            if(nn_value_compare(list->get(i), scfn.argv[0]))
            {
                count++;
            }
        }
        return Value::makeNumber(count);
    }

    static Value nn_objfnarray_extend(const FuncContext& scfn)
    {
        size_t i;
        Array* list;
        Array* list2;
        ArgCheck check;
        nn_argcheck_init(&check, "extend", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isarray);
        list = scfn.thisval.asArray();
        list2 = scfn.argv[0].asArray();
        for(i = 0; i < list2->count(); i++)
        {
            list->push(list2->get(i));
        }
        return Value::makeNull();
    }

    static Value nn_objfnarray_indexof(const FuncContext& scfn)
    {
        size_t i;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "indexOf", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
        list = scfn.thisval.asArray();
        i = 0;
        if(scfn.argc == 2)
        {
            NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
            i = scfn.argv[1].asNumber();
        }
        for(; i < list->count(); i++)
        {
            if(nn_value_compare(list->get(i), scfn.argv[0]))
            {
                return Value::makeNumber(i);
            }
        }
        return Value::makeNumber(-1);
    }

    static Value nn_objfnarray_insert(const FuncContext& scfn)
    {
        //! FIXME
        /*
        int index;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "insert", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 2);
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        list = scfn.thisval.asArray();
        index = (int)scfn.argv[1].asNumber();
        list->m_objvarray.insert(scfn.argv[0], index);
        */
        return Value::makeNull();
    }

    static Value nn_objfnarray_pop(const FuncContext& scfn)
    {
        Value value;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "pop", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        list = scfn.thisval.asArray();
        if(list->pop(&value))
        {
            return value;
        }
        return Value::makeNull();
    }

    static Value nn_objfnarray_shift(const FuncContext& scfn)
    {
        ///FIXME
        /*
        size_t i;
        size_t j;
        size_t count;
        Array* list;
        Array* newlist;
        ArgCheck check;
        nn_argcheck_init(&check, "shift", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
        count = 1;
        if(scfn.argc == 1)
        {
            NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
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

    static Value nn_objfnarray_removeat(const FuncContext& scfn)
    {
        size_t i;
        size_t index;
        Value value;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "removeAt", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
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

    static Value nn_objfnarray_remove(const FuncContext& scfn)
    {
        size_t i;
        size_t index;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "remove", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        list = scfn.thisval.asArray();
        index = -1;
        for(i = 0; i < list->count(); i++)
        {
            if(nn_value_compare(list->get(i), scfn.argv[0]))
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

    static Value nn_objfnarray_reverse(const FuncContext& scfn)
    {
        int fromtop;
        Array* list;
        Array* nlist;
        ArgCheck check;
        nn_argcheck_init(&check, "reverse", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        list = scfn.thisval.asArray();
        nlist = (Array*)nn_gcmem_protect((Object*)Array::make());
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

    static Value nn_objfnarray_sort(const FuncContext& scfn)
    {
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "sort", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        list = scfn.thisval.asArray();
        nn_value_sortvalues((Value*)list->data(), list->count());
        return Value::makeNull();
    }

    static Value nn_objfnarray_contains(const FuncContext& scfn)
    {
        size_t i;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "contains", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        list = scfn.thisval.asArray();
        for(i = 0; i < list->count(); i++)
        {
            if(nn_value_compare(scfn.argv[0], list->get(i)))
            {
                return Value::makeBool(true);
            }
        }
        return Value::makeBool(false);
    }

    static Value nn_objfnarray_delete(const FuncContext& scfn)
    {
        //! FIXME
        /*
        size_t i;
        size_t idxupper;
        size_t idxlower;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "delete", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        idxlower = scfn.argv[0].asNumber();
        idxupper = idxlower;
        if(scfn.argc == 2)
        {
            NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
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

    static Value nn_objfnarray_first(const FuncContext& scfn)
    {
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "first", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        list = scfn.thisval.asArray();
        if(list->count() > 0)
        {
            return list->get(0);
        }
        return Value::makeNull();
    }

    static Value nn_objfnarray_last(const FuncContext& scfn)
    {
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "last", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        list = scfn.thisval.asArray();
        if(list->count() > 0)
        {
            return list->get(list->count() - 1);
        }
        return Value::makeNull();
    }

    static Value nn_objfnarray_isempty(const FuncContext& scfn)
    {
        ArgCheck check;
        nn_argcheck_init(&check, "isEmpty", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        return Value::makeBool(scfn.thisval.asArray()->count() == 0);
    }

    static Value nn_objfnarray_take(const FuncContext& scfn)
    {
        size_t count;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "take", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
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

    static Value nn_objfnarray_get(const FuncContext& scfn)
    {
        size_t index;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "get", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        list = scfn.thisval.asArray();
        index = scfn.argv[0].asNumber();
        if((int)index < 0 || index >= list->count())
        {
            return Value::makeNull();
        }
        return list->get(index);
    }

    static Value nn_objfnarray_compact(const FuncContext& scfn)
    {
        size_t i;
        Array* list;
        Array* newlist;
        ArgCheck check;
        nn_argcheck_init(&check, "compact", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        list = scfn.thisval.asArray();
        newlist = (Array*)nn_gcmem_protect((Object*)Array::make());
        for(i = 0; i < list->count(); i++)
        {
            if(!nn_value_compare(list->get(i), Value::makeNull()))
            {
                newlist->push(list->get(i));
            }
        }
        return Value::fromObject(newlist);
    }

    static Value nn_objfnarray_unique(const FuncContext& scfn)
    {
        size_t i;
        size_t j;
        bool found;
        Array* list;
        Array* newlist;
        ArgCheck check;
        nn_argcheck_init(&check, "unique", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        list = scfn.thisval.asArray();
        newlist = (Array*)nn_gcmem_protect((Object*)Array::make());
        for(i = 0; i < list->count(); i++)
        {
            found = false;
            for(j = 0; j < newlist->count(); j++)
            {
                if(nn_value_compare(newlist->get(j), list->get(i)))
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

    static Value nn_objfnarray_zip(const FuncContext& scfn)
    {
        size_t i;
        size_t j;
        Array* list;
        Array* newlist;
        Array* alist;
        Array** arglist;
        ArgCheck check;
        nn_argcheck_init(&check, "zip", scfn);
        list = scfn.thisval.asArray();
        newlist = (Array*)nn_gcmem_protect((Object*)Array::make());
        arglist = (Array**)SharedState::gcAllocate(sizeof(Array*), scfn.argc, false);
        for(i = 0; i < scfn.argc; i++)
        {
            NEON_ARGS_CHECKTYPE(&check, i, nn_value_isarray);
            arglist[i] = scfn.argv[i].asArray();
        }
        for(i = 0; i < list->count(); i++)
        {
            alist = (Array*)nn_gcmem_protect((Object*)Array::make());
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

    static Value nn_objfnarray_zipfrom(const FuncContext& scfn)
    {
        size_t i;
        size_t j;
        Array* list;
        Array* newlist;
        Array* alist;
        Array* arglist;
        ArgCheck check;
        nn_argcheck_init(&check, "zipFrom", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isarray);
        list = scfn.thisval.asArray();
        newlist = (Array*)nn_gcmem_protect((Object*)Array::make());
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
            alist = (Array*)nn_gcmem_protect((Object*)Array::make());
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

    static Value nn_objfnarray_todict(const FuncContext& scfn)
    {
        size_t i;
        Dict* dict;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "toDict", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        dict = (Dict*)nn_gcmem_protect((Object*)Dict::make());
        list = scfn.thisval.asArray();
        for(i = 0; i < list->count(); i++)
        {
            dict->set(Value::makeNumber(i), list->get(i));
        }
        return Value::fromObject(dict);
    }

    static Value nn_objfnarray_iter(const FuncContext& scfn)
    {
        size_t index;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "iter", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        list = scfn.thisval.asArray();
        index = scfn.argv[0].asNumber();
        if(((int)index > -1) && index < list->count())
        {
            return list->get(index);
        }
        return Value::makeNull();
    }

    static Value nn_objfnarray_itern(const FuncContext& scfn)
    {
        size_t index;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "itern", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
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

    static Value nn_objfnarray_each(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        Value callable;
        Value unused;
        Array* list;
        ArgCheck check;
        Value nestargs[3];
        nn_argcheck_init(&check, "each", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
        list = scfn.thisval.asArray();
        callable = scfn.argv[0];
        arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
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
            nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &unused, false);
        }
        return Value::makeNull();
    }

    static Value nn_objfnarray_map(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        Value res;
        Value callable;
        Array* list;
        Array* resultlist;
        ArgCheck check;
        Value nestargs[3];
        nn_argcheck_init(&check, "map", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
        list = scfn.thisval.asArray();
        callable = scfn.argv[0];
        arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
        resultlist = (Array*)nn_gcmem_protect((Object*)Array::make());
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
                nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &res, false);
                resultlist->push(res);
            }
            else
            {
                resultlist->push(Value::makeNull());
            }
        }
        return Value::fromObject(resultlist);
    }

    static Value nn_objfnarray_filter(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        Value callable;
        Value result;
        Array* list;
        Array* resultlist;
        ArgCheck check;
        Value nestargs[3];
        nn_argcheck_init(&check, "filter", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
        list = scfn.thisval.asArray();
        callable = scfn.argv[0];
        arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
        resultlist = (Array*)nn_gcmem_protect((Object*)Array::make());
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
                nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &result, false);
                if(!result.isFalse())
                {
                    resultlist->push(list->get(i));
                }
            }
        }
        return Value::fromObject(resultlist);
    }

    static Value nn_objfnarray_some(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        Value callable;
        Value result;
        Array* list;
        ArgCheck check;
        Value nestargs[3];
        nn_argcheck_init(&check, "some", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
        list = scfn.thisval.asArray();
        callable = scfn.argv[0];
        arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
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
                nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &result, false);
                if(!result.isFalse())
                {
                    return Value::makeBool(true);
                }
            }
        }
        return Value::makeBool(false);
    }

    static Value nn_objfnarray_every(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        Value result;
        Value callable;
        Array* list;
        ArgCheck check;
        Value nestargs[3];
        nn_argcheck_init(&check, "every", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
        list = scfn.thisval.asArray();
        callable = scfn.argv[0];
        arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
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
                nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &result, false);
                if(result.isFalse())
                {
                    return Value::makeBool(false);
                }
            }
        }
        return Value::makeBool(true);
    }

    static Value nn_objfnarray_reduce(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        size_t startindex;
        Value callable;
        Value accumulator;
        Array* list;
        ArgCheck check;
        Value nestargs[5];
        nn_argcheck_init(&check, "reduce", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
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
        arity = nn_nestcall_prepare(callable, scfn.thisval, nullptr, 4);
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
                nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &accumulator, false);
            }
        }
        return accumulator;
    }

    static Value nn_objfnarray_slice(const FuncContext& scfn)
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
        ArgCheck check;
        nn_argcheck_init(&check, "slice", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
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

    void nn_state_installobjectarray()
    {
        static Class::ConstItem arraymethods[] = { { "size", nn_objfnarray_length }, { "join", nn_objfnarray_join },       { "append", nn_objfnarray_append }, { "push", nn_objfnarray_append }, { "clear", nn_objfnarray_clear },     { "clone", nn_objfnarray_clone },   { "count", nn_objfnarray_count }, { "extend", nn_objfnarray_extend },   { "indexOf", nn_objfnarray_indexof }, { "insert", nn_objfnarray_insert }, { "pop", nn_objfnarray_pop }, { "shift", nn_objfnarray_shift },   { "removeAt", nn_objfnarray_removeat }, { "remove", nn_objfnarray_remove }, { "reverse", nn_objfnarray_reverse }, { "sort", nn_objfnarray_sort },   { "contains", nn_objfnarray_contains }, { "delete", nn_objfnarray_delete }, { "first", nn_objfnarray_first },
                                                         { "last", nn_objfnarray_last },   { "isEmpty", nn_objfnarray_isempty }, { "take", nn_objfnarray_take },     { "get", nn_objfnarray_get },     { "compact", nn_objfnarray_compact }, { "unique", nn_objfnarray_unique }, { "zip", nn_objfnarray_zip },     { "zipFrom", nn_objfnarray_zipfrom }, { "toDict", nn_objfnarray_todict },   { "each", nn_objfnarray_each },     { "map", nn_objfnarray_map }, { "filter", nn_objfnarray_filter }, { "some", nn_objfnarray_some },         { "every", nn_objfnarray_every },   { "reduce", nn_objfnarray_reduce },   { "slice", nn_objfnarray_slice }, { "@iter", nn_objfnarray_iter },        { "@itern", nn_objfnarray_itern },  { nullptr, nullptr } };
        auto gcs = SharedState::get();
        gcs->m_classprimarray->defNativeConstructor(nn_objfnarray_constructor);
        gcs->m_classprimarray->defCallableField(String::intern("length"), nn_objfnarray_length);
        gcs->m_classprimarray->installMethods(arraymethods);
    }





    static Value nn_objfndict_length(const FuncContext& scfn)
    {
        ArgCheck check;
        nn_argcheck_init(&check, "length", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        return Value::makeNumber(scfn.thisval.asDict()->htnames.count());
    }

    static Value nn_objfndict_add(const FuncContext& scfn)
    {
        Value tempvalue;
        Dict* dict;
        ArgCheck check;
        nn_argcheck_init(&check, "add", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 2);
        dict = scfn.thisval.asDict();
        if(dict->htab.get(scfn.argv[0], &tempvalue))
        {
            NEON_RETURNERROR(scfn, "duplicate key %s at add()", nn_value_tostring(scfn.argv[0])->data());
        }
        dict->add(scfn.argv[0], scfn.argv[1]);
        return Value::makeNull();
    }

    static Value nn_objfndict_set(const FuncContext& scfn)
    {
        Value value;
        Dict* dict;
        ArgCheck check;
        nn_argcheck_init(&check, "set", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 2);
        dict = scfn.thisval.asDict();
        if(!dict->htab.get(scfn.argv[0], &value))
        {
            dict->add(scfn.argv[0], scfn.argv[1]);
        }
        else
        {
            dict->set(scfn.argv[0], scfn.argv[1]);
        }
        return Value::makeNull();
    }

    static Value nn_objfndict_clear(const FuncContext& scfn)
    {
        Dict* dict;
        ArgCheck check;
        nn_argcheck_init(&check, "clear", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        dict = scfn.thisval.asDict();
        dict->htnames.deInit();
        dict->htab.deInit();
        return Value::makeNull();
    }

    static Value nn_objfndict_clone(const FuncContext& scfn)
    {
        size_t i;
        Dict* dict;
        Dict* newdict;
        ArgCheck check;
        auto gcs = SharedState::get();
        nn_argcheck_init(&check, "clone", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        dict = scfn.thisval.asDict();
        newdict = (Dict*)nn_gcmem_protect((Object*)Dict::make());
        if(!dict->htab.copyTo(&newdict->htab, true))
        {
            nn_except_throwclass(gcs->m_exceptions.argumenterror, "failed to copy table");
            return Value::makeNull();
        }
        for(i = 0; i < dict->htnames.count(); i++)
        {
            newdict->htnames.push(dict->htnames.get(i));
        }
        return Value::fromObject(newdict);
    }

    static Value nn_objfndict_compact(const FuncContext& scfn)
    {
        size_t i;
        Dict* dict;
        Dict* newdict;
        Value tmpvalue;
        ArgCheck check;
        nn_argcheck_init(&check, "compact", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        dict = scfn.thisval.asDict();
        newdict = (Dict*)nn_gcmem_protect((Object*)Dict::make());
        tmpvalue = Value::makeNull();
        for(i = 0; i < dict->htnames.count(); i++)
        {
            dict->htab.get(dict->htnames.get(i), &tmpvalue);
            if(!nn_value_compare(tmpvalue, Value::makeNull()))
            {
                newdict->add(dict->htnames.get(i), tmpvalue);
            }
        }
        return Value::fromObject(newdict);
    }

    static Value nn_objfndict_contains(const FuncContext& scfn)
    {
        Value value;
        Dict* dict;
        ArgCheck check;
        nn_argcheck_init(&check, "contains", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        dict = scfn.thisval.asDict();
        return Value::makeBool(dict->htab.get(scfn.argv[0], &value));
    }

    static Value nn_objfndict_extend(const FuncContext& scfn)
    {
        size_t i;
        Value tmp;
        Dict* dict;
        Dict* dictcpy;
        ArgCheck check;
        nn_argcheck_init(&check, "extend", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isdict);
        dict = scfn.thisval.asDict();
        dictcpy = scfn.argv[0].asDict();
        for(i = 0; i < dictcpy->htnames.count(); i++)
        {
            if(!dict->htab.get(dictcpy->htnames.get(i), &tmp))
            {
                dict->htnames.push(dictcpy->htnames.get(i));
            }
        }
        dictcpy->htab.copyTo(&dict->htab, true);
        return Value::makeNull();
    }

    static Value nn_objfndict_get(const FuncContext& scfn)
    {
        Dict* dict;
        Property* field;
        ArgCheck check;
        nn_argcheck_init(&check, "get", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
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

    static Value nn_objfndict_keys(const FuncContext& scfn)
    {
        size_t i;
        Dict* dict;
        Array* list;
        ArgCheck check;
        nn_argcheck_init(&check, "keys", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        dict = scfn.thisval.asDict();
        list = (Array*)nn_gcmem_protect((Object*)Array::make());
        for(i = 0; i < dict->htnames.count(); i++)
        {
            list->push(dict->htnames.get(i));
        }
        return Value::fromObject(list);
    }

    static Value nn_objfndict_values(const FuncContext& scfn)
    {
        size_t i;
        Dict* dict;
        Array* list;
        Property* field;
        ArgCheck check;
        nn_argcheck_init(&check, "values", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        dict = scfn.thisval.asDict();
        list = (Array*)nn_gcmem_protect((Object*)Array::make());
        for(i = 0; i < dict->htnames.count(); i++)
        {
            field = dict->get(dict->htnames.get(i));
            list->push(field->value);
        }
        return Value::fromObject(list);
    }

    static Value nn_objfndict_remove(const FuncContext& scfn)
    {
        size_t i;
        int index;
        Value value;
        Dict* dict;
        ArgCheck check;
        nn_argcheck_init(&check, "remove", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        dict = scfn.thisval.asDict();
        if(dict->htab.get(scfn.argv[0], &value))
        {
            dict->htab.remove(scfn.argv[0]);
            index = -1;
            for(i = 0; i < dict->htnames.count(); i++)
            {
                if(nn_value_compare(dict->htnames.get(i), scfn.argv[0]))
                {
                    index = i;
                    break;
                }
            }
            for(i = index; i < dict->htnames.count(); i++)
            {
                dict->htnames.set(i, dict->htnames.get(i + 1));
            }
            dict->htnames.pop(nullptr);
            return value;
        }
        return Value::makeNull();
    }

    static Value nn_objfndict_isempty(const FuncContext& scfn)
    {
        ArgCheck check;
        nn_argcheck_init(&check, "isempty", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        return Value::makeBool(scfn.thisval.asDict()->htnames.count() == 0);
    }

    static Value nn_objfndict_findkey(const FuncContext& scfn)
    {
        ArgCheck check;
        nn_argcheck_init(&check, "findkey", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        auto ht = scfn.thisval.asDict()->htab;
        return ht.findkey(scfn.argv[0], Value::makeNull());
    }

    static Value nn_objfndict_tolist(const FuncContext& scfn)
    {
        size_t i;
        Array* list;
        Dict* dict;
        Array* namelist;
        Array* valuelist;
        ArgCheck check;
        nn_argcheck_init(&check, "tolist", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        dict = scfn.thisval.asDict();
        namelist = (Array*)nn_gcmem_protect((Object*)Array::make());
        valuelist = (Array*)nn_gcmem_protect((Object*)Array::make());
        for(i = 0; i < dict->htnames.count(); i++)
        {
            namelist->push(dict->htnames.get(i));
            Value value;
            if(dict->htab.get(dict->htnames.get(i), &value))
            {
                valuelist->push(value);
            }
            else
            {
                /* theoretically impossible */
                valuelist->push(Value::makeNull());
            }
        }
        list = (Array*)nn_gcmem_protect((Object*)Array::make());
        list->push(Value::fromObject(namelist));
        list->push(Value::fromObject(valuelist));
        return Value::fromObject(list);
    }

    static Value nn_objfndict_iter(const FuncContext& scfn)
    {
        Value result;
        Dict* dict;
        ArgCheck check;
        nn_argcheck_init(&check, "iter", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        dict = scfn.thisval.asDict();
        if(dict->htab.get(scfn.argv[0], &result))
        {
            return result;
        }
        return Value::makeNull();
    }

    static Value nn_objfndict_itern(const FuncContext& scfn)
    {
        size_t i;
        Dict* dict;
        ArgCheck check;
        nn_argcheck_init(&check, "itern", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        dict = scfn.thisval.asDict();
        if(scfn.argv[0].isNull())
        {
            if(dict->htnames.count() == 0)
            {
                return Value::makeBool(false);
            }
            return dict->htnames.get(0);
        }
        for(i = 0; i < dict->htnames.count(); i++)
        {
            if(nn_value_compare(scfn.argv[0], dict->htnames.get(i)) && (i + 1) < dict->htnames.count())
            {
                return dict->htnames.get(i + 1);
            }
        }
        return Value::makeNull();
    }

    static Value nn_objfndict_each(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        int arity;
        Value value;
        Value callable;
        Value unused;
        Dict* dict;
        ArgCheck check;
        Value nestargs[3];
        nn_argcheck_init(&check, "each", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
        dict = scfn.thisval.asDict();
        callable = scfn.argv[0];
        arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
        value = Value::makeNull();
        for(i = 0; i < dict->htnames.count(); i++)
        {
            passi = 0;
            if(arity > 0)
            {
                dict->htab.get(dict->htnames.get(i), &value);
                passi++;
                nestargs[0] = value;
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = dict->htnames.get(i);
                }
            }
            nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &unused, false);
        }
        return Value::makeNull();
    }

    static Value nn_objfndict_filter(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        int arity;
        Value value;
        Value callable;
        Value result;
        Dict* dict;
        Dict* resultdict;
        ArgCheck check;
        Value nestargs[3];
        nn_argcheck_init(&check, "filter", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
        dict = scfn.thisval.asDict();
        callable = scfn.argv[0];
        arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
        resultdict = (Dict*)nn_gcmem_protect((Object*)Dict::make());
        value = Value::makeNull();
        for(i = 0; i < dict->htnames.count(); i++)
        {
            passi = 0;
            dict->htab.get(dict->htnames.get(i), &value);
            if(arity > 0)
            {
                passi++;
                nestargs[0] = value;
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = dict->htnames.get(i);
                }
            }
            nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &result, false);
            if(!result.isFalse())
            {
                resultdict->add(dict->htnames.get(i), value);
            }
        }
        /* pop the call list */
        return Value::fromObject(resultdict);
    }

    static Value nn_objfndict_some(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        int arity;
        Value result;
        Value value;
        Value callable;
        Dict* dict;
        ArgCheck check;
        Value nestargs[3];
        nn_argcheck_init(&check, "some", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
        dict = scfn.thisval.asDict();
        callable = scfn.argv[0];
        arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
        value = Value::makeNull();
        for(i = 0; i < dict->htnames.count(); i++)
        {
            passi = 0;
            if(arity > 0)
            {
                dict->htab.get(dict->htnames.get(i), &value);
                passi++;
                nestargs[0] = value;
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = dict->htnames.get(i);
                }
            }
            nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &result, false);
            if(!result.isFalse())
            {
                /* pop the call list */
                return Value::makeBool(true);
            }
        }
        /* pop the call list */
        return Value::makeBool(false);
    }

    static Value nn_objfndict_every(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        int arity;
        Value value;
        Value callable;
        Value result;
        Dict* dict;
        ArgCheck check;
        Value nestargs[3];
        nn_argcheck_init(&check, "every", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
        dict = scfn.thisval.asDict();
        callable = scfn.argv[0];
        arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
        value = Value::makeNull();
        for(i = 0; i < dict->htnames.count(); i++)
        {
            passi = 0;
            if(arity > 0)
            {
                dict->htab.get(dict->htnames.get(i), &value);
                passi++;
                nestargs[0] = value;
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = dict->htnames.get(i);
                }
            }
            nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &result, false);
            if(result.isFalse())
            {
                /* pop the call list */
                return Value::makeBool(false);
            }
        }
        return Value::makeBool(true);
    }

    static Value nn_objfndict_reduce(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        int arity;
        int startindex;
        Value value;
        Value callable;
        Value accumulator;
        Dict* dict;
        ArgCheck check;
        Value nestargs[5];
        nn_argcheck_init(&check, "reduce", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
        dict = scfn.thisval.asDict();
        callable = scfn.argv[0];
        startindex = 0;
        accumulator = Value::makeNull();
        if(scfn.argc == 2)
        {
            accumulator = scfn.argv[1];
        }
        if(accumulator.isNull() && dict->htnames.count() > 0)
        {
            dict->htab.get(dict->htnames.get(0), &accumulator);
            startindex = 1;
        }
        arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 4);
        value = Value::makeNull();
        for(i = startindex; i < dict->htnames.count(); i++)
        {
            passi = 0;
            /* only call map for non-empty values in a list. */
            if(!dict->htnames.get(i).isNull() && !dict->htnames.get(i).isNull())
            {
                if(arity > 0)
                {
                    passi++;
                    nestargs[0] = accumulator;
                    if(arity > 1)
                    {
                        dict->htab.get(dict->htnames.get(i), &value);
                        passi++;
                        nestargs[1] = value;
                        if(arity > 2)
                        {
                            passi++;
                            nestargs[2] = dict->htnames.get(i);
                            if(arity > 4)
                            {
                                passi++;
                                nestargs[3] = scfn.thisval;
                            }
                        }
                    }
                }
                nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &accumulator, false);
            }
        }
        return accumulator;
    }

    void nn_state_installobjectdict()
    {
        static Class::ConstItem dictmethods[] = {
            { "keys", nn_objfndict_keys }, { "size", nn_objfndict_length }, { "add", nn_objfndict_add }, { "set", nn_objfndict_set }, { "clear", nn_objfndict_clear }, { "clone", nn_objfndict_clone }, { "compact", nn_objfndict_compact }, { "contains", nn_objfndict_contains }, { "extend", nn_objfndict_extend }, { "get", nn_objfndict_get }, { "values", nn_objfndict_values }, { "remove", nn_objfndict_remove }, { "isEmpty", nn_objfndict_isempty }, { "findKey", nn_objfndict_findkey }, { "toList", nn_objfndict_tolist }, { "each", nn_objfndict_each }, { "filter", nn_objfndict_filter }, { "some", nn_objfndict_some }, { "every", nn_objfndict_every }, { "reduce", nn_objfndict_reduce }, { "@iter", nn_objfndict_iter }, { "@itern", nn_objfndict_itern }, { nullptr, nullptr },
        };
        auto gcs = SharedState::get();
    #if 0
        gcs->m_classprimdict->defNativeConstructor(nn_objfndict_constructor);
        gcs->m_classprimdict->defStaticNativeMethod(String::copy("keys"), nn_objfndict_keys);
    #endif
        gcs->m_classprimdict->installMethods(dictmethods);
    }


    static Value nn_objfnfile_constructor(const FuncContext& scfn)
    {
        FILE* hnd;
        const char* path;
        const char* mode;
        String* opath;
        File* file;
        (void)hnd;
        ArgCheck check;
        nn_argcheck_init(&check, "constructor", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        opath = scfn.argv[0].asString();
        if(opath->length() == 0)
        {
            NEON_RETURNERROR(scfn, "file path cannot be empty");
        }
        mode = "r";
        if(scfn.argc == 2)
        {
            NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
            mode = scfn.argv[1].asString()->data();
        }
        path = opath->data();
        file = (File*)nn_gcmem_protect((Object*)File::make(nullptr, false, path, mode));
        file->openWithoutParams();
        return Value::fromObject(file);
    }

    static Value nn_objfnfile_exists(const FuncContext& scfn)
    {
        String* file;
        ArgCheck check;
        nn_argcheck_init(&check, "exists", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        file = scfn.argv[0].asString();
        return Value::makeBool(nn_util_fsfileexists(file->data()));
    }

    static Value nn_objfnfile_isfile(const FuncContext& scfn)
    {
        String* file;
        ArgCheck check;
        nn_argcheck_init(&check, "isfile", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        file = scfn.argv[0].asString();
        return Value::makeBool(nn_util_fsfileisfile(file->data()));
    }

    static Value nn_objfnfile_isdirectory(const FuncContext& scfn)
    {
        String* file;
        ArgCheck check;
        nn_argcheck_init(&check, "isdirectory", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        file = scfn.argv[0].asString();
        return Value::makeBool(nn_util_fsfileisdirectory(file->data()));
    }

    static Value nn_objfnfile_readstatic(const FuncContext& scfn)
    {
        char* buf;
        size_t thismuch;
        size_t actualsz;
        String* filepath;
        ArgCheck check;
        auto gcs = SharedState::get();
        nn_argcheck_init(&check, "read", scfn);
        thismuch = -1;
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        if(scfn.argc > 1)
        {
            NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
            thismuch = (size_t)scfn.argv[1].asNumber();
        }
        filepath = scfn.argv[0].asString();
        buf = nn_util_filereadfile(filepath->data(), &actualsz, true, thismuch);
        if(buf == nullptr)
        {
            nn_except_throwclass(gcs->m_exceptions.ioerror, "%s: %s", filepath->data(), strerror(errno));
            return Value::makeNull();
        }
        return Value::fromObject(String::take(buf, actualsz));
    }

    static Value nn_objfnfile_writestatic(const FuncContext& scfn)
    {
        bool appending;
        size_t rt;
        FILE* fh;
        const char* mode;
        String* filepath;
        String* data;
        ArgCheck check;
        auto gcs = SharedState::get();
        appending = false;
        mode = "wb";
        nn_argcheck_init(&check, "write", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
        if(scfn.argc > 2)
        {
            NEON_ARGS_CHECKTYPE(&check, 2, nn_value_isbool);
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
            nn_except_throwclass(gcs->m_exceptions.ioerror, strerror(errno));
            return Value::makeNull();
        }
        rt = fwrite(data->data(), sizeof(char), data->length(), fh);
        fclose(fh);
        return Value::makeNumber(rt);
    }

    static Value nn_objfnfile_close(const FuncContext& scfn)
    {
        ArgCheck check;
        nn_argcheck_init(&check, "close", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        scfn.thisval.asFile()->closeFile();
        return Value::makeNull();
    }

    static Value nn_objfnfile_open(const FuncContext& scfn)
    {
        ArgCheck check;
        nn_argcheck_init(&check, "open", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        scfn.thisval.asFile()->openWithoutParams();
        return Value::makeNull();
    }

    static Value nn_objfnfile_isopen(const FuncContext& scfn)
    {
        File* file;
        file = scfn.thisval.asFile();
        return Value::makeBool(file->isstd || file->isopen);
    }

    static Value nn_objfnfile_isclosed(const FuncContext& scfn)
    {
        File* file;
        file = scfn.thisval.asFile();
        return Value::makeBool(!file->isstd && !file->isopen);
    }

    static Value nn_objfnfile_readmethod(const FuncContext& scfn)
    {
        size_t readhowmuch;
        IOResult res;
        File* file;
        ArgCheck check;
        nn_argcheck_init(&check, "read", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
        readhowmuch = -1;
        if(scfn.argc == 1)
        {
            NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
            readhowmuch = (size_t)scfn.argv[0].asNumber();
        }
        file = scfn.thisval.asFile();
        //#define FILE_ERROR_(scfn, type, message) NEON_RETURNERROR(scfn, #type " -> %s", message, file->path->data());

        if(!file->readData(readhowmuch, &res))
        {
            NEON_RETURNERROR(scfn, "NotFound -> %s" , strerror(errno));
        }
        return Value::fromObject(String::take(res.data, res.length));
    }

    static Value nn_objfnfile_readline(const FuncContext& scfn)
    {
        long rdline;
        size_t linelen;
        char* strline;
        File* file;
        ArgCheck check;
        String* nos;
        nn_argcheck_init(&check, "readLine", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
        file = scfn.thisval.asFile();
        linelen = 0;
        strline = nullptr;
        rdline = nn_util_filegetlinehandle(&strline, &linelen, file->handle);
        if(rdline == -1)
        {
            return Value::makeNull();
        }
        nos = String::take(strline, rdline);
        return Value::fromObject(nos);
    }

    static Value nn_objfnfile_get(const FuncContext& scfn)
    {
        int ch;
        File* file;
        ArgCheck check;
        nn_argcheck_init(&check, "get", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        file = scfn.thisval.asFile();
        ch = fgetc(file->handle);
        if(ch == EOF)
        {
            return Value::makeNull();
        }
        return Value::makeNumber(ch);
    }

    static Value nn_objfnfile_gets(const FuncContext& scfn)
    {
        long end;
        long length;
        long currentpos;
        size_t bytesread;
        char* buffer;
        File* file;
        ArgCheck check;
        nn_argcheck_init(&check, "gets", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
        length = -1;
        if(scfn.argc == 1)
        {
            NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
            length = (size_t)scfn.argv[0].asNumber();
        }
        file = scfn.thisval.asFile();
        if(!file->isstd)
        {
            if(!nn_util_fsfileexists(file->path->data()))
            {
                NEON_RETURNERROR(scfn, "NotFound -> %s" , "no such file or directory");
            }
            else if(strstr(file->mode->data(), "w") != nullptr && strstr(file->mode->data(), "+") == nullptr)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot read file in write mode");
            }
            if(!file->isopen)
            {
                NEON_RETURNERROR(scfn, "Read -> %s" , "file not open");
            }
            else if(file->handle == nullptr)
            {
                NEON_RETURNERROR(scfn, "Read -> %s" , "could not read file");
            }
            if(length == -1)
            {
                currentpos = ftell(file->handle);
                fseek(file->handle, 0L, SEEK_END);
                end = ftell(file->handle);
                fseek(file->handle, currentpos, SEEK_SET);
                length = end - currentpos;
            }
        }
        else
        {
            if(fileno(stdout) == file->number || fileno(stderr) == file->number)
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
        buffer = (char*)nn_memory_malloc(sizeof(char) * (length + 1));
        if(buffer == nullptr && length != 0)
        {
            NEON_RETURNERROR(scfn, "Buffer -> %s" , "not enough memory to read file");
        }
        bytesread = fread(buffer, sizeof(char), length, file->handle);
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

    static Value nn_objfnfile_write(const FuncContext& scfn)
    {
        size_t count;
        int length;
        unsigned char* data;
        File* file;
        String* string;
        ArgCheck check;
        nn_argcheck_init(&check, "write", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        file = scfn.thisval.asFile();
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        string = scfn.argv[0].asString();
        data = (unsigned char*)string->data();
        length = string->length();
        if(!file->isstd)
        {
            if(strstr(file->mode->data(), "r") != nullptr && strstr(file->mode->data(), "+") == nullptr)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot write into non-writable file");
            }
            else if(length == 0)
            {
                NEON_RETURNERROR(scfn, "Write -> %s" , "cannot write empty buffer to file");
            }
            else if(file->handle == nullptr || !file->isopen)
            {
                file->openWithoutParams();
            }
            else if(file->handle == nullptr)
            {
                NEON_RETURNERROR(scfn, "Write -> %s" , "could not write to file");
            }
        }
        else
        {
            if(fileno(stdin) == file->number)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot write to input file");
            }
        }
        count = fwrite(data, sizeof(unsigned char), length, file->handle);
        fflush(file->handle);
        if(count > (size_t)0)
        {
            return Value::makeBool(true);
        }
        return Value::makeBool(false);
    }

    static Value nn_objfnfile_puts(const FuncContext& scfn)
    {
        size_t i;
        size_t count;
        int rc;
        int length;
        unsigned char* data;
        File* file;
        String* string;
        ArgCheck check;
        nn_argcheck_init(&check, "puts", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        file = scfn.thisval.asFile();

        if(!file->isstd)
        {
            if(strstr(file->mode->data(), "r") != nullptr && strstr(file->mode->data(), "+") == nullptr)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot write into non-writable file");
            }
            else if(!file->isopen)
            {
                NEON_RETURNERROR(scfn, "Write -> %s" , "file not open");
            }
            else if(file->handle == nullptr)
            {
                NEON_RETURNERROR(scfn, "Write -> %s" , "could not write to file");
            }
        }
        else
        {
            if(fileno(stdin) == file->number)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot write to input file");
            }
        }
        rc = 0;
        for(i = 0; i < scfn.argc; i++)
        {
            NEON_ARGS_CHECKTYPE(&check, i, nn_value_isstring);
            string = scfn.argv[i].asString();
            data = (unsigned char*)string->data();
            length = string->length();
            count = fwrite(data, sizeof(unsigned char), length, file->handle);
            if(count > (size_t)0 || length == 0)
            {
                return Value::makeNumber(0);
            }
            rc += count;
        }
        return Value::makeNumber(rc);
    }

    static Value nn_objfnfile_putc(const FuncContext& scfn)
    {
        size_t i;
        int rc;
        File* file;
        ArgCheck check;
        nn_argcheck_init(&check, "puts", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        file = scfn.thisval.asFile();
        if(!file->isstd)
        {
            if(strstr(file->mode->data(), "r") != nullptr && strstr(file->mode->data(), "+") == nullptr)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot write into non-writable file");
            }
            else if(!file->isopen)
            {
                NEON_RETURNERROR(scfn, "Write -> %s" , "file not open");
            }
            else if(file->handle == nullptr)
            {
                NEON_RETURNERROR(scfn, "Write -> %s" , "could not write to file");
            }
        }
        else
        {
            if(fileno(stdin) == file->number)
            {
                NEON_RETURNERROR(scfn, "Unsupported -> %s" , "cannot write to input file");
            }
        }
        rc = 0;
        for(i = 0; i < scfn.argc; i++)
        {
            NEON_ARGS_CHECKTYPE(&check, i, nn_value_isnumber);
            int cv = scfn.argv[i].asNumber();
            rc += fputc(cv, file->handle);
        }
        return Value::makeNumber(rc);
    }

    static Value nn_objfnfile_printf(const FuncContext& scfn)
    {
        File* file;
        IOStream pr;
        String* ofmt;
        ArgCheck check;
        nn_argcheck_init(&check, "printf", scfn);
        file = scfn.thisval.asFile();
        NEON_ARGS_CHECKMINARG(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        ofmt = scfn.argv[0].asString();
        IOStream::makeStackIO(&pr, file->handle, false);
        FormatInfo nfi(&pr, ofmt->data(), ofmt->length());
        if(!nfi.formatWithArgs(scfn.argc, 1, scfn.argv))
        {
        }
        return Value::makeNull();
    }

    static Value nn_objfnfile_number(const FuncContext& scfn)
    {
        ArgCheck check;
        nn_argcheck_init(&check, "number", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        return Value::makeNumber(scfn.thisval.asFile()->number);
    }

    static Value nn_objfnfile_istty(const FuncContext& scfn)
    {
        File* file;
        ArgCheck check;
        nn_argcheck_init(&check, "istty", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        file = scfn.thisval.asFile();
        return Value::makeBool(file->istty);
    }

    static Value nn_objfnfile_flush(const FuncContext& scfn)
    {
        File* file;
        ArgCheck check;
        nn_argcheck_init(&check, "flush", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        file = scfn.thisval.asFile();
        if(!file->isopen)
        {
            NEON_RETURNERROR(scfn, "Unsupported -> %s" , "I/O operation on closed file");
        }
    #if defined(NEON_PLAT_ISLINUX)
        if(fileno(stdin) == file->number)
        {
            while((getchar()) != '\n')
            {
            }
        }
        else
        {
            fflush(file->handle);
        }
    #else
        fflush(file->handle);
    #endif
        return Value::makeNull();
    }

    static Value nn_objfnfile_path(const FuncContext& scfn)
    {
        File* file;
        ArgCheck check;
        nn_argcheck_init(&check, "path", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        file = scfn.thisval.asFile();
        if(file->isstd)
        {
            NEON_RETURNERROR(scfn, "method not supported for std files");
        }
        return Value::fromObject(file->path);
    }

    static Value nn_objfnfile_mode(const FuncContext& scfn)
    {
        File* file;
        ArgCheck check;
        nn_argcheck_init(&check, "mode", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        file = scfn.thisval.asFile();
        return Value::fromObject(file->mode);
    }

    static Value nn_objfnfile_name(const FuncContext& scfn)
    {
        char* name;
        File* file;
        ArgCheck check;
        nn_argcheck_init(&check, "name", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        file = scfn.thisval.asFile();
        if(!file->isstd)
        {
            name = nn_util_fsgetbasename(file->path->data());
            return Value::fromObject(String::copy(name));
        }
        else if(file->istty)
        {
            return Value::fromObject(String::copy("<tty>"));
        }
        return Value::makeNull();
    }

    static Value nn_objfnfile_seek(const FuncContext& scfn)
    {
        long position;
        int seektype;
        File* file;
        ArgCheck check;
        nn_argcheck_init(&check, "seek", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 2);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        file = scfn.thisval.asFile();
        if(file->isstd)
        {
            NEON_RETURNERROR(scfn, "method not supported for std files");
        }
        position = (long)scfn.argv[0].asNumber();
        seektype = scfn.argv[1].asNumber();
        if(fseek(file->handle, position, seektype) == 0)
        {
            return Value::makeBool(true);
        }
        NEON_RETURNERROR(scfn, "File -> %s" , strerror(errno));
    }

    static Value nn_objfnfile_tell(const FuncContext& scfn)
    {
        File* file;
        ArgCheck check;
        nn_argcheck_init(&check, "tell", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        file = scfn.thisval.asFile();
        if(file->isstd)
        {
            NEON_RETURNERROR(scfn, "method not supported for std files");
        }
        return Value::makeNumber(ftell(file->handle));
    }



    Value nn_modfn_os_stat(const FuncContext& scfn);

    void nn_state_installobjectfile()
    {
        static Class::ConstItem filemethods[] = {
            { "close", nn_objfnfile_close }, { "open", nn_objfnfile_open }, { "read", nn_objfnfile_readmethod }, { "get", nn_objfnfile_get }, { "gets", nn_objfnfile_gets }, { "write", nn_objfnfile_write }, { "puts", nn_objfnfile_puts }, { "putc", nn_objfnfile_putc }, { "printf", nn_objfnfile_printf }, { "number", nn_objfnfile_number }, { "isTTY", nn_objfnfile_istty }, { "isOpen", nn_objfnfile_isopen }, { "isClosed", nn_objfnfile_isclosed }, { "flush", nn_objfnfile_flush }, { "path", nn_objfnfile_path }, { "seek", nn_objfnfile_seek }, { "tell", nn_objfnfile_tell }, { "mode", nn_objfnfile_mode }, { "name", nn_objfnfile_name }, { "readLine", nn_objfnfile_readline }, { nullptr, nullptr },
        };
        auto gcs = SharedState::get();
        gcs->m_classprimfile->defNativeConstructor(nn_objfnfile_constructor);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("read"), nn_objfnfile_readstatic);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("write"), nn_objfnfile_writestatic);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("put"), nn_objfnfile_writestatic);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("exists"), nn_objfnfile_exists);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("isFile"), nn_objfnfile_isfile);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("isDirectory"), nn_objfnfile_isdirectory);
        gcs->m_classprimfile->defStaticNativeMethod(String::copy("stat"), nn_modfn_os_stat);
        gcs->m_classprimfile->installMethods(filemethods);
    }

    void nn_funcscript_destroy(Function* ofn)
    {
        Blob::destroy(ofn->m_fnvals.fnscriptfunc.blob);
        nn_memory_free(ofn->m_fnvals.fnscriptfunc.blob);
    }


    /*
     * TODO: when executable is run outside of current dev environment, it should correctly
     *       find the 'mods' folder. trickier than it sounds:
     *   src/<cfiles>
     *   src/vsbuild/run.exe
     *   src/eg/<scriptfiles>
     *   src/mods/<scriptmodules>
     *
     * there's definitely some easy workaround by using Process.scriptdirectory() et al, but it's less than ideal.
     */

    void loadBuiltinMethods()
    {
        int i;
        static ModInitFN g_builtinmodules[] = {
            nn_natmodule_load_null, nn_natmodule_load_os, nn_natmodule_load_astscan, nn_natmodule_load_complex, nullptr,
        };
        for(i = 0; g_builtinmodules[i] != nullptr; i++)
        {
            nn_import_loadnativemodule(g_builtinmodules[i], nullptr, "<__native__>", nullptr);
        }
    }


    void nn_state_setupmodulepaths()
    {
        int i;
        static const char* defaultsearchpaths[] = { "mods", "mods/@/index" NEON_CONFIG_FILEEXT, ".", nullptr };
        auto gcs = SharedState::get();
        Module::addSearchPathObj(gcs->m_processinfo->cliexedirectory);
        for(i = 0; defaultsearchpaths[i] != nullptr; i++)
        {
            Module::addSearchPath(defaultsearchpaths[i]);
        }
    }



    bool nn_import_loadnativemodule(ModInitFN init_fn, char* importname, const char* source, void* dlw)
    {
        size_t j;
        size_t k;
        size_t slen;
        Value v;
        Value fieldname;
        Value funcname;
        Value funcrealvalue;
        DefExport::Function func;
        DefExport::Field field;
        DefExport* defmod;
        Module* targetmod;
        DefExport::Class klassreg;
        String* classname;
        Function* native;
        Class* klass;
        auto gcs = SharedState::get();
        defmod = init_fn();
        if(defmod != nullptr)
        {
            targetmod = (Module*)nn_gcmem_protect((Object*)Module::make((char*)defmod->m_defmodname, source, false, true));
            targetmod->fnpreloaderptr = (void*)defmod->fnpreloaderfunc;
            targetmod->fnunloaderptr = (void*)defmod->fnunloaderfunc;
            if(defmod->definedfields != nullptr)
            {
                for(j = 0; defmod->definedfields[j].m_deffieldname != nullptr; j++)
                {
                    field = defmod->definedfields[j];
                    fieldname = Value::fromObject(nn_gcmem_protect((Object*)String::copy(field.m_deffieldname)));
                    v = field.fieldvalfn(FuncContext{Value::makeNull(), nullptr, 0});
                    gcs->stackPush(v);
                    targetmod->deftable.set(fieldname, v);
                    gcs->stackPop();
                }
            }
            if(defmod->definedfunctions != nullptr)
            {
                for(j = 0; defmod->definedfunctions[j].m_deffuncname != nullptr; j++)
                {
                    func = defmod->definedfunctions[j];
                    funcname = Value::fromObject(nn_gcmem_protect((Object*)String::copy(func.m_deffuncname)));
                    funcrealvalue = Value::fromObject(nn_gcmem_protect((Object*)Function::makeFuncNative(func.function, func.m_deffuncname, nullptr)));
                    gcs->stackPush(funcrealvalue);
                    targetmod->deftable.set(funcname, funcrealvalue);
                    gcs->stackPop();
                }
            }
            if(defmod->definedclasses != nullptr)
            {
                for(j = 0; ((defmod->definedclasses[j].m_defclassname != nullptr) && (defmod->definedclasses[j].defpubfunctions != nullptr)); j++)
                {
                    klassreg = defmod->definedclasses[j];
                    classname = (String*)nn_gcmem_protect((Object*)String::copy(klassreg.m_defclassname));
                    klass = (Class*)nn_gcmem_protect((Object*)Class::make(classname, gcs->m_classprimobject));
                    if(klassreg.defpubfunctions != nullptr)
                    {
                        for(k = 0; klassreg.defpubfunctions[k].m_deffuncname != nullptr; k++)
                        {
                            func = klassreg.defpubfunctions[k];
                            slen = strlen(func.m_deffuncname);
                            funcname = Value::fromObject(nn_gcmem_protect((Object*)String::copy(func.m_deffuncname)));
                            native = (Function*)nn_gcmem_protect((Object*)Function::makeFuncNative(func.function, func.m_deffuncname, nullptr));
                            if(func.isstatic)
                            {
                                native->m_contexttype = Function::CTXTYPE_STATIC;
                            }
                            else if(slen > 0 && func.m_deffuncname[0] == '_')
                            {
                                native->m_contexttype = Function::CTXTYPE_PRIVATE;
                            }
                            if(strncmp(func.m_deffuncname, "constructor", slen) == 0)
                            {
                                klass->m_constructor = Value::fromObject(native);
                            }
                            else
                            {
                                klass->m_instmethods.set(funcname, Value::fromObject(native));
                            }
                        }
                    }
                    if(klassreg.defpubfields != nullptr)
                    {
                        k = 0;
                        while(true)
                        {
                            if(klassreg.defpubfields[k].m_deffieldname == nullptr)
                            {
                                break;
                            }
                            field = klassreg.defpubfields[k];
                            if(field.m_deffieldname != nullptr)
                            {
                                klass->defCallableField(String::copy(field.m_deffieldname), field.fieldvalfn);
                            }
                            k++;
                        }
                    }
                    targetmod->deftable.set(Value::fromObject(classname), Value::fromObject(klass));
                }
            }
            if(dlw != nullptr)
            {
                targetmod->handle = dlw;
            }
            nn_import_addnativemodule(targetmod, targetmod->m_modname->data());
            SharedState::clearGCProtect();
            return true;
        }
        else
        {
            nn_state_warn("Error loading module: %s\n", importname);
        }
        return false;
    }

    void nn_import_addnativemodule(Module* module, const char* as)
    {
        Value name;
        auto gcs = SharedState::get();
        if(as != nullptr)
        {
            module->m_modname = String::copy(as);
        }
        name = Value::fromObject(String::copyObject(module->m_modname));
        gcs->stackPush(name);
        gcs->stackPush(Value::fromObject(module));
        gcs->m_openedmodules.set(name, Value::fromObject(module));
        gcs->stackPop(2);
    }

    void nn_import_closemodule(void* hnd)
    {
        (void)hnd;
    }

    static Value nn_objfnnumber_tohexstring(const FuncContext& scfn)
    {
        return Value::fromObject(nn_util_numbertohexstring(scfn.thisval.asNumber(), false));
    }

    static Value nn_objfnmath_hypot(const FuncContext& scfn)
    {
        return Value::makeNumber(hypot(scfn.argv[0].asNumber(), scfn.argv[1].asNumber()));
    }

    static Value nn_objfnmath_abs(const FuncContext& scfn)
    {
        return Value::makeNumber(fabs(scfn.argv[0].asNumber()));
    }

    static Value nn_objfnmath_round(const FuncContext& scfn)
    {
        return Value::makeNumber(round(scfn.argv[0].asNumber()));
    }

    static Value nn_objfnmath_sqrt(const FuncContext& scfn)
    {
        return Value::makeNumber(sqrt(scfn.argv[0].asNumber()));
    }

    static Value nn_objfnmath_ceil(const FuncContext& scfn)
    {
        return Value::makeNumber(ceil(scfn.argv[0].asNumber()));
    }

    static Value nn_objfnmath_floor(const FuncContext& scfn)
    {
        return Value::makeNumber(floor(scfn.argv[0].asNumber()));
    }

    static Value nn_objfnmath_min(const FuncContext& scfn)
    {
        double b;
        double x;
        double y;
        x = scfn.argv[0].asNumber();
        y = scfn.argv[1].asNumber();
        b = (x < y) ? x : y;
        return Value::makeNumber(b);
    }

    static Value nn_objfnnumber_tobinstring(const FuncContext& scfn)
    {
        return Value::fromObject(nn_util_numbertobinstring(scfn.thisval.asNumber()));
    }

    static Value nn_objfnnumber_tooctstring(const FuncContext& scfn)
    {
        return Value::fromObject(nn_util_numbertooctstring(scfn.thisval.asNumber(), false));
    }

    static Value nn_objfnnumber_constructor(const FuncContext& scfn)
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

    void nn_state_installobjectnumber()
    {
        static Class::ConstItem numbermethods[] = {
            { "toHexString", nn_objfnnumber_tohexstring },
            { "toOctString", nn_objfnnumber_tooctstring },
            { "toBinString", nn_objfnnumber_tobinstring },
            { nullptr, nullptr },
        };
        auto gcs = SharedState::get();
        gcs->m_classprimnumber->defNativeConstructor(nn_objfnnumber_constructor);
        gcs->m_classprimnumber->installMethods(numbermethods);
    }

    void nn_state_installmodmath()
    {
        Class* klass;
        auto gcs = SharedState::get();
        klass = nn_util_makeclass("Math", gcs->m_classprimobject);
        klass->defStaticNativeMethod(String::intern("hypot"), nn_objfnmath_hypot);
        klass->defStaticNativeMethod(String::intern("abs"), nn_objfnmath_abs);
        klass->defStaticNativeMethod(String::intern("round"), nn_objfnmath_round);
        klass->defStaticNativeMethod(String::intern("sqrt"), nn_objfnmath_sqrt);
        klass->defStaticNativeMethod(String::intern("ceil"), nn_objfnmath_ceil);
        klass->defStaticNativeMethod(String::intern("floor"), nn_objfnmath_floor);
        klass->defStaticNativeMethod(String::intern("min"), nn_objfnmath_min);
    }

    static Value nn_objfnobject_dumpself(const FuncContext& scfn)
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

    static Value nn_objfnobject_tostring(const FuncContext& scfn)
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

    static Value nn_objfnobject_typename(const FuncContext& scfn)
    {
        Value v;
        String* os;
        v = scfn.argv[0];
        os = String::copy(nn_value_typename(v, false));
        return Value::fromObject(os);
    }

    static Value nn_objfnobject_getselfinstance(const FuncContext& scfn)
    {
        return scfn.thisval;
    }

    static Value nn_objfnobject_getselfclass(const FuncContext& scfn)
    {
        #if 0
            nn_vmdebug_printvalue(scfn.thisval, "<object>.class:scfn.thisval=");
    #endif
        if(scfn.thisval.isInstance())
        {
            return Value::fromObject(scfn.thisval.asInstance()->m_instanceclass);
        }
        return Value::makeNull();
    }

    static Value nn_objfnobject_isstring(const FuncContext& scfn)
    {
        Value v;
        v = scfn.thisval;
        return Value::makeBool(v.isString());
    }

    static Value nn_objfnobject_isarray(const FuncContext& scfn)
    {
        Value v;
        v = scfn.thisval;
        return Value::makeBool(v.isArray());
    }

    static Value nn_objfnobject_isa(const FuncContext& scfn)
    {
        Value v;
        Value otherclval;
        Class* oclass;
        Class* selfclass;
        v = scfn.thisval;
        otherclval = scfn.argv[0];
        if(otherclval.isClass())
        {
            oclass = otherclval.asClass();
            selfclass = nn_value_getclassfor(v);
            if(selfclass != nullptr)
            {
                return Value::makeBool(nn_util_isinstanceof(selfclass, oclass));
            }
        }
        return Value::makeBool(false);
    }

    static Value nn_objfnobject_iscallable(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return (Value::makeBool(selfval.isClass() || selfval.isFuncscript() || selfval.isFuncclosure() || selfval.isFuncbound() || selfval.isFuncnative()));
    }

    static Value nn_objfnobject_isbool(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isBool());
    }

    static Value nn_objfnobject_isnumber(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isNumber());
    }

    static Value nn_objfnobject_isint(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isNumber() && (((int)selfval.asNumber()) == selfval.asNumber()));
    }

    static Value nn_objfnobject_isdict(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isDict());
    }

    static Value nn_objfnobject_isobject(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isObject());
    }

    static Value nn_objfnobject_isfunction(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isFuncscript() || selfval.isFuncclosure() || selfval.isFuncbound() || selfval.isFuncnative());
    }

    static Value nn_objfnobject_isiterable(const FuncContext& scfn)
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

    static Value nn_objfnobject_isclass(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isClass());
    }

    static Value nn_objfnobject_isfile(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isFile());
    }

    static Value nn_objfnobject_isinstance(const FuncContext& scfn)
    {
        Value selfval;
        selfval = scfn.thisval;
        return Value::makeBool(selfval.isInstance());
    }

    static Value nn_objfnclass_getselfname(const FuncContext& scfn)
    {
        Value selfval;
        Class* klass;
        selfval = scfn.thisval;
        klass = selfval.asClass();
        return Value::fromObject(klass->m_classname);
    }

    void nn_state_installobjectobject()
    {
        static Class::ConstItem objectmethods[] = {
            { "dump", nn_objfnobject_dumpself }, { "isa", nn_objfnobject_isa }, { "toString", nn_objfnobject_tostring }, { "isArray", nn_objfnobject_isarray }, { "isString", nn_objfnobject_isstring }, { "isCallable", nn_objfnobject_iscallable }, { "isBool", nn_objfnobject_isbool }, { "isNumber", nn_objfnobject_isnumber }, { "isInt", nn_objfnobject_isint }, { "isDict", nn_objfnobject_isdict }, { "isObject", nn_objfnobject_isobject }, { "isFunction", nn_objfnobject_isfunction }, { "isIterable", nn_objfnobject_isiterable }, { "isClass", nn_objfnobject_isclass }, { "isFile", nn_objfnobject_isfile }, { "isInstance", nn_objfnobject_isinstance }, { nullptr, nullptr },
        };
        auto gcs = SharedState::get();
        // gcs->m_classprimobject->defCallableField(String::intern("class"), nn_objfnobject_getselfclass);
        gcs->m_classprimobject->defStaticNativeMethod(String::intern("typename"), nn_objfnobject_typename);
        gcs->m_classprimobject->defStaticCallableField(String::intern("prototype"), nn_objfnobject_getselfinstance);
        gcs->m_classprimobject->installMethods(objectmethods);
        {
            gcs->m_classprimclass->defStaticCallableField(String::intern("name"), nn_objfnclass_getselfname);
        }
    }

    static Value nn_objfnprocess_exedirectory(const FuncContext& scfn)
    {
        (void)scfn;
        auto gcs = SharedState::get();
        if(gcs->m_processinfo->cliexedirectory != nullptr)
        {
            return Value::fromObject(gcs->m_processinfo->cliexedirectory);
        }
        return Value::makeNull();
    }

    static Value nn_objfnprocess_scriptfile(const FuncContext& scfn)
    {
        (void)scfn;
        auto gcs = SharedState::get();
        if(gcs->m_processinfo->cliscriptfile != nullptr)
        {
            return Value::fromObject(gcs->m_processinfo->cliscriptfile);
        }
        return Value::makeNull();
    }

    static Value nn_objfnprocess_scriptdirectory(const FuncContext& scfn)
    {
        (void)scfn;
        auto gcs = SharedState::get();
        if(gcs->m_processinfo->cliscriptdirectory != nullptr)
        {
            return Value::fromObject(gcs->m_processinfo->cliscriptdirectory);
        }
        return Value::makeNull();
    }

    static Value nn_objfnprocess_exit(const FuncContext& scfn)
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

    static Value nn_objfnprocess_kill(const FuncContext& scfn)
    {
        int pid;
        int code;
        pid = scfn.argv[0].asNumber();
        code = scfn.argv[1].asNumber();
        osfn_kill(pid, code);
        return Value::makeNull();
    }

    void nn_state_installobjectprocess()
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
        klass->defStaticNativeMethod(String::copy("kill"), nn_objfnprocess_kill);
        klass->defStaticNativeMethod(String::copy("exit"), nn_objfnprocess_exit);
        klass->defStaticNativeMethod(String::copy("exedirectory"), nn_objfnprocess_exedirectory);
        klass->defStaticNativeMethod(String::copy("scriptdirectory"), nn_objfnprocess_scriptdirectory);
        klass->defStaticNativeMethod(String::copy("script"), nn_objfnprocess_scriptfile);
    }

    static Value nn_objfnrange_lower(const FuncContext& scfn)
    {
        ArgCheck check;
        nn_argcheck_init(&check, "lower", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        return Value::makeNumber(scfn.thisval.asRange()->lower);
    }

    static Value nn_objfnrange_upper(const FuncContext& scfn)
    {
        ArgCheck check;
        nn_argcheck_init(&check, "upper", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        return Value::makeNumber(scfn.thisval.asRange()->upper);
    }

    static Value nn_objfnrange_range(const FuncContext& scfn)
    {
        ArgCheck check;
        nn_argcheck_init(&check, "range", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        return Value::makeNumber(scfn.thisval.asRange()->range);
    }

    static Value nn_objfnrange_iter(const FuncContext& scfn)
    {
        int val;
        int index;
        Range* range;
        ArgCheck check;
        nn_argcheck_init(&check, "iter", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        range = scfn.thisval.asRange();
        index = scfn.argv[0].asNumber();
        if(index >= 0 && index < range->range)
        {
            if(index == 0)
            {
                return Value::makeNumber(range->lower);
            }
            if(range->lower > range->upper)
            {
                val = --range->lower;
            }
            else
            {
                val = ++range->lower;
            }
            return Value::makeNumber(val);
        }
        return Value::makeNull();
    }

    static Value nn_objfnrange_itern(const FuncContext& scfn)
    {
        int index;
        Range* range;
        ArgCheck check;
        nn_argcheck_init(&check, "itern", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        range = scfn.thisval.asRange();
        if(scfn.argv[0].isNull())
        {
            if(range->range == 0)
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
        if(index < range->range)
        {
            return Value::makeNumber(index);
        }
        return Value::makeNull();
    }

    static Value nn_objfnrange_expand(const FuncContext& scfn)
    {
        int i;
        Value val;
        Range* range;
        Array* oa;
        range = scfn.thisval.asRange();
        oa = Array::make();
        for(i = 0; i < range->range; i++)
        {
            val = Value::makeNumber(i);
            oa->push(val);
        }
        return Value::fromObject(oa);
    }

    static Value nn_objfnrange_constructor(const FuncContext& scfn)
    {
        int a;
        int b;
        Range* orng;
        a = scfn.argv[0].asNumber();
        b = scfn.argv[1].asNumber();
        orng = nn_object_makerange(a, b);
        return Value::fromObject(orng);
    }

    void nn_state_installobjectrange()
    {
        static Class::ConstItem rangemethods[] = {
            { "lower", nn_objfnrange_lower }, { "upper", nn_objfnrange_upper }, { "range", nn_objfnrange_range }, { "expand", nn_objfnrange_expand }, { "toArray", nn_objfnrange_expand }, { "@iter", nn_objfnrange_iter }, { "@itern", nn_objfnrange_itern }, { nullptr, nullptr },
        };
        auto gcs = SharedState::get();
        gcs->m_classprimrange->defNativeConstructor(nn_objfnrange_constructor);
        gcs->m_classprimrange->installMethods(rangemethods);
    }

    static Value nn_objfnstring_utf8numbytes(const FuncContext& scfn)
    {
        int incode;
        int res;
        ArgCheck check;
        nn_argcheck_init(&check, "utf8NumBytes", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        incode = scfn.argv[0].asNumber();
        res = nn_util_utf8numbytes(incode);
        return Value::makeNumber(res);
    }

    static Value nn_objfnstring_utf8decode(const FuncContext& scfn)
    {
        int res;
        String* instr;
        ArgCheck check;
        nn_argcheck_init(&check, "utf8Decode", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        instr = scfn.argv[0].asString();
        res = nn_util_utf8decode((const uint8_t*)instr->data(), instr->length());
        return Value::makeNumber(res);
    }

    static Value nn_objfnstring_utf8encode(const FuncContext& scfn)
    {
        int incode;
        size_t len;
        String* res;
        char* buf;
        ArgCheck check;
        nn_argcheck_init(&check, "utf8Encode", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        incode = scfn.argv[0].asNumber();
        buf = nn_util_utf8encode(incode, &len);
        res = String::take(buf, len);
        return Value::fromObject(res);
    }

    static Value nn_objfnutilstring_utf8chars(const FuncContext& scfn, bool onlycodepoint)
    {
        int cp;
        bool havemax;
        size_t counter;
        size_t maxamount;
        const char* cstr;
        Array* res;
        String* os;
        String* instr;
        utf8iterator_t iter;
        havemax = false;
        instr = scfn.thisval.asString();
        if(scfn.argc > 0)
        {
            havemax = true;
            maxamount = scfn.argv[0].asNumber();
        }
        res = Array::make();
        nn_utf8iter_init(&iter, instr->data(), instr->length());
        counter = 0;
        while(nn_utf8iter_next(&iter))
        {
            cp = iter.codepoint;
            cstr = nn_utf8iter_getchar(&iter);
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
                os = String::copy(cstr, iter.charsize);
                res->push(Value::fromObject(os));
            }
        }
    finalize:
        return Value::fromObject(res);
    }

    static Value nn_objfnstring_utf8chars(const FuncContext& scfn)
    {
        return nn_objfnutilstring_utf8chars(scfn, false);
    }

    static Value nn_objfnstring_utf8codepoints(const FuncContext& scfn)
    {
        return nn_objfnutilstring_utf8chars(scfn, true);
    }

    static Value nn_objfnstring_fromcharcode(const FuncContext& scfn)
    {
        char ch;
        String* os;
        ArgCheck check;
        nn_argcheck_init(&check, "fromCharCode", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        ch = scfn.argv[0].asNumber();
        os = String::copy(&ch, 1);
        return Value::fromObject(os);
    }

    static Value nn_objfnstring_constructor(const FuncContext& scfn)
    {
        String* os;
        ArgCheck check;
        nn_argcheck_init(&check, "constructor", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        os = String::intern("", 0);
        return Value::fromObject(os);
    }

    static Value nn_objfnstring_length(const FuncContext& scfn)
    {
        ArgCheck check;
        String* selfstr;
        nn_argcheck_init(&check, "length", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        selfstr = scfn.thisval.asString();
        return Value::makeNumber(selfstr->length());
    }

    static Value nn_string_fromrange(const char* buf, int len)
    {
        String* str;
        if(len <= 0)
        {
            return Value::fromObject(String::intern("", 0));
        }
        str = String::intern("", 0);
        str->append(buf, len);
        return Value::fromObject(str);
    }

    String* nn_string_substring(String* selfstr, size_t start, size_t end, bool likejs)
    {
        size_t asz;
        size_t len;
        size_t tmp;
        size_t maxlen;
        char* raw;
        (void)likejs;
        maxlen = selfstr->length();
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
        raw = (char*)nn_memory_malloc(sizeof(char) * asz);
        memset(raw, 0, asz);
        memcpy(raw, selfstr->data() + start, len);
        return String::take(raw, len);
    }

    static Value nn_objfnstring_substring(const FuncContext& scfn)
    {
        size_t end;
        size_t start;
        size_t maxlen;
        String* nos;
        String* selfstr;
        ArgCheck check;
        nn_argcheck_init(&check, "substring", scfn);
        selfstr = scfn.thisval.asString();
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        maxlen = selfstr->length();
        end = maxlen;
        start = scfn.argv[0].asNumber();
        if(scfn.argc > 1)
        {
            NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
            end = scfn.argv[1].asNumber();
        }
        nos = nn_string_substring(selfstr, start, end, true);
        return Value::fromObject(nos);
    }

    static Value nn_objfnstring_charcodeat(const FuncContext& scfn)
    {
        int ch;
        int idx;
        int selflen;
        String* selfstr;
        ArgCheck check;
        nn_argcheck_init(&check, "charCodeAt", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
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

    static Value nn_objfnstring_charat(const FuncContext& scfn)
    {
        char ch;
        int idx;
        int selflen;
        String* selfstr;
        ArgCheck check;
        nn_argcheck_init(&check, "charAt", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
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

    static Value nn_objfnstring_upper(const FuncContext& scfn)
    {
        size_t slen;
        char* string;
        String* str;
        ArgCheck check;
        nn_argcheck_init(&check, "upper", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        str = scfn.thisval.asString();
        slen = str->length();
        string = nn_util_strtoupper(str->mutdata(), slen);
        return Value::fromObject(String::copy(string, slen));
    }

    static Value nn_objfnstring_lower(const FuncContext& scfn)
    {
        size_t slen;
        char* string;
        String* str;
        ArgCheck check;
        nn_argcheck_init(&check, "lower", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        str = scfn.thisval.asString();
        slen = str->length();
        string = nn_util_strtolower(str->mutdata(), slen);
        return Value::fromObject(String::copy(string, slen));
    }

    static Value nn_objfnstring_isalpha(const FuncContext& scfn)
    {
        size_t i;
        size_t len;
        ArgCheck check;
        String* selfstr;
        nn_argcheck_init(&check, "isAlpha", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
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

    static Value nn_objfnstring_isalnum(const FuncContext& scfn)
    {
        size_t i;
        size_t len;
        String* selfstr;
        ArgCheck check;
        nn_argcheck_init(&check, "isAlnum", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
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

    static Value nn_objfnstring_isfloat(const FuncContext& scfn)
    {
        double f;
        char* p;
        String* selfstr;
        ArgCheck check;
        (void)f;
        nn_argcheck_init(&check, "isFloat", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
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

    static Value nn_objfnstring_isnumber(const FuncContext& scfn)
    {
        size_t i;
        size_t len;
        String* selfstr;
        ArgCheck check;
        nn_argcheck_init(&check, "isNumber", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
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

    static Value nn_objfnstring_islower(const FuncContext& scfn)
    {
        size_t i;
        size_t len;
        bool alphafound;
        String* selfstr;
        ArgCheck check;
        nn_argcheck_init(&check, "isLower", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
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

    static Value nn_objfnstring_isupper(const FuncContext& scfn)
    {
        size_t i;
        size_t len;
        bool alphafound;
        String* selfstr;
        ArgCheck check;
        nn_argcheck_init(&check, "isUpper", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
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

    static Value nn_objfnstring_isspace(const FuncContext& scfn)
    {
        size_t i;
        size_t len;
        String* selfstr;
        ArgCheck check;
        nn_argcheck_init(&check, "isSpace", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
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

    static Value nn_objfnstring_trim(const FuncContext& scfn)
    {
        char trimmer;
        char* end;
        char* string;
        String* selfstr;
        ArgCheck check;
        nn_argcheck_init(&check, "trim", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
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

    static Value nn_objfnstring_ltrim(const FuncContext& scfn)
    {
        char trimmer;
        char* end;
        char* string;
        String* selfstr;
        ArgCheck check;
        nn_argcheck_init(&check, "ltrim", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
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

    static Value nn_objfnstring_rtrim(const FuncContext& scfn)
    {
        char trimmer;
        char* end;
        char* string;
        String* selfstr;
        ArgCheck check;
        nn_argcheck_init(&check, "rtrim", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
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

    static Value nn_objfnstring_indexof(const FuncContext& scfn)
    {
        int startindex;
        char* result;
        const char* haystack;
        String* string;
        String* needle;
        ArgCheck check;
        nn_argcheck_init(&check, "indexOf", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        string = scfn.thisval.asString();
        needle = scfn.argv[0].asString();
        startindex = 0;
        if(scfn.argc == 2)
        {
            NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
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

    static Value nn_objfnstring_startswith(const FuncContext& scfn)
    {
        String* substr;
        String* string;
        ArgCheck check;
        nn_argcheck_init(&check, "startsWith", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        string = scfn.thisval.asString();
        substr = scfn.argv[0].asString();
        if(string->length() == 0 || substr->length() == 0 || substr->length() > string->length())
        {
            return Value::makeBool(false);
        }
        return Value::makeBool(memcmp(substr->data(), string->data(), substr->length()) == 0);
    }

    static Value nn_objfnstring_endswith(const FuncContext& scfn)
    {
        int difference;
        String* substr;
        String* string;
        ArgCheck check;
        nn_argcheck_init(&check, "endsWith", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        string = scfn.thisval.asString();
        substr = scfn.argv[0].asString();
        if(string->length() == 0 || substr->length() == 0 || substr->length() > string->length())
        {
            return Value::makeBool(false);
        }
        difference = string->length() - substr->length();
        return Value::makeBool(memcmp(substr->data(), string->data() + difference, substr->length()) == 0);
    }

    static Value nn_util_stringregexmatch(String* string, String* pattern, bool capture)
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
        RegexContext pctx;
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
        RegexContext::initStack(&pctx, tokens, restokens);
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
            nn_except_throwclass(gcs->m_exceptions.regexerror, pctx.errorbuf);
        }
        RegexContext::destroy(&pctx);
        if(capture)
        {
            return Value::makeNull();
        }
        return Value::makeBool(false);
    }

    static Value nn_objfnstring_matchcapture(const FuncContext& scfn)
    {
        String* pattern;
        String* string;
        ArgCheck check;
        nn_argcheck_init(&check, "match", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        string = scfn.thisval.asString();
        pattern = scfn.argv[0].asString();
        return nn_util_stringregexmatch(string, pattern, true);
    }

    static Value nn_objfnstring_matchonly(const FuncContext& scfn)
    {
        String* pattern;
        String* string;
        ArgCheck check;
        nn_argcheck_init(&check, "match", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        string = scfn.thisval.asString();
        pattern = scfn.argv[0].asString();
        return nn_util_stringregexmatch(string, pattern, false);
    }

    static Value nn_objfnstring_count(const FuncContext& scfn)
    {
        int count;
        const char* tmp;
        String* substr;
        String* string;
        ArgCheck check;
        nn_argcheck_init(&check, "count", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        string = scfn.thisval.asString();
        substr = scfn.argv[0].asString();
        if(substr->length() == 0 || string->length() == 0)
        {
            return Value::makeNumber(0);
        }
        count = 0;
        tmp = string->data();
        while((tmp = nn_util_utf8strstr(tmp, substr->data())))
        {
            count++;
            tmp++;
        }
        return Value::makeNumber(count);
    }

    static Value nn_objfnstring_tonumber(const FuncContext& scfn)
    {
        String* selfstr;
        ArgCheck check;
        nn_argcheck_init(&check, "toNumber", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        selfstr = scfn.thisval.asString();
        return Value::makeNumber(strtod(selfstr->data(), nullptr));
    }

    static Value nn_objfnstring_isascii(const FuncContext& scfn)
    {
        String* string;
        ArgCheck check;
        nn_argcheck_init(&check, "isAscii", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
        if(scfn.argc == 1)
        {
            NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isbool);
        }
        string = scfn.thisval.asString();
        return Value::fromObject(string);
    }

    static Value nn_objfnstring_tolist(const FuncContext& scfn)
    {
        size_t i;
        size_t end;
        size_t start;
        size_t length;
        Array* list;
        String* string;
        ArgCheck check;
        nn_argcheck_init(&check, "toList", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        string = scfn.thisval.asString();
        list = (Array*)nn_gcmem_protect((Object*)Array::make());
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

    static Value nn_objfnstring_tobytes(const FuncContext& scfn)
    {
        size_t i;
        size_t length;
        Array* list;
        String* string;
        ArgCheck check;
        nn_argcheck_init(&check, "toBytes", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        string = scfn.thisval.asString();
        list = (Array*)nn_gcmem_protect((Object*)Array::make());
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

    static Value nn_objfnstring_lpad(const FuncContext& scfn)
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
        ArgCheck check;
        nn_argcheck_init(&check, "lpad", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
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
        fill = (char*)nn_memory_malloc(sizeof(char) * ((size_t)fillsize + 1));
        finalsize = string->length() + fillsize;
        for(i = 0; i < fillsize; i++)
        {
            fill[i] = fillchar;
        }
        str = (char*)nn_memory_malloc(sizeof(char) * ((size_t)finalsize + 1));
        memcpy(str, fill, fillsize);
        memcpy(str + fillsize, string->data(), string->length());
        str[finalsize] = '\0';
        nn_memory_free(fill);
        result = String::take(str, finalsize);
        result->setLength(finalsize);
        return Value::fromObject(result);
    }

    static Value nn_objfnstring_rpad(const FuncContext& scfn)
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
        ArgCheck check;
        nn_argcheck_init(&check, "rpad", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
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
        fill = (char*)nn_memory_malloc(sizeof(char) * ((size_t)fillsize + 1));
        finalsize = string->length() + fillsize;
        for(i = 0; i < fillsize; i++)
        {
            fill[i] = fillchar;
        }
        str = (char*)nn_memory_malloc(sizeof(char) * ((size_t)finalsize + 1));
        memcpy(str, string->data(), string->length());
        memcpy(str + string->length(), fill, fillsize);
        str[finalsize] = '\0';
        nn_memory_free(fill);
        result = String::take(str, finalsize);
        result->setLength(finalsize);
        return Value::fromObject(result);
    }

    static Value nn_objfnstring_split(const FuncContext& scfn)
    {
        size_t i;
        size_t end;
        size_t start;
        size_t length;
        Array* list;
        String* string;
        String* delimeter;
        ArgCheck check;
        nn_argcheck_init(&check, "split", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        string = scfn.thisval.asString();
        delimeter = scfn.argv[0].asString();
        /* empty string matches empty string to empty list */
        if(((string->length() == 0) && (delimeter->length() == 0)) || (string->length() == 0) || (delimeter->length() == 0))
        {
            return Value::fromObject(Array::make());
        }
        list = (Array*)nn_gcmem_protect((Object*)Array::make());
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

    static Value nn_objfnstring_replace(const FuncContext& scfn)
    {
        size_t i;
        size_t xlen;
        size_t totallength;
        NNStringBuffer result;
        String* substr;
        String* string;
        String* repsubstr;
        ArgCheck check;
        (void)totallength;
        nn_argcheck_init(&check, "replace", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 2, 3);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
        string = scfn.thisval.asString();
        substr = scfn.argv[0].asString();
        repsubstr = scfn.argv[1].asString();
        if((string->length() == 0 && substr->length() == 0) || string->length() == 0 || substr->length() == 0)
        {
            return Value::fromObject(String::copy(string->data(), string->length()));
        }
        nn_strbuf_makebasicemptystack(&result, nullptr, 0);
        totallength = 0;
        for(i = 0; i < string->length(); i++)
        {
            if(memcmp(string->data() + i, substr->data(), substr->length()) == 0)
            {
                if(substr->length() > 0)
                {
                    nn_strbuf_appendstrn(&result, repsubstr->data(), repsubstr->length());
                }
                i += substr->length() - 1;
                totallength += repsubstr->length();
            }
            else
            {
                nn_strbuf_appendchar(&result, string->get(i));
                totallength++;
            }
        }
        xlen = nn_strbuf_length(&result);
        return Value::fromObject(String::makeFromStrbuf(result, nn_util_hashstring(nn_strbuf_data(&result), xlen), xlen));
    }

    static Value nn_objfnstring_iter(const FuncContext& scfn)
    {
        size_t index;
        size_t length;
        String* string;
        String* result;
        ArgCheck check;
        nn_argcheck_init(&check, "iter", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
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

    static Value nn_objfnstring_itern(const FuncContext& scfn)
    {
        size_t index;
        size_t length;
        String* string;
        ArgCheck check;
        nn_argcheck_init(&check, "itern", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
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

    static Value nn_objfnstring_each(const FuncContext& scfn)
    {
        size_t i;
        size_t passi;
        size_t arity;
        Value callable;
        Value unused;
        String* string;
        ArgCheck check;
        Value nestargs[3];
        nn_argcheck_init(&check, "each", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
        string = scfn.thisval.asString();
        callable = scfn.argv[0];
        arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
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
            nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &unused, false);
        }
        /* pop the argument list */
        return Value::makeNull();
    }

    static Value nn_objfnstring_appendany(const FuncContext& scfn)
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
                oss = nn_value_tostring(arg);
                selfstring->appendObject(oss);
            }
        }
        /* pop the argument list */
        return scfn.thisval;
    }

    static Value nn_objfnstring_appendbytes(const FuncContext& scfn)
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

    void nn_state_installobjectstring()
    {
        static Class::ConstItem stringmethods[] = {
            { "@iter", nn_objfnstring_iter },
            { "@itern", nn_objfnstring_itern },
            { "size", nn_objfnstring_length },
            { "substr", nn_objfnstring_substring },
            { "substring", nn_objfnstring_substring },
            { "charCodeAt", nn_objfnstring_charcodeat },
            { "charAt", nn_objfnstring_charat },
            { "upper", nn_objfnstring_upper },
            { "lower", nn_objfnstring_lower },
            { "trim", nn_objfnstring_trim },
            { "ltrim", nn_objfnstring_ltrim },
            { "rtrim", nn_objfnstring_rtrim },
            { "split", nn_objfnstring_split },
            { "indexOf", nn_objfnstring_indexof },
            { "count", nn_objfnstring_count },
            { "toNumber", nn_objfnstring_tonumber },
            { "toList", nn_objfnstring_tolist },
            { "toBytes", nn_objfnstring_tobytes },
            { "lpad", nn_objfnstring_lpad },
            { "rpad", nn_objfnstring_rpad },
            { "replace", nn_objfnstring_replace },
            { "each", nn_objfnstring_each },
            { "startsWith", nn_objfnstring_startswith },
            { "endsWith", nn_objfnstring_endswith },
            { "isAscii", nn_objfnstring_isascii },
            { "isAlpha", nn_objfnstring_isalpha },
            { "isAlnum", nn_objfnstring_isalnum },
            { "isNumber", nn_objfnstring_isnumber },
            { "isFloat", nn_objfnstring_isfloat },
            { "isLower", nn_objfnstring_islower },
            { "isUpper", nn_objfnstring_isupper },
            { "isSpace", nn_objfnstring_isspace },
            { "utf8Chars", nn_objfnstring_utf8chars },
            { "utf8Codepoints", nn_objfnstring_utf8codepoints },
            { "utf8Bytes", nn_objfnstring_utf8codepoints },
            { "match", nn_objfnstring_matchcapture },
            { "matches", nn_objfnstring_matchonly },
            { "append", nn_objfnstring_appendany },
            { "push", nn_objfnstring_appendany },
            { "appendbytes", nn_objfnstring_appendbytes },
            { "appendbyte", nn_objfnstring_appendbytes },
            { nullptr, nullptr },
        };
        auto gcs = SharedState::get();
        gcs->m_classprimstring->defNativeConstructor(nn_objfnstring_constructor);
        gcs->m_classprimstring->defStaticNativeMethod(String::intern("fromCharCode"), nn_objfnstring_fromcharcode);
        gcs->m_classprimstring->defStaticNativeMethod(String::intern("utf8Decode"), nn_objfnstring_utf8decode);
        gcs->m_classprimstring->defStaticNativeMethod(String::intern("utf8Encode"), nn_objfnstring_utf8encode);
        gcs->m_classprimstring->defStaticNativeMethod(String::intern("utf8NumBytes"), nn_objfnstring_utf8numbytes);
        gcs->m_classprimstring->defCallableField(String::intern("length"), nn_objfnstring_length);
        gcs->m_classprimstring->installMethods(stringmethods);
    }

    static Value nn_modfn_astscan_scan(const FuncContext& scfn)
    {
        enum
        {
            /* 12 == "AstToken::T_".length */
            kTokPrefixLength = 12
        };

        const char* cstr;
        String* insrc;
        AstLexer* scn;
        Array* arr;
        Dict* itm;
        AstToken token;
        ArgCheck check;
        nn_argcheck_init(&check, "scan", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        insrc = scfn.argv[0].asString();
        scn = AstLexer::make(insrc->data());
        arr = Array::make();
        while(!scn->isatend())
        {
            itm = Dict::make();
            token = scn->scantoken();
            itm->addCstr("line", Value::makeNumber(token.m_line));
            cstr = AstLexer::tokTypeToString(token.m_toktype);
            itm->addCstr("type", Value::fromObject(String::copy(cstr + kTokPrefixLength)));
            itm->addCstr("source", Value::fromObject(String::copy(token.m_start, token.length)));
            arr->push(Value::fromObject(itm));
        }
        AstLexer::destroy(scn);
        return Value::fromObject(arr);
    }

    DefExport* nn_natmodule_load_astscan()
    {
        DefExport* ret;
        static DefExport::Function modfuncs[] = {
            { "scan", true, nn_modfn_astscan_scan },
            { nullptr, false, nullptr },
        };
        static DefExport::Field modfields[] = {
            { nullptr, false, nullptr },
        };
        static DefExport module;
        module.m_defmodname = "astscan";
        module.definedfields = modfields;
        module.definedfunctions = modfuncs;
        module.definedclasses = nullptr;
        module.fnpreloaderfunc = nullptr;
        module.fnunloaderfunc = nullptr;
        ret = &module;
        return ret;
    }


    static Value nn_pcomplex_makeinstance(Class* klass, double re, double im)
    {
        UClassComplex* inst;
        inst = (UClassComplex*)Instance::makeInstanceOfSize<UClassComplex>(klass);
        inst->re = re;
        inst->im = im;
        return Value::fromObject((Instance*)inst);
    }

    static Value nn_complexclass_constructor(const FuncContext& scfn)
    {
        UClassComplex* inst;
        assert(scfn.thisval.isInstance());
        inst = (UClassComplex*)scfn.thisval.asInstance();
        return nn_pcomplex_makeinstance(((Instance*)inst)->m_instanceclass, scfn.argv[0].asNumber(), scfn.argv[1].asNumber());
    }

    static Value nn_complexclass_opadd(const FuncContext& scfn)
    {
        Value vother;
        UClassComplex* inst;
        UClassComplex* pv;
        UClassComplex* other;
        vother = scfn.argv[0];
        assert(scfn.thisval.isInstance());
        inst = (UClassComplex*)scfn.thisval.asInstance();
        pv = (UClassComplex*)inst;
        other = (UClassComplex*)vother.asInstance();
        return nn_pcomplex_makeinstance(((Instance*)inst)->m_instanceclass, pv->re + other->re, pv->im + other->im);
    }

    static Value nn_complexclass_opsub(const FuncContext& scfn)
    {
        Value vother;
        UClassComplex* inst;
        UClassComplex* pv;
        UClassComplex* other;
        assert(scfn.thisval.isInstance());
        vother = scfn.argv[0];
        inst = (UClassComplex*)scfn.thisval.asInstance();
        pv = (UClassComplex*)inst;
        other = (UClassComplex*)vother.asInstance();
        return nn_pcomplex_makeinstance(((Instance*)inst)->m_instanceclass, pv->re - other->re, pv->im - other->im);
    }

    static Value nn_complexclass_opmul(const FuncContext& scfn)
    {
        double vre;
        double vim;
        Value vother;
        UClassComplex* inst;
        UClassComplex* pv;
        UClassComplex* other;
        assert(scfn.thisval.isInstance());
        vother = scfn.argv[0];
        inst = (UClassComplex*)scfn.thisval.asInstance();
        pv = (UClassComplex*)inst;
        other = (UClassComplex*)vother.asInstance();
        vre = (pv->re * other->re - pv->im * other->im);
        vim = (pv->re * other->im + pv->im * other->re);
        return nn_pcomplex_makeinstance(((Instance*)inst)->m_instanceclass, vre, vim);
    }

    static Value nn_complexclass_opdiv(const FuncContext& scfn)
    {
        double r;
        double i;
        double ti;
        double tr;
        Value vother;
        UClassComplex* inst;
        UClassComplex* pv;
        UClassComplex* other;
        assert(scfn.thisval.isInstance());
        vother = scfn.argv[0];
        inst = (UClassComplex*)scfn.thisval.asInstance();
        pv = (UClassComplex*)inst;
        other = (UClassComplex*)vother.asInstance();
        r = other->re;
        i = other->im;
        tr = fabs(r);
        ti = fabs(i);
        if(tr <= ti)
        {
            ti = r / i;
            tr = i * (1 + ti * ti);
            r = pv->re;
            i = pv->im;
        }
        else
        {
            ti = -i / r;
            tr = r * (1 + ti * ti);
            r = -pv->im;
            i = pv->re;
        }
        return nn_pcomplex_makeinstance(((Instance*)inst)->m_instanceclass, (r * ti + i) / tr, (i * ti - r) / tr);
    }

    static Value nn_complexclass_getre(const FuncContext& scfn)
    {
        UClassComplex* inst;
        UClassComplex* pv;
        assert(scfn.thisval.isInstance());
        inst = (UClassComplex*)scfn.thisval.asInstance();
        pv = (UClassComplex*)inst;
        return Value::makeNumber(pv->re);
    }

    static Value nn_complexclass_getim(const FuncContext& scfn)
    {
        UClassComplex* inst;
        UClassComplex* pv;
        assert(scfn.thisval.isInstance());
        inst = (UClassComplex*)scfn.thisval.asInstance();
        pv = (UClassComplex*)inst;
        return Value::makeNumber(pv->im);
    }

    DefExport* nn_natmodule_load_complex()
    {
        static DefExport::Function modfuncs[] = {
            { nullptr, false, nullptr },
        };

        static DefExport::Field modfields[] = {
            { nullptr, false, nullptr },
        };
        static DefExport module;
        module.m_defmodname = "complex";
        module.definedfields = modfields;
        module.definedfunctions = modfuncs;
        static DefExport::Function complexfuncs[] = {
            { "constructor", false, nn_complexclass_constructor }, { "__add__", false, nn_complexclass_opadd }, { "__sub__", false, nn_complexclass_opsub }, { "__mul__", false, nn_complexclass_opmul }, { "__div__", false, nn_complexclass_opdiv }, { nullptr, 0, nullptr },
        };
        static DefExport::Field complexfields[] = {
            { "re", false, nn_complexclass_getre },
            { "im", false, nn_complexclass_getim },
            { nullptr, 0, nullptr },
        };
        static DefExport::Class classes[] = { { "Complex", complexfields, complexfuncs }, { nullptr, nullptr, nullptr } };
        module.definedclasses = classes;
        module.fnpreloaderfunc = nullptr;
        module.fnunloaderfunc = nullptr;
        return &module;
    }

    static Value nn_nativefn_time(const FuncContext& scfn)
    {
        struct timeval tv;
        ArgCheck check;
        nn_argcheck_init(&check, "time", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        osfn_gettimeofday(&tv, nullptr);
        return Value::makeNumber((double)tv.tv_sec + ((double)tv.tv_usec / 10000000));
    }

    static Value nn_nativefn_microtime(const FuncContext& scfn)
    {
        struct timeval tv;
        ArgCheck check;
        nn_argcheck_init(&check, "microtime", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 0);
        osfn_gettimeofday(&tv, nullptr);
        return Value::makeNumber((1000000 * (double)tv.tv_sec) + ((double)tv.tv_usec / 10));
    }

    static Value nn_nativefn_id(const FuncContext& scfn)
    {
        Value val;
        ArgCheck check;
        nn_argcheck_init(&check, "id", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        val = scfn.argv[0];
        return Value::makeNumber(*(long*)&val);
    }

    static Value nn_nativefn_int(const FuncContext& scfn)
    {
        ArgCheck check;
        nn_argcheck_init(&check, "int", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
        if(scfn.argc == 0)
        {
            return Value::makeNumber(0);
        }
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        return Value::makeNumber((double)((int)scfn.argv[0].asNumber()));
    }

    static Value nn_nativefn_chr(const FuncContext& scfn)
    {
        size_t len;
        char* string;
        int ch;
        ArgCheck check;
        nn_argcheck_init(&check, "chr", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        ch = scfn.argv[0].asNumber();
        string = nn_util_utf8encode(ch, &len);
        return Value::fromObject(String::take(string, len));
    }

    static Value nn_nativefn_ord(const FuncContext& scfn)
    {
        int ord;
        int length;
        String* string;
        ArgCheck check;
        nn_argcheck_init(&check, "ord", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
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

    static Value nn_nativefn_rand(const FuncContext& scfn)
    {
        int tmp;
        int lowerlimit;
        int upperlimit;
        ArgCheck check;
        nn_argcheck_init(&check, "rand", scfn);
        NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 2);
        lowerlimit = 0;
        upperlimit = 1;
        if(scfn.argc > 0)
        {
            NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
            lowerlimit = scfn.argv[0].asNumber();
        }
        if(scfn.argc == 2)
        {
            NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
            upperlimit = scfn.argv[1].asNumber();
        }
        if(lowerlimit > upperlimit)
        {
            tmp = upperlimit;
            upperlimit = lowerlimit;
            lowerlimit = tmp;
        }
        return Value::makeNumber(nn_util_mtrand(lowerlimit, upperlimit));
    }

    static Value nn_nativefn_eval(const FuncContext& scfn)
    {
        Value result;
        String* os;
        ArgCheck check;
        nn_argcheck_init(&check, "eval", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        os = scfn.argv[0].asString();
        /*fprintf(stderr, "eval:src=%s\n", os->data());*/
        result = evalSource(os->data());
        return result;
    }

    /*
    static Value nn_nativefn_loadfile(const FuncContext& scfn)
    {
        Value result;
        String* os;
        ArgCheck check;
        nn_argcheck_init(&check, "loadfile", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 1);
        os = scfn.argv[0].asString();
        fprintf(stderr, "eval:src=%s\n", os->data());
        result = evalSource(os->data());
        return result;
    }
    */

    static Value nn_nativefn_instanceof(const FuncContext& scfn)
    {
        ArgCheck check;
        nn_argcheck_init(&check, "instanceof", scfn);
        NEON_ARGS_CHECKCOUNT(&check, 2);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isinstance);
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isclass);
        return Value::makeBool(nn_util_isinstanceof(scfn.argv[0].asInstance()->m_instanceclass, scfn.argv[1].asClass()));
    }

    static Value nn_nativefn_sprintf(const FuncContext& scfn)
    {
        IOStream pr;
        String* res;
        String* ofmt;
        ArgCheck check;
        nn_argcheck_init(&check, "sprintf", scfn);
        NEON_ARGS_CHECKMINARG(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
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

    static Value nn_nativefn_printf(const FuncContext& scfn)
    {
        String* ofmt;
        ArgCheck check;
        auto gcs = SharedState::get();
        nn_argcheck_init(&check, "printf", scfn);
        NEON_ARGS_CHECKMINARG(&check, 1);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        ofmt = scfn.argv[0].asString();
        FormatInfo nfi(gcs->m_stdoutprinter, ofmt->data(), ofmt->length());
        if(!nfi.formatWithArgs(scfn.argc, 1, scfn.argv))
        {
        }
        return Value::makeNull();
    }

    static Value nn_nativefn_print(const FuncContext& scfn)
    {
        size_t i;
        auto gcs = SharedState::get();
        for(i = 0; i < scfn.argc; i++)
        {
            ValPrinter::printValue(gcs->m_stdoutprinter, scfn.argv[i], false, true);
        }
        return Value::makeNull();
    }

    static Value nn_nativefn_println(const FuncContext& scfn)
    {
        Value v;
        auto gcs = SharedState::get();
        v = nn_nativefn_print(scfn);
        gcs->m_stdoutprinter->writeString("\n");
        return v;
    }

    static Value nn_nativefn_isnan(const FuncContext& scfn)
    {
        (void)scfn;
        return Value::makeBool(false);
    }

    static Value nn_objfnjson_stringify(const FuncContext& scfn)
    {
        Value v;
        IOStream pr;
        String* os;
        v = scfn.argv[0];
        IOStream::makeStackString(&pr);
        pr.jsonmode = true;
        ValPrinter::printValue(&pr, v, true, false);
        os = pr.takeString();
        IOStream::destroy(&pr);
        return Value::fromObject(os);
    }

    /**
     * setup global functions.
     */
    void nn_state_initbuiltinfunctions()
    {
        Class* klass;
        auto gcs = SharedState::get();
        nn_state_defnativefunction("chr", nn_nativefn_chr);
        nn_state_defnativefunction("id", nn_nativefn_id);
        nn_state_defnativefunction("int", nn_nativefn_int);
        nn_state_defnativefunction("instanceof", nn_nativefn_instanceof);
        nn_state_defnativefunction("ord", nn_nativefn_ord);
        nn_state_defnativefunction("sprintf", nn_nativefn_sprintf);
        nn_state_defnativefunction("printf", nn_nativefn_printf);
        nn_state_defnativefunction("print", nn_nativefn_print);
        nn_state_defnativefunction("println", nn_nativefn_println);
        nn_state_defnativefunction("rand", nn_nativefn_rand);
        nn_state_defnativefunction("eval", nn_nativefn_eval);
        nn_state_defnativefunction("isNaN", nn_nativefn_isnan);
        nn_state_defnativefunction("microtime", nn_nativefn_microtime);
        nn_state_defnativefunction("time", nn_nativefn_time);
        {
            klass = nn_util_makeclass("JSON", gcs->m_classprimobject);
            klass->defStaticNativeMethod(String::copy("stringify"), nn_objfnjson_stringify);
        }
    }

    /*
     * you can use this file as a template for new native modules.
     * just fill out the fields, give the load function a meaningful name (i.e., nn_natmodule_load_foobar if your module is "foobar"),
     * et cetera.
     * then, add said function in libmodule.c's loadBuiltinMethods, and you're good to go!
     */

    DefExport* nn_natmodule_load_null()
    {
        static DefExport::Function modfuncs[] = {
            /* {"somefunc",   true,  myfancymodulefunction},*/
            { nullptr, false, nullptr },
        };

        static DefExport::Field modfields[] = {
            /*{"somefield", true, the_function_that_gets_called},*/
            { nullptr, false, nullptr },
        };
        static DefExport module;
        module.m_defmodname = "null";
        module.definedfields = modfields;
        module.definedfunctions = modfuncs;
        module.definedclasses = nullptr;
        module.fnpreloaderfunc = nullptr;
        module.fnunloaderfunc = nullptr;
        return &module;
    }

    void nn_modfn_os_preloader()
    {
    }

    static Value nn_modfn_os_readdir(const FuncContext& scfn)
    {
        const char* dirn;
        FSDirReader rd;
        FSDirItem itm;
        Value res;
        Value itemval;
        Value callable;
        String* os;
        String* itemstr;
        ArgCheck check;
        Value nestargs[2];
        nn_argcheck_init(&check, "readdir", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_iscallable);

        os = scfn.argv[0].asString();
        callable = scfn.argv[1];
        dirn = os->data();
        if(fslib_diropen(&rd, dirn))
        {
            while(fslib_dirread(&rd, &itm))
            {
    #if 0
                    itemstr = String::intern(itm.name);
    #else
                itemstr = String::copy(itm.name);
    #endif
                itemval = Value::fromObject(itemstr);
                nestargs[0] = itemval;
                nn_nestcall_callfunction(callable, scfn.thisval, nestargs, 1, &res, false);
            }
            fslib_dirclose(&rd);
            return Value::makeNull();
        }
        else
        {
            nn_except_throw("cannot open directory '%s'", dirn);
        }
        return Value::makeNull();
    }

    /*
    static Value nn_modfn_os_$template(const FuncContext& scfn)
    {
        int64_t r;
        int64_t mod;
        String* path;
        ArgCheck check;
        nn_argcheck_init(&check, "chmod", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        path = scfn.argv[0].asString();
        mod = scfn.argv[1].asNumber();
        r = osfn_chmod(path->data(), mod);
        return Value::makeNumber(r);
    }
    */

    static Value nn_modfn_os_chmod(const FuncContext& scfn)
    {
        int64_t r;
        int64_t mod;
        String* path;
        ArgCheck check;
        nn_argcheck_init(&check, "chmod", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        path = scfn.argv[0].asString();
        mod = scfn.argv[1].asNumber();
        r = osfn_chmod(path->data(), mod);
        return Value::makeNumber(r);
    }

    static Value nn_modfn_os_mkdir(const FuncContext& scfn)
    {
        int64_t r;
        int64_t mod;
        String* path;
        ArgCheck check;
        nn_argcheck_init(&check, "mkdir", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        path = scfn.argv[0].asString();
        mod = scfn.argv[1].asNumber();
        r = osfn_mkdir(path->data(), mod);
        return Value::makeNumber(r);
    }

    static Value nn_modfn_os_chdir(const FuncContext& scfn)
    {
        int64_t r;
        String* path;
        ArgCheck check;
        nn_argcheck_init(&check, "chdir", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        path = scfn.argv[0].asString();
        r = osfn_chdir(path->data());
        return Value::makeNumber(r);
    }

    static Value nn_modfn_os_rmdir(const FuncContext& scfn)
    {
        int64_t r;
        String* path;
        ArgCheck check;
        nn_argcheck_init(&check, "rmdir", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        path = scfn.argv[0].asString();
        r = osfn_rmdir(path->data());
        return Value::makeNumber(r);
    }

    static Value nn_modfn_os_unlink(const FuncContext& scfn)
    {
        int64_t r;
        String* path;
        ArgCheck check;
        nn_argcheck_init(&check, "unlink", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        path = scfn.argv[0].asString();
        r = osfn_unlink(path->data());
        return Value::makeNumber(r);
    }

    static Value nn_modfn_os_getenv(const FuncContext& scfn)
    {
        const char* r;
        String* key;
        ArgCheck check;
        nn_argcheck_init(&check, "getenv", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        key = scfn.argv[0].asString();
        r = osfn_getenv(key->data());
        if(r == nullptr)
        {
            return Value::makeNull();
        }
        return Value::fromObject(String::copy(r));
    }

    static Value nn_modfn_os_setenv(const FuncContext& scfn)
    {
        String* key;
        String* value;
        ArgCheck check;
        nn_argcheck_init(&check, "setenv", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
        key = scfn.argv[0].asString();
        value = scfn.argv[1].asString();
        return Value::makeBool(osfn_setenv(key->data(), value->data(), true));
    }

    static Value nn_modfn_os_cwdhelper(const FuncContext& scfn, const char* name)
    {
        enum
        {
            kMaxBufSz = 1024
        };
        ArgCheck check;
        char* r;
        char buf[kMaxBufSz];
        nn_argcheck_init(&check, name, scfn);
        r = osfn_getcwd(buf, kMaxBufSz);
        if(r == nullptr)
        {
            return Value::makeNull();
        }
        return Value::fromObject(String::copy(r));
    }

    static Value nn_modfn_os_cwd(const FuncContext& scfn)
    {
        return nn_modfn_os_cwdhelper(scfn, "cwd");
    }

    static Value nn_modfn_os_pwd(const FuncContext& scfn)
    {
        return nn_modfn_os_cwdhelper(scfn, "pwd");
    }

    static Value nn_modfn_os_basename(const FuncContext& scfn)
    {
        const char* r;
        String* path;
        ArgCheck check;
        nn_argcheck_init(&check, "basename", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        path = scfn.argv[0].asString();
        r = osfn_basename(path->data());
        if(r == nullptr)
        {
            return Value::makeNull();
        }
        return Value::fromObject(String::copy(r));
    }

    static Value nn_modfn_os_dirname(const FuncContext& scfn)
    {
        const char* r;
        String* path;
        ArgCheck check;
        nn_argcheck_init(&check, "dirname", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        path = scfn.argv[0].asString();
        r = osfn_dirname(path->data());
        if(r == nullptr)
        {
            return Value::makeNull();
        }
        return Value::fromObject(String::copy(r));
    }

    static Value nn_modfn_os_touch(const FuncContext& scfn)
    {
        FILE* fh;
        String* path;
        ArgCheck check;
        nn_argcheck_init(&check, "touch", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        path = scfn.argv[0].asString();
        fh = fopen(path->data(), "rb");
        if(fh == nullptr)
        {
            return Value::makeBool(false);
        }
        fclose(fh);
        return Value::makeBool(true);
    }

    #define putorgetkey(md, name, value)                            \
        {                                                           \
            if(havekey && (keywanted != nullptr))                   \
            {                                                       \
                if(strcmp(name, keywanted->data()) == 0) \
                {                                                   \
                    return value;                                   \
                }                                                   \
            }                                                       \
            else                                                    \
            {                                                       \
                md->addCstr(name, value);              \
            }                                                       \
        }

    Value nn_modfn_os_stat(const FuncContext& scfn)
    {
        bool havekey;
        String* keywanted;
        String* path;
        ArgCheck check;
        Dict* md;
        NNFSStat nfs;
        const char* strp;
        auto gcs = SharedState::get();
        havekey = false;
        keywanted = nullptr;
        md = nullptr;
        nn_argcheck_init(&check, "stat", scfn);
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
        path = scfn.argv[0].asString();
        if(scfn.argc > 1)
        {
            NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
            keywanted = scfn.argv[1].asString();
            havekey = true;
        }
        strp = path->data();
        if(!nn_filestat_initfrompath(&nfs, strp))
        {
            nn_except_throwclass(gcs->m_exceptions.ioerror, "%s: %s", strp, strerror(errno));
            return Value::makeNull();
        }
        md = nullptr;
        if(!havekey)
        {
            md = Dict::make();
        }
        putorgetkey(md, "path", Value::fromObject(path));
        putorgetkey(md, "mode", Value::makeNumber(nfs.mode));
        putorgetkey(md, "modename", Value::fromObject(String::copy(nfs.modename)));
        putorgetkey(md, "inode", Value::makeNumber(nfs.inode));
        putorgetkey(md, "links", Value::makeNumber(nfs.numlinks));
        putorgetkey(md, "uid", Value::makeNumber(nfs.owneruid));
        putorgetkey(md, "gid", Value::makeNumber(nfs.ownergid));
        putorgetkey(md, "blocksize", Value::makeNumber(nfs.blocksize));
        putorgetkey(md, "blocks", Value::makeNumber(nfs.blockcount));
        putorgetkey(md, "filesize", Value::makeNumber(nfs.filesize));
        putorgetkey(md, "lastchanged", Value::fromObject(String::copy(nn_filestat_ctimetostring(nfs.tmlastchanged))));
        putorgetkey(md, "lastaccess", Value::fromObject(String::copy(nn_filestat_ctimetostring(nfs.tmlastaccessed))));
        putorgetkey(md, "lastmodified", Value::fromObject(String::copy(nn_filestat_ctimetostring(nfs.tmlastmodified))));
        return Value::fromObject(md);
    }


    DefExport* nn_natmodule_load_os()
    {
        static DefExport::Function modfuncs[] = {
            { "readdir", true, nn_modfn_os_readdir },
            { "chmod", true, nn_modfn_os_chmod },
            { "chdir", true, nn_modfn_os_chdir },
            { "mkdir", true, nn_modfn_os_mkdir },
            { "unlink", true, nn_modfn_os_unlink },
            { "getenv", true, nn_modfn_os_getenv },
            { "setenv", true, nn_modfn_os_setenv },
            { "rmdir", true, nn_modfn_os_rmdir },
            { "pwd", true, nn_modfn_os_pwd },
            { "pwd", true, nn_modfn_os_cwd },
            { "basename", true, nn_modfn_os_basename },
            { "dirname", true, nn_modfn_os_dirname },
            { "touch", true, nn_modfn_os_touch },
            { "stat", true, nn_modfn_os_stat },

    /* todo: implement these! */
    #if 0
            /* shell-like directory - might be trickier */
    #endif
            { nullptr, false, nullptr },
        };
        static DefExport::Field modfields[] = {
            /*{"platform", true, get_os_platform},*/
            { nullptr, false, nullptr },
        };
        static DefExport module;
        module.m_defmodname = "os";
        module.definedfields = modfields;
        module.definedfunctions = modfuncs;
        module.definedclasses = nullptr;
        module.fnpreloaderfunc = &nn_modfn_os_preloader;
        module.fnunloaderfunc = nullptr;
        return &module;
    }

    /* initial amount of frames (will grow dynamically if needed) */
    #define NEON_CONFIG_INITFRAMECOUNT (16)

    /* initial amount of stack values (will grow dynamically if needed) */
    #define NEON_CONFIG_INITSTACKCOUNT (4 * 1)

    void nn_vm_initvmstate()
    {
        size_t finalsz;
        auto gcs = SharedState::get();
        gcs->linkedobjects = nullptr;
        gcs->m_vmstate.currentframe = nullptr;
        {
            gcs->m_vmstate.stackcapacity = NEON_CONFIG_INITSTACKCOUNT;
            finalsz = NEON_CONFIG_INITSTACKCOUNT * sizeof(Value);
            gcs->m_vmstate.stackvalues = (Value*)nn_memory_malloc(finalsz);
            if(gcs->m_vmstate.stackvalues == nullptr)
            {
                fprintf(stderr, "error: failed to allocate stackvalues!\n");
                abort();
            }
            memset(gcs->m_vmstate.stackvalues, 0, finalsz);
        }
        {
            gcs->m_vmstate.framecapacity = NEON_CONFIG_INITFRAMECOUNT;
            finalsz = NEON_CONFIG_INITFRAMECOUNT * sizeof(CallFrame);
            gcs->m_vmstate.framevalues = (CallFrame*)nn_memory_malloc(finalsz);
            if(gcs->m_vmstate.framevalues == nullptr)
            {
                fprintf(stderr, "error: failed to allocate framevalues!\n");
                abort();
            }
            memset(gcs->m_vmstate.framevalues, 0, finalsz);
        }
    }

    NEON_INLINE void nn_vm_resizeinfo(const char* context, Function* closure, size_t needed)
    {
        const char* data;
        const char* name;
        (void)needed;
        name = "unknown";
        if(closure->m_fnvals.fnclosure.scriptfunc != nullptr)
        {
            if(closure->m_fnvals.fnclosure.scriptfunc->m_funcname != nullptr)
            {
                data = closure->m_fnvals.fnclosure.scriptfunc->m_funcname->data();
                if(data != nullptr)
                {
                    name = data;
                }
            }
        }
        fprintf(stderr, "resizing %s for closure %s\n", context, name);
    }

    void nn_state_resetvmstate()
    {
        auto gcs = SharedState::get();
        gcs->m_vmstate.framecount = 0;
        gcs->m_vmstate.stackidx = 0;
        gcs->m_vmstate.openupvalues = nullptr;
    }

    bool nn_vm_callclosure(Function* closure, Value thisval, size_t argcount, bool fromoperator)
    {
        int i;
        int startva;
        CallFrame* frame;
        Array* argslist;
        // closure->clsthisval = thisval;
        NEON_APIDEBUG("thisval.m_valtype=%s, argcount=%d", nn_value_typename(thisval, true), argcount);
        auto gcs = SharedState::get();
        /* fill empty parameters if not variadic */
        for(; !closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.isvariadic && (argcount < size_t(closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity)); argcount++)
        {
            gcs->stackPush(Value::makeNull());
        }
        /* handle variadic arguments... */
        if(closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.isvariadic && (argcount >= size_t(closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity) - 1))
        {
            startva = argcount - closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity;
            argslist = Array::make();
            gcs->stackPush(Value::fromObject(argslist));
            for(i = startva; i >= 0; i--)
            {
                argslist->push(nn_vm_stackpeek(i + 1));
            }
            argcount -= startva;
            /* +1 for the gc protection push above */
            gcs->stackPop(startva + 2);
            gcs->stackPush(Value::fromObject(argslist));
        }
        if(argcount != size_t(closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity))
        {
            gcs->stackPop(argcount);
            if(closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.isvariadic)
            {
                return nn_except_throw("function '%s' expected at least %d arguments but got %d", closure->m_funcname->data(), closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity - 1, argcount);
            }
            else
            {
                return nn_except_throw("function '%s' expected %d arguments but got %d", closure->m_funcname->data(), closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity, argcount);
            }
        }
        if(gcs->checkMaybeResizeFrames() /*|| gcs->checkMaybeResizeStack()*/)
        {
            #if 0
                gcs->stackPop(argcount);
            #endif
        }
        if(fromoperator)
        {
            int64_t spos;
            spos = (gcs->m_vmstate.stackidx + (-argcount - 1));
            gcs->m_vmstate.stackvalues[spos] = thisval;
        }
        frame = &gcs->m_vmstate.framevalues[gcs->m_vmstate.framecount++];
        frame->closure = closure;
        frame->inscode = closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.blob->instrucs;
        frame->stackslotpos = gcs->m_vmstate.stackidx + (-argcount - 1);
        return true;
    }

    NEON_INLINE bool nn_vm_callnative(Function* native, Value thisval, size_t argcount)
    {
        int64_t spos;
        Value r;
        Value* vargs;
        NEON_APIDEBUG("thisval.m_valtype=%s, argcount=%d", nn_value_typename(thisval, true), argcount);
        auto gcs = SharedState::get();
        spos = gcs->m_vmstate.stackidx + (-argcount);
        vargs = &gcs->m_vmstate.stackvalues[spos];
        r = native->m_fnvals.fnnativefunc.natfunc(FuncContext{thisval, vargs, argcount});
        {
            gcs->m_vmstate.stackvalues[spos - 1] = r;
            gcs->m_vmstate.stackidx -= argcount;
        }
        SharedState::clearGCProtect();
        return true;
    }

    bool nn_vm_callvaluewithobject(Value callable, Value thisval, size_t argcount, bool fromoper)
    {
        int64_t spos;
        Function* ofn;
        ofn = callable.asFunction();
    #if 0
        #define NEON_APIPRINT(...) fprintf(stderr, __VA_ARGS__), fprintf(stderr, "\n");
    #else
        #define NEON_APIPRINT(...)
    #endif
        NEON_APIPRINT("*callvaluewithobject*: thisval.m_valtype=%s, callable.m_valtype=%s, argcount=%d", nn_value_typename(thisval, true), nn_value_typename(callable, true), argcount);
        auto gcs = SharedState::get();
        if(callable.isObject())
        {
            switch(callable.objType())
            {
                case Object::OTYP_FUNCCLOSURE:
                {
                    return nn_vm_callclosure(ofn, thisval, argcount, fromoper);
                }
                break;
                case Object::OTYP_FUNCNATIVE:
                {
                    return nn_vm_callnative(ofn, thisval, argcount);
                }
                break;
                case Object::OTYP_FUNCBOUND:
                {
                    Function* bound;
                    bound = ofn;
                    spos = (gcs->m_vmstate.stackidx + (-argcount - 1));
                    gcs->m_vmstate.stackvalues[spos] = thisval;
                    return nn_vm_callclosure(bound->m_fnvals.fnmethod.method, thisval, argcount, fromoper);
                }
                break;
                case Object::OTYP_CLASS:
                {
                    Class* klass;
                    klass = callable.asClass();
                    spos = (gcs->m_vmstate.stackidx + (-argcount - 1));
                    gcs->m_vmstate.stackvalues[spos] = thisval;
                    if(!klass->m_constructor.isNull())
                    {
                        return nn_vm_callvaluewithobject(klass->m_constructor, thisval, argcount, false);
                    }
                    else if(klass->m_superclass != nullptr && !klass->m_superclass->m_constructor.isNull())
                    {
                        return nn_vm_callvaluewithobject(klass->m_superclass->m_constructor, thisval, argcount, false);
                    }
                    else if(argcount != 0)
                    {
                        return nn_except_throw("%s constructor expects 0 arguments, %d given", klass->m_classname->data(), argcount);
                    }
                    return true;
                }
                break;
                case Object::OTYP_MODULE:
                {
                    Module* module;
                    Property* field;
                    module = callable.asModule();
                    field = module->deftable.getfieldbyostr(module->m_modname);
                    if(field != nullptr)
                    {
                        return nn_vm_callvalue(field->value, thisval, argcount, false);
                    }
                    return nn_except_throw("module %s does not export a default function", module->m_modname);
                }
                break;
                default:
                    break;
            }
        }
        return nn_except_throw("object of type %s is not callable", nn_value_typename(callable, false));
    }

    bool nn_vm_callvalue(Value callable, Value thisval, size_t argcount, bool fromoperator)
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
                    NEON_APIDEBUG("actualthisval.m_valtype=%s, argcount=%d", nn_value_typename(actualthisval, true), argcount);
                    return nn_vm_callvaluewithobject(callable, actualthisval, argcount, fromoperator);
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
                    NEON_APIDEBUG("actualthisval.m_valtype=%s, argcount=%d", nn_value_typename(actualthisval, true), argcount);
                    return nn_vm_callvaluewithobject(callable, actualthisval, argcount, fromoperator);
                }
                break;
                default:
                {
                }
                break;
            }
        }
        NEON_APIDEBUG("thisval.m_valtype=%s, argcount=%d", nn_value_typename(thisval, true), argcount);
        return nn_vm_callvaluewithobject(callable, thisval, argcount, fromoperator);
    }

    NEON_INLINE Function::ContextType nn_value_getmethodtype(Value method)
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

    Class* nn_value_getclassfor(Value receiver)
    {
        auto gcs = SharedState::get();
        if(receiver.isNumber())
        {
            return gcs->m_classprimnumber;
        }
        if(receiver.isObject())
        {
            switch(receiver.asObject()->m_objtype)
            {
                case Object::OTYP_STRING:
                    return gcs->m_classprimstring;
                case Object::OTYP_RANGE:
                    return gcs->m_classprimrange;
                case Object::OTYP_ARRAY:
                    return gcs->m_classprimarray;
                case Object::OTYP_DICT:
                    return gcs->m_classprimdict;
                case Object::OTYP_FILE:
                    return gcs->m_classprimfile;
                case Object::OTYP_FUNCBOUND:
                case Object::OTYP_FUNCCLOSURE:
                case Object::OTYP_FUNCSCRIPT:
                    return gcs->m_classprimcallable;

                default:
                {
                    fprintf(stderr, "getclassfor: unhandled type!\n");
                }
                break;
            }
        }
        return nullptr;
    }

    /*
     * the inlined variants of (push|pop(n)|peek) should be
     * used in the main VM engine.
     */

    NEON_INLINE Value nn_vm_stackpeek(int distance)
    {
        Value v;
        auto gcs = SharedState::get();
        v = gcs->m_vmstate.stackvalues[gcs->m_vmstate.stackidx + (-1 - distance)];
        return v;
    }

    /*
     * this macro cannot (rather, should not) be used outside of nn_vm_runvm().
     * if you need to halt the vm, throw an exception instead.
     * this macro is EXCLUSIVELY for non-recoverable errors!
     */
    #define nn_vmmac_exitvm()                               \
        {                                                   \
            (void)you_are_calling_exit_vm_outside_of_runvm; \
            return NEON_STATUS_FAILRUNTIME;                 \
        }

    #define nn_vmmac_tryraise(rtval, ...) \
        if(!nn_except_throw(__VA_ARGS__)) \
        {                                 \
            return rtval;                 \
        }

    /*
     * don't try to further optimize vmbits functions, unless you *really* know what you are doing.
     * they were initially macros; but for better debugging, and better type-enforcement, they
     * are now inlined functions.
     */

    NEON_INLINE uint16_t nn_vmbits_readbyte()
    {
        uint16_t r;
        auto gcs = SharedState::get();
        r = gcs->m_vmstate.currentframe->inscode->code;
        gcs->m_vmstate.currentframe->inscode++;
        return r;
    }

    NEON_INLINE Instruction nn_vmbits_readinstruction()
    {
        Instruction r;
        auto gcs = SharedState::get();
        r = *gcs->m_vmstate.currentframe->inscode;
        gcs->m_vmstate.currentframe->inscode++;
        return r;
    }

    NEON_INLINE uint16_t nn_vmbits_readshort()
    {
        uint16_t b;
        uint16_t a;
        auto gcs = SharedState::get();
        a = gcs->m_vmstate.currentframe->inscode[0].code;
        b = gcs->m_vmstate.currentframe->inscode[1].code;
        gcs->m_vmstate.currentframe->inscode += 2;
        return (uint16_t)((a << 8) | b);
    }

    NEON_INLINE Value nn_vmbits_readconst()
    {
        uint16_t idx;
        idx = nn_vmbits_readshort();
        auto gcs = SharedState::get();
        return gcs->m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.blob->constants.get(idx);
    }

    NEON_INLINE String* nn_vmbits_readstring()
    {
        return nn_vmbits_readconst().asString();
    }

    NEON_INLINE bool nn_vmutil_invokemethodfromclass(Class* klass, String* name, size_t argcount)
    {
        Property* field;
        NEON_APIDEBUG("argcount=%d", argcount);
        field = klass->m_instmethods.getfieldbyostr(name);
        if(field != nullptr)
        {
            if(nn_value_getmethodtype(field->value) == Function::CTXTYPE_PRIVATE)
            {
                return nn_except_throw("cannot call private method '%s' from instance of %s", name->data(), klass->m_classname->data());
            }
            return nn_vm_callvaluewithobject(field->value, Value::fromObject(klass), argcount, false);
        }
        return nn_except_throw("undefined method '%s' in %s", name->data(), klass->m_classname->data());
    }

    NEON_INLINE bool nn_vmutil_invokemethodself(String* name, size_t argcount)
    {
        int64_t spos;
        Value receiver;
        Instance* instance;
        Property* field;
        NEON_APIDEBUG("argcount=%d", argcount);
        auto gcs = SharedState::get();
        receiver = nn_vm_stackpeek(argcount);
        if(receiver.isInstance())
        {
            instance = receiver.asInstance();
            field = instance->m_instanceclass->m_instmethods.getfieldbyostr(name);
            if(field != nullptr)
            {
                return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
            }
            field = instance->m_instanceprops.getfieldbyostr(name);
            if(field != nullptr)
            {
                spos = (gcs->m_vmstate.stackidx + (-argcount - 1));
                gcs->m_vmstate.stackvalues[spos] = receiver;
                return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
            }
        }
        else if(receiver.isClass())
        {
            field = receiver.asClass()->m_instmethods.getfieldbyostr(name);
            if(field != nullptr)
            {
                if(nn_value_getmethodtype(field->value) == Function::CTXTYPE_STATIC)
                {
                    return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
                }
                return nn_except_throw("cannot call non-static method %s() on non instance", name->data());
            }
        }
        return nn_except_throw("cannot call method '%s' on object of type '%s'", name->data(), nn_value_typename(receiver, false));
    }

    NEON_INLINE bool nn_vmutil_invokemethodnormal(String* name, size_t argcount)
    {
        size_t spos;
        Object::Type rectype;
        Value receiver;
        Property* field;
        Class* klass;
        receiver = nn_vm_stackpeek(argcount);
        NEON_APIDEBUG("receiver.m_valtype=%s, argcount=%d", nn_value_typename(receiver, true), argcount);
        auto gcs = SharedState::get();
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
                    field = module->deftable.getfieldbyostr(name);
                    if(field != nullptr)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            return nn_except_throw("cannot call private module method '%s'", name->data());
                        }
                        return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
                    }
                    return nn_except_throw("module '%s' does not have a field named '%s'", module->m_modname->data(), name->data());
                }
                break;
                case Object::OTYP_CLASS:
                {
                    NEON_APIDEBUG("receiver is a class");
                    klass = receiver.asClass();
                    field = klass->getStaticProperty(name);
                    if(field != nullptr)
                    {
                        return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
                    }
                    field = klass->getStaticMethodField(name);
                    if(field != nullptr)
                    {
                        return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
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
                            fntyp = nn_value_getmethodtype(field->value);
                            fprintf(stderr, "fntyp: %d\n", fntyp);
                            if(fntyp == Function::CTXTYPE_PRIVATE)
                            {
                                return nn_except_throw("cannot call private method %s() on %s", name->data(), klass->m_classname->data());
                            }
                            if(fntyp == Function::CTXTYPE_STATIC)
                            {
                                return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
                            }
                        }
                    }
    #endif
                    return nn_except_throw("unknown method %s() in class %s", name->data(), klass->m_classname->data());
                }
                case Object::OTYP_INSTANCE:
                {
                    Instance* instance;
                    NEON_APIDEBUG("receiver is an instance");
                    instance = receiver.asInstance();
                    field = instance->m_instanceprops.getfieldbyostr(name);
                    if(field != nullptr)
                    {
                        spos = (gcs->m_vmstate.stackidx + (-argcount - 1));
                        gcs->m_vmstate.stackvalues[spos] = receiver;
                        return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
                    }
                    return nn_vmutil_invokemethodfromclass(instance->m_instanceclass, name, argcount);
                }
                break;
                case Object::OTYP_DICT:
                {
                    NEON_APIDEBUG("receiver is a dictionary");
                    field = gcs->m_classprimdict->getMethodField(name);
                    if(field != nullptr)
                    {
                        return nn_vm_callnative(field->value.asFunction(), receiver, argcount);
                    }
                    /* NEW in v0.0.84, dictionaries can declare extra methods as part of their entries. */
                    else
                    {
                        field = receiver.asDict()->htab.getfieldbyostr(name);
                        if(field != nullptr)
                        {
                            if(field->value.isCallable())
                            {
                                return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
                            }
                        }
                    }
                    return nn_except_throw("'dict' has no method %s()", name->data());
                }
                default:
                {
                }
                break;
            }
        }
        klass = nn_value_getclassfor(receiver);
        if(klass == nullptr)
        {
            /* @TODO: have methods for non objects as well. */
            return nn_except_throw("non-object %s has no method named '%s'", nn_value_typename(receiver, false), name->data());
        }
        field = klass->getMethodField(name);
        if(field != nullptr)
        {
            return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
        }
        return nn_except_throw("'%s' has no method %s()", klass->m_classname->data(), name->data());
    }

    NEON_INLINE bool nn_vmutil_bindmethod(Class* klass, String* name)
    {
        Value val;
        Property* field;
        Function* bound;
        auto gcs = SharedState::get();
        field = klass->m_instmethods.getfieldbyostr(name);
        if(field != nullptr)
        {
            if(nn_value_getmethodtype(field->value) == Function::CTXTYPE_PRIVATE)
            {
                return nn_except_throw("cannot get private property '%s' from instance", name->data());
            }
            val = nn_vm_stackpeek(0);
            bound = Function::makeFuncBound(val, field->value.asFunction());
            gcs->stackPop();
            gcs->stackPush(Value::fromObject(bound));
            return true;
        }
        return nn_except_throw("undefined property '%s'", name->data());
    }

    NEON_INLINE Upvalue* nn_vmutil_upvaluescapture(Value* local, int stackpos)
    {
        Upvalue* upvalue;
        Upvalue* prevupvalue;
        Upvalue* createdupvalue;
        prevupvalue = nullptr;
        auto gcs = SharedState::get();
        upvalue = gcs->m_vmstate.openupvalues;
        while(upvalue != nullptr && (&upvalue->location) > local)
        {
            prevupvalue = upvalue;
            upvalue = upvalue->next;
        }
        if(upvalue != nullptr && (&upvalue->location) == local)
        {
            return upvalue;
        }
        createdupvalue = nn_object_makeupvalue(local, stackpos);
        createdupvalue->next = upvalue;
        if(prevupvalue == nullptr)
        {
            gcs->m_vmstate.openupvalues = createdupvalue;
        }
        else
        {
            prevupvalue->next = createdupvalue;
        }
        return createdupvalue;
    }

    NEON_INLINE void nn_vmutil_upvaluesclose(const Value* last)
    {
        Upvalue* upvalue;
        auto gcs = SharedState::get();
        while(gcs->m_vmstate.openupvalues != nullptr && (&gcs->m_vmstate.openupvalues->location) >= last)
        {
            upvalue = gcs->m_vmstate.openupvalues;
            upvalue->closed = upvalue->location;
            upvalue->location = upvalue->closed;
            gcs->m_vmstate.openupvalues = upvalue->next;
        }
    }

    NEON_INLINE void nn_vmutil_definemethod(String* name)
    {
        Value method;
        Class* klass;
        auto gcs = SharedState::get();
        method = nn_vm_stackpeek(0);
        klass = nn_vm_stackpeek(1).asClass();
        klass->m_instmethods.set(Value::fromObject(name), method);
        if(nn_value_getmethodtype(method) == Function::CTXTYPE_INITIALIZER)
        {
            klass->m_constructor = method;
        }
        gcs->stackPop();
    }

    NEON_INLINE void nn_vmutil_defineproperty(String* name, bool isstatic)
    {
        Value property;
        Class* klass;
        auto gcs = SharedState::get();
        property = nn_vm_stackpeek(0);
        klass = nn_vm_stackpeek(1).asClass();
        if(!isstatic)
        {
            klass->defProperty(name, property);
        }
        else
        {
            klass->setStaticProperty(name, property);
        }
        gcs->stackPop();
    }

    /*
     * TODO: this is somewhat basic instanceof checking;
     * it does not account for namespacing, which could spell issues in the future.
     * maybe classes should be given a distinct, unique, internal name, which would
     * incidentally remove the need to walk namespaces.
     * something like `class Foo{...}` -> 'Foo@<filename>:<lineno>', i.e., "Foo.class@path/to/somefile.nn:42".
     */
    bool nn_util_isinstanceof(Class* klass1, Class* expected)
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

    /*
     * don' try to optimize too much here, since its largely irrelevant how big or small
     * the strings are; inevitably it will always be <length-of-string> * number.
     * not preallocating also means that the allocator only allocates as much as actually needed.
     */
    NEON_INLINE String* nn_vmutil_multiplystring(String* str, double number)
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

    NEON_INLINE Array* nn_vmutil_combinearrays(Array* a, Array* b)
    {
        size_t i;
        Array* list;
        auto gcs = SharedState::get();
        list = Array::make();
        gcs->stackPush(Value::fromObject(list));
        for(i = 0; i < a->count(); i++)
        {
            list->push(a->get(i));
        }
        for(i = 0; i < b->count(); i++)
        {
            list->push(b->get(i));
        }
        gcs->stackPop();
        return list;
    }

    NEON_INLINE void nn_vmutil_multiplyarray(Array* from, Array* newlist, size_t times)
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

    NEON_INLINE bool nn_vmutil_dogetrangedindexofarray(Array* list, bool willassign)
    {
        long i;
        long idxlower;
        long idxupper;
        Value valupper;
        Value vallower;
        Array* newlist;
        auto gcs = SharedState::get();
        valupper = nn_vm_stackpeek(0);
        vallower = nn_vm_stackpeek(1);
        if(!(vallower.isNull() || vallower.isNumber()) || !(valupper.isNumber() || valupper.isNull()))
        {
            gcs->stackPop(2);
            return nn_except_throw("list range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower, false), nn_value_typename(valupper, false));
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
                gcs->stackPop(3);
            }
            gcs->stackPush(Value::fromObject(Array::make()));
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
        gcs->stackPush(Value::fromObject(newlist));
        for(i = idxlower; i < idxupper; i++)
        {
            newlist->push(list->get(i));
        }
        /* clear gc protect */
        gcs->stackPop();
        if(!willassign)
        {
            /* +1 for the list itself */
            gcs->stackPop(3);
        }
        gcs->stackPush(Value::fromObject(newlist));
        return true;
    }

    NEON_INLINE bool nn_vmutil_dogetrangedindexofstring(String* string, bool willassign)
    {
        int end;
        int start;
        int length;
        int idxupper;
        int idxlower;
        Value valupper;
        Value vallower;
        auto gcs = SharedState::get();
        valupper = nn_vm_stackpeek(0);
        vallower = nn_vm_stackpeek(1);
        if(!(vallower.isNull() || vallower.isNumber()) || !(valupper.isNumber() || valupper.isNull()))
        {
            gcs->stackPop(2);
            return nn_except_throw("string range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower, false), nn_value_typename(valupper, false));
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
                gcs->stackPop(3);
            }
            gcs->stackPush(Value::fromObject(String::intern("", 0)));
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
            gcs->stackPop(3);
        }
        gcs->stackPush(Value::fromObject(String::copy(string->data() + start, end - start)));
        return true;
    }

    NEON_INLINE bool nn_vmdo_getrangedindex()
    {
        bool isgotten;
        uint16_t willassign;
        Value vfrom;
        willassign = nn_vmbits_readbyte();
        isgotten = true;
        vfrom = nn_vm_stackpeek(2);
        if(vfrom.isObject())
        {
            switch(vfrom.asObject()->m_objtype)
            {
                case Object::OTYP_STRING:
                {
                    if(!nn_vmutil_dogetrangedindexofstring(vfrom.asString(), willassign))
                    {
                        return false;
                    }
                    break;
                }
                case Object::OTYP_ARRAY:
                {
                    if(!nn_vmutil_dogetrangedindexofarray(vfrom.asArray(), willassign))
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
            return nn_except_throw("cannot range index object of type %s", nn_value_typename(vfrom, false));
        }
        return true;
    }

    NEON_INLINE bool nn_vmutil_doindexgetdict(Dict* dict, bool willassign)
    {
        Value vindex;
        Property* field;
        auto gcs = SharedState::get();
        vindex = nn_vm_stackpeek(0);
        field = dict->get(vindex);
        if(field != nullptr)
        {
            if(!willassign)
            {
                /* we can safely get rid of the index from the stack */
                gcs->stackPop(2);
            }
            gcs->stackPush(field->value);
            return true;
        }
        gcs->stackPop(1);
        gcs->stackPush(Value::makeNull());
        return true;
    }

    NEON_INLINE bool nn_vmutil_doindexgetmodule(Module* module, bool willassign)
    {
        Value vindex;
        Value result;
        auto gcs = SharedState::get();
        vindex = nn_vm_stackpeek(0);
        if(module->deftable.get(vindex, &result))
        {
            if(!willassign)
            {
                /* we can safely get rid of the index from the stack */
                gcs->stackPop(2);
            }
            gcs->stackPush(result);
            return true;
        }
        gcs->stackPop();
        return nn_except_throw("%s is undefined in module %s", nn_value_tostring(vindex)->data(), module->m_modname);
    }

    NEON_INLINE bool nn_vmutil_doindexgetstring(String* string, bool willassign)
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
        auto gcs = SharedState::get();
        okindex = false;
        vindex = nn_vm_stackpeek(0);
        if(!vindex.isNumber())
        {
            if(vindex.isRange())
            {
                rng = vindex.asRange();
                gcs->stackPop();
                gcs->stackPush(Value::makeNumber(rng->lower));
                gcs->stackPush(Value::makeNumber(rng->upper));
                return nn_vmutil_dogetrangedindexofstring(string, willassign);
            }
            gcs->stackPop(1);
            return nn_except_throw("strings are numerically indexed");
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
            gcs->stackPop(2);
        }
        if(okindex)
        {
            gcs->stackPush(Value::fromObject(String::copy(string->data() + start, end - start)));
        }
        else
        {
            gcs->stackPush(Value::makeNull());
        }
        return true;

        gcs->stackPop(1);
    #if 0
            return nn_except_throw("string index %d out of range of %d", realindex, maxlength);
    #else
        gcs->stackPush(Value::makeNull());
        return true;
    #endif
    }

    NEON_INLINE bool nn_vmutil_doindexgetarray(Array* list, bool willassign)
    {
        long index;
        Value finalval;
        Value vindex;
        Range* rng;
        vindex = nn_vm_stackpeek(0);
        auto gcs = SharedState::get();
        if(nn_util_unlikely(!vindex.isNumber()))
        {
            if(vindex.isRange())
            {
                rng = vindex.asRange();
                gcs->stackPop();
                gcs->stackPush(Value::makeNumber(rng->lower));
                gcs->stackPush(Value::makeNumber(rng->upper));
                return nn_vmutil_dogetrangedindexofarray(list, willassign);
            }
            gcs->stackPop();
            return nn_except_throw("list are numerically indexed");
        }
        index = vindex.asNumber();
        if(nn_util_unlikely(index < 0))
        {
            index = list->count() + index;
        }
        if((index < (long)list->count()) && (index >= 0))
        {
            finalval = list->get(index);
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
            gcs->stackPop(2);
        }
        gcs->stackPush(finalval);
        return true;
    }

    static Property* nn_vmutil_checkoverloadrequirements(const char* ccallername, Value target, String* name)
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

    NEON_INLINE bool nn_vmutil_tryoverloadbasic(String* name, Value target, Value firstargvval, Value setvalue, bool willassign)
    {
        size_t nargc;
        Value finalval;
        Property* field;
        Value scrargv[3];
        nargc = 1;
        auto gcs = SharedState::get();
        field = nn_vmutil_checkoverloadrequirements("tryoverloadgeneric", target, name);
        scrargv[0] = firstargvval;
        if(willassign)
        {
            scrargv[0] = setvalue;
            scrargv[1] = firstargvval;
            nargc = 2;
        }
        if(nn_nestcall_callfunction(field->value, target, scrargv, nargc, &finalval, true))
        {
            if(!willassign)
            {
                gcs->stackPop(2);
            }
            gcs->stackPush(finalval);
            return true;
        }
        return false;
    }

    NEON_INLINE bool nn_vmutil_tryoverloadmath(String* name, Value target, Value right, bool willassign)
    {
        return nn_vmutil_tryoverloadbasic(name, target, right, Value::makeNull(), willassign);
    }

    NEON_INLINE bool nn_vmutil_tryoverloadgeneric(String* name, Value target, bool willassign)
    {
        Value setval;
        Value firstargval;
        firstargval = nn_vm_stackpeek(0);
        setval = nn_vm_stackpeek(1);
        return nn_vmutil_tryoverloadbasic(name, target, firstargval, setval, willassign);
    }

    NEON_INLINE bool nn_vmdo_indexget()
    {
        bool isgotten;
        uint16_t willassign;
        Value thisval;
        auto gcs = SharedState::get();
        willassign = nn_vmbits_readbyte();
        isgotten = true;
        thisval = nn_vm_stackpeek(1);
        if(nn_util_unlikely(thisval.isInstance()))
        {
            if(nn_vmutil_tryoverloadgeneric(gcs->m_defaultstrings.nmindexget, thisval, willassign))
            {
                return true;
            }
        }
        if(nn_util_likely(thisval.isObject()))
        {
            switch(thisval.asObject()->m_objtype)
            {
                case Object::OTYP_STRING:
                {
                    if(!nn_vmutil_doindexgetstring(thisval.asString(), willassign))
                    {
                        return false;
                    }
                    break;
                }
                case Object::OTYP_ARRAY:
                {
                    if(!nn_vmutil_doindexgetarray(thisval.asArray(), willassign))
                    {
                        return false;
                    }
                    break;
                }
                case Object::OTYP_DICT:
                {
                    if(!nn_vmutil_doindexgetdict(thisval.asDict(), willassign))
                    {
                        return false;
                    }
                    break;
                }
                case Object::OTYP_MODULE:
                {
                    if(!nn_vmutil_doindexgetmodule(thisval.asModule(), willassign))
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
            nn_except_throw("cannot index object of type %s", nn_value_typename(thisval, false));
        }
        return true;
    }

    NEON_INLINE bool nn_vmutil_dosetindexdict(Dict* dict, Value index, Value value)
    {
        auto gcs = SharedState::get();
        dict->set(index, value);
        /* pop the value, index and dict out */
        gcs->stackPop(3);
        /*
        // leave the value on the stack for consumption
        // e.g. variable = dict[index] = 10
        */
        gcs->stackPush(value);
        return true;
    }

    NEON_INLINE bool nn_vmutil_dosetindexmodule(Module* module, Value index, Value value)
    {
        auto gcs = SharedState::get();
        module->deftable.set(index, value);
        /* pop the value, index and dict out */
        gcs->stackPop(3);
        /*
        // leave the value on the stack for consumption
        // e.g. variable = dict[index] = 10
        */
        gcs->stackPush(value);
        return true;
    }

    NEON_INLINE bool nn_vmutil_doindexsetarray(Array* list, Value index, Value value)
    {
        int tmp;
        int rawpos;
        int position;
        int ocnt;
        int ocap;
        int vasz;
        auto gcs = SharedState::get();
        if(nn_util_unlikely(!index.isNumber()))
        {
            gcs->stackPop(3);
            /* pop the value, index and list out */
            return nn_except_throw("list are numerically indexed");
        }
        ocap = list->m_objvarray.capacity();
        ocnt = list->count();
        rawpos = index.asNumber();
        position = rawpos;
        list->set(position, value);
        /* pop the value, index and list out */
        gcs->stackPop(3);
        /*
        // leave the value on the stack for consumption
        // e.g. variable = list[index] = 10
        */
        gcs->stackPush(value);
        return true;
        /*
        // pop the value, index and list out
        //gcs->stackPop(3);
        //return nn_except_throw("lists index %d out of range", rawpos);
        //gcs->stackPush(Value::makeNull());
        //return true;
        */
    }

    NEON_INLINE bool nn_vmutil_dosetindexstring(String* os, Value index, Value value)
    {
        int iv;
        int rawpos;
        int position;
        int oslen;
        auto gcs = SharedState::get();
        if(!index.isNumber())
        {
            gcs->stackPop(3);
            /* pop the value, index and list out */
            return nn_except_throw("strings are numerically indexed");
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
            gcs->stackPop(3);
            /*
            // leave the value on the stack for consumption
            // e.g. variable = list[index] = 10
            */
            gcs->stackPush(value);
            return true;
        }
        else
        {
            os->appendByte(iv);
            gcs->stackPop(3);
            gcs->stackPush(value);
        }
        return true;
    }

    NEON_INLINE bool nn_vmdo_indexset()
    {
        bool isset;
        Value value;
        Value index;
        Value thisval;
        auto gcs = SharedState::get();
        isset = true;
        thisval = nn_vm_stackpeek(2);
        if(nn_util_unlikely(thisval.isInstance()))
        {
            if(nn_vmutil_tryoverloadgeneric(gcs->m_defaultstrings.nmindexset, thisval, true))
            {
                return true;
            }
        }
        if(nn_util_likely(thisval.isObject()))
        {
            value = nn_vm_stackpeek(0);
            index = nn_vm_stackpeek(1);
            switch(thisval.asObject()->m_objtype)
            {
                case Object::OTYP_ARRAY:
                {
                    if(!nn_vmutil_doindexsetarray(thisval.asArray(), index, value))
                    {
                        return false;
                    }
                }
                break;
                case Object::OTYP_STRING:
                {
                    if(!nn_vmutil_dosetindexstring(thisval.asString(), index, value))
                    {
                        return false;
                    }
                }
                break;
                case Object::OTYP_DICT:
                {
                    return nn_vmutil_dosetindexdict(thisval.asDict(), index, value);
                }
                break;
                case Object::OTYP_MODULE:
                {
                    return nn_vmutil_dosetindexmodule(thisval.asModule(), index, value);
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
            return nn_except_throw("type of %s is not a valid iterable", nn_value_typename(nn_vm_stackpeek(3), false));
        }
        return true;
    }

    NEON_INLINE bool nn_vmutil_concatenate()
    {
        Value vleft;
        Value vright;
        IOStream pr;
        String* result;
        auto gcs = SharedState::get();
        vright = nn_vm_stackpeek(0);
        vleft = nn_vm_stackpeek(1);
        IOStream::makeStackString(&pr);
        ValPrinter::printValue(&pr, vleft, false, true);
        ValPrinter::printValue(&pr, vright, false, true);
        result = pr.takeString();
        IOStream::destroy(&pr);
        gcs->stackPop(2);
        gcs->stackPush(Value::fromObject(result));
        return true;
    }

    #if 0
    NEON_INLINE Value nn_vmutil_floordiv(double a, double b)
    {
        int d;
        double r;
        d = (int)a / (int)b;
        r = d - ((d * b == a) & ((a < 0) ^ (b < 0)));
        return Value::makeNumber(r);
    }
    #endif

    NEON_INLINE Value nn_vmutil_modulo(double a, double b)
    {
        double r;
        r = fmod(a, b);
        if(r != 0 && ((r < 0) != (b < 0)))
        {
            r += b;
        }
        return Value::makeNumber(r);
    }

    NEON_INLINE Value nn_vmutil_pow(double a, double b)
    {
        double r;
        r = pow(a, b);
        return Value::makeNumber(r);
    }

    NEON_INLINE Property* nn_vmutil_getclassproperty(Class* klass, String* name, bool alsothrow)
    {
        Property* field;
        field = klass->m_instmethods.getfieldbyostr(name);
        if(field != nullptr)
        {
            if(nn_value_getmethodtype(field->value) == Function::CTXTYPE_STATIC)
            {
                if(nn_util_methodisprivate(name))
                {
                    if(alsothrow)
                    {
                        nn_except_throw("cannot call private property '%s' of class %s", name->data(), klass->m_classname->data());
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
                if(nn_util_methodisprivate(name))
                {
                    if(alsothrow)
                    {
                        nn_except_throw("cannot call private property '%s' of class %s", name->data(), klass->m_classname->data());
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
            nn_except_throw("class %s does not have a static property or method named '%s'", klass->m_classname->data(), name->data());
        }
        return nullptr;
    }

    NEON_INLINE Property* nn_vmutil_getproperty(Value peeked, String* name)
    {
        Property* field;
        auto gcs = SharedState::get();
        switch(peeked.asObject()->m_objtype)
        {
            case Object::OTYP_MODULE:
            {
                Module* module;
                module = peeked.asModule();
                field = module->deftable.getfieldbyostr(name);
                if(field != nullptr)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        nn_except_throw("cannot get private module property '%s'", name->data());
                        return nullptr;
                    }
                    return field;
                }
                nn_except_throw("module '%s' does not have a field named '%s'", module->m_modname->data(), name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_CLASS:
            {
                Class* klass;
                klass = peeked.asClass();
                field = nn_vmutil_getclassproperty(klass, name, true);
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
                    if(nn_util_methodisprivate(name))
                    {
                        nn_except_throw("cannot call private property '%s' from instance of %s", name->data(), instance->m_instanceclass->m_classname->data());
                        return nullptr;
                    }
                    return field;
                }
                if(nn_util_methodisprivate(name))
                {
                    nn_except_throw("cannot bind private property '%s' to instance of %s", name->data(), instance->m_instanceclass->m_classname->data());
                    return nullptr;
                }
                if(nn_vmutil_bindmethod(instance->m_instanceclass, name))
                {
                    return field;
                }
                nn_except_throw("instance of class %s does not have a property or method named '%s'", peeked.asInstance()->m_instanceclass->m_classname->data(), name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_STRING:
            {
                field = gcs->m_classprimstring->getPropertyField(name);
                if(field == nullptr)
                {
                    field = nn_vmutil_getclassproperty(gcs->m_classprimstring, name, false);
                }
                if(field != nullptr)
                {
                    return field;
                }
                nn_except_throw("class 'String' has no named property '%s'", name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_ARRAY:
            {
                field = gcs->m_classprimarray->getPropertyField(name);
                if(field == nullptr)
                {
                    field = nn_vmutil_getclassproperty(gcs->m_classprimarray, name, false);
                }
                if(field != nullptr)
                {
                    return field;
                }
                nn_except_throw("class 'Array' has no named property '%s'", name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_RANGE:
            {
                field = gcs->m_classprimrange->getPropertyField(name);
                if(field == nullptr)
                {
                    field = nn_vmutil_getclassproperty(gcs->m_classprimrange, name, false);
                }
                if(field != nullptr)
                {
                    return field;
                }
                nn_except_throw("class 'Range' has no named property '%s'", name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_DICT:
            {
                field = peeked.asDict()->htab.getfieldbyostr(name);
                if(field == nullptr)
                {
                    field = gcs->m_classprimdict->getPropertyField(name);
                }
                if(field != nullptr)
                {
                    return field;
                }
                nn_except_throw("unknown key or class 'Dict' property '%s'", name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_FILE:
            {
                field = gcs->m_classprimfile->getPropertyField(name);
                if(field == nullptr)
                {
                    field = nn_vmutil_getclassproperty(gcs->m_classprimfile, name, false);
                }
                if(field != nullptr)
                {
                    return field;
                }
                nn_except_throw("class 'File' has no named property '%s'", name->data());
                return nullptr;
            }
            break;
            case Object::OTYP_FUNCBOUND:
            case Object::OTYP_FUNCCLOSURE:
            case Object::OTYP_FUNCSCRIPT:
            case Object::OTYP_FUNCNATIVE:
            {
                field = gcs->m_classprimcallable->getPropertyField(name);
                if(field != nullptr)
                {
                    return field;
                }
                else
                {
                    field = nn_vmutil_getclassproperty(gcs->m_classprimcallable, name, false);
                    if(field != nullptr)
                    {
                        return field;
                    }
                }
                nn_except_throw("class 'Function' has no named property '%s'", name->data());
                return nullptr;
            }
            break;
            default:
            {
                nn_except_throw("object of type %s does not carry properties", nn_value_typename(peeked, false));
                return nullptr;
            }
            break;
        }
        return nullptr;
    }

    NEON_INLINE bool nn_vmdo_propertyget()
    {
        Value peeked;
        Property* field;
        String* name;
        auto gcs = SharedState::get();
        name = nn_vmbits_readstring();
        peeked = nn_vm_stackpeek(0);
        if(peeked.isObject())
        {
            field = nn_vmutil_getproperty(peeked, name);
            if(field == nullptr)
            {
                return false;
            }
            else
            {
                if(field->m_fieldtype == Property::FTYP_FUNCTION)
                {
                    nn_vm_callvaluewithobject(field->value, peeked, 0, false);
                }
                else
                {
                    gcs->stackPop();
                    gcs->stackPush(field->value);
                }
            }
            return true;
        }
        else
        {
            nn_except_throw("'%s' of type %s does not have properties", nn_value_tostring(peeked)->data(), nn_value_typename(peeked, false));
        }
        return false;
    }

    NEON_INLINE bool nn_vmdo_propertygetself()
    {
        Value peeked;
        String* name;
        Class* klass;
        Instance* instance;
        Module* module;
        Property* field;
        auto gcs = SharedState::get();
        name = nn_vmbits_readstring();
        peeked = nn_vm_stackpeek(0);
        if(peeked.isInstance())
        {
            instance = peeked.asInstance();
            field = instance->m_instanceprops.getfieldbyostr(name);
            if(field != nullptr)
            {
                /* pop the instance... */
                gcs->stackPop();
                gcs->stackPush(field->value);
                return true;
            }
            if(nn_vmutil_bindmethod(instance->m_instanceclass, name))
            {
                return true;
            }
            nn_vmmac_tryraise(false, "instance of class %s does not have a property or method named '%s'", peeked.asInstance()->m_instanceclass->m_classname->data(), name->data());
            return false;
        }
        else if(peeked.isClass())
        {
            klass = peeked.asClass();
            field = klass->m_instmethods.getfieldbyostr(name);
            if(field != nullptr)
            {
                if(nn_value_getmethodtype(field->value) == Function::CTXTYPE_STATIC)
                {
                    /* pop the class... */
                    gcs->stackPop();
                    gcs->stackPush(field->value);
                    return true;
                }
            }
            else
            {
                field = klass->getStaticProperty(name);
                if(field != nullptr)
                {
                    /* pop the class... */
                    gcs->stackPop();
                    gcs->stackPush(field->value);
                    return true;
                }
            }
            nn_vmmac_tryraise(false, "class %s does not have a static property or method named '%s'", klass->m_classname->data(), name->data());
            return false;
        }
        else if(peeked.isModule())
        {
            module = peeked.asModule();
            field = module->deftable.getfieldbyostr(name);
            if(field != nullptr)
            {
                /* pop the module... */
                gcs->stackPop();
                gcs->stackPush(field->value);
                return true;
            }
            nn_vmmac_tryraise(false, "module '%s' does not have a field named '%s'", module->m_modname->data(), name->data());
            return false;
        }
        nn_vmmac_tryraise(false, "'%s' of type %s does not have properties", nn_value_tostring(peeked)->data(), nn_value_typename(peeked, false));
        return false;
    }

    NEON_INLINE bool nn_vmdo_propertyset()
    {
        Value value;
        Value vtarget;
        Value vpeek;
        Class* klass;
        String* name;
        Dict* dict;
        Instance* instance;
        auto gcs = SharedState::get();
        vtarget = nn_vm_stackpeek(1);
        name = nn_vmbits_readstring();
        vpeek = nn_vm_stackpeek(0);
        if(vtarget.isInstance())
        {
            instance = vtarget.asInstance();
            instance->defProperty(name, vpeek);
            value = gcs->stackPop();
            /* removing the instance object */
            gcs->stackPop();
            gcs->stackPush(value);
        }
        else if(vtarget.isDict())
        {
            dict = vtarget.asDict();
            dict->set(Value::fromObject(name), vpeek);
            value = gcs->stackPop();
            /* removing the dictionary object */
            gcs->stackPop();
            gcs->stackPush(value);
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
                klass = nn_value_getclassfor(vtarget);
                /* still no class found? then it cannot carry properties */
                if(klass == nullptr)
                {
                    nn_except_throw("object of type %s cannot carry properties", nn_value_typename(vtarget, false));
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
            value = gcs->stackPop();
            /* removing the class object */
            gcs->stackPop();
            gcs->stackPush(value);
        }

        return true;
    }

    NEON_INLINE double nn_vmutil_valtonum(Value v)
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

    NEON_INLINE uint32_t nn_vmutil_valtouint(Value v)
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

    NEON_INLINE long nn_vmutil_valtoint(Value v)
    {
        return (long)nn_vmutil_valtonum(v);
    }

    NEON_INLINE bool nn_vmdo_dobinarydirect()
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
        auto gcs = SharedState::get();
        instruction = (Instruction::OpCode)gcs->m_vmstate.currentinstr.code;
        binvalright = nn_vm_stackpeek(0);
        binvalleft = nn_vm_stackpeek(1);
        if(nn_util_unlikely(binvalleft.isInstance()))
        {
            switch(instruction)
            {
                case Instruction::OPC_PRIMADD:
                {
                    if(nn_vmutil_tryoverloadmath(gcs->m_defaultstrings.nmadd, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
                case Instruction::OPC_PRIMSUBTRACT:
                {
                    if(nn_vmutil_tryoverloadmath(gcs->m_defaultstrings.nmsub, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
                case Instruction::OPC_PRIMDIVIDE:
                {
                    if(nn_vmutil_tryoverloadmath(gcs->m_defaultstrings.nmdiv, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
                case Instruction::OPC_PRIMMULTIPLY:
                {
                    if(nn_vmutil_tryoverloadmath(gcs->m_defaultstrings.nmmul, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
                case Instruction::OPC_PRIMAND:
                {
                    if(nn_vmutil_tryoverloadmath(gcs->m_defaultstrings.nmband, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
                case Instruction::OPC_PRIMOR:
                {
                    if(nn_vmutil_tryoverloadmath(gcs->m_defaultstrings.nmbor, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
                case Instruction::OPC_PRIMBITXOR:
                {
                    if(nn_vmutil_tryoverloadmath(gcs->m_defaultstrings.nmbxor, binvalleft, binvalright, willassign))
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
            nn_vmmac_tryraise(false, "unsupported operand %s for %s and %s", nn_dbg_op2str(instruction), nn_value_typename(binvalleft, false), nn_value_typename(binvalright, false));
            return false;
        }
        binvalright = gcs->stackPop();
        binvalleft = gcs->stackPop();
        res = Value::makeNull();
        switch(instruction)
        {
            case Instruction::OPC_PRIMADD:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = Value::makeNumber(dbinleft + dbinright);
            }
            break;
            case Instruction::OPC_PRIMSUBTRACT:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = Value::makeNumber(dbinleft - dbinright);
            }
            break;
            case Instruction::OPC_PRIMDIVIDE:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = Value::makeNumber(dbinleft / dbinright);
            }
            break;
            case Instruction::OPC_PRIMMULTIPLY:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = Value::makeNumber(dbinleft * dbinright);
            }
            break;
            case Instruction::OPC_PRIMAND:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = Value::makeNumber(ibinleft & ibinright);
            }
            break;
            case Instruction::OPC_PRIMOR:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = Value::makeNumber(ibinleft | ibinright);
            }
            break;
            case Instruction::OPC_PRIMBITXOR:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
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
                ubinright = nn_vmutil_valtouint(binvalright);
                ubinleft = nn_vmutil_valtouint(binvalleft);
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
                ubinright = nn_vmutil_valtouint(binvalright);
                ubinleft = nn_vmutil_valtouint(binvalleft);
                ubinright &= 0x1f;
                res = Value::makeNumber(ubinleft >> ubinright);
            }
            break;
            case Instruction::OPC_PRIMGREATER:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = Value::makeBool(dbinleft > dbinright);
            }
            break;
            case Instruction::OPC_PRIMLESSTHAN:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = Value::makeBool(dbinleft < dbinright);
            }
            break;
            default:
            {
                fprintf(stderr, "unhandled instruction %d (%s)!\n", instruction, nn_dbg_op2str(instruction));
                return false;
            }
            break;
        }
        gcs->stackPush(res);
        return true;
    }

    NEON_INLINE bool nn_vmdo_globaldefine()
    {
        Value val;
        String* name;
        name = nn_vmbits_readstring();
        val = nn_vm_stackpeek(0);
        auto gcs = SharedState::get();
        auto tab = &gcs->m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.module->deftable;
        tab->set(Value::fromObject(name), val);
        gcs->stackPop();
    #if (defined(DEBUG_TABLE) && DEBUG_TABLE) || 0
        ->print(gcs->m_debugwriter, &gcs->m_declaredglobals, "globals");
    #endif
        return true;
    }

    NEON_INLINE bool nn_vmdo_globalget()
    {
        String* name;
        Property* field;
        name = nn_vmbits_readstring();
        auto gcs = SharedState::get();
        auto tab = &gcs->m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.module->deftable;
        field = tab->getfieldbyostr(name);
        if(field == nullptr)
        {
            field = gcs->m_declaredglobals.getfieldbyostr(name);
            if(field == nullptr)
            {
                nn_except_throwclass(gcs->m_exceptions.stdexception, "global name '%s' is not defined", name->data());
                return false;
            }
        }
        gcs->stackPush(field->value);
        return true;
    }

    NEON_INLINE bool nn_vmdo_globalset()
    {
        String* name;
        Module* module;
        name = nn_vmbits_readstring();
        auto gcs = SharedState::get();
        module = gcs->m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.module;
        auto table = &module->deftable;
        if(table->set(Value::fromObject(name), nn_vm_stackpeek(0)))
        {
            if(gcs->m_conf.enablestrictmode)
            {
                table->remove(Value::fromObject(name));
                nn_vmmac_tryraise(false, "global name '%s' was not declared", name->data());
                return false;
            }
        }
        return true;
    }

    NEON_INLINE bool nn_vmdo_localget()
    {
        size_t ssp;
        uint16_t slot;
        Value val;
        slot = nn_vmbits_readshort();
        auto gcs = SharedState::get();
        ssp = gcs->m_vmstate.currentframe->stackslotpos;
        val = gcs->m_vmstate.stackvalues[ssp + slot];
        gcs->stackPush(val);
        return true;
    }

    NEON_INLINE bool nn_vmdo_localset()
    {
        size_t ssp;
        uint16_t slot;
        Value peeked;
        slot = nn_vmbits_readshort();
        peeked = nn_vm_stackpeek(0);
        auto gcs = SharedState::get();
        ssp = gcs->m_vmstate.currentframe->stackslotpos;
        gcs->m_vmstate.stackvalues[ssp + slot] = peeked;
        return true;
    }

    /*Instruction::OPC_FUNCARGOPTIONAL*/
    NEON_INLINE bool nn_vmdo_funcargoptional()
    {
        size_t ssp;
        size_t putpos;
        uint16_t slot;
        Value cval;
        Value peeked;
        auto gcs = SharedState::get();
        slot = nn_vmbits_readshort();
        peeked = nn_vm_stackpeek(1);
        cval = nn_vm_stackpeek(2);
        ssp = gcs->m_vmstate.currentframe->stackslotpos;
        #if 0
            putpos = (gcs->m_vmstate.stackidx + (-1 - 1)) ;
        #else
            #if 0
                putpos = gcs->m_vmstate.stackidx + (slot - 0);
            #else
                #if 0
                    putpos = gcs->m_vmstate.stackidx + (slot);
                #else
                    putpos = (ssp + slot) + 0;
                #endif
            #endif
        #endif
        #if 1
            {
                IOStream* pr = gcs->m_stderrprinter;
                pr->format("funcargoptional: slot=%d putpos=%zd cval=<", slot, putpos);
                ValPrinter::printValue(pr, cval, true, false);
                pr->format(">, peeked=<");
                ValPrinter::printValue(pr, peeked, true, false);
                pr->format(">\n");
            }
        #endif
        if(cval.isNull())
        {
            gcs->m_vmstate.stackvalues[putpos] = peeked;
        }
        /*
        else
        {
            #if 0
                gcs->stackPop();
            #endif
        }
        */
        gcs->stackPop();

        return true;
    }

    NEON_INLINE bool nn_vmdo_funcargget()
    {
        size_t ssp;
        uint16_t slot;
        Value val;
        slot = nn_vmbits_readshort();
        auto gcs = SharedState::get();
        ssp = gcs->m_vmstate.currentframe->stackslotpos;
        val = gcs->m_vmstate.stackvalues[ssp + slot];
        gcs->stackPush(val);
        return true;
    }

    NEON_INLINE bool nn_vmdo_funcargset()
    {
        size_t ssp;
        uint16_t slot;
        Value peeked;
        slot = nn_vmbits_readshort();
        peeked = nn_vm_stackpeek(0);
        auto gcs = SharedState::get();
        ssp = gcs->m_vmstate.currentframe->stackslotpos;
        gcs->m_vmstate.stackvalues[ssp + slot] = peeked;
        return true;
    }

    NEON_INLINE bool nn_vmdo_makeclosure()
    {
        size_t i;
        int upvidx;
        size_t ssp;
        uint16_t islocal;
        Value thisval;
        Value* upvals;
        Function* function;
        Function* closure;
        function = nn_vmbits_readconst().asFunction();
    #if 0
            thisval = nn_vm_stackpeek(3);
    #else
        thisval = Value::makeNull();
    #endif
        closure = Function::makeFuncClosure(function, thisval);
        auto gcs = SharedState::get();
        gcs->stackPush(Value::fromObject(closure));
        for(i = 0; i < (size_t)closure->upvalcount; i++)
        {
            islocal = nn_vmbits_readbyte();
            upvidx = nn_vmbits_readshort();
            if(islocal)
            {
                ssp = gcs->m_vmstate.currentframe->stackslotpos;
                upvals = &gcs->m_vmstate.stackvalues[ssp + upvidx];
                closure->m_fnvals.fnclosure.m_upvalues[i] = nn_vmutil_upvaluescapture(upvals, upvidx);
            }
            else
            {
                closure->m_fnvals.fnclosure.m_upvalues[i] = gcs->m_vmstate.currentframe->closure->m_fnvals.fnclosure.m_upvalues[upvidx];
            }
        }
        return true;
    }

    NEON_INLINE bool nn_vmdo_makearray()
    {
        int i;
        int count;
        Array* array;
        count = nn_vmbits_readshort();
        array = Array::make();
        auto gcs = SharedState::get();
        gcs->m_vmstate.stackvalues[gcs->m_vmstate.stackidx + (-count - 1)] = Value::fromObject(array);
        for(i = count - 1; i >= 0; i--)
        {
            array->push(nn_vm_stackpeek(i));
        }
        gcs->stackPop(count);
        return true;
    }

    NEON_INLINE bool nn_vmdo_makedict()
    {
        size_t i;
        size_t count;
        size_t realcount;
        Value name;
        Value value;
        Dict* dict;
        /* 1 for key, 1 for value */
        realcount = nn_vmbits_readshort();
        count = realcount * 2;
        dict = Dict::make();
        auto gcs = SharedState::get();
        gcs->m_vmstate.stackvalues[gcs->m_vmstate.stackidx + (-count - 1)] = Value::fromObject(dict);
        for(i = 0; i < count; i += 2)
        {
            name = gcs->m_vmstate.stackvalues[gcs->m_vmstate.stackidx + (-count + i)];
            if(!name.isString() && !name.isNumber() && !name.isBool())
            {
                nn_vmmac_tryraise(false, "dictionary key must be one of string, number or boolean");
                return false;
            }
            value = gcs->m_vmstate.stackvalues[gcs->m_vmstate.stackidx + (-count + i + 1)];
            dict->set(name, value);
        }
        gcs->stackPop(count);
        return true;
    }

    NEON_INLINE bool nn_vmdo_dobinaryfunc(const char* opname, nnbinopfunc_t opfn)
    {
        double dbinright;
        double dbinleft;
        Value binvalright;
        Value binvalleft;
        binvalright = nn_vm_stackpeek(0);
        binvalleft = nn_vm_stackpeek(1);
        auto gcs = SharedState::get();
        if((!binvalright.isNumber() && !binvalright.isBool()) || (!binvalleft.isNumber() && !binvalleft.isBool()))
        {
            nn_vmmac_tryraise(false, "unsupported operand %s for %s and %s", opname, nn_value_typename(binvalleft, false), nn_value_typename(binvalright, false));
            return false;
        }
        binvalright = gcs->stackPop();
        dbinright = binvalright.isBool() ? (binvalright.asBool() ? 1 : 0) : binvalright.asNumber();
        binvalleft = gcs->stackPop();
        dbinleft = binvalleft.isBool() ? (binvalleft.asBool() ? 1 : 0) : binvalleft.asNumber();
        gcs->stackPush(opfn(dbinleft, dbinright));
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
            #define VM_DISPATCH() goto readnextinstruction
        #else
            #define VM_DISPATCH() continue
        #endif
    #else
        #define VM_CASE(op) case Instruction::op:
        #define VM_DISPATCH() break
    #endif

    Status nn_vm_runvm(int exitframe, Value* rv)
    {
        int iterpos;
        int printpos;
        int ofs;
        /*
         * this variable is a NOP; it only exists to ensure that functions outside of the
         * switch tree are not calling nn_vmmac_exitvm(), as its behavior could be undefined.
         */
        bool you_are_calling_exit_vm_outside_of_runvm;
        Value* dbgslot;
        Instruction currinstr;
        auto vmgcs = SharedState::get();
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
            NEON_SETDISPATCHIDX(OPC_IMPORTIMPORT, &&VM_MAKELABEL(OPC_IMPORTIMPORT)),
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
        vmgcs->m_vmstate.currentframe = &vmgcs->m_vmstate.framevalues[vmgcs->m_vmstate.framecount - 1];
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
            if(vmgcs->m_vmstate.framecount == 0)
            {
                return NEON_STATUS_FAILRUNTIME;
            }
            if(nn_util_unlikely(vmgcs->m_conf.shoulddumpstack))
            {
                ofs = (int)(vmgcs->m_vmstate.currentframe->inscode - vmgcs->m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.blob->instrucs);
                nn_dbg_printinstructionat(vmgcs->m_debugwriter, vmgcs->m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.blob, ofs);
                fprintf(stderr, "stack (before)=[\n");
                iterpos = 0;
                dbgslot = vmgcs->m_vmstate.stackvalues;
                while(dbgslot < &vmgcs->m_vmstate.stackvalues[vmgcs->m_vmstate.stackidx])
                {
                    printpos = iterpos + 1;
                    iterpos++;
                    fprintf(stderr, "  [%s%d%s] ", nn_util_color(NEON_COLOR_YELLOW), printpos, nn_util_color(NEON_COLOR_RESET));
                    vmgcs->m_debugwriter->format("%s", nn_util_color(NEON_COLOR_YELLOW));
                    ValPrinter::printValue(vmgcs->m_debugwriter, *dbgslot, true, false);
                    vmgcs->m_debugwriter->format("%s", nn_util_color(NEON_COLOR_RESET));
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
            currinstr = nn_vmbits_readinstruction();
            vmgcs->m_vmstate.currentinstr = currinstr;
    #if defined(NEON_CONFIG_USECOMPUTEDGOTO) && (NEON_CONFIG_USECOMPUTEDGOTO == 1)
            auto address = dispatchtable[currinstr.code];
            fprintf(stderr, "gotoaddress=%p (for %s (%d))\n", address, nn_dbg_op2str(currinstr.code), currinstr.code);
            goto* address;
    #else
            switch(currinstr.code)
    #endif
            {
                VM_CASE(OPC_RETURN)
                {
                    size_t ssp;
                    Value result;
                    result = vmgcs->stackPop();
                    if(rv != nullptr)
                    {
                        *rv = result;
                    }
                    ssp = vmgcs->m_vmstate.currentframe->stackslotpos;
                    nn_vmutil_upvaluesclose(&vmgcs->m_vmstate.stackvalues[ssp]);
                    vmgcs->m_vmstate.framecount--;
                    if(vmgcs->m_vmstate.framecount == 0)
                    {
                        vmgcs->stackPop();
                        return NEON_STATUS_OK;
                    }
                    ssp = vmgcs->m_vmstate.currentframe->stackslotpos;
                    vmgcs->m_vmstate.stackidx = ssp;
                    vmgcs->stackPush(result);
                    vmgcs->m_vmstate.currentframe = &vmgcs->m_vmstate.framevalues[vmgcs->m_vmstate.framecount - 1];
                    if(vmgcs->m_vmstate.framecount == (int64_t)exitframe)
                    {
                        return NEON_STATUS_OK;
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_HALT)
                {
                    printf("**halting vm**\n");
                }
                goto finished;
                VM_CASE(OPC_PUSHCONSTANT)
                {
                    Value constant;
                    constant = nn_vmbits_readconst();
                    vmgcs->stackPush(constant);
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMADD)
                {
                    Value valright;
                    Value valleft;
                    Value result;
                    valright = nn_vm_stackpeek(0);
                    valleft = nn_vm_stackpeek(1);
                    if(valright.isString() || valleft.isString())
                    {
                        if(nn_util_unlikely(!nn_vmutil_concatenate()))
                        {
                            nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "unsupported operand + for %s and %s", nn_value_typename(valleft, false), nn_value_typename(valright, false));
                            VM_DISPATCH();
                        }
                    }
                    else if(valleft.isArray() && valright.isArray())
                    {
                        result = Value::fromObject(nn_vmutil_combinearrays(valleft.asArray(), valright.asArray()));
                        vmgcs->stackPop(2);
                        vmgcs->stackPush(result);
                    }
                    else
                    {
                        nn_vmdo_dobinarydirect();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMSUBTRACT)
                {
                    nn_vmdo_dobinarydirect();
                }
                VM_DISPATCH();
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
                    peekright = nn_vm_stackpeek(0);
                    peekleft = nn_vm_stackpeek(1);
                    if(peekleft.isString() && peekright.isNumber())
                    {
                        dbnum = peekright.asNumber();
                        string = peekleft.asString();
                        result = Value::fromObject(nn_vmutil_multiplystring(string, dbnum));
                        vmgcs->stackPop(2);
                        vmgcs->stackPush(result);
                        VM_DISPATCH();
                    }
                    else if(peekleft.isArray() && peekright.isNumber())
                    {
                        intnum = (int)peekright.asNumber();
                        vmgcs->stackPop();
                        list = peekleft.asArray();
                        newlist = Array::make();
                        vmgcs->stackPush(Value::fromObject(newlist));
                        nn_vmutil_multiplyarray(list, newlist, intnum);
                        vmgcs->stackPop(2);
                        vmgcs->stackPush(Value::fromObject(newlist));
                        VM_DISPATCH();
                    }
                    else
                    {
                        nn_vmdo_dobinarydirect();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMDIVIDE)
                {
                    nn_vmdo_dobinarydirect();
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMMODULO)
                {
                    if(nn_vmdo_dobinaryfunc("%", (nnbinopfunc_t)nn_vmutil_modulo))
                    {
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMPOW)
                {
                    if(nn_vmdo_dobinaryfunc("**", (nnbinopfunc_t)nn_vmutil_pow))
                    {
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMNEGATE)
                {
                    Value peeked;
                    peeked = nn_vm_stackpeek(0);
                    if(!peeked.isNumber())
                    {
                        nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "operator - not defined for object of type %s", nn_value_typename(peeked, false));
                        VM_DISPATCH();
                    }
                    peeked = vmgcs->stackPop();
                    vmgcs->stackPush(Value::makeNumber(-peeked.asNumber()));
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMBITNOT)
                {
                    Value peeked;
                    peeked = nn_vm_stackpeek(0);
                    if(!peeked.isNumber())
                    {
                        nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "operator ~ not defined for object of type %s", nn_value_typename(peeked, false));
                        VM_DISPATCH();
                    }
                    peeked = vmgcs->stackPop();
                    vmgcs->stackPush(Value::makeNumber(~((int)peeked.asNumber())));
                    VM_DISPATCH();
                }
                VM_CASE(OPC_PRIMAND)
                {
                    nn_vmdo_dobinarydirect();
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMOR)
                {
                    nn_vmdo_dobinarydirect();
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMBITXOR)
                {
                    nn_vmdo_dobinarydirect();
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMSHIFTLEFT)
                {
                    nn_vmdo_dobinarydirect();
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMSHIFTRIGHT)
                {
                    nn_vmdo_dobinarydirect();
                }
                VM_DISPATCH();
                VM_CASE(OPC_PUSHONE)
                {
                    vmgcs->stackPush(Value::makeNumber(1));
                }
                VM_DISPATCH();
                /* comparisons */
                VM_CASE(OPC_EQUAL)
                {
                    Value a;
                    Value b;
                    b = vmgcs->stackPop();
                    a = vmgcs->stackPop();
                    vmgcs->stackPush(Value::makeBool(nn_value_compare(a, b)));
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMGREATER)
                {
                    nn_vmdo_dobinarydirect();
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMLESSTHAN)
                {
                    nn_vmdo_dobinarydirect();
                }
                VM_DISPATCH();
                VM_CASE(OPC_PRIMNOT)
                {
                    Value val;
                    val = vmgcs->stackPop();
                    vmgcs->stackPush(Value::makeBool(val.isFalse()));
                }
                VM_DISPATCH();
                VM_CASE(OPC_PUSHNULL)
                {
                    vmgcs->stackPush(Value::makeNull());
                }
                VM_DISPATCH();
                VM_CASE(OPC_PUSHEMPTY)
                {
                    vmgcs->stackPush(Value::makeNull());
                }
                VM_DISPATCH();
                VM_CASE(OPC_PUSHTRUE)
                {
                    vmgcs->stackPush(Value::makeBool(true));
                }
                VM_DISPATCH();
                VM_CASE(OPC_PUSHFALSE)
                {
                    vmgcs->stackPush(Value::makeBool(false));
                }
                VM_DISPATCH();

                VM_CASE(OPC_JUMPNOW)
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort();
                    vmgcs->m_vmstate.currentframe->inscode += offset;
                }
                VM_DISPATCH();
                VM_CASE(OPC_JUMPIFFALSE)
                {
                    uint16_t offset;
                    Value val;
                    offset = nn_vmbits_readshort();
                    val = nn_vm_stackpeek(0);
                    if(val.isFalse())
                    {
                        vmgcs->m_vmstate.currentframe->inscode += offset;
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_LOOP)
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort();
                    vmgcs->m_vmstate.currentframe->inscode -= offset;
                }
                VM_DISPATCH();
                VM_CASE(OPC_ECHO)
                {
                    Value val;
                    val = nn_vm_stackpeek(0);
                    ValPrinter::printValue(vmgcs->m_stdoutprinter, val, vmgcs->m_isrepl, true);
                    if(!val.isNull())
                    {
                        vmgcs->m_stdoutprinter->writeString("\n");
                    }
                    vmgcs->stackPop();
                }
                VM_DISPATCH();
                VM_CASE(OPC_STRINGIFY)
                {
                    Value peeked;
                    Value popped;
                    String* os;
                    peeked = nn_vm_stackpeek(0);
                    if(!peeked.isString() && !peeked.isNull())
                    {
                        popped = vmgcs->stackPop();
                        os = nn_value_tostring(popped);
                        if(os->length() != 0)
                        {
                            vmgcs->stackPush(Value::fromObject(os));
                        }
                        else
                        {
                            vmgcs->stackPush(Value::makeNull());
                        }
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_DUPONE)
                {
                    Value val;
                    val = nn_vm_stackpeek(0);
                    vmgcs->stackPush(val);
                }
                VM_DISPATCH();
                VM_CASE(OPC_POPONE)
                {
                    vmgcs->stackPop();
                }
                VM_DISPATCH();
                VM_CASE(OPC_POPN)
                {
                    vmgcs->stackPop(nn_vmbits_readshort());
                }
                VM_DISPATCH();
                VM_CASE(OPC_UPVALUECLOSE)
                {
                    nn_vmutil_upvaluesclose(&vmgcs->m_vmstate.stackvalues[vmgcs->m_vmstate.stackidx - 1]);
                    vmgcs->stackPop();
                }
                VM_DISPATCH();
                VM_CASE(OPC_OPINSTANCEOF)
                {
                    bool rt;
                    Value first;
                    Value second;
                    Class* vclass;
                    Class* checkclass;
                    rt = false;
                    first = vmgcs->stackPop();
                    second = vmgcs->stackPop();
    #if 0
                        nn_vmdebug_printvalue(first, "first value");
                        nn_vmdebug_printvalue(second, "second value");
    #endif
                    if(!first.isClass())
                    {
                        nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "invalid use of 'is' on non-class");
                    }
                    checkclass = first.asClass();
                    vclass = nn_value_getclassfor(second);
                    if(vclass)
                    {
                        rt = nn_util_isinstanceof(vclass, checkclass);
                    }
                    vmgcs->stackPush(Value::makeBool(rt));
                }
                VM_DISPATCH();
                VM_CASE(OPC_GLOBALDEFINE)
                {
                    if(!nn_vmdo_globaldefine())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_GLOBALGET)
                {
                    if(!nn_vmdo_globalget())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_GLOBALSET)
                {
                    if(!nn_vmdo_globalset())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_LOCALGET)
                {
                    if(!nn_vmdo_localget())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_LOCALSET)
                {
                    if(!nn_vmdo_localset())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_FUNCARGGET)
                {
                    if(!nn_vmdo_funcargget())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_FUNCARGOPTIONAL)
                {
                    if(!nn_vmdo_funcargoptional())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_FUNCARGSET)
                {
                    if(!nn_vmdo_funcargset())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();

                VM_CASE(OPC_PROPERTYGET)
                {
                    if(!nn_vmdo_propertyget())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_PROPERTYSET)
                {
                    if(!nn_vmdo_propertyset())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_PROPERTYGETSELF)
                {
                    if(!nn_vmdo_propertygetself())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_MAKECLOSURE)
                {
                    if(!nn_vmdo_makeclosure())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_UPVALUEGET)
                {
                    int upvidx;
                    Function* closure;
                    upvidx = nn_vmbits_readshort();
                    closure = vmgcs->m_vmstate.currentframe->closure;
                    if(upvidx < closure->upvalcount)
                    {
                        vmgcs->stackPush((closure->m_fnvals.fnclosure.m_upvalues[upvidx]->location));
                    }
                    else
                    {
                        vmgcs->stackPush(closure->clsthisval);
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_UPVALUESET)
                {
                    int upvidx;
                    Value val;
                    upvidx = nn_vmbits_readshort();
                    val = nn_vm_stackpeek(0);
                    vmgcs->m_vmstate.currentframe->closure->m_fnvals.fnclosure.m_upvalues[upvidx]->location = val;
                }
                VM_DISPATCH();
                VM_CASE(OPC_CALLFUNCTION)
                {
                    size_t argcount;
                    Value callee;
                    Value thisval;
                    thisval = Value::makeNull();
                    argcount = nn_vmbits_readbyte();
                    callee = nn_vm_stackpeek(argcount);
                    if(callee.isFuncclosure())
                    {
                        thisval = (callee.asFunction()->clsthisval);
                    }
                    if(!nn_vm_callvalue(callee, thisval, argcount, false))
                    {
                        nn_vmmac_exitvm();
                    }
                    vmgcs->m_vmstate.currentframe = &vmgcs->m_vmstate.framevalues[vmgcs->m_vmstate.framecount - 1];
                }
                VM_DISPATCH();
                VM_CASE(OPC_CALLMETHOD)
                {
                    size_t argcount;
                    String* method;
                    method = nn_vmbits_readstring();
                    argcount = nn_vmbits_readbyte();
                    if(!nn_vmutil_invokemethodnormal(method, argcount))
                    {
                        nn_vmmac_exitvm();
                    }
                    vmgcs->m_vmstate.currentframe = &vmgcs->m_vmstate.framevalues[vmgcs->m_vmstate.framecount - 1];
                }
                VM_DISPATCH();
                VM_CASE(OPC_CLASSGETTHIS)
                {
                    Value thisval;
                    thisval = nn_vm_stackpeek(3);
                    vmgcs->m_debugwriter->format("CLASSGETTHIS: thisval=");
                    ValPrinter::printValue(vmgcs->m_debugwriter, thisval, true, false);
                    vmgcs->m_debugwriter->format("\n");
                    vmgcs->stackPush(thisval);
                }
                VM_DISPATCH();
                VM_CASE(OPC_CLASSINVOKETHIS)
                {
                    size_t argcount;
                    String* method;
                    method = nn_vmbits_readstring();
                    argcount = nn_vmbits_readbyte();
                    if(!nn_vmutil_invokemethodself(method, argcount))
                    {
                        nn_vmmac_exitvm();
                    }
                    vmgcs->m_vmstate.currentframe = &vmgcs->m_vmstate.framevalues[vmgcs->m_vmstate.framecount - 1];
                }
                VM_DISPATCH();
                VM_CASE(OPC_MAKECLASS)
                {
                    bool haveval;
                    Value pushme;
                    String* name;
                    Class* klass;
                    Property* field;
                    haveval = false;
                    name = nn_vmbits_readstring();
                    field = vmgcs->m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.module->deftable.getfieldbyostr(name);
                    if(field != nullptr)
                    {
                        if(field->value.isClass())
                        {
                            haveval = true;
                            pushme = field->value;
                        }
                    }
                    field = vmgcs->m_declaredglobals.getfieldbyostr(name);
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
                        klass = Class::make(name, vmgcs->m_classprimobject);
                        pushme = Value::fromObject(klass);
                    }
                    vmgcs->stackPush(pushme);
                }
                VM_DISPATCH();
                VM_CASE(OPC_MAKEMETHOD)
                {
                    String* name;
                    name = nn_vmbits_readstring();
                    nn_vmutil_definemethod(name);
                }
                VM_DISPATCH();
                VM_CASE(OPC_CLASSPROPERTYDEFINE)
                {
                    int isstatic;
                    String* name;
                    name = nn_vmbits_readstring();
                    isstatic = nn_vmbits_readbyte();
                    nn_vmutil_defineproperty(name, isstatic == 1);
                }
                VM_DISPATCH();
                VM_CASE(OPC_CLASSINHERIT)
                {
                    Value vclass;
                    Value vsuper;
                    Class* superclass;
                    Class* subclass;
                    vsuper = nn_vm_stackpeek(1);
                    if(!vsuper.isClass())
                    {
                        nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "cannot inherit from non-class object");
                        VM_DISPATCH();
                    }
                    vclass = nn_vm_stackpeek(0);
                    superclass = vsuper.asClass();
                    subclass = vclass.asClass();
                    subclass->inheritFrom(superclass);
                    /* pop the subclass */
                    vmgcs->stackPop();
                }
                VM_DISPATCH();
                VM_CASE(OPC_CLASSGETSUPER)
                {
                    Value vclass;
                    Class* klass;
                    String* name;
                    name = nn_vmbits_readstring();
                    vclass = nn_vm_stackpeek(0);
                    klass = vclass.asClass();
                    if(!nn_vmutil_bindmethod(klass->m_superclass, name))
                    {
                        nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "class '%s' does not have a function '%s'", klass->m_classname->data(), name->data());
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_CLASSINVOKESUPER)
                {
                    size_t argcount;
                    Value vclass;
                    Class* klass;
                    String* method;
                    method = nn_vmbits_readstring();
                    argcount = nn_vmbits_readbyte();
                    vclass = vmgcs->stackPop();
                    klass = vclass.asClass();
                    if(!nn_vmutil_invokemethodfromclass(klass, method, argcount))
                    {
                        nn_vmmac_exitvm();
                    }
                    vmgcs->m_vmstate.currentframe = &vmgcs->m_vmstate.framevalues[vmgcs->m_vmstate.framecount - 1];
                }
                VM_DISPATCH();
                VM_CASE(OPC_CLASSINVOKESUPERSELF)
                {
                    size_t argcount;
                    Value vclass;
                    Class* klass;
                    argcount = nn_vmbits_readbyte();
                    vclass = vmgcs->stackPop();
                    klass = vclass.asClass();
                    if(!nn_vmutil_invokemethodfromclass(klass, vmgcs->m_defaultstrings.nmconstructor, argcount))
                    {
                        nn_vmmac_exitvm();
                    }
                    vmgcs->m_vmstate.currentframe = &vmgcs->m_vmstate.framevalues[vmgcs->m_vmstate.framecount - 1];
                }
                VM_DISPATCH();
                VM_CASE(OPC_MAKEARRAY)
                {
                    if(!nn_vmdo_makearray())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();

                VM_CASE(OPC_MAKERANGE)
                {
                    double lower;
                    double upper;
                    Value vupper;
                    Value vlower;
                    vupper = nn_vm_stackpeek(0);
                    vlower = nn_vm_stackpeek(1);
                    if(!vupper.isNumber() || !vlower.isNumber())
                    {
                        nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "invalid range boundaries");
                        VM_DISPATCH();
                    }
                    lower = vlower.asNumber();
                    upper = vupper.asNumber();
                    vmgcs->stackPop(2);
                    vmgcs->stackPush(Value::fromObject(nn_object_makerange(lower, upper)));
                }
                VM_DISPATCH();
                VM_CASE(OPC_MAKEDICT)
                {
                    if(!nn_vmdo_makedict())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_INDEXGETRANGED)
                {
                    if(!nn_vmdo_getrangedindex())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_INDEXGET)
                {
                    if(!nn_vmdo_indexget())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_INDEXSET)
                {
                    if(!nn_vmdo_indexset())
                    {
                        nn_vmmac_exitvm();
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_IMPORTIMPORT)
                {
                    Value res;
                    Value vname;
                    String* name;
                    Module* mod;
                    vname = nn_vm_stackpeek(0);
                    name = vname.asString();
                    fprintf(stderr, "IMPORTIMPORT: name='%s'\n", name->data());
                    mod = Module::loadScriptModule(vmgcs->m_topmodule, name);
                    fprintf(stderr, "IMPORTIMPORT: mod='%p'\n", (void*)mod);
                    if(mod == nullptr)
                    {
                        res = Value::makeNull();
                    }
                    else
                    {
                        res = Value::fromObject(mod);
                    }
                    vmgcs->stackPush(res);
                }
                VM_DISPATCH();
                VM_CASE(OPC_TYPEOF)
                {
                    Value res;
                    Value thing;
                    const char* result;
                    thing = vmgcs->stackPop();
                    result = nn_value_typename(thing, false);
                    res = Value::fromObject(String::copy(result));
                    vmgcs->stackPush(res);
                }
                VM_DISPATCH();
                VM_CASE(OPC_ASSERT)
                {
                    Value message;
                    Value expression;
                    message = vmgcs->stackPop();
                    expression = vmgcs->stackPop();
                    if(expression.isFalse())
                    {
                        if(!message.isNull())
                        {
                            nn_except_throwclass(vmgcs->m_exceptions.asserterror, nn_value_tostring(message)->data());
                        }
                        else
                        {
                            nn_except_throwclass(vmgcs->m_exceptions.asserterror, "assertion failed");
                        }
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_EXTHROW)
                {
                    bool isok;
                    Value peeked;
                    Value stacktrace;
                    Instance* instance;
                    peeked = nn_vm_stackpeek(0);
                    isok = (peeked.isInstance() || nn_util_isinstanceof(peeked.asInstance()->m_instanceclass, vmgcs->m_exceptions.stdexception));
                    if(!isok)
                    {
                        nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "instance of Exception expected");
                        VM_DISPATCH();
                    }
                    stacktrace = nn_except_getstacktrace();
                    instance = peeked.asInstance();
                    instance->defProperty(String::intern("stacktrace"), stacktrace);
                    if(nn_except_propagate())
                    {
                        vmgcs->m_vmstate.currentframe = &vmgcs->m_vmstate.framevalues[vmgcs->m_vmstate.framecount - 1];
                        VM_DISPATCH();
                    }
                    nn_vmmac_exitvm();
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
                    type = nn_vmbits_readstring();
                    addr = nn_vmbits_readshort();
                    finaddr = nn_vmbits_readshort();
                    if(addr != 0)
                    {
                        value = Value::makeNull();
                        if(!vmgcs->m_declaredglobals.get(Value::fromObject(type), &value))
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
                            if(!vmgcs->m_vmstate.currentframe->closure->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.module->deftable.get(Value::fromObject(type), &value) || !value.isClass())
                            {
                                nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "object of type '%s' is not an exception", type->data());
                                VM_DISPATCH();
                            }
                            */
                            exclass = vmgcs->m_exceptions.stdexception;
                        }
                        nn_except_pushhandler(exclass, addr, finaddr);
                    }
                    else
                    {
                        nn_except_pushhandler(nullptr, addr, finaddr);
                    }
                }
                VM_DISPATCH();
                VM_CASE(OPC_EXPOPTRY)
                {
                    vmgcs->m_vmstate.currentframe->m_handlercount--;
                }
                VM_DISPATCH();
                VM_CASE(OPC_EXPUBLISHTRY)
                {
                    vmgcs->m_vmstate.currentframe->m_handlercount--;
                    if(nn_except_propagate())
                    {
                        vmgcs->m_vmstate.currentframe = &vmgcs->m_vmstate.framevalues[vmgcs->m_vmstate.framecount - 1];
                        VM_DISPATCH();
                    }
                    nn_vmmac_exitvm();
                }
                VM_DISPATCH();
                VM_CASE(OPC_SWITCH)
                {
                    Value expr;
                    Value value;
                    Switch* sw;
                    sw = nn_vmbits_readconst().asSwitch();
                    expr = nn_vm_stackpeek(0);
                    if(sw->table.get(expr, &value))
                    {
                        vmgcs->m_vmstate.currentframe->inscode += (int)value.asNumber();
                    }
                    else if(sw->defaultjump != -1)
                    {
                        vmgcs->m_vmstate.currentframe->inscode += sw->defaultjump;
                    }
                    else
                    {
                        vmgcs->m_vmstate.currentframe->inscode += sw->exitjump;
                    }
                    vmgcs->stackPop();
                }
                VM_DISPATCH();
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
        return NEON_STATUS_OK;
    }

    int nn_nestcall_prepare(Value callable, Value mthobj, Value* callarr, int maxcallarr)
    {
        int arity;
        Function* ofn;
        (void)maxcallarr;
        arity = 0;
        ofn = callable.asFunction();
        if(callable.isFuncclosure())
        {
            arity = ofn->m_fnvals.fnclosure.scriptfunc->m_fnvals.fnscriptfunc.arity;
        }
        else if(callable.isFuncscript())
        {
            arity = ofn->m_fnvals.fnscriptfunc.arity;
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
    bool nn_nestcall_callfunction(Value callable, Value thisval, Value* argv, size_t argc, Value* dest, bool fromoper)
    {
        bool needvm;
        size_t i;
        int64_t pidx;
        Status status;
        Value rtv;
        auto gcs = SharedState::get(); 
        pidx = gcs->m_vmstate.stackidx;
        /* set the closure before the args */
        gcs->stackPush(callable);
        if((argv != nullptr) && (argc > 0))
        {
            for(i = 0; i < argc; i++)
            {
                gcs->stackPush(argv[i]);
            }
        }
        if(!nn_vm_callvaluewithobject(callable, thisval, argc, fromoper))
        {
            fprintf(stderr, "nestcall: nn_vm_callvalue() failed\n");
            abort();
        }
        needvm = true;
        if(callable.isFuncnative())
        {
            needvm = false;
        }
        if(needvm)
        {
            status = nn_vm_runvm(gcs->m_vmstate.framecount - 1, nullptr);
            if(status != NEON_STATUS_OK)
            {
                fprintf(stderr, "nestcall: call to runvm failed\n");
                abort();
            }
        }
        rtv = gcs->m_vmstate.stackvalues[gcs->m_vmstate.stackidx - 1];
        *dest = rtv;
        gcs->stackPop(argc + 0);
        gcs->m_vmstate.stackidx = pidx;
        return true;
    }

    void buildPorcessInfo()
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
            pathp = osfn_getcwd(pathbuf, kMaxBuf);
            if(pathp == nullptr)
            {
                pathp = (char*)".";
            }
            gcs->m_processinfo->cliexedirectory = String::copy(pathp);
        }
        {
            gcs->m_processinfo->cliprocessid = osfn_getpid();
        }
        {
            {
                gcs->m_processinfo->filestdout = File::make(stdout, true, "<stdout>", "wb");
                nn_state_defglobalvalue("STDOUT", Value::fromObject(gcs->m_processinfo->filestdout));
            }
            {
                gcs->m_processinfo->filestderr = File::make(stderr, true, "<stderr>", "wb");
                nn_state_defglobalvalue("STDERR", Value::fromObject(gcs->m_processinfo->filestderr));
            }
            {
                gcs->m_processinfo->filestdin = File::make(stdin, true, "<stdin>", "rb");
                nn_state_defglobalvalue("STDIN", Value::fromObject(gcs->m_processinfo->filestdin));
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
            prealpath = osfn_realpath(gcs->m_rootphysfile, nullptr);
            prealdir = osfn_dirname(prealpath);
            gcs->m_processinfo->cliscriptfile = String::copy(prealpath);
            gcs->m_processinfo->cliscriptdirectory = String::copy(prealdir);
            nn_memory_free(prealpath);
            nn_memory_free(prealdir);
        }
        if(gcs->m_processinfo->cliscriptdirectory != nullptr)
        {
            Module::addSearchPathObj(gcs->m_processinfo->cliscriptdirectory);
        }
    }

    bool initState()
    {
        nn_memory_init();
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
        nn_vm_initvmstate();
        nn_state_resetvmstate();
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
            gcs->m_conf.maxsyntaxerrors = NEON_CONFIG_MAXSYNTAXERRORS;
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
            gcs->m_stdoutprinter->shouldflush = false;
            gcs->m_stderrprinter = IOStream::makeIO(stderr, false);
            gcs->m_debugwriter = IOStream::makeIO(stderr, false);
            gcs->m_debugwriter->shortenvalues = true;
            gcs->m_debugwriter->maxvallength = 15;
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
            gcs->m_classprimclass = nn_util_makeclass("Class", nullptr);
            gcs->m_classprimobject = nn_util_makeclass("Object", gcs->m_classprimclass);
            gcs->m_classprimnumber = nn_util_makeclass("Number", gcs->m_classprimobject);
            gcs->m_classprimstring = nn_util_makeclass("String", gcs->m_classprimobject);
            gcs->m_classprimarray = nn_util_makeclass("Array", gcs->m_classprimobject);
            gcs->m_classprimdict = nn_util_makeclass("Dict", gcs->m_classprimobject);
            gcs->m_classprimfile = nn_util_makeclass("File", gcs->m_classprimobject);
            gcs->m_classprimrange = nn_util_makeclass("Range", gcs->m_classprimobject);
            gcs->m_classprimcallable = nn_util_makeclass("Function", gcs->m_classprimobject);
            gcs->m_classprimprocess = nn_util_makeclass("Process", gcs->m_classprimobject);
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
                gcs->m_exceptions.stdexception = nn_except_makeclass(gcs->m_classprimobject, nullptr, "Exception", true);
            }
            gcs->m_exceptions.asserterror = nn_except_makeclass(gcs->m_classprimobject, nullptr, "AssertError", true);
            gcs->m_exceptions.syntaxerror = nn_except_makeclass(gcs->m_classprimobject, nullptr, "SyntaxError", true);
            gcs->m_exceptions.ioerror = nn_except_makeclass(gcs->m_classprimobject, nullptr, "IOError", true);
            gcs->m_exceptions.oserror = nn_except_makeclass(gcs->m_classprimobject, nullptr, "OSError", true);
            gcs->m_exceptions.argumenterror = nn_except_makeclass(gcs->m_classprimobject, nullptr, "ArgumentError", true);
            gcs->m_exceptions.regexerror = nn_except_makeclass(gcs->m_classprimobject, nullptr, "RegexError", true);
            gcs->m_exceptions.importerror = nn_except_makeclass(gcs->m_classprimobject, nullptr, "ImportError", true);
        }
        /* all the other bits .... */
        buildPorcessInfo();
        /* NOW the module paths can be set up */
        nn_state_setupmodulepaths();
        {
            nn_state_initbuiltinfunctions();
            nn_state_initbuiltinmethods();
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
        SharedState::gcLinkedObjectsDestroy();
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
        nn_memory_free(gcs->m_vmstate.framevalues);
        destrdebug("destroying stackvalues...");
        nn_memory_free(gcs->m_vmstate.stackvalues);
        nn_memory_free(gcs->m_processinfo);
        destrdebug("destroying state...");
        SharedState::destroy();
        destrdebug("done destroying!");
        nn_memory_finish();
    }

    Function* compileSource(Module* module, bool fromeval, const char* source, bool toplevel)
    {
        Blob blob;
        Function* function;
        Function* closure;
        (void)toplevel;
        auto gcs = SharedState::get();
        function = nn_astutil_compilesource(module, source, &blob, false, fromeval);
        if(function == nullptr)
        {
            Blob::destroy(&blob);
            return nullptr;
        }
        if(!fromeval)
        {
            gcs->stackPush(Value::fromObject(function));
        }
        else
        {
            function->m_funcname = String::intern("(evaledcode)");
        }
        closure = Function::makeFuncClosure(function, Value::makeNull());
        if(!fromeval)
        {
            gcs->stackPop();
            gcs->stackPush(Value::fromObject(closure));
        }
        Blob::destroy(&blob);
        return closure;
    }

    Status execSource(Module* module, const char* source, const char* filename, Value* dest)
    {
        char* rp;
        Status status;
        Function* closure;
        auto gcs = SharedState::get();
        gcs->m_rootphysfile = filename;
        updateProcessInfo();
        rp = (char*)filename;
        gcs->m_topmodule->physicalpath = String::copy(rp);
        module->setInternFileField();
        closure = neon::compileSource(module, false, source, true);
        if(closure == nullptr)
        {
            return NEON_STATUS_FAILCOMPILE;
        }
        if(gcs->m_conf.exitafterbytecode)
        {
            return NEON_STATUS_OK;
        }
        /*
         * NB. it is a closure, since it's compiled code.
         * so no need to create a Value and call nn_vm_callvalue().
         */
        nn_vm_callclosure(closure, Value::makeNull(), 0, false);
        status = nn_vm_runvm(0, dest);
        return status;
    }

    Value evalSource(const char* source)
    {
        bool ok;
        size_t argc;
        Value callme;
        Value retval;
        Function* closure;
        (void)argc;
        auto gcs = SharedState::get();
        closure = compileSource(gcs->m_topmodule, true, source, false);
        callme = Value::fromObject(closure);
        argc = nn_nestcall_prepare(callme, Value::makeNull(), nullptr, 0);
        ok = nn_nestcall_callfunction(callme, Value::makeNull(), nullptr, 0, &retval, false);
        if(!ok)
        {
            nn_except_throw("eval() failed");
        }
        return retval;
    }
}
// endnamespace

#define OPTTOSTRIFY_(thing) #thing
#define OPTTOSTRIFY(thing) OPTTOSTRIFY_(thing)

#if defined(NEON_CONF_REPLSUPPORT) && (NEON_CONF_REPLSUPPORT == 1)
static char* nn_cli_getinput(linocontext_t* lictx, const char* prompt)
{
    return lino_context_readline(lictx, prompt);
}

static void nn_cli_addhistoryline(linocontext_t* lictx, const char* line)
{
    lino_context_historyadd(lictx, line);
}

static void nn_cli_freeline(linocontext_t* lictx, char* line)
{
    lino_context_freeline(lictx, line);
}

static bool nn_cli_repl(linocontext_t* lictx)
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
    NNStringBuffer* source;
    const char* cursor;
    neon::Value dest;
    neon::IOStream* pr;
    auto gcs = neon::SharedState::get();
    pr = gcs->m_stdoutprinter;
    rescnt = 0;
    gcs->m_isrepl = true;
    continuerepl = true;
    printf("Type \".exit\" to quit or \".credits\" for more information\n");
    source = nn_strbuf_makebasicempty(nullptr, 0);
    bracecount = 0;
    parencount = 0;
    bracketcount = 0;
    singlequotecount = 0;
    doublequotecount = 0;
    lino_context_setmultiline(lictx, 0);
    lino_context_historyadd(lictx, ".exit");
    while(true)
    {
        if(!continuerepl)
        {
            bracecount = 0;
            parencount = 0;
            bracketcount = 0;
            singlequotecount = 0;
            doublequotecount = 0;
            nn_strbuf_reset(source);
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
        line = nn_cli_getinput(lictx, cursor);
        if(line == nullptr || strcmp(line, ".exit") == 0)
        {
            nn_strbuf_destroy(source);
            return true;
        }
        linelength = (int)strlen(line);
        if(strcmp(line, ".credits") == 0)
        {
            printf("\n" NEON_INFO_COPYRIGHT "\n\n");
            nn_strbuf_reset(source);
            continue;
        }
        nn_cli_addhistoryline(lictx, line);
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
        nn_strbuf_appendstr(source, line);
        if(linelength > 0)
        {
            nn_strbuf_appendstr(source, "\n");
        }
        nn_cli_freeline(lictx, line);
        if(bracketcount == 0 && parencount == 0 && bracecount == 0 && singlequotecount == 0 && doublequotecount == 0)
        {
            memset(varnamebuf, 0, kMaxVarName);
            sprintf(varnamebuf, "_%zd", (long)rescnt);
            neon::execSource(gcs->m_topmodule, nn_strbuf_data(source), "<repl>", &dest);
            dest = gcs->m_lastreplvalue;
            if(!dest.isNull())
            {
                pr->format("%s = ", varnamebuf);
                neon::ValPrinter::printValue(pr, dest, true, true);
                nn_state_defglobalvalue(varnamebuf, dest);
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
#endif

static bool nn_cli_runfile(const char* file)
{
    size_t fsz;
    char* source;
    const char* oldfile;
    neon::Status result;
    auto gcs = neon::SharedState::get();
    source = neon::nn_util_filereadfile(file, &fsz, false, 0);
    if(source == nullptr)
    {
        oldfile = file;
        source = neon::nn_util_filereadfile(file, &fsz, false, 0);
        if(source == nullptr)
        {
            fprintf(stderr, "failed to read from '%s': %s\n", oldfile, strerror(errno));
            return false;
        }
    }
    result = neon::execSource(gcs->m_topmodule, source, file, nullptr);
    nn_memory_free(source);
    fflush(stdout);
    if(result == neon::NEON_STATUS_FAILCOMPILE)
    {
        return false;
    }
    if(result == neon::NEON_STATUS_FAILRUNTIME)
    {
        return false;
    }
    return true;
}

static bool nn_cli_runcode(char* source)
{
    auto gcs = neon::SharedState::get();
    gcs->m_rootphysfile = nullptr;
    auto result = neon::execSource(gcs->m_topmodule, source, "<-e>", nullptr);
    fflush(stdout);
    if(result == neon::NEON_STATUS_FAILCOMPILE)
    {
        return false;
    }
    if(result == neon::NEON_STATUS_FAILRUNTIME)
    {
        return false;
    }
    return true;
}

#if defined(NEON_PLAT_ISWASM)
int __multi3(int a, int b)
{
    return a * b;
}
#endif

static int nn_util_findfirstpos(const char* str, size_t len, int ch)
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

static void nn_cli_parseenv(char** envp)
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
        pos = nn_util_findfirstpos(raw, len, '=');
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

static void optprs_fprintmaybearg(FILE* out, const char* begin, const char* flagname, size_t flaglen, bool needval, bool maybeval, const char* delim)
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

static void optprs_fprintusage(FILE* out, optlongflags_t* flags)
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

static void nn_cli_showusage(char* argv[], optlongflags_t* flags, bool fail)
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
    char* arg;
    char* source;
    const char* filename;
    char* nargv[128];
    optcontext_t options;
    #if defined(NEON_CONF_REPLSUPPORT) && (NEON_CONF_REPLSUPPORT == 1)
    linocontext_t lictx;
    #endif
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
        { "gcstart", 'g', OPTPARSE_REQUIRED, "set minimum bytes at which the GC should kick in. 0 disables GC (default is " OPTTOSTRIFY(NEON_CONFIG_DEFAULTGCSTART) ")" },
        { 0, 0, (optargtype_t)0, nullptr }
    };
    #if defined(NEON_CONF_REPLSUPPORT) && (NEON_CONF_REPLSUPPORT == 1)
    lino_context_init(&lictx);
    #endif
#if defined(NEON_PLAT_ISWINDOWS) || defined(_MSC_VER)
    _setmode(fileno(stdin), _O_BINARY);
    _setmode(fileno(stdout), _O_BINARY);
    _setmode(fileno(stderr), _O_BINARY);
#endif
    ok = true;
    wasusage = false;
    quitafterinit = false;
    source = nullptr;
    nextgcstart = NEON_CONFIG_DEFAULTGCSTART;
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
            nn_cli_showusage(argv, longopts, false);
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
            source = options.optarg;
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
    nn_cli_parseenv(envp);
    while(true)
    {
        arg = optprs_nextpositional(&options);
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
        gcs->m_declaredglobals.set(neon::nn_value_copystr("ARGV"), neon::Value::fromObject(gcs->m_processinfo->cliargv));
    }
    gcs->m_gcstate.nextgc = nextgcstart;
    neon::loadBuiltinMethods();
    if(source != nullptr)
    {
        ok = nn_cli_runcode(source);
    }
    else if(nargc > 0)
    {
        auto os = gcs->m_processinfo->cliargv->get(0).asString();
        filename = os->data();
        ok = nn_cli_runfile(filename);
    }
    else
    {
        #if defined(NEON_CONF_REPLSUPPORT) && (NEON_CONF_REPLSUPPORT == 1)
            ok = nn_cli_repl(&lictx);
        #else
            ok = false;
            fprintf(stderr, "REPL not compiled in\n");
        #endif
    }
cleanup:
    neon::destroyState();
    if(ok)
    {
        return 0;
    }
    return 1;
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
