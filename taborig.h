
namespace neon
{
    struct HashTable final
    {
        public:
            struct Entry
            {
                Value key;
                Property value;
            };

        public:
            /*
            * FIXME: extremely stupid hack: $m_active ensures that a table that was destroyed
            * does not get marked again, et cetera.
            * since ~HashTable() zeroes the data before freeing, $m_active will be
            * false, and thus, no further marking occurs.
            * obviously the reason is that somewhere a table (from ClassInstance) is being
            * read after being freed, but for now, this will work(-ish).
            */
            State* m_pvm;
            bool m_active;
            uint64_t m_count;
            uint64_t m_capacity;
            Entry* m_entries;

        public:
            HashTable(State* state): m_pvm(state)
            {
                m_active = true;
                m_count = 0;
                m_capacity = 0;
                m_entries = nullptr;
            }

            ~HashTable()
            {
                Memory::freeArray(m_entries, m_capacity);
            }

            NEON_FORCEINLINE uint64_t size() const
            {
                return m_count;
            }

            NEON_FORCEINLINE uint64_t length() const
            {
                return m_count;
            }

            NEON_FORCEINLINE uint64_t count() const
            {
                return m_count;
            }

            Entry* findEntryByValue(Entry* availents, int availcap, Value key)
            {
                uint32_t hash;
                uint32_t index;
                State* state;
                Entry* entry;
                Entry* tombstone;
                state = m_pvm;
                hash = Value::getHash(key);
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "looking for key ");
                state->m_debugprinter->printValue(key, true, false);
                fprintf(stderr, " with hash %u in table...\n", hash);
                #endif
                index = hash & (availcap - 1);
                tombstone = nullptr;
                while(true)
                {
                    entry = &availents[index];
                    if(entry->key.isEmpty())
                    {
                        if(entry->value.m_actualval.isNull())
                        {
                            /* empty entry */
                            if(tombstone != nullptr)
                            {
                                return tombstone;
                            }
                            else
                            {
                                return entry;
                            }
                        }
                        else
                        {
                            /* we found a tombstone. */
                            if(tombstone == nullptr)
                            {
                                tombstone = entry;
                            }
                        }
                    }
                    else if(key.compare(state, entry->key))
                    {
                        return entry;
                    }
                    index = (index + 1) & (availcap - 1);
                }
                return nullptr;
            }

            bool compareEntryString(Value vkey, const char* kstr, size_t klen)
            {
                size_t oslen;
                const char* osdata;
                String* entoskey;
                entoskey = vkey.asString();
                oslen = Helper::getStringLength(entoskey);
                osdata = Helper::getStringData(entoskey);
                if(oslen == klen)
                {
                    if(memcmp(kstr, osdata, klen) == 0)
                    {
                        return true;
                    }
                }
                return false;
            }

            Entry* findEntryByStr(Entry* availents, int availcap, Value valkey, const char* kstr, size_t klen, uint32_t hash)
            {
                bool havevalhash;
                uint32_t index;
                uint32_t valhash;
                Entry* entry;
                Entry* tombstone;
                State* state;
                state = m_pvm;
                (void)valhash;
                (void)havevalhash;
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "looking for key ");
                state->m_debugprinter->printValue(key, true, false);
                fprintf(stderr, " with hash %u in table...\n", hash);
                #endif
                valhash = 0;
                havevalhash = false;
                index = hash & (availcap - 1);
                tombstone = nullptr;
                while(true)
                {
                    entry = &availents[index];
                    if(entry->key.isEmpty())
                    {
                        if(entry->value.m_actualval.isNull())
                        {
                            /* empty entry */
                            if(tombstone != nullptr)
                            {
                                return tombstone;
                            }
                            else
                            {
                                return entry;
                            }
                        }
                        else
                        {
                            /* we found a tombstone. */
                            if(tombstone == nullptr)
                            {
                                tombstone = entry;
                            }
                        }
                    }
                    if(entry->key.isString())
                    {
                        /*
                            entoskey = entry->key.asString();
                            if(entoskey->length() == klen)
                            {
                                if(memcmp(kstr, entoskey->data(), klen) == 0)
                                {
                                    return entry;
                                }
                            }
                        */
                        if(compareEntryString(entry->key, kstr, klen))
                        {
                            return entry;
                        }
                    }
                    else
                    {
                        if(!valkey.isEmpty())
                        {
                            if(valkey.compare(state, entry->key))
                            {
                                return entry;
                            }
                        }
                    }
                    index = (index + 1) & (availcap - 1);
                }
                return nullptr;
            }

            Property* getFieldByValue(Value key)
            {
                State* state;
                Entry* entry;
                (void)state;
                state = m_pvm;
                if(m_count == 0 || m_entries == nullptr)
                {
                    return nullptr;
                }
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "getting entry with hash %u...\n", Value::getHash(key));
                #endif
                entry = findEntryByValue(m_entries, m_capacity, key);
                if(entry->key.isEmpty() || entry->key.isNull())
                {
                    return nullptr;
                }
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "found entry for hash %u == ", Value::getHash(entry->key));
                state->m_debugprinter->printValue(entry->value.m_actualval, true, false);
                fprintf(stderr, "\n");
                #endif
                return &entry->value;
            }

            Property* getFieldByStr(Value valkey, const char* kstr, size_t klen, uint32_t hash)
            {
                State* state;
                Entry* entry;
                (void)state;
                state = m_pvm;
                if(m_count == 0 || m_entries == nullptr)
                {
                    return nullptr;
                }
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "getting entry with hash %u...\n", Value::getHash(key));
                #endif
                entry = findEntryByStr(m_entries, m_capacity, valkey, kstr, klen, hash);
                if(entry->key.isEmpty() || entry->key.isNull())
                {
                    return nullptr;
                }
                #if defined(NEON_CFG_DEBUGTABLE) && NEON_CFG_DEBUGTABLE
                fprintf(stderr, "found entry for hash %u == ", Value::getHash(entry->key));
                state->m_debugprinter->printValue(entry->value.m_actualval, true, false);
                fprintf(stderr, "\n");
                #endif
                return &entry->value;
            }

            Property* getByObjString(String* str)
            {
                uint32_t oshash;
                size_t oslen;
                const char* osdata;
                oslen = Helper::getStringLength(str);
                osdata = Helper::getStringData(str);
                oshash = Helper::getStringHash(str);
                return getFieldByStr(Value::makeEmpty(), osdata, oslen, oshash);
            }


            Property* getByCStr(const char* kstr)
            {
                size_t klen;
                uint32_t hash;
                klen = strlen(kstr);
                hash = Util::hashString(kstr, klen);
                return getFieldByStr(Value::makeEmpty(), kstr, klen, hash);
            }

            Property* getField(Value key)
            {
                uint32_t oshash;
                size_t oslen;
                const char* osdata;
                String* oskey;
                if(key.isString())
                {
                    oskey = key.asString();
                    osdata = Helper::getStringData(oskey);
                    oslen = Helper::getStringLength(oskey);
                    oshash = Helper::getStringHash(oskey);
                    return getFieldByStr(key, osdata, oslen, oshash);
                }
                return getFieldByValue(key);
            }

            bool get(Value key, Value* value)
            {
                Property* field;
                field = getField(key);
                if(field != nullptr)
                {
                    *value = field->m_actualval;
                    return true;
                }
                return false;
            }

            void adjustCapacity(uint64_t needcap)
            {
                uint64_t i;
                Entry* dest;
                Entry* entry;
                Entry* nents;
                //nents = (Entry*)State::VM::gcAllocMemory(state, sizeof(Entry), needcap);
                nents = (Entry*)Memory::osMalloc(sizeof(Entry) * needcap);
                for(i = 0; i < needcap; i++)
                {
                    nents[i].key = Value::makeEmpty();
                    nents[i].value = Property(Value::makeNull(), Property::PROPTYPE_VALUE);
                }
                /* repopulate buckets */
                m_count = 0;
                for(i = 0; i < m_capacity; i++)
                {
                    entry = &m_entries[i];
                    if(entry->key.isEmpty())
                    {
                        continue;
                    }
                    dest = findEntryByValue(nents, needcap, entry->key);
                    dest->key = entry->key;
                    dest->value = entry->value;
                    m_count++;
                }
                /* free the old entries... */
                Memory::freeArray(m_entries, m_capacity);
                m_entries = nents;
                m_capacity = needcap;
            }

            bool setType(Value key, Value value, Property::Type ftyp, bool keyisstring)
            {
                bool isnew;
                uint64_t needcap;
                Entry* entry;
                (void)keyisstring;
                if(m_count + 1 > m_capacity * NEON_CFG_MAXTABLELOAD)
                {
                    needcap = Util::growCapacity(m_capacity);
                    adjustCapacity(needcap);
                }
                entry = findEntryByValue(m_entries, m_capacity, key);
                isnew = entry->key.isEmpty();
                if(isnew && entry->value.m_actualval.isNull())
                {
                    m_count++;
                }
                /* overwrites existing entries. */
                entry->key = key;
                entry->value = Property(value, ftyp);
                return isnew;
            }

            bool set(Value key, Value value)
            {
                return setType(key, value, Property::PROPTYPE_VALUE, key.isString());
            }

            bool setCStrType(const char* cstrkey, Value value, Property::Type ftype)
            {
                String* os;
                os = Helper::copyObjString(m_pvm, cstrkey);
                return setType(Value::fromObject(os), value, ftype, true);
            }

            bool setCStr(const char* cstrkey, Value value)
            {
                return setCStrType(cstrkey, value, Property::PROPTYPE_VALUE);
            }

            bool removeByKey(Value key)
            {
                Entry* entry;
                if(m_count == 0)
                {
                    return false;
                }
                /* find the entry */
                entry = findEntryByValue(m_entries, m_capacity, key);
                if(entry->key.isEmpty())
                {
                    return false;
                }
                /* place a tombstone in the entry. */
                entry->key = Value::makeEmpty();
                entry->value = Property(Value::makeBool(true), Property::PROPTYPE_VALUE);
                return true;
            }

            void addAll(HashTable* from)
            {
                uint64_t i;
                Entry* entry;
                for(i = 0; i < from->m_capacity; i++)
                {
                    entry = &from->m_entries[i];
                    if(!entry->key.isEmpty())
                    {
                        setType(entry->key, entry->value.m_actualval, entry->value.m_proptype, false);
                    }
                }
            }

            void copyFrom(HashTable* from)
            {
                uint64_t i;
                State* state;
                Entry* entry;
                state = from->m_pvm;
                for(i = 0; i < from->m_capacity; i++)
                {
                    entry = &from->m_entries[i];
                    if(!entry->key.isEmpty())
                    {
                        setType(entry->key, Value::copyValue(state, entry->value.m_actualval), entry->value.m_proptype, false);
                    }
                }
            }

            String* findString(const char* chars, size_t length, uint32_t hash)
            {
                size_t slen;
                uint32_t shash;
                uint32_t index;
                const char* sdata;
                Entry* entry;
                String* string;
                if(m_count == 0)
                {
                    return nullptr;
                }
                index = hash & (m_capacity - 1);
                while(true)
                {
                    entry = &m_entries[index];
                    if(entry->key.isEmpty())
                    {
                        /*
                        // stop if we find an empty non-tombstone entry
                        //if(entry->m_actualval.isNull())
                        */
                        {
                            return nullptr;
                        }
                    }
                    string = entry->key.asString();
                    slen = Helper::getStringLength(string);
                    sdata = Helper::getStringData(string);
                    shash = Helper::getStringHash(string);
                    if((slen == length) && (shash == hash) && memcmp(sdata, chars, length) == 0)
                    {
                        /* we found it */
                        return string;
                    }
                    index = (index + 1) & (m_capacity - 1);
                }
            }

            Value findKey(Value value)
            {
                uint64_t i;
                Entry* entry;
                for(i = 0; i < m_capacity; i++)
                {
                    entry = &m_entries[i];
                    if(!entry->key.isNull() && !entry->key.isEmpty())
                    {
                        if(entry->value.m_actualval.compare(m_pvm, value))
                        {
                            return entry->key;
                        }
                    }
                }
                return Value::makeNull();
            }

            void markTableValues(State* state)
            {
                uint64_t i;
                Entry* entry;
                if(!m_active)
                {
                    state->warn("trying to mark inactive hashtable <%p>!", this);
                    return;
                }
                for(i = 0; i < m_capacity; i++)
                {
                    entry = &m_entries[i];
                    if(entry != nullptr)
                    {
                        Object::markValue(state, entry->key);
                        Object::markValue(state, entry->value.m_actualval);
                    }
                }
            }

            void removeMarkedValues(State* state)
            {
                uint64_t i;
                Entry* entry;
                for(i = 0; i < m_capacity; i++)
                {
                    entry = &m_entries[i];
                    if(entry->key.isObject() && entry->key.asObject()->m_mark != state->m_vmstate->m_currentmarkvalue)
                    {
                        removeByKey(entry->key);
                    }
                }
            }
    };
}

