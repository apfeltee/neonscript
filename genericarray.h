
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
                size_t m_count;
                /* how many entries can be stored before growing? */
                size_t m_capacity;
                ValType* m_values;

            private:
                bool destroy()
                {
                    if(m_values != nullptr)
                    {
                        Memory::freeArray(m_values, m_capacity);
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

                inline size_t size() const
                {
                    return m_count;
                }

                inline size_t length() const
                {
                    return m_count;
                }

                inline size_t count() const
                {
                    return m_count;
                }

                inline ValType& at(size_t i)
                {
                    return m_values[i];
                }

                inline ValType& at(size_t i) const
                {
                    return m_values[i];
                }

                inline ValType& operator[](size_t i)
                {
                    return m_values[i];
                }

                inline ValType& operator[](size_t i) const
                {
                    return m_values[i];
                }

                ValType pop()
                {
                    if(m_count > 0)
                    {
                        m_count--;
                        return m_values[m_count+1];
                    }
                    return {};
                }

                void push(ValType value)
                {
                    size_t oldcapacity;
                    if(m_capacity < m_count + 1)
                    {
                        oldcapacity = m_capacity;
                        m_capacity = Util::growCapacity(oldcapacity);
                        m_values = Memory::growArray(m_values, oldcapacity, m_capacity);
                    }
                    m_values[m_count] = value;
                    m_count++;
                }

                void insertDefault(ValType value, size_t index, ValType defaultvalue)
                {
                    size_t i;
                    size_t oldcap;
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

                ValType shiftDefault(size_t count, ValType defval)
                {
                    size_t i;
                    size_t j;
                    size_t vsz;
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

