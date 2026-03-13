
#include "priv.h"

static NNValue nn_objfnnumber_tohexstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)argv;
    (void)argc;
    return nn_value_fromobject(nn_util_numbertohexstring(state, nn_value_asnumber(thisval), false));
}


static NNValue nn_objfnmath_hypot(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(hypot(nn_value_asnumber(argv[0]), nn_value_asnumber(argv[1])));
}


static NNValue nn_objfnmath_abs(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(fabs(nn_value_asnumber(argv[0])));
}

static NNValue nn_objfnmath_round(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(round(nn_value_asnumber(argv[0])));
}

static NNValue nn_objfnmath_sqrt(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(sqrt(nn_value_asnumber(argv[0])));
}

static NNValue nn_objfnmath_ceil(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(ceil(nn_value_asnumber(argv[0])));
}

static NNValue nn_objfnmath_floor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(floor(nn_value_asnumber(argv[0])));
}

static NNValue nn_objfnmath_min(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    double b;
    double x;
    double y;
    (void)state;
    (void)thisval;
    (void)argc;
    x = nn_value_asnumber(argv[0]);
    y = nn_value_asnumber(argv[1]);
    b = (x < y) ? x : y;
    return nn_value_makenumber(b);
}

static NNValue nn_objfnnumber_tobinstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)argv;
    (void)argc;
    return nn_value_fromobject(nn_util_numbertobinstring(state, nn_value_asnumber(thisval)));
}

static NNValue nn_objfnnumber_tooctstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)argv;
    (void)argc;
    return nn_value_fromobject(nn_util_numbertooctstring(state, nn_value_asnumber(thisval), false));
}

static NNValue nn_objfnnumber_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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
    nn_astlex_init(&lex, state, nn_string_getdata(os));
    tok = nn_astlex_scannumber(&lex);
    rtval = nn_astparser_compilestrnumber(tok.type, tok.start);
    return rtval;
}

void nn_state_installobjectnumber(NNState* state)
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

void nn_state_installmodmath(NNState* state)
{
    NNObjClass* klass;
    klass = nn_util_makeclass(state, "Math", state->classprimobject);
    nn_class_defstaticnativemethod(klass, nn_string_intern(state, "hypot"), nn_objfnmath_hypot);
    nn_class_defstaticnativemethod(klass, nn_string_intern(state, "abs"), nn_objfnmath_abs);
    nn_class_defstaticnativemethod(klass, nn_string_intern(state, "round"), nn_objfnmath_round);
    nn_class_defstaticnativemethod(klass, nn_string_intern(state, "sqrt"), nn_objfnmath_sqrt);
    nn_class_defstaticnativemethod(klass, nn_string_intern(state, "ceil"), nn_objfnmath_ceil);
    nn_class_defstaticnativemethod(klass, nn_string_intern(state, "floor"), nn_objfnmath_floor);
    nn_class_defstaticnativemethod(klass, nn_string_intern(state, "min"), nn_objfnmath_min);
}




