
    bool Blob::serializeValObjectToStream(State* state, FILE* hnd, const Value& val)
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
                    uint64_t len = os->length();
                    fwrite(&len, sizeof(uint64_t), 1, hnd);
                    fwrite(os->data(), sizeof(char), os->length(), hnd);
                }
                break;
        }
    }


    bool Blob::deserializeValObjectFromStream(State* state, FILE* hnd, Value::ObjType ot, Value& dest)
    {
        size_t rdlen;
        switch(ot)
        {
            case Value::OBJTYPE_STRING:
                {
                    uint64_t length;
                    char* data;
                    if(!readFrom<uint64_t>("String.length", hnd, &length, 1))
                    {
                        return false;
                    }
                    data = (char*)malloc(length+1);
                    //if(!readFrom<char>("String.data", hnd, data, length))
                    rdlen = fread(data, sizeof(char), length, hnd);
                    if(rdlen != length)
                    {
                        fprintf(stderr, "failed to read string data: got %zd, expected %zd\n", rdlen, length);
                        return false;
                    }
                    dest = Value::fromObject(String::take(state, data, length));
                    return true;
                }
                break;
            default:
                {
                    fprintf(stderr, "unhandled object type '%s' for deserializing", Value::typenameFromEnum(ot));
                    return false;
                }
                break;
        }
        return false;
    }

