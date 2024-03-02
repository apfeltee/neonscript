
    template<typename Type>
    size_t fhput(const AnyStream& fh, const Type* thing, size_t count)
    {
        return fh.put(thing, count);
    }

    bool putString(State* state, const AnyStream& hnd, String* os)
    {
        uint64_t len = os->length();
        fhput<uint64_t>(hnd, &len, 1);
        fhput<char>(hnd, os->data(), os->length());
        return true;
    }

    bool readString(State* state, const AnyStream& hnd, String** dest)
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

    bool putFuncScript(State* state, const AnyStream& hnd, FuncScript* fn)
    {
                    /*
                    int m_upvalcount;
                    bool m_isvariadic;
                    Blob* m_compiledblob;
                    String* m_scriptfnname;
                    Module* m_inmodule;
                    */
        fhput<int>(hnd, &fn->m_upvalcount, 1);
        fhput<bool>(hnd, &fn->m_isvariadic, 1);
        putString(state, hnd, fn->m_scriptfnname);
        fn->m_compiledblob->binToStream(hnd);
        return true;
    }


    bool Blob::serializeValObjectToStream(State* state, const AnyStream& hnd, const Value& val)
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

    bool Blob::deserializeValObjectFromStream(State* state, const AnyStream& hnd, Value::ObjType ot, Value& dest)
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
            default:
                {
                    fprintf(stderr, "unhandled object type '%s' for deserializing", Value::typenameFromEnum(ot));
                }
                break;
        }
        return false;
    }

