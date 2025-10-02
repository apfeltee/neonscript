
#include "priv.h"

NNValue nn_modfn_astscan_scan(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    enum {
        /* 12 == "NEON_ASTTOK_".length */
        kTokPrefixLength = 12
    };
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
    scn = nn_astlex_make(state, nn_string_getdata(insrc));
    arr = nn_array_make(state);
    while(!nn_astlex_isatend(scn))
    {
        itm = nn_object_makedict(state);
        token = nn_astlex_scantoken(scn);
        nn_dict_addentrycstr(itm, "line", nn_value_makenumber(token.line));
        cstr = nn_astutil_toktype2str(token.type);
        nn_dict_addentrycstr(itm, "type", nn_value_fromobject(nn_string_copycstr(state, cstr + kTokPrefixLength)));
        nn_dict_addentrycstr(itm, "source", nn_value_fromobject(nn_string_copylen(state, token.start, token.length)));
        nn_array_push(arr, nn_value_fromobject(itm));
    }
    nn_astlex_destroy(state, scn);
    return nn_value_fromobject(arr);
}


NNDefModule* nn_natmodule_load_astscan(NNState* state)
{
    NNDefModule* ret;
    static NNDefFunc modfuncs[] =
    {
        {"scan",   true,  nn_modfn_astscan_scan},
        {NULL,     false, NULL},
    };
    static NNDefField modfields[] =
    {
        {NULL,       false, NULL},
    };
    static NNDefModule module;
    (void)state;
    module.name = "astscan";
    module.definedfields = modfields;
    module.definedfunctions = modfuncs;
    module.definedclasses = NULL;
    module.fnpreloaderfunc = NULL;
    module.fnunloaderfunc = NULL;
    ret = &module;
    return ret;
}


