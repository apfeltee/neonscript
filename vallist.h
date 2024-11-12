


#if 1
    #define MC_UTIL_INCCAPACITY(capacity) (((capacity) < 8) ? 8 : ((capacity) * 2))
#else
    #define MC_UTIL_INCCAPACITY(capacity) ((capacity) + 2)
#endif

NNValArray* nn_vallist_make(NNState* state)
{
    size_t initialsize;
    NNValArray* list;
    initialsize = 32;
    list = (NNValArray*)nn_memory_malloc(sizeof(NNValArray));
    list->pvm = state;
    list->listcount = 0;
    list->listcapacity = 0;
    list->listitems = NULL;
    list->listname = NULL;
    if(initialsize > 0)
    {
        nn_vallist_ensurecapacity(list, initialsize, nn_value_makenull(), true);
    }
    return list;
}

void nn_vallist_destroy(NNValArray* list)
{
    #if 0
    if(list->listname != NULL)
    {
        fprintf(stderr, "vallist of '%s' use at end: count=%ld capacity=%ld\n", list->listname, list->listcount, list->listcapacity);
    }
    #endif
    if(list != NULL)
    {
        nn_memory_free(list->listitems);
        nn_memory_free(list);
        list = NULL;
    }
}

NEON_INLINE size_t nn_vallist_count(NNValArray* list)
{
    return list->listcount;
}

NEON_INLINE NNValue* nn_vallist_data(NNValArray* list)
{
    return list->listitems;
}

NEON_INLINE NNValue nn_vallist_get(NNValArray* list, size_t idx)
{
    return list->listitems[idx];
}

NEON_INLINE NNValue* nn_vallist_getp(NNValArray* list, size_t idx)
{
    return &list->listitems[idx];
}

NEON_INLINE void nn_vallist_mark(NNValArray* list)
{
    size_t i;
    for(i=0; i<list->listcount; i++)
    {
        nn_gcmem_markvalue(list->pvm, list->listitems[i]);
    }
}

NEON_INLINE bool nn_vallist_push(NNValArray* list, NNValue value)
{
    size_t oldcap;
    if(list->listcapacity < list->listcount + 1)
    {
        oldcap = list->listcapacity;
        list->listcapacity = MC_UTIL_INCCAPACITY(oldcap);
        if(list->listitems == NULL)
        {
            list->listitems = (NNValue*)nn_memory_malloc(sizeof(NNValue) * list->listcapacity);
        }
        else
        {
            list->listitems = (NNValue*)nn_memory_realloc(list->listitems, sizeof(NNValue) * list->listcapacity);
        }
    }
    list->listitems[list->listcount] = value;
    list->listcount++;
    return true;
}

NEON_INLINE bool nn_vallist_insert(NNValArray* list, NNValue val, size_t idx)
{
    return nn_vallist_set(list, idx, val);
}


NEON_INLINE bool nn_vallist_set(NNValArray* list, size_t idx, NNValue val)
{
    size_t need;
    need = idx + 8;
    if(list->listcount == 0)
    {
        return nn_vallist_push(list, val);
    }
    if(((idx == 0) || (list->listcapacity == 0)) || (idx >= list->listcapacity))
    {
        nn_vallist_ensurecapacity(list, need, nn_value_makenull(), false);
    }
    list->listitems[idx] = val;
    if(idx > list->listcount)
    {
        list->listcount = idx;
    }
    return true;
}


NEON_INLINE bool nn_vallist_pop(NNValArray* list, NNValue* dest)
{
    if(list->listcount > 0)
    {
        *dest = list->listitems[list->listcount - 1];
        list->listcount--;
        return true;
    }
    return false;
}

NEON_INLINE bool nn_vallist_removeatintern(NNValArray* list, unsigned int ix)
{
    size_t tomovebytes;
    void* src;
    void* dest;
    if(ix == (list->listcount - 1))
    {
        list->listcount--;
        return true;
    }
    tomovebytes = (list->listcount - 1 - ix) * sizeof(NNValue);
    dest = list->listitems + (ix * sizeof(NNValue));
    src = list->listitems + ((ix + 1) * sizeof(NNValue));
    memmove(dest, src, tomovebytes);
    list->listcount--;
    return true;
}

NEON_INLINE bool nn_vallist_removeat(NNValArray* list, unsigned int ix)
{
    if(ix >= list->listcount)
    {
        return false;
    }
    if(ix == 0)
    {
        list->listitems += sizeof(NNValue);
        list->listcapacity--;
        list->listcount--;
        return true;
    }
    return nn_vallist_removeatintern(list, ix);
}

NEON_INLINE void nn_vallist_ensurecapacity(NNValArray* list, size_t needsize, NNValue fillval, bool first)
{
    size_t i;
    size_t ncap;
    size_t oldcap;
    (void)first;
    if(list->listcapacity < needsize)
    {
        oldcap = list->listcapacity;
        if(oldcap == 0)
        {
            ncap = needsize;
        }
        else
        {
            ncap = MC_UTIL_INCCAPACITY(list->listcapacity + needsize);
        }
        list->listcapacity = ncap;
        if(list->listitems == NULL)
        {
            list->listitems = (NNValue*)nn_memory_malloc(sizeof(NNValue) * ncap);
        }
        else
        {
            list->listitems = (NNValue*)nn_memory_realloc(list->listitems, sizeof(NNValue) * ncap);
        }
        for(i = oldcap; i < ncap; i++)
        {
            list->listitems[i] = fillval;
        }
    }
}

NEON_INLINE NNValArray* nn_vallist_copy(NNValArray* list)
{
    size_t i;
    NNValArray* nlist;
    nlist = nn_vallist_make(list->pvm);
    for(i=0; i<list->listcount; i++)
    {
        nn_vallist_push(nlist, list->listitems[i]);
    }
    return nlist;
}

NEON_INLINE void nn_vallist_setempty(NNValArray* list)
{
    if((list->listcapacity > 0) && (list->listitems != NULL))
    {
        memset(list->listitems, 0, sizeof(NNValue) * list->listcapacity);
    }
    list->listcount = 0;
    list->listcapacity = 0;
}

