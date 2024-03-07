

    void Serializer::putBlob(Blob* blob)
    {
        uint64_t i;
        uint64_t cnt;
        {
            cnt = blob->m_count;
            putArrayBegin("instructions", cnt);
            for(i=0; i<blob->m_count; i++)
            {
                putInstruction(blob->m_instrucs[i]);
            }
            putArrayEnd();
        }
        {
            cnt = blob->m_constants->m_count;
            putArrayBegin("constants", cnt);
            for(i=0; i<blob->m_constants->m_count; i++)
            {
                putValue(blob->m_constants->m_values[i]);
            }
            putArrayEnd();
        }
    }

    bool Serializer::putString(String* os)
    {
        uint64_t len;
        len = os->length();
        streamPut<uint64_t>(&len, 1);
        streamPut<char>(os->data(), os->length());
        return true;
    }

    bool Deserializer::readString(String** dest)
    {
        uint64_t length;
        char* data;
        /*
        * first, collect length...
        */
        if(!streamRead<uint64_t>("String.length", &length, 1))
        {
            return false;
        }
        // allocate that
        data = (char*)malloc(length+1);
        if(!m_io.read(data, sizeof(char), length))
        {
            fprintf(stderr, "failed to read string data\n");
            return false;
        }
        // then construct the string. important: don't copy!
        // it is free'd by the GC.
        *dest = String::take(m_pvm, data, length);
        return true;
    }

    bool Deserializer::readFuncScript(FuncScript** dest)
    {
        *dest = FuncScript::make(m_pvm, m_pvm->m_toplevelmodule, FuncCommon::FUNCTYPE_FUNCTION);
        if(!streamRead<FuncCommon::Type>("m_functype", &(*dest)->m_functype, 1))
        {
            return false;
        }
        if(!streamRead<int>("m_arity", &(*dest)->m_arity, 1))
        {
            return false;
        }
        if(!streamRead<int>("m_upvalcount", &(*dest)->m_upvalcount, 1))
        {
            return false;
        }
        if(!streamRead<bool>("m_isvariadic", &(*dest)->m_isvariadic, 1))
        {
            return false;
        }
        if(!readString(&(*dest)->m_scriptfnname))
        {
            return false;
        }
        fprintf(stderr, "deserializing function '%s' (type=%d arity=%d upvalcount=%d isvariadic=%d)\n", (*dest)->m_scriptfnname->data(), (*dest)->m_functype, (*dest)->m_arity, (*dest)->m_upvalcount, (*dest)->m_isvariadic);
        return readBlob((*dest)->m_compiledblob, false);
    }

    bool Serializer::putFuncScript(FuncScript* fn)
    {
        streamPut<FuncCommon::Type>(&fn->m_functype, 1);
        streamPut<int>(&fn->m_arity, 1);
        streamPut<int>(&fn->m_upvalcount, 1);
        streamPut<bool>(&fn->m_isvariadic, 1);
        putString(fn->m_scriptfnname);
        putBlob(fn->m_compiledblob);
        return true;
    }

    /*
        enum ObjType
        {
            OBJTYPE_STRING,
            OBJTYPE_RANGE,
            OBJTYPE_ARRAY,
            OBJTYPE_DICT,
            OBJTYPE_FILE,
            OBJTYPE_UPVALUE,
            OBJTYPE_FUNCBOUND,
            OBJTYPE_FUNCCLOSURE,
            OBJTYPE_FUNCSCRIPT,
            OBJTYPE_INSTANCE,
            OBJTYPE_FUNCNATIVE,
            OBJTYPE_CLASS,
            OBJTYPE_MODULE,
            OBJTYPE_SWITCH,
            OBJTYPE_USERDATA,
        };
    */
    bool Serializer::putObject(const Value& val)
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
            case Value::OBJTYPE_FUNCCLOSURE:
                {
                    auto fn = val.asFuncClosure();
                    return putFuncScript(fn->m_scriptfunc);
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

    bool Deserializer::readObject(Value::ObjType ot, Value& dest)
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

    bool Deserializer::readBlob(Blob* toblob, bool checkhdr)
    {
        int chn;
        int chb;
        int chm;
        uint64_t cnt;
        uint64_t inscnt;
        uint64_t constcnt; 
        Instruction ins;
        Value cval;
        if(checkhdr)
        {
            chn = streamGetOne<char>(-1);
            chb = streamGetOne<char>(-1);
            chm = streamGetOne<char>(-1);
            if((chn != 'N') && (chb != 'B') && (chm != '\b'))
            {
                fprintf(stderr, "error: bad header (got '%c' '%c' '%c')\n", chn, chb, chm);
                return false;
            }
        }
        inscnt = 0;
        if(!streamRead<uint64_t>("instruction count", &inscnt, 1))
        {
            fprintf(stderr, "failed to read instruction count. got %zd\n", inscnt);
            return false;
        }
        cnt = 0;
        while(cnt != inscnt)
        {
            if(!readInstruction(ins))
            {
                fprintf(stderr, "error: failed to read instruction\n");
                return false;
            }
            else
            {
            }
            cnt++;
            toblob->push(ins);
        }
        fprintf(stderr, "successfully read %zd of %zd instructions!\n", cnt, inscnt);
        cnt = 0;
        constcnt = 0;
        if(!streamRead<uint64_t>("constant count", &constcnt, 1))
        {
            return false;
        }
        fprintf(stderr, "binary contains %zd constants\n", constcnt);
        while(cnt != constcnt)
        {
            if(!readValue(cval))
            {
                fprintf(stderr, "error: failed to read constant\n");
                return false;
            }
            else
            {
            }
            cnt++;
            toblob->pushConst(cval);
        }
        fprintf(stderr, "successfully read %zd of %zd constants!\n", cnt, constcnt);

        return true;
    }


