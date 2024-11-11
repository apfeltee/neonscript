
#define nn_class_defnativemethod(a, b, name, fn) \
    {name, fn},

        
        nn_class_defnativemethod(state, state->classprimnumber, "toHexString", nn_objfnnumber_tohexstring);
        nn_class_defnativemethod(state, state->classprimnumber, "toOctString", nn_objfnnumber_tooctstring);
        nn_class_defnativemethod(state, state->classprimnumber, "toBinString", nn_objfnnumber_tobinstring);
