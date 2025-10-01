
#include "neon.h"

void nn_valtable_print(NNState* state, NNPrinter* pr, NNHashValTable* table, const char* name)
{
    size_t i;
    size_t hcap;
    NNHashValEntry* entry;
    (void)state;
    hcap = nn_valtable_capacity(table);
    nn_printer_printf(pr, "<HashTable of %s : {\n", name);
    for(i = 0; i < hcap; i++)
    {
        entry = nn_valtable_entryatindex(table, i);
        if(!nn_value_isnull(entry->key))
        {
            nn_printer_printvalue(pr, entry->key, true, true);
            nn_printer_printf(pr, ": ");
            nn_printer_printvalue(pr, entry->value.value, true, true);
            if(i != (hcap - 1))
            {
                nn_printer_printf(pr, ",\n");
            }
        }
    }
    nn_printer_printf(pr, "}>\n");
}

void nn_printer_initvars(NNState* state, NNPrinter* pr, NNPrMode mode)
{
    pr->pstate = state;
    pr->fromstack = false;
    pr->wrmode = NEON_PRMODE_UNDEFINED;
    pr->shouldclose = false;
    pr->shouldflush = false;
    pr->stringtaken = false;
    pr->shortenvalues = false;
    pr->jsonmode = false;
    pr->maxvallength = 15;
    pr->handle = NULL;
    pr->wrmode = mode;
}

NNPrinter* nn_printer_makeundefined(NNState* state, NNPrMode mode)
{
    NNPrinter* pr;
    (void)state;
    pr = (NNPrinter*)nn_memory_malloc(sizeof(NNPrinter));
    if(!pr)
    {
        fprintf(stderr, "cannot allocate NNPrinter\n");
        return NULL;
    }
    nn_printer_initvars(state, pr, mode);
    return pr;
}

NNPrinter* nn_printer_makeio(NNState* state, FILE* fh, bool shouldclose)
{
    NNPrinter* pr;
    pr = nn_printer_makeundefined(state, NEON_PRMODE_FILE);
    pr->handle = fh;
    pr->shouldclose = shouldclose;
    return pr;
}

NNPrinter* nn_printer_makestring(NNState* state)
{
    NNPrinter* pr;
    pr = nn_printer_makeundefined(state, NEON_PRMODE_STRING);
    nn_strbuf_makebasicemptystack(&pr->strbuf, 0);
    return pr;
}

void nn_printer_makestackio(NNState* state, NNPrinter* pr, FILE* fh, bool shouldclose)
{
    nn_printer_initvars(state, pr, NEON_PRMODE_FILE);
    pr->fromstack = true;
    pr->handle = fh;
    pr->shouldclose = shouldclose;
}

void nn_printer_makestackstring(NNState* state, NNPrinter* pr)
{
    nn_printer_initvars(state, pr, NEON_PRMODE_STRING);
    pr->fromstack = true;
    pr->wrmode = NEON_PRMODE_STRING;
    nn_strbuf_makebasicemptystack(&pr->strbuf, 0);
}

void nn_printer_destroy(NNPrinter* pr)
{
    NNState* state;
    (void)state;
    if(pr == NULL)
    {
        return;
    }
    if(pr->wrmode == NEON_PRMODE_UNDEFINED)
    {
        return;
    }
    /*fprintf(stderr, "nn_printer_destroy: pr->wrmode=%d\n", pr->wrmode);*/
    state = pr->pstate;
    if(pr->wrmode == NEON_PRMODE_STRING)
    {
        if(!pr->stringtaken)
        {
            nn_strbuf_destroy(&pr->strbuf);
        }
    }
    else if(pr->wrmode == NEON_PRMODE_FILE)
    {
        if(pr->shouldclose)
        {
            #if 0
            fclose(pr->handle);
            #endif
        }
    }
    if(!pr->fromstack)
    {
        nn_memory_free(pr);
        pr = NULL;
    }
}

NNObjString* nn_printer_takestring(NNPrinter* pr)
{
    NNState* state;
    NNObjString* os;
    state = pr->pstate;
    os = nn_string_takelen(state, nn_strbuf_mutdata(&pr->strbuf), nn_strbuf_length(&pr->strbuf));
    pr->stringtaken = true;
    return os;
}

NNObjString* nn_printer_copystring(NNPrinter* pr)
{
    NNState* state;
    NNObjString* os;
    state = pr->pstate;
    os = nn_string_copylen(state, nn_strbuf_data(&pr->strbuf), nn_strbuf_length(&pr->strbuf));
    return os;
}

bool nn_printer_writestringl(NNPrinter* pr, const char* estr, size_t elen)
{
    size_t chlen;
    chlen = sizeof(char);
    if(elen > 0)
    {
        if(pr->wrmode == NEON_PRMODE_FILE)
        {
            fwrite(estr, chlen, elen, pr->handle);
            if(pr->shouldflush)
            {
                fflush(pr->handle);
            }
        }
        else if(pr->wrmode == NEON_PRMODE_STRING)
        {
            nn_strbuf_appendstrn(&pr->strbuf, estr, elen);
        }
        else
        {
            return false;
        }
    }
    return true;
}

bool nn_printer_writestring(NNPrinter* pr, const char* estr)
{
    return nn_printer_writestringl(pr, estr, strlen(estr));
}

bool nn_printer_writechar(NNPrinter* pr, int b)
{
    char ch;
    if(pr->wrmode == NEON_PRMODE_STRING)
    {
        ch = b;
        nn_printer_writestringl(pr, &ch, 1);
    }
    else if(pr->wrmode == NEON_PRMODE_FILE)
    {
        fputc(b, pr->handle);
        if(pr->shouldflush)
        {
            fflush(pr->handle);
        }
    }
    return true;
}

bool nn_printer_writeescapedchar(NNPrinter* pr, int ch)
{
    switch(ch)
    {
        case '\'':
            {
                nn_printer_writestring(pr, "\\\'");
            }
            break;
        case '\"':
            {
                nn_printer_writestring(pr, "\\\"");
            }
            break;
        case '\\':
            {
                nn_printer_writestring(pr, "\\\\");
            }
            break;
        case '\b':
            {
                nn_printer_writestring(pr, "\\b");
            }
            break;
        case '\f':
            {
                nn_printer_writestring(pr, "\\f");
            }
            break;
        case '\n':
            {
                nn_printer_writestring(pr, "\\n");
            }
            break;
        case '\r':
            {
                nn_printer_writestring(pr, "\\r");
            }
            break;
        case '\t':
            {
                nn_printer_writestring(pr, "\\t");
            }
            break;
        case 0:
            {
                nn_printer_writestring(pr, "\\0");
            }
            break;
        default:
            {
                nn_printer_printf(pr, "\\x%02x", (unsigned char)ch);
            }
            break;
    }
    return true;
}

bool nn_printer_writequotedstring(NNPrinter* pr, const char* str, size_t len, bool withquot)
{
    int bch;
    size_t i;
    bch = 0;
    if(withquot)
    {
        nn_printer_writechar(pr, '"');
    }
    for(i = 0; i < len; i++)
    {
        bch = str[i];
        if((bch < 32) || (bch > 127) || (bch == '\"') || (bch == '\\'))
        {
            nn_printer_writeescapedchar(pr, bch);
        }
        else
        {
            nn_printer_writechar(pr, bch);
        }
    }
    if(withquot)
    {
        nn_printer_writechar(pr, '"');
    }
    return true;
}

bool nn_printer_vwritefmttostring(NNPrinter* pr, const char* fmt, va_list va)
{
    #if 0
        size_t wsz;
        size_t needed;
        char* buf;
        va_list copy;
        va_copy(copy, va);
        needed = 1 + vsnprintf(NULL, 0, fmt, copy);
        va_end(copy);
        buf = (char*)nn_memory_malloc(sizeof(char) * (needed + 1));
        if(!buf)
        {
            return false;
        }
        memset(buf, 0, needed + 1);
        wsz = vsnprintf(buf, needed, fmt, va);
        nn_printer_writestringl(pr, buf, wsz);
        nn_memory_free(buf);
    #else
        nn_strbuf_appendformatv(&pr->strbuf, fmt, va);
    #endif
    return true;
}

bool nn_printer_vwritefmt(NNPrinter* pr, const char* fmt, va_list va)
{
    if(pr->wrmode == NEON_PRMODE_STRING)
    {
        return nn_printer_vwritefmttostring(pr, fmt, va);
    }
    else if(pr->wrmode == NEON_PRMODE_FILE)
    {
        vfprintf(pr->handle, fmt, va);
        if(pr->shouldflush)
        {
            fflush(pr->handle);
        }
    }
    return true;
}

bool nn_printer_printf(NNPrinter* pr, const char* fmt, ...) NEON_ATTR_PRINTFLIKE(2, 3);

bool nn_printer_printf(NNPrinter* pr, const char* fmt, ...)
{
    bool b;
    va_list va;
    va_start(va, fmt);
    b = nn_printer_vwritefmt(pr, fmt, va);
    va_end(va);
    return b;
}

void nn_printer_printfunction(NNPrinter* pr, NNObjFunction* func)
{
    if(func->name == NULL)
    {
        nn_printer_printf(pr, "<script at %p>", (void*)func);
    }
    else
    {
        if(func->fnscriptfunc.isvariadic)
        {
            nn_printer_printf(pr, "<function %s(%d...) at %p>", nn_string_getdata(func->name), func->fnscriptfunc.arity, (void*)func);
        }
        else
        {
            nn_printer_printf(pr, "<function %s(%d) at %p>", nn_string_getdata(func->name), func->fnscriptfunc.arity, (void*)func);
        }
    }
}

void nn_printer_printarray(NNPrinter* pr, NNObjArray* list)
{
    size_t i;
    size_t vsz;
    bool isrecur;
    NNValue val;
    NNObjArray* subarr;
    vsz = nn_valarray_count(&list->varray);
    nn_printer_printf(pr, "[");
    for(i = 0; i < vsz; i++)
    {
        isrecur = false;
        val = nn_valarray_get(&list->varray, i);
        if(nn_value_isarray(val))
        {
            subarr = nn_value_asarray(val);
            if(subarr == list)
            {
                isrecur = true;
            }
        }
        if(isrecur)
        {
            nn_printer_printf(pr, "<recursion>");
        }
        else
        {
            nn_printer_printvalue(pr, val, true, true);
        }
        if(i != vsz - 1)
        {
            nn_printer_printf(pr, ",");
        }
        if(pr->shortenvalues && (i >= pr->maxvallength))
        {
            nn_printer_printf(pr, " [%ld items]", vsz);
            break;
        }
    }
    nn_printer_printf(pr, "]");
}

void nn_printer_printdict(NNPrinter* pr, NNObjDict* dict)
{
    size_t i;
    size_t dsz;
    bool keyisrecur;
    bool valisrecur;
    NNValue val;
    NNObjDict* subdict;
    NNProperty* field;
    dsz = nn_valarray_count(&dict->names);
    nn_printer_printf(pr, "{");
    for(i = 0; i < dsz; i++)
    {
        valisrecur = false;
        keyisrecur = false;
        val = nn_valarray_get(&dict->names, i);
        if(nn_value_isdict(val))
        {
            subdict = nn_value_asdict(val);
            if(subdict == dict)
            {
                valisrecur = true;
            }
        }
        if(valisrecur)
        {
            nn_printer_printf(pr, "<recursion>");
        }
        else
        {
            nn_printer_printvalue(pr, val, true, true);
        }
        nn_printer_printf(pr, ": ");
        field = nn_valtable_getfield(&dict->htab, nn_valarray_get(&dict->names, i));
        if(field != NULL)
        {
            if(nn_value_isdict(field->value))
            {
                subdict = nn_value_asdict(field->value);
                if(subdict == dict)
                {
                    keyisrecur = true;
                }
            }
            if(keyisrecur)
            {
                nn_printer_printf(pr, "<recursion>");
            }
            else
            {
                nn_printer_printvalue(pr, field->value, true, true);
            }
        }
        if(i != dsz - 1)
        {
            nn_printer_printf(pr, ", ");
        }
        if(pr->shortenvalues && (pr->maxvallength >= i))
        {
            nn_printer_printf(pr, " [%ld items]", dsz);
            break;
        }
    }
    nn_printer_printf(pr, "}");
}

void nn_printer_printfile(NNPrinter* pr, NNObjFile* file)
{
    nn_printer_printf(pr, "<file at %s in mode %s>", nn_string_getdata(file->path), nn_string_getdata(file->mode));
}

void nn_printer_printinstance(NNPrinter* pr, NNObjInstance* instance, bool invmethod)
{
    (void)invmethod;
    #if 0
    int arity;
    NNPrinter subw;
    NNValue resv;
    NNValue thisval;
    NNProperty* field;
    NNState* state;
    NNObjString* os;
    NNObjArray* args;
    state = pr->pstate;
    if(invmethod)
    {
        field = nn_valtable_getfieldbycstr(&instance->klass->instmethods, "toString");
        if(field != NULL)
        {
            args = nn_object_makearray(state);
            thisval = nn_value_fromobject(instance);
            arity = nn_nestcall_prepare(state, field->value, thisval, NULL, 0);
            fprintf(stderr, "arity = %d\n", arity);
            nn_vm_stackpop(state);
            nn_vm_stackpush(state, thisval);
            if(nn_nestcall_callfunction(state, field->value, thisval, NULL, 0, &resv, false))
            {
                nn_printer_makestackstring(state, &subw);
                nn_printer_printvalue(&subw, resv, false, false);
                os = nn_printer_takestring(&subw);
                nn_printer_writestringl(pr, nn_string_getdata(os), nn_string_getlength(os));
                #if 0
                    nn_vm_stackpop(state);
                #endif
                return;
            }
        }
    }
    #endif
    nn_printer_printf(pr, "<instance of %s at %p>", nn_string_getdata(instance->klass->name), (void*)instance);
}

void nn_printer_printtable(NNPrinter* pr, NNHashValTable* table)
{
    size_t i;
    size_t hcap;
    NNHashValEntry* entry;
    NNHashValEntry* nextentry;

    hcap = nn_valtable_capacity(table);
    nn_printer_printf(pr, "{");
    for(i = 0; i < (size_t)hcap; i++)
    {
        entry = nn_valtable_entryatindex(table, i);
        if(nn_value_isnull(entry->key))
        {
            continue;
        }
        nn_printer_printvalue(pr, entry->key, true, false);
        nn_printer_printf(pr, ":");
        nn_printer_printvalue(pr, entry->value.value, true, false);
        nextentry = nn_valtable_entryatindex(table, i+1);
        if((nextentry != NULL) && !nn_value_isnull(nextentry->key))
        {
            if((i+1) < (size_t)hcap)
            {
                nn_printer_printf(pr, ",");
            }
        }
    }
    nn_printer_printf(pr, "}");
}

void nn_printer_printobjclass(NNPrinter* pr, NNValue value, bool fixstring, bool invmethod)
{
    bool oldexp;
    NNObjClass* klass;
    (void)fixstring;
    (void)invmethod;
    klass = nn_value_asclass(value);
    if(pr->jsonmode)
    {
        nn_printer_printf(pr, "{");
        {
            {
                nn_printer_printf(pr, "name: ");
                nn_printer_printvalue(pr, nn_value_fromobject(klass->name), true, false);
                nn_printer_printf(pr, ",");
            }
            {
                nn_printer_printf(pr, "superclass: ");
                oldexp = pr->jsonmode;
                pr->jsonmode = false;
                nn_printer_printvalue(pr, nn_value_fromobject(klass->superclass), true, false);
                pr->jsonmode = oldexp;
                nn_printer_printf(pr, ",");
            }
            {
                nn_printer_printf(pr, "constructor: ");
                nn_printer_printvalue(pr, klass->constructor, true, false);
                nn_printer_printf(pr, ",");
            }
            {
                nn_printer_printf(pr, "instanceproperties:");
                nn_printer_printtable(pr, &klass->instproperties);
                nn_printer_printf(pr, ",");
            }
            {
                nn_printer_printf(pr, "staticproperties:");
                nn_printer_printtable(pr, &klass->staticproperties);
                nn_printer_printf(pr, ",");
            }
            {
                nn_printer_printf(pr, "instancemethods:");
                nn_printer_printtable(pr, &klass->instmethods);
                nn_printer_printf(pr, ",");
            }
            {
                nn_printer_printf(pr, "staticmethods:");
                nn_printer_printtable(pr, &klass->staticmethods);
            }
        }
        nn_printer_printf(pr, "}");
    }
    else
    {
        nn_printer_printf(pr, "<class %s at %p>", nn_string_getdata(klass->name), (void*)klass);
    }
}

void nn_printer_printobject(NNPrinter* pr, NNValue value, bool fixstring, bool invmethod)
{
    NNObject* obj;
    obj = nn_value_asobject(value);
    switch(obj->type)
    {
        case NEON_OBJTYPE_SWITCH:
            {
                nn_printer_writestring(pr, "<switch>");
            }
            break;
        case NEON_OBJTYPE_USERDATA:
            {
                nn_printer_printf(pr, "<userdata %s>", nn_value_asuserdata(value)->name);
            }
            break;
        case NEON_OBJTYPE_RANGE:
            {
                NNObjRange* range;
                range = nn_value_asrange(value);
                nn_printer_printf(pr, "<range %d .. %d>", range->lower, range->upper);
            }
            break;
        case NEON_OBJTYPE_FILE:
            {
                nn_printer_printfile(pr, nn_value_asfile(value));
            }
            break;
        case NEON_OBJTYPE_DICT:
            {
                nn_printer_printdict(pr, nn_value_asdict(value));
            }
            break;
        case NEON_OBJTYPE_ARRAY:
            {
                nn_printer_printarray(pr, nn_value_asarray(value));
            }
            break;
        case NEON_OBJTYPE_FUNCBOUND:
            {
                NNObjFunction* bn;
                bn = nn_value_asfunction(value);
                nn_printer_printfunction(pr, bn->fnmethod.method->fnclosure.scriptfunc);
            }
            break;
        case NEON_OBJTYPE_MODULE:
            {
                NNObjModule* mod;
                mod = nn_value_asmodule(value);
                nn_printer_printf(pr, "<module '%s' at '%s'>", nn_string_getdata(mod->name), nn_string_getdata(mod->physicalpath));
            }
            break;
        case NEON_OBJTYPE_CLASS:
            {
                nn_printer_printobjclass(pr, value, fixstring, invmethod);
            }
            break;
        case NEON_OBJTYPE_FUNCCLOSURE:
            {
                NNObjFunction* cls;
                cls = nn_value_asfunction(value);
                nn_printer_printfunction(pr, cls->fnclosure.scriptfunc);
            }
            break;
        case NEON_OBJTYPE_FUNCSCRIPT:
            {
                NNObjFunction* fn;
                fn = nn_value_asfunction(value);
                nn_printer_printfunction(pr, fn);
            }
            break;
        case NEON_OBJTYPE_INSTANCE:
            {
                /* @TODO: support the toString() override */
                NNObjInstance* instance;
                instance = nn_value_asinstance(value);
                nn_printer_printinstance(pr, instance, invmethod);
            }
            break;
        case NEON_OBJTYPE_FUNCNATIVE:
            {
                NNObjFunction* native;
                native = nn_value_asfunction(value);
                nn_printer_printf(pr, "<function %s(native) at %p>", nn_string_getdata(native->name), (void*)native);
            }
            break;
        case NEON_OBJTYPE_UPVALUE:
            {
                nn_printer_printf(pr, "<upvalue>");
            }
            break;
        case NEON_OBJTYPE_STRING:
            {
                NNObjString* string;
                string = nn_value_asstring(value);
                if(fixstring)
                {
                    nn_printer_writequotedstring(pr, nn_string_getdata(string), nn_string_getlength(string), true);
                }
                else
                {
                    nn_printer_writestringl(pr, nn_string_getdata(string), nn_string_getlength(string));
                }
            }
            break;
    }
}

void nn_printer_printnumber(NNPrinter* pr, NNValue value)
{
    nn_printer_printf(pr, "%.16g", nn_value_asnumber(value));
}

void nn_printer_printvalue(NNPrinter* pr, NNValue value, bool fixstring, bool invmethod)
{
    if(nn_value_isnull(value))
    {
        nn_printer_writestring(pr, "null");
    }
    else if(nn_value_isbool(value))
    {
        nn_printer_writestring(pr, nn_value_asbool(value) ? "true" : "false");
    }
    else if(nn_value_isnumber(value))
    {
        nn_printer_printnumber(pr, value);
    }
    else
    {
        nn_printer_printobject(pr, value, fixstring, invmethod);
    }
}


