
#if !defined(__LIBNEONMAINHEADERFILE_H__)
#define __LIBNEONMAINHEADERFILE_H__

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)
    #define NEON_PLAT_ISWINDOWS
#else
    #if defined(__wasi__)
        #define NEON_PLAT_ISWASM
    #endif
    #define NEON_PLAT_ISLINUX
#endif

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

#if defined(NEON_PLAT_ISWINDOWS)
    #include <sys/utime.h>
#else
    #include <sys/time.h>
    #include <unistd.h>
#endif

/* needed when compiling with wasi. must be defined *before* signal.h is included! */
#if defined(__wasi__)
    #define _WASI_EMULATED_SIGNAL
#endif

#if defined(NEON_PLAT_ISWINDOWS)
    #include <fcntl.h>
    #include <io.h>
#endif

/**
* if enabled, uses NaN tagging for values.
*/
#define NEON_CONFIG_USENANTAGGING 1

/**
* if enabled, most API calls will check for null pointers, and either
* return immediately or return an appropiate default value if a nullpointer is encountered.
* this will make the API much less likely to crash out in a segmentation fault,
* **BUT** the added branching will likely reduce performance.
*/
#define NEON_CONFIG_USENULLPTRCHECKS 0

#if defined(__STRICT_ANSI__)
    #define va_copy(...)
    #define NEON_INLINE static
    #define NEON_FORCEINLINE static
    #define inline
    #define __FUNCTION__ "<here>"
#else
    #define NEON_INLINE static inline
    #if defined(__GNUC__)
        #define NEON_FORCEINLINE static __attribute__((always_inline)) inline
    #else
        #define NEON_FORCEINLINE static inline
    #endif
#endif

#if !defined(NEON_FORCEINLINE)
    #define NEON_FORCEINLINE static
#endif
#if !defined(NEON_INLINE)
    #define NEON_INLINE static
#endif

#if 0
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        #if defined(_MSC_VER)
            #pragma message("*** USING NAN TAGGING ***")
        #else
            #warning "*** USING NAN TAGGING ***"
        #endif
    #endif
#endif

#if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
    #define NEON_NANBOX_SIGNBIT     ((uint64_t) 0x8000000000000000)
    #define NEON_NANBOX_QNAN        ((uint64_t) 0x7ffc000000000000)

    #define NEON_NANBOX_TAGNULL    (1ull<<47) /* 001 */
    #define NEON_NANBOX_TAGBOOL    (2ull<<47) /* 010 */
    #define NEON_NANBOX_TAGINT     (3ull<<47) /* 011 */
    #define NEON_NANBOX_TAGOBJ     (NEON_NANBOX_SIGNBIT)

    #define NEON_NANBOX_TAGVALTRUE    1
    #define NEON_NANBOX_TAGVALFALSE   0

    #define NEON_NANBOX_TYPEBITS (NEON_NANBOX_TAGOBJ | NEON_NANBOX_TAGNULL | NEON_NANBOX_TAGBOOL | NEON_NANBOX_TAGINT)

    #define NEON_VALUE_FALSE ((NNValue) (uint64_t) (NEON_NANBOX_QNAN | NEON_NANBOX_TAGBOOL | NEON_NANBOX_TAGVALFALSE))
    #define NEON_VALUE_TRUE ((NNValue) (uint64_t) (NEON_NANBOX_QNAN | NEON_NANBOX_TAGBOOL | NEON_NANBOX_TAGVALTRUE))
    #define NEON_VALUE_NULL ((NNValue) (uint64_t) (NEON_NANBOX_QNAN | NEON_NANBOX_TAGNULL))
#endif

#if defined(NEON_CONFIG_USENULLPTRCHECKS) && (NEON_CONFIG_USENULLPTRCHECKS == 1)
    #define NN_NULLPTRCHECK_RETURNVALUE(var, defval) \
        { \
            if((var) == NULL) \
            { \
                return (defval); \
            } \
        }
    #define NN_NULLPTRCHECK_RETURN(var) \
        { \
            if((var) == NULL) \
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

/* initial amount of frames (will grow dynamically if needed) */
#define NEON_CONFIG_INITFRAMECOUNT (32)

/* initial amount of stack values (will grow dynamically if needed) */
#define NEON_CONFIG_INITSTACKCOUNT (32 * 1)

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
#define NEON_CONFIG_MAXEXCEPTHANDLERS (16)

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
// at least one call to nn_gcmem_clearprotect() before exiting the function/block
// otherwise, expected unexpected behavior
// 2. The call to nn_gcmem_clearprotect() will be automatic for native functions.
// 3. $thisval must be retrieved before any call to nn_gcmem_protect in a
// native function.
*/
#define GROW_CAPACITY(capacity) \
    ((capacity) < 4 ? 4 : (capacity)*2)

#define nn_gcmem_growarray(state, typsz, pointer, oldcount, newcount) \
    nn_gcmem_reallocate(state, pointer, (typsz) * (oldcount), (typsz) * (newcount), false)

#define nn_gcmem_freearray(state, typsz, pointer, oldcount) \
    nn_gcmem_release(state, pointer, (typsz) * (oldcount))

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
#define NEON_ARGS_CHECKTYPE(chp, i, typefunc) \
    if(nn_util_unlikely(!typefunc((chp)->argv[i]))) \
    { \
        return NEON_ARGS_FAIL(chp, "%s() expects argument %d as %s, %s given", (chp)->name, (i) + 1, nn_value_typefromfunction(typefunc), nn_value_typename((chp)->argv[i], false), false); \
    }

#if 0
    #define NEON_APIDEBUG(state, ...) \
        if((nn_util_unlikely((state)->conf.enableapidebug))) \
        { \
            nn_state_apidebug(state, __FUNCTION__, __VA_ARGS__); \
        }
#else
    #define NEON_APIDEBUG(state, ...)
#endif

/*
* set to 1 to use allocator (default).
* stable, performs well, etc.
* might not be portable beyond linux/windows, and a couple unix derivatives.
* strives to use the least amount of memory (and does so very successfully).
*/
#define NEON_CONF_MEMUSEALLOCATOR 1

#define NEON_CONF_OSPATHSIZE 1024


#define nn_value_fromobject(obj) nn_value_fromobject_actual((NNObject*)obj)


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

#if !defined(NEON_CONFIG_USENANTAGGING) || (NEON_CONFIG_USENANTAGGING == 0)
enum NNValType
{
    NEON_VALTYPE_NULL,
    NEON_VALTYPE_BOOL,
    NEON_VALTYPE_NUMBER,
    NEON_VALTYPE_OBJ,
};
#endif

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

#if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
typedef uint64_t NNValue;
#else
typedef struct /**/ NNValue NNValue;
#endif


typedef enum /**/NNAstCompContext NNAstCompContext;
typedef enum /**/ NNColor NNColor;
typedef enum /**/NNFieldType NNFieldType;

#if !defined(NEON_CONFIG_USENANTAGGING) || (NEON_CONFIG_USENANTAGGING == 0)
typedef enum /**/ NNValType NNValType;
#endif

typedef enum /**/ NNOpCode NNOpCode;
typedef enum /**/ NNFuncContextType NNFuncContextType;
typedef enum /**/ NNObjType NNObjType;
typedef enum /**/ NNStatus NNStatus;
typedef enum /**/ NNAstTokType NNAstTokType;
typedef enum /**/ NNAstPrecedence NNAstPrecedence;
typedef enum /**/ NNPrMode NNPrMode;

typedef struct /**/ NNProcessInfo NNProcessInfo;
typedef struct /**/NNFormatInfo NNFormatInfo;
typedef struct /**/NNIOResult NNIOResult;
typedef struct /**/ NNAstFuncCompiler NNAstFuncCompiler;
typedef struct /**/ NNObject NNObject;
typedef struct /**/ NNObjString NNObjString;
typedef struct /**/ NNObjArray NNObjArray;
typedef struct /**/ NNObjUpvalue NNObjUpvalue;
typedef struct /**/ NNObjClass NNObjClass;

typedef struct /**/ NNObjModule NNObjModule;
typedef struct /**/ NNObjInstance NNObjInstance;
typedef struct /**/ NNObjRange NNObjRange;
typedef struct /**/ NNObjDict NNObjDict;
typedef struct /**/ NNObjFile NNObjFile;
typedef struct /**/ NNObjSwitch NNObjSwitch;
typedef struct /**/ NNObjUserdata NNObjUserdata;

typedef struct /**/ NNObjFunction NNObjFunction;


typedef struct /**/NNPropGetSet NNPropGetSet;
typedef struct /**/NNProperty NNProperty;
typedef struct /**/ NNValArray NNValArray;
typedef struct /**/ NNBlob NNBlob;
typedef struct /**/ NNHashValEntry NNHashValEntry;
typedef struct /**/ NNHashValTable NNHashValTable;
typedef struct /**/ NNExceptionFrame NNExceptionFrame;
typedef struct /**/ NNCallFrame NNCallFrame;
typedef struct /**/ NNAstToken NNAstToken;
typedef struct /**/ NNAstLexer NNAstLexer;
typedef struct /**/ NNAstLocal NNAstLocal;
typedef struct /**/ NNAstUpvalue NNAstUpvalue;
typedef struct /**/ NNAstClassCompiler NNAstClassCompiler;
typedef struct /**/ NNAstParser NNAstParser;
typedef struct /**/ NNAstRule NNAstRule;
typedef struct /**/ NNDefFunc NNDefFunc;
typedef struct /**/ NNDefField NNDefField;
typedef struct /**/ NNDefClass NNDefClass;
typedef struct /**/ NNDefModule NNDefModule;
typedef struct /**/ NNState NNState;
typedef struct /**/ NNPrinter NNPrinter;
typedef struct /**/ NNArgCheck NNArgCheck;
typedef struct /**/NNInstruction NNInstruction;
typedef struct utf8iterator_t utf8iterator_t;
typedef struct NNBoxedString NNBoxedString;
typedef struct NNConstClassMethodItem NNConstClassMethodItem;
typedef struct NNStringBuffer NNStringBuffer;
typedef union NNUtilDblUnion NNUtilDblUnion;

/* fwd from os.h */
typedef struct FSDirReader FSDirReader;
typedef struct FSDirItem FSDirItem;


/* fwd from allocator.h */
typedef size_t NNAllocInfoField;
typedef void NNAllocMSpace;
typedef size_t NNAllocBIndex;
typedef unsigned int NNAllocBinMap;
typedef unsigned int NNAllocFlag;
typedef struct NNAllocMallocInfo NNAllocMallocInfo;
typedef struct NNAllocChunkItem NNAllocChunkItem;
typedef struct NNAllocMemSegment NNAllocMemSegment;
typedef struct NNAllocChunkTree NNAllocChunkTree;
typedef struct NNAllocState NNAllocState;
typedef struct NNAllocParams NNAllocParams;


typedef bool(*NNValIsFuncFN)(NNValue);
typedef NNValue (*NNNativeFN)(NNState*, NNValue, NNValue*, size_t);
typedef void (*NNPtrFreeFN)(void*);
typedef bool (*NNAstParsePrefixFN)(NNAstParser*, bool);
typedef bool (*NNAstParseInfixFN)(NNAstParser*, NNAstToken, bool);
typedef NNValue (*NNClassFieldFN)(NNState*);
typedef void (*NNModLoaderFN)(NNState*);
typedef NNDefModule* (*NNModInitFN)(NNState*);
typedef double(*nnbinopfunc_t)(double, double);

typedef size_t (*mcitemhashfn_t)(void*);
typedef bool (*mcitemcomparefn_t)(void*, void*);

struct NNStringBuffer
{
    bool isintern;

    /* total length of this buffer */
    size_t length;

    /* capacity should be >= length+1 to allow for \0 */
    size_t capacity;

    char* data;
};

typedef struct FSDirReader FSDirReader;
typedef struct FSDirItem FSDirItem;

struct FSDirReader
{
    void* handle;
};

struct FSDirItem
{
    char name[NEON_CONF_OSPATHSIZE + 1];
    bool isdir;
    bool isfile;
};

struct NNConstClassMethodItem
{
    const char* name;
    NNNativeFN fn;
};

union NNUtilDblUnion
{
    uint64_t bits;
    double num;
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
    bool success;
    char* data;
    size_t length;    
};

struct NNPrinter
{
    /* if file: should be closed when writer is destroyed? */
    bool shouldclose;
    /* if file: should write operations be flushed via fflush()? */
    bool shouldflush;
    /* if string: true if $strbuf was taken via nn_printer_take */
    bool stringtaken;
    /* was this writer instance created on stack? */
    bool fromstack;
    bool shortenvalues;
    bool jsonmode;
    size_t maxvallength;
    /* the mode that determines what writer actually does */
    NNPrMode wrmode;
    NNState* pstate;
    NNStringBuffer strbuf;
    FILE* handle;
};

struct NNFormatInfo
{
    /* length of the format string */
	size_t                     fmtlen;               /*     0     8 */
    /* the actual format string */
	const char  *              fmtstr;               /*     8     8 */
	NNPrinter *                writer;               /*    16     8 */
	NNState *                  pstate;                  /*    24     8 */
};

#if !defined(NEON_CONFIG_USENANTAGGING) || (NEON_CONFIG_USENANTAGGING == 0)
struct NNValue
{
    NNValType type;
    union
    {
        bool vbool;
        double vfltnum;
        NNObject* vobjpointer;
    } valunion;
};
#endif

struct NNBoxedString
{
    bool isalloced;
    char* data;
    size_t length;
};


struct NNObject
{
    NNObjType type;
    bool mark;
    NNState* pstate;
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

struct NNPropGetSet
{
    NNValue getter;
    NNValue setter;
};

struct NNProperty
{
    bool havegetset;
    NNFieldType type;
    NNValue value;
    NNPropGetSet getset;
};

struct NNValArray
{
    NNState* pstate;
    const char* listname;
    NNValue* listitems;
    size_t listcapacity;
    size_t listcount;
};

struct NNInstruction
{
    /* is this instruction an opcode? */
    bool isop;
    /* opcode or value */
    uint8_t code;
    /* line corresponding to where this instruction was emitted */
    short srcline;
};

struct NNBlob
{
    int count;
    int capacity;
    NNState* pstate;
    NNInstruction* instrucs;
    NNValArray constants;
    NNValArray argdefvals;
};

struct NNHashValEntry
{
    NNValue key;
    NNProperty value;
};

struct NNHashValTable
{
    /*
    * FIXME: extremely stupid hack: $active ensures that a table that was destroyed
    * does not get marked again, et cetera.
    * since nn_valtable_destroy() zeroes the data before freeing, $active will be
    * false, and thus, no further marking occurs.
    * obviously the reason is that somewhere a table (from NNObjInstance) is being
    * read after being freed, but for now, this will work(-ish).
    */
    bool htactive;
    int htcount;
    int htcapacity;
    NNState* pstate;
    NNHashValEntry* htentries;
};

struct NNObjString
{
    NNObject objpadding;
    uint32_t hashvalue;
    NNStringBuffer sbuf;
};

struct NNObjUpvalue
{
    NNObject objpadding;
    int stackpos;
    NNValue closed;
    NNValue location;
    NNObjUpvalue* next;
};

struct NNObjModule
{
    NNObject objpadding;
    bool imported;
    NNHashValTable deftable;
    NNObjString* name;
    NNObjString* physicalpath;
    void* fnpreloaderptr;
    void* fnunloaderptr;
    void* handle;
};

#if 0
struct NNFuncDefaultVal
{
    size_t pos;
    NNValue val;
};
#endif


/**
* TODO: use a different table implementation to avoid allocating so many strings...
*/
struct NNObjClass
{
    NNObject objpadding;

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
    NNHashValTable instproperties;
    

    /*
    * static, unchangeable(-ish) values. intended for values that are not unique, but shared
    * across classes, without being copied.
    */
    NNHashValTable staticproperties;

    /*
    * method table; they are currently not unique when instantiated; instead, they are
    * read from $methods as-is. this includes adding methods!
    * TODO: introduce a new hashtable field for NNObjInstance for unique methods, perhaps?
    * right now, this method just prevents unnecessary copying.
    */
    NNHashValTable instmethods;
    NNHashValTable staticmethods;
    NNObjString* name;
    NNObjClass* superclass;
};

struct NNObjInstance
{
    NNObject objpadding;
    /*
    * whether this instance is still "active", i.e., not destroyed, deallocated, etc.
    */
    bool active;
    NNHashValTable properties;
    NNObjClass* klass;
    NNObjInstance* superinstance;
};

struct NNObjFunction
{
    NNObject objpadding;
    NNFuncContextType contexttype;
    NNObjString* name;
    int upvalcount;
    NNValue clsthisval;
    union
    {
        /* closure */
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
};

struct NNObjArray
{
    NNObject objpadding;
    NNValArray varray;
};

struct NNObjRange
{
    NNObject objpadding;
    int lower;
    int upper;
    int range;
};

struct NNObjDict
{
    NNObject objpadding;
    NNValArray names;
    NNHashValTable htab;
};

struct NNObjFile
{
    NNObject objpadding;
    bool isopen;
    bool isstd;
    bool istty;
    int number;
    FILE* handle;
    NNObjString* mode;
    NNObjString* path;
};

struct NNObjSwitch
{
    NNObject objpadding;
    int defaultjump;
    int exitjump;
    NNHashValTable table;
};

struct NNObjUserdata
{
    NNObject objpadding;
    void* pointer;
    char* name;
    NNPtrFreeFN ondestroyfn;
};

struct NNExceptionFrame
{
    uint16_t address;
    uint16_t finallyaddress;
    NNObjClass* klass;
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

    struct
    {
        int64_t stackidx;
        int64_t stackcapacity;
        int64_t framecapacity;
        int64_t framecount;
        NNInstruction currentinstr;
        NNCallFrame* currentframe;
        NNObjUpvalue* openupvalues;
        NNObject* linkedobjects;
        NNCallFrame* framevalues;
        NNValue* stackvalues;
    } vmstate;


    struct
    {
        int64_t graycount;
        int64_t graycapacity;
        int64_t bytesallocated;
        int64_t nextgc;
        NNObject** graystack;
    } gcstate;


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

    NNValue lastreplvalue;

    void* memuserptr;
    const char* rootphysfile;

    NNObjDict* envdict;
    NNObjString* constructorname;
    NNObjModule* topmodule;
    NNValArray importpath;

    /* objects tracker */
    NNHashValTable openedmodules;
    NNHashValTable allocatedstrings;
    NNHashValTable conststrings;
    NNHashValTable declaredglobals;

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
    bool markvalue;
    NNProcessInfo* processinfo;

    /* miscellaneous */
    NNPrinter* stdoutprinter;
    NNPrinter* stderrprinter;
    NNPrinter* debugwriter;
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

    /* function that is called directly upon loading the module. can be NULL. */
    NNModLoaderFN fnpreloaderfunc;

    /* function that is called before unloading the module. can be NULL. */
    NNModLoaderFN fnunloaderfunc;
};

struct NNArgCheck
{
    NNState* pstate;
    const char* name;
    int argc;
    NNValue* argv;
};

#include "prot.inc"

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

NEON_FORCEINLINE size_t nn_valtable_count(NNHashValTable* table)
{
    return table->htcount;
}

NEON_FORCEINLINE size_t nn_valtable_capacity(NNHashValTable* table)
{
    return table->htcapacity;
}

NEON_FORCEINLINE NNHashValEntry* nn_valtable_entryatindex(NNHashValTable* table, size_t idx)
{
    return &table->htentries[idx];
}

NEON_FORCEINLINE size_t nn_string_getlength(NNObjString* os)
{
    return os->sbuf.length;
}

NEON_FORCEINLINE const char* nn_string_getdata(NNObjString* os)
{
    return os->sbuf.data;
}

NEON_FORCEINLINE const char* nn_string_getcstr(NNObjString* os)
{
    return nn_string_getdata(os);
}

NEON_FORCEINLINE bool nn_value_isobject(NNValue v)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        return ((v & (NEON_NANBOX_QNAN | NEON_NANBOX_TYPEBITS)) == (NEON_NANBOX_QNAN | NEON_NANBOX_TAGOBJ));
    #else
        return ((v).type == NEON_VALTYPE_OBJ);
    #endif
}

NEON_FORCEINLINE NNObject* nn_value_asobject(NNValue v)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        return ((NNObject*) (uintptr_t) ((v) & (0 - ((NEON_NANBOX_TAGOBJ | NEON_NANBOX_QNAN) + 1))));
    #else
        return ((v).valunion.vobjpointer);
    #endif
}

NEON_FORCEINLINE bool nn_value_isobjtype(NNValue v, NNObjType t)
{
    return nn_value_isobject(v) && (nn_value_asobject(v)->type == t);
}

NEON_FORCEINLINE bool nn_value_isnull(NNValue v)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        return (v == NEON_VALUE_NULL);
    #else
        return ((v).type == NEON_VALTYPE_NULL);
    #endif
}

NEON_FORCEINLINE bool nn_value_isbool(NNValue v)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        return ((v & (NEON_NANBOX_QNAN | NEON_NANBOX_TYPEBITS)) == (NEON_NANBOX_QNAN | NEON_NANBOX_TAGBOOL));
    #else
        return ((v).type == NEON_VALTYPE_BOOL);
    #endif
}

NEON_FORCEINLINE bool nn_value_isnumber(NNValue v)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        return ((v & NEON_NANBOX_QNAN) != NEON_NANBOX_QNAN);
    #else
        return ((v).type == NEON_VALTYPE_NUMBER);
    #endif
}

NEON_FORCEINLINE bool nn_value_isstring(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_STRING);
}

NEON_FORCEINLINE bool nn_value_isfuncnative(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCNATIVE);
}

NEON_FORCEINLINE bool nn_value_isfuncscript(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCSCRIPT);
}

NEON_FORCEINLINE bool nn_value_isfuncclosure(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCCLOSURE);
}

NEON_FORCEINLINE bool nn_value_isfuncbound(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCBOUND);
}

NEON_FORCEINLINE bool nn_value_isclass(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_CLASS);
}

NEON_FORCEINLINE bool nn_value_isinstance(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_INSTANCE);
}

NEON_FORCEINLINE bool nn_value_isarray(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_ARRAY);
}

NEON_FORCEINLINE bool nn_value_isdict(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_DICT);
}

NEON_FORCEINLINE bool nn_value_isfile(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FILE);
}

NEON_FORCEINLINE bool nn_value_isrange(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_RANGE);
}

NEON_FORCEINLINE bool nn_value_ismodule(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_MODULE);
}

NEON_FORCEINLINE bool nn_value_iscallable(NNValue v)
{
    return (
        nn_value_isclass(v) ||
        nn_value_isfuncscript(v) ||
        nn_value_isfuncclosure(v) ||
        nn_value_isfuncbound(v) ||
        nn_value_isfuncnative(v)
    );
}

NEON_FORCEINLINE const char* nn_value_typefromfunction(NNValIsFuncFN func)
{
    if(func == nn_value_isstring)
    {
        return "string";
    }
    else if(func == nn_value_isnull)
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
    return "?unknown?";
}


NEON_FORCEINLINE NNObjType nn_value_objtype(NNValue v)
{
    return nn_value_asobject(v)->type;
}

NEON_FORCEINLINE bool nn_value_asbool(NNValue v)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        if(v == NEON_VALUE_TRUE)
        {
            return true;
        }
        return false;
    #else
        return ((v).valunion.vbool);
    #endif
}

NEON_FORCEINLINE double nn_value_asnumber(NNValue v)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        NNUtilDblUnion data;
        data.bits = v;
        return data.num;
    #else
        return ((v).valunion.vfltnum);
    #endif
}

NEON_FORCEINLINE NNObjString* nn_value_asstring(NNValue v)
{
    return ((NNObjString*)nn_value_asobject(v));
}

NEON_FORCEINLINE NNObjFunction* nn_value_asfunction(NNValue v)
{
    return ((NNObjFunction*)nn_value_asobject(v));
}

NEON_FORCEINLINE NNObjClass* nn_value_asclass(NNValue v)
{
    return ((NNObjClass*)nn_value_asobject(v));
}

NEON_FORCEINLINE NNObjInstance* nn_value_asinstance(NNValue v)
{
    return ((NNObjInstance*)nn_value_asobject(v));
}

NEON_FORCEINLINE NNObjSwitch* nn_value_asswitch(NNValue v)
{
    return ((NNObjSwitch*)nn_value_asobject(v));
}

NEON_FORCEINLINE NNObjUserdata* nn_value_asuserdata(NNValue v)
{
    return ((NNObjUserdata*)nn_value_asobject(v));
}

NEON_FORCEINLINE NNObjModule* nn_value_asmodule(NNValue v)
{
    return ((NNObjModule*)nn_value_asobject(v));
}

NEON_FORCEINLINE NNObjArray* nn_value_asarray(NNValue v)
{
    return ((NNObjArray*)nn_value_asobject(v));
}

NEON_FORCEINLINE NNObjDict* nn_value_asdict(NNValue v)
{
    return ((NNObjDict*)nn_value_asobject(v));
}

NEON_FORCEINLINE NNObjFile* nn_value_asfile(NNValue v)
{
    return ((NNObjFile*)nn_value_asobject(v));
}

NEON_FORCEINLINE NNObjRange* nn_value_asrange(NNValue v)
{
    return ((NNObjRange*)nn_value_asobject(v));
}

#if !defined(NEON_CONFIG_USENANTAGGING) || (NEON_CONFIG_USENANTAGGING == 0)
    NNValue nn_value_makevalue(NNValType type)
    {
        NNValue v;
        v.type = type;
        return v;
    }
#endif

NEON_FORCEINLINE NNValue nn_value_makenull()
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        return NEON_VALUE_NULL;
    #else
        NNValue v;
        v = nn_value_makevalue(NEON_VALTYPE_NULL);
        return v;
    #endif
}

NEON_FORCEINLINE NNValue nn_value_makebool(bool b)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        if(b)
        {
            return NEON_VALUE_TRUE;
        }
        return NEON_VALUE_FALSE;
    #else
        NNValue v;
        v = nn_value_makevalue(NEON_VALTYPE_BOOL);
        v.valunion.vbool = b;
        return v;
    #endif
}

NEON_FORCEINLINE NNValue nn_value_makenumber(double d)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        NNUtilDblUnion data;
        data.num = d;
        return data.bits;
    #else
        NNValue v;
        v = nn_value_makevalue(NEON_VALTYPE_NUMBER);
        v.valunion.vfltnum = d;
        return v;
    #endif
}

NEON_FORCEINLINE NNValue nn_value_makeint(int i)
{
    return nn_value_makenumber(i);
}

NEON_FORCEINLINE NNValue nn_value_fromobject_actual(NNObject* obj)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        return ((NNValue) (NEON_NANBOX_TAGOBJ | NEON_NANBOX_QNAN | (uint64_t)(uintptr_t)(obj)));
    #else
        NNValue v;
        v = nn_value_makevalue(NEON_VALTYPE_OBJ);
        v.valunion.vobjpointer = obj;
        return v;
    #endif
}


NEON_INLINE void nn_valarray_setcount(NNValArray* list, size_t cnt)
{
    list->listcount = cnt;
}

NEON_INLINE void nn_valarray_increaseby(NNValArray* list, size_t cnt)
{
    list->listcount += cnt;
}
    
NEON_INLINE void nn_valarray_decreaseby(NNValArray* list, size_t cnt)
{
    list->listcount -= cnt;
}

NEON_INLINE size_t nn_valarray_count(NNValArray* list)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, 0);
    return list->listcount;
}

NEON_INLINE size_t nn_valarray_capacity(NNValArray* list)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, 0);
    return list->listcapacity;
}

NEON_INLINE NNValue* nn_valarray_data(NNValArray* list)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, NULL);
    return list->listitems;
}

NEON_INLINE NNValue nn_valarray_get(NNValArray* list, size_t idx)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, nn_value_makenull());
    return list->listitems[idx];
}

NEON_INLINE NNValue* nn_valarray_getp(NNValArray* list, size_t idx)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, NULL);
    return &list->listitems[idx];
}


NEON_INLINE bool nn_valarray_insert(NNValArray* list, NNValue val, size_t idx)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, false);
    return nn_valarray_set(list, idx, val);
}

NEON_INLINE bool nn_valarray_pop(NNValArray* list, NNValue* dest)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, false);
    if(list->listcount > 0)
    {
        *dest = list->listitems[list->listcount - 1];
        list->listcount--;
        return true;
    }
    return false;
}

#endif

