
#include "neon.h"

NNObjFunction* nn_object_makefuncgeneric(NNState* state, NNObjString* name, NNObjType fntype, NNValue thisval)
{
    NNObjFunction* ofn;
    ofn = (NNObjFunction*)nn_object_allocobject(state, sizeof(NNObjFunction), fntype, false);
    ofn->clsthisval = thisval;
    ofn->name = name;
    return ofn;

}

NNObjFunction* nn_object_makefuncbound(NNState* state, NNValue receiver, NNObjFunction* method)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncgeneric(state, method->name, NEON_OBJTYPE_FUNCBOUND, nn_value_makenull());
    ofn->fnmethod.receiver = receiver;
    ofn->fnmethod.method = method;
    return ofn;
}

NNObjFunction* nn_object_makefuncscript(NNState* state, NNObjModule* module, NNFuncContextType type)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncgeneric(state, nn_string_intern(state, "<script>"), NEON_OBJTYPE_FUNCSCRIPT, nn_value_makenull());
    ofn->fnscriptfunc.arity = 0;
    ofn->upvalcount = 0;
    ofn->fnscriptfunc.isvariadic = false;
    ofn->name = NULL;
    ofn->contexttype = type;
    ofn->fnscriptfunc.module = module;
    #if 0
        nn_valarray_init(state, &ofn->funcargdefaults);
    #endif
    nn_blob_init(state, &ofn->fnscriptfunc.blob);
    return ofn;
}

void nn_funcscript_destroy(NNObjFunction* ofn)
{
    nn_blob_destroy(&ofn->fnscriptfunc.blob);
}

NNObjFunction* nn_object_makefuncnative(NNState* state, NNNativeFN natfn, const char* name, void* uptr)
{
    NNObjFunction* ofn;
    ofn = nn_object_makefuncgeneric(state, nn_string_copycstr(state, name), NEON_OBJTYPE_FUNCNATIVE, nn_value_makenull());
    ofn->fnnativefunc.natfunc = natfn;
    ofn->contexttype = NEON_FNCONTEXTTYPE_FUNCTION;
    ofn->fnnativefunc.userptr = uptr;
    return ofn;
}

NNObjFunction* nn_object_makefuncclosure(NNState* state, NNObjFunction* innerfn, NNValue thisval)
{
    int i;
    NNObjUpvalue** upvals;
    NNObjFunction* ofn;
    upvals = NULL;
    if(innerfn->upvalcount > 0)
    {
        upvals = (NNObjUpvalue**)nn_gcmem_allocate(state, sizeof(NNObjUpvalue*), innerfn->upvalcount + 1, false);
        for(i = 0; i < innerfn->upvalcount; i++)
        {
            upvals[i] = NULL;
        }
    }
    ofn = nn_object_makefuncgeneric(state, innerfn->name, NEON_OBJTYPE_FUNCCLOSURE, thisval);
    ofn->fnclosure.scriptfunc = innerfn;
    ofn->fnclosure.upvalues = upvals;
    ofn->upvalcount = innerfn->upvalcount;
    return ofn;
}

