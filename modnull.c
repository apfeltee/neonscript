
#include "neon.h"

/*
* you can use this file as a template for new native modules.
* just fill out the fields, give the load function a meaningful name (i.e., nn_natmodule_load_foobar if your module is "foobar"),
* et cetera.
* then, add said function in libmodule.c's nn_import_loadbuiltinmodules, and you're good to go!
*/

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



