

    NEON_FORCEINLINE Value VM::readConst()
    {
        uint16_t idx;
        idx = readShort();
        return m_currentframe->closure->scriptfunc->m_compiledblob->m_constants->m_values[idx];
    }


    bool VM::callClosure(FuncClosure* closure, Value thisval, int argcount)
    {
        int i;
        int startva;
        CallFrame* frame;
        Array* argslist;
        NEON_APIDEBUG(m_pvm, "thisval.type=%s, argcount=%d", Value::Typename(thisval), argcount);
        /* fill empty parameters if not variadic */
        for(; !closure->scriptfunc->m_isvariadic && argcount < closure->scriptfunc->m_arity; argcount++)
        {
            stackPush(Value::makeNull());
        }
        /* handle variadic arguments... */
        if(closure->scriptfunc->m_isvariadic && argcount >= closure->scriptfunc->m_arity - 1)
        {
            startva = argcount - closure->scriptfunc->m_arity;
            argslist = Array::make(m_pvm);
            stackPush(Value::fromObject(argslist));
            for(i = startva; i >= 0; i--)
            {
                argslist->push(stackPeek(i + 1));
            }
            argcount -= startva;
            /* +1 for the gc protection push above */
            stackPop(startva + 2);
            stackPush(Value::fromObject(argslist));
        }
        if(argcount != closure->scriptfunc->m_arity)
        {
            stackPop(argcount);
            if(closure->scriptfunc->m_isvariadic)
            {
                return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "expected at least %d arguments but got %d", closure->scriptfunc->m_arity - 1, argcount);
            }
            else
            {
                return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "expected %d arguments but got %d", closure->scriptfunc->m_arity, argcount);
            }
        }
        if(checkMaybeResize())
        {
            /* stackPop(argcount); */
        }
        frame = &m_framevalues[m_framecount++];
        frame->gcprotcount = 0;
        frame->handlercount = 0;
        frame->closure = closure;
        frame->inscode = closure->scriptfunc->m_compiledblob->m_instrucs;
        frame->stackslotpos = m_stackidx + (-argcount - 1);
        return true;
    }

    bool VM::vmCallNative(FuncNative* native, Value thisval, int argcount)
    {
        size_t spos;
        Value r;
        Value* vargs;
        NEON_APIDEBUG(m_pvm, "thisval.type=%s, argcount=%d", Value::Typename(thisval), argcount);
        spos = m_stackidx + (-argcount);
        vargs = &m_stackvalues[spos];
        Arguments fnargs(m_pvm, native->m_nativefnname, thisval, vargs, argcount, native->m_userptrforfn);
        r = native->m_natfunc(m_pvm, &fnargs);
        {
            m_stackvalues[spos - 1] = r;
            m_stackidx -= argcount;
        }
        m_pvm->m_vmstate->gcClearProtect();
        return true;
    }

    bool VM::vmCallBoundValue(Value callable, Value thisval, int argcount)
    {
        size_t spos;
        NEON_APIDEBUG(m_pvm, "thisval.type=%s, argcount=%d", Value::Typename(thisval), argcount);
        if(callable.isObject())
        {
            switch(callable.objectType())
            {
                case Value::OBJTYPE_FUNCBOUND:
                    {
                        FuncBound* bound;
                        bound = callable.asFuncBound();
                        spos = (m_stackidx + (-argcount - 1));
                        m_stackvalues[spos] = thisval;
                        return callClosure(bound->method, thisval, argcount);
                    }
                    break;
                case Value::OBJTYPE_CLASS:
                    {
                        ClassObject* klass;
                        klass = callable.asClass();
                        spos = (m_stackidx + (-argcount - 1));
                        m_stackvalues[spos] = thisval;
                        if(!klass->m_constructor.isEmpty())
                        {
                            return vmCallBoundValue(klass->m_constructor, thisval, argcount);
                        }
                        else if(klass->m_superclass != nullptr && !klass->m_superclass->m_constructor.isEmpty())
                        {
                            return vmCallBoundValue(klass->m_superclass->m_constructor, thisval, argcount);
                        }
                        else if(argcount != 0)
                        {
                            return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "%s constructor expects 0 arguments, %d given", klass->m_classname->data(), argcount);
                        }
                        return true;
                    }
                    break;
                case Value::OBJTYPE_MODULE:
                    {
                        Module* module;
                        Property* field;
                        module = callable.asModule();
                        field = module->m_deftable->getByObjString(module->m_modname);
                        if(field != nullptr)
                        {
                            return vmCallValue(field->m_actualval, thisval, argcount);
                        }
                        return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "module %s does not export a default function", module->m_modname);
                    }
                    break;
                case Value::OBJTYPE_FUNCCLOSURE:
                    {
                        return callClosure(callable.asFuncClosure(), thisval, argcount);
                    }
                    break;
                case Value::OBJTYPE_FUNCNATIVE:
                    {
                        return vmCallNative(callable.asFuncNative(), thisval, argcount);
                    }
                    break;
                default:
                    break;
            }
        }
        return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "object of type %s is not callable", Value::Typename(callable));
    }

    bool VM::vmCallValue(Value callable, Value thisval, int argcount)
    {
        Value actualthisval;
        if(callable.isObject())
        {
            switch(callable.objectType())
            {
                case Value::OBJTYPE_FUNCBOUND:
                    {
                        FuncBound* bound;
                        bound = callable.asFuncBound();
                        actualthisval = bound->receiver;
                        if(!thisval.isEmpty())
                        {
                            actualthisval = thisval;
                        }
                        NEON_APIDEBUG(m_pvm, "actualthisval.type=%s, argcount=%d", Value::Typename(actualthisval), argcount);
                        return vmCallBoundValue(callable, actualthisval, argcount);
                    }
                    break;
                case Value::OBJTYPE_CLASS:
                    {
                        ClassObject* klass;
                        ClassInstance* instance;
                        klass = callable.asClass();
                        instance = ClassInstance::make(m_pvm, klass);
                        actualthisval = Value::fromObject(instance);
                        if(!thisval.isEmpty())
                        {
                            actualthisval = thisval;
                        }
                        NEON_APIDEBUG(m_pvm, "actualthisval.type=%s, argcount=%d", Value::Typename(actualthisval), argcount);
                        return vmCallBoundValue(callable, actualthisval, argcount);
                    }
                    break;
                default:
                    {
                    }
                    break;
            }
        }
        NEON_APIDEBUG(m_pvm, "thisval.type=%s, argcount=%d", Value::Typename(thisval), argcount);
        return vmCallBoundValue(callable, thisval, argcount);
    }

    bool VM::vmInvokeMethodFromClass(ClassObject* klass, String* name, int argcount)
    {
        Property* field;
        NEON_APIDEBUG(m_pvm, "argcount=%d", argcount);
        field = klass->m_methods->getByObjString(name);
        if(field != nullptr)
        {
            if(FuncCommon::getMethodType(field->m_actualval) == FuncCommon::FUNCTYPE_PRIVATE)
            {
                return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "cannot call private method '%s' from instance of %s", name->data(), klass->m_classname->data());
            }
            return vmCallBoundValue(field->m_actualval, Value::fromObject(klass), argcount);
        }
        return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "undefined method '%s' in %s", name->data(), klass->m_classname->data());
    }

    bool VM::vmInvokeSelfMethod(String* name, int argcount)
    {
        size_t spos;
        Value receiver;
        ClassInstance* instance;
        Property* field;
        NEON_APIDEBUG(m_pvm, "argcount=%d", argcount);
        receiver = stackPeek(argcount);
        if(receiver.isInstance())
        {
            instance = receiver.asInstance();
            field = instance->m_fromclass->m_methods->getByObjString(name);
            if(field != nullptr)
            {
                return vmCallBoundValue(field->m_actualval, receiver, argcount);
            }
            field = instance->m_properties->getByObjString(name);
            if(field != nullptr)
            {
                spos = (m_stackidx + (-argcount - 1));
                m_stackvalues[spos] = receiver;
                return vmCallBoundValue(field->m_actualval, receiver, argcount);
            }
        }
        else if(receiver.isClass())
        {
            field = receiver.asClass()->m_methods->getByObjString(name);
            if(field != nullptr)
            {
                if(FuncCommon::getMethodType(field->m_actualval) == FuncCommon::FUNCTYPE_STATIC)
                {
                    return vmCallBoundValue(field->m_actualval, receiver, argcount);
                }
                return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "cannot call non-static method %s() on non instance", name->data());
            }
        }
        return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "cannot call method '%s' on object of type '%s'", name->data(), Value::Typename(receiver));
    }

    bool VM::vmInvokeMethod(String* name, int argcount)
    {
        size_t spos;
        Value::ObjType rectype;
        Value receiver;
        Property* field;
        ClassObject* klass;
        receiver = stackPeek(argcount);
        NEON_APIDEBUG(m_pvm, "receiver.type=%s, argcount=%d", Value::Typename(receiver), argcount);
        if(receiver.isObject())
        {
            rectype = receiver.asObject()->m_objtype;
            switch(rectype)
            {
                case Value::OBJTYPE_MODULE:
                    {
                        Module* module;
                        NEON_APIDEBUG(m_pvm, "receiver is a module");
                        module = receiver.asModule();
                        field = module->m_deftable->getByObjString(name);
                        if(field != nullptr)
                        {
                            return vmCallBoundValue(field->m_actualval, receiver, argcount);
                        }
                        return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "module %s does not define class or method %s()", module->m_modname, name->data());
                    }
                    break;
                case Value::OBJTYPE_CLASS:
                    {
                        NEON_APIDEBUG(m_pvm, "receiver is a class");
                        klass = receiver.asClass();
                        field = klass->m_methods->getByObjString(name);
                        if(field != nullptr)
                        {
                            if(FuncCommon::getMethodType(field->m_actualval) == FuncCommon::FUNCTYPE_PRIVATE)
                            {
                                return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "cannot call private method %s() on %s", name->data(), klass->m_classname->data());
                            }
                            return vmCallBoundValue(field->m_actualval, receiver, argcount);
                        }
                        else
                        {
                            field = klass->getStaticProperty(name);
                            if(field != nullptr)
                            {
                                return vmCallBoundValue(field->m_actualval, receiver, argcount);
                            }
                            field = klass->getStaticMethodField(name);
                            if(field != nullptr)
                            {
                                return vmCallBoundValue(field->m_actualval, receiver, argcount);
                            }
                        }
                        return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "unknown method %s() in class %s", name->data(), klass->m_classname->data());
                    }
                case Value::OBJTYPE_INSTANCE:
                    {
                        ClassInstance* instance;
                        NEON_APIDEBUG(m_pvm, "receiver is an instance");
                        instance = receiver.asInstance();
                        field = instance->m_properties->getByObjString(name);
                        if(field != nullptr)
                        {
                            spos = (m_stackidx + (-argcount - 1));
                            m_stackvalues[spos] = receiver;
                            return vmCallBoundValue(field->m_actualval, receiver, argcount);
                        }
                        return vmInvokeMethodFromClass(instance->m_fromclass, name, argcount);
                    }
                    break;
                case Value::OBJTYPE_DICT:
                    {
                        NEON_APIDEBUG(m_pvm, "receiver is a dictionary");
                        field = m_pvm->m_classprimdict->getMethodField(name);
                        if(field != nullptr)
                        {
                            return m_pvm->m_vmstate->vmCallNative(field->m_actualval.asFuncNative(), receiver, argcount);
                        }
                        /* NEW in v0.0.84, dictionaries can declare extra methods as part of their entries. */
                        else
                        {
                            field = receiver.asDict()->m_valtable->getByObjString(name);
                            if(field != nullptr)
                            {
                                if(field->m_actualval.isCallable())
                                {
                                    return vmCallBoundValue(field->m_actualval, receiver, argcount);
                                }
                            }
                        }
                        return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "'dict' has no method %s()", name->data());
                    }
                    default:
                        {
                        }
                        break;
            }
        }
        klass = m_pvm->vmGetClassFor(receiver);
        if(klass == nullptr)
        {
            /* @TODO: have methods for non objects as well. */
            return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "non-object %s has no method named '%s'", Value::Typename(receiver), name->data());
        }
        field = klass->getMethodField(name);
        if(field != nullptr)
        {
            return vmCallBoundValue(field->m_actualval, receiver, argcount);
        }
        return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "'%s' has no method %s()", klass->m_classname->data(), name->data());
    }

    bool VM::vmBindMethod(ClassObject* klass, String* name)
    {
        Value val;
        Property* field;
        FuncBound* bound;
        field = klass->m_methods->getByObjString(name);
        if(field != nullptr)
        {
            if(FuncCommon::getMethodType(field->m_actualval) == FuncCommon::FUNCTYPE_PRIVATE)
            {
                return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "cannot get private property '%s' from instance", name->data());
            }
            val = stackPeek(0);
            bound = FuncBound::make(m_pvm, val, field->m_actualval.asFuncClosure());
            stackPop();
            stackPush(Value::fromObject(bound));
            return true;
        }
        return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "undefined property '%s'", name->data());
    }


    Value VM::getExceptionStacktrace()
    {
        int line;
        size_t i;
        size_t instruction;
        const char* fnname;
        const char* physfile;
        CallFrame* frame;
        FuncScript* function;
        String* os;
        Array* oa;
        oa = Array::make(m_pvm);
        {
            for(i = 0; i < m_framecount; i++)
            {
                Printer pd(m_pvm);
                frame = &m_framevalues[i];
                function = frame->closure->scriptfunc;
                /* -1 because the IP is sitting on the next instruction to be executed */
                instruction = frame->inscode - function->m_compiledblob->m_instrucs - 1;
                line = function->m_compiledblob->m_instrucs[instruction].srcline;
                physfile = "(unknown)";
                if(function->m_inmodule->m_physlocation != nullptr)
                {
                    if(function->m_inmodule->m_physlocation->m_sbuf != nullptr)
                    {
                        physfile = function->m_inmodule->m_physlocation->data();
                    }
                }
                fnname = "<script>";
                if(function->m_scriptfnname != nullptr)
                {
                    fnname = function->m_scriptfnname->data();
                }
                pd.putformat("from %s() in %s:%d", fnname, physfile, line);
                os = pd.takeString();
                oa->push(Value::fromObject(os));
                if((i > 15) && (m_pvm->m_conf.showfullstack == false))
                {
                    pd = Printer(m_pvm);
                    pd.putformat("(only upper 15 entries shown)");
                    os = pd.takeString();
                    oa->push(Value::fromObject(os));
                    break;
                }
            }
            return Value::fromObject(oa);
        }
        return Value::fromObject(String::copy(m_pvm, "", 0));
    }

    bool VM::exceptionHandleUncaught(ClassInstance* exception)
    {
        int i;
        int cnt;
        const char* colred;
        const char* colreset;
        Value stackitm;
        Property* field;
        String* emsg;
        Array* oa;
        Terminal nc;
        colred = nc.color('r');
        colreset = nc.color('0');
        /* at this point, the exception is unhandled; so, print it out. */
        fprintf(stderr, "%sunhandled %s%s", colred, exception->m_fromclass->m_classname->data(), colreset);    
        field = exception->m_properties->getByCStr("message");
        if(field != nullptr)
        {
            emsg = Value::toString(m_pvm, field->m_actualval);
            if(emsg->length() > 0)
            {
                fprintf(stderr, ": %s", emsg->data());
            }
            else
            {
                fprintf(stderr, ":");
            }
            fprintf(stderr, "\n");
        }
        else
        {
            fprintf(stderr, "\n");
        }
        field = exception->m_properties->getByCStr("stacktrace");
        if(field != nullptr)
        {
            fprintf(stderr, "  stacktrace:\n");
            oa = field->m_actualval.asArray();
            cnt = oa->size();
            i = cnt-1;
            if(cnt > 0)
            {
                while(true)
                {
                    stackitm = oa->at(i);
                    m_pvm->m_debugprinter->putformat("  ");
                    m_pvm->m_debugprinter->printValue(stackitm, false, true);
                    m_pvm->m_debugprinter->putformat("\n");
                    if(i == 0)
                    {
                        break;
                    }
                    i--;
                }
            }
        }
        return false;
    }        

    bool VM::exceptionPropagate()
    {
        int i;
        FuncScript* function;
        ExceptionFrame* handler;
        ClassInstance* exception;
        exception = stackPeek(0).asInstance();
        while(m_framecount > 0)
        {
            m_currentframe = &m_framevalues[m_framecount - 1];
            for(i = m_currentframe->handlercount; i > 0; i--)
            {
                handler = &m_currentframe->handlers[i - 1];
                function = m_currentframe->closure->scriptfunc;
                if(handler->address != 0 && ClassObject::instanceOf(exception->m_fromclass, handler->klass))
                {
                    m_currentframe->inscode = &function->m_compiledblob->m_instrucs[handler->address];
                    return true;
                }
                else if(handler->finallyaddress != 0)
                {
                    /* continue propagating once the 'finally' block completes */
                    stackPush(Value::makeBool(true));
                    m_currentframe->inscode = &function->m_compiledblob->m_instrucs[handler->finallyaddress];
                    return true;
                }
            }
            m_framecount--;
        }
        return exceptionHandleUncaught(exception);
    }

    void VM::gcDestroyLinkedObjects()
    {
        Object* next;
        Object* object;
        object = m_linkedobjects;
        if(object != nullptr)
        {
            while(object != nullptr)
            {
                next = object->m_nextobj;
                if(object != nullptr)
                {
                    Object::destroyObject(m_pvm, object);
                }
                object = next;
            }
        }
        Memory::osFree(m_gcgraystack);
        m_gcgraystack = nullptr;
    }

    void VM::gcTraceRefs()
    {
        Object* object;
        while(m_gcgraycount > 0)
        {
            object = m_gcgraystack[--m_gcgraycount];
            Object::blackenObject(m_pvm, object);
        }
    }

    void VM::gcMarkRoots()
    {
        int i;
        int j;
        Value* slot;
        ScopeUpvalue* upvalue;
        ExceptionFrame* handler;
        for(slot = m_stackvalues; slot < &m_stackvalues[m_stackidx]; slot++)
        {
            Object::markValue(m_pvm, *slot);
        }
        for(i = 0; i < (int)m_framecount; i++)
        {
            Object::markObject(m_pvm, static_cast<Object*>(m_framevalues[i].closure));
            for(j = 0; j < (int)m_framevalues[i].handlercount; j++)
            {
                handler = &m_framevalues[i].handlers[j];
                Object::markObject(m_pvm, static_cast<Object*>(handler->klass));
            }
        }
        for(upvalue = m_openupvalues; upvalue != nullptr; upvalue = upvalue->m_nextupval)
        {
            Object::markObject(m_pvm, static_cast<Object*>(upvalue));
        }
        HashTable::mark(m_pvm, m_pvm->m_definedglobals);
        HashTable::mark(m_pvm, m_pvm->m_loadedmodules);
        Object::markObject(m_pvm, static_cast<Object*>(m_pvm->m_exceptions.stdexception));
        m_pvm->gcMarkCompilerRoots();
    }

    void VM::gcSweep()
    {
        Object* object;
        Object* previous;
        Object* unreached;
        previous = nullptr;
        object = m_linkedobjects;
        while(object != nullptr)
        {
            if(object->m_mark == m_currentmarkvalue)
            {
                previous = object;
                object = object->m_nextobj;
            }
            else
            {
                unreached = object;
                object = object->m_nextobj;
                if(previous != nullptr)
                {
                    previous->m_nextobj = object;
                }
                else
                {
                    m_linkedobjects = object;
                }
                Object::destroyObject(m_pvm, unreached);
            }
        }
    }

    void VM::gcCollectGarbage()
    {
        size_t before;
        (void)before;
        #if defined(NEON_CFG_DEBUGGC) && NEON_CFG_DEBUGGC
        m_pvm->m_debugprinter->putformat("GC: gc begins\n");
        before = m_gcbytesallocated;
        #endif
        /*
        //  REMOVE THE NEXT LINE TO DISABLE NESTED gcCollectGarbage() POSSIBILITY!
        */
        #if 0
        m_gcnextgc = m_gcbytesallocated;
        #endif
        gcMarkRoots();
        gcTraceRefs();
        HashTable::removeMarked(m_pvm, m_pvm->m_cachedstrings);
        HashTable::removeMarked(m_pvm, m_pvm->m_loadedmodules);
        gcSweep();
        m_gcnextgc = m_gcbytesallocated * NEON_CFG_GCHEAPGROWTHFACTOR;
        m_currentmarkvalue = !m_currentmarkvalue;
        #if defined(NEON_CFG_DEBUGGC) && NEON_CFG_DEBUGGC
        m_pvm->m_debugprinter->putformat("GC: gc ends\n");
        m_pvm->m_debugprinter->putformat("GC: collected %zu bytes (from %zu to %zu), next at %zu\n", before - m_gcbytesallocated, before, m_gcbytesallocated, m_gcnextgc);
        #endif
    }
