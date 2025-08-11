
#include "neon.h"

NNValue nn_objfnnumber_tohexstring(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)argv;
    (void)argc;
    return nn_value_fromobject(nn_util_numbertohexstring(state, nn_value_asnumber(thisval), false));
}

NNValue nn_objfnmath_abs(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(fabs(nn_value_asnumber(argv[0])));
}

NNValue nn_objfnmath_round(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(round(nn_value_asnumber(argv[0])));
}

NNValue nn_objfnmath_sqrt(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(sqrt(nn_value_asnumber(argv[0])));
}

NNValue nn_objfnmath_ceil(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(ceil(nn_value_asnumber(argv[0])));
}

NNValue nn_objfnmath_floor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)state;
    (void)thisval;
    (void)argc;
    return nn_value_makenumber(floor(nn_value_asnumber(argv[0])));
}

NNValue nn_objfnmath_min(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
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



