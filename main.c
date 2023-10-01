

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
#define STACK_MAX (FRAMES_MAX )

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
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)
#define AS_ARRAY(value) ((ObjArray*)AS_OBJ(value))

#define ALLOCATE(vm, type, count) (type*)lox_mem_realloc(vm, NULL, 0, sizeof(type) * (count))

#define FREE(vm, type, pointer) lox_mem_realloc(vm, pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity)*2)

#define GROW_ARRAY(vm, type, pointer, oldcnt, newcnt) (type*)lox_mem_realloc(vm, pointer, sizeof(type) * (oldcnt), sizeof(type) * (newcnt))

#define FREE_ARRAY(vm, type, pointer, oldcnt) lox_mem_realloc(vm, pointer, sizeof(type) * (oldcnt), 0)


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
    PREC_ASSIGNMENT,// =
    PREC_OR,// or
    PREC_AND,// and
    PREC_SHIFT,// << >>
    PREC_EQUALITY,// == !=
    PREC_COMPARISON,// < > <= >=
    PREC_TERM,// + -
    PREC_FACTOR,// * /
    PREC_UNARY,// ! -
    PREC_CALL,// . ()
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
typedef struct /**/VMState VMState;
typedef struct /**/AstToken AstToken;
typedef struct /**/AstRule AstRule;
typedef struct /**/AstLoop AstLoop;
typedef struct /**/AstLocal AstLocal;
typedef struct /**/AstUpvalue AstUpvalue;
typedef struct /**/AstCompiler AstCompiler;
typedef struct /**/AstClassCompiler AstClassCompiler;
typedef struct /**/AstScanner AstScanner;
typedef struct /**/AstParser AstParser;

typedef void (*AstParseFN)(AstParser*, bool);

typedef Value (*NativeFN)(VMState*, int, Value*);



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
    VMState* pvm;

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
    VMState* pvm;
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

struct ObjString
{
    Object obj;
    int length;
    char* chars;
    uint32_t hash;
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
    VMState* pvm;
    const char* start;
    const char* current;
    int line;
};

struct AstParser
{
    VMState* pvm;
    AstScanner* pscn;
    AstToken current;
    AstToken previous;
    bool haderror;
    bool panicmode;
    AstCompiler* currcompiler;
    AstClassCompiler* currclass;
};

struct VMState
{
    bool conf_shouldprintruntime;
    AstParser* parser;
    CallFrame* currframe;
    CallFrame frames[FRAMES_MAX];
    int framecount;
    
    HashTable globals;
    HashTable strings;
    ObjString* initstring;
    ObjUpvalue* openupvalues;


    struct
    {
        size_t bytesallocd;
        size_t nextgc;
        Object* linkedobjects;
        int graycount;
        int graycap;
        Object** graystack;
    } gcstate;


    int64_t stacktop;
    Value stack[STACK_MAX];
 
};

#include "prot.inc"

bool lox_value_isobjtype(Value value, ObjType type)
{
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

void* lox_mem_realloc(VMState* vm, void* pointer, size_t oldsz, size_t newsz)
{
    vm->gcstate.bytesallocd += newsz - oldsz;
    if(newsz > oldsz)
    {
#ifdef DEBUG_STRESS_GC
        lox_gcmem_collectgarbage(vm);
#endif

        if(vm->gcstate.bytesallocd > vm->gcstate.nextgc)
        {
            lox_gcmem_collectgarbage(vm);
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

void lox_gcmem_markobject(VMState* vm, Object* object)
{
    if(object == NULL)
        return;
    if(object->ismarked)
        return;

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
    lox_writer_printvalue(OBJ_VAL(object));
    printf("\n");
#endif

    object->ismarked = true;

    if(vm->gcstate.graycap < vm->gcstate.graycount + 1)
    {
        vm->gcstate.graycap = GROW_CAPACITY(vm->gcstate.graycap);
        vm->gcstate.graystack = (Object**)realloc(vm->gcstate.graystack, sizeof(Object*) * vm->gcstate.graycap);

        if(vm->gcstate.graystack == NULL)
            exit(1);
    }

    vm->gcstate.graystack[vm->gcstate.graycount++] = object;
}

void lox_gcmem_markvalue(VMState* vm, Value value)
{
    if(IS_OBJ(value))
        lox_gcmem_markobject(vm, AS_OBJ(value));
}

void lox_gcmem_markvalarray(VMState* vm, ValArray* array)
{
    for(int i = 0; i < lox_valarray_count(array); i++)
    {
        lox_gcmem_markvalue(vm, array->values[i]);
    }
}

void lox_gcmem_blackenobj(VMState* vm, Object* object)
{
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    lox_writer_printvalue(OBJ_VAL(object));
    printf("\n");
#endif

    switch(object->type)
    {
        case OBJ_BOUNDMETHOD:
            {
                ObjBoundMethod* bound = (ObjBoundMethod*)object;
                lox_gcmem_markvalue(vm, bound->receiver);
                lox_gcmem_markobject(vm, (Object*)bound->method);
            }
            break;
        case OBJ_CLASS:
            {
                ObjClass* klass = (ObjClass*)object;
                lox_gcmem_markobject(vm, (Object*)klass->name);
                lox_gcmem_markhashtable(vm, &klass->methods);
            }
            break;

        case OBJ_CLOSURE:
            {
                ObjClosure* closure = (ObjClosure*)object;
                lox_gcmem_markobject(vm, (Object*)closure->innerfn);
                for(int i = 0; i < closure->upvaluecount; i++)
                {
                    lox_gcmem_markobject(vm, (Object*)closure->upvalues[i]);
                }
            }
            break;
        case OBJ_FUNCTION:
            {
                ObjFunction* ofn = (ObjFunction*)object;
                lox_gcmem_markobject(vm, (Object*)ofn->name);
                lox_gcmem_markvalarray(vm, &ofn->chunk.constants);
            }
            break;
        case OBJ_INSTANCE:
            {
                ObjInstance* instance = (ObjInstance*)object;
                lox_gcmem_markobject(vm, (Object*)instance->klass);
                lox_gcmem_markhashtable(vm, &instance->fields);
            }
            break;
        case OBJ_UPVALUE:
            {
                lox_gcmem_markvalue(vm, ((ObjUpvalue*)object)->closed);
            }
            break;
        case OBJ_ARRAY:
            {
                ObjArray* oa;
                oa = (ObjArray*)object;
                lox_gcmem_markvalarray(vm, &oa->vala);
            }
            break;
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}


void lox_object_release(VMState* vm, Object* object)
{
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif

    switch(object->type)
    {
        case OBJ_BOUNDMETHOD:
            {
                FREE(vm, ObjBoundMethod, object);
            }
            break;
        case OBJ_CLASS:
            {
                ObjClass* klass = (ObjClass*)object;
                lox_hashtable_free(vm, &klass->methods);
                FREE(vm, ObjClass, object);
            }
            break;
        case OBJ_CLOSURE:
            {
                ObjClosure* closure = (ObjClosure*)object;
                FREE_ARRAY(vm, ObjUpvalue*, closure->upvalues, closure->upvaluecount);
                FREE(vm, ObjClosure, object);
            }
            break;

        case OBJ_FUNCTION:
        {
            ObjFunction* ofn = (ObjFunction*)object;
            lox_chunk_free(vm, &ofn->chunk);
            FREE(vm, ObjFunction, object);
            break;
        }
        case OBJ_INSTANCE:
        {
            ObjInstance* instance = (ObjInstance*)object;
            lox_hashtable_free(vm, &instance->fields);
            FREE(vm, ObjInstance, object);
            break;
        }
        case OBJ_NATIVE:
            {
                FREE(vm, ObjNative, object);
            }
            break;
        case OBJ_STRING:
            {
                ObjString* string = (ObjString*)object;
                FREE_ARRAY(vm, char, string->chars, string->length + 1);
                FREE(vm, ObjString, object);
            }
            break;
        case OBJ_ARRAY:
            {
                ObjArray* arr;
                arr = (ObjArray*)object;
                lox_valarray_free(&arr->vala);
                FREE(vm, ObjArray, object);
            }
            break;
        case OBJ_UPVALUE:
            {
                FREE(vm, ObjUpvalue, object);
            }
            break;
    }
}

void lox_gcmem_markroots(VMState* vm)
{
    Value* pslot;
    for(pslot=&vm->stack[0]; pslot < &vm->stack[vm->stacktop]; pslot++)
    {
        lox_gcmem_markvalue(vm, *pslot);
    }

    for(int i = 0; i < vm->framecount; i++)
    {
        lox_gcmem_markobject(vm, (Object*)vm->frames[i].closure);
    }

    for(ObjUpvalue* upvalue = vm->openupvalues; upvalue != NULL; upvalue = upvalue->next)
    {
        lox_gcmem_markobject(vm, (Object*)upvalue);
    }

    lox_gcmem_markhashtable(vm, &vm->globals);
    lox_prs_markcompilerroots(vm);
    lox_gcmem_markobject(vm, (Object*)vm->initstring);
}

void lox_gcmem_tracerefs(VMState* vm)
{
    while(vm->gcstate.graycount > 0)
    {
        Object* object = vm->gcstate.graystack[--vm->gcstate.graycount];
        lox_gcmem_blackenobj(vm, object);
    }
}

void lox_gcmem_sweep(VMState* vm)
{
    Object* previous = NULL;
    Object* object = vm->gcstate.linkedobjects;
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
                vm->gcstate.linkedobjects = object;
            }

            lox_object_release(vm, unreached);
        }
    }
}

void lox_gcmem_collectgarbage(VMState* vm)
{
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = vm->gcstate.bytesallocd;
#endif

    lox_gcmem_markroots(vm);
    lox_gcmem_tracerefs(vm);
    lox_hashtable_remwhite(vm, &vm->strings);
    lox_gcmem_sweep(vm);

    vm->gcstate.nextgc = vm->gcstate.bytesallocd * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n", before - vm->gcstate.bytesallocd, before, vm->gcstate.bytesallocd, vm->gcstate.nextgc);
#endif
}

void lox_vm_gcfreelinkedobjects(VMState* vm)
{
    Object* object = vm->gcstate.linkedobjects;
    while(object != NULL)
    {
        Object* next = object->next;
        lox_object_release(vm, object);
        object = next;
    }

    free(vm->gcstate.graystack);
}

#define lox_valarray_computenextgrow(size) \
    ((size) ? ((size) << 1) : 1)


void lox_valarray_init(VMState* vm, ValArray* array)
{
    array->pvm = vm;
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

void lox_valarray_grow(ValArray* arr, size_t count)
{
    const size_t nsz = count * sizeof(Value);
    void *p1 = arr->values;
    void *p2 = realloc(p1, nsz);
    assert(p2);
    arr->values = p2;
    arr->capacity = count;

}

void lox_valarray_push(ValArray* arr, Value value)
{
    /*
    int oldcap;
    if(arr->capacity < arr->size + 1)
    {
        oldcap = arr->capacity;
        arr->capacity = GROW_CAPACITY(oldcap);
        arr->values = GROW_ARRAY(arr->pvm, Value, arr->values, oldcap, arr->capacity);
    }
    arr->values[arr->size] = value;
    arr->size++;
    */

    size_t cap = arr->capacity;
    if(cap <= arr->size)
    {
        lox_valarray_grow(arr, lox_valarray_computenextgrow(cap));
    }
    arr->values[arr->size] = (value);
    arr->size++;
}

void lox_valarray_insert(ValArray* arr, size_t pos, Value val)
{
    size_t oldcap;
    oldcap = arr->capacity;
    if (oldcap <= arr->size)
    {
        lox_valarray_grow(arr, lox_valarray_computenextgrow(oldcap));
    }
    if(pos < arr->size)
    {
        memmove(arr->values + pos + 1, arr->values + pos, sizeof(Value) * (arr->size - pos));
    }
    arr->values[pos] = val;
    arr->size++;
}

void lox_valarray_erase(ValArray* arr, size_t i)
{
    const size_t osz = arr->size;
    if (i < osz)
    {
        /*
        cvector_elem_destructor_t elem_destructor__ = cvector_elem_destructor(vec);
        if (arr->dtorfunc)
        {
            arr->dtorfunc(&arr->values[i]);
        }
        */
        arr->size = osz - 1;
        arr->size--;
        memmove(arr->values + i, arr->values + i + 1, sizeof(Value) * (osz - 1 - i));
    }

}

Value lox_valarray_pop(ValArray* arr)
{
    arr->size--;
    return arr->values[arr->size + 1];
}

void lox_valarray_free(ValArray* arr)
{
    VMState* vm;
    vm = arr->pvm;
    FREE_ARRAY(vm, Value, arr->values, arr->capacity);
    lox_valarray_init(vm, arr);
}

void lox_writer_printfunction(ObjFunction* ofn)
{
    if(ofn->name == NULL)
    {
        printf("<script>");
        return;
    }
    printf("<fn %s>", ofn->name->chars);
}

void lox_writer_printarray(ObjArray* arr)
{
    size_t i;
    size_t asz;
    asz = lox_valarray_size(&arr->vala);
    printf("[");
    for(i=0; i<asz; i++)
    {
        lox_writer_printvalue(arr->vala.values[i]);
        if((i+1) < asz)
        {
            printf(",");
        }
    }
    printf("]");
}

void lox_writer_printobject(Value value)
{
    switch(OBJ_TYPE(value))
    {
        case OBJ_BOUNDMETHOD:
            {
                lox_writer_printfunction(AS_BOUND_METHOD(value)->method->innerfn);
            }
            break;
        case OBJ_CLASS:
            {
                printf("%s", AS_CLASS(value)->name->chars);
            }
            break;
        case OBJ_CLOSURE:
            {
                lox_writer_printfunction(AS_CLOSURE(value)->innerfn);
            }
            break;
        case OBJ_FUNCTION:
            {
                lox_writer_printfunction(AS_FUNCTION(value));
            }
            break;
        case OBJ_INSTANCE:
            {
                printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
            }
            break;
        case OBJ_NATIVE:
            {
                printf("<native fn>");
            }
            break;
        case OBJ_STRING:
            {
                printf("%s", AS_CSTRING(value));
            }
            break;
        case OBJ_UPVALUE:
            {
                printf("upvalue");
            }
            break;
        case OBJ_ARRAY:
            {
                printf("array printing?");
                lox_writer_printarray(AS_ARRAY(value));
            }
            break;
    }
}

void lox_writer_printvalue(Value value)
{
    switch(value.type)
    {
        case VAL_BOOL:
            printf(AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NIL:
            printf("nil");
            break;
        case VAL_NUMBER:
            printf("%g", AS_NUMBER(value));
            break;
        case VAL_OBJ:
            lox_writer_printobject(value);
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
    return "?unknown?";
}


bool lox_value_equal(Value a, Value b)
{
    if(a.type != b.type)
        return false;
    switch(a.type)
    {
        case VAL_BOOL:
            return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:
            return true;
        case VAL_NUMBER:
            return AS_NUMBER(a) == AS_NUMBER(b);
            /* Strings strings-equal < Hash Tables equal
    case VAL_OBJ: {
      ObjString* aString = AS_STRING(a);
      ObjString* bString = AS_STRING(b);
      return aString->length == bString->length &&
          memcmp(aString->chars, bString->chars,
                 aString->length) == 0;
    }
 */
        case VAL_OBJ:
            return AS_OBJ(a) == AS_OBJ(b);
        default:
            return false;// Unreachable.
    }
}

Object* lox_object_allocobj(VMState* vm, size_t size, ObjType type)
{
    Object* object = (Object*)lox_mem_realloc(vm, NULL, 0, size);
    object->type = type;
    object->ismarked = false;

    object->next = vm->gcstate.linkedobjects;
    vm->gcstate.linkedobjects = object;

#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif

    return object;
}

ObjBoundMethod* lox_object_makeboundmethod(VMState* vm, Value receiver, ObjClosure* method)
{
    ObjBoundMethod* bound = (ObjBoundMethod*)lox_object_allocobj(vm, sizeof(ObjBoundMethod), OBJ_BOUNDMETHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjClass* lox_object_makeclass(VMState* vm, ObjString* name)
{
    ObjClass* klass = (ObjClass*)lox_object_allocobj(vm, sizeof(ObjClass), OBJ_CLASS);
    klass->name = name;// [klass]
    lox_hashtable_init(&klass->methods);
    return klass;
}

ObjClosure* lox_object_makeclosure(VMState* vm, ObjFunction* ofn)
{
    ObjUpvalue** upvalues = ALLOCATE(vm, ObjUpvalue*, ofn->upvaluecount);
    for(int i = 0; i < ofn->upvaluecount; i++)
    {
        upvalues[i] = NULL;
    }

    ObjClosure* closure = (ObjClosure*)lox_object_allocobj(vm, sizeof(ObjClosure), OBJ_CLOSURE);
    closure->innerfn = ofn;
    closure->upvalues = upvalues;
    closure->upvaluecount = ofn->upvaluecount;
    return closure;
}

ObjFunction* lox_object_makefunction(VMState* vm)
{
    ObjFunction* ofn = (ObjFunction*)lox_object_allocobj(vm, sizeof(ObjFunction), OBJ_FUNCTION);
    ofn->arity = 0;
    ofn->upvaluecount = 0;
    ofn->name = NULL;
    lox_chunk_init(vm, &ofn->chunk);
    return ofn;
}

ObjInstance* lox_object_makeinstance(VMState* vm, ObjClass* klass)
{
    ObjInstance* instance = (ObjInstance*)lox_object_allocobj(vm, sizeof(ObjInstance), OBJ_INSTANCE);
    instance->klass = klass;
    lox_hashtable_init(&instance->fields);
    return instance;
}

ObjNative* lox_object_makenative(VMState* vm, NativeFN nat)
{
    ObjNative* native = (ObjNative*)lox_object_allocobj(vm, sizeof(ObjNative), OBJ_NATIVE);
    native->natfunc = nat;
    return native;
}


ObjString* lox_string_allocate(VMState* vm, char* chars, int length, uint32_t hash)
{
    ObjString* string = (ObjString*)lox_object_allocobj(vm, sizeof(ObjString), OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;

    lox_vm_stackpush(vm, OBJ_VAL(string));
    lox_hashtable_set(vm, &vm->strings, string, NIL_VAL);
    lox_vm_stackpop(vm);

    return string;
}

uint32_t lox_util_hashstring(const char* key, int length)
{
    uint32_t hash = 2166136261u;
    for(int i = 0; i < length; i++)
    {
        hash ^= (int32_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

ObjString* lox_string_take(VMState* vm, char* chars, int length)
{
    uint32_t hash = lox_util_hashstring(chars, length);
    ObjString* interned = lox_hashtable_findstring(vm, &vm->strings, chars, length, hash);
    if(interned != NULL)
    {
        FREE_ARRAY(vm, char, chars, length + 1);
        return interned;
    }

    return lox_string_allocate(vm, chars, length, hash);
}

ObjString* lox_string_copy(VMState* vm, const char* chars, int length)
{
    uint32_t hash = lox_util_hashstring(chars, length);
    ObjString* interned = lox_hashtable_findstring(vm, &vm->strings, chars, length, hash);
    if(interned != NULL)
        return interned;

    char* heapchars = ALLOCATE(vm, char, length + 1);
    memcpy(heapchars, chars, length);
    heapchars[length] = '\0';
    return lox_string_allocate(vm, heapchars, length, hash);
}

ObjArray* lox_array_make(VMState* vm)
{
    ObjArray* oa;
    oa = (ObjArray*)lox_object_allocobj(vm, sizeof(ObjArray), OBJ_ARRAY);
    oa->pvm = vm;
    lox_valarray_init(vm, &oa->vala);
    oa->vala.size = 0;
    return oa;
}

bool lox_array_push(ObjArray* arr, Value val)
{
    lox_valarray_push(&arr->vala, val);
    return true;
}

ObjUpvalue* lox_object_makeupvalue(VMState* vm, Value* pslot)
{
    ObjUpvalue* upvalue = (ObjUpvalue*)lox_object_allocobj(vm, sizeof(ObjUpvalue), OBJ_UPVALUE);
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

void lox_hashtable_free(VMState* vm, HashTable* table)
{
    FREE_ARRAY(vm, HashEntry, table->entries, table->capacity);
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

bool lox_hashtable_get(VMState* vm, HashTable* table, ObjString* key, Value* value)
{
    HashEntry* entry;
    (void)vm;
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

void lox_hashtable_adjustcap(VMState* vm, HashTable* table, int capacity)
{
    HashEntry* entries = ALLOCATE(vm, HashEntry, capacity);
    for(int i = 0; i < capacity; i++)
    {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for(int i = 0; i < table->capacity; i++)
    {
        HashEntry* entry = &table->entries[i];
        if(entry->key == NULL)
            continue;

        HashEntry* dest = lox_hashtable_findentry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(vm, HashEntry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool lox_hashtable_set(VMState* vm, HashTable* table, ObjString* key, Value value)
{
    if(table->count + 1 > table->capacity * TABLE_MAX_LOAD)
    {
        int capacity = GROW_CAPACITY(table->capacity);
        lox_hashtable_adjustcap(vm, table, capacity);
    }

    HashEntry* entry = lox_hashtable_findentry(table->entries, table->capacity, key);
    bool isnewkey = entry->key == NULL;
    if(isnewkey && IS_NIL(entry->value))
        table->count++;

    entry->key = key;
    entry->value = value;
    return isnewkey;
}

bool lox_hashtable_delete(VMState* vm, HashTable* table, ObjString* key)
{
    (void)vm;
    if(table->count == 0)
        return false;

    // Find the entry.
    HashEntry* entry = lox_hashtable_findentry(table->entries, table->capacity, key);
    if(entry->key == NULL)
        return false;

    // Place a tombstone in the entry.
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

void lox_hashtable_addall(VMState* vm, HashTable* from, HashTable* to)
{
    for(int i = 0; i < from->capacity; i++)
    {
        HashEntry* entry = &from->entries[i];
        if(entry->key != NULL)
        {
            lox_hashtable_set(vm, to, entry->key, entry->value);
        }
    }
}

ObjString* lox_hashtable_findstring(VMState* vm, HashTable* table, const char* chars, int length, uint32_t hash)
{
    (void)vm;
    if(table->count == 0)
        return NULL;
    uint32_t index = hash & (table->capacity - 1);
    for(;;)
    {
        HashEntry* entry = &table->entries[index];
        if(entry->key == NULL)
        {
            // Stop if we find an empty non-tombstone entry.
            if(IS_NIL(entry->value))
                return NULL;
        }
        else if(entry->key->length == length && entry->key->hash == hash && memcmp(entry->key->chars, chars, length) == 0)
        {
            // We found it.
            return entry->key;
        }
        index = (index + 1) & (table->capacity - 1);
    }
}

void lox_hashtable_remwhite(VMState* vm, HashTable* table)
{
    for(int i = 0; i < table->capacity; i++)
    {
        HashEntry* entry = &table->entries[i];
        if(entry->key != NULL && !entry->key->obj.ismarked)
        {
            lox_hashtable_delete(vm, table, entry->key);
        }
    }
}

void lox_gcmem_markhashtable(VMState* vm, HashTable* table)
{
    for(int i = 0; i < table->capacity; i++)
    {
        HashEntry* entry = &table->entries[i];
        lox_gcmem_markobject(vm, (Object*)entry->key);
        lox_gcmem_markvalue(vm, entry->value);
    }
}

void lox_chunk_init(VMState* vm, Chunk* chunk)
{
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    lox_valarray_init(vm, &chunk->constants);
}

void lox_chunk_free(VMState* vm, Chunk* chunk)
{
    FREE_ARRAY(vm, int32_t, chunk->code, chunk->capacity);
    FREE_ARRAY(vm, int, chunk->lines, chunk->capacity);
    lox_valarray_free(&chunk->constants);
    lox_chunk_init(vm, chunk);
}

void lox_chunk_pushbyte(VMState* vm, Chunk* chunk, int32_t byte, int line)
{
    int oldcap;
    if(chunk->capacity < chunk->count + 1)
    {
        oldcap = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldcap);
        chunk->code = GROW_ARRAY(vm, int32_t, chunk->code, oldcap, chunk->capacity);
        chunk->lines = GROW_ARRAY(vm, int, chunk->lines, oldcap, chunk->capacity);
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int lox_chunk_pushconst(VMState* vm, Chunk* chunk, Value value)
{
    lox_vm_stackpush(vm, value);
    lox_valarray_push(&chunk->constants, value);
    lox_vm_stackpop(vm);
    return lox_valarray_count(&chunk->constants) - 1;
}

void lox_chunk_disasm(Chunk* chunk, const char* name)
{
    printf("== %s ==\n", name);

    for(int offset = 0; offset < chunk->count;)
    {
        offset = lox_dbg_dumpdisasm(chunk, offset);
    }
}

int lox_dbg_dumpconstinstr(const char* name, Chunk* chunk, int offset)
{
    Value val;
    int32_t constant;
    val = NIL_VAL;
    constant = chunk->code[offset + 1];
    if(chunk->constants.size > 0)
    {
        val = chunk->constants.values[constant];
    }
    printf("%-16s %4d '", name, constant);
    
    lox_writer_printvalue(val);
    printf("'\n");
    return offset + 2;
}

int lox_dbg_dumpinvokeinstr(const char* name, Chunk* chunk, int offset)
{
    int32_t constant = chunk->code[offset + 1];
    int32_t argc = chunk->code[offset + 2];
    printf("%-16s (%d args) %4d '", name, argc, constant);
    lox_writer_printvalue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 3;
}

int lox_dbg_dumpsimpleinstr(const char* name, int offset)
{
    printf("%s\n", name);
    return offset + 1;
}

int lox_dbg_dumpbyteinstr(const char* name, Chunk* chunk, int offset)
{
    int32_t islot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, islot);
    return offset + 2;// [debug]
}

int lox_dbg_dumpjumpinstr(const char* name, int sign, Chunk* chunk, int offset)
{
    uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

int lox_dbg_dumpdisasm(Chunk* chunk, int offset)
{
    printf("%04d ", offset);
    if((offset > 0) && (chunk->lines[offset] == chunk->lines[offset - 1]))
    {
        printf("   | ");
    }
    else
    {
        printf("%4d ", chunk->lines[offset]);
    }

    int32_t instruction = chunk->code[offset];
    switch(instruction)
    {
        case OP_PUSHCONST:
            return lox_dbg_dumpconstinstr("OP_PUSHCONST", chunk, offset);
        case OP_PUSHONE:
            return lox_dbg_dumpsimpleinstr("OP_PUSHONE", offset);
        case OP_PUSHNIL:
            return lox_dbg_dumpsimpleinstr("OP_PUSHNIL", offset);
        case OP_PUSHTRUE:
            return lox_dbg_dumpsimpleinstr("OP_PUSHTRUE", offset);
        case OP_PUSHFALSE:
            return lox_dbg_dumpsimpleinstr("OP_PUSHFALSE", offset);
        case OP_MAKEARRAY:
            return lox_dbg_dumpconstinstr("OP_MAKEARRAY", chunk, offset);
        case OP_INDEXGET:
            return lox_dbg_dumpbyteinstr("OP_INDEXGET", chunk, offset);
        case OP_POP:
            return lox_dbg_dumpsimpleinstr("OP_POP", offset);
        case OP_LOCALGET:
            return lox_dbg_dumpbyteinstr("OP_LOCALGET", chunk, offset);
        case OP_LOCALSET:
            return lox_dbg_dumpbyteinstr("OP_LOCALSET", chunk, offset);
        case OP_GLOBALGET:
            return lox_dbg_dumpconstinstr("OP_GLOBALGET", chunk, offset);
        case OP_GLOBALDEFINE:
            return lox_dbg_dumpconstinstr("OP_GLOBALDEFINE", chunk, offset);
        case OP_GLOBALSET:
            return lox_dbg_dumpconstinstr("OP_GLOBALSET", chunk, offset);
        case OP_UPVALGET:
            return lox_dbg_dumpbyteinstr("OP_UPVALGET", chunk, offset);
        case OP_UPVALSET:
            return lox_dbg_dumpbyteinstr("OP_UPVALSET", chunk, offset);
        case OP_PROPERTYGET:
            return lox_dbg_dumpconstinstr("OP_PROPERTYGET", chunk, offset);
        case OP_PROPERTYSET:
            return lox_dbg_dumpconstinstr("OP_PROPERTYSET", chunk, offset);
        case OP_INSTGETSUPER:
            return lox_dbg_dumpconstinstr("OP_INSTGETSUPER", chunk, offset);
        case OP_EQUAL:
            return lox_dbg_dumpsimpleinstr("OP_EQUAL", offset);
        case OP_PRIMGREATER:
            return lox_dbg_dumpsimpleinstr("OP_PRIMGREATER", offset);
        case OP_PRIMLESS:
            return lox_dbg_dumpsimpleinstr("OP_PRIMLESS", offset);
        case OP_PRIMADD:
            return lox_dbg_dumpsimpleinstr("OP_PRIMADD", offset);
        case OP_PRIMSUBTRACT:
            return lox_dbg_dumpsimpleinstr("OP_PRIMSUBTRACT", offset);
        case OP_PRIMMULTIPLY:
            return lox_dbg_dumpsimpleinstr("OP_PRIMMULTIPLY", offset);
        case OP_PRIMDIVIDE:
            return lox_dbg_dumpsimpleinstr("OP_PRIMDIVIDE", offset);
        case OP_PRIMNOT:
            return lox_dbg_dumpsimpleinstr("OP_PRIMNOT", offset);
        case OP_PRIMNEGATE:
            return lox_dbg_dumpsimpleinstr("OP_PRIMNEGATE", offset);
        case OP_DEBUGPRINT:
            return lox_dbg_dumpsimpleinstr("OP_PRINT", offset);
        case OP_JUMPNOW:
            return lox_dbg_dumpjumpinstr("OP_JUMPNOW", 1, chunk, offset);
        case OP_JUMPIFFALSE:
            return lox_dbg_dumpjumpinstr("OP_JUMPIFFALSE", 1, chunk, offset);
        case OP_LOOP:
            return lox_dbg_dumpjumpinstr("OP_LOOP", -1, chunk, offset);
        case OP_CALL:
            return lox_dbg_dumpbyteinstr("OP_CALL", chunk, offset);
        case OP_INSTTHISINVOKE:
            return lox_dbg_dumpinvokeinstr("OP_INSTTHISINVOKE", chunk, offset);
        case OP_INSTSUPERINVOKE:
            return lox_dbg_dumpinvokeinstr("OP_INSTSUPERINVOKE", chunk, offset);
        case OP_CLOSURE:
        {
            offset++;
            int32_t constant = chunk->code[offset++];
            printf("%-16s %4d ", "OP_CLOSURE", constant);
            lox_writer_printvalue(chunk->constants.values[constant]);
            printf("\n");

            ObjFunction* fn = AS_FUNCTION(chunk->constants.values[constant]);
            for(int j = 0; j < fn->upvaluecount; j++)
            {
                int islocal = chunk->code[offset++];
                int index = chunk->code[offset++];
                printf("%04d      |                     %s %d\n", offset - 2, islocal ? "local" : "upvalue", index);
            }

            return offset;
        }
        case OP_UPVALCLOSE:
            return lox_dbg_dumpsimpleinstr("OP_UPVALCLOSE", offset);
        case OP_RETURN:
            return lox_dbg_dumpsimpleinstr("OP_RETURN", offset);
        case OP_CLASS:
            return lox_dbg_dumpconstinstr("OP_CLASS", chunk, offset);
        case OP_INHERIT:
            return lox_dbg_dumpsimpleinstr("OP_INHERIT", offset);
        case OP_METHOD:
            return lox_dbg_dumpconstinstr("OP_METHOD", chunk, offset);
        default:
            printf("unknown opcode %d\n", instruction);
            return offset + 1;
    }
}

void lox_lex_init(VMState* vm, AstScanner* scn, const char* source)
{
    scn->pvm = vm;
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
    for(;;)
    {
        char c = lox_lex_peekcurrent(scn);
        switch(c)
        {
            case ' ':
            case '\r':
            case '\t':
                lox_lex_advance(scn);
                break;
            case '\n':
                scn->line++;
                lox_lex_advance(scn);
                break;
            case '/':
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
        lox_lex_advance(scn);
    return lox_lex_maketoken(scn, lox_lex_scankeyword(scn));
}

AstToken lox_lex_scannumber(AstScanner* scn)
{
    while(lox_lex_isdigit(lox_lex_peekcurrent(scn)))
        lox_lex_advance(scn);

    // Look for a fractional part.
    if(lox_lex_peekcurrent(scn) == '.' && lox_lex_isdigit(lox_lex_peeknext(scn)))
    {
        // Consume the ".".
        lox_lex_advance(scn);

        while(lox_lex_isdigit(lox_lex_peekcurrent(scn)))
            lox_lex_advance(scn);
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

void lox_prs_init(VMState* vm, AstParser* parser, AstScanner* scn)
{
    vm->parser = parser;
    parser->pvm = vm;
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

ObjFunction* lox_prs_compilerfinish(AstParser* prs)
{
    ObjFunction* fn;
    lox_prs_emitreturn(prs);
    fn = prs->currcompiler->compiledfn;
#if (DEBUG_PRINT_CODE == 1)
    if(!prs->haderror)
    {
        lox_chunk_disasm(lox_prs_currentchunk(prs), fn->name != NULL ? fn->name->chars : "<script>");
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
        /*
        if(arg != -1)
        {
            if(getop == OpCode::OP_INDEXGET || getop == OpCode::OP_INDEXRANGEDGET)
            {
                emitbytes(getop, (int32_t)0);
            }
            else
            {
                emitbyteandshort(getop, arg);
            }
        }
        else
        */
        lox_prs_emit2byte(prs, getop, (int32_t)arg);
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
    (void)canassign;
    AstTokType ot;
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
    fprintf(stderr, "parsing index?\n");
    lox_prs_parseexpr(prs);
    lox_prs_consume(prs, TOK_BRACKETCLOSE, "expected ']' after indexing");
    lox_prs_emit1byte(prs, OP_INDEXGET);
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
    fn = lox_prs_compilerfinish(prs);
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
    //lox_prs_consume(prs, TOK_SEMICOLON, "expect ';' after variable declaration.");
    lox_prs_skipsemicolon(prs);
    lox_prs_emitdefvar(prs, global);
}

void lox_prs_parseexprstmt(AstParser* prs)
{
    lox_prs_parseexpr(prs);
    //lox_prs_consume(prs, TOK_SEMICOLON, "expect ';' after expression");
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

ObjFunction* lox_prs_compilesource(VMState* vm, const char* source)
{
    AstScanner scn;
    AstParser parser;
    AstCompiler compiler;
    ObjFunction* fn;
    lox_lex_init(vm, &scn, source);
    lox_prs_init(vm, &parser, &scn);
    lox_prs_compilerinit(&parser, &compiler, TYPE_SCRIPT);
    parser.haderror = false;
    parser.panicmode = false;
    lox_prs_advance(&parser);
    while(!lox_prs_match(&parser, TOK_EOF))
    {
        lox_prs_parsedecl(&parser);
    }
    fn = lox_prs_compilerfinish(&parser);
    if(parser.haderror)
    {
        return NULL;
    }
    return fn;
}

void lox_prs_markcompilerroots(VMState* vm)
{
    AstCompiler* compiler;
    if(vm->parser != NULL)
    {
        compiler = vm->parser->currcompiler;
        while(compiler != NULL)
        {
            lox_gcmem_markobject(vm, (Object*)compiler->compiledfn);
            compiler = compiler->enclosing;
        }
    }
}

void lox_vm_stackreset(VMState* vm)
{
    vm->stacktop = 0;
    vm->framecount = 0;
    vm->openupvalues = NULL;
}

void lox_state_vraiseerror(VMState* vm, const char* format, va_list va)
{
    int i;
    size_t instruction;
    CallFrame* frame;
    ObjFunction* fn;
    vfprintf(stderr, format, va);
    fputs("\n", stderr);

    /* Types of Values runtime-error < Calls and Functions runtime-error-temp */
    //size_t instruction = vm->ip - vm->chunk->code - 1;
    //int line = vm->chunk->lines[instruction];

    /* Calls and Functions runtime-error-temp < Calls and Functions runtime-error-stack */
    //CallFrame* frame = &vm->frames[vm->framecount - 1];
    //size_t instruction = frame->ip - frame->innerfn->chunk.code - 1;
    //int line = frame->innerfn->chunk.lines[instruction];
 
    /* Types of Values runtime-error < Calls and Functions runtime-error-stack */
    //fprintf(stderr, "[line %d] in script\n", line);

    for(i = vm->framecount - 1; i >= 0; i--)
    {
        frame = &vm->frames[i];
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
            fprintf(stderr, "%s()\n", fn->name->chars);
        }
    }
    lox_vm_stackreset(vm);
}

void lox_state_raiseerror(VMState* vm, const char* format, ...)
{
    va_list va;
    va_start(va, format);
    lox_state_vraiseerror(vm, format, va);
    va_end(va);
}

void lox_state_defnative(VMState* vm, const char* name, NativeFN nat)
{
    ObjString* os;
    ObjNative* ofn;
    os = lox_string_copy(vm, name, (int)strlen(name));
    lox_vm_stackpush(vm, OBJ_VAL(os));
    {
        ofn = lox_object_makenative(vm, nat);
        lox_vm_stackpush(vm, OBJ_VAL(ofn));
        {
            lox_hashtable_set(vm, &vm->globals, AS_STRING(vm->stack[0]), vm->stack[1]);
        }
        lox_vm_stackpop(vm);
    }
    lox_vm_stackpop(vm);
}


void lox_state_init(VMState* vm)
{
    lox_vm_stackreset(vm);
    vm->conf_shouldprintruntime = false;
    vm->gcstate.linkedobjects = NULL;
    vm->gcstate.bytesallocd = 0;
    vm->gcstate.nextgc = 1024 * 1024;
    vm->gcstate.graycount = 0;
    vm->gcstate.graycap = 0;
    vm->gcstate.graystack = NULL;
    vm->parser = NULL;
    lox_hashtable_init(&vm->globals);
    lox_hashtable_init(&vm->strings);
    vm->initstring = NULL;
    vm->initstring = lox_string_copy(vm, "init", 4);
}

void lox_state_free(VMState* vm)
{
    lox_hashtable_free(vm, &vm->globals);
    lox_hashtable_free(vm, &vm->strings);
    vm->initstring = NULL;
    lox_vm_gcfreelinkedobjects(vm);
}

void lox_vm_stackpush(VMState* vm, Value value)
{
    vm->stack[vm->stacktop] = value;
    vm->stacktop++;
}

Value lox_vm_stackpop(VMState* vm)
{
    int64_t stop;
    Value v;
    stop = vm->stacktop;
    vm->stacktop--;
    v = vm->stack[stop - 1];
    return v;
}

Value lox_vm_stackpopn(VMState* vm, int32_t n)
{
    int64_t stop;
    Value v;
    stop = vm->stacktop;
    vm->stacktop -= n;
    v = vm->stack[stop - n];
    return v;
}

Value lox_vm_stackpeek(VMState* vm, int distance)
{
    return vm->stack[vm->stacktop + (-1 - distance)];
}

bool lox_vm_callclosure(VMState* vm, ObjClosure* closure, int argc)
{
    CallFrame* frame;
    if(argc != closure->innerfn->arity)
    {
        lox_state_raiseerror(vm, "expected %d arguments but got %d.", closure->innerfn->arity, argc);
        return false;
    }
    if(vm->framecount == FRAMES_MAX)
    {
        lox_state_raiseerror(vm, "stack overflow.");
        return false;
    }
    frame = &vm->frames[vm->framecount++];
    frame->closure = closure;
    frame->ip = closure->innerfn->chunk.code;
    frame->frstackindex = (vm->stacktop - argc) - 1;
    return true;
}

bool lox_vm_callboundmethod(VMState* vm, Value callee, int argc)
{
    ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
    vm->stack[vm->stacktop + (-argc - 1)] = bound->receiver;    
    return lox_vm_callclosure(vm, bound->method, argc);
}

bool lox_vm_callclassconstructor(VMState* vm, Value callee, int argc)
{
    Value initializer;
    ObjClass* klass;
    ObjInstance* instance;
    klass = AS_CLASS(callee);
    instance = lox_object_makeinstance(vm, klass);
    vm->stack[vm->stacktop + (-argc - 1)] = OBJ_VAL(instance);
    if(lox_hashtable_get(vm, &klass->methods, vm->initstring, &initializer))
    {
        return lox_vm_callclosure(vm, AS_CLOSURE(initializer), argc);
    }
    else if(argc != 0)
    {
        lox_state_raiseerror(vm, "expected 0 arguments but got %d.", argc);
        return false;
    }
    return true;
}

bool lox_vm_callnativefunction(VMState* vm, Value callee, int argc)
{
    Value result;
    Value* vargs;
    NativeFN cfunc;
    cfunc = ((ObjNative*)AS_OBJ(callee))->natfunc;
    vargs = (&vm->stack[0] + vm->stacktop) - argc;
    result = cfunc(vm, argc, vargs);
    vm->stacktop -= argc + 1;
    lox_vm_stackpush(vm, result);
    return true;
}

bool lox_vm_callvalue(VMState* vm, Value callee, int argc)
{
    if(IS_OBJ(callee))
    {
        switch(OBJ_TYPE(callee))
        {
            case OBJ_BOUNDMETHOD:
                {
                    return lox_vm_callboundmethod(vm, callee, argc);
                }
                break;
            case OBJ_CLASS:
                {
                    return lox_vm_callclassconstructor(vm, callee, argc);
                }
                break;
            case OBJ_CLOSURE:
                {
                    return lox_vm_callclosure(vm, AS_CLOSURE(callee), argc);
                }
                break;
                /* Calls and Functions call-value < Closures call-value-closure */
                #if 0
            case OBJ_FUNCTION: // [switch]
                {
                    return lox_vm_callclosure(vm, AS_FUNCTION(callee), argc);
                }
                break;
                #endif
            case OBJ_NATIVE:
                {
                    return lox_vm_callnativefunction(vm, callee, argc);
                }
                break;
            default:
                // Non-callable object type.
                break;
        }
    }
    lox_state_raiseerror(vm, "can only call functions and classes.");
    return false;
}

bool lox_vm_invokefromclass(VMState* vm, ObjClass* klass, ObjString* name, int argc)
{
    Value method;
    if(!lox_hashtable_get(vm, &klass->methods, name, &method))
    {
        lox_state_raiseerror(vm, "undefined property '%s'.", name->chars);
        return false;
    }
    return lox_vm_callclosure(vm, AS_CLOSURE(method), argc);
}

bool lox_vmdo_invoke(VMState* vm, ObjString* name, int argc)
{
    Value value;
    Value receiver;
    ObjInstance* instance;
    receiver = lox_vm_stackpeek(vm, argc);
    if(!IS_INSTANCE(receiver))
    {
        lox_state_raiseerror(vm, "only instances have methods.");
        return false;
    }
    instance = AS_INSTANCE(receiver);
    if(lox_hashtable_get(vm, &instance->fields, name, &value))
    {
        vm->stack[vm->stacktop + (-argc - 1)] = value;
        return lox_vm_callvalue(vm, value, argc);
    }
    return lox_vm_invokefromclass(vm, instance->klass, name, argc);
}

bool lox_vmdo_bindmethod(VMState* vm, ObjClass* klass, ObjString* name)
{
    Value method;
    ObjBoundMethod* bound;
    if(!lox_hashtable_get(vm, &klass->methods, name, &method))
    {
        lox_state_raiseerror(vm, "undefined property '%s'.", name->chars);
        return false;
    }
    bound = lox_object_makeboundmethod(vm, lox_vm_stackpeek(vm, 0), AS_CLOSURE(method));
    lox_vm_stackpop(vm);
    lox_vm_stackpush(vm, OBJ_VAL(bound));
    return true;
}

ObjUpvalue* lox_vm_captureupval(VMState* vm, Value* local)
{
    ObjUpvalue* upvalue;
    ObjUpvalue* prevupvalue;
    ObjUpvalue* createdupvalue;
    prevupvalue = NULL;
    upvalue = vm->openupvalues;
    while(upvalue != NULL && upvalue->location > local)
    {
        prevupvalue = upvalue;
        upvalue = upvalue->next;
    }
    if(upvalue != NULL && upvalue->location == local)
    {
        return upvalue;
    }
    createdupvalue = lox_object_makeupvalue(vm, local);
    createdupvalue->next = upvalue;
    if(prevupvalue == NULL)
    {
        vm->openupvalues = createdupvalue;
    }
    else
    {
        prevupvalue->next = createdupvalue;
    }
    return createdupvalue;
}

void lox_vmdo_closeupvals(VMState* vm, Value* last)
{
    ObjUpvalue* upvalue;
    while(vm->openupvalues != NULL && vm->openupvalues->location >= last)
    {
        upvalue = vm->openupvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->openupvalues = upvalue->next;
    }
}

void lox_vmdo_defmethod(VMState* vm, ObjString* name)
{
    Value method;
    ObjClass* klass;
    method = lox_vm_stackpeek(vm, 0);
    klass = AS_CLASS(lox_vm_stackpeek(vm, 1));
    lox_hashtable_set(vm, &klass->methods, name, method);
    lox_vm_stackpop(vm);
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

void lox_vmdo_concat(VMState* vm)
{
    int length;
    char* buf;
    ObjString* b;
    ObjString* a;
    ObjString* result;
    /* Strings concatenate < Garbage Collection concatenate-peek */
    //b = AS_STRING(lox_vm_stackpop(vm));
    //a = AS_STRING(lox_vm_stackpop(vm));

    b = AS_STRING(lox_vm_stackpeek(vm, 0));
    a = AS_STRING(lox_vm_stackpeek(vm, 1));
    length = a->length + b->length;
    buf = ALLOCATE(vm, char, length + 1);
    memcpy(buf, a->chars, a->length);
    memcpy(buf + a->length, b->chars, b->length);
    buf[length] = '\0';
    result = lox_string_take(vm, buf, length);
    lox_vm_stackpop(vm);
    lox_vm_stackpop(vm);
    lox_vm_stackpush(vm, OBJ_VAL(result));
}


/* A Virtual Machine run < Calls and Functions run */
static inline int32_t lox_vmbits_readbyte(VMState* vm)
{
    return (*(vm)->currframe->ip++);
}

/* Jumping Back and Forth read-short < Calls and Functions run */
static inline uint16_t lox_vmbits_readshort(VMState* vm)
{
    /*
    m_instptr += 2;
    return (uint16_t)((m_instptr[-2] << 8) | m_instptr[-1]);
    */

    (vm)->currframe->ip += 2;
    return (uint16_t)((vm->currframe->ip[-2] << 8) | vm->currframe->ip[-1]);
}

/* Calls and Functions run < Closures read-constant */
static inline Value lox_vmbits_readconst(VMState* vm)
{
    return ((vm)->currframe->closure->innerfn->chunk.constants.values[lox_vmbits_readbyte(vm)]);
}

static inline ObjString* lox_vmbits_readstring(VMState* vm)
{
    return AS_STRING(lox_vmbits_readconst(vm));
}

/* A Virtual Machine binary-op < Types of Values binary-op */
bool BINARY_OP(VMState* vm, bool isbool, int32_t op)
{
    double b;
    double a;
    double dw;
    Value res;
    Value peeka;
    Value peekb;
    peeka = lox_vm_stackpeek(vm, 0);
    peekb = lox_vm_stackpeek(vm, 1);
    if(!IS_NUMBER(peeka))
    {
        lox_state_raiseerror(vm, "operand a (at 0) must be a number");
        return false;
    }
    if(!IS_NUMBER(peekb))
    {
        lox_state_raiseerror(vm, "operand b (at 1) must be a number");
        return false;
    }
    b = AS_NUMBER(lox_vm_stackpop(vm));
    a = AS_NUMBER(lox_vm_stackpop(vm));
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
                lox_state_raiseerror(vm, "unrecognized instruction for BINARY_OP");
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
    lox_vm_stackpush(vm, res);
    return true;
}

bool lox_vmdo_indexgetstring(VMState* vm, ObjString* os, Value vidx)
{
    char ch;
    long nidx;
    ObjString* nos;
    if(!IS_NUMBER(vidx))
    {
        lox_state_raiseerror(vm, "cannot index strings with non-number type <%s>", lox_writer_valuetypename(vidx));
        return false;
    }
    nidx = AS_NUMBER(vidx);
    if((nidx >= 0) && (nidx < os->length))
    {
        ch = os->chars[nidx];
        nos = lox_string_copy(vm, &ch, 1);
        lox_vm_stackpush(vm, OBJ_VAL(nos));
        return true;
    }
    return false;
}

bool lox_vmdo_indexget(VMState* vm)
{
    Value vidx;
    Value peeked;
    vidx = lox_vm_stackpop(vm);
    peeked = lox_vm_stackpop(vm);
    if(IS_STRING(peeked))
    {
        if(lox_vmdo_indexgetstring(vm, AS_STRING(peeked), vidx))
        {
            return true;
        }
    }
    else
    {
        lox_state_raiseerror(vm, "cannot index object type <%s>", lox_writer_valuetypename(peeked));
    }
    lox_vm_stackpush(vm, NIL_VAL);
    return false;
}

bool lox_vmdo_propertyget(VMState* vm)
{
    Value value;
    Value peeked;
    Value vidx;
    ObjString* name;
    ObjInstance* instance;
    peeked = lox_vm_stackpeek(vm, 0);
    if(!IS_INSTANCE(peeked))
    {
        lox_state_raiseerror(vm, "only instances have properties.");
        return false;
    }
    vidx = lox_vmbits_readconst(vm);
    lox_vm_stackpop(vm);// Instance.
    instance = AS_INSTANCE(peeked);
    name = AS_STRING(vidx);
    if(lox_hashtable_get(vm, &instance->fields, name, &value))
    {
        lox_vm_stackpush(vm, value);
        return true;
    }
    if(!lox_vmdo_bindmethod(vm, instance->klass, name))
    {
        return false;
    }
    return true;
}

bool lox_vmdo_makearray(VMState* vm)
{
    int i;
    int count;
    Value val;
    ObjArray* array;
    count = lox_vmbits_readbyte(vm);
    array = lox_array_make(vm);
    fprintf(stderr, "makearray: count=%d\n", count);
    //lox_vm_stackpush(vm, NIL_VAL);
    vm->stack[vm->stacktop + (-count - 1)] = OBJ_VAL(array);
    for(i = count - 1; i >= 0; i--)
    {
        val = lox_vm_stackpeek(vm, i);
        lox_array_push(array, val);
    }
    #if 0
        lox_vm_stackpopn(vm, count - 0);
    #else
        for(i=0; i<(count-0); i++)
        {
            lox_vm_stackpop(vm);
        }
    #endif
    lox_vm_stackpush(vm, OBJ_VAL(array));
    return true;
}

StatusCode lox_vm_runvm(VMState* vm)
{
    size_t icnt;
    int64_t nowpos;
    int64_t spos;
    int64_t stacktop;
    OpCode instruc;
    Value* pslot;
    (void)icnt;
    (void)pslot;
    vm->currframe = &vm->frames[vm->framecount - 1];
    for(;;)
    {
        instruc = lox_vmbits_readbyte(vm);
        if(vm->conf_shouldprintruntime)
        {
            pslot = &vm->stack[0];
            printf(" at %p (instruc=%d):", pslot, instruc);
            lox_dbg_dumpdisasm(&vm->currframe->closure->innerfn->chunk, (int)(vm->currframe->ip - vm->currframe->closure->innerfn->chunk.code));
            printf(" ... stack: [\n");
            icnt = vm->currframe->frstackindex;
            stacktop = vm->stacktop;
            icnt = 0;
            spos = 0;
            for(pslot = &vm->stack[0]; pslot < &vm->stack[stacktop]; pslot++)
            {
                nowpos = spos;
                spos++;

                printf("  (%d) ", spos);
                lox_writer_printvalue(*pslot);
                printf("\n");

            }

            printf("]\n");
        }
        switch(instruc)
        {
            case OP_PUSHCONST:
                {
                    Value constant = lox_vmbits_readconst(vm);
                    /* A Virtual Machine op-constant < A Virtual Machine push-constant */
                    /*
                    lox_writer_printvalue(constant);
                    printf("\n");
                    */
                    lox_vm_stackpush(vm, constant);
                }
                break;
            case OP_PUSHONE:
                {
                    lox_vm_stackpush(vm, NUMBER_VAL((double)1));
                }
                break;
            case OP_PUSHNIL:
                {
                    lox_vm_stackpush(vm, NIL_VAL);
                }
                break;
            case OP_PUSHTRUE:
                {
                    lox_vm_stackpush(vm, BOOL_VAL(true));
                }
                break;
            case OP_PUSHFALSE:
                {
                    lox_vm_stackpush(vm, BOOL_VAL(false));
                }
                break;
            case OP_MAKEARRAY:
                {
                    if(!lox_vmdo_makearray(vm))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case OP_POP:
                {
                    lox_vm_stackpop(vm);
                }
                break;
            case OP_POPN:
                {
                    int32_t n;
                    n = lox_vmbits_readbyte(vm);
                    lox_vm_stackpopn(vm, n);
                }
                break;
            case OP_DUP:
                {
                    lox_vm_stackpush(vm, lox_vm_stackpeek(vm, 0));
                }
                break;
            case OP_LOCALGET:
                {
                    int32_t islot;
                    Value val;
                    islot = lox_vmbits_readbyte(vm);
                    val = vm->stack[vm->currframe->frstackindex + (islot + 0)];
                    lox_vm_stackpush(vm, val);
                }
                break;
            case OP_LOCALSET:
                {
                    int32_t islot;
                    Value val;
                    islot = lox_vmbits_readbyte(vm);
                    val = lox_vm_stackpeek(vm, 0);
                    vm->stack[vm->currframe->frstackindex + (islot + 0)] = val;
                }
                break;
            case OP_GLOBALGET:
                {
                    Value value;
                    ObjString* name;
                    name = lox_vmbits_readstring(vm);
                    if(!lox_hashtable_get(vm, &vm->globals, name, &value))
                    {
                        lox_state_raiseerror(vm, "undefined variable '%s'.", name->chars);
                        return STATUS_RUNTIMEERROR;
                    }
                    lox_vm_stackpush(vm, value);
                }
                break;
            case OP_GLOBALDEFINE:
                {
                    ObjString* name;
                    name = lox_vmbits_readstring(vm);
                    lox_hashtable_set(vm, &vm->globals, name, lox_vm_stackpeek(vm, 0));
                    lox_vm_stackpop(vm);
                }
                break;            
            case OP_GLOBALSET:
                {
                    ObjString* name;
                    name = lox_vmbits_readstring(vm);
                    if(lox_hashtable_set(vm, &vm->globals, name, lox_vm_stackpeek(vm, 0)))
                    {
                        //lox_hashtable_delete(vm, &vm->globals, name);// [delete]
                        //lox_state_raiseerror(vm, "undefined variable '%s'.", name->chars);
                        //return STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case OP_UPVALGET:
                {
                    int32_t islot = lox_vmbits_readbyte(vm);
                    lox_vm_stackpush(vm, *vm->currframe->closure->upvalues[islot]->location);
                }
                break;
            case OP_UPVALSET:
                {
                    int32_t islot = lox_vmbits_readbyte(vm);
                    *vm->currframe->closure->upvalues[islot]->location = lox_vm_stackpeek(vm, 0);
                }
                break;

            case OP_INDEXGET:
                {
                    if(!lox_vmdo_indexget(vm))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;
            
            
            case OP_PROPERTYGET:
                {
                    if(!lox_vmdo_propertyget(vm))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;

            case OP_PROPERTYSET:
                {
                    Value value;
                    ObjInstance* instance;
                    if(!IS_INSTANCE(lox_vm_stackpeek(vm, 1)))
                    {
                        lox_state_raiseerror(vm, "only instances have fields.");
                        return STATUS_RUNTIMEERROR;
                    }
                    instance = AS_INSTANCE(lox_vm_stackpeek(vm, 1));
                    lox_hashtable_set(vm, &instance->fields, lox_vmbits_readstring(vm), lox_vm_stackpeek(vm, 0));
                    value = lox_vm_stackpop(vm);
                    lox_vm_stackpop(vm);
                    lox_vm_stackpush(vm, value);
                }
                break;
            case OP_INSTGETSUPER:
                {
                    ObjString* name;
                    ObjClass* superclass;
                    name = lox_vmbits_readstring(vm);
                    superclass = AS_CLASS(lox_vm_stackpop(vm));
                    if(!lox_vmdo_bindmethod(vm, superclass, name))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case OP_EQUAL:
                {
                    Value b = lox_vm_stackpop(vm);
                    Value a = lox_vm_stackpop(vm);
                    lox_vm_stackpush(vm, BOOL_VAL(lox_value_equal(a, b)));
                }
                break;
            case OP_PRIMGREATER:
            case OP_PRIMLESS:
                {
                    if(!BINARY_OP(vm, true, instruc))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case OP_PRIMADD:
                {
                    if(IS_STRING(lox_vm_stackpeek(vm, 0)) && IS_STRING(lox_vm_stackpeek(vm, 1)))
                    {
                        lox_vmdo_concat(vm);
                    }
                    else if(IS_NUMBER(lox_vm_stackpeek(vm, 0)) && IS_NUMBER(lox_vm_stackpeek(vm, 1)))
                    {
                        if(!BINARY_OP(vm, false, instruc))
                        {
                            return STATUS_RUNTIMEERROR;
                        }
                    }
                    else
                    {
                        lox_state_raiseerror(vm, "operands must be two numbers or two strings.");
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
                    if(!BINARY_OP(vm, false, instruc))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                }
                break;
            case OP_PRIMNOT:
                {
                    lox_vm_stackpush(vm, BOOL_VAL(lox_value_isfalsey(lox_vm_stackpop(vm))));
                }
                break;
            case OP_PRIMNEGATE:
                {
                    if(!IS_NUMBER(lox_vm_stackpeek(vm, 0)))
                    {
                        lox_state_raiseerror(vm, "operand must be a number.");
                        return STATUS_RUNTIMEERROR;
                    }
                    lox_vm_stackpush(vm, NUMBER_VAL(-AS_NUMBER(lox_vm_stackpop(vm))));
                }
                break;
            case OP_DEBUGPRINT:
                {
                    lox_writer_printvalue(lox_vm_stackpop(vm));
                }
                break;
            case OP_JUMPNOW:
                {
                    uint16_t offset;
                    offset = lox_vmbits_readshort(vm);
                    vm->currframe->ip += offset;
                }
                break;

            case OP_JUMPIFFALSE:
                {
                    uint16_t offset;
                    offset = lox_vmbits_readshort(vm);
                    if(lox_value_isfalsey(lox_vm_stackpeek(vm, 0)))
                    {
                        vm->currframe->ip += offset;
                    }
                }
                break;
            case OP_LOOP:
                {
                    uint16_t offset;
                    offset = lox_vmbits_readshort(vm);
                    vm->currframe->ip -= offset;
                }
                break;
            case OP_CALL:
                {
                    int argc;
                    argc = lox_vmbits_readbyte(vm);
                    if(!lox_vm_callvalue(vm, lox_vm_stackpeek(vm, argc), argc))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                    vm->currframe = &vm->frames[vm->framecount - 1];
                }
                break;

            case OP_INSTTHISINVOKE:
                {
                    int argc;
                    ObjString* method;
                    method = lox_vmbits_readstring(vm);
                    argc = lox_vmbits_readbyte(vm);
                    if(!lox_vmdo_invoke(vm, method, argc))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                    vm->currframe = &vm->frames[vm->framecount - 1];
                }
                break;
            case OP_INSTSUPERINVOKE:
                {
                    int argc;
                    ObjString* method;
                    ObjClass* superclass;
                    method = lox_vmbits_readstring(vm);
                    argc = lox_vmbits_readbyte(vm);
                    superclass = AS_CLASS(lox_vm_stackpop(vm));
                    if(!lox_vm_invokefromclass(vm, superclass, method, argc))
                    {
                        return STATUS_RUNTIMEERROR;
                    }
                    vm->currframe = &vm->frames[vm->framecount - 1];
                }
                break;
            case OP_CLOSURE:
                {
                    int i;
                    int32_t index;
                    int32_t islocal;
                    ObjFunction* fn;
                    ObjClosure* closure;
                    fn = AS_FUNCTION(lox_vmbits_readconst(vm));
                    closure = lox_object_makeclosure(vm, fn);
                    lox_vm_stackpush(vm, OBJ_VAL(closure));
                    for(i = 0; i < closure->upvaluecount; i++)
                    {
                        islocal = lox_vmbits_readbyte(vm);
                        index = lox_vmbits_readbyte(vm);
                        if(islocal)
                        {
                            closure->upvalues[i] = lox_vm_captureupval(vm, &vm->stack[vm->currframe->frstackindex + index]);
                        }
                        else
                        {
                            closure->upvalues[i] = vm->currframe->closure->upvalues[index];
                        }
                    }
                }
                break;
            case OP_UPVALCLOSE:
                {
                    Value* vargs;
                    vargs = (&vm->stack[0] + vm->stacktop) - 1;
                    lox_vmdo_closeupvals(vm, vargs);
                    lox_vm_stackpop(vm);
                }
                break;
            case OP_RETURN:
                {
                    int64_t usable;
                    Value result;
                    result = lox_vm_stackpop(vm);
                    if(vm->currframe->frstackindex >= 0)
                    {
                        lox_vmdo_closeupvals(vm, &vm->stack[vm->currframe->frstackindex]);
                    }
                    vm->framecount--;
                    if(vm->framecount == 0)
                    {
                        lox_vm_stackpop(vm);
                        fprintf(stderr, "returning due to OP_RETURN\n");
                        return STATUS_OK;
                    }
                    usable = (vm->currframe->frstackindex - 0);
                    vm->stacktop = usable;
                    lox_vm_stackpush(vm, result);
                    vm->currframe = &vm->frames[vm->framecount - 1];

                }
                break;
            case OP_CLASS:
                {
                    lox_vm_stackpush(vm, OBJ_VAL(lox_object_makeclass(vm, lox_vmbits_readstring(vm))));
                }
                break;
            case OP_INHERIT:
                {
                    Value superclass;
                    ObjClass* subclass;
                    superclass = lox_vm_stackpeek(vm, 1);
                    if(!IS_CLASS(superclass))
                    {
                        lox_state_raiseerror(vm, "superclass must be a class.");
                        return STATUS_RUNTIMEERROR;
                    }
                    subclass = AS_CLASS(lox_vm_stackpeek(vm, 0));
                    lox_hashtable_addall(vm, &AS_CLASS(superclass)->methods, &subclass->methods);
                    lox_vm_stackpop(vm);// Subclass.
                }
                break;

            case OP_METHOD:
                {
                    lox_vmdo_defmethod(vm, lox_vmbits_readstring(vm));
                }
                break;
        }
    }
}


StatusCode lox_state_runsource(VMState* vm, const char* source)
{
    ObjFunction* fn;
    ObjClosure* closure;
    fn = lox_prs_compilesource(vm, source);
    if(fn == NULL)
    {
        return STATUS_SYNTAXERROR;
    }
    lox_vm_stackpush(vm, OBJ_VAL(fn));
    closure = lox_object_makeclosure(vm, fn);
    lox_vm_stackpop(vm);
    lox_vm_stackpush(vm, OBJ_VAL(closure));
    lox_vm_callclosure(vm, closure, 0);
    return lox_vm_runvm(vm);
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


void lox_state_runfile(VMState* vm, const char* path)
{
    size_t fsz;
    char* source;
    StatusCode result;
    source = lox_util_readfile(path, &fsz);
    result = lox_state_runsource(vm, source);
    free(source);
    if(result == STATUS_SYNTAXERROR)
    {
        exit(65);
    }
    if(result == STATUS_RUNTIMEERROR)
    {
        exit(70);
    }
}

static void repl(VMState* vm)
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
        lox_state_runsource(vm, line);
    }
}

static Value cfn_clock(VMState* vm, int argc, Value* argv)
{
    (void)vm;
    (void)argc;
    (void)argv;
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value cfn_chr(VMState* vm, int argc, Value* argv)
{
    double dw;
    char ch;
    ObjString* os;
    (void)vm;
    (void)argc;
    (void)argv;
    dw = argv[0].as.number;
    ch = (char)dw;
    os = lox_string_copy(vm, &ch, 1);
    return OBJ_VAL(os);
}

static Value cfn_print(VMState* vm, int argc, Value* argv)
{
    int i;
    (void)vm;
    for(i=0; i<argc; i++)
    {
        lox_writer_printvalue(argv[i]);
    }
    return NUMBER_VAL(0);
}

static Value cfn_println(VMState* vm, int argc, Value* argv)
{
    cfn_print(vm, argc, argv);
    printf("\n");
    return NUMBER_VAL(0);
}



int main(int argc, char** argv)
{
    int i;
    int nargc;
    char oc;
    char noc;
    char** nargv;
    char* codeline;
    const char* filename;
    VMState vm;
    nargv = argv;
    nargc = argc;
    codeline = NULL;
    lox_state_init(&vm);
    lox_state_defnative(&vm, "clock", cfn_clock);
    lox_state_defnative(&vm, "chr", cfn_chr);
    lox_state_defnative(&vm, "print", cfn_print);
    lox_state_defnative(&vm, "println", cfn_println);
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
                vm.conf_shouldprintruntime = true;
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
        lox_state_runsource(&vm, codeline);
    }
    else
    {
        if(argc > 1)
        {
            filename = argv[1];
            fprintf(stderr, "filename=%s\n", filename);
            lox_state_runfile(&vm, filename);
        }
        else
        {
            repl(&vm);
        }
    }
    lox_state_free(&vm);
    return 0;
}
