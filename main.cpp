
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
    NeonState::GC::reallocate(state, pointer, (typsz) * (oldcount), (typsz) * (newcount))

#define nn_gcmem_freearray(state, typsz, pointer, oldcount) \
    NeonState::GC::release(state, pointer, (typsz) * (oldcount))

#define NORMALIZE(token) #token

#define nn_exceptions_throwclass(state, exklass, ...) \
    state->raiseClass(exklass, __FILE__, __LINE__, __VA_ARGS__)

#define nn_exceptions_throwException(state, ...) \
    nn_exceptions_throwclass(state, state->exceptions.stdexception, __VA_ARGS__)

#if defined(__linux__) || defined(__CYGWIN__) || defined(__MINGW32_MAJOR_VERSION)
    #include <libgen.h>
    #include <limits.h>
#endif

#define NEON_RETURNERROR(...) \
    { \
        state->stackPop(args->argc); \
        nn_exceptions_throwException(state, ##__VA_ARGS__); \
    } \
    return NeonValue::makeBool(false);

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
typedef enum /**/ NeonOpCode NeonOpCode;
typedef enum /**/ NeonFuncType NeonFuncType;
typedef enum /**/ NeonObjType NeonObjType;
typedef enum /**/ NeonStatus NeonStatus;
typedef enum /**/ NeonAstTokType NeonAstTokType;
typedef enum /**/ NeonAstPrecedence NeonAstPrecedence;
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
typedef struct /**/ NeonHashTable NeonHashTable;
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

// preproto
uint32_t nn_util_hashstring(const char* key, int length);
void nn_gcmem_markvalue(NeonState* state, NeonValue value);
void nn_import_closemodule(void *dlw);
void nn_blob_destroy(NeonState* state, NeonBlob* blob);
void nn_printer_printvalue(NeonPrinter *wr, NeonValue value, bool fixstring, bool invmethod);
uint32_t nn_value_hashvalue(NeonValue value);
bool nn_value_compare_actual(NeonState* state, NeonValue a, NeonValue b);
bool nn_value_compare(NeonState *state, NeonValue a, NeonValue b);
NeonValue nn_value_copyvalue(NeonState* state, NeonValue value);
void nn_gcmem_destroylinkedobjects(NeonState *state);

class NeonMemory
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


struct NeonValue
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

    public:
        static NEON_ALWAYSINLINE NeonValue makeValue(Type type)
        {
            NeonValue v;
            v.m_valtype = type;
            return v;
        }

        static NEON_ALWAYSINLINE NeonValue makeEmpty()
        {
            return makeValue(Type::T_EMPTY);
        }

        static NEON_ALWAYSINLINE NeonValue makeNull()
        {
            NeonValue v;
            v = makeValue(Type::T_NULL);
            return v;
        }

        static NEON_ALWAYSINLINE NeonValue makeBool(bool b)
        {
            NeonValue v;
            v = makeValue(Type::T_BOOL);
            v.m_valunion.boolean = b;
            return v;
        }

        static NEON_ALWAYSINLINE NeonValue makeNumber(double d)
        {
            NeonValue v;
            v = makeValue(Type::T_NUMBER);
            v.m_valunion.number = d;
            return v;
        }

        static NEON_ALWAYSINLINE NeonValue makeInt(int i)
        {
            NeonValue v;
            v = makeValue(Type::T_NUMBER);
            v.m_valunion.number = i;
            return v;
        }

        template<typename ObjT>
        static NEON_ALWAYSINLINE NeonValue fromObject(ObjT* obj)
        {
            NeonValue v;
            v = makeValue(Type::T_OBJECT);
            v.m_valunion.obj = (NeonObject*)obj;
            return v;
        }

    public:
        Type m_valtype;
        union
        {
            bool boolean;
            double number;
            NeonObject* obj;
        } m_valunion;

    public:
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

        NEON_ALWAYSINLINE bool isObjType(NeonObjType t) const;

        NEON_ALWAYSINLINE bool isFuncNative() const
        {
            return this->isObjType(NEON_OBJTYPE_FUNCNATIVE);
        }

        NEON_ALWAYSINLINE bool isFuncScript() const
        {
            return this->isObjType(NEON_OBJTYPE_FUNCSCRIPT);
        }

        NEON_ALWAYSINLINE bool isFuncClosure() const
        {
            return this->isObjType(NEON_OBJTYPE_FUNCCLOSURE);
        }

        NEON_ALWAYSINLINE bool isFuncBound() const
        {
            return this->isObjType(NEON_OBJTYPE_FUNCBOUND);
        }

        NEON_ALWAYSINLINE bool isClass() const
        {
            return this->isObjType(NEON_OBJTYPE_CLASS);
        }

        NEON_ALWAYSINLINE bool isCallable() const
        {
            return (
                this->isClass() ||
                this->isFuncScript() ||
                this->isFuncClosure() ||
                this->isFuncBound() ||
                this->isFuncNative()
            );
        }

        NEON_ALWAYSINLINE bool isString() const
        {
            return this->isObjType(NEON_OBJTYPE_STRING);
        }

        NEON_ALWAYSINLINE  bool isBool() const
        {
            return (this->type() == NeonValue::Type::T_BOOL);
        }

        NEON_ALWAYSINLINE  bool isNumber() const
        {
            return (this->type() == NeonValue::Type::T_NUMBER);
        }

        NEON_ALWAYSINLINE  bool isInstance() const
        {
            return this->isObjType(NEON_OBJTYPE_INSTANCE);
        }

        NEON_ALWAYSINLINE  bool isArray() const
        {
            return this->isObjType(NEON_OBJTYPE_ARRAY);
        }

        NEON_ALWAYSINLINE  bool isDict() const
        {
            return this->isObjType(NEON_OBJTYPE_DICT);
        }

        NEON_ALWAYSINLINE  bool isFile() const
        {
            return isObjType(NEON_OBJTYPE_FILE);
        }

        NEON_ALWAYSINLINE  bool isRange() const
        {
            return this->isObjType(NEON_OBJTYPE_RANGE);
        }

        NEON_ALWAYSINLINE  bool isModule() const
        {
            return this->isObjType(NEON_OBJTYPE_MODULE);
        }

        NEON_ALWAYSINLINE NeonObject* asObject()
        {
            return (m_valunion.obj);
        }

        NEON_ALWAYSINLINE const NeonObject* asObject() const
        {
            return (m_valunion.obj);
        }

        NEON_ALWAYSINLINE double asNumber()
        {
            return (m_valunion.number);
        }

        NEON_ALWAYSINLINE  NeonObjString* asString()
        {
            return ((NeonObjString*)this->asObject());
        }

        NEON_ALWAYSINLINE  NeonObjArray* asArray()
        {
            return ((NeonObjArray*)this->asObject());
        }

        NEON_ALWAYSINLINE  NeonObjInstance* asInstance()
        {
            return ((NeonObjInstance*)this->asObject());
        }

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


struct NeonState
{
    public:
        struct ExceptionFrame
        {
            uint16_t address;
            uint16_t finallyaddress;
            NeonObjClass* klass;
        };

        struct CallFrame
        {
            int handlercount;
            int gcprotcount;
            int stackslotpos;
            NeonInstruction* inscode;
            NeonObjFuncClosure* closure;
            ExceptionFrame handlers[NEON_CFG_MAXEXCEPTHANDLERS];
        };

        struct GC
        {
            static void collectGarbage(NeonState *state);

            static void maybeCollect(NeonState* state, int addsize, bool wasnew)
            {
                state->gcstate.bytesallocated += addsize;
                if(state->gcstate.nextgc > 0)
                {
                    if(wasnew && state->gcstate.bytesallocated > state->gcstate.nextgc)
                    {
                        if(state->vmstate.currentframe && state->vmstate.currentframe->gcprotcount == 0)
                        {
                            GC::collectGarbage(state);
                        }
                    }
                }
            }

            static void* reallocate(NeonState* state, void* pointer, size_t oldsize, size_t newsize)
            {
                void* result;
                GC::maybeCollect(state, newsize - oldsize, newsize > oldsize);
                result = realloc(pointer, newsize);
                /*
                // just in case reallocation fails... computers ain't infinite!
                */
                if(result == NULL)
                {
                    fprintf(stderr, "fatal error: failed to allocate %ld bytes\n", newsize);
                    abort();
                }
                return result;
            }

            static void release(NeonState* state, void* pointer, size_t oldsize)
            {
                GC::maybeCollect(state, -oldsize, false);
                if(oldsize > 0)
                {
                    memset(pointer, 0, oldsize);
                }
                free(pointer);
                pointer = NULL;
            }

            static void* allocate(NeonState* state, size_t size, size_t amount)
            {
                return GC::reallocate(state, NULL, 0, size * amount);
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
        } conf;

        struct
        {
            size_t stackidx = 0;
            size_t stackcapacity = 0;
            size_t framecapacity = 0;
            size_t framecount = 0;
            NeonInstruction currentinstr;
            CallFrame* currentframe = nullptr;
            NeonObjUpvalue* openupvalues = nullptr;
            NeonObject* linkedobjects = nullptr;
            CallFrame* framevalues = nullptr;
            NeonValue* stackvalues = nullptr;
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

        NeonAstFuncCompiler* compiler;

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
        NeonPrinter* stdoutwriter;
        NeonPrinter* stderrwriter;
        NeonPrinter* debugwriter;

    private:
        /*
        * grows vmstate.(stack|frame)values, respectively.
        * currently it works fine with mob.js (man-or-boy test), although
        * there are still some invalid reads regarding the closure;
        * almost definitely because the pointer address changes.
        *
        * currently, the implementation really does just increase the
        * memory block available:
        * i.e., [NeonValue x 32] -> [NeonValue x <newsize>], without
        * copying anything beyond primitive values.
        */
        bool resizeStack(size_t needed)
        {
            size_t oldsz;
            size_t newsz;
            size_t allocsz;
            size_t nforvals;
            NeonValue* oldbuf;
            NeonValue* newbuf;
            if(((int)needed < 0) /* || (needed < this->vmstate.stackcapacity) */)
            {
                return true;
            }
            nforvals = (needed * 2);
            oldsz = this->vmstate.stackcapacity;
            newsz = oldsz + nforvals;
            allocsz = ((newsz + 1) * sizeof(NeonValue));
            fprintf(stderr, "*** resizing stack: needed %ld (%ld), from %ld to %ld, allocating %ld ***\n", nforvals, needed, oldsz, newsz, allocsz);
            oldbuf = this->vmstate.stackvalues;
            newbuf = (NeonValue*)realloc(oldbuf, allocsz );
            if(newbuf == NULL)
            {
                fprintf(stderr, "internal error: failed to resize stackvalues!\n");
                abort();
            }
            this->vmstate.stackvalues = (NeonValue*)newbuf;
            fprintf(stderr, "oldcap=%ld newsz=%ld\n", this->vmstate.stackcapacity, newsz);
            this->vmstate.stackcapacity = newsz;
            return true;
        }

        bool resizeFrames(size_t needed)
        {
            /* return false; */
            size_t oldsz;
            size_t newsz;
            size_t allocsz;
            int oldhandlercnt;
            NeonInstruction* oldip;
            NeonObjFuncClosure* oldclosure;
            NeonState::CallFrame* oldbuf;
            NeonState::CallFrame* newbuf;
            fprintf(stderr, "*** resizing frames ***\n");
            oldclosure = this->vmstate.currentframe->closure;
            oldip = this->vmstate.currentframe->inscode;
            oldhandlercnt = this->vmstate.currentframe->handlercount;
            oldsz = this->vmstate.framecapacity;
            newsz = oldsz + needed;
            allocsz = ((newsz + 1) * sizeof(NeonState::CallFrame));
            #if 1
                oldbuf = this->vmstate.framevalues;
                newbuf = (NeonState::CallFrame*)realloc(oldbuf, allocsz);
                if(newbuf == NULL)
                {
                    fprintf(stderr, "internal error: failed to resize framevalues!\n");
                    abort();
                }
            #endif
            this->vmstate.framevalues = (NeonState::CallFrame*)newbuf;
            this->vmstate.framecapacity = newsz;
            /*
            * this bit is crucial: realloc changes pointer addresses, and to keep the
            * current frame, re-read it from the new address.
            */
            this->vmstate.currentframe = &this->vmstate.framevalues[this->vmstate.framecount - 1];
            this->vmstate.currentframe->handlercount = oldhandlercnt;
            this->vmstate.currentframe->inscode = oldip;
            this->vmstate.currentframe->closure = oldclosure;
            return true;
        }

        void init(void* userptr);

    public:
        NeonState()
        {
            init(NULL);
        }

        ~NeonState();

        bool checkMaybeResize()
        {
            if((this->vmstate.stackidx+1) >= this->vmstate.stackcapacity)
            {
                if(!this->resizeStack(this->vmstate.stackidx + 1))
                {
                    return nn_exceptions_throwException(this, "failed to resize stack due to overflow");
                }
                return true;
            }
            if(this->vmstate.framecount >= this->vmstate.framecapacity)
            {
                if(!this->resizeFrames(this->vmstate.framecapacity + 1))
                {
                    return nn_exceptions_throwException(this, "failed to resize frames due to overflow");
                }
                return true;
            }
            return false;
        }

        template<typename ObjT>
        ObjT* gcProtect(ObjT* object)
        {
            size_t frpos;
            this->stackPush(NeonValue::fromObject(object));
            frpos = 0;
            if(this->vmstate.framecount > 0)
            {
                frpos = this->vmstate.framecount - 1;
            }
            this->vmstate.framevalues[frpos].gcprotcount++;
            return object;
        }

        void clearProtect()
        {
            size_t frpos;
            CallFrame* frame;
            frpos = 0;
            if(this->vmstate.framecount > 0)
            {
                frpos = this->vmstate.framecount - 1;
            }
            frame = &this->vmstate.framevalues[frpos];
            if(frame->gcprotcount > 0)
            {
                this->vmstate.stackidx -= frame->gcprotcount;
            }
            frame->gcprotcount = 0;
        }

        template<typename... ArgsT>
        void raiseFatal(const char* format, ArgsT&&... args);

        template<typename... ArgsT>
        void warn(const char* fmt, ArgsT&&... args)
        {
            if(this->conf.enablewarnings)
            {
                fprintf(stderr, "WARNING: ");
                fprintf(stderr, fmt, args...);
                fprintf(stderr, "\n");
            }
        }

        bool exceptionPushHandler(NeonObjClass* type, int address, int finallyaddress)
        {
            NeonState::CallFrame* frame;
            frame = &this->vmstate.framevalues[this->vmstate.framecount - 1];
            if(frame->handlercount == NEON_CFG_MAXEXCEPTHANDLERS)
            {
                this->raiseFatal("too many nested exception handlers in one function");
                return false;
            }
            frame->handlers[frame->handlercount].address = address;
            frame->handlers[frame->handlercount].finallyaddress = finallyaddress;
            frame->handlers[frame->handlercount].klass = type;
            frame->handlercount++;
            return true;
        }

        bool exceptionHandleUncaught(NeonObjInstance* exception);

        bool exceptionPropagate();

        template<typename... ArgsT>
        bool raiseClass(NeonObjClass* exklass, const char* srcfile, int srcline, const char* format, ArgsT&&... args);

        NEON_ALWAYSINLINE void stackPush(NeonValue value)
        {
            this->checkMaybeResize();
            this->vmstate.stackvalues[this->vmstate.stackidx] = value;
            this->vmstate.stackidx++;
        }

        NEON_ALWAYSINLINE NeonValue stackPop()
        {
            NeonValue v;
            this->vmstate.stackidx--;
            v = this->vmstate.stackvalues[this->vmstate.stackidx];
            return v;
        }

        NEON_ALWAYSINLINE NeonValue stackPop(int n)
        {
            NeonValue v;
            this->vmstate.stackidx -= n;
            v = this->vmstate.stackvalues[this->vmstate.stackidx];
            return v;
        }

        NEON_ALWAYSINLINE NeonValue stackPeek(int distance)
        {
            NeonValue v;
            v = this->vmstate.stackvalues[this->vmstate.stackidx + (-1 - distance)];
            return v;
        }

};

struct NeonObject
{
    public:
        template<typename ObjT>
        static ObjT* make(NeonState* state, NeonObjType type)
        {
            //size_t size;
            ObjT* actualobj;
            NeonObject* object;
            //size = sizeof(ObjT);
            //object = (NeonObject*)NeonState::GC::allocate(state, size, 1);
            actualobj = new ObjT;
            object = (NeonObject*)actualobj;
            object->m_objtype = type;
            object->m_mark = !state->markvalue;
            object->m_isstale = false;
            object->m_pvm = state;
            object->m_nextobj = state->vmstate.linkedobjects;
            state->vmstate.linkedobjects = object;
            #if defined(DEBUG_GC) && DEBUG_GC
            state->debugwriter->putformat("%p allocate %ld for %d\n", (void*)object, size, type);
            #endif
            return actualobj;
        }

    public:
        NeonObjType m_objtype;
        bool m_mark;
        NeonState* m_pvm;
        /*
        // when an object is marked as stale, it means that the
        // GC will never collect this object. This can be useful
        // for library/package objects that want to reuse native
        // objects in their types/pointers. The GC cannot reach
        // them yet, so it's best for them to be kept stale.
        */
        bool m_isstale;
        NeonObject* m_nextobj;

    public:
};

NEON_ALWAYSINLINE bool NeonValue::isObjType(NeonObjType t) const
{
    return this->isObject() && this->asObject()->m_objtype == t;
}

struct NeonValArray
{
    public:
        NeonState* m_pvm;
        /* type size of the stored value (via sizeof) */
        int m_typsize;
        /* how many entries are currently stored? */
        int m_count;
        /* how many entries can be stored before growing? */
        int capacity;
        NeonValue* values;

    private:
        void makeSize(NeonState* state, size_t size)
        {
            this->m_pvm = state;
            this->m_typsize = size;
            this->capacity = 0;
            this->m_count = 0;
            this->values = NULL;
        }

        bool destroy()
        {
            if(this->values != nullptr)
            {
                nn_gcmem_freearray(this->m_pvm, this->m_typsize, this->values, this->capacity);
                this->values = nullptr;
                this->m_count = 0;
                this->capacity = 0;
                return true;
            }
            return false;
        }

    public:

        NeonValArray(NeonState* state, size_t size)
        {
            makeSize(state, size);
        }

        NeonValArray(NeonState* state)
        {
            makeSize(state, sizeof(NeonValue));
        }

        ~NeonValArray()
        {
            destroy();
        }

        void gcMark()
        {
            int i;
            for(i = 0; i < this->m_count; i++)
            {
                nn_gcmem_markvalue(this->m_pvm, this->values[i]);
            }
        }

        void push(NeonValue value)
        {
            int oldcapacity;
            if(this->capacity < this->m_count + 1)
            {
                oldcapacity = this->capacity;
                this->capacity = GROW_CAPACITY(oldcapacity);
                this->values = (NeonValue*)nn_gcmem_growarray(this->m_pvm, this->m_typsize, this->values, oldcapacity, this->capacity);
            }
            this->values[this->m_count] = value;
            this->m_count++;
        }

        void insert(NeonValue value, int index)
        {
            int i;
            int oldcap;
            if(this->capacity <= index)
            {
                this->capacity = GROW_CAPACITY(index);
                this->values = (NeonValue*)nn_gcmem_growarray(this->m_pvm, this->m_typsize, this->values, this->m_count, this->capacity);
            }
            else if(this->capacity < this->m_count + 2)
            {
                oldcap = this->capacity;
                this->capacity = GROW_CAPACITY(oldcap);
                this->values = (NeonValue*)nn_gcmem_growarray(this->m_pvm, this->m_typsize, this->values, oldcap, this->capacity);
            }
            if(index <= this->m_count)
            {
                for(i = this->m_count - 1; i >= index; i--)
                {
                    this->values[i + 1] = this->values[i];
                }
            }
            else
            {
                for(i = this->m_count; i < index; i++)
                {
                    /* null out overflow indices */
                    this->values[i] = NeonValue::makeNull();
                    this->m_count++;
                }
            }
            this->values[index] = value;
            this->m_count++;
        }
};

struct NeonPropGetSet
{
    NeonValue getter;
    NeonValue setter;
};

struct NeonProperty
{
    public:
        NeonFieldType type;
        NeonValue value;
        bool havegetset;
        NeonPropGetSet getset;

    public:
        void init(NeonValue val, NeonFieldType t)
        {
            this->type = t;
            this->value = val;
            this->havegetset = false;
        }

        NeonProperty(NeonValue val, NeonValue getter, NeonValue setter, NeonFieldType t)
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

        NeonProperty(NeonValue val, NeonFieldType t)
        {
            init(val, t);
        }
};

struct NeonHashTable
{
    public:
        struct Entry
        {
            NeonValue key;
            NeonProperty value;
        };

    public:
        /*
        * FIXME: extremely stupid hack: $active ensures that a table that was destroyed
        * does not get marked again, et cetera.
        * since ~NeonHashTable() zeroes the data before freeing, $active will be
        * false, and thus, no further marking occurs.
        * obviously the reason is that somewhere a table (from NeonObjInstance) is being
        * read after being freed, but for now, this will work(-ish).
        */
        bool active;
        int m_count;
        int capacity;
        NeonState* m_pvm;
        Entry* entries;

    public:

        NeonHashTable(NeonState* state)
        {
            this->m_pvm = state;
            this->active = true;
            this->m_count = 0;
            this->capacity = 0;
            this->entries = NULL;
        }

        ~NeonHashTable()
        {
            NeonState* state;
            state = this->m_pvm;
            nn_gcmem_freearray(state, sizeof(Entry), this->entries, this->capacity);
        }

        Entry* findEntryByValue(Entry* availents, int availcap, NeonValue key)
        {
            uint32_t hash;
            uint32_t index;
            NeonState* state;
            Entry* entry;
            Entry* tombstone;
            state = this->m_pvm;
            hash = nn_value_hashvalue(key);
            #if defined(DEBUG_TABLE) && DEBUG_TABLE
            fprintf(stderr, "looking for key ");
            nn_printer_printvalue(state->debugwriter, key, true, false);
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

        Entry* findEntryByStr(Entry* availents, int availcap, NeonValue valkey, const char* kstr, size_t klen, uint32_t hash);

        NeonProperty* getFieldByValue(NeonValue key)
        {
            NeonState* state;
            Entry* entry;
            (void)state;
            state = this->m_pvm;
            if(this->m_count == 0 || this->entries == NULL)
            {
                return NULL;
            }
            #if defined(DEBUG_TABLE) && DEBUG_TABLE
            fprintf(stderr, "getting entry with hash %u...\n", nn_value_hashvalue(key));
            #endif
            entry = this->findEntryByValue(this->entries, this->capacity, key);
            if(entry->key.isEmpty() || entry->key.isNull())
            {
                return NULL;
            }
            #if defined(DEBUG_TABLE) && DEBUG_TABLE
            fprintf(stderr, "found entry for hash %u == ", nn_value_hashvalue(entry->key));
            nn_printer_printvalue(state->debugwriter, entry->value.value, true, false);
            fprintf(stderr, "\n");
            #endif
            return &entry->value;
        }

        NeonProperty* getFieldByStr(NeonValue valkey, const char* kstr, size_t klen, uint32_t hash)
        {
            NeonState* state;
            Entry* entry;
            (void)state;
            state = this->m_pvm;
            if(this->m_count == 0 || this->entries == NULL)
            {
                return NULL;
            }
            #if defined(DEBUG_TABLE) && DEBUG_TABLE
            fprintf(stderr, "getting entry with hash %u...\n", nn_value_hashvalue(key));
            #endif
            entry = this->findEntryByStr(this->entries, this->capacity, valkey, kstr, klen, hash);
            if(entry->key.isEmpty() || entry->key.isNull())
            {
                return NULL;
            }
            #if defined(DEBUG_TABLE) && DEBUG_TABLE
            fprintf(stderr, "found entry for hash %u == ", nn_value_hashvalue(entry->key));
            nn_printer_printvalue(state->debugwriter, entry->value.value, true, false);
            fprintf(stderr, "\n");
            #endif
            return &entry->value;
        }

        NeonProperty* getFieldByObjStr(NeonObjString* str);

        NeonProperty* getFieldByCStr(const char* kstr)
        {
            size_t klen;
            uint32_t hash;
            klen = strlen(kstr);
            hash = nn_util_hashstring(kstr, klen);
            return this->getFieldByStr(NeonValue::makeEmpty(), kstr, klen, hash);
        }

        NeonProperty* getField(NeonValue key);

        bool get(NeonValue key, NeonValue* value)
        {
            NeonProperty* field;
            field = this->getField(key);
            if(field != NULL)
            {
                *value = field->value;
                return true;
            }
            return false;
        }

        void adjustCapacity(int needcap)
        {
            int i;
            NeonState* state;
            Entry* dest;
            Entry* entry;
            Entry* nents;
            state = this->m_pvm;
            nents = (Entry*)NeonState::GC::allocate(state, sizeof(Entry), needcap);
            for(i = 0; i < needcap; i++)
            {
                nents[i].key = NeonValue::makeEmpty();
                nents[i].value = NeonProperty(NeonValue::makeNull(), NEON_PROPTYPE_VALUE);
            }
            /* repopulate buckets */
            this->m_count = 0;
            for(i = 0; i < this->capacity; i++)
            {
                entry = &this->entries[i];
                if(entry->key.isEmpty())
                {
                    continue;
                }
                dest = this->findEntryByValue(nents, needcap, entry->key);
                dest->key = entry->key;
                dest->value = entry->value;
                this->m_count++;
            }
            /* free the old entries... */
            nn_gcmem_freearray(state, sizeof(Entry), this->entries, this->capacity);
            this->entries = nents;
            this->capacity = needcap;
        }

        bool setType(NeonValue key, NeonValue value, NeonFieldType ftyp, bool keyisstring)
        {
            bool isnew;
            int needcap;
            Entry* entry;
            (void)keyisstring;
            if(this->m_count + 1 > this->capacity * NEON_CFG_MAXTABLELOAD)
            {
                needcap = GROW_CAPACITY(this->capacity);
                this->adjustCapacity(needcap);
            }
            entry = this->findEntryByValue(this->entries, this->capacity, key);
            isnew = entry->key.isEmpty();
            if(isnew && entry->value.value.isNull())
            {
                this->m_count++;
            }
            /* overwrites existing entries. */
            entry->key = key;
            entry->value = NeonProperty(value, ftyp);
            return isnew;
        }

        bool set(NeonValue key, NeonValue value)
        {
            return this->setType(key, value, NEON_PROPTYPE_VALUE, key.isString());
        }

        bool setCStrType(const char* cstrkey, NeonValue value, NeonFieldType ftype);

        bool setCStr(const char* cstrkey, NeonValue value)
        {
            return setCStrType(cstrkey, value, NEON_PROPTYPE_VALUE);
        }

        bool removeByKey(NeonValue key)
        {
            Entry* entry;
            if(this->m_count == 0)
            {
                return false;
            }
            /* find the entry */
            entry = this->findEntryByValue(this->entries, this->capacity, key);
            if(entry->key.isEmpty())
            {
                return false;
            }
            /* place a tombstone in the entry. */
            entry->key = NeonValue::makeEmpty();
            entry->value = NeonProperty(NeonValue::makeBool(true), NEON_PROPTYPE_VALUE);
            return true;
        }

        static void addAll(NeonHashTable* from, NeonHashTable* to)
        {
            int i;
            Entry* entry;
            for(i = 0; i < from->capacity; i++)
            {
                entry = &from->entries[i];
                if(!entry->key.isEmpty())
                {
                    to->setType(entry->key, entry->value.value, entry->value.type, false);
                }
            }
        }

        static void copy(NeonHashTable* from, NeonHashTable* to)
        {
            int i;
            NeonState* state;
            Entry* entry;
            state = from->m_pvm;
            for(i = 0; i < (int)from->capacity; i++)
            {
                entry = &from->entries[i];
                if(!entry->key.isEmpty())
                {
                    to->setType(entry->key, nn_value_copyvalue(state, entry->value.value), entry->value.type, false);
                }
            }
        }

        NeonObjString* findString(const char* chars, size_t length, uint32_t hash);

        NeonValue findKey(NeonValue value)
        {
            int i;
            Entry* entry;
            for(i = 0; i < (int)this->capacity; i++)
            {
                entry = &this->entries[i];
                if(!entry->key.isNull() && !entry->key.isEmpty())
                {
                    if(nn_value_compare(this->m_pvm, entry->value.value, value))
                    {
                        return entry->key;
                    }
                }
            }
            return NeonValue::makeNull();
        }

        void printTo(NeonPrinter* pd, const char* name);

        static void mark(NeonState* state, NeonHashTable* table)
        {
            int i;
            Entry* entry;
            if(table == NULL)
            {
                return;
            }
            if(!table->active)
            {
                state->warn("trying to mark inactive hashtable <%p>!", table);
                return;
            }
            for(i = 0; i < table->capacity; i++)
            {
                entry = &table->entries[i];
                if(entry != NULL)
                {
                    nn_gcmem_markvalue(state, entry->key);
                    nn_gcmem_markvalue(state, entry->value.value);
                }
            }
        }

        static void removeMarked(NeonState* state, NeonHashTable* table)
        {
            int i;
            Entry* entry;
            for(i = 0; i < table->capacity; i++)
            {
                entry = &table->entries[i];
                if(entry->key.isObject() && entry->key.asObject()->m_mark != state->markvalue)
                {
                    table->removeByKey(entry->key);
                }
            }
        }

};

struct NeonObjString: public NeonObject
{
    public:
        static void putInterned(NeonState* state, NeonObjString* rs)
        {
            state->stackPush(NeonValue::fromObject(rs));
            state->strings->set(NeonValue::fromObject(rs), NeonValue::makeNull());
            state->stackPop();
        }

        static NeonObjString* findInterned(NeonState* state, const char* chars, size_t length, uint32_t hash)
        {
            return state->strings->findString(chars, length, hash);
        }

        static NeonObjString* makeFromStrbuf(NeonState* state, StringBuffer* sbuf, uint32_t hash)
        {
            NeonObjString* rs;
            rs = (NeonObjString*)NeonObject::make<NeonObjString>(state, NEON_OBJTYPE_STRING);
            rs->m_sbuf = sbuf;
            rs->m_hash = hash;
            putInterned(state, rs);
            return rs;
        }

        static NeonObjString* allocString(NeonState* state, const char* estr, size_t elen, uint32_t hash, bool istaking)
        {
            StringBuffer* sbuf;
            (void)istaking;
            sbuf = new StringBuffer(elen);
            sbuf->append(estr, elen);
            return makeFromStrbuf(state, sbuf, hash);
        }

        static NeonObjString* take(NeonState* state, char* chars, int length)
        {
            uint32_t hash;
            NeonObjString* rs;
            hash = nn_util_hashstring(chars, length);
            rs = findInterned(state, chars, length, hash);
            if(rs == NULL)
            {
                rs = allocString(state, chars, length, hash, true);
            }
            nn_gcmem_freearray(state, sizeof(char), chars, (size_t)length + 1);
            return rs;
        }

        static NeonObjString* copy(NeonState* state, const char* chars, int length)
        {
            uint32_t hash;
            NeonObjString* rs;
            hash = nn_util_hashstring(chars, length);
            rs = findInterned(state, chars, length, hash);
            if(rs != NULL)
            {
                return rs;
            }
            rs = allocString(state, chars, length, hash, false);
            return rs;
        }

        static NeonObjString* take(NeonState* state, char* chars)
        {
            return take(state, chars, strlen(chars));
        }

        static NeonObjString* copy(NeonState* state, const char* chars)
        {
            return copy(state, chars, strlen(chars));
        }

        static NeonObjString* copyObjString(NeonState* state, NeonObjString* os)
        {
            return copy(state, os->data(), os->length());
        }

        // FIXME: does not actually return the range yet
        static NeonObjString* fromRange(NeonState* state, const char* buf, size_t len, size_t begin, size_t end)
        {
            NeonObjString* str;
            (void)begin;
            (void)end;
            if(int(len) <= 0)
            {
                return NeonObjString::copy(state, "", 0);
            }
            str = NeonObjString::copy(state, "", 0);
            str->m_sbuf->append(buf, len);
            return str;
        }


    public:
        uint32_t m_hash;
        StringBuffer* m_sbuf;

    private:
        inline bool destroy()
        {
            if(m_sbuf != nullptr)
            {
                delete m_sbuf;
                m_sbuf = nullptr;
                return true;
            }
            return false;
        }

    public:
        inline ~NeonObjString()
        {
            destroy();
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
            return this->data();
        }

        NeonObjString* substr(size_t start, size_t end, bool likejs) const
        {
            size_t asz;
            size_t len;
            size_t tmp;
            size_t maxlen;
            char* raw;
            (void)likejs;
            maxlen = this->length();
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
            raw = (char*)NeonState::GC::allocate(m_pvm, sizeof(char), asz);
            memset(raw, 0, asz);
            memcpy(raw, this->data() + start, len);
            return NeonObjString::take(m_pvm, raw, len);
        }

};


struct NeonObjUpvalue: public NeonObject
{
    int stackpos;
    NeonValue closed;
    NeonValue location;
    NeonObjUpvalue* next;
};

struct NeonObjModule: public NeonObject
{
    public:
        bool imported;
        NeonHashTable* deftable;
        NeonObjString* name;
        NeonObjString* physicalpath;
        void* preloader;
        void* unloader;
        void* handle;

    public:
        ~NeonObjModule()
        {
            delete this->deftable;
            /*
            free(this->name);
            free(this->physicalpath);
            */
            if(this->unloader != NULL && this->imported)
            {
                ((NeonModLoaderFN)this->unloader)(m_pvm);
            }
            if(this->handle != NULL)
            {
                nn_import_closemodule(this->handle);
            }
        }

};

struct NeonObjFuncScript: public NeonObject
{
    public:
        NeonFuncType type;
        int arity;
        int upvalcount;
        bool isvariadic;
        NeonBlob blob;
        NeonObjString* name;
        NeonObjModule* module;

    public:
        ~NeonObjFuncScript()
        {
            nn_blob_destroy(m_pvm, &this->blob);
        }
};

struct NeonObjFuncClosure: public NeonObject
{
    public:
        int upvalcount;
        NeonObjFuncScript* scriptfunc;
        NeonObjUpvalue** upvalues;

    public:
        ~NeonObjFuncClosure()
        {
            nn_gcmem_freearray(m_pvm, sizeof(NeonObjUpvalue*), this->upvalues, this->upvalcount);
            /*
            // there may be multiple closures that all reference the same function
            // for this reason, we do not free functions when freeing closures
            */
        }
};

struct NeonObjClass: public NeonObject
{
    public:
        /*
        * the constructor, if any. defaults to <empty>, and if not <empty>, expects to be
        * some callable value.
        */
        NeonValue initializer;

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

    private:
        bool destroy()
        {
            delete this->methods;
            delete this->staticmethods;
            delete this->instprops;
            delete this->staticproperties;
            return true;
        }

    public:
        ~NeonObjClass()
        {
            destroy();
        }
};

struct NeonObjInstance: public NeonObject
{
    public:
        /*
        * whether this instance is still "active", i.e., not destroyed, deallocated, etc.
        * in rare circumstances s
        */
        bool active;
        NeonHashTable* properties;
        NeonObjClass* klass;

    public:
        ~NeonObjInstance()
        {
            delete this->properties;
            this->properties = NULL;
            this->active = false;
        }
};

struct NeonObjFuncBound: public NeonObject
{

    NeonValue receiver;
    NeonObjFuncClosure* method;
};

struct NeonObjFuncNative: public NeonObject
{
    NeonFuncType type;
    const char* name;
    NeonNativeFN natfunc;
};

struct NeonObjArray: public NeonObject
{
    public:
        static NeonObjArray* make(NeonState* state, size_t cnt, NeonValue filler)
        {
            size_t i;
            NeonObjArray* list;
            list = (NeonObjArray*)NeonObject::make<NeonObjArray>(state, NEON_OBJTYPE_ARRAY);
            list->varray = new NeonValArray(state);
            if(cnt > 0)
            {
                for(i=0; i<cnt; i++)
                {
                    list->varray->push(filler);
                }
            }
            return list;
        }

        static NeonObjArray* make(NeonState* state)
        {
            return make(state, 0, NeonValue::makeEmpty());
        }

    public:
        NeonValArray* varray;

    public:
        void push(NeonValue value)
        {
            /*this->m_pvm->stackPush(value);*/
            this->varray->push(value);
            /*this->m_pvm->stackPop(); */
        }

        NeonObjArray* copy(int start, int length)
        {
            int i;
            NeonObjArray* newlist;
            newlist = this->m_pvm->gcProtect(NeonObjArray::make(this->m_pvm));
            if(start == -1)
            {
                start = 0;
            }
            if(length == -1)
            {
                length = this->varray->m_count - start;
            }
            for(i = start; i < start + length; i++)
            {
                newlist->push(this->varray->values[i]);
            }
            return newlist;
        }

    public:
        ~NeonObjArray()
        {
            delete this->varray;
        }
};

struct NeonObjRange: public NeonObject
{
    int lower;
    int upper;
    int range;
};

struct NeonObjDict: public NeonObject
{
    public:
        NeonValArray* names;
        NeonHashTable* htab;

    public:
        ~NeonObjDict()
        {
            delete this->names;
            delete this->htab;
        }
};

struct NeonObjFile: public NeonObject
{
    bool isopen;
    bool isstd;
    bool istty;
    int number;
    FILE* handle;
    NeonObjString* mode;
    NeonObjString* path;
};

struct NeonObjSwitch: public NeonObject
{
    int defaultjump;
    int exitjump;
    NeonHashTable* table;
};

struct NeonObjUserdata: public NeonObject
{
    public:
        void* pointer;
        char* name;
        NeonPtrFreeFN ondestroyfn;

    public:
        ~NeonObjUserdata()
        {
            if(this->ondestroyfn)
            {
                this->ondestroyfn(this->pointer);
            }
        }
};


struct NeonPrinter
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
        NeonState* m_pvm = nullptr;
        StringBuffer* m_strbuf = nullptr;
        FILE* m_filehandle = nullptr;

    private:
        void initVars(NeonState* state, PrintMode mode)
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
        static NeonPrinter* makeUndefined(NeonState* state, PrintMode mode)
        {
            NeonPrinter* pd;
            (void)state;
            pd = new NeonPrinter;
            pd->initVars(state, mode);
            return pd;
        }

        static NeonPrinter* makeIO(NeonState* state, FILE* fh, bool shouldclose)
        {
            NeonPrinter* pd;
            pd = makeUndefined(state, PMODE_FILE);
            pd->m_filehandle = fh;
            pd->m_shouldclose = shouldclose;
            return pd;
        }

        static NeonPrinter* makeString(NeonState* state)
        {
            NeonPrinter* pd;
            pd = makeUndefined(state, PMODE_STRING);
            pd->m_strbuf = new StringBuffer(0);
            return pd;
        }

        static void makeStackIO(NeonState* state, NeonPrinter* pd, FILE* fh, bool shouldclose)
        {
            pd->initVars(state, PMODE_FILE);
            pd->m_fromstack = true;
            pd->m_filehandle = fh;
            pd->m_shouldclose = shouldclose;
        }

        static void makeStackString(NeonState* state, NeonPrinter* pd)
        {
            pd->initVars(state, PMODE_STRING);
            pd->m_fromstack = true;
            pd->m_pmode = PMODE_STRING;
            pd->m_strbuf = new StringBuffer(0);
        }

    public:
        NeonPrinter()
        {
        }

        ~NeonPrinter()
        {
            destroy();
        }

        bool destroy()
        {
            NeonState* state;
            (void)state;
            if(m_pmode == PMODE_UNDEFINED)
            {
                return false;
            }
            /*fprintf(stderr, "NeonPrinter::dtor: m_pmode=%d\n", m_pmode);*/
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

        NeonObjString* takeString()
        {
            uint32_t hash;
            NeonState* state;
            NeonObjString* os;
            state = m_pvm;
            hash = nn_util_hashstring(m_strbuf->data, m_strbuf->length);
            os = NeonObjString::makeFromStrbuf(state, m_strbuf, hash);
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
                        this->putformat("\\x%02x", (unsigned char)ch);
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
                    this->putEscapedChar(bch);
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
            #if 0
                size_t wsz;
                size_t needed;
                char* buf;
                va_list copy;
                va_copy(copy, va);
                needed = 1 + vsnprintf(NULL, 0, fmt, copy);
                va_end(copy);
                buf = (char*)NeonState::GC::allocate(m_pvm, sizeof(char), needed + 1);
                if(!buf)
                {
                    return false;
                }
                memset(buf, 0, needed + 1);
                wsz = vsnprintf(buf, needed, fmt, va);
                this->put(buf, wsz);
                free(buf);
            #else
                m_strbuf->appendFormatv(m_strbuf->length, fmt, va);
            #endif
            return true;
        }

        bool vputFormat(const char* fmt, va_list va)
        {
            if(m_pmode == PMODE_STRING)
            {
                return this->vputFormatToString(fmt, va);
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
            b = this->vputFormat(fmt, va);
            va_end(va);
            return b;
        }
};

struct NeonFormatInfo
{
    public:
        /* length of the format string */
        size_t fmtlen = 0;
        /* the actual format string */
        const char* fmtstr = nullptr;
        /* destination writer */
        NeonPrinter* writer = nullptr;
        NeonState* m_pvm = nullptr;

    public:
        NeonFormatInfo()
        {
        }

        NeonFormatInfo(NeonState* state, NeonPrinter* pr, const char* fstr, size_t flen)
        {
            init(state, pr, fstr, flen);
        }

        ~NeonFormatInfo()
        {
        }

        void init(NeonState* state, NeonPrinter* pr, const char* fstr, size_t flen)
        {
            this->m_pvm = state;
            this->fmtstr = fstr;
            this->fmtlen = flen;
            this->writer = pr;
        }

        bool format(int argc, int argbegin, NeonValue* argv)
        {
            int ch;
            int ival;
            int nextch;
            bool failed;
            size_t i;
            size_t argpos;
            NeonValue cval;
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
                            cval = NeonValue::makeEmpty();
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
                                    nn_exceptions_throwException(this->m_pvm, "unknown/invalid format flag '%%c'", nextch);
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
    NeonState* m_pvm;
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
    NeonState* m_pvm;
    NeonAstLexer* scanner;
    NeonAstToken currtoken;
    NeonAstToken prevtoken;
    NeonAstClassCompiler* currentclass;
    NeonObjModule* module;
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
    const char* name;
    NeonRegField* fields;
    NeonRegFunc* functions;
    NeonRegClass* classes;
    NeonModLoaderFN preloader;
    NeonModLoaderFN unloader;
};

struct NeonArguments
{
    public:
        NeonState* m_pvm;
        const char* name;
        NeonValue thisval;
        int argc;
        NeonValue* argv;

    public:
        inline NeonArguments(NeonState* state, const char* n, NeonValue tv, NeonValue* vargv, int vargc)
        {
            this->m_pvm = state;
            this->name = n;
            this->thisval = tv;
            this->argc = vargc;
            this->argv = vargv;
        }

        template<typename... ArgsT>
        NeonValue fail(const char* srcfile, int srcline, const char* fmt, ArgsT&&... args)
        {
            this->m_pvm->stackPop(this->argc);
            this->m_pvm->raiseClass(this->m_pvm->exceptions.argumenterror, srcfile, srcline, fmt, args...);
            return NeonValue::makeBool(false);
        }
};


#include "prot.inc"


NeonHashTable::Entry* NeonHashTable::findEntryByStr(NeonHashTable::Entry* availents, int availcap, NeonValue valkey, const char* kstr, size_t klen, uint32_t hash)
{
    bool havevalhash;
    uint32_t index;
    uint32_t valhash;
    NeonObjString* entoskey;
    NeonHashTable::Entry* entry;
    NeonHashTable::Entry* tombstone;
    NeonState* state;
    state = this->m_pvm;
    (void)valhash;
    (void)havevalhash;
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "looking for key ");
    nn_printer_printvalue(state->debugwriter, key, true, false);
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

NeonProperty* NeonHashTable::getFieldByObjStr(NeonObjString* str)
{
    return this->getFieldByStr(NeonValue::makeEmpty(), str->data(), str->length(), str->m_hash);
}

NeonProperty* NeonHashTable::getField(NeonValue key)
{
    NeonObjString* oskey;
    if(key.isString())
    {
        oskey = key.asString();
        return this->getFieldByStr(key, oskey->data(), oskey->length(), oskey->m_hash);
    }
    return this->getFieldByValue(key);
}

bool NeonHashTable::setCStrType(const char* cstrkey, NeonValue value, NeonFieldType ftype)
{
    NeonObjString* os;
    NeonState* state;
    state = this->m_pvm;
    os = NeonObjString::copy(state, cstrkey);
    return this->setType(NeonValue::fromObject(os), value, ftype, true);
}

NeonObjString* NeonHashTable::findString(const char* chars, size_t length, uint32_t hash)
{
    size_t slen;
    uint32_t index;
    const char* sdata;
    NeonHashTable::Entry* entry;
    NeonObjString* string;
    if(this->m_count == 0)
    {
        return NULL;
    }
    index = hash & (this->capacity - 1);
    while(true)
    {
        entry = &this->entries[index];
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
        index = (index + 1) & (this->capacity - 1);
    }
}

void NeonHashTable::printTo(NeonPrinter* pd, const char* name)
{
    int i;
    NeonHashTable::Entry* entry;
    pd->putformat("<HashTable of %s : {\n", name);
    for(i = 0; i < this->capacity; i++)
    {
        entry = &this->entries[i];
        if(!entry->key.isEmpty())
        {
            nn_printer_printvalue(pd, entry->key, true, true);
            pd->putformat(": ");
            nn_printer_printvalue(pd, entry->value.value, true, true);
            if(i != this->capacity - 1)
            {
                pd->putformat(",\n");
            }
        }
    }
    pd->putformat("}>\n");
}

template<typename... ArgsT>
void NeonState::raiseFatal(const char* format, ArgsT&&... args)
{
    int i;
    int line;
    size_t instruction;
    NeonState::CallFrame* frame;
    NeonObjFuncScript* function;
    /* flush out anything on stdout first */
    fflush(stdout);
    frame = &this->vmstate.framevalues[this->vmstate.framecount - 1];
    function = frame->closure->scriptfunc;
    instruction = frame->inscode - function->blob.instrucs - 1;
    line = function->blob.instrucs[instruction].srcline;
    fprintf(stderr, "RuntimeError: ");
    fprintf(stderr, format, args...);
    fprintf(stderr, " -> %s:%d ", function->module->physicalpath->data(), line);
    fputs("\n", stderr);
    if(this->vmstate.framecount > 1)
    {
        fprintf(stderr, "stacktrace:\n");
        for(i = this->vmstate.framecount - 1; i >= 0; i--)
        {
            frame = &this->vmstate.framevalues[i];
            function = frame->closure->scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->blob.instrucs - 1;
            fprintf(stderr, "    %s:%d -> ", function->module->physicalpath->data(), function->blob.instrucs[instruction].srcline);
            if(function->name == NULL)
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
bool NeonState::raiseClass(NeonObjClass* exklass, const char* srcfile, int srcline, const char* format, ArgsT&&... args)
{
    int length;
    int needed;
    char* message;
    NeonValue stacktrace;
    NeonObjInstance* instance;
    /* TODO: used to be vasprintf. need to check how much to actually allocate! */
    needed = snprintf(NULL, 0, format, args...);
    needed += 1;
    message = (char*)malloc(needed+1);
    length = snprintf(message, needed, format, args...);
    instance = nn_exceptions_makeinstance(this, exklass, srcfile, srcline, NeonObjString::take(this, message, length));
    this->stackPush(NeonValue::fromObject(instance));
    stacktrace = nn_exceptions_getstacktrace(this);
    this->stackPush(stacktrace);
    nn_instance_defproperty(instance, "stacktrace", stacktrace);
    this->stackPop();
    return this->exceptionPropagate();
}

bool NeonState::exceptionHandleUncaught(NeonObjInstance* exception)
{
    int i;
    int cnt;
    int srcline;
    const char* colred;
    const char* colreset;
    const char* colyellow;
    const char* srcfile;
    NeonValue stackitm;
    NeonProperty* field;
    NeonObjString* emsg;
    NeonObjArray* oa;
    colred = nn_util_color(NEON_COLOR_RED);
    colreset = nn_util_color(NEON_COLOR_RESET);
    colyellow = nn_util_color(NEON_COLOR_YELLOW);
    /* at this point, the exception is unhandled; so, print it out. */
    fprintf(stderr, "%sunhandled %s%s", colred, exception->klass->name->data(), colreset);
    srcfile = "none";
    srcline = 0;
    field = exception->properties->getFieldByCStr("srcline");
    if(field != NULL)
    {
        srcline = field->value.asNumber();
    }
    field = exception->properties->getFieldByCStr("srcfile");
    if(field != NULL)
    {
        srcfile = field->value.asString()->data();
    }
    fprintf(stderr, " [from native %s%s:%d%s]", colyellow, srcfile, srcline, colreset);
    
    field = exception->properties->getFieldByCStr("message");
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
    field = exception->properties->getFieldByCStr("stacktrace");
    if(field != NULL)
    {
        fprintf(stderr, "  stacktrace:\n");
        oa = field->value.asArray();
        cnt = oa->varray->m_count;
        i = cnt-1;
        if(cnt > 0)
        {
            while(true)
            {
                stackitm = oa->varray->values[i];
                this->debugwriter->putformat("  ");
                nn_printer_printvalue(this->debugwriter, stackitm, false, true);
                this->debugwriter->putformat("\n");
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

bool NeonState::exceptionPropagate()
{
    int i;
    NeonObjFuncScript* function;
    NeonState::ExceptionFrame* handler;
    NeonObjInstance* exception;
    exception = this->stackPeek(0).asInstance();
    while(this->vmstate.framecount > 0)
    {
        this->vmstate.currentframe = &this->vmstate.framevalues[this->vmstate.framecount - 1];
        for(i = this->vmstate.currentframe->handlercount; i > 0; i--)
        {
            handler = &this->vmstate.currentframe->handlers[i - 1];
            function = this->vmstate.currentframe->closure->scriptfunc;
            if(handler->address != 0 && nn_util_isinstanceof(exception->klass, handler->klass))
            {
                this->vmstate.currentframe->inscode = &function->blob.instrucs[handler->address];
                return true;
            }
            else if(handler->finallyaddress != 0)
            {
                /* continue propagating once the 'finally' block completes */
                this->stackPush(NeonValue::makeBool(true));
                this->vmstate.currentframe->inscode = &function->blob.instrucs[handler->finallyaddress];
                return true;
            }
        }
        this->vmstate.framecount--;
    }
    return exceptionHandleUncaught(exception);
}

static NEON_ALWAYSINLINE  NeonObjType nn_value_objtype(NeonValue v)
{
    return v.asObject()->m_objtype;
}

static NEON_ALWAYSINLINE  bool nn_value_asbool(NeonValue v)
{
    return ((v).m_valunion.boolean);
}


static NEON_ALWAYSINLINE  NeonObjFuncNative* nn_value_asfuncnative(NeonValue v)
{
    return ((NeonObjFuncNative*)v.asObject());
}

static NEON_ALWAYSINLINE  NeonObjFuncScript* nn_value_asfuncscript(NeonValue v)
{
    return ((NeonObjFuncScript*)v.asObject());
}

static NEON_ALWAYSINLINE  NeonObjFuncClosure* nn_value_asfuncclosure(NeonValue v)
{
    return ((NeonObjFuncClosure*)v.asObject());
}

static NEON_ALWAYSINLINE  NeonObjClass* nn_value_asclass(NeonValue v)
{
    return ((NeonObjClass*)v.asObject());
}

static NEON_ALWAYSINLINE  NeonObjFuncBound* nn_value_asfuncbound(NeonValue v)
{
    return ((NeonObjFuncBound*)v.asObject());
}

static NEON_ALWAYSINLINE  NeonObjSwitch* nn_value_asswitch(NeonValue v)
{
    return ((NeonObjSwitch*)v.asObject());
}

static NEON_ALWAYSINLINE  NeonObjUserdata* nn_value_asuserdata(NeonValue v)
{
    return ((NeonObjUserdata*)v.asObject());
}

static NEON_ALWAYSINLINE  NeonObjModule* nn_value_asmodule(NeonValue v)
{
    return ((NeonObjModule*)v.asObject());
}

static NEON_ALWAYSINLINE  NeonObjDict* nn_value_asdict(NeonValue v)
{
    return ((NeonObjDict*)v.asObject());
}

static NEON_ALWAYSINLINE  NeonObjFile* nn_value_asfile(NeonValue v)
{
    return ((NeonObjFile*)v.asObject());
}

static NEON_ALWAYSINLINE  NeonObjRange* nn_value_asrange(NeonValue v)
{
    return ((NeonObjRange*)v.asObject());
}



#if defined(_WIN32)
    #include <fcntl.h>
    #include <io.h>
#endif


#if defined(__STRICT_ANSI__)
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

const char* nn_util_color(NeonColor tc)
{
    #if !defined(NEON_CFG_FORCEDISABLECOLOR)
        bool istty;
        int fdstdout;
        int fdstderr;
        fdstdout = fileno(stdout);
        fdstderr = fileno(stderr);
        istty = (osfn_isatty(fdstderr) && osfn_isatty(fdstdout));
        if(istty)
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

char* nn_util_strndup(NeonState* state, const char* src, size_t len)
{
    char* buf;
    (void)state;
    buf = (char*)malloc(sizeof(char) * (len+1));
    if(buf == NULL)
    {
        return NULL;
    }
    memset(buf, 0, len+1);
    memcpy(buf, src, len);
    return buf;
}

char* nn_util_strdup(NeonState* state, const char* src)
{
    return nn_util_strndup(state, src, strlen(src));
}

char* nn_util_readhandle(NeonState* state, FILE* hnd, size_t* dlen)
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
    buf = (char*)NeonState::GC::allocate(state, sizeof(char), toldlen + 1);
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

char* nn_util_readfile(NeonState* state, const char* filename, size_t* dlen)
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
    b = nn_util_readhandle(state, fh, dlen);
    fclose(fh);
    return b;
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

char* nn_util_utf8encode(NeonState* state, unsigned int code)
{
    int count;
    char* chars;
    count = nn_util_utf8numbytes((int)code);
    if(count > 0)
    {
        chars = (char*)NeonState::GC::allocate(state, sizeof(char), (size_t)count + 1);
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

char* nn_util_utf8strstr(const char* haystack, size_t hslen, const char* needle, size_t nlen)
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
            haystack = nn_util_utf8codepoint(maybematch, &throwawaycodepoint);
        }
    }
    /* no match */
    return NULL;
}

char* nn_util_strtoupperinplace(char* str, size_t length)
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

char* nn_util_strtolowerinplace(char* str, size_t length)
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

#define NEON_APIDEBUG(state, ...) \
    if((NEON_UNLIKELY((state)->conf.enableapidebug))) \
    { \
        nn_state_apidebug(state, __FUNCTION__, __VA_ARGS__); \
    }


#define NEON_ASTDEBUG(state, ...) \
    if((NEON_UNLIKELY((state)->conf.enableastdebug))) \
    { \
        nn_state_astdebug(state, __FUNCTION__, __VA_ARGS__); \
    }


static NEON_ALWAYSINLINE void nn_state_apidebugv(NeonState* state, const char* funcname, const char* format, va_list va)
{
    (void)state;
    fprintf(stderr, "API CALL: to '%s': ", funcname);
    vfprintf(stderr, format, va);
    fprintf(stderr, "\n");
}

static NEON_ALWAYSINLINE void nn_state_apidebug(NeonState* state, const char* funcname, const char* format, ...)
{
    va_list va;
    va_start(va, format);
    nn_state_apidebugv(state, funcname, format, va);
    va_end(va);
}

static NEON_ALWAYSINLINE void nn_state_astdebugv(NeonState* state, const char* funcname, const char* format, va_list va)
{
    (void)state;
    fprintf(stderr, "AST CALL: to '%s': ", funcname);
    vfprintf(stderr, format, va);
    fprintf(stderr, "\n");
}

static NEON_ALWAYSINLINE void nn_state_astdebug(NeonState* state, const char* funcname, const char* format, ...)
{
    va_list va;
    va_start(va, format);
    nn_state_astdebugv(state, funcname, format, va);
    va_end(va);
}

void nn_gcmem_markobject(NeonState* state, NeonObject* object)
{
    if(object == NULL)
    {
        return;
    }
    if(object->m_mark == state->markvalue)
    {
        return;
    }
    #if defined(DEBUG_GC) && DEBUG_GC
    state->debugwriter->putformat("GC: marking object at <%p> ", (void*)object);
    nn_printer_printvalue(state->debugwriter, NeonValue::fromObject(object), false);
    state->debugwriter->putformat("\n");
    #endif
    object->m_mark = state->markvalue;
    if(state->gcstate.graycapacity < state->gcstate.graycount + 1)
    {
        state->gcstate.graycapacity = GROW_CAPACITY(state->gcstate.graycapacity);
        state->gcstate.graystack = (NeonObject**)realloc(state->gcstate.graystack, sizeof(NeonObject*) * state->gcstate.graycapacity);
        if(state->gcstate.graystack == NULL)
        {
            fflush(stdout);
            fprintf(stderr, "GC encountered an error");
            abort();
        }
    }
    state->gcstate.graystack[state->gcstate.graycount++] = object;
}

void nn_gcmem_markvalue(NeonState* state, NeonValue value)
{
    if(value.isObject())
    {
        nn_gcmem_markobject(state, value.asObject());
    }
}

void nn_gcmem_blackenobject(NeonState* state, NeonObject* object)
{
    #if defined(DEBUG_GC) && DEBUG_GC
    state->debugwriter->putformat("GC: blacken object at <%p> ", (void*)object);
    nn_printer_printvalue(state->debugwriter, NeonValue::fromObject(object), false);
    state->debugwriter->putformat("\n");
    #endif
    switch(object->m_objtype)
    {
        case NEON_OBJTYPE_MODULE:
            {
                NeonObjModule* module;
                module = (NeonObjModule*)object;
                NeonHashTable::mark(state, module->deftable);
            }
            break;
        case NEON_OBJTYPE_SWITCH:
            {
                NeonObjSwitch* sw;
                sw = (NeonObjSwitch*)object;
                NeonHashTable::mark(state, sw->table);
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                NeonObjFile* file;
                file = (NeonObjFile*)object;
                nn_file_mark(state, file);
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                NeonObjDict* dict;
                dict = (NeonObjDict*)object;
                dict->names->gcMark();
                NeonHashTable::mark(state, dict->htab);
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                NeonObjArray* list;
                list = (NeonObjArray*)object;
                list->varray->gcMark();
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
            {
                NeonObjFuncBound* bound;
                bound = (NeonObjFuncBound*)object;
                nn_gcmem_markvalue(state, bound->receiver);
                nn_gcmem_markobject(state, (NeonObject*)bound->method);
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                NeonObjClass* klass;
                klass = (NeonObjClass*)object;
                nn_gcmem_markobject(state, (NeonObject*)klass->name);
                NeonHashTable::mark(state, klass->methods);
                NeonHashTable::mark(state, klass->staticmethods);
                NeonHashTable::mark(state, klass->staticproperties);
                nn_gcmem_markvalue(state, klass->initializer);
                if(klass->superclass != NULL)
                {
                    nn_gcmem_markobject(state, (NeonObject*)klass->superclass);
                }
            }
            break;
        case NEON_OBJTYPE_FUNCCLOSURE:
            {
                int i;
                NeonObjFuncClosure* closure;
                closure = (NeonObjFuncClosure*)object;
                nn_gcmem_markobject(state, (NeonObject*)closure->scriptfunc);
                for(i = 0; i < closure->upvalcount; i++)
                {
                    nn_gcmem_markobject(state, (NeonObject*)closure->upvalues[i]);
                }
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                NeonObjFuncScript* function;
                function = (NeonObjFuncScript*)object;
                nn_gcmem_markobject(state, (NeonObject*)function->name);
                nn_gcmem_markobject(state, (NeonObject*)function->module);
                function->blob.constants->gcMark();
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                NeonObjInstance* instance;
                instance = (NeonObjInstance*)object;
                nn_instance_mark(state, instance);
            }
            break;
        case NEON_OBJTYPE_UPVALUE:
            {
                nn_gcmem_markvalue(state, ((NeonObjUpvalue*)object)->closed);
            }
            break;
        case NEON_OBJTYPE_RANGE:
        case NEON_OBJTYPE_FUNCNATIVE:
        case NEON_OBJTYPE_USERDATA:
        case NEON_OBJTYPE_STRING:
            break;
    }
}

void nn_gcmem_destroyobject(NeonState* state, NeonObject* object)
{
    (void)state;
    #if defined(DEBUG_GC) && DEBUG_GC
    state->debugwriter->putformat("GC: freeing at <%p> of type %d\n", (void*)object, object->m_objtype);
    #endif
    if(object->m_isstale)
    {
        return;
    }
    switch(object->m_objtype)
    {
        case NEON_OBJTYPE_MODULE:
            {
                NeonObjModule* module;
                module = (NeonObjModule*)object;
                delete module;
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                NeonObjFile* file;
                file = (NeonObjFile*)object;
                delete file;
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                NeonObjDict* dict;
                dict = (NeonObjDict*)object;
                delete dict;
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                NeonObjArray* list;
                list = (NeonObjArray*)object;
                delete list;
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
            {
                NeonObjFuncBound* bound;
                bound = (NeonObjFuncBound*)object;
                /*
                // a closure may be bound to multiple instances
                // for this reason, we do not free closures when freeing bound methods
                */
                delete bound;
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                NeonObjClass* klass;
                klass = (NeonObjClass*)object;
                delete klass;
            }
            break;
        case NEON_OBJTYPE_FUNCCLOSURE:
            {
                NeonObjFuncClosure* closure;
                closure = (NeonObjFuncClosure*)object;
                delete closure;
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                NeonObjFuncScript* function;
                function = (NeonObjFuncScript*)object;
                delete function;
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                NeonObjInstance* instance;
                instance = (NeonObjInstance*)object;
                delete instance;
            }
            break;
        case NEON_OBJTYPE_FUNCNATIVE:
            {
                NeonObjFuncNative* native;
                native = (NeonObjFuncNative*)object;
                delete native;
            }
            break;
        case NEON_OBJTYPE_UPVALUE:
            {
                NeonObjUpvalue* upv;
                upv = (NeonObjUpvalue*)object;
                delete upv;
            }
            break;
        case NEON_OBJTYPE_RANGE:
            {
                NeonObjRange* rng;
                rng = (NeonObjRange*)object;
                delete rng;
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                NeonObjString* string;
                string = (NeonObjString*)object;
                delete string;
            }
            break;
        case NEON_OBJTYPE_SWITCH:
            {
                NeonObjSwitch* sw;
                sw = (NeonObjSwitch*)object;
                delete sw->table;
                delete sw;
            }
            break;
        case NEON_OBJTYPE_USERDATA:
            {
                NeonObjUserdata* uptr;
                uptr = (NeonObjUserdata*)object;
                delete uptr;
            }
            break;
        default:
            break;
    }
}

void nn_gcmem_markroots(NeonState* state)
{
    int i;
    int j;
    NeonValue* slot;
    NeonObjUpvalue* upvalue;
    NeonState::ExceptionFrame* handler;
    for(slot = state->vmstate.stackvalues; slot < &state->vmstate.stackvalues[state->vmstate.stackidx]; slot++)
    {
        nn_gcmem_markvalue(state, *slot);
    }
    for(i = 0; i < (int)state->vmstate.framecount; i++)
    {
        nn_gcmem_markobject(state, (NeonObject*)state->vmstate.framevalues[i].closure);
        for(j = 0; j < (int)state->vmstate.framevalues[i].handlercount; j++)
        {
            handler = &state->vmstate.framevalues[i].handlers[j];
            nn_gcmem_markobject(state, (NeonObject*)handler->klass);
        }
    }
    for(upvalue = state->vmstate.openupvalues; upvalue != NULL; upvalue = upvalue->next)
    {
        nn_gcmem_markobject(state, (NeonObject*)upvalue);
    }
    NeonHashTable::mark(state, state->globals);
    NeonHashTable::mark(state, state->modules);
    nn_gcmem_markobject(state, (NeonObject*)state->exceptions.stdexception);
    nn_gcmem_markcompilerroots(state);
}

void nn_gcmem_tracerefs(NeonState* state)
{
    NeonObject* object;
    while(state->gcstate.graycount > 0)
    {
        object = state->gcstate.graystack[--state->gcstate.graycount];
        nn_gcmem_blackenobject(state, object);
    }
}

void nn_gcmem_sweep(NeonState* state)
{
    NeonObject* object;
    NeonObject* previous;
    NeonObject* unreached;
    previous = NULL;
    object = state->vmstate.linkedobjects;
    while(object != NULL)
    {
        if(object->m_mark == state->markvalue)
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
                state->vmstate.linkedobjects = object;
            }
            nn_gcmem_destroyobject(state, unreached);
        }
    }
}

void nn_gcmem_destroylinkedobjects(NeonState* state)
{
    NeonObject* next;
    NeonObject* object;
    object = state->vmstate.linkedobjects;
    while(object != NULL)
    {
        next = object->m_nextobj;
        nn_gcmem_destroyobject(state, object);
        object = next;
    }
    free(state->gcstate.graystack);
    state->gcstate.graystack = NULL;
}

void NeonState::GC::collectGarbage(NeonState* state)
{
    size_t before;
    (void)before;
    #if defined(DEBUG_GC) && DEBUG_GC
    state->debugwriter->putformat("GC: gc begins\n");
    before = state->gcstate.bytesallocated;
    #endif
    /*
    //  REMOVE THE NEXT LINE TO DISABLE NESTED collectGarbage() POSSIBILITY!
    */
    #if 1
    state->gcstate.nextgc = state->gcstate.bytesallocated;
    #endif
    nn_gcmem_markroots(state);
    nn_gcmem_tracerefs(state);
    NeonHashTable::removeMarked(state, state->strings);
    NeonHashTable::removeMarked(state, state->modules);
    nn_gcmem_sweep(state);
    state->gcstate.nextgc = state->gcstate.bytesallocated * NEON_CFG_GCHEAPGROWTHFACTOR;
    state->markvalue = !state->markvalue;
    #if defined(DEBUG_GC) && DEBUG_GC
    state->debugwriter->putformat("GC: gc ends\n");
    state->debugwriter->putformat("GC: collected %zu bytes (from %zu to %zu), next at %zu\n", before - state->gcstate.bytesallocated, before, state->gcstate.bytesallocated, state->gcstate.nextgc);
    #endif
}


void nn_dbg_disasmblob(NeonPrinter* pd, NeonBlob* blob, const char* name)
{
    int offset;
    pd->putformat("== compiled '%s' [[\n", name);
    for(offset = 0; offset < blob->count;)
    {
        offset = nn_dbg_printinstructionat(pd, blob, offset);
    }
    pd->putformat("]]\n");
}

void nn_dbg_printinstrname(NeonPrinter* pd, const char* name)
{
    pd->putformat("%s%-16s%s ", nn_util_color(NEON_COLOR_RED), name, nn_util_color(NEON_COLOR_RESET));
}

int nn_dbg_printsimpleinstr(NeonPrinter* pd, const char* name, int offset)
{
    nn_dbg_printinstrname(pd, name);
    pd->putformat("\n");
    return offset + 1;
}

int nn_dbg_printconstinstr(NeonPrinter* pd, const char* name, NeonBlob* blob, int offset)
{
    uint16_t constant;
    constant = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pd, name);
    pd->putformat("%8d ", constant);
    nn_printer_printvalue(pd, blob->constants->values[constant], true, false);
    pd->putformat("\n");
    return offset + 3;
}

int nn_dbg_printpropertyinstr(NeonPrinter* pd, const char* name, NeonBlob* blob, int offset)
{
    const char* proptn;
    uint16_t constant;
    constant = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pd, name);
    pd->putformat("%8d ", constant);
    nn_printer_printvalue(pd, blob->constants->values[constant], true, false);
    proptn = "";
    if(blob->instrucs[offset + 3].code == 1)
    {
        proptn = "static";
    }
    pd->putformat(" (%s)", proptn);
    pd->putformat("\n");
    return offset + 4;
}

int nn_dbg_printshortinstr(NeonPrinter* pd, const char* name, NeonBlob* blob, int offset)
{
    uint16_t slot;
    slot = (blob->instrucs[offset + 1].code << 8) | blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pd, name);
    pd->putformat("%8d\n", slot);
    return offset + 3;
}

int nn_dbg_printbyteinstr(NeonPrinter* pd, const char* name, NeonBlob* blob, int offset)
{
    uint8_t slot;
    slot = blob->instrucs[offset + 1].code;
    nn_dbg_printinstrname(pd, name);
    pd->putformat("%8d\n", slot);
    return offset + 2;
}

int nn_dbg_printjumpinstr(NeonPrinter* pd, const char* name, int sign, NeonBlob* blob, int offset)
{
    uint16_t jump;
    jump = (uint16_t)(blob->instrucs[offset + 1].code << 8);
    jump |= blob->instrucs[offset + 2].code;
    nn_dbg_printinstrname(pd, name);
    pd->putformat("%8d -> %d\n", offset, offset + 3 + sign * jump);
    return offset + 3;
}

int nn_dbg_printtryinstr(NeonPrinter* pd, const char* name, NeonBlob* blob, int offset)
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
    nn_dbg_printinstrname(pd, name);
    pd->putformat("%8d -> %d, %d\n", type, address, finally);
    return offset + 7;
}

int nn_dbg_printinvokeinstr(NeonPrinter* pd, const char* name, NeonBlob* blob, int offset)
{
    uint16_t constant;
    uint8_t argcount;
    constant = (uint16_t)(blob->instrucs[offset + 1].code << 8);
    constant |= blob->instrucs[offset + 2].code;
    argcount = blob->instrucs[offset + 3].code;
    nn_dbg_printinstrname(pd, name);
    pd->putformat("(%d args) %8d ", argcount, constant);
    nn_printer_printvalue(pd, blob->constants->values[constant], true, false);
    pd->putformat("\n");
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
        default:
            break;
    }
    return "<?unknown?>";
}

int nn_dbg_printclosureinstr(NeonPrinter* pd, const char* name, NeonBlob* blob, int offset)
{
    int j;
    int islocal;
    uint16_t index;
    uint16_t constant;
    const char* locn;
    NeonObjFuncScript* function;
    offset++;
    constant = blob->instrucs[offset++].code << 8;
    constant |= blob->instrucs[offset++].code;
    pd->putformat("%-16s %8d ", name, constant);
    nn_printer_printvalue(pd, blob->constants->values[constant], true, false);
    pd->putformat("\n");
    function = nn_value_asfuncscript(blob->constants->values[constant]);
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
        pd->putformat("%04d      |                     %s %d\n", offset - 3, locn, (int)index);
    }
    return offset;
}

int nn_dbg_printinstructionat(NeonPrinter* pd, NeonBlob* blob, int offset)
{
    uint8_t instruction;
    const char* opname;
    pd->putformat("%08d ", offset);
    if(offset > 0 && blob->instrucs[offset].srcline == blob->instrucs[offset - 1].srcline)
    {
        pd->putformat("       | ");
    }
    else
    {
        pd->putformat("%8d ", blob->instrucs[offset].srcline);
    }
    instruction = blob->instrucs[offset].code;
    opname = nn_dbg_op2str(instruction);
    switch(instruction)
    {
        case NEON_OP_JUMPIFFALSE:
            return nn_dbg_printjumpinstr(pd, opname, 1, blob, offset);
        case NEON_OP_JUMPNOW:
            return nn_dbg_printjumpinstr(pd, opname, 1, blob, offset);
        case NEON_OP_EXTRY:
            return nn_dbg_printtryinstr(pd, opname, blob, offset);
        case NEON_OP_LOOP:
            return nn_dbg_printjumpinstr(pd, opname, -1, blob, offset);
        case NEON_OP_GLOBALDEFINE:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case NEON_OP_GLOBALGET:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case NEON_OP_GLOBALSET:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case NEON_OP_LOCALGET:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case NEON_OP_LOCALSET:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case NEON_OP_FUNCARGGET:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case NEON_OP_FUNCARGSET:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case NEON_OP_PROPERTYGET:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case NEON_OP_PROPERTYGETSELF:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case NEON_OP_PROPERTYSET:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case NEON_OP_UPVALUEGET:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case NEON_OP_UPVALUESET:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case NEON_OP_EXPOPTRY:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_EXPUBLISHTRY:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PUSHCONSTANT:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case NEON_OP_EQUAL:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMGREATER:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMLESSTHAN:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PUSHEMPTY:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PUSHNULL:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PUSHTRUE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PUSHFALSE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMADD:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMSUBTRACT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMMULTIPLY:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMDIVIDE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMFLOORDIVIDE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMMODULO:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMPOW:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMNEGATE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMNOT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMBITNOT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMAND:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMOR:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMBITXOR:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMSHIFTLEFT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PRIMSHIFTRIGHT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_PUSHONE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_IMPORTIMPORT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_TYPEOF:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_ECHO:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_STRINGIFY:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_EXTHROW:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_POPONE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_UPVALUECLOSE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_DUPONE:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_ASSERT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_POPN:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
            /* non-user objects... */
        case NEON_OP_SWITCH:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
            /* data container manipulators */
        case NEON_OP_MAKERANGE:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case NEON_OP_MAKEARRAY:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case NEON_OP_MAKEDICT:
            return nn_dbg_printshortinstr(pd, opname, blob, offset);
        case NEON_OP_INDEXGET:
            return nn_dbg_printbyteinstr(pd, opname, blob, offset);
        case NEON_OP_INDEXGETRANGED:
            return nn_dbg_printbyteinstr(pd, opname, blob, offset);
        case NEON_OP_INDEXSET:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_MAKECLOSURE:
            return nn_dbg_printclosureinstr(pd, opname, blob, offset);
        case NEON_OP_CALLFUNCTION:
            return nn_dbg_printbyteinstr(pd, opname, blob, offset);
        case NEON_OP_CALLMETHOD:
            return nn_dbg_printinvokeinstr(pd, opname, blob, offset);
        case NEON_OP_CLASSINVOKETHIS:
            return nn_dbg_printinvokeinstr(pd, opname, blob, offset);
        case NEON_OP_RETURN:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_MAKECLASS:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case NEON_OP_MAKEMETHOD:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case NEON_OP_CLASSPROPERTYDEFINE:
            return nn_dbg_printpropertyinstr(pd, opname, blob, offset);
        case NEON_OP_CLASSGETSUPER:
            return nn_dbg_printconstinstr(pd, opname, blob, offset);
        case NEON_OP_CLASSINHERIT:
            return nn_dbg_printsimpleinstr(pd, opname, offset);
        case NEON_OP_CLASSINVOKESUPER:
            return nn_dbg_printinvokeinstr(pd, opname, blob, offset);
        case NEON_OP_CLASSINVOKESUPERSELF:
            return nn_dbg_printbyteinstr(pd, opname, blob, offset);
        default:
            {
                pd->putformat("unknown opcode %d\n", instruction);
            }
            break;
    }
    return offset + 1;
}

void nn_blob_init(NeonState* state, NeonBlob* blob)
{
    blob->count = 0;
    blob->capacity = 0;
    blob->instrucs = NULL;
    blob->constants = new NeonValArray(state);
    blob->argdefvals = new NeonValArray(state);
}

void nn_blob_push(NeonState* state, NeonBlob* blob, NeonInstruction ins)
{
    int oldcapacity;
    if(blob->capacity < blob->count + 1)
    {
        oldcapacity = blob->capacity;
        blob->capacity = GROW_CAPACITY(oldcapacity);
        blob->instrucs = (NeonInstruction*)nn_gcmem_growarray(state, sizeof(NeonInstruction), blob->instrucs, oldcapacity, blob->capacity);
    }
    blob->instrucs[blob->count] = ins;
    blob->count++;
}

void nn_blob_destroy(NeonState* state, NeonBlob* blob)
{
    if(blob->instrucs != NULL)
    {
        nn_gcmem_freearray(state, sizeof(NeonInstruction), blob->instrucs, blob->capacity);
    }
    delete blob->constants;
    delete blob->argdefvals;
}

int nn_blob_pushconst(NeonState* state, NeonBlob* blob, NeonValue value)
{
    (void)state;
    blob->constants->push(value);
    return blob->constants->m_count - 1;
}


int nn_blob_pushargdefval(NeonState* state, NeonBlob* blob, NeonValue value)
{
    (void)state;
    blob->argdefvals->push(value);
    return blob->argdefvals->m_count - 1;
}

void nn_printer_printfunction(NeonPrinter* pd, NeonObjFuncScript* func)
{
    if(func->name == NULL)
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

void nn_printer_printarray(NeonPrinter* pd, NeonObjArray* list)
{
    size_t i;
    size_t vsz;
    bool isrecur;
    NeonValue val;
    NeonObjArray* subarr;
    vsz = list->varray->m_count;
    pd->putformat("[");
    for(i = 0; i < vsz; i++)
    {
        isrecur = false;
        val = list->varray->values[i];
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

void nn_printer_printdict(NeonPrinter* pd, NeonObjDict* dict)
{
    size_t i;
    size_t dsz;
    bool keyisrecur;
    bool valisrecur;
    NeonValue val;
    NeonObjDict* subdict;
    NeonProperty* field;
    dsz = dict->names->m_count;
    pd->putformat("{");
    for(i = 0; i < dsz; i++)
    {
        valisrecur = false;
        keyisrecur = false;
        val = dict->names->values[i];
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
        field = dict->htab->getField(dict->names->values[i]);
        if(field != NULL)
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

void nn_printer_printfile(NeonPrinter* pd, NeonObjFile* file)
{
    pd->putformat("<file at %s in mode %s>", file->path->data(), file->mode->data());
}

void nn_printer_printinstance(NeonPrinter* pd, NeonObjInstance* instance, bool invmethod)
{
    (void)invmethod;
    #if 0
    int arity;
    NeonPrinter subw;
    NeonValue resv;
    NeonValue thisval;
    NeonProperty* field;
    NeonState* state;
    NeonObjString* os;
    NeonObjArray* args;
    state = pd->m_pvm;
    if(invmethod)
    {
        field = instance->klass->methods->getFieldByCStr("toString");
        if(field != NULL)
        {
            args = NeonObjArray::make(state);
            thisval = NeonValue::fromObject(instance);
            arity = nn_nestcall_prepare(state, field->value, thisval, args);
            fprintf(stderr, "arity = %d\n", arity);
            state->stackPop();
            state->stackPush(thisval);
            if(nn_nestcall_callfunction(state, field->value, thisval, args, &resv))
            {
                NeonPrinter::makeStackString(state, &subw);
                nn_printer_printvalue(&subw, resv, false, false);
                os = subw.takeString();
                pd->put(os->data(), os->length());
                //state->stackPop();
                return;
            }
        }
    }
    #endif
    pd->putformat("<instance of %s at %p>", instance->klass->name->data(), (void*)instance);
}

void nn_printer_printobject(NeonPrinter* pd, NeonValue value, bool fixstring, bool invmethod)
{
    NeonObject* obj;
    obj = value.asObject();
    switch(obj->m_objtype)
    {
        case NEON_OBJTYPE_SWITCH:
            {
                pd->put("<switch>");
            }
            break;
        case NEON_OBJTYPE_USERDATA:
            {
                pd->putformat("<userdata %s>", nn_value_asuserdata(value)->name);
            }
            break;
        case NEON_OBJTYPE_RANGE:
            {
                NeonObjRange* range;
                range = nn_value_asrange(value);
                pd->putformat("<range %d .. %d>", range->lower, range->upper);
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                nn_printer_printfile(pd, nn_value_asfile(value));
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                nn_printer_printdict(pd, nn_value_asdict(value));
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                nn_printer_printarray(pd, value.asArray());
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
            {
                NeonObjFuncBound* bn;
                bn = nn_value_asfuncbound(value);
                nn_printer_printfunction(pd, bn->method->scriptfunc);
            }
            break;
        case NEON_OBJTYPE_MODULE:
            {
                NeonObjModule* mod;
                mod = nn_value_asmodule(value);
                pd->putformat("<module '%s' at '%s'>", mod->name->data(), mod->physicalpath->data());
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                NeonObjClass* klass;
                klass = nn_value_asclass(value);
                pd->putformat("<class %s at %p>", klass->name->data(), (void*)klass);
            }
            break;
        case NEON_OBJTYPE_FUNCCLOSURE:
            {
                NeonObjFuncClosure* cls;
                cls = nn_value_asfuncclosure(value);
                nn_printer_printfunction(pd, cls->scriptfunc);
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                NeonObjFuncScript* fn;
                fn = nn_value_asfuncscript(value);
                nn_printer_printfunction(pd, fn);
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                /* @TODO: support the toString() override */
                NeonObjInstance* instance;
                instance = value.asInstance();
                nn_printer_printinstance(pd, instance, invmethod);
            }
            break;
        case NEON_OBJTYPE_FUNCNATIVE:
            {
                NeonObjFuncNative* native;
                native = nn_value_asfuncnative(value);
                pd->putformat("<function %s(native) at %p>", native->name, (void*)native);
            }
            break;
        case NEON_OBJTYPE_UPVALUE:
            {
                pd->putformat("<upvalue>");
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                NeonObjString* string;
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

void nn_printer_printvalue(NeonPrinter* pd, NeonValue value, bool fixstring, bool invmethod)
{
    switch(value.type())
    {
        case NeonValue::Type::T_EMPTY:
            {
                pd->put("<empty>");
            }
            break;
        case NeonValue::Type::T_NULL:
            {
                pd->put("null");
            }
            break;
        case NeonValue::Type::T_BOOL:
            {
                pd->put(nn_value_asbool(value) ? "true" : "false");
            }
            break;
        case NeonValue::Type::T_NUMBER:
            {
                pd->putformat("%.16g", value.asNumber());
            }
            break;
        case NeonValue::Type::T_OBJECT:
            {
                nn_printer_printobject(pd, value, fixstring, invmethod);
            }
            break;
        default:
            break;
    }
}

NeonObjString* nn_value_tostring(NeonState* state, NeonValue value)
{
    NeonPrinter pd;
    NeonObjString* s;
    NeonPrinter::makeStackString(state, &pd);
    nn_printer_printvalue(&pd, value, false, true);
    s = pd.takeString();
    return s;
}

const char* nn_value_objecttypename(NeonObject* object)
{
    switch(object->m_objtype)
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
            return ((NeonObjInstance*)object)->klass->name->data();
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

const char* nn_value_typename(NeonValue value)
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

bool nn_value_compobject(NeonState* state, NeonValue a, NeonValue b)
{
    size_t i;
    NeonObjType ta;
    NeonObjType tb;
    NeonObject* oa;
    NeonObject* ob;
    NeonObjString* stra;
    NeonObjString* strb;
    NeonObjArray* arra;
    NeonObjArray* arrb;
    oa = a.asObject();
    ob = b.asObject();
    ta = oa->m_objtype;
    tb = ob->m_objtype;
    if(ta == tb)
    {
        if(ta == NEON_OBJTYPE_STRING)
        {
            stra = (NeonObjString*)oa;
            strb = (NeonObjString*)ob;
            if(stra->length() == strb->length())
            {
                if(memcmp(stra->data(), strb->data(), stra->length()) == 0)
                {
                    return true;
                }
                return false;
            }
        }
        if(ta == NEON_OBJTYPE_ARRAY)
        {
            arra = (NeonObjArray*)oa;
            arrb = (NeonObjArray*)ob;
            if(arra->varray->m_count == arrb->varray->m_count)
            {
                for(i=0; i<(size_t)arra->varray->m_count; i++)
                {
                    if(!nn_value_compare(state, arra->varray->values[i], arrb->varray->values[i]))
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

bool nn_value_compare_actual(NeonState* state, NeonValue a, NeonValue b)
{
    if(a.type() != b.type())
    {
        return false;
    }
    switch(a.type())
    {
        case NeonValue::Type::T_NULL:
        case NeonValue::Type::T_EMPTY:
            {
                return true;
            }
            break;
        case NeonValue::Type::T_BOOL:
            {
                return nn_value_asbool(a) == nn_value_asbool(b);
            }
            break;
        case NeonValue::Type::T_NUMBER:
            {
                return (a.asNumber() == b.asNumber());
            }
            break;
        case NeonValue::Type::T_OBJECT:
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


bool nn_value_compare(NeonState* state, NeonValue a, NeonValue b)
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
    NeonDoubleHashUnion bits;
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

uint32_t nn_object_hashobject(NeonObject* object)
{
    switch(object->m_objtype)
    {
        case NEON_OBJTYPE_CLASS:
            {
                /* Classes just use their name. */
                return ((NeonObjClass*)object)->name->m_hash;
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
                NeonObjFuncScript* fn;
                fn = (NeonObjFuncScript*)object;
                return nn_util_hashdouble(fn->arity) ^ nn_util_hashdouble(fn->blob.count);
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                return ((NeonObjString*)object)->m_hash;
            }
            break;
        default:
            break;
    }
    return 0;
}

uint32_t nn_value_hashvalue(NeonValue value)
{
    switch(value.type())
    {
        case NeonValue::Type::T_BOOL:
            return nn_value_asbool(value) ? 3 : 5;
        case NeonValue::Type::T_NULL:
            return 7;
        case NeonValue::Type::T_NUMBER:
            return nn_util_hashdouble(value.asNumber());
        case NeonValue::Type::T_OBJECT:
            return nn_object_hashobject(value.asObject());
        default:
            /* NeonValue::Type::T_EMPTY */
            break;
    }
    return 0;
}


/**
 * returns the greater of the two values.
 * this function encapsulates the object hierarchy
 */
NeonValue nn_value_findgreater(NeonValue a, NeonValue b)
{
    NeonObjString* osa;
    NeonObjString* osb;    
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
            if(nn_value_asfuncscript(a)->arity >= nn_value_asfuncscript(b)->arity)
            {
                return a;
            }
            return b;
        }
        else if(a.isFuncClosure() && b.isFuncClosure())
        {
            if(nn_value_asfuncclosure(a)->scriptfunc->arity >= nn_value_asfuncclosure(b)->scriptfunc->arity)
            {
                return a;
            }
            return b;
        }
        else if(a.isRange() && b.isRange())
        {
            if(nn_value_asrange(a)->lower >= nn_value_asrange(b)->lower)
            {
                return a;
            }
            return b;
        }
        else if(a.isClass() && b.isClass())
        {
            if(nn_value_asclass(a)->methods->m_count >= nn_value_asclass(b)->methods->m_count)
            {
                return a;
            }
            return b;
        }
        else if(a.isArray() && b.isArray())
        {
            if(a.asArray()->varray->m_count >= b.asArray()->varray->m_count)
            {
                return a;
            }
            return b;
        }
        else if(a.isDict() && b.isDict())
        {
            if(nn_value_asdict(a)->names->m_count >= nn_value_asdict(b)->names->m_count)
            {
                return a;
            }
            return b;
        }
        else if(a.isFile() && b.isFile())
        {
            if(strcmp(nn_value_asfile(a)->path->data(), nn_value_asfile(b)->path->data()) >= 0)
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
void nn_value_sortvalues(NeonState* state, NeonValue* values, int count)
{
    int i;
    int j;
    NeonValue temp;
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
                    nn_value_sortvalues(state, values[i].asArray()->varray->values, values[i].asArray()->varray->m_count);
                }

                if(values[j].isArray())
                {
                    nn_value_sortvalues(state, values[j].asArray()->varray->values, values[j].asArray()->varray->m_count);
                }
            }
        }
    }
}

NeonValue nn_value_copyvalue(NeonState* state, NeonValue value)
{
    if(value.isObject())
    {
        switch(value.asObject()->m_objtype)
        {
            case NEON_OBJTYPE_STRING:
                {
                    NeonObjString* string;
                    string = value.asString();
                    return NeonValue::fromObject(NeonObjString::copy(state, string->data(), string->length()));
                }
                break;
            case NEON_OBJTYPE_ARRAY:
            {
                int i;
                NeonObjArray* list;
                NeonObjArray* newlist;
                list = value.asArray();
                newlist = NeonObjArray::make(state);
                state->stackPush(NeonValue::fromObject(newlist));
                for(i = 0; i < list->varray->m_count; i++)
                {
                    newlist->varray->push(list->varray->values[i]);
                }
                state->stackPop();
                return NeonValue::fromObject(newlist);
            }
            /*
            case NEON_OBJTYPE_DICT:
                {
                    NeonObjDict *dict;
                    NeonObjDict *newdict;
                    dict = nn_value_asdict(value);
                    newdict = nn_object_makedict(state);
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

NeonObjUserdata* nn_object_makeuserdata(NeonState* state, void* pointer, const char* name)
{
    NeonObjUserdata* ptr;
    ptr = (NeonObjUserdata*)NeonObject::make<NeonObjUserdata>(state, NEON_OBJTYPE_USERDATA);
    ptr->pointer = pointer;
    ptr->name = nn_util_strdup(state, name);
    ptr->ondestroyfn = NULL;
    return ptr;
}

NeonObjModule* nn_module_make(NeonState* state, const char* name, const char* file, bool imported)
{
    NeonObjModule* module;
    module = (NeonObjModule*)NeonObject::make<NeonObjModule>(state, NEON_OBJTYPE_MODULE);
    module->deftable = new NeonHashTable(state);
    module->name = NeonObjString::copy(state, name);
    module->physicalpath = NeonObjString::copy(state, file);
    module->unloader = NULL;
    module->preloader = NULL;
    module->handle = NULL;
    module->imported = imported;
    return module;
}

void nn_module_setfilefield(NeonState* state, NeonObjModule* module)
{
    return;
    module->deftable->setCStr("__file__", NeonValue::fromObject(NeonObjString::copyObjString(state, module->physicalpath)));
}

NeonObjSwitch* nn_object_makeswitch(NeonState* state)
{
    NeonObjSwitch* sw;
    sw = (NeonObjSwitch*)NeonObject::make<NeonObjSwitch>(state, NEON_OBJTYPE_SWITCH);
    sw->table = new NeonHashTable(state);
    sw->defaultjump = -1;
    sw->exitjump = -1;
    return sw;
}


NeonObjRange* nn_object_makerange(NeonState* state, int lower, int upper)
{
    NeonObjRange* range;
    range = (NeonObjRange*)NeonObject::make<NeonObjRange>(state, NEON_OBJTYPE_RANGE);
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

NeonObjDict* nn_object_makedict(NeonState* state)
{
    NeonObjDict* dict;
    dict = (NeonObjDict*)NeonObject::make<NeonObjDict>(state, NEON_OBJTYPE_DICT);
    dict->names = new NeonValArray(state);
    dict->htab = new NeonHashTable(state);
    return dict;
}

NeonObjFile* nn_object_makefile(NeonState* state, FILE* handle, bool isstd, const char* path, const char* mode)
{
    NeonObjFile* file;
    file = (NeonObjFile*)NeonObject::make<NeonObjFile>(state, NEON_OBJTYPE_FILE);
    file->isopen = false;
    file->mode = NeonObjString::copy(state, mode);
    file->path = NeonObjString::copy(state, path);
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

void nn_file_destroy(NeonState* state, NeonObjFile* file)
{
    (void)state;
    nn_fileobject_close(file);
}

void nn_file_mark(NeonState* state, NeonObjFile* file)
{
    nn_gcmem_markobject(state, (NeonObject*)file->mode);
    nn_gcmem_markobject(state, (NeonObject*)file->path);
}

NeonObjFuncBound* nn_object_makefuncbound(NeonState* state, NeonValue receiver, NeonObjFuncClosure* method)
{
    NeonObjFuncBound* bound;
    bound = (NeonObjFuncBound*)NeonObject::make<NeonObjFuncBound>(state, NEON_OBJTYPE_FUNCBOUND);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

NeonObjClass* nn_object_makeclass(NeonState* state, NeonObjString* name)
{
    NeonObjClass* klass;
    klass = (NeonObjClass*)NeonObject::make<NeonObjClass>(state, NEON_OBJTYPE_CLASS);
    klass->name = name;
    klass->instprops = new NeonHashTable(state);
    klass->staticproperties = new NeonHashTable(state);
    klass->methods = new NeonHashTable(state);
    klass->staticmethods = new NeonHashTable(state);
    klass->initializer = NeonValue::makeEmpty();
    klass->superclass = NULL;
    return klass;
}


void nn_class_inheritfrom(NeonObjClass* subclass, NeonObjClass* superclass)
{
    NeonHashTable::addAll(superclass->instprops, subclass->instprops);
    NeonHashTable::addAll(superclass->methods, subclass->methods);
    subclass->superclass = superclass;
}

void nn_class_defproperty(NeonObjClass* klass, const char* cstrname, NeonValue val)
{
    klass->instprops->setCStr(cstrname, val);
}

void nn_class_defcallablefield(NeonState* state, NeonObjClass* klass, const char* cstrname, NeonNativeFN function)
{
    NeonObjString* oname;
    NeonObjFuncNative* ofn;
    oname = NeonObjString::copy(state, cstrname);
    ofn = nn_object_makefuncnative(state, function, cstrname);
    klass->instprops->setType(NeonValue::fromObject(oname), NeonValue::fromObject(ofn), NEON_PROPTYPE_FUNCTION, true);
}

void nn_class_setstaticpropertycstr(NeonObjClass* klass, const char* cstrname, NeonValue val)
{
    klass->staticproperties->setCStr(cstrname, val);
}

void nn_class_setstaticproperty(NeonObjClass* klass, NeonObjString* name, NeonValue val)
{
    nn_class_setstaticpropertycstr(klass, name->data(), val);
}

void nn_class_defnativeconstructor(NeonState* state, NeonObjClass* klass, NeonNativeFN function)
{
    const char* cname;
    NeonObjFuncNative* ofn;
    cname = "constructor";
    ofn = nn_object_makefuncnative(state, function, cname);
    klass->initializer = NeonValue::fromObject(ofn);
}

void nn_class_defmethod(NeonState* state, NeonObjClass* klass, const char* name, NeonValue val)
{
    (void)state;
    klass->methods->setCStr(name, val);
}

void nn_class_defnativemethod(NeonState* state, NeonObjClass* klass, const char* name, NeonNativeFN function)
{
    NeonObjFuncNative* ofn;
    ofn = nn_object_makefuncnative(state, function, name);
    nn_class_defmethod(state, klass, name, NeonValue::fromObject(ofn));
}

void nn_class_defstaticnativemethod(NeonState* state, NeonObjClass* klass, const char* name, NeonNativeFN function)
{
    NeonObjFuncNative* ofn;
    ofn = nn_object_makefuncnative(state, function, name);
    klass->staticmethods->setCStr(name, NeonValue::fromObject(ofn));
}

NeonProperty* nn_class_getmethodfield(NeonObjClass* klass, NeonObjString* name)
{
    NeonProperty* field;
    field = klass->methods->getField(NeonValue::fromObject(name));
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

NeonProperty* nn_class_getpropertyfield(NeonObjClass* klass, NeonObjString* name)
{
    NeonProperty* field;
    field = klass->instprops->getField(NeonValue::fromObject(name));
    return field;
}

NeonProperty* nn_class_getstaticproperty(NeonObjClass* klass, NeonObjString* name)
{
    return klass->staticproperties->getFieldByObjStr(name);
}

NeonProperty* nn_class_getstaticmethodfield(NeonObjClass* klass, NeonObjString* name)
{
    NeonProperty* field;
    field = klass->staticmethods->getField(NeonValue::fromObject(name));
    return field;
}

NeonObjInstance* nn_object_makeinstance(NeonState* state, NeonObjClass* klass)
{
    NeonObjInstance* instance;
    instance = (NeonObjInstance*)NeonObject::make<NeonObjInstance>(state, NEON_OBJTYPE_INSTANCE);
    /* gc fix */
    state->stackPush(NeonValue::fromObject(instance));
    instance->active = true;
    instance->klass = klass;
    instance->properties = new NeonHashTable(state);
    if(klass->instprops->m_count > 0)
    {
        NeonHashTable::copy(klass->instprops, instance->properties);
    }
    /* gc fix */
    state->stackPop();
    return instance;
}

void nn_instance_mark(NeonState* state, NeonObjInstance* instance)
{
    if(instance->active == false)
    {
        state->warn("trying to mark inactive instance <%p>!", instance);
        return;
    }
    nn_gcmem_markobject(state, (NeonObject*)instance->klass);
    NeonHashTable::mark(state, instance->properties);
}


void nn_instance_defproperty(NeonObjInstance* instance, const char *cstrname, NeonValue val)
{
    instance->properties->setCStr(cstrname, val);
}

NeonObjFuncScript* nn_object_makefuncscript(NeonState* state, NeonObjModule* module, NeonFuncType type)
{
    NeonObjFuncScript* function;
    function = (NeonObjFuncScript*)NeonObject::make<NeonObjFuncScript>(state, NEON_OBJTYPE_FUNCSCRIPT);
    function->arity = 0;
    function->upvalcount = 0;
    function->isvariadic = false;
    function->name = NULL;
    function->type = type;
    function->module = module;
    nn_blob_init(state, &function->blob);
    return function;
}

NeonObjFuncNative* nn_object_makefuncnative(NeonState* state, NeonNativeFN function, const char* name)
{
    NeonObjFuncNative* native;
    native = (NeonObjFuncNative*)NeonObject::make<NeonObjFuncNative>(state, NEON_OBJTYPE_FUNCNATIVE);
    native->natfunc = function;
    native->name = name;
    native->type = NEON_FUNCTYPE_FUNCTION;
    return native;
}

NeonObjFuncClosure* nn_object_makefuncclosure(NeonState* state, NeonObjFuncScript* function)
{
    int i;
    NeonObjUpvalue** upvals;
    NeonObjFuncClosure* closure;
    upvals = (NeonObjUpvalue**)NeonState::GC::allocate(state, sizeof(NeonObjUpvalue*), function->upvalcount);
    for(i = 0; i < function->upvalcount; i++)
    {
        upvals[i] = NULL;
    }
    closure = (NeonObjFuncClosure*)NeonObject::make<NeonObjFuncClosure>(state, NEON_OBJTYPE_FUNCCLOSURE);
    closure->scriptfunc = function;
    closure->upvalues = upvals;
    closure->upvalcount = function->upvalcount;
    return closure;
}


NeonObjUpvalue* nn_object_makeupvalue(NeonState* state, NeonValue* slot, int stackpos)
{
    NeonObjUpvalue* upvalue;
    upvalue = (NeonObjUpvalue*)NeonObject::make<NeonObjUpvalue>(state, NEON_OBJTYPE_UPVALUE);
    upvalue->closed = NeonValue::makeNull();
    upvalue->location = *slot;
    upvalue->next = NULL;
    upvalue->stackpos = stackpos;
    return upvalue;
}

static const char* g_strthis = "this";
static const char* g_strsuper = "super";

NeonAstLexer* nn_astlex_init(NeonState* state, const char* source)
{
    NeonAstLexer* lex;
    NEON_ASTDEBUG(state, "");
    lex = (NeonAstLexer*)NeonState::GC::allocate(state, sizeof(NeonAstLexer), 1);
    lex->m_pvm = state;
    lex->sourceptr = source;
    lex->start = source;
    lex->line = 1;
    lex->tplstringcount = -1;
    return lex;
}

void nn_astlex_destroy(NeonState* state, NeonAstLexer* lex)
{
    NEON_ASTDEBUG(state, "");
    NeonState::GC::release(state, lex, sizeof(NeonAstLexer));
}

bool nn_astlex_isatend(NeonAstLexer* lex)
{
    return *lex->sourceptr == '\0';
}

NeonAstToken nn_astlex_maketoken(NeonAstLexer* lex, NeonAstTokType type)
{
    NeonAstToken t;
    t.isglobal = false;
    t.type = type;
    t.start = lex->start;
    t.length = (int)(lex->sourceptr - lex->start);
    t.line = lex->line;
    return t;
}

NeonAstToken nn_astlex_errortoken(NeonAstLexer* lex, const char* fmt, ...)
{
    int length;
    char* buf;
    va_list va;
    NeonAstToken t;
    va_start(va, fmt);
    buf = (char*)NeonState::GC::allocate(lex->m_pvm, sizeof(char), 1024);
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
        case NEON_ASTTOK_KWCASE: return "NEON_ASTTOK_KWCASE";
        case NEON_ASTTOK_KWWHILE: return "NEON_ASTTOK_KWWHILE";
        case NEON_ASTTOK_LITERAL: return "NEON_ASTTOK_LITERAL";
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

char nn_astlex_advance(NeonAstLexer* lex)
{
    lex->sourceptr++;
    if(lex->sourceptr[-1] == '\n')
    {
        lex->line++;
    }
    return lex->sourceptr[-1];
}

bool nn_astlex_match(NeonAstLexer* lex, char expected)
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

char nn_astlex_peekcurr(NeonAstLexer* lex)
{
    return *lex->sourceptr;
}

char nn_astlex_peekprev(NeonAstLexer* lex)
{
    return lex->sourceptr[-1];
}

char nn_astlex_peeknext(NeonAstLexer* lex)
{
    if(nn_astlex_isatend(lex))
    {
        return '\0';
    }
    return lex->sourceptr[1];
}

NeonAstToken nn_astlex_skipblockcomments(NeonAstLexer* lex)
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
    //nn_astlex_advance(lex);
    #endif
    return nn_astlex_maketoken(lex, NEON_ASTTOK_UNDEFINED);
}

NeonAstToken nn_astlex_skipspace(NeonAstLexer* lex)
{
    char c;
    NeonAstToken result;
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
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_UNDEFINED);
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
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_UNDEFINED);
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
    return nn_astlex_maketoken(lex, NEON_ASTTOK_UNDEFINED);
}

NeonAstToken nn_astlex_scanstring(NeonAstLexer* lex, char quote, bool withtemplate)
{
    NeonAstToken tkn;
    NEON_ASTDEBUG(lex->m_pvm, "quote=[%c] withtemplate=%d", quote, withtemplate);
    while(nn_astlex_peekcurr(lex) != quote && !nn_astlex_isatend(lex))
    {
        if(withtemplate)
        {
            /* interpolation started */
            if(nn_astlex_peekcurr(lex) == '$' && nn_astlex_peeknext(lex) == '{' && nn_astlex_peekprev(lex) != '\\')
            {
                if(lex->tplstringcount - 1 < NEON_CFG_ASTMAXSTRTPLDEPTH)
                {
                    lex->tplstringcount++;
                    lex->tplstringbuffer[lex->tplstringcount] = (int)quote;
                    lex->sourceptr++;
                    tkn = nn_astlex_maketoken(lex, NEON_ASTTOK_INTERPOLATION);
                    lex->sourceptr++;
                    return tkn;
                }
                return nn_astlex_errortoken(lex, "maximum interpolation nesting of %d exceeded by %d", NEON_CFG_ASTMAXSTRTPLDEPTH,
                    NEON_CFG_ASTMAXSTRTPLDEPTH - lex->tplstringcount + 1);
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
    return nn_astlex_maketoken(lex, NEON_ASTTOK_LITERAL);
}

NeonAstToken nn_astlex_scannumber(NeonAstLexer* lex)
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
            return nn_astlex_maketoken(lex, NEON_ASTTOK_LITNUMBIN);
        }
        else if(nn_astlex_match(lex, 'c'))
        {
            while(nn_astutil_isoctal(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
            return nn_astlex_maketoken(lex, NEON_ASTTOK_LITNUMOCT);
        }
        else if(nn_astlex_match(lex, 'x'))
        {
            while(nn_astutil_ishexadecimal(nn_astlex_peekcurr(lex)))
            {
                nn_astlex_advance(lex);
            }
            return nn_astlex_maketoken(lex, NEON_ASTTOK_LITNUMHEX);
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
    return nn_astlex_maketoken(lex, NEON_ASTTOK_LITNUMREG);
}

NeonAstTokType nn_astlex_getidenttype(NeonAstLexer* lex)
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
        { "false", NEON_ASTTOK_KWFALSE },
        { "finally", NEON_ASTTOK_KWFINALLY },
        { "foreach", NEON_ASTTOK_KWFOREACH },
        { "if", NEON_ASTTOK_KWIF },
        { "import", NEON_ASTTOK_KWIMPORT },
        { "in", NEON_ASTTOK_KWIN },
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
        { "while", NEON_ASTTOK_KWWHILE },
        { NULL, (NeonAstTokType)0 }
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
                return (NeonAstTokType)keywords[i].tokid;
            }
        }
    }
    return NEON_ASTTOK_IDENTNORMAL;
}

NeonAstToken nn_astlex_scanident(NeonAstLexer* lex, bool isdollar)
{
    int cur;
    NeonAstToken tok;
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
    tok = nn_astlex_maketoken(lex, nn_astlex_getidenttype(lex));
    tok.isglobal = isdollar;
    return tok;
}

NeonAstToken nn_astlex_scandecorator(NeonAstLexer* lex)
{
    while(nn_astutil_isalpha(nn_astlex_peekcurr(lex)) || nn_astutil_isdigit(nn_astlex_peekcurr(lex)))
    {
        nn_astlex_advance(lex);
    }
    return nn_astlex_maketoken(lex, NEON_ASTTOK_DECORATOR);
}

NeonAstToken nn_astlex_scantoken(NeonAstLexer* lex)
{
    char c;
    bool isdollar;
    NeonAstToken tk;
    NeonAstToken token;
    tk = nn_astlex_skipspace(lex);
    if(tk.type != NEON_ASTTOK_UNDEFINED)
    {
        return tk;
    }
    lex->start = lex->sourceptr;
    if(nn_astlex_isatend(lex))
    {
        return nn_astlex_maketoken(lex, NEON_ASTTOK_EOF);
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
                return nn_astlex_maketoken(lex, NEON_ASTTOK_PARENOPEN);
            }
            break;
        case ')':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_PARENCLOSE);
            }
            break;
        case '[':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_BRACKETOPEN);
            }
            break;
        case ']':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_BRACKETCLOSE);
            }
            break;
        case '{':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_BRACEOPEN);
            }
            break;
        case '}':
            {
                if(lex->tplstringcount > -1)
                {
                    token = nn_astlex_scanstring(lex, (char)lex->tplstringbuffer[lex->tplstringcount], true);
                    lex->tplstringcount--;
                    return token;
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_BRACECLOSE);
            }
            break;
        case ';':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_SEMICOLON);
            }
            break;
        case '\\':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_BACKSLASH);
            }
            break;
        case ':':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_COLON);
            }
            break;
        case ',':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_COMMA);
            }
            break;
        case '@':
            {
                if(!nn_astutil_isalpha(nn_astlex_peekcurr(lex)))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_AT);
                }
                return nn_astlex_scandecorator(lex);
            }
            break;
        case '!':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_NOTEQUAL);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_EXCLMARK);

            }
            break;
        case '.':
            {
                if(nn_astlex_match(lex, '.'))
                {
                    if(nn_astlex_match(lex, '.'))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_TRIPLEDOT);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_DOUBLEDOT);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_DOT);
            }
            break;
        case '+':
        {
            if(nn_astlex_match(lex, '+'))
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_INCREMENT);
            }
            if(nn_astlex_match(lex, '='))
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_PLUSASSIGN);
            }
            else
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_PLUS);
            }
        }
        break;
        case '-':
            {
                if(nn_astlex_match(lex, '-'))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_DECREMENT);
                }
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_MINUSASSIGN);
                }
                else
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_MINUS);
                }
            }
            break;
        case '*':
            {
                if(nn_astlex_match(lex, '*'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_POWASSIGN);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_POWEROF);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_MULTASSIGN);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_MULTIPLY);
                }
            }
            break;
        case '/':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_DIVASSIGN);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_DIVIDE);
            }
            break;
        case '=':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_EQUAL);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_ASSIGN);
            }        
            break;
        case '<':
            {
                if(nn_astlex_match(lex, '<'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_LEFTSHIFTASSIGN);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_LEFTSHIFT);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_LESSEQUAL);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_LESSTHAN);

                }
            }
            break;
        case '>':
            {
                if(nn_astlex_match(lex, '>'))
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_RIGHTSHIFTASSIGN);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_RIGHTSHIFT);
                }
                else
                {
                    if(nn_astlex_match(lex, '='))
                    {
                        return nn_astlex_maketoken(lex, NEON_ASTTOK_GREATER_EQ);
                    }
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_GREATERTHAN);
                }
            }
            break;
        case '%':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_PERCENT_EQ);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_MODULO);
            }
            break;
        case '&':
            {
                if(nn_astlex_match(lex, '&'))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_KWAND);
                }
                else if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_AMP_EQ);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_AMP);
            }
            break;
        case '|':
            {
                if(nn_astlex_match(lex, '|'))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_KWOR);
                }
                else if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_BAR_EQ);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_BAR);
            }
            break;
        case '~':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_TILDE_EQ);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_TILDE);
            }
            break;
        case '^':
            {
                if(nn_astlex_match(lex, '='))
                {
                    return nn_astlex_maketoken(lex, NEON_ASTTOK_XOR_EQ);
                }
                return nn_astlex_maketoken(lex, NEON_ASTTOK_XOR);
            }
            break;
        case '\n':
            {
                return nn_astlex_maketoken(lex, NEON_ASTTOK_NEWLINE);
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
                return nn_astlex_maketoken(lex, NEON_ASTTOK_QUESTION);
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

NeonAstParser* nn_astparser_make(NeonState* state, NeonAstLexer* lexer, NeonObjModule* module, bool keeplast)
{
    NeonAstParser* parser;
    NEON_ASTDEBUG(state, "");
    parser = (NeonAstParser*)NeonState::GC::allocate(state, sizeof(NeonAstParser), 1);
    parser->m_pvm = state;
    parser->scanner = lexer;
    parser->haderror = false;
    parser->panicmode = false;
    parser->blockcount = 0;
    parser->replcanecho = false;
    parser->isreturning = false;
    parser->istrying = false;
    parser->compcontext = NEON_COMPCONTEXT_NONE;
    parser->innermostloopstart = -1;
    parser->innermostloopscopedepth = 0;
    parser->currentclass = NULL;
    parser->module = module;
    parser->keeplastvalue = keeplast;
    parser->lastwasstatement = false;
    parser->infunction = false;
    parser->currentfile = parser->module->physicalpath->data();
    return parser;
}

void nn_astparser_destroy(NeonState* state, NeonAstParser* parser)
{
    NeonState::GC::release(state, parser, sizeof(NeonAstParser));
}

NeonBlob* nn_astparser_currentblob(NeonAstParser* prs)
{
    return &prs->m_pvm->compiler->targetfunc->blob;
}

bool nn_astparser_raiseerroratv(NeonAstParser* prs, NeonAstToken* t, const char* message, va_list args)
{
    fflush(stdout);
    /*
    // do not cascade error
    // suppress error if already in panic mode
    */
    if(prs->panicmode)
    {
        return false;
    }
    prs->panicmode = true;
    fprintf(stderr, "SyntaxError");
    if(t->type == NEON_ASTTOK_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if(t->type == NEON_ASTTOK_ERROR)
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
    fprintf(stderr, "  %s:%d\n", prs->module->physicalpath->data(), t->line);
    prs->haderror = true;
    return false;
}

bool nn_astparser_raiseerror(NeonAstParser* prs, const char* message, ...)
{
    va_list args;
    va_start(args, message);
    nn_astparser_raiseerroratv(prs, &prs->prevtoken, message, args);
    va_end(args);
    return false;
}

bool nn_astparser_raiseerroratcurrent(NeonAstParser* prs, const char* message, ...)
{
    va_list args;
    va_start(args, message);
    nn_astparser_raiseerroratv(prs, &prs->currtoken, message, args);
    va_end(args);
    return false;
}

void nn_astparser_advance(NeonAstParser* prs)
{
    prs->prevtoken = prs->currtoken;
    while(true)
    {
        prs->currtoken = nn_astlex_scantoken(prs->scanner);
        if(prs->currtoken.type != NEON_ASTTOK_ERROR)
        {
            break;
        }
        nn_astparser_raiseerroratcurrent(prs, prs->currtoken.start);
    }
}

bool nn_astparser_consume(NeonAstParser* prs, NeonAstTokType t, const char* message)
{
    if(prs->currtoken.type == t)
    {
        nn_astparser_advance(prs);
        return true;
    }
    return nn_astparser_raiseerroratcurrent(prs, message);
}

void nn_astparser_consumeor(NeonAstParser* prs, const char* message, const NeonAstTokType* ts, int count)
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

bool nn_astparser_checknumber(NeonAstParser* prs)
{
    NeonAstTokType t;
    t = prs->prevtoken.type;
    if(t == NEON_ASTTOK_LITNUMREG || t == NEON_ASTTOK_LITNUMOCT || t == NEON_ASTTOK_LITNUMBIN || t == NEON_ASTTOK_LITNUMHEX)
    {
        return true;
    }
    return false;
}

bool nn_astparser_check(NeonAstParser* prs, NeonAstTokType t)
{
    return prs->currtoken.type == t;
}

bool nn_astparser_match(NeonAstParser* prs, NeonAstTokType t)
{
    if(!nn_astparser_check(prs, t))
    {
        return false;
    }
    nn_astparser_advance(prs);
    return true;
}

void nn_astparser_runparser(NeonAstParser* parser)
{
    nn_astparser_advance(parser);
    nn_astparser_ignorewhitespace(parser);
    while(!nn_astparser_match(parser, NEON_ASTTOK_EOF))
    {
        nn_astparser_parsedeclaration(parser);
    }
}

void nn_astparser_parsedeclaration(NeonAstParser* prs)
{
    nn_astparser_ignorewhitespace(prs);
    if(nn_astparser_match(prs, NEON_ASTTOK_KWCLASS))
    {
        nn_astparser_parseclassdeclaration(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWFUNCTION))
    {
        nn_astparser_parsefuncdecl(prs);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_KWVAR))
    {
        nn_astparser_parsevardecl(prs, false);
    }
    else if(nn_astparser_match(prs, NEON_ASTTOK_BRACEOPEN))
    {
        if(!nn_astparser_check(prs, NEON_ASTTOK_NEWLINE) && prs->m_pvm->compiler->scopedepth == 0)
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

void nn_astparser_parsestmt(NeonAstParser* prs)
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

void nn_astparser_consumestmtend(NeonAstParser* prs)
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

void nn_astparser_ignorewhitespace(NeonAstParser* prs)
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

int nn_astparser_getcodeargscount(const NeonInstruction* bytecode, const NeonValue* constants, int ip)
{
    int constant;
    NeonOpCode code;
    NeonObjFuncScript* fn;
    code = (NeonOpCode)bytecode[ip].code;
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
        //case NEON_OP_FUNCOPTARG:
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
                fn = nn_value_asfuncscript(constants[constant]);
                /* There is two byte for the constant, then three for each up value. */
                return 2 + (fn->upvalcount * 3);
            }
            break;
        default:
            break;
    }
    return 0;
}

void nn_astemit_emit(NeonAstParser* prs, uint8_t byte, int line, bool isop)
{
    NeonInstruction ins;
    ins.code = byte;
    ins.srcline = line;
    ins.isop = isop;
    nn_blob_push(prs->m_pvm, nn_astparser_currentblob(prs), ins);
}

void nn_astemit_patchat(NeonAstParser* prs, size_t idx, uint8_t byte)
{
    nn_astparser_currentblob(prs)->instrucs[idx].code = byte;
}

void nn_astemit_emitinstruc(NeonAstParser* prs, uint8_t byte)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, true);
}

void nn_astemit_emit1byte(NeonAstParser* prs, uint8_t byte)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
}

void nn_astemit_emit1short(NeonAstParser* prs, uint16_t byte)
{
    nn_astemit_emit(prs, (byte >> 8) & 0xff, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte & 0xff, prs->prevtoken.line, false);
}

void nn_astemit_emit2byte(NeonAstParser* prs, uint8_t byte, uint8_t byte2)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte2, prs->prevtoken.line, false);
}

void nn_astemit_emitbyteandshort(NeonAstParser* prs, uint8_t byte, uint16_t byte2)
{
    nn_astemit_emit(prs, byte, prs->prevtoken.line, false);
    nn_astemit_emit(prs, (byte2 >> 8) & 0xff, prs->prevtoken.line, false);
    nn_astemit_emit(prs, byte2 & 0xff, prs->prevtoken.line, false);
}

void nn_astemit_emitloop(NeonAstParser* prs, int loopstart)
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

void nn_astemit_emitreturn(NeonAstParser* prs)
{
    if(prs->istrying)
    {
        nn_astemit_emitinstruc(prs, NEON_OP_EXPOPTRY);
    }
    if(prs->m_pvm->compiler->type == NEON_FUNCTYPE_INITIALIZER)
    {
        nn_astemit_emitbyteandshort(prs, NEON_OP_LOCALGET, 0);
    }
    else
    {
        if(!prs->keeplastvalue || prs->lastwasstatement)
        {
            if(prs->m_pvm->compiler->fromimport)
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

int nn_astparser_pushconst(NeonAstParser* prs, NeonValue value)
{
    int constant;
    constant = nn_blob_pushconst(prs->m_pvm, nn_astparser_currentblob(prs), value);
    if(constant >= UINT16_MAX)
    {
        nn_astparser_raiseerror(prs, "too many constants in current scope");
        return 0;
    }
    return constant;
}

void nn_astemit_emitconst(NeonAstParser* prs, NeonValue value)
{
    int constant;
    constant = nn_astparser_pushconst(prs, value);
    nn_astemit_emitbyteandshort(prs, NEON_OP_PUSHCONSTANT, (uint16_t)constant);
}

int nn_astemit_emitjump(NeonAstParser* prs, uint8_t instruction)
{
    nn_astemit_emitinstruc(prs, instruction);
    /* placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->count - 2;
}

int nn_astemit_emitswitch(NeonAstParser* prs)
{
    nn_astemit_emitinstruc(prs, NEON_OP_SWITCH);
    /* placeholders */
    nn_astemit_emit1byte(prs, 0xff);
    nn_astemit_emit1byte(prs, 0xff);
    return nn_astparser_currentblob(prs)->count - 2;
}

int nn_astemit_emittry(NeonAstParser* prs)
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

void nn_astemit_patchswitch(NeonAstParser* prs, int offset, int constant)
{
    nn_astemit_patchat(prs, offset, (constant >> 8) & 0xff);
    nn_astemit_patchat(prs, offset + 1, constant & 0xff);
}

void nn_astemit_patchtry(NeonAstParser* prs, int offset, int type, int address, int finally)
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

void nn_astemit_patchjump(NeonAstParser* prs, int offset)
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

void nn_astfunccompiler_init(NeonAstParser* prs, NeonAstFuncCompiler* compiler, NeonFuncType type, bool isanon)
{
    bool candeclthis;
    NeonPrinter wtmp;
    NeonAstLocal* local;
    NeonObjString* fname;
    compiler->enclosing = prs->m_pvm->compiler;
    compiler->targetfunc = NULL;
    compiler->type = type;
    compiler->localcount = 0;
    compiler->scopedepth = 0;
    compiler->handlercount = 0;
    compiler->fromimport = false;
    compiler->targetfunc = nn_object_makefuncscript(prs->m_pvm, prs->module, type);
    prs->m_pvm->compiler = compiler;
    if(type != NEON_FUNCTYPE_SCRIPT)
    {
        prs->m_pvm->stackPush(NeonValue::fromObject(compiler->targetfunc));
        if(isanon)
        {
            NeonPrinter::makeStackString(prs->m_pvm, &wtmp);
            wtmp.putformat("anonymous@[%s:%d]", prs->currentfile, prs->prevtoken.line);
            fname = wtmp.takeString();
        }
        else
        {
            fname = NeonObjString::copy(prs->m_pvm, prs->prevtoken.start, prs->prevtoken.length);
        }
        prs->m_pvm->compiler->targetfunc->name = fname;
        prs->m_pvm->stackPop();
    }
    /* claiming slot zero for use in class methods */
    local = &prs->m_pvm->compiler->locals[0];
    prs->m_pvm->compiler->localcount++;
    local->depth = 0;
    local->iscaptured = false;
    candeclthis = (
        (type != NEON_FUNCTYPE_FUNCTION) &&
        (prs->compcontext == NEON_COMPCONTEXT_CLASS)
    );
    if(candeclthis || (/*(type == NEON_FUNCTYPE_ANONYMOUS) &&*/ (prs->compcontext != NEON_COMPCONTEXT_CLASS)))
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

int nn_astparser_makeidentconst(NeonAstParser* prs, NeonAstToken* name)
{
    int rawlen;
    const char* rawstr;
    NeonObjString* str;
    rawstr = name->start;
    rawlen = name->length;
    if(name->isglobal)
    {
        rawstr++;
        rawlen--;
    }
    str = NeonObjString::copy(prs->m_pvm, rawstr, rawlen);
    return nn_astparser_pushconst(prs, NeonValue::fromObject(str));
}

bool nn_astparser_identsequal(NeonAstToken* a, NeonAstToken* b)
{
    return a->length == b->length && memcmp(a->start, b->start, a->length) == 0;
}

int nn_astfunccompiler_resolvelocal(NeonAstParser* prs, NeonAstFuncCompiler* compiler, NeonAstToken* name)
{
    int i;
    NeonAstLocal* local;
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

int nn_astfunccompiler_addupvalue(NeonAstParser* prs, NeonAstFuncCompiler* compiler, uint16_t index, bool islocal)
{
    int i;
    int upcnt;
    NeonAstUpvalue* upvalue;
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

int nn_astfunccompiler_resolveupvalue(NeonAstParser* prs, NeonAstFuncCompiler* compiler, NeonAstToken* name)
{
    int local;
    int upvalue;
    if(compiler->enclosing == NULL)
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

int nn_astparser_addlocal(NeonAstParser* prs, NeonAstToken name)
{
    NeonAstLocal* local;
    if(prs->m_pvm->compiler->localcount == NEON_CFG_ASTMAXLOCALS)
    {
        /* we've reached maximum local variables per scope */
        nn_astparser_raiseerror(prs, "too many local variables in scope");
        return -1;
    }
    local = &prs->m_pvm->compiler->locals[prs->m_pvm->compiler->localcount++];
    local->name = name;
    local->depth = -1;
    local->iscaptured = false;
    return prs->m_pvm->compiler->localcount;
}

void nn_astparser_declarevariable(NeonAstParser* prs)
{
    int i;
    NeonAstToken* name;
    NeonAstLocal* local;
    /* global variables are implicitly declared... */
    if(prs->m_pvm->compiler->scopedepth == 0)
    {
        return;
    }
    name = &prs->prevtoken;
    for(i = prs->m_pvm->compiler->localcount - 1; i >= 0; i--)
    {
        local = &prs->m_pvm->compiler->locals[i];
        if(local->depth != -1 && local->depth < prs->m_pvm->compiler->scopedepth)
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

int nn_astparser_parsevariable(NeonAstParser* prs, const char* message)
{
    if(!nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, message))
    {
        /* what to do here? */
    }
    nn_astparser_declarevariable(prs);
    /* we are in a local scope... */
    if(prs->m_pvm->compiler->scopedepth > 0)
    {
        return 0;
    }
    return nn_astparser_makeidentconst(prs, &prs->prevtoken);
}

void nn_astparser_markinitialized(NeonAstParser* prs)
{
    if(prs->m_pvm->compiler->scopedepth == 0)
    {
        return;
    }
    prs->m_pvm->compiler->locals[prs->m_pvm->compiler->localcount - 1].depth = prs->m_pvm->compiler->scopedepth;
}

void nn_astparser_definevariable(NeonAstParser* prs, int global)
{
    /* we are in a local scope... */
    if(prs->m_pvm->compiler->scopedepth > 0)
    {
        nn_astparser_markinitialized(prs);
        return;
    }
    nn_astemit_emitbyteandshort(prs, NEON_OP_GLOBALDEFINE, global);
}

NeonAstToken nn_astparser_synthtoken(const char* name)
{
    NeonAstToken token;
    token.isglobal = false;
    token.line = 0;
    token.type = (NeonAstTokType)0;
    token.start = name;
    token.length = (int)strlen(name);
    return token;
}

NeonObjFuncScript* nn_astparser_endcompiler(NeonAstParser* prs)
{
    const char* fname;
    NeonObjFuncScript* function;
    nn_astemit_emitreturn(prs);
    function = prs->m_pvm->compiler->targetfunc;
    fname = NULL;
    if(function->name == NULL)
    {
        fname = prs->module->physicalpath->data();
    }
    else
    {
        fname = function->name->data();
    }
    if(!prs->haderror && prs->m_pvm->conf.dumpbytecode)
    {
        nn_dbg_disasmblob(prs->m_pvm->debugwriter, nn_astparser_currentblob(prs), fname);
    }
    NEON_ASTDEBUG(prs->m_pvm, "for function '%s'", fname);
    prs->m_pvm->compiler = prs->m_pvm->compiler->enclosing;
    return function;
}

void nn_astparser_scopebegin(NeonAstParser* prs)
{
    NEON_ASTDEBUG(prs->m_pvm, "current depth=%d", prs->m_pvm->compiler->scopedepth);
    prs->m_pvm->compiler->scopedepth++;
}

bool nn_astutil_scopeendcancontinue(NeonAstParser* prs)
{
    int lopos;
    int locount;
    int lodepth;
    int scodepth;
    NEON_ASTDEBUG(prs->m_pvm, "");
    locount = prs->m_pvm->compiler->localcount;
    lopos = prs->m_pvm->compiler->localcount - 1;
    lodepth = prs->m_pvm->compiler->locals[lopos].depth;
    scodepth = prs->m_pvm->compiler->scopedepth;
    if(locount > 0 && lodepth > scodepth)
    {
        return true;
    }
    return false;
}

void nn_astparser_scopeend(NeonAstParser* prs)
{
    NEON_ASTDEBUG(prs->m_pvm, "current scope depth=%d", prs->m_pvm->compiler->scopedepth);
    prs->m_pvm->compiler->scopedepth--;
    /*
    // remove all variables declared in scope while exiting...
    */
    if(prs->keeplastvalue)
    {
        //return;
    }
    while(nn_astutil_scopeendcancontinue(prs))
    {
        if(prs->m_pvm->compiler->locals[prs->m_pvm->compiler->localcount - 1].iscaptured)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_UPVALUECLOSE);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        }
        prs->m_pvm->compiler->localcount--;
    }
}

int nn_astparser_discardlocals(NeonAstParser* prs, int depth)
{
    int local;
    NEON_ASTDEBUG(prs->m_pvm, "");
    if(prs->keeplastvalue)
    {
        //return 0;
    }
    if(prs->m_pvm->compiler->scopedepth == -1)
    {
        nn_astparser_raiseerror(prs, "cannot exit top-level scope");
    }
    local = prs->m_pvm->compiler->localcount - 1;
    while(local >= 0 && prs->m_pvm->compiler->locals[local].depth >= depth)
    {
        if(prs->m_pvm->compiler->locals[local].iscaptured)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_UPVALUECLOSE);
        }
        else
        {
            nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
        }
        local--;
    }
    return prs->m_pvm->compiler->localcount - local - 1;
}

void nn_astparser_endloop(NeonAstParser* prs)
{
    int i;
    NeonInstruction* bcode;
    NeonValue* cvals;
    NEON_ASTDEBUG(prs->m_pvm, "");
    /*
    // find all NEON_OP_BREAK_PL placeholder and replace with the appropriate jump...
    */
    i = prs->innermostloopstart;
    while(i < prs->m_pvm->compiler->targetfunc->blob.count)
    {
        if(prs->m_pvm->compiler->targetfunc->blob.instrucs[i].code == NEON_OP_BREAK_PL)
        {
            prs->m_pvm->compiler->targetfunc->blob.instrucs[i].code = NEON_OP_JUMPNOW;
            nn_astemit_patchjump(prs, i + 1);
            i += 3;
        }
        else
        {
            bcode = prs->m_pvm->compiler->targetfunc->blob.instrucs;
            cvals = prs->m_pvm->compiler->targetfunc->blob.constants->values;
            i += 1 + nn_astparser_getcodeargscount(bcode, cvals, i);
        }
    }
}

bool nn_astparser_rulebinary(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    NeonAstTokType op;
    NeonAstRule* rule;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    op = prs->prevtoken.type;
    /* compile the right operand */
    rule = nn_astparser_getrule(op);
    nn_astparser_parseprecedence(prs, (NeonAstPrecedence)(rule->precedence + 1));
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

bool nn_astparser_rulecall(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    uint8_t argcount;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    argcount = nn_astparser_parsefunccallargs(prs);
    nn_astemit_emit2byte(prs, NEON_OP_CALLFUNCTION, argcount);
    return true;
}

bool nn_astparser_ruleliteral(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
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

void nn_astparser_parseassign(NeonAstParser* prs, uint8_t realop, uint8_t getop, uint8_t setop, int arg)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
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

void nn_astparser_assignment(NeonAstParser* prs, uint8_t getop, uint8_t setop, int arg, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
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

bool nn_astparser_ruledot(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    int name;
    bool caninvoke;
    uint8_t argcount;
    NeonOpCode getop;
    NeonOpCode setop;
    NEON_ASTDEBUG(prs->m_pvm, "");
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
            (prs->currentclass != NULL) &&
            (
                (previous.type == NEON_ASTTOK_KWTHIS) ||
                (nn_astparser_identsequal(&prs->prevtoken, &prs->currentclass->name))
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
        if(prs->currentclass != NULL && (previous.type == NEON_ASTTOK_KWTHIS || nn_astparser_identsequal(&prs->prevtoken, &prs->currentclass->name)))
        {
            getop = NEON_OP_PROPERTYGETSELF;
        }
        nn_astparser_assignment(prs, getop, setop, name, canassign);
    }
    return true;
}

void nn_astparser_namedvar(NeonAstParser* prs, NeonAstToken name, bool canassign)
{
    bool fromclass;
    uint8_t getop;
    uint8_t setop;
    int arg;
    (void)fromclass;
    NEON_ASTDEBUG(prs->m_pvm, " name=%.*s", name.length, name.start);
    fromclass = prs->currentclass != NULL;
    arg = nn_astfunccompiler_resolvelocal(prs, prs->m_pvm->compiler, &name);
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
        arg = nn_astfunccompiler_resolveupvalue(prs, prs->m_pvm->compiler, &name);
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

void nn_astparser_createdvar(NeonAstParser* prs, NeonAstToken name)
{
    int local;
    NEON_ASTDEBUG(prs->m_pvm, "name=%.*s", name.length, name.start);
    if(prs->m_pvm->compiler->targetfunc->name != NULL)
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

bool nn_astparser_rulearray(NeonAstParser* prs, bool canassign)
{
    int count;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
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

bool nn_astparser_ruledictionary(NeonAstParser* prs, bool canassign)
{
    bool usedexpression;
    int itemcount;
    NeonAstCompContext oldctx;
    (void)canassign;
    (void)oldctx;
    NEON_ASTDEBUG(prs->m_pvm, "");
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
                    nn_astemit_emitconst(prs, NeonValue::fromObject(NeonObjString::copy(prs->m_pvm, prs->prevtoken.start, prs->prevtoken.length)));
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

bool nn_astparser_ruleindexing(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    bool assignable;
    bool commamatch;
    uint8_t getop;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
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

bool nn_astparser_rulevarnormal(NeonAstParser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_namedvar(prs, prs->prevtoken, canassign);
    return true;
}

bool nn_astparser_rulevarglobal(NeonAstParser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_namedvar(prs, prs->prevtoken, canassign);
    return true;
}

bool nn_astparser_rulethis(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    #if 0
    if(prs->currentclass == NULL)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'this' outside of a class");
        return false;
    }
    #endif
    //if(prs->currentclass != NULL)
    {
        nn_astparser_namedvar(prs, prs->prevtoken, false);
        //nn_astparser_namedvar(prs, nn_astparser_synthtoken(g_strthis), false);
    }
    return true;
}

bool nn_astparser_rulesuper(NeonAstParser* prs, bool canassign)
{
    int name;
    bool invokeself;
    uint8_t argcount;
    NEON_ASTDEBUG(prs->m_pvm, "");
    (void)canassign;
    if(prs->currentclass == NULL)
    {
        nn_astparser_raiseerror(prs, "cannot use keyword 'super' outside of a class");
        return false;
    }
    else if(!prs->currentclass->hassuperclass)
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

bool nn_astparser_rulegrouping(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
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

NeonValue nn_astparser_compilenumber(NeonAstParser* prs)
{
    double dbval;
    long longval;
    long long llval;
    NEON_ASTDEBUG(prs->m_pvm, "");
    if(prs->prevtoken.type == NEON_ASTTOK_LITNUMBIN)
    {
        llval = strtoll(prs->prevtoken.start + 2, NULL, 2);
        return NeonValue::makeNumber(llval);
    }
    else if(prs->prevtoken.type == NEON_ASTTOK_LITNUMOCT)
    {
        longval = strtol(prs->prevtoken.start + 2, NULL, 8);
        return NeonValue::makeNumber(longval);
    }
    else if(prs->prevtoken.type == NEON_ASTTOK_LITNUMHEX)
    {
        longval = strtol(prs->prevtoken.start, NULL, 16);
        return NeonValue::makeNumber(longval);
    }
    else
    {
        dbval = strtod(prs->prevtoken.start, NULL);
        return NeonValue::makeNumber(dbval);
    }
}

bool nn_astparser_rulenumber(NeonAstParser* prs, bool canassign)
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
int nn_astparser_readhexescape(NeonAstParser* prs, const char* str, int index, int count)
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

int nn_astparser_readunicodeescape(NeonAstParser* prs, char* string, const char* realstring, int numberbytes, int realindex, int index)
{
    int value;
    int count;
    char* chr;
    NEON_ASTDEBUG(prs->m_pvm, "");
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
        chr = nn_util_utf8encode(prs->m_pvm, value);
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

char* nn_astparser_compilestring(NeonAstParser* prs, int* length)
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
    NEON_ASTDEBUG(prs->m_pvm, "raw length=%d", rawlen);
    deststr = (char*)NeonState::GC::allocate(prs->m_pvm, sizeof(char), rawlen);
    quote = prs->prevtoken.start[0];
    realstr = (char*)prs->prevtoken.start + 1;
    reallength = prs->prevtoken.length - 2;
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
                        //int nn_astparser_readunicodeescape(NeonAstParser* prs, char* string, char* realstring, int numberbytes, int realindex, int index)
                        //int nn_astparser_readhexescape(NeonAstParser* prs, const char* str, int index, int count)
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

bool nn_astparser_rulestring(NeonAstParser* prs, bool canassign)
{
    int length;
    char* str;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "canassign=%d", canassign);
    str = nn_astparser_compilestring(prs, &length);
    nn_astemit_emitconst(prs, NeonValue::fromObject(NeonObjString::take(prs->m_pvm, str, length)));
    return true;
}

bool nn_astparser_ruleinterpolstring(NeonAstParser* prs, bool canassign)
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
    nn_astparser_consume(prs, NEON_ASTTOK_LITERAL, "unterminated string interpolation");
    if(prs->prevtoken.length - 2 > 0)
    {
        nn_astparser_rulestring(prs, canassign);
        nn_astemit_emitinstruc(prs, NEON_OP_PRIMADD);
    }
    return true;
}

bool nn_astparser_ruleunary(NeonAstParser* prs, bool canassign)
{
    NeonAstTokType op;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
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

bool nn_astparser_ruleand(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    int endjump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    endjump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_AND);
    nn_astemit_patchjump(prs, endjump);
    return true;
}

bool nn_astparser_ruleor(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    int endjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    elsejump = nn_astemit_emitjump(prs, NEON_OP_JUMPIFFALSE);
    endjump = nn_astemit_emitjump(prs, NEON_OP_JUMPNOW);
    nn_astemit_patchjump(prs, elsejump);
    nn_astemit_emitinstruc(prs, NEON_OP_POPONE);
    nn_astparser_parseprecedence(prs, NEON_ASTPREC_OR);
    nn_astemit_patchjump(prs, endjump);
    return true;
}

bool nn_astparser_ruleconditional(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    int thenjump;
    int elsejump;
    (void)previous;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
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

bool nn_astparser_ruleimport(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_IMPORTIMPORT);
    return true;
}

bool nn_astparser_rulenew(NeonAstParser* prs, bool canassign)
{
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "class name after 'new'");
    return nn_astparser_rulevarnormal(prs, canassign);
    //return nn_astparser_rulecall(prs, prs->prevtoken, canassign);
}

bool nn_astparser_ruletypeof(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after 'typeof'");
    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after 'typeof'");
    nn_astemit_emitinstruc(prs, NEON_OP_TYPEOF);
    return true;
}

bool nn_astparser_rulenothingprefix(NeonAstParser* prs, bool canassign)
{
    (void)prs;
    (void)canassign;
    NEON_ASTDEBUG(prs->m_pvm, "");
    return true;
}

bool nn_astparser_rulenothinginfix(NeonAstParser* prs, NeonAstToken previous, bool canassign)
{
    (void)prs;
    (void)previous;
    (void)canassign;
    return true;
}

NeonAstRule* nn_astparser_putrule(NeonAstRule* dest, NeonAstParsePrefixFN prefix, NeonAstParseInfixFN infix, NeonAstPrecedence precedence)
{
    dest->prefix = prefix;
    dest->infix = infix;
    dest->precedence = precedence;
    return dest;
}

#define dorule(tok, prefix, infix, precedence) \
    case tok: return nn_astparser_putrule(&dest, prefix, infix, precedence);

NeonAstRule* nn_astparser_getrule(NeonAstTokType type)
{
    static NeonAstRule dest;
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
        dorule(NEON_ASTTOK_KWCLASS, NULL, NULL, NEON_ASTPREC_NONE );
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
        dorule(NEON_ASTTOK_LITERAL, nn_astparser_rulestring, NULL, NEON_ASTPREC_NONE );
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

bool nn_astparser_doparseprecedence(NeonAstParser* prs, NeonAstPrecedence precedence/*, NeonAstExpression* dest*/)
{
    bool canassign;
    NeonAstToken previous;
    NeonAstParseInfixFN infixrule;
    NeonAstParsePrefixFN prefixrule;
    prefixrule = nn_astparser_getrule(prs->prevtoken.type)->prefix;
    if(prefixrule == NULL)
    {
        nn_astparser_raiseerror(prs, "expected expression");
        return false;
    }
    canassign = precedence <= NEON_ASTPREC_ASSIGNMENT;
    prefixrule(prs, canassign);
    while(precedence <= nn_astparser_getrule(prs->currtoken.type)->precedence)
    {
        previous = prs->prevtoken;
        nn_astparser_ignorewhitespace(prs);
        nn_astparser_advance(prs);
        infixrule = nn_astparser_getrule(prs->prevtoken.type)->infix;
        infixrule(prs, previous, canassign);
    }
    if(canassign && nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
    {
        nn_astparser_raiseerror(prs, "invalid assignment target");
        return false;
    }
    return true;
}

bool nn_astparser_parseprecedence(NeonAstParser* prs, NeonAstPrecedence precedence)
{
    if(nn_astlex_isatend(prs->scanner) && prs->m_pvm->isrepl)
    {
        return false;
    }
    nn_astparser_ignorewhitespace(prs);
    if(nn_astlex_isatend(prs->scanner) && prs->m_pvm->isrepl)
    {
        return false;
    }
    nn_astparser_advance(prs);
    return nn_astparser_doparseprecedence(prs, precedence);
}

bool nn_astparser_parseprecnoadvance(NeonAstParser* prs, NeonAstPrecedence precedence)
{
    if(nn_astlex_isatend(prs->scanner) && prs->m_pvm->isrepl)
    {
        return false;
    }
    nn_astparser_ignorewhitespace(prs);
    if(nn_astlex_isatend(prs->scanner) && prs->m_pvm->isrepl)
    {
        return false;
    }
    return nn_astparser_doparseprecedence(prs, precedence);
}

bool nn_astparser_parseexpression(NeonAstParser* prs)
{
    return nn_astparser_parseprecedence(prs, NEON_ASTPREC_ASSIGNMENT);
}

bool nn_astparser_parseblock(NeonAstParser* prs)
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

void nn_astparser_declarefuncargvar(NeonAstParser* prs)
{
    int i;
    NeonAstToken* name;
    NeonAstLocal* local;
    /* global variables are implicitly declared... */
    if(prs->m_pvm->compiler->scopedepth == 0)
    {
        return;
    }
    name = &prs->prevtoken;
    for(i = prs->m_pvm->compiler->localcount - 1; i >= 0; i--)
    {
        local = &prs->m_pvm->compiler->locals[i];
        if(local->depth != -1 && local->depth < prs->m_pvm->compiler->scopedepth)
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


int nn_astparser_parsefuncparamvar(NeonAstParser* prs, const char* message)
{
    if(!nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, message))
    {
        /* what to do here? */
    }
    nn_astparser_declarefuncargvar(prs);
    /* we are in a local scope... */
    if(prs->m_pvm->compiler->scopedepth > 0)
    {
        return 0;
    }
    return nn_astparser_makeidentconst(prs, &prs->prevtoken);
}

uint8_t nn_astparser_parsefunccallargs(NeonAstParser* prs)
{
    uint8_t argcount;
    argcount = 0;
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENCLOSE))
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
        } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
    }
    nn_astparser_ignorewhitespace(prs);
    if(!nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after argument list"))
    {
        /* TODO: handle this, somehow. */
    }
    return argcount;
}

void nn_astparser_parsefuncparamlist(NeonAstParser* prs)
{
    int paramconstant;
    /* compile argument list... */
    do
    {
        nn_astparser_ignorewhitespace(prs);
        prs->m_pvm->compiler->targetfunc->arity++;
        if(prs->m_pvm->compiler->targetfunc->arity > NEON_CFG_ASTMAXFUNCPARAMS)
        {
            nn_astparser_raiseerroratcurrent(prs, "cannot have more than %d function parameters", NEON_CFG_ASTMAXFUNCPARAMS);
        }
        if(nn_astparser_match(prs, NEON_ASTTOK_TRIPLEDOT))
        {
            prs->m_pvm->compiler->targetfunc->isvariadic = true;
            nn_astparser_addlocal(prs, nn_astparser_synthtoken("__args__"));
            nn_astparser_definevariable(prs, 0);
            break;
        }
        paramconstant = nn_astparser_parsefuncparamvar(prs, "expected parameter name");
        nn_astparser_definevariable(prs, paramconstant);
        nn_astparser_ignorewhitespace(prs);
    } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
}

void nn_astfunccompiler_compilebody(NeonAstParser* prs, NeonAstFuncCompiler* compiler, bool closescope, bool isanon)
{
    int i;
    NeonObjFuncScript* function;
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
    function = nn_astparser_endcompiler(prs);
    prs->m_pvm->stackPush(NeonValue::fromObject(function));
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKECLOSURE, nn_astparser_pushconst(prs, NeonValue::fromObject(function)));
    for(i = 0; i < function->upvalcount; i++)
    {
        nn_astemit_emit1byte(prs, compiler->upvalues[i].islocal ? 1 : 0);
        nn_astemit_emit1short(prs, compiler->upvalues[i].index);
    }
    prs->m_pvm->stackPop();
}

void nn_astparser_parsefuncfull(NeonAstParser* prs, NeonFuncType type, bool isanon)
{
    NeonAstFuncCompiler compiler;
    prs->infunction = true;
    nn_astfunccompiler_init(prs, &compiler, type, isanon);
    nn_astparser_scopebegin(prs);
    /* compile parameter list */
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' after function name");
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENCLOSE))
    {
        nn_astparser_parsefuncparamlist(prs);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after function parameters");
    nn_astfunccompiler_compilebody(prs, &compiler, false, isanon);
    prs->infunction = false;
}

void nn_astparser_parsemethod(NeonAstParser* prs, NeonAstToken classname, bool isstatic)
{
    size_t sn;
    int constant;
    const char* sc;
    NeonFuncType type;
    static NeonAstTokType tkns[] = { NEON_ASTTOK_IDENTNORMAL, NEON_ASTTOK_DECORATOR };
    (void)classname;
    (void)isstatic;
    sc = "constructor";
    sn = strlen(sc);
    nn_astparser_consumeor(prs, "method name expected", tkns, 2);
    constant = nn_astparser_makeidentconst(prs, &prs->prevtoken);
    type = NEON_FUNCTYPE_METHOD;
    if((prs->prevtoken.length == (int)sn) && (memcmp(prs->prevtoken.start, sc, sn) == 0))
    {
        type = NEON_FUNCTYPE_INITIALIZER;
    }
    else if((prs->prevtoken.length > 0) && (prs->prevtoken.start[0] == '_'))
    {
        type = NEON_FUNCTYPE_PRIVATE;
    }
    nn_astparser_parsefuncfull(prs, type, false);
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKEMETHOD, constant);
}

bool nn_astparser_ruleanonfunc(NeonAstParser* prs, bool canassign)
{
    NeonAstFuncCompiler compiler;
    (void)canassign;
    nn_astfunccompiler_init(prs, &compiler, NEON_FUNCTYPE_FUNCTION, true);
    nn_astparser_scopebegin(prs);
    /* compile parameter list */
    nn_astparser_consume(prs, NEON_ASTTOK_PARENOPEN, "expected '(' at start of anonymous function");
    if(!nn_astparser_check(prs, NEON_ASTTOK_PARENCLOSE))
    {
        nn_astparser_parsefuncparamlist(prs);
    }
    nn_astparser_consume(prs, NEON_ASTTOK_PARENCLOSE, "expected ')' after anonymous function parameters");
    nn_astfunccompiler_compilebody(prs, &compiler, true, true);
    return true;
}

void nn_astparser_parsefield(NeonAstParser* prs, bool isstatic)
{
    int fieldconstant;
    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "class property name expected");
    fieldconstant = nn_astparser_makeidentconst(prs, &prs->prevtoken);
    if(nn_astparser_match(prs, NEON_ASTTOK_ASSIGN))
    {
        nn_astparser_parseexpression(prs);
    }
    else
    {
        nn_astemit_emitinstruc(prs, NEON_OP_PUSHNULL);
    }
    nn_astemit_emitbyteandshort(prs, NEON_OP_CLASSPROPERTYDEFINE, fieldconstant);
    nn_astemit_emit1byte(prs, isstatic ? 1 : 0);
    nn_astparser_consumestmtend(prs);
    nn_astparser_ignorewhitespace(prs);
}

void nn_astparser_parsefuncdecl(NeonAstParser* prs)
{
    int global;
    global = nn_astparser_parsevariable(prs, "function name expected");
    nn_astparser_markinitialized(prs);
    nn_astparser_parsefuncfull(prs, NEON_FUNCTYPE_FUNCTION, false);
    nn_astparser_definevariable(prs, global);
}

void nn_astparser_parseclassdeclaration(NeonAstParser* prs)
{
    bool isstatic;
    int nameconst;
    NeonAstCompContext oldctx;
    NeonAstToken classname;
    NeonAstClassCompiler classcompiler;
    nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "class name expected");
    nameconst = nn_astparser_makeidentconst(prs, &prs->prevtoken);
    classname = prs->prevtoken;
    nn_astparser_declarevariable(prs);
    nn_astemit_emitbyteandshort(prs, NEON_OP_MAKECLASS, nameconst);
    nn_astparser_definevariable(prs, nameconst);
    classcompiler.name = prs->prevtoken;
    classcompiler.hassuperclass = false;
    classcompiler.enclosing = prs->currentclass;
    prs->currentclass = &classcompiler;
    oldctx = prs->compcontext;
    prs->compcontext = NEON_COMPCONTEXT_CLASS;
    if(nn_astparser_match(prs, NEON_ASTTOK_LESSTHAN))
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

        if(nn_astparser_match(prs, NEON_ASTTOK_KWVAR))
        {
            nn_astparser_parsefield(prs, isstatic);
        }
        else
        {
            nn_astparser_parsemethod(prs, classname, isstatic);
            nn_astparser_ignorewhitespace(prs);
        }
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
    prs->currentclass = prs->currentclass->enclosing;
    prs->compcontext = oldctx;
}

void nn_astparser_parsevardecl(NeonAstParser* prs, bool isinitializer)
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

void nn_astparser_parseexprstmt(NeonAstParser* prs, bool isinitializer, bool semi)
{
    if(prs->m_pvm->isrepl && prs->m_pvm->compiler->scopedepth == 0)
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
        if(prs->replcanecho && prs->m_pvm->isrepl)
        {
            nn_astemit_emitinstruc(prs, NEON_OP_ECHO);
            prs->replcanecho = false;
        }
        else
        {
            //if(!prs->keeplastvalue)
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
void nn_astparser_parseforstmt(NeonAstParser* prs)
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
        nn_astparser_parsevardecl(prs, true);
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
    prs->innermostloopscopedepth = prs->m_pvm->compiler->scopedepth;
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
void nn_astparser_parseforeachstmt(NeonAstParser* prs)
{
    int citer;
    int citern;
    int falsejump;
    int keyslot;
    int valueslot;
    int iteratorslot;
    int surroundingloopstart;
    int surroundingscopedepth;
    NeonAstToken iteratortoken;
    NeonAstToken keytoken;
    NeonAstToken valuetoken;
    nn_astparser_scopebegin(prs);
    /* define @iter and @itern constant */
    citer = nn_astparser_pushconst(prs, NeonValue::fromObject(NeonObjString::copy(prs->m_pvm, "@iter")));
    citern = nn_astparser_pushconst(prs, NeonValue::fromObject(NeonObjString::copy(prs->m_pvm, "@itern")));
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
    if(prs->m_pvm->compiler->localcount + 3 > NEON_CFG_ASTMAXLOCALS)
    {
        nn_astparser_raiseerror(prs, "cannot declare more than %d variables in one scope", NEON_CFG_ASTMAXLOCALS);
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
    prs->innermostloopscopedepth = prs->m_pvm->compiler->scopedepth;
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
void nn_astparser_parseswitchstmt(NeonAstParser* prs)
{
    int i;
    int length;
    int swstate;
    int casecount;
    int switchcode;
    int startoffset;
    int caseends[NEON_CFG_ASTMAXSWITCHCASES];
    char* str;
    NeonValue jump;
    NeonAstTokType casetype;
    NeonObjSwitch* sw;
    NeonObjString* string;
    /* the expression */
    nn_astparser_parseexpression(prs);
    nn_astparser_consume(prs, NEON_ASTTOK_BRACEOPEN, "expected '{' after 'switch' expression");
    nn_astparser_ignorewhitespace(prs);
    /* 0: before all cases, 1: before default, 2: after default */
    swstate = 0;
    casecount = 0;
    sw = nn_object_makeswitch(prs->m_pvm);
    prs->m_pvm->stackPush(NeonValue::fromObject(sw));
    switchcode = nn_astemit_emitswitch(prs);
    /* nn_astemit_emitbyteandshort(prs, NEON_OP_SWITCH, nn_astparser_pushconst(prs, NeonValue::fromObject(sw))); */
    startoffset = nn_astparser_currentblob(prs)->count;
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
                    jump = NeonValue::makeNumber((double)nn_astparser_currentblob(prs)->count - (double)startoffset);
                    if(prs->prevtoken.type == NEON_ASTTOK_KWTRUE)
                    {
                        sw->table->set(NeonValue::makeBool(true), jump);
                    }
                    else if(prs->prevtoken.type == NEON_ASTTOK_KWFALSE)
                    {
                        sw->table->set(NeonValue::makeBool(false), jump);
                    }
                    else if(prs->prevtoken.type == NEON_ASTTOK_LITERAL)
                    {
                        str = nn_astparser_compilestring(prs, &length);
                        string = NeonObjString::take(prs->m_pvm, str, length);
                        /* gc fix */
                        prs->m_pvm->stackPush(NeonValue::fromObject(string));
                        sw->table->set(NeonValue::fromObject(string), jump);
                        /* gc fix */
                        prs->m_pvm->stackPop();
                    }
                    else if(nn_astparser_checknumber(prs))
                    {
                        sw->table->set(nn_astparser_compilenumber(prs), jump);
                    }
                    else
                    {
                        /* pop the switch */
                        prs->m_pvm->stackPop();
                        nn_astparser_raiseerror(prs, "only constants can be used in 'when' expressions");
                        return;
                    }
                } while(nn_astparser_match(prs, NEON_ASTTOK_COMMA));
            }
            else
            {
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
    nn_astemit_patchswitch(prs, switchcode, nn_astparser_pushconst(prs, NeonValue::fromObject(sw)));
    /* pop the switch */
    prs->m_pvm->stackPop();
}

void nn_astparser_parseifstmt(NeonAstParser* prs)
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

void nn_astparser_parseechostmt(NeonAstParser* prs)
{
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_ECHO);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parsethrowstmt(NeonAstParser* prs)
{
    nn_astparser_parseexpression(prs);
    nn_astemit_emitinstruc(prs, NEON_OP_EXTHROW);
    nn_astparser_discardlocals(prs, prs->m_pvm->compiler->scopedepth - 1);
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_parseassertstmt(NeonAstParser* prs)
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

void nn_astparser_parsetrystmt(NeonAstParser* prs)
{
    int address;
    int type;
    int finally;
    int trybegins;
    int exitjump;
    int continueexecutionaddress;
    bool catchexists;
    bool finalexists;
    if(prs->m_pvm->compiler->handlercount == NEON_CFG_MAXEXCEPTHANDLERS)
    {
        nn_astparser_raiseerror(prs, "maximum exception handler in scope exceeded");
    }
    prs->m_pvm->compiler->handlercount++;
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
        nn_astparser_consume(prs, NEON_ASTTOK_IDENTNORMAL, "missing exception class name");
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
        type = nn_astparser_pushconst(prs, NeonValue::fromObject(NeonObjString::copy(prs->m_pvm, "Exception")));
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

void nn_astparser_parsereturnstmt(NeonAstParser* prs)
{
    prs->isreturning = true;
    /*
    if(prs->m_pvm->compiler->type == NEON_FUNCTYPE_SCRIPT)
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
        if(prs->m_pvm->compiler->type == NEON_FUNCTYPE_INITIALIZER)
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

void nn_astparser_parsewhilestmt(NeonAstParser* prs)
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
    prs->innermostloopscopedepth = prs->m_pvm->compiler->scopedepth;
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

void nn_astparser_parsedo_whilestmt(NeonAstParser* prs)
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
    prs->innermostloopscopedepth = prs->m_pvm->compiler->scopedepth;
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

void nn_astparser_parsecontinuestmt(NeonAstParser* prs)
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

void nn_astparser_parsebreakstmt(NeonAstParser* prs)
{
    if(prs->innermostloopstart == -1)
    {
        nn_astparser_raiseerror(prs, "'break' can only be used in a loop");
    }
    /* discard local variables created in the loop */
    /*
    int i;
    for(i = prs->m_pvm->compiler->localcount - 1; i >= 0 && prs->m_pvm->compiler->locals[i].depth >= prs->m_pvm->compiler->scopedepth; i--)
    {
        if (prs->m_pvm->compiler->locals[i].iscaptured)
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
    nn_astparser_consumestmtend(prs);
}

void nn_astparser_synchronize(NeonAstParser* prs)
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
NeonObjFuncScript* nn_astparser_compilesource(NeonState* state, NeonObjModule* module, const char* source, NeonBlob* blob, bool fromimport, bool keeplast)
{
    NeonAstFuncCompiler compiler;
    NeonAstLexer* lexer;
    NeonAstParser* parser;
    NeonObjFuncScript* function;
    (void)blob;
    NEON_ASTDEBUG(state, "module=%p source=[...] blob=[...] fromimport=%d keeplast=%d", module, fromimport, keeplast);
    lexer = nn_astlex_init(state, source);
    parser = nn_astparser_make(state, lexer, module, keeplast);
    nn_astfunccompiler_init(parser, &compiler, NEON_FUNCTYPE_SCRIPT, true);
    compiler.fromimport = fromimport;
    nn_astparser_runparser(parser);
    function = nn_astparser_endcompiler(parser);
    if(parser->haderror)
    {
        function = NULL;
    }
    nn_astlex_destroy(state, lexer);
    nn_astparser_destroy(state, parser);
    return function;
}

void nn_gcmem_markcompilerroots(NeonState* state)
{
    NeonAstFuncCompiler* compiler;
    compiler = state->compiler;
    while(compiler != NULL)
    {
        nn_gcmem_markobject(state, (NeonObject*)compiler->targetfunc);
        compiler = compiler->enclosing;
    }
}

NeonRegModule* nn_natmodule_load_null(NeonState* state)
{
    (void)state;
    static NeonRegFunc modfuncs[] =
    {
        /* {"somefunc",   true,  myfancymodulefunction},*/
        {NULL, false, NULL},
    };

    static NeonRegField modfields[] =
    {
        /*{"somefield", true, the_function_that_gets_called},*/
        {NULL, false, NULL},
    };

    static NeonRegModule module;
    module.name = "null";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = NULL;
    module.preloader= NULL;
    module.unloader = NULL;
    return &module;
}

void nn_modfn_os_preloader(NeonState* state)
{
    (void)state;
}

NeonValue nn_modfn_os_readdir(NeonState* state, NeonArguments* args)
{
    const char* dirn;
    FSDirReader rd;
    FSDirItem itm;
    NeonObjString* os;
    NeonObjString* aval;
    NeonObjArray* res;
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    os = args->argv[0].asString();
    dirn = os->data();
    if(fslib_diropen(&rd, dirn))
    {
        res = NeonObjArray::make(state);
        while(fslib_dirread(&rd, &itm))
        {
            aval = NeonObjString::copy(state, itm.name);
            res->push(NeonValue::fromObject(aval));
        }
        fslib_dirclose(&rd);
        return NeonValue::fromObject(res);
    }
    else
    {
        nn_exceptions_throwException(state, "cannot open directory '%s'", dirn);
    }
    return NeonValue::makeEmpty();
}

NeonRegModule* nn_natmodule_load_os(NeonState* state)
{
    (void)state;
    static NeonRegFunc modfuncs[] =
    {
        {"readdir",   true,  nn_modfn_os_readdir},
        {NULL,     false, NULL},
    };
    static NeonRegField modfields[] =
    {
        /*{"platform", true, get_os_platform},*/
        {NULL,       false, NULL},
    };
    static NeonRegModule module;
    module.name = "os";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = NULL;
    module.preloader= &nn_modfn_os_preloader;
    module.unloader = NULL;
    return &module;
}

NeonValue nn_modfn_astscan_scan(NeonState* state, NeonArguments* args)
{
    const char* cstr;
    NeonObjString* insrc;
    NeonAstLexer* scn;
    NeonObjArray* arr;
    NeonObjDict* itm;
    NeonAstToken token;
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    insrc = args->argv[0].asString();
    scn = nn_astlex_init(state, insrc->data());
    arr = NeonObjArray::make(state);
    while(!nn_astlex_isatend(scn))
    {
        itm = nn_object_makedict(state);
        token = nn_astlex_scantoken(scn);
        nn_dict_addentrycstr(itm, "line", NeonValue::makeNumber(token.line));
        cstr = nn_astutil_toktype2str(token.type);
        nn_dict_addentrycstr(itm, "type", NeonValue::fromObject(NeonObjString::copy(state, cstr + 12)));
        nn_dict_addentrycstr(itm, "source", NeonValue::fromObject(NeonObjString::copy(state, token.start, token.length)));
        arr->push(NeonValue::fromObject(itm));
    }
    nn_astlex_destroy(state, scn);
    return NeonValue::fromObject(arr);
}

NeonRegModule* nn_natmodule_load_astscan(NeonState* state)
{
    NeonRegModule* ret;
    (void)state;
    static NeonRegFunc modfuncs[] =
    {
        {"scan",   true,  nn_modfn_astscan_scan},
        {NULL,     false, NULL},
    };
    static NeonRegField modfields[] =
    {
        {NULL,       false, NULL},
    };
    static NeonRegModule module;
    module.name = "astscan";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = NULL;
    module.preloader= NULL;
    module.unloader = NULL;
    ret = &module;
    return ret;
}

NeonModInitFN g_builtinmodules[] =
{
    nn_natmodule_load_null,
    nn_natmodule_load_os,
    nn_natmodule_load_astscan,
    NULL,
};

bool nn_import_loadnativemodule(NeonState* state, NeonModInitFN init_fn, char* importname, const char* source, void* dlw)
{
    int j;
    int k;
    NeonValue v;
    NeonValue fieldname;
    NeonValue funcname;
    NeonValue funcrealvalue;
    NeonRegFunc func;
    NeonRegField field;
    NeonRegModule* module;
    NeonObjModule* themodule;
    NeonRegClass klassreg;
    NeonObjString* classname;
    NeonObjFuncNative* native;
    NeonObjClass* klass;
    NeonHashTable* tabdest;
    module = init_fn(state);
    if(module != NULL)
    {
        themodule = state->gcProtect(nn_module_make(state, (char*)module->name, source, false));
        themodule->preloader = (void*)module->preloader;
        themodule->unloader = (void*)module->unloader;
        if(module->fields != NULL)
        {
            for(j = 0; module->fields[j].name != NULL; j++)
            {
                field = module->fields[j];
                fieldname = NeonValue::fromObject(state->gcProtect(NeonObjString::copy(state, field.name)));
                v = field.fieldvalfn(state);
                state->stackPush(v);
                themodule->deftable->set(fieldname, v);
                state->stackPop();
            }
        }
        if(module->functions != NULL)
        {
            for(j = 0; module->functions[j].name != NULL; j++)
            {
                func = module->functions[j];
                funcname = NeonValue::fromObject(state->gcProtect(NeonObjString::copy(state, func.name)));
                funcrealvalue = NeonValue::fromObject(state->gcProtect(nn_object_makefuncnative(state, func.function, func.name)));
                state->stackPush(funcrealvalue);
                themodule->deftable->set(funcname, funcrealvalue);
                state->stackPop();
            }
        }
        if(module->classes != NULL)
        {
            for(j = 0; module->classes[j].name != NULL; j++)
            {
                klassreg = module->classes[j];
                classname = state->gcProtect(NeonObjString::copy(state, klassreg.name));
                klass = state->gcProtect(nn_object_makeclass(state, classname));
                if(klassreg.functions != NULL)
                {
                    for(k = 0; klassreg.functions[k].name != NULL; k++)
                    {
                        func = klassreg.functions[k];
                        funcname = NeonValue::fromObject(state->gcProtect(NeonObjString::copy(state, func.name)));
                        native = state->gcProtect(nn_object_makefuncnative(state, func.function, func.name));
                        if(func.isstatic)
                        {
                            native->type = NEON_FUNCTYPE_STATIC;
                        }
                        else if(strlen(func.name) > 0 && func.name[0] == '_')
                        {
                            native->type = NEON_FUNCTYPE_PRIVATE;
                        }
                        klass->methods->set(funcname, NeonValue::fromObject(native));
                    }
                }
                if(klassreg.fields != NULL)
                {
                    for(k = 0; klassreg.fields[k].name != NULL; k++)
                    {
                        field = klassreg.fields[k];
                        fieldname = NeonValue::fromObject(state->gcProtect(NeonObjString::copy(state, field.name)));
                        v = field.fieldvalfn(state);
                        state->stackPush(v);
                        tabdest = klass->instprops;
                        if(field.isstatic)
                        {
                            tabdest = klass->staticproperties;
                        }
                        tabdest->set(fieldname, v);
                        state->stackPop();
                    }
                }
                themodule->deftable->set(NeonValue::fromObject(classname), NeonValue::fromObject(klass));
            }
        }
        if(dlw != NULL)
        {
            themodule->handle = dlw;
        }
        nn_import_addnativemodule(state, themodule, themodule->name->data());
        state->clearProtect();
        return true;
    }
    else
    {
        state->warn("Error loading module: %s\n", importname);
    }
    return false;
}

void nn_import_addnativemodule(NeonState* state, NeonObjModule* module, const char* as)
{
    NeonValue name;
    if(as != NULL)
    {
        module->name = NeonObjString::copy(state, as);
    }
    name = NeonValue::fromObject(NeonObjString::copyObjString(state, module->name));
    state->stackPush(name);
    state->stackPush(NeonValue::fromObject(module));
    state->modules->set(name, NeonValue::fromObject(module));
    state->stackPop(2);
}

void nn_import_loadbuiltinmodules(NeonState* state)
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


bool nn_util_fsfileexists(NeonState* state, const char* filepath)
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

bool nn_util_fsfileisfile(NeonState* state, const char* filepath)
{
    (void)state;
    (void)filepath;
    return false;
}

bool nn_util_fsfileisdirectory(NeonState* state, const char* filepath)
{
    (void)state;
    (void)filepath;
    return false;
}

NeonObjModule* nn_import_loadmodulescript(NeonState* state, NeonObjModule* intomodule, NeonObjString* modulename)
{
    int argc;
    size_t fsz;
    char* source;
    char* physpath;
    NeonBlob blob;
    NeonValue retv;
    NeonValue callable;
    NeonProperty* field;
    NeonObjArray* args;
    NeonObjString* os;
    NeonObjModule* module;
    NeonObjFuncClosure* closure;
    NeonObjFuncScript* function;
    (void)os;
    (void)argc;
    (void)intomodule;
    field = state->modules->getFieldByObjStr(modulename);
    if(field != NULL)
    {
        return nn_value_asmodule(field->value);
    }
    physpath = nn_import_resolvepath(state, modulename->data(), intomodule->physicalpath->data(), NULL, false);
    if(physpath == NULL)
    {
        nn_exceptions_throwException(state, "module not found: '%s'\n", modulename->data());
        return NULL;
    }
    fprintf(stderr, "loading module from '%s'\n", physpath);
    source = nn_util_readfile(state, physpath, &fsz);
    if(source == NULL)
    {
        nn_exceptions_throwException(state, "could not read import file %s", physpath);
        return NULL;
    }
    nn_blob_init(state, &blob);
    module = nn_module_make(state, modulename->data(), physpath, true);
    free(physpath);
    function = nn_astparser_compilesource(state, module, source, &blob, true, false);
    free(source);
    closure = nn_object_makefuncclosure(state, function);
    callable = NeonValue::fromObject(closure);
    args = NeonObjArray::make(state);
    argc = nn_nestcall_prepare(state, callable, NeonValue::makeNull(), args);
    if(!nn_nestcall_callfunction(state, callable, NeonValue::makeNull(), args, &retv))
    {
        nn_blob_destroy(state, &blob);
        nn_exceptions_throwException(state, "failed to call compiled import closure");
        return NULL;
    }
    nn_blob_destroy(state, &blob);
    return module;
}

char* nn_import_resolvepath(NeonState* state, const char* modulename, const char* currentfile, char* rootfile, bool isrelative)
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
    NeonObjString* pitem;
    StringBuffer* pathbuf;
    (void)rootfile;
    (void)isrelative;
    (void)stroot;
    (void)stmod;
    pathbuf = NULL;
    mlen = strlen(modulename);
    splen = state->importpath->m_count;
    for(i=0; i<splen; i++)
    {
        pitem = state->importpath->values[i].asString();
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
            path1 = osfn_realpath(cstrpath, NULL);
            path2 = osfn_realpath(currentfile, NULL);
            if(path1 != NULL && path2 != NULL)
            {
                if(memcmp(path1, path2, (int)strlen(path2)) == 0)
                {
                    free(path1);
                    free(path2);
                    fprintf(stderr, "resolvepath: refusing to import itself\n");
                    return NULL;
                }
                free(path2);
                delete pathbuf;
                pathbuf = NULL;
                retme = nn_util_strdup(state, path1);
                free(path1);
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

char* nn_util_fsgetbasename(NeonState* state, const char* path)
{
    (void)state;
    return osfn_basename(path);
}

#define ENFORCE_VALID_DICT_KEY(chp, index) \
    NEON_ARGS_REJECTTYPE(chp, isArray, index); \
    NEON_ARGS_REJECTTYPE(chp, isDict, index); \
    NEON_ARGS_REJECTTYPE(chp, isFile, index);

NeonValue nn_memberfunc_dict_length(NeonState* state, NeonArguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return NeonValue::makeNumber(nn_value_asdict(args->thisval)->names->m_count);
}

NeonValue nn_memberfunc_dict_add(NeonState* state, NeonArguments* args)
{
    NeonValue tempvalue;
    NeonObjDict* dict;
    NEON_ARGS_CHECKCOUNT(args, 2);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = nn_value_asdict(args->thisval);
    if(dict->htab->get(args->argv[0], &tempvalue))
    {
        NEON_RETURNERROR("duplicate key %s at add()", nn_value_tostring(state, args->argv[0])->data());
    }
    nn_dict_addentry(dict, args->argv[0], args->argv[1]);
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_dict_set(NeonState* state, NeonArguments* args)
{
    NeonValue value;
    NeonObjDict* dict;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 2);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = nn_value_asdict(args->thisval);
    if(!dict->htab->get(args->argv[0], &value))
    {
        nn_dict_addentry(dict, args->argv[0], args->argv[1]);
    }
    else
    {
        nn_dict_setentry(dict, args->argv[0], args->argv[1]);
    }
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_dict_clear(NeonState* state, NeonArguments* args)
{
    NeonObjDict* dict;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = nn_value_asdict(args->thisval);
    delete dict->names;
    delete dict->htab;
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_dict_clone(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjDict* dict;
    NeonObjDict* newdict;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = nn_value_asdict(args->thisval);
    newdict = state->gcProtect(nn_object_makedict(state));
    NeonHashTable::addAll(dict->htab, newdict->htab);
    for(i = 0; i < dict->names->m_count; i++)
    {
        newdict->names->push(dict->names->values[i]);
    }
    return NeonValue::fromObject(newdict);
}

NeonValue nn_memberfunc_dict_compact(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjDict* dict;
    NeonObjDict* newdict;
    NeonValue tmpvalue;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = nn_value_asdict(args->thisval);
    newdict = state->gcProtect(nn_object_makedict(state));
    for(i = 0; i < dict->names->m_count; i++)
    {
        dict->htab->get(dict->names->values[i], &tmpvalue);
        if(!nn_value_compare(state, tmpvalue, NeonValue::makeNull()))
        {
            nn_dict_addentry(newdict, dict->names->values[i], tmpvalue);
        }
    }
    return NeonValue::fromObject(newdict);
}

NeonValue nn_memberfunc_dict_contains(NeonState* state, NeonArguments* args)
{
    NeonValue value;
    (void)state;
    NeonObjDict* dict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = nn_value_asdict(args->thisval);
    return NeonValue::makeBool(dict->htab->get(args->argv[0], &value));
}

NeonValue nn_memberfunc_dict_extend(NeonState* state, NeonArguments* args)
{
    int i;
    NeonValue tmp;
    NeonObjDict* dict;
    NeonObjDict* dictcpy;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isDict);
    dict = nn_value_asdict(args->thisval);
    dictcpy = nn_value_asdict(args->argv[0]);
    for(i = 0; i < dictcpy->names->m_count; i++)
    {
        if(!dict->htab->get(dictcpy->names->values[i], &tmp))
        {
            dict->names->push(dictcpy->names->values[i]);
        }
    }
    NeonHashTable::addAll(dictcpy->htab, dict->htab);
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_dict_get(NeonState* state, NeonArguments* args)
{
    NeonObjDict* dict;
    NeonProperty* field;
    (void)state;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = nn_value_asdict(args->thisval);
    field = nn_dict_getentry(dict, args->argv[0]);
    if(field == NULL)
    {
        if(args->argc == 1)
        {
            return NeonValue::makeNull();
        }
        else
        {
            return args->argv[1];
        }
    }
    return field->value;
}

NeonValue nn_memberfunc_dict_keys(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjDict* dict;
    NeonObjArray* list;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = nn_value_asdict(args->thisval);
    list = state->gcProtect(NeonObjArray::make(state));
    for(i = 0; i < dict->names->m_count; i++)
    {
        list->push(dict->names->values[i]);
    }
    return NeonValue::fromObject(list);
}

NeonValue nn_memberfunc_dict_values(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjDict* dict;
    NeonObjArray* list;
    NeonProperty* field;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = nn_value_asdict(args->thisval);
    list = state->gcProtect(NeonObjArray::make(state));
    for(i = 0; i < dict->names->m_count; i++)
    {
        field = nn_dict_getentry(dict, dict->names->values[i]);
        list->push(field->value);
    }
    return NeonValue::fromObject(list);
}

NeonValue nn_memberfunc_dict_remove(NeonState* state, NeonArguments* args)
{
    int i;
    int index;
    NeonValue value;
    NeonObjDict* dict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    ENFORCE_VALID_DICT_KEY(args, 0);
    dict = nn_value_asdict(args->thisval);
    if(dict->htab->get(args->argv[0], &value))
    {
        dict->htab->removeByKey(args->argv[0]);
        index = -1;
        for(i = 0; i < dict->names->m_count; i++)
        {
            if(nn_value_compare(state, dict->names->values[i], args->argv[0]))
            {
                index = i;
                break;
            }
        }
        for(i = index; i < dict->names->m_count; i++)
        {
            dict->names->values[i] = dict->names->values[i + 1];
        }
        dict->names->m_count--;
        return value;
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_dict_isempty(NeonState* state, NeonArguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return NeonValue::makeBool(nn_value_asdict(args->thisval)->names->m_count == 0);
}

NeonValue nn_memberfunc_dict_findkey(NeonState* state, NeonArguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    return nn_value_asdict(args->thisval)->htab->findKey(args->argv[0]);
}

NeonValue nn_memberfunc_dict_tolist(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjArray* list;
    NeonObjDict* dict;
    NeonObjArray* namelist;
    NeonObjArray* valuelist;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = nn_value_asdict(args->thisval);
    namelist = state->gcProtect(NeonObjArray::make(state));
    valuelist = state->gcProtect(NeonObjArray::make(state));
    for(i = 0; i < dict->names->m_count; i++)
    {
        namelist->push(dict->names->values[i]);
        NeonValue value;
        if(dict->htab->get(dict->names->values[i], &value))
        {
            valuelist->push(value);
        }
        else
        {
            /* theoretically impossible */
            valuelist->push(NeonValue::makeNull());
        }
    }
    list = state->gcProtect(NeonObjArray::make(state));
    list->push(NeonValue::fromObject(namelist));
    list->push(NeonValue::fromObject(valuelist));
    return NeonValue::fromObject(list);
}

NeonValue nn_memberfunc_dict_iter(NeonState* state, NeonArguments* args)
{
    NeonValue result;
    NeonObjDict* dict;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    dict = nn_value_asdict(args->thisval);
    if(dict->htab->get(args->argv[0], &result))
    {
        return result;
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_dict_itern(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjDict* dict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    dict = nn_value_asdict(args->thisval);
    if(args->argv[0].isNull())
    {
        if(dict->names->m_count == 0)
        {
            return NeonValue::makeBool(false);
        }
        return dict->names->values[0];
    }
    for(i = 0; i < dict->names->m_count; i++)
    {
        if(nn_value_compare(state, args->argv[0], dict->names->values[i]) && (i + 1) < dict->names->m_count)
        {
            return dict->names->values[i + 1];
        }
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_dict_each(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue value;
    NeonValue callable;
    NeonValue unused;
    NeonObjDict* dict;
    NeonObjArray* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = nn_value_asdict(args->thisval);
    callable = args->argv[0];
    nestargs = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < dict->names->m_count; i++)
    {
        if(arity > 0)
        {
            dict->htab->get(dict->names->values[i], &value);
            nestargs->varray->values[0] = value;
            if(arity > 1)
            {
                nestargs->varray->values[1] = dict->names->values[i];
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &unused);
    }
    /* pop the argument list */
    state->stackPop();
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_dict_filter(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue value;
    NeonValue callable;
    NeonValue result;
    NeonObjDict* dict;
    NeonObjArray* nestargs;
    NeonObjDict* resultdict;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = nn_value_asdict(args->thisval);
    callable = args->argv[0];
    nestargs = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    resultdict = state->gcProtect(nn_object_makedict(state));
    for(i = 0; i < dict->names->m_count; i++)
    {
        dict->htab->get(dict->names->values[i], &value);
        if(arity > 0)
        {
            nestargs->varray->values[0] = value;
            if(arity > 1)
            {
                nestargs->varray->values[1] = dict->names->values[i];
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
        if(!nn_value_isfalse(result))
        {
            nn_dict_addentry(resultdict, dict->names->values[i], value);
        }
    }
    /* pop the call list */
    state->stackPop();
    return NeonValue::fromObject(resultdict);
}

NeonValue nn_memberfunc_dict_some(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue result;
    NeonValue value;
    NeonValue callable;
    NeonObjDict* dict;
    NeonObjArray* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = nn_value_asdict(args->thisval);
    callable = args->argv[0];
    nestargs = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < dict->names->m_count; i++)
    {
        if(arity > 0)
        {
            dict->htab->get(dict->names->values[i], &value);
            nestargs->varray->values[0] = value;
            if(arity > 1)
            {
                nestargs->varray->values[1] = dict->names->values[i];
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
        if(!nn_value_isfalse(result))
        {
            /* pop the call list */
            state->stackPop();
            return NeonValue::makeBool(true);
        }
    }
    /* pop the call list */
    state->stackPop();
    return NeonValue::makeBool(false);
}


NeonValue nn_memberfunc_dict_every(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue value;
    NeonValue callable;  
    NeonValue result;
    NeonObjDict* dict;
    NeonObjArray* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = nn_value_asdict(args->thisval);
    callable = args->argv[0];
    nestargs = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < dict->names->m_count; i++)
    {
        if(arity > 0)
        {
            dict->htab->get(dict->names->values[i], &value);
            nestargs->varray->values[0] = value;
            if(arity > 1)
            {
                nestargs->varray->values[1] = dict->names->values[i];
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
        if(nn_value_isfalse(result))
        {
            /* pop the call list */
            state->stackPop();
            return NeonValue::makeBool(false);
        }
    }
    /* pop the call list */
    state->stackPop();
    return NeonValue::makeBool(true);
}

NeonValue nn_memberfunc_dict_reduce(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    int startindex;
    NeonValue value;
    NeonValue callable;
    NeonValue accumulator;
    NeonObjDict* dict;
    NeonObjArray* nestargs;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    dict = nn_value_asdict(args->thisval);
    callable = args->argv[0];
    startindex = 0;
    accumulator = NeonValue::makeNull();
    if(args->argc == 2)
    {
        accumulator = args->argv[1];
    }
    if(accumulator.isNull() && dict->names->m_count > 0)
    {
        dict->htab->get(dict->names->values[0], &accumulator);
        startindex = 1;
    }
    nestargs = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = startindex; i < dict->names->m_count; i++)
    {
        /* only call map for non-empty values in a list. */
        if(!dict->names->values[i].isNull() && !dict->names->values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs->varray->values[0] = accumulator;
                if(arity > 1)
                {
                    dict->htab->get(dict->names->values[i], &value);
                    nestargs->varray->values[1] = value;
                    if(arity > 2)
                    {
                        nestargs->varray->values[2] = dict->names->values[i];
                        if(arity > 4)
                        {
                            nestargs->varray->values[3] = args->thisval;
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
    NEON_RETURNERROR(#type " -> %s", message, file->path->data());

#define RETURN_STATUS(status) \
    if((status) == 0) \
    { \
        return NeonValue::makeBool(true); \
    } \
    else \
    { \
        FILE_ERROR(File, strerror(errno)); \
    }

#define DENY_STD() \
    if(file->isstd) \
    NEON_RETURNERROR("method not supported for std files");

int nn_fileobject_close(NeonObjFile* file)
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

bool nn_fileobject_open(NeonObjFile* file)
{
    if(file->handle != NULL)
    {
        return true;
    }
    if(file->handle == NULL && !file->isstd)
    {
        file->handle = fopen(file->path->data(), file->mode->data());
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

NeonValue nn_memberfunc_file_constructor(NeonState* state, NeonArguments* args)
{
    FILE* hnd;
    const char* path;
    const char* mode;
    NeonObjString* opath;
    NeonObjFile* file;
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
    file = state->gcProtect(nn_object_makefile(state, NULL, false, path, mode));
    nn_fileobject_open(file);
    return NeonValue::fromObject(file);
}

NeonValue nn_memberfunc_file_exists(NeonState* state, NeonArguments* args)
{
    NeonObjString* file;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    file = args->argv[0].asString();
    return NeonValue::makeBool(nn_util_fsfileexists(state, file->data()));
}


NeonValue nn_memberfunc_file_isfile(NeonState* state, NeonArguments* args)
{
    NeonObjString* file;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    file = args->argv[0].asString();
    return NeonValue::makeBool(nn_util_fsfileisfile(state, file->data()));
}

NeonValue nn_memberfunc_file_isdirectory(NeonState* state, NeonArguments* args)
{
    NeonObjString* file;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    file = args->argv[0].asString();
    return NeonValue::makeBool(nn_util_fsfileisdirectory(state, file->data()));
}

NeonValue nn_memberfunc_file_close(NeonState* state, NeonArguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    nn_fileobject_close(nn_value_asfile(args->thisval));
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_file_open(NeonState* state, NeonArguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    nn_fileobject_open(nn_value_asfile(args->thisval));
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_file_isopen(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    (void)state;
    file = nn_value_asfile(args->thisval);
    return NeonValue::makeBool(file->isstd || file->isopen);
}

NeonValue nn_memberfunc_file_isclosed(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    (void)state;
    file = nn_value_asfile(args->thisval);
    return NeonValue::makeBool(!file->isstd && !file->isopen);
}

bool nn_file_read(NeonState* state, NeonObjFile* file, size_t readhowmuch, NeonIOResult* dest)
{
    size_t filesizereal;
    struct stat stats;
    filesizereal = -1;
    dest->success = false;
    dest->length = 0;
    dest->data = NULL;
    if(!file->isstd)
    {
        if(!nn_util_fsfileexists(state, file->path->data()))
        {
            return false;
        }
        /* file is in write only mode */
        /*
        else if(strstr(file->mode->data(), "w") != NULL && strstr(file->mode->data(), "+") == NULL)
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
        if(osfn_lstat(file->path->data(), &stats) == 0)
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
    dest->data = (char*)NeonState::GC::allocate(state, sizeof(char), readhowmuch + 1);
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

NeonValue nn_memberfunc_file_read(NeonState* state, NeonArguments* args)
{
    size_t readhowmuch;
    NeonIOResult res;
    NeonObjFile* file;
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
    return NeonValue::fromObject(NeonObjString::take(state, res.data, res.length));
}

NeonValue nn_memberfunc_file_get(NeonState* state, NeonArguments* args)
{
    int ch;
    NeonObjFile* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
    ch = fgetc(file->handle);
    if(ch == EOF)
    {
        return NeonValue::makeNull();
    }
    return NeonValue::makeNumber(ch);
}

NeonValue nn_memberfunc_file_gets(NeonState* state, NeonArguments* args)
{
    long end;
    long length;
    long currentpos;
    size_t bytesread;
    char* buffer;
    NeonObjFile* file;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    length = -1;
    if(args->argc == 1)
    {
        NEON_ARGS_CHECKTYPE(args, 0, isNumber);
        length = (size_t)args->argv[0].asNumber();
    }
    file = nn_value_asfile(args->thisval);
    if(!file->isstd)
    {
        if(!nn_util_fsfileexists(state, file->path->data()))
        {
            FILE_ERROR(NotFound, "no such file or directory");
        }
        else if(strstr(file->mode->data(), "w") != NULL && strstr(file->mode->data(), "+") == NULL)
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
    buffer = (char*)NeonState::GC::allocate(state, sizeof(char), length + 1);
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
    return NeonValue::fromObject(NeonObjString::take(state, buffer, bytesread));
}

NeonValue nn_memberfunc_file_write(NeonState* state, NeonArguments* args)
{
    size_t count;
    int length;
    unsigned char* data;
    NeonObjFile* file;
    NeonObjString* string;
    NEON_ARGS_CHECKCOUNT(args, 1);
    file = nn_value_asfile(args->thisval);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->argv[0].asString();
    data = (unsigned char*)string->data();
    length = string->length();
    if(!file->isstd)
    {
        if(strstr(file->mode->data(), "r") != NULL && strstr(file->mode->data(), "+") == NULL)
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
        return NeonValue::makeBool(true);
    }
    return NeonValue::makeBool(false);
}

NeonValue nn_memberfunc_file_puts(NeonState* state, NeonArguments* args)
{
    size_t count;
    int length;
    unsigned char* data;
    NeonObjFile* file;
    NeonObjString* string;
    NEON_ARGS_CHECKCOUNT(args, 1);
    file = nn_value_asfile(args->thisval);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->argv[0].asString();
    data = (unsigned char*)string->data();
    length = string->length();
    if(!file->isstd)
    {
        if(strstr(file->mode->data(), "r") != NULL && strstr(file->mode->data(), "+") == NULL)
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
        return NeonValue::makeBool(true);
    }
    return NeonValue::makeBool(false);
}

NeonValue nn_memberfunc_file_printf(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    NeonFormatInfo nfi;
    NeonPrinter pd;
    NeonObjString* ofmt;
    file = nn_value_asfile(args->thisval);
    NEON_ARGS_CHECKMINARG(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    ofmt = args->argv[0].asString();
    NeonPrinter::makeStackIO(state, &pd, file->handle, false);
    nfi.init(state, &pd, ofmt->cstr(), ofmt->length());
    if(!nfi.format(args->argc, 1, args->argv))
    {
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_file_number(NeonState* state, NeonArguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return NeonValue::makeNumber(nn_value_asfile(args->thisval)->number);
}

NeonValue nn_memberfunc_file_istty(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
    return NeonValue::makeBool(file->istty);
}

NeonValue nn_memberfunc_file_flush(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
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
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_file_stats(NeonState* state, NeonArguments* args)
{
    struct stat stats;
    NeonObjFile* file;
    NeonObjDict* dict;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
    dict = state->gcProtect(nn_object_makedict(state));
    if(!file->isstd)
    {
        if(nn_util_fsfileexists(state, file->path->data()))
        {
            if(osfn_lstat(file->path->data(), &stats) == 0)
            {
                #if !defined(NEON_PLAT_ISWINDOWS)
                nn_dict_addentrycstr(dict, "isreadable", NeonValue::makeBool(((stats.st_mode & S_IRUSR) != 0)));
                nn_dict_addentrycstr(dict, "iswritable", NeonValue::makeBool(((stats.st_mode & S_IWUSR) != 0)));
                nn_dict_addentrycstr(dict, "isexecutable", NeonValue::makeBool(((stats.st_mode & S_IXUSR) != 0)));
                nn_dict_addentrycstr(dict, "issymbolic", NeonValue::makeBool((S_ISLNK(stats.st_mode) != 0)));
                #else
                nn_dict_addentrycstr(dict, "isreadable", NeonValue::makeBool(((stats.st_mode & S_IREAD) != 0)));
                nn_dict_addentrycstr(dict, "iswritable", NeonValue::makeBool(((stats.st_mode & S_IWRITE) != 0)));
                nn_dict_addentrycstr(dict, "isexecutable", NeonValue::makeBool(((stats.st_mode & S_IEXEC) != 0)));
                nn_dict_addentrycstr(dict, "issymbolic", NeonValue::makeBool(false));
                #endif
                nn_dict_addentrycstr(dict, "size", NeonValue::makeNumber(stats.st_size));
                nn_dict_addentrycstr(dict, "mode", NeonValue::makeNumber(stats.st_mode));
                nn_dict_addentrycstr(dict, "dev", NeonValue::makeNumber(stats.st_dev));
                nn_dict_addentrycstr(dict, "ino", NeonValue::makeNumber(stats.st_ino));
                nn_dict_addentrycstr(dict, "nlink", NeonValue::makeNumber(stats.st_nlink));
                nn_dict_addentrycstr(dict, "uid", NeonValue::makeNumber(stats.st_uid));
                nn_dict_addentrycstr(dict, "gid", NeonValue::makeNumber(stats.st_gid));
                nn_dict_addentrycstr(dict, "mtime", NeonValue::makeNumber(stats.st_mtime));
                nn_dict_addentrycstr(dict, "atime", NeonValue::makeNumber(stats.st_atime));
                nn_dict_addentrycstr(dict, "ctime", NeonValue::makeNumber(stats.st_ctime));
                nn_dict_addentrycstr(dict, "blocks", NeonValue::makeNumber(0));
                nn_dict_addentrycstr(dict, "blksize", NeonValue::makeNumber(0));
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
            nn_dict_addentrycstr(dict, "isreadable", NeonValue::makeBool(true));
            nn_dict_addentrycstr(dict, "iswritable", NeonValue::makeBool(false));
        }
        else
        {
            nn_dict_addentrycstr(dict, "isreadable", NeonValue::makeBool(false));
            nn_dict_addentrycstr(dict, "iswritable", NeonValue::makeBool(true));
        }
        nn_dict_addentrycstr(dict, "isexecutable", NeonValue::makeBool(false));
        nn_dict_addentrycstr(dict, "size", NeonValue::makeNumber(1));
    }
    return NeonValue::fromObject(dict);
}

NeonValue nn_memberfunc_file_path(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
    DENY_STD();
    return NeonValue::fromObject(file->path);
}

NeonValue nn_memberfunc_file_mode(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
    return NeonValue::fromObject(file->mode);
}

NeonValue nn_memberfunc_file_name(NeonState* state, NeonArguments* args)
{
    char* name;
    NeonObjFile* file;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
    if(!file->isstd)
    {
        name = nn_util_fsgetbasename(state, file->path->data());
        return NeonValue::fromObject(NeonObjString::copy(state, name));
    }
    else if(file->istty)
    {
        /*name = ttyname(file->number);*/
        name = nn_util_strdup(state, "<tty>");
        if(name)
        {
            return NeonValue::fromObject(NeonObjString::copy(state, name));
        }
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_file_seek(NeonState* state, NeonArguments* args)
{
    long position;
    int seektype;
    NeonObjFile* file;
    NEON_ARGS_CHECKCOUNT(args, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    NEON_ARGS_CHECKTYPE(args, 1, isNumber);
    file = nn_value_asfile(args->thisval);
    DENY_STD();
    position = (long)args->argv[0].asNumber();
    seektype = args->argv[1].asNumber();
    RETURN_STATUS(fseek(file->handle, position, seektype));
}

NeonValue nn_memberfunc_file_tell(NeonState* state, NeonArguments* args)
{
    NeonObjFile* file;
    NEON_ARGS_CHECKCOUNT(args, 0);
    file = nn_value_asfile(args->thisval);
    DENY_STD();
    return NeonValue::makeNumber(ftell(file->handle));
}

#undef FILE_ERROR
#undef RETURN_STATUS
#undef DENY_STD




NeonValue nn_memberfunc_array_length(NeonState* state, NeonArguments* args)
{
    NeonObjArray* selfarr;
    (void)state;
    selfarr = args->thisval.asArray();
    return NeonValue::makeNumber(selfarr->varray->m_count);
}

NeonValue nn_memberfunc_array_append(NeonState* state, NeonArguments* args)
{
    int i;
    (void)state;
    for(i = 0; i < args->argc; i++)
    {
        args->thisval.asArray()->push(args->argv[i]);
    }
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_array_clear(NeonState* state, NeonArguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    delete args->thisval.asArray()->varray;
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_array_clone(NeonState* state, NeonArguments* args)
{
    NeonObjArray* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    return NeonValue::fromObject(list->copy(0, list->varray->m_count));
}

NeonValue nn_memberfunc_array_count(NeonState* state, NeonArguments* args)
{
    int i;
    int count;
    NeonObjArray* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    count = 0;
    for(i = 0; i < list->varray->m_count; i++)
    {
        if(nn_value_compare(state, list->varray->values[i], args->argv[0]))
        {
            count++;
        }
    }
    return NeonValue::makeNumber(count);
}

NeonValue nn_memberfunc_array_extend(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjArray* list;
    NeonObjArray* list2;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isArray);
    list = args->thisval.asArray();
    list2 = args->argv[0].asArray();
    for(i = 0; i < list2->varray->m_count; i++)
    {
        list->push(list2->varray->values[i]);
    }
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_array_indexof(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjArray* list;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    list = args->thisval.asArray();
    i = 0;
    if(args->argc == 2)
    {
        NEON_ARGS_CHECKTYPE(args, 1, isNumber);
        i = args->argv[1].asNumber();
    }
    for(; i < list->varray->m_count; i++)
    {
        if(nn_value_compare(state, list->varray->values[i], args->argv[0]))
        {
            return NeonValue::makeNumber(i);
        }
    }
    return NeonValue::makeNumber(-1);
}

NeonValue nn_memberfunc_array_insert(NeonState* state, NeonArguments* args)
{
    int index;
    NeonObjArray* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 2);
    NEON_ARGS_CHECKTYPE(args, 1, isNumber);
    list = args->thisval.asArray();
    index = (int)args->argv[1].asNumber();
    list->varray->insert(args->argv[0], index);
    return NeonValue::makeEmpty();
}


NeonValue nn_memberfunc_array_pop(NeonState* state, NeonArguments* args)
{
    NeonValue value;
    NeonObjArray* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    if(list->varray->m_count > 0)
    {
        value = list->varray->values[list->varray->m_count - 1];
        list->varray->m_count--;
        return value;
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_array_shift(NeonState* state, NeonArguments* args)
{
    int i;
    int j;
    int count;
    NeonObjArray* list;
    NeonObjArray* newlist;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    count = 1;
    if(args->argc == 1)
    {
        NEON_ARGS_CHECKTYPE(args, 0, isNumber);
        count = args->argv[0].asNumber();
    }
    list = args->thisval.asArray();
    if(count >= list->varray->m_count || list->varray->m_count == 1)
    {
        list->varray->m_count = 0;
        return NeonValue::makeNull();
    }
    else if(count > 0)
    {
        newlist = state->gcProtect(NeonObjArray::make(state));
        for(i = 0; i < count; i++)
        {
            newlist->push(list->varray->values[0]);
            for(j = 0; j < list->varray->m_count; j++)
            {
                list->varray->values[j] = list->varray->values[j + 1];
            }
            list->varray->m_count -= 1;
        }
        if(count == 1)
        {
            return newlist->varray->values[0];
        }
        else
        {
            return NeonValue::fromObject(newlist);
        }
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_array_removeat(NeonState* state, NeonArguments* args)
{
    int i;
    int index;
    NeonValue value;
    NeonObjArray* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    list = args->thisval.asArray();
    index = args->argv[0].asNumber();
    if(index < 0 || index >= list->varray->m_count)
    {
        NEON_RETURNERROR("list index %d out of range at remove_at()", index);
    }
    value = list->varray->values[index];
    for(i = index; i < list->varray->m_count - 1; i++)
    {
        list->varray->values[i] = list->varray->values[i + 1];
    }
    list->varray->m_count--;
    return value;
}

NeonValue nn_memberfunc_array_remove(NeonState* state, NeonArguments* args)
{
    int i;
    int index;
    NeonObjArray* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    index = -1;
    for(i = 0; i < list->varray->m_count; i++)
    {
        if(nn_value_compare(state, list->varray->values[i], args->argv[0]))
        {
            index = i;
            break;
        }
    }
    if(index != -1)
    {
        for(i = index; i < list->varray->m_count; i++)
        {
            list->varray->values[i] = list->varray->values[i + 1];
        }
        list->varray->m_count--;
    }
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_array_reverse(NeonState* state, NeonArguments* args)
{
    int fromtop;
    NeonObjArray* list;
    NeonObjArray* nlist;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    nlist = state->gcProtect(NeonObjArray::make(state));
    /* in-place reversal:*/
    /*
    int start = 0;
    int end = list->varray->m_count - 1;
    while (start < end)
    {
        NeonValue temp = list->varray->values[start];
        list->varray->values[start] = list->varray->values[end];
        list->varray->values[end] = temp;
        start++;
        end--;
    }
    */
    for(fromtop = list->varray->m_count - 1; fromtop >= 0; fromtop--)
    {
        nlist->push(list->varray->values[fromtop]);
    }
    return NeonValue::fromObject(nlist);
}

NeonValue nn_memberfunc_array_sort(NeonState* state, NeonArguments* args)
{
    NeonObjArray* list;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    nn_value_sortvalues(state, list->varray->values, list->varray->m_count);
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_array_contains(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjArray* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    for(i = 0; i < list->varray->m_count; i++)
    {
        if(nn_value_compare(state, args->argv[0], list->varray->values[i]))
        {
            return NeonValue::makeBool(true);
        }
    }
    return NeonValue::makeBool(false);
}

NeonValue nn_memberfunc_array_delete(NeonState* state, NeonArguments* args)
{
    int i;
    int idxupper;
    int idxlower;
    NeonObjArray* list;
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
    if(idxlower < 0 || idxlower >= list->varray->m_count)
    {
        NEON_RETURNERROR("list index %d out of range at delete()", idxlower);
    }
    else if(idxupper < idxlower || idxupper >= list->varray->m_count)
    {
        NEON_RETURNERROR("invalid upper limit %d at delete()", idxupper);
    }
    for(i = 0; i < list->varray->m_count - idxupper; i++)
    {
        list->varray->values[idxlower + i] = list->varray->values[i + idxupper + 1];
    }
    list->varray->m_count -= idxupper - idxlower + 1;
    return NeonValue::makeNumber((double)idxupper - (double)idxlower + 1);
}

NeonValue nn_memberfunc_array_first(NeonState* state, NeonArguments* args)
{
    NeonObjArray* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    if(list->varray->m_count > 0)
    {
        return list->varray->values[0];
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_array_last(NeonState* state, NeonArguments* args)
{
    NeonObjArray* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    if(list->varray->m_count > 0)
    {
        return list->varray->values[list->varray->m_count - 1];
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_array_isempty(NeonState* state, NeonArguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return NeonValue::makeBool(args->thisval.asArray()->varray->m_count == 0);
}


NeonValue nn_memberfunc_array_take(NeonState* state, NeonArguments* args)
{
    int count;
    NeonObjArray* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    list = args->thisval.asArray();
    count = args->argv[0].asNumber();
    if(count < 0)
    {
        count = list->varray->m_count + count;
    }
    if(list->varray->m_count < count)
    {
        return NeonValue::fromObject(list->copy(0, list->varray->m_count));
    }
    return NeonValue::fromObject(list->copy(0, count));
}

NeonValue nn_memberfunc_array_get(NeonState* state, NeonArguments* args)
{
    int index;
    NeonObjArray* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    list = args->thisval.asArray();
    index = args->argv[0].asNumber();
    if(index < 0 || index >= list->varray->m_count)
    {
        return NeonValue::makeNull();
    }
    return list->varray->values[index];
}

NeonValue nn_memberfunc_array_compact(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjArray* list;
    NeonObjArray* newlist;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    newlist = state->gcProtect(NeonObjArray::make(state));
    for(i = 0; i < list->varray->m_count; i++)
    {
        if(!nn_value_compare(state, list->varray->values[i], NeonValue::makeNull()))
        {
            newlist->push(list->varray->values[i]);
        }
    }
    return NeonValue::fromObject(newlist);
}


NeonValue nn_memberfunc_array_unique(NeonState* state, NeonArguments* args)
{
    int i;
    int j;
    bool found;
    NeonObjArray* list;
    NeonObjArray* newlist;
    NEON_ARGS_CHECKCOUNT(args, 0);
    list = args->thisval.asArray();
    newlist = state->gcProtect(NeonObjArray::make(state));
    for(i = 0; i < list->varray->m_count; i++)
    {
        found = false;
        for(j = 0; j < newlist->varray->m_count; j++)
        {
            if(nn_value_compare(state, newlist->varray->values[j], list->varray->values[i]))
            {
                found = true;
                continue;
            }
        }
        if(!found)
        {
            newlist->push(list->varray->values[i]);
        }
    }
    return NeonValue::fromObject(newlist);
}

NeonValue nn_memberfunc_array_zip(NeonState* state, NeonArguments* args)
{
    int i;
    int j;
    NeonObjArray* list;
    NeonObjArray* newlist;
    NeonObjArray* alist;
    NeonObjArray** arglist;
    list = args->thisval.asArray();
    newlist = state->gcProtect(NeonObjArray::make(state));
    arglist = (NeonObjArray**)NeonState::GC::allocate(state, sizeof(NeonObjArray*), args->argc);
    for(i = 0; i < args->argc; i++)
    {
        NEON_ARGS_CHECKTYPE(args, i, isArray);
        arglist[i] = args->argv[i].asArray();
    }
    for(i = 0; i < list->varray->m_count; i++)
    {
        alist = state->gcProtect(NeonObjArray::make(state));
        /* item of main list*/
        alist->push(list->varray->values[i]);
        for(j = 0; j < args->argc; j++)
        {
            if(i < arglist[j]->varray->m_count)
            {
                alist->push(arglist[j]->varray->values[i]);
            }
            else
            {
                alist->push(NeonValue::makeNull());
            }
        }
        newlist->push(NeonValue::fromObject(alist));
    }
    return NeonValue::fromObject(newlist);
}


NeonValue nn_memberfunc_array_zipfrom(NeonState* state, NeonArguments* args)
{
    int i;
    int j;
    NeonObjArray* list;
    NeonObjArray* newlist;
    NeonObjArray* alist;
    NeonObjArray* arglist;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isArray);
    list = args->thisval.asArray();
    newlist = state->gcProtect(NeonObjArray::make(state));
    arglist = args->argv[0].asArray();
    for(i = 0; i < arglist->varray->m_count; i++)
    {
        if(!arglist->varray->values[i].isArray())
        {
            NEON_RETURNERROR("invalid list in zip entries");
        }
    }
    for(i = 0; i < list->varray->m_count; i++)
    {
        alist = state->gcProtect(NeonObjArray::make(state));
        alist->push(list->varray->values[i]);
        for(j = 0; j < arglist->varray->m_count; j++)
        {
            if(i < arglist->varray->values[j].asArray()->varray->m_count)
            {
                alist->push(arglist->varray->values[j].asArray()->varray->values[i]);
            }
            else
            {
                alist->push(NeonValue::makeNull());
            }
        }
        newlist->push(NeonValue::fromObject(alist));
    }
    return NeonValue::fromObject(newlist);
}

NeonValue nn_memberfunc_array_todict(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjDict* dict;
    NeonObjArray* list;
    NEON_ARGS_CHECKCOUNT(args, 0);
    dict = state->gcProtect(nn_object_makedict(state));
    list = args->thisval.asArray();
    for(i = 0; i < list->varray->m_count; i++)
    {
        nn_dict_setentry(dict, NeonValue::makeNumber(i), list->varray->values[i]);
    }
    return NeonValue::fromObject(dict);
}

NeonValue nn_memberfunc_array_iter(NeonState* state, NeonArguments* args)
{
    int index;
    NeonObjArray* list;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    list = args->thisval.asArray();
    index = args->argv[0].asNumber();
    if(index > -1 && index < list->varray->m_count)
    {
        return list->varray->values[index];
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_array_itern(NeonState* state, NeonArguments* args)
{
    int index;
    NeonObjArray* list;
    NEON_ARGS_CHECKCOUNT(args, 1);
    list = args->thisval.asArray();
    if(args->argv[0].isNull())
    {
        if(list->varray->m_count == 0)
        {
            return NeonValue::makeBool(false);
        }
        return NeonValue::makeNumber(0);
    }
    if(!args->argv[0].isNumber())
    {
        NEON_RETURNERROR("lists are numerically indexed");
    }
    index = args->argv[0].asNumber();
    if(index < list->varray->m_count - 1)
    {
        return NeonValue::makeNumber((double)index + 1);
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_array_each(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue callable;
    NeonValue unused;
    NeonObjArray* list;
    NeonObjArray* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    list = args->thisval.asArray();
    callable = args->argv[0];
    nestargs = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < list->varray->m_count; i++)
    {
        if(arity > 0)
        {
            nestargs->varray->values[0] = list->varray->values[i];
            if(arity > 1)
            {
                nestargs->varray->values[1] = NeonValue::makeNumber(i);
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &unused);
    }
    state->stackPop();
    return NeonValue::makeEmpty();
}


NeonValue nn_memberfunc_array_map(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue res;
    NeonValue callable;
    NeonObjArray* list;
    NeonObjArray* nestargs;
    NeonObjArray* resultlist;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    list = args->thisval.asArray();
    callable = args->argv[0];
    nestargs = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    resultlist = state->gcProtect(NeonObjArray::make(state));
    for(i = 0; i < list->varray->m_count; i++)
    {
        if(!list->varray->values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs->varray->values[0] = list->varray->values[i];
                if(arity > 1)
                {
                    nestargs->varray->values[1] = NeonValue::makeNumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &res);
            resultlist->push(res);
        }
        else
        {
            resultlist->push(NeonValue::makeEmpty());
        }
    }
    state->stackPop();
    return NeonValue::fromObject(resultlist);
}


NeonValue nn_memberfunc_array_filter(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue callable;
    NeonValue result;
    NeonObjArray* list;
    NeonObjArray* nestargs;
    NeonObjArray* resultlist;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    list = args->thisval.asArray();
    callable = args->argv[0];
    nestargs = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    resultlist = state->gcProtect(NeonObjArray::make(state));
    for(i = 0; i < list->varray->m_count; i++)
    {
        if(!list->varray->values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs->varray->values[0] = list->varray->values[i];
                if(arity > 1)
                {
                    nestargs->varray->values[1] = NeonValue::makeNumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
            if(!nn_value_isfalse(result))
            {
                resultlist->push(list->varray->values[i]);
            }
        }
    }
    state->stackPop();
    return NeonValue::fromObject(resultlist);
}

NeonValue nn_memberfunc_array_some(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue callable;
    NeonValue result;
    NeonObjArray* list;
    NeonObjArray* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    list = args->thisval.asArray();
    callable = args->argv[0];
    nestargs = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < list->varray->m_count; i++)
    {
        if(!list->varray->values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs->varray->values[0] = list->varray->values[i];
                if(arity > 1)
                {
                    nestargs->varray->values[1] = NeonValue::makeNumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
            if(!nn_value_isfalse(result))
            {
                state->stackPop();
                return NeonValue::makeBool(true);
            }
        }
    }
    state->stackPop();
    return NeonValue::makeBool(false);
}


NeonValue nn_memberfunc_array_every(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue result;
    NeonValue callable;
    NeonObjArray* list;
    NeonObjArray* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    list = args->thisval.asArray();
    callable = args->argv[0];
    nestargs = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < list->varray->m_count; i++)
    {
        if(!list->varray->values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs->varray->values[0] = list->varray->values[i];
                if(arity > 1)
                {
                    nestargs->varray->values[1] = NeonValue::makeNumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &result);
            if(nn_value_isfalse(result))
            {
                state->stackPop();
                return NeonValue::makeBool(false);
            }
        }
    }
    state->stackPop();
    return NeonValue::makeBool(true);
}

NeonValue nn_memberfunc_array_reduce(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    int startindex;
    NeonValue callable;
    NeonValue accumulator;
    NeonObjArray* list;
    NeonObjArray* nestargs;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    list = args->thisval.asArray();
    callable = args->argv[0];
    startindex = 0;
    accumulator = NeonValue::makeNull();
    if(args->argc == 2)
    {
        accumulator = args->argv[1];
    }
    if(accumulator.isNull() && list->varray->m_count > 0)
    {
        accumulator = list->varray->values[0];
        startindex = 1;
    }
    nestargs = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = startindex; i < list->varray->m_count; i++)
    {
        if(!list->varray->values[i].isNull() && !list->varray->values[i].isEmpty())
        {
            if(arity > 0)
            {
                nestargs->varray->values[0] = accumulator;
                if(arity > 1)
                {
                    nestargs->varray->values[1] = list->varray->values[i];
                    if(arity > 2)
                    {
                        nestargs->varray->values[2] = NeonValue::makeNumber(i);
                        if(arity > 4)
                        {
                            nestargs->varray->values[3] = args->thisval;
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

NeonValue nn_memberfunc_range_lower(NeonState* state, NeonArguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return NeonValue::makeNumber(nn_value_asrange(args->thisval)->lower);
}

NeonValue nn_memberfunc_range_upper(NeonState* state, NeonArguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return NeonValue::makeNumber(nn_value_asrange(args->thisval)->upper);
}

NeonValue nn_memberfunc_range_range(NeonState* state, NeonArguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    return NeonValue::makeNumber(nn_value_asrange(args->thisval)->range);
}

NeonValue nn_memberfunc_range_iter(NeonState* state, NeonArguments* args)
{
    int val;
    int index;
    NeonObjRange* range;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    range = nn_value_asrange(args->thisval);
    index = args->argv[0].asNumber();
    if(index >= 0 && index < range->range)
    {
        if(index == 0)
        {
            return NeonValue::makeNumber(range->lower);
        }
        if(range->lower > range->upper)
        {
            val = --range->lower;
        }
        else
        {
            val = ++range->lower;
        }
        return NeonValue::makeNumber(val);
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_range_itern(NeonState* state, NeonArguments* args)
{
    int index;
    NeonObjRange* range;
    NEON_ARGS_CHECKCOUNT(args, 1);
    range = nn_value_asrange(args->thisval);
    if(args->argv[0].isNull())
    {
        if(range->range == 0)
        {
            return NeonValue::makeNull();
        }
        return NeonValue::makeNumber(0);
    }
    if(!args->argv[0].isNumber())
    {
        NEON_RETURNERROR("ranges are numerically indexed");
    }
    index = (int)args->argv[0].asNumber() + 1;
    if(index < range->range)
    {
        return NeonValue::makeNumber(index);
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_range_loop(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue callable;
    NeonValue unused;
    NeonObjRange* range;
    NeonObjArray* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    range = nn_value_asrange(args->thisval);
    callable = args->argv[0];
    nestargs = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < range->range; i++)
    {
        if(arity > 0)
        {
            nestargs->varray->values[0] = NeonValue::makeNumber(i);
            if(arity > 1)
            {
                nestargs->varray->values[1] = NeonValue::makeNumber(i);
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &unused);
    }
    state->stackPop();
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_range_expand(NeonState* state, NeonArguments* args)
{
    int i;
    NeonValue val;
    NeonObjRange* range;
    NeonObjArray* oa;
    range = nn_value_asrange(args->thisval);
    oa = NeonObjArray::make(state);
    for(i = 0; i < range->range; i++)
    {
        val = NeonValue::makeNumber(i);
        oa->push(val);
    }
    return NeonValue::fromObject(oa);
}

NeonValue nn_memberfunc_range_constructor(NeonState* state, NeonArguments* args)
{
    int a;
    int b;
    NeonObjRange* orng;
    a = args->argv[0].asNumber();
    b = args->argv[1].asNumber();
    orng = nn_object_makerange(state, a, b);
    return NeonValue::fromObject(orng);
}

NeonValue nn_memberfunc_string_fromcharcode(NeonState* state, NeonArguments* args)
{
    char ch;
    NeonObjString* os;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    ch = args->argv[0].asNumber();
    os = NeonObjString::copy(state, &ch, 1);
    return NeonValue::fromObject(os);
}

NeonValue nn_memberfunc_string_constructor(NeonState* state, NeonArguments* args)
{
    NeonObjString* os;
    NEON_ARGS_CHECKCOUNT(args, 0);
    os = NeonObjString::copy(state, "", 0);
    return NeonValue::fromObject(os);
}

NeonValue nn_memberfunc_string_length(NeonState* state, NeonArguments* args)
{
    NeonObjString* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    return NeonValue::makeNumber(selfstr->length());
}

NeonValue nn_memberfunc_string_substring(NeonState* state, NeonArguments* args)
{
    size_t end;
    size_t start;
    size_t maxlen;
    NeonObjString* nos;
    NeonObjString* selfstr;
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
    return NeonValue::fromObject(nos);
}

NeonValue nn_memberfunc_string_charcodeat(NeonState* state, NeonArguments* args)
{
    int ch;
    int idx;
    int selflen;
    NeonObjString* selfstr;
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
    return NeonValue::makeNumber(ch);
}

NeonValue nn_memberfunc_string_charat(NeonState* state, NeonArguments* args)
{
    char ch;
    int idx;
    int selflen;
    NeonObjString* selfstr;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    selfstr = args->thisval.asString();
    idx = args->argv[0].asNumber();
    selflen = (int)selfstr->length();
    if((idx < 0) || (idx >= selflen))
    {
        return NeonValue::fromObject(NeonObjString::copy(state, "", 0));
    }
    else
    {
        ch = selfstr->data()[idx];
    }
    return NeonValue::fromObject(NeonObjString::copy(state, &ch, 1));
}

NeonValue nn_memberfunc_string_upper(NeonState* state, NeonArguments* args)
{
    size_t slen;
    NeonObjString* str;
    NeonObjString* copied;
    NEON_ARGS_CHECKCOUNT(args, 0);
    str = args->thisval.asString();
    copied = NeonObjString::copy(state, str->data(), str->length());
    slen = copied->length();
    (void)nn_util_strtoupperinplace(copied->mutableData(), slen);
    return NeonValue::fromObject(copied);
}

NeonValue nn_memberfunc_string_lower(NeonState* state, NeonArguments* args)
{
    size_t slen;
    NeonObjString* str;
    NeonObjString* copied;
    NEON_ARGS_CHECKCOUNT(args, 0);
    str = args->thisval.asString();
    copied = NeonObjString::copy(state, str->data(), str->length());
    slen = copied->length();
    (void)nn_util_strtolowerinplace(copied->mutableData(), slen);
    return NeonValue::fromObject(copied);
}

NeonValue nn_memberfunc_string_isalpha(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjString* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    for(i = 0; i < (int)selfstr->length(); i++)
    {
        if(!isalpha((unsigned char)selfstr->data()[i]))
        {
            return NeonValue::makeBool(false);
        }
    }
    return NeonValue::makeBool(selfstr->length() != 0);
}

NeonValue nn_memberfunc_string_isalnum(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjString* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    for(i = 0; i < (int)selfstr->length(); i++)
    {
        if(!isalnum((unsigned char)selfstr->data()[i]))
        {
            return NeonValue::makeBool(false);
        }
    }
    return NeonValue::makeBool(selfstr->length() != 0);
}

NeonValue nn_memberfunc_string_isfloat(NeonState* state, NeonArguments* args)
{
    double f;
    char* p;
    NeonObjString* selfstr;
    (void)f;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    errno = 0;
    if(selfstr->length() ==0)
    {
        return NeonValue::makeBool(false);
    }
    f = strtod(selfstr->data(), &p);
    if(errno)
    {
        return NeonValue::makeBool(false);
    }
    else
    {
        if(*p == 0)
        {
            return NeonValue::makeBool(true);
        }
    }
    return NeonValue::makeBool(false);
}

NeonValue nn_memberfunc_string_isnumber(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjString* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    for(i = 0; i < (int)selfstr->length(); i++)
    {
        if(!isdigit((unsigned char)selfstr->data()[i]))
        {
            return NeonValue::makeBool(false);
        }
    }
    return NeonValue::makeBool(selfstr->length() != 0);
}

NeonValue nn_memberfunc_string_islower(NeonState* state, NeonArguments* args)
{
    int i;
    bool alphafound;
    NeonObjString* selfstr;
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
            return NeonValue::makeBool(false);
        }
    }
    return NeonValue::makeBool(alphafound);
}

NeonValue nn_memberfunc_string_isupper(NeonState* state, NeonArguments* args)
{
    int i;
    bool alphafound;
    NeonObjString* selfstr;
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
            return NeonValue::makeBool(false);
        }
    }
    return NeonValue::makeBool(alphafound);
}

NeonValue nn_memberfunc_string_isspace(NeonState* state, NeonArguments* args)
{
    int i;
    NeonObjString* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    for(i = 0; i < (int)selfstr->length(); i++)
    {
        if(!isspace((unsigned char)selfstr->data()[i]))
        {
            return NeonValue::makeBool(false);
        }
    }
    return NeonValue::makeBool(selfstr->length() != 0);
}

NeonValue nn_memberfunc_string_trim(NeonState* state, NeonArguments* args)
{
    char trimmer;
    char* end;
    char* string;
    NeonObjString* selfstr;
    NeonObjString* copied;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    trimmer = '\0';
    if(args->argc == 1)
    {
        trimmer = (char)args->argv[0].asString()->data()[0];
    }
    selfstr = args->thisval.asString();
    copied = NeonObjString::copy(state, selfstr->data(), selfstr->length());
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
        return NeonValue::fromObject(NeonObjString::copy(state, "", 0));
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
    return NeonValue::fromObject(NeonObjString::copy(state, string));
}

NeonValue nn_memberfunc_string_ltrim(NeonState* state, NeonArguments* args)
{
    char trimmer;
    char* end;
    char* string;
    NeonObjString* selfstr;
    NeonObjString* copied;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    trimmer = '\0';
    if(args->argc == 1)
    {
        trimmer = (char)args->argv[0].asString()->data()[0];
    }
    selfstr = args->thisval.asString();
    copied = NeonObjString::copy(state, selfstr->data(), selfstr->length());
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
        return NeonValue::fromObject(NeonObjString::copy(state, "", 0));
    }
    end = string + strlen(string) - 1;
    end[1] = '\0';
    return NeonValue::fromObject(NeonObjString::copy(state, string));
}

NeonValue nn_memberfunc_string_rtrim(NeonState* state, NeonArguments* args)
{
    char trimmer;
    char* end;
    char* string;
    NeonObjString* selfstr;
    NeonObjString* copied;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    trimmer = '\0';
    if(args->argc == 1)
    {
        trimmer = (char)args->argv[0].asString()->data()[0];
    }
    selfstr = args->thisval.asString();
    copied = NeonObjString::copy(state, selfstr->data(), selfstr->length());
    string = copied->mutableData();
    end = NULL;
    /* All spaces? */
    if(*string == 0)
    {
        return NeonValue::fromObject(NeonObjString::copy(state, "", 0));
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
    return NeonValue::fromObject(NeonObjString::copy(state, string));
}


NeonValue nn_memberfunc_array_constructor(NeonState* state, NeonArguments* args)
{
    int cnt;
    NeonValue filler;
    NeonObjArray* arr;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    filler = NeonValue::makeEmpty();
    if(args->argc > 1)
    {
        filler = args->argv[1];
    }
    cnt = args->argv[0].asNumber();
    arr = NeonObjArray::make(state, cnt, filler);
    return NeonValue::fromObject(arr);
}

NeonValue nn_memberfunc_array_join(NeonState* state, NeonArguments* args)
{
    int i;
    int count;
    NeonPrinter pd;
    NeonValue vjoinee;
    NeonObjArray* selfarr;
    NeonObjString* joinee;
    NeonValue* list;
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
    list = selfarr->varray->values;
    count = selfarr->varray->m_count;
    if(count == 0)
    {
        return NeonValue::fromObject(NeonObjString::copy(state, ""));
    }
    NeonPrinter::makeStackString(state, &pd);
    for(i = 0; i < count; i++)
    {
        nn_printer_printvalue(&pd, list[i], false, true);
        if((joinee != NULL) && ((i+1) < count))
        {
            pd.put(joinee->data(), joinee->length());
        }
    }
    return NeonValue::fromObject(pd.takeString());
}

NeonValue nn_memberfunc_string_indexof(NeonState* state, NeonArguments* args)
{
    int startindex;
    const char* result;
    const char* haystack;
    NeonObjString* string;
    NeonObjString* needle;
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
            return NeonValue::makeNumber((int)(result - haystack));
        }
    }
    return NeonValue::makeNumber(-1);
}

NeonValue nn_memberfunc_string_startswith(NeonState* state, NeonArguments* args)
{
    NeonObjString* substr;
    NeonObjString* string;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->thisval.asString();
    substr = args->argv[0].asString();
    if(string->length() == 0 || substr->length() == 0 || substr->length() > string->length())
    {
        return NeonValue::makeBool(false);
    }
    return NeonValue::makeBool(memcmp(substr->data(), string->data(), substr->length()) == 0);
}

NeonValue nn_memberfunc_string_endswith(NeonState* state, NeonArguments* args)
{
    int difference;
    NeonObjString* substr;
    NeonObjString* string;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->thisval.asString();
    substr = args->argv[0].asString();
    if(string->length() == 0 || substr->length() == 0 || substr->length() > string->length())
    {
        return NeonValue::makeBool(false);
    }
    difference = string->length() - substr->length();
    return NeonValue::makeBool(memcmp(substr->data(), string->data() + difference, substr->length()) == 0);
}

NeonValue nn_memberfunc_string_count(NeonState* state, NeonArguments* args)
{
    int count;
    const char* tmp;
    NeonObjString* substr;
    NeonObjString* string;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->thisval.asString();
    substr = args->argv[0].asString();
    if(substr->length() == 0 || string->length() == 0)
    {
        return NeonValue::makeNumber(0);
    }
    count = 0;
    tmp = string->data();
    while((tmp = nn_util_utf8strstr(tmp, string->length(), substr->data(), substr->length())))
    {
        count++;
        tmp++;
    }
    return NeonValue::makeNumber(count);
}

NeonValue nn_memberfunc_string_tonumber(NeonState* state, NeonArguments* args)
{
    NeonObjString* selfstr;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    selfstr = args->thisval.asString();
    return NeonValue::makeNumber(strtod(selfstr->data(), NULL));
}

NeonValue nn_memberfunc_string_isascii(NeonState* state, NeonArguments* args)
{
    NeonObjString* string;
    (void)state;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    if(args->argc == 1)
    {
        NEON_ARGS_CHECKTYPE(args, 0, isBool);
    }
    string = args->thisval.asString();
    return NeonValue::fromObject(string);
}

NeonValue nn_memberfunc_string_tolist(NeonState* state, NeonArguments* args)
{
    int i;
    int end;
    int start;
    int length;
    NeonObjArray* list;
    NeonObjString* string;
    NEON_ARGS_CHECKCOUNT(args, 0);
    string = args->thisval.asString();
    list = state->gcProtect(NeonObjArray::make(state));
    length = string->length();
    if(length > 0)
    {
        for(i = 0; i < length; i++)
        {
            start = i;
            end = i + 1;
            list->push(NeonValue::fromObject(NeonObjString::copy(state, string->data() + start, (int)(end - start))));
        }
    }
    return NeonValue::fromObject(list);
}

// TODO: lpad and rpad modify m_sbuf members!!!
NeonValue nn_memberfunc_string_lpad(NeonState* state, NeonArguments* args)
{
    int i;
    int width;
    int fillsize;
    int finalsize;
    int finalutf8size;
    char fillchar;
    char* str;
    char* fill;
    NeonObjString* ofillstr;
    NeonObjString* result;
    NeonObjString* string;
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
    fill = (char*)NeonState::GC::allocate(state, sizeof(char), (size_t)fillsize + 1);
    finalsize = string->m_sbuf->length + fillsize;
    finalutf8size = string->m_sbuf->length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)NeonState::GC::allocate(state, sizeof(char), (size_t)finalsize + 1);
    memcpy(str, fill, fillsize);
    memcpy(str + fillsize, string->m_sbuf->data, string->m_sbuf->length);
    str[finalsize] = '\0';
    nn_gcmem_freearray(state, sizeof(char), fill, fillsize + 1);
    result = NeonObjString::take(state, str, finalsize);
    result->m_sbuf->length = finalutf8size;
    result->m_sbuf->length = finalsize;
    return NeonValue::fromObject(result);
}

NeonValue nn_memberfunc_string_rpad(NeonState* state, NeonArguments* args)
{
    int i;
    int width;
    int fillsize;
    int finalsize;
    int finalutf8size;
    char fillchar;
    char* str;
    char* fill;
    NeonObjString* ofillstr;
    NeonObjString* string;
    NeonObjString* result;
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
    fill = (char*)NeonState::GC::allocate(state, sizeof(char), (size_t)fillsize + 1);
    finalsize = string->m_sbuf->length + fillsize;
    finalutf8size = string->m_sbuf->length + fillsize;
    for(i = 0; i < fillsize; i++)
    {
        fill[i] = fillchar;
    }
    str = (char*)NeonState::GC::allocate(state, sizeof(char), (size_t)finalsize + 1);
    memcpy(str, string->m_sbuf->data, string->m_sbuf->length);
    memcpy(str + string->m_sbuf->length, fill, fillsize);
    str[finalsize] = '\0';
    nn_gcmem_freearray(state, sizeof(char), fill, fillsize + 1);
    result = NeonObjString::take(state, str, finalsize);
    result->m_sbuf->length = finalutf8size;
    result->m_sbuf->length = finalsize;
    return NeonValue::fromObject(result);
}

NeonValue nn_memberfunc_string_split(NeonState* state, NeonArguments* args)
{
    int i;
    int end;
    int start;
    int length;
    NeonObjArray* list;
    NeonObjString* string;
    NeonObjString* delimeter;
    NEON_ARGS_CHECKCOUNTRANGE(args, 1, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    string = args->thisval.asString();
    delimeter = args->argv[0].asString();
    /* empty string matches empty string to empty list */
    if(((string->length() == 0) && (delimeter->length() == 0)) || (string->length() == 0) || (delimeter->length() == 0))
    {
        return NeonValue::fromObject(NeonObjArray::make(state));
    }
    list = state->gcProtect(NeonObjArray::make(state));
    if(delimeter->length() > 0)
    {
        start = 0;
        for(i = 0; i <= (int)string->length(); i++)
        {
            /* match found. */
            if(memcmp(string->data() + i, delimeter->data(), delimeter->length()) == 0 || i == (int)string->length())
            {
                list->push(NeonValue::fromObject(NeonObjString::copy(state, string->data() + start, i - start)));
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
            list->push(NeonValue::fromObject(NeonObjString::copy(state, string->data() + start, (int)(end - start))));
        }
    }
    return NeonValue::fromObject(list);
}


NeonValue nn_memberfunc_string_replace(NeonState* state, NeonArguments* args)
{
    int i;
    int totallength;
    StringBuffer* result;
    NeonObjString* substr;
    NeonObjString* string;
    NeonObjString* repsubstr;
    NEON_ARGS_CHECKCOUNTRANGE(args, 2, 3);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    NEON_ARGS_CHECKTYPE(args, 1, isString);
    string = args->thisval.asString();
    substr = args->argv[0].asString();
    repsubstr = args->argv[1].asString();
    if((string->length() == 0 && substr->length() == 0) || string->length() == 0 || substr->length() == 0)
    {
        return NeonValue::fromObject(NeonObjString::copy(state, string->data(), string->length()));
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
    return NeonValue::fromObject(NeonObjString::makeFromStrbuf(state, result, nn_util_hashstring(result->data, result->length)));
}

NeonValue nn_memberfunc_string_iter(NeonState* state, NeonArguments* args)
{
    int index;
    int length;
    NeonObjString* string;
    NeonObjString* result;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    string = args->thisval.asString();
    length = string->length();
    index = args->argv[0].asNumber();
    if(index > -1 && index < length)
    {
        result = NeonObjString::copy(state, &string->data()[index], 1);
        return NeonValue::fromObject(result);
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_string_itern(NeonState* state, NeonArguments* args)
{
    int index;
    int length;
    NeonObjString* string;
    NEON_ARGS_CHECKCOUNT(args, 1);
    string = args->thisval.asString();
    length = string->length();
    if(args->argv[0].isNull())
    {
        if(length == 0)
        {
            return NeonValue::makeBool(false);
        }
        return NeonValue::makeNumber(0);
    }
    if(!args->argv[0].isNumber())
    {
        NEON_RETURNERROR("strings are numerically indexed");
    }
    index = args->argv[0].asNumber();
    if(index < length - 1)
    {
        return NeonValue::makeNumber((double)index + 1);
    }
    return NeonValue::makeNull();
}

NeonValue nn_memberfunc_string_each(NeonState* state, NeonArguments* args)
{
    int i;
    int arity;
    NeonValue callable;
    NeonValue unused;
    NeonObjString* string;
    NeonObjArray* nestargs;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isCallable);
    string = args->thisval.asString();
    callable = args->argv[0];
    nestargs = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(nestargs));
    arity = nn_nestcall_prepare(state, callable, args->thisval, nestargs);
    for(i = 0; i < (int)string->length(); i++)
    {
        if(arity > 0)
        {
            nestargs->varray->values[0] = NeonValue::fromObject(NeonObjString::copy(state, string->data() + i, 1));
            if(arity > 1)
            {
                nestargs->varray->values[1] = NeonValue::makeNumber(i);
            }
        }
        nn_nestcall_callfunction(state, callable, args->thisval, nestargs, &unused);
    }
    /* pop the argument list */
    state->stackPop();
    return NeonValue::makeEmpty();
}

NeonValue nn_memberfunc_object_dump(NeonState* state, NeonArguments* args)
{
    NeonValue v;
    NeonPrinter pd;
    NeonObjString* os;
    v = args->thisval;
    NeonPrinter::makeStackString(state, &pd);
    nn_printer_printvalue(&pd, v, true, false);
    os = pd.takeString();
    return NeonValue::fromObject(os);
}

NeonValue nn_memberfunc_object_tostring(NeonState* state, NeonArguments* args)
{
    NeonValue v;
    NeonPrinter pd;
    NeonObjString* os;
    v = args->thisval;
    NeonPrinter::makeStackString(state, &pd);
    nn_printer_printvalue(&pd, v, false, true);
    os = pd.takeString();
    return NeonValue::fromObject(os);
}

NeonValue nn_memberfunc_object_typename(NeonState* state, NeonArguments* args)
{
    NeonValue v;
    NeonObjString* os;
    v = args->argv[0];
    os = NeonObjString::copy(state, nn_value_typename(v));
    return NeonValue::fromObject(os);
}

NeonValue nn_memberfunc_object_isstring(NeonState* state, NeonArguments* args)
{
    NeonValue v;
    (void)state;
    v = args->thisval;
    return NeonValue::makeBool(v.isString());
}

NeonValue nn_memberfunc_object_isarray(NeonState* state, NeonArguments* args)
{
    NeonValue v;
    (void)state;
    v = args->thisval;
    return NeonValue::makeBool(v.isArray());
}

NeonValue nn_memberfunc_object_iscallable(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return (NeonValue::makeBool(
        selfval.isClass() ||
        selfval.isFuncScript() ||
        selfval.isFuncClosure() ||
        selfval.isFuncBound() ||
        selfval.isFuncNative()
    ));
}

NeonValue nn_memberfunc_object_isbool(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return NeonValue::makeBool(selfval.isBool());
}

NeonValue nn_memberfunc_object_isnumber(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return NeonValue::makeBool(selfval.isNumber());
}

NeonValue nn_memberfunc_object_isint(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return NeonValue::makeBool(selfval.isNumber() && (((int)selfval.asNumber()) == selfval.asNumber()));
}

NeonValue nn_memberfunc_object_isdict(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return NeonValue::makeBool(selfval.isDict());
}

NeonValue nn_memberfunc_object_isobject(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return NeonValue::makeBool(selfval.isObject());
}

NeonValue nn_memberfunc_object_isfunction(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return NeonValue::makeBool(
        selfval.isFuncScript() ||
        selfval.isFuncClosure() ||
        selfval.isFuncBound() ||
        selfval.isFuncNative()
    );
}

NeonValue nn_memberfunc_object_isiterable(NeonState* state, NeonArguments* args)
{
    bool isiterable;
    NeonValue dummy;
    NeonObjClass* klass;
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    isiterable = selfval.isArray() || selfval.isDict() || selfval.isString();
    if(!isiterable && selfval.isInstance())
    {
        klass = selfval.asInstance()->klass;
        isiterable = klass->methods->get(NeonValue::fromObject(NeonObjString::copy(state, "@iter")), &dummy)
            && klass->methods->get(NeonValue::fromObject(NeonObjString::copy(state, "@itern")), &dummy);
    }
    return NeonValue::makeBool(isiterable);
}

NeonValue nn_memberfunc_object_isclass(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return NeonValue::makeBool(selfval.isClass());
}

NeonValue nn_memberfunc_object_isfile(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return NeonValue::makeBool(selfval.isFile());
}

NeonValue nn_memberfunc_object_isinstance(NeonState* state, NeonArguments* args)
{
    NeonValue selfval;
    (void)state;
    selfval = args->thisval;
    return NeonValue::makeBool(selfval.isInstance());
}


NeonObjString* nn_util_numbertobinstring(NeonState* state, long n)
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
    return NeonObjString::copy(state, newstr, length);
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
    //  return NeonObjString::copy(state, str, length);
    */
}

NeonObjString* nn_util_numbertooctstring(NeonState* state, long long n, bool numeric)
{
    int length;
    /* assume maximum of 64 bits + 2 octal indicators (0c) */
    char str[66];
    length = sprintf(str, numeric ? "0c%llo" : "%llo", n);
    return NeonObjString::copy(state, str, length);
}

NeonObjString* nn_util_numbertohexstring(NeonState* state, long long n, bool numeric)
{
    int length;
    /* assume maximum of 64 bits + 2 hex indicators (0x) */
    char str[66];
    length = sprintf(str, numeric ? "0x%llx" : "%llx", n);
    return NeonObjString::copy(state, str, length);
}

NeonValue nn_memberfunc_number_tobinstring(NeonState* state, NeonArguments* args)
{
    return NeonValue::fromObject(nn_util_numbertobinstring(state, args->thisval.asNumber()));
}

NeonValue nn_memberfunc_number_tooctstring(NeonState* state, NeonArguments* args)
{
    return NeonValue::fromObject(nn_util_numbertooctstring(state, args->thisval.asNumber(), false));
}

NeonValue nn_memberfunc_number_tohexstring(NeonState* state, NeonArguments* args)
{
    return NeonValue::fromObject(nn_util_numbertohexstring(state, args->thisval.asNumber(), false));
}

void nn_state_initbuiltinmethods(NeonState* state)
{
    {
        nn_class_setstaticpropertycstr(state->classprimprocess, "env", NeonValue::fromObject(state->envdict));
    }
    {
        nn_class_defstaticnativemethod(state, state->classprimobject, "typename", nn_memberfunc_object_typename);
        nn_class_defnativemethod(state, state->classprimobject, "dump", nn_memberfunc_object_dump);
        nn_class_defnativemethod(state, state->classprimobject, "toString", nn_memberfunc_object_tostring);
        nn_class_defnativemethod(state, state->classprimobject, "isArray", nn_memberfunc_object_isarray);        
        nn_class_defnativemethod(state, state->classprimobject, "isString", nn_memberfunc_object_isstring);
        nn_class_defnativemethod(state, state->classprimobject, "isCallable", nn_memberfunc_object_iscallable);
        nn_class_defnativemethod(state, state->classprimobject, "isBool", nn_memberfunc_object_isbool);
        nn_class_defnativemethod(state, state->classprimobject, "isNumber", nn_memberfunc_object_isnumber);
        nn_class_defnativemethod(state, state->classprimobject, "isInt", nn_memberfunc_object_isint);
        nn_class_defnativemethod(state, state->classprimobject, "isDict", nn_memberfunc_object_isdict);
        nn_class_defnativemethod(state, state->classprimobject, "isObject", nn_memberfunc_object_isobject);
        nn_class_defnativemethod(state, state->classprimobject, "isFunction", nn_memberfunc_object_isfunction);
        nn_class_defnativemethod(state, state->classprimobject, "isIterable", nn_memberfunc_object_isiterable);
        nn_class_defnativemethod(state, state->classprimobject, "isClass", nn_memberfunc_object_isclass);
        nn_class_defnativemethod(state, state->classprimobject, "isFile", nn_memberfunc_object_isfile);
        nn_class_defnativemethod(state, state->classprimobject, "isInstance", nn_memberfunc_object_isinstance);

    }
    
    {
        nn_class_defnativemethod(state, state->classprimnumber, "toHexString", nn_memberfunc_number_tohexstring);
        nn_class_defnativemethod(state, state->classprimnumber, "toOctString", nn_memberfunc_number_tooctstring);
        nn_class_defnativemethod(state, state->classprimnumber, "toBinString", nn_memberfunc_number_tobinstring);
    }
    {
        nn_class_defnativeconstructor(state, state->classprimstring, nn_memberfunc_string_constructor);
        nn_class_defstaticnativemethod(state, state->classprimstring, "fromCharCode", nn_memberfunc_string_fromcharcode);
        nn_class_defcallablefield(state, state->classprimstring, "length", nn_memberfunc_string_length);
        nn_class_defnativemethod(state, state->classprimstring, "@iter", nn_memberfunc_string_iter);
        nn_class_defnativemethod(state, state->classprimstring, "@itern", nn_memberfunc_string_itern);
        nn_class_defnativemethod(state, state->classprimstring, "size", nn_memberfunc_string_length);
        nn_class_defnativemethod(state, state->classprimstring, "substr", nn_memberfunc_string_substring);
        nn_class_defnativemethod(state, state->classprimstring, "substring", nn_memberfunc_string_substring);
        nn_class_defnativemethod(state, state->classprimstring, "charCodeAt", nn_memberfunc_string_charcodeat);
        nn_class_defnativemethod(state, state->classprimstring, "charAt", nn_memberfunc_string_charat);
        nn_class_defnativemethod(state, state->classprimstring, "upper", nn_memberfunc_string_upper);
        nn_class_defnativemethod(state, state->classprimstring, "lower", nn_memberfunc_string_lower);
        nn_class_defnativemethod(state, state->classprimstring, "trim", nn_memberfunc_string_trim);
        nn_class_defnativemethod(state, state->classprimstring, "ltrim", nn_memberfunc_string_ltrim);
        nn_class_defnativemethod(state, state->classprimstring, "rtrim", nn_memberfunc_string_rtrim);
        nn_class_defnativemethod(state, state->classprimstring, "split", nn_memberfunc_string_split);
        nn_class_defnativemethod(state, state->classprimstring, "indexOf", nn_memberfunc_string_indexof);
        nn_class_defnativemethod(state, state->classprimstring, "count", nn_memberfunc_string_count);
        nn_class_defnativemethod(state, state->classprimstring, "toNumber", nn_memberfunc_string_tonumber);
        nn_class_defnativemethod(state, state->classprimstring, "toList", nn_memberfunc_string_tolist);
        nn_class_defnativemethod(state, state->classprimstring, "lpad", nn_memberfunc_string_lpad);
        nn_class_defnativemethod(state, state->classprimstring, "rpad", nn_memberfunc_string_rpad);
        nn_class_defnativemethod(state, state->classprimstring, "replace", nn_memberfunc_string_replace);
        nn_class_defnativemethod(state, state->classprimstring, "each", nn_memberfunc_string_each);
        nn_class_defnativemethod(state, state->classprimstring, "startswith", nn_memberfunc_string_startswith);
        nn_class_defnativemethod(state, state->classprimstring, "endswith", nn_memberfunc_string_endswith);
        nn_class_defnativemethod(state, state->classprimstring, "isAscii", nn_memberfunc_string_isascii);
        nn_class_defnativemethod(state, state->classprimstring, "isAlpha", nn_memberfunc_string_isalpha);
        nn_class_defnativemethod(state, state->classprimstring, "isAlnum", nn_memberfunc_string_isalnum);
        nn_class_defnativemethod(state, state->classprimstring, "isNumber", nn_memberfunc_string_isnumber);
        nn_class_defnativemethod(state, state->classprimstring, "isFloat", nn_memberfunc_string_isfloat);
        nn_class_defnativemethod(state, state->classprimstring, "isLower", nn_memberfunc_string_islower);
        nn_class_defnativemethod(state, state->classprimstring, "isUpper", nn_memberfunc_string_isupper);
        nn_class_defnativemethod(state, state->classprimstring, "isSpace", nn_memberfunc_string_isspace);
        
    }
    {
        #if 1
        nn_class_defnativeconstructor(state, state->classprimarray, nn_memberfunc_array_constructor);
        #endif
        nn_class_defcallablefield(state, state->classprimarray, "length", nn_memberfunc_array_length);
        nn_class_defnativemethod(state, state->classprimarray, "size", nn_memberfunc_array_length);
        nn_class_defnativemethod(state, state->classprimarray, "join", nn_memberfunc_array_join);
        nn_class_defnativemethod(state, state->classprimarray, "append", nn_memberfunc_array_append);
        nn_class_defnativemethod(state, state->classprimarray, "push", nn_memberfunc_array_append);
        nn_class_defnativemethod(state, state->classprimarray, "clear", nn_memberfunc_array_clear);
        nn_class_defnativemethod(state, state->classprimarray, "clone", nn_memberfunc_array_clone);
        nn_class_defnativemethod(state, state->classprimarray, "count", nn_memberfunc_array_count);
        nn_class_defnativemethod(state, state->classprimarray, "extend", nn_memberfunc_array_extend);
        nn_class_defnativemethod(state, state->classprimarray, "indexOf", nn_memberfunc_array_indexof);
        nn_class_defnativemethod(state, state->classprimarray, "insert", nn_memberfunc_array_insert);
        nn_class_defnativemethod(state, state->classprimarray, "pop", nn_memberfunc_array_pop);
        nn_class_defnativemethod(state, state->classprimarray, "shift", nn_memberfunc_array_shift);
        nn_class_defnativemethod(state, state->classprimarray, "removeAt", nn_memberfunc_array_removeat);
        nn_class_defnativemethod(state, state->classprimarray, "remove", nn_memberfunc_array_remove);
        nn_class_defnativemethod(state, state->classprimarray, "reverse", nn_memberfunc_array_reverse);
        nn_class_defnativemethod(state, state->classprimarray, "sort", nn_memberfunc_array_sort);
        nn_class_defnativemethod(state, state->classprimarray, "contains", nn_memberfunc_array_contains);
        nn_class_defnativemethod(state, state->classprimarray, "delete", nn_memberfunc_array_delete);
        nn_class_defnativemethod(state, state->classprimarray, "first", nn_memberfunc_array_first);
        nn_class_defnativemethod(state, state->classprimarray, "last", nn_memberfunc_array_last);
        nn_class_defnativemethod(state, state->classprimarray, "isEmpty", nn_memberfunc_array_isempty);
        nn_class_defnativemethod(state, state->classprimarray, "take", nn_memberfunc_array_take);
        nn_class_defnativemethod(state, state->classprimarray, "get", nn_memberfunc_array_get);
        nn_class_defnativemethod(state, state->classprimarray, "compact", nn_memberfunc_array_compact);
        nn_class_defnativemethod(state, state->classprimarray, "unique", nn_memberfunc_array_unique);
        nn_class_defnativemethod(state, state->classprimarray, "zip", nn_memberfunc_array_zip);
        nn_class_defnativemethod(state, state->classprimarray, "zipFrom", nn_memberfunc_array_zipfrom);
        nn_class_defnativemethod(state, state->classprimarray, "toDict", nn_memberfunc_array_todict);
        nn_class_defnativemethod(state, state->classprimarray, "each", nn_memberfunc_array_each);
        nn_class_defnativemethod(state, state->classprimarray, "map", nn_memberfunc_array_map);
        nn_class_defnativemethod(state, state->classprimarray, "filter", nn_memberfunc_array_filter);
        nn_class_defnativemethod(state, state->classprimarray, "some", nn_memberfunc_array_some);
        nn_class_defnativemethod(state, state->classprimarray, "every", nn_memberfunc_array_every);
        nn_class_defnativemethod(state, state->classprimarray, "reduce", nn_memberfunc_array_reduce);
        nn_class_defnativemethod(state, state->classprimarray, "@iter", nn_memberfunc_array_iter);
        nn_class_defnativemethod(state, state->classprimarray, "@itern", nn_memberfunc_array_itern);
    }
    {
        #if 0
        nn_class_defnativeconstructor(state, state->classprimdict, nn_memberfunc_dict_constructor);
        nn_class_defstaticnativemethod(state, state->classprimdict, "keys", nn_memberfunc_dict_keys);
        #endif
        nn_class_defnativemethod(state, state->classprimdict, "keys", nn_memberfunc_dict_keys);
        nn_class_defnativemethod(state, state->classprimdict, "size", nn_memberfunc_dict_length);
        nn_class_defnativemethod(state, state->classprimdict, "add", nn_memberfunc_dict_add);
        nn_class_defnativemethod(state, state->classprimdict, "set", nn_memberfunc_dict_set);
        nn_class_defnativemethod(state, state->classprimdict, "clear", nn_memberfunc_dict_clear);
        nn_class_defnativemethod(state, state->classprimdict, "clone", nn_memberfunc_dict_clone);
        nn_class_defnativemethod(state, state->classprimdict, "compact", nn_memberfunc_dict_compact);
        nn_class_defnativemethod(state, state->classprimdict, "contains", nn_memberfunc_dict_contains);
        nn_class_defnativemethod(state, state->classprimdict, "extend", nn_memberfunc_dict_extend);
        nn_class_defnativemethod(state, state->classprimdict, "get", nn_memberfunc_dict_get);
        nn_class_defnativemethod(state, state->classprimdict, "values", nn_memberfunc_dict_values);
        nn_class_defnativemethod(state, state->classprimdict, "remove", nn_memberfunc_dict_remove);
        nn_class_defnativemethod(state, state->classprimdict, "isEmpty", nn_memberfunc_dict_isempty);
        nn_class_defnativemethod(state, state->classprimdict, "findKey", nn_memberfunc_dict_findkey);
        nn_class_defnativemethod(state, state->classprimdict, "toList", nn_memberfunc_dict_tolist);
        nn_class_defnativemethod(state, state->classprimdict, "each", nn_memberfunc_dict_each);
        nn_class_defnativemethod(state, state->classprimdict, "filter", nn_memberfunc_dict_filter);
        nn_class_defnativemethod(state, state->classprimdict, "some", nn_memberfunc_dict_some);
        nn_class_defnativemethod(state, state->classprimdict, "every", nn_memberfunc_dict_every);
        nn_class_defnativemethod(state, state->classprimdict, "reduce", nn_memberfunc_dict_reduce);
        nn_class_defnativemethod(state, state->classprimdict, "@iter", nn_memberfunc_dict_iter);
        nn_class_defnativemethod(state, state->classprimdict, "@itern", nn_memberfunc_dict_itern);
    }
    {
        nn_class_defnativeconstructor(state, state->classprimfile, nn_memberfunc_file_constructor);
        nn_class_defstaticnativemethod(state, state->classprimfile, "exists", nn_memberfunc_file_exists);
        nn_class_defnativemethod(state, state->classprimfile, "close", nn_memberfunc_file_close);
        nn_class_defnativemethod(state, state->classprimfile, "open", nn_memberfunc_file_open);
        nn_class_defnativemethod(state, state->classprimfile, "read", nn_memberfunc_file_read);
        nn_class_defnativemethod(state, state->classprimfile, "get", nn_memberfunc_file_get);
        nn_class_defnativemethod(state, state->classprimfile, "gets", nn_memberfunc_file_gets);
        nn_class_defnativemethod(state, state->classprimfile, "write", nn_memberfunc_file_write);
        nn_class_defnativemethod(state, state->classprimfile, "puts", nn_memberfunc_file_puts);
        nn_class_defnativemethod(state, state->classprimfile, "printf", nn_memberfunc_file_printf);
        nn_class_defnativemethod(state, state->classprimfile, "number", nn_memberfunc_file_number);
        nn_class_defnativemethod(state, state->classprimfile, "isTTY", nn_memberfunc_file_istty);
        nn_class_defnativemethod(state, state->classprimfile, "isOpen", nn_memberfunc_file_isopen);
        nn_class_defnativemethod(state, state->classprimfile, "isClosed", nn_memberfunc_file_isclosed);
        nn_class_defnativemethod(state, state->classprimfile, "flush", nn_memberfunc_file_flush);
        nn_class_defnativemethod(state, state->classprimfile, "stats", nn_memberfunc_file_stats);
        nn_class_defnativemethod(state, state->classprimfile, "path", nn_memberfunc_file_path);
        nn_class_defnativemethod(state, state->classprimfile, "seek", nn_memberfunc_file_seek);
        nn_class_defnativemethod(state, state->classprimfile, "tell", nn_memberfunc_file_tell);
        nn_class_defnativemethod(state, state->classprimfile, "mode", nn_memberfunc_file_mode);
        nn_class_defnativemethod(state, state->classprimfile, "name", nn_memberfunc_file_name);
    }
    {
        nn_class_defnativeconstructor(state, state->classprimrange, nn_memberfunc_range_constructor);
        nn_class_defnativemethod(state, state->classprimrange, "lower", nn_memberfunc_range_lower);
        nn_class_defnativemethod(state, state->classprimrange, "upper", nn_memberfunc_range_upper);
        nn_class_defnativemethod(state, state->classprimrange, "range", nn_memberfunc_range_range);
        nn_class_defnativemethod(state, state->classprimrange, "loop", nn_memberfunc_range_loop);
        nn_class_defnativemethod(state, state->classprimrange, "expand", nn_memberfunc_range_expand);
        nn_class_defnativemethod(state, state->classprimrange, "toArray", nn_memberfunc_range_expand);
        nn_class_defnativemethod(state, state->classprimrange, "@iter", nn_memberfunc_range_iter);
        nn_class_defnativemethod(state, state->classprimrange, "@itern", nn_memberfunc_range_itern);
    }
}

NeonValue nn_nativefn_time(NeonState* state, NeonArguments* args)
{
    struct timeval tv;
    (void)args;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    osfn_gettimeofday(&tv, NULL);
    return NeonValue::makeNumber((double)tv.tv_sec + ((double)tv.tv_usec / 10000000));
}

NeonValue nn_nativefn_microtime(NeonState* state, NeonArguments* args)
{
    struct timeval tv;
    (void)args;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 0);
    osfn_gettimeofday(&tv, NULL);
    return NeonValue::makeNumber((1000000 * (double)tv.tv_sec) + ((double)tv.tv_usec / 10));
}

NeonValue nn_nativefn_id(NeonState* state, NeonArguments* args)
{
    NeonValue val;
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 1);
    val = args->argv[0];
    return NeonValue::makeNumber(*(long*)&val);
}

NeonValue nn_nativefn_int(NeonState* state, NeonArguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNTRANGE(args, 0, 1);
    if(args->argc == 0)
    {
        return NeonValue::makeNumber(0);
    }
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    return NeonValue::makeNumber((double)((int)args->argv[0].asNumber()));
}

NeonValue nn_nativefn_chr(NeonState* state, NeonArguments* args)
{
    char* string;
    NEON_ARGS_CHECKCOUNT(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isNumber);
    string = nn_util_utf8encode(state, (int)args->argv[0].asNumber());
    return NeonValue::fromObject(NeonObjString::take(state, string));
}

NeonValue nn_nativefn_ord(NeonState* state, NeonArguments* args)
{
    int ord;
    int length;
    NeonObjString* string;
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
    return NeonValue::makeNumber(ord);
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

NeonValue nn_nativefn_rand(NeonState* state, NeonArguments* args)
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
    return NeonValue::makeNumber(nn_util_mtrand(lowerlimit, upperlimit));
}

NeonValue nn_nativefn_typeof(NeonState* state, NeonArguments* args)
{
    const char* result;
    NEON_ARGS_CHECKCOUNT(args, 1);
    result = nn_value_typename(args->argv[0]);
    return NeonValue::fromObject(NeonObjString::copy(state, result));
}

NeonValue nn_nativefn_eval(NeonState* state, NeonArguments* args)
{
    NeonValue result;
    NeonObjString* os;
    NEON_ARGS_CHECKCOUNT(args, 1);
    os = args->argv[0].asString();
    /*fprintf(stderr, "eval:src=%s\n", os->data());*/
    result = nn_state_evalsource(state, os->data());
    return result;
}

/*
NeonValue nn_nativefn_loadfile(NeonState* state, NeonArguments* args)
{
    NeonValue result;
    NeonObjString* os;
    NEON_ARGS_CHECKCOUNT(args, 1);
    os = args->argv[0].asString();
    fprintf(stderr, "eval:src=%s\n", os->data());
    result = nn_state_evalsource(state, os->data());
    return result;
}
*/

NeonValue nn_nativefn_instanceof(NeonState* state, NeonArguments* args)
{
    (void)state;
    NEON_ARGS_CHECKCOUNT(args, 2);
    NEON_ARGS_CHECKTYPE(args, 0, isInstance);
    NEON_ARGS_CHECKTYPE(args, 1, isClass);
    return NeonValue::makeBool(nn_util_isinstanceof(args->argv[0].asInstance()->klass, nn_value_asclass(args->argv[1])));
}


NeonValue nn_nativefn_sprintf(NeonState* state, NeonArguments* args)
{
    NeonFormatInfo nfi;
    NeonPrinter pd;
    NeonObjString* res;
    NeonObjString* ofmt;
    NEON_ARGS_CHECKMINARG(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    ofmt = args->argv[0].asString();
    NeonPrinter::makeStackString(state, &pd);
    nfi.init(state, &pd, ofmt->cstr(), ofmt->length());
    if(!nfi.format(args->argc, 1, args->argv))
    {
        return NeonValue::makeNull();
    }
    res = pd.takeString();
    return NeonValue::fromObject(res);
}

NeonValue nn_nativefn_printf(NeonState* state, NeonArguments* args)
{
    NeonFormatInfo nfi;
    NeonObjString* ofmt;
    NEON_ARGS_CHECKMINARG(args, 1);
    NEON_ARGS_CHECKTYPE(args, 0, isString);
    ofmt = args->argv[0].asString();
    nfi.init(state, state->stdoutwriter, ofmt->cstr(), ofmt->length());
    if(!nfi.format(args->argc, 1, args->argv))
    {
    }
    return NeonValue::makeNull();
}

NeonValue nn_nativefn_print(NeonState* state, NeonArguments* args)
{
    int i;
    for(i = 0; i < args->argc; i++)
    {
        nn_printer_printvalue(state->stdoutwriter, args->argv[i], false, true);
    }
    if(state->isrepl)
    {
        state->stdoutwriter->put("\n");
    }
    return NeonValue::makeEmpty();
}

NeonValue nn_nativefn_println(NeonState* state, NeonArguments* args)
{
    NeonValue v;
    v = nn_nativefn_print(state, args);
    state->stdoutwriter->put("\n");
    return v;
}

void nn_state_initbuiltinfunctions(NeonState* state)
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

NeonValue nn_exceptions_getstacktrace(NeonState* state)
{
    int line;
    size_t i;
    size_t instruction;
    const char* fnname;
    const char* physfile;
    NeonState::CallFrame* frame;
    NeonObjFuncScript* function;
    NeonObjString* os;
    NeonObjArray* oa;
    NeonPrinter pd;
    oa = NeonObjArray::make(state);
    {
        for(i = 0; i < state->vmstate.framecount; i++)
        {
            NeonPrinter::makeStackString(state, &pd);
            frame = &state->vmstate.framevalues[i];
            function = frame->closure->scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->blob.instrucs - 1;
            line = function->blob.instrucs[instruction].srcline;
            physfile = "(unknown)";
            if(function->module->physicalpath != NULL)
            {
                if(function->module->physicalpath->m_sbuf != NULL)
                {
                    physfile = function->module->physicalpath->data();
                }
            }
            fnname = "<script>";
            if(function->name != NULL)
            {
                fnname = function->name->data();
            }
            pd.putformat("from %s() in %s:%d", fnname, physfile, line);
            os = pd.takeString();
            oa->push(NeonValue::fromObject(os));
            if((i > 15) && (state->conf.showfullstack == false))
            {
                NeonPrinter::makeStackString(state, &pd);
                pd.putformat("(only upper 15 entries shown)");
                os = pd.takeString();
                oa->push(NeonValue::fromObject(os));
                break;
            }
        }
        return NeonValue::fromObject(oa);
    }
    return NeonValue::fromObject(NeonObjString::copy(state, "", 0));
}

static NEON_ALWAYSINLINE NeonInstruction nn_util_makeinst(bool isop, uint8_t code, int srcline)
{
    NeonInstruction inst;
    inst.isop = isop;
    inst.code = code;
    inst.srcline = srcline;
    return inst;
}

NeonObjClass* nn_exceptions_makeclass(NeonState* state, NeonObjModule* module, const char* cstrname)
{
    int messageconst;
    NeonObjClass* klass;
    NeonObjString* classname;
    NeonObjFuncScript* function;
    NeonObjFuncClosure* closure;
    classname = NeonObjString::copy(state, cstrname);
    state->stackPush(NeonValue::fromObject(classname));
    klass = nn_object_makeclass(state, classname);
    state->stackPop();
    state->stackPush(NeonValue::fromObject(klass));
    function = nn_object_makefuncscript(state, module, NEON_FUNCTYPE_METHOD);
    function->arity = 1;
    function->isvariadic = false;
    state->stackPush(NeonValue::fromObject(function));
    {
        /* g_loc 0 */
        nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(false, (0 >> 8) & 0xff, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(false, 0 & 0xff, 0));
    }
    {
        /* g_loc 1 */
        nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(false, (1 >> 8) & 0xff, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(false, 1 & 0xff, 0));
    }
    {
        messageconst = nn_blob_pushconst(state, &function->blob, NeonValue::fromObject(NeonObjString::copy(state, "message")));
        /* s_prop 0 */
        nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_PROPERTYSET, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(false, (messageconst >> 8) & 0xff, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(false, messageconst & 0xff, 0));
    }
    {
        /* pop */
        nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_POPONE, 0));
        nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_POPONE, 0));
    }
    {
        /* g_loc 0 */
        /*
        //  nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        //  nn_blob_push(state, &function->blob, nn_util_makeinst(false, (0 >> 8) & 0xff, 0));
        //  nn_blob_push(state, &function->blob, nn_util_makeinst(false, 0 & 0xff, 0));
        */
    }
    {
        /* ret */
        nn_blob_push(state, &function->blob, nn_util_makeinst(true, NEON_OP_RETURN, 0));
    }
    closure = nn_object_makefuncclosure(state, function);
    state->stackPop();
    /* set class constructor */
    state->stackPush(NeonValue::fromObject(closure));
    klass->methods->set(NeonValue::fromObject(classname), NeonValue::fromObject(closure));
    klass->initializer = NeonValue::fromObject(closure);
    /* set class properties */
    nn_class_defproperty(klass, "message", NeonValue::makeNull());
    nn_class_defproperty(klass, "stacktrace", NeonValue::makeNull());
    state->globals->set(NeonValue::fromObject(classname), NeonValue::fromObject(klass));
    /* for class */
    state->stackPop();
    state->stackPop();
    /* assert error name */
    /* state->stackPop(); */
    return klass;
}

NeonObjInstance* nn_exceptions_makeinstance(NeonState* state, NeonObjClass* exklass, const char* srcfile, int srcline, NeonObjString* message)
{
    NeonObjInstance* instance;
    NeonObjString* osfile;
    instance = nn_object_makeinstance(state, exklass);
    osfile = NeonObjString::copy(state, srcfile);
    state->stackPush(NeonValue::fromObject(instance));
    nn_instance_defproperty(instance, "message", NeonValue::fromObject(message));
    nn_instance_defproperty(instance, "srcfile", NeonValue::fromObject(osfile));
    nn_instance_defproperty(instance, "srcline", NeonValue::makeNumber(srcline));
    state->stackPop();
    return instance;
}



void nn_state_defglobalvalue(NeonState* state, const char* name, NeonValue val)
{
    NeonObjString* oname;
    oname = NeonObjString::copy(state, name);
    state->stackPush(NeonValue::fromObject(oname));
    state->stackPush(val);
    state->globals->set(state->vmstate.stackvalues[0], state->vmstate.stackvalues[1]);
    state->stackPop(2);
}

void nn_state_defnativefunction(NeonState* state, const char* name, NeonNativeFN fptr)
{
    NeonObjFuncNative* func;
    func = nn_object_makefuncnative(state, fptr, name);
    return nn_state_defglobalvalue(state, name, NeonValue::fromObject(func));
}

NeonObjClass* nn_util_makeclass(NeonState* state, const char* name, NeonObjClass* parent)
{
    NeonObjClass* cl;
    NeonObjString* os;
    os = NeonObjString::copy(state, name);
    cl = nn_object_makeclass(state, os);
    cl->superclass = parent;
    state->globals->set(NeonValue::fromObject(os), NeonValue::fromObject(cl));
    return cl;
}

void nn_vm_initvmstate(NeonState* state)
{
    state->vmstate.linkedobjects = NULL;
    state->vmstate.currentframe = NULL;
    {
        state->vmstate.stackcapacity = NEON_CFG_INITSTACKCOUNT;
        state->vmstate.stackvalues = (NeonValue*)malloc(NEON_CFG_INITSTACKCOUNT * sizeof(NeonValue));
        if(state->vmstate.stackvalues == NULL)
        {
            fprintf(stderr, "error: failed to allocate stackvalues!\n");
            abort();
        }
        memset(state->vmstate.stackvalues, 0, NEON_CFG_INITSTACKCOUNT * sizeof(NeonValue));
    }
    {
        state->vmstate.framecapacity = NEON_CFG_INITFRAMECOUNT;
        state->vmstate.framevalues = (NeonState::CallFrame*)malloc(NEON_CFG_INITFRAMECOUNT * sizeof(NeonState::CallFrame));
        if(state->vmstate.framevalues == NULL)
        {
            fprintf(stderr, "error: failed to allocate framevalues!\n");
            abort();
        }
        memset(state->vmstate.framevalues, 0, NEON_CFG_INITFRAMECOUNT * sizeof(NeonState::CallFrame));
    }
}


void nn_state_resetvmstate(NeonState* state)
{
    state->vmstate.framecount = 0;
    state->vmstate.stackidx = 0;
    state->vmstate.openupvalues = NULL;
}

bool nn_state_addsearchpathobj(NeonState* state, NeonObjString* os)
{
    state->importpath->push(NeonValue::fromObject(os));
    return true;
}

bool nn_state_addsearchpath(NeonState* state, const char* path)
{
    return nn_state_addsearchpathobj(state, NeonObjString::copy(state, path));
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

NeonState::~NeonState()
{
    destrdebug("destroying importpath...");
    delete this->importpath;
    destrdebug("destroying linked objects...");
    nn_gcmem_destroylinkedobjects(this);
    /* since object in module can exist in globals, it must come before */
    destrdebug("destroying module table...");
    delete this->modules;
    destrdebug("destroying globals table...");
    delete this->globals;
    destrdebug("destroying strings table...");
    delete this->strings;
    destrdebug("destroying stdoutwriter...");
    delete this->stdoutwriter;
    destrdebug("destroying stderrwriter...");
    delete this->stderrwriter;
    destrdebug("destroying debugwriter...");
    delete this->debugwriter;
    destrdebug("destroying framevalues...");
    free(this->vmstate.framevalues);
    destrdebug("destroying stackvalues...");
    free(this->vmstate.stackvalues);
    destrdebug("destroying this...");
    destrdebug("done destroying!");
}


void NeonState::init(void* userptr)
{
    static const char* defaultsearchpaths[] =
    {
        "mods",
        "mods/@/index" NEON_CFG_FILEEXT,
        ".",
        NULL
    };
    int i;
    this->memuserptr = userptr;
    this->compiler = NULL;
    this->exceptions.stdexception = NULL;
    this->rootphysfile = NULL;
    this->cliargv = NULL;
    this->isrepl = false;
    this->markvalue = true;
    nn_vm_initvmstate(this);
    nn_state_resetvmstate(this);
    {
        this->conf.enablestrictmode = false;
        this->conf.shoulddumpstack = false;
        this->conf.enablewarnings = false;
        this->conf.dumpbytecode = false;
        this->conf.exitafterbytecode = false;
        this->conf.showfullstack = false;
        this->conf.enableapidebug = false;
        this->conf.enableastdebug = false;
    }
    {
        this->gcstate.bytesallocated = 0;
        /* default is 1mb. Can be modified via the -g flag. */
        this->gcstate.nextgc = NEON_CFG_DEFAULTGCSTART;
        this->gcstate.graycount = 0;
        this->gcstate.graycapacity = 0;
        this->gcstate.graystack = NULL;
    }
    {
        this->stdoutwriter = NeonPrinter::makeIO(this, stdout, false);
        this->stdoutwriter->m_shouldflush = true;
        this->stderrwriter = NeonPrinter::makeIO(this, stderr, false);
        this->debugwriter = NeonPrinter::makeIO(this, stderr, false);
        this->debugwriter->m_shortenvalues = true;
        this->debugwriter->m_maxvallength = 15;
    }
    {
        this->modules = new NeonHashTable(this);
        this->strings = new NeonHashTable(this);
        this->globals = new NeonHashTable(this);
    }
    {
        this->topmodule = nn_module_make(this, "", "<this>", false);
        this->constructorname = NeonObjString::copy(this, "constructor");
    }
    {
        this->importpath = new NeonValArray(this);
        for(i=0; defaultsearchpaths[i]!=NULL; i++)
        {
            nn_state_addsearchpath(this, defaultsearchpaths[i]);
        }
    }
    {
        this->classprimobject = nn_util_makeclass(this, "Object", NULL);
        this->classprimprocess = nn_util_makeclass(this, "Process", this->classprimobject);
        this->classprimnumber = nn_util_makeclass(this, "Number", this->classprimobject);
        this->classprimstring = nn_util_makeclass(this, "String", this->classprimobject);
        this->classprimarray = nn_util_makeclass(this, "Array", this->classprimobject);
        this->classprimdict = nn_util_makeclass(this, "Dict", this->classprimobject);
        this->classprimfile = nn_util_makeclass(this, "File", this->classprimobject);
        this->classprimrange = nn_util_makeclass(this, "Range", this->classprimobject);
    }
    {
        this->envdict = nn_object_makedict(this);
    }
    {
        if(this->exceptions.stdexception == NULL)
        {
            this->exceptions.stdexception = nn_exceptions_makeclass(this, NULL, "Exception");
        }
        this->exceptions.asserterror = nn_exceptions_makeclass(this, NULL, "AssertError");
        this->exceptions.syntaxerror = nn_exceptions_makeclass(this, NULL, "SyntaxError");
        this->exceptions.ioerror = nn_exceptions_makeclass(this, NULL, "IOError");
        this->exceptions.oserror = nn_exceptions_makeclass(this, NULL, "OSError");
        this->exceptions.argumenterror = nn_exceptions_makeclass(this, NULL, "ArgumentError");
    }
    {
        nn_state_initbuiltinfunctions(this);
        nn_state_initbuiltinmethods(this);
    }
    {
        {
            this->filestdout = nn_object_makefile(this, stdout, true, "<stdout>", "wb");
            nn_state_defglobalvalue(this, "STDOUT", NeonValue::fromObject(this->filestdout));
        }
        {
            this->filestderr = nn_object_makefile(this, stderr, true, "<stderr>", "wb");
            nn_state_defglobalvalue(this, "STDERR", NeonValue::fromObject(this->filestderr));
        }
        {
            this->filestdin = nn_object_makefile(this, stdin, true, "<stdin>", "rb");
            nn_state_defglobalvalue(this, "STDIN", NeonValue::fromObject(this->filestdin));
        }
    }
}

bool nn_util_methodisprivate(NeonObjString* name)
{
    return name->length() > 0 && name->data()[0] == '_';
}

bool nn_vm_callclosure(NeonState* state, NeonObjFuncClosure* closure, NeonValue thisval, int argcount)
{
    int i;
    int startva;
    NeonState::CallFrame* frame;
    NeonObjArray* argslist;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    /* fill empty parameters if not variadic */
    for(; !closure->scriptfunc->isvariadic && argcount < closure->scriptfunc->arity; argcount++)
    {
        state->stackPush(NeonValue::makeNull());
    }
    /* handle variadic arguments... */
    if(closure->scriptfunc->isvariadic && argcount >= closure->scriptfunc->arity - 1)
    {
        startva = argcount - closure->scriptfunc->arity;
        argslist = NeonObjArray::make(state);
        state->stackPush(NeonValue::fromObject(argslist));
        for(i = startva; i >= 0; i--)
        {
            argslist->varray->push(state->stackPeek(i + 1));
        }
        argcount -= startva;
        /* +1 for the gc protection push above */
        state->stackPop(startva + 2);
        state->stackPush(NeonValue::fromObject(argslist));
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
    frame = &state->vmstate.framevalues[state->vmstate.framecount++];
    frame->closure = closure;
    frame->inscode = closure->scriptfunc->blob.instrucs;
    frame->stackslotpos = state->vmstate.stackidx + (-argcount - 1);
    return true;
}

bool nn_vm_callnative(NeonState* state, NeonObjFuncNative* native, NeonValue thisval, int argcount)
{
    size_t spos;
    NeonValue r;
    NeonValue* vargs;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    spos = state->vmstate.stackidx + (-argcount);
    vargs = &state->vmstate.stackvalues[spos];
    NeonArguments fnargs(state, native->name, thisval, vargs, argcount);
    r = native->natfunc(state, &fnargs);
    {
        state->vmstate.stackvalues[spos - 1] = r;
        state->vmstate.stackidx -= argcount;
    }
    state->clearProtect();
    return true;
}

bool nn_vm_callvaluewithobject(NeonState* state, NeonValue callable, NeonValue thisval, int argcount)
{
    size_t spos;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval), argcount);
    if(callable.isObject())
    {
        switch(nn_value_objtype(callable))
        {
            case NEON_OBJTYPE_FUNCBOUND:
                {
                    NeonObjFuncBound* bound;
                    bound = nn_value_asfuncbound(callable);
                    spos = (state->vmstate.stackidx + (-argcount - 1));
                    state->vmstate.stackvalues[spos] = thisval;
                    return nn_vm_callclosure(state, bound->method, thisval, argcount);
                }
                break;
            case NEON_OBJTYPE_CLASS:
                {
                    NeonObjClass* klass;
                    klass = nn_value_asclass(callable);
                    spos = (state->vmstate.stackidx + (-argcount - 1));
                    state->vmstate.stackvalues[spos] = thisval;
                    if(!klass->initializer.isEmpty())
                    {
                        return nn_vm_callvaluewithobject(state, klass->initializer, thisval, argcount);
                    }
                    else if(klass->superclass != NULL && !klass->superclass->initializer.isEmpty())
                    {
                        return nn_vm_callvaluewithobject(state, klass->superclass->initializer, thisval, argcount);
                    }
                    else if(argcount != 0)
                    {
                        return nn_exceptions_throwException(state, "%s constructor expects 0 arguments, %d given", klass->name->data(), argcount);
                    }
                    return true;
                }
                break;
            case NEON_OBJTYPE_MODULE:
                {
                    NeonObjModule* module;
                    NeonProperty* field;
                    module = nn_value_asmodule(callable);
                    field = module->deftable->getFieldByObjStr(module->name);
                    if(field != NULL)
                    {
                        return nn_vm_callvalue(state, field->value, thisval, argcount);
                    }
                    return nn_exceptions_throwException(state, "module %s does not export a default function", module->name);
                }
                break;
            case NEON_OBJTYPE_FUNCCLOSURE:
                {
                    return nn_vm_callclosure(state, nn_value_asfuncclosure(callable), thisval, argcount);
                }
                break;
            case NEON_OBJTYPE_FUNCNATIVE:
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

bool nn_vm_callvalue(NeonState* state, NeonValue callable, NeonValue thisval, int argcount)
{
    NeonValue actualthisval;
    if(callable.isObject())
    {
        switch(nn_value_objtype(callable))
        {
            case NEON_OBJTYPE_FUNCBOUND:
                {
                    NeonObjFuncBound* bound;
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
            case NEON_OBJTYPE_CLASS:
                {
                    NeonObjClass* klass;
                    NeonObjInstance* instance;
                    klass = nn_value_asclass(callable);
                    instance = nn_object_makeinstance(state, klass);
                    actualthisval = NeonValue::fromObject(instance);
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

NeonFuncType nn_value_getmethodtype(NeonValue method)
{
    switch(nn_value_objtype(method))
    {
        case NEON_OBJTYPE_FUNCNATIVE:
            return nn_value_asfuncnative(method)->type;
        case NEON_OBJTYPE_FUNCCLOSURE:
            return nn_value_asfuncclosure(method)->scriptfunc->type;
        default:
            break;
    }
    return NEON_FUNCTYPE_FUNCTION;
}


NeonObjClass* nn_value_getclassfor(NeonState* state, NeonValue receiver)
{
    if(receiver.isNumber())
    {
        return state->classprimnumber;
    }
    if(receiver.isObject())
    {
        switch(receiver.asObject()->m_objtype)
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
            /*
            case NEON_OBJTYPE_FUNCBOUND:
            case NEON_OBJTYPE_FUNCCLOSURE:
            case NEON_OBJTYPE_FUNCSCRIPT:
                return state->classprimcallable;
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
        return NEON_STATUS_FAILRUNTIME; \
    }        

#define nn_vmmac_tryraise(state, rtval, ...) \
    if(!nn_exceptions_throwException(state, ##__VA_ARGS__)) \
    { \
        return rtval; \
    }

static NEON_ALWAYSINLINE uint8_t nn_vmbits_readbyte(NeonState* state)
{
    uint8_t r;
    r = state->vmstate.currentframe->inscode->code;
    state->vmstate.currentframe->inscode++;
    return r;
}

static NEON_ALWAYSINLINE NeonInstruction nn_vmbits_readinstruction(NeonState* state)
{
    NeonInstruction r;
    r = *state->vmstate.currentframe->inscode;
    state->vmstate.currentframe->inscode++;
    return r;
}

static NEON_ALWAYSINLINE uint16_t nn_vmbits_readshort(NeonState* state)
{
    uint8_t b;
    uint8_t a;
    a = state->vmstate.currentframe->inscode[0].code;
    b = state->vmstate.currentframe->inscode[1].code;
    state->vmstate.currentframe->inscode += 2;
    return (uint16_t)((a << 8) | b);
}

static NEON_ALWAYSINLINE NeonValue nn_vmbits_readconst(NeonState* state)
{
    uint16_t idx;
    idx = nn_vmbits_readshort(state);
    return state->vmstate.currentframe->closure->scriptfunc->blob.constants->values[idx];
}

static NEON_ALWAYSINLINE NeonObjString* nn_vmbits_readstring(NeonState* state)
{
    return nn_vmbits_readconst(state).asString();
}

static NEON_ALWAYSINLINE bool nn_vmutil_invokemethodfromclass(NeonState* state, NeonObjClass* klass, NeonObjString* name, int argcount)
{
    NeonProperty* field;
    NEON_APIDEBUG(state, "argcount=%d", argcount);
    field = klass->methods->getFieldByObjStr(name);
    if(field != NULL)
    {
        if(nn_value_getmethodtype(field->value) == NEON_FUNCTYPE_PRIVATE)
        {
            return nn_exceptions_throwException(state, "cannot call private method '%s' from instance of %s", name->data(), klass->name->data());
        }
        return nn_vm_callvaluewithobject(state, field->value, NeonValue::fromObject(klass), argcount);
    }
    return nn_exceptions_throwException(state, "undefined method '%s' in %s", name->data(), klass->name->data());
}

static NEON_ALWAYSINLINE bool nn_vmutil_invokemethodself(NeonState* state, NeonObjString* name, int argcount)
{
    size_t spos;
    NeonValue receiver;
    NeonObjInstance* instance;
    NeonProperty* field;
    NEON_APIDEBUG(state, "argcount=%d", argcount);
    receiver = state->stackPeek(argcount);
    if(receiver.isInstance())
    {
        instance = receiver.asInstance();
        field = instance->klass->methods->getFieldByObjStr(name);
        if(field != NULL)
        {
            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
        }
        field = instance->properties->getFieldByObjStr(name);
        if(field != NULL)
        {
            spos = (state->vmstate.stackidx + (-argcount - 1));
            #if 0
                state->vmstate.stackvalues[spos] = field->value;
                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
            #else
                state->vmstate.stackvalues[spos] = receiver;
                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
            #endif
        }
    }
    else if(receiver.isClass())
    {
        field = nn_value_asclass(receiver)->methods->getFieldByObjStr(name);
        if(field != NULL)
        {
            if(nn_value_getmethodtype(field->value) == NEON_FUNCTYPE_STATIC)
            {
                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
            }
            return nn_exceptions_throwException(state, "cannot call non-static method %s() on non instance", name->data());
        }
    }
    return nn_exceptions_throwException(state, "cannot call method '%s' on object of type '%s'", name->data(), nn_value_typename(receiver));
}

static NEON_ALWAYSINLINE bool nn_vmutil_invokemethod(NeonState* state, NeonObjString* name, int argcount)
{
    size_t spos;
    NeonObjType rectype;
    NeonValue receiver;
    NeonProperty* field;
    NeonObjClass* klass;
    receiver = state->stackPeek(argcount);
    NEON_APIDEBUG(state, "receiver.type=%s, argcount=%d", nn_value_typename(receiver), argcount);
    if(receiver.isObject())
    {
        rectype = receiver.asObject()->m_objtype;
        switch(rectype)
        {
            case NEON_OBJTYPE_MODULE:
                {
                    NeonObjModule* module;
                    NEON_APIDEBUG(state, "receiver is a module");
                    module = nn_value_asmodule(receiver);
                    field = module->deftable->getFieldByObjStr(name);
                    if(field != NULL)
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
            case NEON_OBJTYPE_CLASS:
                {
                    NEON_APIDEBUG(state, "receiver is a class");
                    klass = nn_value_asclass(receiver);
                    field = klass->methods->getFieldByObjStr(name);
                    if(field != NULL)
                    {
                        if(nn_value_getmethodtype(field->value) == NEON_FUNCTYPE_PRIVATE)
                        {
                            return nn_exceptions_throwException(state, "cannot call private method %s() on %s", name->data(), klass->name->data());
                        }
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
                    }
                    else
                    {
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
                    }
                    return nn_exceptions_throwException(state, "unknown method %s() in class %s", name->data(), klass->name->data());
                }
            case NEON_OBJTYPE_INSTANCE:
                {
                    NeonObjInstance* instance;
                    NEON_APIDEBUG(state, "receiver is an instance");
                    instance = receiver.asInstance();
                    field = instance->properties->getFieldByObjStr(name);
                    if(field != NULL)
                    {
                        spos = (state->vmstate.stackidx + (-argcount - 1));
                        #if 0
                            state->vmstate.stackvalues[spos] = field->value;
                        #else
                            state->vmstate.stackvalues[spos] = receiver;
                        #endif
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
                        return nn_vm_callnative(state, nn_value_asfuncnative(field->value), receiver, argcount);
                    }
                    /* NEW in v0.0.84, dictionaries can declare extra methods as part of their entries. */
                    else
                    {
                        field = nn_value_asdict(receiver)->htab->getFieldByObjStr(name);
                        if(field != NULL)
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
    if(klass == NULL)
    {
        /* @TODO: have methods for non objects as well. */
        return nn_exceptions_throwException(state, "non-object %s has no method named '%s'", nn_value_typename(receiver), name->data());
    }
    field = nn_class_getmethodfield(klass, name);
    if(field != NULL)
    {
        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount);
    }
    return nn_exceptions_throwException(state, "'%s' has no method %s()", klass->name->data(), name->data());
}

static NEON_ALWAYSINLINE bool nn_vmutil_bindmethod(NeonState* state, NeonObjClass* klass, NeonObjString* name)
{
    NeonValue val;
    NeonProperty* field;
    NeonObjFuncBound* bound;
    field = klass->methods->getFieldByObjStr(name);
    if(field != NULL)
    {
        if(nn_value_getmethodtype(field->value) == NEON_FUNCTYPE_PRIVATE)
        {
            return nn_exceptions_throwException(state, "cannot get private property '%s' from instance", name->data());
        }
        val = state->stackPeek(0);
        bound = nn_object_makefuncbound(state, val, nn_value_asfuncclosure(field->value));
        state->stackPop();
        state->stackPush(NeonValue::fromObject(bound));
        return true;
    }
    return nn_exceptions_throwException(state, "undefined property '%s'", name->data());
}

static NEON_ALWAYSINLINE NeonObjUpvalue* nn_vmutil_upvaluescapture(NeonState* state, NeonValue* local, int stackpos)
{
    NeonObjUpvalue* upvalue;
    NeonObjUpvalue* prevupvalue;
    NeonObjUpvalue* createdupvalue;
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

static NEON_ALWAYSINLINE void nn_vmutil_upvaluesclose(NeonState* state, const NeonValue* last)
{
    NeonObjUpvalue* upvalue;
    while(state->vmstate.openupvalues != NULL && (&state->vmstate.openupvalues->location) >= last)
    {
        upvalue = state->vmstate.openupvalues;
        upvalue->closed = upvalue->location;
        upvalue->location = upvalue->closed;
        state->vmstate.openupvalues = upvalue->next;
    }
}

static NEON_ALWAYSINLINE void nn_vmutil_definemethod(NeonState* state, NeonObjString* name)
{
    NeonValue method;
    NeonObjClass* klass;
    method = state->stackPeek(0);
    klass = nn_value_asclass(state->stackPeek(1));
    klass->methods->set(NeonValue::fromObject(name), method);
    if(nn_value_getmethodtype(method) == NEON_FUNCTYPE_INITIALIZER)
    {
        klass->initializer = method;
    }
    state->stackPop();
}

static NEON_ALWAYSINLINE void nn_vmutil_defineproperty(NeonState* state, NeonObjString* name, bool isstatic)
{
    NeonValue property;
    NeonObjClass* klass;
    property = state->stackPeek(0);
    klass = nn_value_asclass(state->stackPeek(1));
    if(!isstatic)
    {
        nn_class_defproperty(klass, name->data(), property);
    }
    else
    {
        nn_class_setstaticproperty(klass, name, property);
    }
    state->stackPop();
}

bool nn_value_isfalse(NeonValue value)
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
        return value.asArray()->varray->m_count == 0;
    }
    /* Non-empty dicts are true, empty dicts are false. */
    if(value.isDict())
    {
        return nn_value_asdict(value)->names->m_count == 0;
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

bool nn_util_isinstanceof(NeonObjClass* klass1, NeonObjClass* expected)
{
    size_t klen;
    size_t elen;
    const char* kname;
    const char* ename;
    while(klass1 != NULL)
    {
        elen = expected->name->length();
        klen = klass1->name->length();
        ename = expected->name->data();
        kname = klass1->name->data();
        if(elen == klen && memcmp(kname, ename, klen) == 0)
        {
            return true;
        }
        klass1 = klass1->superclass;
    }
    return false;
}

bool nn_dict_setentry(NeonObjDict* dict, NeonValue key, NeonValue value)
{
    NeonValue tempvalue;
    if(!dict->htab->get(key, &tempvalue))
    {
        /* add key if it doesn't exist. */
        dict->names->push(key);
    }
    return dict->htab->set(key, value);
}

void nn_dict_addentry(NeonObjDict* dict, NeonValue key, NeonValue value)
{
    nn_dict_setentry(dict, key, value);
}

void nn_dict_addentrycstr(NeonObjDict* dict, const char* ckey, NeonValue value)
{
    NeonObjString* os;
    os = NeonObjString::copy(dict->m_pvm, ckey);
    nn_dict_addentry(dict, NeonValue::fromObject(os), value);
}

NeonProperty* nn_dict_getentry(NeonObjDict* dict, NeonValue key)
{
    return dict->htab->getField(key);
}

static NEON_ALWAYSINLINE NeonObjString* nn_vmutil_multiplystring(NeonState* state, NeonObjString* str, double number)
{
    int i;
    int times;
    NeonPrinter pd;
    times = (int)number;
    /* 'str' * 0 == '', 'str' * -1 == '' */
    if(times <= 0)
    {
        return NeonObjString::copy(state, "", 0);
    }
    /* 'str' * 1 == 'str' */
    else if(times == 1)
    {
        return str;
    }
    NeonPrinter::makeStackString(state, &pd);
    for(i = 0; i < times; i++)
    {
        pd.put(str->data(), str->length());
    }
    return pd.takeString();
}

static NEON_ALWAYSINLINE NeonObjArray* nn_vmutil_combinearrays(NeonState* state, NeonObjArray* a, NeonObjArray* b)
{
    int i;
    NeonObjArray* list;
    list = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(list));
    for(i = 0; i < a->varray->m_count; i++)
    {
        list->varray->push(a->varray->values[i]);
    }
    for(i = 0; i < b->varray->m_count; i++)
    {
        list->varray->push(b->varray->values[i]);
    }
    state->stackPop();
    return list;
}

static NEON_ALWAYSINLINE void nn_vmutil_multiplyarray(NeonState* state, NeonObjArray* from, NeonObjArray* newlist, int times)
{
    int i;
    int j;
    (void)state;
    for(i = 0; i < times; i++)
    {
        for(j = 0; j < from->varray->m_count; j++)
        {
            newlist->varray->push(from->varray->values[j]);
        }
    }
}

static NEON_ALWAYSINLINE bool nn_vmutil_dogetrangedindexofarray(NeonState* state, NeonObjArray* list, bool willassign)
{
    int i;
    int idxlower;
    int idxupper;
    NeonValue valupper;
    NeonValue vallower;
    NeonObjArray* newlist;
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
        idxupper = list->varray->m_count;
    }
    else
    {
        idxupper = valupper.asNumber();
    }
    if(idxlower < 0 || (idxupper < 0 && ((list->varray->m_count + idxupper) < 0)) || idxlower >= list->varray->m_count)
    {
        /* always return an empty list... */
        if(!willassign)
        {
            /* +1 for the list itself */
            state->stackPop(3);
        }
        state->stackPush(NeonValue::fromObject(NeonObjArray::make(state)));
        return true;
    }
    if(idxupper < 0)
    {
        idxupper = list->varray->m_count + idxupper;
    }
    if(idxupper > list->varray->m_count)
    {
        idxupper = list->varray->m_count;
    }
    newlist = NeonObjArray::make(state);
    state->stackPush(NeonValue::fromObject(newlist));
    for(i = idxlower; i < idxupper; i++)
    {
        newlist->varray->push(list->varray->values[i]);
    }
    /* clear gc protect */
    state->stackPop();
    if(!willassign)
    {
        /* +1 for the list itself */
        state->stackPop(3);
    }
    state->stackPush(NeonValue::fromObject(newlist));
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmutil_dogetrangedindexofstring(NeonState* state, NeonObjString* string, bool willassign)
{
    int end;
    int start;
    int length;
    int idxupper;
    int idxlower;
    NeonValue valupper;
    NeonValue vallower;
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
        state->stackPush(NeonValue::fromObject(NeonObjString::copy(state, "", 0)));
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
    state->stackPush(NeonValue::fromObject(NeonObjString::copy(state, string->data() + start, end - start)));
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_getrangedindex(NeonState* state)
{
    bool isgotten;
    uint8_t willassign;
    NeonValue vfrom;
    willassign = nn_vmbits_readbyte(state);
    isgotten = true;
    vfrom = state->stackPeek(2);
    if(vfrom.isObject())
    {
        switch(vfrom.asObject()->m_objtype)
        {
            case NEON_OBJTYPE_STRING:
            {
                if(!nn_vmutil_dogetrangedindexofstring(state, vfrom.asString(), willassign))
                {
                    return false;
                }
                break;
            }
            case NEON_OBJTYPE_ARRAY:
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

static NEON_ALWAYSINLINE bool nn_vmutil_doindexgetdict(NeonState* state, NeonObjDict* dict, bool willassign)
{
    NeonValue vindex;
    NeonProperty* field;
    vindex = state->stackPeek(0);
    field = nn_dict_getentry(dict, vindex);
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
    state->stackPush(NeonValue::makeEmpty());
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmutil_doindexgetmodule(NeonState* state, NeonObjModule* module, bool willassign)
{
    NeonValue vindex;
    NeonValue result;
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
    return nn_exceptions_throwException(state, "%s is undefined in module %s", nn_value_tostring(state, vindex)->data(), module->name);
}

static NEON_ALWAYSINLINE bool nn_vmutil_doindexgetstring(NeonState* state, NeonObjString* string, bool willassign)
{
    int end;
    int start;
    int index;
    int maxlength;
    int realindex;
    NeonValue vindex;
    NeonObjRange* rng;
    (void)realindex;
    vindex = state->stackPeek(0);
    if(!vindex.isNumber())
    {
        if(vindex.isRange())
        {
            rng = nn_value_asrange(vindex);
            state->stackPop();
            state->stackPush(NeonValue::makeNumber(rng->lower));
            state->stackPush(NeonValue::makeNumber(rng->upper));
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
        state->stackPush(NeonValue::fromObject(NeonObjString::copy(state, string->data() + start, end - start)));
        return true;
    }
    state->stackPop(1);
    #if 0
        return nn_exceptions_throwException(state, "string index %d out of range of %d", realindex, maxlength);
    #else
        state->stackPush(NeonValue::makeEmpty());
        return true;
    #endif
}

static NEON_ALWAYSINLINE bool nn_vmutil_doindexgetarray(NeonState* state, NeonObjArray* list, bool willassign)
{
    int index;
    NeonValue finalval;
    NeonValue vindex;
    NeonObjRange* rng;
    vindex = state->stackPeek(0);
    if(NEON_UNLIKELY(!vindex.isNumber()))
    {
        if(vindex.isRange())
        {
            rng = nn_value_asrange(vindex);
            state->stackPop();
            state->stackPush(NeonValue::makeNumber(rng->lower));
            state->stackPush(NeonValue::makeNumber(rng->upper));
            return nn_vmutil_dogetrangedindexofarray(state, list, willassign);
        }
        state->stackPop();
        return nn_exceptions_throwException(state, "list are numerically indexed");
    }
    index = vindex.asNumber();
    if(NEON_UNLIKELY(index < 0))
    {
        index = list->varray->m_count + index;
    }
    if(index < list->varray->m_count && index >= 0)
    {
        finalval = list->varray->values[index];
    }
    else
    {
        finalval = NeonValue::makeNull();
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

static NEON_ALWAYSINLINE bool nn_vmdo_indexget(NeonState* state)
{
    bool isgotten;
    uint8_t willassign;
    NeonValue peeked;
    willassign = nn_vmbits_readbyte(state);
    isgotten = true;
    peeked = state->stackPeek(1);
    if(NEON_LIKELY(peeked.isObject()))
    {
        switch(peeked.asObject()->m_objtype)
        {
            case NEON_OBJTYPE_STRING:
            {
                if(!nn_vmutil_doindexgetstring(state, peeked.asString(), willassign))
                {
                    return false;
                }
                break;
            }
            case NEON_OBJTYPE_ARRAY:
            {
                if(!nn_vmutil_doindexgetarray(state, peeked.asArray(), willassign))
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
        nn_exceptions_throwException(state, "cannot index object of type %s", nn_value_typename(peeked));
    }
    return true;
}


static NEON_ALWAYSINLINE bool nn_vmutil_dosetindexdict(NeonState* state, NeonObjDict* dict, NeonValue index, NeonValue value)
{
    nn_dict_setentry(dict, index, value);
    /* pop the value, index and dict out */
    state->stackPop(3);
    /*
    // leave the value on the stack for consumption
    // e.g. variable = dict[index] = 10
    */
    state->stackPush(value);
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmutil_dosetindexmodule(NeonState* state, NeonObjModule* module, NeonValue index, NeonValue value)
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

static NEON_ALWAYSINLINE bool nn_vmutil_doindexsetarray(NeonState* state, NeonObjArray* list, NeonValue index, NeonValue value)
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
    ocap = list->varray->capacity;
    ocnt = list->varray->m_count;
    rawpos = index.asNumber();
    position = rawpos;
    if(rawpos < 0)
    {
        rawpos = list->varray->m_count + rawpos;
    }
    if(position < ocap && position > -(ocap))
    {
        list->varray->values[position] = value;
        if(position >= ocnt)
        {
            list->varray->m_count++;
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
        vasz = list->varray->m_count;
        if((position > vasz) || ((position == 0) && (vasz == 0)))
        {
            if(position == 0)
            {
                list->push(NeonValue::makeEmpty());
            }
            else
            {
                tmp = position + 1;
                while(tmp > vasz)
                {
                    list->push(NeonValue::makeEmpty());
                    tmp--;
                }
            }
        }
        fprintf(stderr, "setting value at position %d (array count: %d)\n", position, list->varray->m_count);
    }
    list->varray->values[position] = value;
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

static NEON_ALWAYSINLINE bool nn_vmutil_dosetindexstring(NeonState* state, NeonObjString* os, NeonValue index, NeonValue value)
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

static NEON_ALWAYSINLINE bool nn_vmdo_indexset(NeonState* state)
{
    bool isset;
    NeonValue value;
    NeonValue index;
    NeonValue target;
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
            case NEON_OBJTYPE_ARRAY:
                {
                    if(!nn_vmutil_doindexsetarray(state, target.asArray(), index, value))
                    {
                        return false;
                    }
                }
                break;
            case NEON_OBJTYPE_STRING:
                {
                    if(!nn_vmutil_dosetindexstring(state, target.asString(), index, value))
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
        return nn_exceptions_throwException(state, "type of %s is not a valid iterable", nn_value_typename(state->stackPeek(3)));
    }
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmutil_concatenate(NeonState* state)
{
    NeonValue vleft;
    NeonValue vright;
    NeonPrinter pd;
    NeonObjString* result;
    vright = state->stackPeek(0);
    vleft = state->stackPeek(1);
    NeonPrinter::makeStackString(state, &pd);
    nn_printer_printvalue(&pd, vleft, false, true);
    nn_printer_printvalue(&pd, vright, false, true);
    result = pd.takeString();
    state->stackPop(2);
    state->stackPush(NeonValue::fromObject(result));
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

static NEON_ALWAYSINLINE NeonProperty* nn_vmutil_getproperty(NeonState* state, NeonValue peeked, NeonObjString* name)
{
    NeonProperty* field;
    switch(peeked.asObject()->m_objtype)
    {
        case NEON_OBJTYPE_MODULE:
            {
                NeonObjModule* module;
                module = nn_value_asmodule(peeked);
                field = module->deftable->getFieldByObjStr(name);
                if(field != NULL)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        nn_exceptions_throwException(state, "cannot get private module property '%s'", name->data());
                        return NULL;
                    }
                    return field;
                }
                nn_exceptions_throwException(state, "%s module does not define '%s'", module->name, name->data());
                return NULL;
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                field = nn_value_asclass(peeked)->methods->getFieldByObjStr(name);
                if(field != NULL)
                {
                    if(nn_value_getmethodtype(field->value) == NEON_FUNCTYPE_STATIC)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            nn_exceptions_throwException(state, "cannot call private property '%s' of class %s", name->data(),
                                nn_value_asclass(peeked)->name->data());
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
                            nn_exceptions_throwException(state, "cannot call private property '%s' of class %s", name->data(),
                                nn_value_asclass(peeked)->name->data());
                            return NULL;
                        }
                        return field;
                    }
                }
                nn_exceptions_throwException(state, "class %s does not have a static property or method named '%s'",
                    nn_value_asclass(peeked)->name->data(), name->data());
                return NULL;
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                NeonObjInstance* instance;
                instance = peeked.asInstance();
                field = instance->properties->getFieldByObjStr(name);
                if(field != NULL)
                {
                    if(nn_util_methodisprivate(name))
                    {
                        nn_exceptions_throwException(state, "cannot call private property '%s' from instance of %s", name->data(), instance->klass->name->data());
                        return NULL;
                    }
                    return field;
                }
                if(nn_util_methodisprivate(name))
                {
                    nn_exceptions_throwException(state, "cannot bind private property '%s' to instance of %s", name->data(), instance->klass->name->data());
                    return NULL;
                }
                if(nn_vmutil_bindmethod(state, instance->klass, name))
                {
                    return field;
                }
                nn_exceptions_throwException(state, "instance of class %s does not have a property or method named '%s'",
                    peeked.asInstance()->klass->name->data(), name->data());
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
                nn_exceptions_throwException(state, "class String has no named property '%s'", name->data());
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
                nn_exceptions_throwException(state, "class Array has no named property '%s'", name->data());
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
                nn_exceptions_throwException(state, "class Range has no named property '%s'", name->data());
                return NULL;
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                field = nn_value_asdict(peeked)->htab->getFieldByObjStr(name);
                if(field == NULL)
                {
                    field = nn_class_getpropertyfield(state->classprimdict, name);
                }
                if(field != NULL)
                {
                    return field;
                }
                nn_exceptions_throwException(state, "unknown key or class Dict property '%s'", name->data());
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
                nn_exceptions_throwException(state, "class File has no named property '%s'", name->data());
                return NULL;
            }
            break;
        default:
            {
                nn_exceptions_throwException(state, "object of type %s does not carry properties", nn_value_typename(peeked));
                return NULL;
            }
            break;
    }
    return NULL;
}

static NEON_ALWAYSINLINE bool nn_vmdo_propertyget(NeonState* state)
{
    NeonValue peeked;
    NeonProperty* field;
    NeonObjString* name;
    name = nn_vmbits_readstring(state);
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
            if(field->type == NEON_PROPTYPE_FUNCTION)
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
        nn_exceptions_throwException(state, "'%s' of type %s does not have properties", nn_value_tostring(state, peeked)->data(),
            nn_value_typename(peeked));
    }
    return false;
}

static NEON_ALWAYSINLINE bool nn_vmdo_propertygetself(NeonState* state)
{
    NeonValue peeked;
    NeonObjString* name;
    NeonObjClass* klass;
    NeonObjInstance* instance;
    NeonObjModule* module;
    NeonProperty* field;
    name = nn_vmbits_readstring(state);
    peeked = state->stackPeek(0);
    if(peeked.isInstance())
    {
        instance = peeked.asInstance();
        field = instance->properties->getFieldByObjStr(name);
        if(field != NULL)
        {
            /* pop the instance... */
            state->stackPop();
            state->stackPush(field->value);
            return true;
        }
        if(nn_vmutil_bindmethod(state, instance->klass, name))
        {
            return true;
        }
        nn_vmmac_tryraise(state, false, "instance of class %s does not have a property or method named '%s'",
            peeked.asInstance()->klass->name->data(), name->data());
        return false;
    }
    else if(peeked.isClass())
    {
        klass = nn_value_asclass(peeked);
        field = klass->methods->getFieldByObjStr(name);
        if(field != NULL)
        {
            if(nn_value_getmethodtype(field->value) == NEON_FUNCTYPE_STATIC)
            {
                /* pop the class... */
                state->stackPop();
                state->stackPush(field->value);
                return true;
            }
        }
        else
        {
            field = nn_class_getstaticproperty(klass, name);
            if(field != NULL)
            {
                /* pop the class... */
                state->stackPop();
                state->stackPush(field->value);
                return true;
            }
        }
        nn_vmmac_tryraise(state, false, "class %s does not have a static property or method named '%s'", klass->name->data(), name->data());
        return false;
    }
    else if(peeked.isModule())
    {
        module = nn_value_asmodule(peeked);
        field = module->deftable->getFieldByObjStr(name);
        if(field != NULL)
        {
            /* pop the module... */
            state->stackPop();
            state->stackPush(field->value);
            return true;
        }
        nn_vmmac_tryraise(state, false, "module %s does not define '%s'", module->name, name->data());
        return false;
    }
    nn_vmmac_tryraise(state, false, "'%s' of type %s does not have properties", nn_value_tostring(state, peeked)->data(),
        nn_value_typename(peeked));
    return false;
}

static NEON_ALWAYSINLINE bool nn_vmdo_propertyset(NeonState* state)
{
    NeonValue value;
    NeonValue vtarget;
    NeonValue vpeek;
    NeonObjClass* klass;
    NeonObjString* name;
    NeonObjDict* dict;
    NeonObjInstance* instance;
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
            nn_class_defmethod(state, klass, name->data(), vpeek);
        }
        else
        {
            nn_class_defproperty(klass, name->data(), vpeek);
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
        dict = nn_value_asdict(vtarget);
        nn_dict_setentry(dict, NeonValue::fromObject(name), vpeek);
        value = state->stackPop();
        /* removing the dictionary object */
        state->stackPop();
        state->stackPush(value);
    }
    return true;
}

static NEON_ALWAYSINLINE double nn_vmutil_valtonum(NeonValue v)
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


static NEON_ALWAYSINLINE uint32_t nn_vmutil_valtouint(NeonValue v)
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

static NEON_ALWAYSINLINE long nn_vmutil_valtoint(NeonValue v)
{
    return (long)nn_vmutil_valtonum(v);
}



static NEON_ALWAYSINLINE bool nn_vmdo_dobinary(NeonState* state)
{
    bool isfail;
    long ibinright;
    long ibinleft;
    uint32_t ubinright;
    uint32_t ubinleft;
    double dbinright;
    double dbinleft;
    NeonOpCode instruction;
    NeonValue res;
    NeonValue binvalleft;
    NeonValue binvalright;
    instruction = (NeonOpCode)state->vmstate.currentinstr.code;
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
    res = NeonValue::makeEmpty();
    switch(instruction)
    {
        case NEON_OP_PRIMADD:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = NeonValue::makeNumber(dbinleft + dbinright);
            }
            break;
        case NEON_OP_PRIMSUBTRACT:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = NeonValue::makeNumber(dbinleft - dbinright);
            }
            break;
        case NEON_OP_PRIMDIVIDE:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = NeonValue::makeNumber(dbinleft / dbinright);
            }
            break;
        case NEON_OP_PRIMMULTIPLY:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = NeonValue::makeNumber(dbinleft * dbinright);
            }
            break;
        case NEON_OP_PRIMAND:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = NeonValue::makeInt(ibinleft & ibinright);
            }
            break;
        case NEON_OP_PRIMOR:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = NeonValue::makeInt(ibinleft | ibinright);
            }
            break;
        case NEON_OP_PRIMBITXOR:
            {
                ibinright = nn_vmutil_valtoint(binvalright);
                ibinleft = nn_vmutil_valtoint(binvalleft);
                res = NeonValue::makeInt(ibinleft ^ ibinright);
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
                //res = NeonValue::makeInt(ibinleft << ibinright);
                res = NeonValue::makeInt(ubinleft << ubinright);

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
                res = NeonValue::makeInt(ubinleft >> ubinright);
            }
            break;
        case NEON_OP_PRIMGREATER:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = NeonValue::makeBool(dbinleft > dbinright);
            }
            break;
        case NEON_OP_PRIMLESSTHAN:
            {
                dbinright = nn_vmutil_valtonum(binvalright);
                dbinleft = nn_vmutil_valtonum(binvalleft);
                res = NeonValue::makeBool(dbinleft < dbinright);
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

static NEON_ALWAYSINLINE bool nn_vmdo_globaldefine(NeonState* state)
{
    NeonValue val;
    NeonObjString* name;
    NeonHashTable* tab;
    name = nn_vmbits_readstring(state);
    val = state->stackPeek(0);
    if(val.isEmpty())
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    tab = state->vmstate.currentframe->closure->scriptfunc->module->deftable;
    tab->set(NeonValue::fromObject(name), val);
    state->stackPop();
    #if (defined(DEBUG_TABLE) && DEBUG_TABLE) || 0
    state->globals->printTo(state->debugwriter, "globals");
    #endif
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_globalget(NeonState* state)
{
    NeonObjString* name;
    NeonHashTable* tab;
    NeonProperty* field;
    name = nn_vmbits_readstring(state);
    tab = state->vmstate.currentframe->closure->scriptfunc->module->deftable;
    field = tab->getFieldByObjStr(name);
    if(field == NULL)
    {
        field = state->globals->getFieldByObjStr(name);
        if(field == NULL)
        {
            nn_vmmac_tryraise(state, false, "global name '%s' is not defined", name->data());
            return false;
        }
    }
    state->stackPush(field->value);
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_globalset(NeonState* state)
{
    NeonObjString* name;
    NeonHashTable* table;
    if(state->stackPeek(0).isEmpty())
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    name = nn_vmbits_readstring(state);
    table = state->vmstate.currentframe->closure->scriptfunc->module->deftable;
    if(table->set(NeonValue::fromObject(name), state->stackPeek(0)))
    {
        if(state->conf.enablestrictmode)
        {
            table->removeByKey(NeonValue::fromObject(name));
            nn_vmmac_tryraise(state, false, "global name '%s' was not declared", name->data());
            return false;
        }
    }
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_localget(NeonState* state)
{
    size_t ssp;
    uint16_t slot;
    NeonValue val;
    slot = nn_vmbits_readshort(state);
    ssp = state->vmstate.currentframe->stackslotpos;
    val = state->vmstate.stackvalues[ssp + slot];
    state->stackPush(val);
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_localset(NeonState* state)
{
    size_t ssp;
    uint16_t slot;
    NeonValue peeked;
    slot = nn_vmbits_readshort(state);
    peeked = state->stackPeek(0);
    if(peeked.isEmpty())
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    ssp = state->vmstate.currentframe->stackslotpos;
    state->vmstate.stackvalues[ssp + slot] = peeked;
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_funcargget(NeonState* state)
{
    size_t ssp;
    uint16_t slot;
    NeonValue val;
    slot = nn_vmbits_readshort(state);
    ssp = state->vmstate.currentframe->stackslotpos;
    //fprintf(stderr, "FUNCARGGET: %s\n", state->vmstate.currentframe->closure->scriptfunc->name->data());
    val = state->vmstate.stackvalues[ssp + slot];
    state->stackPush(val);
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_funcargset(NeonState* state)
{
    size_t ssp;
    uint16_t slot;
    NeonValue peeked;
    slot = nn_vmbits_readshort(state);
    peeked = state->stackPeek(0);
    if(peeked.isEmpty())
    {
        nn_vmmac_tryraise(state, false, "empty cannot be assigned");
        return false;
    }
    ssp = state->vmstate.currentframe->stackslotpos;
    state->vmstate.stackvalues[ssp + slot] = peeked;
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_makeclosure(NeonState* state)
{
    int i;
    int index;
    size_t ssp;
    uint8_t islocal;
    NeonValue* upvals;
    NeonObjFuncScript* function;
    NeonObjFuncClosure* closure;
    function = nn_value_asfuncscript(nn_vmbits_readconst(state));
    closure = nn_object_makefuncclosure(state, function);
    state->stackPush(NeonValue::fromObject(closure));
    for(i = 0; i < closure->upvalcount; i++)
    {
        islocal = nn_vmbits_readbyte(state);
        index = nn_vmbits_readshort(state);
        if(islocal)
        {
            ssp = state->vmstate.currentframe->stackslotpos;
            upvals = &state->vmstate.stackvalues[ssp + index];
            closure->upvalues[i] = nn_vmutil_upvaluescapture(state, upvals, index);

        }
        else
        {
            closure->upvalues[i] = state->vmstate.currentframe->closure->upvalues[index];
        }
    }
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_makearray(NeonState* state)
{
    int i;
    int count;
    NeonObjArray* array;
    count = nn_vmbits_readshort(state);
    array = NeonObjArray::make(state);
    state->vmstate.stackvalues[state->vmstate.stackidx + (-count - 1)] = NeonValue::fromObject(array);
    for(i = count - 1; i >= 0; i--)
    {
        array->push(state->stackPeek(i));
    }
    state->stackPop(count);
    return true;
}

static NEON_ALWAYSINLINE bool nn_vmdo_makedict(NeonState* state)
{
    int i;
    int count;
    int realcount;
    NeonValue name;
    NeonValue value;
    NeonObjDict* dict;
    /* 1 for key, 1 for value */
    realcount = nn_vmbits_readshort(state);
    count = realcount * 2;
    dict = nn_object_makedict(state);
    state->vmstate.stackvalues[state->vmstate.stackidx + (-count - 1)] = NeonValue::fromObject(dict);
    for(i = 0; i < count; i += 2)
    {
        name = state->vmstate.stackvalues[state->vmstate.stackidx + (-count + i)];
        if(!name.isString() && !name.isNumber() && !name.isBool())
        {
            nn_vmmac_tryraise(state, false, "dictionary key must be one of string, number or boolean");
            return false;
        }
        value = state->vmstate.stackvalues[state->vmstate.stackidx + (-count + i + 1)];
        nn_dict_setentry(dict, name, value);
    }
    state->stackPop(count);
    return true;
}

#define BINARY_MOD_OP(state, type, op) \
    do \
    { \
        double dbinright; \
        double dbinleft; \
        NeonValue binvalright; \
        NeonValue binvalleft; \
        binvalright = state->stackPeek(0); \
        binvalleft = state->stackPeek(1);\
        if((!binvalright.isNumber() && !binvalright.isBool()) \
        || (!binvalleft.isNumber() && !binvalleft.isBool())) \
        { \
            nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "unsupported operand %s for %s and %s", #op, nn_value_typename(binvalleft), nn_value_typename(binvalright)); \
            break; \
        } \
        binvalright = state->stackPop(); \
        dbinright = binvalright.isBool() ? (nn_value_asbool(binvalright) ? 1 : 0) : binvalright.asNumber(); \
        binvalleft = state->stackPop(); \
        dbinleft = binvalleft.isBool() ? (nn_value_asbool(binvalleft) ? 1 : 0) : binvalleft.asNumber(); \
        state->stackPush(type(op(dbinleft, dbinright))); \
    } while(false)


NeonStatus nn_vm_runvm(NeonState* state, int exitframe, NeonValue* rv)
{
    int iterpos;
    int printpos;
    int ofs;
    /*
    * this variable is a NOP; it only exists to ensure that functions outside of the
    * switch tree are not calling nn_vmmac_exitvm(), as its behavior could be undefined.
    */
    bool you_are_calling_exit_vm_outside_of_runvm;
    NeonValue* dbgslot;
    NeonInstruction currinstr;
    you_are_calling_exit_vm_outside_of_runvm = false;
    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
    for(;;)
    {
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
        if(state->conf.shoulddumpstack)
        {
            ofs = (int)(state->vmstate.currentframe->inscode - state->vmstate.currentframe->closure->scriptfunc->blob.instrucs);
            nn_dbg_printinstructionat(state->debugwriter, &state->vmstate.currentframe->closure->scriptfunc->blob, ofs);
            fprintf(stderr, "stack (before)=[\n");
            iterpos = 0;
            for(dbgslot = state->vmstate.stackvalues; dbgslot < &state->vmstate.stackvalues[state->vmstate.stackidx]; dbgslot++)
            {
                printpos = iterpos + 1;
                iterpos++;
                fprintf(stderr, "  [%s%d%s] ", nn_util_color(NEON_COLOR_YELLOW), printpos, nn_util_color(NEON_COLOR_RESET));
                state->debugwriter->putformat("%s", nn_util_color(NEON_COLOR_YELLOW));
                nn_printer_printvalue(state->debugwriter, *dbgslot, true, false);
                state->debugwriter->putformat("%s", nn_util_color(NEON_COLOR_RESET));
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "]\n");
        }
        currinstr = nn_vmbits_readinstruction(state);
        state->vmstate.currentinstr = currinstr;
        //fprintf(stderr, "now executing at line %d\n", state->vmstate.currentinstr.srcline);
        switch(currinstr.code)
        {
            case NEON_OP_RETURN:
                {
                    size_t ssp;
                    NeonValue result;
                    result = state->stackPop();
                    if(rv != NULL)
                    {
                        *rv = result;
                    }
                    ssp = state->vmstate.currentframe->stackslotpos;
                    nn_vmutil_upvaluesclose(state, &state->vmstate.stackvalues[ssp]);
                    state->vmstate.framecount--;
                    if(state->vmstate.framecount == 0)
                    {
                        state->stackPop();
                        return NEON_STATUS_OK;
                    }
                    ssp = state->vmstate.currentframe->stackslotpos;
                    state->vmstate.stackidx = ssp;
                    state->stackPush(result);
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                    if(state->vmstate.framecount == (size_t)exitframe)
                    {
                        return NEON_STATUS_OK;
                    }
                }
                break;
            case NEON_OP_PUSHCONSTANT:
                {
                    NeonValue constant;
                    constant = nn_vmbits_readconst(state);
                    state->stackPush(constant);
                }
                break;
            case NEON_OP_PRIMADD:
                {
                    NeonValue valright;
                    NeonValue valleft;
                    NeonValue result;
                    valright = state->stackPeek(0);
                    valleft = state->stackPeek(1);
                    if(valright.isString() || valleft.isString())
                    {
                        if(!nn_vmutil_concatenate(state))
                        {
                            nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "unsupported operand + for %s and %s", nn_value_typename(valleft), nn_value_typename(valright));
                            break;
                        }
                    }
                    else if(valleft.isArray() && valright.isArray())
                    {
                        result = NeonValue::fromObject(nn_vmutil_combinearrays(state, valleft.asArray(), valright.asArray()));
                        state->stackPop(2);
                        state->stackPush(result);
                    }
                    else
                    {
                        nn_vmdo_dobinary(state);
                    }
                }
                break;
            case NEON_OP_PRIMSUBTRACT:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMMULTIPLY:
                {
                    int intnum;
                    double dbnum;
                    NeonValue peekleft;
                    NeonValue peekright;
                    NeonValue result;
                    NeonObjString* string;
                    NeonObjArray* list;
                    NeonObjArray* newlist;
                    peekright = state->stackPeek(0);
                    peekleft = state->stackPeek(1);
                    if(peekleft.isString() && peekright.isNumber())
                    {
                        dbnum = peekright.asNumber();
                        string = state->stackPeek(1).asString();
                        result = NeonValue::fromObject(nn_vmutil_multiplystring(state, string, dbnum));
                        state->stackPop(2);
                        state->stackPush(result);
                        break;
                    }
                    else if(peekleft.isArray() && peekright.isNumber())
                    {
                        intnum = (int)peekright.asNumber();
                        state->stackPop();
                        list = peekleft.asArray();
                        newlist = NeonObjArray::make(state);
                        state->stackPush(NeonValue::fromObject(newlist));
                        nn_vmutil_multiplyarray(state, list, newlist, intnum);
                        state->stackPop(2);
                        state->stackPush(NeonValue::fromObject(newlist));
                        break;
                    }
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMDIVIDE:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMMODULO:
                {
                    BINARY_MOD_OP(state, NeonValue::makeNumber, nn_vmutil_modulo);
                }
                break;
            case NEON_OP_PRIMPOW:
                {
                    BINARY_MOD_OP(state, NeonValue::makeNumber, pow);
                }
                break;
            case NEON_OP_PRIMFLOORDIVIDE:
                {
                    BINARY_MOD_OP(state, NeonValue::makeNumber, nn_vmutil_floordiv);
                }
                break;
            case NEON_OP_PRIMNEGATE:
                {
                    NeonValue peeked;
                    peeked = state->stackPeek(0);
                    if(!peeked.isNumber())
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "operator - not defined for object of type %s", nn_value_typename(peeked));
                        break;
                    }
                    state->stackPush(NeonValue::makeNumber(-state->stackPop().asNumber()));
                }
                break;
            case NEON_OP_PRIMBITNOT:
            {
                NeonValue peeked;
                peeked = state->stackPeek(0);
                if(!peeked.isNumber())
                {
                    nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "operator ~ not defined for object of type %s", nn_value_typename(peeked));
                    break;
                }
                state->stackPush(NeonValue::makeInt(~((int)state->stackPop().asNumber())));
                break;
            }
            case NEON_OP_PRIMAND:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMOR:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMBITXOR:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMSHIFTLEFT:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMSHIFTRIGHT:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PUSHONE:
                {
                    state->stackPush(NeonValue::makeNumber(1));
                }
                break;
            /* comparisons */
            case NEON_OP_EQUAL:
                {
                    NeonValue a;
                    NeonValue b;
                    b = state->stackPop();
                    a = state->stackPop();
                    state->stackPush(NeonValue::makeBool(nn_value_compare(state, a, b)));
                }
                break;
            case NEON_OP_PRIMGREATER:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMLESSTHAN:
                {
                    nn_vmdo_dobinary(state);
                }
                break;
            case NEON_OP_PRIMNOT:
                {
                    state->stackPush(NeonValue::makeBool(nn_value_isfalse(state->stackPop())));
                }
                break;
            case NEON_OP_PUSHNULL:
                {
                    state->stackPush(NeonValue::makeNull());
                }
                break;
            case NEON_OP_PUSHEMPTY:
                {
                    state->stackPush(NeonValue::makeEmpty());
                }
                break;
            case NEON_OP_PUSHTRUE:
                {
                    state->stackPush(NeonValue::makeBool(true));
                }
                break;
            case NEON_OP_PUSHFALSE:
                {
                    state->stackPush(NeonValue::makeBool(false));
                }
                break;

            case NEON_OP_JUMPNOW:
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    state->vmstate.currentframe->inscode += offset;
                }
                break;
            case NEON_OP_JUMPIFFALSE:
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    if(nn_value_isfalse(state->stackPeek(0)))
                    {
                        state->vmstate.currentframe->inscode += offset;
                    }
                }
                break;
            case NEON_OP_LOOP:
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    state->vmstate.currentframe->inscode -= offset;
                }
                break;
            case NEON_OP_ECHO:
                {
                    NeonValue val;
                    val = state->stackPeek(0);
                    nn_printer_printvalue(state->stdoutwriter, val, state->isrepl, true);
                    if(!val.isEmpty())
                    {
                        state->stdoutwriter->put("\n");
                    }
                    state->stackPop();
                }
                break;
            case NEON_OP_STRINGIFY:
                {
                    NeonValue peeked;
                    NeonObjString* value;
                    peeked = state->stackPeek(0);
                    if(!peeked.isString() && !peeked.isNull())
                    {
                        value = nn_value_tostring(state, state->stackPop());
                        if(value->length() != 0)
                        {
                            state->stackPush(NeonValue::fromObject(value));
                        }
                        else
                        {
                            state->stackPush(NeonValue::makeNull());
                        }
                    }
                }
                break;
            case NEON_OP_DUPONE:
                {
                    state->stackPush(state->stackPeek(0));
                }
                break;
            case NEON_OP_POPONE:
                {
                    state->stackPop();
                }
                break;
            case NEON_OP_POPN:
                {
                    state->stackPop(nn_vmbits_readshort(state));
                }
                break;
            case NEON_OP_UPVALUECLOSE:
                {
                    nn_vmutil_upvaluesclose(state, &state->vmstate.stackvalues[state->vmstate.stackidx - 1]);
                    state->stackPop();
                }
                break;
            case NEON_OP_GLOBALDEFINE:
                {
                    if(!nn_vmdo_globaldefine(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_GLOBALGET:
                {
                    if(!nn_vmdo_globalget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_GLOBALSET:
                {
                    if(!nn_vmdo_globalset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_LOCALGET:
                {
                    if(!nn_vmdo_localget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_LOCALSET:
                {
                    if(!nn_vmdo_localset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_FUNCARGGET:
                {
                    if(!nn_vmdo_funcargget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_FUNCARGSET:
                {
                    if(!nn_vmdo_funcargset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;

            case NEON_OP_PROPERTYGET:
                {
                    if(!nn_vmdo_propertyget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_PROPERTYSET:
                {
                    if(!nn_vmdo_propertyset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_PROPERTYGETSELF:
                {
                    if(!nn_vmdo_propertygetself(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_MAKECLOSURE:
                {
                    if(!nn_vmdo_makeclosure(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_UPVALUEGET:
                {
                    int index;
                    NeonObjFuncClosure* closure;
                    index = nn_vmbits_readshort(state);
                    closure = state->vmstate.currentframe->closure;
                    if(index < closure->upvalcount)
                    {
                        state->stackPush(closure->upvalues[index]->location);
                    }
                    else
                    {
                        state->stackPush(NeonValue::makeEmpty());
                    }
                }
                break;
            case NEON_OP_UPVALUESET:
                {
                    int index;
                    index = nn_vmbits_readshort(state);
                    if(state->stackPeek(0).isEmpty())
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "empty cannot be assigned");
                        break;
                    }
                    state->vmstate.currentframe->closure->upvalues[index]->location = state->stackPeek(0);
                }
                break;
            case NEON_OP_CALLFUNCTION:
                {
                    int argcount;
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vm_callvalue(state, state->stackPeek(argcount), NeonValue::makeEmpty(), argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                break;
            case NEON_OP_CALLMETHOD:
                {
                    int argcount;
                    NeonObjString* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vmutil_invokemethod(state, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                break;
            case NEON_OP_CLASSINVOKETHIS:
                {
                    int argcount;
                    NeonObjString* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vmutil_invokemethodself(state, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                break;
            case NEON_OP_MAKECLASS:
                {
                    bool haveval;
                    NeonValue pushme;
                    NeonObjString* name;
                    NeonObjClass* klass;
                    NeonProperty* field;
                    haveval = false;
                    name = nn_vmbits_readstring(state);
                    field = state->vmstate.currentframe->closure->scriptfunc->module->deftable->getFieldByObjStr(name);
                    if(field != NULL)
                    {
                        if(field->value.isClass())
                        {
                            haveval = true;
                            pushme = field->value;
                        }
                    }
                    field = state->globals->getFieldByObjStr(name);
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
                        klass = nn_object_makeclass(state, name);
                        pushme = NeonValue::fromObject(klass);
                    }
                    state->stackPush(pushme);
                }
                break;
            case NEON_OP_MAKEMETHOD:
                {
                    NeonObjString* name;
                    name = nn_vmbits_readstring(state);
                    nn_vmutil_definemethod(state, name);
                }
                break;
            case NEON_OP_CLASSPROPERTYDEFINE:
                {
                    int isstatic;
                    NeonObjString* name;
                    name = nn_vmbits_readstring(state);
                    isstatic = nn_vmbits_readbyte(state);
                    nn_vmutil_defineproperty(state, name, isstatic == 1);
                }
                break;
            case NEON_OP_CLASSINHERIT:
                {
                    NeonObjClass* superclass;
                    NeonObjClass* subclass;
                    if(!state->stackPeek(1).isClass())
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "cannot inherit from non-class object");
                        break;
                    }
                    superclass = nn_value_asclass(state->stackPeek(1));
                    subclass = nn_value_asclass(state->stackPeek(0));
                    nn_class_inheritfrom(subclass, superclass);
                    /* pop the subclass */
                    state->stackPop();
                }
                break;
            case NEON_OP_CLASSGETSUPER:
                {
                    NeonObjClass* klass;
                    NeonObjString* name;
                    name = nn_vmbits_readstring(state);
                    klass = nn_value_asclass(state->stackPeek(0));
                    if(!nn_vmutil_bindmethod(state, klass->superclass, name))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "class %s does not define a function %s", klass->name->data(), name->data());
                    }
                }
                break;
            case NEON_OP_CLASSINVOKESUPER:
                {
                    int argcount;
                    NeonObjClass* klass;
                    NeonObjString* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    klass = nn_value_asclass(state->stackPop());
                    if(!nn_vmutil_invokemethodfromclass(state, klass, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                break;
            case NEON_OP_CLASSINVOKESUPERSELF:
                {
                    int argcount;
                    NeonObjClass* klass;
                    argcount = nn_vmbits_readbyte(state);
                    klass = nn_value_asclass(state->stackPop());
                    if(!nn_vmutil_invokemethodfromclass(state, klass, state->constructorname, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                break;
            case NEON_OP_MAKEARRAY:
                {
                    if(!nn_vmdo_makearray(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_MAKERANGE:
                {
                    double lower;
                    double upper;
                    NeonValue vupper;
                    NeonValue vlower;
                    vupper = state->stackPeek(0);
                    vlower = state->stackPeek(1);
                    if(!vupper.isNumber() || !vlower.isNumber())
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "invalid range boundaries");
                        break;
                    }
                    lower = vlower.asNumber();
                    upper = vupper.asNumber();
                    state->stackPop(2);
                    state->stackPush(NeonValue::fromObject(nn_object_makerange(state, lower, upper)));
                }
                break;
            case NEON_OP_MAKEDICT:
                {
                    if(!nn_vmdo_makedict(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_INDEXGETRANGED:
                {
                    if(!nn_vmdo_getrangedindex(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_INDEXGET:
                {
                    if(!nn_vmdo_indexget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_INDEXSET:
                {
                    if(!nn_vmdo_indexset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                break;
            case NEON_OP_IMPORTIMPORT:
                {
                    NeonValue res;
                    NeonObjString* name;
                    NeonObjModule* mod;
                    name = state->stackPeek(0).asString();
                    fprintf(stderr, "IMPORTIMPORT: name='%s'\n", name->data());
                    mod = nn_import_loadmodulescript(state, state->topmodule, name);
                    fprintf(stderr, "IMPORTIMPORT: mod='%p'\n", mod);
                    if(mod == NULL)
                    {
                        res = NeonValue::makeNull();
                    }
                    else
                    {
                        res = NeonValue::fromObject(mod);
                    }
                    state->stackPush(res);
                }
                break;
            case NEON_OP_TYPEOF:
                {
                    NeonValue res;
                    NeonValue thing;
                    const char* result;
                    thing = state->stackPop();
                    result = nn_value_typename(thing);
                    res = NeonValue::fromObject(NeonObjString::copy(state, result));
                    state->stackPush(res);
                }
                break;
            case NEON_OP_ASSERT:
                {
                    NeonValue message;
                    NeonValue expression;
                    message = state->stackPop();
                    expression = state->stackPop();
                    if(nn_value_isfalse(expression))
                    {
                        if(!message.isNull())
                        {
                            nn_exceptions_throwclass(state, state->exceptions.asserterror, nn_value_tostring(state, message)->data());
                        }
                        else
                        {
                            nn_exceptions_throwclass(state, state->exceptions.asserterror, "assertion failed");
                        }
                    }
                }
                break;
            case NEON_OP_EXTHROW:
                {
                    bool isok;
                    NeonValue peeked;
                    NeonValue stacktrace;
                    NeonObjInstance* instance;
                    peeked = state->stackPeek(0);
                    isok = (
                        peeked.isInstance() ||
                        nn_util_isinstanceof(peeked.asInstance()->klass, state->exceptions.stdexception)
                    );
                    if(!isok)
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "instance of Exception expected");
                        break;
                    }
                    stacktrace = nn_exceptions_getstacktrace(state);
                    instance = peeked.asInstance();
                    nn_instance_defproperty(instance, "stacktrace", stacktrace);
                    if(state->exceptionPropagate())
                    {
                        state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                        break;
                    }
                    nn_vmmac_exitvm(state);
                }
            case NEON_OP_EXTRY:
                {
                    uint16_t addr;
                    uint16_t finaddr;
                    NeonValue value;
                    NeonObjString* type;
                    type = nn_vmbits_readstring(state);
                    addr = nn_vmbits_readshort(state);
                    finaddr = nn_vmbits_readshort(state);
                    if(addr != 0)
                    {
                        if(!state->globals->get(NeonValue::fromObject(type), &value) || !value.isClass())
                        {
                            if(!state->vmstate.currentframe->closure->scriptfunc->module->deftable->get(NeonValue::fromObject(type), &value) || !value.isClass())
                            {
                                nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "object of type '%s' is not an exception", type->data());
                                break;
                            }
                        }
                        state->exceptionPushHandler(nn_value_asclass(value), addr, finaddr);
                    }
                    else
                    {
                        state->exceptionPushHandler(NULL, addr, finaddr);
                    }
                }
                break;
            case NEON_OP_EXPOPTRY:
                {
                    state->vmstate.currentframe->handlercount--;
                }
                break;
            case NEON_OP_EXPUBLISHTRY:
                {
                    state->vmstate.currentframe->handlercount--;
                    if(state->exceptionPropagate())
                    {
                        state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                        break;
                    }
                    nn_vmmac_exitvm(state);
                }
                break;
            case NEON_OP_SWITCH:
                {
                    NeonValue expr;
                    NeonValue value;
                    NeonObjSwitch* sw;
                    sw = nn_value_asswitch(nn_vmbits_readconst(state));
                    expr = state->stackPeek(0);
                    if(sw->table->get(expr, &value))
                    {
                        state->vmstate.currentframe->inscode += (int)value.asNumber();
                    }
                    else if(sw->defaultjump != -1)
                    {
                        state->vmstate.currentframe->inscode += sw->defaultjump;
                    }
                    else
                    {
                        state->vmstate.currentframe->inscode += sw->exitjump;
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

int nn_nestcall_prepare(NeonState* state, NeonValue callable, NeonValue mthobj, NeonObjArray* callarr)
{
    int arity;
    NeonObjFuncClosure* closure;
    (void)state;
    arity = 0;
    if(callable.isFuncClosure())
    {
        closure = nn_value_asfuncclosure(callable);
        arity = closure->scriptfunc->arity;
    }
    else if(callable.isFuncScript())
    {
        arity = nn_value_asfuncscript(callable)->arity;
    }
    else if(callable.isFuncNative())
    {
        //arity = nn_value_asfuncnative(callable);
    }
    if(arity > 0)
    {
        callarr->push(NeonValue::makeNull());
        if(arity > 1)
        {
            callarr->push(NeonValue::makeNull());
            if(arity > 2)
            {
                callarr->push(mthobj);
            }
        }
    }
    return arity;
}

/* helper function to access call outside the state file. */
bool nn_nestcall_callfunction(NeonState* state, NeonValue callable, NeonValue thisval, NeonObjArray* args, NeonValue* dest)
{
    int i;
    int argc;
    size_t pidx;
    NeonStatus status;
    pidx = state->vmstate.stackidx;
    /* set the closure before the args */
    state->stackPush(callable);
    argc = 0;
    if(args && (argc = args->varray->m_count))
    {
        for(i = 0; i < args->varray->m_count; i++)
        {
            state->stackPush(args->varray->values[i]);
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
    state->stackPop(argc + 1);
    state->vmstate.stackidx = pidx;
    return true;
}

NeonObjFuncClosure* nn_state_compilesource(NeonState* state, NeonObjModule* module, bool fromeval, const char* source)
{
    NeonBlob blob;
    NeonObjFuncScript* function;
    NeonObjFuncClosure* closure;
    nn_blob_init(state, &blob);
    function = nn_astparser_compilesource(state, module, source, &blob, false, fromeval);
    if(function == NULL)
    {
        nn_blob_destroy(state, &blob);
        return NULL;
    }
    if(!fromeval)
    {
        state->stackPush(NeonValue::fromObject(function));
    }
    else
    {
        function->name = NeonObjString::copy(state, "(evaledcode)");
    }
    closure = nn_object_makefuncclosure(state, function);
    if(!fromeval)
    {
        state->stackPop();
        state->stackPush(NeonValue::fromObject(closure));
    }
    nn_blob_destroy(state, &blob);
    return closure;
}

NeonStatus nn_state_execsource(NeonState* state, NeonObjModule* module, const char* source, NeonValue* dest)
{
    NeonStatus status;
    NeonObjFuncClosure* closure;
    nn_module_setfilefield(state, module);
    closure = nn_state_compilesource(state, module, false, source);
    if(closure == NULL)
    {
        return NEON_STATUS_FAILCOMPILE;
    }
    if(state->conf.exitafterbytecode)
    {
        return NEON_STATUS_OK;
    }
    nn_vm_callclosure(state, closure, NeonValue::makeNull(), 0);
    status = nn_vm_runvm(state, 0, dest);
    return status;
}

NeonValue nn_state_evalsource(NeonState* state, const char* source)
{
    bool ok;
    int argc;
    NeonValue callme;
    NeonValue retval;
    NeonObjFuncClosure* closure;
    NeonObjArray* args;
    (void)argc;
    closure = nn_state_compilesource(state, state->topmodule, true, source);
    callme = NeonValue::fromObject(closure);
    args = NeonObjArray::make(state);
    argc = nn_nestcall_prepare(state, callme, NeonValue::makeNull(), args);
    ok = nn_nestcall_callfunction(state, callme, NeonValue::makeNull(), args, &retval);
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
static bool nn_cli_repl(NeonState* state)
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
    NeonValue dest;
    state->isrepl = true;
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
            nn_state_execsource(state, state->topmodule, source.data, &dest);
            fflush(stdout);
            continuerepl = false;
        }
    }
    return true;
}
#endif

static bool nn_cli_runfile(NeonState* state, const char* file)
{
    size_t fsz;
    char* rp;
    char* source;
    const char* oldfile;
    NeonStatus result;
    source = nn_util_readfile(state, file, &fsz);
    if(source == NULL)
    {
        oldfile = file;
        source = nn_util_readfile(state, file, &fsz);
        if(source == NULL)
        {
            fprintf(stderr, "failed to read from '%s': %s\n", oldfile, strerror(errno));
            return false;
        }
    }
    state->rootphysfile = (char*)file;
    rp = osfn_realpath(file, NULL);
    state->topmodule->physicalpath = NeonObjString::copy(state, rp);
    free(rp);
    result = nn_state_execsource(state, state->topmodule, source, NULL);
    free(source);
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

static bool nn_cli_runcode(NeonState* state, char* source)
{
    NeonStatus result;
    state->rootphysfile = NULL;
    result = nn_state_execsource(state, state->topmodule, source, NULL);
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

void nn_cli_parseenv(NeonState* state, char** envp)
{
    enum { kMaxKeyLen = 40 };
    int i;
    int len;
    int pos;
    char* raw;
    char* valbuf;
    char keybuf[kMaxKeyLen];
    NeonObjString* oskey;
    NeonObjString* osval;
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
            oskey = NeonObjString::copy(state, keybuf);
            osval = NeonObjString::copy(state, valbuf);
            nn_dict_setentry(state->envdict, NeonValue::fromObject(oskey), NeonValue::fromObject(osval));
        }
    }
}

void nn_cli_printtypesizes()
{
    #define ptyp(t) \
        fprintf(stderr, "%d\t%s\n", (int)sizeof(t), #t);
    ptyp(NeonPrinter);
    ptyp(NeonValue);
    ptyp(NeonObject);
    ptyp(NeonPropGetSet);
    ptyp(NeonProperty);
    ptyp(NeonValArray);
    ptyp(NeonBlob);
    ptyp(NeonHashTable::Entry);
    ptyp(NeonHashTable);
    ptyp(NeonObjString);
    ptyp(NeonObjUpvalue);
    ptyp(NeonObjModule);
    ptyp(NeonObjFuncScript);
    ptyp(NeonObjFuncClosure);
    ptyp(NeonObjClass);
    ptyp(NeonObjInstance);
    ptyp(NeonObjFuncBound);
    ptyp(NeonObjFuncNative);
    ptyp(NeonObjArray);
    ptyp(NeonObjRange);
    ptyp(NeonObjDict);
    ptyp(NeonObjFile);
    ptyp(NeonObjSwitch);
    ptyp(NeonObjUserdata);
    ptyp(NeonState::ExceptionFrame);
    ptyp(NeonState::CallFrame);
    ptyp(NeonState);
    ptyp(NeonAstToken);
    ptyp(NeonAstLexer);
    ptyp(NeonAstLocal);
    ptyp(NeonAstUpvalue);
    ptyp(NeonAstFuncCompiler);
    ptyp(NeonAstClassCompiler);
    ptyp(NeonAstParser);
    ptyp(NeonAstRule);
    ptyp(NeonRegFunc);
    ptyp(NeonRegField);
    ptyp(NeonRegClass);
    ptyp(NeonRegModule);
    ptyp(NeonInstruction)
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
    NeonState* state;
    ok = true;
    wasusage = false;
    quitafterinit = false;
    source = NULL;
    nextgcstart = NEON_CFG_DEFAULTGCSTART;
    state = new NeonState();
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
        else if(co == 'A')
        {
            state->conf.enableastdebug = true;
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
        fprintf(stderr, "arg=\"%s\" nargc=%d\n", arg, nargc);
        nargv[nargc] = arg;
        nargc++;
    }
    {
        NeonObjString* os;
        state->cliargv = NeonObjArray::make(state);
        for(i=0; i<nargc; i++)
        {
            os = NeonObjString::copy(state, nargv[i]);
            state->cliargv->push(NeonValue::fromObject(os));
        }
        state->globals->setCStr("ARGV", NeonValue::fromObject(state->cliargv));
    }
    state->gcstate.nextgc = nextgcstart;
    nn_import_loadbuiltinmodules(state);
    if(source != NULL)
    {
        ok = nn_cli_runcode(state, source);
    }
    else if(nargc > 0)
    {
        filename = state->cliargv->varray->values[0].asString()->data();
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


