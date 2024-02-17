

#define BINARY_MOD_OP(state, type, op) \
    do \
    { \
        double dbinright; \
        double dbinleft; \
        neon::Value binvalright; \
        neon::Value binvalleft; \
        binvalright = stackPeek(0); \
        binvalleft = stackPeek(1);\
        if((!binvalright.isNumber() && !binvalright.isBool()) \
        || (!binvalleft.isNumber() && !binvalleft.isBool())) \
        { \
            nn_vmmac_tryraise(state, neon::Status::FAIL_RUNTIME, "unsupported operand %s for %s and %s", #op, neon::Value::Typename(binvalleft), neon::Value::Typename(binvalright)); \
            break; \
        } \
        binvalright = stackPop(); \
        dbinright = binvalright.isBool() ? (binvalright.asBool() ? 1 : 0) : binvalright.asNumber(); \
        binvalleft = stackPop(); \
        dbinleft = binvalleft.isBool() ? (binvalleft.asBool() ? 1 : 0) : binvalleft.asNumber(); \
        stackPush(type(op(dbinleft, dbinright))); \
    } while(false)


neon::Status neon::VM::runVM(int exitframe, neon::Value* rv)
{
    int iterpos;
    int printpos;
    int ofs;
    /*
    * this variable is a NOP; it only exists to ensure that functions outside of the
    * switch tree are not calling nn_vmmac_exitvm(), as its behavior could be undefined.
    */
    bool you_are_calling_exit_vm_outside_of_runvm;
    neon::Value* dbgslot;
    neon::Instruction currinstr;
    neon::Terminal nc;
    you_are_calling_exit_vm_outside_of_runvm = false;
    m_currentframe = &m_framevalues[m_framecount - 1];
    DebugPrinter dp(m_pvm, m_pvm->m_debugprinter);
    for(;;)
    {
        /*
        // try...finally... (i.e. try without a catch but finally
        // whose try body raises an exception)
        // can cause us to go into an invalid mode where frame count == 0
        // to fix this, we need to exit with an appropriate mode here.
        */
        if(m_framecount == 0)
        {
            return neon::Status::FAIL_RUNTIME;
        }
        if(m_pvm->m_conf.dumpinstructions)
        {
            ofs = (int)(m_currentframe->inscode - m_currentframe->closure->scriptfunc->m_compiledblob->m_instrucs);
            dp.printInstructionAt(m_currentframe->closure->scriptfunc->m_compiledblob, ofs, false);
            if(m_pvm->m_conf.dumpprintstack)
            {
                fprintf(stderr, "stack (before)=[\n");
                iterpos = 0;
                for(dbgslot = m_stackvalues; dbgslot < &m_stackvalues[m_stackidx]; dbgslot++)
                {
                    printpos = iterpos + 1;
                    iterpos++;
                    fprintf(stderr, "  [%s%d%s] ", nc.color('y'), printpos, nc.color('0'));
                    m_pvm->m_debugprinter->putformat("%s", nc.color('y'));
                    m_pvm->m_debugprinter->printValue(*dbgslot, true, false);
                    m_pvm->m_debugprinter->putformat("%s", nc.color('0'));
                    fprintf(stderr, "\n");
                }
                fprintf(stderr, "]\n");
            }
        }
        currinstr = readInstruction();
        m_currentinstr = currinstr;
        //fprintf(stderr, "now executing at line %d\n", m_currentinstr.srcline);
        switch(currinstr.code)
        {
            case neon::Instruction::OP_RETURN:
                {
                    size_t ssp;
                    neon::Value result;
                    result = stackPop();
                    if(rv != nullptr)
                    {
                        *rv = result;
                    }
                    ssp = m_currentframe->stackslotpos;
                    nn_vmutil_upvaluesclose(m_pvm, &m_stackvalues[ssp]);
                    m_framecount--;
                    if(m_framecount == 0)
                    {
                        stackPop();
                        return neon::Status::OK;
                    }
                    ssp = m_currentframe->stackslotpos;
                    m_stackidx = ssp;
                    stackPush(result);
                    m_currentframe = &m_framevalues[m_framecount - 1];
                    if(m_framecount == (size_t)exitframe)
                    {
                        return neon::Status::OK;
                    }
                }
                break;
            case neon::Instruction::OP_PUSHCONSTANT:
                {
                    neon::Value constant;
                    constant = readConst();
                    stackPush(constant);
                }
                break;
            case neon::Instruction::OP_PRIMADD:
                {
                    neon::Value valright;
                    neon::Value valleft;
                    neon::Value result;
                    valright = stackPeek(0);
                    valleft = stackPeek(1);
                    if(valright.isString() || valleft.isString())
                    {
                        if(!nn_vmutil_concatenate(m_pvm, false))
                        {
                            nn_vmmac_tryraise(m_pvm, neon::Status::FAIL_RUNTIME, "unsupported operand + for %s and %s", neon::Value::Typename(valleft), neon::Value::Typename(valright));
                            break;
                        }
                    }
                    else if(valleft.isArray() && valright.isArray())
                    {
                        result = neon::Value::fromObject(nn_vmutil_combinearrays(m_pvm, valleft.asArray(), valright.asArray()));
                        stackPop(2);
                        stackPush(result);
                    }
                    else
                    {
                        nn_vmdo_dobinary(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_PRIMSUBTRACT:
                {
                    nn_vmdo_dobinary(m_pvm);
                }
                break;
            case neon::Instruction::OP_PRIMMULTIPLY:
                {
                    int intnum;
                    double dbnum;
                    neon::Value peekleft;
                    neon::Value peekright;
                    neon::Value result;
                    neon::String* string;
                    neon::Array* list;
                    neon::Array* newlist;
                    peekright = stackPeek(0);
                    peekleft = stackPeek(1);
                    if(peekleft.isString() && peekright.isNumber())
                    {
                        dbnum = peekright.asNumber();
                        string = stackPeek(1).asString();
                        result = neon::Value::fromObject(nn_vmutil_multiplystring(m_pvm, string, dbnum));
                        stackPop(2);
                        stackPush(result);
                        break;
                    }
                    else if(peekleft.isArray() && peekright.isNumber())
                    {
                        intnum = (int)peekright.asNumber();
                        stackPop();
                        list = peekleft.asArray();
                        newlist = neon::Array::make(m_pvm);
                        stackPush(neon::Value::fromObject(newlist));
                        nn_vmutil_multiplyarray(m_pvm, list, newlist, intnum);
                        stackPop(2);
                        stackPush(neon::Value::fromObject(newlist));
                        break;
                    }
                    nn_vmdo_dobinary(m_pvm);
                }
                break;
            case neon::Instruction::OP_PRIMDIVIDE:
                {
                    nn_vmdo_dobinary(m_pvm);
                }
                break;
            case neon::Instruction::OP_PRIMMODULO:
                {
                    BINARY_MOD_OP(m_pvm, neon::Value::makeNumber, nn_vmutil_modulo);
                }
                break;
            case neon::Instruction::OP_PRIMPOW:
                {
                    BINARY_MOD_OP(m_pvm, neon::Value::makeNumber, pow);
                }
                break;
            case neon::Instruction::OP_PRIMFLOORDIVIDE:
                {
                    BINARY_MOD_OP(m_pvm, neon::Value::makeNumber, nn_vmutil_floordiv);
                }
                break;
            case neon::Instruction::OP_PRIMNEGATE:
                {
                    neon::Value peeked;
                    peeked = stackPeek(0);
                    if(!peeked.isNumber())
                    {
                        nn_vmmac_tryraise(m_pvm, neon::Status::FAIL_RUNTIME, "operator - not defined for object of type %s", neon::Value::Typename(peeked));
                        break;
                    }
                    stackPush(neon::Value::makeNumber(-stackPop().asNumber()));
                }
                break;
            case neon::Instruction::OP_PRIMBITNOT:
            {
                neon::Value peeked;
                peeked = stackPeek(0);
                if(!peeked.isNumber())
                {
                    nn_vmmac_tryraise(m_pvm, neon::Status::FAIL_RUNTIME, "operator ~ not defined for object of type %s", neon::Value::Typename(peeked));
                    break;
                }
                stackPush(neon::Value::makeInt(~((int)stackPop().asNumber())));
                break;
            }
            case neon::Instruction::OP_PRIMAND:
                {
                    nn_vmdo_dobinary(m_pvm);
                }
                break;
            case neon::Instruction::OP_PRIMOR:
                {
                    nn_vmdo_dobinary(m_pvm);
                }
                break;
            case neon::Instruction::OP_PRIMBITXOR:
                {
                    nn_vmdo_dobinary(m_pvm);
                }
                break;
            case neon::Instruction::OP_PRIMSHIFTLEFT:
                {
                    nn_vmdo_dobinary(m_pvm);
                }
                break;
            case neon::Instruction::OP_PRIMSHIFTRIGHT:
                {
                    nn_vmdo_dobinary(m_pvm);
                }
                break;
            case neon::Instruction::OP_PUSHONE:
                {
                    stackPush(neon::Value::makeNumber(1));
                }
                break;
            /* comparisons */
            case neon::Instruction::OP_EQUAL:
                {
                    neon::Value a;
                    neon::Value b;
                    b = stackPop();
                    a = stackPop();
                    stackPush(neon::Value::makeBool(a.compare(m_pvm, b)));
                }
                break;
            case neon::Instruction::OP_PRIMGREATER:
                {
                    nn_vmdo_dobinary(m_pvm);
                }
                break;
            case neon::Instruction::OP_PRIMLESSTHAN:
                {
                    nn_vmdo_dobinary(m_pvm);
                }
                break;
            case neon::Instruction::OP_PRIMNOT:
                {
                    stackPush(neon::Value::makeBool(neon::Value::isFalse(stackPop())));
                }
                break;
            case neon::Instruction::OP_PUSHNULL:
                {
                    stackPush(neon::Value::makeNull());
                }
                break;
            case neon::Instruction::OP_PUSHEMPTY:
                {
                    stackPush(neon::Value::makeEmpty());
                }
                break;
            case neon::Instruction::OP_PUSHTRUE:
                {
                    stackPush(neon::Value::makeBool(true));
                }
                break;
            case neon::Instruction::OP_PUSHFALSE:
                {
                    stackPush(neon::Value::makeBool(false));
                }
                break;

            case neon::Instruction::OP_JUMPNOW:
                {
                    uint16_t offset;
                    offset = readShort();
                    m_currentframe->inscode += offset;
                }
                break;
            case neon::Instruction::OP_JUMPIFFALSE:
                {
                    uint16_t offset;
                    offset = readShort();
                    if(neon::Value::isFalse(stackPeek(0)))
                    {
                        m_currentframe->inscode += offset;
                    }
                }
                break;
            case neon::Instruction::OP_LOOP:
                {
                    uint16_t offset;
                    offset = readShort();
                    m_currentframe->inscode -= offset;
                }
                break;
            case neon::Instruction::OP_ECHO:
                {
                    neon::Value val;
                    val = stackPeek(0);
                    m_pvm->m_stdoutprinter->printValue(val, m_pvm->m_isreplmode, true);
                    if(!val.isEmpty())
                    {
                        m_pvm->m_stdoutprinter->put("\n");
                    }
                    stackPop();
                }
                break;
            case neon::Instruction::OP_STRINGIFY:
                {
                    neon::Value peeked;
                    neon::String* value;
                    peeked = stackPeek(0);
                    if(!peeked.isString() && !peeked.isNull())
                    {
                        value = neon::Value::toString(m_pvm, stackPop());
                        if(value->length() != 0)
                        {
                            stackPush(neon::Value::fromObject(value));
                        }
                        else
                        {
                            stackPush(neon::Value::makeNull());
                        }
                    }
                }
                break;
            case neon::Instruction::OP_DUPONE:
                {
                    stackPush(stackPeek(0));
                }
                break;
            case neon::Instruction::OP_POPONE:
                {
                    stackPop();
                }
                break;
            case neon::Instruction::OP_POPN:
                {
                    stackPop(readShort());
                }
                break;
            case neon::Instruction::OP_UPVALUECLOSE:
                {
                    nn_vmutil_upvaluesclose(m_pvm, &m_stackvalues[m_stackidx - 1]);
                    stackPop();
                }
                break;
            case neon::Instruction::OP_GLOBALDEFINE:
                {
                    if(!nn_vmdo_globaldefine(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_GLOBALGET:
                {
                    if(!nn_vmdo_globalget(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_GLOBALSET:
                {
                    if(!nn_vmdo_globalset(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_LOCALGET:
                {
                    if(!nn_vmdo_localget(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_LOCALSET:
                {
                    if(!nn_vmdo_localset(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_FUNCARGGET:
                {
                    if(!nn_vmdo_funcargget(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_FUNCARGSET:
                {
                    if(!nn_vmdo_funcargset(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;

            case neon::Instruction::OP_PROPERTYGET:
                {
                    if(!nn_vmdo_propertyget(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_PROPERTYSET:
                {
                    if(!nn_vmdo_propertyset(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_PROPERTYGETSELF:
                {
                    if(!nn_vmdo_propertygetself(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_MAKECLOSURE:
                {
                    if(!nn_vmdo_makeclosure(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_UPVALUEGET:
                {
                    int index;
                    neon::FuncClosure* closure;
                    index = readShort();
                    closure = m_currentframe->closure;
                    if(index < closure->m_upvalcount)
                    {
                        stackPush(closure->m_storedupvals[index]->m_location);
                    }
                    else
                    {
                        stackPush(neon::Value::makeEmpty());
                    }
                }
                break;
            case neon::Instruction::OP_UPVALUESET:
                {
                    int index;
                    index = readShort();
                    if(stackPeek(0).isEmpty())
                    {
                        nn_vmmac_tryraise(m_pvm, neon::Status::FAIL_RUNTIME, "empty cannot be assigned");
                        break;
                    }
                    m_currentframe->closure->m_storedupvals[index]->m_location = stackPeek(0);
                }
                break;
            case neon::Instruction::OP_CALLFUNCTION:
                {
                    int argcount;
                    Value func;
                    argcount = readByte();
                    func = stackPeek(argcount);
                    if(!vmCallValue(func, neon::Value::makeEmpty(), argcount))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                    m_currentframe = &m_framevalues[m_framecount - 1];
                }
                break;
            case neon::Instruction::OP_CALLMETHOD:
                {
                    int argcount;
                    neon::String* method;
                    method = readString();
                    argcount = readByte();
                    if(!vmInvokeMethod(method, argcount))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                    m_currentframe = &m_framevalues[m_framecount - 1];
                }
                break;
            case neon::Instruction::OP_CLASSINVOKETHIS:
                {
                    int argcount;
                    neon::String* method;
                    method = readString();
                    argcount = readByte();
                    if(!vmInvokeSelfMethod(method, argcount))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                    m_currentframe = &m_framevalues[m_framecount - 1];
                }
                break;
            case neon::Instruction::OP_MAKECLASS:
                {
                    bool haveval;
                    neon::Value pushme;
                    neon::String* name;
                    neon::ClassObject* klass;
                    neon::Property* field;
                    haveval = false;
                    name = readString();
                    field = m_currentframe->closure->scriptfunc->m_inmodule->m_deftable->getByObjString(name);
                    if(field != nullptr)
                    {
                        if(field->m_actualval.isClass())
                        {
                            haveval = true;
                            pushme = field->m_actualval;
                        }
                    }
                    field = m_pvm->m_definedglobals->getByObjString(name);
                    if(field != nullptr)
                    {
                        if(field->m_actualval.isClass())
                        {
                            haveval = true;
                            pushme = field->m_actualval;
                        }
                    }
                    if(!haveval)
                    {
                        klass = neon::ClassObject::make(m_pvm, name, nullptr);
                        pushme = neon::Value::fromObject(klass);
                    }
                    stackPush(pushme);
                }
                break;
            case neon::Instruction::OP_MAKEMETHOD:
                {
                    neon::String* name;
                    name = readString();
                    nn_vmutil_definemethod(m_pvm, name);
                }
                break;
            case neon::Instruction::OP_CLASSPROPERTYDEFINE:
                {
                    int isstatic;
                    neon::String* name;
                    name = readString();
                    isstatic = readByte();
                    nn_vmutil_defineproperty(m_pvm, name, isstatic == 1);
                }
                break;
            case neon::Instruction::OP_CLASSINHERIT:
                {
                    neon::ClassObject* superclass;
                    neon::ClassObject* subclass;
                    if(!stackPeek(1).isClass())
                    {
                        nn_vmmac_tryraise(m_pvm, neon::Status::FAIL_RUNTIME, "cannot inherit from non-class object");
                        break;
                    }
                    superclass = stackPeek(1).asClass();
                    subclass = stackPeek(0).asClass();
                    subclass->inheritFrom(superclass);
                    /* pop the subclass */
                    stackPop();
                }
                break;
            case neon::Instruction::OP_CLASSGETSUPER:
                {
                    neon::ClassObject* klass;
                    neon::String* name;
                    name = readString();
                    klass = stackPeek(0).asClass();
                    if(!vmBindMethod(klass->m_superclass, name))
                    {
                        nn_vmmac_tryraise(m_pvm, neon::Status::FAIL_RUNTIME, "class %s does not define a function %s", klass->m_classname->data(), name->data());
                    }
                }
                break;
            case neon::Instruction::OP_CLASSINVOKESUPER:
                {
                    int argcount;
                    neon::ClassObject* klass;
                    neon::String* method;
                    method = readString();
                    argcount = readByte();
                    klass = stackPop().asClass();
                    if(!vmInvokeMethodFromClass(klass, method, argcount))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                    m_currentframe = &m_framevalues[m_framecount - 1];
                }
                break;
            case neon::Instruction::OP_CLASSINVOKESUPERSELF:
                {
                    int argcount;
                    neon::ClassObject* klass;
                    argcount = readByte();
                    klass = stackPop().asClass();
                    if(!vmInvokeMethodFromClass(klass, m_pvm->m_constructorname, argcount))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                    m_currentframe = &m_framevalues[m_framecount - 1];
                }
                break;
            case neon::Instruction::OP_MAKEARRAY:
                {
                    if(!nn_vmdo_makearray(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_MAKERANGE:
                {
                    double lower;
                    double upper;
                    neon::Value vupper;
                    neon::Value vlower;
                    vupper = stackPeek(0);
                    vlower = stackPeek(1);
                    if(!vupper.isNumber() || !vlower.isNumber())
                    {
                        nn_vmmac_tryraise(m_pvm, neon::Status::FAIL_RUNTIME, "invalid range boundaries");
                        break;
                    }
                    lower = vlower.asNumber();
                    upper = vupper.asNumber();
                    stackPop(2);
                    stackPush(neon::Value::fromObject(neon::Range::make(m_pvm, lower, upper)));
                }
                break;
            case neon::Instruction::OP_MAKEDICT:
                {
                    if(!nn_vmdo_makedict(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_INDEXGETRANGED:
                {
                    if(!nn_vmdo_getrangedindex(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_INDEXGET:
                {
                    if(!nn_vmdo_indexget(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_INDEXSET:
                {
                    if(!nn_vmdo_indexset(m_pvm))
                    {
                        nn_vmmac_exitvm(m_pvm);
                    }
                }
                break;
            case neon::Instruction::OP_IMPORTIMPORT:
                {
                    neon::Value res;
                    neon::String* name;
                    neon::Module* mod;
                    name = stackPeek(0).asString();
                    fprintf(stderr, "IMPORTIMPORT: name='%s'\n", name->data());
                    mod = neon::Module::loadModuleByName(m_pvm, m_pvm->m_toplevelmodule, name);
                    fprintf(stderr, "IMPORTIMPORT: mod='%p'\n", mod);
                    if(mod == nullptr)
                    {
                        res = neon::Value::makeNull();
                    }
                    else
                    {
                        res = neon::Value::fromObject(mod);
                    }
                    stackPush(res);
                }
                break;
            case neon::Instruction::OP_TYPEOF:
                {
                    neon::Value res;
                    neon::Value thing;
                    const char* result;
                    thing = stackPop();
                    result = neon::Value::Typename(thing);
                    res = neon::Value::fromObject(neon::String::copy(m_pvm, result));
                    stackPush(res);
                }
                break;
            case neon::Instruction::OP_ASSERT:
                {
                    neon::Value message;
                    neon::Value expression;
                    message = stackPop();
                    expression = stackPop();
                    if(neon::Value::isFalse(expression))
                    {
                        if(!message.isNull())
                        {
                            m_pvm->raiseClass(m_pvm->m_exceptions.asserterror, neon::Value::toString(m_pvm, message)->data());
                        }
                        else
                        {
                            m_pvm->raiseClass(m_pvm->m_exceptions.asserterror, "assertion failed");
                        }
                    }
                }
                break;
            case neon::Instruction::OP_EXTHROW:
                {
                    bool isok;
                    neon::Value peeked;
                    neon::Value stacktrace;
                    neon::ClassInstance* instance;
                    peeked = stackPeek(0);
                    isok = (
                        peeked.isInstance() ||
                        neon::ClassObject::instanceOf(peeked.asInstance()->m_fromclass, m_pvm->m_exceptions.stdexception)
                    );
                    if(!isok)
                    {
                        nn_vmmac_tryraise(m_pvm, neon::Status::FAIL_RUNTIME, "instance of Exception expected");
                        break;
                    }
                    stacktrace = getExceptionStacktrace();
                    instance = peeked.asInstance();
                    instance->defProperty("stacktrace", stacktrace);
                    if(exceptionPropagate())
                    {
                        m_currentframe = &m_framevalues[m_framecount - 1];
                        break;
                    }
                    nn_vmmac_exitvm(m_pvm);
                }
            case neon::Instruction::OP_EXTRY:
                {
                    uint16_t addr;
                    uint16_t finaddr;
                    neon::Value value;
                    neon::String* type;
                    type = readString();
                    addr = readShort();
                    finaddr = readShort();
                    if(addr != 0)
                    {
                        if(!m_pvm->m_definedglobals->get(neon::Value::fromObject(type), &value) || !value.isClass())
                        {
                            if(!m_currentframe->closure->scriptfunc->m_inmodule->m_deftable->get(neon::Value::fromObject(type), &value) || !value.isClass())
                            {
                                nn_vmmac_tryraise(m_pvm, neon::Status::FAIL_RUNTIME, "object of type '%s' is not an exception", type->data());
                                break;
                            }
                        }
                        exceptionPushHandler(value.asClass(), addr, finaddr);
                    }
                    else
                    {
                        exceptionPushHandler(nullptr, addr, finaddr);
                    }
                }
                break;
            case neon::Instruction::OP_EXPOPTRY:
                {
                    m_currentframe->handlercount--;
                }
                break;
            case neon::Instruction::OP_EXPUBLISHTRY:
                {
                    m_currentframe->handlercount--;
                    if(exceptionPropagate())
                    {
                        m_currentframe = &m_framevalues[m_framecount - 1];
                        break;
                    }
                    nn_vmmac_exitvm(m_pvm);
                }
                break;
            case neon::Instruction::OP_SWITCH:
                {
                    neon::Value expr;
                    neon::Value value;
                    neon::VarSwitch* sw;
                    sw = readConst().asSwitch();
                    expr = stackPeek(0);
                    if(sw->m_jumppositions->get(expr, &value))
                    {
                        m_currentframe->inscode += (int)value.asNumber();
                    }
                    else if(sw->m_defaultjump != -1)
                    {
                        m_currentframe->inscode += sw->m_defaultjump;
                    }
                    else
                    {
                        m_currentframe->inscode += sw->m_exitjump;
                    }
                    stackPop();
                }
                break;
            default:
                {
                }
                break;
        }

    }
}

