

#include "neon.h"
#include "optparse.h"
#include "lino.h"


static char* nn_cli_getinput(linocontext_t* lictx, const char* prompt)
{
    return lino_context_readline(lictx, prompt);
}

static void nn_cli_addhistoryline(linocontext_t* lictx, const char* line)
{
    lino_context_historyadd(lictx, line);
}

static void nn_cli_freeline(linocontext_t* lictx, char* line)
{
    lino_context_freeline(lictx, line);
}

#if !defined(NEON_PLAT_ISWASM)
static bool nn_cli_repl(linocontext_t* lictx, NNState* state)
{
    enum { kMaxVarName = 512 };
    size_t i;
    size_t rescnt;
    int linelength;
    int bracecount;
    int parencount;
    int bracketcount;
    int doublequotecount;
    int singlequotecount;
    bool continuerepl;
    char* line;
    char varnamebuf[kMaxVarName];
    NNStringBuffer* source;
    const char* cursor;
    NNValue dest;
    NNPrinter* pr;
    pr = state->stdoutprinter;
    rescnt = 0;
    state->isrepl = true;
    continuerepl = true;
    printf("Type \".exit\" to quit or \".credits\" for more information\n");
    source = nn_strbuf_makebasicempty(NULL, 0);
    bracecount = 0;
    parencount = 0;
    bracketcount = 0;
    singlequotecount = 0;
    doublequotecount = 0;
    lino_context_setmultiline(lictx, 0);
    lino_context_historyadd(lictx, ".exit");
    while(true)
    {
        if(!continuerepl)
        {
            bracecount = 0;
            parencount = 0;
            bracketcount = 0;
            singlequotecount = 0;
            doublequotecount = 0;
            nn_strbuf_reset(source);
            continuerepl = true;
        }
        cursor = "%> ";
        if(bracecount > 0 || bracketcount > 0 || parencount > 0)
        {
            cursor = ".. ";
        }
        else if(singlequotecount == 1 || doublequotecount == 1)
        {
            cursor = "";
        }
        line = nn_cli_getinput(lictx, cursor);
        if(line == NULL || strcmp(line, ".exit") == 0)
        {
            nn_strbuf_destroy(source);
            return true;
        }
        linelength = (int)strlen(line);
        if(strcmp(line, ".credits") == 0)
        {
            printf("\n" NEON_INFO_COPYRIGHT "\n\n");
            nn_strbuf_reset(source);
            continue;
        }
        nn_cli_addhistoryline(lictx, line);
        if(linelength > 0 && line[0] == '#')
        {
            continue;
        }
        /* find count of { and }, ( and ), [ and ], " and ' */
        for(i = 0; i < (size_t)linelength; i++)
        {
            if(line[i] == '{')
            {
                bracecount++;
            }
            if(line[i] == '(')
            {
                parencount++;
            }
            if(line[i] == '[')
            {
                bracketcount++;
            }
            if(line[i] == '\'' && doublequotecount == 0)
            {
                if(singlequotecount == 0)
                {
                    singlequotecount++;
                }
                else
                {
                    singlequotecount--;
                }
            }
            if(line[i] == '"' && singlequotecount == 0)
            {
                if(doublequotecount == 0)
                {
                    doublequotecount++;
                }
                else
                {
                    doublequotecount--;
                }
            }
            if(line[i] == '\\' && (singlequotecount > 0 || doublequotecount > 0))
            {
                i++;
            }
            if(line[i] == '}' && bracecount > 0)
            {
                bracecount--;
            }
            if(line[i] == ')' && parencount > 0)
            {
                parencount--;
            }
            if(line[i] == ']' && bracketcount > 0)
            {
                bracketcount--;
            }
        }
        nn_strbuf_appendstr(source, line);
        if(linelength > 0)
        {
            nn_strbuf_appendstr(source, "\n");
        }
        nn_cli_freeline(lictx, line);
        if(bracketcount == 0 && parencount == 0 && bracecount == 0 && singlequotecount == 0 && doublequotecount == 0)
        {
            memset(varnamebuf, 0, kMaxVarName);
            sprintf(varnamebuf, "_%ld", (long)rescnt);
            nn_state_execsource(state, state->topmodule, nn_strbuf_data(source), "<repl>", &dest);
            dest = state->lastreplvalue;
            if(!nn_value_isnull(dest))
            {
                nn_printer_printf(pr, "%s = ", varnamebuf);
                nn_printer_printvalue(pr, dest, true, true);
                nn_state_defglobalvalue(state, varnamebuf, dest);
                nn_printer_printf(pr, "\n");
                rescnt++;
            }
            state->lastreplvalue = nn_value_makenull();
            fflush(stdout);
            continuerepl = false;
        }
    }
    return true;
}
#endif

static bool nn_cli_runfile(NNState* state, const char* file)
{
    size_t fsz;
    char* source;
    const char* oldfile;
    NNStatus result;
    source = nn_util_filereadfile(state, file, &fsz, false, 0);
    if(source == NULL)
    {
        oldfile = file;
        source = nn_util_filereadfile(state, file, &fsz, false, 0);
        if(source == NULL)
        {
            fprintf(stderr, "failed to read from '%s': %s\n", oldfile, strerror(errno));
            return false;
        }
    }
    result = nn_state_execsource(state, state->topmodule, source, file, NULL);
    nn_memory_free(source);
    fflush(stdout);
    if(result == NEON_STATUS_FAILCOMPILE)
    {
        return false;
    }
    if(result == NEON_STATUS_FAILRUNTIME)
    {
        return false;
    }
    return true;
}

static bool nn_cli_runcode(NNState* state, char* source)
{
    NNStatus result;
    state->rootphysfile = NULL;
    result = nn_state_execsource(state, state->topmodule, source, "<-e>", NULL);
    fflush(stdout);
    if(result == NEON_STATUS_FAILCOMPILE)
    {
        return false;
    }
    if(result == NEON_STATUS_FAILRUNTIME)
    {
        return false;
    }
    return true;
}

#if defined(NEON_PLAT_ISWASM)
int __multi3(int a, int b)
{
    return a*b;
}
#endif

static int nn_util_findfirstpos(const char* str, size_t len, int ch)
{
    size_t i;
    for(i=0; i<len; i++)
    {
        if(str[i] == ch)
        {
            return i;
        }
    }
    return -1;
}

static void nn_cli_parseenv(NNState* state, char** envp)
{
    enum { kMaxKeyLen = 40 };
    size_t i;
    int len;
    int pos;
    char* raw;
    char* valbuf;
    char keybuf[kMaxKeyLen];
    NNObjString* oskey;
    NNObjString* osval;
    if(envp == NULL)
    {
        return;
    }
    for(i=0; envp[i] != NULL; i++)
    {
        raw = envp[i];
        len = strlen(raw);
        pos = nn_util_findfirstpos(raw, len, '=');
        if(pos == -1)
        {
            fprintf(stderr, "malformed environment string '%s'\n", raw);
        }
        else
        {
            memset(keybuf, 0, kMaxKeyLen);
            memcpy(keybuf, raw, pos);
            valbuf = &raw[pos+1];
            oskey = nn_string_copycstr(state, keybuf);
            osval = nn_string_copycstr(state, valbuf);
            nn_dict_setentry(state->envdict, nn_value_fromobject(oskey), nn_value_fromobject(osval));
        }
    }
}

static void optprs_fprintmaybearg(FILE* out, const char* begin, const char* flagname, size_t flaglen, bool needval, bool maybeval, const char* delim)
{
    fprintf(out, "%s%.*s", begin, (int)flaglen, flagname);
    if(needval)
    {
        if(maybeval)
        {
            fprintf(out, "[");
        }
        if(delim != NULL)
        {
            fprintf(out, "%s", delim);
        }
        fprintf(out, "<val>");
        if(maybeval)
        {
            fprintf(out, "]");
        }
    }
}

static void optprs_fprintusage(FILE* out, optlongflags_t* flags)
{
    size_t i;
    char ch;
    bool needval;
    bool maybeval;
    bool hadshort;
    optlongflags_t* flag;
    for(i=0; flags[i].longname != NULL; i++)
    {
        flag = &flags[i];
        hadshort = false;
        needval = (flag->argtype > OPTPARSE_NONE);
        maybeval = (flag->argtype == OPTPARSE_OPTIONAL);
        if(flag->shortname > 0)
        {
            hadshort = true;
            ch = flag->shortname;
            fprintf(out, "    ");
            optprs_fprintmaybearg(out, "-", &ch, 1, needval, maybeval, NULL);
        }
        if(flag->longname != NULL)
        {
            if(hadshort)
            {
                fprintf(out, ", ");
            }
            else
            {
                fprintf(out, "    ");
            }
            optprs_fprintmaybearg(out, "--", flag->longname, strlen(flag->longname), needval, maybeval, "=");
        }
        if(flag->helptext != NULL)
        {
            fprintf(out, "  -  %s", flag->helptext);
        }
        fprintf(out, "\n");
    }
}

static void nn_cli_showusage(char* argv[], optlongflags_t* flags, bool fail)
{
    FILE* out;
    out = fail ? stderr : stdout;
    fprintf(out, "Usage: %s [<options>] [<filename> | -e <code>]\n", argv[0]);
    optprs_fprintusage(out, flags);
}

int main(int argc, char* argv[], char** envp)
{
    int i;
    int co;
    int opt;
    int nargc;
    int longindex;
    int nextgcstart;
    bool ok;
    bool wasusage;
    bool quitafterinit;
    char *arg;
    char* source;
    const char* filename;
    char* nargv[128];
    optcontext_t options;
    NNState* state;
    NNObjString* os;
    linocontext_t lictx;
    static optlongflags_t longopts[] =
    {
        {"help", 'h', OPTPARSE_NONE, "this help"},
        {"strict", 's', OPTPARSE_NONE, "enable strict mode, such as requiring explicit var declarations"},
        {"warn", 'w', OPTPARSE_NONE, "enable warnings"},
        {"debug", 'd', OPTPARSE_NONE, "enable debugging: print instructions and stack values during execution"},
        {"exitaftercompile", 'x', OPTPARSE_NONE, "when using '-d', quit after printing compiled function(s)"},
        {"eval", 'e', OPTPARSE_REQUIRED, "evaluate a single line of code"},
        {"quit", 'q', OPTPARSE_NONE, "initiate, then immediately destroy the interpreter state"},
        {"types", 't', OPTPARSE_NONE, "print sizeof() of types"},
        {"apidebug", 'a', OPTPARSE_NONE, "print calls to API (very verbose, very slow)"},
        {"gcstart", 'g', OPTPARSE_REQUIRED, "set minimum bytes at which the GC should kick in. 0 disables GC"},
        {0, 0, (optargtype_t)0, NULL}
    };
    lino_context_init(&lictx);
    nn_memory_init();
    #if defined(NEON_PLAT_ISWINDOWS) || defined(_MSC_VER)
        _setmode(fileno(stdin), _O_BINARY);
        _setmode(fileno(stdout), _O_BINARY);
        _setmode(fileno(stderr), _O_BINARY);
    #endif
    ok = true;
    wasusage = false;
    quitafterinit = false;
    source = NULL;
    nextgcstart = NEON_CONFIG_DEFAULTGCSTART;
    state = nn_state_makealloc();
    if(state == NULL)
    {
        fprintf(stderr, "failed to create state\n");
        return 0;
    }
    nargc = 0;
    optprs_init(&options, argc, argv);
    options.permute = 0;
    while ((opt = optprs_nextlongflag(&options, longopts, &longindex)) != -1)
    {
        co = longopts[longindex].shortname;
        if(opt == '?')
        {
            printf("%s: %s\n", argv[0], options.errmsg);
        }
        else if(co == 'g')
        {
            nextgcstart = atol(options.optarg);
        }
        else if(co == 'h')
        {
            nn_cli_showusage(argv, longopts, false);
            wasusage = true;
        }
        else if(co == 'd' || co == 'j')
        {
            state->conf.dumpbytecode = true;
            state->conf.shoulddumpstack = true;        
        }
        else if(co == 'x')
        {
            state->conf.exitafterbytecode = true;
        }
        else if(co == 'a')
        {
            state->conf.enableapidebug = true;
        }
        else if(co == 's')
        {
            state->conf.enablestrictmode = true;            
        }
        else if(co == 'e')
        {
            source = options.optarg;
        }
        else if(co == 'w')
        {
            state->conf.enablewarnings = true;
        }
        else if(co == 'q')
        {
            quitafterinit = true;
        }
    }
    if(wasusage || quitafterinit)
    {
        goto cleanup;
    }
    nn_cli_parseenv(state, envp);
    while(true)
    {
        arg = optprs_nextpositional(&options);
        if(arg == NULL)
        {
            break;
        }
        nargv[nargc] = arg;
        nargc++;
    }
    {
        for(i=0; i<nargc; i++)
        {
            os = nn_string_copycstr(state, nargv[i]);
            nn_array_push(state->processinfo->cliargv, nn_value_fromobject(os));

        }
        nn_valtable_set(&state->declaredglobals, nn_value_copystr(state, "ARGV"), nn_value_fromobject(state->processinfo->cliargv));
    }
    state->gcstate.nextgc = nextgcstart;
    nn_import_loadbuiltinmodules(state);
    if(source != NULL)
    {
        ok = nn_cli_runcode(state, source);
    }
    else if(nargc > 0)
    {
        os = nn_value_asstring(nn_valarray_get(&state->processinfo->cliargv->varray, 0));
        filename = nn_string_getdata(os);
        ok = nn_cli_runfile(state, filename);
    }
    else
    {
        ok = nn_cli_repl(&lictx, state);
    }
    cleanup:
    nn_state_destroy(state, false);
    nn_memory_finish();
    if(ok)
    {
        return 0;
    }
    return 1;
}

/**
* this function is used by clang-repl ONLY. don't call it directly, or bad things will happen!
*/
int replmain(const char* file)
{
    const char* deffile;
    deffile = "mandel1.nn";
    if(file != NULL)
    {
        deffile = file;
    }
    char* realargv[1024] = {(char*)"a.out", (char*)deffile, NULL};
    return main(1, realargv, NULL);
}
