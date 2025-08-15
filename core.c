

#include "neon.h"

NNValue nn_argcheck_vfail(NNArgCheck* ch, const char* srcfile, int srcline, const char* fmt, va_list va)
{
    #if 0
        nn_vm_stackpopn(ch->pstate, ch->argc);
    #endif
    if(!nn_except_vthrowwithclass(ch->pstate, ch->pstate->exceptions.argumenterror, srcfile, srcline, fmt, va))
    {
    }
    return nn_value_makebool(false);
}

NNValue nn_argcheck_fail(NNArgCheck* ch, const char* srcfile, int srcline, const char* fmt, ...)
{
    NNValue v;
    va_list va;
    va_start(va, fmt);
    v = nn_argcheck_vfail(ch, srcfile, srcline, fmt, va);
    va_end(va);
    return v;
}

void nn_argcheck_init(NNState* state, NNArgCheck* ch, const char* name, NNValue* argv, size_t argc)
{
    ch->pstate = state;
    ch->argc = argc;
    ch->argv = argv;
    ch->name = name;
}

NNProperty nn_property_makewithpointer(NNState* state, NNValue val, NNFieldType type)
{
    NNProperty vf;
    (void)state;
    memset(&vf, 0, sizeof(NNProperty));
    vf.type = type;
    vf.value = val;
    vf.havegetset = false;
    return vf;
}

NNProperty nn_property_makewithgetset(NNState* state, NNValue val, NNValue getter, NNValue setter, NNFieldType type)
{
    bool getisfn;
    bool setisfn;
    NNProperty np;
    np = nn_property_makewithpointer(state, val, type);
    setisfn = nn_value_iscallable(setter);
    getisfn = nn_value_iscallable(getter);
    if(getisfn || setisfn)
    {
        np.getset.setter = setter;
        np.getset.getter = getter;
    }
    return np;
}

NNProperty nn_property_make(NNState* state, NNValue val, NNFieldType type)
{
    return nn_property_makewithpointer(state, val, type);
}

void nn_state_installmethods(NNState* state, NNObjClass* klass, NNConstClassMethodItem* listmethods)
{
    int i;
    const char* rawname;
    NNNativeFN rawfn;
    NNObjString* osname;
    for(i=0; listmethods[i].name != NULL; i++)
    {
        rawname = listmethods[i].name;
        rawfn = listmethods[i].fn;
        osname = nn_string_copycstr(state, rawname);
        nn_class_defnativemethod(klass, osname, rawfn);
    }
}

void nn_state_initbuiltinmethods(NNState* state)
{
    nn_state_installobjectprocess(state);
    nn_state_installobjectobject(state);
    nn_state_installobjectnumber(state);
    nn_state_installobjectstring(state);
    nn_state_installobjectarray(state);
    nn_state_installobjectdict(state);
    nn_state_installobjectfile(state);
    nn_state_installobjectrange(state);
    nn_state_installmodmath(state);
}

/**
* see @nn_state_warn
*/
void nn_state_vwarn(NNState* state, const char* fmt, va_list va)
{
    if(state->conf.enablewarnings)
    {
        fprintf(stderr, "WARNING: ");
        vfprintf(stderr, fmt, va);
        fprintf(stderr, "\n");
    }
}

/**
* print a non-fatal runtime warning.
*/
void nn_state_warn(NNState* state, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    nn_state_vwarn(state, fmt, va);
    va_end(va);
}

/**
* procuce a stacktrace array; it is an object array, because it used both internally and in scripts.
* cannot take any shortcuts here.
*/
NNValue nn_except_getstacktrace(NNState* state)
{
    int line;
    int64_t i;
    size_t instruction;
    const char* fnname;
    const char* physfile;
    NNCallFrame* frame;
    NNObjFunction* function;
    NNObjString* os;
    NNObjArray* oa;
    NNPrinter pr;
    oa = nn_object_makearray(state);
    {
        for(i = 0; i < state->vmstate.framecount; i++)
        {
            nn_printer_makestackstring(state, &pr);
            frame = &state->vmstate.framevalues[i];
            function = frame->closure->fnclosure.scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->fnscriptfunc.blob.instrucs - 1;
            line = function->fnscriptfunc.blob.instrucs[instruction].srcline;
            physfile = "(unknown)";
            if(function->fnscriptfunc.module->physicalpath != NULL)
            {
                physfile = function->fnscriptfunc.module->physicalpath->sbuf.data;
            }
            fnname = "<script>";
            if(function->name != NULL)
            {
                fnname = function->name->sbuf.data;
            }
            nn_printer_printf(&pr, "from %s() in %s:%d", fnname, physfile, line);
            os = nn_printer_takestring(&pr);
            nn_printer_destroy(&pr);
            nn_array_push(oa, nn_value_fromobject(os));
            if((i > 15) && (state->conf.showfullstack == false))
            {
                nn_printer_makestackstring(state, &pr);
                nn_printer_printf(&pr, "(only upper 15 entries shown)");
                os = nn_printer_takestring(&pr);
                nn_printer_destroy(&pr);
                nn_array_push(oa, nn_value_fromobject(os));
                break;
            }
        }
        return nn_value_fromobject(oa);
    }
    return nn_value_fromobject(nn_string_copylen(state, "", 0));
}

/**
* when an exception occured that was not caught, it is handled here.
*/
bool nn_except_propagate(NNState* state)
{
    int i;
    int cnt;
    int srcline;
    const char* colred;
    const char* colreset;
    const char* colyellow;
    const char* colblue;
    const char* srcfile;
    NNValue stackitm;
    NNObjArray* oa;
    NNObjFunction* function;
    NNExceptionFrame* handler;
    NNObjString* emsg;
    NNObjString* tmp;
    NNObjInstance* exception;
    NNProperty* field;
    exception = nn_value_asinstance(nn_vm_stackpeek(state, 0));
    /* look for a handler .... */
    while(state->vmstate.framecount > 0)
    {
        state->vmstate.currentframe = &state->vmstate.framevalues[state->vmstate.framecount - 1];
        for(i = state->vmstate.currentframe->handlercount; i > 0; i--)
        {
            handler = &state->vmstate.currentframe->handlers[i - 1];
            function = state->vmstate.currentframe->closure->fnclosure.scriptfunc;
            if(handler->address != 0 && nn_util_isinstanceof(exception->klass, handler->klass))
            {
                state->vmstate.currentframe->inscode = &function->fnscriptfunc.blob.instrucs[handler->address];
                return true;
            }
            else if(handler->finallyaddress != 0)
            {
                /* continue propagating once the 'finally' block completes */
                nn_vm_stackpush(state, nn_value_makebool(true));
                state->vmstate.currentframe->inscode = &function->fnscriptfunc.blob.instrucs[handler->finallyaddress];
                return true;
            }
        }
        state->vmstate.framecount--;
    }
    /* at this point, the exception is unhandled; so, print it out. */
    colred = nn_util_color(NEON_COLOR_RED);
    colblue = nn_util_color(NEON_COLOR_BLUE);
    colreset = nn_util_color(NEON_COLOR_RESET);
    colyellow = nn_util_color(NEON_COLOR_YELLOW);
    nn_printer_printf(state->debugwriter, "%sunhandled %s%s", colred, exception->klass->name->sbuf.data, colreset);
    srcfile = "none";
    srcline = 0;
    field = nn_valtable_getfieldbycstr(&exception->properties, "srcline");
    if(field != NULL)
    {
        /* why does this happen? */
        if(nn_value_isnumber(field->value))
        {
            srcline = nn_value_asnumber(field->value);
        }
    }
    field = nn_valtable_getfieldbycstr(&exception->properties, "srcfile");
    if(field != NULL)
    {
        if(nn_value_isstring(field->value))
        {
            tmp = nn_value_asstring(field->value);
            srcfile = tmp->sbuf.data;
        }
    }
    nn_printer_printf(state->debugwriter, " [from native %s%s:%d%s]", colyellow, srcfile, srcline, colreset);
    field = nn_valtable_getfieldbycstr(&exception->properties, "message");
    if(field != NULL)
    {
        emsg = nn_value_tostring(state, field->value);
        if(emsg->sbuf.length > 0)
        {
            nn_printer_printf(state->debugwriter, ": %s", emsg->sbuf.data);
        }
        else
        {
            nn_printer_printf(state->debugwriter, ":");
        }
    }
    nn_printer_printf(state->debugwriter, "\n");
    field = nn_valtable_getfieldbycstr(&exception->properties, "stacktrace");
    if(field != NULL)
    {
        nn_printer_printf(state->debugwriter, "%sstacktrace%s:\n", colblue, colreset);
        oa = nn_value_asarray(field->value);
        cnt = nn_valarray_count(&oa->varray);
        i = cnt-1;
        if(cnt > 0)
        {
            while(true)
            {
                stackitm = nn_valarray_get(&oa->varray, i);
                nn_printer_printf(state->debugwriter, "%s", colyellow);
                nn_printer_printf(state->debugwriter, "  ");
                nn_printer_printvalue(state->debugwriter, stackitm, false, true);
                nn_printer_printf(state->debugwriter, "%s\n", colreset);
                if(i == 0)
                {
                    break;
                }
                i--;
            }
        }
    }
    return false;
}

/**
* push an exception handler, assuming it does not exceed NEON_CONFIG_MAXEXCEPTHANDLERS.
*/
bool nn_except_pushhandler(NNState* state, NNObjClass* type, int address, int finallyaddress)
{
    NNCallFrame* frame;
    frame = &state->vmstate.framevalues[state->vmstate.framecount - 1];
    if(frame->handlercount == NEON_CONFIG_MAXEXCEPTHANDLERS)
    {
        nn_state_raisefatalerror(state, "too many nested exception handlers in one function");
        return false;
    }
    frame->handlers[frame->handlercount].address = address;
    frame->handlers[frame->handlercount].finallyaddress = finallyaddress;
    frame->handlers[frame->handlercount].klass = type;
    frame->handlercount++;
    return true;
}


bool nn_except_vthrowactual(NNState* state, NNObjClass* klass, const char* srcfile, int srcline, const char* format, va_list va)
{
    bool b;
    b = nn_except_vthrowwithclass(state, klass, srcfile, srcline, format, va);
    return b;
}

bool nn_except_throwactual(NNState* state, NNObjClass* klass, const char* srcfile, int srcline, const char* format, ...)
{
    bool b;
    va_list va;
    va_start(va, format);
    b = nn_except_vthrowactual(state, klass, srcfile, srcline, format, va);
    va_end(va);
    return b;
}

/**
* throw an exception class. technically, any class can be thrown, but you should really only throw
* those deriving Exception.
*/
bool nn_except_throwwithclass(NNState* state, NNObjClass* klass, const char* srcfile, int srcline, const char* format, ...)
{
    bool b;
    va_list args;
    va_start(args, format);
    b = nn_except_vthrowwithclass(state, klass, srcfile, srcline, format, args);
    va_end(args);
    return b;
}

bool nn_except_vthrowwithclass(NNState* state, NNObjClass* exklass, const char* srcfile, int srcline, const char* format, va_list args)
{
    enum { kMaxBufSize = 1024};
    int length;
    char* message;
    NNValue stacktrace;
    NNObjInstance* instance;
    message = (char*)nn_memory_malloc(kMaxBufSize+1);
    length = vsnprintf(message, kMaxBufSize, format, args);
    instance = nn_except_makeinstance(state, exklass, srcfile, srcline, nn_string_takelen(state, message, length));
    nn_vm_stackpush(state, nn_value_fromobject(instance));
    stacktrace = nn_except_getstacktrace(state);
    nn_vm_stackpush(state, stacktrace);
    nn_instance_defproperty(instance, nn_string_copycstr(state, "stacktrace"), stacktrace);
    nn_vm_stackpop(state);
    return nn_except_propagate(state);
}

/**
* helper for nn_except_makeclass.
*/
NNInstruction nn_util_makeinst(bool isop, uint8_t code, int srcline)
{
    NNInstruction inst;
    inst.isop = isop;
    inst.code = code;
    inst.srcline = srcline;
    return inst;
}

/**
* generate bytecode for a nativee exception class.
* script-side it is enough to just derive from Exception, of course.
*/
NNObjClass* nn_except_makeclass(NNState* state, NNObjModule* module, const char* cstrname, bool iscs)
{
    int messageconst;
    NNObjClass* klass;
    NNObjString* classname;
    NNObjFunction* function;
    NNObjFunction* closure;
    if(iscs)
    {
        classname = nn_string_copycstr(state, cstrname);
    }
    else
    {
        classname = nn_string_copycstr(state, cstrname);
    }
    nn_vm_stackpush(state, nn_value_fromobject(classname));
    klass = nn_object_makeclass(state, classname, state->classprimobject);
    nn_vm_stackpop(state);
    nn_vm_stackpush(state, nn_value_fromobject(klass));
    function = nn_object_makefuncscript(state, module, NEON_FNCONTEXTTYPE_METHOD);
    function->fnscriptfunc.arity = 1;
    function->fnscriptfunc.isvariadic = false;
    nn_vm_stackpush(state, nn_value_fromobject(function));
    {
        /* g_loc 0 */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, (0 >> 8) & 0xff, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, 0 & 0xff, 0));
    }
    {
        /* g_loc 1 */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, (1 >> 8) & 0xff, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, 1 & 0xff, 0));
    }
    {
        messageconst = nn_blob_pushconst(&function->fnscriptfunc.blob, nn_value_fromobject(nn_string_copycstr(state, "message")));
        /* s_prop 0 */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_PROPERTYSET, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, (messageconst >> 8) & 0xff, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, messageconst & 0xff, 0));
    }
    {
        /* pop */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_POPONE, 0));
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_POPONE, 0));
    }
    {
        /* g_loc 0 */
        /*
        //  nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_LOCALGET, 0));
        //  nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, (0 >> 8) & 0xff, 0));
        //  nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(false, 0 & 0xff, 0));
        */
    }
    {
        /* ret */
        nn_blob_push(&function->fnscriptfunc.blob, nn_util_makeinst(true, NEON_OP_RETURN, 0));
    }
    closure = nn_object_makefuncclosure(state, function);
    nn_vm_stackpop(state);
    /* set class constructor */
    nn_vm_stackpush(state, nn_value_fromobject(closure));
    nn_valtable_set(&klass->instmethods, nn_value_fromobject(classname), nn_value_fromobject(closure));
    klass->constructor = nn_value_fromobject(closure);
    /* set class properties */
    nn_class_defproperty(klass, nn_string_copycstr(state, "message"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_copycstr(state, "stacktrace"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_copycstr(state, "srcfile"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_copycstr(state, "srcline"), nn_value_makenull());
    nn_class_defproperty(klass, nn_string_copycstr(state, "class"), nn_value_fromobject(klass));
    nn_valtable_set(&state->declaredglobals, nn_value_fromobject(classname), nn_value_fromobject(klass));
    /* for class */
    nn_vm_stackpop(state);
    nn_vm_stackpop(state);
    /* assert error name */
    /* nn_vm_stackpop(state); */
    return klass;
}

/**
* create an instance of an exception class.
*/
NNObjInstance* nn_except_makeinstance(NNState* state, NNObjClass* exklass, const char* srcfile, int srcline, NNObjString* message)
{
    NNObjInstance* instance;
    NNObjString* osfile;
    instance = nn_object_makeinstance(state, exklass);
    osfile = nn_string_copycstr(state, srcfile);
    nn_vm_stackpush(state, nn_value_fromobject(instance));
    nn_instance_defproperty(instance, nn_string_copycstr(state, "class"), nn_value_fromobject(exklass));
    nn_instance_defproperty(instance, nn_string_copycstr(state, "message"), nn_value_fromobject(message));
    nn_instance_defproperty(instance, nn_string_copycstr(state, "srcfile"), nn_value_fromobject(osfile));
    nn_instance_defproperty(instance, nn_string_copycstr(state, "srcline"), nn_value_makenumber(srcline));
    nn_vm_stackpop(state);
    return instance;
}

/**
* raise a fatal error that cannot recover.
*/
void nn_state_raisefatalerror(NNState* state, const char* format, ...)
{
    int i;
    int line;
    size_t instruction;
    va_list args;
    NNCallFrame* frame;
    NNObjFunction* function;
    /* flush out anything on stdout first */
    fflush(stdout);
    frame = &state->vmstate.framevalues[state->vmstate.framecount - 1];
    function = frame->closure->fnclosure.scriptfunc;
    instruction = frame->inscode - function->fnscriptfunc.blob.instrucs - 1;
    line = function->fnscriptfunc.blob.instrucs[instruction].srcline;
    fprintf(stderr, "RuntimeError: ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, " -> %s:%d ", function->fnscriptfunc.module->physicalpath->sbuf.data, line);
    fputs("\n", stderr);
    if(state->vmstate.framecount > 1)
    {
        fprintf(stderr, "stacktrace:\n");
        for(i = state->vmstate.framecount - 1; i >= 0; i--)
        {
            frame = &state->vmstate.framevalues[i];
            function = frame->closure->fnclosure.scriptfunc;
            /* -1 because the IP is sitting on the next instruction to be executed */
            instruction = frame->inscode - function->fnscriptfunc.blob.instrucs - 1;
            fprintf(stderr, "    %s:%d -> ", function->fnscriptfunc.module->physicalpath->sbuf.data, function->fnscriptfunc.blob.instrucs[instruction].srcline);
            if(function->name == NULL)
            {
                fprintf(stderr, "<script>");
            }
            else
            {
                fprintf(stderr, "%s()", function->name->sbuf.data);
            }
            fprintf(stderr, "\n");
        }
    }
    nn_state_resetvmstate(state);
}

bool nn_state_defglobalvalue(NNState* state, const char* name, NNValue val)
{
    bool r;
    NNObjString* oname;
    oname = nn_string_copycstr(state, name);
    nn_vm_stackpush(state, nn_value_fromobject(oname));
    nn_vm_stackpush(state, val);
    r = nn_valtable_set(&state->declaredglobals, state->vmstate.stackvalues[0], state->vmstate.stackvalues[1]);
    nn_vm_stackpopn(state, 2);
    return r;
}

bool nn_state_defnativefunctionptr(NNState* state, const char* name, NNNativeFN fptr, void* uptr)
{
    NNObjFunction* func;
    func = nn_object_makefuncnative(state, fptr, name, uptr);
    return nn_state_defglobalvalue(state, name, nn_value_fromobject(func));
}

bool nn_state_defnativefunction(NNState* state, const char* name, NNNativeFN fptr)
{
    return nn_state_defnativefunctionptr(state, name, fptr, NULL);
}

NNObjClass* nn_util_makeclass(NNState* state, const char* name, NNObjClass* parent)
{
    NNObjClass* cl;
    NNObjString* os;
    os = nn_string_copycstr(state, name);
    cl = nn_object_makeclass(state, os, parent);
    nn_valtable_set(&state->declaredglobals, nn_value_fromobject(os), nn_value_fromobject(cl));
    return cl;
}



void nn_state_buildprocessinfo(NNState* state)
{
    enum{ kMaxBuf = 1024 };
    char* pathp;
    char pathbuf[kMaxBuf];
    state->processinfo = (NNProcessInfo*)nn_memory_malloc(sizeof(NNProcessInfo));
    state->processinfo->cliscriptfile = NULL;
    state->processinfo->cliscriptdirectory = NULL;
    state->processinfo->cliargv = nn_object_makearray(state);
    {
        pathp = osfn_getcwd(pathbuf, kMaxBuf);
        if(pathp == NULL)
        {
            pathp = (char*)".";
        }
        fprintf(stderr, "pathp=<%s>\n", pathp);
        state->processinfo->cliexedirectory = nn_string_copycstr(state, pathp);
    }
    {
        state->processinfo->cliprocessid = osfn_getpid();
    }
    {
        {
            state->processinfo->filestdout = nn_object_makefile(state, stdout, true, "<stdout>", "wb");
            nn_state_defglobalvalue(state, "STDOUT", nn_value_fromobject(state->processinfo->filestdout));
        }
        {
            state->processinfo->filestderr = nn_object_makefile(state, stderr, true, "<stderr>", "wb");
            nn_state_defglobalvalue(state, "STDERR", nn_value_fromobject(state->processinfo->filestderr));
        }
        {
            state->processinfo->filestdin = nn_object_makefile(state, stdin, true, "<stdin>", "rb");
            nn_state_defglobalvalue(state, "STDIN", nn_value_fromobject(state->processinfo->filestdin));
        }
    }
}

void nn_state_updateprocessinfo(NNState* state)
{
    char* prealpath;
    char* prealdir;
    if(state->rootphysfile != NULL)
    {
        prealpath = osfn_realpath(state->rootphysfile, NULL);
        prealdir = osfn_dirname(prealpath);
        state->processinfo->cliscriptfile = nn_string_copycstr(state, prealpath);
        state->processinfo->cliscriptdirectory = nn_string_copycstr(state, prealdir);
        nn_memory_free(prealpath);
        nn_memory_free(prealdir);
    }
    if(state->processinfo->cliscriptdirectory != NULL)
    {
        nn_state_addmodulesearchpathobj(state, state->processinfo->cliscriptdirectory);
    }
}


bool nn_state_makestack(NNState* pstate)
{
    return nn_state_makewithuserptr(pstate, NULL);
}

NNState* nn_state_makealloc()
{
    NNState* state;
    state = (NNState*)nn_memory_malloc(sizeof(NNState));
    if(state == NULL)
    {
        return NULL;
    }
    if(!nn_state_makewithuserptr(state, NULL))
    {
        return NULL;
    }
    return state;
}

bool nn_state_makewithuserptr(NNState* pstate, void* userptr)
{
    if(pstate == NULL)
    {
        return false;
    }
    memset(pstate, 0, sizeof(NNState));
    pstate->memuserptr = userptr;
    pstate->exceptions.stdexception = NULL;
    pstate->rootphysfile = NULL;
    pstate->processinfo = NULL;
    pstate->isrepl = false;
    pstate->markvalue = true;
    nn_vm_initvmstate(pstate);
    nn_state_resetvmstate(pstate);
    /*
    * setup default config
    */
    {
        pstate->conf.enablestrictmode = false;
        pstate->conf.shoulddumpstack = false;
        pstate->conf.enablewarnings = false;
        pstate->conf.dumpbytecode = false;
        pstate->conf.exitafterbytecode = false;
        pstate->conf.showfullstack = false;
        pstate->conf.enableapidebug = false;
        pstate->conf.maxsyntaxerrors = NEON_CONFIG_MAXSYNTAXERRORS;
    }
    /*
    * initialize GC state
    */
    {
        pstate->gcstate.bytesallocated = 0;
        /* default is 1mb. Can be modified via the -g flag. */
        pstate->gcstate.nextgc = NEON_CONFIG_DEFAULTGCSTART;
        pstate->gcstate.graycount = 0;
        pstate->gcstate.graycapacity = 0;
        pstate->gcstate.graystack = NULL;
        pstate->lastreplvalue = nn_value_makenull();
    }
    /*
    * initialize various printer instances
    */
    {
        pstate->stdoutprinter = nn_printer_makeio(pstate, stdout, false);
        pstate->stdoutprinter->shouldflush = true;
        pstate->stderrprinter = nn_printer_makeio(pstate, stderr, false);
        pstate->debugwriter = nn_printer_makeio(pstate, stderr, false);
        pstate->debugwriter->shortenvalues = true;
        pstate->debugwriter->maxvallength = 15;
    }
    /*
    * initialize runtime tables
    */
    {
        nn_valtable_init(pstate, &pstate->openedmodules);
        nn_valtable_init(pstate, &pstate->allocatedstrings);
        nn_valtable_init(pstate, &pstate->declaredglobals);
    }
    /*
    * initialize the toplevel module
    */
    {
        pstate->topmodule = nn_module_make(pstate, "", "<state>", false, true);
        pstate->constructorname = nn_string_copycstr(pstate, "constructor");
    }
    /*
    * declare default classes
    */
    {
        pstate->classprimclass = nn_util_makeclass(pstate, "Class", NULL);
        pstate->classprimobject = nn_util_makeclass(pstate, "Object", pstate->classprimclass);
        pstate->classprimnumber = nn_util_makeclass(pstate, "Number", pstate->classprimobject);
        pstate->classprimstring = nn_util_makeclass(pstate, "String", pstate->classprimobject);
        pstate->classprimarray = nn_util_makeclass(pstate, "Array", pstate->classprimobject);
        pstate->classprimdict = nn_util_makeclass(pstate, "Dict", pstate->classprimobject);
        pstate->classprimfile = nn_util_makeclass(pstate, "File", pstate->classprimobject);
        pstate->classprimrange = nn_util_makeclass(pstate, "Range", pstate->classprimobject);
        pstate->classprimcallable = nn_util_makeclass(pstate, "Function", pstate->classprimobject);
        pstate->classprimprocess = nn_util_makeclass(pstate, "Process", pstate->classprimobject);
    }
    /*
    * declare environment variables dictionary
    */
    {
        pstate->envdict = nn_object_makedict(pstate);
    }
    /*
    * declare default exception types
    */
    {
        if(pstate->exceptions.stdexception == NULL)
        {
            pstate->exceptions.stdexception = nn_except_makeclass(pstate, NULL, "Exception", true);
        }
        pstate->exceptions.asserterror = nn_except_makeclass(pstate, NULL, "AssertError", true);
        pstate->exceptions.syntaxerror = nn_except_makeclass(pstate, NULL, "SyntaxError", true);
        pstate->exceptions.ioerror = nn_except_makeclass(pstate, NULL, "IOError", true);
        pstate->exceptions.oserror = nn_except_makeclass(pstate, NULL, "OSError", true);
        pstate->exceptions.argumenterror = nn_except_makeclass(pstate, NULL, "ArgumentError", true);
        pstate->exceptions.regexerror = nn_except_makeclass(pstate, NULL, "RegexError", true);
        pstate->exceptions.importerror = nn_except_makeclass(pstate, NULL, "ImportError", true);
    }
    /* all the other bits .... */
    nn_state_buildprocessinfo(pstate);
    /* NOW the module paths can be set up */
    nn_state_setupmodulepaths(pstate);
    {
        nn_state_initbuiltinfunctions(pstate);
        nn_state_initbuiltinmethods(pstate);
    }
    return true;
}

#if 0
    #define destrdebug(...) \
        { \
            fprintf(stderr, "in nn_state_destroy: "); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
        }
#else
    #define destrdebug(...)
#endif
void nn_state_destroy(NNState* state, bool onstack)
{
    destrdebug("destroying importpath...");
    nn_valarray_destroy(&state->importpath, false);
    destrdebug("destroying linked objects...");
    nn_gcmem_destroylinkedobjects(state);
    /* since object in module can exist in declaredglobals, it must come before */
    destrdebug("destroying module table...");
    nn_valtable_destroy(&state->openedmodules);
    destrdebug("destroying globals table...");
    nn_valtable_destroy(&state->declaredglobals);
    destrdebug("destroying strings table...");
    nn_valtable_destroy(&state->allocatedstrings);
    destrdebug("destroying stdoutprinter...");
    nn_printer_destroy(state->stdoutprinter);
    destrdebug("destroying stderrprinter...");
    nn_printer_destroy(state->stderrprinter);
    destrdebug("destroying debugwriter...");
    nn_printer_destroy(state->debugwriter);
    destrdebug("destroying framevalues...");
    nn_memory_free(state->vmstate.framevalues);
    destrdebug("destroying stackvalues...");
    nn_memory_free(state->vmstate.stackvalues);
    nn_memory_free(state->processinfo);
    destrdebug("destroying state...");
    if(!onstack)
    {
        nn_memory_free(state);
    }
    destrdebug("done destroying!");
}

bool nn_util_methodisprivate(NNObjString* name)
{
    return name->sbuf.length > 0 && name->sbuf.data[0] == '_';
}


NNObjFunction* nn_state_compilesource(NNState* state, NNObjModule* module, bool fromeval, const char* source, bool toplevel)
{
    NNBlob blob;
    NNObjFunction* function;
    NNObjFunction* closure;
    (void)toplevel;
    nn_blob_init(state, &blob);
    function = nn_astparser_compilesource(state, module, source, &blob, false, fromeval);
    if(function == NULL)
    {
        nn_blob_destroy(&blob);
        return NULL;
    }
    if(!fromeval)
    {
        nn_vm_stackpush(state, nn_value_fromobject(function));
    }
    else
    {
        function->name = nn_string_copycstr(state, "(evaledcode)");
    }
    closure = nn_object_makefuncclosure(state, function);
    if(!fromeval)
    {
        nn_vm_stackpop(state);
        nn_vm_stackpush(state, nn_value_fromobject(closure));
    }
    nn_blob_destroy(&blob);
    return closure;
}

NNStatus nn_state_execsource(NNState* state, NNObjModule* module, const char* source, const char* filename, NNValue* dest)
{
    char* rp;
    NNStatus status;
    NNObjFunction* closure;
    state->rootphysfile = filename;
    nn_state_updateprocessinfo(state);
    rp = (char*)filename;
    state->topmodule->physicalpath = nn_string_copycstr(state, rp);
    nn_module_setfilefield(state, module);
    closure = nn_state_compilesource(state, module, false, source, true);
    if(closure == NULL)
    {
        return NEON_STATUS_FAILCOMPILE;
    }
    if(state->conf.exitafterbytecode)
    {
        return NEON_STATUS_OK;
    }
    nn_vm_callclosure(state, closure, nn_value_makenull(), 0);
    status = nn_vm_runvm(state, 0, dest);
    return status;
}

NNValue nn_state_evalsource(NNState* state, const char* source)
{
    bool ok;
    int argc;
    NNValue callme;
    NNValue retval;
    NNObjFunction* closure;
    (void)argc;
    closure = nn_state_compilesource(state, state->topmodule, true, source, false);
    callme = nn_value_fromobject(closure);
    argc = nn_nestcall_prepare(state, callme, nn_value_makenull(), NULL, 0);
    ok = nn_nestcall_callfunction(state, callme, nn_value_makenull(), NULL, 0, &retval);
    if(!ok)
    {
        nn_except_throw(state, "eval() failed");
    }
    return retval;
}



