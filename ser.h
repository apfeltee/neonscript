
    template<typename Type>
    size_t fhput(const Util::AnyStream& fh, const Type* thing, size_t count)
    {
        return fh.put(thing, count);
    }

    bool putString(State* state, const Util::AnyStream& hnd, String* os)
    {
        uint64_t len;
        (void)state;
        len = os->length();
        fhput<uint64_t>(hnd, &len, 1);
        fhput<char>(hnd, os->data(), os->length());
        return true;
    }

    bool readString(State* state, const Util::AnyStream& hnd, String** dest)
    {
        uint64_t length;
        char* data;
        if(!Blob::readFrom<uint64_t>("String.length", hnd, &length, 1))
        {
            return false;
        }
        data = (char*)malloc(length+1);
        if(!hnd.read(data, sizeof(char), length))
        {
            fprintf(stderr, "failed to read string data\n");
            return false;
        }
        *dest = String::take(state, data, length);
        return true;
    }

    bool readFuncScript(State* state, const Util::AnyStream& hnd, FuncScript** dest)
    {
        *dest = FuncScript::make(state, state->m_toplevelmodule, FuncCommon::FUNCTYPE_FUNCTION);
        if(!Blob::readFrom<int>("m_arity", hnd, &(*dest)->m_arity, 1))
        {
            return false;
        }
        if(!Blob::readFrom<int>("m_upvalcount", hnd, &(*dest)->m_upvalcount, 1))
        {
            return false;
        }
        if(!Blob::readFrom<bool>("m_isvariadic", hnd, &(*dest)->m_isvariadic, 1))
        {
            return false;
        }
        if(!readString(state, hnd, &(*dest)->m_scriptfnname))
        {
            return false;
        }
        return (*dest)->m_compiledblob->fromHandle(hnd, false);
    }

    bool putFuncScript(State* state, const Util::AnyStream& hnd, FuncScript* fn)
    {
                    /*
                    int m_upvalcount;
                    bool m_isvariadic;
                    Blob* m_compiledblob;
                    String* m_scriptfnname;
                    Module* m_inmodule;
                    */
        fhput<int>(hnd, &fn->m_arity, 1);
        fhput<int>(hnd, &fn->m_upvalcount, 1);
        fhput<bool>(hnd, &fn->m_isvariadic, 1);
        putString(state, hnd, fn->m_scriptfnname);
        fn->m_compiledblob->toStream(hnd);
        return true;
    }


    bool Blob::serializeValObjectToStream(State* state, const Util::AnyStream& hnd, const Value& val)
    {
        switch(val.objectType())
        {
            /*
            case Value::OBJTYPE_STRING:
                {
                }
                break;
            */
            case Value::OBJTYPE_STRING:
                {
                    auto os = val.asString();
                    return putString(state, hnd, os);
                }
                break;
            case Value::OBJTYPE_FUNCSCRIPT:
                {
                    auto fn = val.asFuncScript();
                    return putFuncScript(state, hnd, fn);
                }
                break;
            default:
                {
                    fprintf(stderr, "unhandled object type '%s' for serialization\n", val.name());
                }
                break;
        }
        return false;
    }

    bool Blob::deserializeValObjectFromStream(State* state, const Util::AnyStream& hnd, Value::ObjType ot, Value& dest)
    {
        switch(ot)
        {
            case Value::OBJTYPE_STRING:
                {
                    String* os;
                    if(!readString(state, hnd, &os))
                    {
                        return false;
                    }
                    dest = Value::fromObject(os);
                    return true;
                }
                break;
            case Value::OBJTYPE_FUNCSCRIPT:
                {
                    FuncScript* fn;
                    if(!readFuncScript(state, hnd, &fn))
                    {
                        return false;
                    }
                    dest = Value::fromObject(fn);
                    return true;
                }
                break;
            default:
                {
                    fprintf(stderr, "unhandled object type '%s' for deserializing", Value::typenameFromEnum(ot));
                }
                break;
        }
        return false;
    }

