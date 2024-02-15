
#pragma once

namespace neon
{
    namespace Util
    {
        template<typename ValType>
        struct GenericArray
        {
            public:
                /* how many entries are currently stored? */
                uint64_t m_count;
                /* how many entries can be stored before growing? */
                uint64_t m_capacity;
                ValType* m_values;

            private:
                bool destroy()
                {
                    if(m_values != nullptr)
                    {
                        Memory::Memory::freeArray(m_values, m_capacity);
                        m_values = nullptr;
                        m_count = 0;
                        m_capacity = 0;
                        return true;
                    }
                    return false;
                }

            public:
                GenericArray()
                {
                    m_capacity = 0;
                    m_count = 0;
                    m_values = nullptr;
                }

                ~GenericArray()
                {
                    destroy();
                }

                inline uint64_t size() const
                {
                    return m_count;
                }

                inline uint64_t length() const
                {
                    return m_count;
                }

                inline uint64_t count() const
                {
                    return m_count;
                }

                inline ValType& at(uint64_t i)
                {
                    return m_values[i];
                }

                inline ValType& operator[](uint64_t i)
                {
                    return at(i);
                }

                void push(ValType value)
                {
                    uint64_t oldcapacity;
                    if(m_capacity < m_count + 1)
                    {
                        oldcapacity = m_capacity;
                        m_capacity = Util::growCapacity(oldcapacity);
                        m_values = Memory::growArray(m_values, oldcapacity, m_capacity);
                    }
                    m_values[m_count] = value;
                    m_count++;
                }

                void insertDefault(ValType value, uint64_t index, ValType defaultvalue)
                {
                    uint64_t i;
                    uint64_t oldcap;
                    if(m_capacity <= index)
                    {
                        m_capacity = Util::growCapacity(index);
                        m_values = Memory::growArray(m_values, m_count, m_capacity);
                    }
                    else if(m_capacity < m_count + 2)
                    {
                        oldcap = m_capacity;
                        m_capacity = Util::growCapacity(oldcap);
                        m_values = Memory::growArray(m_values, oldcap, m_capacity);
                    }
                    if(index <= m_count)
                    {
                        for(i = m_count - 1; i >= index; i--)
                        {
                            m_values[i + 1] = m_values[i];
                        }
                    }
                    else
                    {
                        for(i = m_count; i < index; i++)
                        {
                            /* null out overflow indices */
                            m_values[i] = defaultvalue;
                            m_count++;
                        }
                    }
                    m_values[index] = value;
                    m_count++;
                }

                ValType shiftDefault(uint64_t count, ValType defval)
                {
                    uint64_t i;
                    uint64_t j;
                    uint64_t vsz;
                    ValType temp;
                    ValType item;
                    vsz = m_count;
                    if(count >= vsz || vsz == 1)
                    {
                        m_count = 0;
                        return defval;
                    }
                    item = m_values[0];
                    j = 0;
                    for(i=1; i<vsz; i++)
                    {
                        temp = m_values[i];
                        m_values[j] = temp;
                        j++;
                    }
                    m_count--;
                    return item;
                }


        };

    }
}

