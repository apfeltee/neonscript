
#include "neon.h"



NNValue nn_value_copystrlen(NNState* state, const char* str, size_t len)
{
    return nn_value_fromobject(nn_string_copylen(state, str, len));
}

NNValue nn_value_copystr(NNState* state, const char* str)
{
    return nn_value_copystrlen(state, str, strlen(str));
}

NNObjString* nn_value_tostring(NNState* state, NNValue value)
{
    NNPrinter pr;
    NNObjString* s;
    nn_printer_makestackstring(state, &pr);
    nn_printer_printvalue(&pr, value, false, true);
    s = nn_printer_takestring(&pr);
    nn_printer_destroy(&pr);
    return s;
}

const char* nn_value_objecttypename(NNObject* object, bool detailed)
{
    static char buf[60];
    if(detailed)
    {
        switch(object->type)
        {
            case NEON_OBJTYPE_FUNCSCRIPT:
                return "funcscript";
            case NEON_OBJTYPE_FUNCNATIVE:
                return "funcnative";
            case NEON_OBJTYPE_FUNCCLOSURE:
                return "funcclosure";
            case NEON_OBJTYPE_FUNCBOUND:
                return "funcbound";
            default:
                break;
        }
    }
    switch(object->type)
    {
        case NEON_OBJTYPE_MODULE:
            return "module";
        case NEON_OBJTYPE_RANGE:
            return "range";
        case NEON_OBJTYPE_FILE:
            return "file";
        case NEON_OBJTYPE_DICT:
            return "dictionary";
        case NEON_OBJTYPE_ARRAY:
            return "array";
        case NEON_OBJTYPE_CLASS:
            return "class";
        case NEON_OBJTYPE_FUNCSCRIPT:
        case NEON_OBJTYPE_FUNCNATIVE:
        case NEON_OBJTYPE_FUNCCLOSURE:
        case NEON_OBJTYPE_FUNCBOUND:
            return "function";
        case NEON_OBJTYPE_INSTANCE:
            {
                const char* klassname;
                NNObjInstance* inst;
                inst = ((NNObjInstance*)object);
                klassname = nn_string_getdata(inst->klass->name);
                sprintf(buf, "instance@%s", klassname);
                return buf;
            }
            break;
        case NEON_OBJTYPE_STRING:
            return "string";
        case NEON_OBJTYPE_USERDATA:
            return "userdata";
        case NEON_OBJTYPE_SWITCH:
            return "switch";
        default:
            break;
    }
    return "unknown";
}

const char* nn_value_typename(NNValue value, bool detailed)
{
    if(nn_value_isnull(value))
    {
        return "null";
    }
    else if(nn_value_isbool(value))
    {
        return "boolean";
    }
    else if(nn_value_isnumber(value))
    {
        return "number";
    }
    else if(nn_value_isobject(value))
    {
        return nn_value_objecttypename(nn_value_asobject(value), detailed);
    }
    return "?unknown?";
}

bool nn_value_compobjarray(NNState* state, NNObject* oa, NNObject* ob)
{
    size_t i;
    NNObjArray* arra;
    NNObjArray* arrb;
    arra = (NNObjArray*)oa;
    arrb = (NNObjArray*)ob;
    /* unlike NNObjDict, array order matters */
    if(nn_valarray_count(&arra->varray) != nn_valarray_count(&arrb->varray))
    {
        return false;
    }
    for(i=0; i<(size_t)nn_valarray_count(&arra->varray); i++)
    {
        if(!nn_value_compare(state, nn_valarray_get(&arra->varray, i), nn_valarray_get(&arrb->varray, i)))
        {
            return false;
        }
    }
    return true;
}

bool nn_value_compobjstring(NNState* state, NNObject* oa, NNObject* ob)
{
    size_t alen;
    size_t blen;
    const char* adata;
    const char* bdata;
    NNObjString* stra;
    NNObjString* strb;
    (void)state;
    stra = (NNObjString*)oa;
    strb = (NNObjString*)ob;
    alen = nn_string_getlength(stra);
    blen = nn_string_getlength(strb);
    if(alen != blen)
    {
        return false;
    }
    adata = nn_string_getdata(stra);
    bdata = nn_string_getdata(strb);
    return (memcmp(adata, bdata, alen) == 0);
}

bool nn_value_compobjdict(NNState* state, NNObject* oa, NNObject* ob)
{
    NNObjDict* dicta;
    NNObjDict* dictb;
    NNProperty* fielda;
    NNProperty* fieldb;
    size_t ai;
    size_t lena;
    size_t lenb;
    NNValue keya;
    dicta = (NNObjDict*)oa;
    dictb = (NNObjDict*)ob;
    lena = nn_valarray_count(&dicta->names);
    lenb = nn_valarray_count(&dictb->names);
    if(lena != lenb)
    {
        return false;
    }
    ai = 0;
    while(ai < lena)
    {
        /* first, get the key name off of dicta ... */
        keya = nn_valarray_get(&dicta->names, ai);
        fielda = nn_valtable_getfield(&dicta->htab, nn_valarray_get(&dicta->names, ai));
        if(fielda != NULL)
        {
            /* then look up that key in dictb ... */
            fieldb = nn_dict_getentry(dictb, keya);
            if((fielda != NULL) && (fieldb != NULL))
            {
                /* if it exists, compare their values */
                if(!nn_value_compare(state, fielda->value, fieldb->value))
                {
                    return false;
                }
            }
        }
        ai++;
    }
    return true;
}

bool nn_value_compobject(NNState* state, NNValue a, NNValue b)
{
    NNObjType ta;
    NNObjType tb;
    NNObject* oa;
    NNObject* ob;
    oa = nn_value_asobject(a);
    ob = nn_value_asobject(b);
    ta = oa->type;
    tb = ob->type;
    if(ta == tb)
    {
        /* we might not need to do a deep comparison if its the same object */
        if(oa == ob)
        {
            return true;
        }
        else if(ta == NEON_OBJTYPE_STRING)
        {
            return nn_value_compobjstring(state, oa, ob);
        }
        else if(ta == NEON_OBJTYPE_ARRAY)
        {
            return nn_value_compobjarray(state, oa, ob);
        }
        else if(ta == NEON_OBJTYPE_DICT)
        {
            return nn_value_compobjdict(state, oa, ob);
        }
    }
    return false;
}

bool nn_value_compare_actual(NNState* state, NNValue a, NNValue b)
{
    /*
    if(a.type != b.type)
    {
        return false;
    }
    */
    if(nn_value_isnull(a))
    {
        return true;
    }
    else if(nn_value_isbool(a))
    {
        return nn_value_asbool(a) == nn_value_asbool(b);
    }
    else if(nn_value_isnumber(a))
    {
        return (nn_value_asnumber(a) == nn_value_asnumber(b));
    }
    else
    {
        if(nn_value_isobject(a) && nn_value_isobject(b))
        {
            if(nn_value_asobject(a) == nn_value_asobject(b))
            {
                return true;
            }
            return nn_value_compobject(state, a, b);
        }
    }

    return false;
}


bool nn_value_compare(NNState* state, NNValue a, NNValue b)
{
    bool r;
    r = nn_value_compare_actual(state, a, b);
    return r;
}

/**
 * returns the greater of the two values.
 * this function encapsulates the object hierarchy
 */
NNValue nn_value_findgreater(NNValue a, NNValue b)
{
    size_t alen;
    const char* adata;
    const char* bdata;
    NNObjString* osa;
    NNObjString* osb;    
    if(nn_value_isnull(a))
    {
        return b;
    }
    else if(nn_value_isbool(a))
    {
        if(nn_value_isnull(b) || (nn_value_isbool(b) && nn_value_asbool(b) == false))
        {
            /* only null, false and false are lower than numbers */
            return a;
        }
        else
        {
            return b;
        }
    }
    else if(nn_value_isnumber(a))
    {
        if(nn_value_isnull(b) || nn_value_isbool(b))
        {
            return a;
        }
        else if(nn_value_isnumber(b))
        {
            if(nn_value_asnumber(a) >= nn_value_asnumber(b))
            {
                return a;
            }
            return b;
        }
        else
        {
            /* every other thing is greater than a number */
            return b;
        }
    }
    else if(nn_value_isobject(a))
    {
        if(nn_value_isstring(a) && nn_value_isstring(b))
        {
            osa = nn_value_asstring(a);
            osb = nn_value_asstring(b);
            adata = nn_string_getdata(osa);
            bdata = nn_string_getdata(osb);
            alen = nn_string_getlength(osa);
            if(strncmp(adata, bdata, alen) >= 0)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isfuncscript(a) && nn_value_isfuncscript(b))
        {
            if(nn_value_asfunction(a)->fnscriptfunc.arity >= nn_value_asfunction(b)->fnscriptfunc.arity)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isfuncclosure(a) && nn_value_isfuncclosure(b))
        {
            if(nn_value_asfunction(a)->fnclosure.scriptfunc->fnscriptfunc.arity >= nn_value_asfunction(b)->fnclosure.scriptfunc->fnscriptfunc.arity)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isrange(a) && nn_value_isrange(b))
        {
            if(nn_value_asrange(a)->lower >= nn_value_asrange(b)->lower)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isclass(a) && nn_value_isclass(b))
        {
            if(nn_valtable_count(&nn_value_asclass(a)->instmethods) >= nn_valtable_count(&nn_value_asclass(b)->instmethods))
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isarray(a) && nn_value_isarray(b))
        {
            if(nn_valarray_count(&nn_value_asarray(a)->varray) >= nn_valarray_count(&nn_value_asarray(b)->varray))
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isdict(a) && nn_value_isdict(b))
        {
            if(nn_valarray_count(&nn_value_asdict(a)->names) >= nn_valarray_count(&nn_value_asdict(b)->names))
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isfile(a) && nn_value_isfile(b))
        {
            if(strcmp(nn_string_getdata(nn_value_asfile(a)->path), nn_string_getdata(nn_value_asfile(b)->path)) >= 0)
            {
                return a;
            }
            return b;
        }
        else if(nn_value_isobject(b))
        {
            if(nn_value_asobject(a)->type >= nn_value_asobject(b)->type)
            {
                return a;
            }
            return b;
        }
        else
        {
            return a;
        }
    }
    return a;
}

/**
 * sorts values in an array using the bubble-sort algorithm
 */
void nn_value_sortvalues(NNState* state, NNValue* values, int count)
{
    int i;
    int j;
    NNValue temp;
    for(i = 0; i < count; i++)
    {
        for(j = 0; j < count; j++)
        {
            if(nn_value_compare(state, values[j], nn_value_findgreater(values[i], values[j])))
            {
                temp = values[i];
                values[i] = values[j];
                values[j] = temp;
                if(nn_value_isarray(values[i]))
                {
                    nn_value_sortvalues(state, nn_value_asarray(values[i])->varray.listitems, nn_valarray_count(&nn_value_asarray(values[i])->varray));
                }

                if(nn_value_isarray(values[j]))
                {
                    nn_value_sortvalues(state, nn_value_asarray(values[j])->varray.listitems, nn_valarray_count(&nn_value_asarray(values[j])->varray));
                }
            }
        }
    }
}

NNValue nn_value_copyvalue(NNState* state, NNValue value)
{
    if(nn_value_isobject(value))
    {
        switch(nn_value_asobject(value)->type)
        {
            case NEON_OBJTYPE_STRING:
                {
                    NNObjString* string;
                    string = nn_value_asstring(value);
                    return nn_value_fromobject(nn_string_copyobject(state, string));
                }
                break;
            case NEON_OBJTYPE_ARRAY:
                {
                    size_t i;
                    NNObjArray* list;
                    NNObjArray* newlist;
                    list = nn_value_asarray(value);
                    newlist = nn_object_makearray(state);
                    nn_vm_stackpush(state, nn_value_fromobject(newlist));
                    for(i = 0; i < nn_valarray_count(&list->varray); i++)
                    {
                        nn_valarray_push(&newlist->varray, nn_valarray_get(&list->varray, i));
                    }
                    nn_vm_stackpop(state);
                    return nn_value_fromobject(newlist);
                }
                break;
            case NEON_OBJTYPE_DICT:
                {
                    NNObjDict *dict;
                    dict = nn_value_asdict(value);
                    return nn_value_fromobject(nn_dict_copy(dict));
                    
                }
                break;
            default:
                break;
        }
    }
    return value;
}



