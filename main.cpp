
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
        { \
            if((var) == nullptr) \
            { \
                return (defval); \
            } \
        }
    #define NN_NULLPTRCHECK_RETURN(var) \
        { \
            if((var) == nullptr) \
            { \
                return; \
            } \
        }
#else
    #define NN_NULLPTRCHECK_RETURNVALUE(var, defval)
    #define NN_NULLPTRCHECK_RETURN(var)
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define nn_util_likely(x)   (__builtin_expect(!!(x), 1))
    #define nn_util_unlikely(x) (__builtin_expect(!!(x), 0))
#else
    #define nn_util_likely(x)   (x)
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
#define NEON_CONFIG_ASTMAXLOCALS (64*2)

/* how many upvalues per function can be compiled */
#define NEON_CONFIG_ASTMAXUPVALS (64*2)

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

#if defined(__GNUC__)
    #define NEON_ATTR_PRINTFLIKE(fmtarg, firstvararg) __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#else
    #define NEON_ATTR_PRINTFLIKE(a, b)
#endif


/*
// NOTE:
// 1. Any call to nn_gcmem_protect() within a function/block must be accompanied by
// at least one call to GCSingleton::clearGCProtect() before exiting the function/block
// otherwise, expected unexpected behavior
// 2. The call to GCSingleton::clearGCProtect() will be automatic for native functions.
// 3. $thisval must be retrieved before any call to nn_gcmem_protect in a
// native function.
*/

#define nn_gcmem_freearray(typsz, pointer, oldcount) \
    nn_gcmem_release(pointer, (typsz) * (oldcount))

#define nn_except_throwclass(state, exklass, ...) \
    nn_except_throwwithclass(state, exklass, __FILE__, __LINE__, __VA_ARGS__)

#define nn_except_throw(state, ...) \
    nn_except_throwclass(state, state->exceptions.stdexception, __VA_ARGS__)


#define NEON_RETURNERROR(...) \
    { \
        nn_vm_stackpopn(state, argc); \
        nn_except_throw(state, ##__VA_ARGS__); \
    } \
    return nn_value_makebool(false);

#define NEON_ARGS_FAIL(chp, ...) \
    nn_argcheck_fail((chp), __FILE__, __LINE__, __VA_ARGS__)

/* check for exact number of arguments $d */
#define NEON_ARGS_CHECKCOUNT(chp, d) \
    if(nn_util_unlikely((chp)->argc != (d))) \
    { \
        return NEON_ARGS_FAIL(chp, "%s() expects %d arguments, %d given", (chp)->name, d, (chp)->argc); \
    }

/* check for miminum args $d ($d ... n) */
#define NEON_ARGS_CHECKMINARG(chp, d) \
    if(nn_util_unlikely((chp)->argc < (d))) \
    { \
        return NEON_ARGS_FAIL(chp, "%s() expects minimum of %d arguments, %d given", (chp)->name, d, (chp)->argc); \
    }

/* check for range of args ($low .. $up) */
#define NEON_ARGS_CHECKCOUNTRANGE(chp, low, up) \
    if(nn_util_unlikely((chp)->argc < (low) || (chp)->argc > (up))) \
    { \
        return NEON_ARGS_FAIL(chp, "%s() expects between %d and %d arguments, %d given", (chp)->name, low, up, (chp)->argc); \
    }

/* check for argument at index $i for $type, where $type is a nn_value_is*() function */
#if 1
    #define NEON_ARGS_CHECKTYPE(chp, i, typefunc)
#else
    #define NEON_ARGS_CHECKTYPE(chp, i, typefunc) \
        if(nn_util_unlikely(!typefunc((chp)->argv[i]))) \
        { \
            return NEON_ARGS_FAIL(chp, "%s() expects argument %d as %s, %s given", (chp)->name, (i) + 1, nn_value_typefromfunction(typefunc), nn_value_typename((chp)->argv[i], false), false); \
        }
#endif

#if 0
    #define NEON_APIDEBUG(state, ...) \
        if((nn_util_unlikely((state)->conf.enableapidebug))) \
        { \
            nn_state_apidebug(state, __FUNCTION__, __VA_ARGS__); \
        }
#else
    #define NEON_APIDEBUG(state, ...)
#endif

#define nn_value_fromobject(obj) nn_value_fromobject_actual((NNObject*)obj)

#define SCRIPTFN_UNUSED(var) (void)var

enum NNOpCode
{
    NEON_OP_GLOBALDEFINE,
    NEON_OP_GLOBALGET,
    NEON_OP_GLOBALSET,
    NEON_OP_LOCALGET,
    NEON_OP_LOCALSET,
    NEON_OP_FUNCARGOPTIONAL,
    NEON_OP_FUNCARGSET,
    NEON_OP_FUNCARGGET,
    NEON_OP_UPVALUEGET,
    NEON_OP_UPVALUESET,
    NEON_OP_UPVALUECLOSE,
    NEON_OP_PROPERTYGET,
    NEON_OP_PROPERTYGETSELF,
    NEON_OP_PROPERTYSET,
    NEON_OP_JUMPIFFALSE,
    NEON_OP_JUMPNOW,
    NEON_OP_LOOP,
    NEON_OP_EQUAL,
    NEON_OP_PRIMGREATER,
    NEON_OP_PRIMLESSTHAN,
    NEON_OP_PUSHEMPTY,
    NEON_OP_PUSHNULL,
    NEON_OP_PUSHTRUE,
    NEON_OP_PUSHFALSE,
    NEON_OP_PRIMADD,
    NEON_OP_PRIMSUBTRACT,
    NEON_OP_PRIMMULTIPLY,
    NEON_OP_PRIMDIVIDE,
    NEON_OP_PRIMFLOORDIVIDE,
    NEON_OP_PRIMMODULO,
    NEON_OP_PRIMPOW,
    NEON_OP_PRIMNEGATE,
    NEON_OP_PRIMNOT,
    NEON_OP_PRIMBITNOT,
    NEON_OP_PRIMAND,
    NEON_OP_PRIMOR,
    NEON_OP_PRIMBITXOR,
    NEON_OP_PRIMSHIFTLEFT,
    NEON_OP_PRIMSHIFTRIGHT,
    NEON_OP_PUSHONE,
    /* 8-bit constant address (0 - 255) */
    NEON_OP_PUSHCONSTANT,
    NEON_OP_ECHO,
    NEON_OP_POPONE,
    NEON_OP_DUPONE,
    NEON_OP_POPN,
    NEON_OP_ASSERT,
    NEON_OP_EXTHROW,
    NEON_OP_MAKECLOSURE,
    NEON_OP_CALLFUNCTION,
    NEON_OP_CALLMETHOD,
    NEON_OP_CLASSINVOKETHIS,
    NEON_OP_RETURN,
    NEON_OP_MAKECLASS,
    NEON_OP_MAKEMETHOD,
    NEON_OP_CLASSGETTHIS,
    NEON_OP_CLASSPROPERTYDEFINE,
    NEON_OP_CLASSINHERIT,
    NEON_OP_CLASSGETSUPER,
    NEON_OP_CLASSINVOKESUPER,
    NEON_OP_CLASSINVOKESUPERSELF,
    NEON_OP_MAKERANGE,
    NEON_OP_MAKEARRAY,
    NEON_OP_MAKEDICT,
    NEON_OP_INDEXGET,
    NEON_OP_INDEXGETRANGED,
    NEON_OP_INDEXSET,
    NEON_OP_IMPORTIMPORT,
    NEON_OP_EXTRY,
    NEON_OP_EXPOPTRY,
    NEON_OP_EXPUBLISHTRY,
    NEON_OP_STRINGIFY,
    NEON_OP_SWITCH,
    NEON_OP_TYPEOF,
    NEON_OP_OPINSTANCEOF,
    NEON_OP_HALT,
    NEON_OP_BREAK_PL
};

enum NNAstTokType
{
    NEON_ASTTOK_NEWLINE,
    NEON_ASTTOK_PARENOPEN,
    NEON_ASTTOK_PARENCLOSE,
    NEON_ASTTOK_BRACKETOPEN,
    NEON_ASTTOK_BRACKETCLOSE,
    NEON_ASTTOK_BRACEOPEN,
    NEON_ASTTOK_BRACECLOSE,
    NEON_ASTTOK_SEMICOLON,
    NEON_ASTTOK_COMMA,
    NEON_ASTTOK_BACKSLASH,
    NEON_ASTTOK_EXCLMARK,
    NEON_ASTTOK_NOTEQUAL,
    NEON_ASTTOK_COLON,
    NEON_ASTTOK_AT,
    NEON_ASTTOK_DOT,
    NEON_ASTTOK_DOUBLEDOT,
    NEON_ASTTOK_TRIPLEDOT,
    NEON_ASTTOK_PLUS,
    NEON_ASTTOK_PLUSASSIGN,
    NEON_ASTTOK_INCREMENT,
    NEON_ASTTOK_MINUS,
    NEON_ASTTOK_MINUSASSIGN,
    NEON_ASTTOK_DECREMENT,
    NEON_ASTTOK_MULTIPLY,
    NEON_ASTTOK_MULTASSIGN,
    NEON_ASTTOK_POWEROF,
    NEON_ASTTOK_POWASSIGN,
    NEON_ASTTOK_DIVIDE,
    NEON_ASTTOK_DIVASSIGN,
    NEON_ASTTOK_FLOOR,
    NEON_ASTTOK_ASSIGN,
    NEON_ASTTOK_EQUAL,
    NEON_ASTTOK_LESSTHAN,
    NEON_ASTTOK_LESSEQUAL,
    NEON_ASTTOK_LEFTSHIFT,
    NEON_ASTTOK_LEFTSHIFTASSIGN,
    NEON_ASTTOK_GREATERTHAN,
    NEON_ASTTOK_GREATER_EQ,
    NEON_ASTTOK_RIGHTSHIFT,
    NEON_ASTTOK_RIGHTSHIFTASSIGN,
    NEON_ASTTOK_MODULO,
    NEON_ASTTOK_PERCENT_EQ,
    NEON_ASTTOK_AMP,
    NEON_ASTTOK_AMP_EQ,
    NEON_ASTTOK_BAR,
    NEON_ASTTOK_BAR_EQ,
    NEON_ASTTOK_TILDE,
    NEON_ASTTOK_TILDE_EQ,
    NEON_ASTTOK_XOR,
    NEON_ASTTOK_XOR_EQ,
    NEON_ASTTOK_QUESTION,
    NEON_ASTTOK_KWAND,
    NEON_ASTTOK_KWAS,
    NEON_ASTTOK_KWASSERT,
    NEON_ASTTOK_KWBREAK,
    NEON_ASTTOK_KWCATCH,
    NEON_ASTTOK_KWCLASS,
    NEON_ASTTOK_KWCONTINUE,
    NEON_ASTTOK_KWFUNCTION,
    NEON_ASTTOK_KWDEFAULT,
    NEON_ASTTOK_KWTHROW,
    NEON_ASTTOK_KWDO,
    NEON_ASTTOK_KWECHO,
    NEON_ASTTOK_KWELSE,
    NEON_ASTTOK_KWFALSE,
    NEON_ASTTOK_KWFINALLY,
    NEON_ASTTOK_KWFOREACH,
    NEON_ASTTOK_KWIF,
    NEON_ASTTOK_KWIMPORT,
    NEON_ASTTOK_KWIN,
    NEON_ASTTOK_KWINSTANCEOF,
    NEON_ASTTOK_KWFOR,
    NEON_ASTTOK_KWNULL,
    NEON_ASTTOK_KWNEW,
    NEON_ASTTOK_KWOR,
    NEON_ASTTOK_KWSUPER,
    NEON_ASTTOK_KWRETURN,
    NEON_ASTTOK_KWTHIS,
    NEON_ASTTOK_KWSTATIC,
    NEON_ASTTOK_KWTRUE,
    NEON_ASTTOK_KWTRY,
    NEON_ASTTOK_KWTYPEOF,
    NEON_ASTTOK_KWSWITCH,
    NEON_ASTTOK_KWVAR,
    NEON_ASTTOK_KWCONST,
    NEON_ASTTOK_KWCASE,
    NEON_ASTTOK_KWWHILE,
    NEON_ASTTOK_KWEXTENDS,
    NEON_ASTTOK_LITERALSTRING,
    NEON_ASTTOK_LITERALRAWSTRING,
    NEON_ASTTOK_LITNUMREG,
    NEON_ASTTOK_LITNUMBIN,
    NEON_ASTTOK_LITNUMOCT,
    NEON_ASTTOK_LITNUMHEX,
    NEON_ASTTOK_IDENTNORMAL,
    NEON_ASTTOK_DECORATOR,
    NEON_ASTTOK_INTERPOLATION,
    NEON_ASTTOK_EOF,
    NEON_ASTTOK_ERROR,
    NEON_ASTTOK_KWEMPTY,
    NEON_ASTTOK_UNDEFINED,
    NEON_ASTTOK_TOKCOUNT
};

enum NNAstPrecedence
{
    NEON_ASTPREC_NONE,

    /* =, &=, |=, *=, +=, -=, /=, **=, %=, >>=, <<=, ^=, //= */
    NEON_ASTPREC_ASSIGNMENT,
    /* ~= ?: */
    NEON_ASTPREC_CONDITIONAL,
    /* 'or' || */
    NEON_ASTPREC_OR,
    /* 'and' && */
    NEON_ASTPREC_AND,
    /* ==, != */
    NEON_ASTPREC_EQUALITY,
    /* <, >, <=, >= */
    NEON_ASTPREC_COMPARISON,
    /* | */
    NEON_ASTPREC_BITOR,
    /* ^ */
    NEON_ASTPREC_BITXOR,
    /* & */
    NEON_ASTPREC_BITAND,
    /* <<, >> */
    NEON_ASTPREC_SHIFT,
    /* .. */
    NEON_ASTPREC_RANGE,
    /* +, - */
    NEON_ASTPREC_TERM,
    /* *, /, %, **, // */
    NEON_ASTPREC_FACTOR,
    /* !, -, ~, (++, -- this two will now be treated as statements) */
    NEON_ASTPREC_UNARY,
    /* ., () */
    NEON_ASTPREC_CALL,
    NEON_ASTPREC_PRIMARY
};

enum NNFuncContextType
{
    NEON_FNCONTEXTTYPE_ANONYMOUS,
    NEON_FNCONTEXTTYPE_FUNCTION,
    NEON_FNCONTEXTTYPE_METHOD,
    NEON_FNCONTEXTTYPE_INITIALIZER,
    NEON_FNCONTEXTTYPE_PRIVATE,
    NEON_FNCONTEXTTYPE_STATIC,
    NEON_FNCONTEXTTYPE_SCRIPT
};

enum NNStatus
{
    NEON_STATUS_OK,
    NEON_STATUS_FAILCOMPILE,
    NEON_STATUS_FAILRUNTIME
};

enum NNPrMode
{
    NEON_PRMODE_UNDEFINED,
    NEON_PRMODE_STRING,
    NEON_PRMODE_FILE
};

enum NNObjType
{
    /* containers */
    NEON_OBJTYPE_STRING,
    NEON_OBJTYPE_RANGE,
    NEON_OBJTYPE_ARRAY,
    NEON_OBJTYPE_DICT,
    NEON_OBJTYPE_FILE,

    /* base object types */
    NEON_OBJTYPE_UPVALUE,
    NEON_OBJTYPE_FUNCBOUND,
    NEON_OBJTYPE_FUNCCLOSURE,
    NEON_OBJTYPE_FUNCSCRIPT,
    NEON_OBJTYPE_INSTANCE,
    NEON_OBJTYPE_FUNCNATIVE,
    NEON_OBJTYPE_CLASS,

    /* non-user objects */
    NEON_OBJTYPE_MODULE,
    NEON_OBJTYPE_SWITCH,
    /* object type that can hold any C pointer */
    NEON_OBJTYPE_USERDATA
};

enum NNValType
{
    NEON_VALTYPE_NULL,
    NEON_VALTYPE_BOOL,
    NEON_VALTYPE_NUMBER,
    NEON_VALTYPE_OBJ,
};

enum NNFieldType
{
    NEON_PROPTYPE_INVALID,
    NEON_PROPTYPE_VALUE,
    /*
    * indicates this field contains a function, a pseudo-getter (i.e., ".length")
    * which is called upon getting
    */
    NEON_PROPTYPE_FUNCTION
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

struct /**/NNValue;
struct /**/NNObject;
struct /**/NNAstParser;
struct /**/NNAstToken;
struct /**/NNState;
struct /**/NNDefModule;
struct /**/NNObjString;
struct /**/NNObjArray;
struct /**/NNProperty;
struct /**/NNIOStream;
struct /**/NNBlob;
struct /**/NNObjFunction;
struct /**/NNObjDict;
struct /**/NNObject;
struct /**/NNObjFile;
struct /**/NNObjInstance;
struct /**/NNObjClass;
struct /**/NNIOResult;
struct /**/NNInstruction;
struct /**/NNAstLexer;
struct /**/NNFormatInfo;
struct /**/utf8iterator_t;
struct /**/NNConstClassMethodItem;
struct /**/NNObjModule;
struct /**/NNObjUpvalue;
struct /**/NNObjUserdata;
struct /**/NNObjSwitch;
struct /**/NNObjDict;
struct /**/NNObjRange;
struct /**/NNAstRule;

struct NNArgCheck;
template<typename StoredType>
struct NNValArray;

template<typename HTKeyT, typename HTValT>
struct NNHashTable;

typedef bool(*NNValIsFuncFN)(NNValue);
typedef NNValue (*NNNativeFN)(NNState*, NNValue, NNValue*, size_t);
typedef void (*NNPtrFreeFN)(void*);
typedef bool (*NNAstParsePrefixFN)(NNAstParser*, bool);
typedef bool (*NNAstParseInfixFN)(NNAstParser*, NNAstToken, bool);
typedef NNValue (*NNClassFieldFN)(NNState*);
typedef void (*NNModLoaderFN)();
typedef NNDefModule* (*NNModInitFN)();
typedef NNValue(*nnbinopfunc_t)(double, double);

typedef size_t (*mcitemhashfn_t)(void*);
typedef bool (*mcitemcomparefn_t)(void*, void*);
typedef int(*NNStrBufCharModFunc)(int);

NEON_INLINE NNValue nn_value_makevalue(NNValType type);
NEON_INLINE NNValue nn_value_makenull();
NEON_INLINE NNValue nn_value_makebool(bool b);
NEON_INLINE NNValue nn_value_makenumber(double d);

NNProperty nn_property_makewithpointer(NNValue val, NNFieldType type);
NNProperty nn_property_makewithgetset(NNValue val, NNValue getter, NNValue setter, NNFieldType type);
NNProperty nn_property_make(NNValue val, NNFieldType type);
NNProperty nn_property_make(NNValue val, NNFieldType type);
size_t nn_string_getlength(NNObjString* os);
const char* nn_string_getdata(NNObjString* os);
/* allocator.c */
void *nn_allocator_create(void);
void nn_allocator_destroy(void *msp);
void *nn_allocuser_malloc(void *msp, size_t nsize);
void *nn_allocuser_free(void *msp, void *ptr);
void *nn_allocuser_realloc(void *msp, void *ptr, size_t nsize);
/* core.c */
NNValue nn_argcheck_vfail(NNArgCheck *ch, const char *srcfile, int srcline, const char *fmt, va_list va);
NNValue nn_argcheck_fail(NNArgCheck *ch, const char *srcfile, int srcline, const char *fmt, ...);
void nn_argcheck_init(NNState *state, NNArgCheck *ch, const char *name, NNValue *argv, size_t argc);
NNProperty nn_property_makewithpointer(NNValue val, NNFieldType type);
NNProperty nn_property_makewithgetset(NNValue val, NNValue getter, NNValue setter, NNFieldType type);
NNProperty nn_property_make(NNValue val, NNFieldType type);
void nn_state_installmethods(NNState *state, NNObjClass *klass, NNConstClassMethodItem *listmethods);
void nn_state_initbuiltinmethods(NNState *state);
void nn_state_vwarn(NNState *state, const char *fmt, va_list va);
void nn_state_warn(NNState *state, const char *fmt, ...);
NNValue nn_except_getstacktrace(NNState *state);
bool nn_except_propagate(NNState *state);
bool nn_except_pushhandler(NNState *state, NNObjClass *type, int address, int finallyaddress);
bool nn_except_vthrowactual(NNState *state, NNObjClass *klass, const char *srcfile, int srcline, const char *format, va_list va);
bool nn_except_throwactual(NNState *state, NNObjClass *klass, const char *srcfile, int srcline, const char *format, ...);
bool nn_except_throwwithclass(NNState *state, NNObjClass *klass, const char *srcfile, int srcline, const char *format, ...);
bool nn_except_vthrowwithclass(NNState *state, NNObjClass *exklass, const char *srcfile, int srcline, const char *format, va_list args);
NNInstruction nn_util_makeinst(bool isop, uint8_t code, int srcline);
NNObjClass *nn_except_makeclass(NNState *state, NNObjModule *module, const char *cstrname, bool iscs);
NNObjInstance *nn_except_makeinstance(NNState *state, NNObjClass *exklass, const char *srcfile, int srcline, NNObjString *message);
void nn_state_raisefatalerror(NNState *state, const char *format, ...);
bool nn_state_defglobalvalue(NNState *state, const char *name, NNValue val);
bool nn_state_defnativefunctionptr(NNState *state, const char *name, NNNativeFN fptr, void *uptr);
bool nn_state_defnativefunction(NNState *state, const char *name, NNNativeFN fptr);
NNObjClass *nn_util_makeclass(NNState *state, const char *name, NNObjClass *parent);
void nn_state_buildprocessinfo(NNState *state);
void nn_state_updateprocessinfo(NNState *state);
bool nn_state_makestack(NNState *pstate);
NNState *nn_state_makealloc(void);
void nn_state_destroy(NNState *state, bool onstack);
bool nn_util_methodisprivate(NNObjString *name);
NNObjFunction *nn_state_compilesource(NNState *state, NNObjModule *module, bool fromeval, const char *source, bool toplevel);
NNStatus nn_state_execsource(NNState *state, NNObjModule *module, const char *source, const char *filename, NNValue *dest);
NNValue nn_state_evalsource(NNState *state, const char *source);
/* dbg.c */
void nn_dbg_disasmblob(NNIOStream *pr, NNBlob *blob, const char *name);
void nn_dbg_printinstrname(NNIOStream *pr, const char *name);
int nn_dbg_printsimpleinstr(NNIOStream *pr, const char *name, int offset);
int nn_dbg_printconstinstr(NNIOStream *pr, const char *name, NNBlob *blob, int offset);
int nn_dbg_printpropertyinstr(NNIOStream *pr, const char *name, NNBlob *blob, int offset);
int nn_dbg_printshortinstr(NNIOStream *pr, const char *name, NNBlob *blob, int offset);
int nn_dbg_printbyteinstr(NNIOStream *pr, const char *name, NNBlob *blob, int offset);
int nn_dbg_printjumpinstr(NNIOStream *pr, const char *name, int sign, NNBlob *blob, int offset);
int nn_dbg_printtryinstr(NNIOStream *pr, const char *name, NNBlob *blob, int offset);
int nn_dbg_printinvokeinstr(NNIOStream *pr, const char *name, NNBlob *blob, int offset);
const char *nn_dbg_op2str(uint8_t instruc);
int nn_dbg_printclosureinstr(NNIOStream *pr, const char *name, NNBlob *blob, int offset);
int nn_dbg_printinstructionat(NNIOStream *pr, NNBlob *blob, int offset);
/* ioprinter.c */
void nn_iostream_initvars(NNIOStream *pr, NNPrMode mode);
bool nn_iostream_makestackio(NNIOStream *pr, FILE *fh, bool shouldclose);
bool nn_iostream_makestackopenfile(NNIOStream *pr, const char *path, bool writemode);
bool nn_iostream_makestackstring(NNIOStream *pr);
NNIOStream *nn_iostream_makeundefined(NNPrMode mode);
NNIOStream *nn_iostream_makeio(FILE *fh, bool shouldclose);
NNIOStream *nn_iostream_makeopenfile(const char *path, bool writemode);
NNIOStream *nn_iostream_makestring();
void nn_iostream_destroy(NNIOStream *pr);
NNObjString *nn_iostream_takestring(NNIOStream *pr);
NNObjString *nn_iostream_copystring(NNIOStream *pr);
void nn_iostream_flush(NNIOStream *pr);
bool nn_iostream_writestringl(NNIOStream *pr, const char *estr, size_t elen);
bool nn_iostream_writestring(NNIOStream *pr, const char *estr);
bool nn_iostream_writechar(NNIOStream *pr, int b);
bool nn_iostream_writeescapedchar(NNIOStream *pr, int ch);
bool nn_iostream_writequotedstring(NNIOStream *pr, const char *str, size_t len, bool withquot);
bool nn_iostream_vwritefmttostring(NNIOStream *pr, const char *fmt, va_list va);
bool nn_iostream_vwritefmt(NNIOStream *pr, const char *fmt, va_list va);
bool nn_iostream_printf(NNIOStream *pr, const char *fmt, ...);
void nn_valtable_print(NNIOStream *pr, NNHashTable<NNValue, NNValue> *table, const char *name);
void nn_iostream_printfunction(NNIOStream *pr, NNObjFunction *func);
void nn_iostream_printarray(NNIOStream *pr, NNObjArray *list);
void nn_iostream_printdict(NNIOStream *pr, NNObjDict *dict);
void nn_iostream_printfile(NNIOStream *pr, NNObjFile *file);
void nn_iostream_printinstance(NNIOStream *pr, NNObjInstance *instance, bool invmethod);
void nn_iostream_printtable(NNIOStream *pr, NNHashTable<NNValue, NNValue> *table);
void nn_iostream_printobjclass(NNIOStream *pr, NNValue value, bool fixstring, bool invmethod);
void nn_iostream_printobject(NNIOStream *pr, NNValue value, bool fixstring, bool invmethod);
void nn_iostream_printnumber(NNIOStream *pr, NNValue value);
void nn_iostream_printvalue(NNIOStream *pr, NNValue value, bool fixstring, bool invmethod);
/* libarray.c */
NNObjArray *nn_array_makefilled(size_t cnt, NNValue filler);
NNObjArray *nn_array_make();
NNObjArray *nn_object_makearray();
void nn_array_push(NNObjArray *list, NNValue value);
bool nn_array_get(NNObjArray *list, size_t idx, NNValue *vdest);
NNObjArray *nn_array_copy(NNObjArray *list, long start, long length);
void nn_state_installobjectarray(NNState *state);
/* libclass.c */
NNObjClass *nn_object_makeclass(NNObjString *name, NNObjClass *parent);
void nn_class_destroy(NNObjClass *klass);
bool nn_class_inheritfrom(NNObjClass *subclass, NNObjClass *superclass);
bool nn_class_defproperty(NNObjClass *klass, NNObjString *cstrname, NNValue val);
bool nn_class_defcallablefieldptr(NNObjClass *klass, NNObjString *name, NNNativeFN function, void *uptr);
bool nn_class_defcallablefield(NNObjClass *klass, NNObjString *name, NNNativeFN function);
bool nn_class_defstaticcallablefieldptr(NNObjClass *klass, NNObjString *name, NNNativeFN function, void *uptr);
bool nn_class_defstaticcallablefield(NNObjClass *klass, NNObjString *name, NNNativeFN function);
bool nn_class_setstaticproperty(NNObjClass *klass, NNObjString *name, NNValue val);
bool nn_class_defnativeconstructorptr(NNObjClass *klass, NNNativeFN function, void *uptr);
bool nn_class_defnativeconstructor(NNObjClass *klass, NNNativeFN function);
bool nn_class_defmethod(NNObjClass *klass, NNObjString *name, NNValue val);
bool nn_class_defnativemethodptr(NNObjClass *klass, NNObjString *name, NNNativeFN function, void *ptr);
bool nn_class_defnativemethod(NNObjClass *klass, NNObjString *name, NNNativeFN function);
bool nn_class_defstaticnativemethodptr(NNObjClass *klass, NNObjString *name, NNNativeFN function, void *uptr);
bool nn_class_defstaticnativemethod(NNObjClass *klass, NNObjString *name, NNNativeFN function);
NNProperty *nn_class_getmethodfield(NNObjClass *klass, NNObjString *name);
NNProperty *nn_class_getpropertyfield(NNObjClass *klass, NNObjString *name);
NNProperty *nn_class_getstaticproperty(NNObjClass *klass, NNObjString *name);
NNProperty *nn_class_getstaticmethodfield(NNObjClass *klass, NNObjString *name);
NNObjInstance *nn_object_makeinstancesize(NNObjClass *klass, size_t sz);
NNObjInstance *nn_object_makeinstance(NNObjClass *klass);
void nn_instance_mark(NNObjInstance *instance);
void nn_instance_destroy(NNObjInstance *instance);
bool nn_instance_defproperty(NNObjInstance *instance, NNObjString *name, NNValue val);
NNProperty *nn_instance_getvar(NNObjInstance *inst, NNObjString *name);
NNProperty *nn_instance_getvarcstr(NNObjInstance *inst, const char *name);
NNProperty *nn_instance_getmethod(NNObjInstance *inst, NNObjString *name);
NNProperty *nn_instance_getmethodcstr(NNObjInstance *inst, const char *name);
/* libdict.c */
NNObjDict *nn_object_makedict();
void nn_dict_destroy(NNObjDict *dict);
void nn_dict_mark(NNObjDict *dict);
bool nn_dict_setentry(NNObjDict *dict, NNValue key, NNValue value);
void nn_dict_addentry(NNObjDict *dict, NNValue key, NNValue value);
void nn_dict_addentrycstr(NNObjDict *dict, const char *ckey, NNValue value);
NNProperty *nn_dict_getentry(NNObjDict *dict, NNValue key);
NNObjDict *nn_dict_copy(NNObjDict *dict);
void nn_state_installobjectdict(NNState *state);
/* libfile.c */
NNObjFile *nn_object_makefile(FILE *handle, bool isstd, const char *path, const char *mode);
void nn_file_destroy(NNObjFile *file);
void nn_file_mark(NNObjFile *file);
bool nn_file_read(NNObjFile *file, size_t readhowmuch, NNIOResult *dest);
int nn_fileobject_close(NNObjFile *file);
bool nn_fileobject_open(NNObjFile *file);
void nn_state_installobjectfile(NNState *state);
/* libfunc.c */
NNObjFunction *nn_object_makefuncgeneric(NNObjString *name, NNObjType fntype, NNValue thisval);
NNObjFunction *nn_object_makefuncbound(NNValue receiver, NNObjFunction *method);
NNObjFunction *nn_object_makefuncscript(NNObjModule *module, NNFuncContextType type);
void nn_funcscript_destroy(NNObjFunction *ofn);
NNObjFunction *nn_object_makefuncnative(NNNativeFN natfn, const char *name, void *uptr);
NNObjFunction *nn_object_makefuncclosure(NNObjFunction *innerfn, NNValue thisval);
/* libmodule.c */
void nn_import_loadbuiltinmodules(NNState *state);
bool nn_state_addmodulesearchpathobj(NNState *state, NNObjString *os);
bool nn_state_addmodulesearchpath(NNState *state, const char *path);
void nn_state_setupmodulepaths(NNState *state);
void nn_module_setfilefield(NNState *state, NNObjModule *module);
void nn_module_destroy(NNObjModule *module);
NNObjModule *nn_import_loadmodulescript(NNState *state, NNObjModule *intomodule, NNObjString *modulename);
char *nn_import_resolvepath(NNState *state, const char *modulename, const char *currentfile, char *rootfile, bool isrelative);
bool nn_import_loadnativemodule(NNState *state, NNModInitFN init_fn, char *importname, const char *source, void *dlw);
void nn_import_addnativemodule(NNState *state, NNObjModule *module, const char *as);
void nn_import_closemodule(void *hnd);
/* libnumber.c */
void nn_state_installobjectnumber(NNState *state);
void nn_state_installmodmath(NNState *state);
/* libobject.c */
void nn_state_installobjectobject(NNState *state);
/* libprocess.c */
void nn_state_installobjectprocess(NNState *state);
/* librange.c */
void nn_state_installobjectrange(NNState *state);
/* libstring.c */
size_t nn_strutil_rndup2pow64(uint64_t x);
size_t nn_strutil_splitstr(char *str, char sep, char **ptrs, size_t nptrs);
size_t nn_strutil_charreplace(char *str, char from, char to);
void nn_strutil_reverseregion(char *str, size_t length);
bool nn_strutil_isallspace(const char *s);
char *nn_strutil_nextspace(char *s);
char *nn_strutil_trim(char *str);
size_t nn_strutil_chomp(char *str, size_t len);
size_t nn_strutil_countchar(const char *str, char c);
size_t nn_strutil_split(const char *splitat, const char *sourcetxt, char ***result);
void nn_strutil_callboundscheckinsert(NNStringBuffer *sb, size_t pos, const char *file, int line);
void nn_strutil_callboundscheckreadrange(NNStringBuffer *sb, size_t start, size_t len, const char *file, int line);
void nn_strutil_faststrncat(char *dest, const char *src, size_t *size);
size_t nn_strutil_strreplace1(char **str, size_t selflen, const char *findstr, size_t findlen, const char *substr, size_t sublen);
size_t nn_strutil_strrepcount(const char *str, size_t slen, const char *findstr, size_t findlen, size_t sublen);
void nn_strutil_strreplace2(char *target, size_t tgtlen, const char *findstr, size_t findlen, const char *substr, size_t sublen);
bool nn_strutil_inpreplhelper(char *dest, const char *src, size_t srclen, int findme, const char *substr, size_t sublen, size_t maxlen, size_t *dlen);
size_t nn_strutil_inpreplace(char *target, size_t tgtlen, int findme, const char *substr, size_t sublen, size_t maxlen);
NNStringBuffer *nn_strbuf_makelongfromptr(NNStringBuffer *sb, size_t len);
bool nn_strbuf_initbasicempty(NNStringBuffer *sb, const char *str, size_t len, bool onstack);
bool nn_strbuf_makebasicemptystack(NNStringBuffer *sb, const char *str, size_t len);
NNStringBuffer *nn_strbuf_makebasicempty(const char *str, size_t len);
bool nn_strbuf_destroyfromstack(NNStringBuffer *sb);
bool nn_strbuf_destroy(NNStringBuffer *sb);
void nn_strbuf_reset(NNStringBuffer *sb);
bool nn_strbuf_ensurecapacity(NNStringBuffer *sb, size_t len);
bool nn_strbuf_resize(NNStringBuffer *sb, size_t newlen);
bool nn_strbuf_setlength(NNStringBuffer *sb, size_t len);
bool nn_strbuf_setdata(NNStringBuffer *sb, char *str);
size_t nn_strbuf_length(NNStringBuffer *sb);
const char *nn_strbuf_data(NNStringBuffer *sb);
int nn_strbuf_get(NNStringBuffer *sb, size_t idx);
bool nn_strbuf_containschar(NNStringBuffer *sb, char ch);
bool nn_strbuf_fullreplace(NNStringBuffer *sb, const char *findstr, size_t findlen, const char *substr, size_t sublen);
bool nn_strbuf_charreplace(NNStringBuffer *sb, int findme, const char *substr, size_t sublen);
bool nn_strbuf_set(NNStringBuffer *sb, size_t idx, int b);
bool nn_strbuf_appendchar(NNStringBuffer *sb, int c);
bool nn_strbuf_appendstrn(NNStringBuffer *sb, const char *str, size_t len);
bool nn_strbuf_appendstr(NNStringBuffer *sb, const char *str);
bool nn_strbuf_appendbuff(NNStringBuffer *sb1, NNStringBuffer *sb2);
size_t nn_strutil_numofdigits(unsigned long v);
bool nn_strbuf_appendnumulong(NNStringBuffer *sb, unsigned long value);
bool nn_strbuf_appendnumlong(NNStringBuffer *sb, long value);
bool nn_strbuf_appendnumint(NNStringBuffer *sb, int value);
bool nn_strbuf_appendstrnlowercase(NNStringBuffer *sb, const char *str, size_t len);
bool nn_strbuf_appendstrnuppercase(NNStringBuffer *sb, const char *str, size_t len);
void nn_strbuf_shrink(NNStringBuffer *sb, size_t len);
size_t nn_strbuf_chomp(NNStringBuffer *sb);
void nn_strbuf_reverse(NNStringBuffer *sb);
char *nn_strbuf_substr(NNStringBuffer *sb, size_t start, size_t len);
void nn_strbuf_touppercase(NNStringBuffer *sb);
void nn_strbuf_tolowercase(NNStringBuffer *sb);
void nn_strbuf_copyover(NNStringBuffer *sb, size_t dstpos, const char *src, size_t len);
void nn_strbuf_insert(NNStringBuffer *sb, size_t dstpos, const char *src, size_t len);
void nn_strbuf_overwrite(NNStringBuffer *sb, size_t dstpos, size_t dstlen, const char *src, size_t srclen);
void nn_strbuf_erase(NNStringBuffer *sb, size_t pos, size_t len);
int nn_strbuf_appendformatposv(NNStringBuffer *sb, size_t pos, const char *fmt, va_list argptr);
int nn_strbuf_appendformatv(NNStringBuffer *sb, const char *fmt, va_list argptr);
int nn_strbuf_appendformat(NNStringBuffer *sb, const char *fmt, ...);
int nn_strbuf_appendformatat(NNStringBuffer *sb, size_t pos, const char *fmt, ...);
int nn_strbuf_appendformatnoterm(NNStringBuffer *sb, size_t pos, const char *fmt, ...);
void nn_strbuf_triminplace(NNStringBuffer *sb);
void nn_strbuf_trimleftinplace(NNStringBuffer *sb, const char *list);
void nn_strbuf_trimrightinplace(NNStringBuffer *sb, const char *list);
double nn_string_tabhashvaluecombine(const char *data, size_t len, uint32_t hsv);
void nn_string_strtabstore(NNObjString *os);
NNObjString *nn_string_strtabfind(const char *str, size_t len, uint32_t hsv);
NNObjString *nn_string_makefromstrbuf(NNStringBuffer buf, uint32_t hsv, size_t length);
void nn_string_destroy(NNObjString *str);
NNObjString *nn_string_internlen(const char *strdata, int length);
NNObjString *nn_string_intern(const char *strdata);
NNObjString *nn_string_takelen(char *strdata, int length);
NNObjString *nn_string_takecstr(char *strdata);
NNObjString *nn_string_copylen(const char *strdata, int length);
NNObjString *nn_string_copycstr(const char *strdata);
NNObjString *nn_string_copyobject(NNObjString *origos);
const char *nn_string_getdata(NNObjString *os);
char *nn_string_mutdata(NNObjString *os);
size_t nn_string_getlength(NNObjString *os);
bool nn_string_setlength(NNObjString *os, size_t nlen);
bool nn_string_set(NNObjString *os, size_t idx, int byte);
int nn_string_get(NNObjString *os, size_t idx);
bool nn_string_appendstringlen(NNObjString *os, const char *str, size_t len);
bool nn_string_appendstring(NNObjString *os, const char *str);
bool nn_string_appendobject(NNObjString *os, NNObjString *other);
bool nn_string_appendbyte(NNObjString *os, int ch);
bool nn_string_appendnumulong(NNObjString *os, unsigned long val);
bool nn_string_appendnumint(NNObjString *os, int val);
int nn_string_appendfmtv(NNObjString *os, const char *fmt, va_list va);
int nn_string_appendfmt(NNObjString *os, const char *fmt, ...);
NNObjString *nn_string_substrlen(NNObjString *os, size_t start, size_t maxlen);
NNObjString *nn_string_substr(NNObjString *os, size_t start);
NNObjString *nn_string_substring(NNObjString *selfstr, size_t start, size_t end, bool likejs);
void nn_state_installobjectstring(NNState *state);
/* main.c */
int main(int argc, char *argv[], char **envp);
int replmain(const char *file);
/* mem.c */
void nn_memory_init(void);
void nn_memory_finish(void);
void *nn_memory_setsize(void *p, size_t sz);
size_t nn_memory_getsize(void *p);
void *nn_memory_malloc(size_t sz);
void *nn_memory_realloc(void *p, size_t nsz);
void *nn_memory_calloc(size_t count, size_t typsize);
void nn_memory_free(void *ptr);
NNObject *nn_gcmem_protect(NNState *state, NNObject *object);
void nn_gcmem_maybecollect(int addsize, bool wasnew);
void *nn_gcmem_allocate(size_t newsize, size_t amount, bool retain);
void nn_gcmem_release(void *pointer, size_t oldsize);
void nn_gcmem_markobject(NNObject *object);
void nn_gcmem_markvalue(NNValue value);
void nn_gcmem_blackenobject(NNObject *object);
void nn_object_destroy(NNObject *object);
void nn_gcmem_markroots(NNState *state);
void nn_gcmem_tracerefs();
void nn_gcmem_sweep();
void nn_gcmem_destroylinkedobjects(NNState *state);
void nn_gcmem_collectgarbage();
void nn_gcmem_markcompilerroots();
/* modast.c */
NNDefModule *nn_natmodule_load_astscan();
/* modcplx.c */
NNDefModule *nn_natmodule_load_complex();
/* modglobal.c */
void nn_state_initbuiltinfunctions(NNState *state);
/* modnull.c */
NNDefModule *nn_natmodule_load_null();
/* modos.c */
void nn_modfn_os_preloader(NNState *state);
NNValue nn_modfn_os_stat(NNState *state, NNValue thisval, NNValue *argv, size_t argc);
NNDefModule *nn_natmodule_load_os();
/* object.c */
NNObject *nn_object_allocobject(size_t size, NNObjType type, bool retain);
NNObjUpvalue *nn_object_makeupvalue(NNValue *slot, int stackpos);
NNObjUserdata *nn_object_makeuserdata(void *pointer, const char *name);
NNObjModule *nn_module_make(const char *name, const char *file, bool imported, bool retain);
NNObjSwitch *nn_object_makeswitch();
NNObjRange *nn_object_makerange(int lower, int upper);

/* parsercc.c */
void nn_blob_init(NNBlob *blob);
void nn_blob_push(NNBlob *blob, NNInstruction ins);
void nn_blob_destroy(NNBlob *blob);
int nn_blob_pushconst(NNBlob *blob, NNValue value);
void nn_astlex_init(NNAstLexer *lex, NNState *state, const char *source);
NNAstLexer *nn_astlex_make(NNState *state, const char *source);
void nn_astlex_destroy(NNAstLexer *lex);
bool nn_astlex_isatend(NNAstLexer *lex);
const char *nn_astutil_toktype2str(int t);
NNAstToken nn_astlex_scanstring(NNAstLexer *lex, char quote, bool withtemplate, bool permitescapes);
NNAstToken nn_astlex_scannumber(NNAstLexer *lex);
NNAstToken nn_astlex_scanident(NNAstLexer *lex, bool isdollar);
NNAstToken nn_astlex_scantoken(NNAstLexer *lex);
NNAstParser *nn_astparser_makeparser(NNState *state, NNAstLexer *lexer, NNObjModule *module, bool keeplast);
void nn_astparser_destroy(NNAstParser *parser);
bool nn_astparser_istype(NNAstTokType prev, NNAstTokType t);
NNValue nn_astparser_compilestrnumber(NNAstTokType type, const char *source);
NNAstRule *nn_astparser_getrule(NNAstTokType type);
NNObjFunction *nn_astparser_compilesource(NNState *state, NNObjModule *module, const char *source, NNBlob *blob, bool fromimport, bool keeplast);
/* utf.c */
void nn_utf8iter_init(utf8iterator_t *iter, const char *ptr, uint32_t length);
uint8_t nn_utf8iter_charsize(const char *character);
uint32_t nn_utf8iter_converter(const char *character, uint8_t size);
uint8_t nn_utf8iter_next(utf8iterator_t *iter);
const char *nn_utf8iter_getchar(utf8iterator_t *iter);
int nn_util_utf8numbytes(int value);
char *nn_util_utf8encode(unsigned int code, size_t *dlen);
int nn_util_utf8decode(const uint8_t *bytes, uint32_t length);
char *nn_util_utf8codepoint(const char *str, char *outcodepoint);
char *nn_util_utf8strstr(const char *haystack, const char *needle);
char *nn_util_utf8index(char *s, int pos);
void nn_util_utf8slice(char *s, int *start, int *end);
/* utilfmt.c */
void nn_strformat_init(NNState *state, NNFormatInfo *nfi, NNIOStream *writer, const char *fmtstr, size_t fmtlen);
void nn_strformat_destroy(NNFormatInfo *nfi);
bool nn_strformat_format(NNFormatInfo *nfi, int argc, int argbegin, NNValue *argv);
/* utilhmap.c */
void nn_valtable_mark(NNHashTable<NNValue, NNValue> *table);
void nn_valtable_removewhites(NNHashTable<NNValue, NNValue>* table);
/* utilstd.c */
size_t nn_util_rndup2pow64(uint64_t x);
const char *nn_util_color(NNColor tc);
char *nn_util_strndup(const char *src, size_t len);
char *nn_util_strdup(const char *src);
void nn_util_mtseed(uint32_t seed, uint32_t *binst, uint32_t *index);
uint32_t nn_util_mtgenerate(uint32_t *binst, uint32_t *index);
double nn_util_mtrand(double lowerlimit, double upperlimit);
char *nn_util_filereadhandle(FILE *hnd, size_t *dlen, bool havemaxsz, size_t maxsize);
char *nn_util_filereadfile(NNState *state, const char *filename, size_t *dlen, bool havemaxsz, size_t maxsize);
char *nn_util_filegetshandle(char *s, int size, FILE *f, size_t *lendest);
int nn_util_filegetlinehandle(char **lineptr, size_t *destlen, FILE *hnd);
char *nn_util_strtoupper(char *str, size_t length);
char *nn_util_strtolower(char *str, size_t length);
NNObjString *nn_util_numbertobinstring(NNState *state, long n);
NNObjString *nn_util_numbertooctstring(NNState *state, int64_t n, bool numeric);
NNObjString *nn_util_numbertohexstring(NNState *state, int64_t n, bool numeric);
uint32_t nn_object_hashobject(NNObject *object);
uint32_t nn_value_hashvalue(NNValue value);
/* value.c */
NNValue nn_value_copystrlen(const char *str, size_t len);
NNValue nn_value_copystr(const char *str);
NNObjString *nn_value_tostring(NNValue value);
const char *nn_value_objecttypename(NNObject *object, bool detailed);
const char *nn_value_typename(NNValue value, bool detailed);
bool nn_value_isfalse(NNValue value);
bool nn_value_compobjarray(NNObject *oa, NNObject *ob);
bool nn_value_compobjstring(NNObject *oa, NNObject *ob);
bool nn_value_compobjdict(NNObject *oa, NNObject *ob);
bool nn_value_compobject(NNValue a, NNValue b);
bool nn_value_compare_actual(NNValue a, NNValue b);
bool nn_value_compare(NNValue a, NNValue b);
NNValue nn_value_findgreater(NNValue a, NNValue b);
void nn_value_sortvalues(NNState *state, NNValue *values, int count);
NNValue nn_value_copyvalue(NNValue value);
/* vm.c */
void nn_vm_initvmstate(NNState *state);
void nn_state_resetvmstate(NNState *state);
bool nn_vm_callclosure(NNState *state, NNObjFunction *closure, NNValue thisval, int argcount, bool fromoperator);
bool nn_vm_callvaluewithobject(NNState *state, NNValue callable, NNValue thisval, int argcount, bool fromoper);
bool nn_vm_callvalue(NNState *state, NNValue callable, NNValue thisval, int argcount, bool fromoperator);
NNObjClass *nn_value_getclassfor(NNState *state, NNValue receiver);
void nn_vm_stackpush(NNState *state, NNValue value);
NNValue nn_vm_stackpop(NNState *state);
NNValue nn_vm_stackpopn(NNState *state, int n);
NNValue nn_vm_stackpeek(NNState *state, int distance);
bool nn_util_isinstanceof(NNObjClass *klass1, NNObjClass *expected);
NNStatus nn_vm_runvm(NNState *state, int exitframe, NNValue *rv);
int nn_nestcall_prepare(NNValue callable, NNValue mthobj, NNValue *callarr, int maxcallarr);
bool nn_nestcall_callfunction(NNState *state, NNValue callable, NNValue thisval, NNValue *argv, size_t argc, NNValue *dest, bool fromoper);
/* strbuf.h */
size_t nn_strutil_rndup2pow64(uint64_t x);
size_t nn_strutil_splitstr(char *str, char sep, char **ptrs, size_t nptrs);
size_t nn_strutil_charreplace(char *str, char from, char to);
void nn_strutil_reverseregion(char *str, size_t length);
char *nn_strutil_nextspace(char *s);
char *nn_strutil_trim(char *str);
size_t nn_strutil_chomp(char *str, size_t len);
size_t nn_strutil_countchar(const char *str, char c);
size_t nn_strutil_split(const char *splitat, const char *sourcetxt, char ***result);
void nn_strutil_faststrncat(char *dest, const char *src, size_t *size);
size_t nn_strutil_strreplace1(char **str, size_t selflen, const char *findstr, size_t findlen, const char *substr, size_t sublen);
size_t nn_strutil_strrepcount(const char *str, size_t slen, const char *findstr, size_t findlen, size_t sublen);
void nn_strutil_strreplace2(char *target, size_t tgtlen, const char *findstr, size_t findlen, const char *substr, size_t sublen);
size_t nn_strutil_inpreplace(char *target, size_t tgtlen, int findme, const char *substr, size_t sublen, size_t maxlen);
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

NEON_FORCEINLINE uint32_t nn_util_hashstring(const char *str, size_t length)
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
        for (j = 7; j >= 0; j--)
        {
            mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
        i = i + 1;
        ci++;
    }
    return ~crc;
}


struct NNConstClassMethodItem
{
    const char* name;
    NNNativeFN fn;
};

struct utf8iterator_t
{
    /*input string pointer */
    const char* plainstr;

    /* input string length */
    uint32_t plainlen;

    /* the codepoint, or char */
    uint32_t codepoint;

    /* character size in bytes */
    uint8_t charsize;

    /* current character position */
    uint32_t currpos;

    /* next character position */
    uint32_t nextpos;

    /* number of counter characters currently */
    uint32_t currcount;
};

struct NNIOResult
{
    uint8_t success;
    char* data;
    size_t length;    
};

struct NNIOStream
{
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
    NNPrMode wrmode;
    NNStringBuffer strbuf;
    FILE* handle;
};

struct NNFormatInfo
{
    /* length of the format string */
	size_t fmtlen;
    /* the actual format string */
	const char* fmtstr;
	NNIOStream* writer;
	NNState* pstate;
};

struct NNValData
{
    public:
        NNValType type;
        union
        {
            uint8_t vbool;
            double vfltnum;
            NNObject* vobjpointer;
        } valunion;

    public:
        NEON_INLINE bool isNull() const
        {
            return (this->type == NEON_VALTYPE_NULL);
        }

};

template<typename StoredType>
struct NNValArray
{
    public:
        void deInit(bool actuallydelete)
        {
            #if 0
            if(this->listname != nullptr)
            {
                fprintf(stderr, "vallist of '%s' use at end: count=%ld capacity=%ld\n", this->listname, this->listcount, this->listcapacity);
            }
            #endif            
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
            #if 1
                return nn_util_rndup2pow64(capacity+1);
            #else
                return (capacity * 2);
            #endif
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
        NNValData* listitems = nullptr;
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

struct NNPropGetSet
{
    NNValData getter;
    NNValData setter;
};

struct NNProperty
{
    bool havegetset;
    NNFieldType type;
    NNValData value;
    NNPropGetSet getset;
};

template<typename HTKeyT, typename HTValT>
struct NNHashTable
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
            #if 1
                return nn_util_rndup2pow64(capacity+1);
            #else
                return (capacity * 2);
            #endif
        }

        NNObjString* findstring(const char* findstr, size_t findlen, uint32_t findhash);
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
                if(nn_value_isstring((HTKeyT)entry->key))
                {
                    entoskey = nn_value_asstring((HTKeyT)entry->key);
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
            entry = this->findentrybyvalue(this->htentries, this->htcapacity, key);
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
            entry = this->findentrybystr(this->htentries, this->htcapacity, valkey, kstr, klen, hsv);
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
            field = this->getfield(key);
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
                entries[i].value = nn_property_make(HTValT{}, NEON_PROPTYPE_VALUE);
            }
            this->htcount = 0;
            for(i = 0; i < this->htcapacity; i++)
            {
                entry = &this->htentries[i];
                if(entry->key.isNull())
                {
                    continue;
                }
                dest = this->findentrybyvalue(entries, capacity, (HTKeyT)entry->key);
                dest->key = entry->key;
                dest->value = entry->value;
                this->htcount++;
            }
            nn_memory_free(this->htentries);
            this->htentries = entries;
            this->htcapacity = capacity;
            return true;
        }

        bool setwithtype(HTKeyT key, HTValT value, NNFieldType ftyp, bool keyisstring)
        {
            bool isnew;
            int capacity;
            Entry* entry;
            (void)keyisstring;
            if((this->htcount + 1) > (this->htcapacity * NEON_CONFIG_MAXTABLELOAD))
            {
                capacity = getNextCapacity(this->htcapacity);
                if(!this->adjustcapacity(capacity))
                {
                    return false;
                }
            }
            entry = this->findentrybyvalue(this->htentries, this->htcapacity, key);
            isnew = entry->key.isNull();
            if(isnew && entry->value.value.isNull())
            {
                this->htcount++;
            }
            /* overwrites existing entries. */
            entry->key = key;
            entry->value = nn_property_make(value, ftyp);
            return isnew;
        }

        bool set(HTKeyT key, HTValT value)
        {
            return this->setwithtype(key, value, NEON_PROPTYPE_VALUE, nn_value_isstring(key));
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
                if(!entry->key.isNull() && !nn_value_ismodule((HTValT)entry->value.value))
                {
                    /* Don't import private values */
                    if(nn_value_isstring((HTKeyT)entry->key) && nn_string_getdata(nn_value_asstring((HTKeyT)entry->key))[0] == '_')
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

        template<typename InputValT>
        HTKeyT findkey(InputValT value, InputValT defval)
        {
            int i;
            Entry* entry;
            NN_NULLPTRCHECK_RETURNVALUE(this, nn_value_makenull());
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


struct NNInstruction
{
    /* opcode or value */
    uint8_t code;
    /* is this instruction an opcode? */
    uint8_t isop: 1;
    /* line corresponding to where this instruction was emitted */
    uint8_t fromsourceline;
};

struct NNBlob
{
    int count;
    int capacity;
    NNInstruction* instrucs;
    NNValArray<NNValue> constants;
    NNValArray<NNValue> argdefvals;
};


struct NNExceptionFrame
{
    uint16_t address;
    uint16_t finallyaddress;
    //NNObjClass* handlerklass;
};

struct NNCallFrame
{
    int handlercount;
    int gcprotcount;
    int stackslotpos;
    NNInstruction* inscode;
    NNObjFunction* closure;
    /* TODO: should be dynamically allocated */
    NNExceptionFrame handlers[NEON_CONFIG_MAXEXCEPTHANDLERS];
};



struct GCSingleton
{
    public:
        static GCSingleton* m_myself;
        NNObject* linkedobjects;
        bool markvalue;
        struct
        {
            int64_t graycount;
            int64_t graycapacity;
            int64_t bytesallocated;
            int64_t nextgc;
            NNObject** graystack;
        } gcstate;

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
        } vmstate;

        NNHashTable<NNValue, NNValue> allocatedstrings;
        NNHashTable<NNValue, NNValue> openedmodules;
        NNHashTable<NNValue, NNValue> declaredglobals;


    private:
        static void defVarsFor(GCSingleton* gcs)
        {
            gcs->markvalue = true;
            gcs->gcstate.bytesallocated = 0;
            /* default is 1mb. Can be modified via the -g flag. */
            gcs->gcstate.nextgc = NEON_CONFIG_DEFAULTGCSTART;
            gcs->gcstate.graycount = 0;
            gcs->gcstate.graycapacity = 0;
            gcs->gcstate.graystack = nullptr;            
        }

    public:
        static void init()
        {
            GCSingleton::m_myself = new GCSingleton();
            defVarsFor(GCSingleton::m_myself);
        }

        static void destroy()
        {
            delete GCSingleton::m_myself;
        }

        static GCSingleton* get()
        {
            return GCSingleton::m_myself;
        }

        static void clearGCProtect()
        {
            size_t frpos;
            NNCallFrame* frame;
            frpos = 0;
            if(GCSingleton::get()->vmstate.framecount > 0)
            {
                frpos = GCSingleton::get()->vmstate.framecount - 1;
            }
            frame = &GCSingleton::get()->vmstate.framevalues[frpos];
            if(frame->gcprotcount > 0)
            {
                if(frame->gcprotcount > 0)
                {
                    GCSingleton::get()->vmstate.stackidx -= frame->gcprotcount;
                }
                frame->gcprotcount = 0;
            }
        }

};
GCSingleton* GCSingleton::m_myself = nullptr;


struct NNObject
{
	NNObjType type;
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

struct NNObjString: public NNObject
{
    uint32_t hashvalue;
    NNStringBuffer sbuf;
};

struct NNObjUpvalue: public NNObject
{
    int stackpos;
    NNValData closed;
    NNValData location;
    NNObjUpvalue* next;
};

struct NNObjModule: public NNObject
{
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
struct NNObjClass: public NNObject
{

    /*
    * the constructor, if any. defaults to <empty>, and if not <empty>, expects to be
    * some callable value.
    */
    NNValData constructor;
    NNValData destructor;

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

struct NNObjInstance: public NNObject
{
    /*
    * whether this instance is still "active", i.e., not destroyed, deallocated, etc.
    */
    bool active;
    NNHashTable<NNValue, NNValue> properties;
    NNObjClass* klass;
    NNObjInstance* superinstance;
};

struct NNObjFunction: public NNObject
{
	NNFuncContextType contexttype;
	NNObjString* name;
	int upvalcount;
	NNValData clsthisval;
	union {
		struct {
			NNObjFunction * scriptfunc;
			NNObjUpvalue * * upvalues;
		} fnclosure;
		struct {
			int arity;
			bool isvariadic;
			NNBlob blob;
			NNObjModule* module;
		} fnscriptfunc;
		struct {
			NNNativeFN natfunc;
			void* userptr;
		} fnnativefunc;
		struct {
			NNValData receiver;
			NNObjFunction* method;
		} fnmethod;
	};
};

struct NNObjArray: public NNObject
{
    NNValArray<NNValue> varray;
};

struct NNObjRange: public NNObject
{
    int lower;
    int upper;
    int range;
};

struct NNObjDict: public NNObject
{
    NNValArray<NNValue> htnames;
    NNHashTable<NNValue, NNValue> htab;
};

struct NNObjFile: public NNObject
{
    bool isopen;
    bool isstd;
    bool istty;
    int number;
    FILE* handle;
    NNObjString* mode;
    NNObjString* path;
};

struct NNObjSwitch: public NNObject
{
    int defaultjump;
    int exitjump;
    NNHashTable<NNValue, NNValue> table;
};

struct NNObjUserdata: public NNObject
{
    void* pointer;
    char* name;
    NNPtrFreeFN ondestroyfn;
};

struct NNValue: public NNValData
{

};

struct NNProcessInfo
{
    int cliprocessid;
    NNObjArray* cliargv;
    NNObjString* cliexedirectory;
    NNObjString* cliscriptfile;
    NNObjString* cliscriptdirectory;
    NNObjFile* filestdout;
    NNObjFile* filestderr;
    NNObjFile* filestdin;
};

struct NNState
{
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
    } conf;


    struct {
        NNObjClass* stdexception;
        NNObjClass* syntaxerror;
        NNObjClass* asserterror;
        NNObjClass* ioerror;
        NNObjClass* oserror;
        NNObjClass* argumenterror;
        NNObjClass* regexerror;
        NNObjClass* importerror;
    } exceptions;

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
    } defaultstrings;

    NNValue lastreplvalue;

    void* memuserptr;
    const char* rootphysfile;

    NNObjDict* envdict;
    NNObjModule* topmodule;
    NNValArray<NNValue> importpath;

    /* objects tracker */

    /*
    * these classes are used for runtime objects, specifically in nn_value_getclassfor.
    * every other global class needed (as created by nn_util_makeclass) need NOT be declared here.
    * simply put, these classes are primitive types.
    */
    /* the class from which every other class derives */
    NNObjClass* classprimobject;
    /**/
    NNObjClass* classprimclass;
    /* class for numbers, et al */
    NNObjClass* classprimnumber;
    /* class for strings */
    NNObjClass* classprimstring;
    /* class for arrays */
    NNObjClass* classprimarray;
    /* class for dictionaries */
    NNObjClass* classprimdict;
    /* class for I/O file objects */
    NNObjClass* classprimfile;
    /* class for range constructs */
    NNObjClass* classprimrange;
    /* class for anything callable: functions, lambdas, constructors ... */
    NNObjClass* classprimcallable;
    NNObjClass* classprimprocess;

    bool isrepl;
    NNProcessInfo* processinfo;

    /* miscellaneous */
    NNIOStream* stdoutprinter;
    NNIOStream* stderrprinter;
    NNIOStream* debugwriter;
};

struct NNAstToken
{
    bool isglobal;
    NNAstTokType type;
    const char* start;
    int length;
    int line;
};

struct NNAstLexer
{
    NNState* pstate;
    bool onstack;
    const char* start;
    const char* sourceptr;
    int line;
    int tplstringcount;
    int tplstringbuffer[NEON_CONFIG_ASTMAXSTRTPLDEPTH];
};


struct NNAstLocal
{
    bool iscaptured;
    int depth;
    NNAstToken name;
};

struct NNAstUpvalue
{
    bool islocal;
    uint16_t index;
};

struct NNAstFuncCompiler
{
    int localcount;
    int scopedepth;
    int handlercount;
    bool fromimport;
    NNAstFuncCompiler* enclosing;
    /* current function */
    NNObjFunction* targetfunc;
    NNFuncContextType contexttype;
    /* TODO: these should be dynamically allocated */
    NNAstLocal locals[NEON_CONFIG_ASTMAXLOCALS];
    NNAstUpvalue upvalues[NEON_CONFIG_ASTMAXUPVALS];
};

struct NNAstClassCompiler
{
    bool hassuperclass;
    NNAstClassCompiler* enclosing;
    NNAstToken name;
};

struct NNAstParser
{
    bool haderror;
    bool panicmode;
    bool isreturning;
    bool istrying;
    bool replcanecho;
    bool keeplastvalue;
    bool lastwasstatement;
    bool infunction;
    bool inswitch;
    bool stopprintingsyntaxerrors;
    
    /* used for tracking loops for the continue statement... */
    int innermostloopstart;
    int innermostloopscopedepth;
    int blockcount;
    int errorcount;
    /* the context in which the parser resides; none (outer level), inside a class, dict, array, etc */
    NNAstCompContext compcontext;
    const char* currentfile;
    NNState* pstate;
    NNAstLexer* lexer;
    NNAstToken currtoken;
    NNAstToken prevtoken;
    NNAstFuncCompiler* currentfunccompiler;
    NNAstClassCompiler* currentclasscompiler;
    NNObjModule* currentmodule;
};

struct NNAstRule
{
    NNAstParsePrefixFN prefix;
    NNAstParseInfixFN infix;
    NNAstPrecedence precedence;
};

struct NNDefFunc
{
    const char* name;
    bool isstatic;
    NNNativeFN function;
};

struct NNDefField
{
    const char* name;
    bool isstatic;
    NNNativeFN fieldvalfn;
};

struct NNDefClass
{
    const char* name;
    NNDefField* defpubfields;
    NNDefFunc* defpubfunctions;
};

struct NNDefModule
{
    /*
    * the name of this module.
    * note: if the name must be preserved, copy it; it is only a pointer to a
    * string that gets freed past loading.
    */
    const char* name;

    /* exported fields, if any. */
    NNDefField* definedfields;

    /* regular functions, if any. */
    NNDefFunc* definedfunctions;

    /* exported classes, if any.
    * i.e.:
    * {"Stuff",
    *   (NNDefField[]){
    *       {"enabled", true},
    *       ...
    *   },
    *   (NNDefFunc[]){
    *       {"isStuff", myclass_fn_isstuff},
    *       ...
    * }})*/
    NNDefClass* definedclasses;

    /* function that is called directly upon loading the module. can be nullptr. */
    NNModLoaderFN fnpreloaderfunc;

    /* function that is called before unloading the module. can be nullptr. */
    NNModLoaderFN fnunloaderfunc;
};

struct NNArgCheck
{
    NNState* pstate;
    const char* name;
    int argc;
    NNValue* argv;
};

#if defined(NEON_CONFIG_DEBUGMEMORY) && (NEON_CONFIG_DEBUGMEMORY == 1)
    #define nn_memory_malloc(...) nn_memory_debugmalloc(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)
    #define nn_memory_calloc(...) nn_memory_debugcalloc(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)
    #define nn_memory_realloc(...) nn_memory_debugrealloc(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)
#endif





/* bottom */
template<typename HTKeyT, typename HTValT>
NNProperty* NNHashTable<HTKeyT, HTValT>::getfieldbyostr(NNObjString* str)
{
    return this->getfieldbystr(nn_value_makenull(), nn_string_getdata(str), nn_string_getlength(str), str->hashvalue);
}

template<typename HTKeyT, typename HTValT>
NNProperty* NNHashTable<HTKeyT, HTValT>::getfieldbycstr(const char* kstr)
{
    size_t klen;
    uint32_t hsv;
    klen = strlen(kstr);
    hsv = nn_util_hashstring(kstr, klen);
    return this->getfieldbystr(nn_value_makenull(), kstr, klen, hsv);
}

template<typename HTKeyT, typename HTValT>
NNProperty* NNHashTable<HTKeyT, HTValT>::getfield(HTKeyT key)
{
    NNObjString* oskey;
    if(nn_value_isstring(key))
    {
        oskey = nn_value_asstring(key);
        return this->getfieldbystr(key, nn_string_getdata(oskey), nn_string_getlength(oskey), oskey->hashvalue);
    }
    return this->getfieldbyvalue(key);
}

template<typename HTKeyT, typename HTValT>
bool NNHashTable<HTKeyT, HTValT>::remove(HTKeyT key)
{
    Entry* entry;
    if(this->htcount == 0)
    {
        return false;
    }
    /* find the entry */
    entry = this->findentrybyvalue(this->htentries, this->htcapacity, key);
    if(entry->key.isNull())
    {
        return false;
    }
    /* place a tombstone in the entry. */
    entry->key = HTKeyT{};
    entry->value = nn_property_make(nn_value_makebool(true), NEON_PROPTYPE_VALUE);
    return true;
}

template<typename HTKeyT, typename HTValT>
NNObjString* NNHashTable<HTKeyT, HTValT>::findstring(const char* findstr, size_t findlen, uint32_t findhash)
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
    NN_NULLPTRCHECK_RETURNVALUE(this, nullptr);
    if(this->htcount == 0)
    {
        return nullptr;
    }
    #if defined(NEON_CONF_LIBSTRINGUSEHASH) && (NEON_CONF_LIBSTRINGUSEHASH == 1)
    wanteddn = nn_string_tabhashvaluecombine(findstr, findlen, findhash);
    #endif
    index = findhash & (this->htcapacity - 1);
    while(true)
    {
        auto entry = &this->htentries[index];
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
            dn = nn_value_asnumber(entry->key);
            if(dn == wanteddn)
            {
                return nn_value_asstring(entry->value.value);
            }
        #else
            string = nn_value_asstring((HTKeyT)entry->key);
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
        index = (index + 1) & (this->htcapacity - 1);
    }
    return nullptr;
}



NEON_INLINE bool nn_value_isobject(NNValue v)
{
    return ((v).type == NEON_VALTYPE_OBJ);
}

NEON_INLINE NNObject* nn_value_asobject(NNValue v)
{
    return ((v).valunion.vobjpointer);
}

NEON_INLINE bool nn_value_isobjtype(NNValue v, NNObjType t)
{
    return nn_value_isobject(v) && (nn_value_asobject(v)->type == t);
}


NEON_INLINE bool nn_value_isbool(NNValue v)
{
    return ((v).type == NEON_VALTYPE_BOOL);
}

NEON_INLINE bool nn_value_isnumber(NNValue v)
{
    return ((v).type == NEON_VALTYPE_NUMBER);
}

NEON_INLINE bool nn_value_isstring(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_STRING);
}

NEON_INLINE bool nn_value_isfuncnative(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCNATIVE);
}

NEON_INLINE bool nn_value_isfuncscript(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCSCRIPT);
}

NEON_INLINE bool nn_value_isfuncclosure(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCCLOSURE);
}

NEON_INLINE bool nn_value_isfuncbound(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCBOUND);
}

NEON_INLINE bool nn_value_isclass(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_CLASS);
}

NEON_INLINE bool nn_value_isinstance(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_INSTANCE);
}

NEON_INLINE bool nn_value_isarray(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_ARRAY);
}

NEON_INLINE bool nn_value_isdict(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_DICT);
}

NEON_INLINE bool nn_value_isfile(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FILE);
}

NEON_INLINE bool nn_value_isrange(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_RANGE);
}

NEON_INLINE bool nn_value_ismodule(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_MODULE);
}

NEON_INLINE bool nn_value_iscallable(NNValue v)
{
    return (
        nn_value_isclass(v) ||
        nn_value_isfuncscript(v) ||
        nn_value_isfuncclosure(v) ||
        nn_value_isfuncbound(v) ||
        nn_value_isfuncnative(v)
    );
}

NEON_INLINE const char* nn_value_typefromfunction(NNValIsFuncFN func)
{
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


NEON_INLINE NNObjType nn_value_objtype(NNValue v)
{
    return nn_value_asobject(v)->type;
}

NEON_INLINE double nn_value_asnumber(NNValue v)
{
    return ((v).valunion.vfltnum);
}

NEON_INLINE bool nn_value_asbool(NNValue v)
{
    if(nn_value_isnumber(v))
    {
        return nn_value_asnumber(v);
    }
    return ((v).valunion.vbool);
}

NEON_INLINE NNObjString* nn_value_asstring(NNValue v)
{
    return ((NNObjString*)nn_value_asobject(v));
}

NEON_INLINE NNObjFunction* nn_value_asfunction(NNValue v)
{
    return ((NNObjFunction*)nn_value_asobject(v));
}

NEON_INLINE NNObjClass* nn_value_asclass(NNValue v)
{
    return ((NNObjClass*)nn_value_asobject(v));
}

NEON_INLINE NNObjInstance* nn_value_asinstance(NNValue v)
{
    return ((NNObjInstance*)nn_value_asobject(v));
}

NEON_INLINE NNObjSwitch* nn_value_asswitch(NNValue v)
{
    return ((NNObjSwitch*)nn_value_asobject(v));
}

NEON_INLINE NNObjUserdata* nn_value_asuserdata(NNValue v)
{
    return ((NNObjUserdata*)nn_value_asobject(v));
}

NEON_INLINE NNObjModule* nn_value_asmodule(NNValue v)
{
    return ((NNObjModule*)nn_value_asobject(v));
}

NEON_INLINE NNObjArray* nn_value_asarray(NNValue v)
{
    return ((NNObjArray*)nn_value_asobject(v));
}

NEON_INLINE NNObjDict* nn_value_asdict(NNValue v)
{
    return ((NNObjDict*)nn_value_asobject(v));
}

NEON_INLINE NNObjFile* nn_value_asfile(NNValue v)
{
    return ((NNObjFile*)nn_value_asobject(v));
}

NEON_INLINE NNObjRange* nn_value_asrange(NNValue v)
{
    return ((NNObjRange*)nn_value_asobject(v));
}

NEON_INLINE NNValue nn_value_makevalue(NNValType type)
{
    NNValue v;
    memset(&v, 0, sizeof(NNValue));
    v.type = type;
    return v;
}

NEON_INLINE NNValue nn_value_makenull()
{
    NNValue v;
    v = nn_value_makevalue(NEON_VALTYPE_NULL);
    return v;
}

NEON_INLINE NNValue nn_value_makebool(bool b)
{
    NNValue v;
    v = nn_value_makevalue(NEON_VALTYPE_BOOL);
    v.valunion.vbool = b;
    return v;
}

NEON_INLINE NNValue nn_value_makenumber(double d)
{
    NNValue v;
    v = nn_value_makevalue(NEON_VALTYPE_NUMBER);
    v.valunion.vfltnum = d;
    return v;
}


NEON_INLINE NNValue nn_value_fromobject_actual(NNObject* obj)
{
    NNValue v;
    v = nn_value_makevalue(NEON_VALTYPE_OBJ);
    v.valunion.vobjpointer = obj;
    return v;
}




NNObject* nn_gcmem_protect(NNState* state, NNObject* object)
{
    size_t frpos;
    nn_vm_stackpush(state, nn_value_fromobject(object));
    frpos = 0;
    if(GCSingleton::get()->vmstate.framecount > 0)
    {
        frpos = GCSingleton::get()->vmstate.framecount - 1;
    }
    GCSingleton::get()->vmstate.framevalues[frpos].gcprotcount++;
    return object;
}



void nn_gcmem_maybecollect(int addsize, bool wasnew)
{
    GCSingleton::get()->gcstate.bytesallocated += addsize;
    if(GCSingleton::get()->gcstate.nextgc > 0)
    {
        if(wasnew && GCSingleton::get()->gcstate.bytesallocated > GCSingleton::get()->gcstate.nextgc)
        {
            if(GCSingleton::get()->vmstate.currentframe && GCSingleton::get()->vmstate.currentframe->gcprotcount == 0)
            {
                nn_gcmem_collectgarbage();
            }
        }
    }
}

void* nn_gcmem_allocate(size_t newsize, size_t amount, bool retain)
{
    size_t oldsize;
    void* result;
    oldsize = 0;
    if(!retain)
    {
        nn_gcmem_maybecollect(newsize - oldsize, newsize > oldsize);
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

void nn_gcmem_release(void* pointer, size_t oldsize)
{
    nn_gcmem_maybecollect(-oldsize, false);
    if(oldsize > 0)
    {
        memset(pointer, 0, oldsize);
    }
    nn_memory_free(pointer);
    pointer = nullptr;
}

void nn_gcmem_markobject(NNObject* object)
{
    if(object == nullptr)
    {
        return;
    }
    if(object->mark == GCSingleton::get()->markvalue)
    {
        return;
    }
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_iostream_printf(state->debugwriter, "GC: marking object at <%p> ", (void*)object);
    nn_iostream_printvalue(state->debugwriter, nn_value_fromobject(object), false);
    nn_iostream_printf(state->debugwriter, "\n");
    #endif
    object->mark = GCSingleton::get()->markvalue;
    if(GCSingleton::get()->gcstate.graycapacity < GCSingleton::get()->gcstate.graycount + 1)
    {
        GCSingleton::get()->gcstate.graycapacity = NEON_MEMORY_GROWCAPACITY(GCSingleton::get()->gcstate.graycapacity);
        GCSingleton::get()->gcstate.graystack = (NNObject**)nn_memory_realloc(GCSingleton::get()->gcstate.graystack, sizeof(NNObject*) * GCSingleton::get()->gcstate.graycapacity);
        if(GCSingleton::get()->gcstate.graystack == nullptr)
        {
            fflush(stdout);
            fprintf(stderr, "GC encountered an error");
            abort();
        }
    }
    GCSingleton::get()->gcstate.graystack[GCSingleton::get()->gcstate.graycount++] = object;
}


void nn_valarray_mark(NNValArray<NNValue>* list)
{
    size_t i;
    NN_NULLPTRCHECK_RETURN(list);
    for(i=0; i<list->listcount; i++)
    {
        nn_gcmem_markvalue((NNValue)list->listitems[i]);
    }
}



void nn_gcmem_markvalue(NNValue value)
{
    if(nn_value_isobject(value))
    {
        nn_gcmem_markobject(nn_value_asobject(value));
    }
}

void nn_gcmem_blackenobject(NNObject* object)
{
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_iostream_printf(state->debugwriter, "GC: blacken object at <%p> ", (void*)object);
    nn_iostream_printvalue(state->debugwriter, nn_value_fromobject(object), false);
    nn_iostream_printf(state->debugwriter, "\n");
    #endif
    switch(object->type)
    {
        case NEON_OBJTYPE_MODULE:
            {
                NNObjModule* module;
                module = (NNObjModule*)object;
                nn_valtable_mark(&module->deftable);
            }
            break;
        case NEON_OBJTYPE_SWITCH:
            {
                NNObjSwitch* sw;
                sw = (NNObjSwitch*)object;
                nn_valtable_mark(&sw->table);
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                NNObjFile* file;
                file = (NNObjFile*)object;
                nn_file_mark(file);
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                NNObjDict* dict;
                dict = (NNObjDict*)object;
                nn_dict_mark(dict);
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                NNObjArray* list;
                list = (NNObjArray*)object;
                nn_valarray_mark(&list->varray);
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
            {
                NNObjFunction* bound;
                bound = (NNObjFunction*)object;
                nn_gcmem_markvalue((NNValue)bound->fnmethod.receiver);
                nn_gcmem_markobject((NNObject*)bound->fnmethod.method);
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                NNObjClass* klass;
                klass = (NNObjClass*)object;
                nn_gcmem_markobject((NNObject*)klass->name);
                nn_valtable_mark(&klass->instmethods);
                nn_valtable_mark(&klass->staticmethods);
                nn_valtable_mark(&klass->staticproperties);
                nn_gcmem_markvalue((NNValue)klass->constructor);
                nn_gcmem_markvalue((NNValue)klass->destructor);
                if(klass->superclass != nullptr)
                {
                    nn_gcmem_markobject((NNObject*)klass->superclass);
                }
            }
            break;
        case NEON_OBJTYPE_FUNCCLOSURE:
            {
                int i;
                NNObjFunction* closure;
                closure = (NNObjFunction*)object;
                nn_gcmem_markobject((NNObject*)closure->fnclosure.scriptfunc);
                for(i = 0; i < closure->upvalcount; i++)
                {
                    nn_gcmem_markobject((NNObject*)closure->fnclosure.upvalues[i]);
                }
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                NNObjFunction* function;
                function = (NNObjFunction*)object;
                nn_gcmem_markobject((NNObject*)function->name);
                nn_gcmem_markobject((NNObject*)function->fnscriptfunc.module);
                nn_valarray_mark(&function->fnscriptfunc.blob.constants);
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                NNObjInstance* instance;
                instance = (NNObjInstance*)object;
                nn_instance_mark(instance);
            }
            break;
        case NEON_OBJTYPE_UPVALUE:
            {
                auto upv = (NNObjUpvalue*)object;
                nn_gcmem_markvalue((NNValue)upv->closed);
            }
            break;
        case NEON_OBJTYPE_RANGE:
        case NEON_OBJTYPE_FUNCNATIVE:
        case NEON_OBJTYPE_USERDATA:
        case NEON_OBJTYPE_STRING:
            break;
    }
}

void nn_object_destroy(NNObject* object)
{
    if(object->stale)
    {
        return;
    }
    switch(object->type)
    {
        case NEON_OBJTYPE_MODULE:
            {
                NNObjModule* module;
                module = (NNObjModule*)object;
                nn_module_destroy(module);
                nn_gcmem_release(object, sizeof(NNObjModule));
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                NNObjFile* file;
                file = (NNObjFile*)object;
                nn_file_destroy(file);
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                NNObjDict* dict;
                dict = (NNObjDict*)object;
                nn_dict_destroy(dict);
                nn_gcmem_release(object, sizeof(NNObjDict));
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                NNObjArray* list;
                list = (NNObjArray*)object;
                list->varray.deInit(false);
                nn_gcmem_release(object, sizeof(NNObjArray));
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
            {
                /*
                // a closure may be bound to multiple instances
                // for this reason, we do not free closures when freeing bound methods
                */
                nn_gcmem_release(object, sizeof(NNObjFunction));
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                NNObjClass* klass;
                klass = (NNObjClass*)object;
                nn_class_destroy(klass);
            }
            break;
        case NEON_OBJTYPE_FUNCCLOSURE:
            {
                NNObjFunction* closure;
                closure = (NNObjFunction*)object;
                nn_gcmem_freearray(sizeof(NNObjUpvalue*), closure->fnclosure.upvalues, closure->upvalcount);
                /*
                // there may be multiple closures that all reference the same function
                // for this reason, we do not free functions when freeing closures
                */
                nn_gcmem_release(object, sizeof(NNObjFunction));
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                NNObjFunction* function;
                function = (NNObjFunction*)object;
                nn_funcscript_destroy(function);
                nn_gcmem_release(function, sizeof(NNObjFunction));
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                NNObjInstance* instance;
                instance = (NNObjInstance*)object;
                nn_instance_destroy(instance);
            }
            break;
        case NEON_OBJTYPE_FUNCNATIVE:
            {
                nn_gcmem_release(object, sizeof(NNObjFunction));
            }
            break;
        case NEON_OBJTYPE_UPVALUE:
            {
                nn_gcmem_release(object, sizeof(NNObjUpvalue));
            }
            break;
        case NEON_OBJTYPE_RANGE:
            {
                nn_gcmem_release(object, sizeof(NNObjRange));
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                NNObjString* string;
                string = (NNObjString*)object;
                nn_string_destroy(string);
            }
            break;
        case NEON_OBJTYPE_SWITCH:
            {
                NNObjSwitch* sw;
                sw = (NNObjSwitch*)object;
                sw->table.deinit();
                nn_gcmem_release(object, sizeof(NNObjSwitch));
            }
            break;
        case NEON_OBJTYPE_USERDATA:
            {
                NNObjUserdata* ptr;
                ptr = (NNObjUserdata*)object;
                if(ptr->ondestroyfn)
                {
                    ptr->ondestroyfn(ptr->pointer);
                }
                nn_gcmem_release(object, sizeof(NNObjUserdata));
            }
            break;
        default:
            break;
    }
}

void nn_gcmem_markroots()
{
    int i;
    int j;
    NNValue* slot;
    NNObjUpvalue* upvalue;
    NNExceptionFrame* handler;
    (void)handler;
    for(slot = GCSingleton::get()->vmstate.stackvalues; slot < &GCSingleton::get()->vmstate.stackvalues[GCSingleton::get()->vmstate.stackidx]; slot++)
    {
        nn_gcmem_markvalue(*slot);
    }
    for(i = 0; i < (int)GCSingleton::get()->vmstate.framecount; i++)
    {
        nn_gcmem_markobject((NNObject*)GCSingleton::get()->vmstate.framevalues[i].closure);
        for(j = 0; j < (int)GCSingleton::get()->vmstate.framevalues[i].handlercount; j++)
        {
            handler = &GCSingleton::get()->vmstate.framevalues[i].handlers[j];
            /*nn_gcmem_markobject((NNObject*)handler->klass);*/
        }
    }
    for(upvalue = GCSingleton::get()->vmstate.openupvalues; upvalue != nullptr; upvalue = upvalue->next)
    {
        nn_gcmem_markobject((NNObject*)upvalue);
    }
    nn_valtable_mark(&GCSingleton::get()->declaredglobals);
    nn_valtable_mark(&GCSingleton::get()->openedmodules);
    //nn_gcmem_markobject((NNObject*)state->exceptions.stdexception);
    nn_gcmem_markcompilerroots();
}

void nn_gcmem_tracerefs()
{
    NNObject* object;
    while(GCSingleton::get()->gcstate.graycount > 0)
    {
        GCSingleton::get()->gcstate.graycount--;
        object = GCSingleton::get()->gcstate.graystack[GCSingleton::get()->gcstate.graycount];
        nn_gcmem_blackenobject(object);
    }
}

void nn_gcmem_sweep()
{
    NNObject* object;
    NNObject* previous;
    NNObject* unreached;
    previous = nullptr;
    object = GCSingleton::get()->linkedobjects;
    while(object != nullptr)
    {
        if(object->mark == GCSingleton::get()->markvalue)
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
                GCSingleton::get()->linkedobjects = object;
            }
            nn_object_destroy(unreached);
        }
    }
}

void nn_gcmem_destroylinkedobjects(NNState* state)
{
    NNObject* next;
    NNObject* object;
    object = GCSingleton::get()->linkedobjects;
    while(object != nullptr)
    {
        next = object->next;
        nn_object_destroy(object);
        object = next;
    }
    nn_memory_free(GCSingleton::get()->gcstate.graystack);
    GCSingleton::get()->gcstate.graystack = nullptr;
}

void nn_gcmem_collectgarbage()
{
    size_t before;
    (void)before;
    /*
    //  REMOVE THE NEXT LINE TO DISABLE NESTED nn_gcmem_collectgarbage() POSSIBILITY!
    */
    GCSingleton::get()->gcstate.nextgc = GCSingleton::get()->gcstate.bytesallocated;

    nn_gcmem_markroots();
    nn_gcmem_tracerefs();
    nn_valtable_removewhites(&GCSingleton::get()->allocatedstrings);
    nn_valtable_removewhites(&GCSingleton::get()->openedmodules);
    nn_gcmem_sweep();
    GCSingleton::get()->gcstate.nextgc = GCSingleton::get()->gcstate.bytesallocated * NEON_CONFIG_GCHEAPGROWTHFACTOR;
    GCSingleton::get()->markvalue = !GCSingleton::get()->markvalue;
}


void nn_gcmem_markcompilerroots()
{
    /*
    NNAstFuncCompiler* fnc;
    fnc = state->fnc;
    while(fnc != nullptr)
    {
        nn_gcmem_markobject((NNObject*)fnc->targetfunc);
        fnc = fnc->enclosing;
    }
    */
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
uint8_t nn_utf8iter_charsize(const char* character)
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

uint32_t nn_utf8iter_converter(const char* character, uint8_t size)
{
    uint8_t i;
    static uint32_t codepoint = 0;
    static const uint8_t g_utf8iter_table_unicode[] = { 0, 0, 0x1F, 0xF, 0x7, 0x3, 0x1 };
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
uint8_t nn_utf8iter_next(utf8iterator_t* iter)
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
    uint8_t i;
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
    buf = (char*)nn_memory_malloc(sizeof(char) * (len+1));
    if(buf == nullptr)
    {
        return nullptr;
    }
    memset(buf, 0, len+1);
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

char* nn_util_filereadfile(NNState* state, const char* filename, size_t* dlen, bool havemaxsz, size_t maxsize)
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

char* nn_util_filegetshandle(char* s, int size, FILE *f, size_t* lendest)
{
    int c;
    char *p;
    p = s;
    (*lendest) = 0;
    if (size > 0)
    {
        while (--size > 0) 
        {
            if ((c = getc(f)) == -1)
            {
                if (ferror(f) == EINTR)
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


int nn_util_filegetlinehandle(char **lineptr, size_t *destlen, FILE* hnd)
{
    enum { kInitialStrBufSize = 256 };
    static char stackbuf[kInitialStrBufSize];
    char *heapbuf;
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
    if (feof(hnd))
    {
        return -1;     
    }
    nn_util_filegetshandle(stackbuf,kInitialStrBufSize,hnd, &getlen);
    heapbuf = strchr(stackbuf,'\n');   
    if(heapbuf)
    {
        *heapbuf = '\0';
    }
    linelen = getlen;
    if((linelen+1) < kInitialStrBufSize)
    {
        heapbuf = (char*)nn_memory_realloc(*lineptr, kInitialStrBufSize);
        if(heapbuf == nullptr)
        {
            return -1;
        }
        *lineptr = heapbuf;
        *destlen = kInitialStrBufSize;
    }
    strcpy(*lineptr,stackbuf);
    *destlen = linelen;
    return linelen;
}

char* nn_util_strtoupper(char* str, size_t length)
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

char* nn_util_strtolower(char* str, size_t length)
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


NNObjString* nn_util_numbertobinstring(NNState* state, long n)
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

NNObjString* nn_util_numbertooctstring(NNState* state, int64_t n, bool numeric)
{
    int length;
    /* assume maximum of 64 bits + 2 octal indicators (0c) */
    char str[66];
    length = sprintf(str, numeric ? "0c%lo" : "%lo", n);
    return nn_string_copylen(str, length);
}

NNObjString* nn_util_numbertohexstring(NNState* state, int64_t n, bool numeric)
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
        case NEON_OBJTYPE_CLASS:
            {
                /* Classes just use their name. */
                return ((NNObjClass*)object)->name->hashvalue;
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
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
        case NEON_OBJTYPE_STRING:
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
    if(nn_value_isbool(value))
    {
        return nn_value_asbool(value) ? 3 : 5;
    }
    else if(value.isNull())
    {
        return 7;
    }
    else if(nn_value_isnumber(value))
    {
        return nn_util_hashdouble(nn_value_asnumber(value));
    }
    else if(nn_value_isobject(value))
    {
        return nn_object_hashobject(nn_value_asobject(value));
    }
    return 0;
}
 


void nn_strformat_init(NNState* state, NNFormatInfo* nfi, NNIOStream* writer, const char* fmtstr, size_t fmtlen)
{
    nfi->pstate = state;
    nfi->fmtstr = fmtstr;
    nfi->fmtlen = fmtlen;
    nfi->writer = writer;
}

void nn_strformat_destroy(NNFormatInfo* nfi)
{
    (void)nfi;
}

bool nn_strformat_format(NNFormatInfo* nfi, int argc, int argbegin, NNValue* argv)
{
    int ch;
    int ival;
    int nextch;
    bool ok;
    size_t i;
    size_t argpos;
    NNValue cval;
    i = 0;
    argpos = argbegin;
    ok = true;
    while(i < nfi->fmtlen)
    {
        ch = nfi->fmtstr[i];
        nextch = -1;
        if((i + 1) < nfi->fmtlen)
        {
            nextch = nfi->fmtstr[i+1];
        }
        i++;
        if(ch == '%')
        {
            if(nextch == '%')
            {
                nn_iostream_writechar(nfi->writer, '%');
            }
            else
            {
                i++;
                if((int)argpos > argc)
                {
                    nn_except_throwclass(nfi->pstate, nfi->pstate->exceptions.argumenterror, "too few arguments");
                    ok = false;
                    cval = nn_value_makenull();
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
                            ival = (int)nn_value_asnumber(cval);
                            nn_iostream_printf(nfi->writer, "%c", ival);
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
                            nn_except_throwclass(nfi->pstate, nfi->pstate->exceptions.argumenterror, "unknown/invalid format flag '%%c'", nextch);
                        }
                        break;
                }
            }
        }
        else
        {
            nn_iostream_writechar(nfi->writer, ch);
        }
    }
    return ok;
}

void nn_valtable_removewhites(NNHashTable<NNValue, NNValue>* table)
{
    int i;
    for(i = 0; i < table->htcapacity; i++)
    {
        auto entry = &table->htentries[i];
        if(nn_value_isobject(entry->key) && nn_value_asobject(entry->key)->mark != GCSingleton::get()->markvalue)
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
        //nn_state_warn(state, "trying to mark inactive hashtable <%p>!", table);
        return;
    }
    for(i = 0; i < table->htcapacity; i++)
    {
        auto entry = &table->htentries[i];
        if(entry != nullptr)
        {
            if(!entry->key.isNull())
            {
                nn_gcmem_markvalue((NNValue)entry->key);
                nn_gcmem_markvalue((NNValue)entry->value.value);
            }
        }
    }
}

/*
* TODO: get rid of unused functions
*/

#define NEON_CONF_LIBSTRINGUSEHASH 0

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
    return nn_value_makenumber(nn_string_tabhashvaluecombine(data, len, hsv));
}

static NNValue nn_string_tabhashvalueobj(NNObjString* os)
{
    return nn_string_tabhashvaluestr(nn_string_getdata(os), nn_string_getlength(os), os->hashvalue);
}

void nn_string_strtabstore(NNObjString* os)
{
    #if defined(NEON_CONF_LIBSTRINGUSEHASH) && (NEON_CONF_LIBSTRINGUSEHASH == 1)
        GCSingleton::get()->allocatedstrings.set(nn_string_tabhashvalueobj(os), nn_value_fromobject(os));    
    #else
        GCSingleton::get()->allocatedstrings.set(nn_value_fromobject(os), nn_value_makenull());
    #endif
}

NNObjString* nn_string_strtabfind(const char* str, size_t len, uint32_t hsv)
{
    NNObjString* rs;
    rs = GCSingleton::get()->allocatedstrings.findstring(str, len, hsv);
    return rs;
}

NNObjString* nn_string_makefromstrbuf(NNStringBuffer buf, uint32_t hsv, size_t length)
{
    NNObjString* rs;
    rs = (NNObjString*)nn_object_allocobject(sizeof(NNObjString), NEON_OBJTYPE_STRING, false);
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
    nn_gcmem_release(str, sizeof(NNObjString));
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

int nn_string_appendfmtv(NNObjString* os, const char* fmt, va_list va)
{
    return nn_strbuf_appendformatv(&os->sbuf, fmt, va);
}

int nn_string_appendfmt(NNObjString* os, const char* fmt, ...)
{
    int r;
    va_list va;
    va_start(va, fmt);
    r = nn_string_appendfmtv(os, fmt, va);
    va_end(va);
    return r;
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

void nn_iostream_initvars(NNIOStream* pr, NNPrMode mode)
{
    pr->fromstack = false;
    pr->wrmode = NEON_PRMODE_UNDEFINED;
    pr->shouldclose = false;
    pr->shouldflush = false;
    pr->stringtaken = false;
    pr->shortenvalues = false;
    pr->jsonmode = false;
    pr->maxvallength = 15;
    pr->handle = nullptr;
    pr->wrmode = mode;
}


bool nn_iostream_makestackio(NNIOStream* pr, FILE* fh, bool shouldclose)
{
    nn_iostream_initvars(pr, NEON_PRMODE_FILE);
    pr->fromstack = true;
    pr->handle = fh;
    pr->shouldclose = shouldclose;
    return true;
}

bool nn_iostream_makestackopenfile(NNIOStream* pr, const char* path, bool writemode)
{
    const char* mode;
    nn_iostream_initvars(pr, NEON_PRMODE_FILE);
    mode = "rb";
    if(writemode)
    {
        mode = "wb";
    }
    pr->fromstack = true;
    pr->shouldclose = true;
    pr->handle = fopen(path, mode);
    if(pr->handle == nullptr)
    {
        return false;
    }
    return true;
}


bool nn_iostream_makestackstring(NNIOStream* pr)
{
    nn_iostream_initvars(pr, NEON_PRMODE_STRING);
    pr->fromstack = true;
    pr->wrmode = NEON_PRMODE_STRING;
    nn_strbuf_makebasicemptystack(&pr->strbuf, nullptr, 0);
    return true;
}


NNIOStream* nn_iostream_makeundefined(NNPrMode mode)
{
    NNIOStream* pr;
    pr = (NNIOStream*)nn_memory_malloc(sizeof(NNIOStream));
    if(!pr)
    {
        fprintf(stderr, "cannot allocate NNIOStream\n");
        return nullptr;
    }
    nn_iostream_initvars(pr, mode);
    return pr;
}

NNIOStream* nn_iostream_makeio(FILE* fh, bool shouldclose)
{
    NNIOStream* pr;
    pr = nn_iostream_makeundefined(NEON_PRMODE_FILE);
    pr->handle = fh;
    pr->shouldclose = shouldclose;
    return pr;
}

NNIOStream* nn_iostream_makeopenfile(const char* path, bool writemode)
{
    NNIOStream* pr;
    pr = nn_iostream_makeundefined(NEON_PRMODE_FILE);
    if(nn_iostream_makestackopenfile(pr, path, writemode))
    {
        pr->fromstack = false;
        return pr;
    }
    else
    {
        nn_iostream_destroy(pr);
    }
    return nullptr;
}

NNIOStream* nn_iostream_makestring()
{
    NNIOStream* pr;
    pr = nn_iostream_makeundefined(NEON_PRMODE_STRING);
    nn_strbuf_makebasicemptystack(&pr->strbuf, nullptr, 0);
    return pr;
}

void nn_iostream_destroy(NNIOStream* pr)
{
    if(pr == nullptr)
    {
        return;
    }
    if(pr->wrmode == NEON_PRMODE_UNDEFINED)
    {
        return;
    }
    /*fprintf(stderr, "nn_iostream_destroy: pr->wrmode=%d\n", pr->wrmode);*/
    if(pr->wrmode == NEON_PRMODE_STRING)
    {
        if(!pr->stringtaken)
        {
            nn_strbuf_destroyfromstack(&pr->strbuf);
        }
    }
    else if(pr->wrmode == NEON_PRMODE_FILE)
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

NNObjString* nn_iostream_takestring(NNIOStream* pr)
{
    size_t xlen;
    NNObjString* os;
    xlen = nn_strbuf_length(&pr->strbuf);
    os = nn_string_makefromstrbuf(pr->strbuf, nn_util_hashstring(nn_strbuf_data(&pr->strbuf), xlen), xlen);
    pr->stringtaken = true;
    return os;
}

NNObjString* nn_iostream_copystring(NNIOStream* pr)
{
    NNObjString* os;
    os = nn_string_copylen(nn_strbuf_data(&pr->strbuf), nn_strbuf_length(&pr->strbuf));
    return os;
}

void nn_iostream_flush(NNIOStream* pr)
{
    //if(pr->shouldflush)
    {
        fflush(pr->handle);
    }
}

bool nn_iostream_writestringl(NNIOStream* pr, const char* estr, size_t elen)
{
    //fprintf(stderr, "writestringl: (%d) <<<%.*s>>>\n", elen, elen, estr);
    size_t chlen;
    chlen = sizeof(char);
    if(elen > 0)
    {
        if(pr->wrmode == NEON_PRMODE_FILE)
        {
            fwrite(estr, chlen, elen, pr->handle);
            nn_iostream_flush(pr);
        }
        else if(pr->wrmode == NEON_PRMODE_STRING)
        {
            nn_strbuf_appendstrn(&pr->strbuf, estr, elen);
        }
        else
        {
            return false;
        }
    }
    return true;
}

bool nn_iostream_writestring(NNIOStream* pr, const char* estr)
{
    return nn_iostream_writestringl(pr, estr, strlen(estr));
}

bool nn_iostream_writechar(NNIOStream* pr, int b)
{
    char ch;
    if(pr->wrmode == NEON_PRMODE_STRING)
    {
        ch = b;
        nn_iostream_writestringl(pr, &ch, 1);
    }
    else if(pr->wrmode == NEON_PRMODE_FILE)
    {
        fputc(b, pr->handle);
        nn_iostream_flush(pr);
    }
    return true;
}

bool nn_iostream_writeescapedchar(NNIOStream* pr, int ch)
{
    switch(ch)
    {
        case '\'':
            {
                nn_iostream_writestring(pr, "\\\'");
            }
            break;
        case '\"':
            {
                nn_iostream_writestring(pr, "\\\"");
            }
            break;
        case '\\':
            {
                nn_iostream_writestring(pr, "\\\\");
            }
            break;
        case '\b':
            {
                nn_iostream_writestring(pr, "\\b");
            }
            break;
        case '\f':
            {
                nn_iostream_writestring(pr, "\\f");
            }
            break;
        case '\n':
            {
                nn_iostream_writestring(pr, "\\n");
            }
            break;
        case '\r':
            {
                nn_iostream_writestring(pr, "\\r");
            }
            break;
        case '\t':
            {
                nn_iostream_writestring(pr, "\\t");
            }
            break;
        case 0:
            {
                nn_iostream_writestring(pr, "\\0");
            }
            break;
        default:
            {
                nn_iostream_printf(pr, "\\x%02x", (unsigned char)ch);
            }
            break;
    }
    return true;
}

bool nn_iostream_writequotedstring(NNIOStream* pr, const char* str, size_t len, bool withquot)
{
    int bch;
    size_t i;
    bch = 0;
    if(withquot)
    {
        nn_iostream_writechar(pr, '"');
    }
    for(i = 0; i < len; i++)
    {
        bch = str[i];
        if((bch < 32) || (bch > 127) || (bch == '\"') || (bch == '\\'))
        {
            nn_iostream_writeescapedchar(pr, bch);
        }
        else
        {
            nn_iostream_writechar(pr, bch);
        }
    }
    if(withquot)
    {
        nn_iostream_writechar(pr, '"');
    }
    return true;
}

bool nn_iostream_vwritefmttostring(NNIOStream* pr, const char* fmt, va_list va)
{
    nn_strbuf_appendformatv(&pr->strbuf, fmt, va);
    return true;
}

bool nn_iostream_vwritefmt(NNIOStream* pr, const char* fmt, va_list va)
{
    if(pr->wrmode == NEON_PRMODE_STRING)
    {
        return nn_iostream_vwritefmttostring(pr, fmt, va);
    }
    else if(pr->wrmode == NEON_PRMODE_FILE)
    {
        vfprintf(pr->handle, fmt, va);
        nn_iostream_flush(pr);
    }
    return true;
}

bool nn_iostream_printf(NNIOStream* pr, const char* fmt, ...) NEON_ATTR_PRINTFLIKE(2, 3);

bool nn_iostream_printf(NNIOStream* pr, const char* fmt, ...)
{
    bool b;
    va_list va;
    va_start(va, fmt);
    b = nn_iostream_vwritefmt(pr, fmt, va);
    va_end(va);
    return b;
}


void nn_valtable_print(NNIOStream* pr, NNHashTable<NNValue, NNValue>* table, const char* name)
{
    size_t i;
    size_t hcap;
    hcap = table->capacity();
    nn_iostream_printf(pr, "<HashTable of %s : {\n", name);
    for(i = 0; i < hcap; i++)
    {
        auto entry = table->entryatindex(i);
        if(!entry->key.isNull())
        {
            nn_iostream_printvalue(pr, (NNValue)entry->key, true, true);
            nn_iostream_printf(pr, ": ");
            nn_iostream_printvalue(pr, (NNValue)entry->value.value, true, true);
            if(i != (hcap - 1))
            {
                nn_iostream_printf(pr, ",\n");
            }
        }
    }
    nn_iostream_printf(pr, "}>\n");
}

void nn_iostream_printfunction(NNIOStream* pr, NNObjFunction* func)
{
    if(func->name == nullptr)
    {
        nn_iostream_printf(pr, "<script at %p>", (void*)func);
    }
    else
    {
        if(func->fnscriptfunc.isvariadic)
        {
            nn_iostream_printf(pr, "<function %s(%d...) at %p>", nn_string_getdata(func->name), func->fnscriptfunc.arity, (void*)func);
        }
        else
        {
            nn_iostream_printf(pr, "<function %s(%d) at %p>", nn_string_getdata(func->name), func->fnscriptfunc.arity, (void*)func);
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
    nn_iostream_printf(pr, "[");
    for(i = 0; i < vsz; i++)
    {
        isrecur = false;
        val = list->varray.get(i);
        if(nn_value_isarray(val))
        {
            subarr = nn_value_asarray(val);
            if(subarr == list)
            {
                isrecur = true;
            }
        }
        if(isrecur)
        {
            nn_iostream_printf(pr, "<recursion>");
        }
        else
        {
            nn_iostream_printvalue(pr, val, true, true);
        }
        if(i != vsz - 1)
        {
            nn_iostream_printf(pr, ",");
        }
        if(pr->shortenvalues && (i >= pr->maxvallength))
        {
            nn_iostream_printf(pr, " [%ld items]", vsz);
            break;
        }
    }
    nn_iostream_printf(pr, "]");
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
    nn_iostream_printf(pr, "{");
    for(i = 0; i < dsz; i++)
    {
        valisrecur = false;
        keyisrecur = false;
        val = dict->htnames.get(i);
        if(nn_value_isdict(val))
        {
            subdict = nn_value_asdict(val);
            if(subdict == dict)
            {
                valisrecur = true;
            }
        }
        if(valisrecur)
        {
            nn_iostream_printf(pr, "<recursion>");
        }
        else
        {
            nn_iostream_printvalue(pr, val, true, true);
        }
        nn_iostream_printf(pr, ": ");
        auto field = dict->htab.getfield(dict->htnames.get(i));
        if(field != nullptr)
        {
            if(nn_value_isdict((NNValue)field->value))
            {
                subdict = nn_value_asdict((NNValue)field->value);
                if(subdict == dict)
                {
                    keyisrecur = true;
                }
            }
            if(keyisrecur)
            {
                nn_iostream_printf(pr, "<recursion>");
            }
            else
            {
                nn_iostream_printvalue(pr, (NNValue)field->value, true, true);
            }
        }
        if(i != dsz - 1)
        {
            nn_iostream_printf(pr, ", ");
        }
        if(pr->shortenvalues && (pr->maxvallength >= i))
        {
            nn_iostream_printf(pr, " [%ld items]", dsz);
            break;
        }
    }
    nn_iostream_printf(pr, "}");
}

void nn_iostream_printfile(NNIOStream* pr, NNObjFile* file)
{
    nn_iostream_printf(pr, "<file at %s in mode %s>", nn_string_getdata(file->path), nn_string_getdata(file->mode));
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
    NNState* state;
    NNObjString* os;
    NNObjArray* args;
    state = pr->pstate;
    if(invmethod)
    {
        field = instance->klass->instmethods.getfieldbycstr("toString");
        if(field != nullptr)
        {
            args = nn_object_makearray();
            thisval = nn_value_fromobject(instance);
            arity = nn_nestcall_prepare((NNValue)field->value, thisval, nullptr, 0);
            fprintf(stderr, "arity = %d\n", arity);
            nn_vm_stackpop(state);
            nn_vm_stackpush(state, thisval);
            if(nn_nestcall_callfunction(state, (NNValue)field->value, thisval, nullptr, 0, &resv, false))
            {
                nn_iostream_makestackstring(&subw);
                nn_iostream_printvalue(&subw, resv, false, false);
                os = nn_iostream_takestring(&subw);
                nn_iostream_writestringl(pr, nn_string_getdata(os), nn_string_getlength(os));
                #if 0
                    nn_vm_stackpop(state);
                #endif
                return;
            }
        }
    }
    */
    #endif
    nn_iostream_printf(pr, "<instance of %s at %p>", nn_string_getdata(instance->klass->name), (void*)instance);
}

void nn_iostream_printtable(NNIOStream* pr, NNHashTable<NNValue, NNValue>* table)
{
    size_t i;
    size_t hcap;
    hcap = table->capacity();
    nn_iostream_printf(pr, "{");
    for(i = 0; i < (size_t)hcap; i++)
    {
        auto entry = table->entryatindex(i);
        if(entry->key.isNull())
        {
            continue;
        }
        nn_iostream_printvalue(pr, (NNValue)entry->key, true, false);
        nn_iostream_printf(pr, ":");
        nn_iostream_printvalue(pr, (NNValue)entry->value.value, true, false);
        auto nextentry = table->entryatindex(i+1);
        if((nextentry != nullptr) && !nextentry->key.isNull())
        {
            if((i+1) < (size_t)hcap)
            {
                nn_iostream_printf(pr, ",");
            }
        }
    }
    nn_iostream_printf(pr, "}");
}

void nn_iostream_printobjclass(NNIOStream* pr, NNValue value, bool fixstring, bool invmethod)
{
    bool oldexp;
    NNObjClass* klass;
    (void)fixstring;
    (void)invmethod;
    klass = nn_value_asclass(value);
    if(pr->jsonmode)
    {
        nn_iostream_printf(pr, "{");
        {
            {
                nn_iostream_printf(pr, "name: ");
                nn_iostream_printvalue(pr, nn_value_fromobject(klass->name), true, false);
                nn_iostream_printf(pr, ",");
            }
            {
                nn_iostream_printf(pr, "superclass: ");
                oldexp = pr->jsonmode;
                pr->jsonmode = false;
                nn_iostream_printvalue(pr, nn_value_fromobject(klass->superclass), true, false);
                pr->jsonmode = oldexp;
                nn_iostream_printf(pr, ",");
            }
            {
                nn_iostream_printf(pr, "constructor: ");
                nn_iostream_printvalue(pr, (NNValue)klass->constructor, true, false);
                nn_iostream_printf(pr, ",");
            }
            {
                nn_iostream_printf(pr, "instanceproperties:");
                nn_iostream_printtable(pr, &klass->instproperties);
                nn_iostream_printf(pr, ",");
            }
            {
                nn_iostream_printf(pr, "staticproperties:");
                nn_iostream_printtable(pr, &klass->staticproperties);
                nn_iostream_printf(pr, ",");
            }
            {
                nn_iostream_printf(pr, "instancemethods:");
                nn_iostream_printtable(pr, &klass->instmethods);
                nn_iostream_printf(pr, ",");
            }
            {
                nn_iostream_printf(pr, "staticmethods:");
                nn_iostream_printtable(pr, &klass->staticmethods);
            }
        }
        nn_iostream_printf(pr, "}");
    }
    else
    {
        nn_iostream_printf(pr, "<class %s at %p>", nn_string_getdata(klass->name), (void*)klass);
    }
}

void nn_iostream_printobject(NNIOStream* pr, NNValue value, bool fixstring, bool invmethod)
{
    NNObject* obj;
    obj = nn_value_asobject(value);
    switch(obj->type)
    {
        case NEON_OBJTYPE_SWITCH:
            {
                nn_iostream_writestring(pr, "<switch>");
            }
            break;
        case NEON_OBJTYPE_USERDATA:
            {
                nn_iostream_printf(pr, "<userdata %s>", nn_value_asuserdata(value)->name);
            }
            break;
        case NEON_OBJTYPE_RANGE:
            {
                NNObjRange* range;
                range = nn_value_asrange(value);
                nn_iostream_printf(pr, "<range %d .. %d>", range->lower, range->upper);
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                nn_iostream_printfile(pr, nn_value_asfile(value));
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                nn_iostream_printdict(pr, nn_value_asdict(value));
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                nn_iostream_printarray(pr, nn_value_asarray(value));
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
            {
                NNObjFunction* bn;
                bn = nn_value_asfunction(value);
                nn_iostream_printfunction(pr, bn->fnmethod.method->fnclosure.scriptfunc);
            }
            break;
        case NEON_OBJTYPE_MODULE:
            {
                NNObjModule* mod;
                mod = nn_value_asmodule(value);
                nn_iostream_printf(pr, "<module '%s' at '%s'>", nn_string_getdata(mod->name), nn_string_getdata(mod->physicalpath));
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                nn_iostream_printobjclass(pr, value, fixstring, invmethod);
            }
            break;
        case NEON_OBJTYPE_FUNCCLOSURE:
            {
                NNObjFunction* cls;
                cls = nn_value_asfunction(value);
                nn_iostream_printfunction(pr, cls->fnclosure.scriptfunc);
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                NNObjFunction* fn;
                fn = nn_value_asfunction(value);
                nn_iostream_printfunction(pr, fn);
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                /* @TODO: support the toString() override */
                NNObjInstance* instance;
                instance = nn_value_asinstance(value);
                nn_iostream_printinstance(pr, instance, invmethod);
            }
            break;
        case NEON_OBJTYPE_FUNCNATIVE:
            {
                NNObjFunction* native;
                native = nn_value_asfunction(value);
                nn_iostream_printf(pr, "<function %s(native) at %p>", nn_string_getdata(native->name), (void*)native);
            }
            break;
        case NEON_OBJTYPE_UPVALUE:
            {
                nn_iostream_printf(pr, "<upvalue>");
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                NNObjString* string;
                string = nn_value_asstring(value);
                if(fixstring)
                {
                    nn_iostream_writequotedstring(pr, nn_string_getdata(string), nn_string_getlength(string), true);
                }
                else
                {
                    nn_iostream_writestringl(pr, nn_string_getdata(string), nn_string_getlength(string));
                }
            }
            break;
    }
}

void nn_iostream_printnumber(NNIOStream* pr, NNValue value)
{
    double dn;
    dn = nn_value_asnumber(value);
    nn_iostream_printf(pr, "%.16g", dn);
}

void nn_iostream_printvalue(NNIOStream* pr, NNValue value, bool fixstring, bool invmethod)
{
    if(value.isNull())
    {
        nn_iostream_writestring(pr, "null");
    }
    else if(nn_value_isbool(value))
    {
        nn_iostream_writestring(pr, nn_value_asbool(value) ? "true" : "false");
    }
    else if(nn_value_isnumber(value))
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
    nn_iostream_printf(pr, "== compiled '%s' [[\n", name);
    for(offset = 0; offset < blob->count;)
    {
        offset = nn_dbg_printinstructionat(pr, blob, offset);
    }
    nn_iostream_printf(pr, "]]\n");
}

void nn_dbg_printinstrname(NNIOStream* pr, const char* name)
{
    nn_iostream_printf(pr, "%s%-16s%s ", nn_util_color(NEON_COLOR_RED), name, nn_util_color(NEON_COLOR_RESET));
}

int nn_dbg_printsimpleinstr(NNIOStream* pr, const char* name, int offset)
{
    nn_dbg_printinstrname(pr, name);
    nn_iostream_printf(pr, "\n");
    return offset + 1;
}

int nn_dbg_printconstinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset)
{
    uint16_t constant;
    constant = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    nn_iostream_printf(pr, "%8d ", constant);
    nn_iostream_printvalue(pr, blob->constants.get(constant), true, false);
    nn_iostream_printf(pr, "\n");
    return offset + 3;
}

int nn_dbg_printpropertyinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset)
{
    const char* proptn;
    uint16_t constant;
    constant = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    nn_iostream_printf(pr, "%8d ", constant);
    nn_iostream_printvalue(pr, blob->constants.get(constant), true, false);
    proptn = "";
    if(blob->instrucs[offset + 3].code == 1)
    {
        proptn = "static";
    }
    nn_iostream_printf(pr, " (%s)", proptn);
    nn_iostream_printf(pr, "\n");
    return offset + 4;
}

int nn_dbg_printshortinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset)
{
    uint16_t slot;
    slot = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    nn_iostream_printf(pr, "%8d\n", slot);
    return offset + 3;
}

int nn_dbg_printbyteinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset)
{
    uint8_t slot;
    slot = blob->instrucs[offset + 1].code;
    nn_dbg_printinstrname(pr, name);
    nn_iostream_printf(pr, "%8d\n", slot);
    return offset + 2;
}

int nn_dbg_printjumpinstr(NNIOStream* pr, const char* name, int sign, NNBlob* blob, int offset)
{
    uint16_t jump;
    jump = (uint16_t)(blob->instrucs[offset + 1].code << 8);
    jump |= blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    nn_iostream_printf(pr, "%8d -> %d\n", offset, offset + 3 + sign * jump);
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
    nn_iostream_printf(pr, "%8d -> %d, %d\n", type, address, finally);
    return offset + 7;
}

int nn_dbg_printinvokeinstr(NNIOStream* pr, const char* name, NNBlob* blob, int offset)
{
    uint16_t constant;
    uint8_t argcount;
    constant = (uint16_t)(blob->instrucs[offset + 1].code << 8);
    constant |= blob->instrucs[offset + 2].code;
    argcount = blob->instrucs[offset + 3].code;
    nn_dbg_printinstrname(pr, name);
    nn_iostream_printf(pr, "(%d args) %8d ", argcount, constant);
    nn_iostream_printvalue(pr, blob->constants.get(constant), true, false);
    nn_iostream_printf(pr, "\n");
    return offset + 4;
}

const char* nn_dbg_op2str(uint8_t instruc)
{
    switch(instruc)
    {
        case NEON_OP_GLOBALDEFINE: return "NEON_OP_GLOBALDEFINE";
        case NEON_OP_GLOBALGET: return "NEON_OP_GLOBALGET";
        case NEON_OP_GLOBALSET: return "NEON_OP_GLOBALSET";
        case NEON_OP_LOCALGET: return "NEON_OP_LOCALGET";
        case NEON_OP_LOCALSET: return "NEON_OP_LOCALSET";
        case NEON_OP_FUNCARGOPTIONAL: return "NEON_OP_FUNCARGOPTIONAL";
        case NEON_OP_FUNCARGSET: return "NEON_OP_FUNCARGSET";
        case NEON_OP_FUNCARGGET: return "NEON_OP_FUNCARGGET";
        case NEON_OP_UPVALUEGET: return "NEON_OP_UPVALUEGET";
        case NEON_OP_UPVALUESET: return "NEON_OP_UPVALUESET";
        case NEON_OP_UPVALUECLOSE: return "NEON_OP_UPVALUECLOSE";
        case NEON_OP_PROPERTYGET: return "NEON_OP_PROPERTYGET";
        case NEON_OP_PROPERTYGETSELF: return "NEON_OP_PROPERTYGETSELF";
        case NEON_OP_PROPERTYSET: return "NEON_OP_PROPERTYSET";
        case NEON_OP_JUMPIFFALSE: return "NEON_OP_JUMPIFFALSE";
        case NEON_OP_JUMPNOW: return "NEON_OP_JUMPNOW";
        case NEON_OP_LOOP: return "NEON_OP_LOOP";
        case NEON_OP_EQUAL: return "NEON_OP_EQUAL";
        case NEON_OP_PRIMGREATER: return "NEON_OP_PRIMGREATER";
        case NEON_OP_PRIMLESSTHAN: return "NEON_OP_PRIMLESSTHAN";
        case NEON_OP_PUSHEMPTY: return "NEON_OP_PUSHEMPTY";
        case NEON_OP_PUSHNULL: return "NEON_OP_PUSHNULL";
        case NEON_OP_PUSHTRUE: return "NEON_OP_PUSHTRUE";
        case NEON_OP_PUSHFALSE: return "NEON_OP_PUSHFALSE";
        case NEON_OP_PRIMADD: return "NEON_OP_PRIMADD";
        case NEON_OP_PRIMSUBTRACT: return "NEON_OP_PRIMSUBTRACT";
        case NEON_OP_PRIMMULTIPLY: return "NEON_OP_PRIMMULTIPLY";
        case NEON_OP_PRIMDIVIDE: return "NEON_OP_PRIMDIVIDE";
        case NEON_OP_PRIMFLOORDIVIDE: return "NEON_OP_PRIMFLOORDIVIDE";
        case NEON_OP_PRIMMODULO: return "NEON_OP_PRIMMODULO";
        case NEON_OP_PRIMPOW: return "NEON_OP_PRIMPOW";
        case NEON_OP_PRIMNEGATE: return "NEON_OP_PRIMNEGATE";
        case NEON_OP_PRIMNOT: return "NEON_OP_PRIMNOT";
        case NEON_OP_PRIMBITNOT: return "NEON_OP_PRIMBITNOT";
        case NEON_OP_PRIMAND: return "NEON_OP_PRIMAND";
        case NEON_OP_PRIMOR: return "NEON_OP_PRIMOR";
        case NEON_OP_PRIMBITXOR: return "NEON_OP_PRIMBITXOR";
        case NEON_OP_PRIMSHIFTLEFT: return "NEON_OP_PRIMSHIFTLEFT";
        case NEON_OP_PRIMSHIFTRIGHT: return "NEON_OP_PRIMSHIFTRIGHT";
        case NEON_OP_PUSHONE: return "NEON_OP_PUSHONE";
        case NEON_OP_PUSHCONSTANT: return "NEON_OP_PUSHCONSTANT";
        case NEON_OP_ECHO: return "NEON_OP_ECHO";
        case NEON_OP_POPONE: return "NEON_OP_POPONE";
        case NEON_OP_DUPONE: return "NEON_OP_DUPONE";
        case NEON_OP_POPN: return "NEON_OP_POPN";
        case NEON_OP_ASSERT: return "NEON_OP_ASSERT";
        case NEON_OP_EXTHROW: return "NEON_OP_EXTHROW";
        case NEON_OP_MAKECLOSURE: return "NEON_OP_MAKECLOSURE";
        case NEON_OP_CALLFUNCTION: return "NEON_OP_CALLFUNCTION";
        case NEON_OP_CALLMETHOD: return "NEON_OP_CALLMETHOD";
        case NEON_OP_CLASSINVOKETHIS: return "NEON_OP_CLASSINVOKETHIS";
        case NEON_OP_CLASSGETTHIS: return "NEON_OP_CLASSGETTHIS";
        case NEON_OP_RETURN: return "NEON_OP_RETURN";
        case NEON_OP_MAKECLASS: return "NEON_OP_MAKECLASS";
        case NEON_OP_MAKEMETHOD: return "NEON_OP_MAKEMETHOD";
        case NEON_OP_CLASSPROPERTYDEFINE: return "NEON_OP_CLASSPROPERTYDEFINE";
        case NEON_OP_CLASSINHERIT: return "NEON_OP_CLASSINHERIT";
        case NEON_OP_CLASSGETSUPER: return "NEON_OP_CLASSGETSUPER";
        case NEON_OP_CLASSINVOKESUPER: return "NEON_OP_CLASSINVOKESUPER";
        case NEON_OP_CLASSINVOKESUPERSELF: return "NEON_OP_CLASSINVOKESUPERSELF";
        case NEON_OP_MAKERANGE: return "NEON_OP_MAKERANGE";
        case NEON_OP_MAKEARRAY: return "NEON_OP_MAKEARRAY";
        case NEON_OP_MAKEDICT: return "NEON_OP_MAKEDICT";
        case NEON_OP_INDEXGET: return "NEON_OP_INDEXGET";
        case NEON_OP_INDEXGETRANGED: return "NEON_OP_INDEXGETRANGED";
        case NEON_OP_INDEXSET: return "NEON_OP_INDEXSET";
        case NEON_OP_IMPORTIMPORT: return "NEON_OP_IMPORTIMPORT";
        case NEON_OP_EXTRY: return "NEON_OP_EXTRY";
        case NEON_OP_EXPOPTRY: return "NEON_OP_EXPOPTRY";
        case NEON_OP_EXPUBLISHTRY: return "NEON_OP_EXPUBLISHTRY";
        case NEON_OP_STRINGIFY: return "NEON_OP_STRINGIFY";
        case NEON_OP_SWITCH: return "NEON_OP_SWITCH";
        case NEON_OP_TYPEOF: return "NEON_OP_TYPEOF";
        case NEON_OP_BREAK_PL: return "NEON_OP_BREAK_PL";
        case NEON_OP_OPINSTANCEOF: return "NEON_OP_OPINSTANCEOF";
        case NEON_OP_HALT: return "NEON_OP_HALT";
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
    nn_iostream_printf(pr, "%-16s %8d ", name, constant);
    nn_iostream_printvalue(pr, blob->constants.get(constant), true, false);
    nn_iostream_printf(pr, "\n");
    function = nn_value_asfunction(blob->constants.get(constant));
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
        nn_iostream_printf(pr, "%04d      |                     %s %d\n", offset - 3, locn, (int)index);
    }
    return offset;
}

int nn_dbg_printinstructionat(NNIOStream* pr, NNBlob* blob, int offset)
{
    uint8_t instruction;
    const char* opname;
    nn_iostream_printf(pr, "%08d ", offset);
    if(offset > 0 && blob->instrucs[offset].fromsourceline == blob->instrucs[offset - 1].fromsourceline)
    {
        nn_iostream_printf(pr, "       | ");
    }
    else
    {
        nn_iostream_printf(pr, "%8d ", blob->instrucs[offset].fromsourceline);
    }
    instruction = blob->instrucs[offset].code;
    opname = nn_dbg_op2str(instruction);
    switch(instruction)
    {
        case NEON_OP_JUMPIFFALSE:
            return nn_dbg_printjumpinstr(pr, opname, 1, blob, offset);
        case NEON_OP_JUMPNOW:
            return nn_dbg_printjumpinstr(pr, opname, 1, blob, offset);
        case NEON_OP_EXTRY:
            return nn_dbg_printtryinstr(pr, opname, blob, offset);
        case NEON_OP_LOOP:
            return nn_dbg_printjumpinstr(pr, opname, -1, blob, offset);
        case NEON_OP_GLOBALDEFINE:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_GLOBALGET:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_GLOBALSET:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_LOCALGET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_LOCALSET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_FUNCARGOPTIONAL:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_FUNCARGGET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_FUNCARGSET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_PROPERTYGET:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_PROPERTYGETSELF:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_PROPERTYSET:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_UPVALUEGET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_UPVALUESET:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_EXPOPTRY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_EXPUBLISHTRY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PUSHCONSTANT:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_EQUAL:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMGREATER:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMLESSTHAN:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PUSHEMPTY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PUSHNULL:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PUSHTRUE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PUSHFALSE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMADD:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMSUBTRACT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMMULTIPLY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMDIVIDE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMFLOORDIVIDE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMMODULO:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMPOW:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMNEGATE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMNOT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMBITNOT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMAND:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMOR:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMBITXOR:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMSHIFTLEFT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PRIMSHIFTRIGHT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_PUSHONE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_IMPORTIMPORT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_TYPEOF:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_ECHO:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_STRINGIFY:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_EXTHROW:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_POPONE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_OPINSTANCEOF:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_UPVALUECLOSE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_DUPONE:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_ASSERT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_POPN:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
            /* non-user objects... */
        case NEON_OP_SWITCH:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
            /* data container manipulators */
        case NEON_OP_MAKERANGE:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_MAKEARRAY:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_MAKEDICT:
            return nn_dbg_printshortinstr(pr, opname, blob, offset);
        case NEON_OP_INDEXGET:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        case NEON_OP_INDEXGETRANGED:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        case NEON_OP_INDEXSET:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_MAKECLOSURE:
            return nn_dbg_printclosureinstr(pr, opname, blob, offset);
        case NEON_OP_CALLFUNCTION:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        case NEON_OP_CALLMETHOD:
            return nn_dbg_printinvokeinstr(pr, opname, blob, offset);
        case NEON_OP_CLASSINVOKETHIS:
            return nn_dbg_printinvokeinstr(pr, opname, blob, offset);
        case NEON_OP_RETURN:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_CLASSGETTHIS:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_MAKECLASS:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_MAKEMETHOD:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_CLASSPROPERTYDEFINE:
            return nn_dbg_printpropertyinstr(pr, opname, blob, offset);
        case NEON_OP_CLASSGETSUPER:
            return nn_dbg_printconstinstr(pr, opname, blob, offset);
        case NEON_OP_CLASSINHERIT:
            return nn_dbg_printsimpleinstr(pr, opname, offset);
        case NEON_OP_CLASSINVOKESUPER:
            return nn_dbg_printinvokeinstr(pr, opname, blob, offset);
        case NEON_OP_CLASSINVOKESUPERSELF:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        case NEON_OP_HALT:
            return nn_dbg_printbyteinstr(pr, opname, blob, offset);
        default:
            {
                nn_iostream_printf(pr, "unknown opcode %d\n", instruction);
            }
            break;
    }
    return offset + 1;
}



NNValue nn_value_copystrlen(const char* str, size_t len)
{
    return nn_value_fromobject(nn_string_copylen(str, len));
}

NNValue nn_value_copystr(const char* str)
{
    return nn_value_copystrlen(str, strlen(str));
}

NNObjString* nn_value_tostring(NNValue value)
{
    NNIOStream pr;
    NNObjString* s;
    nn_iostream_makestackstring(&pr);
    nn_iostream_printvalue(&pr, value, false, true);
    s = nn_iostream_takestring(&pr);
    return s;
}

const char* nn_value_objecttypename(NNObject* object, bool detailed)
{
    static char buf[60];
    if(detailed)
    {
        switch(object->type)
        {
            case NEON_OBJTYPE_FUNCSCRIPT:
                return "funcscript";
            case NEON_OBJTYPE_FUNCNATIVE:
                return "funcnative";
            case NEON_OBJTYPE_FUNCCLOSURE:
                return "funcclosure";
            case NEON_OBJTYPE_FUNCBOUND:
                return "funcbound";
            default:
                break;
        }
    }
    switch(object->type)
    {
        case NEON_OBJTYPE_MODULE:
            return "module";
        case NEON_OBJTYPE_RANGE:
            return "range";
        case NEON_OBJTYPE_FILE:
            return "file";
        case NEON_OBJTYPE_DICT:
            return "dictionary";
        case NEON_OBJTYPE_ARRAY:
            return "array";
        case NEON_OBJTYPE_CLASS:
            return "class";
        case NEON_OBJTYPE_FUNCSCRIPT:
        case NEON_OBJTYPE_FUNCNATIVE:
        case NEON_OBJTYPE_FUNCCLOSURE:
        case NEON_OBJTYPE_FUNCBOUND:
            return "function";
        case NEON_OBJTYPE_INSTANCE:
            {
                const char* klassname;
                NNObjInstance* inst;
                inst = ((NNObjInstance*)object);
                klassname = nn_string_getdata(inst->klass->name);
                sprintf(buf, "instance@%s", klassname);
                return buf;
            }
            break;
        case NEON_OBJTYPE_STRING:
            return "string";
        case NEON_OBJTYPE_USERDATA:
            return "userdata";
        case NEON_OBJTYPE_SWITCH:
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
    else if(nn_value_isbool(value))
    {
        return "boolean";
    }
    else if(nn_value_isnumber(value))
    {
        return "number";
    }
    else if(nn_value_isobject(value))
    {
        return nn_value_objecttypename(nn_value_asobject(value), detailed);
    }
    return "?unknown?";
}

bool nn_value_isfalse(NNValue value)
{
    if(value.isNull())
    {
        return true;
    }
    if(nn_value_isbool(value))
    {
        return !nn_value_asbool(value);
    }
    /* -1 is the number equivalent of false */
    if(nn_value_isnumber(value))
    {
        return nn_value_asnumber(value) < 0;
    }
    /* Non-empty strings are true, empty strings are false.*/
    if(nn_value_isstring(value))
    {
        return nn_string_getlength(nn_value_asstring(value)) < 1;
    }
    /* Non-empty lists are true, empty lists are false.*/
    if(nn_value_isarray(value))
    {
        return nn_value_asarray(value)->varray.count() == 0;
    }
    /* Non-empty dicts are true, empty dicts are false. */
    if(nn_value_isdict(value))
    {
        return nn_value_asdict(value)->htnames.count() == 0;
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
    for(i=0; i<(size_t)arra->varray.count(); i++)
    {
        if(!nn_value_compare((NNValue)arra->varray.get(i), (NNValue)arrb->varray.get(i)))
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
                if(!nn_value_compare((NNValue)fielda->value, (NNValue)fieldb->value))
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
    NNObjType ta;
    NNObjType tb;
    NNObject* oa;
    NNObject* ob;
    oa = nn_value_asobject(a);
    ob = nn_value_asobject(b);
    ta = oa->type;
    tb = ob->type;
    if(ta == tb)
    {
        /* we might not need to do a deep comparison if its the same object */
        if(oa == ob)
        {
            return true;
        }
        else if(ta == NEON_OBJTYPE_STRING)
        {
            return nn_value_compobjstring(oa, ob);
        }
        else if(ta == NEON_OBJTYPE_ARRAY)
        {
            return nn_value_compobjarray(oa, ob);
        }
        else if(ta == NEON_OBJTYPE_DICT)
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
    else if(nn_value_isbool(a))
    {
        return nn_value_asbool(a) == nn_value_asbool(b);
    }
    else if(nn_value_isnumber(a))
    {
        return (nn_value_asnumber(a) == nn_value_asnumber(b));
    }
    else
    {
        if(nn_value_isobject(a) && nn_value_isobject(b))
        {
            if(nn_value_asobject(a) == nn_value_asobject(b))
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
    else if(nn_value_isbool(a))
    {
        if(b.isNull() || (nn_value_isbool(b) && nn_value_asbool(b) == false))
        {
            /* only null, false and false are lower than numbers */
            return a;
        }
        else
        {
            return b;
        }
    }
    else if(nn_value_isnumber(a))
    {
        if(b.isNull() || nn_value_isbool(b))
        {
            return a;
        }
        else if(nn_value_isnumber(b))
        {
            if(nn_value_asnumber(a) >= nn_value_asnumber(b))
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
    else if(nn_value_isobject(a))
    {
        if(nn_value_isstring(a) && nn_value_isstring(b))
        {
            osa = nn_value_asstring(a);
            osb = nn_value_asstring(b);
            adata = nn_string_getdata(osa);
            bdata = nn_string_getdata(osb);
            alen = nn_string_getlength(osa);
            if(strncmp(adata, bdata, alen) >= 0)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isfuncscript(a) && nn_value_isfuncscript(b))
        {
            if(nn_value_asfunction(a)->fnscriptfunc.arity >= nn_value_asfunction(b)->fnscriptfunc.arity)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isfuncclosure(a) && nn_value_isfuncclosure(b))
        {
            if(nn_value_asfunction(a)->fnclosure.scriptfunc->fnscriptfunc.arity >= nn_value_asfunction(b)->fnclosure.scriptfunc->fnscriptfunc.arity)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isrange(a) && nn_value_isrange(b))
        {
            if(nn_value_asrange(a)->lower >= nn_value_asrange(b)->lower)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isclass(a) && nn_value_isclass(b))
        {
            if(nn_value_asclass(a)->instmethods.count() >= nn_value_asclass(b)->instmethods.count())
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isarray(a) && nn_value_isarray(b))
        {
            if(nn_value_asarray(a)->varray.count() >= nn_value_asarray(b)->varray.count())
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isdict(a) && nn_value_isdict(b))
        {
            if(nn_value_asdict(a)->htnames.count() >= nn_value_asdict(b)->htnames.count())
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isfile(a) && nn_value_isfile(b))
        {
            if(strcmp(nn_string_getdata(nn_value_asfile(a)->path), nn_string_getdata(nn_value_asfile(b)->path)) >= 0)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isobject(b))
        {
            if(nn_value_asobject(a)->type >= nn_value_asobject(b)->type)
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
void nn_value_sortvalues(NNState* state, NNValue* values, int count)
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
                if(nn_value_isarray(values[i]))
                {
                    nn_value_sortvalues(state, (NNValue*)nn_value_asarray(values[i])->varray.listitems, nn_value_asarray(values[i])->varray.count());
                }

                if(nn_value_isarray(values[j]))
                {
                    nn_value_sortvalues(state, (NNValue*)nn_value_asarray(values[j])->varray.listitems, nn_value_asarray(values[j])->varray.count());
                }
            }
        }
    }
}

NNValue nn_value_copyvalue(NNValue value)
{
    if(nn_value_isobject(value))
    {
        switch(nn_value_asobject(value)->type)
        {
            case NEON_OBJTYPE_STRING:
                {
                    NNObjString* string;
                    string = nn_value_asstring(value);
                    return nn_value_fromobject(nn_string_copyobject(string));
                }
                break;
            case NEON_OBJTYPE_ARRAY:
                {
                    size_t i;
                    NNObjArray* list;
                    NNObjArray* newlist;
                    list = nn_value_asarray(value);
                    newlist = nn_object_makearray();
                    for(i = 0; i < list->varray.count(); i++)
                    {
                        newlist->varray.push(list->varray.get(i));
                    }
                    return nn_value_fromobject(newlist);
                }
                break;
            case NEON_OBJTYPE_DICT:
                {
                    NNObjDict *dict;
                    dict = nn_value_asdict(value);
                    return nn_value_fromobject(nn_dict_copy(dict));
                    
                }
                break;
            default:
                break;
        }
    }
    return value;
}





NNObject* nn_object_allocobject(size_t size, NNObjType type, bool retain)
{
    NNObject* object;
    object = (NNObject*)nn_gcmem_allocate(size, 1, retain);
    object->type = type;
    object->mark = !GCSingleton::get()->markvalue;
    object->stale = false;
    object->next = GCSingleton::get()->linkedobjects;
    GCSingleton::get()->linkedobjects = object;
    return object;
}

NNObjUpvalue* nn_object_makeupvalue(NNValue* slot, int stackpos)
{
    NNObjUpvalue* upvalue;
    upvalue = (NNObjUpvalue*)nn_object_allocobject(sizeof(NNObjUpvalue), NEON_OBJTYPE_UPVALUE, false);
    upvalue->closed = nn_value_makenull();
    upvalue->location = *slot;
    upvalue->next = nullptr;
    upvalue->stackpos = stackpos;
    return upvalue;
}

NNObjUserdata* nn_object_makeuserdata(void* pointer, const char* name)
{
    NNObjUserdata* ptr;
    ptr = (NNObjUserdata*)nn_object_allocobject(sizeof(NNObjUserdata), NEON_OBJTYPE_USERDATA, false);
    ptr->pointer = pointer;
    ptr->name = nn_util_strdup(name);
    ptr->ondestroyfn = nullptr;
    return ptr;
}

NNObjModule* nn_module_make(const char* name, const char* file, bool imported, bool retain)
{
    NNObjModule* module;
    module = (NNObjModule*)nn_object_allocobject(sizeof(NNObjModule), NEON_OBJTYPE_MODULE, retain);
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
    sw = (NNObjSwitch*)nn_object_allocobject(sizeof(NNObjSwitch), NEON_OBJTYPE_SWITCH, false);
    sw->table.initTable();
    sw->defaultjump = -1;
    sw->exitjump = -1;
    return sw;
}

NNObjRange* nn_object_makerange(int lower, int upper)
{
    NNObjRange* range;
    range = (NNObjRange*)nn_object_allocobject(sizeof(NNObjRange), NEON_OBJTYPE_RANGE, false);
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





NNValue nn_argcheck_vfail(NNArgCheck* ch, const char* srcfile, int srcline, const char* fmt, va_list va)
{
    #if 0
        nn_vm_stackpopn(ch->pstate, ch->argc);
    #endif
    if(!nn_except_vthrowwithclass(ch->pstate, ch->pstate->exceptions.argumenterror, srcfile, srcline, fmt, va))
    {
    }
    return nn_value_makebool(false);
}

NNValue nn_argcheck_fail(NNArgCheck* ch, const char* srcfile, int srcline, const char* fmt, ...)
{
    NNValue v;
    va_list va;
    va_start(va, fmt);
    v = nn_argcheck_vfail(ch, srcfile, srcline, fmt, va);
    va_end(va);
    return v;
}

void nn_argcheck_init(NNState* state, NNArgCheck* ch, const char* name, NNValue* argv, size_t argc)
{
    ch->pstate = state;
    ch->argc = argc;
    ch->argv = argv;
    ch->name = name;
}

NNProperty nn_property_makewithpointer(NNValue val, NNFieldType type)
{
    NNProperty vf;
    memset(&vf, 0, sizeof(NNProperty));
    vf.type = type;
    vf.value = val;
    vf.havegetset = false;
    return vf;
}

NNProperty nn_property_makewithgetset(NNValue val, NNValue getter, NNValue setter, NNFieldType type)
{
    bool getisfn;
    bool setisfn;
    NNProperty np;
    np = nn_property_makewithpointer(val, type);
    setisfn = nn_value_iscallable(setter);
    getisfn = nn_value_iscallable(getter);
    if(getisfn || setisfn)
    {
        np.getset.setter = setter;
        np.getset.getter = getter;
    }
    return np;
}

NNProperty nn_property_make(NNValue val, NNFieldType type)
{
    return nn_property_makewithpointer(val, type);
}

void nn_state_installmethods(NNState* state, NNObjClass* klass, NNConstClassMethodItem* listmethods)
{
    int i;
    const char* rawname;
    NNNativeFN rawfn;
    NNObjString* osname;
    for(i=0; listmethods[i].name != nullptr; i++)
    {
        rawname = listmethods[i].name;
        rawfn = listmethods[i].fn;
        osname = nn_string_intern(rawname);
        nn_class_defnativemethod(klass, osname, rawfn);
    }
}

void nn_state_initbuiltinmethods(NNState* state)
{
    nn_state_installobjectprocess(state);
    nn_state_installobjectobject(state);
    nn_state_installobjectnumber(state);
    nn_state_installobjectstring(state);
    nn_state_installobjectarray(state);
    nn_state_installobjectdict(state);
    nn_state_installobjectfile(state);
    nn_state_installobjectrange(state);
    nn_state_installmodmath(state);
}

/**
* see @nn_state_warn
*/
void nn_state_vwarn(NNState* state, const char* fmt, va_list va)
{
    if(state->conf.enablewarnings)
    {
        fprintf(stderr, "WARNING: ");
        vfprintf(stderr, fmt, va);
        fprintf(stderr, "\n");
    }
}

/**
* print a non-fatal runtime warning.
*/
void nn_state_warn(NNState* state, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    nn_state_vwarn(state, fmt, va);
    va_end(va);
}

/**
* procuce a stacktrace array; it is an object array, because it used both internally and in scripts.
* cannot take any shortcuts here.
*/
NNValue nn_except_getstacktrace(NNState* state)
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
    oa = nn_object_makearray();
    {
        for(i = 0; i < GCSingleton::get()->vmstate.framecount; i++)
        {
            nn_iostream_makestackstring(&pr);
            frame = &GCSingleton::get()->vmstate.framevalues[i];
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
            nn_iostream_printf(&pr, "from %s() in %s:%d", fnname, physfile, line);
            os = nn_iostream_takestring(&pr);
            nn_iostream_destroy(&pr);
            nn_array_push(oa, nn_value_fromobject(os));
            if((i > 15) && (state->conf.showfullstack == false))
            {
                nn_iostream_makestackstring(&pr);
                nn_iostream_printf(&pr, "(only upper 15 entries shown)");
                os = nn_iostream_takestring(&pr);
                nn_iostream_destroy(&pr);
                nn_array_push(oa, nn_value_fromobject(os));
                break;
            }
        }
        return nn_value_fromobject(oa);
    }
    return nn_value_fromobject(nn_string_internlen("", 0));
}

/**
* when an exception occured that was not caught, it is handled here.
*/
bool nn_except_propagate(NNState* state)
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
    NNExceptionFrame* handler;
    NNObjString* emsg;
    NNObjString* tmp;
    NNObjInstance* exception;
    NNProperty* field;
    exception = nn_value_asinstance(nn_vm_stackpeek(state, 0));
    /* look for a handler .... */
    while(GCSingleton::get()->vmstate.framecount > 0)
    {
        GCSingleton::get()->vmstate.currentframe = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount - 1];
        for(i = GCSingleton::get()->vmstate.currentframe->handlercount; i > 0; i--)
        {
            handler = &GCSingleton::get()->vmstate.currentframe->handlers[i - 1];
            function = GCSingleton::get()->vmstate.currentframe->closure->fnclosure.scriptfunc;
            if(handler->address != 0 /*&& nn_util_isinstanceof(exception->klass, handler->handlerklass)*/)
            {
                GCSingleton::get()->vmstate.currentframe->inscode = &function->fnscriptfunc.blob.instrucs[handler->address];
                return true;
            }
            else if(handler->finallyaddress != 0)
            {
                /* continue propagating once the 'finally' block completes */
                nn_vm_stackpush(state, nn_value_makebool(true));
                GCSingleton::get()->vmstate.currentframe->inscode = &function->fnscriptfunc.blob.instrucs[handler->finallyaddress];
                return true;
            }
        }
        GCSingleton::get()->vmstate.framecount--;
    }
    /* at this point, the exception is unhandled; so, print it out. */
    colred = nn_util_color(NEON_COLOR_RED);
    colblue = nn_util_color(NEON_COLOR_BLUE);
    colreset = nn_util_color(NEON_COLOR_RESET);
    colyellow = nn_util_color(NEON_COLOR_YELLOW);
    nn_iostream_printf(state->debugwriter, "%sunhandled %s%s", colred, nn_string_getdata(exception->klass->name), colreset);
    srcfile = "none";
    srcline = 0;
    field = exception->properties.getfieldbycstr("srcline");
    if(field != nullptr)
    {
        /* why does this happen? */
        if(nn_value_isnumber((NNValue)field->value))
        {
            srcline = nn_value_asnumber((NNValue)field->value);
        }
    }
    field = exception->properties.getfieldbycstr("srcfile");
    if(field != nullptr)
    {
        if(nn_value_isstring((NNValue)field->value))
        {
            tmp = nn_value_asstring((NNValue)field->value);
            srcfile = nn_string_getdata(tmp);
        }
    }
    nn_iostream_printf(state->debugwriter, " [from native %s%s:%d%s]", colyellow, srcfile, srcline, colreset);
    field = exception->properties.getfieldbycstr("message");
    if(field != nullptr)
    {
        emsg = nn_value_tostring((NNValue)field->value);
        if(nn_string_getlength(emsg) > 0)
        {
            nn_iostream_printf(state->debugwriter, ": %s", nn_string_getdata(emsg));
        }
        else
        {
            nn_iostream_printf(state->debugwriter, ":");
        }
    }
    nn_iostream_printf(state->debugwriter, "\n");
    field = exception->properties.getfieldbycstr("stacktrace");
    if(field != nullptr)
    {
        nn_iostream_printf(state->debugwriter, "%sstacktrace%s:\n", colblue, colreset);
        oa = nn_value_asarray((NNValue)field->value);
        cnt = oa->varray.count();
        i = cnt-1;
        if(cnt > 0)
        {
            while(true)
            {
                stackitm = oa->varray.get(i);
                nn_iostream_printf(state->debugwriter, "%s", colyellow);
                nn_iostream_printf(state->debugwriter, "  ");
                nn_iostream_printvalue(state->debugwriter, stackitm, false, true);
                nn_iostream_printf(state->debugwriter, "%s\n", colreset);
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
bool nn_except_pushhandler(NNState* state, NNObjClass* type, int address, int finallyaddress)
{
    NNCallFrame* frame;
    (void)type;
    frame = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount - 1];
    if(frame->handlercount == NEON_CONFIG_MAXEXCEPTHANDLERS)
    {
        nn_state_raisefatalerror(state, "too many nested exception handlers in one function");
        return false;
    }
    frame->handlers[frame->handlercount].address = address;
    frame->handlers[frame->handlercount].finallyaddress = finallyaddress;
    /*frame->handlers[frame->handlercount].handlerklass = type;*/
    frame->handlercount++;
    return true;
}


bool nn_except_vthrowactual(NNState* state, NNObjClass* klass, const char* srcfile, int srcline, const char* format, va_list va)
{
    bool b;
    b = nn_except_vthrowwithclass(state, klass, srcfile, srcline, format, va);
    return b;
}

bool nn_except_throwactual(NNState* state, NNObjClass* klass, const char* srcfile, int srcline, const char* format, ...)
{
    bool b;
    va_list va;
    va_start(va, format);
    b = nn_except_vthrowactual(state, klass, srcfile, srcline, format, va);
    va_end(va);
    return b;
}

/**
* throw an exception class. technically, any class can be thrown, but you should really only throw
* those deriving Exception.
*/
bool nn_except_throwwithclass(NNState* state, NNObjClass* klass, const char* srcfile, int srcline, const char* format, ...)
{
    bool b;
    va_list args;
    va_start(args, format);
    b = nn_except_vthrowwithclass(state, klass, srcfile, srcline, format, args);
    va_end(args);
    return b;
}

bool nn_except_vthrowwithclass(NNState* state, NNObjClass* exklass, const char* srcfile, int srcline, const char* format, va_list args)
{
    enum { kMaxBufSize = 1024};
    int length;
    char* message;
    NNValue stacktrace;
    NNObjInstance* instance;
    message = (char*)nn_memory_malloc(kMaxBufSize+1);
    length = vsnprintf(message, kMaxBufSize, format, args);
    instance = nn_except_makeinstance(state, exklass, srcfile, srcline, nn_string_takelen(message, length));
    nn_vm_stackpush(state, nn_value_fromobject(instance));
    stacktrace = nn_except_getstacktrace(state);
    nn_vm_stackpush(state, stacktrace);
    nn_instance_defproperty(instance, nn_string_intern("stacktrace"), stacktrace);
    nn_vm_stackpop(state);
    return nn_except_propagate(state);
}

/**
* helper for nn_except_makeclass.
*/
NNInstruction nn_util_makeinst(bool isop, uint8_t code, int srcline)
{
    NNInstruction inst;
    inst.isop = isop;
    inst.code = code;
    inst.fromsourceline = srcline;
    return inst;
}

/**
* generate bytecode for a nativee exception class.
* script-side it is enough to just derive from Exception, of course.
*/
NNObjClass* nn_except_makeclass(NNState* state, NNObjModule* module, const char* cstrname, bool iscs)
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
    nn_vm_stackpush(state, nn_value_fromobject(classname));
    klass = nn_object_makeclass(classname, state->classprimobject);
    nn_vm_stackpop(state);
    nn_vm_stackpush(state, nn_value_fromobject(klass));
    function = nn_object_makefuncscript(module, NEON_FNCONTEXTTYPE_METHOD);
    function->fnscriptfunc.arity = 1;
    function->fnscriptfunc.isvariadic = false;
    nn_vm_stackpush(state, nn_value_fromobject(function));
    {
        /* g_loc 0 */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, (0 >> 8) & 0xff, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, 0 & 0xff, 0));
    }
    {
        /* g_loc 1 */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, (1 >> 8) & 0xff, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, 1 & 0xff, 0));
    }
    {
        messageconst = nn_blob_pushconst(&function->fnscriptfunc.blob, nn_value_fromobject(nn_string_intern("message")));
        /* s_prop 0 */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_PROPERTYSET, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, (messageconst >> 8) & 0xff, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, messageconst & 0xff, 0));
    }
    {
        /* pop */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_POPONE, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_POPONE, 0));
    }
    {
        /* g_loc 0 */
        /*
        //  nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        //  nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, (0 >> 8) & 0xff, 0));
        //  nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, 0 & 0xff, 0));
        */
    }
    {
        /* ret */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_RETURN, 0));
    }
    closure = nn_object_makefuncclosure(function, nn_value_makenull());
    nn_vm_stackpop(state);
    /* set class constructor */
    nn_vm_stackpush(state, nn_value_fromobject(closure));
    klass->instmethods.set(nn_value_fromobject(classname), nn_value_fromobject(closure));
    klass->constructor = nn_value_fromobject(closure);
    /* set class properties */
    nn_class_defproperty(klass, nn_string_intern("message"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_intern("stacktrace"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_intern("srcfile"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_intern("srcline"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_intern("class"), nn_value_fromobject(klass));
    GCSingleton::get()->declaredglobals.set(nn_value_fromobject(classname), nn_value_fromobject(klass));
    /* for class */
    nn_vm_stackpop(state);
    nn_vm_stackpop(state);
    /* assert error name */
    /* nn_vm_stackpop(state); */
    return klass;
}

/**
* create an instance of an exception class.
*/
NNObjInstance* nn_except_makeinstance(NNState* state, NNObjClass* exklass, const char* srcfile, int srcline, NNObjString* message)
{
    NNObjInstance* instance;
    NNObjString* osfile;
    instance = nn_object_makeinstance(exklass);
    osfile = nn_string_copycstr(srcfile);
    nn_vm_stackpush(state, nn_value_fromobject(instance));
    nn_instance_defproperty(instance, nn_string_intern("class"), nn_value_fromobject(exklass));
    nn_instance_defproperty(instance, nn_string_intern("message"), nn_value_fromobject(message));
    nn_instance_defproperty(instance, nn_string_intern("srcfile"), nn_value_fromobject(osfile));
    nn_instance_defproperty(instance, nn_string_intern("srcline"), nn_value_makenumber(srcline));
    nn_vm_stackpop(state);
    return instance;
}

/**
* raise a fatal error that cannot recover.
*/
void nn_state_raisefatalerror(NNState* state, const char* format, ...)
{
    int i;
    int line;
    size_t instruction;
    va_list args;
    NNCallFrame* frame;
    NNObjFunction* function;
    /* flush out anything on stdout first */
    fflush(stdout);
    frame = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount - 1];
    function = frame->closure->fnclosure.scriptfunc;
    instruction = frame->inscode - function->fnscriptfunc.blob.instrucs - 1;
    line = function->fnscriptfunc.blob.instrucs[instruction].fromsourceline;
    fprintf(stderr, "RuntimeError: ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, " -> %s:%d ", nn_string_getdata(function->fnscriptfunc.module->physicalpath), line);
    fputs("\n", stderr);
    if(GCSingleton::get()->vmstate.framecount > 1)
    {
        fprintf(stderr, "stacktrace:\n");
        for(i = GCSingleton::get()->vmstate.framecount - 1; i >= 0; i--)
        {
            frame = &GCSingleton::get()->vmstate.framevalues[i];
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
    nn_state_resetvmstate(state);
}

bool nn_state_defglobalvalue(NNState* state, const char* name, NNValue val)
{
    bool r;
    NNObjString* oname;
    oname = nn_string_intern(name);
    nn_vm_stackpush(state, nn_value_fromobject(oname));
    nn_vm_stackpush(state, val);
    r = GCSingleton::get()->declaredglobals.set(GCSingleton::get()->vmstate.stackvalues[0], GCSingleton::get()->vmstate.stackvalues[1]);
    nn_vm_stackpopn(state, 2);
    return r;
}

bool nn_state_defnativefunctionptr(NNState* state, const char* name, NNNativeFN fptr, void* uptr)
{
    NNObjFunction* func;
    func = nn_object_makefuncnative(fptr, name, uptr);
    return nn_state_defglobalvalue(state, name, nn_value_fromobject(func));
}

bool nn_state_defnativefunction(NNState* state, const char* name, NNNativeFN fptr)
{
    return nn_state_defnativefunctionptr(state, name, fptr, nullptr);
}

NNObjClass* nn_util_makeclass(NNState* state, const char* name, NNObjClass* parent)
{
    NNObjClass* cl;
    NNObjString* os;
    os = nn_string_copycstr(name);
    cl = nn_object_makeclass(os, parent);
    GCSingleton::get()->declaredglobals.set(nn_value_fromobject(os), nn_value_fromobject(cl));
    return cl;
}

void nn_state_buildprocessinfo(NNState* state)
{
    enum{ kMaxBuf = 1024 };
    char* pathp;
    char pathbuf[kMaxBuf];
    state->processinfo = (NNProcessInfo*)nn_memory_malloc(sizeof(NNProcessInfo));
    state->processinfo->cliscriptfile = nullptr;
    state->processinfo->cliscriptdirectory = nullptr;
    state->processinfo->cliargv = nn_object_makearray();
    {
        pathp = osfn_getcwd(pathbuf, kMaxBuf);
        if(pathp == nullptr)
        {
            pathp = (char*)".";
        }
        state->processinfo->cliexedirectory = nn_string_copycstr(pathp);
    }
    {
        state->processinfo->cliprocessid = osfn_getpid();
    }
    {
        {
            state->processinfo->filestdout = nn_object_makefile(stdout, true, "<stdout>", "wb");
            nn_state_defglobalvalue(state, "STDOUT", nn_value_fromobject(state->processinfo->filestdout));
        }
        {
            state->processinfo->filestderr = nn_object_makefile(stderr, true, "<stderr>", "wb");
            nn_state_defglobalvalue(state, "STDERR", nn_value_fromobject(state->processinfo->filestderr));
        }
        {
            state->processinfo->filestdin = nn_object_makefile(stdin, true, "<stdin>", "rb");
            nn_state_defglobalvalue(state, "STDIN", nn_value_fromobject(state->processinfo->filestdin));
        }
    }
}

void nn_state_updateprocessinfo(NNState* state)
{
    char* prealpath;
    char* prealdir;
    if(state->rootphysfile != nullptr)
    {
        prealpath = osfn_realpath(state->rootphysfile, nullptr);
        prealdir = osfn_dirname(prealpath);
        state->processinfo->cliscriptfile = nn_string_copycstr(prealpath);
        state->processinfo->cliscriptdirectory = nn_string_copycstr(prealdir);
        nn_memory_free(prealpath);
        nn_memory_free(prealdir);
    }
    if(state->processinfo->cliscriptdirectory != nullptr)
    {
        nn_state_addmodulesearchpathobj(state, state->processinfo->cliscriptdirectory);
    }
}


NNState* nn_state_makealloc()
{
    NNState* state;
    state = (NNState*)nn_memory_malloc(sizeof(NNState));
    if(state == nullptr)
    {
        return nullptr;
    }
    GCSingleton::init();

    state->memuserptr = nullptr;
    state->exceptions.stdexception = nullptr;
    state->rootphysfile = nullptr;
    state->processinfo = nullptr;
    state->isrepl = false;
    nn_vm_initvmstate(state);
    nn_state_resetvmstate(state);
    /*
    * setup default config
    */
    {
        state->conf.enablestrictmode = false;
        state->conf.shoulddumpstack = false;
        state->conf.enablewarnings = false;
        state->conf.dumpbytecode = false;
        state->conf.exitafterbytecode = false;
        state->conf.showfullstack = false;
        state->conf.enableapidebug = false;
        state->conf.maxsyntaxerrors = NEON_CONFIG_MAXSYNTAXERRORS;
    }
    /*
    * initialize GC state
    */
    {

        state->lastreplvalue = nn_value_makenull();
    }
    /*
    * initialize various printer instances
    */
    {
        state->stdoutprinter = nn_iostream_makeio(stdout, false);
        state->stdoutprinter->shouldflush = false;
        state->stderrprinter = nn_iostream_makeio(stderr, false);
        state->debugwriter = nn_iostream_makeio(stderr, false);
        state->debugwriter->shortenvalues = true;
        state->debugwriter->maxvallength = 15;
    }
    /*
    * initialize runtime tables
    */
    {
        GCSingleton::get()->openedmodules.initTable();
        GCSingleton::get()->allocatedstrings.initTable();
        GCSingleton::get()->declaredglobals.initTable();
    }
    /*
    * initialize the toplevel module
    */
    {
        state->topmodule = nn_module_make("", "<state>", false, true);
    }
    {
        state->defaultstrings.nmconstructor = nn_string_intern("constructor");
        state->defaultstrings.nmindexget = nn_string_copycstr("__indexget__");
        state->defaultstrings.nmindexset = nn_string_copycstr("__indexset__");
        state->defaultstrings.nmadd = nn_string_copycstr("__add__");
        state->defaultstrings.nmsub = nn_string_copycstr("__sub__");
        state->defaultstrings.nmdiv = nn_string_copycstr("__div__");
        state->defaultstrings.nmmul = nn_string_copycstr("__mul__");
        state->defaultstrings.nmband = nn_string_copycstr("__band__");
        state->defaultstrings.nmbor = nn_string_copycstr("__bor__");
        state->defaultstrings.nmbxor = nn_string_copycstr("__bxor__");

    }
    /*
    * declare default classes
    */
    {
        state->classprimclass = nn_util_makeclass(state, "Class", nullptr);
        state->classprimobject = nn_util_makeclass(state, "Object", state->classprimclass);
        state->classprimnumber = nn_util_makeclass(state, "Number", state->classprimobject);
        state->classprimstring = nn_util_makeclass(state, "String", state->classprimobject);
        state->classprimarray = nn_util_makeclass(state, "Array", state->classprimobject);
        state->classprimdict = nn_util_makeclass(state, "Dict", state->classprimobject);
        state->classprimfile = nn_util_makeclass(state, "File", state->classprimobject);
        state->classprimrange = nn_util_makeclass(state, "Range", state->classprimobject);
        state->classprimcallable = nn_util_makeclass(state, "Function", state->classprimobject);
        state->classprimprocess = nn_util_makeclass(state, "Process", state->classprimobject);
    }
    /*
    * declare environment variables dictionary
    */
    {
        state->envdict = nn_object_makedict();
    }
    /*
    * declare default exception types
    */
    {
        if(state->exceptions.stdexception == nullptr)
        {
            state->exceptions.stdexception = nn_except_makeclass(state, nullptr, "Exception", true);
        }
        state->exceptions.asserterror = nn_except_makeclass(state, nullptr, "AssertError", true);
        state->exceptions.syntaxerror = nn_except_makeclass(state, nullptr, "SyntaxError", true);
        state->exceptions.ioerror = nn_except_makeclass(state, nullptr, "IOError", true);
        state->exceptions.oserror = nn_except_makeclass(state, nullptr, "OSError", true);
        state->exceptions.argumenterror = nn_except_makeclass(state, nullptr, "ArgumentError", true);
        state->exceptions.regexerror = nn_except_makeclass(state, nullptr, "RegexError", true);
        state->exceptions.importerror = nn_except_makeclass(state, nullptr, "ImportError", true);
    }
    /* all the other bits .... */
    nn_state_buildprocessinfo(state);
    /* NOW the module paths can be set up */
    nn_state_setupmodulepaths(state);
    {
        nn_state_initbuiltinfunctions(state);
        nn_state_initbuiltinmethods(state);
    }
    return state;
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
void nn_state_destroy(NNState* state, bool onstack)
{
    destrdebug("destroying importpath...");
    state->importpath.deInit(false);
    destrdebug("destroying linked objects...");
    nn_gcmem_destroylinkedobjects(state);
    /* since object in module can exist in declaredglobals, it must come before */
    destrdebug("destroying module table...");
    GCSingleton::get()->openedmodules.deinit();
    destrdebug("destroying globals table...");
    GCSingleton::get()->declaredglobals.deinit();
    destrdebug("destroying strings table...");
    GCSingleton::get()->allocatedstrings.deinit();
    destrdebug("destroying stdoutprinter...");
    nn_iostream_destroy(state->stdoutprinter);
    destrdebug("destroying stderrprinter...");
    nn_iostream_destroy(state->stderrprinter);
    destrdebug("destroying debugwriter...");
    nn_iostream_destroy(state->debugwriter);
    destrdebug("destroying framevalues...");
    nn_memory_free(GCSingleton::get()->vmstate.framevalues);
    destrdebug("destroying stackvalues...");
    nn_memory_free(GCSingleton::get()->vmstate.stackvalues);
    nn_memory_free(state->processinfo);
    destrdebug("destroying state...");
    if(!onstack)
    {
        nn_memory_free(state);
    }
    GCSingleton::destroy();
    destrdebug("done destroying!");
}

bool nn_util_methodisprivate(NNObjString* name)
{
    return nn_string_getlength(name) > 0 && nn_string_getdata(name)[0] == '_';
}


NNObjFunction* nn_state_compilesource(NNState* state, NNObjModule* module, bool fromeval, const char* source, bool toplevel)
{
    NNBlob blob;
    NNObjFunction* function;
    NNObjFunction* closure;
    (void)toplevel;
    nn_blob_init(&blob);
    function = nn_astparser_compilesource(state, module, source, &blob, false, fromeval);
    if(function == nullptr)
    {
        nn_blob_destroy(&blob);
        return nullptr;
    }
    if(!fromeval)
    {
        nn_vm_stackpush(state, nn_value_fromobject(function));
    }
    else
    {
        function->name = nn_string_intern("(evaledcode)");
    }
    closure = nn_object_makefuncclosure(function, nn_value_makenull());
    if(!fromeval)
    {
        nn_vm_stackpop(state);
        nn_vm_stackpush(state, nn_value_fromobject(closure));
    }
    nn_blob_destroy(&blob);
    return closure;
}

NNStatus nn_state_execsource(NNState* state, NNObjModule* module, const char* source, const char* filename, NNValue* dest)
{
    char* rp;
    NNStatus status;
    NNObjFunction* closure;
    state->rootphysfile = filename;
    nn_state_updateprocessinfo(state);
    rp = (char*)filename;
    state->topmodule->physicalpath = nn_string_copycstr(rp);
    nn_module_setfilefield(state, module);
    closure = nn_state_compilesource(state, module, false, source, true);
    if(closure == nullptr)
    {
        return NEON_STATUS_FAILCOMPILE;
    }
    if(state->conf.exitafterbytecode)
    {
        return NEON_STATUS_OK;
    }
    /*
    * NB. it is a closure, since it's compiled code.
    * so no need to create a NNValue and call nn_vm_callvalue().
    */
    nn_vm_callclosure(state, closure, nn_value_makenull(), 0, false);
    status = nn_vm_runvm(state, 0, dest);
    return status;
}

NNValue nn_state_evalsource(NNState* state, const char* source)
{
    bool ok;
    int argc;
    NNValue callme;
    NNValue retval;
    NNObjFunction* closure;
    (void)argc;
    closure = nn_state_compilesource(state, state->topmodule, true, source, false);
    callme = nn_value_fromobject(closure);
    argc = nn_nestcall_prepare(callme, nn_value_makenull(), nullptr, 0);
    ok = nn_nestcall_callfunction(state, callme, nn_value_makenull(), nullptr, 0, &retval, false);
    if(!ok)
    {
        nn_except_throw(state, "eval() failed");
    }
    return retval;
}

#define NN_ASTPARSER_GROWCAPACITY(capacity) \
    ((capacity) < 4 ? 4 : (capacity)*2)

static const char* g_strthis = "this";
static const char* g_strsuper = "super";

static void nn_astparser_runparser(NNAstParser* parser);
static void nn_astparser_ignorewhitespace(NNAstParser* prs);
static void nn_astparser_parsedeclaration(NNAstParser* prs);
static bool nn_astparser_raiseerroratv(NNAstParser* prs, NNAstToken* t, const char* message, va_list args);
static void nn_astparser_parseclassdeclaration(NNAstParser* prs, bool named);
static void nn_astparser_parsefuncdecl(NNAstParser* prs);
static void nn_astparser_advance(NNAstParser* prs);
static void nn_astparser_parsevardecl(NNAstParser* prs, bool isinitializer, bool isconst);
static void nn_astparser_parseexprstmt(NNAstParser* prs, bool isinitializer, bool semi);
static void nn_astparser_scopebegin(NNAstParser* prs);
static bool nn_astparser_check(NNAstParser* prs, NNAstTokType t);
static bool nn_astparser_parseblock(NNAstParser* prs);
static void nn_astparser_scopeend(NNAstParser* prs);
static void nn_astparser_parsestmt(NNAstParser* prs);
static void nn_astparser_synchronize(NNAstParser* prs);
static bool nn_astparser_consume(NNAstParser* prs, NNAstTokType t, const char* message);
static void nn_astparser_parseechostmt(NNAstParser* prs);
static void nn_astparser_parseifstmt(NNAstParser* prs);
static void nn_astparser_parsedo_whilestmt(NNAstParser* prs);
static void nn_astparser_parsewhilestmt(NNAstParser* prs);
static void nn_astparser_parseforstmt(NNAstParser* prs);
static void nn_astparser_parseforeachstmt(NNAstParser* prs);
static void nn_astparser_parseswitchstmt(NNAstParser* prs);
static void nn_astparser_parsecontinuestmt(NNAstParser* prs);
static void nn_astparser_parsebreakstmt(NNAstParser* prs);
static void nn_astparser_parsereturnstmt(NNAstParser* prs);
static void nn_astparser_parseassertstmt(NNAstParser* prs);
static void nn_astparser_parsethrowstmt(NNAstParser* prs);
static void nn_astparser_parsetrystmt(NNAstParser* prs);
static bool nn_astparser_rulebinary(NNAstParser* prs, NNAstToken previous, bool canassign);
static bool nn_astparser_parseprecedence(NNAstParser* prs, NNAstPrecedence precedence);
static int nn_astparser_parsevariable(NNAstParser* prs, const char* message);
static bool nn_astparser_rulecall(NNAstParser* prs, NNAstToken previous, bool canassign);
static uint8_t nn_astparser_parsefunccallargs(NNAstParser* prs);
static void nn_astparser_parseassign(NNAstParser* prs, uint8_t realop, uint8_t getop, uint8_t setop, int arg);
static bool nn_astparser_parseexpression(NNAstParser* prs);
static bool nn_astparser_ruleanonfunc(NNAstParser* prs, bool canassign);
static bool nn_astparser_ruleand(NNAstParser* prs, NNAstToken previous, bool canassign);
static NNAstRule* nn_astparser_putrule(NNAstRule* dest, NNAstParsePrefixFN prefix, NNAstParseInfixFN infix, NNAstPrecedence precedence);
static bool nn_astparser_ruleanonclass(NNAstParser* prs, bool canassign);
static void nn_astparser_parsefuncparamlist(NNAstParser* prs, NNAstFuncCompiler* fnc);


void nn_blob_init(NNBlob* blob)
{
    blob->count = 0;
    blob->capacity = 0;
    blob->instrucs = nullptr;
    blob->constants.initSelf();
    blob->argdefvals.initSelf();
}

void nn_blob_push(NNBlob* blob, NNInstruction ins)
{
    int oldcapacity;
    if(blob->capacity < blob->count + 1)
    {
        oldcapacity = blob->capacity;
        blob->capacity = NN_ASTPARSER_GROWCAPACITY(oldcapacity);
        blob->instrucs = (NNInstruction*)nn_memory_realloc(blob->instrucs, blob->capacity * sizeof(NNInstruction));
    }
    blob->instrucs[blob->count] = ins;
    blob->count++;
}

void nn_blob_destroy(NNBlob* blob)
{
    if(blob->instrucs != nullptr)
    {
        nn_memory_free(blob->instrucs);
    }
    blob->constants.deInit(false);
    blob->argdefvals.deInit(false);
}

int nn_blob_pushconst(NNBlob* blob, NNValue value)
{
    blob->constants.push(value);
    return blob->constants.count() - 1;
}


/*
* allows for the lexer to created on the stack.
*/
void nn_astlex_init(NNAstLexer* lex, NNState* state, const char* source)
{
    lex->pstate = state;
    lex->sourceptr = source;
    lex->start = source;
    lex->sourceptr = lex->start;
    lex->line = 1;
    lex->tplstringcount = -1;
    lex->onstack = true;
}

NNAstLexer* nn_astlex_make(NNState* state, const char* source)
{
    NNAstLexer* lex;
    lex = (NNAstLexer*)nn_memory_malloc(sizeof(NNAstLexer));
    nn_astlex_init(lex, state, source);
    lex->onstack = false;
    return lex;
}

void nn_astlex_destroy(NNAstLexer* lex)
{
    if(!lex->onstack)
    {
        nn_memory_free(lex);
    }
}

bool nn_astlex_isatend(NNAstLexer* lex)
{
    return *lex->sourceptr == '\0';
}

static NNAstToken nn_astlex_createtoken(NNAstLexer* lex, NNAstTokType type)
{
    NNAstToken t;
    t.isglobal = false;
    t.type = type;
    t.start = lex->start;
    t.length = (int)(lex->sourceptr - lex->start);
    t.line = lex->line;
    return t;
}

static NNAstToken nn_astlex_errortoken(NNAstLexer* lex, const char* fmt, ...)
{
    int length;
    char* buf;
    va_list va;
    NNAstToken t;
    va_start(va, fmt);
    buf = (char*)nn_memory_malloc(sizeof(char) * 1024);
    /* TODO: used to be vasprintf. need to check how much to actually allocate! */
    length = vsprintf(buf, fmt, va);
    va_end(va);
    t.type = NEON_ASTTOK_ERROR;
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
    t.line = lex->line;
    return t;
}

static bool nn_astutil_isdigit(char c)
{
    return c >= '0' && c <= '9';
}

static bool nn_astutil_isbinary(char c)
{
    return c == '0' || c == '1';
}

static bool nn_astutil_isalpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool nn_astutil_isoctal(char c)
{
    return c >= '0' && c <= '7';
}

static bool nn_astutil_ishexadecimal(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

const char* nn_astutil_toktype2str(int t)
{
    switch(t)
    {
        case NEON_ASTTOK_NEWLINE: return "NEON_ASTTOK_NEWLINE";
        case NEON_ASTTOK_PARENOPEN: return "NEON_ASTTOK_PARENOPEN";
        case NEON_ASTTOK_PARENCLOSE: return "NEON_ASTTOK_PARENCLOSE";
        case NEON_ASTTOK_BRACKETOPEN: return "NEON_ASTTOK_BRACKETOPEN";
        case NEON_ASTTOK_BRACKETCLOSE: return "NEON_ASTTOK_BRACKETCLOSE";
        case NEON_ASTTOK_BRACEOPEN: return "NEON_ASTTOK_BRACEOPEN";
        case NEON_ASTTOK_BRACECLOSE: return "NEON_ASTTOK_BRACECLOSE";
        case NEON_ASTTOK_SEMICOLON: return "NEON_ASTTOK_SEMICOLON";
        case NEON_ASTTOK_COMMA: return "NEON_ASTTOK_COMMA";
        case NEON_ASTTOK_BACKSLASH: return "NEON_ASTTOK_BACKSLASH";
        case NEON_ASTTOK_EXCLMARK: return "NEON_ASTTOK_EXCLMARK";
        case NEON_ASTTOK_NOTEQUAL: return "NEON_ASTTOK_NOTEQUAL";
        case NEON_ASTTOK_COLON: return "NEON_ASTTOK_COLON";
        case NEON_ASTTOK_AT: return "NEON_ASTTOK_AT";
        case NEON_ASTTOK_DOT: return "NEON_ASTTOK_DOT";
        case NEON_ASTTOK_DOUBLEDOT: return "NEON_ASTTOK_DOUBLEDOT";
        case NEON_ASTTOK_TRIPLEDOT: return "NEON_ASTTOK_TRIPLEDOT";
        case NEON_ASTTOK_PLUS: return "NEON_ASTTOK_PLUS";
        case NEON_ASTTOK_PLUSASSIGN: return "NEON_ASTTOK_PLUSASSIGN";
        case NEON_ASTTOK_INCREMENT: return "NEON_ASTTOK_INCREMENT";
        case NEON_ASTTOK_MINUS: return "NEON_ASTTOK_MINUS";
        case NEON_ASTTOK_MINUSASSIGN: return "NEON_ASTTOK_MINUSASSIGN";
        case NEON_ASTTOK_DECREMENT: return "NEON_ASTTOK_DECREMENT";
        case NEON_ASTTOK_MULTIPLY: return "NEON_ASTTOK_MULTIPLY";
        case NEON_ASTTOK_MULTASSIGN: return "NEON_ASTTOK_MULTASSIGN";
        case NEON_ASTTOK_POWEROF: return "NEON_ASTTOK_POWEROF";
        case NEON_ASTTOK_POWASSIGN: return "NEON_ASTTOK_POWASSIGN";
        case NEON_ASTTOK_DIVIDE: return "NEON_ASTTOK_DIVIDE";
        case NEON_ASTTOK_DIVASSIGN: return "NEON_ASTTOK_DIVASSIGN";
        case NEON_ASTTOK_FLOOR: return "NEON_ASTTOK_FLOOR";
        case NEON_ASTTOK_ASSIGN: return "NEON_ASTTOK_ASSIGN";
        case NEON_ASTTOK_EQUAL: return "NEON_ASTTOK_EQUAL";
        case NEON_ASTTOK_LESSTHAN: return "NEON_ASTTOK_LESSTHAN";
        case NEON_ASTTOK_LESSEQUAL: return "NEON_ASTTOK_LESSEQUAL";
        case NEON_ASTTOK_LEFTSHIFT: return "NEON_ASTTOK_LEFTSHIFT";
        case NEON_ASTTOK_LEFTSHIFTASSIGN: return "NEON_ASTTOK_LEFTSHIFTASSIGN";
        case NEON_ASTTOK_GREATERTHAN: return "NEON_ASTTOK_GREATERTHAN";
        case NEON_ASTTOK_GREATER_EQ: return "NEON_ASTTOK_GREATER_EQ";
        case NEON_ASTTOK_RIGHTSHIFT: return "NEON_ASTTOK_RIGHTSHIFT";
        case NEON_ASTTOK_RIGHTSHIFTASSIGN: return "NEON_ASTTOK_RIGHTSHIFTASSIGN";
        case NEON_ASTTOK_MODULO: return "NEON_ASTTOK_MODULO";
        case NEON_ASTTOK_PERCENT_EQ: return "NEON_ASTTOK_PERCENT_EQ";
        case NEON_ASTTOK_AMP: return "NEON_ASTTOK_AMP";
        case NEON_ASTTOK_AMP_EQ: return "NEON_ASTTOK_AMP_EQ";
        case NEON_ASTTOK_BAR: return "NEON_ASTTOK_BAR";
        case NEON_ASTTOK_BAR_EQ: return "NEON_ASTTOK_BAR_EQ";
        case NEON_ASTTOK_TILDE: return "NEON_ASTTOK_TILDE";
        case NEON_ASTTOK_TILDE_EQ: return "NEON_ASTTOK_TILDE_EQ";
        case NEON_ASTTOK_XOR: return "NEON_ASTTOK_XOR";
        case NEON_ASTTOK_XOR_EQ: return "NEON_ASTTOK_XOR_EQ";
        case NEON_ASTTOK_QUESTION: return "NEON_ASTTOK_QUESTION";
        case NEON_ASTTOK_KWAND: return "NEON_ASTTOK_KWAND";
        case NEON_ASTTOK_KWAS: return "NEON_ASTTOK_KWAS";
        case NEON_ASTTOK_KWASSERT: return "NEON_ASTTOK_KWASSERT";
        case NEON_ASTTOK_KWBREAK: return "NEON_ASTTOK_KWBREAK";
        case NEON_ASTTOK_KWCATCH: return "NEON_ASTTOK_KWCATCH";
        case NEON_ASTTOK_KWCLASS: return "NEON_ASTTOK_KWCLASS";
        case NEON_ASTTOK_KWCONTINUE: return "NEON_ASTTOK_KWCONTINUE";
        case NEON_ASTTOK_KWFUNCTION: return "NEON_ASTTOK_KWFUNCTION";
        case NEON_ASTTOK_KWDEFAULT: return "NEON_ASTTOK_KWDEFAULT";
        case NEON_ASTTOK_KWTHROW: return "NEON_ASTTOK_KWTHROW";
        case NEON_ASTTOK_KWDO: return "NEON_ASTTOK_KWDO";
        case NEON_ASTTOK_KWECHO: return "NEON_ASTTOK_KWECHO";
        case NEON_ASTTOK_KWELSE: return "NEON_ASTTOK_KWELSE";
        case NEON_ASTTOK_KWFALSE: return "NEON_ASTTOK_KWFALSE";
        case NEON_ASTTOK_KWFINALLY: return "NEON_ASTTOK_KWFINALLY";
        case NEON_ASTTOK_KWFOREACH: return "NEON_ASTTOK_KWFOREACH";
        case NEON_ASTTOK_KWIF: return "NEON_ASTTOK_KWIF";
        case NEON_ASTTOK_KWIMPORT: return "NEON_ASTTOK_KWIMPORT";
        case NEON_ASTTOK_KWIN: return "NEON_ASTTOK_KWIN";
        case NEON_ASTTOK_KWFOR: return "NEON_ASTTOK_KWFOR";
        case NEON_ASTTOK_KWNULL: return "NEON_ASTTOK_KWNULL";
        case NEON_ASTTOK_KWNEW: return "NEON_ASTTOK_KWNEW";
        case NEON_ASTTOK_KWOR: return "NEON_ASTTOK_KWOR";
        case NEON_ASTTOK_KWSUPER: return "NEON_ASTTOK_KWSUPER";
        case NEON_ASTTOK_KWRETURN: return "NEON_ASTTOK_KWRETURN";
        case NEON_ASTTOK_KWTHIS: return "NEON_ASTTOK_KWTHIS";
        case NEON_ASTTOK_KWSTATIC: return "NEON_ASTTOK_KWSTATIC";
        case NEON_ASTTOK_KWTRUE: return "NEON_ASTTOK_KWTRUE";
        case NEON_ASTTOK_KWTRY: return "NEON_ASTTOK_KWTRY";
        case NEON_ASTTOK_KWSWITCH: return "NEON_ASTTOK_KWSWITCH";
        case NEON_ASTTOK_KWVAR: return "NEON_ASTTOK_KWVAR";
        case NEON_ASTTOK_KWCONST: return "NEON_ASTTOK_KWCONST";
        case NEON_ASTTOK_KWCASE: return "NEON_ASTTOK_KWCASE";
        case NEON_ASTTOK_KWWHILE: return "NEON_ASTTOK_KWWHILE";
        case NEON_ASTTOK_KWINSTANCEOF: return "NEON_ASTTOK_KWINSTANCEOF";
        case NEON_ASTTOK_KWEXTENDS: return "NEON_ASTTOK_KWEXTENDS";
        case NEON_ASTTOK_LITERALSTRING: return "NEON_ASTTOK_LITERALSTRING";
        case NEON_ASTTOK_LITERALRAWSTRING: return "NEON_ASTTOK_LITERALRAWSTRING";
        case NEON_ASTTOK_LITNUMREG: return "NEON_ASTTOK_LITNUMREG";
        case NEON_ASTTOK_LITNUMBIN: return "NEON_ASTTOK_LITNUMBIN";
        case NEON_ASTTOK_LITNUMOCT: return "NEON_ASTTOK_LITNUMOCT";
        case NEON_ASTTOK_LITNUMHEX: return "NEON_ASTTOK_LITNUMHEX";
        case NEON_ASTTOK_IDENTNORMAL: return "NEON_ASTTOK_IDENTNORMAL";
        case NEON_ASTTOK_DECORATOR: return "NEON_ASTTOK_DECORATOR";
        case NEON_ASTTOK_INTERPOLATION: return "NEON_ASTTOK_INTERPOLATION";
        case NEON_ASTTOK_EOF: return "NEON_ASTTOK_EOF";
        case NEON_ASTTOK_ERROR: return "NEON_ASTTOK_ERROR";
        case NEON_ASTTOK_KWEMPTY: return "NEON_ASTTOK_KWEMPTY";
        case NEON_ASTTOK_UNDEFINED: return "NEON_ASTTOK_UNDEFINED";
        case NEON_ASTTOK_TOKCOUNT: return "NEON_ASTTOK_TOKCOUNT";
    }
    return "?invalid?";
}

static char nn_astlex_advance(NNAstLexer* lex)
{
    lex->sourceptr++;
    if(lex->sourceptr[-1] == '\n')
    {
        lex->line++;
    }
    return lex->sourceptr[-1];
}

static bool nn_astlex_match(NNAstLexer* lex, char expected)
{
    if(nn_astlex_isatend(lex))
    {
        return false;
    }
    if(*lex->sourceptr != expected)
    {
        return false;
    }
    lex->sourceptr++;
    if(lex->sourceptr[-1] == '\n')
    {
        lex->line++;
    }
    return true;
}

static char nn_astlex_peekcurr(NNAstLexer* lex)
{
    return *lex->sourceptr;
}

static char nn_astlex_peekprev(NNAstLexer* lex)
{
    if(lex->sourceptr == lex->start)
    {
        return -1;
    }
    return lex->sourceptr[-1];
}

static char nn_astlex_peeknext(NNAstLexer* lex)
{
    if(nn_astlex_isatend(lex))
    {
        return '\0';
    }
    return lex->sourceptr[1];
}

static NNAstToken nn_astlex_skipblockcomments(NNAstLexer* lex)
{
    int nesting;
    nesting = 1;
    while(nesting > 0)
    {
        if(nn_astlex_isatend(lex))
        {
            return nn_astlex_errortoken(lex, "unclosed block comment");
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
        #if 0
            nn_astlex_advance(lex);
        #endif
    #endif
    return nn_astlex_createtoken(lex, NEON_ASTTOK_UNDEFINED);
}

static NNAstToken nn_astlex_skipspace(NNAstLexer* lex)
{
    char c;
    NNAstToken result;
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
                    lex->line++;
                    nn_astlex_advance(lex);
                }
                break;
            */
            /*
            case '#':
                // single line comment
                {
                    while(nn_astlex_peekcurr(lex) != '\n' && !nn_astlex_isatend(lex))
                        nn_astlex_advance(lex);

                }
                break;
            */
            case '/':
            {
                if(nn_astlex_peeknext(lex) == '/')
                {
                    while(nn_astlex_peekcurr(lex) != '\n' && !nn_astlex_isatend(lex))
                    {
                        nn_astlex_advance(lex);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_UNDEFINED);
                }
                else if(nn_astlex_peeknext(lex) == '*')
                {
                    nn_astlex_advance(lex);
                    nn_astlex_advance(lex);
                    result = nn_astlex_skipblockcomments(lex);
                    if(result.type != NEON_ASTTOK_UNDEFINED)
                    {
                        return result;
                    }
                    break;
                }
                else
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_UNDEFINED);
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
    return nn_astlex_createtoken(lex, NEON_ASTTOK_UNDEFINED);
}

NNAstToken nn_astlex_scanstring(NNAstLexer* lex, char quote, bool withtemplate, bool permitescapes)
{
    NNAstToken tkn;
    while(nn_astlex_peekcurr(lex) != quote && !nn_astlex_isatend(lex))
    {
        if(withtemplate)
        {
            /* interpolation started */
            if(nn_astlex_peekcurr(lex) == '$' && nn_astlex_peeknext(lex) == '{' && nn_astlex_peekprev(lex) != '\\')
            {
                if(lex->tplstringcount - 1 < NEON_CONFIG_ASTMAXSTRTPLDEPTH)
                {
                    lex->tplstringcount++;
                    lex->tplstringbuffer[lex->tplstringcount] = (int)quote;
                    lex->sourceptr++;
                    tkn = nn_astlex_createtoken(lex, NEON_ASTTOK_INTERPOLATION);
                    lex->sourceptr++;
                    return tkn;
                }
                return nn_astlex_errortoken(lex, "maximum interpolation nesting of %d exceeded by %d", NEON_CONFIG_ASTMAXSTRTPLDEPTH,
                    NEON_CONFIG_ASTMAXSTRTPLDEPTH - lex->tplstringcount + 1);
            }
        }
        if(nn_astlex_peekcurr(lex) == '\\' && (nn_astlex_peeknext(lex) == quote || nn_astlex_peeknext(lex) == '\\'))
        {
            nn_astlex_advance(lex);
        }
        nn_astlex_advance(lex);
    }
    if(nn_astlex_isatend(lex))
    {
        return nn_astlex_errortoken(lex, "unterminated string (opening quote not matched)");
    }
    /* the closing quote */
    nn_astlex_match(lex, quote);
    if(permitescapes)
    {
        return nn_astlex_createtoken(lex, NEON_ASTTOK_LITERALSTRING);
    }
    return nn_astlex_createtoken(lex, NEON_ASTTOK_LITERALRAWSTRING);
}

NNAstToken nn_astlex_scannumber(NNAstLexer* lex)
{
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
            return nn_astlex_createtoken(lex, NEON_ASTTOK_LITNUMBIN);
        }
        else if(nn_astlex_match(lex, 'c'))
        {
            while(nn_astutil_isoctal(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
            return nn_astlex_createtoken(lex, NEON_ASTTOK_LITNUMOCT);
        }
        else if(nn_astlex_match(lex, 'x'))
        {
            while(nn_astutil_ishexadecimal(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
            return nn_astlex_createtoken(lex, NEON_ASTTOK_LITNUMHEX);
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
    return nn_astlex_createtoken(lex, NEON_ASTTOK_LITNUMREG);
}

static NNAstTokType nn_astlex_getidenttype(NNAstLexer* lex)
{
    static const struct
    {
        const char* str;
        int tokid;
    }
    keywords[] =
    {
        { "and", NEON_ASTTOK_KWAND },
        { "assert", NEON_ASTTOK_KWASSERT },
        { "as", NEON_ASTTOK_KWAS },
        { "break", NEON_ASTTOK_KWBREAK },
        { "catch", NEON_ASTTOK_KWCATCH },
        { "class", NEON_ASTTOK_KWCLASS },
        { "continue", NEON_ASTTOK_KWCONTINUE },
        { "default", NEON_ASTTOK_KWDEFAULT },
        { "def", NEON_ASTTOK_KWFUNCTION },
        { "function", NEON_ASTTOK_KWFUNCTION },
        { "throw", NEON_ASTTOK_KWTHROW },
        { "do", NEON_ASTTOK_KWDO },
        { "echo", NEON_ASTTOK_KWECHO },
        { "else", NEON_ASTTOK_KWELSE },
        { "empty", NEON_ASTTOK_KWEMPTY },
        { "extends", NEON_ASTTOK_KWEXTENDS },
        { "false", NEON_ASTTOK_KWFALSE },
        { "finally", NEON_ASTTOK_KWFINALLY },
        { "foreach", NEON_ASTTOK_KWFOREACH },
        { "if", NEON_ASTTOK_KWIF },
        { "import", NEON_ASTTOK_KWIMPORT },
        { "in", NEON_ASTTOK_KWIN },
        { "instanceof", NEON_ASTTOK_KWINSTANCEOF},
        { "for", NEON_ASTTOK_KWFOR },
        { "null", NEON_ASTTOK_KWNULL },
        { "new", NEON_ASTTOK_KWNEW },
        { "or", NEON_ASTTOK_KWOR },
        { "super", NEON_ASTTOK_KWSUPER },
        { "return", NEON_ASTTOK_KWRETURN },
        { "this", NEON_ASTTOK_KWTHIS },
        { "static", NEON_ASTTOK_KWSTATIC },
        { "true", NEON_ASTTOK_KWTRUE },
        { "try", NEON_ASTTOK_KWTRY },
        { "typeof", NEON_ASTTOK_KWTYPEOF },
        { "switch", NEON_ASTTOK_KWSWITCH },
        { "case", NEON_ASTTOK_KWCASE },
        { "var", NEON_ASTTOK_KWVAR },
        { "let", NEON_ASTTOK_KWVAR },
        { "const", NEON_ASTTOK_KWCONST },
        { "while", NEON_ASTTOK_KWWHILE },
        { nullptr, (NNAstTokType)0 }
    };
    size_t i;
    size_t kwlen;
    size_t ofs;
    const char* kwtext;
    for(i = 0; keywords[i].str != nullptr; i++)
    {
        kwtext = keywords[i].str;
        kwlen = strlen(kwtext);
        ofs = (lex->sourceptr - lex->start);
        if(ofs == kwlen)
        {
            if(memcmp(lex->start, kwtext, kwlen) == 0)
            {
                return (NNAstTokType)keywords[i].tokid;
            }
        }
    }
    return NEON_ASTTOK_IDENTNORMAL;
}

NNAstToken nn_astlex_scanident(NNAstLexer* lex, bool isdollar)
{
    int cur;
    NNAstToken tok;
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
    tok = nn_astlex_createtoken(lex, nn_astlex_getidenttype(lex));
    tok.isglobal = isdollar;
    return tok;
}

static NNAstToken nn_astlex_scandecorator(NNAstLexer* lex)
{
    while(nn_astutil_isalpha(nn_astlex_peekcurr(lex)) || nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
    {
        nn_astlex_advance(lex);
    }
    return nn_astlex_createtoken(lex, NEON_ASTTOK_DECORATOR);
}

NNAstToken nn_astlex_scantoken(NNAstLexer* lex)
{
    char c;
    bool isdollar;
    NNAstToken tk;
    NNAstToken token;
    tk = nn_astlex_skipspace(lex);
    if(tk.type != NEON_ASTTOK_UNDEFINED)
    {
        return tk;
    }
    lex->start = lex->sourceptr;
    if(nn_astlex_isatend(lex))
    {
        return nn_astlex_createtoken(lex, NEON_ASTTOK_EOF);
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
                return nn_astlex_createtoken(lex, NEON_ASTTOK_PARENOPEN);
            }
            break;
        case ')':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_PARENCLOSE);
            }
            break;
        case '[':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_BRACKETOPEN);
            }
            break;
        case ']':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_BRACKETCLOSE);
            }
            break;
        case '{':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_BRACEOPEN);
            }
            break;
        case '}':
            {
                if(lex->tplstringcount > -1)
                {
                    token = nn_astlex_scanstring(lex, (char)lex->tplstringbuffer[lex->tplstringcount], true, true);
                    lex->tplstringcount--;
                    return token;
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_BRACECLOSE);
            }
            break;
        case ';':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_SEMICOLON);
            }
            break;
        case '\\':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_BACKSLASH);
            }
            break;
        case ':':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_COLON);
            }
            break;
        case ',':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_COMMA);
            }
            break;
        case '@':
            {
                if(!nn_astutil_isalpha(nn_astlex_peekcurr(lex)))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_AT);
                }
                return nn_astlex_scandecorator(lex);
            }
            break;
        case '!':
            {
                if(nn_astlex_match(lex, '='))
                {
                    /* pseudo-handle '!==' */
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_NOTEQUAL);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_NOTEQUAL);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_EXCLMARK);

            }
            break;
        case '.':
            {
                if(nn_astlex_match(lex, '.'))
                {
                    if(nn_astlex_match(lex, '.'))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_TRIPLEDOT);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_DOUBLEDOT);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_DOT);
            }
            break;
        case '+':
        {
            if(nn_astlex_match(lex, '+'))
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_INCREMENT);
            }
            if(nn_astlex_match(lex, '='))
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_PLUSASSIGN);
            }
            else
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_PLUS);
            }
        }
        break;
        case '-':
            {
                if(nn_astlex_match(lex, '-'))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_DECREMENT);
                }
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_MINUSASSIGN);
                }
                else
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_MINUS);
                }
            }
            break;
        case '*':
            {
                if(nn_astlex_match(lex, '*'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_POWASSIGN);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_POWEROF);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_MULTASSIGN);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_MULTIPLY);
                }
            }
            break;
        case '/':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_DIVASSIGN);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_DIVIDE);
            }
            break;
        case '=':
            {
                if(nn_astlex_match(lex, '='))
                {
                    /* pseudo-handle === */
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_EQUAL);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_EQUAL);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_ASSIGN);
            }        
            break;
        case '<':
            {
                if(nn_astlex_match(lex, '<'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_LEFTSHIFTASSIGN);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_LEFTSHIFT);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_LESSEQUAL);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_LESSTHAN);

                }
            }
            break;
        case '>':
            {
                if(nn_astlex_match(lex, '>'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_RIGHTSHIFTASSIGN);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_RIGHTSHIFT);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_createtoken(lex, NEON_ASTTOK_GREATER_EQ);
                    }
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_GREATERTHAN);
                }
            }
            break;
        case '%':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_PERCENT_EQ);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_MODULO);
            }
            break;
        case '&':
            {
                if(nn_astlex_match(lex, '&'))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_KWAND);
                }
                else if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_AMP_EQ);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_AMP);
            }
            break;
        case '|':
            {
                if(nn_astlex_match(lex, '|'))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_KWOR);
                }
                else if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_BAR_EQ);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_BAR);
            }
            break;
        case '~':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_TILDE_EQ);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_TILDE);
            }
            break;
        case '^':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_createtoken(lex, NEON_ASTTOK_XOR_EQ);
                }
                return nn_astlex_createtoken(lex, NEON_ASTTOK_XOR);
            }
            break;
        case '\n':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_NEWLINE);
            }
            break;
        case '"':
            {
                return nn_astlex_scanstring(lex, '"', false, true);
            }
            break;
        case '\'':
            {
                return nn_astlex_scanstring(lex, '\'', false, false);
            }
            break;
        case '`':
            {
                return nn_astlex_scanstring(lex, '`', true, true);
            }
            break;
        case '?':
            {
                return nn_astlex_createtoken(lex, NEON_ASTTOK_QUESTION);
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
    return nn_astlex_errortoken(lex, "unexpected character %c", c);
}

NNAstParser* nn_astparser_makeparser(NNState* state, NNAstLexer* lexer, NNObjModule* module, bool keeplast)
{
    NNAstParser* parser;
    parser = (NNAstParser*)nn_memory_malloc(sizeof(NNAstParser));
    parser->pstate = state;
    parser->lexer = lexer;
    parser->currentfunccompiler = nullptr;
    parser->haderror = false;
    parser->panicmode = false;
    parser->stopprintingsyntaxerrors = false;
    parser->blockcount = 0;
    parser->errorcount = 0;
    parser->replcanecho = false;
    parser->isreturning = false;
    parser->istrying = false;
    parser->compcontext = NEON_COMPCONTEXT_NONE;
    parser->innermostloopstart = -1;
    parser->innermostloopscopedepth = 0;
    parser->currentclasscompiler = nullptr;
    parser->currentmodule = module;
    parser->keeplastvalue = keeplast;
    parser->lastwasstatement = false;
    parser->infunction = false;
    parser->inswitch = false;
    parser->currentfile = nn_string_getdata(parser->currentmodule->physicalpath);
    return parser;
}

void nn_astparser_destroy(NNAstParser* parser)
{
    nn_memory_free(parser);
}

static NNBlob* nn_astparser_currentblob(NNAstParser* prs)
{
    return &prs->currentfunccompiler->targetfunc->fnscriptfunc.blob;
}

static bool nn_astparser_raiseerroratv(NNAstParser* prs, NNAstToken* t, const char* message, va_list args)
{
    const char* colred;
    const char* colreset;    
    colred = nn_util_color(NEON_COLOR_RED);
    colreset = nn_util_color(NEON_COLOR_RESET);
    fflush(stdout);
    if(prs->stopprintingsyntaxerrors)
    {
        return false;
    }
    if((prs->pstate->conf.maxsyntaxerrors != 0) && (prs->errorcount >= prs->pstate->conf.maxsyntaxerrors))
    {
        fprintf(stderr, "%stoo many errors emitted%s (maximum is %d)\n", colred, colreset, prs->pstate->conf.maxsyntaxerrors);
        prs->stopprintingsyntaxerrors = true;
        return false;
    }
    /*
    // do not cascade error
    // suppress error if already in panic mode
    */
    if(prs->panicmode)
    {
        return false;
    }
    prs->panicmode = true;
    fprintf(stderr, "(%d) %sSyntaxError%s",  prs->errorcount, colred, colreset);
    fprintf(stderr, " in [%s:%d]: ", nn_string_getdata(prs->currentmodule->physicalpath), t->line);
    vfprintf(stderr, message, args);
    fprintf(stderr, " ");
    if(t->type == NEON_ASTTOK_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if(t->type == NEON_ASTTOK_ERROR)
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
    prs->haderror = true;
    prs->errorcount++;
    return false;
}

static bool nn_astparser_raiseerror(NNAstParser* prs, const char* message, ...)
{
    va_list args;
    va_start(args, message);
    nn_astparser_raiseerroratv(prs, &prs->prevtoken, message, args);
    va_end(args);
    return false;
}

static bool nn_astparser_raiseerroratcurrent(NNAstParser* prs, const char* message, ...)
{
    va_list args;
    va_start(args, message);
    nn_astparser_raiseerroratv(prs, &prs->currtoken, message, args);
    va_end(args);
    return false;
}

static void nn_astparser_advance(NNAstParser* prs)
{
    prs->prevtoken = prs->currtoken;
    while(true)
    {
        prs->currtoken = nn_astlex_scantoken(prs->lexer);
        if(prs->currtoken.type != NEON_ASTTOK_ERROR)
        {
            break;
        }
        nn_astparser_raiseerroratcurrent(prs, prs->currtoken.start);
    }
}

static bool nn_astparser_consume(NNAstParser* prs, NNAstTokType t, const char* message)
{
    if(nn_astparser_istype(prs->currtoken.type, t))
    {
        nn_astparser_advance(prs);
        return true;
    }
    return nn_astparser_raiseerroratcurrent(prs, message);
}

static void nn_astparser_consumeor(NNAstParser* prs, const char* message, const NNAstTokType* ts, int count)
{
    int i;
    for(i = 0; i < count; i++)
    {
        if(prs->currtoken.type == ts[i])
        {
            nn_astparser_advance(prs);
            return;
        }
    }
    nn_astparser_raiseerroratcurrent(prs, message);
}

static bool nn_astparser_checknumber(NNAstParser* prs)
{
    NNAstTokType t;
    t = prs->prevtoken.type;
    if(t == NEON_ASTTOK_LITNUMREG || t == NEON_ASTTOK_LITNUMOCT || t == NEON_ASTTOK_LITNUMBIN || t == NEON_ASTTOK_LITNUMHEX)
    {
        return true;
    }
    return false;
}


bool nn_astparser_istype(NNAstTokType prev, NNAstTokType t)
{
    if(t == NEON_ASTTOK_IDENTNORMAL)
    {
        if(prev == NEON_ASTTOK_KWCLASS)
        {
            return true;
        }
    }
    return (prev == t);
}

static bool nn_astparser_check(NNAstParser* prs, NNAstTokType t)
{
    return nn_astparser_istype(prs->currtoken.type, t);
}

static bool nn_astparser_match(NNAstParser* prs, NNAstTokType t)
{
    if(!nn_astparser_check(prs, t))
    {
        return false;
    }
    nn_astparser_advance(prs);
    return true;
}

static void nn_astparser_runparser(NNAstParser* parser)
{
    nn_astparser_advance(parser);
    nn_astparser_ignorewhitespace(parser);
    while(!nn_astparser_match(parser, NEON_ASTTOK_EOF))
    {
        nn_astparser_parsedeclaration(parser);
    }
}

static void nn_astparser_parsedeclaration(NNAstParser* prs)
{
    nn_astparser_ignorewhitespace(prs);
    if(nn_astparser_match(prs, NEON_ASTTOK_KWCLASS))
    {
        nn_astparser_parseclassdeclaration(prs, true);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWFUNCTION))
    {
        nn_astparser_parsefuncdecl(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWVAR))
    {
        nn_astparser_parsevardecl(prs, false, false);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWCONST))
    {
        nn_astparser_parsevardecl(prs, false, true);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_BRACEOPEN))
    {
        if(!nn_astparser_check(prs, NEON_ASTTOK_NEWLINE) && prs->currentfunccompiler->scopedepth == 0)
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
    if(prs->panicmode)
    {
        nn_astparser_synchronize(prs);
    }
    nn_astparser_ignorewhitespace(prs);
}

static void nn_astparser_parsestmt(NNAstParser* prs)
{
    prs->replcanecho = false;
    nn_astparser_ignorewhitespace(prs);
    if(nn_astparser_match(prs, NEON_ASTTOK_KWECHO))
    {
        nn_astparser_parseechostmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWIF))
    {
        nn_astparser_parseifstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWDO))
    {
        nn_astparser_parsedo_whilestmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWWHILE))
    {
        nn_astparser_parsewhilestmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWFOR))
    {
        nn_astparser_parseforstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWFOREACH))
    {
        nn_astparser_parseforeachstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWSWITCH))
    {
        nn_astparser_parseswitchstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWCONTINUE))
    {
        nn_astparser_parsecontinuestmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWBREAK))
    {
        nn_astparser_parsebreakstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWRETURN))
    {
        nn_astparser_parsereturnstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWASSERT))
    {
        nn_astparser_parseassertstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWTHROW))
    {
        nn_astparser_parsethrowstmt(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_BRACEOPEN))
    {
        nn_astparser_scopebegin(prs);
        nn_astparser_parseblock(prs);
        nn_astparser_scopeend(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWTRY))
    {
        nn_astparser_parsetrystmt(prs);
    }
    else
    {
        nn_astparser_parseexprstmt(prs, false, false);
    }
    nn_astparser_ignorewhitespace(prs);
}

static void nn_astparser_consumestmtend(NNAstParser* prs)
{
    /* allow block last statement to omit statement end */
    if(prs->blockcount > 0 && nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE))
    {
        return;
    }
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
        while(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON) || nn_astparser_match(prs, NEON_ASTTOK_NEWLINE))
        {
        }
        return;
    }
    if(nn_astparser_match(prs, NEON_ASTTOK_EOF) || prs->prevtoken.type == NEON_ASTTOK_EOF)
    {
        return;
    }
    /* nn_astparser_consume(prs, NEON_ASTTOK_NEWLINE, "end of statement expected"); */
    while(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON) || nn_astparser_match(prs, NEON_ASTTOK_NEWLINE))
    {
    }
}

static void nn_astparser_ignorewhitespace(NNAstParser* prs)
{
    while(true)
    {
        if(nn_astparser_check(prs, NEON_ASTTOK_NEWLINE))
        {
            nn_astparser_advance(prs);
        }
        else
        {
            break;
        }
    }
}

static int nn_astparser_getcodeargscount(const NNInstruction* bytecode, const NNValue* constants, int ip)
{
    int constant;
    NNOpCode code;
    NNObjFunction* fn;
    code = (NNOpCode)bytecode[ip].code;
    switch(code)
    {
        case NEON_OP_EQUAL:
        case NEON_OP_PRIMGREATER:
        case NEON_OP_PRIMLESSTHAN:
        case NEON_OP_PUSHNULL:
        case NEON_OP_PUSHTRUE:
        case NEON_OP_PUSHFALSE:
        case NEON_OP_PRIMADD:
        case NEON_OP_PRIMSUBTRACT:
        case NEON_OP_PRIMMULTIPLY:
        case NEON_OP_PRIMDIVIDE:
        case NEON_OP_PRIMFLOORDIVIDE:
        case NEON_OP_PRIMMODULO:
        case NEON_OP_PRIMPOW:
        case NEON_OP_PRIMNEGATE:
        case NEON_OP_PRIMNOT:
        case NEON_OP_ECHO:
        case NEON_OP_TYPEOF:
        case NEON_OP_POPONE:
        case NEON_OP_UPVALUECLOSE:
        case NEON_OP_DUPONE:
        case NEON_OP_RETURN:
        case NEON_OP_CLASSINHERIT:
        case NEON_OP_CLASSGETSUPER:
        case NEON_OP_PRIMAND:
        case NEON_OP_PRIMOR:
        case NEON_OP_PRIMBITXOR:
        case NEON_OP_PRIMSHIFTLEFT:
        case NEON_OP_PRIMSHIFTRIGHT:
        case NEON_OP_PRIMBITNOT:
        case NEON_OP_PUSHONE:
        case NEON_OP_INDEXSET:
        case NEON_OP_ASSERT:
        case NEON_OP_EXTHROW:
        case NEON_OP_EXPOPTRY:
        case NEON_OP_MAKERANGE:
        case NEON_OP_STRINGIFY:
        case NEON_OP_PUSHEMPTY:
        case NEON_OP_EXPUBLISHTRY:
        case NEON_OP_CLASSGETTHIS:
        case NEON_OP_HALT:
            return 0;
        case NEON_OP_CALLFUNCTION:
        case NEON_OP_CLASSINVOKESUPERSELF:
        case NEON_OP_INDEXGET:
        case NEON_OP_INDEXGETRANGED:
            return 1;
        case NEON_OP_GLOBALDEFINE:
        case NEON_OP_GLOBALGET:
        case NEON_OP_GLOBALSET:
        case NEON_OP_LOCALGET:
        case NEON_OP_LOCALSET:
        case NEON_OP_FUNCARGOPTIONAL:
        case NEON_OP_FUNCARGSET:
        case NEON_OP_FUNCARGGET:
        case NEON_OP_UPVALUEGET:
        case NEON_OP_UPVALUESET:
        case NEON_OP_JUMPIFFALSE:
        case NEON_OP_JUMPNOW:
        case NEON_OP_BREAK_PL:
        case NEON_OP_LOOP:
        case NEON_OP_PUSHCONSTANT:
        case NEON_OP_POPN:
        case NEON_OP_MAKECLASS:
        case NEON_OP_PROPERTYGET:
        case NEON_OP_PROPERTYGETSELF:
        case NEON_OP_PROPERTYSET:
        case NEON_OP_MAKEARRAY:
        case NEON_OP_MAKEDICT:
        case NEON_OP_IMPORTIMPORT:
        case NEON_OP_SWITCH:
        case NEON_OP_MAKEMETHOD:
        #if 0
        case NEON_OP_FUNCOPTARG:
        #endif
            return 2;
        case NEON_OP_CALLMETHOD:
        case NEON_OP_CLASSINVOKETHIS:
        case NEON_OP_CLASSINVOKESUPER:
        case NEON_OP_CLASSPROPERTYDEFINE:
            return 3;
        case NEON_OP_EXTRY:
            return 6;
        case NEON_OP_MAKECLOSURE:
            {
                constant = (bytecode[ip + 1].code << 8) | bytecode[ip + 2].code;
                fn = nn_value_asfunction(constants[constant]);
                /* There is two byte for the constant, then three for each up value. */
                return 2 + (fn->upvalcount * 3);
            }
            break;
        default:
            break;
    }
    return 0;
}

static void nn_astemit_emit(NNAstParser* prs, uint8_t byte, int line, bool isop)
{
    NNInstruction ins;
    ins.code = byte;
    ins.fromsourceline = line;
    ins.isop = isop;
    nn_blob_push(nn_astparser_currentblob(prs), ins);
}

static void nn_astemit_patchat(NNAstParser* prs, size_t idx, uint8_t byte)
{
    nn_astparser_currentblob(prs)->instrucs[idx].code = byte;
}

static void nn_astemit_emitinstruc(NNAstParser* prs, uint8_t byte)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, true);
}

static void nn_astemit_emit1byte(NNAstParser* prs, uint8_t byte)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
}

static void nn_astemit_emit1short(NNAstParser* prs, uint16_t byte)
{
    nn_astemit_emit(prs, (byte >> 8) & 0xff, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte & 0xff, prs->prevtoken.line, false);
}

static void nn_astemit_emit2byte(NNAstParser* prs, uint8_t byte, uint8_t byte2)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte2, prs->prevtoken.line, false);
}

static void nn_astemit_emitbyteandshort(NNAstParser* prs, uint8_t byte, uint16_t byte2)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
    nn_astemit_emit(prs, (byte2 >> 8) & 0xff, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte2 & 0xff, prs->prevtoken.line, false);
}

static void nn_astemit_emitloop(NNAstParser* prs, int loopstart)
{
    int offset;
    nn_astemit_emitinstruc(prs, NEON_OP_LOOP);
    offset = nn_astparser_currentblob(prs)->count - loopstart + 2;
    if(offset > UINT16_MAX)
    {
        nn_astparser_raiseerror(prs, "loop body too large");
    }
    nn_astemit_emit1byte(prs, (offset >> 8) & 0xff);
    nn_astemit_emit1byte(prs, offset & 0xff);
}

static void nn_astemit_emitreturn(NNAstParser* prs)
{
    if(prs->istrying)
    {
        nn_astemit_emitinstruc(prs, NEON_OP_EXPOPTRY);
    }
    if(prs->currentfunccompiler->contexttype == NEON_FNCONTEXTTYPE_INITIALIZER)
    {
        nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, 0);
    }
    else
    {
        if(!prs->keeplastvalue || prs->lastwasstatement)
        {
            if(prs->currentfunccompiler->fromimport)
            {
                nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
            }
            else
            {
                nn_astemit_emitinstruc(prs, NEON_OP_PUSHEMPTY);
            }
        }
    }
    nn_astemit_emitinstruc(prs, NEON_OP_RETURN);
}

static int nn_astparser_pushconst(NNAstParser* prs, NNValue value)
{
    int constant;
    constant = nn_blob_pushconst(nn_astparser_currentblob(prs), value);
    return constant;
}

static void nn_astemit_emitconst(NNAstParser* prs, NNValue value)
{
    int constant;
    constant = nn_astparser_pushconst(prs, value);
    nn_astemit_emitbyteandshort(prs, NEON_OP_PUSHCONSTANT, (uint16_t)constant);
}

static int nn_astemit_emitjump(NNAstParser* prs, uint8_t instruction)
{
    nn_astemit_emitinstruc(prs, instruction);
    /* placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->count - 2;
}

static int nn_astemit_emitswitch(NNAstParser* prs)
{
    nn_astemit_emitinstruc(prs, NEON_OP_SWITCH);
    /* placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->count - 2;
}

static int nn_astemit_emittry(NNAstParser* prs)
{
    nn_astemit_emitinstruc(prs, NEON_OP_EXTRY);
    /* type placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    /* handler placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    /* finally placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->count - 6;
}

static void nn_astemit_patchswitch(NNAstParser* prs, int offset, int constant)
{
    nn_astemit_patchat(prs, offset, (constant >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 1, constant & 0xff);
}

static void nn_astemit_patchtry(NNAstParser* prs, int offset, int type, int address, int finally)
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

static void nn_astemit_patchjump(NNAstParser* prs, int offset)
{
    /* -2 to adjust the bytecode for the offset itself */
    int jump;
    jump = nn_astparser_currentblob(prs)->count - offset - 2;
    if(jump > UINT16_MAX)
    {
        nn_astparser_raiseerror(prs, "body of conditional block too large");
    }
    nn_astemit_patchat(prs, offset, (jump >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 1, jump & 0xff);
}

static void nn_astfunccompiler_init(NNAstParser* prs, NNAstFuncCompiler* fnc, NNFuncContextType type, bool isanon)
{
    bool candeclthis;
    NNIOStream wtmp;
    NNAstLocal* local;
    NNObjString* fname;
    fnc->enclosing = prs->currentfunccompiler;
    fnc->targetfunc = nullptr;
    fnc->contexttype = type;
    fnc->localcount = 0;
    fnc->scopedepth = 0;
    fnc->handlercount = 0;
    fnc->fromimport = false;
    fnc->targetfunc = nn_object_makefuncscript(prs->currentmodule, type);
    prs->currentfunccompiler = fnc;
    if(type != NEON_FNCONTEXTTYPE_SCRIPT)
    {
        nn_vm_stackpush(prs->pstate, nn_value_fromobject(fnc->targetfunc));
        if(isanon)
        {
            nn_iostream_makestackstring(&wtmp);
            nn_iostream_printf(&wtmp, "anonymous@[%s:%d]", prs->currentfile, prs->prevtoken.line);
            fname = nn_iostream_takestring(&wtmp);
            nn_iostream_destroy(&wtmp);
        }
        else
        {
            fname = nn_string_copylen(prs->prevtoken.start, prs->prevtoken.length);
        }
        prs->currentfunccompiler->targetfunc->name = fname;
        nn_vm_stackpop(prs->pstate);
    }
    /* claiming slot zero for use in class methods */
    local = &prs->currentfunccompiler->locals[0];
    prs->currentfunccompiler->localcount++;
    local->depth = 0;
    local->iscaptured = false;
    candeclthis = (
        (type != NEON_FNCONTEXTTYPE_FUNCTION) &&
        (prs->compcontext == NEON_COMPCONTEXT_CLASS)
    );
    if(candeclthis || (/*(type == NEON_FNCONTEXTTYPE_ANONYMOUS) &&*/ (prs->compcontext != NEON_COMPCONTEXT_CLASS)))
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

static int nn_astparser_makeidentconst(NNAstParser* prs, NNAstToken* name)
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
    return nn_astparser_pushconst(prs, nn_value_fromobject(str));
}

static bool nn_astparser_identsequal(NNAstToken* a, NNAstToken* b)
{
    return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
}

static int nn_astfunccompiler_resolvelocal(NNAstParser* prs, NNAstFuncCompiler* fnc, NNAstToken* name)
{
    int i;
    NNAstLocal* local;
    (void)prs;
    for(i = fnc->localcount - 1; i >= 0; i--)
    {
        local = &fnc->locals[i];
        if(nn_astparser_identsequal(&local->name, name))
        {
            #if 0
            if(local->depth == -1)
            {
                nn_astparser_raiseerror(prs, "cannot read local variable in it's own initializer");
            }
            #endif
            return i;
        }
    }
    return -1;
}

static int nn_astfunccompiler_addupvalue(NNAstParser* prs, NNAstFuncCompiler* fnc, uint16_t index, bool islocal)
{
    int i;
    int upcnt;
    NNAstUpvalue* upvalue;
    upcnt = fnc->targetfunc->upvalcount;
    for(i = 0; i < upcnt; i++)
    {
        upvalue = &fnc->upvalues[i];
        if(upvalue->index == index && upvalue->islocal == islocal)
        {
            return i;
        }
    }
    if(upcnt == NEON_CONFIG_ASTMAXUPVALS)
    {
        nn_astparser_raiseerror(prs, "too many closure variables in function");
        return 0;
    }
    fnc->upvalues[upcnt].islocal = islocal;
    fnc->upvalues[upcnt].index = index;
    return fnc->targetfunc->upvalcount++;
}

static int nn_astfunccompiler_resolveupvalue(NNAstParser* prs, NNAstFuncCompiler* fnc, NNAstToken* name)
{
    int local;
    int upvalue;
    if(fnc->enclosing == nullptr)
    {
        return -1;
    }
    local = nn_astfunccompiler_resolvelocal(prs, fnc->enclosing, name);
    if(local != -1)
    {
        fnc->enclosing->locals[local].iscaptured = true;
        return nn_astfunccompiler_addupvalue(prs, fnc, (uint16_t)local, true);
    }
    upvalue = nn_astfunccompiler_resolveupvalue(prs, fnc->enclosing, name);
    if(upvalue != -1)
    {
        return nn_astfunccompiler_addupvalue(prs, fnc, (uint16_t)upvalue, false);
    }
    return -1;
}

static int nn_astparser_addlocal(NNAstParser* prs, NNAstToken name)
{
    NNAstLocal* local;
    if(prs->currentfunccompiler->localcount == NEON_CONFIG_ASTMAXLOCALS)
    {
        /* we've reached maximum local variables per scope */
        nn_astparser_raiseerror(prs, "too many local variables in scope");
        return -1;
    }
    local = &prs->currentfunccompiler->locals[prs->currentfunccompiler->localcount++];
    local->name = name;
    local->depth = -1;
    local->iscaptured = false;
    return prs->currentfunccompiler->localcount;
}

static void nn_astparser_declarevariable(NNAstParser* prs)
{
    int i;
    NNAstToken* name;
    NNAstLocal* local;
    /* global variables are implicitly declared... */
    if(prs->currentfunccompiler->scopedepth == 0)
    {
        return;
    }
    name = &prs->prevtoken;
    for(i = prs->currentfunccompiler->localcount - 1; i >= 0; i--)
    {
        local = &prs->currentfunccompiler->locals[i];
        if(local->depth != -1 && local->depth < prs->currentfunccompiler->scopedepth)
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

static int nn_astparser_parsevariable(NNAstParser* prs, const char* message)
{
    if(!nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, message))
    {
        /* what to do here? */
    }
    nn_astparser_declarevariable(prs);
    /* we are in a local scope... */
    if(prs->currentfunccompiler->scopedepth > 0)
    {
        return 0;
    }
    return nn_astparser_makeidentconst(prs, &prs->prevtoken);
}

static void nn_astparser_markinitialized(NNAstParser* prs)
{
    if(prs->currentfunccompiler->scopedepth == 0)
    {
        return;
    }
    prs->currentfunccompiler->locals[prs->currentfunccompiler->localcount - 1].depth = prs->currentfunccompiler->scopedepth;
}

static void nn_astparser_definevariable(NNAstParser* prs, int global)
{
    /* we are in a local scope... */
    if(prs->currentfunccompiler->scopedepth > 0)
    {
        nn_astparser_markinitialized(prs);
        return;
    }
    nn_astemit_emitbyteandshort(prs, NEON_OP_GLOBALDEFINE, global);
}

static NNAstToken nn_astparser_synthtoken(const char* name)
{
    NNAstToken token;
    token.isglobal = false;
    token.line = 0;
    token.type = (NNAstTokType)0;
    token.start = name;
    token.length = (int)strlen(name);
    return token;
}

static NNObjFunction* nn_astparser_endcompiler(NNAstParser* prs, bool istoplevel)
{
    const char* fname;
    NNObjFunction* function;
    nn_astemit_emitreturn(prs);
    if(istoplevel)
    {
    }
    function = prs->currentfunccompiler->targetfunc;
    fname = nullptr;
    if(function->name == nullptr)
    {
        fname = nn_string_getdata(prs->currentmodule->physicalpath);
    }
    else
    {
        fname = nn_string_getdata(function->name);
    }
    if(!prs->haderror && prs->pstate->conf.dumpbytecode)
    {
        nn_dbg_disasmblob(prs->pstate->debugwriter, nn_astparser_currentblob(prs), fname);
    }
    prs->currentfunccompiler = prs->currentfunccompiler->enclosing;
    return function;
}

static void nn_astparser_scopebegin(NNAstParser* prs)
{
    prs->currentfunccompiler->scopedepth++;
}

static bool nn_astutil_scopeendcancontinue(NNAstParser* prs)
{
    int lopos;
    int locount;
    int lodepth;
    int scodepth;
    locount = prs->currentfunccompiler->localcount;
    lopos = prs->currentfunccompiler->localcount - 1;
    lodepth = prs->currentfunccompiler->locals[lopos].depth;
    scodepth = prs->currentfunccompiler->scopedepth;
    if(locount > 0 && lodepth > scodepth)
    {
        return true;
    }
    return false;
}

static void nn_astparser_scopeend(NNAstParser* prs)
{
    prs->currentfunccompiler->scopedepth--;
    /*
    // remove all variables declared in scope while exiting...
    */
    if(prs->keeplastvalue)
    {
        #if 0
            return;
        #endif
    }
    while(nn_astutil_scopeendcancontinue(prs))
    {
        if(prs->currentfunccompiler->locals[prs->currentfunccompiler->localcount - 1].iscaptured)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_UPVALUECLOSE);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        }
        prs->currentfunccompiler->localcount--;
    }
}

static int nn_astparser_discardlocals(NNAstParser* prs, int depth)
{
    int local;
    if(prs->keeplastvalue)
    {
        #if 0
            return 0;
        #endif
    }
    if(prs->currentfunccompiler->scopedepth == -1)
    {
        nn_astparser_raiseerror(prs, "cannot exit top-level scope");
    }
    local = prs->currentfunccompiler->localcount - 1;
    while(local >= 0 && prs->currentfunccompiler->locals[local].depth >= depth)
    {
        if(prs->currentfunccompiler->locals[local].iscaptured)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_UPVALUECLOSE);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        }
        local--;
    }
    return prs->currentfunccompiler->localcount - local - 1;
}

static void nn_astparser_endloop(NNAstParser* prs)
{
    int i;
    NNInstruction* bcode;
    NNValue* cvals;
    /*
    // find all NEON_OP_BREAK_PL placeholder and replace with the appropriate jump...
    */
    i = prs->innermostloopstart;
    while(i < prs->currentfunccompiler->targetfunc->fnscriptfunc.blob.count)
    {
        if(prs->currentfunccompiler->targetfunc->fnscriptfunc.blob.instrucs[i].code == NEON_OP_BREAK_PL)
        {
            prs->currentfunccompiler->targetfunc->fnscriptfunc.blob.instrucs[i].code = NEON_OP_JUMPNOW;
            nn_astemit_patchjump(prs, i + 1);
            i += 3;
        }
        else
        {
            bcode = prs->currentfunccompiler->targetfunc->fnscriptfunc.blob.instrucs;
            cvals = (NNValue*)prs->currentfunccompiler->targetfunc->fnscriptfunc.blob.constants.listitems;
            i += 1 + nn_astparser_getcodeargscount(bcode, cvals, i);
        }
    }
}

static bool nn_astparser_rulebinary(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    NNAstTokType op;
    NNAstRule* rule;
    (void)previous;
    (void)canassign;
    op = prs->prevtoken.type;
    /* compile the right operand */
    rule = nn_astparser_getrule(op);
    nn_astparser_parseprecedence(prs, (NNAstPrecedence)(rule->precedence + 1));
    /* emit the operator instruction */
    switch(op)
    {
        case NEON_ASTTOK_PLUS:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMADD);
            break;
        case NEON_ASTTOK_MINUS:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMSUBTRACT);
            break;
        case NEON_ASTTOK_MULTIPLY:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMMULTIPLY);
            break;
        case NEON_ASTTOK_DIVIDE:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMDIVIDE);
            break;
        case NEON_ASTTOK_MODULO:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMMODULO);
            break;
        case NEON_ASTTOK_POWEROF:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMPOW);
            break;
        case NEON_ASTTOK_FLOOR:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMFLOORDIVIDE);
            break;
            /* equality */
        case NEON_ASTTOK_EQUAL:
            nn_astemit_emitinstruc(prs, NEON_OP_EQUAL);
            break;
        case NEON_ASTTOK_NOTEQUAL:
            nn_astemit_emit2byte(prs, NEON_OP_EQUAL, NEON_OP_PRIMNOT);
            break;
        case NEON_ASTTOK_GREATERTHAN:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMGREATER);
            break;
        case NEON_ASTTOK_GREATER_EQ:
            nn_astemit_emit2byte(prs, NEON_OP_PRIMLESSTHAN, NEON_OP_PRIMNOT);
            break;
        case NEON_ASTTOK_LESSTHAN:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMLESSTHAN);
            break;
        case NEON_ASTTOK_LESSEQUAL:
            nn_astemit_emit2byte(prs, NEON_OP_PRIMGREATER, NEON_OP_PRIMNOT);
            break;
            /* bitwise */
        case NEON_ASTTOK_AMP:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMAND);
            break;
        case NEON_ASTTOK_BAR:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMOR);
            break;
        case NEON_ASTTOK_XOR:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMBITXOR);
            break;
        case NEON_ASTTOK_LEFTSHIFT:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMSHIFTLEFT);
            break;
        case NEON_ASTTOK_RIGHTSHIFT:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMSHIFTRIGHT);
            break;
            /* range */
        case NEON_ASTTOK_DOUBLEDOT:
            nn_astemit_emitinstruc(prs, NEON_OP_MAKERANGE);
            break;
        default:
            break;
    }
    return true;
}

static bool nn_astparser_rulecall(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    uint8_t argcount;
    (void)previous;
    (void)canassign;
    argcount = nn_astparser_parsefunccallargs(prs);
    nn_astemit_emit2byte(prs, NEON_OP_CALLFUNCTION, argcount);
    return true;
}

static bool nn_astparser_ruleliteral(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    switch(prs->prevtoken.type)
    {
        case NEON_ASTTOK_KWNULL:
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
            break;
        case NEON_ASTTOK_KWTRUE:
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHTRUE);
            break;
        case NEON_ASTTOK_KWFALSE:
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHFALSE);
            break;
        default:
            /* TODO: assuming this is correct behaviour ... */
            return false;
    }
    return true;
}

static void nn_astparser_parseassign(NNAstParser* prs, uint8_t realop, uint8_t getop, uint8_t setop, int arg)
{
    prs->replcanecho = false;
    if(getop == NEON_OP_PROPERTYGET || getop == NEON_OP_PROPERTYGETSELF)
    {
        nn_astemit_emitinstruc(prs, NEON_OP_DUPONE);
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

static void nn_astparser_assignment(NNAstParser* prs, uint8_t getop, uint8_t setop, int arg, bool canassign)
{
    if(canassign && nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
    {
        prs->replcanecho = false;
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
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_PLUSASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMADD, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_MINUSASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMSUBTRACT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_MULTASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMMULTIPLY, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_DIVASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMDIVIDE, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_POWASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMPOW, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_PERCENT_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMMODULO, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_AMP_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMAND, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_BAR_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMOR, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_TILDE_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMBITNOT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_XOR_EQ))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMBITXOR, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_LEFTSHIFTASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMSHIFTLEFT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_RIGHTSHIFTASSIGN))
    {
        nn_astparser_parseassign(prs, NEON_OP_PRIMSHIFTRIGHT, getop, setop, arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_INCREMENT))
    {
        prs->replcanecho = false;
        if(getop == NEON_OP_PROPERTYGET || getop == NEON_OP_PROPERTYGETSELF)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_DUPONE);
        }
        if(arg != -1)
        {
            nn_astemit_emitbyteandshort(prs, getop, arg);
        }
        else
        {
            nn_astemit_emit2byte(prs, getop, 1);
        }
        nn_astemit_emit2byte(prs, NEON_OP_PUSHONE, NEON_OP_PRIMADD);
        nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
    }
    else if(canassign && nn_astparser_match(prs, NEON_ASTTOK_DECREMENT))
    {
        prs->replcanecho = false;
        if(getop == NEON_OP_PROPERTYGET || getop == NEON_OP_PROPERTYGETSELF)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_DUPONE);
        }

        if(arg != -1)
        {
            nn_astemit_emitbyteandshort(prs, getop, arg);
        }
        else
        {
            nn_astemit_emit2byte(prs, getop, 1);
        }

        nn_astemit_emit2byte(prs, NEON_OP_PUSHONE, NEON_OP_PRIMSUBTRACT);
        nn_astemit_emitbyteandshort(prs, setop, (uint16_t)arg);
    }
    else
    {
        if(arg != -1)
        {
            if(getop == NEON_OP_INDEXGET || getop == NEON_OP_INDEXGETRANGED)
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

static bool nn_astparser_ruledot(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    int name;
    bool caninvoke;
    uint8_t argcount;
    NNOpCode getop;
    NNOpCode setop;
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected property name after '.'"))
    {
        return false;
    }
    name = nn_astparser_makeidentconst(prs, &prs->prevtoken);
    if(nn_astparser_match(prs, NEON_ASTTOK_PARENOPEN))
    {
        argcount = nn_astparser_parsefunccallargs(prs);
        caninvoke = (
            (prs->currentclasscompiler != nullptr) &&
            (
                (previous.type == NEON_ASTTOK_KWTHIS) ||
                (nn_astparser_identsequal(&prs->prevtoken, &prs->currentclasscompiler->name))
            )
        );
        if(caninvoke)
        {
            nn_astemit_emitbyteandshort(prs, NEON_OP_CLASSINVOKETHIS, name);
        }
        else
        {
            nn_astemit_emitbyteandshort(prs, NEON_OP_CALLMETHOD, name);
        }
        nn_astemit_emit1byte(prs, argcount);
    }
    else
    {
        getop = NEON_OP_PROPERTYGET;
        setop = NEON_OP_PROPERTYSET;
        if(prs->currentclasscompiler != nullptr && (previous.type == NEON_ASTTOK_KWTHIS || nn_astparser_identsequal(&prs->prevtoken, &prs->currentclasscompiler->name)))
        {
            getop = NEON_OP_PROPERTYGETSELF;
        }
        nn_astparser_assignment(prs, getop, setop, name, canassign);
    }
    return true;
}

static void nn_astparser_namedvar(NNAstParser* prs, NNAstToken name, bool canassign)
{
    bool fromclass;
    uint8_t getop;
    uint8_t setop;
    int arg;
    (void)fromclass;
    fromclass = prs->currentclasscompiler != nullptr;
    arg = nn_astfunccompiler_resolvelocal(prs, prs->currentfunccompiler, &name);
    if(arg != -1)
    {
        if(prs->infunction)
        {
            getop = NEON_OP_FUNCARGGET;
            setop = NEON_OP_FUNCARGSET;
        }
        else
        {
            getop = NEON_OP_LOCALGET;
            setop = NEON_OP_LOCALSET;
        }
    }
    else
    {
        arg = nn_astfunccompiler_resolveupvalue(prs, prs->currentfunccompiler, &name);
        if((arg != -1) && (name.isglobal == false))
        {
            getop = NEON_OP_UPVALUEGET;
            setop = NEON_OP_UPVALUESET;
        }
        else
        {
            arg = nn_astparser_makeidentconst(prs, &name);
            getop = NEON_OP_GLOBALGET;
            setop = NEON_OP_GLOBALSET;
        }
    }
    nn_astparser_assignment(prs, getop, setop, arg, canassign);
}

static void nn_astparser_createdvar(NNAstParser* prs, NNAstToken name)
{
    int local;
    if(prs->currentfunccompiler->targetfunc->name != nullptr)
    {
        local = nn_astparser_addlocal(prs, name) - 1;
        nn_astparser_markinitialized(prs);
        nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALSET, (uint16_t)local);
    }
    else
    {
        nn_astemit_emitbyteandshort(prs, NEON_OP_GLOBALDEFINE, (uint16_t)nn_astparser_makeidentconst(prs, &name));
    }
}

static bool nn_astparser_rulearray(NNAstParser* prs, bool canassign)
{
    int count;
    (void)canassign;
    /* placeholder for the list */
    nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    count = 0;
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_check(prs, NEON_ASTTOK_BRACKETCLOSE))
    {
        do
        {
            nn_astparser_ignorewhitespace(prs);
            if(!nn_astparser_check(prs, NEON_ASTTOK_BRACKETCLOSE))
            {
                /* allow comma to end lists */
                nn_astparser_parseexpression(prs);
                nn_astparser_ignorewhitespace(prs);
                count++;
            }
            nn_astparser_ignorewhitespace(prs);
        } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACKETCLOSE, "expected ']' at end of list");
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKEARRAY, count);
    return true;
}

static bool nn_astparser_ruledictionary(NNAstParser* prs, bool canassign)
{
    bool usedexpression;
    int itemcount;
    (void)canassign;
    /* placeholder for the dictionary */
    nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    itemcount = 0;
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE))
    {
        do
        {
            nn_astparser_ignorewhitespace(prs);
            if(!nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE))
            {
                /* allow last pair to end with a comma */
                usedexpression = false;
                if(nn_astparser_check(prs, NEON_ASTTOK_IDENTNORMAL))
                {
                    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "");
                    nn_astemit_emitconst(prs, nn_value_fromobject(nn_string_copylen(prs->prevtoken.start, prs->prevtoken.length)));
                }
                else
                {
                    nn_astparser_parseexpression(prs);
                    usedexpression = true;
                }
                nn_astparser_ignorewhitespace(prs);
                if(!nn_astparser_check(prs, NEON_ASTTOK_COMMA) && !nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE))
                {
                    nn_astparser_consume(prs, NEON_ASTTOK_COLON, "expected ':' after dictionary key");
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
                        nn_astparser_namedvar(prs, prs->prevtoken, false);
                    }
                }
                itemcount++;
            }
        } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACECLOSE, "expected '}' after dictionary");
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKEDICT, itemcount);
    return true;
}

static bool nn_astparser_ruleindexing(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    bool assignable;
    bool commamatch;
    uint8_t getop;
    (void)previous;
    (void)canassign;
    assignable = true;
    commamatch = false;
    getop = NEON_OP_INDEXGET;
    if(nn_astparser_match(prs, NEON_ASTTOK_COMMA))
    {
        nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
        commamatch = true;
        getop = NEON_OP_INDEXGETRANGED;
    }
    else
    {
        nn_astparser_parseexpression(prs);
    }
    if(!nn_astparser_match(prs, NEON_ASTTOK_BRACKETCLOSE))
    {
        getop = NEON_OP_INDEXGETRANGED;
        if(!commamatch)
        {
            nn_astparser_consume(prs, NEON_ASTTOK_COMMA, "expecting ',' or ']'");
        }
        if(nn_astparser_match(prs, NEON_ASTTOK_BRACKETCLOSE))
        {
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
        }
        else
        {
            nn_astparser_parseexpression(prs);
            nn_astparser_consume(prs, NEON_ASTTOK_BRACKETCLOSE, "expected ']' after indexing");
        }
        assignable = false;
    }
    else
    {
        if(commamatch)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
        }
    }
    nn_astparser_assignment(prs, getop, NEON_OP_INDEXSET, -1, assignable);
    return true;
}

static bool nn_astparser_rulevarnormal(NNAstParser* prs, bool canassign)
{
    nn_astparser_namedvar(prs, prs->prevtoken, canassign);
    return true;
}


static bool nn_astparser_rulethis(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    #if 0
    if(prs->currentclasscompiler == nullptr)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'this' outside of a class");
        return false;
    }
    #endif
    #if 0
    if(prs->currentclasscompiler != nullptr)
    #endif
    {
        nn_astparser_namedvar(prs, prs->prevtoken, false);
        #if 0
            nn_astparser_namedvar(prs, nn_astparser_synthtoken(g_strthis), false);
        #endif
    }
    #if 0
        nn_astemit_emitinstruc(prs, NEON_OP_CLASSGETTHIS);
    #endif
    return true;
}

static bool nn_astparser_rulesuper(NNAstParser* prs, bool canassign)
{
    int name;
    bool invokeself;
    uint8_t argcount;
    (void)canassign;
    if(prs->currentclasscompiler == nullptr)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'super' outside of a class");
        return false;
    }
    else if(!prs->currentclasscompiler->hassuperclass)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'super' in a class without a superclass");
        return false;
    }
    name = -1;
    invokeself = false;
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENOPEN))
    {
        nn_astparser_consume(prs, NEON_ASTTOK_DOT, "expected '.' or '(' after super");
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected super class method name after .");
        name = nn_astparser_makeidentconst(prs, &prs->prevtoken);
    }
    else
    {
        invokeself = true;
    }
    nn_astparser_namedvar(prs, nn_astparser_synthtoken(g_strthis), false);
    if(nn_astparser_match(prs, NEON_ASTTOK_PARENOPEN))
    {
        argcount = nn_astparser_parsefunccallargs(prs);
        nn_astparser_namedvar(prs, nn_astparser_synthtoken(g_strsuper), false);
        if(!invokeself)
        {
            nn_astemit_emitbyteandshort(prs, NEON_OP_CLASSINVOKESUPER, name);
            nn_astemit_emit1byte(prs, argcount);
        }
        else
        {
            nn_astemit_emit2byte(prs, NEON_OP_CLASSINVOKESUPERSELF, argcount);
        }
    }
    else
    {
        nn_astparser_namedvar(prs, nn_astparser_synthtoken(g_strsuper), false);
        nn_astemit_emitbyteandshort(prs, NEON_OP_CLASSGETSUPER, name);
    }
    return true;
}

static bool nn_astparser_rulegrouping(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_parseexpression(prs);
    while(nn_astparser_match(prs, NEON_ASTTOK_COMMA))
    {
        nn_astparser_parseexpression(prs);
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after grouped expression");
    return true;
}

NNValue nn_astparser_compilestrnumber(NNAstTokType type, const char* source)
{
    double dbval;
    long longval;
    int64_t llval;
    if(type == NEON_ASTTOK_LITNUMBIN)
    {
        llval = strtoll(source + 2, nullptr, 2);
        return nn_value_makenumber(llval);
    }
    else if(type == NEON_ASTTOK_LITNUMOCT)
    {
        longval = strtol(source + 2, nullptr, 8);
        return nn_value_makenumber(longval);
    }
    else if(type == NEON_ASTTOK_LITNUMHEX)
    {
        longval = strtol(source, nullptr, 16);
        return nn_value_makenumber(longval);
    }
    dbval = strtod(source, nullptr);
    return nn_value_makenumber(dbval);
}

static NNValue nn_astparser_compilenumber(NNAstParser* prs)
{
    return nn_astparser_compilestrnumber(prs->prevtoken.type, prs->prevtoken.start);
}

static bool nn_astparser_rulenumber(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    nn_astemit_emitconst(prs, nn_astparser_compilenumber(prs));
    return true;
}

/*
// Reads the next character, which should be a hex digit (0-9, a-f, or A-F) and
// returns its numeric value. If the character isn't a hex digit, returns -1.
*/
static int nn_astparser_readhexdigit(char c)
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
static int nn_astparser_readhexescape(NNAstParser* prs, const char* str, int index, int count)
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

static int nn_astparser_readunicodeescape(NNAstParser* prs, char* string, const char* realstring, int numberbytes, int realindex, int index)
{
    int value;
    int count;
    size_t len;
    char* chr;
    value = nn_astparser_readhexescape(prs, realstring, realindex, numberbytes);
    count = nn_util_utf8numbytes(value);
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
        chr = nn_util_utf8encode(value, &len);
        if(chr)
        {
            memcpy(string + index, chr, (size_t)count + 1);
            nn_memory_free(chr);
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

static char* nn_astparser_compilestring(NNAstParser* prs, int* length, bool permitescapes)
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
    rawlen = (((size_t)prs->prevtoken.length - 2) + 1);
    deststr = (char*)nn_memory_malloc(sizeof(char) * rawlen);
    quote = prs->prevtoken.start[0];
    realstr = (char*)prs->prevtoken.start + 1;
    reallength = prs->prevtoken.length - 2;
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
                                int nn_astparser_readunicodeescape(NNAstParser* prs, char* string, char* realstring, int numberbytes, int realindex, int index)
                                int nn_astparser_readhexescape(NNAstParser* prs, const char* str, int index, int count)
                                k += nn_astparser_readunicodeescape(prs, deststr, realstr, 2, i, k) - 1;
                                k += nn_astparser_readhexescape(prs, deststr, i, 2) - 0;
                            #endif
                            c = nn_astparser_readhexescape(prs, realstr, i, 2) - 0;
                            i += 2;
                            #if 0
                                continue;
                            #endif
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
        }
        memcpy(deststr + k, &c, 1);
    }
    *length = k;
    deststr[k] = '\0';
    return deststr;
}

static bool nn_astparser_rulestring(NNAstParser* prs, bool canassign)
{
    int length;
    char* str;
    (void)canassign;
    str = nn_astparser_compilestring(prs, &length, true);
    nn_astemit_emitconst(prs, nn_value_fromobject(nn_string_takelen(str, length)));
    return true;
}

static bool nn_astparser_rulerawstring(NNAstParser* prs, bool canassign)
{
    int length;
    char* str;
    (void)canassign;
    str = nn_astparser_compilestring(prs, &length, false);
    nn_astemit_emitconst(prs, nn_value_fromobject(nn_string_takelen(str, length)));
    return true;
}

static bool nn_astparser_ruleinterpolstring(NNAstParser* prs, bool canassign)
{
    int count;
    bool doadd;
    bool stringmatched;
    count = 0;
    do
    {
        doadd = false;
        stringmatched = false;
        if(prs->prevtoken.length - 2 > 0)
        {
            nn_astparser_rulestring(prs, canassign);
            doadd = true;
            stringmatched = true;
            if(count > 0)
            {
                nn_astemit_emitinstruc(prs, NEON_OP_PRIMADD);
            }
        }
        nn_astparser_parseexpression(prs);
        nn_astemit_emitinstruc(prs, NEON_OP_STRINGIFY);
        if(doadd || (count >= 1 && stringmatched == false))
        {
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMADD);
        }
        count++;
    } while(nn_astparser_match(prs, NEON_ASTTOK_INTERPOLATION));
    nn_astparser_consume(prs, NEON_ASTTOK_LITERALSTRING, "unterminated string interpolation");
    if(prs->prevtoken.length - 2 > 0)
    {
        nn_astparser_rulestring(prs, canassign);
        nn_astemit_emitinstruc(prs, NEON_OP_PRIMADD);
    }
    return true;
}

static bool nn_astparser_ruleunary(NNAstParser* prs, bool canassign)
{
    NNAstTokType op;
    (void)canassign;
    op = prs->prevtoken.type;
    /* compile the expression */
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_UNARY);
    /* emit instruction */
    switch(op)
    {
        case NEON_ASTTOK_MINUS:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMNEGATE);
            break;
        case NEON_ASTTOK_EXCLMARK:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMNOT);
            break;
        case NEON_ASTTOK_TILDE:
            nn_astemit_emitinstruc(prs, NEON_OP_PRIMBITNOT);
            break;
        default:
            break;
    }
    return true;
}

static bool nn_astparser_ruleand(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    int endjump;
    (void)previous;
    (void)canassign;
    endjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_AND);
    nn_astemit_patchjump(prs, endjump);
    return true;
}


static bool nn_astparser_ruleor(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    int endjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    elsejump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    endjump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    nn_astemit_patchjump(prs, elsejump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_OR);
    nn_astemit_patchjump(prs, endjump);
    return true;
}

static bool nn_astparser_ruleinstanceof(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    (void)previous;
    (void)canassign;
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_OPINSTANCEOF);

    return true;
}

static bool nn_astparser_ruleconditional(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    int thenjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    thenjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_ignorewhitespace(prs);
    /* compile the then expression */
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_CONDITIONAL);
    nn_astparser_ignorewhitespace(prs);
    elsejump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    nn_astemit_patchjump(prs, thenjump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_consume(prs, NEON_ASTTOK_COLON, "expected matching ':' after '?' conditional");
    nn_astparser_ignorewhitespace(prs);
    /*
    // compile the else expression
    // here we parse at NEON_ASTPREC_ASSIGNMENT precedence as
    // linear conditionals can be nested.
    */
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_ASSIGNMENT);
    nn_astemit_patchjump(prs, elsejump);
    return true;
}

static bool nn_astparser_ruleimport(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_IMPORTIMPORT);
    return true;
}

static bool nn_astparser_rulenew(NNAstParser* prs, bool canassign)
{
    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "class name after 'new'");
    return nn_astparser_rulevarnormal(prs, canassign);
}

static bool nn_astparser_ruletypeof(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'typeof'");
    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'typeof'");
    nn_astemit_emitinstruc(prs, NEON_OP_TYPEOF);
    return true;
}

static bool nn_astparser_rulenothingprefix(NNAstParser* prs, bool canassign)
{
    (void)prs;
    (void)canassign;
    return true;
}

static bool nn_astparser_rulenothinginfix(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    (void)prs;
    (void)previous;
    (void)canassign;
    return true;
}

static NNAstRule* nn_astparser_putrule(NNAstRule* dest, NNAstParsePrefixFN prefix, NNAstParseInfixFN infix, NNAstPrecedence precedence)
{
    dest->prefix = prefix;
    dest->infix = infix;
    dest->precedence = precedence;
    return dest;
}

#define dorule(tok, prefix, infix, precedence) \
    case tok: return nn_astparser_putrule(&dest, prefix, infix, precedence);

NNAstRule* nn_astparser_getrule(NNAstTokType type)
{
    static NNAstRule dest;
    switch(type)
    {
        dorule(NEON_ASTTOK_NEWLINE, nn_astparser_rulenothingprefix, nn_astparser_rulenothinginfix, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_PARENOPEN, nn_astparser_rulegrouping, nn_astparser_rulecall, NEON_ASTPREC_CALL );
        dorule(NEON_ASTTOK_PARENCLOSE, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BRACKETOPEN, nn_astparser_rulearray, nn_astparser_ruleindexing, NEON_ASTPREC_CALL );
        dorule(NEON_ASTTOK_BRACKETCLOSE, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BRACEOPEN, nn_astparser_ruledictionary, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BRACECLOSE, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_SEMICOLON, nn_astparser_rulenothingprefix, nn_astparser_rulenothinginfix, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_COMMA, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BACKSLASH, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_EXCLMARK, nn_astparser_ruleunary, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_NOTEQUAL, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_EQUALITY );
        dorule(NEON_ASTTOK_COLON, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_AT, nn_astparser_ruleanonfunc, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_DOT, nullptr, nn_astparser_ruledot, NEON_ASTPREC_CALL );
        dorule(NEON_ASTTOK_DOUBLEDOT, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_RANGE );
        dorule(NEON_ASTTOK_TRIPLEDOT, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_PLUS, nn_astparser_ruleunary, nn_astparser_rulebinary, NEON_ASTPREC_TERM );
        dorule(NEON_ASTTOK_PLUSASSIGN, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_INCREMENT, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_MINUS, nn_astparser_ruleunary, nn_astparser_rulebinary, NEON_ASTPREC_TERM );
        dorule(NEON_ASTTOK_MINUSASSIGN, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_DECREMENT, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_MULTIPLY, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_MULTASSIGN, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_POWEROF, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_POWASSIGN, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_DIVIDE, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_DIVASSIGN, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_FLOOR, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_ASSIGN, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_EQUAL, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_EQUALITY );
        dorule(NEON_ASTTOK_LESSTHAN, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_LESSEQUAL, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_LEFTSHIFT, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_SHIFT );
        dorule(NEON_ASTTOK_LEFTSHIFTASSIGN, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_GREATERTHAN, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_GREATER_EQ, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_RIGHTSHIFT, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_SHIFT );
        dorule(NEON_ASTTOK_RIGHTSHIFTASSIGN, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_MODULO, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_PERCENT_EQ, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_AMP, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_BITAND );
        dorule(NEON_ASTTOK_AMP_EQ, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BAR, /*nn_astparser_ruleanoncompat*/ nullptr, nn_astparser_rulebinary, NEON_ASTPREC_BITOR );
        dorule(NEON_ASTTOK_BAR_EQ, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_TILDE, nn_astparser_ruleunary, nullptr, NEON_ASTPREC_UNARY );
        dorule(NEON_ASTTOK_TILDE_EQ, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_XOR, nullptr, nn_astparser_rulebinary, NEON_ASTPREC_BITXOR );
        dorule(NEON_ASTTOK_XOR_EQ, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_QUESTION, nullptr, nn_astparser_ruleconditional, NEON_ASTPREC_CONDITIONAL );
        dorule(NEON_ASTTOK_KWAND, nullptr, nn_astparser_ruleand, NEON_ASTPREC_AND );
        dorule(NEON_ASTTOK_KWAS, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWASSERT, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWBREAK, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCLASS, nn_astparser_ruleanonclass, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCONTINUE, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFUNCTION, nn_astparser_ruleanonfunc, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWDEFAULT, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTHROW, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWDO, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWECHO, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWELSE, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFALSE, nn_astparser_ruleliteral, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFOREACH, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWIF, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWIMPORT, nn_astparser_ruleimport, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWIN, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWINSTANCEOF, nullptr, nn_astparser_ruleinstanceof, NEON_ASTPREC_OR );
        dorule(NEON_ASTTOK_KWFOR, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWVAR, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWNULL, nn_astparser_ruleliteral, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWNEW, nn_astparser_rulenew, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTYPEOF, nn_astparser_ruletypeof, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWOR, nullptr, nn_astparser_ruleor, NEON_ASTPREC_OR );
        dorule(NEON_ASTTOK_KWSUPER, nn_astparser_rulesuper, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWRETURN, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTHIS, nn_astparser_rulethis, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWSTATIC, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTRUE, nn_astparser_ruleliteral, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWSWITCH, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCASE, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWWHILE, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTRY, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCATCH, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFINALLY, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITERALSTRING, nn_astparser_rulestring, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITERALRAWSTRING, nn_astparser_rulerawstring, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMREG, nn_astparser_rulenumber, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMBIN, nn_astparser_rulenumber, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMOCT, nn_astparser_rulenumber, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMHEX, nn_astparser_rulenumber, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_IDENTNORMAL, nn_astparser_rulevarnormal, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_INTERPOLATION, nn_astparser_ruleinterpolstring, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_EOF, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_ERROR, nullptr, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWEMPTY, nn_astparser_ruleliteral, nullptr, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_UNDEFINED, nullptr, nullptr, NEON_ASTPREC_NONE );
        default:
            fprintf(stderr, "missing rule?\n");
            break;
    }
    return nullptr;
}
#undef dorule

static bool nn_astparser_doparseprecedence(NNAstParser* prs, NNAstPrecedence precedence/*, NNAstExpression* dest*/)
{
    bool canassign;
    NNAstRule* rule;
    NNAstToken previous;
    NNAstParseInfixFN infixrule;
    NNAstParsePrefixFN prefixrule;
    rule = nn_astparser_getrule(prs->prevtoken.type);
    if(rule == nullptr)
    {
        return false;
    }
    prefixrule = rule->prefix;
    if(prefixrule == nullptr)
    {
        nn_astparser_raiseerror(prs, "expected expression");
        return false;
    }
    canassign = precedence <= NEON_ASTPREC_ASSIGNMENT;
    prefixrule(prs, canassign);
    while(true)
    {
        rule = nn_astparser_getrule(prs->currtoken.type);
        if(rule == nullptr)
        {
            return false;
        }
        if(precedence <= rule->precedence)
        {
            previous = prs->prevtoken;
            nn_astparser_ignorewhitespace(prs);
            nn_astparser_advance(prs);
            infixrule = nn_astparser_getrule(prs->prevtoken.type)->infix;
            infixrule(prs, previous, canassign);
        }
        else
        {
            break;
        }
    }
    if(canassign && nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
    {
        nn_astparser_raiseerror(prs, "invalid assignment target");
        return false;
    }
    return true;
}

static bool nn_astparser_parseprecedence(NNAstParser* prs, NNAstPrecedence precedence)
{
    if(nn_astlex_isatend(prs->lexer) && prs->pstate->isrepl)
    {
        return false;
    }
    nn_astparser_ignorewhitespace(prs);
    if(nn_astlex_isatend(prs->lexer) && prs->pstate->isrepl)
    {
        return false;
    }
    nn_astparser_advance(prs);
    return nn_astparser_doparseprecedence(prs, precedence);
}

static bool nn_astparser_parseprecnoadvance(NNAstParser* prs, NNAstPrecedence precedence)
{
    if(nn_astlex_isatend(prs->lexer) && prs->pstate->isrepl)
    {
        return false;
    }
    nn_astparser_ignorewhitespace(prs);
    if(nn_astlex_isatend(prs->lexer) && prs->pstate->isrepl)
    {
        return false;
    }
    return nn_astparser_doparseprecedence(prs, precedence);
}

static bool nn_astparser_parseexpression(NNAstParser* prs)
{
    return nn_astparser_parseprecedence(prs, NEON_ASTPREC_ASSIGNMENT);
}

static bool nn_astparser_parseblock(NNAstParser* prs)
{
    prs->blockcount++;
    nn_astparser_ignorewhitespace(prs);
    while(!nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE) && !nn_astparser_check(prs, NEON_ASTTOK_EOF))
    {
        nn_astparser_parsedeclaration(prs);
    }
    prs->blockcount--;
    if(!nn_astparser_consume(prs, NEON_ASTTOK_BRACECLOSE, "expected '}' after block"))
    {
        return false;
    }
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
    }
    return true;
}

static void nn_astparser_declarefuncargvar(NNAstParser* prs)
{
    int i;
    NNAstToken* name;
    NNAstLocal* local;
    /* global variables are implicitly declared... */
    if(prs->currentfunccompiler->scopedepth == 0)
    {
        return;
    }
    name = &prs->prevtoken;
    for(i = prs->currentfunccompiler->localcount - 1; i >= 0; i--)
    {
        local = &prs->currentfunccompiler->locals[i];
        if(local->depth != -1 && local->depth < prs->currentfunccompiler->scopedepth)
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


static int nn_astparser_parsefuncparamvar(NNAstParser* prs, const char* message)
{
    if(!nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, message))
    {
        /* what to do here? */
    }
    nn_astparser_declarefuncargvar(prs);
    /* we are in a local scope... */
    if(prs->currentfunccompiler->scopedepth > 0)
    {
        return 0;
    }
    return nn_astparser_makeidentconst(prs, &prs->prevtoken);
}

static uint8_t nn_astparser_parsefunccallargs(NNAstParser* prs)
{
    uint8_t argcount;
    argcount = 0;
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENCLOSE))
    {
        do
        {
            nn_astparser_ignorewhitespace(prs);
            nn_astparser_parseexpression(prs);
            if(argcount == NEON_CONFIG_ASTMAXFUNCPARAMS)
            {
                nn_astparser_raiseerror(prs, "cannot have more than %d arguments to a function", NEON_CONFIG_ASTMAXFUNCPARAMS);
            }
            argcount++;
        } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after argument list"))
    {
        /* TODO: handle this, somehow. */
    }
    return argcount;
}

static void nn_astparser_parsefuncparamlist(NNAstParser* prs, NNAstFuncCompiler* fnc)
{
    int defvalconst;
    int paramconst;
    size_t paramid;
    NNAstToken paramname;
    NNAstToken vargname;
    (void)paramid;
    (void)paramname;
    (void)defvalconst;
    (void)fnc;
    paramid = 0;
    /* compile argument list... */
    do
    {
        nn_astparser_ignorewhitespace(prs);
        prs->currentfunccompiler->targetfunc->fnscriptfunc.arity++;
        if(nn_astparser_match(prs, NEON_ASTTOK_TRIPLEDOT))
        {
            prs->currentfunccompiler->targetfunc->fnscriptfunc.isvariadic = true;
            nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected identifier after '...'");
            vargname = prs->prevtoken;
            nn_astparser_addlocal(prs, vargname);
            nn_astparser_definevariable(prs, 0);
            break;
        }
        paramconst = nn_astparser_parsefuncparamvar(prs, "expected parameter name");
        paramname = prs->prevtoken;
        nn_astparser_definevariable(prs, paramconst);
        nn_astparser_ignorewhitespace(prs);
        #if 1
        if(nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
        {
            fprintf(stderr, "parsing optional argument....\n");
            if(!nn_astparser_parseexpression(prs))
            {
                nn_astparser_raiseerror(prs, "failed to parse function default paramter value");
            }
            #if 0
                defvalconst = nn_astparser_addlocal(prs, paramname);
            #else
                defvalconst = paramconst;
            #endif
            #if 1
                #if 1
                    nn_astemit_emitbyteandshort(prs, NEON_OP_FUNCARGOPTIONAL, defvalconst);
                    //nn_astemit_emit1short(prs, paramid);
                #else
                    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALSET, defvalconst);
                #endif
            #endif
        }
        #endif
        nn_astparser_ignorewhitespace(prs);
        paramid++;

    } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
}

static void nn_astfunccompiler_compilebody(NNAstParser* prs, NNAstFuncCompiler* fnc, bool closescope, bool isanon)
{
    int i;
    NNObjFunction* function;
    (void)isanon;
    /* compile the body */
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACEOPEN, "expected '{' before function body");
    nn_astparser_parseblock(prs);
    /* create the function object */
    if(closescope)
    {
        nn_astparser_scopeend(prs);
    }
    function = nn_astparser_endcompiler(prs, false);
    nn_vm_stackpush(prs->pstate, nn_value_fromobject(function));
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKECLOSURE, nn_astparser_pushconst(prs, nn_value_fromobject(function)));
    for(i = 0; i < function->upvalcount; i++)
    {
        nn_astemit_emit1byte(prs, fnc->upvalues[i].islocal ? 1 : 0);
        nn_astemit_emit1short(prs, fnc->upvalues[i].index);
    }
    nn_vm_stackpop(prs->pstate);
}

static void nn_astparser_parsefuncfull(NNAstParser* prs, NNFuncContextType type, bool isanon)
{
    NNAstFuncCompiler fnc;
    prs->infunction = true;
    nn_astfunccompiler_init(prs, &fnc, type, isanon);
    nn_astparser_scopebegin(prs);
    /* compile parameter list */
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after function name");
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENCLOSE))
    {
        nn_astparser_parsefuncparamlist(prs, &fnc);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after function parameters");
    nn_astfunccompiler_compilebody(prs, &fnc, false, isanon);
    prs->infunction = false;
}

static void nn_astparser_parsemethod(NNAstParser* prs, NNAstToken classname, NNAstToken methodname, bool havenametoken, bool isstatic)
{
    size_t sn;
    int constant;
    const char* sc;
    NNFuncContextType type;
    NNAstToken actualmthname;
    static NNAstTokType tkns[] = { NEON_ASTTOK_IDENTNORMAL, NEON_ASTTOK_DECORATOR };
    (void)classname;
    sc = "constructor";
    sn = strlen(sc);
    if(havenametoken)
    {
        actualmthname = methodname;
    }
    else
    {
        nn_astparser_consumeor(prs, "method name expected", tkns, 2);
        actualmthname = prs->prevtoken;
    }
    constant = nn_astparser_makeidentconst(prs, &actualmthname);
    type = NEON_FNCONTEXTTYPE_METHOD;
    if(isstatic)
    {
        type = NEON_FNCONTEXTTYPE_STATIC;
    }
    if((prs->prevtoken.length == (int)sn) && (memcmp(prs->prevtoken.start, sc, sn) == 0))
    {
        type = NEON_FNCONTEXTTYPE_INITIALIZER;
    }
    else if((prs->prevtoken.length > 0) && (prs->prevtoken.start[0] == '_'))
    {
        type = NEON_FNCONTEXTTYPE_PRIVATE;
    }
    nn_astparser_parsefuncfull(prs, type, false);
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKEMETHOD, constant);
}

static bool nn_astparser_ruleanonfunc(NNAstParser* prs, bool canassign)
{
    NNAstFuncCompiler fnc;
    (void)canassign;
    nn_astfunccompiler_init(prs, &fnc, NEON_FNCONTEXTTYPE_FUNCTION, true);
    nn_astparser_scopebegin(prs);
    /* compile parameter list */
    if(nn_astparser_check(prs, NEON_ASTTOK_IDENTNORMAL))
    {
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "optional name for anonymous function");
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' at start of anonymous function");
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENCLOSE))
    {
        nn_astparser_parsefuncparamlist(prs, &fnc);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after anonymous function parameters");
    nn_astfunccompiler_compilebody(prs, &fnc, true, true);
    return true;
}


static bool nn_astparser_ruleanonclass(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    nn_astparser_parseclassdeclaration(prs, false);
    return true;
}

static bool nn_astparser_parsefield(NNAstParser* prs, NNAstToken* nametokendest, bool* havenamedest, bool isstatic)
{
    int fieldconstant;
    NNAstToken fieldname;
    *havenamedest = false;
    if(nn_astparser_match(prs, NEON_ASTTOK_IDENTNORMAL))
    {
        fieldname = prs->prevtoken;
        *nametokendest = fieldname;
        if(nn_astparser_check(prs, NEON_ASTTOK_ASSIGN))
        {
            nn_astparser_consume(prs, NEON_ASTTOK_ASSIGN, "expected '=' after ident");
            fieldconstant = nn_astparser_makeidentconst(prs, &fieldname);
            nn_astparser_parseexpression(prs);
            nn_astemit_emitbyteandshort(prs, NEON_OP_CLASSPROPERTYDEFINE, fieldconstant);
            nn_astemit_emit1byte(prs, isstatic ? 1 : 0);
            nn_astparser_consumestmtend(prs);
            nn_astparser_ignorewhitespace(prs);
            return true;
        }
    }
    *havenamedest = true;
    return false;
}

static void nn_astparser_parsefuncdecl(NNAstParser* prs)
{
    int global;
    global = nn_astparser_parsevariable(prs, "function name expected");
    nn_astparser_markinitialized(prs);
    nn_astparser_parsefuncfull(prs, NEON_FNCONTEXTTYPE_FUNCTION, false);
    nn_astparser_definevariable(prs, global);
}

static void nn_astparser_parseclassdeclaration(NNAstParser* prs, bool named)
{
    bool isstatic;
    bool havenametoken;
    int nameconst;
    NNAstToken nametoken;
    NNAstCompContext oldctx;
    NNAstToken classname;
    NNAstClassCompiler classcompiler;
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
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "class name expected");
        classname = prs->prevtoken;
        nn_astparser_declarevariable(prs);
    }
    else
    {
        classname = nn_astparser_synthtoken("<anonclass>");
    }
    nameconst = nn_astparser_makeidentconst(prs, &classname);
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKECLASS, nameconst);
    if(named)
    {
        nn_astparser_definevariable(prs, nameconst);
    }
    classcompiler.name = prs->prevtoken;
    classcompiler.hassuperclass = false;
    classcompiler.enclosing = prs->currentclasscompiler;
    prs->currentclasscompiler = &classcompiler;
    oldctx = prs->compcontext;
    prs->compcontext = NEON_COMPCONTEXT_CLASS;
    if(nn_astparser_match(prs, NEON_ASTTOK_KWEXTENDS))
    {
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "name of superclass expected");
        nn_astparser_rulevarnormal(prs, false);
        if(nn_astparser_identsequal(&classname, &prs->prevtoken))
        {
            nn_astparser_raiseerror(prs, "class %.*s cannot inherit from itself", classname.length, classname.start);
        }
        nn_astparser_scopebegin(prs);
        nn_astparser_addlocal(prs, nn_astparser_synthtoken(g_strsuper));
        nn_astparser_definevariable(prs, 0);
        nn_astparser_namedvar(prs, classname, false);
        nn_astemit_emitinstruc(prs, NEON_OP_CLASSINHERIT);
        classcompiler.hassuperclass = true;
    }
    if(named)
    {
        nn_astparser_namedvar(prs, classname, false);
    }
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACEOPEN, "expected '{' before class body");
    nn_astparser_ignorewhitespace(prs);
    while(!nn_astparser_check(prs, NEON_ASTTOK_BRACECLOSE) && !nn_astparser_check(prs, NEON_ASTTOK_EOF))
    {
        isstatic = false;
        if(nn_astparser_match(prs, NEON_ASTTOK_KWSTATIC))
        {
            isstatic = true;
        }
        if(nn_astparser_match(prs, NEON_ASTTOK_KWVAR))
        {
            /*
            * TODO:
            * using 'var ... =' in a class is actually semantically superfluous,
            * but not incorrect either. maybe warn that this syntax is deprecated?
            */
        }
        if(!nn_astparser_parsefield(prs, &nametoken, &havenametoken, isstatic))
        {
            nn_astparser_parsemethod(prs, classname, nametoken, havenametoken, isstatic);
        }
        nn_astparser_ignorewhitespace(prs);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_BRACECLOSE, "expected '}' after class body");
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
    }
    if(named)
    {
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    }
    if(classcompiler.hassuperclass)
    {
        nn_astparser_scopeend(prs);
    }
    prs->currentclasscompiler = prs->currentclasscompiler->enclosing;
    prs->compcontext = oldctx;
}

static void nn_astparser_parsevardecl(NNAstParser* prs, bool isinitializer, bool isconst)
{
    int global;
    int totalparsed;
    (void)isconst;
    totalparsed = 0;
    do
    {
        if(totalparsed > 0)
        {
            nn_astparser_ignorewhitespace(prs);
        }
        global = nn_astparser_parsevariable(prs, "variable name expected");
        if(nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
        {
            nn_astparser_parseexpression(prs);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
        }
        nn_astparser_definevariable(prs, global);
        totalparsed++;
    } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
    if(!isinitializer)
    {
        nn_astparser_consumestmtend(prs);
    }
    else
    {
        nn_astparser_consume(prs, NEON_ASTTOK_SEMICOLON, "expected ';' after initializer");
        nn_astparser_ignorewhitespace(prs);
    }
}

static void nn_astparser_parseexprstmt(NNAstParser* prs, bool isinitializer, bool semi)
{
    if(prs->pstate->isrepl && prs->currentfunccompiler->scopedepth == 0)
    {
        prs->replcanecho = true;
    }
    if(!semi)
    {
        nn_astparser_parseexpression(prs);
    }
    else
    {
        nn_astparser_parseprecnoadvance(prs, NEON_ASTPREC_ASSIGNMENT);
    }
    if(!isinitializer)
    {
        if(prs->replcanecho && prs->pstate->isrepl)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_ECHO);
            prs->replcanecho = false;
        }
        else
        {
            #if 0
            if(!prs->keeplastvalue)
            #endif
            {
                nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
            }
        }
        nn_astparser_consumestmtend(prs);
    }
    else
    {
        nn_astparser_consume(prs, NEON_ASTTOK_SEMICOLON, "expected ';' after initializer");
        nn_astparser_ignorewhitespace(prs);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
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
 
static void nn_astparser_parseforstmt(NNAstParser* prs)
{
    int exitjump;
    int bodyjump;
    int incrstart;
    int surroundingloopstart;
    int surroundingscopedepth;
    nn_astparser_scopebegin(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'for'");
    /* parse initializer... */
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
        /* no initializer */
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWVAR))
    {
        nn_astparser_parsevardecl(prs, true, false);
    }
    else
    {
        nn_astparser_parseexprstmt(prs, true, false);
    }
    /* keep a copy of the surrounding loop's start and depth */
    surroundingloopstart = prs->innermostloopstart;
    surroundingscopedepth = prs->innermostloopscopedepth;
    /* update the parser's loop start and depth to the current */
    prs->innermostloopstart = nn_astparser_currentblob(prs)->count;
    prs->innermostloopscopedepth = prs->currentfunccompiler->scopedepth;
    exitjump = -1;
    if(!nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON))
    {
        /* the condition is optional */
        nn_astparser_parseexpression(prs);
        nn_astparser_consume(prs, NEON_ASTTOK_SEMICOLON, "expected ';' after condition");
        nn_astparser_ignorewhitespace(prs);
        /* jump out of the loop if the condition is false... */
        exitjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        /* pop the condition */
    }
    /* the iterator... */
    if(!nn_astparser_check(prs, NEON_ASTTOK_BRACEOPEN))
    {
        bodyjump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
        incrstart = nn_astparser_currentblob(prs)->count;
        nn_astparser_parseexpression(prs);
        nn_astparser_ignorewhitespace(prs);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        nn_astemit_emitloop(prs, prs->innermostloopstart);
        prs->innermostloopstart = incrstart;
        nn_astemit_patchjump(prs, bodyjump);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'for'");
    nn_astparser_parsestmt(prs);
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    if(exitjump != -1)
    {
        nn_astemit_patchjump(prs, exitjump);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    }
    nn_astparser_endloop(prs);
    /* reset the loop start and scope depth to the surrounding value */
    prs->innermostloopstart = surroundingloopstart;
    prs->innermostloopscopedepth = surroundingscopedepth;
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
static void nn_astparser_parseforeachstmt(NNAstParser* prs)
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
    nn_astparser_scopebegin(prs);
    /* define @iter and @itern constant */
    citer = nn_astparser_pushconst(prs, nn_value_fromobject(nn_string_intern("@iter")));
    citern = nn_astparser_pushconst(prs, nn_value_fromobject(nn_string_intern("@itern")));
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'foreach'");
    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected variable name after 'foreach'");
    if(!nn_astparser_check(prs, NEON_ASTTOK_COMMA))
    {
        keytoken = nn_astparser_synthtoken(" _ ");
        valuetoken = prs->prevtoken;
    }
    else
    {
        keytoken = prs->prevtoken;
        nn_astparser_consume(prs, NEON_ASTTOK_COMMA, "");
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "expected variable name after ','");
        valuetoken = prs->prevtoken;
    }
    nn_astparser_consume(prs, NEON_ASTTOK_KWIN, "expected 'in' after for loop variable(s)");
    nn_astparser_ignorewhitespace(prs);
    /*
    // The space in the variable name ensures it won't collide with a user-defined
    // variable.
    */
    iteratortoken = nn_astparser_synthtoken(" iterator ");
    /* Evaluate the sequence expression and store it in a hidden local variable. */
    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'foreach'");
    if(prs->currentfunccompiler->localcount + 3 > NEON_CONFIG_ASTMAXLOCALS)
    {
        nn_astparser_raiseerror(prs, "cannot declare more than %d variables in one scope", NEON_CONFIG_ASTMAXLOCALS);
        return;
    }
    /* add the iterator to the local scope */
    iteratorslot = nn_astparser_addlocal(prs, iteratortoken) - 1;
    nn_astparser_definevariable(prs, 0);
    /* Create the key local variable. */
    nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    keyslot = nn_astparser_addlocal(prs, keytoken) - 1;
    nn_astparser_definevariable(prs, keyslot);
    /* create the local value slot */
    nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    valueslot = nn_astparser_addlocal(prs, valuetoken) - 1;
    nn_astparser_definevariable(prs, 0);
    surroundingloopstart = prs->innermostloopstart;
    surroundingscopedepth = prs->innermostloopscopedepth;
    /*
    // we'll be jumping back to right before the
    // expression after the loop body
    */
    prs->innermostloopstart = nn_astparser_currentblob(prs)->count;
    prs->innermostloopscopedepth = prs->currentfunccompiler->scopedepth;
    /* key = iterable.iter_n__(key) */
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, iteratorslot);
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, keyslot);
    nn_astemit_emitbyteandshort(prs, NEON_OP_CALLMETHOD, citern);
    nn_astemit_emit1byte(prs, 1);
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALSET, keyslot);
    falsejump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    /* value = iterable.iter__(key) */
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, iteratorslot);
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, keyslot);
    nn_astemit_emitbyteandshort(prs, NEON_OP_CALLMETHOD, citer);
    nn_astemit_emit1byte(prs, 1);
    /*
    // Bind the loop value in its own scope. This ensures we get a fresh
    // variable each iteration so that closures for it don't all see the same one.
    */
    nn_astparser_scopebegin(prs);
    /* update the value */
    nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALSET, valueslot);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parsestmt(prs);
    nn_astparser_scopeend(prs);
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    nn_astemit_patchjump(prs, falsejump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_endloop(prs);
    prs->innermostloopstart = surroundingloopstart;
    prs->innermostloopscopedepth = surroundingscopedepth;
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
static void nn_astparser_parseswitchstmt(NNAstParser* prs)
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
    NNAstTokType casetype;
    NNObjSwitch* sw;
    NNObjString* string;
    /* the expression */
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' before 'switch'");

    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'switch'");
    nn_astparser_ignorewhitespace(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACEOPEN, "expected '{' after 'switch' expression");
    nn_astparser_ignorewhitespace(prs);
    /* 0: before all cases, 1: before default, 2: after default */
    swstate = 0;
    casecount = 0;
    sw = nn_object_makeswitch();
    nn_vm_stackpush(prs->pstate, nn_value_fromobject(sw));
    switchcode = nn_astemit_emitswitch(prs);
    /* nn_astemit_emitbyteandshort(prs, NEON_OP_SWITCH, nn_astparser_pushconst(prs, nn_value_fromobject(sw))); */
    startoffset = nn_astparser_currentblob(prs)->count;
    prs->inswitch = true;
    while(!nn_astparser_match(prs, NEON_ASTTOK_BRACECLOSE) && !nn_astparser_check(prs, NEON_ASTTOK_EOF))
    {
        if(nn_astparser_match(prs, NEON_ASTTOK_KWCASE) || nn_astparser_match(prs, NEON_ASTTOK_KWDEFAULT))
        {
            casetype = prs->prevtoken.type;
            if(swstate == 2)
            {
                nn_astparser_raiseerror(prs, "cannot have another case after a default case");
            }
            if(swstate == 1)
            {
                /* at the end of the previous case, jump over the others... */
                caseends[casecount++] = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
            }
            if(casetype == NEON_ASTTOK_KWCASE)
            {
                swstate = 1;
                do
                {
                    nn_astparser_ignorewhitespace(prs);
                    nn_astparser_advance(prs);
                    jump = nn_value_makenumber((double)nn_astparser_currentblob(prs)->count - (double)startoffset);
                    if(prs->prevtoken.type == NEON_ASTTOK_KWTRUE)
                    {
                        sw->table.set(nn_value_makebool(true), jump);
                    }
                    else if(prs->prevtoken.type == NEON_ASTTOK_KWFALSE)
                    {
                        sw->table.set(nn_value_makebool(false), jump);
                    }
                    else if(prs->prevtoken.type == NEON_ASTTOK_LITERALSTRING || prs->prevtoken.type == NEON_ASTTOK_LITERALRAWSTRING)
                    {
                        str = nn_astparser_compilestring(prs, &length, true);
                        string = nn_string_takelen(str, length);
                        /* gc fix */
                        nn_vm_stackpush(prs->pstate, nn_value_fromobject(string));
                        sw->table.set(nn_value_fromobject(string), jump);
                        /* gc fix */
                        nn_vm_stackpop(prs->pstate);
                    }
                    else if(nn_astparser_checknumber(prs))
                    {
                        sw->table.set(nn_astparser_compilenumber(prs), jump);
                    }
                    else
                    {
                        /* pop the switch */
                        nn_vm_stackpop(prs->pstate);
                        nn_astparser_raiseerror(prs, "only constants can be used in 'case' expressions");
                        return;
                    }
                } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
                nn_astparser_consume(prs, NEON_ASTTOK_COLON, "expected ':' after 'case' constants");
            }
            else
            {
                nn_astparser_consume(prs, NEON_ASTTOK_COLON, "expected ':' after 'default'");
                swstate = 2;
                sw->defaultjump = nn_astparser_currentblob(prs)->count - startoffset;
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
    prs->inswitch = false;
    /* if we ended without a default case, patch its condition jump */
    if(swstate == 1)
    {
        caseends[casecount++] = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    }
    /* patch all the case jumps to the end */
    for(i = 0; i < casecount; i++)
    {
        nn_astemit_patchjump(prs, caseends[i]);
    }
    sw->exitjump = nn_astparser_currentblob(prs)->count - startoffset;
    nn_astemit_patchswitch(prs, switchcode, nn_astparser_pushconst(prs, nn_value_fromobject(sw)));
    /* pop the switch */  
    nn_vm_stackpop(prs->pstate);
}

static void nn_astparser_parseifstmt(NNAstParser* prs)
{
    int elsejump;
    int thenjump;
    nn_astparser_parseexpression(prs);
    thenjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parsestmt(prs);
    elsejump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    nn_astemit_patchjump(prs, thenjump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    if(nn_astparser_match(prs, NEON_ASTTOK_KWELSE))
    {
        nn_astparser_parsestmt(prs);
    }
    nn_astemit_patchjump(prs, elsejump);
}

static void nn_astparser_parseechostmt(NNAstParser* prs)
{
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_ECHO);
    nn_astparser_consumestmtend(prs);
}

static void nn_astparser_parsethrowstmt(NNAstParser* prs)
{
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_EXTHROW);
    nn_astparser_discardlocals(prs, prs->currentfunccompiler->scopedepth - 1);
    nn_astparser_consumestmtend(prs);
}

static void nn_astparser_parseassertstmt(NNAstParser* prs)
{
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'assert'");
    nn_astparser_parseexpression(prs);
    if(nn_astparser_match(prs, NEON_ASTTOK_COMMA))
    {
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parseexpression(prs);
    }
    else
    {
        nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    }
    nn_astemit_emitinstruc(prs, NEON_OP_ASSERT);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'assert'");
    nn_astparser_consumestmtend(prs);
}

static void nn_astparser_parsetrystmt(NNAstParser* prs)
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
    if(prs->currentfunccompiler->handlercount == NEON_CONFIG_MAXEXCEPTHANDLERS)
    {
        nn_astparser_raiseerror(prs, "maximum exception handler in scope exceeded");
    }
    #endif
    prs->currentfunccompiler->handlercount++;
    prs->istrying = true;
    nn_astparser_ignorewhitespace(prs);
    trybegins = nn_astemit_emittry(prs);
    /* compile the try body */
    nn_astparser_parsestmt(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_EXPOPTRY);
    exitjump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    prs->istrying = false;
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
    if(nn_astparser_match(prs, NEON_ASTTOK_KWCATCH))
    {
        catchexists = true;
        nn_astparser_scopebegin(prs);
        nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'catch'");
        /*
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "missing exception class name");
        */
        type = nn_astparser_makeidentconst(prs, &prs->prevtoken);
        address = nn_astparser_currentblob(prs)->count;
        if(nn_astparser_match(prs, NEON_ASTTOK_IDENTNORMAL))
        {
            nn_astparser_createdvar(prs, prs->prevtoken);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        }
        nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'catch'");
        nn_astemit_emitinstruc(prs, NEON_OP_EXPOPTRY);
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parsestmt(prs);
        nn_astparser_scopeend(prs);
    }
    else
    {
        type = nn_astparser_pushconst(prs, nn_value_fromobject(nn_string_intern("Exception")));
    }
    nn_astemit_patchjump(prs, exitjump);
    if(nn_astparser_match(prs, NEON_ASTTOK_KWFINALLY))
    {
        finalexists = true;
        /*
        // if we arrived here from either the try or handler block,
        // we don't want to continue propagating the exception
        */
        nn_astemit_emitinstruc(prs, NEON_OP_PUSHFALSE);
        finally = nn_astparser_currentblob(prs)->count;
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_parsestmt(prs);
        continueexecutionaddress = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
        /* pop the bool off the stack */
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        nn_astemit_emitinstruc(prs, NEON_OP_EXPUBLISHTRY);
        nn_astemit_patchjump(prs, continueexecutionaddress);
        nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    }
    if(!finalexists && !catchexists)
    {
        nn_astparser_raiseerror(prs, "try block must contain at least one of catch or finally");
    }
    nn_astemit_patchtry(prs, trybegins, type, address, finally);
}

static void nn_astparser_parsereturnstmt(NNAstParser* prs)
{
    prs->isreturning = true;
    /*
    if(prs->currentfunccompiler->type == NEON_FNCONTEXTTYPE_SCRIPT)
    {
        nn_astparser_raiseerror(prs, "cannot return from top-level code");
    }
    */
    if(nn_astparser_match(prs, NEON_ASTTOK_SEMICOLON) || nn_astparser_match(prs, NEON_ASTTOK_NEWLINE))
    {
        nn_astemit_emitreturn(prs);
    }
    else
    {
        if(prs->currentfunccompiler->contexttype == NEON_FNCONTEXTTYPE_INITIALIZER)
        {
            nn_astparser_raiseerror(prs, "cannot return value from constructor");
        }
        if(prs->istrying)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_EXPOPTRY);
        }
        nn_astparser_parseexpression(prs);
        nn_astemit_emitinstruc(prs, NEON_OP_RETURN);
        nn_astparser_consumestmtend(prs);
    }
    prs->isreturning = false;
}

static void nn_astparser_parsewhilestmt(NNAstParser* prs)
{
    int exitjump;
    int surroundingloopstart;
    int surroundingscopedepth;
    surroundingloopstart = prs->innermostloopstart;
    surroundingscopedepth = prs->innermostloopscopedepth;
    /*
    // we'll be jumping back to right before the
    // expression after the loop body
    */
    prs->innermostloopstart = nn_astparser_currentblob(prs)->count;
    prs->innermostloopscopedepth = prs->currentfunccompiler->scopedepth;
    nn_astparser_parseexpression(prs);
    exitjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parsestmt(prs);
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    nn_astemit_patchjump(prs, exitjump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_endloop(prs);
    prs->innermostloopstart = surroundingloopstart;
    prs->innermostloopscopedepth = surroundingscopedepth;
}

static void nn_astparser_parsedo_whilestmt(NNAstParser* prs)
{
    int exitjump;
    int surroundingloopstart;
    int surroundingscopedepth;
    surroundingloopstart = prs->innermostloopstart;
    surroundingscopedepth = prs->innermostloopscopedepth;
    /*
    // we'll be jumping back to right before the
    // statements after the loop body
    */
    prs->innermostloopstart = nn_astparser_currentblob(prs)->count;
    prs->innermostloopscopedepth = prs->currentfunccompiler->scopedepth;
    nn_astparser_parsestmt(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_KWWHILE, "expecting 'while' statement");
    nn_astparser_parseexpression(prs);
    exitjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    nn_astemit_patchjump(prs, exitjump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_endloop(prs);
    prs->innermostloopstart = surroundingloopstart;
    prs->innermostloopscopedepth = surroundingscopedepth;
}

static void nn_astparser_parsecontinuestmt(NNAstParser* prs)
{
    if(prs->innermostloopstart == -1)
    {
        nn_astparser_raiseerror(prs, "'continue' can only be used in a loop");
    }
    /*
    // discard local variables created in the loop
    //  discard_local(prs, prs->innermostloopscopedepth);
    */
    nn_astparser_discardlocals(prs, prs->innermostloopscopedepth + 1);
    /* go back to the top of the loop */
    nn_astemit_emitloop(prs, prs->innermostloopstart);
    nn_astparser_consumestmtend(prs);
}

static void nn_astparser_parsebreakstmt(NNAstParser* prs)
{
    if(!prs->inswitch)
    {
        if(prs->innermostloopstart == -1)
        {
            nn_astparser_raiseerror(prs, "'break' can only be used in a loop");
        }
        /* discard local variables created in the loop */
        #if 0
        int i;
        for(i = prs->currentfunccompiler->localcount - 1; i >= 0 && prs->currentfunccompiler->locals[i].depth >= prs->currentfunccompiler->scopedepth; i--)
        {
            if (prs->currentfunccompiler->locals[i].iscaptured)
            {
                nn_astemit_emitinstruc(prs, NEON_OP_UPVALUECLOSE);
            }
            else
            {
                nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
            }
        }
        #endif
        nn_astparser_discardlocals(prs, prs->innermostloopscopedepth + 1);
        nn_astemit_emitjump(prs, NEON_OP_BREAK_PL);
    }
    nn_astparser_consumestmtend(prs);
}

static void nn_astparser_synchronize(NNAstParser* prs)
{
    prs->panicmode = false;
    while(prs->currtoken.type != NEON_ASTTOK_EOF)
    {
        if(prs->currtoken.type == NEON_ASTTOK_NEWLINE || prs->currtoken.type == NEON_ASTTOK_SEMICOLON)
        {
            return;
        }
        switch(prs->currtoken.type)
        {
            case NEON_ASTTOK_KWCLASS:
            case NEON_ASTTOK_KWFUNCTION:
            case NEON_ASTTOK_KWVAR:
            case NEON_ASTTOK_KWFOREACH:
            case NEON_ASTTOK_KWIF:
            case NEON_ASTTOK_KWEXTENDS:
            case NEON_ASTTOK_KWSWITCH:
            case NEON_ASTTOK_KWCASE:
            case NEON_ASTTOK_KWFOR:
            case NEON_ASTTOK_KWDO:
            case NEON_ASTTOK_KWWHILE:
            case NEON_ASTTOK_KWECHO:
            case NEON_ASTTOK_KWASSERT:
            case NEON_ASTTOK_KWTRY:
            case NEON_ASTTOK_KWCATCH:
            case NEON_ASTTOK_KWTHROW:
            case NEON_ASTTOK_KWRETURN:
            case NEON_ASTTOK_KWSTATIC:
            case NEON_ASTTOK_KWTHIS:
            case NEON_ASTTOK_KWSUPER:
            case NEON_ASTTOK_KWFINALLY:
            case NEON_ASTTOK_KWIN:
            case NEON_ASTTOK_KWIMPORT:
            case NEON_ASTTOK_KWAS:
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
NNObjFunction* nn_astparser_compilesource(NNState* state, NNObjModule* module, const char* source, NNBlob* blob, bool fromimport, bool keeplast)
{
    NNAstFuncCompiler fnc;
    NNAstLexer* lexer;
    NNAstParser* parser;
    NNObjFunction* function;
    (void)blob;
    lexer = nn_astlex_make(state, source);
    parser = nn_astparser_makeparser(state, lexer, module, keeplast);
    nn_astfunccompiler_init(parser, &fnc, NEON_FNCONTEXTTYPE_SCRIPT, true);
    fnc.fromimport = fromimport;
    nn_astparser_runparser(parser);
    function = nn_astparser_endcompiler(parser, true);
    if(parser->haderror)
    {
        function = nullptr;
    }
    nn_astlex_destroy(lexer);
    nn_astparser_destroy(parser);
    return function;
}




NNObjArray* nn_array_makefilled(size_t cnt, NNValue filler)
{
    size_t i;
    NNObjArray* list;
    list = (NNObjArray*)nn_object_allocobject(sizeof(NNObjArray), NEON_OBJTYPE_ARRAY, false);
    list->varray.initSelf();
    if(cnt > 0)
    {
        for(i=0; i<cnt; i++)
        {
            list->varray.push(filler);
        }
    }
    return list;
}

NNObjArray* nn_array_make()
{
    return nn_array_makefilled(0, nn_value_makenull());
}

NNObjArray* nn_object_makearray()
{
    return nn_array_make();
}

void nn_array_push(NNObjArray* list, NNValue value)
{
    /*nn_vm_stackpush(state, value);*/
    list->varray.push(value);
    /*nn_vm_stackpop(state); */
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


static NNValue nn_objfnarray_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int cnt;
    NNValue filler;
    NNObjArray* arr;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "constructor", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    filler = nn_value_makenull();
    if(argc > 1)
    {
        filler = argv[1];
    }
    cnt = nn_value_asnumber(argv[0]);
    arr = nn_array_makefilled(cnt, filler);
    return nn_value_fromobject(arr);
}

static NNValue nn_objfnarray_join(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    selfarr = nn_value_asarray(thisval);
    joinee = nullptr;
    havejoinee = false;
    if(argc > 0)
    {
        vjoinee = argv[0];
        if(nn_value_isstring(vjoinee))
        {
            joinee = nn_value_asstring(vjoinee);
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
        return nn_value_fromobject(nn_string_intern(""));
    }
    nn_iostream_makestackstring(&pr);
    for(i = 0; i < count; i++)
    {
        nn_iostream_printvalue(&pr, list[i], false, true);
        if((havejoinee && (joinee != nullptr)) && ((i+1) < count))
        {
            nn_iostream_writestringl(&pr, nn_string_getdata(joinee), nn_string_getlength(joinee));
        }
    }
    ret = nn_value_fromobject(nn_iostream_takestring(&pr));
    nn_iostream_destroy(&pr);
    return ret;
}


static NNValue nn_objfnarray_length(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* selfarr;
    SCRIPTFN_UNUSED(state);
    NNArgCheck check;
    nn_argcheck_init(state, &check, "length", argv, argc);
    selfarr = nn_value_asarray(thisval);
    return nn_value_makenumber(selfarr->varray.count());
}

static NNValue nn_objfnarray_append(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    SCRIPTFN_UNUSED(state);
    for(i = 0; i < argc; i++)
    {
        nn_array_push(nn_value_asarray(thisval), argv[i]);
    }
    return nn_value_makenull();
}

static NNValue nn_objfnarray_clear(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "clear", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_value_asarray(thisval)->varray.deInit(false);
    return nn_value_makenull();
}

static NNValue nn_objfnarray_clone(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "clone", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    return nn_value_fromobject(nn_array_copy(list, 0, list->varray.count()));
}

static NNValue nn_objfnarray_count(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    int count;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "count", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(thisval);
    count = 0;
    for(i = 0; i < list->varray.count(); i++)
    {
        if(nn_value_compare((NNValue)list->varray.get(i), argv[0]))
        {
            count++;
        }
    }
    return nn_value_makenumber(count);
}

static NNValue nn_objfnarray_extend(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjArray* list;
    NNObjArray* list2;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "extend", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isarray);
    list = nn_value_asarray(thisval);
    list2 = nn_value_asarray(argv[0]);
    for(i = 0; i < list2->varray.count(); i++)
    {
        nn_array_push(list, list2->varray.get(i));
    }
    return nn_value_makenull();
}

static NNValue nn_objfnarray_indexof(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "indexOf", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    list = nn_value_asarray(thisval);
    i = 0;
    if(argc == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        i = nn_value_asnumber(argv[1]);
    }
    for(; i < list->varray.count(); i++)
    {
        if(nn_value_compare((NNValue)list->varray.get(i), argv[0]))
        {
            return nn_value_makenumber(i);
        }
    }
    return nn_value_makenumber(-1);
}

static NNValue nn_objfnarray_insert(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "insert", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    list = nn_value_asarray(thisval);
    index = (int)nn_value_asnumber(argv[1]);
    list->varray.insert(argv[0], index);
    return nn_value_makenull();
}


static NNValue nn_objfnarray_pop(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue value;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "pop", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    if(list->varray.count() > 0)
    {
        value = list->varray.get(list->varray.count() - 1);
        list->varray.decreaseBy(1);
        return value;
    }
    return nn_value_makenull();
}

static NNValue nn_objfnarray_shift(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t j;
    size_t count;
    NNObjArray* list;
    NNObjArray* newlist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "shift", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    count = 1;
    if(argc == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        count = nn_value_asnumber(argv[0]);
    }
    list = nn_value_asarray(thisval);
    if(count >= list->varray.count() || list->varray.count() == 1)
    {
        list->varray.setCount(0);
        return nn_value_makenull();
    }
    else if(count > 0)
    {
        newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
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
            return nn_value_fromobject(newlist);
        }
    }
    return nn_value_makenull();
}

static NNValue nn_objfnarray_removeat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t index;
    NNValue value;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "removeAt", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(thisval);
    index = nn_value_asnumber(argv[0]);
    if(((int)index < 0) || index >= list->varray.count())
    {
        NEON_RETURNERROR("list index %d out of range at remove_at()", index);
    }
    value = list->varray.get(index);
    for(i = index; i < list->varray.count() - 1; i++)
    {
        list->varray.set(i, list->varray.get(i + 1));
    }
    list->varray.decreaseBy(1);
    return value;
}

static NNValue nn_objfnarray_remove(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "remove", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(thisval);
    index = -1;
    for(i = 0; i < list->varray.count(); i++)
    {
        if(nn_value_compare((NNValue)list->varray.get(i), argv[0]))
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
    return nn_value_makenull();
}

static NNValue nn_objfnarray_reverse(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int fromtop;
    NNObjArray* list;
    NNObjArray* nlist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "reverse", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    nlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
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
    return nn_value_fromobject(nlist);
}

static NNValue nn_objfnarray_sort(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "sort", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    nn_value_sortvalues(state, (NNValue*)list->varray.listitems, list->varray.count());
    return nn_value_makenull();
}

static NNValue nn_objfnarray_contains(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "contains", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(thisval);
    for(i = 0; i < list->varray.count(); i++)
    {
        if(nn_value_compare(argv[0], (NNValue)list->varray.get(i)))
        {
            return nn_value_makebool(true);
        }
    }
    return nn_value_makebool(false);
}

static NNValue nn_objfnarray_delete(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t idxupper;
    size_t idxlower;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "delete", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    idxlower = nn_value_asnumber(argv[0]);
    idxupper = idxlower;
    if(argc == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        idxupper = nn_value_asnumber(argv[1]);
    }
    list = nn_value_asarray(thisval);
    if(((int)idxlower < 0) || idxlower >= list->varray.count())
    {
        NEON_RETURNERROR("list index %d out of range at delete()", idxlower);
    }
    else if(idxupper < idxlower || idxupper >= list->varray.count())
    {
        NEON_RETURNERROR("invalid upper limit %d at delete()", idxupper);
    }
    for(i = 0; i < list->varray.count() - idxupper; i++)
    {
        list->varray.set(idxlower + i, list->varray.get(i + idxupper + 1));
    }
    list->varray.decreaseBy(idxupper - idxlower + 1);
    return nn_value_makenumber((double)idxupper - (double)idxlower + 1);
}

static NNValue nn_objfnarray_first(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "first", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    if(list->varray.count() > 0)
    {
        return list->varray.get(0);
    }
    return nn_value_makenull();
}

static NNValue nn_objfnarray_last(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "last", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    if(list->varray.count() > 0)
    {
        return list->varray.get(list->varray.count() - 1);
    }
    return nn_value_makenull();
}

static NNValue nn_objfnarray_isempty(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isEmpty", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makebool(nn_value_asarray(thisval)->varray.count() == 0);
}


static NNValue nn_objfnarray_take(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t count;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "take", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(thisval);
    count = nn_value_asnumber(argv[0]);
    if((int)count < 0)
    {
        count = list->varray.count() + count;
    }
    if(list->varray.count() < count)
    {
        return nn_value_fromobject(nn_array_copy(list, 0, list->varray.count()));
    }
    return nn_value_fromobject(nn_array_copy(list, 0, count));
}

static NNValue nn_objfnarray_get(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "get", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(thisval);
    index = nn_value_asnumber(argv[0]);
    if((int)index < 0 || index >= list->varray.count())
    {
        return nn_value_makenull();
    }
    return list->varray.get(index);
}

static NNValue nn_objfnarray_compact(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjArray* list;
    NNObjArray* newlist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "compact", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
    for(i = 0; i < list->varray.count(); i++)
    {
        if(!nn_value_compare((NNValue)list->varray.get(i), nn_value_makenull()))
        {
            nn_array_push(newlist, (NNValue)list->varray.get(i));
        }
    }
    return nn_value_fromobject(newlist);
}


static NNValue nn_objfnarray_unique(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t j;
    bool found;
    NNObjArray* list;
    NNObjArray* newlist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "unique", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
    for(i = 0; i < list->varray.count(); i++)
    {
        found = false;
        for(j = 0; j < newlist->varray.count(); j++)
        {
            if(nn_value_compare((NNValue)newlist->varray.get(j), (NNValue)list->varray.get(i)))
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
    return nn_value_fromobject(newlist);
}

static NNValue nn_objfnarray_zip(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t j;
    NNObjArray* list;
    NNObjArray* newlist;
    NNObjArray* alist;
    NNObjArray** arglist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "zip", argv, argc);
    list = nn_value_asarray(thisval);
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
    arglist = (NNObjArray**)nn_gcmem_allocate(sizeof(NNObjArray*), argc, false);
    for(i = 0; i < argc; i++)
    {
        NEON_ARGS_CHECKTYPE(&check, i, nn_value_isarray);
        arglist[i] = nn_value_asarray(argv[i]);
    }
    for(i = 0; i < list->varray.count(); i++)
    {
        alist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
        /* item of main list*/
        nn_array_push(alist, list->varray.get(i));
        for(j = 0; j < argc; j++)
        {
            if(i < arglist[j]->varray.count())
            {
                nn_array_push(alist, arglist[j]->varray.get(i));
            }
            else
            {
                nn_array_push(alist, nn_value_makenull());
            }
        }
        nn_array_push(newlist, nn_value_fromobject(alist));
    }
    return nn_value_fromobject(newlist);
}


static NNValue nn_objfnarray_zipfrom(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t j;
    NNObjArray* list;
    NNObjArray* newlist;
    NNObjArray* alist;
    NNObjArray* arglist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "zipFrom", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isarray);
    list = nn_value_asarray(thisval);
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
    arglist = nn_value_asarray(argv[0]);
    for(i = 0; i < arglist->varray.count(); i++)
    {
        if(!nn_value_isarray(arglist->varray.get(i)))
        {
            NEON_RETURNERROR("invalid list in zip entries");
        }
    }
    for(i = 0; i < list->varray.count(); i++)
    {
        alist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
        nn_array_push(alist, list->varray.get(i));
        for(j = 0; j < arglist->varray.count(); j++)
        {
            if(i < nn_value_asarray(arglist->varray.get(j))->varray.count())
            {
                nn_array_push(alist, nn_value_asarray(arglist->varray.get(j))->varray.get(i));
            }
            else
            {
                nn_array_push(alist, nn_value_makenull());
            }
        }
        nn_array_push(newlist, nn_value_fromobject(alist));
    }
    return nn_value_fromobject(newlist);
}

static NNValue nn_objfnarray_todict(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "toDict", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict());
    list = nn_value_asarray(thisval);
    for(i = 0; i < list->varray.count(); i++)
    {
        nn_dict_setentry(dict, nn_value_makenumber(i), list->varray.get(i));
    }
    return nn_value_fromobject(dict);
}

static NNValue nn_objfnarray_iter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "iter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(thisval);   
    index = nn_value_asnumber(argv[0]);
    if(((int)index > -1) && index < list->varray.count())
    {
        return list->varray.get(index);
    }
    return nn_value_makenull();
}

static NNValue nn_objfnarray_itern(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "itern", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(thisval);
    if(argv[0].isNull())
    {
        if(list->varray.count() == 0)
        {
            return nn_value_makebool(false);
        }
        return nn_value_makenumber(0);
    }
    if(!nn_value_isnumber(argv[0]))
    {
        NEON_RETURNERROR("lists are numerically indexed");
    }
    index = nn_value_asnumber(argv[0]);
    if(index < list->varray.count() - 1)
    {
        return nn_value_makenumber((double)index + 1);
    }
    return nn_value_makenull();
}

static NNValue nn_objfnarray_each(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue callable;
    NNValue unused;
    NNObjArray* list;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "each", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(callable, thisval, nestargs, 2);
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
                nestargs[1] = nn_value_makenumber(i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &unused, false);
    }
    return nn_value_makenull();
}


static NNValue nn_objfnarray_map(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_argcheck_init(state, &check, "map", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(callable, thisval, nestargs, 2);
    resultlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
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
                    nestargs[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &res, false);
            nn_array_push(resultlist, res);
        }
        else
        {
            nn_array_push(resultlist, nn_value_makenull());
        }
    }
    return nn_value_fromobject(resultlist);
}


static NNValue nn_objfnarray_filter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_argcheck_init(state, &check, "filter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(callable, thisval, nestargs, 2);
    resultlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
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
                    nestargs[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result, false);
            if(!nn_value_isfalse(result))
            {
                nn_array_push(resultlist, list->varray.get(i));
            }
        }
    }
    return nn_value_fromobject(resultlist);
}

static NNValue nn_objfnarray_some(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue callable;
    NNValue result;
    NNObjArray* list;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "some", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(callable, thisval, nestargs, 2);
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
                    nestargs[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result, false);
            if(!nn_value_isfalse(result))
            {
                return nn_value_makebool(true);
            }
        }
    }
    return nn_value_makebool(false);
}


static NNValue nn_objfnarray_every(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue result;
    NNValue callable;
    NNObjArray* list;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "every", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(callable, thisval, nestargs, 2);
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
                    nestargs[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result, false);
            if(nn_value_isfalse(result))
            {
                return nn_value_makebool(false);
            }
        }
    }
    return nn_value_makebool(true);
}

static NNValue nn_objfnarray_reduce(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_argcheck_init(state, &check, "reduce", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(thisval);
    callable = argv[0];
    startindex = 0;
    accumulator = nn_value_makenull();
    if(argc == 2)
    {
        accumulator = argv[1];
    }
    if(accumulator.isNull() && list->varray.count() > 0)
    {
        accumulator = list->varray.get(0);
        startindex = 1;
    }
    arity = nn_nestcall_prepare(callable, thisval, nullptr, 4);
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
                        nestargs[2] = nn_value_makenumber(i);
                        if(arity > 4)
                        {
                            passi++;
                            nestargs[3] = thisval;
                        }
                    }
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &accumulator, false);
        }
    }
    return accumulator;
}

static NNValue nn_objfnarray_slice(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_argcheck_init(state, &check, "slice", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    selfarr = nn_value_asarray(thisval);
    salen = selfarr->varray.count();
    end = salen;
    start = nn_value_asnumber(argv[0]);
    backwards = false;
    if(start < 0)
    {
        backwards = true;
    }
    if(argc > 1)
    {
        end = nn_value_asnumber(argv[1]);
    }
    narr = nn_object_makearray();
    i = 0;
    if(backwards)
    {
        i = (end - start);
        until = 0;
        ibegin = ((salen + start)-0);
        iend = end+0;
    }
    else
    {
        until = end;
        ibegin = start;
        iend = until;
    }
    for(i=ibegin; i!=iend; i++)
    {
        nn_array_push(narr, selfarr->varray.get(i));        
    }
    return nn_value_fromobject(narr);
}

void nn_state_installobjectarray(NNState* state)
{
    static NNConstClassMethodItem arraymethods[] =
    {
        {"size", nn_objfnarray_length},
        {"join", nn_objfnarray_join},
        {"append", nn_objfnarray_append},
        {"push", nn_objfnarray_append},
        {"clear", nn_objfnarray_clear},
        {"clone", nn_objfnarray_clone},
        {"count", nn_objfnarray_count},
        {"extend", nn_objfnarray_extend},
        {"indexOf", nn_objfnarray_indexof},
        {"insert", nn_objfnarray_insert},
        {"pop", nn_objfnarray_pop},
        {"shift", nn_objfnarray_shift},
        {"removeAt", nn_objfnarray_removeat},
        {"remove", nn_objfnarray_remove},
        {"reverse", nn_objfnarray_reverse},
        {"sort", nn_objfnarray_sort},
        {"contains", nn_objfnarray_contains},
        {"delete", nn_objfnarray_delete},
        {"first", nn_objfnarray_first},
        {"last", nn_objfnarray_last},
        {"isEmpty", nn_objfnarray_isempty},
        {"take", nn_objfnarray_take},
        {"get", nn_objfnarray_get},
        {"compact", nn_objfnarray_compact},
        {"unique", nn_objfnarray_unique},
        {"zip", nn_objfnarray_zip},
        {"zipFrom", nn_objfnarray_zipfrom},
        {"toDict", nn_objfnarray_todict},
        {"each", nn_objfnarray_each},
        {"map", nn_objfnarray_map},
        {"filter", nn_objfnarray_filter},
        {"some", nn_objfnarray_some},
        {"every", nn_objfnarray_every},
        {"reduce", nn_objfnarray_reduce},
        {"slice", nn_objfnarray_slice},
        {"@iter", nn_objfnarray_iter},
        {"@itern", nn_objfnarray_itern},
        {nullptr, nullptr}
    };
    nn_class_defnativeconstructor(state->classprimarray, nn_objfnarray_constructor);
    nn_class_defcallablefield(state->classprimarray, nn_string_intern("length"), nn_objfnarray_length);
    nn_state_installmethods(state, state->classprimarray, arraymethods);

}



NNObjClass* nn_object_makeclass(NNObjString* name, NNObjClass* parent)
{
    NNObjClass* klass;
    klass = (NNObjClass*)nn_object_allocobject(sizeof(NNObjClass), NEON_OBJTYPE_CLASS, false);
    klass->name = name;
    klass->instproperties.initTable();
    klass->staticproperties.initTable();
    klass->instmethods.initTable();
    klass->staticmethods.initTable();
    klass->constructor = nn_value_makenull();
    klass->destructor = nn_value_makenull();
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
    nn_gcmem_release(klass, sizeof(NNObjClass));   
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
    return klass->instproperties.set(nn_value_fromobject(cstrname), val);
}

bool nn_class_defcallablefieldptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncnative(function, nn_string_getdata(name), uptr);
    return klass->instproperties.setwithtype(nn_value_fromobject(name), nn_value_fromobject(ofn), NEON_PROPTYPE_FUNCTION, true);
}

bool nn_class_defcallablefield(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defcallablefieldptr(klass, name, function, nullptr);
}

bool nn_class_defstaticcallablefieldptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncnative(function, nn_string_getdata(name), uptr);
    return klass->staticproperties.setwithtype(nn_value_fromobject(name), nn_value_fromobject(ofn), NEON_PROPTYPE_FUNCTION, true);
}

bool nn_class_defstaticcallablefield(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defstaticcallablefieldptr(klass, name, function, nullptr);
}

bool nn_class_setstaticproperty(NNObjClass* klass, NNObjString* name, NNValue val)
{
    return klass->staticproperties.set(nn_value_fromobject(name), val);
}

bool nn_class_defnativeconstructorptr(NNObjClass* klass, NNNativeFN function, void* uptr)
{
    const char* cname;
    NNObjFunction* ofn;
    cname = "constructor";
    ofn = nn_object_makefuncnative(function, cname, uptr);
    klass->constructor = nn_value_fromobject(ofn);
    return true;
}

bool nn_class_defnativeconstructor(NNObjClass* klass, NNNativeFN function)
{
    return nn_class_defnativeconstructorptr(klass, function, nullptr);
}

bool nn_class_defmethod(NNObjClass* klass, NNObjString* name, NNValue val)
{
    return klass->instmethods.set(nn_value_fromobject(name), val);
}

bool nn_class_defnativemethodptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* ptr)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncnative(function, nn_string_getdata(name), ptr);
    return nn_class_defmethod(klass, name, nn_value_fromobject(ofn));
}

bool nn_class_defnativemethod(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defnativemethodptr(klass, name, function, nullptr);
}

bool nn_class_defstaticnativemethodptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncnative(function, nn_string_getdata(name), uptr);
    return klass->staticmethods.set(nn_value_fromobject(name), nn_value_fromobject(ofn));
}

bool nn_class_defstaticnativemethod(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defstaticnativemethodptr(klass, name, function, nullptr);
}

NNProperty* nn_class_getmethodfield(NNObjClass* klass, NNObjString* name)
{
    NNProperty* field;
    field = klass->instmethods.getfield(nn_value_fromobject(name));
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
    field = klass->instproperties.getfield(nn_value_fromobject(name));
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
    field = klass->staticmethods.getfield(nn_value_fromobject(name));
    return field;

}

NNObjInstance* nn_object_makeinstancesize(NNObjClass* klass, size_t sz)
{
    NNObjInstance* oinst;
    NNObjInstance* instance;
    oinst = nullptr;
    instance = (NNObjInstance*)nn_object_allocobject(sz, NEON_OBJTYPE_INSTANCE, false);
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
        //nn_state_warn(state, "trying to mark inactive instance <%p>!", instance);
        return;
    }
    nn_valtable_mark(&instance->properties);
    nn_gcmem_markobject((NNObject*)instance->klass);
}

void nn_instance_destroy(NNObjInstance* instance)
{
    if(!instance->klass->destructor.isNull())
    {
        //if(!nn_vm_callvaluewithobject(state, (NNValue)instance->klass->destructor, nn_value_fromobject(instance), 0, false))
        {
            
        }
    }
    instance->properties.deinit();
    instance->active = false;
    nn_gcmem_release(instance, sizeof(NNObjInstance));
}

bool nn_instance_defproperty(NNObjInstance* instance, NNObjString* name, NNValue val)
{
    return instance->properties.set(nn_value_fromobject(name), val);
}

NNProperty* nn_instance_getvar(NNObjInstance* inst, NNObjString* name)
{
    NNProperty* field;
    field = inst->properties.getfield(nn_value_fromobject(name));
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
    dict = (NNObjDict*)nn_object_allocobject(sizeof(NNObjDict), NEON_OBJTYPE_DICT, false);
    dict->htnames.initSelf();
    dict->htab.initTable();
    return dict;
}

void nn_dict_destroy(NNObjDict* dict)
{
    dict->htnames.deInit(false);
    dict->htab.deinit();
}

void nn_dict_mark(NNObjDict* dict)
{    
    nn_valarray_mark(&dict->htnames);
    nn_valtable_mark(&dict->htab);
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
    nn_dict_addentry(dict, nn_value_fromobject(os), value);
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
    NNObjDict *ndict;
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
        ndict->htab.setwithtype(key, (NNValue)field->value, field->type, nn_value_isstring(key));        
    }
    return ndict;
}

static NNValue nn_objfndict_length(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "length", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asdict(thisval)->htnames.count());
}

static NNValue nn_objfndict_add(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue tempvalue;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "add", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    dict = nn_value_asdict(thisval);
    if(dict->htab.get(argv[0], &tempvalue))
    {
        NEON_RETURNERROR("duplicate key %s at add()", nn_string_getdata(nn_value_tostring(argv[0])));
    }
    nn_dict_addentry(dict, argv[0], argv[1]);
    return nn_value_makenull();
}

static NNValue nn_objfndict_set(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue value;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "set", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    dict = nn_value_asdict(thisval);
    if(!dict->htab.get(argv[0], &value))
    {
        nn_dict_addentry(dict, argv[0], argv[1]);
    }
    else
    {
        nn_dict_setentry(dict, argv[0], argv[1]);
    }
    return nn_value_makenull();
}

static NNValue nn_objfndict_clear(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "clear", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    dict->htnames.deInit(false);
    dict->htab.deinit();
    return nn_value_makenull();
}

static NNValue nn_objfndict_clone(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjDict* newdict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "clone", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    newdict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict());
    if(!dict->htab.copyTo(&newdict->htab, true))
    {
        nn_except_throwclass(state, state->exceptions.argumenterror, "failed to copy table");
        return nn_value_makenull();
    }
    for(i = 0; i < dict->htnames.count(); i++)
    {
        newdict->htnames.push(dict->htnames.get(i));
    }
    return nn_value_fromobject(newdict);
}

static NNValue nn_objfndict_compact(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjDict* newdict;
    NNValue tmpvalue;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "compact", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    newdict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict());
    tmpvalue = nn_value_makenull();
    for(i = 0; i < dict->htnames.count(); i++)
    {
        dict->htab.get(dict->htnames.get(i), &tmpvalue);
        if(!nn_value_compare(tmpvalue, nn_value_makenull()))
        {
            nn_dict_addentry(newdict, dict->htnames.get(i), tmpvalue);
        }
    }
    return nn_value_fromobject(newdict);
}

static NNValue nn_objfndict_contains(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue value;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "contains", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(thisval);
    return nn_value_makebool(dict->htab.get(argv[0], &value));
}

static NNValue nn_objfndict_extend(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNValue tmp;
    NNObjDict* dict;
    NNObjDict* dictcpy;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "extend", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isdict);
    dict = nn_value_asdict(thisval);
    dictcpy = nn_value_asdict(argv[0]);
    for(i = 0; i < dictcpy->htnames.count(); i++)
    {
        if(!dict->htab.get(dictcpy->htnames.get(i), &tmp))
        {
            dict->htnames.push(dictcpy->htnames.get(i));
        }
    }
    dictcpy->htab.copyTo(&dict->htab, true);
    return nn_value_makenull();
}

static NNValue nn_objfndict_get(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjDict* dict;
    NNProperty* field;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "get", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    dict = nn_value_asdict(thisval);
    field = nn_dict_getentry(dict, argv[0]);
    if(field == nullptr)
    {
        if(argc == 1)
        {
            return nn_value_makenull();
        }
        else
        {
            return argv[1];
        }
    }
    return (NNValue)field->value;
}

static NNValue nn_objfndict_keys(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "keys", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
    for(i = 0; i < dict->htnames.count(); i++)
    {
        nn_array_push(list, dict->htnames.get(i));
    }
    return nn_value_fromobject(list);
}

static NNValue nn_objfndict_values(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjArray* list;
    NNProperty* field;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "values", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
    for(i = 0; i < dict->htnames.count(); i++)
    {
        field = nn_dict_getentry(dict, dict->htnames.get(i));
        nn_array_push(list, (NNValue)field->value);
    }
    return nn_value_fromobject(list);
}

static NNValue nn_objfndict_remove(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    int index;
    NNValue value;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "remove", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(thisval);
    if(dict->htab.get(argv[0], &value))
    {
        dict->htab.remove(argv[0]);
        index = -1;
        for(i = 0; i < dict->htnames.count(); i++)
        {
            if(nn_value_compare((NNValue)dict->htnames.get(i), argv[0]))
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
    return nn_value_makenull();
}

static NNValue nn_objfndict_isempty(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isempty", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makebool(nn_value_asdict(thisval)->htnames.count() == 0);
}

static NNValue nn_objfndict_findkey(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "findkey", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    auto ht = nn_value_asdict(thisval)->htab;
    return ht.findkey(argv[0], nn_value_makenull());
}

static NNValue nn_objfndict_tolist(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjArray* list;
    NNObjDict* dict;
    NNObjArray* namelist;
    NNObjArray* valuelist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "tolist", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    namelist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
    valuelist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
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
            nn_array_push(valuelist, nn_value_makenull());
        }
    }
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
    nn_array_push(list, nn_value_fromobject(namelist));
    nn_array_push(list, nn_value_fromobject(valuelist));
    return nn_value_fromobject(list);
}

static NNValue nn_objfndict_iter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue result;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "iter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(thisval);
    if(dict->htab.get(argv[0], &result))
    {
        return result;
    }
    return nn_value_makenull();
}

static NNValue nn_objfndict_itern(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "itern", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(thisval);
    if(argv[0].isNull())
    {
        if(dict->htnames.count() == 0)
        {
            return nn_value_makebool(false);
        }
        return dict->htnames.get(0);
    }
    for(i = 0; i < dict->htnames.count(); i++)
    {
        if(nn_value_compare(argv[0], (NNValue)dict->htnames.get(i)) && (i + 1) < dict->htnames.count())
        {
            return dict->htnames.get(i + 1);
        }
    }
    return nn_value_makenull();
}

static NNValue nn_objfndict_each(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_argcheck_init(state, &check, "each", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(callable, thisval, nestargs, 2);
    value = nn_value_makenull();
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
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &unused, false);
    }
    return nn_value_makenull();
}

static NNValue nn_objfndict_filter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_argcheck_init(state, &check, "filter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(callable, thisval, nestargs, 2);
    resultdict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict());
    value = nn_value_makenull();
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
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result, false);
        if(!nn_value_isfalse(result))
        {
            nn_dict_addentry(resultdict, dict->htnames.get(i), value);
        }
    }
    /* pop the call list */
    return nn_value_fromobject(resultdict);
}

static NNValue nn_objfndict_some(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_argcheck_init(state, &check, "some", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(callable, thisval, nestargs, 2);
    value = nn_value_makenull();
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
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result, false);
        if(!nn_value_isfalse(result))
        {
            /* pop the call list */
            return nn_value_makebool(true);
        }
    }
    /* pop the call list */
    return nn_value_makebool(false);
}

static NNValue nn_objfndict_every(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_argcheck_init(state, &check, "every", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(callable, thisval, nestargs, 2);
    value = nn_value_makenull();
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
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result, false);
        if(nn_value_isfalse(result))
        {
            /* pop the call list */
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(true);
}

static NNValue nn_objfndict_reduce(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_argcheck_init(state, &check, "reduce", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(thisval);
    callable = argv[0];
    startindex = 0;
    accumulator = nn_value_makenull();
    if(argc == 2)
    {
        accumulator = argv[1];
    }
    if(accumulator.isNull() && dict->htnames.count() > 0)
    {
        dict->htab.get(dict->htnames.get(0), &accumulator);
        startindex = 1;
    }
    arity = nn_nestcall_prepare(callable, thisval, nestargs, 4);
    value = nn_value_makenull();
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
                            nestargs[3] = thisval;
                        }
                    }
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &accumulator, false);
        }
    }
    return accumulator;
}

void nn_state_installobjectdict(NNState* state)
{
    static NNConstClassMethodItem dictmethods[] =
    {
        {"keys", nn_objfndict_keys},
        {"size", nn_objfndict_length},
        {"add", nn_objfndict_add},
        {"set", nn_objfndict_set},
        {"clear", nn_objfndict_clear},
        {"clone", nn_objfndict_clone},
        {"compact", nn_objfndict_compact},
        {"contains", nn_objfndict_contains},
        {"extend", nn_objfndict_extend},
        {"get", nn_objfndict_get},
        {"values", nn_objfndict_values},
        {"remove", nn_objfndict_remove},
        {"isEmpty", nn_objfndict_isempty},
        {"findKey", nn_objfndict_findkey},
        {"toList", nn_objfndict_tolist},
        {"each", nn_objfndict_each},
        {"filter", nn_objfndict_filter},
        {"some", nn_objfndict_some},
        {"every", nn_objfndict_every},
        {"reduce", nn_objfndict_reduce},
        {"@iter", nn_objfndict_iter},
        {"@itern", nn_objfndict_itern},
        {nullptr, nullptr},
    };
    #if 0
    nn_class_defnativeconstructor(state->classprimdict, nn_objfndict_constructor);
    nn_class_defstaticnativemethod(state->classprimdict, nn_string_copycstr("keys"), nn_objfndict_keys);
    #endif
    nn_state_installmethods(state, state->classprimdict, dictmethods);
}


#
NNObjFile* nn_object_makefile(FILE* handle, bool isstd, const char* path, const char* mode)
{
    NNObjFile* file;
    file = (NNObjFile*)nn_object_allocobject(sizeof(NNObjFile), NEON_OBJTYPE_FILE, false);
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
    nn_gcmem_release(file, sizeof(NNObjFile));
}

void nn_file_mark(NNObjFile* file)
{
    nn_gcmem_markobject((NNObject*)file->mode);
    nn_gcmem_markobject((NNObject*)file->path);
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
            FILE_ERROR(Unsupported, "cannot read file in write mode");
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


#define FILE_ERROR(type, message) \
    NEON_RETURNERROR(#type " -> %s", message, nn_string_getdata(file->path));

#define RETURN_STATUS(status) \
    if((status) == 0) \
    { \
        return nn_value_makebool(true); \
    } \
    else \
    { \
        FILE_ERROR(File, strerror(errno)); \
    }

#define DENY_STD() \
    if(file->isstd) \
    NEON_RETURNERROR("method not supported for std files");

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

static NNValue nn_objfnfile_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    FILE* hnd;
    const char* path;
    const char* mode;
    NNObjString* opath;
    NNObjFile* file;
    (void)hnd;
    (void)thisval;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "constructor", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    opath = nn_value_asstring(argv[0]);
    if(nn_string_getlength(opath) == 0)
    {
        NEON_RETURNERROR("file path cannot be empty");
    }
    mode = "r";
    if(argc == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
        mode = nn_string_getdata(nn_value_asstring(argv[1]));
    }
    path = nn_string_getdata(opath);
    file = (NNObjFile*)nn_gcmem_protect(state, (NNObject*)nn_object_makefile(nullptr, false, path, mode));
    nn_fileobject_open(file);
    return nn_value_fromobject(file);
}

static NNValue nn_objfnfile_exists(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* file;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "exists", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = nn_value_asstring(argv[0]);
    return nn_value_makebool(nn_util_fsfileexists(nn_string_getdata(file)));
}

static NNValue nn_objfnfile_isfile(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* file;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "isfile", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = nn_value_asstring(argv[0]);
    return nn_value_makebool(nn_util_fsfileisfile(nn_string_getdata(file)));
}

static NNValue nn_objfnfile_isdirectory(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* file;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "isdirectory", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = nn_value_asstring(argv[0]);
    return nn_value_makebool(nn_util_fsfileisdirectory(nn_string_getdata(file)));
}


static NNValue nn_objfnfile_readstatic(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char* buf;
    size_t thismuch;
    size_t actualsz;
    NNObjString* filepath;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "read", argv, argc);
    thismuch = -1;
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    if(argc > 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        thismuch = (size_t)nn_value_asnumber(argv[1]);
    }
    filepath = nn_value_asstring(argv[0]);
    buf = nn_util_filereadfile(state, nn_string_getdata(filepath), &actualsz, true, thismuch);
    if(buf == nullptr)
    {
        nn_except_throwclass(state, state->exceptions.ioerror, "%s: %s", nn_string_getdata(filepath), strerror(errno));
        return nn_value_makenull();
    }
    return nn_value_fromobject(nn_string_takelen(buf, actualsz));
}


static NNValue nn_objfnfile_writestatic(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    bool appending;
    size_t rt;
    FILE* fh;
    const char* mode;
    NNObjString* filepath;
    NNObjString* data;
    NNArgCheck check;
    (void)thisval;
    appending = false;
    mode = "wb";
    nn_argcheck_init(state, &check, "write", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
    if(argc > 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 2, nn_value_isbool);
        appending = nn_value_asbool(argv[2]);
    }
    if(appending)
    {
        mode = "ab";
    }
    filepath = nn_value_asstring(argv[0]);
    data = nn_value_asstring(argv[1]);
    fh = fopen(nn_string_getdata(filepath), mode);
    if(fh == nullptr)
    {
        nn_except_throwclass(state, state->exceptions.ioerror, strerror(errno));
        return nn_value_makenull();
    }
    rt = fwrite(nn_string_getdata(data), sizeof(char), nn_string_getlength(data), fh);
    fclose(fh);
    return nn_value_makenumber(rt);
}




static NNValue nn_objfnfile_close(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "close", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_fileobject_close(nn_value_asfile(thisval));
    return nn_value_makenull();
}

static NNValue nn_objfnfile_open(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "open", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_fileobject_open(nn_value_asfile(thisval));
    return nn_value_makenull();
}

static NNValue nn_objfnfile_isopen(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    file = nn_value_asfile(thisval);
    return nn_value_makebool(file->isstd || file->isopen);
}

static NNValue nn_objfnfile_isclosed(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    file = nn_value_asfile(thisval);
    return nn_value_makebool(!file->isstd && !file->isopen);
}

static NNValue nn_objfnfile_readmethod(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t readhowmuch;
    NNIOResult res;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "read", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    readhowmuch = -1;
    if(argc == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        readhowmuch = (size_t)nn_value_asnumber(argv[0]);
    }
    file = nn_value_asfile(thisval);
    if(!nn_file_read(file, readhowmuch, &res))
    {
        FILE_ERROR(NotFound, strerror(errno));
    }
    return nn_value_fromobject(nn_string_takelen(res.data, res.length));
}


static NNValue nn_objfnfile_readline(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    long rdline;
    size_t linelen;
    char* strline;
    NNObjFile* file;
    NNArgCheck check;
    NNObjString* nos;
    nn_argcheck_init(state, &check, "readLine", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    file = nn_value_asfile(thisval);
    linelen = 0;
    strline = nullptr;
    rdline = nn_util_filegetlinehandle(&strline, &linelen, file->handle);
    if(rdline == -1)
    {
        return nn_value_makenull();
    }
    nos = nn_string_takelen(strline, rdline);
    return nn_value_fromobject(nos);
}

static NNValue nn_objfnfile_get(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int ch;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "get", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    ch = fgetc(file->handle);
    if(ch == EOF)
    {
        return nn_value_makenull();
    }
    return nn_value_makenumber(ch);
}

static NNValue nn_objfnfile_gets(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    long end;
    long length;
    long currentpos;
    size_t bytesread;
    char* buffer;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "gets", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    length = -1;
    if(argc == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        length = (size_t)nn_value_asnumber(argv[0]);
    }
    file = nn_value_asfile(thisval);
    if(!file->isstd)
    {
        if(!nn_util_fsfileexists(nn_string_getdata(file->path)))
        {
            FILE_ERROR(NotFound, "no such file or directory");
        }
        else if(strstr(nn_string_getdata(file->mode), "w") != nullptr && strstr(nn_string_getdata(file->mode), "+") == nullptr)
        {
            FILE_ERROR(Unsupported, "cannot read file in write mode");
        }
        if(!file->isopen)
        {
            FILE_ERROR(Read, "file not open");
        }
        else if(file->handle == nullptr)
        {
            FILE_ERROR(Read, "could not read file");
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
    buffer = (char*)nn_memory_malloc(sizeof(char) * (length + 1));
    if(buffer == nullptr && length != 0)
    {
        FILE_ERROR(Buffer, "not enough memory to read file");
    }
    bytesread = fread(buffer, sizeof(char), length, file->handle);
    if(bytesread == 0 && length != 0)
    {
        FILE_ERROR(Read, "could not read file contents");
    }
    if(buffer != nullptr)
    {
        buffer[bytesread] = '\0';
    }
    return nn_value_fromobject(nn_string_takelen(buffer, bytesread));
}

static NNValue nn_objfnfile_write(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t count;
    int length;
    unsigned char* data;
    NNObjFile* file;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "write", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    file = nn_value_asfile(thisval);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(argv[0]);
    data = (unsigned char*)nn_string_getdata(string);
    length = nn_string_getlength(string);
    if(!file->isstd)
    {
        if(strstr(nn_string_getdata(file->mode), "r") != nullptr && strstr(nn_string_getdata(file->mode), "+") == nullptr)
        {
            FILE_ERROR(Unsupported, "cannot write into non-writable file");
        }
        else if(length == 0)
        {
            FILE_ERROR(Write, "cannot write empty buffer to file");
        }
        else if(file->handle == nullptr || !file->isopen)
        {
            nn_fileobject_open(file);
        }
        else if(file->handle == nullptr)
        {
            FILE_ERROR(Write, "could not write to file");
        }
    }
    else
    {
        if(fileno(stdin) == file->number)
        {
            FILE_ERROR(Unsupported, "cannot write to input file");
        }
    }
    count = fwrite(data, sizeof(unsigned char), length, file->handle);
    fflush(file->handle);
    if(count > (size_t)0)
    {
        return nn_value_makebool(true);
    }
    return nn_value_makebool(false);
}

static NNValue nn_objfnfile_puts(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t count;
    int rc;
    int length;
    unsigned char* data;
    NNObjFile* file;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "puts", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    file = nn_value_asfile(thisval);

    if(!file->isstd)
    {
        if(strstr(nn_string_getdata(file->mode), "r") != nullptr && strstr(nn_string_getdata(file->mode), "+") == nullptr)
        {
            FILE_ERROR(Unsupported, "cannot write into non-writable file");
        }
        else if(!file->isopen)
        {
            FILE_ERROR(Write, "file not open");
        }
        else if(file->handle == nullptr)
        {
            FILE_ERROR(Write, "could not write to file");
        }
    }
    else
    {
        if(fileno(stdin) == file->number)
        {
            FILE_ERROR(Unsupported, "cannot write to input file");
        }
    }
    rc = 0;
    for(i=0; i<argc; i++)
    {
        NEON_ARGS_CHECKTYPE(&check, i, nn_value_isstring);
        string = nn_value_asstring(argv[i]);
        data = (unsigned char*)nn_string_getdata(string);
        length = nn_string_getlength(string);
        count = fwrite(data, sizeof(unsigned char), length, file->handle);
        if(count > (size_t)0 || length == 0)
        {
            return nn_value_makenumber(0);
        }
        rc += count;
    }
    return nn_value_makenumber(rc);
}

static NNValue nn_objfnfile_putc(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    int rc;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "puts", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    file = nn_value_asfile(thisval);
    if(!file->isstd)
    {
        if(strstr(nn_string_getdata(file->mode), "r") != nullptr && strstr(nn_string_getdata(file->mode), "+") == nullptr)
        {
            FILE_ERROR(Unsupported, "cannot write into non-writable file");
        }
        else if(!file->isopen)
        {
            FILE_ERROR(Write, "file not open");
        }
        else if(file->handle == nullptr)
        {
            FILE_ERROR(Write, "could not write to file");
        }
    }
    else
    {
        if(fileno(stdin) == file->number)
        {
            FILE_ERROR(Unsupported, "cannot write to input file");
        }
    }
    rc = 0;
    for(i=0; i<argc; i++)
    {
        NEON_ARGS_CHECKTYPE(&check, i, nn_value_isnumber);
        int cv = nn_value_asnumber(argv[i]);
        rc += fputc(cv, file->handle);
    }
    return nn_value_makenumber(rc);
}


static NNValue nn_objfnfile_printf(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNFormatInfo nfi;
    NNIOStream pr;
    NNObjString* ofmt;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "printf", argv, argc);
    file = nn_value_asfile(thisval);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = nn_value_asstring(argv[0]);
    nn_iostream_makestackio(&pr, file->handle, false);
    nn_strformat_init(state, &nfi, &pr, nn_string_getdata(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, argc, 1, argv))
    {
    }
    return nn_value_makenull();
}

static NNValue nn_objfnfile_number(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "number", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asfile(thisval)->number);
}

static NNValue nn_objfnfile_istty(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "istty", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    return nn_value_makebool(file->istty);
}

static NNValue nn_objfnfile_flush(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "flush", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    if(!file->isopen)
    {
        FILE_ERROR(Unsupported, "I/O operation on closed file");
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
    return nn_value_makenull();
}


static NNValue nn_objfnfile_path(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "path", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    DENY_STD();
    return nn_value_fromobject(file->path);
}

static NNValue nn_objfnfile_mode(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "mode", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    return nn_value_fromobject(file->mode);
}

static NNValue nn_objfnfile_name(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char* name;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "name", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    if(!file->isstd)
    {
        name = nn_util_fsgetbasename(nn_string_getdata(file->path));
        return nn_value_fromobject(nn_string_copycstr(name));
    }
    else if(file->istty)
    {
        return nn_value_fromobject(nn_string_copycstr("<tty>"));
    }
    return nn_value_makenull();
}

static NNValue nn_objfnfile_seek(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    long position;
    int seektype;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "seek", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    file = nn_value_asfile(thisval);
    DENY_STD();
    position = (long)nn_value_asnumber(argv[0]);
    seektype = nn_value_asnumber(argv[1]);
    RETURN_STATUS(fseek(file->handle, position, seektype));
}

static NNValue nn_objfnfile_tell(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "tell", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    DENY_STD();
    return nn_value_makenumber(ftell(file->handle));
}

#undef FILE_ERROR
#undef RETURN_STATUS
#undef DENY_STD


void nn_state_installobjectfile(NNState* state)
{
    static NNConstClassMethodItem filemethods[] =
    {
        {"close", nn_objfnfile_close},
        {"open", nn_objfnfile_open},
        {"read", nn_objfnfile_readmethod},
        {"get", nn_objfnfile_get},
        {"gets", nn_objfnfile_gets},
        {"write", nn_objfnfile_write},
        {"puts", nn_objfnfile_puts},
        {"putc", nn_objfnfile_putc},
        {"printf", nn_objfnfile_printf},
        {"number", nn_objfnfile_number},
        {"isTTY", nn_objfnfile_istty},
        {"isOpen", nn_objfnfile_isopen},
        {"isClosed", nn_objfnfile_isclosed},
        {"flush", nn_objfnfile_flush},
        {"path", nn_objfnfile_path},
        {"seek", nn_objfnfile_seek},
        {"tell", nn_objfnfile_tell},
        {"mode", nn_objfnfile_mode},
        {"name", nn_objfnfile_name},
        {"readLine", nn_objfnfile_readline},
        {nullptr, nullptr},
    };
    nn_class_defnativeconstructor(state->classprimfile, nn_objfnfile_constructor);
    nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr("read"), nn_objfnfile_readstatic);
    nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr("write"), nn_objfnfile_writestatic);
    nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr("put"), nn_objfnfile_writestatic);
    nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr("exists"), nn_objfnfile_exists);
    nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr("isFile"), nn_objfnfile_isfile);
    nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr("isDirectory"), nn_objfnfile_isdirectory);
    nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr("stat"), nn_modfn_os_stat);
    nn_state_installmethods(state, state->classprimfile, filemethods);
}


NNObjFunction* nn_object_makefuncgeneric(NNObjString* name, NNObjType fntype, NNValue thisval)
{
    NNObjFunction* ofn;
    ofn = (NNObjFunction*)nn_object_allocobject(sizeof(NNObjFunction), fntype, false);
    ofn->clsthisval = thisval;
    ofn->name = name;
    return ofn;

}

NNObjFunction* nn_object_makefuncbound(NNValue receiver, NNObjFunction* method)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncgeneric(method->name, NEON_OBJTYPE_FUNCBOUND, nn_value_makenull());
    ofn->fnmethod.receiver = receiver;
    ofn->fnmethod.method = method;
    return ofn;
}

NNObjFunction* nn_object_makefuncscript(NNObjModule* module, NNFuncContextType type)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncgeneric(nn_string_intern("<script>"), NEON_OBJTYPE_FUNCSCRIPT, nn_value_makenull());
    ofn->fnscriptfunc.arity = 0;
    ofn->upvalcount = 0;
    ofn->fnscriptfunc.isvariadic = false;
    ofn->name = nullptr;
    ofn->contexttype = type;
    ofn->fnscriptfunc.module = module;
    nn_blob_init(&ofn->fnscriptfunc.blob);
    return ofn;
}

void nn_funcscript_destroy(NNObjFunction* ofn)
{
    nn_blob_destroy(&ofn->fnscriptfunc.blob);
}

NNObjFunction* nn_object_makefuncnative(NNNativeFN natfn, const char* name, void* uptr)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncgeneric(nn_string_copycstr(name), NEON_OBJTYPE_FUNCNATIVE, nn_value_makenull());
    ofn->fnnativefunc.natfunc = natfn;
    ofn->contexttype = NEON_FNCONTEXTTYPE_FUNCTION;
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
        upvals = (NNObjUpvalue**)nn_gcmem_allocate(sizeof(NNObjUpvalue*), innerfn->upvalcount + 1, false);
        for(i = 0; i < innerfn->upvalcount; i++)
        {
            upvals[i] = nullptr;
        }
    }
    ofn = nn_object_makefuncgeneric(innerfn->name, NEON_OBJTYPE_FUNCCLOSURE, thisval);
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

void nn_import_loadbuiltinmodules(NNState* state)
{
    int i;
    static NNModInitFN g_builtinmodules[] =
    {
        nn_natmodule_load_null,
        nn_natmodule_load_os,
        nn_natmodule_load_astscan,
        nn_natmodule_load_complex,
        nullptr,
    };
    for(i = 0; g_builtinmodules[i] != nullptr; i++)
    {
        nn_import_loadnativemodule(state, g_builtinmodules[i], nullptr, "<__native__>", nullptr);
    }
}


bool nn_state_addmodulesearchpathobj(NNState* state, NNObjString* os)
{
    state->importpath.push(nn_value_fromobject(os));
    return true;
}

bool nn_state_addmodulesearchpath(NNState* state, const char* path)
{
    return nn_state_addmodulesearchpathobj(state, nn_string_copycstr(path));
}

void nn_state_setupmodulepaths(NNState* state)
{
    int i;
    static const char* defaultsearchpaths[] =
    {
        "mods",
        "mods/@/index" NEON_CONFIG_FILEEXT,
        ".",
        nullptr
    };
    state->importpath.initSelf();
    nn_state_addmodulesearchpathobj(state, state->processinfo->cliexedirectory);
    for(i=0; defaultsearchpaths[i]!=nullptr; i++)
    {
        nn_state_addmodulesearchpath(state, defaultsearchpaths[i]);
    }
}

void nn_module_setfilefield(NNState* state, NNObjModule* module)
{
    return;
    module->deftable.set(nn_value_fromobject(nn_string_intern("__file__")), nn_value_fromobject(nn_string_copyobject(module->physicalpath)));
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

NNObjModule* nn_import_loadmodulescript(NNState* state, NNObjModule* intomodule, NNObjString* modulename)
{
    int argc;
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
    (void)argc;
    (void)intomodule;
    field = GCSingleton::get()->openedmodules.getfieldbyostr(modulename);
    if(field != nullptr)
    {
        return nn_value_asmodule((NNValue)field->value);
    }
    physpath = nn_import_resolvepath(state, nn_string_getdata(modulename), nn_string_getdata(intomodule->physicalpath), nullptr, false);
    if(physpath == nullptr)
    {
        nn_except_throwclass(state, state->exceptions.importerror, "module not found: '%s'", nn_string_getdata(modulename));
        return nullptr;
    }
    fprintf(stderr, "loading module from '%s'\n", physpath);
    source = nn_util_filereadfile(state, physpath, &fsz, false, 0);
    if(source == nullptr)
    {
        nn_except_throwclass(state, state->exceptions.importerror, "could not read import file %s", physpath);
        return nullptr;
    }
    nn_blob_init(&blob);
    module = nn_module_make(nn_string_getdata(modulename), physpath, true, true);
    nn_memory_free(physpath);
    function = nn_astparser_compilesource(state, module, source, &blob, true, false);
    nn_memory_free(source);
    closure = nn_object_makefuncclosure(function, nn_value_makenull());
    callable = nn_value_fromobject(closure);
    nn_nestcall_prepare(callable, nn_value_makenull(), nullptr, 0);     
    if(!nn_nestcall_callfunction(state, callable, nn_value_makenull(), nullptr, 0, &retv, false))
    {
        nn_blob_destroy(&blob);
        nn_except_throwclass(state, state->exceptions.importerror, "failed to call compiled import closure");
        return nullptr;
    }
    nn_blob_destroy(&blob);
    return module;
}

char* nn_import_resolvepath(NNState* state, const   char* modulename, const char* currentfile, char* rootfile, bool isrelative)
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
    mlen = strlen(modulename);
    splen = state->importpath.count();
    pathbuf = nn_strbuf_makebasicempty(nullptr, 0);
    for(i=0; i<splen; i++)
    {
        pitem = nn_value_asstring(state->importpath.get(i));
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


bool nn_import_loadnativemodule(NNState* state, NNModInitFN init_fn, char* importname, const char* source, void* dlw)
{
    size_t j;
    size_t k;
    size_t slen;
    NNValue v;
    NNValue fieldname;
    NNValue funcname;
    NNValue funcrealvalue;
    NNDefFunc func;
    NNDefField field;
    NNDefModule* defmod;
    NNObjModule* targetmod;
    NNDefClass klassreg;
    NNObjString* classname;
    NNObjFunction* native;
    NNObjClass* klass;
    defmod = init_fn();
    if(defmod != nullptr)
    {
        targetmod = (NNObjModule*)nn_gcmem_protect(state, (NNObject*)nn_module_make((char*)defmod->name, source, false, true));
        targetmod->fnpreloaderptr = (void*)defmod->fnpreloaderfunc;
        targetmod->fnunloaderptr = (void*)defmod->fnunloaderfunc;
        if(defmod->definedfields != nullptr)
        {
            for(j = 0; defmod->definedfields[j].name != nullptr; j++)
            {
                field = defmod->definedfields[j];
                fieldname = nn_value_fromobject(nn_gcmem_protect(state, (NNObject*)nn_string_copycstr(field.name)));
                v = field.fieldvalfn(state, nn_value_makenull(), nullptr, 0);
                nn_vm_stackpush(state, v);
                targetmod->deftable.set(fieldname, v);
                nn_vm_stackpop(state);
            }
        }
        if(defmod->definedfunctions != nullptr)
        {
            for(j = 0; defmod->definedfunctions[j].name != nullptr; j++)
            {
                func = defmod->definedfunctions[j];
                funcname = nn_value_fromobject(nn_gcmem_protect(state, (NNObject*)nn_string_copycstr(func.name)));
                funcrealvalue = nn_value_fromobject(nn_gcmem_protect(state, (NNObject*)nn_object_makefuncnative(func.function, func.name, nullptr)));
                nn_vm_stackpush(state, funcrealvalue);
                targetmod->deftable.set(funcname, funcrealvalue);
                nn_vm_stackpop(state);
            }
        }
        if(defmod->definedclasses != nullptr)
        {
            for(j = 0; ((defmod->definedclasses[j].name != nullptr) && (defmod->definedclasses[j].defpubfunctions != nullptr)); j++)
            {
                klassreg = defmod->definedclasses[j];
                classname = (NNObjString*)nn_gcmem_protect(state, (NNObject*)nn_string_copycstr(klassreg.name));
                klass = (NNObjClass*)nn_gcmem_protect(state, (NNObject*)nn_object_makeclass(classname, state->classprimobject));
                if(klassreg.defpubfunctions != nullptr)
                {
                    for(k = 0; klassreg.defpubfunctions[k].name != nullptr; k++)
                    {
                        func = klassreg.defpubfunctions[k];
                        slen = strlen(func.name);
                        funcname = nn_value_fromobject(nn_gcmem_protect(state, (NNObject*)nn_string_copycstr(func.name)));
                        native = (NNObjFunction*)nn_gcmem_protect(state, (NNObject*)nn_object_makefuncnative(func.function, func.name, nullptr));
                        if(func.isstatic)
                        {
                            native->contexttype = NEON_FNCONTEXTTYPE_STATIC;
                        }
                        else if(slen > 0 && func.name[0] == '_')
                        {
                            native->contexttype = NEON_FNCONTEXTTYPE_PRIVATE;
                        }
                        if(strncmp(func.name, "constructor", slen) == 0)
                        {
                            klass->constructor = nn_value_fromobject(native);
                        }
                        else
                        {
                            klass->instmethods.set(funcname, nn_value_fromobject(native));
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
                targetmod->deftable.set(nn_value_fromobject(classname), nn_value_fromobject(klass));
            }
        }
        if(dlw != nullptr)
        {
            targetmod->handle = dlw;
        }
        nn_import_addnativemodule(state, targetmod, nn_string_getdata(targetmod->name));
        GCSingleton::clearGCProtect();
        return true;
    }
    else
    {
        nn_state_warn(state, "Error loading module: %s\n", importname);
    }
    return false;
}

void nn_import_addnativemodule(NNState* state, NNObjModule* module, const char* as)
{
    NNValue name;
    if(as != nullptr)
    {
        module->name = nn_string_copycstr(as);
    }
    name = nn_value_fromobject(nn_string_copyobject(module->name));
    nn_vm_stackpush(state, name);
    nn_vm_stackpush(state, nn_value_fromobject(module));
    GCSingleton::get()->openedmodules.set(name, nn_value_fromobject(module));
    nn_vm_stackpopn(state, 2);
}


void nn_import_closemodule(void* hnd)
{
    (void)hnd;
}



static NNValue nn_objfnnumber_tohexstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)argv;
    (void)argc;
    return nn_value_fromobject(nn_util_numbertohexstring(state, nn_value_asnumber(thisval), false));
}


static NNValue nn_objfnmath_hypot(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    SCRIPTFN_UNUSED(state);
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(hypot(nn_value_asnumber(argv[0]), nn_value_asnumber(argv[1])));
}


static NNValue nn_objfnmath_abs(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    SCRIPTFN_UNUSED(state);
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(fabs(nn_value_asnumber(argv[0])));
}

static NNValue nn_objfnmath_round(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    SCRIPTFN_UNUSED(state);
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(round(nn_value_asnumber(argv[0])));
}

static NNValue nn_objfnmath_sqrt(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    SCRIPTFN_UNUSED(state);
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(sqrt(nn_value_asnumber(argv[0])));
}

static NNValue nn_objfnmath_ceil(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    SCRIPTFN_UNUSED(state);
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(ceil(nn_value_asnumber(argv[0])));
}

static NNValue nn_objfnmath_floor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    SCRIPTFN_UNUSED(state);
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(floor(nn_value_asnumber(argv[0])));
}

static NNValue nn_objfnmath_min(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    double b;
    double x;
    double y;
    SCRIPTFN_UNUSED(state);
    (void)thisval;
    (void)argc;
    x = nn_value_asnumber(argv[0]);
    y = nn_value_asnumber(argv[1]);
    b = (x < y) ? x : y;
    return nn_value_makenumber(b);
}

static NNValue nn_objfnnumber_tobinstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)argv;
    (void)argc;
    return nn_value_fromobject(nn_util_numbertobinstring(state, nn_value_asnumber(thisval)));
}

static NNValue nn_objfnnumber_tooctstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)argv;
    (void)argc;
    return nn_value_fromobject(nn_util_numbertooctstring(state, nn_value_asnumber(thisval), false));
}

static NNValue nn_objfnnumber_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue val;
    NNValue rtval;
    NNObjString* os;
    (void)thisval;
    if(argc == 0)
    {
        return nn_value_makenumber(0);
    }
    val = argv[0];
    if(nn_value_isnumber(val))
    {
        return val;
    }
    if(val.isNull())
    {
        return nn_value_makenumber(0);
    }
    if(!nn_value_isstring(val))
    {
        NEON_RETURNERROR("Number() expects no arguments, or number, or string");
    }
    NNAstToken tok;
    NNAstLexer lex;
    os = nn_value_asstring(val);
    nn_astlex_init(&lex, state, nn_string_getdata(os));
    tok = nn_astlex_scannumber(&lex);
    rtval = nn_astparser_compilestrnumber(tok.type, tok.start);
    return rtval;
}

void nn_state_installobjectnumber(NNState* state)
{
    static NNConstClassMethodItem numbermethods[] =
    {
        {"toHexString", nn_objfnnumber_tohexstring},
        {"toOctString", nn_objfnnumber_tooctstring},
        {"toBinString", nn_objfnnumber_tobinstring},
        {nullptr, nullptr},
    };
    nn_class_defnativeconstructor(state->classprimnumber, nn_objfnnumber_constructor);
    nn_state_installmethods(state, state->classprimnumber, numbermethods);
}

void nn_state_installmodmath(NNState* state)
{
    NNObjClass* klass;
    klass = nn_util_makeclass(state, "Math", state->classprimobject);
    nn_class_defstaticnativemethod(klass, nn_string_intern("hypot"), nn_objfnmath_hypot);
    nn_class_defstaticnativemethod(klass, nn_string_intern("abs"), nn_objfnmath_abs);
    nn_class_defstaticnativemethod(klass, nn_string_intern("round"), nn_objfnmath_round);
    nn_class_defstaticnativemethod(klass, nn_string_intern("sqrt"), nn_objfnmath_sqrt);
    nn_class_defstaticnativemethod(klass, nn_string_intern("ceil"), nn_objfnmath_ceil);
    nn_class_defstaticnativemethod(klass, nn_string_intern("floor"), nn_objfnmath_floor);
    nn_class_defstaticnativemethod(klass, nn_string_intern("min"), nn_objfnmath_min);
}






static NNValue nn_objfnobject_dumpself(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNIOStream pr;
    NNObjString* os;
    v = thisval;
    (void)argv;
    (void)argc;
    nn_iostream_makestackstring(&pr);
    nn_iostream_printvalue(&pr, v, true, false);
    os = nn_iostream_takestring(&pr);
    nn_iostream_destroy(&pr);
    return nn_value_fromobject(os);
}

static NNValue nn_objfnobject_tostring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNIOStream pr;
    NNObjString* os;
    (void)argv;
    (void)argc;
    v = thisval;
    nn_iostream_makestackstring(&pr);
    nn_iostream_printvalue(&pr, v, false, true);
    os = nn_iostream_takestring(&pr);
    nn_iostream_destroy(&pr);
    return nn_value_fromobject(os);
}

static NNValue nn_objfnobject_typename(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNObjString* os;
    (void)thisval;
    (void)argc;
    v = argv[0];
    os = nn_string_copycstr(nn_value_typename(v, false));
    return nn_value_fromobject(os);
}

static NNValue nn_objfnobject_getselfinstance(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    return thisval;
}

static NNValue nn_objfnobject_getselfclass(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    #if 0
        nn_vmdebug_printvalue(state, thisval, "<object>.class:thisval=");
    #endif
    if(nn_value_isinstance(thisval))
    {
        return nn_value_fromobject(nn_value_asinstance(thisval)->klass);
    }
    return nn_value_makenull();
}


static NNValue nn_objfnobject_isstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    v = thisval;
    return nn_value_makebool(nn_value_isstring(v));
}

static NNValue nn_objfnobject_isarray(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    v = thisval;
    return nn_value_makebool(nn_value_isarray(v));
}

static NNValue nn_objfnobject_isa(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNValue otherclval;
    NNObjClass* oclass;
    NNObjClass* selfclass;
    SCRIPTFN_UNUSED(state);
    (void)argc;
    v = thisval;
    otherclval = argv[0];
    if(nn_value_isclass(otherclval))
    {
        oclass = nn_value_asclass(otherclval);
        selfclass = nn_value_getclassfor(state, v);
        if(selfclass != nullptr)
        {
            return nn_value_makebool(nn_util_isinstanceof(selfclass, oclass));
        }
    }
    return nn_value_makebool(false);
}

static NNValue nn_objfnobject_iscallable(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    selfval = thisval;
    return (nn_value_makebool(
        nn_value_isclass(selfval) ||
        nn_value_isfuncscript(selfval) ||
        nn_value_isfuncclosure(selfval) ||
        nn_value_isfuncbound(selfval) ||
        nn_value_isfuncnative(selfval)
    ));
}

static NNValue nn_objfnobject_isbool(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isbool(selfval));
}

static NNValue nn_objfnobject_isnumber(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isnumber(selfval));
}

static NNValue nn_objfnobject_isint(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isnumber(selfval) && (((int)nn_value_asnumber(selfval)) == nn_value_asnumber(selfval)));
}

static NNValue nn_objfnobject_isdict(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isdict(selfval));
}

static NNValue nn_objfnobject_isobject(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isobject(selfval));
}

static NNValue nn_objfnobject_isfunction(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(
        nn_value_isfuncscript(selfval) ||
        nn_value_isfuncclosure(selfval) ||
        nn_value_isfuncbound(selfval) ||
        nn_value_isfuncnative(selfval)
    );
}

static NNValue nn_objfnobject_isiterable(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    bool isiterable;
    NNValue dummy;
    NNObjClass* klass;
    NNValue selfval;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    selfval = thisval;
    isiterable = nn_value_isarray(selfval) || nn_value_isdict(selfval) || nn_value_isstring(selfval);
    if(!isiterable && nn_value_isinstance(selfval))
    {
        klass = nn_value_asinstance(selfval)->klass;
        isiterable = klass->instmethods.get(nn_value_fromobject(nn_string_intern("@iter")), &dummy)
            && klass->instmethods.get(nn_value_fromobject(nn_string_intern("@itern")), &dummy);
    }
    return nn_value_makebool(isiterable);
}

static NNValue nn_objfnobject_isclass(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isclass(selfval));
}

static NNValue nn_objfnobject_isfile(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isfile(selfval));
}

static NNValue nn_objfnobject_isinstance(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isinstance(selfval));
}


static NNValue nn_objfnclass_getselfname(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    NNObjClass* klass;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    selfval = thisval;
    klass = nn_value_asclass(selfval);
    return nn_value_fromobject(klass->name);
}


void nn_state_installobjectobject(NNState* state)
{
    static NNConstClassMethodItem objectmethods[] =
    {
        {"dump", nn_objfnobject_dumpself},
        {"isa", nn_objfnobject_isa},
        {"toString", nn_objfnobject_tostring},
        {"isArray", nn_objfnobject_isarray},
        {"isString", nn_objfnobject_isstring},
        {"isCallable", nn_objfnobject_iscallable},
        {"isBool", nn_objfnobject_isbool},
        {"isNumber", nn_objfnobject_isnumber},
        {"isInt", nn_objfnobject_isint},
        {"isDict", nn_objfnobject_isdict},
        {"isObject", nn_objfnobject_isobject},
        {"isFunction", nn_objfnobject_isfunction},
        {"isIterable", nn_objfnobject_isiterable},
        {"isClass", nn_objfnobject_isclass},
        {"isFile", nn_objfnobject_isfile},
        {"isInstance", nn_objfnobject_isinstance},
        {nullptr, nullptr},
    };

    //nn_class_defcallablefield(state->classprimobject, nn_string_intern("class"), nn_objfnobject_getselfclass);

    nn_class_defstaticnativemethod(state->classprimobject, nn_string_intern("typename"), nn_objfnobject_typename);
    nn_class_defstaticcallablefield(state->classprimobject, nn_string_intern("prototype"), nn_objfnobject_getselfinstance);
    nn_state_installmethods(state, state->classprimobject, objectmethods);
    {
        nn_class_defstaticcallablefield(state->classprimclass, nn_string_intern("name"), nn_objfnclass_getselfname);
    }
}


static NNValue nn_objfnprocess_exedirectory(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)thisval;
    (void)argv;
    (void)argc;
    if(state->processinfo->cliexedirectory != nullptr)
    {
        return nn_value_fromobject(state->processinfo->cliexedirectory);
    }
    return nn_value_makenull();
}

static NNValue nn_objfnprocess_scriptfile(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)thisval;
    (void)argv;
    (void)argc;
    if(state->processinfo->cliscriptfile != nullptr)
    {
        return nn_value_fromobject(state->processinfo->cliscriptfile);
    }
    return nn_value_makenull();
}


static NNValue nn_objfnprocess_scriptdirectory(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)thisval;
    (void)argv;
    (void)argc;
    if(state->processinfo->cliscriptdirectory != nullptr)
    {
        return nn_value_fromobject(state->processinfo->cliscriptdirectory);
    }
    return nn_value_makenull();
}

static NNValue nn_objfnprocess_exit(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int rc;
    SCRIPTFN_UNUSED(state);
    (void)thisval;
    rc = 0;
    if(argc > 0)
    {
        rc = nn_value_asnumber(argv[0]);
    }
    exit(rc);
    return nn_value_makenull();
}

static NNValue nn_objfnprocess_kill(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int pid;
    int code;
    SCRIPTFN_UNUSED(state);
    (void)thisval;
    (void)argc;
    pid = nn_value_asnumber(argv[0]);
    code = nn_value_asnumber(argv[1]);
    osfn_kill(pid, code);
    return nn_value_makenull();
}

void nn_state_installobjectprocess(NNState* state)
{
    NNObjClass* klass;
    klass = state->classprimprocess;
    nn_class_setstaticproperty(klass, nn_string_copycstr("directory"), nn_value_fromobject(state->processinfo->cliexedirectory));
    nn_class_setstaticproperty(klass, nn_string_copycstr("env"), nn_value_fromobject(state->envdict));
    nn_class_setstaticproperty(klass, nn_string_copycstr("stdin"), nn_value_fromobject(state->processinfo->filestdin));
    nn_class_setstaticproperty(klass, nn_string_copycstr("stdout"), nn_value_fromobject(state->processinfo->filestdout));
    nn_class_setstaticproperty(klass, nn_string_copycstr("stderr"), nn_value_fromobject(state->processinfo->filestderr));
    nn_class_setstaticproperty(klass, nn_string_copycstr("pid"), nn_value_makenumber(state->processinfo->cliprocessid));
    nn_class_defstaticnativemethod(klass, nn_string_copycstr("kill"), nn_objfnprocess_kill);
    nn_class_defstaticnativemethod(klass, nn_string_copycstr("exit"), nn_objfnprocess_exit);
    nn_class_defstaticnativemethod(klass, nn_string_copycstr("exedirectory"), nn_objfnprocess_exedirectory);
    nn_class_defstaticnativemethod(klass, nn_string_copycstr("scriptdirectory"), nn_objfnprocess_scriptdirectory);
    nn_class_defstaticnativemethod(klass, nn_string_copycstr("script"), nn_objfnprocess_scriptfile);
}



static NNValue nn_objfnrange_lower(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "lower", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asrange(thisval)->lower);
}

static NNValue nn_objfnrange_upper(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "upper", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asrange(thisval)->upper);
}

static NNValue nn_objfnrange_range(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "range", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asrange(thisval)->range);
}

static NNValue nn_objfnrange_iter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int val;
    int index;
    NNObjRange* range;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "iter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    range = nn_value_asrange(thisval);
    index = nn_value_asnumber(argv[0]);
    if(index >= 0 && index < range->range)
    {
        if(index == 0)
        {
            return nn_value_makenumber(range->lower);
        }
        if(range->lower > range->upper)
        {
            val = --range->lower;
        }
        else
        {
            val = ++range->lower;
        }
        return nn_value_makenumber(val);
    }
    return nn_value_makenull();
}

static NNValue nn_objfnrange_itern(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int index;
    NNObjRange* range;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "itern", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    range = nn_value_asrange(thisval);
    if(argv[0].isNull())
    {
        if(range->range == 0)
        {
            return nn_value_makenull();
        }
        return nn_value_makenumber(0);
    }
    if(!nn_value_isnumber(argv[0]))
    {
        NEON_RETURNERROR("ranges are numerically indexed");
    }
    index = (int)nn_value_asnumber(argv[0]) + 1;
    if(index < range->range)
    {
        return nn_value_makenumber(index);
    }
    return nn_value_makenull();
}

static NNValue nn_objfnrange_expand(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int i;
    NNValue val;
    NNObjRange* range;
    NNObjArray* oa;
    (void)argv;
    (void)argc;
    range = nn_value_asrange(thisval);
    oa = nn_object_makearray();
    for(i = 0; i < range->range; i++)
    {
        val = nn_value_makenumber(i);
        nn_array_push(oa, val);
    }
    return nn_value_fromobject(oa);
}

static NNValue nn_objfnrange_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int a;
    int b;
    NNObjRange* orng;
    (void)thisval;
    (void)argc;
    a = nn_value_asnumber(argv[0]);
    b = nn_value_asnumber(argv[1]);
    orng = nn_object_makerange(a, b);
    return nn_value_fromobject(orng);
}

void nn_state_installobjectrange(NNState* state)
{    
    static NNConstClassMethodItem rangemethods[] =
    {
        {"lower", nn_objfnrange_lower},
        {"upper", nn_objfnrange_upper},
        {"range", nn_objfnrange_range},
        {"expand", nn_objfnrange_expand},
        {"toArray", nn_objfnrange_expand},
        {"@iter", nn_objfnrange_iter},
        {"@itern", nn_objfnrange_itern},
        {nullptr, nullptr},
    };
    nn_class_defnativeconstructor(state->classprimrange, nn_objfnrange_constructor);
    nn_state_installmethods(state, state->classprimrange, rangemethods);    
}

static NNValue nn_objfnstring_utf8numbytes(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int incode;
    int res;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "utf8NumBytes", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    incode = nn_value_asnumber(argv[0]);
    res = nn_util_utf8numbytes(incode);
    return nn_value_makenumber(res);
}

static NNValue nn_objfnstring_utf8decode(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int res;
    NNObjString* instr;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "utf8Decode", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    instr = nn_value_asstring(argv[0]);
    res = nn_util_utf8decode((const uint8_t*)nn_string_getdata(instr), nn_string_getlength(instr));
    return nn_value_makenumber(res);
}

static NNValue nn_objfnstring_utf8encode(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int incode;
    size_t len;
    NNObjString* res;
    char* buf;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "utf8Encode", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    incode = nn_value_asnumber(argv[0]);
    buf = nn_util_utf8encode(incode, &len);
    res = nn_string_takelen(buf, len);
    return nn_value_fromobject(res);
}

static NNValue nn_util_stringutf8chars(NNState* state, NNValue thisval, NNValue* argv, size_t argc, bool onlycodepoint)
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
    instr = nn_value_asstring(thisval);
    if(argc > 0)
    {
        havemax = true;
        maxamount = nn_value_asnumber(argv[0]);
    }
    res = nn_array_make();
    nn_utf8iter_init(&iter, nn_string_getdata(instr), nn_string_getlength (instr));
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
            nn_array_push(res, nn_value_makenumber(cp));
        }
        else
        {
            os = nn_string_copylen(cstr, iter.charsize);
            nn_array_push(res, nn_value_fromobject(os));
        }
    }
    finalize:
    return nn_value_fromobject(res);
}

static NNValue nn_objfnstring_utf8chars(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    return nn_util_stringutf8chars(state, thisval, argv, argc, false);
}

static NNValue nn_objfnstring_utf8codepoints(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    return nn_util_stringutf8chars(state, thisval, argv, argc, true);
}


static NNValue nn_objfnstring_fromcharcode(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char ch;
    NNObjString* os;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "fromCharCode", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    ch = nn_value_asnumber(argv[0]);
    os = nn_string_copylen(&ch, 1);
    return nn_value_fromobject(os);
}

static NNValue nn_objfnstring_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* os;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "constructor", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    os = nn_string_internlen("", 0);
    return nn_value_fromobject(os);
}

static NNValue nn_objfnstring_length(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    NNObjString* selfstr;
    nn_argcheck_init(state, &check, "length", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    return nn_value_makenumber(nn_string_getlength(selfstr));
}

static NNValue nn_string_fromrange(NNState* state, const char* buf, int len)
{
    NNObjString* str;
    if(len <= 0)
    {
        return nn_value_fromobject(nn_string_internlen("", 0));
    }
    str = nn_string_internlen("", 0);
    nn_string_appendstringlen(str, buf, len);
    return nn_value_fromobject(str);
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

static NNValue nn_objfnstring_substring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t end;
    size_t start;
    size_t maxlen;
    NNObjString* nos;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "substring", argv, argc);
    selfstr = nn_value_asstring(thisval);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    maxlen = nn_string_getlength(selfstr);
    end = maxlen;
    start = nn_value_asnumber(argv[0]);
    if(argc > 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        end = nn_value_asnumber(argv[1]);
    }
    nos = nn_string_substring(selfstr, start, end, true);
    return nn_value_fromobject(nos);
}

static NNValue nn_objfnstring_charcodeat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int ch;
    int idx;
    int selflen;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "charCodeAt", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    selfstr = nn_value_asstring(thisval);
    idx = nn_value_asnumber(argv[0]);
    selflen = (int)nn_string_getlength(selfstr);
    if((idx < 0) || (idx >= selflen))
    {
        ch = -1;
    }
    else
    {
        ch = nn_string_get(selfstr, idx);
    }
    return nn_value_makenumber(ch);
}

static NNValue nn_objfnstring_charat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char ch;
    int idx;
    int selflen;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "charAt", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    selfstr = nn_value_asstring(thisval);
    idx = nn_value_asnumber(argv[0]);
    selflen = (int)nn_string_getlength(selfstr);
    if((idx < 0) || (idx >= selflen))
    {
        return nn_value_fromobject(nn_string_internlen("", 0));
    }
    else
    {
        ch = nn_string_get(selfstr, idx);
    }
    return nn_value_fromobject(nn_string_copylen(&ch, 1));
}

static NNValue nn_objfnstring_upper(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t slen;
    char* string;
    NNObjString* str;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "upper", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    str = nn_value_asstring(thisval);
    slen = nn_string_getlength(str);
    string = nn_util_strtoupper(nn_string_mutdata(str), slen);
    return nn_value_fromobject(nn_string_copylen(string, slen));
}

static NNValue nn_objfnstring_lower(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t slen;
    char* string;
    NNObjString* str;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "lower", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    str = nn_value_asstring(thisval);
    slen = nn_string_getlength(str);
    string = nn_util_strtolower(nn_string_mutdata(str), slen);
    return nn_value_fromobject(nn_string_copylen(string, slen));
}

static NNValue nn_objfnstring_isalpha(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t len;
    NNArgCheck check;
    NNObjString* selfstr;
    nn_argcheck_init(state, &check, "isAlpha", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!isalpha((unsigned char)nn_string_get(selfstr, i)))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(nn_string_getlength(selfstr) != 0);
}

static NNValue nn_objfnstring_isalnum(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t len;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isAlnum", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!isalnum((unsigned char)nn_string_get(selfstr, i)))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(nn_string_getlength(selfstr) != 0);
}

static NNValue nn_objfnstring_isfloat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    double f;
    char* p;
    NNObjString* selfstr;
    NNArgCheck check;
    (void)f;
    nn_argcheck_init(state, &check, "isFloat", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    errno = 0;
    if(nn_string_getlength(selfstr) ==0)
    {
        return nn_value_makebool(false);
    }
    f = strtod(nn_string_getdata(selfstr), &p);
    if(errno)
    {
        return nn_value_makebool(false);
    }
    else
    {
        if(*p == 0)
        {
            return nn_value_makebool(true);
        }
    }
    return nn_value_makebool(false);
}

static NNValue nn_objfnstring_isnumber(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t len;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isNumber", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!isdigit((unsigned char)nn_string_get(selfstr, i)))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(nn_string_getlength(selfstr) != 0);
}

static NNValue nn_objfnstring_islower(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t len;
    bool alphafound;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isLower", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
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
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(alphafound);
}

static NNValue nn_objfnstring_isupper(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t len;
    bool alphafound;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isUpper", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
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
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(alphafound);
}

static NNValue nn_objfnstring_isspace(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t len;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isSpace", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    len = nn_string_getlength(selfstr);
    for(i = 0; i < len; i++)
    {
        if(!isspace((unsigned char)nn_string_get(selfstr, i)))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(nn_string_getlength(selfstr) != 0);
}

static NNValue nn_objfnstring_trim(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char trimmer;
    char* end;
    char* string;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "trim", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(argc == 1)
    {
        trimmer = (char)nn_string_get(nn_value_asstring(argv[0]), 0);
    }
    selfstr = nn_value_asstring(thisval);
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
        return nn_value_fromobject(nn_string_internlen("", 0));
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
    return nn_value_fromobject(nn_string_copycstr(string));
}

static NNValue nn_objfnstring_ltrim(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char trimmer;
    char* end;
    char* string;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "ltrim", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(argc == 1)
    {
        trimmer = (char)nn_string_get(nn_value_asstring(argv[0]), 0);
    }
    selfstr = nn_value_asstring(thisval);
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
        return nn_value_fromobject(nn_string_internlen("", 0));
    }
    end = string + strlen(string) - 1;
    end[1] = '\0';
    return nn_value_fromobject(nn_string_copycstr(string));
}

static NNValue nn_objfnstring_rtrim(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char trimmer;
    char* end;
    char* string;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "rtrim", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(argc == 1)
    {
        trimmer = (char)nn_string_get(nn_value_asstring(argv[0]), 0);
    }
    selfstr = nn_value_asstring(thisval);
    string = nn_string_mutdata(selfstr);
    end = nullptr;
    /* All spaces? */
    if(*string == 0)
    {
        return nn_value_fromobject(nn_string_internlen("", 0));
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
    return nn_value_fromobject(nn_string_copycstr(string));
}

static NNValue nn_objfnstring_indexof(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int startindex;
    char* result;
    const char* haystack;
    NNObjString* string;
    NNObjString* needle;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "indexOf", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    needle = nn_value_asstring(argv[0]);
    startindex = 0;
    if(argc == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        startindex = nn_value_asnumber(argv[1]);
    }
    if(nn_string_getlength(string) > 0 && nn_string_getlength(needle) > 0)
    {
        haystack = nn_string_getdata(string);
        result = (char*)strstr(haystack + startindex, nn_string_getdata(needle));
        if(result != nullptr)
        {
            return nn_value_makenumber((int)(result - haystack));
        }
    }
    return nn_value_makenumber(-1);
}

static NNValue nn_objfnstring_startswith(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* substr;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "startsWith", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    substr = nn_value_asstring(argv[0]);
    if(nn_string_getlength(string) == 0 || nn_string_getlength(substr) == 0 || nn_string_getlength(substr) > nn_string_getlength(string))
    {
        return nn_value_makebool(false);
    }
    return nn_value_makebool(memcmp(nn_string_getdata(substr), nn_string_getdata(string), nn_string_getlength(substr)) == 0);
}

static NNValue nn_objfnstring_endswith(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int difference;
    NNObjString* substr;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "endsWith", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    substr = nn_value_asstring(argv[0]);
    if(nn_string_getlength(string) == 0 || nn_string_getlength(substr) == 0 || nn_string_getlength(substr) > nn_string_getlength(string))
    {
        return nn_value_makebool(false);
    }
    difference = nn_string_getlength(string) - nn_string_getlength(substr);
    return nn_value_makebool(memcmp(nn_string_getdata(substr), nn_string_getdata(string) + difference, nn_string_getlength(substr)) == 0);
}

static NNValue nn_util_stringregexmatch(NNState* state, NNObjString* string, NNObjString* pattern, bool capture)
{
    enum {
        matchMaxTokens = 128*4,
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
    memset(tokens, 0, (matchMaxTokens+1) * sizeof(RegexToken));
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
                for(i=0; i<cpres; i++)
                {
                    mtstart = capstarts[i];
                    mtlength = caplengths[i];
                    if(mtlength > 0)
                    {
                        strstart = &nn_string_getdata(string)[mtstart];
                        rstr = nn_string_copylen(strstart, mtlength);
                        dm = nn_object_makedict();
                        nn_dict_addentrycstr(dm, "string", nn_value_fromobject(rstr));
                        nn_dict_addentrycstr(dm, "start", nn_value_makenumber(mtstart));
                        nn_dict_addentrycstr(dm, "length", nn_value_makenumber(mtlength));                        
                        nn_array_push(oa, nn_value_fromobject(dm));
                    }
                }
                return nn_value_fromobject(oa);
            }
            else
            {
                return nn_value_makebool(true);
            }
        }
    }
    else
    {
        nn_except_throwclass(state, state->exceptions.regexerror, pctx.errorbuf);
    }
    mrx_context_destroy(&pctx);
    if(capture)
    {
        return nn_value_makenull();
    }
    return nn_value_makebool(false);
}

static NNValue nn_objfnstring_matchcapture(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* pattern;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "match", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    pattern = nn_value_asstring(argv[0]);
    return nn_util_stringregexmatch(state, string, pattern, true);
}

static NNValue nn_objfnstring_matchonly(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* pattern;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "match", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    pattern = nn_value_asstring(argv[0]);
    return nn_util_stringregexmatch(state, string, pattern, false);
}

static NNValue nn_objfnstring_count(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int count;
    const char* tmp;
    NNObjString* substr;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "count", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    substr = nn_value_asstring(argv[0]);
    if(nn_string_getlength(substr) == 0 || nn_string_getlength(string) == 0)
    {
        return nn_value_makenumber(0);
    }
    count = 0;
    tmp = nn_string_getdata(string);
    while((tmp = nn_util_utf8strstr(tmp, nn_string_getdata(substr))))
    {
        count++;
        tmp++;
    }
    return nn_value_makenumber(count);
}

static NNValue nn_objfnstring_tonumber(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "toNumber", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    return nn_value_makenumber(strtod(nn_string_getdata(selfstr), nullptr));
}

static NNValue nn_objfnstring_isascii(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isAscii", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    if(argc == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isbool);
    }
    string = nn_value_asstring(thisval);
    return nn_value_fromobject(string);
}

static NNValue nn_objfnstring_tolist(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t end;
    size_t start;
    size_t length;
    NNObjArray* list;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "toList", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    string = nn_value_asstring(thisval);
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
    length = nn_string_getlength(string);
    if(length > 0)
    {
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            nn_array_push(list, nn_value_fromobject(nn_string_copylen(nn_string_getdata(string) + start, (int)(end - start))));
        }
    }
    return nn_value_fromobject(list);
}

static NNValue nn_objfnstring_tobytes(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t length;
    NNObjArray* list;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "toBytes", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    string = nn_value_asstring(thisval);
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
    length = nn_string_getlength(string);
    if(length > 0)
    {
        for(i = 0; i < length; i++)
        {
            nn_array_push(list, nn_value_makenumber(nn_string_get(string, i)));
        }
    }
    return nn_value_fromobject(list);
}

static NNValue nn_objfnstring_lpad(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_argcheck_init(state, &check, "lpad", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    string = nn_value_asstring(thisval);
    width = nn_value_asnumber(argv[0]);
    fillchar = ' ';
    if(argc == 2)
    {
        ofillstr = nn_value_asstring(argv[1]);
        fillchar = nn_string_get(ofillstr, 0);
    }
    if(width <= nn_string_getlength(string))
    {
        return thisval;
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
    return nn_value_fromobject(result);
}

static NNValue nn_objfnstring_rpad(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_argcheck_init(state, &check, "rpad", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    string = nn_value_asstring(thisval);
    width = nn_value_asnumber(argv[0]);
    fillchar = ' ';
    if(argc == 2)
    {
        ofillstr = nn_value_asstring(argv[1]);
        fillchar = nn_string_get(ofillstr, 0);
    }
    if(width <= nn_string_getlength(string))
    {
        return thisval;
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
    return nn_value_fromobject(result);
}

static NNValue nn_objfnstring_split(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t end;
    size_t start;
    size_t length;
    NNObjArray* list;
    NNObjString* string;
    NNObjString* delimeter;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "split", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    delimeter = nn_value_asstring(argv[0]);
    /* empty string matches empty string to empty list */
    if(((nn_string_getlength(string) == 0) && (nn_string_getlength(delimeter) == 0)) || (nn_string_getlength(string) == 0) || (nn_string_getlength(delimeter) == 0))
    {
        return nn_value_fromobject(nn_object_makearray());
    }
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray());
    if(nn_string_getlength(delimeter) > 0)
    {
        start = 0;
        for(i = 0; i <= nn_string_getlength(string); i++)
        {
            /* match found. */
            if(memcmp(nn_string_getdata(string) + i, nn_string_getdata(delimeter), nn_string_getlength(delimeter)) == 0 || i == nn_string_getlength(string))
            {
                nn_array_push(list, nn_value_fromobject(nn_string_copylen(nn_string_getdata(string) + start, i - start)));
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
            nn_array_push(list, nn_value_fromobject(nn_string_copylen(nn_string_getdata(string) + start, (int)(end - start))));
        }
    }
    return nn_value_fromobject(list);
}

static NNValue nn_objfnstring_replace(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_argcheck_init(state, &check, "replace", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 2, 3);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
    string = nn_value_asstring(thisval);
    substr = nn_value_asstring(argv[0]);
    repsubstr = nn_value_asstring(argv[1]);
    if((nn_string_getlength(string) == 0 && nn_string_getlength(substr) == 0) || nn_string_getlength(string) == 0 || nn_string_getlength(substr) == 0)
    {
        return nn_value_fromobject(nn_string_copylen(nn_string_getdata(string), nn_string_getlength(string)));
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
    return nn_value_fromobject(nn_string_makefromstrbuf(result, nn_util_hashstring(nn_strbuf_data(&result), xlen), xlen));
}

static NNValue nn_objfnstring_iter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    size_t length;
    NNObjString* string;
    NNObjString* result;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "iter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    string = nn_value_asstring(thisval);
    length = nn_string_getlength(string);
    index = nn_value_asnumber(argv[0]);
    if(((int)index > -1) && (index < length))
    {
        result = nn_string_copylen(&nn_string_getdata(string)[index], 1);
        return nn_value_fromobject(result);
    }
    return nn_value_makenull();
}

static NNValue nn_objfnstring_itern(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    size_t length;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "itern", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    string = nn_value_asstring(thisval);
    length = nn_string_getlength(string);
    if(argv[0].isNull())
    {
        if(length == 0)
        {
            return nn_value_makebool(false);
        }
        return nn_value_makenumber(0);
    }
    if(!nn_value_isnumber(argv[0]))
    {
        NEON_RETURNERROR("strings are numerically indexed");
    }
    index = nn_value_asnumber(argv[0]);
    if(index < length - 1)
    {
        return nn_value_makenumber((double)index + 1);
    }
    return nn_value_makenull();
}

static NNValue nn_objfnstring_each(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue callable;
    NNValue unused;
    NNObjString* string;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "each", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    string = nn_value_asstring(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(callable, thisval, nestargs, 2);
    for(i = 0; i < nn_string_getlength(string); i++)
    {
        passi = 0;
        if(arity > 0)
        {
            passi++;
            nestargs[0] = nn_value_fromobject(nn_string_copylen(nn_string_getdata(string) + i, 1));
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_value_makenumber(i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &unused, false);
    }
    /* pop the argument list */
    return nn_value_makenull();
}

static NNValue nn_objfnstring_appendany(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNValue arg;
    NNObjString* oss;
    NNObjString* selfstring;
    selfstring = nn_value_asstring(thisval);
    for(i = 0; i < argc; i++)
    {
        arg = argv[i];
        if(nn_value_isnumber(arg))
        {
            nn_string_appendbyte(selfstring, nn_value_asnumber(arg));
        }
        else
        {
            oss = nn_value_tostring(arg);
            nn_string_appendobject(selfstring, oss);
        }
    }
    /* pop the argument list */
    return thisval;
}

static NNValue nn_objfnstring_appendbytes(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNValue arg;
    NNObjString* selfstring;
    selfstring = nn_value_asstring(thisval);
    for(i = 0; i < argc; i++)
    {
        arg = argv[i];
        if(nn_value_isnumber(arg))
        {
            nn_string_appendbyte(selfstring, nn_value_asnumber(arg));
        }
        else
        {
            NEON_RETURNERROR("appendbytes expects number types");
        }
    }
    /* pop the argument list */
    return thisval;
}

void nn_state_installobjectstring(NNState* state)
{
    static NNConstClassMethodItem stringmethods[] =
    {
        {"@iter", nn_objfnstring_iter},
        {"@itern", nn_objfnstring_itern},
        {"size", nn_objfnstring_length},
        {"substr", nn_objfnstring_substring},
        {"substring", nn_objfnstring_substring},
        {"charCodeAt", nn_objfnstring_charcodeat},
        {"charAt", nn_objfnstring_charat},
        {"upper", nn_objfnstring_upper},
        {"lower", nn_objfnstring_lower},
        {"trim", nn_objfnstring_trim},
        {"ltrim", nn_objfnstring_ltrim},
        {"rtrim", nn_objfnstring_rtrim},
        {"split", nn_objfnstring_split},
        {"indexOf", nn_objfnstring_indexof},
        {"count", nn_objfnstring_count},
        {"toNumber", nn_objfnstring_tonumber},
        {"toList", nn_objfnstring_tolist},
        {"toBytes", nn_objfnstring_tobytes},
        {"lpad", nn_objfnstring_lpad},
        {"rpad", nn_objfnstring_rpad},
        {"replace", nn_objfnstring_replace},
        {"each", nn_objfnstring_each},
        {"startsWith", nn_objfnstring_startswith},
        {"endsWith", nn_objfnstring_endswith},
        {"isAscii", nn_objfnstring_isascii},
        {"isAlpha", nn_objfnstring_isalpha},
        {"isAlnum", nn_objfnstring_isalnum},
        {"isNumber", nn_objfnstring_isnumber},
        {"isFloat", nn_objfnstring_isfloat},
        {"isLower", nn_objfnstring_islower},
        {"isUpper", nn_objfnstring_isupper},
        {"isSpace", nn_objfnstring_isspace},
        {"utf8Chars", nn_objfnstring_utf8chars},
        {"utf8Codepoints", nn_objfnstring_utf8codepoints},
        {"utf8Bytes", nn_objfnstring_utf8codepoints},
        {"match", nn_objfnstring_matchcapture},
        {"matches", nn_objfnstring_matchonly},
        {"append", nn_objfnstring_appendany},
        {"push", nn_objfnstring_appendany},
        {"appendbytes", nn_objfnstring_appendbytes},
        {"appendbyte", nn_objfnstring_appendbytes},
        {nullptr, nullptr},
    };
    nn_class_defnativeconstructor(state->classprimstring, nn_objfnstring_constructor);
    nn_class_defstaticnativemethod(state->classprimstring, nn_string_intern("fromCharCode"), nn_objfnstring_fromcharcode);
    nn_class_defstaticnativemethod(state->classprimstring, nn_string_intern("utf8Decode"), nn_objfnstring_utf8decode);
    nn_class_defstaticnativemethod(state->classprimstring, nn_string_intern("utf8Encode"), nn_objfnstring_utf8encode);
    nn_class_defstaticnativemethod(state->classprimstring, nn_string_intern("utf8NumBytes"), nn_objfnstring_utf8numbytes);
    nn_class_defcallablefield(state->classprimstring, nn_string_intern("length"), nn_objfnstring_length);
    nn_state_installmethods(state, state->classprimstring, stringmethods);

}



static NNValue nn_modfn_astscan_scan(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    enum {
        /* 12 == "NEON_ASTTOK_".length */
        kTokPrefixLength = 12
    };

    const char* cstr;
    NNObjString* insrc;
    NNAstLexer* scn;
    NNObjArray* arr;
    NNObjDict* itm;
    NNAstToken token;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "scan", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    insrc = nn_value_asstring(argv[0]);
    scn = nn_astlex_make(state, nn_string_getdata(insrc));
    arr = nn_array_make();
    while(!nn_astlex_isatend(scn))
    {
        itm = nn_object_makedict();
        token = nn_astlex_scantoken(scn);
        nn_dict_addentrycstr(itm, "line", nn_value_makenumber(token.line));
        cstr = nn_astutil_toktype2str(token.type);
        nn_dict_addentrycstr(itm, "type", nn_value_fromobject(nn_string_copycstr(cstr + kTokPrefixLength)));
        nn_dict_addentrycstr(itm, "source", nn_value_fromobject(nn_string_copylen(token.start, token.length)));
        nn_array_push(arr, nn_value_fromobject(itm));
    }
    nn_astlex_destroy(scn);
    return nn_value_fromobject(arr);
}


NNDefModule* nn_natmodule_load_astscan()
{
    NNDefModule* ret;
    static NNDefFunc modfuncs[] =
    {
        {"scan",   true,  nn_modfn_astscan_scan},
        {nullptr,     false, nullptr},
    };
    static NNDefField modfields[] =
    {
        {nullptr,       false, nullptr},
    };
    static NNDefModule module;
    module.name = "astscan";
    module.definedfields = modfields;
    module.definedfunctions = modfuncs;
    module.definedclasses = nullptr;
    module.fnpreloaderfunc = nullptr;
    module.fnunloaderfunc = nullptr;
    ret = &module;
    return ret;
}




typedef struct NNUClassComplex NNUClassComplex;
struct NNUClassComplex
{
    NNObjInstance selfinstance;
    double re;
    double im;
};

static NNValue nn_pcomplex_makeinstance(NNState* state, NNObjClass* klass, double re, double im)
{
    NNUClassComplex* inst;
    inst = (NNUClassComplex*)nn_object_makeinstancesize(klass, sizeof(NNUClassComplex));
    inst->re = re;
    inst->im = im;
    return nn_value_fromobject((NNObjInstance*)inst);
    
}

static NNValue nn_complexclass_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNUClassComplex* inst;
    (void)thisval;
    (void)argc;
    assert(nn_value_isinstance(thisval));
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    return nn_pcomplex_makeinstance(state, ((NNObjInstance*)inst)->klass, nn_value_asnumber(argv[0]), nn_value_asnumber(argv[1]));
}

static NNValue nn_complexclass_opadd(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue vother;
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    NNUClassComplex* other;
    (void)argc;
    vother = argv[0];
    assert(nn_value_isinstance(thisval));
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    pv = (NNUClassComplex*)inst;
    other = (NNUClassComplex*)nn_value_asinstance(vother);
    return nn_pcomplex_makeinstance(state, ((NNObjInstance*)inst)->klass, pv->re + other->re, pv->im + other->im);
}

static NNValue nn_complexclass_opsub(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue vother;
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    NNUClassComplex* other;
    (void)argc;
    assert(nn_value_isinstance(thisval));
    vother = argv[0];
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    pv = (NNUClassComplex*)inst;
    other = (NNUClassComplex*)nn_value_asinstance(vother);
    return nn_pcomplex_makeinstance(state, ((NNObjInstance*)inst)->klass, pv->re - other->re, pv->im - other->im);
}

static NNValue nn_complexclass_opmul(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    double vre;
    double vim;
    NNValue vother;
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    NNUClassComplex* other;
    (void)argc;
    assert(nn_value_isinstance(thisval));
    vother = argv[0];
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    pv = (NNUClassComplex*)inst;
    other = (NNUClassComplex*)nn_value_asinstance(vother);
    vre = (pv->re * other->re - pv->im * other->im);
    vim = (pv->re * other->im + pv->im * other->re);
    return nn_pcomplex_makeinstance(state, ((NNObjInstance*)inst)->klass, vre, vim);
}

static NNValue nn_complexclass_opdiv(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    double r;
    double i;
    double ti;
    double tr;
    NNValue vother;
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    NNUClassComplex* other;
    (void)argc;
    assert(nn_value_isinstance(thisval));
    vother = argv[0];
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    pv = (NNUClassComplex*)inst;
    other = (NNUClassComplex*)nn_value_asinstance(vother);
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
    return nn_pcomplex_makeinstance(state, ((NNObjInstance*)inst)->klass, (r * ti + i) / tr, (i * ti - r) / tr);
}

static NNValue nn_complexclass_getre(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    assert(nn_value_isinstance(thisval));
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    pv = (NNUClassComplex*)inst;
    return nn_value_makenumber(pv->re);
}

static NNValue nn_complexclass_getim(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    SCRIPTFN_UNUSED(state);
    (void)argv;
    (void)argc;
    assert(nn_value_isinstance(thisval));
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    pv = (NNUClassComplex*)inst;
    return nn_value_makenumber(pv->im);
}

NNDefModule* nn_natmodule_load_complex()
{
    static NNDefFunc modfuncs[] =
    {
        {nullptr, false, nullptr},
    };

    static NNDefField modfields[] =
    {
        {nullptr, false, nullptr},
    };
    static NNDefModule module;
    module.name = "complex";
    module.definedfields = modfields;
    module.definedfunctions = modfuncs;
    static NNDefFunc complexfuncs[] =
    {
        {"constructor", false, nn_complexclass_constructor},
        {"__add__", false, nn_complexclass_opadd},
        {"__sub__", false, nn_complexclass_opsub},
        {"__mul__", false, nn_complexclass_opmul},
        {"__div__", false, nn_complexclass_opdiv},
        {nullptr, 0, nullptr},
    };
    static NNDefField complexfields[] =
    {
        {"re", false, nn_complexclass_getre},
        {"im", false, nn_complexclass_getim},
        {nullptr, 0, nullptr},
    };
    static NNDefClass classes[] = {
        {"Complex", complexfields, complexfuncs},
        {nullptr, nullptr, nullptr}
    };
    module.definedclasses = classes;
    module.fnpreloaderfunc = nullptr;
    module.fnunloaderfunc = nullptr;
    return &module;
}


static NNValue nn_nativefn_time(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    struct timeval tv;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "time", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    osfn_gettimeofday(&tv, nullptr);
    return nn_value_makenumber((double)tv.tv_sec + ((double)tv.tv_usec / 10000000));
}

static NNValue nn_nativefn_microtime(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    struct timeval tv;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "microtime", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    osfn_gettimeofday(&tv, nullptr);
    return nn_value_makenumber((1000000 * (double)tv.tv_sec) + ((double)tv.tv_usec / 10));
}

static NNValue nn_nativefn_id(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue val;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "id", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    val = argv[0];
    return nn_value_makenumber(*(long*)&val);
}

static NNValue nn_nativefn_int(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "int", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    if(argc == 0)
    {
        return nn_value_makenumber(0);
    }
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    return nn_value_makenumber((double)((int)nn_value_asnumber(argv[0])));
}

static NNValue nn_nativefn_chr(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t len;
    char* string;
    int ch;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "chr", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    ch = nn_value_asnumber(argv[0]);
    string = nn_util_utf8encode(ch, &len);
    return nn_value_fromobject(nn_string_takelen(string, len));
}

static NNValue nn_nativefn_ord(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int ord;
    int length;
    NNObjString* string;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "ord", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(argv[0]);
    length = nn_string_getlength(string);
    if(length > 1)
    {
        NEON_RETURNERROR("ord() expects character as argument, string given");
    }
    ord = (int)nn_string_getdata(string)[0];
    if(ord < 0)
    {
        ord += 256;
    }
    return nn_value_makenumber(ord);
}

static NNValue nn_nativefn_rand(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int tmp;
    int lowerlimit;
    int upperlimit;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "rand", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 2);
    lowerlimit = 0;
    upperlimit = 1;
    if(argc > 0)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        lowerlimit = nn_value_asnumber(argv[0]);
    }
    if(argc == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        upperlimit = nn_value_asnumber(argv[1]);
    }
    if(lowerlimit > upperlimit)
    {
        tmp = upperlimit;
        upperlimit = lowerlimit;
        lowerlimit = tmp;
    }
    return nn_value_makenumber(nn_util_mtrand(lowerlimit, upperlimit));
}

static NNValue nn_nativefn_eval(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue result;
    NNObjString* os;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "eval", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    os = nn_value_asstring(argv[0]);
    /*fprintf(stderr, "eval:src=%s\n", nn_string_getdata(os));*/
    result = nn_state_evalsource(state, nn_string_getdata(os));
    return result;
}

/*
static NNValue nn_nativefn_loadfile(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue result;
    NNObjString* os;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "loadfile", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    os = nn_value_asstring(argv[0]);
    fprintf(stderr, "eval:src=%s\n", nn_string_getdata(os));
    result = nn_state_evalsource(state, nn_string_getdata(os));
    return result;
}
*/

static NNValue nn_nativefn_instanceof(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "instanceof", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isinstance);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isclass);
    return nn_value_makebool(nn_util_isinstanceof(nn_value_asinstance(argv[0])->klass, nn_value_asclass(argv[1])));
}

static NNValue nn_nativefn_sprintf(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNFormatInfo nfi;
    NNIOStream pr;
    NNObjString* res;
    NNObjString* ofmt;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "sprintf", argv, argc);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = nn_value_asstring(argv[0]);
    nn_iostream_makestackstring(&pr);
    nn_strformat_init(state, &nfi, &pr, nn_string_getdata(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, argc, 1, argv))
    {
        return nn_value_makenull();
    }
    res = nn_iostream_takestring(&pr);
    nn_iostream_destroy(&pr);
    return nn_value_fromobject(res);
}

static NNValue nn_nativefn_printf(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNFormatInfo nfi;
    NNObjString* ofmt;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "printf", argv, argc);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = nn_value_asstring(argv[0]);
    nn_strformat_init(state, &nfi, state->stdoutprinter, nn_string_getdata(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, argc, 1, argv))
    {
    }
    return nn_value_makenull();
}

static NNValue nn_nativefn_print(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    (void)thisval;
    for(i = 0; i < argc; i++)
    {
        nn_iostream_printvalue(state->stdoutprinter, argv[i], false, true);
    }
    return nn_value_makenull();
}

static NNValue nn_nativefn_println(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    v = nn_nativefn_print(state, thisval, argv, argc);
    nn_iostream_writestring(state->stdoutprinter, "\n");
    return v;
}


static NNValue nn_nativefn_isnan(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    SCRIPTFN_UNUSED(state);
    (void)thisval;
    (void)argv;
    (void)argc;
    return nn_value_makebool(false);
}

static NNValue nn_objfnjson_stringify(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNIOStream pr;
    NNObjString* os;
    (void)thisval;
    (void)argc;
    v = argv[0];
    nn_iostream_makestackstring(&pr);
    pr.jsonmode = true;
    nn_iostream_printvalue(&pr, v, true, false);
    os = nn_iostream_takestring(&pr);
    nn_iostream_destroy(&pr);
    return nn_value_fromobject(os);
}

/**
* setup global functions.
*/
void nn_state_initbuiltinfunctions(NNState* state)
{
    NNObjClass* klass;
    nn_state_defnativefunction(state, "chr", nn_nativefn_chr);
    nn_state_defnativefunction(state, "id", nn_nativefn_id);
    nn_state_defnativefunction(state, "int", nn_nativefn_int);
    nn_state_defnativefunction(state, "instanceof", nn_nativefn_instanceof);
    nn_state_defnativefunction(state, "ord", nn_nativefn_ord);
    nn_state_defnativefunction(state, "sprintf", nn_nativefn_sprintf);
    nn_state_defnativefunction(state, "printf", nn_nativefn_printf);
    nn_state_defnativefunction(state, "print", nn_nativefn_print);
    nn_state_defnativefunction(state, "println", nn_nativefn_println);
    nn_state_defnativefunction(state, "rand", nn_nativefn_rand);
    nn_state_defnativefunction(state, "eval", nn_nativefn_eval);
    nn_state_defnativefunction(state, "isNaN", nn_nativefn_isnan);
    nn_state_defnativefunction(state, "microtime", nn_nativefn_microtime);
    nn_state_defnativefunction(state, "time", nn_nativefn_time);
    {
        klass = nn_util_makeclass(state, "JSON", state->classprimobject);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr("stringify"), nn_objfnjson_stringify);
    }
}




/*
* you can use this file as a template for new native modules.
* just fill out the fields, give the load function a meaningful name (i.e., nn_natmodule_load_foobar if your module is "foobar"),
* et cetera.
* then, add said function in libmodule.c's nn_import_loadbuiltinmodules, and you're good to go!
*/

NNDefModule* nn_natmodule_load_null()
{
    static NNDefFunc modfuncs[] =
    {
        /* {"somefunc",   true,  myfancymodulefunction},*/
        {nullptr, false, nullptr},
    };

    static NNDefField modfields[] =
    {
        /*{"somefield", true, the_function_that_gets_called},*/
        {nullptr, false, nullptr},
    };
    static NNDefModule module;
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

static NNValue nn_modfn_os_readdir(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_argcheck_init(state, &check, "readdir", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_iscallable);

    os = nn_value_asstring(argv[0]);
    callable = argv[1];
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
            itemval = nn_value_fromobject(itemstr);
            nestargs[0] = itemval;
            nn_nestcall_callfunction(state, callable, thisval, nestargs, 1, &res, false);
        }
        fslib_dirclose(&rd);
        return nn_value_makenull();
    }
    else
    {
        nn_except_throw(state, "cannot open directory '%s'", dirn);
    }
    return nn_value_makenull();
}

/*
static NNValue nn_modfn_os_$template(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int64_t r;
    int64_t mod;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "chmod", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    path = nn_value_asstring(argv[0]);
    mod = nn_value_asnumber(argv[1]);
    r = osfn_chmod(nn_string_getdata(path), mod);
    return nn_value_makenumber(r);
}
*/

static NNValue nn_modfn_os_chmod(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int64_t r;
    int64_t mod;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "chmod", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    path = nn_value_asstring(argv[0]);
    mod = nn_value_asnumber(argv[1]);
    r = osfn_chmod(nn_string_getdata(path), mod);
    return nn_value_makenumber(r);
}

static NNValue nn_modfn_os_mkdir(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int64_t r;
    int64_t mod;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "mkdir", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    path = nn_value_asstring(argv[0]);
    mod = nn_value_asnumber(argv[1]);
    r = osfn_mkdir(nn_string_getdata(path), mod);
    return nn_value_makenumber(r);
}


static NNValue nn_modfn_os_chdir(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int64_t r;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "chdir", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = nn_value_asstring(argv[0]);
    r = osfn_chdir(nn_string_getdata(path));
    return nn_value_makenumber(r);
}

static NNValue nn_modfn_os_rmdir(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int64_t r;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "rmdir", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    path = nn_value_asstring(argv[0]);
    r = osfn_rmdir(nn_string_getdata(path));
    return nn_value_makenumber(r);
}

static NNValue nn_modfn_os_unlink(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int64_t r;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "unlink", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = nn_value_asstring(argv[0]);
    r = osfn_unlink(nn_string_getdata(path));
    return nn_value_makenumber(r);
}

static NNValue nn_modfn_os_getenv(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    const char* r;
    NNObjString* key;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "getenv", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    key = nn_value_asstring(argv[0]);
    r = osfn_getenv(nn_string_getdata(key));
    if(r == nullptr)
    {
        return nn_value_makenull();
    }
    return nn_value_fromobject(nn_string_copycstr(r));
}

static NNValue nn_modfn_os_setenv(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* key;
    NNObjString* value;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "setenv", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
    key = nn_value_asstring(argv[0]);
    value = nn_value_asstring(argv[1]);
    return nn_value_makebool(osfn_setenv(nn_string_getdata(key), nn_string_getdata(value), true));
}

static NNValue nn_modfn_os_cwdhelper(NNState* state, NNValue thisval, NNValue* argv, size_t argc, const char* name)
{
    enum { kMaxBufSz = 1024 };
    NNArgCheck check;
    char* r;
    char buf[kMaxBufSz];
    (void)thisval;
    nn_argcheck_init(state, &check, name, argv, argc);
    r = osfn_getcwd(buf, kMaxBufSz);
    if(r == nullptr)
    {
        return nn_value_makenull();
    }
    return nn_value_fromobject(nn_string_copycstr(r));
}

static NNValue nn_modfn_os_cwd(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    return nn_modfn_os_cwdhelper(state, thisval, argv, argc, "cwd");
}

static NNValue nn_modfn_os_pwd(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    return nn_modfn_os_cwdhelper(state, thisval, argv, argc, "pwd");
}

static NNValue nn_modfn_os_basename(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    const char* r;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "basename", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = nn_value_asstring(argv[0]);
    r = osfn_basename(nn_string_getdata(path));
    if(r == nullptr)
    {
        return nn_value_makenull();
    }
    return nn_value_fromobject(nn_string_copycstr(r));
}

static NNValue nn_modfn_os_dirname(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    const char* r;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "dirname", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = nn_value_asstring(argv[0]);
    r = osfn_dirname(nn_string_getdata(path));
    if(r == nullptr)
    {
        return nn_value_makenull();
    }
    return nn_value_fromobject(nn_string_copycstr(r));
}

static NNValue nn_modfn_os_touch(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    FILE* fh;
    NNObjString* path;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "touch", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = nn_value_asstring(argv[0]);
    fh = fopen(nn_string_getdata(path), "rb");
    if(fh == nullptr)
    {
        return nn_value_makebool(false);
    }
    fclose(fh);
    return nn_value_makebool(true);
}

#define putorgetkey(md, name, value) \
    { \
        if(havekey && (keywanted != nullptr)) \
        { \
            if(strcmp(name, nn_string_getdata(keywanted)) == 0) \
            { \
                return value; \
            } \
        } \
        else \
        { \
           nn_dict_addentrycstr(md, name, value); \
        } \
    }


NNValue nn_modfn_os_stat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    bool havekey;
    NNObjString* keywanted;
    NNObjString* path;
    NNArgCheck check;
    NNObjDict* md;
    NNFSStat nfs;
    const char* strp;
    (void)thisval;
    havekey = false;
    keywanted = nullptr;
    md = nullptr;
    nn_argcheck_init(state, &check, "stat", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    path = nn_value_asstring(argv[0]);
    if(argc > 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
        keywanted = nn_value_asstring(argv[1]);
        havekey = true;
    }
    strp = nn_string_getdata(path);
    if(!nn_filestat_initfrompath(&nfs, strp))
    {
        nn_except_throwclass(state, state->exceptions.ioerror, "%s: %s", strp, strerror(errno));
        return nn_value_makenull();
    }
    md = nullptr;
    if(!havekey)
    {
        md = nn_object_makedict();
    }
    putorgetkey(md, "path", nn_value_fromobject(path));
    putorgetkey(md, "mode", nn_value_makenumber(nfs.mode));
    putorgetkey(md, "modename", nn_value_fromobject(nn_string_copycstr(nfs.modename)));
    putorgetkey(md, "inode", nn_value_makenumber(nfs.inode));
    putorgetkey(md, "links", nn_value_makenumber(nfs.numlinks));
    putorgetkey(md, "uid", nn_value_makenumber(nfs.owneruid));
    putorgetkey(md, "gid", nn_value_makenumber(nfs.ownergid));
    putorgetkey(md, "blocksize", nn_value_makenumber(nfs.blocksize));
    putorgetkey(md, "blocks", nn_value_makenumber(nfs.blockcount));
    putorgetkey(md, "filesize", nn_value_makenumber(nfs.filesize));
    putorgetkey(md, "lastchanged", nn_value_fromobject(nn_string_copycstr(nn_filestat_ctimetostring(nfs.tmlastchanged))));
    putorgetkey(md, "lastaccess", nn_value_fromobject(nn_string_copycstr(nn_filestat_ctimetostring(nfs.tmlastaccessed))));
    putorgetkey(md, "lastmodified", nn_value_fromobject(nn_string_copycstr(nn_filestat_ctimetostring(nfs.tmlastmodified))));
    return nn_value_fromobject(md);
}

NNDefModule* nn_natmodule_load_os()
{
    static NNDefFunc modfuncs[] =
    {
        {"readdir",   true,  nn_modfn_os_readdir},
        {"chmod",   true,  nn_modfn_os_chmod},
        {"chdir",   true,  nn_modfn_os_chdir},
        {"mkdir",   true,  nn_modfn_os_mkdir},
        {"unlink",   true,  nn_modfn_os_unlink},
        {"getenv",   true,  nn_modfn_os_getenv},
        {"setenv",   true,  nn_modfn_os_setenv},
        {"rmdir",   true,  nn_modfn_os_rmdir},
        {"pwd",   true,  nn_modfn_os_pwd},
        {"pwd",   true,  nn_modfn_os_cwd},
        {"basename",   true,  nn_modfn_os_basename},
        {"dirname",   true,  nn_modfn_os_dirname},
        {"touch",   true,  nn_modfn_os_touch},
        {"stat",   true,  nn_modfn_os_stat},

        /* todo: implement these! */
        #if 0
        /* shell-like directory state - might be trickier */
        #endif
        {nullptr,     false, nullptr},
    };
    static NNDefField modfields[] =
    {
        /*{"platform", true, get_os_platform},*/
        {nullptr,       false, nullptr},
    };
    static NNDefModule module;
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

void nn_vm_initvmstate(NNState* state)
{
    size_t finalsz;
    GCSingleton::get()->linkedobjects = nullptr;
    GCSingleton::get()->vmstate.currentframe = nullptr;
    {
        GCSingleton::get()->vmstate.stackcapacity = NEON_CONFIG_INITSTACKCOUNT;
        finalsz = NEON_CONFIG_INITSTACKCOUNT * sizeof(NNValue);
        GCSingleton::get()->vmstate.stackvalues = (NNValue*)nn_memory_malloc(finalsz);
        if(GCSingleton::get()->vmstate.stackvalues == nullptr)
        {
            fprintf(stderr, "error: failed to allocate stackvalues!\n");
            abort();
        }
        memset(GCSingleton::get()->vmstate.stackvalues, 0, finalsz);
    }
    {
        GCSingleton::get()->vmstate.framecapacity = NEON_CONFIG_INITFRAMECOUNT;
        finalsz = NEON_CONFIG_INITFRAMECOUNT * sizeof(NNCallFrame);
        GCSingleton::get()->vmstate.framevalues = (NNCallFrame*)nn_memory_malloc(finalsz);
        if(GCSingleton::get()->vmstate.framevalues == nullptr)
        {
            fprintf(stderr, "error: failed to allocate framevalues!\n");
            abort();
        }
        memset(GCSingleton::get()->vmstate.framevalues, 0, finalsz);
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


/**
* grows vmstate.(stack|frame)values, respectively.
* currently it works fine with mob.js (man-or-boy test), although
* there are still some invalid reads regarding the closure;
* almost definitely because the pointer address changes.
*
* currently, the implementation really does just increase the
* memory block available:
* i.e., [NNValue x 32] -> [NNValue x <newsize>], without
* copying anything beyond primitive values.
*/
NEON_INLINE bool nn_vm_resizestack(NNState* state, NNObjFunction* closure, size_t needed)
{
    size_t oldsz;
    size_t newsz;
    size_t allocsz;
    size_t nforvals;
    NNValue* oldbuf;
    NNValue* newbuf;
    nforvals = (needed * 2);
    oldsz = GCSingleton::get()->vmstate.stackcapacity;
    newsz = oldsz + nforvals;
    allocsz = ((newsz + 1) * sizeof(NNValue));
    if(nn_util_unlikely(state->conf.enableapidebug))
    {
        if(closure != nullptr)
        {
            nn_vm_resizeinfo("stack", closure, needed);
        }
        fprintf(stderr, "*** resizing stack: needed %ld, from %ld to %ld, allocating %ld ***\n", (long)nforvals, (long)oldsz, (long)newsz, (long)allocsz);
    }
    oldbuf = GCSingleton::get()->vmstate.stackvalues;
    newbuf = (NNValue*)nn_memory_realloc(oldbuf, allocsz);
    if(newbuf == nullptr)
    {
        fprintf(stderr, "internal error: failed to resize stackvalues!\n");
        abort();
    }
    GCSingleton::get()->vmstate.stackvalues = (NNValue*)newbuf;
    GCSingleton::get()->vmstate.stackcapacity = newsz;
    return true;
}

NEON_INLINE bool nn_vm_resizeframes(NNState* state, NNObjFunction* closure, size_t needed)
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
    if(nn_util_unlikely(state->conf.enableapidebug))
    {
        if(closure != nullptr)
        {
            nn_vm_resizeinfo("frames", closure, needed);
        }
        fprintf(stderr, "*** resizing frames ***\n");
    }
    oldclosure = GCSingleton::get()->vmstate.currentframe->closure;
    oldip = GCSingleton::get()->vmstate.currentframe->inscode;
    oldhandlercnt = GCSingleton::get()->vmstate.currentframe->handlercount;
    oldsz = GCSingleton::get()->vmstate.framecapacity;
    newsz = oldsz + needed;
    allocsz = ((newsz + 1) * sizeof(NNCallFrame));
    #if 1
        oldbuf = GCSingleton::get()->vmstate.framevalues;
        newbuf = (NNCallFrame*)nn_memory_realloc(oldbuf, allocsz);
        if(newbuf == nullptr)
        {
            fprintf(stderr, "internal error: failed to resize framevalues!\n");
            abort();
        }
    #endif
    GCSingleton::get()->vmstate.framevalues = (NNCallFrame*)newbuf;
    GCSingleton::get()->vmstate.framecapacity = newsz;
    /*
    * this bit is crucial: realloc changes pointer addresses, and to keep the
    * current frame, re-read it from the new address.
    */
    GCSingleton::get()->vmstate.currentframe = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount - 1];
    GCSingleton::get()->vmstate.currentframe->handlercount = oldhandlercnt;
    GCSingleton::get()->vmstate.currentframe->inscode = oldip;
    GCSingleton::get()->vmstate.currentframe->closure = oldclosure;
    return true;
}

NEON_INLINE bool nn_vm_checkmayberesize(NNState* state)
{
    NNObjFunction* closure;
    closure = nullptr;
    if(GCSingleton::get()->vmstate.currentframe != nullptr)
    {
        closure = GCSingleton::get()->vmstate.currentframe->closure;
    }
    if((GCSingleton::get()->vmstate.stackidx+1) >= GCSingleton::get()->vmstate.stackcapacity)
    {
        if(!nn_vm_resizestack(state, closure, GCSingleton::get()->vmstate.stackidx + 1))
        {
            return nn_except_throw(state, "failed to resize stack due to overflow");
        }
        return true;
    }
    if(GCSingleton::get()->vmstate.framecount >= GCSingleton::get()->vmstate.framecapacity)
    {
        if(!nn_vm_resizeframes(state, closure, GCSingleton::get()->vmstate.framecapacity + 1))
        {
            return nn_except_throw(state, "failed to resize frames due to overflow");
        }
        return true;
    }
    return false;
}

void nn_state_resetvmstate(NNState* state)
{
    GCSingleton::get()->vmstate.framecount = 0;
    GCSingleton::get()->vmstate.stackidx = 0;
    GCSingleton::get()->vmstate.openupvalues = nullptr;
}

bool nn_vm_callclosure(NNState* state, NNObjFunction* closure, NNValue thisval, int argcount, bool fromoperator)
{
    int i;
    int startva;
    NNCallFrame* frame;
    NNObjArray* argslist;
    //closure->clsthisval = thisval;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval, true), argcount);
    /* fill empty parameters if not variadic */
    for(; !closure->fnclosure.scriptfunc->fnscriptfunc.isvariadic && argcount < closure->fnclosure.scriptfunc->fnscriptfunc.arity; argcount++)
    {
        nn_vm_stackpush(state, nn_value_makenull());
    }
    /* handle variadic arguments... */
    if(closure->fnclosure.scriptfunc->fnscriptfunc.isvariadic && argcount >= closure->fnclosure.scriptfunc->fnscriptfunc.arity - 1)
    {
        startva = argcount - closure->fnclosure.scriptfunc->fnscriptfunc.arity;
        argslist = nn_object_makearray();
        nn_vm_stackpush(state, nn_value_fromobject(argslist));
        for(i = startva; i >= 0; i--)
        {
            argslist->varray.push(nn_vm_stackpeek(state, i + 1));
        }
        argcount -= startva;
        /* +1 for the gc protection push above */
        nn_vm_stackpopn(state, startva + 2);
        nn_vm_stackpush(state, nn_value_fromobject(argslist));
    }
    if(argcount != closure->fnclosure.scriptfunc->fnscriptfunc.arity)
    {
        nn_vm_stackpopn(state, argcount);
        if(closure->fnclosure.scriptfunc->fnscriptfunc.isvariadic)
        {
            return nn_except_throw(state, "function '%s' expected at least %d arguments but got %d", nn_string_getdata(closure->name), closure->fnclosure.scriptfunc->fnscriptfunc.arity - 1, argcount);
        }
        else
        {
            return nn_except_throw(state, "function '%s' expected %d arguments but got %d", nn_string_getdata(closure->name), closure->fnclosure.scriptfunc->fnscriptfunc.arity, argcount);
        }
    }
    if(nn_vm_checkmayberesize(state))
    {
        #if 0
            nn_vm_stackpopn(state, argcount);
        #endif
    }
    if(fromoperator)
    {
        #if 0
            nn_vm_stackpop(state);
            nn_vm_stackpush(state, thisval);
        #else
            int64_t spos;
            spos = (GCSingleton::get()->vmstate.stackidx + (-argcount - 1));
            #if 0
                GCSingleton::get()->vmstate.stackvalues[spos] = closure->clsthisval;
            #else
                GCSingleton::get()->vmstate.stackvalues[spos] = thisval;
            #endif
        #endif
    }
    frame = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount++];
    frame->closure = closure;
    frame->inscode = closure->fnclosure.scriptfunc->fnscriptfunc.blob.instrucs;
    frame->stackslotpos = GCSingleton::get()->vmstate.stackidx + (-argcount - 1);
    return true;
}

NEON_INLINE bool nn_vm_callnative(NNState* state, NNObjFunction* native, NNValue thisval, int argcount)
{
    int64_t spos;
    NNValue r;
    NNValue* vargs;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval, true), argcount);
    spos = GCSingleton::get()->vmstate.stackidx + (-argcount);
    vargs = &GCSingleton::get()->vmstate.stackvalues[spos];
    r = native->fnnativefunc.natfunc(state, thisval, vargs, argcount);
    {
        GCSingleton::get()->vmstate.stackvalues[spos - 1] = r;
        GCSingleton::get()->vmstate.stackidx -= argcount;
    }
    GCSingleton::clearGCProtect();
    return true;
}

bool nn_vm_callvaluewithobject(NNState* state, NNValue callable, NNValue thisval, int argcount, bool fromoper)
{
    int64_t spos;
    NNObjFunction* ofn;
    ofn = nn_value_asfunction(callable);
    #if 0
        #define NEON_APIPRINT(state, ...) fprintf(stderr, __VA_ARGS__), fprintf(stderr, "\n");
    #else
        #define NEON_APIPRINT(state, ...)
    #endif
    NEON_APIPRINT(state, "*callvaluewithobject*: thisval.type=%s, callable.type=%s, argcount=%d", nn_value_typename(thisval, true), nn_value_typename(callable, true), argcount);

    if(nn_value_isobject(callable))
    {
        switch(nn_value_objtype(callable))
        {
            case NEON_OBJTYPE_FUNCCLOSURE:
                {
                    return nn_vm_callclosure(state, ofn, thisval, argcount, fromoper);
                }
                break;
            case NEON_OBJTYPE_FUNCNATIVE:
                {
                    return nn_vm_callnative(state, ofn, thisval, argcount);
                }
                break;
            case NEON_OBJTYPE_FUNCBOUND:
                {
                    NNObjFunction* bound;
                    bound = ofn;
                    spos = (GCSingleton::get()->vmstate.stackidx + (-argcount - 1));
                    GCSingleton::get()->vmstate.stackvalues[spos] = thisval;
                    return nn_vm_callclosure(state, bound->fnmethod.method, thisval, argcount, fromoper);
                }
                break;
            case NEON_OBJTYPE_CLASS:
                {
                    NNObjClass* klass;
                    klass = nn_value_asclass(callable);
                    spos = (GCSingleton::get()->vmstate.stackidx + (-argcount - 1));
                    GCSingleton::get()->vmstate.stackvalues[spos] = thisval;
                    if(!klass->constructor.isNull())
                    {
                        return nn_vm_callvaluewithobject(state, (NNValue)klass->constructor, thisval, argcount, false);
                    }
                    else if(klass->superclass != nullptr && !klass->superclass->constructor.isNull())
                    {
                        return nn_vm_callvaluewithobject(state, (NNValue)klass->superclass->constructor, thisval, argcount, false);
                    }
                    else if(argcount != 0)
                    {
                        return nn_except_throw(state, "%s constructor expects 0 arguments, %d given", nn_string_getdata(klass->name), argcount);
                    }
                    return true;
                }
                break;
            case NEON_OBJTYPE_MODULE:
                {
                    NNObjModule* module;
                    NNProperty* field;
                    module = nn_value_asmodule(callable);
                    field = module->deftable.getfieldbyostr(module->name);
                    if(field != nullptr)
                    {
                        return nn_vm_callvalue(state, (NNValue)field->value, thisval, argcount, false);
                    }
                    return nn_except_throw(state, "module %s does not export a default function", module->name);
                }
                break;
            default:
                break;
        }
    }
    return nn_except_throw(state, "object of type %s is not callable", nn_value_typename(callable, false));
}

bool nn_vm_callvalue(NNState* state, NNValue callable, NNValue thisval, int argcount, bool fromoperator)
{
    NNValue actualthisval;
    NNObjFunction* ofn;
    if(nn_value_isobject(callable))
    {
        ofn = nn_value_asfunction(callable);
        switch(nn_value_objtype(callable))
        {
            case NEON_OBJTYPE_FUNCBOUND:
                {
                    NNObjFunction* bound;
                    bound = ofn;
                    actualthisval = (NNValue)bound->fnmethod.receiver;
                    if(!thisval.isNull())
                    {
                        actualthisval = thisval;
                    }
                    NEON_APIDEBUG(state, "actualthisval.type=%s, argcount=%d", nn_value_typename(actualthisval, true), argcount);
                    return nn_vm_callvaluewithobject(state, callable, actualthisval, argcount, fromoperator);
                }
                break;
            case NEON_OBJTYPE_CLASS:
                {
                    NNObjClass* klass;
                    NNObjInstance* instance;
                    klass = nn_value_asclass(callable);
                    instance = nn_object_makeinstance(klass);
                    actualthisval = nn_value_fromobject(instance);
                    if(!thisval.isNull())
                    {
                        actualthisval = thisval;
                    }
                    NEON_APIDEBUG(state, "actualthisval.type=%s, argcount=%d", nn_value_typename(actualthisval, true), argcount);
                    return nn_vm_callvaluewithobject(state, callable, actualthisval, argcount, fromoperator);
                }
                break;
            default:
                {
                }
                break;
        }
    }
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval, true), argcount);
    return nn_vm_callvaluewithobject(state, callable, thisval, argcount, fromoperator);
}

NEON_INLINE NNFuncContextType nn_value_getmethodtype(NNValue method)
{
    NNObjFunction* ofn;
    ofn = nn_value_asfunction(method);
    switch(nn_value_objtype(method))
    {
        case NEON_OBJTYPE_FUNCNATIVE:
            return ofn->contexttype;
        case NEON_OBJTYPE_FUNCCLOSURE:
            return ofn->fnclosure.scriptfunc->contexttype;
        default:
            break;
    }
    return NEON_FNCONTEXTTYPE_FUNCTION;
}

NNObjClass* nn_value_getclassfor(NNState* state, NNValue receiver)
{
    if(nn_value_isnumber(receiver))
    {
        return state->classprimnumber;
    }
    if(nn_value_isobject(receiver))
    {
        switch(nn_value_asobject(receiver)->type)
        {
            case NEON_OBJTYPE_STRING:
                return state->classprimstring;
            case NEON_OBJTYPE_RANGE:
                return state->classprimrange;
            case NEON_OBJTYPE_ARRAY:
                return state->classprimarray;
            case NEON_OBJTYPE_DICT:
                return state->classprimdict;
            case NEON_OBJTYPE_FILE:
                return state->classprimfile;
            case NEON_OBJTYPE_FUNCBOUND:
            case NEON_OBJTYPE_FUNCCLOSURE:
            case NEON_OBJTYPE_FUNCSCRIPT:
                return state->classprimcallable;
            
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

NEON_INLINE void nn_vmbits_stackpush(NNState* state, NNValue value)
{
    nn_vm_checkmayberesize(state);
    GCSingleton::get()->vmstate.stackvalues[GCSingleton::get()->vmstate.stackidx] = value;
    GCSingleton::get()->vmstate.stackidx++;
}

void nn_vm_stackpush(NNState* state, NNValue value)
{
    nn_vmbits_stackpush(state, value);
}

NEON_INLINE NNValue nn_vmbits_stackpop(NNState* state)
{
    NNValue v;
    GCSingleton::get()->vmstate.stackidx--;
    if(GCSingleton::get()->vmstate.stackidx < 0)
    {
        GCSingleton::get()->vmstate.stackidx = 0;
    }
    v = GCSingleton::get()->vmstate.stackvalues[GCSingleton::get()->vmstate.stackidx];
    return v;
}

NNValue nn_vm_stackpop(NNState* state)
{
    return nn_vmbits_stackpop(state);
}

NEON_INLINE NNValue nn_vmbits_stackpopn(NNState* state, int n)
{
    NNValue v;
    GCSingleton::get()->vmstate.stackidx -= n;
    if(GCSingleton::get()->vmstate.stackidx < 0)
    {
        GCSingleton::get()->vmstate.stackidx = 0;
    }
    v = GCSingleton::get()->vmstate.stackvalues[GCSingleton::get()->vmstate.stackidx];
    return v;
}

NNValue nn_vm_stackpopn(NNState* state, int n)
{
    return nn_vmbits_stackpopn(state, n);
}

NEON_INLINE NNValue nn_vmbits_stackpeek(NNState* state, int distance)
{
    NNValue v;
    v = GCSingleton::get()->vmstate.stackvalues[GCSingleton::get()->vmstate.stackidx + (-1 - distance)];
    return v;

}

NNValue nn_vm_stackpeek(NNState* state, int distance)
{
    return nn_vmbits_stackpeek(state, distance);
}

/*
* this macro cannot (rather, should not) be used outside of nn_vm_runvm().
* if you need to halt the vm, throw an exception instead.
* this macro is EXCLUSIVELY for non-recoverable errors!
*/
#define nn_vmmac_exitvm(state) \
    { \
        (void)you_are_calling_exit_vm_outside_of_runvm; \
        return NEON_STATUS_FAILRUNTIME; \
    }        

#define nn_vmmac_tryraise(state, rtval, ...) \
    if(!nn_except_throw(state, ##__VA_ARGS__)) \
    { \
        return rtval; \
    }


/*
* don't try to further optimize vmbits functions, unless you *really* know what you are doing.
* they were initially macros; but for better debugging, and better type-enforcement, they
* are now inlined functions.
*/

NEON_FORCEINLINE uint8_t nn_vmbits_readbyte(NNState* state)
{
    uint8_t r;
    r = GCSingleton::get()->vmstate.currentframe->inscode->code;
    GCSingleton::get()->vmstate.currentframe->inscode++;
    return r;
}

NEON_FORCEINLINE NNInstruction nn_vmbits_readinstruction(NNState* state)
{
    NNInstruction r;
    r = *GCSingleton::get()->vmstate.currentframe->inscode;
    GCSingleton::get()->vmstate.currentframe->inscode++;
    return r;
}

NEON_FORCEINLINE uint16_t nn_vmbits_readshort(NNState* state)
{
    uint8_t b;
    uint8_t a;
    a = GCSingleton::get()->vmstate.currentframe->inscode[0].code;
    b = GCSingleton::get()->vmstate.currentframe->inscode[1].code;
    GCSingleton::get()->vmstate.currentframe->inscode += 2;
    return (uint16_t)((a << 8) | b);
}

NEON_FORCEINLINE NNValue nn_vmbits_readconst(NNState* state)
{
    uint16_t idx;
    idx = nn_vmbits_readshort(state);
    return GCSingleton::get()->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.blob.constants.get(idx);
}

NEON_FORCEINLINE NNObjString* nn_vmbits_readstring(NNState* state)
{
    return nn_value_asstring(nn_vmbits_readconst(state));
}

NEON_FORCEINLINE bool nn_vmutil_invokemethodfromclass(NNState* state, NNObjClass* klass, NNObjString* name, int argcount)
{
    NNProperty* field;
    NEON_APIDEBUG(state, "argcount=%d", argcount);
    field = klass->instmethods.getfieldbyostr(name);
    if(field != nullptr)
    {
        if(nn_value_getmethodtype((NNValue)field->value) == NEON_FNCONTEXTTYPE_PRIVATE)
        {
            return nn_except_throw(state, "cannot call private method '%s' from instance of %s", nn_string_getdata(name), nn_string_getdata(klass->name));
        }
        return nn_vm_callvaluewithobject(state, (NNValue)field->value, nn_value_fromobject(klass), argcount, false);
    }
    return nn_except_throw(state, "undefined method '%s' in %s", nn_string_getdata(name), nn_string_getdata(klass->name));
}

NEON_FORCEINLINE bool nn_vmutil_invokemethodself(NNState* state, NNObjString* name, int argcount)
{
    int64_t spos;
    NNValue receiver;
    NNObjInstance* instance;
    NNProperty* field;
    NEON_APIDEBUG(state, "argcount=%d", argcount);
    receiver = nn_vmbits_stackpeek(state, argcount);
    if(nn_value_isinstance(receiver))
    {
        instance = nn_value_asinstance(receiver);
        field = instance->klass->instmethods.getfieldbyostr(name);
        if(field != nullptr)
        {
            return nn_vm_callvaluewithobject(state, (NNValue)field->value, receiver, argcount, false);
        }
        field = instance->properties.getfieldbyostr(name);
        if(field != nullptr)
        {
            spos = (GCSingleton::get()->vmstate.stackidx + (-argcount - 1));
            GCSingleton::get()->vmstate.stackvalues[spos] = receiver;
            return nn_vm_callvaluewithobject(state, (NNValue)field->value, receiver, argcount, false);
        }
    }
    else if(nn_value_isclass(receiver))
    {
        field = nn_value_asclass(receiver)->instmethods.getfieldbyostr(name);
        if(field != nullptr)
        {
            if(nn_value_getmethodtype((NNValue)field->value) == NEON_FNCONTEXTTYPE_STATIC)
            {
                return nn_vm_callvaluewithobject(state, (NNValue)field->value, receiver, argcount, false);
            }
            return nn_except_throw(state, "cannot call non-static method %s() on non instance", nn_string_getdata(name));
        }
    }
    return nn_except_throw(state, "cannot call method '%s' on object of type '%s'", nn_string_getdata(name), nn_value_typename(receiver, false));
}

NEON_FORCEINLINE bool nn_vmutil_invokemethodnormal(NNState* state, NNObjString* name, int argcount)
{
    size_t spos;
    NNObjType rectype;
    NNValue receiver;
    NNProperty* field;
    NNObjClass* klass;
    receiver = nn_vmbits_stackpeek(state, argcount);
    NEON_APIDEBUG(state, "receiver.type=%s, argcount=%d", nn_value_typename(receiver, true), argcount);
    if(nn_value_isobject(receiver))
    {
        rectype = nn_value_asobject(receiver)->type;
        switch(rectype)
        {
            case NEON_OBJTYPE_MODULE:
                {
                    NNObjModule* module;
                    NEON_APIDEBUG(state, "receiver is a module");
                    module = nn_value_asmodule(receiver);
                    field = module->deftable.getfieldbyostr(name);
                    if(field != nullptr)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            return nn_except_throw(state, "cannot call private module method '%s'", nn_string_getdata(name));
                        }
                        return nn_vm_callvaluewithobject(state, (NNValue)field->value, receiver, argcount, false);
                    }
                    return nn_except_throw(state, "module '%s' does not have a field named '%s'", nn_string_getdata(module->name), nn_string_getdata(name));
                }
                break;
            case NEON_OBJTYPE_CLASS:
                {
                    NEON_APIDEBUG(state, "receiver is a class");
                    klass = nn_value_asclass(receiver);
                    field = nn_class_getstaticproperty(klass, name);
                    if(field != nullptr)
                    {
                        return nn_vm_callvaluewithobject(state, (NNValue)field->value, receiver, argcount, false);
                    }
                    field = nn_class_getstaticmethodfield(klass, name);
                    if(field != nullptr)
                    {
                        return nn_vm_callvaluewithobject(state, (NNValue)field->value, receiver, argcount, false);
                    }
                    /*
                    * TODO:
                    * should this return the function? the returned value cannot be called without an object,
                    * so ... whats the right move here?
                    */
                    #if 1
                    else
                    {
                        NNFuncContextType fntyp;
                        field = klass->instmethods.getfieldbyostr(name);
                        if(field != nullptr)
                        {
                            fntyp = nn_value_getmethodtype((NNValue)field->value);
                            fprintf(stderr, "fntyp: %d\n", fntyp);
                            if(fntyp == NEON_FNCONTEXTTYPE_PRIVATE)
                            {
                                return nn_except_throw(state, "cannot call private method %s() on %s", nn_string_getdata(name), nn_string_getdata(klass->name));
                            }
                            if(fntyp == NEON_FNCONTEXTTYPE_STATIC)
                            {
                                return nn_vm_callvaluewithobject(state, (NNValue)field->value, receiver, argcount, false);
                            }
                        }
                    }
                    #endif
                    return nn_except_throw(state, "unknown method %s() in class %s", nn_string_getdata(name), nn_string_getdata(klass->name));
                }
            case NEON_OBJTYPE_INSTANCE:
                {
                    NNObjInstance* instance;
                    NEON_APIDEBUG(state, "receiver is an instance");
                    instance = nn_value_asinstance(receiver);
                    field = instance->properties.getfieldbyostr(name);
                    if(field != nullptr)
                    {
                        spos = (GCSingleton::get()->vmstate.stackidx + (-argcount - 1));
                        GCSingleton::get()->vmstate.stackvalues[spos] = receiver;
                        return nn_vm_callvaluewithobject(state, (NNValue)field->value, receiver, argcount, false);
                    }
                    return nn_vmutil_invokemethodfromclass(state, instance->klass, name, argcount);
                }
                break;
            case NEON_OBJTYPE_DICT:
                {
                    NEON_APIDEBUG(state, "receiver is a dictionary");
                    field = nn_class_getmethodfield(state->classprimdict, name);
                    if(field != nullptr)
                    {
                        return nn_vm_callnative(state, nn_value_asfunction((NNValue)field->value), receiver, argcount);
                    }
                    /* NEW in v0.0.84, dictionaries can declare extra methods as part of their entries. */
                    else
                    {
                        field = nn_value_asdict(receiver)->htab.getfieldbyostr(name);
                        if(field != nullptr)
                        {
                            if(nn_value_iscallable((NNValue)field->value))
                            {
                                return nn_vm_callvaluewithobject(state, (NNValue)field->value, receiver, argcount, false);
                            }
                        }
                    }
                    return nn_except_throw(state, "'dict' has no method %s()", nn_string_getdata(name));
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
        return nn_except_throw(state, "non-object %s has no method named '%s'", nn_value_typename(receiver, false), nn_string_getdata(name));
    }
    field = nn_class_getmethodfield(klass, name);
    if(field != nullptr)
    {
        return nn_vm_callvaluewithobject(state, (NNValue)field->value, receiver, argcount, false);
    }
    return nn_except_throw(state, "'%s' has no method %s()", nn_string_getdata(klass->name), nn_string_getdata(name));
}

NEON_FORCEINLINE bool nn_vmutil_bindmethod(NNState* state, NNObjClass* klass, NNObjString* name)
{
    NNValue val;
    NNProperty* field;
    NNObjFunction* bound;
    field = klass->instmethods.getfieldbyostr(name);
    if(field != nullptr)
    {
        if(nn_value_getmethodtype((NNValue)field->value) == NEON_FNCONTEXTTYPE_PRIVATE)
        {
            return nn_except_throw(state, "cannot get private property '%s' from instance", nn_string_getdata(name));
        }
        val = nn_vmbits_stackpeek(state, 0);
        bound = nn_object_makefuncbound(val, nn_value_asfunction((NNValue)field->value));
        nn_vmbits_stackpop(state);
        nn_vmbits_stackpush(state, nn_value_fromobject(bound));
        return true;
    }
    return nn_except_throw(state, "undefined property '%s'", nn_string_getdata(name));
}

NEON_FORCEINLINE NNObjUpvalue* nn_vmutil_upvaluescapture(NNState* state, NNValue* local, int stackpos)
{
    NNObjUpvalue* upvalue;
    NNObjUpvalue* prevupvalue;
    NNObjUpvalue* createdupvalue;
    prevupvalue = nullptr;
    upvalue = GCSingleton::get()->vmstate.openupvalues;
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
        GCSingleton::get()->vmstate.openupvalues = createdupvalue;
    }
    else
    {
        prevupvalue->next = createdupvalue;
    }
    return createdupvalue;
}

NEON_FORCEINLINE void nn_vmutil_upvaluesclose(NNState* state, const NNValue* last)
{
    NNObjUpvalue* upvalue;
    while(GCSingleton::get()->vmstate.openupvalues != nullptr && (&GCSingleton::get()->vmstate.openupvalues->location) >= last)
    {
        upvalue = GCSingleton::get()->vmstate.openupvalues;
        upvalue->closed = upvalue->location;
        upvalue->location = upvalue->closed;
        GCSingleton::get()->vmstate.openupvalues = upvalue->next;
    }
}

NEON_FORCEINLINE void nn_vmutil_definemethod(NNState* state, NNObjString* name)
{
    NNValue method;
    NNObjClass* klass;
    method = nn_vmbits_stackpeek(state, 0);
    klass = nn_value_asclass(nn_vmbits_stackpeek(state, 1));
    klass->instmethods.set(nn_value_fromobject(name), method);
    if(nn_value_getmethodtype(method) == NEON_FNCONTEXTTYPE_INITIALIZER)
    {
        klass->constructor = method;
    }
    nn_vmbits_stackpop(state);
}

NEON_FORCEINLINE void nn_vmutil_defineproperty(NNState* state, NNObjString* name, bool isstatic)
{
    NNValue property;
    NNObjClass* klass;
    property = nn_vmbits_stackpeek(state, 0);
    klass = nn_value_asclass(nn_vmbits_stackpeek(state, 1));
    if(!isstatic)
    {
        nn_class_defproperty(klass, name, property);
    }
    else
    {
        nn_class_setstaticproperty(klass, name, property);
    }
    nn_vmbits_stackpop(state);
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
NEON_FORCEINLINE NNObjString* nn_vmutil_multiplystring(NNState* state, NNObjString* str, double number)
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
    nn_iostream_makestackstring(&pr);
    for(i = 0; i < times; i++)
    {
        nn_iostream_writestringl(&pr, nn_string_getdata(str), nn_string_getlength(str));
    }
    os = nn_iostream_takestring(&pr);
    nn_iostream_destroy(&pr);
    return os;
}

NEON_FORCEINLINE NNObjArray* nn_vmutil_combinearrays(NNState* state, NNObjArray* a, NNObjArray* b)
{
    size_t i;
    NNObjArray* list;
    list = nn_object_makearray();
    nn_vmbits_stackpush(state, nn_value_fromobject(list));
    for(i = 0; i < a->varray.count(); i++)
    {
        list->varray.push(a->varray.get(i));
    }
    for(i = 0; i < b->varray.count(); i++)
    {
        list->varray.push(b->varray.get(i));
    }
    nn_vmbits_stackpop(state);
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

NEON_FORCEINLINE bool nn_vmutil_dogetrangedindexofarray(NNState* state, NNObjArray* list, bool willassign)
{
    long i;
    long idxlower;
    long idxupper;
    NNValue valupper;
    NNValue vallower;
    NNObjArray* newlist;
    valupper = nn_vmbits_stackpeek(state, 0);
    vallower = nn_vmbits_stackpeek(state, 1);
    if(!(vallower.isNull() || nn_value_isnumber(vallower)) || !(nn_value_isnumber(valupper) || valupper.isNull()))
    {
        nn_vmbits_stackpopn(state, 2);
        return nn_except_throw(state, "list range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower, false), nn_value_typename(valupper, false));
    }
    idxlower = 0;
    if(nn_value_isnumber(vallower))
    {
        idxlower = nn_value_asnumber(vallower);
    }
    if(valupper.isNull())
    {
        idxupper = list->varray.count();
    }
    else
    {
        idxupper = nn_value_asnumber(valupper);
    }
    if((idxlower < 0) || ((idxupper < 0) && ((long)(list->varray.count() + idxupper) < 0)) || (idxlower >= (long)list->varray.count()))
    {
        /* always return an empty list... */
        if(!willassign)
        {
            /* +1 for the list itself */
            nn_vmbits_stackpopn(state, 3);
        }
        nn_vmbits_stackpush(state, nn_value_fromobject(nn_object_makearray()));
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
    nn_vmbits_stackpush(state, nn_value_fromobject(newlist));
    for(i = idxlower; i < idxupper; i++)
    {
        newlist->varray.push(list->varray.get(i));
    }
    /* clear gc protect */
    nn_vmbits_stackpop(state);
    if(!willassign)
    {
        /* +1 for the list itself */
        nn_vmbits_stackpopn(state, 3);
    }
    nn_vmbits_stackpush(state, nn_value_fromobject(newlist));
    return true;
}

NEON_FORCEINLINE bool nn_vmutil_dogetrangedindexofstring(NNState* state, NNObjString* string, bool willassign)
{
    int end;
    int start;
    int length;
    int idxupper;
    int idxlower;
    NNValue valupper;
    NNValue vallower;
    valupper = nn_vmbits_stackpeek(state, 0);
    vallower = nn_vmbits_stackpeek(state, 1);
    if(!(vallower.isNull() || nn_value_isnumber(vallower)) || !(nn_value_isnumber(valupper) || valupper.isNull()))
    {
        nn_vmbits_stackpopn(state, 2);
        return nn_except_throw(state, "string range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower, false), nn_value_typename(valupper, false));
    }
    length = nn_string_getlength(string);
    idxlower = 0;
    if(nn_value_isnumber(vallower))
    {
        idxlower = nn_value_asnumber(vallower);
    }
    if(valupper.isNull())
    {
        idxupper = length;
    }
    else
    {
        idxupper = nn_value_asnumber(valupper);
    }
    if(idxlower < 0 || (idxupper < 0 && ((length + idxupper) < 0)) || idxlower >= length)
    {
        /* always return an empty string... */
        if(!willassign)
        {
            /* +1 for the string itself */
            nn_vmbits_stackpopn(state, 3);
        }
        nn_vmbits_stackpush(state, nn_value_fromobject(nn_string_internlen("", 0)));
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
        nn_vmbits_stackpopn(state, 3);
    }
    nn_vmbits_stackpush(state, nn_value_fromobject(nn_string_copylen(nn_string_getdata(string) + start, end - start)));
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_getrangedindex(NNState* state)
{
    bool isgotten;
    uint8_t willassign;
    NNValue vfrom;
    willassign = nn_vmbits_readbyte(state);
    isgotten = true;
    vfrom = nn_vmbits_stackpeek(state, 2);
    if(nn_value_isobject(vfrom))
    {
        switch(nn_value_asobject(vfrom)->type)
        {
            case NEON_OBJTYPE_STRING:
            {
                if(!nn_vmutil_dogetrangedindexofstring(state, nn_value_asstring(vfrom), willassign))
                {
                    return false;
                }
                break;
            }
            case NEON_OBJTYPE_ARRAY:
            {
                if(!nn_vmutil_dogetrangedindexofarray(state, nn_value_asarray(vfrom), willassign))
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
        return nn_except_throw(state, "cannot range index object of type %s", nn_value_typename(vfrom, false));
    }
    return true;
}

NEON_FORCEINLINE bool nn_vmutil_doindexgetdict(NNState* state, NNObjDict* dict, bool willassign)
{
    NNValue vindex;
    NNProperty* field;
    vindex = nn_vmbits_stackpeek(state, 0);
    field = nn_dict_getentry(dict, vindex);
    if(field != nullptr)
    {
        if(!willassign)
        {
            /* we can safely get rid of the index from the stack */
            nn_vmbits_stackpopn(state, 2);
        }
        nn_vmbits_stackpush(state, (NNValue)field->value);
        return true;
    }
    nn_vmbits_stackpopn(state, 1);
    nn_vmbits_stackpush(state, nn_value_makenull());
    return true;
}

NEON_FORCEINLINE bool nn_vmutil_doindexgetmodule(NNState* state, NNObjModule* module, bool willassign)
{
    NNValue vindex;
    NNValue result;
    vindex = nn_vmbits_stackpeek(state, 0);
    if(module->deftable.get(vindex, &result))
    {
        if(!willassign)
        {
            /* we can safely get rid of the index from the stack */
            nn_vmbits_stackpopn(state, 2);
        }
        nn_vmbits_stackpush(state, result);
        return true;
    }
    nn_vmbits_stackpop(state);
    return nn_except_throw(state, "%s is undefined in module %s", nn_string_getdata(nn_value_tostring(vindex)), module->name);
}

NEON_FORCEINLINE bool nn_vmutil_doindexgetstring(NNState* state, NNObjString* string, bool willassign)
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
    okindex = false;
    vindex = nn_vmbits_stackpeek(state, 0);
    if(!nn_value_isnumber(vindex))
    {
        if(nn_value_isrange(vindex))
        {
            rng = nn_value_asrange(vindex);
            nn_vmbits_stackpop(state);
            nn_vmbits_stackpush(state, nn_value_makenumber(rng->lower));
            nn_vmbits_stackpush(state, nn_value_makenumber(rng->upper));
            return nn_vmutil_dogetrangedindexofstring(state, string, willassign);
        }
        nn_vmbits_stackpopn(state, 1);
        return nn_except_throw(state, "strings are numerically indexed");
    }
    index = nn_value_asnumber(vindex);
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
        nn_vmbits_stackpopn(state, 2);
    }
    if(okindex)
    {
        nn_vmbits_stackpush(state, nn_value_fromobject(nn_string_copylen(nn_string_getdata(string) + start, end - start)));
    }
    else
    {
        nn_vmbits_stackpush(state, nn_value_makenull());
    }
    return true;

    nn_vmbits_stackpopn(state, 1);
    #if 0
        return nn_except_throw(state, "string index %d out of range of %d", realindex, maxlength);
    #else
        nn_vmbits_stackpush(state, nn_value_makenull());
        return true;
    #endif
}

NEON_FORCEINLINE bool nn_vmutil_doindexgetarray(NNState* state, NNObjArray* list, bool willassign)
{
    long index;
    NNValue finalval;
    NNValue vindex;
    NNObjRange* rng;
    vindex = nn_vmbits_stackpeek(state, 0);
    if(nn_util_unlikely(!nn_value_isnumber(vindex)))
    {
        if(nn_value_isrange(vindex))
        {
            rng = nn_value_asrange(vindex);
            nn_vmbits_stackpop(state);
            nn_vmbits_stackpush(state, nn_value_makenumber(rng->lower));
            nn_vmbits_stackpush(state, nn_value_makenumber(rng->upper));
            return nn_vmutil_dogetrangedindexofarray(state, list, willassign);
        }
        nn_vmbits_stackpop(state);
        return nn_except_throw(state, "list are numerically indexed");
    }
    index = nn_value_asnumber(vindex);
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
        finalval = nn_value_makenull();
    }
    if(!willassign)
    {
        /*
        // we can safely get rid of the index from the stack
        // +1 for the list itself
        */
        nn_vmbits_stackpopn(state, 2);
    }
    nn_vmbits_stackpush(state, finalval);
    return true;
}

static NNProperty* nn_vmutil_checkoverloadrequirements(const char* ccallername, NNValue target, NNObjString* name)
{
    NNProperty* field;
    if(!nn_value_isinstance(target))
    {
        fprintf(stderr, "%s: not an instance\n", ccallername);
        return nullptr;
    }
    field = nn_instance_getmethod(nn_value_asinstance(target), name);
    if(field == nullptr)
    {
        fprintf(stderr, "%s: failed to get '%s'\n", ccallername, nn_string_getdata(name));
        return nullptr;
    }
    if(!nn_value_iscallable((NNValue)field->value))
    {
        fprintf(stderr, "%s: field not callable\n", ccallername);
        return nullptr;
    }
    return field;
}

NEON_FORCEINLINE bool nn_vmutil_tryoverloadbasic(NNState* state, NNObjString* name, NNValue target, NNValue firstargvval, NNValue setvalue, bool willassign)
{
    size_t nargc;
    NNValue finalval;
    NNProperty* field;
    NNValue scrargv[3];
    nargc = 1;
    field = nn_vmutil_checkoverloadrequirements("tryoverloadgeneric", target, name); 
    scrargv[0] = firstargvval;
    if(willassign)
    {
        scrargv[0] = setvalue;
        scrargv[1] = firstargvval;
        nargc = 2;
    }
    if(nn_nestcall_callfunction(state, (NNValue)field->value, target, scrargv, nargc, &finalval, true))
    {
        if(!willassign)
        {
            nn_vmbits_stackpopn(state, 2);
        }
        nn_vmbits_stackpush(state, finalval);
        return true;
    }
    return false;
}

NEON_FORCEINLINE bool nn_vmutil_tryoverloadmath(NNState* state, NNObjString* name, NNValue target, NNValue right, bool willassign)
{
    return nn_vmutil_tryoverloadbasic(state, name, target, right, nn_value_makenull(), willassign);
}

NEON_FORCEINLINE bool nn_vmutil_tryoverloadgeneric(NNState* state, NNObjString* name, NNValue target, bool willassign)
{
    NNValue setval;
    NNValue firstargval;
    firstargval = nn_vmbits_stackpeek(state, 0);
    setval = nn_vmbits_stackpeek(state, 1);
    return nn_vmutil_tryoverloadbasic(state, name, target, firstargval, setval, willassign);
}


NEON_FORCEINLINE bool nn_vmdo_indexget(NNState* state)
{
    bool isgotten;
    uint8_t willassign;
    NNValue thisval;
    willassign = nn_vmbits_readbyte(state);
    isgotten = true;
    thisval = nn_vmbits_stackpeek(state, 1);
    if(nn_util_unlikely(nn_value_isinstance(thisval)))
    {
        if(nn_vmutil_tryoverloadgeneric(state, state->defaultstrings.nmindexget, thisval, willassign))
        {
            return true;
        }
    }
    if(nn_util_likely(nn_value_isobject(thisval)))
    {
        switch(nn_value_asobject(thisval)->type)
        {
            case NEON_OBJTYPE_STRING:
            {
                if(!nn_vmutil_doindexgetstring(state, nn_value_asstring(thisval), willassign))
                {
                    return false;
                }
                break;
            }
            case NEON_OBJTYPE_ARRAY:
            {
                if(!nn_vmutil_doindexgetarray(state, nn_value_asarray(thisval), willassign))
                {
                    return false;
                }
                break;
            }
            case NEON_OBJTYPE_DICT:
            {
                if(!nn_vmutil_doindexgetdict(state, nn_value_asdict(thisval), willassign))
                {
                    return false;
                }
                break;
            }
            case NEON_OBJTYPE_MODULE:
            {
                if(!nn_vmutil_doindexgetmodule(state, nn_value_asmodule(thisval), willassign))
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
        nn_except_throw(state, "cannot index object of type %s", nn_value_typename(thisval, false));
    }
    return true;
}


NEON_FORCEINLINE bool nn_vmutil_dosetindexdict(NNState* state, NNObjDict* dict, NNValue index, NNValue value)
{
    nn_dict_setentry(dict, index, value);
    /* pop the value, index and dict out */
    nn_vmbits_stackpopn(state, 3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = dict[index] = 10
    */
    nn_vmbits_stackpush(state, value);
    return true;
}

NEON_FORCEINLINE bool nn_vmutil_dosetindexmodule(NNState* state, NNObjModule* module, NNValue index, NNValue value)
{
    module->deftable.set(index, value);
    /* pop the value, index and dict out */
    nn_vmbits_stackpopn(state, 3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = dict[index] = 10
    */
    nn_vmbits_stackpush(state, value);
    return true;
}

NEON_FORCEINLINE bool nn_vmutil_doindexsetarray(NNState* state, NNObjArray* list, NNValue index, NNValue value)
{
    int tmp;
    int rawpos;
    int position;
    int ocnt;
    int ocap;
    int vasz;
    if(nn_util_unlikely(!nn_value_isnumber(index)))
    {
        nn_vmbits_stackpopn(state, 3);
        /* pop the value, index and list out */
        return nn_except_throw(state, "list are numerically indexed");
    }
    ocap = list->varray.capacity();
    ocnt = list->varray.count();
    rawpos = nn_value_asnumber(index);
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
                nn_array_push(list, nn_value_makenull());
            }
            else
            {
                tmp = position + 1;
                while(tmp > vasz)
                {
                    nn_array_push(list, nn_value_makenull());
                    tmp--;
                }
            }
        }
        fprintf(stderr, "setting value at position %ld (array count: %ld)\n", (long)position, (long)list->varray.count());
    }
    list->varray.set(position, value);
    /* pop the value, index and list out */
    nn_vmbits_stackpopn(state, 3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = list[index] = 10    
    */
    nn_vmbits_stackpush(state, value);
    return true;
    /*
    // pop the value, index and list out
    //nn_vmbits_stackpopn(state, 3);
    //return nn_except_throw(state, "lists index %d out of range", rawpos);
    //nn_vmbits_stackpush(state, nn_value_makenull());
    //return true;
    */
}

NEON_FORCEINLINE bool nn_vmutil_dosetindexstring(NNState* state, NNObjString* os, NNValue index, NNValue value)
{
    int iv;
    int rawpos;
    int position;
    int oslen;
    if(!nn_value_isnumber(index))
    {
        nn_vmbits_stackpopn(state, 3);
        /* pop the value, index and list out */
        return nn_except_throw(state, "strings are numerically indexed");
    }
    iv = nn_value_asnumber(value);
    rawpos = nn_value_asnumber(index);
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
        nn_vmbits_stackpopn(state, 3);
        /*
        // leave the value on the stack for consumption
        // e.g. variable = list[index] = 10
        */
        nn_vmbits_stackpush(state, value);
        return true;
    }
    else
    {
        nn_string_appendbyte(os, iv);
        nn_vmbits_stackpopn(state, 3);
        nn_vmbits_stackpush(state, value);
    }
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_indexset(NNState* state)
{
    bool isset;
    NNValue value;
    NNValue index;
    NNValue thisval;
    isset = true;
    thisval = nn_vmbits_stackpeek(state, 2);
    if(nn_util_unlikely(nn_value_isinstance(thisval)))
    {
        if(nn_vmutil_tryoverloadgeneric(state, state->defaultstrings.nmindexset, thisval, true))
        {
            return true;
        }
    }
    if(nn_util_likely(nn_value_isobject(thisval)))
    {
        value = nn_vmbits_stackpeek(state, 0);
        index = nn_vmbits_stackpeek(state, 1);
        switch(nn_value_asobject(thisval)->type)
        {
            case NEON_OBJTYPE_ARRAY:
                {
                    if(!nn_vmutil_doindexsetarray(state, nn_value_asarray(thisval), index, value))
                    {
                        return false;
                    }
                }
                break;
            case NEON_OBJTYPE_STRING:
                {
                    if(!nn_vmutil_dosetindexstring(state, nn_value_asstring(thisval), index, value))
                    {
                        return false;
                    }
                }
                break;
            case NEON_OBJTYPE_DICT:
                {
                    return nn_vmutil_dosetindexdict(state, nn_value_asdict(thisval), index, value);
                }
                break;
            case NEON_OBJTYPE_MODULE:
                {
                    return nn_vmutil_dosetindexmodule(state, nn_value_asmodule(thisval), index, value);
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
        return nn_except_throw(state, "type of %s is not a valid iterable", nn_value_typename(nn_vmbits_stackpeek(state, 3), false));
    }
    return true;
}

NEON_FORCEINLINE bool nn_vmutil_concatenate(NNState* state)
{
    NNValue vleft;
    NNValue vright;
    NNIOStream pr;
    NNObjString* result;
    vright = nn_vmbits_stackpeek(state, 0);
    vleft = nn_vmbits_stackpeek(state, 1);
    nn_iostream_makestackstring(&pr);
    nn_iostream_printvalue(&pr, vleft, false, true);
    nn_iostream_printvalue(&pr, vright, false, true);
    result = nn_iostream_takestring(&pr);
    nn_iostream_destroy(&pr);
    nn_vmbits_stackpopn(state, 2);
    nn_vmbits_stackpush(state, nn_value_fromobject(result));
    return true;
}

#if 0
NEON_INLINE NNValue nn_vmutil_floordiv(double a, double b)
{
    int d;
    double r;
    d = (int)a / (int)b;
    r = d - ((d * b == a) & ((a < 0) ^ (b < 0)));
    return nn_value_makenumber(r);
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
    return nn_value_makenumber(r);
}

NEON_INLINE NNValue nn_vmutil_pow(double a, double b)
{
    double r;
    r = pow(a, b);
    return nn_value_makenumber(r);
}

NEON_FORCEINLINE NNProperty* nn_vmutil_getclassproperty(NNState* state, NNObjClass* klass, NNObjString* name, bool alsothrow)
{
    NNProperty* field;
    field = klass->instmethods.getfieldbyostr(name);
    if(field != nullptr)
    {
        if(nn_value_getmethodtype((NNValue)field->value) == NEON_FNCONTEXTTYPE_STATIC)
        {
            if(nn_util_methodisprivate(name))
            {
                if(alsothrow)
                {
                    nn_except_throw(state, "cannot call private property '%s' of class %s", nn_string_getdata(name),
                        nn_string_getdata(klass->name));
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
                    nn_except_throw(state, "cannot call private property '%s' of class %s", nn_string_getdata(name),
                        nn_string_getdata(klass->name));
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
        nn_except_throw(state, "class %s does not have a static property or method named '%s'",
            nn_string_getdata(klass->name), nn_string_getdata(name));
    }
    return nullptr;
}

NEON_FORCEINLINE NNProperty* nn_vmutil_getproperty(NNState* state, NNValue peeked, NNObjString* name)
{
    NNProperty* field;
    switch(nn_value_asobject(peeked)->type)
    {
        case NEON_OBJTYPE_MODULE:
            {
                NNObjModule* module;
                module = nn_value_asmodule(peeked);
                field = module->deftable.getfieldbyostr(name);
                if(field != nullptr)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        nn_except_throw(state, "cannot get private module property '%s'", nn_string_getdata(name));
                        return nullptr;
                    }
                    return field;
                }
                nn_except_throw(state, "module '%s' does not have a field named '%s'", nn_string_getdata(module->name), nn_string_getdata(name));
                return nullptr;
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                NNObjClass* klass;
                klass = nn_value_asclass(peeked);
                field = nn_vmutil_getclassproperty(state, klass, name, true);
                if(field != nullptr)
                {
                    return field;
                }
                return nullptr;
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                NNObjInstance* instance;
                instance = nn_value_asinstance(peeked);
                field = instance->properties.getfieldbyostr(name);
                if(field != nullptr)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        nn_except_throw(state, "cannot call private property '%s' from instance of %s", nn_string_getdata(name), nn_string_getdata(instance->klass->name));
                        return nullptr;
                    }
                    return field;
                }
                if(nn_util_methodisprivate(name))
                {
                    nn_except_throw(state, "cannot bind private property '%s' to instance of %s", nn_string_getdata(name), nn_string_getdata(instance->klass->name));
                    return nullptr;
                }
                if(nn_vmutil_bindmethod(state, instance->klass, name))
                {
                    return field;
                }
                nn_except_throw(state, "instance of class %s does not have a property or method named '%s'",
                    nn_string_getdata(nn_value_asinstance(peeked)->klass->name), nn_string_getdata(name));
                return nullptr;
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                field = nn_class_getpropertyfield(state->classprimstring, name);
                if(field == nullptr)
                {
                    field = nn_vmutil_getclassproperty(state, state->classprimstring, name, false);
                }
                if(field != nullptr)
                {
                    return field;
                }
                nn_except_throw(state, "class String has no named property '%s'", nn_string_getdata(name));
                return nullptr;
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                field = nn_class_getpropertyfield(state->classprimarray, name);
                if(field == nullptr)
                {
                    field = nn_vmutil_getclassproperty(state, state->classprimarray, name, false);
                }
                if(field != nullptr)
                {
                    return field;
                }
                nn_except_throw(state, "class Array has no named property '%s'", nn_string_getdata(name));
                return nullptr;
            }
            break;
        case NEON_OBJTYPE_RANGE:
            {
                field = nn_class_getpropertyfield(state->classprimrange, name);
                if(field == nullptr)
                {
                    field = nn_vmutil_getclassproperty(state, state->classprimrange, name, false);
                }
                if(field != nullptr)
                {
                    return field;
                }
                nn_except_throw(state, "class Range has no named property '%s'", nn_string_getdata(name));
                return nullptr;
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                field = nn_value_asdict(peeked)->htab.getfieldbyostr(name);
                if(field == nullptr)
                {
                    field = nn_class_getpropertyfield(state->classprimdict, name);
                }
                if(field != nullptr)
                {
                    return field;
                }
                nn_except_throw(state, "unknown key or class Dict property '%s'", nn_string_getdata(name));
                return nullptr;
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                field = nn_class_getpropertyfield(state->classprimfile, name);
                if(field == nullptr)
                {
                    field = nn_vmutil_getclassproperty(state, state->classprimfile, name, false);
                }
                if(field != nullptr)
                {
                    return field;
                }
                nn_except_throw(state, "class File has no named property '%s'", nn_string_getdata(name));
                return nullptr;
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
        case NEON_OBJTYPE_FUNCCLOSURE:
        case NEON_OBJTYPE_FUNCSCRIPT:
        case NEON_OBJTYPE_FUNCNATIVE:
            {
                field = nn_class_getpropertyfield(state->classprimcallable, name);
                if(field != nullptr)
                {
                    return field;
                }
                else
                {
                    field = nn_vmutil_getclassproperty(state, state->classprimcallable, name, false);
                    if(field != nullptr)
                    {
                        return field;
                    }
                }
                nn_except_throw(state, "class Function has no named property '%s'", nn_string_getdata(name));
                return nullptr;
            }
            break;
        default:
            {
                nn_except_throw(state, "object of type %s does not carry properties", nn_value_typename(peeked, false));
                return nullptr;
            }
            break;
    }
    return nullptr;
}

NEON_FORCEINLINE bool nn_vmdo_propertyget(NNState* state)
{
    NNValue peeked;
    NNProperty* field;
    NNObjString* name;
    name = nn_vmbits_readstring(state);
    peeked = nn_vmbits_stackpeek(state, 0);
    if(nn_value_isobject(peeked))
    {
        field = nn_vmutil_getproperty(state, peeked, name);
        if(field == nullptr)
        {
            return false;
        }
        else
        {
            if(field->type == NEON_PROPTYPE_FUNCTION)
            {
                nn_vm_callvaluewithobject(state, (NNValue)field->value, peeked, 0, false);
            }
            else
            {
                nn_vmbits_stackpop(state);
                nn_vmbits_stackpush(state, (NNValue)field->value);
            }
        }
        return true;
    }
    else
    {
        nn_except_throw(state, "'%s' of type %s does not have properties", nn_string_getdata(nn_value_tostring(peeked)),
            nn_value_typename(peeked, false));
    }
    return false;
}

NEON_FORCEINLINE bool nn_vmdo_propertygetself(NNState* state)
{
    NNValue peeked;
    NNObjString* name;
    NNObjClass* klass;
    NNObjInstance* instance;
    NNObjModule* module;
    NNProperty* field;
    name = nn_vmbits_readstring(state);
    peeked = nn_vmbits_stackpeek(state, 0);
    if(nn_value_isinstance(peeked))
    {
        instance = nn_value_asinstance(peeked);
        field = instance->properties.getfieldbyostr(name);
        if(field != nullptr)
        {
            /* pop the instance... */
            nn_vmbits_stackpop(state);
            nn_vmbits_stackpush(state, (NNValue)field->value);
            return true;
        }
        if(nn_vmutil_bindmethod(state, instance->klass, name))
        {
            return true;
        }
        nn_vmmac_tryraise(state, false, "instance of class %s does not have a property or method named '%s'",
            nn_string_getdata(nn_value_asinstance(peeked)->klass->name), nn_string_getdata(name));
        return false;
    }
    else if(nn_value_isclass(peeked))
    {
        klass = nn_value_asclass(peeked);
        field = klass->instmethods.getfieldbyostr(name);
        if(field != nullptr)
        {
            if(nn_value_getmethodtype((NNValue)field->value) == NEON_FNCONTEXTTYPE_STATIC)
            {
                /* pop the class... */
                nn_vmbits_stackpop(state);
                nn_vmbits_stackpush(state, (NNValue)field->value);
                return true;
            }
        }
        else
        {
            field = nn_class_getstaticproperty(klass, name);
            if(field != nullptr)
            {
                /* pop the class... */
                nn_vmbits_stackpop(state);
                nn_vmbits_stackpush(state, (NNValue)field->value);
                return true;
            }
        }
        nn_vmmac_tryraise(state, false, "class %s does not have a static property or method named '%s'", nn_string_getdata(klass->name), nn_string_getdata(name));
        return false;
    }
    else if(nn_value_ismodule(peeked))
    {
        module = nn_value_asmodule(peeked);
        field = module->deftable.getfieldbyostr(name);
        if(field != nullptr)
        {
            /* pop the module... */
            nn_vmbits_stackpop(state);
            nn_vmbits_stackpush(state, (NNValue)field->value);
            return true;
        }
        nn_vmmac_tryraise(state, false, "module '%s' does not have a field named '%s'", nn_string_getdata(module->name), nn_string_getdata(name));
        return false;
    }
    nn_vmmac_tryraise(state, false, "'%s' of type %s does not have properties", nn_string_getdata(nn_value_tostring(peeked)),
        nn_value_typename(peeked, false));
    return false;
}

NEON_FORCEINLINE bool nn_vmdo_propertyset(NNState* state)
{
    NNValue value;
    NNValue vtarget;
    NNValue vpeek;
    NNObjClass* klass;
    NNObjString* name;
    NNObjDict* dict;
    NNObjInstance* instance;
    vtarget = nn_vmbits_stackpeek(state, 1);
    name = nn_vmbits_readstring(state);
    vpeek = nn_vmbits_stackpeek(state, 0);
    if(nn_value_isinstance(vtarget))
    {
        instance = nn_value_asinstance(vtarget);
        nn_instance_defproperty(instance, name, vpeek);
        value = nn_vmbits_stackpop(state);
        /* removing the instance object */
        nn_vmbits_stackpop(state);
        nn_vmbits_stackpush(state, value);
    }
    else if(nn_value_isdict(vtarget))
    {
        dict = nn_value_asdict(vtarget);
        nn_dict_setentry(dict, nn_value_fromobject(name), vpeek);
        value = nn_vmbits_stackpop(state);
        /* removing the dictionary object */
        nn_vmbits_stackpop(state);
        nn_vmbits_stackpush(state, value);
    }
    /* nn_value_isclass(...) */
    else
    {
        klass = nullptr;
        if(nn_value_isclass(vtarget))
        {
            klass = nn_value_asclass(vtarget);
        }
        else if(nn_value_isinstance(vtarget))
        {
            klass = nn_value_asinstance(vtarget)->klass;
        }
        else
        {
            klass = nn_value_getclassfor(state, vtarget);
            /* still no class found? then it cannot carry properties */
            if(klass == nullptr)
            {
                nn_except_throw(state, "object of type %s cannot carry properties", nn_value_typename(vtarget, false));
                return false;
            }
        }
        if(nn_value_iscallable(vpeek))
        {
            nn_class_defmethod(klass, name, vpeek);
        }
        else
        {
            nn_class_defproperty(klass, name, vpeek);
        }
        value = nn_vmbits_stackpop(state);
        /* removing the class object */
        nn_vmbits_stackpop(state);
        nn_vmbits_stackpush(state, value);
    }

    return true;
}

NEON_FORCEINLINE double nn_vmutil_valtonum(NNValue v)
{
    if(v.isNull())
    {
        return 0;
    }
    if(nn_value_isbool(v))
    {
        if(nn_value_asbool(v))
        {
            return 1;
        }
        return 0;
    }
    return nn_value_asnumber(v);
}


NEON_FORCEINLINE uint32_t nn_vmutil_valtouint(NNValue v)
{
    if(v.isNull())
    {
        return 0;
    }
    if(nn_value_isbool(v))
    {
        if(nn_value_asbool(v))
        {
            return 1;
        }
        return 0;
    }
    return nn_value_asnumber(v);
}

NEON_FORCEINLINE long nn_vmutil_valtoint(NNValue v)
{
    return (long)nn_vmutil_valtonum(v);
}

NEON_FORCEINLINE bool nn_vmdo_dobinarydirect(NNState* state)
{
    bool isfail;
    bool willassign;
    int64_t ibinright;
    int64_t ibinleft;
    uint32_t ubinright;
    uint32_t ubinleft;
    double dbinright;
    double dbinleft;
    NNOpCode instruction;
    NNValue res;
    NNValue binvalleft;
    NNValue binvalright;
    willassign = false;
    instruction = (NNOpCode)GCSingleton::get()->vmstate.currentinstr.code;
    binvalright = nn_vmbits_stackpeek(state, 0);
    binvalleft = nn_vmbits_stackpeek(state, 1);
    if(nn_util_unlikely(nn_value_isinstance(binvalleft)))
    {
        switch(instruction)
        {
            case NEON_OP_PRIMADD:
                {
                    if(nn_vmutil_tryoverloadmath(state, state->defaultstrings.nmadd, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
            case NEON_OP_PRIMSUBTRACT:
                {
                    if(nn_vmutil_tryoverloadmath(state, state->defaultstrings.nmsub, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
            case NEON_OP_PRIMDIVIDE:
                {
                    if(nn_vmutil_tryoverloadmath(state, state->defaultstrings.nmdiv, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
            case NEON_OP_PRIMMULTIPLY:
                {
                    if(nn_vmutil_tryoverloadmath(state, state->defaultstrings.nmmul, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
            case NEON_OP_PRIMAND:
                {
                    if(nn_vmutil_tryoverloadmath(state, state->defaultstrings.nmband, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
            case NEON_OP_PRIMOR:
                {
                    if(nn_vmutil_tryoverloadmath(state, state->defaultstrings.nmbor, binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
            case NEON_OP_PRIMBITXOR:
                {
                    if(nn_vmutil_tryoverloadmath(state, state->defaultstrings.nmbxor, binvalleft, binvalright, willassign))
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
    isfail = (
        (!nn_value_isnumber(binvalright) && !nn_value_isbool(binvalright) && !binvalright.isNull()) ||
        (!nn_value_isnumber(binvalleft) && !nn_value_isbool(binvalleft) && !binvalleft.isNull())
    );
    if(isfail)
    {
        nn_vmmac_tryraise(state, false, "unsupported operand %s for %s and %s", nn_dbg_op2str(instruction), nn_value_typename(binvalleft, false), nn_value_typename(binvalright, false));
        return false;
    }
    binvalright = nn_vmbits_stackpop(state);
    binvalleft = nn_vmbits_stackpop(state);
    res = nn_value_makenull();
    switch(instruction)
    {
        case NEON_OP_PRIMADD:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = nn_value_makenumber(dbinleft + dbinright);
            }
            break;
        case NEON_OP_PRIMSUBTRACT:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = nn_value_makenumber(dbinleft - dbinright);
            }
            break;
        case NEON_OP_PRIMDIVIDE:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = nn_value_makenumber(dbinleft / dbinright);
            }
            break;
        case NEON_OP_PRIMMULTIPLY:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = nn_value_makenumber(dbinleft * dbinright);
            }
            break;
        case NEON_OP_PRIMAND:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = nn_value_makenumber(ibinleft & ibinright);
            }
            break;
        case NEON_OP_PRIMOR:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = nn_value_makenumber(ibinleft | ibinright);
            }
            break;
        case NEON_OP_PRIMBITXOR:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = nn_value_makenumber(ibinleft ^ ibinright);
            }
            break;
        case NEON_OP_PRIMSHIFTLEFT:
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
                res = nn_value_makenumber(ubinleft << ubinright);

            }
            break;
        case NEON_OP_PRIMSHIFTRIGHT:
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
                res = nn_value_makenumber(ubinleft >> ubinright);
            }
            break;
        case NEON_OP_PRIMGREATER:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = nn_value_makebool(dbinleft > dbinright);
            }
            break;
        case NEON_OP_PRIMLESSTHAN:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = nn_value_makebool(dbinleft < dbinright);
            }
            break;
        default:
            {
                fprintf(stderr, "unhandled instruction %d (%s)!\n", instruction, nn_dbg_op2str(instruction));
                return false;
            }
            break;
    }
    nn_vmbits_stackpush(state, res);
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_globaldefine(NNState* state)
{
    NNValue val;
    NNObjString* name;
    name = nn_vmbits_readstring(state);
    val = nn_vmbits_stackpeek(state, 0);
    auto tab = &GCSingleton::get()->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module->deftable;
    tab->set(nn_value_fromobject(name), val);
    nn_vmbits_stackpop(state);
    #if (defined(DEBUG_TABLE) && DEBUG_TABLE) || 0
    state->print(state->debugwriter, &GCSingleton::get()->declaredglobals, "globals");
    #endif
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_globalget(NNState* state)
{
    NNObjString* name;
    NNProperty* field;
    name = nn_vmbits_readstring(state);
    auto tab = &GCSingleton::get()->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module->deftable;
    field = tab->getfieldbyostr(name);
    if(field == nullptr)
    {
        field = GCSingleton::get()->declaredglobals.getfieldbyostr(name);
        if(field == nullptr)
        {
            nn_except_throwclass(state, state->exceptions.stdexception, "global name '%s' is not defined", nn_string_getdata(name));
            return false;
        }
    }
    nn_vmbits_stackpush(state, (NNValue)field->value);
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_globalset(NNState* state)
{
    NNObjString* name;
    NNObjModule* module;
    name = nn_vmbits_readstring(state);
    module = GCSingleton::get()->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module;
    auto table = &module->deftable;
    if(table->set(nn_value_fromobject(name), nn_vmbits_stackpeek(state, 0)))
    {
        if(state->conf.enablestrictmode)
        {
            table->remove(nn_value_fromobject(name));
            nn_vmmac_tryraise(state, false, "global name '%s' was not declared", nn_string_getdata(name));
            return false;
        }
    }
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_localget(NNState* state)
{
    size_t ssp;
    uint16_t slot;
    NNValue val;
    slot = nn_vmbits_readshort(state);
    ssp = GCSingleton::get()->vmstate.currentframe->stackslotpos;
    val = GCSingleton::get()->vmstate.stackvalues[ssp + slot];
    nn_vmbits_stackpush(state, val);
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_localset(NNState* state)
{
    size_t ssp;
    uint16_t slot;
    NNValue peeked;
    slot = nn_vmbits_readshort(state);
    peeked = nn_vmbits_stackpeek(state, 0);
    ssp = GCSingleton::get()->vmstate.currentframe->stackslotpos;
    GCSingleton::get()->vmstate.stackvalues[ssp + slot] = peeked;

    return true;
}

/*NEON_OP_FUNCARGOPTIONAL*/
NEON_FORCEINLINE bool nn_vmdo_funcargoptional(NNState* state)
{
    size_t ssp;
    size_t putpos;
    uint16_t slot;
    NNValue cval;
    NNValue peeked;
    slot = nn_vmbits_readshort(state);
    peeked = nn_vmbits_stackpeek(state, 1);
    cval = nn_vmbits_stackpeek(state, 2);

        ssp = GCSingleton::get()->vmstate.currentframe->stackslotpos;

        #if 0
            putpos = (GCSingleton::get()->vmstate.stackidx + (-1 - 1)) ;
        #else
            #if 0
                putpos = GCSingleton::get()->vmstate.stackidx + (slot - 0);
            #else
                #if 0
                    putpos = GCSingleton::get()->vmstate.stackidx + (slot);
                #else
                    putpos = (ssp + slot) + 0;
                #endif
            #endif
        #endif
    #if 1
    {
        NNIOStream* pr = state->stderrprinter;
        nn_iostream_printf(pr, "funcargoptional: slot=%d putpos=%ld cval=<", slot, putpos);
        nn_iostream_printvalue(pr, cval, true, false);
        nn_iostream_printf(pr, ">, peeked=<");
        nn_iostream_printvalue(pr, peeked, true, false);
        nn_iostream_printf(pr, ">\n");
    }
    #endif
    if(cval.isNull())
    {
        GCSingleton::get()->vmstate.stackvalues[putpos] = peeked;
    }
    /*
    else
    {
        #if 0
            nn_vmbits_stackpop(state);
        #endif
    }
    */
    nn_vmbits_stackpop(state);

    return true;
}

NEON_FORCEINLINE bool nn_vmdo_funcargget(NNState* state)
{
    size_t ssp;
    uint16_t slot;
    NNValue val;
    slot = nn_vmbits_readshort(state);
    ssp = GCSingleton::get()->vmstate.currentframe->stackslotpos;
    val = GCSingleton::get()->vmstate.stackvalues[ssp + slot];
    nn_vmbits_stackpush(state, val);
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_funcargset(NNState* state)
{
    size_t ssp;
    uint16_t slot;
    NNValue peeked;
    slot = nn_vmbits_readshort(state);
    peeked = nn_vmbits_stackpeek(state, 0);
    ssp = GCSingleton::get()->vmstate.currentframe->stackslotpos;
    GCSingleton::get()->vmstate.stackvalues[ssp + slot] = peeked;
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_makeclosure(NNState* state)
{
    size_t i;
    int upvidx;
    size_t ssp;
    uint8_t islocal;
    NNValue thisval;
    NNValue* upvals;
    NNObjFunction* function;
    NNObjFunction* closure;
    function = nn_value_asfunction(nn_vmbits_readconst(state));
    #if 0
        thisval = nn_vmbits_stackpeek(state, 3);
    #else
        thisval = nn_value_makenull();
    #endif
    closure = nn_object_makefuncclosure(function, thisval);
    nn_vmbits_stackpush(state, nn_value_fromobject(closure));
    for(i = 0; i < (size_t)closure->upvalcount; i++)
    {
        islocal = nn_vmbits_readbyte(state);
        upvidx = nn_vmbits_readshort(state);
        if(islocal)
        {
            ssp = GCSingleton::get()->vmstate.currentframe->stackslotpos;
            upvals = &GCSingleton::get()->vmstate.stackvalues[ssp + upvidx];
            closure->fnclosure.upvalues[i] = nn_vmutil_upvaluescapture(state, upvals, upvidx);
        }
        else
        {
            closure->fnclosure.upvalues[i] = GCSingleton::get()->vmstate.currentframe->closure->fnclosure.upvalues[upvidx];
        }
    }
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_makearray(NNState* state)
{
    int i;
    int count;
    NNObjArray* array;
    count = nn_vmbits_readshort(state);
    array = nn_object_makearray();
    GCSingleton::get()->vmstate.stackvalues[GCSingleton::get()->vmstate.stackidx + (-count - 1)] = nn_value_fromobject(array);
    for(i = count - 1; i >= 0; i--)
    {
        nn_array_push(array, nn_vmbits_stackpeek(state, i));
    }
    nn_vmbits_stackpopn(state, count);
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_makedict(NNState* state)
{
    size_t i;
    size_t count;
    size_t realcount;
    NNValue name;
    NNValue value;
    NNObjDict* dict;
    /* 1 for key, 1 for value */
    realcount = nn_vmbits_readshort(state);
    count = realcount * 2;
    dict = nn_object_makedict();
    GCSingleton::get()->vmstate.stackvalues[GCSingleton::get()->vmstate.stackidx + (-count - 1)] = nn_value_fromobject(dict);
    for(i = 0; i < count; i += 2)
    {
        name = GCSingleton::get()->vmstate.stackvalues[GCSingleton::get()->vmstate.stackidx + (-count + i)];
        if(!nn_value_isstring(name) && !nn_value_isnumber(name) && !nn_value_isbool(name))
        {
            nn_vmmac_tryraise(state, false, "dictionary key must be one of string, number or boolean");
            return false;
        }
        value = GCSingleton::get()->vmstate.stackvalues[GCSingleton::get()->vmstate.stackidx + (-count + i + 1)];
        nn_dict_setentry(dict, name, value);
    }
    nn_vmbits_stackpopn(state, count);
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_dobinaryfunc(NNState* state, const char* opname, nnbinopfunc_t opfn)
{
    double dbinright;
    double dbinleft;
    NNValue binvalright;
    NNValue binvalleft;
    binvalright = nn_vmbits_stackpeek(state, 0);
    binvalleft = nn_vmbits_stackpeek(state, 1);
    if((!nn_value_isnumber(binvalright) && !nn_value_isbool(binvalright))
    || (!nn_value_isnumber(binvalleft) && !nn_value_isbool(binvalleft)))
    {
        nn_vmmac_tryraise(state, false, "unsupported operand %s for %s and %s", opname, nn_value_typename(binvalleft, false), nn_value_typename(binvalright, false));
        return false;
    }
    binvalright = nn_vmbits_stackpop(state);
    dbinright = nn_value_isbool(binvalright) ? (nn_value_asbool(binvalright) ? 1 : 0) : nn_value_asnumber(binvalright);
    binvalleft = nn_vmbits_stackpop(state);
    dbinleft = nn_value_isbool(binvalleft) ? (nn_value_asbool(binvalleft) ? 1 : 0) : nn_value_asnumber(binvalleft);
    nn_vmbits_stackpush(state, opfn(dbinleft, dbinright));
    return true;
}

static void nn_vmdebug_printvalue(NNState* state, NNValue val, const char* fmt, ...)
{
    va_list va;
    NNIOStream* pr;
    pr = state->stderrprinter;
    nn_iostream_printf(pr, "VMDEBUG: val=<<<");
    nn_iostream_printvalue(pr, val, true, false);
    nn_iostream_printf(pr, ">>> ");
    va_start(va, fmt);
    nn_iostream_vwritefmt(pr, fmt, va);
    va_end(va);
    nn_iostream_printf(pr, "\n");
}

#define NEON_CONFIG_USECOMPUTEDGOTO 0

/*
* something about using computed goto is currently breaking some scripts, specifically
* code generated for things like `somevar[idx]++`
* no issue with switch/case, though.
*/
#if 0
    #if defined(__GNUC__)
        #if defined(NEON_CONFIG_USECOMPUTEDGOTO)
            #undef NEON_CONFIG_USECOMPUTEDGOTO
        #endif
        #define NEON_CONFIG_USECOMPUTEDGOTO 1
        #if defined(__STRICT_ANSI__)
            #define NEON_SETDISPATCHIDX(idx, val) val
        #else
            #define NEON_SETDISPATCHIDX(idx, val) [idx] = val
        #endif
    #endif
#endif

#if defined(NEON_CONFIG_USECOMPUTEDGOTO) && (NEON_CONFIG_USECOMPUTEDGOTO == 1)
    #define VM_MAKELABEL(op) LABEL_##op
    #define VM_CASE(op) LABEL_##op:
    //#define VM_DISPATCH() goto readnextinstruction
    #define VM_DISPATCH() continue
    
#else
    #define VM_CASE(op) case op:
    #define VM_DISPATCH() break
#endif

NNStatus nn_vm_runvm(NNState* state, int exitframe, NNValue* rv)
{
    int iterpos;
    int printpos;
    int ofs;
    /*
    * this variable is a NOP; it only exists to ensure that functions outside of the
    * switch tree are not calling nn_vmmac_exitvm(), as its behavior could be undefined.
    */
    bool you_are_calling_exit_vm_outside_of_runvm;
    #if defined(NEON_CONFIG_USECOMPUTEDGOTO) && (NEON_CONFIG_USECOMPUTEDGOTO == 1)
        void* computedaddr;
    #endif
    NNValue* dbgslot;
    NNInstruction currinstr;
    #if defined(NEON_CONFIG_USECOMPUTEDGOTO) && (NEON_CONFIG_USECOMPUTEDGOTO == 1)
        static void* dispatchtable[] =
        {

            NEON_SETDISPATCHIDX(NEON_OP_GLOBALDEFINE, &&VM_MAKELABEL(NEON_OP_GLOBALDEFINE)),
            NEON_SETDISPATCHIDX(NEON_OP_GLOBALGET, &&VM_MAKELABEL(NEON_OP_GLOBALGET)),
            NEON_SETDISPATCHIDX(NEON_OP_GLOBALSET, &&VM_MAKELABEL(NEON_OP_GLOBALSET)),
            NEON_SETDISPATCHIDX(NEON_OP_LOCALGET, &&VM_MAKELABEL(NEON_OP_LOCALGET)),
            NEON_SETDISPATCHIDX(NEON_OP_LOCALSET, &&VM_MAKELABEL(NEON_OP_LOCALSET)),
            NEON_SETDISPATCHIDX(NEON_OP_FUNCARGOPTIONAL, &&VM_MAKELABEL(NEON_OP_FUNCARGOPTIONAL)),
            NEON_SETDISPATCHIDX(NEON_OP_FUNCARGSET, &&VM_MAKELABEL(NEON_OP_FUNCARGSET)),
            NEON_SETDISPATCHIDX(NEON_OP_FUNCARGGET, &&VM_MAKELABEL(NEON_OP_FUNCARGGET)),
            NEON_SETDISPATCHIDX(NEON_OP_UPVALUEGET, &&VM_MAKELABEL(NEON_OP_UPVALUEGET)),
            NEON_SETDISPATCHIDX(NEON_OP_UPVALUESET, &&VM_MAKELABEL(NEON_OP_UPVALUESET)),
            NEON_SETDISPATCHIDX(NEON_OP_UPVALUECLOSE, &&VM_MAKELABEL(NEON_OP_UPVALUECLOSE)),
            NEON_SETDISPATCHIDX(NEON_OP_PROPERTYGET, &&VM_MAKELABEL(NEON_OP_PROPERTYGET)),
            NEON_SETDISPATCHIDX(NEON_OP_PROPERTYGETSELF, &&VM_MAKELABEL(NEON_OP_PROPERTYGETSELF)),
            NEON_SETDISPATCHIDX(NEON_OP_PROPERTYSET, &&VM_MAKELABEL(NEON_OP_PROPERTYSET)),
            NEON_SETDISPATCHIDX(NEON_OP_JUMPIFFALSE, &&VM_MAKELABEL(NEON_OP_JUMPIFFALSE)),
            NEON_SETDISPATCHIDX(NEON_OP_JUMPNOW, &&VM_MAKELABEL(NEON_OP_JUMPNOW)),
            NEON_SETDISPATCHIDX(NEON_OP_LOOP, &&VM_MAKELABEL(NEON_OP_LOOP)),
            NEON_SETDISPATCHIDX(NEON_OP_EQUAL, &&VM_MAKELABEL(NEON_OP_EQUAL)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMGREATER, &&VM_MAKELABEL(NEON_OP_PRIMGREATER)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMLESSTHAN, &&VM_MAKELABEL(NEON_OP_PRIMLESSTHAN)),
            NEON_SETDISPATCHIDX(NEON_OP_PUSHEMPTY, &&VM_MAKELABEL(NEON_OP_PUSHEMPTY)),
            NEON_SETDISPATCHIDX(NEON_OP_PUSHNULL, &&VM_MAKELABEL(NEON_OP_PUSHNULL)),
            NEON_SETDISPATCHIDX(NEON_OP_PUSHTRUE, &&VM_MAKELABEL(NEON_OP_PUSHTRUE)),
            NEON_SETDISPATCHIDX(NEON_OP_PUSHFALSE, &&VM_MAKELABEL(NEON_OP_PUSHFALSE)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMADD, &&VM_MAKELABEL(NEON_OP_PRIMADD)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMSUBTRACT, &&VM_MAKELABEL(NEON_OP_PRIMSUBTRACT)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMMULTIPLY, &&VM_MAKELABEL(NEON_OP_PRIMMULTIPLY)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMDIVIDE, &&VM_MAKELABEL(NEON_OP_PRIMDIVIDE)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMFLOORDIVIDE, &&VM_MAKELABEL(NEON_OP_PRIMFLOORDIVIDE)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMMODULO, &&VM_MAKELABEL(NEON_OP_PRIMMODULO)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMPOW, &&VM_MAKELABEL(NEON_OP_PRIMPOW)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMNEGATE, &&VM_MAKELABEL(NEON_OP_PRIMNEGATE)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMNOT, &&VM_MAKELABEL(NEON_OP_PRIMNOT)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMBITNOT, &&VM_MAKELABEL(NEON_OP_PRIMBITNOT)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMAND, &&VM_MAKELABEL(NEON_OP_PRIMAND)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMOR, &&VM_MAKELABEL(NEON_OP_PRIMOR)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMBITXOR, &&VM_MAKELABEL(NEON_OP_PRIMBITXOR)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMSHIFTLEFT, &&VM_MAKELABEL(NEON_OP_PRIMSHIFTLEFT)),
            NEON_SETDISPATCHIDX(NEON_OP_PRIMSHIFTRIGHT, &&VM_MAKELABEL(NEON_OP_PRIMSHIFTRIGHT)),
            NEON_SETDISPATCHIDX(NEON_OP_PUSHONE, &&VM_MAKELABEL(NEON_OP_PUSHONE)),
            NEON_SETDISPATCHIDX(NEON_OP_PUSHCONSTANT, &&VM_MAKELABEL(NEON_OP_PUSHCONSTANT)),
            NEON_SETDISPATCHIDX(NEON_OP_ECHO, &&VM_MAKELABEL(NEON_OP_ECHO)),
            NEON_SETDISPATCHIDX(NEON_OP_POPONE, &&VM_MAKELABEL(NEON_OP_POPONE)),
            NEON_SETDISPATCHIDX(NEON_OP_DUPONE, &&VM_MAKELABEL(NEON_OP_DUPONE)),
            NEON_SETDISPATCHIDX(NEON_OP_POPN, &&VM_MAKELABEL(NEON_OP_POPN)),
            NEON_SETDISPATCHIDX(NEON_OP_ASSERT, &&VM_MAKELABEL(NEON_OP_ASSERT)),
            NEON_SETDISPATCHIDX(NEON_OP_EXTHROW, &&VM_MAKELABEL(NEON_OP_EXTHROW)),
            NEON_SETDISPATCHIDX(NEON_OP_MAKECLOSURE, &&VM_MAKELABEL(NEON_OP_MAKECLOSURE)),
            NEON_SETDISPATCHIDX(NEON_OP_CALLFUNCTION, &&VM_MAKELABEL(NEON_OP_CALLFUNCTION)),
            NEON_SETDISPATCHIDX(NEON_OP_CALLMETHOD, &&VM_MAKELABEL(NEON_OP_CALLMETHOD)),
            NEON_SETDISPATCHIDX(NEON_OP_CLASSINVOKETHIS, &&VM_MAKELABEL(NEON_OP_CLASSINVOKETHIS)),
            NEON_SETDISPATCHIDX(NEON_OP_RETURN, &&VM_MAKELABEL(NEON_OP_RETURN)),
            NEON_SETDISPATCHIDX(NEON_OP_MAKECLASS, &&VM_MAKELABEL(NEON_OP_MAKECLASS)),
            NEON_SETDISPATCHIDX(NEON_OP_MAKEMETHOD, &&VM_MAKELABEL(NEON_OP_MAKEMETHOD)),
            NEON_SETDISPATCHIDX(NEON_OP_CLASSGETTHIS, &&VM_MAKELABEL(NEON_OP_CLASSGETTHIS)),
            NEON_SETDISPATCHIDX(NEON_OP_CLASSPROPERTYDEFINE, &&VM_MAKELABEL(NEON_OP_CLASSPROPERTYDEFINE)),
            NEON_SETDISPATCHIDX(NEON_OP_CLASSINHERIT, &&VM_MAKELABEL(NEON_OP_CLASSINHERIT)),
            NEON_SETDISPATCHIDX(NEON_OP_CLASSGETSUPER, &&VM_MAKELABEL(NEON_OP_CLASSGETSUPER)),
            NEON_SETDISPATCHIDX(NEON_OP_CLASSINVOKESUPER, &&VM_MAKELABEL(NEON_OP_CLASSINVOKESUPER)),
            NEON_SETDISPATCHIDX(NEON_OP_CLASSINVOKESUPERSELF, &&VM_MAKELABEL(NEON_OP_CLASSINVOKESUPERSELF)),
            NEON_SETDISPATCHIDX(NEON_OP_MAKERANGE, &&VM_MAKELABEL(NEON_OP_MAKERANGE)),
            NEON_SETDISPATCHIDX(NEON_OP_MAKEARRAY, &&VM_MAKELABEL(NEON_OP_MAKEARRAY)),
            NEON_SETDISPATCHIDX(NEON_OP_MAKEDICT, &&VM_MAKELABEL(NEON_OP_MAKEDICT)),
            NEON_SETDISPATCHIDX(NEON_OP_INDEXGET, &&VM_MAKELABEL(NEON_OP_INDEXGET)),
            NEON_SETDISPATCHIDX(NEON_OP_INDEXGETRANGED, &&VM_MAKELABEL(NEON_OP_INDEXGETRANGED)),
            NEON_SETDISPATCHIDX(NEON_OP_INDEXSET, &&VM_MAKELABEL(NEON_OP_INDEXSET)),
            NEON_SETDISPATCHIDX(NEON_OP_IMPORTIMPORT, &&VM_MAKELABEL(NEON_OP_IMPORTIMPORT)),
            NEON_SETDISPATCHIDX(NEON_OP_EXTRY, &&VM_MAKELABEL(NEON_OP_EXTRY)),
            NEON_SETDISPATCHIDX(NEON_OP_EXPOPTRY, &&VM_MAKELABEL(NEON_OP_EXPOPTRY)),
            NEON_SETDISPATCHIDX(NEON_OP_EXPUBLISHTRY, &&VM_MAKELABEL(NEON_OP_EXPUBLISHTRY)),
            NEON_SETDISPATCHIDX(NEON_OP_STRINGIFY, &&VM_MAKELABEL(NEON_OP_STRINGIFY)),
            NEON_SETDISPATCHIDX(NEON_OP_SWITCH, &&VM_MAKELABEL(NEON_OP_SWITCH)),
            NEON_SETDISPATCHIDX(NEON_OP_TYPEOF, &&VM_MAKELABEL(NEON_OP_TYPEOF)),
            NEON_SETDISPATCHIDX(NEON_OP_OPINSTANCEOF, &&VM_MAKELABEL(NEON_OP_OPINSTANCEOF)),
            NEON_SETDISPATCHIDX(NEON_OP_HALT, &&VM_MAKELABEL(NEON_OP_HALT)),


        };
    #endif
    you_are_calling_exit_vm_outside_of_runvm = false;
    GCSingleton::get()->vmstate.currentframe = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount - 1];

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
        if(GCSingleton::get()->vmstate.framecount == 0)
        {
            return NEON_STATUS_FAILRUNTIME;
        }
        if(nn_util_unlikely(state->conf.shoulddumpstack))
        {
            ofs = (int)(GCSingleton::get()->vmstate.currentframe->inscode - GCSingleton::get()->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.blob.instrucs);
            nn_dbg_printinstructionat(state->debugwriter, &GCSingleton::get()->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.blob, ofs);
            fprintf(stderr, "stack (before)=[\n");
            iterpos = 0;
            dbgslot = GCSingleton::get()->vmstate.stackvalues;
            while(dbgslot < &GCSingleton::get()->vmstate.stackvalues[GCSingleton::get()->vmstate.stackidx])
            {
                printpos = iterpos + 1;
                iterpos++;
                fprintf(stderr, "  [%s%d%s] ", nn_util_color(NEON_COLOR_YELLOW), printpos, nn_util_color(NEON_COLOR_RESET));
                nn_iostream_printf(state->debugwriter, "%s", nn_util_color(NEON_COLOR_YELLOW));
                nn_iostream_printvalue(state->debugwriter, *dbgslot, true, false);
                nn_iostream_printf(state->debugwriter, "%s", nn_util_color(NEON_COLOR_RESET));
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
        currinstr = nn_vmbits_readinstruction(state);
        GCSingleton::get()->vmstate.currentinstr = currinstr;
        #if defined(NEON_CONFIG_USECOMPUTEDGOTO) && (NEON_CONFIG_USECOMPUTEDGOTO == 1)
            computedaddr = dispatchtable[currinstr.code];
            /* TODO: figure out why this happens (failing instruction is 255) */
            if(nn_util_unlikely(computedaddr == nullptr))
            {
                #if 0
                    goto trynext;
                #else
                    fprintf(stderr, "computedaddr is nullptr!!\n");
                    return NEON_STATUS_FAILRUNTIME;
                #endif
            }
            goto* computedaddr;
        #else
            switch(currinstr.code)
        #endif
        {
            VM_CASE(NEON_OP_RETURN)
                {
                    size_t ssp;
                    NNValue result;
                    result = nn_vmbits_stackpop(state);
                    if(rv != nullptr)
                    {
                        *rv = result;
                    }
                    ssp = GCSingleton::get()->vmstate.currentframe->stackslotpos;
                    nn_vmutil_upvaluesclose(state, &GCSingleton::get()->vmstate.stackvalues[ssp]);
                    GCSingleton::get()->vmstate.framecount--;
                    if(GCSingleton::get()->vmstate.framecount == 0)
                    {
                        nn_vmbits_stackpop(state);
                        return NEON_STATUS_OK;
                    }
                    ssp = GCSingleton::get()->vmstate.currentframe->stackslotpos;
                    GCSingleton::get()->vmstate.stackidx = ssp;
                    nn_vmbits_stackpush(state, result);
                    GCSingleton::get()->vmstate.currentframe = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount - 1];
                    if(GCSingleton::get()->vmstate.framecount == (int64_t)exitframe)
                    {
                        return NEON_STATUS_OK;
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_HALT)
                {
                    printf("**halting vm**\n");
                }
                goto finished;
            VM_CASE(NEON_OP_PUSHCONSTANT)
                {
                    NNValue constant;
                    constant = nn_vmbits_readconst(state);
                    nn_vmbits_stackpush(state, constant);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMADD)
                {
                    NNValue valright;
                    NNValue valleft;
                    NNValue result;
                    valright = nn_vmbits_stackpeek(state, 0);
                    valleft = nn_vmbits_stackpeek(state, 1);
                    if(nn_value_isstring(valright) || nn_value_isstring(valleft))
                    {
                        if(nn_util_unlikely(!nn_vmutil_concatenate(state)))
                        {
                            nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "unsupported operand + for %s and %s", nn_value_typename(valleft, false), nn_value_typename(valright, false));
                            VM_DISPATCH();
                        }
                    }
                    else if(nn_value_isarray(valleft) && nn_value_isarray(valright))
                    {
                        result = nn_value_fromobject(nn_vmutil_combinearrays(state, nn_value_asarray(valleft), nn_value_asarray(valright)));
                        nn_vmbits_stackpopn(state, 2);
                        nn_vmbits_stackpush(state, result);
                    }
                    else
                    {
                        nn_vmdo_dobinarydirect(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMSUBTRACT)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMMULTIPLY)
                {
                    int intnum;
                    double dbnum;
                    NNValue peekleft;
                    NNValue peekright;
                    NNValue result;
                    NNObjString* string;
                    NNObjArray* list;
                    NNObjArray* newlist;
                    peekright = nn_vmbits_stackpeek(state, 0);
                    peekleft = nn_vmbits_stackpeek(state, 1);
                    if(nn_value_isstring(peekleft) && nn_value_isnumber(peekright))
                    {
                        dbnum = nn_value_asnumber(peekright);
                        string = nn_value_asstring(peekleft);
                        result = nn_value_fromobject(nn_vmutil_multiplystring(state, string, dbnum));
                        nn_vmbits_stackpopn(state, 2);
                        nn_vmbits_stackpush(state, result);
                        VM_DISPATCH();
                    }
                    else if(nn_value_isarray(peekleft) && nn_value_isnumber(peekright))
                    {
                        intnum = (int)nn_value_asnumber(peekright);
                        nn_vmbits_stackpop(state);
                        list = nn_value_asarray(peekleft);
                        newlist = nn_object_makearray();
                        nn_vmbits_stackpush(state, nn_value_fromobject(newlist));
                        nn_vmutil_multiplyarray(list, newlist, intnum);
                        nn_vmbits_stackpopn(state, 2);
                        nn_vmbits_stackpush(state, nn_value_fromobject(newlist));
                        VM_DISPATCH();
                    }
                    else
                    {
                        nn_vmdo_dobinarydirect(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMDIVIDE)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMMODULO)
                {
                    if(nn_vmdo_dobinaryfunc(state, "%", (nnbinopfunc_t)nn_vmutil_modulo))
                    {
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMPOW)
                {
                    if(nn_vmdo_dobinaryfunc(state, "**", (nnbinopfunc_t)nn_vmutil_pow))
                    {
                    }
                }
                VM_DISPATCH();
            #if 0
            VM_CASE(NEON_OP_PRIMFLOORDIVIDE)
                {
                    if(nn_vmdo_dobinaryfunc(state, "//", (nnbinopfunc_t)nn_vmutil_floordiv))
                    {
                    }
                }
                VM_DISPATCH();
            #endif
            VM_CASE(NEON_OP_PRIMNEGATE)
                {
                    NNValue peeked;
                    peeked = nn_vmbits_stackpeek(state, 0);
                    if(!nn_value_isnumber(peeked))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "operator - not defined for object of type %s", nn_value_typename(peeked, false));
                        VM_DISPATCH();
                    }
                    peeked = nn_vmbits_stackpop(state);
                    nn_vmbits_stackpush(state, nn_value_makenumber(-nn_value_asnumber(peeked)));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMBITNOT)
            {
                NNValue peeked;
                peeked = nn_vmbits_stackpeek(state, 0);
                if(!nn_value_isnumber(peeked))
                {
                    nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "operator ~ not defined for object of type %s", nn_value_typename(peeked, false));
                    VM_DISPATCH();
                }
                peeked = nn_vmbits_stackpop(state);
                nn_vmbits_stackpush(state, nn_value_makenumber(~((int)nn_value_asnumber(peeked))));
                VM_DISPATCH();
            }
            VM_CASE(NEON_OP_PRIMAND)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMOR)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMBITXOR)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMSHIFTLEFT)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMSHIFTRIGHT)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PUSHONE)
                {
                    nn_vmbits_stackpush(state, nn_value_makenumber(1));
                }
                VM_DISPATCH();
            /* comparisons */
            VM_CASE(NEON_OP_EQUAL)
                {
                    NNValue a;
                    NNValue b;
                    b = nn_vmbits_stackpop(state);
                    a = nn_vmbits_stackpop(state);
                    nn_vmbits_stackpush(state, nn_value_makebool(nn_value_compare(a, b)));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMGREATER)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMLESSTHAN)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMNOT)
                {
                    NNValue val;
                    val = nn_vmbits_stackpop(state);
                    nn_vmbits_stackpush(state, nn_value_makebool(nn_value_isfalse(val)));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PUSHNULL)
                {
                    nn_vmbits_stackpush(state, nn_value_makenull());
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PUSHEMPTY)
                {
                    nn_vmbits_stackpush(state, nn_value_makenull());
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PUSHTRUE)
                {
                    nn_vmbits_stackpush(state, nn_value_makebool(true));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PUSHFALSE)
                {
                    nn_vmbits_stackpush(state, nn_value_makebool(false));
                }
                VM_DISPATCH();

            VM_CASE(NEON_OP_JUMPNOW)
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    GCSingleton::get()->vmstate.currentframe->inscode += offset;
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_JUMPIFFALSE)
                {
                    uint16_t offset;
                    NNValue val;
                    offset = nn_vmbits_readshort(state);
                    val = nn_vmbits_stackpeek(state, 0);
                    if(nn_value_isfalse(val))
                    {
                        GCSingleton::get()->vmstate.currentframe->inscode += offset;
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_LOOP)
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    GCSingleton::get()->vmstate.currentframe->inscode -= offset;
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_ECHO)
                {
                    NNValue val;
                    val = nn_vmbits_stackpeek(state, 0);
                    nn_iostream_printvalue(state->stdoutprinter, val, state->isrepl, true);
                    if(!val.isNull())
                    {
                        nn_iostream_writestring(state->stdoutprinter, "\n");
                    }
                    nn_vmbits_stackpop(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_STRINGIFY)
                {
                    NNValue peeked;
                    NNValue popped;
                    NNObjString* os;
                    peeked = nn_vmbits_stackpeek(state, 0);
                    if(!nn_value_isstring(peeked) && !peeked.isNull())
                    {
                        popped = nn_vmbits_stackpop(state);
                        os = nn_value_tostring(popped);
                        if(nn_string_getlength(os) != 0)
                        {
                            nn_vmbits_stackpush(state, nn_value_fromobject(os));
                        }
                        else
                        {
                            nn_vmbits_stackpush(state, nn_value_makenull());
                        }
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_DUPONE)
                {
                    NNValue val;
                    val = nn_vmbits_stackpeek(state, 0);
                    nn_vmbits_stackpush(state, val);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_POPONE)
                {
                    nn_vmbits_stackpop(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_POPN)
                {
                    nn_vmbits_stackpopn(state, nn_vmbits_readshort(state));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_UPVALUECLOSE)
                {
                    nn_vmutil_upvaluesclose(state, &GCSingleton::get()->vmstate.stackvalues[GCSingleton::get()->vmstate.stackidx - 1]);
                    nn_vmbits_stackpop(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_OPINSTANCEOF)
                {
                    bool rt;
                    NNValue first;
                    NNValue second;
                    NNObjClass* vclass;
                    NNObjClass* checkclass;
                    rt = false;
                    first = nn_vmbits_stackpop(state);
                    second = nn_vmbits_stackpop(state);
                    #if 0
                    nn_vmdebug_printvalue(state, first, "first value");
                    nn_vmdebug_printvalue(state, second, "second value");
                    #endif
                    if(!nn_value_isclass(first))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "invalid use of 'is' on non-class");
                    }
                    checkclass = nn_value_asclass(first);
                    vclass = nn_value_getclassfor(state, second);
                    if(vclass )
                    {
                        rt = nn_util_isinstanceof(vclass, checkclass);
                    }
                    nn_vmbits_stackpush(state, nn_value_makebool(rt));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_GLOBALDEFINE)
                {
                    if(!nn_vmdo_globaldefine(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_GLOBALGET)
                {
                    if(!nn_vmdo_globalget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_GLOBALSET)
                {
                    if(!nn_vmdo_globalset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_LOCALGET)
                {
                    if(!nn_vmdo_localget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_LOCALSET)
                {
                    if(!nn_vmdo_localset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_FUNCARGGET)
                {
                    if(!nn_vmdo_funcargget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_FUNCARGOPTIONAL)
                {
                    if(!nn_vmdo_funcargoptional(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_FUNCARGSET)
                {
                    if(!nn_vmdo_funcargset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();

            VM_CASE(NEON_OP_PROPERTYGET)
                {
                    if(!nn_vmdo_propertyget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PROPERTYSET)
                {
                    if(!nn_vmdo_propertyset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PROPERTYGETSELF)
                {
                    if(!nn_vmdo_propertygetself(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_MAKECLOSURE)
                {
                    if(!nn_vmdo_makeclosure(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_UPVALUEGET)
                {
                    int upvidx;
                    NNObjFunction* closure;
                    upvidx = nn_vmbits_readshort(state);
                    closure = GCSingleton::get()->vmstate.currentframe->closure;
                    if(upvidx < closure->upvalcount)
                    {
                        nn_vmbits_stackpush(state, (NNValue)(closure->fnclosure.upvalues[upvidx]->location));
                    }
                    else
                    {
                        nn_vmbits_stackpush(state, (NNValue)closure->clsthisval);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_UPVALUESET)
                {
                    int upvidx;
                    NNValue val;
                    upvidx = nn_vmbits_readshort(state);
                    val = nn_vmbits_stackpeek(state, 0);
                    GCSingleton::get()->vmstate.currentframe->closure->fnclosure.upvalues[upvidx]->location = val;
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CALLFUNCTION)
                {
                    int argcount;
                    NNValue callee;
                    NNValue thisval;
                    thisval = nn_value_makenull();
                    argcount = nn_vmbits_readbyte(state);
                    callee = nn_vmbits_stackpeek(state, argcount);
                    if(nn_value_isfuncclosure(callee))
                    {
                        thisval = (NNValue)(nn_value_asfunction(callee)->clsthisval);
                    }
                    if(!nn_vm_callvalue(state, callee, thisval, argcount, false))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    GCSingleton::get()->vmstate.currentframe = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount - 1];
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CALLMETHOD)
                {
                    int argcount;
                    NNObjString* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vmutil_invokemethodnormal(state, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    GCSingleton::get()->vmstate.currentframe = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount - 1];
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSGETTHIS)
                {
                    NNValue thisval;
                    thisval = nn_vmbits_stackpeek(state, 3);
                    nn_iostream_printf(state->debugwriter, "CLASSGETTHIS: thisval=");
                    nn_iostream_printvalue(state->debugwriter, thisval, true, false);
                    nn_iostream_printf(state->debugwriter, "\n");
                    nn_vmbits_stackpush(state, thisval);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSINVOKETHIS)
                {
                    int argcount;
                    NNObjString* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vmutil_invokemethodself(state, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    GCSingleton::get()->vmstate.currentframe = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount - 1];
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_MAKECLASS)
                {
                    bool haveval;
                    NNValue pushme;
                    NNObjString* name;
                    NNObjClass* klass;
                    NNProperty* field;
                    haveval = false;
                    name = nn_vmbits_readstring(state);
                    field = GCSingleton::get()->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module->deftable.getfieldbyostr(name);
                    if(field != nullptr)
                    {
                        if(nn_value_isclass((NNValue)field->value))
                        {
                            haveval = true;
                            pushme = (NNValue)field->value;
                        }
                    }
                    field = GCSingleton::get()->declaredglobals.getfieldbyostr(name);
                    if(field != nullptr)
                    {
                        if(nn_value_isclass((NNValue)field->value))
                        {
                            haveval = true;
                            pushme = (NNValue)field->value;
                        }
                    }
                    if(!haveval)
                    {
                        klass = nn_object_makeclass(name, state->classprimobject);
                        pushme = nn_value_fromobject(klass);
                    }
                    nn_vmbits_stackpush(state, pushme);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_MAKEMETHOD)
                {
                    NNObjString* name;
                    name = nn_vmbits_readstring(state);
                    nn_vmutil_definemethod(state, name);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSPROPERTYDEFINE)
                {
                    int isstatic;
                    NNObjString* name;
                    name = nn_vmbits_readstring(state);
                    isstatic = nn_vmbits_readbyte(state);
                    nn_vmutil_defineproperty(state, name, isstatic == 1);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSINHERIT)
                {
                    NNValue vclass;
                    NNValue vsuper;
                    NNObjClass* superclass;
                    NNObjClass* subclass;
                    vsuper = nn_vmbits_stackpeek(state, 1);
                    if(!nn_value_isclass(vsuper))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "cannot inherit from non-class object");
                        VM_DISPATCH();
                    }
                    vclass = nn_vmbits_stackpeek(state, 0);
                    superclass = nn_value_asclass(vsuper);
                    subclass = nn_value_asclass(vclass);
                    nn_class_inheritfrom(subclass, superclass);
                    /* pop the subclass */
                    nn_vmbits_stackpop(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSGETSUPER)
                {
                    NNValue vclass;
                    NNObjClass* klass;
                    NNObjString* name;
                    name = nn_vmbits_readstring(state);
                    vclass = nn_vmbits_stackpeek(state, 0);
                    klass = nn_value_asclass(vclass);
                    if(!nn_vmutil_bindmethod(state, klass->superclass, name))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "class '%s' does not have a function '%s'", nn_string_getdata(klass->name), nn_string_getdata(name));
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSINVOKESUPER)
                {
                    int argcount;
                    NNValue vclass;
                    NNObjClass* klass;
                    NNObjString* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    vclass = nn_vmbits_stackpop(state);
                    klass = nn_value_asclass(vclass);
                    if(!nn_vmutil_invokemethodfromclass(state, klass, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    GCSingleton::get()->vmstate.currentframe = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount - 1];
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSINVOKESUPERSELF)
                {
                    int argcount;
                    NNValue vclass;
                    NNObjClass* klass;
                    argcount = nn_vmbits_readbyte(state);
                    vclass = nn_vmbits_stackpop(state);
                    klass = nn_value_asclass(vclass);
                    if(!nn_vmutil_invokemethodfromclass(state, klass, state->defaultstrings.nmconstructor, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    GCSingleton::get()->vmstate.currentframe = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount - 1];
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_MAKEARRAY)
                {
                    if(!nn_vmdo_makearray(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();

            VM_CASE(NEON_OP_MAKERANGE)
                {
                    double lower;
                    double upper;
                    NNValue vupper;
                    NNValue vlower;
                    vupper = nn_vmbits_stackpeek(state, 0);
                    vlower = nn_vmbits_stackpeek(state, 1);
                    if(!nn_value_isnumber(vupper) || !nn_value_isnumber(vlower))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "invalid range boundaries");
                        VM_DISPATCH();
                    }
                    lower = nn_value_asnumber(vlower);
                    upper = nn_value_asnumber(vupper);
                    nn_vmbits_stackpopn(state, 2);
                    nn_vmbits_stackpush(state, nn_value_fromobject(nn_object_makerange(lower, upper)));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_MAKEDICT)
                {
                    if(!nn_vmdo_makedict(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_INDEXGETRANGED)
                {
                    if(!nn_vmdo_getrangedindex(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_INDEXGET)
                {
                    if(!nn_vmdo_indexget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_INDEXSET)
                {
                    if(!nn_vmdo_indexset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_IMPORTIMPORT)
                {
                    NNValue res;
                    NNValue vname;
                    NNObjString* name;
                    NNObjModule* mod;
                    vname = nn_vmbits_stackpeek(state, 0);
                    name = nn_value_asstring(vname);
                    fprintf(stderr, "IMPORTIMPORT: name='%s'\n", nn_string_getdata(name));
                    mod = nn_import_loadmodulescript(state, state->topmodule, name);
                    fprintf(stderr, "IMPORTIMPORT: mod='%p'\n", (void*)mod);
                    if(mod == nullptr)
                    {
                        res = nn_value_makenull();
                    }
                    else
                    {
                        res = nn_value_fromobject(mod);
                    }
                    nn_vmbits_stackpush(state, res);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_TYPEOF)
                {
                    NNValue res;
                    NNValue thing;
                    const char* result;
                    thing = nn_vmbits_stackpop(state);
                    result = nn_value_typename(thing, false);
                    res = nn_value_fromobject(nn_string_copycstr(result));
                    nn_vmbits_stackpush(state, res);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_ASSERT)
                {
                    NNValue message;
                    NNValue expression;
                    message = nn_vmbits_stackpop(state);
                    expression = nn_vmbits_stackpop(state);
                    if(nn_value_isfalse(expression))
                    {
                        if(!message.isNull())
                        {
                            nn_except_throwclass(state, state->exceptions.asserterror, nn_string_getdata(nn_value_tostring(message)));
                        }
                        else
                        {
                            nn_except_throwclass(state, state->exceptions.asserterror, "assertion failed");
                        }
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_EXTHROW)
                {
                    bool isok;
                    NNValue peeked;
                    NNValue stacktrace;
                    NNObjInstance* instance;
                    peeked = nn_vmbits_stackpeek(state, 0);
                    isok = (
                        nn_value_isinstance(peeked) ||
                        nn_util_isinstanceof(nn_value_asinstance(peeked)->klass, state->exceptions.stdexception)
                    );
                    if(!isok)
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "instance of Exception expected");
                        VM_DISPATCH();
                    }
                    stacktrace = nn_except_getstacktrace(state);
                    instance = nn_value_asinstance(peeked);
                    nn_instance_defproperty(instance, nn_string_intern("stacktrace"), stacktrace);
                    if(nn_except_propagate(state))
                    {
                        GCSingleton::get()->vmstate.currentframe = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount - 1];
                        VM_DISPATCH();
                    }
                    nn_vmmac_exitvm(state);
                }
            VM_CASE(NEON_OP_EXTRY)
                {
                    bool haveclass;
                    uint16_t addr;
                    uint16_t finaddr;
                    NNValue value;
                    NNObjString* type;
                    NNObjClass* exclass;
                    haveclass = false;
                    exclass = nullptr;
                    type = nn_vmbits_readstring(state);
                    addr = nn_vmbits_readshort(state);
                    finaddr = nn_vmbits_readshort(state);
                    if(addr != 0)
                    {
                        value = nn_value_makenull();
                        if(!GCSingleton::get()->declaredglobals.get(nn_value_fromobject(type), &value))
                        {
                            if(nn_value_isclass(value))
                            {
                                haveclass = true;
                                exclass = nn_value_asclass(value);
                            }
                        }
                        if(!haveclass)
                        {
                            /*
                            if(!GCSingleton::get()->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module->deftable.get(nn_value_fromobject(type), &value) || !nn_value_isclass(value))
                            {
                                nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "object of type '%s' is not an exception", nn_string_getdata(type));
                                VM_DISPATCH();
                            }
                            */
                            exclass = state->exceptions.stdexception;
                        }
                        nn_except_pushhandler(state, exclass, addr, finaddr);
                    }
                    else
                    {
                        nn_except_pushhandler(state, nullptr, addr, finaddr);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_EXPOPTRY)
                {
                    GCSingleton::get()->vmstate.currentframe->handlercount--;
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_EXPUBLISHTRY)
                {
                    GCSingleton::get()->vmstate.currentframe->handlercount--;
                    if(nn_except_propagate(state))
                    {
                        GCSingleton::get()->vmstate.currentframe = &GCSingleton::get()->vmstate.framevalues[GCSingleton::get()->vmstate.framecount - 1];
                        VM_DISPATCH();
                    }
                    nn_vmmac_exitvm(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_SWITCH)
                {
                    NNValue expr;
                    NNValue value;
                    NNObjSwitch* sw;
                    sw = nn_value_asswitch(nn_vmbits_readconst(state));
                    expr = nn_vmbits_stackpeek(state, 0);
                    if(sw->table.get(expr, &value))
                    {
                        GCSingleton::get()->vmstate.currentframe->inscode += (int)nn_value_asnumber(value);
                    }
                    else if(sw->defaultjump != -1)
                    {
                        GCSingleton::get()->vmstate.currentframe->inscode += sw->defaultjump;
                    }
                    else
                    {
                        GCSingleton::get()->vmstate.currentframe->inscode += sw->exitjump;
                    }
                    nn_vmbits_stackpop(state);
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
    ofn = nn_value_asfunction(callable);
    if(nn_value_isfuncclosure(callable))
    {
        arity = ofn->fnclosure.scriptfunc->fnscriptfunc.arity;
    }
    else if(nn_value_isfuncscript(callable))
    {
        arity = ofn->fnscriptfunc.arity;
    }
    else if(nn_value_isfuncnative(callable))
    {
        #if 0
            arity = ofn;
        #endif
    }
    if(arity > 0)
    {
        callarr[0] = nn_value_makenull();
        if(arity > 1)
        {
            callarr[1] = nn_value_makenull();
            if(arity > 2)
            {
                callarr[2] = mthobj;
            }
        }
    }
    return arity;
}

/* helper function to access call outside the state file. */
bool nn_nestcall_callfunction(NNState* state, NNValue callable, NNValue thisval, NNValue* argv, size_t argc, NNValue* dest, bool fromoper)
{
    bool needvm;
    size_t i;
    int64_t pidx;
    NNStatus status;
    NNValue rtv;
    pidx = GCSingleton::get()->vmstate.stackidx;
    /* set the closure before the args */
    nn_vm_stackpush(state, callable);
    if((argv != nullptr) && (argc > 0))
    {
        for(i = 0; i < argc; i++)
        {
            nn_vm_stackpush(state, argv[i]);
        }
    }
    if(!nn_vm_callvaluewithobject(state, callable, thisval, argc, fromoper))
    {
        fprintf(stderr, "nestcall: nn_vm_callvalue() failed\n");
        abort();
    }
    needvm = true;
    if(nn_value_isfuncnative(callable))
    {
        needvm = false;
    }
    if(needvm)
    {
        status = nn_vm_runvm(state, GCSingleton::get()->vmstate.framecount - 1, nullptr);
        if(status != NEON_STATUS_OK)
        {
            fprintf(stderr, "nestcall: call to runvm failed\n");
            abort();
        }
    }
    rtv = GCSingleton::get()->vmstate.stackvalues[GCSingleton::get()->vmstate.stackidx - 1];
    *dest = rtv;
    nn_vm_stackpopn(state, argc + 0);
    GCSingleton::get()->vmstate.stackidx = pidx;
    return true;
}




#define STRINGIFY_(thing) #thing
#define STRINGIFY(thing) STRINGIFY_(thing)

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
static bool nn_cli_repl(linocontext_t* lictx, NNState* state)
{
    enum { kMaxVarName = 512 };
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
    pr = state->stdoutprinter;
    rescnt = 0;
    state->isrepl = true;
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
            nn_state_execsource(state, state->topmodule, nn_strbuf_data(source), "<repl>", &dest);
            dest = state->lastreplvalue;
            if(!dest.isNull())
            {
                nn_iostream_printf(pr, "%s = ", varnamebuf);
                nn_iostream_printvalue(pr, dest, true, true);
                nn_state_defglobalvalue(state, varnamebuf, dest);
                nn_iostream_printf(pr, "\n");
                rescnt++;
            }
            state->lastreplvalue = nn_value_makenull();
            fflush(stdout);
            continuerepl = false;
        }
    }
    return true;
}
#endif

static bool nn_cli_runfile(NNState* state, const char* file)
{
    size_t fsz;
    char* source;
    const char* oldfile;
    NNStatus result;
    source = nn_util_filereadfile(state, file, &fsz, false, 0);
    if(source == nullptr)
    {
        oldfile = file;
        source = nn_util_filereadfile(state, file, &fsz, false, 0);
        if(source == nullptr)
        {
            fprintf(stderr, "failed to read from '%s': %s\n", oldfile, strerror(errno));
            return false;
        }
    }
    result = nn_state_execsource(state, state->topmodule, source, file, nullptr);
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

static bool nn_cli_runcode(NNState* state, char* source)
{
    NNStatus result;
    state->rootphysfile = nullptr;
    result = nn_state_execsource(state, state->topmodule, source, "<-e>", nullptr);
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
    return a*b;
}
#endif

static int nn_util_findfirstpos(const char* str, size_t len, int ch)
{
    size_t i;
    for(i=0; i<len; i++)
    {
        if(str[i] == ch)
        {
            return i;
        }
    }
    return -1;
}

static void nn_cli_parseenv(NNState* state, char** envp)
{
    enum { kMaxKeyLen = 40 };
    size_t i;
    int len;
    int pos;
    char* raw;
    char* valbuf;
    char keybuf[kMaxKeyLen];
    NNObjString* oskey;
    NNObjString* osval;
    if(envp == nullptr)
    {
        return;
    }
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
            oskey = nn_string_copycstr(keybuf);
            osval = nn_string_copycstr(valbuf);
            nn_dict_setentry(state->envdict, nn_value_fromobject(oskey), nn_value_fromobject(osval));
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
    char *arg;
    char* source;
    const char* filename;
    char* nargv[128];
    optcontext_t options;
    NNState* state;
    NNObjString* os;
    linocontext_t lictx;
    static optlongflags_t longopts[] =
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
        {"gcstart", 'g', OPTPARSE_REQUIRED, "set minimum bytes at which the GC should kick in. 0 disables GC (default is " STRINGIFY(NEON_CONFIG_DEFAULTGCSTART) ")"},
        {0, 0, (optargtype_t)0, nullptr}
    };
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
    state = nn_state_makealloc();
    if(state == nullptr)
    {
        fprintf(stderr, "failed to create state\n");
        return 0;
    }
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
        else if(co == 'h')
        {
            nn_cli_showusage(argv, longopts, false);
            wasusage = true;
        }
        else if(co == 'd' || co == 'j')
        {
            state->conf.dumpbytecode = true;
            state->conf.shoulddumpstack = true;        
        }
        else if(co == 'x')
        {
            state->conf.exitafterbytecode = true;
        }
        else if(co == 'a')
        {
            state->conf.enableapidebug = true;
        }
        else if(co == 's')
        {
            state->conf.enablestrictmode = true;            
        }
        else if(co == 'e')
        {
            source = options.optarg;
        }
        else if(co == 'w')
        {
            state->conf.enablewarnings = true;
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
        nargv[nargc] = arg;
        nargc++;
    }
    {
        for(i=0; i<nargc; i++)
        {
            os = nn_string_copycstr(nargv[i]);
            nn_array_push(state->processinfo->cliargv, nn_value_fromobject(os));

        }
        GCSingleton::get()->declaredglobals.set(nn_value_copystr("ARGV"), nn_value_fromobject(state->processinfo->cliargv));
    }
    GCSingleton::get()->gcstate.nextgc = nextgcstart;
    nn_import_loadbuiltinmodules(state);
    if(source != nullptr)
    {
        ok = nn_cli_runcode(state, source);
    }
    else if(nargc > 0)
    {
        os = nn_value_asstring(state->processinfo->cliargv->varray.get(0));
        filename = nn_string_getdata(os);
        ok = nn_cli_runfile(state, filename);
    }
    else
    {
        ok = nn_cli_repl(&lictx, state);
    }
    cleanup:
    nn_state_destroy(state, false);
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
    char* realargv[1024] = {(char*)"a.out", (char*)deffile, nullptr};
    return main(1, realargv, nullptr);
}

