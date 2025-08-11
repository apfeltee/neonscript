
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
    nn_printer_printf(state->debugwriter, "%p allocate %ld for %d\n", (void*)object, size, type);
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




NNValue nn_objfnobject_dumpself(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNPrinter pr;
    NNObjString* os;
    v = thisval;
    (void)argv;
    (void)argc;
    nn_printer_makestackstring(state, &pr);
    nn_printer_printvalue(&pr, v, true, false);
    os = nn_printer_takestring(&pr);
    nn_printer_destroy(&pr);
    return nn_value_fromobject(os);
}

NNValue nn_objfnobject_tostring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNPrinter pr;
    NNObjString* os;
    (void)argv;
    (void)argc;
    v = thisval;
    nn_printer_makestackstring(state, &pr);
    nn_printer_printvalue(&pr, v, false, true);
    os = nn_printer_takestring(&pr);
    nn_printer_destroy(&pr);
    return nn_value_fromobject(os);
}

NNValue nn_objfnobject_typename(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNObjString* os;
    (void)thisval;
    (void)argc;
    v = argv[0];
    os = nn_string_copycstr(state, nn_value_typename(v));
    return nn_value_fromobject(os);
}

NNValue nn_objfnobject_getselfclass(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)argv;
    (void)argc;
    return thisval;
}

NNValue nn_objfnobject_isstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    (void)state;
    (void)argv;
    (void)argc;
    v = thisval;
    return nn_value_makebool(nn_value_isstring(v));
}

NNValue nn_objfnobject_isarray(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    (void)state;
    (void)argv;
    (void)argc;
    v = thisval;
    return nn_value_makebool(nn_value_isarray(v));
}

NNValue nn_objfnobject_isa(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNValue otherclval;
    NNObjClass* oclass;
    NNObjClass* selfclass;
    (void)state;
    (void)argc;
    v = thisval;
    otherclval = argv[0];
    if(nn_value_isclass(otherclval))
    {
        oclass = nn_value_asclass(otherclval);
        selfclass = nn_value_getclassfor(state, v);
        if(selfclass != NULL)
        {
            return nn_value_makebool(nn_util_isinstanceof(selfclass, oclass));
        }
    }
    return nn_value_makebool(false);
}

NNValue nn_objfnobject_iscallable(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return (nn_value_makebool(
        nn_value_isclass(selfval) ||
        nn_value_isfuncscript(selfval) ||
        nn_value_isfuncclosure(selfval) ||
        nn_value_isfuncbound(selfval) ||
        nn_value_isfuncnative(selfval)
    ));
}

NNValue nn_objfnobject_isbool(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isbool(selfval));
}

NNValue nn_objfnobject_isnumber(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isnumber(selfval));
}

NNValue nn_objfnobject_isint(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isnumber(selfval) && (((int)nn_value_asnumber(selfval)) == nn_value_asnumber(selfval)));
}

NNValue nn_objfnobject_isdict(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isdict(selfval));
}

NNValue nn_objfnobject_isobject(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isobject(selfval));
}

NNValue nn_objfnobject_isfunction(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(
        nn_value_isfuncscript(selfval) ||
        nn_value_isfuncclosure(selfval) ||
        nn_value_isfuncbound(selfval) ||
        nn_value_isfuncnative(selfval)
    );
}

NNValue nn_objfnobject_isiterable(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    bool isiterable;
    NNValue dummy;
    NNObjClass* klass;
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    isiterable = nn_value_isarray(selfval) || nn_value_isdict(selfval) || nn_value_isstring(selfval);
    if(!isiterable && nn_value_isinstance(selfval))
    {
        klass = nn_value_asinstance(selfval)->klass;
        isiterable = nn_valtable_get(&klass->instmethods, nn_value_fromobject(nn_string_copycstr(state, "@iter")), &dummy)
            && nn_valtable_get(&klass->instmethods, nn_value_fromobject(nn_string_copycstr(state, "@itern")), &dummy);
    }
    return nn_value_makebool(isiterable);
}

NNValue nn_objfnobject_isclass(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isclass(selfval));
}

NNValue nn_objfnobject_isfile(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isfile(selfval));
}

NNValue nn_objfnobject_isinstance(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    return nn_value_makebool(nn_value_isinstance(selfval));
}


NNValue nn_objfnclass_getselfname(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue selfval;
    NNObjClass* klass;
    (void)state;
    (void)argv;
    (void)argc;
    selfval = thisval;
    klass = nn_value_asclass(selfval);
    return nn_value_fromobject(klass->name);
}

