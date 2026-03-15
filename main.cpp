
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

#include "mem.h"
#include "mrx.h"
#include "strbuf.h"
#include "oslib.h"
#include "optparse.h"
#include "lino.h"

#if defined(NEON_PLAT_ISWINDOWS)
    #include <sys/utime.h>
    #include <fcntl.h>
    #include <io.h>
    #include <winsock2.h>
#else
    #include <sys/time.h>
    #include <unistd.h>
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

#if 0
    #if defined(__STRICT_ANSI__)
        #define NEON_INLINE
        #define NEON_FORCEINLINE
        #define inline
    #else
        #define NEON_INLINE inline
        #if defined(__GNUC__) || defined(__TINYC__)
            #define NEON_FORCEINLINE __attribute__((always_inline)) inline
        #else
            #define NEON_FORCEINLINE inline
        #endif
    #endif
#else
    #define NEON_INLINE
    #define NEON_FORCEINLINE
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

/* how deep template strings can be nested (i.e., "foo${getBar("quux${getBonk("...")}")}") */
#define NEON_CONFIG_ASTMAXSTRTPLDEPTH (8)

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

#define nn_except_throwclass(exklass, ...) nn_except_throwactual(exklass, __FILE__, __LINE__, __VA_ARGS__)

#define nn_except_throw(...) nn_except_throwclass(SharedState::get()->m_exceptions.stdexception, __VA_ARGS__)

#define NEON_RETURNERROR(scfn, ...)               \
    {                                       \
        SharedState::get()->stackPop(scfn.argc); \
        nn_except_throw(__VA_ARGS__);       \
    }                                       \
    return NNValue::makeBool(false);

#define NEON_ARGS_FAIL(chp, ...) nn_argcheck_fail((chp), __FILE__, __LINE__, __VA_ARGS__)

/* check for exact number of arguments $d */
#define NEON_ARGS_CHECKCOUNT(chp, d)                                                                    \
    if(nn_util_unlikely((chp)->m_scriptfnctx.argc != (d)))                                                            \
    {                                                                                                   \
        return NEON_ARGS_FAIL(chp, "%s() expects %d arguments, %d given", (chp)->name, d, (chp)->m_scriptfnctx.argc); \
    }

/* check for miminum args $d ($d ... n) */
#define NEON_ARGS_CHECKMINARG(chp, d)                                                                              \
    if(nn_util_unlikely((chp)->m_scriptfnctx.argc < (d)))                                                                        \
    {                                                                                                              \
        return NEON_ARGS_FAIL(chp, "%s() expects minimum of %d arguments, %d given", (chp)->name, d, (chp)->m_scriptfnctx.argc); \
    }

/* check for range of args ($low .. $up) */
#define NEON_ARGS_CHECKCOUNTRANGE(chp, low, up)                                                                              \
    if((int(nn_util_unlikely((chp)->m_scriptfnctx.argc) < int(low)) || (int((chp)->m_scriptfnctx.argc) > int(up))))                                                          \
    {                                                                                                                        \
        return NEON_ARGS_FAIL(chp, "%s() expects between %d and %d arguments, %d given", (chp)->name, low, up, (chp)->m_scriptfnctx.argc); \
    }

/* check for argument at index $i for $type, where $type is a nn_value_is*() function */
#if 1
    #define NEON_ARGS_CHECKTYPE(chp, i, typefunc)
#else
    #define NEON_ARGS_CHECKTYPE(chp, i, typefunc)                                                                                                                                               \
        if(nn_util_unlikely(!typefunc((chp)->m_scriptfnctx.argv[i])))                                                                                                                                         \
        {                                                                                                                                                                                       \
            return NEON_ARGS_FAIL(chp, "%s() expects argument %d as %s, %s given", (chp)->name, (i) + 1, nn_value_typefromfunction(typefunc), nn_value_typename((chp)->m_scriptfnctx.argv[i], false), false); \
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




enum NNStatus
{
    NEON_STATUS_OK,
    NEON_STATUS_FAILCOMPILE,
    NEON_STATUS_FAILRUNTIME
};



enum NNColor
{
    NEON_COLOR_RESET,
    NEON_COLOR_RED,
    NEON_COLOR_GREEN,
    NEON_COLOR_YELLOW,
    NEON_COLOR_BLUE,
    NEON_COLOR_MAGENTA,
    NEON_COLOR_CYAN
};

enum NNAstCompContext
{
    NEON_COMPCONTEXT_NONE,
    NEON_COMPCONTEXT_CLASS,
    NEON_COMPCONTEXT_ARRAY,
    NEON_COMPCONTEXT_NESTEDFUNCTION
};

class /**/ NNValue;
class /**/ NNObject;
class /**/ NNAstParser;
class /**/ NNAstToken;
class /**/ DefExport;
class /**/ NNObjString;
class /**/ NNObjArray;
class /**/ NNProperty;
class /**/ NNIOStream;
class /**/ NNBlob;
class /**/ NNObjFunction;
class /**/ NNObjDict;
class /**/ NNObject;
class /**/ NNObjFile;
class /**/ NNObjInstance;
class /**/ NNObjClass;
class /**/ NNIOResult;
class /**/ NNInstruction;
class /**/ NNAstLexer;
class /**/ NNFormatInfo;
class /**/ utf8iterator_t;
class /**/ NNConstClassMethodItem;
class /**/ NNObjModule;
class /**/ NNObjUpvalue;
class /**/ NNObjUserdata;
class /**/ NNObjSwitch;
class /**/ NNObjDict;
class /**/ NNObjRange;
class /**/ NNFuncContext;
class NNArgCheck;
template <typename StoredType> class NNValArray;

template <typename HTKeyT, typename HTValT> class NNHashTable;

typedef bool(*NNValIsFuncFN);
typedef NNValue (*NNNativeFN)(const NNFuncContext&);
typedef void (*NNPtrFreeFN)(void*);
typedef bool (*NNAstParsePrefixFN)(NNAstParser*, bool);
typedef bool (*NNAstParseInfixFN)(NNAstParser*, NNAstToken, bool);
typedef NNValue (*NNClassFieldFN)();
typedef void (*NNModLoaderFN)();
typedef DefExport* (*NNModInitFN)();
typedef NNValue (*nnbinopfunc_t)(double, double);

typedef size_t (*mcitemhashfn_t)(void*);
typedef bool (*mcitemcomparefn_t)(void*, void*);
typedef int (*NNStrBufCharModFunc)(int);

template <typename ClassT>
concept MemoryClassHasDestroyFunc = requires(ClassT* ptr) {
    { ClassT::destroy(ptr) } -> std::same_as<void>;
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



size_t nn_string_getlength(NNObjString* os);
const char* nn_string_getdata(NNObjString* os);
/* allocator.c */
void* nn_allocator_create();
void nn_allocator_destroy(void* msp);
void* nn_allocuser_malloc(void* msp, size_t nsize);
void* nn_allocuser_free(void* msp, void* ptr);
void* nn_allocuser_realloc(void* msp, void* ptr, size_t nsize);
/* core.c */
void nn_argcheck_init(NNArgCheck* ch, const char* name, const NNFuncContext&);



void nn_state_installmethods(NNObjClass* klass, NNConstClassMethodItem* listmethods);
void nn_state_initbuiltinmethods();
NNValue nn_except_getstacktrace();
bool nn_except_propagate();
bool nn_except_pushhandler(NNObjClass* type, int address, int finallyaddress);
NNObjClass* nn_except_makeclass(NNObjClass* baseclass, NNObjModule* module, const char* cstrname, bool iscs);
NNObjInstance* nn_except_makeinstance(NNObjClass* exklass, const char* srcfile, int srcline, NNObjString* message);
bool nn_state_defglobalvalue(const char* name, NNValue val);
bool nn_state_defnativefunctionptr(const char* name, NNNativeFN fptr, void* uptr);
bool nn_state_defnativefunction(const char* name, NNNativeFN fptr);
NNObjClass* nn_util_makeclass(const char* name, NNObjClass* parent);
void nn_state_buildprocessinfo();
void nn_state_updateprocessinfo();
bool nn_util_methodisprivate(NNObjString* name);
NNObjFunction* nn_state_compilesource(NNObjModule* module, bool fromeval, const char* source, bool toplevel);
NNStatus nn_state_execsource(NNObjModule* module, const char* source, const char* filename, NNValue* dest);
NNValue nn_state_evalsource(const char* source);
/* dbg.c */
void nn_dbg_disasmblob(NNIOStream* pr, NNBlob* blob, const char* name);
void nn_dbg_printinstrname(NNIOStream* pr, const char* name);
int nn_dbg_printsimpleinstr(NNIOStream* pr, const char* name, int offset);
int nn_dbg_printconstinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset);
int nn_dbg_printpropertyinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset);
int nn_dbg_printshortinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset);
int nn_dbg_printbyteinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset);
int nn_dbg_printjumpinstr(NNIOStream* pr, const char* name, int sign, NNBlob* blob, int offset);
int nn_dbg_printtryinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset);
int nn_dbg_printinvokeinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset);
const char* nn_dbg_op2str(uint16_t instruc);
int nn_dbg_printclosureinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset);
int nn_dbg_printinstructionat(NNIOStream* pr, NNBlob* blob, int offset);

void nn_iostream_printvaltable(NNIOStream* pr, NNHashTable<NNValue, NNValue>* table, const char* name);
void nn_iostream_printfunction(NNIOStream* pr, NNObjFunction* func);
void nn_iostream_printarray(NNIOStream* pr, NNObjArray* list);
void nn_iostream_printdict(NNIOStream* pr, NNObjDict* dict);
void nn_iostream_printfile(NNIOStream* pr, NNObjFile* file);
void nn_iostream_printinstance(NNIOStream* pr, NNObjInstance* instance, bool invmethod);
void nn_iostream_printtable(NNIOStream* pr, NNHashTable<NNValue, NNValue>* table);
void nn_iostream_printobjclass(NNIOStream* pr, NNValue value, bool fixstring, bool invmethod);
void nn_iostream_printobject(NNIOStream* pr, NNValue value, bool fixstring, bool invmethod);
void nn_iostream_printnumber(NNIOStream* pr, NNValue value);
void nn_iostream_printvalue(NNIOStream* pr, NNValue value, bool fixstring, bool invmethod);
NNObjFunction* nn_object_makefuncscript(NNObjModule* module, int type);

NNObjArray* nn_array_makefilled(size_t cnt, NNValue filler);
NNObjArray* nn_array_make();
NNObjArray* nn_object_makearray();
void nn_array_push(NNObjArray* list, NNValue value);
bool nn_array_get(NNObjArray* list, size_t idx, NNValue* vdest);
NNObjArray* nn_array_copy(NNObjArray* list, long start, long length);
void nn_state_installobjectarray();
/* libclass.c */
NNObjClass* nn_object_makeclass(NNObjString* name, NNObjClass* parent);
void nn_class_destroy(NNObjClass* klass);
bool nn_class_inheritfrom(NNObjClass* subclass, NNObjClass* superclass);
bool nn_class_defproperty(NNObjClass* klass, NNObjString* cstrname, NNValue val);
bool nn_class_defcallablefieldptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr);
bool nn_class_defcallablefield(NNObjClass* klass, NNObjString* name, NNNativeFN function);
bool nn_class_defstaticcallablefieldptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr);
bool nn_class_defstaticcallablefield(NNObjClass* klass, NNObjString* name, NNNativeFN function);
bool nn_class_setstaticproperty(NNObjClass* klass, NNObjString* name, NNValue val);
bool nn_class_defnativeconstructorptr(NNObjClass* klass, NNNativeFN function, void* uptr);
bool nn_class_defnativeconstructor(NNObjClass* klass, NNNativeFN function);
bool nn_class_defmethod(NNObjClass* klass, NNObjString* name, NNValue val);
bool nn_class_defnativemethodptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* ptr);
bool nn_class_defnativemethod(NNObjClass* klass, NNObjString* name, NNNativeFN function);
bool nn_class_defstaticnativemethodptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr);
bool nn_class_defstaticnativemethod(NNObjClass* klass, NNObjString* name, NNNativeFN function);
NNProperty* nn_class_getmethodfield(NNObjClass* klass, NNObjString* name);
NNProperty* nn_class_getpropertyfield(NNObjClass* klass, NNObjString* name);
NNProperty* nn_class_getstaticproperty(NNObjClass* klass, NNObjString* name);
NNProperty* nn_class_getstaticmethodfield(NNObjClass* klass, NNObjString* name);
NNObjInstance* nn_object_makeinstancesize(NNObjClass* klass, size_t sz);
NNObjInstance* nn_object_makeinstance(NNObjClass* klass);
void nn_instance_mark(NNObjInstance* instance);
void nn_instance_destroy(NNObjInstance* instance);
bool nn_instance_defproperty(NNObjInstance* instance, NNObjString* name, NNValue val);
NNProperty* nn_instance_getvar(NNObjInstance* inst, NNObjString* name);
NNProperty* nn_instance_getvarcstr(NNObjInstance* inst, const char* name);
NNProperty* nn_instance_getmethod(NNObjInstance* inst, NNObjString* name);
NNProperty* nn_instance_getmethodcstr(NNObjInstance* inst, const char* name);
/* libdict.c */
NNObjDict* nn_object_makedict();
void nn_dict_destroy(NNObjDict* dict);
bool nn_dict_setentry(NNObjDict* dict, NNValue key, NNValue value);
void nn_dict_addentry(NNObjDict* dict, NNValue key, NNValue value);
void nn_dict_addentrycstr(NNObjDict* dict, const char* ckey, NNValue value);
NNProperty* nn_dict_getentry(NNObjDict* dict, NNValue key);
NNObjDict* nn_dict_copy(NNObjDict* dict);
void nn_state_installobjectdict();
/* libfile.c */
NNObjFile* nn_object_makefile(FILE* handle, bool isstd, const char* path, const char* mode);
void nn_file_destroy(NNObjFile* file);
void nn_file_mark(NNObjFile* file);
bool nn_file_read(NNObjFile* file, size_t readhowmuch, NNIOResult* dest);
int nn_fileobject_close(NNObjFile* file);
bool nn_fileobject_open(NNObjFile* file);
void nn_state_installobjectfile();
/* libfunc.c */
NNObjFunction* nn_object_makefuncbound(NNValue receiver, NNObjFunction* method);
void nn_funcscript_destroy(NNObjFunction* ofn);
NNObjFunction* nn_object_makefuncnative(NNNativeFN natfn, const char* name, void* uptr);
NNObjFunction* nn_object_makefuncclosure(NNObjFunction* innerfn, NNValue thisval);
/* libmodule.c */
void nn_import_loadbuiltinmodules();
bool nn_state_addmodulesearchpathobj(NNObjString* os);
bool nn_state_addmodulesearchpath(const char* path);
void nn_state_setupmodulepaths();
void nn_module_setfilefield(NNObjModule* module);
void nn_module_destroy(NNObjModule* module);
NNObjModule* nn_import_loadmodulescript(NNObjModule* intomodule, NNObjString* modulename);
char* nn_import_resolvepath(const char* modulename, const char* currentfile, char* rootfile, bool isrelative);
bool nn_import_loadnativemodule(NNModInitFN init_fn, char* importname, const char* source, void* dlw);
void nn_import_addnativemodule(NNObjModule* module, const char* as);
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
/* libstring.c */
size_t nn_strutil_rndup2pow64(uint64_t x);
size_t nn_strutil_splitstr(char* str, char sep, char** ptrs, size_t nptrs);
size_t nn_strutil_charreplace(char* str, char from, char to);
void nn_strutil_reverseregion(char* str, size_t length);
bool nn_strutil_isallspace(const char* s);
char* nn_strutil_nextspace(char* s);
char* nn_strutil_trim(char* str);
size_t nn_strutil_chomp(char* str, size_t len);
size_t nn_strutil_countchar(const char* str, char c);
size_t nn_strutil_split(const char* splitat, const char* sourcetxt, char*** result);
void nn_strutil_callboundscheckinsert(NNStringBuffer* sb, size_t pos, const char* file, int line);
void nn_strutil_callboundscheckreadrange(NNStringBuffer* sb, size_t start, size_t len, const char* file, int line);
void nn_strutil_faststrncat(char* dest, const char* src, size_t* size);
size_t nn_strutil_strreplace1(char** str, size_t selflen, const char* findstr, size_t findlen, const char* substr, size_t sublen);
size_t nn_strutil_strrepcount(const char* str, size_t slen, const char* findstr, size_t findlen, size_t sublen);
void nn_strutil_strreplace2(char* target, size_t tgtlen, const char* findstr, size_t findlen, const char* substr, size_t sublen);
bool nn_strutil_inpreplhelper(char* dest, const char* src, size_t srclen, int findme, const char* substr, size_t sublen, size_t maxlen, size_t* dlen);
size_t nn_strutil_inpreplace(char* target, size_t tgtlen, int findme, const char* substr, size_t sublen, size_t maxlen);
NNStringBuffer* nn_strbuf_makelongfromptr(NNStringBuffer* sb, size_t len);
bool nn_strbuf_initbasicempty(NNStringBuffer* sb, const char* str, size_t len, bool onstack);
bool nn_strbuf_makebasicemptystack(NNStringBuffer* sb, const char* str, size_t len);
NNStringBuffer* nn_strbuf_makebasicempty(const char* str, size_t len);
bool nn_strbuf_destroyfromstack(NNStringBuffer* sb);
bool nn_strbuf_destroy(NNStringBuffer* sb);
void nn_strbuf_reset(NNStringBuffer* sb);
bool nn_strbuf_ensurecapacity(NNStringBuffer* sb, size_t len);
bool nn_strbuf_resize(NNStringBuffer* sb, size_t newlen);
bool nn_strbuf_setlength(NNStringBuffer* sb, size_t len);
bool nn_strbuf_setdata(NNStringBuffer* sb, char* str);
size_t nn_strbuf_length(NNStringBuffer* sb);
const char* nn_strbuf_data(NNStringBuffer* sb);
int nn_strbuf_get(NNStringBuffer* sb, size_t idx);
bool nn_strbuf_containschar(NNStringBuffer* sb, char ch);
bool nn_strbuf_fullreplace(NNStringBuffer* sb, const char* findstr, size_t findlen, const char* substr, size_t sublen);
bool nn_strbuf_charreplace(NNStringBuffer* sb, int findme, const char* substr, size_t sublen);
bool nn_strbuf_set(NNStringBuffer* sb, size_t idx, int b);
bool nn_strbuf_appendchar(NNStringBuffer* sb, int c);
bool nn_strbuf_appendstrn(NNStringBuffer* sb, const char* str, size_t len);
bool nn_strbuf_appendstr(NNStringBuffer* sb, const char* str);
bool nn_strbuf_appendbuff(NNStringBuffer* sb1, NNStringBuffer* sb2);
size_t nn_strutil_numofdigits(unsigned long v);
bool nn_strbuf_appendnumulong(NNStringBuffer* sb, unsigned long value);
bool nn_strbuf_appendnumlong(NNStringBuffer* sb, long value);
bool nn_strbuf_appendnumint(NNStringBuffer* sb, int value);
bool nn_strbuf_appendstrnlowercase(NNStringBuffer* sb, const char* str, size_t len);
bool nn_strbuf_appendstrnuppercase(NNStringBuffer* sb, const char* str, size_t len);
void nn_strbuf_shrink(NNStringBuffer* sb, size_t len);
size_t nn_strbuf_chomp(NNStringBuffer* sb);
void nn_strbuf_reverse(NNStringBuffer* sb);
char* nn_strbuf_substr(NNStringBuffer* sb, size_t start, size_t len);
void nn_strbuf_touppercase(NNStringBuffer* sb);
void nn_strbuf_tolowercase(NNStringBuffer* sb);
void nn_strbuf_copyover(NNStringBuffer* sb, size_t dstpos, const char* src, size_t len);
void nn_strbuf_insert(NNStringBuffer* sb, size_t dstpos, const char* src, size_t len);
void nn_strbuf_overwrite(NNStringBuffer* sb, size_t dstpos, size_t dstlen, const char* src, size_t srclen);
void nn_strbuf_erase(NNStringBuffer* sb, size_t pos, size_t len);
int nn_strbuf_appendformatposv(NNStringBuffer* sb, size_t pos, const char* fmt, va_list argptr);
int nn_strbuf_appendformatv(NNStringBuffer* sb, const char* fmt, va_list argptr);
int nn_strbuf_appendformat(NNStringBuffer* sb, const char* fmt, ...);
int nn_strbuf_appendformatat(NNStringBuffer* sb, size_t pos, const char* fmt, ...);
int nn_strbuf_appendformatnoterm(NNStringBuffer* sb, size_t pos, const char* fmt, ...);
void nn_strbuf_triminplace(NNStringBuffer* sb);
void nn_strbuf_trimleftinplace(NNStringBuffer* sb, const char* list);
void nn_strbuf_trimrightinplace(NNStringBuffer* sb, const char* list);
double nn_string_tabhashvaluecombine(const char* data, size_t len, uint32_t hsv);
void nn_string_strtabstore(NNObjString* os);
NNObjString* nn_string_strtabfind(const char* str, size_t len, uint32_t hsv);
NNObjString* nn_string_makefromstrbuf(NNStringBuffer buf, uint32_t hsv, size_t length);
void nn_string_destroy(NNObjString* str);
NNObjString* nn_string_internlen(const char* strdata, int length);
NNObjString* nn_string_intern(const char* strdata);
NNObjString* nn_string_takelen(char* strdata, int length);
NNObjString* nn_string_takecstr(char* strdata);
NNObjString* nn_string_copylen(const char* strdata, int length);
NNObjString* nn_string_copycstr(const char* strdata);
NNObjString* nn_string_copyobject(NNObjString* origos);
const char* nn_string_getdata(NNObjString* os);
char* nn_string_mutdata(NNObjString* os);
size_t nn_string_getlength(NNObjString* os);
bool nn_string_setlength(NNObjString* os, size_t nlen);
bool nn_string_set(NNObjString* os, size_t idx, int byte);
int nn_string_get(NNObjString* os, size_t idx);
bool nn_string_appendstringlen(NNObjString* os, const char* str, size_t len);
bool nn_string_appendstring(NNObjString* os, const char* str);
bool nn_string_appendobject(NNObjString* os, NNObjString* other);
bool nn_string_appendbyte(NNObjString* os, int ch);
bool nn_string_appendnumulong(NNObjString* os, unsigned long val);
bool nn_string_appendnumint(NNObjString* os, int val);
NNObjString* nn_string_substrlen(NNObjString* os, size_t start, size_t maxlen);
NNObjString* nn_string_substr(NNObjString* os, size_t start);
NNObjString* nn_string_substring(NNObjString* selfstr, size_t start, size_t end, bool likejs);
void nn_state_installobjectstring();
/* main.c */
int main(int argc, char* argv[], char** envp);
int replmain(const char* file);
/* mem.c */
void nn_memory_init();
void nn_memory_finish();
void* nn_memory_setsize(void* p, size_t sz);
size_t nn_memory_getsize(void* p);
void* nn_memory_malloc(size_t sz);
void* nn_memory_realloc(void* p, size_t nsz);
void* nn_memory_calloc(size_t count, size_t typsize);
void nn_memory_free(void* ptr);
NNObject* nn_gcmem_protect(NNObject* object);

void nn_gcmem_markvalue(NNValue value);

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
NNObjUpvalue* nn_object_makeupvalue(NNValue* slot, int stackpos);
NNObjUserdata* nn_object_makeuserdata(void* pointer, const char* name);
NNObjModule* nn_module_make(const char* name, const char* file, bool imported, bool retain);
NNObjSwitch* nn_object_makeswitch();
NNObjRange* nn_object_makerange(int lower, int upper);



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
/* utilfmt.c */
void nn_strformat_init(NNFormatInfo* nfi, NNIOStream* writer, const char* fmtstr, size_t fmtlen);
void nn_strformat_destroy(NNFormatInfo* nfi);
bool nn_strformat_format(NNFormatInfo* nfi, size_t argc, size_t argbegin, NNValue* argv);
/* utilhmap.c */
void nn_valtable_mark(NNHashTable<NNValue, NNValue>* table);
void nn_valtable_removewhites(NNHashTable<NNValue, NNValue>* table);
/* utilstd.c */
size_t nn_util_rndup2pow64(uint64_t x);
const char* nn_util_color(NNColor tc);
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
NNObjString* nn_util_numbertobinstring(long n);
NNObjString* nn_util_numbertooctstring(int64_t n, bool numeric);
NNObjString* nn_util_numbertohexstring(int64_t n, bool numeric);
uint32_t nn_object_hashobject(NNObject* object);
uint32_t nn_value_hashvalue(NNValue value);
/* value.c */
NNValue nn_value_copystrlen(const char* str, size_t len);
NNValue nn_value_copystr(const char* str);
NNObjString* nn_value_tostring(NNValue value);
const char* nn_value_objecttypename(NNObject* object, bool detailed);
const char* nn_value_typename(NNValue value, bool detailed);
bool nn_value_compobjarray(NNObject* oa, NNObject* ob);
bool nn_value_compobjstring(NNObject* oa, NNObject* ob);
bool nn_value_compobjdict(NNObject* oa, NNObject* ob);
bool nn_value_compobject(NNValue a, NNValue b);
bool nn_value_compare_actual(NNValue a, NNValue b);
bool nn_value_compare(NNValue a, NNValue b);
NNValue nn_value_findgreater(NNValue a, NNValue b);
void nn_value_sortvalues(NNValue* values, int count);
NNValue nn_value_copyvalue(NNValue value);
/* vm.c */
void nn_vm_initvmstate();
void nn_state_resetvmstate();
bool nn_vm_callclosure(NNObjFunction* closure, NNValue thisval, size_t argcount, bool fromoperator);
bool nn_vm_callvaluewithobject(NNValue callable, NNValue thisval, size_t argcount, bool fromoper);
bool nn_vm_callvalue(NNValue callable, NNValue thisval, size_t argcount, bool fromoperator);
NNObjClass* nn_value_getclassfor(NNValue receiver);

NNValue nn_vm_stackpeek(int distance);
bool nn_util_isinstanceof(NNObjClass* klass1, NNObjClass* expected);
NNStatus nn_vm_runvm(int exitframe, NNValue* rv);
int nn_nestcall_prepare(NNValue callable, NNValue mthobj, NNValue* callarr, int maxcallarr);
bool nn_nestcall_callfunction(NNValue callable, NNValue thisval, NNValue* argv, size_t argc, NNValue* dest, bool fromoper);
/* strbuf.h */
size_t nn_strutil_rndup2pow64(uint64_t x);
size_t nn_strutil_splitstr(char* str, char sep, char** ptrs, size_t nptrs);
size_t nn_strutil_charreplace(char* str, char from, char to);
void nn_strutil_reverseregion(char* str, size_t length);
char* nn_strutil_nextspace(char* s);
char* nn_strutil_trim(char* str);
size_t nn_strutil_chomp(char* str, size_t len);
size_t nn_strutil_countchar(const char* str, char c);
size_t nn_strutil_split(const char* splitat, const char* sourcetxt, char*** result);
void nn_strutil_faststrncat(char* dest, const char* src, size_t* size);
size_t nn_strutil_strreplace1(char** str, size_t selflen, const char* findstr, size_t findlen, const char* substr, size_t sublen);
size_t nn_strutil_strrepcount(const char* str, size_t slen, const char* findstr, size_t findlen, size_t sublen);
void nn_strutil_strreplace2(char* target, size_t tgtlen, const char* findstr, size_t findlen, const char* substr, size_t sublen);
size_t nn_strutil_inpreplace(char* target, size_t tgtlen, int findme, const char* substr, size_t sublen, size_t maxlen);
size_t nn_strutil_numofdigits(unsigned long v);

/* topproto */

union NNUtilDblUnion
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

NEON_FORCEINLINE uint32_t nn_util_hashbits(uint64_t hs)
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

NEON_FORCEINLINE uint32_t nn_util_hashdouble(double value)
{
    NNUtilDblUnion bits;
    bits.num = value;
    return nn_util_hashbits(bits.bits);
}

NEON_FORCEINLINE uint32_t nn_util_hashstring(const char* str, size_t length)
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

class NNConstClassMethodItem
{
    public:
        const char* name;
        NNNativeFN fn;
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

class NNIOResult
{
    public:
        uint8_t success;
        char* data;
        size_t length;
};

class NNIOStream
{
    public:
        enum Mode
        {
            PRMODE_UNDEFINED,
            PRMODE_STRING,
            PRMODE_FILE
        };

    public:
        static bool makeStackIO(NNIOStream* pr, FILE* fh, bool shouldclose)
        {
            initStreamVars(pr, PRMODE_FILE);
            pr->fromstack = true;
            pr->handle = fh;
            pr->shouldclose = shouldclose;
            return true;
        }

        static bool makeStackString(NNIOStream* pr)
        {
            initStreamVars(pr, PRMODE_STRING);
            pr->fromstack = true;
            pr->wrmode = PRMODE_STRING;
            nn_strbuf_makebasicemptystack(&pr->strbuf, nullptr, 0);
            return true;
        }

        static NNIOStream* makeUndefStream(Mode mode)
        {
            NNIOStream* pr;
            pr = Memory::make<NNIOStream>();
            if(!pr)
            {
                fprintf(stderr, "cannot allocate NNIOStream\n");
                return nullptr;
            }
            initStreamVars(pr, mode);
            return pr;
        }

        static NNIOStream* makeIO(FILE* fh, bool shouldclose)
        {
            NNIOStream* pr;
            pr = makeUndefStream(PRMODE_FILE);
            pr->handle = fh;
            pr->shouldclose = shouldclose;
            return pr;
        }

        static void destroy(NNIOStream* pr)
        {
            if(pr == nullptr)
            {
                return;
            }
            if(pr->wrmode == PRMODE_UNDEFINED)
            {
                return;
            }
            /*fprintf(stderr, "NNIOStream::destroy: pr->wrmode=%d\n", pr->wrmode);*/
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

        static void initStreamVars(NNIOStream* pr, Mode mode)
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

        NNObjString* takeString()
        {
            size_t xlen;
            NNObjString* os;
            xlen = nn_strbuf_length(&this->strbuf);
            os = nn_string_makefromstrbuf(this->strbuf, nn_util_hashstring(nn_strbuf_data(&this->strbuf), xlen), xlen);
            this->stringtaken = true;
            return os;
        }

        NNObjString* copyString()
        {
            NNObjString* os;
            os = nn_string_copylen(nn_strbuf_data(&this->strbuf), nn_strbuf_length(&this->strbuf));
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

class NNFormatInfo
{
    public:
        /* length of the format string */
        size_t fmtlen;
        /* the actual format string */
        const char* fmtstr;
        NNIOStream* writer;
};

class NNObject
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
        static void markObject(NNObject* object);

        static void blackenObject(NNObject* object);

        static void destroyObject(NNObject* object);




    public:
        Type type;
        bool mark;
        /*
        // when an object is marked as stale, it means that the
        // GC will never collect this object. This can be useful
        // for library/package objects that want to reuse native
        // objects in their types/pointers. The GC cannot reach
        // them yet, so it's best for them to be kept stale.
        */
        bool stale;
        NNObject* next;
};

class NNValue
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
        static void markValArray(NNValArray<NNValue>* list);
        static void markDict(NNObjDict* dict);

    public:
        Type type;
        union
        {
            uint8_t vbool;
            double vfltnum;
            NNObject* vobjpointer;
        } valunion;

    public:
        static NEON_INLINE NNValue makeTypedValue(Type type)
        {
            NNValue v;
            memset(&v, 0, sizeof(NNValue));
            v.type = type;
            return v;
        }

        template<typename InputT>
        static NEON_INLINE NNValue fromObject(InputT* obj)
        {
            NNValue v;
            v = makeTypedValue(VT_OBJ);
            v.valunion.vobjpointer = obj;
            return v;
        }


        static NEON_INLINE NNValue makeNull()
        {
            NNValue v;
            v = makeTypedValue(VT_NULL);
            return v;
        }

        static NEON_INLINE NNValue makeBool(bool b)
        {
            NNValue v;
            v = makeTypedValue(VT_BOOL);
            v.valunion.vbool = b;
            return v;
        }

        static NEON_INLINE NNValue makeNumber(double d)
        {
            NNValue v;
            v = makeTypedValue(VT_NUMBER);
            v.valunion.vfltnum = d;
            return v;
        }

    public:
        NEON_INLINE NNObject::Type objType() const
        {
            return asObject()->type;
        }

        NEON_INLINE NNObjFunction* asFunction() const
        {
            return ((NNObjFunction*)asObject());
        }

        NEON_INLINE NNObjClass* asClass() const
        {
            return ((NNObjClass*)asObject());
        }

        NEON_INLINE NNObjInstance* asInstance() const
        {
            return ((NNObjInstance*)asObject());
        }

        NEON_INLINE NNObjSwitch* asSwitch() const
        {
            return ((NNObjSwitch*)asObject());
        }

        NEON_INLINE NNObjUserdata* asUserdata() const
        {
            return ((NNObjUserdata*)asObject());
        }

        NEON_INLINE NNObjModule* asModule() const
        {
            return ((NNObjModule*)asObject());
        }

        NEON_INLINE NNObjFile* asFile() const
        {
            return ((NNObjFile*)asObject());
        }

        NEON_INLINE NNObjRange* asRange() const
        {
            return ((NNObjRange*)asObject());
        }


        NEON_INLINE bool isNull() const
        {
            return (this->type == VT_NULL);
        }

        NEON_INLINE bool isObject() const
        {
            return (this->type == VT_OBJ);
        }

        NEON_INLINE bool isObjtype(NNObject::Type t) const
        {
            return isObject() && (asObject()->type == t);
        }

        NEON_INLINE bool isBool() const
        {
            return (this->type == VT_BOOL);
        }

        NEON_INLINE bool isNumber() const
        {
            return (this->type == VT_NUMBER);
        }

        NEON_INLINE bool isString() const
        {
            return isObjtype(NNObject::OTYP_STRING);
        }

        NEON_INLINE bool isFuncnative() const
        {
            return isObjtype(NNObject::OTYP_FUNCNATIVE);
        }

        NEON_INLINE bool isFuncscript() const
        {
            return isObjtype(NNObject::OTYP_FUNCSCRIPT);
        }

        NEON_INLINE bool isFuncclosure() const
        {
            return isObjtype(NNObject::OTYP_FUNCCLOSURE);
        }

        NEON_INLINE bool isFuncbound() const
        {
            return isObjtype(NNObject::OTYP_FUNCBOUND);
        }

        NEON_INLINE bool isClass() const
        {
            return isObjtype(NNObject::OTYP_CLASS);
        }

        NEON_INLINE bool isInstance() const
        {
            return isObjtype(NNObject::OTYP_INSTANCE);
        }

        NEON_INLINE bool isArray() const
        {
            return isObjtype(NNObject::OTYP_ARRAY);
        }

        NEON_INLINE bool isDict() const
        {
            return isObjtype(NNObject::OTYP_DICT);
        }

        NEON_INLINE bool isFile() const
        {
            return isObjtype(NNObject::OTYP_FILE);
        }

        NEON_INLINE bool isRange() const
        {
            return isObjtype(NNObject::OTYP_RANGE);
        }

        NEON_INLINE bool isModule() const
        {
            return isObjtype(NNObject::OTYP_MODULE);
        }

        NEON_INLINE bool isCallable() const
        {
            return (isClass() || isFuncscript() || isFuncclosure() || isFuncbound() || isFuncnative());
        }

        NEON_INLINE NNObject* asObject() const
        {
            return (valunion.vobjpointer);
        }

        NEON_INLINE double asNumber() const
        {
            return (this->valunion.vfltnum);
        }

        NEON_INLINE bool asBool() const
        {
            if(isNumber())
            {
                return asNumber();
            }
            return (this->valunion.vbool);
        }

        NEON_INLINE NNObjString* asString() const
        {
            return ((NNObjString*)asObject());
        }

        NEON_INLINE NNObjArray* asArray() const
        {
            return ((NNObjArray*)asObject());
        }

        NEON_INLINE NNObjDict* asDict() const
        {
            return ((NNObjDict*)asObject());
        }

        bool isFalse() const;
};

template <typename StoredType>
class NNValArray
{
    public:
        void deInit(bool actuallydelete)
        {
            nn_memory_free(this->listitems);
            if(actuallydelete)
            {
                nn_memory_free(this);
            }
        }

        static uint64_t getNextCapacity(uint64_t capacity)
        {
            if(capacity < 4)
            {
                return 4;
            }
            return nn_util_rndup2pow64(capacity + 1);
        }

        void initSelf()
        {
            size_t initialsize;
            initialsize = 32;
            listcount = 0;
            listcapacity = 0;
            listitems = nullptr;
            listname = nullptr;
            if(initialsize > 0)
            {
                ensureCapacity(initialsize, StoredType{}, true);
            }
        }

    public:
        const char* listname = nullptr;
        NNValue* listitems = nullptr;
        size_t listcapacity = 0;
        size_t listcount = 0;

    public:
        NNValArray()
        {
        }

        bool push(StoredType value)
        {
            size_t oldcap;
            if(this->listcapacity < this->listcount + 1)
            {
                oldcap = this->listcapacity;
                this->listcapacity = getNextCapacity(oldcap);
                if(this->listitems == nullptr)
                {
                    this->listitems = (StoredType*)nn_memory_malloc(sizeof(StoredType) * this->listcapacity);
                }
                else
                {
                    this->listitems = (StoredType*)nn_memory_realloc(this->listitems, sizeof(StoredType) * this->listcapacity);
                }
            }
            this->listitems[this->listcount] = value;
            this->listcount++;
            return true;
        }

        bool set(size_t idx, StoredType val)
        {
            size_t need;
            need = idx + 8;
            if(this->listcount == 0)
            {
                return push(val);
            }
            if(((idx == 0) || (this->listcapacity == 0)) || (idx >= this->listcapacity))
            {
                if(!ensureCapacity(need, StoredType{}, false))
                {
                    return false;
                }
            }
            this->listitems[idx] = val;
            if(idx > this->listcount)
            {
                this->listcount = idx;
            }
            return true;
        }

        bool removeAtIntern(unsigned int ix)
        {
            size_t tomovebytes;
            void* src;
            void* dest;
            if(ix == (this->listcount - 1))
            {
                this->listcount--;
                return true;
            }
            tomovebytes = (this->listcount - 1 - ix) * sizeof(StoredType);
            dest = this->listitems + (ix * sizeof(StoredType));
            src = this->listitems + ((ix + 1) * sizeof(StoredType));
            memmove(dest, src, tomovebytes);
            this->listcount--;
            return true;
        }

        bool removeAt(unsigned int ix)
        {
            if(ix >= this->listcount)
            {
                return false;
            }
            if(ix == 0)
            {
                this->listitems += sizeof(StoredType);
                this->listcapacity--;
                this->listcount--;
                return true;
            }
            return removeAtIntern(this, ix);
        }

        bool ensureCapacity(size_t needsize, StoredType fillval, bool first)
        {
            size_t i;
            size_t ncap;
            size_t oldcap;
            (void)first;
            if(this->listcapacity < needsize)
            {
                oldcap = this->listcapacity;
                if(oldcap == 0)
                {
                    ncap = needsize;
                }
                else
                {
                    ncap = getNextCapacity(this->listcapacity + needsize);
                }
                this->listcapacity = ncap;
                if(this->listitems == nullptr)
                {
                    this->listitems = (StoredType*)nn_memory_malloc(sizeof(StoredType) * ncap);
                }
                else
                {
                    this->listitems = (StoredType*)nn_memory_realloc(this->listitems, sizeof(StoredType) * ncap);
                }
                if(this->listitems == nullptr)
                {
                    return false;
                }
                for(i = oldcap; i < ncap; i++)
                {
                    this->listitems[i] = fillval;
                }
            }
            return true;
        }

        void setEmpty()
        {
            if((this->listcapacity > 0) && (this->listitems != nullptr))
            {
                memset(this->listitems, 0, sizeof(StoredType) * this->listcapacity);
            }
            this->listcount = 0;
            this->listcapacity = 0;
        }

        NEON_INLINE void setCount(size_t cnt)
        {
            this->listcount = cnt;
        }

        NEON_INLINE void increaseBy(size_t cnt)
        {
            this->listcount += cnt;
        }

        NEON_INLINE void decreaseBy(size_t cnt)
        {
            this->listcount -= cnt;
        }

        NEON_INLINE size_t count()
        {
            return this->listcount;
        }

        NEON_INLINE size_t capacity()
        {
            return this->listcapacity;
        }

        NEON_INLINE StoredType* data()
        {
            return (StoredType*)this->listitems;
        }

        NEON_INLINE StoredType get(size_t idx)
        {
            return ((StoredType*)this->listitems)[idx];
        }

        NEON_INLINE StoredType* getp(size_t idx)
        {
            return (StoredType*)&this->listitems[idx];
        }

        NEON_INLINE bool insert(StoredType val, size_t idx)
        {
            return set(idx, val);
        }

        NEON_INLINE bool pop(StoredType* dest)
        {
            if(this->listcount > 0)
            {
                *dest = (StoredType)this->listitems[this->listcount - 1];
                this->listcount--;
                return true;
            }
            return false;
        }
};

class NNProperty
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
            NNValue getter;
            NNValue setter;
        };

    public:
        bool havegetset;
        FieldType type;
        NNValue value;
        GetSet getset;

    public:
        static NNProperty makeWithPointer(NNValue val, FieldType type)
        {
            NNProperty vf;
            memset(&vf, 0, sizeof(NNProperty));
            vf.type = type;
            vf.value = val;
            vf.havegetset = false;
            return vf;
        }

        static NNProperty makeWithGetSet(NNValue val, NNValue getter, NNValue setter, FieldType type)
        {
            bool getisfn;
            bool setisfn;
            NNProperty np;
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

        static NNProperty make(NNValue val, FieldType type)
        {
            return makeWithPointer(val, type);
        }

};

template <typename HTKeyT, typename HTValT>
class NNHashTable
{
    public:
        struct Entry
        {
            HTKeyT key;
            NNProperty value;
        };

    public:
        /*
         * FIXME: extremely stupid hack: $htactive ensures that a table that was destroyed
         * does not get marked again, et cetera.
         * since destroy() zeroes the data before freeing, $active will be
         * false, and thus, no further marking occurs.
         * obviously the reason is that somewhere a table (from NNObjInstance) is being
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

        void deinit()
        {
            nn_memory_free(this->htentries);
        }

        NEON_INLINE size_t count()
        {
            return this->htcount;
        }

        NEON_INLINE size_t capacity()
        {
            return this->htcapacity;
        }

        NEON_INLINE Entry* entryatindex(size_t idx)
        {
            return &this->htentries[idx];
        }

        Entry* findentrybyvalue(Entry* entries, int capacity, HTKeyT key)
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

        Entry* findentrybystr(Entry* entries, int capacity, HTKeyT valkey, const char* kstr, size_t klen, uint32_t hsv)
        {
            bool havevalhash;
            uint32_t index;
            uint32_t valhash;
            NNObjString* entoskey;
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
                    if(nn_string_getlength(entoskey) == klen)
                    {
                        if(memcmp(kstr, nn_string_getdata(entoskey), klen) == 0)
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

        NNProperty* getfieldbyvalue(HTKeyT key)
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

        NNProperty* getfieldbystr(HTKeyT valkey, const char* kstr, size_t klen, uint32_t hsv)
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

        NNProperty* getfieldbyostr(NNObjString* str);

        NNProperty* getfieldbycstr(const char* kstr);
        NNProperty* getfield(HTKeyT key);

        bool get(HTKeyT key, HTValT* value)
        {
            NNProperty* field;
            field = getfield(key);
            if(field != nullptr)
            {
                *value = (HTValT)field->value;
                return true;
            }
            return false;
        }

        bool adjustcapacity(int capacity)
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
                fprintf(stderr, "hashtab:adjustcapacity: failed to allocate %ld bytes\n", sz);
                abort();
                return false;
            }
            for(i = 0; i < capacity; i++)
            {
                entries[i].key = HTKeyT{};
                entries[i].value = NNProperty::make(HTValT{}, NNProperty::FTYP_VALUE);
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

        bool setwithtype(HTKeyT key, HTValT value, NNProperty::FieldType ftyp, bool keyisstring)
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
            entry->value = NNProperty::make(value, ftyp);
            return isnew;
        }

        bool set(HTKeyT key, HTValT value)
        {
            return setwithtype(key, value, NNProperty::FTYP_VALUE, key.isString());
        }

        bool remove(HTKeyT key);

        bool copyTo(NNHashTable* to, bool keepgoing)
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
                    if(!to->setwithtype((HTKeyT)entry->key, (HTValT)entry->value.value, entry->value.type, false))
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

        void importall(NNHashTable* from, NNHashTable* to)
        {
            int i;
            Entry* entry;
            for(i = 0; i < (int)from->htcapacity; i++)
            {
                entry = &from->htentries[i];
                if(!entry->key.isNull() && !(HTValT)entry->value.value.isModule())
                {
                    /* Don't import private values */
                    if((HTKeyT)entry->key.isString() && nn_string_getdata((HTKeyT)entry->key.asString())[0] == '_')
                    {
                        continue;
                    }
                    to->setwithtype((HTKeyT)entry->key, (HTValT)entry->value.value, entry->value.type, false);
                }
            }
        }

        bool copy(NNHashTable* to)
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
                    to->setwithtype((HTKeyT)entry->key, nn_value_copyvalue((HTValT)entry->value.value), entry->value.type, false);
                }
            }
            return true;
        }

        template <typename InputValT> HTKeyT findkey(InputValT value, InputValT defval)
        {
            int i;
            Entry* entry;
            NN_NULLPTRCHECK_RETURNVALUE(this, NNValue::makeNull());
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

class NNInstruction
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
        static NNInstruction make(bool isop, uint16_t code, int srcline)
        {
            NNInstruction inst;
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

class NNBlob
{
    public:
        static void init(NNBlob* blob)
        {
            blob->count = 0;
            blob->capacity = 0;
            blob->instrucs = nullptr;
            blob->constants.initSelf();
            blob->argdefvals.initSelf();
        }

        static void destroy(NNBlob* blob)
        {
            if(blob->instrucs != nullptr)
            {
                nn_memory_free(blob->instrucs);
            }
            blob->constants.deInit(false);
            blob->argdefvals.deInit(false);
        }


    public:
        int count;
        int capacity;
        NNInstruction* instrucs;
        NNValArray<NNValue> constants;
        NNValArray<NNValue> argdefvals;

    public:
        void push(NNInstruction ins)
        {
            int oldcapacity;
            if(this->capacity < this->count + 1)
            {
                oldcapacity = this->capacity;
                this->capacity = NN_ASTPARSER_GROWCAPACITY(oldcapacity);
                this->instrucs = (NNInstruction*)nn_memory_realloc(this->instrucs, this->capacity * sizeof(NNInstruction));
            }
            this->instrucs[this->count] = ins;
            this->count++;
        }

        int addConstant(NNValue value)
        {
            this->constants.push(value);
            return this->constants.count() - 1;
        }
};

class NNCallFrame
{
    public:
        struct ExceptionInfo
        {
            uint16_t address;
            uint16_t finallyaddress;
        };

    public:
        int handlercount;
        int gcprotcount;
        int stackslotpos;
        NNInstruction* inscode;
        NNObjFunction* closure;
        /* TODO: should be dynamically allocated */
        ExceptionInfo handlers[NEON_CONFIG_MAXEXCEPTHANDLERS];
};

class NNProcessInfo
{
    public:
        int cliprocessid;
        NNObjArray* cliargv;
        NNObjString* cliexedirectory;
        NNObjString* cliscriptfile;
        NNObjString* cliscriptdirectory;
        NNObjFile* filestdout;
        NNObjFile* filestderr;
        NNObjFile* filestdin;
};

class NNObjString : public NNObject
{
    public:
        uint32_t hashvalue;
        NNStringBuffer sbuf;
};

class NNObjUpvalue : public NNObject
{
    public:
        int stackpos;
        NNValue closed;
        NNValue location;
        NNObjUpvalue* next;
};

class NNObjModule : public NNObject
{
    public:
        /* was this module imported? */
        bool imported;
        /* named exports */
        NNHashTable<NNValue, NNValue> deftable;
        /* the name of this module */
        NNObjString* name;
        /* physsical location of this module, or nullptr if some other non-physical location */
        NNObjString* physicalpath;
        /* callback to call BEFORE this module is loaded */
        void* fnpreloaderptr;
        /* callbac to call AFTER this module is unloaded */
        void* fnunloaderptr;
        /* pointer that is based to preloader/unloader */
        void* handle;
};
/**
 * TODO: use a different table implementation to avoid allocating so many strings...
 */
class NNObjClass : public NNObject
{
    public:
        /*
         * the constructor, if any. defaults to <empty>, and if not <empty>, expects to be
         * some callable value.
         */
        NNValue constructor;
        NNValue destructor;

        /*
         * when declaring a class, $instproperties (their names, and initial values) are
         * copied to NNObjInstance::properties.
         * so `$instproperties["something"] = somefunction;` remains untouched *until* an
         * instance of this class is created.
         */
        NNHashTable<NNValue, NNValue> instproperties;

        /*
         * static, unchangeable(-ish) values. intended for values that are not unique, but shared
         * across classes, without being copied.
         */
        NNHashTable<NNValue, NNValue> staticproperties;

        /*
         * method table; they are currently not unique when instantiated; instead, they are
         * read from $methods as-is. this includes adding methods!
         * TODO: introduce a new hashtable field for NNObjInstance for unique methods, perhaps?
         * right now, this method just prevents unnecessary copying.
         */
        NNHashTable<NNValue, NNValue> instmethods;
        NNHashTable<NNValue, NNValue> staticmethods;
        NNObjString* name;
        NNObjClass* superclass;
};

class NNObjInstance : public NNObject
{
    public:
        /*
         * whether this instance is still "active", i.e., not destroyed, deallocated, etc.
         */
        bool active;
        NNHashTable<NNValue, NNValue> properties;
        NNObjClass* klass;
        NNObjInstance* superinstance;
};

class NNObjFunction : public NNObject
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
        ContextType contexttype;
        NNObjString* name;
        int upvalcount;
        NNValue clsthisval;
        union
        {
            struct
            {
                NNObjFunction* scriptfunc;
                NNObjUpvalue** upvalues;
            } fnclosure;
            struct
            {
                int arity;
                bool isvariadic;
                NNBlob blob;
                NNObjModule* module;
            } fnscriptfunc;
            struct
            {
                NNNativeFN natfunc;
                void* userptr;
            } fnnativefunc;
            struct
            {
                NNValue receiver;
                NNObjFunction* method;
            } fnmethod;
        };

    public:

};

class NNObjArray : public NNObject
{
    public:
        NNValArray<NNValue> varray;
};

class NNObjRange : public NNObject
{
    public:
        int lower;
        int upper;
        int range;
};

class NNObjDict : public NNObject
{
    public:
        NNValArray<NNValue> htnames;
        NNHashTable<NNValue, NNValue> htab;
};

class NNObjFile : public NNObject
{
    public:
        bool isopen;
        bool isstd;
        bool istty;
        int number;
        FILE* handle;
        NNObjString* mode;
        NNObjString* path;
};

class NNObjSwitch : public NNObject
{
    public:
        int defaultjump;
        int exitjump;
        NNHashTable<NNValue, NNValue> table;
};

class NNObjUserdata : public NNObject
{
    public:
        void* pointer;
        char* name;
        NNPtrFreeFN ondestroyfn;
};



#define NEON_CONF_LIBSTRINGUSEHASH 0

class InternedStringTab
{
    public:
        NNHashTable<NNValue, NNValue> m_htab;

    public:
        InternedStringTab()
        {
            m_htab.initTable();
        }

        void deinit()
        {
            m_htab.deinit();
        }

        void store(NNObjString* os)
        {
            #if defined(NEON_CONF_LIBSTRINGUSEHASH) && (NEON_CONF_LIBSTRINGUSEHASH == 1)
                m_htab.set(nn_string_tabhashvalueobj(os), NNValue::fromObject(os));
            #else
                m_htab.set(NNValue::fromObject(os), NNValue::makeNull());
            #endif
        }

        NNObjString* findstring(const char* findstr, size_t findlen, uint32_t findhash)
        {
            size_t slen;
            uint32_t index;
        #if defined(NEON_CONF_LIBSTRINGUSEHASH) && (NEON_CONF_LIBSTRINGUSEHASH == 1)
            double dn;
            double wanteddn;
        #endif
            const char* sdata;
            NNObjString* string;
            (void)findstr;
            (void)sdata;
            NN_NULLPTRCHECK_RETURNVALUE(m_htab, nullptr);
            if(m_htab.htcount == 0)
            {
                return nullptr;
            }
        #if defined(NEON_CONF_LIBSTRINGUSEHASH) && (NEON_CONF_LIBSTRINGUSEHASH == 1)
            wanteddn = nn_string_tabhashvaluecombine(findstr, findlen, findhash);
        #endif
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
        #if defined(NEON_CONF_LIBSTRINGUSEHASH) && (NEON_CONF_LIBSTRINGUSEHASH == 1)
                dn = entry->key.asNumber();
                if(dn == wanteddn)
                {
                    return entry->value.value.asString();
                }
        #else
                string = entry->key.asString();
                slen = nn_string_getlength(string);
                sdata = nn_string_getdata(string);
                if((slen == findlen) && (string->hashvalue == findhash))
                {
            #if 0
                        if(memcmp(sdata, findstr, findlen) == 0)
            #endif
                    {
                        /* we found it */
                        return string;
                    }
                }
        #endif
                index = (index + 1) & (m_htab.htcapacity - 1);
            }
            return nullptr;
        }
    
};

class SharedState
{
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
            NNObject** graystack;
        } m_gcstate;

        struct
        {
            int64_t stackidx;
            int64_t stackcapacity;
            int64_t framecapacity;
            int64_t framecount;
            NNInstruction currentinstr;
            NNCallFrame* currentframe;
            NNObjUpvalue* openupvalues;
            NNCallFrame* framevalues;
            NNValue* stackvalues;
        } m_vmstate;

        struct
        {
            NNObjClass* stdexception;
            NNObjClass* syntaxerror;
            NNObjClass* asserterror;
            NNObjClass* ioerror;
            NNObjClass* oserror;
            NNObjClass* argumenterror;
            NNObjClass* regexerror;
            NNObjClass* importerror;
        } m_exceptions;

        struct
        {
            /* __indexget__ */
            NNObjString* nmindexget;
            /* __indexset__ */
            NNObjString* nmindexset;
            /* __add__ */
            NNObjString* nmadd;
            /* __sub__ */
            NNObjString* nmsub;
            /* __div__ */
            NNObjString* nmdiv;
            /* __mul__ */
            NNObjString* nmmul;
            /* __band__ */
            NNObjString* nmband;
            /* __bor__ */
            NNObjString* nmbor;
            /* __bxor__ */
            NNObjString* nmbxor;

            NNObjString* nmconstructor;
        } m_defaultstrings;

        NNObject* linkedobjects;
        bool markvalue;
        InternedStringTab m_allocatedstrings;
        NNHashTable<NNValue, NNValue> m_openedmodules;
        NNHashTable<NNValue, NNValue> m_declaredglobals;

        /*
         * these classes are used for runtime objects, specifically in nn_value_getclassfor.
         * every other global class needed (as created by nn_util_makeclass) need NOT be declared here.
         * simply put, these classes are primitive types.
         */
        /* the class from which every other class derives */
        NNObjClass* m_classprimobject;
        /**/
        NNObjClass* m_classprimclass;
        /* class for numbers, et al */
        NNObjClass* m_classprimnumber;
        /* class for strings */
        NNObjClass* m_classprimstring;
        /* class for arrays */
        NNObjClass* m_classprimarray;
        /* class for dictionaries */
        NNObjClass* m_classprimdict;
        /* class for I/O file objects */
        NNObjClass* m_classprimfile;
        /* class for range constructs */
        NNObjClass* m_classprimrange;
        /* class for anything callable: functions, lambdas, constructors ... */
        NNObjClass* m_classprimcallable;
        NNObjClass* m_classprimprocess;

        bool m_isrepl;
        NNProcessInfo* m_processinfo;

        /* miscellaneous */
        NNIOStream* m_stdoutprinter;
        NNIOStream* m_stderrprinter;
        NNIOStream* m_debugwriter;
        const char* m_rootphysfile;
        NNObjDict* m_envdict;
        NNValArray<NNValue> m_importpath;

        NNValue m_lastreplvalue;
        void* m_memuserptr;
        NNObjModule* m_topmodule;

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

        static NNObject* gcMakeObject(size_t size, NNObject::Type type, bool retain)
        {
            NNObject* object;
            auto gcs = SharedState::get();
            object = (NNObject*)SharedState::gcAllocate(size, 1, retain);
            object->type = type;
            object->mark = !gcs->markvalue;
            object->stale = false;
            object->next = gcs->linkedobjects;
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
            NNObject* object;
            auto gcs = SharedState::get();
            while(gcs->m_gcstate.graycount > 0)
            {
                gcs->m_gcstate.graycount--;
                object = gcs->m_gcstate.graystack[gcs->m_gcstate.graycount];
                NNObject::blackenObject(object);
            }
        }

        static void gcSweep()
        {
            NNObject* object;
            NNObject* previous;
            NNObject* unreached;
            previous = nullptr;
            auto gcs = SharedState::get();
            object = gcs->linkedobjects;
            while(object != nullptr)
            {
                if(object->mark == gcs->markvalue)
                {
                    previous = object;
                    object = object->next;
                }
                else
                {
                    unreached = object;
                    object = object->next;
                    if(previous != nullptr)
                    {
                        previous->next = object;
                    }
                    else
                    {
                        gcs->linkedobjects = object;
                    }
                    NNObject::destroyObject(unreached);
                }
            }
        }

        static void gcLinkedObjectsDestroy()
        {
            NNObject* next;
            NNObject* object;
            auto gcs = SharedState::get();
            object = gcs->linkedobjects;
            while(object != nullptr)
            {
                next = object->next;
                NNObject::destroyObject(object);
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
                NNObject::markObject((NNObject*)fnc->targetfunc);
                fnc = fnc->enclosing;
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
            Memory::destroy(pointer);
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
            NNCallFrame* frame;
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
         * i.e., [NNValue x 32] -> [NNValue x <newsize>], without
         * copying anything beyond primitive values.
         */
        NEON_INLINE bool resizeStack(size_t needed)
        {
            size_t oldsz;
            size_t newsz;
            size_t allocsz;
            size_t nforvals;
            NNValue* oldbuf;
            NNValue* newbuf;
            nforvals = (needed * 2);
            oldsz = m_vmstate.stackcapacity;
            newsz = oldsz + nforvals;
            allocsz = ((newsz + 1) * sizeof(NNValue));
            oldbuf = m_vmstate.stackvalues;
            newbuf = (NNValue*)nn_memory_realloc(oldbuf, allocsz);
            if(newbuf == nullptr)
            {
                fprintf(stderr, "internal error: failed to resize stackvalues!\n");
                abort();
            }
            m_vmstate.stackvalues = (NNValue*)newbuf;
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
            NNInstruction* oldip;
            NNObjFunction* oldclosure;
            NNCallFrame* oldbuf;
            NNCallFrame* newbuf;
            (void)i;
            oldclosure = m_vmstate.currentframe->closure;
            oldip = m_vmstate.currentframe->inscode;
            oldhandlercnt = m_vmstate.currentframe->handlercount;
            oldsz = m_vmstate.framecapacity;
            newsz = oldsz + needed;
            allocsz = ((newsz + 1) * sizeof(NNCallFrame));
    #if 1
            oldbuf = m_vmstate.framevalues;
            newbuf = (NNCallFrame*)nn_memory_realloc(oldbuf, allocsz);
            if(newbuf == nullptr)
            {
                fprintf(stderr, "internal error: failed to resize framevalues!\n");
                abort();
            }
    #endif
            m_vmstate.framevalues = (NNCallFrame*)newbuf;
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

        NEON_INLINE bool checkMaybeResize()
        {
            if((m_vmstate.stackidx + 1) >= m_vmstate.stackcapacity)
            {
                if(!resizeStack(m_vmstate.stackidx + 1))
                {
                    fprintf(stderr, "failed to resize stack due to overflow");
                    return false;
                }
                return true;
            }
            if(m_vmstate.framecount >= m_vmstate.framecapacity)
            {
                if(!resizeFrames(m_vmstate.framecapacity + 1))
                {
                    fprintf(stderr, "failed to resize frames due to overflow");
                    return false;
                }
                return true;
            }
            return false;
        }

        template <typename InputT> NEON_INLINE void stackPush(InputT value)
        {
            checkMaybeResize();
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



class NNAstToken
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
        Type type;
        const char* start;
        int length;
        int line;
};

class NNAstLexer
{
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
                case NNAstToken::T_NEWLINE:
                    return "NNAstToken::T_NEWLINE";
                case NNAstToken::T_PARENOPEN:
                    return "NNAstToken::T_PARENOPEN";
                case NNAstToken::T_PARENCLOSE:
                    return "NNAstToken::T_PARENCLOSE";
                case NNAstToken::T_BRACKETOPEN:
                    return "NNAstToken::T_BRACKETOPEN";
                case NNAstToken::T_BRACKETCLOSE:
                    return "NNAstToken::T_BRACKETCLOSE";
                case NNAstToken::T_BRACEOPEN:
                    return "NNAstToken::T_BRACEOPEN";
                case NNAstToken::T_BRACECLOSE:
                    return "NNAstToken::T_BRACECLOSE";
                case NNAstToken::T_SEMICOLON:
                    return "NNAstToken::T_SEMICOLON";
                case NNAstToken::T_COMMA:
                    return "NNAstToken::T_COMMA";
                case NNAstToken::T_BACKSLASH:
                    return "NNAstToken::T_BACKSLASH";
                case NNAstToken::T_EXCLMARK:
                    return "NNAstToken::T_EXCLMARK";
                case NNAstToken::T_NOTEQUAL:
                    return "NNAstToken::T_NOTEQUAL";
                case NNAstToken::T_COLON:
                    return "NNAstToken::T_COLON";
                case NNAstToken::T_AT:
                    return "NNAstToken::T_AT";
                case NNAstToken::T_DOT:
                    return "NNAstToken::T_DOT";
                case NNAstToken::T_DOUBLEDOT:
                    return "NNAstToken::T_DOUBLEDOT";
                case NNAstToken::T_TRIPLEDOT:
                    return "NNAstToken::T_TRIPLEDOT";
                case NNAstToken::T_PLUS:
                    return "NNAstToken::T_PLUS";
                case NNAstToken::T_PLUSASSIGN:
                    return "NNAstToken::T_PLUSASSIGN";
                case NNAstToken::T_INCREMENT:
                    return "NNAstToken::T_INCREMENT";
                case NNAstToken::T_MINUS:
                    return "NNAstToken::T_MINUS";
                case NNAstToken::T_MINUSASSIGN:
                    return "NNAstToken::T_MINUSASSIGN";
                case NNAstToken::T_DECREMENT:
                    return "NNAstToken::T_DECREMENT";
                case NNAstToken::T_MULTIPLY:
                    return "NNAstToken::T_MULTIPLY";
                case NNAstToken::T_MULTASSIGN:
                    return "NNAstToken::T_MULTASSIGN";
                case NNAstToken::T_POWEROF:
                    return "NNAstToken::T_POWEROF";
                case NNAstToken::T_POWASSIGN:
                    return "NNAstToken::T_POWASSIGN";
                case NNAstToken::T_DIVIDE:
                    return "NNAstToken::T_DIVIDE";
                case NNAstToken::T_DIVASSIGN:
                    return "NNAstToken::T_DIVASSIGN";
                case NNAstToken::T_ASSIGN:
                    return "NNAstToken::T_ASSIGN";
                case NNAstToken::T_EQUAL:
                    return "NNAstToken::T_EQUAL";
                case NNAstToken::T_LESSTHAN:
                    return "NNAstToken::T_LESSTHAN";
                case NNAstToken::T_LESSEQUAL:
                    return "NNAstToken::T_LESSEQUAL";
                case NNAstToken::T_LEFTSHIFT:
                    return "NNAstToken::T_LEFTSHIFT";
                case NNAstToken::T_LEFTSHIFTASSIGN:
                    return "NNAstToken::T_LEFTSHIFTASSIGN";
                case NNAstToken::T_GREATERTHAN:
                    return "NNAstToken::T_GREATERTHAN";
                case NNAstToken::T_GREATER_EQ:
                    return "NNAstToken::T_GREATER_EQ";
                case NNAstToken::T_RIGHTSHIFT:
                    return "NNAstToken::T_RIGHTSHIFT";
                case NNAstToken::T_RIGHTSHIFTASSIGN:
                    return "NNAstToken::T_RIGHTSHIFTASSIGN";
                case NNAstToken::T_MODULO:
                    return "NNAstToken::T_MODULO";
                case NNAstToken::T_PERCENT_EQ:
                    return "NNAstToken::T_PERCENT_EQ";
                case NNAstToken::T_AMP:
                    return "NNAstToken::T_AMP";
                case NNAstToken::T_AMP_EQ:
                    return "NNAstToken::T_AMP_EQ";
                case NNAstToken::T_BAR:
                    return "NNAstToken::T_BAR";
                case NNAstToken::T_BAR_EQ:
                    return "NNAstToken::T_BAR_EQ";
                case NNAstToken::T_TILDE:
                    return "NNAstToken::T_TILDE";
                case NNAstToken::T_TILDE_EQ:
                    return "NNAstToken::T_TILDE_EQ";
                case NNAstToken::T_XOR:
                    return "NNAstToken::T_XOR";
                case NNAstToken::T_XOR_EQ:
                    return "NNAstToken::T_XOR_EQ";
                case NNAstToken::T_QUESTION:
                    return "NNAstToken::T_QUESTION";
                case NNAstToken::T_KWAND:
                    return "NNAstToken::T_KWAND";
                case NNAstToken::T_KWAS:
                    return "NNAstToken::T_KWAS";
                case NNAstToken::T_KWASSERT:
                    return "NNAstToken::T_KWASSERT";
                case NNAstToken::T_KWBREAK:
                    return "NNAstToken::T_KWBREAK";
                case NNAstToken::T_KWCATCH:
                    return "NNAstToken::T_KWCATCH";
                case NNAstToken::T_KWCLASS:
                    return "NNAstToken::T_KWCLASS";
                case NNAstToken::T_KWCONTINUE:
                    return "NNAstToken::T_KWCONTINUE";
                case NNAstToken::T_KWFUNCTION:
                    return "NNAstToken::T_KWFUNCTION";
                case NNAstToken::T_KWDEFAULT:
                    return "NNAstToken::T_KWDEFAULT";
                case NNAstToken::T_KWTHROW:
                    return "NNAstToken::T_KWTHROW";
                case NNAstToken::T_KWDO:
                    return "NNAstToken::T_KWDO";
                case NNAstToken::T_KWECHO:
                    return "NNAstToken::T_KWECHO";
                case NNAstToken::T_KWELSE:
                    return "NNAstToken::T_KWELSE";
                case NNAstToken::T_KWFALSE:
                    return "NNAstToken::T_KWFALSE";
                case NNAstToken::T_KWFINALLY:
                    return "NNAstToken::T_KWFINALLY";
                case NNAstToken::T_KWFOREACH:
                    return "NNAstToken::T_KWFOREACH";
                case NNAstToken::T_KWIF:
                    return "NNAstToken::T_KWIF";
                case NNAstToken::T_KWIMPORT:
                    return "NNAstToken::T_KWIMPORT";
                case NNAstToken::T_KWIN:
                    return "NNAstToken::T_KWIN";
                case NNAstToken::T_KWFOR:
                    return "NNAstToken::T_KWFOR";
                case NNAstToken::T_KWNULL:
                    return "NNAstToken::T_KWNULL";
                case NNAstToken::T_KWNEW:
                    return "NNAstToken::T_KWNEW";
                case NNAstToken::T_KWOR:
                    return "NNAstToken::T_KWOR";
                case NNAstToken::T_KWSUPER:
                    return "NNAstToken::T_KWSUPER";
                case NNAstToken::T_KWRETURN:
                    return "NNAstToken::T_KWRETURN";
                case NNAstToken::T_KWTHIS:
                    return "NNAstToken::T_KWTHIS";
                case NNAstToken::T_KWSTATIC:
                    return "NNAstToken::T_KWSTATIC";
                case NNAstToken::T_KWTRUE:
                    return "NNAstToken::T_KWTRUE";
                case NNAstToken::T_KWTRY:
                    return "NNAstToken::T_KWTRY";
                case NNAstToken::T_KWSWITCH:
                    return "NNAstToken::T_KWSWITCH";
                case NNAstToken::T_KWVAR:
                    return "NNAstToken::T_KWVAR";
                case NNAstToken::T_KWCONST:
                    return "NNAstToken::T_KWCONST";
                case NNAstToken::T_KWCASE:
                    return "NNAstToken::T_KWCASE";
                case NNAstToken::T_KWWHILE:
                    return "NNAstToken::T_KWWHILE";
                case NNAstToken::T_KWINSTANCEOF:
                    return "NNAstToken::T_KWINSTANCEOF";
                case NNAstToken::T_KWEXTENDS:
                    return "NNAstToken::T_KWEXTENDS";
                case NNAstToken::T_LITERALSTRING:
                    return "NNAstToken::T_LITERALSTRING";
                case NNAstToken::T_LITERALRAWSTRING:
                    return "NNAstToken::T_LITERALRAWSTRING";
                case NNAstToken::T_LITNUMREG:
                    return "NNAstToken::T_LITNUMREG";
                case NNAstToken::T_LITNUMBIN:
                    return "NNAstToken::T_LITNUMBIN";
                case NNAstToken::T_LITNUMOCT:
                    return "NNAstToken::T_LITNUMOCT";
                case NNAstToken::T_LITNUMHEX:
                    return "NNAstToken::T_LITNUMHEX";
                case NNAstToken::T_IDENTNORMAL:
                    return "NNAstToken::T_IDENTNORMAL";
                case NNAstToken::T_DECORATOR:
                    return "NNAstToken::T_DECORATOR";
                case NNAstToken::T_INTERPOLATION:
                    return "NNAstToken::T_INTERPOLATION";
                case NNAstToken::T_EOF:
                    return "NNAstToken::T_EOF";
                case NNAstToken::T_ERROR:
                    return "NNAstToken::T_ERROR";
                case NNAstToken::T_KWEMPTY:
                    return "NNAstToken::T_KWEMPTY";
                case NNAstToken::T_UNDEFINED:
                    return "NNAstToken::T_UNDEFINED";
                case NNAstToken::T_TOKCOUNT:
                    return "NNAstToken::T_TOKCOUNT";
            }
            return "?invalid?";
        }

        static NNAstLexer* make(const char* source)
        {
            NNAstLexer* lex;
            lex = Memory::make<NNAstLexer>();
            lex->init(source);
            lex->onstack = false;
            return lex;
        }


        static void destroy(NNAstLexer* lex)
        {
            if(!lex->onstack)
            {
                nn_memory_free(lex);
            }
        }

    public:
        bool onstack;
        const char* start;
        const char* sourceptr;
        int line;
        int tplstringcount;
        int tplstringbuffer[NEON_CONFIG_ASTMAXSTRTPLDEPTH];

    public:
        /*
         * allows for the lexer to created on the stack.
         */
        void init(const char* source)
        {
            this->sourceptr = source;
            this->start = source;
            this->sourceptr = this->start;
            this->line = 1;
            this->tplstringcount = -1;
            this->onstack = true;
        }

        bool isatend()
        {
            return *this->sourceptr == '\0';
        }

        NNAstToken createtoken(NNAstToken::Type type)
        {
            NNAstToken t;
            t.isglobal = false;
            t.type = type;
            t.start = this->start;
            t.length = (int)(this->sourceptr - this->start);
            t.line = this->line;
            return t;
        }

        template<typename... ArgsT>
        NNAstToken errortoken(const char* fmt, ArgsT&&... args)
        {
            constexpr static auto tmpsprintf = sprintf;
            int length;
            char* buf;
            NNAstToken t;
            buf = (char*)nn_memory_malloc(sizeof(char) * 1024);
            /* TODO: used to be vasprintf. need to check how much to actually allocate! */
            length = tmpsprintf(buf, fmt, args...);
            t.type = NNAstToken::T_ERROR;
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
            t.line = this->line;
            return t;
        }

        char advance()
        {
            this->sourceptr++;
            if(this->sourceptr[-1] == '\n')
            {
                this->line++;
            }
            return this->sourceptr[-1];
        }

        bool match(char expected)
        {
            if(isatend())
            {
                return false;
            }
            if(*this->sourceptr != expected)
            {
                return false;
            }
            this->sourceptr++;
            if(this->sourceptr[-1] == '\n')
            {
                this->line++;
            }
            return true;
        }

        char peekcurr()
        {
            return *this->sourceptr;
        }

        char peekprev()
        {
            if(this->sourceptr == this->start)
            {
                return -1;
            }
            return this->sourceptr[-1];
        }

        char peeknext()
        {
            if(isatend())
            {
                return '\0';
            }
            return this->sourceptr[1];
        }

        NNAstToken skipblockcomments()
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
        #if defined(NEON_PLAT_ISWINDOWS)
            #if 0
                    advance();
            #endif
        #endif
            return createtoken(NNAstToken::T_UNDEFINED);
        }

        NNAstToken skipspace()
        {
            char c;
            NNAstToken result;
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
                            return createtoken(NNAstToken::T_UNDEFINED);
                        }
                        else if(peeknext() == '*')
                        {
                            advance();
                            advance();
                            result = skipblockcomments();
                            if(result.type != NNAstToken::T_UNDEFINED)
                            {
                                return result;
                            }
                            break;
                        }
                        else
                        {
                            return createtoken(NNAstToken::T_UNDEFINED);
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
            return createtoken(NNAstToken::T_UNDEFINED);
        }

        NNAstToken scanstring(char quote, bool withtemplate, bool permitescapes)
        {
            NNAstToken tkn;
            while(peekcurr() != quote && !isatend())
            {
                if(withtemplate)
                {
                    /* interpolation started */
                    if(peekcurr() == '$' && peeknext() == '{' && peekprev() != '\\')
                    {
                        if(this->tplstringcount - 1 < NEON_CONFIG_ASTMAXSTRTPLDEPTH)
                        {
                            this->tplstringcount++;
                            this->tplstringbuffer[this->tplstringcount] = (int)quote;
                            this->sourceptr++;
                            tkn = createtoken(NNAstToken::T_INTERPOLATION);
                            this->sourceptr++;
                            return tkn;
                        }
                        return errortoken("maximum interpolation nesting of %d exceeded by %d", NEON_CONFIG_ASTMAXSTRTPLDEPTH, NEON_CONFIG_ASTMAXSTRTPLDEPTH - this->tplstringcount + 1);
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
                return createtoken(NNAstToken::T_LITERALSTRING);
            }
            return createtoken(NNAstToken::T_LITERALRAWSTRING);
        }

        NNAstToken scannumber()
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
                    return createtoken(NNAstToken::T_LITNUMBIN);
                }
                else if(match('c'))
                {
                    while(lexUtilIsOctal(peekcurr()))
                    {
                        advance();
                    }
                    return createtoken(NNAstToken::T_LITNUMOCT);
                }
                else if(match('x'))
                {
                    while(lexUtilIsHexadecimal(peekcurr()))
                    {
                        advance();
                    }
                    return createtoken(NNAstToken::T_LITNUMHEX);
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
            return createtoken(NNAstToken::T_LITNUMREG);
        }

        NNAstToken::Type getidenttype()
        {
            /* clang-format off */
            static const struct
            {
                const char* str;
                int tokid;
            } keywords[] = {
                { "and", NNAstToken::T_KWAND },
                { "assert", NNAstToken::T_KWASSERT },
                { "as", NNAstToken::T_KWAS },
                { "break", NNAstToken::T_KWBREAK },
                { "catch", NNAstToken::T_KWCATCH },
                { "class", NNAstToken::T_KWCLASS },
                { "continue", NNAstToken::T_KWCONTINUE },
                { "default", NNAstToken::T_KWDEFAULT },
                { "def", NNAstToken::T_KWFUNCTION },
                { "function", NNAstToken::T_KWFUNCTION },
                { "throw", NNAstToken::T_KWTHROW },
                { "do", NNAstToken::T_KWDO },
                { "echo", NNAstToken::T_KWECHO },
                { "else", NNAstToken::T_KWELSE },
                { "empty", NNAstToken::T_KWEMPTY },
                { "extends", NNAstToken::T_KWEXTENDS },
                { "false", NNAstToken::T_KWFALSE },
                { "finally", NNAstToken::T_KWFINALLY },
                { "foreach", NNAstToken::T_KWFOREACH },
                { "if", NNAstToken::T_KWIF },
                { "import", NNAstToken::T_KWIMPORT },                
                { "in", NNAstToken::T_KWIN },
                { "instanceof", NNAstToken::T_KWINSTANCEOF },
                { "for", NNAstToken::T_KWFOR },
                { "null", NNAstToken::T_KWNULL },
                { "new", NNAstToken::T_KWNEW },
                { "or", NNAstToken::T_KWOR },
                { "super", NNAstToken::T_KWSUPER },
                { "return", NNAstToken::T_KWRETURN },
                { "this", NNAstToken::T_KWTHIS },
                { "static", NNAstToken::T_KWSTATIC },
                { "true", NNAstToken::T_KWTRUE },
                { "try", NNAstToken::T_KWTRY },
                { "typeof", NNAstToken::T_KWTYPEOF },
                { "switch", NNAstToken::T_KWSWITCH },
                { "case", NNAstToken::T_KWCASE },
                { "var", NNAstToken::T_KWVAR },
                { "let", NNAstToken::T_KWVAR },
                { "const", NNAstToken::T_KWCONST },
                { "while", NNAstToken::T_KWWHILE },
                { nullptr, (NNAstToken::Type)0 }
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
                ofs = (this->sourceptr - this->start);
                if(ofs == kwlen)
                {
                    if(memcmp(this->start, kwtext, kwlen) == 0)
                    {
                        return (NNAstToken::Type)keywords[i].tokid;
                    }
                }
            }
            return NNAstToken::T_IDENTNORMAL;
        }

        NNAstToken scanident(bool isdollar)
        {
            int cur;
            NNAstToken tok;
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

        NNAstToken scandecorator()
        {
            while(lexUtilIsAlpha(peekcurr()) || lexUtilIsDigit(peekcurr()))
            {
                advance();
            }
            return createtoken(NNAstToken::T_DECORATOR);
        }

        NNAstToken scantoken()
        {
            char c;
            bool isdollar;
            NNAstToken tk;
            NNAstToken token;
            tk = skipspace();
            if(tk.type != NNAstToken::T_UNDEFINED)
            {
                return tk;
            }
            this->start = this->sourceptr;
            if(isatend())
            {
                return createtoken(NNAstToken::T_EOF);
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
                    return createtoken(NNAstToken::T_PARENOPEN);
                }
                break;
                case ')':
                {
                    return createtoken(NNAstToken::T_PARENCLOSE);
                }
                break;
                case '[':
                {
                    return createtoken(NNAstToken::T_BRACKETOPEN);
                }
                break;
                case ']':
                {
                    return createtoken(NNAstToken::T_BRACKETCLOSE);
                }
                break;
                case '{':
                {
                    return createtoken(NNAstToken::T_BRACEOPEN);
                }
                break;
                case '}':
                {
                    if(this->tplstringcount > -1)
                    {
                        token = scanstring((char)this->tplstringbuffer[this->tplstringcount], true, true);
                        this->tplstringcount--;
                        return token;
                    }
                    return createtoken(NNAstToken::T_BRACECLOSE);
                }
                break;
                case ';':
                {
                    return createtoken(NNAstToken::T_SEMICOLON);
                }
                break;
                case '\\':
                {
                    return createtoken(NNAstToken::T_BACKSLASH);
                }
                break;
                case ':':
                {
                    return createtoken(NNAstToken::T_COLON);
                }
                break;
                case ',':
                {
                    return createtoken(NNAstToken::T_COMMA);
                }
                break;
                case '@':
                {
                    if(!lexUtilIsAlpha(peekcurr()))
                    {
                        return createtoken(NNAstToken::T_AT);
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
                            return createtoken(NNAstToken::T_NOTEQUAL);
                        }
                        return createtoken(NNAstToken::T_NOTEQUAL);
                    }
                    return createtoken(NNAstToken::T_EXCLMARK);
                }
                break;
                case '.':
                {
                    if(match('.'))
                    {
                        if(match('.'))
                        {
                            return createtoken(NNAstToken::T_TRIPLEDOT);
                        }
                        return createtoken(NNAstToken::T_DOUBLEDOT);
                    }
                    return createtoken(NNAstToken::T_DOT);
                }
                break;
                case '+':
                {
                    if(match('+'))
                    {
                        return createtoken(NNAstToken::T_INCREMENT);
                    }
                    if(match('='))
                    {
                        return createtoken(NNAstToken::T_PLUSASSIGN);
                    }
                    else
                    {
                        return createtoken(NNAstToken::T_PLUS);
                    }
                }
                break;
                case '-':
                {
                    if(match('-'))
                    {
                        return createtoken(NNAstToken::T_DECREMENT);
                    }
                    if(match('='))
                    {
                        return createtoken(NNAstToken::T_MINUSASSIGN);
                    }
                    else
                    {
                        return createtoken(NNAstToken::T_MINUS);
                    }
                }
                break;
                case '*':
                {
                    if(match('*'))
                    {
                        if(match('='))
                        {
                            return createtoken(NNAstToken::T_POWASSIGN);
                        }
                        return createtoken(NNAstToken::T_POWEROF);
                    }
                    else
                    {
                        if(match('='))
                        {
                            return createtoken(NNAstToken::T_MULTASSIGN);
                        }
                        return createtoken(NNAstToken::T_MULTIPLY);
                    }
                }
                break;
                case '/':
                {
                    if(match('='))
                    {
                        return createtoken(NNAstToken::T_DIVASSIGN);
                    }
                    return createtoken(NNAstToken::T_DIVIDE);
                }
                break;
                case '=':
                {
                    if(match('='))
                    {
                        /* pseudo-handle === */
                        if(match('='))
                        {
                            return createtoken(NNAstToken::T_EQUAL);
                        }
                        return createtoken(NNAstToken::T_EQUAL);
                    }
                    return createtoken(NNAstToken::T_ASSIGN);
                }
                break;
                case '<':
                {
                    if(match('<'))
                    {
                        if(match('='))
                        {
                            return createtoken(NNAstToken::T_LEFTSHIFTASSIGN);
                        }
                        return createtoken(NNAstToken::T_LEFTSHIFT);
                    }
                    else
                    {
                        if(match('='))
                        {
                            return createtoken(NNAstToken::T_LESSEQUAL);
                        }
                        return createtoken(NNAstToken::T_LESSTHAN);
                    }
                }
                break;
                case '>':
                {
                    if(match('>'))
                    {
                        if(match('='))
                        {
                            return createtoken(NNAstToken::T_RIGHTSHIFTASSIGN);
                        }
                        return createtoken(NNAstToken::T_RIGHTSHIFT);
                    }
                    else
                    {
                        if(match('='))
                        {
                            return createtoken(NNAstToken::T_GREATER_EQ);
                        }
                        return createtoken(NNAstToken::T_GREATERTHAN);
                    }
                }
                break;
                case '%':
                {
                    if(match('='))
                    {
                        return createtoken(NNAstToken::T_PERCENT_EQ);
                    }
                    return createtoken(NNAstToken::T_MODULO);
                }
                break;
                case '&':
                {
                    if(match('&'))
                    {
                        return createtoken(NNAstToken::T_KWAND);
                    }
                    else if(match('='))
                    {
                        return createtoken(NNAstToken::T_AMP_EQ);
                    }
                    return createtoken(NNAstToken::T_AMP);
                }
                break;
                case '|':
                {
                    if(match('|'))
                    {
                        return createtoken(NNAstToken::T_KWOR);
                    }
                    else if(match('='))
                    {
                        return createtoken(NNAstToken::T_BAR_EQ);
                    }
                    return createtoken(NNAstToken::T_BAR);
                }
                break;
                case '~':
                {
                    if(match('='))
                    {
                        return createtoken(NNAstToken::T_TILDE_EQ);
                    }
                    return createtoken(NNAstToken::T_TILDE);
                }
                break;
                case '^':
                {
                    if(match('='))
                    {
                        return createtoken(NNAstToken::T_XOR_EQ);
                    }
                    return createtoken(NNAstToken::T_XOR);
                }
                break;
                case '\n':
                {
                    return createtoken(NNAstToken::T_NEWLINE);
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
                    return createtoken(NNAstToken::T_QUESTION);
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

class NNAstParser
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
                NNAstParsePrefixFN prefix;
                NNAstParseInfixFN infix;
                Precedence precedence;
        };

        struct LocalVarInfo
        {
            bool iscaptured;
            int depth;
            NNAstToken name;
        };

        struct UpvalInfo
        {
            bool islocal;
            uint16_t index;
        };

        class FuncCompiler
        {
            public:
                int localcount;
                int scopedepth;
                int handlercount;
                bool fromimport;
                FuncCompiler* enclosing;
                NNAstParser* m_sharedprs;
                /* current function */
                NNObjFunction* targetfunc;
                NNObjFunction::ContextType contexttype;
                /* TODO: these should be dynamically allocated */
                LocalVarInfo locals[NEON_CONFIG_ASTMAXLOCALS];
                UpvalInfo upvalues[NEON_CONFIG_ASTMAXUPVALS];

            public:
                FuncCompiler(NNAstParser* prs, NNObjFunction::ContextType type, bool isanon)
                {
                    bool candeclthis;
                    NNIOStream wtmp;
                    LocalVarInfo* local;
                    NNObjString* fname;
                    auto gcs = SharedState::get();
                    this->m_sharedprs = prs;
                    this->enclosing = m_sharedprs->m_currentfunccompiler;
                    this->targetfunc = nullptr;
                    this->contexttype = type;
                    this->localcount = 0;
                    this->scopedepth = 0;
                    this->handlercount = 0;
                    this->fromimport = false;
                    this->targetfunc = nn_object_makefuncscript(m_sharedprs->m_currentmodule, type);
                    m_sharedprs->m_currentfunccompiler = this;
                    if(type != NNObjFunction::CTXTYPE_SCRIPT)
                    {
                        gcs->stackPush(NNValue::fromObject(this->targetfunc));
                        if(isanon)
                        {
                            NNIOStream::makeStackString(&wtmp);
                            wtmp.format("anonymous@[%s:%d]", m_sharedprs->m_currentfile, m_sharedprs->m_prevtoken.line);
                            fname = wtmp.takeString();
                            NNIOStream::destroy(&wtmp);
                        }
                        else
                        {
                            fname = nn_string_copylen(m_sharedprs->m_prevtoken.start, m_sharedprs->m_prevtoken.length);
                        }
                        m_sharedprs->m_currentfunccompiler->targetfunc->name = fname;
                        gcs->stackPop();
                    }
                    /* claiming slot zero for use in class methods */
                    local = &m_sharedprs->m_currentfunccompiler->locals[0];
                    m_sharedprs->m_currentfunccompiler->localcount++;
                    local->depth = 0;
                    local->iscaptured = false;
                    candeclthis = ((type != NNObjFunction::CTXTYPE_FUNCTION) && (m_sharedprs->m_compcontext == NEON_COMPCONTEXT_CLASS));
                    if(candeclthis || (/*(type == NNObjFunction::CTXTYPE_ANONYMOUS) &&*/ (m_sharedprs->m_compcontext != NEON_COMPCONTEXT_CLASS)))
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

                int resolveLocal(NNAstToken* name)
                {
                    int i;
                    LocalVarInfo* local;
                    for(i = this->localcount - 1; i >= 0; i--)
                    {
                        local = &this->locals[i];
                        if(utilIdentsEqual(&local->name, name))
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
                    upcnt = this->targetfunc->upvalcount;
                    for(i = 0; i < upcnt; i++)
                    {
                        upvalue = &this->upvalues[i];
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
                    this->upvalues[upcnt].islocal = islocal;
                    this->upvalues[upcnt].index = index;
                    return this->targetfunc->upvalcount++;
                }

                int resolveUpvalue(NNAstToken* name)
                {
                    int local;
                    int upvalue;
                    if(this->enclosing == nullptr)
                    {
                        return -1;
                    }
                    local = this->enclosing->resolveLocal(name);
                    if(local != -1)
                    {
                        this->enclosing->locals[local].iscaptured = true;
                        return this->addUpvalue((uint16_t)local, true);
                    }
                    upvalue = this->enclosing->resolveUpvalue(name);
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
                    NNAstToken paramname;
                    NNAstToken vargname;
                    (void)paramid;
                    (void)paramname;
                    (void)defvalconst;
                    paramid = 0;
                    /* compile argument list... */
                    do
                    {
                        m_sharedprs->ignorewhitespace();
                        m_sharedprs->m_currentfunccompiler->targetfunc->fnscriptfunc.arity++;
                        if(m_sharedprs->match(NNAstToken::T_TRIPLEDOT))
                        {
                            m_sharedprs->m_currentfunccompiler->targetfunc->fnscriptfunc.isvariadic = true;
                            m_sharedprs->consume(NNAstToken::T_IDENTNORMAL, "expected identifier after '...'");
                            vargname = m_sharedprs->m_prevtoken;
                            m_sharedprs->addlocal(vargname);
                            m_sharedprs->definevariable(0);
                            break;
                        }
                        paramconst = m_sharedprs->parsefuncparamvar("expected parameter name");
                        paramname = m_sharedprs->m_prevtoken;
                        m_sharedprs->definevariable(paramconst);
                        m_sharedprs->ignorewhitespace();
                        if(m_sharedprs->match(NNAstToken::T_ASSIGN))
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
                                    m_sharedprs->emitbyteandshort(NNInstruction::OPC_FUNCARGOPTIONAL, defvalconst);
                                    // emit1short(paramid);
                                #else
                                    m_sharedprs->emitbyteandshort(NNInstruction::OPC_LOCALSET, defvalconst);
                                #endif
                            #endif
                        }
                        m_sharedprs->ignorewhitespace();
                        paramid++;

                    } while(m_sharedprs->match(NNAstToken::T_COMMA));
                }

                void compileBody(bool closescope, bool isanon)
                {
                    int i;
                    NNObjFunction* function;
                    (void)isanon;
                    auto gcs = SharedState::get();
                    /* compile the body */
                    m_sharedprs->ignorewhitespace();
                    m_sharedprs->consume(NNAstToken::T_BRACEOPEN, "expected '{' before function body");
                    m_sharedprs->parseblock();
                    /* create the function object */
                    if(closescope)
                    {
                        m_sharedprs->scopeend();
                    }
                    function = m_sharedprs->endcompiler(false);
                    gcs->stackPush(NNValue::fromObject(function));
                    m_sharedprs->emitbyteandshort(NNInstruction::OPC_MAKECLOSURE, m_sharedprs->pushconst(NNValue::fromObject(function)));
                    for(i = 0; i < function->upvalcount; i++)
                    {
                        m_sharedprs->emit1byte(this->upvalues[i].islocal ? 1 : 0);
                        m_sharedprs->emit1short(this->upvalues[i].index);
                    }
                    gcs->stackPop();
                }


        };

        class ClassCompiler
        {
            public:
                bool hassuperclass;
                ClassCompiler* enclosing;
                NNAstToken name;
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
        NNAstCompContext m_compcontext;
        const char* m_currentfile;
        NNAstLexer* m_lexer;
        NNAstToken m_currtoken;
        NNAstToken m_prevtoken;
        FuncCompiler* m_currentfunccompiler;
        ClassCompiler* m_currentclasscompiler;
        NNObjModule* m_currentmodule;

    public:
        static NNAstParser* make(NNAstLexer* m_lexer, NNObjModule* module, bool keeplast)
        {
            NNAstParser* parser;
            parser = Memory::make<NNAstParser>();
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
            parser->m_currentfile = nn_string_getdata(parser->m_currentmodule->physicalpath);
            return parser;
        }

        static void destroy(NNAstParser* parser)
        {
            nn_memory_free(parser);
        }

        static NNValue utilConvertNumberString(NNAstToken::Type type, const char* source)
        {
            double dbval;
            long longval;
            int64_t llval;
            if(type == NNAstToken::T_LITNUMBIN)
            {
                llval = strtoll(source + 2, nullptr, 2);
                return NNValue::makeNumber(llval);
            }
            else if(type == NNAstToken::T_LITNUMOCT)
            {
                longval = strtol(source + 2, nullptr, 8);
                return NNValue::makeNumber(longval);
            }
            else if(type == NNAstToken::T_LITNUMHEX)
            {
                longval = strtol(source, nullptr, 16);
                return NNValue::makeNumber(longval);
            }
            dbval = strtod(source, nullptr);
            return NNValue::makeNumber(dbval);
        }

        static NNAstToken utilMakeSynthToken(const char* name)
        {
            NNAstToken token;
            token.isglobal = false;
            token.line = 0;
            token.type = (NNAstToken::Type)0;
            token.start = name;
            token.length = (int)strlen(name);
            return token;
        }

        static bool utilIdentsEqual(NNAstToken* a, NNAstToken* b)
        {
            return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
        }

        static bool astruleanonfunc(NNAstParser* prs, bool canassign)
        {
            (void)canassign;
            FuncCompiler fnc(prs, NNObjFunction::CTXTYPE_FUNCTION, true);
            prs->scopebegin();
            /* compile parameter list */
            if(prs->check(NNAstToken::T_IDENTNORMAL))
            {
                prs->consume(NNAstToken::T_IDENTNORMAL, "optional name for anonymous function");
            }
            prs->consume(NNAstToken::T_PARENOPEN, "expected '(' at start of anonymous function");
            if(!prs->check(NNAstToken::T_PARENCLOSE))
            {
                fnc.parseParamList();
            }
            prs->consume(NNAstToken::T_PARENCLOSE, "expected ')' after anonymous function parameters");
            fnc.compileBody(true, true);
            return true;
        }

        static bool astruleanonclass(NNAstParser* prs, bool canassign)
        {
            (void)canassign;
            prs->parseclassdeclaration(false);
            return true;
        }

        static bool astrulenumber(NNAstParser* prs, bool canassign)
        {
            (void)canassign;
            prs->emitconst(prs->compilenumber());
            return true;
        }

        static bool astrulebinary(NNAstParser* prs, NNAstToken previous, bool canassign)
        {
            NNAstToken::Type op;
            Rule* rule;
            (void)previous;
            (void)canassign;
            op = prs->m_prevtoken.type;
            /* compile the right operand */
            rule = getRule(op);
            prs->parseprecedence((Rule::Precedence)(rule->precedence + 1));
            /* emit the operator instruction */
            switch(op)
            {
                case NNAstToken::T_PLUS:
                    prs->emitinstruc(NNInstruction::OPC_PRIMADD);
                    break;
                case NNAstToken::T_MINUS:
                    prs->emitinstruc(NNInstruction::OPC_PRIMSUBTRACT);
                    break;
                case NNAstToken::T_MULTIPLY:
                    prs->emitinstruc(NNInstruction::OPC_PRIMMULTIPLY);
                    break;
                case NNAstToken::T_DIVIDE:
                    prs->emitinstruc(NNInstruction::OPC_PRIMDIVIDE);
                    break;
                case NNAstToken::T_MODULO:
                    prs->emitinstruc(NNInstruction::OPC_PRIMMODULO);
                    break;
                case NNAstToken::T_POWEROF:
                    prs->emitinstruc(NNInstruction::OPC_PRIMPOW);
                    break;
                    /* equality */
                case NNAstToken::T_EQUAL:
                    prs->emitinstruc(NNInstruction::OPC_EQUAL);
                    break;
                case NNAstToken::T_NOTEQUAL:
                    prs->emit2byte(NNInstruction::OPC_EQUAL, NNInstruction::OPC_PRIMNOT);
                    break;
                case NNAstToken::T_GREATERTHAN:
                    prs->emitinstruc(NNInstruction::OPC_PRIMGREATER);
                    break;
                case NNAstToken::T_GREATER_EQ:
                    prs->emit2byte(NNInstruction::OPC_PRIMLESSTHAN, NNInstruction::OPC_PRIMNOT);
                    break;
                case NNAstToken::T_LESSTHAN:
                    prs->emitinstruc(NNInstruction::OPC_PRIMLESSTHAN);
                    break;
                case NNAstToken::T_LESSEQUAL:
                    prs->emit2byte(NNInstruction::OPC_PRIMGREATER, NNInstruction::OPC_PRIMNOT);
                    break;
                    /* bitwise */
                case NNAstToken::T_AMP:
                    prs->emitinstruc(NNInstruction::OPC_PRIMAND);
                    break;
                case NNAstToken::T_BAR:
                    prs->emitinstruc(NNInstruction::OPC_PRIMOR);
                    break;
                case NNAstToken::T_XOR:
                    prs->emitinstruc(NNInstruction::OPC_PRIMBITXOR);
                    break;
                case NNAstToken::T_LEFTSHIFT:
                    prs->emitinstruc(NNInstruction::OPC_PRIMSHIFTLEFT);
                    break;
                case NNAstToken::T_RIGHTSHIFT:
                    prs->emitinstruc(NNInstruction::OPC_PRIMSHIFTRIGHT);
                    break;
                    /* range */
                case NNAstToken::T_DOUBLEDOT:
                    prs->emitinstruc(NNInstruction::OPC_MAKERANGE);
                    break;
                default:
                    break;
            }
            return true;
        }

        static bool astrulecall(NNAstParser* prs, NNAstToken previous, bool canassign)
        {
            uint32_t argcount;
            (void)previous;
            (void)canassign;
            argcount = prs->parsefunccallargs();
            prs->emit2byte(NNInstruction::OPC_CALLFUNCTION, argcount);
            return true;
        }

        static bool astruleliteral(NNAstParser* prs, bool canassign)
        {
            (void)canassign;
            switch(prs->m_prevtoken.type)
            {
                case NNAstToken::T_KWNULL:
                    prs->emitinstruc(NNInstruction::OPC_PUSHNULL);
                    break;
                case NNAstToken::T_KWTRUE:
                    prs->emitinstruc(NNInstruction::OPC_PUSHTRUE);
                    break;
                case NNAstToken::T_KWFALSE:
                    prs->emitinstruc(NNInstruction::OPC_PUSHFALSE);
                    break;
                default:
                    /* TODO: assuming this is correct behaviour ... */
                    return false;
            }
            return true;
        }

        static bool astruledot(NNAstParser* prs, NNAstToken previous, bool canassign)
        {
            int name;
            bool caninvoke;
            uint16_t argcount;
            NNInstruction::OpCode getop;
            NNInstruction::OpCode setop;
            prs->ignorewhitespace();
            if(!prs->consume(NNAstToken::T_IDENTNORMAL, "expected property name after '.'"))
            {
                return false;
            }
            name = prs->makeidentconst(&prs->m_prevtoken);
            if(prs->match(NNAstToken::T_PARENOPEN))
            {
                argcount = prs->parsefunccallargs();
                caninvoke = ((prs->m_currentclasscompiler != nullptr) && ((previous.type == NNAstToken::T_KWTHIS) || (utilIdentsEqual(&prs->m_prevtoken, &prs->m_currentclasscompiler->name))));
                if(caninvoke)
                {
                    prs->emitbyteandshort(NNInstruction::OPC_CLASSINVOKETHIS, name);
                }
                else
                {
                    prs->emitbyteandshort(NNInstruction::OPC_CALLMETHOD, name);
                }
                prs->emit1byte(argcount);
            }
            else
            {
                getop = NNInstruction::OPC_PROPERTYGET;
                setop = NNInstruction::OPC_PROPERTYSET;
                if(prs->m_currentclasscompiler != nullptr && (previous.type == NNAstToken::T_KWTHIS || utilIdentsEqual(&prs->m_prevtoken, &prs->m_currentclasscompiler->name)))
                {
                    getop = NNInstruction::OPC_PROPERTYGETSELF;
                }
                prs->assignment(getop, setop, name, canassign);
            }
            return true;
        }

        static bool astrulearray(NNAstParser* prs, bool canassign)
        {
            int count;
            (void)canassign;
            /* placeholder for the list */
            prs->emitinstruc(NNInstruction::OPC_PUSHNULL);
            count = 0;
            prs->ignorewhitespace();
            if(!prs->check(NNAstToken::T_BRACKETCLOSE))
            {
                do
                {
                    prs->ignorewhitespace();
                    if(!prs->check(NNAstToken::T_BRACKETCLOSE))
                    {
                        /* allow comma to end lists */
                        prs->parseexpression();
                        prs->ignorewhitespace();
                        count++;
                    }
                    prs->ignorewhitespace();
                } while(prs->match(NNAstToken::T_COMMA));
            }
            prs->ignorewhitespace();
            prs->consume(NNAstToken::T_BRACKETCLOSE, "expected ']' at end of list");
            prs->emitbyteandshort(NNInstruction::OPC_MAKEARRAY, count);
            return true;
        }

        static bool astruledictionary(NNAstParser* prs, bool canassign)
        {
            bool usedexpression;
            int itemcount;
            (void)canassign;
            /* placeholder for the dictionary */
            prs->emitinstruc(NNInstruction::OPC_PUSHNULL);
            itemcount = 0;
            prs->ignorewhitespace();
            if(!prs->check(NNAstToken::T_BRACECLOSE))
            {
                do
                {
                    prs->ignorewhitespace();
                    if(!prs->check(NNAstToken::T_BRACECLOSE))
                    {
                        /* allow last pair to end with a comma */
                        usedexpression = false;
                        if(prs->check(NNAstToken::T_IDENTNORMAL))
                        {
                            prs->consume(NNAstToken::T_IDENTNORMAL, "");
                            prs->emitconst(NNValue::fromObject(nn_string_copylen(prs->m_prevtoken.start, prs->m_prevtoken.length)));
                        }
                        else
                        {
                            prs->parseexpression();
                            usedexpression = true;
                        }
                        prs->ignorewhitespace();
                        if(!prs->check(NNAstToken::T_COMMA) && !prs->check(NNAstToken::T_BRACECLOSE))
                        {
                            prs->consume(NNAstToken::T_COLON, "expected ':' after dictionary key");
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
                } while(prs->match(NNAstToken::T_COMMA));
            }
            prs->ignorewhitespace();
            prs->consume(NNAstToken::T_BRACECLOSE, "expected '}' after dictionary");
            prs->emitbyteandshort(NNInstruction::OPC_MAKEDICT, itemcount);
            return true;
        }

        static bool astruleindexing(NNAstParser* prs, NNAstToken previous, bool canassign)
        {
            bool assignable;
            bool commamatch;
            uint16_t getop;
            (void)previous;
            (void)canassign;
            assignable = true;
            commamatch = false;
            getop = NNInstruction::OPC_INDEXGET;
            if(prs->match(NNAstToken::T_COMMA))
            {
                prs->emitinstruc(NNInstruction::OPC_PUSHNULL);
                commamatch = true;
                getop = NNInstruction::OPC_INDEXGETRANGED;
            }
            else
            {
                prs->parseexpression();
            }
            if(!prs->match(NNAstToken::T_BRACKETCLOSE))
            {
                getop = NNInstruction::OPC_INDEXGETRANGED;
                if(!commamatch)
                {
                    prs->consume(NNAstToken::T_COMMA, "expecting ',' or ']'");
                }
                if(prs->match(NNAstToken::T_BRACKETCLOSE))
                {
                    prs->emitinstruc(NNInstruction::OPC_PUSHNULL);
                }
                else
                {
                    prs->parseexpression();
                    prs->consume(NNAstToken::T_BRACKETCLOSE, "expected ']' after indexing");
                }
                assignable = false;
            }
            else
            {
                if(commamatch)
                {
                    prs->emitinstruc(NNInstruction::OPC_PUSHNULL);
                }
            }
            prs->assignment(getop, NNInstruction::OPC_INDEXSET, -1, assignable);
            return true;
        }

        static bool astrulevarnormal(NNAstParser* prs, bool canassign)
        {
            prs->namedvar(prs->m_prevtoken, canassign);
            return true;
        }

        static bool astrulethis(NNAstParser* prs, bool canassign)
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
                prs->emitinstruc(NNInstruction::OPC_CLASSGETTHIS);
        #endif
            return true;
        }

        static bool astrulesuper(NNAstParser* prs, bool canassign)
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
            if(!prs->check(NNAstToken::T_PARENOPEN))
            {
                prs->consume(NNAstToken::T_DOT, "expected '.' or '(' after super");
                prs->consume(NNAstToken::T_IDENTNORMAL, "expected super class method name after .");
                name = prs->makeidentconst(&prs->m_prevtoken);
            }
            else
            {
                invokeself = true;
            }
            prs->namedvar(utilMakeSynthToken(g_strthis), false);
            if(prs->match(NNAstToken::T_PARENOPEN))
            {
                argcount = prs->parsefunccallargs();
                prs->namedvar(utilMakeSynthToken(g_strsuper), false);
                if(!invokeself)
                {
                    prs->emitbyteandshort(NNInstruction::OPC_CLASSINVOKESUPER, name);
                    prs->emit1byte(argcount);
                }
                else
                {
                    prs->emit2byte(NNInstruction::OPC_CLASSINVOKESUPERSELF, argcount);
                }
            }
            else
            {
                prs->namedvar(utilMakeSynthToken(g_strsuper), false);
                prs->emitbyteandshort(NNInstruction::OPC_CLASSGETSUPER, name);
            }
            return true;
        }

        static bool astrulegrouping(NNAstParser* prs, bool canassign)
        {
            (void)canassign;
            prs->ignorewhitespace();
            prs->parseexpression();
            while(prs->match(NNAstToken::T_COMMA))
            {
                prs->parseexpression();
            }
            prs->ignorewhitespace();
            prs->consume(NNAstToken::T_PARENCLOSE, "expected ')' after grouped expression");
            return true;
        }

        static bool astrulestring(NNAstParser* prs, bool canassign)
        {
            int length;
            char* str;
            (void)canassign;
            str = prs->compilestring(&length, true);
            prs->emitconst(NNValue::fromObject(nn_string_takelen(str, length)));
            return true;
        }

        static bool astrulerawstring(NNAstParser* prs, bool canassign)
        {
            int length;
            char* str;
            (void)canassign;
            str = prs->compilestring(&length, false);
            prs->emitconst(NNValue::fromObject(nn_string_takelen(str, length)));
            return true;
        }

        static bool astruleinterpolstring(NNAstParser* prs, bool canassign)
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
                        prs->emitinstruc(NNInstruction::OPC_PRIMADD);
                    }
                }
                prs->parseexpression();
                prs->emitinstruc(NNInstruction::OPC_STRINGIFY);
                if(doadd || (count >= 1 && stringmatched == false))
                {
                    prs->emitinstruc(NNInstruction::OPC_PRIMADD);
                }
                count++;
            } while(prs->match(NNAstToken::T_INTERPOLATION));
            prs->consume(NNAstToken::T_LITERALSTRING, "unterminated string interpolation");
            if(prs->m_prevtoken.length - 2 > 0)
            {
                astrulestring(prs, canassign);
                prs->emitinstruc(NNInstruction::OPC_PRIMADD);
            }
            return true;
        }

        static bool astruleunary(NNAstParser* prs, bool canassign)
        {
            NNAstToken::Type op;
            (void)canassign;
            op = prs->m_prevtoken.type;
            /* compile the expression */
            prs->parseprecedence(Rule::PREC_UNARY);
            /* emit instruction */
            switch(op)
            {
                case NNAstToken::T_MINUS:
                    prs->emitinstruc(NNInstruction::OPC_PRIMNEGATE);
                    break;
                case NNAstToken::T_EXCLMARK:
                    prs->emitinstruc(NNInstruction::OPC_PRIMNOT);
                    break;
                case NNAstToken::T_TILDE:
                    prs->emitinstruc(NNInstruction::OPC_PRIMBITNOT);
                    break;
                default:
                    break;
            }
            return true;
        }

        static bool astruleand(NNAstParser* prs, NNAstToken previous, bool canassign)
        {
            int endjump;
            (void)previous;
            (void)canassign;
            endjump = prs->emitjump(NNInstruction::OPC_JUMPIFFALSE);
            prs->emitinstruc(NNInstruction::OPC_POPONE);
            prs->parseprecedence(Rule::PREC_AND);
            prs->patchjump(endjump);
            return true;
        }

        static bool astruleor(NNAstParser* prs, NNAstToken previous, bool canassign)
        {
            int endjump;
            int elsejump;
            (void)previous;
            (void)canassign;
            elsejump = prs->emitjump(NNInstruction::OPC_JUMPIFFALSE);
            endjump = prs->emitjump(NNInstruction::OPC_JUMPNOW);
            prs->patchjump(elsejump);
            prs->emitinstruc(NNInstruction::OPC_POPONE);
            prs->parseprecedence(Rule::PREC_OR);
            prs->patchjump(endjump);
            return true;
        }

        static bool astruleinstanceof(NNAstParser* prs, NNAstToken previous, bool canassign)
        {
            (void)previous;
            (void)canassign;
            prs->parseexpression();
            prs->emitinstruc(NNInstruction::OPC_OPINSTANCEOF);

            return true;
        }

        static bool astruleconditional(NNAstParser* prs, NNAstToken previous, bool canassign)
        {
            int thenjump;
            int elsejump;
            (void)previous;
            (void)canassign;
            thenjump = prs->emitjump(NNInstruction::OPC_JUMPIFFALSE);
            prs->emitinstruc(NNInstruction::OPC_POPONE);
            prs->ignorewhitespace();
            /* compile the then expression */
            prs->parseprecedence(Rule::PREC_CONDITIONAL);
            prs->ignorewhitespace();
            elsejump = prs->emitjump(NNInstruction::OPC_JUMPNOW);
            prs->patchjump(thenjump);
            prs->emitinstruc(NNInstruction::OPC_POPONE);
            prs->consume(NNAstToken::T_COLON, "expected matching ':' after '?' conditional");
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

        static bool astruleimport(NNAstParser* prs, bool canassign)
        {
            (void)canassign;
            prs->parseexpression();
            prs->emitinstruc(NNInstruction::OPC_IMPORTIMPORT);
            return true;
        }

        static bool astrulenew(NNAstParser* prs, bool canassign)
        {
            prs->consume(NNAstToken::T_IDENTNORMAL, "class name after 'new'");
            return astrulevarnormal(prs, canassign);
        }

        static bool astruletypeof(NNAstParser* prs, bool canassign)
        {
            (void)canassign;
            prs->consume(NNAstToken::T_PARENOPEN, "expected '(' after 'typeof'");
            prs->parseexpression();
            prs->consume(NNAstToken::T_PARENCLOSE, "expected ')' after 'typeof'");
            prs->emitinstruc(NNInstruction::OPC_TYPEOF);
            return true;
        }

        static bool astrulenothingprefix(NNAstParser* prs, bool canassign)
        {
            (void)prs;
            (void)canassign;
            return true;
        }

        static bool astrulenothinginfix(NNAstParser* prs, NNAstToken previous, bool canassign)
        {
            (void)prs;
            (void)previous;
            (void)canassign;
            return true;
        }

        static Rule* putrule(Rule* dest, NNAstParsePrefixFN prefix, NNAstParseInfixFN infix, Rule::Precedence precedence)
        {
            dest->prefix = prefix;
            dest->infix = infix;
            dest->precedence = precedence;
            return dest;
        }

        #define dorule(tok, prefix, infix, precedence) \
            case tok:                                  \
                return putrule(&dest, prefix, infix, precedence);

        static Rule* getRule(NNAstToken::Type type)
        {
            static Rule dest;
            switch(type)
            {
                dorule(NNAstToken::T_NEWLINE, astrulenothingprefix, astrulenothinginfix, Rule::PREC_NONE);
                dorule(NNAstToken::T_PARENOPEN, astrulegrouping, astrulecall, Rule::PREC_CALL);
                dorule(NNAstToken::T_PARENCLOSE, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_BRACKETOPEN, astrulearray, astruleindexing, Rule::PREC_CALL);
                dorule(NNAstToken::T_BRACKETCLOSE, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_BRACEOPEN, astruledictionary, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_BRACECLOSE, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_SEMICOLON, astrulenothingprefix, astrulenothinginfix, Rule::PREC_NONE);
                dorule(NNAstToken::T_COMMA, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_BACKSLASH, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_EXCLMARK, astruleunary, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_NOTEQUAL, nullptr, astrulebinary, Rule::PREC_EQUALITY);
                dorule(NNAstToken::T_COLON, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_AT, astruleanonfunc, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_DOT, nullptr, astruledot, Rule::PREC_CALL);
                dorule(NNAstToken::T_DOUBLEDOT, nullptr, astrulebinary, Rule::PREC_RANGE);
                dorule(NNAstToken::T_TRIPLEDOT, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_PLUS, astruleunary, astrulebinary, Rule::PREC_TERM);
                dorule(NNAstToken::T_PLUSASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_INCREMENT, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_MINUS, astruleunary, astrulebinary, Rule::PREC_TERM);
                dorule(NNAstToken::T_MINUSASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_DECREMENT, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_MULTIPLY, nullptr, astrulebinary, Rule::PREC_FACTOR);
                dorule(NNAstToken::T_MULTASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_POWEROF, nullptr, astrulebinary, Rule::PREC_FACTOR);
                dorule(NNAstToken::T_POWASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_DIVIDE, nullptr, astrulebinary, Rule::PREC_FACTOR);
                dorule(NNAstToken::T_DIVASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_EQUAL, nullptr, astrulebinary, Rule::PREC_EQUALITY);
                dorule(NNAstToken::T_LESSTHAN, nullptr, astrulebinary, Rule::PREC_COMPARISON);
                dorule(NNAstToken::T_LESSEQUAL, nullptr, astrulebinary, Rule::PREC_COMPARISON);
                dorule(NNAstToken::T_LEFTSHIFT, nullptr, astrulebinary, Rule::PREC_SHIFT);
                dorule(NNAstToken::T_LEFTSHIFTASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_GREATERTHAN, nullptr, astrulebinary, Rule::PREC_COMPARISON);
                dorule(NNAstToken::T_GREATER_EQ, nullptr, astrulebinary, Rule::PREC_COMPARISON);
                dorule(NNAstToken::T_RIGHTSHIFT, nullptr, astrulebinary, Rule::PREC_SHIFT);
                dorule(NNAstToken::T_RIGHTSHIFTASSIGN, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_MODULO, nullptr, astrulebinary, Rule::PREC_FACTOR);
                dorule(NNAstToken::T_PERCENT_EQ, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_AMP, nullptr, astrulebinary, Rule::PREC_BITAND);
                dorule(NNAstToken::T_AMP_EQ, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_BAR, /*astruleanoncompat*/ nullptr, astrulebinary, Rule::PREC_BITOR);
                dorule(NNAstToken::T_BAR_EQ, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_TILDE, astruleunary, nullptr, Rule::PREC_UNARY);
                dorule(NNAstToken::T_TILDE_EQ, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_XOR, nullptr, astrulebinary, Rule::PREC_BITXOR);
                dorule(NNAstToken::T_XOR_EQ, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_QUESTION, nullptr, astruleconditional, Rule::PREC_CONDITIONAL);
                dorule(NNAstToken::T_KWAND, nullptr, astruleand, Rule::PREC_AND);
                dorule(NNAstToken::T_KWAS, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWASSERT, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWBREAK, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWCLASS, astruleanonclass, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWCONTINUE, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWFUNCTION, astruleanonfunc, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWDEFAULT, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWTHROW, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWDO, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWECHO, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWELSE, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWFALSE, astruleliteral, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWFOREACH, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWIF, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWIMPORT, astruleimport, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWIN, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWINSTANCEOF, nullptr, astruleinstanceof, Rule::PREC_OR);
                dorule(NNAstToken::T_KWFOR, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWVAR, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWNULL, astruleliteral, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWNEW, astrulenew, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWTYPEOF, astruletypeof, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWOR, nullptr, astruleor, Rule::PREC_OR);
                dorule(NNAstToken::T_KWSUPER, astrulesuper, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWRETURN, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWTHIS, astrulethis, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWSTATIC, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWTRUE, astruleliteral, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWSWITCH, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWCASE, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWWHILE, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWTRY, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWCATCH, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWFINALLY, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_LITERALSTRING, astrulestring, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_LITERALRAWSTRING, astrulerawstring, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_LITNUMREG, astrulenumber, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_LITNUMBIN, astrulenumber, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_LITNUMOCT, astrulenumber, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_LITNUMHEX, astrulenumber, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_IDENTNORMAL, astrulevarnormal, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_INTERPOLATION, astruleinterpolstring, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_EOF, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_ERROR, nullptr, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_KWEMPTY, astruleliteral, nullptr, Rule::PREC_NONE);
                dorule(NNAstToken::T_UNDEFINED, nullptr, nullptr, Rule::PREC_NONE);
                default:
                    fprintf(stderr, "missing rule?\n");
                    break;
            }
            return nullptr;
        }
        #undef dorule

    public:

        NNBlob* currentblob()
        {
            return &m_currentfunccompiler->targetfunc->fnscriptfunc.blob;
        }

        bool raiseerroratv(NNAstToken* t, const char* message, va_list args)
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
            fprintf(stderr, " in [%s:%d]: ", nn_string_getdata(m_currentmodule->physicalpath), t->line);
            vfprintf(stderr, message, args);
            fprintf(stderr, " ");
            if(t->type == NNAstToken::T_EOF)
            {
                fprintf(stderr, " at end");
            }
            else if(t->type == NNAstToken::T_ERROR)
            {
                /* do nothing */
                fprintf(stderr, "at <internal error>");
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
                if(m_currtoken.type != NNAstToken::T_ERROR)
                {
                    break;
                }
                raiseerroratcurrent(m_currtoken.start);
            }
        }


        bool istype(NNAstToken::Type prev, NNAstToken::Type t)
        {
            if(t == NNAstToken::T_IDENTNORMAL)
            {
                if(prev == NNAstToken::T_KWCLASS)
                {
                    return true;
                }
            }
            return (prev == t);
        }


        bool consume(NNAstToken::Type t, const char* message)
        {
            if(istype(m_currtoken.type, t))
            {
                advance();
                return true;
            }
            return raiseerroratcurrent(message);
        }

        void consumeor(const char* message, const NNAstToken::Type* ts, int count)
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
            raiseerroratcurrent(message);
        }

        bool checknumber()
        {
            NNAstToken::Type t;
            t = m_prevtoken.type;
            if(t == NNAstToken::T_LITNUMREG || t == NNAstToken::T_LITNUMOCT || t == NNAstToken::T_LITNUMBIN || t == NNAstToken::T_LITNUMHEX)
            {
                return true;
            }
            return false;
        }

        bool check(NNAstToken::Type t)
        {
            return istype(m_currtoken.type, t);
        }

        bool match(NNAstToken::Type t)
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
            while(!match(NNAstToken::T_EOF))
            {
                parsedeclaration();
            }
        }

        void parsedeclaration()
        {
            ignorewhitespace();
            if(match(NNAstToken::T_KWCLASS))
            {
                parseclassdeclaration(true);
            }
            else if(match(NNAstToken::T_KWFUNCTION))
            {
                parsefuncdecl();
            }
            else if(match(NNAstToken::T_KWVAR))
            {
                parsevardecl(false, false);
            }
            else if(match(NNAstToken::T_KWCONST))
            {
                parsevardecl(false, true);
            }
            else if(match(NNAstToken::T_BRACEOPEN))
            {
                if(!check(NNAstToken::T_NEWLINE) && m_currentfunccompiler->scopedepth == 0)
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
            if(match(NNAstToken::T_KWECHO))
            {
                parseechostmt();
            }
            else if(match(NNAstToken::T_KWIF))
            {
                parseifstmt();
            }
            else if(match(NNAstToken::T_KWDO))
            {
                parsedo_whilestmt();
            }
            else if(match(NNAstToken::T_KWWHILE))
            {
                parsewhilestmt();
            }
            else if(match(NNAstToken::T_KWFOR))
            {
                parseforstmt();
            }
            else if(match(NNAstToken::T_KWFOREACH))
            {
                parseforeachstmt();
            }
            else if(match(NNAstToken::T_KWSWITCH))
            {
                parseswitchstmt();
            }
            else if(match(NNAstToken::T_KWCONTINUE))
            {
                parsecontinuestmt();
            }
            else if(match(NNAstToken::T_KWBREAK))
            {
                parsebreakstmt();
            }
            else if(match(NNAstToken::T_KWRETURN))
            {
                parsereturnstmt();
            }
            else if(match(NNAstToken::T_KWASSERT))
            {
                parseassertstmt();
            }
            else if(match(NNAstToken::T_KWTHROW))
            {
                parsethrowstmt();
            }
            else if(match(NNAstToken::T_BRACEOPEN))
            {
                scopebegin();
                parseblock();
                scopeend();
            }
            else if(match(NNAstToken::T_KWTRY))
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
            if(m_blockcount > 0 && check(NNAstToken::T_BRACECLOSE))
            {
                return;
            }
            if(match(NNAstToken::T_SEMICOLON))
            {
                while(match(NNAstToken::T_SEMICOLON) || match(NNAstToken::T_NEWLINE))
                {
                }
                return;
            }
            if(match(NNAstToken::T_EOF) || m_prevtoken.type == NNAstToken::T_EOF)
            {
                return;
            }
            /* consume(NNAstToken::T_NEWLINE, "end of statement expected"); */
            while(match(NNAstToken::T_SEMICOLON) || match(NNAstToken::T_NEWLINE))
            {
            }
        }

        void ignorewhitespace()
        {
            while(true)
            {
                if(check(NNAstToken::T_NEWLINE))
                {
                    advance();
                }
                else
                {
                    break;
                }
            }
        }

        int getcodeargscount(const NNInstruction* bytecode, const NNValue* constants, int ip)
        {
            int constant;
            NNInstruction::OpCode code;
            NNObjFunction* fn;
            code = (NNInstruction::OpCode)bytecode[ip].code;
            switch(code)
            {
                case NNInstruction::OPC_EQUAL:
                case NNInstruction::OPC_PRIMGREATER:
                case NNInstruction::OPC_PRIMLESSTHAN:
                case NNInstruction::OPC_PUSHNULL:
                case NNInstruction::OPC_PUSHTRUE:
                case NNInstruction::OPC_PUSHFALSE:
                case NNInstruction::OPC_PRIMADD:
                case NNInstruction::OPC_PRIMSUBTRACT:
                case NNInstruction::OPC_PRIMMULTIPLY:
                case NNInstruction::OPC_PRIMDIVIDE:
                case NNInstruction::OPC_PRIMMODULO:
                case NNInstruction::OPC_PRIMPOW:
                case NNInstruction::OPC_PRIMNEGATE:
                case NNInstruction::OPC_PRIMNOT:
                case NNInstruction::OPC_ECHO:
                case NNInstruction::OPC_TYPEOF:
                case NNInstruction::OPC_POPONE:
                case NNInstruction::OPC_UPVALUECLOSE:
                case NNInstruction::OPC_DUPONE:
                case NNInstruction::OPC_RETURN:
                case NNInstruction::OPC_CLASSINHERIT:
                case NNInstruction::OPC_CLASSGETSUPER:
                case NNInstruction::OPC_PRIMAND:
                case NNInstruction::OPC_PRIMOR:
                case NNInstruction::OPC_PRIMBITXOR:
                case NNInstruction::OPC_PRIMSHIFTLEFT:
                case NNInstruction::OPC_PRIMSHIFTRIGHT:
                case NNInstruction::OPC_PRIMBITNOT:
                case NNInstruction::OPC_PUSHONE:
                case NNInstruction::OPC_INDEXSET:
                case NNInstruction::OPC_ASSERT:
                case NNInstruction::OPC_EXTHROW:
                case NNInstruction::OPC_EXPOPTRY:
                case NNInstruction::OPC_MAKERANGE:
                case NNInstruction::OPC_STRINGIFY:
                case NNInstruction::OPC_PUSHEMPTY:
                case NNInstruction::OPC_EXPUBLISHTRY:
                case NNInstruction::OPC_CLASSGETTHIS:
                case NNInstruction::OPC_HALT:
                    return 0;
                case NNInstruction::OPC_CALLFUNCTION:
                case NNInstruction::OPC_CLASSINVOKESUPERSELF:
                case NNInstruction::OPC_INDEXGET:
                case NNInstruction::OPC_INDEXGETRANGED:
                    return 1;
                case NNInstruction::OPC_GLOBALDEFINE:
                case NNInstruction::OPC_GLOBALGET:
                case NNInstruction::OPC_GLOBALSET:
                case NNInstruction::OPC_LOCALGET:
                case NNInstruction::OPC_LOCALSET:
                case NNInstruction::OPC_FUNCARGOPTIONAL:
                case NNInstruction::OPC_FUNCARGSET:
                case NNInstruction::OPC_FUNCARGGET:
                case NNInstruction::OPC_UPVALUEGET:
                case NNInstruction::OPC_UPVALUESET:
                case NNInstruction::OPC_JUMPIFFALSE:
                case NNInstruction::OPC_JUMPNOW:
                case NNInstruction::OPC_BREAK_PL:
                case NNInstruction::OPC_LOOP:
                case NNInstruction::OPC_PUSHCONSTANT:
                case NNInstruction::OPC_POPN:
                case NNInstruction::OPC_MAKECLASS:
                case NNInstruction::OPC_PROPERTYGET:
                case NNInstruction::OPC_PROPERTYGETSELF:
                case NNInstruction::OPC_PROPERTYSET:
                case NNInstruction::OPC_MAKEARRAY:
                case NNInstruction::OPC_MAKEDICT:
                case NNInstruction::OPC_IMPORTIMPORT:
                case NNInstruction::OPC_SWITCH:
                case NNInstruction::OPC_MAKEMETHOD:
        #if 0
                case NNInstruction::OPC_FUNCOPTARG:
        #endif
                    return 2;
                case NNInstruction::OPC_CALLMETHOD:
                case NNInstruction::OPC_CLASSINVOKETHIS:
                case NNInstruction::OPC_CLASSINVOKESUPER:
                case NNInstruction::OPC_CLASSPROPERTYDEFINE:
                    return 3;
                case NNInstruction::OPC_EXTRY:
                    return 6;
                case NNInstruction::OPC_MAKECLOSURE:
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
            NNInstruction ins;
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
            emit(byte, m_prevtoken.line, true);
        }

        void emit1byte(uint16_t byte)
        {
            emit(byte, m_prevtoken.line, false);
        }

        void emit1short(uint16_t byte)
        {
            emit((byte >> 8) & 0xff, m_prevtoken.line, false);
            emit(byte & 0xff, m_prevtoken.line, false);
        }

        void emit2byte(uint16_t byte, uint16_t byte2)
        {
            emit(byte, m_prevtoken.line, false);
            emit(byte2, m_prevtoken.line, false);
        }

        void emitbyteandshort(uint16_t byte, uint16_t byte2)
        {
            emit(byte, m_prevtoken.line, false);
            emit((byte2 >> 8) & 0xff, m_prevtoken.line, false);
            emit(byte2 & 0xff, m_prevtoken.line, false);
        }

        void emitloop(int loopstart)
        {
            int offset;
            emitinstruc(NNInstruction::OPC_LOOP);
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
                emitinstruc(NNInstruction::OPC_EXPOPTRY);
            }
            if(m_currentfunccompiler->contexttype == NNObjFunction::CTXTYPE_INITIALIZER)
            {
                emitbyteandshort(NNInstruction::OPC_LOCALGET, 0);
            }
            else
            {
                if(!m_keeplastvalue || m_lastwasstatement)
                {
                    if(m_currentfunccompiler->fromimport)
                    {
                        emitinstruc(NNInstruction::OPC_PUSHNULL);
                    }
                    else
                    {
                        emitinstruc(NNInstruction::OPC_PUSHEMPTY);
                    }
                }
            }
            emitinstruc(NNInstruction::OPC_RETURN);
        }

        int pushconst(NNValue value)
        {
            int constant;
            constant = currentblob()->addConstant(value);
            return constant;
        }

        void emitconst(NNValue value)
        {
            int constant;
            constant = pushconst(value);
            emitbyteandshort(NNInstruction::OPC_PUSHCONSTANT, (uint16_t)constant);
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
            emitinstruc(NNInstruction::OPC_SWITCH);
            /* placeholders */
            emit1byte(0xff);
            emit1byte(0xff);
            return currentblob()->count - 2;
        }

        int emittry()
        {
            emitinstruc(NNInstruction::OPC_EXTRY);
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


        int makeidentconst(NNAstToken* name)
        {
            int rawlen;
            const char* rawstr;
            NNObjString* str;
            rawstr = name->start;
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
            str = nn_string_copylen(rawstr, rawlen);
            return pushconst(NNValue::fromObject(str));
        }



        int addlocal(NNAstToken name)
        {
            LocalVarInfo* local;
            if(m_currentfunccompiler->localcount == NEON_CONFIG_ASTMAXLOCALS)
            {
                /* we've reached maximum local variables per scope */
                raiseerror("too many local variables in scope");
                return -1;
            }
            local = &m_currentfunccompiler->locals[m_currentfunccompiler->localcount++];
            local->name = name;
            local->depth = -1;
            local->iscaptured = false;
            return m_currentfunccompiler->localcount;
        }

        void declarevariable()
        {
            int i;
            NNAstToken* name;
            LocalVarInfo* local;
            /* global variables are implicitly declared... */
            if(m_currentfunccompiler->scopedepth == 0)
            {
                return;
            }
            name = &m_prevtoken;
            for(i = m_currentfunccompiler->localcount - 1; i >= 0; i--)
            {
                local = &m_currentfunccompiler->locals[i];
                if(local->depth != -1 && local->depth < m_currentfunccompiler->scopedepth)
                {
                    break;
                }
                if(utilIdentsEqual(name, &local->name))
                {
                    raiseerror("%.*s already declared in current scope", name->length, name->start);
                }
            }
            addlocal(*name);
        }

        int parsevariable(const char* message)
        {
            if(!consume(NNAstToken::T_IDENTNORMAL, message))
            {
                /* what to do here? */
            }
            declarevariable();
            /* we are in a local scope... */
            if(m_currentfunccompiler->scopedepth > 0)
            {
                return 0;
            }
            return makeidentconst(&m_prevtoken);
        }

        void markinitialized()
        {
            if(m_currentfunccompiler->scopedepth == 0)
            {
                return;
            }
            m_currentfunccompiler->locals[m_currentfunccompiler->localcount - 1].depth = m_currentfunccompiler->scopedepth;
        }

        void definevariable(int global)
        {
            /* we are in a local scope... */
            if(m_currentfunccompiler->scopedepth > 0)
            {
                markinitialized();
                return;
            }
            emitbyteandshort(NNInstruction::OPC_GLOBALDEFINE, global);
        }


        NNObjFunction* endcompiler(bool istoplevel)
        {
            const char* fname;
            NNObjFunction* function;
            auto gcs = SharedState::get();
            emitreturn();
            if(istoplevel)
            {
            }
            function = m_currentfunccompiler->targetfunc;
            fname = nullptr;
            if(function->name == nullptr)
            {
                fname = nn_string_getdata(m_currentmodule->physicalpath);
            }
            else
            {
                fname = nn_string_getdata(function->name);
            }
            if(!m_haderror && gcs->m_conf.dumpbytecode)
            {
                nn_dbg_disasmblob(gcs->m_debugwriter, currentblob(), fname);
            }
            m_currentfunccompiler = m_currentfunccompiler->enclosing;
            return function;
        }

        void scopebegin()
        {
            m_currentfunccompiler->scopedepth++;
        }

        bool scopeCanEndContinue()
        {
            int lopos;
            int locount;
            int lodepth;
            int scodepth;
            locount = m_currentfunccompiler->localcount;
            lopos = m_currentfunccompiler->localcount - 1;
            lodepth = m_currentfunccompiler->locals[lopos].depth;
            scodepth = m_currentfunccompiler->scopedepth;
            if(locount > 0 && lodepth > scodepth)
            {
                return true;
            }
            return false;
        }

        void scopeend()
        {
            m_currentfunccompiler->scopedepth--;
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
                if(m_currentfunccompiler->locals[m_currentfunccompiler->localcount - 1].iscaptured)
                {
                    emitinstruc(NNInstruction::OPC_UPVALUECLOSE);
                }
                else
                {
                    emitinstruc(NNInstruction::OPC_POPONE);
                }
                m_currentfunccompiler->localcount--;
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
            if(m_currentfunccompiler->scopedepth == -1)
            {
                raiseerror("cannot exit top-level scope");
            }
            local = m_currentfunccompiler->localcount - 1;
            while(local >= 0 && m_currentfunccompiler->locals[local].depth >= depth)
            {
                if(m_currentfunccompiler->locals[local].iscaptured)
                {
                    emitinstruc(NNInstruction::OPC_UPVALUECLOSE);
                }
                else
                {
                    emitinstruc(NNInstruction::OPC_POPONE);
                }
                local--;
            }
            return m_currentfunccompiler->localcount - local - 1;
        }

        void endloop()
        {
            int i;
            NNInstruction* bcode;
            NNValue* cvals;
            /*
            // find all NNInstruction::OPC_BREAK_PL placeholder and replace with the appropriate jump...
            */
            i = m_innermostloopstart;
            while(i < m_currentfunccompiler->targetfunc->fnscriptfunc.blob.count)
            {
                if(m_currentfunccompiler->targetfunc->fnscriptfunc.blob.instrucs[i].code == NNInstruction::OPC_BREAK_PL)
                {
                    m_currentfunccompiler->targetfunc->fnscriptfunc.blob.instrucs[i].code = NNInstruction::OPC_JUMPNOW;
                    patchjump(i + 1);
                    i += 3;
                }
                else
                {
                    bcode = m_currentfunccompiler->targetfunc->fnscriptfunc.blob.instrucs;
                    cvals = (NNValue*)m_currentfunccompiler->targetfunc->fnscriptfunc.blob.constants.listitems;
                    i += 1 + getcodeargscount(bcode, cvals, i);
                }
            }
        }


        void parseassign(uint16_t realop, uint16_t getop, uint16_t setop, int arg)
        {
            m_replcanecho = false;
            if(getop == NNInstruction::OPC_PROPERTYGET || getop == NNInstruction::OPC_PROPERTYGETSELF)
            {
                emitinstruc(NNInstruction::OPC_DUPONE);
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
            if(canassign && match(NNAstToken::T_ASSIGN))
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
            else if(canassign && match(NNAstToken::T_PLUSASSIGN))
            {
                parseassign(NNInstruction::OPC_PRIMADD, getop, setop, arg);
            }
            else if(canassign && match(NNAstToken::T_MINUSASSIGN))
            {
                parseassign(NNInstruction::OPC_PRIMSUBTRACT, getop, setop, arg);
            }
            else if(canassign && match(NNAstToken::T_MULTASSIGN))
            {
                parseassign(NNInstruction::OPC_PRIMMULTIPLY, getop, setop, arg);
            }
            else if(canassign && match(NNAstToken::T_DIVASSIGN))
            {
                parseassign(NNInstruction::OPC_PRIMDIVIDE, getop, setop, arg);
            }
            else if(canassign && match(NNAstToken::T_POWASSIGN))
            {
                parseassign(NNInstruction::OPC_PRIMPOW, getop, setop, arg);
            }
            else if(canassign && match(NNAstToken::T_PERCENT_EQ))
            {
                parseassign(NNInstruction::OPC_PRIMMODULO, getop, setop, arg);
            }
            else if(canassign && match(NNAstToken::T_AMP_EQ))
            {
                parseassign(NNInstruction::OPC_PRIMAND, getop, setop, arg);
            }
            else if(canassign && match(NNAstToken::T_BAR_EQ))
            {
                parseassign(NNInstruction::OPC_PRIMOR, getop, setop, arg);
            }
            else if(canassign && match(NNAstToken::T_TILDE_EQ))
            {
                parseassign(NNInstruction::OPC_PRIMBITNOT, getop, setop, arg);
            }
            else if(canassign && match(NNAstToken::T_XOR_EQ))
            {
                parseassign(NNInstruction::OPC_PRIMBITXOR, getop, setop, arg);
            }
            else if(canassign && match(NNAstToken::T_LEFTSHIFTASSIGN))
            {
                parseassign(NNInstruction::OPC_PRIMSHIFTLEFT, getop, setop, arg);
            }
            else if(canassign && match(NNAstToken::T_RIGHTSHIFTASSIGN))
            {
                parseassign(NNInstruction::OPC_PRIMSHIFTRIGHT, getop, setop, arg);
            }
            else if(canassign && match(NNAstToken::T_INCREMENT))
            {
                m_replcanecho = false;
                if(getop == NNInstruction::OPC_PROPERTYGET || getop == NNInstruction::OPC_PROPERTYGETSELF)
                {
                    emitinstruc(NNInstruction::OPC_DUPONE);
                }
                if(arg != -1)
                {
                    emitbyteandshort(getop, arg);
                }
                else
                {
                    emit2byte(getop, 1);
                }
                emit2byte(NNInstruction::OPC_PUSHONE, NNInstruction::OPC_PRIMADD);
                emitbyteandshort(setop, (uint16_t)arg);
            }
            else if(canassign && match(NNAstToken::T_DECREMENT))
            {
                m_replcanecho = false;
                if(getop == NNInstruction::OPC_PROPERTYGET || getop == NNInstruction::OPC_PROPERTYGETSELF)
                {
                    emitinstruc(NNInstruction::OPC_DUPONE);
                }

                if(arg != -1)
                {
                    emitbyteandshort(getop, arg);
                }
                else
                {
                    emit2byte(getop, 1);
                }

                emit2byte(NNInstruction::OPC_PUSHONE, NNInstruction::OPC_PRIMSUBTRACT);
                emitbyteandshort(setop, (uint16_t)arg);
            }
            else
            {
                if(arg != -1)
                {
                    if(getop == NNInstruction::OPC_INDEXGET || getop == NNInstruction::OPC_INDEXGETRANGED)
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


        void namedvar(NNAstToken name, bool canassign)
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
                    getop = NNInstruction::OPC_FUNCARGGET;
                    setop = NNInstruction::OPC_FUNCARGSET;
                }
                else
                {
                    getop = NNInstruction::OPC_LOCALGET;
                    setop = NNInstruction::OPC_LOCALSET;
                }
            }
            else
            {
                arg = m_currentfunccompiler->resolveUpvalue(&name);
                if((arg != -1) && (name.isglobal == false))
                {
                    getop = NNInstruction::OPC_UPVALUEGET;
                    setop = NNInstruction::OPC_UPVALUESET;
                }
                else
                {
                    arg = makeidentconst(&name);
                    getop = NNInstruction::OPC_GLOBALGET;
                    setop = NNInstruction::OPC_GLOBALSET;
                }
            }
            assignment(getop, setop, arg, canassign);
        }

        void createdvar(NNAstToken name)
        {
            int local;
            if(m_currentfunccompiler->targetfunc->name != nullptr)
            {
                local = addlocal(name) - 1;
                markinitialized();
                emitbyteandshort(NNInstruction::OPC_LOCALSET, (uint16_t)local);
            }
            else
            {
                emitbyteandshort(NNInstruction::OPC_GLOBALDEFINE, (uint16_t)makeidentconst(&name));
            }
        }

        NNValue compilenumber()
        {
            return utilConvertNumberString(m_prevtoken.type, m_prevtoken.start);
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
            quote = m_prevtoken.start[0];
            realstr = (char*)m_prevtoken.start + 1;
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
                                        int readunicodeescape(NNAstParser* this, char* string, char* realstring, int numberbytes, int realindex, int index)
                                        int readhexescape(NNAstParser* this, const char* str, int index, int count)
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



        bool doparseprecedence(Rule::Precedence precedence /*, NNAstExpression* dest*/)
        {
            bool canassign;
            Rule* rule;
            NNAstToken previous;
            NNAstParseInfixFN infixrule;
            NNAstParsePrefixFN prefixrule;
            rule = getRule(m_prevtoken.type);
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
                rule = getRule(m_currtoken.type);
                if(rule == nullptr)
                {
                    return false;
                }
                if(precedence <= rule->precedence)
                {
                    previous = m_prevtoken;
                    ignorewhitespace();
                    advance();
                    infixrule = getRule(m_prevtoken.type)->infix;
                    infixrule(this, previous, canassign);
                }
                else
                {
                    break;
                }
            }
            if(canassign && match(NNAstToken::T_ASSIGN))
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
            while(!check(NNAstToken::T_BRACECLOSE) && !check(NNAstToken::T_EOF))
            {
                parsedeclaration();
            }
            m_blockcount--;
            if(!consume(NNAstToken::T_BRACECLOSE, "expected '}' after block"))
            {
                return false;
            }
            if(match(NNAstToken::T_SEMICOLON))
            {
            }
            return true;
        }

        void declarefuncargvar()
        {
            int i;
            NNAstToken* name;
            LocalVarInfo* local;
            /* global variables are implicitly declared... */
            if(m_currentfunccompiler->scopedepth == 0)
            {
                return;
            }
            name = &m_prevtoken;
            for(i = m_currentfunccompiler->localcount - 1; i >= 0; i--)
            {
                local = &m_currentfunccompiler->locals[i];
                if(local->depth != -1 && local->depth < m_currentfunccompiler->scopedepth)
                {
                    break;
                }
                if(utilIdentsEqual(name, &local->name))
                {
                    raiseerror("%.*s already declared in current scope", name->length, name->start);
                }
            }
            addlocal(*name);
        }

        int parsefuncparamvar(const char* message)
        {
            if(!consume(NNAstToken::T_IDENTNORMAL, message))
            {
                /* what to do here? */
            }
            declarefuncargvar();
            /* we are in a local scope... */
            if(m_currentfunccompiler->scopedepth > 0)
            {
                return 0;
            }
            return makeidentconst(&m_prevtoken);
        }

        uint32_t parsefunccallargs()
        {
            uint16_t argcount;
            argcount = 0;
            if(!check(NNAstToken::T_PARENCLOSE))
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
                } while(match(NNAstToken::T_COMMA));
            }
            ignorewhitespace();
            if(!consume(NNAstToken::T_PARENCLOSE, "expected ')' after argument list"))
            {
                /* TODO: handle this, somehow. */
            }
            return argcount;
        }

        void parsefuncfull(NNObjFunction::ContextType type, bool isanon)
        {
            m_infunction = true;
            FuncCompiler fnc(this, type, isanon);
            scopebegin();
            /* compile parameter list */
            consume(NNAstToken::T_PARENOPEN, "expected '(' after function name");
            if(!check(NNAstToken::T_PARENCLOSE))
            {
                fnc.parseParamList();
            }
            consume(NNAstToken::T_PARENCLOSE, "expected ')' after function parameters");
            fnc.compileBody(false, isanon);
            m_infunction = false;
        }

        void parsemethod(NNAstToken classname, NNAstToken methodname, bool havenametoken, bool isstatic)
        {
            size_t sn;
            int constant;
            const char* sc;
            NNObjFunction::ContextType type;
            NNAstToken actualmthname;
            NNAstToken::Type tkns[] = { NNAstToken::T_IDENTNORMAL, NNAstToken::T_DECORATOR };
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
            type = NNObjFunction::CTXTYPE_METHOD;
            if(isstatic)
            {
                type = NNObjFunction::CTXTYPE_STATIC;
            }
            if((m_prevtoken.length == (int)sn) && (memcmp(m_prevtoken.start, sc, sn) == 0))
            {
                type = NNObjFunction::CTXTYPE_INITIALIZER;
            }
            else if((m_prevtoken.length > 0) && (m_prevtoken.start[0] == '_'))
            {
                type = NNObjFunction::CTXTYPE_PRIVATE;
            }
            parsefuncfull(type, false);
            emitbyteandshort(NNInstruction::OPC_MAKEMETHOD, constant);
        }


        bool parsefield(NNAstToken* nametokendest, bool* havenamedest, bool isstatic)
        {
            int fieldconstant;
            NNAstToken fieldname;
            *havenamedest = false;
            if(match(NNAstToken::T_IDENTNORMAL))
            {
                fieldname = m_prevtoken;
                *nametokendest = fieldname;
                if(check(NNAstToken::T_ASSIGN))
                {
                    consume(NNAstToken::T_ASSIGN, "expected '=' after ident");
                    fieldconstant = makeidentconst(&fieldname);
                    parseexpression();
                    emitbyteandshort(NNInstruction::OPC_CLASSPROPERTYDEFINE, fieldconstant);
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
            parsefuncfull(NNObjFunction::CTXTYPE_FUNCTION, false);
            definevariable(global);
        }

        void parseclassdeclaration(bool named)
        {
            bool isstatic;
            bool havenametoken;
            int nameconst;
            NNAstToken nametoken;
            NNAstCompContext oldctx;
            NNAstToken classname;
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
                consume(NNAstToken::T_IDENTNORMAL, "class name expected");
                classname = m_prevtoken;
                declarevariable();
            }
            else
            {
                classname = utilMakeSynthToken("<anonclass>");
            }
            nameconst = makeidentconst(&classname);
            emitbyteandshort(NNInstruction::OPC_MAKECLASS, nameconst);
            if(named)
            {
                definevariable(nameconst);
            }
            classcompiler.name = m_prevtoken;
            classcompiler.hassuperclass = false;
            classcompiler.enclosing = m_currentclasscompiler;
            m_currentclasscompiler = &classcompiler;
            oldctx = m_compcontext;
            m_compcontext = NEON_COMPCONTEXT_CLASS;
            if(match(NNAstToken::T_KWEXTENDS))
            {
                consume(NNAstToken::T_IDENTNORMAL, "name of superclass expected");
                astrulevarnormal(this, false);
                if(utilIdentsEqual(&classname, &m_prevtoken))
                {
                    raiseerror("class %.*s cannot inherit from itself", classname.length, classname.start);
                }
                scopebegin();
                addlocal(utilMakeSynthToken(g_strsuper));
                definevariable(0);
                namedvar(classname, false);
                emitinstruc(NNInstruction::OPC_CLASSINHERIT);
                classcompiler.hassuperclass = true;
            }
            if(named)
            {
                namedvar(classname, false);
            }
            ignorewhitespace();
            consume(NNAstToken::T_BRACEOPEN, "expected '{' before class body");
            ignorewhitespace();
            while(!check(NNAstToken::T_BRACECLOSE) && !check(NNAstToken::T_EOF))
            {
                isstatic = false;
                if(match(NNAstToken::T_KWSTATIC))
                {
                    isstatic = true;
                }
                if(match(NNAstToken::T_KWVAR))
                {
                    /*
                     * TODO:
                     * using 'var ... =' in a class is actually semantically superfluous,
                     * but not incorrect either. maybe warn that this syntax is deprecated?
                     */
                }
                if(!parsefield(&nametoken, &havenametoken, isstatic))
                {
                    parsemethod(classname, nametoken, havenametoken, isstatic);
                }
                ignorewhitespace();
            }
            consume(NNAstToken::T_BRACECLOSE, "expected '}' after class body");
            if(match(NNAstToken::T_SEMICOLON))
            {
            }
            if(named)
            {
                emitinstruc(NNInstruction::OPC_POPONE);
            }
            if(classcompiler.hassuperclass)
            {
                scopeend();
            }
            m_currentclasscompiler = m_currentclasscompiler->enclosing;
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
                if(match(NNAstToken::T_ASSIGN))
                {
                    parseexpression();
                }
                else
                {
                    emitinstruc(NNInstruction::OPC_PUSHNULL);
                }
                definevariable(global);
                totalparsed++;
            } while(match(NNAstToken::T_COMMA));
            if(!isinitializer)
            {
                consumestmtend();
            }
            else
            {
                consume(NNAstToken::T_SEMICOLON, "expected ';' after initializer");
                ignorewhitespace();
            }
        }

        void parseexprstmt(bool isinitializer, bool semi)
        {
            auto gcs = SharedState::get();
            if(gcs->m_isrepl && m_currentfunccompiler->scopedepth == 0)
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
                    emitinstruc(NNInstruction::OPC_ECHO);
                    m_replcanecho = false;
                }
                else
                {
        #if 0
                    if(!m_keeplastvalue)
        #endif
                    {
                        emitinstruc(NNInstruction::OPC_POPONE);
                    }
                }
                consumestmtend();
            }
            else
            {
                consume(NNAstToken::T_SEMICOLON, "expected ';' after initializer");
                ignorewhitespace();
                emitinstruc(NNInstruction::OPC_POPONE);
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
            consume(NNAstToken::T_PARENOPEN, "expected '(' after 'for'");
            /* parse initializer... */
            if(match(NNAstToken::T_SEMICOLON))
            {
                /* no initializer */
            }
            else if(match(NNAstToken::T_KWVAR))
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
            m_innermostloopscopedepth = m_currentfunccompiler->scopedepth;
            exitjump = -1;
            if(!match(NNAstToken::T_SEMICOLON))
            {
                /* the condition is optional */
                parseexpression();
                consume(NNAstToken::T_SEMICOLON, "expected ';' after condition");
                ignorewhitespace();
                /* jump out of the loop if the condition is false... */
                exitjump = emitjump(NNInstruction::OPC_JUMPIFFALSE);
                emitinstruc(NNInstruction::OPC_POPONE);
                /* pop the condition */
            }
            /* the iterator... */
            if(!check(NNAstToken::T_BRACEOPEN))
            {
                bodyjump = emitjump(NNInstruction::OPC_JUMPNOW);
                incrstart = currentblob()->count;
                parseexpression();
                ignorewhitespace();
                emitinstruc(NNInstruction::OPC_POPONE);
                emitloop(m_innermostloopstart);
                m_innermostloopstart = incrstart;
                patchjump(bodyjump);
            }
            consume(NNAstToken::T_PARENCLOSE, "expected ')' after 'for'");
            parsestmt();
            emitloop(m_innermostloopstart);
            if(exitjump != -1)
            {
                patchjump(exitjump);
                emitinstruc(NNInstruction::OPC_POPONE);
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
            NNAstToken iteratortoken;
            NNAstToken keytoken;
            NNAstToken valuetoken;
            scopebegin();
            /* define @iter and @itern constant */
            citer = pushconst(NNValue::fromObject(nn_string_intern("@iter")));
            citern = pushconst(NNValue::fromObject(nn_string_intern("@itern")));
            consume(NNAstToken::T_PARENOPEN, "expected '(' after 'foreach'");
            consume(NNAstToken::T_IDENTNORMAL, "expected variable name after 'foreach'");
            if(!check(NNAstToken::T_COMMA))
            {
                keytoken = utilMakeSynthToken(" _ ");
                valuetoken = m_prevtoken;
            }
            else
            {
                keytoken = m_prevtoken;
                consume(NNAstToken::T_COMMA, "");
                consume(NNAstToken::T_IDENTNORMAL, "expected variable name after ','");
                valuetoken = m_prevtoken;
            }
            consume(NNAstToken::T_KWIN, "expected 'in' after for loop variable(s)");
            ignorewhitespace();
            /*
            // The space in the variable name ensures it won't collide with a user-defined
            // variable.
            */
            iteratortoken = utilMakeSynthToken(" iterator ");
            /* Evaluate the sequence expression and store it in a hidden local variable. */
            parseexpression();
            consume(NNAstToken::T_PARENCLOSE, "expected ')' after 'foreach'");
            if(m_currentfunccompiler->localcount + 3 > NEON_CONFIG_ASTMAXLOCALS)
            {
                raiseerror("cannot declare more than %d variables in one scope", NEON_CONFIG_ASTMAXLOCALS);
                return;
            }
            /* add the iterator to the local scope */
            iteratorslot = addlocal(iteratortoken) - 1;
            definevariable(0);
            /* Create the key local variable. */
            emitinstruc(NNInstruction::OPC_PUSHNULL);
            keyslot = addlocal(keytoken) - 1;
            definevariable(keyslot);
            /* create the local value slot */
            emitinstruc(NNInstruction::OPC_PUSHNULL);
            valueslot = addlocal(valuetoken) - 1;
            definevariable(0);
            surroundingloopstart = m_innermostloopstart;
            surroundingscopedepth = m_innermostloopscopedepth;
            /*
            // we'll be jumping back to right before the
            // expression after the loop body
            */
            m_innermostloopstart = currentblob()->count;
            m_innermostloopscopedepth = m_currentfunccompiler->scopedepth;
            /* key = iterable.iter_n__(key) */
            emitbyteandshort(NNInstruction::OPC_LOCALGET, iteratorslot);
            emitbyteandshort(NNInstruction::OPC_LOCALGET, keyslot);
            emitbyteandshort(NNInstruction::OPC_CALLMETHOD, citern);
            emit1byte(1);
            emitbyteandshort(NNInstruction::OPC_LOCALSET, keyslot);
            falsejump = emitjump(NNInstruction::OPC_JUMPIFFALSE);
            emitinstruc(NNInstruction::OPC_POPONE);
            /* value = iterable.iter__(key) */
            emitbyteandshort(NNInstruction::OPC_LOCALGET, iteratorslot);
            emitbyteandshort(NNInstruction::OPC_LOCALGET, keyslot);
            emitbyteandshort(NNInstruction::OPC_CALLMETHOD, citer);
            emit1byte(1);
            /*
            // Bind the loop value in its own scope. This ensures we get a fresh
            // variable each iteration so that closures for it don't all see the same one.
            */
            scopebegin();
            /* update the value */
            emitbyteandshort(NNInstruction::OPC_LOCALSET, valueslot);
            emitinstruc(NNInstruction::OPC_POPONE);
            parsestmt();
            scopeend();
            emitloop(m_innermostloopstart);
            patchjump(falsejump);
            emitinstruc(NNInstruction::OPC_POPONE);
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
            NNValue jump;
            NNAstToken::Type casetype;
            NNObjSwitch* sw;
            NNObjString* string;
            auto gcs = SharedState::get();
            /* the expression */
            consume(NNAstToken::T_PARENOPEN, "expected '(' before 'switch'");

            parseexpression();
            consume(NNAstToken::T_PARENCLOSE, "expected ')' after 'switch'");
            ignorewhitespace();
            consume(NNAstToken::T_BRACEOPEN, "expected '{' after 'switch' expression");
            ignorewhitespace();
            /* 0: before all cases, 1: before default, 2: after default */
            swstate = 0;
            casecount = 0;
            sw = nn_object_makeswitch();
            gcs->stackPush(NNValue::fromObject(sw));
            switchcode = emitswitch();
            /* emitbyteandshort(NNInstruction::OPC_SWITCH, pushconst(NNValue::fromObject(sw))); */
            startoffset = currentblob()->count;
            m_inswitch = true;
            while(!match(NNAstToken::T_BRACECLOSE) && !check(NNAstToken::T_EOF))
            {
                if(match(NNAstToken::T_KWCASE) || match(NNAstToken::T_KWDEFAULT))
                {
                    casetype = m_prevtoken.type;
                    if(swstate == 2)
                    {
                        raiseerror("cannot have another case after a default case");
                    }
                    if(swstate == 1)
                    {
                        /* at the end of the previous case, jump over the others... */
                        caseends[casecount++] = emitjump(NNInstruction::OPC_JUMPNOW);
                    }
                    if(casetype == NNAstToken::T_KWCASE)
                    {
                        swstate = 1;
                        do
                        {
                            ignorewhitespace();
                            advance();
                            jump = NNValue::makeNumber((double)currentblob()->count - (double)startoffset);
                            if(m_prevtoken.type == NNAstToken::T_KWTRUE)
                            {
                                sw->table.set(NNValue::makeBool(true), jump);
                            }
                            else if(m_prevtoken.type == NNAstToken::T_KWFALSE)
                            {
                                sw->table.set(NNValue::makeBool(false), jump);
                            }
                            else if(m_prevtoken.type == NNAstToken::T_LITERALSTRING || m_prevtoken.type == NNAstToken::T_LITERALRAWSTRING)
                            {
                                str = compilestring(&length, true);
                                string = nn_string_takelen(str, length);
                                /* gc fix */
                                gcs->stackPush(NNValue::fromObject(string));
                                sw->table.set(NNValue::fromObject(string), jump);
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
                        } while(match(NNAstToken::T_COMMA));
                        consume(NNAstToken::T_COLON, "expected ':' after 'case' constants");
                    }
                    else
                    {
                        consume(NNAstToken::T_COLON, "expected ':' after 'default'");
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
                caseends[casecount++] = emitjump(NNInstruction::OPC_JUMPNOW);
            }
            /* patch all the case jumps to the end */
            for(i = 0; i < casecount; i++)
            {
                patchjump(caseends[i]);
            }
            sw->exitjump = currentblob()->count - startoffset;
            patchswitch(switchcode, pushconst(NNValue::fromObject(sw)));
            /* pop the switch */
            gcs->stackPop();
        }

        void parseifstmt()
        {
            int elsejump;
            int thenjump;
            parseexpression();
            thenjump = emitjump(NNInstruction::OPC_JUMPIFFALSE);
            emitinstruc(NNInstruction::OPC_POPONE);
            parsestmt();
            elsejump = emitjump(NNInstruction::OPC_JUMPNOW);
            patchjump(thenjump);
            emitinstruc(NNInstruction::OPC_POPONE);
            if(match(NNAstToken::T_KWELSE))
            {
                parsestmt();
            }
            patchjump(elsejump);
        }

        void parseechostmt()
        {
            parseexpression();
            emitinstruc(NNInstruction::OPC_ECHO);
            consumestmtend();
        }

        void parsethrowstmt()
        {
            parseexpression();
            emitinstruc(NNInstruction::OPC_EXTHROW);
            discardlocals(m_currentfunccompiler->scopedepth - 1);
            consumestmtend();
        }

        void parseassertstmt()
        {
            consume(NNAstToken::T_PARENOPEN, "expected '(' after 'assert'");
            parseexpression();
            if(match(NNAstToken::T_COMMA))
            {
                ignorewhitespace();
                parseexpression();
            }
            else
            {
                emitinstruc(NNInstruction::OPC_PUSHNULL);
            }
            emitinstruc(NNInstruction::OPC_ASSERT);
            consume(NNAstToken::T_PARENCLOSE, "expected ')' after 'assert'");
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
            if(m_currentfunccompiler->handlercount == NEON_CONFIG_MAXEXCEPTHANDLERS)
            {
                raiseerror("maximum exception handler in scope exceeded");
            }
        #endif
            m_currentfunccompiler->handlercount++;
            m_istrying = true;
            ignorewhitespace();
            trybegins = emittry();
            /* compile the try body */
            parsestmt();
            emitinstruc(NNInstruction::OPC_EXPOPTRY);
            exitjump = emitjump(NNInstruction::OPC_JUMPNOW);
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
            if(match(NNAstToken::T_KWCATCH))
            {
                catchexists = true;
                scopebegin();
                consume(NNAstToken::T_PARENOPEN, "expected '(' after 'catch'");
                /*
                consume(NNAstToken::T_IDENTNORMAL, "missing exception class name");
                */
                type = makeidentconst(&m_prevtoken);
                address = currentblob()->count;
                if(match(NNAstToken::T_IDENTNORMAL))
                {
                    createdvar(m_prevtoken);
                }
                else
                {
                    emitinstruc(NNInstruction::OPC_POPONE);
                }
                consume(NNAstToken::T_PARENCLOSE, "expected ')' after 'catch'");
                emitinstruc(NNInstruction::OPC_EXPOPTRY);
                ignorewhitespace();
                parsestmt();
                scopeend();
            }
            else
            {
                type = pushconst(NNValue::fromObject(nn_string_intern("Exception")));
            }
            patchjump(exitjump);
            if(match(NNAstToken::T_KWFINALLY))
            {
                finalexists = true;
                /*
                // if we arrived here from either the try or handler block,
                // we don't want to continue propagating the exception
                */
                emitinstruc(NNInstruction::OPC_PUSHFALSE);
                finally = currentblob()->count;
                ignorewhitespace();
                parsestmt();
                continueexecutionaddress = emitjump(NNInstruction::OPC_JUMPIFFALSE);
                /* pop the bool off the stack */
                emitinstruc(NNInstruction::OPC_POPONE);
                emitinstruc(NNInstruction::OPC_EXPUBLISHTRY);
                patchjump(continueexecutionaddress);
                emitinstruc(NNInstruction::OPC_POPONE);
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
            if(m_currentfunccompiler->type == NNObjFunction::CTXTYPE_SCRIPT)
            {
                raiseerror("cannot return from top-level code");
            }
            */
            if(match(NNAstToken::T_SEMICOLON) || match(NNAstToken::T_NEWLINE))
            {
                emitreturn();
            }
            else
            {
                if(m_currentfunccompiler->contexttype == NNObjFunction::CTXTYPE_INITIALIZER)
                {
                    raiseerror("cannot return value from constructor");
                }
                if(m_istrying)
                {
                    emitinstruc(NNInstruction::OPC_EXPOPTRY);
                }
                parseexpression();
                emitinstruc(NNInstruction::OPC_RETURN);
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
            m_innermostloopscopedepth = m_currentfunccompiler->scopedepth;
            parseexpression();
            exitjump = emitjump(NNInstruction::OPC_JUMPIFFALSE);
            emitinstruc(NNInstruction::OPC_POPONE);
            parsestmt();
            emitloop(m_innermostloopstart);
            patchjump(exitjump);
            emitinstruc(NNInstruction::OPC_POPONE);
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
            m_innermostloopscopedepth = m_currentfunccompiler->scopedepth;
            parsestmt();
            consume(NNAstToken::T_KWWHILE, "expecting 'while' statement");
            parseexpression();
            exitjump = emitjump(NNInstruction::OPC_JUMPIFFALSE);
            emitinstruc(NNInstruction::OPC_POPONE);
            emitloop(m_innermostloopstart);
            patchjump(exitjump);
            emitinstruc(NNInstruction::OPC_POPONE);
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
                for(i = m_currentfunccompiler->localcount - 1; i >= 0 && m_currentfunccompiler->locals[i].depth >= m_currentfunccompiler->scopedepth; i--)
                {
                    if (m_currentfunccompiler->locals[i].iscaptured)
                    {
                        emitinstruc(NNInstruction::OPC_UPVALUECLOSE);
                    }
                    else
                    {
                        emitinstruc(NNInstruction::OPC_POPONE);
                    }
                }
        #endif
                discardlocals(m_innermostloopscopedepth + 1);
                emitjump(NNInstruction::OPC_BREAK_PL);
            }
            consumestmtend();
        }

        void synchronize()
        {
            m_panicmode = false;
            while(m_currtoken.type != NNAstToken::T_EOF)
            {
                if(m_currtoken.type == NNAstToken::T_NEWLINE || m_currtoken.type == NNAstToken::T_SEMICOLON)
                {
                    return;
                }
                switch(m_currtoken.type)
                {
                    case NNAstToken::T_KWCLASS:
                    case NNAstToken::T_KWFUNCTION:
                    case NNAstToken::T_KWVAR:
                    case NNAstToken::T_KWFOREACH:
                    case NNAstToken::T_KWIF:
                    case NNAstToken::T_KWEXTENDS:
                    case NNAstToken::T_KWSWITCH:
                    case NNAstToken::T_KWCASE:
                    case NNAstToken::T_KWFOR:
                    case NNAstToken::T_KWDO:
                    case NNAstToken::T_KWWHILE:
                    case NNAstToken::T_KWECHO:
                    case NNAstToken::T_KWASSERT:
                    case NNAstToken::T_KWTRY:
                    case NNAstToken::T_KWCATCH:
                    case NNAstToken::T_KWTHROW:
                    case NNAstToken::T_KWRETURN:
                    case NNAstToken::T_KWSTATIC:
                    case NNAstToken::T_KWTHIS:
                    case NNAstToken::T_KWSUPER:
                    case NNAstToken::T_KWFINALLY:
                    case NNAstToken::T_KWIN:
                    case NNAstToken::T_KWIMPORT:
                    case NNAstToken::T_KWAS:
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
            const char* name;
            bool isstatic;
            NNNativeFN function;
        };

        struct Field
        {
            const char* name;
            bool isstatic;
            NNNativeFN fieldvalfn;
        };

        struct Class
        {
            const char* name;
            Field* defpubfields;
            Function* defpubfunctions;
        };

    public:
        /*
         * the name of this module.
         * note: if the name must be preserved, copy it; it is only a pointer to a
         * string that gets freed past loading.
         */
        const char* name;

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
        NNModLoaderFN fnpreloaderfunc;

        /* function that is called before unloading the module. can be nullptr. */
        NNModLoaderFN fnunloaderfunc;
};


class NNFuncContext
{
    public:
        NNValue thisval;
        NNValue* argv;
        size_t argc;
};


class NNArgCheck
{
    public:
        const char* name;
        NNFuncContext m_scriptfnctx;
};


class NNUClassComplex
{
    public:
        NNObjInstance selfinstance;
        double re;
        double im;
};

#if defined(NEON_CONFIG_DEBUGMEMORY) && (NEON_CONFIG_DEBUGMEMORY == 1)
    #define nn_memory_malloc(...) nn_memory_debugmalloc(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)
    #define nn_memory_calloc(...) nn_memory_debugcalloc(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)
    #define nn_memory_realloc(...) nn_memory_debugrealloc(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)
#endif

/* bottom */

NNObjFunction* nn_object_makefuncgeneric(NNObjString* name, NNObject::Type fntype, NNValue thisval)
{
    NNObjFunction* ofn;
    ofn = (NNObjFunction*)SharedState::gcMakeObject(sizeof(NNObjFunction), fntype, false);
    ofn->clsthisval = thisval;
    ofn->name = name;
    return ofn;
}

NNObjFunction* nn_object_makefuncbound(NNValue receiver, NNObjFunction* method)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncgeneric(method->name, NNObject::OTYP_FUNCBOUND, NNValue::makeNull());
    ofn->fnmethod.receiver = receiver;
    ofn->fnmethod.method = method;
    return ofn;
}

NNObjFunction* nn_object_makefuncscript(NNObjModule* module, int type)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncgeneric(nn_string_intern("<script>"), NNObject::OTYP_FUNCSCRIPT, NNValue::makeNull());
    ofn->fnscriptfunc.arity = 0;
    ofn->upvalcount = 0;
    ofn->fnscriptfunc.isvariadic = false;
    ofn->name = nullptr;
    ofn->contexttype = (NNObjFunction::ContextType)type;
    ofn->fnscriptfunc.module = module;
    NNBlob::init(&ofn->fnscriptfunc.blob);
    return ofn;
}

void NNValue::markDict(NNObjDict* dict)
{
    NNValue::markValArray(&dict->htnames);
    nn_valtable_mark(&dict->htab);
}

void NNValue::markValArray(NNValArray<NNValue>* list)
{
    size_t i;
    NN_NULLPTRCHECK_RETURN(list);
    for(i = 0; i < list->listcount; i++)
    {
        nn_gcmem_markvalue(list->listitems[i]);
    }
}

bool NNValue::isFalse() const
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
        return nn_string_getlength(asString()) < 1;
    }
    /* Non-empty lists are true, empty lists are false.*/
    if(isArray())
    {
        return asArray()->varray.count() == 0;
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


void NNObject::markObject(NNObject* object)
{
    if(object == nullptr)
    {
        return;
    }
    auto gcs = SharedState::get();
    if(object->mark == gcs->markvalue)
    {
        return;
    }
#if defined(DEBUG_GC) && DEBUG_GC
    gcs->m_debugwriter->format("GC: marking object at <%p> ", (void*)object);
    nn_iostream_printvalue(gcs->m_debugwriter, NNValue::fromObject(object), false);
    gcs->m_debugwriter->format("\n");
#endif
    object->mark = gcs->markvalue;
    if(gcs->m_gcstate.graycapacity < gcs->m_gcstate.graycount + 1)
    {
        gcs->m_gcstate.graycapacity = NEON_MEMORY_GROWCAPACITY(gcs->m_gcstate.graycapacity);
        gcs->m_gcstate.graystack = (NNObject**)nn_memory_realloc(gcs->m_gcstate.graystack, sizeof(NNObject*) * gcs->m_gcstate.graycapacity);
        if(gcs->m_gcstate.graystack == nullptr)
        {
            fflush(stdout);
            fprintf(stderr, "GC encountered an error");
            abort();
        }
    }
    gcs->m_gcstate.graystack[gcs->m_gcstate.graycount++] = object;
}

void NNObject::blackenObject(NNObject* object)
{
#if defined(DEBUG_GC) && DEBUG_GC
    auto gcs = SharedState::get();
    gcs->m_debugwriter->format("GC: blacken object at <%p> ", (void*)object);
    nn_iostream_printvalue(gcs->m_debugwriter, NNValue::fromObject(object), false);
    gcs->m_debugwriter->format("\n");
#endif
    switch(object->type)
    {
        case NNObject::OTYP_MODULE:
        {
            NNObjModule* module;
            module = (NNObjModule*)object;
            nn_valtable_mark(&module->deftable);
        }
        break;
        case NNObject::OTYP_SWITCH:
        {
            NNObjSwitch* sw;
            sw = (NNObjSwitch*)object;
            nn_valtable_mark(&sw->table);
        }
        break;
        case NNObject::OTYP_FILE:
        {
            NNObjFile* file;
            file = (NNObjFile*)object;
            nn_file_mark(file);
        }
        break;
        case NNObject::OTYP_DICT:
        {
            NNObjDict* dict;
            dict = (NNObjDict*)object;
            NNValue::markDict(dict);
        }
        break;
        case NNObject::OTYP_ARRAY:
        {
            NNObjArray* list;
            list = (NNObjArray*)object;
            NNValue::markValArray(&list->varray);
        }
        break;
        case NNObject::OTYP_FUNCBOUND:
        {
            NNObjFunction* bound;
            bound = (NNObjFunction*)object;
            nn_gcmem_markvalue(bound->fnmethod.receiver);
            NNObject::markObject((NNObject*)bound->fnmethod.method);
        }
        break;
        case NNObject::OTYP_CLASS:
        {
            NNObjClass* klass;
            klass = (NNObjClass*)object;
            NNObject::markObject((NNObject*)klass->name);
            nn_valtable_mark(&klass->instmethods);
            nn_valtable_mark(&klass->staticmethods);
            nn_valtable_mark(&klass->staticproperties);
            nn_gcmem_markvalue(klass->constructor);
            nn_gcmem_markvalue(klass->destructor);
            if(klass->superclass != nullptr)
            {
                NNObject::markObject((NNObject*)klass->superclass);
            }
        }
        break;
        case NNObject::OTYP_FUNCCLOSURE:
        {
            int i;
            NNObjFunction* closure;
            closure = (NNObjFunction*)object;
            NNObject::markObject((NNObject*)closure->fnclosure.scriptfunc);
            for(i = 0; i < closure->upvalcount; i++)
            {
                NNObject::markObject((NNObject*)closure->fnclosure.upvalues[i]);
            }
        }
        break;
        case NNObject::OTYP_FUNCSCRIPT:
        {
            NNObjFunction* function;
            function = (NNObjFunction*)object;
            NNObject::markObject((NNObject*)function->name);
            NNObject::markObject((NNObject*)function->fnscriptfunc.module);
            NNValue::markValArray(&function->fnscriptfunc.blob.constants);
        }
        break;
        case NNObject::OTYP_INSTANCE:
        {
            NNObjInstance* instance;
            instance = (NNObjInstance*)object;
            nn_instance_mark(instance);
        }
        break;
        case NNObject::OTYP_UPVALUE:
        {
            auto upv = (NNObjUpvalue*)object;
            nn_gcmem_markvalue(upv->closed);
        }
        break;
        case NNObject::OTYP_RANGE:
        case NNObject::OTYP_FUNCNATIVE:
        case NNObject::OTYP_USERDATA:
        case NNObject::OTYP_STRING:
            break;
    }
}

void NNObject::destroyObject(NNObject* object)
{
    if(object->stale)
    {
        return;
    }
    switch(object->type)
    {
        case NNObject::OTYP_MODULE:
        {
            NNObjModule* module;
            module = (NNObjModule*)object;
            nn_module_destroy(module);
            SharedState::gcRelease(module);
        }
        break;
        case NNObject::OTYP_FILE:
        {
            NNObjFile* file;
            file = (NNObjFile*)object;
            nn_file_destroy(file);
        }
        break;
        case NNObject::OTYP_DICT:
        {
            NNObjDict* dict;
            dict = (NNObjDict*)object;
            nn_dict_destroy(dict);
            SharedState::gcRelease(dict);
        }
        break;
        case NNObject::OTYP_ARRAY:
        {
            NNObjArray* list;
            list = (NNObjArray*)object;
            list->varray.deInit(false);
            SharedState::gcRelease(list);
        }
        break;
        case NNObject::OTYP_FUNCBOUND:
        {
            /*
            // a closure may be bound to multiple instances
            // for this reason, we do not free closures when freeing bound methods
            */
            auto fn = (NNObjFunction*)object;
            SharedState::gcRelease(fn);
        }
        break;
        case NNObject::OTYP_CLASS:
        {
            NNObjClass* klass;
            klass = (NNObjClass*)object;
            nn_class_destroy(klass);
        }
        break;
        case NNObject::OTYP_FUNCCLOSURE:
        {
            NNObjFunction* closure;
            closure = (NNObjFunction*)object;
            nn_gcmem_freearray(sizeof(NNObjUpvalue*), closure->fnclosure.upvalues, closure->upvalcount);
            /*
            // there may be multiple closures that all reference the same function
            // for this reason, we do not free functions when freeing closures
            */
            SharedState::gcRelease(closure);
        }
        break;
        case NNObject::OTYP_FUNCSCRIPT:
        {
            NNObjFunction* function;
            function = (NNObjFunction*)object;
            nn_funcscript_destroy(function);
            SharedState::gcRelease(function);
        }
        break;
        case NNObject::OTYP_INSTANCE:
        {
            NNObjInstance* instance;
            instance = (NNObjInstance*)object;
            nn_instance_destroy(instance);
        }
        break;
        case NNObject::OTYP_FUNCNATIVE:
        {
            auto fn = (NNObjFunction*)object;
            SharedState::gcRelease(fn);
        }
        break;
        case NNObject::OTYP_UPVALUE:
        {
            auto upv = (NNObjUpvalue*)object;
            SharedState::gcRelease(upv);
        }
        break;
        case NNObject::OTYP_RANGE:
        {
            auto rn = (NNObjRange*)object;
            SharedState::gcRelease(rn);
        }
        break;
        case NNObject::OTYP_STRING:
        {
            NNObjString* string;
            string = (NNObjString*)object;
            nn_string_destroy(string);
        }
        break;
        case NNObject::OTYP_SWITCH:
        {
            NNObjSwitch* sw;
            sw = (NNObjSwitch*)object;
            sw->table.deinit();
            SharedState::gcRelease(sw);
        }
        break;
        case NNObject::OTYP_USERDATA:
        {
            NNObjUserdata* ptr;
            ptr = (NNObjUserdata*)object;
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
    NNValue* slot;
    NNObjUpvalue* upvalue;
    NNCallFrame::ExceptionInfo* handler;
    (void)handler;
    auto gcs = SharedState::get();
    for(slot = gcs->m_vmstate.stackvalues; slot < &gcs->m_vmstate.stackvalues[gcs->m_vmstate.stackidx]; slot++)
    {
        nn_gcmem_markvalue(*slot);
    }
    for(i = 0; i < (int)gcs->m_vmstate.framecount; i++)
    {
        NNObject::markObject((NNObject*)gcs->m_vmstate.framevalues[i].closure);
        for(j = 0; j < (int)gcs->m_vmstate.framevalues[i].handlercount; j++)
        {
            handler = &gcs->m_vmstate.framevalues[i].handlers[j];
            /*NNObject::markObject((NNObject*)handler->klass);*/
        }
    }
    for(upvalue = gcs->m_vmstate.openupvalues; upvalue != nullptr; upvalue = upvalue->next)
    {
        NNObject::markObject((NNObject*)upvalue);
    }
    nn_valtable_mark(&gcs->m_declaredglobals);
    nn_valtable_mark(&gcs->m_openedmodules);
    // NNObject::markObject((NNObject*)gcs->m_exceptions.stdexception);
    gcMarkCompilerRoots();
}


template <typename HTKeyT, typename HTValT> NNProperty* NNHashTable<HTKeyT, HTValT>::getfieldbyostr(NNObjString* str)
{
    return getfieldbystr(NNValue::makeNull(), nn_string_getdata(str), nn_string_getlength(str), str->hashvalue);
}

template <typename HTKeyT, typename HTValT> NNProperty* NNHashTable<HTKeyT, HTValT>::getfieldbycstr(const char* kstr)
{
    size_t klen;
    uint32_t hsv;
    klen = strlen(kstr);
    hsv = nn_util_hashstring(kstr, klen);
    return getfieldbystr(NNValue::makeNull(), kstr, klen, hsv);
}

template <typename HTKeyT, typename HTValT> NNProperty* NNHashTable<HTKeyT, HTValT>::getfield(HTKeyT key)
{
    NNObjString* oskey;
    if(key.isString())
    {
        oskey = key.asString();
        return getfieldbystr(key, nn_string_getdata(oskey), nn_string_getlength(oskey), oskey->hashvalue);
    }
    return getfieldbyvalue(key);
}

template <typename HTKeyT, typename HTValT> bool NNHashTable<HTKeyT, HTValT>::remove(HTKeyT key)
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
    entry->value = NNProperty::make(NNValue::makeBool(true), NNProperty::FTYP_VALUE);
    return true;
}



template<typename... ArgsT>
static void nn_vmdebug_printvalue(NNValue val, const char* fmt, ArgsT&&... args)
{
    NNIOStream* pr;
    auto gcs = SharedState::get();
    pr = gcs->m_stderrprinter;
    pr->format("VMDEBUG: val=<<<");
    nn_iostream_printvalue(pr, val, true, false);
    pr->format(">>> ");
    pr->format(fmt, args...);
    pr->format("\n");
}

template<typename... ArgsT>
bool nn_except_throwactual(NNObjClass* exklass, const char* srcfile, int srcline, const char* format, ArgsT&&... args)
{
    enum
    {
        kMaxBufSize = 1024
    };
    constexpr static auto tmpsnprintf = snprintf;
    int length;
    char* message;
    NNValue stacktrace;
    NNObjInstance* instance;
    auto gcs = SharedState::get();
    message = (char*)nn_memory_malloc(kMaxBufSize + 1);
    length = tmpsnprintf(message, kMaxBufSize, format, args...);
    instance = nn_except_makeinstance(exklass, srcfile, srcline, nn_string_takelen(message, length));
    gcs->stackPush(NNValue::fromObject(instance));
    stacktrace = nn_except_getstacktrace();
    gcs->stackPush(stacktrace);
    nn_instance_defproperty(instance, nn_string_intern("stacktrace"), stacktrace);
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
    NNCallFrame* frame;
    NNObjFunction* function;
    /* flush out anything on stdout first */
    fflush(stdout);
    auto gcs = SharedState::get();
    frame = &gcs->m_vmstate.framevalues[gcs->m_vmstate.framecount - 1];
    function = frame->closure->fnclosure.scriptfunc;
    instruction = frame->inscode - function->fnscriptfunc.blob.instrucs - 1;
    line = function->fnscriptfunc.blob.instrucs[instruction].fromsourceline;
    fprintf(stderr, "RuntimeError: ");
    tmpfprintf(stderr, format, args...);
    fprintf(stderr, " -> %s:%d ", nn_string_getdata(function->fnscriptfunc.module->physicalpath), line);
    fputs("\n", stderr);
    if(gcs->m_vmstate.framecount > 1)
    {
        fprintf(stderr, "stacktrace:\n");
        for(i = gcs->m_vmstate.framecount - 1; i >= 0; i--)
        {
            frame = &gcs->m_vmstate.framevalues[i];
            function = frame->closure->fnclosure.scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->fnscriptfunc.blob.instrucs - 1;
            fprintf(stderr, "    %s:%d -> ", nn_string_getdata(function->fnscriptfunc.module->physicalpath), function->fnscriptfunc.blob.instrucs[instruction].fromsourceline);
            if(function->name == nullptr)
            {
                fprintf(stderr, "<script>");
            }
            else
            {
                fprintf(stderr, "%s()", nn_string_getdata(function->name));
            }
            fprintf(stderr, "\n");
        }
    }
    nn_state_resetvmstate();
}

NEON_INLINE const char* nn_value_typefromfunction(NNValIsFuncFN func)
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



NNObject* nn_gcmem_protect(NNObject* object)
{
    size_t frpos;
    auto gcs = SharedState::get();
    gcs->stackPush(NNValue::fromObject(object));
    frpos = 0;
    if(gcs->m_vmstate.framecount > 0)
    {
        frpos = gcs->m_vmstate.framecount - 1;
    }
    gcs->m_vmstate.framevalues[frpos].gcprotcount++;
    return object;
}


void nn_gcmem_markvalue(NNValue value)
{
    if(value.isObject())
    {
        NNObject::markObject(value.asObject());
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

const char* nn_util_color(NNColor tc)
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

NNObjString* nn_util_numbertobinstring(long n)
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
    return nn_string_copylen(newstr, length);
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
    //  return nn_string_copylen(str, length);
    */
}

NNObjString* nn_util_numbertooctstring(int64_t n, bool numeric)
{
    int length;
    /* assume maximum of 64 bits + 2 octal indicators (0c) */
    char str[66];
    length = sprintf(str, numeric ? "0c%lo" : "%lo", n);
    return nn_string_copylen(str, length);
}

NNObjString* nn_util_numbertohexstring(int64_t n, bool numeric)
{
    int length;
    /* assume maximum of 64 bits + 2 hex indicators (0x) */
    char str[66];
    length = sprintf(str, numeric ? "0x%lx" : "%lx", n);
    return nn_string_copylen(str, length);
}

uint32_t nn_object_hashobject(NNObject* object)
{
    switch(object->type)
    {
        case NNObject::OTYP_CLASS:
        {
            /* Classes just use their name. */
            return ((NNObjClass*)object)->name->hashvalue;
        }
        break;
        case NNObject::OTYP_FUNCSCRIPT:
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
            NNObjFunction* fn;
            fn = (NNObjFunction*)object;
            tmpptr = (uint32_t)(uintptr_t)fn;
#if 1
            tmpa = nn_util_hashdouble(fn->fnscriptfunc.arity);
            tmpb = nn_util_hashdouble(fn->fnscriptfunc.blob.count);
            tmpres = tmpa ^ tmpb;
            tmpres = tmpres ^ tmpptr;
#else
            tmpres = tmpptr;
#endif
            return tmpres;
        }
        break;
        case NNObject::OTYP_STRING:
        {
            return ((NNObjString*)object)->hashvalue;
        }
        break;
        default:
            break;
    }
    return 0;
}

uint32_t nn_value_hashvalue(NNValue value)
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

void nn_strformat_init(NNFormatInfo* nfi, NNIOStream* writer, const char* fmtstr, size_t fmtlen)
{
    nfi->fmtstr = fmtstr;
    nfi->fmtlen = fmtlen;
    nfi->writer = writer;
}

void nn_strformat_destroy(NNFormatInfo* nfi)
{
    (void)nfi;
}

bool nn_strformat_format(NNFormatInfo* nfi, size_t argc, size_t argbegin, NNValue* argv)
{
    int ch;
    int ival;
    int nextch;
    bool ok;
    size_t i;
    size_t argpos;
    NNValue cval;
    i = 0;
    auto gcs = SharedState::get();
    argpos = argbegin;
    ok = true;
    while(i < nfi->fmtlen)
    {
        ch = nfi->fmtstr[i];
        nextch = -1;
        if((i + 1) < nfi->fmtlen)
        {
            nextch = nfi->fmtstr[i + 1];
        }
        i++;
        if(ch == '%')
        {
            if(nextch == '%')
            {
                nfi->writer->writeChar('%');
            }
            else
            {
                i++;
                if(argpos > argc)
                {
                    nn_except_throwclass(gcs->m_exceptions.argumenterror, "too few arguments");
                    ok = false;
                    cval = NNValue::makeNull();
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
                        nn_iostream_printvalue(nfi->writer, cval, true, true);
                    }
                    break;
                    case 'c':
                    {
                        ival = (int)cval.asNumber();
                        nfi->writer->format("%c", ival);
                    }
                    break;
                    /* TODO: implement actual field formatting */
                    case 's':
                    case 'd':
                    case 'i':
                    case 'g':
                    {
                        nn_iostream_printvalue(nfi->writer, cval, false, true);
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
            nfi->writer->writeChar(ch);
        }
    }
    return ok;
}

void nn_valtable_removewhites(NNHashTable<NNValue, NNValue>* table)
{
    int i;
    auto gcs = SharedState::get();
    for(i = 0; i < table->htcapacity; i++)
    {
        auto entry = &table->htentries[i];
        if(entry->key.isObject() && entry->key.asObject()->mark != gcs->markvalue)
        {
            table->remove(entry->key);
        }
    }
}

void nn_valtable_mark(NNHashTable<NNValue, NNValue>* table)
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

static NNValue nn_string_tabhashvaluestr(const char* data, size_t len, uint32_t hsv)
{
    return NNValue::makeNumber(nn_string_tabhashvaluecombine(data, len, hsv));
}

static NNValue nn_string_tabhashvalueobj(NNObjString* os)
{
    return nn_string_tabhashvaluestr(nn_string_getdata(os), nn_string_getlength(os), os->hashvalue);
}

void nn_string_strtabstore(NNObjString* os)
{
    auto gcs = SharedState::get();
    gcs->m_allocatedstrings.store(os);

}

NNObjString* nn_string_strtabfind(const char* str, size_t len, uint32_t hsv)
{
    NNObjString* rs;
    auto gcs = SharedState::get();
    rs = gcs->m_allocatedstrings.findstring(str, len, hsv);
    return rs;
}

NNObjString* nn_string_makefromstrbuf(NNStringBuffer buf, uint32_t hsv, size_t length)
{
    NNObjString* rs;
    rs = (NNObjString*)SharedState::gcMakeObject(sizeof(NNObjString), NNObject::OTYP_STRING, false);
    rs->sbuf = buf;
    rs->hashvalue = hsv;
    if(length > 0)
    {
        nn_string_strtabstore(rs);
    }
    return rs;
}

void nn_string_destroy(NNObjString* str)
{
    nn_strbuf_destroyfromstack(&str->sbuf);
    SharedState::gcRelease(str);
}

NNObjString* nn_string_internlen(const char* strdata, int length)
{
    uint32_t hsv;
    NNStringBuffer buf;
    NNObjString* rs;
    hsv = nn_util_hashstring(strdata, length);
    rs = nn_string_strtabfind(strdata, length, hsv);
    if(rs != nullptr)
    {
        return rs;
    }
    nn_strbuf_makebasicemptystack(&buf, nullptr, 0);
    buf.isintern = true;
    nn_strbuf_setdata(&buf, (char*)strdata);
    nn_strbuf_setlength(&buf, length);
    return nn_string_makefromstrbuf(buf, hsv, length);
}

NNObjString* nn_string_intern(const char* strdata)
{
    return nn_string_internlen(strdata, strlen(strdata));
}

NNObjString* nn_string_takelen(char* strdata, int length)
{
    uint32_t hsv;
    NNObjString* rs;
    NNStringBuffer buf;
    hsv = nn_util_hashstring(strdata, length);
    rs = nn_string_strtabfind(strdata, length, hsv);
    if(rs != nullptr)
    {
        nn_memory_free(strdata);
        return rs;
    }
    nn_strbuf_makebasicemptystack(&buf, nullptr, 0);
    nn_strbuf_setdata(&buf, strdata);
    nn_strbuf_setlength(&buf, length);
    return nn_string_makefromstrbuf(buf, hsv, length);
}

NNObjString* nn_string_takecstr(char* strdata)
{
    return nn_string_takelen(strdata, strlen(strdata));
}

NNObjString* nn_string_copylen(const char* strdata, int length)
{
    uint32_t hsv;
    NNStringBuffer sb;
    NNObjString* rs;
    hsv = nn_util_hashstring(strdata, length);
    if(length == 0)
    {
        return nn_string_internlen(strdata, length);
    }
    {
        rs = nn_string_strtabfind(strdata, length, hsv);
        if(rs != nullptr)
        {
            return rs;
        }
    }
    nn_strbuf_makebasicemptystack(&sb, strdata, length);
    rs = nn_string_makefromstrbuf(sb, hsv, length);
    return rs;
}

NNObjString* nn_string_copycstr(const char* strdata)
{
    return nn_string_copylen(strdata, strlen(strdata));
}

NNObjString* nn_string_copyobject(NNObjString* origos)
{
    return nn_string_copylen(nn_string_getdata(origos), nn_string_getlength(origos));
}

const char* nn_string_getdata(NNObjString* os)
{
    return nn_strbuf_data(&os->sbuf);
}

char* nn_string_mutdata(NNObjString* os)
{
    return nn_strbuf_mutdata(&os->sbuf);
}

size_t nn_string_getlength(NNObjString* os)
{
    return nn_strbuf_length(&os->sbuf);
}

bool nn_string_setlength(NNObjString* os, size_t nlen)
{
    return nn_strbuf_setlength(&os->sbuf, nlen);
}

bool nn_string_set(NNObjString* os, size_t idx, int byte)
{
    return nn_strbuf_set(&os->sbuf, idx, byte);
}

int nn_string_get(NNObjString* os, size_t idx)
{
    return nn_strbuf_get(&os->sbuf, idx);
}

bool nn_string_appendstringlen(NNObjString* os, const char* str, size_t len)
{
    return nn_strbuf_appendstrn(&os->sbuf, str, len);
}

bool nn_string_appendstring(NNObjString* os, const char* str)
{
    return nn_string_appendstringlen(os, str, strlen(str));
}

bool nn_string_appendobject(NNObjString* os, NNObjString* other)
{
    return nn_string_appendstringlen(os, nn_string_getdata(other), nn_string_getlength(other));
}

bool nn_string_appendbyte(NNObjString* os, int ch)
{
    return nn_strbuf_appendchar(&os->sbuf, ch);
}

bool nn_string_appendnumulong(NNObjString* os, unsigned long val)
{
    return nn_strbuf_appendnumulong(&os->sbuf, val);
}

bool nn_string_appendnumint(NNObjString* os, int val)
{
    return nn_strbuf_appendnumint(&os->sbuf, val);
}

template<typename... ArgsT>
int nn_string_appendfmt(NNObjString* os, const char* fmt, ArgsT&&... args)
{
    return nn_strbuf_appendformat(&os->sbuf, fmt, args...);
}

NNObjString* nn_string_substrlen(NNObjString* os, size_t start, size_t maxlen)
{
    char* str;
    NNObjString* rt;
    str = nn_strbuf_substr(&os->sbuf, start, maxlen);
    rt = nn_string_takelen(str, maxlen);
    return rt;
}

NNObjString* nn_string_substr(NNObjString* os, size_t start)
{
    return nn_string_substrlen(os, start, nn_string_getlength(os));
}



void nn_iostream_printvaltable(NNIOStream* pr, NNHashTable<NNValue, NNValue>* table, const char* name)
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
            nn_iostream_printvalue(pr, entry->key, true, true);
            pr->format(": ");
            nn_iostream_printvalue(pr, entry->value.value, true, true);
            if(i != (hcap - 1))
            {
                pr->format(",\n");
            }
        }
    }
    pr->format("}>\n");
}

void nn_iostream_printfunction(NNIOStream* pr, NNObjFunction* func)
{
    if(func->name == nullptr)
    {
        pr->format("<script at %p>", (void*)func);
    }
    else
    {
        if(func->fnscriptfunc.isvariadic)
        {
            pr->format("<function %s(%d...) at %p>", nn_string_getdata(func->name), func->fnscriptfunc.arity, (void*)func);
        }
        else
        {
            pr->format("<function %s(%d) at %p>", nn_string_getdata(func->name), func->fnscriptfunc.arity, (void*)func);
        }
    }
}

void nn_iostream_printarray(NNIOStream* pr, NNObjArray* list)
{
    size_t i;
    size_t vsz;
    bool isrecur;
    NNValue val;
    NNObjArray* subarr;
    vsz = list->varray.count();
    pr->format("[");
    for(i = 0; i < vsz; i++)
    {
        isrecur = false;
        val = list->varray.get(i);
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
            nn_iostream_printvalue(pr, val, true, true);
        }
        if(i != vsz - 1)
        {
            pr->format(",");
        }
        if(pr->shortenvalues && (i >= pr->maxvallength))
        {
            pr->format(" [%ld items]", vsz);
            break;
        }
    }
    pr->format("]");
}

void nn_iostream_printdict(NNIOStream* pr, NNObjDict* dict)
{
    size_t i;
    size_t dsz;
    bool keyisrecur;
    bool valisrecur;
    NNValue val;
    NNObjDict* subdict;
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
            nn_iostream_printvalue(pr, val, true, true);
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
                nn_iostream_printvalue(pr, field->value, true, true);
            }
        }
        if(i != dsz - 1)
        {
            pr->format(", ");
        }
        if(pr->shortenvalues && (pr->maxvallength >= i))
        {
            pr->format(" [%ld items]", dsz);
            break;
        }
    }
    pr->format("}");
}

void nn_iostream_printfile(NNIOStream* pr, NNObjFile* file)
{
    pr->format("<file at %s in mode %s>", nn_string_getdata(file->path), nn_string_getdata(file->mode));
}

void nn_iostream_printinstance(NNIOStream* pr, NNObjInstance* instance, bool invmethod)
{
    (void)invmethod;
#if 0
    /*
    int arity;
    NNIOStream subw;
    NNValue resv;
    NNValue thisval;
    NNProperty* field;
    NNObjString* os;
    NNObjArray* args;
    auto gcs = SharedState::get();
    if(invmethod)
    {
        field = instance->klass->instmethods.getfieldbycstr("toString");
        if(field != nullptr)
        {
            args = nn_object_makearray();
            thisval = NNValue::fromObject(instance);
            arity = nn_nestcall_prepare(field->value, thisval, nullptr, 0);
            fprintf(stderr, "arity = %d\n", arity);
            gcs->stackPop();
            gcs->stackPush(thisval);
            if(nn_nestcall_callfunction(field->value, thisval, nullptr, 0, &resv, false))
            {
                NNIOStream::makeStackString(&subw);
                nn_iostream_printvalue(&subw, resv, false, false);
                os = subw.takeString();
                pr->writeString(nn_string_getdata(os), nn_string_getlength(os));
                #if 0
                    gcs->stackPop();
                #endif
                return;
            }
        }
    }
    */
#endif
    pr->format("<instance of %s at %p>", nn_string_getdata(instance->klass->name), (void*)instance);
}

void nn_iostream_printtable(NNIOStream* pr, NNHashTable<NNValue, NNValue>* table)
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
        nn_iostream_printvalue(pr, entry->key, true, false);
        pr->format(":");
        nn_iostream_printvalue(pr, entry->value.value, true, false);
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

void nn_iostream_printobjclass(NNIOStream* pr, NNValue value, bool fixstring, bool invmethod)
{
    bool oldexp;
    NNObjClass* klass;
    (void)fixstring;
    (void)invmethod;
    klass = value.asClass();
    if(pr->jsonmode)
    {
        pr->format("{");
        {
            {
                pr->format("name: ");
                nn_iostream_printvalue(pr, NNValue::fromObject(klass->name), true, false);
                pr->format(",");
            }
            {
                pr->format("superclass: ");
                oldexp = pr->jsonmode;
                pr->jsonmode = false;
                nn_iostream_printvalue(pr, NNValue::fromObject(klass->superclass), true, false);
                pr->jsonmode = oldexp;
                pr->format(",");
            }
            {
                pr->format("constructor: ");
                nn_iostream_printvalue(pr, klass->constructor, true, false);
                pr->format(",");
            }
            {
                pr->format("instanceproperties:");
                nn_iostream_printtable(pr, &klass->instproperties);
                pr->format(",");
            }
            {
                pr->format("staticproperties:");
                nn_iostream_printtable(pr, &klass->staticproperties);
                pr->format(",");
            }
            {
                pr->format("instancemethods:");
                nn_iostream_printtable(pr, &klass->instmethods);
                pr->format(",");
            }
            {
                pr->format("staticmethods:");
                nn_iostream_printtable(pr, &klass->staticmethods);
            }
        }
        pr->format("}");
    }
    else
    {
        pr->format("<class %s at %p>", nn_string_getdata(klass->name), (void*)klass);
    }
}

void nn_iostream_printobject(NNIOStream* pr, NNValue value, bool fixstring, bool invmethod)
{
    NNObject* obj;
    obj = value.asObject();
    switch(obj->type)
    {
        case NNObject::OTYP_SWITCH:
        {
            pr->writeString("<switch>");
        }
        break;
        case NNObject::OTYP_USERDATA:
        {
            pr->format("<userdata %s>", value.asUserdata()->name);
        }
        break;
        case NNObject::OTYP_RANGE:
        {
            NNObjRange* range;
            range = value.asRange();
            pr->format("<range %d .. %d>", range->lower, range->upper);
        }
        break;
        case NNObject::OTYP_FILE:
        {
            nn_iostream_printfile(pr, value.asFile());
        }
        break;
        case NNObject::OTYP_DICT:
        {
            nn_iostream_printdict(pr, value.asDict());
        }
        break;
        case NNObject::OTYP_ARRAY:
        {
            nn_iostream_printarray(pr, value.asArray());
        }
        break;
        case NNObject::OTYP_FUNCBOUND:
        {
            NNObjFunction* bn;
            bn = value.asFunction();
            nn_iostream_printfunction(pr, bn->fnmethod.method->fnclosure.scriptfunc);
        }
        break;
        case NNObject::OTYP_MODULE:
        {
            NNObjModule* mod;
            mod = value.asModule();
            pr->format("<module '%s' at '%s'>", nn_string_getdata(mod->name), nn_string_getdata(mod->physicalpath));
        }
        break;
        case NNObject::OTYP_CLASS:
        {
            nn_iostream_printobjclass(pr, value, fixstring, invmethod);
        }
        break;
        case NNObject::OTYP_FUNCCLOSURE:
        {
            NNObjFunction* cls;
            cls = value.asFunction();
            nn_iostream_printfunction(pr, cls->fnclosure.scriptfunc);
        }
        break;
        case NNObject::OTYP_FUNCSCRIPT:
        {
            NNObjFunction* fn;
            fn = value.asFunction();
            nn_iostream_printfunction(pr, fn);
        }
        break;
        case NNObject::OTYP_INSTANCE:
        {
            /* @TODO: support the toString() override */
            NNObjInstance* instance;
            instance = value.asInstance();
            nn_iostream_printinstance(pr, instance, invmethod);
        }
        break;
        case NNObject::OTYP_FUNCNATIVE:
        {
            NNObjFunction* native;
            native = value.asFunction();
            pr->format("<function %s(native) at %p>", nn_string_getdata(native->name), (void*)native);
        }
        break;
        case NNObject::OTYP_UPVALUE:
        {
            pr->format("<upvalue>");
        }
        break;
        case NNObject::OTYP_STRING:
        {
            NNObjString* string;
            string = value.asString();
            if(fixstring)
            {
                pr->writeQuotedString(nn_string_getdata(string), nn_string_getlength(string), true);
            }
            else
            {
                pr->writeString(nn_string_getdata(string), nn_string_getlength(string));
            }
        }
        break;
    }
}

void nn_iostream_printnumber(NNIOStream* pr, NNValue value)
{
    double dn;
    dn = value.asNumber();
    pr->format("%.16g", dn);
}

void nn_iostream_printvalue(NNIOStream* pr, NNValue value, bool fixstring, bool invmethod)
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
        nn_iostream_printnumber(pr, value);
    }
    else
    {
        nn_iostream_printobject(pr, value, fixstring, invmethod);
    }
}

void nn_dbg_disasmblob(NNIOStream* pr, NNBlob* blob, const char* name)
{
    int offset;
    pr->format("== compiled '%s' [[\n", name);
    for(offset = 0; offset < blob->count;)
    {
        offset = nn_dbg_printinstructionat(pr, blob, offset);
    }
    pr->format("]]\n");
}

void nn_dbg_printinstrname(NNIOStream* pr, const char* name)
{
    pr->format("%s%-16s%s ", nn_util_color(NEON_COLOR_RED), name, nn_util_color(NEON_COLOR_RESET));
}

int nn_dbg_printsimpleinstr(NNIOStream* pr, const char* name, int offset)
{
    nn_dbg_printinstrname(pr, name);
    pr->format("\n");
    return offset + 1;
}

int nn_dbg_printconstinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset)
{
    uint16_t constant;
    constant = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    pr->format("%8d ", constant);
    nn_iostream_printvalue(pr, blob->constants.get(constant), true, false);
    pr->format("\n");
    return offset + 3;
}

int nn_dbg_printpropertyinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset)
{
    const char* proptn;
    uint16_t constant;
    constant = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    pr->format("%8d ", constant);
    nn_iostream_printvalue(pr, blob->constants.get(constant), true, false);
    proptn = "";
    if(blob->instrucs[offset + 3].code == 1)
    {
        proptn = "static";
    }
    pr->format(" (%s)", proptn);
    pr->format("\n");
    return offset + 4;
}

int nn_dbg_printshortinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset)
{
    uint16_t slot;
    slot = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    pr->format("%8d\n", slot);
    return offset + 3;
}

int nn_dbg_printbyteinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset)
{
    uint16_t slot;
    slot = blob->instrucs[offset + 1].code;
    nn_dbg_printinstrname(pr, name);
    pr->format("%8d\n", slot);
    return offset + 2;
}

int nn_dbg_printjumpinstr(NNIOStream* pr, const char* name, int sign, NNBlob* blob, int offset)
{
    uint16_t jump;
    jump = (uint16_t)(blob->instrucs[offset + 1].code << 8);
    jump |= blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    pr->format("%8d -> %d\n", offset, offset + 3 + sign * jump);
    return offset + 3;
}

int nn_dbg_printtryinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset)
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

int nn_dbg_printinvokeinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset)
{
    uint16_t constant;
    uint16_t argcount;
    constant = (uint16_t)(blob->instrucs[offset + 1].code << 8);
    constant |= blob->instrucs[offset + 2].code;
    argcount = blob->instrucs[offset + 3].code;
    nn_dbg_printinstrname(pr, name);
    pr->format("(%d args) %8d ", argcount, constant);
    nn_iostream_printvalue(pr, blob->constants.get(constant), true, false);
    pr->format("\n");
    return offset + 4;
}

const char* nn_dbg_op2str(uint16_t instruc)
{
    switch(instruc)
    {
        case NNInstruction::OPC_GLOBALDEFINE:
            return "OPC_GLOBALDEFINE";
        case NNInstruction::OPC_GLOBALGET:
            return "OPC_GLOBALGET";
        case NNInstruction::OPC_GLOBALSET:
            return "OPC_GLOBALSET";
        case NNInstruction::OPC_LOCALGET:
            return "OPC_LOCALGET";
        case NNInstruction::OPC_LOCALSET:
            return "OPC_LOCALSET";
        case NNInstruction::OPC_FUNCARGOPTIONAL:
            return "OPC_FUNCARGOPTIONAL";
        case NNInstruction::OPC_FUNCARGSET:
            return "OPC_FUNCARGSET";
        case NNInstruction::OPC_FUNCARGGET:
            return "OPC_FUNCARGGET";
        case NNInstruction::OPC_UPVALUEGET:
            return "OPC_UPVALUEGET";
        case NNInstruction::OPC_UPVALUESET:
            return "OPC_UPVALUESET";
        case NNInstruction::OPC_UPVALUECLOSE:
            return "OPC_UPVALUECLOSE";
        case NNInstruction::OPC_PROPERTYGET:
            return "OPC_PROPERTYGET";
        case NNInstruction::OPC_PROPERTYGETSELF:
            return "OPC_PROPERTYGETSELF";
        case NNInstruction::OPC_PROPERTYSET:
            return "OPC_PROPERTYSET";
        case NNInstruction::OPC_JUMPIFFALSE:
            return "OPC_JUMPIFFALSE";
        case NNInstruction::OPC_JUMPNOW:
            return "OPC_JUMPNOW";
        case NNInstruction::OPC_LOOP:
            return "OPC_LOOP";
        case NNInstruction::OPC_EQUAL:
            return "OPC_EQUAL";
        case NNInstruction::OPC_PRIMGREATER:
            return "OPC_PRIMGREATER";
        case NNInstruction::OPC_PRIMLESSTHAN:
            return "OPC_PRIMLESSTHAN";
        case NNInstruction::OPC_PUSHEMPTY:
            return "OPC_PUSHEMPTY";
        case NNInstruction::OPC_PUSHNULL:
            return "OPC_PUSHNULL";
        case NNInstruction::OPC_PUSHTRUE:
            return "OPC_PUSHTRUE";
        case NNInstruction::OPC_PUSHFALSE:
            return "OPC_PUSHFALSE";
        case NNInstruction::OPC_PRIMADD:
            return "OPC_PRIMADD";
        case NNInstruction::OPC_PRIMSUBTRACT:
            return "OPC_PRIMSUBTRACT";
        case NNInstruction::OPC_PRIMMULTIPLY:
            return "OPC_PRIMMULTIPLY";
        case NNInstruction::OPC_PRIMDIVIDE:
            return "OPC_PRIMDIVIDE";
        case NNInstruction::OPC_PRIMMODULO:
            return "OPC_PRIMMODULO";
        case NNInstruction::OPC_PRIMPOW:
            return "OPC_PRIMPOW";
        case NNInstruction::OPC_PRIMNEGATE:
            return "OPC_PRIMNEGATE";
        case NNInstruction::OPC_PRIMNOT:
            return "OPC_PRIMNOT";
        case NNInstruction::OPC_PRIMBITNOT:
            return "OPC_PRIMBITNOT";
        case NNInstruction::OPC_PRIMAND:
            return "OPC_PRIMAND";
        case NNInstruction::OPC_PRIMOR:
            return "OPC_PRIMOR";
        case NNInstruction::OPC_PRIMBITXOR:
            return "OPC_PRIMBITXOR";
        case NNInstruction::OPC_PRIMSHIFTLEFT:
            return "OPC_PRIMSHIFTLEFT";
        case NNInstruction::OPC_PRIMSHIFTRIGHT:
            return "OPC_PRIMSHIFTRIGHT";
        case NNInstruction::OPC_PUSHONE:
            return "OPC_PUSHONE";
        case NNInstruction::OPC_PUSHCONSTANT:
            return "OPC_PUSHCONSTANT";
        case NNInstruction::OPC_ECHO:
            return "OPC_ECHO";
        case NNInstruction::OPC_POPONE:
            return "OPC_POPONE";
        case NNInstruction::OPC_DUPONE:
            return "OPC_DUPONE";
        case NNInstruction::OPC_POPN:
            return "OPC_POPN";
        case NNInstruction::OPC_ASSERT:
            return "OPC_ASSERT";
        case NNInstruction::OPC_EXTHROW:
            return "OPC_EXTHROW";
        case NNInstruction::OPC_MAKECLOSURE:
            return "OPC_MAKECLOSURE";
        case NNInstruction::OPC_CALLFUNCTION:
            return "OPC_CALLFUNCTION";
        case NNInstruction::OPC_CALLMETHOD:
            return "OPC_CALLMETHOD";
        case NNInstruction::OPC_CLASSINVOKETHIS:
            return "OPC_CLASSINVOKETHIS";
        case NNInstruction::OPC_CLASSGETTHIS:
            return "OPC_CLASSGETTHIS";
        case NNInstruction::OPC_RETURN:
            return "OPC_RETURN";
        case NNInstruction::OPC_MAKECLASS:
            return "OPC_MAKECLASS";
        case NNInstruction::OPC_MAKEMETHOD:
            return "OPC_MAKEMETHOD";
        case NNInstruction::OPC_CLASSPROPERTYDEFINE:
            return "OPC_CLASSPROPERTYDEFINE";
        case NNInstruction::OPC_CLASSINHERIT:
            return "OPC_CLASSINHERIT";
        case NNInstruction::OPC_CLASSGETSUPER:
            return "OPC_CLASSGETSUPER";
        case NNInstruction::OPC_CLASSINVOKESUPER:
            return "OPC_CLASSINVOKESUPER";
        case NNInstruction::OPC_CLASSINVOKESUPERSELF:
            return "OPC_CLASSINVOKESUPERSELF";
        case NNInstruction::OPC_MAKERANGE:
            return "OPC_MAKERANGE";
        case NNInstruction::OPC_MAKEARRAY:
            return "OPC_MAKEARRAY";
        case NNInstruction::OPC_MAKEDICT:
            return "OPC_MAKEDICT";
        case NNInstruction::OPC_INDEXGET:
            return "OPC_INDEXGET";
        case NNInstruction::OPC_INDEXGETRANGED:
            return "OPC_INDEXGETRANGED";
        case NNInstruction::OPC_INDEXSET:
            return "OPC_INDEXSET";
        case NNInstruction::OPC_IMPORTIMPORT:
            return "OPC_IMPORTIMPORT";
        case NNInstruction::OPC_EXTRY:
            return "OPC_EXTRY";
        case NNInstruction::OPC_EXPOPTRY:
            return "OPC_EXPOPTRY";
        case NNInstruction::OPC_EXPUBLISHTRY:
            return "OPC_EXPUBLISHTRY";
        case NNInstruction::OPC_STRINGIFY:
            return "OPC_STRINGIFY";
        case NNInstruction::OPC_SWITCH:
            return "OPC_SWITCH";
        case NNInstruction::OPC_TYPEOF:
            return "OPC_TYPEOF";
        case NNInstruction::OPC_BREAK_PL:
            return "OPC_BREAK_PL";
        case NNInstruction::OPC_OPINSTANCEOF:
            return "OPC_OPINSTANCEOF";
        case NNInstruction::OPC_HALT:
            return "OPC_HALT";
    }
    return "<?unknown?>";
}

int nn_dbg_printclosureinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset)
{
    int j;
    int islocal;
    uint16_t index;
    uint16_t constant;
    const char* locn;
    NNObjFunction* function;
    offset++;
    constant = blob->instrucs[offset++].code << 8;
    constant |= blob->instrucs[offset++].code;
    pr->format("%-16s %8d ", name, constant);
    nn_iostream_printvalue(pr, blob->constants.get(constant), true, false);
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

int nn_dbg_printinstructionat(NNIOStream* pr, NNBlob* blob, int offset)
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
        case NNInstruction::OPC_JUMPIFFALSE:
            return nn_dbg_printjumpinstr(pr, opname, 1, blob, offset);
        case NNInstruction::OPC_JUMPNOW:
            return nn_dbg_printjumpinstr(pr, opname, 1, blob, offset);
        case NNInstruction::OPC_EXTRY:
            return nn_dbg_printtryinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_LOOP:
            return nn_dbg_printjumpinstr(pr, opname, -1, blob, offset);
        case NNInstruction::OPC_GLOBALDEFINE:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_GLOBALGET:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_GLOBALSET:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_LOCALGET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_LOCALSET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_FUNCARGOPTIONAL:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_FUNCARGGET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_FUNCARGSET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_PROPERTYGET:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_PROPERTYGETSELF:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_PROPERTYSET:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_UPVALUEGET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_UPVALUESET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_EXPOPTRY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_EXPUBLISHTRY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PUSHCONSTANT:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_EQUAL:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMGREATER:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMLESSTHAN:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PUSHEMPTY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PUSHNULL:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PUSHTRUE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PUSHFALSE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMADD:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMSUBTRACT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMMULTIPLY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMDIVIDE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMMODULO:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMPOW:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMNEGATE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMNOT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMBITNOT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMAND:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMOR:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMBITXOR:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMSHIFTLEFT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PRIMSHIFTRIGHT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_PUSHONE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_IMPORTIMPORT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_TYPEOF:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_ECHO:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_STRINGIFY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_EXTHROW:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_POPONE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_OPINSTANCEOF:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_UPVALUECLOSE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_DUPONE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_ASSERT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_POPN:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
            /* non-user objects... */
        case NNInstruction::OPC_SWITCH:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
            /* data container manipulators */
        case NNInstruction::OPC_MAKERANGE:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_MAKEARRAY:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_MAKEDICT:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_INDEXGET:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_INDEXGETRANGED:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_INDEXSET:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_MAKECLOSURE:
            return nn_dbg_printclosureinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_CALLFUNCTION:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_CALLMETHOD:
            return nn_dbg_printinvokeinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_CLASSINVOKETHIS:
            return nn_dbg_printinvokeinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_RETURN:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_CLASSGETTHIS:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_MAKECLASS:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_MAKEMETHOD:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_CLASSPROPERTYDEFINE:
            return nn_dbg_printpropertyinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_CLASSGETSUPER:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_CLASSINHERIT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NNInstruction::OPC_CLASSINVOKESUPER:
            return nn_dbg_printinvokeinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_CLASSINVOKESUPERSELF:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        case NNInstruction::OPC_HALT:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        default:
        {
            pr->format("unknown opcode %d\n", instruction);
        }
        break;
    }
    return offset + 1;
}

NNValue nn_value_copystrlen(const char* str, size_t len)
{
    return NNValue::fromObject(nn_string_copylen(str, len));
}

NNValue nn_value_copystr(const char* str)
{
    return nn_value_copystrlen(str, strlen(str));
}

NNObjString* nn_value_tostring(NNValue value)
{
    NNIOStream pr;
    NNObjString* s;
    NNIOStream::makeStackString(&pr);
    nn_iostream_printvalue(&pr, value, false, true);
    s = pr.takeString();
    return s;
}

const char* nn_value_objecttypename(NNObject* object, bool detailed)
{
    static char buf[60];
    if(detailed)
    {
        switch(object->type)
        {
            case NNObject::OTYP_FUNCSCRIPT:
                return "funcscript";
            case NNObject::OTYP_FUNCNATIVE:
                return "funcnative";
            case NNObject::OTYP_FUNCCLOSURE:
                return "funcclosure";
            case NNObject::OTYP_FUNCBOUND:
                return "funcbound";
            default:
                break;
        }
    }
    switch(object->type)
    {
        case NNObject::OTYP_MODULE:
            return "module";
        case NNObject::OTYP_RANGE:
            return "range";
        case NNObject::OTYP_FILE:
            return "file";
        case NNObject::OTYP_DICT:
            return "dictionary";
        case NNObject::OTYP_ARRAY:
            return "array";
        case NNObject::OTYP_CLASS:
            return "class";
        case NNObject::OTYP_FUNCSCRIPT:
        case NNObject::OTYP_FUNCNATIVE:
        case NNObject::OTYP_FUNCCLOSURE:
        case NNObject::OTYP_FUNCBOUND:
            return "function";
        case NNObject::OTYP_INSTANCE:
        {
            const char* klassname;
            NNObjInstance* inst;
            inst = ((NNObjInstance*)object);
            klassname = nn_string_getdata(inst->klass->name);
            sprintf(buf, "instance@%s", klassname);
            return buf;
        }
        break;
        case NNObject::OTYP_STRING:
            return "string";
        case NNObject::OTYP_USERDATA:
            return "userdata";
        case NNObject::OTYP_SWITCH:
            return "switch";
        default:
            break;
    }
    return "unknown";
}

const char* nn_value_typename(NNValue value, bool detailed)
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

bool nn_value_compobjarray(NNObject* oa, NNObject* ob)
{
    size_t i;
    NNObjArray* arra;
    NNObjArray* arrb;
    arra = (NNObjArray*)oa;
    arrb = (NNObjArray*)ob;
    /* unlike NNObjDict, array order matters */
    if(arra->varray.count() != arrb->varray.count())
    {
        return false;
    }
    for(i = 0; i < (size_t)arra->varray.count(); i++)
    {
        if(!nn_value_compare(arra->varray.get(i), arrb->varray.get(i)))
        {
            return false;
        }
    }
    return true;
}

bool nn_value_compobjstring(NNObject* oa, NNObject* ob)
{
    size_t alen;
    size_t blen;
    const char* adata;
    const char* bdata;
    NNObjString* stra;
    NNObjString* strb;
    stra = (NNObjString*)oa;
    strb = (NNObjString*)ob;
    alen = nn_string_getlength(stra);
    blen = nn_string_getlength(strb);
    if(alen != blen)
    {
        return false;
    }
    adata = nn_string_getdata(stra);
    bdata = nn_string_getdata(strb);
    return (memcmp(adata, bdata, alen) == 0);
}

bool nn_value_compobjdict(NNObject* oa, NNObject* ob)
{
    NNObjDict* dicta;
    NNObjDict* dictb;
    NNProperty* fielda;
    NNProperty* fieldb;
    size_t ai;
    size_t lena;
    size_t lenb;
    NNValue keya;
    dicta = (NNObjDict*)oa;
    dictb = (NNObjDict*)ob;
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
            fieldb = nn_dict_getentry(dictb, keya);
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

bool nn_value_compobject(NNValue a, NNValue b)
{
    NNObject::Type ta;
    NNObject::Type tb;
    NNObject* oa;
    NNObject* ob;
    oa = a.asObject();
    ob = b.asObject();
    ta = oa->type;
    tb = ob->type;
    if(ta == tb)
    {
        /* we might not need to do a deep comparison if its the same object */
        if(oa == ob)
        {
            return true;
        }
        else if(ta == NNObject::OTYP_STRING)
        {
            return nn_value_compobjstring(oa, ob);
        }
        else if(ta == NNObject::OTYP_ARRAY)
        {
            return nn_value_compobjarray(oa, ob);
        }
        else if(ta == NNObject::OTYP_DICT)
        {
            return nn_value_compobjdict(oa, ob);
        }
    }
    return false;
}

bool nn_value_compare_actual(NNValue a, NNValue b)
{
    /*
    if(a.type != b.type)
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

bool nn_value_compare(NNValue a, NNValue b)
{
    bool r;
    r = nn_value_compare_actual(a, b);
    return r;
}

/**
 * returns the greater of the two values.
 * this function encapsulates the object hierarchy
 */
NNValue nn_value_findgreater(NNValue a, NNValue b)
{
    size_t alen;
    const char* adata;
    const char* bdata;
    NNObjString* osa;
    NNObjString* osb;
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
            adata = nn_string_getdata(osa);
            bdata = nn_string_getdata(osb);
            alen = nn_string_getlength(osa);
            if(strncmp(adata, bdata, alen) >= 0)
            {
                return a;
            }
            return b;
        }
        else if(a.isFuncscript() && b.isFuncscript())
        {
            if(a.asFunction()->fnscriptfunc.arity >= b.asFunction()->fnscriptfunc.arity)
            {
                return a;
            }
            return b;
        }
        else if(a.isFuncclosure() && b.isFuncclosure())
        {
            if(a.asFunction()->fnclosure.scriptfunc->fnscriptfunc.arity >= b.asFunction()->fnclosure.scriptfunc->fnscriptfunc.arity)
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
            if(a.asClass()->instmethods.count() >= b.asClass()->instmethods.count())
            {
                return a;
            }
            return b;
        }
        else if(a.isArray() && b.isArray())
        {
            if(a.asArray()->varray.count() >= b.asArray()->varray.count())
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
            if(strcmp(nn_string_getdata(a.asFile()->path), nn_string_getdata(b.asFile()->path)) >= 0)
            {
                return a;
            }
            return b;
        }
        else if(b.isObject())
        {
            if(a.asObject()->type >= b.asObject()->type)
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
void nn_value_sortvalues(NNValue* values, int count)
{
    int i;
    int j;
    NNValue temp;
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
                    nn_value_sortvalues((NNValue*)values[i].asArray()->varray.listitems, values[i].asArray()->varray.count());
                }

                if(values[j].isArray())
                {
                    nn_value_sortvalues((NNValue*)values[j].asArray()->varray.listitems, values[j].asArray()->varray.count());
                }
            }
        }
    }
}

NNValue nn_value_copyvalue(NNValue value)
{
    if(value.isObject())
    {
        switch(value.asObject()->type)
        {
            case NNObject::OTYP_STRING:
            {
                NNObjString* string;
                string = value.asString();
                return NNValue::fromObject(nn_string_copyobject(string));
            }
            break;
            case NNObject::OTYP_ARRAY:
            {
                size_t i;
                NNObjArray* list;
                NNObjArray* newlist;
                list = value.asArray();
                newlist = nn_object_makearray();
                for(i = 0; i < list->varray.count(); i++)
                {
                    newlist->varray.push(list->varray.get(i));
                }
                return NNValue::fromObject(newlist);
            }
            break;
            case NNObject::OTYP_DICT:
            {
                NNObjDict* dict;
                dict = value.asDict();
                return NNValue::fromObject(nn_dict_copy(dict));
            }
            break;
            default:
                break;
        }
    }
    return value;
}


NNObjUpvalue* nn_object_makeupvalue(NNValue* slot, int stackpos)
{
    NNObjUpvalue* upvalue;
    upvalue = (NNObjUpvalue*)SharedState::gcMakeObject(sizeof(NNObjUpvalue), NNObject::OTYP_UPVALUE, false);
    upvalue->closed = NNValue::makeNull();
    upvalue->location = *slot;
    upvalue->next = nullptr;
    upvalue->stackpos = stackpos;
    return upvalue;
}

NNObjUserdata* nn_object_makeuserdata(void* pointer, const char* name)
{
    NNObjUserdata* ptr;
    ptr = (NNObjUserdata*)SharedState::gcMakeObject(sizeof(NNObjUserdata), NNObject::OTYP_USERDATA, false);
    ptr->pointer = pointer;
    ptr->name = nn_util_strdup(name);
    ptr->ondestroyfn = nullptr;
    return ptr;
}

NNObjModule* nn_module_make(const char* name, const char* file, bool imported, bool retain)
{
    NNObjModule* module;
    module = (NNObjModule*)SharedState::gcMakeObject(sizeof(NNObjModule), NNObject::OTYP_MODULE, retain);
    module->deftable.initTable();
    module->name = nn_string_copycstr(name);
    module->physicalpath = nn_string_copycstr(file);
    module->fnunloaderptr = nullptr;
    module->fnpreloaderptr = nullptr;
    module->handle = nullptr;
    module->imported = imported;
    return module;
}

NNObjSwitch* nn_object_makeswitch()
{
    NNObjSwitch* sw;
    sw = (NNObjSwitch*)SharedState::gcMakeObject(sizeof(NNObjSwitch), NNObject::OTYP_SWITCH, false);
    sw->table.initTable();
    sw->defaultjump = -1;
    sw->exitjump = -1;
    return sw;
}

NNObjRange* nn_object_makerange(int lower, int upper)
{
    NNObjRange* range;
    range = (NNObjRange*)SharedState::gcMakeObject(sizeof(NNObjRange), NNObject::OTYP_RANGE, false);
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


void nn_argcheck_init(NNArgCheck* ch, const char* name, const NNFuncContext& scfn)
{
    ch->m_scriptfnctx = scfn;
    ch->name = name;
}

template<typename... ArgsT>
NNValue nn_argcheck_fail(NNArgCheck* ch, const char* srcfile, int srcline, const char* fmt, ArgsT&&... args)
{
    auto gcs = SharedState::get();
    (void)ch;
#if 0
        gcs->stackPop(ch->m_scriptfnctx.argc);
#endif
    if(!nn_except_throwactual(gcs->m_exceptions.argumenterror, srcfile, srcline, fmt, args...))
    {
    }
    return NNValue::makeBool(false);
}






void nn_state_installmethods(NNObjClass* klass, NNConstClassMethodItem* listmethods)
{
    int i;
    const char* rawname;
    NNNativeFN rawfn;
    NNObjString* osname;
    for(i = 0; listmethods[i].name != nullptr; i++)
    {
        rawname = listmethods[i].name;
        rawfn = listmethods[i].fn;
        osname = nn_string_intern(rawname);
        nn_class_defnativemethod(klass, osname, rawfn);
    }
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
NNValue nn_except_getstacktrace()
{
    int line;
    int64_t i;
    size_t instruction;
    const char* fnname;
    const char* physfile;
    NNCallFrame* frame;
    NNObjFunction* function;
    NNObjString* os;
    NNObjArray* oa;
    NNIOStream pr;
    auto gcs = SharedState::get();
    oa = nn_object_makearray();
    {
        for(i = 0; i < gcs->m_vmstate.framecount; i++)
        {
            NNIOStream::makeStackString(&pr);
            frame = &gcs->m_vmstate.framevalues[i];
            function = frame->closure->fnclosure.scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->fnscriptfunc.blob.instrucs - 1;
            line = function->fnscriptfunc.blob.instrucs[instruction].fromsourceline;
            physfile = "(unknown)";
            if(function->fnscriptfunc.module->physicalpath != nullptr)
            {
                physfile = nn_string_getdata(function->fnscriptfunc.module->physicalpath);
            }
            fnname = "<script>";
            if(function->name != nullptr)
            {
                fnname = nn_string_getdata(function->name);
            }
            pr.format("from %s() in %s:%d", fnname, physfile, line);
            os = pr.takeString();
            NNIOStream::destroy(&pr);
            nn_array_push(oa, NNValue::fromObject(os));
            if((i > 15) && (gcs->m_conf.showfullstack == false))
            {
                NNIOStream::makeStackString(&pr);
                pr.format("(only upper 15 entries shown)");
                os = pr.takeString();
                NNIOStream::destroy(&pr);
                nn_array_push(oa, NNValue::fromObject(os));
                break;
            }
        }
        return NNValue::fromObject(oa);
    }
    return NNValue::fromObject(nn_string_internlen("", 0));
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
    NNValue stackitm;
    NNObjArray* oa;
    NNObjFunction* function;
    NNCallFrame::ExceptionInfo* handler;
    NNObjString* emsg;
    NNObjString* tmp;
    NNObjInstance* exception;
    NNProperty* field;
    auto gcs = SharedState::get();
    exception = nn_vm_stackpeek(0).asInstance();
    /* look for a handler .... */
    while(gcs->m_vmstate.framecount > 0)
    {
        gcs->m_vmstate.currentframe = &gcs->m_vmstate.framevalues[gcs->m_vmstate.framecount - 1];
        for(i = gcs->m_vmstate.currentframe->handlercount; i > 0; i--)
        {
            handler = &gcs->m_vmstate.currentframe->handlers[i - 1];
            function = gcs->m_vmstate.currentframe->closure->fnclosure.scriptfunc;
            if(handler->address != 0 /*&& nn_util_isinstanceof(exception->klass, handler->handlerklass)*/)
            {
                gcs->m_vmstate.currentframe->inscode = &function->fnscriptfunc.blob.instrucs[handler->address];
                return true;
            }
            else if(handler->finallyaddress != 0)
            {
                /* continue propagating once the 'finally' block completes */
                gcs->stackPush(NNValue::makeBool(true));
                gcs->m_vmstate.currentframe->inscode = &function->fnscriptfunc.blob.instrucs[handler->finallyaddress];
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
    gcs->m_debugwriter->format("%sunhandled %s%s", colred, nn_string_getdata(exception->klass->name), colreset);
    srcfile = "none";
    srcline = 0;
    field = exception->properties.getfieldbycstr("srcline");
    if(field != nullptr)
    {
        /* why does this happen? */
        if(field->value.isNumber())
        {
            srcline = field->value.asNumber();
        }
    }
    field = exception->properties.getfieldbycstr("srcfile");
    if(field != nullptr)
    {
        if(field->value.isString())
        {
            tmp = field->value.asString();
            srcfile = nn_string_getdata(tmp);
        }
    }
    gcs->m_debugwriter->format(" [from native %s%s:%d%s]", colyellow, srcfile, srcline, colreset);
    field = exception->properties.getfieldbycstr("message");
    if(field != nullptr)
    {
        emsg = nn_value_tostring(field->value);
        if(nn_string_getlength(emsg) > 0)
        {
            gcs->m_debugwriter->format( ": %s", nn_string_getdata(emsg));
        }
        else
        {
            gcs->m_debugwriter->format(":");
        }
    }
    gcs->m_debugwriter->format("\n");
    field = exception->properties.getfieldbycstr("stacktrace");
    if(field != nullptr)
    {
        gcs->m_debugwriter->format("%sstacktrace%s:\n", colblue, colreset);
        oa = field->value.asArray();
        cnt = oa->varray.count();
        i = cnt - 1;
        if(cnt > 0)
        {
            while(true)
            {
                stackitm = oa->varray.get(i);
                gcs->m_debugwriter->format("%s", colyellow);
                gcs->m_debugwriter->format("  ");
                nn_iostream_printvalue(gcs->m_debugwriter, stackitm, false, true);
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
bool nn_except_pushhandler(NNObjClass* type, int address, int finallyaddress)
{
    NNCallFrame* frame;
    (void)type;
    auto gcs = SharedState::get();
    frame = &gcs->m_vmstate.framevalues[gcs->m_vmstate.framecount - 1];
    if(frame->handlercount == NEON_CONFIG_MAXEXCEPTHANDLERS)
    {
        nn_state_raisefatalerror("too many nested exception handlers in one function");
        return false;
    }
    frame->handlers[frame->handlercount].address = address;
    frame->handlers[frame->handlercount].finallyaddress = finallyaddress;
    /*frame->handlers[frame->handlercount].handlerklass = type;*/
    frame->handlercount++;
    return true;
}



/**
 * generate bytecode for a nativee exception class.
 * script-side it is enough to just derive from Exception, of course.
 */
NNObjClass* nn_except_makeclass(NNObjClass* baseclass, NNObjModule* module, const char* cstrname, bool iscs)
{
    int messageconst;
    NNObjClass* klass;
    NNObjString* classname;
    NNObjFunction* function;
    NNObjFunction* closure;
    if(iscs)
    {
        classname = nn_string_copycstr(cstrname);
    }
    else
    {
        classname = nn_string_copycstr(cstrname);
    }
    auto gcs = SharedState::get();
    gcs->stackPush(NNValue::fromObject(classname));
    klass = nn_object_makeclass(classname, baseclass);
    gcs->stackPop();
    gcs->stackPush(NNValue::fromObject(klass));
    function = nn_object_makefuncscript(module, NNObjFunction::CTXTYPE_METHOD);
    function->fnscriptfunc.arity = 1;
    function->fnscriptfunc.isvariadic = false;
    gcs->stackPush(NNValue::fromObject(function));
    {
        /* g_loc 0 */
        function->fnscriptfunc.blob.push(NNInstruction::make(true, NNInstruction::OPC_LOCALGET, 0));
        function->fnscriptfunc.blob.push(NNInstruction::make(false, (0 >> 8) & 0xff, 0));
        function->fnscriptfunc.blob.push(NNInstruction::make(false, 0 & 0xff, 0));
    }
    {
        /* g_loc 1 */
        function->fnscriptfunc.blob.push(NNInstruction::make(true, NNInstruction::OPC_LOCALGET, 0));
        function->fnscriptfunc.blob.push(NNInstruction::make(false, (1 >> 8) & 0xff, 0));
        function->fnscriptfunc.blob.push(NNInstruction::make(false, 1 & 0xff, 0));
    }
    {
        messageconst = function->fnscriptfunc.blob.addConstant(NNValue::fromObject(nn_string_intern("message")));
        /* s_prop 0 */
        function->fnscriptfunc.blob.push(NNInstruction::make(true, NNInstruction::OPC_PROPERTYSET, 0));
        function->fnscriptfunc.blob.push(NNInstruction::make(false, (messageconst >> 8) & 0xff, 0));
        function->fnscriptfunc.blob.push(NNInstruction::make(false, messageconst & 0xff, 0));
    }
    {
        /* pop */
        function->fnscriptfunc.blob.push(NNInstruction::make(true, NNInstruction::OPC_POPONE, 0));
        function->fnscriptfunc.blob.push(NNInstruction::make(true, NNInstruction::OPC_POPONE, 0));
    }
    {
        /* g_loc 0 */
        /*
        //  function->fnscriptfunc.blob.push(NNInstruction::make(true, NNInstruction::OPC_LOCALGET, 0));
        //  function->fnscriptfunc.blob.push(NNInstruction::make(false, (0 >> 8) & 0xff, 0));
        //  function->fnscriptfunc.blob.push(NNInstruction::make(false, 0 & 0xff, 0));
        */
    }
    {
        /* ret */
        function->fnscriptfunc.blob.push(NNInstruction::make(true, NNInstruction::OPC_RETURN, 0));
    }
    closure = nn_object_makefuncclosure(function, NNValue::makeNull());
    gcs->stackPop();
    /* set class constructor */
    gcs->stackPush(NNValue::fromObject(closure));
    klass->instmethods.set(NNValue::fromObject(classname), NNValue::fromObject(closure));
    klass->constructor = NNValue::fromObject(closure);
    /* set class properties */
    nn_class_defproperty(klass, nn_string_intern("message"), NNValue::makeNull());
    nn_class_defproperty(klass, nn_string_intern("stacktrace"), NNValue::makeNull());
    nn_class_defproperty(klass, nn_string_intern("srcfile"), NNValue::makeNull());
    nn_class_defproperty(klass, nn_string_intern("srcline"), NNValue::makeNull());
    nn_class_defproperty(klass, nn_string_intern("class"), NNValue::fromObject(klass));
    gcs->m_declaredglobals.set(NNValue::fromObject(classname), NNValue::fromObject(klass));
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
NNObjInstance* nn_except_makeinstance(NNObjClass* exklass, const char* srcfile, int srcline, NNObjString* message)
{
    NNObjInstance* instance;
    NNObjString* osfile;
    auto gcs = SharedState::get();
    instance = nn_object_makeinstance(exklass);
    osfile = nn_string_copycstr(srcfile);
    gcs->stackPush(NNValue::fromObject(instance));
    nn_instance_defproperty(instance, nn_string_intern("class"), NNValue::fromObject(exklass));
    nn_instance_defproperty(instance, nn_string_intern("message"), NNValue::fromObject(message));
    nn_instance_defproperty(instance, nn_string_intern("srcfile"), NNValue::fromObject(osfile));
    nn_instance_defproperty(instance, nn_string_intern("srcline"), NNValue::makeNumber(srcline));
    gcs->stackPop();
    return instance;
}



bool nn_state_defglobalvalue(const char* name, NNValue val)
{
    bool r;
    NNObjString* oname;
    oname = nn_string_intern(name);
    auto gcs = SharedState::get();
    gcs->stackPush(NNValue::fromObject(oname));
    gcs->stackPush(val);
    r = gcs->m_declaredglobals.set(gcs->m_vmstate.stackvalues[0], gcs->m_vmstate.stackvalues[1]);
    gcs->stackPop(2);
    return r;
}

bool nn_state_defnativefunctionptr(const char* name, NNNativeFN fptr, void* uptr)
{
    NNObjFunction* func;
    func = nn_object_makefuncnative(fptr, name, uptr);
    return nn_state_defglobalvalue(name, NNValue::fromObject(func));
}

bool nn_state_defnativefunction(const char* name, NNNativeFN fptr)
{
    return nn_state_defnativefunctionptr(name, fptr, nullptr);
}

NNObjClass* nn_util_makeclass(const char* name, NNObjClass* parent)
{
    NNObjClass* cl;
    NNObjString* os;
    auto gcs = SharedState::get();
    os = nn_string_copycstr(name);
    cl = nn_object_makeclass(os, parent);
    gcs->m_declaredglobals.set(NNValue::fromObject(os), NNValue::fromObject(cl));
    return cl;
}



bool nn_util_methodisprivate(NNObjString* name)
{
    return nn_string_getlength(name) > 0 && nn_string_getdata(name)[0] == '_';
}





/*
 * $keeplast: whether to emit code that retains or discards the value of the last statement/expression.
 * SHOULD NOT BE USED FOR ORDINARY SCRIPTS as it will almost definitely result in the stack containing invalid values.
 */
NNObjFunction* nn_astutil_compilesource(NNObjModule* module, const char* source, NNBlob* blob, bool fromimport, bool keeplast)
{
    NNAstLexer* lexer;
    NNAstParser* parser;
    NNObjFunction* function;
    (void)blob;
    lexer = NNAstLexer::make(source);
    parser = NNAstParser::make(lexer, module, keeplast);
    NNAstParser::FuncCompiler fnc(parser, NNObjFunction::CTXTYPE_SCRIPT, true);
    fnc.fromimport = fromimport;
    parser->runparser();
    function = parser->endcompiler(true);
    if(parser->m_haderror)
    {
        function = nullptr;
    }
    NNAstLexer::destroy(lexer);
    NNAstParser::destroy(parser);
    return function;
}

NNObjArray* nn_array_makefilled(size_t cnt, NNValue filler)
{
    size_t i;
    NNObjArray* list;
    list = (NNObjArray*)SharedState::gcMakeObject(sizeof(NNObjArray), NNObject::OTYP_ARRAY, false);
    list->varray.initSelf();
    if(cnt > 0)
    {
        for(i = 0; i < cnt; i++)
        {
            list->varray.push(filler);
        }
    }
    return list;
}

NNObjArray* nn_array_make()
{
    return nn_array_makefilled(0, NNValue::makeNull());
}

NNObjArray* nn_object_makearray()
{
    return nn_array_make();
}

void nn_array_push(NNObjArray* list, NNValue value)
{
    auto gcs = SharedState::get();
    (void)gcs;
    /*gcs->stackPush(value);*/
    list->varray.push(value);
    /*gcs->stackPop(); */
}

bool nn_array_get(NNObjArray* list, size_t idx, NNValue* vdest)
{
    size_t vc;
    vc = list->varray.count();
    if((vc > 0) && (idx < vc))
    {
        *vdest = list->varray.get(idx);
        return true;
    }
    return false;
}

NNObjArray* nn_array_copy(NNObjArray* list, long start, long length)
{
    size_t i;
    NNObjArray* newlist;
    newlist = (NNObjArray*)nn_object_makearray();
    if(start == -1)
    {
        start = 0;
    }
    if(length == -1)
    {
        length = list->varray.count() - start;
    }
    for(i = start; i < (size_t)(start + length); i++)
    {
        nn_array_push(newlist, list->varray.get(i));
    }
    return newlist;
}


static NNValue nn_objfnarray_constructor(const NNFuncContext& scfn)
{
    int cnt;
    NNValue filler;
    NNObjArray* arr;
    NNArgCheck check;
    nn_argcheck_init(&check, "constructor", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    filler = NNValue::makeNull();
    if(scfn.argc > 1)
    {
        filler = scfn.argv[1];
    }
    cnt = scfn.argv[0].asNumber();
    arr = nn_array_makefilled(cnt, filler);
    return NNValue::fromObject(arr);
}

static NNValue nn_objfnarray_join(const NNFuncContext& scfn)
{
    bool havejoinee;
    size_t i;
    size_t count;
    NNIOStream pr;
    NNValue ret;
    NNValue vjoinee;
    NNObjArray* selfarr;
    NNObjString* joinee;
    NNValue* list;
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
    list = (NNValue*)selfarr->varray.listitems;
    count = selfarr->varray.count();
    if(count == 0)
    {
        return NNValue::fromObject(nn_string_intern(""));
    }
    NNIOStream::makeStackString(&pr);
    for(i = 0; i < count; i++)
    {
        nn_iostream_printvalue(&pr, list[i], false, true);
        if((havejoinee && (joinee != nullptr)) && ((i + 1) < count))
        {
            pr.writeString(nn_string_getdata(joinee), nn_string_getlength(joinee));
        }
    }
    ret = NNValue::fromObject(pr.takeString());
    NNIOStream::destroy(&pr);
    return ret;
}

static NNValue nn_objfnarray_length(const NNFuncContext& scfn)
{
    NNObjArray* selfarr;
    NNArgCheck check;
    nn_argcheck_init(&check, "length", scfn);
    selfarr = scfn.thisval.asArray();
    return NNValue::makeNumber(selfarr->varray.count());
}

static NNValue nn_objfnarray_append(const NNFuncContext& scfn)
{
    size_t i;
    for(i = 0; i < scfn.argc; i++)
    {
        nn_array_push(scfn.thisval.asArray(), scfn.argv[i]);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnarray_clear(const NNFuncContext& scfn)
{
    NNArgCheck check;
    nn_argcheck_init(&check, "clear", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    scfn.thisval.asArray()->varray.deInit(false);
    return NNValue::makeNull();
}

static NNValue nn_objfnarray_clone(const NNFuncContext& scfn)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "clone", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = scfn.thisval.asArray();
    return NNValue::fromObject(nn_array_copy(list, 0, list->varray.count()));
}

static NNValue nn_objfnarray_count(const NNFuncContext& scfn)
{
    size_t i;
    int count;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "count", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = scfn.thisval.asArray();
    count = 0;
    for(i = 0; i < list->varray.count(); i++)
    {
        if(nn_value_compare(list->varray.get(i), scfn.argv[0]))
        {
            count++;
        }
    }
    return NNValue::makeNumber(count);
}

static NNValue nn_objfnarray_extend(const NNFuncContext& scfn)
{
    size_t i;
    NNObjArray* list;
    NNObjArray* list2;
    NNArgCheck check;
    nn_argcheck_init(&check, "extend", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isarray);
    list = scfn.thisval.asArray();
    list2 = scfn.argv[0].asArray();
    for(i = 0; i < list2->varray.count(); i++)
    {
        nn_array_push(list, list2->varray.get(i));
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnarray_indexof(const NNFuncContext& scfn)
{
    size_t i;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "indexOf", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    list = scfn.thisval.asArray();
    i = 0;
    if(scfn.argc == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        i = scfn.argv[1].asNumber();
    }
    for(; i < list->varray.count(); i++)
    {
        if(nn_value_compare(list->varray.get(i), scfn.argv[0]))
        {
            return NNValue::makeNumber(i);
        }
    }
    return NNValue::makeNumber(-1);
}

static NNValue nn_objfnarray_insert(const NNFuncContext& scfn)
{
    int index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "insert", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    list = scfn.thisval.asArray();
    index = (int)scfn.argv[1].asNumber();
    list->varray.insert(scfn.argv[0], index);
    return NNValue::makeNull();
}

static NNValue nn_objfnarray_pop(const NNFuncContext& scfn)
{
    NNValue value;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "pop", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = scfn.thisval.asArray();
    if(list->varray.count() > 0)
    {
        value = list->varray.get(list->varray.count() - 1);
        list->varray.decreaseBy(1);
        return value;
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnarray_shift(const NNFuncContext& scfn)
{
    size_t i;
    size_t j;
    size_t count;
    NNObjArray* list;
    NNObjArray* newlist;
    NNArgCheck check;
    nn_argcheck_init(&check, "shift", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    count = 1;
    if(scfn.argc == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        count = scfn.argv[0].asNumber();
    }
    list = scfn.thisval.asArray();
    if(count >= list->varray.count() || list->varray.count() == 1)
    {
        list->varray.setCount(0);
        return NNValue::makeNull();
    }
    else if(count > 0)
    {
        newlist = (NNObjArray*)((NNObject*)nn_object_makearray());
        for(i = 0; i < count; i++)
        {
            nn_array_push(newlist, list->varray.get(0));
            for(j = 0; j < list->varray.count(); j++)
            {
                list->varray.set(j, list->varray.get(j + 1));
            }
            list->varray.decreaseBy(1);
        }
        if(count == 1)
        {
            return newlist->varray.get(0);
        }
        else
        {
            return NNValue::fromObject(newlist);
        }
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnarray_removeat(const NNFuncContext& scfn)
{
    size_t i;
    size_t index;
    NNValue value;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "removeAt", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = scfn.thisval.asArray();
    index = scfn.argv[0].asNumber();
    if(((int)index < 0) || index >= list->varray.count())
    {
        NEON_RETURNERROR(scfn, "list index %d out of range at remove_at()", index);
    }
    value = list->varray.get(index);
    for(i = index; i < list->varray.count() - 1; i++)
    {
        list->varray.set(i, list->varray.get(i + 1));
    }
    list->varray.decreaseBy(1);
    return value;
}

static NNValue nn_objfnarray_remove(const NNFuncContext& scfn)
{
    size_t i;
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "remove", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = scfn.thisval.asArray();
    index = -1;
    for(i = 0; i < list->varray.count(); i++)
    {
        if(nn_value_compare(list->varray.get(i), scfn.argv[0]))
        {
            index = i;
            break;
        }
    }
    if((int)index != -1)
    {
        for(i = index; i < list->varray.count(); i++)
        {
            list->varray.set(i, list->varray.get(i + 1));
        }
        list->varray.decreaseBy(1);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnarray_reverse(const NNFuncContext& scfn)
{
    int fromtop;
    NNObjArray* list;
    NNObjArray* nlist;
    NNArgCheck check;
    nn_argcheck_init(&check, "reverse", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = scfn.thisval.asArray();
    nlist = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    /* in-place reversal:*/
    /*
    int start = 0;
    int end = list->varray.count() - 1;
    while (start < end)
    {
        NNValue temp = list->varray.get(start);
        list->varray.set(start, list->varray.get(end));
        list->varray.set(end, temp);
        start++;
        end--;
    }
    */
    for(fromtop = list->varray.count() - 1; fromtop >= 0; fromtop--)
    {
        nn_array_push(nlist, list->varray.get(fromtop));
    }
    return NNValue::fromObject(nlist);
}

static NNValue nn_objfnarray_sort(const NNFuncContext& scfn)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "sort", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = scfn.thisval.asArray();
    nn_value_sortvalues((NNValue*)list->varray.listitems, list->varray.count());
    return NNValue::makeNull();
}

static NNValue nn_objfnarray_contains(const NNFuncContext& scfn)
{
    size_t i;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "contains", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = scfn.thisval.asArray();
    for(i = 0; i < list->varray.count(); i++)
    {
        if(nn_value_compare(scfn.argv[0], list->varray.get(i)))
        {
            return NNValue::makeBool(true);
        }
    }
    return NNValue::makeBool(false);
}

static NNValue nn_objfnarray_delete(const NNFuncContext& scfn)
{
    size_t i;
    size_t idxupper;
    size_t idxlower;
    NNObjArray* list;
    NNArgCheck check;
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
    if(((int)idxlower < 0) || idxlower >= list->varray.count())
    {
        NEON_RETURNERROR(scfn, "list index %d out of range at delete()", idxlower);
    }
    else if(idxupper < idxlower || idxupper >= list->varray.count())
    {
        NEON_RETURNERROR(scfn, "invalid upper limit %d at delete()", idxupper);
    }
    for(i = 0; i < list->varray.count() - idxupper; i++)
    {
        list->varray.set(idxlower + i, list->varray.get(i + idxupper + 1));
    }
    list->varray.decreaseBy(idxupper - idxlower + 1);
    return NNValue::makeNumber((double)idxupper - (double)idxlower + 1);
}

static NNValue nn_objfnarray_first(const NNFuncContext& scfn)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "first", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = scfn.thisval.asArray();
    if(list->varray.count() > 0)
    {
        return list->varray.get(0);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnarray_last(const NNFuncContext& scfn)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "last", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = scfn.thisval.asArray();
    if(list->varray.count() > 0)
    {
        return list->varray.get(list->varray.count() - 1);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnarray_isempty(const NNFuncContext& scfn)
{
    NNArgCheck check;
    nn_argcheck_init(&check, "isEmpty", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return NNValue::makeBool(scfn.thisval.asArray()->varray.count() == 0);
}

static NNValue nn_objfnarray_take(const NNFuncContext& scfn)
{
    size_t count;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "take", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = scfn.thisval.asArray();
    count = scfn.argv[0].asNumber();
    if((int)count < 0)
    {
        count = list->varray.count() + count;
    }
    if(list->varray.count() < count)
    {
        return NNValue::fromObject(nn_array_copy(list, 0, list->varray.count()));
    }
    return NNValue::fromObject(nn_array_copy(list, 0, count));
}

static NNValue nn_objfnarray_get(const NNFuncContext& scfn)
{
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "get", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = scfn.thisval.asArray();
    index = scfn.argv[0].asNumber();
    if((int)index < 0 || index >= list->varray.count())
    {
        return NNValue::makeNull();
    }
    return list->varray.get(index);
}

static NNValue nn_objfnarray_compact(const NNFuncContext& scfn)
{
    size_t i;
    NNObjArray* list;
    NNObjArray* newlist;
    NNArgCheck check;
    nn_argcheck_init(&check, "compact", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = scfn.thisval.asArray();
    newlist = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    for(i = 0; i < list->varray.count(); i++)
    {
        if(!nn_value_compare(list->varray.get(i), NNValue::makeNull()))
        {
            nn_array_push(newlist, list->varray.get(i));
        }
    }
    return NNValue::fromObject(newlist);
}

static NNValue nn_objfnarray_unique(const NNFuncContext& scfn)
{
    size_t i;
    size_t j;
    bool found;
    NNObjArray* list;
    NNObjArray* newlist;
    NNArgCheck check;
    nn_argcheck_init(&check, "unique", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = scfn.thisval.asArray();
    newlist = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    for(i = 0; i < list->varray.count(); i++)
    {
        found = false;
        for(j = 0; j < newlist->varray.count(); j++)
        {
            if(nn_value_compare(newlist->varray.get(j), list->varray.get(i)))
            {
                found = true;
                continue;
            }
        }
        if(!found)
        {
            nn_array_push(newlist, list->varray.get(i));
        }
    }
    return NNValue::fromObject(newlist);
}

static NNValue nn_objfnarray_zip(const NNFuncContext& scfn)
{
    size_t i;
    size_t j;
    NNObjArray* list;
    NNObjArray* newlist;
    NNObjArray* alist;
    NNObjArray** arglist;
    NNArgCheck check;
    nn_argcheck_init(&check, "zip", scfn);
    list = scfn.thisval.asArray();
    newlist = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    arglist = (NNObjArray**)SharedState::gcAllocate(sizeof(NNObjArray*), scfn.argc, false);
    for(i = 0; i < scfn.argc; i++)
    {
        NEON_ARGS_CHECKTYPE(&check, i, nn_value_isarray);
        arglist[i] = scfn.argv[i].asArray();
    }
    for(i = 0; i < list->varray.count(); i++)
    {
        alist = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
        /* item of main list*/
        nn_array_push(alist, list->varray.get(i));
        for(j = 0; j < scfn.argc; j++)
        {
            if(i < arglist[j]->varray.count())
            {
                nn_array_push(alist, arglist[j]->varray.get(i));
            }
            else
            {
                nn_array_push(alist, NNValue::makeNull());
            }
        }
        nn_array_push(newlist, NNValue::fromObject(alist));
    }
    return NNValue::fromObject(newlist);
}

static NNValue nn_objfnarray_zipfrom(const NNFuncContext& scfn)
{
    size_t i;
    size_t j;
    NNObjArray* list;
    NNObjArray* newlist;
    NNObjArray* alist;
    NNObjArray* arglist;
    NNArgCheck check;
    nn_argcheck_init(&check, "zipFrom", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isarray);
    list = scfn.thisval.asArray();
    newlist = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    arglist = scfn.argv[0].asArray();
    for(i = 0; i < arglist->varray.count(); i++)
    {
        if(!arglist->varray.get(i).isArray())
        {
            NEON_RETURNERROR(scfn, "invalid list in zip entries");
        }
    }
    for(i = 0; i < list->varray.count(); i++)
    {
        alist = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
        nn_array_push(alist, list->varray.get(i));
        for(j = 0; j < arglist->varray.count(); j++)
        {
            if(i < arglist->varray.get(j).asArray()->varray.count())
            {
                nn_array_push(alist, arglist->varray.get(j).asArray()->varray.get(i));
            }
            else
            {
                nn_array_push(alist, NNValue::makeNull());
            }
        }
        nn_array_push(newlist, NNValue::fromObject(alist));
    }
    return NNValue::fromObject(newlist);
}

static NNValue nn_objfnarray_todict(const NNFuncContext& scfn)
{
    size_t i;
    NNObjDict* dict;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "toDict", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = (NNObjDict*)nn_gcmem_protect((NNObject*)nn_object_makedict());
    list = scfn.thisval.asArray();
    for(i = 0; i < list->varray.count(); i++)
    {
        nn_dict_setentry(dict, NNValue::makeNumber(i), list->varray.get(i));
    }
    return NNValue::fromObject(dict);
}

static NNValue nn_objfnarray_iter(const NNFuncContext& scfn)
{
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "iter", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = scfn.thisval.asArray();
    index = scfn.argv[0].asNumber();
    if(((int)index > -1) && index < list->varray.count())
    {
        return list->varray.get(index);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnarray_itern(const NNFuncContext& scfn)
{
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "itern", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = scfn.thisval.asArray();
    if(scfn.argv[0].isNull())
    {
        if(list->varray.count() == 0)
        {
            return NNValue::makeBool(false);
        }
        return NNValue::makeNumber(0);
    }
    if(!scfn.argv[0].isNumber())
    {
        NEON_RETURNERROR(scfn, "lists are numerically indexed");
    }
    index = scfn.argv[0].asNumber();
    if(index < list->varray.count() - 1)
    {
        return NNValue::makeNumber((double)index + 1);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnarray_each(const NNFuncContext& scfn)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue callable;
    NNValue unused;
    NNObjArray* list;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(&check, "each", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = scfn.thisval.asArray();
    callable = scfn.argv[0];
    arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
    for(i = 0; i < list->varray.count(); i++)
    {
        passi = 0;
        if(arity > 0)
        {
            passi++;
            nestargs[0] = list->varray.get(i);
            if(arity > 1)
            {
                passi++;
                nestargs[1] = NNValue::makeNumber(i);
            }
        }
        nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &unused, false);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnarray_map(const NNFuncContext& scfn)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue res;
    NNValue callable;
    NNObjArray* list;
    NNObjArray* resultlist;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(&check, "map", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = scfn.thisval.asArray();
    callable = scfn.argv[0];
    arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
    resultlist = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    for(i = 0; i < list->varray.count(); i++)
    {
        passi = 0;
        if(!list->varray.get(i).isNull())
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = list->varray.get(i);
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = NNValue::makeNumber(i);
                }
            }
            nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &res, false);
            nn_array_push(resultlist, res);
        }
        else
        {
            nn_array_push(resultlist, NNValue::makeNull());
        }
    }
    return NNValue::fromObject(resultlist);
}

static NNValue nn_objfnarray_filter(const NNFuncContext& scfn)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue callable;
    NNValue result;
    NNObjArray* list;
    NNObjArray* resultlist;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(&check, "filter", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = scfn.thisval.asArray();
    callable = scfn.argv[0];
    arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
    resultlist = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    for(i = 0; i < list->varray.count(); i++)
    {
        passi = 0;
        if(!list->varray.get(i).isNull())
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = list->varray.get(i);
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = NNValue::makeNumber(i);
                }
            }
            nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &result, false);
            if(!result.isFalse())
            {
                nn_array_push(resultlist, list->varray.get(i));
            }
        }
    }
    return NNValue::fromObject(resultlist);
}

static NNValue nn_objfnarray_some(const NNFuncContext& scfn)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue callable;
    NNValue result;
    NNObjArray* list;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(&check, "some", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = scfn.thisval.asArray();
    callable = scfn.argv[0];
    arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
    for(i = 0; i < list->varray.count(); i++)
    {
        passi = 0;
        if(!list->varray.get(i).isNull())
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = list->varray.get(i);
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = NNValue::makeNumber(i);
                }
            }
            nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &result, false);
            if(!result.isFalse())
            {
                return NNValue::makeBool(true);
            }
        }
    }
    return NNValue::makeBool(false);
}

static NNValue nn_objfnarray_every(const NNFuncContext& scfn)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue result;
    NNValue callable;
    NNObjArray* list;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(&check, "every", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = scfn.thisval.asArray();
    callable = scfn.argv[0];
    arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
    for(i = 0; i < list->varray.count(); i++)
    {
        passi = 0;
        if(!list->varray.get(i).isNull())
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = list->varray.get(i);
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = NNValue::makeNumber(i);
                }
            }
            nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &result, false);
            if(result.isFalse())
            {
                return NNValue::makeBool(false);
            }
        }
    }
    return NNValue::makeBool(true);
}

static NNValue nn_objfnarray_reduce(const NNFuncContext& scfn)
{
    size_t i;
    size_t passi;
    size_t arity;
    size_t startindex;
    NNValue callable;
    NNValue accumulator;
    NNObjArray* list;
    NNArgCheck check;
    NNValue nestargs[5];
    nn_argcheck_init(&check, "reduce", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = scfn.thisval.asArray();
    callable = scfn.argv[0];
    startindex = 0;
    accumulator = NNValue::makeNull();
    if(scfn.argc == 2)
    {
        accumulator = scfn.argv[1];
    }
    if(accumulator.isNull() && list->varray.count() > 0)
    {
        accumulator = list->varray.get(0);
        startindex = 1;
    }
    arity = nn_nestcall_prepare(callable, scfn.thisval, nullptr, 4);
    for(i = startindex; i < list->varray.count(); i++)
    {
        passi = 0;
        if(!list->varray.get(i).isNull() && !list->varray.get(i).isNull())
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = accumulator;
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = list->varray.get(i);
                    if(arity > 2)
                    {
                        passi++;
                        nestargs[2] = NNValue::makeNumber(i);
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

static NNValue nn_objfnarray_slice(const NNFuncContext& scfn)
{
    int64_t i;
    int64_t until;
    int64_t start;
    int64_t end;
    int64_t ibegin;
    int64_t iend;
    int64_t salen;
    bool backwards;
    NNObjArray* selfarr;
    NNObjArray* narr;
    NNArgCheck check;
    nn_argcheck_init(&check, "slice", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    selfarr = scfn.thisval.asArray();
    salen = selfarr->varray.count();
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
    narr = nn_object_makearray();
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
        nn_array_push(narr, selfarr->varray.get(i));
    }
    return NNValue::fromObject(narr);
}

void nn_state_installobjectarray()
{
    static NNConstClassMethodItem arraymethods[] = { { "size", nn_objfnarray_length }, { "join", nn_objfnarray_join },       { "append", nn_objfnarray_append }, { "push", nn_objfnarray_append }, { "clear", nn_objfnarray_clear },     { "clone", nn_objfnarray_clone },   { "count", nn_objfnarray_count }, { "extend", nn_objfnarray_extend },   { "indexOf", nn_objfnarray_indexof }, { "insert", nn_objfnarray_insert }, { "pop", nn_objfnarray_pop }, { "shift", nn_objfnarray_shift },   { "removeAt", nn_objfnarray_removeat }, { "remove", nn_objfnarray_remove }, { "reverse", nn_objfnarray_reverse }, { "sort", nn_objfnarray_sort },   { "contains", nn_objfnarray_contains }, { "delete", nn_objfnarray_delete }, { "first", nn_objfnarray_first },
                                                     { "last", nn_objfnarray_last },   { "isEmpty", nn_objfnarray_isempty }, { "take", nn_objfnarray_take },     { "get", nn_objfnarray_get },     { "compact", nn_objfnarray_compact }, { "unique", nn_objfnarray_unique }, { "zip", nn_objfnarray_zip },     { "zipFrom", nn_objfnarray_zipfrom }, { "toDict", nn_objfnarray_todict },   { "each", nn_objfnarray_each },     { "map", nn_objfnarray_map }, { "filter", nn_objfnarray_filter }, { "some", nn_objfnarray_some },         { "every", nn_objfnarray_every },   { "reduce", nn_objfnarray_reduce },   { "slice", nn_objfnarray_slice }, { "@iter", nn_objfnarray_iter },        { "@itern", nn_objfnarray_itern },  { nullptr, nullptr } };
    auto gcs = SharedState::get();
    nn_class_defnativeconstructor(gcs->m_classprimarray, nn_objfnarray_constructor);
    nn_class_defcallablefield(gcs->m_classprimarray, nn_string_intern("length"), nn_objfnarray_length);
    nn_state_installmethods(gcs->m_classprimarray, arraymethods);
}

NNObjClass* nn_object_makeclass(NNObjString* name, NNObjClass* parent)
{
    NNObjClass* klass;
    klass = (NNObjClass*)SharedState::gcMakeObject(sizeof(NNObjClass), NNObject::OTYP_CLASS, false);
    klass->name = name;
    klass->instproperties.initTable();
    klass->staticproperties.initTable();
    klass->instmethods.initTable();
    klass->staticmethods.initTable();
    klass->constructor = NNValue::makeNull();
    klass->destructor = NNValue::makeNull();
    klass->superclass = parent;
    return klass;
}

void nn_class_destroy(NNObjClass* klass)
{
    klass->instmethods.deinit();
    klass->staticmethods.deinit();
    klass->instproperties.deinit();
    klass->staticproperties.deinit();
    /*
    // We are not freeing the initializer because it's a closure and will still be freed accordingly later.
    */
    memset(klass, 0, sizeof(NNObjClass));
    SharedState::gcRelease(klass);
}

bool nn_class_inheritfrom(NNObjClass* subclass, NNObjClass* superclass)
{
    int failcnt;
    failcnt = 0;
    if(!superclass->instproperties.copyTo(&subclass->instproperties, true))
    {
        failcnt++;
    }
    if(!superclass->instmethods.copyTo(&subclass->instmethods, true))
    {
        failcnt++;
    }
    subclass->superclass = superclass;
    if(failcnt == 0)
    {
        return true;
    }
    return false;
}

bool nn_class_defproperty(NNObjClass* klass, NNObjString* cstrname, NNValue val)
{
    return klass->instproperties.set(NNValue::fromObject(cstrname), val);
}

bool nn_class_defcallablefieldptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncnative(function, nn_string_getdata(name), uptr);
    return klass->instproperties.setwithtype(NNValue::fromObject(name), NNValue::fromObject(ofn), NNProperty::FTYP_FUNCTION, true);
}

bool nn_class_defcallablefield(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defcallablefieldptr(klass, name, function, nullptr);
}

bool nn_class_defstaticcallablefieldptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncnative(function, nn_string_getdata(name), uptr);
    return klass->staticproperties.setwithtype(NNValue::fromObject(name), NNValue::fromObject(ofn), NNProperty::FTYP_FUNCTION, true);
}

bool nn_class_defstaticcallablefield(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defstaticcallablefieldptr(klass, name, function, nullptr);
}

bool nn_class_setstaticproperty(NNObjClass* klass, NNObjString* name, NNValue val)
{
    return klass->staticproperties.set(NNValue::fromObject(name), val);
}

bool nn_class_defnativeconstructorptr(NNObjClass* klass, NNNativeFN function, void* uptr)
{
    const char* cname;
    NNObjFunction* ofn;
    cname = "constructor";
    ofn = nn_object_makefuncnative(function, cname, uptr);
    klass->constructor = NNValue::fromObject(ofn);
    return true;
}

bool nn_class_defnativeconstructor(NNObjClass* klass, NNNativeFN function)
{
    return nn_class_defnativeconstructorptr(klass, function, nullptr);
}

bool nn_class_defmethod(NNObjClass* klass, NNObjString* name, NNValue val)
{
    return klass->instmethods.set(NNValue::fromObject(name), val);
}

bool nn_class_defnativemethodptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* ptr)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncnative(function, nn_string_getdata(name), ptr);
    return nn_class_defmethod(klass, name, NNValue::fromObject(ofn));
}

bool nn_class_defnativemethod(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defnativemethodptr(klass, name, function, nullptr);
}

bool nn_class_defstaticnativemethodptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncnative(function, nn_string_getdata(name), uptr);
    return klass->staticmethods.set(NNValue::fromObject(name), NNValue::fromObject(ofn));
}

bool nn_class_defstaticnativemethod(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defstaticnativemethodptr(klass, name, function, nullptr);
}

NNProperty* nn_class_getmethodfield(NNObjClass* klass, NNObjString* name)
{
    NNProperty* field;
    field = klass->instmethods.getfield(NNValue::fromObject(name));
    if(field != nullptr)
    {
        return field;
    }
    if(klass->superclass != nullptr)
    {
        return nn_class_getmethodfield(klass->superclass, name);
    }
    return nullptr;
}

NNProperty* nn_class_getpropertyfield(NNObjClass* klass, NNObjString* name)
{
    NNProperty* field;
    field = klass->instproperties.getfield(NNValue::fromObject(name));
#if 0
    if(field == nullptr)
    {
        if(klass->superclass != nullptr)
        {
            return nn_class_getpropertyfield(klass->superclass, name);
        }
    }
#endif
    return field;
}

NNProperty* nn_class_getstaticproperty(NNObjClass* klass, NNObjString* name)
{
    NNProperty* np;
    np = klass->staticproperties.getfieldbyostr(name);
    if(np != nullptr)
    {
        return np;
    }
    if(klass->superclass != nullptr)
    {
        return nn_class_getstaticproperty(klass->superclass, name);
    }
    return nullptr;
}

NNProperty* nn_class_getstaticmethodfield(NNObjClass* klass, NNObjString* name)
{
    NNProperty* field;
    field = klass->staticmethods.getfield(NNValue::fromObject(name));
    return field;
}

NNObjInstance* nn_object_makeinstancesize(NNObjClass* klass, size_t sz)
{
    NNObjInstance* oinst;
    NNObjInstance* instance;
    oinst = nullptr;
    instance = (NNObjInstance*)SharedState::gcMakeObject(sz, NNObject::OTYP_INSTANCE, false);
    instance->active = true;
    instance->klass = klass;
    instance->superinstance = nullptr;
    instance->properties.initTable();
    if(klass->instproperties.count() > 0)
    {
        klass->instproperties.copy(&instance->properties);
    }
    if(klass->superclass != nullptr)
    {
        oinst = nn_object_makeinstance(klass->superclass);
        instance->superinstance = oinst;
    }
    return instance;
}

NNObjInstance* nn_object_makeinstance(NNObjClass* klass)
{
    return nn_object_makeinstancesize(klass, sizeof(NNObjInstance));
}

void nn_instance_mark(NNObjInstance* instance)
{
    if(instance->active == false)
    {
        // nn_state_warn("trying to mark inactive instance <%p>!", instance);
        return;
    }
    nn_valtable_mark(&instance->properties);
    NNObject::markObject((NNObject*)instance->klass);
}

void nn_instance_destroy(NNObjInstance* instance)
{
    if(!instance->klass->destructor.isNull())
    {
        // if(!nn_vm_callvaluewithobject(instance->klass->destructor, NNValue::fromObject(instance), 0, false))
        {
        }
    }
    instance->properties.deinit();
    instance->active = false;
    SharedState::gcRelease(instance);
}

bool nn_instance_defproperty(NNObjInstance* instance, NNObjString* name, NNValue val)
{
    return instance->properties.set(NNValue::fromObject(name), val);
}

NNProperty* nn_instance_getvar(NNObjInstance* inst, NNObjString* name)
{
    NNProperty* field;
    field = inst->properties.getfield(NNValue::fromObject(name));
    if(field == nullptr)
    {
        if(inst->superinstance != nullptr)
        {
            return nn_instance_getvar(inst->superinstance, name);
        }
    }
    return field;
}

NNProperty* nn_instance_getvarcstr(NNObjInstance* inst, const char* name)
{
    NNObjString* os;
    os = nn_string_intern(name);
    return nn_instance_getvar(inst, os);
}

NNProperty* nn_instance_getmethod(NNObjInstance* inst, NNObjString* name)
{
    NNProperty* field;
    field = nn_class_getmethodfield(inst->klass, name);
    if(field == nullptr)
    {
        if(inst->superinstance != nullptr)
        {
            return nn_instance_getmethod(inst->superinstance, name);
        }
    }
    return field;
}

NNProperty* nn_instance_getmethodcstr(NNObjInstance* inst, const char* name)
{
    NNObjString* os;
    os = nn_string_intern(name);
    return nn_instance_getmethod(inst, os);
}

NNObjDict* nn_object_makedict()
{
    NNObjDict* dict;
    dict = (NNObjDict*)SharedState::gcMakeObject(sizeof(NNObjDict), NNObject::OTYP_DICT, false);
    dict->htnames.initSelf();
    dict->htab.initTable();
    return dict;
}

void nn_dict_destroy(NNObjDict* dict)
{
    dict->htnames.deInit(false);
    dict->htab.deinit();
}

bool nn_dict_setentry(NNObjDict* dict, NNValue key, NNValue value)
{
    NNValue tempvalue;
    if(!dict->htab.get(key, &tempvalue))
    {
        /* add key if it doesn't exist. */
        dict->htnames.push(key);
    }
    return dict->htab.set(key, value);
}

void nn_dict_addentry(NNObjDict* dict, NNValue key, NNValue value)
{
    nn_dict_setentry(dict, key, value);
}

void nn_dict_addentrycstr(NNObjDict* dict, const char* ckey, NNValue value)
{
    NNObjString* os;
    os = nn_string_copycstr(ckey);
    nn_dict_addentry(dict, NNValue::fromObject(os), value);
}

NNProperty* nn_dict_getentry(NNObjDict* dict, NNValue key)
{
    return dict->htab.getfield(key);
}

NNObjDict* nn_dict_copy(NNObjDict* dict)
{
    size_t i;
    size_t dsz;
    NNValue key;
    NNProperty* field;
    NNObjDict* ndict;
    ndict = nn_object_makedict();
    /*
    // @TODO: Figure out how to handle dictionary values correctly
    // remember that copying keys is redundant and unnecessary
    */
    dsz = dict->htnames.count();
    for(i = 0; i < dsz; i++)
    {
        key = dict->htnames.get(i);
        field = dict->htab.getfield(dict->htnames.get(i));
        ndict->htnames.push(key);
        ndict->htab.setwithtype(key, field->value, field->type, key.isString());
    }
    return ndict;
}

static NNValue nn_objfndict_length(const NNFuncContext& scfn)
{
    NNArgCheck check;
    nn_argcheck_init(&check, "length", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return NNValue::makeNumber(scfn.thisval.asDict()->htnames.count());
}

static NNValue nn_objfndict_add(const NNFuncContext& scfn)
{
    NNValue tempvalue;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(&check, "add", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    dict = scfn.thisval.asDict();
    if(dict->htab.get(scfn.argv[0], &tempvalue))
    {
        NEON_RETURNERROR(scfn, "duplicate key %s at add()", nn_string_getdata(nn_value_tostring(scfn.argv[0])));
    }
    nn_dict_addentry(dict, scfn.argv[0], scfn.argv[1]);
    return NNValue::makeNull();
}

static NNValue nn_objfndict_set(const NNFuncContext& scfn)
{
    NNValue value;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(&check, "set", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    dict = scfn.thisval.asDict();
    if(!dict->htab.get(scfn.argv[0], &value))
    {
        nn_dict_addentry(dict, scfn.argv[0], scfn.argv[1]);
    }
    else
    {
        nn_dict_setentry(dict, scfn.argv[0], scfn.argv[1]);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfndict_clear(const NNFuncContext& scfn)
{
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(&check, "clear", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = scfn.thisval.asDict();
    dict->htnames.deInit(false);
    dict->htab.deinit();
    return NNValue::makeNull();
}

static NNValue nn_objfndict_clone(const NNFuncContext& scfn)
{
    size_t i;
    NNObjDict* dict;
    NNObjDict* newdict;
    NNArgCheck check;
    auto gcs = SharedState::get();
    nn_argcheck_init(&check, "clone", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = scfn.thisval.asDict();
    newdict = (NNObjDict*)nn_gcmem_protect((NNObject*)nn_object_makedict());
    if(!dict->htab.copyTo(&newdict->htab, true))
    {
        nn_except_throwclass(gcs->m_exceptions.argumenterror, "failed to copy table");
        return NNValue::makeNull();
    }
    for(i = 0; i < dict->htnames.count(); i++)
    {
        newdict->htnames.push(dict->htnames.get(i));
    }
    return NNValue::fromObject(newdict);
}

static NNValue nn_objfndict_compact(const NNFuncContext& scfn)
{
    size_t i;
    NNObjDict* dict;
    NNObjDict* newdict;
    NNValue tmpvalue;
    NNArgCheck check;
    nn_argcheck_init(&check, "compact", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = scfn.thisval.asDict();
    newdict = (NNObjDict*)nn_gcmem_protect((NNObject*)nn_object_makedict());
    tmpvalue = NNValue::makeNull();
    for(i = 0; i < dict->htnames.count(); i++)
    {
        dict->htab.get(dict->htnames.get(i), &tmpvalue);
        if(!nn_value_compare(tmpvalue, NNValue::makeNull()))
        {
            nn_dict_addentry(newdict, dict->htnames.get(i), tmpvalue);
        }
    }
    return NNValue::fromObject(newdict);
}

static NNValue nn_objfndict_contains(const NNFuncContext& scfn)
{
    NNValue value;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(&check, "contains", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = scfn.thisval.asDict();
    return NNValue::makeBool(dict->htab.get(scfn.argv[0], &value));
}

static NNValue nn_objfndict_extend(const NNFuncContext& scfn)
{
    size_t i;
    NNValue tmp;
    NNObjDict* dict;
    NNObjDict* dictcpy;
    NNArgCheck check;
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
    return NNValue::makeNull();
}

static NNValue nn_objfndict_get(const NNFuncContext& scfn)
{
    NNObjDict* dict;
    NNProperty* field;
    NNArgCheck check;
    nn_argcheck_init(&check, "get", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    dict = scfn.thisval.asDict();
    field = nn_dict_getentry(dict, scfn.argv[0]);
    if(field == nullptr)
    {
        if(scfn.argc == 1)
        {
            return NNValue::makeNull();
        }
        else
        {
            return scfn.argv[1];
        }
    }
    return field->value;
}

static NNValue nn_objfndict_keys(const NNFuncContext& scfn)
{
    size_t i;
    NNObjDict* dict;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(&check, "keys", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = scfn.thisval.asDict();
    list = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    for(i = 0; i < dict->htnames.count(); i++)
    {
        nn_array_push(list, dict->htnames.get(i));
    }
    return NNValue::fromObject(list);
}

static NNValue nn_objfndict_values(const NNFuncContext& scfn)
{
    size_t i;
    NNObjDict* dict;
    NNObjArray* list;
    NNProperty* field;
    NNArgCheck check;
    nn_argcheck_init(&check, "values", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = scfn.thisval.asDict();
    list = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    for(i = 0; i < dict->htnames.count(); i++)
    {
        field = nn_dict_getentry(dict, dict->htnames.get(i));
        nn_array_push(list, field->value);
    }
    return NNValue::fromObject(list);
}

static NNValue nn_objfndict_remove(const NNFuncContext& scfn)
{
    size_t i;
    int index;
    NNValue value;
    NNObjDict* dict;
    NNArgCheck check;
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
        dict->htnames.decreaseBy(1);
        return value;
    }
    return NNValue::makeNull();
}

static NNValue nn_objfndict_isempty(const NNFuncContext& scfn)
{
    NNArgCheck check;
    nn_argcheck_init(&check, "isempty", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return NNValue::makeBool(scfn.thisval.asDict()->htnames.count() == 0);
}

static NNValue nn_objfndict_findkey(const NNFuncContext& scfn)
{
    NNArgCheck check;
    nn_argcheck_init(&check, "findkey", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    auto ht = scfn.thisval.asDict()->htab;
    return ht.findkey(scfn.argv[0], NNValue::makeNull());
}

static NNValue nn_objfndict_tolist(const NNFuncContext& scfn)
{
    size_t i;
    NNObjArray* list;
    NNObjDict* dict;
    NNObjArray* namelist;
    NNObjArray* valuelist;
    NNArgCheck check;
    nn_argcheck_init(&check, "tolist", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = scfn.thisval.asDict();
    namelist = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    valuelist = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    for(i = 0; i < dict->htnames.count(); i++)
    {
        nn_array_push(namelist, dict->htnames.get(i));
        NNValue value;
        if(dict->htab.get(dict->htnames.get(i), &value))
        {
            nn_array_push(valuelist, value);
        }
        else
        {
            /* theoretically impossible */
            nn_array_push(valuelist, NNValue::makeNull());
        }
    }
    list = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    nn_array_push(list, NNValue::fromObject(namelist));
    nn_array_push(list, NNValue::fromObject(valuelist));
    return NNValue::fromObject(list);
}

static NNValue nn_objfndict_iter(const NNFuncContext& scfn)
{
    NNValue result;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(&check, "iter", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = scfn.thisval.asDict();
    if(dict->htab.get(scfn.argv[0], &result))
    {
        return result;
    }
    return NNValue::makeNull();
}

static NNValue nn_objfndict_itern(const NNFuncContext& scfn)
{
    size_t i;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(&check, "itern", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = scfn.thisval.asDict();
    if(scfn.argv[0].isNull())
    {
        if(dict->htnames.count() == 0)
        {
            return NNValue::makeBool(false);
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
    return NNValue::makeNull();
}

static NNValue nn_objfndict_each(const NNFuncContext& scfn)
{
    size_t i;
    size_t passi;
    int arity;
    NNValue value;
    NNValue callable;
    NNValue unused;
    NNObjDict* dict;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(&check, "each", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = scfn.thisval.asDict();
    callable = scfn.argv[0];
    arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
    value = NNValue::makeNull();
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
    return NNValue::makeNull();
}

static NNValue nn_objfndict_filter(const NNFuncContext& scfn)
{
    size_t i;
    size_t passi;
    int arity;
    NNValue value;
    NNValue callable;
    NNValue result;
    NNObjDict* dict;
    NNObjDict* resultdict;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(&check, "filter", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = scfn.thisval.asDict();
    callable = scfn.argv[0];
    arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
    resultdict = (NNObjDict*)nn_gcmem_protect((NNObject*)nn_object_makedict());
    value = NNValue::makeNull();
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
            nn_dict_addentry(resultdict, dict->htnames.get(i), value);
        }
    }
    /* pop the call list */
    return NNValue::fromObject(resultdict);
}

static NNValue nn_objfndict_some(const NNFuncContext& scfn)
{
    size_t i;
    size_t passi;
    int arity;
    NNValue result;
    NNValue value;
    NNValue callable;
    NNObjDict* dict;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(&check, "some", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = scfn.thisval.asDict();
    callable = scfn.argv[0];
    arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
    value = NNValue::makeNull();
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
            return NNValue::makeBool(true);
        }
    }
    /* pop the call list */
    return NNValue::makeBool(false);
}

static NNValue nn_objfndict_every(const NNFuncContext& scfn)
{
    size_t i;
    size_t passi;
    int arity;
    NNValue value;
    NNValue callable;
    NNValue result;
    NNObjDict* dict;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(&check, "every", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = scfn.thisval.asDict();
    callable = scfn.argv[0];
    arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
    value = NNValue::makeNull();
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
            return NNValue::makeBool(false);
        }
    }
    return NNValue::makeBool(true);
}

static NNValue nn_objfndict_reduce(const NNFuncContext& scfn)
{
    size_t i;
    size_t passi;
    int arity;
    int startindex;
    NNValue value;
    NNValue callable;
    NNValue accumulator;
    NNObjDict* dict;
    NNArgCheck check;
    NNValue nestargs[5];
    nn_argcheck_init(&check, "reduce", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = scfn.thisval.asDict();
    callable = scfn.argv[0];
    startindex = 0;
    accumulator = NNValue::makeNull();
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
    value = NNValue::makeNull();
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
    static NNConstClassMethodItem dictmethods[] = {
        { "keys", nn_objfndict_keys }, { "size", nn_objfndict_length }, { "add", nn_objfndict_add }, { "set", nn_objfndict_set }, { "clear", nn_objfndict_clear }, { "clone", nn_objfndict_clone }, { "compact", nn_objfndict_compact }, { "contains", nn_objfndict_contains }, { "extend", nn_objfndict_extend }, { "get", nn_objfndict_get }, { "values", nn_objfndict_values }, { "remove", nn_objfndict_remove }, { "isEmpty", nn_objfndict_isempty }, { "findKey", nn_objfndict_findkey }, { "toList", nn_objfndict_tolist }, { "each", nn_objfndict_each }, { "filter", nn_objfndict_filter }, { "some", nn_objfndict_some }, { "every", nn_objfndict_every }, { "reduce", nn_objfndict_reduce }, { "@iter", nn_objfndict_iter }, { "@itern", nn_objfndict_itern }, { nullptr, nullptr },
    };
    auto gcs = SharedState::get();
#if 0
    nn_class_defnativeconstructor(gcs->m_classprimdict, nn_objfndict_constructor);
    nn_class_defstaticnativemethod(gcs->m_classprimdict, nn_string_copycstr("keys"), nn_objfndict_keys);
#endif
    nn_state_installmethods(gcs->m_classprimdict, dictmethods);
}

#
NNObjFile* nn_object_makefile(FILE* handle, bool isstd, const char* path, const char* mode)
{
    NNObjFile* file;
    file = (NNObjFile*)SharedState::gcMakeObject(sizeof(NNObjFile), NNObject::OTYP_FILE, false);
    file->isopen = false;
    file->mode = nn_string_copycstr(mode);
    file->path = nn_string_copycstr(path);
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

void nn_file_destroy(NNObjFile* file)
{
    nn_fileobject_close(file);
    SharedState::gcRelease(file);
}

void nn_file_mark(NNObjFile* file)
{
    NNObject::markObject((NNObject*)file->mode);
    NNObject::markObject((NNObject*)file->path);
}

bool nn_file_read(NNObjFile* file, size_t readhowmuch, NNIOResult* dest)
{
    size_t filesizereal;
    struct stat stats;
    filesizereal = -1;
    dest->success = false;
    dest->length = 0;
    dest->data = nullptr;
    if(!file->isstd)
    {
        if(!nn_util_fsfileexists(nn_string_getdata(file->path)))
        {
            return false;
        }
        /* file is in write only mode */
        /*
        else if(strstr(nn_string_getdata(file->mode), "w") != nullptr && strstr(nn_string_getdata(file->mode), "+") == nullptr)
        {
            FILE_ERROR(scfn, Unsupported, "cannot read file in write mode");
        }
        */
        if(!file->isopen)
        {
            /* open the file if it isn't open */
            nn_fileobject_open(file);
        }
        else if(file->handle == nullptr)
        {
            return false;
        }
        if(osfn_lstat(nn_string_getdata(file->path), &stats) == 0)
        {
            filesizereal = (size_t)stats.st_size;
        }
        else
        {
            /* fallback */
            fseek(file->handle, 0L, SEEK_END);
            filesizereal = ftell(file->handle);
            rewind(file->handle);
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
    dest->length = fread(dest->data, sizeof(char), readhowmuch, file->handle);
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

#define FILE_ERROR(scfn, type, message) NEON_RETURNERROR(scfn, #type " -> %s", message, nn_string_getdata(file->path));

#define RETURN_STATUS(scfn, status)              \
    if((status) == 0)                      \
    {                                      \
        return NNValue::makeBool(true);    \
    }                                      \
    else                                   \
    {                                      \
        FILE_ERROR(scfn, File, strerror(errno)); \
    }

#define DENY_STD(scfn)  \
    if(file->isstd) \
        NEON_RETURNERROR(scfn, "method not supported for std files");

int nn_fileobject_close(NNObjFile* file)
{
    int result;
    if(file->handle != nullptr && !file->isstd)
    {
        fflush(file->handle);
        result = fclose(file->handle);
        file->handle = nullptr;
        file->isopen = false;
        file->number = -1;
        file->istty = false;
        return result;
    }
    return -1;
}

bool nn_fileobject_open(NNObjFile* file)
{
    if(file->handle != nullptr)
    {
        return true;
    }
    if(file->handle == nullptr && !file->isstd)
    {
        file->handle = fopen(nn_string_getdata(file->path), nn_string_getdata(file->mode));
        if(file->handle != nullptr)
        {
            file->isopen = true;
            file->number = fileno(file->handle);
            file->istty = osfn_isatty(file->number);
            return true;
        }
        else
        {
            file->number = -1;
            file->istty = false;
        }
        return false;
    }
    return false;
}

static NNValue nn_objfnfile_constructor(const NNFuncContext& scfn)
{
    FILE* hnd;
    const char* path;
    const char* mode;
    NNObjString* opath;
    NNObjFile* file;
    (void)hnd;
    NNArgCheck check;
    nn_argcheck_init(&check, "constructor", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    opath = scfn.argv[0].asString();
    if(nn_string_getlength(opath) == 0)
    {
        NEON_RETURNERROR(scfn, "file path cannot be empty");
    }
    mode = "r";
    if(scfn.argc == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
        mode = nn_string_getdata(scfn.argv[1].asString());
    }
    path = nn_string_getdata(opath);
    file = (NNObjFile*)nn_gcmem_protect((NNObject*)nn_object_makefile(nullptr, false, path, mode));
    nn_fileobject_open(file);
    return NNValue::fromObject(file);
}

static NNValue nn_objfnfile_exists(const NNFuncContext& scfn)
{
    NNObjString* file;
    NNArgCheck check;
    nn_argcheck_init(&check, "exists", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = scfn.argv[0].asString();
    return NNValue::makeBool(nn_util_fsfileexists(nn_string_getdata(file)));
}

static NNValue nn_objfnfile_isfile(const NNFuncContext& scfn)
{
    NNObjString* file;
    NNArgCheck check;
    nn_argcheck_init(&check, "isfile", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = scfn.argv[0].asString();
    return NNValue::makeBool(nn_util_fsfileisfile(nn_string_getdata(file)));
}

static NNValue nn_objfnfile_isdirectory(const NNFuncContext& scfn)
{
    NNObjString* file;
    NNArgCheck check;
    nn_argcheck_init(&check, "isdirectory", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = scfn.argv[0].asString();
    return NNValue::makeBool(nn_util_fsfileisdirectory(nn_string_getdata(file)));
}

static NNValue nn_objfnfile_readstatic(const NNFuncContext& scfn)
{
    char* buf;
    size_t thismuch;
    size_t actualsz;
    NNObjString* filepath;
    NNArgCheck check;
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
    buf = nn_util_filereadfile(nn_string_getdata(filepath), &actualsz, true, thismuch);
    if(buf == nullptr)
    {
        nn_except_throwclass(gcs->m_exceptions.ioerror, "%s: %s", nn_string_getdata(filepath), strerror(errno));
        return NNValue::makeNull();
    }
    return NNValue::fromObject(nn_string_takelen(buf, actualsz));
}

static NNValue nn_objfnfile_writestatic(const NNFuncContext& scfn)
{
    bool appending;
    size_t rt;
    FILE* fh;
    const char* mode;
    NNObjString* filepath;
    NNObjString* data;
    NNArgCheck check;
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
    fh = fopen(nn_string_getdata(filepath), mode);
    if(fh == nullptr)
    {
        nn_except_throwclass(gcs->m_exceptions.ioerror, strerror(errno));
        return NNValue::makeNull();
    }
    rt = fwrite(nn_string_getdata(data), sizeof(char), nn_string_getlength(data), fh);
    fclose(fh);
    return NNValue::makeNumber(rt);
}

static NNValue nn_objfnfile_close(const NNFuncContext& scfn)
{
    NNArgCheck check;
    nn_argcheck_init(&check, "close", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_fileobject_close(scfn.thisval.asFile());
    return NNValue::makeNull();
}

static NNValue nn_objfnfile_open(const NNFuncContext& scfn)
{
    NNArgCheck check;
    nn_argcheck_init(&check, "open", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_fileobject_open(scfn.thisval.asFile());
    return NNValue::makeNull();
}

static NNValue nn_objfnfile_isopen(const NNFuncContext& scfn)
{
    NNObjFile* file;
    file = scfn.thisval.asFile();
    return NNValue::makeBool(file->isstd || file->isopen);
}

static NNValue nn_objfnfile_isclosed(const NNFuncContext& scfn)
{
    NNObjFile* file;
    file = scfn.thisval.asFile();
    return NNValue::makeBool(!file->isstd && !file->isopen);
}

static NNValue nn_objfnfile_readmethod(const NNFuncContext& scfn)
{
    size_t readhowmuch;
    NNIOResult res;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(&check, "read", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    readhowmuch = -1;
    if(scfn.argc == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        readhowmuch = (size_t)scfn.argv[0].asNumber();
    }
    file = scfn.thisval.asFile();
    if(!nn_file_read(file, readhowmuch, &res))
    {
        FILE_ERROR(scfn, NotFound, strerror(errno));
    }
    return NNValue::fromObject(nn_string_takelen(res.data, res.length));
}

static NNValue nn_objfnfile_readline(const NNFuncContext& scfn)
{
    long rdline;
    size_t linelen;
    char* strline;
    NNObjFile* file;
    NNArgCheck check;
    NNObjString* nos;
    nn_argcheck_init(&check, "readLine", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    file = scfn.thisval.asFile();
    linelen = 0;
    strline = nullptr;
    rdline = nn_util_filegetlinehandle(&strline, &linelen, file->handle);
    if(rdline == -1)
    {
        return NNValue::makeNull();
    }
    nos = nn_string_takelen(strline, rdline);
    return NNValue::fromObject(nos);
}

static NNValue nn_objfnfile_get(const NNFuncContext& scfn)
{
    int ch;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(&check, "get", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = scfn.thisval.asFile();
    ch = fgetc(file->handle);
    if(ch == EOF)
    {
        return NNValue::makeNull();
    }
    return NNValue::makeNumber(ch);
}

static NNValue nn_objfnfile_gets(const NNFuncContext& scfn)
{
    long end;
    long length;
    long currentpos;
    size_t bytesread;
    char* buffer;
    NNObjFile* file;
    NNArgCheck check;
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
        if(!nn_util_fsfileexists(nn_string_getdata(file->path)))
        {
            FILE_ERROR(scfn, NotFound, "no such file or directory");
        }
        else if(strstr(nn_string_getdata(file->mode), "w") != nullptr && strstr(nn_string_getdata(file->mode), "+") == nullptr)
        {
            FILE_ERROR(scfn, Unsupported, "cannot read file in write mode");
        }
        if(!file->isopen)
        {
            FILE_ERROR(scfn, Read, "file not open");
        }
        else if(file->handle == nullptr)
        {
            FILE_ERROR(scfn, Read, "could not read file");
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
            FILE_ERROR(scfn, Unsupported, "cannot read from output file");
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
        FILE_ERROR(scfn, Buffer, "not enough memory to read file");
    }
    bytesread = fread(buffer, sizeof(char), length, file->handle);
    if(bytesread == 0 && length != 0)
    {
        FILE_ERROR(scfn, Read, "could not read file contents");
    }
    if(buffer != nullptr)
    {
        buffer[bytesread] = '\0';
    }
    return NNValue::fromObject(nn_string_takelen(buffer, bytesread));
}

static NNValue nn_objfnfile_write(const NNFuncContext& scfn)
{
    size_t count;
    int length;
    unsigned char* data;
    NNObjFile* file;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(&check, "write", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    file = scfn.thisval.asFile();
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = scfn.argv[0].asString();
    data = (unsigned char*)nn_string_getdata(string);
    length = nn_string_getlength(string);
    if(!file->isstd)
    {
        if(strstr(nn_string_getdata(file->mode), "r") != nullptr && strstr(nn_string_getdata(file->mode), "+") == nullptr)
        {
            FILE_ERROR(scfn, Unsupported, "cannot write into non-writable file");
        }
        else if(length == 0)
        {
            FILE_ERROR(scfn, Write, "cannot write empty buffer to file");
        }
        else if(file->handle == nullptr || !file->isopen)
        {
            nn_fileobject_open(file);
        }
        else if(file->handle == nullptr)
        {
            FILE_ERROR(scfn, Write, "could not write to file");
        }
    }
    else
    {
        if(fileno(stdin) == file->number)
        {
            FILE_ERROR(scfn, Unsupported, "cannot write to input file");
        }
    }
    count = fwrite(data, sizeof(unsigned char), length, file->handle);
    fflush(file->handle);
    if(count > (size_t)0)
    {
        return NNValue::makeBool(true);
    }
    return NNValue::makeBool(false);
}

static NNValue nn_objfnfile_puts(const NNFuncContext& scfn)
{
    size_t i;
    size_t count;
    int rc;
    int length;
    unsigned char* data;
    NNObjFile* file;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(&check, "puts", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    file = scfn.thisval.asFile();

    if(!file->isstd)
    {
        if(strstr(nn_string_getdata(file->mode), "r") != nullptr && strstr(nn_string_getdata(file->mode), "+") == nullptr)
        {
            FILE_ERROR(scfn, Unsupported, "cannot write into non-writable file");
        }
        else if(!file->isopen)
        {
            FILE_ERROR(scfn, Write, "file not open");
        }
        else if(file->handle == nullptr)
        {
            FILE_ERROR(scfn, Write, "could not write to file");
        }
    }
    else
    {
        if(fileno(stdin) == file->number)
        {
            FILE_ERROR(scfn, Unsupported, "cannot write to input file");
        }
    }
    rc = 0;
    for(i = 0; i < scfn.argc; i++)
    {
        NEON_ARGS_CHECKTYPE(&check, i, nn_value_isstring);
        string = scfn.argv[i].asString();
        data = (unsigned char*)nn_string_getdata(string);
        length = nn_string_getlength(string);
        count = fwrite(data, sizeof(unsigned char), length, file->handle);
        if(count > (size_t)0 || length == 0)
        {
            return NNValue::makeNumber(0);
        }
        rc += count;
    }
    return NNValue::makeNumber(rc);
}

static NNValue nn_objfnfile_putc(const NNFuncContext& scfn)
{
    size_t i;
    int rc;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(&check, "puts", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    file = scfn.thisval.asFile();
    if(!file->isstd)
    {
        if(strstr(nn_string_getdata(file->mode), "r") != nullptr && strstr(nn_string_getdata(file->mode), "+") == nullptr)
        {
            FILE_ERROR(scfn, Unsupported, "cannot write into non-writable file");
        }
        else if(!file->isopen)
        {
            FILE_ERROR(scfn, Write, "file not open");
        }
        else if(file->handle == nullptr)
        {
            FILE_ERROR(scfn, Write, "could not write to file");
        }
    }
    else
    {
        if(fileno(stdin) == file->number)
        {
            FILE_ERROR(scfn, Unsupported, "cannot write to input file");
        }
    }
    rc = 0;
    for(i = 0; i < scfn.argc; i++)
    {
        NEON_ARGS_CHECKTYPE(&check, i, nn_value_isnumber);
        int cv = scfn.argv[i].asNumber();
        rc += fputc(cv, file->handle);
    }
    return NNValue::makeNumber(rc);
}

static NNValue nn_objfnfile_printf(const NNFuncContext& scfn)
{
    NNObjFile* file;
    NNFormatInfo nfi;
    NNIOStream pr;
    NNObjString* ofmt;
    NNArgCheck check;
    nn_argcheck_init(&check, "printf", scfn);
    file = scfn.thisval.asFile();
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = scfn.argv[0].asString();
    NNIOStream::makeStackIO(&pr, file->handle, false);
    nn_strformat_init(&nfi, &pr, nn_string_getdata(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, scfn.argc, 1, scfn.argv))
    {
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnfile_number(const NNFuncContext& scfn)
{
    NNArgCheck check;
    nn_argcheck_init(&check, "number", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return NNValue::makeNumber(scfn.thisval.asFile()->number);
}

static NNValue nn_objfnfile_istty(const NNFuncContext& scfn)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(&check, "istty", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = scfn.thisval.asFile();
    return NNValue::makeBool(file->istty);
}

static NNValue nn_objfnfile_flush(const NNFuncContext& scfn)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(&check, "flush", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = scfn.thisval.asFile();
    if(!file->isopen)
    {
        FILE_ERROR(scfn, Unsupported, "I/O operation on closed file");
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
    return NNValue::makeNull();
}

static NNValue nn_objfnfile_path(const NNFuncContext& scfn)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(&check, "path", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = scfn.thisval.asFile();
    DENY_STD(scfn);
    return NNValue::fromObject(file->path);
}

static NNValue nn_objfnfile_mode(const NNFuncContext& scfn)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(&check, "mode", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = scfn.thisval.asFile();
    return NNValue::fromObject(file->mode);
}

static NNValue nn_objfnfile_name(const NNFuncContext& scfn)
{
    char* name;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(&check, "name", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = scfn.thisval.asFile();
    if(!file->isstd)
    {
        name = nn_util_fsgetbasename(nn_string_getdata(file->path));
        return NNValue::fromObject(nn_string_copycstr(name));
    }
    else if(file->istty)
    {
        return NNValue::fromObject(nn_string_copycstr("<tty>"));
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnfile_seek(const NNFuncContext& scfn)
{
    long position;
    int seektype;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(&check, "seek", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    file = scfn.thisval.asFile();
    DENY_STD(scfn);
    position = (long)scfn.argv[0].asNumber();
    seektype = scfn.argv[1].asNumber();
    RETURN_STATUS(scfn, fseek(file->handle, position, seektype));
}

static NNValue nn_objfnfile_tell(const NNFuncContext& scfn)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(&check, "tell", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = scfn.thisval.asFile();
    DENY_STD(scfn);
    return NNValue::makeNumber(ftell(file->handle));
}

#undef FILE_ERROR
#undef RETURN_STATUS
#undef DENY_STD

NNValue nn_modfn_os_stat(const NNFuncContext& scfn);

void nn_state_installobjectfile()
{
    static NNConstClassMethodItem filemethods[] = {
        { "close", nn_objfnfile_close }, { "open", nn_objfnfile_open }, { "read", nn_objfnfile_readmethod }, { "get", nn_objfnfile_get }, { "gets", nn_objfnfile_gets }, { "write", nn_objfnfile_write }, { "puts", nn_objfnfile_puts }, { "putc", nn_objfnfile_putc }, { "printf", nn_objfnfile_printf }, { "number", nn_objfnfile_number }, { "isTTY", nn_objfnfile_istty }, { "isOpen", nn_objfnfile_isopen }, { "isClosed", nn_objfnfile_isclosed }, { "flush", nn_objfnfile_flush }, { "path", nn_objfnfile_path }, { "seek", nn_objfnfile_seek }, { "tell", nn_objfnfile_tell }, { "mode", nn_objfnfile_mode }, { "name", nn_objfnfile_name }, { "readLine", nn_objfnfile_readline }, { nullptr, nullptr },
    };
    auto gcs = SharedState::get();
    nn_class_defnativeconstructor(gcs->m_classprimfile, nn_objfnfile_constructor);
    nn_class_defstaticnativemethod(gcs->m_classprimfile, nn_string_copycstr("read"), nn_objfnfile_readstatic);
    nn_class_defstaticnativemethod(gcs->m_classprimfile, nn_string_copycstr("write"), nn_objfnfile_writestatic);
    nn_class_defstaticnativemethod(gcs->m_classprimfile, nn_string_copycstr("put"), nn_objfnfile_writestatic);
    nn_class_defstaticnativemethod(gcs->m_classprimfile, nn_string_copycstr("exists"), nn_objfnfile_exists);
    nn_class_defstaticnativemethod(gcs->m_classprimfile, nn_string_copycstr("isFile"), nn_objfnfile_isfile);
    nn_class_defstaticnativemethod(gcs->m_classprimfile, nn_string_copycstr("isDirectory"), nn_objfnfile_isdirectory);
    nn_class_defstaticnativemethod(gcs->m_classprimfile, nn_string_copycstr("stat"), nn_modfn_os_stat);
    nn_state_installmethods(gcs->m_classprimfile, filemethods);
}

void nn_funcscript_destroy(NNObjFunction* ofn)
{
    NNBlob::destroy(&ofn->fnscriptfunc.blob);
}

NNObjFunction* nn_object_makefuncnative(NNNativeFN natfn, const char* name, void* uptr)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncgeneric(nn_string_copycstr(name), NNObject::OTYP_FUNCNATIVE, NNValue::makeNull());
    ofn->fnnativefunc.natfunc = natfn;
    ofn->contexttype = NNObjFunction::CTXTYPE_FUNCTION;
    ofn->fnnativefunc.userptr = uptr;
    return ofn;
}

NNObjFunction* nn_object_makefuncclosure(NNObjFunction* innerfn, NNValue thisval)
{
    int i;
    NNObjUpvalue** upvals;
    NNObjFunction* ofn;
    upvals = nullptr;
    if(innerfn->upvalcount > 0)
    {
        upvals = (NNObjUpvalue**)SharedState::gcAllocate(sizeof(NNObjUpvalue*), innerfn->upvalcount + 1, false);
        for(i = 0; i < innerfn->upvalcount; i++)
        {
            upvals[i] = nullptr;
        }
    }
    ofn = nn_object_makefuncgeneric(innerfn->name, NNObject::OTYP_FUNCCLOSURE, thisval);
    ofn->fnclosure.scriptfunc = innerfn;
    ofn->fnclosure.upvalues = upvals;
    ofn->upvalcount = innerfn->upvalcount;
    return ofn;
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

void nn_import_loadbuiltinmodules()
{
    int i;
    static NNModInitFN g_builtinmodules[] = {
        nn_natmodule_load_null, nn_natmodule_load_os, nn_natmodule_load_astscan, nn_natmodule_load_complex, nullptr,
    };
    for(i = 0; g_builtinmodules[i] != nullptr; i++)
    {
        nn_import_loadnativemodule(g_builtinmodules[i], nullptr, "<__native__>", nullptr);
    }
}

bool nn_state_addmodulesearchpathobj(NNObjString* os)
{
    auto gcs = SharedState::get();
    gcs->m_importpath.push(NNValue::fromObject(os));
    return true;
}

bool nn_state_addmodulesearchpath(const char* path)
{
    return nn_state_addmodulesearchpathobj(nn_string_copycstr(path));
}

void nn_state_setupmodulepaths()
{
    int i;
    static const char* defaultsearchpaths[] = { "mods", "mods/@/index" NEON_CONFIG_FILEEXT, ".", nullptr };
    auto gcs = SharedState::get();
    gcs->m_importpath.initSelf();
    nn_state_addmodulesearchpathobj(gcs->m_processinfo->cliexedirectory);
    for(i = 0; defaultsearchpaths[i] != nullptr; i++)
    {
        nn_state_addmodulesearchpath(defaultsearchpaths[i]);
    }
}

void nn_module_setfilefield(NNObjModule* module)
{
    return;
    module->deftable.set(NNValue::fromObject(nn_string_intern("__file__")), NNValue::fromObject(nn_string_copyobject(module->physicalpath)));
}

void nn_module_destroy(NNObjModule* module)
{
    NNModLoaderFN asfn;
    module->deftable.deinit();
    /*
    nn_memory_free(module->name);
    nn_memory_free(module->physicalpath);
    */
    if(module->fnunloaderptr != nullptr && module->imported)
    {
        asfn = *(NNModLoaderFN*)module->fnunloaderptr;
        asfn();
    }
    if(module->handle != nullptr)
    {
        nn_import_closemodule(module->handle);
    }
}

NNObjModule* nn_import_loadmodulescript(NNObjModule* intomodule, NNObjString* modulename)
{
    size_t fsz;
    char* source;
    char* physpath;
    NNBlob blob;
    NNValue retv;
    NNValue callable;
    NNProperty* field;
    NNObjString* os;
    NNObjModule* module;
    NNObjFunction* closure;
    NNObjFunction* function;
    (void)os;
    (void)intomodule;
    auto gcs = SharedState::get();
    field = gcs->m_openedmodules.getfieldbyostr(modulename);
    if(field != nullptr)
    {
        return field->value.asModule();
    }
    physpath = nn_import_resolvepath(nn_string_getdata(modulename), nn_string_getdata(intomodule->physicalpath), nullptr, false);
    if(physpath == nullptr)
    {
        nn_except_throwclass(gcs->m_exceptions.importerror, "module not found: '%s'", nn_string_getdata(modulename));
        return nullptr;
    }
    fprintf(stderr, "loading module from '%s'\n", physpath);
    source = nn_util_filereadfile(physpath, &fsz, false, 0);
    if(source == nullptr)
    {
        nn_except_throwclass(gcs->m_exceptions.importerror, "could not read import file %s", physpath);
        return nullptr;
    }
    NNBlob::init(&blob);
    module = nn_module_make(nn_string_getdata(modulename), physpath, true, true);
    nn_memory_free(physpath);
    function = nn_astutil_compilesource(module, source, &blob, true, false);
    nn_memory_free(source);
    closure = nn_object_makefuncclosure(function, NNValue::makeNull());
    callable = NNValue::fromObject(closure);
    nn_nestcall_prepare(callable, NNValue::makeNull(), nullptr, 0);
    if(!nn_nestcall_callfunction(callable, NNValue::makeNull(), nullptr, 0, &retv, false))
    {
        NNBlob::destroy(&blob);
        nn_except_throwclass(gcs->m_exceptions.importerror, "failed to call compiled import closure");
        return nullptr;
    }
    NNBlob::destroy(&blob);
    return module;
}

char* nn_import_resolvepath(const char* modulename, const char* currentfile, char* rootfile, bool isrelative)
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
    NNObjString* pitem;
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
        nn_strbuf_appendstrn(pathbuf, nn_string_getdata(pitem), nn_string_getlength(pitem));
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

bool nn_import_loadnativemodule(NNModInitFN init_fn, char* importname, const char* source, void* dlw)
{
    size_t j;
    size_t k;
    size_t slen;
    NNValue v;
    NNValue fieldname;
    NNValue funcname;
    NNValue funcrealvalue;
    DefExport::Function func;
    DefExport::Field field;
    DefExport* defmod;
    NNObjModule* targetmod;
    DefExport::Class klassreg;
    NNObjString* classname;
    NNObjFunction* native;
    NNObjClass* klass;
    auto gcs = SharedState::get();
    defmod = init_fn();
    if(defmod != nullptr)
    {
        targetmod = (NNObjModule*)nn_gcmem_protect((NNObject*)nn_module_make((char*)defmod->name, source, false, true));
        targetmod->fnpreloaderptr = (void*)defmod->fnpreloaderfunc;
        targetmod->fnunloaderptr = (void*)defmod->fnunloaderfunc;
        if(defmod->definedfields != nullptr)
        {
            for(j = 0; defmod->definedfields[j].name != nullptr; j++)
            {
                field = defmod->definedfields[j];
                fieldname = NNValue::fromObject(nn_gcmem_protect((NNObject*)nn_string_copycstr(field.name)));
                v = field.fieldvalfn(NNFuncContext{NNValue::makeNull(), nullptr, 0});
                gcs->stackPush(v);
                targetmod->deftable.set(fieldname, v);
                gcs->stackPop();
            }
        }
        if(defmod->definedfunctions != nullptr)
        {
            for(j = 0; defmod->definedfunctions[j].name != nullptr; j++)
            {
                func = defmod->definedfunctions[j];
                funcname = NNValue::fromObject(nn_gcmem_protect((NNObject*)nn_string_copycstr(func.name)));
                funcrealvalue = NNValue::fromObject(nn_gcmem_protect((NNObject*)nn_object_makefuncnative(func.function, func.name, nullptr)));
                gcs->stackPush(funcrealvalue);
                targetmod->deftable.set(funcname, funcrealvalue);
                gcs->stackPop();
            }
        }
        if(defmod->definedclasses != nullptr)
        {
            for(j = 0; ((defmod->definedclasses[j].name != nullptr) && (defmod->definedclasses[j].defpubfunctions != nullptr)); j++)
            {
                klassreg = defmod->definedclasses[j];
                classname = (NNObjString*)nn_gcmem_protect((NNObject*)nn_string_copycstr(klassreg.name));
                klass = (NNObjClass*)nn_gcmem_protect((NNObject*)nn_object_makeclass(classname, gcs->m_classprimobject));
                if(klassreg.defpubfunctions != nullptr)
                {
                    for(k = 0; klassreg.defpubfunctions[k].name != nullptr; k++)
                    {
                        func = klassreg.defpubfunctions[k];
                        slen = strlen(func.name);
                        funcname = NNValue::fromObject(nn_gcmem_protect((NNObject*)nn_string_copycstr(func.name)));
                        native = (NNObjFunction*)nn_gcmem_protect((NNObject*)nn_object_makefuncnative(func.function, func.name, nullptr));
                        if(func.isstatic)
                        {
                            native->contexttype = NNObjFunction::CTXTYPE_STATIC;
                        }
                        else if(slen > 0 && func.name[0] == '_')
                        {
                            native->contexttype = NNObjFunction::CTXTYPE_PRIVATE;
                        }
                        if(strncmp(func.name, "constructor", slen) == 0)
                        {
                            klass->constructor = NNValue::fromObject(native);
                        }
                        else
                        {
                            klass->instmethods.set(funcname, NNValue::fromObject(native));
                        }
                    }
                }
                if(klassreg.defpubfields != nullptr)
                {
                    k = 0;
                    while(true)
                    {
                        if(klassreg.defpubfields[k].name == nullptr)
                        {
                            break;
                        }
                        field = klassreg.defpubfields[k];
                        if(field.name != nullptr)
                        {
                            nn_class_defcallablefield(klass, nn_string_copycstr(field.name), field.fieldvalfn);
                        }
                        k++;
                    }
                }
                targetmod->deftable.set(NNValue::fromObject(classname), NNValue::fromObject(klass));
            }
        }
        if(dlw != nullptr)
        {
            targetmod->handle = dlw;
        }
        nn_import_addnativemodule(targetmod, nn_string_getdata(targetmod->name));
        SharedState::clearGCProtect();
        return true;
    }
    else
    {
        nn_state_warn("Error loading module: %s\n", importname);
    }
    return false;
}

void nn_import_addnativemodule(NNObjModule* module, const char* as)
{
    NNValue name;
    auto gcs = SharedState::get();
    if(as != nullptr)
    {
        module->name = nn_string_copycstr(as);
    }
    name = NNValue::fromObject(nn_string_copyobject(module->name));
    gcs->stackPush(name);
    gcs->stackPush(NNValue::fromObject(module));
    gcs->m_openedmodules.set(name, NNValue::fromObject(module));
    gcs->stackPop(2);
}

void nn_import_closemodule(void* hnd)
{
    (void)hnd;
}

static NNValue nn_objfnnumber_tohexstring(const NNFuncContext& scfn)
{
    return NNValue::fromObject(nn_util_numbertohexstring(scfn.thisval.asNumber(), false));
}

static NNValue nn_objfnmath_hypot(const NNFuncContext& scfn)
{
    return NNValue::makeNumber(hypot(scfn.argv[0].asNumber(), scfn.argv[1].asNumber()));
}

static NNValue nn_objfnmath_abs(const NNFuncContext& scfn)
{
    return NNValue::makeNumber(fabs(scfn.argv[0].asNumber()));
}

static NNValue nn_objfnmath_round(const NNFuncContext& scfn)
{
    return NNValue::makeNumber(round(scfn.argv[0].asNumber()));
}

static NNValue nn_objfnmath_sqrt(const NNFuncContext& scfn)
{
    return NNValue::makeNumber(sqrt(scfn.argv[0].asNumber()));
}

static NNValue nn_objfnmath_ceil(const NNFuncContext& scfn)
{
    return NNValue::makeNumber(ceil(scfn.argv[0].asNumber()));
}

static NNValue nn_objfnmath_floor(const NNFuncContext& scfn)
{
    return NNValue::makeNumber(floor(scfn.argv[0].asNumber()));
}

static NNValue nn_objfnmath_min(const NNFuncContext& scfn)
{
    double b;
    double x;
    double y;
    x = scfn.argv[0].asNumber();
    y = scfn.argv[1].asNumber();
    b = (x < y) ? x : y;
    return NNValue::makeNumber(b);
}

static NNValue nn_objfnnumber_tobinstring(const NNFuncContext& scfn)
{
    return NNValue::fromObject(nn_util_numbertobinstring(scfn.thisval.asNumber()));
}

static NNValue nn_objfnnumber_tooctstring(const NNFuncContext& scfn)
{
    return NNValue::fromObject(nn_util_numbertooctstring(scfn.thisval.asNumber(), false));
}

static NNValue nn_objfnnumber_constructor(const NNFuncContext& scfn)
{
    NNValue val;
    NNValue rtval;
    NNObjString* os;
    if(scfn.argc == 0)
    {
        return NNValue::makeNumber(0);
    }
    val = scfn.argv[0];
    if(val.isNumber())
    {
        return val;
    }
    if(val.isNull())
    {
        return NNValue::makeNumber(0);
    }
    if(!val.isString())
    {
        NEON_RETURNERROR(scfn, "Number() expects no arguments, or number, or string");
    }
    NNAstToken tok;
    NNAstLexer lex;
    os = val.asString();
    lex.init(nn_string_getdata(os));
    tok = lex.scannumber();
    rtval = NNAstParser::utilConvertNumberString(tok.type, tok.start);
    return rtval;
}

void nn_state_installobjectnumber()
{
    static NNConstClassMethodItem numbermethods[] = {
        { "toHexString", nn_objfnnumber_tohexstring },
        { "toOctString", nn_objfnnumber_tooctstring },
        { "toBinString", nn_objfnnumber_tobinstring },
        { nullptr, nullptr },
    };
    auto gcs = SharedState::get();
    nn_class_defnativeconstructor(gcs->m_classprimnumber, nn_objfnnumber_constructor);
    nn_state_installmethods(gcs->m_classprimnumber, numbermethods);
}

void nn_state_installmodmath()
{
    NNObjClass* klass;
    auto gcs = SharedState::get();
    klass = nn_util_makeclass("Math", gcs->m_classprimobject);
    nn_class_defstaticnativemethod(klass, nn_string_intern("hypot"), nn_objfnmath_hypot);
    nn_class_defstaticnativemethod(klass, nn_string_intern("abs"), nn_objfnmath_abs);
    nn_class_defstaticnativemethod(klass, nn_string_intern("round"), nn_objfnmath_round);
    nn_class_defstaticnativemethod(klass, nn_string_intern("sqrt"), nn_objfnmath_sqrt);
    nn_class_defstaticnativemethod(klass, nn_string_intern("ceil"), nn_objfnmath_ceil);
    nn_class_defstaticnativemethod(klass, nn_string_intern("floor"), nn_objfnmath_floor);
    nn_class_defstaticnativemethod(klass, nn_string_intern("min"), nn_objfnmath_min);
}

static NNValue nn_objfnobject_dumpself(const NNFuncContext& scfn)
{
    NNValue v;
    NNIOStream pr;
    NNObjString* os;
    v = scfn.thisval;
    NNIOStream::makeStackString(&pr);
    nn_iostream_printvalue(&pr, v, true, false);
    os = pr.takeString();
    NNIOStream::destroy(&pr);
    return NNValue::fromObject(os);
}

static NNValue nn_objfnobject_tostring(const NNFuncContext& scfn)
{
    NNValue v;
    NNIOStream pr;
    NNObjString* os;
    v = scfn.thisval;
    NNIOStream::makeStackString(&pr);
    nn_iostream_printvalue(&pr, v, false, true);
    os = pr.takeString();
    NNIOStream::destroy(&pr);
    return NNValue::fromObject(os);
}

static NNValue nn_objfnobject_typename(const NNFuncContext& scfn)
{
    NNValue v;
    NNObjString* os;
    v = scfn.argv[0];
    os = nn_string_copycstr(nn_value_typename(v, false));
    return NNValue::fromObject(os);
}

static NNValue nn_objfnobject_getselfinstance(const NNFuncContext& scfn)
{
    return scfn.thisval;
}

static NNValue nn_objfnobject_getselfclass(const NNFuncContext& scfn)
{
    #if 0
        nn_vmdebug_printvalue(scfn.thisval, "<object>.class:scfn.thisval=");
#endif
    if(scfn.thisval.isInstance())
    {
        return NNValue::fromObject(scfn.thisval.asInstance()->klass);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnobject_isstring(const NNFuncContext& scfn)
{
    NNValue v;
    v = scfn.thisval;
    return NNValue::makeBool(v.isString());
}

static NNValue nn_objfnobject_isarray(const NNFuncContext& scfn)
{
    NNValue v;
    v = scfn.thisval;
    return NNValue::makeBool(v.isArray());
}

static NNValue nn_objfnobject_isa(const NNFuncContext& scfn)
{
    NNValue v;
    NNValue otherclval;
    NNObjClass* oclass;
    NNObjClass* selfclass;
    v = scfn.thisval;
    otherclval = scfn.argv[0];
    if(otherclval.isClass())
    {
        oclass = otherclval.asClass();
        selfclass = nn_value_getclassfor(v);
        if(selfclass != nullptr)
        {
            return NNValue::makeBool(nn_util_isinstanceof(selfclass, oclass));
        }
    }
    return NNValue::makeBool(false);
}

static NNValue nn_objfnobject_iscallable(const NNFuncContext& scfn)
{
    NNValue selfval;
    selfval = scfn.thisval;
    return (NNValue::makeBool(selfval.isClass() || selfval.isFuncscript() || selfval.isFuncclosure() || selfval.isFuncbound() || selfval.isFuncnative()));
}

static NNValue nn_objfnobject_isbool(const NNFuncContext& scfn)
{
    NNValue selfval;
    selfval = scfn.thisval;
    return NNValue::makeBool(selfval.isBool());
}

static NNValue nn_objfnobject_isnumber(const NNFuncContext& scfn)
{
    NNValue selfval;
    selfval = scfn.thisval;
    return NNValue::makeBool(selfval.isNumber());
}

static NNValue nn_objfnobject_isint(const NNFuncContext& scfn)
{
    NNValue selfval;
    selfval = scfn.thisval;
    return NNValue::makeBool(selfval.isNumber() && (((int)selfval.asNumber()) == selfval.asNumber()));
}

static NNValue nn_objfnobject_isdict(const NNFuncContext& scfn)
{
    NNValue selfval;
    selfval = scfn.thisval;
    return NNValue::makeBool(selfval.isDict());
}

static NNValue nn_objfnobject_isobject(const NNFuncContext& scfn)
{
    NNValue selfval;
    selfval = scfn.thisval;
    return NNValue::makeBool(selfval.isObject());
}

static NNValue nn_objfnobject_isfunction(const NNFuncContext& scfn)
{
    NNValue selfval;
    selfval = scfn.thisval;
    return NNValue::makeBool(selfval.isFuncscript() || selfval.isFuncclosure() || selfval.isFuncbound() || selfval.isFuncnative());
}

static NNValue nn_objfnobject_isiterable(const NNFuncContext& scfn)
{
    bool isiterable;
    NNValue dummy;
    NNObjClass* klass;
    NNValue selfval;
    selfval = scfn.thisval;
    isiterable = selfval.isArray() || selfval.isDict() || selfval.isString();
    if(!isiterable && selfval.isInstance())
    {
        klass = selfval.asInstance()->klass;
        isiterable = klass->instmethods.get(NNValue::fromObject(nn_string_intern("@iter")), &dummy) && klass->instmethods.get(NNValue::fromObject(nn_string_intern("@itern")), &dummy);
    }
    return NNValue::makeBool(isiterable);
}

static NNValue nn_objfnobject_isclass(const NNFuncContext& scfn)
{
    NNValue selfval;
    selfval = scfn.thisval;
    return NNValue::makeBool(selfval.isClass());
}

static NNValue nn_objfnobject_isfile(const NNFuncContext& scfn)
{
    NNValue selfval;
    selfval = scfn.thisval;
    return NNValue::makeBool(selfval.isFile());
}

static NNValue nn_objfnobject_isinstance(const NNFuncContext& scfn)
{
    NNValue selfval;
    selfval = scfn.thisval;
    return NNValue::makeBool(selfval.isInstance());
}

static NNValue nn_objfnclass_getselfname(const NNFuncContext& scfn)
{
    NNValue selfval;
    NNObjClass* klass;
    selfval = scfn.thisval;
    klass = selfval.asClass();
    return NNValue::fromObject(klass->name);
}

void nn_state_installobjectobject()
{
    static NNConstClassMethodItem objectmethods[] = {
        { "dump", nn_objfnobject_dumpself }, { "isa", nn_objfnobject_isa }, { "toString", nn_objfnobject_tostring }, { "isArray", nn_objfnobject_isarray }, { "isString", nn_objfnobject_isstring }, { "isCallable", nn_objfnobject_iscallable }, { "isBool", nn_objfnobject_isbool }, { "isNumber", nn_objfnobject_isnumber }, { "isInt", nn_objfnobject_isint }, { "isDict", nn_objfnobject_isdict }, { "isObject", nn_objfnobject_isobject }, { "isFunction", nn_objfnobject_isfunction }, { "isIterable", nn_objfnobject_isiterable }, { "isClass", nn_objfnobject_isclass }, { "isFile", nn_objfnobject_isfile }, { "isInstance", nn_objfnobject_isinstance }, { nullptr, nullptr },
    };
    auto gcs = SharedState::get();
    // nn_class_defcallablefield(gcs->m_classprimobject, nn_string_intern("class"), nn_objfnobject_getselfclass);
    nn_class_defstaticnativemethod(gcs->m_classprimobject, nn_string_intern("typename"), nn_objfnobject_typename);
    nn_class_defstaticcallablefield(gcs->m_classprimobject, nn_string_intern("prototype"), nn_objfnobject_getselfinstance);
    nn_state_installmethods(gcs->m_classprimobject, objectmethods);
    {
        nn_class_defstaticcallablefield(gcs->m_classprimclass, nn_string_intern("name"), nn_objfnclass_getselfname);
    }
}

static NNValue nn_objfnprocess_exedirectory(const NNFuncContext& scfn)
{
    (void)scfn;
    auto gcs = SharedState::get();
    if(gcs->m_processinfo->cliexedirectory != nullptr)
    {
        return NNValue::fromObject(gcs->m_processinfo->cliexedirectory);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnprocess_scriptfile(const NNFuncContext& scfn)
{
    (void)scfn;
    auto gcs = SharedState::get();
    if(gcs->m_processinfo->cliscriptfile != nullptr)
    {
        return NNValue::fromObject(gcs->m_processinfo->cliscriptfile);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnprocess_scriptdirectory(const NNFuncContext& scfn)
{
    (void)scfn;
    auto gcs = SharedState::get();
    if(gcs->m_processinfo->cliscriptdirectory != nullptr)
    {
        return NNValue::fromObject(gcs->m_processinfo->cliscriptdirectory);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnprocess_exit(const NNFuncContext& scfn)
{
    int rc;
    rc = 0;
    if(scfn.argc > 0)
    {
        rc = scfn.argv[0].asNumber();
    }
    exit(rc);
    return NNValue::makeNull();
}

static NNValue nn_objfnprocess_kill(const NNFuncContext& scfn)
{
    int pid;
    int code;
    pid = scfn.argv[0].asNumber();
    code = scfn.argv[1].asNumber();
    osfn_kill(pid, code);
    return NNValue::makeNull();
}

void nn_state_installobjectprocess()
{
    NNObjClass* klass;
    auto gcs = SharedState::get();
    klass = gcs->m_classprimprocess;
    nn_class_setstaticproperty(klass, nn_string_copycstr("directory"), NNValue::fromObject(gcs->m_processinfo->cliexedirectory));
    nn_class_setstaticproperty(klass, nn_string_copycstr("env"), NNValue::fromObject(gcs->m_envdict));
    nn_class_setstaticproperty(klass, nn_string_copycstr("stdin"), NNValue::fromObject(gcs->m_processinfo->filestdin));
    nn_class_setstaticproperty(klass, nn_string_copycstr("stdout"), NNValue::fromObject(gcs->m_processinfo->filestdout));
    nn_class_setstaticproperty(klass, nn_string_copycstr("stderr"), NNValue::fromObject(gcs->m_processinfo->filestderr));
    nn_class_setstaticproperty(klass, nn_string_copycstr("pid"), NNValue::makeNumber(gcs->m_processinfo->cliprocessid));
    nn_class_defstaticnativemethod(klass, nn_string_copycstr("kill"), nn_objfnprocess_kill);
    nn_class_defstaticnativemethod(klass, nn_string_copycstr("exit"), nn_objfnprocess_exit);
    nn_class_defstaticnativemethod(klass, nn_string_copycstr("exedirectory"), nn_objfnprocess_exedirectory);
    nn_class_defstaticnativemethod(klass, nn_string_copycstr("scriptdirectory"), nn_objfnprocess_scriptdirectory);
    nn_class_defstaticnativemethod(klass, nn_string_copycstr("script"), nn_objfnprocess_scriptfile);
}

static NNValue nn_objfnrange_lower(const NNFuncContext& scfn)
{
    NNArgCheck check;
    nn_argcheck_init(&check, "lower", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return NNValue::makeNumber(scfn.thisval.asRange()->lower);
}

static NNValue nn_objfnrange_upper(const NNFuncContext& scfn)
{
    NNArgCheck check;
    nn_argcheck_init(&check, "upper", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return NNValue::makeNumber(scfn.thisval.asRange()->upper);
}

static NNValue nn_objfnrange_range(const NNFuncContext& scfn)
{
    NNArgCheck check;
    nn_argcheck_init(&check, "range", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return NNValue::makeNumber(scfn.thisval.asRange()->range);
}

static NNValue nn_objfnrange_iter(const NNFuncContext& scfn)
{
    int val;
    int index;
    NNObjRange* range;
    NNArgCheck check;
    nn_argcheck_init(&check, "iter", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    range = scfn.thisval.asRange();
    index = scfn.argv[0].asNumber();
    if(index >= 0 && index < range->range)
    {
        if(index == 0)
        {
            return NNValue::makeNumber(range->lower);
        }
        if(range->lower > range->upper)
        {
            val = --range->lower;
        }
        else
        {
            val = ++range->lower;
        }
        return NNValue::makeNumber(val);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnrange_itern(const NNFuncContext& scfn)
{
    int index;
    NNObjRange* range;
    NNArgCheck check;
    nn_argcheck_init(&check, "itern", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    range = scfn.thisval.asRange();
    if(scfn.argv[0].isNull())
    {
        if(range->range == 0)
        {
            return NNValue::makeNull();
        }
        return NNValue::makeNumber(0);
    }
    if(!scfn.argv[0].isNumber())
    {
        NEON_RETURNERROR(scfn, "ranges are numerically indexed");
    }
    index = (int)scfn.argv[0].asNumber() + 1;
    if(index < range->range)
    {
        return NNValue::makeNumber(index);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnrange_expand(const NNFuncContext& scfn)
{
    int i;
    NNValue val;
    NNObjRange* range;
    NNObjArray* oa;
    range = scfn.thisval.asRange();
    oa = nn_object_makearray();
    for(i = 0; i < range->range; i++)
    {
        val = NNValue::makeNumber(i);
        nn_array_push(oa, val);
    }
    return NNValue::fromObject(oa);
}

static NNValue nn_objfnrange_constructor(const NNFuncContext& scfn)
{
    int a;
    int b;
    NNObjRange* orng;
    a = scfn.argv[0].asNumber();
    b = scfn.argv[1].asNumber();
    orng = nn_object_makerange(a, b);
    return NNValue::fromObject(orng);
}

void nn_state_installobjectrange()
{
    static NNConstClassMethodItem rangemethods[] = {
        { "lower", nn_objfnrange_lower }, { "upper", nn_objfnrange_upper }, { "range", nn_objfnrange_range }, { "expand", nn_objfnrange_expand }, { "toArray", nn_objfnrange_expand }, { "@iter", nn_objfnrange_iter }, { "@itern", nn_objfnrange_itern }, { nullptr, nullptr },
    };
    auto gcs = SharedState::get();
    nn_class_defnativeconstructor(gcs->m_classprimrange, nn_objfnrange_constructor);
    nn_state_installmethods(gcs->m_classprimrange, rangemethods);
}

static NNValue nn_objfnstring_utf8numbytes(const NNFuncContext& scfn)
{
    int incode;
    int res;
    NNArgCheck check;
    nn_argcheck_init(&check, "utf8NumBytes", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    incode = scfn.argv[0].asNumber();
    res = nn_util_utf8numbytes(incode);
    return NNValue::makeNumber(res);
}

static NNValue nn_objfnstring_utf8decode(const NNFuncContext& scfn)
{
    int res;
    NNObjString* instr;
    NNArgCheck check;
    nn_argcheck_init(&check, "utf8Decode", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    instr = scfn.argv[0].asString();
    res = nn_util_utf8decode((const uint8_t*)nn_string_getdata(instr), nn_string_getlength(instr));
    return NNValue::makeNumber(res);
}

static NNValue nn_objfnstring_utf8encode(const NNFuncContext& scfn)
{
    int incode;
    size_t len;
    NNObjString* res;
    char* buf;
    NNArgCheck check;
    nn_argcheck_init(&check, "utf8Encode", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    incode = scfn.argv[0].asNumber();
    buf = nn_util_utf8encode(incode, &len);
    res = nn_string_takelen(buf, len);
    return NNValue::fromObject(res);
}

static NNValue nn_objfnutilstring_utf8chars(const NNFuncContext& scfn, bool onlycodepoint)
{
    int cp;
    bool havemax;
    size_t counter;
    size_t maxamount;
    const char* cstr;
    NNObjArray* res;
    NNObjString* os;
    NNObjString* instr;
    utf8iterator_t iter;
    havemax = false;
    instr = scfn.thisval.asString();
    if(scfn.argc > 0)
    {
        havemax = true;
        maxamount = scfn.argv[0].asNumber();
    }
    res = nn_array_make();
    nn_utf8iter_init(&iter, nn_string_getdata(instr), nn_string_getlength(instr));
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
            nn_array_push(res, NNValue::makeNumber(cp));
        }
        else
        {
            os = nn_string_copylen(cstr, iter.charsize);
            nn_array_push(res, NNValue::fromObject(os));
        }
    }
finalize:
    return NNValue::fromObject(res);
}

static NNValue nn_objfnstring_utf8chars(const NNFuncContext& scfn)
{
    return nn_objfnutilstring_utf8chars(scfn, false);
}

static NNValue nn_objfnstring_utf8codepoints(const NNFuncContext& scfn)
{
    return nn_objfnutilstring_utf8chars(scfn, true);
}

static NNValue nn_objfnstring_fromcharcode(const NNFuncContext& scfn)
{
    char ch;
    NNObjString* os;
    NNArgCheck check;
    nn_argcheck_init(&check, "fromCharCode", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    ch = scfn.argv[0].asNumber();
    os = nn_string_copylen(&ch, 1);
    return NNValue::fromObject(os);
}

static NNValue nn_objfnstring_constructor(const NNFuncContext& scfn)
{
    NNObjString* os;
    NNArgCheck check;
    nn_argcheck_init(&check, "constructor", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    os = nn_string_internlen("", 0);
    return NNValue::fromObject(os);
}

static NNValue nn_objfnstring_length(const NNFuncContext& scfn)
{
    NNArgCheck check;
    NNObjString* selfstr;
    nn_argcheck_init(&check, "length", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = scfn.thisval.asString();
    return NNValue::makeNumber(nn_string_getlength(selfstr));
}

static NNValue nn_string_fromrange(const char* buf, int len)
{
    NNObjString* str;
    if(len <= 0)
    {
        return NNValue::fromObject(nn_string_internlen("", 0));
    }
    str = nn_string_internlen("", 0);
    nn_string_appendstringlen(str, buf, len);
    return NNValue::fromObject(str);
}

NNObjString* nn_string_substring(NNObjString* selfstr, size_t start, size_t end, bool likejs)
{
    size_t asz;
    size_t len;
    size_t tmp;
    size_t maxlen;
    char* raw;
    (void)likejs;
    maxlen = nn_string_getlength(selfstr);
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
    memcpy(raw, nn_string_getdata(selfstr) + start, len);
    return nn_string_takelen(raw, len);
}

static NNValue nn_objfnstring_substring(const NNFuncContext& scfn)
{
    size_t end;
    size_t start;
    size_t maxlen;
    NNObjString* nos;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(&check, "substring", scfn);
    selfstr = scfn.thisval.asString();
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    maxlen = nn_string_getlength(selfstr);
    end = maxlen;
    start = scfn.argv[0].asNumber();
    if(scfn.argc > 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        end = scfn.argv[1].asNumber();
    }
    nos = nn_string_substring(selfstr, start, end, true);
    return NNValue::fromObject(nos);
}

static NNValue nn_objfnstring_charcodeat(const NNFuncContext& scfn)
{
    int ch;
    int idx;
    int selflen;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(&check, "charCodeAt", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    selfstr = scfn.thisval.asString();
    idx = scfn.argv[0].asNumber();
    selflen = (int)nn_string_getlength(selfstr);
    if((idx < 0) || (idx >= selflen))
    {
        ch = -1;
    }
    else
    {
        ch = nn_string_get(selfstr, idx);
    }
    return NNValue::makeNumber(ch);
}

static NNValue nn_objfnstring_charat(const NNFuncContext& scfn)
{
    char ch;
    int idx;
    int selflen;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(&check, "charAt", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    selfstr = scfn.thisval.asString();
    idx = scfn.argv[0].asNumber();
    selflen = (int)nn_string_getlength(selfstr);
    if((idx < 0) || (idx >= selflen))
    {
        return NNValue::fromObject(nn_string_internlen("", 0));
    }
    else
    {
        ch = nn_string_get(selfstr, idx);
    }
    return NNValue::fromObject(nn_string_copylen(&ch, 1));
}

static NNValue nn_objfnstring_upper(const NNFuncContext& scfn)
{
    size_t slen;
    char* string;
    NNObjString* str;
    NNArgCheck check;
    nn_argcheck_init(&check, "upper", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    str = scfn.thisval.asString();
    slen = nn_string_getlength(str);
    string = nn_util_strtoupper(nn_string_mutdata(str), slen);
    return NNValue::fromObject(nn_string_copylen(string, slen));
}

static NNValue nn_objfnstring_lower(const NNFuncContext& scfn)
{
    size_t slen;
    char* string;
    NNObjString* str;
    NNArgCheck check;
    nn_argcheck_init(&check, "lower", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    str = scfn.thisval.asString();
    slen = nn_string_getlength(str);
    string = nn_util_strtolower(nn_string_mutdata(str), slen);
    return NNValue::fromObject(nn_string_copylen(string, slen));
}

static NNValue nn_objfnstring_isalpha(const NNFuncContext& scfn)
{
    size_t i;
    size_t len;
    NNArgCheck check;
    NNObjString* selfstr;
    nn_argcheck_init(&check, "isAlpha", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = scfn.thisval.asString();
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!isalpha((unsigned char)nn_string_get(selfstr, i)))
        {
            return NNValue::makeBool(false);
        }
    }
    return NNValue::makeBool(nn_string_getlength(selfstr) != 0);
}

static NNValue nn_objfnstring_isalnum(const NNFuncContext& scfn)
{
    size_t i;
    size_t len;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(&check, "isAlnum", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = scfn.thisval.asString();
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!isalnum((unsigned char)nn_string_get(selfstr, i)))
        {
            return NNValue::makeBool(false);
        }
    }
    return NNValue::makeBool(nn_string_getlength(selfstr) != 0);
}

static NNValue nn_objfnstring_isfloat(const NNFuncContext& scfn)
{
    double f;
    char* p;
    NNObjString* selfstr;
    NNArgCheck check;
    (void)f;
    nn_argcheck_init(&check, "isFloat", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = scfn.thisval.asString();
    errno = 0;
    if(nn_string_getlength(selfstr) == 0)
    {
        return NNValue::makeBool(false);
    }
    f = strtod(nn_string_getdata(selfstr), &p);
    if(errno)
    {
        return NNValue::makeBool(false);
    }
    else
    {
        if(*p == 0)
        {
            return NNValue::makeBool(true);
        }
    }
    return NNValue::makeBool(false);
}

static NNValue nn_objfnstring_isnumber(const NNFuncContext& scfn)
{
    size_t i;
    size_t len;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(&check, "isNumber", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = scfn.thisval.asString();
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!isdigit((unsigned char)nn_string_get(selfstr, i)))
        {
            return NNValue::makeBool(false);
        }
    }
    return NNValue::makeBool(nn_string_getlength(selfstr) != 0);
}

static NNValue nn_objfnstring_islower(const NNFuncContext& scfn)
{
    size_t i;
    size_t len;
    bool alphafound;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(&check, "isLower", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = scfn.thisval.asString();
    alphafound = false;
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!alphafound && !isdigit(nn_string_get(selfstr, 0)))
        {
            alphafound = true;
        }
        if(isupper(nn_string_get(selfstr, 0)))
        {
            return NNValue::makeBool(false);
        }
    }
    return NNValue::makeBool(alphafound);
}

static NNValue nn_objfnstring_isupper(const NNFuncContext& scfn)
{
    size_t i;
    size_t len;
    bool alphafound;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(&check, "isUpper", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = scfn.thisval.asString();
    alphafound = false;
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!alphafound && !isdigit(nn_string_get(selfstr, 0)))
        {
            alphafound = true;
        }
        if(islower(nn_string_get(selfstr, i)))
        {
            return NNValue::makeBool(false);
        }
    }
    return NNValue::makeBool(alphafound);
}

static NNValue nn_objfnstring_isspace(const NNFuncContext& scfn)
{
    size_t i;
    size_t len;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(&check, "isSpace", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = scfn.thisval.asString();
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!isspace((unsigned char)nn_string_get(selfstr, i)))
        {
            return NNValue::makeBool(false);
        }
    }
    return NNValue::makeBool(nn_string_getlength(selfstr) != 0);
}

static NNValue nn_objfnstring_trim(const NNFuncContext& scfn)
{
    char trimmer;
    char* end;
    char* string;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(&check, "trim", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(scfn.argc == 1)
    {
        trimmer = (char)nn_string_get(scfn.argv[0].asString(), 0);
    }
    selfstr = scfn.thisval.asString();
    string = nn_string_mutdata(selfstr);
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
        return NNValue::fromObject(nn_string_internlen("", 0));
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
    return NNValue::fromObject(nn_string_copycstr(string));
}

static NNValue nn_objfnstring_ltrim(const NNFuncContext& scfn)
{
    char trimmer;
    char* end;
    char* string;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(&check, "ltrim", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(scfn.argc == 1)
    {
        trimmer = (char)nn_string_get(scfn.argv[0].asString(), 0);
    }
    selfstr = scfn.thisval.asString();
    string = nn_string_mutdata(selfstr);
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
        return NNValue::fromObject(nn_string_internlen("", 0));
    }
    end = string + strlen(string) - 1;
    end[1] = '\0';
    return NNValue::fromObject(nn_string_copycstr(string));
}

static NNValue nn_objfnstring_rtrim(const NNFuncContext& scfn)
{
    char trimmer;
    char* end;
    char* string;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(&check, "rtrim", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(scfn.argc == 1)
    {
        trimmer = (char)nn_string_get(scfn.argv[0].asString(), 0);
    }
    selfstr = scfn.thisval.asString();
    string = nn_string_mutdata(selfstr);
    end = nullptr;
    /* All spaces? */
    if(*string == 0)
    {
        return NNValue::fromObject(nn_string_internlen("", 0));
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
    return NNValue::fromObject(nn_string_copycstr(string));
}

static NNValue nn_objfnstring_indexof(const NNFuncContext& scfn)
{
    int startindex;
    char* result;
    const char* haystack;
    NNObjString* string;
    NNObjString* needle;
    NNArgCheck check;
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
    if(nn_string_getlength(string) > 0 && nn_string_getlength(needle) > 0)
    {
        haystack = nn_string_getdata(string);
        result = (char*)strstr(haystack + startindex, nn_string_getdata(needle));
        if(result != nullptr)
        {
            return NNValue::makeNumber((int)(result - haystack));
        }
    }
    return NNValue::makeNumber(-1);
}

static NNValue nn_objfnstring_startswith(const NNFuncContext& scfn)
{
    NNObjString* substr;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(&check, "startsWith", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = scfn.thisval.asString();
    substr = scfn.argv[0].asString();
    if(nn_string_getlength(string) == 0 || nn_string_getlength(substr) == 0 || nn_string_getlength(substr) > nn_string_getlength(string))
    {
        return NNValue::makeBool(false);
    }
    return NNValue::makeBool(memcmp(nn_string_getdata(substr), nn_string_getdata(string), nn_string_getlength(substr)) == 0);
}

static NNValue nn_objfnstring_endswith(const NNFuncContext& scfn)
{
    int difference;
    NNObjString* substr;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(&check, "endsWith", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = scfn.thisval.asString();
    substr = scfn.argv[0].asString();
    if(nn_string_getlength(string) == 0 || nn_string_getlength(substr) == 0 || nn_string_getlength(substr) > nn_string_getlength(string))
    {
        return NNValue::makeBool(false);
    }
    difference = nn_string_getlength(string) - nn_string_getlength(substr);
    return NNValue::makeBool(memcmp(nn_string_getdata(substr), nn_string_getdata(string) + difference, nn_string_getlength(substr)) == 0);
}

static NNValue nn_util_stringregexmatch(NNObjString* string, NNObjString* pattern, bool capture)
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
    RegexToken tokens[matchMaxTokens + 1];
    RegexContext pctx;
    auto gcs = SharedState::get();
    memset(tokens, 0, (matchMaxTokens + 1) * sizeof(RegexToken));
    memset(caplengths, 0, (matchMaxCaptures + 1) * sizeof(int64_t));
    memset(capstarts, 0, (matchMaxCaptures + 1) * sizeof(int64_t));
    const char* strstart;
    NNObjString* rstr;
    NNObjArray* oa;
    NNObjDict* dm;
    restokens = matchMaxTokens;
    actualmaxcaptures = 0;
    mrx_context_initstack(&pctx, tokens, restokens);
    if(capture)
    {
        actualmaxcaptures = matchMaxCaptures;
    }
    prc = mrx_regex_parse(&pctx, nn_string_getdata(pattern), 0);
    if(prc == 0)
    {
        cpres = mrx_regex_match(&pctx, nn_string_getdata(string), 0, actualmaxcaptures, capstarts, caplengths);
        if(cpres > 0)
        {
            if(capture)
            {
                oa = nn_object_makearray();
                for(i = 0; i < cpres; i++)
                {
                    mtstart = capstarts[i];
                    mtlength = caplengths[i];
                    if(mtlength > 0)
                    {
                        strstart = &nn_string_getdata(string)[mtstart];
                        rstr = nn_string_copylen(strstart, mtlength);
                        dm = nn_object_makedict();
                        nn_dict_addentrycstr(dm, "string", NNValue::fromObject(rstr));
                        nn_dict_addentrycstr(dm, "start", NNValue::makeNumber(mtstart));
                        nn_dict_addentrycstr(dm, "length", NNValue::makeNumber(mtlength));
                        nn_array_push(oa, NNValue::fromObject(dm));
                    }
                }
                return NNValue::fromObject(oa);
            }
            else
            {
                return NNValue::makeBool(true);
            }
        }
    }
    else
    {
        nn_except_throwclass(gcs->m_exceptions.regexerror, pctx.errorbuf);
    }
    mrx_context_destroy(&pctx);
    if(capture)
    {
        return NNValue::makeNull();
    }
    return NNValue::makeBool(false);
}

static NNValue nn_objfnstring_matchcapture(const NNFuncContext& scfn)
{
    NNObjString* pattern;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(&check, "match", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = scfn.thisval.asString();
    pattern = scfn.argv[0].asString();
    return nn_util_stringregexmatch(string, pattern, true);
}

static NNValue nn_objfnstring_matchonly(const NNFuncContext& scfn)
{
    NNObjString* pattern;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(&check, "match", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = scfn.thisval.asString();
    pattern = scfn.argv[0].asString();
    return nn_util_stringregexmatch(string, pattern, false);
}

static NNValue nn_objfnstring_count(const NNFuncContext& scfn)
{
    int count;
    const char* tmp;
    NNObjString* substr;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(&check, "count", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = scfn.thisval.asString();
    substr = scfn.argv[0].asString();
    if(nn_string_getlength(substr) == 0 || nn_string_getlength(string) == 0)
    {
        return NNValue::makeNumber(0);
    }
    count = 0;
    tmp = nn_string_getdata(string);
    while((tmp = nn_util_utf8strstr(tmp, nn_string_getdata(substr))))
    {
        count++;
        tmp++;
    }
    return NNValue::makeNumber(count);
}

static NNValue nn_objfnstring_tonumber(const NNFuncContext& scfn)
{
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(&check, "toNumber", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = scfn.thisval.asString();
    return NNValue::makeNumber(strtod(nn_string_getdata(selfstr), nullptr));
}

static NNValue nn_objfnstring_isascii(const NNFuncContext& scfn)
{
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(&check, "isAscii", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    if(scfn.argc == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isbool);
    }
    string = scfn.thisval.asString();
    return NNValue::fromObject(string);
}

static NNValue nn_objfnstring_tolist(const NNFuncContext& scfn)
{
    size_t i;
    size_t end;
    size_t start;
    size_t length;
    NNObjArray* list;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(&check, "toList", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    string = scfn.thisval.asString();
    list = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    length = nn_string_getlength(string);
    if(length > 0)
    {
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            nn_array_push(list, NNValue::fromObject(nn_string_copylen(nn_string_getdata(string) + start, (int)(end - start))));
        }
    }
    return NNValue::fromObject(list);
}

static NNValue nn_objfnstring_tobytes(const NNFuncContext& scfn)
{
    size_t i;
    size_t length;
    NNObjArray* list;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(&check, "toBytes", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    string = scfn.thisval.asString();
    list = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    length = nn_string_getlength(string);
    if(length > 0)
    {
        for(i = 0; i < length; i++)
        {
            nn_array_push(list, NNValue::makeNumber(nn_string_get(string, i)));
        }
    }
    return NNValue::fromObject(list);
}

static NNValue nn_objfnstring_lpad(const NNFuncContext& scfn)
{
    size_t i;
    size_t width;
    size_t fillsize;
    size_t finalsize;
    char fillchar;
    char* str;
    char* fill;
    NNObjString* ofillstr;
    NNObjString* result;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(&check, "lpad", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    string = scfn.thisval.asString();
    width = scfn.argv[0].asNumber();
    fillchar = ' ';
    if(scfn.argc == 2)
    {
        ofillstr = scfn.argv[1].asString();
        fillchar = nn_string_get(ofillstr, 0);
    }
    if(width <= nn_string_getlength(string))
    {
        return scfn.thisval;
    }
    fillsize = width - nn_string_getlength(string);
    fill = (char*)nn_memory_malloc(sizeof(char) * ((size_t)fillsize + 1));
    finalsize = nn_string_getlength(string) + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)nn_memory_malloc(sizeof(char) * ((size_t)finalsize + 1));
    memcpy(str, fill, fillsize);
    memcpy(str + fillsize, nn_string_getdata(string), nn_string_getlength(string));
    str[finalsize] = '\0';
    nn_memory_free(fill);
    result = nn_string_takelen(str, finalsize);
    nn_string_setlength(result, finalsize);
    return NNValue::fromObject(result);
}

static NNValue nn_objfnstring_rpad(const NNFuncContext& scfn)
{
    size_t i;
    size_t width;
    size_t fillsize;
    size_t finalsize;
    char fillchar;
    char* str;
    char* fill;
    NNObjString* ofillstr;
    NNObjString* string;
    NNObjString* result;
    NNArgCheck check;
    nn_argcheck_init(&check, "rpad", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    string = scfn.thisval.asString();
    width = scfn.argv[0].asNumber();
    fillchar = ' ';
    if(scfn.argc == 2)
    {
        ofillstr = scfn.argv[1].asString();
        fillchar = nn_string_get(ofillstr, 0);
    }
    if(width <= nn_string_getlength(string))
    {
        return scfn.thisval;
    }
    fillsize = width - nn_string_getlength(string);
    fill = (char*)nn_memory_malloc(sizeof(char) * ((size_t)fillsize + 1));
    finalsize = nn_string_getlength(string) + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)nn_memory_malloc(sizeof(char) * ((size_t)finalsize + 1));
    memcpy(str, nn_string_getdata(string), nn_string_getlength(string));
    memcpy(str + nn_string_getlength(string), fill, fillsize);
    str[finalsize] = '\0';
    nn_memory_free(fill);
    result = nn_string_takelen(str, finalsize);
    nn_string_setlength(result, finalsize);
    return NNValue::fromObject(result);
}

static NNValue nn_objfnstring_split(const NNFuncContext& scfn)
{
    size_t i;
    size_t end;
    size_t start;
    size_t length;
    NNObjArray* list;
    NNObjString* string;
    NNObjString* delimeter;
    NNArgCheck check;
    nn_argcheck_init(&check, "split", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = scfn.thisval.asString();
    delimeter = scfn.argv[0].asString();
    /* empty string matches empty string to empty list */
    if(((nn_string_getlength(string) == 0) && (nn_string_getlength(delimeter) == 0)) || (nn_string_getlength(string) == 0) || (nn_string_getlength(delimeter) == 0))
    {
        return NNValue::fromObject(nn_object_makearray());
    }
    list = (NNObjArray*)nn_gcmem_protect((NNObject*)nn_object_makearray());
    if(nn_string_getlength(delimeter) > 0)
    {
        start = 0;
        for(i = 0; i <= nn_string_getlength(string); i++)
        {
            /* match found. */
            if(memcmp(nn_string_getdata(string) + i, nn_string_getdata(delimeter), nn_string_getlength(delimeter)) == 0 || i == nn_string_getlength(string))
            {
                nn_array_push(list, NNValue::fromObject(nn_string_copylen(nn_string_getdata(string) + start, i - start)));
                i += nn_string_getlength(delimeter) - 1;
                start = i + 1;
            }
        }
    }
    else
    {
        length = nn_string_getlength(string);
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            nn_array_push(list, NNValue::fromObject(nn_string_copylen(nn_string_getdata(string) + start, (int)(end - start))));
        }
    }
    return NNValue::fromObject(list);
}

static NNValue nn_objfnstring_replace(const NNFuncContext& scfn)
{
    size_t i;
    size_t xlen;
    size_t totallength;
    NNStringBuffer result;
    NNObjString* substr;
    NNObjString* string;
    NNObjString* repsubstr;
    NNArgCheck check;
    (void)totallength;
    nn_argcheck_init(&check, "replace", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 2, 3);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
    string = scfn.thisval.asString();
    substr = scfn.argv[0].asString();
    repsubstr = scfn.argv[1].asString();
    if((nn_string_getlength(string) == 0 && nn_string_getlength(substr) == 0) || nn_string_getlength(string) == 0 || nn_string_getlength(substr) == 0)
    {
        return NNValue::fromObject(nn_string_copylen(nn_string_getdata(string), nn_string_getlength(string)));
    }
    nn_strbuf_makebasicemptystack(&result, nullptr, 0);
    totallength = 0;
    for(i = 0; i < nn_string_getlength(string); i++)
    {
        if(memcmp(nn_string_getdata(string) + i, nn_string_getdata(substr), nn_string_getlength(substr)) == 0)
        {
            if(nn_string_getlength(substr) > 0)
            {
                nn_strbuf_appendstrn(&result, nn_string_getdata(repsubstr), nn_string_getlength(repsubstr));
            }
            i += nn_string_getlength(substr) - 1;
            totallength += nn_string_getlength(repsubstr);
        }
        else
        {
            nn_strbuf_appendchar(&result, nn_string_get(string, i));
            totallength++;
        }
    }
    xlen = nn_strbuf_length(&result);
    return NNValue::fromObject(nn_string_makefromstrbuf(result, nn_util_hashstring(nn_strbuf_data(&result), xlen), xlen));
}

static NNValue nn_objfnstring_iter(const NNFuncContext& scfn)
{
    size_t index;
    size_t length;
    NNObjString* string;
    NNObjString* result;
    NNArgCheck check;
    nn_argcheck_init(&check, "iter", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    string = scfn.thisval.asString();
    length = nn_string_getlength(string);
    index = scfn.argv[0].asNumber();
    if(((int)index > -1) && (index < length))
    {
        result = nn_string_copylen(&nn_string_getdata(string)[index], 1);
        return NNValue::fromObject(result);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnstring_itern(const NNFuncContext& scfn)
{
    size_t index;
    size_t length;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(&check, "itern", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    string = scfn.thisval.asString();
    length = nn_string_getlength(string);
    if(scfn.argv[0].isNull())
    {
        if(length == 0)
        {
            return NNValue::makeBool(false);
        }
        return NNValue::makeNumber(0);
    }
    if(!scfn.argv[0].isNumber())
    {
        NEON_RETURNERROR(scfn, "strings are numerically indexed");
    }
    index = scfn.argv[0].asNumber();
    if(index < length - 1)
    {
        return NNValue::makeNumber((double)index + 1);
    }
    return NNValue::makeNull();
}

static NNValue nn_objfnstring_each(const NNFuncContext& scfn)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue callable;
    NNValue unused;
    NNObjString* string;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(&check, "each", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    string = scfn.thisval.asString();
    callable = scfn.argv[0];
    arity = nn_nestcall_prepare(callable, scfn.thisval, nestargs, 2);
    for(i = 0; i < nn_string_getlength(string); i++)
    {
        passi = 0;
        if(arity > 0)
        {
            passi++;
            nestargs[0] = NNValue::fromObject(nn_string_copylen(nn_string_getdata(string) + i, 1));
            if(arity > 1)
            {
                passi++;
                nestargs[1] = NNValue::makeNumber(i);
            }
        }
        nn_nestcall_callfunction(callable, scfn.thisval, nestargs, passi, &unused, false);
    }
    /* pop the argument list */
    return NNValue::makeNull();
}

static NNValue nn_objfnstring_appendany(const NNFuncContext& scfn)
{
    size_t i;
    NNValue arg;
    NNObjString* oss;
    NNObjString* selfstring;
    selfstring = scfn.thisval.asString();
    for(i = 0; i < scfn.argc; i++)
    {
        arg = scfn.argv[i];
        if(arg.isNumber())
        {
            nn_string_appendbyte(selfstring, arg.asNumber());
        }
        else
        {
            oss = nn_value_tostring(arg);
            nn_string_appendobject(selfstring, oss);
        }
    }
    /* pop the argument list */
    return scfn.thisval;
}

static NNValue nn_objfnstring_appendbytes(const NNFuncContext& scfn)
{
    size_t i;
    NNValue arg;
    NNObjString* selfstring;
    selfstring = scfn.thisval.asString();
    for(i = 0; i < scfn.argc; i++)
    {
        arg = scfn.argv[i];
        if(arg.isNumber())
        {
            nn_string_appendbyte(selfstring, arg.asNumber());
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
    static NNConstClassMethodItem stringmethods[] = {
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
    nn_class_defnativeconstructor(gcs->m_classprimstring, nn_objfnstring_constructor);
    nn_class_defstaticnativemethod(gcs->m_classprimstring, nn_string_intern("fromCharCode"), nn_objfnstring_fromcharcode);
    nn_class_defstaticnativemethod(gcs->m_classprimstring, nn_string_intern("utf8Decode"), nn_objfnstring_utf8decode);
    nn_class_defstaticnativemethod(gcs->m_classprimstring, nn_string_intern("utf8Encode"), nn_objfnstring_utf8encode);
    nn_class_defstaticnativemethod(gcs->m_classprimstring, nn_string_intern("utf8NumBytes"), nn_objfnstring_utf8numbytes);
    nn_class_defcallablefield(gcs->m_classprimstring, nn_string_intern("length"), nn_objfnstring_length);
    nn_state_installmethods(gcs->m_classprimstring, stringmethods);
}

static NNValue nn_modfn_astscan_scan(const NNFuncContext& scfn)
{
    enum
    {
        /* 12 == "NNAstToken::T_".length */
        kTokPrefixLength = 12
    };

    const char* cstr;
    NNObjString* insrc;
    NNAstLexer* scn;
    NNObjArray* arr;
    NNObjDict* itm;
    NNAstToken token;
    NNArgCheck check;
    nn_argcheck_init(&check, "scan", scfn);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    insrc = scfn.argv[0].asString();
    scn = NNAstLexer::make(nn_string_getdata(insrc));
    arr = nn_array_make();
    while(!scn->isatend())
    {
        itm = nn_object_makedict();
        token = scn->scantoken();
        nn_dict_addentrycstr(itm, "line", NNValue::makeNumber(token.line));
        cstr = NNAstLexer::tokTypeToString(token.type);
        nn_dict_addentrycstr(itm, "type", NNValue::fromObject(nn_string_copycstr(cstr + kTokPrefixLength)));
        nn_dict_addentrycstr(itm, "source", NNValue::fromObject(nn_string_copylen(token.start, token.length)));
        nn_array_push(arr, NNValue::fromObject(itm));
    }
    NNAstLexer::destroy(scn);
    return NNValue::fromObject(arr);
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
    module.name = "astscan";
    module.definedfields = modfields;
    module.definedfunctions = modfuncs;
    module.definedclasses = nullptr;
    module.fnpreloaderfunc = nullptr;
    module.fnunloaderfunc = nullptr;
    ret = &module;
    return ret;
}


static NNValue nn_pcomplex_makeinstance(NNObjClass* klass, double re, double im)
{
    NNUClassComplex* inst;
    inst = (NNUClassComplex*)nn_object_makeinstancesize(klass, sizeof(NNUClassComplex));
    inst->re = re;
    inst->im = im;
    return NNValue::fromObject((NNObjInstance*)inst);
}

static NNValue nn_complexclass_constructor(const NNFuncContext& scfn)
{
    NNUClassComplex* inst;
    assert(scfn.thisval.isInstance());
    inst = (NNUClassComplex*)scfn.thisval.asInstance();
    return nn_pcomplex_makeinstance(((NNObjInstance*)inst)->klass, scfn.argv[0].asNumber(), scfn.argv[1].asNumber());
}

static NNValue nn_complexclass_opadd(const NNFuncContext& scfn)
{
    NNValue vother;
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    NNUClassComplex* other;
    vother = scfn.argv[0];
    assert(scfn.thisval.isInstance());
    inst = (NNUClassComplex*)scfn.thisval.asInstance();
    pv = (NNUClassComplex*)inst;
    other = (NNUClassComplex*)vother.asInstance();
    return nn_pcomplex_makeinstance(((NNObjInstance*)inst)->klass, pv->re + other->re, pv->im + other->im);
}

static NNValue nn_complexclass_opsub(const NNFuncContext& scfn)
{
    NNValue vother;
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    NNUClassComplex* other;
    assert(scfn.thisval.isInstance());
    vother = scfn.argv[0];
    inst = (NNUClassComplex*)scfn.thisval.asInstance();
    pv = (NNUClassComplex*)inst;
    other = (NNUClassComplex*)vother.asInstance();
    return nn_pcomplex_makeinstance(((NNObjInstance*)inst)->klass, pv->re - other->re, pv->im - other->im);
}

static NNValue nn_complexclass_opmul(const NNFuncContext& scfn)
{
    double vre;
    double vim;
    NNValue vother;
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    NNUClassComplex* other;
    assert(scfn.thisval.isInstance());
    vother = scfn.argv[0];
    inst = (NNUClassComplex*)scfn.thisval.asInstance();
    pv = (NNUClassComplex*)inst;
    other = (NNUClassComplex*)vother.asInstance();
    vre = (pv->re * other->re - pv->im * other->im);
    vim = (pv->re * other->im + pv->im * other->re);
    return nn_pcomplex_makeinstance(((NNObjInstance*)inst)->klass, vre, vim);
}

static NNValue nn_complexclass_opdiv(const NNFuncContext& scfn)
{
    double r;
    double i;
    double ti;
    double tr;
    NNValue vother;
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    NNUClassComplex* other;
    assert(scfn.thisval.isInstance());
    vother = scfn.argv[0];
    inst = (NNUClassComplex*)scfn.thisval.asInstance();
    pv = (NNUClassComplex*)inst;
    other = (NNUClassComplex*)vother.asInstance();
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
    return nn_pcomplex_makeinstance(((NNObjInstance*)inst)->klass, (r * ti + i) / tr, (i * ti - r) / tr);
}

static NNValue nn_complexclass_getre(const NNFuncContext& scfn)
{
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    assert(scfn.thisval.isInstance());
    inst = (NNUClassComplex*)scfn.thisval.asInstance();
    pv = (NNUClassComplex*)inst;
    return NNValue::makeNumber(pv->re);
}

static NNValue nn_complexclass_getim(const NNFuncContext& scfn)
{
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    assert(scfn.thisval.isInstance());
    inst = (NNUClassComplex*)scfn.thisval.asInstance();
    pv = (NNUClassComplex*)inst;
    return NNValue::makeNumber(pv->im);
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
    module.name = "complex";
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

static NNValue nn_nativefn_time(const NNFuncContext& scfn)
{
    struct timeval tv;
    NNArgCheck check;
    nn_argcheck_init(&check, "time", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    osfn_gettimeofday(&tv, nullptr);
    return NNValue::makeNumber((double)tv.tv_sec + ((double)tv.tv_usec / 10000000));
}

static NNValue nn_nativefn_microtime(const NNFuncContext& scfn)
{
    struct timeval tv;
    NNArgCheck check;
    nn_argcheck_init(&check, "microtime", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    osfn_gettimeofday(&tv, nullptr);
    return NNValue::makeNumber((1000000 * (double)tv.tv_sec) + ((double)tv.tv_usec / 10));
}

static NNValue nn_nativefn_id(const NNFuncContext& scfn)
{
    NNValue val;
    NNArgCheck check;
    nn_argcheck_init(&check, "id", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    val = scfn.argv[0];
    return NNValue::makeNumber(*(long*)&val);
}

static NNValue nn_nativefn_int(const NNFuncContext& scfn)
{
    NNArgCheck check;
    nn_argcheck_init(&check, "int", scfn);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    if(scfn.argc == 0)
    {
        return NNValue::makeNumber(0);
    }
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    return NNValue::makeNumber((double)((int)scfn.argv[0].asNumber()));
}

static NNValue nn_nativefn_chr(const NNFuncContext& scfn)
{
    size_t len;
    char* string;
    int ch;
    NNArgCheck check;
    nn_argcheck_init(&check, "chr", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    ch = scfn.argv[0].asNumber();
    string = nn_util_utf8encode(ch, &len);
    return NNValue::fromObject(nn_string_takelen(string, len));
}

static NNValue nn_nativefn_ord(const NNFuncContext& scfn)
{
    int ord;
    int length;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(&check, "ord", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = scfn.argv[0].asString();
    length = nn_string_getlength(string);
    if(length > 1)
    {
        NEON_RETURNERROR(scfn, "ord() expects character as argument, string given");
    }
    ord = (int)nn_string_getdata(string)[0];
    if(ord < 0)
    {
        ord += 256;
    }
    return NNValue::makeNumber(ord);
}

static NNValue nn_nativefn_rand(const NNFuncContext& scfn)
{
    int tmp;
    int lowerlimit;
    int upperlimit;
    NNArgCheck check;
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
    return NNValue::makeNumber(nn_util_mtrand(lowerlimit, upperlimit));
}

static NNValue nn_nativefn_eval(const NNFuncContext& scfn)
{
    NNValue result;
    NNObjString* os;
    NNArgCheck check;
    nn_argcheck_init(&check, "eval", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    os = scfn.argv[0].asString();
    /*fprintf(stderr, "eval:src=%s\n", nn_string_getdata(os));*/
    result = nn_state_evalsource(nn_string_getdata(os));
    return result;
}

/*
static NNValue nn_nativefn_loadfile(const NNFuncContext& scfn)
{
    NNValue result;
    NNObjString* os;
    NNArgCheck check;
    nn_argcheck_init(&check, "loadfile", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    os = scfn.argv[0].asString();
    fprintf(stderr, "eval:src=%s\n", nn_string_getdata(os));
    result = nn_state_evalsource(nn_string_getdata(os));
    return result;
}
*/

static NNValue nn_nativefn_instanceof(const NNFuncContext& scfn)
{
    NNArgCheck check;
    nn_argcheck_init(&check, "instanceof", scfn);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isinstance);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isclass);
    return NNValue::makeBool(nn_util_isinstanceof(scfn.argv[0].asInstance()->klass, scfn.argv[1].asClass()));
}

static NNValue nn_nativefn_sprintf(const NNFuncContext& scfn)
{
    NNFormatInfo nfi;
    NNIOStream pr;
    NNObjString* res;
    NNObjString* ofmt;
    NNArgCheck check;
    nn_argcheck_init(&check, "sprintf", scfn);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = scfn.argv[0].asString();
    NNIOStream::makeStackString(&pr);
    nn_strformat_init(&nfi, &pr, nn_string_getdata(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, scfn.argc, 1, scfn.argv))
    {
        return NNValue::makeNull();
    }
    res = pr.takeString();
    NNIOStream::destroy(&pr);
    return NNValue::fromObject(res);
}

static NNValue nn_nativefn_printf(const NNFuncContext& scfn)
{
    NNFormatInfo nfi;
    NNObjString* ofmt;
    NNArgCheck check;
    auto gcs = SharedState::get();
    nn_argcheck_init(&check, "printf", scfn);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = scfn.argv[0].asString();
    nn_strformat_init(&nfi, gcs->m_stdoutprinter, nn_string_getdata(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, scfn.argc, 1, scfn.argv))
    {
    }
    return NNValue::makeNull();
}

static NNValue nn_nativefn_print(const NNFuncContext& scfn)
{
    size_t i;
    auto gcs = SharedState::get();
    for(i = 0; i < scfn.argc; i++)
    {
        nn_iostream_printvalue(gcs->m_stdoutprinter, scfn.argv[i], false, true);
    }
    return NNValue::makeNull();
}

static NNValue nn_nativefn_println(const NNFuncContext& scfn)
{
    NNValue v;
    auto gcs = SharedState::get();
    v = nn_nativefn_print(scfn);
    gcs->m_stdoutprinter->writeString("\n");
    return v;
}

static NNValue nn_nativefn_isnan(const NNFuncContext& scfn)
{
    (void)scfn;
    return NNValue::makeBool(false);
}

static NNValue nn_objfnjson_stringify(const NNFuncContext& scfn)
{
    NNValue v;
    NNIOStream pr;
    NNObjString* os;
    v = scfn.argv[0];
    NNIOStream::makeStackString(&pr);
    pr.jsonmode = true;
    nn_iostream_printvalue(&pr, v, true, false);
    os = pr.takeString();
    NNIOStream::destroy(&pr);
    return NNValue::fromObject(os);
}

/**
 * setup global functions.
 */
void nn_state_initbuiltinfunctions()
{
    NNObjClass* klass;
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
        nn_class_defstaticnativemethod(klass, nn_string_copycstr("stringify"), nn_objfnjson_stringify);
    }
}

/*
 * you can use this file as a template for new native modules.
 * just fill out the fields, give the load function a meaningful name (i.e., nn_natmodule_load_foobar if your module is "foobar"),
 * et cetera.
 * then, add said function in libmodule.c's nn_import_loadbuiltinmodules, and you're good to go!
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
    module.name = "null";
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

static NNValue nn_modfn_os_readdir(const NNFuncContext& scfn)
{
    const char* dirn;
    FSDirReader rd;
    FSDirItem itm;
    NNValue res;
    NNValue itemval;
    NNValue callable;
    NNObjString* os;
    NNObjString* itemstr;
    NNArgCheck check;
    NNValue nestargs[2];
    nn_argcheck_init(&check, "readdir", scfn);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_iscallable);

    os = scfn.argv[0].asString();
    callable = scfn.argv[1];
    dirn = nn_string_getdata(os);
    if(fslib_diropen(&rd, dirn))
    {
        while(fslib_dirread(&rd, &itm))
        {
#if 0
                itemstr = nn_string_intern(itm.name);
#else
            itemstr = nn_string_copycstr(itm.name);
#endif
            itemval = NNValue::fromObject(itemstr);
            nestargs[0] = itemval;
            nn_nestcall_callfunction(callable, scfn.thisval, nestargs, 1, &res, false);
        }
        fslib_dirclose(&rd);
        return NNValue::makeNull();
    }
    else
    {
        nn_except_throw("cannot open directory '%s'", dirn);
    }
    return NNValue::makeNull();
}

/*
static NNValue nn_modfn_os_$template(const NNFuncContext& scfn)
{
    int64_t r;
    int64_t mod;
    NNObjString* path;
    NNArgCheck check;
    nn_argcheck_init(&check, "chmod", scfn);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    path = scfn.argv[0].asString();
    mod = scfn.argv[1].asNumber();
    r = osfn_chmod(nn_string_getdata(path), mod);
    return NNValue::makeNumber(r);
}
*/

static NNValue nn_modfn_os_chmod(const NNFuncContext& scfn)
{
    int64_t r;
    int64_t mod;
    NNObjString* path;
    NNArgCheck check;
    nn_argcheck_init(&check, "chmod", scfn);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    path = scfn.argv[0].asString();
    mod = scfn.argv[1].asNumber();
    r = osfn_chmod(nn_string_getdata(path), mod);
    return NNValue::makeNumber(r);
}

static NNValue nn_modfn_os_mkdir(const NNFuncContext& scfn)
{
    int64_t r;
    int64_t mod;
    NNObjString* path;
    NNArgCheck check;
    nn_argcheck_init(&check, "mkdir", scfn);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    path = scfn.argv[0].asString();
    mod = scfn.argv[1].asNumber();
    r = osfn_mkdir(nn_string_getdata(path), mod);
    return NNValue::makeNumber(r);
}

static NNValue nn_modfn_os_chdir(const NNFuncContext& scfn)
{
    int64_t r;
    NNObjString* path;
    NNArgCheck check;
    nn_argcheck_init(&check, "chdir", scfn);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = scfn.argv[0].asString();
    r = osfn_chdir(nn_string_getdata(path));
    return NNValue::makeNumber(r);
}

static NNValue nn_modfn_os_rmdir(const NNFuncContext& scfn)
{
    int64_t r;
    NNObjString* path;
    NNArgCheck check;
    nn_argcheck_init(&check, "rmdir", scfn);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    path = scfn.argv[0].asString();
    r = osfn_rmdir(nn_string_getdata(path));
    return NNValue::makeNumber(r);
}

static NNValue nn_modfn_os_unlink(const NNFuncContext& scfn)
{
    int64_t r;
    NNObjString* path;
    NNArgCheck check;
    nn_argcheck_init(&check, "unlink", scfn);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = scfn.argv[0].asString();
    r = osfn_unlink(nn_string_getdata(path));
    return NNValue::makeNumber(r);
}

static NNValue nn_modfn_os_getenv(const NNFuncContext& scfn)
{
    const char* r;
    NNObjString* key;
    NNArgCheck check;
    nn_argcheck_init(&check, "getenv", scfn);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    key = scfn.argv[0].asString();
    r = osfn_getenv(nn_string_getdata(key));
    if(r == nullptr)
    {
        return NNValue::makeNull();
    }
    return NNValue::fromObject(nn_string_copycstr(r));
}

static NNValue nn_modfn_os_setenv(const NNFuncContext& scfn)
{
    NNObjString* key;
    NNObjString* value;
    NNArgCheck check;
    nn_argcheck_init(&check, "setenv", scfn);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
    key = scfn.argv[0].asString();
    value = scfn.argv[1].asString();
    return NNValue::makeBool(osfn_setenv(nn_string_getdata(key), nn_string_getdata(value), true));
}

static NNValue nn_modfn_os_cwdhelper(const NNFuncContext& scfn, const char* name)
{
    enum
    {
        kMaxBufSz = 1024
    };
    NNArgCheck check;
    char* r;
    char buf[kMaxBufSz];
    nn_argcheck_init(&check, name, scfn);
    r = osfn_getcwd(buf, kMaxBufSz);
    if(r == nullptr)
    {
        return NNValue::makeNull();
    }
    return NNValue::fromObject(nn_string_copycstr(r));
}

static NNValue nn_modfn_os_cwd(const NNFuncContext& scfn)
{
    return nn_modfn_os_cwdhelper(scfn, "cwd");
}

static NNValue nn_modfn_os_pwd(const NNFuncContext& scfn)
{
    return nn_modfn_os_cwdhelper(scfn, "pwd");
}

static NNValue nn_modfn_os_basename(const NNFuncContext& scfn)
{
    const char* r;
    NNObjString* path;
    NNArgCheck check;
    nn_argcheck_init(&check, "basename", scfn);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = scfn.argv[0].asString();
    r = osfn_basename(nn_string_getdata(path));
    if(r == nullptr)
    {
        return NNValue::makeNull();
    }
    return NNValue::fromObject(nn_string_copycstr(r));
}

static NNValue nn_modfn_os_dirname(const NNFuncContext& scfn)
{
    const char* r;
    NNObjString* path;
    NNArgCheck check;
    nn_argcheck_init(&check, "dirname", scfn);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = scfn.argv[0].asString();
    r = osfn_dirname(nn_string_getdata(path));
    if(r == nullptr)
    {
        return NNValue::makeNull();
    }
    return NNValue::fromObject(nn_string_copycstr(r));
}

static NNValue nn_modfn_os_touch(const NNFuncContext& scfn)
{
    FILE* fh;
    NNObjString* path;
    NNArgCheck check;
    nn_argcheck_init(&check, "touch", scfn);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = scfn.argv[0].asString();
    fh = fopen(nn_string_getdata(path), "rb");
    if(fh == nullptr)
    {
        return NNValue::makeBool(false);
    }
    fclose(fh);
    return NNValue::makeBool(true);
}

#define putorgetkey(md, name, value)                            \
    {                                                           \
        if(havekey && (keywanted != nullptr))                   \
        {                                                       \
            if(strcmp(name, nn_string_getdata(keywanted)) == 0) \
            {                                                   \
                return value;                                   \
            }                                                   \
        }                                                       \
        else                                                    \
        {                                                       \
            nn_dict_addentrycstr(md, name, value);              \
        }                                                       \
    }

NNValue nn_modfn_os_stat(const NNFuncContext& scfn)
{
    bool havekey;
    NNObjString* keywanted;
    NNObjString* path;
    NNArgCheck check;
    NNObjDict* md;
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
    strp = nn_string_getdata(path);
    if(!nn_filestat_initfrompath(&nfs, strp))
    {
        nn_except_throwclass(gcs->m_exceptions.ioerror, "%s: %s", strp, strerror(errno));
        return NNValue::makeNull();
    }
    md = nullptr;
    if(!havekey)
    {
        md = nn_object_makedict();
    }
    putorgetkey(md, "path", NNValue::fromObject(path));
    putorgetkey(md, "mode", NNValue::makeNumber(nfs.mode));
    putorgetkey(md, "modename", NNValue::fromObject(nn_string_copycstr(nfs.modename)));
    putorgetkey(md, "inode", NNValue::makeNumber(nfs.inode));
    putorgetkey(md, "links", NNValue::makeNumber(nfs.numlinks));
    putorgetkey(md, "uid", NNValue::makeNumber(nfs.owneruid));
    putorgetkey(md, "gid", NNValue::makeNumber(nfs.ownergid));
    putorgetkey(md, "blocksize", NNValue::makeNumber(nfs.blocksize));
    putorgetkey(md, "blocks", NNValue::makeNumber(nfs.blockcount));
    putorgetkey(md, "filesize", NNValue::makeNumber(nfs.filesize));
    putorgetkey(md, "lastchanged", NNValue::fromObject(nn_string_copycstr(nn_filestat_ctimetostring(nfs.tmlastchanged))));
    putorgetkey(md, "lastaccess", NNValue::fromObject(nn_string_copycstr(nn_filestat_ctimetostring(nfs.tmlastaccessed))));
    putorgetkey(md, "lastmodified", NNValue::fromObject(nn_string_copycstr(nn_filestat_ctimetostring(nfs.tmlastmodified))));
    return NNValue::fromObject(md);
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
    module.name = "os";
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
        finalsz = NEON_CONFIG_INITSTACKCOUNT * sizeof(NNValue);
        gcs->m_vmstate.stackvalues = (NNValue*)nn_memory_malloc(finalsz);
        if(gcs->m_vmstate.stackvalues == nullptr)
        {
            fprintf(stderr, "error: failed to allocate stackvalues!\n");
            abort();
        }
        memset(gcs->m_vmstate.stackvalues, 0, finalsz);
    }
    {
        gcs->m_vmstate.framecapacity = NEON_CONFIG_INITFRAMECOUNT;
        finalsz = NEON_CONFIG_INITFRAMECOUNT * sizeof(NNCallFrame);
        gcs->m_vmstate.framevalues = (NNCallFrame*)nn_memory_malloc(finalsz);
        if(gcs->m_vmstate.framevalues == nullptr)
        {
            fprintf(stderr, "error: failed to allocate framevalues!\n");
            abort();
        }
        memset(gcs->m_vmstate.framevalues, 0, finalsz);
    }
}

NEON_INLINE void nn_vm_resizeinfo(const char* context, NNObjFunction* closure, size_t needed)
{
    const char* data;
    const char* name;
    (void)needed;
    name = "unknown";
    if(closure->fnclosure.scriptfunc != nullptr)
    {
        if(closure->fnclosure.scriptfunc->name != nullptr)
        {
            data = nn_string_getdata(closure->fnclosure.scriptfunc->name);
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

bool nn_vm_callclosure(NNObjFunction* closure, NNValue thisval, size_t argcount, bool fromoperator)
{
    int i;
    int startva;
    NNCallFrame* frame;
    NNObjArray* argslist;
    // closure->clsthisval = thisval;
    NEON_APIDEBUG("thisval.type=%s, argcount=%d", nn_value_typename(thisval, true), argcount);
    auto gcs = SharedState::get();
    /* fill empty parameters if not variadic */
    for(; !closure->fnclosure.scriptfunc->fnscriptfunc.isvariadic && (argcount < size_t(closure->fnclosure.scriptfunc->fnscriptfunc.arity)); argcount++)
    {
        gcs->stackPush(NNValue::makeNull());
    }
    /* handle variadic arguments... */
    if(closure->fnclosure.scriptfunc->fnscriptfunc.isvariadic && (argcount >= size_t(closure->fnclosure.scriptfunc->fnscriptfunc.arity) - 1))
    {
        startva = argcount - closure->fnclosure.scriptfunc->fnscriptfunc.arity;
        argslist = nn_object_makearray();
        gcs->stackPush(NNValue::fromObject(argslist));
        for(i = startva; i >= 0; i--)
        {
            argslist->varray.push(nn_vm_stackpeek(i + 1));
        }
        argcount -= startva;
        /* +1 for the gc protection push above */
        gcs->stackPop(startva + 2);
        gcs->stackPush(NNValue::fromObject(argslist));
    }
    if(argcount != size_t(closure->fnclosure.scriptfunc->fnscriptfunc.arity))
    {
        gcs->stackPop(argcount);
        if(closure->fnclosure.scriptfunc->fnscriptfunc.isvariadic)
        {
            return nn_except_throw("function '%s' expected at least %d arguments but got %d", nn_string_getdata(closure->name), closure->fnclosure.scriptfunc->fnscriptfunc.arity - 1, argcount);
        }
        else
        {
            return nn_except_throw("function '%s' expected %d arguments but got %d", nn_string_getdata(closure->name), closure->fnclosure.scriptfunc->fnscriptfunc.arity, argcount);
        }
    }
    if(gcs->checkMaybeResize())
    {
#if 0
            gcs->stackPop(argcount);
#endif
    }
    if(fromoperator)
    {
#if 0
            gcs->stackPop();
            gcs->stackPush(thisval);
#else
        int64_t spos;
        spos = (gcs->m_vmstate.stackidx + (-argcount - 1));
    #if 0
                gcs->m_vmstate.stackvalues[spos] = closure->clsthisval;
    #else
        gcs->m_vmstate.stackvalues[spos] = thisval;
    #endif
#endif
    }
    frame = &gcs->m_vmstate.framevalues[gcs->m_vmstate.framecount++];
    frame->closure = closure;
    frame->inscode = closure->fnclosure.scriptfunc->fnscriptfunc.blob.instrucs;
    frame->stackslotpos = gcs->m_vmstate.stackidx + (-argcount - 1);
    return true;
}

NEON_INLINE bool nn_vm_callnative(NNObjFunction* native, NNValue thisval, size_t argcount)
{
    int64_t spos;
    NNValue r;
    NNValue* vargs;
    NEON_APIDEBUG("thisval.type=%s, argcount=%d", nn_value_typename(thisval, true), argcount);
    auto gcs = SharedState::get();
    spos = gcs->m_vmstate.stackidx + (-argcount);
    vargs = &gcs->m_vmstate.stackvalues[spos];
    r = native->fnnativefunc.natfunc(NNFuncContext{thisval, vargs, argcount});
    {
        gcs->m_vmstate.stackvalues[spos - 1] = r;
        gcs->m_vmstate.stackidx -= argcount;
    }
    SharedState::clearGCProtect();
    return true;
}

bool nn_vm_callvaluewithobject(NNValue callable, NNValue thisval, size_t argcount, bool fromoper)
{
    int64_t spos;
    NNObjFunction* ofn;
    ofn = callable.asFunction();
#if 0
    #define NEON_APIPRINT(...) fprintf(stderr, __VA_ARGS__), fprintf(stderr, "\n");
#else
    #define NEON_APIPRINT(...)
#endif
    NEON_APIPRINT("*callvaluewithobject*: thisval.type=%s, callable.type=%s, argcount=%d", nn_value_typename(thisval, true), nn_value_typename(callable, true), argcount);
    auto gcs = SharedState::get();
    if(callable.isObject())
    {
        switch(callable.objType())
        {
            case NNObject::OTYP_FUNCCLOSURE:
            {
                return nn_vm_callclosure(ofn, thisval, argcount, fromoper);
            }
            break;
            case NNObject::OTYP_FUNCNATIVE:
            {
                return nn_vm_callnative(ofn, thisval, argcount);
            }
            break;
            case NNObject::OTYP_FUNCBOUND:
            {
                NNObjFunction* bound;
                bound = ofn;
                spos = (gcs->m_vmstate.stackidx + (-argcount - 1));
                gcs->m_vmstate.stackvalues[spos] = thisval;
                return nn_vm_callclosure(bound->fnmethod.method, thisval, argcount, fromoper);
            }
            break;
            case NNObject::OTYP_CLASS:
            {
                NNObjClass* klass;
                klass = callable.asClass();
                spos = (gcs->m_vmstate.stackidx + (-argcount - 1));
                gcs->m_vmstate.stackvalues[spos] = thisval;
                if(!klass->constructor.isNull())
                {
                    return nn_vm_callvaluewithobject(klass->constructor, thisval, argcount, false);
                }
                else if(klass->superclass != nullptr && !klass->superclass->constructor.isNull())
                {
                    return nn_vm_callvaluewithobject(klass->superclass->constructor, thisval, argcount, false);
                }
                else if(argcount != 0)
                {
                    return nn_except_throw("%s constructor expects 0 arguments, %d given", nn_string_getdata(klass->name), argcount);
                }
                return true;
            }
            break;
            case NNObject::OTYP_MODULE:
            {
                NNObjModule* module;
                NNProperty* field;
                module = callable.asModule();
                field = module->deftable.getfieldbyostr(module->name);
                if(field != nullptr)
                {
                    return nn_vm_callvalue(field->value, thisval, argcount, false);
                }
                return nn_except_throw("module %s does not export a default function", module->name);
            }
            break;
            default:
                break;
        }
    }
    return nn_except_throw("object of type %s is not callable", nn_value_typename(callable, false));
}

bool nn_vm_callvalue(NNValue callable, NNValue thisval, size_t argcount, bool fromoperator)
{
    NNValue actualthisval;
    NNObjFunction* ofn;
    if(callable.isObject())
    {
        ofn = callable.asFunction();
        switch(callable.objType())
        {
            case NNObject::OTYP_FUNCBOUND:
            {
                NNObjFunction* bound;
                bound = ofn;
                actualthisval = bound->fnmethod.receiver;
                if(!thisval.isNull())
                {
                    actualthisval = thisval;
                }
                NEON_APIDEBUG("actualthisval.type=%s, argcount=%d", nn_value_typename(actualthisval, true), argcount);
                return nn_vm_callvaluewithobject(callable, actualthisval, argcount, fromoperator);
            }
            break;
            case NNObject::OTYP_CLASS:
            {
                NNObjClass* klass;
                NNObjInstance* instance;
                klass = callable.asClass();
                instance = nn_object_makeinstance(klass);
                actualthisval = NNValue::fromObject(instance);
                if(!thisval.isNull())
                {
                    actualthisval = thisval;
                }
                NEON_APIDEBUG("actualthisval.type=%s, argcount=%d", nn_value_typename(actualthisval, true), argcount);
                return nn_vm_callvaluewithobject(callable, actualthisval, argcount, fromoperator);
            }
            break;
            default:
            {
            }
            break;
        }
    }
    NEON_APIDEBUG("thisval.type=%s, argcount=%d", nn_value_typename(thisval, true), argcount);
    return nn_vm_callvaluewithobject(callable, thisval, argcount, fromoperator);
}

NEON_INLINE NNObjFunction::ContextType nn_value_getmethodtype(NNValue method)
{
    NNObjFunction* ofn;
    ofn = method.asFunction();
    switch(method.objType())
    {
        case NNObject::OTYP_FUNCNATIVE:
            return ofn->contexttype;
        case NNObject::OTYP_FUNCCLOSURE:
            return ofn->fnclosure.scriptfunc->contexttype;
        default:
            break;
    }
    return NNObjFunction::CTXTYPE_FUNCTION;
}

NNObjClass* nn_value_getclassfor(NNValue receiver)
{
    auto gcs = SharedState::get();
    if(receiver.isNumber())
    {
        return gcs->m_classprimnumber;
    }
    if(receiver.isObject())
    {
        switch(receiver.asObject()->type)
        {
            case NNObject::OTYP_STRING:
                return gcs->m_classprimstring;
            case NNObject::OTYP_RANGE:
                return gcs->m_classprimrange;
            case NNObject::OTYP_ARRAY:
                return gcs->m_classprimarray;
            case NNObject::OTYP_DICT:
                return gcs->m_classprimdict;
            case NNObject::OTYP_FILE:
                return gcs->m_classprimfile;
            case NNObject::OTYP_FUNCBOUND:
            case NNObject::OTYP_FUNCCLOSURE:
            case NNObject::OTYP_FUNCSCRIPT:
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

NEON_INLINE NNValue nn_vm_stackpeek(int distance)
{
    NNValue v;
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

NEON_FORCEINLINE uint16_t nn_vmbits_readbyte()
{
    uint16_t r;
    auto gcs = SharedState::get();
    r = gcs->m_vmstate.currentframe->inscode->code;
    gcs->m_vmstate.currentframe->inscode++;
    return r;
}

NEON_FORCEINLINE NNInstruction nn_vmbits_readinstruction()
{
    NNInstruction r;
    auto gcs = SharedState::get();
    r = *gcs->m_vmstate.currentframe->inscode;
    gcs->m_vmstate.currentframe->inscode++;
    return r;
}

NEON_FORCEINLINE uint16_t nn_vmbits_readshort()
{
    uint16_t b;
    uint16_t a;
    auto gcs = SharedState::get();
    a = gcs->m_vmstate.currentframe->inscode[0].code;
    b = gcs->m_vmstate.currentframe->inscode[1].code;
    gcs->m_vmstate.currentframe->inscode += 2;
    return (uint16_t)((a << 8) | b);
}

NEON_FORCEINLINE NNValue nn_vmbits_readconst()
{
    uint16_t idx;
    idx = nn_vmbits_readshort();
    auto gcs = SharedState::get();
    return gcs->m_vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.blob.constants.get(idx);
}

NEON_FORCEINLINE NNObjString* nn_vmbits_readstring()
{
    return nn_vmbits_readconst().asString();
}

NEON_FORCEINLINE bool nn_vmutil_invokemethodfromclass(NNObjClass* klass, NNObjString* name, size_t argcount)
{
    NNProperty* field;
    NEON_APIDEBUG("argcount=%d", argcount);
    field = klass->instmethods.getfieldbyostr(name);
    if(field != nullptr)
    {
        if(nn_value_getmethodtype(field->value) == NNObjFunction::CTXTYPE_PRIVATE)
        {
            return nn_except_throw("cannot call private method '%s' from instance of %s", nn_string_getdata(name), nn_string_getdata(klass->name));
        }
        return nn_vm_callvaluewithobject(field->value, NNValue::fromObject(klass), argcount, false);
    }
    return nn_except_throw("undefined method '%s' in %s", nn_string_getdata(name), nn_string_getdata(klass->name));
}

NEON_FORCEINLINE bool nn_vmutil_invokemethodself(NNObjString* name, size_t argcount)
{
    int64_t spos;
    NNValue receiver;
    NNObjInstance* instance;
    NNProperty* field;
    NEON_APIDEBUG("argcount=%d", argcount);
    auto gcs = SharedState::get();
    receiver = nn_vm_stackpeek(argcount);
    if(receiver.isInstance())
    {
        instance = receiver.asInstance();
        field = instance->klass->instmethods.getfieldbyostr(name);
        if(field != nullptr)
        {
            return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
        }
        field = instance->properties.getfieldbyostr(name);
        if(field != nullptr)
        {
            spos = (gcs->m_vmstate.stackidx + (-argcount - 1));
            gcs->m_vmstate.stackvalues[spos] = receiver;
            return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
        }
    }
    else if(receiver.isClass())
    {
        field = receiver.asClass()->instmethods.getfieldbyostr(name);
        if(field != nullptr)
        {
            if(nn_value_getmethodtype(field->value) == NNObjFunction::CTXTYPE_STATIC)
            {
                return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
            }
            return nn_except_throw("cannot call non-static method %s() on non instance", nn_string_getdata(name));
        }
    }
    return nn_except_throw("cannot call method '%s' on object of type '%s'", nn_string_getdata(name), nn_value_typename(receiver, false));
}

NEON_FORCEINLINE bool nn_vmutil_invokemethodnormal(NNObjString* name, size_t argcount)
{
    size_t spos;
    NNObject::Type rectype;
    NNValue receiver;
    NNProperty* field;
    NNObjClass* klass;
    receiver = nn_vm_stackpeek(argcount);
    NEON_APIDEBUG("receiver.type=%s, argcount=%d", nn_value_typename(receiver, true), argcount);
    auto gcs = SharedState::get();
    if(receiver.isObject())
    {
        rectype = receiver.asObject()->type;
        switch(rectype)
        {
            case NNObject::OTYP_MODULE:
            {
                NNObjModule* module;
                NEON_APIDEBUG("receiver is a module");
                module = receiver.asModule();
                field = module->deftable.getfieldbyostr(name);
                if(field != nullptr)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        return nn_except_throw("cannot call private module method '%s'", nn_string_getdata(name));
                    }
                    return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
                }
                return nn_except_throw("module '%s' does not have a field named '%s'", nn_string_getdata(module->name), nn_string_getdata(name));
            }
            break;
            case NNObject::OTYP_CLASS:
            {
                NEON_APIDEBUG("receiver is a class");
                klass = receiver.asClass();
                field = nn_class_getstaticproperty(klass, name);
                if(field != nullptr)
                {
                    return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
                }
                field = nn_class_getstaticmethodfield(klass, name);
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
                    NNObjFunction::ContextType fntyp;
                    field = klass->instmethods.getfieldbyostr(name);
                    if(field != nullptr)
                    {
                        fntyp = nn_value_getmethodtype(field->value);
                        fprintf(stderr, "fntyp: %d\n", fntyp);
                        if(fntyp == NNObjFunction::CTXTYPE_PRIVATE)
                        {
                            return nn_except_throw("cannot call private method %s() on %s", nn_string_getdata(name), nn_string_getdata(klass->name));
                        }
                        if(fntyp == NNObjFunction::CTXTYPE_STATIC)
                        {
                            return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
                        }
                    }
                }
#endif
                return nn_except_throw("unknown method %s() in class %s", nn_string_getdata(name), nn_string_getdata(klass->name));
            }
            case NNObject::OTYP_INSTANCE:
            {
                NNObjInstance* instance;
                NEON_APIDEBUG("receiver is an instance");
                instance = receiver.asInstance();
                field = instance->properties.getfieldbyostr(name);
                if(field != nullptr)
                {
                    spos = (gcs->m_vmstate.stackidx + (-argcount - 1));
                    gcs->m_vmstate.stackvalues[spos] = receiver;
                    return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
                }
                return nn_vmutil_invokemethodfromclass(instance->klass, name, argcount);
            }
            break;
            case NNObject::OTYP_DICT:
            {
                NEON_APIDEBUG("receiver is a dictionary");
                field = nn_class_getmethodfield(gcs->m_classprimdict, name);
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
                return nn_except_throw("'dict' has no method %s()", nn_string_getdata(name));
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
        return nn_except_throw("non-object %s has no method named '%s'", nn_value_typename(receiver, false), nn_string_getdata(name));
    }
    field = nn_class_getmethodfield(klass, name);
    if(field != nullptr)
    {
        return nn_vm_callvaluewithobject(field->value, receiver, argcount, false);
    }
    return nn_except_throw("'%s' has no method %s()", nn_string_getdata(klass->name), nn_string_getdata(name));
}

NEON_FORCEINLINE bool nn_vmutil_bindmethod(NNObjClass* klass, NNObjString* name)
{
    NNValue val;
    NNProperty* field;
    NNObjFunction* bound;
    auto gcs = SharedState::get();
    field = klass->instmethods.getfieldbyostr(name);
    if(field != nullptr)
    {
        if(nn_value_getmethodtype(field->value) == NNObjFunction::CTXTYPE_PRIVATE)
        {
            return nn_except_throw("cannot get private property '%s' from instance", nn_string_getdata(name));
        }
        val = nn_vm_stackpeek(0);
        bound = nn_object_makefuncbound(val, field->value.asFunction());
        gcs->stackPop();
        gcs->stackPush(NNValue::fromObject(bound));
        return true;
    }
    return nn_except_throw("undefined property '%s'", nn_string_getdata(name));
}

NEON_FORCEINLINE NNObjUpvalue* nn_vmutil_upvaluescapture(NNValue* local, int stackpos)
{
    NNObjUpvalue* upvalue;
    NNObjUpvalue* prevupvalue;
    NNObjUpvalue* createdupvalue;
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

NEON_FORCEINLINE void nn_vmutil_upvaluesclose(const NNValue* last)
{
    NNObjUpvalue* upvalue;
    auto gcs = SharedState::get();
    while(gcs->m_vmstate.openupvalues != nullptr && (&gcs->m_vmstate.openupvalues->location) >= last)
    {
        upvalue = gcs->m_vmstate.openupvalues;
        upvalue->closed = upvalue->location;
        upvalue->location = upvalue->closed;
        gcs->m_vmstate.openupvalues = upvalue->next;
    }
}

NEON_FORCEINLINE void nn_vmutil_definemethod(NNObjString* name)
{
    NNValue method;
    NNObjClass* klass;
    auto gcs = SharedState::get();
    method = nn_vm_stackpeek(0);
    klass = nn_vm_stackpeek(1).asClass();
    klass->instmethods.set(NNValue::fromObject(name), method);
    if(nn_value_getmethodtype(method) == NNObjFunction::CTXTYPE_INITIALIZER)
    {
        klass->constructor = method;
    }
    gcs->stackPop();
}

NEON_FORCEINLINE void nn_vmutil_defineproperty(NNObjString* name, bool isstatic)
{
    NNValue property;
    NNObjClass* klass;
    auto gcs = SharedState::get();
    property = nn_vm_stackpeek(0);
    klass = nn_vm_stackpeek(1).asClass();
    if(!isstatic)
    {
        nn_class_defproperty(klass, name, property);
    }
    else
    {
        nn_class_setstaticproperty(klass, name, property);
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
bool nn_util_isinstanceof(NNObjClass* klass1, NNObjClass* expected)
{
    size_t klen;
    size_t elen;
    const char* kname;
    const char* ename;
    while(klass1 != nullptr)
    {
        elen = nn_string_getlength(expected->name);
        klen = nn_string_getlength(klass1->name);
        ename = nn_string_getdata(expected->name);
        kname = nn_string_getdata(klass1->name);
        if(elen == klen && memcmp(kname, ename, klen) == 0)
        {
            return true;
        }
        klass1 = klass1->superclass;
    }
    return false;
}

/*
 * don' try to optimize too much here, since its largely irrelevant how big or small
 * the strings are; inevitably it will always be <length-of-string> * number.
 * not preallocating also means that the allocator only allocates as much as actually needed.
 */
NEON_FORCEINLINE NNObjString* nn_vmutil_multiplystring(NNObjString* str, double number)
{
    size_t i;
    size_t times;
    NNIOStream pr;
    NNObjString* os;
    times = (size_t)number;
    /* 'str' * 0 == '', 'str' * -1 == '' */
    if(times <= 0)
    {
        return nn_string_internlen("", 0);
    }
    /* 'str' * 1 == 'str' */
    else if(times == 1)
    {
        return str;
    }
    NNIOStream::makeStackString(&pr);
    for(i = 0; i < times; i++)
    {
        pr.writeString(nn_string_getdata(str), nn_string_getlength(str));
    }
    os = pr.takeString();
    NNIOStream::destroy(&pr);
    return os;
}

NEON_FORCEINLINE NNObjArray* nn_vmutil_combinearrays(NNObjArray* a, NNObjArray* b)
{
    size_t i;
    NNObjArray* list;
    auto gcs = SharedState::get();
    list = nn_object_makearray();
    gcs->stackPush(NNValue::fromObject(list));
    for(i = 0; i < a->varray.count(); i++)
    {
        list->varray.push(a->varray.get(i));
    }
    for(i = 0; i < b->varray.count(); i++)
    {
        list->varray.push(b->varray.get(i));
    }
    gcs->stackPop();
    return list;
}

NEON_FORCEINLINE void nn_vmutil_multiplyarray(NNObjArray* from, NNObjArray* newlist, size_t times)
{
    size_t i;
    size_t j;
    for(i = 0; i < times; i++)
    {
        for(j = 0; j < from->varray.count(); j++)
        {
            newlist->varray.push(from->varray.get(j));
        }
    }
}

NEON_FORCEINLINE bool nn_vmutil_dogetrangedindexofarray(NNObjArray* list, bool willassign)
{
    long i;
    long idxlower;
    long idxupper;
    NNValue valupper;
    NNValue vallower;
    NNObjArray* newlist;
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
        idxupper = list->varray.count();
    }
    else
    {
        idxupper = valupper.asNumber();
    }
    if((idxlower < 0) || ((idxupper < 0) && ((long)(list->varray.count() + idxupper) < 0)) || (idxlower >= (long)list->varray.count()))
    {
        /* always return an empty list... */
        if(!willassign)
        {
            /* +1 for the list itself */
            gcs->stackPop(3);
        }
        gcs->stackPush(NNValue::fromObject(nn_object_makearray()));
        return true;
    }
    if(idxupper < 0)
    {
        idxupper = list->varray.count() + idxupper;
    }
    if(idxupper > (long)list->varray.count())
    {
        idxupper = list->varray.count();
    }
    newlist = nn_object_makearray();
    gcs->stackPush(NNValue::fromObject(newlist));
    for(i = idxlower; i < idxupper; i++)
    {
        newlist->varray.push(list->varray.get(i));
    }
    /* clear gc protect */
    gcs->stackPop();
    if(!willassign)
    {
        /* +1 for the list itself */
        gcs->stackPop(3);
    }
    gcs->stackPush(NNValue::fromObject(newlist));
    return true;
}

NEON_FORCEINLINE bool nn_vmutil_dogetrangedindexofstring(NNObjString* string, bool willassign)
{
    int end;
    int start;
    int length;
    int idxupper;
    int idxlower;
    NNValue valupper;
    NNValue vallower;
    auto gcs = SharedState::get();
    valupper = nn_vm_stackpeek(0);
    vallower = nn_vm_stackpeek(1);
    if(!(vallower.isNull() || vallower.isNumber()) || !(valupper.isNumber() || valupper.isNull()))
    {
        gcs->stackPop(2);
        return nn_except_throw("string range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower, false), nn_value_typename(valupper, false));
    }
    length = nn_string_getlength(string);
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
        gcs->stackPush(NNValue::fromObject(nn_string_internlen("", 0)));
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
    gcs->stackPush(NNValue::fromObject(nn_string_copylen(nn_string_getdata(string) + start, end - start)));
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_getrangedindex()
{
    bool isgotten;
    uint16_t willassign;
    NNValue vfrom;
    willassign = nn_vmbits_readbyte();
    isgotten = true;
    vfrom = nn_vm_stackpeek(2);
    if(vfrom.isObject())
    {
        switch(vfrom.asObject()->type)
        {
            case NNObject::OTYP_STRING:
            {
                if(!nn_vmutil_dogetrangedindexofstring(vfrom.asString(), willassign))
                {
                    return false;
                }
                break;
            }
            case NNObject::OTYP_ARRAY:
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

NEON_FORCEINLINE bool nn_vmutil_doindexgetdict(NNObjDict* dict, bool willassign)
{
    NNValue vindex;
    NNProperty* field;
    auto gcs = SharedState::get();
    vindex = nn_vm_stackpeek(0);
    field = nn_dict_getentry(dict, vindex);
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
    gcs->stackPush(NNValue::makeNull());
    return true;
}

NEON_FORCEINLINE bool nn_vmutil_doindexgetmodule(NNObjModule* module, bool willassign)
{
    NNValue vindex;
    NNValue result;
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
    return nn_except_throw("%s is undefined in module %s", nn_string_getdata(nn_value_tostring(vindex)), module->name);
}

NEON_FORCEINLINE bool nn_vmutil_doindexgetstring(NNObjString* string, bool willassign)
{
    bool okindex;
    int end;
    int start;
    int index;
    int maxlength;
    int realindex;
    NNValue vindex;
    NNObjRange* rng;
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
            gcs->stackPush(NNValue::makeNumber(rng->lower));
            gcs->stackPush(NNValue::makeNumber(rng->upper));
            return nn_vmutil_dogetrangedindexofstring(string, willassign);
        }
        gcs->stackPop(1);
        return nn_except_throw("strings are numerically indexed");
    }
    index = vindex.asNumber();
    maxlength = nn_string_getlength(string);
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
        gcs->stackPush(NNValue::fromObject(nn_string_copylen(nn_string_getdata(string) + start, end - start)));
    }
    else
    {
        gcs->stackPush(NNValue::makeNull());
    }
    return true;

    gcs->stackPop(1);
#if 0
        return nn_except_throw("string index %d out of range of %d", realindex, maxlength);
#else
    gcs->stackPush(NNValue::makeNull());
    return true;
#endif
}

NEON_FORCEINLINE bool nn_vmutil_doindexgetarray(NNObjArray* list, bool willassign)
{
    long index;
    NNValue finalval;
    NNValue vindex;
    NNObjRange* rng;
    vindex = nn_vm_stackpeek(0);
    auto gcs = SharedState::get();
    if(nn_util_unlikely(!vindex.isNumber()))
    {
        if(vindex.isRange())
        {
            rng = vindex.asRange();
            gcs->stackPop();
            gcs->stackPush(NNValue::makeNumber(rng->lower));
            gcs->stackPush(NNValue::makeNumber(rng->upper));
            return nn_vmutil_dogetrangedindexofarray(list, willassign);
        }
        gcs->stackPop();
        return nn_except_throw("list are numerically indexed");
    }
    index = vindex.asNumber();
    if(nn_util_unlikely(index < 0))
    {
        index = list->varray.count() + index;
    }
    if((index < (long)list->varray.count()) && (index >= 0))
    {
        finalval = list->varray.get(index);
    }
    else
    {
        finalval = NNValue::makeNull();
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

static NNProperty* nn_vmutil_checkoverloadrequirements(const char* ccallername, NNValue target, NNObjString* name)
{
    NNProperty* field;
    if(!target.isInstance())
    {
        fprintf(stderr, "%s: not an instance\n", ccallername);
        return nullptr;
    }
    field = nn_instance_getmethod(target.asInstance(), name);
    if(field == nullptr)
    {
        fprintf(stderr, "%s: failed to get '%s'\n", ccallername, nn_string_getdata(name));
        return nullptr;
    }
    if(!field->value.isCallable())
    {
        fprintf(stderr, "%s: field not callable\n", ccallername);
        return nullptr;
    }
    return field;
}

NEON_FORCEINLINE bool nn_vmutil_tryoverloadbasic(NNObjString* name, NNValue target, NNValue firstargvval, NNValue setvalue, bool willassign)
{
    size_t nargc;
    NNValue finalval;
    NNProperty* field;
    NNValue scrargv[3];
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

NEON_FORCEINLINE bool nn_vmutil_tryoverloadmath(NNObjString* name, NNValue target, NNValue right, bool willassign)
{
    return nn_vmutil_tryoverloadbasic(name, target, right, NNValue::makeNull(), willassign);
}

NEON_FORCEINLINE bool nn_vmutil_tryoverloadgeneric(NNObjString* name, NNValue target, bool willassign)
{
    NNValue setval;
    NNValue firstargval;
    firstargval = nn_vm_stackpeek(0);
    setval = nn_vm_stackpeek(1);
    return nn_vmutil_tryoverloadbasic(name, target, firstargval, setval, willassign);
}

NEON_FORCEINLINE bool nn_vmdo_indexget()
{
    bool isgotten;
    uint16_t willassign;
    NNValue thisval;
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
        switch(thisval.asObject()->type)
        {
            case NNObject::OTYP_STRING:
            {
                if(!nn_vmutil_doindexgetstring(thisval.asString(), willassign))
                {
                    return false;
                }
                break;
            }
            case NNObject::OTYP_ARRAY:
            {
                if(!nn_vmutil_doindexgetarray(thisval.asArray(), willassign))
                {
                    return false;
                }
                break;
            }
            case NNObject::OTYP_DICT:
            {
                if(!nn_vmutil_doindexgetdict(thisval.asDict(), willassign))
                {
                    return false;
                }
                break;
            }
            case NNObject::OTYP_MODULE:
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

NEON_FORCEINLINE bool nn_vmutil_dosetindexdict(NNObjDict* dict, NNValue index, NNValue value)
{
    auto gcs = SharedState::get();
    nn_dict_setentry(dict, index, value);
    /* pop the value, index and dict out */
    gcs->stackPop(3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = dict[index] = 10
    */
    gcs->stackPush(value);
    return true;
}

NEON_FORCEINLINE bool nn_vmutil_dosetindexmodule(NNObjModule* module, NNValue index, NNValue value)
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

NEON_FORCEINLINE bool nn_vmutil_doindexsetarray(NNObjArray* list, NNValue index, NNValue value)
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
    ocap = list->varray.capacity();
    ocnt = list->varray.count();
    rawpos = index.asNumber();
    position = rawpos;
    if(rawpos < 0)
    {
        rawpos = list->varray.count() + rawpos;
    }
    if(position < ocap && position > -(ocap))
    {
        list->varray.set(position, value);
        if(position >= ocnt)
        {
            list->varray.increaseBy(1);
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
        vasz = list->varray.count();
        if((position > vasz) || ((position == 0) && (vasz == 0)))
        {
            if(position == 0)
            {
                nn_array_push(list, NNValue::makeNull());
            }
            else
            {
                tmp = position + 1;
                while(tmp > vasz)
                {
                    nn_array_push(list, NNValue::makeNull());
                    tmp--;
                }
            }
        }
        fprintf(stderr, "setting value at position %ld (array count: %ld)\n", (long)position, (long)list->varray.count());
    }
    list->varray.set(position, value);
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
    //gcs->stackPush(NNValue::makeNull());
    //return true;
    */
}

NEON_FORCEINLINE bool nn_vmutil_dosetindexstring(NNObjString* os, NNValue index, NNValue value)
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
    oslen = nn_string_getlength(os);
    position = rawpos;
    if(rawpos < 0)
    {
        position = (oslen + rawpos);
    }
    if(position < oslen && position > -oslen)
    {
        nn_string_set(os, position, iv);
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
        nn_string_appendbyte(os, iv);
        gcs->stackPop(3);
        gcs->stackPush(value);
    }
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_indexset()
{
    bool isset;
    NNValue value;
    NNValue index;
    NNValue thisval;
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
        switch(thisval.asObject()->type)
        {
            case NNObject::OTYP_ARRAY:
            {
                if(!nn_vmutil_doindexsetarray(thisval.asArray(), index, value))
                {
                    return false;
                }
            }
            break;
            case NNObject::OTYP_STRING:
            {
                if(!nn_vmutil_dosetindexstring(thisval.asString(), index, value))
                {
                    return false;
                }
            }
            break;
            case NNObject::OTYP_DICT:
            {
                return nn_vmutil_dosetindexdict(thisval.asDict(), index, value);
            }
            break;
            case NNObject::OTYP_MODULE:
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

NEON_FORCEINLINE bool nn_vmutil_concatenate()
{
    NNValue vleft;
    NNValue vright;
    NNIOStream pr;
    NNObjString* result;
    auto gcs = SharedState::get();
    vright = nn_vm_stackpeek(0);
    vleft = nn_vm_stackpeek(1);
    NNIOStream::makeStackString(&pr);
    nn_iostream_printvalue(&pr, vleft, false, true);
    nn_iostream_printvalue(&pr, vright, false, true);
    result = pr.takeString();
    NNIOStream::destroy(&pr);
    gcs->stackPop(2);
    gcs->stackPush(NNValue::fromObject(result));
    return true;
}

#if 0
NEON_INLINE NNValue nn_vmutil_floordiv(double a, double b)
{
    int d;
    double r;
    d = (int)a / (int)b;
    r = d - ((d * b == a) & ((a < 0) ^ (b < 0)));
    return NNValue::makeNumber(r);
}
#endif

NEON_INLINE NNValue nn_vmutil_modulo(double a, double b)
{
    double r;
    r = fmod(a, b);
    if(r != 0 && ((r < 0) != (b < 0)))
    {
        r += b;
    }
    return NNValue::makeNumber(r);
}

NEON_INLINE NNValue nn_vmutil_pow(double a, double b)
{
    double r;
    r = pow(a, b);
    return NNValue::makeNumber(r);
}

NEON_FORCEINLINE NNProperty* nn_vmutil_getclassproperty(NNObjClass* klass, NNObjString* name, bool alsothrow)
{
    NNProperty* field;
    field = klass->instmethods.getfieldbyostr(name);
    if(field != nullptr)
    {
        if(nn_value_getmethodtype(field->value) == NNObjFunction::CTXTYPE_STATIC)
        {
            if(nn_util_methodisprivate(name))
            {
                if(alsothrow)
                {
                    nn_except_throw("cannot call private property '%s' of class %s", nn_string_getdata(name), nn_string_getdata(klass->name));
                }
                return nullptr;
            }
            return field;
        }
    }
    else
    {
        field = nn_class_getstaticproperty(klass, name);
        if(field != nullptr)
        {
            if(nn_util_methodisprivate(name))
            {
                if(alsothrow)
                {
                    nn_except_throw("cannot call private property '%s' of class %s", nn_string_getdata(name), nn_string_getdata(klass->name));
                }
                return nullptr;
            }
            return field;
        }
        else
        {
            field = nn_class_getstaticmethodfield(klass, name);
            if(field != nullptr)
            {
                return field;
            }
        }
    }
    if(alsothrow)
    {
        nn_except_throw("class %s does not have a static property or method named '%s'", nn_string_getdata(klass->name), nn_string_getdata(name));
    }
    return nullptr;
}

NEON_FORCEINLINE NNProperty* nn_vmutil_getproperty(NNValue peeked, NNObjString* name)
{
    NNProperty* field;
    auto gcs = SharedState::get();
    switch(peeked.asObject()->type)
    {
        case NNObject::OTYP_MODULE:
        {
            NNObjModule* module;
            module = peeked.asModule();
            field = module->deftable.getfieldbyostr(name);
            if(field != nullptr)
            {
                if(nn_util_methodisprivate(name))
                {
                    nn_except_throw("cannot get private module property '%s'", nn_string_getdata(name));
                    return nullptr;
                }
                return field;
            }
            nn_except_throw("module '%s' does not have a field named '%s'", nn_string_getdata(module->name), nn_string_getdata(name));
            return nullptr;
        }
        break;
        case NNObject::OTYP_CLASS:
        {
            NNObjClass* klass;
            klass = peeked.asClass();
            field = nn_vmutil_getclassproperty(klass, name, true);
            if(field != nullptr)
            {
                return field;
            }
            return nullptr;
        }
        break;
        case NNObject::OTYP_INSTANCE:
        {
            NNObjInstance* instance;
            instance = peeked.asInstance();
            field = instance->properties.getfieldbyostr(name);
            if(field != nullptr)
            {
                if(nn_util_methodisprivate(name))
                {
                    nn_except_throw("cannot call private property '%s' from instance of %s", nn_string_getdata(name), nn_string_getdata(instance->klass->name));
                    return nullptr;
                }
                return field;
            }
            if(nn_util_methodisprivate(name))
            {
                nn_except_throw("cannot bind private property '%s' to instance of %s", nn_string_getdata(name), nn_string_getdata(instance->klass->name));
                return nullptr;
            }
            if(nn_vmutil_bindmethod(instance->klass, name))
            {
                return field;
            }
            nn_except_throw("instance of class %s does not have a property or method named '%s'", nn_string_getdata(peeked.asInstance()->klass->name), nn_string_getdata(name));
            return nullptr;
        }
        break;
        case NNObject::OTYP_STRING:
        {
            field = nn_class_getpropertyfield(gcs->m_classprimstring, name);
            if(field == nullptr)
            {
                field = nn_vmutil_getclassproperty(gcs->m_classprimstring, name, false);
            }
            if(field != nullptr)
            {
                return field;
            }
            nn_except_throw("class String has no named property '%s'", nn_string_getdata(name));
            return nullptr;
        }
        break;
        case NNObject::OTYP_ARRAY:
        {
            field = nn_class_getpropertyfield(gcs->m_classprimarray, name);
            if(field == nullptr)
            {
                field = nn_vmutil_getclassproperty(gcs->m_classprimarray, name, false);
            }
            if(field != nullptr)
            {
                return field;
            }
            nn_except_throw("class Array has no named property '%s'", nn_string_getdata(name));
            return nullptr;
        }
        break;
        case NNObject::OTYP_RANGE:
        {
            field = nn_class_getpropertyfield(gcs->m_classprimrange, name);
            if(field == nullptr)
            {
                field = nn_vmutil_getclassproperty(gcs->m_classprimrange, name, false);
            }
            if(field != nullptr)
            {
                return field;
            }
            nn_except_throw("class Range has no named property '%s'", nn_string_getdata(name));
            return nullptr;
        }
        break;
        case NNObject::OTYP_DICT:
        {
            field = peeked.asDict()->htab.getfieldbyostr(name);
            if(field == nullptr)
            {
                field = nn_class_getpropertyfield(gcs->m_classprimdict, name);
            }
            if(field != nullptr)
            {
                return field;
            }
            nn_except_throw("unknown key or class Dict property '%s'", nn_string_getdata(name));
            return nullptr;
        }
        break;
        case NNObject::OTYP_FILE:
        {
            field = nn_class_getpropertyfield(gcs->m_classprimfile, name);
            if(field == nullptr)
            {
                field = nn_vmutil_getclassproperty(gcs->m_classprimfile, name, false);
            }
            if(field != nullptr)
            {
                return field;
            }
            nn_except_throw("class File has no named property '%s'", nn_string_getdata(name));
            return nullptr;
        }
        break;
        case NNObject::OTYP_FUNCBOUND:
        case NNObject::OTYP_FUNCCLOSURE:
        case NNObject::OTYP_FUNCSCRIPT:
        case NNObject::OTYP_FUNCNATIVE:
        {
            field = nn_class_getpropertyfield(gcs->m_classprimcallable, name);
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
            nn_except_throw("class Function has no named property '%s'", nn_string_getdata(name));
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

NEON_FORCEINLINE bool nn_vmdo_propertyget()
{
    NNValue peeked;
    NNProperty* field;
    NNObjString* name;
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
            if(field->type == NNProperty::FTYP_FUNCTION)
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
        nn_except_throw("'%s' of type %s does not have properties", nn_string_getdata(nn_value_tostring(peeked)), nn_value_typename(peeked, false));
    }
    return false;
}

NEON_FORCEINLINE bool nn_vmdo_propertygetself()
{
    NNValue peeked;
    NNObjString* name;
    NNObjClass* klass;
    NNObjInstance* instance;
    NNObjModule* module;
    NNProperty* field;
    auto gcs = SharedState::get();
    name = nn_vmbits_readstring();
    peeked = nn_vm_stackpeek(0);
    if(peeked.isInstance())
    {
        instance = peeked.asInstance();
        field = instance->properties.getfieldbyostr(name);
        if(field != nullptr)
        {
            /* pop the instance... */
            gcs->stackPop();
            gcs->stackPush(field->value);
            return true;
        }
        if(nn_vmutil_bindmethod(instance->klass, name))
        {
            return true;
        }
        nn_vmmac_tryraise(false, "instance of class %s does not have a property or method named '%s'", nn_string_getdata(peeked.asInstance()->klass->name), nn_string_getdata(name));
        return false;
    }
    else if(peeked.isClass())
    {
        klass = peeked.asClass();
        field = klass->instmethods.getfieldbyostr(name);
        if(field != nullptr)
        {
            if(nn_value_getmethodtype(field->value) == NNObjFunction::CTXTYPE_STATIC)
            {
                /* pop the class... */
                gcs->stackPop();
                gcs->stackPush(field->value);
                return true;
            }
        }
        else
        {
            field = nn_class_getstaticproperty(klass, name);
            if(field != nullptr)
            {
                /* pop the class... */
                gcs->stackPop();
                gcs->stackPush(field->value);
                return true;
            }
        }
        nn_vmmac_tryraise(false, "class %s does not have a static property or method named '%s'", nn_string_getdata(klass->name), nn_string_getdata(name));
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
        nn_vmmac_tryraise(false, "module '%s' does not have a field named '%s'", nn_string_getdata(module->name), nn_string_getdata(name));
        return false;
    }
    nn_vmmac_tryraise(false, "'%s' of type %s does not have properties", nn_string_getdata(nn_value_tostring(peeked)), nn_value_typename(peeked, false));
    return false;
}

NEON_FORCEINLINE bool nn_vmdo_propertyset()
{
    NNValue value;
    NNValue vtarget;
    NNValue vpeek;
    NNObjClass* klass;
    NNObjString* name;
    NNObjDict* dict;
    NNObjInstance* instance;
    auto gcs = SharedState::get();
    vtarget = nn_vm_stackpeek(1);
    name = nn_vmbits_readstring();
    vpeek = nn_vm_stackpeek(0);
    if(vtarget.isInstance())
    {
        instance = vtarget.asInstance();
        nn_instance_defproperty(instance, name, vpeek);
        value = gcs->stackPop();
        /* removing the instance object */
        gcs->stackPop();
        gcs->stackPush(value);
    }
    else if(vtarget.isDict())
    {
        dict = vtarget.asDict();
        nn_dict_setentry(dict, NNValue::fromObject(name), vpeek);
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
            klass = vtarget.asInstance()->klass;
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
            nn_class_defmethod(klass, name, vpeek);
        }
        else
        {
            nn_class_defproperty(klass, name, vpeek);
        }
        value = gcs->stackPop();
        /* removing the class object */
        gcs->stackPop();
        gcs->stackPush(value);
    }

    return true;
}

NEON_FORCEINLINE double nn_vmutil_valtonum(NNValue v)
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

NEON_FORCEINLINE uint32_t nn_vmutil_valtouint(NNValue v)
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

NEON_FORCEINLINE long nn_vmutil_valtoint(NNValue v)
{
    return (long)nn_vmutil_valtonum(v);
}

NEON_FORCEINLINE bool nn_vmdo_dobinarydirect()
{
    bool isfail;
    bool willassign;
    int64_t ibinright;
    int64_t ibinleft;
    uint32_t ubinright;
    uint32_t ubinleft;
    double dbinright;
    double dbinleft;
    NNInstruction::OpCode instruction;
    NNValue res;
    NNValue binvalleft;
    NNValue binvalright;
    willassign = false;
    auto gcs = SharedState::get();
    instruction = (NNInstruction::OpCode)gcs->m_vmstate.currentinstr.code;
    binvalright = nn_vm_stackpeek(0);
    binvalleft = nn_vm_stackpeek(1);
    if(nn_util_unlikely(binvalleft.isInstance()))
    {
        switch(instruction)
        {
            case NNInstruction::OPC_PRIMADD:
            {
                if(nn_vmutil_tryoverloadmath(gcs->m_defaultstrings.nmadd, binvalleft, binvalright, willassign))
                {
                    return true;
                }
            }
            break;
            case NNInstruction::OPC_PRIMSUBTRACT:
            {
                if(nn_vmutil_tryoverloadmath(gcs->m_defaultstrings.nmsub, binvalleft, binvalright, willassign))
                {
                    return true;
                }
            }
            break;
            case NNInstruction::OPC_PRIMDIVIDE:
            {
                if(nn_vmutil_tryoverloadmath(gcs->m_defaultstrings.nmdiv, binvalleft, binvalright, willassign))
                {
                    return true;
                }
            }
            break;
            case NNInstruction::OPC_PRIMMULTIPLY:
            {
                if(nn_vmutil_tryoverloadmath(gcs->m_defaultstrings.nmmul, binvalleft, binvalright, willassign))
                {
                    return true;
                }
            }
            break;
            case NNInstruction::OPC_PRIMAND:
            {
                if(nn_vmutil_tryoverloadmath(gcs->m_defaultstrings.nmband, binvalleft, binvalright, willassign))
                {
                    return true;
                }
            }
            break;
            case NNInstruction::OPC_PRIMOR:
            {
                if(nn_vmutil_tryoverloadmath(gcs->m_defaultstrings.nmbor, binvalleft, binvalright, willassign))
                {
                    return true;
                }
            }
            break;
            case NNInstruction::OPC_PRIMBITXOR:
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
    res = NNValue::makeNull();
    switch(instruction)
    {
        case NNInstruction::OPC_PRIMADD:
        {
            dbinright = nn_vmutil_valtonum(binvalright);
            dbinleft = nn_vmutil_valtonum(binvalleft);
            res = NNValue::makeNumber(dbinleft + dbinright);
        }
        break;
        case NNInstruction::OPC_PRIMSUBTRACT:
        {
            dbinright = nn_vmutil_valtonum(binvalright);
            dbinleft = nn_vmutil_valtonum(binvalleft);
            res = NNValue::makeNumber(dbinleft - dbinright);
        }
        break;
        case NNInstruction::OPC_PRIMDIVIDE:
        {
            dbinright = nn_vmutil_valtonum(binvalright);
            dbinleft = nn_vmutil_valtonum(binvalleft);
            res = NNValue::makeNumber(dbinleft / dbinright);
        }
        break;
        case NNInstruction::OPC_PRIMMULTIPLY:
        {
            dbinright = nn_vmutil_valtonum(binvalright);
            dbinleft = nn_vmutil_valtonum(binvalleft);
            res = NNValue::makeNumber(dbinleft * dbinright);
        }
        break;
        case NNInstruction::OPC_PRIMAND:
        {
            ibinright = nn_vmutil_valtoint(binvalright);
            ibinleft = nn_vmutil_valtoint(binvalleft);
            res = NNValue::makeNumber(ibinleft & ibinright);
        }
        break;
        case NNInstruction::OPC_PRIMOR:
        {
            ibinright = nn_vmutil_valtoint(binvalright);
            ibinleft = nn_vmutil_valtoint(binvalleft);
            res = NNValue::makeNumber(ibinleft | ibinright);
        }
        break;
        case NNInstruction::OPC_PRIMBITXOR:
        {
            ibinright = nn_vmutil_valtoint(binvalright);
            ibinleft = nn_vmutil_valtoint(binvalleft);
            res = NNValue::makeNumber(ibinleft ^ ibinright);
        }
        break;
        case NNInstruction::OPC_PRIMSHIFTLEFT:
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
            res = NNValue::makeNumber(ubinleft << ubinright);
        }
        break;
        case NNInstruction::OPC_PRIMSHIFTRIGHT:
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
            res = NNValue::makeNumber(ubinleft >> ubinright);
        }
        break;
        case NNInstruction::OPC_PRIMGREATER:
        {
            dbinright = nn_vmutil_valtonum(binvalright);
            dbinleft = nn_vmutil_valtonum(binvalleft);
            res = NNValue::makeBool(dbinleft > dbinright);
        }
        break;
        case NNInstruction::OPC_PRIMLESSTHAN:
        {
            dbinright = nn_vmutil_valtonum(binvalright);
            dbinleft = nn_vmutil_valtonum(binvalleft);
            res = NNValue::makeBool(dbinleft < dbinright);
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

NEON_FORCEINLINE bool nn_vmdo_globaldefine()
{
    NNValue val;
    NNObjString* name;
    name = nn_vmbits_readstring();
    val = nn_vm_stackpeek(0);
    auto gcs = SharedState::get();
    auto tab = &gcs->m_vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module->deftable;
    tab->set(NNValue::fromObject(name), val);
    gcs->stackPop();
#if (defined(DEBUG_TABLE) && DEBUG_TABLE) || 0
    ->print(gcs->m_debugwriter, &gcs->m_declaredglobals, "globals");
#endif
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_globalget()
{
    NNObjString* name;
    NNProperty* field;
    name = nn_vmbits_readstring();
    auto gcs = SharedState::get();
    auto tab = &gcs->m_vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module->deftable;
    field = tab->getfieldbyostr(name);
    if(field == nullptr)
    {
        field = gcs->m_declaredglobals.getfieldbyostr(name);
        if(field == nullptr)
        {
            nn_except_throwclass(gcs->m_exceptions.stdexception, "global name '%s' is not defined", nn_string_getdata(name));
            return false;
        }
    }
    gcs->stackPush(field->value);
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_globalset()
{
    NNObjString* name;
    NNObjModule* module;
    name = nn_vmbits_readstring();
    auto gcs = SharedState::get();
    module = gcs->m_vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module;
    auto table = &module->deftable;
    if(table->set(NNValue::fromObject(name), nn_vm_stackpeek(0)))
    {
        if(gcs->m_conf.enablestrictmode)
        {
            table->remove(NNValue::fromObject(name));
            nn_vmmac_tryraise(false, "global name '%s' was not declared", nn_string_getdata(name));
            return false;
        }
    }
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_localget()
{
    size_t ssp;
    uint16_t slot;
    NNValue val;
    slot = nn_vmbits_readshort();
    auto gcs = SharedState::get();
    ssp = gcs->m_vmstate.currentframe->stackslotpos;
    val = gcs->m_vmstate.stackvalues[ssp + slot];
    gcs->stackPush(val);
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_localset()
{
    size_t ssp;
    uint16_t slot;
    NNValue peeked;
    slot = nn_vmbits_readshort();
    peeked = nn_vm_stackpeek(0);
    auto gcs = SharedState::get();
    ssp = gcs->m_vmstate.currentframe->stackslotpos;
    gcs->m_vmstate.stackvalues[ssp + slot] = peeked;
    return true;
}

/*NNInstruction::OPC_FUNCARGOPTIONAL*/
NEON_FORCEINLINE bool nn_vmdo_funcargoptional()
{
    size_t ssp;
    size_t putpos;
    uint16_t slot;
    NNValue cval;
    NNValue peeked;
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
            NNIOStream* pr = gcs->m_stderrprinter;
            pr->format("funcargoptional: slot=%d putpos=%ld cval=<", slot, putpos);
            nn_iostream_printvalue(pr, cval, true, false);
            pr->format(">, peeked=<");
            nn_iostream_printvalue(pr, peeked, true, false);
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

NEON_FORCEINLINE bool nn_vmdo_funcargget()
{
    size_t ssp;
    uint16_t slot;
    NNValue val;
    slot = nn_vmbits_readshort();
    auto gcs = SharedState::get();
    ssp = gcs->m_vmstate.currentframe->stackslotpos;
    val = gcs->m_vmstate.stackvalues[ssp + slot];
    gcs->stackPush(val);
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_funcargset()
{
    size_t ssp;
    uint16_t slot;
    NNValue peeked;
    slot = nn_vmbits_readshort();
    peeked = nn_vm_stackpeek(0);
    auto gcs = SharedState::get();
    ssp = gcs->m_vmstate.currentframe->stackslotpos;
    gcs->m_vmstate.stackvalues[ssp + slot] = peeked;
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_makeclosure()
{
    size_t i;
    int upvidx;
    size_t ssp;
    uint16_t islocal;
    NNValue thisval;
    NNValue* upvals;
    NNObjFunction* function;
    NNObjFunction* closure;
    function = nn_vmbits_readconst().asFunction();
#if 0
        thisval = nn_vm_stackpeek(3);
#else
    thisval = NNValue::makeNull();
#endif
    closure = nn_object_makefuncclosure(function, thisval);
    auto gcs = SharedState::get();
    gcs->stackPush(NNValue::fromObject(closure));
    for(i = 0; i < (size_t)closure->upvalcount; i++)
    {
        islocal = nn_vmbits_readbyte();
        upvidx = nn_vmbits_readshort();
        if(islocal)
        {
            ssp = gcs->m_vmstate.currentframe->stackslotpos;
            upvals = &gcs->m_vmstate.stackvalues[ssp + upvidx];
            closure->fnclosure.upvalues[i] = nn_vmutil_upvaluescapture(upvals, upvidx);
        }
        else
        {
            closure->fnclosure.upvalues[i] = gcs->m_vmstate.currentframe->closure->fnclosure.upvalues[upvidx];
        }
    }
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_makearray()
{
    int i;
    int count;
    NNObjArray* array;
    count = nn_vmbits_readshort();
    array = nn_object_makearray();
    auto gcs = SharedState::get();
    gcs->m_vmstate.stackvalues[gcs->m_vmstate.stackidx + (-count - 1)] = NNValue::fromObject(array);
    for(i = count - 1; i >= 0; i--)
    {
        nn_array_push(array, nn_vm_stackpeek(i));
    }
    gcs->stackPop(count);
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_makedict()
{
    size_t i;
    size_t count;
    size_t realcount;
    NNValue name;
    NNValue value;
    NNObjDict* dict;
    /* 1 for key, 1 for value */
    realcount = nn_vmbits_readshort();
    count = realcount * 2;
    dict = nn_object_makedict();
    auto gcs = SharedState::get();
    gcs->m_vmstate.stackvalues[gcs->m_vmstate.stackidx + (-count - 1)] = NNValue::fromObject(dict);
    for(i = 0; i < count; i += 2)
    {
        name = gcs->m_vmstate.stackvalues[gcs->m_vmstate.stackidx + (-count + i)];
        if(!name.isString() && !name.isNumber() && !name.isBool())
        {
            nn_vmmac_tryraise(false, "dictionary key must be one of string, number or boolean");
            return false;
        }
        value = gcs->m_vmstate.stackvalues[gcs->m_vmstate.stackidx + (-count + i + 1)];
        nn_dict_setentry(dict, name, value);
    }
    gcs->stackPop(count);
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_dobinaryfunc(const char* opname, nnbinopfunc_t opfn)
{
    double dbinright;
    double dbinleft;
    NNValue binvalright;
    NNValue binvalleft;
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
    #define NEON_SETDISPATCHIDX(idx, val) [NNInstruction::idx] = val
    #define VM_MAKELABEL(op) LABEL_##op
    #define VM_CASE(op) VM_MAKELABEL(op) :
    #if 1
        #define VM_DISPATCH() goto readnextinstruction
    #else
        #define VM_DISPATCH() continue
    #endif
#else
    #define VM_CASE(op) case NNInstruction::op:
    #define VM_DISPATCH() break
#endif

NNStatus nn_vm_runvm(int exitframe, NNValue* rv)
{
    int iterpos;
    int printpos;
    int ofs;
    /*
     * this variable is a NOP; it only exists to ensure that functions outside of the
     * switch tree are not calling nn_vmmac_exitvm(), as its behavior could be undefined.
     */
    bool you_are_calling_exit_vm_outside_of_runvm;
    NNValue* dbgslot;
    NNInstruction currinstr;
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
            ofs = (int)(vmgcs->m_vmstate.currentframe->inscode - vmgcs->m_vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.blob.instrucs);
            nn_dbg_printinstructionat(vmgcs->m_debugwriter, &vmgcs->m_vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.blob, ofs);
            fprintf(stderr, "stack (before)=[\n");
            iterpos = 0;
            dbgslot = vmgcs->m_vmstate.stackvalues;
            while(dbgslot < &vmgcs->m_vmstate.stackvalues[vmgcs->m_vmstate.stackidx])
            {
                printpos = iterpos + 1;
                iterpos++;
                fprintf(stderr, "  [%s%d%s] ", nn_util_color(NEON_COLOR_YELLOW), printpos, nn_util_color(NEON_COLOR_RESET));
                vmgcs->m_debugwriter->format("%s", nn_util_color(NEON_COLOR_YELLOW));
                nn_iostream_printvalue(vmgcs->m_debugwriter, *dbgslot, true, false);
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
                NNValue result;
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
                NNValue constant;
                constant = nn_vmbits_readconst();
                vmgcs->stackPush(constant);
            }
            VM_DISPATCH();
            VM_CASE(OPC_PRIMADD)
            {
                NNValue valright;
                NNValue valleft;
                NNValue result;
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
                    result = NNValue::fromObject(nn_vmutil_combinearrays(valleft.asArray(), valright.asArray()));
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
                NNValue peekleft;
                NNValue peekright;
                NNValue result;
                NNObjString* string;
                NNObjArray* list;
                NNObjArray* newlist;
                peekright = nn_vm_stackpeek(0);
                peekleft = nn_vm_stackpeek(1);
                if(peekleft.isString() && peekright.isNumber())
                {
                    dbnum = peekright.asNumber();
                    string = peekleft.asString();
                    result = NNValue::fromObject(nn_vmutil_multiplystring(string, dbnum));
                    vmgcs->stackPop(2);
                    vmgcs->stackPush(result);
                    VM_DISPATCH();
                }
                else if(peekleft.isArray() && peekright.isNumber())
                {
                    intnum = (int)peekright.asNumber();
                    vmgcs->stackPop();
                    list = peekleft.asArray();
                    newlist = nn_object_makearray();
                    vmgcs->stackPush(NNValue::fromObject(newlist));
                    nn_vmutil_multiplyarray(list, newlist, intnum);
                    vmgcs->stackPop(2);
                    vmgcs->stackPush(NNValue::fromObject(newlist));
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
                NNValue peeked;
                peeked = nn_vm_stackpeek(0);
                if(!peeked.isNumber())
                {
                    nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "operator - not defined for object of type %s", nn_value_typename(peeked, false));
                    VM_DISPATCH();
                }
                peeked = vmgcs->stackPop();
                vmgcs->stackPush(NNValue::makeNumber(-peeked.asNumber()));
            }
            VM_DISPATCH();
            VM_CASE(OPC_PRIMBITNOT)
            {
                NNValue peeked;
                peeked = nn_vm_stackpeek(0);
                if(!peeked.isNumber())
                {
                    nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "operator ~ not defined for object of type %s", nn_value_typename(peeked, false));
                    VM_DISPATCH();
                }
                peeked = vmgcs->stackPop();
                vmgcs->stackPush(NNValue::makeNumber(~((int)peeked.asNumber())));
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
                vmgcs->stackPush(NNValue::makeNumber(1));
            }
            VM_DISPATCH();
            /* comparisons */
            VM_CASE(OPC_EQUAL)
            {
                NNValue a;
                NNValue b;
                b = vmgcs->stackPop();
                a = vmgcs->stackPop();
                vmgcs->stackPush(NNValue::makeBool(nn_value_compare(a, b)));
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
                NNValue val;
                val = vmgcs->stackPop();
                vmgcs->stackPush(NNValue::makeBool(val.isFalse()));
            }
            VM_DISPATCH();
            VM_CASE(OPC_PUSHNULL)
            {
                vmgcs->stackPush(NNValue::makeNull());
            }
            VM_DISPATCH();
            VM_CASE(OPC_PUSHEMPTY)
            {
                vmgcs->stackPush(NNValue::makeNull());
            }
            VM_DISPATCH();
            VM_CASE(OPC_PUSHTRUE)
            {
                vmgcs->stackPush(NNValue::makeBool(true));
            }
            VM_DISPATCH();
            VM_CASE(OPC_PUSHFALSE)
            {
                vmgcs->stackPush(NNValue::makeBool(false));
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
                NNValue val;
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
                NNValue val;
                val = nn_vm_stackpeek(0);
                nn_iostream_printvalue(vmgcs->m_stdoutprinter, val, vmgcs->m_isrepl, true);
                if(!val.isNull())
                {
                    vmgcs->m_stdoutprinter->writeString("\n");
                }
                vmgcs->stackPop();
            }
            VM_DISPATCH();
            VM_CASE(OPC_STRINGIFY)
            {
                NNValue peeked;
                NNValue popped;
                NNObjString* os;
                peeked = nn_vm_stackpeek(0);
                if(!peeked.isString() && !peeked.isNull())
                {
                    popped = vmgcs->stackPop();
                    os = nn_value_tostring(popped);
                    if(nn_string_getlength(os) != 0)
                    {
                        vmgcs->stackPush(NNValue::fromObject(os));
                    }
                    else
                    {
                        vmgcs->stackPush(NNValue::makeNull());
                    }
                }
            }
            VM_DISPATCH();
            VM_CASE(OPC_DUPONE)
            {
                NNValue val;
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
                NNValue first;
                NNValue second;
                NNObjClass* vclass;
                NNObjClass* checkclass;
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
                vmgcs->stackPush(NNValue::makeBool(rt));
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
                NNObjFunction* closure;
                upvidx = nn_vmbits_readshort();
                closure = vmgcs->m_vmstate.currentframe->closure;
                if(upvidx < closure->upvalcount)
                {
                    vmgcs->stackPush((closure->fnclosure.upvalues[upvidx]->location));
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
                NNValue val;
                upvidx = nn_vmbits_readshort();
                val = nn_vm_stackpeek(0);
                vmgcs->m_vmstate.currentframe->closure->fnclosure.upvalues[upvidx]->location = val;
            }
            VM_DISPATCH();
            VM_CASE(OPC_CALLFUNCTION)
            {
                size_t argcount;
                NNValue callee;
                NNValue thisval;
                thisval = NNValue::makeNull();
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
                NNObjString* method;
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
                NNValue thisval;
                thisval = nn_vm_stackpeek(3);
                vmgcs->m_debugwriter->format("CLASSGETTHIS: thisval=");
                nn_iostream_printvalue(vmgcs->m_debugwriter, thisval, true, false);
                vmgcs->m_debugwriter->format("\n");
                vmgcs->stackPush(thisval);
            }
            VM_DISPATCH();
            VM_CASE(OPC_CLASSINVOKETHIS)
            {
                size_t argcount;
                NNObjString* method;
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
                NNValue pushme;
                NNObjString* name;
                NNObjClass* klass;
                NNProperty* field;
                haveval = false;
                name = nn_vmbits_readstring();
                field = vmgcs->m_vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module->deftable.getfieldbyostr(name);
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
                    klass = nn_object_makeclass(name, vmgcs->m_classprimobject);
                    pushme = NNValue::fromObject(klass);
                }
                vmgcs->stackPush(pushme);
            }
            VM_DISPATCH();
            VM_CASE(OPC_MAKEMETHOD)
            {
                NNObjString* name;
                name = nn_vmbits_readstring();
                nn_vmutil_definemethod(name);
            }
            VM_DISPATCH();
            VM_CASE(OPC_CLASSPROPERTYDEFINE)
            {
                int isstatic;
                NNObjString* name;
                name = nn_vmbits_readstring();
                isstatic = nn_vmbits_readbyte();
                nn_vmutil_defineproperty(name, isstatic == 1);
            }
            VM_DISPATCH();
            VM_CASE(OPC_CLASSINHERIT)
            {
                NNValue vclass;
                NNValue vsuper;
                NNObjClass* superclass;
                NNObjClass* subclass;
                vsuper = nn_vm_stackpeek(1);
                if(!vsuper.isClass())
                {
                    nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "cannot inherit from non-class object");
                    VM_DISPATCH();
                }
                vclass = nn_vm_stackpeek(0);
                superclass = vsuper.asClass();
                subclass = vclass.asClass();
                nn_class_inheritfrom(subclass, superclass);
                /* pop the subclass */
                vmgcs->stackPop();
            }
            VM_DISPATCH();
            VM_CASE(OPC_CLASSGETSUPER)
            {
                NNValue vclass;
                NNObjClass* klass;
                NNObjString* name;
                name = nn_vmbits_readstring();
                vclass = nn_vm_stackpeek(0);
                klass = vclass.asClass();
                if(!nn_vmutil_bindmethod(klass->superclass, name))
                {
                    nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "class '%s' does not have a function '%s'", nn_string_getdata(klass->name), nn_string_getdata(name));
                }
            }
            VM_DISPATCH();
            VM_CASE(OPC_CLASSINVOKESUPER)
            {
                size_t argcount;
                NNValue vclass;
                NNObjClass* klass;
                NNObjString* method;
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
                NNValue vclass;
                NNObjClass* klass;
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
                NNValue vupper;
                NNValue vlower;
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
                vmgcs->stackPush(NNValue::fromObject(nn_object_makerange(lower, upper)));
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
                NNValue res;
                NNValue vname;
                NNObjString* name;
                NNObjModule* mod;
                vname = nn_vm_stackpeek(0);
                name = vname.asString();
                fprintf(stderr, "IMPORTIMPORT: name='%s'\n", nn_string_getdata(name));
                mod = nn_import_loadmodulescript(vmgcs->m_topmodule, name);
                fprintf(stderr, "IMPORTIMPORT: mod='%p'\n", (void*)mod);
                if(mod == nullptr)
                {
                    res = NNValue::makeNull();
                }
                else
                {
                    res = NNValue::fromObject(mod);
                }
                vmgcs->stackPush(res);
            }
            VM_DISPATCH();
            VM_CASE(OPC_TYPEOF)
            {
                NNValue res;
                NNValue thing;
                const char* result;
                thing = vmgcs->stackPop();
                result = nn_value_typename(thing, false);
                res = NNValue::fromObject(nn_string_copycstr(result));
                vmgcs->stackPush(res);
            }
            VM_DISPATCH();
            VM_CASE(OPC_ASSERT)
            {
                NNValue message;
                NNValue expression;
                message = vmgcs->stackPop();
                expression = vmgcs->stackPop();
                if(expression.isFalse())
                {
                    if(!message.isNull())
                    {
                        nn_except_throwclass(vmgcs->m_exceptions.asserterror, nn_string_getdata(nn_value_tostring(message)));
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
                NNValue peeked;
                NNValue stacktrace;
                NNObjInstance* instance;
                peeked = nn_vm_stackpeek(0);
                isok = (peeked.isInstance() || nn_util_isinstanceof(peeked.asInstance()->klass, vmgcs->m_exceptions.stdexception));
                if(!isok)
                {
                    nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "instance of Exception expected");
                    VM_DISPATCH();
                }
                stacktrace = nn_except_getstacktrace();
                instance = peeked.asInstance();
                nn_instance_defproperty(instance, nn_string_intern("stacktrace"), stacktrace);
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
                NNValue value;
                NNObjString* type;
                NNObjClass* exclass;
                haveclass = false;
                exclass = nullptr;
                type = nn_vmbits_readstring();
                addr = nn_vmbits_readshort();
                finaddr = nn_vmbits_readshort();
                if(addr != 0)
                {
                    value = NNValue::makeNull();
                    if(!vmgcs->m_declaredglobals.get(NNValue::fromObject(type), &value))
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
                        if(!vmgcs->m_vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module->deftable.get(NNValue::fromObject(type), &value) || !value.isClass())
                        {
                            nn_vmmac_tryraise(NEON_STATUS_FAILRUNTIME, "object of type '%s' is not an exception", nn_string_getdata(type));
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
                vmgcs->m_vmstate.currentframe->handlercount--;
            }
            VM_DISPATCH();
            VM_CASE(OPC_EXPUBLISHTRY)
            {
                vmgcs->m_vmstate.currentframe->handlercount--;
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
                NNValue expr;
                NNValue value;
                NNObjSwitch* sw;
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

int nn_nestcall_prepare(NNValue callable, NNValue mthobj, NNValue* callarr, int maxcallarr)
{
    int arity;
    NNObjFunction* ofn;
    (void)maxcallarr;
    arity = 0;
    ofn = callable.asFunction();
    if(callable.isFuncclosure())
    {
        arity = ofn->fnclosure.scriptfunc->fnscriptfunc.arity;
    }
    else if(callable.isFuncscript())
    {
        arity = ofn->fnscriptfunc.arity;
    }
    else if(callable.isFuncnative())
    {
#if 0
            arity = ofn;
#endif
    }
    if(arity > 0)
    {
        callarr[0] = NNValue::makeNull();
        if(arity > 1)
        {
            callarr[1] = NNValue::makeNull();
            if(arity > 2)
            {
                callarr[2] = mthobj;
            }
        }
    }
    return arity;
}

/* helper function to access call outside the file. */
bool nn_nestcall_callfunction(NNValue callable, NNValue thisval, NNValue* argv, size_t argc, NNValue* dest, bool fromoper)
{
    bool needvm;
    size_t i;
    int64_t pidx;
    NNStatus status;
    NNValue rtv;
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

void nn_state_buildprocessinfo()
{
    enum
    {
        kMaxBuf = 1024
    };
    char* pathp;
    char pathbuf[kMaxBuf];
    auto gcs = SharedState::get();
    gcs->m_processinfo = Memory::make<NNProcessInfo>();
    gcs->m_processinfo->cliscriptfile = nullptr;
    gcs->m_processinfo->cliscriptdirectory = nullptr;
    gcs->m_processinfo->cliargv = nn_object_makearray();
    {
        pathp = osfn_getcwd(pathbuf, kMaxBuf);
        if(pathp == nullptr)
        {
            pathp = (char*)".";
        }
        gcs->m_processinfo->cliexedirectory = nn_string_copycstr(pathp);
    }
    {
        gcs->m_processinfo->cliprocessid = osfn_getpid();
    }
    {
        {
            gcs->m_processinfo->filestdout = nn_object_makefile(stdout, true, "<stdout>", "wb");
            nn_state_defglobalvalue("STDOUT", NNValue::fromObject(gcs->m_processinfo->filestdout));
        }
        {
            gcs->m_processinfo->filestderr = nn_object_makefile(stderr, true, "<stderr>", "wb");
            nn_state_defglobalvalue("STDERR", NNValue::fromObject(gcs->m_processinfo->filestderr));
        }
        {
            gcs->m_processinfo->filestdin = nn_object_makefile(stdin, true, "<stdin>", "rb");
            nn_state_defglobalvalue("STDIN", NNValue::fromObject(gcs->m_processinfo->filestdin));
        }
    }
}

void nn_state_updateprocessinfo()
{
    char* prealpath;
    char* prealdir;
    auto gcs = SharedState::get();
    if(gcs->m_rootphysfile != nullptr)
    {
        prealpath = osfn_realpath(gcs->m_rootphysfile, nullptr);
        prealdir = osfn_dirname(prealpath);
        gcs->m_processinfo->cliscriptfile = nn_string_copycstr(prealpath);
        gcs->m_processinfo->cliscriptdirectory = nn_string_copycstr(prealdir);
        nn_memory_free(prealpath);
        nn_memory_free(prealdir);
    }
    if(gcs->m_processinfo->cliscriptdirectory != nullptr)
    {
        nn_state_addmodulesearchpathobj(gcs->m_processinfo->cliscriptdirectory);
    }
}

bool nn_state_makestate()
{
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
        gcs->m_lastreplvalue = NNValue::makeNull();
    }
    /*
     * initialize various printer instances
     */
    {
        gcs->m_stdoutprinter = NNIOStream::makeIO(stdout, false);
        gcs->m_stdoutprinter->shouldflush = false;
        gcs->m_stderrprinter = NNIOStream::makeIO(stderr, false);
        gcs->m_debugwriter = NNIOStream::makeIO(stderr, false);
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
        gcs->m_topmodule = nn_module_make("", "<state>", false, true);
    }
    {
        gcs->m_defaultstrings.nmconstructor = nn_string_intern("constructor");
        gcs->m_defaultstrings.nmindexget = nn_string_copycstr("__indexget__");
        gcs->m_defaultstrings.nmindexset = nn_string_copycstr("__indexset__");
        gcs->m_defaultstrings.nmadd = nn_string_copycstr("__add__");
        gcs->m_defaultstrings.nmsub = nn_string_copycstr("__sub__");
        gcs->m_defaultstrings.nmdiv = nn_string_copycstr("__div__");
        gcs->m_defaultstrings.nmmul = nn_string_copycstr("__mul__");
        gcs->m_defaultstrings.nmband = nn_string_copycstr("__band__");
        gcs->m_defaultstrings.nmbor = nn_string_copycstr("__bor__");
        gcs->m_defaultstrings.nmbxor = nn_string_copycstr("__bxor__");
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
        gcs->m_envdict = nn_object_makedict();
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
    nn_state_buildprocessinfo();
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
            fprintf(stderr, "in nn_state_destroystate: "); \
            fprintf(stderr, __VA_ARGS__);             \
            fprintf(stderr, "\n");                    \
        }
#else
    #define destrdebug(...)
#endif
void nn_state_destroystate()
{
    auto gcs = SharedState::get();
    destrdebug("destroying m_importpath...");
    gcs->m_importpath.deInit(false);
    destrdebug("destroying linked objects...");
    SharedState::gcLinkedObjectsDestroy();
    /* since object in module can exist in m_declaredglobals, it must come before */
    destrdebug("destroying module table...");
    gcs->m_openedmodules.deinit();
    destrdebug("destroying globals table...");
    gcs->m_declaredglobals.deinit();
    destrdebug("destroying strings table...");
    gcs->m_allocatedstrings.deinit();
    destrdebug("destroying m_stdoutprinter...");
    NNIOStream::destroy(gcs->m_stdoutprinter);
    destrdebug("destroying m_stderrprinter...");
    NNIOStream::destroy(gcs->m_stderrprinter);
    destrdebug("destroying m_debugwriter...");
    NNIOStream::destroy(gcs->m_debugwriter);
    destrdebug("destroying framevalues...");
    nn_memory_free(gcs->m_vmstate.framevalues);
    destrdebug("destroying stackvalues...");
    nn_memory_free(gcs->m_vmstate.stackvalues);
    nn_memory_free(gcs->m_processinfo);
    destrdebug("destroying state...");
    SharedState::destroy();
    destrdebug("done destroying!");
}

NNObjFunction* nn_state_compilesource(NNObjModule* module, bool fromeval, const char* source, bool toplevel)
{
    NNBlob blob;
    NNObjFunction* function;
    NNObjFunction* closure;
    (void)toplevel;
    auto gcs = SharedState::get();
    NNBlob::init(&blob);
    function = nn_astutil_compilesource(module, source, &blob, false, fromeval);
    if(function == nullptr)
    {
        NNBlob::destroy(&blob);
        return nullptr;
    }
    if(!fromeval)
    {
        gcs->stackPush(NNValue::fromObject(function));
    }
    else
    {
        function->name = nn_string_intern("(evaledcode)");
    }
    closure = nn_object_makefuncclosure(function, NNValue::makeNull());
    if(!fromeval)
    {
        gcs->stackPop();
        gcs->stackPush(NNValue::fromObject(closure));
    }
    NNBlob::destroy(&blob);
    return closure;
}

NNStatus nn_state_execsource(NNObjModule* module, const char* source, const char* filename, NNValue* dest)
{
    char* rp;
    NNStatus status;
    NNObjFunction* closure;
    auto gcs = SharedState::get();
    gcs->m_rootphysfile = filename;
    nn_state_updateprocessinfo();
    rp = (char*)filename;
    gcs->m_topmodule->physicalpath = nn_string_copycstr(rp);
    nn_module_setfilefield(module);
    closure = nn_state_compilesource(module, false, source, true);
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
     * so no need to create a NNValue and call nn_vm_callvalue().
     */
    nn_vm_callclosure(closure, NNValue::makeNull(), 0, false);
    status = nn_vm_runvm(0, dest);
    return status;
}

NNValue nn_state_evalsource(const char* source)
{
    bool ok;
    size_t argc;
    NNValue callme;
    NNValue retval;
    NNObjFunction* closure;
    (void)argc;
    auto gcs = SharedState::get();
    closure = nn_state_compilesource(gcs->m_topmodule, true, source, false);
    callme = NNValue::fromObject(closure);
    argc = nn_nestcall_prepare(callme, NNValue::makeNull(), nullptr, 0);
    ok = nn_nestcall_callfunction(callme, NNValue::makeNull(), nullptr, 0, &retval, false);
    if(!ok)
    {
        nn_except_throw("eval() failed");
    }
    return retval;
}

#define OPTTOSTRIFY_(thing) #thing
#define OPTTOSTRIFY(thing) OPTTOSTRIFY_(thing)

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

#if !defined(NEON_PLAT_ISWASM)
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
    NNValue dest;
    NNIOStream* pr;
    auto gcs = SharedState::get();
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
            sprintf(varnamebuf, "_%ld", (long)rescnt);
            nn_state_execsource(gcs->m_topmodule, nn_strbuf_data(source), "<repl>", &dest);
            dest = gcs->m_lastreplvalue;
            if(!dest.isNull())
            {
                pr->format("%s = ", varnamebuf);
                nn_iostream_printvalue(pr, dest, true, true);
                nn_state_defglobalvalue(varnamebuf, dest);
                pr->format("\n");
                rescnt++;
            }
            gcs->m_lastreplvalue = NNValue::makeNull();
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
    NNStatus result;
    auto gcs = SharedState::get();
    source = nn_util_filereadfile(file, &fsz, false, 0);
    if(source == nullptr)
    {
        oldfile = file;
        source = nn_util_filereadfile(file, &fsz, false, 0);
        if(source == nullptr)
        {
            fprintf(stderr, "failed to read from '%s': %s\n", oldfile, strerror(errno));
            return false;
        }
    }
    result = nn_state_execsource(gcs->m_topmodule, source, file, nullptr);
    nn_memory_free(source);
    fflush(stdout);
    if(result == NEON_STATUS_FAILCOMPILE)
    {
        return false;
    }
    if(result == NEON_STATUS_FAILRUNTIME)
    {
        return false;
    }
    return true;
}

static bool nn_cli_runcode(char* source)
{
    NNStatus result;
    auto gcs = SharedState::get();
    gcs->m_rootphysfile = nullptr;
    result = nn_state_execsource(gcs->m_topmodule, source, "<-e>", nullptr);
    fflush(stdout);
    if(result == NEON_STATUS_FAILCOMPILE)
    {
        return false;
    }
    if(result == NEON_STATUS_FAILRUNTIME)
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
    NNObjString* oskey;
    NNObjString* osval;
    auto gcs = SharedState::get();
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
            oskey = nn_string_copycstr(keybuf);
            osval = nn_string_copycstr(valbuf);
            nn_dict_setentry(gcs->m_envdict, NNValue::fromObject(oskey), NNValue::fromObject(osval));
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
    NNObjString* os;
    linocontext_t lictx;
    static optlongflags_t longopts[] = { { "help", 'h', OPTPARSE_NONE, "this help" },
                                         { "strict", 's', OPTPARSE_NONE, "enable strict mode, such as requiring explicit var declarations" },
                                         { "warn", 'w', OPTPARSE_NONE, "enable warnings" },
                                         { "debug", 'd', OPTPARSE_NONE, "enable debugging: print instructions and stack values during execution" },
                                         { "exitaftercompile", 'x', OPTPARSE_NONE, "when using '-d', quit after printing compiled function(s)" },
                                         { "eval", 'e', OPTPARSE_REQUIRED, "evaluate a single line of code" },
                                         { "quit", 'q', OPTPARSE_NONE, "initiate, then immediately destroy the interpreter state" },
                                         { "types", 't', OPTPARSE_NONE, "print sizeof() of types" },
                                         { "apidebug", 'a', OPTPARSE_NONE, "print calls to API (very verbose, very slow)" },
                                         { "gcstart", 'g', OPTPARSE_REQUIRED, "set minimum bytes at which the GC should kick in. 0 disables GC (default is " OPTTOSTRIFY(NEON_CONFIG_DEFAULTGCSTART) ")" },
                                         { 0, 0, (optargtype_t)0, nullptr } };
    lino_context_init(&lictx);
    nn_memory_init();
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
    if(!nn_state_makestate())
    {
        fprintf(stderr, "failed to create state\n");
        return 0;
    }
    auto gcs = SharedState::get();
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
            os = nn_string_copycstr(nargv[i]);
            nn_array_push(gcs->m_processinfo->cliargv, NNValue::fromObject(os));
        }
        gcs->m_declaredglobals.set(nn_value_copystr("ARGV"), NNValue::fromObject(gcs->m_processinfo->cliargv));
    }
    gcs->m_gcstate.nextgc = nextgcstart;
    nn_import_loadbuiltinmodules();
    if(source != nullptr)
    {
        ok = nn_cli_runcode(source);
    }
    else if(nargc > 0)
    {
        os = gcs->m_processinfo->cliargv->varray.get(0).asString();
        filename = nn_string_getdata(os);
        ok = nn_cli_runfile(filename);
    }
    else
    {
        ok = nn_cli_repl(&lictx);
    }
cleanup:
    nn_state_destroystate();
    nn_memory_finish();
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
