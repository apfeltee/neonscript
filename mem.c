
#include "neon.h"
#include "allocator.h"

#if !defined(NEON_CONF_MEMUSEALLOCATOR) || (NEON_CONF_MEMUSEALLOCATOR == 0)
    #define NEON_CONF_MEMUSEALLOCATOR 1
#endif

#if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
    /* if any global variables need to be declared, declare them here. */
#endif


void nn_memory_init()
{
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        nn_allocator_init();
    #endif
}

void nn_memory_finish()
{
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
    #endif
}

void* nn_memory_setsize(void* p, size_t sz)
{
    size_t* sp;
    sp = (size_t*)p;
    *sp = sz;
    ++sp;
    memset(sp,0,sz);
    return sp;
}

size_t nn_memory_getsize(void * p)
{
    size_t* in = (size_t*)p;
    if (in)
    {
        --in;
        return *in;
    }
    return -1;
}


void* nn_memory_malloc(size_t sz)
{
    void* p;
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        p = (void*)nn_allocuser_malloc(sz);
    #else
        p = (void*)malloc(sz);
    #endif
    return p;
}

void* nn_memory_realloc(void* p, size_t nsz)
{
    void* retp;
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        if(p == NULL)
        {
            return nn_memory_malloc(nsz);
        }
        retp = (void*)nn_allocuser_realloc(p, nsz);
    #else
        retp = (void*)realloc(p, nsz);
    #endif
    return retp;
}

void* nn_memory_calloc(size_t count, size_t typsize)
{
    void* p;
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        p = (void*)nn_allocuser_malloc(count * typsize);
    #else
        p = (void*)calloc(count, typsize);
    #endif
    return p;
}

void nn_memory_free(void* ptr)
{
    #if defined(NEON_CONF_MEMUSEALLOCATOR) && (NEON_CONF_MEMUSEALLOCATOR == 1)
        nn_allocuser_free(ptr);
    #else
        free(ptr);
    #endif
}


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



