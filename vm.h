


static inline int32_t neon_vmbits_readinstruction(NeonState* state)
{
    int idx;
    int32_t r;
    int32_t* vp;
    NeonObjClosure* cls;
    (void)vp;
    cls = state->vmstate.activeframe->closure;
    idx = state->vmstate.activeframe->instrucidx;
    if(idx >= cls->fnptr->blob->count)
    {
        r = NEON_OP_HALTVM;
    }
    else
    {
        r = cls->fnptr->blob->bincode[idx];
    }
    state->vmstate.activeframe->instrucidx++;
    return r;
}
static inline int32_t neon_vmbits_readbyte(NeonState* state)
{
    int idx;
    int32_t r;
    int32_t* vp;
    NeonObjClosure* cls;
    (void)vp;
    cls = state->vmstate.activeframe->closure;
    idx = state->vmstate.activeframe->instrucidx;
    r = cls->fnptr->blob->bincode[idx];
    state->vmstate.activeframe->instrucidx++;
    return r;
}

/* Jumping Back and Forth read-short < Calls and Functions run */
static inline uint16_t neon_vmbits_readshort(NeonState* state)
{
    int idx;
    int32_t a;
    int32_t b;
    idx = state->vmstate.activeframe->instrucidx;
    a = state->vmstate.activeframe->closure->fnptr->blob->bincode[idx + 0];
    b = state->vmstate.activeframe->closure->fnptr->blob->bincode[idx + 1];
    state->vmstate.activeframe->instrucidx += 2;
    return (uint16_t)((a << 8) | b);
}

static inline NeonValue neon_vmbits_readconstbyindex(NeonState* state, int32_t idx)
{
    size_t vsz;
    NeonValArray* vaconst;
    (void)vsz;
    if(state->vmstate.activeframe->closure == NULL)
    {
        return neon_value_makenil();
    }
    vaconst = state->vmstate.activeframe->closure->fnptr->blob->constants;
    if(vaconst == NULL)
    {
        return neon_value_makenil();
    }
    vsz = vaconst->size;
    return (vaconst->values[idx]);

} 

/* Calls and Functions run < Closures read-constant */
static inline NeonValue neon_vmbits_readconst(NeonState* state)
{
    int32_t b;
    b = neon_vmbits_readbyte(state);
    return neon_vmbits_readconstbyindex(state, b);
}

static inline NeonObjString* neon_vmbits_readstring(NeonState* state)
{
    return neon_value_asstring(neon_vmbits_readconst(state));
}


bool neon_vmbits_docallclosurewrap(NeonState* state, NeonValue receiver, NeonObjClosure* closure, int argc, bool iseval)
{
    int fromtop;
    int vaargsstart;
    const char* fnname;
    NeonCallFrame* frame;
    NeonObjArray* argslist;
    (void)receiver;
    (void)iseval;
    fnname = "<closure>";
    if(closure->fnptr != NULL)
    {
        if(closure->fnptr->name != NULL)
        {
            fnname = closure->fnptr->name->sbuf->data;
        }
    }
    // fill empty parameters if not variadic
    for(; !closure->fnptr->isvariadic && argc < closure->fnptr->arity; argc++)
    {
        neon_vm_stackpush(state, neon_value_makenil());
    }
    // handle variadic arguments...
    if(closure->fnptr->isvariadic && argc >= closure->fnptr->arity - 1)
    {
        vaargsstart = argc - closure->fnptr->arity;
        argslist = neon_object_makearray(state);
        neon_vm_stackpush(state, neon_value_fromobject(argslist));
        for(fromtop = vaargsstart; fromtop >= 0; fromtop--)
        {
            neon_valarray_push(argslist->vala, neon_vm_stackpeek(state, fromtop + 1));
        }
        argc -= vaargsstart;
        neon_vm_stackpopn(state, vaargsstart + 2);// +1 for the gc protection push above
        neon_vm_stackpush(state, neon_value_fromobject(argslist));
    }
    if(argc != closure->fnptr->arity)
    {
        neon_vm_stackpopn(state, argc);
        if(closure->fnptr->isvariadic)
        {
            neon_state_raiseerror(state, "function '%s' expected at least %d arguments but got %d", fnname, closure->fnptr->arity - 1, argc);
            return false;
        }
        else
        {
            neon_state_raiseerror(state, "function '%s' expected %d arguments but got %d", fnname, closure->fnptr->arity, argc);
            return false;
        }
    }
    frame = &state->vmstate.framevalues[state->vmstate.framecount++];
    frame->closure = closure;
    //frame->ip = closure->fnptr->blob.code;
    frame->instrucidx = 0;
    //frame->slots = state->vmstate.stacktop - argc - 1;
    frame->frstackindex = (state->vmstate.stacktop - argc) - 1;
    return true;
}

/*
bool neon_vmbits_docallclosurewrap(NeonState* state, NeonValue receiver, NeonObjClosure* closure, int argc, bool iseval)
{
    NeonCallFrame* frame;
    (void)receiver;
    #if 0
    if(argc != closure->fnptr->arity)
    {
        neon_state_raiseerror(state, "expected %d arguments, but got %d", closure->fnptr->arity, argc);
        return false;
    }
    #endif
    state->vmstate.iseval = iseval;
    state->vmstate.havekeeper = iseval;
    if(iseval)
    {
        state->vmstate.keepframe = *state->vmstate.activeframe;
    }
    neon_vm_framesmaybegrow(state, state->vmstate.framecount + 0);
    frame = &state->vmstate.framevalues[state->vmstate.framecount];
    state->vmstate.framecount++;
    frame->closure = closure;
    frame->instrucidx = 0;
    frame->frstackindex = (state->vmstate.stacktop - argc) - 1;
    return true;
}
*/

bool neon_vmbits_docallclosure(NeonState* state, NeonValue receiver, NeonObjClosure* closure, int argc)
{
    return neon_vmbits_docallclosurewrap(state, receiver, closure, argc, false);
}


bool neon_vmbits_callnativefunction(NeonState* state, NeonValue receiver, NeonValue callee, int argc)
{
    NeonValue result;
    NeonValue* vargs;
    NeonNativeFN cfunc;
    NeonObjNativeFunction* nfn;
    nfn = (NeonObjNativeFunction*)neon_value_asobject(callee);
    cfunc = nfn->natfunc;
    vargs = (&state->vmstate.stackvalues[0] + state->vmstate.stacktop) - argc;
    result = cfunc(state, receiver, argc, vargs);
    state->vmstate.stacktop -= argc + 1;
    neon_vmbits_stackpush(state, result);
    if(state->vmstate.hasraised)
    {
        return false;
    }
    return true;
}

bool neon_vmbits_callvalue(NeonState* state, NeonValue receiver, NeonValue callee, int argc)
{
    if(neon_value_isobject(callee))
    {
        neon_vmbits_debugcall(state, receiver, callee, argc);
        switch(neon_value_objtype(callee))
        {
            case NEON_OBJ_BOUNDMETHOD:
                {
                    return neon_vmbits_callboundmethod(state, receiver, callee, argc);
                }
                break;
            case NEON_OBJ_CLASS:
                {
                    return neon_vmbits_callclassconstructor(state, receiver, callee, argc);
                }
                break;
            case NEON_OBJ_CLOSURE:
                {
                    return neon_vmbits_docallclosure(state, receiver, neon_value_asclosure(callee), argc);
                }
                break;
                /* Calls and Functions call-value < Closures call-value-closure */
                #if 0
            case NEON_OBJ_FUNCTION: // [switch]
                {
                    return neon_vmbits_docallclosure(state, receiver, neon_value_asscriptfunction(callee), argc);
                }
                break;
                #endif
            case NEON_OBJ_NATIVE:
                {
                    return neon_vmbits_callnativefunction(state, receiver, callee, argc);
                }
                break;
            default:
                // Non-callable object type.
                break;
        }
    }
    neon_state_raiseerror(state, "cannot call object type <%s>", neon_value_typename(callee));
    return false;
}



bool neon_vmbits_callprogramclosure(NeonState* state, NeonObjClosure* closure, bool iseval)
{
    bool rt;
    rt = neon_vmbits_docallclosurewrap(state, neon_value_makenil(), closure, 0, iseval);
    return rt;
}

bool neon_vmbits_callboundmethod(NeonState* state, NeonValue receiver, NeonValue callee, int argc)
{
    int ofs;
    NeonObjBoundFunction* bound;
    bound = neon_value_asboundfunction(callee);
    ofs = state->vmstate.stacktop + (-argc - 1);
    neon_writer_valfmt(state->stderrwriter, "bound->receiver=@\n", bound->receiver);
    state->vmstate.stackvalues[ofs] = bound->receiver;    
    return neon_vmbits_docallclosure(state, receiver, bound->method, argc);
}

bool neon_vmbits_callclassconstructor(NeonState* state, NeonValue receiver, NeonValue callee, int argc)
{
    NeonValue initializer;
    NeonObjClass* klass;
    NeonObjInstance* instance;
    klass = neon_value_asclass(callee);
    instance = neon_object_makeinstance(state, klass);
    state->vmstate.stackvalues[state->vmstate.stacktop + (-argc - 1)] = neon_value_fromobject(instance);
    //fprintf(stderr, "in callclassconstructor...\n");
    if(neon_hashtable_get(klass->methods, state->initstring, &initializer))
    {
        //fprintf(stderr, "in callclassconstructor...: received method\n");
        return neon_vmbits_callvalue(state, receiver, initializer, argc);
    }
    //fprintf(stderr, "in callclassconstructor...: failed to get method\n");
    #if 0
    else if(argc != 0)
    {
        neon_state_raiseerror(state, "expected 0 arguments but got %d.", argc);
        return false;
    }
    #endif
    return true;
}

void neon_vmbits_debugcall(NeonState* state, NeonValue receiver, NeonValue callee, int argc)
{
    size_t i;
    NeonValue* vargs;
    vargs = (&state->vmstate.stackvalues[0] + state->vmstate.stacktop) - argc;
    if(state->conf.shouldprintruntime)
    {
        neon_writer_writefmt(state->stderrwriter, "*debug* neon_vmbits_callvalue [\n");
        neon_writer_valfmt(state->stderrwriter,   "    callee: [<%s>] %q\n", neon_value_typename(callee), callee);
        neon_writer_writefmt(state->stderrwriter, "    argc: %d\n", argc);
        neon_writer_valfmt(state->stderrwriter,   "    receiver: [<%s>] %q\n", neon_value_typename(receiver), receiver);
        neon_writer_writefmt(state->stderrwriter, "    argv: [\n");
        for(i=0; i<(size_t)argc; i++)
        {
            neon_writer_valfmt(state->stderrwriter, "        [%d] [<%s>] %q\n", (int)i, neon_value_typename(vargs[i]), vargs[i]);
        }
        neon_writer_writefmt(state->stderrwriter, "    ]\n");
    }
}

bool neon_vmbits_invokefromclass(NeonState* state, NeonValue receiver, NeonObjClass* klass, NeonObjString* name, int argc)
{
    NeonValue method;
    if(!neon_hashtable_get(klass->methods, name, &method))
    {
        neon_state_raiseerror(state, "undefined property '%s'.", name->sbuf->data);
        return false;
    }
    return neon_vmbits_docallclosure(state, receiver, neon_value_asclosure(method), argc);
}

NeonObjClass* neon_value_getvalueclass(NeonState* state, NeonValue val)
{
    NeonObjClass* pc;
    if(neon_value_isclass(val))
    {
        pc = neon_value_asclass(val);
        if(pc == state->objvars.classprimobject)
        {
            return state->objvars.classprimobject;
        }
    }
    if(neon_value_isnumber(val))
    {
        return state->objvars.classprimnumber;
    }
    else if(neon_value_isarray(val))
    {
        return state->objvars.classprimarray;
    }
    else if(neon_value_isstring(val))
    {
        return state->objvars.classprimstring;
    }
    return NULL;
}

bool neon_value_getcallable(NeonState* state, NeonValue receiver, NeonObjString* name, NeonValue* dest)
{
    NeonObject* obj;
    NeonObjMap* map;
    NeonObjClass* klass;
    if(neon_value_ismap(receiver))
    {
        map = neon_value_asmap(receiver);
        if(!neon_map_get(map, name, dest))
        {
            neon_state_raiseerror(state, "cannot get field '%s' from map", name->sbuf->data);
            return false;
        }
        return true;
    }
    else
    {
        /* first, check method table specific to that type ... */
        klass = neon_value_getvalueclass(state, receiver);
        if(klass == NULL)
        {
            if(neon_value_isclass(receiver))
            {
                klass = neon_value_asclass(receiver);
            }
        }
        if(klass != NULL)
        {
            if(neon_class_getmethod(klass, name, dest))
            {
                return true;
            }
            if(neon_class_getstaticmethod(klass, name, dest))
            {
                return true;
            }
        }
        /* if there isn't one, check if the object has inited its $objmethods field ... */
        if(neon_value_isobject(receiver))
        {
            obj = neon_value_asobject(receiver);
            if(obj->objmethods != NULL)
            {
                if(neon_hashtable_get(obj->objmethods, name, dest))
                {
                    return true;
                }
            }
        }
        /* if that is also not the case, see if $mthobject has a matching method */
        if(neon_class_getmethod(state->objvars.classprimobject, name, dest))
        {
            return true;
        }            
        /* when all else is exhausted, then there is no such method. */
        return false;
    }
    return false;
}

bool neon_vmbits_invoke(NeonState* state, NeonObjString* name, int argc)
{
    NeonValue callable;
    NeonValue receiver;
    NeonObjUserdata* ud;
    NeonObjInstance* instance;
    receiver = neon_vmbits_stackpeek(state, argc);
    if(neon_value_isinstance(receiver))
    {
        instance = neon_value_asinstance(receiver);
        if(neon_hashtable_get(instance->fields, name, &callable))
        {
            state->vmstate.stackvalues[state->vmstate.stacktop + (-argc - 1)] = callable;
            return neon_vmbits_callvalue(state, receiver, callable, argc);
        }
        return neon_vmbits_invokefromclass(state, receiver, instance->klass, name, argc);
    }
    else if(neon_value_isuserdata(receiver))
    {
        ud = neon_value_asuserdata(receiver);
        if(neon_hashtable_get(ud->methods, name, &callable))
        {
            return neon_vmbits_callvalue(state, receiver, callable, argc);
        }
    }
    else
    {
        if(neon_value_getcallable(state, receiver, name, &callable))
        {
            state->vmstate.stackvalues[state->vmstate.stacktop + (-argc - 1)] = callable;
            return neon_vmbits_callvalue(state, receiver, callable, argc);
        }
        neon_state_raiseerror(state, "cannot invoke method '%s' on non-instance object type <%s> (peek at %d)", name->sbuf->data, neon_value_typename(receiver), argc);
    }
    return false;
}

bool neon_vmbits_bindmethod(NeonState* state, NeonObjClass* klass, NeonObjString* name)
{
    NeonValue method;
    NeonValue peeked;
    NeonObjBoundFunction* bound;
    if(!neon_hashtable_get(klass->methods, name, &method))
    {
        neon_state_raiseerror(state, "cannot bind undefined method '%s'", name->sbuf->data);
        return false;
    }
    peeked = neon_vmbits_stackpeek(state, 0);
    bound = neon_object_makeboundmethod(state, peeked, neon_value_asclosure(method));
    neon_vmbits_stackpop(state);
    neon_vmbits_stackpush(state, neon_value_fromobject(bound));
    return true;
}

NeonObjUpvalue* neon_object_makeupvalue(NeonState* state, NeonValue* pslot, int32_t upidx)
{
    NeonObjUpvalue* obj;
    obj = (NeonObjUpvalue*)neon_object_allocobj(state, sizeof(NeonObjUpvalue), NEON_OBJ_UPVALUE);
    obj->closed = neon_value_makenil();
    obj->location = *pslot;
    obj->next = NULL;
    obj->upindex = upidx;
    return obj;
}

NeonObjUpvalue* neon_vmbits_captureupval(NeonState* state, NeonValue* local, int32_t upidx)
{
    NeonObjUpvalue* upvalue;
    NeonObjUpvalue* prevupvalue;
    NeonObjUpvalue* createdupvalue;
    prevupvalue = NULL;
    upvalue = state->openupvalues;
    while(upvalue != NULL && &upvalue->location > local)
    {
        prevupvalue = upvalue;
        upvalue = upvalue->next;
    }
    if(upvalue != NULL && &upvalue->location == local)
    {
        return upvalue;
    }
    createdupvalue = neon_object_makeupvalue(state, local, upidx);
    createdupvalue->next = upvalue;
    if(prevupvalue == NULL)
    {
        state->openupvalues = createdupvalue;
    }
    else
    {
        prevupvalue->next = createdupvalue;
    }
    return createdupvalue;
}

void neon_vmbits_closeupvalues(NeonState* state, NeonValue* last)
{
    NeonObjUpvalue* currup;
    while(state->openupvalues != NULL && &state->openupvalues->location >= last)
    {
        currup = state->openupvalues;
        currup->closed = currup->location;
        currup->location = currup->closed;
        state->openupvalues = currup->next;
    }
}

void neon_vmbits_defmethod(NeonState* state, NeonObjString* name)
{
    NeonValue method;
    NeonObjClass* klass;
    method = neon_vmbits_stackpeek(state, 0);
    klass = neon_value_asclass(neon_vmbits_stackpeek(state, 1));
    neon_hashtable_set(klass->methods, name, method);
    neon_vmbits_stackpop(state);
}

static inline void concatputval(NeonWriter* wr, NeonValue v)
{
    NeonObjString* os;
    if(neon_value_isstring(v))
    {
        os = neon_value_asstring(v);
        neon_writer_writestringl(wr, os->sbuf->data, os->sbuf->length);
    }
    else
    {
        neon_writer_printvalue(wr, v, false);
    }
}

static inline bool neon_vmexec_doconcat(NeonState* state)
{
    NeonValue peeka;
    NeonValue peekb;
    NeonObjString* result;
    NeonWriter* wr;
    wr = neon_writer_makestring(state);
    peekb = neon_vmbits_stackpeek(state, 0);
    peeka = neon_vmbits_stackpeek(state, 1);
    concatputval(wr, peeka);
    concatputval(wr, peekb);
    result = neon_string_copy(state, wr->strbuf->data, wr->strbuf->length);
    neon_writer_destroy(wr);
    neon_vmbits_stackpop(state);
    neon_vmbits_stackpop(state);
    neon_vmbits_stackpush(state, neon_value_fromobject(result));
    return true;
}

static inline int neon_vmutil_numtoint32(NeonValue val)
{
    if(neon_value_isbool(val))
    {
        return (neon_value_asbool(val) ? 1 : 0);
    }
    return neon_util_numbertoint32(neon_value_asnumber(val));
}

static inline unsigned int neon_vmutil_numtouint32(NeonValue val)
{
    if(neon_value_isbool(val))
    {
        return (neon_value_asbool(val) ? 1 : 0);
    }
    return neon_util_numbertouint32(neon_value_asnumber(val));
}

static inline double neon_vmutil_tonum(NeonValue val)
{
    if(neon_value_isbool(val))
    {
        return (neon_value_asbool(val) ? 1 : 0);
    }
    return neon_value_asnumber(val);
}

static inline long neon_vmutil_toint(NeonValue val)
{
    if(neon_value_isbool(val))
    {
        return (neon_value_asbool(val) ? 1 : 0);
    }
    return neon_value_asnumber(val);
}

static inline bool neon_vmexec_dobinary(NeonState* state, bool asbool, NeonOpCode op, NeonVMBinaryCallbackFN fn)
{
    long leftint;
    long rightint;
    double numres;
    double leftflt;
    double rightflt;
    int leftsigned;
    unsigned int rightusigned;
    NeonValue resval;
    NeonValue leftinval;
    NeonValue rightinval;
    rightinval = neon_vm_stackpop(state);
    leftinval = neon_vm_stackpop(state);
    //fprintf(stderr, "neon_vmexec_dobinary(asbool=%d, op=%s)\n", asbool, neon_dbg_op2str(op));
    if((!neon_value_isnumber(leftinval) && !neon_value_isbool(leftinval)) || (!neon_value_isnumber(rightinval) && !neon_value_isbool(rightinval)))
    {
        neon_state_raiseerror(state, "unsupported operand %d for %s and %s", neon_dbg_op2str(op), neon_value_typename(leftinval), neon_value_typename(rightinval));
    }
    if(fn != NULL)
    {
        leftflt = neon_vmutil_tonum(leftinval);
        rightflt = neon_vmutil_tonum(rightinval);
        numres = fn(leftflt, rightflt);
    }
    else
    {
        switch(op)
        {
            case NEON_OP_PRIMADD:
                {
                    leftflt = neon_vmutil_tonum(leftinval);
                    rightflt = neon_vmutil_tonum(rightinval);
                    numres = (leftflt + rightflt);
                }
                break;
            case NEON_OP_PRIMSUBTRACT:
                {
                    leftflt = neon_vmutil_tonum(leftinval);
                    rightflt = neon_vmutil_tonum(rightinval);
                    numres = (leftflt - rightflt);
                }
                break;
            case NEON_OP_PRIMMULTIPLY:
                {
                    leftflt = neon_vmutil_tonum(leftinval);
                    rightflt = neon_vmutil_tonum(rightinval);
                    numres = (leftflt * rightflt);
                }
                break;
            case NEON_OP_PRIMDIVIDE:
                {
                    leftflt = neon_vmutil_tonum(leftinval);
                    rightflt = neon_vmutil_tonum(rightinval);
                    numres = (leftflt / rightflt);
                }
                break;
            case NEON_OP_PRIMSHIFTRIGHT:
                {
                    leftsigned = neon_vmutil_numtoint32(leftinval);
                    rightusigned = neon_vmutil_numtouint32(rightinval);
                    numres = leftsigned >> (rightusigned & 0x1F);
                }
                break;
            case NEON_OP_PRIMSHIFTLEFT:
                {
                    leftsigned = neon_vmutil_numtoint32(leftinval);
                    rightusigned = neon_vmutil_numtouint32(rightinval);
                    numres = leftsigned << (rightusigned & 0x1F);
                }
                break;
            case NEON_OP_PRIMBINXOR:
                {
                    leftint = neon_vmutil_toint(leftinval);
                    rightint = neon_vmutil_toint(rightinval);
                    numres = (leftint ^ rightint);
                }
                break;
            case NEON_OP_PRIMBINOR:
                {
                    leftint = neon_vmutil_toint(leftinval);
                    rightint = neon_vmutil_toint(rightinval);
                    numres = (leftint | rightint);
                }
                break;
            case NEON_OP_PRIMBINAND:
                {
                    leftint = neon_vmutil_toint(leftinval);
                    rightint = neon_vmutil_toint(rightinval);
                    numres = (leftint & rightint);
                }
                break;
            case NEON_OP_PRIMGREATER:
                {
                    leftflt = neon_vmutil_tonum(leftinval);
                    rightflt = neon_vmutil_tonum(rightinval);
                    numres = (leftflt > rightflt);
                }
                break;
            case NEON_OP_PRIMLESS:
                {
                    leftflt = neon_vmutil_tonum(leftinval);
                    rightflt = neon_vmutil_tonum(rightinval);
                    numres = (leftflt < rightflt);
                }
                break;
            default:
                {
                    fprintf(stderr, "missed an opcode here?\n");
                    assert(false);
                }
                break;
        }
    }
    if(asbool)
    {
        resval = neon_value_makebool(numres);
    }
    else
    {
        resval = neon_value_makenumber(numres);
    }
    neon_vm_stackpush(state, resval);
    return true;
}

static inline bool neon_vmexec_doindexgetstring(NeonState* state, NeonObjString* os, NeonValue vidx, NeonValue* destval)
{
    char ch;
    long nidx;
    NeonObjString* nos;
    if(!neon_value_isnumber(vidx))
    {
        neon_state_raiseerror(state, "cannot get string index with non-number type <%s>", neon_value_typename(vidx));
        return false;
    }
    nidx = neon_value_asnumber(vidx);
    if((nidx >= 0) && (nidx < (long)os->sbuf->length))
    {
        ch = os->sbuf->data[nidx];
        nos = neon_string_copy(state, &ch, 1);
        nos->sbuf->length = 1;
        nos->sbuf->data[1] = '\0';
        *destval = neon_value_fromobject(nos);
        return true;
    }
    else
    {
        *destval = neon_value_makenil();
    }
    return false;
}

static inline bool neon_vmexec_doindexgetarray(NeonState* state, NeonObjArray* oa, NeonValue vidx, NeonValue* destval)
{
    long nidx;
    NeonValue val;
    if(!neon_value_isnumber(vidx))
    {
        neon_state_raiseerror(state, "cannot get array index with non-number type <%s>", neon_value_typename(vidx));
        return false;
    }
    nidx = neon_value_asnumber(vidx);
    //fprintf(stderr, "nidx=%d\n", nidx);
    if((nidx >= 0) && (nidx < (long)oa->vala->size))
    {
        val = oa->vala->values[nidx];
        *destval = val;
        return true;
    }
    else
    {
        *destval = neon_value_makenil();
    }
    return false;
}

static inline bool neon_vmexec_doindexgetmap(NeonState* state, NeonObjMap* om, NeonValue vidx, NeonValue* destval)
{
    NeonValue val;
    NeonObjString* key;
    if(!neon_value_isstring(vidx))
    {
        neon_state_raiseerror(state, "cannot get map index with non-string type <%s>", neon_value_typename(vidx));
        return false;
    }
    key = neon_value_asstring(vidx);
    if(neon_map_get(om, key, &val))
    {
        *destval = val;
    }
    else
    {
        *destval = neon_value_makenil();
    }
    return true;
}

static inline bool neon_vmexec_doindexget(NeonState* state)
{
    bool ok;
    bool willassign;
    int32_t waint;
    NeonValue destval;
    NeonValue vidx;
    NeonValue targetobj;
    (void)waint;
    waint = 0;
    ok = false;
    vidx = neon_vmbits_stackpeek(state, 0);
    targetobj = neon_vmbits_stackpeek(state, 1);
    waint = neon_vmbits_readbyte(state);
    willassign = (waint == 1);
    if(!willassign)
    {
        neon_vmbits_stackpop(state);
        neon_vmbits_stackpop(state);
    }
    //fprintf(stderr, "indexget: waint=%d vidx=<%s> targetobj=<%s>\n", waint, neon_value_typename(vidx), neon_value_typename(targetobj));
    if(neon_value_isstring(targetobj))
    {
        if(neon_vmexec_doindexgetstring(state, neon_value_asstring(targetobj), vidx, &destval))
        {
            ok = true;
        }
    }
    else if(neon_value_isarray(targetobj))
    {
        if(neon_vmexec_doindexgetarray(state, neon_value_asarray(targetobj), vidx, &destval))
        {
            ok = true;
        }
    }
    else if(neon_value_ismap(targetobj))
    {
        if(neon_vmexec_doindexgetmap(state, neon_value_asmap(targetobj), vidx, &destval))
        {
            ok = true;
        }
    }
    else
    {
        neon_state_raiseerror(state, "cannot get index object type <%s>", neon_value_typename(targetobj));
    }
    if(!ok)
    {
        destval = neon_value_makenil();
    }
    neon_vmbits_stackpush(state, destval);
    return ok;
}

static inline bool neon_vmexec_doindexsetarray(NeonState* state, NeonObjArray* oa, NeonValue vidx, NeonValue setval)
{
    long nidx;
    if(!neon_value_isnumber(vidx))
    {
        neon_state_raiseerror(state, "cannot set array index with non-number type <%s>", neon_value_typename(vidx));
        return false;
    }
    nidx = neon_value_asnumber(vidx);
    /*
    neon_writer_writefmt(state->stderrwriter, "indexsetarray: nidx=%d, setval=<%s> ", nidx, neon_value_typename(setval));
    neon_writer_printvalue(state->stderrwriter, setval, true);
    neon_writer_writefmt(state->stderrwriter, "\n");
    */
    neon_valarray_insert(oa->vala, nidx, setval);
    return true;
}

static inline bool neon_vmexec_doindexsetmap(NeonState* state, NeonObjMap* om, NeonValue vidx, NeonValue setval)
{
    NeonObjString* key;
    if(!neon_value_isstring(vidx))
    {
        neon_state_raiseerror(state, "cannot set map index with non-string type <%s>", neon_value_typename(vidx));
        return false;
    }
    key = neon_value_asstring(vidx);
    neon_map_set(om, key, setval);
    return true;
}

static inline bool neon_vmexec_doindexset(NeonState* state)
{
    bool ok;
    bool willassign;
    int waint;
    NeonValue vidx;
    NeonValue targetobj;
    NeonValue setval;
    ok = false;
    setval = neon_vmbits_stackpeek(state, 0);
    vidx = neon_vmbits_stackpeek(state, 1);
    targetobj = neon_vmbits_stackpeek(state, 2);
    waint = neon_vmbits_readbyte(state);
    willassign = (waint == 1);
    if(!willassign)
    {
        neon_vmbits_stackpop(state);
        neon_vmbits_stackpop(state);
        neon_vmbits_stackpop(state);
    }
    if(neon_value_isarray(targetobj))
    {
        if(neon_vmexec_doindexsetarray(state, neon_value_asarray(targetobj), vidx, setval))
        {
            ok = true;
        }
    }
    else if(neon_value_ismap(targetobj))
    {
        if(neon_vmexec_doindexsetmap(state, neon_value_asmap(targetobj), vidx, setval))
        {
            ok = true;
        }
    }
    else
    {
        neon_state_raiseerror(state, "cannot set index object type <%s>", neon_value_typename(targetobj));
    }
    neon_vmbits_stackpush(state, setval);
    return ok;
}


static inline bool neon_vmutil_hasproperties(NeonState* state, NeonValue val)
{
    (void)state;
    return (
        neon_value_ismap(val) ||
        neon_value_isinstance(val)
    );
}

static inline bool neon_vmexec_dopropertyget(NeonState* state)
{
    NeonValue value;
    NeonValue peeked;
    NeonValue vidx;
    NeonObjString* name;
    NeonObjMap* map;
    NeonObjInstance* instance;
    peeked = neon_vmbits_stackpeek(state, 0);
    if(!neon_vmutil_hasproperties(state, peeked))
    {
        neon_state_raiseerror(state, "cannot get property for object type <%s>", neon_value_typename(peeked));
        return false;
    }
    vidx = neon_vmbits_readconst(state);
    neon_vmbits_stackpop(state);// Instance.
    instance = neon_value_asinstance(peeked);
    name = neon_value_asstring(vidx);
    if(neon_value_isinstance(peeked))
    {
        if(neon_hashtable_get(instance->fields, name, &value))
        {
            neon_vmbits_stackpush(state, value);
            return true;
        }
        if(!neon_vmbits_bindmethod(state, instance->klass, name))
        {
            return false;
        }
    }
    else if(neon_value_ismap(peeked))
    {
        map = neon_value_asmap(peeked);
        if(!neon_map_get(map, name, &value))
        {
            return false;
        }
        neon_vmbits_stackpush(state, value);
        return true;
    }
    else
    {
        neon_state_raiseerror(state, "missing getproperty clause for object type <%s>", neon_value_typename(peeked));
        return false;
    }
    return true;
}

static inline bool neon_vmexec_dopropertyset(NeonState* state)
{
    NeonValue value;
    NeonValue peeked;
    NeonValue propval;
    NeonObjString* propname;
    NeonObjMap* map;
    NeonObjInstance* instance;
    peeked = neon_vmbits_stackpeek(state, 1);
    if(!neon_vmutil_hasproperties(state, peeked))
    {
        neon_state_raiseerror(state, "cannot set property for object type <%s>", neon_value_typename(peeked));
        return false;
    }
    propname = neon_vmbits_readstring(state);
    propval = neon_vmbits_stackpeek(state, 0);
    if(neon_value_isinstance(peeked))
    {
        instance = neon_value_asinstance(peeked);
        neon_hashtable_set(instance->fields, propname, propval);
    }
    else if(neon_value_ismap(peeked))
    {
        map = neon_value_asmap(peeked);
        if(!neon_map_set(map, propname, propval))
        {
            neon_state_raiseerror(state, "cannot set map field '%s'", propname->sbuf->data);
            return false;
        }
    }
    else
    {
        neon_state_raiseerror(state, "missing setproperty clause for object type <%s>", neon_value_typename(peeked));
        return false;
    }
    value = neon_vmbits_stackpop(state);
    neon_vmbits_stackpop(state);
    neon_vmbits_stackpush(state, value);
    return true;
}

static inline bool neon_vmexec_domakearray(NeonState* state)
{
    int i;
    int count;
    NeonValue val;
    NeonObjArray* array;
    count = neon_vmbits_readbyte(state);
    array = neon_array_make(state);
    //state->vmstate.stackvalues[-count - 1] = neon_value_fromobject(array);
    //fprintf(stderr, "makearray: count=%d\n", count);
    for(i = count - 1; i >= 0; i--)
    {
        val = neon_vmbits_stackpeek(state, i);
        neon_array_push(array, val);
    }
    //fprintf(stderr, "makearray: count=%d array->size=%d\n", (int)count, (int)neon_array_count(array));
    if(count > 0)
    {
        neon_vmbits_stackpopn(state, count+0);
    }
    else
    {
        
    }
    neon_vmbits_stackpush(state, neon_value_fromobject(array));
    return true;
}

static inline bool neon_vmexec_domakemap(NeonState* state)
{
    int count;
    NeonObjMap* map;
    (void)count;
    count = neon_vmbits_readbyte(state);
    map = neon_object_makemap(state);
    neon_vmbits_stackpush(state, neon_value_fromobject(map));
    return true;
}


static inline bool neon_vmexec_doglobalstmt(NeonState* state)
{
    int32_t cnidx;
    NeonValue val;
    NeonValue cval;
    NeonObjString* cname;
    NeonObjMap* map;
    cnidx = neon_vmbits_readbyte(state);
    if(cnidx == -1)
    {
        map = neon_object_makemapfromtable(state, state->globals, false);
        neon_vmbits_stackpush(state, neon_value_fromobject(map));
    }
    else
    {
        cval = neon_vmbits_readconstbyindex(state, cnidx);
        cname = neon_value_asstring(cval);
        if(!neon_hashtable_get(state->globals, cname, &val))
        {
            val = neon_value_makenil();
        }
        neon_vmbits_stackpush(state, val);
    }
    return true;
}

bool neon_vmdo_classbindmethod(NeonState* state, NeonObjClass* klass, NeonObjString* name)
{
    NeonValue method;
    NeonObjBoundFunction* bound;
    if(neon_hashtable_get(klass->methods, name, &method))
    {
        bound = neon_object_makeboundmethod(state, neon_vm_stackpeek(state, 0), neon_value_asclosure(method));
        neon_vm_stackpop(state);
        neon_vm_stackpush(state, neon_value_fromobject(bound));
        return true;
    }
    neon_state_raiseerror(state, "undefined property '%s'", name->sbuf->data);
    return false;
}

bool neon_vmexec_doinstthispropertyget(NeonState* state)
{
    NeonValue value;
    NeonValue peeked;
    NeonObjString* name;
    NeonObjClass* klass;
    NeonObjInstance* instance;
    name = neon_vmbits_readstring(state);
    peeked = neon_vm_stackpeek(state, 0);
    if(neon_value_isinstance(peeked))
    {
        instance = neon_value_asinstance(peeked);
        klass = instance->klass;
        if(neon_hashtable_get(instance->fields, name, &value))
        {
            neon_vm_stackpop(state);
            neon_vm_stackpush(state, value);
            return true;
        }
        if(neon_vmdo_classbindmethod(state, klass, name))
        {
            return true;
        }
        neon_state_raiseerror(state, "instance of class %s does not have a property or method named '%s'", klass->name->sbuf->data,
                      name->sbuf->data);
    }
    neon_state_raiseerror(state, "object of type '%s' does not have properties", neon_value_typename(peeked));
    return false;
}

#define vmmac_opinstname(n) n
#define vm_default() default:
#define op_case(name) case vmmac_opinstname(name):
#define vm_breakouter() break

NeonStatusCode neon_vm_runvm(NeonState* state, NeonValue* evdest, bool fromnested)
{
    size_t icnt;
    int32_t instruc;
    NeonStatusCode sc;
    NeonWriter* owr;
    (void)icnt;
    //fprintf(stderr, "in neon_vm_runvm\n");
    owr = state->stderrwriter;
    state->vmstate.activeframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
    for(;;)
    {
        instruc = neon_vmbits_readinstruction(state);
        if(state->conf.shouldprintruntime)
        {
            neon_dbg_debugprintatinstruction(state, owr, state->vmstate.activeframe);
        }
        sc = neon_vm_runexecinstruc(state, instruc, evdest, fromnested);
        if(sc != NEON_STATUS_OK)
        {
            return sc;
        }
    }
    return NEON_STATUS_OK;
}

NeonStatusCode neon_vm_runexecinstruc(NeonState* state, int32_t instruc, NeonValue* evdest, bool fromnested)
{
    switch(instruc)
    {
        op_case(NEON_OP_PUSHCONST)
        {
            NeonValue cval;
            cval = neon_vmbits_readconst(state);
            /* A Virtual Machine op-constant < A Virtual Machine push-constant */
            /*
            neon_writer_writefmt(state->stderrwriter, "pushconst: ");
            neon_writer_printvalue(state->stderrwriter, cval, true);
            neon_writer_writefmt(state->stderrwriter, "\n");
            */
            neon_vmbits_stackpush(state, cval);
        }
        vm_breakouter();
    op_case(NEON_OP_PUSHONE)
        {
            neon_vmbits_stackpush(state, neon_value_makenumber((double)1));
        }
        vm_breakouter();
    op_case(NEON_OP_PUSHNIL)
        {
            neon_vmbits_stackpush(state, neon_value_makenil());
        }
        vm_breakouter();
    op_case(NEON_OP_PUSHTRUE)
        {
            neon_vmbits_stackpush(state, neon_value_makebool(true));
        }
        vm_breakouter();
    op_case(NEON_OP_PUSHFALSE)
        {
            neon_vmbits_stackpush(state, neon_value_makebool(false));
        }
        vm_breakouter();
    op_case(NEON_OP_MAKEARRAY)
        {
            if(!neon_vmexec_domakearray(state))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_MAKEMAP)
        {
            if(!neon_vmexec_domakemap(state))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_POPONE)
        {
            neon_vmbits_stackpop(state);
        }
        vm_breakouter();
    op_case(NEON_OP_POPN)
        {
            int32_t n;
            n = neon_vmbits_readbyte(state);
            neon_vmbits_stackpopn(state, n);
        }
        vm_breakouter();
    op_case(NEON_OP_DUP)
        {
            neon_vmbits_stackpush(state, neon_vmbits_stackpeek(state, 0));
        }
        vm_breakouter();
    op_case(NEON_OP_LOCALGET)
        {
            int32_t islot;
            int32_t actualpos;
            NeonValue val;
            islot = neon_vmbits_readbyte(state);
            actualpos = state->vmstate.activeframe->frstackindex + (islot + 0);
            neon_vm_stackmaybegrow(state, actualpos);
            val = state->vmstate.stackvalues[actualpos];
            //neon_writer_valfmt(state->stderrwriter, "LOCALGET: val=@\n", val);
            neon_vmbits_stackpush(state, val);
        }
        vm_breakouter();
    op_case(NEON_OP_LOCALSET)
        {
            int32_t islot;
            int32_t actualpos;
            NeonValue val;
            islot = neon_vmbits_readbyte(state);
            val = neon_vmbits_stackpeek(state, 0);
            actualpos = state->vmstate.activeframe->frstackindex + (islot + 0);
            neon_vm_stackmaybegrow(state, actualpos);
            state->vmstate.stackvalues[actualpos] = val;
            
        }
        vm_breakouter();
    op_case(NEON_OP_GLOBALGET)
        {
            NeonValue value;
            NeonObjString* name;
            name = neon_vmbits_readstring(state);
            if(!neon_hashtable_get(state->globals, name, &value))
            {
                neon_state_raiseerror(state, "undefined variable '%s'.", name->sbuf->data);
                return NEON_STATUS_RUNTIMEERROR;
            }
            neon_vmbits_stackpush(state, value);
        }
        vm_breakouter();
    op_case(NEON_OP_GLOBALDEFINE)
        {
            NeonValue peeked;
            NeonObjString* name;
            name = neon_vmbits_readstring(state);
            peeked = neon_vmbits_stackpeek(state, 0);
            neon_hashtable_set(state->globals, name, peeked);
            neon_vmbits_stackpop(state);
        }
        vm_breakouter();            
    op_case(NEON_OP_GLOBALSET)
        {
            NeonValue peeked;
            NeonObjString* name;
            name = neon_vmbits_readstring(state);
            peeked = neon_vmbits_stackpeek(state, 0);
            if(neon_hashtable_set(state->globals, name, peeked))
            {
                if(state->conf.strictmode)
                {
                    neon_hashtable_delete(state, state->globals, name);// [delete]
                    neon_state_raiseerror(state, "strict mode: undefined variable '%s'.", name->sbuf->data);
                    return NEON_STATUS_RUNTIMEERROR;
                }
            }
        }
        vm_breakouter();
    op_case(NEON_OP_CLOSURE)
        {
            int i;
            int32_t index;
            int32_t islocal;
            int32_t upidx;
            NeonValue vcval;
            NeonObjScriptFunction* fn;
            NeonObjClosure* closure;
            vcval = neon_vmbits_readconst(state);
            fn = neon_value_asscriptfunction(vcval);
            closure = neon_object_makeclosure(state, fn);
            neon_vmbits_stackpush(state, neon_value_fromobject(closure));
            for(i = 0; i < closure->upvaluecount; i++)
            {
                islocal = neon_vmbits_readbyte(state);
                index = neon_vmbits_readbyte(state);
                if(islocal)
                {
                    upidx = state->vmstate.activeframe->frstackindex + index;
                    closure->upvalues[i] = neon_vmbits_captureupval(state, &state->vmstate.stackvalues[upidx], upidx);
                }
                else
                {
                    closure->upvalues[i] = state->vmstate.activeframe->closure->upvalues[index];
                }
            }
        }
        vm_breakouter();
    op_case(NEON_OP_UPVALCLOSE)
        {
            NeonValue* vargs;
            vargs = (&state->vmstate.stackvalues[0]) + (state->vmstate.stacktop - 1);
            neon_vmbits_closeupvalues(state, vargs);
            neon_vmbits_stackpop(state);
        }
        vm_breakouter();
    op_case(NEON_OP_UPVALGET)
        {
            int32_t islot;
            NeonValue val;
            NeonObjClosure* cls;
            islot = neon_vmbits_readbyte(state);
            cls = state->vmstate.activeframe->closure;
            val = cls->upvalues[islot]->location;
            neon_vmbits_stackpush(state, val);
        }
        vm_breakouter();
    op_case(NEON_OP_UPVALSET)
        {
            int32_t idx;
            int32_t islot;
            NeonValue peeked;
            islot = neon_vmbits_readbyte(state);
            peeked = neon_vmbits_stackpeek(state, 0);
            idx = 0;
            state->vmstate.activeframe->closure->upvalues[islot]->upindex = (idx);
            state->vmstate.activeframe->closure->upvalues[islot]->location = peeked;
        }
        vm_breakouter();

    op_case(NEON_OP_INDEXGET)
        {
            if(!neon_vmexec_doindexget(state))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_INDEXSET)
        {
            if(!neon_vmexec_doindexset(state))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_PROPERTYGET)
        {
            if(!neon_vmexec_dopropertyget(state))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_PROPERTYSET)
        {
            if(!neon_vmexec_dopropertyset(state))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_INSTTHISPROPERTYGET)
        {
            if(!neon_vmexec_doinstthispropertyget(state))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_INSTGETSUPER)
        {
            NeonObjString* name;
            NeonObjClass* superclass;
            name = neon_vmbits_readstring(state);
            superclass = neon_value_asclass(neon_vmbits_stackpop(state));
            if(!neon_vmbits_bindmethod(state, superclass, name))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_EQUAL)
        {
            NeonValue a;
            NeonValue b;
            b = neon_vmbits_stackpop(state);
            a = neon_vmbits_stackpop(state);
            neon_vmbits_stackpush(state, neon_value_makebool(neon_value_equal(state, a, b)));
        }
        vm_breakouter();
    op_case(NEON_OP_PRIMGREATER)
    op_case(NEON_OP_PRIMLESS)
        {
            if(!neon_vmexec_dobinary(state, true, (NeonOpCode)instruc, NULL))
            {
                abort();
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_PRIMADD)
        {
            NeonValue peek1;
            NeonValue peek2;
            peek1 = neon_vmbits_stackpeek(state, 0);
            peek2 = neon_vmbits_stackpeek(state, 1);
            if(neon_value_isnumber(peek1) && neon_value_isnumber(peek2))
            {
                if(!neon_vmexec_dobinary(state, false, (NeonOpCode)instruc, NULL))
                {
                    return NEON_STATUS_RUNTIMEERROR;
                }
            }
            else
            {
                neon_vmexec_doconcat(state);
            }
        }
        vm_breakouter();
    op_case(NEON_OP_PRIMSUBTRACT)
    op_case(NEON_OP_PRIMMULTIPLY)
    op_case(NEON_OP_PRIMDIVIDE)
    op_case(NEON_OP_PRIMSHIFTLEFT)
    op_case(NEON_OP_PRIMSHIFTRIGHT)
    op_case(NEON_OP_PRIMBINAND)
    op_case(NEON_OP_PRIMBINOR)
    op_case(NEON_OP_PRIMBINXOR)
        {
            if(!neon_vmexec_dobinary(state, false, (NeonOpCode)instruc, NULL))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_PRIMMODULO)
        {
            if(!neon_vmexec_dobinary(state, false, NEON_OP_PRIMMODULO, (NeonVMBinaryCallbackFN)neon_util_modulo))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_PRIMBINNOT)
        {
            NeonValue peeked;
            NeonValue popped;
            peeked = neon_vm_stackpeek(state, 0);
            if(!neon_value_isnumber(peeked))
            {
                neon_state_raiseerror(state, "operator ~ not defined for object of type %s", neon_value_typename(peeked));
                break;
            }
            popped = neon_vm_stackpop(state);
            neon_vm_stackpush(state, neon_value_makenumber(~((int)neon_value_asnumber(popped))));
        }
        vm_breakouter();
    op_case(NEON_OP_PRIMNOT)
        {
            NeonValue popped;
            popped = neon_vmbits_stackpop(state);
            neon_vmbits_stackpush(state, neon_value_makebool(neon_value_isfalsey(popped)));
        }
        vm_breakouter();
    op_case(NEON_OP_PRIMNEGATE)
        {
            NeonValue peeked;
            NeonValue popped;
            peeked = neon_vmbits_stackpeek(state, 0);
            if(!neon_value_isnumber(peeked))
            {
                neon_state_raiseerror(state, "operand must be a number.");
                return NEON_STATUS_RUNTIMEERROR;
            }
            popped = neon_vmbits_stackpop(state);
            neon_vmbits_stackpush(state, neon_value_makenumber(-neon_value_asnumber(popped)));
        }
        vm_breakouter();
    op_case(NEON_OP_DEBUGPRINT)
        {
            NeonValue val;
            val = neon_vmbits_stackpop(state);
            neon_writer_writefmt(state->stderrwriter, "debug: ");
            neon_writer_printvalue(state->stderrwriter, val, true);
            neon_writer_writefmt(state->stderrwriter, "\n");
        }
        vm_breakouter();
    op_case(NEON_OP_GLOBALSTMT)
        {
            if(!neon_vmexec_doglobalstmt(state))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_TYPEOF)
        {
            NeonValue val;
            NeonObjString* os;
            const char* tname;
            val = neon_vmbits_stackpop(state);
            tname = neon_value_basicname(val);
            os = neon_string_copy(state, tname, strlen(tname));
            neon_vmbits_stackpush(state, neon_value_fromobject(os));
        }
        vm_breakouter();
    op_case(NEON_OP_JUMPNOW)
        {
            uint16_t offset;
            offset = neon_vmbits_readshort(state);
            state->vmstate.activeframe->instrucidx += offset;
        }
        vm_breakouter();
    op_case(NEON_OP_JUMPIFFALSE)
        {
            uint16_t offset;
            NeonValue peeked;
            offset = neon_vmbits_readshort(state);
            peeked = neon_vmbits_stackpeek(state, 0);
            if(neon_value_isfalsey(peeked))
            {
                state->vmstate.activeframe->instrucidx += offset;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_LOOP)
        {
            uint16_t offset;
            offset = neon_vmbits_readshort(state);
            state->vmstate.activeframe->instrucidx -= offset;
        }
        vm_breakouter();
    op_case(NEON_OP_CALL)
        {
            int argc;
            NeonValue peeked;
            argc = neon_vmbits_readbyte(state);
            peeked = neon_vmbits_stackpeek(state, argc);
            if(!neon_vmbits_callvalue(state, neon_value_makenil(), peeked, argc))
            {
                fprintf(stderr, "returning error\n");
                return NEON_STATUS_RUNTIMEERROR;
            }
            state->vmstate.activeframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
        }
        vm_breakouter();
    op_case(NEON_OP_INSTTHISINVOKE)
        {
            int argc;
            NeonObjString* method;
            method = neon_vmbits_readstring(state);
            argc = neon_vmbits_readbyte(state);
            if(!neon_vmbits_invoke(state, method, argc))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
            state->vmstate.activeframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
        }
        vm_breakouter();
    op_case(NEON_OP_INSTSUPERINVOKE)
        {
            int argc;
            NeonValue popped;
            NeonObjString* method;
            NeonObjClass* superclass;
            method = neon_vmbits_readstring(state);
            argc = neon_vmbits_readbyte(state);
            popped = neon_vmbits_stackpop(state);
            superclass = neon_value_asclass(popped);
            if(!neon_vmbits_invokefromclass(state, popped, superclass, method, argc))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
            state->vmstate.activeframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
        }
        vm_breakouter();
    op_case(NEON_OP_RETURN)
        {
            int64_t usable;
            NeonValue result;
            result = neon_vmbits_stackpop(state);
            if(state->vmstate.activeframe->frstackindex >= 0)
            {
                neon_vmbits_closeupvalues(state, &state->vmstate.stackvalues[state->vmstate.activeframe->frstackindex]);
            }
            state->vmstate.framecount--;
            if(state->vmstate.framecount == 0)
            {
                //neon_vmbits_stackpop(state);
                //fprintf(stderr, "returning due to NEON_OP_RETURN\n");
                return NEON_STATUS_OK;
            }
            usable = (state->vmstate.activeframe->frstackindex - 0);
            state->vmstate.stacktop = usable;
            neon_vmbits_stackpush(state, result);
            if(evdest != NULL)
            {
                *evdest = result;
            }
            if(fromnested)
            {
            }
            else
            {
                state->vmstate.activeframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
            }
        }
        vm_breakouter();
    op_case(NEON_OP_CLASS)
        {
            NeonObjClass* klass;
            NeonObjString* clname;
            clname = neon_vmbits_readstring(state);
            klass = neon_object_makeclass(state, clname, state->objvars.classprimobject);
            neon_vmbits_stackpush(state, neon_value_fromobject(klass));
        }
        vm_breakouter();
    op_case(NEON_OP_INHERIT)
        {
            NeonValue vklass;
            NeonValue superclass;
            NeonObjClass* subclass;
            superclass = neon_vmbits_stackpeek(state, 1);
            if(!neon_value_isclass(superclass))
            {
                neon_state_raiseerror(state, "superclass must be a class.");
                return NEON_STATUS_RUNTIMEERROR;
            }
            vklass = neon_vmbits_stackpeek(state, 0);
            subclass = neon_value_asclass(vklass);
            neon_hashtable_addall(neon_value_asclass(superclass)->methods, subclass->methods);
            neon_vmbits_stackpop(state);// Subclass.
        }
        vm_breakouter();

    op_case(NEON_OP_METHOD)
        {
            NeonObjString* name;
            name = neon_vmbits_readstring(state);
            neon_vmbits_defmethod(state, name);
        }
        vm_breakouter();
    op_case(NEON_OP_RESTOREFRAME)
        {
            if(state->vmstate.havekeeper)
            {
                fprintf(stderr, "**restoring frame**\n");
                state->vmstate.activeframe->frstackindex = state->vmstate.keepframe.frstackindex;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_HALTVM)
        {
            return NEON_STATUS_HALT;
        }
        vm_breakouter();
    vm_default()
        {
            if(instruc != -1)
            {
                neon_state_raiseerror(state, "internal error: invalid opcode %d!", instruc);
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    }
    return NEON_STATUS_OK;
}




