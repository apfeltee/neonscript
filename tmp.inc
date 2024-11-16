    
            VM_CASE(NEON_OP_RETURN)
                {
                    size_t ssp;
                    NNValue result;
                    result = nn_vmbits_stackpop(state);
                    if(rv != NULL)
                    {
                        *rv = result;
                    }
                    ssp = state->vmstate.currentframe->stackslotpos;
                    nn_vmutil_upvaluesclose(state, &state->vmstate.stackvalues[ssp]);
                    state->vmstate.framecount--;
                    if(state->vmstate.framecount == 0)
                    {
                        nn_vmbits_stackpop(state);
                        return NEON_STATUS_OK;
                    }
                    ssp = state->vmstate.currentframe->stackslotpos;
                    state->vmstate.stackidx = ssp;
                    nn_vmbits_stackpush(state, result);
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                    if(state->vmstate.framecount == (size_t)exitframe)
                    {
                        return NEON_STATUS_OK;
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PUSHCONSTANT)
                {
                    NNValue constant;
                    constant = nn_vmbits_readconst(state);
                    nn_vmbits_stackpush(state, constant);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMADD)
                {
                    NNValue valright;
                    NNValue valleft;
                    NNValue result;
                    valright = nn_vmbits_stackpeek(state, 0);
                    valleft = nn_vmbits_stackpeek(state, 1);
                    if(nn_value_isstring(valright) || nn_value_isstring(valleft))
                    {
                        if(nn_util_unlikely(!nn_vmutil_concatenate(state)))
                        {
                            nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "unsupported operand + for %s and %s", nn_value_typename(valleft), nn_value_typename(valright));
                            VM_DISPATCH();
                        }
                    }
                    else if(nn_value_isarray(valleft) && nn_value_isarray(valright))
                    {
                        result = nn_value_fromobject(nn_vmutil_combinearrays(state, nn_value_asarray(valleft), nn_value_asarray(valright)));
                        nn_vmbits_stackpopn(state, 2);
                        nn_vmbits_stackpush(state, result);
                    }
                    else
                    {
                        nn_vmdo_dobinarydirect(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMSUBTRACT)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMMULTIPLY)
                {
                    int intnum;
                    double dbnum;
                    NNValue peekleft;
                    NNValue peekright;
                    NNValue result;
                    NNObjString* string;
                    NNObjArray* list;
                    NNObjArray* newlist;
                    peekright = nn_vmbits_stackpeek(state, 0);
                    peekleft = nn_vmbits_stackpeek(state, 1);
                    if(nn_value_isstring(peekleft) && nn_value_isnumber(peekright))
                    {
                        dbnum = nn_value_asnumber(peekright);
                        string = nn_value_asstring(nn_vmbits_stackpeek(state, 1));
                        result = nn_value_fromobject(nn_vmutil_multiplystring(state, string, dbnum));
                        nn_vmbits_stackpopn(state, 2);
                        nn_vmbits_stackpush(state, result);
                        VM_DISPATCH();
                    }
                    else if(nn_value_isarray(peekleft) && nn_value_isnumber(peekright))
                    {
                        intnum = (int)nn_value_asnumber(peekright);
                        nn_vmbits_stackpop(state);
                        list = nn_value_asarray(peekleft);
                        newlist = nn_object_makearray(state);
                        nn_vmbits_stackpush(state, nn_value_fromobject(newlist));
                        nn_vmutil_multiplyarray(state, list, newlist, intnum);
                        nn_vmbits_stackpopn(state, 2);
                        nn_vmbits_stackpush(state, nn_value_fromobject(newlist));
                        VM_DISPATCH();
                    }
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMDIVIDE)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMMODULO)
                {
                    if(nn_vmdo_dobinaryfunc(state, "%", (nnbinopfunc_t)nn_vmutil_modulo))
                    {
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMPOW)
                {
                    if(nn_vmdo_dobinaryfunc(state, "**", (nnbinopfunc_t)pow))
                    {
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMFLOORDIVIDE)
                {
                    if(nn_vmdo_dobinaryfunc(state, "//", (nnbinopfunc_t)nn_vmutil_floordiv))
                    {
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMNEGATE)
                {
                    NNValue peeked;
                    peeked = nn_vmbits_stackpeek(state, 0);
                    if(!nn_value_isnumber(peeked))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "operator - not defined for object of type %s", nn_value_typename(peeked));
                        VM_DISPATCH();
                    }
                    nn_vmbits_stackpush(state, nn_value_makenumber(-nn_value_asnumber(nn_vmbits_stackpop(state))));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMBITNOT)
            {
                NNValue peeked;
                peeked = nn_vmbits_stackpeek(state, 0);
                if(!nn_value_isnumber(peeked))
                {
                    nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "operator ~ not defined for object of type %s", nn_value_typename(peeked));
                    VM_DISPATCH();
                }
                nn_vmbits_stackpush(state, nn_value_makeint(~((int)nn_value_asnumber(nn_vmbits_stackpop(state)))));
                VM_DISPATCH();
            }
            VM_CASE(NEON_OP_PRIMAND)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMOR)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMBITXOR)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMSHIFTLEFT)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMSHIFTRIGHT)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PUSHONE)
                {
                    nn_vmbits_stackpush(state, nn_value_makenumber(1));
                }
                VM_DISPATCH();
            /* comparisons */
            VM_CASE(NEON_OP_EQUAL)
                {
                    NNValue a;
                    NNValue b;
                    b = nn_vmbits_stackpop(state);
                    a = nn_vmbits_stackpop(state);
                    nn_vmbits_stackpush(state, nn_value_makebool(nn_value_compare(state, a, b)));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMGREATER)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMLESSTHAN)
                {
                    nn_vmdo_dobinarydirect(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PRIMNOT)
                {
                    nn_vmbits_stackpush(state, nn_value_makebool(nn_value_isfalse(nn_vmbits_stackpop(state))));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PUSHNULL)
                {
                    nn_vmbits_stackpush(state, nn_value_makenull());
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PUSHEMPTY)
                {
                    nn_vmbits_stackpush(state, nn_value_makenull());
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PUSHTRUE)
                {
                    nn_vmbits_stackpush(state, nn_value_makebool(true));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PUSHFALSE)
                {
                    nn_vmbits_stackpush(state, nn_value_makebool(false));
                }
                VM_DISPATCH();

            VM_CASE(NEON_OP_JUMPNOW)
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    state->vmstate.currentframe->inscode += offset;
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_JUMPIFFALSE)
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    if(nn_value_isfalse(nn_vmbits_stackpeek(state, 0)))
                    {
                        state->vmstate.currentframe->inscode += offset;
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_LOOP)
                {
                    uint16_t offset;
                    offset = nn_vmbits_readshort(state);
                    state->vmstate.currentframe->inscode -= offset;
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_ECHO)
                {
                    NNValue val;
                    val = nn_vmbits_stackpeek(state, 0);
                    nn_printer_printvalue(state->stdoutprinter, val, state->isrepl, true);
                    if(!nn_value_isnull(val))
                    {
                        nn_printer_writestring(state->stdoutprinter, "\n");
                    }
                    nn_vmbits_stackpop(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_STRINGIFY)
                {
                    NNValue peeked;
                    NNObjString* value;
                    peeked = nn_vmbits_stackpeek(state, 0);
                    if(!nn_value_isstring(peeked) && !nn_value_isnull(peeked))
                    {
                        value = nn_value_tostring(state, nn_vmbits_stackpop(state));
                        if(value->sbuf->length != 0)
                        {
                            nn_vmbits_stackpush(state, nn_value_fromobject(value));
                        }
                        else
                        {
                            nn_vmbits_stackpush(state, nn_value_makenull());
                        }
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_DUPONE)
                {
                    nn_vmbits_stackpush(state, nn_vmbits_stackpeek(state, 0));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_POPONE)
                {
                    nn_vmbits_stackpop(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_POPN)
                {
                    nn_vmbits_stackpopn(state, nn_vmbits_readshort(state));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_UPVALUECLOSE)
                {
                    nn_vmutil_upvaluesclose(state, &state->vmstate.stackvalues[state->vmstate.stackidx - 1]);
                    nn_vmbits_stackpop(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_OPINSTANCEOF)
                {
                    bool rt;
                    NNValue first;
                    NNValue second;
                    NNObjClass* vclass;
                    NNObjClass* checkclass;
                    rt = false;
                    first = nn_vmbits_stackpop(state);
                    second = nn_vmbits_stackpop(state);
                    #if 0
                    nn_vmdebug_printvalue(state, first, "first value");
                    nn_vmdebug_printvalue(state, second, "second value");
                    #endif
                    if(!nn_value_isclass(first))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "invalid use of 'is' on non-class");
                    }
                    checkclass = nn_value_asclass(first);
                    vclass = nn_value_getclassfor(state, second);
                    if(vclass )
                    {
                        rt = nn_util_isinstanceof(vclass, checkclass);
                    }
                    nn_vmbits_stackpush(state, nn_value_makebool(rt));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_GLOBALDEFINE)
                {
                    if(!nn_vmdo_globaldefine(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_GLOBALGET)
                {
                    if(!nn_vmdo_globalget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_GLOBALSET)
                {
                    if(!nn_vmdo_globalset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_LOCALGET)
                {
                    if(!nn_vmdo_localget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_LOCALSET)
                {
                    if(!nn_vmdo_localset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_FUNCARGGET)
                {
                    if(!nn_vmdo_funcargget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_FUNCARGSET)
                {
                    if(!nn_vmdo_funcargset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();

            VM_CASE(NEON_OP_PROPERTYGET)
                {
                    if(!nn_vmdo_propertyget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PROPERTYSET)
                {
                    if(!nn_vmdo_propertyset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_PROPERTYGETSELF)
                {
                    if(!nn_vmdo_propertygetself(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_MAKECLOSURE)
                {
                    if(!nn_vmdo_makeclosure(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_UPVALUEGET)
                {
                    int index;
                    NNObjFuncClosure* closure;
                    index = nn_vmbits_readshort(state);
                    closure = state->vmstate.currentframe->closure;
                    if(index < closure->upvalcount)
                    {
                        nn_vmbits_stackpush(state, closure->upvalues[index]->location);
                    }
                    else
                    {
                        nn_vmbits_stackpush(state, nn_value_makenull());
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_UPVALUESET)
                {
                    int index;
                    index = nn_vmbits_readshort(state);
                    state->vmstate.currentframe->closure->upvalues[index]->location = nn_vmbits_stackpeek(state, 0);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CALLFUNCTION)
                {
                    int argcount;
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vm_callvalue(state, nn_vmbits_stackpeek(state, argcount), nn_value_makenull(), argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CALLMETHOD)
                {
                    int argcount;
                    NNObjString* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vmutil_invokemethod(state, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSGETTHIS)
                {
                    NNValue thisval;
                    thisval = nn_vmbits_stackpeek(state, 3);
                    nn_printer_printf(state->debugwriter, "CLASSGETTHIS: thisval=");
                    nn_printer_printvalue(state->debugwriter, thisval, true, false);
                    nn_printer_printf(state->debugwriter, "\n");
                    nn_vmbits_stackpush(state, thisval);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSINVOKETHIS)
                {
                    int argcount;
                    NNObjString* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    if(!nn_vmutil_invokemethodself(state, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_MAKECLASS)
                {
                    bool haveval;
                    NNValue pushme;
                    NNObjString* name;
                    NNObjClass* klass;
                    NNProperty* field;
                    haveval = false;
                    name = nn_vmbits_readstring(state);
                    field = nn_tableval_getfieldbyostr(state->vmstate.currentframe->closure->scriptfunc->module->deftable, name);
                    if(field != NULL)
                    {
                        if(nn_value_isclass(field->value))
                        {
                            haveval = true;
                            pushme = field->value;
                        }
                    }
                    field = nn_tableval_getfieldbyostr(state->globals, name);
                    if(field != NULL)
                    {
                        if(nn_value_isclass(field->value))
                        {
                            haveval = true;
                            pushme = field->value;
                        }
                    }
                    if(!haveval)
                    {
                        klass = nn_object_makeclass(state, name, state->classprimobject);
                        pushme = nn_value_fromobject(klass);
                    }
                    nn_vmbits_stackpush(state, pushme);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_MAKEMETHOD)
                {
                    NNObjString* name;
                    name = nn_vmbits_readstring(state);
                    nn_vmutil_definemethod(state, name);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSPROPERTYDEFINE)
                {
                    int isstatic;
                    NNObjString* name;
                    name = nn_vmbits_readstring(state);
                    isstatic = nn_vmbits_readbyte(state);
                    nn_vmutil_defineproperty(state, name, isstatic == 1);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSINHERIT)
                {
                    NNObjClass* superclass;
                    NNObjClass* subclass;
                    if(!nn_value_isclass(nn_vmbits_stackpeek(state, 1)))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "cannot inherit from non-class object");
                        VM_DISPATCH();
                    }
                    superclass = nn_value_asclass(nn_vmbits_stackpeek(state, 1));
                    subclass = nn_value_asclass(nn_vmbits_stackpeek(state, 0));
                    nn_class_inheritfrom(subclass, superclass);
                    /* pop the subclass */
                    nn_vmbits_stackpop(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSGETSUPER)
                {
                    NNObjClass* klass;
                    NNObjString* name;
                    name = nn_vmbits_readstring(state);
                    klass = nn_value_asclass(nn_vmbits_stackpeek(state, 0));
                    if(!nn_vmutil_bindmethod(state, klass->superclass, name))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "class %s does not define a function %s", klass->name->sbuf->data, name->sbuf->data);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSINVOKESUPER)
                {
                    int argcount;
                    NNObjClass* klass;
                    NNObjString* method;
                    method = nn_vmbits_readstring(state);
                    argcount = nn_vmbits_readbyte(state);
                    klass = nn_value_asclass(nn_vmbits_stackpop(state));
                    if(!nn_vmutil_invokemethodfromclass(state, klass, method, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_CLASSINVOKESUPERSELF)
                {
                    int argcount;
                    NNObjClass* klass;
                    argcount = nn_vmbits_readbyte(state);
                    klass = nn_value_asclass(nn_vmbits_stackpop(state));
                    if(!nn_vmutil_invokemethodfromclass(state, klass, state->constructorname, argcount))
                    {
                        nn_vmmac_exitvm(state);
                    }
                    state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_MAKEARRAY)
                {
                    if(!nn_vmdo_makearray(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();

            VM_CASE(NEON_OP_MAKERANGE)
                {
                    double lower;
                    double upper;
                    NNValue vupper;
                    NNValue vlower;
                    vupper = nn_vmbits_stackpeek(state, 0);
                    vlower = nn_vmbits_stackpeek(state, 1);
                    if(!nn_value_isnumber(vupper) || !nn_value_isnumber(vlower))
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "invalid range boundaries");
                        VM_DISPATCH();
                    }
                    lower = nn_value_asnumber(vlower);
                    upper = nn_value_asnumber(vupper);
                    nn_vmbits_stackpopn(state, 2);
                    nn_vmbits_stackpush(state, nn_value_fromobject(nn_object_makerange(state, lower, upper)));
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_MAKEDICT)
                {
                    if(!nn_vmdo_makedict(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_INDEXGETRANGED)
                {
                    if(!nn_vmdo_getrangedindex(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_INDEXGET)
                {
                    if(!nn_vmdo_indexget(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_INDEXSET)
                {
                    if(!nn_vmdo_indexset(state))
                    {
                        nn_vmmac_exitvm(state);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_IMPORTIMPORT)
                {
                    NNValue res;
                    NNObjString* name;
                    NNObjModule* mod;
                    name = nn_value_asstring(nn_vmbits_stackpeek(state, 0));
                    fprintf(stderr, "IMPORTIMPORT: name='%s'\n", name->sbuf->data);
                    mod = nn_import_loadmodulescript(state, state->topmodule, name);
                    fprintf(stderr, "IMPORTIMPORT: mod='%p'\n", (void*)mod);
                    if(mod == NULL)
                    {
                        res = nn_value_makenull();
                    }
                    else
                    {
                        res = nn_value_fromobject(mod);
                    }
                    nn_vmbits_stackpush(state, res);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_TYPEOF)
                {
                    NNValue res;
                    NNValue thing;
                    const char* result;
                    thing = nn_vmbits_stackpop(state);
                    result = nn_value_typename(thing);
                    res = nn_value_fromobject(nn_string_copycstr(state, result));
                    nn_vmbits_stackpush(state, res);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_ASSERT)
                {
                    NNValue message;
                    NNValue expression;
                    message = nn_vmbits_stackpop(state);
                    expression = nn_vmbits_stackpop(state);
                    if(nn_value_isfalse(expression))
                    {
                        if(!nn_value_isnull(message))
                        {
                            nn_exceptions_throwclass(state, state->exceptions.asserterror, nn_value_tostring(state, message)->sbuf->data);
                        }
                        else
                        {
                            nn_exceptions_throwclass(state, state->exceptions.asserterror, "assertion failed");
                        }
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_EXTHROW)
                {
                    bool isok;
                    NNValue peeked;
                    NNValue stacktrace;
                    NNObjInstance* instance;
                    peeked = nn_vmbits_stackpeek(state, 0);
                    isok = (
                        nn_value_isinstance(peeked) ||
                        nn_util_isinstanceof(nn_value_asinstance(peeked)->klass, state->exceptions.stdexception)
                    );
                    if(!isok)
                    {
                        nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "instance of Exception expected");
                        VM_DISPATCH();
                    }
                    stacktrace = nn_exceptions_getstacktrace(state);
                    instance = nn_value_asinstance(peeked);
                    nn_instance_defproperty(instance, nn_string_intern(state, "stacktrace"), stacktrace);
                    if(nn_exceptions_propagate(state))
                    {
                        state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                        VM_DISPATCH();
                    }
                    nn_vmmac_exitvm(state);
                }
            VM_CASE(NEON_OP_EXTRY)
                {
                    bool haveclass;
                    uint16_t addr;
                    uint16_t finaddr;
                    NNValue value;
                    NNObjString* type;
                    NNObjClass* exclass;
                    haveclass = false;
                    exclass = NULL;
                    type = nn_vmbits_readstring(state);
                    addr = nn_vmbits_readshort(state);
                    finaddr = nn_vmbits_readshort(state);
                    if(addr != 0)
                    {
                        value = nn_value_makenull();
                        if(!nn_tableval_get(state->globals, nn_value_fromobject(type), &value))
                        {
                            if(nn_value_isclass(value))
                            {
                                haveclass = true;
                                exclass = nn_value_asclass(value);
                            }
                        }
                        if(!haveclass)
                        {
                            /*
                            if(!nn_tableval_get(state->vmstate.currentframe->closure->scriptfunc->module->deftable, nn_value_fromobject(type), &value) || !nn_value_isclass(value))
                            {
                                nn_vmmac_tryraise(state, NEON_STATUS_FAILRUNTIME, "object of type '%s' is not an exception", type->sbuf->data);
                                VM_DISPATCH();
                            }
                            */
                            exclass = state->exceptions.stdexception;
                        }
                        nn_exceptions_pushhandler(state, exclass, addr, finaddr);
                    }
                    else
                    {
                        nn_exceptions_pushhandler(state, NULL, addr, finaddr);
                    }
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_EXPOPTRY)
                {
                    state->vmstate.currentframe->handlercount--;
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_EXPUBLISHTRY)
                {
                    state->vmstate.currentframe->handlercount--;
                    if(nn_exceptions_propagate(state))
                    {
                        state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
                        VM_DISPATCH();
                    }
                    nn_vmmac_exitvm(state);
                }
                VM_DISPATCH();
            VM_CASE(NEON_OP_SWITCH)
                {
                    NNValue expr;
                    NNValue value;
                    NNObjSwitch* sw;
                    sw = nn_value_asswitch(nn_vmbits_readconst(state));
                    expr = nn_vmbits_stackpeek(state, 0);
                    if(nn_tableval_get(sw->table, expr, &value))
                    {
                        state->vmstate.currentframe->inscode += (int)nn_value_asnumber(value);
                    }
                    else if(sw->defaultjump != -1)
                    {
                        state->vmstate.currentframe->inscode += sw->defaultjump;
                    }
                    else
                    {
                        state->vmstate.currentframe->inscode += sw->exitjump;
                    }
                    nn_vmbits_stackpop(state);
                }
                VM_DISPATCH();
