
#include "neon.h"

NNObjArray* nn_array_makefilled(NNState* state, size_t cnt, NNValue filler)
{
    size_t i;
    NNObjArray* list;
    list = (NNObjArray*)nn_object_allocobject(state, sizeof(NNObjArray), NEON_OBJTYPE_ARRAY, false);
    nn_valarray_init(state, &list->varray);
    if(cnt > 0)
    {
        for(i=0; i<cnt; i++)
        {
            nn_valarray_push(&list->varray, filler);
        }
    }
    return list;
}

NNObjArray* nn_array_make(NNState* state)
{
    return nn_array_makefilled(state, 0, nn_value_makenull());
}

NNObjArray* nn_object_makearray(NNState* state)
{
    return nn_array_make(state);
}

void nn_array_push(NNObjArray* list, NNValue value)
{
    NNState* state;
    (void)state;
    state = ((NNObject*)list)->pstate;
    /*nn_vm_stackpush(state, value);*/
    nn_valarray_push(&list->varray, value);
    /*nn_vm_stackpop(state); */
}

bool nn_array_get(NNObjArray* list, size_t idx, NNValue* vdest)
{
    size_t vc;
    vc = nn_valarray_count(&list->varray);
    if((vc > 0) && (idx < vc))
    {
        *vdest = nn_valarray_get(&list->varray, idx);
        return true;
    }
    return false;
}

NNObjArray* nn_array_copy(NNObjArray* list, long start, long length)
{
    size_t i;
    NNState* state;
    NNObjArray* newlist;
    state = ((NNObject*)list)->pstate;
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    if(start == -1)
    {
        start = 0;
    }
    if(length == -1)
    {
        length = nn_valarray_count(&list->varray) - start;
    }
    for(i = start; i < (size_t)(start + length); i++)
    {
        nn_array_push(newlist, nn_valarray_get(&list->varray, i));
    }
    return newlist;
}


NNValue nn_objfnarray_constructor(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int cnt;
    NNValue filler;
    NNObjArray* arr;
    NNArgCheck check;
    (void)thisval;
    nn_argcheck_init(state, &check, "constructor", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    filler = nn_value_makenull();
    if(argc > 1)
    {
        filler = argv[1];
    }
    cnt = nn_value_asnumber(argv[0]);
    arr = nn_array_makefilled(state, cnt, filler);
    return nn_value_fromobject(arr);
}

NNValue nn_objfnarray_join(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    bool havejoinee;
    size_t i;
    size_t count;
    NNPrinter pr;
    NNValue ret;
    NNValue vjoinee;
    NNObjArray* selfarr;
    NNObjString* joinee;
    NNValue* list;
    selfarr = nn_value_asarray(thisval);
    joinee = NULL;
    havejoinee = false;
    if(argc > 0)
    {
        vjoinee = argv[0];
        if(nn_value_isstring(vjoinee))
        {
            joinee = nn_value_asstring(vjoinee);
            havejoinee = true;
        }
        else
        {
            joinee = nn_value_tostring(state, vjoinee);
            havejoinee = true;
        }
    }
    list = selfarr->varray.listitems;
    count = nn_valarray_count(&selfarr->varray);
    if(count == 0)
    {
        return nn_value_fromobject(nn_string_intern(state, ""));
    }
    nn_printer_makestackstring(state, &pr);
    for(i = 0; i < count; i++)
    {
        nn_printer_printvalue(&pr, list[i], false, true);
        if((havejoinee && (joinee != NULL)) && ((i+1) < count))
        {
            nn_printer_writestringl(&pr, nn_string_getdata(joinee), nn_string_getlength(joinee));
        }
    }
    ret = nn_value_fromobject(nn_printer_takestring(&pr));
    nn_printer_destroy(&pr);
    return ret;
}


NNValue nn_objfnarray_length(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* selfarr;
    (void)state;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "length", argv, argc);
    selfarr = nn_value_asarray(thisval);
    return nn_value_makenumber(nn_valarray_count(&selfarr->varray));
}

NNValue nn_objfnarray_append(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    (void)state;
    for(i = 0; i < argc; i++)
    {
        nn_array_push(nn_value_asarray(thisval), argv[i]);
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_clear(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "clear", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    nn_valarray_destroy(&nn_value_asarray(thisval)->varray, false);
    return nn_value_makenull();
}

NNValue nn_objfnarray_clone(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "clone", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    return nn_value_fromobject(nn_array_copy(list, 0, nn_valarray_count(&list->varray)));
}

NNValue nn_objfnarray_count(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    int count;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "count", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(thisval);
    count = 0;
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        if(nn_value_compare(state, nn_valarray_get(&list->varray, i), argv[0]))
        {
            count++;
        }
    }
    return nn_value_makenumber(count);
}

NNValue nn_objfnarray_extend(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjArray* list;
    NNObjArray* list2;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "extend", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isarray);
    list = nn_value_asarray(thisval);
    list2 = nn_value_asarray(argv[0]);
    for(i = 0; i < nn_valarray_count(&list2->varray); i++)
    {
        nn_array_push(list, nn_valarray_get(&list2->varray, i));
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_indexof(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "indexOf", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    list = nn_value_asarray(thisval);
    i = 0;
    if(argc == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        i = nn_value_asnumber(argv[1]);
    }
    for(; i < nn_valarray_count(&list->varray); i++)
    {
        if(nn_value_compare(state, nn_valarray_get(&list->varray, i), argv[0]))
        {
            return nn_value_makenumber(i);
        }
    }
    return nn_value_makenumber(-1);
}

NNValue nn_objfnarray_insert(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "insert", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 2);
    NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
    list = nn_value_asarray(thisval);
    index = (int)nn_value_asnumber(argv[1]);
    nn_valarray_insert(&list->varray, argv[0], index);
    return nn_value_makenull();
}


NNValue nn_objfnarray_pop(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNValue value;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "pop", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    if(nn_valarray_count(&list->varray) > 0)
    {
        value = nn_valarray_get(&list->varray, nn_valarray_count(&list->varray) - 1);
        nn_valarray_decreaseby(&list->varray, 1);
        return value;
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_shift(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t j;
    size_t count;
    NNObjArray* list;
    NNObjArray* newlist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "shift", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 0, 1);
    count = 1;
    if(argc == 1)
    {
        NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
        count = nn_value_asnumber(argv[0]);
    }
    list = nn_value_asarray(thisval);
    if(count >= nn_valarray_count(&list->varray) || nn_valarray_count(&list->varray) == 1)
    {
        nn_valarray_setcount(&list->varray, 0);
        return nn_value_makenull();
    }
    else if(count > 0)
    {
        newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
        for(i = 0; i < count; i++)
        {
            nn_array_push(newlist, nn_valarray_get(&list->varray, 0));
            for(j = 0; j < nn_valarray_count(&list->varray); j++)
            {
                nn_valarray_set(&list->varray, j, nn_valarray_get(&list->varray, j + 1));
            }
            nn_valarray_decreaseby(&list->varray, 1);
        }
        if(count == 1)
        {
            return nn_valarray_get(&newlist->varray, 0);
        }
        else
        {
            return nn_value_fromobject(newlist);
        }
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_removeat(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t index;
    NNValue value;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "removeAt", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(thisval);
    index = nn_value_asnumber(argv[0]);
    if(((int)index < 0) || index >= nn_valarray_count(&list->varray))
    {
        NEON_RETURNERROR("list index %d out of range at remove_at()", index);
    }
    value = nn_valarray_get(&list->varray, index);
    for(i = index; i < nn_valarray_count(&list->varray) - 1; i++)
    {
        nn_valarray_set(&list->varray, i, nn_valarray_get(&list->varray, i + 1));
    }
    nn_valarray_decreaseby(&list->varray, 1);
    return value;
}

NNValue nn_objfnarray_remove(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "remove", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(thisval);
    index = -1;
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        if(nn_value_compare(state, nn_valarray_get(&list->varray, i), argv[0]))
        {
            index = i;
            break;
        }
    }
    if((int)index != -1)
    {
        for(i = index; i < nn_valarray_count(&list->varray); i++)
        {
            nn_valarray_set(&list->varray, i, nn_valarray_get(&list->varray, i + 1));
        }
        nn_valarray_decreaseby(&list->varray, 1);
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_reverse(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int fromtop;
    NNObjArray* list;
    NNObjArray* nlist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "reverse", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    nlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    /* in-place reversal:*/
    /*
    int start = 0;
    int end = nn_valarray_count(&list->varray) - 1;
    while (start < end)
    {
        NNValue temp = nn_valarray_get(&list->varray, start);
        nn_valarray_set(&list->varray, start, nn_valarray_get(&list->varray, end));
        nn_valarray_set(&list->varray, end, temp);
        start++;
        end--;
    }
    */
    for(fromtop = nn_valarray_count(&list->varray) - 1; fromtop >= 0; fromtop--)
    {
        nn_array_push(nlist, nn_valarray_get(&list->varray, fromtop));
    }
    return nn_value_fromobject(nlist);
}

NNValue nn_objfnarray_sort(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "sort", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    nn_value_sortvalues(state, list->varray.listitems, nn_valarray_count(&list->varray));
    return nn_value_makenull();
}

NNValue nn_objfnarray_contains(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "contains", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(thisval);
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        if(nn_value_compare(state, argv[0], nn_valarray_get(&list->varray, i)))
        {
            return nn_value_makebool(true);
        }
    }
    return nn_value_makebool(false);
}

NNValue nn_objfnarray_delete(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t idxupper;
    size_t idxlower;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "delete", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    idxlower = nn_value_asnumber(argv[0]);
    idxupper = idxlower;
    if(argc == 2)
    {
        NEON_ARGS_CHECKTYPE(&check, 1, nn_value_isnumber);
        idxupper = nn_value_asnumber(argv[1]);
    }
    list = nn_value_asarray(thisval);
    if(((int)idxlower < 0) || idxlower >= nn_valarray_count(&list->varray))
    {
        NEON_RETURNERROR("list index %d out of range at delete()", idxlower);
    }
    else if(idxupper < idxlower || idxupper >= nn_valarray_count(&list->varray))
    {
        NEON_RETURNERROR("invalid upper limit %d at delete()", idxupper);
    }
    for(i = 0; i < nn_valarray_count(&list->varray) - idxupper; i++)
    {
        nn_valarray_set(&list->varray, idxlower + i, nn_valarray_get(&list->varray, i + idxupper + 1));
    }
    nn_valarray_decreaseby(&list->varray, idxupper - idxlower + 1);
    return nn_value_makenumber((double)idxupper - (double)idxlower + 1);
}

NNValue nn_objfnarray_first(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "first", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    if(nn_valarray_count(&list->varray) > 0)
    {
        return nn_valarray_get(&list->varray, 0);
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_last(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "last", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    if(nn_valarray_count(&list->varray) > 0)
    {
        return nn_valarray_get(&list->varray, nn_valarray_count(&list->varray) - 1);
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_isempty(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    NNArgCheck check;
    nn_argcheck_init(state, &check, "isEmpty", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    return nn_value_makebool(nn_valarray_count(&nn_value_asarray(thisval)->varray) == 0);
}


NNValue nn_objfnarray_take(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t count;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "take", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(thisval);
    count = nn_value_asnumber(argv[0]);
    if((int)count < 0)
    {
        count = nn_valarray_count(&list->varray) + count;
    }
    if(nn_valarray_count(&list->varray) < count)
    {
        return nn_value_fromobject(nn_array_copy(list, 0, nn_valarray_count(&list->varray)));
    }
    return nn_value_fromobject(nn_array_copy(list, 0, count));
}

NNValue nn_objfnarray_get(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "get", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(thisval);
    index = nn_value_asnumber(argv[0]);
    if((int)index < 0 || index >= nn_valarray_count(&list->varray))
    {
        return nn_value_makenull();
    }
    return nn_valarray_get(&list->varray, index);
}

NNValue nn_objfnarray_compact(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjArray* list;
    NNObjArray* newlist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "compact", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        if(!nn_value_compare(state, nn_valarray_get(&list->varray, i), nn_value_makenull()))
        {
            nn_array_push(newlist, nn_valarray_get(&list->varray, i));
        }
    }
    return nn_value_fromobject(newlist);
}


NNValue nn_objfnarray_unique(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t j;
    bool found;
    NNObjArray* list;
    NNObjArray* newlist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "unique", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    list = nn_value_asarray(thisval);
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        found = false;
        for(j = 0; j < nn_valarray_count(&newlist->varray); j++)
        {
            if(nn_value_compare(state, nn_valarray_get(&newlist->varray, j), nn_valarray_get(&list->varray, i)))
            {
                found = true;
                continue;
            }
        }
        if(!found)
        {
            nn_array_push(newlist, nn_valarray_get(&list->varray, i));
        }
    }
    return nn_value_fromobject(newlist);
}

NNValue nn_objfnarray_zip(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t j;
    NNObjArray* list;
    NNObjArray* newlist;
    NNObjArray* alist;
    NNObjArray** arglist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "zip", argv, argc);
    list = nn_value_asarray(thisval);
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    arglist = (NNObjArray**)nn_gcmem_allocate(state, sizeof(NNObjArray*), argc, false);
    for(i = 0; i < argc; i++)
    {
        NEON_ARGS_CHECKTYPE(&check, i, nn_value_isarray);
        arglist[i] = nn_value_asarray(argv[i]);
    }
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        alist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
        /* item of main list*/
        nn_array_push(alist, nn_valarray_get(&list->varray, i));
        for(j = 0; j < argc; j++)
        {
            if(i < nn_valarray_count(&arglist[j]->varray))
            {
                nn_array_push(alist, nn_valarray_get(&arglist[j]->varray, i));
            }
            else
            {
                nn_array_push(alist, nn_value_makenull());
            }
        }
        nn_array_push(newlist, nn_value_fromobject(alist));
    }
    return nn_value_fromobject(newlist);
}


NNValue nn_objfnarray_zipfrom(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t j;
    NNObjArray* list;
    NNObjArray* newlist;
    NNObjArray* alist;
    NNObjArray* arglist;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "zipFrom", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isarray);
    list = nn_value_asarray(thisval);
    newlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    arglist = nn_value_asarray(argv[0]);
    for(i = 0; i < nn_valarray_count(&arglist->varray); i++)
    {
        if(!nn_value_isarray(nn_valarray_get(&arglist->varray, i)))
        {
            NEON_RETURNERROR("invalid list in zip entries");
        }
    }
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        alist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
        nn_array_push(alist, nn_valarray_get(&list->varray, i));
        for(j = 0; j < nn_valarray_count(&arglist->varray); j++)
        {
            if(i < nn_valarray_count(&nn_value_asarray(nn_valarray_get(&arglist->varray, j))->varray))
            {
                nn_array_push(alist, nn_valarray_get(&nn_value_asarray(nn_valarray_get(&arglist->varray, j))->varray, i));
            }
            else
            {
                nn_array_push(alist, nn_value_makenull());
            }
        }
        nn_array_push(newlist, nn_value_fromobject(alist));
    }
    return nn_value_fromobject(newlist);
}

NNValue nn_objfnarray_todict(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    NNObjDict* dict;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "toDict", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 0);
    dict = (NNObjDict*)nn_gcmem_protect(state, (NNObject*)nn_object_makedict(state));
    list = nn_value_asarray(thisval);
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        nn_dict_setentry(dict, nn_value_makenumber(i), nn_valarray_get(&list->varray, i));
    }
    return nn_value_fromobject(dict);
}

NNValue nn_objfnarray_iter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "iter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    list = nn_value_asarray(thisval);
    index = nn_value_asnumber(argv[0]);
    if(((int)index > -1) && index < nn_valarray_count(&list->varray))
    {
        return nn_valarray_get(&list->varray, index);
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_itern(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t index;
    NNObjArray* list;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "itern", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    list = nn_value_asarray(thisval);
    if(nn_value_isnull(argv[0]))
    {
        if(nn_valarray_count(&list->varray) == 0)
        {
            return nn_value_makebool(false);
        }
        return nn_value_makenumber(0);
    }
    if(!nn_value_isnumber(argv[0]))
    {
        NEON_RETURNERROR("lists are numerically indexed");
    }
    index = nn_value_asnumber(argv[0]);
    if(index < nn_valarray_count(&list->varray) - 1)
    {
        return nn_value_makenumber((double)index + 1);
    }
    return nn_value_makenull();
}

NNValue nn_objfnarray_each(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue callable;
    NNValue unused;
    NNObjArray* list;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "each", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        passi = 0;
        if(arity > 0)
        {
            passi++;
            nestargs[0] = nn_valarray_get(&list->varray, i);
            if(arity > 1)
            {
                passi++;
                nestargs[1] = nn_value_makenumber(i);
            }
        }
        nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &unused, false);
    }
    return nn_value_makenull();
}


NNValue nn_objfnarray_map(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue res;
    NNValue callable;
    NNObjArray* list;
    NNObjArray* resultlist;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "map", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    resultlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        passi = 0;
        if(!nn_value_isnull(nn_valarray_get(&list->varray, i)))
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = nn_valarray_get(&list->varray, i);
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &res, false);
            nn_array_push(resultlist, res);
        }
        else
        {
            nn_array_push(resultlist, nn_value_makenull());
        }
    }
    return nn_value_fromobject(resultlist);
}


NNValue nn_objfnarray_filter(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue callable;
    NNValue result;
    NNObjArray* list;
    NNObjArray* resultlist;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "filter", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    resultlist = (NNObjArray*)nn_gcmem_protect(state, (NNObject*)nn_object_makearray(state));
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        passi = 0;
        if(!nn_value_isnull(nn_valarray_get(&list->varray, i)))
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = nn_valarray_get(&list->varray, i);
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result, false);
            if(!nn_value_isfalse(result))
            {
                nn_array_push(resultlist, nn_valarray_get(&list->varray, i));
            }
        }
    }
    return nn_value_fromobject(resultlist);
}

NNValue nn_objfnarray_some(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue callable;
    NNValue result;
    NNObjArray* list;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "some", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        passi = 0;
        if(!nn_value_isnull(nn_valarray_get(&list->varray, i)))
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = nn_valarray_get(&list->varray, i);
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result, false);
            if(!nn_value_isfalse(result))
            {
                return nn_value_makebool(true);
            }
        }
    }
    return nn_value_makebool(false);
}


NNValue nn_objfnarray_every(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    size_t arity;
    NNValue result;
    NNValue callable;
    NNObjArray* list;
    NNArgCheck check;
    NNValue nestargs[3];
    nn_argcheck_init(state, &check, "every", argv, argc);
    NEON_ARGS_CHECKCOUNT(&check, 1);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(thisval);
    callable = argv[0];
    arity = nn_nestcall_prepare(state, callable, thisval, nestargs, 2);
    for(i = 0; i < nn_valarray_count(&list->varray); i++)
    {
        passi = 0;
        if(!nn_value_isnull(nn_valarray_get(&list->varray, i)))
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = nn_valarray_get(&list->varray, i);
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = nn_value_makenumber(i);
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &result, false);
            if(nn_value_isfalse(result))
            {
                return nn_value_makebool(false);
            }
        }
    }
    return nn_value_makebool(true);
}

NNValue nn_objfnarray_reduce(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    size_t i;
    size_t passi;
    size_t arity;
    size_t startindex;
    NNValue callable;
    NNValue accumulator;
    NNObjArray* list;
    NNArgCheck check;
    NNValue nestargs[5];
    nn_argcheck_init(state, &check, "reduce", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_iscallable);
    list = nn_value_asarray(thisval);
    callable = argv[0];
    startindex = 0;
    accumulator = nn_value_makenull();
    if(argc == 2)
    {
        accumulator = argv[1];
    }
    if(nn_value_isnull(accumulator) && nn_valarray_count(&list->varray) > 0)
    {
        accumulator = nn_valarray_get(&list->varray, 0);
        startindex = 1;
    }
    arity = nn_nestcall_prepare(state, callable, thisval, NULL, 4);
    for(i = startindex; i < nn_valarray_count(&list->varray); i++)
    {
        passi = 0;
        if(!nn_value_isnull(nn_valarray_get(&list->varray, i)) && !nn_value_isnull(nn_valarray_get(&list->varray, i)))
        {
            if(arity > 0)
            {
                passi++;
                nestargs[0] = accumulator;
                if(arity > 1)
                {
                    passi++;
                    nestargs[1] = nn_valarray_get(&list->varray, i);
                    if(arity > 2)
                    {
                        passi++;
                        nestargs[2] = nn_value_makenumber(i);
                        if(arity > 4)
                        {
                            passi++;
                            nestargs[3] = thisval;
                        }
                    }
                }
            }
            nn_nestcall_callfunction(state, callable, thisval, nestargs, passi, &accumulator, false);
        }
    }
    return accumulator;
}

NNValue nn_objfnarray_slice(NNState* state, NNValue thisval, NNValue* argv, size_t argc)
{
    int64_t i;
    int64_t until;
    int64_t start;
    int64_t end;
    int64_t ibegin;
    int64_t iend;
    int64_t salen;
    bool backwards;
    NNObjArray* selfarr;
    NNObjArray* narr;
    NNArgCheck check;
    nn_argcheck_init(state, &check, "slice", argv, argc);
    NEON_ARGS_CHECKCOUNTRANGE(&check, 1, 2);
    NEON_ARGS_CHECKTYPE(&check, 0, nn_value_isnumber);
    selfarr = nn_value_asarray(thisval);
    salen = nn_valarray_count(&selfarr->varray);
    end = salen;
    start = nn_value_asnumber(argv[0]);
    backwards = false;
    if(start < 0)
    {
        backwards = true;
    }
    if(argc > 1)
    {
        end = nn_value_asnumber(argv[1]);
    }
    narr = nn_object_makearray(state);
    i = 0;
    if(backwards)
    {
        i = (end - start);
        until = 0;
        ibegin = ((salen + start)-0);
        iend = end+0;
    }
    else
    {
        until = end;
        ibegin = start;
        iend = until;
    }
    for(i=ibegin; i!=iend; i++)
    {
        nn_array_push(narr, nn_valarray_get(&selfarr->varray, i));        
    }
    return nn_value_fromobject(narr);
}

void nn_state_installobjectarray(NNState* state)
{
    static NNConstClassMethodItem arraymethods[] =
    {
        {"size", nn_objfnarray_length},
        {"join", nn_objfnarray_join},
        {"append", nn_objfnarray_append},
        {"push", nn_objfnarray_append},
        {"clear", nn_objfnarray_clear},
        {"clone", nn_objfnarray_clone},
        {"count", nn_objfnarray_count},
        {"extend", nn_objfnarray_extend},
        {"indexOf", nn_objfnarray_indexof},
        {"insert", nn_objfnarray_insert},
        {"pop", nn_objfnarray_pop},
        {"shift", nn_objfnarray_shift},
        {"removeAt", nn_objfnarray_removeat},
        {"remove", nn_objfnarray_remove},
        {"reverse", nn_objfnarray_reverse},
        {"sort", nn_objfnarray_sort},
        {"contains", nn_objfnarray_contains},
        {"delete", nn_objfnarray_delete},
        {"first", nn_objfnarray_first},
        {"last", nn_objfnarray_last},
        {"isEmpty", nn_objfnarray_isempty},
        {"take", nn_objfnarray_take},
        {"get", nn_objfnarray_get},
        {"compact", nn_objfnarray_compact},
        {"unique", nn_objfnarray_unique},
        {"zip", nn_objfnarray_zip},
        {"zipFrom", nn_objfnarray_zipfrom},
        {"toDict", nn_objfnarray_todict},
        {"each", nn_objfnarray_each},
        {"map", nn_objfnarray_map},
        {"filter", nn_objfnarray_filter},
        {"some", nn_objfnarray_some},
        {"every", nn_objfnarray_every},
        {"reduce", nn_objfnarray_reduce},
        {"slice", nn_objfnarray_slice},
        {"@iter", nn_objfnarray_iter},
        {"@itern", nn_objfnarray_itern},
        {NULL, NULL}
    };
    nn_class_defnativeconstructor(state->classprimarray, nn_objfnarray_constructor);
    nn_class_defcallablefield(state->classprimarray, nn_string_intern(state, "length"), nn_objfnarray_length);
    nn_state_installmethods(state, state->classprimarray, arraymethods);

}
