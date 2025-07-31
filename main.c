

#if defined(_WIN32) || defined(_WIN64)
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


#include "strbuf.h"
#include "optparse.h"
#include "os.h"
#include "mem.h"
#include "deps/myregex/mrx.h"
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

#if !defined(NEON_PLAT_ISWASM) && !defined(NEON_PLAT_ISWINDOWS)
    #define NEON_CONFIG_USELINENOISE 1
#endif

#include "lino.h"

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

#undef NEON_FORCEINLINE
#undef NEON_INLINE
#define NEON_FORCEINLINE
#define NEON_INLINE


#if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
    #if defined(_MSC_VER)
        #pragma message("*** USING NAN TAGGING ***")
    #else
        #warning "*** USING NAN TAGGING ***"
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
#define NEON_ARGS_CHECKTYPE(chp, i, type) \
    if(nn_util_unlikely(!type((chp)->argv[i]))) \
    { \
        return NEON_ARGS_FAIL(chp, "%s() expects argument %d as %s, %s given", (chp)->name, (i) + 1, nn_value_typefromfunction(type), nn_value_typename((chp)->argv[i])); \
    }

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
typedef struct /**/ NNRegFunc NNRegFunc;
typedef struct /**/ NNRegField NNRegField;
typedef struct /**/ NNRegClass NNRegClass;
typedef struct /**/ NNRegModule NNRegModule;
typedef struct /**/ NNState NNState;
typedef struct /**/ NNPrinter NNPrinter;
typedef struct /**/ NNArgCheck NNArgCheck;
typedef struct /**/NNInstruction NNInstruction;
typedef struct utf8iterator_t utf8iterator_t;
typedef struct NNBoxedString NNBoxedString;
typedef struct NNHashPtrTable NNHashPtrTable;
typedef struct NNConstClassMethodItem NNConstClassMethodItem;

typedef union NNUtilDblUnion NNUtilDblUnion;

typedef bool(*NNValIsFuncFN)(NNValue);
typedef NNValue (*NNNativeFN)(NNState*, NNValue, NNValue*, size_t);
typedef void (*NNPtrFreeFN)(void*);
typedef bool (*NNAstParsePrefixFN)(NNAstParser*, bool);
typedef bool (*NNAstParseInfixFN)(NNAstParser*, NNAstToken, bool);
typedef NNValue (*NNClassFieldFN)(NNState*);
typedef void (*NNModLoaderFN)(NNState*);
typedef NNRegModule* (*NNModInitFN)(NNState*);
typedef double(*nnbinopfunc_t)(double, double);

typedef size_t (*mcitemhashfn_t)(void*);
typedef bool (*mcitemcomparefn_t)(void*, void*);


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
    StringBuffer strbuf;
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
    bool active;
    int count;
    int capacity;
    NNState* pstate;
    NNHashValEntry* entries;
};

struct NNHashPtrTable
{
    NNState* pstate;
    size_t keytypesize;
    size_t valtypesize;
    unsigned int* vdcells;
    unsigned long* vdhashes;
    char** vdkeys;
    void** vdvalues;
    unsigned int* vdcellindices;
    unsigned int vdcount;
    unsigned int vditemcapacity;
    unsigned int vdcellcapacity;
    mcitemhashfn_t funchashfn;
    mcitemcomparefn_t funckeyequalsfn;
};


struct NNObjString
{
    NNObject objpadding;
    uint32_t hashvalue;
    StringBuffer sbuf;
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
    void* preloader;
    void* unloader;
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
};

struct NNObjFunction
{
    NNObject objpadding;
    NNFuncContextType contexttype;
    NNObjString* name;
    int upvalcount;
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

struct NNRegFunc
{
    const char* name;
    bool isstatic;
    NNNativeFN function;
};

struct NNRegField
{
    const char* name;
    bool isstatic;
    NNClassFieldFN fieldvalfn;
};

struct NNRegClass
{
    const char* name;
    NNRegField* fields;
    NNRegFunc* functions;
};

struct NNRegModule
{
    /*
    * the name of this module.
    * note: if the name must be preserved, copy it; it is only a pointer to a
    * string that gets freed past loading.
    */
    const char* name;

    /* exported fields, if any. */
    NNRegField* fields;

    /* regular functions, if any. */
    NNRegFunc* functions;

    /* exported classes, if any.
    * i.e.:
    * {"Stuff",
    *   (NNRegField[]){
    *       {"enabled", true},
    *       ...
    *   },
    *   (NNRegFunc[]){
    *       {"isStuff", myclass_fn_isstuff},
    *       ...
    * }})*/
    NNRegClass* classes;

    /* function that is called directly upon loading the module. can be NULL. */
    NNModLoaderFN preloader;

    /* function that is called before unloading the module. can be NULL. */
    NNModLoaderFN unloader;
};

struct NNArgCheck
{
    NNState* pstate;
    const char* name;
    int argc;
    NNValue* argv;
};

#include "prot.inc"

static const char* g_strthis = "this";
static const char* g_strsuper = "super";


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

uint32_t nn_utf8iter_converter(const char* character, uint8_t size)
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

/* returns 1 if there is a character in the next position. If there is not, return 0. */
uint8_t nn_utf8iter_next(utf8iterator_t* iter)
{
    const char* pointer;
    if(iter == NULL)
    {
        return 0;
    }
    if(iter->plainstr == NULL)
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
    if(iter == NULL)
    {
        return str;
    }
    if(iter->plainstr == NULL)
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

bool nn_value_isobject(NNValue v)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        return ((v & (NEON_NANBOX_QNAN | NEON_NANBOX_TYPEBITS)) == (NEON_NANBOX_QNAN | NEON_NANBOX_TAGOBJ));
    #else
        return ((v).type == NEON_VALTYPE_OBJ);
    #endif
}

NNObject* nn_value_asobject(NNValue v)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        return ((NNObject*) (uintptr_t) ((v) & (0 - ((NEON_NANBOX_TAGOBJ | NEON_NANBOX_QNAN) + 1))));
    #else
        return ((v).valunion.vobjpointer);
    #endif
}

bool nn_value_isobjtype(NNValue v, NNObjType t)
{
    return nn_value_isobject(v) && nn_value_asobject(v)->type == t;
}

bool nn_value_isnull(NNValue v)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        return (v == NEON_VALUE_NULL);
    #else
        return ((v).type == NEON_VALTYPE_NULL);
    #endif
}

bool nn_value_isbool(NNValue v)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        return ((v & (NEON_NANBOX_QNAN | NEON_NANBOX_TYPEBITS)) == (NEON_NANBOX_QNAN | NEON_NANBOX_TAGBOOL));
    #else
        return ((v).type == NEON_VALTYPE_BOOL);
    #endif
}

bool nn_value_isnumber(NNValue v)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        return ((v & NEON_NANBOX_QNAN) != NEON_NANBOX_QNAN);
    #else
        return ((v).type == NEON_VALTYPE_NUMBER);
    #endif
}

bool nn_value_isstring(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_STRING);
}

bool nn_value_isfuncnative(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCNATIVE);
}

bool nn_value_isfuncscript(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCSCRIPT);
}

bool nn_value_isfuncclosure(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCCLOSURE);
}

bool nn_value_isfuncbound(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCBOUND);
}

bool nn_value_isclass(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_CLASS);
}

bool nn_value_isinstance(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_INSTANCE);
}

bool nn_value_isarray(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_ARRAY);
}

bool nn_value_isdict(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_DICT);
}

bool nn_value_isfile(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FILE);
}

bool nn_value_isrange(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_RANGE);
}

bool nn_value_ismodule(NNValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_MODULE);
}

bool nn_value_iscallable(NNValue v)
{
    return (
        nn_value_isclass(v) ||
        nn_value_isfuncscript(v) ||
        nn_value_isfuncclosure(v) ||
        nn_value_isfuncbound(v) ||
        nn_value_isfuncnative(v)
    );
}

NNObjType nn_value_objtype(NNValue v)
{
    return nn_value_asobject(v)->type;
}

bool nn_value_asbool(NNValue v)
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

double nn_value_asnumber(NNValue v)
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        NNUtilDblUnion data;
        data.bits = v;
        return data.num;
    #else
        return ((v).valunion.vfltnum);
    #endif
}

NNObjString* nn_value_asstring(NNValue v)
{
    return ((NNObjString*)nn_value_asobject(v));
}


NNObjFunction* nn_value_asfunction(NNValue v)
{
    return ((NNObjFunction*)nn_value_asobject(v));
}

NNObjClass* nn_value_asclass(NNValue v)
{
    return ((NNObjClass*)nn_value_asobject(v));
}

NNObjInstance* nn_value_asinstance(NNValue v)
{
    return ((NNObjInstance*)nn_value_asobject(v));
}

NNObjSwitch* nn_value_asswitch(NNValue v)
{
    return ((NNObjSwitch*)nn_value_asobject(v));
}

NNObjUserdata* nn_value_asuserdata(NNValue v)
{
    return ((NNObjUserdata*)nn_value_asobject(v));
}

NNObjModule* nn_value_asmodule(NNValue v)
{
    return ((NNObjModule*)nn_value_asobject(v));
}

NNObjArray* nn_value_asarray(NNValue v)
{
    return ((NNObjArray*)nn_value_asobject(v));
}

NNObjDict* nn_value_asdict(NNValue v)
{
    return ((NNObjDict*)nn_value_asobject(v));
}

NNObjFile* nn_value_asfile(NNValue v)
{
    return ((NNObjFile*)nn_value_asobject(v));
}

NNObjRange* nn_value_asrange(NNValue v)
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

NNValue nn_value_makenull()
{
    #if defined(NEON_CONFIG_USENANTAGGING) && (NEON_CONFIG_USENANTAGGING == 1)
        return NEON_VALUE_NULL;
    #else
        NNValue v;
        v = nn_value_makevalue(NEON_VALTYPE_NULL);
        return v;
    #endif
}

NNValue nn_value_makebool(bool b)
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

NNValue nn_value_makenumber(double d)
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

NNValue nn_value_makeint(int i)
{
    return nn_value_makenumber(i);
}

#define nn_value_fromobject(obj) nn_value_fromobject_actual((NNObject*)obj)

NNValue nn_value_fromobject_actual(NNObject* obj)
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

NNValue nn_value_copystrlen(NNState* state, const char* str, size_t len)
{
    return nn_value_fromobject(nn_string_copylen(state, str, len));
}

NNValue nn_value_copystr(NNState* state, const char* str)
{
    return nn_value_copystrlen(state, str, strlen(str));
}

#include "vallist.h"
#include "hashtabval.h"

NNObject* nn_gcmem_protect(NNState* state, NNObject* object)
{
    size_t frpos;
    nn_vm_stackpush(state, nn_value_fromobject(object));
    frpos = 0;
    if(state->vmstate.framecount > 0)
    {
        frpos = state->vmstate.framecount - 1;
    }
    state->vmstate.framevalues[frpos].gcprotcount++;
    return object;
}

void nn_gcmem_clearprotect(NNState* state)
{
    size_t frpos;
    NNCallFrame* frame;
    frpos = 0;
    if(state->vmstate.framecount > 0)
    {
        frpos = state->vmstate.framecount - 1;
    }
    frame = &state->vmstate.framevalues[frpos];
    if(frame->gcprotcount > 0)
    {
        state->vmstate.stackidx -= frame->gcprotcount;
    }
    frame->gcprotcount = 0;
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
    if(buf == NULL)
    {
        return NULL;
    }
    memset(buf, 0, len+1);
    memcpy(buf, src, len);
    return buf;
}

char* nn_util_strdup(const char* src)
{
    return nn_util_strndup(src, strlen(src));
}

char* nn_util_filereadhandle(NNState* state, FILE* hnd, size_t* dlen, bool havemaxsz, size_t maxsize)
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
    if(havemaxsz)
    {
        if(toldlen > maxsize)
        {
            toldlen = maxsize;
        }
    }
    buf = (char*)nn_memory_malloc(sizeof(char) * (toldlen + 1));
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

char* nn_util_filereadfile(NNState* state, const char* filename, size_t* dlen, bool havemaxsz, size_t maxsize)
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
    b = nn_util_filereadhandle(state, fh, dlen, havemaxsz, maxsize);
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
    return NULL;
}


int nn_util_filegetlinehandle(char **lineptr, size_t *destlen, FILE* hnd)
{
    enum { kInitialStrBufSize = 256 };
    static char stackbuf[kInitialStrBufSize];
    char *heapbuf;
    size_t getlen;
    unsigned int linelen;
    getlen = 0;
    if(lineptr == NULL || destlen == NULL)
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
        if(heapbuf == NULL)
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
    return NULL;
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
    return NULL;
}

/*
// converts codepoint indexes start and end to byte offsets in the buffer at s
*/
void nn_util_utf8slice(char* s, int* start, int* end)
{
    char* p;
    p = nn_util_utf8index(s, *start);
    if(p != NULL)
    {
        *start = (int)(p - s);
    }
    else
    {
        *start = -1;
    }
    p = nn_util_utf8index(s, *end);
    if(p != NULL)
    {
        *end = (int)(p - s);
    }
    else
    {
        *end = (int)strlen(s);
    }
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


#if 0
    #define NEON_APIDEBUG(state, ...) \
        if((nn_util_unlikely((state)->conf.enableapidebug))) \
        { \
            nn_state_apidebug(state, __FUNCTION__, __VA_ARGS__); \
        }
#else
    #define NEON_APIDEBUG(state, ...)
#endif

void nn_gcmem_maybecollect(NNState* state, int addsize, bool wasnew)
{
    state->gcstate.bytesallocated += addsize;
    if(state->gcstate.nextgc > 0)
    {
        if(wasnew && state->gcstate.bytesallocated > state->gcstate.nextgc)
        {
            if(state->vmstate.currentframe && state->vmstate.currentframe->gcprotcount == 0)
            {
                nn_gcmem_collectgarbage(state);
            }
        }
    }
}

void* nn_gcmem_reallocate(NNState* state, void* pointer, size_t oldsize, size_t newsize, bool retain)
{
    void* result;
    if(!retain)
    {
        nn_gcmem_maybecollect(state, newsize - oldsize, newsize > oldsize);
    }
    if(pointer == NULL)
    {
        result = nn_memory_malloc(newsize);
    }
    else
    {
        result = nn_memory_realloc(pointer, newsize);
    }
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

void* nn_gcmem_allocate(NNState* state, size_t size, size_t amount, bool retain)
{
    return nn_gcmem_reallocate(state, NULL, 0, size * amount, retain);
}

void nn_gcmem_release(NNState* state, void* pointer, size_t oldsize)
{
    nn_gcmem_maybecollect(state, -oldsize, false);
    if(oldsize > 0)
    {
        memset(pointer, 0, oldsize);
    }
    nn_memory_free(pointer);
    pointer = NULL;
}

void nn_gcmem_markobject(NNState* state, NNObject* object)
{
    if(object == NULL)
    {
        return;
    }
    if(object->mark == state->markvalue)
    {
        return;
    }
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_printer_printf(state->debugwriter, "GC: marking object at <%p> ", (void*)object);
    nn_printer_printvalue(state->debugwriter, nn_value_fromobject(object), false);
    nn_printer_printf(state->debugwriter, "\n");
    #endif
    object->mark = state->markvalue;
    if(state->gcstate.graycapacity < state->gcstate.graycount + 1)
    {
        state->gcstate.graycapacity = GROW_CAPACITY(state->gcstate.graycapacity);
        state->gcstate.graystack = (NNObject**)nn_memory_realloc(state->gcstate.graystack, sizeof(NNObject*) * state->gcstate.graycapacity);
        if(state->gcstate.graystack == NULL)
        {
            fflush(stdout);
            fprintf(stderr, "GC encountered an error");
            abort();
        }
    }
    state->gcstate.graystack[state->gcstate.graycount++] = object;
}

void nn_gcmem_markvalue(NNState* state, NNValue value)
{
    if(nn_value_isobject(value))
    {
        nn_gcmem_markobject(state, nn_value_asobject(value));
    }
}

void nn_gcmem_blackenobject(NNState* state, NNObject* object)
{
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_printer_printf(state->debugwriter, "GC: blacken object at <%p> ", (void*)object);
    nn_printer_printvalue(state->debugwriter, nn_value_fromobject(object), false);
    nn_printer_printf(state->debugwriter, "\n");
    #endif
    switch(object->type)
    {
        case NEON_OBJTYPE_MODULE:
            {
                NNObjModule* module;
                module = (NNObjModule*)object;
                nn_valtable_mark(state, &module->deftable);
            }
            break;
        case NEON_OBJTYPE_SWITCH:
            {
                NNObjSwitch* sw;
                sw = (NNObjSwitch*)object;
                nn_valtable_mark(state, &sw->table);
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
                nn_valarray_mark(&dict->names);
                nn_valtable_mark(state, &dict->htab);
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
                nn_gcmem_markvalue(state, bound->fnmethod.receiver);
                nn_gcmem_markobject(state, (NNObject*)bound->fnmethod.method);
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                NNObjClass* klass;
                klass = (NNObjClass*)object;
                nn_gcmem_markobject(state, (NNObject*)klass->name);
                nn_valtable_mark(state, &klass->instmethods);
                nn_valtable_mark(state, &klass->staticmethods);
                nn_valtable_mark(state, &klass->staticproperties);
                nn_gcmem_markvalue(state, klass->constructor);
                nn_gcmem_markvalue(state, klass->destructor);
                if(klass->superclass != NULL)
                {
                    nn_gcmem_markobject(state, (NNObject*)klass->superclass);
                }
            }
            break;
        case NEON_OBJTYPE_FUNCCLOSURE:
            {
                int i;
                NNObjFunction* closure;
                closure = (NNObjFunction*)object;
                nn_gcmem_markobject(state, (NNObject*)closure->fnclosure.scriptfunc);
                for(i = 0; i < closure->upvalcount; i++)
                {
                    nn_gcmem_markobject(state, (NNObject*)closure->fnclosure.upvalues[i]);
                }
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                NNObjFunction* function;
                function = (NNObjFunction*)object;
                nn_gcmem_markobject(state, (NNObject*)function->name);
                nn_gcmem_markobject(state, (NNObject*)function->fnscriptfunc.module);
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
                nn_gcmem_markvalue(state, ((NNObjUpvalue*)object)->closed);
            }
            break;
        case NEON_OBJTYPE_RANGE:
        case NEON_OBJTYPE_FUNCNATIVE:
        case NEON_OBJTYPE_USERDATA:
        case NEON_OBJTYPE_STRING:
            break;
    }
}

void nn_object_destroy(NNState* state, NNObject* object)
{
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_printer_printf(state->debugwriter, "GC: freeing at <%p> of type %d\n", (void*)object, object->type);
    #endif
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
                nn_module_destroy(state, module);
                nn_gcmem_release(state, object, sizeof(NNObjModule));
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
                nn_valarray_destroy(&dict->names, false);
                nn_valtable_destroy(&dict->htab);
                nn_gcmem_release(state, object, sizeof(NNObjDict));
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                NNObjArray* list;
                list = (NNObjArray*)object;
                nn_valarray_destroy(&list->varray, false);
                nn_gcmem_release(state, object, sizeof(NNObjArray));
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
            {
                /*
                // a closure may be bound to multiple instances
                // for this reason, we do not free closures when freeing bound methods
                */
                nn_gcmem_release(state, object, sizeof(NNObjFunction));
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
                nn_gcmem_freearray(state, sizeof(NNObjUpvalue*), closure->fnclosure.upvalues, closure->upvalcount);
                /*
                // there may be multiple closures that all reference the same function
                // for this reason, we do not free functions when freeing closures
                */
                nn_gcmem_release(state, object, sizeof(NNObjFunction));
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                NNObjFunction* function;
                function = (NNObjFunction*)object;
                nn_funcscript_destroy(function);
                nn_gcmem_release(state, function, sizeof(NNObjFunction));
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
                nn_gcmem_release(state, object, sizeof(NNObjFunction));
            }
            break;
        case NEON_OBJTYPE_UPVALUE:
            {
                nn_gcmem_release(state, object, sizeof(NNObjUpvalue));
            }
            break;
        case NEON_OBJTYPE_RANGE:
            {
                nn_gcmem_release(state, object, sizeof(NNObjRange));
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                NNObjString* string;
                string = (NNObjString*)object;
                nn_string_destroy(state, string);
            }
            break;
        case NEON_OBJTYPE_SWITCH:
            {
                NNObjSwitch* sw;
                sw = (NNObjSwitch*)object;
                nn_valtable_destroy(&sw->table);
                nn_gcmem_release(state, object, sizeof(NNObjSwitch));
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
                nn_gcmem_release(state, object, sizeof(NNObjUserdata));
            }
            break;
        default:
            break;
    }
}

void nn_gcmem_markroots(NNState* state)
{
    int i;
    int j;
    NNValue* slot;
    NNObjUpvalue* upvalue;
    NNExceptionFrame* handler;
    for(slot = state->vmstate.stackvalues; slot < &state->vmstate.stackvalues[state->vmstate.stackidx]; slot++)
    {
        nn_gcmem_markvalue(state, *slot);
    }
    for(i = 0; i < (int)state->vmstate.framecount; i++)
    {
        nn_gcmem_markobject(state, (NNObject*)state->vmstate.framevalues[i].closure);
        for(j = 0; j < (int)state->vmstate.framevalues[i].handlercount; j++)
        {
            handler = &state->vmstate.framevalues[i].handlers[j];
            nn_gcmem_markobject(state, (NNObject*)handler->klass);
        }
    }
    for(upvalue = state->vmstate.openupvalues; upvalue != NULL; upvalue = upvalue->next)
    {
        nn_gcmem_markobject(state, (NNObject*)upvalue);
    }
    nn_valtable_mark(state, &state->declaredglobals);
    nn_valtable_mark(state, &state->openedmodules);
    nn_gcmem_markobject(state, (NNObject*)state->exceptions.stdexception);
    nn_gcmem_markcompilerroots(state);
}

void nn_gcmem_tracerefs(NNState* state)
{
    NNObject* object;
    while(state->gcstate.graycount > 0)
    {
        state->gcstate.graycount--;
        object = state->gcstate.graystack[state->gcstate.graycount];
        nn_gcmem_blackenobject(state, object);
    }
}

void nn_gcmem_sweep(NNState* state)
{
    NNObject* object;
    NNObject* previous;
    NNObject* unreached;
    previous = NULL;
    object = state->vmstate.linkedobjects;
    while(object != NULL)
    {
        if(object->mark == state->markvalue)
        {
            previous = object;
            object = object->next;
        }
        else
        {
            unreached = object;
            object = object->next;
            if(previous != NULL)
            {
                previous->next = object;
            }
            else
            {
                state->vmstate.linkedobjects = object;
            }
            nn_object_destroy(state, unreached);
        }
    }
}

void nn_gcmem_destroylinkedobjects(NNState* state)
{
    NNObject* next;
    NNObject* object;
    object = state->vmstate.linkedobjects;
    while(object != NULL)
    {
        next = object->next;
        nn_object_destroy(state, object);
        object = next;
    }
    nn_memory_free(state->gcstate.graystack);
    state->gcstate.graystack = NULL;
}

void nn_gcmem_collectgarbage(NNState* state)
{
    size_t before;
    (void)before;
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_printer_printf(state->debugwriter, "GC: gc begins\n");
    before = state->gcstate.bytesallocated;
    #endif
    /*
    //  REMOVE THE NEXT LINE TO DISABLE NESTED nn_gcmem_collectgarbage() POSSIBILITY!
    */
    #if 1
    state->gcstate.nextgc = state->gcstate.bytesallocated;
    #endif
    nn_gcmem_markroots(state);
    nn_gcmem_tracerefs(state);
    nn_valtable_removewhites(state, &state->allocatedstrings);
    nn_valtable_removewhites(state, &state->openedmodules);
    nn_gcmem_sweep(state);
    state->gcstate.nextgc = state->gcstate.bytesallocated * NEON_CONFIG_GCHEAPGROWTHFACTOR;
    state->markvalue = !state->markvalue;
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_printer_printf(state->debugwriter, "GC: gc ends\n");
    nn_printer_printf(state->debugwriter, "GC: collected %zu bytes (from %zu to %zu), next at %zu\n", before - state->gcstate.bytesallocated, before, state->gcstate.bytesallocated, state->gcstate.nextgc);
    #endif
}

NNValue nn_argcheck_vfail(NNArgCheck* ch, const char* srcfile, int srcline, const char* fmt, va_list va)
{
    //nn_vm_stackpopn(ch->pstate, ch->argc);
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

void nn_dbg_disasmblob(NNPrinter* pr, NNBlob* blob, const char* name)
{
    int offset;
    nn_printer_printf(pr, "== compiled '%s' [[\n", name);
    for(offset = 0; offset < blob->count;)
    {
        offset = nn_dbg_printinstructionat(pr, blob, offset);
    }
    nn_printer_printf(pr, "]]\n");
}

void nn_dbg_printinstrname(NNPrinter* pr, const char* name)
{
    nn_printer_printf(pr, "%s%-16s%s ", nn_util_color(NEON_COLOR_RED), name, nn_util_color(NEON_COLOR_RESET));
}

int nn_dbg_printsimpleinstr(NNPrinter* pr, const char* name, int offset)
{
    nn_dbg_printinstrname(pr, name);
    nn_printer_printf(pr, "\n");
    return offset + 1;
}

int nn_dbg_printconstinstr(NNPrinter* pr, const char* name, NNBlob* blob, int offset)
{
    uint16_t constant;
    constant = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    nn_printer_printf(pr, "%8d ", constant);
    nn_printer_printvalue(pr, nn_valarray_get(&blob->constants, constant), true, false);
    nn_printer_printf(pr, "\n");
    return offset + 3;
}

int nn_dbg_printpropertyinstr(NNPrinter* pr, const char* name, NNBlob* blob, int offset)
{
    const char* proptn;
    uint16_t constant;
    constant = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    nn_printer_printf(pr, "%8d ", constant);
    nn_printer_printvalue(pr, nn_valarray_get(&blob->constants, constant), true, false);
    proptn = "";
    if(blob->instrucs[offset + 3].code == 1)
    {
        proptn = "static";
    }
    nn_printer_printf(pr, " (%s)", proptn);
    nn_printer_printf(pr, "\n");
    return offset + 4;
}

int nn_dbg_printshortinstr(NNPrinter* pr, const char* name, NNBlob* blob, int offset)
{
    uint16_t slot;
    slot = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    nn_printer_printf(pr, "%8d\n", slot);
    return offset + 3;
}

int nn_dbg_printbyteinstr(NNPrinter* pr, const char* name, NNBlob* blob, int offset)
{
    uint8_t slot;
    slot = blob->instrucs[offset + 1].code;
    nn_dbg_printinstrname(pr, name);
    nn_printer_printf(pr, "%8d\n", slot);
    return offset + 2;
}

int nn_dbg_printjumpinstr(NNPrinter* pr, const char* name, int sign, NNBlob* blob, int offset)
{
    uint16_t jump;
    jump = (uint16_t)(blob->instrucs[offset + 1].code << 8);
    jump |= blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pr, name);
    nn_printer_printf(pr, "%8d -> %d\n", offset, offset + 3 + sign * jump);
    return offset + 3;
}

int nn_dbg_printtryinstr(NNPrinter* pr, const char* name, NNBlob* blob, int offset)
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
    nn_printer_printf(pr, "%8d -> %d, %d\n", type, address, finally);
    return offset + 7;
}

int nn_dbg_printinvokeinstr(NNPrinter* pr, const char* name, NNBlob* blob, int offset)
{
    uint16_t constant;
    uint8_t argcount;
    constant = (uint16_t)(blob->instrucs[offset + 1].code << 8);
    constant |= blob->instrucs[offset + 2].code;
    argcount = blob->instrucs[offset + 3].code;
    nn_dbg_printinstrname(pr, name);
    nn_printer_printf(pr, "(%d args) %8d ", argcount, constant);
    nn_printer_printvalue(pr, nn_valarray_get(&blob->constants, constant), true, false);
    nn_printer_printf(pr, "\n");
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

int nn_dbg_printclosureinstr(NNPrinter* pr, const char* name, NNBlob* blob, int offset)
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
    nn_printer_printf(pr, "%-16s %8d ", name, constant);
    nn_printer_printvalue(pr, nn_valarray_get(&blob->constants, constant), true, false);
    nn_printer_printf(pr, "\n");
    function = nn_value_asfunction(nn_valarray_get(&blob->constants, constant));
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
        nn_printer_printf(pr, "%04d      |                     %s %d\n", offset - 3, locn, (int)index);
    }
    return offset;
}

int nn_dbg_printinstructionat(NNPrinter* pr, NNBlob* blob, int offset)
{
    uint8_t instruction;
    const char* opname;
    nn_printer_printf(pr, "%08d ", offset);
    if(offset > 0 && blob->instrucs[offset].srcline == blob->instrucs[offset - 1].srcline)
    {
        nn_printer_printf(pr, "       | ");
    }
    else
    {
        nn_printer_printf(pr, "%8d ", blob->instrucs[offset].srcline);
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
                nn_printer_printf(pr, "unknown opcode %d\n", instruction);
            }
            break;
    }
    return offset + 1;
}

void nn_blob_init(NNState* state, NNBlob* blob)
{
    blob->pstate = state;
    blob->count = 0;
    blob->capacity = 0;
    blob->instrucs = NULL;
    nn_valarray_init(state, &blob->constants);
    nn_valarray_init(state, &blob->argdefvals);
}

void nn_blob_push(NNBlob* blob, NNInstruction ins)
{
    NNState* state;
    int oldcapacity;
    state = blob->pstate;
    if(blob->capacity < blob->count + 1)
    {
        oldcapacity = blob->capacity;
        blob->capacity = GROW_CAPACITY(oldcapacity);
        blob->instrucs = (NNInstruction*)nn_gcmem_growarray(state, sizeof(NNInstruction), blob->instrucs, oldcapacity, blob->capacity);
    }
    blob->instrucs[blob->count] = ins;
    blob->count++;
}

void nn_blob_destroy(NNBlob* blob)
{
    if(blob->instrucs != NULL)
    {
        nn_memory_free(blob->instrucs);
    }
    nn_valarray_destroy(&blob->constants, false);
    nn_valarray_destroy(&blob->argdefvals, false);
}

int nn_blob_pushconst(NNBlob* blob, NNValue value)
{
    nn_valarray_push(&blob->constants, value);
    return nn_valarray_count(&blob->constants) - 1;
}

NNProperty nn_property_makewithpointer(NNState* state, NNValue val, NNFieldType type)
{
    NNProperty vf;
    (void)state;
    memset(&vf, 0, sizeof(NNProperty));
    vf.type = type;
    vf.value = val;
    vf.havegetset = false;
    return vf;
}

NNProperty nn_property_makewithgetset(NNState* state, NNValue val, NNValue getter, NNValue setter, NNFieldType type)
{
    bool getisfn;
    bool setisfn;
    NNProperty np;
    np = nn_property_makewithpointer(state, val, type);
    setisfn = nn_value_iscallable(setter);
    getisfn = nn_value_iscallable(getter);
    if(getisfn || setisfn)
    {
        np.getset.setter = setter;
        np.getset.getter = getter;
    }
    return np;
}

NNProperty nn_property_make(NNState* state, NNValue val, NNFieldType type)
{
    return nn_property_makewithpointer(state, val, type);
}

void nn_valtable_print(NNState* state, NNPrinter* pr, NNHashValTable* table, const char* name)
{
    int i;
    NNHashValEntry* entry;
    (void)state;
    nn_printer_printf(pr, "<HashTable of %s : {\n", name);
    for(i = 0; i < table->capacity; i++)
    {
        entry = &table->entries[i];
        if(!nn_value_isnull(entry->key))
        {
            nn_printer_printvalue(pr, entry->key, true, true);
            nn_printer_printf(pr, ": ");
            nn_printer_printvalue(pr, entry->value.value, true, true);
            if(i != table->capacity - 1)
            {
                nn_printer_printf(pr, ",\n");
            }
        }
    }
    nn_printer_printf(pr, "}>\n");
}

void nn_printer_initvars(NNState* state, NNPrinter* pr, NNPrMode mode)
{
    pr->pstate = state;
    pr->fromstack = false;
    pr->wrmode = NEON_PRMODE_UNDEFINED;
    pr->shouldclose = false;
    pr->shouldflush = false;
    pr->stringtaken = false;
    pr->shortenvalues = false;
    pr->jsonmode = false;
    pr->maxvallength = 15;
    pr->handle = NULL;
    pr->wrmode = mode;
}

NNPrinter* nn_printer_makeundefined(NNState* state, NNPrMode mode)
{
    NNPrinter* pr;
    (void)state;
    pr = (NNPrinter*)nn_memory_malloc(sizeof(NNPrinter));
    if(!pr)
    {
        fprintf(stderr, "cannot allocate NNPrinter\n");
        return NULL;
    }
    nn_printer_initvars(state, pr, mode);
    return pr;
}

NNPrinter* nn_printer_makeio(NNState* state, FILE* fh, bool shouldclose)
{
    NNPrinter* pr;
    pr = nn_printer_makeundefined(state, NEON_PRMODE_FILE);
    pr->handle = fh;
    pr->shouldclose = shouldclose;
    return pr;
}

NNPrinter* nn_printer_makestring(NNState* state)
{
    NNPrinter* pr;
    pr = nn_printer_makeundefined(state, NEON_PRMODE_STRING);
    dyn_strbuf_makebasicemptystack(&pr->strbuf, 0);
    return pr;
}

void nn_printer_makestackio(NNState* state, NNPrinter* pr, FILE* fh, bool shouldclose)
{
    nn_printer_initvars(state, pr, NEON_PRMODE_FILE);
    pr->fromstack = true;
    pr->handle = fh;
    pr->shouldclose = shouldclose;
}

void nn_printer_makestackstring(NNState* state, NNPrinter* pr)
{
    nn_printer_initvars(state, pr, NEON_PRMODE_STRING);
    pr->fromstack = true;
    pr->wrmode = NEON_PRMODE_STRING;
    dyn_strbuf_makebasicemptystack(&pr->strbuf, 0);
}

void nn_printer_destroy(NNPrinter* pr)
{
    NNState* state;
    (void)state;
    if(pr == NULL)
    {
        return;
    }
    if(pr->wrmode == NEON_PRMODE_UNDEFINED)
    {
        return;
    }
    /*fprintf(stderr, "nn_printer_destroy: pr->wrmode=%d\n", pr->wrmode);*/
    state = pr->pstate;
    if(pr->wrmode == NEON_PRMODE_STRING)
    {
        if(!pr->stringtaken)
        {
            dyn_strbuf_destroy(&pr->strbuf);
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
        pr = NULL;
    }
}

NNObjString* nn_printer_takestring(NNPrinter* pr)
{
    NNState* state;
    NNObjString* os;
    state = pr->pstate;
    os = nn_string_takelen(state, pr->strbuf.data, pr->strbuf.length);
    pr->stringtaken = true;
    return os;
}

NNObjString* nn_printer_copystring(NNPrinter* pr)
{
    NNState* state;
    NNObjString* os;
    state = pr->pstate;
    os = nn_string_copylen(state, pr->strbuf.data, pr->strbuf.length);
    return os;
}

bool nn_printer_writestringl(NNPrinter* pr, const char* estr, size_t elen)
{
    if(elen > 0)
    {
        if(pr->wrmode == NEON_PRMODE_FILE)
        {
            fwrite(estr, sizeof(char), elen, pr->handle);
            if(pr->shouldflush)
            {
                fflush(pr->handle);
            }
        }
        else if(pr->wrmode == NEON_PRMODE_STRING)
        {
            dyn_strbuf_appendstrn(&pr->strbuf, estr, elen);
        }
        else
        {
            return false;
        }
    }
    return true;
}

bool nn_printer_writestring(NNPrinter* pr, const char* estr)
{
    return nn_printer_writestringl(pr, estr, strlen(estr));
}

bool nn_printer_writechar(NNPrinter* pr, int b)
{
    char ch;
    if(pr->wrmode == NEON_PRMODE_STRING)
    {
        ch = b;
        nn_printer_writestringl(pr, &ch, 1);
    }
    else if(pr->wrmode == NEON_PRMODE_FILE)
    {
        fputc(b, pr->handle);
        if(pr->shouldflush)
        {
            fflush(pr->handle);
        }
    }
    return true;
}

bool nn_printer_writeescapedchar(NNPrinter* pr, int ch)
{
    switch(ch)
    {
        case '\'':
            {
                nn_printer_writestring(pr, "\\\'");
            }
            break;
        case '\"':
            {
                nn_printer_writestring(pr, "\\\"");
            }
            break;
        case '\\':
            {
                nn_printer_writestring(pr, "\\\\");
            }
            break;
        case '\b':
            {
                nn_printer_writestring(pr, "\\b");
            }
            break;
        case '\f':
            {
                nn_printer_writestring(pr, "\\f");
            }
            break;
        case '\n':
            {
                nn_printer_writestring(pr, "\\n");
            }
            break;
        case '\r':
            {
                nn_printer_writestring(pr, "\\r");
            }
            break;
        case '\t':
            {
                nn_printer_writestring(pr, "\\t");
            }
            break;
        case 0:
            {
                nn_printer_writestring(pr, "\\0");
            }
            break;
        default:
            {
                nn_printer_printf(pr, "\\x%02x", (unsigned char)ch);
            }
            break;
    }
    return true;
}

bool nn_printer_writequotedstring(NNPrinter* pr, const char* str, size_t len, bool withquot)
{
    int bch;
    size_t i;
    bch = 0;
    if(withquot)
    {
        nn_printer_writechar(pr, '"');
    }
    for(i = 0; i < len; i++)
    {
        bch = str[i];
        if((bch < 32) || (bch > 127) || (bch == '\"') || (bch == '\\'))
        {
            nn_printer_writeescapedchar(pr, bch);
        }
        else
        {
            nn_printer_writechar(pr, bch);
        }
    }
    if(withquot)
    {
        nn_printer_writechar(pr, '"');
    }
    return true;
}

bool nn_printer_vwritefmttostring(NNPrinter* pr, const char* fmt, va_list va)
{
    #if 0
        size_t wsz;
        size_t needed;
        char* buf;
        va_list copy;
        va_copy(copy, va);
        needed = 1 + vsnprintf(NULL, 0, fmt, copy);
        va_end(copy);
        buf = (char*)nn_memory_malloc(sizeof(char) * (needed + 1));
        if(!buf)
        {
            return false;
        }
        memset(buf, 0, needed + 1);
        wsz = vsnprintf(buf, needed, fmt, va);
        nn_printer_writestringl(pr, buf, wsz);
        nn_memory_free(buf);
    #else
        dyn_strbuf_appendformatv(&pr->strbuf, fmt, va);
    #endif
    return true;
}

bool nn_printer_vwritefmt(NNPrinter* pr, const char* fmt, va_list va)
{
    if(pr->wrmode == NEON_PRMODE_STRING)
    {
        return nn_printer_vwritefmttostring(pr, fmt, va);
    }
    else if(pr->wrmode == NEON_PRMODE_FILE)
    {
        vfprintf(pr->handle, fmt, va);
        if(pr->shouldflush)
        {
            fflush(pr->handle);
        }
    }
    return true;
}

bool nn_printer_printf(NNPrinter* pr, const char* fmt, ...) NEON_ATTR_PRINTFLIKE(2, 3);

bool nn_printer_printf(NNPrinter* pr, const char* fmt, ...)
{
    bool b;
    va_list va;
    va_start(va, fmt);
    b = nn_printer_vwritefmt(pr, fmt, va);
    va_end(va);
    return b;
}

void nn_printer_printfunction(NNPrinter* pr, NNObjFunction* func)
{
    if(func->name == NULL)
    {
        nn_printer_printf(pr, "<script at %p>", (void*)func);
    }
    else
    {
        if(func->fnscriptfunc.isvariadic)
        {
            nn_printer_printf(pr, "<function %s(%d...) at %p>", func->name->sbuf.data, func->fnscriptfunc.arity, (void*)func);
        }
        else
        {
            nn_printer_printf(pr, "<function %s(%d) at %p>", func->name->sbuf.data, func->fnscriptfunc.arity, (void*)func);
        }
    }
}

void nn_printer_printarray(NNPrinter* pr, NNObjArray* list)
{
    size_t i;
    size_t vsz;
    bool isrecur;
    NNValue val;
    NNObjArray* subarr;
    vsz = nn_valarray_count(&list->varray);
    nn_printer_printf(pr, "[");
    for(i = 0; i < vsz; i++)
    {
        isrecur = false;
        val = nn_valarray_get(&list->varray, i);
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
            nn_printer_printf(pr, "<recursion>");
        }
        else
        {
            nn_printer_printvalue(pr, val, true, true);
        }
        if(i != vsz - 1)
        {
            nn_printer_printf(pr, ",");
        }
        if(pr->shortenvalues && (i >= pr->maxvallength))
        {
            nn_printer_printf(pr, " [%ld items]", vsz);
            break;
        }
    }
    nn_printer_printf(pr, "]");
}

void nn_printer_printdict(NNPrinter* pr, NNObjDict* dict)
{
    size_t i;
    size_t dsz;
    bool keyisrecur;
    bool valisrecur;
    NNValue val;
    NNObjDict* subdict;
    NNProperty* field;
    dsz = nn_valarray_count(&dict->names);
    nn_printer_printf(pr, "{");
    for(i = 0; i < dsz; i++)
    {
        valisrecur = false;
        keyisrecur = false;
        val = nn_valarray_get(&dict->names, i);
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
            nn_printer_printf(pr, "<recursion>");
        }
        else
        {
            nn_printer_printvalue(pr, val, true, true);
        }
        nn_printer_printf(pr, ": ");
        field = nn_valtable_getfield(&dict->htab, nn_valarray_get(&dict->names, i));
        if(field != NULL)
        {
            if(nn_value_isdict(field->value))
            {
                subdict = nn_value_asdict(field->value);
                if(subdict == dict)
                {
                    keyisrecur = true;
                }
            }
            if(keyisrecur)
            {
                nn_printer_printf(pr, "<recursion>");
            }
            else
            {
                nn_printer_printvalue(pr, field->value, true, true);
            }
        }
        if(i != dsz - 1)
        {
            nn_printer_printf(pr, ", ");
        }
        if(pr->shortenvalues && (pr->maxvallength >= i))
        {
            nn_printer_printf(pr, " [%ld items]", dsz);
            break;
        }
    }
    nn_printer_printf(pr, "}");
}

void nn_printer_printfile(NNPrinter* pr, NNObjFile* file)
{
    nn_printer_printf(pr, "<file at %s in mode %s>", file->path->sbuf.data, file->mode->sbuf.data);
}

void nn_printer_printinstance(NNPrinter* pr, NNObjInstance* instance, bool invmethod)
{
    (void)invmethod;
    #if 0
    int arity;
    NNPrinter subw;
    NNValue resv;
    NNValue thisval;
    NNProperty* field;
    NNState* state;
    NNObjString* os;
    NNObjArray* args;
    state = pr->pstate;
    if(invmethod)
    {
        field = nn_valtable_getfieldbycstr(&instance->klass->instmethods, "toString");
        if(field != NULL)
        {
            args = nn_object_makearray(state);
            thisval = nn_value_fromobject(instance);
            arity = nn_nestcall_prepare(state, field->value, thisval, NULL, 0);
            fprintf(stderr, "arity = %d\n", arity);
            nn_vm_stackpop(state);
            nn_vm_stackpush(state, thisval);
            if(nn_nestcall_callfunction(state, field->value, thisval, NULL, 0, &resv))
            {
                nn_printer_makestackstring(state, &subw);
                nn_printer_printvalue(&subw, resv, false, false);
                os = nn_printer_takestring(&subw);
                nn_printer_writestringl(pr, os->sbuf.data, os->sbuf.length);
                #if 0
                    nn_vm_stackpop(state);
                #endif
                return;
            }
        }
    }
    #endif
    nn_printer_printf(pr, "<instance of %s at %p>", instance->klass->name->sbuf.data, (void*)instance);
}

void nn_printer_printtable(NNPrinter* pr, NNHashValTable* table)
{
    size_t i;
    NNHashValEntry* entry;
    nn_printer_printf(pr, "{");
    for(i = 0; i < (size_t)table->capacity; i++)
    {
        entry = &table->entries[i];
        if(nn_value_isnull(entry->key))
        {
            continue;
        }
        nn_printer_printvalue(pr, entry->key, true, false);
        nn_printer_printf(pr, ":");
        nn_printer_printvalue(pr, entry->value.value, true, false);
        if(!nn_value_isnull(table->entries[i+1].key))
        {
            if((i+1) < (size_t)table->capacity)
            {
                nn_printer_printf(pr, ",");
            }
        }
    }
    nn_printer_printf(pr, "}");
}

void nn_printer_printobjclass(NNPrinter* pr, NNValue value, bool fixstring, bool invmethod)
{
    bool oldexp;
    NNObjClass* klass;
    (void)fixstring;
    (void)invmethod;
    klass = nn_value_asclass(value);
    if(pr->jsonmode)
    {
        nn_printer_printf(pr, "{");
        {
            {
                nn_printer_printf(pr, "name: ");
                nn_printer_printvalue(pr, nn_value_fromobject(klass->name), true, false);
                nn_printer_printf(pr, ",");
            }
            {
                nn_printer_printf(pr, "superclass: ");
                oldexp = pr->jsonmode;
                pr->jsonmode = false;
                nn_printer_printvalue(pr, nn_value_fromobject(klass->superclass), true, false);
                pr->jsonmode = oldexp;
                nn_printer_printf(pr, ",");
            }
            {
                nn_printer_printf(pr, "constructor: ");
                nn_printer_printvalue(pr, klass->constructor, true, false);
                nn_printer_printf(pr, ",");
            }
            {
                nn_printer_printf(pr, "instanceproperties:");
                nn_printer_printtable(pr, &klass->instproperties);
                nn_printer_printf(pr, ",");
            }
            {
                nn_printer_printf(pr, "staticproperties:");
                nn_printer_printtable(pr, &klass->staticproperties);
                nn_printer_printf(pr, ",");
            }
            {
                nn_printer_printf(pr, "instancemethods:");
                nn_printer_printtable(pr, &klass->instmethods);
                nn_printer_printf(pr, ",");
            }
            {
                nn_printer_printf(pr, "staticmethods:");
                nn_printer_printtable(pr, &klass->staticmethods);
            }
        }
        nn_printer_printf(pr, "}");
    }
    else
    {
        nn_printer_printf(pr, "<class %s at %p>", klass->name->sbuf.data, (void*)klass);
    }
}

void nn_printer_printobject(NNPrinter* pr, NNValue value, bool fixstring, bool invmethod)
{
    NNObject* obj;
    obj = nn_value_asobject(value);
    switch(obj->type)
    {
        case NEON_OBJTYPE_SWITCH:
            {
                nn_printer_writestring(pr, "<switch>");
            }
            break;
        case NEON_OBJTYPE_USERDATA:
            {
                nn_printer_printf(pr, "<userdata %s>", nn_value_asuserdata(value)->name);
            }
            break;
        case NEON_OBJTYPE_RANGE:
            {
                NNObjRange* range;
                range = nn_value_asrange(value);
                nn_printer_printf(pr, "<range %d .. %d>", range->lower, range->upper);
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                nn_printer_printfile(pr, nn_value_asfile(value));
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                nn_printer_printdict(pr, nn_value_asdict(value));
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                nn_printer_printarray(pr, nn_value_asarray(value));
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
            {
                NNObjFunction* bn;
                bn = nn_value_asfunction(value);
                nn_printer_printfunction(pr, bn->fnmethod.method->fnclosure.scriptfunc);
            }
            break;
        case NEON_OBJTYPE_MODULE:
            {
                NNObjModule* mod;
                mod = nn_value_asmodule(value);
                nn_printer_printf(pr, "<module '%s' at '%s'>", mod->name->sbuf.data, mod->physicalpath->sbuf.data);
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                nn_printer_printobjclass(pr, value, fixstring, invmethod);
            }
            break;
        case NEON_OBJTYPE_FUNCCLOSURE:
            {
                NNObjFunction* cls;
                cls = nn_value_asfunction(value);
                nn_printer_printfunction(pr, cls->fnclosure.scriptfunc);
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                NNObjFunction* fn;
                fn = nn_value_asfunction(value);
                nn_printer_printfunction(pr, fn);
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                /* @TODO: support the toString() override */
                NNObjInstance* instance;
                instance = nn_value_asinstance(value);
                nn_printer_printinstance(pr, instance, invmethod);
            }
            break;
        case NEON_OBJTYPE_FUNCNATIVE:
            {
                NNObjFunction* native;
                native = nn_value_asfunction(value);
                nn_printer_printf(pr, "<function %s(native) at %p>", native->name->sbuf.data, (void*)native);
            }
            break;
        case NEON_OBJTYPE_UPVALUE:
            {
                nn_printer_printf(pr, "<upvalue>");
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                NNObjString* string;
                string = nn_value_asstring(value);
                if(fixstring)
                {
                    nn_printer_writequotedstring(pr, string->sbuf.data, string->sbuf.length, true);
                }
                else
                {
                    nn_printer_writestringl(pr, string->sbuf.data, string->sbuf.length);
                }
            }
            break;
    }
}

void nn_printer_printnumber(NNPrinter* pr, NNValue value)
{
    nn_printer_printf(pr, "%.16g", nn_value_asnumber(value));
}

void nn_printer_printvalue(NNPrinter* pr, NNValue value, bool fixstring, bool invmethod)
{
    if(nn_value_isnull(value))
    {
        nn_printer_writestring(pr, "null");
    }
    else if(nn_value_isbool(value))
    {
        nn_printer_writestring(pr, nn_value_asbool(value) ? "true" : "false");
    }
    else if(nn_value_isnumber(value))
    {
        nn_printer_printnumber(pr, value);
    }
    else
    {
        nn_printer_printobject(pr, value, fixstring, invmethod);
    }
}

NNObjString* nn_value_tostring(NNState* state, NNValue value)
{
    NNPrinter pr;
    NNObjString* s;
    nn_printer_makestackstring(state, &pr);
    nn_printer_printvalue(&pr, value, false, true);
    s = nn_printer_takestring(&pr);
    nn_printer_destroy(&pr);
    return s;
}

const char* nn_value_objecttypename(NNObject* object)
{
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
            return ((NNObjInstance*)object)->klass->name->sbuf.data;
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

const char* nn_value_typename(NNValue value)
{
    if(nn_value_isnull(value))
    {
        return "empty";
    }
    if(nn_value_isnull(value))
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
        return nn_value_objecttypename(nn_value_asobject(value));
    }
    return "?unknown?";
}

const char* nn_value_typefromfunction(NNValIsFuncFN func)
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


bool nn_value_compobject(NNState* state, NNValue a, NNValue b)
{
    size_t i;
    NNObjType ta;
    NNObjType tb;
    NNObject* oa;
    NNObject* ob;
    NNObjString* stra;
    NNObjString* strb;
    NNObjArray* arra;
    NNObjArray* arrb;
    oa = nn_value_asobject(a);
    ob = nn_value_asobject(b);
    ta = oa->type;
    tb = ob->type;
    if(ta == tb)
    {
        if(ta == NEON_OBJTYPE_STRING)
        {
            stra = (NNObjString*)oa;
            strb = (NNObjString*)ob;
            if(stra->sbuf.length == strb->sbuf.length)
            {
                if(memcmp(stra->sbuf.data, strb->sbuf.data, stra->sbuf.length) == 0)
                {
                    return true;
                }
                return false;
            }
        }
        if(ta == NEON_OBJTYPE_ARRAY)
        {
            arra = (NNObjArray*)oa;
            arrb = (NNObjArray*)ob;
            if(nn_valarray_count(&arra->varray) == nn_valarray_count(&arrb->varray))
            {
                for(i=0; i<(size_t)nn_valarray_count(&arra->varray); i++)
                {
                    if(!nn_value_compare(state, nn_valarray_get(&arra->varray, i), nn_valarray_get(&arrb->varray, i)))
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

bool nn_value_compare_actual(NNState* state, NNValue a, NNValue b)
{
    /*
    if(a.type != b.type)
    {
        return false;
    }
    */
    if(nn_value_isnull(a))
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
            return nn_value_compobject(state, a, b);
        }
    }

    return false;
}


bool nn_value_compare(NNState* state, NNValue a, NNValue b)
{
    bool r;
    r = nn_value_compare_actual(state, a, b);
    return r;
}

uint32_t nn_util_hashbits(uint64_t hs)
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

uint32_t nn_util_hashdouble(double value)
{
    NNUtilDblUnion bits;
    bits.num = value;
    return nn_util_hashbits(bits.bits);
}

uint32_t nn_util_hashstring(const char *str, size_t length)
{
    // Source: https://stackoverflow.com/a/21001712
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
                tmpa = nn_util_hashdouble(fn->fnscriptfunc.arity);
                tmpb = nn_util_hashdouble(fn->fnscriptfunc.blob.count);
                tmpres = tmpa ^ tmpb;
                tmpres = tmpres ^ tmpptr;
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
    else if(nn_value_isnull(value))
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

/**
 * returns the greater of the two values.
 * this function encapsulates the object hierarchy
 */
NNValue nn_value_findgreater(NNValue a, NNValue b)
{
    NNObjString* osa;
    NNObjString* osb;    
    if(nn_value_isnull(a))
    {
        return b;
    }
    else if(nn_value_isbool(a))
    {
        if(nn_value_isnull(b) || (nn_value_isbool(b) && nn_value_asbool(b) == false))
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
        if(nn_value_isnull(b) || nn_value_isbool(b))
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
            if(strncmp(osa->sbuf.data, osb->sbuf.data, osa->sbuf.length) >= 0)
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
            if(nn_value_asclass(a)->instmethods.count >= nn_value_asclass(b)->instmethods.count)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isarray(a) && nn_value_isarray(b))
        {
            if(nn_valarray_count(&nn_value_asarray(a)->varray) >= nn_valarray_count(&nn_value_asarray(b)->varray))
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isdict(a) && nn_value_isdict(b))
        {
            if(nn_valarray_count(&nn_value_asdict(a)->names) >= nn_valarray_count(&nn_value_asdict(b)->names))
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isfile(a) && nn_value_isfile(b))
        {
            if(strcmp(nn_value_asfile(a)->path->sbuf.data, nn_value_asfile(b)->path->sbuf.data) >= 0)
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
            if(nn_value_compare(state, values[j], nn_value_findgreater(values[i], values[j])))
            {
                temp = values[i];
                values[i] = values[j];
                values[j] = temp;
                if(nn_value_isarray(values[i]))
                {
                    nn_value_sortvalues(state, nn_value_asarray(values[i])->varray.listitems, nn_valarray_count(&nn_value_asarray(values[i])->varray));
                }

                if(nn_value_isarray(values[j]))
                {
                    nn_value_sortvalues(state, nn_value_asarray(values[j])->varray.listitems, nn_valarray_count(&nn_value_asarray(values[j])->varray));
                }
            }
        }
    }
}

NNValue nn_value_copyvalue(NNState* state, NNValue value)
{
    if(nn_value_isobject(value))
    {
        switch(nn_value_asobject(value)->type)
        {
            case NEON_OBJTYPE_STRING:
                {
                    NNObjString* string;
                    string = nn_value_asstring(value);
                    return nn_value_fromobject(nn_string_copyobject(state, string));
                }
                break;
            case NEON_OBJTYPE_ARRAY:
                {
                    size_t i;
                    NNObjArray* list;
                    NNObjArray* newlist;
                    list = nn_value_asarray(value);
                    newlist = nn_object_makearray(state);
                    nn_vm_stackpush(state, nn_value_fromobject(newlist));
                    for(i = 0; i < nn_valarray_count(&list->varray); i++)
                    {
                        nn_valarray_push(&newlist->varray, nn_valarray_get(&list->varray, i));
                    }
                    nn_vm_stackpop(state);
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

NNObject* nn_object_allocobject(NNState* state, size_t size, NNObjType type, bool retain)
{
    NNObject* object;
    object = (NNObject*)nn_gcmem_allocate(state, size, 1, retain);
    object->type = type;
    object->mark = !state->markvalue;
    object->stale = false;
    object->pstate = state;
    object->next = state->vmstate.linkedobjects;
    state->vmstate.linkedobjects = object;
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_printer_printf(state->debugwriter, "%p allocate %ld for %d\n", (void*)object, size, type);
    #endif
    return object;
}

NNObjUserdata* nn_object_makeuserdata(NNState* state, void* pointer, const char* name)
{
    NNObjUserdata* ptr;
    ptr = (NNObjUserdata*)nn_object_allocobject(state, sizeof(NNObjUserdata), NEON_OBJTYPE_USERDATA, false);
    ptr->pointer = pointer;
    ptr->name = nn_util_strdup(name);
    ptr->ondestroyfn = NULL;
    return ptr;
}

NNObjModule* nn_module_make(NNState* state, const char* name, const char* file, bool imported, bool retain)
{
    NNObjModule* module;
    module = (NNObjModule*)nn_object_allocobject(state, sizeof(NNObjModule), NEON_OBJTYPE_MODULE, retain);
    nn_valtable_init(state, &module->deftable);
    module->name = nn_string_copycstr(state, name);
    module->physicalpath = nn_string_copycstr(state, file);
    module->unloader = NULL;
    module->preloader = NULL;
    module->handle = NULL;
    module->imported = imported;
    return module;
}

void nn_module_destroy(NNState* state, NNObjModule* module)
{
    nn_valtable_destroy(&module->deftable);
    /*
    nn_memory_free(module->name);
    nn_memory_free(module->physicalpath);
    */
    if(module->unloader != NULL && module->imported)
    {
        ((NNModLoaderFN)module->unloader)(state);
    }
    if(module->handle != NULL)
    {
        nn_import_closemodule(module->handle);
    }
}

void nn_module_setfilefield(NNState* state, NNObjModule* module)
{
    return;
    nn_valtable_set(&module->deftable, nn_value_fromobject(nn_string_copycstr(state, "__file__")), nn_value_fromobject(nn_string_copyobject(state, module->physicalpath)));
}

NNObjSwitch* nn_object_makeswitch(NNState* state)
{
    NNObjSwitch* sw;
    sw = (NNObjSwitch*)nn_object_allocobject(state, sizeof(NNObjSwitch), NEON_OBJTYPE_SWITCH, false);
    nn_valtable_init(state, &sw->table);
    sw->defaultjump = -1;
    sw->exitjump = -1;
    return sw;
}

NNObjArray* nn_object_makearray(NNState* state)
{
    return nn_array_make(state);
}

NNObjRange* nn_object_makerange(NNState* state, int lower, int upper)
{
    NNObjRange* range;
    range = (NNObjRange*)nn_object_allocobject(state, sizeof(NNObjRange), NEON_OBJTYPE_RANGE, false);
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

NNObjDict* nn_object_makedict(NNState* state)
{
    NNObjDict* dict;
    dict = (NNObjDict*)nn_object_allocobject(state, sizeof(NNObjDict), NEON_OBJTYPE_DICT, false);
    nn_valarray_init(state, &dict->names);
    nn_valtable_init(state, &dict->htab);
    return dict;
}

NNObjFile* nn_object_makefile(NNState* state, FILE* handle, bool isstd, const char* path, const char* mode)
{
    NNObjFile* file;
    file = (NNObjFile*)nn_object_allocobject(state, sizeof(NNObjFile), NEON_OBJTYPE_FILE, false);
    file->isopen = false;
    file->mode = nn_string_copycstr(state, mode);
    file->path = nn_string_copycstr(state, path);
    file->isstd = isstd;
    file->handle = handle;
    file->istty = false;
    file->number = -1;
    if(file->handle != NULL)
    {
        file->isopen = true;
    }
    return file;
}

void nn_file_destroy(NNObjFile* file)
{
    NNState* state;
    state = ((NNObject*)file)->pstate;
    nn_fileobject_close(file);
    nn_gcmem_release(state, file, sizeof(NNObjFile));
}

void nn_file_mark(NNObjFile* file)
{
    NNState* state;
    state = ((NNObject*)file)->pstate;
    nn_gcmem_markobject(state, (NNObject*)file->mode);
    nn_gcmem_markobject(state, (NNObject*)file->path);
}

bool nn_file_read(NNObjFile* file, size_t readhowmuch, NNIOResult* dest)
{
    NNState* state;
    size_t filesizereal;
    struct stat stats;
    state = ((NNObject*)file)->pstate;
    filesizereal = -1;
    dest->success = false;
    dest->length = 0;
    dest->data = NULL;
    if(!file->isstd)
    {
        if(!nn_util_fsfileexists(state, file->path->sbuf.data))
        {
            return false;
        }
        /* file is in write only mode */
        /*
        else if(strstr(file->mode->sbuf.data, "w") != NULL && strstr(file->mode->sbuf.data, "+") == NULL)
        {
            FILE_ERROR(Unsupported, "cannot read file in write mode");
        }
        */
        if(!file->isopen)
        {
            /* open the file if it isn't open */
            nn_fileobject_open(file);
        }
        else if(file->handle == NULL)
        {
            return false;
        }
        if(osfn_lstat(file->path->sbuf.data, &stats) == 0)
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
    if(dest->data == NULL && readhowmuch != 0)
    {
        return false;
    }
    dest->length = fread(dest->data, sizeof(char), readhowmuch, file->handle);
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

NNObjFunction* nn_object_makefuncbound(NNState* state, NNValue receiver, NNObjFunction* method)
{
    NNObjFunction* bound;
    bound = (NNObjFunction*)nn_object_allocobject(state, sizeof(NNObjFunction), NEON_OBJTYPE_FUNCBOUND, false);
    bound->fnmethod.receiver = receiver;
    bound->fnmethod.method = method;
    return bound;
}

NNObjClass* nn_object_makeclass(NNState* state, NNObjString* name, NNObjClass* parent)
{
    NNObjClass* klass;
    klass = (NNObjClass*)nn_object_allocobject(state, sizeof(NNObjClass), NEON_OBJTYPE_CLASS, false);
    klass->name = name;
    nn_valtable_init(state, &klass->instproperties);
    nn_valtable_init(state, &klass->staticproperties);
    nn_valtable_init(state, &klass->instmethods);
    nn_valtable_init(state, &klass->staticmethods);
    klass->constructor = nn_value_makenull();
    klass->destructor = nn_value_makenull();
    klass->superclass = parent;
    return klass;
}

void nn_class_destroy(NNObjClass* klass)
{
    NNState* state;
    state = ((NNObject*)klass)->pstate;
    nn_valtable_destroy(&klass->instmethods);
    nn_valtable_destroy(&klass->staticmethods);
    nn_valtable_destroy(&klass->instproperties);
    nn_valtable_destroy(&klass->staticproperties);
    /*
    // We are not freeing the initializer because it's a closure and will still be freed accordingly later.
    */
    memset(klass, 0, sizeof(NNObjClass));
    nn_gcmem_release(state, klass, sizeof(NNObjClass));   
}

bool nn_class_inheritfrom(NNObjClass* subclass, NNObjClass* superclass)
{
    int failcnt;
    failcnt = 0;
    if(!nn_valtable_addall(&superclass->instproperties, &subclass->instproperties, true))
    {
        failcnt++;
    }
    if(!nn_valtable_addall(&superclass->instmethods, &subclass->instmethods, true))
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
    return nn_valtable_set(&klass->instproperties, nn_value_fromobject(cstrname), val);
}

bool nn_class_defcallablefieldptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr)
{
    NNState* state;
    NNObjFunction* ofn;
    state = ((NNObject*)klass)->pstate;
    ofn = nn_object_makefuncnative(state, function, name->sbuf.data, uptr);
    return nn_valtable_setwithtype(&klass->instproperties, nn_value_fromobject(name), nn_value_fromobject(ofn), NEON_PROPTYPE_FUNCTION, true);
}

bool nn_class_defcallablefield(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defcallablefieldptr(klass, name, function, NULL);
}

bool nn_class_defstaticcallablefieldptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr)
{
    NNState* state;
    NNObjFunction* ofn;
    state = ((NNObject*)klass)->pstate;
    ofn = nn_object_makefuncnative(state, function, name->sbuf.data, uptr);
    return nn_valtable_setwithtype(&klass->staticproperties, nn_value_fromobject(name), nn_value_fromobject(ofn), NEON_PROPTYPE_FUNCTION, true);
}

bool nn_class_defstaticcallablefield(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defstaticcallablefieldptr(klass, name, function, NULL);
}

bool nn_class_setstaticproperty(NNObjClass* klass, NNObjString* name, NNValue val)
{
    return nn_valtable_set(&klass->staticproperties, nn_value_fromobject(name), val);
}

bool nn_class_defnativeconstructorptr(NNObjClass* klass, NNNativeFN function, void* uptr)
{
    const char* cname;
    NNState* state;
    NNObjFunction* ofn;
    state = ((NNObject*)klass)->pstate;
    cname = "constructor";
    ofn = nn_object_makefuncnative(state, function, cname, uptr);
    klass->constructor = nn_value_fromobject(ofn);
    return true;
}

bool nn_class_defnativeconstructor(NNObjClass* klass, NNNativeFN function)
{
    return nn_class_defnativeconstructorptr(klass, function, NULL);
}

bool nn_class_defmethod(NNObjClass* klass, NNObjString* name, NNValue val)
{
    return nn_valtable_set(&klass->instmethods, nn_value_fromobject(name), val);
}

bool nn_class_defnativemethodptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* ptr)
{
    NNObjFunction* ofn;
    NNState* state;
    state = ((NNObject*)klass)->pstate;
    ofn = nn_object_makefuncnative(state, function, name->sbuf.data, ptr);
    return nn_class_defmethod(klass, name, nn_value_fromobject(ofn));
}

bool nn_class_defnativemethod(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defnativemethodptr(klass, name, function, NULL);
}

bool nn_class_defstaticnativemethodptr(NNObjClass* klass, NNObjString* name, NNNativeFN function, void* uptr)
{
    NNState* state;
    NNObjFunction* ofn;
    state = ((NNObject*)klass)->pstate;
    ofn = nn_object_makefuncnative(state, function, name->sbuf.data, uptr);
    return nn_valtable_set(&klass->staticmethods, nn_value_fromobject(name), nn_value_fromobject(ofn));
}

bool nn_class_defstaticnativemethod(NNObjClass* klass, NNObjString* name, NNNativeFN function)
{
    return nn_class_defstaticnativemethodptr(klass, name, function, NULL);
}

NNProperty* nn_class_getmethodfield(NNObjClass* klass, NNObjString* name)
{
    NNProperty* field;
    field = nn_valtable_getfield(&klass->instmethods, nn_value_fromobject(name));
    if(field != NULL)
    {
        return field;
    }
    if(klass->superclass != NULL)
    {
        return nn_class_getmethodfield(klass->superclass, name);
    }
    return NULL;
}

NNProperty* nn_class_getpropertyfield(NNObjClass* klass, NNObjString* name)
{
    NNProperty* field;
    field = nn_valtable_getfield(&klass->instproperties, nn_value_fromobject(name));
    return field;
}

NNProperty* nn_class_getstaticproperty(NNObjClass* klass, NNObjString* name)
{
    NNProperty* np;
    np = nn_valtable_getfieldbyostr(&klass->staticproperties, name);
    if(np != NULL)
    {
        return np;
    }
    if(klass->superclass != NULL)
    {
        return nn_class_getstaticproperty(klass->superclass, name);
    }
    return NULL;
}

NNProperty* nn_class_getstaticmethodfield(NNObjClass* klass, NNObjString* name)
{
    NNProperty* field;
    field = nn_valtable_getfield(&klass->staticmethods, nn_value_fromobject(name));
    return field;
}

NNObjInstance* nn_object_makeinstance(NNState* state, NNObjClass* klass)
{
    NNObjInstance* instance;
    instance = (NNObjInstance*)nn_object_allocobject(state, sizeof(NNObjInstance), NEON_OBJTYPE_INSTANCE, false);
    /* gc fix */
    nn_vm_stackpush(state, nn_value_fromobject(instance));
    instance->active = true;
    instance->klass = klass;
    nn_valtable_init(state, &instance->properties);
    if(klass->instproperties.count > 0)
    {
        nn_valtable_copy(&klass->instproperties, &instance->properties);
    }
    /* gc fix */
    nn_vm_stackpop(state);
    return instance;
}

void nn_instance_mark(NNObjInstance* instance)
{
    NNState* state;
    state = ((NNObject*)instance)->pstate;
    if(instance->active == false)
    {
        nn_state_warn(state, "trying to mark inactive instance <%p>!", instance);
        return;
    }
    nn_valtable_mark(state, &instance->properties);
    nn_gcmem_markobject(state, (NNObject*)instance->klass);
}

void nn_instance_destroy(NNObjInstance* instance)
{
    NNState* state;
    state = ((NNObject*)instance)->pstate;
    if(!nn_value_isnull(instance->klass->destructor))
    {
        if(!nn_vm_callvaluewithobject(state, instance->klass->constructor, nn_value_fromobject(instance), 0))
        {
            
        }
    }
    nn_valtable_destroy(&instance->properties);
    instance->active = false;
    nn_gcmem_release(state, instance, sizeof(NNObjInstance));
}

bool nn_instance_defproperty(NNObjInstance* instance, NNObjString* name, NNValue val)
{
    return nn_valtable_set(&instance->properties, nn_value_fromobject(name), val);
}

NNObjFunction* nn_object_makefuncscript(NNState* state, NNObjModule* module, NNFuncContextType type)
{
    NNObjFunction* function;
    function = (NNObjFunction*)nn_object_allocobject(state, sizeof(NNObjFunction), NEON_OBJTYPE_FUNCSCRIPT, false);
    function->fnscriptfunc.arity = 0;
    function->upvalcount = 0;
    function->fnscriptfunc.isvariadic = false;
    function->name = NULL;
    function->contexttype = type;
    function->fnscriptfunc.module = module;
    #if 0
        nn_valarray_init(state, &function->funcargdefaults);
    #endif
    nn_blob_init(state, &function->fnscriptfunc.blob);
    return function;
}

void nn_funcscript_destroy(NNObjFunction* function)
{
    nn_blob_destroy(&function->fnscriptfunc.blob);
}

NNObjFunction* nn_object_makefuncnative(NNState* state, NNNativeFN function, const char* name, void* uptr)
{
    NNObjFunction* native;
    native = (NNObjFunction*)nn_object_allocobject(state, sizeof(NNObjFunction), NEON_OBJTYPE_FUNCNATIVE, false);
    native->fnnativefunc.natfunc = function;
    native->name = nn_string_copycstr(state, name);
    native->contexttype = NEON_FNCONTEXTTYPE_FUNCTION;
    native->fnnativefunc.userptr = uptr;
    return native;
}

NNObjFunction* nn_object_makefuncclosure(NNState* state, NNObjFunction* function)
{
    int i;
    NNObjUpvalue** upvals;
    NNObjFunction* closure;
    upvals = NULL;
    if(function->upvalcount > 0)
    {
        upvals = (NNObjUpvalue**)nn_gcmem_allocate(state, sizeof(NNObjUpvalue*), function->upvalcount + 1, false);
        for(i = 0; i < function->upvalcount; i++)
        {
            upvals[i] = NULL;
        }
    }
    closure = (NNObjFunction*)nn_object_allocobject(state, sizeof(NNObjFunction), NEON_OBJTYPE_FUNCCLOSURE, false);
    closure->fnclosure.scriptfunc = function;
    closure->fnclosure.upvalues = upvals;
    closure->upvalcount = function->upvalcount;
    return closure;
}

NNObjString* nn_string_makefromstrbuf(NNState* state, StringBuffer* sbuf, uint32_t hsv)
{
    NNObjString* rs;
    rs = (NNObjString*)nn_object_allocobject(state, sizeof(NNObjString), NEON_OBJTYPE_STRING, false);
    rs->sbuf = *sbuf;
    rs->hashvalue = hsv;
    nn_vm_stackpush(state, nn_value_fromobject(rs));
    nn_valtable_set(&state->allocatedstrings, nn_value_fromobject(rs), nn_value_makenull());
    nn_vm_stackpop(state);
    return rs;
}

size_t nn_string_getlength(NNObjString* os)
{
    return os->sbuf.length;
}

const char* nn_string_getdata(NNObjString* os)
{
    return os->sbuf.data;
}

const char* nn_string_getcstr(NNObjString* os)
{
    return nn_string_getdata(os);
}

void nn_string_destroy(NNState* state, NNObjString* str)
{
    dyn_strbuf_destroyfromstack(&str->sbuf);
    nn_gcmem_release(state, str, sizeof(NNObjString));
}

NNObjString* nn_string_internlen(NNState* state, const char* chars, int length)
{
    uint32_t hsv;
    StringBuffer sbuf;
    hsv = nn_util_hashstring(chars, length);
    memset(&sbuf, 0, sizeof(StringBuffer));
    sbuf.data = (char*)chars;
    sbuf.length = length;
    sbuf.isintern = true;
    return nn_string_makefromstrbuf(state, &sbuf, hsv);
}

NNObjString* nn_string_intern(NNState* state, const char* chars)
{
    return nn_string_internlen(state, chars, strlen(chars));
}

NNObjString* nn_string_takelen(NNState* state, char* chars, int length)
{
    uint32_t hsv;
    NNObjString* rs;
    StringBuffer sbuf;
    hsv = nn_util_hashstring(chars, length);
    rs = nn_valtable_findstring(&state->allocatedstrings, chars, length, hsv);
    if(rs != NULL)
    {
        nn_memory_free(chars);
        return rs;
    }
    memset(&sbuf, 0, sizeof(StringBuffer));
    sbuf.data = chars;
    sbuf.length = length;
    return nn_string_makefromstrbuf(state, &sbuf, hsv);
}

NNObjString* nn_string_takecstr(NNState* state, char* chars)
{
    return nn_string_takelen(state, chars, strlen(chars));
}

NNObjString* nn_string_copylen(NNState* state, const char* chars, int length)
{
    uint32_t hsv;
    StringBuffer sbuf;
    NNObjString* rs;
    hsv = nn_util_hashstring(chars, length);
    rs = nn_valtable_findstring(&state->allocatedstrings, chars, length, hsv);
    if(rs != NULL)
    {
        return rs;
    }
    memset(&sbuf, 0, sizeof(StringBuffer));
    dyn_strbuf_makebasicemptystack(&sbuf, length);
    dyn_strbuf_appendstrn(&sbuf, chars, length);
    rs = nn_string_makefromstrbuf(state, &sbuf, hsv);
    return rs;
}

NNObjString* nn_string_copycstr(NNState* state, const char* chars)
{
    return nn_string_copylen(state, chars, strlen(chars));
}

NNObjString* nn_string_copyobject(NNState* state, NNObjString* origos)
{
    return nn_string_copylen(state, origos->sbuf.data, origos->sbuf.length);
}

NNObjUpvalue* nn_object_makeupvalue(NNState* state, NNValue* slot, int stackpos)
{
    NNObjUpvalue* upvalue;
    upvalue = (NNObjUpvalue*)nn_object_allocobject(state, sizeof(NNObjUpvalue), NEON_OBJTYPE_UPVALUE, false);
    upvalue->closed = nn_value_makenull();
    upvalue->location = *slot;
    upvalue->next = NULL;
    upvalue->stackpos = stackpos;
    return upvalue;
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

void nn_astlex_destroy(NNState* state, NNAstLexer* lex)
{
    (void)state;
    if(!lex->onstack)
    {
        nn_memory_free(lex);
    }
}

bool nn_astlex_isatend(NNAstLexer* lex)
{
    return *lex->sourceptr == '\0';
}

NNAstToken nn_astlex_createtoken(NNAstLexer* lex, NNAstTokType type)
{
    NNAstToken t;
    t.isglobal = false;
    t.type = type;
    t.start = lex->start;
    t.length = (int)(lex->sourceptr - lex->start);
    t.line = lex->line;
    return t;
}

NNAstToken nn_astlex_errortoken(NNAstLexer* lex, const char* fmt, ...)
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
    if(buf != NULL)
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

char nn_astlex_advance(NNAstLexer* lex)
{
    lex->sourceptr++;
    if(lex->sourceptr[-1] == '\n')
    {
        lex->line++;
    }
    return lex->sourceptr[-1];
}

bool nn_astlex_match(NNAstLexer* lex, char expected)
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

char nn_astlex_peekcurr(NNAstLexer* lex)
{
    return *lex->sourceptr;
}

char nn_astlex_peekprev(NNAstLexer* lex)
{
    if(lex->sourceptr == lex->start)
    {
        return -1;
    }
    return lex->sourceptr[-1];
}

char nn_astlex_peeknext(NNAstLexer* lex)
{
    if(nn_astlex_isatend(lex))
    {
        return '\0';
    }
    return lex->sourceptr[1];
}

NNAstToken nn_astlex_skipblockcomments(NNAstLexer* lex)
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

NNAstToken nn_astlex_skipspace(NNAstLexer* lex)
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

NNAstTokType nn_astlex_getidenttype(NNAstLexer* lex)
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
        { NULL, (NNAstTokType)0 }
    };
    size_t i;
    size_t kwlen;
    size_t ofs;
    const char* kwtext;
    for(i = 0; keywords[i].str != NULL; i++)
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

NNAstToken nn_astlex_scandecorator(NNAstLexer* lex)
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
    parser->currentfunccompiler = NULL;
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
    parser->currentclasscompiler = NULL;
    parser->currentmodule = module;
    parser->keeplastvalue = keeplast;
    parser->lastwasstatement = false;
    parser->infunction = false;
    parser->inswitch = false;
    parser->currentfile = parser->currentmodule->physicalpath->sbuf.data;
    return parser;
}

void nn_astparser_destroy(NNAstParser* parser)
{
    nn_memory_free(parser);
}

NNBlob* nn_astparser_currentblob(NNAstParser* prs)
{
    return &prs->currentfunccompiler->targetfunc->fnscriptfunc.blob;
}

bool nn_astparser_raiseerroratv(NNAstParser* prs, NNAstToken* t, const char* message, va_list args)
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
    fprintf(stderr, " in [%s:%d]: ", prs->currentmodule->physicalpath->sbuf.data, t->line);
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

bool nn_astparser_raiseerror(NNAstParser* prs, const char* message, ...)
{
    va_list args;
    va_start(args, message);
    nn_astparser_raiseerroratv(prs, &prs->prevtoken, message, args);
    va_end(args);
    return false;
}

bool nn_astparser_raiseerroratcurrent(NNAstParser* prs, const char* message, ...)
{
    va_list args;
    va_start(args, message);
    nn_astparser_raiseerroratv(prs, &prs->currtoken, message, args);
    va_end(args);
    return false;
}

void nn_astparser_advance(NNAstParser* prs)
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


bool nn_astparser_consume(NNAstParser* prs, NNAstTokType t, const char* message)
{
    if(nn_astparser_istype(prs->currtoken.type, t))
    {
        nn_astparser_advance(prs);
        return true;
    }
    return nn_astparser_raiseerroratcurrent(prs, message);
}

void nn_astparser_consumeor(NNAstParser* prs, const char* message, const NNAstTokType* ts, int count)
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

bool nn_astparser_checknumber(NNAstParser* prs)
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

bool nn_astparser_check(NNAstParser* prs, NNAstTokType t)
{
    return nn_astparser_istype(prs->currtoken.type, t);
}

bool nn_astparser_match(NNAstParser* prs, NNAstTokType t)
{
    if(!nn_astparser_check(prs, t))
    {
        return false;
    }
    nn_astparser_advance(prs);
    return true;
}

void nn_astparser_runparser(NNAstParser* parser)
{
    nn_astparser_advance(parser);
    nn_astparser_ignorewhitespace(parser);
    while(!nn_astparser_match(parser, NEON_ASTTOK_EOF))
    {
        nn_astparser_parsedeclaration(parser);
    }
}

void nn_astparser_parsedeclaration(NNAstParser* prs)
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

void nn_astparser_parsestmt(NNAstParser* prs)
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

void nn_astparser_consumestmtend(NNAstParser* prs)
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

void nn_astparser_ignorewhitespace(NNAstParser* prs)
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

int nn_astparser_getcodeargscount(const NNInstruction* bytecode, const NNValue* constants, int ip)
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

void nn_astemit_emit(NNAstParser* prs, uint8_t byte, int line, bool isop)
{
    NNInstruction ins;
    ins.code = byte;
    ins.srcline = line;
    ins.isop = isop;
    nn_blob_push(nn_astparser_currentblob(prs), ins);
}

void nn_astemit_patchat(NNAstParser* prs, size_t idx, uint8_t byte)
{
    nn_astparser_currentblob(prs)->instrucs[idx].code = byte;
}

void nn_astemit_emitinstruc(NNAstParser* prs, uint8_t byte)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, true);
}

void nn_astemit_emit1byte(NNAstParser* prs, uint8_t byte)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
}

void nn_astemit_emit1short(NNAstParser* prs, uint16_t byte)
{
    nn_astemit_emit(prs, (byte >> 8) & 0xff, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte & 0xff, prs->prevtoken.line, false);
}

void nn_astemit_emit2byte(NNAstParser* prs, uint8_t byte, uint8_t byte2)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte2, prs->prevtoken.line, false);
}

void nn_astemit_emitbyteandshort(NNAstParser* prs, uint8_t byte, uint16_t byte2)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
    nn_astemit_emit(prs, (byte2 >> 8) & 0xff, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte2 & 0xff, prs->prevtoken.line, false);
}

void nn_astemit_emitloop(NNAstParser* prs, int loopstart)
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

void nn_astemit_emitreturn(NNAstParser* prs)
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

int nn_astparser_pushconst(NNAstParser* prs, NNValue value)
{
    int constant;
    constant = nn_blob_pushconst(nn_astparser_currentblob(prs), value);
    return constant;
}

void nn_astemit_emitconst(NNAstParser* prs, NNValue value)
{
    int constant;
    constant = nn_astparser_pushconst(prs, value);
    nn_astemit_emitbyteandshort(prs, NEON_OP_PUSHCONSTANT, (uint16_t)constant);
}

int nn_astemit_emitjump(NNAstParser* prs, uint8_t instruction)
{
    nn_astemit_emitinstruc(prs, instruction);
    /* placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->count - 2;
}

int nn_astemit_emitswitch(NNAstParser* prs)
{
    nn_astemit_emitinstruc(prs, NEON_OP_SWITCH);
    /* placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->count - 2;
}

int nn_astemit_emittry(NNAstParser* prs)
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

void nn_astemit_patchswitch(NNAstParser* prs, int offset, int constant)
{
    nn_astemit_patchat(prs, offset, (constant >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 1, constant & 0xff);
}

void nn_astemit_patchtry(NNAstParser* prs, int offset, int type, int address, int finally)
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

void nn_astemit_patchjump(NNAstParser* prs, int offset)
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

void nn_astfunccompiler_init(NNAstParser* prs, NNAstFuncCompiler* fnc, NNFuncContextType type, bool isanon)
{
    bool candeclthis;
    NNPrinter wtmp;
    NNAstLocal* local;
    NNObjString* fname;
    fnc->enclosing = prs->currentfunccompiler;
    fnc->targetfunc = NULL;
    fnc->contexttype = type;
    fnc->localcount = 0;
    fnc->scopedepth = 0;
    fnc->handlercount = 0;
    fnc->fromimport = false;
    fnc->targetfunc = nn_object_makefuncscript(prs->pstate, prs->currentmodule, type);
    prs->currentfunccompiler = fnc;
    if(type != NEON_FNCONTEXTTYPE_SCRIPT)
    {
        nn_vm_stackpush(prs->pstate, nn_value_fromobject(fnc->targetfunc));
        if(isanon)
        {
            nn_printer_makestackstring(prs->pstate, &wtmp);
            nn_printer_printf(&wtmp, "anonymous@[%s:%d]", prs->currentfile, prs->prevtoken.line);
            fname = nn_printer_takestring(&wtmp);
            nn_printer_destroy(&wtmp);
        }
        else
        {
            fname = nn_string_copylen(prs->pstate, prs->prevtoken.start, prs->prevtoken.length);
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

int nn_astparser_makeidentconst(NNAstParser* prs, NNAstToken* name)
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
    str = nn_string_copylen(prs->pstate, rawstr, rawlen);
    return nn_astparser_pushconst(prs, nn_value_fromobject(str));
}

bool nn_astparser_identsequal(NNAstToken* a, NNAstToken* b)
{
    return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
}

int nn_astfunccompiler_resolvelocal(NNAstParser* prs, NNAstFuncCompiler* fnc, NNAstToken* name)
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

int nn_astfunccompiler_addupvalue(NNAstParser* prs, NNAstFuncCompiler* fnc, uint16_t index, bool islocal)
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

int nn_astfunccompiler_resolveupvalue(NNAstParser* prs, NNAstFuncCompiler* fnc, NNAstToken* name)
{
    int local;
    int upvalue;
    if(fnc->enclosing == NULL)
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

int nn_astparser_addlocal(NNAstParser* prs, NNAstToken name)
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

void nn_astparser_declarevariable(NNAstParser* prs)
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

int nn_astparser_parsevariable(NNAstParser* prs, const char* message)
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

void nn_astparser_markinitialized(NNAstParser* prs)
{
    if(prs->currentfunccompiler->scopedepth == 0)
    {
        return;
    }
    prs->currentfunccompiler->locals[prs->currentfunccompiler->localcount - 1].depth = prs->currentfunccompiler->scopedepth;
}

void nn_astparser_definevariable(NNAstParser* prs, int global)
{
    /* we are in a local scope... */
    if(prs->currentfunccompiler->scopedepth > 0)
    {
        nn_astparser_markinitialized(prs);
        return;
    }
    nn_astemit_emitbyteandshort(prs, NEON_OP_GLOBALDEFINE, global);
}

NNAstToken nn_astparser_synthtoken(const char* name)
{
    NNAstToken token;
    token.isglobal = false;
    token.line = 0;
    token.type = (NNAstTokType)0;
    token.start = name;
    token.length = (int)strlen(name);
    return token;
}

NNObjFunction* nn_astparser_endcompiler(NNAstParser* prs, bool istoplevel)
{
    const char* fname;
    NNObjFunction* function;
    nn_astemit_emitreturn(prs);
    if(istoplevel)
    {
    }
    function = prs->currentfunccompiler->targetfunc;
    fname = NULL;
    if(function->name == NULL)
    {
        fname = prs->currentmodule->physicalpath->sbuf.data;
    }
    else
    {
        fname = function->name->sbuf.data;
    }
    if(!prs->haderror && prs->pstate->conf.dumpbytecode)
    {
        nn_dbg_disasmblob(prs->pstate->debugwriter, nn_astparser_currentblob(prs), fname);
    }
    prs->currentfunccompiler = prs->currentfunccompiler->enclosing;
    return function;
}

void nn_astparser_scopebegin(NNAstParser* prs)
{
    prs->currentfunccompiler->scopedepth++;
}

bool nn_astutil_scopeendcancontinue(NNAstParser* prs)
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

void nn_astparser_scopeend(NNAstParser* prs)
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

int nn_astparser_discardlocals(NNAstParser* prs, int depth)
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

void nn_astparser_endloop(NNAstParser* prs)
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
            cvals = prs->currentfunccompiler->targetfunc->fnscriptfunc.blob.constants.listitems;
            i += 1 + nn_astparser_getcodeargscount(bcode, cvals, i);
        }
    }
}

bool nn_astparser_rulebinary(NNAstParser* prs, NNAstToken previous, bool canassign)
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

bool nn_astparser_rulecall(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    uint8_t argcount;
    (void)previous;
    (void)canassign;
    argcount = nn_astparser_parsefunccallargs(prs);
    nn_astemit_emit2byte(prs, NEON_OP_CALLFUNCTION, argcount);
    return true;
}

bool nn_astparser_ruleliteral(NNAstParser* prs, bool canassign)
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

void nn_astparser_parseassign(NNAstParser* prs, uint8_t realop, uint8_t getop, uint8_t setop, int arg)
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

void nn_astparser_assignment(NNAstParser* prs, uint8_t getop, uint8_t setop, int arg, bool canassign)
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

bool nn_astparser_ruledot(NNAstParser* prs, NNAstToken previous, bool canassign)
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
            (prs->currentclasscompiler != NULL) &&
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
        if(prs->currentclasscompiler != NULL && (previous.type == NEON_ASTTOK_KWTHIS || nn_astparser_identsequal(&prs->prevtoken, &prs->currentclasscompiler->name)))
        {
            getop = NEON_OP_PROPERTYGETSELF;
        }
        nn_astparser_assignment(prs, getop, setop, name, canassign);
    }
    return true;
}

void nn_astparser_namedvar(NNAstParser* prs, NNAstToken name, bool canassign)
{
    bool fromclass;
    uint8_t getop;
    uint8_t setop;
    int arg;
    (void)fromclass;
    fromclass = prs->currentclasscompiler != NULL;
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

void nn_astparser_createdvar(NNAstParser* prs, NNAstToken name)
{
    int local;
    if(prs->currentfunccompiler->targetfunc->name != NULL)
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

bool nn_astparser_rulearray(NNAstParser* prs, bool canassign)
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

bool nn_astparser_ruledictionary(NNAstParser* prs, bool canassign)
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
                    nn_astemit_emitconst(prs, nn_value_fromobject(nn_string_copylen(prs->pstate, prs->prevtoken.start, prs->prevtoken.length)));
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

bool nn_astparser_ruleindexing(NNAstParser* prs, NNAstToken previous, bool canassign)
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

bool nn_astparser_rulevarnormal(NNAstParser* prs, bool canassign)
{
    nn_astparser_namedvar(prs, prs->prevtoken, canassign);
    return true;
}


bool nn_astparser_rulethis(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    #if 0
    if(prs->currentclasscompiler == NULL)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'this' outside of a class");
        return false;
    }
    #endif
    #if 0
    if(prs->currentclasscompiler != NULL)
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

bool nn_astparser_rulesuper(NNAstParser* prs, bool canassign)
{
    int name;
    bool invokeself;
    uint8_t argcount;
    (void)canassign;
    if(prs->currentclasscompiler == NULL)
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

bool nn_astparser_rulegrouping(NNAstParser* prs, bool canassign)
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
        llval = strtoll(source + 2, NULL, 2);
        return nn_value_makenumber(llval);
    }
    else if(type == NEON_ASTTOK_LITNUMOCT)
    {
        longval = strtol(source + 2, NULL, 8);
        return nn_value_makenumber(longval);
    }
    else if(type == NEON_ASTTOK_LITNUMHEX)
    {
        longval = strtol(source, NULL, 16);
        return nn_value_makenumber(longval);
    }
    dbval = strtod(source, NULL);
    return nn_value_makenumber(dbval);
}

NNValue nn_astparser_compilenumber(NNAstParser* prs)
{
    return nn_astparser_compilestrnumber(prs->prevtoken.type, prs->prevtoken.start);
}

bool nn_astparser_rulenumber(NNAstParser* prs, bool canassign)
{
    (void)canassign;
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
int nn_astparser_readhexescape(NNAstParser* prs, const char* str, int index, int count)
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

int nn_astparser_readunicodeescape(NNAstParser* prs, char* string, const char* realstring, int numberbytes, int realindex, int index)
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

char* nn_astparser_compilestring(NNAstParser* prs, int* length, bool permitescapes)
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

bool nn_astparser_rulestring(NNAstParser* prs, bool canassign)
{
    int length;
    char* str;
    (void)canassign;
    str = nn_astparser_compilestring(prs, &length, true);
    nn_astemit_emitconst(prs, nn_value_fromobject(nn_string_takelen(prs->pstate, str, length)));
    return true;
}

bool nn_astparser_rulerawstring(NNAstParser* prs, bool canassign)
{
    int length;
    char* str;
    (void)canassign;
    str = nn_astparser_compilestring(prs, &length, false);
    nn_astemit_emitconst(prs, nn_value_fromobject(nn_string_takelen(prs->pstate, str, length)));
    return true;
}

bool nn_astparser_ruleinterpolstring(NNAstParser* prs, bool canassign)
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

bool nn_astparser_ruleunary(NNAstParser* prs, bool canassign)
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

bool nn_astparser_ruleand(NNAstParser* prs, NNAstToken previous, bool canassign)
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


bool nn_astparser_ruleor(NNAstParser* prs, NNAstToken previous, bool canassign)
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

bool nn_astparser_ruleinstanceof(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    (void)previous;
    (void)canassign;
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_OPINSTANCEOF);

    return true;
}

bool nn_astparser_ruleconditional(NNAstParser* prs, NNAstToken previous, bool canassign)
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

bool nn_astparser_ruleimport(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_IMPORTIMPORT);
    return true;
}

bool nn_astparser_rulenew(NNAstParser* prs, bool canassign)
{
    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "class name after 'new'");
    return nn_astparser_rulevarnormal(prs, canassign);
}

bool nn_astparser_ruletypeof(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'typeof'");
    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'typeof'");
    nn_astemit_emitinstruc(prs, NEON_OP_TYPEOF);
    return true;
}

bool nn_astparser_rulenothingprefix(NNAstParser* prs, bool canassign)
{
    (void)prs;
    (void)canassign;
    return true;
}

bool nn_astparser_rulenothinginfix(NNAstParser* prs, NNAstToken previous, bool canassign)
{
    (void)prs;
    (void)previous;
    (void)canassign;
    return true;
}

NNAstRule* nn_astparser_putrule(NNAstRule* dest, NNAstParsePrefixFN prefix, NNAstParseInfixFN infix, NNAstPrecedence precedence)
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
        dorule(NEON_ASTTOK_PARENCLOSE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BRACKETOPEN, nn_astparser_rulearray, nn_astparser_ruleindexing, NEON_ASTPREC_CALL );
        dorule(NEON_ASTTOK_BRACKETCLOSE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BRACEOPEN, nn_astparser_ruledictionary, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BRACECLOSE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_SEMICOLON, nn_astparser_rulenothingprefix, nn_astparser_rulenothinginfix, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_COMMA, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BACKSLASH, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_EXCLMARK, nn_astparser_ruleunary, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_NOTEQUAL, NULL, nn_astparser_rulebinary, NEON_ASTPREC_EQUALITY );
        dorule(NEON_ASTTOK_COLON, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_AT, nn_astparser_ruleanonfunc, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_DOT, NULL, nn_astparser_ruledot, NEON_ASTPREC_CALL );
        dorule(NEON_ASTTOK_DOUBLEDOT, NULL, nn_astparser_rulebinary, NEON_ASTPREC_RANGE );
        dorule(NEON_ASTTOK_TRIPLEDOT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_PLUS, nn_astparser_ruleunary, nn_astparser_rulebinary, NEON_ASTPREC_TERM );
        dorule(NEON_ASTTOK_PLUSASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_INCREMENT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_MINUS, nn_astparser_ruleunary, nn_astparser_rulebinary, NEON_ASTPREC_TERM );
        dorule(NEON_ASTTOK_MINUSASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_DECREMENT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_MULTIPLY, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_MULTASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_POWEROF, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_POWASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_DIVIDE, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_DIVASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_FLOOR, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_ASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_EQUAL, NULL, nn_astparser_rulebinary, NEON_ASTPREC_EQUALITY );
        dorule(NEON_ASTTOK_LESSTHAN, NULL, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_LESSEQUAL, NULL, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_LEFTSHIFT, NULL, nn_astparser_rulebinary, NEON_ASTPREC_SHIFT );
        dorule(NEON_ASTTOK_LEFTSHIFTASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_GREATERTHAN, NULL, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_GREATER_EQ, NULL, nn_astparser_rulebinary, NEON_ASTPREC_COMPARISON );
        dorule(NEON_ASTTOK_RIGHTSHIFT, NULL, nn_astparser_rulebinary, NEON_ASTPREC_SHIFT );
        dorule(NEON_ASTTOK_RIGHTSHIFTASSIGN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_MODULO, NULL, nn_astparser_rulebinary, NEON_ASTPREC_FACTOR );
        dorule(NEON_ASTTOK_PERCENT_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_AMP, NULL, nn_astparser_rulebinary, NEON_ASTPREC_BITAND );
        dorule(NEON_ASTTOK_AMP_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_BAR, /*nn_astparser_ruleanoncompat*/ NULL, nn_astparser_rulebinary, NEON_ASTPREC_BITOR );
        dorule(NEON_ASTTOK_BAR_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_TILDE, nn_astparser_ruleunary, NULL, NEON_ASTPREC_UNARY );
        dorule(NEON_ASTTOK_TILDE_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_XOR, NULL, nn_astparser_rulebinary, NEON_ASTPREC_BITXOR );
        dorule(NEON_ASTTOK_XOR_EQ, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_QUESTION, NULL, nn_astparser_ruleconditional, NEON_ASTPREC_CONDITIONAL );
        dorule(NEON_ASTTOK_KWAND, NULL, nn_astparser_ruleand, NEON_ASTPREC_AND );
        dorule(NEON_ASTTOK_KWAS, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWASSERT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWBREAK, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCLASS, nn_astparser_ruleanonclass, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCONTINUE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFUNCTION, nn_astparser_ruleanonfunc, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWDEFAULT, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTHROW, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWDO, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWECHO, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWELSE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFALSE, nn_astparser_ruleliteral, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFOREACH, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWIF, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWIMPORT, nn_astparser_ruleimport, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWIN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWINSTANCEOF, NULL, nn_astparser_ruleinstanceof, NEON_ASTPREC_OR );
        dorule(NEON_ASTTOK_KWFOR, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWVAR, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWNULL, nn_astparser_ruleliteral, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWNEW, nn_astparser_rulenew, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTYPEOF, nn_astparser_ruletypeof, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWOR, NULL, nn_astparser_ruleor, NEON_ASTPREC_OR );
        dorule(NEON_ASTTOK_KWSUPER, nn_astparser_rulesuper, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWRETURN, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTHIS, nn_astparser_rulethis, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWSTATIC, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTRUE, nn_astparser_ruleliteral, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWSWITCH, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCASE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWWHILE, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWTRY, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWCATCH, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWFINALLY, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITERALSTRING, nn_astparser_rulestring, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITERALRAWSTRING, nn_astparser_rulerawstring, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMREG, nn_astparser_rulenumber, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMBIN, nn_astparser_rulenumber, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMOCT, nn_astparser_rulenumber, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_LITNUMHEX, nn_astparser_rulenumber, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_IDENTNORMAL, nn_astparser_rulevarnormal, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_INTERPOLATION, nn_astparser_ruleinterpolstring, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_EOF, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_ERROR, NULL, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_KWEMPTY, nn_astparser_ruleliteral, NULL, NEON_ASTPREC_NONE );
        dorule(NEON_ASTTOK_UNDEFINED, NULL, NULL, NEON_ASTPREC_NONE );
        default:
            fprintf(stderr, "missing rule?\n");
            break;
    }
    return NULL;
}
#undef dorule

bool nn_astparser_doparseprecedence(NNAstParser* prs, NNAstPrecedence precedence/*, NNAstExpression* dest*/)
{
    bool canassign;
    NNAstRule* rule;
    NNAstToken previous;
    NNAstParseInfixFN infixrule;
    NNAstParsePrefixFN prefixrule;
    rule = nn_astparser_getrule(prs->prevtoken.type);
    if(rule == NULL)
    {
        return false;
    }
    prefixrule = rule->prefix;
    if(prefixrule == NULL)
    {
        nn_astparser_raiseerror(prs, "expected expression");
        return false;
    }
    canassign = precedence <= NEON_ASTPREC_ASSIGNMENT;
    prefixrule(prs, canassign);
    while(true)
    {
        rule = nn_astparser_getrule(prs->currtoken.type);
        if(rule == NULL)
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

bool nn_astparser_parseprecedence(NNAstParser* prs, NNAstPrecedence precedence)
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

bool nn_astparser_parseprecnoadvance(NNAstParser* prs, NNAstPrecedence precedence)
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

bool nn_astparser_parseexpression(NNAstParser* prs)
{
    return nn_astparser_parseprecedence(prs, NEON_ASTPREC_ASSIGNMENT);
}

bool nn_astparser_parseblock(NNAstParser* prs)
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

void nn_astparser_declarefuncargvar(NNAstParser* prs)
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


int nn_astparser_parsefuncparamvar(NNAstParser* prs, const char* message)
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

uint8_t nn_astparser_parsefunccallargs(NNAstParser* prs)
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

void nn_astparser_parsefuncparamlist(NNAstParser* prs)
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
            #if 1
                defvalconst = nn_astparser_addlocal(prs, paramname);
            #else
                defvalconst = paramconst;
            #endif
            #if 1
                #if 1
                    nn_astemit_emitbyteandshort(prs, NEON_OP_FUNCARGOPTIONAL, defvalconst);
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

void nn_astfunccompiler_compilebody(NNAstParser* prs, NNAstFuncCompiler* fnc, bool closescope, bool isanon)
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

void nn_astparser_parsefuncfull(NNAstParser* prs, NNFuncContextType type, bool isanon)
{
    NNAstFuncCompiler fnc;
    prs->infunction = true;
    nn_astfunccompiler_init(prs, &fnc, type, isanon);
    nn_astparser_scopebegin(prs);
    /* compile parameter list */
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after function name");
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENCLOSE))
    {
        nn_astparser_parsefuncparamlist(prs);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after function parameters");
    nn_astfunccompiler_compilebody(prs, &fnc, false, isanon);
    prs->infunction = false;
}

void nn_astparser_parsemethod(NNAstParser* prs, NNAstToken classname, NNAstToken methodname, bool havenametoken, bool isstatic)
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

bool nn_astparser_ruleanonfunc(NNAstParser* prs, bool canassign)
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
        nn_astparser_parsefuncparamlist(prs);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after anonymous function parameters");
    nn_astfunccompiler_compilebody(prs, &fnc, true, true);
    return true;
}


bool nn_astparser_ruleanonclass(NNAstParser* prs, bool canassign)
{
    (void)canassign;
    nn_astparser_parseclassdeclaration(prs, false);
    return true;
}

bool nn_astparser_parsefield(NNAstParser* prs, NNAstToken* nametokendest, bool* havenamedest, bool isstatic)
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

void nn_astparser_parsefuncdecl(NNAstParser* prs)
{
    int global;
    global = nn_astparser_parsevariable(prs, "function name expected");
    nn_astparser_markinitialized(prs);
    nn_astparser_parsefuncfull(prs, NEON_FNCONTEXTTYPE_FUNCTION, false);
    nn_astparser_definevariable(prs, global);
}

void nn_astparser_parseclassdeclaration(NNAstParser* prs, bool named)
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
    nn_astparser_definevariable(prs, nameconst);
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
    nn_astparser_namedvar(prs, classname, false);
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
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    if(classcompiler.hassuperclass)
    {
        nn_astparser_scopeend(prs);
    }
    prs->currentclasscompiler = prs->currentclasscompiler->enclosing;
    prs->compcontext = oldctx;
}

void nn_astparser_parsevardecl(NNAstParser* prs, bool isinitializer, bool isconst)
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

void nn_astparser_parseexprstmt(NNAstParser* prs, bool isinitializer, bool semi)
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
 
void nn_astparser_parseforstmt(NNAstParser* prs)
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
void nn_astparser_parseforeachstmt(NNAstParser* prs)
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
    citer = nn_astparser_pushconst(prs, nn_value_fromobject(nn_string_copycstr(prs->pstate, "@iter")));
    citern = nn_astparser_pushconst(prs, nn_value_fromobject(nn_string_copycstr(prs->pstate, "@itern")));
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
void nn_astparser_parseswitchstmt(NNAstParser* prs)
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
    sw = nn_object_makeswitch(prs->pstate);
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
                        nn_valtable_set(&sw->table, nn_value_makebool(true), jump);
                    }
                    else if(prs->prevtoken.type == NEON_ASTTOK_KWFALSE)
                    {
                        nn_valtable_set(&sw->table, nn_value_makebool(false), jump);
                    }
                    else if(prs->prevtoken.type == NEON_ASTTOK_LITERALSTRING || prs->prevtoken.type == NEON_ASTTOK_LITERALRAWSTRING)
                    {
                        str = nn_astparser_compilestring(prs, &length, true);
                        string = nn_string_takelen(prs->pstate, str, length);
                        /* gc fix */
                        nn_vm_stackpush(prs->pstate, nn_value_fromobject(string));
                        nn_valtable_set(&sw->table, nn_value_fromobject(string), jump);
                        /* gc fix */
                        nn_vm_stackpop(prs->pstate);
                    }
                    else if(nn_astparser_checknumber(prs))
                    {
                        nn_valtable_set(&sw->table, nn_astparser_compilenumber(prs), jump);
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

void nn_astparser_parseifstmt(NNAstParser* prs)
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

void nn_astparser_parseechostmt(NNAstParser* prs)
{
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_ECHO);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parsethrowstmt(NNAstParser* prs)
{
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_EXTHROW);
    nn_astparser_discardlocals(prs, prs->currentfunccompiler->scopedepth - 1);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parseassertstmt(NNAstParser* prs)
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

void nn_astparser_parsetrystmt(NNAstParser* prs)
{
    int address;
    int type;
    int finally;
    int trybegins;
    int exitjump;
    int continueexecutionaddress;
    bool catchexists;
    bool finalexists;
    if(prs->currentfunccompiler->handlercount == NEON_CONFIG_MAXEXCEPTHANDLERS)
    {
        nn_astparser_raiseerror(prs, "maximum exception handler in scope exceeded");
    }
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
        type = nn_astparser_pushconst(prs, nn_value_fromobject(nn_string_copycstr(prs->pstate, "Exception")));
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

void nn_astparser_parsereturnstmt(NNAstParser* prs)
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

void nn_astparser_parsewhilestmt(NNAstParser* prs)
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

void nn_astparser_parsedo_whilestmt(NNAstParser* prs)
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

void nn_astparser_parsecontinuestmt(NNAstParser* prs)
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

void nn_astparser_parsebreakstmt(NNAstParser* prs)
{
    if(!prs->inswitch)
    {
        if(prs->innermostloopstart == -1)
        {
            nn_astparser_raiseerror(prs, "'break' can only be used in a loop");
        }
        /* discard local variables created in the loop */
        /*
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
        */
        nn_astparser_discardlocals(prs, prs->innermostloopscopedepth + 1);
        nn_astemit_emitjump(prs, NEON_OP_BREAK_PL);
    }
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_synchronize(NNAstParser* prs)
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
        function = NULL;
    }
    nn_astlex_destroy(state, lexer);
    nn_astparser_destroy(parser);
    return function;
}

void nn_gcmem_markcompilerroots(NNState* state)
{
    (void)state;
    /*
    NNAstFuncCompiler* fnc;
    fnc = state->fnc;
    while(fnc != NULL)
    {
        nn_gcmem_markobject(state, (NNObject*)fnc->targetfunc);
        fnc = fnc->enclosing;
    }
    */
}

NNRegModule* nn_natmodule_load_null(NNState* state)
{
    static NNRegFunc modfuncs[] =
    {
        /* {"somefunc",   true,  myfancymodulefunction},*/
        {NULL, false, NULL},
    };

    static NNRegField modfields[] =
    {
        /*{"somefield", true, the_function_that_gets_called},*/
        {NULL, false, NULL},
    };
    static NNRegModule module;
    (void)state;
    module.name = "null";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = NULL;
    module.preloader= NULL;
    module.unloader = NULL;
    return &module;
}

void nn_modfn_os_preloader(NNState* state)
{
    (void)state;
}

NNValue nn_modfn_os_readdir(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    os = nn_value_asstring(argv[0]);
    callable = argv[1];
    dirn = os->sbuf.data;
    if(fslib_diropen(&rd, dirn))
    {
        while(fslib_dirread(&rd, &itm))
        {
            #if 0
                itemstr = nn_string_intern(state, itm.name);
            #else
                itemstr = nn_string_copycstr(state, itm.name);
            #endif
            itemval = nn_value_fromobject(itemstr);
            nestargs[0] = itemval;
            nn_nestcall_callfunction(state, callable, thisval, nestargs, 1, &res);
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

NNRegModule* nn_natmodule_load_os(NNState* state)
{
    static NNRegFunc modfuncs[] =
    {
        {"readdir",   true,  nn_modfn_os_readdir},
        {NULL,     false, NULL},
    };
    static NNRegField modfields[] =
    {
        /*{"platform", true, get_os_platform},*/
        {NULL,       false, NULL},
    };
    static NNRegModule module;
    (void)state;
    module.name = "os";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = NULL;
    module.preloader= &nn_modfn_os_preloader;
    module.unloader = NULL;
    return &module;
}

NNValue nn_modfn_astscan_scan(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
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
    scn = nn_astlex_make(state, insrc->sbuf.data);
    arr = nn_array_make(state);
    while(!nn_astlex_isatend(scn))
    {
        itm = nn_object_makedict(state);
        token = nn_astlex_scantoken(scn);
        nn_dict_addentrycstr(itm, "line", nn_value_makenumber(token.line));
        cstr = nn_astutil_toktype2str(token.type);
        /* 12 == "NEON_ASTTOK_".length */
        nn_dict_addentrycstr(itm, "type", nn_value_fromobject(nn_string_copycstr(state, cstr + 12)));
        nn_dict_addentrycstr(itm, "source", nn_value_fromobject(nn_string_copylen(state, token.start, token.length)));
        nn_array_push(arr, nn_value_fromobject(itm));
    }
    nn_astlex_destroy(state, scn);
    return nn_value_fromobject(arr);
}

NNRegModule* nn_natmodule_load_astscan(NNState* state)
{
    NNRegModule* ret;
    static NNRegFunc modfuncs[] =
    {
        {"scan",   true,  nn_modfn_astscan_scan},
        {NULL,     false, NULL},
    };
    static NNRegField modfields[] =
    {
        {NULL,       false, NULL},
    };
    static NNRegModule module;
    (void)state;
    module.name = "astscan";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = NULL;
    module.preloader= NULL;
    module.unloader = NULL;
    ret = &module;
    return ret;
}

NNModInitFN g_builtinmodules[] =
{
    nn_natmodule_load_null,
    nn_natmodule_load_os,
    nn_natmodule_load_astscan,
    NULL,
};

bool nn_import_loadnativemodule(NNState* state, NNModInitFN init_fn, char* importname, const char* source, void* dlw)
{
    int j;
    int k;
    NNValue v;
    NNValue fieldname;
    NNValue funcname;
    NNValue funcrealvalue;
    NNRegFunc func;
    NNRegField field;
    NNRegModule* module;
    NNObjModule* themodule;
    NNRegClass klassreg;
    NNObjString* classname;
    NNObjFunction* native;
    NNObjClass* klass;
    NNHashValTable* tabdest;
    module = init_fn(state);
    if(module != NULL)
    {
        themodule = (NNObjModule*)nn_gcmem_protect(state, (NNObject*)nn_module_make(state, (char*)module->name, source, false, true));
        themodule->preloader = (void*)module->preloader;
        themodule->unloader = (void*)module->unloader;
        if(module->fields != NULL)
        {
            for(j = 0; module->fields[j].name != NULL; j++)
            {
                field = module->fields[j];
                fieldname = nn_value_fromobject(nn_gcmem_protect(state, (NNObject*)nn_string_copycstr(state, field.name)));
                v = field.fieldvalfn(state);
                nn_vm_stackpush(state, v);
                nn_valtable_set(&themodule->deftable, fieldname, v);
                nn_vm_stackpop(state);
            }
        }
        if(module->functions != NULL)
        {
            for(j = 0; module->functions[j].name != NULL; j++)
            {
                func = module->functions[j];
                funcname = nn_value_fromobject(nn_gcmem_protect(state, (NNObject*)nn_string_copycstr(state, func.name)));
                funcrealvalue = nn_value_fromobject(nn_gcmem_protect(state, (NNObject*)nn_object_makefuncnative(state, func.function, func.name, NULL)));
                nn_vm_stackpush(state, funcrealvalue);
                nn_valtable_set(&themodule->deftable, funcname, funcrealvalue);
                nn_vm_stackpop(state);
            }
        }
        if(module->classes != NULL)
        {
            for(j = 0; module->classes[j].name != NULL; j++)
            {
                klassreg = module->classes[j];
                classname = (NNObjString*)nn_gcmem_protect(state, (NNObject*)nn_string_copycstr(state, klassreg.name));
                klass = (NNObjClass*)nn_gcmem_protect(state, (NNObject*)nn_object_makeclass(state, classname, state->classprimobject));
                if(klassreg.functions != NULL)
                {
                    for(k = 0; klassreg.functions[k].name != NULL; k++)
                    {
                        func = klassreg.functions[k];
                        funcname = nn_value_fromobject(nn_gcmem_protect(state, (NNObject*)nn_string_copycstr(state, func.name)));
                        native = (NNObjFunction*)nn_gcmem_protect(state, (NNObject*)nn_object_makefuncnative(state, func.function, func.name, NULL));
                        if(func.isstatic)
                        {
                            native->contexttype = NEON_FNCONTEXTTYPE_STATIC;
                        }
                        else if(strlen(func.name) > 0 && func.name[0] == '_')
                        {
                            native->contexttype = NEON_FNCONTEXTTYPE_PRIVATE;
                        }
                        nn_valtable_set(&klass->instmethods, funcname, nn_value_fromobject(native));
                    }
                }
                if(klassreg.fields != NULL)
                {
                    for(k = 0; klassreg.fields[k].name != NULL; k++)
                    {
                        field = klassreg.fields[k];
                        fieldname = nn_value_fromobject(nn_gcmem_protect(state, (NNObject*)nn_string_copycstr(state, field.name)));
                        v = field.fieldvalfn(state);
                        nn_vm_stackpush(state, v);
                        tabdest = &klass->instproperties;
                        if(field.isstatic)
                        {
                            tabdest = &klass->staticproperties;
                        }
                        nn_valtable_set(tabdest, fieldname, v);
                        nn_vm_stackpop(state);
                    }
                }
                nn_valtable_set(&themodule->deftable, nn_value_fromobject(classname), nn_value_fromobject(klass));
            }
        }
        if(dlw != NULL)
        {
            themodule->handle = dlw;
        }
        nn_import_addnativemodule(state, themodule, themodule->name->sbuf.data);
        nn_gcmem_clearprotect(state);
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
    if(as != NULL)
    {
        module->name = nn_string_copycstr(state, as);
    }
    name = nn_value_fromobject(nn_string_copyobject(state, module->name));
    nn_vm_stackpush(state, name);
    nn_vm_stackpush(state, nn_value_fromobject(module));
    nn_valtable_set(&state->openedmodules, name, nn_value_fromobject(module));
    nn_vm_stackpopn(state, 2);
}

void nn_import_loadbuiltinmodules(NNState* state)
{
    int i;
    for(i = 0; g_builtinmodules[i] != NULL; i++)
    {
        nn_import_loadnativemodule(state, g_builtinmodules[i], NULL, "<__native__>", NULL);
    }
}

void nn_import_closemodule(void* hnd)
{
    (void)hnd;
}

bool nn_util_fsfileexists(NNState* state, const char* filepath)
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

bool nn_util_fsfileistype(NNState* state, const char* filepath, int typ)
{
    struct stat st;
    (void)state;
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

bool nn_util_fsfileisfile(NNState* state, const char* filepath)
{
    return nn_util_fsfileistype(state, filepath, 'f');
}

bool nn_util_fsfileisdirectory(NNState* state, const char* filepath)
{
    return nn_util_fsfileistype(state, filepath, 'd');
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
    field = nn_valtable_getfieldbyostr(&state->openedmodules, modulename);
    if(field != NULL)
    {
        return nn_value_asmodule(field->value);
    }
    physpath = nn_import_resolvepath(state, modulename->sbuf.data, intomodule->physicalpath->sbuf.data, NULL, false);
    if(physpath == NULL)
    {
        nn_except_throw(state, "module not found: '%s'\n", modulename->sbuf.data);
        return NULL;
    }
    fprintf(stderr, "loading module from '%s'\n", physpath);
    source = nn_util_filereadfile(state, physpath, &fsz, false, 0);
    if(source == NULL)
    {
        nn_except_throw(state, "could not read import file %s", physpath);
        return NULL;
    }
    nn_blob_init(state, &blob);
    module = nn_module_make(state, modulename->sbuf.data, physpath, true, true);
    nn_memory_free(physpath);
    function = nn_astparser_compilesource(state, module, source, &blob, true, false);
    nn_memory_free(source);
    closure = nn_object_makefuncclosure(state, function);
    callable = nn_value_fromobject(closure);
    nn_nestcall_prepare(state, callable, nn_value_makenull(), NULL, 0);     
    if(!nn_nestcall_callfunction(state, callable, nn_value_makenull(), NULL, 0, &retv))
    {
        nn_blob_destroy(&blob);
        nn_except_throw(state, "failed to call compiled import closure");
        return NULL;
    }
    nn_blob_destroy(&blob);
    return module;
}

char* nn_import_resolvepath(NNState* state, char* modulename, const char* currentfile, char* rootfile, bool isrelative)
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
    StringBuffer* pathbuf;
    (void)rootfile;
    (void)isrelative;
    (void)stroot;
    (void)stmod;
    mlen = strlen(modulename);
    splen = nn_valarray_count(&state->importpath);
    pathbuf = dyn_strbuf_makebasicempty(0);
    for(i=0; i<splen; i++)
    {
        pitem = nn_value_asstring(nn_valarray_get(&state->importpath, i));
        dyn_strbuf_reset(pathbuf);
        dyn_strbuf_appendstrn(pathbuf, pitem->sbuf.data, pitem->sbuf.length);
        if(dyn_strbuf_containschar(pathbuf, '@'))
        {
            dyn_strbuf_charreplace(pathbuf, '@', modulename, mlen);
        }
        else
        {
            dyn_strbuf_appendstr(pathbuf, "/");
            dyn_strbuf_appendstr(pathbuf, modulename);
            dyn_strbuf_appendstr(pathbuf, NEON_CONFIG_FILEEXT);
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
            #if 1   
                path1 = osfn_realpath(cstrpath, NULL);
                path2 = osfn_realpath(currentfile, NULL);
            #else
                path1 = strdup(cstrpath);
                path2 = strdup(currentfile);
            #endif
            if(path1 != NULL && path2 != NULL)
            {
                if(memcmp(path1, path2, (int)strlen(path2)) == 0)
                {
                    nn_memory_free(path1);
                    nn_memory_free(path2);
                    path1 = NULL;
                    path2 = NULL;
                    fprintf(stderr, "resolvepath: refusing to import itself\n");
                    return NULL;
                }
                if(path2 != NULL)
                {
                    nn_memory_free(path2);
                }
                dyn_strbuf_destroy(pathbuf);
                pathbuf = NULL;
                retme = nn_util_strdup(path1);
                if(path1 != NULL)
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
    dyn_strbuf_destroy(pathbuf);
    return NULL;
}

char* nn_util_fsgetbasename(NNState* state, char* path)
{
    (void)state;
    return osfn_basename(path);
}


NNValue nn_objfndict_length(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "length", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_valarray_count(&nn_value_asdict(thisval)->names));
}

NNValue nn_objfndict_add(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue tempvalue;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "add", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    dict = nn_value_asdict(thisval);
    if(nn_valtable_get(&dict->htab, argv[0], &tempvalue))
    {
        NEON_RETURNERROR("duplicate key %s at add()", nn_value_tostring(state, argv[0])->sbuf.data);
    }
    nn_dict_addentry(dict, argv[0], argv[1]);
    return nn_value_makenull();
}

NNValue nn_objfndict_set(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue value;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "set", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    dict = nn_value_asdict(thisval);
    if(!nn_valtable_get(&dict->htab, argv[0], &value))
    {
        nn_dict_addentry(dict, argv[0], argv[1]);
    }
    else
    {
        nn_dict_setentry(dict, argv[0], argv[1]);
    }
    return nn_value_makenull();
}

NNValue nn_objfndict_clear(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "clear", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    nn_valarray_destroy(&dict->names, false);
    nn_valtable_destroy(&dict->htab);
    return nn_value_makenull();
}

NNValue nn_objfndict_clone(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjDict* newdict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "clone", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    newdict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict(state));
    if(!nn_valtable_addall(&dict->htab, &newdict->htab, true))
    {
        nn_except_throwclass(state, state->exceptions.argumenterror, "failed to copy table");
        return nn_value_makenull();
    }
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        nn_valarray_push(&newdict->names, nn_valarray_get(&dict->names, i));
    }
    return nn_value_fromobject(newdict);
}

NNValue nn_objfndict_compact(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjDict* newdict;
    NNValue tmpvalue;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "compact", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    newdict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict(state));
    tmpvalue = nn_value_makenull();
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &tmpvalue);
        if(!nn_value_compare(state, tmpvalue, nn_value_makenull()))
        {
            nn_dict_addentry(newdict, nn_valarray_get(&dict->names, i), tmpvalue);
        }
    }
    return nn_value_fromobject(newdict);
}

NNValue nn_objfndict_contains(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue value;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "contains", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(thisval);
    return nn_value_makebool(nn_valtable_get(&dict->htab, argv[0], &value));
}

NNValue nn_objfndict_extend(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    for(i = 0; i < nn_valarray_count(&dictcpy->names); i++)
    {
        if(!nn_valtable_get(&dict->htab, nn_valarray_get(&dictcpy->names, i), &tmp))
        {
            nn_valarray_push(&dict->names, nn_valarray_get(&dictcpy->names, i));
        }
    }
    nn_valtable_addall(&dictcpy->htab, &dict->htab, true);
    return nn_value_makenull();
}

NNValue nn_objfndict_get(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjDict* dict;
    NNProperty* field;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "get", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    dict = nn_value_asdict(thisval);
    field = nn_dict_getentry(dict, argv[0]);
    if(field == NULL)
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
    return field->value;
}

NNValue nn_objfndict_keys(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "keys", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        nn_array_push(list, nn_valarray_get(&dict->names, i));
    }
    return nn_value_fromobject(list);
}

NNValue nn_objfndict_values(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjArray* list;
    NNProperty* field;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "values", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        field = nn_dict_getentry(dict, nn_valarray_get(&dict->names, i));
        nn_array_push(list, field->value);
    }
    return nn_value_fromobject(list);
}

NNValue nn_objfndict_remove(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    int index;
    NNValue value;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "remove", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(thisval);
    if(nn_valtable_get(&dict->htab, argv[0], &value))
    {
        nn_valtable_delete(&dict->htab, argv[0]);
        index = -1;
        for(i = 0; i < nn_valarray_count(&dict->names); i++)
        {
            if(nn_value_compare(state, nn_valarray_get(&dict->names, i), argv[0]))
            {
                index = i;
                break;
            }
        }
        for(i = index; i < nn_valarray_count(&dict->names); i++)
        {
            nn_valarray_set(&dict->names, i, nn_valarray_get(&dict->names, i + 1));
        }
        nn_valarray_decreaseby(&dict->names, 1);
        return value;
    }
    return nn_value_makenull();
}

NNValue nn_objfndict_isempty(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isempty", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makebool(nn_valarray_count(&nn_value_asdict(thisval)->names) == 0);
}

NNValue nn_objfndict_findkey(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "findkey", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    return nn_valtable_findkey(&nn_value_asdict(thisval)->htab, argv[0]);
}

NNValue nn_objfndict_tolist(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    namelist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    valuelist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        nn_array_push(namelist, nn_valarray_get(&dict->names, i));
        NNValue value;
        if(nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &value))
        {
            nn_array_push(valuelist, value);
        }
        else
        {
            /* theoretically impossible */
            nn_array_push(valuelist, nn_value_makenull());
        }
    }
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    nn_array_push(list, nn_value_fromobject(namelist));
    nn_array_push(list, nn_value_fromobject(valuelist));
    return nn_value_fromobject(list);
}

NNValue nn_objfndict_iter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue result;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "iter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(thisval);
    if(nn_valtable_get(&dict->htab, argv[0], &result))
    {
        return result;
    }
    return nn_value_makenull();
}

NNValue nn_objfndict_itern(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "itern", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(thisval);
    if(nn_value_isnull(argv[0]))
    {
        if(nn_valarray_count(&dict->names) == 0)
        {
            return nn_value_makebool(false);
        }
        return nn_valarray_get(&dict->names, 0);
    }
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        if(nn_value_compare(state, argv[0], nn_valarray_get(&dict->names, i)) && (i + 1) < nn_valarray_count(&dict->names))
        {
            return nn_valarray_get(&dict->names, i + 1);
        }
    }
    return nn_value_makenull();
}

NNValue nn_objfndict_each(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    value = nn_value_makenull();
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        passi = 0;
        if(arity > 0)
        {
            nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &value);
            passi++;
            nestargs[0] = value;
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_valarray_get(&dict->names, i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &unused);
    }
    return nn_value_makenull();
}

NNValue nn_objfndict_filter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    resultdict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict(state));
    value = nn_value_makenull();
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        passi = 0;
        nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &value);
        if(arity > 0)
        {
            passi++;
            nestargs[0] = value;
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_valarray_get(&dict->names, i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result);
        if(!nn_value_isfalse(result))
        {
            nn_dict_addentry(resultdict, nn_valarray_get(&dict->names, i), value);
        }
    }
    /* pop the call list */
    return nn_value_fromobject(resultdict);
}

NNValue nn_objfndict_some(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    value = nn_value_makenull();
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        passi = 0;
        if(arity > 0)
        {
            nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &value);
            passi++;
            nestargs[0] = value;
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_valarray_get(&dict->names, i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result);
        if(!nn_value_isfalse(result))
        {
            /* pop the call list */
            return nn_value_makebool(true);
        }
    }
    /* pop the call list */
    return nn_value_makebool(false);
}

NNValue nn_objfndict_every(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    value = nn_value_makenull();
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        passi = 0;
        if(arity > 0)
        {
            nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &value);
            passi++;
            nestargs[0] = value;
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_valarray_get(&dict->names, i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result);
        if(nn_value_isfalse(result))
        {
            /* pop the call list */
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(true);
}

NNValue nn_objfndict_reduce(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    if(nn_value_isnull(accumulator) && nn_valarray_count(&dict->names) > 0)
    {
        nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, 0), &accumulator);
        startindex = 1;
    }
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 4);
    value = nn_value_makenull();
    for(i = startindex; i < nn_valarray_count(&dict->names); i++)
    {
        passi = 0;
        /* only call map for non-empty values in a list. */
        if(!nn_value_isnull(nn_valarray_get(&dict->names, i)) && !nn_value_isnull(nn_valarray_get(&dict->names, i)))
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = accumulator;
                if(arity > 1)
                {
                    nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &value);
                    passi++;
                    nestargs[1] = value;
                    if(arity > 2)
                    {
                        passi++;
                        nestargs[2] = nn_valarray_get(&dict->names, i);
                        if(arity > 4)
                        {
                            passi++;
                            nestargs[3] = thisval;
                        }
                    }
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &accumulator);
        }
    }
    return accumulator;
}



#define FILE_ERROR(type, message) \
    NEON_RETURNERROR(#type " -> %s", message, file->path->sbuf.data);

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
    if(file->handle != NULL && !file->isstd)
    {
        fflush(file->handle);
        result = fclose(file->handle);
        file->handle = NULL;
        file->isopen = false;
        file->number = -1;
        file->istty = false;
        return result;
    }
    return -1;
}

bool nn_fileobject_open(NNObjFile* file)
{
    if(file->handle != NULL)
    {
        return true;
    }
    if(file->handle == NULL && !file->isstd)
    {
        file->handle = fopen(file->path->sbuf.data, file->mode->sbuf.data);
        if(file->handle != NULL)
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

NNValue nn_objfnfile_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    if(opath->sbuf.length == 0)
    {
        NEON_RETURNERROR("file path cannot be empty");
    }
    mode = "r";
    if(argc == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isstring);
        mode = nn_value_asstring(argv[1])->sbuf.data;
    }
    path = opath->sbuf.data;
    file = (NNObjFile*)nn_gcmem_protect(state, (NNObject*)nn_object_makefile(state, NULL, false, path, mode));
    nn_fileobject_open(file);
    return nn_value_fromobject(file);
}

NNValue nn_objfnfile_exists(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* file;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "exists", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = nn_value_asstring(argv[0]);
    return nn_value_makebool(nn_util_fsfileexists(state, file->sbuf.data));
}

NNValue nn_objfnfile_isfile(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* file;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "isfile", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = nn_value_asstring(argv[0]);
    return nn_value_makebool(nn_util_fsfileisfile(state, file->sbuf.data));
}

NNValue nn_objfnfile_isdirectory(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* file;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "isdirectory", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    file = nn_value_asstring(argv[0]);
    return nn_value_makebool(nn_util_fsfileisdirectory(state, file->sbuf.data));
}


NNValue nn_objfnfile_readstatic(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    buf = nn_util_filereadfile(state, filepath->sbuf.data, &actualsz, true, thismuch);
    if(buf == NULL)
    {
        nn_except_throwclass(state, state->exceptions.ioerror, "%s: %s", filepath->sbuf.data, strerror(errno));
        return nn_value_makenull();
    }
    return nn_value_fromobject(nn_string_takelen(state, buf, actualsz));
}


NNValue nn_objfnfile_writestatic(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    fh = fopen(filepath->sbuf.data, mode);
    if(fh == NULL)
    {
        nn_except_throwclass(state, state->exceptions.ioerror, strerror(errno));
        return nn_value_makenull();
    }
    rt = fwrite(data->sbuf.data, sizeof(char), data->sbuf.length, fh);
    fclose(fh);
    return nn_value_makenumber(rt);
}


NNValue nn_objfnfile_statstatic(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* file;
    NNArgCheck check;
    NNObjDict* dict;
    struct stat st;
    (void)thisval;
    nn_argcheck_init(state, &check, "stat", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    dict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict(state));
    file = nn_value_asstring(argv[0]);
    if(osfn_lstat(file->sbuf.data, &st) == 0)
    {
        nn_util_statfilldictphysfile(dict, &st);
        return nn_value_fromobject(dict);
    }
    return nn_value_makenull();
}

NNValue nn_objfnfile_close(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "close", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_fileobject_close(nn_value_asfile(thisval));
    return nn_value_makenull();
}

NNValue nn_objfnfile_open(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "open", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_fileobject_open(nn_value_asfile(thisval));
    return nn_value_makenull();
}

NNValue nn_objfnfile_isopen(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    (void)state;
    (void)argv;
    (void)argc;
    file = nn_value_asfile(thisval);
    return nn_value_makebool(file->isstd || file->isopen);
}

NNValue nn_objfnfile_isclosed(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    (void)state;
    (void)argv;
    (void)argc;
    file = nn_value_asfile(thisval);
    return nn_value_makebool(!file->isstd && !file->isopen);
}

NNValue nn_objfnfile_readmethod(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    return nn_value_fromobject(nn_string_takelen(state, res.data, res.length));
}


NNValue nn_objfnfile_readline(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    strline = NULL;
    rdline = nn_util_filegetlinehandle(&strline, &linelen, file->handle);
    if(rdline == -1)
    {
        return nn_value_makenull();
    }
    nos = nn_string_takelen(state, strline, rdline);
    return nn_value_fromobject(nos);
}

NNValue nn_objfnfile_get(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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

NNValue nn_objfnfile_gets(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
        if(!nn_util_fsfileexists(state, file->path->sbuf.data))
        {
            FILE_ERROR(NotFound, "no such file or directory");
        }
        else if(strstr(file->mode->sbuf.data, "w") != NULL && strstr(file->mode->sbuf.data, "+") == NULL)
        {
            FILE_ERROR(Unsupported, "cannot read file in write mode");
        }
        if(!file->isopen)
        {
            FILE_ERROR(Read, "file not open");
        }
        else if(file->handle == NULL)
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
    if(buffer == NULL && length != 0)
    {
        FILE_ERROR(Buffer, "not enough memory to read file");
    }
    bytesread = fread(buffer, sizeof(char), length, file->handle);
    if(bytesread == 0 && length != 0)
    {
        FILE_ERROR(Read, "could not read file contents");
    }
    if(buffer != NULL)
    {
        buffer[bytesread] = '\0';
    }
    return nn_value_fromobject(nn_string_takelen(state, buffer, bytesread));
}

NNValue nn_objfnfile_write(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    data = (unsigned char*)string->sbuf.data;
    length = string->sbuf.length;
    if(!file->isstd)
    {
        if(strstr(file->mode->sbuf.data, "r") != NULL && strstr(file->mode->sbuf.data, "+") == NULL)
        {
            FILE_ERROR(Unsupported, "cannot write into non-writable file");
        }
        else if(length == 0)
        {
            FILE_ERROR(Write, "cannot write empty buffer to file");
        }
        else if(file->handle == NULL || !file->isopen)
        {
            nn_fileobject_open(file);
        }
        else if(file->handle == NULL)
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

NNValue nn_objfnfile_puts(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t count;
    int length;
    unsigned char* data;
    NNObjFile* file;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "puts", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    file = nn_value_asfile(thisval);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(argv[0]);
    data = (unsigned char*)string->sbuf.data;
    length = string->sbuf.length;
    if(!file->isstd)
    {
        if(strstr(file->mode->sbuf.data, "r") != NULL && strstr(file->mode->sbuf.data, "+") == NULL)
        {
            FILE_ERROR(Unsupported, "cannot write into non-writable file");
        }
        else if(length == 0)
        {
            FILE_ERROR(Write, "cannot write empty buffer to file");
        }
        else if(!file->isopen)
        {
            FILE_ERROR(Write, "file not open");
        }
        else if(file->handle == NULL)
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
    if(count > (size_t)0 || length == 0)
    {
        return nn_value_makebool(true);
    }
    return nn_value_makebool(false);
}

NNValue nn_objfnfile_printf(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNFormatInfo nfi;
    NNPrinter pr;
    NNObjString* ofmt;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "printf", argv, argc);
    file = nn_value_asfile(thisval);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = nn_value_asstring(argv[0]);
    nn_printer_makestackio(state, &pr, file->handle, false);
    nn_strformat_init(state, &nfi, &pr, nn_string_getcstr(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, argc, 1, argv))
    {
    }
    return nn_value_makenull();
}

NNValue nn_objfnfile_number(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "number", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asfile(thisval)->number);
}

NNValue nn_objfnfile_istty(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "istty", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    return nn_value_makebool(file->istty);
}

NNValue nn_objfnfile_flush(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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

void nn_util_statfilldictphysfile(NNObjDict* dict, struct stat* st)
{
    #if !defined(NEON_PLAT_ISWINDOWS)
    nn_dict_addentrycstr(dict, "isreadable", nn_value_makebool(((st->st_mode & S_IRUSR) != 0)));
    nn_dict_addentrycstr(dict, "iswritable", nn_value_makebool(((st->st_mode & S_IWUSR) != 0)));
    nn_dict_addentrycstr(dict, "isexecutable", nn_value_makebool(((st->st_mode & S_IXUSR) != 0)));
    nn_dict_addentrycstr(dict, "issymbolic", nn_value_makebool((S_ISLNK(st->st_mode) != 0)));
    #else
    nn_dict_addentrycstr(dict, "isreadable", nn_value_makebool(((st->st_mode & S_IREAD) != 0)));
    nn_dict_addentrycstr(dict, "iswritable", nn_value_makebool(((st->st_mode & S_IWRITE) != 0)));
    nn_dict_addentrycstr(dict, "isexecutable", nn_value_makebool(((st->st_mode & S_IEXEC) != 0)));
    nn_dict_addentrycstr(dict, "issymbolic", nn_value_makebool(false));
    #endif
    nn_dict_addentrycstr(dict, "size", nn_value_makenumber(st->st_size));
    nn_dict_addentrycstr(dict, "mode", nn_value_makenumber(st->st_mode));
    nn_dict_addentrycstr(dict, "dev", nn_value_makenumber(st->st_dev));
    nn_dict_addentrycstr(dict, "ino", nn_value_makenumber(st->st_ino));
    nn_dict_addentrycstr(dict, "nlink", nn_value_makenumber(st->st_nlink));
    nn_dict_addentrycstr(dict, "uid", nn_value_makenumber(st->st_uid));
    nn_dict_addentrycstr(dict, "gid", nn_value_makenumber(st->st_gid));
    nn_dict_addentrycstr(dict, "mtime", nn_value_makenumber(st->st_mtime));
    nn_dict_addentrycstr(dict, "atime", nn_value_makenumber(st->st_atime));
    nn_dict_addentrycstr(dict, "ctime", nn_value_makenumber(st->st_ctime));
    nn_dict_addentrycstr(dict, "blocks", nn_value_makenumber(0));
    nn_dict_addentrycstr(dict, "blksize", nn_value_makenumber(0));
}

NNValue nn_objfnfile_statmethod(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    struct stat stats;
    NNObjFile* file;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "stat", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    dict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict(state));
    if(!file->isstd)
    {
        if(nn_util_fsfileexists(state, file->path->sbuf.data))
        {
            if(osfn_lstat(file->path->sbuf.data, &stats) == 0)
            {
                nn_util_statfilldictphysfile(dict, &stats);
            }
        }
        else
        {
            NEON_RETURNERROR("cannot get stats for non-existing file");
        }
    }
    else
    {
        if(fileno(stdin) == file->number)
        {
            nn_dict_addentrycstr(dict, "isreadable", nn_value_makebool(true));
            nn_dict_addentrycstr(dict, "iswritable", nn_value_makebool(false));
        }
        else
        {
            nn_dict_addentrycstr(dict, "isreadable", nn_value_makebool(false));
            nn_dict_addentrycstr(dict, "iswritable", nn_value_makebool(true));
        }
        nn_dict_addentrycstr(dict, "isexecutable", nn_value_makebool(false));
        nn_dict_addentrycstr(dict, "size", nn_value_makenumber(1));
    }
    return nn_value_fromobject(dict);
}

NNValue nn_objfnfile_path(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "path", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    DENY_STD();
    return nn_value_fromobject(file->path);
}

NNValue nn_objfnfile_mode(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "mode", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    return nn_value_fromobject(file->mode);
}

NNValue nn_objfnfile_name(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char* name;
    NNObjFile* file;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "name", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    file = nn_value_asfile(thisval);
    if(!file->isstd)
    {
        name = nn_util_fsgetbasename(state, file->path->sbuf.data);
        return nn_value_fromobject(nn_string_copycstr(state, name));
    }
    else if(file->istty)
    {
        return nn_value_fromobject(nn_string_copycstr(state, "<tty>"));
    }
    return nn_value_makenull();
}

NNValue nn_objfnfile_seek(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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

NNValue nn_objfnfile_tell(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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

NNObjArray* nn_array_makefilled(NNState* state, size_t cnt, NNValue filler)
{
    size_t i;
    NNObjArray* list;
    list = (NNObjArray*)nn_object_allocobject(state, sizeof(NNObjArray), NEON_OBJTYPE_ARRAY, false);
    nn_valarray_init(state, &list->varray);
    if(cnt > 0)
    {
        for(i=0; i<cnt; i++)
        {
            nn_valarray_push(&list->varray, filler);
        }
    }
    return list;
}

NNObjArray* nn_array_make(NNState* state)
{
    return nn_array_makefilled(state, 0, nn_value_makenull());
}

void nn_array_push(NNObjArray* list, NNValue value)
{
    NNState* state;
    (void)state;
    state = ((NNObject*)list)->pstate;
    /*nn_vm_stackpush(state, value);*/
    nn_valarray_push(&list->varray, value);
    /*nn_vm_stackpop(state); */
}

bool nn_array_get(NNObjArray* list, size_t idx, NNValue* vdest)
{
    size_t vc;
    vc = nn_valarray_count(&list->varray);
    if((vc > 0) && (idx < vc))
    {
        *vdest = nn_valarray_get(&list->varray, idx);
        return true;
    }
    return false;
}

NNObjArray* nn_array_copy(NNObjArray* list, long start, long length)
{
    size_t i;
    NNState* state;
    NNObjArray* newlist;
    state = ((NNObject*)list)->pstate;
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    if(start == -1)
    {
        start = 0;
    }
    if(length == -1)
    {
        length = nn_valarray_count(&list->varray) - start;
    }
    for(i = start; i < (size_t)(start + length); i++)
    {
        nn_array_push(newlist, nn_valarray_get(&list->varray, i));
    }
    return newlist;
}

NNValue nn_objfnarray_length(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* selfarr;
    (void)state;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "length", argv, argc);
    selfarr = nn_value_asarray(thisval);
    return nn_value_makenumber(nn_valarray_count(&selfarr->varray));
}

NNValue nn_objfnarray_append(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    (void)state;
    for(i = 0; i < argc; i++)
    {
        nn_array_push(nn_value_asarray(thisval), argv[i]);
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_clear(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "clear", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_valarray_destroy(&nn_value_asarray(thisval)->varray, false);
    return nn_value_makenull();
}

NNValue nn_objfnarray_clone(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "clone", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    return nn_value_fromobject(nn_array_copy(list, 0, nn_valarray_count(&list->varray)));
}

NNValue nn_objfnarray_count(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    int count;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "count", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(thisval);
    count = 0;
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        if(nn_value_compare(state, nn_valarray_get(&list->varray, i), argv[0]))
        {
            count++;
        }
    }
    return nn_value_makenumber(count);
}

NNValue nn_objfnarray_extend(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    for(i = 0; i < nn_valarray_count(&list2->varray); i++)
    {
        nn_array_push(list, nn_valarray_get(&list2->varray, i));
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_indexof(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    for(; i < nn_valarray_count(&list->varray); i++)
    {
        if(nn_value_compare(state, nn_valarray_get(&list->varray, i), argv[0]))
        {
            return nn_value_makenumber(i);
        }
    }
    return nn_value_makenumber(-1);
}

NNValue nn_objfnarray_insert(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "insert", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    list = nn_value_asarray(thisval);
    index = (int)nn_value_asnumber(argv[1]);
    nn_valarray_insert(&list->varray, argv[0], index);
    return nn_value_makenull();
}


NNValue nn_objfnarray_pop(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue value;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "pop", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    if(nn_valarray_count(&list->varray) > 0)
    {
        value = nn_valarray_get(&list->varray, nn_valarray_count(&list->varray) - 1);
        nn_valarray_decreaseby(&list->varray, 1);
        return value;
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_shift(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    if(count >= nn_valarray_count(&list->varray) || nn_valarray_count(&list->varray) == 1)
    {
        nn_valarray_setcount(&list->varray, 0);
        return nn_value_makenull();
    }
    else if(count > 0)
    {
        newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
        for(i = 0; i < count; i++)
        {
            nn_array_push(newlist, nn_valarray_get(&list->varray, 0));
            for(j = 0; j < nn_valarray_count(&list->varray); j++)
            {
                nn_valarray_set(&list->varray, j, nn_valarray_get(&list->varray, j + 1));
            }
            nn_valarray_decreaseby(&list->varray, 1);
        }
        if(count == 1)
        {
            return nn_valarray_get(&newlist->varray, 0);
        }
        else
        {
            return nn_value_fromobject(newlist);
        }
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_removeat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    if(((int)index < 0) || index >= nn_valarray_count(&list->varray))
    {
        NEON_RETURNERROR("list index %d out of range at remove_at()", index);
    }
    value = nn_valarray_get(&list->varray, index);
    for(i = index; i < nn_valarray_count(&list->varray) - 1; i++)
    {
        nn_valarray_set(&list->varray, i, nn_valarray_get(&list->varray, i + 1));
    }
    nn_valarray_decreaseby(&list->varray, 1);
    return value;
}

NNValue nn_objfnarray_remove(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "remove", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(thisval);
    index = -1;
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        if(nn_value_compare(state, nn_valarray_get(&list->varray, i), argv[0]))
        {
            index = i;
            break;
        }
    }
    if((int)index != -1)
    {
        for(i = index; i < nn_valarray_count(&list->varray); i++)
        {
            nn_valarray_set(&list->varray, i, nn_valarray_get(&list->varray, i + 1));
        }
        nn_valarray_decreaseby(&list->varray, 1);
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_reverse(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int fromtop;
    NNObjArray* list;
    NNObjArray* nlist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "reverse", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    nlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    /* in-place reversal:*/
    /*
    int start = 0;
    int end = nn_valarray_count(&list->varray) - 1;
    while (start < end)
    {
        NNValue temp = nn_valarray_get(&list->varray, start);
        nn_valarray_set(&list->varray, start, nn_valarray_get(&list->varray, end));
        nn_valarray_set(&list->varray, end, temp);
        start++;
        end--;
    }
    */
    for(fromtop = nn_valarray_count(&list->varray) - 1; fromtop >= 0; fromtop--)
    {
        nn_array_push(nlist, nn_valarray_get(&list->varray, fromtop));
    }
    return nn_value_fromobject(nlist);
}

NNValue nn_objfnarray_sort(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "sort", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    nn_value_sortvalues(state, list->varray.listitems, nn_valarray_count(&list->varray));
    return nn_value_makenull();
}

NNValue nn_objfnarray_contains(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "contains", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(thisval);
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        if(nn_value_compare(state, argv[0], nn_valarray_get(&list->varray, i)))
        {
            return nn_value_makebool(true);
        }
    }
    return nn_value_makebool(false);
}

NNValue nn_objfnarray_delete(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    if(((int)idxlower < 0) || idxlower >= nn_valarray_count(&list->varray))
    {
        NEON_RETURNERROR("list index %d out of range at delete()", idxlower);
    }
    else if(idxupper < idxlower || idxupper >= nn_valarray_count(&list->varray))
    {
        NEON_RETURNERROR("invalid upper limit %d at delete()", idxupper);
    }
    for(i = 0; i < nn_valarray_count(&list->varray) - idxupper; i++)
    {
        nn_valarray_set(&list->varray, idxlower + i, nn_valarray_get(&list->varray, i + idxupper + 1));
    }
    nn_valarray_decreaseby(&list->varray, idxupper - idxlower + 1);
    return nn_value_makenumber((double)idxupper - (double)idxlower + 1);
}

NNValue nn_objfnarray_first(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "first", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    if(nn_valarray_count(&list->varray) > 0)
    {
        return nn_valarray_get(&list->varray, 0);
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_last(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "last", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    if(nn_valarray_count(&list->varray) > 0)
    {
        return nn_valarray_get(&list->varray, nn_valarray_count(&list->varray) - 1);
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_isempty(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isEmpty", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makebool(nn_valarray_count(&nn_value_asarray(thisval)->varray) == 0);
}


NNValue nn_objfnarray_take(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
        count = nn_valarray_count(&list->varray) + count;
    }
    if(nn_valarray_count(&list->varray) < count)
    {
        return nn_value_fromobject(nn_array_copy(list, 0, nn_valarray_count(&list->varray)));
    }
    return nn_value_fromobject(nn_array_copy(list, 0, count));
}

NNValue nn_objfnarray_get(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "get", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(thisval);
    index = nn_value_asnumber(argv[0]);
    if((int)index < 0 || index >= nn_valarray_count(&list->varray))
    {
        return nn_value_makenull();
    }
    return nn_valarray_get(&list->varray, index);
}

NNValue nn_objfnarray_compact(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjArray* list;
    NNObjArray* newlist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "compact", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        if(!nn_value_compare(state, nn_valarray_get(&list->varray, i), nn_value_makenull()))
        {
            nn_array_push(newlist, nn_valarray_get(&list->varray, i));
        }
    }
    return nn_value_fromobject(newlist);
}


NNValue nn_objfnarray_unique(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        found = false;
        for(j = 0; j < nn_valarray_count(&newlist->varray); j++)
        {
            if(nn_value_compare(state, nn_valarray_get(&newlist->varray, j), nn_valarray_get(&list->varray, i)))
            {
                found = true;
                continue;
            }
        }
        if(!found)
        {
            nn_array_push(newlist, nn_valarray_get(&list->varray, i));
        }
    }
    return nn_value_fromobject(newlist);
}

NNValue nn_objfnarray_zip(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    arglist = (NNObjArray**)nn_gcmem_allocate(state, sizeof(NNObjArray*), argc, false);
    for(i = 0; i < argc; i++)
    {
        NEON_ARGS_CHECKTYPE(&check, i, nn_value_isarray);
        arglist[i] = nn_value_asarray(argv[i]);
    }
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        alist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
        /* item of main list*/
        nn_array_push(alist, nn_valarray_get(&list->varray, i));
        for(j = 0; j < argc; j++)
        {
            if(i < nn_valarray_count(&arglist[j]->varray))
            {
                nn_array_push(alist, nn_valarray_get(&arglist[j]->varray, i));
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


NNValue nn_objfnarray_zipfrom(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    arglist = nn_value_asarray(argv[0]);
    for(i = 0; i < nn_valarray_count(&arglist->varray); i++)
    {
        if(!nn_value_isarray(nn_valarray_get(&arglist->varray, i)))
        {
            NEON_RETURNERROR("invalid list in zip entries");
        }
    }
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        alist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
        nn_array_push(alist, nn_valarray_get(&list->varray, i));
        for(j = 0; j < nn_valarray_count(&arglist->varray); j++)
        {
            if(i < nn_valarray_count(&nn_value_asarray(nn_valarray_get(&arglist->varray, j))->varray))
            {
                nn_array_push(alist, nn_valarray_get(&nn_value_asarray(nn_valarray_get(&arglist->varray, j))->varray, i));
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

NNValue nn_objfnarray_todict(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "toDict", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict(state));
    list = nn_value_asarray(thisval);
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        nn_dict_setentry(dict, nn_value_makenumber(i), nn_valarray_get(&list->varray, i));
    }
    return nn_value_fromobject(dict);
}

NNValue nn_objfnarray_iter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "iter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(thisval);
    index = nn_value_asnumber(argv[0]);
    if(((int)index > -1) && index < nn_valarray_count(&list->varray))
    {
        return nn_valarray_get(&list->varray, index);
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_itern(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "itern", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(thisval);
    if(nn_value_isnull(argv[0]))
    {
        if(nn_valarray_count(&list->varray) == 0)
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
    if(index < nn_valarray_count(&list->varray) - 1)
    {
        return nn_value_makenumber((double)index + 1);
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_each(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        passi = 0;
        if(arity > 0)
        {
            passi++;
            nestargs[0] = nn_valarray_get(&list->varray, i);
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_value_makenumber(i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &unused);
    }
    return nn_value_makenull();
}


NNValue nn_objfnarray_map(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    resultlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        passi = 0;
        if(!nn_value_isnull(nn_valarray_get(&list->varray, i)))
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = nn_valarray_get(&list->varray, i);
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &res);
            nn_array_push(resultlist, res);
        }
        else
        {
            nn_array_push(resultlist, nn_value_makenull());
        }
    }
    return nn_value_fromobject(resultlist);
}


NNValue nn_objfnarray_filter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    resultlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        passi = 0;
        if(!nn_value_isnull(nn_valarray_get(&list->varray, i)))
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = nn_valarray_get(&list->varray, i);
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result);
            if(!nn_value_isfalse(result))
            {
                nn_array_push(resultlist, nn_valarray_get(&list->varray, i));
            }
        }
    }
    return nn_value_fromobject(resultlist);
}

NNValue nn_objfnarray_some(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        passi = 0;
        if(!nn_value_isnull(nn_valarray_get(&list->varray, i)))
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = nn_valarray_get(&list->varray, i);
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result);
            if(!nn_value_isfalse(result))
            {
                return nn_value_makebool(true);
            }
        }
    }
    return nn_value_makebool(false);
}


NNValue nn_objfnarray_every(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        passi = 0;
        if(!nn_value_isnull(nn_valarray_get(&list->varray, i)))
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = nn_valarray_get(&list->varray, i);
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result);
            if(nn_value_isfalse(result))
            {
                return nn_value_makebool(false);
            }
        }
    }
    return nn_value_makebool(true);
}

NNValue nn_objfnarray_reduce(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    if(nn_value_isnull(accumulator) && nn_valarray_count(&list->varray) > 0)
    {
        accumulator = nn_valarray_get(&list->varray, 0);
        startindex = 1;
    }
    arity = nn_nestcall_prepare(state, callable, thisval, NULL, 4);
    for(i = startindex; i < nn_valarray_count(&list->varray); i++)
    {
        passi = 0;
        if(!nn_value_isnull(nn_valarray_get(&list->varray, i)) && !nn_value_isnull(nn_valarray_get(&list->varray, i)))
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = accumulator;
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = nn_valarray_get(&list->varray, i);
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
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &accumulator);
        }
    }
    return accumulator;
}

NNValue nn_objfnarray_slice(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    salen = nn_valarray_count(&selfarr->varray);
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
    narr = nn_object_makearray(state);
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
        nn_array_push(narr, nn_valarray_get(&selfarr->varray, i));        
    }
    return nn_value_fromobject(narr);
}

NNValue nn_objfnrange_lower(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "lower", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asrange(thisval)->lower);
}

NNValue nn_objfnrange_upper(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "upper", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asrange(thisval)->upper);
}

NNValue nn_objfnrange_range(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "range", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asrange(thisval)->range);
}

NNValue nn_objfnrange_iter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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

NNValue nn_objfnrange_itern(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int index;
    NNObjRange* range;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "itern", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    range = nn_value_asrange(thisval);
    if(nn_value_isnull(argv[0]))
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



NNValue nn_objfnrange_expand(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int i;
    NNValue val;
    NNObjRange* range;
    NNObjArray* oa;
    (void)argv;
    (void)argc;
    range = nn_value_asrange(thisval);
    oa = nn_object_makearray(state);
    for(i = 0; i < range->range; i++)
    {
        val = nn_value_makenumber(i);
        nn_array_push(oa, val);
    }
    return nn_value_fromobject(oa);
}

NNValue nn_objfnrange_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int a;
    int b;
    NNObjRange* orng;
    (void)thisval;
    (void)argc;
    a = nn_value_asnumber(argv[0]);
    b = nn_value_asnumber(argv[1]);
    orng = nn_object_makerange(state, a, b);
    return nn_value_fromobject(orng);
}

NNValue nn_objfnstring_utf8numbytes(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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

NNValue nn_objfnstring_utf8decode(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int res;
    NNObjString* instr;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "utf8Decode", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    instr = nn_value_asstring(argv[0]);
    res = nn_util_utf8decode((const uint8_t*)instr->sbuf.data, instr->sbuf.length);
    return nn_value_makenumber(res);
}

NNValue nn_objfnstring_utf8encode(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    res = nn_string_takelen(state, buf, len);
    return nn_value_fromobject(res);
}

NNValue nn_util_stringutf8chars(NNState* state, NNValue thisval, NNValue* argv, size_t argc, bool onlycodepoint)
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
    res = nn_array_make(state);
    nn_utf8iter_init(&iter, instr->sbuf.data, instr->sbuf.length);
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
            os = nn_string_copylen(state, cstr, iter.charsize);
            nn_array_push(res, nn_value_fromobject(os));
        }
    }
    finalize:
    return nn_value_fromobject(res);
}

NNValue nn_objfnstring_utf8chars(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    return nn_util_stringutf8chars(state, thisval, argv, argc, false);
}

NNValue nn_objfnstring_utf8codepoints(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    return nn_util_stringutf8chars(state, thisval, argv, argc, true);
}


NNValue nn_objfnstring_fromcharcode(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char ch;
    NNObjString* os;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "fromCharCode", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    ch = nn_value_asnumber(argv[0]);
    os = nn_string_copylen(state, &ch, 1);
    return nn_value_fromobject(os);
}

NNValue nn_objfnstring_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* os;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "constructor", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    os = nn_string_copylen(state, "", 0);
    return nn_value_fromobject(os);
}

NNValue nn_objfnstring_length(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    NNObjString* selfstr;
    nn_argcheck_init(state, &check, "length", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    return nn_value_makenumber(selfstr->sbuf.length);
}

NNValue nn_string_fromrange(NNState* state, const char* buf, int len)
{
    NNObjString* str;
    if(len <= 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    str = nn_string_copylen(state, "", 0);
    dyn_strbuf_appendstrn(&str->sbuf, buf, len);
    return nn_value_fromobject(str);
}

NNObjString* nn_string_substring(NNState* state, NNObjString* selfstr, size_t start, size_t end, bool likejs)
{
    size_t asz;
    size_t len;
    size_t tmp;
    size_t maxlen;
    char* raw;
    (void)likejs;
    maxlen = selfstr->sbuf.length;
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
    memcpy(raw, selfstr->sbuf.data + start, len);
    return nn_string_takelen(state, raw, len);
}

NNValue nn_objfnstring_substring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    maxlen = selfstr->sbuf.length;
    end = maxlen;
    start = nn_value_asnumber(argv[0]);
    if(argc > 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        end = nn_value_asnumber(argv[1]);
    }
    nos = nn_string_substring(state, selfstr, start, end, true);
    return nn_value_fromobject(nos);
}

NNValue nn_objfnstring_charcodeat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    selflen = (int)selfstr->sbuf.length;
    if((idx < 0) || (idx >= selflen))
    {
        ch = -1;
    }
    else
    {
        ch = selfstr->sbuf.data[idx];
    }
    return nn_value_makenumber(ch);
}

NNValue nn_objfnstring_charat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    selflen = (int)selfstr->sbuf.length;
    if((idx < 0) || (idx >= selflen))
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    else
    {
        ch = selfstr->sbuf.data[idx];
    }
    return nn_value_fromobject(nn_string_copylen(state, &ch, 1));
}

NNValue nn_objfnstring_upper(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t slen;
    char* string;
    NNObjString* str;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "upper", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    str = nn_value_asstring(thisval);
    slen = str->sbuf.length;
    string = nn_util_strtoupper(str->sbuf.data, slen);
    return nn_value_fromobject(nn_string_copylen(state, string, slen));
}

NNValue nn_objfnstring_lower(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t slen;
    char* string;
    NNObjString* str;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "lower", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    str = nn_value_asstring(thisval);
    slen = str->sbuf.length;
    string = nn_util_strtolower(str->sbuf.data, slen);
    return nn_value_fromobject(nn_string_copylen(state, string, slen));
}

NNValue nn_objfnstring_isalpha(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNArgCheck check;
    NNObjString* selfstr;
    nn_argcheck_init(state, &check, "isAlpha", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    for(i = 0; i < selfstr->sbuf.length; i++)
    {
        if(!isalpha((unsigned char)selfstr->sbuf.data[i]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(selfstr->sbuf.length != 0);
}

NNValue nn_objfnstring_isalnum(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isAlnum", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    for(i = 0; i < selfstr->sbuf.length; i++)
    {
        if(!isalnum((unsigned char)selfstr->sbuf.data[i]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(selfstr->sbuf.length != 0);
}

NNValue nn_objfnstring_isfloat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    if(selfstr->sbuf.length ==0)
    {
        return nn_value_makebool(false);
    }
    f = strtod(selfstr->sbuf.data, &p);
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

NNValue nn_objfnstring_isnumber(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isNumber", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    for(i = 0; i < selfstr->sbuf.length; i++)
    {
        if(!isdigit((unsigned char)selfstr->sbuf.data[i]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(selfstr->sbuf.length != 0);
}

NNValue nn_objfnstring_islower(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    bool alphafound;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isLower", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    alphafound = false;
    for(i = 0; i < selfstr->sbuf.length; i++)
    {
        if(!alphafound && !isdigit(selfstr->sbuf.data[0]))
        {
            alphafound = true;
        }
        if(isupper(selfstr->sbuf.data[0]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(alphafound);
}

NNValue nn_objfnstring_isupper(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    bool alphafound;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isUpper", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    alphafound = false;
    for(i = 0; i < selfstr->sbuf.length; i++)
    {
        if(!alphafound && !isdigit(selfstr->sbuf.data[0]))
        {
            alphafound = true;
        }
        if(islower(selfstr->sbuf.data[0]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(alphafound);
}

NNValue nn_objfnstring_isspace(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isSpace", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    for(i = 0; i < selfstr->sbuf.length; i++)
    {
        if(!isspace((unsigned char)selfstr->sbuf.data[i]))
        {
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(selfstr->sbuf.length != 0);
}

NNValue nn_objfnstring_trim(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
        trimmer = (char)nn_value_asstring(argv[0])->sbuf.data[0];
    }
    selfstr = nn_value_asstring(thisval);
    string = selfstr->sbuf.data;
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
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
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
    return nn_value_fromobject(nn_string_copycstr(state, string));
}

NNValue nn_objfnstring_ltrim(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char* end;
    char* string;
    char trimmer;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "ltrim", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(argc == 1)
    {
        trimmer = (char)nn_value_asstring(argv[0])->sbuf.data[0];
    }
    selfstr = nn_value_asstring(thisval);
    string = selfstr->sbuf.data;
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
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
    }
    end = string + strlen(string) - 1;
    end[1] = '\0';
    return nn_value_fromobject(nn_string_copycstr(state, string));
}

NNValue nn_objfnstring_rtrim(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    char* end;
    char* string;
    char trimmer;
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "rtrim", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    trimmer = '\0';
    if(argc == 1)
    {
        trimmer = (char)nn_value_asstring(argv[0])->sbuf.data[0];
    }
    selfstr = nn_value_asstring(thisval);
    string = selfstr->sbuf.data;
    end = NULL;
    /* All spaces? */
    if(*string == 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, "", 0));
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
    return nn_value_fromobject(nn_string_copycstr(state, string));
}


NNValue nn_objfnarray_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    arr = nn_array_makefilled(state, cnt, filler);
    return nn_value_fromobject(arr);
}

NNValue nn_objfnarray_join(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    bool havejoinee;
    size_t i;
    size_t count;
    NNPrinter pr;
    NNValue ret;
    NNValue vjoinee;
    NNObjArray* selfarr;
    NNObjString* joinee;
    NNValue* list;
    selfarr = nn_value_asarray(thisval);
    joinee = NULL;
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
            joinee = nn_value_tostring(state, vjoinee);
            havejoinee = true;
        }
    }
    list = selfarr->varray.listitems;
    count = nn_valarray_count(&selfarr->varray);
    if(count == 0)
    {
        return nn_value_fromobject(nn_string_copycstr(state, ""));
    }
    nn_printer_makestackstring(state, &pr);
    for(i = 0; i < count; i++)
    {
        nn_printer_printvalue(&pr, list[i], false, true);
        if((havejoinee && (joinee != NULL)) && ((i+1) < count))
        {
            nn_printer_writestringl(&pr, joinee->sbuf.data, joinee->sbuf.length);
        }
    }
    ret = nn_value_fromobject(nn_printer_takestring(&pr));
    nn_printer_destroy(&pr);
    return ret;
}

NNValue nn_objfnstring_indexof(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int startindex;
    char* result;
    char* haystack;
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
    if(string->sbuf.length > 0 && needle->sbuf.length > 0)
    {
        haystack = string->sbuf.data;
        result = strstr(haystack + startindex, needle->sbuf.data);
        if(result != NULL)
        {
            return nn_value_makenumber((int)(result - haystack));
        }
    }
    return nn_value_makenumber(-1);
}

NNValue nn_objfnstring_startswith(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* substr;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "startsWith", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(thisval);
    substr = nn_value_asstring(argv[0]);
    if(string->sbuf.length == 0 || substr->sbuf.length == 0 || substr->sbuf.length > string->sbuf.length)
    {
        return nn_value_makebool(false);
    }
    return nn_value_makebool(memcmp(substr->sbuf.data, string->sbuf.data, substr->sbuf.length) == 0);
}

NNValue nn_objfnstring_endswith(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    if(string->sbuf.length == 0 || substr->sbuf.length == 0 || substr->sbuf.length > string->sbuf.length)
    {
        return nn_value_makebool(false);
    }
    difference = string->sbuf.length - substr->sbuf.length;
    return nn_value_makebool(memcmp(substr->sbuf.data, string->sbuf.data + difference, substr->sbuf.length) == 0);
}


/*
    //  Returns 0 on success, or -1 on invalid or unsupported regex, or -2 on not enough tokens given to parse regex.
    REMIMU_INLINE int mrx_regex_parse(
        //  Regex pattern to parse. Must be null-terminated.
        const char * pattern,       
        //  Output buffer of tokencount regex tokens
        RegexToken * tokens,
        //  Maximum allowed number of tokens to write
        int16_t * tokencount,
        // Optional bitflags.
        int32_t flags
        
    )
    
    // Returns match length, or -1 on no match, or -2 on out of memory, or -3 ifthe regex is invalid.
    REMIMU_INLINE int64_t mrx_regex_match(
        // Parsed regex to match against text.
        const RegexToken * tokens,
        // Text to match against tokens.
        const char * text,
        // index value to match at.
        size_t starti,
        // Number of allowed capture info output slots.
        uint16_t capslots,
        // Capture position info output buffer.
        int64_t* cappos,
        // Capture length info output buffer.
        int64_t* capspan
    ) 
    
*/

NNValue nn_util_stringregexmatch(NNState* state, NNObjString* string, NNObjString* pattern, bool capture)
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
    RegexContext* pctx;
    memset(tokens, 0, (matchMaxTokens+1) * sizeof(RegexToken));
    memset(caplengths, 0, (matchMaxCaptures + 1) * sizeof(int64_t));
    memset(capstarts, 0, (matchMaxCaptures + 1) * sizeof(int64_t));
    const char* strstart;
    NNObjString* rstr;
    NNObjArray* oa;
    NNObjDict* dm;
    restokens = matchMaxTokens;
    actualmaxcaptures = 0;
    pctx = mrx_context_init(tokens, restokens);
    if(capture)
    {
        actualmaxcaptures = matchMaxCaptures;
    }
    prc = mrx_regex_parse(pctx, pattern->sbuf.data, 0);
    if(prc == 0)
    {
        cpres = mrx_regex_match(pctx, string->sbuf.data, 0, actualmaxcaptures, capstarts, caplengths);
        if(cpres > 0)
        {
            if(capture)
            {
                oa = nn_object_makearray(state);
                for(i=0; i<cpres; i++)
                {
                    mtstart = capstarts[i];
                    mtlength = caplengths[i];
                    if(mtlength > 0)
                    {
                        strstart = &string->sbuf.data[mtstart];
                        rstr = nn_string_copylen(state, strstart, mtlength);
                        dm = nn_object_makedict(state);
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
        nn_except_throwclass(state, state->exceptions.regexerror, pctx->errorbuf);
    }
    mrx_context_destroy(pctx);
    if(capture)
    {
        return nn_value_makenull();
    }
    return nn_value_makebool(false);
}

NNValue nn_objfnstring_matchcapture(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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

NNValue nn_objfnstring_matchonly(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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

NNValue nn_objfnstring_count(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    if(substr->sbuf.length == 0 || string->sbuf.length == 0)
    {
        return nn_value_makenumber(0);
    }
    count = 0;
    tmp = string->sbuf.data;
    while((tmp = nn_util_utf8strstr(tmp, substr->sbuf.data)))
    {
        count++;
        tmp++;
    }
    return nn_value_makenumber(count);
}

NNValue nn_objfnstring_tonumber(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjString* selfstr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "toNumber", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    selfstr = nn_value_asstring(thisval);
    return nn_value_makenumber(strtod(selfstr->sbuf.data, NULL));
}

NNValue nn_objfnstring_isascii(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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

NNValue nn_objfnstring_tolist(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    length = string->sbuf.length;
    if(length > 0)
    {
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            nn_array_push(list, nn_value_fromobject(nn_string_copylen(state, string->sbuf.data + start, (int)(end - start))));
        }
    }
    return nn_value_fromobject(list);
}

NNValue nn_objfnstring_lpad(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t width;
    size_t fillsize;
    size_t finalsize;
    size_t finalutf8size;
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
        fillchar = ofillstr->sbuf.data[0];
    }
    if(width <= string->sbuf.length)
    {
        return thisval;
    }
    fillsize = width - string->sbuf.length;
    fill = (char*)nn_memory_malloc(sizeof(char) * ((size_t)fillsize + 1));
    finalsize = string->sbuf.length + fillsize;
    finalutf8size = string->sbuf.length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)nn_memory_malloc(sizeof(char) * ((size_t)finalsize + 1));
    memcpy(str, fill, fillsize);
    memcpy(str + fillsize, string->sbuf.data, string->sbuf.length);
    str[finalsize] = '\0';
    nn_memory_free(fill);
    result = nn_string_takelen(state, str, finalsize);
    result->sbuf.length = finalutf8size;
    result->sbuf.length = finalsize;
    return nn_value_fromobject(result);
}

NNValue nn_objfnstring_rpad(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t width;
    size_t fillsize;
    size_t finalsize;
    size_t finalutf8size;
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
        fillchar = ofillstr->sbuf.data[0];
    }
    if(width <= string->sbuf.length)
    {
        return thisval;
    }
    fillsize = width - string->sbuf.length;
    fill = (char*)nn_memory_malloc(sizeof(char) * ((size_t)fillsize + 1));
    finalsize = string->sbuf.length + fillsize;
    finalutf8size = string->sbuf.length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)nn_memory_malloc(sizeof(char) * ((size_t)finalsize + 1));
    memcpy(str, string->sbuf.data, string->sbuf.length);
    memcpy(str + string->sbuf.length, fill, fillsize);
    str[finalsize] = '\0';
    nn_memory_free(fill);
    result = nn_string_takelen(state, str, finalsize);
    result->sbuf.length = finalutf8size;
    result->sbuf.length = finalsize;
    return nn_value_fromobject(result);
}

NNValue nn_objfnstring_split(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    if(((string->sbuf.length == 0) && (delimeter->sbuf.length == 0)) || (string->sbuf.length == 0) || (delimeter->sbuf.length == 0))
    {
        return nn_value_fromobject(nn_object_makearray(state));
    }
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    if(delimeter->sbuf.length > 0)
    {
        start = 0;
        for(i = 0; i <= string->sbuf.length; i++)
        {
            /* match found. */
            if(memcmp(string->sbuf.data + i, delimeter->sbuf.data, delimeter->sbuf.length) == 0 || i == string->sbuf.length)
            {
                nn_array_push(list, nn_value_fromobject(nn_string_copylen(state, string->sbuf.data + start, i - start)));
                i += delimeter->sbuf.length - 1;
                start = i + 1;
            }
        }
    }
    else
    {
        length = string->sbuf.length;
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            nn_array_push(list, nn_value_fromobject(nn_string_copylen(state, string->sbuf.data + start, (int)(end - start))));
        }
    }
    return nn_value_fromobject(list);
}

NNValue nn_objfnstring_replace(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t totallength;
    StringBuffer* result;
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
    if((string->sbuf.length == 0 && substr->sbuf.length == 0) || string->sbuf.length == 0 || substr->sbuf.length == 0)
    {
        return nn_value_fromobject(nn_string_copylen(state, string->sbuf.data, string->sbuf.length));
    }
    result = dyn_strbuf_makebasicempty(0);
    totallength = 0;
    for(i = 0; i < string->sbuf.length; i++)
    {
        if(memcmp(string->sbuf.data + i, substr->sbuf.data, substr->sbuf.length) == 0)
        {
            if(substr->sbuf.length > 0)
            {
                dyn_strbuf_appendstrn(result, repsubstr->sbuf.data, repsubstr->sbuf.length);
            }
            i += substr->sbuf.length - 1;
            totallength += repsubstr->sbuf.length;
        }
        else
        {
            dyn_strbuf_appendchar(result, string->sbuf.data[i]);
            totallength++;
        }
    }
    return nn_value_fromobject(nn_string_makefromstrbuf(state, result, nn_util_hashstring(result->data, result->length)));
}

NNValue nn_objfnstring_iter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    length = string->sbuf.length;
    index = nn_value_asnumber(argv[0]);
    if(((int)index > -1) && (index < length))
    {
        result = nn_string_copylen(state, &string->sbuf.data[index], 1);
        return nn_value_fromobject(result);
    }
    return nn_value_makenull();
}

NNValue nn_objfnstring_itern(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    size_t length;
    NNObjString* string;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "itern", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    string = nn_value_asstring(thisval);
    length = string->sbuf.length;
    if(nn_value_isnull(argv[0]))
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

NNValue nn_objfnstring_each(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    for(i = 0; i < string->sbuf.length; i++)
    {
        passi = 0;
        if(arity > 0)
        {
            passi++;
            nestargs[0] = nn_value_fromobject(nn_string_copylen(state, string->sbuf.data + i, 1));
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_value_makenumber(i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &unused);
    }
    /* pop the argument list */
    return nn_value_makenull();
}

NNValue nn_objfnobject_dumpself(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNPrinter pr;
    NNObjString* os;
    v = thisval;
    (void)argv;
    (void)argc;
    nn_printer_makestackstring(state, &pr);
    nn_printer_printvalue(&pr, v, true, false);
    os = nn_printer_takestring(&pr);
    nn_printer_destroy(&pr);
    return nn_value_fromobject(os);
}

NNValue nn_objfnobject_tostring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNPrinter pr;
    NNObjString* os;
    (void)argv;
    (void)argc;
    v = thisval;
    nn_printer_makestackstring(state, &pr);
    nn_printer_printvalue(&pr, v, false, true);
    os = nn_printer_takestring(&pr);
    nn_printer_destroy(&pr);
    return nn_value_fromobject(os);
}

NNValue nn_objfnobject_typename(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNObjString* os;
    (void)thisval;
    (void)argc;
    v = argv[0];
    os = nn_string_copycstr(state, nn_value_typename(v));
    return nn_value_fromobject(os);
}

NNValue nn_objfnobject_getselfclass(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)argv;
    (void)argc;
    return thisval;
}

NNValue nn_objfnobject_isstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    (void)state;
    (void)argv;
    (void)argc;
    v = thisval;
    return nn_value_makebool(nn_value_isstring(v));
}

NNValue nn_objfnobject_isarray(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    (void)state;
    (void)argv;
    (void)argc;
    v = thisval;
    return nn_value_makebool(nn_value_isarray(v));
}

NNValue nn_objfnobject_isa(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNValue otherclval;
    NNObjClass* oclass;
    NNObjClass* selfclass;
    (void)state;
    (void)argc;
    v = thisval;
    otherclval = argv[0];
    if(nn_value_isclass(otherclval))
    {
        oclass = nn_value_asclass(otherclval);
        selfclass = nn_value_getclassfor(state, v);
        if(selfclass != NULL)
        {
            return nn_value_makebool(nn_util_isinstanceof(selfclass, oclass));
        }
    }
    return nn_value_makebool(false);
}

NNValue nn_objfnobject_iscallable(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
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

NNValue nn_objfnobject_isbool(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isbool(selfval));
}

NNValue nn_objfnobject_isnumber(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isnumber(selfval));
}

NNValue nn_objfnobject_isint(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isnumber(selfval) && (((int)nn_value_asnumber(selfval)) == nn_value_asnumber(selfval)));
}

NNValue nn_objfnobject_isdict(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isdict(selfval));
}

NNValue nn_objfnobject_isobject(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isobject(selfval));
}

NNValue nn_objfnobject_isfunction(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
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

NNValue nn_objfnobject_isiterable(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    bool isiterable;
    NNValue dummy;
    NNObjClass* klass;
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    isiterable = nn_value_isarray(selfval) || nn_value_isdict(selfval) || nn_value_isstring(selfval);
    if(!isiterable && nn_value_isinstance(selfval))
    {
        klass = nn_value_asinstance(selfval)->klass;
        isiterable = nn_valtable_get(&klass->instmethods, nn_value_fromobject(nn_string_copycstr(state, "@iter")), &dummy)
            && nn_valtable_get(&klass->instmethods, nn_value_fromobject(nn_string_copycstr(state, "@itern")), &dummy);
    }
    return nn_value_makebool(isiterable);
}

NNValue nn_objfnobject_isclass(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isclass(selfval));
}

NNValue nn_objfnobject_isfile(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isfile(selfval));
}

NNValue nn_objfnobject_isinstance(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isinstance(selfval));
}


NNValue nn_objfnclass_getselfname(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    NNObjClass* klass;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    klass = nn_value_asclass(selfval);
    return nn_value_fromobject(klass->name);
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
    return nn_string_copylen(state, newstr, length);
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
    //  return nn_string_copylen(state, str, length);
    */
}

NNObjString* nn_util_numbertooctstring(NNState* state, int64_t n, bool numeric)
{
    int length;
    /* assume maximum of 64 bits + 2 octal indicators (0c) */
    char str[66];
    length = sprintf(str, numeric ? "0c%lo" : "%lo", n);
    return nn_string_copylen(state, str, length);
}

NNObjString* nn_util_numbertohexstring(NNState* state, int64_t n, bool numeric)
{
    int length;
    /* assume maximum of 64 bits + 2 hex indicators (0x) */
    char str[66];
    length = sprintf(str, numeric ? "0x%lx" : "%lx", n);
    return nn_string_copylen(state, str, length);
}

NNValue nn_objfnnumber_tobinstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)argv;
    (void)argc;
    return nn_value_fromobject(nn_util_numbertobinstring(state, nn_value_asnumber(thisval)));
}

NNValue nn_objfnnumber_tooctstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)argv;
    (void)argc;
    return nn_value_fromobject(nn_util_numbertooctstring(state, nn_value_asnumber(thisval), false));
}

NNValue nn_objfnnumber_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    if(nn_value_isnull(val))
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
    nn_astlex_init(&lex, state, os->sbuf.data);
    tok = nn_astlex_scannumber(&lex);
    rtval = nn_astparser_compilestrnumber(tok.type, tok.start);
    return rtval;
}

NNValue nn_objfnnumber_tohexstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)argv;
    (void)argc;
    return nn_value_fromobject(nn_util_numbertohexstring(state, nn_value_asnumber(thisval), false));
}

NNValue nn_objfnmath_abs(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(fabs(nn_value_asnumber(argv[0])));
}

NNValue nn_objfnmath_round(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(round(nn_value_asnumber(argv[0])));
}

NNValue nn_objfnmath_sqrt(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(sqrt(nn_value_asnumber(argv[0])));
}

NNValue nn_objfnmath_ceil(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(ceil(nn_value_asnumber(argv[0])));
}

NNValue nn_objfnmath_floor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(floor(nn_value_asnumber(argv[0])));
}

NNValue nn_objfnmath_min(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    double b;
    double x;
    double y;
    (void)state;
    (void)thisval;
    (void)argc;
    x = nn_value_asnumber(argv[0]);
    y = nn_value_asnumber(argv[1]);
    b = (x < y) ? x : y;
    return nn_value_makenumber(b);
}

NNValue nn_objfnprocess_exedirectory(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)thisval;
    (void)argv;
    (void)argc;
    if(state->processinfo->cliexedirectory != NULL)
    {
        return nn_value_fromobject(state->processinfo->cliexedirectory);
    }
    return nn_value_makenull();
}

NNValue nn_objfnprocess_scriptfile(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)thisval;
    (void)argv;
    (void)argc;
    if(state->processinfo->cliscriptfile != NULL)
    {
        return nn_value_fromobject(state->processinfo->cliscriptfile);
    }
    return nn_value_makenull();
}


NNValue nn_objfnprocess_scriptdirectory(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)thisval;
    (void)argv;
    (void)argc;
    if(state->processinfo->cliscriptdirectory != NULL)
    {
        return nn_value_fromobject(state->processinfo->cliscriptdirectory);
    }
    return nn_value_makenull();
}


NNValue nn_objfnprocess_exit(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int rc;
    (void)state;
    (void)thisval;
    rc = 0;
    if(argc > 0)
    {
        rc = nn_value_asnumber(argv[0]);
    }
    exit(rc);
    return nn_value_makenull();
}

NNValue nn_objfnprocess_kill(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int pid;
    int code;
    (void)state;
    (void)thisval;
    (void)argc;
    pid = nn_value_asnumber(argv[0]);
    code = nn_value_asnumber(argv[1]);
    osfn_kill(pid, code);
    return nn_value_makenull();
}

NNValue nn_objfnjson_stringify(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNPrinter pr;
    NNObjString* os;
    (void)thisval;
    (void)argc;
    v = argv[0];
    nn_printer_makestackstring(state, &pr);
    pr.jsonmode = true;
    nn_printer_printvalue(&pr, v, true, false);
    os = nn_printer_takestring(&pr);
    nn_printer_destroy(&pr);
    return nn_value_fromobject(os);
}

void nn_state_installmethods(NNState* state, NNObjClass* klass, NNConstClassMethodItem* listmethods)
{
    int i;
    const char* rawname;
    NNNativeFN rawfn;
    NNObjString* osname;
    for(i=0; listmethods[i].name != NULL; i++)
    {
        rawname = listmethods[i].name;
        rawfn = listmethods[i].fn;
        osname = nn_string_copycstr(state, rawname);
        nn_class_defnativemethod(klass, osname, rawfn);
    }
}

void nn_state_initbuiltinmethods(NNState* state)
{
    NNObjClass* klass;
    {
        klass = state->classprimprocess;
        nn_class_setstaticproperty(klass, nn_string_copycstr(state, "directory"), nn_value_fromobject(state->processinfo->cliexedirectory));
        nn_class_setstaticproperty(klass, nn_string_copycstr(state, "env"), nn_value_fromobject(state->envdict));
        nn_class_setstaticproperty(klass, nn_string_copycstr(state, "stdin"), nn_value_fromobject(state->processinfo->filestdin));
        nn_class_setstaticproperty(klass, nn_string_copycstr(state, "stdout"), nn_value_fromobject(state->processinfo->filestdout));
        nn_class_setstaticproperty(klass, nn_string_copycstr(state, "stderr"), nn_value_fromobject(state->processinfo->filestderr));
        nn_class_setstaticproperty(klass, nn_string_copycstr(state, "pid"), nn_value_makenumber(state->processinfo->cliprocessid));
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "kill"), nn_objfnprocess_kill);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "exit"), nn_objfnprocess_exit);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "exedirectory"), nn_objfnprocess_exedirectory);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "scriptdirectory"), nn_objfnprocess_scriptdirectory);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "script"), nn_objfnprocess_scriptfile);
    }
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
            {NULL, NULL},
        };
        nn_class_defstaticnativemethod(state->classprimobject, nn_string_copycstr(state, "typename"), nn_objfnobject_typename);
        nn_class_defstaticcallablefield(state->classprimobject, nn_string_copycstr(state, "prototype"), nn_objfnobject_getselfclass);
        nn_state_installmethods(state, state->classprimobject, objectmethods);
    }
    {
        nn_class_defstaticcallablefield(state->classprimclass, nn_string_copycstr(state, "name"), nn_objfnclass_getselfname);

    }    
    {
        static NNConstClassMethodItem numbermethods[] =
        {
            {"toHexString", nn_objfnnumber_tohexstring},
            {"toOctString", nn_objfnnumber_tooctstring},
            {"toBinString", nn_objfnnumber_tobinstring},
            {NULL, NULL},
        };
        nn_class_defnativeconstructor(state->classprimnumber, nn_objfnnumber_constructor);
        nn_state_installmethods(state, state->classprimnumber, numbermethods);
    }
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
            {NULL, NULL},
        };
        nn_class_defnativeconstructor(state->classprimstring, nn_objfnstring_constructor);
        nn_class_defstaticnativemethod(state->classprimstring, nn_string_copycstr(state, "fromCharCode"), nn_objfnstring_fromcharcode);
        nn_class_defstaticnativemethod(state->classprimstring, nn_string_copycstr(state, "utf8Decode"), nn_objfnstring_utf8decode);
        nn_class_defstaticnativemethod(state->classprimstring, nn_string_copycstr(state, "utf8Encode"), nn_objfnstring_utf8encode);
        nn_class_defstaticnativemethod(state->classprimstring, nn_string_copycstr(state, "utf8NumBytes"), nn_objfnstring_utf8numbytes);
        nn_class_defcallablefield(state->classprimstring, nn_string_copycstr(state, "length"), nn_objfnstring_length);
        nn_state_installmethods(state, state->classprimstring, stringmethods);
    }
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
            {NULL, NULL}
        };
        nn_class_defnativeconstructor(state->classprimarray, nn_objfnarray_constructor);
        nn_class_defcallablefield(state->classprimarray, nn_string_copycstr(state, "length"), nn_objfnarray_length);
        nn_state_installmethods(state, state->classprimarray, arraymethods);
    }
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
            {NULL, NULL},
        };
        #if 0
        nn_class_defnativeconstructor(state->classprimdict, nn_objfndict_constructor);
        nn_class_defstaticnativemethod(state->classprimdict, nn_string_copycstr(state, "keys"), nn_objfndict_keys);
        #endif
        nn_state_installmethods(state, state->classprimdict, dictmethods);
    }
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
            {"printf", nn_objfnfile_printf},
            {"number", nn_objfnfile_number},
            {"isTTY", nn_objfnfile_istty},
            {"isOpen", nn_objfnfile_isopen},
            {"isClosed", nn_objfnfile_isclosed},
            {"flush", nn_objfnfile_flush},
            {"stats", nn_objfnfile_statmethod},
            {"path", nn_objfnfile_path},
            {"seek", nn_objfnfile_seek},
            {"tell", nn_objfnfile_tell},
            {"mode", nn_objfnfile_mode},
            {"name", nn_objfnfile_name},
            {"readLine", nn_objfnfile_readline},
            {NULL, NULL},
        };
        nn_class_defnativeconstructor(state->classprimfile, nn_objfnfile_constructor);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "read"), nn_objfnfile_readstatic);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "write"), nn_objfnfile_writestatic);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "put"), nn_objfnfile_writestatic);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "exists"), nn_objfnfile_exists);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "isFile"), nn_objfnfile_isfile);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "isDirectory"), nn_objfnfile_isdirectory);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "stat"), nn_objfnfile_statstatic);
        nn_state_installmethods(state, state->classprimfile, filemethods);
    }
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
            {NULL, NULL},
        };
        nn_class_defnativeconstructor(state->classprimrange, nn_objfnrange_constructor);
        nn_state_installmethods(state, state->classprimrange, rangemethods);
    }
    {
        klass = nn_util_makeclass(state, "Math", state->classprimobject);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "abs"), nn_objfnmath_abs);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "round"), nn_objfnmath_round);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "sqrt"), nn_objfnmath_sqrt);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "ceil"), nn_objfnmath_ceil);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "floor"), nn_objfnmath_floor);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "min"), nn_objfnmath_min);
    }
    {
        klass = nn_util_makeclass(state, "JSON", state->classprimobject);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "stringify"), nn_objfnjson_stringify);
    }
}

NNValue nn_nativefn_time(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    struct timeval tv;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "time", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    osfn_gettimeofday(&tv, NULL);
    return nn_value_makenumber((double)tv.tv_sec + ((double)tv.tv_usec / 10000000));
}

NNValue nn_nativefn_microtime(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    struct timeval tv;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "microtime", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    osfn_gettimeofday(&tv, NULL);
    return nn_value_makenumber((1000000 * (double)tv.tv_sec) + ((double)tv.tv_usec / 10));
}

NNValue nn_nativefn_id(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue val;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "id", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    val = argv[0];
    return nn_value_makenumber(*(long*)&val);
}

NNValue nn_nativefn_int(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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

NNValue nn_nativefn_chr(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    return nn_value_fromobject(nn_string_takelen(state, string, len));
}

NNValue nn_nativefn_ord(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    length = string->sbuf.length;
    if(length > 1)
    {
        NEON_RETURNERROR("ord() expects character as argument, string given");
    }
    ord = (int)string->sbuf.data[0];
    if(ord < 0)
    {
        ord += 256;
    }
    return nn_value_makenumber(ord);
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
        osfn_gettimeofday(&tv, NULL);
        nn_util_mtseed((uint32_t)(1000000 * tv.tv_sec + tv.tv_usec), mtstate, &mtindex);
    }
    randval = nn_util_mtgenerate(mtstate, &mtindex);
    randnum = lowerlimit + ((double)randval / UINT32_MAX) * (upperlimit - lowerlimit);
    return randnum;
}

NNValue nn_nativefn_rand(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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

NNValue nn_nativefn_eval(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue result;
    NNObjString* os;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "eval", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    os = nn_value_asstring(argv[0]);
    /*fprintf(stderr, "eval:src=%s\n", os->sbuf.data);*/
    result = nn_state_evalsource(state, os->sbuf.data);
    return result;
}

/*
NNValue nn_nativefn_loadfile(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue result;
    NNObjString* os;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "loadfile", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    os = nn_value_asstring(argv[0]);
    fprintf(stderr, "eval:src=%s\n", os->sbuf.data);
    result = nn_state_evalsource(state, os->sbuf.data);
    return result;
}
*/

NNValue nn_nativefn_instanceof(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "instanceof", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isinstance);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isclass);
    return nn_value_makebool(nn_util_isinstanceof(nn_value_asinstance(argv[0])->klass, nn_value_asclass(argv[1])));
}


void nn_strformat_init(NNState* state, NNFormatInfo* nfi, NNPrinter* writer, const char* fmtstr, size_t fmtlen)
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
                nn_printer_writechar(nfi->writer, '%');
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
                            nn_printer_printvalue(nfi->writer, cval, true, true);
                        }
                        break;
                    case 'c':
                        {
                            ival = (int)nn_value_asnumber(cval);
                            nn_printer_printf(nfi->writer, "%c", ival);
                        }
                        break;
                    /* TODO: implement actual field formatting */
                    case 's':
                    case 'd':
                    case 'i':
                    case 'g':
                        {
                            nn_printer_printvalue(nfi->writer, cval, false, true);
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
            nn_printer_writechar(nfi->writer, ch);
        }
    }
    return ok;
}

NNValue nn_nativefn_sprintf(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNFormatInfo nfi;
    NNPrinter pr;
    NNObjString* res;
    NNObjString* ofmt;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "sprintf", argv, argc);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = nn_value_asstring(argv[0]);
    nn_printer_makestackstring(state, &pr);
    nn_strformat_init(state, &nfi, &pr, nn_string_getcstr(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, argc, 1, argv))
    {
        return nn_value_makenull();
    }
    res = nn_printer_takestring(&pr);
    nn_printer_destroy(&pr);
    return nn_value_fromobject(res);
}

NNValue nn_nativefn_printf(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNFormatInfo nfi;
    NNObjString* ofmt;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "printf", argv, argc);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = nn_value_asstring(argv[0]);
    nn_strformat_init(state, &nfi, state->stdoutprinter, nn_string_getcstr(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, argc, 1, argv))
    {
    }
    return nn_value_makenull();
}

NNValue nn_nativefn_print(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    (void)thisval;
    for(i = 0; i < argc; i++)
    {
        nn_printer_printvalue(state->stdoutprinter, argv[i], false, true);
    }
    return nn_value_makenull();
}

NNValue nn_nativefn_println(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    v = nn_nativefn_print(state, thisval, argv, argc);
    nn_printer_writestring(state->stdoutprinter, "\n");
    return v;
}


NNValue nn_nativefn_isnan(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argv;
    (void)argc;
    return nn_value_makebool(false);
}


void nn_state_initbuiltinfunctions(NNState* state)
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
    nn_state_defnativefunction(state, "isNaN", nn_nativefn_isnan);
}

void nn_state_vwarn(NNState* state, const char* fmt, va_list va)
{
    if(state->conf.enablewarnings)
    {
        fprintf(stderr, "WARNING: ");
        vfprintf(stderr, fmt, va);
        fprintf(stderr, "\n");
    }
}

void nn_state_warn(NNState* state, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    nn_state_vwarn(state, fmt, va);
    va_end(va);
}

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
    NNPrinter pr;
    oa = nn_object_makearray(state);
    {
        for(i = 0; i < state->vmstate.framecount; i++)
        {
            nn_printer_makestackstring(state, &pr);
            frame = &state->vmstate.framevalues[i];
            function = frame->closure->fnclosure.scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->fnscriptfunc.blob.instrucs - 1;
            line = function->fnscriptfunc.blob.instrucs[instruction].srcline;
            physfile = "(unknown)";
            if(function->fnscriptfunc.module->physicalpath != NULL)
            {
                physfile = function->fnscriptfunc.module->physicalpath->sbuf.data;
            }
            fnname = "<script>";
            if(function->name != NULL)
            {
                fnname = function->name->sbuf.data;
            }
            nn_printer_printf(&pr, "from %s() in %s:%d", fnname, physfile, line);
            os = nn_printer_takestring(&pr);
            nn_printer_destroy(&pr);
            nn_array_push(oa, nn_value_fromobject(os));
            if((i > 15) && (state->conf.showfullstack == false))
            {
                nn_printer_makestackstring(state, &pr);
                nn_printer_printf(&pr, "(only upper 15 entries shown)");
                os = nn_printer_takestring(&pr);
                nn_printer_destroy(&pr);
                nn_array_push(oa, nn_value_fromobject(os));
                break;
            }
        }
        return nn_value_fromobject(oa);
    }
    return nn_value_fromobject(nn_string_copylen(state, "", 0));
}

bool nn_except_propagate(NNState* state)
{
    int i;
    int cnt;
    int srcline;
/*
{
    NEON_COLOR_RESET,
    NEON_COLOR_RED,
    NEON_COLOR_GREEN,
    NEON_COLOR_YELLOW,
    NEON_COLOR_BLUE,
    NEON_COLOR_MAGENTA,
    NEON_COLOR_CYAN
}
*/
    const char* colred;
    const char* colreset;
    const char* colyellow;
    const char* srcfile;
    NNValue stackitm;
    NNObjArray* oa;
    NNObjFunction* function;
    NNExceptionFrame* handler;
    NNObjString* emsg;
    NNObjInstance* exception;
    NNProperty* field;
    exception = nn_value_asinstance(nn_vm_stackpeek(state, 0));
    while(state->vmstate.framecount > 0)
    {
        state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
        for(i = state->vmstate.currentframe->handlercount; i > 0; i--)
        {
            handler = &state->vmstate.currentframe->handlers[i - 1];
            function = state->vmstate.currentframe->closure->fnclosure.scriptfunc;
            if(handler->address != 0 && nn_util_isinstanceof(exception->klass, handler->klass))
            {
                state->vmstate.currentframe->inscode = &function->fnscriptfunc.blob.instrucs[handler->address];
                return true;
            }
            else if(handler->finallyaddress != 0)
            {
                /* continue propagating once the 'finally' block completes */
                nn_vm_stackpush(state, nn_value_makebool(true));
                state->vmstate.currentframe->inscode = &function->fnscriptfunc.blob.instrucs[handler->finallyaddress];
                return true;
            }
        }
        state->vmstate.framecount--;
    }
    colred = nn_util_color(NEON_COLOR_RED);
    colreset = nn_util_color(NEON_COLOR_RESET);
    colyellow = nn_util_color(NEON_COLOR_YELLOW);
    /* at this point, the exception is unhandled; so, print it out. */
    fprintf(stderr, "%sunhandled %s%s", colred, exception->klass->name->sbuf.data, colreset);
    srcfile = "none";
    srcline = 0;
    field = nn_valtable_getfieldbycstr(&exception->properties, "srcline");
    if(field != NULL)
    {
        srcline = nn_value_asnumber(field->value);
    }
    field = nn_valtable_getfieldbycstr(&exception->properties, "srcfile");
    if(field != NULL)
    {
        srcfile = nn_value_asstring(field->value)->sbuf.data;
    }
    fprintf(stderr, " [from native %s%s:%d%s]", colyellow, srcfile, srcline, colreset);
    
    field = nn_valtable_getfieldbycstr(&exception->properties, "message");
    if(field != NULL)
    {
        emsg = nn_value_tostring(state, field->value);
        if(emsg->sbuf.length > 0)
        {
            fprintf(stderr, ": %s", emsg->sbuf.data);
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
    field = nn_valtable_getfieldbycstr(&exception->properties, "stacktrace");
    if(field != NULL)
    {
        fprintf(stderr, "  stacktrace:\n");
        oa = nn_value_asarray(field->value);
        cnt = nn_valarray_count(&oa->varray);
        i = cnt-1;
        if(cnt > 0)
        {
            while(true)
            {
                stackitm = nn_valarray_get(&oa->varray, i);
                nn_printer_printf(state->debugwriter, "  ");
                nn_printer_printvalue(state->debugwriter, stackitm, false, true);
                nn_printer_printf(state->debugwriter, "\n");
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

bool nn_except_pushhandler(NNState* state, NNObjClass* type, int address, int finallyaddress)
{
    NNCallFrame* frame;
    frame = &state->vmstate.framevalues[state->vmstate.framecount - 1];
    if(frame->handlercount == NEON_CONFIG_MAXEXCEPTHANDLERS)
    {
        nn_vm_raisefatalerror(state, "too many nested exception handlers in one function");
        return false;
    }
    frame->handlers[frame->handlercount].address = address;
    frame->handlers[frame->handlercount].finallyaddress = finallyaddress;
    frame->handlers[frame->handlercount].klass = type;
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
    instance = nn_except_makeinstance(state, exklass, srcfile, srcline, nn_string_takelen(state, message, length));
    nn_vm_stackpush(state, nn_value_fromobject(instance));
    stacktrace = nn_except_getstacktrace(state);
    nn_vm_stackpush(state, stacktrace);
    nn_instance_defproperty(instance, nn_string_copycstr(state, "stacktrace"), stacktrace);
    nn_vm_stackpop(state);
    return nn_except_propagate(state);
}

NNInstruction nn_util_makeinst(bool isop, uint8_t code, int srcline)
{
    NNInstruction inst;
    inst.isop = isop;
    inst.code = code;
    inst.srcline = srcline;
    return inst;
}

NNObjClass* nn_except_makeclass(NNState* state, NNObjModule* module, const char* cstrname, bool iscs)
{
    int messageconst;
    NNObjClass* klass;
    NNObjString* classname;
    NNObjFunction* function;
    NNObjFunction* closure;
    if(iscs)
    {
        classname = nn_string_copycstr(state, cstrname);
    }
    else
    {
        classname = nn_string_copycstr(state, cstrname);
    }
    nn_vm_stackpush(state, nn_value_fromobject(classname));
    klass = nn_object_makeclass(state, classname, state->classprimobject);
    nn_vm_stackpop(state);
    nn_vm_stackpush(state, nn_value_fromobject(klass));
    function = nn_object_makefuncscript(state, module, NEON_FNCONTEXTTYPE_METHOD);
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
        messageconst = nn_blob_pushconst(&function->fnscriptfunc.blob, nn_value_fromobject(nn_string_copycstr(state, "message")));
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
    closure = nn_object_makefuncclosure(state, function);
    nn_vm_stackpop(state);
    /* set class constructor */
    nn_vm_stackpush(state, nn_value_fromobject(closure));
    nn_valtable_set(&klass->instmethods, nn_value_fromobject(classname), nn_value_fromobject(closure));
    klass->constructor = nn_value_fromobject(closure);
    /* set class properties */
    nn_class_defproperty(klass, nn_string_copycstr(state, "message"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_copycstr(state, "stacktrace"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_copycstr(state, "srcfile"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_copycstr(state, "srcline"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_copycstr(state, "class"), nn_value_fromobject(klass));
    nn_valtable_set(&state->declaredglobals, nn_value_fromobject(classname), nn_value_fromobject(klass));
    /* for class */
    nn_vm_stackpop(state);
    nn_vm_stackpop(state);
    /* assert error name */
    /* nn_vm_stackpop(state); */
    return klass;
}

NNObjInstance* nn_except_makeinstance(NNState* state, NNObjClass* exklass, const char* srcfile, int srcline, NNObjString* message)
{
    NNObjInstance* instance;
    NNObjString* osfile;
    instance = nn_object_makeinstance(state, exklass);
    osfile = nn_string_copycstr(state, srcfile);
    nn_vm_stackpush(state, nn_value_fromobject(instance));
    nn_instance_defproperty(instance, nn_string_copycstr(state, "class"), nn_value_fromobject(exklass));
    nn_instance_defproperty(instance, nn_string_copycstr(state, "message"), nn_value_fromobject(message));
    nn_instance_defproperty(instance, nn_string_copycstr(state, "srcfile"), nn_value_fromobject(osfile));
    nn_instance_defproperty(instance, nn_string_copycstr(state, "srcline"), nn_value_makenumber(srcline));
    nn_vm_stackpop(state);
    return instance;
}

void nn_vm_raisefatalerror(NNState* state, const char* format, ...)
{
    int i;
    int line;
    size_t instruction;
    va_list args;
    NNCallFrame* frame;
    NNObjFunction* function;
    /* flush out anything on stdout first */
    fflush(stdout);
    frame = &state->vmstate.framevalues[state->vmstate.framecount - 1];
    function = frame->closure->fnclosure.scriptfunc;
    instruction = frame->inscode - function->fnscriptfunc.blob.instrucs - 1;
    line = function->fnscriptfunc.blob.instrucs[instruction].srcline;
    fprintf(stderr, "RuntimeError: ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, " -> %s:%d ", function->fnscriptfunc.module->physicalpath->sbuf.data, line);
    fputs("\n", stderr);
    if(state->vmstate.framecount > 1)
    {
        fprintf(stderr, "stacktrace:\n");
        for(i = state->vmstate.framecount - 1; i >= 0; i--)
        {
            frame = &state->vmstate.framevalues[i];
            function = frame->closure->fnclosure.scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->fnscriptfunc.blob.instrucs - 1;
            fprintf(stderr, "    %s:%d -> ", function->fnscriptfunc.module->physicalpath->sbuf.data, function->fnscriptfunc.blob.instrucs[instruction].srcline);
            if(function->name == NULL)
            {
                fprintf(stderr, "<script>");
            }
            else
            {
                fprintf(stderr, "%s()", function->name->sbuf.data);
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
    oname = nn_string_copycstr(state, name);
    nn_vm_stackpush(state, nn_value_fromobject(oname));
    nn_vm_stackpush(state, val);
    r = nn_valtable_set(&state->declaredglobals, state->vmstate.stackvalues[0], state->vmstate.stackvalues[1]);
    nn_vm_stackpopn(state, 2);
    return r;
}

bool nn_state_defnativefunctionptr(NNState* state, const char* name, NNNativeFN fptr, void* uptr)
{
    NNObjFunction* func;
    func = nn_object_makefuncnative(state, fptr, name, uptr);
    return nn_state_defglobalvalue(state, name, nn_value_fromobject(func));
}

bool nn_state_defnativefunction(NNState* state, const char* name, NNNativeFN fptr)
{
    return nn_state_defnativefunctionptr(state, name, fptr, NULL);
}

NNObjClass* nn_util_makeclass(NNState* state, const char* name, NNObjClass* parent)
{
    NNObjClass* cl;
    NNObjString* os;
    os = nn_string_copycstr(state, name);
    cl = nn_object_makeclass(state, os, parent);
    nn_valtable_set(&state->declaredglobals, nn_value_fromobject(os), nn_value_fromobject(cl));
    return cl;
}

void nn_vm_initvmstate(NNState* state)
{
    state->vmstate.linkedobjects = NULL;
    state->vmstate.currentframe = NULL;
    {
        state->vmstate.stackcapacity = NEON_CONFIG_INITSTACKCOUNT;
        state->vmstate.stackvalues = (NNValue*)nn_memory_malloc(NEON_CONFIG_INITSTACKCOUNT * sizeof(NNValue));
        if(state->vmstate.stackvalues == NULL)
        {
            fprintf(stderr, "error: failed to allocate stackvalues!\n");
            abort();
        }
        memset(state->vmstate.stackvalues, 0, NEON_CONFIG_INITSTACKCOUNT * sizeof(NNValue));
    }
    {
        state->vmstate.framecapacity = NEON_CONFIG_INITFRAMECOUNT;
        state->vmstate.framevalues = (NNCallFrame*)nn_memory_malloc(NEON_CONFIG_INITFRAMECOUNT * sizeof(NNCallFrame));
        if(state->vmstate.framevalues == NULL)
        {
            fprintf(stderr, "error: failed to allocate framevalues!\n");
            abort();
        }
        memset(state->vmstate.framevalues, 0, NEON_CONFIG_INITFRAMECOUNT * sizeof(NNCallFrame));
    }
}


/*
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
bool nn_vm_resizestack(NNState* state, NNObjFunction* closure, size_t needed)
{
    size_t oldsz;
    size_t newsz;
    size_t allocsz;
    size_t nforvals;
    NNValue* oldbuf;
    NNValue* newbuf;
    nforvals = (needed * 2);
    oldsz = state->vmstate.stackcapacity;
    newsz = oldsz + nforvals;
    allocsz = ((newsz + 1) * sizeof(NNValue));
    if(nn_util_unlikely(state->conf.enableapidebug))
    {
        if(closure != NULL)
        {
            nn_vm_resizeinfo(state, "stack", closure, needed);
        }
        fprintf(stderr, "*** resizing stack: needed %ld, from %ld to %ld, allocating %ld ***\n", (long)nforvals, (long)oldsz, (long)newsz, (long)allocsz);
    }
    oldbuf = state->vmstate.stackvalues;
    newbuf = (NNValue*)nn_memory_realloc(oldbuf, allocsz);
    if(newbuf == NULL)
    {
        fprintf(stderr, "internal error: failed to resize stackvalues!\n");
        abort();
    }
    state->vmstate.stackvalues = (NNValue*)newbuf;
    state->vmstate.stackcapacity = newsz;
    return true;
}

bool nn_vm_resizeframes(NNState* state, NNObjFunction* closure, size_t needed)
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
        if(closure != NULL)
        {
            nn_vm_resizeinfo(state, "frames", closure, needed);
        }
        fprintf(stderr, "*** resizing frames ***\n");
    }
    oldclosure = state->vmstate.currentframe->closure;
    oldip = state->vmstate.currentframe->inscode;
    oldhandlercnt = state->vmstate.currentframe->handlercount;
    oldsz = state->vmstate.framecapacity;
    newsz = oldsz + needed;
    allocsz = ((newsz + 1) * sizeof(NNCallFrame));
    #if 1
        oldbuf = state->vmstate.framevalues;
        newbuf = (NNCallFrame*)nn_memory_realloc(oldbuf, allocsz);
        if(newbuf == NULL)
        {
            fprintf(stderr, "internal error: failed to resize framevalues!\n");
            abort();
        }
    #endif
    state->vmstate.framevalues = (NNCallFrame*)newbuf;
    state->vmstate.framecapacity = newsz;
    /*
    * this bit is crucial: realloc changes pointer addresses, and to keep the
    * current frame, re-read it from the new address.
    */
    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
    state->vmstate.currentframe->handlercount = oldhandlercnt;
    state->vmstate.currentframe->inscode = oldip;
    state->vmstate.currentframe->closure = oldclosure;
    return true;
}

bool nn_vm_checkmayberesize(NNState* state)
{
    NNObjFunction* closure;
    closure = NULL;
    if(state->vmstate.currentframe != NULL)
    {
        closure = state->vmstate.currentframe->closure;
    }
    if((state->vmstate.stackidx+1) >= state->vmstate.stackcapacity)
    {
        if(!nn_vm_resizestack(state, closure, state->vmstate.stackidx + 1))
        {
            return nn_except_throw(state, "failed to resize stack due to overflow");
        }
        return true;
    }
    if(state->vmstate.framecount >= state->vmstate.framecapacity)
    {
        if(!nn_vm_resizeframes(state, closure, state->vmstate.framecapacity + 1))
        {
            return nn_except_throw(state, "failed to resize frames due to overflow");
        }
        return true;
    }
    return false;
}

void nn_vm_resizeinfo(NNState* state, const char* context, NNObjFunction* closure, size_t needed)
{
    const char* name;
    (void)state;
    (void)needed;
    name = "unknown";
    if(closure->fnclosure.scriptfunc != NULL)
    {
        if(closure->fnclosure.scriptfunc->name != NULL)
        {
            if(closure->fnclosure.scriptfunc->name->sbuf.data != NULL)
            {
                name = closure->fnclosure.scriptfunc->name->sbuf.data; 
            }
        }
    }
    fprintf(stderr, "resizing %s for closure %s\n", context, name);
}


void nn_state_resetvmstate(NNState* state)
{
    state->vmstate.framecount = 0;
    state->vmstate.stackidx = 0;
    state->vmstate.openupvalues = NULL;
}

bool nn_state_addsearchpathobj(NNState* state, NNObjString* os)
{
    nn_valarray_push(&state->importpath, nn_value_fromobject(os));
    return true;
}

bool nn_state_addsearchpath(NNState* state, const char* path)
{
    return nn_state_addsearchpathobj(state, nn_string_copycstr(state, path));
}

void nn_state_buildprocessinfo(NNState* state)
{
    enum{ kMaxBuf = 1024 };
    char* pathp;
    char pathbuf[kMaxBuf];
    state->processinfo = (NNProcessInfo*)nn_memory_malloc(sizeof(NNProcessInfo));
    state->processinfo->cliscriptfile = NULL;
    state->processinfo->cliscriptdirectory = NULL;
    state->processinfo->cliargv = nn_object_makearray(state);
    {
        pathp = osfn_getcwd(pathbuf, kMaxBuf);
        if(pathp == NULL)
        {
            pathp = (char*)".";
        }
        fprintf(stderr, "pathp=<%s>\n", pathp);
        state->processinfo->cliexedirectory = nn_string_copycstr(state, pathp);
    }
    {
        state->processinfo->cliprocessid = osfn_getpid();
    }
    {
        {
            state->processinfo->filestdout = nn_object_makefile(state, stdout, true, "<stdout>", "wb");
            nn_state_defglobalvalue(state, "STDOUT", nn_value_fromobject(state->processinfo->filestdout));
        }
        {
            state->processinfo->filestderr = nn_object_makefile(state, stderr, true, "<stderr>", "wb");
            nn_state_defglobalvalue(state, "STDERR", nn_value_fromobject(state->processinfo->filestderr));
        }
        {
            state->processinfo->filestdin = nn_object_makefile(state, stdin, true, "<stdin>", "rb");
            nn_state_defglobalvalue(state, "STDIN", nn_value_fromobject(state->processinfo->filestdin));
        }
    }
}

void nn_state_updateprocessinfo(NNState* state)
{
    char* prealpath;
    char* prealdir;
    if(state->rootphysfile != NULL)
    {
        prealpath = osfn_realpath(state->rootphysfile, NULL);
        prealdir = osfn_dirname(prealpath);
        state->processinfo->cliscriptfile = nn_string_copycstr(state, prealpath);
        state->processinfo->cliscriptdirectory = nn_string_copycstr(state, prealdir);
        nn_memory_free(prealpath);
        nn_memory_free(prealdir);
    }
}

bool nn_state_makestack(NNState* pstate)
{
    return nn_state_makewithuserptr(pstate, NULL);
}

NNState* nn_state_makealloc()
{
    NNState* state;
    state = (NNState*)nn_memory_malloc(sizeof(NNState));
    if(state == NULL)
    {
        return NULL;
    }
    if(!nn_state_makewithuserptr(state, NULL))
    {
        return NULL;
    }
    return state;
}


bool nn_state_makewithuserptr(NNState* pstate, void* userptr)
{
    static const char* defaultsearchpaths[] =
    {
        "mods",
        "mods/@/index" NEON_CONFIG_FILEEXT,
        ".",
        NULL
    };
    size_t i;
    if(pstate == NULL)
    {
        return false;
    }
    memset(pstate, 0, sizeof(NNState));
    pstate->memuserptr = userptr;
    pstate->exceptions.stdexception = NULL;
    pstate->rootphysfile = NULL;
    pstate->processinfo = NULL;
    pstate->isrepl = false;
    pstate->markvalue = true;
    nn_vm_initvmstate(pstate);
    nn_state_resetvmstate(pstate);
    {
        pstate->conf.enablestrictmode = false;
        pstate->conf.shoulddumpstack = false;
        pstate->conf.enablewarnings = false;
        pstate->conf.dumpbytecode = false;
        pstate->conf.exitafterbytecode = false;
        pstate->conf.showfullstack = false;
        pstate->conf.enableapidebug = false;
        pstate->conf.maxsyntaxerrors = NEON_CONFIG_MAXSYNTAXERRORS;
    }
    {
        pstate->gcstate.bytesallocated = 0;
        /* default is 1mb. Can be modified via the -g flag. */
        pstate->gcstate.nextgc = NEON_CONFIG_DEFAULTGCSTART;
        pstate->gcstate.graycount = 0;
        pstate->gcstate.graycapacity = 0;
        pstate->gcstate.graystack = NULL;
        pstate->lastreplvalue = nn_value_makenull();
    }
    {
        pstate->stdoutprinter = nn_printer_makeio(pstate, stdout, false);
        pstate->stdoutprinter->shouldflush = true;
        pstate->stderrprinter = nn_printer_makeio(pstate, stderr, false);
        pstate->debugwriter = nn_printer_makeio(pstate, stderr, false);
        pstate->debugwriter->shortenvalues = true;
        pstate->debugwriter->maxvallength = 15;
    }
    {
        nn_valtable_init(pstate, &pstate->openedmodules);
        nn_valtable_init(pstate, &pstate->allocatedstrings);
        nn_valtable_init(pstate, &pstate->declaredglobals);
    }
    {
        pstate->topmodule = nn_module_make(pstate, "", "<state>", false, true);
        pstate->constructorname = nn_string_copycstr(pstate, "constructor");
    }
    {
        nn_valarray_init(pstate, &pstate->importpath);
        for(i=0; defaultsearchpaths[i]!=NULL; i++)
        {
            nn_state_addsearchpath(pstate, defaultsearchpaths[i]);
        }
    }
    {
        pstate->classprimclass = nn_util_makeclass(pstate, "Class", NULL);
        pstate->classprimobject = nn_util_makeclass(pstate, "Object", pstate->classprimclass);
        pstate->classprimnumber = nn_util_makeclass(pstate, "Number", pstate->classprimobject);
        pstate->classprimstring = nn_util_makeclass(pstate, "String", pstate->classprimobject);
        pstate->classprimarray = nn_util_makeclass(pstate, "Array", pstate->classprimobject);
        pstate->classprimdict = nn_util_makeclass(pstate, "Dict", pstate->classprimobject);
        pstate->classprimfile = nn_util_makeclass(pstate, "File", pstate->classprimobject);
        pstate->classprimrange = nn_util_makeclass(pstate, "Range", pstate->classprimobject);
        pstate->classprimcallable = nn_util_makeclass(pstate, "Function", pstate->classprimobject);
        pstate->classprimprocess = nn_util_makeclass(pstate, "Process", pstate->classprimobject);
    }
    {
        pstate->envdict = nn_object_makedict(pstate);
    }
    {
        if(pstate->exceptions.stdexception == NULL)
        {
            pstate->exceptions.stdexception = nn_except_makeclass(pstate, NULL, "Exception", true);
        }
        pstate->exceptions.asserterror = nn_except_makeclass(pstate, NULL, "AssertError", true);
        pstate->exceptions.syntaxerror = nn_except_makeclass(pstate, NULL, "SyntaxError", true);
        pstate->exceptions.ioerror = nn_except_makeclass(pstate, NULL, "IOError", true);
        pstate->exceptions.oserror = nn_except_makeclass(pstate, NULL, "OSError", true);
        pstate->exceptions.argumenterror = nn_except_makeclass(pstate, NULL, "ArgumentError", true);
        pstate->exceptions.regexerror = nn_except_makeclass(pstate, NULL, "RegexError", true);
    }
    nn_state_buildprocessinfo(pstate);
    nn_state_addsearchpathobj(pstate, pstate->processinfo->cliexedirectory);
    {
        nn_state_initbuiltinfunctions(pstate);
        nn_state_initbuiltinmethods(pstate);
    }
    return true;
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
    nn_valarray_destroy(&state->importpath, false);
    destrdebug("destroying linked objects...");
    nn_gcmem_destroylinkedobjects(state);
    /* since object in module can exist in declaredglobals, it must come before */
    destrdebug("destroying module table...");
    nn_valtable_destroy(&state->openedmodules);
    destrdebug("destroying globals table...");
    nn_valtable_destroy(&state->declaredglobals);
    destrdebug("destroying strings table...");
    nn_valtable_destroy(&state->allocatedstrings);
    destrdebug("destroying stdoutprinter...");
    nn_printer_destroy(state->stdoutprinter);
    destrdebug("destroying stderrprinter...");
    nn_printer_destroy(state->stderrprinter);
    destrdebug("destroying debugwriter...");
    nn_printer_destroy(state->debugwriter);
    destrdebug("destroying framevalues...");
    nn_memory_free(state->vmstate.framevalues);
    destrdebug("destroying stackvalues...");
    nn_memory_free(state->vmstate.stackvalues);
    nn_memory_free(state->processinfo);
    destrdebug("destroying state...");
    if(!onstack)
    {
        nn_memory_free(state);
    }
    destrdebug("done destroying!");
}

bool nn_util_methodisprivate(NNObjString* name)
{
    return name->sbuf.length > 0 && name->sbuf.data[0] == '_';
}

bool nn_vm_callclosure(NNState* state, NNObjFunction* closure, NNValue thisval, int argcount)
{
    int i;
    int startva;
    NNCallFrame* frame;
    NNObjArray* argslist;
    (void)thisval;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    /* fill empty parameters if not variadic */
    for(; !closure->fnclosure.scriptfunc->fnscriptfunc.isvariadic && argcount < closure->fnclosure.scriptfunc->fnscriptfunc.arity; argcount++)
    {
        nn_vm_stackpush(state, nn_value_makenull());
    }
    /* handle variadic arguments... */
    if(closure->fnclosure.scriptfunc->fnscriptfunc.isvariadic && argcount >= closure->fnclosure.scriptfunc->fnscriptfunc.arity - 1)
    {
        startva = argcount - closure->fnclosure.scriptfunc->fnscriptfunc.arity;
        argslist = nn_object_makearray(state);
        nn_vm_stackpush(state, nn_value_fromobject(argslist));
        for(i = startva; i >= 0; i--)
        {
            nn_valarray_push(&argslist->varray, nn_vm_stackpeek(state, i + 1));
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
            return nn_except_throw(state, "expected at least %d arguments but got %d", closure->fnclosure.scriptfunc->fnscriptfunc.arity - 1, argcount);
        }
        else
        {
            return nn_except_throw(state, "expected %d arguments but got %d", closure->fnclosure.scriptfunc->fnscriptfunc.arity, argcount);
        }
    }
    if(nn_vm_checkmayberesize(state))
    {
        #if 0
            nn_vm_stackpopn(state, argcount);
        #endif
    }
    frame = &state->vmstate.framevalues[state->vmstate.framecount++];
    frame->closure = closure;
    frame->inscode = closure->fnclosure.scriptfunc->fnscriptfunc.blob.instrucs;
    frame->stackslotpos = state->vmstate.stackidx + (-argcount - 1);
    return true;
}

bool nn_vm_callnative(NNState* state, NNObjFunction* native, NNValue thisval, int argcount)
{
    size_t spos;
    NNValue r;
    NNValue* vargs;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    spos = state->vmstate.stackidx + (-argcount);
    vargs = &state->vmstate.stackvalues[spos];
    r = native->fnnativefunc.natfunc(state, thisval, vargs, argcount);
    {
        state->vmstate.stackvalues[spos - 1] = r;
        state->vmstate.stackidx -= argcount;
    }
    nn_gcmem_clearprotect(state);
    return true;
}

bool nn_vm_callvaluewithobject(NNState* state, NNValue callable, NNValue thisval, int argcount)
{
    size_t spos;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    if(nn_value_isobject(callable))
    {
        switch(nn_value_objtype(callable))
        {
            case NEON_OBJTYPE_FUNCBOUND:
                {
                    NNObjFunction* bound;
                    bound = nn_value_asfunction(callable);
                    spos = (state->vmstate.stackidx + (-argcount - 1));
                    state->vmstate.stackvalues[spos] = thisval;
                    return nn_vm_callclosure(state, bound->fnmethod.method, thisval, argcount);
                }
                break;
            case NEON_OBJTYPE_CLASS:
                {
                    NNObjClass* klass;
                    klass = nn_value_asclass(callable);
                    spos = (state->vmstate.stackidx + (-argcount - 1));
                    state->vmstate.stackvalues[spos] = thisval;
                    if(!nn_value_isnull(klass->constructor))
                    {
                        return nn_vm_callvaluewithobject(state, klass->constructor, thisval, argcount);
                    }
                    else if(klass->superclass != NULL && !nn_value_isnull(klass->superclass->constructor))
                    {
                        return nn_vm_callvaluewithobject(state, klass->superclass->constructor, thisval, argcount);
                    }
                    else if(argcount != 0)
                    {
                        return nn_except_throw(state, "%s constructor expects 0 arguments, %d given", klass->name->sbuf.data, argcount);
                    }
                    return true;
                }
                break;
            case NEON_OBJTYPE_MODULE:
                {
                    NNObjModule* module;
                    NNProperty* field;
                    module = nn_value_asmodule(callable);
                    field = nn_valtable_getfieldbyostr(&module->deftable, module->name);
                    if(field != NULL)
                    {
                        return nn_vm_callvalue(state, field->value, thisval, argcount);
                    }
                    return nn_except_throw(state, "module %s does not export a default function", module->name);
                }
                break;
            case NEON_OBJTYPE_FUNCCLOSURE:
                {
                    return nn_vm_callclosure(state, nn_value_asfunction(callable), thisval, argcount);
                }
                break;
            case NEON_OBJTYPE_FUNCNATIVE:
                {
                    return nn_vm_callnative(state, nn_value_asfunction(callable), thisval, argcount);
                }
                break;
            default:
                break;
        }
    }
    return nn_except_throw(state, "object of type %s is not callable", nn_value_typename(callable));
}

bool nn_vm_callvalue(NNState* state, NNValue callable, NNValue thisval, int argcount)
{
    NNValue actualthisval;
    if(nn_value_isobject(callable))
    {
        switch(nn_value_objtype(callable))
        {
            case NEON_OBJTYPE_FUNCBOUND:
                {
                    NNObjFunction* bound;
                    bound = nn_value_asfunction(callable);
                    actualthisval = bound->fnmethod.receiver;
                    if(!nn_value_isnull(thisval))
                    {
                        actualthisval = thisval;
                    }
                    NEON_APIDEBUG(state, "actualthisval.type=%s, argcount=%d", nn_value_typename(actualthisval), argcount);
                    return nn_vm_callvaluewithobject(state, callable, actualthisval, argcount);
                }
                break;
            case NEON_OBJTYPE_CLASS:
                {
                    NNObjClass* klass;
                    NNObjInstance* instance;
                    klass = nn_value_asclass(callable);
                    instance = nn_object_makeinstance(state, klass);
                    actualthisval = nn_value_fromobject(instance);
                    if(!nn_value_isnull(thisval))
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

NNFuncContextType nn_value_getmethodtype(NNValue method)
{
    switch(nn_value_objtype(method))
    {
        case NEON_OBJTYPE_FUNCNATIVE:
            return nn_value_asfunction(method)->contexttype;
        case NEON_OBJTYPE_FUNCCLOSURE:
            return nn_value_asfunction(method)->fnclosure.scriptfunc->contexttype;
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
    return NULL;
}

NEON_FORCEINLINE void nn_vmbits_stackpush(NNState* state, NNValue value)
{
    nn_vm_checkmayberesize(state);
    state->vmstate.stackvalues[state->vmstate.stackidx] = value;
    state->vmstate.stackidx++;
}

void nn_vm_stackpush(NNState* state, NNValue value)
{
    nn_vmbits_stackpush(state, value);
}

NEON_FORCEINLINE NNValue nn_vmbits_stackpop(NNState* state)
{
    NNValue v;
    state->vmstate.stackidx--;
    if(state->vmstate.stackidx < 0)
    {
        state->vmstate.stackidx = 0;
    }
    v = state->vmstate.stackvalues[state->vmstate.stackidx];
    return v;
}

NNValue nn_vm_stackpop(NNState* state)
{
    return nn_vmbits_stackpop(state);
}

NEON_FORCEINLINE NNValue nn_vmbits_stackpopn(NNState* state, int n)
{
    NNValue v;
    state->vmstate.stackidx -= n;
    if(state->vmstate.stackidx < 0)
    {
        state->vmstate.stackidx = 0;
    }
    v = state->vmstate.stackvalues[state->vmstate.stackidx];
    return v;
}

NNValue nn_vm_stackpopn(NNState* state, int n)
{
    return nn_vmbits_stackpopn(state, n);
}

NEON_FORCEINLINE NNValue nn_vmbits_stackpeek(NNState* state, int distance)
{
    NNValue v;
    v = state->vmstate.stackvalues[state->vmstate.stackidx + (-1 - distance)];
    return v;

}

NNValue nn_vm_stackpeek(NNState* state, int distance)
{
    return nn_vmbits_stackpeek(state, distance);
}

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

NEON_FORCEINLINE uint8_t nn_vmbits_readbyte(NNState* state)
{
    uint8_t r;
    r = state->vmstate.currentframe->inscode->code;
    state->vmstate.currentframe->inscode++;
    return r;
}

NEON_FORCEINLINE NNInstruction nn_vmbits_readinstruction(NNState* state)
{
    NNInstruction r;
    r = *state->vmstate.currentframe->inscode;
    state->vmstate.currentframe->inscode++;
    return r;
}

NEON_FORCEINLINE uint16_t nn_vmbits_readshort(NNState* state)
{
    uint8_t b;
    uint8_t a;
    a = state->vmstate.currentframe->inscode[0].code;
    b = state->vmstate.currentframe->inscode[1].code;
    state->vmstate.currentframe->inscode += 2;
    return (uint16_t)((a << 8) | b);
}

NEON_FORCEINLINE NNValue nn_vmbits_readconst(NNState* state)
{
    uint16_t idx;
    idx = nn_vmbits_readshort(state);
    return nn_valarray_get(&state->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.blob.constants, idx);
}

NEON_FORCEINLINE NNObjString* nn_vmbits_readstring(NNState* state)
{
    return nn_value_asstring(nn_vmbits_readconst(state));
}

NEON_FORCEINLINE bool nn_vmutil_invokemethodfromclass(NNState* state, NNObjClass* klass, NNObjString* name, int argcount)
{
    NNProperty* field;
    NEON_APIDEBUG(state, "argcount=%d", argcount);
    field = nn_valtable_getfieldbyostr(&klass->instmethods, name);
    if(field != NULL)
    {
        if(nn_value_getmethodtype(field->value) == NEON_FNCONTEXTTYPE_PRIVATE)
        {
            return nn_except_throw(state, "cannot call private method '%s' from instance of %s", name->sbuf.data, klass->name->sbuf.data);
        }
        return nn_vm_callvaluewithobject(state, field->value, nn_value_fromobject(klass), argcount);
    }
    return nn_except_throw(state, "undefined method '%s' in %s", name->sbuf.data, klass->name->sbuf.data);
}

NEON_FORCEINLINE bool nn_vmutil_invokemethodself(NNState* state, NNObjString* name, int argcount)
{
    size_t spos;
    NNValue receiver;
    NNObjInstance* instance;
    NNProperty* field;
    NEON_APIDEBUG(state, "argcount=%d", argcount);
    receiver = nn_vmbits_stackpeek(state, argcount);
    if(nn_value_isinstance(receiver))
    {
        instance = nn_value_asinstance(receiver);
        field = nn_valtable_getfieldbyostr(&instance->klass->instmethods, name);
        if(field != NULL)
        {
            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
        }
        field = nn_valtable_getfieldbyostr(&instance->properties, name);
        if(field != NULL)
        {
            spos = (state->vmstate.stackidx + (-argcount - 1));
            state->vmstate.stackvalues[spos] = receiver;
            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
        }
    }
    else if(nn_value_isclass(receiver))
    {
        field = nn_valtable_getfieldbyostr(&nn_value_asclass(receiver)->instmethods, name);
        if(field != NULL)
        {
            if(nn_value_getmethodtype(field->value) == NEON_FNCONTEXTTYPE_STATIC)
            {
                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
            }
            return nn_except_throw(state, "cannot call non-static method %s() on non instance", name->sbuf.data);
        }
    }
    return nn_except_throw(state, "cannot call method '%s' on object of type '%s'", name->sbuf.data, nn_value_typename(receiver));
}

NEON_FORCEINLINE bool nn_vmutil_invokemethodnormal(NNState* state, NNObjString* name, int argcount)
{
    size_t spos;
    NNObjType rectype;
    NNValue receiver;
    NNProperty* field;
    NNObjClass* klass;
    receiver = nn_vmbits_stackpeek(state, argcount);
    NEON_APIDEBUG(state, "receiver.type=%s, argcount=%d", nn_value_typename(receiver), argcount);
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
                    field = nn_valtable_getfieldbyostr(&module->deftable, name);
                    if(field != NULL)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            return nn_except_throw(state, "cannot call private module method '%s'", name->sbuf.data);
                        }
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                    }
                    return nn_except_throw(state, "module %s does not define class or method %s()", module->name, name->sbuf.data);
                }
                break;
            case NEON_OBJTYPE_CLASS:
                {
                    NEON_APIDEBUG(state, "receiver is a class");
                    klass = nn_value_asclass(receiver);
                    field = nn_class_getstaticproperty(klass, name);
                    if(field != NULL)
                    {
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                    }
                    field = nn_class_getstaticmethodfield(klass, name);
                    if(field != NULL)
                    {
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
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
                        field = nn_valtable_getfieldbyostr(&klass->instmethods, name);
                        if(field != NULL)
                        {
                            fntyp = nn_value_getmethodtype(field->value);
                            fprintf(stderr, "fntyp: %d\n", fntyp);
                            if(fntyp == NEON_FNCONTEXTTYPE_PRIVATE)
                            {
                                return nn_except_throw(state, "cannot call private method %s() on %s", name->sbuf.data, klass->name->sbuf.data);
                            }
                            if(fntyp == NEON_FNCONTEXTTYPE_STATIC)
                            {
                                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                            }
                        }
                    }
                    #endif
                    return nn_except_throw(state, "unknown method %s() in class %s", name->sbuf.data, klass->name->sbuf.data);
                }
            case NEON_OBJTYPE_INSTANCE:
                {
                    NNObjInstance* instance;
                    NEON_APIDEBUG(state, "receiver is an instance");
                    instance = nn_value_asinstance(receiver);
                    field = nn_valtable_getfieldbyostr(&instance->properties, name);
                    if(field != NULL)
                    {
                        spos = (state->vmstate.stackidx + (-argcount - 1));
                        state->vmstate.stackvalues[spos] = receiver;
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                    }
                    return nn_vmutil_invokemethodfromclass(state, instance->klass, name, argcount);
                }
                break;
            case NEON_OBJTYPE_DICT:
                {
                    NEON_APIDEBUG(state, "receiver is a dictionary");
                    field = nn_class_getmethodfield(state->classprimdict, name);
                    if(field != NULL)
                    {
                        return nn_vm_callnative(state, nn_value_asfunction(field->value), receiver, argcount);
                    }
                    /* NEW in v0.0.84, dictionaries can declare extra methods as part of their entries. */
                    else
                    {
                        field = nn_valtable_getfieldbyostr(&nn_value_asdict(receiver)->htab, name);
                        if(field != NULL)
                        {
                            if(nn_value_iscallable(field->value))
                            {
                                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                            }
                        }
                    }
                    return nn_except_throw(state, "'dict' has no method %s()", name->sbuf.data);
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
        return nn_except_throw(state, "non-object %s has no method named '%s'", nn_value_typename(receiver), name->sbuf.data);
    }
    field = nn_class_getmethodfield(klass, name);
    if(field != NULL)
    {
        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
    }
    return nn_except_throw(state, "'%s' has no method %s()", klass->name->sbuf.data, name->sbuf.data);
}

NEON_FORCEINLINE bool nn_vmutil_bindmethod(NNState* state, NNObjClass* klass, NNObjString* name)
{
    NNValue val;
    NNProperty* field;
    NNObjFunction* bound;
    field = nn_valtable_getfieldbyostr(&klass->instmethods, name);
    if(field != NULL)
    {
        if(nn_value_getmethodtype(field->value) == NEON_FNCONTEXTTYPE_PRIVATE)
        {
            return nn_except_throw(state, "cannot get private property '%s' from instance", name->sbuf.data);
        }
        val = nn_vmbits_stackpeek(state, 0);
        bound = nn_object_makefuncbound(state, val, nn_value_asfunction(field->value));
        nn_vmbits_stackpop(state);
        nn_vmbits_stackpush(state, nn_value_fromobject(bound));
        return true;
    }
    return nn_except_throw(state, "undefined property '%s'", name->sbuf.data);
}

NEON_FORCEINLINE NNObjUpvalue* nn_vmutil_upvaluescapture(NNState* state, NNValue* local, int stackpos)
{
    NNObjUpvalue* upvalue;
    NNObjUpvalue* prevupvalue;
    NNObjUpvalue* createdupvalue;
    prevupvalue = NULL;
    upvalue = state->vmstate.openupvalues;
    while(upvalue != NULL && (&upvalue->location) > local)
    {
        prevupvalue = upvalue;
        upvalue = upvalue->next;
    }
    if(upvalue != NULL && (&upvalue->location) == local)
    {
        return upvalue;
    }
    createdupvalue = nn_object_makeupvalue(state, local, stackpos);
    createdupvalue->next = upvalue;
    if(prevupvalue == NULL)
    {
        state->vmstate.openupvalues = createdupvalue;
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
    while(state->vmstate.openupvalues != NULL && (&state->vmstate.openupvalues->location) >= last)
    {
        upvalue = state->vmstate.openupvalues;
        upvalue->closed = upvalue->location;
        upvalue->location = upvalue->closed;
        state->vmstate.openupvalues = upvalue->next;
    }
}

NEON_FORCEINLINE void nn_vmutil_definemethod(NNState* state, NNObjString* name)
{
    NNValue method;
    NNObjClass* klass;
    method = nn_vmbits_stackpeek(state, 0);
    klass = nn_value_asclass(nn_vmbits_stackpeek(state, 1));
    nn_valtable_set(&klass->instmethods, nn_value_fromobject(name), method);
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

bool nn_value_isfalse(NNValue value)
{
    if(nn_value_isbool(value))
    {
        return nn_value_isbool(value) && !nn_value_asbool(value);
    }
    if(nn_value_isnull(value) || nn_value_isnull(value))
    {
        return true;
    }
    /* -1 is the number equivalent of false */
    if(nn_value_isnumber(value))
    {
        return nn_value_asnumber(value) < 0;
    }
    /* Non-empty strings are true, empty strings are false.*/
    if(nn_value_isstring(value))
    {
        return nn_value_asstring(value)->sbuf.length < 1;
    }
    /* Non-empty lists are true, empty lists are false.*/
    if(nn_value_isarray(value))
    {
        return nn_valarray_count(&nn_value_asarray(value)->varray) == 0;
    }
    /* Non-empty dicts are true, empty dicts are false. */
    if(nn_value_isdict(value))
    {
        return nn_valarray_count(&nn_value_asdict(value)->names) == 0;
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

bool nn_util_isinstanceof(NNObjClass* klass1, NNObjClass* expected)
{
    size_t klen;
    size_t elen;
    const char* kname;
    const char* ename;
    while(klass1 != NULL)
    {
        elen = expected->name->sbuf.length;
        klen = klass1->name->sbuf.length;
        ename = expected->name->sbuf.data;
        kname = klass1->name->sbuf.data;
        if(elen == klen && memcmp(kname, ename, klen) == 0)
        {
            return true;
        }
        klass1 = klass1->superclass;
    }
    return false;
}

bool nn_dict_setentry(NNObjDict* dict, NNValue key, NNValue value)
{
    NNValue tempvalue;
    if(!nn_valtable_get(&dict->htab, key, &tempvalue))
    {
        /* add key if it doesn't exist. */
        nn_valarray_push(&dict->names, key);
    }
    return nn_valtable_set(&dict->htab, key, value);
}

void nn_dict_addentry(NNObjDict* dict, NNValue key, NNValue value)
{
    nn_dict_setentry(dict, key, value);
}

void nn_dict_addentrycstr(NNObjDict* dict, const char* ckey, NNValue value)
{
    NNObjString* os;
    NNState* state;
    state = ((NNObject*)dict)->pstate;
    os = nn_string_copycstr(state, ckey);
    nn_dict_addentry(dict, nn_value_fromobject(os), value);
}

NNProperty* nn_dict_getentry(NNObjDict* dict, NNValue key)
{
    return nn_valtable_getfield(&dict->htab, key);
}


NNObjDict* nn_dict_copy(NNObjDict* dict)
{

    size_t i;
    size_t dsz;
    NNValue key;
    NNProperty* field;
    NNObjDict *ndict;
    NNState* state;
    state = ((NNObject*)dict)->pstate;    
    ndict = nn_object_makedict(state);
    /*
    // @TODO: Figure out how to handle dictionary values correctly
    // remember that copying keys is redundant and unnecessary
    */
    dsz = nn_valarray_count(&dict->names);
    for(i = 0; i < dsz; i++)
    {
        key = nn_valarray_get(&dict->names, i);
        field = nn_valtable_getfield(&dict->htab, nn_valarray_get(&dict->names, i));
        nn_valarray_push(&ndict->names, key);
        nn_valtable_setwithtype(&ndict->htab, key, field->value, field->type, nn_value_isstring(key));
        
    }
    return ndict;
}

NEON_FORCEINLINE NNObjString* nn_vmutil_multiplystring(NNState* state, NNObjString* str, double number)
{
    size_t i;
    size_t times;
    NNPrinter pr;
    NNObjString* os;
    times = (size_t)number;
    /* 'str' * 0 == '', 'str' * -1 == '' */
    if(times <= 0)
    {
        return nn_string_copylen(state, "", 0);
    }
    /* 'str' * 1 == 'str' */
    else if(times == 1)
    {
        return str;
    }
    nn_printer_makestackstring(state, &pr);
    for(i = 0; i < times; i++)
    {
        nn_printer_writestringl(&pr, str->sbuf.data, str->sbuf.length);
    }
    os = nn_printer_takestring(&pr);
    nn_printer_destroy(&pr);
    return os;
}

NEON_FORCEINLINE NNObjArray* nn_vmutil_combinearrays(NNState* state, NNObjArray* a, NNObjArray* b)
{
    size_t i;
    NNObjArray* list;
    list = nn_object_makearray(state);
    nn_vmbits_stackpush(state, nn_value_fromobject(list));
    for(i = 0; i < nn_valarray_count(&a->varray); i++)
    {
        nn_valarray_push(&list->varray, nn_valarray_get(&a->varray, i));
    }
    for(i = 0; i < nn_valarray_count(&b->varray); i++)
    {
        nn_valarray_push(&list->varray, nn_valarray_get(&b->varray, i));
    }
    nn_vmbits_stackpop(state);
    return list;
}

NEON_FORCEINLINE void nn_vmutil_multiplyarray(NNState* state, NNObjArray* from, NNObjArray* newlist, size_t times)
{
    size_t i;
    size_t j;
    (void)state;
    for(i = 0; i < times; i++)
    {
        for(j = 0; j < nn_valarray_count(&from->varray); j++)
        {
            nn_valarray_push(&newlist->varray, nn_valarray_get(&from->varray, j));
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
    if(!(nn_value_isnull(vallower) || nn_value_isnumber(vallower)) || !(nn_value_isnumber(valupper) || nn_value_isnull(valupper)))
    {
        nn_vmbits_stackpopn(state, 2);
        return nn_except_throw(state, "list range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower), nn_value_typename(valupper));
    }
    idxlower = 0;
    if(nn_value_isnumber(vallower))
    {
        idxlower = nn_value_asnumber(vallower);
    }
    if(nn_value_isnull(valupper))
    {
        idxupper = nn_valarray_count(&list->varray);
    }
    else
    {
        idxupper = nn_value_asnumber(valupper);
    }
    if((idxlower < 0) || ((idxupper < 0) && ((long)(nn_valarray_count(&list->varray) + idxupper) < 0)) || (idxlower >= (long)nn_valarray_count(&list->varray)))
    {
        /* always return an empty list... */
        if(!willassign)
        {
            /* +1 for the list itself */
            nn_vmbits_stackpopn(state, 3);
        }
        nn_vmbits_stackpush(state, nn_value_fromobject(nn_object_makearray(state)));
        return true;
    }
    if(idxupper < 0)
    {
        idxupper = nn_valarray_count(&list->varray) + idxupper;
    }
    if(idxupper > (long)nn_valarray_count(&list->varray))
    {
        idxupper = nn_valarray_count(&list->varray);
    }
    newlist = nn_object_makearray(state);
    nn_vmbits_stackpush(state, nn_value_fromobject(newlist));
    for(i = idxlower; i < idxupper; i++)
    {
        nn_valarray_push(&newlist->varray, nn_valarray_get(&list->varray, i));
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
    if(!(nn_value_isnull(vallower) || nn_value_isnumber(vallower)) || !(nn_value_isnumber(valupper) || nn_value_isnull(valupper)))
    {
        nn_vmbits_stackpopn(state, 2);
        return nn_except_throw(state, "string range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower), nn_value_typename(valupper));
    }
    length = string->sbuf.length;
    idxlower = 0;
    if(nn_value_isnumber(vallower))
    {
        idxlower = nn_value_asnumber(vallower);
    }
    if(nn_value_isnull(valupper))
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
        nn_vmbits_stackpush(state, nn_value_fromobject(nn_string_copylen(state, "", 0)));
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
    nn_vmbits_stackpush(state, nn_value_fromobject(nn_string_copylen(state, string->sbuf.data + start, end - start)));
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
        return nn_except_throw(state, "cannot range index object of type %s", nn_value_typename(vfrom));
    }
    return true;
}

NEON_FORCEINLINE bool nn_vmutil_doindexgetdict(NNState* state, NNObjDict* dict, bool willassign)
{
    NNValue vindex;
    NNProperty* field;
    vindex = nn_vmbits_stackpeek(state, 0);
    field = nn_dict_getentry(dict, vindex);
    if(field != NULL)
    {
        if(!willassign)
        {
            /* we can safely get rid of the index from the stack */
            nn_vmbits_stackpopn(state, 2);
        }
        nn_vmbits_stackpush(state, field->value);
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
    if(nn_valtable_get(&module->deftable, vindex, &result))
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
    return nn_except_throw(state, "%s is undefined in module %s", nn_value_tostring(state, vindex)->sbuf.data, module->name);
}

NEON_FORCEINLINE bool nn_vmutil_doindexgetstring(NNState* state, NNObjString* string, bool willassign)
{
    int end;
    int start;
    int index;
    int maxlength;
    int realindex;
    NNValue vindex;
    NNObjRange* rng;
    (void)realindex;
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
    maxlength = string->sbuf.length;
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
            nn_vmbits_stackpopn(state, 2);
        }
        nn_vmbits_stackpush(state, nn_value_fromobject(nn_string_copylen(state, string->sbuf.data + start, end - start)));
        return true;
    }
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
        index = nn_valarray_count(&list->varray) + index;
    }
    if((index < (long)nn_valarray_count(&list->varray)) && (index >= 0))
    {
        finalval = nn_valarray_get(&list->varray, index);
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

NEON_FORCEINLINE bool nn_vmdo_indexget(NNState* state)
{
    bool isgotten;
    uint8_t willassign;
    NNValue peeked;
    willassign = nn_vmbits_readbyte(state);
    isgotten = true;
    peeked = nn_vmbits_stackpeek(state, 1);
    if(nn_util_likely(nn_value_isobject(peeked)))
    {
        switch(nn_value_asobject(peeked)->type)
        {
            case NEON_OBJTYPE_STRING:
            {
                if(!nn_vmutil_doindexgetstring(state, nn_value_asstring(peeked), willassign))
                {
                    return false;
                }
                break;
            }
            case NEON_OBJTYPE_ARRAY:
            {
                if(!nn_vmutil_doindexgetarray(state, nn_value_asarray(peeked), willassign))
                {
                    return false;
                }
                break;
            }
            case NEON_OBJTYPE_DICT:
            {
                if(!nn_vmutil_doindexgetdict(state, nn_value_asdict(peeked), willassign))
                {
                    return false;
                }
                break;
            }
            case NEON_OBJTYPE_MODULE:
            {
                if(!nn_vmutil_doindexgetmodule(state, nn_value_asmodule(peeked), willassign))
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
        nn_except_throw(state, "cannot index object of type %s", nn_value_typename(peeked));
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
    nn_valtable_set(&module->deftable, index, value);
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
    ocap = nn_valarray_capacity(&list->varray);
    ocnt = nn_valarray_count(&list->varray);
    rawpos = nn_value_asnumber(index);
    position = rawpos;
    if(rawpos < 0)
    {
        rawpos = nn_valarray_count(&list->varray) + rawpos;
    }
    if(position < ocap && position > -(ocap))
    {
        nn_valarray_set(&list->varray, position, value);
        if(position >= ocnt)
        {
            nn_valarray_increaseby(&list->varray, 1);
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
        vasz = nn_valarray_count(&list->varray);
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
        fprintf(stderr, "setting value at position %ld (array count: %ld)\n", (long)position, (long)nn_valarray_count(&list->varray));
    }
    nn_valarray_set(&list->varray, position, value);
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
    oslen = os->sbuf.length;
    position = rawpos;
    if(rawpos < 0)
    {
        position = (oslen + rawpos);
    }
    if(position < oslen && position > -oslen)
    {
        os->sbuf.data[position] = iv;
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
        dyn_strbuf_appendchar(&os->sbuf, iv);
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
    NNValue target;
    isset = true;
    target = nn_vmbits_stackpeek(state, 2);
    if(nn_util_likely(nn_value_isobject(target)))
    {
        value = nn_vmbits_stackpeek(state, 0);
        index = nn_vmbits_stackpeek(state, 1);
        switch(nn_value_asobject(target)->type)
        {
            case NEON_OBJTYPE_ARRAY:
                {
                    if(!nn_vmutil_doindexsetarray(state, nn_value_asarray(target), index, value))
                    {
                        return false;
                    }
                }
                break;
            case NEON_OBJTYPE_STRING:
                {
                    if(!nn_vmutil_dosetindexstring(state, nn_value_asstring(target), index, value))
                    {
                        return false;
                    }
                }
                break;
            case NEON_OBJTYPE_DICT:
                {
                    return nn_vmutil_dosetindexdict(state, nn_value_asdict(target), index, value);
                }
                break;
            case NEON_OBJTYPE_MODULE:
                {
                    return nn_vmutil_dosetindexmodule(state, nn_value_asmodule(target), index, value);
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
        return nn_except_throw(state, "type of %s is not a valid iterable", nn_value_typename(nn_vmbits_stackpeek(state, 3)));
    }
    return true;
}

NEON_FORCEINLINE bool nn_vmutil_concatenate(NNState* state)
{
    NNValue vleft;
    NNValue vright;
    NNPrinter pr;
    NNObjString* result;
    vright = nn_vmbits_stackpeek(state, 0);
    vleft = nn_vmbits_stackpeek(state, 1);
    nn_printer_makestackstring(state, &pr);
    nn_printer_printvalue(&pr, vleft, false, true);
    nn_printer_printvalue(&pr, vright, false, true);
    result = nn_printer_takestring(&pr);
    nn_printer_destroy(&pr);
    nn_vmbits_stackpopn(state, 2);
    nn_vmbits_stackpush(state, nn_value_fromobject(result));
    return true;
}

NEON_FORCEINLINE double nn_vmutil_floordiv(double a, double b)
{
    int d;
    d = (int)a / (int)b;
    return d - ((d * b == a) & ((a < 0) ^ (b < 0)));
}

NEON_FORCEINLINE double nn_vmutil_modulo(double a, double b)
{
    double r;
    r = fmod(a, b);
    if(r != 0 && ((r < 0) != (b < 0)))
    {
        r += b;
    }
    return r;
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
                field = nn_valtable_getfieldbyostr(&module->deftable, name);
                if(field != NULL)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        nn_except_throw(state, "cannot get private module property '%s'", name->sbuf.data);
                        return NULL;
                    }
                    return field;
                }
                nn_except_throw(state, "%s module does not define '%s'", module->name, name->sbuf.data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                field = nn_valtable_getfieldbyostr(&nn_value_asclass(peeked)->instmethods, name);
                if(field != NULL)
                {
                    if(nn_value_getmethodtype(field->value) == NEON_FNCONTEXTTYPE_STATIC)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            nn_except_throw(state, "cannot call private property '%s' of class %s", name->sbuf.data,
                                nn_value_asclass(peeked)->name->sbuf.data);
                            return NULL;
                        }
                        return field;
                    }
                }
                else
                {
                    field = nn_class_getstaticproperty(nn_value_asclass(peeked), name);
                    if(field != NULL)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            nn_except_throw(state, "cannot call private property '%s' of class %s", name->sbuf.data,
                                nn_value_asclass(peeked)->name->sbuf.data);
                            return NULL;
                        }
                        return field;
                    }
                }
                nn_except_throw(state, "class %s does not have a static property or method named '%s'",
                    nn_value_asclass(peeked)->name->sbuf.data, name->sbuf.data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                NNObjInstance* instance;
                instance = nn_value_asinstance(peeked);
                field = nn_valtable_getfieldbyostr(&instance->properties, name);
                if(field != NULL)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        nn_except_throw(state, "cannot call private property '%s' from instance of %s", name->sbuf.data, instance->klass->name->sbuf.data);
                        return NULL;
                    }
                    return field;
                }
                if(nn_util_methodisprivate(name))
                {
                    nn_except_throw(state, "cannot bind private property '%s' to instance of %s", name->sbuf.data, instance->klass->name->sbuf.data);
                    return NULL;
                }
                if(nn_vmutil_bindmethod(state, instance->klass, name))
                {
                    return field;
                }
                nn_except_throw(state, "instance of class %s does not have a property or method named '%s'",
                    nn_value_asinstance(peeked)->klass->name->sbuf.data, name->sbuf.data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                field = nn_class_getpropertyfield(state->classprimstring, name);
                if(field != NULL)
                {
                    return field;
                }
                nn_except_throw(state, "class String has no named property '%s'", name->sbuf.data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                field = nn_class_getpropertyfield(state->classprimarray, name);
                if(field != NULL)
                {
                    return field;
                }
                nn_except_throw(state, "class Array has no named property '%s'", name->sbuf.data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_RANGE:
            {
                field = nn_class_getpropertyfield(state->classprimrange, name);
                if(field != NULL)
                {
                    return field;
                }
                nn_except_throw(state, "class Range has no named property '%s'", name->sbuf.data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                field = nn_valtable_getfieldbyostr(&nn_value_asdict(peeked)->htab, name);
                if(field == NULL)
                {
                    field = nn_class_getpropertyfield(state->classprimdict, name);
                }
                if(field != NULL)
                {
                    return field;
                }
                nn_except_throw(state, "unknown key or class Dict property '%s'", name->sbuf.data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                field = nn_class_getpropertyfield(state->classprimfile, name);
                if(field != NULL)
                {
                    return field;
                }
                nn_except_throw(state, "class File has no named property '%s'", name->sbuf.data);
                return NULL;
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
        case NEON_OBJTYPE_FUNCCLOSURE:
        case NEON_OBJTYPE_FUNCSCRIPT:
        case NEON_OBJTYPE_FUNCNATIVE:
            {
                field = nn_class_getpropertyfield(state->classprimcallable, name);
                if(field != NULL)
                {
                    return field;
                }
                else
                {
                    field = nn_class_getstaticproperty(state->classprimcallable, name);
                    if(field != NULL)
                    {
                        return field;
                    }
                }
                nn_except_throw(state, "class Function has no named property '%s'", name->sbuf.data);
                return NULL;
            }
            break;
        default:
            {
                nn_except_throw(state, "object of type %s does not carry properties", nn_value_typename(peeked));
                return NULL;
            }
            break;
    }
    return NULL;
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
        if(field == NULL)
        {
            return false;
        }
        else
        {
            if(field->type == NEON_PROPTYPE_FUNCTION)
            {
                nn_vm_callvaluewithobject(state, field->value, peeked, 0);
            }
            else
            {
                nn_vmbits_stackpop(state);
                nn_vmbits_stackpush(state, field->value);
            }
        }
        return true;
    }
    else
    {
        nn_except_throw(state, "'%s' of type %s does not have properties", nn_value_tostring(state, peeked)->sbuf.data,
            nn_value_typename(peeked));
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
        field = nn_valtable_getfieldbyostr(&instance->properties, name);
        if(field != NULL)
        {
            /* pop the instance... */
            nn_vmbits_stackpop(state);
            nn_vmbits_stackpush(state, field->value);
            return true;
        }
        if(nn_vmutil_bindmethod(state, instance->klass, name))
        {
            return true;
        }
        nn_vmmac_tryraise(state, false, "instance of class %s does not have a property or method named '%s'",
            nn_value_asinstance(peeked)->klass->name->sbuf.data, name->sbuf.data);
        return false;
    }
    else if(nn_value_isclass(peeked))
    {
        klass = nn_value_asclass(peeked);
        field = nn_valtable_getfieldbyostr(&klass->instmethods, name);
        if(field != NULL)
        {
            if(nn_value_getmethodtype(field->value) == NEON_FNCONTEXTTYPE_STATIC)
            {
                /* pop the class... */
                nn_vmbits_stackpop(state);
                nn_vmbits_stackpush(state, field->value);
                return true;
            }
        }
        else
        {
            field = nn_class_getstaticproperty(klass, name);
            if(field != NULL)
            {
                /* pop the class... */
                nn_vmbits_stackpop(state);
                nn_vmbits_stackpush(state, field->value);
                return true;
            }
        }
        nn_vmmac_tryraise(state, false, "class %s does not have a static property or method named '%s'", klass->name->sbuf.data, name->sbuf.data);
        return false;
    }
    else if(nn_value_ismodule(peeked))
    {
        module = nn_value_asmodule(peeked);
        field = nn_valtable_getfieldbyostr(&module->deftable, name);
        if(field != NULL)
        {
            /* pop the module... */
            nn_vmbits_stackpop(state);
            nn_vmbits_stackpush(state, field->value);
            return true;
        }
        nn_vmmac_tryraise(state, false, "module %s does not define '%s'", module->name, name->sbuf.data);
        return false;
    }
    nn_vmmac_tryraise(state, false, "'%s' of type %s does not have properties", nn_value_tostring(state, peeked)->sbuf.data,
        nn_value_typename(peeked));
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
        klass = NULL;
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
            if(klass == NULL)
            {
                nn_except_throw(state, "object of type %s cannot carry properties", nn_value_typename(vtarget));
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
    if(nn_value_isnull(v))
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
    if(nn_value_isnull(v))
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
    long ibinright;
    long ibinleft;
    uint32_t ubinright;
    uint32_t ubinleft;
    double dbinright;
    double dbinleft;
    NNOpCode instruction;
    NNValue res;
    NNValue binvalleft;
    NNValue binvalright;
    instruction = (NNOpCode)state->vmstate.currentinstr.code;
    binvalright = nn_vmbits_stackpeek(state, 0);
    binvalleft = nn_vmbits_stackpeek(state, 1);
    isfail = (
        (!nn_value_isnumber(binvalright) && !nn_value_isbool(binvalright) && !nn_value_isnull(binvalright)) ||
        (!nn_value_isnumber(binvalleft) && !nn_value_isbool(binvalleft) && !nn_value_isnull(binvalleft))
    );
    if(isfail)
    {
        nn_vmmac_tryraise(state, false, "unsupported operand %s for %s and %s", nn_dbg_op2str(instruction), nn_value_typename(binvalleft), nn_value_typename(binvalright));
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
                res = nn_value_makeint(ibinleft & ibinright);
            }
            break;
        case NEON_OP_PRIMOR:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = nn_value_makeint(ibinleft | ibinright);
            }
            break;
        case NEON_OP_PRIMBITXOR:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = nn_value_makeint(ibinleft ^ ibinright);
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
                res = nn_value_makeint(ubinleft << ubinright);

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
                res = nn_value_makeint(ubinleft >> ubinright);
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
    NNHashValTable* tab;
    name = nn_vmbits_readstring(state);
    val = nn_vmbits_stackpeek(state, 0);
    tab = &state->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module->deftable;
    nn_valtable_set(tab, nn_value_fromobject(name), val);
    nn_vmbits_stackpop(state);
    #if (defined(DEBUG_TABLE) && DEBUG_TABLE) || 0
    nn_valtable_print(state, state->debugwriter, &state->declaredglobals, "globals");
    #endif
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_globalget(NNState* state)
{
    NNObjString* name;
    NNHashValTable* tab;
    NNProperty* field;
    name = nn_vmbits_readstring(state);
    tab = &state->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module->deftable;
    field = nn_valtable_getfieldbyostr(tab, name);
    if(field == NULL)
    {
        field = nn_valtable_getfieldbyostr(&state->declaredglobals, name);
        if(field == NULL)
        {
            nn_except_throwclass(state, state->exceptions.stdexception, "global name '%s' is not defined", name->sbuf.data);
            return false;
        }
    }
    nn_vmbits_stackpush(state, field->value);
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_globalset(NNState* state)
{
    NNObjString* name;
    NNHashValTable* table;
    NNObjModule* module;
    name = nn_vmbits_readstring(state);
    module = state->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module;
    table = &module->deftable;
    if(nn_valtable_set(table, nn_value_fromobject(name), nn_vmbits_stackpeek(state, 0)))
    {
        if(state->conf.enablestrictmode)
        {
            nn_valtable_delete(table, nn_value_fromobject(name));
            nn_vmmac_tryraise(state, false, "global name '%s' was not declared", name->sbuf.data);
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
    ssp = state->vmstate.currentframe->stackslotpos;
    val = state->vmstate.stackvalues[ssp + slot];
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
    ssp = state->vmstate.currentframe->stackslotpos;
    state->vmstate.stackvalues[ssp + slot] = peeked;
    return true;
}

/*NEON_OP_FUNCARGOPTIONAL*/
NEON_FORCEINLINE bool nn_vmdo_funcargoptional(NNState* state)
{
    size_t putpos;
    uint16_t slot;
    NNValue peeked;
    NNValue cval;
    slot = 0;
    slot = nn_vmbits_readshort(state);
    cval = nn_vmbits_stackpeek(state, 0);
    peeked = nn_vmbits_stackpeek(state, 1);

    #if 1
        putpos = (state->vmstate.stackidx + (-1 - 1)) ;
    #else
        #if 1
            putpos = state->vmstate.stackidx + (slot - 0);
        #else
            putpos = state->vmstate.stackidx + (-1 - slot);
        #endif
    #endif

    #if 1
    {
        NNPrinter* pr = state->stderrprinter;
        nn_printer_printf(pr, "funcargoptional: slot=%d putpos=%ld cval=<", slot, putpos);
        nn_printer_printvalue(pr, cval, true, false);
        nn_printer_printf(pr, "> peeked=<");
        nn_printer_printvalue(pr, peeked, true, false);
        nn_printer_printf(pr, ">\n");
    }
    #endif
    if(nn_value_isnull(peeked))
    {
        state->vmstate.stackvalues[putpos] = cval;
    }
    else
    {
        #if 0
            nn_vmbits_stackpop(state);
        #endif
    }
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_funcargget(NNState* state)
{
    size_t ssp;
    uint16_t slot;
    NNValue val;
    slot = nn_vmbits_readshort(state);
    ssp = state->vmstate.currentframe->stackslotpos;
    val = state->vmstate.stackvalues[ssp + slot];
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
    ssp = state->vmstate.currentframe->stackslotpos;
    state->vmstate.stackvalues[ssp + slot] = peeked;
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_makeclosure(NNState* state)
{
    size_t i;
    int index;
    size_t ssp;
    uint8_t islocal;
    NNValue* upvals;
    NNObjFunction* function;
    NNObjFunction* closure;
    function = nn_value_asfunction(nn_vmbits_readconst(state));
    closure = nn_object_makefuncclosure(state, function);
    nn_vmbits_stackpush(state, nn_value_fromobject(closure));
    for(i = 0; i < (size_t)closure->upvalcount; i++)
    {
        islocal = nn_vmbits_readbyte(state);
        index = nn_vmbits_readshort(state);
        if(islocal)
        {
            ssp = state->vmstate.currentframe->stackslotpos;
            upvals = &state->vmstate.stackvalues[ssp + index];
            closure->fnclosure.upvalues[i] = nn_vmutil_upvaluescapture(state, upvals, index);
        }
        else
        {
            closure->fnclosure.upvalues[i] = state->vmstate.currentframe->closure->fnclosure.upvalues[index];
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
    array = nn_object_makearray(state);
    state->vmstate.stackvalues[state->vmstate.stackidx + (-count - 1)] = nn_value_fromobject(array);
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
    dict = nn_object_makedict(state);
    state->vmstate.stackvalues[state->vmstate.stackidx + (-count - 1)] = nn_value_fromobject(dict);
    for(i = 0; i < count; i += 2)
    {
        name = state->vmstate.stackvalues[state->vmstate.stackidx + (-count + i)];
        if(!nn_value_isstring(name) && !nn_value_isnumber(name) && !nn_value_isbool(name))
        {
            nn_vmmac_tryraise(state, false, "dictionary key must be one of string, number or boolean");
            return false;
        }
        value = state->vmstate.stackvalues[state->vmstate.stackidx + (-count + i + 1)];
        nn_dict_setentry(dict, name, value);
    }
    nn_vmbits_stackpopn(state, count);
    return true;
}

NEON_FORCEINLINE bool nn_vmdo_dobinaryfunc(NNState* state, const char* opname, nnbinopfunc_t op)
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
        nn_vmmac_tryraise(state, false, "unsupported operand %s for %s and %s", opname, nn_value_typename(binvalleft), nn_value_typename(binvalright));
        return false;
    }
    binvalright = nn_vmbits_stackpop(state);
    dbinright = nn_value_isbool(binvalright) ? (nn_value_asbool(binvalright) ? 1 : 0) : nn_value_asnumber(binvalright);
    binvalleft = nn_vmbits_stackpop(state);
    dbinleft = nn_value_isbool(binvalleft) ? (nn_value_asbool(binvalleft) ? 1 : 0) : nn_value_asnumber(binvalleft);
    nn_vmbits_stackpush(state, nn_value_makenumber(op(dbinleft, dbinright)));
    return true;
}

void nn_vmdebug_printvalue(NNState* state, NNValue val, const char* fmt, ...)
{
    va_list va;
    NNPrinter* pr;
    pr = state->stderrprinter;
    nn_printer_printf(pr, "VMDEBUG: val=<<<");
    nn_printer_printvalue(pr, val, true, false);
    nn_printer_printf(pr, ">>> ");
    va_start(va, fmt);
    nn_printer_vwritefmt(pr, fmt, va);
    va_end(va);
    nn_printer_printf(pr, "\n");
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
    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];

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
        if(state->vmstate.framecount == 0)
        {
            return NEON_STATUS_FAILRUNTIME;
        }
        if(nn_util_unlikely(state->conf.shoulddumpstack))
        {
            ofs = (int)(state->vmstate.currentframe->inscode - state->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.blob.instrucs);
            nn_dbg_printinstructionat(state->debugwriter, &state->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.blob, ofs);
            fprintf(stderr, "stack (before)=[\n");
            iterpos = 0;
            dbgslot = state->vmstate.stackvalues;
            while(dbgslot < &state->vmstate.stackvalues[state->vmstate.stackidx])
            {
                printpos = iterpos + 1;
                iterpos++;
                fprintf(stderr, "  [%s%d%s] ", nn_util_color(NEON_COLOR_YELLOW), printpos, nn_util_color(NEON_COLOR_RESET));
                nn_printer_printf(state->debugwriter, "%s", nn_util_color(NEON_COLOR_YELLOW));
                nn_printer_printvalue(state->debugwriter, *dbgslot, true, false);
                nn_printer_printf(state->debugwriter, "%s", nn_util_color(NEON_COLOR_RESET));
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
        state->vmstate.currentinstr = currinstr;
        #if defined(NEON_CONFIG_USECOMPUTEDGOTO) && (NEON_CONFIG_USECOMPUTEDGOTO == 1)
            computedaddr = dispatchtable[currinstr.code];
            /* TODO: figure out why this happens (failing instruction is 255) */
            if(nn_util_unlikely(computedaddr == NULL))
            {
                #if 0
                    goto trynext;
                #else
                    fprintf(stderr, "computedaddr is NULL!!\n");
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
                    if(rv != NULL)
                    {
                        *rv = result;
                    }
                    ssp = state->vmstate.currentframe->stackslotpos;
                    nn_vmutil_upvaluesclose(state, &state->vmstate.stackvalues[ssp]);
                    state->vmstate.framecount--;
                    if(state->vmstate.framecount == 0)
                    {
                        nn_vmbits_stackpop(state);
                        return NEON_STATUS_OK;
                    }
                    ssp = state->vmstate.currentframe->stackslotpos;
                    state->vmstate.stackidx = ssp;
                    nn_vmbits_stackpush(state, result);
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                    if(state->vmstate.framecount == (int64_t)exitframe)
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
                            nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "unsupported operand + for %s and %s", nn_value_typename(valleft), nn_value_typename(valright));
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
                        string = nn_value_asstring(nn_vmbits_stackpeek(state, 1));
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
                        newlist = nn_object_makearray(state);
                        nn_vmbits_stackpush(state, nn_value_fromobject(newlist));
                        nn_vmutil_multiplyarray(state, list, newlist, intnum);
                        nn_vmbits_stackpopn(state, 2);
                        nn_vmbits_stackpush(state, nn_value_fromobject(newlist));
                        VM_DISPATCH();
                    }
                    nn_vmdo_dobinarydirect(state);
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
                    if(nn_vmdo_dobinaryfunc(state, "**", (nnbinopfunc_t)pow))
                    {
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMFLOORDIVIDE)
                {
                    if(nn_vmdo_dobinaryfunc(state, "//", (nnbinopfunc_t)nn_vmutil_floordiv))
                    {
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMNEGATE)
                {
                    NNValue peeked;
                    peeked = nn_vmbits_stackpeek(state, 0);
                    if(!nn_value_isnumber(peeked))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "operator - not defined for object of type %s", nn_value_typename(peeked));
                        VM_DISPATCH();
                    }
                    nn_vmbits_stackpush(state, nn_value_makenumber(-nn_value_asnumber(nn_vmbits_stackpop(state))));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMBITNOT)
            {
                NNValue peeked;
                peeked = nn_vmbits_stackpeek(state, 0);
                if(!nn_value_isnumber(peeked))
                {
                    nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "operator ~ not defined for object of type %s", nn_value_typename(peeked));
                    VM_DISPATCH();
                }
                nn_vmbits_stackpush(state, nn_value_makeint(~((int)nn_value_asnumber(nn_vmbits_stackpop(state)))));
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
                    nn_vmbits_stackpush(state, nn_value_makebool(nn_value_compare(state, a, b)));
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
                    nn_vmbits_stackpush(state, nn_value_makebool(nn_value_isfalse(nn_vmbits_stackpop(state))));
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
                    state->vmstate.currentframe->inscode += offset;
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_JUMPIFFALSE)
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    if(nn_value_isfalse(nn_vmbits_stackpeek(state, 0)))
                    {
                        state->vmstate.currentframe->inscode += offset;
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_LOOP)
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    state->vmstate.currentframe->inscode -= offset;
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_ECHO)
                {
                    NNValue val;
                    val = nn_vmbits_stackpeek(state, 0);
                    nn_printer_printvalue(state->stdoutprinter, val, state->isrepl, true);
                    if(!nn_value_isnull(val))
                    {
                        nn_printer_writestring(state->stdoutprinter, "\n");
                    }
                    nn_vmbits_stackpop(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_STRINGIFY)
                {
                    NNValue peeked;
                    NNObjString* value;
                    peeked = nn_vmbits_stackpeek(state, 0);
                    if(!nn_value_isstring(peeked) && !nn_value_isnull(peeked))
                    {
                        value = nn_value_tostring(state, nn_vmbits_stackpop(state));
                        if(value->sbuf.length != 0)
                        {
                            nn_vmbits_stackpush(state, nn_value_fromobject(value));
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
                    nn_vmbits_stackpush(state, nn_vmbits_stackpeek(state, 0));
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
                    nn_vmutil_upvaluesclose(state, &state->vmstate.stackvalues[state->vmstate.stackidx - 1]);
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
                    int index;
                    NNObjFunction* closure;
                    index = nn_vmbits_readshort(state);
                    closure = state->vmstate.currentframe->closure;
                    if(index < closure->upvalcount)
                    {
                        nn_vmbits_stackpush(state, closure->fnclosure.upvalues[index]->location);
                    }
                    else
                    {
                        nn_vmbits_stackpush(state, nn_value_makenull());
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_UPVALUESET)
                {
                    int index;
                    index = nn_vmbits_readshort(state);
                    state->vmstate.currentframe->closure->fnclosure.upvalues[index]->location = nn_vmbits_stackpeek(state, 0);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CALLFUNCTION)
                {
                    int argcount;
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vm_callvalue(state, nn_vmbits_stackpeek(state, argcount), nn_value_makenull(), argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
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
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSGETTHIS)
                {
                    NNValue thisval;
                    thisval = nn_vmbits_stackpeek(state, 3);
                    nn_printer_printf(state->debugwriter, "CLASSGETTHIS: thisval=");
                    nn_printer_printvalue(state->debugwriter, thisval, true, false);
                    nn_printer_printf(state->debugwriter, "\n");
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
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
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
                    field = nn_valtable_getfieldbyostr(&state->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module->deftable, name);
                    if(field != NULL)
                    {
                        if(nn_value_isclass(field->value))
                        {
                            haveval = true;
                            pushme = field->value;
                        }
                    }
                    field = nn_valtable_getfieldbyostr(&state->declaredglobals, name);
                    if(field != NULL)
                    {
                        if(nn_value_isclass(field->value))
                        {
                            haveval = true;
                            pushme = field->value;
                        }
                    }
                    if(!haveval)
                    {
                        klass = nn_object_makeclass(state, name, state->classprimobject);
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
                    NNObjClass* superclass;
                    NNObjClass* subclass;
                    if(!nn_value_isclass(nn_vmbits_stackpeek(state, 1)))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "cannot inherit from non-class object");
                        VM_DISPATCH();
                    }
                    superclass = nn_value_asclass(nn_vmbits_stackpeek(state, 1));
                    subclass = nn_value_asclass(nn_vmbits_stackpeek(state, 0));
                    nn_class_inheritfrom(subclass, superclass);
                    /* pop the subclass */
                    nn_vmbits_stackpop(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSGETSUPER)
                {
                    NNObjClass* klass;
                    NNObjString* name;
                    name = nn_vmbits_readstring(state);
                    klass = nn_value_asclass(nn_vmbits_stackpeek(state, 0));
                    if(!nn_vmutil_bindmethod(state, klass->superclass, name))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "class %s does not define a function %s", klass->name->sbuf.data, name->sbuf.data);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSINVOKESUPER)
                {
                    int argcount;
                    NNObjClass* klass;
                    NNObjString* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    klass = nn_value_asclass(nn_vmbits_stackpop(state));
                    if(!nn_vmutil_invokemethodfromclass(state, klass, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSINVOKESUPERSELF)
                {
                    int argcount;
                    NNObjClass* klass;
                    argcount = nn_vmbits_readbyte(state);
                    klass = nn_value_asclass(nn_vmbits_stackpop(state));
                    if(!nn_vmutil_invokemethodfromclass(state, klass, state->constructorname, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
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
                    nn_vmbits_stackpush(state, nn_value_fromobject(nn_object_makerange(state, lower, upper)));
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
                    NNObjString* name;
                    NNObjModule* mod;
                    name = nn_value_asstring(nn_vmbits_stackpeek(state, 0));
                    fprintf(stderr, "IMPORTIMPORT: name='%s'\n", name->sbuf.data);
                    mod = nn_import_loadmodulescript(state, state->topmodule, name);
                    fprintf(stderr, "IMPORTIMPORT: mod='%p'\n", (void*)mod);
                    if(mod == NULL)
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
                    result = nn_value_typename(thing);
                    res = nn_value_fromobject(nn_string_copycstr(state, result));
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
                        if(!nn_value_isnull(message))
                        {
                            nn_except_throwclass(state, state->exceptions.asserterror, nn_value_tostring(state, message)->sbuf.data);
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
                    nn_instance_defproperty(instance, nn_string_copycstr(state, "stacktrace"), stacktrace);
                    if(nn_except_propagate(state))
                    {
                        state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
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
                    exclass = NULL;
                    type = nn_vmbits_readstring(state);
                    addr = nn_vmbits_readshort(state);
                    finaddr = nn_vmbits_readshort(state);
                    if(addr != 0)
                    {
                        value = nn_value_makenull();
                        if(!nn_valtable_get(&state->declaredglobals, nn_value_fromobject(type), &value))
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
                            if(!nn_valtable_get(&state->vmstate.currentframe->closure->fnclosure.scriptfunc->fnscriptfunc.module->deftable, nn_value_fromobject(type), &value) || !nn_value_isclass(value))
                            {
                                nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "object of type '%s' is not an exception", type->sbuf.data);
                                VM_DISPATCH();
                            }
                            */
                            exclass = state->exceptions.stdexception;
                        }
                        nn_except_pushhandler(state, exclass, addr, finaddr);
                    }
                    else
                    {
                        nn_except_pushhandler(state, NULL, addr, finaddr);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_EXPOPTRY)
                {
                    state->vmstate.currentframe->handlercount--;
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_EXPUBLISHTRY)
                {
                    state->vmstate.currentframe->handlercount--;
                    if(nn_except_propagate(state))
                    {
                        state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
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
                    if(nn_valtable_get(&sw->table, expr, &value))
                    {
                        state->vmstate.currentframe->inscode += (int)nn_value_asnumber(value);
                    }
                    else if(sw->defaultjump != -1)
                    {
                        state->vmstate.currentframe->inscode += sw->defaultjump;
                    }
                    else
                    {
                        state->vmstate.currentframe->inscode += sw->exitjump;
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

int nn_nestcall_prepare(NNState* state, NNValue callable, NNValue mthobj, NNValue* callarr, int maxcallarr)
{
    int arity;
    NNObjFunction* closure;
    (void)state;
    (void)maxcallarr;
    arity = 0;
    if(nn_value_isfuncclosure(callable))
    {
        closure = nn_value_asfunction(callable);
        arity = closure->fnclosure.scriptfunc->fnscriptfunc.arity;
    }
    else if(nn_value_isfuncscript(callable))
    {
        arity = nn_value_asfunction(callable)->fnscriptfunc.arity;
    }
    else if(nn_value_isfuncnative(callable))
    {
        #if 0
            arity = nn_value_asfunction(callable);
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
bool nn_nestcall_callfunction(NNState* state, NNValue callable, NNValue thisval, NNValue* argv, size_t argc, NNValue* dest)
{
    size_t i;
    size_t pidx;
    NNStatus status;
    pidx = state->vmstate.stackidx;
    /* set the closure before the args */
    nn_vm_stackpush(state, callable);
    if((argv != NULL) && (argc > 0))
    {
        for(i = 0; i < argc; i++)
        {
            nn_vm_stackpush(state, argv[i]);
        }
    }
    if(!nn_vm_callvaluewithobject(state, callable, thisval, argc))
    {
        fprintf(stderr, "nestcall: nn_vm_callvalue() failed\n");
        abort();
    }
    status = nn_vm_runvm(state, state->vmstate.framecount - 1, NULL);
    if(status != NEON_STATUS_OK)
    {
        fprintf(stderr, "nestcall: call to runvm failed\n");
        abort();
    }
    *dest = state->vmstate.stackvalues[state->vmstate.stackidx - 1];
    nn_vm_stackpopn(state, argc + 0);
    state->vmstate.stackidx = pidx;
    return true;
}

NNObjFunction* nn_state_compilesource(NNState* state, NNObjModule* module, bool fromeval, const char* source, bool toplevel)
{
    NNBlob blob;
    NNObjFunction* function;
    NNObjFunction* closure;
    (void)toplevel;
    nn_blob_init(state, &blob);
    function = nn_astparser_compilesource(state, module, source, &blob, false, fromeval);
    if(function == NULL)
    {
        nn_blob_destroy(&blob);
        return NULL;
    }
    if(!fromeval)
    {
        nn_vm_stackpush(state, nn_value_fromobject(function));
    }
    else
    {
        function->name = nn_string_copycstr(state, "(evaledcode)");
    }
    closure = nn_object_makefuncclosure(state, function);
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
    state->topmodule->physicalpath = nn_string_copycstr(state, rp);
    nn_module_setfilefield(state, module);
    closure = nn_state_compilesource(state, module, false, source, true);
    if(closure == NULL)
    {
        return NEON_STATUS_FAILCOMPILE;
    }
    if(state->conf.exitafterbytecode)
    {
        return NEON_STATUS_OK;
    }
    nn_vm_callclosure(state, closure, nn_value_makenull(), 0);
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
    argc = nn_nestcall_prepare(state, callme, nn_value_makenull(), NULL, 0);
    ok = nn_nestcall_callfunction(state, callme, nn_value_makenull(), NULL, 0, &retval);
    if(!ok)
    {
        nn_except_throw(state, "eval() failed");
    }
    return retval;
}

char* nn_cli_getinput(const char* prompt)
{
    #if defined(NEON_CONFIG_USELINENOISE) && (NEON_CONFIG_USELINENOISE == 1)
        return lino_readline(prompt);
    #else
        enum { kMaxLineSize = 1024 };
        size_t len;
        char* rt;
        char rawline[kMaxLineSize+1] = {0};
        fprintf(stdout, "%s", prompt);
        fflush(stdout);
        rt = nn_util_filegetshandle(rawline, kMaxLineSize, stdin, &len);
        rt[len - 1] = 0;
        return rt;
    #endif
}

void nn_cli_addhistoryline(const char* line)
{
    #if defined(NEON_CONFIG_USELINENOISE) && (NEON_CONFIG_USELINENOISE == 1)
        lino_historyadd(line);
    #else
        (void)line;
    #endif
}

void nn_cli_freeline(char* line)
{
    #if defined(NEON_CONFIG_USELINENOISE) && (NEON_CONFIG_USELINENOISE == 1)
        lino_freeline(line);
    #else
        (void)line;
    #endif
}

#if !defined(NEON_PLAT_ISWASM)
bool nn_cli_repl(NNState* state)
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
    StringBuffer* source;
    const char* cursor;
    NNValue dest;
    NNPrinter* pr;
    pr = state->stdoutprinter;
    rescnt = 0;
    state->isrepl = true;
    continuerepl = true;
    printf("Type \".exit\" to quit or \".credits\" for more information\n");
    source = dyn_strbuf_makebasicempty(0);
    bracecount = 0;
    parencount = 0;
    bracketcount = 0;
    singlequotecount = 0;
    doublequotecount = 0;
    #if !defined(NEON_PLAT_ISWINDOWS)
        #if defined(NEON_CONFIG_USELINENOISE) && (NEON_CONFIG_USELINENOISE == 1)
            /* linenoiseSetEncodingFunctions(linenoiseUtf8PrevCharLen, linenoiseUtf8NextCharLen, linenoiseUtf8ReadCode); */
            lino_setmultiline(0);
            lino_historyadd(".exit");
        #endif
    #endif
    while(true)
    {
        if(!continuerepl)
        {
            bracecount = 0;
            parencount = 0;
            bracketcount = 0;
            singlequotecount = 0;
            doublequotecount = 0;
            dyn_strbuf_reset(source);
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
        if(line == NULL || strcmp(line, ".exit") == 0)
        {
            dyn_strbuf_destroy(source);
            return true;
        }
        linelength = (int)strlen(line);
        if(strcmp(line, ".credits") == 0)
        {
            printf("\n" NEON_INFO_COPYRIGHT "\n\n");
            dyn_strbuf_reset(source);
            continue;
        }
        nn_cli_addhistoryline(line);
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
        dyn_strbuf_appendstr(source, line);
        if(linelength > 0)
        {
            dyn_strbuf_appendstr(source, "\n");
        }
        nn_cli_freeline(line);
        if(bracketcount == 0 && parencount == 0 && bracecount == 0 && singlequotecount == 0 && doublequotecount == 0)
        {
            memset(varnamebuf, 0, kMaxVarName);
            sprintf(varnamebuf, "_%ld", (long)rescnt);
            nn_state_execsource(state, state->topmodule, source->data, "<repl>", &dest);
            dest = state->lastreplvalue;
            if(!nn_value_isnull(dest))
            {
                nn_printer_printf(pr, "%s = ", varnamebuf);
                nn_printer_printvalue(pr, dest, true, true);
                nn_state_defglobalvalue(state, varnamebuf, dest);
                nn_printer_printf(pr, "\n");
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

bool nn_cli_runfile(NNState* state, const char* file)
{
    size_t fsz;
    char* source;
    const char* oldfile;
    NNStatus result;
    source = nn_util_filereadfile(state, file, &fsz, false, 0);
    if(source == NULL)
    {
        oldfile = file;
        source = nn_util_filereadfile(state, file, &fsz, false, 0);
        if(source == NULL)
        {
            fprintf(stderr, "failed to read from '%s': %s\n", oldfile, strerror(errno));
            return false;
        }
    }
    result = nn_state_execsource(state, state->topmodule, source, file, NULL);
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

bool nn_cli_runcode(NNState* state, char* source)
{
    NNStatus result;
    state->rootphysfile = NULL;
    result = nn_state_execsource(state, state->topmodule, source, "<-e>", NULL);
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

int nn_util_findfirstpos(const char* str, size_t len, int ch)
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

void nn_cli_parseenv(NNState* state, char** envp)
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
    if(envp == NULL)
    {
        return;
    }
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
            oskey = nn_string_copycstr(state, keybuf);
            osval = nn_string_copycstr(state, valbuf);
            nn_dict_setentry(state->envdict, nn_value_fromobject(oskey), nn_value_fromobject(osval));
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
    ptyp(NNPrinter);
    ptyp(NNValue);
    ptyp(NNObject);
    ptyp(NNPropGetSet);
    ptyp(NNProperty);
    ptyp(NNValArray);
    ptyp(NNBlob);
    ptyp(NNHashValEntry);
    ptyp(NNHashValTable);
    ptyp(NNObjString);
    ptyp(NNObjUpvalue);
    ptyp(NNObjModule);
    ptyp(NNObjClass);
    ptyp(NNObjInstance);
    ptyp(NNObjFunction);
    ptyp(NNObjArray);
    ptyp(NNObjRange);
    ptyp(NNObjDict);
    ptyp(NNObjFile);
    ptyp(NNObjSwitch);
    ptyp(NNObjUserdata);
    ptyp(NNExceptionFrame);
    ptyp(NNCallFrame);
    ptyp(NNState);
    ptyp(NNAstToken);
    ptyp(NNAstLexer);
    ptyp(NNAstLocal);
    ptyp(NNAstUpvalue);
    ptyp(NNAstFuncCompiler);
    ptyp(NNAstClassCompiler);
    ptyp(NNAstParser);
    ptyp(NNAstRule);
    ptyp(NNRegFunc);
    ptyp(NNRegField);
    ptyp(NNRegClass);
    ptyp(NNRegModule);
    ptyp(NNInstruction)
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
    NNState* state;
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
        {"gcstart", 'g', OPTPARSE_REQUIRED, "set minimum bytes at which the GC should kick in. 0 disables GC"},
        {0, 0, (optargtype_t)0, NULL}
    };
    nn_memory_init();
    #if defined(NEON_PLAT_ISWINDOWS)
        _setmode(fileno(stdin), _O_BINARY);
        _setmode(fileno(stdout), _O_BINARY);
        _setmode(fileno(stderr), _O_BINARY);
    #endif
    ok = true;
    wasusage = false;
    quitafterinit = false;
    source = NULL;
    nextgcstart = NEON_CONFIG_DEFAULTGCSTART;
    state = nn_state_makealloc();
    if(state == NULL)
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
        if(arg == NULL)
        {
            break;
        }
        nargv[nargc] = arg;
        nargc++;
    }
    {
        NNObjString* os;
        for(i=0; i<nargc; i++)
        {
            os = nn_string_copycstr(state, nargv[i]);
            nn_array_push(state->processinfo->cliargv, nn_value_fromobject(os));

        }
        nn_valtable_set(&state->declaredglobals, nn_value_copystr(state, "ARGV"), nn_value_fromobject(state->processinfo->cliargv));
    }
    state->gcstate.nextgc = nextgcstart;
    nn_import_loadbuiltinmodules(state);
    if(source != NULL)
    {
        ok = nn_cli_runcode(state, source);
    }
    else if(nargc > 0)
    {
        filename = nn_value_asstring(nn_valarray_get(&state->processinfo->cliargv->varray, 0))->sbuf.data;
        fprintf(stderr, "nargv[0]=%s\n", filename);
        ok = nn_cli_runfile(state,  filename);
    }
    else
    {
        ok = nn_cli_repl(state);
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

int replmain(const char* file)
{
    const char* deffile;
    deffile = "mandel1.nn";
    if(file != NULL)
    {
        deffile = file;
    }
    char* realargv[1024] = {(char*)"a.out", (char*)deffile, NULL};
    return main(1, realargv, NULL);
}
