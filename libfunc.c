
#include "neon.h"

NNObjFunction* nn_object_makefuncgeneric(NNState* state, NNObjType fntype, NNValue thisval)
{
    NNObjFunction* ofn;
    ofn = (NNObjFunction*)nn_object_allocobject(state, sizeof(NNObjFunction), fntype, false);
    ofn->clsthisval = thisval;
    return ofn;

}

NNObjFunction* nn_object_makefuncbound(NNState* state, NNValue receiver, NNObjFunction* method)
{
    NNObjFunction* bound;
    bound = nn_object_makefuncgeneric(state, NEON_OBJTYPE_FUNCBOUND, nn_value_makenull());
    bound->fnmethod.receiver = receiver;
    bound->fnmethod.method = method;
    return bound;
}

NNObjFunction* nn_object_makefuncscript(NNState* state, NNObjModule* module, NNFuncContextType type)
{
    NNObjFunction* function;
    function = nn_object_makefuncgeneric(state, NEON_OBJTYPE_FUNCSCRIPT, nn_value_makenull());
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
    native = nn_object_makefuncgeneric(state, NEON_OBJTYPE_FUNCNATIVE, nn_value_makenull());
    native->fnnativefunc.natfunc = function;
    native->name = nn_string_copycstr(state, name);
    native->contexttype = NEON_FNCONTEXTTYPE_FUNCTION;
    native->fnnativefunc.userptr = uptr;
    return native;
}

NNObjFunction* nn_object_makefuncclosure(NNState* state, NNObjFunction* function, NNValue thisval)
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
    closure = nn_object_makefuncgeneric(state, NEON_OBJTYPE_FUNCCLOSURE, thisval);
    closure->fnclosure.scriptfunc = function;
    closure->fnclosure.upvalues = upvals;
    closure->upvalcount = function->upvalcount;
    return closure;
}

