


#if 1
    #define MC_UTIL_INCCAPACITY(capacity) (((capacity) < 8) ? 8 : ((capacity) * 2))
#else
    #define MC_UTIL_INCCAPACITY(capacity) ((capacity) + 2)
#endif

NEON_INLINE bool nn_valarray_ensurecapacity(NNValArray* list, size_t needsize, NNValue fillval, bool first);

NEON_INLINE void nn_valarray_init(NNState* state, NNValArray* list)
{
    size_t initialsize;
    initialsize = 32;
    list->pstate = state;
    list->listcount = 0;
    list->listcapacity = 0;
    list->listitems = NULL;
    list->listname = NULL;
    if(initialsize > 0)
    {
        nn_valarray_ensurecapacity(list, initialsize, nn_value_makenull(), true);
    }
}

NNValArray* nn_valarray_make(NNState* state)
{
    NNValArray* list;
    list = (NNValArray*)nn_memory_malloc(sizeof(NNValArray));
    nn_valarray_init(state, list);
    return list;
}

void nn_valarray_destroy(NNValArray* list, bool actuallydelete)
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
        if(actuallydelete)
        {
            nn_memory_free(list);
        }
        list = NULL;
    }
}

NEON_INLINE void nn_valarray_setcount(NNValArray* list, size_t cnt)
{
    list->listcount = cnt;
}

NEON_INLINE void nn_valarray_increaseby(NNValArray* list, size_t cnt)
{
    list->listcount += cnt;
}
    
NEON_INLINE void nn_valarray_decreaseby(NNValArray* list, size_t cnt)
{
    list->listcount -= cnt;
}

NEON_INLINE size_t nn_valarray_count(NNValArray* list)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, 0);
    return list->listcount;
}

NEON_INLINE size_t nn_valarray_capacity(NNValArray* list)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, 0);
    return list->listcapacity;
}

NEON_INLINE NNValue* nn_valarray_data(NNValArray* list)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, NULL);
    return list->listitems;
}

NEON_INLINE NNValue nn_valarray_get(NNValArray* list, size_t idx)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, nn_value_makenull());
    return list->listitems[idx];
}

NEON_INLINE NNValue* nn_valarray_getp(NNValArray* list, size_t idx)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, NULL);
    return &list->listitems[idx];
}

NEON_INLINE void nn_valarray_mark(NNValArray* list)
{
    size_t i;
    NN_NULLPTRCHECK_RETURN(list);
    for(i=0; i<list->listcount; i++)
    {
        nn_gcmem_markvalue(list->pstate, list->listitems[i]);
    }
}

NEON_INLINE bool nn_valarray_push(NNValArray* list, NNValue value)
{
    size_t oldcap;
    NN_NULLPTRCHECK_RETURNVALUE(list, false);
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

NEON_INLINE bool nn_valarray_set(NNValArray* list, size_t idx, NNValue val)
{
    size_t need;
    NN_NULLPTRCHECK_RETURNVALUE(list, false);
    need = idx + 8;
    if(list->listcount == 0)
    {
        return nn_valarray_push(list, val);
    }
    if(((idx == 0) || (list->listcapacity == 0)) || (idx >= list->listcapacity))
    {
        if(!nn_valarray_ensurecapacity(list, need, nn_value_makenull(), false))
        {
            return false;
        }
    }
    list->listitems[idx] = val;
    if(idx > list->listcount)
    {
        list->listcount = idx;
    }
    return true;
}

NEON_INLINE bool nn_valarray_insert(NNValArray* list, NNValue val, size_t idx)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, false);
    return nn_valarray_set(list, idx, val);
}

NEON_INLINE bool nn_valarray_pop(NNValArray* list, NNValue* dest)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, false);
    if(list->listcount > 0)
    {
        *dest = list->listitems[list->listcount - 1];
        list->listcount--;
        return true;
    }
    return false;
}

NEON_INLINE bool nn_valarray_removeatintern(NNValArray* list, unsigned int ix)
{
    size_t tomovebytes;
    void* src;
    void* dest;
    NN_NULLPTRCHECK_RETURNVALUE(list, false);
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

NEON_INLINE bool nn_valarray_removeat(NNValArray* list, unsigned int ix)
{
    NN_NULLPTRCHECK_RETURNVALUE(list, false);
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
    return nn_valarray_removeatintern(list, ix);
}

NEON_INLINE bool nn_valarray_ensurecapacity(NNValArray* list, size_t needsize, NNValue fillval, bool first)
{
    size_t i;
    size_t ncap;
    size_t oldcap;
    (void)first;
    NN_NULLPTRCHECK_RETURNVALUE(list, false);
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
        if(list->listitems == NULL)
        {
            return false;
        }
        for(i = oldcap; i < ncap; i++)
        {
            list->listitems[i] = fillval;
        }
    }
    return true;
}

NEON_INLINE NNValArray* nn_valarray_copy(NNValArray* list)
{
    size_t i;
    NNValArray* nlist;
    NN_NULLPTRCHECK_RETURNVALUE(list, NULL);
    nlist = nn_valarray_make(list->pstate);
    for(i=0; i<list->listcount; i++)
    {
        nn_valarray_push(nlist, list->listitems[i]);
    }
    return nlist;
}

NEON_INLINE void nn_valarray_setempty(NNValArray* list)
{
    if((list->listcapacity > 0) && (list->listitems != NULL))
    {
        memset(list->listitems, 0, sizeof(NNValue) * list->listcapacity);
    }
    list->listcount = 0;
    list->listcapacity = 0;
}

