
bool nn_valdict_init(NNState* state, NNHashPtrTable* dict, unsigned int initialcapacity, size_t ktsz, size_t vtsz)
{
    unsigned int i;
    dict->pstate = state;
    dict->keytypesize = ktsz;
    dict->valtypesize = vtsz;
    dict->vdcells = NULL;
    dict->vdkeys = NULL;
    dict->vdvalues = NULL;
    dict->vdcellindices = NULL;
    dict->vdhashes = NULL;
    dict->vdcount = 0;
    dict->vdcellcapacity = initialcapacity;
    dict->vditemcapacity = (unsigned int)(initialcapacity * 0.7f);
    dict->funckeyequalsfn = NULL;
    dict->funchashfn = NULL;
    dict->vdcells = (unsigned int*)nn_memory_malloc(dict->vdcellcapacity * sizeof(unsigned int));
    dict->vdkeys = (char**)nn_memory_malloc(dict->vditemcapacity * dict->keytypesize);
    dict->vdvalues = (void**)nn_memory_malloc(dict->vditemcapacity * dict->valtypesize);
    dict->vdcellindices = (unsigned int*)nn_memory_malloc(dict->vditemcapacity * sizeof(unsigned int));
    dict->vdhashes = (long unsigned int*)nn_memory_malloc(dict->vditemcapacity * sizeof(unsigned long));
    if(dict->vdcells == NULL || dict->vdkeys == NULL || dict->vdvalues == NULL || dict->vdcellindices == NULL || dict->vdhashes == NULL)
    {
        goto dictallocfailed;
    }
    for(i = 0; i < dict->vdcellcapacity; i++)
    {
        dict->vdcells[i] = NEON_CONFIG_VALDICTINVALIDIX;
    }
    return true;
dictallocfailed:
    nn_memory_free(dict->vdcells);
    nn_memory_free(dict->vdkeys);
    nn_memory_free(dict->vdvalues);
    nn_memory_free(dict->vdcellindices);
    nn_memory_free(dict->vdhashes);
    return false;
}

void nn_valdict_deinit(NNHashPtrTable* dict)
{
    dict->keytypesize = 0;
    dict->valtypesize = 0;
    dict->vdcount = 0;
    dict->vditemcapacity = 0;
    dict->vdcellcapacity = 0;
    nn_memory_free(dict->vdcells);
    nn_memory_free(dict->vdkeys);
    nn_memory_free(dict->vdvalues);
    nn_memory_free(dict->vdcellindices);
    nn_memory_free(dict->vdhashes);
    dict->vdcells = NULL;
    dict->vdkeys = NULL;
    dict->vdvalues = NULL;
    dict->vdcellindices = NULL;
    dict->vdhashes = NULL;
}

NNHashPtrTable* nn_valdict_makedefault(NNState* state, size_t ktsz, size_t vtsz)
{
    return nn_valdict_makecapacity(state, NEON_CONFIG_GENERICDICTINITSIZE, ktsz, vtsz);
}

NNHashPtrTable* nn_valdict_makecapacity(NNState* state, unsigned int mincapacity, size_t ktsz, size_t vtsz)
{
    bool ok;
    unsigned int capacity;
    NNHashPtrTable* dict;
    capacity = nn_util_upperpowoftwo(mincapacity * 2);
    dict = (NNHashPtrTable*)nn_memory_malloc(sizeof(NNHashPtrTable));
    if(!dict)
    {
        return NULL;
    }
    ok = nn_valdict_init(state, dict, capacity, ktsz, vtsz);
    if(!ok)
    {
        nn_memory_free(dict);
        return NULL;
    }
    dict->pstate = state;
    return dict;
}

void nn_valdict_destroy(NNHashPtrTable* dict)
{
    if(!dict)
    {
        return;
    }
    nn_valdict_deinit(dict);
    nn_memory_free(dict);
}

NEON_INLINE void nn_valdict_sethashfunction(NNHashPtrTable* dict, mcitemhashfn_t hashfn)
{
    dict->funchashfn = hashfn;
}

NEON_INLINE void nn_valdict_setequalsfunction(NNHashPtrTable* dict, mcitemcomparefn_t equalsfn)
{
    dict->funckeyequalsfn = equalsfn;
}

bool nn_valdict_setkvintern(NNHashPtrTable* dict, unsigned int cellix, unsigned long hash, void* key, void* value)
{
    bool ok;
    bool found;
    unsigned int lastix;
    if(dict->vdcount >= dict->vditemcapacity)
    {
        ok = nn_valdict_growandrehash(dict);
        if(!ok)
        {
            return false;
        }
        cellix = nn_valdict_getcellindex(dict, key, hash, &found);
    }
    lastix = dict->vdcount;
    dict->vdcount++;
    dict->vdcells[cellix] = lastix;
    nn_valdict_setkeyat(dict, lastix, key);
    nn_valdict_setvalueat(dict, lastix, value);
    dict->vdcellindices[lastix] = cellix;
    dict->vdhashes[lastix] = hash;
    return true;
}

NEON_INLINE bool nn_valdict_setkv(NNHashPtrTable* dict, void* key, void* value)
{
    bool found;
    unsigned long hash;
    unsigned int cellix;
    unsigned int itemix;
    hash = nn_valdict_hashkey(dict, key);
    found = false;
    cellix = nn_valdict_getcellindex(dict, key, hash, &found);
    if(found)
    {
        itemix = dict->vdcells[cellix];
        nn_valdict_setvalueat(dict, itemix, value);
        return true;
    }
    return nn_valdict_setkvintern(dict, cellix, hash, key, value);
}

NEON_INLINE void* nn_valdict_get(NNHashPtrTable* dict, void* key)
{
    bool found;
    unsigned int itemix;
    unsigned long hash;
    unsigned long cellix;
    if(dict->vdcount == 0)
    {
        return NULL;
    }
    hash = nn_valdict_hashkey(dict, key);
    found = false;
    cellix = nn_valdict_getcellindex(dict, key, hash, &found);
    if(!found)
    {
        return NULL;
    }
    itemix = dict->vdcells[cellix];
    return nn_valdict_getvalueat(dict, itemix);
}

NEON_INLINE void* nn_valdict_getkeyat(NNHashPtrTable* dict, unsigned int ix)
{
    if(ix >= dict->vdcount)
    {
        return NULL;
    }
    return (char*)dict->vdkeys + (dict->keytypesize * ix);
}

NEON_INLINE void* nn_valdict_getvalueat(NNHashPtrTable* dict, unsigned int ix)
{
    if(ix >= dict->vdcount)
    {
        return NULL;
    }
    return (char*)dict->vdvalues + (dict->valtypesize * ix);
}

NEON_INLINE unsigned int nn_valdict_getcapacity(NNHashPtrTable* dict)
{
    return dict->vditemcapacity;
}

NEON_INLINE bool nn_valdict_setvalueat(NNHashPtrTable* dict, unsigned int ix, void* value)
{
    size_t offset;
    if(ix >= dict->vdcount)
    {
        return false;
    }
    offset = ix * dict->valtypesize;
    memcpy((char*)dict->vdvalues + offset, value, dict->valtypesize);
    return true;
}

NEON_INLINE int nn_valdict_count(NNHashPtrTable* dict)
{
    if(!dict)
    {
        return 0;
    }
    return dict->vdcount;
}

NEON_INLINE bool nn_valdict_removebykey(NNHashPtrTable* dict, void* key)
{
    bool found;
    unsigned int x;
    unsigned int k;
    unsigned int i;
    unsigned int j;
    unsigned int cell;
    unsigned int itemix;
    unsigned int lastitemix;
    unsigned long hash;
    void* lastkey;
    void* lastvalue;
    hash = nn_valdict_hashkey(dict, key);
    found = false;
    cell = nn_valdict_getcellindex(dict, key, hash, &found);
    if(!found)
    {
        return false;
    }
    itemix = dict->vdcells[cell];
    lastitemix = dict->vdcount - 1;
    if(itemix < lastitemix)
    {
        lastkey = nn_valdict_getkeyat(dict, lastitemix);
        nn_valdict_setkeyat(dict, itemix, lastkey);
        lastvalue = nn_valdict_getkeyat(dict, lastitemix);
        nn_valdict_setvalueat(dict, itemix, lastvalue);
        dict->vdcellindices[itemix] = dict->vdcellindices[lastitemix];
        dict->vdhashes[itemix] = dict->vdhashes[lastitemix];
        dict->vdcells[dict->vdcellindices[itemix]] = itemix;
    }
    dict->vdcount--;
    i = cell;
    j = i;
    for(x = 0; x < (dict->vdcellcapacity - 1); x++)
    {
        j = (j + 1) & (dict->vdcellcapacity - 1);
        if(dict->vdcells[j] == NEON_CONFIG_VALDICTINVALIDIX)
        {
            break;
        }
        k = (unsigned int)(dict->vdhashes[dict->vdcells[j]]) & (dict->vdcellcapacity - 1);
        if((j > i && (k <= i || k > j)) || (j < i && (k <= i && k > j)))
        {
            dict->vdcellindices[dict->vdcells[j]] = i;
            dict->vdcells[i] = dict->vdcells[j];
            i = j;
        }
    }
    dict->vdcells[i] = NEON_CONFIG_VALDICTINVALIDIX;
    return true;
}

NEON_INLINE void nn_valdict_clear(NNHashPtrTable* dict)
{
    unsigned int i;
    dict->vdcount = 0;
    for(i = 0; i < dict->vdcellcapacity; i++)
    {
        dict->vdcells[i] = NEON_CONFIG_VALDICTINVALIDIX;
    }
}

NEON_INLINE unsigned int nn_valdict_getcellindex(NNHashPtrTable* dict, void* key, unsigned long hash, bool* outfound)
{
    bool areequal;
    unsigned int i;
    unsigned int ix;
    unsigned int cell;
    unsigned int cellix;
    unsigned long hashtocheck;
    void* keytocheck;
    *outfound = false;
    cellix = (unsigned int)hash & (dict->vdcellcapacity - 1);
    for(i = 0; i < dict->vdcellcapacity; i++)
    {
        ix = (cellix + i) & (dict->vdcellcapacity - 1);
        cell = dict->vdcells[ix];
        if(cell == NEON_CONFIG_VALDICTINVALIDIX)
        {
            return ix;
        }
        hashtocheck = dict->vdhashes[cell];
        if(hash != hashtocheck)
        {
            continue;
        }
        keytocheck = nn_valdict_getkeyat(dict, cell);
        areequal = nn_valdict_keysareequal(dict, key, keytocheck);
        if(areequal)
        {
            *outfound = true;
            return ix;
        }
    }
    return NEON_CONFIG_VALDICTINVALIDIX;
}

NEON_INLINE bool nn_valdict_growandrehash(NNHashPtrTable* dict)
{
    bool ok;
    NNHashPtrTable newdict;
    unsigned int i;
    unsigned ncap;
    char* key;
    void* value;
    ncap = MC_UTIL_INCCAPACITY(dict->vdcellcapacity);    
    ok = nn_valdict_init(dict->pstate, &newdict, ncap, dict->keytypesize, dict->valtypesize);
    if(!ok)
    {
        return false;
    }
    newdict.funckeyequalsfn = dict->funckeyequalsfn;
    newdict.funchashfn = dict->funchashfn;
    for(i = 0; i < dict->vdcount; i++)
    {
        key = (char*)nn_valdict_getkeyat(dict, i);
        value = nn_valdict_getvalueat(dict, i);
        ok = nn_valdict_setkv(&newdict, key, value);
        
        if(!ok)
        {
            nn_valdict_deinit(&newdict);
            return false;
        }
    }
    nn_valdict_deinit(dict);
    *dict = newdict;
    return true;
}

bool nn_valdict_addall(NNHashPtrTable* to, NNHashPtrTable* from)
{
    bool ok;
    size_t i;
    char* key;
    void* value;
    (void)ok;
    for(i=0; i<from->vdcount; i++)
    {
        key = (char*)nn_valdict_getkeyat(from, i);
        value = nn_valdict_getvalueat(from, i);
        ok = nn_valdict_setkv(to, key, value);
    }
    return true;
}


NEON_INLINE bool nn_valdict_setkeyat(NNHashPtrTable* dict, unsigned int ix, void* key)
{
    size_t offset;
    if(ix >= dict->vdcount)
    {
        return false;
    }
    offset = ix * dict->keytypesize;
    memcpy((char*)dict->vdkeys + offset, key, dict->keytypesize);
    return true;
}

NEON_INLINE bool nn_valdict_keysareequal(NNHashPtrTable* dict, void* a, void* b)
{
    if(dict->funckeyequalsfn)
    {
        return dict->funckeyequalsfn(a, b);
    }
    return memcmp(a, b, dict->keytypesize) == 0;
}

NEON_INLINE unsigned long nn_valdict_hashkey(NNHashPtrTable* dict, void* key)
{
    if(dict->funchashfn)
    {
        return dict->funchashfn(key);
    }
    return nn_util_hashdata(key, dict->keytypesize);
}

