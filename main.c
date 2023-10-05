

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>
#include "cvector.h"

#define UINT8_COUNT (UINT8_MAX + 1)

// In the book, we show them defined, but for working on them locally,
// we don't want them to be.
#define DEBUG_PRINT_CODE 0
#undef DEBUG_STRESS_GC
#undef DEBUG_LOG_GC


#define FRAMES_MAX 1024
#define STACK_MAX (FRAMES_MAX * 2)

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity)*2)

#define GC_HEAP_GROW_FACTOR 2

#define TABLE_MAX_LOAD 0.75

// NEON_OP_FUNCARG - explicitly for function arguments (and default values)?

enum NeonOpCode
{
    NEON_OP_PUSHCONST,
    NEON_OP_PUSHNIL,
    NEON_OP_PUSHTRUE,
    NEON_OP_PUSHFALSE,
    NEON_OP_PUSHONE,
    NEON_OP_POP,
    NEON_OP_POPN,
    NEON_OP_DUP,
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
    NEON_OP_PRIMNOT,
    NEON_OP_PRIMNEGATE,
    NEON_OP_DEBUGPRINT,
    NEON_OP_JUMPNOW,
    NEON_OP_JUMPIFFALSE,
    NEON_OP_LOOP,
    NEON_OP_CALL,
    NEON_OP_INSTTHISINVOKE,
    NEON_OP_INSTSUPERINVOKE,
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
    NEON_OP_HALTVM,
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
};

enum NeonStatusCode
{
    NEON_STATUS_OK,
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
    NEON_TOK_SLASH,
    NEON_TOK_STAR,
    NEON_TOK_MODULO,
    NEON_TOK_BINAND,
    NEON_TOK_BINOR,
    NEON_TOK_BINXOR,
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
    // Literals.
    NEON_TOK_IDENTIFIER,
    NEON_TOK_STRING,
    NEON_TOK_NUMBER,
    // Keywords.
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
typedef struct /**/NeonChunk NeonChunk;
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
typedef struct /**/NeonStringBuffer NeonStringBuffer;
typedef struct /**/NeonWriter NeonWriter;
typedef struct /**/NeonVMVars NeonVMVars;
typedef struct /**/NeonGCVars NeonGCVars;
typedef struct /**/NeonConfig NeonConfig;

typedef void (*NeonAstParseFN)(NeonAstParser*, bool);
typedef NeonValue (*NeonNativeFN)(NeonState*, int, NeonValue*);
typedef bool (*NeonValDestroyFN)(NeonState*, void*, NeonValue);

struct NeonWriter
{
    bool isstring;
    bool shouldclose;
    NeonState* pvm;
    NeonStringBuffer* strbuf;
    FILE* handle;
};


/* Chunks of Bytecode value-h < Types of Values value
typedef double NeonValue;
*/
struct NeonValue
{
    NeonValType type;

    union
    {
        bool boolean;
        double number;
        NeonObject* obj;
    } as;// [as]
};

struct NeonObject
{
    bool ismarked;
    NeonObjType type;
    NeonObject* next;
};


struct NeonValArray
{
    NeonState* pvm;

    size_t size;
    size_t capacity;
    NeonValue* values;
};


struct NeonChunk
{
    int count;
    int capacity;
    int32_t* code;
    int* lines;
    NeonValArray constants;
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
    NeonHashEntry* entries;
};

struct NeonObjArray
{
    NeonObject obj;
    NeonState* pvm;
    NeonValArray vala;
};

struct NeonObjMap
{
    NeonObject obj;
    NeonState* pvm;
    NeonHashTable mapping;
};

struct NeonObjScriptFunction
{
    NeonObject obj;
    int arity;
    int upvaluecount;
    NeonChunk chunk;
    NeonObjString* name;
};

struct NeonObjNativeFunction
{
    NeonObject obj;
    NeonNativeFN natfunc;
};

struct NeonStringBuffer
{
    NeonState* pvm;
    int length;
    size_t capacity;
    char* data;
};

struct NeonObjString
{
    NeonObject obj;
    uint32_t hash;
    // actual string handling is handled by NeonStringBuffer.
    // this is to avoid to trashing the stack with temporary values
    NeonStringBuffer* sbuf;
};

struct NeonObjUpvalue
{
    NeonObject obj;
    NeonValue* location;
    NeonValue closed;
    NeonObjUpvalue* next;
};

struct NeonObjClosure
{
    NeonObject obj;
    NeonObjScriptFunction* innerfn;
    NeonObjUpvalue** upvalues;
    int upvaluecount;
};

struct NeonObjClass
{
    NeonObject obj;
    NeonObjString* name;
    NeonHashTable methods;
};

struct NeonObjInstance
{
    NeonObject obj;
    NeonObjClass* klass;
    NeonHashTable fields;// [fields]
};

struct NeonObjBoundFunction
{
    NeonObject obj;
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
    NeonAstParseFN prefix;
    NeonAstParseFN infix;
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
    NeonObjScriptFunction* compiledfn;
    NeonAstLoop* loop;
    NeonAstUpvalue upvalues[UINT8_COUNT];
    NeonAstLocal locals[UINT8_COUNT];
};

struct NeonAstClassCompiler
{
    bool hassuperclass;
    NeonAstClassCompiler* enclosing;
};

struct NeonAstScanner
{
    int line;
    const char* start;
    const char* current;
    NeonState* pvm;
};

struct NeonAstParser
{
    bool haderror;
    bool panicmode;
    NeonAstToken current;
    NeonAstToken previous;
    NeonState* pvm;
    NeonAstScanner* pscn;
    NeonAstCompiler* currcompiler;
    NeonAstClassCompiler* currclass;
};

struct NeonCallFrame
{
    int32_t* ip;
    int64_t frstackindex;
    NeonObjClosure* closure;
};

struct NeonVMVars
{
    bool hasraised;
    int framecount;
    int64_t stacktop;
    NeonCallFrame* currframe;
    NeonCallFrame framevalues[FRAMES_MAX];
    NeonValue stackvalues[STACK_MAX];
};

struct NeonGCVars
{
    int graycount;
    int graycap;
    size_t bytesallocd;
    size_t nextgc;
    NeonObject* linkedobjects;
    NeonObject** graystack;
};

struct NeonConfig
{
    bool shouldprintruntime;
};

struct NeonState
{
    NeonConfig conf;
    NeonAstParser* parser;
    NeonVMVars vmvars;
    NeonGCVars gcstate;
    NeonHashTable globals;
    NeonHashTable strings;
    NeonObjString* initstring;
    NeonObjUpvalue* openupvalues;
    NeonWriter* stdoutwriter;
    NeonWriter* stderrwriter;
};

#include "prot.inc"

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

static inline NeonValue neon_value_makebool(bool b)
{
    NeonValue nv;
    nv = neon_value_makevalue(NEON_VAL_BOOL);
    nv.as.boolean = b;
    return nv;
}

static inline NeonValue neon_value_makenumber(double dw)
{
    NeonValue nv;
    nv = neon_value_makevalue(NEON_VAL_NUMBER);
    nv.as.number = dw;
    return nv;
}

#define neon_value_makeobject(obj) neon_value_makeobject_actual((NeonObject*)(obj))

static inline NeonValue neon_value_makeobject_actual(NeonObject* o)
{
    NeonValue nv;
    nv = neon_value_makevalue(NEON_VAL_OBJ);
    nv.as.obj = o;
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
    return v.as.obj;
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
    return (v.as.boolean);
}

static inline double neon_value_asnumber(NeonValue v)
{
    return (v.as.number);
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

bool neon_value_isfalsey(NeonValue value)
{
    return (
        neon_value_isnil(value) || (
            neon_value_isbool(value) &&
            !neon_value_asbool(value)
        )
    );
}

void* neon_mem_allocate(NeonState* state, size_t tsz, size_t count)
{
    return neon_mem_wrapalloc(state, NULL, 0, tsz * count);
}

void* neon_mem_growarray(NeonState* state, size_t tsz, void* pointer, size_t oldcnt, size_t newcnt)
{
    return neon_mem_wrapalloc(state, pointer, (tsz * oldcnt), (tsz * newcnt));
}
 
void neon_mem_freearray(NeonState* state, size_t tsz, void* pointer, size_t oldcnt)
{
    neon_mem_wrapalloc(state, pointer, (tsz * oldcnt), 0);
}

void neon_mem_release(NeonState* state, size_t tsz, void* pointer)
{
    neon_mem_wrapalloc(state, pointer, tsz, 0);
}

void* neon_mem_wrapalloc(NeonState* state, void* pointer, size_t oldsz, size_t newsz)
{
    state->gcstate.bytesallocd += newsz - oldsz;
    if(newsz > oldsz)
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

    void* result = realloc(pointer, newsz);
    if(result == NULL)
        exit(1);
    return result;
}

void neon_gcmem_markroots(NeonState* state)
{
    int i;
    NeonValue* pslot;
    for(pslot=&state->vmvars.stackvalues[0]; pslot < &state->vmvars.stackvalues[state->vmvars.stacktop]; pslot++)
    {
        neon_gcmem_markvalue(state, *pslot);
    }
    for(i = 0; i < state->vmvars.framecount; i++)
    {
        neon_gcmem_markobject(state, (NeonObject*)state->vmvars.framevalues[i].closure);
    }
    for(NeonObjUpvalue* upvalue = state->openupvalues; upvalue != NULL; upvalue = upvalue->next)
    {
        neon_gcmem_markobject(state, (NeonObject*)upvalue);
    }
    neon_gcmem_markhashtable(state, &state->globals);
    neon_prs_markcompilerroots(state);
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
            neon_object_release(state, unreached);
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
    neon_hashtable_remwhite(state, &state->strings);
    neon_gcmem_sweep(state);
    state->gcstate.nextgc = state->gcstate.bytesallocd * GC_HEAP_GROW_FACTOR;
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
        neon_object_release(state, obj);
        obj = next;
    }
    free(state->gcstate.graystack);
}

void neon_gcmem_markhashtable(NeonState* state, NeonHashTable* table)
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
#ifdef DEBUG_LOG_GC
    neon_writer_writeformat(state->stderrwriter, "markobject: at %p: ", (void*)object);
    neon_writer_printvalue(state->stderrwriter, neon_value_makeobject(object), true);
    neon_writer_writestring(state->stderrwriter, "\n");
#endif
    object->ismarked = true;
    if(state->gcstate.graycap < (state->gcstate.graycount + 1))
    {
        state->gcstate.graycap = GROW_CAPACITY(state->gcstate.graycap);
        needsz = sizeof(NeonObject*) * state->gcstate.graycap;
        state->gcstate.graystack = (NeonObject**)realloc(state->gcstate.graystack, needsz);
        if(state->gcstate.graystack == NULL)
        {
            exit(1);
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
    neon_writer_writeformat(state->stderrwriter, "blackenobj: at %p: ", (void*)object);
    neon_writer_printvalue(state->stderrwriter, neon_value_makeobject(object), true);
    neon_writer_writestring(state->stderrwriter, "\n");
#endif

    switch(object->type)
    {
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
                neon_gcmem_markhashtable(state, &klass->methods);
            }
            break;

        case NEON_OBJ_CLOSURE:
            {
                int i;
                NeonObjClosure* closure;
                closure = (NeonObjClosure*)object;
                neon_gcmem_markobject(state, (NeonObject*)closure->innerfn);
                for(i = 0; i < closure->upvaluecount; i++)
                {
                    neon_gcmem_markobject(state, (NeonObject*)closure->upvalues[i]);
                }
            }
            break;
        case NEON_OBJ_FUNCTION:
            {
                NeonObjScriptFunction* ofn;
                ofn = (NeonObjScriptFunction*)object;
                neon_gcmem_markobject(state, (NeonObject*)ofn->name);
                neon_gcmem_markvalarray(state, &ofn->chunk.constants);
            }
            break;
        case NEON_OBJ_INSTANCE:
            {
                NeonObjInstance* instance;
                instance = (NeonObjInstance*)object;
                neon_gcmem_markobject(state, (NeonObject*)instance->klass);
                neon_gcmem_markhashtable(state, &instance->fields);
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
                neon_gcmem_markvalarray(state, &oa->vala);
            }
            break;
        case NEON_OBJ_MAP:
            {
                NeonObjMap* om;
                om = (NeonObjMap*)object;
                neon_gcmem_markhashtable(state, &om->mapping);
            }
            break;
        case NEON_OBJ_NATIVE:
        case NEON_OBJ_STRING:
            break;
    }
}


void neon_object_release(NeonState* state, NeonObject* object)
{
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif

    switch(object->type)
    {
        case NEON_OBJ_BOUNDMETHOD:
            {
                neon_mem_release(state, sizeof(NeonObjBoundFunction), object);
            }
            break;
        case NEON_OBJ_CLASS:
            {
                NeonObjClass* klass;
                klass = (NeonObjClass*)object;
                neon_hashtable_free(state, &klass->methods);
                neon_mem_release(state, sizeof(NeonObjClass), object);
            }
            break;
        case NEON_OBJ_CLOSURE:
            {
                NeonObjClosure* closure;
                closure = (NeonObjClosure*)object;
                neon_mem_freearray(state, sizeof(NeonObjUpvalue*), closure->upvalues, closure->upvaluecount);
                neon_mem_release(state, sizeof(NeonObjClosure), object);
            }
            break;

        case NEON_OBJ_FUNCTION:
        {
            NeonObjScriptFunction* ofn;
            ofn = (NeonObjScriptFunction*)object;
            neon_chunk_free(state, &ofn->chunk);
            neon_mem_release(state, sizeof(NeonObjScriptFunction), object);
            break;
        }
        case NEON_OBJ_INSTANCE:
        {
            NeonObjInstance* instance;
            instance = (NeonObjInstance*)object;
            neon_hashtable_free(state, &instance->fields);
            neon_mem_release(state, sizeof(NeonObjInstance), object);
            break;
        }
        case NEON_OBJ_NATIVE:
            {
                neon_mem_release(state, sizeof(NeonObjNativeFunction), object);
            }
            break;
        case NEON_OBJ_STRING:
            {
                NeonObjString* string;
                string = (NeonObjString*)object;
                neon_string_release(state, string);
                neon_mem_release(state, sizeof(NeonObjString), object);
            }
            break;
        case NEON_OBJ_ARRAY:
            {
                NeonObjArray* arr;
                arr = (NeonObjArray*)object;
                neon_valarray_free(&arr->vala);
                neon_mem_release(state, sizeof(NeonObjArray), object);
            }
            break;
        case NEON_OBJ_MAP:
            {
                NeonObjMap* om;
                om = (NeonObjMap*)object;
                neon_hashtable_free(state, &om->mapping);
                neon_mem_release(state, sizeof(NeonObjMap), object);
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


NeonStringBuffer* neon_strbuf_make(NeonState* state)
{
    NeonStringBuffer* sbuf;
    sbuf = (NeonStringBuffer*)malloc(sizeof(NeonStringBuffer));
    sbuf->pvm = state;
    sbuf->length = 0;
    sbuf->data = NULL;
    sbuf->capacity = 0;
    return sbuf;
}

void neon_strbuf_release(NeonState* state, NeonStringBuffer* sb)
{
    neon_mem_freearray(state, sizeof(char), sb->data, sb->length + 1);
}

bool neon_strbuf_append(NeonStringBuffer* sb, const char* extstr, size_t extlen)
{
    enum {
        string_chunk_size = 52,
    };
    size_t i;
    size_t needsz;
    size_t selfpos;
    size_t extpos;
    char* temp;
    temp = NULL;
    if((extstr != NULL) && (extlen > 0))
    {
        if(extlen > 0)
        {
            // + 2 for the '\0' characters
            if(sb->capacity < sb->length + extlen + 2)
            {
                needsz = sb->length + extlen + string_chunk_size;
                temp = (char*)realloc(sb->data, needsz);
                if(temp == NULL)
                {
                    fprintf(stderr, "neon_strbuf_append: realloc(%ld) failed!\n", needsz);
                }
                sb->data = temp;
                sb->capacity = sb->length + extlen + string_chunk_size;
            }
            if(sb->capacity >= sb->length + extlen + 2)
            {
                selfpos = sb->length;
                //for(selfpos = sb->length, extpos = 0; extpos < extlen; selfpos++, extpos++)
                //while((extpos < extlen) && (extpos < extlen))
                //while(true)
                //fprintf(stderr, "strbuf_append: extlen=%d extstr=\"%s\"\n", extlen, extstr);
                for(extpos=0; extpos<extlen; extpos++)
                {
                    sb->data[selfpos] = extstr[extpos];
                    selfpos++;
                }
                sb->length += extlen;
                sb->data[selfpos] = 0;
            }
        }
        return true;
    }
    return false;
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
    wr->strbuf = NULL;
    wr->handle = NULL;
    return wr;
}

void neon_writer_release(NeonWriter* wr)
{
    NeonState* state;
    state = wr->pvm;
    if(wr->isstring)
    {
        neon_strbuf_release(state, wr->strbuf);
        free(wr->strbuf);
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
    wr->strbuf = neon_strbuf_make(state);
    return wr;
}

void neon_writer_writestringl(NeonWriter* wr, const char* estr, size_t elen)
{
    if(wr->isstring)
    {
        neon_strbuf_append(wr->strbuf, estr, elen);
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
                neon_writer_writeformat(wr, "\\x%02x", (unsigned char)ch);
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
        neon_writer_writeformat(wr, "(len=%d)", len);
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

void neon_writer_vwriteformat(NeonWriter* wr, const char* fmt, va_list va)
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

void neon_writer_writeformat(NeonWriter* wr, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    neon_writer_vwriteformat(wr, fmt, va);
    va_end(va);
}

void neon_writer_printfunction(NeonWriter* wr, NeonObjScriptFunction* ofn)
{
    if(ofn->name == NULL)
    {
        neon_writer_writestring(wr, "<script>");
    }
    else
    {
        neon_writer_writeformat(wr, "<fn %s>", ofn->name->sbuf->data);
    }
}

void neon_writer_printarray(NeonWriter* wr, NeonObjArray* arr)
{
    size_t i;
    size_t asz;
    asz = neon_valarray_size(&arr->vala);
    neon_writer_writestring(wr, "[");
    for(i=0; i<asz; i++)
    {
        neon_writer_writeformat(wr, "%ld: <(%d) %s>", i, arr->vala.values[i].type, neon_value_valuetypename(arr->vala.values[i]));
        neon_writer_printvalue(wr, arr->vala.values[i], true);
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
    cap = map->mapping.capacity;
    neon_writer_writestring(wr, "{");
    for(i=0; i<cap; i++)
    {
        entry = &map->mapping.entries[i];
        if(entry != NULL)
        {
            if(entry->key != NULL)
            {
                neon_writer_printstring(wr, entry->key, true);
                neon_writer_writeformat(wr, ": ");
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
        case NEON_OBJ_BOUNDMETHOD:
            {
                neon_writer_printfunction(wr, neon_value_asboundfunction(value)->method->innerfn);
            }
            break;
        case NEON_OBJ_CLASS:
            {
                neon_writer_writeformat(wr, "<class '%s'>", neon_value_asclass(value)->name->sbuf->data);
            }
            break;
        case NEON_OBJ_CLOSURE:
            {
                neon_writer_printfunction(wr, neon_value_asclosure(value)->innerfn);
            }
            break;
        case NEON_OBJ_FUNCTION:
            {
                neon_writer_printfunction(wr, neon_value_asscriptfunction(value));
            }
            break;
        case NEON_OBJ_INSTANCE:
            {
                neon_writer_writeformat(wr, "<instance '%s'>", neon_value_asinstance(value)->klass->name->sbuf->data);
            }
            break;
        case NEON_OBJ_NATIVE:
            {
                neon_writer_writeformat(wr, "<nativefn>");
            }
            break;
        case NEON_OBJ_STRING:
            {
                neon_writer_printstring(wr, neon_value_asstring(value), fixstring);
            }
            break;
        case NEON_OBJ_UPVALUE:
            {
                neon_writer_writeformat(wr, "<upvalue>");
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
    }
}

void neon_writer_printvalue(NeonWriter* wr, NeonValue value, bool fixstring)
{
    switch(value.type)
    {
        case NEON_VAL_BOOL:
            neon_writer_writestring(wr, neon_value_asbool(value) ? "true" : "false");
            break;
        case NEON_VAL_NIL:
            neon_writer_writestring(wr, "nil");
            break;
        case NEON_VAL_NUMBER:
            neon_writer_writeformat(wr, "%g", neon_value_asnumber(value));
            break;
        case NEON_VAL_OBJ:
            neon_writer_printobject(wr, value, fixstring);
            break;
    }
}

const char* neon_value_objecttypename(NeonObject* obj)
{
    switch(obj->type)
    {
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

const char* neon_value_valuetypename(NeonValue value)
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
            return neon_value_objecttypename(neon_value_asobject(value));
            break;
    }
    return "?unknownvalue?";
}

bool neon_value_stringequal(NeonObjString* aos, NeonObjString* bos)
{
    if(aos->sbuf->length == bos->sbuf->length)
    {
        if(memcmp(aos->sbuf->data, bos->sbuf->data, aos->sbuf->length) == 0)
        {
            return true;
        }
    }
    return false;
}

bool neon_value_objectequal(NeonValue a, NeonValue b)
{
    if(neon_value_isstring(a) && neon_value_isstring(b))
    {
        return neon_value_stringequal(neon_value_asstring(a), neon_value_asstring(b));
    }
    return neon_value_asobject(a) == neon_value_asobject(b);
}

bool neon_value_equal(NeonValue a, NeonValue b)
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
                return neon_value_objectequal(a, b);
            }
            break;
        default:
            break;
    }
    return false;
}

void neon_valarray_init(NeonState* state, NeonValArray* array)
{
    array->pvm = state;
    array->values = NULL;
    array->capacity = 0;
    array->size = 0;
}

size_t neon_valarray_count(NeonValArray* array)
{
    return array->size;
}

size_t neon_valarray_size(NeonValArray* array)
{
    return array->size;
}

NeonValue neon_valarray_at(NeonValArray* array, size_t i)
{
    return array->values[i];
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
    arr->size--;
    return arr->values[arr->size + 1];
}

void neon_valarray_free(NeonValArray* arr)
{
    NeonState* state;
    state = arr->pvm;
    neon_mem_freearray(state, sizeof(NeonValue), arr->values, arr->capacity);
    neon_valarray_init(state, arr);
}

NeonObject* neon_object_allocobj(NeonState* state, size_t size, NeonObjType type)
{
    NeonObject* baseobj;
    baseobj = (NeonObject*)neon_mem_wrapalloc(state, NULL, 0, size);
    baseobj->type = type;
    baseobj->ismarked = false;
    baseobj->next = state->gcstate.linkedobjects;
    state->gcstate.linkedobjects = baseobj;
#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)baseobj, size, type);
#endif
    return baseobj;
}

NeonObjBoundFunction* neon_object_makeboundmethod(NeonState* state, NeonValue receiver, NeonObjClosure* method)
{
    NeonObjBoundFunction* obj;
    obj = (NeonObjBoundFunction*)neon_object_allocobj(state, sizeof(NeonObjBoundFunction), NEON_OBJ_BOUNDMETHOD);
    obj->receiver = receiver;
    obj->method = method;
    return obj;
}

NeonObjClass* neon_object_makeclass(NeonState* state, NeonObjString* name)
{
    NeonObjClass* obj;
    obj = (NeonObjClass*)neon_object_allocobj(state, sizeof(NeonObjClass), NEON_OBJ_CLASS);
    obj->name = name;
    neon_hashtable_init(&obj->methods);
    return obj;
}

NeonObjClosure* neon_object_makeclosure(NeonState* state, NeonObjScriptFunction* ofn)
{
    int i;
    NeonObjClosure* closure;
    NeonObjUpvalue** upvals;
    upvals = (NeonObjUpvalue**)neon_mem_allocate(state, sizeof(NeonObjUpvalue*), ofn->upvaluecount);
    for(i = 0; i < ofn->upvaluecount; i++)
    {
        upvals[i] = NULL;
    }
    closure = (NeonObjClosure*)neon_object_allocobj(state, sizeof(NeonObjClosure), NEON_OBJ_CLOSURE);
    closure->innerfn = ofn;
    closure->upvalues = upvals;
    closure->upvaluecount = ofn->upvaluecount;
    return closure;
}

NeonObjScriptFunction* neon_object_makefunction(NeonState* state)
{
    NeonObjScriptFunction* obj;
    obj = (NeonObjScriptFunction*)neon_object_allocobj(state, sizeof(NeonObjScriptFunction), NEON_OBJ_FUNCTION);
    obj->arity = 0;
    obj->upvaluecount = 0;
    obj->name = NULL;
    neon_chunk_init(state, &obj->chunk);
    return obj;
}

NeonObjInstance* neon_object_makeinstance(NeonState* state, NeonObjClass* klass)
{
    NeonObjInstance* obj;
    obj = (NeonObjInstance*)neon_object_allocobj(state, sizeof(NeonObjInstance), NEON_OBJ_INSTANCE);
    obj->klass = klass;
    neon_hashtable_init(&obj->fields);
    return obj;
}

NeonObjNativeFunction* neon_object_makenative(NeonState* state, NeonNativeFN nat)
{
    NeonObjNativeFunction* obj;
    obj = (NeonObjNativeFunction*)neon_object_allocobj(state, sizeof(NeonObjNativeFunction), NEON_OBJ_NATIVE);
    obj->natfunc = nat;
    return obj;
}

NeonObjString* neon_string_allocfromstrbuf(NeonState* state, NeonStringBuffer* sbuf, uint32_t hash)
{
    NeonObjString* rs;
    rs = (NeonObjString*)neon_object_allocobj(state, sizeof(NeonObjString), NEON_OBJ_STRING);
    rs->sbuf = sbuf;
    rs->hash = hash;
    neon_vm_stackpush(state, neon_value_makeobject(rs));
    neon_hashtable_set(state, &state->strings, rs, neon_value_makenil());
    neon_vm_stackpop(state);
    return rs;
}

NeonObjString* neon_string_allocate(NeonState* state, const char* estr, size_t elen, uint32_t hash)
{
    NeonStringBuffer* sbuf;
    sbuf = neon_strbuf_make(state);
    neon_strbuf_append(sbuf, estr, elen);
    return neon_string_allocfromstrbuf(state, sbuf, hash);
}

NeonObjString* neon_string_take(NeonState* state, char* estr, size_t elen)
{
    uint32_t hash;
    NeonObjString* rs;
    hash = neon_util_hashstring(estr, elen);
    rs = neon_hashtable_findstring(state, &state->strings, estr, elen, hash);
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
    rs = neon_hashtable_findstring(state, &state->strings, estr, elen, hash);
    if(rs != NULL)
    {
        return rs;
    }
    rs = neon_string_allocate(state, estr, elen, hash);
    return rs;
}

void neon_string_release(NeonState* state, NeonObjString* os)
{
    neon_strbuf_release(state, os->sbuf);
    free(os->sbuf);
}

bool neon_string_append(NeonObjString* os, const char* extstr, size_t extlen)
{
    return neon_strbuf_append(os->sbuf, extstr, extlen);
}

NeonObjArray* neon_array_make(NeonState* state)
{
    NeonObjArray* oa;
    oa = (NeonObjArray*)neon_object_allocobj(state, sizeof(NeonObjArray), NEON_OBJ_ARRAY);
    oa->pvm = state;
    neon_valarray_init(state, &oa->vala);
    oa->vala.size = 0;
    return oa;
}

bool neon_array_push(NeonObjArray* arr, NeonValue val)
{
    neon_valarray_push(&arr->vala, val);
    return true;
}

NeonObjMap* neon_object_makemap(NeonState* state)
{
    NeonObjMap* obj;
    obj = (NeonObjMap*)neon_object_allocobj(state, sizeof(NeonObjMap), NEON_OBJ_MAP);
    obj->pvm = state;
    neon_hashtable_init(&obj->mapping);
    return obj;
}

bool neon_map_set(NeonObjMap* map, NeonObjString* name, NeonValue val)
{
    return neon_hashtable_set(map->pvm, &map->mapping, name, val);
}

//bool neon_hashtable_get(NeonState* state, NeonHashTable* table, NeonObjString* key, NeonValue* value)
bool neon_map_get(NeonObjMap* map, NeonObjString* key, NeonValue* dest)
{
    return neon_hashtable_get(map->pvm, &map->mapping, key, dest);
}

NeonObjUpvalue* neon_object_makeupvalue(NeonState* state, NeonValue* pslot)
{
    NeonObjUpvalue* obj;
    obj = (NeonObjUpvalue*)neon_object_allocobj(state, sizeof(NeonObjUpvalue), NEON_OBJ_UPVALUE);
    obj->closed = neon_value_makenil();
    obj->location = pslot;
    obj->next = NULL;
    return obj;
}

void neon_hashtable_init(NeonHashTable* table)
{
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void neon_hashtable_free(NeonState* state, NeonHashTable* table)
{
    neon_mem_freearray(state, sizeof(NeonHashEntry), table->entries, table->capacity);
    neon_hashtable_init(table);
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

bool neon_hashtable_get(NeonState* state, NeonHashTable* table, NeonObjString* key, NeonValue* value)
{
    NeonHashEntry* entry;
    (void)state;
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
    entries = (NeonHashEntry*)neon_mem_allocate(state, sizeof(NeonHashEntry), capacity);
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

bool neon_hashtable_set(NeonState* state, NeonHashTable* table, NeonObjString* key, NeonValue value)
{
    int capacity;
    bool isnewkey;
    NeonHashEntry* entry;
    if(table->count + 1 > table->capacity * TABLE_MAX_LOAD)
    {
        capacity = GROW_CAPACITY(table->capacity);
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

void neon_hashtable_addall(NeonState* state, NeonHashTable* from, NeonHashTable* to)
{
    int i;
    NeonHashEntry* entry;
    for(i = 0; i < from->capacity; i++)
    {
        entry = &from->entries[i];
        if(entry->key != NULL)
        {
            neon_hashtable_set(state, to, entry->key, entry->value);
        }
    }
}

NeonObjString* neon_hashtable_findstring(NeonState* state, NeonHashTable* table, const char* estr, int elen, uint32_t hash)
{
    uint32_t index;
    NeonHashEntry* entry;
    (void)state;
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
    int i;
    NeonHashEntry* entry;
    for(i = 0; i < table->capacity; i++)
    {
        entry = &table->entries[i];
        if(entry->key != NULL && !entry->key->obj.ismarked)
        {
            neon_hashtable_delete(state, table, entry->key);
        }
    }
}

void neon_chunk_init(NeonState* state, NeonChunk* chunk)
{
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    neon_valarray_init(state, &chunk->constants);
}

void neon_chunk_free(NeonState* state, NeonChunk* chunk)
{
    neon_mem_freearray(state, sizeof(int32_t), chunk->code, chunk->capacity);
    neon_mem_freearray(state, sizeof(int), chunk->lines, chunk->capacity);
    neon_valarray_free(&chunk->constants);
    neon_chunk_init(state, chunk);
}

void neon_chunk_pushbyte(NeonState* state, NeonChunk* chunk, int32_t byte, int line)
{
    int oldcap;
    if(chunk->capacity < chunk->count + 1)
    {
        oldcap = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldcap);
        chunk->code = (int32_t*)neon_mem_growarray(state, sizeof(int32_t), chunk->code, oldcap, chunk->capacity);
        chunk->lines = (int*)neon_mem_growarray(state, sizeof(int), chunk->lines, oldcap, chunk->capacity);
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int neon_chunk_pushconst(NeonState* state, NeonChunk* chunk, NeonValue value)
{
    neon_vm_stackpush(state, value);
    neon_valarray_push(&chunk->constants, value);
    neon_vm_stackpop(state);
    return neon_valarray_count(&chunk->constants) - 1;
}

void neon_chunk_disasm(NeonState* state, NeonWriter* wr, NeonChunk* chunk, const char* name)
{
    int offset;
    neon_writer_writeformat(wr, "== %s ==\n", name);
    for(offset = 0; offset < chunk->count;)
    {
        offset = neon_dbg_dumpdisasm(state, wr, chunk, offset);
    }
}

int neon_dbg_dumpconstinstr(NeonState* state, NeonWriter* wr, const char* name, NeonChunk* chunk, int offset)
{
    NeonValue val;
    int32_t constant;
    (void)state;
    val = neon_value_makenil();
    constant = chunk->code[offset + 1];
    if(chunk->constants.size > 0)
    {
        val = chunk->constants.values[constant];
    }
    neon_writer_writeformat(wr, "%-16s %4d '", name, constant);
    neon_writer_printvalue(wr, val, true);
    neon_writer_writestring(wr, "'\n");
    return offset + 2;
}

int neon_dbg_dumpinvokeinstr(NeonState* state, NeonWriter* wr, const char* name, NeonChunk* chunk, int offset)
{
    int32_t argc;
    int32_t constant;
    (void)state;
    constant = chunk->code[offset + 1];
    argc = chunk->code[offset + 2];
    neon_writer_writeformat(wr, "%-16s (%d args) %4d {", name, argc, constant);
    neon_writer_printvalue(wr, chunk->constants.values[constant], true);
    neon_writer_writestring(wr, "}\n");
    return offset + 3;
}

int neon_dbg_dumpsimpleinstr(NeonState* state, NeonWriter* wr, const char* name, int offset)
{
    (void)state;
    neon_writer_writeformat(wr, "%s\n", name);
    return offset + 1;
}

int neon_dbg_dumpbyteinstr(NeonState* state, NeonWriter* wr, const char* name, NeonChunk* chunk, int offset)
{
    int32_t islot;
    (void)state;
    islot = chunk->code[offset + 1];
    neon_writer_writeformat(wr, "%-16s %4d\n", name, islot);
    return offset + 2;// [debug]
}

int neon_dbg_dumpjumpinstr(NeonState* state, NeonWriter* wr, const char* name, int sign, NeonChunk* chunk, int offset)
{
    uint16_t jump;
    (void)state;
    jump= (uint16_t)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    neon_writer_writeformat(wr, "%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

int neon_dbg_dumpclosure(NeonState* state, NeonWriter* wr, NeonChunk* chunk, int offset)
{
    int j;
    int index;
    int islocal;
    int32_t constant;
    NeonObjScriptFunction* fn;
    (void)state;
    offset++;
    constant = chunk->code[offset++];
    neon_writer_writeformat(wr, "%-16s %4d ", "NEON_OP_CLOSURE", constant);
    neon_writer_printvalue(wr, chunk->constants.values[constant], true);
    neon_writer_writestring(wr, "\n");
    fn = neon_value_asscriptfunction(chunk->constants.values[constant]);
    for(j = 0; j < fn->upvaluecount; j++)
    {
        islocal = chunk->code[offset++];
        index = chunk->code[offset++];
        neon_writer_writeformat(wr, "%04d      |                     %s %d\n", offset - 2, islocal ? "local" : "upvalue", index);
    }
    return offset;
}

int neon_dbg_dumpdisasm(NeonState* state, NeonWriter* wr, NeonChunk* chunk, int offset)
{
    int32_t instruction;
    (void)state;
    neon_writer_writeformat(wr, "%04d ", offset);
    if((offset > 0) && (chunk->lines[offset] == chunk->lines[offset - 1]))
    {
        neon_writer_writeformat(wr, "   | ");
    }
    else
    {
        neon_writer_writeformat(wr, "%4d ", chunk->lines[offset]);
    }
    instruction = chunk->code[offset];
    switch(instruction)
    {
        case NEON_OP_PUSHCONST:
            return neon_dbg_dumpconstinstr(state, wr, "NEON_OP_PUSHCONST", chunk, offset);
        case NEON_OP_PUSHONE:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_PUSHONE", offset);
        case NEON_OP_PUSHNIL:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_PUSHNIL", offset);
        case NEON_OP_PUSHTRUE:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_PUSHTRUE", offset);
        case NEON_OP_PUSHFALSE:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_PUSHFALSE", offset);
        case NEON_OP_MAKEARRAY:
            return neon_dbg_dumpconstinstr(state, wr, "NEON_OP_MAKEARRAY", chunk, offset);
        case NEON_OP_MAKEMAP:
            return neon_dbg_dumpconstinstr(state, wr, "NEON_OP_MAKEMAP", chunk, offset);
        case NEON_OP_INDEXGET:
            return neon_dbg_dumpbyteinstr(state, wr, "NEON_OP_INDEXGET", chunk, offset);
        case NEON_OP_INDEXSET:
            return neon_dbg_dumpbyteinstr(state, wr, "NEON_OP_INDEXSET", chunk, offset);
        case NEON_OP_POP:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_POP", offset);
        case NEON_OP_LOCALGET:
            return neon_dbg_dumpbyteinstr(state, wr, "NEON_OP_LOCALGET", chunk, offset);
        case NEON_OP_LOCALSET:
            return neon_dbg_dumpbyteinstr(state, wr, "NEON_OP_LOCALSET", chunk, offset);
        case NEON_OP_GLOBALGET:
            return neon_dbg_dumpconstinstr(state, wr, "NEON_OP_GLOBALGET", chunk, offset);
        case NEON_OP_GLOBALDEFINE:
            return neon_dbg_dumpconstinstr(state, wr, "NEON_OP_GLOBALDEFINE", chunk, offset);
        case NEON_OP_GLOBALSET:
            return neon_dbg_dumpconstinstr(state, wr, "NEON_OP_GLOBALSET", chunk, offset);
        case NEON_OP_UPVALGET:
            return neon_dbg_dumpbyteinstr(state, wr, "NEON_OP_UPVALGET", chunk, offset);
        case NEON_OP_UPVALSET:
            return neon_dbg_dumpbyteinstr(state, wr, "NEON_OP_UPVALSET", chunk, offset);
        case NEON_OP_PROPERTYGET:
            return neon_dbg_dumpconstinstr(state, wr, "NEON_OP_PROPERTYGET", chunk, offset);
        case NEON_OP_PROPERTYSET:
            return neon_dbg_dumpconstinstr(state, wr, "NEON_OP_PROPERTYSET", chunk, offset);
        case NEON_OP_INSTGETSUPER:
            return neon_dbg_dumpconstinstr(state, wr, "NEON_OP_INSTGETSUPER", chunk, offset);
        case NEON_OP_EQUAL:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_EQUAL", offset);
        case NEON_OP_PRIMGREATER:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_PRIMGREATER", offset);
        case NEON_OP_PRIMLESS:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_PRIMLESS", offset);
        case NEON_OP_PRIMADD:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_PRIMADD", offset);
        case NEON_OP_PRIMSUBTRACT:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_PRIMSUBTRACT", offset);
        case NEON_OP_PRIMMULTIPLY:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_PRIMMULTIPLY", offset);
        case NEON_OP_PRIMDIVIDE:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_PRIMDIVIDE", offset);
        case NEON_OP_PRIMNOT:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_PRIMNOT", offset);
        case NEON_OP_PRIMNEGATE:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_PRIMNEGATE", offset);
        case NEON_OP_DEBUGPRINT:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_PRINT", offset);
        case NEON_OP_JUMPNOW:
            return neon_dbg_dumpjumpinstr(state, wr, "NEON_OP_JUMPNOW", 1, chunk, offset);
        case NEON_OP_JUMPIFFALSE:
            return neon_dbg_dumpjumpinstr(state, wr, "NEON_OP_JUMPIFFALSE", 1, chunk, offset);
        case NEON_OP_LOOP:
            return neon_dbg_dumpjumpinstr(state, wr, "NEON_OP_LOOP", -1, chunk, offset);
        case NEON_OP_CALL:
            return neon_dbg_dumpbyteinstr(state, wr, "NEON_OP_CALL", chunk, offset);
        case NEON_OP_INSTTHISINVOKE:
            return neon_dbg_dumpinvokeinstr(state, wr, "NEON_OP_INSTTHISINVOKE", chunk, offset);
        case NEON_OP_INSTSUPERINVOKE:
            return neon_dbg_dumpinvokeinstr(state, wr, "NEON_OP_INSTSUPERINVOKE", chunk, offset);
        case NEON_OP_CLOSURE:
            {
                /*offset =*/ neon_dbg_dumpclosure(state, wr, chunk, offset);
                return offset;
            }
        case NEON_OP_UPVALCLOSE:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_UPVALCLOSE", offset);
        case NEON_OP_RETURN:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_RETURN", offset);
        case NEON_OP_CLASS:
            return neon_dbg_dumpconstinstr(state, wr, "NEON_OP_CLASS", chunk, offset);
        case NEON_OP_INHERIT:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_INHERIT", offset);
        case NEON_OP_METHOD:
            return neon_dbg_dumpconstinstr(state, wr, "NEON_OP_METHOD", chunk, offset);
        case NEON_OP_HALTVM:
            return neon_dbg_dumpsimpleinstr(state, wr, "NEON_OP_HALTVM", offset);
        /*
        default:
            neon_writer_writeformat(wr, "!!!!unknown opcode %d!!!!\n", instruction);
            return offset + 1;
            */
    }
    return offset + 1;
}

void neon_lex_init(NeonState* state, NeonAstScanner* scn, const char* source)
{
    scn->pvm = state;
    scn->start = source;
    scn->current = source;
    scn->line = 1;
}

bool neon_lex_isalpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool neon_lex_isdigit(char c)
{
    return c >= '0' && c <= '9';
}

bool neon_lex_isatend(NeonAstScanner* scn)
{
    return *scn->current == '\0';
}

char neon_lex_advance(NeonAstScanner* scn)
{
    scn->current++;
    return scn->current[-1];
}

char neon_lex_peekcurrent(NeonAstScanner* scn)
{
    return *scn->current;
}

char neon_lex_peeknext(NeonAstScanner* scn)
{
    if(neon_lex_isatend(scn))
    {
        return '\0';
    }
    return scn->current[1];
}

bool neon_lex_match(NeonAstScanner* scn, char expected)
{
    if(neon_lex_isatend(scn))
        return false;
    if(*scn->current != expected)
        return false;
    scn->current++;
    return true;
}

NeonAstToken neon_lex_maketoken(NeonAstScanner* scn, NeonAstTokType type)
{
    NeonAstToken token;
    token.type = type;
    token.start = scn->start;
    token.length = (int)(scn->current - scn->start);
    token.line = scn->line;
    return token;
}

NeonAstToken neon_lex_makeerrortoken(NeonAstScanner* scn, const char* message)
{
    NeonAstToken token;
    token.type = NEON_TOK_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scn->line;
    return token;
}

void neon_lex_skipspace(NeonAstScanner* scn)
{
    char c;
    for(;;)
    {
        c = neon_lex_peekcurrent(scn);
        switch(c)
        {
            case ' ':
            case '\r':
            case '\t':
                {
                    neon_lex_advance(scn);
                }
                break;
            case '\n':
                {
                    scn->line++;
                    neon_lex_advance(scn);
                }
                break;
            case '/':
                {
                    if(neon_lex_peeknext(scn) == '/')
                    {
                        // A comment goes until the end of the line.
                        while(neon_lex_peekcurrent(scn) != '\n' && !neon_lex_isatend(scn))
                            neon_lex_advance(scn);
                    }
                    else
                    {
                        return;
                    }
                }
                break;
            default:
                return;
        }
    }
}

NeonAstTokType neon_lex_scankeyword(NeonAstScanner* scn)
{
    static const struct
    {
        const char* str;
        NeonAstTokType type;
    } keywords[] =
    {
        {"and", NEON_TOK_KWAND},
        {"break", NEON_TOK_KWBREAK},
        {"continue", NEON_TOK_KWCONTINUE},
        {"class", NEON_TOK_KWCLASS},
        {"else", NEON_TOK_KWELSE},
        {"false", NEON_TOK_KWFALSE},
        {"for", NEON_TOK_KWFOR},
        {"function", NEON_TOK_KWFUNCTION},
        {"fun", NEON_TOK_KWFUNCTION},
        {"if", NEON_TOK_KWIF},
        {"nil", NEON_TOK_KWNIL},
        {"or", NEON_TOK_KWOR},
        {"debugprint", NEON_TOK_KWDEBUGPRINT},
        {"return", NEON_TOK_KWRETURN},
        {"super", NEON_TOK_KWSUPER},
        {"this", NEON_TOK_KWTHIS},
        {"true", NEON_TOK_KWTRUE},
        {"var", NEON_TOK_KWVAR},
        {"while", NEON_TOK_KWWHILE},
        {NULL, (NeonAstTokType)0},
    };
    size_t i;
    size_t kwlen;
    size_t ofs;
    const char* kwtext;
    for(i=0; keywords[i].str != NULL; i++)
    {
        kwtext = keywords[i].str;
        kwlen = strlen(kwtext);
        ofs = (scn->current - scn->start);
        if((ofs == (0 + kwlen)) && (memcmp(scn->start + 0, kwtext, kwlen) == 0))
        {
            return keywords[i].type;
        }
    }
    return NEON_TOK_IDENTIFIER;
}

NeonAstToken neon_lex_scanident(NeonAstScanner* scn)
{
    while(neon_lex_isalpha(neon_lex_peekcurrent(scn)) || neon_lex_isdigit(neon_lex_peekcurrent(scn)))
    {
        neon_lex_advance(scn);
    }
    return neon_lex_maketoken(scn, neon_lex_scankeyword(scn));
}

NeonAstToken neon_lex_scannumber(NeonAstScanner* scn)
{
    while(neon_lex_isdigit(neon_lex_peekcurrent(scn)))
    {
        neon_lex_advance(scn);
    }
    // Look for a fractional part.
    if(neon_lex_peekcurrent(scn) == '.' && neon_lex_isdigit(neon_lex_peeknext(scn)))
    {
        // Consume the ".".
        neon_lex_advance(scn);
        while(neon_lex_isdigit(neon_lex_peekcurrent(scn)))
        {
            neon_lex_advance(scn);
        }
    }
    return neon_lex_maketoken(scn, NEON_TOK_NUMBER);
}

NeonAstToken neon_lex_scanstring(NeonAstScanner* scn)
{
    while(neon_lex_peekcurrent(scn) != '"' && !neon_lex_isatend(scn))
    {
        if(neon_lex_peekcurrent(scn) == '\n')
        {
            scn->line++;
        }
        neon_lex_advance(scn);
    }
    if(neon_lex_isatend(scn))
    {
        return neon_lex_makeerrortoken(scn, "unterminated string");
    }
    // The closing quote.
    neon_lex_advance(scn);
    return neon_lex_maketoken(scn, NEON_TOK_STRING);
}

NeonAstToken neon_lex_scantoken(NeonAstScanner* scn)
{
    char c;
    neon_lex_skipspace(scn);
    scn->start = scn->current;
    if(neon_lex_isatend(scn))
    {
        return neon_lex_maketoken(scn, NEON_TOK_EOF);
    }
    c = neon_lex_advance(scn);
    if(neon_lex_isalpha(c))
    {
        return neon_lex_scanident(scn);
    }
    if(neon_lex_isdigit(c))
    {
        return neon_lex_scannumber(scn);
    }
    switch(c)
    {
        case '\n':
            {
                return neon_lex_maketoken(scn, NEON_TOK_NEWLINE);
            }
            break;
        case '(':
            {
                return neon_lex_maketoken(scn, NEON_TOK_PARENOPEN);
            }
            break;
        case ')':
            {
                return neon_lex_maketoken(scn, NEON_TOK_PARENCLOSE);
            }
            break;
        case '{':
            {
                return neon_lex_maketoken(scn, NEON_TOK_BRACEOPEN);
            }
            break;
        case '}':
            {
                return neon_lex_maketoken(scn, NEON_TOK_BRACECLOSE);
            }
            break;
        case '[':
            {
                return neon_lex_maketoken(scn, NEON_TOK_BRACKETOPEN);
            }
            break;
        case ']':
            {
                return neon_lex_maketoken(scn, NEON_TOK_BRACKETCLOSE);
            }
            break;
        case ';':
            {
                return neon_lex_maketoken(scn, NEON_TOK_SEMICOLON);
            }
            break;
        case ',':
            {
                return neon_lex_maketoken(scn, NEON_TOK_COMMA);
            }
            break;
        case '.':
            {
                return neon_lex_maketoken(scn, NEON_TOK_DOT);
            }
            break;
        case '-':
            {
                if(neon_lex_match(scn, '-'))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_DECREMENT);
                }
                return neon_lex_maketoken(scn, NEON_TOK_MINUS);
            }
            break;
        case '+':
            {
                if(neon_lex_match(scn, '+'))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_INCREMENT);
                }
                return neon_lex_maketoken(scn, NEON_TOK_PLUS);
            }
            break;
        case '&':
            {
                if(neon_lex_match(scn, '&'))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_KWAND);
                }
                return neon_lex_maketoken(scn, NEON_TOK_BINAND);
            }
            break;
        case '|':
            {
                if(neon_lex_match(scn, '|'))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_KWOR);
                }
                return neon_lex_maketoken(scn, NEON_TOK_BINOR);
            }
            break;
        case '^':
            {
                return neon_lex_maketoken(scn, NEON_TOK_BINXOR);
            }
            break;
        case '%':
            {
                return neon_lex_maketoken(scn, NEON_TOK_MODULO);
            }
            break;
        case '/':
            {
                return neon_lex_maketoken(scn, NEON_TOK_SLASH);
            }
            break;
        case '*':
            {
                return neon_lex_maketoken(scn, NEON_TOK_STAR);
            }
            break;
        case '!':
            {
                if(neon_lex_match(scn, '='))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_COMPNOTEQUAL);
                }
                return neon_lex_maketoken(scn, NEON_TOK_EXCLAM);
            }
            break;
        case '=':
            {
                if(neon_lex_match(scn, '='))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_COMPEQUAL);
                }
                return neon_lex_maketoken(scn, NEON_TOK_ASSIGN);
            }
            break;
        case '<':
            {
                if(neon_lex_match(scn, '='))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_COMPLESSEQUAL);
                }
                else if(neon_lex_match(scn, '<'))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_SHIFTLEFT);
                }
                return neon_lex_maketoken(scn, NEON_TOK_COMPLESSTHAN);
            }
            break;
        case '>':
            {
                if(neon_lex_match(scn, '='))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_COMPGREATEREQUAL);
                }
                else if(neon_lex_match(scn, '>'))
                {
                    return neon_lex_maketoken(scn, NEON_TOK_SHIFTRIGHT);
                }
                return neon_lex_maketoken(scn, NEON_TOK_COMPGREATERTHAN);
            }
            break;
        case '"':
            {
                return neon_lex_scanstring(scn);
            }
            break;
    }
    return neon_lex_makeerrortoken(scn, "unexpected character");
}

void neon_prs_init(NeonState* state, NeonAstParser* parser, NeonAstScanner* scn)
{
    state->parser = parser;
    parser->pvm = state;
    parser->pscn = scn;
    parser->currcompiler = NULL;
    parser->currclass = NULL;
}

NeonChunk* neon_prs_currentchunk(NeonAstParser* prs)
{
    return &prs->currcompiler->compiledfn->chunk;
}

void neon_prs_vraiseattoken(NeonAstParser* prs, NeonAstToken* token, const char* message, va_list va)
{
    if(prs->panicmode)
    {
        return;
    }
    prs->panicmode = true;
    fprintf(stderr, "[line %d] error", token->line);
    if(token->type == NEON_TOK_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if(token->type == NEON_TOK_ERROR)
    {
        // Nothing.
    }
    else
    {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    fprintf(stderr, ": ");
    vfprintf(stderr, message, va);
    fprintf(stderr, "\n");
    prs->haderror = true;
}

void neon_prs_raiseerror(NeonAstParser* prs, const char* message, ...)
{
    va_list va;
    va_start(va, message);
    neon_prs_vraiseattoken(prs, &prs->previous, message, va);
    va_end(va);
}

void neon_prs_raiseatcurrent(NeonAstParser* prs, const char* message, ...)
{
    va_list va;
    va_start(va, message);
    neon_prs_vraiseattoken(prs, &prs->current, message, va);
    va_end(va);
}

const char* neon_prs_op2str(int32_t opcode)
{
    switch(opcode)
    {
        case NEON_OP_PUSHCONST: return "NEON_OP_PUSHCONST";
        case NEON_OP_PUSHNIL: return "NEON_OP_PUSHNIL";
        case NEON_OP_PUSHTRUE: return "NEON_OP_PUSHTRUE";
        case NEON_OP_PUSHFALSE: return "NEON_OP_PUSHFALSE";
        case NEON_OP_PUSHONE: return "NEON_OP_PUSHONE";
        case NEON_OP_POP: return "NEON_OP_POP";
        case NEON_OP_DUP: return "NEON_OP_DUP";
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
        case NEON_OP_PRIMNOT: return "NEON_OP_PRIMNOT";
        case NEON_OP_PRIMNEGATE: return "NEON_OP_PRIMNEGATE";
        case NEON_OP_DEBUGPRINT: return "NEON_OP_DEBUGPRINT";
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
        case NEON_OP_PSEUDOBREAK: return "NEON_OP_PSEUDOBREAK";

    }
    return "?unknown?";
}

void neon_prs_skipsemicolon(NeonAstParser* prs)
{
    while(neon_prs_match(prs, NEON_TOK_NEWLINE))
    {
    }
    while(neon_prs_match(prs, NEON_TOK_SEMICOLON))
    {
    }
}

void neon_prs_advance(NeonAstParser* prs)
{
    prs->previous = prs->current;
    for(;;)
    {
        prs->current = neon_lex_scantoken(prs->pscn);
        if(prs->current.type != NEON_TOK_ERROR)
        {
            break;
        }
        neon_prs_raiseatcurrent(prs, prs->current.start);
    }
}

void neon_prs_consume(NeonAstParser* prs, NeonAstTokType type, const char* message)
{
    if(prs->current.type == type)
    {
        neon_prs_advance(prs);
        return;
    }
    neon_prs_raiseatcurrent(prs, message);
}

bool neon_prs_checkcurrent(NeonAstParser* prs, NeonAstTokType type)
{
    return prs->current.type == type;
}

bool neon_prs_match(NeonAstParser* prs, NeonAstTokType type)
{
    if(!neon_prs_checkcurrent(prs, type))
    {
        return false;
    }
    neon_prs_advance(prs);
    return true;
}

void neon_prs_emit1byte(NeonAstParser* prs, int32_t byte)
{
    neon_chunk_pushbyte(prs->pvm, neon_prs_currentchunk(prs), byte, prs->previous.line);
}

void neon_prs_emit2byte(NeonAstParser* prs, int32_t byte1, int32_t byte2)
{
    neon_prs_emit1byte(prs, byte1);
    neon_prs_emit1byte(prs, byte2);
}

void neon_prs_emitloop(NeonAstParser* prs, int loopstart)
{
    int offset;
    neon_prs_emit1byte(prs, NEON_OP_LOOP);
    offset = neon_prs_currentchunk(prs)->count - loopstart + 2;
    if(offset > UINT16_MAX)
    {
        neon_prs_raiseerror(prs, "loop body too large");
    }
    neon_prs_emit1byte(prs, (offset >> 8) & 0xff);
    neon_prs_emit1byte(prs, offset & 0xff);
}


int neon_prs_realgetcodeargscount(const int32_t* code, int ip)
{
    int32_t op = code[ip];
    switch(op)
    {
        case NEON_OP_PUSHTRUE:
        case NEON_OP_PUSHFALSE:
        case NEON_OP_PUSHNIL:
        case NEON_OP_POP:
        case NEON_OP_EQUAL:
        case NEON_OP_PRIMGREATER:
        case NEON_OP_PRIMLESS:
        case NEON_OP_PRIMADD:
        case NEON_OP_PRIMSUBTRACT:
        case NEON_OP_PRIMMULTIPLY:
        case NEON_OP_PRIMDIVIDE:
        case NEON_OP_PRIMBINAND:
        case NEON_OP_PRIMBINOR:
        case NEON_OP_PRIMBINXOR:
        case NEON_OP_PRIMMODULO:
        case NEON_OP_PRIMSHIFTLEFT:
        case NEON_OP_PRIMSHIFTRIGHT:
        case NEON_OP_PRIMNOT:
        case NEON_OP_PRIMNEGATE:
        case NEON_OP_DEBUGPRINT:
        //case NEON_OP_DUP:
        case NEON_OP_PUSHONE:
        case NEON_OP_UPVALCLOSE:
        case NEON_OP_RETURN:
        case NEON_OP_INHERIT:
        case NEON_OP_INDEXSET:
            return 0;
        case NEON_OP_POPN:
        case NEON_OP_PUSHCONST:
        //case NEON_OP_CONSTANT_LONG:
        //case NEON_OP_POPN:
        case NEON_OP_LOCALSET:
        case NEON_OP_LOCALGET:
        case NEON_OP_UPVALSET:
        case NEON_OP_UPVALGET:
        case NEON_OP_PROPERTYGET:
        case NEON_OP_PROPERTYSET:
        /*
        case NEON_OP_GET_EXPR_PROPERTY:
        case NEON_OP_SET_EXPR_PROPERTY:
        */
        case NEON_OP_CALL:
        case NEON_OP_INSTGETSUPER:
        case NEON_OP_GLOBALGET:
        case NEON_OP_GLOBALDEFINE:
        case NEON_OP_GLOBALSET:
        case NEON_OP_CLOSURE:
        case NEON_OP_CLASS:
        case NEON_OP_METHOD:
        case NEON_OP_INDEXGET:
            return 1;
        case NEON_OP_MAKEARRAY:
        case NEON_OP_MAKEMAP:
        case NEON_OP_JUMPNOW:
        case NEON_OP_JUMPIFFALSE:
        case NEON_OP_PSEUDOBREAK:
        case NEON_OP_LOOP:
        case NEON_OP_INSTTHISINVOKE:
        case NEON_OP_INSTSUPERINVOKE:
            return 2;
    }
    fprintf(stderr, "internal error: failed to compute operand argument size\n");
    return -1;
}

int neon_prs_getcodeargscount(const int32_t* bytecode, int ip)
{
    int rc;
    //const char* os;
    rc = neon_prs_realgetcodeargscount(bytecode, ip);
    //os = neon_prs_op2str(bytecode[ip]);
    //fprintf(stderr, "getcodeargscount(..., code=%s) = %d\n", os, rc);
    return rc;
}

void neon_prs_startloop(NeonAstParser* prs, NeonAstLoop* loop)
{
    loop->enclosing = prs->currcompiler->loop;
    loop->start = neon_prs_currentchunk(prs)->count;
    loop->scopedepth = prs->currcompiler->scopedepth;
    prs->currcompiler->loop = loop;
}

void neon_prs_endloop(NeonAstParser* prs)
{
    int i;
    NeonChunk* chunk;
    i = prs->currcompiler->loop->body;
    chunk = neon_prs_currentchunk(prs);
    while(i < chunk->count)
    {
        if(chunk->code[i] == NEON_OP_PSEUDOBREAK)
        {
            chunk->code[i] = NEON_OP_JUMPNOW;
            neon_prs_emitpatchjump(prs, i + 1);
            i += 3;
        }
        else
        {
            i += 1 + neon_prs_getcodeargscount(chunk->code, i);
        }
    }
    prs->currcompiler->loop = prs->currcompiler->loop->enclosing;
}

int neon_prs_emitjump(NeonAstParser* prs, int32_t instruction)
{
    neon_prs_emit1byte(prs, instruction);
    neon_prs_emit1byte(prs, 0xff);
    neon_prs_emit1byte(prs, 0xff);
    return neon_prs_currentchunk(prs)->count - 2;
}

void neon_prs_emitreturn(NeonAstParser* prs)
{
    if(prs->currcompiler->type == NEON_TYPE_INITIALIZER)
    {
        neon_prs_emit2byte(prs, NEON_OP_LOCALGET, 0);
    }
    else
    {
        //neon_prs_emit1byte(prs, NEON_OP_PUSHNIL);
    }
    neon_prs_emit1byte(prs, NEON_OP_RETURN);
}

int32_t neon_prs_makeconstant(NeonAstParser* prs, NeonValue value)
{
    int constant;
    constant = neon_chunk_pushconst(prs->pvm, neon_prs_currentchunk(prs), value);
    if(constant > UINT8_MAX)
    {
        neon_prs_raiseerror(prs, "too many constants in one chunk");
        return 0;
    }
    return (int32_t)constant;
}

void neon_prs_emitconstant(NeonAstParser* prs, NeonValue value)
{
    neon_prs_emit2byte(prs, NEON_OP_PUSHCONST, neon_prs_makeconstant(prs, value));
}

void neon_prs_emitpatchjump(NeonAstParser* prs, int offset)
{
    int jump;
    // -2 to adjust for the bytecode for the jump offset itself.
    jump = neon_prs_currentchunk(prs)->count - offset - 2;
    if(jump > UINT16_MAX)
    {
        neon_prs_raiseerror(prs, "too much code to jump over");
    }
    neon_prs_currentchunk(prs)->code[offset] = (jump >> 8) & 0xff;
    neon_prs_currentchunk(prs)->code[offset + 1] = jump & 0xff;
}

void neon_prs_compilerinit(NeonAstParser* prs, NeonAstCompiler* compiler, NeonAstFuncType type)
{
    NeonAstLocal* local;
    compiler->enclosing = prs->currcompiler;
    compiler->compiledfn = NULL;
    compiler->type = type;
    compiler->localcount = 0;
    compiler->scopedepth = 0;
    compiler->compiledfn = neon_object_makefunction(prs->pvm);
    prs->currcompiler = compiler;
    if(type != NEON_TYPE_SCRIPT)
    {
        prs->currcompiler->compiledfn->name = neon_string_copy(prs->pvm, prs->previous.start, prs->previous.length);
    }
    local = &prs->currcompiler->locals[prs->currcompiler->localcount++];
    local->depth = 0;
    local->iscaptured = false;
    /* Calls and Functions init-function-slot < Methods and Initializers slot-zero */
    /*
    local->name.start = "";
    local->name.length = 0;
    */
    if(type != NEON_TYPE_FUNCTION)
    {
        local->name.start = "this";
        local->name.length = 4;
    }
    else
    {
        local->name.start = "";
        local->name.length = 0;
    }
}

NeonObjScriptFunction* neon_prs_compilerfinish(NeonAstParser* prs, bool ismainfn)
{
    NeonObjScriptFunction* fn;
    neon_prs_emitreturn(prs);
    if(ismainfn)
    {
        //neon_prs_emit1byte(prs, NEON_OP_HALTVM);
    }
    fn = prs->currcompiler->compiledfn;
#if (DEBUG_PRINT_CODE == 1)
    if(!prs->haderror)
    {
        neon_chunk_disasm(prs->pvm, prs->pvm->stderrwriter, neon_prs_currentchunk(prs), fn->name != NULL ? fn->name->sbuf->data : "<script>");
    }
#endif
    prs->currcompiler = prs->currcompiler->enclosing;
    return fn;
}

void neon_prs_scopebegin(NeonAstParser* prs)
{
    prs->currcompiler->scopedepth++;
}

void neon_prs_scopeend(NeonAstParser* prs)
{
    NeonAstCompiler* pc;
    pc = prs->currcompiler;
    pc->scopedepth--;
    while(pc->localcount > 0 && pc->locals[pc->localcount - 1].depth > pc->scopedepth)
    {
        if(pc->locals[pc->localcount - 1].iscaptured)
        {
            neon_prs_emit1byte(prs, NEON_OP_UPVALCLOSE);
        }
        else
        {
            neon_prs_emit1byte(prs, NEON_OP_POP);
        }
        pc->localcount--;
    }
}

int32_t neon_prs_makeidentconstant(NeonAstParser* prs, NeonAstToken* name)
{
    return neon_prs_makeconstant(prs, neon_value_makeobject(neon_string_copy(prs->pvm, name->start, name->length)));
}

bool neon_prs_identsequal(NeonAstToken* a, NeonAstToken* b)
{
    if(a->length != b->length)
    {
        return false;
    }
    return memcmp(a->start, b->start, a->length) == 0;
}

int neon_prs_resolvelocal(NeonAstParser* prs, NeonAstCompiler* compiler, NeonAstToken* name)
{
    int i;
    NeonAstLocal* local;
    for(i = compiler->localcount - 1; i >= 0; i--)
    {
        local = &compiler->locals[i];
        if(neon_prs_identsequal(name, &local->name))
        {
            if(local->depth == -1)
            {
                neon_prs_raiseerror(prs, "cannot read local variable in its own initializer");
            }
            return i;
        }
    }
    return -1;
}

int neon_prs_addupval(NeonAstParser* prs, NeonAstCompiler* compiler, int32_t index, bool islocal)
{
    int i;
    int upvaluecount;
    NeonAstUpvalue* upvalue;
    upvaluecount = compiler->compiledfn->upvaluecount;
    for(i = 0; i < upvaluecount; i++)
    {
        upvalue = &compiler->upvalues[i];
        if(upvalue->index == index && upvalue->islocal == islocal)
        {
            return i;
        }
    }
    if(upvaluecount == UINT8_COUNT)
    {
        neon_prs_raiseerror(prs, "too many closure variables in function");
        return 0;
    }
    compiler->upvalues[upvaluecount].islocal = islocal;
    compiler->upvalues[upvaluecount].index = index;
    return compiler->compiledfn->upvaluecount++;
}

int neon_prs_resolveupval(NeonAstParser* prs, NeonAstCompiler* compiler, NeonAstToken* name)
{
    int local;
    int upvalue;
    if(compiler->enclosing == NULL)
    {
        return -1;
    }
    local = neon_prs_resolvelocal(prs, compiler->enclosing, name);
    if(local != -1)
    {
        compiler->enclosing->locals[local].iscaptured = true;
        return neon_prs_addupval(prs, compiler, (int32_t)local, true);
    }
    upvalue = neon_prs_resolveupval(prs, compiler->enclosing, name);
    if(upvalue != -1)
    {
        return neon_prs_addupval(prs, compiler, (int32_t)upvalue, false);
    }
    return -1;
}

void neon_prs_addlocal(NeonAstParser* prs, NeonAstToken name)
{
    NeonAstLocal* local;
    if(prs->currcompiler->localcount == UINT8_COUNT)
    {
        neon_prs_raiseerror(prs, "too many local variables in function");
        return;
    }
    local = &prs->currcompiler->locals[prs->currcompiler->localcount++];
    local->name = name;
    local->depth = -1;
    local->iscaptured = false;
}

int neon_prs_discardlocals(NeonAstParser* prs, NeonAstCompiler* current)
{
    int n;
    int lc;
    int depth;
    depth = current->loop->scopedepth + 1;
    lc = current->localcount - 1;
    n = 0;
    while(lc >= 0 && current->locals[lc].depth >= depth)
    {
        current->localcount--;
        n++;
        lc--;
    }
    if(n != 0)
    {
        neon_prs_emit2byte(prs, NEON_OP_POPN, (int32_t)n);
    }
    return current->localcount - lc - 1;
}

void neon_prs_parsevarident(NeonAstParser* prs)
{
    int i;
    NeonAstLocal* local;
    NeonAstToken* name;
    if(prs->currcompiler->scopedepth == 0)
    {
        return;
    }
    name = &prs->previous;
    for(i = prs->currcompiler->localcount - 1; i >= 0; i--)
    {
        local = &prs->currcompiler->locals[i];
        if(local->depth != -1 && local->depth < prs->currcompiler->scopedepth)
        {
            break;// [negative]
        }
        if(neon_prs_identsequal(name, &local->name))
        {
            neon_prs_raiseerror(prs, "already a variable with this name in this scope");
        }
    }
    neon_prs_addlocal(prs, *name);
}

int32_t neon_prs_parsevarname(NeonAstParser* prs, const char* errormessage)
{
    neon_prs_consume(prs, NEON_TOK_IDENTIFIER, errormessage);
    neon_prs_parsevarident(prs);
    if(prs->currcompiler->scopedepth > 0)
    {
        return 0;
    }
    return neon_prs_makeidentconstant(prs, &prs->previous);
}

void neon_prs_markinit(NeonAstParser* prs)
{
    if(prs->currcompiler->scopedepth == 0)
    {
        return;
    }
    prs->currcompiler->locals[prs->currcompiler->localcount - 1].depth = prs->currcompiler->scopedepth;
}

void neon_prs_emitdefvar(NeonAstParser* prs, int32_t global)
{
    if(prs->currcompiler->scopedepth > 0)
    {
        neon_prs_markinit(prs);
        return;
    }
    neon_prs_emit2byte(prs, NEON_OP_GLOBALDEFINE, global);
}

int32_t neon_prs_parsearglist(NeonAstParser* prs)
{
    int32_t argc;
    argc = 0;
    if(!neon_prs_checkcurrent(prs, NEON_TOK_PARENCLOSE))
    {
        do
        {
            neon_prs_parseexpr(prs);
            if(argc == 255)
            {
                neon_prs_raiseerror(prs, "cannot have more than 255 arguments");
            }
            argc++;
        } while(neon_prs_match(prs, NEON_TOK_COMMA));
    }
    neon_prs_consume(prs, NEON_TOK_PARENCLOSE, "expected ')' after arguments");
    return argc;
}

void neon_prs_ruleand(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    int endjump;
    endjump = neon_prs_emitjump(prs, NEON_OP_JUMPIFFALSE);
    neon_prs_emit1byte(prs, NEON_OP_POP);
    neon_prs_parseprec(prs, NEON_PREC_AND);
    neon_prs_emitpatchjump(prs, endjump);
}

void neon_prs_rulebinary(NeonAstParser* prs, bool canassign)
{
    NeonAstRule* rule;
    NeonAstTokType ot;
    (void)canassign;
    ot = prs->previous.type;
    rule = neon_prs_getrule(ot);
    neon_prs_parseprec(prs, (NeonAstPrecedence)(rule->precedence + 1));
    switch(ot)
    {
        case NEON_TOK_COMPNOTEQUAL:
            neon_prs_emit2byte(prs, NEON_OP_EQUAL, NEON_OP_PRIMNOT);
            break;
        case NEON_TOK_COMPEQUAL:
            neon_prs_emit1byte(prs, NEON_OP_EQUAL);
            break;
        case NEON_TOK_COMPGREATERTHAN:
            neon_prs_emit1byte(prs, NEON_OP_PRIMGREATER);
            break;
        case NEON_TOK_COMPGREATEREQUAL:
            neon_prs_emit2byte(prs, NEON_OP_PRIMLESS, NEON_OP_PRIMNOT);
            break;
        case NEON_TOK_COMPLESSTHAN:
            neon_prs_emit1byte(prs, NEON_OP_PRIMLESS);
            break;
        case NEON_TOK_COMPLESSEQUAL:
            neon_prs_emit2byte(prs, NEON_OP_PRIMGREATER, NEON_OP_PRIMNOT);
            break;
        case NEON_TOK_PLUS:
            neon_prs_emit1byte(prs, NEON_OP_PRIMADD);
            break;
        case NEON_TOK_MINUS:
            neon_prs_emit1byte(prs, NEON_OP_PRIMSUBTRACT);
            break;
        case NEON_TOK_STAR:
            neon_prs_emit1byte(prs, NEON_OP_PRIMMULTIPLY);
            break;
        case NEON_TOK_SLASH:
            neon_prs_emit1byte(prs, NEON_OP_PRIMDIVIDE);
            break;
        case NEON_TOK_MODULO:
            neon_prs_emit1byte(prs, NEON_OP_PRIMMODULO);
            break;
        case NEON_TOK_BINAND:
            neon_prs_emit1byte(prs, NEON_OP_PRIMBINAND);
            break;
        case NEON_TOK_BINOR:
            neon_prs_emit1byte(prs, NEON_OP_PRIMBINOR);
            break;
        case NEON_TOK_BINXOR:
            neon_prs_emit1byte(prs, NEON_OP_PRIMBINXOR);
            break;
        case NEON_TOK_SHIFTLEFT:
            neon_prs_emit1byte(prs, NEON_OP_PRIMSHIFTLEFT);
            break;
        case NEON_TOK_SHIFTRIGHT:
            neon_prs_emit1byte(prs, NEON_OP_PRIMSHIFTRIGHT);
            break;
        default:
            return;// Unreachable.
    }
}

void neon_prs_rulecall(NeonAstParser* prs, bool canassign)
{
    int32_t argc;
    (void)canassign;
    argc = neon_prs_parsearglist(prs);
    neon_prs_emit2byte(prs, NEON_OP_CALL, argc);
}

void neon_prs_ruledot(NeonAstParser* prs, bool canassign)
{
    int32_t name;
    int32_t argc;
    (void)canassign;
    neon_prs_consume(prs, NEON_TOK_IDENTIFIER, "expect property name after '.'.");
    name = neon_prs_makeidentconstant(prs, &prs->previous);
    if(canassign && neon_prs_match(prs, NEON_TOK_ASSIGN))
    {
        neon_prs_parseexpr(prs);
        neon_prs_emit2byte(prs, NEON_OP_PROPERTYSET, name);
    }
    else if(neon_prs_match(prs, NEON_TOK_PARENOPEN))
    {
        argc = neon_prs_parsearglist(prs);
        neon_prs_emit2byte(prs, NEON_OP_INSTTHISINVOKE, name);
        neon_prs_emit1byte(prs, argc);
    }
    else
    {
        neon_prs_emit2byte(prs, NEON_OP_PROPERTYGET, name);
    }
}

void neon_prs_ruleliteral(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    switch(prs->previous.type)
    {
        case NEON_TOK_KWFALSE:
            neon_prs_emit1byte(prs, NEON_OP_PUSHFALSE);
            break;
        case NEON_TOK_KWNIL:
            neon_prs_emit1byte(prs, NEON_OP_PUSHNIL);
            break;
        case NEON_TOK_KWTRUE:
            neon_prs_emit1byte(prs, NEON_OP_PUSHTRUE);
            break;
        default:
            return;// Unreachable.
    }
}

void neon_prs_rulegrouping(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    neon_prs_parseexpr(prs);
    neon_prs_consume(prs, NEON_TOK_PARENCLOSE, "expect ')' after expression");
}

void neon_prs_rulenumber(NeonAstParser* prs, bool canassign)
{
    double value;
    (void)canassign;
    value = strtod(prs->previous.start, NULL);
    neon_prs_emitconstant(prs, neon_value_makenumber(value));
}

void neon_prs_ruleor(NeonAstParser* prs, bool canassign)
{
    int endjump;
    int elsejump;
    (void)canassign;
    elsejump = neon_prs_emitjump(prs, NEON_OP_JUMPIFFALSE);
    endjump = neon_prs_emitjump(prs, NEON_OP_JUMPNOW);
    neon_prs_emitpatchjump(prs, elsejump);
    neon_prs_emit1byte(prs, NEON_OP_POP);
    neon_prs_parseprec(prs, NEON_PREC_OR);
    neon_prs_emitpatchjump(prs, endjump);
}

#define stringesc1(c, rpl1) \
    case c: \
        { \
            buf[pi] = rpl1; \
            pi += 1; \
            i += 1; \
        } \
        break;

void neon_prs_rulestring(NeonAstParser* prs, bool canassign)
{
    size_t i;
    size_t pi;
    size_t rawlen;
    size_t nlen;
    int currc;
    int nextc;
    bool needalloc;
    char* buf;
    const char* rawstr;
    NeonObjString* os;
    (void)canassign;
    needalloc = false;
    rawstr = prs->previous.start + 1;
    rawlen = prs->previous.length - 2;
    // first, iterate, try to figure out how much more to allocate...
    nlen = rawlen;
    for(i=0; i<rawlen; i++)
    {
        currc = rawstr[i];
        if(currc == '\\')
        {
            needalloc = true;
            nlen += 1;
        }
    }
    if(needalloc)
    {
        buf = (char*)malloc(nlen + 1);
        memset(buf, 0, nlen+1);
        pi = 0;
        i = 0;
        while(i<rawlen)
        {
            currc = rawstr[i];
            nextc = -1;
            if((i + 1) < rawlen)
            {
                nextc = rawstr[i + 1];
            }
            if(currc == '\\')
            {
                if(nextc == '\\')
                {
                    buf[pi+0] = '\\';
                    buf[pi+1] = '\\';
                    pi += 2;
                }
                else
                {
                    switch(nextc)
                    {
                        stringesc1('0', '\0');
                        stringesc1('1', '\1');
                        stringesc1('2', '\2');
                        stringesc1('n', '\n');
                        stringesc1('t', '\t');
                        stringesc1('r', '\r');
                        stringesc1('e', '\e');
                        default:
                            {
                                neon_prs_raiseerror(prs, "unknown string escape character '%c'", nextc);
                            }
                            break;
                    }
                    #undef stringesc1
                }
            }
            else
            {
                buf[pi] = currc;
                pi += 1;
            }
            i++;
        }
        os = neon_string_take(prs->pvm, buf, pi);
    }
    else
    {
        os = neon_string_copy(prs->pvm, rawstr, rawlen);
    }
    neon_prs_emitconstant(prs, neon_value_makeobject(os));

}


void neon_prs_parsenamedvar(NeonAstParser* prs, NeonAstToken name, bool canassign)
{
    int32_t getop;
    int32_t setop;
    int arg;
    (void)canassign;
    arg = neon_prs_resolvelocal(prs, prs->currcompiler, &name);
    if(arg != -1)
    {
        getop = NEON_OP_LOCALGET;
        setop = NEON_OP_LOCALSET;
    }
    else if((arg = neon_prs_resolveupval(prs, prs->currcompiler, &name)) != -1)
    {
        getop = NEON_OP_UPVALGET;
        setop = NEON_OP_UPVALSET;
    }
    else
    {
        arg = neon_prs_makeidentconstant(prs, &name);
        getop = NEON_OP_GLOBALGET;
        setop = NEON_OP_GLOBALSET;
    }
    return neon_prs_doassign(prs, getop, setop, arg, canassign);
}

void neon_prs_rulevariable(NeonAstParser* prs, bool canassign)
{
    neon_prs_parsenamedvar(prs, prs->previous, canassign);
}

NeonAstToken neon_prs_makesyntoken(NeonAstParser* prs, const char* text)
{
    NeonAstToken token;
    (void)prs;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}

void neon_prs_rulesuper(NeonAstParser* prs, bool canassign)
{
    int32_t name;
    int32_t argc;
    (void)canassign;
    if(prs->currclass == NULL)
    {
        neon_prs_raiseerror(prs, "cannot use 'super' outside of a class");
    }
    else if(!prs->currclass->hassuperclass)
    {
        neon_prs_raiseerror(prs, "cannot use 'super' in a class with no superclass");
    }
    neon_prs_consume(prs, NEON_TOK_DOT, "expected '.' after 'super'");
    neon_prs_consume(prs, NEON_TOK_IDENTIFIER, "expected superclass method name");
    name = neon_prs_makeidentconstant(prs, &prs->previous);
    neon_prs_parsenamedvar(prs, neon_prs_makesyntoken(prs, "this"), false);
    /* Superclasses super-get < Superclasses super-invoke */
    /*
    neon_prs_parsenamedvar(prs, neon_prs_makesyntoken(prs, "super"), false);
    neon_prs_emit2byte(prs, NEON_OP_INSTGETSUPER, name);
    */
    if(neon_prs_match(prs, NEON_TOK_PARENOPEN))
    {
        argc = neon_prs_parsearglist(prs);
        neon_prs_parsenamedvar(prs, neon_prs_makesyntoken(prs, "super"), false);
        neon_prs_emit2byte(prs, NEON_OP_INSTSUPERINVOKE, name);
        neon_prs_emit1byte(prs, argc);
    }
    else
    {
        neon_prs_parsenamedvar(prs, neon_prs_makesyntoken(prs, "super"), false);
        neon_prs_emit2byte(prs, NEON_OP_INSTGETSUPER, name);
    }
}

void neon_prs_rulethis(NeonAstParser* prs, bool canassign)
{
    (void)canassign;
    if(prs->currclass == NULL)
    {
        neon_prs_raiseerror(prs, "can't use 'this' outside of a class.");
        return;
    }
    neon_prs_rulevariable(prs, false);
}

void neon_prs_ruleunary(NeonAstParser* prs, bool canassign)
{
    NeonAstTokType ot;
    (void)canassign;
    ot = prs->previous.type;
    // Compile the operand.
    /* Compiling Expressions unary < Compiling Expressions unary-operand */
    //neon_prs_parseexpr(prs);
    neon_prs_parseprec(prs, NEON_PREC_UNARY);
    // Emit the operator instruction.
    switch(ot)
    {
        case NEON_TOK_EXCLAM:
            neon_prs_emit1byte(prs, NEON_OP_PRIMNOT);
            break;
        case NEON_TOK_MINUS:
            neon_prs_emit1byte(prs, NEON_OP_PRIMNEGATE);
            break;
        default:
            return;// Unreachable.
    }
}


void neon_prs_doassign(NeonAstParser* prs, int32_t getop, int32_t setop, int arg, bool canassign)
{

    if(canassign && neon_prs_match(prs, NEON_TOK_ASSIGN))
    {
        neon_prs_parseexpr(prs);
        neon_prs_emit2byte(prs, setop, (int32_t)arg);
    }
    else if(canassign && neon_prs_match(prs, NEON_TOK_INCREMENT))
    {
        if(getop == NEON_OP_PROPERTYGET /*|| getop == NEON_OP_GETPROPERTYGETTHIS*/)
        {
            neon_prs_emit1byte(prs, NEON_OP_DUP);
        }
        if(arg != -1)
        {
            neon_prs_emit2byte(prs, getop, arg);
        }
        else
        {
            neon_prs_emit2byte(prs, getop, 1);

        }
        neon_prs_emit2byte(prs, NEON_OP_PUSHONE, NEON_OP_PRIMADD);
        neon_prs_emit2byte(prs, setop, arg);
    }
    else if(canassign && neon_prs_match(prs, NEON_TOK_DECREMENT))
    {
        if(arg != -1)
        {
            neon_prs_emit2byte(prs, getop, arg);
        }
        else
        {
            neon_prs_emit2byte(prs, getop, 1);
        }
        neon_prs_emit2byte(prs, NEON_OP_PUSHONE, NEON_OP_PRIMSUBTRACT);
        neon_prs_emit2byte(prs, setop, arg);
    }
    else
    {
        if(arg != -1)
        {
            if(getop == NEON_OP_INDEXGET)
            {
                neon_prs_emit2byte(prs, getop, (int32_t)canassign);
            }
            else
            {
                neon_prs_emit2byte(prs, getop, arg);
            }
        }
        else
        {
            neon_prs_emit2byte(prs, getop, (int32_t)arg);
        }
    }
}

void neon_prs_rulearray(NeonAstParser* prs, bool canassign)
{
    int count;
    (void)canassign;
    count = 0;
    if(!neon_prs_checkcurrent(prs, NEON_TOK_BRACKETCLOSE))
    {
        do
        {
            neon_prs_parseexpr(prs);
            count++;
        } while(neon_prs_match(prs, NEON_TOK_COMMA));
    }
    neon_prs_consume(prs, NEON_TOK_BRACKETCLOSE, "expecteded ']' at end of list literal");
    neon_prs_emit2byte(prs, NEON_OP_MAKEARRAY, count);
}

void neon_prs_ruleindex(NeonAstParser* prs, bool canassign)
{
    bool willassign;
    (void)willassign;
    willassign = false;
    neon_prs_parseexpr(prs);
    neon_prs_consume(prs, NEON_TOK_BRACKETCLOSE, "expecteded ']' after indexing");
    if(neon_prs_checkcurrent(prs, NEON_TOK_ASSIGN))
    {
        willassign = true;
    }
    neon_prs_doassign(prs, NEON_OP_INDEXGET, NEON_OP_INDEXSET, -1, canassign);
}

void neon_prs_rulemap(NeonAstParser* prs, bool canassign)
{
    int count;
    (void)canassign;
    count = 0;
    neon_prs_consume(prs, NEON_TOK_BRACECLOSE, "expected '}' at end of map literal");
    neon_prs_emit2byte(prs, NEON_OP_MAKEMAP, count);
}

NeonAstRule* neon_prs_setrule(NeonAstRule* rule, NeonAstParseFN prefix, NeonAstParseFN infix, NeonAstPrecedence precedence)
{
    rule->prefix = prefix;
    rule->infix = infix;
    rule->precedence = precedence;
    return rule;
}

NeonAstRule* neon_prs_getrule(NeonAstTokType type)
{
    static NeonAstRule dest;
    switch(type)
    {
        /* Compiling Expressions rules < Calls and Functions infix-left-paren */
        // [NEON_TOK_PARENOPEN]    = {grouping, NULL,   NEON_PREC_NONE},
        case NEON_TOK_PARENOPEN:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulegrouping, neon_prs_rulecall, NEON_PREC_CALL );
            }
            break;
        case NEON_TOK_PARENCLOSE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_BRACEOPEN:
            {
                return neon_prs_setrule(&dest, neon_prs_rulemap, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_BRACECLOSE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_BRACKETOPEN:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulearray, neon_prs_ruleindex, NEON_PREC_CALL );
            }
            break;
        case NEON_TOK_BRACKETCLOSE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_COMMA:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Classes and Instances table-dot */
        // [NEON_TOK_DOT]           = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_DOT:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_ruledot, NEON_PREC_CALL );
            }
            break;
        case NEON_TOK_MINUS:
            {
                return neon_prs_setrule(&dest,  neon_prs_ruleunary, neon_prs_rulebinary, NEON_PREC_TERM );
            }
            break;
        case NEON_TOK_PLUS:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_TERM );
            }
            break;
        case NEON_TOK_SEMICOLON:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_NEWLINE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_SLASH:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_STAR:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_MODULO:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_BINAND:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_BINOR:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_BINXOR:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_FACTOR );
            }
            break;
        case NEON_TOK_SHIFTLEFT:
            {
                return neon_prs_setrule(&dest, NULL, neon_prs_rulebinary, NEON_PREC_SHIFT);
            }
            break;
        case NEON_TOK_SHIFTRIGHT:
            {
                return neon_prs_setrule(&dest, NULL, neon_prs_rulebinary, NEON_PREC_SHIFT);
            }
            break;
        case NEON_TOK_INCREMENT:
            {
                return neon_prs_setrule(&dest, NULL, NULL, NEON_PREC_NONE);
            }
            break;
        case NEON_TOK_DECREMENT:
            {
                return neon_prs_setrule(&dest, NULL, NULL, NEON_PREC_NONE);
            }
            break;
        /* Compiling Expressions rules < Types of Values table-not */
        // [NEON_TOK_EXCLAM]          = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_EXCLAM:
            {
                return neon_prs_setrule(&dest,  neon_prs_ruleunary, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Types of Values table-equal */
        // [NEON_TOK_COMPNOTEQUAL]    = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_COMPNOTEQUAL:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_EQUALITY );
            }
            break;
        case NEON_TOK_ASSIGN:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Types of Values table-comparisons */
        // [NEON_TOK_COMPEQUAL]   = {NULL,     NULL,   NEON_PREC_NONE},
        // [NEON_TOK_COMPGREATERTHAN]       = {NULL,     NULL,   NEON_PREC_NONE},
        // [NEON_TOK_COMPGREATEREQUAL] = {NULL,     NULL,   NEON_PREC_NONE},
        // [NEON_TOK_COMPLESSTHAN]          = {NULL,     NULL,   NEON_PREC_NONE},
        // [NEON_TOK_COMPLESSEQUAL]    = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_COMPEQUAL:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_EQUALITY );
            }
            break;
        case NEON_TOK_COMPGREATERTHAN:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_COMPARISON );
            }
            break;
        case NEON_TOK_COMPGREATEREQUAL:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_COMPARISON );
            }
            break;
        case NEON_TOK_COMPLESSTHAN:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_COMPARISON );
            }
            break;
        case NEON_TOK_COMPLESSEQUAL:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_rulebinary, NEON_PREC_COMPARISON );
            }
            break;
        /* Compiling Expressions rules < Global Variables table-identifier */
        // [NEON_TOK_IDENTIFIER]    = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_IDENTIFIER:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulevariable, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Strings table-string */
        // [NEON_TOK_STRING]        = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_STRING:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulestring, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_NUMBER:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulenumber, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Jumping Back and Forth table-and */
        // [NEON_TOK_KWAND]           = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_KWBREAK:
            {
                return neon_prs_setrule(&dest, NULL, NULL, NEON_PREC_NONE);
            }
            break;
        case NEON_TOK_KWCONTINUE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWAND:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_ruleand, NEON_PREC_AND );
            }
            break;
        case NEON_TOK_KWCLASS:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWELSE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Types of Values table-false */
        // [NEON_TOK_KWFALSE]         = {NULL,     NULL,   NEON_PREC_NONE},
        case NEON_TOK_KWFALSE:
            {
                return neon_prs_setrule(&dest,  neon_prs_ruleliteral, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWFOR:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWFUNCTION:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWIF:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Types of Values table-nil
        * [NEON_TOK_KWNIL]           = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWNIL:
            {
                return neon_prs_setrule(&dest,  neon_prs_ruleliteral, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Jumping Back and Forth table-or
        * [NEON_TOK_KWOR]            = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWOR:
            {
                return neon_prs_setrule(&dest,  NULL, neon_prs_ruleor, NEON_PREC_OR );
            }
            break;
        case NEON_TOK_KWDEBUGPRINT:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWRETURN:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Superclasses table-super
        * [NEON_TOK_KWSUPER]         = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWSUPER:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulesuper, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Methods and Initializers table-this
        * [NEON_TOK_KWTHIS]          = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWTHIS:
            {
                return neon_prs_setrule(&dest,  neon_prs_rulethis, NULL, NEON_PREC_NONE );
            }
            break;
        /* Compiling Expressions rules < Types of Values table-true
        * [NEON_TOK_KWTRUE]          = {NULL,     NULL,   NEON_PREC_NONE},
        */
        case NEON_TOK_KWTRUE:
            {
                return neon_prs_setrule(&dest,  neon_prs_ruleliteral, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWVAR:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_KWWHILE:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_ERROR:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
        case NEON_TOK_EOF:
            {
                return neon_prs_setrule(&dest,  NULL, NULL, NEON_PREC_NONE );
            }
            break;
    }
    return NULL;
}

void neon_prs_parseprec(NeonAstParser* prs, NeonAstPrecedence precedence)
{
    bool canassign;
    NeonAstParseFN infixrule;
    NeonAstParseFN prefixrule;
    neon_prs_advance(prs);
    prefixrule = neon_prs_getrule(prs->previous.type)->prefix;
    if(prefixrule == NULL)
    {
        neon_prs_raiseerror(prs, "expected expression");
        return;
    }
    canassign = precedence <= NEON_PREC_ASSIGNMENT;
    prefixrule(prs, canassign);
    while(precedence <= neon_prs_getrule(prs->current.type)->precedence)
    {
        neon_prs_advance(prs);
        infixrule = neon_prs_getrule(prs->previous.type)->infix;
        /* Compiling Expressions infix < Global Variables infix-rule */
        //infixrule();
        infixrule(prs, canassign);
    }
    if(canassign && neon_prs_match(prs, NEON_TOK_ASSIGN))
    {
        neon_prs_raiseerror(prs, "invalid assignment target");
    }
}

void neon_prs_parseexpr(NeonAstParser* prs)
{
    /* Compiling Expressions expression < Compiling Expressions expression-body
    // What goes here?
    */
    neon_prs_parseprec(prs, NEON_PREC_ASSIGNMENT);
}

void neon_prs_parseblock(NeonAstParser* prs)
{
    while(!neon_prs_checkcurrent(prs, NEON_TOK_BRACECLOSE) && !neon_prs_checkcurrent(prs, NEON_TOK_EOF))
    {
        neon_prs_parsedecl(prs);
    }
    neon_prs_consume(prs, NEON_TOK_BRACECLOSE, "expected '}' after block");
}

void neon_prs_parsefunction(NeonAstParser* prs, NeonAstFuncType type)
{
    int i;
    int32_t constant;
    NeonObjScriptFunction* fn;
    NeonAstCompiler compiler;
    neon_prs_compilerinit(prs, &compiler, type);
    neon_prs_scopebegin(prs);// [no-end-scope]
    neon_prs_consume(prs, NEON_TOK_PARENOPEN, "expected '(' after function name");
    if(!neon_prs_checkcurrent(prs, NEON_TOK_PARENCLOSE))
    {
        do
        {
            prs->currcompiler->compiledfn->arity++;
            if(prs->currcompiler->compiledfn->arity > 255)
            {
                neon_prs_raiseatcurrent(prs, "function has too many arguments declared");
            }
            constant = neon_prs_parsevarname(prs, "expected parameter name");
            neon_prs_emitdefvar(prs, constant);
        } while(neon_prs_match(prs, NEON_TOK_COMMA));
    }
    neon_prs_consume(prs, NEON_TOK_PARENCLOSE, "expected ')' after parameters");
    neon_prs_consume(prs, NEON_TOK_BRACEOPEN, "expected '{' before function body");
    neon_prs_parseblock(prs);
    fn = neon_prs_compilerfinish(prs, false);
    /* Calls and Functions compile-function < Closures emit-closure */
    // neon_prs_emit2byte(prs, NEON_OP_PUSHCONST, neon_prs_makeconstant(prs, neon_value_makeobject(fn)));
    neon_prs_emit2byte(prs, NEON_OP_CLOSURE, neon_prs_makeconstant(prs, neon_value_makeobject(fn)));
    for(i = 0; i < fn->upvaluecount; i++)
    {
        neon_prs_emit1byte(prs, compiler.upvalues[i].islocal ? 1 : 0);
        neon_prs_emit1byte(prs, compiler.upvalues[i].index);
    }
}

void neon_prs_parsemethod(NeonAstParser* prs)
{
    int32_t constant;
    NeonAstFuncType type;
    neon_prs_consume(prs, NEON_TOK_IDENTIFIER, "expect method name.");
    constant = neon_prs_makeidentconstant(prs, &prs->previous);

    /* Methods and Initializers method-body < Methods and Initializers method-type */
    //type = NEON_TYPE_FUNCTION;
    type = NEON_TYPE_METHOD;
    if(prs->previous.length == 4 && memcmp(prs->previous.start, "init", 4) == 0)
    {
        type = NEON_TYPE_INITIALIZER;
    }
    neon_prs_parsefunction(prs, type);
    neon_prs_emit2byte(prs, NEON_OP_METHOD, constant);
}

void neon_prs_parseclassdecl(NeonAstParser* prs)
{
    int32_t nameconstant;
    NeonAstToken classname;
    NeonAstClassCompiler classcompiler;
    neon_prs_consume(prs, NEON_TOK_IDENTIFIER, "expect class name.");
    classname = prs->previous;
    nameconstant = neon_prs_makeidentconstant(prs, &prs->previous);
    neon_prs_parsevarident(prs);
    neon_prs_emit2byte(prs, NEON_OP_CLASS, nameconstant);
    neon_prs_emitdefvar(prs, nameconstant);
    classcompiler.hassuperclass = false;
    classcompiler.enclosing = prs->currclass;
    prs->currclass = &classcompiler;
    if(neon_prs_match(prs, NEON_TOK_COMPLESSTHAN))
    {
        neon_prs_consume(prs, NEON_TOK_IDENTIFIER, "expect superclass name");
        neon_prs_rulevariable(prs, false);
        if(neon_prs_identsequal(&classname, &prs->previous))
        {
            neon_prs_raiseerror(prs, "a class cannot inherit from itself");
        }
        neon_prs_scopebegin(prs);
        neon_prs_addlocal(prs, neon_prs_makesyntoken(prs, "super"));
        neon_prs_emitdefvar(prs, 0);
        neon_prs_parsenamedvar(prs, classname, false);
        neon_prs_emit1byte(prs, NEON_OP_INHERIT);
        classcompiler.hassuperclass = true;
    }
    neon_prs_parsenamedvar(prs, classname, false);
    neon_prs_consume(prs, NEON_TOK_BRACEOPEN, "expected '{' before class body");
    while(!neon_prs_checkcurrent(prs, NEON_TOK_BRACECLOSE) && !neon_prs_checkcurrent(prs, NEON_TOK_EOF))
    {
        neon_prs_parsemethod(prs);
    }
    neon_prs_consume(prs, NEON_TOK_BRACECLOSE, "expected '}' after class body");
    neon_prs_emit1byte(prs, NEON_OP_POP);
    if(classcompiler.hassuperclass)
    {
        neon_prs_scopeend(prs);
    }
    prs->currclass = prs->currclass->enclosing;
}

void neon_prs_parsefuncdecl(NeonAstParser* prs)
{
    int32_t global;
    global = neon_prs_parsevarname(prs, "expected function name");
    neon_prs_markinit(prs);
    neon_prs_parsefunction(prs, NEON_TYPE_FUNCTION);
    neon_prs_emitdefvar(prs, global);
}

void neon_prs_parsevardecl(NeonAstParser* prs)
{
    int32_t global;
    global = neon_prs_parsevarname(prs, "expected variable name");
    if(neon_prs_match(prs, NEON_TOK_ASSIGN))
    {
        neon_prs_parseexpr(prs);
    }
    else
    {
        neon_prs_emit1byte(prs, NEON_OP_PUSHNIL);
    }
    neon_prs_skipsemicolon(prs);
    neon_prs_emitdefvar(prs, global);
}

void neon_prs_parseexprstmt(NeonAstParser* prs)
{
    neon_prs_parseexpr(prs);
    neon_prs_skipsemicolon(prs);
    neon_prs_emit1byte(prs, NEON_OP_POP);
}

void neon_prs_parseforstmt(NeonAstParser* prs)
{
    int loopstart;
    int exitjump;
    int bodyjump;
    int incrementstart;
    NeonAstLoop loop;
    neon_prs_scopebegin(prs);
    neon_prs_startloop(prs, &loop);
    neon_prs_consume(prs, NEON_TOK_PARENOPEN, "expected '(' after 'for'");
    if(neon_prs_match(prs, NEON_TOK_SEMICOLON))
    {
        // No initializer.
    }
    else if(neon_prs_match(prs, NEON_TOK_KWVAR))
    {
        neon_prs_parsevardecl(prs);
    }
    else
    {
        neon_prs_parseexprstmt(prs);
    }
    loopstart = neon_prs_currentchunk(prs)->count;
    exitjump = -1;
    if(!neon_prs_match(prs, NEON_TOK_SEMICOLON))
    {
        neon_prs_parseexpr(prs);
        neon_prs_consume(prs, NEON_TOK_SEMICOLON, "expected ';' after 'for' loop condition");
        // Jump out of the loop if the condition is false.
        exitjump = neon_prs_emitjump(prs, NEON_OP_JUMPIFFALSE);
        neon_prs_emit1byte(prs, NEON_OP_POP);// Condition
    }
    if(!neon_prs_match(prs, NEON_TOK_PARENCLOSE))
    {
        bodyjump = neon_prs_emitjump(prs, NEON_OP_JUMPNOW);
        incrementstart = neon_prs_currentchunk(prs)->count;
        // when we 'continue' in for loop, we want to jump here
        prs->currcompiler->loop->start = incrementstart;
        neon_prs_parseexpr(prs);
        neon_prs_emit1byte(prs, NEON_OP_POP);
        neon_prs_consume(prs, NEON_TOK_PARENCLOSE, "expected ')' after 'for' clauses");
        neon_prs_emitloop(prs, loopstart);
        loopstart = incrementstart;
        neon_prs_emitpatchjump(prs, bodyjump);
    }
    prs->currcompiler->loop->body = neon_prs_currentchunk(prs)->count;
    neon_prs_parsestmt(prs);
    neon_prs_emitloop(prs, loopstart);
    if(exitjump != -1)
    {
        neon_prs_emitpatchjump(prs, exitjump);
        neon_prs_emit1byte(prs, NEON_OP_POP);
    }
    neon_prs_endloop(prs);
    neon_prs_scopeend(prs);
}

void neon_prs_parsewhilestmt(NeonAstParser* prs)
{
    int loopstart;
    int exitjump;
    NeonAstLoop loop;
    neon_prs_startloop(prs, &loop);
    loopstart = neon_prs_currentchunk(prs)->count;
    neon_prs_consume(prs, NEON_TOK_PARENOPEN, "expected '(' after 'while'");
    neon_prs_parseexpr(prs);
    neon_prs_consume(prs, NEON_TOK_PARENCLOSE, "expected ')' after condition");
    exitjump = neon_prs_emitjump(prs, NEON_OP_JUMPIFFALSE);
    neon_prs_emit1byte(prs, NEON_OP_POP);
    prs->currcompiler->loop->body = neon_prs_currentchunk(prs)->count;
    neon_prs_parsestmt(prs);
    neon_prs_emitloop(prs, loopstart);
    neon_prs_emitpatchjump(prs, exitjump);
    neon_prs_emit1byte(prs, NEON_OP_POP);
    neon_prs_endloop(prs);
}

void neon_prs_parsebreakstmt(NeonAstParser* prs)
{
    if(prs->currcompiler->loop == NULL)
    {
        neon_prs_raiseerror(prs, "cannot use 'break' outside of a loop");
        return;
    }
    neon_prs_skipsemicolon(prs);
    neon_prs_discardlocals(prs, prs->currcompiler);
    neon_prs_emitjump(prs, NEON_OP_PSEUDOBREAK);
}

void neon_prs_parsecontinuestmt(NeonAstParser* prs)
{
    if(prs->currcompiler->loop == NULL)
    {
        neon_prs_raiseerror(prs, "cannot use 'continue' outside of a loop");
        return;
    }
    neon_prs_skipsemicolon(prs);
    neon_prs_discardlocals(prs, prs->currcompiler);
    neon_prs_emitloop(prs, prs->currcompiler->loop->start);
}

void neon_prs_parseifstmt(NeonAstParser* prs)
{
    int thenjump;
    int elsejump;
    neon_prs_consume(prs, NEON_TOK_PARENOPEN, "expected '(' after 'if'");
    neon_prs_parseexpr(prs);
    neon_prs_consume(prs, NEON_TOK_PARENCLOSE, "expect ')' after condition");// [paren]
    thenjump = neon_prs_emitjump(prs, NEON_OP_JUMPIFFALSE);
    neon_prs_emit1byte(prs, NEON_OP_POP);
    neon_prs_parsestmt(prs);
    elsejump = neon_prs_emitjump(prs, NEON_OP_JUMPNOW);
    neon_prs_emitpatchjump(prs, thenjump);
    neon_prs_emit1byte(prs, NEON_OP_POP);
    if(neon_prs_match(prs, NEON_TOK_KWELSE))
    {
        neon_prs_parsestmt(prs);
    }
    neon_prs_emitpatchjump(prs, elsejump);
}

void neon_prs_parseprintstmt(NeonAstParser* prs)
{
    neon_prs_parseexpr(prs);
    //neon_prs_consume(prs, NEON_TOK_SEMICOLON, "expect ';' after value.");
    neon_prs_skipsemicolon(prs);
    neon_prs_emit1byte(prs, NEON_OP_DEBUGPRINT);
}

void neon_prs_parsereturnstmt(NeonAstParser* prs)
{
    if(prs->currcompiler->type == NEON_TYPE_SCRIPT)
    {
        neon_prs_raiseerror(prs, "cannot return from top-level code");
    }
    if(neon_prs_match(prs, NEON_TOK_SEMICOLON) || neon_prs_match(prs, NEON_TOK_NEWLINE))
    {
        neon_prs_emitreturn(prs);
    }
    else
    {
        if(prs->currcompiler->type == NEON_TYPE_INITIALIZER)
        {
            neon_prs_raiseerror(prs, "cannot return a value from an initializer");
        }
        neon_prs_parseexpr(prs);
        //neon_prs_consume(prs, NEON_TOK_SEMICOLON, "expect ';' after return value.");
        neon_prs_skipsemicolon(prs);
        neon_prs_emit1byte(prs, NEON_OP_RETURN);
    }
}

void neon_prs_synchronize(NeonAstParser* prs)
{
    prs->panicmode = false;
    while(prs->current.type != NEON_TOK_EOF)
    {
        if(prs->previous.type == NEON_TOK_SEMICOLON)
        {
            return;
        }
        switch(prs->current.type)
        {
            case NEON_TOK_KWBREAK:
            case NEON_TOK_KWCONTINUE:
            case NEON_TOK_KWCLASS:
            case NEON_TOK_KWFUNCTION:
            case NEON_TOK_KWVAR:
            case NEON_TOK_KWFOR:
            case NEON_TOK_KWIF:
            case NEON_TOK_KWWHILE:
            case NEON_TOK_KWDEBUGPRINT:
            case NEON_TOK_KWRETURN:
                return;
            default:
                // Do nothing.
                break;
        }
        neon_prs_advance(prs);
    }
}

void neon_prs_parsedecl(NeonAstParser* prs)
{
    if(neon_prs_match(prs, NEON_TOK_KWCLASS))
    {
        neon_prs_parseclassdecl(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWFUNCTION))
    {
        neon_prs_parsefuncdecl(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWVAR))
    {
        neon_prs_parsevardecl(prs);
    }
    else
    {
        neon_prs_parsestmt(prs);
    }
    /* Global Variables declaration < Global Variables match-var */
    // neon_prs_parsestmt(prs);
    if(prs->panicmode)
    {
        neon_prs_synchronize(prs);
    }
}

void neon_prs_parsestmt(NeonAstParser* prs)
{
    if(neon_prs_match(prs, NEON_TOK_KWDEBUGPRINT))
    {
        neon_prs_parseprintstmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWBREAK))
    {
        neon_prs_parsebreakstmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWCONTINUE))
    {
        neon_prs_parsecontinuestmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWFOR))
    {
        neon_prs_parseforstmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWIF))
    {
        neon_prs_parseifstmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWRETURN))
    {
        neon_prs_parsereturnstmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_KWWHILE))
    {
        neon_prs_parsewhilestmt(prs);
    }
    else if(neon_prs_match(prs, NEON_TOK_BRACEOPEN))
    {
        neon_prs_scopebegin(prs);
        neon_prs_parseblock(prs);
        neon_prs_scopeend(prs);
    }
    else
    {
        neon_prs_parseexprstmt(prs);
    }
}

NeonObjScriptFunction* neon_prs_compilesource(NeonState* state, const char* source)
{
    NeonAstScanner scn;
    NeonAstParser parser;
    NeonAstCompiler compiler;
    NeonObjScriptFunction* fn;
    neon_lex_init(state, &scn, source);
    neon_prs_init(state, &parser, &scn);
    neon_prs_compilerinit(&parser, &compiler, NEON_TYPE_SCRIPT);
    parser.haderror = false;
    parser.panicmode = false;
    neon_prs_advance(&parser);
    while(!neon_prs_match(&parser, NEON_TOK_EOF))
    {
        neon_prs_parsedecl(&parser);
    }
    fn = neon_prs_compilerfinish(&parser, true);
    if(parser.haderror)
    {
        return NULL;
    }
    return fn;
}

void neon_prs_markcompilerroots(NeonState* state)
{
    NeonAstCompiler* compiler;
    if(state->parser != NULL)
    {
        compiler = state->parser->currcompiler;
        while(compiler != NULL)
        {
            neon_gcmem_markobject(state, (NeonObject*)compiler->compiledfn);
            compiler = compiler->enclosing;
        }
    }
}

void neon_vm_stackreset(NeonState* state)
{
    state->vmvars.stacktop = 0;
    state->vmvars.framecount = 0;
    state->openupvalues = NULL;
}

void neon_state_vraiseerror(NeonState* state, const char* format, va_list va)
{
    int i;
    size_t instruction;
    NeonCallFrame* frame;
    NeonObjScriptFunction* fn;
    state->vmvars.hasraised = true;
    vfprintf(stderr, format, va);
    fputs("\n", stderr);

    /* Types of Values runtime-error < Calls and Functions runtime-error-temp */
    //size_t instruction = state->ip - state->chunk->code - 1;
    //int line = state->chunk->lines[instruction];

    /* Calls and Functions runtime-error-temp < Calls and Functions runtime-error-stack */
    //NeonCallFrame* frame = &state->vmvars.framevalues[state->vmvars.framecount - 1];
    //size_t instruction = frame->ip - frame->innerfn->chunk.code - 1;
    //int line = frame->innerfn->chunk.lines[instruction];
 
    /* Types of Values runtime-error < Calls and Functions runtime-error-stack */
    //fprintf(stderr, "[line %d] in script\n", line);

    for(i = state->vmvars.framecount - 1; i >= 0; i--)
    {
        frame = &state->vmvars.framevalues[i];
        /* Calls and Functions runtime-error-stack < Closures runtime-error-function */
        // NeonObjScriptFunction* function = frame->innerfn;
        fn = frame->closure->innerfn;
        instruction = frame->ip - fn->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", fn->chunk.lines[instruction]);
        if(fn->name == NULL)
        {
            fprintf(stderr, "script\n");
        }
        else
        {
            fprintf(stderr, "%s()\n", fn->name->sbuf->data);
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

void neon_state_defnative(NeonState* state, const char* name, NeonNativeFN nat)
{
    NeonObjString* os;
    NeonObjNativeFunction* ofn;
    os = neon_string_copy(state, name, (int)strlen(name));
    neon_vm_stackpush(state, neon_value_makeobject(os));
    {
        ofn = neon_object_makenative(state, nat);
        neon_vm_stackpush(state, neon_value_makeobject(ofn));
        {
            neon_hashtable_set(state, &state->globals, neon_value_asstring(state->vmvars.stackvalues[0]), state->vmvars.stackvalues[1]);
        }
        neon_vm_stackpop(state);
    }
    neon_vm_stackpop(state);
}


NeonState* neon_state_init()
{
    NeonState* state;
    state = (NeonState*)malloc(sizeof(NeonState));
    if(state == NULL)
    {
        return NULL;
    }
    memset(state, 0, sizeof(NeonState));
    neon_vm_stackreset(state);
    state->vmvars.hasraised = false;
    state->conf.shouldprintruntime = false;
    state->gcstate.linkedobjects = NULL;
    state->gcstate.bytesallocd = 0;
    state->gcstate.nextgc = 1024 * 1024;
    state->gcstate.graycount = 0;
    state->gcstate.graycap = 0;
    state->gcstate.graystack = NULL;
    state->parser = NULL;
    state->stdoutwriter = neon_writer_makeio(state, stdout, false);
    state->stderrwriter = neon_writer_makeio(state, stderr, false);
    neon_hashtable_init(&state->globals);
    neon_hashtable_init(&state->strings);
    state->initstring = NULL;
    state->initstring = neon_string_copy(state, "init", 4);
    return state;
}

void neon_state_free(NeonState* state)
{
    neon_writer_release(state->stdoutwriter);
    neon_writer_release(state->stderrwriter);
    neon_hashtable_free(state, &state->globals);
    neon_hashtable_free(state, &state->strings);
    state->initstring = NULL;
    neon_vm_gcfreelinkedobjects(state);
    free(state);
}


/* A Virtual Machine run < Calls and Functions run */
static inline int32_t neon_vmbits_readbyte(NeonState* state)
{
    uint32_t r;
    r = *state->vmvars.currframe->ip;
    state->vmvars.currframe->ip++;
    return r;
}

/* Jumping Back and Forth read-short < Calls and Functions run */
static inline uint16_t neon_vmbits_readshort(NeonState* state)
{
    (state)->vmvars.currframe->ip += 2;
    return (uint16_t)((state->vmvars.currframe->ip[-2] << 8) | state->vmvars.currframe->ip[-1]);
}

/* Calls and Functions run < Closures read-constant */
static inline NeonValue neon_vmbits_readconst(NeonState* state)
{
    int32_t b;
    size_t vsz;
    NeonValArray* vaconst;
    (void)vsz;
    if(state->vmvars.currframe->closure == NULL)
    {
        return neon_value_makenil();
    }
    vaconst = &state->vmvars.currframe->closure->innerfn->chunk.constants;
    if(vaconst == NULL)
    {
        return neon_value_makenil();
    }
    vsz = vaconst->size;
    b = neon_vmbits_readbyte(state);
    return (vaconst->values[b]);
}

static inline NeonObjString* neon_vmbits_readstring(NeonState* state)
{
    return neon_value_asstring(neon_vmbits_readconst(state));
}

static inline void neon_vmbits_stackpush(NeonState* state, NeonValue value)
{
    state->vmvars.stackvalues[state->vmvars.stacktop] = value;
    state->vmvars.stacktop++;
}

static inline NeonValue neon_vmbits_stackpop(NeonState* state)
{
    int64_t stop;
    NeonValue v;
    stop = state->vmvars.stacktop;
    state->vmvars.stacktop--;
    v = state->vmvars.stackvalues[stop - 1];
    return v;
}

static inline NeonValue neon_vmbits_stackpopn(NeonState* state, int32_t n)
{
    int64_t stop;
    NeonValue v;
    stop = state->vmvars.stacktop;
    state->vmvars.stacktop -= n;
    v = state->vmvars.stackvalues[stop - n];
    return v;
}

static inline NeonValue neon_vmbits_stackpeek(NeonState* state, int distance)
{
    return state->vmvars.stackvalues[state->vmvars.stacktop + (-1 - distance)];
}

//---------------------

void neon_vm_stackpush(NeonState* state, NeonValue value)
{
    return neon_vmbits_stackpush(state, value);
}

NeonValue neon_vm_stackpop(NeonState* state)
{
    return neon_vmbits_stackpop(state);
}

NeonValue neon_vm_stackpopn(NeonState* state, int32_t n)
{
    return neon_vmbits_stackpopn(state, n);
}

NeonValue neon_vm_stackpeek(NeonState* state, int distance)
{
    return neon_vmbits_stackpeek(state, distance);
}

bool neon_vmbits_callclosure(NeonState* state, NeonObjClosure* closure, int argc)
{
    NeonCallFrame* frame;
    if(argc != closure->innerfn->arity)
    {
        neon_state_raiseerror(state, "expected %d arguments, but got %d", closure->innerfn->arity, argc);
        return false;
    }
    if(state->vmvars.framecount == FRAMES_MAX)
    {
        neon_state_raiseerror(state, "stack overflow");
        return false;
    }
    frame = &state->vmvars.framevalues[state->vmvars.framecount++];
    frame->closure = closure;
    frame->ip = closure->innerfn->chunk.code;
    frame->frstackindex = (state->vmvars.stacktop - argc) - 1;
    return true;
}

bool neon_vmbits_callboundmethod(NeonState* state, NeonValue callee, int argc)
{
    NeonObjBoundFunction* bound;
    bound = neon_value_asboundfunction(callee);
    state->vmvars.stackvalues[state->vmvars.stacktop + (-argc - 1)] = bound->receiver;    
    return neon_vmbits_callclosure(state, bound->method, argc);
}

bool neon_vmbits_callclassconstructor(NeonState* state, NeonValue callee, int argc)
{
    NeonValue initializer;
    NeonObjClass* klass;
    NeonObjInstance* instance;
    klass = neon_value_asclass(callee);
    instance = neon_object_makeinstance(state, klass);
    state->vmvars.stackvalues[state->vmvars.stacktop + (-argc - 1)] = neon_value_makeobject(instance);
    if(neon_hashtable_get(state, &klass->methods, state->initstring, &initializer))
    {
        return neon_vmbits_callclosure(state, neon_value_asclosure(initializer), argc);
    }
    else if(argc != 0)
    {
        neon_state_raiseerror(state, "expected 0 arguments but got %d.", argc);
        return false;
    }
    return true;
}

bool neon_vmbits_callnativefunction(NeonState* state, NeonValue callee, int argc)
{
    NeonValue result;
    NeonValue* vargs;
    NeonNativeFN cfunc;
    NeonObjNativeFunction* nfn;
    nfn = (NeonObjNativeFunction*)neon_value_asobject(callee);
    cfunc = nfn->natfunc;
    vargs = (&state->vmvars.stackvalues[0] + state->vmvars.stacktop) - argc;
    result = cfunc(state, argc, vargs);
    state->vmvars.stacktop -= argc + 1;
    neon_vmbits_stackpush(state, result);
    if(state->vmvars.hasraised)
    {
        return false;
    }
    return true;
}

bool neon_vmbits_callvalue(NeonState* state, NeonValue callee, int argc)
{
    if(neon_value_isobject(callee))
    {
        switch(neon_value_objtype(callee))
        {
            case NEON_OBJ_BOUNDMETHOD:
                {
                    return neon_vmbits_callboundmethod(state, callee, argc);
                }
                break;
            case NEON_OBJ_CLASS:
                {
                    return neon_vmbits_callclassconstructor(state, callee, argc);
                }
                break;
            case NEON_OBJ_CLOSURE:
                {
                    return neon_vmbits_callclosure(state, neon_value_asclosure(callee), argc);
                }
                break;
                /* Calls and Functions call-value < Closures call-value-closure */
                #if 0
            case NEON_OBJ_FUNCTION: // [switch]
                {
                    return neon_vmbits_callclosure(state, neon_value_asscriptfunction(callee), argc);
                }
                break;
                #endif
            case NEON_OBJ_NATIVE:
                {
                    return neon_vmbits_callnativefunction(state, callee, argc);
                }
                break;
            default:
                // Non-callable object type.
                break;
        }
    }
    neon_state_raiseerror(state, "cannot call object type <%s>", neon_value_valuetypename(callee));
    return false;
}

bool neon_vmbits_invokefromclass(NeonState* state, NeonObjClass* klass, NeonObjString* name, int argc)
{
    NeonValue method;
    if(!neon_hashtable_get(state, &klass->methods, name, &method))
    {
        neon_state_raiseerror(state, "undefined property '%s'.", name->sbuf->data);
        return false;
    }
    return neon_vmbits_callclosure(state, neon_value_asclosure(method), argc);
}

bool neon_vmbits_invoke(NeonState* state, NeonObjString* name, int argc)
{
    NeonValue value;
    NeonValue receiver;
    NeonObjInstance* instance;
    receiver = neon_vmbits_stackpeek(state, argc);
    if(!neon_value_isinstance(receiver))
    {
        neon_state_raiseerror(state, "cannot invoke method '%s' on non-instance object type <%s>", name->sbuf->data, receiver);
        return false;
    }
    instance = neon_value_asinstance(receiver);
    if(neon_hashtable_get(state, &instance->fields, name, &value))
    {
        state->vmvars.stackvalues[state->vmvars.stacktop + (-argc - 1)] = value;
        return neon_vmbits_callvalue(state, value, argc);
    }
    return neon_vmbits_invokefromclass(state, instance->klass, name, argc);
}

bool neon_vmbits_bindmethod(NeonState* state, NeonObjClass* klass, NeonObjString* name)
{
    NeonValue method;
    NeonValue peeked;
    NeonObjBoundFunction* bound;
    if(!neon_hashtable_get(state, &klass->methods, name, &method))
    {
        neon_state_raiseerror(state, "cannot bind undefined method '%s'", name->sbuf->data);
        return false;
    }
    peeked = neon_vmbits_stackpeek(state, 0);
    bound = neon_object_makeboundmethod(state, peeked, neon_value_asclosure(method));
    neon_vmbits_stackpop(state);
    neon_vmbits_stackpush(state, neon_value_makeobject(bound));
    return true;
}

NeonObjUpvalue* neon_vmbits_captureupval(NeonState* state, NeonValue* local)
{
    NeonObjUpvalue* upvalue;
    NeonObjUpvalue* prevupvalue;
    NeonObjUpvalue* createdupvalue;
    prevupvalue = NULL;
    upvalue = state->openupvalues;
    while(upvalue != NULL && upvalue->location > local)
    {
        prevupvalue = upvalue;
        upvalue = upvalue->next;
    }
    if(upvalue != NULL && upvalue->location == local)
    {
        return upvalue;
    }
    createdupvalue = neon_object_makeupvalue(state, local);
    createdupvalue->next = upvalue;
    if(prevupvalue == NULL)
    {
        state->openupvalues = createdupvalue;
    }
    else
    {
        prevupvalue->next = createdupvalue;
    }
    return createdupvalue;
}

void neon_vmbits_closeupvals(NeonState* state, NeonValue* last)
{
    NeonObjUpvalue* upvalue;
    while(state->openupvalues != NULL && state->openupvalues->location >= last)
    {
        upvalue = state->openupvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        state->openupvalues = upvalue->next;
    }
}

void neon_vmbits_defmethod(NeonState* state, NeonObjString* name)
{
    NeonValue method;
    NeonObjClass* klass;
    method = neon_vmbits_stackpeek(state, 0);
    klass = neon_value_asclass(neon_vmbits_stackpeek(state, 1));
    neon_hashtable_set(state, &klass->methods, name, method);
    neon_vmbits_stackpop(state);
}

static inline bool neon_vmexec_concat(NeonState* state)
{
    NeonValue peeka;
    NeonValue peekb;
    NeonObjString* b;
    NeonObjString* a;
    NeonObjString* result;
    NeonStringBuffer* sb;
    peekb = neon_vmbits_stackpeek(state, 0);
    peeka = neon_vmbits_stackpeek(state, 1);
    b = neon_value_asstring(peekb);
    a = neon_value_asstring(peeka);
    sb = neon_strbuf_make(state);
    neon_strbuf_append(sb, a->sbuf->data, a->sbuf->length);
    neon_strbuf_append(sb, b->sbuf->data, b->sbuf->length);
    result = neon_string_allocfromstrbuf(state, sb, neon_util_hashstring(sb->data, sb->length));
    neon_vmbits_stackpop(state);
    neon_vmbits_stackpop(state);
    neon_vmbits_stackpush(state, neon_value_makeobject(result));
    return true;
}

/* A Virtual Machine binary-op < Types of Values binary-op */
static inline bool neon_vmexec_dobinary(NeonState* state, bool isbool, int32_t op)
{
    double b;
    double a;
    double dw;
    NeonValue res;
    NeonValue peeka;
    NeonValue peekb;
    peeka = neon_vmbits_stackpeek(state, 0);
    peekb = neon_vmbits_stackpeek(state, 1);
    if(!neon_value_isnumber(peeka))
    {
        neon_state_raiseerror(state, "operand a (at 0) must be a number");
        return false;
    }
    if(!neon_value_isnumber(peekb))
    {
        neon_state_raiseerror(state, "operand b (at 1) must be a number");
        return false;
    }
    b = neon_value_asnumber(neon_vmbits_stackpop(state));
    a = neon_value_asnumber(neon_vmbits_stackpop(state));
    switch(op)
    {
        case NEON_OP_PRIMGREATER:
            {
                dw = a > b;
            }
            break;
        case NEON_OP_PRIMLESS:
            {
                dw = a < b;
            }
            break;
        case NEON_OP_PRIMADD:
            {
                dw = a + b;
            }
            break;
        case NEON_OP_PRIMSUBTRACT:
            {
                dw = a - b;
            }
            break;
        case NEON_OP_PRIMMULTIPLY:
            {
                dw = a * b;
            }
            break;
        case NEON_OP_PRIMDIVIDE:
            {
                dw = a / b;
            }
            break;
        case NEON_OP_PRIMMODULO:
            {
                dw = fmod(a, b);
            }
            break;
        case NEON_OP_PRIMBINAND:
            {
                dw = (int)a & (int)b;
            }
            break;
        case NEON_OP_PRIMBINOR:
            {
                dw = (int)a | (int)b;
            }
            break;
        case NEON_OP_PRIMBINXOR:
            {
                dw = (int)a ^ (int)b;
            }
            break;
        case NEON_OP_PRIMSHIFTLEFT:
            {
                dw = (int)a << (int)b;
            }
            break;
        case NEON_OP_PRIMSHIFTRIGHT:
            {
                dw = (int)a >> (int)b;
            }
            break;
        default:
            {
                neon_state_raiseerror(state, "unrecognized instruction for binary");
                return false;
            }
            break;
    }
    if(isbool)
    {
        res = neon_value_makebool(dw);
    }
    else
    {
        res = neon_value_makenumber(dw);
    }
    neon_vmbits_stackpush(state, res);
    return true;
}

static inline bool neon_vmexec_indexgetstring(NeonState* state, NeonObjString* os, NeonValue vidx, NeonValue* destval)
{
    char ch;
    long nidx;
    NeonObjString* nos;
    if(!neon_value_isnumber(vidx))
    {
        neon_state_raiseerror(state, "cannot get string index with non-number type <%s>", neon_value_valuetypename(vidx));
        return false;
    }
    nidx = neon_value_asnumber(vidx);
    if((nidx >= 0) && (nidx < os->sbuf->length))
    {
        ch = os->sbuf->data[nidx];
        nos = neon_string_copy(state, &ch, 1);
        nos->sbuf->length = 1;
        nos->sbuf->data[1] = '\0';
        *destval = neon_value_makeobject(nos);
        return true;
    }
    return false;
}

static inline bool neon_vmexec_indexgetarray(NeonState* state, NeonObjArray* oa, NeonValue vidx, NeonValue* destval)
{
    long nidx;
    NeonValue val;
    if(!neon_value_isnumber(vidx))
    {
        neon_state_raiseerror(state, "cannot get array index with non-number type <%s>", neon_value_valuetypename(vidx));
        return false;
    }
    nidx = neon_value_asnumber(vidx);
    if((nidx >= 0) && (nidx < (long)oa->vala.size))
    {
        val = oa->vala.values[nidx];
        *destval = val;
        return true;
    }
    return false;
}

static inline bool neon_vmexec_indexgetmap(NeonState* state, NeonObjMap* om, NeonValue vidx, NeonValue* destval)
{
    NeonValue val;
    NeonObjString* key;
    if(!neon_value_isstring(vidx))
    {
        neon_state_raiseerror(state, "cannot get map index with non-string type <%s>", neon_value_valuetypename(vidx));
        return false;
    }
    key = neon_value_asstring(vidx);
    if(neon_map_get(om, key, &val))
    {
        *destval = val;
    }
    else
    {
        *destval = neon_value_makenil();
    }
    return true;
}

static inline bool neon_vmexec_indexget(NeonState* state)
{
    bool ok;
    bool willassign;
    int32_t waint;
    NeonValue destval;
    NeonValue vidx;
    NeonValue targetobj;
    (void)waint;
    waint = 0;
    ok = false;
    vidx = neon_vmbits_stackpeek(state, 0);
    targetobj = neon_vmbits_stackpeek(state, 1);
    waint = neon_vmbits_readbyte(state);
    willassign = (waint == 1);
    if(!willassign)
    {
        neon_vmbits_stackpop(state);
        neon_vmbits_stackpop(state);
    }
    //fprintf(stderr, "indexget: waint=%d vidx=<%s> targetobj=<%s>\n", waint, neon_value_valuetypename(vidx), neon_value_valuetypename(targetobj));
    if(neon_value_isstring(targetobj))
    {
        if(neon_vmexec_indexgetstring(state, neon_value_asstring(targetobj), vidx, &destval))
        {
            ok = true;
        }
    }
    else if(neon_value_isarray(targetobj))
    {
        if(neon_vmexec_indexgetarray(state, neon_value_asarray(targetobj), vidx, &destval))
        {
            ok = true;
        }
    }
    else if(neon_value_ismap(targetobj))
    {
        if(neon_vmexec_indexgetmap(state, neon_value_asmap(targetobj), vidx, &destval))
        {
            ok = true;
        }
    }
    else
    {
        neon_state_raiseerror(state, "cannot get index object type <%s>", neon_value_valuetypename(targetobj));
    }
    neon_vmbits_stackpush(state, destval);
    return ok;
}

static inline bool neon_vmexec_indexsetarray(NeonState* state, NeonObjArray* oa, NeonValue vidx, NeonValue setval)
{
    long nidx;
    if(!neon_value_isnumber(vidx))
    {
        neon_state_raiseerror(state, "cannot set array index with non-number type <%s>", neon_value_valuetypename(vidx));
        return false;
    }
    nidx = neon_value_asnumber(vidx);
    /*
    neon_writer_writeformat(state->stderrwriter, "indexsetarray: nidx=%d, setval=<%s> ", nidx, neon_value_valuetypename(setval));
    neon_writer_printvalue(state->stderrwriter, setval, true);
    neon_writer_writeformat(state->stderrwriter, "\n");
    */
    neon_valarray_insert(&oa->vala, nidx, setval);
    return true;
}

static inline bool neon_vmexec_indexsetmap(NeonState* state, NeonObjMap* om, NeonValue vidx, NeonValue setval)
{
    NeonObjString* key;
    if(!neon_value_isstring(vidx))
    {
        neon_state_raiseerror(state, "cannot set map index with non-string type <%s>", neon_value_valuetypename(vidx));
        return false;
    }
    key = neon_value_asstring(vidx);
    neon_map_set(om, key, setval);
    return true;
}

static inline bool neon_vmexec_indexset(NeonState* state)
{
    bool ok;
    bool willassign;
    int waint;
    NeonValue vidx;
    NeonValue targetobj;
    NeonValue setval;
    ok = false;
    setval = neon_vmbits_stackpeek(state, 0);
    vidx = neon_vmbits_stackpeek(state, 1);
    targetobj = neon_vmbits_stackpeek(state, 2);
    waint = neon_vmbits_readbyte(state);
    willassign = (waint == 1);
    
    if(!willassign)
    {
        neon_vmbits_stackpop(state);
        neon_vmbits_stackpop(state);
        neon_vmbits_stackpop(state);
    }
    if(neon_value_isarray(targetobj))
    {
        if(neon_vmexec_indexsetarray(state, neon_value_asarray(targetobj), vidx, setval))
        {
            ok = true;
        }
    }
    else if(neon_value_ismap(targetobj))
    {
        if(neon_vmexec_indexsetmap(state, neon_value_asmap(targetobj), vidx, setval))
        {
            ok = true;
        }
    }
    else
    {
        neon_state_raiseerror(state, "cannot set index object type <%s>", neon_value_valuetypename(targetobj));
    }
    neon_vmbits_stackpush(state, setval);
    return ok;
}

static inline bool neon_vmexec_propertyget(NeonState* state)
{
    NeonValue value;
    NeonValue peeked;
    NeonValue vidx;
    NeonObjString* name;
    NeonObjInstance* instance;
    peeked = neon_vmbits_stackpeek(state, 0);
    if(!neon_value_isinstance(peeked))
    {
        neon_state_raiseerror(state, "cannot get property for object type <%s>", neon_value_valuetypename(peeked));
        return false;
    }
    vidx = neon_vmbits_readconst(state);
    neon_vmbits_stackpop(state);// Instance.
    instance = neon_value_asinstance(peeked);
    name = neon_value_asstring(vidx);
    if(neon_hashtable_get(state, &instance->fields, name, &value))
    {
        neon_vmbits_stackpush(state, value);
        return true;
    }
    if(!neon_vmbits_bindmethod(state, instance->klass, name))
    {
        return false;
    }
    return true;
}

static inline bool neon_vmexec_propertyset(NeonState* state)
{
    NeonValue value;
    NeonValue peeked;
    NeonValue propval;
    NeonObjString* propname;
    NeonObjInstance* instance;
    peeked = neon_vmbits_stackpeek(state, 1);
    if(!neon_value_isinstance(peeked))
    {
        neon_state_raiseerror(state, "cannot set property for object type <%s>", neon_value_valuetypename(peeked));
        return false;
    }
    instance = neon_value_asinstance(peeked);
    propname = neon_vmbits_readstring(state);
    propval = neon_vmbits_stackpeek(state, 0);
    neon_hashtable_set(state, &instance->fields, propname, propval);
    value = neon_vmbits_stackpop(state);
    neon_vmbits_stackpop(state);
    neon_vmbits_stackpush(state, value);
    return true;
}

static inline bool neon_vmexec_makearray(NeonState* state)
{
    int i;
    int count;
    NeonValue val;
    NeonObjArray* array;
    count = neon_vmbits_readbyte(state);
    array = neon_array_make(state);
    //fprintf(stderr, "makearray: count=%d\n", count);
    for(i = count - 1; i >= 0; i--)
    {
        val = neon_vmbits_stackpeek(state, i);
        neon_array_push(array, val);
    }
    if(count > 0)
    {
        for(i=0; i<(count-0); i++)
        {
            neon_vmbits_stackpop(state);
        }
    }
    neon_vmbits_stackpush(state, neon_value_makeobject(array));
    return true;
}

static inline bool neon_vmexec_makemap(NeonState* state)
{
    int count;
    NeonObjMap* map;
    (void)count;
    count = neon_vmbits_readbyte(state);
    map = neon_object_makemap(state);
    neon_vmbits_stackpush(state, neon_value_makeobject(map));
    return true;
}

static inline void neon_vm_stackdebugprint(NeonState* state, NeonWriter* owr, NeonCallFrame* frame)
{
    int ofs;
    int nowpos;
    int spos;
    int frompos;
    int64_t stacktop;
    NeonChunk* chnk;
    NeonValue* slot;
    NeonValue* stv;
    if(frame == NULL)
    {
        return;
    }
    stacktop = state->vmvars.stacktop;
    stv = state->vmvars.stackvalues;
    if(stacktop == -1)
    {
        stacktop = 0;
    }

    chnk = &frame->closure->innerfn->chunk;
    ofs = (int)(frame->ip - frame->closure->innerfn->chunk.code) - 1;
    neon_dbg_dumpdisasm(state, owr, chnk, ofs);
    neon_writer_writeformat(owr, "  stack=[\n");

    frompos = 0;
    //frompos = frame->frstackindex;

    spos = 0;
    //spos = frame->frstackindex;
    
    for(slot = &stv[frompos]; slot < &stv[stacktop]; slot++)
    {
        nowpos = spos;
        spos++;
        neon_writer_writeformat(owr, "    [%d] <%s> ", (int)nowpos-1, neon_value_valuetypename(*slot));
        neon_writer_printvalue(owr, *slot, true);
        neon_writer_writeformat(owr, "\n");
    }
    neon_writer_writeformat(owr, "  ]\n");
}

NeonStatusCode neon_vm_runvm(NeonState* state)
{
    size_t icnt;
    int32_t instruc;
    NeonWriter* owr;
    (void)icnt;
    owr = state->stderrwriter;
    state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];
    for(;;)
    {
        instruc = neon_vmbits_readbyte(state);
        if(state->conf.shouldprintruntime)
        {
            neon_vm_stackdebugprint(state, owr, state->vmvars.currframe);
        }
        switch(instruc)
        {
            case NEON_OP_PUSHCONST:
                {
                    NeonValue cval;
                    cval = neon_vmbits_readconst(state);
                    /* A Virtual Machine op-constant < A Virtual Machine push-constant */
                    /*
                    neon_writer_writeformat(state->stderrwriter, "pushconst: ");
                    neon_writer_printvalue(state->stderrwriter, cval, true);
                    neon_writer_writeformat(state->stderrwriter, "\n");
                    */
                    neon_vmbits_stackpush(state, cval);
                }
                break;
            case NEON_OP_PUSHONE:
                {
                    neon_vmbits_stackpush(state, neon_value_makenumber((double)1));
                }
                break;
            case NEON_OP_PUSHNIL:
                {
                    neon_vmbits_stackpush(state, neon_value_makenil());
                }
                break;
            case NEON_OP_PUSHTRUE:
                {
                    neon_vmbits_stackpush(state, neon_value_makebool(true));
                }
                break;
            case NEON_OP_PUSHFALSE:
                {
                    neon_vmbits_stackpush(state, neon_value_makebool(false));
                }
                break;
            case NEON_OP_MAKEARRAY:
                {
                    if(!neon_vmexec_makearray(state))
                    {
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case NEON_OP_MAKEMAP:
                {
                    if(!neon_vmexec_makemap(state))
                    {
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case NEON_OP_POP:
                {
                    neon_vmbits_stackpop(state);
                }
                break;
            case NEON_OP_POPN:
                {
                    int32_t n;
                    n = neon_vmbits_readbyte(state);
                    neon_vmbits_stackpopn(state, n);
                }
                break;
            case NEON_OP_DUP:
                {
                    neon_vmbits_stackpush(state, neon_vmbits_stackpeek(state, 0));
                }
                break;
            case NEON_OP_LOCALGET:
                {
                    int32_t islot;
                    NeonValue val;
                    islot = neon_vmbits_readbyte(state);
                    val = state->vmvars.stackvalues[state->vmvars.currframe->frstackindex + (islot + 0)];
                    neon_vmbits_stackpush(state, val);
                }
                break;
            case NEON_OP_LOCALSET:
                {
                    int32_t islot;
                    NeonValue val;
                    islot = neon_vmbits_readbyte(state);
                    val = neon_vmbits_stackpeek(state, 0);
                    state->vmvars.stackvalues[state->vmvars.currframe->frstackindex + (islot + 0)] = val;
                }
                break;
            case NEON_OP_GLOBALGET:
                {
                    NeonValue value;
                    NeonObjString* name;
                    name = neon_vmbits_readstring(state);
                    if(!neon_hashtable_get(state, &state->globals, name, &value))
                    {
                        neon_state_raiseerror(state, "undefined variable '%s'.", name->sbuf->data);
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                    neon_vmbits_stackpush(state, value);
                }
                break;
            case NEON_OP_GLOBALDEFINE:
                {
                    NeonValue peeked;
                    NeonObjString* name;
                    name = neon_vmbits_readstring(state);
                    peeked = neon_vmbits_stackpeek(state, 0);
                    neon_hashtable_set(state, &state->globals, name, peeked);
                    neon_vmbits_stackpop(state);
                }
                break;            
            case NEON_OP_GLOBALSET:
                {
                    NeonValue peeked;
                    NeonObjString* name;
                    name = neon_vmbits_readstring(state);
                    peeked = neon_vmbits_stackpeek(state, 0);
                    if(neon_hashtable_set(state, &state->globals, name, peeked))
                    {
                        //neon_hashtable_delete(state, &state->globals, name);// [delete]
                        //neon_state_raiseerror(state, "undefined variable '%s'.", name->sbuf->data);
                        //return NEON_STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case NEON_OP_UPVALGET:
                {
                    int32_t islot;
                    islot = neon_vmbits_readbyte(state);
                    neon_vmbits_stackpush(state, *state->vmvars.currframe->closure->upvalues[islot]->location);
                }
                break;
            case NEON_OP_UPVALSET:
                {
                    int32_t islot;
                    NeonValue peeked;
                    islot = neon_vmbits_readbyte(state);
                    peeked = neon_vmbits_stackpeek(state, 0);
                    *state->vmvars.currframe->closure->upvalues[islot]->location = peeked;
                }
                break;

            case NEON_OP_INDEXGET:
                {
                    if(!neon_vmexec_indexget(state))
                    {
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case NEON_OP_INDEXSET:
                {
                    if(!neon_vmexec_indexset(state))
                    {
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case NEON_OP_PROPERTYGET:
                {
                    if(!neon_vmexec_propertyget(state))
                    {
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case NEON_OP_PROPERTYSET:
                {
                    if(!neon_vmexec_propertyset(state))
                    {
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case NEON_OP_INSTGETSUPER:
                {
                    NeonObjString* name;
                    NeonObjClass* superclass;
                    name = neon_vmbits_readstring(state);
                    superclass = neon_value_asclass(neon_vmbits_stackpop(state));
                    if(!neon_vmbits_bindmethod(state, superclass, name))
                    {
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case NEON_OP_EQUAL:
                {
                    NeonValue a;
                    NeonValue b;
                    b = neon_vmbits_stackpop(state);
                    a = neon_vmbits_stackpop(state);
                    neon_vmbits_stackpush(state, neon_value_makebool(neon_value_equal(a, b)));
                }
                break;
            case NEON_OP_PRIMGREATER:
            case NEON_OP_PRIMLESS:
                {
                    if(!neon_vmexec_dobinary(state, true, instruc))
                    {
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case NEON_OP_PRIMADD:
                {
                    NeonValue peek1;
                    NeonValue peek2;
                    peek1 = neon_vmbits_stackpeek(state, 0);
                    peek2 = neon_vmbits_stackpeek(state, 1);
                    if(neon_value_isstring(peek1) && neon_value_isstring(peek2))
                    {
                        neon_vmexec_concat(state);
                    }
                    else if(neon_value_isnumber(peek1) && neon_value_isnumber(peek2))
                    {
                        if(!neon_vmexec_dobinary(state, false, instruc))
                        {
                            return NEON_STATUS_RUNTIMEERROR;
                        }
                    }
                    else
                    {
                        neon_state_raiseerror(state, "expected <number>|<number> or <string>|<string>, but got (#0|#1) <%s>|<%s>", neon_value_valuetypename(peek1), neon_value_valuetypename(peek2));
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case NEON_OP_PRIMSUBTRACT:
            case NEON_OP_PRIMMULTIPLY:
            case NEON_OP_PRIMDIVIDE:
            case NEON_OP_PRIMMODULO:
            case NEON_OP_PRIMSHIFTLEFT:
            case NEON_OP_PRIMSHIFTRIGHT:
            case NEON_OP_PRIMBINAND:
            case NEON_OP_PRIMBINOR:
            case NEON_OP_PRIMBINXOR:
                {
                    if(!neon_vmexec_dobinary(state, false, instruc))
                    {
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case NEON_OP_PRIMNOT:
                {
                    NeonValue popped;
                    popped = neon_vmbits_stackpop(state);
                    neon_vmbits_stackpush(state, neon_value_makebool(neon_value_isfalsey(popped)));
                }
                break;
            case NEON_OP_PRIMNEGATE:
                {
                    NeonValue peeked;
                    NeonValue popped;
                    peeked = neon_vmbits_stackpeek(state, 0);
                    if(!neon_value_isnumber(peeked))
                    {
                        neon_state_raiseerror(state, "operand must be a number.");
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                    popped = neon_vmbits_stackpop(state);
                    neon_vmbits_stackpush(state, neon_value_makenumber(-neon_value_asnumber(popped)));
                }
                break;
            case NEON_OP_DEBUGPRINT:
                {
                    NeonValue val;
                    val = neon_vmbits_stackpop(state);
                    neon_writer_writeformat(state->stderrwriter, "debug: ");
                    neon_writer_printvalue(state->stderrwriter, val, true);
                    neon_writer_writeformat(state->stderrwriter, "\n");
                }
                break;
            case NEON_OP_JUMPNOW:
                {
                    uint16_t offset;
                    offset = neon_vmbits_readshort(state);
                    state->vmvars.currframe->ip += offset;
                }
                break;

            case NEON_OP_JUMPIFFALSE:
                {
                    uint16_t offset;
                    NeonValue peeked;
                    offset = neon_vmbits_readshort(state);
                    peeked = neon_vmbits_stackpeek(state, 0);
                    if(neon_value_isfalsey(peeked))
                    {
                        state->vmvars.currframe->ip += offset;
                    }
                }
                break;
            case NEON_OP_LOOP:
                {
                    uint16_t offset;
                    offset = neon_vmbits_readshort(state);
                    state->vmvars.currframe->ip -= offset;
                }
                break;
            case NEON_OP_CALL:
                {
                    int argc;
                    NeonValue peeked;
                    argc = neon_vmbits_readbyte(state);
                    peeked = neon_vmbits_stackpeek(state, argc);
                    if(!neon_vmbits_callvalue(state, peeked, argc))
                    {
                        fprintf(stderr, "returning error\n");
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                    state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];
                }
                break;

            case NEON_OP_INSTTHISINVOKE:
                {
                    int argc;
                    NeonObjString* method;
                    method = neon_vmbits_readstring(state);
                    argc = neon_vmbits_readbyte(state);
                    if(!neon_vmbits_invoke(state, method, argc))
                    {
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                    state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];
                }
                break;
            case NEON_OP_INSTSUPERINVOKE:
                {
                    int argc;
                    NeonValue popped;
                    NeonObjString* method;
                    NeonObjClass* superclass;
                    method = neon_vmbits_readstring(state);
                    argc = neon_vmbits_readbyte(state);
                    popped = neon_vmbits_stackpop(state);
                    superclass = neon_value_asclass(popped);
                    if(!neon_vmbits_invokefromclass(state, superclass, method, argc))
                    {
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                    state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];
                }
                break;
            case NEON_OP_CLOSURE:
                {
                    int i;
                    int32_t index;
                    int32_t islocal;
                    NeonValue vcval;
                    NeonObjScriptFunction* fn;
                    NeonObjClosure* closure;
                    vcval = neon_vmbits_readconst(state);
                    fn = neon_value_asscriptfunction(vcval);
                    closure = neon_object_makeclosure(state, fn);
                    neon_vmbits_stackpush(state, neon_value_makeobject(closure));
                    for(i = 0; i < closure->upvaluecount; i++)
                    {
                        islocal = neon_vmbits_readbyte(state);
                        index = neon_vmbits_readbyte(state);
                        if(islocal)
                        {
                            closure->upvalues[i] = neon_vmbits_captureupval(state, &state->vmvars.stackvalues[state->vmvars.currframe->frstackindex + index]);
                        }
                        else
                        {
                            closure->upvalues[i] = state->vmvars.currframe->closure->upvalues[index];
                        }
                    }
                }
                break;
            case NEON_OP_UPVALCLOSE:
                {
                    NeonValue* vargs;
                    vargs = (&state->vmvars.stackvalues[0] + state->vmvars.stacktop) - 1;
                    neon_vmbits_closeupvals(state, vargs);
                    neon_vmbits_stackpop(state);
                }
                break;
            case NEON_OP_RETURN:
                {
                    int64_t usable;
                    NeonValue result;
                    result = neon_vmbits_stackpop(state);
                    if(state->vmvars.currframe->frstackindex >= 0)
                    {
                        neon_vmbits_closeupvals(state, &state->vmvars.stackvalues[state->vmvars.currframe->frstackindex]);
                    }
                    state->vmvars.framecount--;
                    if(state->vmvars.framecount == 0)
                    {
                        neon_vmbits_stackpop(state);
                        //fprintf(stderr, "returning due to NEON_OP_RETURN\n");
                        return NEON_STATUS_OK;
                    }
                    usable = (state->vmvars.currframe->frstackindex - 0);
                    state->vmvars.stacktop = usable;
                    neon_vmbits_stackpush(state, result);
                    state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];

                }
                break;
            case NEON_OP_CLASS:
                {
                    NeonObjClass* klass;
                    NeonObjString* clname;
                    clname = neon_vmbits_readstring(state);
                    klass = neon_object_makeclass(state, clname);
                    neon_vmbits_stackpush(state, neon_value_makeobject(klass));
                }
                break;
            case NEON_OP_INHERIT:
                {
                    NeonValue vklass;
                    NeonValue superclass;
                    NeonObjClass* subclass;
                    superclass = neon_vmbits_stackpeek(state, 1);
                    if(!neon_value_isclass(superclass))
                    {
                        neon_state_raiseerror(state, "superclass must be a class.");
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                    vklass = neon_vmbits_stackpeek(state, 0);
                    subclass = neon_value_asclass(vklass);
                    neon_hashtable_addall(state, &neon_value_asclass(superclass)->methods, &subclass->methods);
                    neon_vmbits_stackpop(state);// Subclass.
                }
                break;

            case NEON_OP_METHOD:
                {
                    NeonObjString* name;
                    name = neon_vmbits_readstring(state);
                    neon_vmbits_defmethod(state, name);
                }
                break;
            case NEON_OP_HALTVM:
                {
                    return NEON_STATUS_OK;
                }
                break;
            default:
                {
                    if(instruc != -1)
                    {
                        neon_state_raiseerror(state, "internal error: invalid opcode %d!", instruc);
                        return NEON_STATUS_RUNTIMEERROR;
                    }
                }
                break;
        }
    }
}


NeonStatusCode neon_state_runsource(NeonState* state, const char* source)
{
    NeonObjScriptFunction* fn;
    NeonObjClosure* closure;
    fn = neon_prs_compilesource(state, source);
    if(fn == NULL)
    {
        return NEON_STATUS_SYNTAXERROR;
    }
    neon_vm_stackpush(state, neon_value_makeobject(fn));
    closure = neon_object_makeclosure(state, fn);
    neon_vm_stackpop(state);
    neon_vm_stackpush(state, neon_value_makeobject(closure));
    neon_vmbits_callclosure(state, closure, 0);
    return neon_vm_runvm(state);
}

static char* neon_util_readhandle(FILE* hnd, size_t* dlen)
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

static char* neon_util_readfile(const char* filename, size_t* dlen)
{
    char* b;
    FILE* fh;
    if((fh = fopen(filename, "rb")) == NULL)
    {
        return NULL;
    }
    b = neon_util_readhandle(fh, dlen);
    fclose(fh);
    return b;
}


bool neon_state_runfile(NeonState* state, const char* path)
{
    size_t fsz;
    char* source;
    NeonStatusCode result;
    source = neon_util_readfile(path, &fsz);
    if(source == NULL)
    {
        fprintf(stderr, "failed to read from '%s'\n", path);
        return false;
    }
    result = neon_state_runsource(state, source);
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

static void repl(NeonState* state)
{
    char line[1024];
    for(;;)
    {
        printf("> ");
        if(!fgets(line, sizeof(line), stdin))
        {
            printf("\n");
            break;
        }
        neon_state_runsource(state, line);
    }
}

static NeonValue cfn_clock(NeonState* state, int argc, NeonValue* argv)
{
    (void)state;
    (void)argc;
    (void)argv;
    return neon_value_makenumber((double)clock() / CLOCKS_PER_SEC);
}

static NeonValue cfn_stringchr(NeonState* state, int argc, NeonValue* argv)
{
    double dw;
    char ch;
    NeonObjString* os;
    (void)state;
    (void)argc;
    (void)argv;
    dw = argv[0].as.number;
    ch = (char)dw;
    os = neon_string_copy(state, &ch, 1);
    return neon_value_makeobject(os);
}

static NeonValue cfn_stringlength(NeonState* state, int argc, NeonValue* argv)
{
    NeonObjString* os;
    (void)argv;
    os = neon_value_asstring(argv[0]);
    return neon_value_makenumber(os->sbuf->length);
}


static NeonValue cfn_stringdump(NeonState* state, int argc, NeonValue* argv)
{
    NeonValue vs;
    NeonObjString* res;
    NeonWriter* wr;
    (void)argv;
    vs = argv[0];
    wr = neon_writer_makestring(state);
    neon_writer_printvalue(wr, vs, true);
    res = neon_string_copy(state, wr->strbuf->data, wr->strbuf->length);
    neon_writer_release(wr);
    return neon_value_makeobject(res);
}


static NeonValue cfn_print(NeonState* state, int argc, NeonValue* argv)
{
    int i;
    (void)state;
    for(i=0; i<argc; i++)
    {
        neon_writer_printvalue(state->stdoutwriter, argv[i], false);
    }
    return neon_value_makenumber(0);
}

static NeonValue cfn_println(NeonState* state, int argc, NeonValue* argv)
{
    cfn_print(state, argc, argv);
    printf("\n");
    return neon_value_makenumber(0);
}

static NeonValue cfn_arraypush(NeonState* state, int argc, NeonValue* argv)
{
    int i;
    (void)state;
    NeonValue selfval;
    NeonObjArray* oa;
    selfval = argv[0];
    if(!neon_value_isarray(selfval))
    {
        neon_state_raiseerror(state, "first argument must be array");
    }
    else
    {
        oa = neon_value_asarray(selfval);
        for(i=1; i<argc; i++)
        {
            neon_array_push(oa, argv[i]);
        }
    }
    return neon_value_makenumber(0);
}

static NeonValue cfn_arraycount(NeonState* state, int argc, NeonValue* argv)
{
    (void)state;
    NeonValue selfval;
    NeonObjArray* oa;
    (void)argc;
    selfval = argv[0];
    if(!neon_value_isarray(selfval))
    {
        neon_state_raiseerror(state, "first argument must be array");
        return neon_value_makenil();
    }
    else
    {
        oa = neon_value_asarray(selfval);
        return neon_value_makenumber(oa->vala.size);
    }
    return neon_value_makenumber(0);
}

static NeonValue cfn_arrayset(NeonState* state, int argc, NeonValue* argv)
{
    long idx;
    NeonValue putval;
    NeonValue idxval;
    NeonValue selfval;
    NeonObjArray* oa;
    (void)state;
    (void)argc;
    selfval = argv[0];
    idxval = argv[1];
    putval = argv[2];
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
        neon_valarray_insert(&oa->vala, idx, putval);
    }
    return neon_value_makenumber(0);
}

static NeonValue cfn_arrayerase(NeonState* state, int argc, NeonValue* argv)
{
    long idx;
    NeonValue idxval;
    NeonValue selfval;
    NeonObjArray* oa;
    (void)state;
    (void)argc;
    selfval = argv[0];
    idxval = argv[1];
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
        return neon_value_makebool(neon_valarray_erase(&oa->vala, idx));
    }
    return neon_value_makebool(false);
}

static NeonValue cfn_mapmake(NeonState* state, int argc, NeonValue* argv)
{
    NeonObjMap* om;
    (void)argc;
    (void)argv;
    om = neon_object_makemap(state);
    return neon_value_makeobject(om);
}


static NeonValue cfn_ioreadfile(NeonState* state, int argc, NeonValue* argv)
{
    size_t flen;
    char* buf;
    const char* fname;
    NeonObjString* resos;
    NeonObjString* osname;
    if(argc > 0)
    {
        osname = neon_value_asstring(argv[0]);
        fname = osname->sbuf->data;
        buf = neon_util_readfile(fname, &flen);
        if(buf == NULL)
        {
            neon_state_raiseerror(state, "failed to read from '%s'", fname);
        }
        else
        {
            resos = neon_string_take(state, buf, flen);
            return neon_value_makeobject(resos);
        }
    }
    neon_state_raiseerror(state, "expected string as filename");
    return neon_value_makenil();
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
    NeonState* state;
    exitcode = 0;
    nargv = argv;
    nargc = argc;
    codeline = NULL;
    state = neon_state_init();
    neon_state_defnative(state, "clock", cfn_clock);
    neon_state_defnative(state, "string_length", cfn_stringlength);
    neon_state_defnative(state, "string_chr", cfn_stringchr);
    neon_state_defnative(state, "string_dump", cfn_stringdump);
    neon_state_defnative(state, "print", cfn_print);
    neon_state_defnative(state, "println", cfn_println);
    neon_state_defnative(state, "array_count", cfn_arraycount);
    neon_state_defnative(state, "array_push", cfn_arraypush);
    neon_state_defnative(state, "array_erase", cfn_arrayerase);
    neon_state_defnative(state, "array_set", cfn_arrayset);
    neon_state_defnative(state, "map_make", cfn_mapmake);
    neon_state_defnative(state, "io_readfile", cfn_ioreadfile);
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
        neon_state_runsource(state, codeline);
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
    neon_state_free(state);
    return exitcode;
}
