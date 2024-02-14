
#pragma once

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
#include "wrapdlfcn.h"

#if defined(__STRICT_ANSI__)
    #define NEON_INLINE
#else
    #define NEON_INLINE inline
    #if 0 //defined(__GNUC__)
        #define NEON_FORCEINLINE __attribute__((always_inline)) inline
    #else
        #define NEON_FORCEINLINE inline
    #endif
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
    nn_gcmem_reallocate(state, pointer, (typsz) * (oldcount), (typsz) * (newcount))

#define nn_gcmem_freearray(state, typsz, pointer, oldcount) \
    nn_gcmem_release(state, pointer, (typsz) * (oldcount))

#define NORMALIZE(token) #token

#define nn_exceptions_throwclass(state, exklass, ...) \
    nn_exceptions_throwwithclass(state, exklass, __FILE__, __LINE__, __VA_ARGS__)

#define nn_exceptions_throw(state, ...) \
    nn_exceptions_throwclass(state, state->exceptions.stdexception, __VA_ARGS__)

#if defined(__linux__) || defined(__CYGWIN__) || defined(__MINGW32_MAJOR_VERSION)
    #include <libgen.h>
    #include <limits.h>
    #if defined(__sun)
        #define PROC_SELF_EXE "/proc/self/path/a.out"
    #else
        #define PROC_SELF_EXE "/proc/self/exe"
    #endif
#endif

#define NEON_RETURNERROR(...) \
    { \
        nn_vm_stackpopn(state, args->count); \
        nn_exceptions_throw(state, ##__VA_ARGS__); \
    } \
    return nn_value_makebool(false);

#define NEON_ARGS_FAIL(chp, ...) \
    nn_argcheck_fail((chp), __FILE__, __LINE__, __VA_ARGS__)

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
#define NEON_ARGS_CHECKTYPE(chp, i, type) \
    if(!type((chp)->argv[i])) \
    { \
        return NEON_ARGS_FAIL(chp, "%s() expects argument %d as " NORMALIZE(type) ", %s given", (chp)->name, (i) + 1, nn_value_typename((chp)->argv[i])); \
    }

/* reject argument $type at $index */
#define NEON_ARGS_REJECTTYPE(chp, type, index) \
    if(type((chp)->argv[index])) \
    { \
        return NEON_ARGS_FAIL(chp, "invalid type %s() as argument %d in %s()", nn_value_typename((chp)->argv[index]), (index) + 1, (chp)->name); \
    }

enum NeonOpCode
{
    NEON_OP_GLOBALDEFINE,
    NEON_OP_GLOBALGET,
    NEON_OP_GLOBALSET,
    NEON_OP_LOCALGET,
    NEON_OP_LOCALSET,
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
    NEON_OP_BREAK_PL,
};

enum NeonAstTokType
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
    NEON_ASTTOK_KWCASE,
    NEON_ASTTOK_KWWHILE,
    NEON_ASTTOK_LITERAL,
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
    NEON_ASTTOK_TOKCOUNT,
};

enum NeonAstPrecedence
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

enum NeonFuncType
{
    NEON_FUNCTYPE_ANONYMOUS,
    NEON_FUNCTYPE_FUNCTION,
    NEON_FUNCTYPE_METHOD,
    NEON_FUNCTYPE_INITIALIZER,
    NEON_FUNCTYPE_PRIVATE,
    NEON_FUNCTYPE_STATIC,
    NEON_FUNCTYPE_SCRIPT,
};

enum NeonStatus
{
    NEON_STATUS_OK,
    NEON_STATUS_FAILCOMPILE,
    NEON_STATUS_FAILRUNTIME,
};

enum NeonPrMode
{
    NEON_PRMODE_UNDEFINED,
    NEON_PRMODE_STRING,
    NEON_PRMODE_FILE,
};

enum NeonObjType
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
    NEON_OBJTYPE_USERDATA,
};

enum NeonValType
{
    NEON_VALTYPE_NULL,
    NEON_VALTYPE_BOOL,
    NEON_VALTYPE_NUMBER,
    NEON_VALTYPE_OBJ,
    NEON_VALTYPE_EMPTY,
};

enum NeonFieldType
{
    NEON_PROPTYPE_INVALID,
    NEON_PROPTYPE_VALUE,
    /*
    * indicates this field contains a function, a pseudo-getter (i.e., ".length")
    * which is called upon getting
    */
    NEON_PROPTYPE_FUNCTION,
};


enum NeonColor
{
    NEON_COLOR_RESET,
    NEON_COLOR_RED,
    NEON_COLOR_GREEN,    
    NEON_COLOR_YELLOW,
    NEON_COLOR_BLUE,
    NEON_COLOR_MAGENTA,
    NEON_COLOR_CYAN
};

enum NeonAstCompContext
{
    NEON_COMPCONTEXT_NONE,
    NEON_COMPCONTEXT_CLASS,
    NEON_COMPCONTEXT_ARRAY,
    NEON_COMPCONTEXT_NESTEDFUNCTION,
};

typedef enum /**/NeonAstCompContext NeonAstCompContext;
typedef enum /**/ NeonColor NeonColor;
typedef enum /**/NeonFieldType NeonFieldType;
typedef enum /**/ NeonValType NeonValType;
typedef enum /**/ NeonOpCode NeonOpCode;
typedef enum /**/ NeonFuncType NeonFuncType;
typedef enum /**/ NeonObjType NeonObjType;
typedef enum /**/ NeonStatus NeonStatus;
typedef enum /**/ NeonAstTokType NeonAstTokType;
typedef enum /**/ NeonAstPrecedence NeonAstPrecedence;
typedef enum /**/ NeonPrMode NeonPrMode;

typedef struct /**/NeonFormatInfo NeonFormatInfo;
typedef struct /**/NeonIOResult NeonIOResult;
typedef struct /**/ NeonAstFuncCompiler NeonAstFuncCompiler;
typedef struct /**/ NeonObject NeonObject;
typedef struct /**/ NeonObjString NeonObjString;
typedef struct /**/ NeonObjArray NeonObjArray;
typedef struct /**/ NeonObjUpvalue NeonObjUpvalue;
typedef struct /**/ NeonObjClass NeonObjClass;
typedef struct /**/ NeonObjFuncNative NeonObjFuncNative;
typedef struct /**/ NeonObjModule NeonObjModule;
typedef struct /**/ NeonObjFuncScript NeonObjFuncScript;
typedef struct /**/ NeonObjFuncClosure NeonObjFuncClosure;
typedef struct /**/ NeonObjInstance NeonObjInstance;
typedef struct /**/ NeonObjFuncBound NeonObjFuncBound;
typedef struct /**/ NeonObjRange NeonObjRange;
typedef struct /**/ NeonObjDict NeonObjDict;
typedef struct /**/ NeonObjFile NeonObjFile;
typedef struct /**/ NeonObjSwitch NeonObjSwitch;
typedef struct /**/ NeonObjUserdata NeonObjUserdata;
typedef union /**/ NeonDoubleHashUnion NeonDoubleHashUnion;
typedef struct /**/ NeonValue NeonValue;
typedef struct /**/NeonPropGetSet NeonPropGetSet;
typedef struct /**/NeonProperty NeonProperty;
typedef struct /**/ NeonValArray NeonValArray;
typedef struct /**/ NeonBlob NeonBlob;
typedef struct /**/ NeonHashEntry NeonHashEntry;
typedef struct /**/ NeonHashTable NeonHashTable;
typedef struct /**/ NeonExceptionFrame NeonExceptionFrame;
typedef struct /**/ NeonCallFrame NeonCallFrame;
typedef struct /**/ NeonAstToken NeonAstToken;
typedef struct /**/ NeonAstLexer NeonAstLexer;
typedef struct /**/ NeonAstLocal NeonAstLocal;
typedef struct /**/ NeonAstUpvalue NeonAstUpvalue;
typedef struct /**/ NeonAstClassCompiler NeonAstClassCompiler;
typedef struct /**/ NeonAstParser NeonAstParser;
typedef struct /**/ NeonAstRule NeonAstRule;
typedef struct /**/ NeonRegFunc NeonRegFunc;
typedef struct /**/ NeonRegField NeonRegField;
typedef struct /**/ NeonRegClass NeonRegClass;
typedef struct /**/ NeonRegModule NeonRegModule;
typedef struct /**/ NeonState NeonState;
typedef struct /**/ NeonPrinter NeonPrinter;
typedef struct /**/ NeonArgCheck NeonArgCheck;
typedef struct /**/ NeonArguments NeonArguments;
typedef struct /**/NeonInstruction NeonInstruction;

typedef NeonValue (*NeonNativeFN)(NeonState*, NeonArguments*);
typedef void (*NeonPtrFreeFN)(void*);
typedef bool (*NeonAstParsePrefixFN)(NeonAstParser*, bool);
typedef bool (*NeonAstParseInfixFN)(NeonAstParser*, NeonAstToken, bool);
typedef NeonValue (*NeonClassFieldFN)(NeonState*);
typedef void (*NeonModLoaderFN)(NeonState*);
typedef NeonRegModule* (*NeonModInitFN)(NeonState*);

union NeonDoubleHashUnion
{
    uint64_t bits;
    double num;
};

struct NeonIOResult
{
    bool success;
    char* data;
    size_t length;    
};

struct NeonPrinter
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
    int maxvallength;
    /* the mode that determines what writer actually does */
    NeonPrMode wrmode;
    NeonState* pvm;
    StringBuffer* strbuf;
    FILE* handle;
};

struct NeonFormatInfo
{
    /* length of the format string */
    size_t fmtlen;
    /* the actual format string */
    const char* fmtstr;
    /* destination writer */
    NeonPrinter* writer;
    NeonState* pvm;
};

struct NeonValue
{
    NeonValType type;
    union
    {
        bool vbool;
        double vfltnum;
        NeonObject* vobjpointer;
    } valunion;
};

struct NeonObject
{
    NeonObjType type;
    bool mark;
    NeonState* pvm;
    /*
    // when an object is marked as stale, it means that the
    // GC will never collect this object. This can be useful
    // for library/package objects that want to reuse native
    // objects in their types/pointers. The GC cannot reach
    // them yet, so it's best for them to be kept stale.
    */
    bool stale;
    NeonObject* next;
};

struct NeonPropGetSet
{
    NeonValue getter;
    NeonValue setter;
};

struct NeonProperty
{
    NeonFieldType type;
    NeonValue value;
    bool havegetset;
    NeonPropGetSet getset;
};

struct NeonValArray
{
    NeonState* pvm;
    /* type size of the stored value (via sizeof) */
    int tsize;
    /* how many entries are currently stored? */
    int count;
    /* how many entries can be stored before growing? */
    int capacity;
    NeonValue* values;
};

struct NeonInstruction
{
    /* is this instruction an opcode? */
    bool isop;
    /* opcode or value */
    uint8_t code;
    /* line corresponding to where this instruction was emitted */
    int srcline;
};

struct NeonBlob
{
    int count;
    int capacity;
    NeonInstruction* instrucs;
    NeonValArray* constants;
    NeonValArray* argdefvals;
};

struct NeonHashEntry
{
    NeonValue key;
    NeonProperty value;
};

struct NeonHashTable
{
    /*
    * FIXME: extremely stupid hack: $active ensures that a table that was destroyed
    * does not get marked again, et cetera.
    * since nn_table_destroy() zeroes the data before freeing, $active will be
    * false, and thus, no further marking occurs.
    * obviously the reason is that somewhere a table (from NeonObjInstance) is being
    * read after being freed, but for now, this will work(-ish).
    */
    bool active;
    int count;
    int capacity;
    NeonState* pvm;
    NeonHashEntry* entries;
};

struct NeonObjString
{
    NeonObject objpadding;
    uint32_t hash;
    StringBuffer* sbuf;
};

struct NeonObjUpvalue
{
    NeonObject objpadding;
    int stackpos;
    NeonValue closed;
    NeonValue location;
    NeonObjUpvalue* next;
};

struct NeonObjModule
{
    NeonObject objpadding;
    bool imported;
    NeonHashTable* deftable;
    NeonObjString* name;
    NeonObjString* physicalpath;
    void* preloader;
    void* unloader;
    WrapDL* handle;
};

struct NeonObjFuncScript
{
    NeonObject objpadding;
    NeonFuncType type;
    int arity;
    int upvalcount;
    bool isvariadic;
    NeonBlob blob;
    NeonObjString* name;
    NeonObjModule* module;
};

struct NeonObjFuncClosure
{
    NeonObject objpadding;
    int upvalcount;
    NeonObjFuncScript* scriptfunc;
    NeonObjUpvalue** upvalues;
};

struct NeonObjClass
{
    NeonObject objpadding;

    /*
    * the constructor, if any. defaults to <empty>, and if not <empty>, expects to be
    * some callable value.
    */
    NeonValue constructor;
    NeonValue destructor;

    /*
    * when declaring a class, $instprops (their names, and initial values) are
    * copied to NeonObjInstance::properties.
    * so `$instprops["something"] = somefunction;` remains untouched *until* an
    * instance of this class is created.
    */
    NeonHashTable* instprops;

    /*
    * static, unchangeable(-ish) values. intended for values that are not unique, but shared
    * across classes, without being copied.
    */
    NeonHashTable* staticproperties;

    /*
    * method table; they are currently not unique when instantiated; instead, they are
    * read from $methods as-is. this includes adding methods!
    * TODO: introduce a new hashtable field for NeonObjInstance for unique methods, perhaps?
    * right now, this method just prevents unnecessary copying.
    */
    NeonHashTable* methods;
    NeonHashTable* staticmethods;
    NeonObjString* name;
    NeonObjClass* superclass;
};

struct NeonObjInstance
{
    NeonObject objpadding;
    /*
    * whether this instance is still "active", i.e., not destroyed, deallocated, etc.
    * in rare circumstances s
    */
    bool active;
    NeonHashTable* properties;
    NeonObjClass* klass;
};

struct NeonObjFuncBound
{
    NeonObject objpadding;
    NeonValue receiver;
    NeonObjFuncClosure* method;
};

struct NeonObjFuncNative
{
    NeonObject objpadding;
    NeonFuncType type;
    const char* name;
    NeonNativeFN natfunc;
    void* userptr;
};

struct NeonObjArray
{
    NeonObject objpadding;
    NeonValArray* varray;
};

struct NeonObjRange
{
    NeonObject objpadding;
    int lower;
    int upper;
    int range;
};

struct NeonObjDict
{
    NeonObject objpadding;
    NeonValArray* names;
    NeonHashTable* htab;
};

struct NeonObjFile
{
    NeonObject objpadding;
    bool isopen;
    bool isstd;
    bool istty;
    int number;
    FILE* handle;
    NeonObjString* mode;
    NeonObjString* path;
};

struct NeonObjSwitch
{
    NeonObject objpadding;
    int defaultjump;
    int exitjump;
    NeonHashTable* table;
};

struct NeonObjUserdata
{
    NeonObject objpadding;
    void* pointer;
    char* name;
    NeonPtrFreeFN ondestroyfn;
};

struct NeonExceptionFrame
{
    uint16_t address;
    uint16_t finallyaddress;
    NeonObjClass* klass;
};

struct NeonCallFrame
{
    int handlercount;
    int gcprotcount;
    int stackslotpos;
    NeonInstruction* inscode;
    NeonObjFuncClosure* closure;
    NeonExceptionFrame handlers[NEON_CFG_MAXEXCEPTHANDLERS];
};

struct NeonState
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
        bool enableastdebug;
    } conf;

    struct
    {
        size_t stackidx;
        size_t stackcapacity;
        size_t framecapacity;
        size_t framecount;
        NeonInstruction currentinstr;
        NeonCallFrame* currentframe;
        NeonObjUpvalue* openupvalues;
        NeonObject* linkedobjects;
        NeonCallFrame* framevalues;
        NeonValue* stackvalues;
    } vmstate;

    struct
    {
        int graycount;
        int graycapacity;
        int bytesallocated;
        int nextgc;
        NeonObject** graystack;
    } gcstate;


    struct {
        NeonObjClass* stdexception;
        NeonObjClass* syntaxerror;
        NeonObjClass* asserterror;
        NeonObjClass* ioerror;
        NeonObjClass* oserror;
        NeonObjClass* argumenterror;
    } exceptions;

    void* memuserptr;


    char* rootphysfile;

    NeonObjDict* envdict;

    NeonObjString* constructorname;
    NeonObjModule* topmodule;
    NeonValArray* importpath;

    /* objects tracker */
    NeonHashTable* modules;
    NeonHashTable* strings;
    NeonHashTable* globals;

    /* object public methods */
    NeonObjClass* classprimprocess;
    NeonObjClass* classprimobject;
    NeonObjClass* classprimnumber;
    NeonObjClass* classprimstring;
    NeonObjClass* classprimarray;
    NeonObjClass* classprimdict;
    NeonObjClass* classprimfile;
    NeonObjClass* classprimrange;

    bool isrepl;
    bool markvalue;
    NeonObjArray* cliargv;
    NeonObjFile* filestdout;
    NeonObjFile* filestderr;
    NeonObjFile* filestdin;

    /* miscellaneous */
    NeonPrinter* stdoutprinter;
    NeonPrinter* stderrprinter;
    NeonPrinter* debugwriter;
};

struct NeonAstToken
{
    bool isglobal;
    NeonAstTokType type;
    const char* start;
    int length;
    int line;
};

struct NeonAstLexer
{
    NeonState* pvm;
    const char* start;
    const char* sourceptr;
    int line;
    int tplstringcount;
    int tplstringbuffer[NEON_CFG_ASTMAXSTRTPLDEPTH];
};

struct NeonAstLocal
{
    bool iscaptured;
    int depth;
    NeonAstToken name;
};

struct NeonAstUpvalue
{
    bool islocal;
    uint16_t index;
};

struct NeonAstFuncCompiler
{
    int localcount;
    int scopedepth;
    int handlercount;
    bool fromimport;
    NeonAstFuncCompiler* enclosing;
    /* current function */
    NeonObjFuncScript* targetfunc;
    NeonFuncType type;
    NeonAstLocal locals[NEON_CFG_ASTMAXLOCALS];
    NeonAstUpvalue upvalues[NEON_CFG_ASTMAXUPVALS];
};

struct NeonAstClassCompiler
{
    bool hassuperclass;
    NeonAstClassCompiler* enclosing;
    NeonAstToken name;
};

struct NeonAstParser
{
    bool haderror;
    bool panicmode;
    bool isreturning;
    bool istrying;
    bool replcanecho;
    bool keeplastvalue;
    bool lastwasstatement;
    bool infunction;
    /* used for tracking loops for the continue statement... */
    int innermostloopstart;
    int innermostloopscopedepth;
    int blockcount;
    /* the context in which the parser resides; none (outer level), inside a class, dict, array, etc */
    NeonAstCompContext compcontext;
    const char* currentfile;
    NeonState* pvm;
    NeonAstLexer* lexer;
    NeonAstToken currtoken;
    NeonAstToken prevtoken;
    NeonAstFuncCompiler* currentfunccompiler;
    NeonAstClassCompiler* currentclasscompiler;
    NeonObjModule* currentmodule;
};

struct NeonAstRule
{
    NeonAstParsePrefixFN prefix;
    NeonAstParseInfixFN infix;
    NeonAstPrecedence precedence;
};

struct NeonRegFunc
{
    const char* name;
    bool isstatic;
    NeonNativeFN function;
};

struct NeonRegField
{
    const char* name;
    bool isstatic;
    NeonClassFieldFN fieldvalfn;
};

struct NeonRegClass
{
    const char* name;
    NeonRegField* fields;
    NeonRegFunc* functions;
};

struct NeonRegModule
{
    /*
    * the name of this module.
    * note: if the name must be preserved, copy it; it is only a pointer to a
    * string that gets freed past loading.
    */
    const char* name;

    /* exported fields, if any. */
    NeonRegField* fields;

    /* regular functions, if any. */
    NeonRegFunc* functions;

    /* exported classes, if any.
    * i.e.:
    * {"Stuff",
    *   (NeonRegField[]){
    *       {"enabled", true},
    *       ...
    *   },
    *   (NeonRegFunc[]){
    *       {"isStuff", myclass_fn_isstuff},
    *       ...
    * }})*/
    NeonRegClass* classes;

    /* function that is called directly upon loading the module. can be NULL. */
    NeonModLoaderFN preloader;

    /* function that is called before unloading the module. can be NULL. */
    NeonModLoaderFN unloader;
};

struct NeonArgCheck
{
    NeonState* pvm;
    const char* name;
    int argc;
    NeonValue* argv;
};

struct NeonArguments
{
    /* number of arguments */
    int count;

    /* the actual arguments */
    NeonValue* args;

    /*
    * if called within something with an instance, this is where its reference will be.
    * otherwise, empty.
    */
    NeonValue thisval;

    /*
    * the name of the function being called.
    * note: this is the *declarative* name; meaning, on an alias'd func, it will be
    * the name of the origin.
    */
    const char* name;

    /*
    * a userpointer, if declared with a userpointer. otherwise NULL.
    */
    void* userptr;
};

#include "prot.inc"

static NEON_FORCEINLINE bool nn_value_isobject(NeonValue v)
{
    return ((v).type == NEON_VALTYPE_OBJ);
}

static NEON_FORCEINLINE  NeonObject* nn_value_asobject(NeonValue v)
{
    return ((v).valunion.vobjpointer);
}

static NEON_FORCEINLINE  bool nn_value_isobjtype(NeonValue v, NeonObjType t)
{
    return nn_value_isobject(v) && nn_value_asobject(v)->type == t;
}

static NEON_FORCEINLINE  bool nn_value_isnull(NeonValue v)
{
    return ((v).type == NEON_VALTYPE_NULL);
}

static NEON_FORCEINLINE  bool nn_value_isbool(NeonValue v)
{
    return ((v).type == NEON_VALTYPE_BOOL);
}

static NEON_FORCEINLINE  bool nn_value_isnumber(NeonValue v)
{
    return ((v).type == NEON_VALTYPE_NUMBER);
}

static NEON_FORCEINLINE  bool nn_value_isempty(NeonValue v)
{
    return ((v).type == NEON_VALTYPE_EMPTY);
}

static NEON_FORCEINLINE  bool nn_value_isstring(NeonValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_STRING);
}

static NEON_FORCEINLINE  bool nn_value_isfuncnative(NeonValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCNATIVE);
}

static NEON_FORCEINLINE  bool nn_value_isfuncscript(NeonValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCSCRIPT);
}

static NEON_FORCEINLINE  bool nn_value_isfuncclosure(NeonValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCCLOSURE);
}

static NEON_FORCEINLINE  bool nn_value_isfuncbound(NeonValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FUNCBOUND);
}

static NEON_FORCEINLINE  bool nn_value_isclass(NeonValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_CLASS);
}

static NEON_FORCEINLINE  bool nn_value_isinstance(NeonValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_INSTANCE);
}

static NEON_FORCEINLINE  bool nn_value_isarray(NeonValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_ARRAY);
}

static NEON_FORCEINLINE  bool nn_value_isdict(NeonValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_DICT);
}

static NEON_FORCEINLINE  bool nn_value_isfile(NeonValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_FILE);
}

static NEON_FORCEINLINE  bool nn_value_isrange(NeonValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_RANGE);
}

static NEON_FORCEINLINE  bool nn_value_ismodule(NeonValue v)
{
    return nn_value_isobjtype(v, NEON_OBJTYPE_MODULE);
}

static NEON_FORCEINLINE  bool nn_value_iscallable(NeonValue v)
{
    return (
        nn_value_isclass(v) ||
        nn_value_isfuncscript(v) ||
        nn_value_isfuncclosure(v) ||
        nn_value_isfuncbound(v) ||
        nn_value_isfuncnative(v)
    );
}

static NEON_FORCEINLINE  NeonObjType nn_value_objtype(NeonValue v)
{
    return nn_value_asobject(v)->type;
}

static NEON_FORCEINLINE  bool nn_value_asbool(NeonValue v)
{
    return ((v).valunion.vbool);
}

static NEON_FORCEINLINE  double nn_value_asnumber(NeonValue v)
{
    return ((v).valunion.vfltnum);
}

static NEON_FORCEINLINE  NeonObjString* nn_value_asstring(NeonValue v)
{
    return ((NeonObjString*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonObjFuncNative* nn_value_asfuncnative(NeonValue v)
{
    return ((NeonObjFuncNative*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonObjFuncScript* nn_value_asfuncscript(NeonValue v)
{
    return ((NeonObjFuncScript*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonObjFuncClosure* nn_value_asfuncclosure(NeonValue v)
{
    return ((NeonObjFuncClosure*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonObjClass* nn_value_asclass(NeonValue v)
{
    return ((NeonObjClass*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonObjInstance* nn_value_asinstance(NeonValue v)
{
    return ((NeonObjInstance*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonObjFuncBound* nn_value_asfuncbound(NeonValue v)
{
    return ((NeonObjFuncBound*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonObjSwitch* nn_value_asswitch(NeonValue v)
{
    return ((NeonObjSwitch*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonObjUserdata* nn_value_asuserdata(NeonValue v)
{
    return ((NeonObjUserdata*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonObjModule* nn_value_asmodule(NeonValue v)
{
    return ((NeonObjModule*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonObjArray* nn_value_asarray(NeonValue v)
{
    return ((NeonObjArray*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonObjDict* nn_value_asdict(NeonValue v)
{
    return ((NeonObjDict*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonObjFile* nn_value_asfile(NeonValue v)
{
    return ((NeonObjFile*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonObjRange* nn_value_asrange(NeonValue v)
{
    return ((NeonObjRange*)nn_value_asobject(v));
}

static NEON_FORCEINLINE  NeonValue nn_value_makevalue(NeonValType type)
{
    NeonValue v;
    v.type = type;
    return v;
}

static NEON_FORCEINLINE  NeonValue nn_value_makeempty()
{
    return nn_value_makevalue(NEON_VALTYPE_EMPTY);
}

static NEON_FORCEINLINE  NeonValue nn_value_makenull()
{
    NeonValue v;
    v = nn_value_makevalue(NEON_VALTYPE_NULL);
    return v;
}

static NEON_FORCEINLINE  NeonValue nn_value_makebool(bool b)
{
    NeonValue v;
    v = nn_value_makevalue(NEON_VALTYPE_BOOL);
    v.valunion.vbool = b;
    return v;
}

static NEON_FORCEINLINE  NeonValue nn_value_makenumber(double d)
{
    NeonValue v;
    v = nn_value_makevalue(NEON_VALTYPE_NUMBER);
    v.valunion.vfltnum = d;
    return v;
}

static NEON_FORCEINLINE  NeonValue nn_value_makeint(int i)
{
    NeonValue v;
    v = nn_value_makevalue(NEON_VALTYPE_NUMBER);
    v.valunion.vfltnum = i;
    return v;
}

#define nn_value_fromobject(obj) nn_value_fromobject_actual((NeonObject*)obj)

static NEON_FORCEINLINE  NeonValue nn_value_fromobject_actual(NeonObject* obj)
{
    NeonValue v;
    v = nn_value_makevalue(NEON_VALTYPE_OBJ);
    v.valunion.vobjpointer = obj;
    return v;
}


