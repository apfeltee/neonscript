

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


/* A Virtual Machine stack-max < Calls and Functions frame-max
#define STACK_MAX 256
*/
#define FRAMES_MAX 1024
#define STACK_MAX (FRAMES_MAX * 2)

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value) ((value).type == VAL_OBJ)

#define BOOL_VAL(value) ((Value){ VAL_BOOL, { .boolean = value } })
#define NIL_VAL ((Value){ VAL_NIL, { .number = 0 } })
#define NUMBER_VAL(value) ((Value){ VAL_NUMBER, { .number = value } })
#define OBJ_VAL(object) ((Value){ VAL_OBJ, { .obj = (Object*)object } })

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

//#define IS_BOUND_METHOD(value) lox_value_isobjtype(value, OBJ_BOUNDMETHOD)
#define IS_CLASS(value) lox_value_isobjtype(value, OBJ_CLASS)
//#define IS_CLOSURE(value) lox_value_isobjtype(value, OBJ_CLOSURE)
//#define IS_FUNCTION(value) lox_value_isobjtype(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) lox_value_isobjtype(value, OBJ_INSTANCE)
//#define IS_NATIVE(value) lox_value_isobjtype(value, OBJ_NATIVE)
#define IS_STRING(value) lox_value_isobjtype(value, OBJ_STRING)
#define IS_ARRAY(value) lox_value_isobjtype(value, OBJ_ARRAY)

#define AS_OBJ(value) ((value).as.obj)
#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value) ((ObjNative*)AS_OBJ(value))
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->sbuf->data)
#define AS_ARRAY(value) ((ObjArray*)AS_OBJ(value))

#define ALLOCATE(state, type, count) (type*)lox_mem_realloc(state, NULL, 0, sizeof(type) * (count))

#define FREE(state, type, pointer) lox_mem_realloc(state, pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity)*2)

#define GROW_ARRAY(state, type, pointer, oldcnt, newcnt) (type*)lox_mem_realloc(state, pointer, sizeof(type) * (oldcnt), sizeof(type) * (newcnt))

#define FREE_ARRAY(state, type, pointer, oldcnt) lox_mem_realloc(state, pointer, sizeof(type) * (oldcnt), 0)


#define GC_HEAP_GROW_FACTOR 2


#define TABLE_MAX_LOAD 0.75



// OP_FUNCARG - explicitly for function arguments (and default values)?

enum OpCode
{
    OP_PUSHCONST,
    OP_PUSHNIL,
    OP_PUSHTRUE,
    OP_PUSHFALSE,
    OP_PUSHONE,
    OP_POP,
    OP_POPN,
    OP_DUP,
    OP_LOCALGET,
    OP_LOCALSET,
    OP_GLOBALGET,
    OP_GLOBALDEFINE,
    OP_GLOBALSET,
    OP_UPVALGET,
    OP_UPVALSET,
    OP_PROPERTYGET,
    OP_PROPERTYSET,
    OP_INSTGETSUPER,
    OP_EQUAL,
    OP_PRIMGREATER,
    OP_PRIMLESS,
    OP_PRIMADD,
    OP_PRIMSUBTRACT,
    OP_PRIMMULTIPLY,
    OP_PRIMDIVIDE,
    OP_PRIMMODULO,
    OP_PRIMSHIFTLEFT,
    OP_PRIMSHIFTRIGHT,
    OP_PRIMBINAND,
    OP_PRIMBINOR,
    OP_PRIMBINXOR,
    OP_PRIMNOT,
    OP_PRIMNEGATE,
    OP_DEBUGPRINT,
    OP_JUMPNOW,
    OP_JUMPIFFALSE,
    OP_LOOP,
    OP_CALL,
    OP_INSTTHISINVOKE,
    OP_INSTSUPERINVOKE,
    OP_CLOSURE,
    OP_UPVALCLOSE,
    OP_RETURN,
    OP_CLASS,
    OP_INHERIT,
    OP_METHOD,
    OP_MAKEARRAY,
    OP_INDEXGET,
    OP_INDEXSET,
    OP_HALTVM,
    OP_PSEUDOBREAK,
};

enum ValueType
{
    VAL_BOOL,
    VAL_NIL,// [user-types]
    VAL_NUMBER,
    VAL_OBJ
};

enum ObjType
{
    OBJ_BOUNDMETHOD,
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE,
    OBJ_ARRAY,
    OBJ_MAP,
};

enum StatusCode
{
    STATUS_OK,
    STATUS_SYNTAXERROR,
    STATUS_RUNTIMEERROR
};

enum AstTokType
{
    // Single-character tokens.
    TOK_PARENOPEN,
    TOK_PARENCLOSE,
    TOK_BRACEOPEN,
    TOK_BRACECLOSE,
    TOK_BRACKETOPEN,
    TOK_BRACKETCLOSE,
    TOK_COMMA,
    TOK_DOT,
    TOK_MINUS,
    TOK_PLUS,
    TOK_SEMICOLON,
    TOK_NEWLINE,
    TOK_SLASH,
    TOK_STAR,
    TOK_MODULO,
    TOK_BINAND,
    TOK_BINOR,
    TOK_BINXOR,
    // One or two character tokens.
    TOK_SHIFTLEFT,
    TOK_SHIFTRIGHT,
    TOK_EXCLAM,
    TOK_COMPNOTEQUAL,
    TOK_ASSIGN,
    TOK_COMPEQUAL,
    TOK_COMPGREATERTHAN,
    TOK_COMPGREATEREQUAL,
    TOK_COMPLESSTHAN,
    TOK_COMPLESSEQUAL,
    TOK_INCREMENT,
    TOK_DECREMENT,
    // Literals.
    TOK_IDENTIFIER,
    TOK_STRING,
    TOK_NUMBER,
    // Keywords.
    TOK_KWBREAK,
    TOK_KWCONTINUE,
    TOK_KWAND,
    TOK_KWCLASS,
    TOK_KWELSE,
    TOK_KWFALSE,
    TOK_KWFOR,
    TOK_KWFUNCTION,
    TOK_KWIF,
    TOK_KWNIL,
    TOK_KWOR,
    TOK_KWDEBUGPRINT,
    TOK_KWRETURN,
    TOK_KWSUPER,
    TOK_KWTHIS,
    TOK_KWTRUE,
    TOK_KWVAR,
    TOK_KWWHILE,
    TOK_ERROR,
    TOK_EOF
};

enum AstPrecedence
{
    PREC_NONE,
    PREC_ASSIGNMENT,// =, &=, |=, *=, +=, -=, /=, **=, %=, >>=, <<=, ^=, //=
    // ~=
    PREC_CONDITIONAL,// ?:
    PREC_OR,// or
    PREC_AND,// and
    PREC_EQUALITY,// ==, !=
    PREC_COMPARISON,// <, >, <=, >=
    PREC_BIT_OR,// |
    PREC_BIT_XOR,// ^
    PREC_BIT_AND,// &
    PREC_SHIFT,// <<, >>
    PREC_RANGE,// ..
    PREC_TERM,// +, -
    PREC_FACTOR,// *, /, %, **, //
    PREC_UNARY,// !, -, ~, (++, -- this two will now be treated as statements)
    PREC_CALL,// ., ()
    PREC_PRIMARY
};


enum AstFuncType
{
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_SCRIPT
};


typedef enum ObjType ObjType;
typedef enum AstPrecedence AstPrecedence;
typedef enum AstFuncType AstFuncType;
typedef enum ValueType ValueType;
typedef enum StatusCode StatusCode;
typedef enum AstTokType AstTokType;
typedef enum OpCode OpCode;

typedef struct /**/Object Object;
typedef struct /**/ObjString ObjString;
typedef struct /**/ObjFunction ObjFunction;
typedef struct /**/ObjNative ObjNative;
typedef struct /**/ObjUpvalue ObjUpvalue;
typedef struct /**/ObjClosure ObjClosure;
typedef struct /**/ObjClass ObjClass;
typedef struct /**/ObjInstance ObjInstance;
typedef struct /**/ObjBoundMethod ObjBoundMethod;
typedef struct /**/ObjArray ObjArray;
typedef struct /**/CallFrame CallFrame;
typedef struct /**/Value Value;
typedef struct /**/ValArray ValArray;
typedef struct /**/Chunk Chunk;
typedef struct /**/HashEntry HashEntry;
typedef struct /**/HashTable HashTable;
typedef struct /**/LoxState LoxState;
typedef struct /**/AstToken AstToken;
typedef struct /**/AstRule AstRule;
typedef struct /**/AstLoop AstLoop;
typedef struct /**/AstLocal AstLocal;
typedef struct /**/AstUpvalue AstUpvalue;
typedef struct /**/AstCompiler AstCompiler;
typedef struct /**/AstClassCompiler AstClassCompiler;
typedef struct /**/AstScanner AstScanner;
typedef struct /**/AstParser AstParser;
typedef struct /**/Strbuf Strbuf;
typedef struct /**/Writer Writer;
typedef struct /**/LoxVMVars LoxVMVars;
typedef struct /**/LoxGCVars LoxGCVars;
typedef struct /**/LoxConfig LoxConfig;

typedef void (*AstParseFN)(AstParser*, bool);
typedef Value (*NativeFN)(LoxState*, int, Value*);
typedef bool (*DestroyFN)(LoxState*, void*, Value);

struct Writer
{
    bool isstring;
    bool shouldclose;
    Strbuf* strbuf;
    FILE* handle;
};


/* Chunks of Bytecode value-h < Types of Values value
typedef double Value;
*/
struct Value
{
    ValueType type;

    union
    {
        bool boolean;
        double number;
        Object* obj;
    } as;// [as]
};

struct ValArray
{
    LoxState* pvm;

    size_t size;
    size_t capacity;
    Value* values;
};


struct Chunk
{
    int count;
    int capacity;
    int32_t* code;
    int* lines;
    ValArray constants;
};

struct HashEntry
{
    ObjString* key;
    Value value;
};

struct HashTable
{
    int count;
    int capacity;
    HashEntry* entries;
};


struct Object
{
    ObjType type;
    bool ismarked;
    Object* next;
};

struct ObjArray
{
    Object obj;
    LoxState* pvm;
    ValArray vala;
};

struct ObjFunction
{
    Object obj;
    int arity;
    int upvaluecount;
    Chunk chunk;
    ObjString* name;
};

struct ObjNative
{
    Object obj;
    NativeFN natfunc;
};

struct Strbuf
{
    LoxState* pvm;
    int length;
    size_t capacity;
    char* data;
};

struct ObjString
{
    Object obj;
    uint32_t hash;
    // actual string handling is handled by Strbuf.
    // this is to avoid to trashing the stack with temporary values
    Strbuf* sbuf;
};

struct ObjUpvalue
{
    Object obj;
    Value* location;
    Value closed;
    ObjUpvalue* next;
};

struct ObjClosure
{
    Object obj;
    ObjFunction* innerfn;
    ObjUpvalue** upvalues;
    int upvaluecount;
};

struct ObjClass
{
    Object obj;
    ObjString* name;
    HashTable methods;
};

struct ObjInstance
{
    Object obj;
    ObjClass* klass;
    HashTable fields;// [fields]
};

struct ObjBoundMethod
{
    Object obj;
    Value receiver;
    ObjClosure* method;
};

struct CallFrame
{
    ObjClosure* closure;
    int32_t* ip;
    int64_t frstackindex;
};

struct AstToken
{
    AstTokType type;
    const char* start;
    int length;
    int line;
};

struct AstRule
{
    AstParseFN prefix;
    AstParseFN infix;
    AstPrecedence precedence;
};

struct AstLoop
{
    int start;
    int body;
    int scopedepth;
    AstLoop* enclosing;
};


struct AstLocal
{
    AstToken name;
    int depth;
    bool iscaptured;
};

struct AstUpvalue
{
    int32_t index;
    bool islocal;
};

struct AstCompiler
{
    AstCompiler* enclosing;
    ObjFunction* compiledfn;
    AstFuncType type;

    AstLocal locals[UINT8_COUNT];
    int localcount;
    AstUpvalue upvalues[UINT8_COUNT];
    int scopedepth;
    AstLoop* loop;
};

struct AstClassCompiler
{
    AstClassCompiler* enclosing;
    bool hassuperclass;
};

struct AstScanner
{
    LoxState* pvm;
    const char* start;
    const char* current;
    int line;
};

struct AstParser
{
    LoxState* pvm;
    AstScanner* pscn;
    AstToken current;
    AstToken previous;
    bool haderror;
    bool panicmode;
    AstCompiler* currcompiler;
    AstClassCompiler* currclass;
};

struct LoxVMVars
{
    int64_t stacktop;
    int framecount;
    bool hasraised;
    CallFrame* currframe;
    CallFrame framevalues[FRAMES_MAX];
    Value stackvalues[STACK_MAX];    
};

struct LoxGCVars
{
    size_t bytesallocd;
    size_t nextgc;
    Object* linkedobjects;
    int graycount;
    int graycap;
    Object** graystack;
};

struct LoxConfig
{
    bool shouldprintruntime;
};

struct LoxState
{
    LoxConfig conf;
    AstParser* parser;
    LoxVMVars vmvars;
    LoxGCVars gcstate;
    HashTable globals;
    HashTable strings;
    ObjString* initstring;
    ObjUpvalue* openupvalues;
    Writer* stdoutwriter;
    Writer* stderrwriter;
};

#include "prot.inc"

bool lox_value_isobjtype(Value value, ObjType type)
{
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

bool lox_value_isfalsey(Value value)
{
    return (
        IS_NIL(value) || (
            IS_BOOL(value) &&
            !AS_BOOL(value)
        )
    );
}


void* lox_mem_realloc(LoxState* state, void* pointer, size_t oldsz, size_t newsz)
{
    state->gcstate.bytesallocd += newsz - oldsz;
    if(newsz > oldsz)
    {
#ifdef DEBUG_STRESS_GC
        lox_gcmem_collectgarbage(state);
#endif

        if(state->gcstate.bytesallocd > state->gcstate.nextgc)
        {
            lox_gcmem_collectgarbage(state);
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

void lox_gcmem_markroots(LoxState* state)
{
    Value* pslot;
    for(pslot=&state->vmvars.stackvalues[0]; pslot < &state->vmvars.stackvalues[state->vmvars.stacktop]; pslot++)
    {
        lox_gcmem_markvalue(state, *pslot);
    }

    for(int i = 0; i < state->vmvars.framecount; i++)
    {
        lox_gcmem_markobject(state, (Object*)state->vmvars.framevalues[i].closure);
    }

    for(ObjUpvalue* upvalue = state->openupvalues; upvalue != NULL; upvalue = upvalue->next)
    {
        lox_gcmem_markobject(state, (Object*)upvalue);
    }

    lox_gcmem_markhashtable(state, &state->globals);
    lox_prs_markcompilerroots(state);
    lox_gcmem_markobject(state, (Object*)state->initstring);
}

void lox_gcmem_tracerefs(LoxState* state)
{
    while(state->gcstate.graycount > 0)
    {
        Object* object = state->gcstate.graystack[--state->gcstate.graycount];
        lox_gcmem_blackenobj(state, object);
    }
}

void lox_gcmem_sweep(LoxState* state)
{
    Object* previous = NULL;
    Object* object = state->gcstate.linkedobjects;
    while(object != NULL)
    {
        if(object->ismarked)
        {
            object->ismarked = false;
            previous = object;
            object = object->next;
        }
        else
        {
            Object* unreached = object;
            object = object->next;
            if(previous != NULL)
            {
                previous->next = object;
            }
            else
            {
                state->gcstate.linkedobjects = object;
            }

            lox_object_release(state, unreached);
        }
    }
}

void lox_gcmem_collectgarbage(LoxState* state)
{
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = state->gcstate.bytesallocd;
#endif

    lox_gcmem_markroots(state);
    lox_gcmem_tracerefs(state);
    lox_hashtable_remwhite(state, &state->strings);
    lox_gcmem_sweep(state);

    state->gcstate.nextgc = state->gcstate.bytesallocd * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n", before - state->gcstate.bytesallocd, before, state->gcstate.bytesallocd, state->gcstate.nextgc);
#endif
}

void lox_vm_gcfreelinkedobjects(LoxState* state)
{
    Object* object = state->gcstate.linkedobjects;
    while(object != NULL)
    {
        Object* next = object->next;
        lox_object_release(state, object);
        object = next;
    }

    free(state->gcstate.graystack);
}

void lox_gcmem_markhashtable(LoxState* state, HashTable* table)
{
    for(int i = 0; i < table->capacity; i++)
    {
        HashEntry* entry = &table->entries[i];
        lox_gcmem_markobject(state, (Object*)entry->key);
        lox_gcmem_markvalue(state, entry->value);
    }
}


void lox_gcmem_markobject(LoxState* state, Object* object)
{
    if(object == NULL)
        return;
    if(object->ismarked)
        return;

#ifdef DEBUG_LOG_GC
    lox_writer_writeformat(state->stderrwriter, "markobject: at %p: ", (void*)object);
    lox_writer_printvalue(state->stderrwriter, OBJ_VAL(object), true);
    lox_writer_writestring(state->stderrwriter, "\n");
#endif

    object->ismarked = true;

    if(state->gcstate.graycap < state->gcstate.graycount + 1)
    {
        state->gcstate.graycap = GROW_CAPACITY(state->gcstate.graycap);
        state->gcstate.graystack = (Object**)realloc(state->gcstate.graystack, sizeof(Object*) * state->gcstate.graycap);

        if(state->gcstate.graystack == NULL)
            exit(1);
    }

    state->gcstate.graystack[state->gcstate.graycount++] = object;
}

void lox_gcmem_markvalue(LoxState* state, Value value)
{
    if(IS_OBJ(value))
        lox_gcmem_markobject(state, AS_OBJ(value));
}

void lox_gcmem_markvalarray(LoxState* state, ValArray* array)
{
    size_t i;
    for(i = 0; i < lox_valarray_count(array); i++)
    {
        lox_gcmem_markvalue(state, array->values[i]);
    }
}

void lox_gcmem_blackenobj(LoxState* state, Object* object)
{
#ifdef DEBUG_LOG_GC
    lox_writer_writeformat(state->stderrwriter, "blackenobj: at %p: ", (void*)object);
    lox_writer_printvalue(state->stderrwriter, OBJ_VAL(object), true);
    lox_writer_writestring(state->stderrwriter, "\n");
#endif

    switch(object->type)
    {
        case OBJ_BOUNDMETHOD:
            {
                ObjBoundMethod* bound = (ObjBoundMethod*)object;
                lox_gcmem_markvalue(state, bound->receiver);
                lox_gcmem_markobject(state, (Object*)bound->method);
            }
            break;
        case OBJ_CLASS:
            {
                ObjClass* klass = (ObjClass*)object;
                lox_gcmem_markobject(state, (Object*)klass->name);
                lox_gcmem_markhashtable(state, &klass->methods);
            }
            break;

        case OBJ_CLOSURE:
            {
                ObjClosure* closure = (ObjClosure*)object;
                lox_gcmem_markobject(state, (Object*)closure->innerfn);
                for(int i = 0; i < closure->upvaluecount; i++)
                {
                    lox_gcmem_markobject(state, (Object*)closure->upvalues[i]);
                }
            }
            break;
        case OBJ_FUNCTION:
            {
                ObjFunction* ofn = (ObjFunction*)object;
                lox_gcmem_markobject(state, (Object*)ofn->name);
                lox_gcmem_markvalarray(state, &ofn->chunk.constants);
            }
            break;
        case OBJ_INSTANCE:
            {
                ObjInstance* instance = (ObjInstance*)object;
                lox_gcmem_markobject(state, (Object*)instance->klass);
                lox_gcmem_markhashtable(state, &instance->fields);
            }
            break;
        case OBJ_UPVALUE:
            {
                lox_gcmem_markvalue(state, ((ObjUpvalue*)object)->closed);
            }
            break;
        case OBJ_ARRAY:
            {
                ObjArray* oa;
                oa = (ObjArray*)object;
                lox_gcmem_markvalarray(state, &oa->vala);
            }
            break;
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}


void lox_object_release(LoxState* state, Object* object)
{
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif

    switch(object->type)
    {
        case OBJ_BOUNDMETHOD:
            {
                FREE(state, ObjBoundMethod, object);
            }
            break;
        case OBJ_CLASS:
            {
                ObjClass* klass = (ObjClass*)object;
                lox_hashtable_free(state, &klass->methods);
                FREE(state, ObjClass, object);
            }
            break;
        case OBJ_CLOSURE:
            {
                ObjClosure* closure = (ObjClosure*)object;
                FREE_ARRAY(state, ObjUpvalue*, closure->upvalues, closure->upvaluecount);
                FREE(state, ObjClosure, object);
            }
            break;

        case OBJ_FUNCTION:
        {
            ObjFunction* ofn = (ObjFunction*)object;
            lox_chunk_free(state, &ofn->chunk);
            FREE(state, ObjFunction, object);
            break;
        }
        case OBJ_INSTANCE:
        {
            ObjInstance* instance = (ObjInstance*)object;
            lox_hashtable_free(state, &instance->fields);
            FREE(state, ObjInstance, object);
            break;
        }
        case OBJ_NATIVE:
            {
                FREE(state, ObjNative, object);
            }
            break;
        case OBJ_STRING:
            {
                ObjString* string = (ObjString*)object;
                lox_string_release(state, string);
                FREE(state, ObjString, object);
            }
            break;
        case OBJ_ARRAY:
            {
                ObjArray* arr;
                arr = (ObjArray*)object;
                lox_valarray_free(&arr->vala);
                FREE(state, ObjArray, object);
            }
            break;
        case OBJ_UPVALUE:
            {
                FREE(state, ObjUpvalue, object);
            }
            break;
    }
}



#define lox_valarray_computenextgrow(size) \
    ((size) ? ((size) << 1) : 1)


uint32_t lox_util_hashstring(const char* key, size_t length)
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


Strbuf* lox_strbuf_make(LoxState* state)
{
    Strbuf* sbuf;
    sbuf = (Strbuf*)malloc(sizeof(Strbuf));
    sbuf->pvm = state;
    sbuf->length = 0;
    sbuf->data = NULL;
    sbuf->capacity = 0;
    return sbuf;
}

void lox_strbuf_release(LoxState* state, Strbuf* sb)
{
    FREE_ARRAY(state, char, sb->data, sb->length + 1);
}

bool lox_strbuf_append(Strbuf* sb, const char* extstr, size_t extlen)
{
    enum {
        string_chunk_size = 52,
    };
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
                    fprintf(stderr, "lox_strbuf_append: realloc(%ld) failed!\n", needsz);
                }
                sb->data = temp;
                sb->capacity = sb->length + extlen + string_chunk_size;
            }
            if(sb->capacity >= sb->length + extlen + 2)
            {
                selfpos = sb->length;
                extpos = 0;
                //for(selfpos = sb->length, extpos = 0; extpos < extlen; selfpos++, extpos++)
                //while((extpos < extlen) && (extpos < extlen))
                while(true)
                {
                    if((extpos + 0) == extlen)
                    {
                        break;
                    }
                    sb->data[selfpos] = extstr[extpos];
                    sb->length++;
                    selfpos++;
                    extpos++;
                }
                sb->data[selfpos] = 0;
            }
        }
        return true;
    }
    return false;
}


Writer* lox_writer_make(LoxState* state)
{
    Writer* wr;
    (void)state;
    wr = (Writer*)malloc(sizeof(Writer));
    if(!wr)
    {
        fprintf(stderr, "cannot allocate Writer\n");
    }
    wr->isstring = false;
    wr->shouldclose = false;
    wr->strbuf = NULL;
    wr->handle = NULL;
    return wr;
}

void lox_writer_release(LoxState* state, Writer* wr)
{
    if(wr->isstring)
    {
        lox_strbuf_release(state, wr->strbuf);
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

Writer* lox_writer_makeio(LoxState* state, FILE* fh, bool shouldclose)
{
    Writer* wr;
    wr = lox_writer_make(state);
    wr->handle = fh;
    wr->shouldclose = shouldclose;
    return wr;
}

Writer* lox_writer_makestring(LoxState* state)
{
    Writer* wr;
    wr = lox_writer_make(state);
    wr->isstring = true;
    wr->strbuf = lox_strbuf_make(state);
    return wr;
}

void lox_writer_writestringl(Writer* wr, const char* estr, size_t elen)
{
    if(wr->isstring)
    {
        lox_strbuf_append(wr->strbuf, estr, elen);
    }
    else
    {
        fwrite(estr, sizeof(char), elen, wr->handle);
        fflush(wr->handle);
    }
}

void lox_writer_writestring(Writer* wr, const char* estr)
{
    return lox_writer_writestringl(wr, estr, strlen(estr));
}

void lox_writer_writechar(Writer* wr, int b)
{
    char ch;
    if(wr->isstring)
    {
        ch = b;
        lox_writer_writestringl(wr, &ch, 1);
    }
    else
    {
        fputc(b, wr->handle);
        fflush(wr->handle);
    }
}

void lox_writer_writeescapedchar(Writer* wr, int ch)
{
    switch(ch)
    {
        case '\'':
            {
                lox_writer_writestring(wr, "\\\'");
            }
            break;
        case '\"':
            {
                lox_writer_writestring(wr, "\\\"");
            }
            break;
        case '\\':
            {
                lox_writer_writestring(wr, "\\\\");
            }
            break;
        case '\b':
            {
                lox_writer_writestring(wr, "\\b");
            }
            break;
        case '\f':
            {
                lox_writer_writestring(wr, "\\f");
            }
            break;
        case '\n':
            {
                lox_writer_writestring(wr, "\\n");
            }
            break;
        case '\r':
            {
                lox_writer_writestring(wr, "\\r");
            }
            break;
        case '\t':
            {
                lox_writer_writestring(wr, "\\t");
            }
            break;
        case 0:
            {
                lox_writer_writestring(wr, "\\0");
            }
            break;
        default:
            {
                lox_writer_writeformat(wr, "\\x%02x", (unsigned char)ch);
            }
            break;
    }
}

void lox_writer_writequotedstring(Writer* wr, const char* str, size_t len, bool withquot)
{
    int bch;
    size_t i;
    bch = 0;
    if(withquot)
    {
        lox_writer_writechar(wr, '"');
    }
    for(i=0; i<len; i++)
    {
        bch = str[i];
        if((bch < 32) || (bch > 127) || (bch == '\"') || (bch == '\\'))
        {
            lox_writer_writeescapedchar(wr, bch);
        }
        else
        {
            lox_writer_writechar(wr, bch);
        }
    }
    if(withquot)
    {
        lox_writer_writechar(wr, '"');
    }
}

void lox_writer_vwritefmttostring(Writer* wr, const char* fmt, va_list va)
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
    lox_writer_writestringl(wr, buf, wsz);
    free(buf);
}

void lox_writer_vwriteformat(Writer* wr, const char* fmt, va_list va)
{
    if(wr->isstring)
    {
        return lox_writer_vwritefmttostring(wr, fmt, va);
    }
    else
    {
        vfprintf(wr->handle, fmt, va);
        fflush(wr->handle);
    }
}

void lox_writer_writeformat(Writer* wr, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    lox_writer_vwriteformat(wr, fmt, va);
    va_end(va);
}

void lox_writer_printfunction(Writer* wr, ObjFunction* ofn)
{
    if(ofn->name == NULL)
    {
        lox_writer_writestring(wr, "<script>");
    }
    else
    {
        lox_writer_writeformat(wr, "<fn %s>", ofn->name->sbuf->data);
    }
}

void lox_writer_printarray(Writer* wr, ObjArray* arr)
{
    size_t i;
    size_t asz;
    asz = lox_valarray_size(&arr->vala);
    lox_writer_writestring(wr, "[");
    for(i=0; i<asz; i++)
    {
        lox_writer_writeformat(wr, "%ld:", i);
        lox_writer_printvalue(wr, arr->vala.values[i], true);
        if((i+1) < asz)
        {
            lox_writer_writestring(wr, ",");
        }
    }
    lox_writer_writestring(wr, "]");
}

void lox_writer_printstring(Writer* wr, ObjString* os, bool fixstring)
{
    size_t len;
    const char* sp;
    len = os->sbuf->length;
    sp = os->sbuf->data;
    if(fixstring)
    {
        lox_writer_writequotedstring(wr, sp, len, true);
    }
    else
    {
        lox_writer_writestringl(wr, sp, len);
    }
}

void lox_writer_printobject(Writer* wr, Value value, bool fixstring)
{
    switch(OBJ_TYPE(value))
    {
        case OBJ_BOUNDMETHOD:
            {
                lox_writer_printfunction(wr, AS_BOUND_METHOD(value)->method->innerfn);
            }
            break;
        case OBJ_CLASS:
            {
                lox_writer_writeformat(wr, "<class '%s'>", AS_CLASS(value)->name->sbuf->data);
            }
            break;
        case OBJ_CLOSURE:
            {
                lox_writer_printfunction(wr, AS_CLOSURE(value)->innerfn);
            }
            break;
        case OBJ_FUNCTION:
            {
                lox_writer_printfunction(wr, AS_FUNCTION(value));
            }
            break;
        case OBJ_INSTANCE:
            {
                lox_writer_writeformat(wr, "<instance '%s'>", AS_INSTANCE(value)->klass->name->sbuf->data);
            }
            break;
        case OBJ_NATIVE:
            {
                lox_writer_writeformat(wr, "<nativefn>");
            }
            break;
        case OBJ_STRING:
            {
                lox_writer_printstring(wr, AS_STRING(value), fixstring);
            }
            break;
        case OBJ_UPVALUE:
            {
                lox_writer_writeformat(wr, "<upvalue>");
            }
            break;
        case OBJ_ARRAY:
            {
                lox_writer_printarray(wr, AS_ARRAY(value));
            }
            break;
    }
}

void lox_writer_printvalue(Writer* wr, Value value, bool fixstring)
{
    switch(value.type)
    {
        case VAL_BOOL:
            lox_writer_writestring(wr, AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NIL:
            lox_writer_writestring(wr, "nil");
            break;
        case VAL_NUMBER:
            lox_writer_writeformat(wr, "%g", AS_NUMBER(value));
            break;
        case VAL_OBJ:
            lox_writer_printobject(wr, value, fixstring);
            break;
    }
}

const char* lox_writer_objecttypename(Object* obj)
{
    switch(obj->type)
    {
        case OBJ_BOUNDMETHOD:
            {
                return "boundmethod";
            }
            break;
        case OBJ_CLASS:
            {
                return "class";
            }
            break;
        case OBJ_CLOSURE:
            {
                return "closure";
            }
            break;
        case OBJ_FUNCTION:
            {
                return "function";
            }
            break;
        case OBJ_INSTANCE:
            {
                return "instance";
            }
            break;
        case OBJ_NATIVE:
            {
                return "nativefunction";
            }
            break;
        case OBJ_STRING:
            {
                return "string";
            }
            break;
        case OBJ_UPVALUE:
            {
                return "upvalue";
            }
            break;
        case OBJ_ARRAY:
            {
                return "array";
            }
            break;
    }
    return "?unknownobject?";
}

const char* lox_writer_valuetypename(Value value)
{
    switch(value.type)
    {
        case VAL_BOOL:
            return "bool";
            break;
        case VAL_NIL:
            return "nil";
            break;
        case VAL_NUMBER:
            return "string";
            break;
        case VAL_OBJ:
            return lox_writer_objecttypename(AS_OBJ(value));
            break;
    }
    return "?unknownvalue?";
}


bool lox_value_equal(Value a, Value b)
{
    if(a.type != b.type)
    {
        return false;
    }
    switch(a.type)
    {
        case VAL_BOOL:
            {
                return AS_BOOL(a) == AS_BOOL(b);
            }
            break;
        case VAL_NIL:
            {
                return true;
            }
            break;
        case VAL_NUMBER:
            {
                return AS_NUMBER(a) == AS_NUMBER(b);
            }
            break;
            /* Strings strings-equal < Hash Tables equal */
        case VAL_OBJ:
            {
                ObjString* aos;
                ObjString* bos;
                if(IS_STRING(a) && IS_STRING(b))
                {
                    aos = AS_STRING(a);
                    bos = AS_STRING(b);
                    if(aos->sbuf->length == bos->sbuf->length)
                    {
                        if(memcmp(aos->sbuf->data, bos->sbuf->data, aos->sbuf->length) == 0)
                        {
                            return true;
                        }
                    }
                    return false;
                }
                return AS_OBJ(a) == AS_OBJ(b);
            }
            break;
        default:
            break;
    }
    return false;
}



void lox_valarray_init(LoxState* state, ValArray* array)
{
    array->pvm = state;
    array->values = NULL;
    array->capacity = 0;
    array->size = 0;
}

size_t lox_valarray_count(ValArray* array)
{
    return array->size;
}

size_t lox_valarray_size(ValArray* array)
{
    return array->size;
}

Value lox_valarray_at(ValArray* array, size_t i)
{
    return array->values[i];
}

bool lox_valarray_grow(ValArray* arr, size_t count)
{
    size_t nsz;
    void* p1;
    void* newbuf;
    nsz = count * sizeof(Value);
    p1 = arr->values;
    newbuf = realloc(p1, nsz);
    if(newbuf == NULL)
    {
        return false;
    }
    arr->values = newbuf;
    arr->capacity = count;
    return true;
}

bool lox_valarray_push(ValArray* arr, Value value)
{
    size_t cap;
    cap = arr->capacity;
    if(cap <= arr->size)
    {
        if(!lox_valarray_grow(arr, lox_valarray_computenextgrow(cap)))
        {
            return false;
        }
    }
    arr->values[arr->size] = (value);
    arr->size++;
    return true;
}

bool lox_valarray_insert(ValArray* arr, size_t pos, Value val)
{
    size_t i;
    size_t asz;
    size_t oldcap;
    bool shouldinc;
    shouldinc = false;
    oldcap = arr->capacity;
    if (oldcap <= arr->size)
    {
        if(!lox_valarray_grow(arr, lox_valarray_computenextgrow(oldcap)))
        {
            return false;
        }
    }
    if((pos > arr->size) || (arr->size == 0))
    {
        asz = arr->size;
        for(i=0; i<(pos+1); i++)
        {
            lox_valarray_push(arr, NIL_VAL);
        }
    }
    arr->values[pos] = val;
    if(shouldinc)
    {
        arr->size++;
    }
    return true;
}

bool lox_valarray_erase(ValArray* arr, size_t idx)
{
    size_t i;
    size_t ni;
    size_t osz;
    osz = arr->size;
    if(idx < osz)
    {
        //arr->size = osz - 1;
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

Value lox_valarray_pop(ValArray* arr)
{
    arr->size--;
    return arr->values[arr->size + 1];
}

void lox_valarray_free(ValArray* arr)
{
    LoxState* state;
    state = arr->pvm;
    FREE_ARRAY(state, Value, arr->values, arr->capacity);
    lox_valarray_init(state, arr);
}

Object* lox_object_allocobj(LoxState* state, size_t size, ObjType type)
{
    Object* baseobj;
    baseobj = (Object*)lox_mem_realloc(state, NULL, 0, size);
    baseobj->type = type;
    baseobj->ismarked = false;
    baseobj->next = state->gcstate.linkedobjects;
    state->gcstate.linkedobjects = baseobj;
#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)baseobj, size, type);
#endif
    return baseobj;
}

ObjBoundMethod* lox_object_makeboundmethod(LoxState* state, Value receiver, ObjClosure* method)
{
    ObjBoundMethod* obj;
    obj = (ObjBoundMethod*)lox_object_allocobj(state, sizeof(ObjBoundMethod), OBJ_BOUNDMETHOD);
    obj->receiver = receiver;
    obj->method = method;
    return obj;
}

ObjClass* lox_object_makeclass(LoxState* state, ObjString* name)
{
    ObjClass* obj;
    obj = (ObjClass*)lox_object_allocobj(state, sizeof(ObjClass), OBJ_CLASS);
    obj->name = name;// [klass]
    lox_hashtable_init(&obj->methods);
    return obj;
}

ObjClosure* lox_object_makeclosure(LoxState* state, ObjFunction* ofn)
{
    int i;
    ObjClosure* closure;
    ObjUpvalue** upvals;
    upvals = ALLOCATE(state, ObjUpvalue*, ofn->upvaluecount);
    for(i = 0; i < ofn->upvaluecount; i++)
    {
        upvals[i] = NULL;
    }
    closure = (ObjClosure*)lox_object_allocobj(state, sizeof(ObjClosure), OBJ_CLOSURE);
    closure->innerfn = ofn;
    closure->upvalues = upvals;
    closure->upvaluecount = ofn->upvaluecount;
    return closure;
}

ObjFunction* lox_object_makefunction(LoxState* state)
{
    ObjFunction* obj;
    obj = (ObjFunction*)lox_object_allocobj(state, sizeof(ObjFunction), OBJ_FUNCTION);
    obj->arity = 0;
    obj->upvaluecount = 0;
    obj->name = NULL;
    lox_chunk_init(state, &obj->chunk);
    return obj;
}

ObjInstance* lox_object_makeinstance(LoxState* state, ObjClass* klass)
{
    ObjInstance* obj;
    obj = (ObjInstance*)lox_object_allocobj(state, sizeof(ObjInstance), OBJ_INSTANCE);
    obj->klass = klass;
    lox_hashtable_init(&obj->fields);
    return obj;
}

ObjNative* lox_object_makenative(LoxState* state, NativeFN nat)
{
    ObjNative* obj;
    obj = (ObjNative*)lox_object_allocobj(state, sizeof(ObjNative), OBJ_NATIVE);
    obj->natfunc = nat;
    return obj;
}

ObjString* lox_string_allocfromstrbuf(LoxState* state, Strbuf* sbuf, uint32_t hash)
{
    ObjString* rs;
    rs = (ObjString*)lox_object_allocobj(state, sizeof(ObjString), OBJ_STRING);
    rs->sbuf = sbuf;
    rs->hash = hash;
    lox_vm_stackpush(state, OBJ_VAL(rs));
    lox_hashtable_set(state, &state->strings, rs, NIL_VAL);
    lox_vm_stackpop(state);
    return rs;
}

ObjString* lox_string_allocate(LoxState* state, const char* estr, size_t elen, uint32_t hash)
{
    Strbuf* sbuf;
    sbuf = lox_strbuf_make(state);
    lox_strbuf_append(sbuf, estr, elen);
    return lox_string_allocfromstrbuf(state, sbuf, hash);
}

ObjString* lox_string_take(LoxState* state, char* estr, size_t elen)
{
    uint32_t hash;
    ObjString* rs;
    hash = lox_util_hashstring(estr, elen);
    rs = lox_hashtable_findstring(state, &state->strings, estr, elen, hash);
    if(rs == NULL)
    {
        rs = lox_string_copy(state, (const char*)estr, elen);
    }
    FREE_ARRAY(state, char, estr, elen + 1);
    return rs;
}

ObjString* lox_string_copy(LoxState* state, const char* estr, int elen)
{
    uint32_t hash;
    ObjString* rs;
    hash = lox_util_hashstring(estr, elen);
    rs = lox_hashtable_findstring(state, &state->strings, estr, elen, hash);
    if(rs != NULL)
    {
        return rs;
    }
    rs = lox_string_allocate(state, estr, elen, hash);
    return rs;
}

void lox_string_release(LoxState* state, ObjString* os)
{
    lox_strbuf_release(state, os->sbuf);
    free(os->sbuf);
}

bool lox_string_append(ObjString* os, const char* extstr, size_t extlen)
{
    return lox_strbuf_append(os->sbuf, extstr, extlen);
}

ObjArray* lox_array_make(LoxState* state)
{
    ObjArray* oa;
    oa = (ObjArray*)lox_object_allocobj(state, sizeof(ObjArray), OBJ_ARRAY);
    oa->pvm = state;
    lox_valarray_init(state, &oa->vala);
    oa->vala.size = 0;
    return oa;
}

bool lox_array_push(ObjArray* arr, Value val)
{
    lox_valarray_push(&arr->vala, val);
    return true;
}

ObjUpvalue* lox_object_makeupvalue(LoxState* state, Value* pslot)
{
    ObjUpvalue* upvalue = (ObjUpvalue*)lox_object_allocobj(state, sizeof(ObjUpvalue), OBJ_UPVALUE);
    upvalue->closed = NIL_VAL;
    upvalue->location = pslot;
    upvalue->next = NULL;
    return upvalue;
}


void lox_hashtable_init(HashTable* table)
{
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void lox_hashtable_free(LoxState* state, HashTable* table)
{
    FREE_ARRAY(state, HashEntry, table->entries, table->capacity);
    lox_hashtable_init(table);
}

// NOTE: The "optimization" chapter has a manual copy of this function.
// If you change it here, make sure to update that copy.
HashEntry* lox_hashtable_findentry(HashEntry* entries, int capacity, ObjString* key)
{
    uint32_t index;
    HashEntry* entry;
    HashEntry* tombstone;
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
                    if(IS_NIL(entry->value))
                    {
                        // Empty entry.
                        return tombstone != NULL ? tombstone : entry;
                    }
                    else
                    {
                        // We found a tombstone.
                        if(tombstone == NULL)
                            tombstone = entry;
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

bool lox_hashtable_get(LoxState* state, HashTable* table, ObjString* key, Value* value)
{
    HashEntry* entry;
    (void)state;
    if(table->count == 0)
    {
        return false;
    }
    entry = lox_hashtable_findentry(table->entries, table->capacity, key);
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

void lox_hashtable_adjustcap(LoxState* state, HashTable* table, int capacity)
{
    int i;
    HashEntry* dest;
    HashEntry* entry;
    HashEntry* entries;
    entries = ALLOCATE(state, HashEntry, capacity);
    for(i = 0; i < capacity; i++)
    {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }
    table->count = 0;
    for(i = 0; i < table->capacity; i++)
    {
        entry = &table->entries[i];
        if(entry->key == NULL)
        {
            continue;
        }
        dest = lox_hashtable_findentry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(state, HashEntry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool lox_hashtable_set(LoxState* state, HashTable* table, ObjString* key, Value value)
{
    int capacity;
    bool isnewkey;
    HashEntry* entry;
    if(table->count + 1 > table->capacity * TABLE_MAX_LOAD)
    {
        capacity = GROW_CAPACITY(table->capacity);
        lox_hashtable_adjustcap(state, table, capacity);
    }
    entry = lox_hashtable_findentry(table->entries, table->capacity, key);
    isnewkey = entry->key == NULL;
    if(isnewkey && IS_NIL(entry->value))
    {
        table->count++;
    }
    entry->key = key;
    entry->value = value;
    return isnewkey;
}

bool lox_hashtable_delete(LoxState* state, HashTable* table, ObjString* key)
{
    HashEntry* entry;
    (void)state;
    if(table->count == 0)
    {
        return false;
    }
    // Find the entry.
    entry = lox_hashtable_findentry(table->entries, table->capacity, key);
    if(entry->key == NULL)
    {
        return false;
    }
    // Place a tombstone in the entry.
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

void lox_hashtable_addall(LoxState* state, HashTable* from, HashTable* to)
{
    int i;
    HashEntry* entry;
    for(i = 0; i < from->capacity; i++)
    {
        entry = &from->entries[i];
        if(entry->key != NULL)
        {
            lox_hashtable_set(state, to, entry->key, entry->value);
        }
    }
}

ObjString* lox_hashtable_findstring(LoxState* state, HashTable* table, const char* estr, int elen, uint32_t hash)
{
    uint32_t index;
    HashEntry* entry;
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
            if(IS_NIL(entry->value))
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

void lox_hashtable_remwhite(LoxState* state, HashTable* table)
{
    int i;
    HashEntry* entry;
    for(i = 0; i < table->capacity; i++)
    {
        entry = &table->entries[i];
        if(entry->key != NULL && !entry->key->obj.ismarked)
        {
            lox_hashtable_delete(state, table, entry->key);
        }
    }
}

void lox_chunk_init(LoxState* state, Chunk* chunk)
{
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    lox_valarray_init(state, &chunk->constants);
}

void lox_chunk_free(LoxState* state, Chunk* chunk)
{
    FREE_ARRAY(state, int32_t, chunk->code, chunk->capacity);
    FREE_ARRAY(state, int, chunk->lines, chunk->capacity);
    lox_valarray_free(&chunk->constants);
    lox_chunk_init(state, chunk);
}

void lox_chunk_pushbyte(LoxState* state, Chunk* chunk, int32_t byte, int line)
{
    int oldcap;
    if(chunk->capacity < chunk->count + 1)
    {
        oldcap = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldcap);
        chunk->code = GROW_ARRAY(state, int32_t, chunk->code, oldcap, chunk->capacity);
        chunk->lines = GROW_ARRAY(state, int, chunk->lines, oldcap, chunk->capacity);
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int lox_chunk_pushconst(LoxState* state, Chunk* chunk, Value value)
{
    lox_vm_stackpush(state, value);
    lox_valarray_push(&chunk->constants, value);
    lox_vm_stackpop(state);
    return lox_valarray_count(&chunk->constants) - 1;
}

void lox_chunk_disasm(LoxState* state, Writer* wr, Chunk* chunk, const char* name)
{
    int offset;
    lox_writer_writeformat(wr, "== %s ==\n", name);
    for(offset = 0; offset < chunk->count;)
    {
        offset = lox_dbg_dumpdisasm(state, wr, chunk, offset);
    }
}

int lox_dbg_dumpconstinstr(LoxState* state, Writer* wr, const char* name, Chunk* chunk, int offset)
{
    Value val;
    int32_t constant;
    (void)state;
    val = NIL_VAL;
    constant = chunk->code[offset + 1];
    if(chunk->constants.size > 0)
    {
        val = chunk->constants.values[constant];
    }
    lox_writer_writeformat(wr, "%-16s %4d '", name, constant);
    lox_writer_printvalue(wr, val, true);
    lox_writer_writestring(wr, "'\n");
    return offset + 2;
}

int lox_dbg_dumpinvokeinstr(LoxState* state, Writer* wr, const char* name, Chunk* chunk, int offset)
{
    int32_t argc;
    int32_t constant;
    (void)state;
    constant = chunk->code[offset + 1];
    argc = chunk->code[offset + 2];
    lox_writer_writeformat(wr, "%-16s (%d args) %4d {", name, argc, constant);
    lox_writer_printvalue(wr, chunk->constants.values[constant], true);
    lox_writer_writestring(wr, "}\n");
    return offset + 3;
}

int lox_dbg_dumpsimpleinstr(LoxState* state, Writer* wr, const char* name, int offset)
{
    (void)state;
    lox_writer_writeformat(wr, "%s\n", name);
    return offset + 1;
}

int lox_dbg_dumpbyteinstr(LoxState* state, Writer* wr, const char* name, Chunk* chunk, int offset)
{
    int32_t islot;
    (void)state;
    islot = chunk->code[offset + 1];
    lox_writer_writeformat(wr, "%-16s %4d\n", name, islot);
    return offset + 2;// [debug]
}

int lox_dbg_dumpjumpinstr(LoxState* state, Writer* wr, const char* name, int sign, Chunk* chunk, int offset)
{
    uint16_t jump;
    (void)state;
    jump= (uint16_t)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    lox_writer_writeformat(wr, "%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

int lox_dbg_dumpclosure(LoxState* state, Writer* wr, Chunk* chunk, int offset)
{
    int j;
    int index;
    int islocal;
    int32_t constant;
    ObjFunction* fn;
    (void)state;
    offset++;
    constant = chunk->code[offset++];
    lox_writer_writeformat(wr, "%-16s %4d ", "OP_CLOSURE", constant);
    lox_writer_printvalue(wr, chunk->constants.values[constant], true);
    lox_writer_writestring(wr, "\n");
    fn = AS_FUNCTION(chunk->constants.values[constant]);
    for(j = 0; j < fn->upvaluecount; j++)
    {
        islocal = chunk->code[offset++];
        index = chunk->code[offset++];
        lox_writer_writeformat(wr, "%04d      |                     %s %d\n", offset - 2, islocal ? "local" : "upvalue", index);
    }
    return offset;
}

int lox_dbg_dumpdisasm(LoxState* state, Writer* wr, Chunk* chunk, int offset)
{
    int32_t instruction;
    (void)state;
    lox_writer_writeformat(wr, "%04d ", offset);
    if((offset > 0) && (chunk->lines[offset] == chunk->lines[offset - 1]))
    {
        lox_writer_writeformat(wr, "   | ");
    }
    else
    {
        lox_writer_writeformat(wr, "%4d ", chunk->lines[offset]);
    }
    instruction = chunk->code[offset];
    switch(instruction)
    {
        case OP_PUSHCONST:
            return lox_dbg_dumpconstinstr(state, wr, "OP_PUSHCONST", chunk, offset);
        case OP_PUSHONE:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_PUSHONE", offset);
        case OP_PUSHNIL:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_PUSHNIL", offset);
        case OP_PUSHTRUE:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_PUSHTRUE", offset);
        case OP_PUSHFALSE:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_PUSHFALSE", offset);
        case OP_MAKEARRAY:
            return lox_dbg_dumpconstinstr(state, wr, "OP_MAKEARRAY", chunk, offset);
        case OP_INDEXGET:
            return lox_dbg_dumpbyteinstr(state, wr, "OP_INDEXGET", chunk, offset);
        case OP_POP:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_POP", offset);
        case OP_LOCALGET:
            return lox_dbg_dumpbyteinstr(state, wr, "OP_LOCALGET", chunk, offset);
        case OP_LOCALSET:
            return lox_dbg_dumpbyteinstr(state, wr, "OP_LOCALSET", chunk, offset);
        case OP_GLOBALGET:
            return lox_dbg_dumpconstinstr(state, wr, "OP_GLOBALGET", chunk, offset);
        case OP_GLOBALDEFINE:
            return lox_dbg_dumpconstinstr(state, wr, "OP_GLOBALDEFINE", chunk, offset);
        case OP_GLOBALSET:
            return lox_dbg_dumpconstinstr(state, wr, "OP_GLOBALSET", chunk, offset);
        case OP_UPVALGET:
            return lox_dbg_dumpbyteinstr(state, wr, "OP_UPVALGET", chunk, offset);
        case OP_UPVALSET:
            return lox_dbg_dumpbyteinstr(state, wr, "OP_UPVALSET", chunk, offset);
        case OP_PROPERTYGET:
            return lox_dbg_dumpconstinstr(state, wr, "OP_PROPERTYGET", chunk, offset);
        case OP_PROPERTYSET:
            return lox_dbg_dumpconstinstr(state, wr, "OP_PROPERTYSET", chunk, offset);
        case OP_INSTGETSUPER:
            return lox_dbg_dumpconstinstr(state, wr, "OP_INSTGETSUPER", chunk, offset);
        case OP_EQUAL:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_EQUAL", offset);
        case OP_PRIMGREATER:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_PRIMGREATER", offset);
        case OP_PRIMLESS:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_PRIMLESS", offset);
        case OP_PRIMADD:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_PRIMADD", offset);
        case OP_PRIMSUBTRACT:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_PRIMSUBTRACT", offset);
        case OP_PRIMMULTIPLY:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_PRIMMULTIPLY", offset);
        case OP_PRIMDIVIDE:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_PRIMDIVIDE", offset);
        case OP_PRIMNOT:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_PRIMNOT", offset);
        case OP_PRIMNEGATE:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_PRIMNEGATE", offset);
        case OP_DEBUGPRINT:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_PRINT", offset);
        case OP_JUMPNOW:
            return lox_dbg_dumpjumpinstr(state, wr, "OP_JUMPNOW", 1, chunk, offset);
        case OP_JUMPIFFALSE:
            return lox_dbg_dumpjumpinstr(state, wr, "OP_JUMPIFFALSE", 1, chunk, offset);
        case OP_LOOP:
            return lox_dbg_dumpjumpinstr(state, wr, "OP_LOOP", -1, chunk, offset);
        case OP_CALL:
            return lox_dbg_dumpbyteinstr(state, wr, "OP_CALL", chunk, offset);
        case OP_INSTTHISINVOKE:
            return lox_dbg_dumpinvokeinstr(state, wr, "OP_INSTTHISINVOKE", chunk, offset);
        case OP_INSTSUPERINVOKE:
            return lox_dbg_dumpinvokeinstr(state, wr, "OP_INSTSUPERINVOKE", chunk, offset);
        case OP_CLOSURE:
            {
                offset = lox_dbg_dumpclosure(state, wr, chunk, offset);
                return offset;
            }
        case OP_UPVALCLOSE:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_UPVALCLOSE", offset);
        case OP_RETURN:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_RETURN", offset);
        case OP_CLASS:
            return lox_dbg_dumpconstinstr(state, wr, "OP_CLASS", chunk, offset);
        case OP_INHERIT:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_INHERIT", offset);
        case OP_METHOD:
            return lox_dbg_dumpconstinstr(state, wr, "OP_METHOD", chunk, offset);
        case OP_HALTVM:
            return lox_dbg_dumpsimpleinstr(state, wr, "OP_HALTVM", offset);
        default:
            lox_writer_writeformat(wr, "!!!!unknown opcode %d!!!!\n", instruction);
            return offset + 1;
    }
}

void lox_lex_init(LoxState* state, AstScanner* scn, const char* source)
{
    scn->pvm = state;
    scn->start = source;
    scn->current = source;
    scn->line = 1;
}

bool lox_lex_isalpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool lox_lex_isdigit(char c)
{
    return c >= '0' && c <= '9';
}

bool lox_lex_isatend(AstScanner* scn)
{
    return *scn->current == '\0';
}

char lox_lex_advance(AstScanner* scn)
{
    scn->current++;
    return scn->current[-1];
}

char lox_lex_peekcurrent(AstScanner* scn)
{
    return *scn->current;
}

char lox_lex_peeknext(AstScanner* scn)
{
    if(lox_lex_isatend(scn))
    {
        return '\0';
    }
    return scn->current[1];
}

bool lox_lex_match(AstScanner* scn, char expected)
{
    if(lox_lex_isatend(scn))
        return false;
    if(*scn->current != expected)
        return false;
    scn->current++;
    return true;
}

AstToken lox_lex_maketoken(AstScanner* scn, AstTokType type)
{
    AstToken token;
    token.type = type;
    token.start = scn->start;
    token.length = (int)(scn->current - scn->start);
    token.line = scn->line;
    return token;
}

AstToken lox_lex_makeerrortoken(AstScanner* scn, const char* message)
{
    AstToken token;
    token.type = TOK_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scn->line;
    return token;
}

void lox_lex_skipspace(AstScanner* scn)
{
    char c;
    for(;;)
    {
        c = lox_lex_peekcurrent(scn);
        switch(c)
        {
            case ' ':
            case '\r':
            case '\t':
                {
                    lox_lex_advance(scn);
                }
                break;
            case '\n':
                {
                    scn->line++;
                    lox_lex_advance(scn);
                }
                break;
            case '/':
                {
                    if(lox_lex_peeknext(scn) == '/')
                    {
                        // A comment goes until the end of the line.
                        while(lox_lex_peekcurrent(scn) != '\n' && !lox_lex_isatend(scn))
                            lox_lex_advance(scn);
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

AstTokType lox_lex_scankeyword(AstScanner* scn)
{
    static const struct
    {
        const char* str;
        AstTokType type;
    } keywords[] =
    {
        {"and", TOK_KWAND},
        {"break", TOK_KWBREAK},
        {"continue", TOK_KWCONTINUE},
        {"class", TOK_KWCLASS},
        {"else", TOK_KWELSE},
        {"false", TOK_KWFALSE},
        {"for", TOK_KWFOR},
        {"function", TOK_KWFUNCTION},
        {"fun", TOK_KWFUNCTION},
        {"if", TOK_KWIF},
        {"nil", TOK_KWNIL},
        {"or", TOK_KWOR},
        {"debugprint", TOK_KWDEBUGPRINT},
        {"return", TOK_KWRETURN},
        {"super", TOK_KWSUPER},
        {"this", TOK_KWTHIS},
        {"true", TOK_KWTRUE},
        {"var", TOK_KWVAR},
        {"while", TOK_KWWHILE},
        {NULL, (AstTokType)0},
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
    return TOK_IDENTIFIER;
}

AstToken lox_lex_scanident(AstScanner* scn)
{
    while(lox_lex_isalpha(lox_lex_peekcurrent(scn)) || lox_lex_isdigit(lox_lex_peekcurrent(scn)))
    {
        lox_lex_advance(scn);
    }
    return lox_lex_maketoken(scn, lox_lex_scankeyword(scn));
}

AstToken lox_lex_scannumber(AstScanner* scn)
{
    while(lox_lex_isdigit(lox_lex_peekcurrent(scn)))
    {
        lox_lex_advance(scn);
    }
    // Look for a fractional part.
    if(lox_lex_peekcurrent(scn) == '.' && lox_lex_isdigit(lox_lex_peeknext(scn)))
    {
        // Consume the ".".
        lox_lex_advance(scn);
        while(lox_lex_isdigit(lox_lex_peekcurrent(scn)))
        {
            lox_lex_advance(scn);
        }
    }
    return lox_lex_maketoken(scn, TOK_NUMBER);
}

AstToken lox_lex_scanstring(AstScanner* scn)
{
    while(lox_lex_peekcurrent(scn) != '"' && !lox_lex_isatend(scn))
    {
        if(lox_lex_peekcurrent(scn) == '\n')
        {
            scn->line++;
        }
        lox_lex_advance(scn);
    }
    if(lox_lex_isatend(scn))
    {
        return lox_lex_makeerrortoken(scn, "unterminated string.");
    }
    // The closing quote.
    lox_lex_advance(scn);
    return lox_lex_maketoken(scn, TOK_STRING);
}

AstToken lox_lex_scantoken(AstScanner* scn)
{
    char c;
    lox_lex_skipspace(scn);
    scn->start = scn->current;
    if(lox_lex_isatend(scn))
    {
        return lox_lex_maketoken(scn, TOK_EOF);
    }
    c = lox_lex_advance(scn);
    if(lox_lex_isalpha(c))
    {
        return lox_lex_scanident(scn);
    }
    if(lox_lex_isdigit(c))
    {
        return lox_lex_scannumber(scn);
    }
    switch(c)
    {
        case '\n':
            {
                return lox_lex_maketoken(scn, TOK_NEWLINE);
            }
            break;
        case '(':
            {
                return lox_lex_maketoken(scn, TOK_PARENOPEN);
            }
            break;
        case ')':
            {
                return lox_lex_maketoken(scn, TOK_PARENCLOSE);
            }
            break;
        case '{':
            {
                return lox_lex_maketoken(scn, TOK_BRACEOPEN);
            }
            break;
        case '}':
            {
                return lox_lex_maketoken(scn, TOK_BRACECLOSE);
            }
            break;
        case '[':
            {
                return lox_lex_maketoken(scn, TOK_BRACKETOPEN);
            }
            break;
        case ']':
            {
                return lox_lex_maketoken(scn, TOK_BRACKETCLOSE);
            }
            break;
        case ';':
            {
                return lox_lex_maketoken(scn, TOK_SEMICOLON);
            }
            break;
        case ',':
            {
                return lox_lex_maketoken(scn, TOK_COMMA);
            }
            break;
        case '.':
            {
                return lox_lex_maketoken(scn, TOK_DOT);
            }
            break;
        case '-':
            {
                if(lox_lex_match(scn, '-'))
                {
                    return lox_lex_maketoken(scn, TOK_DECREMENT);
                }
                return lox_lex_maketoken(scn, TOK_MINUS);
            }
            break;
        case '+':
            {
                if(lox_lex_match(scn, '+'))
                {
                    return lox_lex_maketoken(scn, TOK_INCREMENT);
                }
                return lox_lex_maketoken(scn, TOK_PLUS);
            }
            break;
        case '&':
            {
                if(lox_lex_match(scn, '&'))
                {
                    return lox_lex_maketoken(scn, TOK_KWAND);
                }
                return lox_lex_maketoken(scn, TOK_BINAND);
            }
            break;
        case '|':
            {
                if(lox_lex_match(scn, '|'))
                {
                    return lox_lex_maketoken(scn, TOK_KWOR);
                }
                return lox_lex_maketoken(scn, TOK_BINOR);
            }
            break;
        case '^':
            {
                return lox_lex_maketoken(scn, TOK_BINXOR);
            }
            break;
        case '%':
            {
                return lox_lex_maketoken(scn, TOK_MODULO);
            }
            break;
        case '/':
            {
                return lox_lex_maketoken(scn, TOK_SLASH);
            }
            break;
        case '*':
            {
                return lox_lex_maketoken(scn, TOK_STAR);
            }
            break;
        case '!':
            {
                if(lox_lex_match(scn, '='))
                {
                    return lox_lex_maketoken(scn, TOK_COMPNOTEQUAL);
                }
                return lox_lex_maketoken(scn, TOK_EXCLAM);
            }
            break;
        case '=':
            {
                if(lox_lex_match(scn, '='))
                {
                    return lox_lex_maketoken(scn, TOK_COMPEQUAL);
                }
                return lox_lex_maketoken(scn, TOK_ASSIGN);
            }
            break;
        case '<':
            {
                if(lox_lex_match(scn, '='))
                {
                    return lox_lex_maketoken(scn, TOK_COMPLESSEQUAL);
                }
                else if(lox_lex_match(scn, '<'))
                {
                    return lox_lex_maketoken(scn, TOK_SHIFTLEFT);
                }
                return lox_lex_maketoken(scn, TOK_COMPLESSTHAN);
            }
            break;
        case '>':
            {
                if(lox_lex_match(scn, '='))
                {
                    return lox_lex_maketoken(scn, TOK_COMPGREATEREQUAL);
                }
                else if(lox_lex_match(scn, '>'))
                {
                    return lox_lex_maketoken(scn, TOK_SHIFTRIGHT);
                }
                return lox_lex_maketoken(scn, TOK_COMPGREATERTHAN);
            }
            break;
        case '"':
            {
                return lox_lex_scanstring(scn);
            }
            break;
    }
    return lox_lex_makeerrortoken(scn, "unexpected character.");
}

void lox_prs_init(LoxState* state, AstParser* parser, AstScanner* scn)
{
    state->parser = parser;
    parser->pvm = state;
    parser->pscn = scn;
    parser->currcompiler = NULL;
    parser->currclass = NULL;
}

Chunk* lox_prs_currentchunk(AstParser* prs)
{
    return &prs->currcompiler->compiledfn->chunk;
}

void lox_prs_vraiseattoken(AstParser* prs, AstToken* token, const char* message, va_list va)
{
    if(prs->panicmode)
    {
        return;
    }
    prs->panicmode = true;
    fprintf(stderr, "[line %d] error", token->line);
    if(token->type == TOK_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if(token->type == TOK_ERROR)
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

void lox_prs_raiseerror(AstParser* prs, const char* message, ...)
{
    va_list va;
    va_start(va, message);
    lox_prs_vraiseattoken(prs, &prs->previous, message, va);
    va_end(va);
}

void lox_prs_raiseatcurrent(AstParser* prs, const char* message, ...)
{
    va_list va;
    va_start(va, message);
    lox_prs_vraiseattoken(prs, &prs->current, message, va);
    va_end(va);
}

const char* lox_prs_op2str(int32_t opcode)
{
    switch(opcode)
    {
        case OP_PUSHCONST: return "OP_PUSHCONST";
        case OP_PUSHNIL: return "OP_PUSHNIL";
        case OP_PUSHTRUE: return "OP_PUSHTRUE";
        case OP_PUSHFALSE: return "OP_PUSHFALSE";
        case OP_PUSHONE: return "OP_PUSHONE";
        case OP_POP: return "OP_POP";
        case OP_DUP: return "OP_DUP";
        case OP_LOCALGET: return "OP_LOCALGET";
        case OP_LOCALSET: return "OP_LOCALSET";
        case OP_GLOBALGET: return "OP_GLOBALGET";
        case OP_GLOBALDEFINE: return "OP_GLOBALDEFINE";
        case OP_GLOBALSET: return "OP_GLOBALSET";
        case OP_UPVALGET: return "OP_UPVALGET";
        case OP_UPVALSET: return "OP_UPVALSET";
        case OP_PROPERTYGET: return "OP_PROPERTYGET";
        case OP_PROPERTYSET: return "OP_PROPERTYSET";
        case OP_INSTGETSUPER: return "OP_INSTGETSUPER";
        case OP_EQUAL: return "OP_EQUAL";
        case OP_PRIMGREATER: return "OP_PRIMGREATER";
        case OP_PRIMLESS: return "OP_PRIMLESS";
        case OP_PRIMADD: return "OP_PRIMADD";
        case OP_PRIMSUBTRACT: return "OP_PRIMSUBTRACT";
        case OP_PRIMMULTIPLY: return "OP_PRIMMULTIPLY";
        case OP_PRIMDIVIDE: return "OP_PRIMDIVIDE";
        case OP_PRIMNOT: return "OP_PRIMNOT";
        case OP_PRIMNEGATE: return "OP_PRIMNEGATE";
        case OP_DEBUGPRINT: return "OP_DEBUGPRINT";
        case OP_JUMPNOW: return "OP_JUMPNOW";
        case OP_JUMPIFFALSE: return "OP_JUMPIFFALSE";
        case OP_LOOP: return "OP_LOOP";
        case OP_CALL: return "OP_CALL";
        case OP_INSTTHISINVOKE: return "OP_INSTTHISINVOKE";
        case OP_INSTSUPERINVOKE: return "OP_INSTSUPERINVOKE";
        case OP_CLOSURE: return "OP_CLOSURE";
        case OP_UPVALCLOSE: return "OP_UPVALCLOSE";
        case OP_RETURN: return "OP_RETURN";
        case OP_CLASS: return "OP_CLASS";
        case OP_INHERIT: return "OP_INHERIT";
        case OP_METHOD: return "OP_METHOD";
        case OP_PSEUDOBREAK: return "OP_PSEUDOBREAK";

    }
    return "?unknown?";
}

void lox_prs_skipsemicolon(AstParser* prs)
{
    while(lox_prs_match(prs, TOK_NEWLINE))
    {
    }
    while(lox_prs_match(prs, TOK_SEMICOLON))
    {
    }
}

void lox_prs_advance(AstParser* prs)
{
    prs->previous = prs->current;
    for(;;)
    {
        prs->current = lox_lex_scantoken(prs->pscn);
        if(prs->current.type != TOK_ERROR)
        {
            break;
        }
        lox_prs_raiseatcurrent(prs, prs->current.start);
    }
}

void lox_prs_consume(AstParser* prs, AstTokType type, const char* message)
{
    if(prs->current.type == type)
    {
        lox_prs_advance(prs);
        return;
    }
    lox_prs_raiseatcurrent(prs, message);
}

bool lox_prs_check(AstParser* prs, AstTokType type)
{
    return prs->current.type == type;
}

bool lox_prs_match(AstParser* prs, AstTokType type)
{
    if(!lox_prs_check(prs, type))
    {
        return false;
    }
    lox_prs_advance(prs);
    return true;
}

void lox_prs_emit1byte(AstParser* prs, int32_t byte)
{
    lox_chunk_pushbyte(prs->pvm, lox_prs_currentchunk(prs), byte, prs->previous.line);
}

void lox_prs_emit2byte(AstParser* prs, int32_t byte1, int32_t byte2)
{
    lox_prs_emit1byte(prs, byte1);
    lox_prs_emit1byte(prs, byte2);
}

void lox_prs_emitloop(AstParser* prs, int loopstart)
{
    int offset;
    lox_prs_emit1byte(prs, OP_LOOP);
    offset = lox_prs_currentchunk(prs)->count - loopstart + 2;
    if(offset > UINT16_MAX)
    {
        lox_prs_raiseerror(prs, "loop body too large.");
    }
    lox_prs_emit1byte(prs, (offset >> 8) & 0xff);
    lox_prs_emit1byte(prs, offset & 0xff);
}


int lox_prs_realgetcodeargscount(const int32_t* code, int ip)
{
    int32_t op = code[ip];
    switch(op)
    {
        case OP_PUSHTRUE:
        case OP_PUSHFALSE:
        case OP_PUSHNIL:
        case OP_POP:
        case OP_EQUAL:
        case OP_PRIMGREATER:
        case OP_PRIMLESS:
        case OP_PRIMADD:
        case OP_PRIMSUBTRACT:
        case OP_PRIMMULTIPLY:
        case OP_PRIMDIVIDE:
        case OP_PRIMBINAND:
        case OP_PRIMBINOR:
        case OP_PRIMBINXOR:
        case OP_PRIMMODULO:
        case OP_PRIMSHIFTLEFT:
        case OP_PRIMSHIFTRIGHT:
        case OP_PRIMNOT:
        case OP_PRIMNEGATE:
        case OP_DEBUGPRINT:
        //case OP_DUP:
        case OP_PUSHONE:
        case OP_UPVALCLOSE:
        case OP_RETURN:
        case OP_INHERIT:
            return 0;
        case OP_POPN:
        case OP_PUSHCONST:
        //case OP_CONSTANT_LONG:
        //case OP_POPN:
        case OP_LOCALSET:
        case OP_LOCALGET:
        case OP_UPVALSET:
        case OP_UPVALGET:
        case OP_PROPERTYGET:
        case OP_PROPERTYSET:
        /*
        case OP_GET_EXPR_PROPERTY:
        case OP_SET_EXPR_PROPERTY:
        */
        case OP_CALL:
        case OP_INSTGETSUPER:
        case OP_GLOBALGET:
        case OP_GLOBALDEFINE:
        case OP_GLOBALSET:
        case OP_CLOSURE:
        case OP_CLASS:
        case OP_METHOD:
            return 1;
        case OP_MAKEARRAY:
        case OP_JUMPNOW:
        case OP_JUMPIFFALSE:
        case OP_PSEUDOBREAK:
        case OP_LOOP:
        case OP_INSTTHISINVOKE:
        case OP_INSTSUPERINVOKE:
            return 2;
    }
    fprintf(stderr, "internal error: failed to compute operand argument size\n");
    return -1;
}

int lox_prs_getcodeargscount(const int32_t* bytecode, int ip)
{
    int rc;
    //const char* os;
    rc = lox_prs_realgetcodeargscount(bytecode, ip);
    //os = lox_prs_op2str(bytecode[ip]);
    //fprintf(stderr, "getcodeargscount(..., code=%s) = %d\n", os, rc);
    return rc;
}

void lox_prs_startloop(AstParser* prs, AstLoop* loop)
{
    loop->enclosing = prs->currcompiler->loop;
    loop->start = lox_prs_currentchunk(prs)->count;
    loop->scopedepth = prs->currcompiler->scopedepth;
    prs->currcompiler->loop = loop;
}

void lox_prs_endloop(AstParser* prs)
{
    int i;
    Chunk* chunk;
    i = prs->currcompiler->loop->body;
    chunk = lox_prs_currentchunk(prs);
    while(i < chunk->count)
    {
        if(chunk->code[i] == OP_PSEUDOBREAK)
        {
            chunk->code[i] = OP_JUMPNOW;
            lox_prs_emitpatchjump(prs, i + 1);
            i += 3;
        }
        else
        {
            i += 1 + lox_prs_getcodeargscount(chunk->code, i);
        }
    }
    prs->currcompiler->loop = prs->currcompiler->loop->enclosing;
}

int lox_prs_emitjump(AstParser* prs, int32_t instruction)
{
    lox_prs_emit1byte(prs, instruction);
    lox_prs_emit1byte(prs, 0xff);
    lox_prs_emit1byte(prs, 0xff);
    return lox_prs_currentchunk(prs)->count - 2;
}

void lox_prs_emitreturn(AstParser* prs)
{
    if(prs->currcompiler->type == TYPE_INITIALIZER)
    {
        lox_prs_emit2byte(prs, OP_LOCALGET, 0);
    }
    else
    {
        lox_prs_emit1byte(prs, OP_PUSHNIL);
    }
    lox_prs_emit1byte(prs, OP_RETURN);
}

int32_t lox_prs_makeconstant(AstParser* prs, Value value)
{
    int constant;
    constant = lox_chunk_pushconst(prs->pvm, lox_prs_currentchunk(prs), value);
    if(constant > UINT8_MAX)
    {
        lox_prs_raiseerror(prs, "too many constants in one chunk.");
        return 0;
    }
    return (int32_t)constant;
}

void lox_prs_emitconstant(AstParser* prs, Value value)
{
    lox_prs_emit2byte(prs, OP_PUSHCONST, lox_prs_makeconstant(prs, value));
}

void lox_prs_emitpatchjump(AstParser* prs, int offset)
{
    int jump;
    // -2 to adjust for the bytecode for the jump offset itself.
    jump = lox_prs_currentchunk(prs)->count - offset - 2;
    if(jump > UINT16_MAX)
    {
        lox_prs_raiseerror(prs, "too much code to jump over.");
    }
    lox_prs_currentchunk(prs)->code[offset] = (jump >> 8) & 0xff;
    lox_prs_currentchunk(prs)->code[offset + 1] = jump & 0xff;
}

void lox_prs_compilerinit(AstParser* prs, AstCompiler* compiler, AstFuncType type)
{
    AstLocal* local;
    compiler->enclosing = prs->currcompiler;
    compiler->compiledfn = NULL;
    compiler->type = type;
    compiler->localcount = 0;
    compiler->scopedepth = 0;
    compiler->compiledfn = lox_object_makefunction(prs->pvm);
    prs->currcompiler = compiler;
    if(type != TYPE_SCRIPT)
    {
        prs->currcompiler->compiledfn->name = lox_string_copy(prs->pvm, prs->previous.start, prs->previous.length);
    }
    local = &prs->currcompiler->locals[prs->currcompiler->localcount++];
    local->depth = 0;
    local->iscaptured = false;
    /* Calls and Functions init-function-slot < Methods and Initializers slot-zero */
    /*
    local->name.start = "";
    local->name.length = 0;
    */
    if(type != TYPE_FUNCTION)
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

ObjFunction* lox_prs_compilerfinish(AstParser* prs, bool ismainfn)
{
    ObjFunction* fn;
    lox_prs_emitreturn(prs);
    if(ismainfn)
    {
        lox_prs_emit1byte(prs, OP_HALTVM);
    }
    fn = prs->currcompiler->compiledfn;
#if (DEBUG_PRINT_CODE == 1)
    if(!prs->haderror)
    {
        lox_chunk_disasm(prs->pvm, prs->pvm->stderrwriter, lox_prs_currentchunk(prs), fn->name != NULL ? fn->name->sbuf->data : "<script>");
    }
#endif
    prs->currcompiler = prs->currcompiler->enclosing;
    return fn;
}

void lox_prs_scopebegin(AstParser* prs)
{
    prs->currcompiler->scopedepth++;
}

void lox_prs_scopeend(AstParser* prs)
{
    AstCompiler* pc;
    pc = prs->currcompiler;
    pc->scopedepth--;
    while(pc->localcount > 0 && pc->locals[pc->localcount - 1].depth > pc->scopedepth)
    {
        if(pc->locals[pc->localcount - 1].iscaptured)
        {
            lox_prs_emit1byte(prs, OP_UPVALCLOSE);
        }
        else
        {
            lox_prs_emit1byte(prs, OP_POP);
        }
        pc->localcount--;
    }
}

int32_t lox_prs_makeidentconstant(AstParser* prs, AstToken* name)
{
    return lox_prs_makeconstant(prs, OBJ_VAL(lox_string_copy(prs->pvm, name->start, name->length)));
}

bool lox_prs_identsequal(AstToken* a, AstToken* b)
{
    if(a->length != b->length)
    {
        return false;
    }
    return memcmp(a->start, b->start, a->length) == 0;
}

int lox_prs_resolvelocal(AstParser* prs, AstCompiler* compiler, AstToken* name)
{
    int i;
    AstLocal* local;
    for(i = compiler->localcount - 1; i >= 0; i--)
    {
        local = &compiler->locals[i];
        if(lox_prs_identsequal(name, &local->name))
        {
            if(local->depth == -1)
            {
                lox_prs_raiseerror(prs, "can't read local variable in its own initializer.");
            }
            return i;
        }
    }
    return -1;
}

int lox_prs_addupval(AstParser* prs, AstCompiler* compiler, int32_t index, bool islocal)
{
    int i;
    int upvaluecount;
    AstUpvalue* upvalue;
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
        lox_prs_raiseerror(prs, "too many closure variables in function.");
        return 0;
    }
    compiler->upvalues[upvaluecount].islocal = islocal;
    compiler->upvalues[upvaluecount].index = index;
    return compiler->compiledfn->upvaluecount++;
}

int lox_prs_resolveupval(AstParser* prs, AstCompiler* compiler, AstToken* name)
{
    int local;
    int upvalue;
    if(compiler->enclosing == NULL)
    {
        return -1;
    }
    local = lox_prs_resolvelocal(prs, compiler->enclosing, name);
    if(local != -1)
    {
        compiler->enclosing->locals[local].iscaptured = true;
        return lox_prs_addupval(prs, compiler, (int32_t)local, true);
    }
    upvalue = lox_prs_resolveupval(prs, compiler->enclosing, name);
    if(upvalue != -1)
    {
        return lox_prs_addupval(prs, compiler, (int32_t)upvalue, false);
    }
    return -1;
}

void lox_prs_addlocal(AstParser* prs, AstToken name)
{
    AstLocal* local;
    if(prs->currcompiler->localcount == UINT8_COUNT)
    {
        lox_prs_raiseerror(prs, "too many local variables in function.");
        return;
    }
    local = &prs->currcompiler->locals[prs->currcompiler->localcount++];
    local->name = name;
    local->depth = -1;
    local->iscaptured = false;
}

int lox_prs_discardlocals(AstParser* prs, AstCompiler* current)
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
        lox_prs_emit2byte(prs, OP_POPN, (int32_t)n);
    }
    return current->localcount - lc - 1;
}

void lox_prs_parsevarident(AstParser* prs)
{
    int i;
    AstLocal* local;
    AstToken* name;
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
        if(lox_prs_identsequal(name, &local->name))
        {
            lox_prs_raiseerror(prs, "already a variable with this name in this scope.");
        }
    }
    lox_prs_addlocal(prs, *name);
}

int32_t lox_prs_parsevarname(AstParser* prs, const char* errormessage)
{
    lox_prs_consume(prs, TOK_IDENTIFIER, errormessage);
    lox_prs_parsevarident(prs);
    if(prs->currcompiler->scopedepth > 0)
    {
        return 0;
    }
    return lox_prs_makeidentconstant(prs, &prs->previous);
}

void lox_prs_markinit(AstParser* prs)
{
    if(prs->currcompiler->scopedepth == 0)
    {
        return;
    }
    prs->currcompiler->locals[prs->currcompiler->localcount - 1].depth = prs->currcompiler->scopedepth;
}

void lox_prs_emitdefvar(AstParser* prs, int32_t global)
{
    if(prs->currcompiler->scopedepth > 0)
    {
        lox_prs_markinit(prs);
        return;
    }
    lox_prs_emit2byte(prs, OP_GLOBALDEFINE, global);
}

int32_t lox_prs_parsearglist(AstParser* prs)
{
    int32_t argc = 0;
    if(!lox_prs_check(prs, TOK_PARENCLOSE))
    {
        do
        {
            lox_prs_parseexpr(prs);
            if(argc == 255)
            {
                lox_prs_raiseerror(prs, "can't have more than 255 arguments.");
            }
            argc++;
        } while(lox_prs_match(prs, TOK_COMMA));
    }
    lox_prs_consume(prs, TOK_PARENCLOSE, "expect ')' after arguments.");
    return argc;
}

void lox_prs_ruleand(AstParser* prs, bool canassign)
{
    (void)canassign;
    int endjump;
    endjump = lox_prs_emitjump(prs, OP_JUMPIFFALSE);
    lox_prs_emit1byte(prs, OP_POP);
    lox_prs_parseprec(prs, PREC_AND);
    lox_prs_emitpatchjump(prs, endjump);
}

void lox_prs_rulebinary(AstParser* prs, bool canassign)
{
    AstRule* rule;
    AstTokType ot;
    (void)canassign;
    ot = prs->previous.type;
    rule = lox_prs_getrule(ot);
    lox_prs_parseprec(prs, (AstPrecedence)(rule->precedence + 1));
    switch(ot)
    {
        case TOK_COMPNOTEQUAL:
            lox_prs_emit2byte(prs, OP_EQUAL, OP_PRIMNOT);
            break;
        case TOK_COMPEQUAL:
            lox_prs_emit1byte(prs, OP_EQUAL);
            break;
        case TOK_COMPGREATERTHAN:
            lox_prs_emit1byte(prs, OP_PRIMGREATER);
            break;
        case TOK_COMPGREATEREQUAL:
            lox_prs_emit2byte(prs, OP_PRIMLESS, OP_PRIMNOT);
            break;
        case TOK_COMPLESSTHAN:
            lox_prs_emit1byte(prs, OP_PRIMLESS);
            break;
        case TOK_COMPLESSEQUAL:
            lox_prs_emit2byte(prs, OP_PRIMGREATER, OP_PRIMNOT);
            break;
        case TOK_PLUS:
            lox_prs_emit1byte(prs, OP_PRIMADD);
            break;
        case TOK_MINUS:
            lox_prs_emit1byte(prs, OP_PRIMSUBTRACT);
            break;
        case TOK_STAR:
            lox_prs_emit1byte(prs, OP_PRIMMULTIPLY);
            break;
        case TOK_SLASH:
            lox_prs_emit1byte(prs, OP_PRIMDIVIDE);
            break;
        case TOK_MODULO:
            lox_prs_emit1byte(prs, OP_PRIMMODULO);
            break;
        case TOK_BINAND:
            lox_prs_emit1byte(prs, OP_PRIMBINAND);
            break;
        case TOK_BINOR:
            lox_prs_emit1byte(prs, OP_PRIMBINOR);
            break;
        case TOK_BINXOR:
            lox_prs_emit1byte(prs, OP_PRIMBINXOR);
            break;
        case TOK_SHIFTLEFT:
            lox_prs_emit1byte(prs, OP_PRIMSHIFTLEFT);
            break;
        case TOK_SHIFTRIGHT:
            lox_prs_emit1byte(prs, OP_PRIMSHIFTRIGHT);
            break;
        default:
            return;// Unreachable.
    }
}

void lox_prs_rulecall(AstParser* prs, bool canassign)
{
    int32_t argc;
    (void)canassign;
    argc = lox_prs_parsearglist(prs);
    lox_prs_emit2byte(prs, OP_CALL, argc);
}

void lox_prs_ruledot(AstParser* prs, bool canassign)
{
    int32_t name;
    int32_t argc;
    (void)canassign;
    lox_prs_consume(prs, TOK_IDENTIFIER, "expect property name after '.'.");
    name = lox_prs_makeidentconstant(prs, &prs->previous);
    if(canassign && lox_prs_match(prs, TOK_ASSIGN))
    {
        lox_prs_parseexpr(prs);
        lox_prs_emit2byte(prs, OP_PROPERTYSET, name);
    }
    else if(lox_prs_match(prs, TOK_PARENOPEN))
    {
        argc = lox_prs_parsearglist(prs);
        lox_prs_emit2byte(prs, OP_INSTTHISINVOKE, name);
        lox_prs_emit1byte(prs, argc);
    }
    else
    {
        lox_prs_emit2byte(prs, OP_PROPERTYGET, name);
    }
}

void lox_prs_ruleliteral(AstParser* prs, bool canassign)
{
    (void)canassign;
    switch(prs->previous.type)
    {
        case TOK_KWFALSE:
            lox_prs_emit1byte(prs, OP_PUSHFALSE);
            break;
        case TOK_KWNIL:
            lox_prs_emit1byte(prs, OP_PUSHNIL);
            break;
        case TOK_KWTRUE:
            lox_prs_emit1byte(prs, OP_PUSHTRUE);
            break;
        default:
            return;// Unreachable.
    }
}

void lox_prs_rulegrouping(AstParser* prs, bool canassign)
{
    (void)canassign;
    lox_prs_parseexpr(prs);
    lox_prs_consume(prs, TOK_PARENCLOSE, "expect ')' after expression");
}

void lox_prs_rulenumber(AstParser* prs, bool canassign)
{
    double value;
    (void)canassign;
    value = strtod(prs->previous.start, NULL);
    lox_prs_emitconstant(prs, NUMBER_VAL(value));
}

void lox_prs_ruleor(AstParser* prs, bool canassign)
{
    int endjump;
    int elsejump;
    (void)canassign;
    elsejump = lox_prs_emitjump(prs, OP_JUMPIFFALSE);
    endjump = lox_prs_emitjump(prs, OP_JUMPNOW);
    lox_prs_emitpatchjump(prs, elsejump);
    lox_prs_emit1byte(prs, OP_POP);
    lox_prs_parseprec(prs, PREC_OR);
    lox_prs_emitpatchjump(prs, endjump);
}

#define stringesc1(c, rpl1) \
    case c: \
        { \
            buf[pi] = rpl1; \
            pi += 1; \
            i += 1; \
        } \
        break;

void lox_prs_rulestring(AstParser* prs, bool canassign)
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
    ObjString* os;
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
                                lox_prs_raiseerror(prs, "unknown string escape character '%c'", nextc);
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
        os = lox_string_take(prs->pvm, buf, nlen);
    }
    else
    {
        os = lox_string_copy(prs->pvm, rawstr, rawlen);
    }
    lox_prs_emitconstant(prs, OBJ_VAL(os));

}

void lox_prs_doassign(AstParser* prs, int32_t getop, int32_t setop, int arg, bool canassign)
{

    if(canassign && lox_prs_match(prs, TOK_ASSIGN))
    {
        lox_prs_parseexpr(prs);
        lox_prs_emit2byte(prs, setop, (int32_t)arg);
    }
    else if(canassign && lox_prs_match(prs, TOK_INCREMENT))
    {
        if(getop == OP_PROPERTYGET /*|| getop == OP_GETPROPERTYGETTHIS*/)
        {
            lox_prs_emit1byte(prs, OP_DUP);
        }
        if(arg != -1)
        {
            lox_prs_emit2byte(prs, getop, arg);
        }
        else
        {
            lox_prs_emit2byte(prs, getop, 1);
        }
        lox_prs_emit2byte(prs, OP_PUSHONE, OP_PRIMADD);
        lox_prs_emit2byte(prs, setop, arg);
    }
    else if(canassign && lox_prs_match(prs, TOK_DECREMENT))
    {
        /*
        if(getop == OpCode::OP_PROPERTYGET || getop == OpCode::OP_PROPERTYGETTHIS)
        {
            emitbyte(OpCode::OP_DUP);
        }
        */
        if(arg != -1)
        {
            lox_prs_emit2byte(prs, getop, arg);
        }
        else
        {
            lox_prs_emit2byte(prs, getop, 1);
        }
        lox_prs_emit2byte(prs, OP_PUSHONE, OP_PRIMSUBTRACT);
        lox_prs_emit2byte(prs, setop, arg);
    }
    else
    {
        if(arg != -1)
        {
            if(getop == OP_INDEXGET)
            {
                lox_prs_emit2byte(prs, getop, (int32_t)0);
            }
            else
            {
                lox_prs_emit2byte(prs, getop, arg);
            }
        }
        else
        {
            lox_prs_emit2byte(prs, getop, (int32_t)arg);
        }
    }
}

void lox_prs_parsenamedvar(AstParser* prs, AstToken name, bool canassign)
{
    int32_t getop;
    int32_t setop;
    int arg;
    (void)canassign;
    arg = lox_prs_resolvelocal(prs, prs->currcompiler, &name);
    if(arg != -1)
    {
        getop = OP_LOCALGET;
        setop = OP_LOCALSET;
    }
    else if((arg = lox_prs_resolveupval(prs, prs->currcompiler, &name)) != -1)
    {
        getop = OP_UPVALGET;
        setop = OP_UPVALSET;
    }
    else
    {
        arg = lox_prs_makeidentconstant(prs, &name);
        getop = OP_GLOBALGET;
        setop = OP_GLOBALSET;
    }
    return lox_prs_doassign(prs, getop, setop, arg, canassign);
}

void lox_prs_rulevariable(AstParser* prs, bool canassign)
{
    lox_prs_parsenamedvar(prs, prs->previous, canassign);
}

AstToken lox_prs_makesyntoken(AstParser* prs, const char* text)
{
    AstToken token;
    (void)prs;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}

void lox_prs_rulesuper(AstParser* prs, bool canassign)
{
    int32_t name;
    int32_t argc;
    (void)canassign;
    if(prs->currclass == NULL)
    {
        lox_prs_raiseerror(prs, "can't use 'super' outside of a class.");
    }
    else if(!prs->currclass->hassuperclass)
    {
        lox_prs_raiseerror(prs, "can't use 'super' in a class with no superclass.");
    }
    lox_prs_consume(prs, TOK_DOT, "expect '.' after 'super'.");
    lox_prs_consume(prs, TOK_IDENTIFIER, "expect superclass method name.");
    name = lox_prs_makeidentconstant(prs, &prs->previous);
    lox_prs_parsenamedvar(prs, lox_prs_makesyntoken(prs, "this"), false);
    /* Superclasses super-get < Superclasses super-invoke */
    /*
    lox_prs_parsenamedvar(prs, lox_prs_makesyntoken(prs, "super"), false);
    lox_prs_emit2byte(prs, OP_INSTGETSUPER, name);
    */
    if(lox_prs_match(prs, TOK_PARENOPEN))
    {
        argc = lox_prs_parsearglist(prs);
        lox_prs_parsenamedvar(prs, lox_prs_makesyntoken(prs, "super"), false);
        lox_prs_emit2byte(prs, OP_INSTSUPERINVOKE, name);
        lox_prs_emit1byte(prs, argc);
    }
    else
    {
        lox_prs_parsenamedvar(prs, lox_prs_makesyntoken(prs, "super"), false);
        lox_prs_emit2byte(prs, OP_INSTGETSUPER, name);
    }
}

void lox_prs_rulethis(AstParser* prs, bool canassign)
{
    (void)canassign;
    if(prs->currclass == NULL)
    {
        lox_prs_raiseerror(prs, "can't use 'this' outside of a class.");
        return;
    }
    lox_prs_rulevariable(prs, false);
}

void lox_prs_ruleunary(AstParser* prs, bool canassign)
{
    AstTokType ot;
    (void)canassign;
    ot = prs->previous.type;
    // Compile the operand.
    /* Compiling Expressions unary < Compiling Expressions unary-operand */
    //lox_prs_parseexpr(prs);
    lox_prs_parseprec(prs, PREC_UNARY);
    // Emit the operator instruction.
    switch(ot)
    {
        case TOK_EXCLAM:
            lox_prs_emit1byte(prs, OP_PRIMNOT);
            break;
        case TOK_MINUS:
            lox_prs_emit1byte(prs, OP_PRIMNEGATE);
            break;
        default:
            return;// Unreachable.
    }
}

void lox_prs_rulearray(AstParser* prs, bool canassign)
{
    int count;
    (void)canassign;
    fprintf(stderr, "parsing array? ... \n");
    count = 0;
    lox_prs_emit1byte(prs, OP_PUSHNIL);// placeholder for the list
    if(!lox_prs_check(prs, TOK_BRACKETCLOSE))
    {
        do
        {
            lox_prs_parseexpr(prs);
            count++;
        } while(lox_prs_match(prs, TOK_COMMA));
    }
    lox_prs_consume(prs, TOK_BRACKETCLOSE, "expected ']' at end of list");
    fprintf(stderr, "parsing array: count=%d\n", count);
    lox_prs_emit2byte(prs, OP_MAKEARRAY, count);
}

void lox_prs_ruleindex(AstParser* prs, bool canassign)
{
    (void)canassign;
    fprintf(stderr, "parsing index?\n");
    lox_prs_parseexpr(prs);
    if(!lox_prs_match(prs, TOK_BRACKETCLOSE))
    {
        canassign = false;
    }
    //lox_prs_consume(prs, TOK_BRACKETCLOSE, "expected ']' after indexing");
    if(canassign)
    {
        lox_prs_doassign(prs, OP_INDEXGET, OP_INDEXSET, -1, canassign);
    }
    else
    {
        lox_prs_emit1byte(prs, OP_INDEXGET);
    }
}

/* clang-format off */
static const AstRule parserules[] = {
    /* Compiling Expressions rules < Calls and Functions infix-left-paren */
    // [TOK_PARENOPEN]    = {grouping, NULL,   PREC_NONE},
    [TOK_PARENOPEN] = { lox_prs_rulegrouping, lox_prs_rulecall, PREC_CALL },
    [TOK_PARENCLOSE] = { NULL, NULL, PREC_NONE },
    [TOK_BRACEOPEN] = { NULL, NULL, PREC_NONE },// [big]
    [TOK_BRACECLOSE] = { NULL, NULL, PREC_NONE },
    [TOK_BRACKETOPEN] = { lox_prs_rulearray, lox_prs_ruleindex, PREC_CALL },
    [TOK_BRACKETCLOSE] = { NULL, NULL, PREC_NONE },
    [TOK_COMMA] = { NULL, NULL, PREC_NONE },
    /* Compiling Expressions rules < Classes and Instances table-dot */
    // [TOK_DOT]           = {NULL,     NULL,   PREC_NONE},
    [TOK_DOT] = { NULL, lox_prs_ruledot, PREC_CALL },
    [TOK_MINUS] = { lox_prs_ruleunary, lox_prs_rulebinary, PREC_TERM },
    [TOK_PLUS] = { NULL, lox_prs_rulebinary, PREC_TERM },
    [TOK_SEMICOLON] = { NULL, NULL, PREC_NONE },
    [TOK_NEWLINE] = { NULL, NULL, PREC_NONE },
    [TOK_SLASH] = { NULL, lox_prs_rulebinary, PREC_FACTOR },
    [TOK_STAR] = { NULL, lox_prs_rulebinary, PREC_FACTOR },
    [TOK_MODULO] = { NULL, lox_prs_rulebinary, PREC_FACTOR },
    [TOK_BINAND] = { NULL, lox_prs_rulebinary, PREC_FACTOR },
    [TOK_BINOR] = { NULL, lox_prs_rulebinary, PREC_FACTOR },
    [TOK_BINXOR] = { NULL, lox_prs_rulebinary, PREC_FACTOR },
    [TOK_SHIFTLEFT] = {NULL, lox_prs_rulebinary, PREC_SHIFT},
    [TOK_SHIFTRIGHT] = {NULL, lox_prs_rulebinary, PREC_SHIFT},
    [TOK_INCREMENT] = {NULL, NULL, PREC_NONE},
    [TOK_DECREMENT] = {NULL, NULL, PREC_NONE},
    /* Compiling Expressions rules < Types of Values table-not */
    // [TOK_EXCLAM]          = {NULL,     NULL,   PREC_NONE},
    [TOK_EXCLAM] = { lox_prs_ruleunary, NULL, PREC_NONE },
    /* Compiling Expressions rules < Types of Values table-equal */
    // [TOK_COMPNOTEQUAL]    = {NULL,     NULL,   PREC_NONE},
    [TOK_COMPNOTEQUAL] = { NULL, lox_prs_rulebinary, PREC_EQUALITY },
    [TOK_ASSIGN] = { NULL, NULL, PREC_NONE },
    /* Compiling Expressions rules < Types of Values table-comparisons */
    // [TOK_COMPEQUAL]   = {NULL,     NULL,   PREC_NONE},
    // [TOK_COMPGREATERTHAN]       = {NULL,     NULL,   PREC_NONE},
    // [TOK_COMPGREATEREQUAL] = {NULL,     NULL,   PREC_NONE},
    // [TOK_COMPLESSTHAN]          = {NULL,     NULL,   PREC_NONE},
    // [TOK_COMPLESSEQUAL]    = {NULL,     NULL,   PREC_NONE},
    [TOK_COMPEQUAL] = { NULL, lox_prs_rulebinary, PREC_EQUALITY },
    [TOK_COMPGREATERTHAN] = { NULL, lox_prs_rulebinary, PREC_COMPARISON },
    [TOK_COMPGREATEREQUAL] = { NULL, lox_prs_rulebinary, PREC_COMPARISON },
    [TOK_COMPLESSTHAN] = { NULL, lox_prs_rulebinary, PREC_COMPARISON },
    [TOK_COMPLESSEQUAL] = { NULL, lox_prs_rulebinary, PREC_COMPARISON },
    /* Compiling Expressions rules < Global Variables table-identifier */
    // [TOK_IDENTIFIER]    = {NULL,     NULL,   PREC_NONE},
    [TOK_IDENTIFIER] = { lox_prs_rulevariable, NULL, PREC_NONE },
    /* Compiling Expressions rules < Strings table-string */
    // [TOK_STRING]        = {NULL,     NULL,   PREC_NONE},
    [TOK_STRING] = { lox_prs_rulestring, NULL, PREC_NONE },
    [TOK_NUMBER] = { lox_prs_rulenumber, NULL, PREC_NONE },
    /* Compiling Expressions rules < Jumping Back and Forth table-and */
    // [TOK_KWAND]           = {NULL,     NULL,   PREC_NONE},
    [TOK_KWBREAK] = {NULL, NULL, PREC_NONE},
    [TOK_KWCONTINUE] = { NULL, NULL, PREC_NONE },
    [TOK_KWAND] = { NULL, lox_prs_ruleand, PREC_AND },
    [TOK_KWCLASS] = { NULL, NULL, PREC_NONE },
    [TOK_KWELSE] = { NULL, NULL, PREC_NONE },
    /* Compiling Expressions rules < Types of Values table-false */
    // [TOK_KWFALSE]         = {NULL,     NULL,   PREC_NONE},
    [TOK_KWFALSE] = { lox_prs_ruleliteral, NULL, PREC_NONE },
    [TOK_KWFOR] = { NULL, NULL, PREC_NONE },
    [TOK_KWFUNCTION] = { NULL, NULL, PREC_NONE },
    [TOK_KWIF] = { NULL, NULL, PREC_NONE },
    /* Compiling Expressions rules < Types of Values table-nil
    * [TOK_KWNIL]           = {NULL,     NULL,   PREC_NONE},
    */
    [TOK_KWNIL] = { lox_prs_ruleliteral, NULL, PREC_NONE },
    /* Compiling Expressions rules < Jumping Back and Forth table-or
    * [TOK_KWOR]            = {NULL,     NULL,   PREC_NONE},
    */
    [TOK_KWOR] = { NULL, lox_prs_ruleor, PREC_OR },
    [TOK_KWDEBUGPRINT] = { NULL, NULL, PREC_NONE },
    [TOK_KWRETURN] = { NULL, NULL, PREC_NONE },
    /* Compiling Expressions rules < Superclasses table-super
    * [TOK_KWSUPER]         = {NULL,     NULL,   PREC_NONE},
    */
    [TOK_KWSUPER] = { lox_prs_rulesuper, NULL, PREC_NONE },
    /* Compiling Expressions rules < Methods and Initializers table-this
    * [TOK_KWTHIS]          = {NULL,     NULL,   PREC_NONE},
    */
    [TOK_KWTHIS] = { lox_prs_rulethis, NULL, PREC_NONE },
    /* Compiling Expressions rules < Types of Values table-true
    * [TOK_KWTRUE]          = {NULL,     NULL,   PREC_NONE},
    */
    [TOK_KWTRUE] = { lox_prs_ruleliteral, NULL, PREC_NONE },
    [TOK_KWVAR] = { NULL, NULL, PREC_NONE },
    [TOK_KWWHILE] = { NULL, NULL, PREC_NONE },
    [TOK_ERROR] = { NULL, NULL, PREC_NONE },
    [TOK_EOF] = { NULL, NULL, PREC_NONE },
};
/* clang-format on */

AstRule* lox_prs_getrule(AstTokType type)
{
    return (AstRule*)&parserules[type];
}

void lox_prs_parseprec(AstParser* prs, AstPrecedence precedence)
{
    bool canassign;
    AstParseFN infixrule;
    AstParseFN prefixrule;
    lox_prs_advance(prs);
    prefixrule = lox_prs_getrule(prs->previous.type)->prefix;
    if(prefixrule == NULL)
    {
        lox_prs_raiseerror(prs, "expected expression");
        return;
    }
    canassign = precedence <= PREC_ASSIGNMENT;
    prefixrule(prs, canassign);
    while(precedence <= lox_prs_getrule(prs->current.type)->precedence)
    {
        lox_prs_advance(prs);
        infixrule = lox_prs_getrule(prs->previous.type)->infix;
        /* Compiling Expressions infix < Global Variables infix-rule */
        //infixrule();
        infixrule(prs, canassign);
    }
    if(canassign && lox_prs_match(prs, TOK_ASSIGN))
    {
        lox_prs_raiseerror(prs, "invalid assignment target");
    }
}

void lox_prs_parseexpr(AstParser* prs)
{
    /* Compiling Expressions expression < Compiling Expressions expression-body
    // What goes here?
    */
    lox_prs_parseprec(prs, PREC_ASSIGNMENT);
}

void lox_prs_parseblock(AstParser* prs)
{
    while(!lox_prs_check(prs, TOK_BRACECLOSE) && !lox_prs_check(prs, TOK_EOF))
    {
        lox_prs_parsedecl(prs);
    }
    lox_prs_consume(prs, TOK_BRACECLOSE, "expect '}' after block.");
}

void lox_prs_parsefunction(AstParser* prs, AstFuncType type)
{
    int i;
    int32_t constant;
    ObjFunction* fn;
    AstCompiler compiler;
    lox_prs_compilerinit(prs, &compiler, type);
    lox_prs_scopebegin(prs);// [no-end-scope]
    lox_prs_consume(prs, TOK_PARENOPEN, "expect '(' after function name.");
    if(!lox_prs_check(prs, TOK_PARENCLOSE))
    {
        do
        {
            prs->currcompiler->compiledfn->arity++;
            if(prs->currcompiler->compiledfn->arity > 255)
            {
                lox_prs_raiseatcurrent(prs, "can't have more than 255 parameters.");
            }
            constant = lox_prs_parsevarname(prs, "expect parameter name.");
            lox_prs_emitdefvar(prs, constant);
        } while(lox_prs_match(prs, TOK_COMMA));
    }
    lox_prs_consume(prs, TOK_PARENCLOSE, "expect ')' after parameters.");
    lox_prs_consume(prs, TOK_BRACEOPEN, "expect '{' before function body.");
    lox_prs_parseblock(prs);
    fn = lox_prs_compilerfinish(prs, false);
    /* Calls and Functions compile-function < Closures emit-closure */
    // lox_prs_emit2byte(prs, OP_PUSHCONST, lox_prs_makeconstant(prs, OBJ_VAL(fn)));
    lox_prs_emit2byte(prs, OP_CLOSURE, lox_prs_makeconstant(prs, OBJ_VAL(fn)));
    for(i = 0; i < fn->upvaluecount; i++)
    {
        lox_prs_emit1byte(prs, compiler.upvalues[i].islocal ? 1 : 0);
        lox_prs_emit1byte(prs, compiler.upvalues[i].index);
    }
}

void lox_prs_parsemethod(AstParser* prs)
{
    int32_t constant;
    AstFuncType type;
    lox_prs_consume(prs, TOK_IDENTIFIER, "expect method name.");
    constant = lox_prs_makeidentconstant(prs, &prs->previous);

    /* Methods and Initializers method-body < Methods and Initializers method-type */
    //type = TYPE_FUNCTION;
    type = TYPE_METHOD;
    if(prs->previous.length == 4 && memcmp(prs->previous.start, "init", 4) == 0)
    {
        type = TYPE_INITIALIZER;
    }
    lox_prs_parsefunction(prs, type);
    lox_prs_emit2byte(prs, OP_METHOD, constant);
}

void lox_prs_parseclassdecl(AstParser* prs)
{
    int32_t nameconstant;
    AstToken classname;
    AstClassCompiler classcompiler;
    lox_prs_consume(prs, TOK_IDENTIFIER, "expect class name.");
    classname = prs->previous;
    nameconstant = lox_prs_makeidentconstant(prs, &prs->previous);
    lox_prs_parsevarident(prs);
    lox_prs_emit2byte(prs, OP_CLASS, nameconstant);
    lox_prs_emitdefvar(prs, nameconstant);
    classcompiler.hassuperclass = false;
    classcompiler.enclosing = prs->currclass;
    prs->currclass = &classcompiler;
    if(lox_prs_match(prs, TOK_COMPLESSTHAN))
    {
        lox_prs_consume(prs, TOK_IDENTIFIER, "expect superclass name");
        lox_prs_rulevariable(prs, false);
        if(lox_prs_identsequal(&classname, &prs->previous))
        {
            lox_prs_raiseerror(prs, "a class cannot inherit from itself");
        }
        lox_prs_scopebegin(prs);
        lox_prs_addlocal(prs, lox_prs_makesyntoken(prs, "super"));
        lox_prs_emitdefvar(prs, 0);
        lox_prs_parsenamedvar(prs, classname, false);
        lox_prs_emit1byte(prs, OP_INHERIT);
        classcompiler.hassuperclass = true;
    }
    lox_prs_parsenamedvar(prs, classname, false);
    lox_prs_consume(prs, TOK_BRACEOPEN, "expect '{' before class body");
    while(!lox_prs_check(prs, TOK_BRACECLOSE) && !lox_prs_check(prs, TOK_EOF))
    {
        lox_prs_parsemethod(prs);
    }
    lox_prs_consume(prs, TOK_BRACECLOSE, "expect '}' after class body");
    lox_prs_emit1byte(prs, OP_POP);
    if(classcompiler.hassuperclass)
    {
        lox_prs_scopeend(prs);
    }
    prs->currclass = prs->currclass->enclosing;
}

void lox_prs_parsefuncdecl(AstParser* prs)
{
    int32_t global;
    global = lox_prs_parsevarname(prs, "expect function name.");
    lox_prs_markinit(prs);
    lox_prs_parsefunction(prs, TYPE_FUNCTION);
    lox_prs_emitdefvar(prs, global);
}

void lox_prs_parsevardecl(AstParser* prs)
{
    int32_t global;
    global = lox_prs_parsevarname(prs, "expect variable name.");
    if(lox_prs_match(prs, TOK_ASSIGN))
    {
        lox_prs_parseexpr(prs);
    }
    else
    {
        lox_prs_emit1byte(prs, OP_PUSHNIL);
    }
    lox_prs_skipsemicolon(prs);
    lox_prs_emitdefvar(prs, global);
}

void lox_prs_parseexprstmt(AstParser* prs)
{
    lox_prs_parseexpr(prs);
    lox_prs_skipsemicolon(prs);
    lox_prs_emit1byte(prs, OP_POP);
}

void lox_prs_parseforstmt(AstParser* prs)
{
    int loopstart;
    int exitjump;
    int bodyjump;
    int incrementstart;
    AstLoop loop;
    lox_prs_scopebegin(prs);
    lox_prs_startloop(prs, &loop);
    lox_prs_consume(prs, TOK_PARENOPEN, "Expect '(' after 'for'.");
    if(lox_prs_match(prs, TOK_SEMICOLON))
    {
        // No initializer.
    }
    else if(lox_prs_match(prs, TOK_KWVAR))
    {
        lox_prs_parsevardecl(prs);
    }
    else
    {
        lox_prs_parseexprstmt(prs);
    }
    loopstart = lox_prs_currentchunk(prs)->count;
    exitjump = -1;
    if(!lox_prs_match(prs, TOK_SEMICOLON))
    {
        lox_prs_parseexpr(prs);
        lox_prs_consume(prs, TOK_SEMICOLON, "Expect ';' after loop condition.");
        // Jump out of the loop if the condition is false.
        exitjump = lox_prs_emitjump(prs, OP_JUMPIFFALSE);
        lox_prs_emit1byte(prs, OP_POP);// Condition
    }
    if(!lox_prs_match(prs, TOK_PARENCLOSE))
    {
        bodyjump = lox_prs_emitjump(prs, OP_JUMPNOW);
        incrementstart = lox_prs_currentchunk(prs)->count;
        // when we 'continue' in for loop, we want to jump here
        prs->currcompiler->loop->start = incrementstart;
        lox_prs_parseexpr(prs);
        lox_prs_emit1byte(prs, OP_POP);
        lox_prs_consume(prs, TOK_PARENCLOSE, "Expect ')' after 'for' clauses.");
        lox_prs_emitloop(prs, loopstart);
        loopstart = incrementstart;
        lox_prs_emitpatchjump(prs, bodyjump);
    }
    prs->currcompiler->loop->body = lox_prs_currentchunk(prs)->count;
    lox_prs_parsestmt(prs);
    lox_prs_emitloop(prs, loopstart);
    if(exitjump != -1)
    {
        lox_prs_emitpatchjump(prs, exitjump);
        lox_prs_emit1byte(prs, OP_POP);
    }
    lox_prs_endloop(prs);
    lox_prs_scopeend(prs);
}

void lox_prs_parsewhilestmt(AstParser* prs)
{
    int loopstart;
    int exitjump;
    AstLoop loop;
    lox_prs_startloop(prs, &loop);
    loopstart = lox_prs_currentchunk(prs)->count;
    lox_prs_consume(prs, TOK_PARENOPEN, "expect '(' after 'while'");
    lox_prs_parseexpr(prs);
    lox_prs_consume(prs, TOK_PARENCLOSE, "expect ')' after condition");
    exitjump = lox_prs_emitjump(prs, OP_JUMPIFFALSE);
    lox_prs_emit1byte(prs, OP_POP);
    prs->currcompiler->loop->body = lox_prs_currentchunk(prs)->count;
    lox_prs_parsestmt(prs);
    lox_prs_emitloop(prs, loopstart);
    lox_prs_emitpatchjump(prs, exitjump);
    lox_prs_emit1byte(prs, OP_POP);
    lox_prs_endloop(prs);
}

void lox_prs_parsebreakstmt(AstParser* prs)
{
    if(prs->currcompiler->loop == NULL)
    {
        lox_prs_raiseerror(prs, "'break' can only be used in a loop");
        return;
    }
    lox_prs_skipsemicolon(prs);
    lox_prs_discardlocals(prs, prs->currcompiler);
    lox_prs_emitjump(prs, OP_PSEUDOBREAK);
}

void lox_prs_parsecontinuestmt(AstParser* prs)
{
    if(prs->currcompiler->loop == NULL)
    {
        lox_prs_raiseerror(prs, "cannot use 'continue' outside of a loop");
        return;
    }
    lox_prs_skipsemicolon(prs);
    lox_prs_discardlocals(prs, prs->currcompiler);
    lox_prs_emitloop(prs, prs->currcompiler->loop->start);
}

void lox_prs_parseifstmt(AstParser* prs)
{
    int thenjump;
    int elsejump;
    lox_prs_consume(prs, TOK_PARENOPEN, "expect '(' after 'if'.");
    lox_prs_parseexpr(prs);
    lox_prs_consume(prs, TOK_PARENCLOSE, "expect ')' after condition.");// [paren]
    thenjump = lox_prs_emitjump(prs, OP_JUMPIFFALSE);
    lox_prs_emit1byte(prs, OP_POP);
    lox_prs_parsestmt(prs);
    elsejump = lox_prs_emitjump(prs, OP_JUMPNOW);
    lox_prs_emitpatchjump(prs, thenjump);
    lox_prs_emit1byte(prs, OP_POP);
    if(lox_prs_match(prs, TOK_KWELSE))
    {
        lox_prs_parsestmt(prs);
    }
    lox_prs_emitpatchjump(prs, elsejump);
}

void lox_prs_parseprintstmt(AstParser* prs)
{
    lox_prs_parseexpr(prs);
    //lox_prs_consume(prs, TOK_SEMICOLON, "expect ';' after value.");
    lox_prs_skipsemicolon(prs);
    lox_prs_emit1byte(prs, OP_DEBUGPRINT);
}

void lox_prs_parsereturnstmt(AstParser* prs)
{
    if(prs->currcompiler->type == TYPE_SCRIPT)
    {
        lox_prs_raiseerror(prs, "cannot return from top-level code");
    }
    if(lox_prs_match(prs, TOK_SEMICOLON) || lox_prs_match(prs, TOK_NEWLINE))
    {
        lox_prs_emitreturn(prs);
    }
    else
    {
        if(prs->currcompiler->type == TYPE_INITIALIZER)
        {
            lox_prs_raiseerror(prs, "cannot return a value from an initializer");
        }
        lox_prs_parseexpr(prs);
        //lox_prs_consume(prs, TOK_SEMICOLON, "expect ';' after return value.");
        lox_prs_skipsemicolon(prs);
        lox_prs_emit1byte(prs, OP_RETURN);
    }
}

void lox_prs_synchronize(AstParser* prs)
{
    prs->panicmode = false;
    while(prs->current.type != TOK_EOF)
    {
        if(prs->previous.type == TOK_SEMICOLON)
        {
            return;
        }
        switch(prs->current.type)
        {
            case TOK_KWBREAK:
            case TOK_KWCONTINUE:
            case TOK_KWCLASS:
            case TOK_KWFUNCTION:
            case TOK_KWVAR:
            case TOK_KWFOR:
            case TOK_KWIF:
            case TOK_KWWHILE:
            case TOK_KWDEBUGPRINT:
            case TOK_KWRETURN:
                return;
            default:
                // Do nothing.
                break;
        }
        lox_prs_advance(prs);
    }
}

void lox_prs_parsedecl(AstParser* prs)
{
    if(lox_prs_match(prs, TOK_KWCLASS))
    {
        lox_prs_parseclassdecl(prs);
    }
    else if(lox_prs_match(prs, TOK_KWFUNCTION))
    {
        lox_prs_parsefuncdecl(prs);
    }
    else if(lox_prs_match(prs, TOK_KWVAR))
    {
        lox_prs_parsevardecl(prs);
    }
    else
    {
        lox_prs_parsestmt(prs);
    }
    /* Global Variables declaration < Global Variables match-var */
    // lox_prs_parsestmt(prs);
    if(prs->panicmode)
    {
        lox_prs_synchronize(prs);
    }
}

void lox_prs_parsestmt(AstParser* prs)
{
    if(lox_prs_match(prs, TOK_KWDEBUGPRINT))
    {
        lox_prs_parseprintstmt(prs);
    }
    else if(lox_prs_match(prs, TOK_KWBREAK))
    {
        lox_prs_parsebreakstmt(prs);
    }
    else if(lox_prs_match(prs, TOK_KWCONTINUE))
    {
        lox_prs_parsecontinuestmt(prs);
    }
    else if(lox_prs_match(prs, TOK_KWFOR))
    {
        lox_prs_parseforstmt(prs);
    }
    else if(lox_prs_match(prs, TOK_KWIF))
    {
        lox_prs_parseifstmt(prs);
    }
    else if(lox_prs_match(prs, TOK_KWRETURN))
    {
        lox_prs_parsereturnstmt(prs);
    }
    else if(lox_prs_match(prs, TOK_KWWHILE))
    {
        lox_prs_parsewhilestmt(prs);
    }
    else if(lox_prs_match(prs, TOK_BRACEOPEN))
    {
        lox_prs_scopebegin(prs);
        lox_prs_parseblock(prs);
        lox_prs_scopeend(prs);
    }
    else
    {
        lox_prs_parseexprstmt(prs);
    }
}

ObjFunction* lox_prs_compilesource(LoxState* state, const char* source)
{
    AstScanner scn;
    AstParser parser;
    AstCompiler compiler;
    ObjFunction* fn;
    lox_lex_init(state, &scn, source);
    lox_prs_init(state, &parser, &scn);
    lox_prs_compilerinit(&parser, &compiler, TYPE_SCRIPT);
    parser.haderror = false;
    parser.panicmode = false;
    lox_prs_advance(&parser);
    while(!lox_prs_match(&parser, TOK_EOF))
    {
        lox_prs_parsedecl(&parser);
    }
    fn = lox_prs_compilerfinish(&parser, true);
    if(parser.haderror)
    {
        return NULL;
    }
    return fn;
}

void lox_prs_markcompilerroots(LoxState* state)
{
    AstCompiler* compiler;
    if(state->parser != NULL)
    {
        compiler = state->parser->currcompiler;
        while(compiler != NULL)
        {
            lox_gcmem_markobject(state, (Object*)compiler->compiledfn);
            compiler = compiler->enclosing;
        }
    }
}

void lox_vm_stackreset(LoxState* state)
{
    state->vmvars.stacktop = 0;
    state->vmvars.framecount = 0;
    state->openupvalues = NULL;
}

void lox_state_vraiseerror(LoxState* state, const char* format, va_list va)
{
    int i;
    size_t instruction;
    CallFrame* frame;
    ObjFunction* fn;
    state->vmvars.hasraised = true;
    vfprintf(stderr, format, va);
    fputs("\n", stderr);

    /* Types of Values runtime-error < Calls and Functions runtime-error-temp */
    //size_t instruction = state->ip - state->chunk->code - 1;
    //int line = state->chunk->lines[instruction];

    /* Calls and Functions runtime-error-temp < Calls and Functions runtime-error-stack */
    //CallFrame* frame = &state->vmvars.framevalues[state->vmvars.framecount - 1];
    //size_t instruction = frame->ip - frame->innerfn->chunk.code - 1;
    //int line = frame->innerfn->chunk.lines[instruction];
 
    /* Types of Values runtime-error < Calls and Functions runtime-error-stack */
    //fprintf(stderr, "[line %d] in script\n", line);

    for(i = state->vmvars.framecount - 1; i >= 0; i--)
    {
        frame = &state->vmvars.framevalues[i];
        /* Calls and Functions runtime-error-stack < Closures runtime-error-function */
        // ObjFunction* function = frame->innerfn;
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
    lox_vm_stackreset(state);
}

void lox_state_raiseerror(LoxState* state, const char* format, ...)
{
    va_list va;
    va_start(va, format);
    lox_state_vraiseerror(state, format, va);
    va_end(va);
}

void lox_state_defnative(LoxState* state, const char* name, NativeFN nat)
{
    ObjString* os;
    ObjNative* ofn;
    os = lox_string_copy(state, name, (int)strlen(name));
    lox_vm_stackpush(state, OBJ_VAL(os));
    {
        ofn = lox_object_makenative(state, nat);
        lox_vm_stackpush(state, OBJ_VAL(ofn));
        {
            lox_hashtable_set(state, &state->globals, AS_STRING(state->vmvars.stackvalues[0]), state->vmvars.stackvalues[1]);
        }
        lox_vm_stackpop(state);
    }
    lox_vm_stackpop(state);
}


LoxState* lox_state_init()
{
    LoxState* state;
    state = (LoxState*)malloc(sizeof(LoxState));
    if(state == NULL)
    {
        return NULL;
    }
    memset(state, 0, sizeof(LoxState));
    lox_vm_stackreset(state);
    state->vmvars.hasraised = false;
    state->conf.shouldprintruntime = false;
    state->gcstate.linkedobjects = NULL;
    state->gcstate.bytesallocd = 0;
    state->gcstate.nextgc = 1024 * 1024;
    state->gcstate.graycount = 0;
    state->gcstate.graycap = 0;
    state->gcstate.graystack = NULL;
    state->parser = NULL;
    state->stdoutwriter = lox_writer_makeio(state, stdout, false);
    state->stderrwriter = lox_writer_makeio(state, stderr, false);
    lox_hashtable_init(&state->globals);
    lox_hashtable_init(&state->strings);
    state->initstring = NULL;
    state->initstring = lox_string_copy(state, "init", 4);
    return state;
}

void lox_state_free(LoxState* state)
{
    lox_writer_release(state, state->stdoutwriter);
    lox_writer_release(state, state->stderrwriter);
    lox_hashtable_free(state, &state->globals);
    lox_hashtable_free(state, &state->strings);
    state->initstring = NULL;
    lox_vm_gcfreelinkedobjects(state);
    free(state);
}


/* A Virtual Machine run < Calls and Functions run */
static inline int32_t lox_vmbits_readbyte(LoxState* state)
{
    uint32_t r;
    r = *state->vmvars.currframe->ip;
    state->vmvars.currframe->ip++;
    return r;
}

/* Jumping Back and Forth read-short < Calls and Functions run */
static inline uint16_t lox_vmbits_readshort(LoxState* state)
{
    (state)->vmvars.currframe->ip += 2;
    return (uint16_t)((state->vmvars.currframe->ip[-2] << 8) | state->vmvars.currframe->ip[-1]);
}

/* Calls and Functions run < Closures read-constant */
static inline Value lox_vmbits_readconst(LoxState* state)
{
    int32_t b;
    size_t vsz;
    ValArray* vaconst;
    (void)vsz;
    if(state->vmvars.currframe->closure == NULL)
    {
        return NIL_VAL;
    }
    vaconst = &state->vmvars.currframe->closure->innerfn->chunk.constants;
    if(vaconst == NULL)
    {
        return NIL_VAL;
    }
    vsz = vaconst->size;
    b = lox_vmbits_readbyte(state);
    return (vaconst->values[b]);
}

static inline ObjString* lox_vmbits_readstring(LoxState* state)
{
    return AS_STRING(lox_vmbits_readconst(state));
}

static inline void lox_vmbits_stackpush(LoxState* state, Value value)
{
    state->vmvars.stackvalues[state->vmvars.stacktop] = value;
    state->vmvars.stacktop++;
}

static inline Value lox_vmbits_stackpop(LoxState* state)
{
    int64_t stop;
    Value v;
    stop = state->vmvars.stacktop;
    state->vmvars.stacktop--;
    v = state->vmvars.stackvalues[stop - 1];
    return v;
}

static inline Value lox_vmbits_stackpopn(LoxState* state, int32_t n)
{
    int64_t stop;
    Value v;
    stop = state->vmvars.stacktop;
    state->vmvars.stacktop -= n;
    v = state->vmvars.stackvalues[stop - n];
    return v;
}

static inline Value lox_vmbits_stackpeek(LoxState* state, int distance)
{
    return state->vmvars.stackvalues[state->vmvars.stacktop + (-1 - distance)];
}

//---------------------

void lox_vm_stackpush(LoxState* state, Value value)
{
    return lox_vmbits_stackpush(state, value);
}

Value lox_vm_stackpop(LoxState* state)
{
    return lox_vmbits_stackpop(state);
}

Value lox_vm_stackpopn(LoxState* state, int32_t n)
{
    return lox_vmbits_stackpopn(state, n);
}

Value lox_vm_stackpeek(LoxState* state, int distance)
{
    return lox_vmbits_stackpeek(state, distance);
}

bool lox_vmbits_callclosure(LoxState* state, ObjClosure* closure, int argc)
{
    CallFrame* frame;
    if(argc != closure->innerfn->arity)
    {
        lox_state_raiseerror(state, "expected %d arguments but got %d.", closure->innerfn->arity, argc);
        return false;
    }
    if(state->vmvars.framecount == FRAMES_MAX)
    {
        lox_state_raiseerror(state, "stack overflow.");
        return false;
    }
    frame = &state->vmvars.framevalues[state->vmvars.framecount++];
    frame->closure = closure;
    frame->ip = closure->innerfn->chunk.code;
    frame->frstackindex = (state->vmvars.stacktop - argc) - 1;
    return true;
}

bool lox_vmbits_callboundmethod(LoxState* state, Value callee, int argc)
{
    ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
    state->vmvars.stackvalues[state->vmvars.stacktop + (-argc - 1)] = bound->receiver;    
    return lox_vmbits_callclosure(state, bound->method, argc);
}

bool lox_vmbits_callclassconstructor(LoxState* state, Value callee, int argc)
{
    Value initializer;
    ObjClass* klass;
    ObjInstance* instance;
    klass = AS_CLASS(callee);
    instance = lox_object_makeinstance(state, klass);
    state->vmvars.stackvalues[state->vmvars.stacktop + (-argc - 1)] = OBJ_VAL(instance);
    if(lox_hashtable_get(state, &klass->methods, state->initstring, &initializer))
    {
        return lox_vmbits_callclosure(state, AS_CLOSURE(initializer), argc);
    }
    else if(argc != 0)
    {
        lox_state_raiseerror(state, "expected 0 arguments but got %d.", argc);
        return false;
    }
    return true;
}

bool lox_vmbits_callnativefunction(LoxState* state, Value callee, int argc)
{
    Value result;
    Value* vargs;
    NativeFN cfunc;
    ObjNative* nfn;
    nfn = (ObjNative*)AS_OBJ(callee);
    cfunc = nfn->natfunc;
    vargs = (&state->vmvars.stackvalues[0] + state->vmvars.stacktop) - argc;
    result = cfunc(state, argc, vargs);
    state->vmvars.stacktop -= argc + 1;
    lox_vmbits_stackpush(state, result);
    if(state->vmvars.hasraised)
    {
        return false;
    }
    return true;
}

bool lox_vmbits_callvalue(LoxState* state, Value callee, int argc)
{
    if(IS_OBJ(callee))
    {
        switch(OBJ_TYPE(callee))
        {
            case OBJ_BOUNDMETHOD:
                {
                    return lox_vmbits_callboundmethod(state, callee, argc);
                }
                break;
            case OBJ_CLASS:
                {
                    return lox_vmbits_callclassconstructor(state, callee, argc);
                }
                break;
            case OBJ_CLOSURE:
                {
                    return lox_vmbits_callclosure(state, AS_CLOSURE(callee), argc);
                }
                break;
                /* Calls and Functions call-value < Closures call-value-closure */
                #if 0
            case OBJ_FUNCTION: // [switch]
                {
                    return lox_vmbits_callclosure(state, AS_FUNCTION(callee), argc);
                }
                break;
                #endif
            case OBJ_NATIVE:
                {
                    return lox_vmbits_callnativefunction(state, callee, argc);
                }
                break;
            default:
                // Non-callable object type.
                break;
        }
    }
    lox_state_raiseerror(state, "can only call functions and classes.");
    return false;
}

bool lox_vmbits_invokefromclass(LoxState* state, ObjClass* klass, ObjString* name, int argc)
{
    Value method;
    if(!lox_hashtable_get(state, &klass->methods, name, &method))
    {
        lox_state_raiseerror(state, "undefined property '%s'.", name->sbuf->data);
        return false;
    }
    return lox_vmbits_callclosure(state, AS_CLOSURE(method), argc);
}

bool lox_vmbits_invoke(LoxState* state, ObjString* name, int argc)
{
    Value value;
    Value receiver;
    ObjInstance* instance;
    receiver = lox_vmbits_stackpeek(state, argc);
    if(!IS_INSTANCE(receiver))
    {
        lox_state_raiseerror(state, "only instances have methods.");
        return false;
    }
    instance = AS_INSTANCE(receiver);
    if(lox_hashtable_get(state, &instance->fields, name, &value))
    {
        state->vmvars.stackvalues[state->vmvars.stacktop + (-argc - 1)] = value;
        return lox_vmbits_callvalue(state, value, argc);
    }
    return lox_vmbits_invokefromclass(state, instance->klass, name, argc);
}

bool lox_vmbits_bindmethod(LoxState* state, ObjClass* klass, ObjString* name)
{
    Value method;
    ObjBoundMethod* bound;
    if(!lox_hashtable_get(state, &klass->methods, name, &method))
    {
        lox_state_raiseerror(state, "undefined property '%s'.", name->sbuf->data);
        return false;
    }
    bound = lox_object_makeboundmethod(state, lox_vmbits_stackpeek(state, 0), AS_CLOSURE(method));
    lox_vmbits_stackpop(state);
    lox_vmbits_stackpush(state, OBJ_VAL(bound));
    return true;
}

ObjUpvalue* lox_vmbits_captureupval(LoxState* state, Value* local)
{
    ObjUpvalue* upvalue;
    ObjUpvalue* prevupvalue;
    ObjUpvalue* createdupvalue;
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
    createdupvalue = lox_object_makeupvalue(state, local);
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

void lox_vmbits_closeupvals(LoxState* state, Value* last)
{
    ObjUpvalue* upvalue;
    while(state->openupvalues != NULL && state->openupvalues->location >= last)
    {
        upvalue = state->openupvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        state->openupvalues = upvalue->next;
    }
}

void lox_vmbits_defmethod(LoxState* state, ObjString* name)
{
    Value method;
    ObjClass* klass;
    method = lox_vmbits_stackpeek(state, 0);
    klass = AS_CLASS(lox_vmbits_stackpeek(state, 1));
    lox_hashtable_set(state, &klass->methods, name, method);
    lox_vmbits_stackpop(state);
}

static inline bool lox_vmexec_concat(LoxState* state)
{
    ObjString* b;
    ObjString* a;
    ObjString* result;
    Strbuf* sb;
    b = AS_STRING(lox_vmbits_stackpeek(state, 0));
    a = AS_STRING(lox_vmbits_stackpeek(state, 1));
    sb = lox_strbuf_make(state);
    lox_strbuf_append(sb, a->sbuf->data, a->sbuf->length);
    lox_strbuf_append(sb, b->sbuf->data, b->sbuf->length);
    result = lox_string_allocfromstrbuf(state, sb, lox_util_hashstring(sb->data, sb->length));
    lox_vmbits_stackpop(state);
    lox_vmbits_stackpop(state);
    lox_vmbits_stackpush(state, OBJ_VAL(result));
    return true;
}

/* A Virtual Machine binary-op < Types of Values binary-op */
static inline bool lox_vmexec_dobinary(LoxState* state, bool isbool, int32_t op)
{
    double b;
    double a;
    double dw;
    Value res;
    Value peeka;
    Value peekb;
    peeka = lox_vmbits_stackpeek(state, 0);
    peekb = lox_vmbits_stackpeek(state, 1);
    if(!IS_NUMBER(peeka))
    {
        lox_state_raiseerror(state, "operand a (at 0) must be a number");
        return false;
    }
    if(!IS_NUMBER(peekb))
    {
        lox_state_raiseerror(state, "operand b (at 1) must be a number");
        return false;
    }
    b = AS_NUMBER(lox_vmbits_stackpop(state));
    a = AS_NUMBER(lox_vmbits_stackpop(state));
    switch(op)
    {
        case OP_PRIMGREATER:
            {
                dw = a > b;
            }
            break;
        case OP_PRIMLESS:
            {
                dw = a < b;
            }
            break;
        case OP_PRIMADD:
            {
                dw = a + b;
            }
            break;
        case OP_PRIMSUBTRACT:
            {
                dw = a - b;
            }
            break;
        case OP_PRIMMULTIPLY:
            {
                dw = a * b;
            }
            break;
        case OP_PRIMDIVIDE:
            {
                dw = a / b;
            }
            break;
        case OP_PRIMMODULO:
            {
                dw = fmod(a, b);
            }
            break;
        case OP_PRIMBINAND:
            {
                dw = (int)a & (int)b;
            }
            break;
        case OP_PRIMBINOR:
            {
                dw = (int)a | (int)b;
            }
            break;
        case OP_PRIMBINXOR:
            {
                dw = (int)a ^ (int)b;
            }
            break;
        case OP_PRIMSHIFTLEFT:
            {
                dw = (int)a << (int)b;
            }
            break;
        case OP_PRIMSHIFTRIGHT:
            {
                dw = (int)a >> (int)b;
            }
            break;
        default:
            {
                lox_state_raiseerror(state, "unrecognized instruction for binary");
                return false;
            }
            break;
    }
    if(isbool)
    {
        res = BOOL_VAL(dw);
    }
    else
    {
        res = NUMBER_VAL(dw);
    }
    lox_vmbits_stackpush(state, res);
    return true;
}

static inline bool lox_vmexec_indexgetstring(LoxState* state, ObjString* os, Value vidx)
{
    char ch;
    long nidx;
    ObjString* nos;
    if(!IS_NUMBER(vidx))
    {
        lox_state_raiseerror(state, "cannot index strings with non-number type <%s>", lox_writer_valuetypename(vidx));
        return false;
    }
    nidx = AS_NUMBER(vidx);
    if((nidx >= 0) && (nidx < os->sbuf->length))
    {
        ch = os->sbuf->data[nidx];
        nos = lox_string_copy(state, &ch, 1);
        lox_vmbits_stackpush(state, OBJ_VAL(nos));
        return true;
    }
    return false;
}


static inline bool lox_vmexec_indexgetarray(LoxState* state, ObjArray* oa, Value vidx)
{
    long nidx;
    Value val;
    if(!IS_NUMBER(vidx))
    {
        lox_state_raiseerror(state, "cannot index arrays with non-number type <%s>", lox_writer_valuetypename(vidx));
        return false;
    }
    nidx = AS_NUMBER(vidx);
    if((nidx >= 0) && (nidx < (long)oa->vala.size))
    {
        val = oa->vala.values[nidx];
        lox_vmbits_stackpush(state, val);
        return true;
    }
    return false;
}


static inline bool lox_vmexec_indexget(LoxState* state)
{
    Value vidx;
    Value peeked;
    vidx = lox_vmbits_stackpop(state);
    peeked = lox_vmbits_stackpop(state);
    if(IS_STRING(peeked))
    {
        if(lox_vmexec_indexgetstring(state, AS_STRING(peeked), vidx))
        {
            return true;
        }
    }
    else if(IS_ARRAY(peeked))
    {
        if(lox_vmexec_indexgetarray(state, AS_ARRAY(peeked), vidx))
        {
            return true;
        }
    }
    else
    {
        lox_state_raiseerror(state, "cannot get index object type <%s>", lox_writer_valuetypename(peeked));
    }
    lox_vmbits_stackpush(state, NIL_VAL);
    return false;
}

static inline bool lox_vmexec_indexsetarray(LoxState* state, ObjArray* oa, Value vidx, Value setval)
{
    long nidx;
    if(!IS_NUMBER(vidx))
    {
        lox_state_raiseerror(state, "cannot index arrays with non-number type <%s>", lox_writer_valuetypename(vidx));
        return false;
    }
    nidx = AS_NUMBER(vidx);
    lox_writer_writeformat(state->stderrwriter, "indexsetarray: nidx=%d, setval=", nidx);
    lox_writer_printvalue(state->stderrwriter, setval, true);
    lox_writer_writeformat(state->stderrwriter, "\n");
    lox_valarray_insert(&oa->vala, nidx, setval);
    //lox_vmbits_stackpush(state, NIL_VAL);
    return true;
}

static inline bool lox_vmexec_indexset(LoxState* state)
{
    Value vidx;
    Value peeked;
    Value setval;
    setval = lox_vmbits_stackpeek(state, 0);
    vidx = lox_vmbits_stackpeek(state, 1);
    peeked = lox_vmbits_stackpeek(state, 2);
    if(IS_ARRAY(peeked))
    {
        if(lox_vmexec_indexsetarray(state, AS_ARRAY(peeked), vidx, setval))
        {
            return true;
        }
    }
    else
    {
        lox_state_raiseerror(state, "cannot set index object type <%s>", lox_writer_valuetypename(peeked));
    }
    //lox_vmbits_stackpush(state, NIL_VAL);
    return false;
}

static inline bool lox_vmexec_propertyget(LoxState* state)
{
    Value value;
    Value peeked;
    Value vidx;
    ObjString* name;
    ObjInstance* instance;
    peeked = lox_vmbits_stackpeek(state, 0);
    if(!IS_INSTANCE(peeked))
    {
        lox_state_raiseerror(state, "only instances have properties.");
        return false;
    }
    vidx = lox_vmbits_readconst(state);
    lox_vmbits_stackpop(state);// Instance.
    instance = AS_INSTANCE(peeked);
    name = AS_STRING(vidx);
    if(lox_hashtable_get(state, &instance->fields, name, &value))
    {
        lox_vmbits_stackpush(state, value);
        return true;
    }
    if(!lox_vmbits_bindmethod(state, instance->klass, name))
    {
        return false;
    }
    return true;
}

static inline bool lox_vmexec_makearray(LoxState* state)
{
    int i;
    int count;
    Value val;
    ObjArray* array;
    count = lox_vmbits_readbyte(state);
    array = lox_array_make(state);
    fprintf(stderr, "makearray: count=%d\n", count);
    //lox_vmbits_stackpush(state, NIL_VAL);
    state->vmvars.stackvalues[state->vmvars.stacktop + (-count - 1)] = OBJ_VAL(array);
    for(i = count - 1; i >= 0; i--)
    {
        val = lox_vmbits_stackpeek(state, i);
        lox_array_push(array, val);
    }
    #if 0
        lox_vmbits_stackpopn(state, count - 0);
    #else
        if(count > 0)
        {
            for(i=0; i<(count-0); i++)
            {
                lox_vmbits_stackpop(state);
            }
        }
        lox_vmbits_stackpop(state);
    #endif
    lox_vmbits_stackpush(state, OBJ_VAL(array));
    return true;
}

StatusCode lox_vm_runvm(LoxState* state)
{
    size_t icnt;
    int64_t nowpos;
    int64_t spos;
    int64_t stacktop;
    int32_t instruc;
    Value* pslot;
    Writer* owr;
    (void)icnt;
    (void)pslot;
    owr = state->stderrwriter;
    state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];
    for(;;)
    {
        instruc = lox_vmbits_readbyte(state);
        if(state->conf.shouldprintruntime)
        {
            pslot = &state->vmvars.stackvalues[0];
            lox_writer_writeformat(owr, " at %p (instruc=%d):", pslot, instruc);
            lox_dbg_dumpdisasm(state, owr, &state->vmvars.currframe->closure->innerfn->chunk, (int)(state->vmvars.currframe->ip - state->vmvars.currframe->closure->innerfn->chunk.code));
            lox_writer_writestring(owr, " ... stack: [\n");
            icnt = state->vmvars.currframe->frstackindex;
            stacktop = state->vmvars.stacktop;
            icnt = 0;
            spos = 0;
            for(pslot = &state->vmvars.stackvalues[0]; pslot < &state->vmvars.stackvalues[stacktop]; pslot++)
            {
                nowpos = spos;
                spos++;
                lox_writer_writeformat(owr, "  (%ld) ", nowpos);
                lox_writer_printvalue(owr, *pslot, true);
                lox_writer_writeformat(owr, "\n");
            }
            lox_writer_writeformat(owr, "]\n");
        }
        switch(instruc)
        {
            case OP_PUSHCONST:
                {
                    Value constant = lox_vmbits_readconst(state);
                    /* A Virtual Machine op-constant < A Virtual Machine push-constant */
                    /*
                    lox_writer_writeformat(state->stderrwriter, "pushconst: ");
                    lox_writer_printvalue(state->stderrwriter, constant, true);
                    lox_writer_writeformat(state->stderrwriter, "\n");
                    */
                    lox_vmbits_stackpush(state, constant);
                }
                break;
            case OP_PUSHONE:
                {
                    lox_vmbits_stackpush(state, NUMBER_VAL((double)1));
                }
                break;
            case OP_PUSHNIL:
                {
                    lox_vmbits_stackpush(state, NIL_VAL);
                }
                break;
            case OP_PUSHTRUE:
                {
                    lox_vmbits_stackpush(state, BOOL_VAL(true));
                }
                break;
            case OP_PUSHFALSE:
                {
                    lox_vmbits_stackpush(state, BOOL_VAL(false));
                }
                break;
            case OP_MAKEARRAY:
                {
                    if(!lox_vmexec_makearray(state))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case OP_POP:
                {
                    lox_vmbits_stackpop(state);
                }
                break;
            case OP_POPN:
                {
                    int32_t n;
                    n = lox_vmbits_readbyte(state);
                    lox_vmbits_stackpopn(state, n);
                }
                break;
            case OP_DUP:
                {
                    lox_vmbits_stackpush(state, lox_vmbits_stackpeek(state, 0));
                }
                break;
            case OP_LOCALGET:
                {
                    int32_t islot;
                    Value val;
                    islot = lox_vmbits_readbyte(state);
                    val = state->vmvars.stackvalues[state->vmvars.currframe->frstackindex + (islot + 0)];
                    lox_vmbits_stackpush(state, val);
                }
                break;
            case OP_LOCALSET:
                {
                    int32_t islot;
                    Value val;
                    islot = lox_vmbits_readbyte(state);
                    val = lox_vmbits_stackpeek(state, 0);
                    state->vmvars.stackvalues[state->vmvars.currframe->frstackindex + (islot + 0)] = val;
                }
                break;
            case OP_GLOBALGET:
                {
                    Value value;
                    ObjString* name;
                    name = lox_vmbits_readstring(state);
                    if(!lox_hashtable_get(state, &state->globals, name, &value))
                    {
                        lox_state_raiseerror(state, "undefined variable '%s'.", name->sbuf->data);
                        return STATUS_RUNTIMEERROR;
                    }
                    lox_vmbits_stackpush(state, value);
                }
                break;
            case OP_GLOBALDEFINE:
                {
                    ObjString* name;
                    name = lox_vmbits_readstring(state);
                    lox_hashtable_set(state, &state->globals, name, lox_vmbits_stackpeek(state, 0));
                    lox_vmbits_stackpop(state);
                }
                break;            
            case OP_GLOBALSET:
                {
                    ObjString* name;
                    name = lox_vmbits_readstring(state);
                    if(lox_hashtable_set(state, &state->globals, name, lox_vmbits_stackpeek(state, 0)))
                    {
                        //lox_hashtable_delete(state, &state->globals, name);// [delete]
                        //lox_state_raiseerror(state, "undefined variable '%s'.", name->sbuf->data);
                        //return STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case OP_UPVALGET:
                {
                    int32_t islot = lox_vmbits_readbyte(state);
                    lox_vmbits_stackpush(state, *state->vmvars.currframe->closure->upvalues[islot]->location);
                }
                break;
            case OP_UPVALSET:
                {
                    int32_t islot = lox_vmbits_readbyte(state);
                    *state->vmvars.currframe->closure->upvalues[islot]->location = lox_vmbits_stackpeek(state, 0);
                }
                break;

            case OP_INDEXGET:
                {
                    if(!lox_vmexec_indexget(state))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;

            case OP_INDEXSET:
                {
                    if(!lox_vmexec_indexset(state))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;

            
            case OP_PROPERTYGET:
                {
                    if(!lox_vmexec_propertyget(state))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;

            case OP_PROPERTYSET:
                {
                    Value value;
                    ObjInstance* instance;
                    if(!IS_INSTANCE(lox_vmbits_stackpeek(state, 1)))
                    {
                        lox_state_raiseerror(state, "only instances have fields.");
                        return STATUS_RUNTIMEERROR;
                    }
                    instance = AS_INSTANCE(lox_vmbits_stackpeek(state, 1));
                    lox_hashtable_set(state, &instance->fields, lox_vmbits_readstring(state), lox_vmbits_stackpeek(state, 0));
                    value = lox_vmbits_stackpop(state);
                    lox_vmbits_stackpop(state);
                    lox_vmbits_stackpush(state, value);
                }
                break;
            case OP_INSTGETSUPER:
                {
                    ObjString* name;
                    ObjClass* superclass;
                    name = lox_vmbits_readstring(state);
                    superclass = AS_CLASS(lox_vmbits_stackpop(state));
                    if(!lox_vmbits_bindmethod(state, superclass, name))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case OP_EQUAL:
                {
                    Value b = lox_vmbits_stackpop(state);
                    Value a = lox_vmbits_stackpop(state);
                    lox_vmbits_stackpush(state, BOOL_VAL(lox_value_equal(a, b)));
                }
                break;
            case OP_PRIMGREATER:
            case OP_PRIMLESS:
                {
                    if(!lox_vmexec_dobinary(state, true, instruc))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case OP_PRIMADD:
                {
                    if(IS_STRING(lox_vmbits_stackpeek(state, 0)) && IS_STRING(lox_vmbits_stackpeek(state, 1)))
                    {
                        lox_vmexec_concat(state);
                    }
                    else if(IS_NUMBER(lox_vmbits_stackpeek(state, 0)) && IS_NUMBER(lox_vmbits_stackpeek(state, 1)))
                    {
                        if(!lox_vmexec_dobinary(state, false, instruc))
                        {
                            return STATUS_RUNTIMEERROR;
                        }
                    }
                    else
                    {
                        lox_state_raiseerror(state, "operands must be two numbers or two strings.");
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case OP_PRIMSUBTRACT:
            case OP_PRIMMULTIPLY:
            case OP_PRIMDIVIDE:
            case OP_PRIMMODULO:
            case OP_PRIMSHIFTLEFT:
            case OP_PRIMSHIFTRIGHT:
            case OP_PRIMBINAND:
            case OP_PRIMBINOR:
            case OP_PRIMBINXOR:
                {
                    if(!lox_vmexec_dobinary(state, false, instruc))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case OP_PRIMNOT:
                {
                    lox_vmbits_stackpush(state, BOOL_VAL(lox_value_isfalsey(lox_vmbits_stackpop(state))));
                }
                break;
            case OP_PRIMNEGATE:
                {
                    if(!IS_NUMBER(lox_vmbits_stackpeek(state, 0)))
                    {
                        lox_state_raiseerror(state, "operand must be a number.");
                        return STATUS_RUNTIMEERROR;
                    }
                    lox_vmbits_stackpush(state, NUMBER_VAL(-AS_NUMBER(lox_vmbits_stackpop(state))));
                }
                break;
            case OP_DEBUGPRINT:
                {
                    lox_writer_printvalue(state->stderrwriter, lox_vmbits_stackpop(state), true);
                }
                break;
            case OP_JUMPNOW:
                {
                    uint16_t offset;
                    offset = lox_vmbits_readshort(state);
                    state->vmvars.currframe->ip += offset;
                }
                break;

            case OP_JUMPIFFALSE:
                {
                    uint16_t offset;
                    offset = lox_vmbits_readshort(state);
                    if(lox_value_isfalsey(lox_vmbits_stackpeek(state, 0)))
                    {
                        state->vmvars.currframe->ip += offset;
                    }
                }
                break;
            case OP_LOOP:
                {
                    uint16_t offset;
                    offset = lox_vmbits_readshort(state);
                    state->vmvars.currframe->ip -= offset;
                }
                break;
            case OP_CALL:
                {
                    int argc;
                    argc = lox_vmbits_readbyte(state);
                    if(!lox_vmbits_callvalue(state, lox_vmbits_stackpeek(state, argc), argc))
                    {
                        fprintf(stderr, "returning error\n");
                        return STATUS_RUNTIMEERROR;
                    }
                    state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];
                }
                break;

            case OP_INSTTHISINVOKE:
                {
                    int argc;
                    ObjString* method;
                    method = lox_vmbits_readstring(state);
                    argc = lox_vmbits_readbyte(state);
                    if(!lox_vmbits_invoke(state, method, argc))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                    state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];
                }
                break;
            case OP_INSTSUPERINVOKE:
                {
                    int argc;
                    ObjString* method;
                    ObjClass* superclass;
                    method = lox_vmbits_readstring(state);
                    argc = lox_vmbits_readbyte(state);
                    superclass = AS_CLASS(lox_vmbits_stackpop(state));
                    if(!lox_vmbits_invokefromclass(state, superclass, method, argc))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                    state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];
                }
                break;
            case OP_CLOSURE:
                {
                    int i;
                    int32_t index;
                    int32_t islocal;
                    ObjFunction* fn;
                    ObjClosure* closure;
                    fn = AS_FUNCTION(lox_vmbits_readconst(state));
                    closure = lox_object_makeclosure(state, fn);
                    lox_vmbits_stackpush(state, OBJ_VAL(closure));
                    for(i = 0; i < closure->upvaluecount; i++)
                    {
                        islocal = lox_vmbits_readbyte(state);
                        index = lox_vmbits_readbyte(state);
                        if(islocal)
                        {
                            closure->upvalues[i] = lox_vmbits_captureupval(state, &state->vmvars.stackvalues[state->vmvars.currframe->frstackindex + index]);
                        }
                        else
                        {
                            closure->upvalues[i] = state->vmvars.currframe->closure->upvalues[index];
                        }
                    }
                }
                break;
            case OP_UPVALCLOSE:
                {
                    Value* vargs;
                    vargs = (&state->vmvars.stackvalues[0] + state->vmvars.stacktop) - 1;
                    lox_vmbits_closeupvals(state, vargs);
                    lox_vmbits_stackpop(state);
                }
                break;
            case OP_RETURN:
                {
                    int64_t usable;
                    Value result;
                    result = lox_vmbits_stackpop(state);
                    if(state->vmvars.currframe->frstackindex >= 0)
                    {
                        lox_vmbits_closeupvals(state, &state->vmvars.stackvalues[state->vmvars.currframe->frstackindex]);
                    }
                    state->vmvars.framecount--;
                    if(state->vmvars.framecount == 0)
                    {
                        lox_vmbits_stackpop(state);
                        fprintf(stderr, "returning due to OP_RETURN\n");
                        return STATUS_OK;
                    }
                    usable = (state->vmvars.currframe->frstackindex - 0);
                    state->vmvars.stacktop = usable;
                    lox_vmbits_stackpush(state, result);
                    state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];

                }
                break;
            case OP_CLASS:
                {
                    lox_vmbits_stackpush(state, OBJ_VAL(lox_object_makeclass(state, lox_vmbits_readstring(state))));
                }
                break;
            case OP_INHERIT:
                {
                    Value superclass;
                    ObjClass* subclass;
                    superclass = lox_vmbits_stackpeek(state, 1);
                    if(!IS_CLASS(superclass))
                    {
                        lox_state_raiseerror(state, "superclass must be a class.");
                        return STATUS_RUNTIMEERROR;
                    }
                    subclass = AS_CLASS(lox_vmbits_stackpeek(state, 0));
                    lox_hashtable_addall(state, &AS_CLASS(superclass)->methods, &subclass->methods);
                    lox_vmbits_stackpop(state);// Subclass.
                }
                break;

            case OP_METHOD:
                {
                    lox_vmbits_defmethod(state, lox_vmbits_readstring(state));
                }
                break;
            case OP_HALTVM:
                {
                    return STATUS_OK;
                }
                break;
            default:
                {
                    if(instruc != -1)
                    {
                        lox_state_raiseerror(state, "internal error: invalid opcode %d!", instruc);
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;
        }
    }
}


StatusCode lox_state_runsource(LoxState* state, const char* source)
{
    ObjFunction* fn;
    ObjClosure* closure;
    fn = lox_prs_compilesource(state, source);
    if(fn == NULL)
    {
        return STATUS_SYNTAXERROR;
    }
    lox_vm_stackpush(state, OBJ_VAL(fn));
    closure = lox_object_makeclosure(state, fn);
    lox_vm_stackpop(state);
    lox_vm_stackpush(state, OBJ_VAL(closure));
    lox_vmbits_callclosure(state, closure, 0);
    return lox_vm_runvm(state);
}

static char* lox_util_readhandle(FILE* hnd, size_t* dlen)
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

static char* lox_util_readfile(const char* filename, size_t* dlen)
{
    char* b;
    FILE* fh;
    if((fh = fopen(filename, "rb")) == NULL)
    {
        return NULL;
    }
    b = lox_util_readhandle(fh, dlen);
    fclose(fh);
    return b;
}


bool lox_state_runfile(LoxState* state, const char* path)
{
    size_t fsz;
    char* source;
    StatusCode result;
    source = lox_util_readfile(path, &fsz);
    if(source == NULL)
    {
        fprintf(stderr, "failed to read from '%s'\n", path);
        return false;
    }
    result = lox_state_runsource(state, source);
    free(source);
    if(result == STATUS_SYNTAXERROR)
    {
        return false;
    }
    if(result == STATUS_RUNTIMEERROR)
    {
        return false;
    }
    return true;
}

static void repl(LoxState* state)
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
        lox_state_runsource(state, line);
    }
}

static Value cfn_clock(LoxState* state, int argc, Value* argv)
{
    (void)state;
    (void)argc;
    (void)argv;
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value cfn_chr(LoxState* state, int argc, Value* argv)
{
    double dw;
    char ch;
    ObjString* os;
    (void)state;
    (void)argc;
    (void)argv;
    dw = argv[0].as.number;
    ch = (char)dw;
    os = lox_string_copy(state, &ch, 1);
    return OBJ_VAL(os);
}

static Value cfn_print(LoxState* state, int argc, Value* argv)
{
    int i;
    (void)state;
    for(i=0; i<argc; i++)
    {
        lox_writer_printvalue(state->stdoutwriter, argv[i], false);
    }
    return NUMBER_VAL(0);
}

static Value cfn_println(LoxState* state, int argc, Value* argv)
{
    cfn_print(state, argc, argv);
    printf("\n");
    return NUMBER_VAL(0);
}

static Value cfn_arraypush(LoxState* state, int argc, Value* argv)
{
    int i;
    (void)state;
    Value selfval;
    ObjArray* oa;
    selfval = argv[0];
    if(!IS_ARRAY(selfval))
    {
        lox_state_raiseerror(state, "first argument must be array");
    }
    else
    {
        oa = AS_ARRAY(selfval);
        for(i=1; i<argc; i++)
        {
            lox_array_push(oa, argv[i]);
        }
    }
    return NUMBER_VAL(0);
}

static Value cfn_arraycount(LoxState* state, int argc, Value* argv)
{
    (void)state;
    Value selfval;
    ObjArray* oa;
    (void)argc;
    selfval = argv[0];
    if(!IS_ARRAY(selfval))
    {
        lox_state_raiseerror(state, "first argument must be array");
        return NIL_VAL;
    }
    else
    {
        oa = AS_ARRAY(selfval);
        return NUMBER_VAL(oa->vala.size);
    }
    return NUMBER_VAL(0);
}

static Value cfn_arrayset(LoxState* state, int argc, Value* argv)
{
    long idx;
    Value putval;
    Value idxval;
    Value selfval;
    ObjArray* oa;
    (void)state;
    (void)argc;
    selfval = argv[0];
    idxval = argv[1];
    putval = argv[2];
    if(!IS_ARRAY(selfval))
    {
        lox_state_raiseerror(state, "first argument must be array");
        return NIL_VAL;
    }
    else
    {
        if(!IS_NUMBER(idxval))
        {
            lox_state_raiseerror(state, "second argument must be number");
            return NIL_VAL;
        }
        oa = AS_ARRAY(selfval);
        idx = AS_NUMBER(idxval);
        lox_valarray_insert(&oa->vala, idx, putval);
    }
    return NUMBER_VAL(0);
}

static Value cfn_arrayerase(LoxState* state, int argc, Value* argv)
{
    long idx;
    Value idxval;
    Value selfval;
    ObjArray* oa;
    (void)state;
    (void)argc;
    selfval = argv[0];
    idxval = argv[1];
    if(!IS_ARRAY(selfval))
    {
        lox_state_raiseerror(state, "first argument must be array");
        return NIL_VAL;
    }
    else
    {
        if(!IS_NUMBER(idxval))
        {
            lox_state_raiseerror(state, "second argument must be number");
            return NIL_VAL;
        }
        oa = AS_ARRAY(selfval);
        idx = AS_NUMBER(idxval);
        return BOOL_VAL(lox_valarray_erase(&oa->vala, idx));
    }
    return BOOL_VAL(false);
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
    LoxState* state;
    exitcode = 0;
    nargv = argv;
    nargc = argc;
    codeline = NULL;
    state = lox_state_init();
    lox_state_defnative(state, "clock", cfn_clock);
    lox_state_defnative(state, "chr", cfn_chr);
    lox_state_defnative(state, "print", cfn_print);
    lox_state_defnative(state, "println", cfn_println);
    lox_state_defnative(state, "array_count", cfn_arraycount);
    lox_state_defnative(state, "array_push", cfn_arraypush);
    lox_state_defnative(state, "array_erase", cfn_arrayerase);
    lox_state_defnative(state, "array_set", cfn_arrayset);
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
        fprintf(stderr, "codeline=%s\n", codeline);
        lox_state_runsource(state, codeline);
    }
    else
    {
        if(nargc > 1)
        {
            filename = nargv[1];
            fprintf(stderr, "filename=%s\n", filename);
            if(!lox_state_runfile(state, filename))
            {
                exitcode = 1;
            }
        }
        else
        {
            repl(state);
        }
    }
    lox_state_free(state);
    return exitcode;
}
