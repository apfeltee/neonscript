
    struct VM
    {
        public:
            struct GC
            {

                /*
                static void* allocate(State* state, size_t size, size_t amount)
                {
                    return GC::reallocate(state, nullptr, 0, size * amount);
                }
                */

                static void* allocate(State* state, size_t tsize, size_t count)
                {
                    void* result;
                    size_t actualsz;
                    // this is a dummy; this function used be 'reallocate()'
                    size_t oldsize;
                    actualsz = (tsize * count);
                    oldsize = 0;
                    state->m_vmstate->gcMaybeCollect(actualsz - oldsize, actualsz > oldsize);
                    result = Memory::osMalloc(actualsz);
                    if(result == nullptr)
                    {
                        fprintf(stderr, "fatal error: failed to allocate %ld bytes (size: %ld, count: %ld)\n", actualsz, tsize, count);
                        abort();
                    }
                    return result;
                }

                static void gcRelease(State* state, void* pointer, size_t oldsize)
                {
                    state->m_vmstate->gcMaybeCollect(-oldsize, false);
                    if(oldsize > 0)
                    {
                        memset(pointer, 0, oldsize);
                    }
                    Memory::osFree(pointer);
                    pointer = nullptr;
                }

                template<typename Type, typename... ArgsT>
                static Type* gcCreate(State* state, ArgsT&&... args)
                {
                    Type* rt;
                    Type* buf;
                    buf = (Type*)GC::allocate(state, sizeof(Type), 1);
                    rt = new(buf) Type(args...);
                    return rt;
                }

            };

        public:
            State* m_pvm;
            size_t m_stackidx = 0;
            size_t m_stackcapacity = 0;
            size_t m_framecapacity = 0;
            size_t m_framecount = 0;
            Instruction m_currentinstr;
            CallFrame* m_currentframe = nullptr;
            ScopeUpvalue* m_openupvalues = nullptr;
            Object* m_linkedobjects = nullptr;
            CallFrame* m_framevalues = nullptr;
            Value* m_stackvalues = nullptr;
            bool m_currentmarkvalue;

            struct
            {
                int graycount;
                int graycapacity;
                int bytesallocated;
                int nextgc;
                Object** graystack;
            } m_gcstate;


        public:
            VM(State* state): m_pvm(state)
            {
                size_t i;
                size_t j;
                m_linkedobjects = nullptr;
                m_currentframe = nullptr;
                {
                    m_stackcapacity = NEON_CFG_INITSTACKCOUNT;
                    m_stackvalues = (neon::Value*)neon::Memory::osMalloc(NEON_CFG_INITSTACKCOUNT * sizeof(neon::Value));
                    if(m_stackvalues == nullptr)
                    {
                        fprintf(stderr, "error: failed to allocate stackvalues!\n");
                        abort();
                    }
                    memset(m_stackvalues, 0, NEON_CFG_INITSTACKCOUNT * sizeof(neon::Value));
                }
                {
                    m_framecapacity = NEON_CFG_INITFRAMECOUNT;
                    m_framevalues = (neon::CallFrame*)neon::Memory::osMalloc(NEON_CFG_INITFRAMECOUNT * sizeof(neon::CallFrame));
                    if(m_framevalues == nullptr)
                    {
                        fprintf(stderr, "error: failed to allocate framevalues!\n");
                        abort();
                    }
                    memset(m_framevalues, 0, NEON_CFG_INITFRAMECOUNT * sizeof(neon::CallFrame));
                    for(i=0; i<NEON_CFG_INITFRAMECOUNT; i++)
                    {
                        for(j=0; j<NEON_CFG_MAXEXCEPTHANDLERS; j++)
                        {
                            m_framevalues[i].handlers[j].klass = nullptr;
                        }
                    }
                }
            }

            ~VM()
            {
            }

            void gcCollectGarbage();
            void gcTraceRefs();
            void gcMarkRoots();
            void gcSweep();
            void gcDestroyLinkedObjects();

            void resetVMState()
            {
                m_framecount = 0;
                m_stackidx = 0;
                m_openupvalues = nullptr;
            }

            template<typename SubObjT>
            SubObjT* gcProtect(SubObjT* object)
            {
                size_t frpos;
                stackPush(Value::fromObject(object));
                frpos = 0;
                if(m_framecount > 0)
                {
                    frpos = m_framecount - 1;
                }
                m_framevalues[frpos].gcprotcount++;
                return object;
            }

            void gcClearProtect()
            {
                size_t frpos;
                CallFrame* frame;
                frpos = 0;
                if(m_framecount > 0)
                {
                    frpos = m_framecount - 1;
                }
                frame = &m_framevalues[frpos];
                if(frame->gcprotcount > 0)
                {
                    m_stackidx -= frame->gcprotcount;
                }
                frame->gcprotcount = 0;
            }


            NEON_FORCEINLINE uint8_t readByte()
            {
                uint8_t r;
                r = m_currentframe->inscode->code;
                m_currentframe->inscode++;
                return r;
            }

            NEON_FORCEINLINE Instruction readInstruction()
            {
                Instruction r;
                r = *m_currentframe->inscode;
                m_currentframe->inscode++;
                return r;
            }

            NEON_FORCEINLINE uint16_t readShort()
            {
                uint8_t b;
                uint8_t a;
                a = m_currentframe->inscode[0].code;
                b = m_currentframe->inscode[1].code;
                m_currentframe->inscode += 2;
                return (uint16_t)((a << 8) | b);
            }

            NEON_FORCEINLINE Value readConst();

            NEON_FORCEINLINE String* readString()
            {
                return readConst().asString();
            }

            NEON_FORCEINLINE void stackPush(Value value)
            {
                checkMaybeResize();
                m_stackvalues[m_stackidx] = value;
                m_stackidx++;
            }

            NEON_FORCEINLINE Value stackPop()
            {
                Value v;
                m_stackidx--;
                v = m_stackvalues[m_stackidx];
                return v;
            }

            NEON_FORCEINLINE Value stackPop(int n)
            {
                Value v;
                m_stackidx -= n;
                v = m_stackvalues[m_stackidx];
                return v;
            }

            NEON_FORCEINLINE Value stackPeek(int distance)
            {
                Value v;
                v = m_stackvalues[m_stackidx + (-1 - distance)];
                return v;
            }

            bool checkMaybeResize()
            {
                if((m_stackidx+1) >= m_stackcapacity)
                {
                    if(!resizeStack(m_stackidx + 1))
                    {
                        return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "failed to resize stack due to overflow");
                    }
                    return true;
                }
                if(m_framecount >= m_framecapacity)
                {
                    if(!resizeFrames(m_framecapacity + 1))
                    {
                        return m_pvm->raiseClass(m_pvm->m_exceptions.stdexception, "failed to resize frames due to overflow");
                    }
                    return true;
                }
                return false;
            }

            /*
            * grows m_(stack|frame)values, respectively.
            * currently it works fine with mob.js (man-or-boy test), although
            * there are still some invalid reads regarding the closure;
            * almost definitely because the pointer address changes.
            *
            * currently, the implementation really does just increase the
            * memory block available:
            * i.e., [Value x 32] -> [Value x <newsize>], without
            * copying anything beyond primitive values.
            */
            bool resizeStack(size_t needed)
            {
                size_t oldsz;
                size_t newsz;
                size_t allocsz;
                size_t nforvals;
                Value* oldbuf;
                Value* newbuf;
                if(((int)needed < 0) /* || (needed < m_stackcapacity) */)
                {
                    return true;
                }
                nforvals = (needed * 2);
                oldsz = m_stackcapacity;
                newsz = oldsz + nforvals;
                allocsz = ((newsz + 1) * sizeof(Value));
                fprintf(stderr, "*** resizing stack: needed %ld (%ld), from %ld to %ld, allocating %ld ***\n", nforvals, needed, oldsz, newsz, allocsz);
                oldbuf = m_stackvalues;
                newbuf = (Value*)Memory::osRealloc(oldbuf, allocsz );
                if(newbuf == nullptr)
                {
                    fprintf(stderr, "internal error: failed to resize stackvalues!\n");
                    abort();
                }
                m_stackvalues = (Value*)newbuf;
                fprintf(stderr, "oldcap=%ld newsz=%ld\n", m_stackcapacity, newsz);
                m_stackcapacity = newsz;
                return true;
            }

            bool resizeFrames(size_t needed)
            {
                /* return false; */
                size_t oldsz;
                size_t newsz;
                size_t allocsz;
                int oldhandlercnt;
                Instruction* oldip;
                FuncClosure* oldclosure;
                CallFrame* oldbuf;
                CallFrame* newbuf;
                fprintf(stderr, "*** resizing frames ***\n");
                oldclosure = m_currentframe->closure;
                oldip = m_currentframe->inscode;
                oldhandlercnt = m_currentframe->handlercount;
                oldsz = m_framecapacity;
                newsz = oldsz + needed;
                allocsz = ((newsz + 1) * sizeof(CallFrame));
                #if 1
                    oldbuf = m_framevalues;
                    newbuf = (CallFrame*)Memory::osRealloc(oldbuf, allocsz);
                    if(newbuf == nullptr)
                    {
                        fprintf(stderr, "internal error: failed to resize framevalues!\n");
                        abort();
                    }
                #endif
                m_framevalues = (CallFrame*)newbuf;
                m_framecapacity = newsz;
                /*
                * this bit is crucial: realloc changes pointer addresses, and to keep the
                * current frame, re-read it from the new address.
                */
                m_currentframe = &m_framevalues[m_framecount - 1];
                m_currentframe->handlercount = oldhandlercnt;
                m_currentframe->inscode = oldip;
                m_currentframe->closure = oldclosure;
                return true;
            }

            void gcMaybeCollect(int addsize, bool wasnew)
            {
                m_gcstate.bytesallocated += addsize;
                if(m_gcstate.nextgc > 0)
                {
                    if(wasnew && m_gcstate.bytesallocated > m_gcstate.nextgc)
                    {
                        if(m_currentframe)
                        {
                            if(m_currentframe->gcprotcount == 0)
                            {
                                gcCollectGarbage();
                            }
                        }
                    }
                }
            }


            bool exceptionPushHandler(ClassObject* type, int address, int finallyaddress)
            {
                CallFrame* frame;
                frame = &m_framevalues[m_framecount - 1];
                if(frame->handlercount == NEON_CFG_MAXEXCEPTHANDLERS)
                {
                    m_pvm->raiseFatal("too many nested exception handlers in one function");
                    return false;
                }
                frame->handlers[frame->handlercount].address = address;
                frame->handlers[frame->handlercount].finallyaddress = finallyaddress;
                frame->handlers[frame->handlercount].klass = type;
                frame->handlercount++;
                return true;
            }


            bool exceptionHandleUncaught(ClassInstance* exception);
            Value getExceptionStacktrace();
            bool exceptionPropagate();


            bool vmCallNative(FuncNative* native, Value thisval, int argcount);

            bool vmCallBoundValue(Value callable, Value thisval, int argcount);

            bool callClosure(FuncClosure* closure, Value thisval, int argcount);


            bool vmCallValue(Value callable, Value thisval, int argcount);


            bool vmInvokeMethodFromClass(ClassObject* klass, String* name, int argcount);

            bool vmInvokeSelfMethod(String* name, int argcount);

            bool vmInvokeMethod(String* name, int argcount);

            bool vmBindMethod(ClassObject* klass, String* name);



            Status runVM(int exitframe, Value* rv);

    };

