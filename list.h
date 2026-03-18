
template<typename StoredTyp>
class ValArray
{
    public:
        using OnCopyFN = std::function<StoredTyp(StoredTyp)>;
        using OnDestroyFN = std::function<void(StoredTyp)>;

    public:
        static void destroy(ValArray* list)
        {
            if(list != nullptr)
            {
                list->deInit();
                nn_memory_free(list);
                list = nullptr;
            }
        }

        static void destroy(ValArray* list, OnDestroyFN dfn)
        {
            if(list != nullptr)
            {    
                if(dfn != nullptr)
                {
                    destroyAndClear(list, dfn);
                }
                list->deInit();
                nn_memory_free(list);
            }
        }

        static void destroyAndClear(ValArray* list, OnDestroyFN dfn)
        {
            size_t i;
            for(i = 0; i < list->count(); i++)
            {
                auto& item = list->get(i);
                if(dfn != nullptr)
                {
                    dfn(item);
                }
            }
            list->clear();
        }

    private:
        size_t m_listcapacity = 0;
        size_t m_listcount = 0;
        StoredTyp* m_listitems = nullptr;

    private:
        NEON_INLINE bool removeAtIntern(unsigned int ix)
        {
            size_t tomovebytes;
            void* src;
            void* dest;
            if(ix == (m_listcount - 1))
            {
                m_listcount--;
                return true;
            }
            tomovebytes = (m_listcount - 1 - ix) * sizeof(StoredTyp);
            dest = m_listitems + (ix * sizeof(StoredTyp));
            src = m_listitems + ((ix + 1) * sizeof(StoredTyp));
            memmove(dest, src, tomovebytes);
            m_listcount--;
            return true;
        }

        size_t nextCapacity(size_t oldcap)
        {
            if((oldcap) < 8)
            {
                return 8;
            }
            #if 1
                return (oldcap * 2);
            #else
                return (((oldcap * 3) / 2) + 1);
            #endif
        }

        NEON_INLINE void ensureCapacity(size_t needsize, const StoredTyp& fillval, bool first)
        {
            size_t i;
            size_t ncap;
            size_t oldcap;
            (void)first;
            if(m_listcapacity < needsize)
            {
                oldcap = m_listcapacity;
                ncap = nextCapacity(m_listcapacity + needsize);
                m_listcapacity = ncap;
                if(m_listitems == nullptr)
                {
                    m_listitems = (StoredTyp*)nn_memory_malloc(sizeof(StoredTyp) * ncap);
                }
                else
                {
                    m_listitems = (StoredTyp*)nn_memory_realloc(m_listitems, sizeof(StoredTyp) * ncap);
                }
                for(i = oldcap; i < ncap; i++)
                {
                    m_listitems[i] = fillval;
                }
            }
        }

    public:
        NEON_INLINE ValArray(): ValArray(32)
        {
        }

        NEON_INLINE ValArray(size_t initialsize)
        {
            m_listcount = 0;
            m_listcapacity = 0;
            m_listitems = nullptr;
            if(initialsize > 0)
            {
                if constexpr(std::is_pointer<StoredTyp>::value)
                {
                    ensureCapacity(initialsize, nullptr, true);
                }
                else
                {
                    ensureCapacity(initialsize, {}, true);                    
                }
            }
        }

        template<typename InputT>
        NEON_INLINE ValArray(size_t initialsize, const InputT& value)
        {
            m_listcount = 0;
            m_listcapacity = 0;
            m_listitems = nullptr;
            if(initialsize > 0)
            {
                ensureCapacity(initialsize, value, true);
            }
        }

        NEON_INLINE ~ValArray()
        {
            //deInit();
        }


        template<typename InputT>
        void reserve(size_t sz, const InputT& value)
        {
            ensureCapacity(sz, value, true);
        }

        StoredTyp* getOrphanedData()
        {
            size_t i;
            auto rtdata = (StoredTyp*)nn_memory_malloc(count() * sizeof(StoredTyp));
            for(i=0; i<count(); i++)
            {
                rtdata[i] = get(i);
            }
            return rtdata;
        }

        NEON_INLINE void deInit(OnDestroyFN dfn)
        {
            size_t i;
            for(i=0; i<m_listcount; i++)
            {
                auto& item = get(i);
                dfn(item);
            }
            deInit();
        }

        NEON_INLINE void deInit()
        {
            if(m_listitems != nullptr)
            {
                nn_memory_free(m_listitems);
            }
            //m_listitems = nullptr;
            m_listcount = 0;
            m_listcapacity = 0;
        }

        bool shrinkToFit()
        {
            void* ndata;
            m_listcapacity = m_listcount;
            if(m_listitems == nullptr)
            {}
            ndata = nn_memory_realloc(m_listitems, m_listcount * sizeof(StoredTyp));
            if(ndata == NULL)
            {
                return false;
            }
            m_listitems = (StoredTyp*)ndata;
            return true;
        }

        NEON_INLINE void clear()
        {
            m_listcount = 0;
        }

        NEON_INLINE ValArray* copyToHeap(OnCopyFN copyfn, OnDestroyFN dfn)
        {
            bool ok;
            size_t i;
            ValArray* arrcopy;
            (void)ok;
            arrcopy = Memory::make<ValArray<StoredTyp>>(m_listcapacity);
            for(i = 0; i < count(); i++)
            {
                auto& item = get(i);
                if(copyfn)
                {
                    auto itemcopy = (StoredTyp)copyfn(item);
                    if(!arrcopy->push(itemcopy))
                    {
                        goto listcopyfailed;
                    }
                }
                else
                {
                    if(!arrcopy->push(item))
                    {
                        goto listcopyfailed;
                    }
                }
            }
            return arrcopy;
        listcopyfailed:
            Memory::destroy(arrcopy, dfn);
            return nullptr;
        }

        NEON_INLINE ValArray* copyToHeap()
        {
            OnCopyFN dummycopy = nullptr;
            OnDestroyFN dummydel = nullptr;
            return copyToHeap(dummycopy, dummydel);
        }

        NEON_INLINE bool copyToStack(ValArray* dest, OnCopyFN copyfn, OnDestroyFN dfn)
        {
            bool ok;
            size_t i;
            (void)ok;
            (void)dfn;
            for(i = 0; i < count(); i++)
            {
                auto& item = get(i);
                if(copyfn)
                {
                    auto itemcopy = (StoredTyp)copyfn(item);
                    if(!dest->push(itemcopy))
                    {
                        goto listcopyfailed;
                    }
                }
                else
                {
                    if(!dest->push(item))
                    {
                        goto listcopyfailed;
                    }
                }
            }
            return true;
        listcopyfailed:
            return false;
        }

        NEON_INLINE bool copyToStack(ValArray* dest)
        {
            OnCopyFN dummycopy = nullptr;
            OnDestroyFN dummydel = nullptr;
            return copyToStack(dest, dummycopy, dummydel);
        }

        NEON_INLINE ValArray copyToStack(OnCopyFN copyfn, OnDestroyFN dfn)
        {
            ValArray dest;
            copyToStack(&dest, copyfn, dfn);
            return dest;
        }

        NEON_INLINE ValArray copyToStack()
        {
            ValArray dest;
            copyToStack(&dest);
            return dest;
        }

        NEON_INLINE size_t count() const
        {
            return m_listcount;
        }

        NEON_INLINE size_t capacity() const
        {
            return m_listcapacity;
        }

        NEON_INLINE StoredTyp* data() const
        {
            return m_listitems;
        }

        NEON_INLINE StoredTyp& get(size_t idx) const
        {
            return m_listitems[idx];
        }

        NEON_INLINE StoredTyp* getp(size_t idx) const
        {
            return &m_listitems[idx];
        }

        NEON_INLINE StoredTyp& top() const
        {
            if(m_listcount == 0)
            {
                if constexpr(std::is_pointer<StoredTyp>::value)
                {
                    static StoredTyp nv = nullptr;
                    return nv;
                }
                else
                {
                    static StoredTyp nv = {};
                    return nv;
                }
            }
            return get(m_listcount - 1);
        }

        StoredTyp* topp() const
        {
            int ofs = 0;
            if(m_listcount == 0)
            {
                return nullptr;
            }
            if(m_listcount > 0)
            {
                ofs = m_listcount - 1;
            }
            return getp(ofs);
        }

        NEON_INLINE StoredTyp* set(size_t idx, const StoredTyp& val)
        {
            size_t need;
            need = idx + 1;
            if(((idx == 0) || (m_listcapacity == 0)) || (idx >= m_listcapacity))
            {
                if constexpr(std::is_pointer<StoredTyp>::value)
                {
                    ensureCapacity(need, nullptr, false);
                }
                else
                {
                    ensureCapacity(need, {}, false);
                }
            }
            if(idx > m_listcount)
            {
                m_listcount = idx;
            }
            m_listitems[idx] = val;
            return &m_listitems[idx];
        }

        NEON_INLINE bool push(const StoredTyp& value)
        {
            size_t oldcap;
            if(m_listcapacity < m_listcount + 1)
            {
                oldcap = m_listcapacity;
                m_listcapacity =  nextCapacity(oldcap);
                if(m_listitems == nullptr)
                {
                    m_listitems = (StoredTyp*)nn_memory_malloc(sizeof(StoredTyp) * m_listcapacity);
                }
                else
                {
                    m_listitems = (StoredTyp*)nn_memory_realloc(m_listitems, sizeof(StoredTyp) * m_listcapacity);
                }
            }
            m_listitems[m_listcount] = value;
            m_listcount++;
            return true;
        }

        NEON_INLINE bool pop(StoredTyp* dest)
        {
            if(m_listcount > 0)
            {
                if(dest != nullptr)
                {
                    *dest = m_listitems[m_listcount - 1];
                }
                m_listcount--;
                return true;
            }
            return false;
        }

        NEON_INLINE bool removeAt(unsigned int ix)
        {
            if(ix >= m_listcount)
            {
                return false;
            }
            if(ix == 0)
            {
                m_listitems += sizeof(StoredTyp);
                m_listcapacity--;
                m_listcount--;
                return true;
            }
            return removeAtIntern(ix);
        }

        NEON_INLINE void setEmpty()
        {
            if((m_listcapacity > 0) && (m_listitems != nullptr))
            {
                memset(m_listitems, 0, sizeof(StoredTyp) * m_listcapacity);
            }
            m_listcount = 0;
            m_listcapacity = 0;
        }
};
