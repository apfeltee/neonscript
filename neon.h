
#pragma once

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
#if defined(__linux__) || defined(__unix__)
    #include <unistd.h>
    #define NEON_CONF_ISLINUX
#endif
#include "strbuf.h"

/*
// In the book, we show them defined, but for working on them locally,
// we don't want them to be.
*/
#define DEBUG_PRINT_CODE 0
#undef DEBUG_STRESS_GC
#undef DEBUG_LOG_GC

#if !defined(NEON_CONF_USEPROTINC)
    #define NEON_CONF_USEPROTINC 1
#endif

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

#if !defined(NEON_CONF_ISLINUX)
    #define NEON_CONF_FORCEDISABLECOLOR
#endif

/*
// OP_FUNCARG - explicitly for function arguments (and default values)?
*/
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
    NEON_OP_CALLCALLABLE,
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
    NEON_VAL_NIL,
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
    NEON_TOK_ASSIGNSHIFTRIGHT,
    NEON_TOK_ASSIGNSHIFTLEFT,
    NEON_TOK_ASSIGNBINOR,
    NEON_TOK_ASSIGNBINXOR,
    NEON_TOK_ASSIGNBINAND,
    NEON_TOK_ASSIGNTILDE,

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


enum NeonColor
{
    NEON_COLOR_RESET,
    NEON_COLOR_RED,
    NEON_COLOR_GREEN,    
    NEON_COLOR_YELLOW,
    NEON_COLOR_BLUE,
};

typedef enum NeonColor NeonColor;
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
typedef struct /**/NeonFileHandle NeonFileHandle;


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
    } as;
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
    bool fromstack;
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

struct NeonFileHandle
{
    bool isopen;
    FILE* handle;
    NeonObjString* path;
    NeonObjString* mode;
};

struct NeonObjUserdata
{
    NeonObject objectheader;
    void* userptr;
    NeonUserFinalizeFN finalfn;
    NeonHashTable* methods;
};

struct NeonObjArray
{
    NeonObject obj;
    NeonValArray* vala;
};

struct NeonObjMap
{
    NeonObject obj;
    bool shouldmark;
    NeonHashTable* mapping;
};

struct NeonObjScriptFunction
{
    NeonObject obj;
    bool isvariadic;
    int arity;
    int upvaluecount;
    NeonBinaryBlob* blob;
    NeonObjString* name;
};

struct NeonObjNativeFunction
{
    NeonObject obj;
    const char* name;
    NeonNativeFN natfunc;
};

struct NeonObjString
{
    NeonObject objectheader;
    uint32_t hash;
    /*
    // actual string handling is handled by StringBuffer.
    // this is to avoid to trashing the stack with temporary values
    */
    StringBuffer* sbuf;
};

struct NeonObjUpvalue
{
    NeonObject objectheader;
    NeonValue location;
    NeonValue closed;
    NeonObjUpvalue* next;
    int32_t upindex;
};

struct NeonObjClosure
{
    NeonObject objectheader;
    NeonObjScriptFunction* fnptr;
    NeonObjUpvalue** upvalues;
    int upvaluecount;
};

struct NeonObjClass
{
    NeonObject objectheader;
    NeonObjString* name;
    NeonHashTable* methods;
    NeonHashTable* staticmethods;
    NeonHashTable* properties;
    NeonObjClass* parent;
};

struct NeonObjInstance
{
    NeonObject objectheader;
    NeonObjClass* klass;
    NeonHashTable* fields;
};

struct NeonObjBoundFunction
{
    NeonObject objectheader;
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
    int end;
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

    /* if true, all variables must be explicitly declared with 'var' */
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
    /* is the GC currently allowed to collect? not currently used. */
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
    /* whether an error has been raised; meant to avoid re-raising the same error in an infinite loop */
    bool hasraised;

    /* whether this VM state was initiated by eval(), or something like it */
    bool iseval;

    /* used by NEON_OP_RESTOREFRAME */
    bool havekeeper;

    /* number of frames */
    int framecount;

    /* index to the topmost $stackvalues entry, excluding the final NUL byte. */
    int64_t stacktop;

    /* how big is stackvalues, before resizing is necessary? */
    size_t stackcapacity;

    /* how many frames are available, before resizing is necessary? */
    size_t framecapacity;

    /* used by NEON_OP_RESTOREFRAME */
    NeonCallFrame keepframe;

    /* the currently active frame, which is $framevalues[$framecount - 1] */
    NeonCallFrame* activeframe;

    /* where frames lie on the stack. dynamically allocated; avoid pointer-arithmetics. */
    NeonCallFrame* framevalues;

    /* where values lie on the stack. dynamically allocated; avoid pointer-arithmetics. */
    NeonValue* stackvalues;

    /* the closure compiled from the input source, whether file, or string. */
    NeonObjClosure* topclosure;
};

struct NeonState
{
    NeonConfig conf;

    /* contains the system environment variables */
    NeonObjMap* envvars;

    /* instance of the parser. heap-allocated! */
    NeonAstParser* parser;

    /* state of the VM */
    NeonVMStateVars vmstate;

    /* state of the garbage collector, and related information */
    NeonVMGCVars gcstate;

    /* contains class objects of primitive classes (Object, String, Number...) */
    NeonVMObjVars objvars;

    /*
    * global names are stored here.
    * additionally, any variables declared outside of a block-scope are also stored here.
    */
    NeonHashTable* globals;

    /* llookup table for strings, to avoid redundant string copies */
    NeonHashTable* strings;

    /* class constructor name */
    NeonObjString* initstring;

    /* currently active upvalues */
    NeonObjUpvalue* openupvalues;

    /* NeonWriter instances that point to standard output/error. */
    NeonWriter* stdoutwriter;
    NeonWriter* stderrwriter;

};

/*
// wrapper that merely fixes warnings about NeonObj<name>* objects not being NeonObject*,
// despite being down-castable. 
*/
#define neon_value_fromobject(obj) neon_value_fromobject_actual((NeonObject*)(obj))

#if (NEON_CONF_USEPROTINC == 1)
    #include "prot.inc"
#endif


