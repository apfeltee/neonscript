
NNHashValTable* nn_tableval_make(NNState* state)
{
    NNHashValTable* table;
    table = (NNHashValTable*)nn_gcmem_allocate(state, sizeof(NNHashValTable), 1);
    if(table == NULL)
    {
        return NULL;
    }
    table->pvm = state;
    table->active = true;
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
    return table;
}

void nn_tableval_destroy(NNHashValTable* table)
{
    NNState* state;
    if(table != NULL)
    {
        state = table->pvm;
        nn_gcmem_freearray(state, sizeof(NNHashValEntry), table->entries, table->capacity);
        memset(table, 0, sizeof(NNHashValTable));
        nn_gcmem_release(state, table, sizeof(NNHashValTable));
    }
}

NNHashValEntry* nn_tableval_findentrybyvalue(NNHashValTable* table, NNHashValEntry* entries, int capacity, NNValue key)
{
    uint32_t hash;
    uint32_t index;
    NNState* state;
    NNHashValEntry* entry;
    NNHashValEntry* tombstone;
    state = table->pvm;
    hash = nn_value_hashvalue(key);
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "looking for key ");
    nn_printer_printvalue(state->debugwriter, key, true, false);
    fprintf(stderr, " with hash %u in table...\n", hash);
    #endif
    index = hash & (capacity - 1);
    tombstone = NULL;
    while(true)
    {
        entry = &entries[index];
        if(nn_value_isnull(entry->key))
        {
            if(nn_value_isnull(entry->value.value))
            {
                /* empty entry */
                if(tombstone != NULL)
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
                if(tombstone == NULL)
                {
                    tombstone = entry;
                }
            }
        }
        else if(nn_value_compare(state, key, entry->key))
        {
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
    return NULL;
}

NNHashValEntry* nn_tableval_findentrybystr(NNHashValTable* table, NNHashValEntry* entries, int capacity, NNValue valkey, const char* kstr, size_t klen, uint32_t hash)
{
    bool havevalhash;
    uint32_t index;
    uint32_t valhash;
    NNObjString* entoskey;
    NNHashValEntry* entry;
    NNHashValEntry* tombstone;
    NNState* state;
    state = table->pvm;
    (void)valhash;
    (void)havevalhash;
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "looking for key ");
    nn_printer_printvalue(state->debugwriter, key, true, false);
    fprintf(stderr, " with hash %u in table...\n", hash);
    #endif
    valhash = 0;
    havevalhash = false;
    index = hash & (capacity - 1);
    tombstone = NULL;
    while(true)
    {
        entry = &entries[index];
        if(nn_value_isnull(entry->key))
        {
            if(nn_value_isnull(entry->value.value))
            {
                /* empty entry */
                if(tombstone != NULL)
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
                if(tombstone == NULL)
                {
                    tombstone = entry;
                }
            }
        }
        if(nn_value_isstring(entry->key))
        {
            entoskey = nn_value_asstring(entry->key);
            if(entoskey->sbuf->length == klen)
            {
                if(memcmp(kstr, entoskey->sbuf->data, klen) == 0)
                {
                    return entry;
                }
            }
        }
        else
        {
            if(!nn_value_isnull(valkey))
            {
                if(nn_value_compare(state, valkey, entry->key))
                {
                    return entry;
                }
            }
        }
        index = (index + 1) & (capacity - 1);
    }
    return NULL;
}

NNProperty* nn_tableval_getfieldbyvalue(NNHashValTable* table, NNValue key)
{
    NNState* state;
    NNHashValEntry* entry;
    (void)state;
    state = table->pvm;
    if(table->count == 0 || table->entries == NULL)
    {
        return NULL;
    }
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "getting entry with hash %u...\n", nn_value_hashvalue(key));
    #endif
    entry = nn_tableval_findentrybyvalue(table, table->entries, table->capacity, key);
    if(nn_value_isnull(entry->key) || nn_value_isnull(entry->key))
    {
        return NULL;
    }
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "found entry for hash %u == ", nn_value_hashvalue(entry->key));
    nn_printer_printvalue(state->debugwriter, entry->value.value, true, false);
    fprintf(stderr, "\n");
    #endif
    return &entry->value;
}

NNProperty* nn_tableval_getfieldbystr(NNHashValTable* table, NNValue valkey, const char* kstr, size_t klen, uint32_t hash)
{
    NNState* state;
    NNHashValEntry* entry;
    (void)state;
    state = table->pvm;
    if(table->count == 0 || table->entries == NULL)
    {
        return NULL;
    }
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "getting entry with hash %u...\n", nn_value_hashvalue(key));
    #endif
    entry = nn_tableval_findentrybystr(table, table->entries, table->capacity, valkey, kstr, klen, hash);
    if(nn_value_isnull(entry->key) || nn_value_isnull(entry->key))
    {
        return NULL;
    }
    #if defined(DEBUG_TABLE) && DEBUG_TABLE
    fprintf(stderr, "found entry for hash %u == ", nn_value_hashvalue(entry->key));
    nn_printer_printvalue(state->debugwriter, entry->value.value, true, false);
    fprintf(stderr, "\n");
    #endif
    return &entry->value;
}

NNProperty* nn_tableval_getfieldbyostr(NNHashValTable* table, NNObjString* str)
{
    return nn_tableval_getfieldbystr(table, nn_value_makenull(), str->sbuf->data, str->sbuf->length, str->hash);
}

NNProperty* nn_tableval_getfieldbycstr(NNHashValTable* table, const char* kstr)
{
    size_t klen;
    uint32_t hash;
    klen = strlen(kstr);
    hash = nn_util_hashstring(kstr, klen);
    return nn_tableval_getfieldbystr(table, nn_value_makenull(), kstr, klen, hash);
}

NNProperty* nn_tableval_getfield(NNHashValTable* table, NNValue key)
{
    NNObjString* oskey;
    if(nn_value_isstring(key))
    {
        oskey = nn_value_asstring(key);
        return nn_tableval_getfieldbystr(table, key, oskey->sbuf->data, oskey->sbuf->length, oskey->hash);
    }
    return nn_tableval_getfieldbyvalue(table, key);
}

bool nn_tableval_get(NNHashValTable* table, NNValue key, NNValue* value)
{
    NNProperty* field;
    field = nn_tableval_getfield(table, key);
    if(field != NULL)
    {
        *value = field->value;
        return true;
    }
    return false;
}

void nn_tableval_adjustcapacity(NNHashValTable* table, int capacity)
{
    int i;
    NNState* state;
    NNHashValEntry* dest;
    NNHashValEntry* entry;
    NNHashValEntry* entries;
    state = table->pvm;
    entries = (NNHashValEntry*)nn_gcmem_allocate(state, sizeof(NNHashValEntry), capacity);
    for(i = 0; i < capacity; i++)
    {
        entries[i].key = nn_value_makenull();
        entries[i].value = nn_property_make(state, nn_value_makenull(), NEON_PROPTYPE_VALUE);
    }
    /* repopulate buckets */
    table->count = 0;
    for(i = 0; i < table->capacity; i++)
    {
        entry = &table->entries[i];
        if(nn_value_isnull(entry->key))
        {
            continue;
        }
        dest = nn_tableval_findentrybyvalue(table, entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }
    /* free the old entries... */
    nn_gcmem_freearray(state, sizeof(NNHashValEntry), table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool nn_tableval_setwithtype(NNHashValTable* table, NNValue key, NNValue value, NNFieldType ftyp, bool keyisstring)
{
    bool isnew;
    int capacity;
    NNState* state;
    NNHashValEntry* entry;
    (void)keyisstring;
    state = table->pvm;
    if(table->count + 1 > table->capacity * NEON_CFG_MAXTABLELOAD)
    {
        capacity = GROW_CAPACITY(table->capacity);
        nn_tableval_adjustcapacity(table, capacity);
    }
    entry = nn_tableval_findentrybyvalue(table, table->entries, table->capacity, key);
    isnew = nn_value_isnull(entry->key);
    if(isnew && nn_value_isnull(entry->value.value))
    {
        table->count++;
    }
    /* overwrites existing entries. */
    entry->key = key;
    entry->value = nn_property_make(state, value, ftyp);
    return isnew;
}

bool nn_tableval_set(NNHashValTable* table, NNValue key, NNValue value)
{
    return nn_tableval_setwithtype(table, key, value, NEON_PROPTYPE_VALUE, nn_value_isstring(key));
}

bool nn_tableval_setcstrwithtype(NNHashValTable* table, const char* cstrkey, NNValue value, NNFieldType ftype)
{
    NNObjString* os;
    NNState* state;
    state = table->pvm;
    os = nn_string_copycstr(state, cstrkey);
    return nn_tableval_setwithtype(table, nn_value_fromobject(os), value, ftype, true);
}

bool nn_tableval_setcstr(NNHashValTable* table, const char* cstrkey, NNValue value)
{
    return nn_tableval_setcstrwithtype(table, cstrkey, value, NEON_PROPTYPE_VALUE);
}

bool nn_tableval_delete(NNHashValTable* table, NNValue key)
{
    NNHashValEntry* entry;
    if(table->count == 0)
    {
        return false;
    }
    /* find the entry */
    entry = nn_tableval_findentrybyvalue(table, table->entries, table->capacity, key);
    if(nn_value_isnull(entry->key))
    {
        return false;
    }
    /* place a tombstone in the entry. */
    entry->key = nn_value_makenull();
    entry->value = nn_property_make(table->pvm, nn_value_makebool(true), NEON_PROPTYPE_VALUE);
    return true;
}

void nn_tableval_addall(NNHashValTable* from, NNHashValTable* to)
{
    int i;
    NNHashValEntry* entry;
    for(i = 0; i < from->capacity; i++)
    {
        entry = &from->entries[i];
        if(!nn_value_isnull(entry->key))
        {
            nn_tableval_setwithtype(to, entry->key, entry->value.value, entry->value.type, false);
        }
    }
}

void nn_tableval_importall(NNHashValTable* from, NNHashValTable* to)
{
    int i;
    NNHashValEntry* entry;
    for(i = 0; i < (int)from->capacity; i++)
    {
        entry = &from->entries[i];
        if(!nn_value_isnull(entry->key) && !nn_value_ismodule(entry->value.value))
        {
            /* Don't import private values */
            if(nn_value_isstring(entry->key) && nn_value_asstring(entry->key)->sbuf->data[0] == '_')
            {
                continue;
            }
            nn_tableval_setwithtype(to, entry->key, entry->value.value, entry->value.type, false);
        }
    }
}

void nn_tableval_copy(NNHashValTable* from, NNHashValTable* to)
{
    int i;
    NNState* state;
    NNHashValEntry* entry;
    state = from->pvm;
    for(i = 0; i < (int)from->capacity; i++)
    {
        entry = &from->entries[i];
        if(!nn_value_isnull(entry->key))
        {
            nn_tableval_setwithtype(to, entry->key, nn_value_copyvalue(state, entry->value.value), entry->value.type, false);
        }
    }
}

NNObjString* nn_tableval_findstring(NNHashValTable* table, const char* chars, size_t length, uint32_t hash)
{
    size_t slen;
    uint32_t index;
    const char* sdata;
    NNHashValEntry* entry;
    NNObjString* string;
    if(table->count == 0)
    {
        return NULL;
    }
    index = hash & (table->capacity - 1);
    while(true)
    {
        entry = &table->entries[index];
        if(nn_value_isnull(entry->key))
        {
            /*
            // stop if we find an empty non-tombstone entry
            //if (nn_value_isnull(entry->value))
            */
            {
                return NULL;
            }
        }
        string = nn_value_asstring(entry->key);
        slen = string->sbuf->length;
        sdata = string->sbuf->data;
        if((slen == length) && (string->hash == hash) && memcmp(sdata, chars, length) == 0)
        {
            /* we found it */
            return string;
        }
        index = (index + 1) & (table->capacity - 1);
    }
}

NNValue nn_tableval_findkey(NNHashValTable* table, NNValue value)
{
    int i;
    NNHashValEntry* entry;
    for(i = 0; i < (int)table->capacity; i++)
    {
        entry = &table->entries[i];
        if(!nn_value_isnull(entry->key) && !nn_value_isnull(entry->key))
        {
            if(nn_value_compare(table->pvm, entry->value.value, value))
            {
                return entry->key;
            }
        }
    }
    return nn_value_makenull();
}

NNObjArray* nn_tableval_getkeys(NNHashValTable* table)
{
    int i;
    NNState* state;
    NNObjArray* list;
    NNHashValEntry* entry;
    state = table->pvm;
    list = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < table->capacity; i++)
    {
        entry = &table->entries[i];
        if(!nn_value_isnull(entry->key) && !nn_value_isnull(entry->key))
        {
            nn_vallist_push(list->varray, entry->key);
        }
    }
    return list;
}
