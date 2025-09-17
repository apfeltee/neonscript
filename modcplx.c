
#include <assert.h>
#include "neon.h"

typedef struct NNUClassComplex NNUClassComplex;
struct NNUClassComplex
{
    NNObjInstance selfinstance;
    double re;
    double im;
};

static NNValue nn_pcomplex_makeinstance(NNState* state, NNObjClass* klass, double re, double im)
{
    NNUClassComplex* inst;
    inst = (NNUClassComplex*)nn_object_makeinstancesize(state, klass, sizeof(NNUClassComplex));
    inst->re = re;
    inst->im = im;
    return nn_value_fromobject((NNObjInstance*)inst);
    
}

static NNValue nn_complexclass_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNUClassComplex* inst;
    (void)thisval;
    (void)argc;
    assert(nn_value_isinstance(thisval));
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    return nn_pcomplex_makeinstance(state, ((NNObjInstance*)inst)->klass, nn_value_asnumber(argv[0]), nn_value_asnumber(argv[1]));
}

static NNValue nn_complexclass_opadd(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue vother;
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    NNUClassComplex* other;
    (void)argc;
    vother = argv[0];
    assert(nn_value_isinstance(thisval));
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    pv = (NNUClassComplex*)inst;
    other = (NNUClassComplex*)nn_value_asinstance(vother);
    return nn_pcomplex_makeinstance(state, ((NNObjInstance*)inst)->klass, pv->re + other->re, pv->im + other->im);
}

static NNValue nn_complexclass_opsub(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue vother;
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    NNUClassComplex* other;
    (void)argc;
    assert(nn_value_isinstance(thisval));
    vother = argv[0];
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    pv = (NNUClassComplex*)inst;
    other = (NNUClassComplex*)nn_value_asinstance(vother);
    return nn_pcomplex_makeinstance(state, ((NNObjInstance*)inst)->klass, pv->re - other->re, pv->im - other->im);
}

static NNValue nn_complexclass_opmul(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    double vre;
    double vim;
    NNValue vother;
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    NNUClassComplex* other;
    (void)argc;
    assert(nn_value_isinstance(thisval));
    vother = argv[0];
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    pv = (NNUClassComplex*)inst;
    other = (NNUClassComplex*)nn_value_asinstance(vother);
    vre = (pv->re * other->re - pv->im * other->im);
    vim = (pv->re * other->im + pv->im * other->re);
    return nn_pcomplex_makeinstance(state, ((NNObjInstance*)inst)->klass, vre, vim);
}

static NNValue nn_complexclass_opdiv(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    double r;
    double i;
    double ti;
    double tr;
    NNValue vother;
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    NNUClassComplex* other;
    (void)argc;
    assert(nn_value_isinstance(thisval));
    vother = argv[0];
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    pv = (NNUClassComplex*)inst;
    other = (NNUClassComplex*)nn_value_asinstance(vother);
    r = other->re;
    i = other->im;
    tr = fabs(r);
    ti = fabs(i);
    if(tr <= ti)
    {
        ti = r / i;
        tr = i * (1 + ti * ti);
        r = pv->re;
        i = pv->im;
    }
    else
    {
        ti = -i / r;
        tr = r * (1 + ti * ti);
        r = -pv->im;
        i = pv->re;
    }
    return nn_pcomplex_makeinstance(state, ((NNObjInstance*)inst)->klass, (r * ti + i) / tr, (i * ti - r) / tr);
}

static NNValue nn_complexclass_getre(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    (void)state;
    (void)argv;
    (void)argc;
    assert(nn_value_isinstance(thisval));
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    pv = (NNUClassComplex*)inst;
    return nn_value_makenumber(pv->re);
}

static NNValue nn_complexclass_getim(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNUClassComplex* inst;
    NNUClassComplex* pv;
    (void)state;
    (void)argv;
    (void)argc;
    assert(nn_value_isinstance(thisval));
    inst = (NNUClassComplex*)nn_value_asinstance(thisval);
    pv = (NNUClassComplex*)inst;
    return nn_value_makenumber(pv->im);
}

NNDefModule* nn_natmodule_load_complex(NNState* state)
{
    static NNDefFunc modfuncs[] =
    {
        {NULL, false, NULL},
    };

    static NNDefField modfields[] =
    {
        {NULL, false, NULL},
    };
    static NNDefModule module;
    (void)state;
    module.name = "complex";
    module.definedfields = modfields;
    module.definedfunctions = modfuncs;
    static NNDefFunc complexfuncs[] =
    {
        {"constructor", false, nn_complexclass_constructor},
        {"__add__", false, nn_complexclass_opadd},
        {"__sub__", false, nn_complexclass_opsub},
        {"__mul__", false, nn_complexclass_opmul},
        {"__div__", false, nn_complexclass_opdiv},
        {NULL, 0, NULL},
    };
    static NNDefField complexfields[] =
    {
        {"re", false, nn_complexclass_getre},
        {"im", false, nn_complexclass_getim},
        {NULL, 0, NULL},
    };
    static NNDefClass classes[] = {
        {"Complex", complexfields, complexfuncs},
        {NULL, NULL, NULL}
    };
    module.definedclasses = classes;
    module.fnpreloaderfunc = NULL;
    module.fnunloaderfunc = NULL;
    return &module;
}



