


static inline int32_t neon_vmbits_readinstruction(NeonState* state)
{
    int idx;
    int32_t r;
    int32_t* vp;
    NeonObjClosure* cls;
    (void)vp;
    cls = state->vmvars.currframe->closure;
    idx = state->vmvars.currframe->instrucidx;
    if(idx >= cls->innerfn->chunk->count)
    {
        r = NEON_OP_HALTVM;
    }
    else
    {
        r = cls->innerfn->chunk->bincode[idx];
    }
    state->vmvars.currframe->instrucidx++;
    return r;
}
static inline int32_t neon_vmbits_readbyte(NeonState* state)
{
    int idx;
    int32_t r;
    int32_t* vp;
    NeonObjClosure* cls;
    (void)vp;
    cls = state->vmvars.currframe->closure;
    idx = state->vmvars.currframe->instrucidx;
    r = cls->innerfn->chunk->bincode[idx];
    state->vmvars.currframe->instrucidx++;
    return r;
}

/* Jumping Back and Forth read-short < Calls and Functions run */
static inline uint16_t neon_vmbits_readshort(NeonState* state)
{
    int idx;
    int32_t a;
    int32_t b;
    idx = state->vmvars.currframe->instrucidx;
    a = state->vmvars.currframe->closure->innerfn->chunk->bincode[idx + 0];
    b = state->vmvars.currframe->closure->innerfn->chunk->bincode[idx + 1];
    state->vmvars.currframe->instrucidx += 2;
    return (uint16_t)((a << 8) | b);
}

static inline NeonValue neon_vmbits_readconstbyindex(NeonState* state, int32_t idx)
{
    size_t vsz;
    NeonValArray* vaconst;
    (void)vsz;
    if(state->vmvars.currframe->closure == NULL)
    {
        return neon_value_makenil();
    }
    vaconst = state->vmvars.currframe->closure->innerfn->chunk->constants;
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

bool neon_vmbits_callclosurewrap(NeonState* state, NeonValue receiver, NeonObjClosure* closure, int argc, bool iseval)
{
    NeonCallFrame* frame;
    (void)receiver;
    #if 0
    if(argc != closure->innerfn->arity)
    {
        neon_state_raiseerror(state, "expected %d arguments, but got %d", closure->innerfn->arity, argc);
        return false;
    }
    #endif
    state->vmvars.iseval = iseval;
    state->vmvars.havekeeper = iseval;
    if(iseval)
    {
        state->vmvars.keepframe = *state->vmvars.currframe;
    }
    neon_vm_framesmaybegrow(state, state->vmvars.framecount + 0);
    frame = &state->vmvars.framevalues[state->vmvars.framecount];
    state->vmvars.framecount++;
    frame->closure = closure;
    frame->instrucidx = 0;
    frame->frstackindex = (state->vmvars.stacktop - argc) - 1;
    return true;
}

bool neon_vmbits_callclosure(NeonState* state, NeonValue receiver, NeonObjClosure* closure, int argc)
{
    return neon_vmbits_callclosurewrap(state, receiver, closure, argc, false);
}

bool neon_vmbits_callprogramclosure(NeonState* state, NeonObjClosure* closure, bool iseval)
{
    bool rt;
    rt = neon_vmbits_callclosurewrap(state, neon_value_makenil(), closure, 0, iseval);
    return rt;
}

bool neon_vmbits_callboundmethod(NeonState* state, NeonValue receiver, NeonValue callee, int argc)
{
    NeonObjBoundFunction* bound;
    bound = neon_value_asboundfunction(callee);
    state->vmvars.stackvalues[state->vmvars.stacktop + (-argc - 1)] = bound->receiver;    
    return neon_vmbits_callclosure(state, receiver, bound->method, argc);
}

bool neon_vmbits_callclassconstructor(NeonState* state, NeonValue receiver, NeonValue callee, int argc)
{
    NeonValue initializer;
    NeonObjClass* klass;
    NeonObjInstance* instance;
    klass = neon_value_asclass(callee);
    instance = neon_object_makeinstance(state, klass);
    state->vmvars.stackvalues[state->vmvars.stacktop + (-argc - 1)] = neon_value_fromobject(instance);
    if(neon_hashtable_get(klass->methods, state->initstring, &initializer))
    {
        return neon_vmbits_callclosure(state, receiver, neon_value_asclosure(initializer), argc);
    }
    else if(argc != 0)
    {
        neon_state_raiseerror(state, "expected 0 arguments but got %d.", argc);
        return false;
    }
    return true;
}

void neon_vmbits_debugcall(NeonState* state, NeonValue receiver, NeonValue callee, int argc)
{
    size_t i;
    NeonValue* vargs;
    vargs = (&state->vmvars.stackvalues[0] + state->vmvars.stacktop) - argc;
    if(state->conf.shouldprintruntime)
    {
        neon_writer_writeformat(state->stderrwriter, "*debug* neon_vmbits_callvalue [\n");
        neon_writer_writeformat(state->stderrwriter, "    callee: [<%s>]", neon_value_typename(callee));
        neon_writer_printvalue(state->stderrwriter, callee, true);
        neon_writer_writeformat(state->stderrwriter, "\n");
        neon_writer_writeformat(state->stderrwriter, "    argc: %d\n", argc);
        neon_writer_writeformat(state->stderrwriter, "    receiver: [<%s>]", neon_value_typename(receiver));
        neon_writer_printvalue(state->stderrwriter, receiver, true);
        neon_writer_writeformat(state->stderrwriter, "\n");
        neon_writer_writeformat(state->stderrwriter, "    argv: [\n");
        for(i=0; i<(size_t)argc; i++)
        {
            neon_writer_writeformat(state->stderrwriter, "        [%d] [<%s>]", (int)i, neon_value_typename(vargs[i]));
            neon_writer_printvalue(state->stderrwriter, vargs[i], true);
            neon_writer_writeformat(state->stderrwriter, "\n");
        }
        neon_writer_writeformat(state->stderrwriter, "    ]\n");
    }
}

bool neon_vmbits_callnativefunction(NeonState* state, NeonValue receiver, NeonValue callee, int argc)
{
    NeonValue result;
    NeonValue* vargs;
    NeonNativeFN cfunc;
    NeonObjNativeFunction* nfn;
    nfn = (NeonObjNativeFunction*)neon_value_asobject(callee);
    cfunc = nfn->natfunc;
    vargs = (&state->vmvars.stackvalues[0] + state->vmvars.stacktop) - argc;
    result = cfunc(state, receiver, argc, vargs);
    state->vmvars.stacktop -= argc + 1;
    neon_vmbits_stackpush(state, result);
    if(state->vmvars.hasraised)
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
                    return neon_vmbits_callclosure(state, receiver, neon_value_asclosure(callee), argc);
                }
                break;
                /* Calls and Functions call-value < Closures call-value-closure */
                #if 0
            case NEON_OBJ_FUNCTION: // [switch]
                {
                    return neon_vmbits_callclosure(state, receiver, neon_value_asscriptfunction(callee), argc);
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

bool neon_vmbits_invokefromclass(NeonState* state, NeonValue receiver, NeonObjClass* klass, NeonObjString* name, int argc)
{
    NeonValue method;
    if(!neon_hashtable_get(klass->methods, name, &method))
    {
        neon_state_raiseerror(state, "undefined property '%s'.", name->sbuf->data);
        return false;
    }
    return neon_vmbits_callclosure(state, receiver, neon_value_asclosure(method), argc);
}

NeonObjClass* neon_value_getvalueclass(NeonState* state, NeonValue val)
{
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
        if(klass != NULL)
        {
            if(neon_class_getmethod(klass, name, dest))
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
            state->vmvars.stackvalues[state->vmvars.stacktop + (-argc - 1)] = callable;
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
            state->vmvars.stackvalues[state->vmvars.stacktop + (-argc - 1)] = callable;
            return neon_vmbits_callvalue(state, receiver, callable, argc);
        }
        neon_state_raiseerror(state, "cannot invoke method '%s' on non-instance object type <%s>", name->sbuf->data, neon_value_typename(receiver));
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

void neon_vmbits_closeupvals(NeonState* state, NeonValue* last)
{
    return;
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

static inline bool neon_vmexec_concat(NeonState* state)
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
    neon_writer_release(wr);
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

/*
static inline bool neon_vmexec_dobinary(NeonState* state, bool isbool, int32_t op)
{
    double b;
    double a;
    double dw;
    NeonValue res;
    NeonValue peeka;
    NeonValue peekb;
    peeka = neon_vmbits_stackpeek(state, 0);
    peekb = neon_vmbits_stackpeek(state, 1);
    if(!neon_value_isnumber(peeka) || !neon_value_isnumber(peekb))
    {
        neon_state_raiseerror(state, "expected number|number, but got (#0) (%d) <%s> | (#1) (%d) <%s>",
            peeka.type, neon_value_typename(peeka), peekb.type, neon_value_typename(peekb));
        return false;
    }
    b = neon_value_asnumber(neon_vmbits_stackpop(state));
    a = neon_value_asnumber(neon_vmbits_stackpop(state));
    switch(op)
    {
        case NEON_OP_PRIMGREATER:
            {
                dw = a > b;
            }
            break;
        case NEON_OP_PRIMLESS:
            {
                dw = a < b;
            }
            break;
        case NEON_OP_PRIMADD:
            {
                dw = a + b;
            }
            break;
        case NEON_OP_PRIMSUBTRACT:
            {
                dw = a - b;
            }
            break;
        case NEON_OP_PRIMMULTIPLY:
            {
                dw = a * b;
            }
            break;
        case NEON_OP_PRIMDIVIDE:
            {
                dw = a / b;
            }
            break;
        case NEON_OP_PRIMMODULO:
            {
                dw = fmod(a, b);
            }
            break;
        case NEON_OP_PRIMBINAND:
            {
                dw = (int)a & (int)b;
            }
            break;
        case NEON_OP_PRIMBINOR:
            {
                dw = (int)a | (int)b;
            }
            break;
            
        case NEON_OP_PRIMBINXOR:
            {
                dw = (int)a ^ (int)b;
            }
            break;
        case NEON_OP_PRIMSHIFTLEFT:
            {
                int leftsigned;
                unsigned int rightusigned;
                //dw = (int)a << (int)b;
                leftsigned = neon_vmutil_numtoint32(peekb);
                rightusigned = neon_vmutil_numtouint32(peeka);
                dw = leftsigned << (rightusigned & 0x1F);
            }
            break;

        case NEON_OP_PRIMSHIFTLEFT:
                {
                    leftsigned = vmutil_numtoint32(leftinval);
                    rightusigned = vmutil_numtouint32(rightinval);
                    numres = leftsigned << (rightusigned & 0x1F);
                }
                break;

        case NEON_OP_PRIMSHIFTRIGHT:
            {
                int leftsigned;
                unsigned int rightusigned;
                //dw = (int)a >> (int)b;
                leftsigned = neon_vmutil_numtoint32(peekb);
                rightusigned = neon_vmutil_numtouint32(peeka);
                dw = leftsigned >> (rightusigned & 0x1F);
            }
            break;
        default:
            {
                neon_state_raiseerror(state, "unrecognized instruction for binary");
                return false;
            }
            break;
    }
    if(isbool)
    {
        res = neon_value_makebool(dw);
    }
    else
    {
        res = neon_value_makenumber(dw);
    }
    neon_vmbits_stackpush(state, res);
    return true;
}
*/

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

static inline bool neon_vmexec_indexgetstring(NeonState* state, NeonObjString* os, NeonValue vidx, NeonValue* destval)
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
    return false;
}

static inline bool neon_vmexec_indexgetarray(NeonState* state, NeonObjArray* oa, NeonValue vidx, NeonValue* destval)
{
    long nidx;
    NeonValue val;
    if(!neon_value_isnumber(vidx))
    {
        neon_state_raiseerror(state, "cannot get array index with non-number type <%s>", neon_value_typename(vidx));
        return false;
    }
    nidx = neon_value_asnumber(vidx);
    if((nidx >= 0) && (nidx < (long)oa->vala->size))
    {
        val = oa->vala->values[nidx];
        *destval = val;
        return true;
    }
    return false;
}

static inline bool neon_vmexec_indexgetmap(NeonState* state, NeonObjMap* om, NeonValue vidx, NeonValue* destval)
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

static inline bool neon_vmexec_indexget(NeonState* state)
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
        if(neon_vmexec_indexgetstring(state, neon_value_asstring(targetobj), vidx, &destval))
        {
            ok = true;
        }
    }
    else if(neon_value_isarray(targetobj))
    {
        if(neon_vmexec_indexgetarray(state, neon_value_asarray(targetobj), vidx, &destval))
        {
            ok = true;
        }
    }
    else if(neon_value_ismap(targetobj))
    {
        if(neon_vmexec_indexgetmap(state, neon_value_asmap(targetobj), vidx, &destval))
        {
            ok = true;
        }
    }
    else
    {
        neon_state_raiseerror(state, "cannot get index object type <%s>", neon_value_typename(targetobj));
    }
    neon_vmbits_stackpush(state, destval);
    return ok;
}

static inline bool neon_vmexec_indexsetarray(NeonState* state, NeonObjArray* oa, NeonValue vidx, NeonValue setval)
{
    long nidx;
    if(!neon_value_isnumber(vidx))
    {
        neon_state_raiseerror(state, "cannot set array index with non-number type <%s>", neon_value_typename(vidx));
        return false;
    }
    nidx = neon_value_asnumber(vidx);
    /*
    neon_writer_writeformat(state->stderrwriter, "indexsetarray: nidx=%d, setval=<%s> ", nidx, neon_value_typename(setval));
    neon_writer_printvalue(state->stderrwriter, setval, true);
    neon_writer_writeformat(state->stderrwriter, "\n");
    */
    neon_valarray_insert(oa->vala, nidx, setval);
    return true;
}

static inline bool neon_vmexec_indexsetmap(NeonState* state, NeonObjMap* om, NeonValue vidx, NeonValue setval)
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

static inline bool neon_vmexec_indexset(NeonState* state)
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
        if(neon_vmexec_indexsetarray(state, neon_value_asarray(targetobj), vidx, setval))
        {
            ok = true;
        }
    }
    else if(neon_value_ismap(targetobj))
    {
        if(neon_vmexec_indexsetmap(state, neon_value_asmap(targetobj), vidx, setval))
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

static inline bool neon_vmexec_propertyget(NeonState* state)
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

static inline bool neon_vmexec_propertyset(NeonState* state)
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

static inline bool neon_vmexec_makearray(NeonState* state)
{
    int i;
    int count;
    NeonValue val;
    NeonObjArray* array;
    count = neon_vmbits_readbyte(state);
    array = neon_array_make(state);
    //state->vmvars.stackvalues[-count - 1] = neon_value_fromobject(array);
    //fprintf(stderr, "makearray: count=%d\n", count);
    for(i = count - 1; i >= 0; i--)
    {
        val = neon_vmbits_stackpeek(state, i);
        neon_array_push(array, val);
    }
    neon_vmbits_stackpopn(state, count);
    neon_vmbits_stackpush(state, neon_value_fromobject(array));
    return true;
}

static inline bool neon_vmexec_makemap(NeonState* state)
{
    int count;
    NeonObjMap* map;
    (void)count;
    count = neon_vmbits_readbyte(state);
    map = neon_object_makemap(state);
    neon_vmbits_stackpush(state, neon_value_fromobject(map));
    return true;
}


static inline bool neon_vmexec_globalstmt(NeonState* state)
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

static inline void neon_vm_debugprintvalue(NeonState* state, NeonWriter* owr, NeonValue val, const char* fmt, ...)
{
    va_list va;
    (void)state;
    va_start(va, fmt);
    neon_writer_vwriteformat(owr, fmt, va);
    neon_writer_writeformat(owr, " [<%s>] ", neon_value_typename(val));
    neon_writer_printvalue(owr, val, true);
    neon_writer_writeformat(owr, "\n");
    va_end(va);
}

static inline void neon_vm_debugprintstack(NeonState* state, NeonWriter* owr)
{
    int spos;
    int frompos;
    int nowpos;
    int64_t stacktop;
    NeonValue* slot;
    NeonValue* stv;
    stacktop = state->vmvars.stacktop;
    stv = state->vmvars.stackvalues;
    if(stacktop == -1)
    {
        stacktop = 0;
    }
    neon_writer_writeformat(owr, "  stack=[\n");
    frompos = 0;
    spos = 0;    
    for(slot = &stv[frompos]; slot < &stv[stacktop]; slot++)
    {
        nowpos = spos;
        spos++;
        neon_vm_debugprintvalue(state, owr, *slot, "    [%d]", (int)nowpos-0);
    }
    neon_writer_writeformat(owr, "  ]\n");
}

static inline void neon_vm_debugprintinstruction(NeonState* state, NeonWriter* owr, NeonCallFrame* frame)
{
    int ofs;
    NeonChunk* chnk;
    if(frame == NULL)
    {
        return;
    }
    chnk = frame->closure->innerfn->chunk;
    ofs = frame->instrucidx - 1;
    neon_dbg_dumpdisasm(state, owr, chnk, ofs);
    neon_vm_debugprintstack(state, owr);
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
    fprintf(stderr, "in neon_vm_runvm\n");
    owr = state->stderrwriter;
    state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];
    for(;;)
    {
        instruc = neon_vmbits_readinstruction(state);
        if(state->conf.shouldprintruntime)
        {
            neon_vm_debugprintinstruction(state, owr, state->vmvars.currframe);
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
            neon_writer_writeformat(state->stderrwriter, "pushconst: ");
            neon_writer_printvalue(state->stderrwriter, cval, true);
            neon_writer_writeformat(state->stderrwriter, "\n");
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
            if(!neon_vmexec_makearray(state))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_MAKEMAP)
        {
            if(!neon_vmexec_makemap(state))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_POP)
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
            actualpos = state->vmvars.currframe->frstackindex + (islot + 0);
            neon_vm_stackmaybegrow(state, actualpos);
            val = state->vmvars.stackvalues[actualpos];
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
            actualpos = state->vmvars.currframe->frstackindex + (islot + 0);
            neon_vm_stackmaybegrow(state, actualpos);
            state->vmvars.stackvalues[actualpos] = val;
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
                //neon_hashtable_delete(state, state->globals, name);// [delete]
                //neon_state_raiseerror(state, "undefined variable '%s'.", name->sbuf->data);
                //return NEON_STATUS_RUNTIMEERROR;
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
                    upidx = state->vmvars.currframe->frstackindex + index;
                    closure->upvalues[i] = neon_vmbits_captureupval(state, &state->vmvars.stackvalues[upidx], upidx);
                }
                else
                {
                    closure->upvalues[i] = state->vmvars.currframe->closure->upvalues[index];
                }
            }
        }
        vm_breakouter();
    op_case(NEON_OP_UPVALCLOSE)
        {
            NeonValue* vargs;
            vargs = (&state->vmvars.stackvalues[0] + state->vmvars.stacktop) - 1;
            neon_vmbits_closeupvals(state, vargs);
            neon_vmbits_stackpop(state);
        }
        vm_breakouter();
    op_case(NEON_OP_UPVALGET)
        {
            int32_t islot;
            NeonValue val;
            NeonObjClosure* cls;
            islot = neon_vmbits_readbyte(state);
            cls = state->vmvars.currframe->closure;
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
            state->vmvars.currframe->closure->upvalues[islot]->upindex = (idx);
            state->vmvars.currframe->closure->upvalues[islot]->location = peeked;
        }
        vm_breakouter();

    op_case(NEON_OP_INDEXGET)
        {
            if(!neon_vmexec_indexget(state))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_INDEXSET)
        {
            if(!neon_vmexec_indexset(state))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_PROPERTYGET)
        {
            if(!neon_vmexec_propertyget(state))
            {
                return NEON_STATUS_RUNTIMEERROR;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_PROPERTYSET)
        {
            if(!neon_vmexec_propertyset(state))
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
            neon_vmbits_stackpush(state, neon_value_makebool(neon_value_equal(a, b)));
        }
        vm_breakouter();
    op_case(NEON_OP_PRIMGREATER)
    op_case(NEON_OP_PRIMLESS)
        {
            if(!neon_vmexec_dobinary(state, true, instruc, NULL))
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
                if(!neon_vmexec_dobinary(state, false, instruc, NULL))
                {
                    return NEON_STATUS_RUNTIMEERROR;
                }
            }
            else
            {
                neon_vmexec_concat(state);
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
            if(!neon_vmexec_dobinary(state, false, instruc, NULL))
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
            neon_writer_writeformat(state->stderrwriter, "debug: ");
            neon_writer_printvalue(state->stderrwriter, val, true);
            neon_writer_writeformat(state->stderrwriter, "\n");
        }
        vm_breakouter();
    op_case(NEON_OP_GLOBALSTMT)
        {
            if(!neon_vmexec_globalstmt(state))
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
            tname = neon_value_typename(val);
            os = neon_string_copy(state, tname, strlen(tname));
            neon_vmbits_stackpush(state, neon_value_fromobject(os));
        }
        vm_breakouter();
    op_case(NEON_OP_JUMPNOW)
        {
            uint16_t offset;
            offset = neon_vmbits_readshort(state);
            state->vmvars.currframe->instrucidx += offset;
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
                state->vmvars.currframe->instrucidx += offset;
            }
        }
        vm_breakouter();
    op_case(NEON_OP_LOOP)
        {
            uint16_t offset;
            offset = neon_vmbits_readshort(state);
            state->vmvars.currframe->instrucidx -= offset;
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
            state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];
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
            state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];
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
            state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];
        }
        vm_breakouter();
    op_case(NEON_OP_RETURN)
        {
            int64_t usable;
            NeonValue result;
            result = neon_vmbits_stackpop(state);
            if(state->vmvars.currframe->frstackindex >= 0)
            {
                neon_vmbits_closeupvals(state, &state->vmvars.stackvalues[state->vmvars.currframe->frstackindex]);
            }
            state->vmvars.framecount--;
            if(state->vmvars.framecount == 0)
            {
                //neon_vmbits_stackpop(state);
                //fprintf(stderr, "returning due to NEON_OP_RETURN\n");
                return NEON_STATUS_OK;
            }
            usable = (state->vmvars.currframe->frstackindex - 0);
            state->vmvars.stacktop = usable;
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
                state->vmvars.currframe = &state->vmvars.framevalues[state->vmvars.framecount - 1];
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
            if(state->vmvars.havekeeper)
            {
                fprintf(stderr, "**restoring frame**\n");
                state->vmvars.currframe->frstackindex = state->vmvars.keepframe.frstackindex;
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




