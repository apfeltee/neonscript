
#include "neon.h"

NNObject* nn_object_allocobject(NNState* state, size_t size, NNObjType type, bool retain)
{
    NNObject* object;
    object = (NNObject*)nn_gcmem_allocate(state, size, 1, retain);
    object->type = type;
    object->mark = !state->markvalue;
    object->stale = false;
    object->pstate = state;
    object->next = state->vmstate.linkedobjects;
    state->vmstate.linkedobjects = object;
    #if defined(DEBUG_GC) && DEBUG_GC
    nn_iostream_printf(state->debugwriter, "%p allocate %ld for %d\n", (void*)object, size, type);
    #endif
    return object;
}

NNObjUpvalue* nn_object_makeupvalue(NNState* state, NNValue* slot, int stackpos)
{
    NNObjUpvalue* upvalue;
    upvalue = (NNObjUpvalue*)nn_object_allocobject(state, sizeof(NNObjUpvalue), NEON_OBJTYPE_UPVALUE, false);
    upvalue->closed = nn_value_makenull();
    upvalue->location = *slot;
    upvalue->next = NULL;
    upvalue->stackpos = stackpos;
    return upvalue;
}

NNObjUserdata* nn_object_makeuserdata(NNState* state, void* pointer, const char* name)
{
    NNObjUserdata* ptr;
    ptr = (NNObjUserdata*)nn_object_allocobject(state, sizeof(NNObjUserdata), NEON_OBJTYPE_USERDATA, false);
    ptr->pointer = pointer;
    ptr->name = nn_util_strdup(name);
    ptr->ondestroyfn = NULL;
    return ptr;
}

NNObjModule* nn_module_make(NNState* state, const char* name, const char* file, bool imported, bool retain)
{
    NNObjModule* module;
    module = (NNObjModule*)nn_object_allocobject(state, sizeof(NNObjModule), NEON_OBJTYPE_MODULE, retain);
    nn_valtable_init(state, &module->deftable);
    module->name = nn_string_copycstr(state, name);
    module->physicalpath = nn_string_copycstr(state, file);
    module->fnunloaderptr = NULL;
    module->fnpreloaderptr = NULL;
    module->handle = NULL;
    module->imported = imported;
    return module;
}

NNObjSwitch* nn_object_makeswitch(NNState* state)
{
    NNObjSwitch* sw;
    sw = (NNObjSwitch*)nn_object_allocobject(state, sizeof(NNObjSwitch), NEON_OBJTYPE_SWITCH, false);
    nn_valtable_init(state, &sw->table);
    sw->defaultjump = -1;
    sw->exitjump = -1;
    return sw;
}

NNObjRange* nn_object_makerange(NNState* state, int lower, int upper)
{
    NNObjRange* range;
    range = (NNObjRange*)nn_object_allocobject(state, sizeof(NNObjRange), NEON_OBJTYPE_RANGE, false);
    range->lower = lower;
    range->upper = upper;
    if(upper > lower)
    {
        range->range = upper - lower;
    }
    else
    {
        range->range = lower - upper;
    }
    return range;
}


