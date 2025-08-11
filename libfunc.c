
#include "neon.h"

NNObjFunction* nn_object_makefuncbound(NNState* state, NNValue receiver, NNObjFunction* method)
{
    NNObjFunction* bound;
    bound = (NNObjFunction*)nn_object_allocobject(state, sizeof(NNObjFunction), NEON_OBJTYPE_FUNCBOUND, false);
    bound->fnmethod.receiver = receiver;
    bound->fnmethod.method = method;
    return bound;
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

