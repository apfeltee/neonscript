
    struct Serializer
    {
        public:
            State* m_pvm;
            const Util::AnyStream& m_io;

        public:
            Serializer(State* state, const Util::AnyStream& io): m_pvm(state), m_io(io)
            {
            }

            template<typename Type>
            bool readFrom(const char* name, Type* dest, size_t count)
            {
                size_t rdlen;
                rdlen = m_io.read(dest, sizeof(Type), 1);
                if(rdlen != count)
                {
                    fprintf(stderr, "failed to read %zd items of %s (got %zd, expected %zd)\n", count, name, rdlen, count);
                    return false;
                }
                return true;
            }

            template<typename Type>
            size_t fhput(const Type* thing, size_t count)
            {
                return m_io.put(thing, count);
            }

            bool putString(String* os);
            bool readString(String** dest);
            bool readFuncScript(FuncScript** dest);
            bool putFuncScript(FuncScript* fn);

            bool serializeInstructionToStream(const Instruction& inst)
            {
                /*
                    bool isop;
                    uint8_t code;
                    int srcline;
                */
                m_io.put(&inst.isop, 1);
                m_io.put(&inst.code, 1);
                m_io.put(&inst.srcline, 1);
                return true;
            }

            bool readInstructionFromStream(Instruction& dest)
            {
                if(!readFrom<decltype(Instruction::isop)>("Instruction::isop", &dest.isop, 1))
                {
                    return false;
                }
                if(!readFrom<decltype(Instruction::code)>("Instruction::code", &dest.code, 1))
                {
                    return false;
                }
                if(!readFrom<decltype(Instruction::srcline)>("Instruction::srcline", &dest.srcline, 1))
                {
                    return false;
                }
                return true;
            }

            bool putValueToStream(const Value& val)
            {
                /*
                    ValType m_valtype;
                    ValUnion m_valunion;
                    union ValUnion
                    {
                        bool boolean;
                        double number;
                        Object* obj;
                    };
                    enum ValType
                    {
                        VALTYPE_EMPTY,
                        VALTYPE_NULL,
                        VALTYPE_BOOL,
                        VALTYPE_NUMBER,
                        VALTYPE_OBJECT,
                    };
                */
                char nothing = 0;
                m_io.put(&val.m_valtype, 1);
                if(val.m_valtype == Value::VALTYPE_OBJECT)
                {
                    auto ob = val.objectType();
                    m_io.put(&ob, 1);
                }
                else
                {
                    Value::ObjType dummy = Value::OBJTYPE_INVALID;
                    m_io.put(&dummy, 1);
                }
                switch(val.m_valtype)
                {
                    case Value::VALTYPE_EMPTY:
                    case Value::VALTYPE_NULL:
                        {
                            m_io.put(&nothing, 1);
                        }
                        break;
                    case Value::VALTYPE_NUMBER:
                        {
                            m_io.put(&val.m_valunion.number, 1);
                        }
                        break;
                    case Value::VALTYPE_BOOL:
                        {
                            m_io.put(&val.m_valunion.boolean, 1);
                        }
                        break;
                    case Value::VALTYPE_OBJECT:
                        {
                            putValObjectToStream(val);
                        }
                        break;
                    default:
                        {
                            fprintf(stderr, "should not happen - trying to serialize something beyond VALTYPE_OBJECT!");
                        }
                        break;
                }
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
            bool putValObjectToStream(const Value& val);
            bool readValObjectFromStream(Value::ObjType ot, Value& dest);

            bool readValueFromStream(Value& dest)
            {
                const char* tn;
                Value::ObjType ot;
                if(!readFrom<decltype(Value::m_valtype)>("Value::m_valtype", &dest.m_valtype, 1))
                {
                    return false;
                }
                if(!readFrom<Value::ObjType>("objectType", &ot, 1))
                {
                    return false;
                }
                switch(dest.m_valtype)
                {
                    case Value::VALTYPE_EMPTY:
                    case Value::VALTYPE_NULL:
                        {
                            m_io.get();
                        }
                        break;
                    case Value::VALTYPE_NUMBER:
                        {
                            if(!readFrom<decltype(Value::m_valunion.number)>("Value::m_valunion.number", &dest.m_valunion.number, 1))
                            {
                                return false;
                            }
                        }
                        break;
                    case Value::VALTYPE_BOOL:
                        {
                            if(!readFrom<decltype(Value::m_valunion.boolean)>("Value::m_valunion.boolean", &dest.m_valunion.boolean, 1))
                            {
                                return false;
                            }
                        }
                        break;
                    case Value::VALTYPE_OBJECT:
                        {
                            if(ot == Value::OBJTYPE_INVALID)
                            {
                                fprintf(stderr, "bad object type (OBJTYPE_INVALID)");
                                return false;
                            }
                            if(!readValObjectFromStream(ot, dest))
                            {
                                return false;
                            }
                        }
                        break;
                    default:
                        {
                            tn = Value::typenameFromEnum(dest.m_valtype);
                            if(dest.m_valtype == Value::VALTYPE_OBJECT)
                            {
                                tn = Value::typenameFromEnum(ot);
                            }
                            fprintf(stderr, "unhandled type '%s'\n", tn);
                        }
                }
                return true;
            }


    };

    struct Blob
    {
        public:
            State* m_pvm;
            uint64_t m_count;
            uint64_t m_capacity;
            Instruction* m_instrucs;
            ValArray* m_constants;
            ValArray* m_argdefvals;

        public:
            Blob(State* state): m_pvm(state)
            {
                m_count = 0;
                m_capacity = 0;
                m_instrucs = nullptr;
                m_constants = Memory::create<ValArray>();
                m_argdefvals = Memory::create<ValArray>();
            }

            ~Blob()
            {
                if(m_instrucs != nullptr)
                {
                    Memory::freeArray(m_instrucs, m_capacity);
                }
                Memory::destroy(m_constants);
                Memory::destroy(m_argdefvals);
            }


            void binToStream(Serializer& ser)
            {
                uint64_t i;
                {
                    ser.m_io.put(&m_count, 1);
                    for(i=0; i<m_count; i++)
                    {
                        ser.serializeInstructionToStream(m_instrucs[i]);
                    }
                }
                {
                    ser.m_io.put(&m_constants->m_count, 1);
                    for(i=0; i<m_constants->m_count; i++)
                    {
                        ser.putValueToStream(m_constants->m_values[i]);
                    }
                }
                {
                }
            }

            void toStream(const Util::AnyStream& hnd)
            {
                Serializer ser(m_pvm, hnd);
                ser.m_io.put('N');
                ser.m_io.put('B');
                ser.m_io.put('\b');
                binToStream(ser);
            }

            bool fromHandle(const Util::AnyStream& hnd, bool checkhdr)
            {
                int chn;
                int chb;
                int chm;
                uint64_t cnt;
                uint64_t inscnt;
                uint64_t constcnt; 
                Instruction ins;
                Value cval;
                Serializer ser(m_pvm, hnd);
                if(checkhdr)
                {
                    chn = hnd.get();
                    chb = hnd.get();
                    chm = hnd.get();
                    if((chn != 'N') && (chb != 'B') && (chm != '\b'))
                    {
                        fprintf(stderr, "error: bad header (got '%c' '%c' '%c')\n", chn, chb, chm);
                        return false;
                    }
                }
                inscnt = 0;
                if(!ser.readFrom<uint64_t>("instruction count", &inscnt, 1))
                {
                    fprintf(stderr, "failed to read instruction count. got %zd\n", inscnt);
                    return false;
                }
                cnt = 0;
                while(cnt != inscnt)
                {
                    if(!ser.readInstructionFromStream(ins))
                    {
                        fprintf(stderr, "error: failed to read instruction\n");
                        return false;
                    }
                    else
                    {
                        fprintf(stderr, "read instruction %zd of %zd ...\n", cnt, inscnt);
                    }
                    cnt++;
                    push(ins);
                }
                cnt = 0;
                constcnt = 0;
                if(!ser.readFrom<uint64_t>("constant count", &constcnt, 1))
                {
                    return false;
                }
                fprintf(stderr, "binary contains %zd constants\n", constcnt);
                while(cnt != constcnt)
                {
                    //if(!ser.readFrom<Value>("constant", &cval, 1))
                    if(!ser.readValueFromStream(cval))
                    {
                        fprintf(stderr, "error: failed to read constant\n");
                        return false;
                    }
                    else
                    {
                        fprintf(stderr, "read constant %zd of %zd ...\n", cnt, constcnt);
                    }
                    cnt++;
                    pushConst(cval);
                }
                return true;
            }

            void push(Instruction ins)
            {
                uint64_t oldcapacity;
                if(m_capacity < m_count + 1)
                {
                    oldcapacity = m_capacity;
                    m_capacity = Util::growCapacity(oldcapacity);
                    m_instrucs = Memory::growArray(m_instrucs, oldcapacity, m_capacity);
                }
                m_instrucs[m_count] = ins;
                m_count++;
            }

            int pushConst(Value value)
            {
                m_constants->push(value);
                return m_constants->m_count - 1;
            }

            int pushArgDefault(Value value)
            {
                m_argdefvals->push(value);
                return m_argdefvals->m_count - 1;
            }
    };
