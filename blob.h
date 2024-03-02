
    struct AnyStream
    {
        public:
            enum Type
            {
                REC_INVALID,
                REC_CFILE,
                REC_CPPSTREAM,
                REC_STRING,
            };

            union RecUnion
            {
                Util::StrBuffer* strbuf;
                FILE* handle;
                std::ostream* ostrm;
            };

        private:
            Type m_type = REC_INVALID;
            RecUnion m_recs;

        private:
            template<typename ItemT>
            bool putToString(const ItemT* thing, size_t count) const
            {
                m_recs.strbuf->append((const char*)thing, count);
                return true;
            }

            template<typename ItemT>
            bool putToCFile(const ItemT* thing, size_t count) const
            {
                fwrite(thing, sizeof(ItemT), count, m_recs.handle);
                return true;
            }

            template<typename ItemT>
            bool putToCPPStream(const ItemT* thing, size_t count) const
            {
                m_recs.ostrm->write((const char*)thing, count);
                return true;
            }

            bool putOneToCFile(int b) const
            {
                fputc(b, m_recs.handle);
                return true;
            }

            template<typename ItemT>
            bool readFromCFile(ItemT* target, size_t tsz, size_t count) const
            {
                size_t rd;
                rd = fread(target, tsz, count, m_recs.handle);
                if(rd == count)
                {
                    return true;
                }
                return false;
            }

            int readOneFromCFile() const
            {
                return fgetc(m_recs.handle);
            }


        public:
            AnyStream()
            {
            }

            AnyStream(FILE* hnd): m_type(REC_CFILE)
            {
                m_recs.handle = hnd;
            }

            AnyStream(std::ostream& os): m_type(REC_CPPSTREAM)
            {
                m_recs.ostrm = &os;
            }

            AnyStream(Util::StrBuffer& os): m_type(REC_STRING)
            {
                m_recs.strbuf = &os;
            }

            template<typename ItemT>
            bool put(const ItemT* thing, size_t count) const
            {
                switch(m_type)
                {
                    case REC_CFILE:
                        return putToCFile(thing, count);
                    case REC_CPPSTREAM:
                        return putToCPPStream(thing, count);
                    case REC_STRING:
                        return putToString(thing, count);
                    default:
                        break;
                }
                return false;
            }

            bool putOne(int b) const
            {
                switch(m_type)
                {
                    case REC_CFILE:
                        return putOneToCFile(b);
                    default:
                        break;
                }
                return false;
            }

            template<typename ItemT>
            bool read(ItemT* target, size_t tsz, size_t count) const
            {
                switch(m_type)
                {
                    case REC_CFILE:
                        return readFromCFile(target, tsz, count);
                    default:
                        break;
                }
                return false;
            }

            int readOne() const
            {
                switch(m_type)
                {
                    case REC_CFILE:
                        return readOneFromCFile();
                    default:
                        break;
                }
                return -1;
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

            static bool serializeInstructionToStream(State* state, const AnyStream& hnd, const Instruction& inst)
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
            static bool readFrom(const char* name, const AnyStream& hnd, Type* dest, size_t count)
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

            static bool deserializeInstructionFromStream(State* state, const AnyStream& hnd, Instruction& dest)
            {
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

            static bool serializeValueToStream(State* state, const AnyStream& hnd, const Value& val)
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
            static bool serializeValObjectToStream(State* state, const AnyStream& hnd, const Value& val);
            static bool deserializeValObjectFromStream(State* state, const AnyStream& hnd, Value::ObjType ot, Value& dest);

            static bool deserializeValueFromStream(State* state, const AnyStream& hnd, Value& dest)
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
                            hnd.readOne();
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

            void binToStream(const AnyStream& hnd)
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

            void toStream(const AnyStream& hnd)
            {
                hnd.putOne('N');
                hnd.putOne('B');
                hnd.putOne('\b');
                binToStream(hnd);
            }

            bool fromHandle(const AnyStream& hnd)
            {
                int chn;
                int chb;
                int chm;
                uint64_t cnt;
                uint64_t inscnt;
                uint64_t constcnt; 
                Instruction ins;
                Value cval;
                chn = hnd.readOne();
                chb = hnd.readOne();
                chm = hnd.readOne();
                if((chn != 'N') && (chb != 'B') && (chm != '\b'))
                {
                    fprintf(stderr, "error: bad header\n");
                    return false;
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
