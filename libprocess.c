
#include "neon.h"

NNValue nn_objfnprocess_exedirectory(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)thisval;
    (void)argv;
    (void)argc;
    if(state->processinfo->cliexedirectory != NULL)
    {
        return nn_value_fromobject(state->processinfo->cliexedirectory);
    }
    return nn_value_makenull();
}

NNValue nn_objfnprocess_scriptfile(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)thisval;
    (void)argv;
    (void)argc;
    if(state->processinfo->cliscriptfile != NULL)
    {
        return nn_value_fromobject(state->processinfo->cliscriptfile);
    }
    return nn_value_makenull();
}


NNValue nn_objfnprocess_scriptdirectory(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    (void)thisval;
    (void)argv;
    (void)argc;
    if(state->processinfo->cliscriptdirectory != NULL)
    {
        return nn_value_fromobject(state->processinfo->cliscriptdirectory);
    }
    return nn_value_makenull();
}

NNValue nn_objfnprocess_exit(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int rc;
    (void)state;
    (void)thisval;
    rc = 0;
    if(argc > 0)
    {
        rc = nn_value_asnumber(argv[0]);
    }
    exit(rc);
    return nn_value_makenull();
}

NNValue nn_objfnprocess_kill(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int pid;
    int code;
    (void)state;
    (void)thisval;
    (void)argc;
    pid = nn_value_asnumber(argv[0]);
    code = nn_value_asnumber(argv[1]);
    osfn_kill(pid, code);
    return nn_value_makenull();
}

