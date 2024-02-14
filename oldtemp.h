


    struct FormatInfo
    {
        public:
            struct FlagParseState;
            struct FmtArgument;
            //static inline void doFormatArgs(FormatInfo* inf, const char *format, const FmtArgument *args, size_t num_args)

            using GetNextFN = FmtArgument*(*)(FormatInfo*, FlagParseState*, FmtArgument*, size_t, size_t);

            struct FmtArgument
            {
                public:
                    enum Type
                    {
                        FA_NUMINT,
                        FA_NUMDOUBLE,
                        FA_CSTRING,
                        FA_VALUE,
                    };

                    struct StringBox
                    {
                        const char* data;
                        size_t length;
                    };

                public:
                    Type m_type;

                    union
                    {
                        int valnumint;
                        double valnumdouble;
                        StringBox valcstring;
                        Value valvalue;
                    } m_vals;

                public:
                    FmtArgument(int value): m_type(FA_NUMINT)
                    {
                        m_vals.valnumint = value;
                    }

                    FmtArgument(double value): m_type(FA_NUMDOUBLE)
                    {
                        m_vals.valnumdouble = value;
                    }

                    FmtArgument(const char* value, size_t len): m_type(FA_CSTRING)
                    {
                        m_vals.valcstring = StringBox{value, len};
                    }

                    FmtArgument(String* os): FmtArgument(os->data(), os->size())
                    {
                    }

                    FmtArgument(Value value): m_type(FA_VALUE)
                    {
                        m_vals.valvalue = value;
                    }
            };

            struct FlagParseState
            {
                int currchar = -1;
                int nextchar = -1;
                bool failed = false;
                size_t position = 0;
                size_t argpos = 0;
                int argc = 0;
                Value* argv = nullptr;
                //Value currvalue;
                FmtArgument* currvalue;
            };

        public:
            /*
            template<typename... VargT>
            static inline bool formatArgs(Printer* pr, const char* fmt, VargT&&... args)
            {
                size_t argc = sizeof...(args);
                Value fargs[] = {(args) ..., Value::makeEmpty()};
                FormatInfo inf(pr->m_pvm, pr, fmt, strlen(fmt));
                return inf.format(argc, 0, fargs);
            }
            */

            //using GetNextFN = bool(*)(FormatInfo*, FlagParseState*, const FmtArgument*, size_t);

            static inline FmtArgument* doFormatArgs(FormatInfo* inf, FlagParseState* st, FmtArgument *args, size_t numargs, size_t pos)
            {
                // here we can access arguments by index
                if(pos <= numargs)
                {
                    return &args[pos];
                }
                return nullptr;
            }

            template<typename... ArgsT>
            static inline bool formatArgs(Printer* pr, const char *format, ArgsT&&... args)
            {
                FmtArgument argvals[] = {args...};
                FormatInfo inf(pr->m_pvm, pr, format, strlen(format), &FormatInfo::doFormatArgs);
                //doFormatArgs(inf, format, argvals, sizeof...(Args));
                return inf.format(0, 0, nullptr);
            }


        private:
            static inline FmtArgument* defaultGetNextValue(FormatInfo* inf, FlagParseState* st, FmtArgument *args, size_t numargs, size_t pos)
            {
                (void)inf;
                (void)args;
                (void)numargs;
                //st.currvalue = st.argv[st.argpos];
                //return st->argv[pos];
                static FmtArgument rt(st->argv[st->argpos]);
                return &rt;
            }

        public:
            State* m_pvm;
            /* length of the format string */
            size_t m_fmtlen = 0;
            /* the actual format string */
            const char* m_fmtstr = nullptr;
            /* destination printer */
            Printer* m_printer = nullptr;
            GetNextFN m_fngetnext = nullptr;

        private:
            inline void doPrintFmtArg(const FmtArgument* arg, FmtArgument::Type expect, bool dump)
            {
                if(expect != FmtArgument::FA_UNDEFINED)
                {
                    
                }
                else
                {
                    switch(arg->m_type)
                    {
                        case FmtArgument::FA_VALUE:
                            {
                                m_printer->printValue(arg->m_vals.valvalue, dump, false);
                            }
                            break;
                        case FmtArgument::FA_CSTRING:
                            {
                                m_printer->put(arg->m_vals.valcstring.data, arg->m_vals.valcstring.length);
                            }
                            break;
                            
                    }
                }
            }

            inline void doHandleFlag(FlagParseState& st)
            {
                int intval;
                if(st.nextchar == '%')
                {
                    m_printer->putChar('%');
                }
                else
                {
                    st.position++;
                    if((int)st.argpos > st.argc)
                    {
                        st.failed = true;
                        //st.currvalue = Value::makeEmpty();
                        st.currvalue = nullptr;
                    }
                    else
                    {
                        /*
                        if(m_fngetnext != nullptr)
                        {*/
                            st.currvalue = m_fngetnext(this, &st, nullptr, 0, st.argpos);
                        /*}
                        else
                        {
                            st.currvalue = st.argv[st.argpos];
                        }*/
                    }
                    st.argpos++;
                    switch(st.nextchar)
                    {
                        case 'q':
                        case 'p':
                            {
                                //m_printer->printValue(st.currvalue, true, true);
                                doPrintFmtArg(st.currvalue, FmtArgument::FA_VALUE, true);
                            }
                            break;
                        case 'c':
                            {
                                intval = (int)st.currvalue->m_vals.valvalue.asNumber();
                                m_printer->putformat("%c", intval);
                            }
                            break;
                        /* TODO: implement actual field formatting */
                        case 's':
                        case 'd':
                        case 'i':
                        case 'g':
                            {
                                //m_printer->printValue(st.currvalue, false, true);
                                doPrintFmtArg(st.currvalue, FmtArgument::);
                            }
                            break;
                        default:
                            {
                                m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "unknown/invalid format flag '%%c'", st.nextchar);
                            }
                            break;
                    }
                }
            }

        public:
            inline FormatInfo(State* state, Printer* pr, const char* fstr, size_t flen): m_pvm(state)
            {
                m_fmtstr = fstr;
                m_fmtlen = flen;
                m_printer = pr;
            }

            inline ~FormatInfo()
            {
            }

            inline bool format(int argc, int argbegin, Value* argv)
            {
                return format(argc, argbegin, argv, &FormatInfo::defaultGetNextValue);
            }

            inline bool format(int argc, int argbegin, Value* argv, GetNextFN fn)
            {
                FlagParseState st;
                st.position = 0;
                st.argpos = argbegin;
                st.failed = false;
                st.argc = argc;
                st.argv = argv;
                m_fngetnext = fn;
                while(st.position < m_fmtlen)
                {
                    st.currchar = m_fmtstr[st.position];
                    st.nextchar = -1;
                    if((st.position + 1) < m_fmtlen)
                    {
                        st.nextchar = m_fmtstr[st.position+1];
                    }
                    st.position++;
                    if(st.currchar == '%')
                    {
                        doHandleFlag(st);
                    }
                    else
                    {
                        m_printer->putChar(st.currchar);
                    }
                }
                return st.failed;
            }
    };


