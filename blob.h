
    /*
    * the (de-)Serializer.
    * note: no attempt is made to check for endianness. this class
    * assumes x64. Might just work on other processors, but don't count on it.
    */
    struct Serializer
    {
        public:
            static constexpr bool isLittleEndian()
            {
                return std::endian::native == std::endian::little;
            }

        public:
            State* m_pvm;
            const Util::AnyStream& m_io;

        public:
            Serializer(State* state, const Util::AnyStream& io): m_pvm(state), m_io(io)
            {
            }

            /*
            * attempts to read a <Type> (of size of that type) of count $count.
            * functionally equiv to just calling fread(), except
            * looking slightly nicer - and also auto-complains
            * when it failed.
            *
            * if read succeeded, returns true.
            */
            template<typename Type>
            bool readDirect(const char* name, Type* dest, size_t count)
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

            /*
            * puts a <Type> (of size of that type), of count $count.
            * will happily read/from to references, and it does
            * not check if $thing is a reference.
            */
            template<typename Type>
            size_t fhput(const Type* thing, size_t count)
            {
                return m_io.put(thing, count);
            }

            /*
            * serializes a String.
            */
            bool putString(String* os);

            /*
            * reads a String object into $os.
            * param $os does not need to be allocated (in fact, should not be!)
            */
            bool readString(String** dest);

            /*
            * reads a FuncScript into $dest. same as readString; $dest should
            * not be allocated, because it will be overwritten
            */
            bool readFuncScript(FuncScript** dest);

            /*
            * serializes a FuncScript.
            */
            bool putFuncScript(FuncScript* fn);

            bool putInstruction(const Instruction& inst)
            {
                m_io.put(&inst.code, 1);
                //m_io.put(&inst.isop, 1);
                //m_io.put(&inst.srcline, 1);
                return true;
            }

            bool readInstruction(Instruction& dest)
            {
                dest.srcline = 0;
                dest.isop = false;
                if(!readDirect<decltype(Instruction::code)>("Instruction::code", &dest.code, 1))
                {
                    return false;
                }
                /*
                if(!readDirect<decltype(Instruction::isop)>("Instruction::isop", &dest.isop, 1))
                {
                    return false;
                }
                if(!readDirect<decltype(Instruction::srcline)>("Instruction::srcline", &dest.srcline, 1))
                {
                    return false;
                }
                */
                return true;
            }

            bool putValue(const Value& val)
            {
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
                            // these two are dummies, but they still occupy a bit of space.
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
                            putObject(val);
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

            bool putObject(const Value& val);
            bool readObject(Value::ObjType ot, Value& dest);

            bool readValue(Value& dest)
            {
                const char* tn;
                Value::ObjType ot;
                if(!readDirect<decltype(Value::m_valtype)>("Value::m_valtype", &dest.m_valtype, 1))
                {
                    return false;
                }
                if(!readDirect<Value::ObjType>("objectType", &ot, 1))
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
                            if(!readDirect<decltype(Value::m_valunion.number)>("Value::m_valunion.number", &dest.m_valunion.number, 1))
                            {
                                return false;
                            }
                        }
                        break;
                    case Value::VALTYPE_BOOL:
                        {
                            if(!readDirect<decltype(Value::m_valunion.boolean)>("Value::m_valunion.boolean", &dest.m_valunion.boolean, 1))
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
                            if(!readObject(ot, dest))
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

            bool readBlob(Blob* toblob, bool checkhdr);

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
                        ser.putInstruction(m_instrucs[i]);
                    }
                }
                {
                    ser.m_io.put(&m_constants->m_count, 1);
                    for(i=0; i<m_constants->m_count; i++)
                    {
                        ser.putValue(m_constants->m_values[i]);
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
                Serializer ser(m_pvm, hnd);
                return ser.readBlob(this, checkhdr);
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
