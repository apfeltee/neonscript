
/* for struct timeval - yeah, i don't know either why windows defines it in winsock ... */
#if defined(_MSC_VER)
    #include <winsock2.h>
#else
    #include <sys/time.h>
#endif

#include "neon.h"

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

/**
* setup global functions.
*/
void nn_state_initbuiltinfunctions(NNState* state)
{
    NNObjClass* klass;
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
    {
        klass = nn_util_makeclass(state, "JSON", state->classprimobject);
        nn_class_defstaticnativemethod(klass, nn_string_copycstr(state, "stringify"), nn_objfnjson_stringify);
    }
}


