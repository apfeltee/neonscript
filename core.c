
/* for struct timeval - yeah, i don't know either why windows defines it in winsock ... */
#if defined(_MSC_VER)
    #include <winsock2.h>
#else
    #include <sys/time.h>
#endif
#include "neon.h"

NNValue nn_argcheck_vfail(NNArgCheck* ch, const char* srcfile, int srcline, const char* fmt, va_list va)
{
    #if 0
        nn_vm_stackpopn(ch->pstate, ch->argc);
    #endif
    if(!nn_except_vthrowwithclass(ch->pstate, ch->pstate->exceptions.argumenterror, srcfile, srcline, fmt, va))
    {
    }
    return nn_value_makebool(false);
}

NNValue nn_argcheck_fail(NNArgCheck* ch, const char* srcfile, int srcline, const char* fmt, ...)
{
    NNValue v;
    va_list va;
    va_start(va, fmt);
    v = nn_argcheck_vfail(ch, srcfile, srcline, fmt, va);
    va_end(va);
    return v;
}

void nn_argcheck_init(NNState* state, NNArgCheck* ch, const char* name, NNValue* argv, size_t argc)
{
    ch->pstate = state;
    ch->argc = argc;
    ch->argv = argv;
    ch->name = name;
}

NNProperty nn_property_makewithpointer(NNState* state, NNValue val, NNFieldType type)
{
    NNProperty vf;
    (void)state;
    memset(&vf, 0, sizeof(NNProperty));
    vf.type = type;
    vf.value = val;
    vf.havegetset = false;
    return vf;
}

NNProperty nn_property_makewithgetset(NNState* state, NNValue val, NNValue getter, NNValue setter, NNFieldType type)
{
    bool getisfn;
    bool setisfn;
    NNProperty np;
    np = nn_property_makewithpointer(state, val, type);
    setisfn = nn_value_iscallable(setter);
    getisfn = nn_value_iscallable(getter);
    if(getisfn || setisfn)
    {
        np.getset.setter = setter;
        np.getset.getter = getter;
    }
    return np;
}

NNProperty nn_property_make(NNState* state, NNValue val, NNFieldType type)
{
    return nn_property_makewithpointer(state, val, type);
}


NNRegModule* nn_natmodule_load_null(NNState* state)
{
    static NNRegFunc modfuncs[] =
    {
        /* {"somefunc",   true,  myfancymodulefunction},*/
        {NULL, false, NULL},
    };

    static NNRegField modfields[] =
    {
        /*{"somefield", true, the_function_that_gets_called},*/
        {NULL, false, NULL},
    };
    static NNRegModule module;
    (void)state;
    module.name = "null";
    module.fields = modfields;
    module.functions = modfuncs;
    module.classes = NULL;
    module.fnpreloaderfunc = NULL;
    module.fnunloaderfunc = NULL;
    return &module;
}

NNValue nn_modfn_astscan_scan(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    const char* cstr;
    NNObjString* insrc;
    NNAstLexer* scn;
    NNObjArray* arr;
    NNObjDict* itm;
    NNAstToken token;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "scan", argv, argc);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    insrc = nn_value_asstring(argv[0]);
    scn = nn_astlex_make(state, insrc->sbuf.data);
    arr = nn_array_make(state);
    while(!nn_astlex_isatend(scn))
    {
        itm = nn_object_makedict(state);
        token = nn_astlex_scantoken(scn);
        nn_dict_addentrycstr(itm, "line", nn_value_makenumber(token.line));
        cstr = nn_astutil_toktype2str(token.type);
        /* 12 == "NEON_ASTTOK_".length */
        nn_dict_addentrycstr(itm, "type", nn_value_fromobject(nn_string_copycstr(state, cstr + 12)));
        nn_dict_addentrycstr(itm, "source", nn_value_fromobject(nn_string_copylen(state, token.start, token.length)));
        nn_array_push(arr, nn_value_fromobject(itm));
    }
    nn_astlex_destroy(state, scn);
    return nn_value_fromobject(arr);
}



NNValue nn_objfnnumber_tobinstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)argv;
    (void)argc;
    return nn_value_fromobject(nn_util_numbertobinstring(state, nn_value_asnumber(thisval)));
}

NNValue nn_objfnnumber_tooctstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)argv;
    (void)argc;
    return nn_value_fromobject(nn_util_numbertooctstring(state, nn_value_asnumber(thisval), false));
}

NNValue nn_objfnnumber_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue val;
    NNValue rtval;
    NNObjString* os;
    (void)thisval;
    if(argc == 0)
    {
        return nn_value_makenumber(0);
    }
    val = argv[0];
    if(nn_value_isnumber(val))
    {
        return val;
    }
    if(nn_value_isnull(val))
    {
        return nn_value_makenumber(0);
    }
    if(!nn_value_isstring(val))
    {
        NEON_RETURNERROR("Number() expects no arguments, or number, or string");
    }
    NNAstToken tok;
    NNAstLexer lex;
    os = nn_value_asstring(val);
    nn_astlex_init(&lex, state, os->sbuf.data);
    tok = nn_astlex_scannumber(&lex);
    rtval = nn_astparser_compilestrnumber(tok.type, tok.start);
    return rtval;
}



NNValue nn_objfnjson_stringify(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    NNPrinter pr;
    NNObjString* os;
    (void)thisval;
    (void)argc;
    v = argv[0];
    nn_printer_makestackstring(state, &pr);
    pr.jsonmode = true;
    nn_printer_printvalue(&pr, v, true, false);
    os = nn_printer_takestring(&pr);
    nn_printer_destroy(&pr);
    return nn_value_fromobject(os);
}

void nn_state_installmethods(NNState* state, NNObjClass* klass, NNConstClassMethodItem* listmethods)
{
    int i;
    const char* rawname;
    NNNativeFN rawfn;
    NNObjString* osname;
    for(i=0; listmethods[i].name != NULL; i++)
    {
        rawname = listmethods[i].name;
        rawfn = listmethods[i].fn;
        osname = nn_string_copycstr(state, rawname);
        nn_class_defnativemethod(klass, osname, rawfn);
    }
}

void nn_state_initbuiltinmethods(NNState* state)
{
    NNObjClass* klass;
    {
        klass = state->classprimprocess;
        nn_class_setstaticproperty(klass, nn_string_copycstr(state, "directory"), nn_value_fromobject(state->processinfo->cliexedirectory));
        nn_class_setstaticproperty(klass, nn_string_copycstr(state, "env"), nn_value_fromobject(state->envdict));
        nn_class_setstaticproperty(klass, nn_string_copycstr(state, "stdin"), nn_value_fromobject(state->processinfo->filestdin));
        nn_class_setstaticproperty(klass, nn_string_copycstr(state, "stdout"), nn_value_fromobject(state->processinfo->filestdout));
        nn_class_setstaticproperty(klass, nn_string_copycstr(state, "stderr"), nn_value_fromobject(state->processinfo->filestderr));
        nn_class_setstaticproperty(klass, nn_string_copycstr(state, "pid"), nn_value_makenumber(state->processinfo->cliprocessid));
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "kill"), nn_objfnprocess_kill);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "exit"), nn_objfnprocess_exit);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "exedirectory"), nn_objfnprocess_exedirectory);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "scriptdirectory"), nn_objfnprocess_scriptdirectory);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "script"), nn_objfnprocess_scriptfile);
    }
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
    }
    {
        nn_class_defstaticcallablefield(state->classprimclass, nn_string_copycstr(state, "name"), nn_objfnclass_getselfname);

    }    
    {
        static NNConstClassMethodItem numbermethods[] =
        {
            {"toHexString", nn_objfnnumber_tohexstring},
            {"toOctString", nn_objfnnumber_tooctstring},
            {"toBinString", nn_objfnnumber_tobinstring},
            {NULL, NULL},
        };
        nn_class_defnativeconstructor(state->classprimnumber, nn_objfnnumber_constructor);
        nn_state_installmethods(state, state->classprimnumber, numbermethods);
    }
    {
        static NNConstClassMethodItem stringmethods[] =
        {
            {"@iter", nn_objfnstring_iter},
            {"@itern", nn_objfnstring_itern},
            {"size", nn_objfnstring_length},
            {"substr", nn_objfnstring_substring},
            {"substring", nn_objfnstring_substring},
            {"charCodeAt", nn_objfnstring_charcodeat},
            {"charAt", nn_objfnstring_charat},
            {"upper", nn_objfnstring_upper},
            {"lower", nn_objfnstring_lower},
            {"trim", nn_objfnstring_trim},
            {"ltrim", nn_objfnstring_ltrim},
            {"rtrim", nn_objfnstring_rtrim},
            {"split", nn_objfnstring_split},
            {"indexOf", nn_objfnstring_indexof},
            {"count", nn_objfnstring_count},
            {"toNumber", nn_objfnstring_tonumber},
            {"toList", nn_objfnstring_tolist},
            {"lpad", nn_objfnstring_lpad},
            {"rpad", nn_objfnstring_rpad},
            {"replace", nn_objfnstring_replace},
            {"each", nn_objfnstring_each},
            {"startsWith", nn_objfnstring_startswith},
            {"endsWith", nn_objfnstring_endswith},
            {"isAscii", nn_objfnstring_isascii},
            {"isAlpha", nn_objfnstring_isalpha},
            {"isAlnum", nn_objfnstring_isalnum},
            {"isNumber", nn_objfnstring_isnumber},
            {"isFloat", nn_objfnstring_isfloat},
            {"isLower", nn_objfnstring_islower},
            {"isUpper", nn_objfnstring_isupper},
            {"isSpace", nn_objfnstring_isspace},
            {"utf8Chars", nn_objfnstring_utf8chars},
            {"utf8Codepoints", nn_objfnstring_utf8codepoints},
            {"utf8Bytes", nn_objfnstring_utf8codepoints},
            {"match", nn_objfnstring_matchcapture},
            {"matches", nn_objfnstring_matchonly},
            {NULL, NULL},
        };
        nn_class_defnativeconstructor(state->classprimstring, nn_objfnstring_constructor);
        nn_class_defstaticnativemethod(state->classprimstring, nn_string_copycstr(state, "fromCharCode"), nn_objfnstring_fromcharcode);
        nn_class_defstaticnativemethod(state->classprimstring, nn_string_copycstr(state, "utf8Decode"), nn_objfnstring_utf8decode);
        nn_class_defstaticnativemethod(state->classprimstring, nn_string_copycstr(state, "utf8Encode"), nn_objfnstring_utf8encode);
        nn_class_defstaticnativemethod(state->classprimstring, nn_string_copycstr(state, "utf8NumBytes"), nn_objfnstring_utf8numbytes);
        nn_class_defcallablefield(state->classprimstring, nn_string_copycstr(state, "length"), nn_objfnstring_length);
        nn_state_installmethods(state, state->classprimstring, stringmethods);
    }
    {
        static NNConstClassMethodItem arraymethods[] =
        {
            {"size", nn_objfnarray_length},
            {"join", nn_objfnarray_join},
            {"append", nn_objfnarray_append},
            {"push", nn_objfnarray_append},
            {"clear", nn_objfnarray_clear},
            {"clone", nn_objfnarray_clone},
            {"count", nn_objfnarray_count},
            {"extend", nn_objfnarray_extend},
            {"indexOf", nn_objfnarray_indexof},
            {"insert", nn_objfnarray_insert},
            {"pop", nn_objfnarray_pop},
            {"shift", nn_objfnarray_shift},
            {"removeAt", nn_objfnarray_removeat},
            {"remove", nn_objfnarray_remove},
            {"reverse", nn_objfnarray_reverse},
            {"sort", nn_objfnarray_sort},
            {"contains", nn_objfnarray_contains},
            {"delete", nn_objfnarray_delete},
            {"first", nn_objfnarray_first},
            {"last", nn_objfnarray_last},
            {"isEmpty", nn_objfnarray_isempty},
            {"take", nn_objfnarray_take},
            {"get", nn_objfnarray_get},
            {"compact", nn_objfnarray_compact},
            {"unique", nn_objfnarray_unique},
            {"zip", nn_objfnarray_zip},
            {"zipFrom", nn_objfnarray_zipfrom},
            {"toDict", nn_objfnarray_todict},
            {"each", nn_objfnarray_each},
            {"map", nn_objfnarray_map},
            {"filter", nn_objfnarray_filter},
            {"some", nn_objfnarray_some},
            {"every", nn_objfnarray_every},
            {"reduce", nn_objfnarray_reduce},
            {"slice", nn_objfnarray_slice},
            {"@iter", nn_objfnarray_iter},
            {"@itern", nn_objfnarray_itern},
            {NULL, NULL}
        };
        nn_class_defnativeconstructor(state->classprimarray, nn_objfnarray_constructor);
        nn_class_defcallablefield(state->classprimarray, nn_string_copycstr(state, "length"), nn_objfnarray_length);
        nn_state_installmethods(state, state->classprimarray, arraymethods);
    }
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
    {
        static NNConstClassMethodItem filemethods[] =
        {
            {"close", nn_objfnfile_close},
            {"open", nn_objfnfile_open},
            {"read", nn_objfnfile_readmethod},
            {"get", nn_objfnfile_get},
            {"gets", nn_objfnfile_gets},
            {"write", nn_objfnfile_write},
            {"puts", nn_objfnfile_puts},
            {"printf", nn_objfnfile_printf},
            {"number", nn_objfnfile_number},
            {"isTTY", nn_objfnfile_istty},
            {"isOpen", nn_objfnfile_isopen},
            {"isClosed", nn_objfnfile_isclosed},
            {"flush", nn_objfnfile_flush},
            {"stats", nn_objfnfile_statmethod},
            {"path", nn_objfnfile_path},
            {"seek", nn_objfnfile_seek},
            {"tell", nn_objfnfile_tell},
            {"mode", nn_objfnfile_mode},
            {"name", nn_objfnfile_name},
            {"readLine", nn_objfnfile_readline},
            {NULL, NULL},
        };
        nn_class_defnativeconstructor(state->classprimfile, nn_objfnfile_constructor);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "read"), nn_objfnfile_readstatic);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "write"), nn_objfnfile_writestatic);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "put"), nn_objfnfile_writestatic);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "exists"), nn_objfnfile_exists);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "isFile"), nn_objfnfile_isfile);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "isDirectory"), nn_objfnfile_isdirectory);
        nn_class_defstaticnativemethod(state->classprimfile, nn_string_copycstr(state, "stat"), nn_objfnfile_statstatic);
        nn_state_installmethods(state, state->classprimfile, filemethods);
    }
    {
        static NNConstClassMethodItem rangemethods[] =
        {
            {"lower", nn_objfnrange_lower},
            {"upper", nn_objfnrange_upper},
            {"range", nn_objfnrange_range},
            {"expand", nn_objfnrange_expand},
            {"toArray", nn_objfnrange_expand},
            {"@iter", nn_objfnrange_iter},
            {"@itern", nn_objfnrange_itern},
            {NULL, NULL},
        };
        nn_class_defnativeconstructor(state->classprimrange, nn_objfnrange_constructor);
        nn_state_installmethods(state, state->classprimrange, rangemethods);
    }
    {
        klass = nn_util_makeclass(state, "Math", state->classprimobject);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "abs"), nn_objfnmath_abs);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "round"), nn_objfnmath_round);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "sqrt"), nn_objfnmath_sqrt);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "ceil"), nn_objfnmath_ceil);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "floor"), nn_objfnmath_floor);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "min"), nn_objfnmath_min);
    }
    {
        klass = nn_util_makeclass(state, "JSON", state->classprimobject);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "stringify"), nn_objfnjson_stringify);
    }
}

NNValue nn_nativefn_time(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    struct timeval tv;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "time", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    osfn_gettimeofday(&tv, NULL);
    return nn_value_makenumber((double)tv.tv_sec + ((double)tv.tv_usec / 10000000));
}

NNValue nn_nativefn_microtime(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    struct timeval tv;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "microtime", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    osfn_gettimeofday(&tv, NULL);
    return nn_value_makenumber((1000000 * (double)tv.tv_sec) + ((double)tv.tv_usec / 10));
}

NNValue nn_nativefn_id(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue val;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "id", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    val = argv[0];
    return nn_value_makenumber(*(long*)&val);
}

NNValue nn_nativefn_int(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "int", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    if(argc == 0)
    {
        return nn_value_makenumber(0);
    }
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    return nn_value_makenumber((double)((int)nn_value_asnumber(argv[0])));
}

NNValue nn_nativefn_chr(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t len;
    char* string;
    int ch;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "chr", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    ch = nn_value_asnumber(argv[0]);
    string = nn_util_utf8encode(ch, &len);
    return nn_value_fromobject(nn_string_takelen(state, string, len));
}

NNValue nn_nativefn_ord(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int ord;
    int length;
    NNObjString* string;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "ord", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    string = nn_value_asstring(argv[0]);
    length = string->sbuf.length;
    if(length > 1)
    {
        NEON_RETURNERROR("ord() expects character as argument, string given");
    }
    ord = (int)string->sbuf.data[0];
    if(ord < 0)
    {
        ord += 256;
    }
    return nn_value_makenumber(ord);
}

void nn_util_mtseed(uint32_t seed, uint32_t* binst, uint32_t* index)
{
    uint32_t i;
    binst[0] = seed;
    for(i = 1; i < NEON_CONFIG_MTSTATESIZE; i++)
    {
        binst[i] = (uint32_t)(1812433253UL * (binst[i - 1] ^ (binst[i - 1] >> 30)) + i);
    }
    *index = NEON_CONFIG_MTSTATESIZE;
}

uint32_t nn_util_mtgenerate(uint32_t* binst, uint32_t* index)
{
    uint32_t i;
    uint32_t y;
    if(*index >= NEON_CONFIG_MTSTATESIZE)
    {
        for(i = 0; i < NEON_CONFIG_MTSTATESIZE - 397; i++)
        {
            y = (binst[i] & 0x80000000) | (binst[i + 1] & 0x7fffffff);
            binst[i] = binst[i + 397] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
        }
        for(; i < NEON_CONFIG_MTSTATESIZE - 1; i++)
        {
            y = (binst[i] & 0x80000000) | (binst[i + 1] & 0x7fffffff);
            binst[i] = binst[i + (397 - NEON_CONFIG_MTSTATESIZE)] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
        }
        y = (binst[NEON_CONFIG_MTSTATESIZE - 1] & 0x80000000) | (binst[0] & 0x7fffffff);
        binst[NEON_CONFIG_MTSTATESIZE - 1] = binst[396] ^ (y >> 1) ^ ((y & 1) * 0x9908b0df);
        *index = 0;
    }
    y = binst[*index];
    *index = *index + 1;
    y = y ^ (y >> 11);
    y = y ^ ((y << 7) & 0x9d2c5680);
    y = y ^ ((y << 15) & 0xefc60000);
    y = y ^ (y >> 18);
    return y;
}

double nn_util_mtrand(double lowerlimit, double upperlimit)
{
    double randnum;
    uint32_t randval;
    struct timeval tv;
    static uint32_t mtstate[NEON_CONFIG_MTSTATESIZE];
    static uint32_t mtindex = NEON_CONFIG_MTSTATESIZE + 1;
    if(mtindex >= NEON_CONFIG_MTSTATESIZE)
    {
        osfn_gettimeofday(&tv, NULL);
        nn_util_mtseed((uint32_t)(1000000 * tv.tv_sec + tv.tv_usec), mtstate, &mtindex);
    }
    randval = nn_util_mtgenerate(mtstate, &mtindex);
    randnum = lowerlimit + ((double)randval / UINT32_MAX) * (upperlimit - lowerlimit);
    return randnum;
}

NNValue nn_nativefn_rand(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int tmp;
    int lowerlimit;
    int upperlimit;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "rand", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 2);
    lowerlimit = 0;
    upperlimit = 1;
    if(argc > 0)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        lowerlimit = nn_value_asnumber(argv[0]);
    }
    if(argc == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        upperlimit = nn_value_asnumber(argv[1]);
    }
    if(lowerlimit > upperlimit)
    {
        tmp = upperlimit;
        upperlimit = lowerlimit;
        lowerlimit = tmp;
    }
    return nn_value_makenumber(nn_util_mtrand(lowerlimit, upperlimit));
}

NNValue nn_nativefn_eval(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue result;
    NNObjString* os;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "eval", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    os = nn_value_asstring(argv[0]);
    /*fprintf(stderr, "eval:src=%s\n", os->sbuf.data);*/
    result = nn_state_evalsource(state, os->sbuf.data);
    return result;
}

/*
NNValue nn_nativefn_loadfile(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue result;
    NNObjString* os;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "loadfile", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    os = nn_value_asstring(argv[0]);
    fprintf(stderr, "eval:src=%s\n", os->sbuf.data);
    result = nn_state_evalsource(state, os->sbuf.data);
    return result;
}
*/

NNValue nn_nativefn_instanceof(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "instanceof", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isinstance);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isclass);
    return nn_value_makebool(nn_util_isinstanceof(nn_value_asinstance(argv[0])->klass, nn_value_asclass(argv[1])));
}


void nn_strformat_init(NNState* state, NNFormatInfo* nfi, NNPrinter* writer, const char* fmtstr, size_t fmtlen)
{
    nfi->pstate = state;
    nfi->fmtstr = fmtstr;
    nfi->fmtlen = fmtlen;
    nfi->writer = writer;
}

void nn_strformat_destroy(NNFormatInfo* nfi)
{
    (void)nfi;
}

bool nn_strformat_format(NNFormatInfo* nfi, int argc, int argbegin, NNValue* argv)
{
    int ch;
    int ival;
    int nextch;
    bool ok;
    size_t i;
    size_t argpos;
    NNValue cval;
    i = 0;
    argpos = argbegin;
    ok = true;
    while(i < nfi->fmtlen)
    {
        ch = nfi->fmtstr[i];
        nextch = -1;
        if((i + 1) < nfi->fmtlen)
        {
            nextch = nfi->fmtstr[i+1];
        }
        i++;
        if(ch == '%')
        {
            if(nextch == '%')
            {
                nn_printer_writechar(nfi->writer, '%');
            }
            else
            {
                i++;
                if((int)argpos > argc)
                {
                    nn_except_throwclass(nfi->pstate, nfi->pstate->exceptions.argumenterror, "too few arguments");
                    ok = false;
                    cval = nn_value_makenull();
                }
                else
                {
                    cval = argv[argpos];
                }
                argpos++;
                switch(nextch)
                {
                    case 'q':
                    case 'p':
                        {
                            nn_printer_printvalue(nfi->writer, cval, true, true);
                        }
                        break;
                    case 'c':
                        {
                            ival = (int)nn_value_asnumber(cval);
                            nn_printer_printf(nfi->writer, "%c", ival);
                        }
                        break;
                    /* TODO: implement actual field formatting */
                    case 's':
                    case 'd':
                    case 'i':
                    case 'g':
                        {
                            nn_printer_printvalue(nfi->writer, cval, false, true);
                        }
                        break;
                    default:
                        {
                            nn_except_throwclass(nfi->pstate, nfi->pstate->exceptions.argumenterror, "unknown/invalid format flag '%%c'", nextch);
                        }
                        break;
                }
            }
        }
        else
        {
            nn_printer_writechar(nfi->writer, ch);
        }
    }
    return ok;
}

NNValue nn_nativefn_sprintf(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNFormatInfo nfi;
    NNPrinter pr;
    NNObjString* res;
    NNObjString* ofmt;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "sprintf", argv, argc);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = nn_value_asstring(argv[0]);
    nn_printer_makestackstring(state, &pr);
    nn_strformat_init(state, &nfi, &pr, nn_string_getcstr(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, argc, 1, argv))
    {
        return nn_value_makenull();
    }
    res = nn_printer_takestring(&pr);
    nn_printer_destroy(&pr);
    return nn_value_fromobject(res);
}

NNValue nn_nativefn_printf(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNFormatInfo nfi;
    NNObjString* ofmt;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "printf", argv, argc);
    NEON_ARGS_CHECKMINARG(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isstring);
    ofmt = nn_value_asstring(argv[0]);
    nn_strformat_init(state, &nfi, state->stdoutprinter, nn_string_getcstr(ofmt), nn_string_getlength(ofmt));
    if(!nn_strformat_format(&nfi, argc, 1, argv))
    {
    }
    return nn_value_makenull();
}

NNValue nn_nativefn_print(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    (void)thisval;
    for(i = 0; i < argc; i++)
    {
        nn_printer_printvalue(state->stdoutprinter, argv[i], false, true);
    }
    return nn_value_makenull();
}

NNValue nn_nativefn_println(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue v;
    v = nn_nativefn_print(state, thisval, argv, argc);
    nn_printer_writestring(state->stdoutprinter, "\n");
    return v;
}


NNValue nn_nativefn_isnan(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argv;
    (void)argc;
    return nn_value_makebool(false);
}

/**
* setup global functions.
*/
void nn_state_initbuiltinfunctions(NNState* state)
{
    nn_state_defnativefunction(state, "chr", nn_nativefn_chr);
    nn_state_defnativefunction(state, "id", nn_nativefn_id);
    nn_state_defnativefunction(state, "int", nn_nativefn_int);
    nn_state_defnativefunction(state, "instanceof", nn_nativefn_instanceof);
    nn_state_defnativefunction(state, "ord", nn_nativefn_ord);
    nn_state_defnativefunction(state, "sprintf", nn_nativefn_sprintf);
    nn_state_defnativefunction(state, "printf", nn_nativefn_printf);
    nn_state_defnativefunction(state, "print", nn_nativefn_print);
    nn_state_defnativefunction(state, "println", nn_nativefn_println);
    nn_state_defnativefunction(state, "rand", nn_nativefn_rand);
    nn_state_defnativefunction(state, "eval", nn_nativefn_eval);
    nn_state_defnativefunction(state, "isNaN", nn_nativefn_isnan);
    nn_state_defnativefunction(state, "microtime", nn_nativefn_microtime);
    nn_state_defnativefunction(state, "time", nn_nativefn_time);

}

/**
* see @nn_state_warn
*/
void nn_state_vwarn(NNState* state, const char* fmt, va_list va)
{
    if(state->conf.enablewarnings)
    {
        fprintf(stderr, "WARNING: ");
        vfprintf(stderr, fmt, va);
        fprintf(stderr, "\n");
    }
}

/**
* print a non-fatal runtime warning.
*/
void nn_state_warn(NNState* state, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    nn_state_vwarn(state, fmt, va);
    va_end(va);
}

/**
* procuce a stacktrace array; it is an object array, because it used both internally and in scripts.
* cannot take any shortcuts here.
*/
NNValue nn_except_getstacktrace(NNState* state)
{
    int line;
    int64_t i;
    size_t instruction;
    const char* fnname;
    const char* physfile;
    NNCallFrame* frame;
    NNObjFunction* function;
    NNObjString* os;
    NNObjArray* oa;
    NNPrinter pr;
    oa = nn_object_makearray(state);
    {
        for(i = 0; i < state->vmstate.framecount; i++)
        {
            nn_printer_makestackstring(state, &pr);
            frame = &state->vmstate.framevalues[i];
            function = frame->closure->fnclosure.scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->fnscriptfunc.blob.instrucs - 1;
            line = function->fnscriptfunc.blob.instrucs[instruction].srcline;
            physfile = "(unknown)";
            if(function->fnscriptfunc.module->physicalpath != NULL)
            {
                physfile = function->fnscriptfunc.module->physicalpath->sbuf.data;
            }
            fnname = "<script>";
            if(function->name != NULL)
            {
                fnname = function->name->sbuf.data;
            }
            nn_printer_printf(&pr, "from %s() in %s:%d", fnname, physfile, line);
            os = nn_printer_takestring(&pr);
            nn_printer_destroy(&pr);
            nn_array_push(oa, nn_value_fromobject(os));
            if((i > 15) && (state->conf.showfullstack == false))
            {
                nn_printer_makestackstring(state, &pr);
                nn_printer_printf(&pr, "(only upper 15 entries shown)");
                os = nn_printer_takestring(&pr);
                nn_printer_destroy(&pr);
                nn_array_push(oa, nn_value_fromobject(os));
                break;
            }
        }
        return nn_value_fromobject(oa);
    }
    return nn_value_fromobject(nn_string_copylen(state, "", 0));
}

/**
* when an exception occured that was not caught, it is handled here.
*/
bool nn_except_propagate(NNState* state)
{
    int i;
    int cnt;
    int srcline;
    const char* colred;
    const char* colreset;
    const char* colyellow;
    const char* colblue;
    const char* srcfile;
    NNValue stackitm;
    NNObjArray* oa;
    NNObjFunction* function;
    NNExceptionFrame* handler;
    NNObjString* emsg;
    NNObjString* tmp;
    NNObjInstance* exception;
    NNProperty* field;
    exception = nn_value_asinstance(nn_vm_stackpeek(state, 0));
    /* look for a handler .... */
    while(state->vmstate.framecount > 0)
    {
        state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
        for(i = state->vmstate.currentframe->handlercount; i > 0; i--)
        {
            handler = &state->vmstate.currentframe->handlers[i - 1];
            function = state->vmstate.currentframe->closure->fnclosure.scriptfunc;
            if(handler->address != 0 && nn_util_isinstanceof(exception->klass, handler->klass))
            {
                state->vmstate.currentframe->inscode = &function->fnscriptfunc.blob.instrucs[handler->address];
                return true;
            }
            else if(handler->finallyaddress != 0)
            {
                /* continue propagating once the 'finally' block completes */
                nn_vm_stackpush(state, nn_value_makebool(true));
                state->vmstate.currentframe->inscode = &function->fnscriptfunc.blob.instrucs[handler->finallyaddress];
                return true;
            }
        }
        state->vmstate.framecount--;
    }
    /* at this point, the exception is unhandled; so, print it out. */
    colred = nn_util_color(NEON_COLOR_RED);
    colblue = nn_util_color(NEON_COLOR_BLUE);
    colreset = nn_util_color(NEON_COLOR_RESET);
    colyellow = nn_util_color(NEON_COLOR_YELLOW);
    nn_printer_printf(state->debugwriter, "%sunhandled %s%s", colred, exception->klass->name->sbuf.data, colreset);
    srcfile = "none";
    srcline = 0;
    field = nn_valtable_getfieldbycstr(&exception->properties, "srcline");
    if(field != NULL)
    {
        /* why does this happen? */
        if(nn_value_isnumber(field->value))
        {
            srcline = nn_value_asnumber(field->value);
        }
    }
    field = nn_valtable_getfieldbycstr(&exception->properties, "srcfile");
    if(field != NULL)
    {
        if(nn_value_isstring(field->value))
        {
            tmp = nn_value_asstring(field->value);
            srcfile = tmp->sbuf.data;
        }
    }
    nn_printer_printf(state->debugwriter, " [from native %s%s:%d%s]", colyellow, srcfile, srcline, colreset);
    field = nn_valtable_getfieldbycstr(&exception->properties, "message");
    if(field != NULL)
    {
        emsg = nn_value_tostring(state, field->value);
        if(emsg->sbuf.length > 0)
        {
            nn_printer_printf(state->debugwriter, ": %s", emsg->sbuf.data);
        }
        else
        {
            nn_printer_printf(state->debugwriter, ":");
        }
    }
    nn_printer_printf(state->debugwriter, "\n");
    field = nn_valtable_getfieldbycstr(&exception->properties, "stacktrace");
    if(field != NULL)
    {
        nn_printer_printf(state->debugwriter, "%sstacktrace%s:\n", colblue, colreset);
        oa = nn_value_asarray(field->value);
        cnt = nn_valarray_count(&oa->varray);
        i = cnt-1;
        if(cnt > 0)
        {
            while(true)
            {
                stackitm = nn_valarray_get(&oa->varray, i);
                nn_printer_printf(state->debugwriter, "%s", colyellow);
                nn_printer_printf(state->debugwriter, "  ");
                nn_printer_printvalue(state->debugwriter, stackitm, false, true);
                nn_printer_printf(state->debugwriter, "%s\n", colreset);
                if(i == 0)
                {
                    break;
                }
                i--;
            }
        }
    }
    return false;
}

/**
* push an exception handler, assuming it does not exceed NEON_CONFIG_MAXEXCEPTHANDLERS.
*/
bool nn_except_pushhandler(NNState* state, NNObjClass* type, int address, int finallyaddress)
{
    NNCallFrame* frame;
    frame = &state->vmstate.framevalues[state->vmstate.framecount - 1];
    if(frame->handlercount == NEON_CONFIG_MAXEXCEPTHANDLERS)
    {
        nn_state_raisefatalerror(state, "too many nested exception handlers in one function");
        return false;
    }
    frame->handlers[frame->handlercount].address = address;
    frame->handlers[frame->handlercount].finallyaddress = finallyaddress;
    frame->handlers[frame->handlercount].klass = type;
    frame->handlercount++;
    return true;
}


bool nn_except_vthrowactual(NNState* state, NNObjClass* klass, const char* srcfile, int srcline, const char* format, va_list va)
{
    bool b;
    b = nn_except_vthrowwithclass(state, klass, srcfile, srcline, format, va);
    return b;
}

bool nn_except_throwactual(NNState* state, NNObjClass* klass, const char* srcfile, int srcline, const char* format, ...)
{
    bool b;
    va_list va;
    va_start(va, format);
    b = nn_except_vthrowactual(state, klass, srcfile, srcline, format, va);
    va_end(va);
    return b;
}

/**
* throw an exception class. technically, any class can be thrown, but you should really only throw
* those deriving Exception.
*/
bool nn_except_throwwithclass(NNState* state, NNObjClass* klass, const char* srcfile, int srcline, const char* format, ...)
{
    bool b;
    va_list args;
    va_start(args, format);
    b = nn_except_vthrowwithclass(state, klass, srcfile, srcline, format, args);
    va_end(args);
    return b;
}

bool nn_except_vthrowwithclass(NNState* state, NNObjClass* exklass, const char* srcfile, int srcline, const char* format, va_list args)
{
    enum { kMaxBufSize = 1024};
    int length;
    char* message;
    NNValue stacktrace;
    NNObjInstance* instance;
    message = (char*)nn_memory_malloc(kMaxBufSize+1);
    length = vsnprintf(message, kMaxBufSize, format, args);
    instance = nn_except_makeinstance(state, exklass, srcfile, srcline, nn_string_takelen(state, message, length));
    nn_vm_stackpush(state, nn_value_fromobject(instance));
    stacktrace = nn_except_getstacktrace(state);
    nn_vm_stackpush(state, stacktrace);
    nn_instance_defproperty(instance, nn_string_copycstr(state, "stacktrace"), stacktrace);
    nn_vm_stackpop(state);
    return nn_except_propagate(state);
}

/**
* helper for nn_except_makeclass.
*/
NNInstruction nn_util_makeinst(bool isop, uint8_t code, int srcline)
{
    NNInstruction inst;
    inst.isop = isop;
    inst.code = code;
    inst.srcline = srcline;
    return inst;
}

/**
* generate bytecode for a nativee exception class.
* script-side it is enough to just derive from Exception, of course.
*/
NNObjClass* nn_except_makeclass(NNState* state, NNObjModule* module, const char* cstrname, bool iscs)
{
    int messageconst;
    NNObjClass* klass;
    NNObjString* classname;
    NNObjFunction* function;
    NNObjFunction* closure;
    if(iscs)
    {
        classname = nn_string_copycstr(state, cstrname);
    }
    else
    {
        classname = nn_string_copycstr(state, cstrname);
    }
    nn_vm_stackpush(state, nn_value_fromobject(classname));
    klass = nn_object_makeclass(state, classname, state->classprimobject);
    nn_vm_stackpop(state);
    nn_vm_stackpush(state, nn_value_fromobject(klass));
    function = nn_object_makefuncscript(state, module, NEON_FNCONTEXTTYPE_METHOD);
    function->fnscriptfunc.arity = 1;
    function->fnscriptfunc.isvariadic = false;
    nn_vm_stackpush(state, nn_value_fromobject(function));
    {
        /* g_loc 0 */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, (0 >> 8) & 0xff, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, 0 & 0xff, 0));
    }
    {
        /* g_loc 1 */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, (1 >> 8) & 0xff, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, 1 & 0xff, 0));
    }
    {
        messageconst = nn_blob_pushconst(&function->fnscriptfunc.blob, nn_value_fromobject(nn_string_copycstr(state, "message")));
        /* s_prop 0 */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_PROPERTYSET, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, (messageconst >> 8) & 0xff, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, messageconst & 0xff, 0));
    }
    {
        /* pop */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_POPONE, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_POPONE, 0));
    }
    {
        /* g_loc 0 */
        /*
        //  nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        //  nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, (0 >> 8) & 0xff, 0));
        //  nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, 0 & 0xff, 0));
        */
    }
    {
        /* ret */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_RETURN, 0));
    }
    closure = nn_object_makefuncclosure(state, function);
    nn_vm_stackpop(state);
    /* set class constructor */
    nn_vm_stackpush(state, nn_value_fromobject(closure));
    nn_valtable_set(&klass->instmethods, nn_value_fromobject(classname), nn_value_fromobject(closure));
    klass->constructor = nn_value_fromobject(closure);
    /* set class properties */
    nn_class_defproperty(klass, nn_string_copycstr(state, "message"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_copycstr(state, "stacktrace"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_copycstr(state, "srcfile"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_copycstr(state, "srcline"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_copycstr(state, "class"), nn_value_fromobject(klass));
    nn_valtable_set(&state->declaredglobals, nn_value_fromobject(classname), nn_value_fromobject(klass));
    /* for class */
    nn_vm_stackpop(state);
    nn_vm_stackpop(state);
    /* assert error name */
    /* nn_vm_stackpop(state); */
    return klass;
}

/**
* create an instance of an exception class.
*/
NNObjInstance* nn_except_makeinstance(NNState* state, NNObjClass* exklass, const char* srcfile, int srcline, NNObjString* message)
{
    NNObjInstance* instance;
    NNObjString* osfile;
    instance = nn_object_makeinstance(state, exklass);
    osfile = nn_string_copycstr(state, srcfile);
    nn_vm_stackpush(state, nn_value_fromobject(instance));
    nn_instance_defproperty(instance, nn_string_copycstr(state, "class"), nn_value_fromobject(exklass));
    nn_instance_defproperty(instance, nn_string_copycstr(state, "message"), nn_value_fromobject(message));
    nn_instance_defproperty(instance, nn_string_copycstr(state, "srcfile"), nn_value_fromobject(osfile));
    nn_instance_defproperty(instance, nn_string_copycstr(state, "srcline"), nn_value_makenumber(srcline));
    nn_vm_stackpop(state);
    return instance;
}

/**
* raise a fatal error that cannot recover.
*/
void nn_state_raisefatalerror(NNState* state, const char* format, ...)
{
    int i;
    int line;
    size_t instruction;
    va_list args;
    NNCallFrame* frame;
    NNObjFunction* function;
    /* flush out anything on stdout first */
    fflush(stdout);
    frame = &state->vmstate.framevalues[state->vmstate.framecount - 1];
    function = frame->closure->fnclosure.scriptfunc;
    instruction = frame->inscode - function->fnscriptfunc.blob.instrucs - 1;
    line = function->fnscriptfunc.blob.instrucs[instruction].srcline;
    fprintf(stderr, "RuntimeError: ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, " -> %s:%d ", function->fnscriptfunc.module->physicalpath->sbuf.data, line);
    fputs("\n", stderr);
    if(state->vmstate.framecount > 1)
    {
        fprintf(stderr, "stacktrace:\n");
        for(i = state->vmstate.framecount - 1; i >= 0; i--)
        {
            frame = &state->vmstate.framevalues[i];
            function = frame->closure->fnclosure.scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->fnscriptfunc.blob.instrucs - 1;
            fprintf(stderr, "    %s:%d -> ", function->fnscriptfunc.module->physicalpath->sbuf.data, function->fnscriptfunc.blob.instrucs[instruction].srcline);
            if(function->name == NULL)
            {
                fprintf(stderr, "<script>");
            }
            else
            {
                fprintf(stderr, "%s()", function->name->sbuf.data);
            }
            fprintf(stderr, "\n");
        }
    }
    nn_state_resetvmstate(state);
}

bool nn_state_defglobalvalue(NNState* state, const char* name, NNValue val)
{
    bool r;
    NNObjString* oname;
    oname = nn_string_copycstr(state, name);
    nn_vm_stackpush(state, nn_value_fromobject(oname));
    nn_vm_stackpush(state, val);
    r = nn_valtable_set(&state->declaredglobals, state->vmstate.stackvalues[0], state->vmstate.stackvalues[1]);
    nn_vm_stackpopn(state, 2);
    return r;
}

bool nn_state_defnativefunctionptr(NNState* state, const char* name, NNNativeFN fptr, void* uptr)
{
    NNObjFunction* func;
    func = nn_object_makefuncnative(state, fptr, name, uptr);
    return nn_state_defglobalvalue(state, name, nn_value_fromobject(func));
}

bool nn_state_defnativefunction(NNState* state, const char* name, NNNativeFN fptr)
{
    return nn_state_defnativefunctionptr(state, name, fptr, NULL);
}

NNObjClass* nn_util_makeclass(NNState* state, const char* name, NNObjClass* parent)
{
    NNObjClass* cl;
    NNObjString* os;
    os = nn_string_copycstr(state, name);
    cl = nn_object_makeclass(state, os, parent);
    nn_valtable_set(&state->declaredglobals, nn_value_fromobject(os), nn_value_fromobject(cl));
    return cl;
}



void nn_state_buildprocessinfo(NNState* state)
{
    enum{ kMaxBuf = 1024 };
    char* pathp;
    char pathbuf[kMaxBuf];
    state->processinfo = (NNProcessInfo*)nn_memory_malloc(sizeof(NNProcessInfo));
    state->processinfo->cliscriptfile = NULL;
    state->processinfo->cliscriptdirectory = NULL;
    state->processinfo->cliargv = nn_object_makearray(state);
    {
        pathp = osfn_getcwd(pathbuf, kMaxBuf);
        if(pathp == NULL)
        {
            pathp = (char*)".";
        }
        fprintf(stderr, "pathp=<%s>\n", pathp);
        state->processinfo->cliexedirectory = nn_string_copycstr(state, pathp);
    }
    {
        state->processinfo->cliprocessid = osfn_getpid();
    }
    {
        {
            state->processinfo->filestdout = nn_object_makefile(state, stdout, true, "<stdout>", "wb");
            nn_state_defglobalvalue(state, "STDOUT", nn_value_fromobject(state->processinfo->filestdout));
        }
        {
            state->processinfo->filestderr = nn_object_makefile(state, stderr, true, "<stderr>", "wb");
            nn_state_defglobalvalue(state, "STDERR", nn_value_fromobject(state->processinfo->filestderr));
        }
        {
            state->processinfo->filestdin = nn_object_makefile(state, stdin, true, "<stdin>", "rb");
            nn_state_defglobalvalue(state, "STDIN", nn_value_fromobject(state->processinfo->filestdin));
        }
    }
}

void nn_state_updateprocessinfo(NNState* state)
{
    char* prealpath;
    char* prealdir;
    if(state->rootphysfile != NULL)
    {
        prealpath = osfn_realpath(state->rootphysfile, NULL);
        prealdir = osfn_dirname(prealpath);
        state->processinfo->cliscriptfile = nn_string_copycstr(state, prealpath);
        state->processinfo->cliscriptdirectory = nn_string_copycstr(state, prealdir);
        nn_memory_free(prealpath);
        nn_memory_free(prealdir);
    }
    if(state->processinfo->cliscriptdirectory != NULL)
    {
        nn_state_addmodulesearchpathobj(state, state->processinfo->cliscriptdirectory);
    }
}


bool nn_state_makestack(NNState* pstate)
{
    return nn_state_makewithuserptr(pstate, NULL);
}

NNState* nn_state_makealloc()
{
    NNState* state;
    state = (NNState*)nn_memory_malloc(sizeof(NNState));
    if(state == NULL)
    {
        return NULL;
    }
    if(!nn_state_makewithuserptr(state, NULL))
    {
        return NULL;
    }
    return state;
}

bool nn_state_makewithuserptr(NNState* pstate, void* userptr)
{
    if(pstate == NULL)
    {
        return false;
    }
    memset(pstate, 0, sizeof(NNState));
    pstate->memuserptr = userptr;
    pstate->exceptions.stdexception = NULL;
    pstate->rootphysfile = NULL;
    pstate->processinfo = NULL;
    pstate->isrepl = false;
    pstate->markvalue = true;
    nn_vm_initvmstate(pstate);
    nn_state_resetvmstate(pstate);
    {
        pstate->conf.enablestrictmode = false;
        pstate->conf.shoulddumpstack = false;
        pstate->conf.enablewarnings = false;
        pstate->conf.dumpbytecode = false;
        pstate->conf.exitafterbytecode = false;
        pstate->conf.showfullstack = false;
        pstate->conf.enableapidebug = false;
        pstate->conf.maxsyntaxerrors = NEON_CONFIG_MAXSYNTAXERRORS;
    }
    {
        pstate->gcstate.bytesallocated = 0;
        /* default is 1mb. Can be modified via the -g flag. */
        pstate->gcstate.nextgc = NEON_CONFIG_DEFAULTGCSTART;
        pstate->gcstate.graycount = 0;
        pstate->gcstate.graycapacity = 0;
        pstate->gcstate.graystack = NULL;
        pstate->lastreplvalue = nn_value_makenull();
    }
    {
        pstate->stdoutprinter = nn_printer_makeio(pstate, stdout, false);
        pstate->stdoutprinter->shouldflush = true;
        pstate->stderrprinter = nn_printer_makeio(pstate, stderr, false);
        pstate->debugwriter = nn_printer_makeio(pstate, stderr, false);
        pstate->debugwriter->shortenvalues = true;
        pstate->debugwriter->maxvallength = 15;
    }
    {
        nn_valtable_init(pstate, &pstate->openedmodules);
        nn_valtable_init(pstate, &pstate->allocatedstrings);
        nn_valtable_init(pstate, &pstate->declaredglobals);
    }
    {
        pstate->topmodule = nn_module_make(pstate, "", "<state>", false, true);
        pstate->constructorname = nn_string_copycstr(pstate, "constructor");
    }

    {
        pstate->classprimclass = nn_util_makeclass(pstate, "Class", NULL);
        pstate->classprimobject = nn_util_makeclass(pstate, "Object", pstate->classprimclass);
        pstate->classprimnumber = nn_util_makeclass(pstate, "Number", pstate->classprimobject);
        pstate->classprimstring = nn_util_makeclass(pstate, "String", pstate->classprimobject);
        pstate->classprimarray = nn_util_makeclass(pstate, "Array", pstate->classprimobject);
        pstate->classprimdict = nn_util_makeclass(pstate, "Dict", pstate->classprimobject);
        pstate->classprimfile = nn_util_makeclass(pstate, "File", pstate->classprimobject);
        pstate->classprimrange = nn_util_makeclass(pstate, "Range", pstate->classprimobject);
        pstate->classprimcallable = nn_util_makeclass(pstate, "Function", pstate->classprimobject);
        pstate->classprimprocess = nn_util_makeclass(pstate, "Process", pstate->classprimobject);
    }
    {
        pstate->envdict = nn_object_makedict(pstate);
    }
    {
        if(pstate->exceptions.stdexception == NULL)
        {
            pstate->exceptions.stdexception = nn_except_makeclass(pstate, NULL, "Exception", true);
        }
        pstate->exceptions.asserterror = nn_except_makeclass(pstate, NULL, "AssertError", true);
        pstate->exceptions.syntaxerror = nn_except_makeclass(pstate, NULL, "SyntaxError", true);
        pstate->exceptions.ioerror = nn_except_makeclass(pstate, NULL, "IOError", true);
        pstate->exceptions.oserror = nn_except_makeclass(pstate, NULL, "OSError", true);
        pstate->exceptions.argumenterror = nn_except_makeclass(pstate, NULL, "ArgumentError", true);
        pstate->exceptions.regexerror = nn_except_makeclass(pstate, NULL, "RegexError", true);
        pstate->exceptions.importerror = nn_except_makeclass(pstate, NULL, "ImportError", true);

    }
    nn_state_buildprocessinfo(pstate);
    nn_state_setupmodulepaths(pstate);
    {
        nn_state_initbuiltinfunctions(pstate);
        nn_state_initbuiltinmethods(pstate);
    }
    return true;
}

#if 0
    #define destrdebug(...) \
        { \
            fprintf(stderr, "in nn_state_destroy: "); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
        }
#else
    #define destrdebug(...)
#endif
void nn_state_destroy(NNState* state, bool onstack)
{
    destrdebug("destroying importpath...");
    nn_valarray_destroy(&state->importpath, false);
    destrdebug("destroying linked objects...");
    nn_gcmem_destroylinkedobjects(state);
    /* since object in module can exist in declaredglobals, it must come before */
    destrdebug("destroying module table...");
    nn_valtable_destroy(&state->openedmodules);
    destrdebug("destroying globals table...");
    nn_valtable_destroy(&state->declaredglobals);
    destrdebug("destroying strings table...");
    nn_valtable_destroy(&state->allocatedstrings);
    destrdebug("destroying stdoutprinter...");
    nn_printer_destroy(state->stdoutprinter);
    destrdebug("destroying stderrprinter...");
    nn_printer_destroy(state->stderrprinter);
    destrdebug("destroying debugwriter...");
    nn_printer_destroy(state->debugwriter);
    destrdebug("destroying framevalues...");
    nn_memory_free(state->vmstate.framevalues);
    destrdebug("destroying stackvalues...");
    nn_memory_free(state->vmstate.stackvalues);
    nn_memory_free(state->processinfo);
    destrdebug("destroying state...");
    if(!onstack)
    {
        nn_memory_free(state);
    }
    destrdebug("done destroying!");
}

bool nn_util_methodisprivate(NNObjString* name)
{
    return name->sbuf.length > 0 && name->sbuf.data[0] == '_';
}


NNObjFunction* nn_state_compilesource(NNState* state, NNObjModule* module, bool fromeval, const char* source, bool toplevel)
{
    NNBlob blob;
    NNObjFunction* function;
    NNObjFunction* closure;
    (void)toplevel;
    nn_blob_init(state, &blob);
    function = nn_astparser_compilesource(state, module, source, &blob, false, fromeval);
    if(function == NULL)
    {
        nn_blob_destroy(&blob);
        return NULL;
    }
    if(!fromeval)
    {
        nn_vm_stackpush(state, nn_value_fromobject(function));
    }
    else
    {
        function->name = nn_string_copycstr(state, "(evaledcode)");
    }
    closure = nn_object_makefuncclosure(state, function);
    if(!fromeval)
    {
        nn_vm_stackpop(state);
        nn_vm_stackpush(state, nn_value_fromobject(closure));
    }
    nn_blob_destroy(&blob);
    return closure;
}

NNStatus nn_state_execsource(NNState* state, NNObjModule* module, const char* source, const char* filename, NNValue* dest)
{
    char* rp;
    NNStatus status;
    NNObjFunction* closure;
    state->rootphysfile = filename;
    nn_state_updateprocessinfo(state);
    rp = (char*)filename;
    state->topmodule->physicalpath = nn_string_copycstr(state, rp);
    nn_module_setfilefield(state, module);
    closure = nn_state_compilesource(state, module, false, source, true);
    if(closure == NULL)
    {
        return NEON_STATUS_FAILCOMPILE;
    }
    if(state->conf.exitafterbytecode)
    {
        return NEON_STATUS_OK;
    }
    nn_vm_callclosure(state, closure, nn_value_makenull(), 0);
    status = nn_vm_runvm(state, 0, dest);
    return status;
}

NNValue nn_state_evalsource(NNState* state, const char* source)
{
    bool ok;
    int argc;
    NNValue callme;
    NNValue retval;
    NNObjFunction* closure;
    (void)argc;
    closure = nn_state_compilesource(state, state->topmodule, true, source, false);
    callme = nn_value_fromobject(closure);
    argc = nn_nestcall_prepare(state, callme, nn_value_makenull(), NULL, 0);
    ok = nn_nestcall_callfunction(state, callme, nn_value_makenull(), NULL, 0, &retval);
    if(!ok)
    {
        nn_except_throw(state, "eval() failed");
    }
    return retval;
}



