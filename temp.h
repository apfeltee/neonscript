
    struct FormatInfo
    {
        public:
            struct FlagParseState;
            using GetNextFN = Value(*)(FlagParseState*, int);

            struct FlagParseState
            {
                int currchar = -1;
                int nextchar = -1;
                bool failed = false;
                size_t position = 0;
                size_t argpos = 0;
                int argc = 0;
                Value* argv = nullptr;
                Value currvalue;
            };

        public:
            template<typename... VargT>
            static inline bool formatArgs(Printer* pr, const char* fmt, VargT&&... args)
            {
                size_t argc = sizeof...(args);
                Value fargs[] = {(args) ..., Value::makeEmpty()};
                FormatInfo inf(pr->m_pvm, pr, fmt, strlen(fmt));
                return inf.format(argc, 0, fargs);
            }

        private:
            static inline Value defaultGetNextValue(FlagParseState* st, int pos)
            {
                //st.currvalue = st.argv[st.argpos];
                return st->argv[pos];
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
            inline void doHandleFlag(FlagParseState& st)
            {
                int intval;
                char chval;
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
                        st.currvalue = Value::makeEmpty();
                    }
                    else
                    {                        
                        if(m_fngetnext != nullptr)
                        {
                            st.currvalue = m_fngetnext(&st, st.argpos);
                        }
                        else
                        {
                            st.currvalue = st.argv[st.argpos];
                        }
                    }
                    st.argpos++;
                    switch(st.nextchar)
                    {
                        case 'q':
                        case 'p':
                            {
                                m_printer->printValue(st.currvalue, true, true);
                            }
                            break;
                        case 'c':
                            {
                                intval = (int)st.currvalue.asNumber();
                                chval = intval;
                                //m_printer->putformat("%c", intval);
                                m_printer->put(&chval, 1);
                            }
                            break;
                        /* TODO: implement actual field formatting */
                        case 's':
                        case 'd':
                        case 'i':
                        case 'g':
                            {
                                m_printer->printValue(st.currvalue, false, true);
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
                return format(argc, argbegin, argv, nullptr);
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


