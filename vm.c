
#include <assert.h>
#include "neon.h"

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

NEON_INLINE void nn_vm_resizeinfo(NNState* state, const char* context, NNObjFunction* closure, size_t needed)
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

NEON_INLINE bool nn_vm_checkmayberesize(NNState* state)
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


void nn_state_resetvmstate(NNState* state)
{
    state->vmstate.framecount = 0;
    state->vmstate.stackidx = 0;
    state->vmstate.openupvalues = NULL;
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
            return nn_except_throw(state, "function '%s' expected at least %d arguments but got %d", closure->name->sbuf.data, closure->fnclosure.scriptfunc->fnscriptfunc.arity - 1, argcount);
        }
        else
        {
            return nn_except_throw(state, "function '%s' expected %d arguments but got %d", closure->name->sbuf.data, closure->fnclosure.scriptfunc->fnscriptfunc.arity, argcount);
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
            spos = (state->vmstate.stackidx + (-argcount - 1));
            #if 0
                state->vmstate.stackvalues[spos] = closure->clsthisval;
            #else
                state->vmstate.stackvalues[spos] = thisval;
            #endif
        #endif
    }
    frame = &state->vmstate.framevalues[state->vmstate.framecount++];
    frame->closure = closure;
    frame->inscode = closure->fnclosure.scriptfunc->fnscriptfunc.blob.instrucs;
    frame->stackslotpos = state->vmstate.stackidx + (-argcount - 1);
    return true;
}

NEON_INLINE bool nn_vm_callnative(NNState* state, NNObjFunction* native, NNValue thisval, int argcount)
{
    int64_t spos;
    NNValue r;
    NNValue* vargs;
    NEON_APIDEBUG(state, "thisval.type=%s, argcount=%d", nn_value_typename(thisval, true), argcount);
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
                    spos = (state->vmstate.stackidx + (-argcount - 1));
                    state->vmstate.stackvalues[spos] = thisval;
                    return nn_vm_callclosure(state, bound->fnmethod.method, thisval, argcount, fromoper);
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
                        return nn_vm_callvaluewithobject(state, klass->constructor, thisval, argcount, false);
                    }
                    else if(klass->superclass != NULL && !nn_value_isnull(klass->superclass->constructor))
                    {
                        return nn_vm_callvaluewithobject(state, klass->superclass->constructor, thisval, argcount, false);
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
                        return nn_vm_callvalue(state, field->value, thisval, argcount, false);
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
                    actualthisval = bound->fnmethod.receiver;
                    if(!nn_value_isnull(thisval))
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
                    instance = nn_object_makeinstance(state, klass);
                    actualthisval = nn_value_fromobject(instance);
                    if(!nn_value_isnull(thisval))
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
    return NULL;
}

/*
* the inlined variants of (push|pop(n)|peek) should be
* used in the main VM engine.
*/

NEON_INLINE void nn_vmbits_stackpush(NNState* state, NNValue value)
{
    nn_vm_checkmayberesize(state);
    state->vmstate.stackvalues[state->vmstate.stackidx] = value;
    state->vmstate.stackidx++;
}

void nn_vm_stackpush(NNState* state, NNValue value)
{
    nn_vmbits_stackpush(state, value);
}

NEON_INLINE NNValue nn_vmbits_stackpop(NNState* state)
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

NEON_INLINE NNValue nn_vmbits_stackpopn(NNState* state, int n)
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

NEON_INLINE NNValue nn_vmbits_stackpeek(NNState* state, int distance)
{
    NNValue v;
    v = state->vmstate.stackvalues[state->vmstate.stackidx + (-1 - distance)];
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
        return nn_vm_callvaluewithobject(state, field->value, nn_value_fromobject(klass), argcount, false);
    }
    return nn_except_throw(state, "undefined method '%s' in %s", name->sbuf.data, klass->name->sbuf.data);
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
        field = nn_valtable_getfieldbyostr(&instance->klass->instmethods, name);
        if(field != NULL)
        {
            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount, false);
        }
        field = nn_valtable_getfieldbyostr(&instance->properties, name);
        if(field != NULL)
        {
            spos = (state->vmstate.stackidx + (-argcount - 1));
            state->vmstate.stackvalues[spos] = receiver;
            return nn_vm_callvaluewithobject(state, field->value, receiver, argcount, false);
        }
    }
    else if(nn_value_isclass(receiver))
    {
        field = nn_valtable_getfieldbyostr(&nn_value_asclass(receiver)->instmethods, name);
        if(field != NULL)
        {
            if(nn_value_getmethodtype(field->value) == NEON_FNCONTEXTTYPE_STATIC)
            {
                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount, false);
            }
            return nn_except_throw(state, "cannot call non-static method %s() on non instance", name->sbuf.data);
        }
    }
    return nn_except_throw(state, "cannot call method '%s' on object of type '%s'", name->sbuf.data, nn_value_typename(receiver, false));
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
                    field = nn_valtable_getfieldbyostr(&module->deftable, name);
                    if(field != NULL)
                    {
                        if(nn_util_methodisprivate(name))
                        {
                            return nn_except_throw(state, "cannot call private module method '%s'", name->sbuf.data);
                        }
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount, false);
                    }
                    return nn_except_throw(state, "module '%s' does not have a field named '%s'", module->name->sbuf.data, name->sbuf.data);
                }
                break;
            case NEON_OBJTYPE_CLASS:
                {
                    NEON_APIDEBUG(state, "receiver is a class");
                    klass = nn_value_asclass(receiver);
                    field = nn_class_getstaticproperty(klass, name);
                    if(field != NULL)
                    {
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount, false);
                    }
                    field = nn_class_getstaticmethodfield(klass, name);
                    if(field != NULL)
                    {
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount, false);
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
                                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount, false);
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
                        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount, false);
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
                                return nn_vm_callvaluewithobject(state, field->value, receiver, argcount, false);
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
        return nn_except_throw(state, "non-object %s has no method named '%s'", nn_value_typename(receiver, false), name->sbuf.data);
    }
    field = nn_class_getmethodfield(klass, name);
    if(field != NULL)
    {
        return nn_vm_callvaluewithobject(state, field->value, receiver, argcount, false);
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

/*
* don' try to optimize too much here, since its largely irrelevant how big or small
* the strings are; inevitably it will always be <length-of-string> * number.
* not preallocating also means that the allocator only allocates as much as actually needed.
*/
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
        return nn_except_throw(state, "list range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower, false), nn_value_typename(valupper, false));
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
        return nn_except_throw(state, "string range index expects upper and lower to be numbers, but got '%s', '%s'", nn_value_typename(vallower, false), nn_value_typename(valupper, false));
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

NNProperty* nn_vmutil_checkoverloadrequirements(NNState* state, const char* ccallername, NNValue target, const char* name)
{
    NNProperty* field;
    (void)state;
    if(!nn_value_isinstance(target))
    {
        fprintf(stderr, "%s: not an instance\n", ccallername);
        return NULL;
    }
    field = nn_instance_getmethodcstr(nn_value_asinstance(target), name);
    if(field == NULL)
    {
        fprintf(stderr, "%s: failed to get '%s'\n", ccallername, name);
        return NULL;
    }
    if(!nn_value_iscallable(field->value))
    {
        fprintf(stderr, "%s: field not callable\n", ccallername);
        return NULL;
    }
    return field;
}

NEON_FORCEINLINE bool nn_vmutil_tryoverloadbasic(NNState* state, const char* name, NNValue target, NNValue firstargvval, NNValue setvalue, bool willassign)
{
    size_t nargc;
    NNValue finalval;
    NNProperty* field;
    NNValue scrargv[3];
    nargc = 1;
    field = nn_vmutil_checkoverloadrequirements(state, "tryoverloadgeneric", target, name); 
    scrargv[0] = firstargvval;
    if(willassign)
    {
        scrargv[0] = setvalue;
        scrargv[1] = firstargvval;
        nargc = 2;
    }
    if(nn_nestcall_callfunction(state, field->value, target, scrargv, nargc, &finalval, true))
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

NEON_FORCEINLINE bool nn_vmutil_tryoverloadmath(NNState* state, const char* name, NNValue target, NNValue right, bool willassign)
{
    return nn_vmutil_tryoverloadbasic(state, name, target, right, nn_value_makenull(), willassign);
}

NEON_FORCEINLINE bool nn_vmutil_tryoverloadgeneric(NNState* state, const char* name, NNValue target, bool willassign)
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
        if(nn_vmutil_tryoverloadgeneric(state, "__indexget__", thisval, willassign))
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
        nn_strbuf_appendchar(&os->sbuf, iv);
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
        if(nn_vmutil_tryoverloadgeneric(state, "__indexset__", thisval, true))
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

NEON_INLINE double nn_vmutil_floordiv(double a, double b)
{
    int d;
    d = (int)a / (int)b;
    return d - ((d * b == a) & ((a < 0) ^ (b < 0)));
}

NEON_INLINE double nn_vmutil_modulo(double a, double b)
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
                nn_except_throw(state, "module '%s' does not have a field named '%s'", module->name->sbuf.data, name->sbuf.data);
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
                nn_except_throw(state, "object of type %s does not carry properties", nn_value_typename(peeked, false));
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
                nn_vm_callvaluewithobject(state, field->value, peeked, 0, false);
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
        nn_vmmac_tryraise(state, false, "module '%s' does not have a field named '%s'", module->name->sbuf.data, name->sbuf.data);
        return false;
    }
    nn_vmmac_tryraise(state, false, "'%s' of type %s does not have properties", nn_value_tostring(state, peeked)->sbuf.data,
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
    bool willassign;
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
    willassign = false;
    instruction = (NNOpCode)state->vmstate.currentinstr.code;
    binvalright = nn_vmbits_stackpeek(state, 0);
    binvalleft = nn_vmbits_stackpeek(state, 1);
    if(nn_util_unlikely(nn_value_isinstance(binvalleft)))
    {
        switch(instruction)
        {
            case NEON_OP_PRIMADD:
                {
                    if(nn_vmutil_tryoverloadmath(state, "__add__", binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
            case NEON_OP_PRIMSUBTRACT:
                {
                    if(nn_vmutil_tryoverloadmath(state, "__sub__", binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
            case NEON_OP_PRIMDIVIDE:
                {
                    if(nn_vmutil_tryoverloadmath(state, "__div__", binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
            case NEON_OP_PRIMMULTIPLY:
                {
                    if(nn_vmutil_tryoverloadmath(state, "__mul__", binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
            case NEON_OP_PRIMAND:
                {
                    if(nn_vmutil_tryoverloadmath(state, "__band__", binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
            case NEON_OP_PRIMOR:
                {
                    if(nn_vmutil_tryoverloadmath(state, "__bor__", binvalleft, binvalright, willassign))
                    {
                        return true;
                    }
                }
                break;
            case NEON_OP_PRIMBITXOR:
                {
                    if(nn_vmutil_tryoverloadmath(state, "__bxor__", binvalleft, binvalright, willassign))
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
        (!nn_value_isnumber(binvalright) && !nn_value_isbool(binvalright) && !nn_value_isnull(binvalright)) ||
        (!nn_value_isnumber(binvalleft) && !nn_value_isbool(binvalleft) && !nn_value_isnull(binvalleft))
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
    //nn_valtable_set(&fnc->targetfunc->defaultargvalues, nn_value_makenumber(defvalconst), nn_value_makenumber(paramid));
    size_t ssp;
    size_t putpos;
    uint16_t slot;
    uint64_t pos;
    NNValue vfn;
    NNValue vpos;
    NNValue cval;
    NNValue peeked;
    NNObjFunction* ofn;
    slot = nn_vmbits_readshort(state);
    peeked = nn_vmbits_stackpeek(state, 1);
    cval = nn_vmbits_stackpeek(state, 2);

        ssp = state->vmstate.currentframe->stackslotpos;

        #if 0
            putpos = (state->vmstate.stackidx + (-1 - 1)) ;
        #else
            #if 0
                putpos = state->vmstate.stackidx + (slot - 0);
            #else
                #if 0
                    putpos = state->vmstate.stackidx + (slot);
                #else
                    putpos = (ssp + slot) + 0;
                #endif
            #endif
        #endif
    #if 1
    {
        NNPrinter* pr = state->stderrprinter;
        nn_printer_printf(pr, "funcargoptional: slot=%d putpos=%ld cval=<", slot, putpos);
        nn_printer_printvalue(pr, cval, true, false);
        nn_printer_printf(pr, ">, peeked=<");
        nn_printer_printvalue(pr, peeked, true, false);
        nn_printer_printf(pr, ">\n");
    }
    #endif
    if(nn_value_isnull(cval))
    {
        state->vmstate.stackvalues[putpos] = peeked;
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

NEON_FORCEINLINE bool nn_vmdo_makeclosure(NNState* state, NNValue thisval)
{
    size_t i;
    int index;
    size_t ssp;
    uint8_t islocal;
    NNValue* upvals;
    NNObjFunction* function;
    NNObjFunction* closure;
    function = nn_value_asfunction(nn_vmbits_readconst(state));
    closure = nn_object_makefuncclosure(state, function, thisval);
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
        nn_vmmac_tryraise(state, false, "unsupported operand %s for %s and %s", opname, nn_value_typename(binvalleft, false), nn_value_typename(binvalright, false));
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
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "operator - not defined for object of type %s", nn_value_typename(peeked, false));
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
                    nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "operator ~ not defined for object of type %s", nn_value_typename(peeked, false));
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
                    if(!nn_vmdo_makeclosure(state, nn_vmbits_stackpeek(state, 3)))
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
                        nn_vmbits_stackpush(state, closure->clsthisval);
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
                    NNValue callee;
                    NNValue thisval;
                    thisval = nn_value_makenull();
                    argcount = nn_vmbits_readbyte(state);
                    callee = nn_vmbits_stackpeek(state, argcount);
                    if(nn_value_isfuncclosure(callee))
                    {
                        thisval = nn_value_asfunction(callee)->clsthisval;
                    }
                    if(!nn_vm_callvalue(state, callee, thisval, argcount, false))
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
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "class '%s' does not have a function '%s'", klass->name->sbuf.data, name->sbuf.data);
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
                    result = nn_value_typename(thing, false);
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
    NNObjFunction* ofn;
    (void)state;
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
        status = nn_vm_runvm(state, state->vmstate.framecount - 1, NULL);
        if(status != NEON_STATUS_OK)
        {
            fprintf(stderr, "nestcall: call to runvm failed\n");
            abort();
        }
    }
    rtv = state->vmstate.stackvalues[state->vmstate.stackidx - 1];
    *dest = rtv;
    nn_vm_stackpopn(state, argc + 0);
    state->vmstate.stackidx = pidx;
    return true;
}



