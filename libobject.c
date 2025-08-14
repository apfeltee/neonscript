
#include "neon.h"

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


void nn_state_installobjectobject(NNState* state)
{
    static NNConstClassMethodItem objectmethods[] =
    {
        {"dump", nn_objfnobject_dumpself},
        {"isa", nn_objfnobject_isa},
        {"toString", nn_objfnobject_tostring},
        {"isArray", nn_objfnobject_isarray},
        {"isString", nn_objfnobject_isstring},
        {"isCallable", nn_objfnobject_iscallable},
        {"isBool", nn_objfnobject_isbool},
        {"isNumber", nn_objfnobject_isnumber},
        {"isInt", nn_objfnobject_isint},
        {"isDict", nn_objfnobject_isdict},
        {"isObject", nn_objfnobject_isobject},
        {"isFunction", nn_objfnobject_isfunction},
        {"isIterable", nn_objfnobject_isiterable},
        {"isClass", nn_objfnobject_isclass},
        {"isFile", nn_objfnobject_isfile},
        {"isInstance", nn_objfnobject_isinstance},
        {NULL, NULL},
    };
    nn_class_defstaticnativemethod(state->classprimobject, nn_string_copycstr(state, "typename"), nn_objfnobject_typename);
    nn_class_defstaticcallablefield(state->classprimobject, nn_string_copycstr(state, "prototype"), nn_objfnobject_getselfclass);
    nn_state_installmethods(state, state->classprimobject, objectmethods);
    {
        nn_class_defstaticcallablefield(state->classprimclass, nn_string_copycstr(state, "name"), nn_objfnclass_getselfname);
    }
}
