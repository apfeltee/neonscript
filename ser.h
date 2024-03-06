
            bool Serializer::putString(String* os)
            {
                uint64_t len;
                len = os->length();
                fhput<uint64_t>(&len, 1);
                fhput<char>(os->data(), os->length());
                return true;
            }

            bool Serializer::readString(String** dest)
            {
                uint64_t length;
                char* data;
                if(!readFrom<uint64_t>("String.length", &length, 1))
                {
                    return false;
                }
                data = (char*)malloc(length+1);
                if(!m_io.read(data, sizeof(char), length))
                {
                    fprintf(stderr, "failed to read string data\n");
                    return false;
                }
                *dest = String::take(m_pvm, data, length);
                return true;
            }

            bool Serializer::readFuncScript(FuncScript** dest)
            {
                *dest = FuncScript::make(m_pvm, m_pvm->m_toplevelmodule, FuncCommon::FUNCTYPE_FUNCTION);
                if(!readFrom<int>("m_arity", &(*dest)->m_arity, 1))
                {
                    return false;
                }
                if(!readFrom<int>("m_upvalcount", &(*dest)->m_upvalcount, 1))
                {
                    return false;
                }
                if(!readFrom<bool>("m_isvariadic", &(*dest)->m_isvariadic, 1))
                {
                    return false;
                }
                if(!readString(&(*dest)->m_scriptfnname))
                {
                    return false;
                }
                return (*dest)->m_compiledblob->fromHandle(m_io, false);
            }

            bool Serializer::putFuncScript(FuncScript* fn)
            {
                            /*
                            int m_upvalcount;
                            bool m_isvariadic;
                            Blob* m_compiledblob;
                            String* m_scriptfnname;
                            Module* m_inmodule;
                            */
                fhput<int>(&fn->m_arity, 1);
                fhput<int>(&fn->m_upvalcount, 1);
                fhput<bool>(&fn->m_isvariadic, 1);
                putString(fn->m_scriptfnname);
                fn->m_compiledblob->binToStream(*this);
                return true;
            }


    bool Serializer::putValObjectToStream(const Value& val)
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
                    return putString(os);
                }
                break;
            case Value::OBJTYPE_FUNCSCRIPT:
                {
                    auto fn = val.asFuncScript();
                    return putFuncScript(fn);
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

    bool Serializer::readValObjectFromStream(Value::ObjType ot, Value& dest)
    {
        switch(ot)
        {
            case Value::OBJTYPE_STRING:
                {
                    String* os;
                    if(!readString(&os))
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
                    if(!readFuncScript(&fn))
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

