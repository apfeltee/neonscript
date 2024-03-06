
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

            static bool serializeInstructionToStream(State* state, const Util::AnyStream& hnd, const Instruction& inst)
            {
                (void)state;
                /*
                    bool isop;
                    uint8_t code;
                    int srcline;
                */
                hnd.put(&inst.isop, 1);
                hnd.put(&inst.code, 1);
                hnd.put(&inst.srcline, 1);
                return true;
            }


            template<typename Type>
            static bool readFrom(const char* name, const Util::AnyStream& hnd, Type* dest, size_t count)
            {
                size_t rdlen;
                rdlen = hnd.read(dest, sizeof(Type), 1);
                if(rdlen != count)
                {
                    fprintf(stderr, "failed to read %zd items of %s (got %zd, expected %zd)\n", count, name, rdlen, count);
                    return false;
                }
                return true;
            }

            static bool deserializeInstructionFromStream(State* state, const Util::AnyStream& hnd, Instruction& dest)
            {
                (void)state;
                if(!readFrom<decltype(Instruction::isop)>("Instruction::isop", hnd, &dest.isop, 1))
                {
                    return false;
                }
                if(!readFrom<decltype(Instruction::code)>("Instruction::code", hnd, &dest.code, 1))
                {
                    return false;
                }
                if(!readFrom<decltype(Instruction::srcline)>("Instruction::srcline", hnd, &dest.srcline, 1))
                {
                    return false;
                }
                return true;
            }

            static bool serializeValueToStream(State* state, const Util::AnyStream& hnd, const Value& val)
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
                hnd.put(&val.m_valtype, 1);
                if(val.m_valtype == Value::VALTYPE_OBJECT)
                {
                    auto ob = val.objectType();
                    hnd.put(&ob, 1);
                }
                else
                {
                    Value::ObjType dummy = Value::OBJTYPE_INVALID;
                    hnd.put(&dummy, 1);
                }
                switch(val.m_valtype)
                {
                    case Value::VALTYPE_EMPTY:
                    case Value::VALTYPE_NULL:
                        {
                            hnd.put(&nothing, 1);
                        }
                        break;
                    case Value::VALTYPE_NUMBER:
                        {
                            hnd.put(&val.m_valunion.number, 1);
                        }
                        break;
                    case Value::VALTYPE_BOOL:
                        {
                            hnd.put(&val.m_valunion.boolean, 1);
                        }
                        break;
                    case Value::VALTYPE_OBJECT:
                        {
                            serializeValObjectToStream(state, hnd, val);
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
            static bool serializeValObjectToStream(State* state, const Util::AnyStream& hnd, const Value& val);
            static bool deserializeValObjectFromStream(State* state, const Util::AnyStream& hnd, Value::ObjType ot, Value& dest);

            static bool deserializeValueFromStream(State* state, const Util::AnyStream& hnd, Value& dest)
            {
                const char* tn;
                Value::ObjType ot;
                if(!readFrom<decltype(Value::m_valtype)>("Value::m_valtype", hnd, &dest.m_valtype, 1))
                {
                    return false;
                }
                if(!readFrom<Value::ObjType>("objectType", hnd, &ot, 1))
                {
                    return false;
                }
                switch(dest.m_valtype)
                {
                    case Value::VALTYPE_EMPTY:
                    case Value::VALTYPE_NULL:
                        {
                            hnd.get();
                        }
                        break;
                    case Value::VALTYPE_NUMBER:
                        {
                            if(!readFrom<decltype(Value::m_valunion.number)>("Value::m_valunion.number", hnd, &dest.m_valunion.number, 1))
                            {
                                return false;
                            }
                        }
                        break;
                    case Value::VALTYPE_BOOL:
                        {
                            if(!readFrom<decltype(Value::m_valunion.boolean)>("Value::m_valunion.boolean", hnd, &dest.m_valunion.boolean, 1))
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
                            if(!deserializeValObjectFromStream(state, hnd, ot, dest))
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

            void binToStream(const Util::AnyStream& hnd)
            {
                uint64_t i;
                {
                    hnd.put(&m_count, 1);
                    for(i=0; i<m_count; i++)
                    {
                        serializeInstructionToStream(m_pvm, hnd, m_instrucs[i]);
                    }
                }
                {
                    hnd.put(&m_constants->m_count, 1);
                    for(i=0; i<m_constants->m_count; i++)
                    {
                        serializeValueToStream(m_pvm, hnd, m_constants->m_values[i]);
                    }
                }
                {
                }
            }

            void toStream(const Util::AnyStream& hnd)
            {
                hnd.put('N');
                hnd.put('B');
                hnd.put('\b');
                binToStream(hnd);
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
                //if(checkhdr)
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
                if(!readFrom<uint64_t>("instruction count", hnd, &inscnt, 1))
                {
                    fprintf(stderr, "failed to read instruction count. got %zd\n", inscnt);
                    return false;
                }
                cnt = 0;
                while(cnt != inscnt)
                {
                    if(!deserializeInstructionFromStream(m_pvm, hnd, ins))
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
                if(!readFrom<uint64_t>("constant count", hnd, &constcnt, 1))
                {
                    return false;
                }
                fprintf(stderr, "binary contains %zd constants\n", constcnt);
                while(cnt != constcnt)
                {
                    //if(!readFrom<Value>("constant", hnd, &cval, 1))
                    if(!deserializeValueFromStream(m_pvm, hnd, cval))
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
