
#include "neon.h"

NNValue nn_objfnrange_lower(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "lower", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asrange(thisval)->lower);
}

NNValue nn_objfnrange_upper(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "upper", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asrange(thisval)->upper);
}

NNValue nn_objfnrange_range(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "range", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makenumber(nn_value_asrange(thisval)->range);
}

NNValue nn_objfnrange_iter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int val;
    int index;
    NNObjRange* range;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "iter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    range = nn_value_asrange(thisval);
    index = nn_value_asnumber(argv[0]);
    if(index >= 0 && index < range->range)
    {
        if(index == 0)
        {
            return nn_value_makenumber(range->lower);
        }
        if(range->lower > range->upper)
        {
            val = --range->lower;
        }
        else
        {
            val = ++range->lower;
        }
        return nn_value_makenumber(val);
    }
    return nn_value_makenull();
}

NNValue nn_objfnrange_itern(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int index;
    NNObjRange* range;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "itern", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    range = nn_value_asrange(thisval);
    if(nn_value_isnull(argv[0]))
    {
        if(range->range == 0)
        {
            return nn_value_makenull();
        }
        return nn_value_makenumber(0);
    }
    if(!nn_value_isnumber(argv[0]))
    {
        NEON_RETURNERROR("ranges are numerically indexed");
    }
    index = (int)nn_value_asnumber(argv[0]) + 1;
    if(index < range->range)
    {
        return nn_value_makenumber(index);
    }
    return nn_value_makenull();
}

NNValue nn_objfnrange_expand(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int i;
    NNValue val;
    NNObjRange* range;
    NNObjArray* oa;
    (void)argv;
    (void)argc;
    range = nn_value_asrange(thisval);
    oa = nn_object_makearray(state);
    for(i = 0; i < range->range; i++)
    {
        val = nn_value_makenumber(i);
        nn_array_push(oa, val);
    }
    return nn_value_fromobject(oa);
}

NNValue nn_objfnrange_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int a;
    int b;
    NNObjRange* orng;
    (void)thisval;
    (void)argc;
    a = nn_value_asnumber(argv[0]);
    b = nn_value_asnumber(argv[1]);
    orng = nn_object_makerange(state, a, b);
    return nn_value_fromobject(orng);
}


