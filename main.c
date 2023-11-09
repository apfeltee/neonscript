

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include "strbuf.h"

// In the book, we show them defined, but for working on them locally,
// we don't want them to be.
#define DEBUG_PRINT_CODE 0
#undef DEBUG_STRESS_GC
#undef DEBUG_LOG_GC

#define NEON_MAX_COMPUPVALS (128 + 1)
#define NEON_MAX_COMPLOCALS (128 + 1)

/*
* NB. (1024*5) will run mob.js (man-or-boy test)
* stack resizing is buggy, for some reason
*/
#define NEON_MAX_VMFRAMES (1024 * 1)
#define NEON_MAX_VMSTACK (NEON_MAX_VMFRAMES * 1)

#define NEON_MAX_GCHEAPGROWFACTOR 2

#define NEON_MAX_TABLELOAD 0.75

#define NEON_NEXTCAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity)*2)

#if defined(__GNUC__)
    #define NEON_ATTR_PRINTFLIKE(fmtarg, firstvararg) __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#else
    #define NEON_ATTR_PRINTFLIKE(a, b)
#endif

// NEON_OP_FUNCARG - explicitly for function arguments (and default values)?

enum NeonOpCode
{
    NEON_OP_PUSHCONST,
    NEON_OP_PUSHNIL,
    NEON_OP_PUSHTRUE,
    NEON_OP_PUSHFALSE,
    NEON_OP_PUSHONE,
    NEON_OP_POPONE,
    NEON_OP_POPN,
    NEON_OP_DUP,
    NEON_OP_TYPEOF,
    NEON_OP_LOCALGET,
    NEON_OP_LOCALSET,
    NEON_OP_GLOBALGET,
    NEON_OP_GLOBALDEFINE,
    NEON_OP_GLOBALSET,
    NEON_OP_UPVALGET,
    NEON_OP_UPVALSET,
    NEON_OP_PROPERTYGET,
    NEON_OP_PROPERTYSET,
    NEON_OP_INSTGETSUPER,
    NEON_OP_EQUAL,
    NEON_OP_PRIMGREATER,
    NEON_OP_PRIMLESS,
    NEON_OP_PRIMADD,
    NEON_OP_PRIMSUBTRACT,
    NEON_OP_PRIMMULTIPLY,
    NEON_OP_PRIMDIVIDE,
    NEON_OP_PRIMMODULO,
    NEON_OP_PRIMSHIFTLEFT,
    NEON_OP_PRIMSHIFTRIGHT,
    NEON_OP_PRIMBINAND,
    NEON_OP_PRIMBINOR,
    NEON_OP_PRIMBINXOR,
    NEON_OP_PRIMBINNOT,
    NEON_OP_PRIMNOT,
    NEON_OP_PRIMNEGATE,
    NEON_OP_DEBUGPRINT,
    NEON_OP_GLOBALSTMT,
    NEON_OP_JUMPNOW,
    NEON_OP_JUMPIFFALSE,
    NEON_OP_LOOP,
    NEON_OP_CALL,
    NEON_OP_INSTTHISINVOKE,
    NEON_OP_INSTSUPERINVOKE,
    NEON_OP_INSTTHISPROPERTYGET,
    NEON_OP_CLOSURE,
    NEON_OP_UPVALCLOSE,
    NEON_OP_RETURN,
    NEON_OP_CLASS,
    NEON_OP_INHERIT,
    NEON_OP_METHOD,
    NEON_OP_MAKEARRAY,
    NEON_OP_MAKEMAP,
    NEON_OP_INDEXGET,
    NEON_OP_INDEXSET,
    NEON_OP_RESTOREFRAME,
    NEON_OP_HALTVM,

    /* *must* be last */
    NEON_OP_PSEUDOBREAK,

};

enum NeonValType
{
    NEON_VAL_NIL,// [user-types]
    NEON_VAL_BOOL,
    NEON_VAL_NUMBER,
    NEON_VAL_OBJ
};

enum NeonObjType
{
    NEON_OBJ_BOUNDMETHOD,
    NEON_OBJ_CLASS,
    NEON_OBJ_CLOSURE,
    NEON_OBJ_FUNCTION,
    NEON_OBJ_INSTANCE,
    NEON_OBJ_NATIVE,
    NEON_OBJ_STRING,
    NEON_OBJ_UPVALUE,
    NEON_OBJ_ARRAY,
    NEON_OBJ_MAP,
    NEON_OBJ_USERDATA,
};

enum NeonStatusCode
{
    NEON_STATUS_OK,
    NEON_STATUS_HALT,
    NEON_STATUS_SYNTAXERROR,
    NEON_STATUS_RUNTIMEERROR
};

enum NeonAstTokType
{
    // Single-character tokens.
    NEON_TOK_PARENOPEN,
    NEON_TOK_PARENCLOSE,
    NEON_TOK_BRACEOPEN,
    NEON_TOK_BRACECLOSE,
    NEON_TOK_BRACKETOPEN,
    NEON_TOK_BRACKETCLOSE,
    NEON_TOK_COMMA,
    NEON_TOK_DOT,
    NEON_TOK_MINUS,
    NEON_TOK_PLUS,
    NEON_TOK_SEMICOLON,
    NEON_TOK_NEWLINE,
    NEON_TOK_DIVIDE,
    NEON_TOK_MULTIPLY,
    NEON_TOK_MODULO,
    NEON_TOK_BINAND,
    NEON_TOK_BINOR,
    NEON_TOK_BINXOR,
    NEON_TOK_TILDE,
    // One or two character tokens.
    NEON_TOK_SHIFTLEFT,
    NEON_TOK_SHIFTRIGHT,
    NEON_TOK_EXCLAM,
    NEON_TOK_COMPNOTEQUAL,
    NEON_TOK_ASSIGN,
    NEON_TOK_COMPEQUAL,
    NEON_TOK_COMPGREATERTHAN,
    NEON_TOK_COMPGREATEREQUAL,
    NEON_TOK_COMPLESSTHAN,
    NEON_TOK_COMPLESSEQUAL,
    NEON_TOK_INCREMENT,
    NEON_TOK_DECREMENT,
    NEON_TOK_ASSIGNPLUS,
    NEON_TOK_ASSIGNMINUS,
    NEON_TOK_ASSIGNMULT,
    NEON_TOK_ASSIGNDIV,
    NEON_TOK_ASSIGNMODULO,

    // Literals.
    NEON_TOK_IDENTIFIER,
    NEON_TOK_STRING,
    NEON_TOK_NUMBER,
    NEON_TOK_OCTNUMBER,
    NEON_TOK_BINNUMBER,
    NEON_TOK_HEXNUMBER,
    // Keywords.
    NEON_TOK_KWNEW,
    NEON_TOK_KWBREAK,
    NEON_TOK_KWCONTINUE,
    NEON_TOK_KWAND,
    NEON_TOK_KWCLASS,
    NEON_TOK_KWELSE,
    NEON_TOK_KWFALSE,
    NEON_TOK_KWFOR,
    NEON_TOK_KWFUNCTION,
    NEON_TOK_KWIF,
    NEON_TOK_KWNIL,
    NEON_TOK_KWOR,
    NEON_TOK_KWDEBUGPRINT,
    NEON_TOK_KWGLOBAL,
    NEON_TOK_KWTYPEOF,
    NEON_TOK_KWRETURN,
    NEON_TOK_KWSUPER,
    NEON_TOK_KWTHIS,
    NEON_TOK_KWTRUE,
    NEON_TOK_KWVAR,
    NEON_TOK_KWWHILE,
    NEON_TOK_ERROR,
    NEON_TOK_EOF
};


enum NeonAstPrecedence
{
    #if 0
    NEON_PREC_NONE,
    NEON_PREC_ASSIGNMENT,// =, &=, |=, *=, +=, -=, /=, **=, %=, >>=, <<=, ^=, //=
    // ~=
    NEON_PREC_CONDITIONAL,// ?:
    NEON_PREC_OR,// or
    NEON_PREC_AND,// and
    NEON_PREC_EQUALITY,// ==, !=
    NEON_PREC_COMPARISON,// <, >, <=, >=
    NEON_PREC_BIT_OR,// |
    NEON_PREC_BIT_XOR,// ^
    NEON_PREC_BIT_AND,// &
    NEON_PREC_SHIFT,// <<, >>
    NEON_PREC_RANGE,// ..
    NEON_PREC_TERM,// +, -
    NEON_PREC_FACTOR,// *, /, %, **, //
    NEON_PREC_UNARY,// !, -, ~, (++, -- this two will now be treated as statements)
    NEON_PREC_CALL,// ., ()
    NEON_PREC_PRIMARY
    #endif

    NEON_PREC_NONE,
    NEON_PREC_ASSIGNMENT,// =, &=, |=, *=, +=, -=, /=, **=, %=, >>=, <<=, ^=, //=
    // ~=
    NEON_PREC_CONDITIONAL,// ?:
    NEON_PREC_OR,// or
    NEON_PREC_AND,// and
    NEON_PREC_EQUALITY,// ==, !=
    NEON_PREC_COMPARISON,// <, >, <=, >=
    NEON_PREC_BIT_OR,// |
    NEON_PREC_BIT_XOR,// ^
    NEON_PREC_BIT_AND,// &
    NEON_PREC_SHIFT,// <<, >>
    NEON_PREC_RANGE,// ..
    NEON_PREC_TERM,// +, -
    NEON_PREC_FACTOR,// *, /, %, **, //
    NEON_PREC_UNARY,// !, -, ~, (++, -- this two will now be treated as statements)
    NEON_PREC_CALL,// ., ()
    NEON_PREC_PRIMARY
};




enum NeonAstFuncType
{
    NEON_TYPE_FUNCTION,
    NEON_TYPE_INITIALIZER,
    NEON_TYPE_METHOD,
    NEON_TYPE_SCRIPT
};



typedef enum NeonObjType NeonObjType;
typedef enum NeonAstPrecedence NeonAstPrecedence;
typedef enum NeonAstFuncType NeonAstFuncType;
typedef enum NeonValType NeonValType;
typedef enum NeonStatusCode NeonStatusCode;
typedef enum NeonAstTokType NeonAstTokType;
typedef enum NeonOpCode NeonOpCode;

typedef struct /**/NeonObject NeonObject;
typedef struct /**/NeonObjUserdata NeonObjUserdata;
typedef struct /**/NeonObjString NeonObjString;
typedef struct /**/NeonObjScriptFunction NeonObjScriptFunction;
typedef struct /**/NeonObjNativeFunction NeonObjNativeFunction;
typedef struct /**/NeonObjUpvalue NeonObjUpvalue;
typedef struct /**/NeonObjClosure NeonObjClosure;
typedef struct /**/NeonObjClass NeonObjClass;
typedef struct /**/NeonObjInstance NeonObjInstance;
typedef struct /**/NeonObjBoundFunction NeonObjBoundFunction;
typedef struct /**/NeonObjArray NeonObjArray;
typedef struct /**/NeonObjMap NeonObjMap;
typedef struct /**/NeonCallFrame NeonCallFrame;
typedef struct /**/NeonValue NeonValue;
typedef struct /**/NeonValArray NeonValArray;
typedef struct /**/NeonBinaryBlob NeonBinaryBlob;
typedef struct /**/NeonHashEntry NeonHashEntry;
typedef struct /**/NeonHashTable NeonHashTable;
typedef struct /**/NeonState NeonState;
typedef struct /**/NeonAstToken NeonAstToken;
typedef struct /**/NeonAstRule NeonAstRule;
typedef struct /**/NeonAstLoop NeonAstLoop;
typedef struct /**/NeonAstLocal NeonAstLocal;
typedef struct /**/NeonAstUpvalue NeonAstUpvalue;
typedef struct /**/NeonAstCompiler NeonAstCompiler;
typedef struct /**/NeonAstClassCompiler NeonAstClassCompiler;
typedef struct /**/NeonAstScanner NeonAstScanner;
typedef struct /**/NeonAstParser NeonAstParser;
typedef struct /**/NeonWriter NeonWriter;
typedef struct /**/NeonVMStateVars NeonVMStateVars;
typedef struct /**/NeonVMGCVars NeonVMGCVars;
typedef struct /**/NeonVMObjVars NeonVMObjVars;
typedef struct /**/NeonConfig NeonConfig;
typedef struct /**/NeonNestCall NeonNestCall;


typedef void (*NeonAstParsePrefixFN)(NeonAstParser*, bool);
typedef void (*NeonAstParseInfixFN)(NeonAstParser*, NeonAstToken, bool);

typedef NeonValue (*NeonNativeFN)(NeonState*, NeonValue, int, NeonValue*);
typedef bool (*NeonValDestroyFN)(NeonState*, void*, NeonValue);
typedef void (*NeonUserFinalizeFN)(NeonState*, NeonObjUserdata*);
typedef double(*NeonVMBinaryCallbackFN)(double, double);

struct NeonValue
{
    NeonValType type;

    union
    {
        bool valbool;
        double valnumber;
        NeonObject* valobjptr;
    } as;// [as]
};

struct NeonObject
{
    bool ismarked;
    bool keep;
    NeonObjType type;
    NeonState* pvm;
    NeonObject* next;
    NeonHashTable* objmethods;
};

struct NeonWriter
{
    bool isstring;
    bool shouldclose;
    bool stringtaken;
    NeonState* pvm;
    StringBuffer* strbuf;
    FILE* handle;
};

struct NeonBinaryBlob
{
    int count;
    int capacity;
    int32_t* bincode;
    int* srclinenos;
    NeonValArray* constants;
};

struct NeonHashEntry
{
    NeonObjString* key;
    NeonValue value;
};

struct NeonHashTable
{
    int count;
    int capacity;
    NeonState* pvm;
    NeonHashEntry* entries;
};

struct NeonValArray
{
    NeonState* pvm;
    size_t size;
    size_t capacity;
    NeonValue* values;
};

struct NeonObjUserdata
{
    NeonObject objectheader;
    NeonState* pvm;
    void* userptr;
    NeonUserFinalizeFN finalfn;
    NeonHashTable* methods;
};

struct NeonObjArray
{
    NeonObject obj;
    NeonState* pvm;
    NeonValArray* vala;
};

struct NeonObjMap
{
    NeonObject obj;
    bool shouldmark;
    NeonState* pvm;
    NeonHashTable* mapping;
};

struct NeonObjScriptFunction
{
    NeonObject obj;
    bool isvariadic;
    NeonState* pvm;
    int arity;
    int upvaluecount;
    NeonBinaryBlob* blob;
    NeonObjString* name;
};

struct NeonObjNativeFunction
{
    NeonObject obj;
    NeonState* pvm;
    const char* name;
    NeonNativeFN natfunc;
};

struct NeonObjString
{
    NeonObject objectheader;
    NeonState* pvm;
    uint32_t hash;
    // actual string handling is handled by StringBuffer.
    // this is to avoid to trashing the stack with temporary values
    StringBuffer* sbuf;
};

struct NeonObjUpvalue
{
    NeonObject objectheader;
    NeonState* pvm;
    NeonValue location;
    NeonValue closed;
    NeonObjUpvalue* next;
    int32_t upindex;
};

struct NeonObjClosure
{
    NeonObject objectheader;
    NeonState* pvm;
    NeonObjScriptFunction* fnptr;
    NeonObjUpvalue** upvalues;
    int upvaluecount;
};

struct NeonObjClass
{
    NeonObject objectheader;
    NeonState* pvm;
    NeonObjString* name;
    NeonHashTable* methods;
    NeonHashTable* staticmethods;
    NeonHashTable* properties;
    NeonObjClass* parent;
};

struct NeonObjInstance
{
    NeonObject objectheader;
    NeonState* pvm;
    NeonObjClass* klass;
    NeonHashTable* fields;// [fields]
};

struct NeonObjBoundFunction
{
    NeonObject objectheader;
    NeonState* pvm;
    NeonValue receiver;
    NeonObjClosure* method;
};

struct NeonAstToken
{
    int length;
    int line;
    const char* start;
    NeonAstTokType type;
};

struct NeonAstRule
{
    NeonAstParsePrefixFN prefix;
    NeonAstParseInfixFN infix;
    NeonAstPrecedence precedence;
};

struct NeonAstLoop
{
    int start;
    int body;
    int scopedepth;
    NeonAstLoop* enclosing;
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
    int32_t index;
};

struct NeonAstCompiler
{
    int scopedepth;
    int localcount;
    NeonAstFuncType type;
    NeonAstCompiler* enclosing;
    NeonObjScriptFunction* currfunc;
    NeonAstLoop* loop;
    NeonAstUpvalue compupvals[NEON_MAX_COMPUPVALS];
    NeonAstLocal locals[NEON_MAX_COMPLOCALS];
};

struct NeonAstClassCompiler
{
    bool hassuperclass;
    NeonAstClassCompiler* enclosing;
};

struct NeonAstScanner
{
    int line;
    size_t maxlength;
    const char* start;
    const char* current;
    NeonState* pvm;
};

struct NeonAstParser
{
    bool haderror;
    bool panicmode;
    bool iseval;
    int blockcount;
    // used for tracking loops for the continue statement...
    int innermostloopstart;
    int innermostloopscopedepth;
    NeonAstToken current;
    NeonAstToken previous;
    NeonState* pvm;
    NeonAstScanner* pscn;
    NeonAstCompiler* currcompiler;
    NeonAstClassCompiler* currclass;
};


struct NeonVMObjVars
{
    NeonObjClass* classprimobject;
    NeonObjClass* classprimnumber;
    NeonObjClass* classprimstring;
    NeonObjClass* classprimarray;
};

struct NeonConfig
{
    /* if true, will print instruction and stack during execution */
    bool shouldprintruntime;
    bool strictmode;
};

struct NeonCallFrame
{
    int instrucidx;
    int64_t frstackindex;
    NeonObjClosure* closure;
};

struct NeonVMGCVars
{
    bool allowed;
    int graycount;
    int graycap;
    size_t bytesallocd;
    size_t nextgc;
    NeonObject* linkedobjects;
    NeonObject** graystack;
};

struct NeonVMStateVars
{
    bool hasraised;
    bool iseval;
    bool havekeeper;
    int framecount;
    int64_t stacktop;
    size_t stackcapacity;
    size_t framecapacity;
    NeonCallFrame keepframe;
    NeonCallFrame* activeframe;
    NeonCallFrame* framevalues;
    NeonValue* stackvalues;
    NeonObjClosure* topclosure;
    
};

struct NeonState
{
    NeonConfig conf;
    NeonObjMap* envvars;
    /* instance of the parser. this is */
    NeonAstParser* parser;
    NeonVMStateVars vmstate;
    NeonVMGCVars gcstate;
    NeonVMObjVars objvars;
    NeonHashTable* globals;
    NeonHashTable* strings;
    NeonObjString* initstring;
    NeonObjUpvalue* openupvalues;
    NeonWriter* stdoutwriter;
    NeonWriter* stderrwriter;

};

#include "prot.inc"

extern char **environ;

#define pack754_64(f) (pack754((f), 64, 11))
#define unpack754_64(i) (unpack754((i), 64, 11))

uint64_t pack754(long double f, unsigned bits, unsigned expbits)
{
    long double fnorm;
    int shift;
    long long sign;
    long long exp;
    long long significand;
    unsigned significandbits;
    /* -1 for sign bit */
    significandbits = bits - expbits - 1;
    /* get this special case out of the way */
    if(f == 0.0)
    {
        return 0;
    }
    /* check sign and begin normalization */
    if(f < 0)
    {
        sign = 1;
        fnorm = -f;
    }
    else
    {
        sign = 0;
        fnorm = f;
    }
    /* get the normalized form of f and track the exponent */
    shift = 0;
    while(fnorm >= 2.0)
    {
        fnorm /= 2.0;
        shift++;
    }
    while(fnorm < 1.0)
    {
        fnorm *= 2.0;
        shift--;
    }
    fnorm = fnorm - 1.0;
    /* calculate the binary form (non-float) of the significand data */
    significand = fnorm * ((1LL << significandbits) + 0.5f);
    /* get the biased exponent */
    /* shift + bias */
    exp = shift + ((1<<(expbits-1)) - 1);
    /* return the final answer */
    return (
        (sign << (bits - 1)) | (exp << (bits - expbits - 1)) | significand
    );
}

long double unpack754(uint64_t i, unsigned bits, unsigned expbits)
{
    long double result;
    long long shift;
    unsigned bias;
    unsigned significandbits;
    /* -1 for sign bit */
    significandbits = bits - expbits - 1;
    if(i == 0)
    {
        return 0.0;
    }
    /* pull the significand */
    /* mask */
    result = (i&((1LL<<significandbits)-1));
    /* convert back to float */
    result /= (1LL<<significandbits);
    /* add the one back on */
    result += 1.0f;
    /* deal with the exponent */
    bias = ((1 << (expbits - 1)) - 1);
    shift = (((i >> significandbits) & ((1LL << expbits) - 1)) - bias);
    while(shift > 0)
    {
        result *= 2.0;
        shift--;
    }
    while(shift < 0)
    {
        result /= 2.0;
        shift++;
    }
    /* sign it */
    if(((i>>(bits-1)) & 1) == 1)
    {
        result = result * -1.0;
    }
    else
    {
        result = result * 1.0;
    }
    return result;
}

/* this used to be done via type punning, which may not be portable */
double neon_util_uinttofloat(unsigned int val)
{
    return unpack754_64(val);
}

unsigned int neon_util_floattouint(double val)
{
    return pack754_64(val);
}

int neon_util_doubletoint(double n)
{
    if(n == 0)
    {
        return 0;
    }
    if(isnan(n))
    {
        return 0;
    }
    if(n < 0)
    {
        n = -floor(-n);
    }
    else
    {
        n = floor(n);
    }
    if(n < INT_MIN)
    {
        return INT_MIN;
    }
    if(n > INT_MAX)
    {
        return INT_MAX;
    }
    return (int)n;
}

int neon_util_numbertoint32(double n)
{
    /* magic. no idea. */
    bool isf;
    double two32 = 4294967296.0;
    double two31 = 2147483648.0;
    isf = isfinite(n);
    if(!isf || (n == 0))
    {
        return 0;
    }
    n = fmod(n, two32);
    if(n >= 0)
    {
        n = floor(n);
    }
    else
    {
        n = ceil(n) + two32;
    }
    if(n >= two31)
    {
        return n - two32;
    }
    return n;
}

unsigned int neon_util_numbertouint32(double n)
{
    return (unsigned int)neon_util_numbertoint32(n);
}

double neon_util_modulo(double a, double b)
{
    double r;
    r = fmod(a, b);
    if(r != 0 && ((r < 0) != (b < 0)))
    {
        r += b;
    }
    return r;
}

static inline NeonValue neon_value_makevalue(NeonValType vt)
{
    NeonValue v;
    v.type = vt;
    return v;
}

static inline NeonValue neon_value_makenil()
{
    NeonValue v;
    v = neon_value_makevalue(NEON_VAL_NIL);
    return v;
}

static inline NeonValue neon_value_makenull()
{
    return neon_value_makenil();
}


static inline NeonValue neon_value_makebool(bool b)
{
    NeonValue nv;
    nv = neon_value_makevalue(NEON_VAL_BOOL);
    nv.as.valbool = b;
    return nv;
}

static inline NeonValue neon_value_makenumber(double dw)
{
    NeonValue nv;
    nv = neon_value_makevalue(NEON_VAL_NUMBER);
    nv.as.valnumber = dw;
    return nv;
}

#define neon_value_fromobject(obj) neon_value_fromobject_actual((NeonObject*)(obj))

static inline NeonValue neon_value_fromobject_actual(NeonObject* o)
{
    NeonValue nv;
    nv = neon_value_makevalue(NEON_VAL_OBJ);
    nv.as.valobjptr = o;
    return nv;
}

static inline bool neon_value_isbool(NeonValue v)
{
    return (v.type == NEON_VAL_BOOL);
}

static inline bool neon_value_isnil(NeonValue v)
{
    return (v.type == NEON_VAL_NIL);
}

static inline bool neon_value_isnumber(NeonValue v)
{
    return (v.type == NEON_VAL_NUMBER);
}

static inline bool neon_value_isobject(NeonValue v)
{
    return (v.type == NEON_VAL_OBJ);
}

static inline NeonObject* neon_value_asobject(NeonValue v)
{
    return v.as.valobjptr;
}

static inline bool neon_value_isobjtype(NeonValue value, NeonObjType type)
{
    return (
        neon_value_isobject(value) &&
        (neon_value_asobject(value)->type == type)
    );
}

static inline bool neon_value_asbool(NeonValue v)
{
    return (v.as.valbool);
}

static inline double neon_value_asnumber(NeonValue v)
{
    return (v.as.valnumber);
}

static inline NeonObjUserdata* neon_value_asuserdata(NeonValue v)
{
    return ((NeonObjUserdata*)neon_value_asobject(v));
}

static inline NeonObjBoundFunction* neon_value_asboundfunction(NeonValue v)
{
    return ((NeonObjBoundFunction*)neon_value_asobject(v));
}

static inline NeonObjClass* neon_value_asclass(NeonValue v)
{
    return ((NeonObjClass*)neon_value_asobject(v));
}

static inline NeonObjClosure* neon_value_asclosure(NeonValue v)
{
    return ((NeonObjClosure*)neon_value_asobject(v));
}

static inline NeonObjScriptFunction* neon_value_asscriptfunction(NeonValue v)
{
    return ((NeonObjScriptFunction*)neon_value_asobject(v));
}

static inline NeonObjNativeFunction* neon_value_asnativefunction(NeonValue v)
{
    return ((NeonObjNativeFunction*)neon_value_asobject(v));
}


static inline NeonObjInstance* neon_value_asinstance(NeonValue v)
{
    return ((NeonObjInstance*)neon_value_asobject(v));
}

static inline NeonObjString* neon_value_asstring(NeonValue v)
{
    return ((NeonObjString*)neon_value_asobject(v));
}

static inline NeonObjArray* neon_value_asarray(NeonValue v)
{
    return ((NeonObjArray*)neon_value_asobject(v));
}

static inline NeonObjMap* neon_value_asmap(NeonValue v)
{
    return ((NeonObjMap*)neon_value_asobject(v));
}

static inline NeonObjType neon_value_objtype(NeonValue v)
{
    return (neon_value_asobject(v)->type);
}

//#define IS_CLOSURE(value) neon_value_isobjtype(value, NEON_OBJ_CLOSURE)
//#define IS_FUNCTION(value) neon_value_isobjtype(value, NEON_OBJ_FUNCTION)
//#define IS_NATIVE(value) neon_value_isobjtype(value, NEON_OBJ_NATIVE)
//#define IS_BOUND_METHOD(value) neon_value_isobjtype(value, NEON_OBJ_BOUNDMETHOD)

static inline bool neon_value_isclass(NeonValue v)
{
    return neon_value_isobjtype(v, NEON_OBJ_CLASS);
}

static inline bool neon_value_isuserdata(NeonValue v)
{
    return neon_value_isobjtype(v, NEON_OBJ_USERDATA);
}

static inline bool neon_value_isinstance(NeonValue v)
{
    return neon_value_isobjtype(v, NEON_OBJ_INSTANCE);
}

static inline bool neon_value_isstring(NeonValue v)
{
    return neon_value_isobjtype(v, NEON_OBJ_STRING);
}
    
static inline bool neon_value_isarray(NeonValue v)
{
    return neon_value_isobjtype(v, NEON_OBJ_ARRAY);
}

static inline bool neon_value_ismap(NeonValue v)
{
    return neon_value_isobjtype(v, NEON_OBJ_MAP);
}

static inline bool neon_value_isfalsey(NeonValue value)
{
    return (
        neon_value_isnil(value) || (
            neon_value_isbool(value) &&
            !neon_value_asbool(value)
        )
    );
}

void* neon_mem_allocate(NeonState* state, size_t tsz, size_t count, bool dogc)
{
    size_t totalsz;
    void* p;
    totalsz = (tsz * count);
    p = neon_mem_wrapalloc(state, NULL, 0, totalsz, dogc);
    if(p != NULL)
    {
        memset(p, 0, totalsz);
    }
    return p;
}

void* neon_mem_growarray(NeonState* state, size_t tsz, void* pointer, size_t oldcnt, size_t newcnt)
{
    return neon_mem_wrapalloc(state, pointer, (tsz * oldcnt), (tsz * newcnt), true);
}
 
void neon_mem_freearray(NeonState* state, size_t tsz, void* pointer, size_t oldcnt)
{
    neon_mem_wrapalloc(state, pointer, (tsz * oldcnt), 0, true);
}

void neon_mem_release(NeonState* state, size_t tsz, void* pointer)
{
    neon_mem_wrapalloc(state, pointer, tsz, 0, true);
}

void* neon_mem_wrapalloc(NeonState* state, void* pointer, size_t oldsz, size_t newsz, bool dogc)
{
    void* result;
    state->gcstate.bytesallocd += newsz - oldsz;
    if((newsz > oldsz) && dogc)
    {
#ifdef DEBUG_STRESS_GC
        neon_gcmem_collectgarbage(state);
#endif
        if(state->gcstate.bytesallocd > state->gcstate.nextgc)
        {
            neon_gcmem_collectgarbage(state);
        }
    }
    if(newsz == 0)
    {
        free(pointer);
        return NULL;
    }
    result = realloc(pointer, newsz);
    if(result == NULL)
    {
        fprintf(stderr, "internal error: realloc() failed\n");
        abort();
    }
    return result;
}

void neon_gcmem_markroots(NeonState* state)
{
    int i;
    NeonValue* pslot;
    NeonObjUpvalue* upvalue;
    for(pslot=&state->vmstate.stackvalues[0]; pslot < &state->vmstate.stackvalues[state->vmstate.stacktop]; pslot++)
    {
        neon_gcmem_markvalue(state, *pslot);
    }
    for(i = 0; i < state->vmstate.framecount; i++)
    {
        neon_gcmem_markobject(state, (NeonObject*)state->vmstate.framevalues[i].closure);
    }
    for(upvalue = state->openupvalues; upvalue != NULL; upvalue = upvalue->next)
    {
        neon_gcmem_markobject(state, (NeonObject*)upvalue);
    }
    neon_hashtable_mark(state, state->globals);
    neon_astparser_markcompilerroots(state);
    neon_gcmem_markobject(state, (NeonObject*)state->initstring);
}

void neon_gcmem_tracerefs(NeonState* state)
{
    NeonObject* obj;
    while(state->gcstate.graycount > 0)
    {
        --state->gcstate.graycount;
        obj = state->gcstate.graystack[state->gcstate.graycount];
        neon_gcmem_blackenobj(state, obj);
    }
}

void neon_gcmem_sweep(NeonState* state)
{
    NeonObject* obj;
    NeonObject* previous;
    NeonObject* unreached;
    previous = NULL;
    obj = state->gcstate.linkedobjects;
    while(obj != NULL)
    {
        if(obj->ismarked)
        {
            obj->ismarked = false;
            previous = obj;
            obj = obj->next;
        }
        else
        {
            unreached = obj;
            obj = obj->next;
            if(previous != NULL)
            {
                previous->next = obj;
            }
            else
            {
                state->gcstate.linkedobjects = obj;
            }
            neon_object_destroy(state, unreached);
        }
    }
}

void neon_gcmem_collectgarbage(NeonState* state)
{
    size_t before;
    (void)before;
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    before = state->gcstate.bytesallocd;
#endif
    neon_gcmem_markroots(state);
    neon_gcmem_tracerefs(state);
    neon_hashtable_remwhite(state, state->strings);
    neon_gcmem_sweep(state);
    state->gcstate.nextgc = state->gcstate.bytesallocd * NEON_MAX_GCHEAPGROWFACTOR;
#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n", before - state->gcstate.bytesallocd, before, state->gcstate.bytesallocd, state->gcstate.nextgc);
#endif
}

void neon_vm_gcfreelinkedobjects(NeonState* state)
{
    NeonObject* obj;
    NeonObject* next;
    obj = state->gcstate.linkedobjects;
    while(obj != NULL)
    {
        next = obj->next;
        neon_object_destroy(state, obj);
        obj = next;
    }
    free(state->gcstate.graystack);
}


void neon_gcmem_markobject(NeonState* state, NeonObject* object)
{
    size_t needsz;
    if(object == NULL)
    {
        return;
    }
    if(object->ismarked)
    {
        return;
    }
#if 0
    neon_writer_writefmt(state->stderrwriter, "markobject: (graycap=%d graycount=%d) at %p: ", state->gcstate.graycap, state->gcstate.graycount, (void*)object);
    neon_writer_printvalue(state->stderrwriter, neon_value_fromobject(object), true);
    neon_writer_writestring(state->stderrwriter, "\n");
#endif
    object->ismarked = true;
    if(state->gcstate.graycap < (state->gcstate.graycount + 1))
    {
        state->gcstate.graycap = NEON_NEXTCAPACITY(state->gcstate.graycap);
        needsz = sizeof(NeonObject*) * state->gcstate.graycap;
        state->gcstate.graystack = (NeonObject**)realloc(state->gcstate.graystack, needsz);
        if(state->gcstate.graystack == NULL)
        {
            fprintf(stderr, "internal error: cannot grow graystack\n");
            abort();
        }
    }
    state->gcstate.graystack[state->gcstate.graycount++] = object;
}

void neon_gcmem_markvalue(NeonState* state, NeonValue value)
{
    if(neon_value_isobject(value))
    {
        neon_gcmem_markobject(state, neon_value_asobject(value));
    }
}

void neon_gcmem_markvalarray(NeonState* state, NeonValArray* array)
{
    size_t i;
    for(i = 0; i < neon_valarray_count(array); i++)
    {
        neon_gcmem_markvalue(state, array->values[i]);
    }
}

void neon_gcmem_blackenobj(NeonState* state, NeonObject* object)
{
#ifdef DEBUG_LOG_GC
    neon_writer_writefmt(state->stderrwriter, "blackenobj: at %p: ", (void*)object);
    neon_writer_printvalue(state->stderrwriter, neon_value_fromobject(object), true);
    neon_writer_writestring(state->stderrwriter, "\n");
#endif

    switch(object->type)
    {
        case NEON_OBJ_USERDATA:
            {
            }
            break;
        case NEON_OBJ_BOUNDMETHOD:
            {
                NeonObjBoundFunction* bound;
                bound = (NeonObjBoundFunction*)object;
                neon_gcmem_markvalue(state, bound->receiver);
                neon_gcmem_markobject(state, (NeonObject*)bound->method);
            }
            break;
        case NEON_OBJ_CLASS:
            {
                NeonObjClass* klass;
                klass = (NeonObjClass*)object;
                neon_gcmem_markobject(state, (NeonObject*)klass->name);
                neon_hashtable_mark(state, klass->methods);
                neon_hashtable_mark(state, klass->staticmethods);

            }
            break;

        case NEON_OBJ_CLOSURE:
            {
                NeonObjClosure* closure;
                closure = (NeonObjClosure*)object;
                neon_closure_mark(closure);
            }
            break;
        case NEON_OBJ_FUNCTION:
            {
                NeonObjScriptFunction* ofn;
                ofn = (NeonObjScriptFunction*)object;
                neon_gcmem_markobject(state, (NeonObject*)ofn->name);
                neon_gcmem_markvalarray(state, ofn->blob->constants);
            }
            break;
        case NEON_OBJ_INSTANCE:
            {
                NeonObjInstance* instance;
                instance = (NeonObjInstance*)object;
                neon_instance_mark(instance);
            }
            break;
        case NEON_OBJ_UPVALUE:
            {
                neon_gcmem_markvalue(state, ((NeonObjUpvalue*)object)->closed);
            }
            break;
        case NEON_OBJ_ARRAY:
            {
                NeonObjArray* oa;
                oa = (NeonObjArray*)object;
                neon_array_mark(oa);
            }
            break;
        case NEON_OBJ_MAP:
            {
                NeonObjMap* om;
                om = (NeonObjMap*)object;
                neon_map_mark(om);
            }
            break;
        case NEON_OBJ_NATIVE:
        case NEON_OBJ_STRING:
            break;
    }
}

void neon_object_destroy(NeonState* state, NeonObject* object)
{
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif
    if(object->keep)
    {
        return;
    }
    switch(object->type)
    {
        case NEON_OBJ_USERDATA:
            {
                NeonObjUserdata* ud;
                ud = (NeonObjUserdata*)object;
                neon_userdata_destroy(ud);
            }
            break;
        case NEON_OBJ_BOUNDMETHOD:
            {
                neon_mem_release(state, sizeof(NeonObjBoundFunction), object);
            }
            break;
        case NEON_OBJ_CLASS:
            {
                NeonObjClass* klass;
                klass = (NeonObjClass*)object;
                neon_class_destroy(klass);
                klass = NULL;
            }
            break;
        case NEON_OBJ_CLOSURE:
            {
                NeonObjClosure* closure;
                closure = (NeonObjClosure*)object;
                neon_closure_destroy(closure);
                closure = NULL;
            }
            break;
        case NEON_OBJ_FUNCTION:
            {
                NeonObjScriptFunction* ofn;
                ofn = (NeonObjScriptFunction*)object;
                neon_blob_destroy(state, ofn->blob);
                neon_mem_release(state, sizeof(NeonObjScriptFunction), object);
                ofn = NULL;
            }
            break;
        case NEON_OBJ_INSTANCE:
            {
                NeonObjInstance* instance;
                instance = (NeonObjInstance*)object;
                neon_hashtable_destroy(instance->fields);
                neon_mem_release(state, sizeof(NeonObjInstance), object);
                instance = NULL;
            }
            break;
        case NEON_OBJ_NATIVE:
            {
                neon_mem_release(state, sizeof(NeonObjNativeFunction), object);
            }
            break;
        case NEON_OBJ_STRING:
            {
                NeonObjString* string;
                string = (NeonObjString*)object;
                neon_string_destroy(string);
            }
            break;
        case NEON_OBJ_ARRAY:
            {
                NeonObjArray* arr;
                arr = (NeonObjArray*)object;
                neon_array_destroy(arr);
            }
            break;
        case NEON_OBJ_MAP:
            {
                NeonObjMap* om;
                om = (NeonObjMap*)object;
                neon_map_destroy(om);
            }
            break;
        case NEON_OBJ_UPVALUE:
            {
                neon_mem_release(state, sizeof(NeonObjUpvalue), object);
            }
            break;
    }
}

uint32_t neon_util_hashstring(const char* key, size_t length)
{
    size_t i;
    uint32_t hash;
    hash = 2166136261u;
    for(i = 0; i < length; i++)
    {
        hash ^= (int32_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

NeonWriter* neon_writer_make(NeonState* state)
{
    NeonWriter* wr;
    (void)state;
    wr = (NeonWriter*)malloc(sizeof(NeonWriter));
    if(!wr)
    {
        fprintf(stderr, "cannot allocate NeonWriter\n");
    }
    wr->pvm = state;
    wr->isstring = false;
    wr->shouldclose = false;
    wr->stringtaken = false;
    wr->strbuf = NULL;
    wr->handle = NULL;
    return wr;
}

void neon_writer_destroy(NeonWriter* wr)
{
    NeonState* state;
    (void)state;
    state = wr->pvm;
    if(wr->isstring)
    {
        if(!wr->stringtaken)
        {
            dyn_strbuf_destroy(wr->strbuf);
        }
    }
    else
    {
        if(wr->shouldclose)
        {
            fclose(wr->handle);
        }
    }
    free(wr);
}

NeonObjString* neon_writer_takestring(NeonWriter* wr)
{
    uint32_t hash;
    NeonState* state;
    NeonObjString* os;
    state = wr->pvm;
    hash = neon_util_hashstring(wr->strbuf->data, wr->strbuf->length);
    os = neon_string_allocfromstrbuf(state, wr->strbuf, hash);
    wr->stringtaken = true;
    return os;

}

NeonWriter* neon_writer_makeio(NeonState* state, FILE* fh, bool shouldclose)
{
    NeonWriter* wr;
    wr = neon_writer_make(state);
    wr->handle = fh;
    wr->shouldclose = shouldclose;
    return wr;
}

NeonWriter* neon_writer_makestring(NeonState* state)
{
    NeonWriter* wr;
    wr = neon_writer_make(state);
    wr->isstring = true;
    wr->strbuf = dyn_strbuf_makeempty(0);
    return wr;
}

void neon_writer_writestringl(NeonWriter* wr, const char* estr, size_t elen)
{
    if(wr->isstring)
    {
        dyn_strbuf_appendstrn(wr->strbuf, estr, elen);
    }
    else
    {
        fwrite(estr, sizeof(char), elen, wr->handle);
        fflush(wr->handle);
    }
}

void neon_writer_writestring(NeonWriter* wr, const char* estr)
{
    return neon_writer_writestringl(wr, estr, strlen(estr));
}

void neon_writer_writechar(NeonWriter* wr, int b)
{
    char ch;
    if(wr->isstring)
    {
        ch = b;
        neon_writer_writestringl(wr, &ch, 1);
    }
    else
    {
        fputc(b, wr->handle);
        fflush(wr->handle);
    }
}

void neon_writer_writeescapedchar(NeonWriter* wr, int ch)
{
    switch(ch)
    {
        case '\'':
            {
                neon_writer_writestring(wr, "\\\'");
            }
            break;
        case '\"':
            {
                neon_writer_writestring(wr, "\\\"");
            }
            break;
        case '\\':
            {
                neon_writer_writestring(wr, "\\\\");
            }
            break;
        case '\b':
            {
                neon_writer_writestring(wr, "\\b");
            }
            break;
        case '\f':
            {
                neon_writer_writestring(wr, "\\f");
            }
            break;
        case '\n':
            {
                neon_writer_writestring(wr, "\\n");
            }
            break;
        case '\r':
            {
                neon_writer_writestring(wr, "\\r");
            }
            break;
        case '\t':
            {
                neon_writer_writestring(wr, "\\t");
            }
            break;
        case 0:
            {
                neon_writer_writestring(wr, "\\0");
            }
            break;
        default:
            {
                neon_writer_writefmt(wr, "\\x%02x", (unsigned char)ch);
            }
            break;
    }
}

void neon_writer_writequotedstring(NeonWriter* wr, const char* str, size_t len, bool withquot)
{
    int bch;
    size_t i;
    bch = 0;
    if(withquot)
    {
        neon_writer_writechar(wr, '"');
    }
    for(i=0; i<len; i++)
    {
        bch = str[i];
        if((bch < 32) || (bch > 127) || (bch == '\"') || (bch == '\\'))
        {
            neon_writer_writeescapedchar(wr, bch);
        }
        else
        {
            neon_writer_writechar(wr, bch);
        }
    }
    if(withquot)
    {
        neon_writer_writechar(wr, '"');
    }
}

void neon_writer_vwritefmttostring(NeonWriter* wr, const char* fmt, va_list va)
{
    size_t wsz;
    size_t needed;
    char* buf;
    va_list copy;
    va_copy(copy, va);
    needed = 1 + vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    buf = (char*)malloc(needed+1);
    memset(buf, 0, needed+1);
    wsz = vsnprintf(buf, needed, fmt, va);
    neon_writer_writestringl(wr, buf, wsz);
    free(buf);
}

void neon_writer_vwritefmt(NeonWriter* wr, const char* fmt, va_list va)
{
    if(wr->isstring)
    {
        return neon_writer_vwritefmttostring(wr, fmt, va);
    }
    else
    {
        vfprintf(wr->handle, fmt, va);
        fflush(wr->handle);
    }
}

void neon_writer_writefmt(NeonWriter* wr, const char* fmt, ...) NEON_ATTR_PRINTFLIKE(2, 3);


void neon_writer_writefmt(NeonWriter* wr, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    neon_writer_vwritefmt(wr, fmt, va);
    va_end(va);
}

bool neon_writer_vvalfmt(NeonWriter* wr, const char* format, va_list arglist)
{
    size_t cslen;
    bool wasallowed;
    const char* cstr;
    NeonValue val;
    NeonState* state;
    state = wr->pvm;
    wasallowed = state->gcstate.allowed;
    state->gcstate.allowed = false;
    while(*format)
    {
        if(*format == '%')
        {
            format++;
            if(*format == '%')
            {
                neon_writer_writechar(wr, '%');
            }
            else if(*format == 'v' || *format == 'q')
            {
                val = va_arg(arglist, NeonValue);
                neon_writer_printvalue(wr, val, *format == 'q');
            }
            else if(*format == 's')
            {
                cstr = va_arg(arglist, char*);
                if(cstr != NULL)
                {
                    cslen = strlen(cstr);
                    neon_writer_writestringl(wr, cstr, cslen);
                }
                else
                {
                    neon_writer_writefmt(wr, "(nullstr)");
                }
            }
            else if(*format == 'd' || *format == 'i')// Case: '%d' or '%i' prints an integer
            {
                int iw;
                iw = va_arg(arglist, int);
                neon_writer_writefmt(wr, "%d", iw);
            }
        }
        else
        {
            neon_writer_writechar(wr, *format);
        }
        format++;
    }
    state->gcstate.allowed = wasallowed;
    return true;
}

bool neon_writer_valfmt(NeonWriter* wr, const char* format, ...)
{
    bool b;
    va_list va;
    va_start(va, format);
    b = neon_writer_vvalfmt(wr, format, va);
    va_end(va);
    return b;
}

void neon_writer_printfunction(NeonWriter* wr, NeonObjScriptFunction* ofn)
{
    if(ofn->name == NULL)
    {
        neon_writer_writestring(wr, "<script>");
    }
    else
    {
        neon_writer_writefmt(wr, "<function '%s'>", ofn->name->sbuf->data);
    }
}

void neon_writer_printarray(NeonWriter* wr, NeonObjArray* arr)
{
    size_t i;
    size_t asz;
    asz = neon_valarray_size(arr->vala);
    neon_writer_writestring(wr, "[");
    for(i=0; i<asz; i++)
    {
        neon_writer_printvalue(wr, arr->vala->values[i], true);
        if((i+1) < asz)
        {
            neon_writer_writestring(wr, ",");
        }
    }
    neon_writer_writestring(wr, "]");
}

void neon_writer_printmap(NeonWriter* wr, NeonObjMap* map)
{
    size_t i;
    size_t cap;
    NeonHashEntry* entry;
    cap = map->mapping->capacity;
    neon_writer_writestring(wr, "{");
    for(i=0; i<cap; i++)
    {
        entry = &map->mapping->entries[i];
        if(entry != NULL)
        {
            if(entry->key != NULL)
            {
                neon_writer_printstring(wr, entry->key, true);
                neon_writer_writefmt(wr, ": ");
                neon_writer_printvalue(wr, entry->value, true);
                if((i+1) < cap)
                {
                    neon_writer_writestring(wr, ",");
                }
            }
        }
    }
    neon_writer_writestring(wr, "}");
}

void neon_writer_printstring(NeonWriter* wr, NeonObjString* os, bool fixstring)
{
    size_t len;
    const char* sp;
    len = os->sbuf->length;
    sp = os->sbuf->data;
    if(fixstring)
    {
        neon_writer_writequotedstring(wr, sp, len, true);
    }
    else
    {
        neon_writer_writestringl(wr, sp, len);
    }
}

void neon_writer_printobject(NeonWriter* wr, NeonValue value, bool fixstring)
{
    switch(neon_value_objtype(value))
    {
        case NEON_OBJ_USERDATA:
            {
                neon_writer_writefmt(wr, "<userdata %p>", neon_value_asuserdata(value));
            }
            break;
        case NEON_OBJ_BOUNDMETHOD:
            {
                neon_writer_printfunction(wr, neon_value_asboundfunction(value)->method->fnptr);
            }
            break;
        case NEON_OBJ_CLASS:
            {
                neon_writer_writefmt(wr, "<class '%s'>", neon_value_asclass(value)->name->sbuf->data);
            }
            break;
        case NEON_OBJ_CLOSURE:
            {
                neon_writer_printfunction(wr, neon_value_asclosure(value)->fnptr);
            }
            break;
        case NEON_OBJ_FUNCTION:
            {
                neon_writer_printfunction(wr, neon_value_asscriptfunction(value));
            }
            break;
        case NEON_OBJ_INSTANCE:
            {
                neon_writer_writefmt(wr, "<instance '%s'>", neon_value_asinstance(value)->klass->name->sbuf->data);
            }
            break;
        case NEON_OBJ_NATIVE:
            {
                NeonObjNativeFunction* onat;
                onat = neon_value_asnativefunction(value);
                neon_writer_writefmt(wr, "<nativefn '%s'@%p>", onat->name, onat);
            }
            break;
        case NEON_OBJ_STRING:
            {
                neon_writer_printstring(wr, neon_value_asstring(value), fixstring);
            }
            break;
        case NEON_OBJ_UPVALUE:
            {
                neon_writer_writefmt(wr, "<upvalue>");
            }
            break;
        case NEON_OBJ_ARRAY:
            {
                neon_writer_printarray(wr, neon_value_asarray(value));
            }
            break;
        case NEON_OBJ_MAP:
            {
                neon_writer_printmap(wr, neon_value_asmap(value));
            }
            break;
        default:
            {
                neon_writer_writefmt(wr, "?unknownobject?");
            }
    }
}

void neon_writer_printvalue(NeonWriter* wr, NeonValue value, bool fixstring)
{
    int64_t iv;
    double dw;
    switch(value.type)
    {
        case NEON_VAL_BOOL:
            neon_writer_writestring(wr, neon_value_asbool(value) ? "true" : "false");
            break;
        case NEON_VAL_NIL:
            neon_writer_writestring(wr, "nil");
            break;
        case NEON_VAL_NUMBER:
            {
                dw = neon_value_asnumber(value);
                if((dw <= LONG_MIN) || (dw >= LONG_MAX) || (dw == (long)dw))
                {
                    iv = (int64_t)dw;
                    neon_writer_writefmt(wr, "%ld", iv);
                }
                else
                {
                    neon_writer_writefmt(wr, "%g", dw);
                }
            }
            break;
        case NEON_VAL_OBJ:
            neon_writer_printobject(wr, value, fixstring);
            break;
        default:
            neon_writer_writefmt(wr, "?unknownvalue?");
            break;
    }
}

const char* neon_object_typename(NeonObject* obj, bool detailed)
{
    if(!detailed)
    {
        switch(obj->type)
        {
            case NEON_OBJ_CLOSURE:
            case NEON_OBJ_FUNCTION:
            case NEON_OBJ_BOUNDMETHOD:
            case NEON_OBJ_NATIVE:
                {
                    return "function";
                }
                break;
            default:
                break;
        }
    }
    switch(obj->type)
    {
        case NEON_OBJ_USERDATA:
            {
                return "userdata";
            }
            break;
        case NEON_OBJ_BOUNDMETHOD:
            {
                return "boundmethod";
            }
            break;
        case NEON_OBJ_CLASS:
            {
                return "class";
            }
            break;
        case NEON_OBJ_CLOSURE:
            {
                return "closure";
            }
            break;
        case NEON_OBJ_FUNCTION:
            {
                return "function";
            }
            break;
        case NEON_OBJ_INSTANCE:
            {
                return "instance";
            }
            break;
        case NEON_OBJ_NATIVE:
            {
                return "nativefunction";
            }
            break;
        case NEON_OBJ_STRING:
            {
                return "string";
            }
            break;
        case NEON_OBJ_UPVALUE:
            {
                return "upvalue";
            }
            break;
        case NEON_OBJ_ARRAY:
            {
                return "array";
            }
            break;
        case NEON_OBJ_MAP:
            {
                return "map";
            }
            break;
    }
    return "?unknownobject?";
}

const char* neon_value_gettypename(NeonValue value, bool detailed)
{
    switch(value.type)
    {
        case NEON_VAL_BOOL:
            return "bool";
            break;
        case NEON_VAL_NIL:
            return "nil";
            break;
        case NEON_VAL_NUMBER:
            return "number";
            break;
        case NEON_VAL_OBJ:
            return neon_object_typename(neon_value_asobject(value), detailed);
            break;
    }
    return "?unknownvalue?";
}

const char* neon_value_typename(NeonValue value)
{
    return neon_value_gettypename(value, true);
}


const char* neon_value_basicname(NeonValue value)
{
    return neon_value_gettypename(value, false);
}

bool neon_value_stringequal(NeonState* state, NeonObjString* aos, NeonObjString* bos)
{
    bool r;
    size_t alen;
    size_t blen;
    const char* astr;
    const char* bstr;
    (void)state;
    alen = aos->sbuf->length;
    blen = bos->sbuf->length;
    astr = aos->sbuf->data;
    bstr = bos->sbuf->data;
    r = false;
    if(alen == blen)
    {
        if(memcmp(astr, bstr, alen) == 0)
        {
            r = true;
        }
    }
    //neon_writer_valfmt(state->stderrwriter, "aos(%d)=%q bos(%d)=%q === %d\n", alen, neon_value_fromobject(aos), blen, neon_value_fromobject(bos), r);
    return r;
}

bool neon_value_objectequal(NeonState* state, NeonValue a, NeonValue b)
{
    if(neon_value_isstring(a) && neon_value_isstring(b))
    {
        return neon_value_stringequal(state, neon_value_asstring(a), neon_value_asstring(b));
    }
    return neon_value_asobject(a) == neon_value_asobject(b);
}

bool neon_value_equal(NeonState* state, NeonValue a, NeonValue b)
{
    if(a.type != b.type)
    {
        return false;
    }
    switch(a.type)
    {
        case NEON_VAL_BOOL:
            {
                return neon_value_asbool(a) == neon_value_asbool(b);
            }
            break;
        case NEON_VAL_NIL:
            {
                return true;
            }
            break;
        case NEON_VAL_NUMBER:
            {
                return neon_value_asnumber(a) == neon_value_asnumber(b);
            }
            break;
            /* Strings strings-equal < Hash Tables equal */
        case NEON_VAL_OBJ:
            {
                return neon_value_objectequal(state, a, b);
            }
            break;
        default:
            break;
    }
    return false;
}

NeonValArray* neon_valarray_make(NeonState* state)
{
    NeonValArray* va;
    va = (NeonValArray*)malloc(sizeof(NeonValArray));
    va->pvm = state;
    va->values = NULL;
    va->capacity = 0;
    va->size = 0;
    return va;
}

void neon_valarray_destroy(NeonValArray* arr)
{
    NeonState* state;
    state = arr->pvm;
    neon_mem_freearray(state, sizeof(NeonValue), arr->values, arr->capacity);
    free(arr);
}

size_t neon_valarray_count(NeonValArray* va)
{
    return va->size;
}

size_t neon_valarray_size(NeonValArray* array)
{
    return array->size;
}

NeonValue neon_valarray_at(NeonValArray* array, size_t i)
{
    return array->values[i];
}

NeonValue neon_valarray_get(NeonValArray* array, size_t i)
{
    return neon_valarray_at(array, i);
}

static inline size_t neon_valarray_computenextgrow(size_t size)
{
    if(size > 0)
    {
        return (size << 1);
    }
    return 2;
}

bool neon_valarray_grow(NeonValArray* arr, size_t count)
{
    size_t nsz;
    void* oldbuf;
    void* newbuf;
    nsz = (count * sizeof(NeonValue));
    oldbuf = arr->values;
    newbuf = realloc(oldbuf, nsz);
    if(newbuf == NULL)
    {
        return false;
    }
    arr->values = (NeonValue*)newbuf;
    //memset(arr->values + arr->capacity, 0, (arr->capacity - 0));
    arr->capacity = count;
    return true;
}

bool neon_valarray_push(NeonValArray* arr, NeonValue value)
{
    size_t cap;
    cap = arr->capacity;
    if(cap <= arr->size)
    {
        if(!neon_valarray_grow(arr, neon_valarray_computenextgrow(cap)))
        {
            return false;
        }
    }
    arr->values[arr->size] = (value);
    arr->size++;
    return true;
}

bool neon_valarray_insert(NeonValArray* arr, size_t pos, NeonValue val)
{
    size_t i;
    size_t asz;
    size_t oldcap;
    bool shouldinc;
    (void)asz;
    shouldinc = false;
    oldcap = arr->capacity;
    if (oldcap <= arr->size)
    {
        if(!neon_valarray_grow(arr, neon_valarray_computenextgrow(oldcap)))
        {
            return false;
        }
    }
    if((pos > arr->size) || (arr->size == 0))
    {
        asz = arr->size;
        for(i=0; i<(pos+1); i++)
        {
            neon_valarray_push(arr, neon_value_makenil());
        }
    }
    arr->values[pos] = val;
    if(shouldinc)
    {
        arr->size++;
    }
    return true;
}

bool neon_valarray_erase(NeonValArray* arr, size_t idx)
{
    size_t i;
    size_t ni;
    size_t osz;
    osz = arr->size;
    if(idx < osz)
    {
        arr->size--;
        ni = 0;
        for(i=0; i<osz; i++)
        {
            if(i == idx)
            {
                /*
                if(arr->dtorfunc)
                {
                    arr->dtorfunc(arr->pvm, arr->userptr, arr->values[idx]);
                }
                */
            }
            else
            {
                arr->values[ni] = arr->values[i];
                ni++;
            }
        }
        return true;
    }
    return false;
}

NeonValue neon_valarray_pop(NeonValArray* arr)
{
    NeonValue val;
    val = arr->values[arr->size - 1];
    arr->size--;
    return val;
}


NeonObject* neon_object_allocobj(NeonState* state, size_t size, NeonObjType type)
{
    NeonObject* baseobj;
    baseobj = (NeonObject*)neon_mem_wrapalloc(state, NULL, 0, size, true);
    baseobj->type = type;
    baseobj->ismarked = false;
    baseobj->keep = false;
    baseobj->pvm = state;
    baseobj->next = state->gcstate.linkedobjects;
    baseobj->objmethods = NULL;
    state->gcstate.linkedobjects = baseobj;
#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)baseobj, size, type);
#endif
    return baseobj;
}

NeonObjUserdata* neon_object_makeuserdata(NeonState* state, void* ptr, NeonUserFinalizeFN finfn)
{
    NeonObjUserdata* obj;
    obj = (NeonObjUserdata*)neon_object_allocobj(state, sizeof(NeonObjBoundFunction), NEON_OBJ_USERDATA);
    obj->pvm = state;
    obj->userptr = ptr;
    obj->finalfn = finfn;
    obj->methods = neon_hashtable_make(state);
    return obj;
}

void neon_userdata_destroy(NeonObjUserdata* ud)
{
    NeonState* state;
    state = ud->pvm;
    if(ud->finalfn != NULL)
    {
        ud->finalfn(state, ud);
    }
    neon_hashtable_destroy(ud->methods);
    neon_mem_release(state, sizeof(NeonObjUserdata), ud);
}

void neon_userdata_bindmethod(NeonObjUserdata* ud, const char* name, NeonNativeFN fn)
{
    neon_hashtable_setstrfunction(ud->methods, name, fn);    
}

NeonObjBoundFunction* neon_object_makeboundmethod(NeonState* state, NeonValue receiver, NeonObjClosure* method)
{
    NeonObjBoundFunction* obj;
    obj = (NeonObjBoundFunction*)neon_object_allocobj(state, sizeof(NeonObjBoundFunction), NEON_OBJ_BOUNDMETHOD);
    obj->pvm = state;
    obj->receiver = receiver;
    obj->method = method;
    return obj;
}

NeonObjClass* neon_object_makeclass(NeonState* state, NeonObjString* name, NeonObjClass* parent)
{
    NeonObjClass* obj;
    obj = (NeonObjClass*)neon_object_allocobj(state, sizeof(NeonObjClass), NEON_OBJ_CLASS);
    obj->pvm = state;
    obj->name = name;
    obj->methods = neon_hashtable_make(state);
    obj->staticmethods = neon_hashtable_make(state);
    obj->properties = neon_hashtable_make(state);
    obj->parent = parent;
    return obj;
}

NeonObjClass* neon_state_makeclass(NeonState* state, const char* name, NeonObjClass* parent)
{
    NeonObjString* objstr;
    NeonObjClass* oclass;
    objstr = neon_string_copycstr(state, name);
    oclass = neon_object_makeclass(state, objstr, parent);
    neon_state_defineglobal(state, oclass->name, neon_value_fromobject(oclass));
    return oclass;
}

void neon_class_destroy(NeonObjClass* klass)
{
    NeonState* state;
    state = klass->pvm;
    neon_hashtable_destroy(klass->properties);
    neon_hashtable_destroy(klass->methods);
    neon_hashtable_destroy(klass->staticmethods);
    neon_mem_release(state, sizeof(NeonObjClass), klass);
}


bool neon_class_putvalintable(NeonState* state, NeonObjClass* klass, NeonHashTable* htab, NeonObjString* name, NeonValue val)
{
    bool b;
    (void)state;
    (void)klass;
    b = neon_hashtable_set(htab, name, val);
    return b;
}

bool neon_class_setnativemethod(NeonObjClass* klass, NeonObjString* name, NeonNativeFN fn)
{
    bool b;
    NeonState* state;
    NeonObjNativeFunction* natfn;
    state = ((NeonObject*)klass)->pvm;
    natfn = neon_object_makenative(state, fn, name->sbuf->data);
    b = neon_class_putvalintable(state, klass, klass->methods, name, neon_value_fromobject(natfn));
    return b;
}

bool neon_class_setnativestaticmethod(NeonObjClass* klass, NeonObjString* name, NeonNativeFN fn)
{
    bool b;
    NeonState* state;
    NeonObjNativeFunction* natfn;
    state = ((NeonObject*)klass)->pvm;
    natfn = neon_object_makenative(state, fn, name->sbuf->data);
    b = neon_class_putvalintable(state, klass, klass->staticmethods, name, neon_value_fromobject(natfn));
    return b;
}

bool neon_class_setfunctioncstr(NeonObjClass* klass, const char* name, NeonNativeFN fn)
{
    NeonState* state;
    NeonObjString* os;
    state = ((NeonObject*)klass)->pvm;
    os = neon_string_copycstr(state, name);
    return neon_class_setnativemethod(klass, os, fn);
}

bool neon_class_setstaticfunctioncstr(NeonObjClass* klass, const char* name, NeonNativeFN fn)
{
    NeonState* state;
    NeonObjString* os;
    state = ((NeonObject*)klass)->pvm;
    os = neon_string_copycstr(state, name);
    return neon_class_setnativestaticmethod(klass, os, fn);
}

bool neon_class_getmethod(NeonObjClass* klass, NeonObjString* name, NeonValue* dest)
{
    if(neon_hashtable_get(klass->methods, name, dest))
    {
        return true;
    }
    if(klass->parent != NULL)
    {
        return neon_class_getmethod(klass->parent, name, dest);
    }
    return false;
}

bool neon_class_getstaticmethod(NeonObjClass* klass, NeonObjString* name, NeonValue* dest)
{
    if(neon_hashtable_get(klass->staticmethods, name, dest))
    {
        return true;
    }
    // should we follow the chain for static functions...?
    /*
    if(klass->parent != NULL)
    {
        return neon_class_getstaticmethod(klass->parent, name, dest);
    }
    */
    return false;
}


NeonObjClosure* neon_object_makeclosure(NeonState* state, NeonObjScriptFunction* ofn)
{
    int i;
    NeonObjClosure* closure;
    NeonObjUpvalue** upvals;
    upvals = (NeonObjUpvalue**)neon_mem_allocate(state, sizeof(NeonObjUpvalue*), ofn->upvaluecount, true);
    for(i = 0; i < ofn->upvaluecount; i++)
    {
        upvals[i] = NULL;
    }
    closure = (NeonObjClosure*)neon_object_allocobj(state, sizeof(NeonObjClosure), NEON_OBJ_CLOSURE);
    closure->pvm = state;
    closure->fnptr = ofn;
    closure->upvalues = upvals;
    closure->upvaluecount = ofn->upvaluecount;
    return closure;
}

void neon_closure_mark(NeonObjClosure* closure)
{
    size_t i;
    NeonState* state;
    state = closure->pvm;
    neon_gcmem_markobject(state, (NeonObject*)closure->fnptr);
    for(i = 0; i < (size_t)closure->upvaluecount; i++)
    {
        neon_gcmem_markobject(state, (NeonObject*)closure->upvalues[i]);
    }
}

void neon_closure_destroy(NeonObjClosure* closure)
{
    NeonState* state;
    state = closure->pvm;
    neon_mem_freearray(state, sizeof(NeonObjUpvalue*), closure->upvalues, closure->upvaluecount);
    neon_mem_release(state, sizeof(NeonObjClosure), closure);
}

NeonObjScriptFunction* neon_object_makefunction(NeonState* state)
{
    NeonObjScriptFunction* obj;
    obj = (NeonObjScriptFunction*)neon_object_allocobj(state, sizeof(NeonObjScriptFunction), NEON_OBJ_FUNCTION);
    obj->pvm = state;
    obj->arity = 0;
    obj->upvaluecount = 0;
    obj->name = NULL;
    obj->isvariadic = false;
    obj->blob = neon_blob_make(state);
    return obj;
}

NeonObjInstance* neon_object_makeinstance(NeonState* state, NeonObjClass* klass)
{
    NeonObjInstance* obj;
    obj = (NeonObjInstance*)neon_object_allocobj(state, sizeof(NeonObjInstance), NEON_OBJ_INSTANCE);
    obj->pvm = state;
    obj->klass = klass;
    obj->fields = neon_hashtable_make(state);
    return obj;
}

void neon_instance_mark(NeonObjInstance* instance)
{
    NeonState* state;
    state = instance->pvm;
    neon_gcmem_markobject(state, (NeonObject*)instance->klass);
    neon_hashtable_mark(state, instance->fields);
}

NeonObjNativeFunction* neon_object_makenative(NeonState* state, NeonNativeFN nat, const char* name)
{
    NeonObjNativeFunction* obj;
    obj = (NeonObjNativeFunction*)neon_object_allocobj(state, sizeof(NeonObjNativeFunction), NEON_OBJ_NATIVE);
    obj->pvm = state;
    obj->natfunc = nat;
    obj->name = name;
    return obj;
}

NeonObjString* neon_string_allocfromstrbuf(NeonState* state, StringBuffer* sbuf, uint32_t hash)
{
    NeonObjString* rs;
    rs = (NeonObjString*)neon_object_allocobj(state, sizeof(NeonObjString), NEON_OBJ_STRING);
    rs->pvm = state;
    rs->sbuf = sbuf;
    rs->hash = hash;
    //neon_vm_stackpush(state, neon_value_fromobject(rs));
    neon_hashtable_set(state->strings, rs, neon_value_makenil());
    //neon_vm_stackpop(state);
    return rs;
}

NeonObjString* neon_string_allocate(NeonState* state, const char* estr, size_t elen, uint32_t hash)
{
    StringBuffer* sbuf;
    sbuf = dyn_strbuf_makeempty(elen);
    dyn_strbuf_appendstrn(sbuf, estr, elen);
    return neon_string_allocfromstrbuf(state, sbuf, hash);
}

NeonObjString* neon_string_take(NeonState* state, char* estr, size_t elen)
{
    uint32_t hash;
    NeonObjString* rs;
    hash = neon_util_hashstring(estr, elen);
    rs = neon_hashtable_findstring(state->strings, estr, elen, hash);
    if(rs == NULL)
    {
        rs = neon_string_copy(state, (const char*)estr, elen);
    }
    neon_mem_freearray(state, sizeof(char), estr, elen + 1);
    return rs;
}

NeonObjString* neon_string_copy(NeonState* state, const char* estr, int elen)
{
    uint32_t hash;
    NeonObjString* rs;
    hash = neon_util_hashstring(estr, elen);
    rs = neon_hashtable_findstring(state->strings, estr, elen, hash);
    if(rs != NULL)
    {
        return rs;
    }
    rs = neon_string_allocate(state, estr, elen, hash);
    return rs;
}

NeonObjString* neon_string_copycstr(NeonState* state, const char* estr)
{
    return neon_string_copy(state, estr, strlen(estr));
}

void neon_string_destroy(NeonObjString* os)
{
    NeonState* state;
    state = os->pvm;
    dyn_strbuf_destroy(os->sbuf);
    neon_mem_release(state, sizeof(NeonObjString), os);
}

bool neon_string_append(NeonObjString* os, const char* extstr, size_t extlen)
{
    return dyn_strbuf_appendstrn(os->sbuf, extstr, extlen);
}

int neon_string_find(StringBuffer* str, StringBuffer* substr, size_t startidx, bool icase)
{
    bool found;
    bool matching;
    int index;
    size_t i;
    size_t j;
    size_t slen;
    size_t sublen;
    char subch;
    char selfch;
    slen = str->length;
    sublen = substr->length;
    index = -1;
    if (sublen <= slen)
    {
        found = false;
        for (i = startidx; !found && i < slen - sublen + 1; ++i)
        {
            matching = true;
            for(j = 0; matching && j < sublen; ++j)
            {
                selfch = str->data[i + j];
                subch = substr->data[j];
                if(icase)
                {
                    selfch = tolower(selfch);
                    subch = tolower(subch);
                }
                if(selfch != subch)
                {
                    matching = false;
                }
            }
            if (matching)
            {
                index = i;
                found = true;
            }
        }
    }
    return index;
}

bool neon_string_split(NeonState* state, StringBuffer* str, StringBuffer* sep, NeonValArray* dest)
{
    int matchidx;
    size_t sublen;
    size_t lastidx;
    NeonObjString* rest;
    NeonObjString *substr;
    lastidx = 0;
    if(sep->length == 0)
    {
        neon_valarray_push(dest, neon_value_fromobject(neon_string_copy(state, str->data, str->length)));
    }
    else
    {
        if(sep->length <= str->length)
        {
            while(lastidx < ((str->length - sep->length) + 1))
            {
                matchidx = neon_string_find(str, sep, lastidx, false);
                // if the seperator is not found again, break
                if (matchidx == -1)
                {
                    break;
                }
                else
                {
                    sublen = (size_t)matchidx - lastidx;
                    // get the substring and push it
                    substr = neon_string_copy(state, &str->data[lastidx], sublen);
                    neon_valarray_push(dest, neon_value_fromobject(substr));
                    lastidx = (size_t)matchidx + sep->length;
                }
            }
        }
        // push the rest
        rest = neon_string_copy(state, &str->data[lastidx], str->length - lastidx);
        neon_valarray_push(dest, neon_value_fromobject(rest));
    }
    return true;
}


NeonObjArray* neon_array_makefromvalarray(NeonState* state, NeonValArray* va)
{
    NeonObjArray* oa;
    oa = (NeonObjArray*)neon_object_allocobj(state, sizeof(NeonObjArray), NEON_OBJ_ARRAY);
    oa->pvm = state;
    oa->vala = va;
    oa->vala->size = 0;
    return oa;
}

NeonObjArray* neon_array_make(NeonState* state)
{
    NeonValArray* va;
    va = neon_valarray_make(state);
    return neon_array_makefromvalarray(state, va);
}

void neon_array_mark(NeonObjArray* arr)
{
    NeonState* state;
    state = arr->pvm;
    neon_gcmem_markvalarray(state, arr->vala);
}

void neon_array_destroy(NeonObjArray* arr)
{
    NeonState* state;
    state = arr->pvm;
    neon_valarray_destroy(arr->vala);
    neon_mem_release(state, sizeof(NeonObjArray), arr);
}

bool neon_array_push(NeonObjArray* arr, NeonValue val)
{
    neon_valarray_push(arr->vala, val);
    return true;
}

NeonValue neon_array_get(NeonObjArray* arr, size_t idx)
{
    return neon_valarray_get(arr->vala, idx);
}

size_t neon_array_count(NeonObjArray* arr)
{
    return neon_valarray_count(arr->vala);
}

NeonObjMap* neon_object_makemapfromtable(NeonState* state, NeonHashTable* tbl, bool shouldmark)
{
    NeonObjMap* obj;
    obj = (NeonObjMap*)neon_object_allocobj(state, sizeof(NeonObjMap), NEON_OBJ_MAP);
    obj->pvm = state;
    if(tbl == NULL)
    {
        obj->mapping = neon_hashtable_make(state);
    }
    else
    {
        obj->mapping = tbl;
    }
    obj->shouldmark = shouldmark;
    return obj;
}

NeonObjMap* neon_object_makemap(NeonState* state)
{
    return neon_object_makemapfromtable(state, NULL, true);
}

NeonObjArray* neon_object_makearray(NeonState* state)
{
    return neon_array_make(state);
}


bool neon_map_set(NeonObjMap* map, NeonObjString* name, NeonValue val)
{
    return neon_hashtable_set(map->mapping, name, val);
}

bool neon_map_setstr(NeonObjMap* map, const char* name, NeonValue val)
{
    NeonObjString* os;
    os = neon_string_copy(map->pvm, name, strlen(name));
    return neon_map_set(map, os, val);
}

bool neon_map_setstrobject(NeonObjMap* map, const char* name, NeonObject* val)
{
    return neon_map_setstr(map, name, neon_value_fromobject(val));
}

bool neon_map_setstrfunction(NeonObjMap* map, const char* name, NeonNativeFN nat)
{
    return neon_map_setstr(map, name, neon_value_fromobject(neon_object_makenative(map->pvm, nat, name)));
}

bool neon_map_get(NeonObjMap* map, NeonObjString* key, NeonValue* dest)
{
    return neon_hashtable_get(map->mapping, key, dest);
}

void neon_map_mark(NeonObjMap* map)
{
    NeonState* state;
    state = map->pvm;
    if(map->shouldmark)
    {
        neon_hashtable_mark(state, map->mapping);
    }
}

void neon_map_destroy(NeonObjMap* map)
{
    NeonState* state;
    state = map->pvm;
    if(map->shouldmark)
    {
        neon_hashtable_destroy(map->mapping);
    }
    neon_mem_release(state, sizeof(NeonObjMap), map);
}


NeonHashTable* neon_hashtable_make(NeonState* state)
{
    NeonHashTable* tbl;
    tbl = (NeonHashTable*)malloc(sizeof(NeonHashTable));
    memset(tbl, 0, sizeof(NeonHashTable));
    tbl->pvm = state;
    tbl->count = 0;
    tbl->capacity = 0;
    tbl->entries = NULL;
    return tbl;
}

void neon_hashtable_mark(NeonState* state, NeonHashTable* table)
{
    int i;
    NeonHashEntry* entry;
    for(i = 0; i < table->capacity; i++)
    {
        entry = &table->entries[i];
        neon_gcmem_markobject(state, (NeonObject*)entry->key);
        neon_gcmem_markvalue(state, entry->value);
    }
}

void neon_hashtable_destroy(NeonHashTable* table)
{
    NeonState* state;
    state = table->pvm;
    neon_mem_freearray(state, sizeof(NeonHashEntry), table->entries, table->capacity);
    free(table);
}

// NOTE: The "optimization" chapter has a manual copy of this function.
// If you change it here, make sure to update that copy.
NeonHashEntry* neon_hashtable_findentry(NeonHashEntry* entries, int capacity, NeonObjString* key)
{
    uint32_t index;
    NeonHashEntry* entry;
    NeonHashEntry* tombstone;
    index = key->hash & (capacity - 1);
    tombstone = NULL;
    if(capacity > 0)
    {
        for(;;)
        {
            entry = &entries[index];
            if(entry != NULL)
            {
                if(entry->key == NULL)
                {
                    if(neon_value_isnil(entry->value))
                    {
                        // Empty entry.
                        return tombstone != NULL ? tombstone : entry;
                    }
                    else
                    {
                        // We found a tombstone.
                        if(tombstone == NULL)
                        {
                            tombstone = entry;
                        }
                    }
                }
                else if(entry->key == key)
                {
                    // We found the key.
                    return entry;
                }
            }
            index = (index + 1) & (capacity - 1);
        }
    }
    return NULL;
}

bool neon_hashtable_get(NeonHashTable* table, NeonObjString* key, NeonValue* value)
{
    NeonHashEntry* entry;
    if(table->count == 0)
    {
        return false;
    }
    entry = neon_hashtable_findentry(table->entries, table->capacity, key);
    if(entry == NULL)
    {
        return false;
    }
    if(entry->key == NULL)
    {
        return false;
    }
    *value = entry->value;
    return true;
}

void neon_hashtable_adjustcap(NeonState* state, NeonHashTable* table, int capacity)
{
    int i;
    NeonHashEntry* dest;
    NeonHashEntry* entry;
    NeonHashEntry* entries;
    entries = (NeonHashEntry*)neon_mem_allocate(state, sizeof(NeonHashEntry), capacity, true);
    for(i = 0; i < capacity; i++)
    {
        entries[i].key = NULL;
        entries[i].value = neon_value_makenil();
    }
    table->count = 0;
    for(i = 0; i < table->capacity; i++)
    {
        entry = &table->entries[i];
        if(entry->key == NULL)
        {
            continue;
        }
        dest = neon_hashtable_findentry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }
    neon_mem_freearray(state, sizeof(NeonHashEntry), table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool neon_hashtable_setchecked(NeonHashTable* table, NeonObjString* key, NeonValue value, bool* hasfailed)
{
    int capacity;
    bool isnewkey;
    NeonState* state;
    NeonHashEntry* entry;
    *hasfailed = false;
    state = table->pvm;
    if(table->count + 1 > table->capacity * NEON_MAX_TABLELOAD)
    {
        capacity = NEON_NEXTCAPACITY(table->capacity);
        neon_hashtable_adjustcap(state, table, capacity);
    }
    entry = neon_hashtable_findentry(table->entries, table->capacity, key);
    isnewkey = entry->key == NULL;
    if(isnewkey && neon_value_isnil(entry->value))
    {
        table->count++;
    }
    entry->key = key;
    entry->value = value;
    return isnewkey;
}

bool neon_hashtable_set(NeonHashTable* table, NeonObjString* key, NeonValue value)
{
    bool b;
    bool isnew;
    (void)isnew;
    isnew = neon_hashtable_setchecked(table, key, value, &b);
    if(b)
    {
        return false;
    }
    return true;
}

bool neon_hashtable_setstr(NeonHashTable* tab, const char* name, NeonValue val)
{
    NeonObjString* os;
    os = neon_string_copy(tab->pvm, name, strlen(name));
    return neon_hashtable_set(tab, os, val);
}

bool neon_hashtable_setstrfunction(NeonHashTable* tab, const char* name, NeonNativeFN nat)
{
    return neon_hashtable_setstr(tab, name, neon_value_fromobject(neon_object_makenative(tab->pvm, nat, name)));
}

bool neon_hashtable_delete(NeonState* state, NeonHashTable* table, NeonObjString* key)
{
    NeonHashEntry* entry;
    (void)state;
    if(table->count == 0)
    {
        return false;
    }
    // Find the entry.
    entry = neon_hashtable_findentry(table->entries, table->capacity, key);
    if(entry->key == NULL)
    {
        return false;
    }
    // Place a tombstone in the entry.
    entry->key = NULL;
    entry->value = neon_value_makebool(true);
    return true;
}

void neon_hashtable_addall(NeonHashTable* from, NeonHashTable* to)
{
    int i;
    NeonHashEntry* entry;
    for(i = 0; i < from->capacity; i++)
    {
        entry = &from->entries[i];
        if(entry->key != NULL)
        {
            neon_hashtable_set(to, entry->key, entry->value);
        }
    }
}

NeonObjString* neon_hashtable_findstring(NeonHashTable* table, const char* estr, size_t elen, uint32_t hash)
{
    uint32_t index;
    NeonHashEntry* entry;
    NeonState* state;
    (void)state;
    state = table->pvm;
    if(table->count == 0)
    {
        return NULL;
    }
    index = hash & (table->capacity - 1);
    for(;;)
    {
        entry = &table->entries[index];
        if(entry->key == NULL)
        {
            // Stop if we find an empty non-tombstone entry.
            if(neon_value_isnil(entry->value))
            {
                return NULL;
            }
        }
        else if(entry->key->sbuf->length == elen && entry->key->hash == hash && memcmp(entry->key->sbuf->data, estr, elen) == 0)
        {
            // We found it.
            return entry->key;
        }
        index = (index + 1) & (table->capacity - 1);
    }
    return NULL;
}

void neon_hashtable_remwhite(NeonState* state, NeonHashTable* table)
{
    size_t i;
    NeonHashEntry* entry;
    for(i = 0; i < (size_t)table->capacity; i++)
    {
        entry = &table->entries[i];
        //if(entry->key != NULL && !entry->key->objpadding.ismarked)
        if(entry->key != NULL && (!((NeonObject*)entry->key)->ismarked))
        {
            neon_hashtable_delete(state, table, entry->key);
        }
    }
}

NeonBinaryBlob* neon_blob_make(NeonState* state)
{
    NeonBinaryBlob* blob;
    blob = (NeonBinaryBlob*)malloc(sizeof(NeonBinaryBlob));
    blob->count = 0;
    blob->capacity = 0;
    blob->bincode = NULL;
    blob->srclinenos = NULL;
    blob->constants = neon_valarray_make(state);
    return blob;
}

void neon_blob_destroy(NeonState* state, NeonBinaryBlob* blob)
{
    neon_mem_freearray(state, sizeof(int32_t), blob->bincode, blob->capacity);
    neon_mem_freearray(state, sizeof(int), blob->srclinenos, blob->capacity);
    neon_valarray_destroy(blob->constants);
    free(blob);
}

void neon_blob_pushbyte(NeonState* state, NeonBinaryBlob* blob, int32_t byte, int line)
{
    int oldcap;
    if(blob->capacity < blob->count + 1)
    {
        oldcap = blob->capacity;
        blob->capacity = NEON_NEXTCAPACITY(oldcap);
        blob->bincode = (int32_t*)neon_mem_growarray(state, sizeof(int32_t), blob->bincode, oldcap, blob->capacity);
        blob->srclinenos = (int*)neon_mem_growarray(state, sizeof(int), blob->srclinenos, oldcap, blob->capacity);
    }
    blob->bincode[blob->count] = byte;
    blob->srclinenos[blob->count] = line;
    blob->count++;
}

int neon_blob_pushconst(NeonState* state, NeonBinaryBlob* blob, NeonValue value)
{
    (void)state;
    //neon_vm_stackpush(state, value);
    neon_valarray_push(blob->constants, value);
    //neon_vm_stackpop(state);
    return neon_valarray_count(blob->constants) - 1;
}

void neon_blob_disasm(NeonState* state, NeonWriter* wr, NeonBinaryBlob* blob, const char* name)
{
    int offset;
    neon_writer_writefmt(wr, "== %s ==\n", name);
    for(offset = 0; offset < blob->count;)
    {
        offset = neon_dbg_dumpdisasm(state, wr, blob, offset);
    }
}

const char* neon_dbg_op2str(int32_t opcode)
{
    switch(opcode)
    {
        case NEON_OP_PUSHCONST: return "NEON_OP_PUSHCONST";
        case NEON_OP_PUSHNIL: return "NEON_OP_PUSHNIL";
        case NEON_OP_PUSHTRUE: return "NEON_OP_PUSHTRUE";
        case NEON_OP_PUSHFALSE: return "NEON_OP_PUSHFALSE";
        case NEON_OP_PUSHONE: return "NEON_OP_PUSHONE";
        case NEON_OP_POPONE: return "NEON_OP_POPONE";
        case NEON_OP_POPN: return "NEON_OP_POPN";
        case NEON_OP_DUP: return "NEON_OP_DUP";
        case NEON_OP_TYPEOF: return "NEON_OP_TYPEOF";
        case NEON_OP_LOCALGET: return "NEON_OP_LOCALGET";
        case NEON_OP_LOCALSET: return "NEON_OP_LOCALSET";
        case NEON_OP_GLOBALGET: return "NEON_OP_GLOBALGET";
        case NEON_OP_GLOBALDEFINE: return "NEON_OP_GLOBALDEFINE";
        case NEON_OP_GLOBALSET: return "NEON_OP_GLOBALSET";
        case NEON_OP_UPVALGET: return "NEON_OP_UPVALGET";
        case NEON_OP_UPVALSET: return "NEON_OP_UPVALSET";
        case NEON_OP_PROPERTYGET: return "NEON_OP_PROPERTYGET";
        case NEON_OP_PROPERTYSET: return "NEON_OP_PROPERTYSET";
        case NEON_OP_INSTGETSUPER: return "NEON_OP_INSTGETSUPER";
        case NEON_OP_EQUAL: return "NEON_OP_EQUAL";
        case NEON_OP_PRIMGREATER: return "NEON_OP_PRIMGREATER";
        case NEON_OP_PRIMLESS: return "NEON_OP_PRIMLESS";
        case NEON_OP_PRIMADD: return "NEON_OP_PRIMADD";
        case NEON_OP_PRIMSUBTRACT: return "NEON_OP_PRIMSUBTRACT";
        case NEON_OP_PRIMMULTIPLY: return "NEON_OP_PRIMMULTIPLY";
        case NEON_OP_PRIMDIVIDE: return "NEON_OP_PRIMDIVIDE";
        case NEON_OP_PRIMMODULO: return "NEON_OP_PRIMMODULO";
        case NEON_OP_PRIMSHIFTLEFT: return "NEON_OP_PRIMSHIFTLEFT";
        case NEON_OP_PRIMSHIFTRIGHT: return "NEON_OP_PRIMSHIFTRIGHT";
        case NEON_OP_PRIMBINAND: return "NEON_OP_PRIMBINAND";
        case NEON_OP_PRIMBINOR: return "NEON_OP_PRIMBINOR";
        case NEON_OP_PRIMBINXOR: return "NEON_OP_PRIMBINXOR";
        case NEON_OP_PRIMBINNOT: return "NEON_OP_PRIMBINNOT";
        case NEON_OP_PRIMNOT: return "NEON_OP_PRIMNOT";
        case NEON_OP_PRIMNEGATE: return "NEON_OP_PRIMNEGATE";
        case NEON_OP_DEBUGPRINT: return "NEON_OP_DEBUGPRINT";
        case NEON_OP_GLOBALSTMT: return "NEON_OP_GLOBALSTMT";
        case NEON_OP_JUMPNOW: return "NEON_OP_JUMPNOW";
        case NEON_OP_JUMPIFFALSE: return "NEON_OP_JUMPIFFALSE";
        case NEON_OP_LOOP: return "NEON_OP_LOOP";
        case NEON_OP_CALL: return "NEON_OP_CALL";
        case NEON_OP_INSTTHISINVOKE: return "NEON_OP_INSTTHISINVOKE";
        case NEON_OP_INSTSUPERINVOKE: return "NEON_OP_INSTSUPERINVOKE";
        case NEON_OP_CLOSURE: return "NEON_OP_CLOSURE";
        case NEON_OP_UPVALCLOSE: return "NEON_OP_UPVALCLOSE";
        case NEON_OP_RETURN: return "NEON_OP_RETURN";
        case NEON_OP_CLASS: return "NEON_OP_CLASS";
        case NEON_OP_INHERIT: return "NEON_OP_INHERIT";
        case NEON_OP_METHOD: return "NEON_OP_METHOD";
        case NEON_OP_MAKEARRAY: return "NEON_OP_MAKEARRAY";
        case NEON_OP_MAKEMAP: return "NEON_OP_MAKEMAP";
        case NEON_OP_INDEXGET: return "NEON_OP_INDEXGET";
        case NEON_OP_INDEXSET: return "NEON_OP_INDEXSET";
        case NEON_OP_RESTOREFRAME: return "NEON_OP_RESTOREFRAME";
        case NEON_OP_HALTVM: return "NEON_OP_HALTVM";
        case NEON_OP_PSEUDOBREAK: return "NEON_OP_PSEUDOBREAK";
    }
    return "?unknown?";
}


int neon_dbg_dumpconstinstr(NeonState* state, NeonWriter* wr, const char* name, NeonBinaryBlob* blob, int offset)
{
    NeonValue val;
    int32_t constant;
    (void)state;
    val = neon_value_makenil();
    constant = blob->bincode[offset + 1];
    if(blob->constants->size > 0)
    {
        val = blob->constants->values[constant];
    }
    neon_writer_writefmt(wr, "%-16s %4d '", name, constant);
    neon_writer_printvalue(wr, val, true);
    neon_writer_writestring(wr, "'\n");
    return offset + 2;
}

int neon_dbg_dumpinvokeinstr(NeonState* state, NeonWriter* wr, const char* name, NeonBinaryBlob* blob, int offset)
{
    int32_t argc;
    int32_t constant;
    (void)state;
    constant = blob->bincode[offset + 1];
    argc = blob->bincode[offset + 2];
    neon_writer_writefmt(wr, "%-16s (%d args) %4d {", name, argc, constant);
    neon_writer_printvalue(wr, blob->constants->values[constant], true);
    neon_writer_writestring(wr, "}\n");
    return offset + 3;
}

int neon_dbg_dumpsimpleinstr(NeonState* state, NeonWriter* wr, const char* name, int offset)
{
    (void)state;
    neon_writer_writefmt(wr, "%s\n", name);
    return offset + 1;
}

int neon_dbg_dumpbyteinstr(NeonState* state, NeonWriter* wr, const char* name, NeonBinaryBlob* blob, int offset)
{
    int32_t islot;
    (void)state;
    islot = blob->bincode[offset + 1];
    neon_writer_writefmt(wr, "%-16s %4d\n", name, islot);
    return offset + 2;// [debug]
}

int neon_dbg_dumpjumpinstr(NeonState* state, NeonWriter* wr, const char* name, int sign, NeonBinaryBlob* blob, int offset)
{
    uint16_t jump;
    (void)state;
    jump= (uint16_t)(blob->bincode[offset + 1] << 8);
    jump |= blob->bincode[offset + 2];
    neon_writer_writefmt(wr, "%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

int neon_dbg_dumpclosure(NeonState* state, NeonWriter* wr, NeonBinaryBlob* blob, int offset)
{
    int j;
    int index;
    int islocal;
    int32_t constant;
    NeonObjScriptFunction* fn;
    (void)state;
    offset++;
    constant = blob->bincode[offset++];
    neon_writer_writefmt(wr, "%-16s %4d ", "NEON_OP_CLOSURE", constant);
    neon_writer_printvalue(wr, blob->constants->values[constant], true);
    neon_writer_writestring(wr, "\n");
    fn = neon_value_asscriptfunction(blob->constants->values[constant]);
    for(j = 0; j < fn->upvaluecount; j++)
    {
        islocal = blob->bincode[offset++];
        index = blob->bincode[offset++];
        neon_writer_writefmt(wr, "%04d      |                     %s %d\n", offset - 2, islocal ? "local" : "upvalue", index);
    }
    return offset;
}

int neon_dbg_dumpdisasm(NeonState* state, NeonWriter* wr, NeonBinaryBlob* blob, int offset)
{
    int32_t instruction;
    const char* instrname;
    (void)state;
    neon_writer_writefmt(wr, "%04d ", offset);
    if((offset > 0) && (blob->srclinenos[offset] == blob->srclinenos[offset - 1]))
    {
        neon_writer_writefmt(wr, "   | ");
    }
    else
    {
        neon_writer_writefmt(wr, "%4d ", blob->srclinenos[offset]);
    }
    instruction = blob->bincode[offset];
    instrname = neon_dbg_op2str(instruction);
    switch(instruction)
    {
        case NEON_OP_PUSHCONST:
            return neon_dbg_dumpconstinstr(state, wr, instrname, blob, offset);
        case NEON_OP_PUSHONE:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_PUSHNIL:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_PUSHTRUE:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_PUSHFALSE:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_MAKEARRAY:
            return neon_dbg_dumpconstinstr(state, wr, instrname, blob, offset);
        case NEON_OP_MAKEMAP:
            return neon_dbg_dumpconstinstr(state, wr, instrname, blob, offset);
        case NEON_OP_INDEXGET:
            return neon_dbg_dumpbyteinstr(state, wr, instrname, blob, offset);
        case NEON_OP_INDEXSET:
            return neon_dbg_dumpbyteinstr(state, wr, instrname, blob, offset);
        case NEON_OP_POPONE:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_LOCALGET:
            return neon_dbg_dumpbyteinstr(state, wr, instrname, blob, offset);
        case NEON_OP_LOCALSET:
            return neon_dbg_dumpbyteinstr(state, wr, instrname, blob, offset);
        case NEON_OP_GLOBALGET:
            return neon_dbg_dumpconstinstr(state, wr, instrname, blob, offset);
        case NEON_OP_GLOBALDEFINE:
            return neon_dbg_dumpconstinstr(state, wr, instrname, blob, offset);
        case NEON_OP_GLOBALSET:
            return neon_dbg_dumpconstinstr(state, wr, instrname, blob, offset);
        case NEON_OP_UPVALGET:
            return neon_dbg_dumpbyteinstr(state, wr, instrname, blob, offset);
        case NEON_OP_UPVALSET:
            return neon_dbg_dumpbyteinstr(state, wr, instrname, blob, offset);
        case NEON_OP_PROPERTYGET:
            return neon_dbg_dumpconstinstr(state, wr, instrname, blob, offset);
        case NEON_OP_PROPERTYSET:
            return neon_dbg_dumpconstinstr(state, wr, instrname, blob, offset);
        case NEON_OP_INSTGETSUPER:
            return neon_dbg_dumpconstinstr(state, wr, instrname, blob, offset);
        case NEON_OP_EQUAL:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_PRIMGREATER:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_PRIMLESS:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_PRIMADD:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_PRIMSUBTRACT:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_PRIMMULTIPLY:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_PRIMDIVIDE:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_PRIMNOT:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_PRIMNEGATE:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_DEBUGPRINT:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_GLOBALSTMT:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_JUMPNOW:
            return neon_dbg_dumpjumpinstr(state, wr, instrname, 1, blob, offset);
        case NEON_OP_JUMPIFFALSE:
            return neon_dbg_dumpjumpinstr(state, wr, instrname, 1, blob, offset);
        case NEON_OP_LOOP:
            return neon_dbg_dumpjumpinstr(state, wr, instrname, -1, blob, offset);
        case NEON_OP_CALL:
            return neon_dbg_dumpbyteinstr(state, wr, instrname, blob, offset);
        case NEON_OP_INSTTHISINVOKE:
            return neon_dbg_dumpinvokeinstr(state, wr, instrname, blob, offset);
        case NEON_OP_INSTSUPERINVOKE:
            return neon_dbg_dumpinvokeinstr(state, wr, instrname, blob, offset);
        case NEON_OP_CLOSURE:
            {
                /*offset =*/ neon_dbg_dumpclosure(state, wr, blob, offset);
                return offset;
            }
        case NEON_OP_UPVALCLOSE:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_RETURN:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_CLASS:
            return neon_dbg_dumpconstinstr(state, wr, instrname, blob, offset);
        case NEON_OP_INHERIT:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        case NEON_OP_METHOD:
            return neon_dbg_dumpconstinstr(state, wr, instrname, blob, offset);
        case NEON_OP_HALTVM:
            return neon_dbg_dumpsimpleinstr(state, wr, instrname, offset);
        /*
        default:
            neon_writer_writefmt(wr, "!!!!unknown opcode %d!!!!\n", instruction);
            return offset + 1;
            */
    }
    return offset + 1;
}


#include "parse.h"

NeonObjScriptFunction* neon_astparser_compilesource(NeonState* state, const char* source, size_t len, bool iseval)
{
    
    NeonAstCompiler compiler;
    NeonObjScriptFunction* fn;
    NeonObjScriptFunction* rtv;
    state->parser = neon_astparser_make(state);
    state->parser->iseval = iseval;
    state->parser->pscn = neon_astlex_make(state, source, len);
    neon_astparser_compilerinit(state->parser, &compiler, NEON_TYPE_SCRIPT, NULL);
    neon_astparser_advance(state->parser);
    while(!neon_astparser_match(state->parser, NEON_TOK_EOF))
    {
        neon_astparser_parsedecl(state->parser);
    }
    fn = neon_astparser_compilerfinish(state->parser, true);
    rtv = fn;
    if(state->parser->haderror)
    {
        rtv = NULL;
    }
    neon_astparser_destroy(state->parser);
    state->parser = NULL;
    return rtv;
}

void neon_astparser_markcompilerroots(NeonState* state)
{
    NeonAstCompiler* compiler;
    if(state->parser != NULL)
    {
        compiler = state->parser->currcompiler;
        while(compiler != NULL)
        {
            neon_gcmem_markobject(state, (NeonObject*)compiler->currfunc);
            compiler = compiler->enclosing;
        }
    }
}

void neon_state_defineglobal(NeonState* state, NeonObjString* name, NeonValue val)
{
    neon_hashtable_set(state->globals, name, val);
}


void neon_state_vraiseerror(NeonState* state, const char* format, va_list va)
{
    int itd;
    int frmcnt;
    size_t count;
    size_t instruction;
    NeonCallFrame* frame;
    NeonObjScriptFunction* fn;
    state->vmstate.hasraised = true;
    vfprintf(stderr, format, va);
    fputs("\n", stderr);
    count = 0;
    frmcnt = state->vmstate.framecount;
    for(itd = frmcnt - 1; itd >= 0; itd--)
    {
        frame = &state->vmstate.framevalues[itd];
        /* Calls and Functions runtime-error-stack < Closures runtime-error-function */
        // NeonObjScriptFunction* function = frame->fnptr;
        fn = frame->closure->fnptr;
        //instruction = frame->instrucptr - fn->blob->bincode - 1;
        instruction = frame->instrucidx;
        fprintf(stderr, "[line %d] in ", fn->blob->srclinenos[instruction]);
        if(fn->name == NULL)
        {
            fprintf(stderr, "script\n");
        }
        else
        {
            fprintf(stderr, "%s()\n", fn->name->sbuf->data);
        }
        count++;
        if(count == 15)
        {
            fprintf(stderr, "too many frames; %d frames in total. skipping the rest\n", frmcnt);
            break;
        }
    }
    neon_vm_stackreset(state);
}

void neon_state_raiseerror(NeonState* state, const char* format, ...)
{
    va_list va;
    va_start(va, format);
    neon_state_vraiseerror(state, format, va);
    va_end(va);
}

void neon_state_defvalue(NeonState* state, const char* name, NeonValue val)
{
    NeonObjString* os;
    os = neon_string_copy(state, name, (int)strlen(name));
    #if 1
        neon_vm_stackpush(state, neon_value_fromobject(os));
        {
            neon_vm_stackpush(state, val);
            {
                neon_hashtable_set(state->globals, neon_value_asstring(state->vmstate.stackvalues[0]), state->vmstate.stackvalues[1]);
            }
            neon_vm_stackpop(state);
        }
        neon_vm_stackpop(state);
    #else
        neon_hashtable_set(state->globals, os, val);
    #endif
}

void neon_state_defnative(NeonState* state, const char* name, NeonNativeFN nat)
{
    NeonObjNativeFunction* ofn;
    ofn = neon_object_makenative(state, nat, name);
    return neon_state_defvalue(state, name, neon_value_fromobject(ofn));
}


void neon_vm_stackinit(NeonState* state)
{
    NeonVMStateVars* vm;
    vm = &state->vmstate;
    vm->framevalues = (NeonCallFrame*)malloc((NEON_MAX_VMFRAMES + 1) * sizeof(NeonCallFrame));
    vm->stackvalues = (NeonValue*)malloc((NEON_MAX_VMSTACK + 1) * sizeof(NeonValue));
    vm->stackcapacity = NEON_MAX_VMSTACK;
    vm->framecapacity = NEON_MAX_VMFRAMES;
}

/*
* grows vmstate.(stack|frame)values, respectively.
* currently it works fine with mob.js (man-or-boy test), although
* there are still some invalid reads regarding the closure;
* almost definitely because the pointer address changes.
*/
void neon_vm_stackresize(NeonState* state, size_t needed)
{
    size_t oldsz;
    size_t newsz;
    size_t allocsz;
    size_t nforvals;
    void* oldbuf;
    void* newbuf;
    /*
    * needed size for $stackvalues is length($framevalues)*2
    */
    nforvals = (needed * 2);
    //fprintf(stderr, "*** resizing the stack ***\n");
    /*
    * keep closure address. though this might be incorrect...
    */
    {
        oldsz = state->vmstate.stackcapacity;
        newsz = oldsz + nforvals;
        allocsz = ((newsz + 1) * sizeof(NeonValue));
        #if 1
            oldbuf = state->vmstate.stackvalues;
            newbuf = realloc(oldbuf, allocsz);
            if(newbuf == NULL)
            {
                fprintf(stderr, "internal error: failed to resize stackvalues!\n");
                abort();
            }
        #endif
        state->vmstate.stackvalues = (NeonValue*)newbuf;
        state->vmstate.stackcapacity = newsz;
    }
    {
        oldsz = state->vmstate.framecapacity;
        newsz = oldsz + needed;
        allocsz = ((newsz + 1) * sizeof(NeonCallFrame));
        #if 1
            oldbuf = state->vmstate.framevalues;
            newbuf = realloc(oldbuf, allocsz);
            if(newbuf == NULL)
            {
                fprintf(stderr, "internal error: failed to resize framevalues!\n");
                abort();
            }
        #endif
        state->vmstate.framevalues = (NeonCallFrame*)newbuf;
        state->vmstate.framecapacity = newsz;
    }
    /*
    * this bit is crucial: realloc changes pointer addresses, and to keep the
    * current frame, re-read it from the new address.
    */
    state->vmstate.activeframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
}

void neon_vm_stackgrow(NeonState* state)
{
    /*
    * increasing by 1024 may seem extreme, but actually uses the least amount of memory,
    * without using too much.
    * decreasing (for example, to 256) will quadruple memory usage!!!
    * the idea is, the fewer times the stack is being resized, the less memory is being used.
    */
    neon_vm_stackresize(state, 1024);
}

void neon_vm_stackmaybegrow(NeonState* state, size_t pos)
{
    if(state->vmstate.stackcapacity < (pos + 1))
    {
        neon_vm_stackgrow(state);
    }
}

void neon_vm_framesmaybegrow(NeonState* state, size_t pos)
{
    if(state->vmstate.framecapacity < (pos + 1))
    {
        neon_vm_stackgrow(state);
    }
}

void neon_vm_stackreset(NeonState* state)
{
    state->vmstate.hasraised = false;
    state->vmstate.stacktop = 0;
    state->vmstate.framecount = 0;
    state->openupvalues = NULL;
}

static inline NeonValue neon_vmbits_stackpeek(NeonState* state, int distance)
{
    return state->vmstate.stackvalues[state->vmstate.stacktop + (-1 - distance)];
}

static inline void neon_vmbits_stackpush(NeonState* state, NeonValue value)
{
    neon_vm_stackmaybegrow(state, state->vmstate.stacktop + 0);
    state->vmstate.stackvalues[state->vmstate.stacktop] = value;
    state->vmstate.stacktop++;
}

static inline NeonValue neon_vmbits_stackpop(NeonState* state)
{
    NeonValue v;

    /*
        vm.stackTop--;
        return *vm.stackTop;
    */


    state->vmstate.stacktop--;
    v = state->vmstate.stackvalues[state->vmstate.stacktop + 0];

    return v;
}

static inline void neon_vmbits_stackpopn(NeonState* state, int32_t n)
{
    state->vmstate.stacktop -= n;
}

void neon_vm_stackpush(NeonState* state, NeonValue value)
{
    return neon_vmbits_stackpush(state, value);
}

NeonValue neon_vm_stackpop(NeonState* state)
{
    return neon_vmbits_stackpop(state);
}

void neon_vm_stackpopn(NeonState* state, int32_t n)
{
    return neon_vmbits_stackpopn(state, n);
}

NeonValue neon_vm_stackpeek(NeonState* state, int distance)
{
    return neon_vmbits_stackpeek(state, distance);
}

void neon_state_initobjvars(NeonState* state)
{
    NeonObjClass* objclass;
    state->objvars.classprimobject = neon_state_makeclass(state, "Object", NULL);
    objclass = state->objvars.classprimobject;
    state->objvars.classprimnumber = neon_state_makeclass(state, "Number", objclass);
    state->objvars.classprimstring = neon_state_makeclass(state, "String", objclass);
    state->objvars.classprimarray = neon_state_makeclass(state, "Array", objclass);
}

void neon_state_releaseobjvars(NeonState* state)
{
    (void)state;
    /*
    neon_hashtable_destroy(state->objvars.mthnumber);
    neon_hashtable_destroy(state->objvars.mtharray);
    neon_hashtable_destroy(state->objvars.mthstring);
    neon_hashtable_destroy(state->objvars.mthobject);
    */
}

void neon_state_makeenvmap(NeonState* state)
{
    size_t i;
    size_t j;
    size_t rawlen;
    size_t klen;
    size_t vlen;
    const char* rawkey;
    const char* rawval;
    NeonObjString* oskey;
    NeonObjString* osval;
    state->envvars = neon_object_makemap(state);
    for(i=0; environ[i]!=NULL; i++)
    {
        rawlen = strlen(environ[i]);
        klen = 0;
        for(j=0; j<rawlen; j++)
        {
            if(environ[i][j] == '=')
            {
                break;
            }
            klen++;
        }
        rawkey = &environ[i][0];
        /* value is rawstring + length-of(rawkey) plus 1 */
        rawval = &environ[i][klen + 1];
        /* value is length-of(rawstring) minus length-of(key) minus 1 */
        vlen = (rawlen - klen) - 1;
        oskey = neon_string_copy(state, rawkey, klen);
        osval = neon_string_copy(state, rawval, vlen);
        neon_map_set(state->envvars, oskey, neon_value_fromobject(osval));
    }
}

NeonState* neon_state_make()
{
    NeonState* state;
    state = (NeonState*)malloc(sizeof(NeonState));
    if(state == NULL)
    {
        return NULL;
    }
    memset(state, 0, sizeof(NeonState));
    state->conf.shouldprintruntime = false;
    state->conf.strictmode = false;
    state->gcstate.allowed = true;
    state->gcstate.linkedobjects = NULL;
    state->gcstate.bytesallocd = 0;
    state->gcstate.nextgc = 1024 * 1024;
    state->gcstate.graycount = 0;
    state->gcstate.graycap = 0;
    state->gcstate.graystack = NULL;
    neon_vm_stackinit(state);
    neon_vm_stackreset(state);
    state->vmstate.topclosure = NULL;
    state->vmstate.iseval = false;
    state->vmstate.havekeeper = false;
    state->parser = NULL;
    state->stdoutwriter = neon_writer_makeio(state, stdout, false);
    state->stderrwriter = neon_writer_makeio(state, stderr, false);
    state->globals = neon_hashtable_make(state);
    state->strings = neon_hashtable_make(state);
    state->initstring = NULL;
    state->initstring = neon_string_copy(state, "constructor", 11);
    neon_state_initobjvars(state);
    neon_state_makeenvmap(state);
    {
    }
    return state;
}

void neon_state_destroy(NeonState* state)
{
    neon_writer_destroy(state->stdoutwriter);
    neon_writer_destroy(state->stderrwriter);
    neon_hashtable_destroy(state->globals);
    neon_hashtable_destroy(state->strings);
    state->initstring = NULL;
    neon_vm_gcfreelinkedobjects(state);
    if(state->parser != NULL)
    {
        neon_astparser_destroy(state->parser);
    }
    neon_state_releaseobjvars(state);
    free(state->vmstate.framevalues);
    free(state->vmstate.stackvalues);
    free(state);
}

#include "vm.h"

NeonStatusCode neon_state_runsource(NeonState* state, const char* source, size_t len, bool iseval, NeonValue* evdest)
{
    bool b;
    NeonObjScriptFunction* fn;
    NeonObjClosure* closure;
    fn = neon_astparser_compilesource(state, source, len, iseval);
    if(fn == NULL)
    {
        return NEON_STATUS_SYNTAXERROR;
    }
    //neon_vm_stackpush(state, neon_value_fromobject(fn));
    closure = neon_object_makeclosure(state, fn);
    //neon_vm_stackpop(state);
    neon_vm_stackpush(state, neon_value_fromobject(closure));
    b = neon_vmbits_callprogramclosure(state, closure, iseval);
    if(iseval)
    {
        if(b)
        {
            return NEON_STATUS_OK;
        }
        return NEON_STATUS_RUNTIMEERROR;
    }
    return neon_vm_runvm(state, evdest, false);
}

char* neon_util_readhandle(FILE* hnd, bool withminsize, size_t minsize, size_t* dlen)
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
    if(withminsize)
    {
        if(toldlen > minsize)
        {
            toldlen = minsize;
        }
    }
    buf = (char*)malloc(toldlen + 1);
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

char* neon_util_readfile(const char* filename, bool withminsize, size_t minsize, size_t* dlen)
{
    char* b;
    FILE* fh;
    if((fh = fopen(filename, "rb")) == NULL)
    {
        return NULL;
    }
    b = neon_util_readhandle(fh, withminsize, minsize, dlen);
    fclose(fh);
    return b;
}


bool neon_state_runfile(NeonState* state, const char* path)
{
    size_t fsz;
    char* source;
    NeonValue unused;
    NeonStatusCode result;
    source = neon_util_readfile(path, false, 0, &fsz);
    if(source == NULL)
    {
        fprintf(stderr, "failed to read from '%s'\n", path);
        return false;
    }
    result = neon_state_runsource(state, source, fsz, false, &unused);
    free(source);
    if(result == NEON_STATUS_SYNTAXERROR)
    {
        return false;
    }
    if(result == NEON_STATUS_RUNTIMEERROR)
    {
        return false;
    }
    return true;
}

void repl(NeonState* state)
{
    int cnt;
    char line[1024];
    NeonValue rv;
    cnt = 0;
    for(;;)
    {
        printf("> ");
        if(!fgets(line, sizeof(line), stdin))
        {
            printf("\n");
            break;
        }
        neon_state_runsource(state, line, strlen(line), (cnt > 0), &rv);
        cnt++;
    }
}

static NeonValue objfn_time_func_unixclock(NeonState* state, NeonValue rec, int argc, NeonValue* argv)
{
    (void)state;
    (void)rec;
    (void)argc;
    (void)argv;
    return neon_value_makenumber((double)clock() / CLOCKS_PER_SEC);
}

static NeonValue objfn_number_chr(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    double dw;
    char ch;
    NeonObjString* os;
    (void)state;
    (void)argc;
    (void)argv;
    dw = selfval.as.valnumber;
    ch = (char)dw;
    os = neon_string_copy(state, &ch, 1);
    return neon_value_fromobject(os);
}


static NeonValue objfn_string_staticfunc_fromcharcode(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    char ch;
    NeonObjString* os;
    (void)state;
    (void)argc;
    (void)argv;
    ch = neon_value_asnumber(argv[0]);
    os = neon_string_copy(state, &ch, 1);
    return neon_value_fromobject(os);
}


static NeonValue objfn_string_func_length(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    NeonObjString* os;
    (void)state;
    (void)argc;
    (void)argv;
    os = neon_value_asstring(selfval);
    return neon_value_makenumber(os->sbuf->length);
}

static NeonValue objfn_string_func_ord(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    char ch;
    NeonObjString* selfstr;
    (void)state;
    (void)argc;
    (void)argv;
    selfstr = neon_value_asstring(selfval);
    ch = selfstr->sbuf->data[0];
    return neon_value_makenumber(ch);
}

static NeonValue objfn_string_func_tonumber(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    char* end;
    double dw;
    NeonObjString* selfstr;
    (void)state;
    (void)argc;
    (void)argv;
    selfstr = neon_value_asstring(selfval);
    dw = strtod (selfstr->sbuf->data, &end);
    return neon_value_makenumber(dw);
}

static NeonValue objfn_string_func_indexof(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    int i;
    int findme;
    NeonObjString* findstr;
    NeonObjString* selfstr;
    (void)state;
    (void)argc;
    (void)argv;
    selfstr = neon_value_asstring(selfval);
    findstr = neon_value_asstring(argv[0]);
    findme = findstr->sbuf->data[0];
    for(i=0; i<(int)selfstr->sbuf->length; i++)
    {
        if(selfstr->sbuf->data[i] == findme)
        {
            return neon_value_makenumber(i);
        }
    }
    return neon_value_makenumber(-1);
}

static NeonValue objfn_string_func_charat(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    int idx;
    char ch;
    NeonObjString* os;
    NeonObjString* selfstr;
    (void)state;
    (void)argc;
    (void)argv;
    selfstr = neon_value_asstring(selfval);
    idx = neon_value_asnumber(argv[0]);
    if(idx > (int)selfstr->sbuf->length)
    {
        return neon_value_fromobject(neon_string_copy(state, "", 0));
    }
    ch = selfstr->sbuf->data[idx];
    os = neon_string_copy(state, &ch, 1);
    return neon_value_fromobject(os);
}


static NeonValue objfn_string_func_charcodeat(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    int idx;
    char ch;
    NeonObjString* selfstr;
    (void)state;
    (void)argc;
    (void)argv;
    selfstr = neon_value_asstring(selfval);
    idx = neon_value_asnumber(argv[0]);
    if(idx > (int)selfstr->sbuf->length)
    {
        return neon_value_makenumber(-1);
    }
    ch = selfstr->sbuf->data[idx];
    return neon_value_makenumber(ch);
}

static NeonValue objfn_string_func_substr(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    size_t i;
    size_t len;
    size_t iend;
    size_t ibegin;
    NeonObjString* os;
    NeonObjString* selfstr;
    ibegin = 0;
    selfstr = neon_value_asstring(selfval);
    len = selfstr->sbuf->length;
    iend = len;
    if(argc == 0)
    {
        neon_state_raiseerror(state, "substr() expects at least 1 argument");
        return neon_value_makenil();
    }
    if(!neon_value_isnumber(argv[0]))
    {
        neon_state_raiseerror(state, "first argument expected to be a number");
        return neon_value_makenil();
    }
    ibegin = neon_value_asnumber(argv[0]);
    if(argc > 1)
    {
        if(!neon_value_isnumber(argv[1]))
        {
            neon_state_raiseerror(state, "second argument expected to be a number");
        }
        iend = neon_value_asnumber(argv[1]);
    }
    os = neon_string_copy(state, "", 0);
    
    //for(i=ibegin; i<((len - iend) + 1); i++)
    for(i=ibegin; i<iend; i++)
    {
        dyn_strbuf_appendchar(os->sbuf, selfstr->sbuf->data[i]);
    }
    return neon_value_fromobject(os);
}

static NeonValue objfn_string_func_chars(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    size_t i;
    NeonObjArray* oa;
    NeonObjString* os;
    NeonObjString* selfstr;
    (void)state;
    (void)argc;
    (void)argv;
    selfstr = neon_value_asstring(selfval);
    oa = neon_array_make(state);
    for(i=0; i<selfstr->sbuf->length; i++)
    {
        os = neon_string_copy(state, &selfstr->sbuf->data[i], 1);
        neon_array_push(oa, neon_value_fromobject(os));
    }
    return neon_value_fromobject(oa);
}

static NeonValue objfn_string_func_split(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    NeonValue vsep;
    NeonObjArray* oa;
    NeonObjString* selfstr;
    NeonObjString* sep;
    (void)state;
    (void)argc;
    (void)argv;
    sep = NULL;
    if(argc > 0)
    {
        vsep = argv[0];
        if(!neon_value_isstring(vsep))
        {
            neon_state_raiseerror(state, "first argument must be string");
            return neon_value_makenil();
        }
        sep = neon_value_asstring(vsep);
    }
    if(sep == NULL)
    {
        return objfn_string_func_chars(state, selfval, argc, argv);
    }
    selfstr = neon_value_asstring(selfval);
    oa = neon_array_make(state);
    neon_string_split(state, selfstr->sbuf, sep->sbuf, oa->vala);
    return neon_value_fromobject(oa);
}

static NeonValue objfn_object_func_dump(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    NeonObjString* res;
    NeonWriter* wr;
    (void)argc;
    (void)argv;
    if(argc > 0)
    {
        selfval = argv[0];
    }
    wr = neon_writer_makestring(state);
    neon_writer_printvalue(wr, selfval, true);
    res = neon_string_copy(state, wr->strbuf->data, wr->strbuf->length);
    neon_writer_destroy(wr);
    return neon_value_fromobject(res);
}

/*
struct NeonHashEntry
{
    NeonObjString* key;
    NeonValue value;
};

struct NeonHashTable
{
    int count;
    int capacity;
    NeonState* pvm;
    NeonHashEntry* entries;
};
*/
void fillarrayfromtable(NeonState* state, NeonObjArray* dest, NeonHashTable* from, bool wantkeys, bool wantvalues)
{
    size_t i;
    NeonHashEntry* ent;
    (void)state;
    for(i=0; i<(size_t)from->capacity; i++)
    {
        ent = &from->entries[i];
        if(ent != NULL)
        {
            if(ent->key != NULL)
            {
                if(wantkeys)
                {
                    neon_array_push(dest, neon_value_fromobject(ent->key));
                }
                if(wantvalues)
                {
                    neon_array_push(dest, ent->value);
                }
            }
        }
    }
}

static NeonValue objfn_object_staticfunc_keys(NeonState* state, NeonValue unused, int argc, NeonValue* argv)
{
    NeonValue selfval;
    NeonObjArray* oa;
    NeonObjMap* map;
    (void)unused;
    (void)argc;
    (void)argv;
    selfval = argv[0];
    if(neon_value_ismap(selfval))
    {
        map = neon_value_asmap(selfval);
        oa = neon_array_make(state);
        fillarrayfromtable(state, oa, map->mapping, true, false);
        return neon_value_fromobject(oa);
    }
    return neon_value_makenil();
}


static NeonValue cfn_print(NeonState* state, NeonValue rec, int argc, NeonValue* argv)
{
    int i;
    (void)state;
    (void)rec;
    for(i=0; i<argc; i++)
    {
        neon_writer_printvalue(state->stdoutwriter, argv[i], false);
    }
    return neon_value_makenumber(0);
}

static NeonValue cfn_println(NeonState* state, NeonValue rec, int argc, NeonValue* argv)
{
    cfn_print(state, rec, argc, argv);
    printf("\n");
    return neon_value_makenumber(0);
}

static NeonValue cfn_eval(NeonState* state, NeonValue rec, int argc, NeonValue* argv)
{
    size_t oldcnt;
    NeonStatusCode rc;
    NeonValue val;
    NeonValue rtv;
    NeonObjString* os;
    StringBuffer* copy;
    (void)state;
    (void)rec;
    (void)argc;
    (void)oldcnt;
    oldcnt = state->vmstate.framecount;
    val = argv[0];
    os = neon_value_asstring(val);
    copy = dyn_strbuf_makeempty(0);
    dyn_strbuf_appendstrn(copy, os->sbuf->data, os->sbuf->length);
    rc = neon_state_runsource(state, copy->data, copy->length, true, &rtv);
    dyn_strbuf_destroy(copy);
    if(rc == NEON_STATUS_OK)
    {
        neon_vm_stackpop(state);
    }
    return rtv;
}

static NeonValue objfn_array_func_push(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    int i;
    (void)state;
    NeonObjArray* oa;
    if(!neon_value_isarray(selfval))
    {
        neon_state_raiseerror(state, "first argument must be array");
    }
    else
    {
        oa = neon_value_asarray(selfval);
        for(i=0; i<argc; i++)
        {
            neon_array_push(oa, argv[i]);
        }
    }
    return neon_value_makenumber(0);
}


/*
struct NeonVMStateVars
{
    bool hasraised;
    bool iseval;
    bool havekeeper;
    int framecount;
    int64_t stacktop;
    size_t stackcapacity;
    size_t framecapacity;
    NeonCallFrame keepframe;
    NeonCallFrame* activeframe;
    NeonCallFrame* framevalues;
    NeonValue* stackvalues;
    NeonObjClosure* topclosure;    
};
*/

struct NeonNestCall
{
    /*
    * tracks calls to _init, _call, _restore respectively.
    * attempts to prevent to restore frames to junk values, if _restore is called
    * more than once after _call.
    */
    int restorecnt;
    int frameidx;
    int instrucidx;
    int stacktop;
    NeonState* pvm;
    NeonCallFrame* frame;
};

/* initializes a nested call by copying relevant pre-frame values */
void neon_nestcall_init(NeonState* state, NeonNestCall* nnc)
{
    nnc->pvm = state;
    nnc->restorecnt = 0;
    nnc->stacktop = state->vmstate.stacktop;
    nnc->frameidx = state->vmstate.framecount;
    nnc->frame = state->vmstate.activeframe;
    nnc->instrucidx = state->vmstate.activeframe->instrucidx;
}

/* after neon_nestcall_call(), restores the frame values to pre-frame values */
void neon_nestcall_restore(NeonNestCall* nnc)
{
    NeonState* state;
    if(nnc->restorecnt > 0)
    {
        return;
    }
    nnc->restorecnt++;
    state = nnc->pvm;
    state->vmstate.stacktop = nnc->stacktop;
    state->vmstate.framecount = nnc->frameidx + 0;
    state->vmstate.activeframe = nnc->frame;
    state->vmstate.activeframe->instrucidx = nnc->instrucidx - 0;
}

bool neon_nestcall_call(NeonNestCall* nnc, NeonValue instance, NeonValue callee, int argc, NeonValue* rv)
{
    bool b;
    NeonState* state;
    state = nnc->pvm;
    b = neon_vmbits_callvalue(state, instance, callee, argc);
    if(b)
    {
        /* restore is only needed when neon_vm_runvm() was called */
        nnc->restorecnt--;
        neon_vm_runvm(state, rv, true);
    }
    else
    {
        // TODO: figure out if stack must be popped
        fprintf(stderr, "+++ some error occured in neon_vmbits_callvalue(). cannot continue :(\n");
        return false;
    }
    return true;
}

static NeonValue objfn_array_func_map(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    size_t i;
    size_t cnt;
    bool b;
    NeonValue callee;
    NeonValue val;
    NeonValue nval;
    NeonNestCall nnc;
    NeonObjArray* oa;
    NeonObjArray* na;
    /*
    NeonCheck check;
    neon_check_init(state, &check, selfval, argc, argv);
    if(!neon_check_requireinstance(&check, ))
    */
    if(!neon_value_isarray(selfval))
    {
        neon_state_raiseerror(state, "expected receiver to be array, but got %s", neon_value_typename(selfval));
        return neon_value_makenil();
    }
    if(argc == 0)
    {
        neon_state_raiseerror(state, "need function argument");
        return neon_value_makenil();
    }
    callee = argv[0];
    oa = neon_value_asarray(selfval);
    na = neon_array_make(state);
    cnt = neon_array_count(oa);
    neon_nestcall_init(state, &nnc);
    //fprintf(stderr, "cnt=%d\n", (int)cnt);
    for(i=0; i<cnt; i++)
    {
        val = neon_array_get(oa, i);
        {
            nval = neon_value_makenil();
            neon_vm_stackpush(state, val);
            b = neon_nestcall_call(&nnc, neon_value_makenil(), callee, 1, &nval);
            neon_nestcall_restore(&nnc);
            if(!b)
            {
                fprintf(stderr, "+++ breaking because of errors\n");
                break;
            }
        }
        //neon_vm_debugprintvalue(state, state->stderrwriter, nval, "returned from call: ");
        neon_array_push(na, nval);
    }
    //fprintf(stderr, "+++ returning new array\n");
    return neon_value_fromobject(na);
}

static NeonValue objfn_array_func_count(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    (void)state;
    NeonObjArray* oa;
    (void)argc;
    (void)argv;
    if(!neon_value_isarray(selfval))
    {
        neon_state_raiseerror(state, "Array.count: first argument must be array, but got <%s>", neon_value_typename(selfval));
        return neon_value_makenil();
    }
    else
    {
        oa = neon_value_asarray(selfval);
        return neon_value_makenumber(oa->vala->size);
    }
    return neon_value_makenumber(0);
}

static NeonValue objfn_array_func_pop(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    (void)state;
    NeonValue val;
    NeonObjArray* oa;
    (void)argc;
    (void)argv;
    if(!neon_value_isarray(selfval))
    {
        neon_state_raiseerror(state, "Array.pop: first argument must be array, but got <%s>", neon_value_typename(selfval));
        return neon_value_makenil();
    }
    else
    {
        oa = neon_value_asarray(selfval);
        val = neon_valarray_pop(oa->vala);
        return val;
    }
    return neon_value_makenumber(0);
}

static NeonValue objfn_array_func_set(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    long idx;
    NeonValue putval;
    NeonValue idxval;
    NeonObjArray* oa;
    (void)state;
    (void)argc;
    idxval = argv[0];
    putval = argv[1];
    if(!neon_value_isarray(selfval))
    {
        neon_state_raiseerror(state, "first argument must be array");
        return neon_value_makenil();
    }
    else
    {
        if(!neon_value_isnumber(idxval))
        {
            neon_state_raiseerror(state, "second argument must be number");
            return neon_value_makenil();
        }
        oa = neon_value_asarray(selfval);
        idx = neon_value_asnumber(idxval);
        neon_valarray_insert(oa->vala, idx, putval);
    }
    return neon_value_makenumber(0);
}


static NeonValue objfn_array_func_erase(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    long idx;
    NeonValue idxval;
    NeonObjArray* oa;
    (void)state;
    (void)argc;
    idxval = argv[0];
    if(!neon_value_isarray(selfval))
    {
        neon_state_raiseerror(state, "first argument must be array");
        return neon_value_makenil();
    }
    else
    {
        if(!neon_value_isnumber(idxval))
        {
            neon_state_raiseerror(state, "second argument must be number");
            return neon_value_makenil();
        }
        oa = neon_value_asarray(selfval);
        idx = neon_value_asnumber(idxval);
        return neon_value_makebool(neon_valarray_erase(oa->vala, idx));
    }
    return neon_value_makebool(false);
}


static NeonValue objfn_array_func_join(NeonState* state, NeonValue selfval, int argc, NeonValue* argv)
{
    size_t i;
    size_t cnt;
    bool hasjoinee;
    NeonValue val;
    NeonValue vjoinee;
    NeonWriter* wr;
    NeonObjArray* oa;
    NeonObjString* os;
    hasjoinee = false;
    if(!neon_value_isarray(selfval))
    {
        neon_state_raiseerror(state, "first argument must be array");
        return neon_value_makenil();
    }
    if(argc > 0)
    {
        vjoinee = argv[0];
        if(!neon_value_isnil(vjoinee))
        {
            hasjoinee = true;
        }
    }
    oa = neon_value_asarray(selfval);
    cnt = neon_array_count(oa);
    wr = neon_writer_makestring(state);
    for(i=0; i<cnt; i++)
    {
        val = neon_array_get(oa, i);
        neon_writer_printvalue(wr, val, false);
        if(((i+1) != cnt) && hasjoinee)
        {
            neon_writer_printvalue(wr, vjoinee, false);
        }
    }
    os = neon_writer_takestring(wr);
    neon_writer_destroy(wr);
    return neon_value_fromobject(os);
}

static NeonValue objfn_map_func_make(NeonState* state, NeonValue rec, int argc, NeonValue* argv)
{
    NeonObjMap* om;
    (void)rec;
    (void)argc;
    (void)argv;
    om = neon_object_makemap(state);
    return neon_value_fromobject(om);
}

static NeonValue objfn_map_func_size(NeonState* state, NeonValue rec, int argc, NeonValue* argv)
{
    size_t sz;
    NeonObjMap* om;
    (void)state;
    (void)rec;
    (void)argc;
    (void)argv;
    om = neon_value_asmap(argv[0]);
    sz = om->mapping->count;
    return neon_value_makenumber(sz);
}

static NeonValue objfn_file_func_readfile(NeonState* state, NeonValue rec, int argc, NeonValue* argv)
{
    bool withmin;
    size_t flen;
    size_t minsz;
    char* buf;
    const char* fname;
    NeonValue mval;
    NeonObjString* resos;
    NeonObjString* osname;
    (void)rec;
    withmin = false;
    minsz = 0;
    if(argc > 0)
    {
        if(argc > 1)
        {
            mval = argv[1];
            if(!neon_value_isnumber(mval))
            {
                neon_state_raiseerror(state, "optional second argument must be number");
            }
            minsz = neon_value_asnumber(mval);
            withmin = true;
        }
        osname = neon_value_asstring(argv[0]);
        fname = osname->sbuf->data;
        buf = neon_util_readfile(fname, withmin, minsz, &flen);
        if(buf == NULL)
        {
            neon_state_raiseerror(state, "failed to read from '%s'", fname);
            return neon_value_makenil();
        }
        else
        {
            resos = neon_string_take(state, buf, flen);
            return neon_value_fromobject(resos);
        }
    }
    neon_state_raiseerror(state, "expected string as filename");
    return neon_value_makenil();
}

static void fileopen_close(NeonState* state, NeonObjUserdata* ud)
{
    FILE* fh;
    (void)state;
    fh = (FILE*)ud->userptr;
    if(fh != NULL)
    {
        fprintf(stderr, "closing file\n");
        fclose(fh);
    }
}

bool neon_udfile_seek(NeonState* state, FILE* hnd, long offset, int whence)
{
    if(fseek(hnd, offset, whence) == -1)
    {
        neon_state_raiseerror(state, "cannot seek from offset %d to %d", offset, whence);
        return false;
    }
    return true;
}

bool neon_udfile_tell(NeonState* state, FILE* hnd, size_t* dest)
{
    long rt;
    rt = ftell(hnd);
    if(rt == -1)
    {
        neon_state_raiseerror(state, "ftell() failed");
        return false;
    }
    *dest = rt;
    return true;

}

static char* neon_udfile_read(NeonState* state, FILE* hnd, size_t howmuch, size_t* actual)
{
    char* buf;
    buf = (char*)malloc(howmuch+1);
    if(buf == NULL)
    {
        neon_state_raiseerror(state, "failed to allocate %d bytes", howmuch+1);
        return NULL;
    }
    *actual = fread(buf, sizeof(char), howmuch, hnd);
    if(*actual == 0)
    {
        free(buf);
        return NULL;
    }
    return buf;
}

static NeonValue objfn_udfile_read(NeonState* state, NeonValue rec, int argc, NeonValue* argv)
{
    bool readall;
    size_t rawtold;
    size_t actuallen;
    size_t howmuch;
    char* buf;
    FILE* hnd;
    NeonObjString* os;
    NeonObjUserdata* selfud;
    readall = true;
    howmuch = 0;
    selfud = neon_value_asuserdata(rec);
    hnd = (FILE*)selfud->userptr;
    if(argc > 0)
    {
        readall = false;
        if(!neon_value_isnumber(argv[0]))
        {
            neon_state_raiseerror(state, "first argument must be number");
        }
        howmuch = neon_value_asnumber(argv[0]);
    }
    if(readall)
    {
        if(!neon_udfile_seek(state, hnd, 0, SEEK_END))
        {
            return neon_value_makenil();
        }
        if(!neon_udfile_tell(state, hnd, &rawtold))
        {
            return neon_value_makenil();
        }
        howmuch = rawtold;
        if(!neon_udfile_seek(state, hnd, 0, SEEK_SET))
        {
            return neon_value_makenil();
        }
    }
    buf = neon_udfile_read(state, hnd, howmuch, &actuallen);
    if(!buf)
    {
        return neon_value_makenil();
    }
    os = neon_string_take(state, buf, actuallen);
    return neon_value_fromobject(os);
}

static NeonValue objfn_udfile_close(NeonState* state, NeonValue rec, int argc, NeonValue* argv)
{
    FILE* hnd;
    NeonObjUserdata* selfud;
    (void)state;
    (void)argc;
    (void)argv;
    selfud = neon_value_asuserdata(rec);
    hnd = (FILE*)selfud->userptr;
    fprintf(stderr, "in udfile_close\n");
    fclose(hnd);
    selfud->userptr = NULL;
    return neon_value_makenil();
}

static NeonValue objfn_file_func_open(NeonState* state, NeonValue rec, int argc, NeonValue* argv)
{
    FILE* fh;
    NeonValue va;
    NeonObjUserdata* ud;
    NeonObjString* path;
    NeonObjString* mode;
    (void)rec;
    if(argc > 1)
    {
        va = argv[0];
        if(!neon_value_isstring(va))
        {
            neon_state_raiseerror(state, "first argument must be string");
            return neon_value_makenil();
        }
        path = neon_value_asstring(va);
        va = argv[1];
        if(!neon_value_isstring(va))
        {
            neon_state_raiseerror(state, "second argument must be string");
            return neon_value_makenil();
        }
        mode = neon_value_asstring(va);
    }
    else
    {
        neon_state_raiseerror(state, "expected string|string");
        return neon_value_makenil();
    }
    fh = fopen(path->sbuf->data, mode->sbuf->data);
    if(fh == NULL)
    {
        neon_state_raiseerror(state, "cannot open '%s': %s", path->sbuf->data, strerror(errno));
        return neon_value_makenil();
    }
    ud = neon_object_makeuserdata(state, (void*)fh, fileopen_close);
    
    neon_userdata_bindmethod(ud, "close", objfn_udfile_close);
    neon_userdata_bindmethod(ud, "read", objfn_udfile_read);

    return neon_value_fromobject(ud);
}

static NeonValue objfn_os_func_getenv(NeonState* state, NeonValue rec, int argc, NeonValue* argv)
{
    NeonObjString* osval;
    NeonObjString* varname;
    const char* rawkey;
    const char* rawval;
    (void)rec;
    varname = NULL;
    if(argc > 0)
    {
        if(!neon_value_isstring(argv[0]))
        {
            neon_state_raiseerror(state, "expected string");
            return neon_value_makenil();
        }
        varname = neon_value_asstring(argv[0]);
    }
    if(varname != NULL)
    {
        rawkey = varname->sbuf->data;
        rawval = getenv(rawkey);
        if(rawval == NULL)
        {
            return neon_value_makenil();
        }
        osval = neon_string_copy(state, rawval, strlen(rawval));
        return neon_value_fromobject(osval);
    }
    return neon_value_fromobject(state->envvars);
}

static NeonValue objfn_os_func_setenv(NeonState* state, NeonValue rec, int argc, NeonValue* argv)
{
    int rc;
    NeonObjString* osval;
    NeonObjString* oskey;
    (void)rec;
    if(argc < 2)
    {
        neon_state_raiseerror(state, "expected string|string");
        return neon_value_makebool(false);
    }
    if(!neon_value_isstring(argv[0]))
    {
        neon_state_raiseerror(state, "key must be string");
        return neon_value_makenil();
    }
    if(!neon_value_isstring(argv[1]))
    {
        neon_state_raiseerror(state, "value must be string");
        return neon_value_makenil();
    }
    oskey = neon_value_asstring(argv[0]);
    osval = neon_value_asstring(argv[1]);
    rc = setenv(oskey->sbuf->data, osval->sbuf->data, true);
    if(rc == -1)
    {
        return neon_value_makebool(false);
    }
    /* update $envvars to reflect the change*/
    neon_map_set(state->envvars, oskey, neon_value_fromobject(osval));
    return neon_value_makebool(true);
}

int main(int argc, char** argv)
{
    int i;
    int nargc;
    int exitcode;
    char oc;
    char noc;
    char** nargv;
    char* codeline;
    const char* filename;
    NeonValue unused;
    NeonObjMap* map;
    NeonObjClass* klass;
    NeonState* state;
    exitcode = 0;
    nargv = argv;
    nargc = argc;
    codeline = NULL;
    state = neon_state_make();
    
    neon_state_defnative(state, "print", cfn_print);
    neon_state_defnative(state, "println", cfn_println);
    neon_state_defnative(state, "eval", cfn_eval);
    {
        klass = state->objvars.classprimobject;
        neon_class_setfunctioncstr(klass, "dump", objfn_object_func_dump);
        neon_class_setstaticfunctioncstr(klass, "keys", objfn_object_staticfunc_keys);
    }
    {
        klass = state->objvars.classprimnumber;
        neon_class_setfunctioncstr(klass, "chr", objfn_number_chr);
    }
    {
        klass = state->objvars.classprimstring;
        neon_class_setstaticfunctioncstr(klass, "fromCharCode", objfn_string_staticfunc_fromcharcode);        
        neon_class_setfunctioncstr(klass, "length", objfn_string_func_length);
        neon_class_setfunctioncstr(klass, "size", objfn_string_func_length);
        neon_class_setfunctioncstr(klass, "split", objfn_string_func_split);
        neon_class_setfunctioncstr(klass, "chars", objfn_string_func_chars);
        neon_class_setfunctioncstr(klass, "ord", objfn_string_func_ord);
        neon_class_setfunctioncstr(klass, "toNumber", objfn_string_func_tonumber);
        neon_class_setfunctioncstr(klass, "substr", objfn_string_func_substr);
        neon_class_setfunctioncstr(klass, "indexOf", objfn_string_func_indexof);
        neon_class_setfunctioncstr(klass, "charAt", objfn_string_func_charat);
        neon_class_setfunctioncstr(klass, "charCodeAt", objfn_string_func_charcodeat);
        #if 0
        neon_class_setfunctioncstr(klass, "lstrip", objfn_string_func_leftstrip);
        neon_class_setfunctioncstr(klass, "rstrip", objfn_string_func_rightstrip);
        neon_class_setfunctioncstr(klass, "strip", objfn_string_func_strip);
        neon_class_setfunctioncstr(klass, "toUpper", objfn_string_func_toupper);
        neon_class_setfunctioncstr(klass, "tolower", objfn_string_func_tolower);
        #endif
    }
    {
        klass = state->objvars.classprimarray;
        neon_class_setfunctioncstr(klass, "join", objfn_array_func_join);
        neon_class_setfunctioncstr(klass, "map", objfn_array_func_map);
        neon_class_setfunctioncstr(klass, "count", objfn_array_func_count);
        neon_class_setfunctioncstr(klass, "length", objfn_array_func_count);
        neon_class_setfunctioncstr(klass, "size", objfn_array_func_count);
        neon_class_setfunctioncstr(klass, "push", objfn_array_func_push);
        neon_class_setfunctioncstr(klass, "append", objfn_array_func_push);
        neon_class_setfunctioncstr(klass, "erase", objfn_array_func_erase);
        neon_class_setfunctioncstr(klass, "set", objfn_array_func_set);
        neon_class_setfunctioncstr(klass, "pop", objfn_array_func_pop);
    }
    {
        map = neon_object_makemap(state);
        neon_map_setstrfunction(map, "unixclock", objfn_time_func_unixclock);
        neon_state_defvalue(state, "Time", neon_value_fromobject(map));        
    }
    {
        map = neon_object_makemap(state);
        neon_map_setstrfunction(map, "make", objfn_map_func_make);
        neon_map_setstrfunction(map, "size", objfn_map_func_size);
        neon_state_defvalue(state, "Map", neon_value_fromobject(map));
    }
    {
        map = neon_object_makemap(state);
        neon_map_setstrfunction(map, "getenv", objfn_os_func_getenv);
        neon_map_setstrfunction(map, "setenv", objfn_os_func_setenv);
        neon_state_defvalue(state, "OS", neon_value_fromobject(map));
        
    }
    {
        map = neon_object_makemap(state);
        neon_map_setstrfunction(map, "readfile", objfn_file_func_readfile);
        neon_map_setstrfunction(map, "open", objfn_file_func_open);
        #if 0
        neon_map_setstrfunction(map, "exists", objfn_file_func_exists);
        neon_map_setstrfunction(map, "isFile", objfn_file_func_isfile);
        neon_map_setstrfunction(map, "isDirectory", objfn_file_func_isdirectory);
        #endif
        neon_state_defvalue(state, "File", neon_value_fromobject(map));
    }
    for(i=1; i<argc; i++)
    {
        if(argv[i][0] == '-')
        {
            oc = argv[i][1];
            noc = argv[i][2];
            if(oc == 'e')
            {
                if(noc == 0)
                {
                    if((i+1) != argc)
                    {
                        codeline = argv[i+1];
                        nargc--;
                        nargv++;
                    }
                    else
                    {
                        fprintf(stderr, "-e needs a value\n");
                        return 1;
                    }
                }
                else
                {
                    codeline = (argv[i] + 2);
                }
                nargc--;
                nargv++;
            }
            else if(oc == 'd')
            {
                state->conf.shouldprintruntime = true;
                nargc--;
                nargv++;
            }
            else if(oc == 's')
            {
                state->conf.strictmode = true;
                nargc--;
                nargv++;
            }
            else
            {
                fprintf(stderr, "invalid flag '-%c'\n", oc);
                return 1;
            }
        }
    }
    if(codeline != NULL)
    {
        //fprintf(stderr, "codeline=%s\n", codeline);
        neon_state_runsource(state, codeline, strlen(codeline), false, &unused);
    }
    else
    {
        if(nargc > 1)
        {
            filename = nargv[1];
            //fprintf(stderr, "filename=%s\n", filename);
            if(!neon_state_runfile(state, filename))
            {
                exitcode = 1;
            }
        }
        else
        {
            repl(state);
        }
    }
    neon_state_destroy(state);
    return exitcode;
}
