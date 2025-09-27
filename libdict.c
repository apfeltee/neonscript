
#include "neon.h"

NNObjDict* nn_object_makedict(NNState* state)
{
    NNObjDict* dict;
    dict = (NNObjDict*)nn_object_allocobject(state, sizeof(NNObjDict), NEON_OBJTYPE_DICT, false);
    nn_valarray_init(state, &dict->names);
    nn_valtable_init(state, &dict->htab);
    return dict;
}


bool nn_dict_setentry(NNObjDict* dict, NNValue key, NNValue value)
{
    NNValue tempvalue;
    if(!nn_valtable_get(&dict->htab, key, &tempvalue))
    {
        /* add key if it doesn't exist. */
        nn_valarray_push(&dict->names, key);
    }
    return nn_valtable_set(&dict->htab, key, value);
}

void nn_dict_addentry(NNObjDict* dict, NNValue key, NNValue value)
{
    nn_dict_setentry(dict, key, value);
}

void nn_dict_addentrycstr(NNObjDict* dict, const char* ckey, NNValue value)
{
    NNObjString* os;
    NNState* state;
    state = ((NNObject*)dict)->pstate;
    os = nn_string_copycstr(state, ckey);
    nn_dict_addentry(dict, nn_value_fromobject(os), value);
}

NNProperty* nn_dict_getentry(NNObjDict* dict, NNValue key)
{
    return nn_valtable_getfield(&dict->htab, key);
}

NNObjDict* nn_dict_copy(NNObjDict* dict)
{

    size_t i;
    size_t dsz;
    NNValue key;
    NNProperty* field;
    NNObjDict *ndict;
    NNState* state;
    state = ((NNObject*)dict)->pstate;    
    ndict = nn_object_makedict(state);
    /*
    // @TODO: Figure out how to handle dictionary values correctly
    // remember that copying keys is redundant and unnecessary
    */
    dsz = nn_valarray_count(&dict->names);
    for(i = 0; i < dsz; i++)
    {
        key = nn_valarray_get(&dict->names, i);
        field = nn_valtable_getfield(&dict->htab, nn_valarray_get(&dict->names, i));
        nn_valarray_push(&ndict->names, key);
        nn_valtable_setwithtype(&ndict->htab, key, field->value, field->type, nn_value_isstring(key));
        
    }
    return ndict;
}

NNValue nn_objfndict_length(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "length", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_valarray_count(&nn_value_asdict(thisval)->names));
}

NNValue nn_objfndict_add(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue tempvalue;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "add", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    dict = nn_value_asdict(thisval);
    if(nn_valtable_get(&dict->htab, argv[0], &tempvalue))
    {
        NEON_RETURNERROR("duplicate key %s at add()", nn_string_getdata(nn_value_tostring(state, argv[0])));
    }
    nn_dict_addentry(dict, argv[0], argv[1]);
    return nn_value_makenull();
}

NNValue nn_objfndict_set(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue value;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "set", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    dict = nn_value_asdict(thisval);
    if(!nn_valtable_get(&dict->htab, argv[0], &value))
    {
        nn_dict_addentry(dict, argv[0], argv[1]);
    }
    else
    {
        nn_dict_setentry(dict, argv[0], argv[1]);
    }
    return nn_value_makenull();
}

NNValue nn_objfndict_clear(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "clear", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    nn_valarray_destroy(&dict->names, false);
    nn_valtable_destroy(&dict->htab);
    return nn_value_makenull();
}

NNValue nn_objfndict_clone(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjDict* newdict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "clone", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    newdict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict(state));
    if(!nn_valtable_addall(&dict->htab, &newdict->htab, true))
    {
        nn_except_throwclass(state, state->exceptions.argumenterror, "failed to copy table");
        return nn_value_makenull();
    }
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        nn_valarray_push(&newdict->names, nn_valarray_get(&dict->names, i));
    }
    return nn_value_fromobject(newdict);
}

NNValue nn_objfndict_compact(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjDict* newdict;
    NNValue tmpvalue;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "compact", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    newdict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict(state));
    tmpvalue = nn_value_makenull();
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &tmpvalue);
        if(!nn_value_compare(state, tmpvalue, nn_value_makenull()))
        {
            nn_dict_addentry(newdict, nn_valarray_get(&dict->names, i), tmpvalue);
        }
    }
    return nn_value_fromobject(newdict);
}

NNValue nn_objfndict_contains(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue value;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "contains", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(thisval);
    return nn_value_makebool(nn_valtable_get(&dict->htab, argv[0], &value));
}

NNValue nn_objfndict_extend(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNValue tmp;
    NNObjDict* dict;
    NNObjDict* dictcpy;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "extend", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isdict);
    dict = nn_value_asdict(thisval);
    dictcpy = nn_value_asdict(argv[0]);
    for(i = 0; i < nn_valarray_count(&dictcpy->names); i++)
    {
        if(!nn_valtable_get(&dict->htab, nn_valarray_get(&dictcpy->names, i), &tmp))
        {
            nn_valarray_push(&dict->names, nn_valarray_get(&dictcpy->names, i));
        }
    }
    nn_valtable_addall(&dictcpy->htab, &dict->htab, true);
    return nn_value_makenull();
}

NNValue nn_objfndict_get(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjDict* dict;
    NNProperty* field;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "get", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    dict = nn_value_asdict(thisval);
    field = nn_dict_getentry(dict, argv[0]);
    if(field == NULL)
    {
        if(argc == 1)
        {
            return nn_value_makenull();
        }
        else
        {
            return argv[1];
        }
    }
    return field->value;
}

NNValue nn_objfndict_keys(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "keys", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        nn_array_push(list, nn_valarray_get(&dict->names, i));
    }
    return nn_value_fromobject(list);
}

NNValue nn_objfndict_values(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjArray* list;
    NNProperty* field;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "values", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        field = nn_dict_getentry(dict, nn_valarray_get(&dict->names, i));
        nn_array_push(list, field->value);
    }
    return nn_value_fromobject(list);
}

NNValue nn_objfndict_remove(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    int index;
    NNValue value;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "remove", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(thisval);
    if(nn_valtable_get(&dict->htab, argv[0], &value))
    {
        nn_valtable_delete(&dict->htab, argv[0]);
        index = -1;
        for(i = 0; i < nn_valarray_count(&dict->names); i++)
        {
            if(nn_value_compare(state, nn_valarray_get(&dict->names, i), argv[0]))
            {
                index = i;
                break;
            }
        }
        for(i = index; i < nn_valarray_count(&dict->names); i++)
        {
            nn_valarray_set(&dict->names, i, nn_valarray_get(&dict->names, i + 1));
        }
        nn_valarray_decreaseby(&dict->names, 1);
        return value;
    }
    return nn_value_makenull();
}

NNValue nn_objfndict_isempty(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isempty", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makebool(nn_valarray_count(&nn_value_asdict(thisval)->names) == 0);
}

NNValue nn_objfndict_findkey(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "findkey", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    return nn_valtable_findkey(&nn_value_asdict(thisval)->htab, argv[0]);
}

NNValue nn_objfndict_tolist(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjArray* list;
    NNObjDict* dict;
    NNObjArray* namelist;
    NNObjArray* valuelist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "tolist", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = nn_value_asdict(thisval);
    namelist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    valuelist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        nn_array_push(namelist, nn_valarray_get(&dict->names, i));
        NNValue value;
        if(nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &value))
        {
            nn_array_push(valuelist, value);
        }
        else
        {
            /* theoretically impossible */
            nn_array_push(valuelist, nn_value_makenull());
        }
    }
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    nn_array_push(list, nn_value_fromobject(namelist));
    nn_array_push(list, nn_value_fromobject(valuelist));
    return nn_value_fromobject(list);
}

NNValue nn_objfndict_iter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue result;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "iter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(thisval);
    if(nn_valtable_get(&dict->htab, argv[0], &result))
    {
        return result;
    }
    return nn_value_makenull();
}

NNValue nn_objfndict_itern(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "itern", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    dict = nn_value_asdict(thisval);
    if(nn_value_isnull(argv[0]))
    {
        if(nn_valarray_count(&dict->names) == 0)
        {
            return nn_value_makebool(false);
        }
        return nn_valarray_get(&dict->names, 0);
    }
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        if(nn_value_compare(state, argv[0], nn_valarray_get(&dict->names, i)) && (i + 1) < nn_valarray_count(&dict->names))
        {
            return nn_valarray_get(&dict->names, i + 1);
        }
    }
    return nn_value_makenull();
}

NNValue nn_objfndict_each(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    int arity;
    NNValue value;
    NNValue callable;
    NNValue unused;
    NNObjDict* dict;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "each", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    value = nn_value_makenull();
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        passi = 0;
        if(arity > 0)
        {
            nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &value);
            passi++;
            nestargs[0] = value;
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_valarray_get(&dict->names, i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &unused, false);
    }
    return nn_value_makenull();
}

NNValue nn_objfndict_filter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    int arity;
    NNValue value;
    NNValue callable;
    NNValue result;
    NNObjDict* dict;
    NNObjDict* resultdict;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "filter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    resultdict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict(state));
    value = nn_value_makenull();
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        passi = 0;
        nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &value);
        if(arity > 0)
        {
            passi++;
            nestargs[0] = value;
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_valarray_get(&dict->names, i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result, false);
        if(!nn_value_isfalse(result))
        {
            nn_dict_addentry(resultdict, nn_valarray_get(&dict->names, i), value);
        }
    }
    /* pop the call list */
    return nn_value_fromobject(resultdict);
}

NNValue nn_objfndict_some(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    int arity;
    NNValue result;
    NNValue value;
    NNValue callable;
    NNObjDict* dict;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "some", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    value = nn_value_makenull();
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        passi = 0;
        if(arity > 0)
        {
            nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &value);
            passi++;
            nestargs[0] = value;
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_valarray_get(&dict->names, i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result, false);
        if(!nn_value_isfalse(result))
        {
            /* pop the call list */
            return nn_value_makebool(true);
        }
    }
    /* pop the call list */
    return nn_value_makebool(false);
}

NNValue nn_objfndict_every(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    int arity;
    NNValue value;
    NNValue callable;  
    NNValue result;
    NNObjDict* dict;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "every", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    value = nn_value_makenull();
    for(i = 0; i < nn_valarray_count(&dict->names); i++)
    {
        passi = 0;
        if(arity > 0)
        {
            nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &value);
            passi++;
            nestargs[0] = value;
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_valarray_get(&dict->names, i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result, false);
        if(nn_value_isfalse(result))
        {
            /* pop the call list */
            return nn_value_makebool(false);
        }
    }
    return nn_value_makebool(true);
}

NNValue nn_objfndict_reduce(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    int arity;
    int startindex;
    NNValue value;
    NNValue callable;
    NNValue accumulator;
    NNObjDict* dict;
    NNArgCheck check;
    NNValue nestargs[5];
    nn_argcheck_init(state, &check, "reduce", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    dict = nn_value_asdict(thisval);
    callable = argv[0];
    startindex = 0;
    accumulator = nn_value_makenull();
    if(argc == 2)
    {
        accumulator = argv[1];
    }
    if(nn_value_isnull(accumulator) && nn_valarray_count(&dict->names) > 0)
    {
        nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, 0), &accumulator);
        startindex = 1;
    }
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 4);
    value = nn_value_makenull();
    for(i = startindex; i < nn_valarray_count(&dict->names); i++)
    {
        passi = 0;
        /* only call map for non-empty values in a list. */
        if(!nn_value_isnull(nn_valarray_get(&dict->names, i)) && !nn_value_isnull(nn_valarray_get(&dict->names, i)))
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = accumulator;
                if(arity > 1)
                {
                    nn_valtable_get(&dict->htab, nn_valarray_get(&dict->names, i), &value);
                    passi++;
                    nestargs[1] = value;
                    if(arity > 2)
                    {
                        passi++;
                        nestargs[2] = nn_valarray_get(&dict->names, i);
                        if(arity > 4)
                        {
                            passi++;
                            nestargs[3] = thisval;
                        }
                    }
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &accumulator, false);
        }
    }
    return accumulator;
}

void nn_state_installobjectdict(NNState* state)
{
    static NNConstClassMethodItem dictmethods[] =
    {
        {"keys", nn_objfndict_keys},
        {"size", nn_objfndict_length},
        {"add", nn_objfndict_add},
        {"set", nn_objfndict_set},
        {"clear", nn_objfndict_clear},
        {"clone", nn_objfndict_clone},
        {"compact", nn_objfndict_compact},
        {"contains", nn_objfndict_contains},
        {"extend", nn_objfndict_extend},
        {"get", nn_objfndict_get},
        {"values", nn_objfndict_values},
        {"remove", nn_objfndict_remove},
        {"isEmpty", nn_objfndict_isempty},
        {"findKey", nn_objfndict_findkey},
        {"toList", nn_objfndict_tolist},
        {"each", nn_objfndict_each},
        {"filter", nn_objfndict_filter},
        {"some", nn_objfndict_some},
        {"every", nn_objfndict_every},
        {"reduce", nn_objfndict_reduce},
        {"@iter", nn_objfndict_iter},
        {"@itern", nn_objfndict_itern},
        {NULL, NULL},
    };
    #if 0
    nn_class_defnativeconstructor(state->classprimdict, nn_objfndict_constructor);
    nn_class_defstaticnativemethod(state->classprimdict, nn_string_copycstr(state, "keys"), nn_objfndict_keys);
    #endif
    nn_state_installmethods(state, state->classprimdict, dictmethods);
}

